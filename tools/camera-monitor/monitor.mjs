import {
  LineFramer,
  StreamFrameAssembler,
  classifyDiagnostic,
  parseCamError,
  parseStreamPong,
  parseStreamStarted,
  parseStreamStopped,
  runCapture,
} from "./protocol.mjs";

const $ = id => document.getElementById(id);
const supported = window.isSecureContext && "serial" in navigator;
const encoder = new TextEncoder();

let port = null;
let reader = null;
let writer = null;
let readTask = null;
let opening = false;
let closing = false;
let capturing = false;

let streaming = false;
let streamBusy = false;
let streamSession = 0;
let streamAssembler = null;
let streamPingTimer = null;
let streamTickTimer = null;
let streamEndsAt = 0;
let liveUrl = null;
let latestLiveFrame = null;
let liveFrameTimes = [];
let latestTempC = null;

let activeWaiter = null;
let sessionSeed = Math.floor(Math.random() * 0x7fffffff) + 1;

const logs = [];
const history = [];
let selectedIndex = -1;
let rotationDeg = 0;
let mirrored = false;

const stats = {
  retries: 0,
  rejected: 0,
  i2cErrors: 0,
  faults: 0,
  overlong: 0,
  droppedFrames: 0,
};

function nextSessionId() {
  sessionSeed = (sessionSeed % 0xfffffffe) + 1;
  return sessionSeed;
}

function formatTemp(tempC) {
  return tempC === null || tempC === undefined ? "—" : `${tempC.toFixed(1)} °C`;
}

function formatCountdown(ms) {
  const sec = Math.max(0, Math.ceil(ms / 1000));
  const m = String(Math.floor(sec / 60)).padStart(2, "0");
  const s = String(sec % 60).padStart(2, "0");
  return `${m}:${s}`;
}

function setMessage(text = "") {
  $("message").textContent = text;
  $("message").hidden = !text;
}

function appendLog(line) {
  logs.push(line);
  if (logs.length > 120) logs.shift();
  const el = $("raw-log");
  const atBottom = el.scrollTop + el.clientHeight >= el.scrollHeight - 20;
  el.textContent = logs.join("\n");
  if (atBottom) el.scrollTop = el.scrollHeight;
}

function updateTransform() {
  const img = $("preview-image");
  const scaleX = mirrored ? -1 : 1;
  img.style.transform = `rotate(${rotationDeg}deg) scaleX(${scaleX})`;
  $("rotate").textContent = `旋轉 ${rotationDeg}°`;
  $("mirror").textContent = mirrored ? "鏡像：開" : "鏡像：關";
  $("mirror").setAttribute("aria-pressed", String(mirrored));
}

function computeFps() {
  const now = performance.now();
  liveFrameTimes = liveFrameTimes.filter(t => now - t <= 3000);
  if (liveFrameTimes.length < 2) return liveFrameTimes.length ? 1 : 0;
  const spanSec = (liveFrameTimes[liveFrameTimes.length - 1] - liveFrameTimes[0]) / 1000;
  return spanSec > 0 ? (liveFrameTimes.length - 1) / spanSec : 0;
}

function renderHistory() {
  const strip = $("history-strip");
  $("history-count").textContent = `${history.length} / 6 張`;
  strip.textContent = "";
  if (history.length === 0) {
    const empty = document.createElement("p");
    empty.className = "small";
    empty.textContent = "按「拍一張」留下的照片會出現在這裡（最多 6 張），重新整理頁面會清空。";
    strip.appendChild(empty);
    return;
  }
  history.forEach((item, index) => {
    const btn = document.createElement("button");
    btn.type = "button";
    btn.className = "thumb" + (index === selectedIndex ? " selected" : "");
    const thumbImg = document.createElement("img");
    thumbImg.src = item.url;
    thumbImg.alt = `拍攝於 ${item.timeLabel}`;
    const label = document.createElement("span");
    label.textContent = `${item.timeLabel} · ${item.length} B`;
    btn.appendChild(thumbImg);
    btn.appendChild(label);
    btn.addEventListener("click", () => selectShot(index));
    strip.appendChild(btn);
  });
}

