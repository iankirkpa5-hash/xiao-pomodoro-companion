import {
  LineFramer,
  StreamFrameAssembler,
  classifyDiagnostic,
  parseCamError,
  parseCamTuning,
  parseStreamPong,
  parseStreamStarted,
  parseStreamStopped,
  runCapture,
} from "./protocol.mjs";
import { MotionDetector, rgbaToLuma } from "./vision.mjs";

const $ = id => document.getElementById(id);
const supported = window.isSecureContext && "serial" in navigator;
const recorderSupported = typeof MediaRecorder !== "undefined" && typeof HTMLCanvasElement !== "undefined";
const encoder = new TextEncoder();

const MOTION_W = 64;
const MOTION_H = 48;
const MOTION_SCALE_MAX = 25; // Visual meter full scale = 25% changed pixels.

let port = null;
let reader = null;
let writer = null;
let readTask = null;
let opening = false;
let closing = false;
let capturing = false;
let tuningBusy = false;

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

let timelapseRunning = false;
let timelapseDone = 0;
let timelapseTotal = 0;
let timelapseNextAt = 0;
let timelapseTimer = null;
let timelapseTickTimer = null;

// Browser-side motion detection state
let motionEnabled = false;
let motionBusy = false;
const motionDetector = new MotionDetector({
  width: MOTION_W,
  height: MOTION_H,
  triggerPercent: 4,
  cooldownMs: 5000,
  warmupFrames: 3,
});
const motionCanvas = document.createElement("canvas");
motionCanvas.width = MOTION_W;
motionCanvas.height = MOTION_H;
const motionCtx = motionCanvas.getContext("2d", { willReadFrequently: true });

// Browser-side WebM recording state
let recordingActive = false;
let mediaRecorder = null;
let recordChunks = [];
let recordFrames = 0;
let recordStartedAt = 0;
let recordTickTimer = null;
const recordCanvas = document.createElement("canvas");
recordCanvas.width = 320;
recordCanvas.height = 240;
const recordCtx = recordCanvas.getContext("2d");

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

function formatElapsed(ms) {
  const sec = Math.max(0, Math.floor(ms / 1000));
  const m = String(Math.floor(sec / 60)).padStart(2, "0");
  const s = String(sec % 60).padStart(2, "0");
  return `${m}:${s}`;
}

function timestampSlug() {
  const now = new Date();
  const pad = n => String(n).padStart(2, "0");
  return (
    `${now.getFullYear()}${pad(now.getMonth() + 1)}${pad(now.getDate())}-` +
    `${pad(now.getHours())}${pad(now.getMinutes())}${pad(now.getSeconds())}`
  );
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
  const frame = $("stage-frame");
  const scaleX = mirrored ? -1 : 1;
  frame.style.transform = `rotate(${rotationDeg}deg) scaleX(${scaleX})`;
  $("rotate").textContent = `旋轉 ${rotationDeg}°`;
  $("mirror").textContent = mirrored ? "鏡像：開" : "鏡像：關";
  $("mirror").setAttribute("aria-pressed", String(mirrored));
}

function updateMotionThresholdUi() {
  const pct = Number($("motion-threshold").value) || 4;
  motionDetector.triggerPercent = pct;
  $("val-motion-threshold").textContent = `${pct}%`;
  const markLeft = Math.min(100, Math.round((pct / MOTION_SCALE_MAX) * 100));
  $("motion-threshold-mark").style.left = `${markLeft}%`;
  if (!motionEnabled) {
    $("motion-readout").textContent = `變化 0.0% / 門檻 ${pct.toFixed(1)}%`;
  }
}

function clearMotionOverlay() {
  const overlay = $("motion-overlay");
  const ctx = overlay.getContext("2d");
  ctx.clearRect(0, 0, overlay.width, overlay.height);
  overlay.hidden = true;
}

