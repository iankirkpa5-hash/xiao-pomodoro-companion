import assert from "node:assert/strict";
import test from "node:test";
import {
  LineFramer,
  classifyDiagnostic,
  crc32,
  hexToBytes,
  parseCamData,
  parseCamError,
  parseCamInfo,
  parseCamState,
  runCapture,
  verifyJpeg,
} from "./protocol.mjs";

function makeJpeg(payloadLen = 140, trailingZeros = 0) {
  const bytes = new Uint8Array(2 + payloadLen + 2 + trailingZeros);
  bytes[0] = 0xff;
  bytes[1] = 0xd8;
  for (let i = 0; i < payloadLen; i++) bytes[2 + i] = (i * 31 + 11) & 0xff;
  bytes[2 + payloadLen] = 0xff;
  bytes[3 + payloadLen] = 0xd9;
  return bytes;
}

function bytesToHex(bytes) {
  return Array.from(bytes, b => b.toString(16).padStart(2, "0")).join("");
}

test("IEEE CRC32 matches standard test vector", () => {
  const input = new TextEncoder().encode("123456789");
  assert.equal(crc32(input), 0xcbf43926);
  assert.deepEqual(hexToBytes("ffd8ffd9"), new Uint8Array([0xff, 0xd8, 0xff, 0xd9]));
  assert.throws(() => hexToBytes("abc"));
  assert.throws(() => hexToBytes("zzzz"));
});

test("LineFramer splits chunks, handles CRLF, and discards overlong lines", () => {
  const framer = new LineFramer();
  assert.deepEqual(framer.push("CAM INFO "), { lines: [], discarded: 0 });
  assert.deepEqual(framer.push("1 320 240 JPEG 131072 64 8000000\r\n"), {
    lines: ["CAM INFO 1 320 240 JPEG 131072 64 8000000"],
    discarded: 0,
  });
  const overlong = "x".repeat(4100) + "\nCAM RELEASED 1\n";
  assert.deepEqual(framer.push(overlong), { lines: ["CAM RELEASED 1"], discarded: 1 });
});

test("CAM INFO, STATE, DATA, and ERROR parsing validate fields and CRC", () => {
  const info = parseCamInfo("CAM INFO 1 320 240 JPEG 131072 64 8000000");
  assert.equal(info.width, 320);
  assert.equal(info.chunkBytes, 64);
  assert.throws(() => parseCamInfo("CAM INFO 2 320 240 JPEG 131072 64 8000000"));
  assert.equal(parseCamInfo("Pomodoro: Focus 1500s running=false"), null);

  const jpeg = makeJpeg(100, 2);
  const stateLine = `CAM STATE 42 READY ${jpeg.length} 320 240 4 260 ${crc32(jpeg).toString(16).padStart(8, "0")}`;
  const state = parseCamState(stateLine, 42, 131072);
  assert.equal(state.length, jpeg.length);
  assert.equal(parseCamState(stateLine, 43, 131072), null);
  assert.throws(() => parseCamState(stateLine.replace(" 4 260", " 0 260"), 42, 131072));

  const chunk = jpeg.subarray(0, 64);
  const dataLine = `CAM DATA 42 0 ${bytesToHex(chunk)} ${crc32(chunk).toString(16).padStart(8, "0")}`;
  assert.deepEqual(parseCamData(dataLine, 42, 0, 64), chunk);
  assert.equal(parseCamData(dataLine, 42, 64, 64), null);
  assert.throws(() => parseCamData(dataLine.slice(0, -8) + "00000000", 42, 0, 64));

  assert.deepEqual(parseCamError("CAM ERROR 42 INIT_FAILED", 42), { id: 42, reason: "INIT_FAILED" });
  assert.equal(parseCamError("CAM ERROR 99 INIT_FAILED", 42), null);
  assert.equal(classifyDiagnostic("i2cRead() Error").i2cError, true);
  assert.equal(classifyDiagnostic("Guru Meditation Error").fault, true);
});

test("verifyJpeg accepts OV2640 zero padding and rejects malformed frames", () => {
  const jpeg = makeJpeg(80, 3);
  const state = { state: "READY", length: jpeg.length, crc32: crc32(jpeg) };
  const details = verifyJpeg(jpeg, state);
  assert.equal(details.soiValid, true);
  assert.equal(details.trailingZeroBytes, 3);

  const badSoi = jpeg.slice();
  badSoi[0] = 0x00;
  assert.throws(() => verifyJpeg(badSoi, { state: "READY", length: badSoi.length, crc32: crc32(badSoi) }));

  const nonZeroTail = jpeg.slice();
  nonZeroTail[nonZeroTail.length - 1] = 0x01;
  assert.throws(() =>
    verifyJpeg(nonZeroTail, { state: "READY", length: nonZeroTail.length, crc32: crc32(nonZeroTail) }),
  );
});

