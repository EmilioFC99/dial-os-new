/**
 * Shared Canvas 2D primitives matching the DialOS graphic engine specification
 * (pure geometric vector shapes, crisp strokes, zero gradients or shadows).
 */

import {
  DISPLAY_CENTER_PX,
  DISPLAY_DIAMETER_PX,
  FULL_CIRCLE_RADIANS,
  OUTER_TICK_RADIUS_PX,
  SAFE_CLIP_RADIUS_PX,
  TOP_ANGLE_RADIANS,
  clamp,
  easeOutExpo,
  lerp,
} from "./geometry.js";
import { FONT_MONO } from "./tokens.js";

const ALPHA_VISIBILITY_THRESHOLD = 0.005;
const SIMULATOR_BADGE_LABEL = "SIMULATOR";
const SIMULATOR_BADGE_HEIGHT_PX = 16;
const SIMULATOR_BADGE_PADDING_PX = 8;
const SIMULATOR_INDICATOR_SIZE_PX = 4;
const SIMULATOR_INDICATOR_GAP_PX = 6;

/**
 * Draw an orthogonal '+' registration mark centered at (centerX, centerY).
 *
 * @param {CanvasRenderingContext2D} drawingContext Canvas 2D context.
 * @param {number} centerX Center X coordinate.
 * @param {number} centerY Center Y coordinate.
 * @param {number} halfLength Half arm length in pixels.
 * @param {number} lineWidth Stroke thickness.
 * @param {string} color Stroke color.
 * @param {number} alpha Layer opacity.
 */
export function drawCrosshairMark(
  drawingContext,
  centerX,
  centerY,
  halfLength,
  lineWidth,
  color,
  alpha,
) {
  if (alpha <= ALPHA_VISIBILITY_THRESHOLD) {
    return;
  }
  drawingContext.save();
  drawingContext.globalAlpha = alpha;
  drawingContext.strokeStyle = color;
  drawingContext.lineWidth = lineWidth;
  drawingContext.beginPath();
  drawingContext.moveTo(centerX - halfLength, centerY);
  drawingContext.lineTo(centerX + halfLength, centerY);
  drawingContext.moveTo(centerX, centerY - halfLength);
  drawingContext.lineTo(centerX, centerY + halfLength);
  drawingContext.stroke();
  drawingContext.restore();
}

/**
 * Draw a 45-degree 'x' registration mark centered at (centerX, centerY).
 *
 * @param {CanvasRenderingContext2D} drawingContext Canvas 2D context.
 * @param {number} centerX Center X coordinate.
 * @param {number} centerY Center Y coordinate.
 * @param {number} halfSpan Half diagonal span in pixels.
 * @param {number} lineWidth Stroke thickness.
 * @param {string} color Stroke color.
 * @param {number} alpha Layer opacity.
 */
export function drawDiagonalMark(
  drawingContext,
  centerX,
  centerY,
  halfSpan,
  lineWidth,
  color,
  alpha,
) {
  if (alpha <= ALPHA_VISIBILITY_THRESHOLD) {
    return;
  }
  drawingContext.save();
  drawingContext.globalAlpha = alpha;
  drawingContext.strokeStyle = color;
  drawingContext.lineWidth = lineWidth;
  drawingContext.beginPath();
  drawingContext.moveTo(centerX - halfSpan, centerY - halfSpan);
  drawingContext.lineTo(centerX + halfSpan, centerY + halfSpan);
  drawingContext.moveTo(centerX + halfSpan, centerY - halfSpan);
  drawingContext.lineTo(centerX - halfSpan, centerY + halfSpan);
  drawingContext.stroke();
  drawingContext.restore();
}

/**
 * Draw precision radial perimeter tick marks clockwise from 12 o'clock.
 *
 * @param {CanvasRenderingContext2D} drawingContext Canvas 2D context.
 * @param {number} tickCount Total number of ticks around the full circle.
 * @param {number} innerRadius Base inner radius of standard ticks.
 * @param {number} outerRadius Outer radius of ticks.
 * @param {number} visibleRatio Fraction of ticks revealed in [0, 1].
 * @param {object} palette Semantic Dark/Light appearance palette.
 * @param {number} baseAlpha Base opacity multiplier.
 */
export function drawPerimeterTicks(
  drawingContext,
  tickCount,
  innerRadius,
  outerRadius,
  visibleRatio,
  palette,
  baseAlpha,
) {
  const visibleTicks = Math.floor(tickCount * clamp(visibleRatio, 0, 1));
  if (visibleTicks <= 0 || baseAlpha <= ALPHA_VISIBILITY_THRESHOLD) {
    return;
  }

  drawingContext.save();
  for (let tickIndex = 0; tickIndex < visibleTicks; tickIndex += 1) {
    const angleRadians = TOP_ANGLE_RADIANS + (tickIndex / tickCount) * FULL_CIRCLE_RADIANS;
    const isMajorTick = tickIndex % 12 === 0;
    const isMidTick = tickIndex % 6 === 0 && !isMajorTick;
    const tickStart = isMajorTick
      ? innerRadius - 4
      : isMidTick
        ? innerRadius - 1.5
        : innerRadius + 1;
    const tickEnd = outerRadius;

    drawingContext.strokeStyle = isMajorTick ? palette.inkSecondary : palette.hairlineStrong;
    drawingContext.globalAlpha = baseAlpha * (isMajorTick ? 0.9 : 0.45);
    drawingContext.lineWidth = isMajorTick ? 1.5 : 1;

    drawingContext.beginPath();
    drawingContext.moveTo(
      DISPLAY_CENTER_PX + Math.cos(angleRadians) * tickStart,
      DISPLAY_CENTER_PX + Math.sin(angleRadians) * tickStart,
    );
    drawingContext.lineTo(
      DISPLAY_CENTER_PX + Math.cos(angleRadians) * tickEnd,
      DISPLAY_CENTER_PX + Math.sin(angleRadians) * tickEnd,
    );
    drawingContext.stroke();
  }
  drawingContext.restore();
}

