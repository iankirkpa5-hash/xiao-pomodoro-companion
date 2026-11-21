const MAX_LINE_LENGTH = 4096;
const INFO_RE = /^CAM INFO (\d+) (\d+) (\d+) (JPEG) (\d+) (\d+) (\d+)$/;
const STATE_RE = /^CAM STATE (\d+) (READY) (\d+) (\d+) (\d+) (\d+) (\d+) ([0-9a-f]{8})$/;
const DATA_RE = /^CAM DATA (\d+) (\d+) ([0-9a-f]+) ([0-9a-f]{8})$/;
const ERROR_RE = /^CAM ERROR (\d+) ([A-Z_]+)$/;
const I2C_ERROR_RE = /i2cRead.*Error|requestFrom\(\).*Error/i;
const FAULT_RE = /read failed|init failed|Guru Meditation|Backtrace:|brownout|watchdog|assert failed|abort\(\)/i;

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

export async function runCapture({
  request,
  session,
  onStage = () => {},
  onProgress = () => {},
}) {
  let released = false;
  let releaseError = null;
  try {
    onStage("info");
    const info = await request("CAM INFO", parseCamInfo, { timeoutMs: 1200, attempts: 5 });

    onStage("capture", { info });
    const state = await request(
      `CAM CAPTURE ${session}`,
      line => parseCamState(line, session, info.maxFrameBytes),
      { timeoutMs: 5500, attempts: 2 },
    );

    onStage("download", { info, state });
    const jpeg = new Uint8Array(state.length);
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

    const verification = verifyJpeg(jpeg, state);
    return { info, state, jpeg, verification, released: false, releaseError: null };
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