function pushSavedShot({ jpeg, width, height, length, crc32, captureMs, tempC, source }) {
  const blob = new Blob([jpeg], { type: "image/jpeg" });
  const url = URL.createObjectURL(blob);
  const now = new Date();
  const pad = n => String(n).padStart(2, "0");
  const timeLabel = `${pad(now.getHours())}:${pad(now.getMinutes())}:${pad(now.getSeconds())}`;
  const filename =
    `xiao-cam-${now.getFullYear()}${pad(now.getMonth() + 1)}${pad(now.getDate())}-` +
    `${pad(now.getHours())}${pad(now.getMinutes())}${pad(now.getSeconds())}.jpg`;

  history.unshift({
    url,
    blob,
    filename,
    timeLabel,
    width,
    height,
    length,
    crc32,
    captureMs,
    tempC,
    source,
  });
  if (history.length > 6) {
    const dropped = history.pop();
    URL.revokeObjectURL(dropped.url);
  }
  selectedIndex = 0;
  renderHistory();
  if (!streaming) {
    showShotInStage(history[0]);
  }
  renderControls();
}

function showShotInStage(item) {
  $("empty-state").hidden = true;
  $("preview-image").src = item.url;
  $("preview-image").hidden = false;
  $("meta-size").textContent = `${item.width} × ${item.height} JPEG`;
  $("meta-bytes").textContent = `${item.length.toLocaleString()} bytes`;
  $("meta-temp").textContent = formatTemp(item.tempC ?? latestTempC);
  $("meta-verify").textContent = `PASS (${item.crc32.toString(16).padStart(8, "0")})`;
  $("viewer-subtitle").textContent =
    item.source === "live-snap"
      ? `預覽快門存檔 ${item.timeLabel}`
      : `單張拍攝 ${item.timeLabel} · 耗時 ${item.captureMs} ms`;
  updateTransform();
}

function selectShot(index) {
  if (index < 0 || index >= history.length) return;
  selectedIndex = index;
  renderHistory();
  if (!streaming) {
    showShotInStage(history[index]);
  }
  renderControls();
}

function clearStreamTimers() {
  if (streamPingTimer) {
    clearInterval(streamPingTimer);
    streamPingTimer = null;
  }
  if (streamTickTimer) {
    clearInterval(streamTickTimer);
    streamTickTimer = null;
  }
}

function endStreamState(reasonText = "") {
  clearStreamTimers();
  streaming = false;
  streamBusy = false;
  streamSession = 0;
  streamAssembler = null;
  liveFrameTimes = [];
  $("live-pill").hidden = true;
  if (reasonText) {
    $("viewer-subtitle").textContent = reasonText;
  }
  if (selectedIndex >= 0 && selectedIndex < history.length) {
    showShotInStage(history[selectedIndex]);
  }
  renderControls();
}

function renderControls() {
  const connected = port !== null && !closing;
  const hasShot = selectedIndex >= 0 && selectedIndex < history.length;
  const hasAnyImage = !$("preview-image").hidden;

  $("connect").disabled = !supported || opening || port !== null;
  $("disconnect").disabled = !connected || opening;

  const previewBtn = $("preview-toggle");
  previewBtn.disabled = !connected || capturing || streamBusy;
  previewBtn.textContent = streaming ? "停止預覽" : "開始即時預覽";
  previewBtn.classList.toggle("danger-btn", streaming);
  previewBtn.classList.toggle("primary", !streaming);

  const captureBtn = $("capture");
  captureBtn.disabled = !connected || capturing || streamBusy || (streaming && !latestLiveFrame);
  captureBtn.textContent = streaming ? "拍一張（存下目前畫面）" : "拍一張";

  $("download").disabled = !hasShot;
  $("rotate").disabled = !hasAnyImage;
  $("mirror").disabled = !hasAnyImage;

  const badge = $("status-badge");
  if (!connected) {
    badge.dataset.state = "idle";
    badge.textContent = hasShot ? "已中斷 · 保留照片" : "等待連線";
  } else if (streaming) {
    badge.dataset.state = "ready";
    badge.textContent = "即時預覽中 · 同步畫面";
  } else if (capturing || streamBusy) {
    badge.dataset.state = "busy";
    badge.textContent = "處理中…";
  } else {
    badge.dataset.state = "ready";
    badge.textContent = "已連線 · 鏡頭已關閉";
  }

  const dropped = stats.droppedFrames + (streamAssembler ? streamAssembler.droppedFrames : 0);
  $("diagnostic-summary").textContent =
    `重試 ${stats.retries} · 略過幀 ${dropped} · I2C 錯誤 ${stats.i2cErrors} · 異常 ${stats.faults}`;
}

function setProgress(visible, done = 0, total = 1, text = "") {
  $("progress-wrap").hidden = !visible;
  if (!visible) return;
  const pct = total > 0 ? Math.min(100, Math.round((done / total) * 100)) : 0;
  $("progress-bar").style.width = `${pct}%`;
  $("progress-text").textContent = text || `${done.toLocaleString()} / ${total.toLocaleString()} bytes (${pct}%)`;
}

