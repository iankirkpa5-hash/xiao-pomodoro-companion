export const HEADER = "timestamp_ms,rms,peak,level";
const UINT32 = 2 ** 32;

export function parseLine(raw) {
  const line = raw.trim();
  if (!line) return { kind: "empty" };
  if (line === HEADER) return { kind: "header" };
  const i2c = /i2cRead|requestFrom\(\).*Error/.test(line);
  if (/^\d+,/.test(line)) {
    const fields = line.split(",");
    if (fields.length === 4 && /^\d+$/.test(fields[0]) &&
        /^\d+(?:\.\d+)?$/.test(fields[1]) && /^\d+$/.test(fields[2]) &&
        /^(Quiet|Normal|Loud)$/.test(fields[3])) {
      const [timestamp, rms, peak] = fields.slice(0, 3).map(Number);
      if (Number.isSafeInteger(timestamp) && timestamp < UINT32 &&
          Number.isFinite(rms) && Number.isSafeInteger(peak)) {
        return { kind: "sample", timestamp, rms, peak, level: fields[3] };
      }
    }
    return { kind: "invalid", i2c };
  }
  return { kind: "diagnostic", i2c };
}

// USB reads can split one line across chunks or combine many complete lines.
export class LineFramer {
  constructor(maxLength = 8192) {
    this.maxLength = maxLength;
    this.pending = "";
    this.dropping = false;
  }

  push(chunk) {
    const lines = [];
    let discarded = 0;
    const pieces = chunk.split("\n");
    for (let i = 0; i < pieces.length; i++) {
      if (!this.dropping) {
        if (this.pending.length + pieces[i].length > this.maxLength) {
          this.pending = "";
          this.dropping = true;
          discarded++;
        } else {
          this.pending += pieces[i];
        }
      }
      if (i < pieces.length - 1) {
        if (!this.dropping) lines.push(this.pending.replace(/\r$/, ""));
        this.pending = "";
        this.dropping = false;
      }
    }
    return { lines, discarded };
  }
}

export class Telemetry {
  constructor(capacity = 10000) {
    this.capacity = capacity;
    this.rows = [];
    this.plot = [];
    this.count = 0;
    this.dropped = 0;
    this.i2cErrors = 0;
    this.invalid = 0;
    this.restarts = 0;
    this.duplicates = 0;
    this.last = null;
    this.gap = null;
    this.maxGap = 0;
    this.plotTime = 0;
  }

  accept(sample) {
    let restart = false;
    if (this.last) {
      const delta = (sample.timestamp - this.last.timestamp + UINT32) % UINT32;
      if (delta === 0) {
        this.duplicates++;
        return { accepted: false, restart: false };
      }
      // A normal millis() rollover has a small forward unsigned delta.
      restart = delta > UINT32 / 2;
      if (restart) {
        this.restarts++;
        this.plot = [];
        this.plotTime = 0;
        this.gap = null;
      } else {
        this.gap = delta;
        this.maxGap = Math.max(this.maxGap, delta);
        this.plotTime += delta;
      }
    }
    this.last = sample;
    this.count++;
    this.rows.push(sample);
    if (this.rows.length > this.capacity) {
      this.rows.shift();
      this.dropped++;
    }
    this.plot.push({ ...sample, time: this.plotTime });
    while (this.plot.length > 1 && this.plot[0].time < this.plotTime - 60000) this.plot.shift();
    // Bound memory even if an unexpected device sends at a much higher rate.
    if (this.plot.length > 2000) this.plot.shift();
    return { accepted: true, restart };
  }

  csv() {
    return HEADER + "\r\n" + this.rows.map(row =>
      `${row.timestamp},${row.rms.toFixed(1)},${row.peak},${row.level}`
    ).join("\r\n") + (this.rows.length ? "\r\n" : "");
  }
}