/**
 * Draw the DialOS Disc-Dial emblem (broken optical ring, inner track, spindle,
 * and angled Signal detent block).
 *
 * @param {CanvasRenderingContext2D} drawingContext Canvas 2D context.
 * @param {number} centerX Center X coordinate.
 * @param {number} centerY Center Y coordinate.
 * @param {number} radius Outer dial radius.
 * @param {object} palette Semantic Dark/Light appearance palette.
 * @param {number} alpha Layer opacity.
 * @param {number} detentPop Pop-in progress for the radial detent block [0, 1].
 */
export function drawDialOsEmblem(
  drawingContext,
  centerX,
  centerY,
  radius,
  palette,
  alpha,
  detentPop = 1,
) {
  if (alpha <= ALPHA_VISIBILITY_THRESHOLD) {
    return;
  }
  drawingContext.save();
  drawingContext.translate(centerX, centerY);
  drawingContext.globalAlpha = alpha;

  const ringThickness = Math.max(5, radius * 0.25);
  const gapStartAngle = -Math.PI * 0.36;
  const gapEndAngle = -Math.PI * 0.06;

  drawingContext.beginPath();
  drawingContext.arc(0, 0, radius, gapEndAngle, gapStartAngle + FULL_CIRCLE_RADIANS, false);
  drawingContext.strokeStyle = palette.inkPrimary;
  drawingContext.lineWidth = ringThickness;
  drawingContext.lineCap = "butt";
  drawingContext.stroke();

  drawingContext.beginPath();
  drawingContext.arc(0, 0, radius * 0.56, 0, FULL_CIRCLE_RADIANS);
  drawingContext.strokeStyle = palette.hairlineStrong;
  drawingContext.lineWidth = 1.25;
  drawingContext.stroke();

  drawingContext.beginPath();
  drawingContext.arc(0, 0, radius * 0.22, 0, FULL_CIRCLE_RADIANS);
  drawingContext.fillStyle = palette.inkPrimary;
  drawingContext.fill();

  const detentAngle = (gapStartAngle + gapEndAngle) / 2;
  const detentScale = lerp(0.4, 1.0, easeOutExpo(detentPop));
  const detentSize = ringThickness * 0.96 * detentScale;
  const detentX = Math.cos(detentAngle) * radius;
  const detentY = Math.sin(detentAngle) * radius;

  drawingContext.save();
  drawingContext.translate(detentX, detentY);
  drawingContext.rotate(detentAngle + Math.PI / 4);
  drawingContext.fillStyle = palette.signalHighlight;
  drawingContext.fillRect(-detentSize / 2, -detentSize / 2, detentSize, detentSize);
  drawingContext.restore();

  drawingContext.restore();
}

/**
 * Draw a rectangular clipped band of animated diagonal warning hazard stripes.
 *
 * @param {CanvasRenderingContext2D} drawingContext Canvas 2D context.
 * @param {number} originX Band top-left X coordinate.
 * @param {number} originY Band top-left Y coordinate.
 * @param {number} bandWidth Width of hazard band.
 * @param {number} bandHeight Height of hazard band.
 * @param {number} phaseOffset Horizontal scroll offset in pixels.
 * @param {string} fillColor Stripe fill color.
 */
export function drawHazardStripeBand(
  drawingContext,
  originX,
  originY,
  bandWidth,
  bandHeight,
  phaseOffset,
  fillColor,
) {
  drawingContext.save();
  drawingContext.beginPath();
  drawingContext.rect(originX, originY, bandWidth, bandHeight);
  drawingContext.clip();

  drawingContext.fillStyle = fillColor;
  const stripePitch = 16;
  const stripeWidth = 7;
  const shiftedOffset = ((phaseOffset % stripePitch) + stripePitch) % stripePitch;

  for (
    let stripeX = originX - bandHeight - stripePitch;
    stripeX < originX + bandWidth + stripePitch;
    stripeX += stripePitch
  ) {
    drawingContext.beginPath();
    drawingContext.moveTo(stripeX + shiftedOffset, originY + bandHeight);
    drawingContext.lineTo(stripeX + shiftedOffset + stripeWidth, originY + bandHeight);
    drawingContext.lineTo(stripeX + shiftedOffset + stripeWidth + bandHeight, originY);
    drawingContext.lineTo(stripeX + shiftedOffset + bandHeight, originY);
    drawingContext.closePath();
    drawingContext.fill();
  }
  drawingContext.restore();
}

