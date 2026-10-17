import { LineFramer, Telemetry, parseLine } from "./telemetry.mjs";

const $ = id => document.getElementById(id);
const telemetry = new Telemetry();
let port = null;
let reader = null;
let readTask = null;
let closing = false;
let opening = false;
let lastDataAt = 0;
let connectedAt = 0;
let hasCurrentData = false;
let repaintPending = false;
const logs = [];
const supported = window.isSecureContext && "serial" in navigator;

function message(text = "") {
  $("message").textContent = text;
  $("message").hidden = !text;
}

function log(line) {
  logs.push(line);
  if (logs.length > 120) logs.shift();
}

function render() {
  const active = port !== null && !closing;
  $("connect").disabled = !supported || opening || port !== null;
  $("disconnect").disabled = !active || opening;
  $("download").disabled = telemetry.rows.length === 0;
  const stale = active && hasCurrentData && performance.now() - lastDataAt > 2000;
  $("freshness").dataset.live = String(active && hasCurrentData && !stale);
  $("freshness").textContent = !active ? (telemetry.count ? "已中斷 · 保留觀測資料" : "等待設備資料") :
    stale ? "資料暫停超過 2 秒" : hasCurrentData ? "正在接收設備資料" : "已連線 · 等待資料";
  $("level").textContent = !active ? "等待連線" : !hasCurrentData ? "等待資料" : stale ? "資料暫停" : telemetry.last.level;
  $("level").dataset.level = active && hasCurrentData && !stale ? telemetry.last.level : "Off";
  $("level-note").textContent = hasCurrentData && telemetry.last ?
    `設備時間 ${(telemetry.last.timestamp / 1000).toFixed(1)} 秒` : "與圓屏使用同一個音量分級";
  $("rms").textContent = active && hasCurrentData && !stale ? telemetry.last.rms.toFixed(1) : "—";
  $("peak").textContent = active && hasCurrentData && !stale ? String(telemetry.last.peak) : "—";
  $("row-count").textContent = telemetry.count.toLocaleString();
  $("interval").textContent = telemetry.gap === null ? "—" : `${telemetry.gap} ms`;
  $("max-gap").textContent = telemetry.maxGap ? `${telemetry.maxGap} ms` : "—";
  $("i2c-count").textContent = telemetry.i2cErrors.toLocaleString();
  $("i2c-stat").classList.toggle("has-errors", telemetry.i2cErrors > 0);
  $("diagnostic-summary").textContent = `格式錯誤 ${telemetry.invalid} · 重複時間 ${telemetry.duplicates} · 時間重置 ${telemetry.restarts}`;
  $("retention").textContent = telemetry.count ?
    `可下載 ${telemetry.rows.length.toLocaleString()} 行。${telemetry.dropped ? `已略過最舊的 ${telemetry.dropped} 行。` : "最多保留最近 10,000 行，關閉頁面前請下載。"}` : "尚無可下載的資料。最多保留最近 10,000 行。";
  if (telemetry.i2cErrors) {
    $("chart-note").textContent = "已收到 I2C 錯誤，詳見下方診斷。曲線中斷處請同時觀察設備反應。";
  }
  const rawLog = $("raw-log");
  const atBottom = rawLog.scrollTop + rawLog.clientHeight >= rawLog.scrollHeight - 20;
  rawLog.textContent = logs.join("\n");
  if (atBottom) rawLog.scrollTop = rawLog.scrollHeight;
  schedulePaint();
}

function handleChunk(framer, text) {
  const { lines, discarded } = framer.push(text);
  telemetry.invalid += discarded;
  if (discarded) log(`[網頁] 已略過 ${discarded} 行過長資料。`);
  for (const line of lines) {
    const parsed = parseLine(line);
    if (parsed.kind === "empty") continue;
    log(line);
    if (parsed.i2c) telemetry.i2cErrors++;
    if (parsed.kind === "invalid") telemetry.invalid++;
    if (parsed.kind === "sample") {
      const result = telemetry.accept(parsed);
      if (result.accepted) {
        if (!hasCurrentData && $("message").textContent.startsWith("串口已開啟")) message();
        hasCurrentData = true;
        lastDataAt = performance.now();
      }
      if (result.restart) log("[網頁] 設備時間倒退；曲線開始新的一段，CSV 保留原始時間。 ");
    }
  }
  render();
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
      if (value) handleChunk(framer, decoder.decode(value, { stream: true }));
    }
  } catch (error) {
    if (!closing) failure = `串口讀取中斷：${error.message}`;
  } finally {
    if (reader) {
      reader.releaseLock();
      reader = null;
    }
    try { await selected.close(); } catch { /* Unplugged ports may already be closed. */ }
    if (framer.pending || framer.dropping) log("[網頁] 中斷時未完成的一行已略過。");
    if (port === selected) port = null;
    closing = false;
    hasCurrentData = false;
    $("connection-state").textContent = "已中斷連線";
    if (failure) message(failure);
    render();
  }
}

