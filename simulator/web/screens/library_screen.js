/**
 * Pure Canvas 2D renderer and hit-tester for the DialOS 03 LIBRARY screen:
 * 2-up Crystal Hub with living sculptural icons, curved circular-chord lists,
 * non-deforming Vinyl Crate with real cover art + geometric motif fallbacks,
 * Crystal-Slab bezel Letter Seek, Playlist Action Sheet / Hold-to-Delete, and
 * seamless Vinyl Launch transition into Now Playing chrome.
 */

import {
  DISPLAY_CENTER_PX,
  DISPLAY_DIAMETER_PX,
  FULL_CIRCLE_RADIANS,
  TOP_ANGLE_RADIANS,
  clamp,
  easeInOutCubic,
  easeOutCubic,
  easeOutExpo,
  formatClockTime,
  formatIntegerCount,
  lerp,
  normalizeRange,
} from "../core/geometry.js";
import {
  ALBUM_SORT_MODE_LABELS,
  COLLECTION_SORT_MODE_LABELS,
  LIBRARY_CONSTANTS,
  LIBRARY_HUB_SECTIONS,
  buildAlbumSeekIndex,
  buildAlphabeticalLetterIndex,
  buildCollectionSeekIndex,
  computeLibraryCurvedRowLayout,
  computeLibraryHubTileRect,
  computeSortWheelSlots,
  computeWindowedCrateBars,
  resolveLibraryRimSliceIndex,
  shortestSliceDistance,
  sortAlbumsByMode,
  sortAlphabeticallyBy,
  sortArtistsByMode,
} from "../core/library_controller.js";
import {
  drawCrosshairMark,
  drawDiagonalMark,
  drawHazardStripeBand,
} from "../core/primitives.js";
import { APPEARANCE_PALETTES, FONT_DISPLAY, FONT_MONO } from "../core/tokens.js";

const ALPHA_VISIBILITY_THRESHOLD = 0.005;
const CONTENT_VISIBILITY_THRESHOLD = 0.01;
const PERCENT_SCALE = 100;
const MIN_ALPHABETICAL_INDEX_COUNT = 3;
const MIN_LETTER_GROUPS_FOR_SEEK = 2;

const HEADER_CLIP_TOP_Y = 76;
const RIM_SHEEN_RADIUS_PX = 175;
const SCROLL_ARC_RADIUS_PX = 166;
const SCROLL_ARC_START_RADIANS = -0.44;
const SCROLL_ARC_END_RADIANS = 0.44;
const BACK_SWIPE_ARC_RADIUS_PX = 168;
const BACK_SWIPE_ARC_SPAN_RADIANS = 0.34;
const TOP_HANDLE_HALF_WIDTH_PX = 14;
const TOP_HANDLE_HEIGHT_PX = 3;

const CRATE_BAR_LIMIT = 7;
const CRATE_BAR_HEIGHT_PX = 5;
const CRATE_BAR_GAP_PX = 6;
const CRATE_BAR_Y = 292;
const CRATE_MIN_SLEEVE_PX = 86;
const CRATE_MAX_SLEEVE_PX = 120;
const CRATE_MIN_OPACITY = 0.32;
const CRATE_DISC_PEEK_THRESHOLD = 0.42;

const SHEET_CARD_WIDTH_PX = 224;
const SHEET_CARD_HEIGHT_PX = 194;
const SHEET_BUTTON_LEFT_X = 82;
const SHEET_BUTTON_RIGHT_X = 278;
const SHEET_PLAY_TOP_Y = 146;
const SHEET_PLAY_BOTTOM_Y = 184;
const SHEET_SHUFFLE_TOP_Y = 188;
const SHEET_SHUFFLE_BOTTOM_Y = 226;
const SHEET_DELETE_TOP_Y = 232;
const SHEET_DELETE_BOTTOM_Y = 268;

const EMPTY_BACK_CTA_TOP_Y = 236;
const EMPTY_BACK_CTA_BOTTOM_Y = 284;
const EMPTY_BACK_CTA_HALF_WIDTH_PX = 88;

const CRATE_PEEK_LEFT_X = 224;
const CRATE_PEEK_RIGHT_X = 272;
const CRATE_PEEK_TOP_Y = 96;
const CRATE_PEEK_BOTTOM_Y = 212;
const CRATE_SLEEVE_LEFT_X = 108;
const CRATE_SLEEVE_TOP_Y = 90;
const CRATE_SLEEVE_BOTTOM_Y = 216;
const CRATE_SORT_BUTTON_CENTER_X = LIBRARY_CONSTANTS.CRATE_SORT_BUTTON_X;
const CRATE_NOTE_BUTTON_CENTER_X = LIBRARY_CONSTANTS.CRATE_NOTE_BUTTON_X;
const CRATE_NOTE_BUTTON_CENTER_Y = LIBRARY_CONSTANTS.CRATE_NOTE_BUTTON_Y;
const CRATE_NOTE_BUTTON_RADIUS_PX = LIBRARY_CONSTANTS.CRATE_BOTTOM_BUTTON_RADIUS_PX;
const CRATE_NOTE_BUTTON_HIT_RADIUS_PX = LIBRARY_CONSTANTS.CRATE_BOTTOM_BUTTON_HIT_RADIUS_PX;
const ALBUM_QUICK_ACTION_BUTTON_CENTER_Y = LIBRARY_CONSTANTS.ALBUM_QUICK_ACTION_BUTTON_CENTER_Y;
const ALBUM_QUICK_ACTION_BUTTON_RADIUS_PX = LIBRARY_CONSTANTS.ALBUM_QUICK_ACTION_BUTTON_RADIUS_PX;
const ALBUM_QUICK_ACTION_BUTTON_HIT_RADIUS_PX =
  LIBRARY_CONSTANTS.ALBUM_QUICK_ACTION_BUTTON_HIT_RADIUS_PX;
const CRYSTAL_OVERLAY_TOP_Y = HEADER_CLIP_TOP_Y - 2;
const CRATE_SLEEVE_START_X_PX = DISPLAY_CENTER_PX - 8;
const CRATE_SLEEVE_EXIT_X_PX = -110;
const CRATE_DISC_PEEK_X_PX = 219.2;
const CRATE_DISC_APEX_X_PX = 246;
const CRATE_DISC_START_RADIUS_PX = 52.8;
const TRACKLIST_DISC_START_RADIUS_PX = 36;
const CRATE_SLIP_OUT_SPLIT_PROGRESS = 0.36;
const CRATE_SLEEVE_SLIDE_END_PROGRESS = 0.72;
const CRATE_SLEEVE_FADE_START_PROGRESS = 0.38;

const HUB_ALBUMS_TILE_INDEX = 3;
const HUB_TILE_ICON_OFFSET_Y_PX = 14;
const HUB_TILE_FULL_ICON_RADIUS_PX = 19;
const HUB_TILE_PEEK_ICON_RADIUS_PX = 15;
const HUB_ALBUMS_ICON_SLEEVE_RATIO = 0.70;
const HUB_ALBUMS_ICON_SHIFT_X_RATIO = 0.24;
const HUB_ALBUMS_ICON_DISC_OFFSET_RATIO = 0.88;

const HUB_DIVE_FLOOR_START = 0.24;
const HUB_DIVE_FLOOR_END = 0.68;
const HUB_DIVE_HEADER_START = 0.42;
const HUB_DIVE_HEADER_END = 0.96;
const HUB_DIVE_HEADER_DRIFT_Y_PX = -18;
const HUB_DIVE_TOP_ROW_START = 0.30;
const HUB_DIVE_TOP_ROW_END = 0.88;
const HUB_DIVE_PEEK_ROW_START = 0.42;
const HUB_DIVE_PEEK_ROW_END = 1.0;
const HUB_DIVE_TILE_MIN_SCALE = 0.80;
const HUB_DIVE_TILE_RISE_Y_PX = 18;
const HUB_DIVE_ARC_START = 0.65;
const HUB_DIVE_ARC_END = 1.0;

const ALBUMS_BLOOM_HUB_HEADER_END = 0.42;
const ALBUMS_BLOOM_CRATE_HEADER_START = 0.44;
const ALBUMS_BLOOM_CRATE_HEADER_END = 0.92;
const ALBUMS_BLOOM_HEADER_DRIFT_Y_PX = 10;
const ALBUMS_BLOOM_HUB_PART_END = 0.44;
const ALBUMS_BLOOM_HUB_PART_X_PX = 95;
const ALBUMS_BLOOM_HUB_PART_Y_PX = 24;
const ALBUMS_BLOOM_TILE_SHELL_END = 0.30;
const ALBUMS_BLOOM_ARC_EXIT_END = 0.28;
const ALBUMS_BLOOM_SHELF_START = 0.36;
const ALBUMS_BLOOM_SHELF_END = 0.85;
const ALBUMS_BLOOM_NEIGHBOR_DEAL_START = 0.34;
const ALBUMS_BLOOM_NEIGHBOR_DEAL_END = 0.96;
const ALBUMS_BLOOM_NEIGHBOR_ALPHA_START = 0.36;
const ALBUMS_BLOOM_NEIGHBOR_ALPHA_END = 0.92;
const ALBUMS_BLOOM_NEIGHBOR_SPREAD_PX = 120;
const ALBUMS_BLOOM_BOTTOM_START = 0.42;
const ALBUMS_BLOOM_BOTTOM_END = 0.96;
const ALBUMS_BLOOM_BOTTOM_RISE_Y_PX = 22;
const ALBUMS_BLOOM_ARTWORK_FADE_START = 0.06;
const ALBUMS_BLOOM_ARTWORK_FADE_END = 0.34;
const ALBUMS_BLOOM_MINI_ICON_FADE_START = 0.08;
const ALBUMS_BLOOM_MINI_ICON_FADE_END = 0.34;
const ALBUMS_BLOOM_DISC_TUCK_END = 0.38;
const ALBUMS_BLOOM_HERO_ALPHA_END = 0.32;
const ALBUMS_BLOOM_BRACKET_START = 0.55;

function resolvePalette(appearance, palette) {
  if (palette && typeof palette === "object") {
    return palette;
  }
  return APPEARANCE_PALETTES[appearance === "light" ? "light" : "dark"];
}

function resolveAppearance(appearance) {
  return appearance === "light" ? "light" : "dark";
}

/**
 * Truncate text cleanly with an ellipsis when its measured canvas width exceeds `maxWidthPx`.
 *
 * @param {CanvasRenderingContext2D} drawingContext Canvas 2D context.
 * @param {string} labelText Source text string.
 * @param {number} maxWidthPx Maximum line width in pixels.
 * @returns {string} Fitted text string.
 */
export function truncateCanvasText(drawingContext, labelText, maxWidthPx) {
  const safeLabel = String(labelText ?? "");
  if (drawingContext.measureText(safeLabel).width <= maxWidthPx) {
    return safeLabel;
  }
  let clipped = safeLabel;
  while (
    clipped.length > 2 &&
    drawingContext.measureText(`${clipped}\u2026`).width > maxWidthPx
  ) {
    clipped = clipped.slice(0, -1);
  }
  return `${clipped.trimEnd()}\u2026`;
}

function parseCanvasFontSizePx(fontSpec) {
  const sizeMatch = /(\d+(?:\.\d+)?)px/.exec(String(fontSpec || ""));
  return sizeMatch ? Number(sizeMatch[1]) : 12;
}

/**
 * Draw text statically when fitting inside `maxWidthPx`, or clip and smoothly marquee
 * with a start pause, end pause, whitespace gap, and continuous loop when overflowing.
 *
 * @param {CanvasRenderingContext2D} drawingContext Canvas 2D context.
 * @param {string} labelText Full label text.
 * @param {number} anchorX Text alignment anchor X coordinate.
 * @param {number} anchorY Text baseline Y coordinate.
 * @param {number} maxWidthPx Maximum available horizontal width in pixels.
 * @param {number|object} [timeOrOptions=0] Animation clock in seconds or options object.
 * @param {object} [extraOptions={}] Optional marquee timing configuration.
 */
export function drawMarqueeCanvasText(
  drawingContext,
  labelText,
  anchorX,
  anchorY,
  maxWidthPx,
  timeOrOptions = 0,
  extraOptions = {},
) {
  if (!drawingContext) {
    return;
  }
  const safeLabel = String(labelText ?? "");
  if (!safeLabel) {
    return;
  }

  const measuredWidth = drawingContext.measureText(safeLabel).width;
  if (!Number.isFinite(maxWidthPx) || maxWidthPx <= 0 || measuredWidth <= maxWidthPx) {
    drawingContext.fillText(safeLabel, anchorX, anchorY);
    return;
  }

  const optionsObject =
    timeOrOptions && typeof timeOrOptions === "object"
      ? timeOrOptions
      : extraOptions && typeof extraOptions === "object"
        ? extraOptions
        : {};
  const animationTimeSeconds =
    typeof timeOrOptions === "number" && Number.isFinite(timeOrOptions)
      ? Math.max(0, timeOrOptions)
      : Number.isFinite(optionsObject.animationTimeSeconds)
        ? Math.max(0, optionsObject.animationTimeSeconds)
        : 0;

  const fontSizePx = parseCanvasFontSizePx(drawingContext.font);
  const startPauseSeconds = Number.isFinite(optionsObject.startPauseSeconds)
    ? optionsObject.startPauseSeconds
    : 0.9;
  const endPauseSeconds = Number.isFinite(optionsObject.endPauseSeconds)
    ? optionsObject.endPauseSeconds
    : 0.8;
  const gapWidthPx = Number.isFinite(optionsObject.gapWidthPx) ? optionsObject.gapWidthPx : 36;
  const scrollSpeedPxPerSecond = Number.isFinite(optionsObject.scrollSpeedPxPerSecond)
    ? Math.max(8, optionsObject.scrollSpeedPxPerSecond)
    : 28;
  const clipHeightPx = Number.isFinite(optionsObject.clipHeightPx)
    ? optionsObject.clipHeightPx
    : Math.max(20, fontSizePx * 1.7);

  const overflowDistance = measuredWidth - maxWidthPx;
  const scrollToEndSeconds = overflowDistance / scrollSpeedPxPerSecond;
  const gapScrollDistance = maxWidthPx + gapWidthPx;
  const scrollThroughGapSeconds = gapScrollDistance / scrollSpeedPxPerSecond;
  const cycleDurationSeconds =
    startPauseSeconds + scrollToEndSeconds + endPauseSeconds + scrollThroughGapSeconds;
  const cycleTimeSeconds =
    cycleDurationSeconds > 0 ? animationTimeSeconds % cycleDurationSeconds : 0;

  let horizontalOffsetPx = 0;
  if (cycleTimeSeconds <= startPauseSeconds) {
    horizontalOffsetPx = 0;
  } else if (cycleTimeSeconds <= startPauseSeconds + scrollToEndSeconds) {
    horizontalOffsetPx = (cycleTimeSeconds - startPauseSeconds) * scrollSpeedPxPerSecond;
  } else if (cycleTimeSeconds <= startPauseSeconds + scrollToEndSeconds + endPauseSeconds) {
    horizontalOffsetPx = overflowDistance;
  } else {
    horizontalOffsetPx =
      overflowDistance +
      (cycleTimeSeconds - startPauseSeconds - scrollToEndSeconds - endPauseSeconds) *
        scrollSpeedPxPerSecond;
  }

  const activeAlign = drawingContext.textAlign;
  const boxLeftX =
    activeAlign === "center"
      ? anchorX - maxWidthPx / 2
      : activeAlign === "right" || activeAlign === "end"
        ? anchorX - maxWidthPx
        : anchorX;

  const activeBaseline = drawingContext.textBaseline;
  const clipTopY =
    activeBaseline === "top"
      ? anchorY - 2
      : activeBaseline === "bottom"
        ? anchorY - clipHeightPx + 2
        : anchorY - clipHeightPx / 2;

  drawingContext.save();
  drawingContext.beginPath();
  drawingContext.rect(boxLeftX, clipTopY, maxWidthPx, clipHeightPx);
  drawingContext.clip();
  drawingContext.textAlign = "left";

  const primaryLeftX = boxLeftX - horizontalOffsetPx;
  if (primaryLeftX + measuredWidth >= boxLeftX - 4) {
    drawingContext.fillText(safeLabel, primaryLeftX, anchorY);
  }
  const trailingLeftX = primaryLeftX + measuredWidth + gapWidthPx;
  if (trailingLeftX <= boxLeftX + maxWidthPx + 4) {
    drawingContext.fillText(safeLabel, trailingLeftX, anchorY);
  }

  drawingContext.restore();
}

/**
 * Trace an octagonal chamfered rectangle path onto `drawingContext`.
 *
 * @param {CanvasRenderingContext2D} drawingContext Canvas 2D context.
 * @param {number} leftX Top-left X coordinate.
 * @param {number} topY Top-left Y coordinate.
 * @param {number} width Rectangle width in pixels.
 * @param {number} height Rectangle height in pixels.
 * @param {number} chamfer Corner chamfer size in pixels.
 */
