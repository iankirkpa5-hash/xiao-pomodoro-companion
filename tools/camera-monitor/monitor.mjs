import { LineFramer, classifyDiagnostic, parseCamError, runCapture } from "./protocol.mjs";

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
};

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

function renderHistory() {
  const strip = $("history-strip");
  $("history-count").textContent = `${history.length} / 6 張`;
  strip.textContent = "";
  if (history.length === 0) {
    const empty = document.createElement("p");
    empty.className = "small";
    empty.textContent = "目前尚無暫存照片。最多保留最近 6 張，重新整理頁面會清空。";
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
    label.textContent = `${item.timeLabel} · ${item.state.length} B`;
    btn.appendChild(thumbImg);
    btn.appendChild(label);
    btn.addEventListener("click", () => selectShot(index));
    strip.appendChild(btn);
  });
}

function selectShot(index) {
  if (index < 0 || index >= history.length) return;
  selectedIndex = index;
  const item = history[index];
  $("empty-state").hidden = true;
  $("preview-image").src = item.url;
  $("preview-image").hidden = false;
  $("meta-size").textContent = `${item.state.width} × ${item.state.height} JPEG`;
  $("meta-bytes").textContent = `${item.state.length.toLocaleString()} bytes`;
  $("meta-capture-ms").textContent = `${item.state.captureMs} ms`;
  $("meta-verify").textContent = `PASS (${item.state.crc32.toString(16).padStart(8, "0")})`;
  $("viewer-subtitle").textContent =
    `拍攝時間 ${item.timeLabel} · 可用 PSRAM ${Math.round(item.info.freePsram / 1024).toLocaleString()} KiB`;
  updateTransform();
  renderHistory();
  renderControls();
}

function renderControls() {
  const connected = port !== null && !closing;
  const hasShot = selectedIndex >= 0 && selectedIndex < history.length;
  $("connect").disabled = !supported || opening || port !== null;
  $("disconnect").disabled = !connected || opening;
  $("capture").disabled = !connected || capturing;
  $("download").disabled = !hasShot;
  $("rotate").disabled = !hasShot;
  $("mirror").disabled = !hasShot;

  const badge = $("status-badge");
  if (!connected) {
    badge.dataset.state = "idle";
    badge.textContent = hasShot ? "已中斷 · 保留預覽" : "等待連線";
  } else if (capturing) {
    badge.dataset.state = "busy";
    badge.textContent = "拍攝／傳輸中…";
  } else {
    badge.dataset.state = "ready";
    badge.textContent = "已連線 · 可按需拍照";
  }

  $("diagnostic-summary").textContent =
    `重試 ${stats.retries} · 拒收封包 ${stats.rejected} · I2C 錯誤 ${stats.i2cErrors} · 異常 ${stats.faults}`;
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

function handleIncomingLine(line) {
  if (!line) return;
  appendLog(line);
  const diag = classifyDiagnostic(line);
  if (diag.i2cError) stats.i2cErrors++;
  if (diag.fault) stats.faults++;

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
      await writer.write(encoder.encode(`\n${command}\n`));
      return await waitPromise;
    } catch (error) {
      if (error.message.startsWith("設備回報錯誤：") || error.message === "串口已中斷" || error.message === "連線已中斷") {
        throw error;
      }
      if (attempt + 1 >= attempts) throw error;
    }
  }
  throw new Error(`無法取得有效回覆：${command}`);
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
      renderControls();
    }
  } catch (error) {
    if (!closing) failure = `串口讀取中斷：${error.message}`;
  } finally {
    abortActiveWaiter("連線已中斷");
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
    appendLog("[網頁] 已連接 XIAO USB 串口（未自動啟動鏡頭）。");
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

$("capture").addEventListener("click", async () => {
  if (!port || closing || capturing) return;
  capturing = true;
  setMessage();
  const session = (sessionSeed = (sessionSeed % 0xfffffffe) + 1);
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

    const blob = new Blob([result.jpeg], { type: "image/jpeg" });
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
      info: result.info,
      state: result.state,
      verification: result.verification,
    });
    if (history.length > 6) {
      const dropped = history.pop();
      URL.revokeObjectURL(dropped.url);
    }
    selectShot(0);
    appendLog(
      `[網頁] 拍照完成：${result.state.width}x${result.state.height}, ${result.state.length} bytes, ` +
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
  for (const item of history) URL.revokeObjectURL(item.url);
});

renderControls();
