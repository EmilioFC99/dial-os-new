/**
 * Circular 360x360 display geometry constants, easing helpers, 3D rotation,
 * and integer count formatting for DialOS Canvas rendering.
 */

export const DISPLAY_DIAMETER_PX = 360;
export const DISPLAY_CENTER_PX = 180;
export const SAFE_CLIP_RADIUS_PX = 174;
export const OUTER_TICK_RADIUS_PX = 160;
export const BUFFER_ARC_RADIUS_PX = 148;
export const INNER_RING_RADIUS_PX = 112;
export const REGISTRATION_MARK_RADIUS_PX = 136;

export const FULL_CIRCLE_RADIANS = Math.PI * 2;
export const TOP_ANGLE_RADIANS = -Math.PI / 2;
export const MILLISECONDS_PER_SECOND = 1000;
const SECONDS_PER_MINUTE = 60;

const INTEGER_COUNT_FORMATTER = new Intl.NumberFormat("en-US");

/**
 * Clamp a numeric value within [minimum, maximum].
 *
 * @param {number} value Input number.
 * @param {number} minimum Lower bound.
 * @param {number} maximum Upper bound.
 * @returns {number} Clamped value.
 */
export function clamp(value, minimum, maximum) {
  return Math.min(Math.max(value, minimum), maximum);
}

/**
 * Map a value within [rangeStart, rangeEnd] into normalized [0, 1].
 *
 * @param {number} value Input scalar.
 * @param {number} rangeStart Start of range.
 * @param {number} rangeEnd End of range.
 * @returns {number} Normalized progress in [0, 1].
 */
export function normalizeRange(value, rangeStart, rangeEnd) {
  if (rangeEnd <= rangeStart) {
    return value >= rangeEnd ? 1 : 0;
  }
  return clamp((value - rangeStart) / (rangeEnd - rangeStart), 0, 1);
}

/**
 * Linearly interpolate between startValue and endValue by factor.
 *
 * @param {number} startValue Start scalar.
 * @param {number} endValue End scalar.
 * @param {number} factor Interpolation factor.
 * @returns {number} Interpolated scalar.
 */
export function lerp(startValue, endValue, factor) {
  return startValue + (endValue - startValue) * factor;
}

/**
 * Cubic ease-out curve on clamped progress [0, 1].
 *
 * @param {number} progress Normalized progress.
 * @returns {number} Eased progress.
 */
export function easeOutCubic(progress) {
  const inverted = 1 - clamp(progress, 0, 1);
  return 1 - inverted * inverted * inverted;
}

/**
 * Exponential ease-out curve on clamped progress [0, 1].
 *
 * @param {number} progress Normalized progress.
 * @returns {number} Eased progress.
 */
export function easeOutExpo(progress) {
  const clamped = clamp(progress, 0, 1);
  return clamped === 1 ? 1 : 1 - Math.pow(2, -10 * clamped);
}

/**
 * Symmetric cubic ease-in-out curve on clamped progress [0, 1].
 *
 * @param {number} progress Normalized progress.
 * @returns {number} Eased progress.
 */
export function easeInOutCubic(progress) {
  const clamped = clamp(progress, 0, 1);
  return clamped < 0.5
    ? 4 * clamped * clamped * clamped
    : 1 - Math.pow(-2 * clamped + 2, 3) / 2;
}

/**
 * Rotate a 3D point sequentially around Y, X, and Z axes for orthographic rendering.
 *
 * @param {number} pointX X coordinate.
 * @param {number} pointY Y coordinate.
 * @param {number} pointZ Z coordinate.
 * @param {number} angleX Rotation around X axis in radians.
 * @param {number} angleY Rotation around Y axis in radians.
 * @param {number} angleZ Rotation around Z axis in radians.
 * @returns {{ x: number, y: number, z: number }} Rotated 3D coordinates.
 */
export function rotatePoint3d(pointX, pointY, pointZ, angleX, angleY, angleZ) {
  const cosY = Math.cos(angleY);
  const sinY = Math.sin(angleY);
  const rotatedX1 = pointX * cosY + pointZ * sinY;
  const rotatedZ1 = -pointX * sinY + pointZ * cosY;

  const cosX = Math.cos(angleX);
  const sinX = Math.sin(angleX);
  const rotatedY2 = pointY * cosX - rotatedZ1 * sinX;
  const rotatedZ2 = pointY * sinX + rotatedZ1 * cosX;

  const cosZ = Math.cos(angleZ);
  const sinZ = Math.sin(angleZ);
  return {
    x: rotatedX1 * cosZ - rotatedY2 * sinZ,
    y: rotatedX1 * sinZ + rotatedY2 * cosZ,
    z: rotatedZ2,
  };
}

/**
 * Project a 3D point onto the 2D display plane using perspective division.
 *
 * @param {number} pointX 3D X coordinate relative to center.
 * @param {number} pointY 3D Y coordinate relative to center.
 * @param {number} pointZ 3D Z coordinate toward the viewer.
 * @param {number} centerX 2D projection center X in pixels.
 * @param {number} centerY 2D projection center Y in pixels.
 * @param {number} focalLength Perspective focal distance in pixels.
 * @returns {{ screenX: number, screenY: number, depthZ: number, scale: number }} Projected screen coordinates.
 */
export function projectPoint3d(pointX, pointY, pointZ, centerX, centerY, focalLength) {
  const depthDenominator = focalLength - pointZ;
  const perspectiveScale = depthDenominator !== 0 ? focalLength / depthDenominator : 1;
  return {
    screenX: centerX + pointX * perspectiveScale,
    screenY: centerY + pointY * perspectiveScale,
    depthZ: pointZ,
    scale: perspectiveScale,
  };
}

/**
 * Format a non-negative integer count with US grouping commas.
 *
 * @param {number} value Numeric count to format.
 * @returns {string} Formatted integer string.
 */
export function formatIntegerCount(value) {
  const numericValue = Number(value || 0);
  const safeInteger = Number.isFinite(numericValue)
    ? Math.max(0, Math.round(numericValue))
    : 0;
  return INTEGER_COUNT_FORMATTER.format(safeInteger);
}

/**
 * Format elapsed or duration seconds safely as zero-padded "MM:SS".
 *
 * @param {number} totalSeconds Duration in seconds.
 * @returns {string} Formatted clock string, or "00:00" when negative/non-finite.
 */
export function formatClockTime(totalSeconds) {
  if (typeof totalSeconds !== "number" || !Number.isFinite(totalSeconds) || totalSeconds <= 0) {
    return "00:00";
  }
  const wholeSeconds = Math.floor(totalSeconds);
  const minutes = Math.floor(wholeSeconds / SECONDS_PER_MINUTE);
  const seconds = wholeSeconds % SECONDS_PER_MINUTE;
  return `${String(minutes).padStart(2, "0")}:${String(seconds).padStart(2, "0")}`;
}
