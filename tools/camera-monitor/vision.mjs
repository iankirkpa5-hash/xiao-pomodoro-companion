export function rgbaToLuma(rgba, width, height) {
  const count = width * height;
  if (rgba.length < count * 4) {
    throw new Error("RGBA buffer smaller than width * height * 4");
  }
  const luma = new Uint8Array(count);
  for (let i = 0, p = 0; i < count; i++, p += 4) {
    luma[i] = (77 * rgba[p] + 150 * rgba[p + 1] + 29 * rgba[p + 2]) >> 8;
  }
  return luma;
}

export function computeMotionScore(refLuma, currLuma, width, height, pixelDeltaThreshold = 14) {
  const totalPixels = width * height;
  if (refLuma.length !== totalPixels || currLuma.length !== totalPixels) {
    throw new Error("luma dimensions mismatch");
  }
  let changedPixels = 0;
  let diffSum = 0;
  let minX = width;
  let minY = height;
  let maxX = -1;
  let maxY = -1;

  for (let y = 0; y < height; y++) {
    const row = y * width;
    for (let x = 0; x < width; x++) {
      const idx = row + x;
      const diff = Math.abs(currLuma[idx] - refLuma[idx]);
      diffSum += diff;
      if (diff >= pixelDeltaThreshold) {
        changedPixels++;
        if (x < minX) minX = x;
        if (x > maxX) maxX = x;
        if (y < minY) minY = y;
        if (y > maxY) maxY = y;
      }
    }
  }

  const changedRatio = totalPixels > 0 ? changedPixels / totalPixels : 0;
  const box =
    changedPixels > 0
      ? {
          x: minX / width,
          y: minY / height,
          w: (maxX - minX + 1) / width,
          h: (maxY - minY + 1) / height,
        }
      : null;

  return {
    changedRatio,
    changedPercent: changedRatio * 100,
    meanDiff: totalPixels > 0 ? diffSum / totalPixels : 0,
    changedPixels,
    totalPixels,
    box,
  };
}

export class MotionDetector {
  constructor({
    width = 64,
    height = 48,
    pixelDeltaThreshold = 14,
    triggerPercent = 3.0,
    cooldownMs = 5000,
    warmupFrames = 4,
    bgTauSec = 0.65,
  } = {}) {
    this.width = width;
    this.height = height;
    this.pixelDeltaThreshold = pixelDeltaThreshold;
    this.triggerPercent = triggerPercent;
    this.cooldownMs = cooldownMs;
    this.warmupFrames = warmupFrames;
    this.bgTauSec = bgTauSec;
    this.reset();
  }

  reset() {
    this.bgLuma = null;
    this.lastUpdateMs = null;
    this.framesSeen = 0;
    this.lastTriggerMs = -Infinity;
    this.triggerCount = 0;
  }

  update(currLuma, nowMs) {
    this.framesSeen++;
    if (!this.bgLuma) {
      this.bgLuma = Float32Array.from(currLuma);
      this.lastUpdateMs = nowMs;
      return {
        ready: false,
        triggered: false,
        cooldownRemainingMs: 0,
        score: {
          changedRatio: 0,
          changedPercent: 0,
          meanDiff: 0,
          changedPixels: 0,
          totalPixels: this.width * this.height,
          box: null,
        },
      };
    }

    // Compare against time-normalized EMA background so normal-speed movement
    // is detected equally well at both 6.9 FPS and 27.8 FPS.
    const score = computeMotionScore(
      this.bgLuma,
      currLuma,
      this.width,
      this.height,
      this.pixelDeltaThreshold,
    );

    const dtSec = Math.min(0.5, Math.max(0.01, (nowMs - this.lastUpdateMs) / 1000));
    this.lastUpdateMs = nowMs;
    const alpha = 1 - Math.exp(-dtSec / this.bgTauSec);
    for (let i = 0; i < this.bgLuma.length; i++) {
      this.bgLuma[i] += alpha * (currLuma[i] - this.bgLuma[i]);
    }

    const ready = this.framesSeen > this.warmupFrames;
    const sinceLast = nowMs - this.lastTriggerMs;
    const inCooldown = sinceLast < this.cooldownMs;
    const triggered = ready && !inCooldown && score.changedPercent >= this.triggerPercent;
    if (triggered) {
      this.lastTriggerMs = nowMs;
      this.triggerCount++;
    }

    return {
      ready,
      triggered,
      cooldownRemainingMs: inCooldown ? Math.max(0, this.cooldownMs - sinceLast) : 0,
      score,
    };
  }
}