test("runCapture downloads verified JPEG, handles retries, and always releases buffer", async () => {
  const jpeg = makeJpeg(150, 1);
  const commands = [];
  const progress = [];
  let releasedStage = null;

  async function request(command, parse) {
    commands.push(command);
    if (command === "CAM INFO") {
      return parse("CAM INFO 1 320 240 JPEG 131072 64 8000000");
    }
    if (command === "CAM CAPTURE 77") {
      return parse(
        `CAM STATE 77 READY ${jpeg.length} 320 240 4 270 ${crc32(jpeg).toString(16).padStart(8, "0")}`,
      );
    }
    if (command.startsWith("CAM READ 77 ")) {
      const offset = Number(command.split(" ")[3]);
      const chunk = jpeg.subarray(offset, Math.min(offset + 64, jpeg.length));
      return parse(`CAM DATA 77 ${offset} ${bytesToHex(chunk)} ${crc32(chunk).toString(16).padStart(8, "0")}`);
    }
    if (command === "CAM RELEASE 77") {
      return parse("CAM RELEASED 77");
    }
    throw new Error("unexpected command: " + command);
  }

  const result = await runCapture({
    request,
    session: 77,
    onStage: (stage, detail) => {
      if (stage === "released") releasedStage = detail;
    },
    onProgress: (done, total) => progress.push([done, total]),
  });

  assert.deepEqual(result.jpeg, jpeg);
  assert.equal(result.verification.trailingZeroBytes, 1);
  assert.equal(progress[progress.length - 1][0], jpeg.length);
  assert.equal(commands[commands.length - 1], "CAM RELEASE 77");
  assert.deepEqual(releasedStage, { released: true, releaseError: null });
});

test("runCapture still sends CAM RELEASE after a mid-download error", async () => {
  const commands = [];
  await assert.rejects(
    runCapture({
      session: 88,
      async request(command, parse) {
        commands.push(command);
        if (command === "CAM INFO") return parse("CAM INFO 1 320 240 JPEG 131072 64 8000000");
        if (command === "CAM CAPTURE 88") throw new Error("device: INIT_FAILED");
        if (command === "CAM RELEASE 88") return parse("CAM RELEASED 88");
        return null;
      },
    }),
    /INIT_FAILED/,
  );
  assert.deepEqual(commands, ["CAM INFO", "CAM CAPTURE 88", "CAM RELEASE 88"]);
});

test("StreamFrameAssembler reassembles Base64 JPEG frames and drops corrupt frames", async () => {
  const {
    StreamFrameAssembler,
    base64ToBytes,
    parseStreamPong,
    parseStreamStarted,
    parseStreamStopped,
  } = await import("./protocol.mjs");

  assert.deepEqual(base64ToBytes("Zm9v"), new TextEncoder().encode("foo"));
  assert.deepEqual(base64ToBytes("Zm8="), new TextEncoder().encode("fo"));
  assert.deepEqual(base64ToBytes("Zg=="), new TextEncoder().encode("f"));
  assert.throws(() => base64ToBytes("abc"));

  assert.deepEqual(parseStreamStarted("CAM STREAMING 9 150 180000 415", 9), {
    intervalMs: 150,
    maxMs: 180000,
    tempC: 41.5,
  });
  assert.deepEqual(parseStreamPong("CAM PONG 9 120000 420", 9), {
    remainingMs: 120000,
    tempC: 42.0,
  });
  assert.deepEqual(parseStreamStopped("CAM STOPPED 9 WATCHDOG", 9), { reason: "WATCHDOG" });

  const jpeg = makeJpeg(250, 2);
  const b64All = Buffer.from(jpeg).toString("base64");
  const assembler = new StreamFrameAssembler(9);
  assert.equal(
    assembler.feed(`CAM FRAME 9 1 ${jpeg.length} 320 240 ${crc32(jpeg).toString(16).padStart(8, "0")} 415`),
    null,
  );
  // Split in valid 4-char multiples simulating 120-byte chunks.
  for (let i = 0; i < b64All.length; i += 160) {
    assert.equal(assembler.feed(`CAM FDATA 9 1 ${b64All.slice(i, i + 160)}`), null);
  }
  const assembled = assembler.feed("CAM FEND 9 1");
  assert.ok(assembled);
  assert.deepEqual(assembled.jpeg, jpeg);
  assert.equal(assembled.tempC, 41.5);
  assert.equal(assembler.droppedFrames, 0);

  // Corrupt CRC frame is cleanly dropped without throwing.
  assembler.feed(`CAM FRAME 9 2 ${jpeg.length} 320 240 00000000 420`);
  assembler.feed(`CAM FDATA 9 2 ${b64All}`);
  assert.equal(assembler.feed("CAM FEND 9 2"), null);
  assert.equal(assembler.droppedFrames, 1);
});