/**
 * Draw optional safe-area guide overlays (R174 hardware clip, R160 text chord, center axes).
 *
 * @param {CanvasRenderingContext2D} drawingContext Canvas 2D context.
 * @param {object} palette Semantic Dark/Light appearance palette.
 */
export function drawSafeAreaGuides(drawingContext, palette) {
  drawingContext.save();
  drawingContext.setLineDash([4, 4]);
  drawingContext.lineWidth = 1;

  drawingContext.strokeStyle = palette.signalActive;
  drawingContext.globalAlpha = 0.7;
  drawingContext.beginPath();
  drawingContext.arc(DISPLAY_CENTER_PX, DISPLAY_CENTER_PX, SAFE_CLIP_RADIUS_PX, 0, FULL_CIRCLE_RADIANS);
  drawingContext.stroke();

  drawingContext.strokeStyle = palette.signalHighlight;
  drawingContext.globalAlpha = 0.65;
  drawingContext.beginPath();
  drawingContext.arc(DISPLAY_CENTER_PX, DISPLAY_CENTER_PX, OUTER_TICK_RADIUS_PX, 0, FULL_CIRCLE_RADIANS);
  drawingContext.stroke();

  drawingContext.strokeStyle = palette.inkSecondary;
  drawingContext.globalAlpha = 0.35;
  drawingContext.beginPath();
  drawingContext.moveTo(0, DISPLAY_CENTER_PX);
  drawingContext.lineTo(DISPLAY_DIAMETER_PX, DISPLAY_CENTER_PX);
  drawingContext.moveTo(DISPLAY_CENTER_PX, 0);
  drawingContext.lineTo(DISPLAY_CENTER_PX, DISPLAY_DIAMETER_PX);
  drawingContext.stroke();

  drawingContext.setLineDash([]);
  drawingContext.font = `600 9px ${FONT_MONO}`;
  drawingContext.fillStyle = palette.signalActive;
  drawingContext.textAlign = "center";
  drawingContext.globalAlpha = 0.9;
  drawingContext.fillText("R174 CLIP", DISPLAY_CENTER_PX, 18);

  drawingContext.fillStyle = palette.signalHighlight;
  drawingContext.fillText("R160 CHORD", DISPLAY_CENTER_PX, 32);

  drawingContext.restore();
}

/**
 * Draw a sharp 0px-radius industrial monospace "SIMULATOR" badge centered at (centerX, centerY).
 *
 * @param {CanvasRenderingContext2D} drawingContext Canvas 2D context.
 * @param {number} centerX Center X coordinate.
 * @param {number} centerY Center Y coordinate.
 * @param {object} palette Semantic Dark/Light appearance palette.
 * @param {number} alpha Layer opacity.
 */
export function drawSimulatorBadge(drawingContext, centerX, centerY, palette, alpha = 1) {
  if (alpha <= ALPHA_VISIBILITY_THRESHOLD) {
    return;
  }

  drawingContext.save();
  drawingContext.globalAlpha = alpha;
  drawingContext.font = `700 9px ${FONT_MONO}`;

  const labelWidth = drawingContext.measureText(SIMULATOR_BADGE_LABEL).width;
  const contentWidth = SIMULATOR_INDICATOR_SIZE_PX + SIMULATOR_INDICATOR_GAP_PX + labelWidth;
  const badgeWidth = contentWidth + SIMULATOR_BADGE_PADDING_PX * 2;
  const badgeLeftX = centerX - badgeWidth / 2;
  const badgeTopY = centerY - SIMULATOR_BADGE_HEIGHT_PX / 2;

  drawingContext.fillStyle = palette.surface;
  drawingContext.fillRect(badgeLeftX, badgeTopY, badgeWidth, SIMULATOR_BADGE_HEIGHT_PX);

  drawingContext.strokeStyle = palette.hairlineStrong;
  drawingContext.lineWidth = 1;
  drawingContext.strokeRect(
    badgeLeftX + 0.5,
    badgeTopY + 0.5,
    badgeWidth - 1,
    SIMULATOR_BADGE_HEIGHT_PX - 1,
  );

  const indicatorX = badgeLeftX + SIMULATOR_BADGE_PADDING_PX;
  const indicatorY = centerY - SIMULATOR_INDICATOR_SIZE_PX / 2;
  drawingContext.fillStyle = palette.signalHighlight;
  drawingContext.fillRect(
    indicatorX,
    indicatorY,
    SIMULATOR_INDICATOR_SIZE_PX,
    SIMULATOR_INDICATOR_SIZE_PX,
  );

  drawingContext.fillStyle = palette.inkSecondary;
  drawingContext.textAlign = "left";
  drawingContext.textBaseline = "middle";
  drawingContext.fillText(
    SIMULATOR_BADGE_LABEL,
    indicatorX + SIMULATOR_INDICATOR_SIZE_PX + SIMULATOR_INDICATOR_GAP_PX,
    centerY,
  );

  drawingContext.restore();
}
