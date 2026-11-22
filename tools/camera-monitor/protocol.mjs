const MAX_LINE_LENGTH = 4096;
const INFO_RE = /^CAM INFO (\d+) (\d+) (\d+) (JPEG) (\d+) (\d+) (\d+)$/;
const STATE_RE = /^CAM STATE (\d+) (READY) (\d+) (\d+) (\d+) (\d+) (\d+) ([0-9a-f]{8})$/;
const DATA_RE = /^CAM DATA (\d+) (\d+) ([0-9a-f]+) ([0-9a-f]{8})$/;
const ERROR_RE = /^CAM ERROR (\d+) ([A-Z_]+)$/;
const STREAMING_RE = /^CAM STREAMING (\d+) (\d+) (\d+) (-?\d+)$/;
const PONG_RE = /^CAM PONG (\d+) (\d+) (-?\d+)$/;
const STOPPED_RE = /^CAM STOPPED (\d+) ([A-Z_]+)$/;
const TUNING_RE = /^CAM TUNING (-?\d+) (-?\d+) (-?\d+) (-?\d+) (\d+) (\d+) (\d+)$/;
const FRAME_RE = /^CAM FRAME (\d+) (\d+) (\d+) (\d+) (\d+) ([0-9a-f]{8}) (-?\d+)$/;
const FDATA_RE = /^CAM FDATA (\d+) (\d+) ([A-Za-z0-9+/=]+)$/;
const FEND_RE = /^CAM FEND (\d+) (\d+)$/;
const I2C_ERROR_RE = /i2cRead.*Error|requestFrom\(\).*Error/i;
const FAULT_RE = /read failed|init failed|Guru Meditation|Backtrace:|brownout|watchdog|assert failed|abort\(\)/i;

const B64_LOOKUP = (() => {
  const table = new Int16Array(128).fill(-1);
  const chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  for (let i = 0; i < chars.length; i++) table[chars.charCodeAt(i)] = i;
  return table;
})();

const CRC_TABLE = (() => {
  const table = new Uint32Array(256);
  for (let i = 0; i < 256; i++) {
    let c = i;
    for (let bit = 0; bit < 8; bit++) {
      c = (c >>> 1) ^ (c & 1 ? 0xedb88320 : 0);
    }
    table[i] = c >>> 0;
  }
  return table;
})();

export function crc32(bytes) {
  let crc = 0xffffffff;
  for (let i = 0; i < bytes.length; i++) {
    crc = CRC_TABLE[(crc ^ bytes[i]) & 0xff] ^ (crc >>> 8);
  }
  return (crc ^ 0xffffffff) >>> 0;
}

export function hexToBytes(hex) {
  if (typeof hex !== "string" || hex.length % 2 !== 0 || !/^[0-9a-f]*$/.test(hex)) {
    throw new Error("invalid hex payload");
  }
  const out = new Uint8Array(hex.length / 2);
  for (let i = 0; i < out.length; i++) {
    out[i] = parseInt(hex.slice(i * 2, i * 2 + 2), 16);
  }
  return out;
}

export function base64ToBytes(b64) {
  if (typeof b64 !== "string" || b64.length === 0 || b64.length % 4 !== 0) {
    throw new Error("invalid base64 length");
  }
  let pad = 0;
  if (b64.endsWith("==")) pad = 2;
  else if (b64.endsWith("=")) pad = 1;
  const outLen = (b64.length / 4) * 3 - pad;
  const out = new Uint8Array(outLen);
  let outPos = 0;
  for (let i = 0; i < b64.length; i += 4) {
    const c0 = b64.charCodeAt(i);
    const c1 = b64.charCodeAt(i + 1);
    const c2 = b64.charCodeAt(i + 2);
    const c3 = b64.charCodeAt(i + 3);
    const v0 = c0 < 128 ? B64_LOOKUP[c0] : -1;
    const v1 = c1 < 128 ? B64_LOOKUP[c1] : -1;
    const isPad2 = c2 === 61 && i + 4 === b64.length;
    const isPad3 = c3 === 61 && i + 4 === b64.length;
    const v2 = isPad2 ? 0 : (c2 < 128 ? B64_LOOKUP[c2] : -1);
    const v3 = isPad3 ? 0 : (c3 < 128 ? B64_LOOKUP[c3] : -1);
    if (v0 < 0 || v1 < 0 || v2 < 0 || v3 < 0 || (isPad2 && !isPad3)) {
      throw new Error("invalid base64 character");
    }
    const triple = (v0 << 18) | (v1 << 12) | (v2 << 6) | v3;
    out[outPos++] = (triple >> 16) & 0xff;
    if (!isPad2) out[outPos++] = (triple >> 8) & 0xff;
    if (!isPad3) out[outPos++] = triple & 0xff;
  }
  return out;
}

function parseDeciTemp(raw) {
  const deci = Number(raw);
  if (!Number.isFinite(deci) || deci <= -9000) return null;
  return deci / 10;
}