function abortActiveWaiter(reason) {
  if (!activeWaiter) return;
  const waiter = activeWaiter;
  activeWaiter = null;
  clearTimeout(waiter.timer);
  waiter.reject(new Error(reason));
}

function handleLiveFrame(frame) {
  latestLiveFrame = frame;
  if (frame.tempC !== null) latestTempC = frame.tempC;
  liveFrameTimes.push(performance.now());
  const fps = computeFps();

  const blob = new Blob([frame.jpeg], { type: "image/jpeg" });
  const nextUrl = URL.createObjectURL(blob);
  const oldUrl = liveUrl;
  liveUrl = nextUrl;

  $("empty-state").hidden = true;
  $("preview-image").src = nextUrl;
  $("preview-image").hidden = false;
  $("live-pill").hidden = false;
  $("live-pill").textContent = fps > 0 ? `LIVE · ${fps.toFixed(1)} FPS` : "LIVE";
  updateTransform();
  if (oldUrl) URL.revokeObjectURL(oldUrl);

  $("meta-size").textContent = `${frame.width} × ${frame.height} · ${fps.toFixed(1)} fps`;
  $("meta-bytes").textContent = `${frame.length.toLocaleString()} bytes`;
  $("meta-temp").textContent = formatTemp(latestTempC);
  $("meta-verify").textContent = `倒數 ${formatCountdown(streamEndsAt - performance.now())}`;
  $("viewer-subtitle").textContent =
    `即時同步取景中（第 #${frame.seq} 幀）· 按「拍一張」直接存檔，停止即 deinit`;
  renderControls();
}

function handleIncomingLine(line) {
  if (!line) return;
  const isStreamData = line.startsWith("CAM FDATA ") || line.startsWith("CAM FEND ");
  if (!isStreamData) appendLog(line);

  const diag = classifyDiagnostic(line);
  if (diag.i2cError) stats.i2cErrors++;
  if (diag.fault) stats.faults++;

  if (streaming && streamAssembler) {
    const pong = parseStreamPong(line, streamSession);
    if (pong) {
      streamEndsAt = performance.now() + pong.remainingMs;
      if (pong.tempC !== null) latestTempC = pong.tempC;
      $("meta-temp").textContent = formatTemp(latestTempC);
      $("meta-verify").textContent = `倒數 ${formatCountdown(pong.remainingMs)}`;
    }
    const stopped = parseStreamStopped(line, streamSession);
    if (stopped && !activeWaiter) {
      stats.droppedFrames += streamAssembler.droppedFrames;
      const msg =
        stopped.reason === "MAX_TIME"
          ? "已達 3 分鐘安全上限，板端自動停止預覽並完成 deinit。"
          : stopped.reason === "WATCHDOG"
            ? "心跳逾時，板端已自動停止預覽並完成 deinit。"
            : `預覽已停止（${stopped.reason}）。`;
      appendLog(`[網頁] ${msg}`);
      endStreamState(msg);
      return;
    }
    const frame = streamAssembler.feed(line);
    if (frame) {
      handleLiveFrame(frame);
      return;
    }
  }

  if (!activeWaiter) return;
  const waiter = activeWaiter;
  const deviceErr = parseCamError(line, waiter.session);
  if (deviceErr) {
    activeWaiter = null;
    clearTimeout(waiter.timer);
    waiter.reject(new Error(`設備回報錯誤：${deviceErr.reason}`));
    return;
  }
  try {
    const parsed = waiter.parse(line);
    if (parsed !== null && parsed !== undefined) {
      activeWaiter = null;
      clearTimeout(waiter.timer);
      waiter.resolve(parsed);
    }
  } catch (error) {
    stats.rejected++;
    appendLog(`[網頁] 略過無效封包：${error.message}`);
  }
}

async function sendLineRaw(line) {
  if (!port || closing || !writer) throw new Error("串口已中斷");
  await writer.write(encoder.encode(`\n${line}\n`));
}

async function sendRequest(session, command, parse, { timeoutMs = 1200, attempts = 3 } = {}) {
  for (let attempt = 0; attempt < attempts; attempt++) {
    if (!port || closing || !writer) {
      throw new Error("串口已中斷");
    }
    if (attempt > 0) stats.retries++;
    try {
      const waitPromise = new Promise((resolve, reject) => {
        const timer = setTimeout(() => {
          if (activeWaiter && activeWaiter.timer === timer) {
            activeWaiter = null;
          }
          reject(new Error(`等待回覆逾時：${command}`));
        }, timeoutMs);
        activeWaiter = { session, parse, resolve, reject, timer };
      });
      await sendLineRaw(command);
      return await waitPromise;
    } catch (error) {
      if (
        error.message.startsWith("設備回報錯誤：") ||
        error.message === "串口已中斷" ||
        error.message === "連線已中斷"
      ) {
        throw error;
      }
      if (attempt + 1 >= attempts) throw error;
    }
  }
  throw new Error(`無法取得有效回覆：${command}`);
}