function drawMotionOverlay(box, triggered) {
  const overlay = $("motion-overlay");
  const ctx = overlay.getContext("2d");
  ctx.clearRect(0, 0, overlay.width, overlay.height);
  if (!box) {
    overlay.hidden = true;
    return;
  }
  overlay.hidden = false;
  ctx.lineWidth = triggered ? 3 : 2;
  ctx.strokeStyle = triggered ? "#ef5350" : "#26c6da";
  ctx.fillStyle = triggered ? "rgba(239, 83, 80, 0.14)" : "rgba(38, 198, 218, 0.08)";
  const x = box.x * overlay.width;
  const y = box.y * overlay.height;
  const w = box.w * overlay.width;
  const h = box.h * overlay.height;
  ctx.fillRect(x, y, w, h);
  ctx.strokeRect(x, y, w, h);
}

function applyTuningToUi(t) {
  if (!t) return;
  $("tune-brightness").value = String(t.brightness);
  $("val-brightness").textContent = t.brightness > 0 ? `+${t.brightness}` : String(t.brightness);
  $("tune-ae").value = String(t.ae);
  $("val-ae").textContent = t.ae > 0 ? `+${t.ae}` : String(t.ae);
  $("tune-contrast").value = String(t.contrast);
  $("val-contrast").textContent = t.contrast > 0 ? `+${t.contrast}` : String(t.contrast);
  $("tune-saturation").value = String(t.saturation);
  $("val-saturation").textContent = t.saturation > 0 ? `+${t.saturation}` : String(t.saturation);
  $("tune-wb").value = String(t.wb);
  $("tune-hmirror").checked = Boolean(t.hmirror);
  $("tune-vflip").checked = Boolean(t.vflip);
}

function computeFps() {
  const now = performance.now();
  liveFrameTimes = liveFrameTimes.filter(t => now - t <= 3000);
  if (liveFrameTimes.length < 2) return liveFrameTimes.length ? 1 : 0;
  const spanSec = (liveFrameTimes[liveFrameTimes.length - 1] - liveFrameTimes[0]) / 1000;
  return spanSec > 0 ? (liveFrameTimes.length - 1) / spanSec : 0;
}