export function traceChamferRect(drawingContext, leftX, topY, width, height, chamfer) {
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
 * Draw opposing top-left and bottom-right L-shaped registration brackets around a rectangle.
 *
 * @param {CanvasRenderingContext2D} drawingContext Canvas 2D context.
 * @param {number} leftX Bounding box left X.
 * @param {number} topY Bounding box top Y.
 * @param {number} width Bounding box width.
 * @param {number} height Bounding box height.
 * @param {number} armLength Bracket leg length in pixels.
 * @param {number} lineWidth Stroke width in pixels.
 * @param {string} color Stroke color.
 * @param {number} alpha Layer opacity.
 */
export function drawLibraryCornerBrackets(
  drawingContext,
  leftX,
  topY,
  width,
  height,
  armLength,
  lineWidth,
  color,
  alpha,
) {
  if (alpha <= CONTENT_VISIBILITY_THRESHOLD) {
    return;
  }
  const rightX = leftX + width;
  const bottomY = topY + height;
  drawingContext.save();
  drawingContext.globalAlpha = alpha;
  drawingContext.strokeStyle = color;
  drawingContext.lineWidth = lineWidth;
  drawingContext.lineCap = "butt";
  drawingContext.beginPath();
  drawingContext.moveTo(leftX, topY + armLength);
  drawingContext.lineTo(leftX, topY);
  drawingContext.lineTo(leftX + armLength, topY);
  drawingContext.moveTo(rightX - armLength, bottomY);
  drawingContext.lineTo(rightX, bottomY);
  drawingContext.lineTo(rightX, bottomY - armLength);
  drawingContext.stroke();
  drawingContext.restore();
}

function traceCurvedSlabWedge(drawingContext, innerRadius, outerRadius, startAngle, endAngle) {
  drawingContext.beginPath();
  drawingContext.arc(DISPLAY_CENTER_PX, DISPLAY_CENTER_PX, outerRadius, startAngle, endAngle, false);
  drawingContext.arc(DISPLAY_CENTER_PX, DISPLAY_CENTER_PX, innerRadius, endAngle, startAngle, true);
  drawingContext.closePath();
}

/**
 * Draw a living sculptural 2D icon corresponding to a Library hub section or row type.
 *
 * @param {CanvasRenderingContext2D} drawingContext Canvas 2D context.
 * @param {string} iconId Canonical section or row identifier.
 * @param {number} centerX Center X coordinate.
 * @param {number} centerY Center Y coordinate.
 * @param {number} radius Icon nominal radius in pixels.
 * @param {number} clockSeconds Idle animation clock in seconds.
 * @param {boolean} isEmphasized True while pressed or playing.
 * @param {object} [palette] Semantic Dark/Light appearance palette.
 * @param {string} [appearance="dark"] Semantic appearance ("dark" | "light").
 */
export function drawLibraryLivingIcon(
  drawingContext,
  iconId,
  centerX,
  centerY,
  radius,
  clockSeconds,
  isEmphasized,
  palette,
  appearance = "dark",
) {
  if (!drawingContext || radius <= 0) {
    return;
  }
  const resolvedAppearance = resolveAppearance(appearance);
  const resolvedPalette = resolvePalette(resolvedAppearance, palette);
  const safeClock = Number.isFinite(clockSeconds) ? clockSeconds : 0;
  const inkColor = isEmphasized ? resolvedPalette.signalHighlight : resolvedPalette.inkPrimary;
  const accentColor = resolvedPalette.signalHighlight;
  const mutedColor = resolvedPalette.hairlineStrong;
  const isDark = resolvedAppearance === "dark";

  drawingContext.save();
  drawingContext.translate(centerX, centerY);
  drawingContext.lineCap = "butt";
  drawingContext.lineJoin = "miter";

  if (iconId === "favorites") {
    const spinWidth = 0.68 + 0.32 * Math.abs(Math.cos(safeClock * 1.4));
    drawingContext.scale(spinWidth, 1);
    drawingContext.beginPath();
    drawingContext.moveTo(0, radius * 0.78);
    drawingContext.lineTo(-radius * 0.86, -radius * 0.08);
    drawingContext.lineTo(-radius * 0.82, -radius * 0.58);
    drawingContext.lineTo(-radius * 0.34, -radius * 0.80);
    drawingContext.lineTo(0, -radius * 0.40);
    drawingContext.lineTo(radius * 0.34, -radius * 0.80);
    drawingContext.lineTo(radius * 0.82, -radius * 0.58);
    drawingContext.lineTo(radius * 0.86, -radius * 0.08);
    drawingContext.closePath();
    drawingContext.fillStyle = isEmphasized
      ? accentColor
      : isDark
        ? "rgba(245, 246, 248, 0.16)"
        : "rgba(46, 50, 56, 0.14)";
    drawingContext.fill();
    drawingContext.strokeStyle = inkColor;
    drawingContext.lineWidth = 1.6;
    drawingContext.stroke();

    drawingContext.beginPath();
    drawingContext.moveTo(0, -radius * 0.40);
    drawingContext.lineTo(0, radius * 0.72);
    drawingContext.strokeStyle = isEmphasized ? resolvedPalette.signalHighlightInk : accentColor;
    drawingContext.lineWidth = 1.3;
    drawingContext.stroke();
  } else if (iconId === "playlists") {
    const plateTiers = [
      { baseY: radius * 0.46, widthRadius: radius * 0.72, phase: 0.0, isHot: false },
      { baseY: 0, widthRadius: radius * 0.84, phase: 2.0, isHot: true },
      { baseY: -radius * 0.46, widthRadius: radius * 0.68, phase: 4.0, isHot: false },
    ];
    for (let tierIndex = 0; tierIndex < plateTiers.length; tierIndex += 1) {
      const tier = plateTiers[tierIndex];
      const driftY = tier.baseY + Math.sin(safeClock * 2.2 + tier.phase) * 1.6;
      drawingContext.beginPath();
      drawingContext.ellipse(0, driftY, tier.widthRadius, radius * 0.24, 0, 0, FULL_CIRCLE_RADIANS);
      drawingContext.fillStyle =
        tier.isHot && isEmphasized
          ? accentColor
          : isDark
            ? "rgba(245, 246, 248, 0.12)"
            : "rgba(46, 50, 56, 0.12)";
      drawingContext.fill();
      drawingContext.strokeStyle = tier.isHot
        ? isEmphasized
          ? accentColor
          : inkColor
        : mutedColor;
      drawingContext.lineWidth = tier.isHot ? 1.6 : 1.15;
      drawingContext.stroke();
    }
  } else if (iconId === "artists") {
    const swayX = Math.sin(safeClock * 1.5) * 1.4;
    drawingContext.beginPath();
    drawingContext.arc(swayX, -radius * 0.34, radius * 0.33, 0, FULL_CIRCLE_RADIANS);
    drawingContext.fillStyle = isEmphasized
      ? accentColor
      : isDark
        ? "rgba(245, 246, 248, 0.16)"
        : "rgba(46, 50, 56, 0.14)";
    drawingContext.fill();
    drawingContext.strokeStyle = inkColor;
    drawingContext.lineWidth = 1.5;
    drawingContext.stroke();

    drawingContext.beginPath();
    drawingContext.moveTo(-radius * 0.74, radius * 0.72);
    drawingContext.lineTo(-radius * 0.56, radius * 0.20);
    drawingContext.lineTo(radius * 0.56, radius * 0.20);
    drawingContext.lineTo(radius * 0.74, radius * 0.72);
    drawingContext.strokeStyle = inkColor;
    drawingContext.lineWidth = 1.65;
    drawingContext.stroke();
  } else if (iconId === "albums") {
    const spin = safeClock * 1.25;
    const sleeveHalf = radius * 0.70;
    const sleeveCenterX = -radius * 0.24;
    const discPeekShift = Math.sin(safeClock * 1.8) * radius * 0.05;
    const discCenterX = sleeveCenterX + sleeveHalf * 0.88 + discPeekShift;
    const discRadius = sleeveHalf * 0.88;

    drawingContext.save();
    drawingContext.beginPath();
    drawingContext.arc(discCenterX, 0, discRadius, 0, FULL_CIRCLE_RADIANS);
    drawingContext.fillStyle = isDark ? "#13171F" : "#242830";
    drawingContext.fill();
    drawingContext.strokeStyle = isDark
      ? "rgba(245, 246, 248, 0.34)"
      : "rgba(46, 50, 56, 0.55)";
    drawingContext.lineWidth = 1.1;
    drawingContext.stroke();

    drawingContext.beginPath();
    drawingContext.arc(discCenterX, 0, discRadius * 0.66, spin, spin + Math.PI * 0.75);
    drawingContext.strokeStyle = isDark
      ? "rgba(245, 246, 248, 0.36)"
      : "rgba(245, 246, 248, 0.48)";
    drawingContext.lineWidth = 1.1;
    drawingContext.stroke();

    drawingContext.beginPath();
    drawingContext.arc(discCenterX, 0, discRadius * 0.30, 0, FULL_CIRCLE_RADIANS);
    drawingContext.fillStyle = accentColor;
    drawingContext.fill();
    drawingContext.beginPath();
    drawingContext.arc(discCenterX, 0, discRadius * 0.11, 0, FULL_CIRCLE_RADIANS);
    drawingContext.fillStyle = "#060709";
    drawingContext.fill();

    drawingContext.beginPath();
    drawingContext.arc(discCenterX, 0, discRadius + 0.8, -Math.PI * 0.42, Math.PI * 0.42);
    drawingContext.strokeStyle = accentColor;
    drawingContext.lineWidth = 1.4;
    drawingContext.stroke();
    drawingContext.restore();

    const sleeveLeft = sleeveCenterX - sleeveHalf;
    const sleeveTop = -sleeveHalf;
    const sleeveSize = sleeveHalf * 2;

    drawingContext.save();
    drawingContext.beginPath();
    drawingContext.rect(sleeveLeft, sleeveTop, sleeveSize, sleeveSize);
    drawingContext.clip();

    drawingContext.fillStyle = isDark ? "#141923" : "#272C35";
    drawingContext.fillRect(sleeveLeft, sleeveTop, sleeveSize, sleeveSize);

    drawingContext.fillStyle = accentColor;
    drawingContext.beginPath();
    drawingContext.moveTo(sleeveCenterX - sleeveHalf * 0.14, sleeveTop);
    drawingContext.lineTo(sleeveCenterX + sleeveHalf * 0.56, sleeveTop);
    drawingContext.lineTo(sleeveCenterX - sleeveHalf * 0.24, sleeveTop + sleeveSize);
    drawingContext.lineTo(sleeveCenterX - sleeveHalf * 0.68, sleeveTop + sleeveSize);
    drawingContext.closePath();
    drawingContext.fill();

    drawingContext.strokeStyle = "#F5F6F8";
    drawingContext.lineWidth = Math.max(1.5, sleeveHalf * 0.18);
    drawingContext.beginPath();
    drawingContext.arc(sleeveCenterX, 0, sleeveHalf * 0.54, -Math.PI * 0.22, Math.PI * 0.50);
    drawingContext.stroke();

    drawingContext.fillStyle = "rgba(255, 255, 255, 0.16)";
    drawingContext.beginPath();
    drawingContext.moveTo(sleeveLeft, sleeveTop);
    drawingContext.lineTo(sleeveLeft + sleeveSize * 0.45, sleeveTop);
    drawingContext.lineTo(sleeveLeft + sleeveSize * 0.08, sleeveTop + sleeveSize);
    drawingContext.lineTo(sleeveLeft - sleeveSize * 0.35, sleeveTop + sleeveSize);
    drawingContext.closePath();
    drawingContext.fill();
    drawingContext.restore();

    drawingContext.strokeStyle = isEmphasized ? accentColor : inkColor;
    drawingContext.lineWidth = isEmphasized ? 1.5 : 1.25;
    drawingContext.strokeRect(sleeveLeft + 0.5, sleeveTop + 0.5, sleeveSize - 1, sleeveSize - 1);

    const cornerArm = Math.max(4, sleeveHalf * 0.36);
    drawingContext.strokeStyle = accentColor;
    drawingContext.lineWidth = 1.7;
    drawingContext.beginPath();
    drawingContext.moveTo(sleeveLeft, sleeveTop + cornerArm);
    drawingContext.lineTo(sleeveLeft, sleeveTop);
    drawingContext.lineTo(sleeveLeft + cornerArm, sleeveTop);
    drawingContext.stroke();
  } else if (iconId === "genres") {
    const barCount = 4;
    const barWidth = radius * 0.26;
    const barGap = radius * 0.16;
    const totalWidth = barCount * barWidth + (barCount - 1) * barGap;
    for (let barIndex = 0; barIndex < barCount; barIndex += 1) {
      const level =
        0.28 +
        0.72 * (0.5 + 0.5 * Math.sin(safeClock * (3.2 + barIndex * 0.9) + barIndex * 1.4));
      const barHeight = radius * (0.45 + level * 0.95);
      const barX = -totalWidth / 2 + barIndex * (barWidth + barGap);
      drawingContext.fillStyle =
        barIndex === 1 && isEmphasized
          ? accentColor
          : barIndex % 2 === 0
            ? inkColor
            : mutedColor;
      drawingContext.fillRect(barX, radius * 0.68 - barHeight, barWidth, barHeight);
    }
  } else if (iconId === "track") {
    const spin = safeClock * (isEmphasized ? 2.4 : 0.9);
    drawingContext.beginPath();
    drawingContext.arc(0, 0, radius * 0.78, 0, FULL_CIRCLE_RADIANS);
    drawingContext.strokeStyle = inkColor;
    drawingContext.lineWidth = 1.4;
    drawingContext.stroke();

    drawingContext.beginPath();
    drawingContext.arc(0, 0, radius * 0.44, spin, spin + Math.PI * 0.65);
    drawingContext.strokeStyle = isEmphasized ? accentColor : mutedColor;
    drawingContext.lineWidth = 1.8;
    drawingContext.stroke();

    drawingContext.beginPath();
    drawingContext.arc(0, 0, radius * 0.16, 0, FULL_CIRCLE_RADIANS);
    drawingContext.fillStyle = inkColor;
    drawingContext.fill();
  } else {
    const orbit = safeClock * 1.2;
    drawingContext.beginPath();
    drawingContext.arc(0, 0, radius * 0.78, orbit, orbit + Math.PI * 1.45);
    drawingContext.strokeStyle = inkColor;
    drawingContext.lineWidth = 1.7;
    drawingContext.stroke();

    drawingContext.beginPath();
    drawingContext.arc(0, 0, radius * 0.44, -orbit * 1.3, -orbit * 1.3 + Math.PI * 1.1);
    drawingContext.strokeStyle = isEmphasized ? accentColor : mutedColor;
    drawingContext.lineWidth = 1.4;
    drawingContext.stroke();

    const dotX = Math.cos(orbit + Math.PI * 1.65) * radius * 0.78;
    const dotY = Math.sin(orbit + Math.PI * 1.65) * radius * 0.78;
    drawingContext.fillStyle = accentColor;
    drawingContext.fillRect(dotX - 2.2, dotY - 2.2, 4.4, 4.4);
  }

  drawingContext.restore();
}

/**
 * Draw real center-cropped album cover art when cached and loaded, or fall back cleanly
 * to one of the six DialOS pure-2D geometric artwork motifs (`prism` by default).
 *
 * @param {CanvasRenderingContext2D} drawingContext Canvas 2D context centered on artwork origin.
 * @param {object | null | undefined} albumSpec Album descriptor with optional `coverUrl` and `motif`.
 * @param {number} spanHalf Half side length of the square artwork region.
 * @param {object} [palette] Semantic Dark/Light appearance palette.
 * @param {string} [appearance="dark"] Semantic appearance ("dark" | "light").
 * @param {{ getOrLoadAlbumCover?: (albumSpec: object) => object | null } | null} [coverCache] LRU cover cache.
 */
export function drawLibraryAlbumArtworkMotif(
  drawingContext,
  albumSpec,
  spanHalf,
  palette,
  appearance = "dark",
  coverCache = null,
) {
  if (!drawingContext || spanHalf <= 0) {
    return;
  }
  const resolvedAppearance = resolveAppearance(appearance);
  const resolvedPalette = resolvePalette(resolvedAppearance, palette);
  const isDark = resolvedAppearance === "dark";
  const baseInk = isDark ? "#11151C" : "#22262E";
  const slateSurface = isDark ? "#1D2330" : "#343A46";
  const starkLight = "#F5F6F8";
  const accentColor = resolvedPalette.signalHighlight;

  drawingContext.fillStyle = baseInk;
  drawingContext.fillRect(-spanHalf, -spanHalf, spanHalf * 2, spanHalf * 2);

  const coverEntry =
    coverCache && typeof coverCache.getOrLoadAlbumCover === "function"
      ? coverCache.getOrLoadAlbumCover(albumSpec)
      : null;
  if (
    coverEntry &&
    coverEntry.status === "loaded" &&
    coverEntry.image &&
    coverEntry.image.naturalWidth > 0 &&
    coverEntry.image.naturalHeight > 0
  ) {
    const imageWidth = coverEntry.image.naturalWidth;
    const imageHeight = coverEntry.image.naturalHeight;
    const cropSide = Math.min(imageWidth, imageHeight);
    const cropX = (imageWidth - cropSide) / 2;
    const cropY = (imageHeight - cropSide) / 2;
    drawingContext.drawImage(
      coverEntry.image,
      cropX,
      cropY,
      cropSide,
      cropSide,
      -spanHalf,
      -spanHalf,
      spanHalf * 2,
      spanHalf * 2,
    );
    return;
  }

  const motif =
    typeof albumSpec?.motif === "string" && albumSpec.motif.length > 0
      ? albumSpec.motif
      : "prism";

  if (motif === "prism") {
    drawingContext.fillStyle = slateSurface;
    drawingContext.beginPath();
    drawingContext.moveTo(-spanHalf, -spanHalf * 0.18);
    drawingContext.lineTo(spanHalf * 0.35, -spanHalf);
    drawingContext.lineTo(spanHalf, -spanHalf);
    drawingContext.lineTo(spanHalf, spanHalf * 0.25);
    drawingContext.lineTo(-spanHalf * 0.42, spanHalf);
    drawingContext.lineTo(-spanHalf, spanHalf);
    drawingContext.closePath();
    drawingContext.fill();

    drawingContext.fillStyle = accentColor;
    drawingContext.beginPath();
    drawingContext.moveTo(-spanHalf * 0.14, -spanHalf);
    drawingContext.lineTo(spanHalf * 0.56, -spanHalf);
    drawingContext.lineTo(-spanHalf * 0.24, spanHalf);
    drawingContext.lineTo(-spanHalf * 0.68, spanHalf);
    drawingContext.closePath();
    drawingContext.fill();

    drawingContext.strokeStyle = starkLight;
    drawingContext.lineWidth = Math.max(3, spanHalf * 0.14);
    drawingContext.lineCap = "butt";
    drawingContext.beginPath();
    drawingContext.arc(0, 0, spanHalf * 0.62, -Math.PI * 0.22, Math.PI * 0.48);
    drawingContext.stroke();

    drawingContext.fillStyle = starkLight;
    const barHeight = Math.max(2, spanHalf * 0.05);
    const barPitch = Math.max(4, spanHalf * 0.105);
    for (let barIndex = 0; barIndex < 4; barIndex += 1) {
      drawingContext.fillRect(
        -spanHalf * 0.78,
        -spanHalf * 0.58 + barIndex * barPitch,
        spanHalf * (0.48 - barIndex * 0.08),
        barHeight,
      );
    }
  } else if (motif === "grid") {
    drawingContext.fillStyle = "#0F1D2B";
    drawingContext.fillRect(-spanHalf, -spanHalf, spanHalf * 2, spanHalf * 2);

    drawingContext.strokeStyle = "rgba(94, 220, 230, 0.36)";
    drawingContext.lineWidth = 1;
    const gridStep = spanHalf * 0.34;
    for (let gridOffset = -spanHalf * 1.4; gridOffset <= spanHalf * 1.4; gridOffset += gridStep) {
      drawingContext.beginPath();
      drawingContext.moveTo(gridOffset, -spanHalf);
      drawingContext.lineTo(gridOffset + spanHalf * 0.6, spanHalf);
      drawingContext.moveTo(-spanHalf, gridOffset);
      drawingContext.lineTo(spanHalf, gridOffset - spanHalf * 0.35);
      drawingContext.stroke();
    }

    drawingContext.fillStyle = "#5EDCE6";
    drawingContext.beginPath();
    drawingContext.moveTo(-spanHalf * 0.48, -spanHalf * 0.12);
    drawingContext.lineTo(spanHalf * 0.16, -spanHalf * 0.52);
    drawingContext.lineTo(spanHalf * 0.58, -spanHalf * 0.14);
    drawingContext.lineTo(-spanHalf * 0.06, spanHalf * 0.26);
    drawingContext.closePath();
    drawingContext.fill();

    drawingContext.fillStyle = starkLight;
    drawingContext.fillRect(-spanHalf * 0.56, spanHalf * 0.34, spanHalf * 1.12, spanHalf * 0.14);
    drawCrosshairMark(
      drawingContext,
      -spanHalf * 0.52,
      -spanHalf * 0.54,
      spanHalf * 0.12,
      1.4,
      starkLight,
      0.9,
    );
    drawCrosshairMark(
      drawingContext,
      spanHalf * 0.52,
      spanHalf * 0.64,
      spanHalf * 0.12,
      1.4,
      accentColor,
      0.95,
    );
  } else if (motif === "monolith") {
    drawingContext.fillStyle = starkLight;
    drawingContext.fillRect(-spanHalf, -spanHalf, spanHalf * 2, spanHalf * 1.12);
    drawingContext.fillStyle = baseInk;
    drawingContext.fillRect(-spanHalf, spanHalf * 0.12, spanHalf * 2, spanHalf * 0.88);

    drawingContext.strokeStyle = baseInk;
    drawingContext.lineWidth = Math.max(3, spanHalf * 0.14);
    drawingContext.lineCap = "butt";
    for (let chevronIndex = 0; chevronIndex < 2; chevronIndex += 1) {
      const offsetX = -spanHalf * 0.36 + chevronIndex * spanHalf * 0.36;
      drawingContext.beginPath();
      drawingContext.moveTo(offsetX - spanHalf * 0.18, -spanHalf * 0.58);
      drawingContext.lineTo(offsetX + spanHalf * 0.18, -spanHalf * 0.26);
      drawingContext.lineTo(offsetX - spanHalf * 0.18, spanHalf * 0.04);
      drawingContext.stroke();
    }

    drawingContext.fillStyle = accentColor;
    drawingContext.fillRect(spanHalf * 0.28, spanHalf * 0.32, spanHalf * 0.42, spanHalf * 0.42);
    drawingContext.fillStyle = starkLight;
    drawingContext.fillRect(-spanHalf * 0.72, spanHalf * 0.48, spanHalf * 0.74, spanHalf * 0.08);
  } else if (motif === "tape") {
    drawingContext.fillStyle = "#191B20";
    drawingContext.fillRect(-spanHalf, -spanHalf, spanHalf * 2, spanHalf * 2);

    drawingContext.fillStyle = "#F5B841";
    drawingContext.beginPath();
    drawingContext.moveTo(-spanHalf, spanHalf * 0.18);
    drawingContext.lineTo(spanHalf, -spanHalf * 0.54);
    drawingContext.lineTo(spanHalf, -spanHalf * 0.14);
    drawingContext.lineTo(-spanHalf, spanHalf * 0.58);
    drawingContext.closePath();
    drawingContext.fill();

    const reelOffsets = [-spanHalf * 0.36, spanHalf * 0.36];
    for (let reelIndex = 0; reelIndex < reelOffsets.length; reelIndex += 1) {
      const reelX = reelOffsets[reelIndex];
      drawingContext.beginPath();
      drawingContext.arc(reelX, 0, spanHalf * 0.28, 0, FULL_CIRCLE_RADIANS);
      drawingContext.fillStyle = baseInk;
      drawingContext.fill();
      drawingContext.strokeStyle = starkLight;
      drawingContext.lineWidth = Math.max(1.5, spanHalf * 0.045);
      drawingContext.stroke();

      drawingContext.beginPath();
      drawingContext.arc(reelX, 0, spanHalf * 0.09, 0, FULL_CIRCLE_RADIANS);
      drawingContext.fillStyle = starkLight;
      drawingContext.fill();
    }

    drawingContext.fillStyle = starkLight;
    drawingContext.fillRect(-spanHalf * 0.64, spanHalf * 0.62, spanHalf * 1.28, spanHalf * 0.07);
  } else if (motif === "optical") {
    drawingContext.fillStyle = "#161224";
    drawingContext.fillRect(-spanHalf, -spanHalf, spanHalf * 2, spanHalf * 2);

    drawingContext.strokeStyle = "#B296F5";
    drawingContext.lineWidth = Math.max(2.5, spanHalf * 0.11);
    drawingContext.lineCap = "butt";
    drawingContext.beginPath();
    drawingContext.arc(0, 0, spanHalf * 0.68, -Math.PI * 0.85, Math.PI * 0.15);
    drawingContext.stroke();

    drawingContext.strokeStyle = starkLight;
    drawingContext.lineWidth = Math.max(1.5, spanHalf * 0.045);
    drawingContext.beginPath();
    drawingContext.arc(0, 0, spanHalf * 0.42, Math.PI * 0.10, Math.PI * 1.25);
    drawingContext.stroke();

    drawingContext.fillStyle = accentColor;
    drawingContext.fillRect(spanHalf * 0.24, -spanHalf * 0.68, spanHalf * 0.42, spanHalf * 0.42);
    drawDiagonalMark(
      drawingContext,
      spanHalf * 0.45,
      -spanHalf * 0.47,
      spanHalf * 0.11,
      1.5,
      resolvedPalette.signalHighlightInk,
      1,
    );
    drawDiagonalMark(
      drawingContext,
      -spanHalf * 0.52,
      spanHalf * 0.54,
      spanHalf * 0.11,
      1.4,
      starkLight,
      0.85,
    );
  } else {
    drawingContext.fillStyle = "#D4D7DC";
    drawingContext.fillRect(-spanHalf, -spanHalf, spanHalf * 2, spanHalf * 2);

    drawingContext.fillStyle = "#262A31";
    drawingContext.fillRect(-spanHalf * 0.78, -spanHalf * 0.72, spanHalf * 0.68, spanHalf * 1.44);
    drawingContext.fillRect(spanHalf * 0.08, -spanHalf * 0.28, spanHalf * 0.70, spanHalf * 1.00);

    drawHazardStripeBand(
      drawingContext,
      spanHalf * 0.08,
      -spanHalf * 0.72,
      spanHalf * 0.70,
      spanHalf * 0.30,
      0,
      resolvedPalette.signalActive,
    );

    drawingContext.fillStyle = accentColor;
    drawingContext.beginPath();
    drawingContext.arc(-spanHalf * 0.44, -spanHalf * 0.34, spanHalf * 0.16, 0, FULL_CIRCLE_RADIANS);
    drawingContext.fill();
  }
}

/**
 * Draw a spinning circular vinyl disc displaying album artwork, optical grooves,
 * and center record label plate.
 *
 * @param {CanvasRenderingContext2D} drawingContext Canvas 2D context.
 * @param {object | null | undefined} albumSpec Album descriptor.
 * @param {number} centerX Disc center X coordinate.
 * @param {number} centerY Disc center Y coordinate.
 * @param {number} radius Disc outer radius in pixels.
 * @param {number} rotationRadians Spin angle in radians.
 * @param {object} [palette] Semantic Dark/Light appearance palette.
 * @param {string} [appearance="dark"] Semantic appearance ("dark" | "light").
 * @param {number} [alpha=1] Layer opacity.
 * @param {{ getOrLoadAlbumCover?: (albumSpec: object) => object | null } | null} [coverCache] LRU cover cache.
 */
export function drawLibraryVinylDisc(
  drawingContext,
  albumSpec,
  centerX,
  centerY,
  radius,
  rotationRadians,
  palette,
  appearance = "dark",
  alpha = 1,
  coverCache = null,
  vinylOptions = {},
) {
  if (!drawingContext || alpha <= ALPHA_VISIBILITY_THRESHOLD || radius <= 2) {
    return;
  }
  const resolvedAppearance = resolveAppearance(appearance);
  const resolvedPalette = resolvePalette(resolvedAppearance, palette);
  const safeRotation = Number.isFinite(rotationRadians) ? rotationRadians : 0;
  const showCenterLabel = vinylOptions?.showCenterLabel !== false;
  const grooveAlpha = clamp(
    Number.isFinite(vinylOptions?.grooveAlpha) ? vinylOptions.grooveAlpha : 1,
    0,
    1,
  );

  drawingContext.save();
  drawingContext.globalAlpha = alpha;
  drawingContext.translate(centerX, centerY);

  drawingContext.beginPath();
  drawingContext.arc(0, 0, radius, 0, FULL_CIRCLE_RADIANS);
  drawingContext.clip();

  drawingContext.rotate(safeRotation);
  drawLibraryAlbumArtworkMotif(
    drawingContext,
    albumSpec,
    radius,
    resolvedPalette,
    resolvedAppearance,
    coverCache,
  );

  if (grooveAlpha > ALPHA_VISIBILITY_THRESHOLD) {
    const baseGrooveOpacity = resolvedAppearance === "dark" ? 0.1 : 0.13;
    drawingContext.strokeStyle = `rgba(245, 246, 248, ${(baseGrooveOpacity * grooveAlpha).toFixed(3)})`;
    drawingContext.lineWidth = Math.max(0.8, radius * 0.008);
    const grooveRatios = [0.88, 0.78, 0.68, 0.48];
    for (let grooveIndex = 0; grooveIndex < grooveRatios.length; grooveIndex += 1) {
      drawingContext.beginPath();
      drawingContext.arc(0, 0, radius * grooveRatios[grooveIndex], 0, FULL_CIRCLE_RADIANS);
      drawingContext.stroke();
    }
  }

  if (showCenterLabel) {
    const labelRadius = radius * 0.28;
    drawingContext.beginPath();
    drawingContext.arc(0, 0, labelRadius, 0, FULL_CIRCLE_RADIANS);
    drawingContext.fillStyle = "#11151C";
    drawingContext.fill();
    drawingContext.strokeStyle = "#F5F6F8";
    drawingContext.lineWidth = Math.max(1.2, radius * 0.018);
    drawingContext.stroke();

    const labelBlockSize = Math.max(3, radius * 0.075);
    drawingContext.fillStyle = resolvedPalette.signalHighlight;
    drawingContext.fillRect(labelRadius * 0.22, -labelBlockSize / 2, labelBlockSize, labelBlockSize);

    drawingContext.beginPath();
    drawingContext.arc(0, 0, Math.max(2.5, radius * 0.075), 0, FULL_CIRCLE_RADIANS);
    drawingContext.fillStyle = "#060709";
    drawingContext.fill();
  }

  drawingContext.restore();
}

function drawLibraryPeripheralChrome(
  drawingContext,
  title,
  subtitle,
  headerOffsetY,
  chromeAlpha,
  palette,
  appearance,
  animationTimeSeconds = 0,
) {
  if (chromeAlpha <= ALPHA_VISIBILITY_THRESHOLD) {
    return;
  }
  const isDark = appearance === "dark";
  drawingContext.save();
  drawingContext.globalAlpha = chromeAlpha;

  drawingContext.lineCap = "round";
  drawingContext.beginPath();
  drawingContext.arc(
    DISPLAY_CENTER_PX,
    DISPLAY_CENTER_PX,
    RIM_SHEEN_RADIUS_PX,
    -Math.PI * 0.84,
    -Math.PI * 0.30,
  );
  drawingContext.strokeStyle = isDark
    ? "rgba(235, 244, 255, 0.14)"
    : "rgba(255, 255, 255, 0.72)";
  drawingContext.lineWidth = 2;
  drawingContext.stroke();

  drawingContext.beginPath();
  drawingContext.arc(
    DISPLAY_CENTER_PX,
    DISPLAY_CENTER_PX,
    RIM_SHEEN_RADIUS_PX,
    Math.PI * 0.14,
    Math.PI * 0.44,
  );
  drawingContext.strokeStyle = isDark
    ? "rgba(192, 254, 5, 0.18)"
    : "rgba(232, 90, 36, 0.22)";
  drawingContext.lineWidth = 1.5;
  drawingContext.stroke();

  drawingContext.translate(0, headerOffsetY);

  drawingContext.fillStyle = palette.hairlineStrong;
  drawingContext.globalAlpha = chromeAlpha * 0.65;
  drawingContext.fillRect(
    DISPLAY_CENTER_PX - TOP_HANDLE_HALF_WIDTH_PX,
    20,
    TOP_HANDLE_HALF_WIDTH_PX * 2,
    TOP_HANDLE_HEIGHT_PX,
  );

  drawingContext.globalAlpha = chromeAlpha;
  drawingContext.font = `700 16px ${FONT_DISPLAY}`;
  drawingContext.fillStyle = palette.inkPrimary;
  drawingContext.textAlign = "center";
  drawingContext.textBaseline = "middle";
  drawMarqueeCanvasText(drawingContext, title, DISPLAY_CENTER_PX, 44, 184, animationTimeSeconds);

  if (subtitle) {
    drawingContext.font = `600 9px ${FONT_MONO}`;
    drawingContext.fillStyle = palette.inkSecondary;
    drawMarqueeCanvasText(
      drawingContext,
      subtitle,
      DISPLAY_CENTER_PX,
      60,
      196,
      animationTimeSeconds,
    );
  }

  drawingContext.strokeStyle = palette.hairline;
  drawingContext.lineWidth = 1;
  drawingContext.beginPath();
  drawingContext.moveTo(DISPLAY_CENTER_PX - 52, 72);
  drawingContext.lineTo(DISPLAY_CENTER_PX + 52, 72);
  drawingContext.stroke();

  drawingContext.restore();
}

function drawArtistHeaderChrome(
  drawingContext,
  title,
  artistModeBlend,
  headerOffsetY,
  chromeAlpha,
  palette,
  appearance,
  animationTimeSeconds = 0,
) {
  if (chromeAlpha <= ALPHA_VISIBILITY_THRESHOLD) {
    return;
  }
  const isDark = appearance === "dark";
  const clampedBlend = clamp(Number(artistModeBlend ?? 0), 0, 1);
  const easedBlend = easeInOutCubic(clampedBlend);
  const toggleWidth = LIBRARY_CONSTANTS.ARTIST_TOGGLE_WIDTH_PX;
  const toggleHeight = LIBRARY_CONSTANTS.ARTIST_TOGGLE_HEIGHT_PX;
  const toggleLeft = DISPLAY_CENTER_PX - toggleWidth / 2;
  const toggleTop = LIBRARY_CONSTANTS.ARTIST_TOGGLE_TOP_Y;
  const toggleChamfer = LIBRARY_CONSTANTS.ARTIST_TOGGLE_CHAMFER_PX;
  const segmentHalfWidth = toggleWidth / 2;

  drawingContext.save();
  drawingContext.globalAlpha = chromeAlpha;

  drawingContext.lineCap = "round";
  drawingContext.beginPath();
  drawingContext.arc(
    DISPLAY_CENTER_PX,
    DISPLAY_CENTER_PX,
    RIM_SHEEN_RADIUS_PX,
    -Math.PI * 0.84,
    -Math.PI * 0.30,
  );
  drawingContext.strokeStyle = isDark
    ? "rgba(235, 244, 255, 0.14)"
    : "rgba(255, 255, 255, 0.72)";
  drawingContext.lineWidth = 2;
  drawingContext.stroke();

  drawingContext.beginPath();
  drawingContext.arc(
    DISPLAY_CENTER_PX,
    DISPLAY_CENTER_PX,
    RIM_SHEEN_RADIUS_PX,
    Math.PI * 0.14,
    Math.PI * 0.44,
  );
  drawingContext.strokeStyle = isDark
    ? "rgba(192, 254, 5, 0.18)"
    : "rgba(232, 90, 36, 0.22)";
  drawingContext.lineWidth = 1.5;
  drawingContext.stroke();

  drawingContext.translate(0, headerOffsetY);

  drawingContext.fillStyle = palette.hairlineStrong;
  drawingContext.globalAlpha = chromeAlpha * 0.65;
  drawingContext.fillRect(
    DISPLAY_CENTER_PX - TOP_HANDLE_HALF_WIDTH_PX,
    20,
    TOP_HANDLE_HALF_WIDTH_PX * 2,
    TOP_HANDLE_HEIGHT_PX,
  );

  drawingContext.globalAlpha = chromeAlpha;
  drawingContext.font = `700 15.5px ${FONT_DISPLAY}`;
  drawingContext.fillStyle = palette.inkPrimary;
  drawingContext.textAlign = "center";
  drawingContext.textBaseline = "middle";
  drawMarqueeCanvasText(drawingContext, title, DISPLAY_CENTER_PX, 38, 184, animationTimeSeconds);

  traceChamferRect(drawingContext, toggleLeft, toggleTop, toggleWidth, toggleHeight, toggleChamfer);
  drawingContext.fillStyle = isDark
    ? "rgba(210, 230, 255, 0.08)"
    : "rgba(255, 255, 255, 0.56)";
  drawingContext.fill();
  drawingContext.strokeStyle = isDark
    ? "rgba(235, 245, 255, 0.24)"
    : "rgba(46, 50, 56, 0.22)";
  drawingContext.lineWidth = 1;
  drawingContext.stroke();

  drawingContext.save();
  traceChamferRect(drawingContext, toggleLeft, toggleTop, toggleWidth, toggleHeight, toggleChamfer);
  drawingContext.clip();
  const activeLeftX = lerp(toggleLeft, toggleLeft + segmentHalfWidth, easedBlend);
  drawingContext.fillStyle = palette.signalHighlight;
  drawingContext.fillRect(activeLeftX, toggleTop, segmentHalfWidth, toggleHeight);
  drawingContext.restore();

  drawingContext.font = `800 8.5px ${FONT_MONO}`;
  drawingContext.textAlign = "center";
  drawingContext.textBaseline = "middle";
  drawingContext.fillStyle =
    clampedBlend < 0.5 ? palette.signalHighlightInk : palette.inkSecondary;
  drawingContext.fillText(
    "ALBUMS",
    toggleLeft + segmentHalfWidth / 2,
    toggleTop + toggleHeight / 2 + 0.5,
  );
  drawingContext.fillStyle =
    clampedBlend >= 0.5 ? palette.signalHighlightInk : palette.inkSecondary;
  drawingContext.fillText(
    "SONGS",
    toggleLeft + segmentHalfWidth * 1.5,
    toggleTop + toggleHeight / 2 + 0.5,
  );

  drawingContext.strokeStyle = palette.hairline;
  drawingContext.lineWidth = 1;
  drawingContext.beginPath();
  drawingContext.moveTo(DISPLAY_CENTER_PX - 52, LIBRARY_CONSTANTS.ARTIST_HEADER_DIVIDER_Y);
  drawingContext.lineTo(DISPLAY_CENTER_PX + 52, LIBRARY_CONSTANTS.ARTIST_HEADER_DIVIDER_Y);
  drawingContext.stroke();

  drawingContext.restore();
}

/**
 * Draw the curved right-edge scroll indicator arc and active position thumb.
 *
 * @param {CanvasRenderingContext2D} drawingContext Canvas 2D context.
 * @param {number} scrollPosition Continuous list scroll position.
 * @param {number} totalRows Total row count in the list.
 * @param {number} alpha Layer opacity.
 * @param {object} palette Semantic Dark/Light appearance palette.
 */
export function drawLibraryScrollArc(drawingContext, scrollPosition, totalRows, alpha, palette) {
  if (totalRows <= LIBRARY_CONSTANTS.LIST_FULL_SLOTS || alpha <= CONTENT_VISIBILITY_THRESHOLD) {
    return;
  }
  const arcSpan = SCROLL_ARC_END_RADIANS - SCROLL_ARC_START_RADIANS;
  const visibleRatio = clamp(LIBRARY_CONSTANTS.LIST_FULL_SLOTS / totalRows, 0.22, 0.72);
  const thumbSpan = arcSpan * visibleRatio;
  const maxScroll = Math.max(1, totalRows - LIBRARY_CONSTANTS.LIST_FULL_SLOTS);
  const normalizedScroll = clamp(scrollPosition / maxScroll, 0, 1);
  const thumbStart = lerp(
    SCROLL_ARC_START_RADIANS,
    SCROLL_ARC_END_RADIANS - thumbSpan,
    normalizedScroll,
  );

  drawingContext.save();
  drawingContext.globalAlpha = alpha;
  drawingContext.lineCap = "butt";

  drawingContext.beginPath();
  drawingContext.arc(
    DISPLAY_CENTER_PX,
    DISPLAY_CENTER_PX,
    SCROLL_ARC_RADIUS_PX,
    SCROLL_ARC_START_RADIANS,
    SCROLL_ARC_END_RADIANS,
  );
  drawingContext.strokeStyle = palette.hairline;
  drawingContext.lineWidth = 2.5;
  drawingContext.stroke();

  drawingContext.beginPath();
  drawingContext.arc(
    DISPLAY_CENTER_PX,
    DISPLAY_CENTER_PX,
    SCROLL_ARC_RADIUS_PX,
    thumbStart,
    thumbStart + thumbSpan,
  );
  drawingContext.strokeStyle = palette.signalHighlight;
  drawingContext.lineWidth = 3.2;
  drawingContext.stroke();

  drawingContext.restore();
}

/**
 * Draw the subtle perspective floor grid and diagonal registration marks.
 *
 * @param {CanvasRenderingContext2D} drawingContext Canvas 2D context.
 * @param {number} scrollPosition Continuous scroll offset for subtle floor parallax.
 * @param {number} alpha Layer opacity.
 * @param {object} palette Semantic Dark/Light appearance palette.
 * @param {string} appearance Semantic appearance ("dark" | "light").
 */
export function drawLibraryPerspectiveFloor(
  drawingContext,
  scrollPosition,
  alpha,
  palette,
  appearance,
) {
  if (alpha <= CONTENT_VISIBILITY_THRESHOLD) {
    return;
  }
  const isDark = appearance === "dark";
  drawingContext.save();
  drawingContext.globalAlpha = alpha;

  const floorTopY = 266;
  const floorBottomY = 326;
  drawingContext.strokeStyle = isDark
    ? "rgba(196, 218, 250, 0.10)"
    : "rgba(46, 52, 62, 0.10)";
  drawingContext.lineWidth = 1;
  const rowRatios = [0, 0.5, 1];
  for (let rowIndex = 0; rowIndex < rowRatios.length; rowIndex += 1) {
    const lineY = lerp(floorTopY, floorBottomY, rowRatios[rowIndex]);
    const span = lerp(112, 74, rowRatios[rowIndex]);
    drawingContext.beginPath();
    drawingContext.moveTo(DISPLAY_CENTER_PX - span, lineY);
    drawingContext.lineTo(DISPLAY_CENTER_PX + span, lineY);
    drawingContext.stroke();
  }
  const fractional = ((scrollPosition % 1) + 1) % 1;
  for (let columnIndex = -3; columnIndex <= 3; columnIndex += 1) {
    const topX = DISPLAY_CENTER_PX + (columnIndex - fractional * 0.5) * 34;
    const bottomX = DISPLAY_CENTER_PX + (columnIndex - fractional * 0.5) * 52;
    drawingContext.beginPath();
    drawingContext.moveTo(topX, floorTopY);
    drawingContext.lineTo(bottomX, floorBottomY);
    drawingContext.stroke();
  }

  drawDiagonalMark(
    drawingContext,
    DISPLAY_CENTER_PX - 102,
    74,
    3.5,
    1.2,
    palette.hairlineStrong,
    alpha * 0.55,
  );
  drawDiagonalMark(
    drawingContext,
    DISPLAY_CENTER_PX + 102,
    294,
    3.5,
    1.2,
    palette.hairlineStrong,
    alpha * 0.55,
  );
  drawingContext.restore();
}

/**
 * Draw an animated 3-bar mini equalizer indicator for the currently playing track row.
 *
 * @param {CanvasRenderingContext2D} drawingContext Canvas 2D context.
 * @param {number} centerX Center X coordinate.
 * @param {number} centerY Center Y coordinate.
 * @param {number} clockSeconds Continuous clock in seconds.
 * @param {object} palette Semantic Dark/Light appearance palette.
 */
export function drawLiveMiniEqualizer(drawingContext, centerX, centerY, clockSeconds, palette) {
  const barWidth = 2.6;
  const barGap = 2.2;
  const totalWidth = barWidth * 3 + barGap * 2;
  const startX = centerX - totalWidth / 2;
  drawingContext.save();
  drawingContext.fillStyle = palette.signalHighlight;
  for (let barIndex = 0; barIndex < 3; barIndex += 1) {
    const wave =
      0.25 +
      0.75 * (0.5 + 0.5 * Math.sin(clockSeconds * (5.2 + barIndex * 1.4) + barIndex * 1.7));
    const barHeight = 3.5 + wave * 9.5;
    drawingContext.fillRect(
      startX + barIndex * (barWidth + barGap),
      centerY + 6 - barHeight,
      barWidth,
      barHeight,
    );
  }
  drawingContext.restore();
}

function resolveLibraryRowPressIntensity(rowIndex, libraryState) {
  let activeLevel = 0;
  if (libraryState.pressedRowIndex === rowIndex) {
    activeLevel = Math.max(activeLevel, Number(libraryState.pressHighlightLevel ?? 0));
  }
  if (libraryState.pressFlashRowIndex === rowIndex) {
    activeLevel = Math.max(activeLevel, Number(libraryState.pressFlashLevel ?? 0));
  }
  return clamp(activeLevel, 0, 1);
}

function resolveCrateAlbumsFromState(libraryState, currentView) {
  if (
    Array.isArray(libraryState?.currentCrateAlbums) &&
    (currentView?.isFavoritesView ||
      libraryState.currentCrateAlbums.length > 0 ||
      !libraryState?.catalog?.albums?.length)
  ) {
    return libraryState.currentCrateAlbums;
  }
  const sortMode = libraryState?.albumSortMode || "alphabetical";
  const catalogAlbums = Array.isArray(libraryState?.catalog?.albums)
    ? libraryState.catalog.albums
    : [];
  if (currentView?.isFavoritesView) {
    return sortAlbumsByMode(
      catalogAlbums.filter((albumEntry) => Boolean(albumEntry?.isFavorite)),
      sortMode,
    );
  }
  if (Array.isArray(currentView?.albumIds) && currentView.albumIds.length > 0) {
    const albumByIdMap = new Map(catalogAlbums.map((albumEntry) => [albumEntry.id, albumEntry]));
    return sortAlbumsByMode(
      currentView.albumIds.map((albumId) => albumByIdMap.get(albumId)).filter(Boolean),
      sortMode,
    );
  }
  return sortAlbumsByMode(catalogAlbums, sortMode);
}

function resolveRowsFromState(libraryState, currentView) {
  if (Array.isArray(libraryState?.currentRows)) {
    return libraryState.currentRows;
  }
  if (currentView?.kind === "hub") {
    const counts = libraryState?.counts || {};
    return LIBRARY_HUB_SECTIONS.map((sectionSpec) => {
      const countValue =
        sectionSpec.id === "all-songs" ? counts.tracks : counts[sectionSpec.id];
      const countLabel = formatIntegerCount(countValue ?? 0);
      return {
        id: sectionSpec.id,
        rowType: "hub",
        title: sectionSpec.title,
        countLabel,
        countText: countLabel,
        sectionSpec,
      };
    });
  }
  return [];
}

function resolveLetterEntriesFromState(currentView, libraryState) {
  if (Array.isArray(libraryState?.letterEntries)) {
    return libraryState.letterEntries;
  }
  if (currentView.kind === "albums") {
    const crateAlbums = resolveCrateAlbumsFromState(libraryState, currentView);
    if (crateAlbums.length < MIN_ALPHABETICAL_INDEX_COUNT) {
      return [];
    }
    const anchors = buildAlbumSeekIndex(
      crateAlbums,
      libraryState?.albumSortMode || "alphabetical",
    );
    return anchors.length >= MIN_LETTER_GROUPS_FOR_SEEK ? anchors : [];
  }

  if (currentView.kind === "artists") {
    const entries = resolveRowsFromState(libraryState, currentView);
    if (entries.length < MIN_ALPHABETICAL_INDEX_COUNT) {
      return [];
    }
    const anchors = buildCollectionSeekIndex(
      entries,
      libraryState?.artistSortMode || "alphabetical",
      (entry) => entry?.title ?? entry?.name ?? "",
    );
    return anchors.length >= MIN_LETTER_GROUPS_FOR_SEEK ? anchors : [];
  }

  const isAlphabeticalView =
    currentView.kind === "genres" ||
    currentView.kind === "playlists" ||
    (currentView.kind === "tracks" && currentView.sourceType !== "album");

  if (!isAlphabeticalView) {
    return [];
  }

  const entries = resolveRowsFromState(libraryState, currentView);

  if (entries.length < MIN_ALPHABETICAL_INDEX_COUNT) {
    return [];
  }

  const letters = buildAlphabeticalLetterIndex(
    entries,
    (entry) => entry?.title ?? entry?.name ?? entry,
  );
  return letters.length >= MIN_LETTER_GROUPS_FOR_SEEK ? letters : [];
}

function buildHubSectionRowsFromCounts(counts = {}) {
  const safeCounts = counts && typeof counts === "object" ? counts : {};
  const countMap = {
    favorites: Number.isFinite(safeCounts.favorites) ? Math.max(0, Math.round(safeCounts.favorites)) : 0,
    playlists: Number.isFinite(safeCounts.playlists) ? Math.max(0, Math.round(safeCounts.playlists)) : 0,
    artists: Number.isFinite(safeCounts.artists) ? Math.max(0, Math.round(safeCounts.artists)) : 0,
    albums: Number.isFinite(safeCounts.albums) ? Math.max(0, Math.round(safeCounts.albums)) : 0,
    genres: Number.isFinite(safeCounts.genres) ? Math.max(0, Math.round(safeCounts.genres)) : 0,
    "all-songs": Number.isFinite(safeCounts.tracks) ? Math.max(0, Math.round(safeCounts.tracks)) : 0,
  };
  return LIBRARY_HUB_SECTIONS.map((sectionSpec) => {
    const countLabel = formatIntegerCount(countMap[sectionSpec.id] || 0);
    return {
      id: sectionSpec.id,
      rowType: "hub",
      title: sectionSpec.title,
      countLabel,
      countText: countLabel,
    };
  });
}

function drawSingleHubCrystalTile(
  drawingContext,
  row,
  {
    centerX,
    centerY,
    width,
    height,
    isPeeking = false,
    tileAlpha = 1,
    pressIntensity = 0,
    clockSeconds = 0,
    showIcon = true,
    palette,
    appearance = "dark",
  },
) {
  if (tileAlpha <= CONTENT_VISIBILITY_THRESHOLD || width <= 2 || height <= 2) {
    return;
  }
  const isDark = appearance === "dark";
  const isPressed = pressIntensity > 0.10;
  const leftX = centerX - width / 2;
  const topY = centerY - height / 2;
  const chamferScale = clamp(width / 114, 0.5, 1.0);
  const outerChamfer = 12 * chamferScale;
  const innerChamfer = 9 * chamferScale;

  drawingContext.save();
  drawingContext.globalAlpha = tileAlpha;

  traceChamferRect(drawingContext, leftX, topY, width, height, outerChamfer);
  drawingContext.fillStyle = isDark
    ? "rgba(210, 230, 255, 0.065)"
    : "rgba(255, 255, 255, 0.54)";
  drawingContext.fill();

  traceChamferRect(drawingContext, leftX + 4, topY + 4, width - 8, height - 8, innerChamfer);
  drawingContext.strokeStyle = isDark
    ? "rgba(225, 240, 255, 0.09)"
    : "rgba(255, 255, 255, 0.65)";
  drawingContext.lineWidth = 0.85;
  drawingContext.stroke();

  drawingContext.save();
  traceChamferRect(drawingContext, leftX, topY, width, height, outerChamfer);
  drawingContext.clip();
  if (pressIntensity > CONTENT_VISIBILITY_THRESHOLD) {
    drawingContext.fillStyle = palette.signalHighlightSoft;
    drawingContext.globalAlpha = tileAlpha * pressIntensity;
    drawingContext.fillRect(leftX, topY, width, height);
  }
  drawingContext.globalAlpha = tileAlpha * (isPressed ? 0.22 : 0.085);
  drawingContext.fillStyle = "#FFFFFF";
  drawingContext.beginPath();
  drawingContext.moveTo(leftX + width * 0.25, topY);
  drawingContext.lineTo(leftX + width * 0.52, topY);
  drawingContext.lineTo(leftX + width * 0.12, topY + height);
  drawingContext.lineTo(leftX - width * 0.15, topY + height);
  drawingContext.closePath();
  drawingContext.fill();
  drawingContext.restore();

  traceChamferRect(drawingContext, leftX + 0.5, topY + 0.5, width - 1, height - 1, outerChamfer);
  drawingContext.strokeStyle = isPressed
    ? palette.signalHighlight
    : isDark
      ? "rgba(235, 245, 255, 0.24)"
      : "rgba(46, 50, 56, 0.22)";
  drawingContext.lineWidth = isPressed ? 1.6 : 1.1;
  drawingContext.stroke();

  if (pressIntensity > 0.04) {
    drawLibraryCornerBrackets(
      drawingContext,
      leftX,
      topY,
      width,
      height,
      12 * chamferScale,
      2.3,
      palette.signalHighlight,
      tileAlpha * pressIntensity,
    );
  }

  const sizeScale = width / (isPeeking ? 94 : 114);
  if (showIcon) {
    drawLibraryLivingIcon(
      drawingContext,
      row.id,
      centerX,
      centerY - HUB_TILE_ICON_OFFSET_Y_PX * sizeScale,
      (isPeeking ? HUB_TILE_PEEK_ICON_RADIUS_PX : HUB_TILE_FULL_ICON_RADIUS_PX) * sizeScale,
      clockSeconds,
      isPressed,
      palette,
      appearance,
    );
  }

  drawingContext.textAlign = "center";
  drawingContext.textBaseline = "middle";
  const titleFontSize = (isPeeking ? 11.5 : 13) * sizeScale;
  drawingContext.font = `${isPeeking ? "600" : "700"} ${titleFontSize.toFixed(2)}px ${FONT_DISPLAY}`;
  drawingContext.fillStyle = isPressed ? palette.signalHighlight : palette.inkPrimary;
  drawMarqueeCanvasText(
    drawingContext,
    row.title,
    centerX,
    centerY + 25 * sizeScale,
    Math.max(40, width - 14),
    clockSeconds,
  );

  const countFontSize = 9 * sizeScale;
  drawingContext.font = `600 ${countFontSize.toFixed(2)}px ${FONT_MONO}`;
  drawingContext.fillStyle = palette.inkSecondary;
  drawingContext.fillText(
    row.countText ?? row.countLabel ?? "0",
    centerX,
    centerY + 41 * sizeScale,
  );

  drawingContext.restore();
}

/**
 * Render the Library 2-up Crystal Hub depth-dive transition layer for Home <-> Library
 * handoffs (`revealProgress` in [0, 1]). At `revealProgress === 1`, output matches the
 * settled Library Hub frame pixel-for-pixel so screen handoff has zero visual pop.
 *
 * @param {CanvasRenderingContext2D} drawingContext Canvas 2D context.
 * @param {object} [options={}] Transition state, counts, scroll position, and theme.
 */
export function renderLibraryHubTransitionLayer(drawingContext, options = {}) {
  if (!drawingContext || !options || typeof options !== "object") {
    return;
  }
  const revealProgress = clamp(Number(options.revealProgress ?? 0), 0, 1);
  if (revealProgress <= CONTENT_VISIBILITY_THRESHOLD) {
    return;
  }

  const appearance = resolveAppearance(options.appearance);
  const palette = resolvePalette(appearance, options.palette);
  const scrollPairPosition = Number.isFinite(options.scrollPosition)
    ? Math.max(0, options.scrollPosition)
    : 0;
  const clockSeconds = Number.isFinite(options.clockSeconds) ? options.clockSeconds : 0;
  const rows = Array.isArray(options.rows)
    ? options.rows
    : buildHubSectionRowsFromCounts(options.counts);
  const totalTrackCount = Number.isFinite(options.counts?.tracks)
    ? Math.max(0, Math.round(options.counts.tracks))
    : 0;
  const hubSubtitle =
    totalTrackCount === 0
      ? "0 TRACKS \u00b7 EMPTY COLLECTION"
      : `2-UP CRYSTAL HUB \u00b7 ${formatIntegerCount(totalTrackCount)} TRACKS`;

  const floorAlpha = easeOutCubic(
    normalizeRange(revealProgress, HUB_DIVE_FLOOR_START, HUB_DIVE_FLOOR_END),
  );
  const headerProgress = normalizeRange(revealProgress, HUB_DIVE_HEADER_START, HUB_DIVE_HEADER_END);
  const headerAlpha = easeOutCubic(headerProgress);
  const headerOffsetY = lerp(HUB_DIVE_HEADER_DRIFT_Y_PX, 0, easeOutExpo(headerProgress));

  drawLibraryPerspectiveFloor(drawingContext, scrollPairPosition, floorAlpha, palette, appearance);
  drawLibraryPeripheralChrome(
    drawingContext,
    "Library",
    hubSubtitle,
    headerOffsetY,
    headerAlpha,
    palette,
    appearance,
    clockSeconds,
  );

  drawingContext.save();
  drawingContext.beginPath();
  drawingContext.rect(
    0,
    HEADER_CLIP_TOP_Y,
    DISPLAY_DIAMETER_PX,
    DISPLAY_DIAMETER_PX - HEADER_CLIP_TOP_Y,
  );
  drawingContext.clip();

  for (let tileIndex = 0; tileIndex < rows.length; tileIndex += 1) {
    const tileRect = computeLibraryHubTileRect(tileIndex, scrollPairPosition);
    if (tileRect.alpha <= CONTENT_VISIBILITY_THRESHOLD) {
      continue;
    }
    const rowProgress = tileRect.isPeeking
      ? normalizeRange(revealProgress, HUB_DIVE_PEEK_ROW_START, HUB_DIVE_PEEK_ROW_END)
      : normalizeRange(revealProgress, HUB_DIVE_TOP_ROW_START, HUB_DIVE_TOP_ROW_END);
    const tileFade = easeOutCubic(rowProgress);
    if (tileFade <= CONTENT_VISIBILITY_THRESHOLD) {
      continue;
    }
    const rowEase = easeOutExpo(rowProgress);
    const tileScale = lerp(HUB_DIVE_TILE_MIN_SCALE, 1.0, rowEase);
    const tileCenterX = lerp(DISPLAY_CENTER_PX, tileRect.centerX, tileScale);
    const tileCenterY = lerp(
      DISPLAY_CENTER_PX + HUB_DIVE_TILE_RISE_Y_PX,
      tileRect.centerY,
      tileScale,
    );

    drawSingleHubCrystalTile(drawingContext, rows[tileIndex], {
      centerX: tileCenterX,
      centerY: tileCenterY,
      width: tileRect.width * tileScale,
      height: tileRect.height * tileScale,
      isPeeking: tileRect.isPeeking,
      tileAlpha: tileRect.alpha * tileFade,
      pressIntensity: 0,
      clockSeconds,
      showIcon: true,
      palette,
      appearance,
    });
  }

  drawingContext.restore();

  const arcAlpha = easeOutCubic(
    normalizeRange(revealProgress, HUB_DIVE_ARC_START, HUB_DIVE_ARC_END),
  );
  if (arcAlpha > CONTENT_VISIBILITY_THRESHOLD) {
    const hubScrollRowSpan = Math.ceil(rows.length / 2) + 1;
    drawLibraryScrollArc(
      drawingContext,
      scrollPairPosition,
      hubScrollRowSpan,
      arcAlpha * 0.85,
      palette,
    );
  }
}

function renderLibraryHubGrid(
  drawingContext,
  viewEntry,
  rows,
  libraryState,
  contentAlpha,
  palette,
  appearance,
) {
  const scrollPairPosition = Number.isFinite(viewEntry.scrollPosition) ? viewEntry.scrollPosition : 0;
  const clockSeconds = Number.isFinite(libraryState.idleClockSeconds)
    ? libraryState.idleClockSeconds
    : 0;

  drawingContext.save();
  drawingContext.beginPath();
  drawingContext.rect(
    0,
    HEADER_CLIP_TOP_Y,
    DISPLAY_DIAMETER_PX,
    DISPLAY_DIAMETER_PX - HEADER_CLIP_TOP_Y,
  );
  drawingContext.clip();

  for (let tileIndex = 0; tileIndex < rows.length; tileIndex += 1) {
    const tileRect = computeLibraryHubTileRect(tileIndex, scrollPairPosition);
    if (tileRect.alpha <= CONTENT_VISIBILITY_THRESHOLD) {
      continue;
    }
    const row = rows[tileIndex];
    const tileAlpha = tileRect.alpha * contentAlpha;
    const pressIntensity = resolveLibraryRowPressIntensity(tileIndex, libraryState);
    const scale = lerp(1.0, 0.96, pressIntensity);

    drawSingleHubCrystalTile(drawingContext, row, {
      centerX: tileRect.centerX,
      centerY: tileRect.centerY,
      width: tileRect.width * scale,
      height: tileRect.height * scale,
      isPeeking: tileRect.isPeeking,
      tileAlpha,
      pressIntensity,
      clockSeconds,
      showIcon: true,
      palette,
      appearance,
    });
  }

  drawingContext.restore();

  if (viewEntry.kind === "artists" && rows.length > 0) {
    drawFloatingSortButton(
      drawingContext,
      libraryState.artistSortMode || "alphabetical",
      LIBRARY_CONSTANTS.ARTISTS_SORT_BUTTON_X,
      LIBRARY_CONSTANTS.ARTISTS_SORT_BUTTON_Y,
      clockSeconds,
      contentAlpha,
      palette,
      appearance,
    );
  }

  const hideDefaultThumb =
    (libraryState.arcFeedbackAlpha ?? 0) > 0.05 ||
    (libraryState.letterSearchHudAlpha ?? 0) > 0.05;
  if (!hideDefaultThumb) {
    const hubScrollRowSpan = Math.ceil(rows.length / 2) + 1;
    drawLibraryScrollArc(
      drawingContext,
      scrollPairPosition,
      hubScrollRowSpan,
      contentAlpha * 0.85,
      palette,
    );
  }
}

function renderLibraryCurvedList(
  drawingContext,
  viewEntry,
  rows,
  libraryState,
  contentAlpha,
  palette,
  appearance,
  animationTimeSeconds = 0,
) {
  const scrollPosition = Number.isFinite(viewEntry.scrollPosition) ? viewEntry.scrollPosition : 0;
  const clockSeconds = Number.isFinite(libraryState.idleClockSeconds)
    ? libraryState.idleClockSeconds
    : animationTimeSeconds;
  const marqueeTimeSeconds = Number.isFinite(animationTimeSeconds)
    ? animationTimeSeconds
    : clockSeconds;

  drawingContext.save();
  drawingContext.beginPath();
  drawingContext.rect(
    0,
    HEADER_CLIP_TOP_Y,
    DISPLAY_DIAMETER_PX,
    DISPLAY_DIAMETER_PX - HEADER_CLIP_TOP_Y,
  );
  drawingContext.clip();

  const visibleStartIndex = Math.max(0, Math.floor(scrollPosition) - 2);
  const visibleEndIndex = Math.min(rows.length - 1, Math.ceil(scrollPosition) + 4);
  for (let rowIndex = visibleStartIndex; rowIndex <= visibleEndIndex; rowIndex += 1) {
    const layout = computeLibraryCurvedRowLayout(rowIndex, scrollPosition);
    if (layout.alpha <= CONTENT_VISIBILITY_THRESHOLD) {
      continue;
    }
    const row = rows[rowIndex];
    const rowAlpha = layout.alpha * contentAlpha;
    const isPreview = layout.previewWeight > 0.45;
    const pressIntensity = resolveLibraryRowPressIntensity(rowIndex, libraryState);
    const isPressed = pressIntensity > 0.10;
    const leftEdgeX = DISPLAY_CENTER_PX - layout.rowHalfWidth;
    const rightEdgeX = DISPLAY_CENTER_PX + layout.rowHalfWidth;

    drawingContext.save();
    drawingContext.globalAlpha = rowAlpha;

    if (pressIntensity > CONTENT_VISIBILITY_THRESHOLD) {
      traceChamferRect(
        drawingContext,
        leftEdgeX,
        layout.rowY - layout.rowHeight / 2,
        layout.rowHalfWidth * 2,
        layout.rowHeight,
        8,
      );
      drawingContext.fillStyle = palette.signalHighlightSoft;
      drawingContext.fill();
      drawLibraryCornerBrackets(
        drawingContext,
        leftEdgeX,
        layout.rowY - layout.rowHeight / 2,
        layout.rowHalfWidth * 2,
        layout.rowHeight,
        10,
        2.1,
        palette.signalHighlight,
        rowAlpha * pressIntensity,
      );
    } else {
      drawingContext.strokeStyle = palette.hairline;
      drawingContext.lineWidth = 1;
      drawingContext.beginPath();
      drawingContext.moveTo(leftEdgeX + 12, layout.rowY + layout.rowHeight / 2);
      drawingContext.lineTo(rightEdgeX - 12, layout.rowY + layout.rowHeight / 2);
      drawingContext.stroke();
    }

    let textOriginX = leftEdgeX + 40;
    const rightPaddingX = rightEdgeX - 10;

    if (row.rowType === "artist" || row.rowType === "genre" || row.rowType === "playlist") {
      const iconKind =
        row.rowType === "artist"
          ? "artists"
          : row.rowType === "genre"
            ? "genres"
            : "playlists";
      drawLibraryLivingIcon(
        drawingContext,
        iconKind,
        leftEdgeX + 22,
        layout.rowY,
        isPreview ? 9 : 11,
        clockSeconds,
        isPressed,
        palette,
        appearance,
      );
      textOriginX = leftEdgeX + 44;

      const countText = String(row.countText ?? row.countLabel ?? "0");
      drawingContext.font = `${isPressed ? "700" : "600"} 10.5px ${FONT_MONO}`;
      drawingContext.fillStyle = isPressed ? palette.signalHighlight : palette.inkSecondary;
      drawingContext.textAlign = "right";
      drawingContext.textBaseline = "middle";
      drawingContext.fillText(countText, rightPaddingX, layout.rowY + 0.5);

      const countWidth = drawingContext.measureText(countText).width;
      const showArtistHeart = row.rowType === "artist" && Boolean(row.isFavorite);
      if (showArtistHeart) {
        drawCrispHeartGlyph(
          drawingContext,
          rightPaddingX - countWidth - 10,
          layout.rowY + 0.5,
          4.5,
          true,
          palette.signalHighlight,
          palette.signalHighlight,
          1.1,
        );
      }
      const rightReservedWidth = countWidth + (showArtistHeart ? 24 : 10);
      const maxTitleWidth = Math.max(44, rightPaddingX - textOriginX - rightReservedWidth);
      drawingContext.font = `${isPreview ? "600 13.5px" : "700 15.5px"} ${FONT_DISPLAY}`;
      drawingContext.fillStyle = isPreview ? palette.inkSecondary : palette.inkPrimary;
      drawingContext.textAlign = "left";
      drawMarqueeCanvasText(
        drawingContext,
        row.title,
        textOriginX,
        layout.rowY + 0.5,
        maxTitleWidth,
        marqueeTimeSeconds,
      );
    } else if (row.rowType === "track") {
      const isPlayingTrack = Boolean(
        libraryState.activeTrackId && row.id === libraryState.activeTrackId,
      );

      if (row.showTrackNumbers) {
        drawingContext.font = `700 10.5px ${FONT_MONO}`;
        drawingContext.fillStyle =
          isPlayingTrack || isPressed ? palette.signalHighlight : palette.inkSecondary;
        drawingContext.textAlign = "left";
        drawingContext.textBaseline = "middle";
        drawingContext.fillText(String(row.trackNumber || "01"), leftEdgeX + 12, layout.rowY + 0.5);
        textOriginX = leftEdgeX + 36;
      } else {
        drawLibraryLivingIcon(
          drawingContext,
          "track",
          leftEdgeX + 20,
          layout.rowY,
          isPreview ? 8 : 10,
          clockSeconds,
          isPressed || isPlayingTrack,
          palette,
          appearance,
        );
        textOriginX = leftEdgeX + 40;
      }

      let rightReserved = 42;
      if (isPlayingTrack) {
        drawLiveMiniEqualizer(drawingContext, rightPaddingX - 10, layout.rowY, clockSeconds, palette);
        rightReserved = 28;
      } else {
        const durationLabel =
          typeof row.duration === "string" && row.duration.length > 0
            ? row.duration
            : formatClockTime(row.durationSeconds);
        drawingContext.font = `600 10px ${FONT_MONO}`;
        drawingContext.fillStyle = isPressed ? palette.signalHighlight : palette.inkSecondary;
        drawingContext.textAlign = "right";
        drawingContext.textBaseline = "middle";
        drawingContext.fillText(durationLabel, rightPaddingX, layout.rowY + 0.5);
        rightReserved = drawingContext.measureText(durationLabel).width + 10;
      }

      const maxTitleWidth = Math.max(48, rightPaddingX - textOriginX - rightReserved);
      drawingContext.textAlign = "left";
      drawingContext.textBaseline = "middle";

      if (!isPreview && !row.showTrackNumbers) {
        drawingContext.font = `700 15px ${FONT_DISPLAY}`;
        drawingContext.fillStyle = isPlayingTrack ? palette.signalHighlight : palette.inkPrimary;
        drawMarqueeCanvasText(
          drawingContext,
          row.title,
          textOriginX,
          layout.rowY - 8,
          maxTitleWidth,
          marqueeTimeSeconds,
        );

        drawingContext.font = `600 9px ${FONT_MONO}`;
        drawingContext.fillStyle = palette.inkSecondary;
        const subtitleText = row.showAlbumSubtitle
          ? String(row.albumTitle || "").toUpperCase()
          : String(row.artist || "").toUpperCase();
        drawMarqueeCanvasText(
          drawingContext,
          subtitleText,
          textOriginX,
          layout.rowY + 9,
          maxTitleWidth,
          marqueeTimeSeconds,
        );
      } else {
        drawingContext.font = `${isPreview ? "600 13.5px" : "700 15px"} ${FONT_DISPLAY}`;
        drawingContext.fillStyle = isPlayingTrack
          ? palette.signalHighlight
          : isPreview
            ? palette.inkSecondary
            : palette.inkPrimary;
        drawMarqueeCanvasText(
          drawingContext,
          row.title,
          textOriginX,
          layout.rowY + 0.5,
          maxTitleWidth,
          marqueeTimeSeconds,
        );
      }
    }

    drawingContext.restore();
  }

  drawingContext.restore();

  if (viewEntry.kind === "artists" && rows.length > 0) {
    drawFloatingSortButton(
      drawingContext,
      libraryState.artistSortMode || "alphabetical",
      LIBRARY_CONSTANTS.ARTISTS_SORT_BUTTON_X,
      LIBRARY_CONSTANTS.ARTISTS_SORT_BUTTON_Y,
      clockSeconds,
      contentAlpha,
      palette,
      appearance,
    );
  }

  const hideDefaultThumb =
    (libraryState.arcFeedbackAlpha ?? 0) > 0.05 ||
    (libraryState.letterSearchHudAlpha ?? 0) > 0.05;
  if (!hideDefaultThumb) {
    drawLibraryScrollArc(drawingContext, scrollPosition, rows.length, contentAlpha * 0.85, palette);
  }
}

/**
 * Draw a square crate album sleeve box without rendering a peeking vinyl disc.
 *
 * @param {CanvasRenderingContext2D} drawingContext Canvas 2D context.
 * @param {object} album Album metadata descriptor.
 * @param {object} options Geometry and style configuration.
 */
export function drawCrateAlbumSleeveBox(
  drawingContext,
  album,
  {
    centerX,
    centerY,
    sleeveSize,
    focusWeight = 1,
    delta = 0,
    baseAlpha = 1,
    alpha,
    palette,
    appearance = "dark",
    coverCache = null,
  },
) {
  const resolvedAlpha = Number.isFinite(alpha) ? alpha : baseAlpha;
  if (!album || resolvedAlpha <= CONTENT_VISIBILITY_THRESHOLD) {
    return;
  }
  const resolvedAppearance = resolveAppearance(appearance);
  const resolvedPalette = resolvePalette(resolvedAppearance, palette);
  const isDark = resolvedAppearance === "dark";
  const sleeveHalf = sleeveSize / 2;

  drawingContext.save();
  drawingContext.globalAlpha = resolvedAlpha;
  drawingContext.translate(centerX, centerY);

  drawingContext.save();
  drawingContext.beginPath();
  drawingContext.rect(-sleeveHalf, -sleeveHalf, sleeveSize, sleeveSize);
  drawingContext.clip();

  drawLibraryAlbumArtworkMotif(
    drawingContext,
    album,
    sleeveHalf,
    resolvedPalette,
    resolvedAppearance,
    coverCache,
  );

  const glareShift = delta * 26;
  drawingContext.fillStyle = isDark ? "rgba(255, 255, 255, 0.11)" : "rgba(255, 255, 255, 0.30)";
  drawingContext.beginPath();
  drawingContext.moveTo(-sleeveHalf * 0.95 + glareShift, -sleeveHalf);
  drawingContext.lineTo(-sleeveHalf * 0.38 + glareShift, -sleeveHalf);
  drawingContext.lineTo(-sleeveHalf * 1.12 + glareShift, sleeveHalf);
  drawingContext.lineTo(-sleeveHalf * 1.68 + glareShift, sleeveHalf);
  drawingContext.closePath();
  drawingContext.fill();
  drawingContext.restore();

  drawingContext.strokeStyle = isDark
    ? focusWeight > 0.5
      ? "rgba(245, 250, 255, 0.78)"
      : "rgba(215, 228, 246, 0.52)"
    : focusWeight > 0.5
      ? "rgba(46, 50, 56, 0.68)"
      : "rgba(46, 50, 56, 0.42)";
  drawingContext.lineWidth = focusWeight > 0.5 ? 1.6 : 1.15;
  drawingContext.strokeRect(-sleeveHalf + 0.5, -sleeveHalf + 0.5, sleeveSize - 1, sleeveSize - 1);

  if (focusWeight > 0.5) {
    const armLength = 12;
    drawingContext.strokeStyle = resolvedPalette.signalHighlight;
    drawingContext.lineWidth = 2.3;
    drawingContext.globalAlpha = resolvedAlpha * focusWeight;

    drawingContext.beginPath();
    drawingContext.moveTo(-sleeveHalf, -sleeveHalf + armLength);
    drawingContext.lineTo(-sleeveHalf, -sleeveHalf);
    drawingContext.lineTo(-sleeveHalf + armLength, -sleeveHalf);
    drawingContext.moveTo(sleeveHalf - armLength, sleeveHalf);
    drawingContext.lineTo(sleeveHalf, sleeveHalf);
    drawingContext.lineTo(sleeveHalf, sleeveHalf - armLength);
    drawingContext.stroke();
  }

  if (Boolean(album?.isFavorite)) {
    const badgeSize = Math.round(sleeveSize * 0.21);
    const badgeX = sleeveHalf - badgeSize - 5;
    const badgeY = -sleeveHalf + 5;
    drawingContext.save();
    drawingContext.globalAlpha = resolvedAlpha;
    traceChamferRect(drawingContext, badgeX, badgeY, badgeSize, badgeSize, 4);
    drawingContext.fillStyle = isDark ? "rgba(6, 7, 9, 0.82)" : "rgba(245, 246, 248, 0.90)";
    drawingContext.fill();
    drawingContext.strokeStyle = resolvedPalette.signalHighlight;
    drawingContext.lineWidth = 1.25;
    drawingContext.stroke();
    drawCrispHeartGlyph(
      drawingContext,
      badgeX + badgeSize / 2,
      badgeY + badgeSize / 2 + 0.5,
      badgeSize * 0.29,
      true,
      resolvedPalette.signalHighlight,
      resolvedPalette.signalHighlight,
      1.2,
    );
    drawingContext.restore();
  }

  drawingContext.restore();
}

function drawCrispHeartGlyph(
  drawingContext,
  centerX,
  centerY,
  radius,
  filled,
  fillColor,
  strokeColor,
  lineWidth = 1.4,
) {
  drawingContext.save();
  drawingContext.translate(centerX, centerY);
  drawingContext.beginPath();
  drawingContext.moveTo(0, radius * 0.78);
  drawingContext.lineTo(-radius * 0.86, -radius * 0.08);
  drawingContext.lineTo(-radius * 0.82, -radius * 0.58);
  drawingContext.lineTo(-radius * 0.34, -radius * 0.80);
  drawingContext.lineTo(0, -radius * 0.38);
  drawingContext.lineTo(radius * 0.34, -radius * 0.80);
  drawingContext.lineTo(radius * 0.82, -radius * 0.58);
  drawingContext.lineTo(radius * 0.86, -radius * 0.08);
  drawingContext.closePath();
  if (filled) {
    drawingContext.fillStyle = fillColor;
    drawingContext.fill();
  }
  drawingContext.strokeStyle = strokeColor;
  drawingContext.lineWidth = lineWidth;
  drawingContext.lineJoin = "miter";
  drawingContext.stroke();
  drawingContext.restore();
}

function drawCrateAlbumSleeveWithDisc(
  drawingContext,
  album,
  {
    centerX,
    centerY,
    sleeveSize,
    focusWeight,
    delta,
    baseAlpha,
    vinylRotationRadians,
    showPeekingDisc = false,
    isDiscPlaying = false,
    palette,
    appearance,
    coverCache,
  },
) {
  if (!album || baseAlpha <= CONTENT_VISIBILITY_THRESHOLD) {
    return;
  }

  const sleeveHalf = sleeveSize / 2;

  drawingContext.save();
  drawingContext.globalAlpha = baseAlpha;

  if (showPeekingDisc && focusWeight > CRATE_DISC_PEEK_THRESHOLD) {
    const peekWeight = easeOutCubic(
      normalizeRange(focusWeight, CRATE_DISC_PEEK_THRESHOLD, 1.0),
    );
    const discPeekX = centerX + lerp(sleeveHalf * 0.12, sleeveHalf * 0.42 + 22, peekWeight);
    const discRadius = sleeveHalf * 0.88;
    const discAlpha = baseAlpha * peekWeight * 0.96;
    const appliedRotationRadians = isDiscPlaying ? vinylRotationRadians : 0;
    drawLibraryVinylDisc(
      drawingContext,
      album,
      discPeekX,
      centerY,
      discRadius,
      appliedRotationRadians,
      palette,
      appearance,
      discAlpha,
      coverCache,
      { showCenterLabel: false },
    );
  }

  drawCrateAlbumSleeveBox(drawingContext, album, {
    centerX,
    centerY,
    sleeveSize,
    focusWeight,
    delta,
    baseAlpha,
    palette,
    appearance,
    coverCache,
  });

  drawingContext.restore();
}

function drawSortModeIconGlyph(drawingContext, sortMode, centerX, centerY, accentColor) {
  drawingContext.save();
  drawingContext.translate(centerX, centerY);

  // 3 stepped horizontal sort bars on the left
  drawingContext.strokeStyle = accentColor;
  drawingContext.lineWidth = 1.5;
  drawingContext.lineCap = "square";
  drawingContext.beginPath();
  drawingContext.moveTo(-8, -4.5);
  drawingContext.lineTo(1, -4.5);
  drawingContext.moveTo(-8, 0);
  drawingContext.lineTo(-1.5, 0);
  drawingContext.moveTo(-8, 4.5);
  drawingContext.lineTo(-4, 4.5);
  drawingContext.stroke();

  if (sortMode === "liked-first") {
    drawCrispHeartGlyph(
      drawingContext,
      5.5,
      2.5,
      4.2,
      true,
      accentColor,
      accentColor,
      1.1,
    );
  } else if (sortMode === "recent" || sortMode === "oldest") {
    const arrowX = 5.5;
    const isDown = sortMode === "recent";
    drawingContext.strokeStyle = accentColor;
    drawingContext.lineWidth = 1.6;
    drawingContext.lineCap = "butt";
    drawingContext.beginPath();
    drawingContext.moveTo(arrowX, -5.5);
    drawingContext.lineTo(arrowX, 5.5);
    if (isDown) {
      drawingContext.moveTo(arrowX - 3.2, 2.2);
      drawingContext.lineTo(arrowX, 5.8);
      drawingContext.lineTo(arrowX + 3.2, 2.2);
    } else {
      drawingContext.moveTo(arrowX - 3.2, -2.2);
      drawingContext.lineTo(arrowX, -5.8);
      drawingContext.lineTo(arrowX + 3.2, -2.2);
    }
    drawingContext.stroke();
  } else {
    drawingContext.font = `800 6.5px ${FONT_MONO}`;
    drawingContext.fillStyle = accentColor;
    drawingContext.textAlign = "center";
    drawingContext.textBaseline = "middle";
    drawingContext.fillText(sortMode === "reverse-alphabetical" ? "ZA" : "AZ", 5.5, 3.2);
  }

  drawingContext.restore();
}

function drawFloatingSortButton(
  drawingContext,
  sortMode,
  centerX,
  baseCenterY,
  clockSeconds,
  alpha,
  palette,
  appearance,
) {
  if (alpha <= CONTENT_VISIBILITY_THRESHOLD) {
    return;
  }

  const isDark = appearance === "dark";
  const safeClock = Number.isFinite(clockSeconds) ? clockSeconds : 0;
  const centerY = baseCenterY + Math.sin(safeClock * 2.4 + 1.2) * 1.1;
  const buttonRadius = CRATE_NOTE_BUTTON_RADIUS_PX;
  const isCustomSort = sortMode !== "alphabetical";
  const accentColor = isCustomSort ? palette.signalHighlight : palette.inkPrimary;

  drawingContext.save();
  drawingContext.globalAlpha = alpha;
  drawingContext.translate(centerX, centerY);

  drawingContext.beginPath();
  drawingContext.arc(0, 0, buttonRadius, 0, FULL_CIRCLE_RADIANS);
  drawingContext.fillStyle = isDark ? "rgba(18, 22, 30, 0.88)" : "rgba(255, 255, 255, 0.84)";
  drawingContext.fill();
  drawingContext.strokeStyle = isCustomSort ? palette.signalHighlight : palette.hairlineStrong;
  drawingContext.lineWidth = 1.4;
  drawingContext.stroke();

  drawSortModeIconGlyph(drawingContext, sortMode, 0, 0, accentColor);

  drawingContext.restore();
}

function drawCrateFloatingSortButton(
  drawingContext,
  sortMode,
  clockSeconds,
  alpha,
  palette,
  appearance,
) {
  drawFloatingSortButton(
    drawingContext,
    sortMode,
    CRATE_SORT_BUTTON_CENTER_X,
    CRATE_NOTE_BUTTON_CENTER_Y,
    clockSeconds,
    alpha,
    palette,
    appearance,
  );
}

function drawCrateFloatingNoteButton(
  drawingContext,
  clockSeconds,
  alpha,
  palette,
  appearance,
) {
  if (alpha <= CONTENT_VISIBILITY_THRESHOLD) {
    return;
  }

  const isDark = appearance === "dark";
  const safeClock = Number.isFinite(clockSeconds) ? clockSeconds : 0;
  const centerY = CRATE_NOTE_BUTTON_CENTER_Y + Math.sin(safeClock * 2.4) * 1.1;
  const buttonRadius = CRATE_NOTE_BUTTON_RADIUS_PX;

  drawingContext.save();
  drawingContext.globalAlpha = alpha;
  drawingContext.translate(CRATE_NOTE_BUTTON_CENTER_X, centerY);

  drawingContext.beginPath();
  drawingContext.arc(0, 0, buttonRadius, 0, FULL_CIRCLE_RADIANS);
  drawingContext.fillStyle = isDark ? "rgba(18, 22, 30, 0.88)" : "rgba(255, 255, 255, 0.84)";
  drawingContext.fill();
  drawingContext.strokeStyle = palette.signalHighlight;
  drawingContext.lineWidth = 1.4;
  drawingContext.stroke();

  // Crisp 2D beamed eighth-note vector icon
  drawingContext.fillStyle = palette.signalHighlight;
  drawingContext.fillRect(-5.5, 2.5, 4.5, 3.6);
  drawingContext.fillRect(1.5, 1.0, 4.5, 3.6);

  drawingContext.strokeStyle = palette.signalHighlight;
  drawingContext.lineWidth = 1.65;
  drawingContext.lineCap = "butt";
  drawingContext.beginPath();
  drawingContext.moveTo(-1.2, 4.0);
  drawingContext.lineTo(-1.2, -5.0);
  drawingContext.lineTo(5.8, -6.5);
  drawingContext.lineTo(5.8, 2.5);
  drawingContext.stroke();

  drawingContext.restore();
}

function drawAlbumTracklistCrystalScreen(
  drawingContext,
  sheetRevealProgress,
  alpha,
  palette,
  appearance,
) {
  if (alpha <= CONTENT_VISIBILITY_THRESHOLD || sheetRevealProgress <= CONTENT_VISIBILITY_THRESHOLD) {
    return;
  }

  const isDark = appearance === "dark";
  const revealEase = easeOutExpo(clamp(sheetRevealProgress, 0, 1));
  const sheetTopY = lerp(DISPLAY_DIAMETER_PX - 44, CRYSTAL_OVERLAY_TOP_Y, revealEase);
  const sheetHeight = DISPLAY_DIAMETER_PX - sheetTopY;

  drawingContext.save();
  drawingContext.globalAlpha = alpha;

  const glassGradient = drawingContext.createLinearGradient(0, sheetTopY, 0, DISPLAY_DIAMETER_PX);
  if (isDark) {
    glassGradient.addColorStop(0, "rgba(10, 13, 19, 0.80)");
    glassGradient.addColorStop(0.55, "rgba(8, 11, 16, 0.74)");
    glassGradient.addColorStop(1, "rgba(6, 9, 14, 0.84)");
  } else {
    glassGradient.addColorStop(0, "rgba(239, 243, 248, 0.84)");
    glassGradient.addColorStop(0.55, "rgba(235, 240, 246, 0.78)");
    glassGradient.addColorStop(1, "rgba(229, 234, 242, 0.86)");
  }
  drawingContext.fillStyle = glassGradient;
  drawingContext.fillRect(0, sheetTopY, DISPLAY_DIAMETER_PX, sheetHeight);

  /* Crisp diagonal crystal refraction sheen across the sliding glass sheet */
  drawingContext.save();
  drawingContext.beginPath();
  drawingContext.rect(0, sheetTopY, DISPLAY_DIAMETER_PX, sheetHeight);
  drawingContext.clip();
  drawingContext.fillStyle = isDark
    ? "rgba(225, 238, 255, 0.035)"
    : "rgba(255, 255, 255, 0.24)";
  drawingContext.beginPath();
  drawingContext.moveTo(42, sheetTopY);
  drawingContext.lineTo(136, sheetTopY);
  drawingContext.lineTo(54, DISPLAY_DIAMETER_PX);
  drawingContext.lineTo(-40, DISPLAY_DIAMETER_PX);
  drawingContext.closePath();
  drawingContext.fill();
  drawingContext.restore();

  drawingContext.strokeStyle = isDark
    ? "rgba(235, 245, 255, 0.24)"
    : "rgba(46, 50, 56, 0.22)";
  drawingContext.lineWidth = 1.25;
  drawingContext.beginPath();
  drawingContext.moveTo(48, sheetTopY + 0.5);
  drawingContext.lineTo(DISPLAY_DIAMETER_PX - 48, sheetTopY + 0.5);
  drawingContext.stroke();

  drawingContext.fillStyle = palette.hairlineStrong;
  drawingContext.fillRect(
    DISPLAY_CENTER_PX - TOP_HANDLE_HALF_WIDTH_PX,
    sheetTopY + 6,
    TOP_HANDLE_HALF_WIDTH_PX * 2,
    TOP_HANDLE_HEIGHT_PX,
  );

  drawingContext.restore();
}

function renderLibraryAlbumCrate(
  drawingContext,
  viewEntry,
  albums,
  libraryState,
  contentAlpha,
  vinylLaunchProgress,
  palette,
  appearance,
  coverCache,
  animationTimeSeconds = 0,
  playbackActive = false,
) {
  const scrollPosition = Number.isFinite(viewEntry.scrollPosition) ? viewEntry.scrollPosition : 0;
  const clockSeconds = Number.isFinite(libraryState.idleClockSeconds)
    ? libraryState.idleClockSeconds
    : animationTimeSeconds;
  const marqueeTimeSeconds = Number.isFinite(animationTimeSeconds)
    ? animationTimeSeconds
    : clockSeconds;
  const activeTrackId = libraryState.activeTrackId || null;
  const isDiscPlaying =
    typeof libraryState.isPlaying === "boolean" ? libraryState.isPlaying : Boolean(playbackActive);
  const launchPartEase = easeInOutCubic(normalizeRange(vinylLaunchProgress, 0.0, 0.50));
  const focusedIndex = clamp(Math.round(scrollPosition), 0, Math.max(0, albums.length - 1));

  const shelfAlpha = contentAlpha * (1 - launchPartEase);
  if (shelfAlpha > CONTENT_VISIBILITY_THRESHOLD) {
    drawingContext.save();
    drawingContext.globalAlpha = shelfAlpha * 0.45;
    drawingContext.strokeStyle = palette.hairlineStrong;
    drawingContext.lineWidth = 1;
    drawingContext.beginPath();
    drawingContext.moveTo(DISPLAY_CENTER_PX - 118, 224);
    drawingContext.lineTo(DISPLAY_CENTER_PX + 118, 224);
    drawingContext.stroke();
    drawingContext.restore();
  }

  const drawOrder = [];
  const crateStartIndex = Math.max(0, Math.floor(scrollPosition) - 3);
  const crateEndIndex = Math.min(albums.length - 1, Math.ceil(scrollPosition) + 3);
  for (let albumIndex = crateStartIndex; albumIndex <= crateEndIndex; albumIndex += 1) {
    drawOrder.push({ albumIndex, absDelta: Math.abs(albumIndex - scrollPosition) });
  }
  drawOrder.sort((firstEntry, secondEntry) => secondEntry.absDelta - firstEntry.absDelta);

  for (let orderIndex = 0; orderIndex < drawOrder.length; orderIndex += 1) {
    const albumIndex = drawOrder[orderIndex].albumIndex;
    const album = albums[albumIndex];
    const delta = albumIndex - scrollPosition;
    const absDelta = Math.abs(delta);
    if (absDelta > 1.85) {
      continue;
    }
    const focusWeight = clamp(1 - absDelta, 0, 1);
    const isFocusedTarget = albumIndex === focusedIndex;
    const isAlbumCurrentlyPlaying = Boolean(
      activeTrackId &&
        Array.isArray(album.tracks) &&
        album.tracks.some((trackEntry) => trackEntry.id === activeTrackId),
    );

    const launchSideSpread = !isFocusedTarget
      ? Math.sign(delta || 1) * 145 * launchPartEase
      : 0;
    const neighborFade = isFocusedTarget ? 1 : 1 - launchPartEase;
    const focusOpacity = lerp(CRATE_MIN_OPACITY, 1.0, easeInOutCubic(focusWeight));
    const outerEdgeFade = 1 - normalizeRange(absDelta, 1.15, 1.85);
    const baseAlpha = outerEdgeFade * focusOpacity * contentAlpha * neighborFade;
    if (baseAlpha <= CONTENT_VISIBILITY_THRESHOLD) {
      continue;
    }

    const sleeveSize = lerp(CRATE_MIN_SLEEVE_PX, CRATE_MAX_SLEEVE_PX, easeInOutCubic(focusWeight));
    const focusShiftLeft = focusWeight * 8;
    const centerX =
      DISPLAY_CENTER_PX -
      focusShiftLeft +
      delta * LIBRARY_CONSTANTS.CRATE_SPACING_PX +
      launchSideSpread;
    const bobY =
      isAlbumCurrentlyPlaying && isDiscPlaying
        ? Math.sin(clockSeconds * 1.7 + albumIndex * 1.4) *
          lerp(0.8, 2.2, focusWeight) *
          (1 - launchPartEase)
        : 0;
    const centerY =
      LIBRARY_CONSTANTS.CRATE_CENTER_Y + Math.min(absDelta, 1.5) * 6 + bobY;

    if (isFocusedTarget && vinylLaunchProgress > CONTENT_VISIBILITY_THRESHOLD) {
      continue;
    }

    drawCrateAlbumSleeveWithDisc(drawingContext, album, {
      centerX,
      centerY,
      sleeveSize,
      focusWeight,
      delta,
      baseAlpha,
      vinylRotationRadians: libraryState.vinylRotationRadians,
      showPeekingDisc: isAlbumCurrentlyPlaying,
      isDiscPlaying: isAlbumCurrentlyPlaying && isDiscPlaying,
      palette,
      appearance,
      coverCache,
    });
  }

  const bottomExit = easeInOutCubic(normalizeRange(vinylLaunchProgress, 0.0, 0.42));
  const bottomAlpha = contentAlpha * (1 - bottomExit);
  if (bottomAlpha > CONTENT_VISIBILITY_THRESHOLD && albums.length > 0) {
    const focusedAlbum = albums[focusedIndex];
    const offsetFromFocused = scrollPosition - focusedIndex;
    const textCrossfade = clamp(1 - Math.abs(offsetFromFocused) * 1.8, 0, 1);
    const trackCount = Array.isArray(focusedAlbum.tracks) ? focusedAlbum.tracks.length : 0;

    drawingContext.save();
    drawingContext.translate(0, lerp(0, 24, bottomExit));

    drawingContext.save();
    drawingContext.globalAlpha = bottomAlpha * textCrossfade;
    drawingContext.translate(-offsetFromFocused * 24, 0);

    drawingContext.font = `700 19px ${FONT_DISPLAY}`;
    drawingContext.fillStyle = palette.inkPrimary;
    drawingContext.textAlign = "center";
    drawingContext.textBaseline = "middle";
    drawMarqueeCanvasText(
      drawingContext,
      focusedAlbum.title,
      DISPLAY_CENTER_PX,
      246,
      224,
      marqueeTimeSeconds,
    );

    drawingContext.font = `600 9.5px ${FONT_MONO}`;
    drawingContext.fillStyle = palette.inkSecondary;
    const metaLine = `${String(focusedAlbum.artist || "").toUpperCase()} \u00b7 ${formatIntegerCount(trackCount)} TRACKS`;
    drawMarqueeCanvasText(
      drawingContext,
      metaLine,
      DISPLAY_CENTER_PX,
      268,
      214,
      marqueeTimeSeconds,
    );
    drawingContext.restore();

    const crateBars = computeWindowedCrateBars(scrollPosition, albums.length, CRATE_BAR_LIMIT);
    let totalStripWidth = 0;
    for (let barIndex = 0; barIndex < crateBars.length; barIndex += 1) {
      totalStripWidth += crateBars[barIndex].width;
      if (barIndex < crateBars.length - 1) {
        totalStripWidth += CRATE_BAR_GAP_PX;
      }
    }

    let cursorX = DISPLAY_CENTER_PX - totalStripWidth / 2;
    for (let barIndex = 0; barIndex < crateBars.length; barIndex += 1) {
      const bar = crateBars[barIndex];
      drawingContext.fillStyle =
        bar.focus > 0.5 ? palette.signalHighlight : palette.hairlineStrong;
      drawingContext.globalAlpha = bottomAlpha * lerp(0.55, 1.0, bar.focus);
      drawingContext.fillRect(cursorX, CRATE_BAR_Y, bar.width, CRATE_BAR_HEIGHT_PX);
      cursorX += bar.width + CRATE_BAR_GAP_PX;
    }

    drawCrateFloatingSortButton(
      drawingContext,
      libraryState.albumSortMode || "alphabetical",
      clockSeconds,
      bottomAlpha,
      palette,
      appearance,
    );

    drawCrateFloatingNoteButton(
      drawingContext,
      clockSeconds,
      bottomAlpha,
      palette,
      appearance,
    );

    drawingContext.restore();
  }
}

function renderFavoritesInlineEmptyState(
  drawingContext,
  tabMode,
  clockSeconds,
  contentAlpha,
  palette,
  appearance,
) {
  if (contentAlpha <= CONTENT_VISIBILITY_THRESHOLD) {
    return;
  }
  const isAlbumsTab = tabMode !== "songs";
  const isDark = appearance === "dark";
  const safeClock = Number.isFinite(clockSeconds) ? clockSeconds : 0;
  const breatheScale = 1 + Math.sin(safeClock * 2.1) * 0.04;

  drawingContext.save();
  drawingContext.globalAlpha = contentAlpha;

  const emblemCenterY = 152;
  drawingContext.save();
  drawingContext.translate(DISPLAY_CENTER_PX, emblemCenterY);
  drawingContext.scale(breatheScale, breatheScale);

  drawingContext.beginPath();
  drawingContext.arc(0, 0, 32, 0, FULL_CIRCLE_RADIANS);
  drawingContext.fillStyle = isDark
    ? "rgba(210, 230, 255, 0.06)"
    : "rgba(255, 255, 255, 0.56)";
  drawingContext.fill();
  drawingContext.strokeStyle = isDark
    ? "rgba(235, 245, 255, 0.22)"
    : "rgba(46, 50, 56, 0.22)";
  drawingContext.lineWidth = 1.25;
  drawingContext.stroke();

  drawingContext.beginPath();
  drawingContext.arc(0, 0, 40, -Math.PI * 0.68, Math.PI * 0.12);
  drawingContext.strokeStyle = palette.signalHighlight;
  drawingContext.lineWidth = 1.6;
  drawingContext.lineCap = "round";
  drawingContext.stroke();
  drawingContext.restore();

  drawLibraryLivingIcon(
    drawingContext,
    isAlbumsTab ? "albums" : "favorites",
    DISPLAY_CENTER_PX,
    emblemCenterY,
    16,
    safeClock,
    false,
    palette,
    appearance,
  );

  drawingContext.font = `700 18px ${FONT_DISPLAY}`;
  drawingContext.fillStyle = palette.inkPrimary;
  drawingContext.textAlign = "center";
  drawingContext.textBaseline = "middle";
  drawingContext.fillText(
    isAlbumsTab ? "No Favorite Albums" : "No Favorite Songs",
    DISPLAY_CENTER_PX,
    214,
  );

  drawingContext.font = `600 9.5px ${FONT_MONO}`;
  drawingContext.fillStyle = palette.inkSecondary;
  drawingContext.fillText(
    isAlbumsTab ? "HOLD AN ALBUM SLEEVE TO LIKE" : "HEART A TRACK WHILE LISTENING",
    DISPLAY_CENTER_PX,
    236,
  );

  drawingContext.restore();
}

function renderLibraryEmptyState(
  drawingContext,
  viewEntry,
  clockSeconds,
  contentAlpha,
  palette,
  appearance,
) {
  if (contentAlpha <= CONTENT_VISIBILITY_THRESHOLD) {
    return;
  }
  const isDark = appearance === "dark";
  const iconId = viewEntry.emptyIcon || "favorites";
  const safeClock = Number.isFinite(clockSeconds) ? clockSeconds : 0;
  const breatheScale = 1 + Math.sin(safeClock * 2.1) * 0.045;

  drawingContext.save();
  drawingContext.globalAlpha = contentAlpha;

  const emblemCenterY = 128;
  drawingContext.save();
  drawingContext.translate(DISPLAY_CENTER_PX, emblemCenterY);
  drawingContext.scale(breatheScale, breatheScale);

  drawingContext.beginPath();
  drawingContext.arc(0, 0, 34, 0, FULL_CIRCLE_RADIANS);
  drawingContext.fillStyle = isDark
    ? "rgba(210, 230, 255, 0.06)"
    : "rgba(255, 255, 255, 0.56)";
  drawingContext.fill();
  drawingContext.strokeStyle = isDark
    ? "rgba(235, 245, 255, 0.22)"
    : "rgba(46, 50, 56, 0.22)";
  drawingContext.lineWidth = 1.25;
  drawingContext.stroke();

  drawingContext.beginPath();
  drawingContext.arc(0, 0, 42, -Math.PI * 0.68, Math.PI * 0.12);
  drawingContext.strokeStyle = palette.signalHighlight;
  drawingContext.lineWidth = 1.6;
  drawingContext.lineCap = "round";
  drawingContext.stroke();
  drawingContext.restore();

  drawLibraryLivingIcon(
    drawingContext,
    iconId,
    DISPLAY_CENTER_PX,
    emblemCenterY,
    17,
    safeClock,
    false,
    palette,
    appearance,
  );

  drawingContext.font = `700 20px ${FONT_DISPLAY}`;
  drawingContext.fillStyle = palette.inkPrimary;
  drawingContext.textAlign = "center";
  drawingContext.textBaseline = "middle";
  drawingContext.fillText(viewEntry.emptyTitle || "No Favorites Yet", DISPLAY_CENTER_PX, 192);

  drawingContext.font = `600 9.5px ${FONT_MONO}`;
  drawingContext.fillStyle = palette.inkSecondary;
  drawingContext.fillText(
    viewEntry.emptyMessage || "HEART A TRACK WHILE LISTENING",
    DISPLAY_CENTER_PX,
    214,
  );

  const buttonWidth = 166;
  const buttonHeight = 36;
  const buttonX = DISPLAY_CENTER_PX - buttonWidth / 2;
  const buttonY = 242;

  traceChamferRect(drawingContext, buttonX, buttonY, buttonWidth, buttonHeight, 8);
  drawingContext.fillStyle = palette.signalHighlight;
  drawingContext.fill();

  drawingContext.font = `800 10.5px ${FONT_MONO}`;
  drawingContext.fillStyle = palette.signalHighlightInk;
  drawingContext.fillText("BACK TO LIBRARY", DISPLAY_CENTER_PX, buttonY + buttonHeight / 2 + 0.5);

  drawingContext.restore();
}

function drawLibraryRimArcFeedback(
  drawingContext,
  libraryState,
  currentView,
  palette,
  appearance,
) {
  const arcAlpha = Math.max(
    Number(libraryState.arcFeedbackAlpha ?? 0),
    Number(libraryState.letterSearchHudAlpha ?? 0),
  );
  if (arcAlpha <= CONTENT_VISIBILITY_THRESHOLD) {
    return;
  }
  const isDark = appearance === "dark";
  const letterIndex = resolveLetterEntriesFromState(currentView, libraryState);
  const supportsLetterSearch = letterIndex.length > 0;

  drawingContext.save();
  drawingContext.globalAlpha = arcAlpha;

  if (libraryState.letterSearchActive || (libraryState.letterSearchHudAlpha ?? 0) > 0.15) {
    const sliceCount = LIBRARY_CONSTANTS.RIM_SLICE_COUNT;
    const slicePitch = FULL_CIRCLE_RADIANS / sliceCount;
    const pullLevels = Array.isArray(libraryState.rimSlabPullLevels)
      ? libraryState.rimSlabPullLevels
      : [];
    const drawOrder = [];
    for (let sliceIndex = 0; sliceIndex < sliceCount; sliceIndex += 1) {
      drawOrder.push({ sliceIndex, pull: pullLevels[sliceIndex] || 0 });
    }
    drawOrder.sort((firstSlice, secondSlice) => firstSlice.pull - secondSlice.pull);

    for (let orderIndex = 0; orderIndex < drawOrder.length; orderIndex += 1) {
      const sliceIndex = drawOrder[orderIndex].sliceIndex;
      const pull = easeOutCubic(drawOrder[orderIndex].pull);
      const sliceCenter = TOP_ANGLE_RADIANS + (sliceIndex + 0.5) * slicePitch;
      const angularSpan = slicePitch * lerp(0.56, 0.72, pull);
      const startAngle = sliceCenter - angularSpan / 2;
      const endAngle = sliceCenter + angularSpan / 2;

      if (pull < 0.04) {
        drawingContext.save();
        drawingContext.globalAlpha = arcAlpha * 0.42;
        drawingContext.beginPath();
        drawingContext.arc(DISPLAY_CENTER_PX, DISPLAY_CENTER_PX, 171.5, startAngle, endAngle);
        drawingContext.strokeStyle = palette.hairlineStrong;
        drawingContext.lineWidth = 1.25;
        drawingContext.lineCap = "butt";
        drawingContext.stroke();
        drawingContext.restore();
      } else {
        const outerRadius = lerp(172, 173, pull);
        const innerRadius = lerp(170.5, 161, pull);

        traceCurvedSlabWedge(drawingContext, innerRadius, outerRadius, startAngle, endAngle);
        drawingContext.fillStyle = isDark
          ? `rgba(210, 230, 255, ${lerp(0.08, 0.19, pull).toFixed(3)})`
          : `rgba(255, 255, 255, ${lerp(0.36, 0.72, pull).toFixed(3)})`;
        drawingContext.fill();

        if (pull > 0.65) {
          drawingContext.save();
          traceCurvedSlabWedge(drawingContext, innerRadius, outerRadius, startAngle, endAngle);
          drawingContext.clip();
          const midpointRadius = (innerRadius + outerRadius) / 2;
          const glintX = DISPLAY_CENTER_PX + Math.cos(sliceCenter) * midpointRadius;
          const glintY = DISPLAY_CENTER_PX + Math.sin(sliceCenter) * midpointRadius;
          drawingContext.translate(glintX, glintY);
          drawingContext.rotate(sliceCenter + 0.55);
          drawingContext.fillStyle = isDark
            ? "rgba(255, 255, 255, 0.18)"
            : "rgba(255, 255, 255, 0.56)";
          drawingContext.fillRect(-12, -1.6, 24, 3.2);
          drawingContext.restore();
        }

        traceCurvedSlabWedge(drawingContext, innerRadius, outerRadius, startAngle, endAngle);
        drawingContext.strokeStyle = isDark
          ? `rgba(235, 245, 255, ${lerp(0.24, 0.58, pull).toFixed(3)})`
          : `rgba(46, 50, 56, ${lerp(0.22, 0.45, pull).toFixed(3)})`;
        drawingContext.lineWidth = 1;
        drawingContext.stroke();

        if (pull > 0.50) {
          drawingContext.beginPath();
          drawingContext.arc(
            DISPLAY_CENTER_PX,
            DISPLAY_CENTER_PX,
            innerRadius + 0.6,
            startAngle + 0.02,
            endAngle - 0.02,
          );
          drawingContext.strokeStyle = palette.signalHighlight;
          drawingContext.lineWidth = 1.6;
          drawingContext.lineCap = "butt";
          drawingContext.stroke();
        }
      }
    }

    drawingContext.restore();
    return;
  }

  drawingContext.beginPath();
  drawingContext.arc(
    DISPLAY_CENTER_PX,
    DISPLAY_CENTER_PX,
    SCROLL_ARC_RADIUS_PX,
    0,
    FULL_CIRCLE_RADIANS,
  );
  drawingContext.strokeStyle = palette.hairline;
  drawingContext.lineWidth = 2;
  drawingContext.stroke();

  const clampedSweep = clamp(
    Number(libraryState.arcSignedSweepRadians ?? 0),
    -LIBRARY_CONSTANTS.LETTER_SEARCH_THRESHOLD_RADIANS,
    LIBRARY_CONSTANTS.LETTER_SEARCH_THRESHOLD_RADIANS,
  );
  const startAngle = Number(libraryState.arcStartAngle ?? 0);
  const endAngle = startAngle + clampedSweep;
  const counterClockwise = clampedSweep < 0;

  if (Math.abs(clampedSweep) > 0.02) {
    drawingContext.beginPath();
    drawingContext.arc(
      DISPLAY_CENTER_PX,
      DISPLAY_CENTER_PX,
      SCROLL_ARC_RADIUS_PX,
      startAngle,
      endAngle,
      counterClockwise,
    );
    drawingContext.strokeStyle = palette.signalHighlight;
    drawingContext.lineWidth = 3.5;
    drawingContext.lineCap = "round";
    drawingContext.stroke();
  }

  if (supportsLetterSearch) {
    const targetAngle = startAngle + (clampedSweep >= 0 ? Math.PI : -Math.PI);
    const markerX = DISPLAY_CENTER_PX + Math.cos(targetAngle) * SCROLL_ARC_RADIUS_PX;
    const markerY = DISPLAY_CENTER_PX + Math.sin(targetAngle) * SCROLL_ARC_RADIUS_PX;
    drawingContext.beginPath();
    drawingContext.arc(markerX, markerY, 3, 0, FULL_CIRCLE_RADIANS);
    drawingContext.fillStyle = palette.inkSecondary;
    drawingContext.fill();
  }

  const currentAngle = Number(libraryState.arcCurrentAngle ?? startAngle);
  const beadX = DISPLAY_CENTER_PX + Math.cos(currentAngle) * SCROLL_ARC_RADIUS_PX;
  const beadY = DISPLAY_CENTER_PX + Math.sin(currentAngle) * SCROLL_ARC_RADIUS_PX;
  drawingContext.beginPath();
  drawingContext.arc(beadX, beadY, 4.5, 0, FULL_CIRCLE_RADIANS);
  drawingContext.fillStyle = palette.signalHighlight;
  drawingContext.fill();

  drawingContext.restore();
}

function drawSeekHeartBadge(drawingContext, centerX, centerY, radius, fillColor) {
  drawingContext.save();
  drawingContext.translate(centerX, centerY);
  drawingContext.beginPath();
  drawingContext.moveTo(0, radius * 0.78);
  drawingContext.lineTo(-radius * 0.88, -radius * 0.08);
  drawingContext.lineTo(-radius * 0.84, -radius * 0.58);
  drawingContext.lineTo(-radius * 0.35, -radius * 0.80);
  drawingContext.lineTo(0, -radius * 0.40);
  drawingContext.lineTo(radius * 0.35, -radius * 0.80);
  drawingContext.lineTo(radius * 0.84, -radius * 0.58);
  drawingContext.lineTo(radius * 0.88, -radius * 0.08);
  drawingContext.closePath();
  drawingContext.fillStyle = fillColor;
  drawingContext.fill();
  drawingContext.restore();
}

function renderLibraryLetterSearchHud(
  drawingContext,
  libraryState,
  currentView,
  palette,
  appearance,
) {
  const hudAlpha = Number(libraryState.letterSearchHudAlpha ?? 0);
  if (hudAlpha <= CONTENT_VISIBILITY_THRESHOLD) {
    return;
  }
  const letters = resolveLetterEntriesFromState(currentView, libraryState);
  if (letters.length === 0) {
    return;
  }
  const isDark = appearance === "dark";
  const activeIndex = clamp(Number(libraryState.letterSearchIndex ?? 0), 0, letters.length - 1);
  const activeEntry = letters[activeIndex];
  const previousEntry = activeIndex > 0 ? letters[activeIndex - 1] : null;
  const nextEntry = activeIndex < letters.length - 1 ? letters[activeIndex + 1] : null;
  const ease = easeOutExpo(hudAlpha);
  const isDateAnchor =
    (typeof activeEntry.kind === "string" && activeEntry.kind.startsWith("date-")) ||
    Boolean(activeEntry.subLabel) ||
    String(activeEntry.letter || "").length > 1;

  drawingContext.save();

  drawingContext.fillStyle = isDark ? "rgba(6, 7, 9, 0.64)" : "rgba(230, 232, 235, 0.70)";
  drawingContext.globalAlpha = hudAlpha;
  drawingContext.fillRect(0, 0, DISPLAY_DIAMETER_PX, DISPLAY_DIAMETER_PX);

  const lensRadius = lerp(50, 62, ease);
  drawingContext.save();
  drawingContext.globalAlpha = ease;
  drawingContext.beginPath();
  drawingContext.arc(DISPLAY_CENTER_PX, DISPLAY_CENTER_PX - 6, lensRadius, 0, FULL_CIRCLE_RADIANS);
  drawingContext.fillStyle = isDark ? "rgba(18, 21, 27, 0.92)" : "rgba(248, 249, 251, 0.94)";
  drawingContext.fill();
  drawingContext.strokeStyle = palette.signalHighlight;
  drawingContext.lineWidth = 1.6;
  drawingContext.stroke();

  drawingContext.textAlign = "center";
  drawingContext.textBaseline = "middle";

  if (isDateAnchor) {
    drawingContext.font = `700 11px ${FONT_MONO}`;
    drawingContext.fillStyle = palette.inkSecondary;
    if (previousEntry) {
      drawingContext.globalAlpha = ease * 0.78;
      drawingContext.textAlign = "right";
      drawingContext.fillText(
        previousEntry.shortLabel || previousEntry.letter,
        DISPLAY_CENTER_PX - 74,
        DISPLAY_CENTER_PX - 6,
      );
    }
    if (nextEntry) {
      drawingContext.globalAlpha = ease * 0.78;
      drawingContext.textAlign = "left";
      drawingContext.fillText(
        nextEntry.shortLabel || nextEntry.letter,
        DISPLAY_CENTER_PX + 74,
        DISPLAY_CENTER_PX - 6,
      );
    }

    drawingContext.textAlign = "center";
    if (activeEntry.subLabel) {
      const primaryFontSize = String(activeEntry.letter || "").length <= 2 ? 34 : 30;
      drawingContext.globalAlpha = ease;
      drawingContext.font = `800 ${primaryFontSize}px ${FONT_DISPLAY}`;
      drawingContext.fillStyle = palette.signalHighlight;
      drawingContext.fillText(activeEntry.letter, DISPLAY_CENTER_PX, DISPLAY_CENTER_PX - 8);

      drawingContext.globalAlpha = ease * 0.85;
      drawingContext.font = `700 10.5px ${FONT_MONO}`;
      drawingContext.fillStyle = palette.inkPrimary;
      drawingContext.fillText(activeEntry.subLabel, DISPLAY_CENTER_PX, DISPLAY_CENTER_PX + 18);
    } else {
      drawingContext.globalAlpha = ease;
      drawingContext.font = `800 30px ${FONT_DISPLAY}`;
      drawingContext.fillStyle = palette.signalHighlight;
      drawingContext.fillText(activeEntry.letter, DISPLAY_CENTER_PX, DISPLAY_CENTER_PX - 2);
    }
  } else {
    drawingContext.font = `700 14px ${FONT_DISPLAY}`;
    drawingContext.fillStyle = palette.inkSecondary;
    if (previousEntry) {
      drawingContext.globalAlpha = ease * 0.65;
      drawingContext.fillText(previousEntry.letter, DISPLAY_CENTER_PX - 40, DISPLAY_CENTER_PX - 6);
      if (previousEntry.isFavorite) {
        drawSeekHeartBadge(
          drawingContext,
          DISPLAY_CENTER_PX - 40,
          DISPLAY_CENTER_PX - 21,
          3.8,
          palette.signalHighlight,
        );
      }
    }
    if (nextEntry) {
      drawingContext.globalAlpha = ease * 0.65;
      drawingContext.fillText(nextEntry.letter, DISPLAY_CENTER_PX + 40, DISPLAY_CENTER_PX - 6);
      if (nextEntry.isFavorite) {
        drawSeekHeartBadge(
          drawingContext,
          DISPLAY_CENTER_PX + 40,
          DISPLAY_CENTER_PX - 21,
          3.8,
          palette.signalHighlight,
        );
      }
    }

    drawingContext.globalAlpha = ease;
    drawingContext.font = `800 46px ${FONT_DISPLAY}`;
    drawingContext.fillStyle = palette.signalHighlight;
    drawingContext.fillText(activeEntry.letter, DISPLAY_CENTER_PX, DISPLAY_CENTER_PX - 4);
    if (activeEntry.isFavorite) {
      drawSeekHeartBadge(
        drawingContext,
        DISPLAY_CENTER_PX,
        DISPLAY_CENTER_PX + 32,
        6,
        palette.signalHighlight,
      );
    }
  }

  drawingContext.globalAlpha = ease;
  drawingContext.font = `700 10px ${FONT_MONO}`;
  drawingContext.fillStyle = palette.inkPrimary;
  drawMarqueeCanvasText(
    drawingContext,
    String(activeEntry.label || "").toUpperCase(),
    DISPLAY_CENTER_PX,
    266,
    196,
    Number(libraryState.idleClockSeconds ?? 0),
  );

  drawingContext.font = `600 8.5px ${FONT_MONO}`;
  drawingContext.fillStyle = palette.inkSecondary;
  drawingContext.fillText("6% ARC STEP \u00b7 CW NEXT \u00b7 CCW PREV", DISPLAY_CENTER_PX, 286);

  drawingContext.restore();
  drawingContext.restore();
}

function renderLibraryPlaylistSheet(
  drawingContext,
  libraryState,
  palette,
  appearance,
  animationTimeSeconds = 0,
) {
  const sheetProgress = Number(libraryState.playlistSheetProgress ?? 0);
  if (sheetProgress <= CONTENT_VISIBILITY_THRESHOLD) {
    return;
  }
  const sheetPlaylists = Array.isArray(libraryState.playlists) ? libraryState.playlists : [];
  const targetPlaylist = sheetPlaylists[libraryState.playlistSheetIndex] || sheetPlaylists[0];
  if (!targetPlaylist) {
    return;
  }
  const isDark = appearance === "dark";
  const ease = easeOutExpo(sheetProgress);

  drawingContext.save();

  drawingContext.fillStyle = isDark ? "rgba(6, 7, 9, 0.78)" : "rgba(230, 232, 235, 0.82)";
  drawingContext.globalAlpha = sheetProgress;
  drawingContext.fillRect(0, 0, DISPLAY_DIAMETER_PX, DISPLAY_DIAMETER_PX);

  const cardX = DISPLAY_CENTER_PX - SHEET_CARD_WIDTH_PX / 2;
  const cardY = lerp(112, 86, ease);

  drawingContext.globalAlpha = ease;
  drawingContext.fillStyle = isDark ? "rgba(18, 21, 27, 0.96)" : "rgba(246, 247, 249, 0.96)";
  drawingContext.fillRect(cardX, cardY, SHEET_CARD_WIDTH_PX, SHEET_CARD_HEIGHT_PX);
  drawingContext.strokeStyle = isDark
    ? "rgba(245, 250, 255, 0.34)"
    : "rgba(46, 50, 56, 0.36)";
  drawingContext.lineWidth = 1.25;
  drawingContext.strokeRect(
    cardX + 0.5,
    cardY + 0.5,
    SHEET_CARD_WIDTH_PX - 1,
    SHEET_CARD_HEIGHT_PX - 1,
  );

  drawingContext.font = `700 9px ${FONT_MONO}`;
  drawingContext.fillStyle = palette.inkSecondary;
  drawingContext.textAlign = "center";
  drawingContext.textBaseline = "middle";
  drawingContext.fillText("PLAYLIST ACTIONS", DISPLAY_CENTER_PX, cardY + 18);

  drawingContext.font = `700 15.5px ${FONT_DISPLAY}`;
  drawingContext.fillStyle = palette.inkPrimary;
  drawMarqueeCanvasText(
    drawingContext,
    targetPlaylist.name,
    DISPLAY_CENTER_PX,
    cardY + 40,
    SHEET_CARD_WIDTH_PX - 28,
    animationTimeSeconds,
  );

  const buttonX = cardX + 14;
  const buttonWidth = SHEET_CARD_WIDTH_PX - 28;
  const buttonHeight = 34;

  const playY = cardY + 62;
  drawingContext.fillStyle = palette.signalHighlight;
  drawingContext.fillRect(buttonX, playY, buttonWidth, buttonHeight);
  drawingContext.font = `800 10.5px ${FONT_MONO}`;
  drawingContext.fillStyle = palette.signalHighlightInk;
  drawingContext.fillText("PLAY MIX", DISPLAY_CENTER_PX, playY + buttonHeight / 2 + 0.5);

  const shuffleY = cardY + 104;
  drawingContext.fillStyle = palette.surface;
  drawingContext.fillRect(buttonX, shuffleY, buttonWidth, buttonHeight);
  drawingContext.strokeStyle = palette.hairlineStrong;
  drawingContext.lineWidth = 1.1;
  drawingContext.strokeRect(buttonX + 0.5, shuffleY + 0.5, buttonWidth - 1, buttonHeight - 1);
  drawingContext.fillStyle = palette.inkPrimary;
  drawingContext.fillText("SHUFFLE MIX", DISPLAY_CENTER_PX, shuffleY + buttonHeight / 2 + 0.5);

  const deleteY = cardY + 146;
  const holdProgress = clamp(Number(libraryState.deleteHoldProgress ?? 0), 0, 1);

  drawingContext.fillStyle = palette.signalActiveSoft;
  drawingContext.fillRect(buttonX, deleteY, buttonWidth, buttonHeight);

  if (holdProgress > ALPHA_VISIBILITY_THRESHOLD) {
    drawingContext.fillStyle = palette.signalActive;
    drawingContext.fillRect(buttonX, deleteY, buttonWidth * holdProgress, buttonHeight);
  }

  drawingContext.strokeStyle = palette.signalActive;
  drawingContext.lineWidth = 1.3;
  drawingContext.strokeRect(buttonX + 0.5, deleteY + 0.5, buttonWidth - 1, buttonHeight - 1);

  drawingContext.fillStyle = holdProgress > 0.48 ? "#FFFFFF" : palette.signalActive;
  const deleteLabel =
    holdProgress > 0.04
      ? `HOLDING \u00b7 ${Math.round(holdProgress * PERCENT_SCALE)}%`
      : "HOLD TO DELETE";
  drawingContext.fillText(deleteLabel, DISPLAY_CENTER_PX, deleteY + buttonHeight / 2 + 0.5);

  if (holdProgress > CONTENT_VISIBILITY_THRESHOLD) {
    drawingContext.strokeStyle = palette.signalActive;
    drawingContext.lineWidth = 4;
    drawingContext.lineCap = "butt";
    drawingContext.beginPath();
    drawingContext.arc(
      DISPLAY_CENTER_PX,
      DISPLAY_CENTER_PX,
      BACK_SWIPE_ARC_RADIUS_PX,
      TOP_ANGLE_RADIANS,
      TOP_ANGLE_RADIANS + holdProgress * FULL_CIRCLE_RADIANS,
    );
    drawingContext.stroke();
  }

  drawingContext.font = `600 9px ${FONT_MONO}`;
  drawingContext.fillStyle = palette.inkSecondary;
  drawingContext.fillText(
    "SWIPE LEFT EDGE TO CANCEL",
    DISPLAY_CENTER_PX,
    cardY + SHEET_CARD_HEIGHT_PX + 20,
  );

  drawingContext.restore();
}

function renderLibrarySortToast(drawingContext, libraryState, palette, appearance) {
  const toastAlpha = clamp(Number(libraryState?.sortToastAlpha ?? 0), 0, 1);
  const toastLabel = libraryState?.sortToastLabel;
  if (toastAlpha <= CONTENT_VISIBILITY_THRESHOLD || !toastLabel) {
    return;
  }
  const isDark = appearance === "dark";
  const pillWidth = 168;
  const pillHeight = 26;
  const pillLeft = DISPLAY_CENTER_PX - pillWidth / 2;
  const pillTop = 139;

  drawingContext.save();
  drawingContext.globalAlpha = toastAlpha;
  traceChamferRect(drawingContext, pillLeft, pillTop, pillWidth, pillHeight, 6);
  drawingContext.fillStyle = isDark ? "rgba(6, 7, 9, 0.92)" : "rgba(46, 50, 56, 0.92)";
  drawingContext.fill();
  drawingContext.strokeStyle = palette.signalHighlight;
  drawingContext.lineWidth = 1.25;
  drawingContext.stroke();

  drawingContext.font = `800 9px ${FONT_MONO}`;
  drawingContext.fillStyle = palette.signalHighlight;
  drawingContext.textAlign = "center";
  drawingContext.textBaseline = "middle";
  drawingContext.fillText(`SORT \u00b7 ${toastLabel}`, DISPLAY_CENTER_PX, pillTop + pillHeight / 2 + 0.5);
  drawingContext.restore();
}

function resolveQuickActionsViewModel(libraryState) {
  const unifiedProgress = Number(libraryState?.quickActionsProgress ?? 0);
  const albumLegacyProgress = Number(libraryState?.albumQuickActionsProgress ?? 0);
  const artistLegacyProgress = Number(libraryState?.artistQuickActionsProgress ?? 0);
  const progress = clamp(
    Math.max(unifiedProgress, albumLegacyProgress, artistLegacyProgress),
    0,
    1,
  );
  if (progress <= CONTENT_VISIBILITY_THRESHOLD) {
    return null;
  }

  const kind =
    libraryState?.quickActionsKind ||
    (artistLegacyProgress > albumLegacyProgress ? "artist" : "album");
  const targetId =
    libraryState?.quickActionsTargetId ||
    (kind === "artist"
      ? libraryState?.artistQuickActionsArtistId
      : libraryState?.albumQuickActionsAlbumId);
  const catalogAlbums = Array.isArray(libraryState?.catalog?.albums)
    ? libraryState.catalog.albums
    : [];

  if (kind === "artist") {
    const catalogArtists = Array.isArray(libraryState?.catalog?.artists)
      ? libraryState.catalog.artists
      : [];
    const targetArtist =
      catalogArtists.find((artistEntry) => artistEntry.id === targetId) || null;
    if (!targetArtist) {
      return null;
    }
    const artistAlbumIds = Array.isArray(targetArtist.albumIds) ? targetArtist.albumIds : [];
    const artistAlbums = artistAlbumIds
      .map((albumId) => catalogAlbums.find((albumEntry) => albumEntry.id === albumId))
      .filter(Boolean);
    const albumCount = artistAlbums.length > 0 ? artistAlbums.length : artistAlbumIds.length;
    const trackCount = artistAlbums.reduce(
      (total, albumEntry) =>
        total + (Array.isArray(albumEntry.tracks) ? albumEntry.tracks.length : 0),
      0,
    );
    const albumWord = albumCount === 1 ? "Album" : "Albums";
    const trackWord = trackCount === 1 ? "Track" : "Tracks";
    const isLiked = Boolean(targetArtist.isFavorite);
    return {
      progress,
      headerText: "ARTIST ACTIONS",
      titleText: targetArtist.name || "Unknown Artist",
      subtitleText: `${formatIntegerCount(albumCount)} ${albumWord} \u00b7 ${formatIntegerCount(trackCount)} ${trackWord}`,
      isFavorited: isLiked,
      buttonLabel: isLiked ? "LIKED" : "LIKE",
    };
  }

  const targetAlbum =
    catalogAlbums.find((albumEntry) => albumEntry.id === targetId) || null;
  if (!targetAlbum) {
    return null;
  }
  const trackCount = Array.isArray(targetAlbum.tracks) ? targetAlbum.tracks.length : 0;
  const isFavorited = Boolean(targetAlbum.isFavorite);
  return {
    progress,
    headerText: "ALBUM ACTIONS",
    titleText: targetAlbum.title || "Unknown Album",
    subtitleText: `${targetAlbum.artist || "Unknown Artist"} \u00b7 ${formatIntegerCount(trackCount)} Tracks`,
    isFavorited,
    buttonLabel: isFavorited ? "FAVORITED" : "FAVORITE",
  };
}

function renderLibraryQuickActions(
  drawingContext,
  libraryState,
  palette,
  appearance,
  animationTimeSeconds = 0,
) {
  const viewModel = resolveQuickActionsViewModel(libraryState);
  if (!viewModel) {
    return;
  }

  const { progress, headerText, titleText, subtitleText, isFavorited, buttonLabel } = viewModel;
  const isDark = appearance === "dark";
  const ease = easeOutExpo(progress);
  const isPressed = Boolean(libraryState.pressedQuickAction ?? libraryState.pressedAlbumQuickAction);
  const buttonScale = isPressed ? 0.93 : 1.0;

  drawingContext.save();

  drawingContext.fillStyle = isDark ? "rgba(6, 7, 9, 0.78)" : "rgba(14, 17, 22, 0.76)";
  drawingContext.globalAlpha = progress;
  drawingContext.fillRect(0, 0, DISPLAY_DIAMETER_PX, DISPLAY_DIAMETER_PX);

  const headerYOffset = lerp(-12, 0, ease);
  drawingContext.save();
  drawingContext.translate(0, headerYOffset);
  drawingContext.globalAlpha = ease;
  drawingContext.textAlign = "center";
  drawingContext.textBaseline = "middle";

  drawingContext.font = `700 9px ${FONT_MONO}`;
  drawingContext.fillStyle = palette.signalHighlight;
  drawingContext.fillText(headerText, DISPLAY_CENTER_PX, 68);

  drawingContext.font = `800 18px ${FONT_DISPLAY}`;
  drawingContext.fillStyle = "#F5F6F8";
  drawMarqueeCanvasText(
    drawingContext,
    titleText,
    DISPLAY_CENTER_PX,
    94,
    224,
    animationTimeSeconds,
  );

  drawingContext.font = `600 10.5px ${FONT_DISPLAY}`;
  drawingContext.fillStyle = "rgba(245, 246, 248, 0.72)";
  drawMarqueeCanvasText(
    drawingContext,
    subtitleText,
    DISPLAY_CENTER_PX,
    114,
    244,
    animationTimeSeconds,
  );

  drawingContext.strokeStyle = "rgba(245, 250, 255, 0.16)";
  drawingContext.lineWidth = 1;
  drawingContext.beginPath();
  drawingContext.moveTo(116, 132);
  drawingContext.lineTo(244, 132);
  drawingContext.stroke();
  drawingContext.restore();

  const clusterYOffset = lerp(14, 0, ease);
  drawingContext.save();
  drawingContext.translate(0, clusterYOffset);
  drawingContext.globalAlpha = ease;

  drawingContext.save();
  drawingContext.translate(DISPLAY_CENTER_PX, ALBUM_QUICK_ACTION_BUTTON_CENTER_Y);
  drawingContext.scale(buttonScale, buttonScale);

  drawingContext.beginPath();
  drawingContext.arc(0, 0, ALBUM_QUICK_ACTION_BUTTON_RADIUS_PX, 0, FULL_CIRCLE_RADIANS);
  drawingContext.fillStyle = isFavorited
    ? palette.signalHighlight
    : isPressed
      ? "rgba(245, 250, 255, 0.24)"
      : "rgba(22, 26, 34, 0.90)";
  drawingContext.fill();

  drawingContext.strokeStyle =
    isFavorited || isPressed ? palette.signalHighlight : "rgba(245, 250, 255, 0.34)";
  drawingContext.lineWidth = isFavorited || isPressed ? 2.0 : 1.5;
  drawingContext.stroke();

  drawCrispHeartGlyph(
    drawingContext,
    0,
    0,
    14,
    isFavorited,
    isFavorited ? palette.signalHighlightInk : "transparent",
    isFavorited ? palette.signalHighlightInk : "#F5F6F8",
    1.8,
  );

  drawingContext.restore();

  drawingContext.font = `700 9px ${FONT_MONO}`;
  drawingContext.fillStyle = isFavorited ? palette.signalHighlight : "rgba(245, 246, 248, 0.80)";
  drawingContext.textAlign = "center";
  drawingContext.textBaseline = "middle";
  drawingContext.fillText(
    buttonLabel,
    DISPLAY_CENTER_PX,
    ALBUM_QUICK_ACTION_BUTTON_CENTER_Y + ALBUM_QUICK_ACTION_BUTTON_RADIUS_PX + 16,
  );

  drawingContext.font = `600 8.5px ${FONT_MONO}`;
  drawingContext.fillStyle = "rgba(245, 246, 248, 0.48)";
  drawingContext.fillText("TAP OUTSIDE TO CLOSE", DISPLAY_CENTER_PX, 316);

  drawingContext.restore();
  drawingContext.restore();
}

function renderLibrarySortWheel(drawingContext, libraryState, palette, appearance) {
  const progress = clamp(Number(libraryState?.sortWheelProgress ?? 0), 0, 1);
  if (progress <= CONTENT_VISIBILITY_THRESHOLD) {
    return;
  }

  const isDark = appearance === "dark";
  const ease = easeOutExpo(progress);
  const isArtistsWheel =
    libraryState?.sortWheelScope === "artists" ||
    (!libraryState?.sortWheelScope && libraryState?.currentView?.kind === "artists");
  const anchorX = isArtistsWheel
    ? LIBRARY_CONSTANTS.ARTISTS_SORT_BUTTON_X
    : CRATE_SORT_BUTTON_CENTER_X;
  const anchorY = isArtistsWheel
    ? LIBRARY_CONSTANTS.ARTISTS_SORT_BUTTON_Y
    : CRATE_NOTE_BUTTON_CENTER_Y;
  const hoveredMode = libraryState?.sortWheelHoveredMode || null;
  const activeSortMode = isArtistsWheel
    ? libraryState?.artistSortMode || "alphabetical"
    : libraryState?.albumSortMode || "alphabetical";
  const slots = computeSortWheelSlots(anchorX, anchorY);

  drawingContext.save();

  // Translucent scrim behind the radial half-wheel
  drawingContext.fillStyle = isDark ? "rgba(6, 7, 9, 0.58)" : "rgba(14, 17, 22, 0.52)";
  drawingContext.globalAlpha = progress;
  drawingContext.fillRect(0, 0, DISPLAY_DIAMETER_PX, DISPLAY_DIAMETER_PX);

  // Semi-circular crystal arc band
  const innerRadius = 52 * ease;
  const outerRadius = 98 * ease;
  const arcStart = -Math.PI * 0.96;
  const arcEnd = -Math.PI * 0.04;

  drawingContext.globalAlpha = ease;
  drawingContext.beginPath();
  drawingContext.arc(anchorX, anchorY, outerRadius, arcStart, arcEnd, false);
  drawingContext.arc(anchorX, anchorY, innerRadius, arcEnd, arcStart, true);
  drawingContext.closePath();
  drawingContext.fillStyle = isDark ? "rgba(18, 22, 30, 0.90)" : "rgba(246, 247, 249, 0.92)";
  drawingContext.fill();
  drawingContext.strokeStyle = isDark ? "rgba(245, 250, 255, 0.24)" : "rgba(46, 50, 56, 0.22)";
  drawingContext.lineWidth = 1.25;
  drawingContext.stroke();

  // Highlighted sector wedge & guide ray for currently hovered mode
  const hoveredSlot = slots.find((slot) => slot.mode === hoveredMode) || null;
  if (hoveredSlot) {
    const sectorSpan = 0.25;
    drawingContext.beginPath();
    drawingContext.arc(
      anchorX,
      anchorY,
      outerRadius,
      hoveredSlot.angleRadians - sectorSpan,
      hoveredSlot.angleRadians + sectorSpan,
      false,
    );
    drawingContext.arc(
      anchorX,
      anchorY,
      innerRadius,
      hoveredSlot.angleRadians + sectorSpan,
      hoveredSlot.angleRadians - sectorSpan,
      true,
    );
    drawingContext.closePath();
    drawingContext.fillStyle = palette.signalHighlightSoft;
    drawingContext.fill();

    const targetX = lerp(anchorX, hoveredSlot.centerX, ease);
    const targetY = lerp(anchorY, hoveredSlot.centerY, ease);
    drawingContext.strokeStyle = palette.signalHighlight;
    drawingContext.lineWidth = 1.5;
    drawingContext.beginPath();
    drawingContext.moveTo(anchorX, anchorY);
    drawingContext.lineTo(targetX, targetY);
    drawingContext.stroke();
  }

  // 4 blooming sort-mode medallions
  for (let slotIndex = 0; slotIndex < slots.length; slotIndex += 1) {
    const slot = slots[slotIndex];
    const drawX = lerp(anchorX, slot.centerX, ease);
    const drawY = lerp(anchorY, slot.centerY, ease);
    const isHovered = slot.mode === hoveredMode;
    const isCurrent = slot.mode === activeSortMode;
    const medallionScale = isHovered ? 1.16 : 1.0;

    drawingContext.save();
    drawingContext.translate(drawX, drawY);
    drawingContext.scale(medallionScale, medallionScale);

    drawingContext.beginPath();
    drawingContext.arc(0, 0, slot.radius, 0, FULL_CIRCLE_RADIANS);
    drawingContext.fillStyle = isHovered
      ? palette.signalHighlight
      : isCurrent
        ? isDark
          ? "rgba(192, 254, 5, 0.16)"
          : "rgba(232, 90, 36, 0.16)"
        : isDark
          ? "rgba(18, 22, 30, 0.94)"
          : "rgba(246, 247, 249, 0.94)";
    drawingContext.fill();

    drawingContext.strokeStyle =
      isHovered || isCurrent ? palette.signalHighlight : palette.hairlineStrong;
    drawingContext.lineWidth = isHovered || isCurrent ? 1.6 : 1.2;
    drawingContext.stroke();

    const iconInk = isHovered
      ? palette.signalHighlightInk
      : isCurrent
        ? palette.signalHighlight
        : palette.inkPrimary;
    drawSortModeIconGlyph(drawingContext, slot.mode, 0, 0, iconInk);
    drawingContext.restore();
  }

  // Live mode readout pill above the wheel apex
  const readoutLabel = hoveredMode
    ? ALBUM_SORT_MODE_LABELS[hoveredMode]?.toastLabel || "SORT"
    : "DRAG TO SORT";
  const pillWidth = 136;
  const pillHeight = 24;
  const pillLeft = anchorX - pillWidth / 2;
  const pillTop = 200;
  traceChamferRect(drawingContext, pillLeft, pillTop, pillWidth, pillHeight, 5);
  drawingContext.fillStyle = isDark ? "rgba(6, 7, 9, 0.92)" : "rgba(46, 50, 56, 0.92)";
  drawingContext.fill();
  drawingContext.strokeStyle = hoveredMode ? palette.signalHighlight : palette.hairlineStrong;
  drawingContext.lineWidth = 1.2;
  drawingContext.stroke();

  drawingContext.font = `800 9px ${FONT_MONO}`;
  drawingContext.fillStyle = hoveredMode ? palette.signalHighlight : "rgba(245, 246, 248, 0.78)";
  drawingContext.textAlign = "center";
  drawingContext.textBaseline = "middle";
  drawingContext.fillText(readoutLabel, anchorX, pillTop + pillHeight / 2 + 0.5);

  // Anchored Sort hub at (152, 325) so the finger stays grounded
  drawingContext.beginPath();
  drawingContext.arc(anchorX, anchorY, CRATE_NOTE_BUTTON_RADIUS_PX, 0, FULL_CIRCLE_RADIANS);
  drawingContext.fillStyle = isDark ? "rgba(18, 22, 30, 0.96)" : "rgba(255, 255, 255, 0.96)";
  drawingContext.fill();
  drawingContext.strokeStyle = palette.signalHighlight;
  drawingContext.lineWidth = 1.8;
  drawingContext.stroke();
  drawSortModeIconGlyph(drawingContext, activeSortMode, anchorX, anchorY, palette.signalHighlight);

  drawingContext.restore();
}

function drawMiniatureAlbumsIconOverlay(
  drawingContext,
  centerX,
  centerY,
  sleeveSize,
  alpha,
  palette,
  appearance,
) {
  if (alpha <= CONTENT_VISIBILITY_THRESHOLD || sleeveSize <= 2) {
    return;
  }
  const isDark = appearance === "dark";
  const accentColor = palette.signalHighlight;
  const sleeveHalf = sleeveSize / 2;

  drawingContext.save();
  drawingContext.globalAlpha = alpha;
  drawingContext.translate(centerX, centerY);

  drawingContext.save();
  drawingContext.beginPath();
  drawingContext.rect(-sleeveHalf, -sleeveHalf, sleeveSize, sleeveSize);
  drawingContext.clip();

  drawingContext.fillStyle = isDark ? "#141923" : "#272C35";
  drawingContext.fillRect(-sleeveHalf, -sleeveHalf, sleeveSize, sleeveSize);

  drawingContext.fillStyle = accentColor;
  drawingContext.beginPath();
  drawingContext.moveTo(-sleeveHalf * 0.14, -sleeveHalf);
  drawingContext.lineTo(sleeveHalf * 0.56, -sleeveHalf);
  drawingContext.lineTo(-sleeveHalf * 0.24, sleeveHalf);
  drawingContext.lineTo(-sleeveHalf * 0.68, sleeveHalf);
  drawingContext.closePath();
  drawingContext.fill();

  drawingContext.strokeStyle = "#F5F6F8";
  drawingContext.lineWidth = Math.max(1.5, sleeveHalf * 0.18);
  drawingContext.beginPath();
  drawingContext.arc(0, 0, sleeveHalf * 0.54, -Math.PI * 0.22, Math.PI * 0.50);
  drawingContext.stroke();

  drawingContext.fillStyle = "rgba(255, 255, 255, 0.16)";
  drawingContext.beginPath();
  drawingContext.moveTo(-sleeveHalf, -sleeveHalf);
  drawingContext.lineTo(-sleeveHalf + sleeveSize * 0.45, -sleeveHalf);
  drawingContext.lineTo(-sleeveHalf + sleeveSize * 0.08, sleeveHalf);
  drawingContext.lineTo(-sleeveHalf - sleeveSize * 0.35, sleeveHalf);
  drawingContext.closePath();
  drawingContext.fill();
  drawingContext.restore();

  drawingContext.strokeStyle = palette.inkPrimary;
  drawingContext.lineWidth = 1.25;
  drawingContext.strokeRect(-sleeveHalf + 0.5, -sleeveHalf + 0.5, sleeveSize - 1, sleeveSize - 1);

  const cornerArm = Math.max(4, sleeveHalf * 0.36);
  drawingContext.strokeStyle = accentColor;
  drawingContext.lineWidth = 1.7;
  drawingContext.beginPath();
  drawingContext.moveTo(-sleeveHalf, -sleeveHalf + cornerArm);
  drawingContext.lineTo(-sleeveHalf, -sleeveHalf);
  drawingContext.lineTo(-sleeveHalf + cornerArm, -sleeveHalf);
  drawingContext.stroke();

  drawingContext.restore();
}

function drawMiniaturePeekingVinylDisc(
  drawingContext,
  centerX,
  centerY,
  discRadius,
  clockSeconds,
  alpha,
  palette,
  appearance,
) {
  if (alpha <= CONTENT_VISIBILITY_THRESHOLD || discRadius <= 2) {
    return;
  }
  const isDark = appearance === "dark";
  const accentColor = palette.signalHighlight;
  const spin = clockSeconds * 1.25;

  drawingContext.save();
  drawingContext.globalAlpha = alpha;
  drawingContext.beginPath();
  drawingContext.arc(centerX, centerY, discRadius, 0, FULL_CIRCLE_RADIANS);
  drawingContext.fillStyle = isDark ? "#13171F" : "#242830";
  drawingContext.fill();
  drawingContext.strokeStyle = isDark
    ? "rgba(245, 246, 248, 0.34)"
    : "rgba(46, 50, 56, 0.55)";
  drawingContext.lineWidth = 1.1;
  drawingContext.stroke();

  drawingContext.beginPath();
  drawingContext.arc(centerX, centerY, discRadius * 0.66, spin, spin + Math.PI * 0.75);
  drawingContext.strokeStyle = isDark
    ? "rgba(245, 246, 248, 0.36)"
    : "rgba(245, 246, 248, 0.48)";
  drawingContext.lineWidth = 1.1;
  drawingContext.stroke();

  drawingContext.beginPath();
  drawingContext.arc(centerX, centerY, discRadius * 0.30, 0, FULL_CIRCLE_RADIANS);
  drawingContext.fillStyle = accentColor;
  drawingContext.fill();

  drawingContext.beginPath();
  drawingContext.arc(centerX, centerY, discRadius * 0.11, 0, FULL_CIRCLE_RADIANS);
  drawingContext.fillStyle = "#060709";
  drawingContext.fill();

  drawingContext.beginPath();
  drawingContext.arc(centerX, centerY, discRadius + 0.8, -Math.PI * 0.42, Math.PI * 0.42);
  drawingContext.strokeStyle = accentColor;
  drawingContext.lineWidth = 1.4;
  drawingContext.stroke();
  drawingContext.restore();
}

function renderLibraryHubAlbumsTransition(
  drawingContext,
  libraryState,
  {
    crateFactor,
    palette,
    appearance,
    coverCache,
    animationTimeSeconds,
    playbackIsPlaying,
  },
) {
  const clampedFactor = clamp(crateFactor, 0, 1);
  const clockSeconds = Number.isFinite(libraryState.idleClockSeconds)
    ? libraryState.idleClockSeconds
    : animationTimeSeconds;
  const marqueeTimeSeconds = Number.isFinite(animationTimeSeconds)
    ? animationTimeSeconds
    : clockSeconds;
  const hubScroll = Number.isFinite(libraryState.albumsTransitionHubScroll)
    ? Math.max(0, libraryState.albumsTransitionHubScroll)
    : 0;
  const crateAlbums = Array.isArray(libraryState.catalog?.albums)
    ? libraryState.catalog.albums
    : [];
  const crateScroll = Number.isFinite(libraryState.albumsTransitionCrateScroll)
    ? clamp(libraryState.albumsTransitionCrateScroll, 0, Math.max(0, crateAlbums.length - 1))
    : 0;
  const matchedHeroIndex = libraryState.albumsTransitionAlbumId
    ? crateAlbums.findIndex((albumEntry) => albumEntry.id === libraryState.albumsTransitionAlbumId)
    : -1;
  const heroAlbumIndex =
    matchedHeroIndex >= 0
      ? matchedHeroIndex
      : clamp(Math.round(crateScroll), 0, Math.max(0, crateAlbums.length - 1));
  const heroAlbum = crateAlbums[heroAlbumIndex] || null;

  const hubRows = buildHubSectionRowsFromCounts(libraryState.counts || libraryState.catalog?.counts);
  const totalTrackCount = Number(
    libraryState.counts?.tracks ?? libraryState.catalog?.allTracks?.length ?? 0,
  );
  const hubSubtitle =
    totalTrackCount === 0
      ? "0 TRACKS \u00b7 EMPTY COLLECTION"
      : `2-UP CRYSTAL HUB \u00b7 ${formatIntegerCount(totalTrackCount)} TRACKS`;
  const albumsSubtitle =
    crateAlbums.length === 0
      ? "VINYL CRATE \u00b7 00 / 00"
      : `VINYL CRATE \u00b7 ${String(heroAlbumIndex + 1).padStart(2, "0")} / ${String(crateAlbums.length).padStart(2, "0")}`;

  /* 1. Anchored spatial perspective floor */
  drawLibraryPerspectiveFloor(
    drawingContext,
    lerp(hubScroll, crateScroll, easeInOutCubic(clampedFactor)),
    1,
    palette,
    appearance,
  );

  /* 2. Clean non-colliding header crossfade (Library <-> Albums) */
  const hubHeaderExit = normalizeRange(clampedFactor, 0.0, ALBUMS_BLOOM_HUB_HEADER_END);
  const hubHeaderAlpha = 1 - easeOutCubic(hubHeaderExit);
  if (hubHeaderAlpha > CONTENT_VISIBILITY_THRESHOLD) {
    drawLibraryPeripheralChrome(
      drawingContext,
      "Library",
      hubSubtitle,
      lerp(0, -ALBUMS_BLOOM_HEADER_DRIFT_Y_PX, hubHeaderExit),
      hubHeaderAlpha,
      palette,
      appearance,
      marqueeTimeSeconds,
    );
  }

  const crateHeaderEnter = normalizeRange(
    clampedFactor,
    ALBUMS_BLOOM_CRATE_HEADER_START,
    ALBUMS_BLOOM_CRATE_HEADER_END,
  );
  const crateHeaderAlpha = easeOutCubic(crateHeaderEnter);
  if (crateHeaderAlpha > CONTENT_VISIBILITY_THRESHOLD) {
    drawLibraryPeripheralChrome(
      drawingContext,
      "Albums",
      albumsSubtitle,
      lerp(ALBUMS_BLOOM_HEADER_DRIFT_Y_PX, 0, easeOutExpo(crateHeaderEnter)),
      crateHeaderAlpha,
      palette,
      appearance,
      marqueeTimeSeconds,
    );
  }

  /* 3. Surrounding Hub tiles parting outward & Albums tile shell dissolving in place */
  const albumsTileRect = computeLibraryHubTileRect(HUB_ALBUMS_TILE_INDEX, hubScroll);
  const hubPartFactor = easeInOutCubic(
    normalizeRange(clampedFactor, 0.0, ALBUMS_BLOOM_HUB_PART_END),
  );
  const albumsShellAlpha =
    1 - easeOutCubic(normalizeRange(clampedFactor, 0.0, ALBUMS_BLOOM_TILE_SHELL_END));

  if (1 - hubPartFactor > CONTENT_VISIBILITY_THRESHOLD || albumsShellAlpha > CONTENT_VISIBILITY_THRESHOLD) {
    drawingContext.save();
    drawingContext.beginPath();
    drawingContext.rect(
      0,
      HEADER_CLIP_TOP_Y,
      DISPLAY_DIAMETER_PX,
      DISPLAY_DIAMETER_PX - HEADER_CLIP_TOP_Y,
    );
    drawingContext.clip();

    for (let tileIndex = 0; tileIndex < hubRows.length; tileIndex += 1) {
      const tileRect = computeLibraryHubTileRect(tileIndex, hubScroll);
      if (tileRect.alpha <= CONTENT_VISIBILITY_THRESHOLD) {
        continue;
      }

      if (tileIndex === HUB_ALBUMS_TILE_INDEX) {
        if (albumsShellAlpha > CONTENT_VISIBILITY_THRESHOLD) {
          drawSingleHubCrystalTile(drawingContext, hubRows[tileIndex], {
            centerX: tileRect.centerX,
            centerY: tileRect.centerY,
            width: tileRect.width,
            height: tileRect.height,
            isPeeking: tileRect.isPeeking,
            tileAlpha: tileRect.alpha * albumsShellAlpha,
            pressIntensity: 0,
            clockSeconds,
            showIcon: false,
            palette,
            appearance,
          });
        }
        continue;
      }

      const neighborTileAlpha = tileRect.alpha * (1 - hubPartFactor);
      if (neighborTileAlpha <= CONTENT_VISIBILITY_THRESHOLD) {
        continue;
      }
      const horizontalSign = tileIndex % 2 === 0 ? -1 : 1;
      const verticalSign = tileRect.centerY < albumsTileRect.centerY ? -1 : 1;
      drawSingleHubCrystalTile(drawingContext, hubRows[tileIndex], {
        centerX: tileRect.centerX + horizontalSign * ALBUMS_BLOOM_HUB_PART_X_PX * hubPartFactor,
        centerY: tileRect.centerY + verticalSign * ALBUMS_BLOOM_HUB_PART_Y_PX * hubPartFactor,
        width: tileRect.width,
        height: tileRect.height,
        isPeeking: tileRect.isPeeking,
        tileAlpha: neighborTileAlpha,
        pressIntensity: 0,
        clockSeconds,
        showIcon: true,
        palette,
        appearance,
      });
    }

    drawingContext.restore();
  }

  const hubArcAlpha = 1 - easeOutCubic(normalizeRange(clampedFactor, 0.0, ALBUMS_BLOOM_ARC_EXIT_END));
  if (hubArcAlpha > CONTENT_VISIBILITY_THRESHOLD) {
    const hubScrollRowSpan = Math.ceil(hubRows.length / 2) + 1;
    drawLibraryScrollArc(drawingContext, hubScroll, hubScrollRowSpan, hubArcAlpha * 0.85, palette);
  }

  /* 4. Albums Roulette shelf line, opacity-tiered neighbor covers & bottom metadata deal-in */
  const shelfEase = easeOutCubic(
    normalizeRange(clampedFactor, ALBUMS_BLOOM_SHELF_START, ALBUMS_BLOOM_SHELF_END),
  );
  if (shelfEase > CONTENT_VISIBILITY_THRESHOLD) {
    const shelfHalfSpan = lerp(24, 118, shelfEase);
    drawingContext.save();
    drawingContext.globalAlpha = shelfEase * 0.45;
    drawingContext.strokeStyle = palette.hairlineStrong;
    drawingContext.lineWidth = 1;
    drawingContext.beginPath();
    drawingContext.moveTo(DISPLAY_CENTER_PX - shelfHalfSpan, 224);
    drawingContext.lineTo(DISPLAY_CENTER_PX + shelfHalfSpan, 224);
    drawingContext.stroke();
    drawingContext.restore();
  }

  const neighborDealEase = easeOutExpo(
    normalizeRange(clampedFactor, ALBUMS_BLOOM_NEIGHBOR_DEAL_START, ALBUMS_BLOOM_NEIGHBOR_DEAL_END),
  );
  const neighborAlphaEase = easeOutCubic(
    normalizeRange(clampedFactor, ALBUMS_BLOOM_NEIGHBOR_ALPHA_START, ALBUMS_BLOOM_NEIGHBOR_ALPHA_END),
  );
  if (neighborAlphaEase > CONTENT_VISIBILITY_THRESHOLD && crateAlbums.length > 1) {
    const neighborEntries = [];
    const crateStartIndex = Math.max(0, Math.floor(crateScroll) - 3);
    const crateEndIndex = Math.min(crateAlbums.length - 1, Math.ceil(crateScroll) + 3);
    for (let albumIndex = crateStartIndex; albumIndex <= crateEndIndex; albumIndex += 1) {
      if (albumIndex === heroAlbumIndex) {
        continue;
      }
      neighborEntries.push({ albumIndex, absDelta: Math.abs(albumIndex - crateScroll) });
    }
    neighborEntries.sort((firstEntry, secondEntry) => secondEntry.absDelta - firstEntry.absDelta);

    for (let entryIndex = 0; entryIndex < neighborEntries.length; entryIndex += 1) {
      const albumIndex = neighborEntries[entryIndex].albumIndex;
      const album = crateAlbums[albumIndex];
      const delta = albumIndex - crateScroll;
      const absDelta = Math.abs(delta);
      if (absDelta > 1.85) {
        continue;
      }
      const focusWeight = clamp(1 - absDelta, 0, 1);
      const focusOpacity = lerp(CRATE_MIN_OPACITY, 1.0, easeInOutCubic(focusWeight));
      const outerEdgeFade = 1 - normalizeRange(absDelta, 1.15, 1.85);
      const neighborAlpha = outerEdgeFade * focusOpacity * neighborAlphaEase;
      if (neighborAlpha <= CONTENT_VISIBILITY_THRESHOLD) {
        continue;
      }
      const neighborScale = lerp(0.86, 1.0, neighborDealEase);
      const sleeveSize =
        lerp(CRATE_MIN_SLEEVE_PX, CRATE_MAX_SLEEVE_PX, easeInOutCubic(focusWeight)) * neighborScale;
      const focusShiftLeft = focusWeight * 8;
      const dealSpreadX =
        Math.sign(delta || 1) * ALBUMS_BLOOM_NEIGHBOR_SPREAD_PX * (1 - neighborDealEase);
      const centerX =
        DISPLAY_CENTER_PX -
        focusShiftLeft +
        delta * LIBRARY_CONSTANTS.CRATE_SPACING_PX +
        dealSpreadX;
      const centerY = LIBRARY_CONSTANTS.CRATE_CENTER_Y + Math.min(absDelta, 1.5) * 6;

      drawCrateAlbumSleeveBox(drawingContext, album, {
        centerX,
        centerY,
        sleeveSize,
        focusWeight,
        delta,
        baseAlpha: neighborAlpha,
        palette,
        appearance,
        coverCache,
      });
    }
  }

  const bottomReveal = easeOutCubic(
    normalizeRange(clampedFactor, ALBUMS_BLOOM_BOTTOM_START, ALBUMS_BLOOM_BOTTOM_END),
  );
  if (bottomReveal > CONTENT_VISIBILITY_THRESHOLD && heroAlbum) {
    const bottomRiseY = lerp(
      ALBUMS_BLOOM_BOTTOM_RISE_Y_PX,
      0,
      easeOutExpo(normalizeRange(clampedFactor, ALBUMS_BLOOM_BOTTOM_START, 1.0)),
    );
    const trackCount = Array.isArray(heroAlbum.tracks) ? heroAlbum.tracks.length : 0;

    drawingContext.save();
    drawingContext.translate(0, bottomRiseY);

    drawingContext.save();
    drawingContext.globalAlpha = bottomReveal;
    drawingContext.font = `700 19px ${FONT_DISPLAY}`;
    drawingContext.fillStyle = palette.inkPrimary;
    drawingContext.textAlign = "center";
    drawingContext.textBaseline = "middle";
    drawMarqueeCanvasText(
      drawingContext,
      heroAlbum.title,
      DISPLAY_CENTER_PX,
      246,
      224,
      marqueeTimeSeconds,
    );

    drawingContext.font = `600 9.5px ${FONT_MONO}`;
    drawingContext.fillStyle = palette.inkSecondary;
    const metaLine = `${String(heroAlbum.artist || "").toUpperCase()} \u00b7 ${formatIntegerCount(trackCount)} TRACKS`;
    drawMarqueeCanvasText(
      drawingContext,
      metaLine,
      DISPLAY_CENTER_PX,
      268,
      214,
      marqueeTimeSeconds,
    );
    drawingContext.restore();

    const crateBars = computeWindowedCrateBars(crateScroll, crateAlbums.length, CRATE_BAR_LIMIT);
    let totalStripWidth = 0;
    for (let barIndex = 0; barIndex < crateBars.length; barIndex += 1) {
      totalStripWidth += crateBars[barIndex].width;
      if (barIndex < crateBars.length - 1) {
        totalStripWidth += CRATE_BAR_GAP_PX;
      }
    }
    let cursorX = DISPLAY_CENTER_PX - totalStripWidth / 2;
    for (let barIndex = 0; barIndex < crateBars.length; barIndex += 1) {
      const bar = crateBars[barIndex];
      drawingContext.fillStyle =
        bar.focus > 0.5 ? palette.signalHighlight : palette.hairlineStrong;
      drawingContext.globalAlpha = bottomReveal * lerp(0.55, 1.0, bar.focus);
      drawingContext.fillRect(cursorX, CRATE_BAR_Y, bar.width, CRATE_BAR_HEIGHT_PX);
      cursorX += bar.width + CRATE_BAR_GAP_PX;
    }

    drawCrateFloatingNoteButton(drawingContext, clockSeconds, bottomReveal, palette, appearance);
    drawingContext.restore();
  }

  /* 5. Shared-element 1:1 Hero Sleeve Bloom (Albums tile icon <-> centered crate sleeve) */
  const iconRadius = albumsTileRect.isPeeking
    ? HUB_TILE_PEEK_ICON_RADIUS_PX
    : HUB_TILE_FULL_ICON_RADIUS_PX;
  const startSleeveHalf = iconRadius * HUB_ALBUMS_ICON_SLEEVE_RATIO;
  const startSleeveSize = startSleeveHalf * 2;
  const startCenterX = albumsTileRect.centerX - iconRadius * HUB_ALBUMS_ICON_SHIFT_X_RATIO;
  const startCenterY = albumsTileRect.centerY - HUB_TILE_ICON_OFFSET_Y_PX;

  const endCenterX = CRATE_SLEEVE_START_X_PX;
  const endCenterY = LIBRARY_CONSTANTS.CRATE_CENTER_Y;
  const endSleeveSize = CRATE_MAX_SLEEVE_PX;

  const glideEase = easeInOutCubic(clampedFactor);
  const sizeEase = easeOutCubic(clampedFactor);
  const heroCenterX = lerp(startCenterX, endCenterX, glideEase);
  const heroCenterY = lerp(startCenterY, endCenterY, glideEase);
  const heroSleeveSize = lerp(startSleeveSize, endSleeveSize, sizeEase);
  const heroAlpha = lerp(
    albumsTileRect.alpha,
    1.0,
    easeOutCubic(normalizeRange(clampedFactor, 0.0, ALBUMS_BLOOM_HERO_ALPHA_END)),
  );

  const activeTrackId = libraryState.activeTrackId || null;
  const isHeroAlbumPlaying = Boolean(
    heroAlbum &&
      activeTrackId &&
      Array.isArray(heroAlbum.tracks) &&
      heroAlbum.tracks.some((trackEntry) => trackEntry.id === activeTrackId),
  );
  const startDiscOffsetX = startSleeveHalf * HUB_ALBUMS_ICON_DISC_OFFSET_RATIO;
  const startDiscRadius = startSleeveHalf * HUB_ALBUMS_ICON_DISC_OFFSET_RATIO;

  if (isHeroAlbumPlaying) {
    const endDiscOffsetX = (endSleeveSize / 2) * 0.42 + 22;
    const endDiscRadius = (endSleeveSize / 2) * 0.88;
    const discCenterX = heroCenterX + lerp(startDiscOffsetX, endDiscOffsetX, glideEase);
    const discRadius = lerp(startDiscRadius, endDiscRadius, glideEase);
    const discRotation = playbackIsPlaying ? libraryState.vinylRotationRadians : 0;
    drawLibraryVinylDisc(
      drawingContext,
      heroAlbum,
      discCenterX,
      heroCenterY,
      discRadius,
      discRotation,
      palette,
      appearance,
      heroAlpha * 0.96,
      coverCache,
      { showCenterLabel: false },
    );
  } else {
    const tuckProgress = easeInOutCubic(
      normalizeRange(clampedFactor, 0.0, ALBUMS_BLOOM_DISC_TUCK_END),
    );
    const miniDiscAlpha = heroAlpha * (1 - tuckProgress);
    if (miniDiscAlpha > CONTENT_VISIBILITY_THRESHOLD) {
      const discCenterX = heroCenterX + lerp(startDiscOffsetX, 0, tuckProgress);
      drawMiniaturePeekingVinylDisc(
        drawingContext,
        discCenterX,
        heroCenterY,
        startDiscRadius,
        clockSeconds,
        miniDiscAlpha,
        palette,
        appearance,
      );
    }
  }

  const realSleeveAlpha =
    heroAlpha *
    easeOutCubic(
      normalizeRange(clampedFactor, ALBUMS_BLOOM_ARTWORK_FADE_START, ALBUMS_BLOOM_ARTWORK_FADE_END),
    );
  const bracketFocusWeight = lerp(
    0,
    1,
    easeOutCubic(normalizeRange(clampedFactor, ALBUMS_BLOOM_BRACKET_START, 1.0)),
  );
  if (heroAlbum && realSleeveAlpha > CONTENT_VISIBILITY_THRESHOLD) {
    drawCrateAlbumSleeveBox(drawingContext, heroAlbum, {
      centerX: heroCenterX,
      centerY: heroCenterY,
      sleeveSize: heroSleeveSize,
      focusWeight: bracketFocusWeight,
      delta: 0,
      baseAlpha: realSleeveAlpha,
      palette,
      appearance,
      coverCache,
    });
  }

  const miniOverlayAlpha =
    heroAlpha *
    (1 -
      easeOutCubic(
        normalizeRange(
          clampedFactor,
          ALBUMS_BLOOM_MINI_ICON_FADE_START,
          ALBUMS_BLOOM_MINI_ICON_FADE_END,
        ),
      ));
  if (miniOverlayAlpha > CONTENT_VISIBILITY_THRESHOLD) {
    drawMiniatureAlbumsIconOverlay(
      drawingContext,
      heroCenterX,
      heroCenterY,
      heroSleeveSize,
      miniOverlayAlpha,
      palette,
      appearance,
    );
  }
}

/**
 * Render the DialOS 03 LIBRARY screen from deterministic Library controller state,
 * live playback progress, and cover image cache.
 *
 * @param {CanvasRenderingContext2D} drawingContext Canvas 2D context.
 * @param {object} libraryState Snapshot from createLibraryController().getRenderSnapshot().
 * @param {object} [renderContext={}] Theme, palette, coverCache, and playback telemetry.
 */
export function renderLibraryScreen(drawingContext, libraryState, renderContext = {}) {
  if (!drawingContext || !libraryState) {
    return;
  }

  const appearance = resolveAppearance(renderContext?.appearance);
  const palette = resolvePalette(appearance, renderContext?.palette);
  const coverCache = renderContext?.coverCache || null;
  const playback = renderContext?.playback || {};
  const animationTimeSeconds = Number.isFinite(renderContext?.animationTimeSeconds)
    ? renderContext.animationTimeSeconds
    : Number(libraryState.idleClockSeconds ?? 0);
  const playbackIsPlaying =
    typeof libraryState.isPlaying === "boolean"
      ? libraryState.isPlaying
      : Boolean(playback.isPlaying);

  const viewStack = Array.isArray(libraryState.viewStack) ? libraryState.viewStack : [];
  const currentView = libraryState.currentView || viewStack[viewStack.length - 1];
  if (!currentView) {
    return;
  }

  const scrollPosition = Number.isFinite(currentView.scrollPosition) ? currentView.scrollPosition : 0;
  const isAlbumTracklistOverlay =
    currentView.kind === "tracks" && currentView.sourceType === "album";
  const transitionProgress = clamp(Number(libraryState.transitionProgress ?? 1), 0, 1);
  const transitionDirection = Number.isFinite(libraryState.transitionDirection)
    ? libraryState.transitionDirection
    : 1;
  const transitionEase = easeOutExpo(transitionProgress);
  const slideOffsetX = isAlbumTracklistOverlay
    ? 0
    : (1 - transitionEase) * 28 * transitionDirection;
  const verticalSheetSlideY = isAlbumTracklistOverlay ? (1 - transitionEase) * 42 : 0;
  const launchProgress = clamp(Number(libraryState.vinylLaunchProgress ?? 0), 0, 1);

  const albumsTransitionMode = libraryState.albumsTransitionMode || "none";
  const albumsTransitionProgress = clamp(
    Number(libraryState.albumsTransitionProgress ?? 1),
    0,
    1,
  );
  const isAlbumsHeroTransitionActive =
    launchProgress <= CONTENT_VISIBILITY_THRESHOLD &&
    (albumsTransitionMode === "hub-to-albums" || albumsTransitionMode === "albums-to-hub") &&
    albumsTransitionProgress < 1;

  if (isAlbumsHeroTransitionActive) {
    const crateFactor =
      albumsTransitionMode === "hub-to-albums"
        ? albumsTransitionProgress
        : 1 - albumsTransitionProgress;
    renderLibraryHubAlbumsTransition(drawingContext, libraryState, {
      crateFactor,
      palette,
      appearance,
      coverCache,
      animationTimeSeconds,
      playbackIsPlaying,
    });
    return;
  }

  const headerLaunchExit = easeInOutCubic(normalizeRange(launchProgress, 0.0, 0.44));
  const headerOffsetY = lerp(0, -34, headerLaunchExit);
  const chromeAlpha =
    (1 - headerLaunchExit) * easeOutCubic(normalizeRange(transitionProgress, 0.0, 0.85));

  const crateAlbums =
    currentView.kind === "albums" ? resolveCrateAlbumsFromState(libraryState, currentView) : [];

  let dynamicSubtitle = currentView.subtitle || "";
  if (currentView.kind === "hub") {
    const totalTrackCount = Number(
      libraryState.counts?.tracks ?? libraryState.catalog?.allTracks?.length ?? 0,
    );
    dynamicSubtitle =
      totalTrackCount === 0
        ? "0 TRACKS \u00b7 EMPTY COLLECTION"
        : `2-UP CRYSTAL HUB \u00b7 ${formatIntegerCount(totalTrackCount)} TRACKS`;
  } else if (currentView.kind === "albums") {
    if (crateAlbums.length === 0) {
      dynamicSubtitle = "VINYL CRATE \u00b7 00 / 00";
    } else {
      const focusedIndex = clamp(Math.round(scrollPosition), 0, crateAlbums.length - 1);
      const currentLabel = String(focusedIndex + 1).padStart(2, "0");
      const totalLabel = String(crateAlbums.length).padStart(2, "0");
      dynamicSubtitle = `VINYL CRATE \u00b7 ${currentLabel} / ${totalLabel}`;
    }
  } else if (currentView.kind === "playlists") {
    const playlistCount = Array.isArray(libraryState.playlists)
      ? libraryState.playlists.length
      : 0;
    dynamicSubtitle = `${formatIntegerCount(playlistCount)} MIXES \u00b7 HOLD ROW TO MANAGE`;
  }

  drawLibraryPerspectiveFloor(drawingContext, scrollPosition, chromeAlpha, palette, appearance);
  if (currentView.isArtistView || currentView.isFavoritesView) {
    const activeBlend = currentView.isFavoritesView
      ? (libraryState.favoritesModeBlend ?? (currentView.favoritesViewMode === "songs" ? 1 : 0))
      : (libraryState.artistModeBlend ?? (currentView.artistViewMode === "songs" ? 1 : 0));
    drawArtistHeaderChrome(
      drawingContext,
      currentView.title || (currentView.isFavoritesView ? "Favorites" : "Artist"),
      activeBlend,
      headerOffsetY,
      chromeAlpha,
      palette,
      appearance,
      animationTimeSeconds,
    );
  } else {
    drawLibraryPeripheralChrome(
      drawingContext,
      currentView.title || "Library",
      dynamicSubtitle,
      headerOffsetY,
      chromeAlpha,
      palette,
      appearance,
      animationTimeSeconds,
    );
  }

  const artistTabFade = currentView.isArtistView
    ? easeOutCubic(clamp(Number(libraryState.artistTabFadeProgress ?? 1), 0, 1))
    : currentView.isFavoritesView
      ? easeOutCubic(clamp(Number(libraryState.favoritesTabFadeProgress ?? 1), 0, 1))
      : 1;

  drawingContext.save();
  drawingContext.translate(slideOffsetX, 0);

  if (currentView.kind === "hub") {
    const rows = resolveRowsFromState(libraryState, currentView);
    const hubFade =
      easeOutCubic(transitionProgress) *
      (1 - easeInOutCubic(normalizeRange(launchProgress, 0.0, 0.40)));
    if (hubFade > CONTENT_VISIBILITY_THRESHOLD) {
      renderLibraryHubGrid(
        drawingContext,
        currentView,
        rows,
        libraryState,
        hubFade,
        palette,
        appearance,
      );
    }
  } else if (currentView.kind === "albums") {
    if (currentView.isFavoritesView && crateAlbums.length === 0) {
      renderFavoritesInlineEmptyState(
        drawingContext,
        "albums",
        libraryState.idleClockSeconds,
        easeOutCubic(transitionProgress) * artistTabFade * (1 - headerLaunchExit),
        palette,
        appearance,
      );
    } else {
      renderLibraryAlbumCrate(
        drawingContext,
        currentView,
        crateAlbums,
        libraryState,
        easeOutCubic(transitionProgress) * artistTabFade,
        launchProgress,
        palette,
        appearance,
        coverCache,
        animationTimeSeconds,
        playbackIsPlaying,
      );
    }
  } else if (currentView.kind === "empty") {
    renderLibraryEmptyState(
      drawingContext,
      currentView,
      libraryState.idleClockSeconds,
      easeOutCubic(transitionProgress) * (1 - headerLaunchExit),
      palette,
      appearance,
    );
  } else {
    const rows = resolveRowsFromState(libraryState, currentView);
    const listFade =
      easeOutCubic(transitionProgress) *
      artistTabFade *
      (1 - easeInOutCubic(normalizeRange(launchProgress, 0.0, 0.40)));
    if (currentView.isFavoritesView && rows.length === 0) {
      renderFavoritesInlineEmptyState(
        drawingContext,
        "songs",
        libraryState.idleClockSeconds,
        listFade,
        palette,
        appearance,
      );
    } else {
      if (isAlbumTracklistOverlay) {
      const catalogAlbums = Array.isArray(libraryState.catalog?.albums)
        ? libraryState.catalog.albums
        : [];
      const singleBackgroundAlbum =
        catalogAlbums.find((albumEntry) => albumEntry.id === currentView.albumId) || null;
      const singleAlbumAlpha = 1 - easeInOutCubic(normalizeRange(launchProgress, 0.0, 0.34));
      if (singleBackgroundAlbum && singleAlbumAlpha > CONTENT_VISIBILITY_THRESHOLD) {
        const activeTrackId = libraryState.activeTrackId || null;
        const isBackgroundAlbumPlaying = Boolean(
          activeTrackId &&
            Array.isArray(singleBackgroundAlbum.tracks) &&
            singleBackgroundAlbum.tracks.some((trackEntry) => trackEntry.id === activeTrackId),
        );
        drawCrateAlbumSleeveWithDisc(drawingContext, singleBackgroundAlbum, {
          centerX: DISPLAY_CENTER_PX - 8,
          centerY: LIBRARY_CONSTANTS.CRATE_CENTER_Y,
          sleeveSize: CRATE_MAX_SLEEVE_PX,
          focusWeight: 1,
          delta: 0,
          baseAlpha: singleAlbumAlpha,
          vinylRotationRadians: libraryState.vinylRotationRadians,
          showPeekingDisc: isBackgroundAlbumPlaying,
          isDiscPlaying: isBackgroundAlbumPlaying && playbackIsPlaying,
          palette,
          appearance,
          coverCache,
        });
      }
      if (listFade > CONTENT_VISIBILITY_THRESHOLD) {
        drawAlbumTracklistCrystalScreen(
          drawingContext,
          transitionProgress,
          listFade,
          palette,
          appearance,
        );
      }
    }
    if (listFade > CONTENT_VISIBILITY_THRESHOLD) {
      drawingContext.save();
      drawingContext.translate(0, verticalSheetSlideY);
      renderLibraryCurvedList(
        drawingContext,
        currentView,
        rows,
        libraryState,
        listFade,
        palette,
        appearance,
        animationTimeSeconds,
      );
      drawingContext.restore();
    }
    }
  }
  drawingContext.restore();

  if (launchProgress > CONTENT_VISIBILITY_THRESHOLD) {
    const catalogAlbums = Array.isArray(libraryState.catalog?.albums)
      ? libraryState.catalog.albums
      : [];
    const vinylLaunchAlbum =
      catalogAlbums.find((albumEntry) => albumEntry.id === libraryState.vinylLaunchAlbumId) ||
      catalogAlbums[0] || { motif: "prism" };
    const launchEase = easeInOutCubic(launchProgress);
    const fromAlbumsCrate = libraryState.vinylLaunchSource === "albums";
    const totalRotation =
      (Number.isFinite(libraryState.vinylRotationRadians) ? libraryState.vinylRotationRadians : 0) +
      (Number.isFinite(libraryState.vinylNudgeRadians) ? libraryState.vinylNudgeRadians : 0);

    let discCenterX = DISPLAY_CENTER_PX;
    let discCenterY = DISPLAY_CENTER_PX;
    let discRadius = lerp(TRACKLIST_DISC_START_RADIUS_PX, DISPLAY_CENTER_PX, launchEase);
    let discAlpha = easeOutCubic(normalizeRange(launchProgress, 0.04, 0.24));

    if (fromAlbumsCrate) {
      discCenterX =
        launchProgress <= CRATE_SLIP_OUT_SPLIT_PROGRESS
          ? lerp(
              CRATE_DISC_PEEK_X_PX,
              CRATE_DISC_APEX_X_PX,
              easeOutCubic(launchProgress / CRATE_SLIP_OUT_SPLIT_PROGRESS),
            )
          : lerp(
              CRATE_DISC_APEX_X_PX,
              DISPLAY_CENTER_PX,
              easeInOutCubic(
                (launchProgress - CRATE_SLIP_OUT_SPLIT_PROGRESS) /
                  (1 - CRATE_SLIP_OUT_SPLIT_PROGRESS),
              ),
            );
      discCenterY = lerp(LIBRARY_CONSTANTS.CRATE_CENTER_Y, DISPLAY_CENTER_PX, launchEase);
      discRadius = lerp(CRATE_DISC_START_RADIUS_PX, DISPLAY_CENTER_PX, launchEase);
      discAlpha = 1;
    }

    drawLibraryVinylDisc(
      drawingContext,
      vinylLaunchAlbum,
      discCenterX,
      discCenterY,
      discRadius,
      totalRotation,
      palette,
      appearance,
      discAlpha,
      coverCache,
      { showCenterLabel: false },
    );

    if (fromAlbumsCrate) {
      const sleeveSlideEase = easeInOutCubic(
        normalizeRange(launchProgress, 0.0, CRATE_SLEEVE_SLIDE_END_PROGRESS),
      );
      const sleeveCenterX = lerp(
        CRATE_SLEEVE_START_X_PX,
        CRATE_SLEEVE_EXIT_X_PX,
        sleeveSlideEase,
      );
      const sleeveAlpha =
        1 -
        easeOutCubic(
          normalizeRange(
            launchProgress,
            CRATE_SLEEVE_FADE_START_PROGRESS,
            CRATE_SLEEVE_SLIDE_END_PROGRESS,
          ),
        );
      if (sleeveAlpha > CONTENT_VISIBILITY_THRESHOLD) {
        drawCrateAlbumSleeveBox(drawingContext, vinylLaunchAlbum, {
          centerX: sleeveCenterX,
          centerY: LIBRARY_CONSTANTS.CRATE_CENTER_Y,
          sleeveSize: CRATE_MAX_SLEEVE_PX,
          focusWeight: 1,
          delta: 0,
          alpha: sleeveAlpha,
          palette,
          appearance,
          coverCache,
        });
      }
    }
  }

  if (Number(libraryState.playlistSheetProgress ?? 0) > CONTENT_VISIBILITY_THRESHOLD) {
    renderLibraryPlaylistSheet(
      drawingContext,
      libraryState,
      palette,
      appearance,
      animationTimeSeconds,
    );
  }

  const quickActionsActiveProgress = Math.max(
    Number(libraryState.quickActionsProgress ?? 0),
    Number(libraryState.albumQuickActionsProgress ?? 0),
    Number(libraryState.artistQuickActionsProgress ?? 0),
  );
  if (quickActionsActiveProgress > CONTENT_VISIBILITY_THRESHOLD) {
    renderLibraryQuickActions(
      drawingContext,
      libraryState,
      palette,
      appearance,
      animationTimeSeconds,
    );
  }

  if (Number(libraryState.sortWheelProgress ?? 0) > CONTENT_VISIBILITY_THRESHOLD) {
    renderLibrarySortWheel(drawingContext, libraryState, palette, appearance);
  }

  if (
    launchProgress < 0.05 &&
    Number(libraryState.playlistSheetProgress ?? 0) < 0.05 &&
    quickActionsActiveProgress < 0.05 &&
    Number(libraryState.sortWheelProgress ?? 0) < 0.05
  ) {
    renderLibrarySortToast(drawingContext, libraryState, palette, appearance);
    renderLibraryLetterSearchHud(drawingContext, libraryState, currentView, palette, appearance);
    drawLibraryRimArcFeedback(drawingContext, libraryState, currentView, palette, appearance);
  }

  const deleteCompletedFlash = Number(libraryState.deleteCompletedFlash ?? 0);
  if (deleteCompletedFlash > CONTENT_VISIBILITY_THRESHOLD) {
    drawingContext.save();
    drawingContext.globalAlpha = deleteCompletedFlash * 0.28;
    drawingContext.fillStyle = palette.signalActive;
    drawingContext.fillRect(0, 0, DISPLAY_DIAMETER_PX, DISPLAY_DIAMETER_PX);
    drawingContext.restore();
  }

  const backSwipeProgress = Number(libraryState.backSwipeProgress ?? 0);
  if (backSwipeProgress > 0.02) {
    const swipeAlpha = clamp(backSwipeProgress, 0, 1);
    drawingContext.save();
    drawingContext.globalAlpha = swipeAlpha * 0.92;
    drawingContext.strokeStyle = palette.signalHighlight;
    drawingContext.lineWidth = 4;
    drawingContext.lineCap = "butt";
    drawingContext.beginPath();
    drawingContext.arc(
      DISPLAY_CENTER_PX,
      DISPLAY_CENTER_PX,
      BACK_SWIPE_ARC_RADIUS_PX,
      Math.PI - BACK_SWIPE_ARC_SPAN_RADIANS * swipeAlpha,
      Math.PI + BACK_SWIPE_ARC_SPAN_RADIANS * swipeAlpha,
    );
    drawingContext.stroke();
    drawingContext.restore();
  }
}

/**
 * Determine whether display coordinates (pointX, pointY) hover an actionable Library
 * target (Hub crystal tile, curved list row, album crate sleeve/peeking disc,
 * empty BACK TO LIBRARY plate, or Playlist Action Sheet buttons).
 *
 * @param {number} pointX Display coordinate X in [0, 360].
 * @param {number} pointY Display coordinate Y in [0, 360].
 * @param {object} libraryState Snapshot from createLibraryController().getRenderSnapshot().
 * @returns {boolean} True when hovering a clickable Library element.
 */
export function hitTestLibraryInteractive(pointX, pointY, libraryState) {
  if (!libraryState || !Number.isFinite(pointX) || !Number.isFinite(pointY)) {
    return false;
  }

  if (libraryState.vinylLaunchTarget === 1 || Number(libraryState.vinylLaunchProgress ?? 0) > 0.15) {
    return false;
  }

  if (libraryState.playlistSheetOpen || Number(libraryState.playlistSheetProgress ?? 0) > 0.5) {
    if (pointX < SHEET_BUTTON_LEFT_X || pointX > SHEET_BUTTON_RIGHT_X) {
      return false;
    }
    const hitPlayButton = pointY >= SHEET_PLAY_TOP_Y && pointY <= SHEET_PLAY_BOTTOM_Y;
    const hitShuffleButton = pointY >= SHEET_SHUFFLE_TOP_Y && pointY <= SHEET_SHUFFLE_BOTTOM_Y;
    const hitDeleteButton = pointY >= SHEET_DELETE_TOP_Y && pointY <= SHEET_DELETE_BOTTOM_Y;
    return hitPlayButton || hitShuffleButton || hitDeleteButton;
  }

  if (
    libraryState.quickActionsOpen ||
    libraryState.albumQuickActionsOpen ||
    libraryState.artistQuickActionsOpen ||
    Math.max(
      Number(libraryState.quickActionsProgress ?? 0),
      Number(libraryState.albumQuickActionsProgress ?? 0),
      Number(libraryState.artistQuickActionsProgress ?? 0),
    ) > 0.5
  ) {
    return (
      Math.hypot(pointX - DISPLAY_CENTER_PX, pointY - ALBUM_QUICK_ACTION_BUTTON_CENTER_Y) <=
      ALBUM_QUICK_ACTION_BUTTON_HIT_RADIUS_PX
    );
  }

  if (libraryState.sortWheelOpen || Number(libraryState.sortWheelProgress ?? 0) > 0.5) {
    const isArtistsWheel =
      libraryState?.sortWheelScope === "artists" ||
      (!libraryState?.sortWheelScope && libraryState?.currentView?.kind === "artists");
    const slots = isArtistsWheel
      ? computeSortWheelSlots(
          LIBRARY_CONSTANTS.ARTISTS_SORT_BUTTON_X,
          LIBRARY_CONSTANTS.ARTISTS_SORT_BUTTON_Y,
        )
      : computeSortWheelSlots();
    return slots.some(
      (slot) => Math.hypot(pointX - slot.centerX, pointY - slot.centerY) <= slot.radius + 6,
    );
  }

  const viewStack = Array.isArray(libraryState.viewStack) ? libraryState.viewStack : [];
  const currentView = libraryState.currentView || viewStack[viewStack.length - 1];
  if (!currentView) {
    return false;
  }

  if (currentView.kind === "empty") {
    return (
      Math.abs(pointX - DISPLAY_CENTER_PX) <= EMPTY_BACK_CTA_HALF_WIDTH_PX &&
      pointY >= EMPTY_BACK_CTA_TOP_Y &&
      pointY <= EMPTY_BACK_CTA_BOTTOM_Y
    );
  }

  if (
    (currentView.isArtistView || currentView.isFavoritesView) &&
    pointY >= LIBRARY_CONSTANTS.ARTIST_TOGGLE_HIT_TOP_Y &&
    pointY <= LIBRARY_CONSTANTS.ARTIST_TOGGLE_HIT_BOTTOM_Y &&
    Math.abs(pointX - DISPLAY_CENTER_PX) <= LIBRARY_CONSTANTS.ARTIST_TOGGLE_HIT_HALF_WIDTH_PX
  ) {
    return true;
  }

  const scrollPosition = Number.isFinite(currentView.scrollPosition) ? currentView.scrollPosition : 0;

  if (currentView.kind === "albums") {
    const crateAlbums = resolveCrateAlbumsFromState(libraryState, currentView);
    if (crateAlbums.length === 0) {
      return false;
    }
    const hitCenterSleeve =
      pointX >= CRATE_SLEEVE_LEFT_X &&
      pointX < CRATE_PEEK_LEFT_X &&
      pointY >= CRATE_SLEEVE_TOP_Y &&
      pointY <= CRATE_SLEEVE_BOTTOM_Y;
    const hitPeekingDisc =
      pointX >= CRATE_PEEK_LEFT_X &&
      pointX <= CRATE_PEEK_RIGHT_X &&
      pointY >= CRATE_PEEK_TOP_Y &&
      pointY <= CRATE_PEEK_BOTTOM_Y;
    const hitFloatingSortButton =
      Math.hypot(pointX - CRATE_SORT_BUTTON_CENTER_X, pointY - CRATE_NOTE_BUTTON_CENTER_Y) <=
      CRATE_NOTE_BUTTON_HIT_RADIUS_PX;
    const hitFloatingNoteButton =
      Math.hypot(pointX - CRATE_NOTE_BUTTON_CENTER_X, pointY - CRATE_NOTE_BUTTON_CENTER_Y) <=
      CRATE_NOTE_BUTTON_HIT_RADIUS_PX;
    if (hitCenterSleeve || hitPeekingDisc || hitFloatingSortButton || hitFloatingNoteButton) {
      return true;
    }

    const startIndex = Math.max(0, Math.floor(scrollPosition) - 1);
    const endIndex = Math.min(crateAlbums.length - 1, Math.ceil(scrollPosition) + 1);
    for (let albumIndex = startIndex; albumIndex <= endIndex; albumIndex += 1) {
      const delta = albumIndex - scrollPosition;
      const absDelta = Math.abs(delta);
      if (absDelta > 1.5) {
        continue;
      }
      const focusWeight = clamp(1 - absDelta, 0, 1);
      const sleeveSize = lerp(
        CRATE_MIN_SLEEVE_PX,
        CRATE_MAX_SLEEVE_PX,
        easeInOutCubic(focusWeight),
      );
      const sleeveHalf = sleeveSize / 2;
      const sleeveCenterX =
        DISPLAY_CENTER_PX - focusWeight * 8 + delta * LIBRARY_CONSTANTS.CRATE_SPACING_PX;
      const sleeveCenterY = LIBRARY_CONSTANTS.CRATE_CENTER_Y + Math.min(absDelta, 1.5) * 6;
      if (
        Math.abs(pointX - sleeveCenterX) <= sleeveHalf &&
        Math.abs(pointY - sleeveCenterY) <= sleeveHalf
      ) {
        return true;
      }
    }
    return false;
  }

  if (pointY < HEADER_CLIP_TOP_Y) {
    return false;
  }

  const rows = resolveRowsFromState(libraryState, currentView);
  if (
    currentView.kind === "artists" &&
    rows.length > 0 &&
    Math.hypot(
      pointX - LIBRARY_CONSTANTS.ARTISTS_SORT_BUTTON_X,
      pointY - LIBRARY_CONSTANTS.ARTISTS_SORT_BUTTON_Y,
    ) <= CRATE_NOTE_BUTTON_HIT_RADIUS_PX
  ) {
    return true;
  }
  if (currentView.kind === "hub") {
    for (let tileIndex = 0; tileIndex < rows.length; tileIndex += 1) {
      const tileRect = computeLibraryHubTileRect(tileIndex, scrollPosition);
      if (tileRect.alpha < 0.25) {
        continue;
      }
      if (
        Math.abs(pointX - tileRect.centerX) <= tileRect.width / 2 &&
        Math.abs(pointY - tileRect.centerY) <= tileRect.height / 2
      ) {
        return true;
      }
    }
    return false;
  }

  const startRow = Math.max(0, Math.floor(scrollPosition) - 2);
  const endRow = Math.min(rows.length - 1, Math.ceil(scrollPosition) + 4);
  for (let rowIndex = startRow; rowIndex <= endRow; rowIndex += 1) {
    const rowLayout = computeLibraryCurvedRowLayout(rowIndex, scrollPosition);
    if (rowLayout.alpha < 0.22) {
      continue;
    }
    if (
      Math.abs(pointX - DISPLAY_CENTER_PX) <= rowLayout.rowHalfWidth &&
      Math.abs(pointY - rowLayout.rowY) <= rowLayout.rowHeight / 2 + 3
    ) {
      return true;
    }
  }

  return false;
}

export {
  resolveLibraryRimSliceIndex,
  shortestSliceDistance,
};