async function startLivePreview() {
  if (!port || closing || streaming || capturing || streamBusy) return;
  streamBusy = true;
  setMessage();
  const session = nextSessionId();
  renderControls();
  try {
    const started = await sendRequest(
      session,
      `CAM STREAM START ${session} 150`,
      line => parseStreamStarted(line, session),
      { timeoutMs: 4000, attempts: 2 },
    );
    streaming = true;
    streamSession = session;
    streamAssembler = new StreamFrameAssembler(session);
    latestLiveFrame = null;
    liveFrameTimes = [];
    streamEndsAt = performance.now() + started.maxMs;
    if (started.tempC !== null) latestTempC = started.tempC;
    $("meta-temp").textContent = formatTemp(latestTempC);
    $("meta-verify").textContent = `倒數 ${formatCountdown(started.maxMs)}`;
    $("live-pill").hidden = false;
    $("live-pill").textContent = "LIVE · 啟動中";
    $("viewer-subtitle").textContent = "即時預覽已啟動，等待第一幀…";

    streamPingTimer = setInterval(() => {
      if (!streaming || !port || closing) return;
      sendLineRaw(`CAM STREAM PING ${streamSession}`).catch(() => {});
    }, 3000);
    streamTickTimer = setInterval(() => {
      if (!streaming) return;
      $("meta-verify").textContent = `倒數 ${formatCountdown(streamEndsAt - performance.now())}`;
    }, 1000);
    appendLog(`[網頁] 已啟動即時預覽（間隔 ${started.intervalMs} ms，安全上限 ${Math.round(started.maxMs / 1000)} 秒）。`);
  } catch (error) {
    setMessage(`無法啟動即時預覽：${error.message}`);
    appendLog(`[網頁] 啟動預覽失敗：${error.message}`);
  } finally {
    streamBusy = false;
    renderControls();
  }
}

async function stopLivePreview() {
  if (!streaming || streamBusy) return;
  streamBusy = true;
  const session = streamSession;
  if (streamAssembler) stats.droppedFrames += streamAssembler.droppedFrames;
  clearStreamTimers();
  renderControls();
  try {
    if (port && !closing) {
      await sendRequest(
        session,
        `CAM STREAM STOP ${session}`,
        line => parseStreamStopped(line, session),
        { timeoutMs: 2000, attempts: 3 },
      );
    }
    appendLog("[網頁] 已停止即時預覽，板端已執行 esp_camera_deinit()。");
  } catch (error) {
    appendLog(`[網頁] 停止預覽回覆異常（10 秒 watchdog 仍會自動 deinit）：${error.message}`);
  } finally {
    endStreamState("即時預覽已停止（鏡頭已 deinit）");
  }
}

async function readLoop(selected) {
  const decoder = new TextDecoder();
  const framer = new LineFramer();
  let failure = "";
  try {
    if (!selected.readable) throw new Error("串口沒有可讀取的資料流。");
    reader = selected.readable.getReader();
    while (!closing) {
      const { value, done } = await reader.read();
      if (done) break;
      if (!value) continue;
      const { lines, discarded } = framer.push(decoder.decode(value, { stream: true }));
      if (discarded) {
        stats.overlong += discarded;
        appendLog(`[網頁] 已略過 ${discarded} 行過長資料。`);
      }
      for (const line of lines) handleIncomingLine(line);
    }
  } catch (error) {
    if (!closing) failure = `串口讀取中斷：${error.message}`;
  } finally {
    abortActiveWaiter("連線已中斷");
    if (streaming) endStreamState("串口已中斷");
    if (reader) {
      try { reader.releaseLock(); } catch { /* ignore */ }
      reader = null;
    }
    if (writer) {
      try { writer.releaseLock(); } catch { /* ignore */ }
      writer = null;
    }
    try { await selected.close(); } catch { /* unplugged */ }
    if (port === selected) port = null;
    closing = false;
    capturing = false;
    setProgress(false);
    $("connection-state").textContent = "已中斷連線";
    if (failure) setMessage(failure);
    renderControls();
  }
}

async function disconnect() {
  if (!port || closing) return;
  if (streaming) {
    try { await stopLivePreview(); } catch { /* ignore */ }
  }
  closing = true;
  renderControls();
  abortActiveWaiter("連線已中斷");
  if (reader) {
    try { await reader.cancel(); } catch { /* ignore */ }
  }
  if (readTask) {
    await readTask;
    readTask = null;
  }
}