$("connect").addEventListener("click", async () => {
  if (!supported || opening || port) return;
  opening = true;
  message();
  render();
  let selected;
  try {
    selected = await navigator.serial.requestPort();
    await selected.open({ baudRate: 115200, bufferSize: 8192, flowControl: "none" });
    port = selected;
    closing = false;
    hasCurrentData = false;
    connectedAt = performance.now();
    // Reconnecting starts a new chart segment; previously collected CSV rows stay available.
    telemetry.last = null;
    telemetry.plot = [];
    telemetry.plotTime = 0;
    telemetry.gap = null;
    $("connection-state").textContent = "USB 串口已連接 · 115200 baud";
    readTask = readLoop(selected);
  } catch (error) {
    if (error.name === "NotFoundError") message("尚未選擇串口，可以再次點擊連接。");
    else message(`無法開啟串口：${error.message}。請確認 XIAO 已接上，並關閉 VS Code 或其他串口監視器。`);
  } finally {
    opening = false;
    render();
  }
});

$("disconnect").addEventListener("click", async () => {
  closing = true;
  render();
  try { await reader?.cancel(); } catch { /* Removal may race with cancellation. */ }
  if (readTask) await readTask;
});

$("download").addEventListener("click", () => {
  if (!telemetry.rows.length) return;
  const url = URL.createObjectURL(new Blob([telemetry.csv()], { type: "text/csv;charset=utf-8" }));
  const link = document.createElement("a");
  link.href = url;
  link.download = `xiao-mic-${new Date().toISOString().replace(/[:.]/g, "-")}.csv`;
  document.body.append(link);
  link.click();
  link.remove();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
});

function schedulePaint() {
  if (repaintPending) return;
  repaintPending = true;
  requestAnimationFrame(() => {
    repaintPending = false;
    drawChart($("rms-chart"), "rms", "#137c70");
    drawChart($("peak-chart"), "peak", "#af602b");
  });
}

function drawChart(canvas, field, color) {
  const { width, height } = canvas.getBoundingClientRect();
  if (!width || !height) return;
  const ratio = window.devicePixelRatio || 1;
  canvas.width = Math.round(width * ratio);
  canvas.height = Math.round(height * ratio);
  const ctx = canvas.getContext("2d");
  ctx.scale(ratio, ratio);
  const left = 43, top = 8, right = width - 9, bottom = height - 25;
  const points = telemetry.plot;
  const largest = Math.max(field === "rms" ? 10 : 30, ...points.map(p => p[field]));
  const scale = Math.ceil(largest * 1.15 / 10) * 10;
  ctx.font = '10px "Cascadia Code", Consolas, monospace';
  for (let step = 0; step <= 2; step++) {
    const y = top + (bottom - top) * step / 2;
    ctx.strokeStyle = "#e3e8e1";
    ctx.beginPath(); ctx.moveTo(left, y); ctx.lineTo(right, y); ctx.stroke();
    ctx.fillStyle = "#758780";
    ctx.textAlign = "right";
    ctx.fillText(String(Math.round(scale * (1 - step / 2))), left - 8, y + 3);
  }
  for (let step = 0; step <= 4; step++) {
    const x = left + (right - left) * step / 4;
    ctx.textAlign = step === 4 ? "right" : step === 0 ? "left" : "center";
    ctx.fillText(step === 4 ? "0s" : `${-60 + step * 15}s`, x, height - 5);
  }
  if (!points.length) {
    ctx.textAlign = "center";
    ctx.fillStyle = "#93a097";
    ctx.fillText("WAITING FOR XIAO", (left + right) / 2, (top + bottom) / 2);
    return;
  }
  ctx.strokeStyle = color; ctx.fillStyle = color; ctx.lineWidth = 1.8;
  ctx.beginPath();
  let previous = null;
  for (const point of points) {
    const x = left + (point.time - telemetry.plotTime + 60000) / 60000 * (right - left);
    const y = bottom - point[field] / scale * (bottom - top);
    if (!previous || point.time - previous.time > 1200) ctx.moveTo(x, y);
    else ctx.lineTo(x, y);
    previous = point;
  }
  ctx.stroke();
  const latest = points.at(-1);
  ctx.beginPath(); ctx.arc(right, bottom - latest[field] / scale * (bottom - top), 3, 0, Math.PI * 2); ctx.fill();
}

new ResizeObserver(schedulePaint).observe($("rms-chart"));
setInterval(() => {
  if (port && !hasCurrentData && performance.now() - connectedAt > 4000) {
    message("串口已開啟，但尚未收到四欄資料。請確認設備燒錄的是本次串口測試固件；下方可查看原始記錄。");
  }
  render();
}, 1000);
if (!supported) {
  message("此瀏覽器無法使用 Web Serial。請用桌面版 Edge 或 Chrome 開啟本機網址，並連接 XIAO。");
  $("connection-state").textContent = "瀏覽器不支援串口";
}
render();
