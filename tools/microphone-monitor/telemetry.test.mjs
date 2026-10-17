import test from "node:test";
import assert from "node:assert/strict";
import { HEADER, LineFramer, Telemetry, parseLine } from "./telemetry.mjs";

test("split USB chunks and CRLF form complete rows", () => {
  const framer = new LineFramer();
  assert.deepEqual(framer.push("1000,5."), { lines: [], discarded: 0 });
  assert.deepEqual(framer.push("2,15,Quiet\r\n1500,130.0,1600,Normal\n16"), {
    lines: ["1000,5.2,15,Quiet", "1500,130.0,1600,Normal"], discarded: 0,
  });
  assert.equal(framer.pending, "16");
});

test("overlong input is bounded and recovery starts at the next line", () => {
  const framer = new LineFramer(10);
  assert.deepEqual(framer.push("x".repeat(20)), { lines: [], discarded: 1 });
  assert.equal(framer.pending, "");
  assert.deepEqual(framer.push("tail\nnext\n"), { lines: ["next"], discarded: 0 });
});

test("headers, I2C logs and malformed rows never become measurements", () => {
  assert.equal(parseLine(HEADER).kind, "header");
  assert.deepEqual(parseLine("[73804][E] requestFrom(): i2cRead returned Error -1"), { kind: "diagnostic", i2c: true });
  assert.equal(parseLine("Pomodoro: Focus 1500s running=false").kind, "diagnostic");
  for (const line of ["100,3.1,10,Quiet,extra", "100,NaN,10,Quiet", "100,-1.0,10,Quiet", "100,1.0,2,Off", "4294967296,1.0,2,Quiet"]) {
    assert.equal(parseLine(line).kind, "invalid", line);
  }
  assert.deepEqual(parseLine("73764,7.4,19,Quiet"), { kind: "sample", timestamp: 73764, rms: 7.4, peak: 19, level: "Quiet" });
});

test("device labels are preserved, including a Loud hold on a quiet sample", () => {
  const data = new Telemetry();
  data.accept(parseLine("1000,37.5,93,Loud"));
  assert.equal(data.last.level, "Loud");
  assert.equal(data.csv(), HEADER + "\r\n1000,37.5,93,Loud\r\n");
});

test("rollover stays continuous, reboot starts a plot segment, duplicate is skipped", () => {
  const data = new Telemetry();
  data.accept(parseLine("4294967040,1.0,3,Quiet"));
  data.accept(parseLine("244,2.0,6,Quiet"));
  assert.equal(data.gap, 500);
  assert.equal(data.restarts, 0);
  data.accept(parseLine("244,2.0,6,Quiet"));
  assert.equal(data.duplicates, 1);
  assert.equal(data.count, 2);
  assert.equal(data.accept(parseLine("10,3.0,8,Normal")).restart, true);
  assert.equal(data.restarts, 1);
  assert.equal(data.plot.length, 1);
  assert.equal(data.rows.length, 3);
});

test("gaps are visible and retained CSV rows have a bounded limit", () => {
  const data = new Telemetry(2);
  for (const t of [1000,1500,3510]) data.accept(parseLine(`${t},4.0,10,Quiet`));
  assert.equal(data.maxGap, 2010);
  assert.equal(data.rows.length, 2);
  assert.equal(data.dropped, 1);
  assert.equal(data.count, 3);
  assert.match(data.csv(), /1500,4.0,10,Quiet/);
  assert.doesNotMatch(data.csv(), /\r\n1000,/);
});
