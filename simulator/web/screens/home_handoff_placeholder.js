/**
 * Step 1 Home Handoff placeholder screen receiving the centered DialOS emblem
 * after Boot Act 04 completes and displaying live library counts + Reboot plate.
 */

import {
  DISPLAY_CENTER_PX,
  FULL_CIRCLE_RADIANS,
  OUTER_TICK_RADIUS_PX,
  REGISTRATION_MARK_RADIUS_PX,
  clamp,
  easeOutCubic,
  easeOutExpo,
  formatIntegerCount,
  lerp,
  normalizeRange,
} from "../core/geometry.js";
import { drawDiagonalMark, drawDialOsEmblem } from "../core/primitives.js";
import { FONT_DISPLAY, FONT_MONO } from "../core/tokens.js";

const EMBLEM_START_RADIUS_PX = 38;
const EMBLEM_END_RADIUS_PX = 26;
const EMBLEM_END_Y = 122;

const REBOOT_PLATE_CENTER_Y = 264;
const REBOOT_PLATE_WIDTH_PX = 136;
const REBOOT_PLATE_HEIGHT_PX = 40;
const REBOOT_PLATE_CHAMFER_PX = 8;

const REBOOT_HIT_TOP_Y = 244;
const REBOOT_HIT_BOTTOM_Y = 284;
const REBOOT_HIT_HALF_WIDTH_PX = 68;

/**
 * Trace a chamfered polygon plate (45-degree corner cuts) at (leftX, topY).
 *
 * @param {CanvasRenderingContext2D} drawingContext Canvas 2D context.
 * @param {number} leftX Left X coordinate.
 * @param {number} topY Top Y coordinate.
 * @param {number} width Plate width.
 * @param {number} height Plate height.
 * @param {number} chamfer Corner cut size in pixels.
 */
function traceChamferPlate(drawingContext, leftX, topY, width, height, chamfer) {
  const cut = Math.min(chamfer, width * 0.4, height * 0.4);
  const rightX = leftX + width;
  const bottomY = topY + height;

  drawingContext.beginPath();
  drawingContext.moveTo(leftX + cut, topY);
  drawingContext.lineTo(rightX - cut, topY);
  drawingContext.lineTo(rightX, topY + cut);
  drawingContext.lineTo(rightX, bottomY - cut);
  drawingContext.lineTo(rightX - cut, bottomY);
  drawingContext.lineTo(leftX + cut, bottomY);
  drawingContext.lineTo(leftX, bottomY - cut);
  drawingContext.lineTo(leftX, topY + cut);
  drawingContext.closePath();
}

/**
 * Render the Step 1 Home Handoff placeholder screen.
 *
 * @param {CanvasRenderingContext2D} drawingContext Canvas 2D context.
 * @param {object} routerState Snapshot from createRouter().getState().
 * @param {object} bootSnapshot Render snapshot from createBootController().
 * @param {object} palette Semantic Dark/Light appearance palette.
 */