export class LineFramer {
  constructor() {
    this.pending = "";
    this.dropping = false;
  }

  push(chunk) {
    const lines = [];
    let discarded = 0;
    for (let i = 0; i < chunk.length; i++) {
      const ch = chunk[i];
      if (ch === "\n") {
        if (!this.dropping) {
          lines.push(this.pending.endsWith("\r") ? this.pending.slice(0, -1) : this.pending);
        }
        this.pending = "";
        this.dropping = false;
      } else if (!this.dropping) {
        this.pending += ch;
        if (this.pending.length > MAX_LINE_LENGTH) {
          this.pending = "";
          this.dropping = true;
          discarded++;
        }
      }
    }
    return { lines, discarded };
  }
}

export function classifyDiagnostic(line) {
  return {
    i2cError: I2C_ERROR_RE.test(line),
    fault: FAULT_RE.test(line),
  };
}

export function parseCamInfo(line) {
  const m = INFO_RE.exec(line);
  if (!m) return null;
  const protocol = Number(m[1]);
  const width = Number(m[2]);
  const height = Number(m[3]);
  const format = m[4];
  const maxFrameBytes = Number(m[5]);
  const chunkBytes = Number(m[6]);
  const freePsram = Number(m[7]);
  if (protocol !== 1 || format !== "JPEG" || width < 1 || height < 1 || maxFrameBytes < 1 || chunkBytes !== 64) {
    throw new Error("unsupported CAM protocol/format");
  }
  return { protocol, width, height, format, maxFrameBytes, chunkBytes, freePsram };
}

export function parseCamState(line, session, maxFrameBytes) {
  const m = STATE_RE.exec(line);
  if (!m || Number(m[1]) !== session) return null;
  const length = Number(m[3]);
  const width = Number(m[4]);
  const height = Number(m[5]);
  const format = Number(m[6]);
  const captureMs = Number(m[7]);
  const expectedCrc = parseInt(m[8], 16) >>> 0;
  if (length < 4 || length > maxFrameBytes || width < 1 || height < 1 || format !== 4) {
    throw new Error("inconsistent camera metadata");
  }
  if (!Number.isSafeInteger(captureMs) || captureMs < 0 || captureMs >= 2 ** 32) {
    throw new Error("invalid uint32 metadata");
  }
  return { state: m[2], length, width, height, format, captureMs, crc32: expectedCrc };
}

export function parseCamData(line, session, offset, expectedLength) {
  const m = DATA_RE.exec(line);
  if (!m || Number(m[1]) !== session || Number(m[2]) !== offset) return null;
  const bytes = hexToBytes(m[3]);
  const expectedCrc = parseInt(m[4], 16) >>> 0;
  if (bytes.length !== expectedLength || crc32(bytes) !== expectedCrc) {
    throw new Error("chunk length/CRC mismatch");
  }
  return bytes;
}

export function parseCamError(line, session) {
  const m = ERROR_RE.exec(line);
  if (!m) return null;
  const id = Number(m[1]);
  if (id !== session && id !== 0) return null;
  return { id, reason: m[2] };
}

export function parseStreamStarted(line, session) {
  const m = STREAMING_RE.exec(line);
  if (!m || Number(m[1]) !== session) return null;
  return {
    intervalMs: Number(m[2]),
    maxMs: Number(m[3]),
    tempC: parseDeciTemp(m[4]),
  };
}

export function parseStreamPong(line, session) {
  const m = PONG_RE.exec(line);
  if (!m || Number(m[1]) !== session) return null;
  return {
    remainingMs: Number(m[2]),
    tempC: parseDeciTemp(m[3]),
  };
}

export function parseStreamStopped(line, session) {
  const m = STOPPED_RE.exec(line);
  if (!m || Number(m[1]) !== session) return null;
  return { reason: m[2] };
}

export function parseCamTuning(line) {
  const m = TUNING_RE.exec(line);
  if (!m) return null;
  return {
    brightness: Number(m[1]),
    contrast: Number(m[2]),
    saturation: Number(m[3]),
    ae: Number(m[4]),
    wb: Number(m[5]),
    hmirror: Number(m[6]),
    vflip: Number(m[7]),
  };
}

export function verifyJpeg(bytes, state) {
  if (!state || state.state !== "READY") {
    throw new Error("capture not ready");
  }
  if (bytes.length !== state.length || crc32(bytes) !== state.crc32) {
    throw new Error("whole-frame length/CRC mismatch");
  }
  if (bytes.length < 4 || bytes[0] !== 0xff || bytes[1] !== 0xd8) {
    throw new Error("missing JPEG SOI marker");
  }
  let eoiOffset = -1;
  for (let i = bytes.length - 2; i >= 2; i--) {
    if (bytes[i] === 0xff && bytes[i + 1] === 0xd9) {
      eoiOffset = i;
      break;
    }
  }
  if (eoiOffset < 2) {
    throw new Error("missing JPEG EOI marker");
  }
  for (let i = eoiOffset + 2; i < bytes.length; i++) {
    if (bytes[i] !== 0) {
      throw new Error("unexpected non-zero data after JPEG EOI marker");
    }
  }
  return {
    soiValid: true,
    eoiOffset,
    trailingZeroBytes: bytes.length - (eoiOffset + 2),
    bytes: bytes.length,
  };
}

