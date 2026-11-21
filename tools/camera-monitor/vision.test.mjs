import assert from "node:assert/strict";
import test from "node:test";
import { MotionDetector, computeMotionScore, rgbaToLuma } from "./vision.mjs";

function solidLuma(width, height, value) {
  return new Uint8Array(width * height).fill(value);
}

test("rgbaToLuma converts RGB pixels using BT.601 weights", () => {
  const rgba = new Uint8Array([
    0, 0, 0, 255,
    255, 255, 255, 255,
    255, 0, 0, 255,
    0, 255, 0, 255,
  ]);
  const luma = rgbaToLuma(rgba, 2, 2);
  assert.equal(luma[0], 0);
  assert.equal(luma[1], 255);
  assert.ok(luma[2] >= 75 && luma[2] <= 77);
  assert.ok(luma[3] >= 148 && luma[3] <= 150);
  assert.throws(() => rgbaToLuma(new Uint8Array(4), 2, 2));
});

test("computeMotionScore ignores small noise and locates changed bounding box", () => {
  const a = solidLuma(10, 10, 100);
  const noisy = solidLuma(10, 10, 108); // diff 8 < threshold 18
  const quietScore = computeMotionScore(a, noisy, 10, 10, 18);
  assert.equal(quietScore.changedPixels, 0);
  assert.equal(quietScore.box, null);

  const moved = a.slice();
  // Change a 2x2 block at x=4..5, y=2..3 (4% of 100 pixels)
  for (let y = 2; y <= 3; y++) {
    for (let x = 4; x <= 5; x++) moved[y * 10 + x] = 180;
  }
  const score = computeMotionScore(a, moved, 10, 10, 18);
  assert.equal(score.changedPixels, 4);
  assert.equal(score.changedPercent, 4);
  assert.deepEqual(score.box, { x: 0.4, y: 0.2, w: 0.2, h: 0.2 });
});

test("MotionDetector enforces warmup frames and cooldown between triggers", () => {
  const det = new MotionDetector({
    width: 10,
    height: 10,
    triggerPercent: 5,
    cooldownMs: 3000,
    warmupFrames: 2,
  });
  const still = solidLuma(10, 10, 50);
  const active = solidLuma(10, 10, 200);

  // Frame 1 & 2 are warmup -> never trigger even with huge change.
  assert.equal(det.update(still, 0).triggered, false);
  assert.equal(det.update(active, 100).triggered, false);

  // Frame 3 (> warmupFrames) with large change -> triggers!
  const hit1 = det.update(still, 200);
  assert.equal(hit1.ready, true);
  assert.equal(hit1.triggered, true);
  assert.equal(det.triggerCount, 1);

  // Frame 4 within cooldown -> suppressed!
  const duringCooldown = det.update(active, 1500);
  assert.equal(duringCooldown.triggered, false);
  assert.equal(duringCooldown.cooldownRemainingMs, 1700);
  assert.equal(det.triggerCount, 1);

  // Frame 5 after cooldown expires -> triggers again!
  const hit2 = det.update(still, 3300);
  assert.equal(hit2.triggered, true);
  assert.equal(det.triggerCount, 2);
});