export function renderHomeHandoffPlaceholder(
  drawingContext,
  routerState,
  bootSnapshot,
  palette,
) {
  const entryProgress = clamp(Number(routerState?.entryProgress ?? 1), 0, 1);
  const entryEase = easeOutExpo(entryProgress);
  const chromeAlpha = easeOutCubic(normalizeRange(entryProgress, 0.14, 0.90));

  /* 1. Subtle perimeter hairline (r = 160) and opposing registration marks (r = 136) */
  if (chromeAlpha > 0.01) {
    drawingContext.save();
    drawingContext.globalAlpha = chromeAlpha * 0.85;
    drawingContext.beginPath();
    drawingContext.arc(
      DISPLAY_CENTER_PX,
      DISPLAY_CENTER_PX,
      OUTER_TICK_RADIUS_PX,
      0,
      FULL_CIRCLE_RADIANS,
    );
    drawingContext.strokeStyle = palette.hairline;
    drawingContext.lineWidth = 1;
    drawingContext.stroke();
    drawingContext.restore();

    const opposingMarkAngles = [-Math.PI * 0.25, Math.PI * 0.75];
    for (let markIndex = 0; markIndex < opposingMarkAngles.length; markIndex += 1) {
      const angleRadians = opposingMarkAngles[markIndex];
      drawDiagonalMark(
        drawingContext,
        DISPLAY_CENTER_PX + Math.cos(angleRadians) * REGISTRATION_MARK_RADIUS_PX,
        DISPLAY_CENTER_PX + Math.sin(angleRadians) * REGISTRATION_MARK_RADIUS_PX,
        4.5,
        1.5,
        palette.inkSecondary,
        chromeAlpha * 0.85,
      );
    }
  }

  /* 2. Receive centered DialOS emblem from Boot Act 04 and glide smoothly to resting crown */
  const emblemY = lerp(DISPLAY_CENTER_PX, EMBLEM_END_Y, entryEase);
  const emblemRadius = lerp(EMBLEM_START_RADIUS_PX, EMBLEM_END_RADIUS_PX, entryEase);
  drawDialOsEmblem(drawingContext, DISPLAY_CENTER_PX, emblemY, emblemRadius, palette, 1, 1);

  if (chromeAlpha <= 0.01) {
    return;
  }

  /* 3. Home Ready header, Step 1 marker, live library counts, and chamfered Reboot plate */
  const counts = bootSnapshot?.backendCounts ?? { tracks: 0, albums: 0, artists: 0 };
  const statsLabel =
    `${formatIntegerCount(counts.tracks)} TRACKS \u00b7 ` +
    `${formatIntegerCount(counts.albums)} ALBUMS \u00b7 ` +
    `${formatIntegerCount(counts.artists)} ARTISTS`;

  const contentDriftY = lerp(8, 0, entryEase);

  drawingContext.save();
  drawingContext.globalAlpha = chromeAlpha;
  drawingContext.translate(0, contentDriftY);

  drawingContext.font = `700 20px ${FONT_DISPLAY}`;
  drawingContext.fillStyle = palette.inkPrimary;
  drawingContext.textAlign = "center";
  drawingContext.textBaseline = "middle";
  drawingContext.fillText("HOME READY", DISPLAY_CENTER_PX, 178);

  const stepLabel = "SIMULATOR \u00b7 STEP 1";
  drawingContext.font = `700 10px ${FONT_MONO}`;
  const stepLabelWidth = drawingContext.measureText(stepLabel).width;
  const indicatorSize = 6;
  const indicatorGap = 7;
  const stepOriginX = DISPLAY_CENTER_PX - (indicatorSize + indicatorGap + stepLabelWidth) / 2;

  drawingContext.fillStyle = palette.signalHighlight;
  drawingContext.fillRect(stepOriginX, 200 - indicatorSize / 2, indicatorSize, indicatorSize);

  drawingContext.fillStyle = palette.inkSecondary;
  drawingContext.textAlign = "left";
  drawingContext.fillText(stepLabel, stepOriginX + indicatorSize + indicatorGap, 200.5);

  drawingContext.font = `600 10px ${FONT_MONO}`;
  drawingContext.fillStyle = palette.inkSecondary;
  drawingContext.textAlign = "center";
  drawingContext.fillText(statsLabel, DISPLAY_CENTER_PX, 222);

  /* Chamfered REBOOT button plate centered at (180, 264) */
  const plateLeftX = DISPLAY_CENTER_PX - REBOOT_PLATE_WIDTH_PX / 2;
  const plateTopY = REBOOT_PLATE_CENTER_Y - REBOOT_PLATE_HEIGHT_PX / 2;

  traceChamferPlate(
    drawingContext,
    plateLeftX,
    plateTopY,
    REBOOT_PLATE_WIDTH_PX,
    REBOOT_PLATE_HEIGHT_PX,
    REBOOT_PLATE_CHAMFER_PX,
  );
  drawingContext.fillStyle = palette.surface;
  drawingContext.fill();
  drawingContext.strokeStyle = palette.hairlineStrong;
  drawingContext.lineWidth = 1;
  drawingContext.stroke();

  const rebootLabel = "REBOOT";
  drawingContext.font = `700 11px ${FONT_MONO}`;
  const rebootLabelWidth = drawingContext.measureText(rebootLabel).width;
  const accentSquareSize = 6;
  const accentGap = 8;
  const rebootOriginX = DISPLAY_CENTER_PX - (accentSquareSize + accentGap + rebootLabelWidth) / 2;

  drawingContext.fillStyle = palette.signalHighlight;
  drawingContext.fillRect(
    rebootOriginX,
    REBOOT_PLATE_CENTER_Y - accentSquareSize / 2,
    accentSquareSize,
    accentSquareSize,
  );

  drawingContext.fillStyle = palette.inkPrimary;
  drawingContext.textAlign = "left";
  drawingContext.fillText(
    rebootLabel,
    rebootOriginX + accentSquareSize + accentGap,
    REBOOT_PLATE_CENTER_Y + 0.5,
  );

  drawingContext.restore();
}

/**
 * Hit-test canvas coordinates against the Home Handoff placeholder controls.
 *
 * @param {number} pointX Display coordinate X in [0, 360].
 * @param {number} pointY Display coordinate Y in [0, 360].
 * @returns {"reboot" | null} Action identifier when tapped on REBOOT plate.
 */
export function hitTestHomeHandoffPlaceholder(pointX, pointY) {
  if (!Number.isFinite(pointX) || !Number.isFinite(pointY)) {
    return null;
  }
  if (
    pointY >= REBOOT_HIT_TOP_Y &&
    pointY <= REBOOT_HIT_BOTTOM_Y &&
    Math.abs(pointX - DISPLAY_CENTER_PX) <= REBOOT_HIT_HALF_WIDTH_PX
  ) {
    return "reboot";
  }
  return null;
}