export class StreamFrameAssembler {
  constructor(session, maxFrameBytes = 262144, onProgress = null) {
    this.session = session;
    this.maxFrameBytes = maxFrameBytes;
    this.onProgress = onProgress;
    this.current = null;
    this.droppedFrames = 0;
  }

  feed(line) {
    const fm = FRAME_RE.exec(line);
    if (fm && Number(fm[1]) === this.session) {
      if (this.current) this.droppedFrames++;
      const length = Number(fm[3]);
      const width = Number(fm[4]);
      const height = Number(fm[5]);
      if (length < 4 || length > this.maxFrameBytes || width < 1 || height < 1) {
        this.current = null;
        this.droppedFrames++;
        return null;
      }
      this.current = {
        seq: Number(fm[2]),
        length,
        width,
        height,
        crc32: parseInt(fm[6], 16) >>> 0,
        tempC: parseDeciTemp(fm[7]),
        buffer: new Uint8Array(length),
        offset: 0,
      };
      if (this.onProgress) this.onProgress(0, length);
      return null;
    }

    const dm = FDATA_RE.exec(line);
    if (dm && Number(dm[1]) === this.session) {
      if (!this.current || Number(dm[2]) !== this.current.seq) return null;
      try {
        const bytes = base64ToBytes(dm[3]);
        if (this.current.offset + bytes.length > this.current.length) {
          throw new Error("stream frame overflow");
        }
        this.current.buffer.set(bytes, this.current.offset);
        this.current.offset += bytes.length;
        if (this.onProgress) this.onProgress(this.current.offset, this.current.length);
      } catch {
        this.current = null;
        this.droppedFrames++;
      }
      return null;
    }

    const em = FEND_RE.exec(line);
    if (em && Number(em[1]) === this.session) {
      if (!this.current || Number(em[2]) !== this.current.seq) return null;
      const frame = this.current;
      this.current = null;
      if (frame.offset !== frame.length) {
        this.droppedFrames++;
        return null;
      }
      try {
        const verification = verifyJpeg(frame.buffer, {
          state: "READY",
          length: frame.length,
          crc32: frame.crc32,
        });
        return {
          seq: frame.seq,
          width: frame.width,
          height: frame.height,
          length: frame.length,
          crc32: frame.crc32,
          tempC: frame.tempC,
          jpeg: frame.buffer,
          verification,
        };
      } catch {
        this.droppedFrames++;
        return null;
      }
    }

    return null;
  }
}

export async function runCapture({
  request,
  session,
  size = "QVGA",
  dumpFrame = null,
  onStage = () => {},
  onProgress = () => {},
}) {
  let released = false;
  let releaseError = null;
  try {
    onStage("info");
    const info = await request("CAM INFO", parseCamInfo, { timeoutMs: 1200, attempts: 5 });

    onStage("capture", { info, size });
    const captureCmd = size && size !== "QVGA" ? `CAM CAPTURE ${session} ${size}` : `CAM CAPTURE ${session}`;
    const state = await request(
      captureCmd,
      line => parseCamState(line, session, info.maxFrameBytes),
      { timeoutMs: 7000, attempts: 2 },
    );

    onStage("download", { info, state });
    let jpeg = null;
    let tempC = null;
    if (dumpFrame) {
      try {
        const dumped = await dumpFrame(session, info.maxFrameBytes, onProgress);
        if (dumped && dumped.jpeg && dumped.jpeg.length === state.length && dumped.crc32 === state.crc32) {
          jpeg = dumped.jpeg;
          tempC = dumped.tempC;
        }
      } catch {
        jpeg = null;
      }
    }
    if (!jpeg) {
      jpeg = new Uint8Array(state.length);
      for (let offset = 0; offset < state.length; offset += info.chunkBytes) {
        const length = Math.min(info.chunkBytes, state.length - offset);
        const chunk = await request(
          `CAM READ ${session} ${offset}`,
          line => parseCamData(line, session, offset, length),
          { timeoutMs: 1200, attempts: 3 },
        );
        jpeg.set(chunk, offset);
        onProgress(offset + length, state.length);
      }
    }

    const verification = verifyJpeg(jpeg, state);
    return { info, state, jpeg, tempC, verification, released: false, releaseError: null };
  } finally {
    try {
      await request(
        `CAM RELEASE ${session}`,
        line => (line === `CAM RELEASED ${session}` ? true : null),
        { timeoutMs: 1200, attempts: 3 },
      );
      released = true;
    } catch (error) {
      releaseError = error instanceof Error ? error.message : String(error);
    }
    onStage("released", { released, releaseError });
  }
}
