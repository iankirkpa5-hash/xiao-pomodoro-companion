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
  const noisy = solidLuma(10, 10, 108); // diff 8 < threshold 14
  const quietScore = computeMotionScore(a, noisy, 10, 10, 14);
  assert.equal(quietScore.changedPixels, 0);
  assert.equal(quietScore.box, null);

  const moved = a.slice();
  for (let y = 2; y <= 3; y++) {
    for (let x = 4; x <= 5; x++) moved[y * 10 + x] = 180;
  }
  const score = computeMotionScore(a, moved, 10, 10, 14);
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

  // Warmup frames never trigger even with huge change.
  assert.equal(det.update(still, 0).triggered, false);
  assert.equal(det.update(still, 100).triggered, false);

  // Frame 3 (> warmupFrames) with large change -> triggers!
  const hit1 = det.update(active, 200);
  assert.equal(hit1.ready, true);
  assert.equal(hit1.triggered, true);
  assert.equal(det.triggerCount, 1);

  // Within cooldown -> suppressed!
  const duringCooldown = det.update(still, 1500);
  assert.equal(duringCooldown.triggered, false);
  assert.equal(duringCooldown.cooldownRemainingMs, 1700);
  assert.equal(det.triggerCount, 1);

  // After cooldown expires with new motion -> triggers again!
  const hit2 = det.update(active, 3300);
  assert.equal(hit2.triggered, true);
  assert.equal(det.triggerCount, 2);
});

test("MotionDetector detects normal-speed gradual motion at 28 FPS and settles when still", () => {
  const det = new MotionDetector({
    width: 10,
    height: 10,
    pixelDeltaThreshold: 14,
    triggerPercent: 3.0,
    cooldownMs: 5000,
    warmupFrames: 3,
    bgTauSec: 0.65,
  });

  // Warmup stationary at luma=60 (36 ms per frame ~ 27.8 FPS).
  let t = 0;
  for (let i = 0; i < 4; i++) {
    assert.equal(det.update(solidLuma(10, 10, 60), t).triggered, false);
    t += 36;
  }

  // Slow gradual movement: 10% of pixels brighten by only +4 luma per 36 ms frame
  // (would NEVER exceed pixelDeltaThreshold=14 in 1-frame diff!).
  let triggeredAt = -1;
  for (let step = 1; step <= 15; step++) {
    const frame = solidLuma(10, 10, 60);
    const val = Math.min(255, 60 + step * 4);
    for (let i = 0; i < 10; i++) frame[i] = val;
    const res = det.update(frame, t);
    if (res.triggered && triggeredAt < 0) triggeredAt = step;
    t += 36;
  }
  assert.ok(triggeredAt > 0, "expected slow motion at 28 FPS to trigger EMA detector");

  // Once the subject stays still at the new pose for ~2.5s, EMA background converges back to 0%.
  const settled = solidLuma(10, 10, 60);
  for (let i = 0; i < 10; i++) settled[i] = 120;
  let lastScore = null;
  for (let i = 0; i < 70; i++) {
    lastScore = det.update(settled, t).score;
    t += 36;
  }
  assert.equal(lastScore.changedPixels, 0);
});