$("connect").addEventListener("click", async () => {
  if (!supported || opening || port) return;
  opening = true;
  setMessage();
  renderControls();
  try {
    const selected = await navigator.serial.requestPort();
    await selected.open({ baudRate: 115200 });
    if (!selected.writable) throw new Error("串口不支援寫入，無法傳送拍照指令。");
    port = selected;
    writer = selected.writable.getWriter();
    $("connection-state").textContent = "已連線（115200 baud）";
    appendLog("[網頁] 已連接 XIAO USB 串口（開機與連線時鏡頭預設關閉）。");
    readTask = readLoop(selected);
  } catch (error) {
    if (error && error.name !== "NotFoundError") {
      setMessage(`無法連接串口：${error.message}`);
    }
  } finally {
    opening = false;
    renderControls();
  }
});

$("disconnect").addEventListener("click", disconnect);

$("preview-toggle").addEventListener("click", () => {
  if (streaming) stopLivePreview();
  else startLivePreview();
});

$("capture").addEventListener("click", async () => {
  if (!port || closing || capturing || streamBusy) return;

  // Instant shutter from live preview stream.
  if (streaming && latestLiveFrame) {
    const frame = latestLiveFrame;
    pushSavedShot({
      jpeg: frame.jpeg.slice(),
      width: frame.width,
      height: frame.height,
      length: frame.length,
      crc32: frame.crc32,
      captureMs: 0,
      tempC: frame.tempC,
      source: "live-snap",
    });
    appendLog(
      `[網頁] 已從即時預覽存下第 #${frame.seq} 幀（${frame.length} bytes，CRC32=${frame.crc32.toString(16).padStart(8, "0")}）。`,
    );
    return;
  }

  // Single-shot capture with immediate deinit when preview is off.
  capturing = true;
  setMessage();
  const session = nextSessionId();
  setProgress(true, 0, 1, "正在檢查 CAM INFO…");
  renderControls();
  try {
    const result = await runCapture({
      session,
      request: (cmd, parse, opts) => sendRequest(session, cmd, parse, opts),
      onStage: (stage, detail) => {
        if (stage === "capture") {
          setProgress(true, 0, 1, "正在拍照並立即 deinit…");
        } else if (stage === "download" && detail?.state) {
          setProgress(true, 0, detail.state.length, `下載中 0 / ${detail.state.length} bytes…`);
        } else if (stage === "released" && detail && !detail.released) {
          appendLog(`[網頁] CAM RELEASE 未確認：${detail.releaseError}`);
        }
      },
      onProgress: (done, total) => setProgress(true, done, total),
    });

    pushSavedShot({
      jpeg: result.jpeg,
      width: result.state.width,
      height: result.state.height,
      length: result.state.length,
      crc32: result.state.crc32,
      captureMs: result.state.captureMs,
      tempC: latestTempC,
      source: "single-capture",
    });
    appendLog(
      `[網頁] 單張拍照完成：${result.state.width}x${result.state.height}, ${result.state.length} bytes, ` +
      `capture=${result.state.captureMs} ms, CRC32=${result.state.crc32.toString(16).padStart(8, "0")} PASS`,
    );
  } catch (error) {
    setMessage(`拍照失敗：${error.message}`);
    appendLog(`[網頁] 拍照失敗：${error.message}`);
  } finally {
    capturing = false;
    setProgress(false);
    renderControls();
  }
});

$("download").addEventListener("click", () => {
  if (selectedIndex < 0 || selectedIndex >= history.length) return;
  const item = history[selectedIndex];
  const link = document.createElement("a");
  link.href = item.url;
  link.download = item.filename;
  link.click();
});

$("rotate").addEventListener("click", () => {
  rotationDeg = (rotationDeg + 90) % 360;
  updateTransform();
});

$("mirror").addEventListener("click", () => {
  mirrored = !mirrored;
  updateTransform();
});

if (!supported) {
  setMessage("此瀏覽器不支援 Web Serial，或不是安全來源（請使用桌面版 Chrome / Edge 開啟 http://127.0.0.1:8766）。");
}

window.addEventListener("beforeunload", () => {
  if (streaming && port && writer && !closing) {
    try { writer.write(encoder.encode(`\nCAM STREAM STOP ${streamSession}\n`)); } catch { /* ignore */ }
  }
  if (liveUrl) URL.revokeObjectURL(liveUrl);
  for (const item of history) URL.revokeObjectURL(item.url);
});

renderControls();