function triggerBlobDownload(url, filename) {
  const link = document.createElement("a");
  link.href = url;
  link.download = filename;
  link.click();
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

function pushSavedShot({ jpeg, width, height, length, crc32, captureMs, tempC, source, filenamePrefix = "xiao-cam", autoDownload = false }) {
  const blob = new Blob([jpeg], { type: "image/jpeg" });
  const url = URL.createObjectURL(blob);
  const now = new Date();
  const pad = n => String(n).padStart(2, "0");
  const timeLabel = `${pad(now.getHours())}:${pad(now.getMinutes())}:${pad(now.getSeconds())}`;
  const filename = `${filenamePrefix}-${timestampSlug()}.jpg`;

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
  if (autoDownload) {
    triggerBlobDownload(url, filename);
  }
  renderControls();
}

function showShotInStage(item) {
  $("empty-state").hidden = true;
  $("stage-frame").hidden = false;
  $("preview-image").src = item.url;
  clearMotionOverlay();
  $("meta-size").textContent = `${item.width} × ${item.height} JPEG`;
  $("meta-bytes").textContent = `${item.length.toLocaleString()} bytes`;
  $("meta-temp").textContent = formatTemp(item.tempC ?? latestTempC);
  $("meta-verify").textContent = `PASS (${item.crc32.toString(16).padStart(8, "0")})`;
  $("viewer-subtitle").textContent =
    item.source === "live-snap"
      ? `預覽快門存檔 ${item.timeLabel}`
      : item.source === "motion"
        ? `移動偵測自動抓拍 ${item.timeLabel}`
        : item.source === "timelapse"
          ? `定時連拍 ${item.timeLabel} · 耗時 ${item.captureMs} ms`
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

function clearTimelapseTimers() {
  if (timelapseTimer) {
    clearTimeout(timelapseTimer);
    timelapseTimer = null;
  }
  if (timelapseTickTimer) {
    clearInterval(timelapseTickTimer);
    timelapseTickTimer = null;
  }
}

function stopTimelapse(statusText = "已停止") {
  clearTimelapseTimers();
  timelapseRunning = false;
  $("timelapse-status").textContent = statusText;
  renderControls();
}

function pickRecorderMime() {
  if (!recorderSupported) return "";
  for (const mime of ["video/webm;codecs=vp9", "video/webm;codecs=vp8", "video/webm"]) {
    if (MediaRecorder.isTypeSupported(mime)) return mime;
  }
  return "";
}

function drawBitmapToRecordCanvas(bitmap) {
  const swap = rotationDeg % 180 !== 0;
  const targetW = swap ? bitmap.height : bitmap.width;
  const targetH = swap ? bitmap.width : bitmap.height;
  if (recordCanvas.width !== targetW || recordCanvas.height !== targetH) {
    recordCanvas.width = targetW;
    recordCanvas.height = targetH;
  }
  recordCtx.save();
  recordCtx.clearRect(0, 0, recordCanvas.width, recordCanvas.height);
  recordCtx.translate(recordCanvas.width / 2, recordCanvas.height / 2);
  recordCtx.rotate((rotationDeg * Math.PI) / 180);
  if (mirrored) recordCtx.scale(-1, 1);
  recordCtx.drawImage(bitmap, -bitmap.width / 2, -bitmap.height / 2, bitmap.width, bitmap.height);
  recordCtx.restore();
}

function startRecording() {
  if (!streaming || recordingActive || !recorderSupported) return;
  try {
    const swap = rotationDeg % 180 !== 0;
    recordCanvas.width = swap ? 240 : 320;
    recordCanvas.height = swap ? 320 : 240;
    const stream = recordCanvas.captureStream(30);
    const mimeType = pickRecorderMime();
    mediaRecorder = mimeType ? new MediaRecorder(stream, { mimeType }) : new MediaRecorder(stream);
    recordChunks = [];
    recordFrames = 0;
    recordStartedAt = performance.now();

    mediaRecorder.ondataavailable = event => {
      if (event.data && event.data.size > 0) recordChunks.push(event.data);
    };
    mediaRecorder.onstop = () => {
      const chunks = recordChunks;
      const frames = recordFrames;
      const mime = (mediaRecorder && mediaRecorder.mimeType) || "video/webm";
      recordChunks = [];
      mediaRecorder = null;
      if (chunks.length > 0) {
        const blob = new Blob(chunks, { type: mime });
        const url = URL.createObjectURL(blob);
        const filename = `xiao-video-${timestampSlug()}.webm`;
        triggerBlobDownload(url, filename);
        setTimeout(() => URL.revokeObjectURL(url), 30000);
        appendLog(
          `[網頁] 錄影完成並下載 ${filename}（${frames} 幀，${Math.round(blob.size / 1024)} KiB）。`,
        );
        $("record-status").textContent = `已下載（${frames} 幀）`;
      } else {
        $("record-status").textContent = "未錄到畫面";
      }
      renderControls();
    };

    mediaRecorder.start(500);
    recordingActive = true;
    $("rec-pill").hidden = false;
    $("rec-pill").textContent = "REC 00:00";
    $("record-status").textContent = "錄影中 00:00";
    recordTickTimer = setInterval(() => {
      if (!recordingActive) return;
      const elapsed = formatElapsed(performance.now() - recordStartedAt);
      $("rec-pill").textContent = `REC ${elapsed}`;
      $("record-status").textContent = `錄影中 ${elapsed} · ${recordFrames} 幀`;
    }, 500);
    appendLog("[網頁] 開始瀏覽器端 WebM 錄影。");
    renderControls();
  } catch (error) {
    setMessage(`無法開始錄影：${error.message}`);
    appendLog(`[網頁] 錄影啟動失敗：${error.message}`);
  }
}

function stopRecording() {
  if (!recordingActive) return;
  recordingActive = false;
  if (recordTickTimer) {
    clearInterval(recordTickTimer);
    recordTickTimer = null;
  }
  $("rec-pill").hidden = true;
  if (mediaRecorder && mediaRecorder.state !== "inactive") {
    try { mediaRecorder.stop(); } catch { /* ignore */ }
  }
  renderControls();
}

function disableMotionDetection(statusText = "未啟用") {
  motionEnabled = false;
  motionDetector.reset();
  clearMotionOverlay();
  $("motion-bar").style.width = "0%";
  $("motion-bar").classList.remove("triggered");
  $("motion-status").textContent = statusText;
  updateMotionThresholdUi();
}

function endStreamState(reasonText = "") {
  if (recordingActive) stopRecording();
  if (motionEnabled) disableMotionDetection("預覽已停");
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
  const hasAnyImage = !$("stage-frame").hidden;
  const busy = capturing || streamBusy || timelapseRunning;

  $("connect").disabled = !supported || opening || port !== null;
  $("disconnect").disabled = !connected || opening;

  const previewBtn = $("preview-toggle");
  previewBtn.disabled = !connected || capturing || streamBusy || timelapseRunning;
  previewBtn.textContent = streaming ? "停止預覽" : "開始即時預覽";
  previewBtn.classList.toggle("danger-btn", streaming);
  previewBtn.classList.toggle("primary", !streaming);

  $("preview-interval").disabled = !connected || busy || streaming;

  const sizeSelect = $("capture-size");
  sizeSelect.disabled = !connected || busy;
  const selectedSize = sizeSelect.value;

  const captureBtn = $("capture");
  const waitingLiveSnap = streaming && selectedSize === "LIVE" && !latestLiveFrame;
  captureBtn.disabled = !connected || busy || waitingLiveSnap;
  captureBtn.textContent =
    selectedSize === "LIVE" && streaming
      ? "拍一張（瞬拍目前畫面）"
      : `拍一張（${selectedSize === "LIVE" ? "QVGA" : selectedSize}）`;

  $("download").disabled = !hasShot;
  $("rotate").disabled = !hasAnyImage || recordingActive;
  $("mirror").disabled = !hasAnyImage || recordingActive;

  // Motion & recording controls
  $("motion-threshold").disabled = !connected;
  $("motion-cooldown").disabled = !connected;
  $("motion-autodownload").disabled = !connected;
  const motionBtn = $("motion-toggle");
  motionBtn.disabled = !streaming;
  motionBtn.textContent = motionEnabled ? "關閉移動偵測" : "開啟移動偵測";
  motionBtn.classList.toggle("danger-btn", motionEnabled);
  motionBtn.classList.toggle("secondary", !motionEnabled);

  const recBtn = $("record-toggle");
  recBtn.disabled = !streaming || !recorderSupported;
  recBtn.textContent = recordingActive ? "停止錄影並下載" : "開始錄影";
  recBtn.classList.toggle("danger-btn", recordingActive);
  recBtn.classList.toggle("secondary", !recordingActive);

  const tuneDisabled = !connected || capturing || streamBusy || timelapseRunning || tuningBusy;
  for (const id of [
    "tune-brightness",
    "tune-ae",
    "tune-contrast",
    "tune-saturation",
    "tune-wb",
    "tune-hmirror",
    "tune-vflip",
    "tune-reset",
  ]) {
    $(id).disabled = tuneDisabled;
  }

  $("timelapse-interval").disabled = !connected || busy;
  $("timelapse-count").disabled = !connected || busy;
  $("timelapse-autodownload").disabled = !connected || busy;
  const tlBtn = $("timelapse-toggle");
  tlBtn.disabled = !connected || (!timelapseRunning && (capturing || streamBusy));
  tlBtn.textContent = timelapseRunning ? "停止連拍" : "開始定時連拍";
  tlBtn.classList.toggle("danger-btn", timelapseRunning);
  tlBtn.classList.toggle("secondary", !timelapseRunning);

  const badge = $("status-badge");
  if (!connected) {
    badge.dataset.state = "idle";
    badge.textContent = hasShot ? "已中斷 · 保留照片" : "等待連線";
  } else if (timelapseRunning) {
    badge.dataset.state = "busy";
    badge.textContent = `定時連拍中 (${timelapseDone}/${timelapseTotal})`;
  } else if (recordingActive) {
    badge.dataset.state = "error";
    badge.textContent = "即時預覽＋錄影中";
  } else if (streaming) {
    badge.dataset.state = "ready";
    badge.textContent = motionEnabled ? "預覽＋移動偵測中" : "即時預覽中 · 同步畫面";
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

async function processLiveBitmap(blob, frame) {
  if (motionBusy || typeof createImageBitmap !== "function") return;
  motionBusy = true;
  let bitmap = null;
  try {
    bitmap = await createImageBitmap(blob);
    if (recordingActive) {
      drawBitmapToRecordCanvas(bitmap);
      recordFrames++;
    }
    if (motionEnabled && streaming) {
      motionCtx.drawImage(bitmap, 0, 0, MOTION_W, MOTION_H);
      const rgba = motionCtx.getImageData(0, 0, MOTION_W, MOTION_H).data;
      const luma = rgbaToLuma(rgba, MOTION_W, MOTION_H);
      const res = motionDetector.update(luma, performance.now());
      const pct = res.score.changedPercent;
      const barWidth = Math.min(100, (pct / MOTION_SCALE_MAX) * 100);
      $("motion-bar").style.width = `${barWidth.toFixed(1)}%`;
      $("motion-bar").classList.toggle("triggered", pct >= motionDetector.triggerPercent);
      $("motion-readout").textContent =
        `變化 ${pct.toFixed(1)}% / 門檻 ${motionDetector.triggerPercent.toFixed(1)}%`;

      if (!res.ready) {
        $("motion-status").textContent = "暖機建立基準…";
        clearMotionOverlay();
      } else {
        drawMotionOverlay(pct >= motionDetector.triggerPercent ? res.score.box : null, res.triggered);
        if (res.triggered) {
          const autoDownload = $("motion-autodownload").checked;
          const tag = String(motionDetector.triggerCount).padStart(3, "0");
          pushSavedShot({
            jpeg: frame.jpeg.slice(),
            width: frame.width,
            height: frame.height,
            length: frame.length,
            crc32: frame.crc32,
            captureMs: 0,
            tempC: frame.tempC,
            source: "motion",
            filenamePrefix: `xiao-motion-${tag}`,
            autoDownload,
          });
          appendLog(
            `[網頁] 偵測到移動（變化 ${pct.toFixed(1)}%）→ 自動抓拍第 #${motionDetector.triggerCount} 張。`,
          );
        }
        $("motion-status").textContent =
          res.cooldownRemainingMs > 0
            ? `已觸發 ${motionDetector.triggerCount} 次 · 冷卻 ${Math.ceil(res.cooldownRemainingMs / 1000)}s`
            : `監控中 · 已觸發 ${motionDetector.triggerCount} 次`;
      }
    }
  } catch {
    // Ignore transient decode failure on individual live frame.
  } finally {
    if (bitmap) bitmap.close();
    motionBusy = false;
  }
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
  $("stage-frame").hidden = false;
  $("preview-image").src = nextUrl;
  $("live-pill").hidden = false;
  $("live-pill").textContent = fps > 0 ? `LIVE · ${fps.toFixed(1)} FPS` : "LIVE";
  updateTransform();
  if (oldUrl) URL.revokeObjectURL(oldUrl);

  $("meta-size").textContent = `${frame.width} × ${frame.height} · ${fps.toFixed(1)} fps`;
  $("meta-bytes").textContent = `${frame.length.toLocaleString()} bytes`;
  $("meta-temp").textContent = formatTemp(latestTempC);
  $("meta-verify").textContent = `倒數 ${formatCountdown(streamEndsAt - performance.now())}`;
  $("viewer-subtitle").textContent =
    `即時同步取景中（第 #${frame.seq} 幀）· 按「拍一張」存檔，停止即 deinit`;

  if (motionEnabled || recordingActive) {
    processLiveBitmap(blob, frame);
  }
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

function dumpCapturedFrame(session, maxFrameBytes, onProgress) {
  const assembler = new StreamFrameAssembler(session, maxFrameBytes, onProgress);
  return sendRequest(
    session,
    `CAM DUMP ${session}`,
    line => {
      const result = assembler.feed(line);
      if (result) return result;
      if (line === `CAM FEND ${session} 0`) return { failed: true };
      return null;
    },
    { timeoutMs: 25000, attempts: 1 },
  );
}

async function captureStillFrame(targetSize, { source = "single-capture", filenamePrefix = "xiao-cam", autoDownload = false } = {}) {
  capturing = true;
  setMessage();
  const session = nextSessionId();
  setProgress(true, 0, 1, "正在檢查 CAM INFO…");
  renderControls();
  try {
    const result = await runCapture({
      session,
      size: targetSize,
      dumpFrame: dumpCapturedFrame,
      request: (cmd, parse, opts) => sendRequest(session, cmd, parse, opts),
      onStage: (stage, detail) => {
        if (stage === "capture") {
          setProgress(true, 0, 1, `正在拍攝 ${targetSize} 並立即 deinit…`);
        } else if (stage === "download" && detail?.state) {
          setProgress(true, 0, detail.state.length, `高速傳輸中 0 / ${detail.state.length} bytes…`);
        } else if (stage === "released" && detail && !detail.released) {
          appendLog(`[網頁] CAM RELEASE 未確認：${detail.releaseError}`);
        }
      },
      onProgress: (done, total) => setProgress(true, done, total),
    });

    if (result.tempC !== null && result.tempC !== undefined) latestTempC = result.tempC;
    pushSavedShot({
      jpeg: result.jpeg,
      width: result.state.width,
      height: result.state.height,
      length: result.state.length,
      crc32: result.state.crc32,
      captureMs: result.state.captureMs,
      tempC: result.tempC ?? latestTempC,
      source,
      filenamePrefix,
      autoDownload,
    });
    appendLog(
      `[網頁] 拍照完成（${targetSize}）：${result.state.width}x${result.state.height}, ` +
      `${result.state.length} bytes, capture=${result.state.captureMs} ms, ` +
      `CRC32=${result.state.crc32.toString(16).padStart(8, "0")} PASS`,
    );
    return result;
  } finally {
    capturing = false;
    setProgress(false);
    renderControls();
  }
}

async function sendTuneCommand(cmd) {
  if (!port || closing || capturing || streamBusy || timelapseRunning || tuningBusy) return;
  tuningBusy = true;
  renderControls();
  try {
    const tuning = await sendRequest(0, cmd, parseCamTuning, { timeoutMs: 1500, attempts: 2 });
    applyTuningToUi(tuning);
  } catch (error) {
    setMessage(`影像參數套用失敗：${error.message}`);
    appendLog(`[網頁] ${cmd} 失敗：${error.message}`);
  } finally {
    tuningBusy = false;
    renderControls();
  }
}

async function startLivePreview() {
  if (!port || closing || streaming || capturing || streamBusy || timelapseRunning) return;
  streamBusy = true;
  setMessage();
  const session = nextSessionId();
  const rawMode = $("preview-interval").value;
  const [intervalStr, modeFlag] = rawMode.split(":");
  const intervalMs = Number(intervalStr);
  const safeInterval = Number.isFinite(intervalMs) && intervalMs >= 0 ? intervalMs : 0;
  const isFast = modeFlag === "FAST";
  const startCmd = isFast
    ? `CAM STREAM START ${session} ${safeInterval} FAST`
    : `CAM STREAM START ${session} ${safeInterval}`;
  renderControls();
  try {
    const started = await sendRequest(
      session,
      startCmd,
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
    appendLog(
      `[網頁] 已啟動即時預覽（${isFast ? "20 MHz 雙緩衝" : "10 MHz"}，間隔 ${started.intervalMs} ms，安全上限 ${Math.round(started.maxMs / 1000)} 秒）。`,
    );
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
  if (recordingActive) stopRecording();
  if (motionEnabled) disableMotionDetection("預覽已停");
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

async function runTimelapseStep(intervalSec, targetSize, autoDownload) {
  if (!timelapseRunning || !port || closing) return;
  const shotNum = timelapseDone + 1;
  $("timelapse-status").textContent = `正在拍攝第 ${shotNum} / ${timelapseTotal} 張…`;
  try {
    const seqTag = String(shotNum).padStart(3, "0");
    await captureStillFrame(targetSize, {
      source: "timelapse",
      filenamePrefix: `xiao-timelapse-${seqTag}`,
      autoDownload,
    });
    if (!timelapseRunning) return;
    timelapseDone++;
    if (timelapseDone >= timelapseTotal) {
      appendLog(`[網頁] 定時連拍完成：共 ${timelapseDone} 張（${targetSize}）。`);
      stopTimelapse(`已完成 ${timelapseDone} / ${timelapseTotal} 張`);
      return;
    }
    timelapseNextAt = performance.now() + intervalSec * 1000;
    $("timelapse-status").textContent =
      `已完成 ${timelapseDone} / ${timelapseTotal} 張 · 倒數 ${intervalSec} 秒`;
    timelapseTimer = setTimeout(
      () => runTimelapseStep(intervalSec, targetSize, autoDownload),
      intervalSec * 1000,
    );
  } catch (error) {
    setMessage(`定時連拍中斷：${error.message}`);
    appendLog(`[網頁] 定時連拍失敗：${error.message}`);
    stopTimelapse(`中斷於第 ${shotNum} 張`);
  }
}

async function startTimelapse() {
  if (!port || closing || capturing || streamBusy || timelapseRunning) return;
  if (streaming) {
    await stopLivePreview();
  }
  const intervalSec = Number($("timelapse-interval").value) || 10;
  const totalCount = Number($("timelapse-count").value) || 5;
  const autoDownload = $("timelapse-autodownload").checked;
  const selectedSize = $("capture-size").value;
  const targetSize = selectedSize === "LIVE" ? "QVGA" : selectedSize;

  timelapseRunning = true;
  timelapseDone = 0;
  timelapseTotal = totalCount;
  timelapseNextAt = 0;
  setMessage();
  appendLog(`[網頁] 開始定時連拍：每 ${intervalSec} 秒 1 張，共 ${totalCount} 張（${targetSize}，自動下載=${autoDownload ? "開" : "關"}）。`);

  timelapseTickTimer = setInterval(() => {
    if (!timelapseRunning || capturing || !timelapseNextAt) return;
    const remSec = Math.max(0, Math.ceil((timelapseNextAt - performance.now()) / 1000));
    $("timelapse-status").textContent =
      `已完成 ${timelapseDone} / ${timelapseTotal} 張 · 倒數 ${remSec} 秒`;
  }, 500);

  renderControls();
  runTimelapseStep(intervalSec, targetSize, autoDownload);
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
    if (timelapseRunning) stopTimelapse("串口已中斷");
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
    tuningBusy = false;
    setProgress(false);
    $("connection-state").textContent = "已中斷連線";
    if (failure) setMessage(failure);
    renderControls();
  }
}

async function disconnect() {
  if (!port || closing) return;
  if (timelapseRunning) stopTimelapse("已手動停止");
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
    sendRequest(0, "CAM TUNE GET", parseCamTuning, { timeoutMs: 1200, attempts: 2 })
      .then(applyTuningToUi)
      .catch(() => {});
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

$("capture-size").addEventListener("change", () => renderControls());

$("capture").addEventListener("click", async () => {
  if (!port || closing || capturing || streamBusy || timelapseRunning) return;
  const selectedSize = $("capture-size").value;

  if (selectedSize === "LIVE" && streaming && latestLiveFrame) {
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

  if (streaming) {
    await stopLivePreview();
  }
  const targetSize = selectedSize === "LIVE" ? "QVGA" : selectedSize;
  try {
    await captureStillFrame(targetSize);
  } catch (error) {
    setMessage(`拍照失敗：${error.message}`);
    appendLog(`[網頁] 拍照失敗：${error.message}`);
  }
});

$("motion-threshold").addEventListener("input", updateMotionThresholdUi);
$("motion-cooldown").addEventListener("change", () => {
  motionDetector.cooldownMs = Number($("motion-cooldown").value) || 5000;
});

$("motion-toggle").addEventListener("click", () => {
  if (!streaming) return;
  if (motionEnabled) {
    disableMotionDetection("已關閉");
    appendLog("[網頁] 已關閉移動偵測。");
  } else {
    motionDetector.triggerPercent = Number($("motion-threshold").value) || 4;
    motionDetector.cooldownMs = Number($("motion-cooldown").value) || 5000;
    motionDetector.reset();
    motionEnabled = true;
    $("motion-status").textContent = "暖機建立基準…";
    appendLog(
      `[網頁] 已開啟移動偵測（門檻 ${motionDetector.triggerPercent}%，冷卻 ${motionDetector.cooldownMs / 1000} 秒）。`,
    );
  }
  renderControls();
});

$("record-toggle").addEventListener("click", () => {
  if (recordingActive) stopRecording();
  else startRecording();
});

for (const [id, key, valId] of [
  ["tune-brightness", "BRIGHTNESS", "val-brightness"],
  ["tune-ae", "AE", "val-ae"],
  ["tune-contrast", "CONTRAST", "val-contrast"],
  ["tune-saturation", "SATURATION", "val-saturation"],
]) {
  $(id).addEventListener("input", () => {
    const n = Number($(id).value);
    $(valId).textContent = n > 0 ? `+${n}` : String(n);
  });
  $(id).addEventListener("change", () => sendTuneCommand(`CAM TUNE SET ${key} ${$(id).value}`));
}

$("tune-wb").addEventListener("change", () => sendTuneCommand(`CAM TUNE SET WB ${$("tune-wb").value}`));
$("tune-hmirror").addEventListener("change", () =>
  sendTuneCommand(`CAM TUNE SET HMIRROR ${$("tune-hmirror").checked ? 1 : 0}`),
);
$("tune-vflip").addEventListener("change", () =>
  sendTuneCommand(`CAM TUNE SET VFLIP ${$("tune-vflip").checked ? 1 : 0}`),
);
$("tune-reset").addEventListener("click", () => sendTuneCommand("CAM TUNE RESET"));

$("timelapse-toggle").addEventListener("click", () => {
  if (timelapseRunning) {
    appendLog("[網頁] 已手動停止定時連拍。");
    stopTimelapse(`已手動停止（共 ${timelapseDone} 張）`);
  } else {
    startTimelapse();
  }
});

$("download").addEventListener("click", () => {
  if (selectedIndex < 0 || selectedIndex >= history.length) return;
  const item = history[selectedIndex];
  triggerBlobDownload(item.url, item.filename);
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
  clearTimelapseTimers();
  if (recordingActive) stopRecording();
  if (streaming && port && writer && !closing) {
    try { writer.write(encoder.encode(`\nCAM STREAM STOP ${streamSession}\n`)); } catch { /* ignore */ }
  }
  if (liveUrl) URL.revokeObjectURL(liveUrl);
  for (const item of history) URL.revokeObjectURL(item.url);
});

updateMotionThresholdUi();
renderControls();
