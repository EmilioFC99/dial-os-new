/**
 * Pure Canvas 2D renderer and hit-tester for the DialOS 04 NOW PLAYING and 05 LYRICS screens:
 * full-bleed rotating optical vinyl with stationary overhead reflection wedges, dual-disc skip
 * transitions, frosted motif-glow lyrics overlay with 2D multi-tap optical defocus, long-press
 * Seek Mode ring, right-bezel Volume HUD, 3+2 Quick Actions cluster, Playlist Picker, Track
 * Details telemetry grid, Context Queue sheet, idle/receiver modes, and left-edge back-swipe.
 */

import { resolveSkipOutcome } from "../core/audio_engine.js";
import {
  DISPLAY_CENTER_PX,
  DISPLAY_DIAMETER_PX,
  FULL_CIRCLE_RADIANS,
  REGISTRATION_MARK_RADIUS_PX,
  SAFE_CLIP_RADIUS_PX,
  TOP_ANGLE_RADIANS,
  clamp,
  easeInOutCubic,
  easeOutCubic,
  easeOutExpo,
  formatClockTime,
  lerp,
  normalizeRange,
} from "../core/geometry.js";
import {
  NOW_PLAYING_CONSTANTS,
  NOW_PLAYING_QUICK_BUTTONS,
  NOW_PLAYING_REPEAT_MODES,
  computeNowPlayingQueueRowLayout,
  hitTestNowPlayingQueueRow,
} from "../core/now_playing_controller.js";
import { drawCrosshairMark, drawDiagonalMark } from "../core/primitives.js";
import { APPEARANCE_PALETTES, FONT_DISPLAY, FONT_MONO } from "../core/tokens.js";
import {
  drawCrateAlbumSleeveBox,
  drawLibraryAlbumArtworkMotif,
  drawLibraryCornerBrackets,
  drawLibraryPerspectiveFloor,
  drawLibraryScrollArc,
  drawLibraryVinylDisc,
  drawLiveMiniEqualizer,
  drawMarqueeCanvasText,
  traceChamferRect,
  truncateCanvasText,
} from "./library_screen.js";

const PERCENT_SCALE = 100;
const ALPHA_VISIBILITY_THRESHOLD = 0.005;
const LAYER_VISIBILITY_THRESHOLD = 0.01;
const DIAGONAL_MARK_COS_45 = 0.707;

const RIM_SHEEN_RADIUS_PX = 175;
const EDGE_PROGRESS_RADIUS_PX = DISPLAY_CENTER_PX - 1.5;
const EDGE_PROGRESS_LINE_WIDTH_PX = 1.6;
const VOLUME_ARC_RADIUS_PX = 166;
const VOLUME_ARC_START_RADIANS = -0.48;
const VOLUME_ARC_END_RADIANS = 0.48;
const BACK_SWIPE_ARC_RADIUS_PX = 168;
const BACK_SWIPE_ARC_SPAN_RADIANS = 0.34;
const DISC_SKIP_SLIDE_DISTANCE_PX = 370;
const HOME_ENTRANCE_START_DISC_RADIUS_PX = 54;
const ALBUM_CRATE_SLEEVE_START_X_PX = 172;
const ALBUM_CRATE_SLEEVE_EXIT_X_PX = -110;
const ALBUM_CRATE_SLEEVE_CENTER_Y_PX = 152;
const ALBUM_CRATE_SLEEVE_SIZE_PX = 120;
const ALBUM_CRATE_START_DISC_RADIUS_PX = 52.8;
const ALBUM_CRATE_DISC_PEEK_X_PX = 218;
const ALBUM_CRATE_DISC_APEX_X_PX = 246;
const ALBUM_CRATE_SLIP_OUT_SPLIT_PROGRESS = 0.36;
const ALBUM_CRATE_SLEEVE_SLIDE_END_PROGRESS = 0.72;
const ALBUM_CRATE_SLEEVE_FADE_START_PROGRESS = 0.38;

const IDLE_CTA_LEFT_X_PX = 96;
const IDLE_CTA_RIGHT_X_PX = 264;
const IDLE_CTA_TOP_Y_PX = 246;
const IDLE_CTA_BOTTOM_Y_PX = 288;

const PICKER_CARD_WIDTH_PX = 228;
const PICKER_CARD_HEIGHT_PX = 202;
const PICKER_ROW_LEFT_X_PX = 78;
const PICKER_ROW_RIGHT_X_PX = 282;
const PICKER_ROW_START_Y_PX = 132;
const PICKER_ROW_PITCH_PX = 35;
const PICKER_ROW_HEIGHT_PX = 32;
const PICKER_MAX_VISIBLE_ROWS = 4;

const QUICK_ACTION_HIT_PAD_PX = 5;
const QUEUE_LIST_CLIP_TOP_Y = 102;
const QUEUE_LIST_CLIP_HEIGHT_PX = 214;

const LYRICS_FIELD_LOWER_HORIZON_Y_PX = 238;
const LYRICS_FIELD_LOWER_EDGE_Y_PX = 368;
const LYRICS_FIELD_UPPER_HORIZON_Y_PX = 114;
const LYRICS_FIELD_UPPER_EDGE_Y_PX = -8;
const LYRICS_FIELD_RAY_HALF_COUNT = 4;
const LYRICS_FIELD_TOP_RAY_STEP_PX = 28;
const LYRICS_FIELD_BOTTOM_RAY_STEP_PX = 74;
const LYRICS_FIELD_DEPTH_STEPS = 4;
const LYRICS_FIELD_DRIFT_FACTOR = 0.35;
const LYRICS_FIELD_LINE_MARGIN_PX = 10;

const LYRICS_VIGNETTE_CENTER_Y_OFFSET_PX = -6;
const LYRICS_VIGNETTE_INNER_RADIUS_PX = 42;
const LYRICS_VIGNETTE_OUTER_RADIUS_PX = 144;

const LYRICS_OUTER_CROSSHAIRS = Object.freeze([
  Object.freeze({ centerX: 74, centerY: 84, kind: "plus" }),
  Object.freeze({ centerX: 286, centerY: 84, kind: "cross" }),
  Object.freeze({ centerX: 52, centerY: 274, kind: "cross" }),
  Object.freeze({ centerX: 308, centerY: 274, kind: "plus" }),
]);
const LYRICS_CROSSHAIR_PLUS_HALF_LENGTH_PX = 4.2;
const LYRICS_CROSSHAIR_CROSS_HALF_SPAN_PX = 3.6;
const LYRICS_CROSSHAIR_LINE_WIDTH_PX = 1.15;

const LYRICS_DIVIDER_MIN_Y_PX = 214;
const LYRICS_DIVIDER_MAX_Y_PX = 234;
const LYRICS_DIVIDER_OFFSET_Y_PX = 12;
const LYRICS_DIVIDER_LEFT_X_PX = 116;
const LYRICS_DIVIDER_RIGHT_X_PX = 244;
const LYRICS_DIVIDER_LEFT_CAP_X_PX = 110;
const LYRICS_DIVIDER_RIGHT_CAP_X_PX = 250;
const LYRICS_DIVIDER_CAP_SPAN_PX = 2.4;

const NO_LYRICS_MEDALLION_CENTER_Y_PX = 148;
const NO_LYRICS_MEDALLION_BASE_RADIUS_PX = 30;
const NO_LYRICS_ICON_RADIUS_PX = 16;
const NO_LYRICS_SLASH_HALF_SPAN_X_PX = 15;
const NO_LYRICS_SLASH_HALF_SPAN_Y_PX = 14;
const NO_LYRICS_SLASH_KNOCKOUT_WIDTH_PX = 4.4;
const NO_LYRICS_SLASH_LINE_WIDTH_PX = 2.0;

const NOW_PLAYING_MOTIF_GLOW_PALETTES = Object.freeze({
  prism: Object.freeze({
    primaryRgb: "192, 254, 5",
    lightPrimaryRgb: "232, 90, 36",
    secondaryRgb: "64, 96, 152",
  }),
  grid: Object.freeze({
    primaryRgb: "94, 220, 230",
    lightPrimaryRgb: "56, 178, 198",
    secondaryRgb: "26, 78, 118",
  }),
  monolith: Object.freeze({
    primaryRgb: "220, 228, 238",
    lightPrimaryRgb: "232, 90, 36",
    secondaryRgb: "108, 120, 138",
  }),
  tape: Object.freeze({
    primaryRgb: "245, 184, 65",
    lightPrimaryRgb: "232, 146, 38",
    secondaryRgb: "110, 88, 56",
  }),
  optical: Object.freeze({
    primaryRgb: "178, 150, 245",
    lightPrimaryRgb: "148, 116, 228",
    secondaryRgb: "68, 44, 124",
  }),
  concrete: Object.freeze({
    primaryRgb: "255, 96, 58",
    lightPrimaryRgb: "222, 74, 42",
    secondaryRgb: "132, 140, 152",
  }),
});

const MODE_COPY_MAP = Object.freeze({
  idle: Object.freeze({
    title: "Not Playing",
    subtitle: "NO TRACK QUEUED ON DISC",
    ctaLabel: "BROWSE LIBRARY",
    primaryCta: true,
  }),
  "usb-dac": Object.freeze({
    title: "USB DAC",
    subtitle: "PCM 32-BIT \u00b7 384 KHZ \u00b7 LOCKED",
    ctaLabel: "BACK TO HOME",
    primaryCta: false,
  }),
  "bt-rx": Object.freeze({
    title: "Bluetooth Rx",
    subtitle: "LDAC 990 KBPS \u00b7 WIRELESS INPUT",
    ctaLabel: "BACK TO HOME",
    primaryCta: false,
  }),
  "usb-storage": Object.freeze({
    title: "USB Storage",
    subtitle: "MOUNTED \u00b7 42.6 GB FREE",
    ctaLabel: "BACK TO HOME",
    primaryCta: false,
  }),
});

function resolveAppearance(appearance) {
  return appearance === "light" ? "light" : "dark";
}

function resolvePalette(appearance, palette) {
  if (palette && typeof palette === "object") {
    return palette;
  }
  return APPEARANCE_PALETTES[resolveAppearance(appearance)];
}

function resolveAlbumDescriptor(albumCandidate, trackCandidate, catalogAlbums) {
  const targetAlbumId = trackCandidate?.albumId || albumCandidate?.id || "";
  if (
    albumCandidate &&
    typeof albumCandidate === "object" &&
    (!trackCandidate?.albumId || albumCandidate.id === trackCandidate.albumId)
  ) {
    return albumCandidate;
  }
  if (targetAlbumId && Array.isArray(catalogAlbums)) {
    const matchedAlbum = catalogAlbums.find((albumEntry) => albumEntry?.id === targetAlbumId);
    if (matchedAlbum) {
      return matchedAlbum;
    }
  }
  if (albumCandidate && typeof albumCandidate === "object") {
    return albumCandidate;
  }
  return {
    id: targetAlbumId,
    title: trackCandidate?.albumTitle || "Unknown Album",
    artist: trackCandidate?.artist || "Unknown Artist",
    motif: trackCandidate?.motif || "prism",
    coverUrl: trackCandidate?.coverUrl || null,
  };
}

function wrapCanvasTextLines(drawingContext, lyricText, maxWidthPx, maxLineCount) {
  const cleanText = String(lyricText || "").trim();
  if (!cleanText) {
    return [];
  }
  const words = cleanText.split(/\s+/);
  const wrappedLines = [];
  let currentLine = "";

  for (let wordIndex = 0; wordIndex < words.length; wordIndex += 1) {
    const nextWord = words[wordIndex];
    const candidateLine = currentLine ? `${currentLine} ${nextWord}` : nextWord;
    if (drawingContext.measureText(candidateLine).width <= maxWidthPx || !currentLine) {
      currentLine = candidateLine;
    } else {
      wrappedLines.push(currentLine);
      currentLine = nextWord;
      if (wrappedLines.length >= maxLineCount - 1) {
        const remainder = words.slice(wordIndex).join(" ");
        wrappedLines.push(truncateCanvasText(drawingContext, remainder, maxWidthPx));
        return wrappedLines;
      }
    }
  }
  if (currentLine) {
    wrappedLines.push(truncateCanvasText(drawingContext, currentLine, maxWidthPx));
  }
  return wrappedLines;
}

/**
 * Pure 2D multi-tap optical text defocus (zero GPU blur shader; sharpens to 1 crisp pass at 0px).
 *
 * @param {CanvasRenderingContext2D} drawingContext Canvas 2D context.
 * @param {string} textLine Text line to render.
 * @param {number} centerX Center X coordinate.
 * @param {number} centerY Center Y coordinate.
 * @param {number} baseAlpha Base opacity multiplier.
 * @param {number} defocusRadiusPx Defocus spread in pixels.
 * @param {string} fillHex Text fill color.
 */
function drawSoftDefocusedText(
  drawingContext,
  textLine,
  centerX,
  centerY,
  baseAlpha,
  defocusRadiusPx,
  fillHex,
) {
  if (baseAlpha <= ALPHA_VISIBILITY_THRESHOLD || !textLine) {
    return;
  }
  drawingContext.save();
  drawingContext.fillStyle = fillHex;
  if (defocusRadiusPx <= 0.12) {
    drawingContext.globalAlpha = baseAlpha;
    drawingContext.fillText(textLine, centerX, centerY);
    drawingContext.restore();
    return;
  }

  const haloOffsets = [
    { offsetX: -defocusRadiusPx, offsetY: 0, weight: 0.19 },
    { offsetX: defocusRadiusPx, offsetY: 0, weight: 0.19 },
    { offsetX: 0, offsetY: -defocusRadiusPx * 0.82, weight: 0.18 },
    { offsetX: 0, offsetY: defocusRadiusPx * 0.82, weight: 0.18 },
    { offsetX: -defocusRadiusPx * 0.72, offsetY: -defocusRadiusPx * 0.65, weight: 0.11 },
    { offsetX: defocusRadiusPx * 0.72, offsetY: defocusRadiusPx * 0.65, weight: 0.11 },
  ];
  for (let tapIndex = 0; tapIndex < haloOffsets.length; tapIndex += 1) {
    const tap = haloOffsets[tapIndex];
    drawingContext.globalAlpha = baseAlpha * tap.weight;
    drawingContext.fillText(textLine, centerX + tap.offsetX, centerY + tap.offsetY);
  }
  drawingContext.globalAlpha = baseAlpha * 0.42;
  drawingContext.fillText(textLine, centerX, centerY);
  drawingContext.restore();
}

function drawNowPlayingOpticalVinyl(
  drawingContext,
  albumSpec,
  centerX,
  centerY,
  radius,
  rotationRadians,
  palette,
  appearance,
  alpha,
  coverCache,
) {
  if (alpha <= ALPHA_VISIBILITY_THRESHOLD || radius <= 2) {
    return;
  }
  drawLibraryVinylDisc(
    drawingContext,
    albumSpec,
    centerX,
    centerY,
    radius,
    rotationRadians,
    palette,
    appearance,
    alpha,
    coverCache,
    {
      showCenterLabel: false,
      grooveAlpha: 0,
    },
  );
}

/**
 * Draw the chamfered speech bubble with two quote marks used for Lyrics actions and No Lyrics state.
 *
 * @param {CanvasRenderingContext2D} drawingContext Canvas 2D context.
 * @param {number} centerX Center X coordinate.
 * @param {number} centerY Center Y coordinate.
 * @param {number} radius Glyph bounding radius in pixels.
 * @param {object} options Stroke/fill colors and line widths.
 */
function drawLyricsBubbleGlyph(
  drawingContext,
  centerX,
  centerY,
  radius,
  {
    bubbleColor,
    quoteColor,
    bubbleLineWidth = 1.8,
    quoteLineWidth = 1.6,
  },
) {
  drawingContext.save();
  drawingContext.translate(centerX, centerY);
  drawingContext.strokeStyle = bubbleColor;
  drawingContext.lineWidth = bubbleLineWidth;
  drawingContext.lineCap = "butt";
  drawingContext.lineJoin = "miter";

  // Speech bubble with two quote marks, chamfered to match DialOS 2D primitives.
  drawingContext.beginPath();
  drawingContext.moveTo(-radius * 0.62, -radius * 0.64);
  drawingContext.lineTo(radius * 0.62, -radius * 0.64);
  drawingContext.lineTo(radius * 0.80, -radius * 0.46);
  drawingContext.lineTo(radius * 0.80, radius * 0.22);
  drawingContext.lineTo(radius * 0.62, radius * 0.40);
  drawingContext.lineTo(-radius * 0.02, radius * 0.40);
  drawingContext.lineTo(-radius * 0.42, radius * 0.80);
  drawingContext.lineTo(-radius * 0.36, radius * 0.40);
  drawingContext.lineTo(-radius * 0.62, radius * 0.40);
  drawingContext.lineTo(-radius * 0.80, radius * 0.22);
  drawingContext.lineTo(-radius * 0.80, -radius * 0.46);
  drawingContext.closePath();
  drawingContext.stroke();

  drawingContext.fillStyle = quoteColor;
  drawingContext.strokeStyle = quoteColor;
  drawingContext.lineWidth = quoteLineWidth;
  for (const quoteCenterX of [-radius * 0.24, radius * 0.24]) {
    drawingContext.fillRect(
      quoteCenterX - radius * 0.11,
      -radius * 0.32,
      radius * 0.22,
      radius * 0.22,
    );
    drawingContext.beginPath();
    drawingContext.moveTo(quoteCenterX + radius * 0.11, -radius * 0.12);
    drawingContext.lineTo(quoteCenterX - radius * 0.05, radius * 0.16);
    drawingContext.stroke();
  }

  drawingContext.restore();
}

function drawNowPlayingActionIcon(
  drawingContext,
  iconId,
  centerX,
  centerY,
  radius,
  isActive,
  repeatMode,
  palette,
) {
  const strokeColor = isActive ? palette.signalHighlightInk : "#F5F6F8";
  const accentColor = palette.signalHighlight;

  drawingContext.save();
  drawingContext.translate(centerX, centerY);
  drawingContext.strokeStyle = strokeColor;
  drawingContext.fillStyle = strokeColor;
  drawingContext.lineWidth = 1.8;
  drawingContext.lineCap = "butt";
  drawingContext.lineJoin = "miter";

  if (iconId === "favorite") {
    drawingContext.beginPath();
    drawingContext.moveTo(0, radius * 0.72);
    drawingContext.lineTo(-radius * 0.80, -radius * 0.04);
    drawingContext.lineTo(-radius * 0.76, -radius * 0.52);
    drawingContext.lineTo(-radius * 0.30, -radius * 0.72);
    drawingContext.lineTo(0, -radius * 0.34);
    drawingContext.lineTo(radius * 0.30, -radius * 0.72);
    drawingContext.lineTo(radius * 0.76, -radius * 0.52);
    drawingContext.lineTo(radius * 0.80, -radius * 0.04);
    drawingContext.closePath();
    if (isActive) {
      drawingContext.fill();
    } else {
      drawingContext.stroke();
    }
  } else if (iconId === "shuffle") {
    drawingContext.beginPath();
    drawingContext.moveTo(-radius * 0.74, -radius * 0.44);
    drawingContext.lineTo(-radius * 0.22, -radius * 0.44);
    drawingContext.lineTo(radius * 0.22, radius * 0.44);
    drawingContext.lineTo(radius * 0.68, radius * 0.44);
    drawingContext.moveTo(radius * 0.40, radius * 0.18);
    drawingContext.lineTo(radius * 0.68, radius * 0.44);
    drawingContext.lineTo(radius * 0.40, radius * 0.70);

    drawingContext.moveTo(-radius * 0.74, radius * 0.44);
    drawingContext.lineTo(-radius * 0.22, radius * 0.44);
    drawingContext.lineTo(radius * 0.22, -radius * 0.44);
    drawingContext.lineTo(radius * 0.68, -radius * 0.44);
    drawingContext.moveTo(radius * 0.40, -radius * 0.70);
    drawingContext.lineTo(radius * 0.68, -radius * 0.44);
    drawingContext.lineTo(radius * 0.40, -radius * 0.18);
    drawingContext.stroke();
  } else if (iconId === "repeat") {
    drawingContext.beginPath();
    drawingContext.moveTo(-radius * 0.64, radius * 0.02);
    drawingContext.lineTo(-radius * 0.64, -radius * 0.46);
    drawingContext.lineTo(radius * 0.58, -radius * 0.46);
    drawingContext.moveTo(radius * 0.30, -radius * 0.72);
    drawingContext.lineTo(radius * 0.58, -radius * 0.46);
    drawingContext.lineTo(radius * 0.30, -radius * 0.20);

    drawingContext.moveTo(radius * 0.64, -radius * 0.02);
    drawingContext.lineTo(radius * 0.64, radius * 0.46);
    drawingContext.lineTo(-radius * 0.58, radius * 0.46);
    drawingContext.moveTo(-radius * 0.30, radius * 0.20);
    drawingContext.lineTo(-radius * 0.58, radius * 0.46);
    drawingContext.lineTo(-radius * 0.30, radius * 0.72);
    drawingContext.stroke();

    if (repeatMode === "one") {
      drawingContext.font = `800 8.5px ${FONT_MONO}`;
      drawingContext.fillStyle = isActive ? palette.signalHighlightInk : accentColor;
      drawingContext.textAlign = "center";
      drawingContext.textBaseline = "middle";
      drawingContext.fillText("1", 0, 0.5);
    }
  } else if (iconId === "playlist") {
    drawingContext.beginPath();
    drawingContext.moveTo(-radius * 0.72, -radius * 0.48);
    drawingContext.lineTo(radius * 0.42, -radius * 0.48);
    drawingContext.moveTo(-radius * 0.72, -radius * 0.04);
    drawingContext.lineTo(radius * 0.22, -radius * 0.04);
    drawingContext.moveTo(-radius * 0.72, radius * 0.40);
    drawingContext.lineTo(radius * 0.04, radius * 0.40);
    drawingContext.stroke();

    drawingContext.strokeStyle = isActive ? palette.signalHighlightInk : accentColor;
    drawingContext.lineWidth = 2;
    drawingContext.beginPath();
    drawingContext.moveTo(radius * 0.46, radius * 0.06);
    drawingContext.lineTo(radius * 0.46, radius * 0.72);
    drawingContext.moveTo(radius * 0.14, radius * 0.39);
    drawingContext.lineTo(radius * 0.78, radius * 0.39);
    drawingContext.stroke();
  } else if (iconId === "lyrics") {
    drawLyricsBubbleGlyph(drawingContext, 0, 0, radius, {
      bubbleColor: strokeColor,
      quoteColor: isActive ? palette.signalHighlightInk : accentColor,
      bubbleLineWidth: 1.8,
      quoteLineWidth: 1.6,
    });
  } else if (iconId === "info") {
    drawingContext.beginPath();
    drawingContext.arc(0, 0, radius * 0.76, 0, FULL_CIRCLE_RADIANS);
    drawingContext.strokeStyle = isActive
      ? palette.signalHighlightInk
      : "rgba(245, 246, 248, 0.72)";
    drawingContext.lineWidth = 1.5;
    drawingContext.stroke();

    drawingContext.fillStyle = isActive ? palette.signalHighlightInk : accentColor;
    drawingContext.fillRect(-1.5, -radius * 0.46, 3, 3);

    drawingContext.fillStyle = isActive ? palette.signalHighlightInk : "#F5F6F8";
    drawingContext.fillRect(-1.5, -radius * 0.14, 3, radius * 0.58);
    drawingContext.fillRect(-3.5, radius * 0.36, 7, 1.8);
  }

  drawingContext.restore();
}

function drawNowPlayingSpindleAndSeek(
  drawingContext,
  nowPlayingState,
  trackDurationSeconds,
  openWaveOne,
  palette,
) {
  const seekExpand = easeOutExpo(clamp(Number(nowPlayingState.seekExpandProgress ?? 0), 0, 1));
  const holdCharge = clamp(Number(nowPlayingState.seekHoldProgress ?? 0), 0, 1);
  const radius =
    lerp(
      NOW_PLAYING_CONSTANTS.SPINDLE_BASE_RADIUS_PX,
      NOW_PLAYING_CONSTANTS.SPINDLE_SEEK_RADIUS_PX,
      seekExpand,
    ) * lerp(0.42, 1, openWaveOne);
  if (radius <= 2 || openWaveOne <= LAYER_VISIBILITY_THRESHOLD) {
    return;
  }

  const safeDuration = Math.max(1, trackDurationSeconds);
  const elapsedSeconds = Number(nowPlayingState.trackElapsedSeconds ?? 0);
  const trackRatio = clamp(elapsedSeconds / safeDuration, 0, 1);
  const progressEndAngle = TOP_ANGLE_RADIANS + trackRatio * FULL_CIRCLE_RADIANS;

  drawingContext.save();
  drawingContext.globalAlpha = openWaveOne;

  // Thin outer-edge song progress bar so center artwork stays pure full-bleed at rest.
  drawingContext.beginPath();
  drawingContext.arc(
    DISPLAY_CENTER_PX,
    DISPLAY_CENTER_PX,
    EDGE_PROGRESS_RADIUS_PX,
    0,
    FULL_CIRCLE_RADIANS,
  );
  drawingContext.strokeStyle = "rgba(245, 246, 248, 0.20)";
  drawingContext.lineWidth = EDGE_PROGRESS_LINE_WIDTH_PX;
  drawingContext.stroke();

  if (trackRatio > 0.001) {
    drawingContext.beginPath();
    drawingContext.arc(
      DISPLAY_CENTER_PX,
      DISPLAY_CENTER_PX,
      EDGE_PROGRESS_RADIUS_PX,
      TOP_ANGLE_RADIANS,
      progressEndAngle,
    );
    drawingContext.strokeStyle = palette.signalHighlight;
    drawingContext.lineWidth = EDGE_PROGRESS_LINE_WIDTH_PX;
    drawingContext.lineCap = "butt";
    drawingContext.stroke();
  }

  if (holdCharge <= 0.02 && seekExpand <= 0.02) {
    drawingContext.restore();
    return;
  }

  if (holdCharge > 0.02 && seekExpand < 0.35) {
    const chargeRadius = radius + 6;
    drawingContext.save();
    drawingContext.globalAlpha = openWaveOne * (1 - seekExpand);
    drawingContext.beginPath();
    drawingContext.arc(DISPLAY_CENTER_PX, DISPLAY_CENTER_PX, chargeRadius, 0, FULL_CIRCLE_RADIANS);
    drawingContext.strokeStyle = "rgba(245, 246, 248, 0.24)";
    drawingContext.lineWidth = 2.6;
    drawingContext.stroke();

    drawingContext.beginPath();
    drawingContext.arc(
      DISPLAY_CENTER_PX,
      DISPLAY_CENTER_PX,
      chargeRadius,
      TOP_ANGLE_RADIANS,
      TOP_ANGLE_RADIANS + holdCharge * FULL_CIRCLE_RADIANS,
    );
    drawingContext.strokeStyle = palette.signalHighlight;
    drawingContext.lineWidth = 3.2;
    drawingContext.lineCap = "butt";
    drawingContext.stroke();
    drawingContext.restore();
  }

  if (seekExpand > 0.02) {
    drawingContext.beginPath();
    drawingContext.arc(DISPLAY_CENTER_PX, DISPLAY_CENTER_PX, radius + 6, 0, FULL_CIRCLE_RADIANS);
    drawingContext.strokeStyle = palette.signalHighlightSoft;
    drawingContext.lineWidth = 4 * seekExpand;
    drawingContext.stroke();
  }

  drawingContext.beginPath();
  drawingContext.arc(DISPLAY_CENTER_PX, DISPLAY_CENTER_PX, radius, 0, FULL_CIRCLE_RADIANS);
  drawingContext.fillStyle = seekExpand > 0.05 ? "rgba(6, 7, 9, 0.92)" : "rgba(6, 7, 9, 0.86)";
  drawingContext.fill();

  if (seekExpand > 0.15) {
    const tickAlpha = normalizeRange(seekExpand, 0.15, 0.85) * 0.42;
    drawingContext.save();
    drawingContext.globalAlpha = openWaveOne * tickAlpha;
    drawingContext.strokeStyle = "#F5F6F8";
    drawingContext.lineWidth = 1;
    const tickCount = 24;
    for (let tickIndex = 0; tickIndex < tickCount; tickIndex += 1) {
      const tickAngle = TOP_ANGLE_RADIANS + (tickIndex / tickCount) * FULL_CIRCLE_RADIANS;
      const innerTick = radius - (tickIndex % 6 === 0 ? 11.5 : 9.5);
      const outerTick = radius - 7.5;
      drawingContext.beginPath();
      drawingContext.moveTo(
        DISPLAY_CENTER_PX + Math.cos(tickAngle) * innerTick,
        DISPLAY_CENTER_PX + Math.sin(tickAngle) * innerTick,
      );
      drawingContext.lineTo(
        DISPLAY_CENTER_PX + Math.cos(tickAngle) * outerTick,
        DISPLAY_CENTER_PX + Math.sin(tickAngle) * outerTick,
      );
      drawingContext.stroke();
    }
    drawingContext.restore();
  }

  // Resting state draws one thin perimeter circle (r = 13.5px) acting directly as progress ring.
  const trackRingRadius = seekExpand > 0.05 ? radius - lerp(0, 5.5, seekExpand) : radius;
  const ringStrokeWidth = lerp(1.4, 4.2, seekExpand);
  drawingContext.beginPath();
  drawingContext.arc(
    DISPLAY_CENTER_PX,
    DISPLAY_CENTER_PX,
    trackRingRadius,
    0,
    FULL_CIRCLE_RADIANS,
  );
  drawingContext.strokeStyle = "rgba(245, 246, 248, 0.28)";
  drawingContext.lineWidth = ringStrokeWidth;
  drawingContext.stroke();

  drawingContext.beginPath();
  drawingContext.arc(
    DISPLAY_CENTER_PX,
    DISPLAY_CENTER_PX,
    trackRingRadius,
    TOP_ANGLE_RADIANS,
    progressEndAngle,
  );
  drawingContext.strokeStyle = palette.signalHighlight;
  drawingContext.lineWidth = ringStrokeWidth;
  drawingContext.lineCap = "butt";
  drawingContext.stroke();

  if (seekExpand > 0.08) {
    const cursorX = DISPLAY_CENTER_PX + Math.cos(progressEndAngle) * trackRingRadius;
    const cursorY = DISPLAY_CENTER_PX + Math.sin(progressEndAngle) * trackRingRadius;
    drawingContext.save();
    drawingContext.translate(cursorX, cursorY);
    drawingContext.rotate(progressEndAngle);
    drawingContext.fillStyle = "#F5F6F8";
    drawingContext.fillRect(-3, -3, 6, 6);
    drawingContext.restore();
  }

  const restAlpha = 1 - normalizeRange(seekExpand, 0.05, 0.55);
  if (restAlpha > 0.02) {
    drawingContext.save();
    drawingContext.globalAlpha = openWaveOne * restAlpha;
    if (!nowPlayingState.isPlaying) {
      drawingContext.fillStyle = palette.signalHighlight;
      drawingContext.fillRect(DISPLAY_CENTER_PX - 3.4, DISPLAY_CENTER_PX - 4, 2.2, 8);
      drawingContext.fillRect(DISPLAY_CENTER_PX + 1.2, DISPLAY_CENTER_PX - 4, 2.2, 8);
    } else {
      drawingContext.beginPath();
      drawingContext.arc(DISPLAY_CENTER_PX, DISPLAY_CENTER_PX, 3.6, 0, FULL_CIRCLE_RADIANS);
      drawingContext.fillStyle = "#F5F6F8";
      drawingContext.fill();
      drawingContext.beginPath();
      drawingContext.arc(DISPLAY_CENTER_PX, DISPLAY_CENTER_PX, 1.4, 0, FULL_CIRCLE_RADIANS);
      drawingContext.fillStyle = "#060709";
      drawingContext.fill();
    }
    drawingContext.restore();
  }

  const timeAlpha = normalizeRange(seekExpand, 0.25, 0.90);
  if (timeAlpha > 0.02) {
    drawingContext.save();
    drawingContext.globalAlpha = openWaveOne * timeAlpha;
    drawingContext.textAlign = "center";
    drawingContext.textBaseline = "middle";

    drawingContext.font = `700 8px ${FONT_MONO}`;
    drawingContext.fillStyle = palette.signalHighlight;
    drawingContext.fillText("SEEK", DISPLAY_CENTER_PX, DISPLAY_CENTER_PX - 21);

    drawingContext.font = `800 18px ${FONT_MONO}`;
    drawingContext.fillStyle = "#F5F6F8";
    drawingContext.fillText(
      formatClockTime(elapsedSeconds),
      DISPLAY_CENTER_PX,
      DISPLAY_CENTER_PX + 1,
    );

    drawingContext.font = `600 9.5px ${FONT_MONO}`;
    drawingContext.fillStyle = "rgba(245, 246, 248, 0.58)";
    drawingContext.fillText(
      `/ ${formatClockTime(safeDuration)}`,
      DISPLAY_CENTER_PX,
      DISPLAY_CENTER_PX + 20,
    );
    drawingContext.restore();
  }

  drawingContext.restore();
}

function drawNowPlayingVolumeHud(drawingContext, nowPlayingState, openWaveTwo, palette) {
  const volumeLevel = clamp(Number(nowPlayingState.volumeLevel ?? 0), 0, 1);
  const volumeHudAlpha = clamp(Number(nowPlayingState.volumeHudAlpha ?? 0), 0, 1);
  const rimAlpha = volumeHudAlpha;
  const centerRingAlpha = easeOutExpo(volumeHudAlpha);

  if (rimAlpha > LAYER_VISIBILITY_THRESHOLD) {
    const fillStartAngle = lerp(VOLUME_ARC_END_RADIANS, VOLUME_ARC_START_RADIANS, volumeLevel);
    const limitAngle = lerp(
      VOLUME_ARC_END_RADIANS,
      VOLUME_ARC_START_RADIANS,
      NOW_PLAYING_CONSTANTS.VOLUME_LIMIT_RATIO,
    );

    drawingContext.save();
    drawingContext.globalAlpha = rimAlpha;

    drawingContext.beginPath();
    drawingContext.arc(
      DISPLAY_CENTER_PX,
      DISPLAY_CENTER_PX,
      VOLUME_ARC_RADIUS_PX,
      VOLUME_ARC_START_RADIANS - 0.03,
      VOLUME_ARC_END_RADIANS + 0.03,
    );
    drawingContext.strokeStyle = "rgba(6, 7, 9, 0.56)";
    drawingContext.lineWidth = 9;
    drawingContext.lineCap = "butt";
    drawingContext.stroke();

    drawingContext.beginPath();
    drawingContext.arc(
      DISPLAY_CENTER_PX,
      DISPLAY_CENTER_PX,
      VOLUME_ARC_RADIUS_PX,
      VOLUME_ARC_START_RADIANS,
      VOLUME_ARC_END_RADIANS,
    );
    drawingContext.strokeStyle = "rgba(245, 246, 248, 0.28)";
    drawingContext.lineWidth = 3.6;
    drawingContext.stroke();

    if (volumeLevel > ALPHA_VISIBILITY_THRESHOLD) {
      drawingContext.beginPath();
      drawingContext.arc(
        DISPLAY_CENTER_PX,
        DISPLAY_CENTER_PX,
        VOLUME_ARC_RADIUS_PX,
        fillStartAngle,
        VOLUME_ARC_END_RADIANS,
      );
      drawingContext.strokeStyle =
        volumeLevel > NOW_PLAYING_CONSTANTS.VOLUME_LIMIT_RATIO
          ? palette.signalActive
          : palette.signalHighlight;
      drawingContext.lineWidth = 4.2;
      drawingContext.stroke();
    }

    drawingContext.strokeStyle = "rgba(245, 246, 248, 0.75)";
    drawingContext.lineWidth = 1.5;
    drawingContext.beginPath();
    drawingContext.moveTo(
      DISPLAY_CENTER_PX + Math.cos(limitAngle) * (VOLUME_ARC_RADIUS_PX - 5),
      DISPLAY_CENTER_PX + Math.sin(limitAngle) * (VOLUME_ARC_RADIUS_PX - 5),
    );
    drawingContext.lineTo(
      DISPLAY_CENTER_PX + Math.cos(limitAngle) * (VOLUME_ARC_RADIUS_PX + 4),
      DISPLAY_CENTER_PX + Math.sin(limitAngle) * (VOLUME_ARC_RADIUS_PX + 4),
    );
    drawingContext.stroke();

    if (centerRingAlpha > 0.05) {
      const capX = DISPLAY_CENTER_PX + Math.cos(fillStartAngle) * VOLUME_ARC_RADIUS_PX;
      const capY = DISPLAY_CENTER_PX + Math.sin(fillStartAngle) * VOLUME_ARC_RADIUS_PX;
      drawingContext.save();
      drawingContext.translate(capX, capY);
      drawingContext.rotate(fillStartAngle);
      drawingContext.fillStyle = "#F5F6F8";
      drawingContext.fillRect(-3.5, -2.5, 7, 5);
      drawingContext.restore();
    }

    drawingContext.restore();
  }

  if (centerRingAlpha > LAYER_VISIBILITY_THRESHOLD) {
    const ringRadius = lerp(42, 58, centerRingAlpha);
    const percentValue = Math.round(volumeLevel * PERCENT_SCALE);
    const isOverLimit = volumeLevel > NOW_PLAYING_CONSTANTS.VOLUME_LIMIT_RATIO;
    const activeColor = isOverLimit ? palette.signalActive : palette.signalHighlight;

    drawingContext.save();
    drawingContext.globalAlpha = centerRingAlpha;

    drawingContext.beginPath();
    drawingContext.arc(DISPLAY_CENTER_PX, DISPLAY_CENTER_PX, ringRadius, 0, FULL_CIRCLE_RADIANS);
    drawingContext.fillStyle = "rgba(6, 7, 9, 0.92)";
    drawingContext.fill();
    drawingContext.strokeStyle = "rgba(245, 250, 255, 0.32)";
    drawingContext.lineWidth = 1.25;
    drawingContext.stroke();

    drawingContext.beginPath();
    drawingContext.arc(
      DISPLAY_CENTER_PX,
      DISPLAY_CENTER_PX,
      ringRadius - 7,
      0,
      FULL_CIRCLE_RADIANS,
    );
    drawingContext.strokeStyle = "rgba(245, 246, 248, 0.18)";
    drawingContext.lineWidth = 4.2;
    drawingContext.stroke();

    if (volumeLevel > ALPHA_VISIBILITY_THRESHOLD) {
      drawingContext.beginPath();
      drawingContext.arc(
        DISPLAY_CENTER_PX,
        DISPLAY_CENTER_PX,
        ringRadius - 7,
        TOP_ANGLE_RADIANS,
        TOP_ANGLE_RADIANS + volumeLevel * FULL_CIRCLE_RADIANS,
      );
      drawingContext.strokeStyle = activeColor;
      drawingContext.lineWidth = 4.6;
      drawingContext.lineCap = "butt";
      drawingContext.stroke();
    }

    drawingContext.fillStyle = volumeLevel === 0 ? palette.signalActive : "#F5F6F8";
    drawingContext.beginPath();
    drawingContext.moveTo(DISPLAY_CENTER_PX - 6, DISPLAY_CENTER_PX - 23);
    drawingContext.lineTo(DISPLAY_CENTER_PX - 3, DISPLAY_CENTER_PX - 23);
    drawingContext.lineTo(DISPLAY_CENTER_PX + 1, DISPLAY_CENTER_PX - 27);
    drawingContext.lineTo(DISPLAY_CENTER_PX + 1, DISPLAY_CENTER_PX - 15);
    drawingContext.lineTo(DISPLAY_CENTER_PX - 3, DISPLAY_CENTER_PX - 19);
    drawingContext.lineTo(DISPLAY_CENTER_PX - 6, DISPLAY_CENTER_PX - 19);
    drawingContext.closePath();
    drawingContext.fill();

    if (volumeLevel > 0) {
      drawingContext.strokeStyle = activeColor;
      drawingContext.lineWidth = 1.4;
      drawingContext.beginPath();
      drawingContext.arc(DISPLAY_CENTER_PX + 1, DISPLAY_CENTER_PX - 21, 5, -0.7, 0.7);
      drawingContext.stroke();
    }

    drawingContext.textAlign = "center";
    drawingContext.textBaseline = "middle";
    drawingContext.font = `800 22px ${FONT_MONO}`;
    drawingContext.fillStyle = "#F5F6F8";
    drawingContext.fillText(`${percentValue}%`, DISPLAY_CENTER_PX, DISPLAY_CENTER_PX + 4);

    drawingContext.font = `700 8px ${FONT_MONO}`;
    drawingContext.fillStyle = isOverLimit ? palette.signalActive : "rgba(245, 246, 248, 0.58)";
    drawingContext.fillText(
      isOverLimit ? "HIGH OUTPUT" : "VOLUME",
      DISPLAY_CENTER_PX,
      DISPLAY_CENTER_PX + 24,
    );

    drawingContext.restore();
  }
}

function renderNowPlayingQuickActions(
  drawingContext,
  nowPlayingState,
  track,
  albumSpec,
  palette,
  appearance,
  animationTimeSeconds = 0,
) {
  const progress = clamp(Number(nowPlayingState.quickActionsProgress ?? 0), 0, 1);
  if (progress <= LAYER_VISIBILITY_THRESHOLD) {
    return;
  }
  const ease = easeOutExpo(progress);
  const isFavorited = Boolean(nowPlayingState.activeTrack?.isFavorite ?? track?.isFavorite);
  const repeatMode = nowPlayingState.repeatMode || "off";
  const activeRepeatSpec =
    NOW_PLAYING_REPEAT_MODES.find((modeEntry) => modeEntry.id === repeatMode) ||
    NOW_PLAYING_REPEAT_MODES[0];
  const isDark = appearance === "dark";
  const trackDurationSeconds = Math.max(
    1,
    Number(nowPlayingState.trackDurationSeconds || track?.durationSeconds || 1),
  );

  drawingContext.save();

  drawingContext.fillStyle = isDark ? "rgba(6, 7, 9, 0.72)" : "rgba(14, 17, 22, 0.74)";
  drawingContext.globalAlpha = progress;
  drawingContext.fillRect(0, 0, DISPLAY_DIAMETER_PX, DISPLAY_DIAMETER_PX);

  const headerYOffset = lerp(-12, 0, ease);
  drawingContext.save();
  drawingContext.translate(0, headerYOffset);
  drawingContext.globalAlpha = ease;
  drawingContext.textAlign = "center";
  drawingContext.textBaseline = "middle";

  const statusTag = nowPlayingState.isPlaying
    ? `${formatClockTime(nowPlayingState.trackElapsedSeconds)} / ${formatClockTime(trackDurationSeconds)}`
    : `PAUSED \u00b7 ${formatClockTime(nowPlayingState.trackElapsedSeconds)}`;
  drawingContext.font = `700 9px ${FONT_MONO}`;
  drawingContext.fillStyle = palette.signalHighlight;
  drawingContext.fillText(statusTag, DISPLAY_CENTER_PX, 54);

  drawingContext.font = `800 19px ${FONT_DISPLAY}`;
  drawingContext.fillStyle = "#F5F6F8";
  drawMarqueeCanvasText(
    drawingContext,
    track.title || "No Track Loaded",
    DISPLAY_CENTER_PX,
    80,
    224,
    animationTimeSeconds,
  );

  const artistLabel = track.artist || albumSpec?.artist || "Unknown Artist";
  const albumLabel = track.albumTitle || albumSpec?.title || "Unknown Album";
  drawingContext.font = `600 11px ${FONT_DISPLAY}`;
  drawingContext.fillStyle = "rgba(245, 246, 248, 0.74)";
  drawMarqueeCanvasText(
    drawingContext,
    `${artistLabel} \u00b7 ${albumLabel}`,
    DISPLAY_CENTER_PX,
    102,
    244,
    animationTimeSeconds,
  );

  drawingContext.strokeStyle = "rgba(245, 250, 255, 0.16)";
  drawingContext.lineWidth = 1;
  drawingContext.beginPath();
  drawingContext.moveTo(112, 120);
  drawingContext.lineTo(248, 120);
  drawingContext.stroke();
  drawingContext.restore();

  const clusterYOffset = lerp(14, 0, ease);
  drawingContext.save();
  drawingContext.translate(0, clusterYOffset);
  drawingContext.globalAlpha = ease;

  for (let buttonIndex = 0; buttonIndex < NOW_PLAYING_QUICK_BUTTONS.length; buttonIndex += 1) {
    const buttonSpec = NOW_PLAYING_QUICK_BUTTONS[buttonIndex];
    const isPressed = nowPlayingState.pressedActionId === buttonSpec.id;
    const isToggledActive =
      (buttonSpec.id === "favorite" && isFavorited) ||
      (buttonSpec.id === "shuffle" && Boolean(nowPlayingState.shuffleEnabled)) ||
      (buttonSpec.id === "repeat" && repeatMode !== "off") ||
      (buttonSpec.id === "playlist" && nowPlayingState.playlistPickerOpen) ||
      (buttonSpec.id === "lyrics" && nowPlayingState.lyricsOpen) ||
      (buttonSpec.id === "info" && nowPlayingState.detailsSheetOpen);
    const scaleFactor = isPressed ? 0.93 : 1.0;

    drawingContext.save();
    drawingContext.translate(buttonSpec.centerX, buttonSpec.centerY);
    drawingContext.scale(scaleFactor, scaleFactor);

    drawingContext.beginPath();
    drawingContext.arc(0, 0, buttonSpec.radius, 0, FULL_CIRCLE_RADIANS);
    drawingContext.fillStyle = isToggledActive
      ? palette.signalHighlight
      : isPressed
        ? "rgba(245, 250, 255, 0.24)"
        : "rgba(22, 26, 34, 0.86)";
    drawingContext.fill();

    if (!isToggledActive) {
      drawingContext.save();
      drawingContext.beginPath();
      drawingContext.arc(0, 0, buttonSpec.radius, 0, FULL_CIRCLE_RADIANS);
      drawingContext.clip();
      drawingContext.fillStyle = "rgba(255, 255, 255, 0.09)";
      drawingContext.beginPath();
      drawingContext.moveTo(-buttonSpec.radius, -buttonSpec.radius);
      drawingContext.lineTo(0, -buttonSpec.radius);
      drawingContext.lineTo(-buttonSpec.radius * 0.45, buttonSpec.radius);
      drawingContext.lineTo(-buttonSpec.radius, buttonSpec.radius);
      drawingContext.closePath();
      drawingContext.fill();
      drawingContext.restore();
    }

    drawingContext.beginPath();
    drawingContext.arc(0, 0, buttonSpec.radius, 0, FULL_CIRCLE_RADIANS);
    drawingContext.strokeStyle =
      isPressed || isToggledActive ? palette.signalHighlight : "rgba(245, 250, 255, 0.30)";
    drawingContext.lineWidth = isPressed || isToggledActive ? 1.8 : 1.15;
    drawingContext.stroke();

    drawNowPlayingActionIcon(
      drawingContext,
      buttonSpec.id,
      0,
      0,
      10.8,
      isToggledActive,
      repeatMode,
      palette,
    );

    drawingContext.restore();

    const captionText = buttonSpec.id === "repeat" ? activeRepeatSpec.caption : buttonSpec.label;
    drawingContext.font = `700 7.8px ${FONT_MONO}`;
    drawingContext.fillStyle = isToggledActive
      ? palette.signalHighlight
      : "rgba(245, 246, 248, 0.70)";
    drawingContext.textAlign = "center";
    drawingContext.textBaseline = "middle";
    drawingContext.fillText(
      captionText,
      buttonSpec.centerX,
      buttonSpec.centerY + buttonSpec.radius + 11,
    );
  }

  drawingContext.restore();
  drawingContext.restore();
}

function renderNowPlayingPlaylistPicker(
  drawingContext,
  nowPlayingState,
  track,
  palette,
  appearance,
  animationTimeSeconds = 0,
) {
  const progress = clamp(Number(nowPlayingState.playlistPickerProgress ?? 0), 0, 1);
  if (progress <= LAYER_VISIBILITY_THRESHOLD) {
    return;
  }
  const ease = easeOutExpo(progress);
  const isDark = appearance === "dark";
  const playlists = Array.isArray(nowPlayingState.playlists) ? nowPlayingState.playlists : [];

  drawingContext.save();
  drawingContext.fillStyle = isDark ? "rgba(6, 7, 9, 0.80)" : "rgba(18, 21, 27, 0.82)";
  drawingContext.globalAlpha = progress;
  drawingContext.fillRect(0, 0, DISPLAY_DIAMETER_PX, DISPLAY_DIAMETER_PX);

  const cardX = DISPLAY_CENTER_PX - PICKER_CARD_WIDTH_PX / 2;
  const cardY = lerp(114, 78, ease);

  drawingContext.globalAlpha = ease;
  traceChamferRect(drawingContext, cardX, cardY, PICKER_CARD_WIDTH_PX, PICKER_CARD_HEIGHT_PX, 10);
  drawingContext.fillStyle = isDark ? "rgba(18, 21, 27, 0.96)" : "rgba(246, 247, 249, 0.96)";
  drawingContext.fill();
  drawingContext.strokeStyle = isDark ? "rgba(245, 250, 255, 0.34)" : "rgba(46, 50, 56, 0.38)";
  drawingContext.lineWidth = 1.25;
  drawingContext.stroke();

  drawingContext.textAlign = "center";
  drawingContext.textBaseline = "middle";
  drawingContext.font = `700 8.5px ${FONT_MONO}`;
  drawingContext.fillStyle = palette.signalHighlight;
  drawingContext.fillText("ADD TO PLAYLIST", DISPLAY_CENTER_PX, cardY + 18);

  drawingContext.font = `700 13.5px ${FONT_DISPLAY}`;
  drawingContext.fillStyle = palette.inkPrimary;
  drawMarqueeCanvasText(
    drawingContext,
    track.title || "No Track Loaded",
    DISPLAY_CENTER_PX,
    cardY + 37,
    PICKER_CARD_WIDTH_PX - 28,
    animationTimeSeconds,
  );

  const rowX = cardX + 12;
  const rowWidth = PICKER_CARD_WIDTH_PX - 24;
  const visibleCount = Math.min(PICKER_MAX_VISIBLE_ROWS, playlists.length);

  for (let playlistIndex = 0; playlistIndex < visibleCount; playlistIndex += 1) {
    const playlistEntry = playlists[playlistIndex];
    const rowY = cardY + 54 + playlistIndex * PICKER_ROW_PITCH_PX;
    const trackIds = Array.isArray(playlistEntry.trackIds) ? playlistEntry.trackIds : [];
    const alreadyContains = trackIds.includes(track.id);
    const isPressed = nowPlayingState.pressedPlaylistIndex === playlistIndex;

    traceChamferRect(drawingContext, rowX, rowY, rowWidth, PICKER_ROW_HEIGHT_PX, 6);
    drawingContext.fillStyle = isPressed
      ? palette.signalHighlightSoft
      : isDark
        ? "rgba(255, 255, 255, 0.05)"
        : "rgba(46, 50, 56, 0.06)";
    drawingContext.fill();
    drawingContext.strokeStyle =
      isPressed || alreadyContains ? palette.signalHighlight : palette.hairlineStrong;
    drawingContext.lineWidth = isPressed ? 1.4 : 1;
    drawingContext.stroke();

    drawingContext.font = `600 12px ${FONT_DISPLAY}`;
    drawingContext.fillStyle = palette.inkPrimary;
    drawingContext.textAlign = "left";
    drawMarqueeCanvasText(
      drawingContext,
      playlistEntry.name || "Playlist",
      rowX + 12,
      rowY + PICKER_ROW_HEIGHT_PX / 2 + 0.5,
      rowWidth - 52,
      animationTimeSeconds,
    );

    drawingContext.font = `700 9px ${FONT_MONO}`;
    drawingContext.fillStyle = alreadyContains ? palette.signalHighlight : palette.inkSecondary;
    drawingContext.textAlign = "right";
    drawingContext.fillText(
      alreadyContains ? "ADDED" : "+ ADD",
      rowX + rowWidth - 10,
      rowY + PICKER_ROW_HEIGHT_PX / 2 + 0.5,
    );
  }

  drawingContext.textAlign = "center";
  drawingContext.font = `600 8.5px ${FONT_MONO}`;
  drawingContext.fillStyle = "rgba(245, 246, 248, 0.65)";
  drawingContext.fillText(
    "SWIPE LEFT EDGE TO CLOSE",
    DISPLAY_CENTER_PX,
    cardY + PICKER_CARD_HEIGHT_PX + 18,
  );

  drawingContext.restore();
}

function renderNowPlayingTrackDetails(
  drawingContext,
  nowPlayingState,
  trackDetails,
  albumSpec,
  palette,
  appearance,
  coverCache,
  animationTimeSeconds = 0,
) {
  const progress = clamp(Number(nowPlayingState.detailsSheetProgress ?? 0), 0, 1);
  if (progress <= LAYER_VISIBILITY_THRESHOLD) {
    return;
  }
  const ease = easeOutExpo(progress);
  const isDark = appearance === "dark";

  drawingContext.save();

  drawingContext.fillStyle = isDark ? "rgba(6, 7, 9, 0.84)" : "rgba(230, 232, 235, 0.88)";
  drawingContext.globalAlpha = progress;
  drawingContext.fillRect(0, 0, DISPLAY_DIAMETER_PX, DISPLAY_DIAMETER_PX);

  const sheetOffsetY = lerp(84, 0, ease);
  drawingContext.translate(0, sheetOffsetY);
  drawingContext.globalAlpha = ease;

  drawingContext.textAlign = "center";
  drawingContext.textBaseline = "middle";
  drawingContext.font = `700 9px ${FONT_MONO}`;
  drawingContext.fillStyle = palette.signalHighlight;
  drawingContext.fillText("TRACK DETAILS", DISPLAY_CENTER_PX, 54);

  const headerX = 56;
  const headerY = 70;
  const headerWidth = 248;
  const headerHeight = 68;

  traceChamferRect(drawingContext, headerX, headerY, headerWidth, headerHeight, 9);
  drawingContext.fillStyle = isDark ? "rgba(18, 21, 27, 0.94)" : "rgba(255, 255, 255, 0.78)";
  drawingContext.fill();
  drawingContext.strokeStyle = isDark ? "rgba(245, 250, 255, 0.24)" : "rgba(46, 50, 56, 0.26)";
  drawingContext.lineWidth = 1.15;
  drawingContext.stroke();

  const thumbSize = 46;
  const thumbCenterX = headerX + 11 + thumbSize / 2;
  const thumbCenterY = headerY + headerHeight / 2;
  drawingContext.save();
  drawingContext.translate(thumbCenterX, thumbCenterY);
  drawingContext.beginPath();
  drawingContext.rect(-thumbSize / 2, -thumbSize / 2, thumbSize, thumbSize);
  drawingContext.clip();
  drawLibraryAlbumArtworkMotif(
    drawingContext,
    albumSpec,
    thumbSize / 2,
    palette,
    appearance,
    coverCache,
  );
  drawingContext.restore();
  drawingContext.strokeStyle = palette.signalHighlight;
  drawingContext.lineWidth = 1.2;
  drawingContext.strokeRect(
    headerX + 11.5,
    thumbCenterY - thumbSize / 2 + 0.5,
    thumbSize - 1,
    thumbSize - 1,
  );

  const textLeftX = headerX + 68;
  const maxTextWidth = headerWidth - 80;
  drawingContext.textAlign = "left";
  drawingContext.font = `800 14.5px ${FONT_DISPLAY}`;
  drawingContext.fillStyle = palette.inkPrimary;
  drawMarqueeCanvasText(
    drawingContext,
    trackDetails.title || "No Track Loaded",
    textLeftX,
    headerY + 21,
    maxTextWidth,
    animationTimeSeconds,
  );

  drawingContext.font = `600 11px ${FONT_DISPLAY}`;
  drawingContext.fillStyle = palette.inkSecondary;
  drawMarqueeCanvasText(
    drawingContext,
    trackDetails.artist || "Unknown Artist",
    textLeftX,
    headerY + 38,
    maxTextWidth,
    animationTimeSeconds,
  );

  const albumUpper = String(trackDetails.albumTitle || "Unknown Album").toUpperCase();
  const yearSuffix = trackDetails.year ? ` \u00b7 ${trackDetails.year}` : "";
  drawingContext.font = `600 9px ${FONT_MONO}`;
  drawingContext.fillStyle = palette.inkSecondary;
  drawMarqueeCanvasText(
    drawingContext,
    `${albumUpper}${yearSuffix}`,
    textLeftX,
    headerY + 54,
    maxTextWidth,
    animationTimeSeconds,
  );

  const specs = [
    { label: "FORMAT", value: trackDetails.format || "FLAC 16-BIT", accent: true },
    { label: "SAMPLE RATE", value: trackDetails.sampleRate || "44.1 KHZ", accent: false },
    { label: "BITRATE", value: trackDetails.bitrate || "1,024 KBPS", accent: false },
    { label: "DISC TRACK", value: trackDetails.trackIndexLabel || "00 / 00", accent: false },
  ];
  const cellWidth = 120;
  const cellHeight = 48;
  const cellGap = 8;
  const gridLeftX = DISPLAY_CENTER_PX - cellWidth - cellGap / 2;
  const gridTopY = 148;

  for (let specIndex = 0; specIndex < specs.length; specIndex += 1) {
    const specEntry = specs[specIndex];
    const columnIndex = specIndex % 2;
    const rowIndex = Math.floor(specIndex / 2);
    const cellX = gridLeftX + columnIndex * (cellWidth + cellGap);
    const cellY = gridTopY + rowIndex * (cellHeight + cellGap);

    traceChamferRect(drawingContext, cellX, cellY, cellWidth, cellHeight, 7);
    drawingContext.fillStyle = isDark ? "rgba(18, 21, 27, 0.90)" : "rgba(255, 255, 255, 0.72)";
    drawingContext.fill();
    drawingContext.strokeStyle = specEntry.accent ? palette.signalHighlight : palette.hairlineStrong;
    drawingContext.lineWidth = specEntry.accent ? 1.25 : 1;
    drawingContext.stroke();

    drawingContext.textAlign = "left";
    drawingContext.font = `700 8px ${FONT_MONO}`;
    drawingContext.fillStyle = palette.inkSecondary;
    drawingContext.fillText(specEntry.label, cellX + 10, cellY + 15);

    drawingContext.font = `800 12px ${FONT_MONO}`;
    drawingContext.fillStyle = specEntry.accent ? palette.signalHighlight : palette.inkPrimary;
    drawMarqueeCanvasText(
      drawingContext,
      specEntry.value,
      cellX + 10,
      cellY + 33,
      cellWidth - 20,
      animationTimeSeconds,
    );
  }

  const footerY = 264;
  const footerWidth = cellWidth * 2 + cellGap;
  traceChamferRect(drawingContext, gridLeftX, footerY, footerWidth, 30, 6);
  drawingContext.fillStyle = isDark ? "rgba(18, 21, 27, 0.84)" : "rgba(214, 217, 223, 0.84)";
  drawingContext.fill();
  drawingContext.strokeStyle = palette.hairline;
  drawingContext.lineWidth = 1;
  drawingContext.stroke();

  const footerSummary =
    `${String(trackDetails.genre || "Unknown Genre").toUpperCase()}  \u00b7  ` +
    `${trackDetails.fileSize || "0.0 MB"}  \u00b7  ` +
    `${formatClockTime(trackDetails.durationSeconds)}`;
  drawingContext.textAlign = "center";
  drawingContext.font = `700 9.5px ${FONT_MONO}`;
  drawingContext.fillStyle = palette.inkPrimary;
  drawMarqueeCanvasText(
    drawingContext,
    footerSummary,
    DISPLAY_CENTER_PX,
    footerY + 15.5,
    footerWidth - 16,
    animationTimeSeconds,
  );

  drawingContext.restore();
}

function renderNowPlayingQueueSheet(
  drawingContext,
  nowPlayingState,
  queueContext,
  palette,
  appearance,
  animationTimeSeconds = 0,
) {
  const progress = clamp(Number(nowPlayingState.queueSheetProgress ?? 0), 0, 1);
  if (progress <= LAYER_VISIBILITY_THRESHOLD) {
    return;
  }
  const ease = easeOutExpo(progress);
  const isDark = appearance === "dark";
  const tracks =
    Array.isArray(queueContext?.tracks) && queueContext.tracks.length > 0
      ? queueContext.tracks
      : Array.isArray(nowPlayingState.queueTracks)
        ? nowPlayingState.queueTracks
        : [];
  const activeIndex =
    tracks.length > 0
      ? ((Number(nowPlayingState.queueIndex ?? 0) % tracks.length) + tracks.length) % tracks.length
      : 0;
  const badgeText = String(queueContext?.badge || "ALBUM").toUpperCase();
  const titleText = String(queueContext?.title || "Queue");
  const scrollPosition = Number(nowPlayingState.queueScrollPosition ?? 0);

  drawingContext.save();

  drawingContext.fillStyle = isDark ? "rgba(6, 7, 9, 0.84)" : "rgba(230, 232, 235, 0.88)";
  drawingContext.globalAlpha = progress;
  drawingContext.fillRect(0, 0, DISPLAY_DIAMETER_PX, DISPLAY_DIAMETER_PX);

  const sheetOffsetY = lerp(88, 0, ease);
  drawingContext.translate(0, sheetOffsetY);
  drawingContext.globalAlpha = ease;

  drawingContext.fillStyle = palette.hairlineStrong;
  drawingContext.fillRect(DISPLAY_CENTER_PX - 16, 26, 32, 3);

  drawingContext.textAlign = "center";
  drawingContext.textBaseline = "middle";
  drawingContext.font = `700 8.5px ${FONT_MONO}`;
  drawingContext.fillStyle = palette.signalHighlight;
  drawingContext.fillText(`${badgeText} QUEUE`, DISPLAY_CENTER_PX, 44);

  drawingContext.font = `800 18.5px ${FONT_DISPLAY}`;
  drawingContext.fillStyle = palette.inkPrimary;
  drawMarqueeCanvasText(
    drawingContext,
    titleText,
    DISPLAY_CENTER_PX,
    67,
    218,
    animationTimeSeconds,
  );

  const activePositionLabel = tracks.length > 0 ? String(activeIndex + 1).padStart(2, "0") : "00";
  const totalCountLabel = String(tracks.length).padStart(2, "0");
  const queueModeFlags = [];
  if (nowPlayingState.shuffleEnabled) {
    queueModeFlags.push("SHUFFLE");
  }
  if (nowPlayingState.repeatMode === "all") {
    queueModeFlags.push("REP ALL");
  } else if (nowPlayingState.repeatMode === "one") {
    queueModeFlags.push("REP 1");
  }
  const queueSubtitleSuffix = queueModeFlags.length > 0 ? queueModeFlags.join(" \u00b7 ") : "TAP TO PLAY";
  drawingContext.font = `600 9px ${FONT_MONO}`;
  drawingContext.fillStyle = palette.inkSecondary;
  drawingContext.fillText(
    `TRACK ${activePositionLabel} OF ${totalCountLabel} \u00b7 ${queueSubtitleSuffix}`,
    DISPLAY_CENTER_PX,
    87,
  );

  drawingContext.save();
  drawingContext.beginPath();
  drawingContext.rect(0, QUEUE_LIST_CLIP_TOP_Y, DISPLAY_DIAMETER_PX, QUEUE_LIST_CLIP_HEIGHT_PX);
  drawingContext.clip();

  if (tracks.length > 0) {
    const queueStartRow = Math.max(0, Math.floor(scrollPosition) - 2);
    const queueEndRow = Math.min(tracks.length - 1, Math.ceil(scrollPosition) + 4);
    for (let rowIndex = queueStartRow; rowIndex <= queueEndRow; rowIndex += 1) {
      const layout = computeNowPlayingQueueRowLayout(rowIndex, scrollPosition);
      if (layout.alpha <= 0.02 || layout.rowHalfWidth < 46) {
        continue;
      }
      const queueTrack = tracks[rowIndex];
      const isCurrentTrack = rowIndex === activeIndex;
      const isPressed = nowPlayingState.pressedQueueRowIndex === rowIndex;
      const rowLeft = DISPLAY_CENTER_PX - layout.rowHalfWidth;
      const rowTop = layout.rowY - layout.rowHeight / 2;
      const rowWidth = layout.rowHalfWidth * 2;

      drawingContext.save();
      drawingContext.globalAlpha = ease * layout.alpha;

      traceChamferRect(drawingContext, rowLeft, rowTop, rowWidth, layout.rowHeight, 8);
      drawingContext.fillStyle = isPressed
        ? palette.signalHighlightSoft
        : isDark
          ? "rgba(18, 22, 30, 0.88)"
          : "rgba(255, 255, 255, 0.72)";
      drawingContext.fill();

      drawingContext.save();
      traceChamferRect(drawingContext, rowLeft, rowTop, rowWidth, layout.rowHeight, 8);
      drawingContext.clip();
      drawingContext.fillStyle = isDark ? "rgba(255, 255, 255, 0.038)" : "rgba(255, 255, 255, 0.34)";
      drawingContext.beginPath();
      drawingContext.moveTo(rowLeft, rowTop);
      drawingContext.lineTo(rowLeft + rowWidth * 0.38, rowTop);
      drawingContext.lineTo(rowLeft + rowWidth * 0.14, rowTop + layout.rowHeight);
      drawingContext.lineTo(rowLeft, rowTop + layout.rowHeight);
      drawingContext.closePath();
      drawingContext.fill();

      if (isCurrentTrack) {
        drawingContext.fillStyle = palette.signalHighlight;
        drawingContext.fillRect(rowLeft, rowTop + 8, 3, layout.rowHeight - 16);
      }
      drawingContext.restore();

      traceChamferRect(drawingContext, rowLeft, rowTop, rowWidth, layout.rowHeight, 8);
      drawingContext.strokeStyle = isPressed
        ? palette.signalHighlight
        : isCurrentTrack
          ? isDark
            ? "rgba(192, 254, 5, 0.55)"
            : "rgba(232, 90, 36, 0.58)"
          : isDark
            ? "rgba(245, 250, 255, 0.18)"
            : "rgba(46, 50, 56, 0.22)";
      drawingContext.lineWidth = isPressed ? 1.45 : 1;
      drawingContext.stroke();

      if (isPressed) {
        drawLibraryCornerBrackets(
          drawingContext,
          rowLeft - 2,
          rowTop - 2,
          rowWidth + 4,
          layout.rowHeight + 4,
          7,
          1.6,
          palette.signalHighlight,
          0.95,
        );
      }

      const leftSlotX = rowLeft + 21;
      if (isCurrentTrack && nowPlayingState.isPlaying) {
        drawLiveMiniEqualizer(
          drawingContext,
          leftSlotX,
          layout.rowY,
          Number(nowPlayingState.idleClockSeconds ?? 0),
          palette,
        );
      } else {
        drawingContext.font = `700 10px ${FONT_MONO}`;
        drawingContext.fillStyle = isCurrentTrack ? palette.signalHighlight : palette.inkSecondary;
        drawingContext.textAlign = "center";
        drawingContext.fillText(String(rowIndex + 1).padStart(2, "0"), leftSlotX, layout.rowY + 0.5);
      }

      const textX = rowLeft + 38;
      const maxTextWidth = rowWidth - 86;
      drawingContext.textAlign = "left";
      drawingContext.font = `${isCurrentTrack ? "800" : "700"} 13px ${FONT_DISPLAY}`;
      drawingContext.fillStyle = isCurrentTrack ? palette.signalHighlight : palette.inkPrimary;
      drawMarqueeCanvasText(
        drawingContext,
        queueTrack.title || "Untitled",
        textX,
        layout.rowY - 6.5,
        maxTextWidth,
        animationTimeSeconds,
      );

      const artistLabel = queueTrack.artist || "Unknown Artist";
      const secondaryLine =
        queueContext?.sourceId === "album"
          ? artistLabel
          : queueTrack.albumTitle
            ? `${artistLabel} \u00b7 ${queueTrack.albumTitle}`
            : artistLabel;
      drawingContext.font = `600 9.5px ${FONT_DISPLAY}`;
      drawingContext.fillStyle = palette.inkSecondary;
      drawMarqueeCanvasText(
        drawingContext,
        secondaryLine,
        textX,
        layout.rowY + 9.5,
        maxTextWidth,
        animationTimeSeconds,
      );

      const durationLabel =
        typeof queueTrack.duration === "string" && queueTrack.duration.length > 0
          ? queueTrack.duration
          : formatClockTime(queueTrack.durationSeconds);
      drawingContext.textAlign = "right";
      drawingContext.font = `600 9.5px ${FONT_MONO}`;
      drawingContext.fillStyle = isCurrentTrack ? palette.inkPrimary : palette.inkSecondary;
      drawingContext.fillText(durationLabel, rowLeft + rowWidth - 11, layout.rowY + 0.5);

      drawingContext.restore();
    }
  }
  drawingContext.restore();

  if (tracks.length > 2) {
    drawLibraryScrollArc(drawingContext, scrollPosition, tracks.length, ease * 0.9, palette);
  }

  drawingContext.textAlign = "center";
  drawingContext.font = `600 8.5px ${FONT_MONO}`;
  drawingContext.fillStyle = palette.inkSecondary;
  drawingContext.fillText("SWIPE DOWN OR LEFT EDGE TO CLOSE", DISPLAY_CENTER_PX, 326);

  drawingContext.restore();
}

function drawNowPlayingModeMedallionIcon(
  drawingContext,
  modeId,
  centerX,
  centerY,
  clockSeconds,
  palette,
  appearance,
) {
  const isDark = appearance === "dark";
  const accent = palette.signalHighlight;
  const ink = palette.inkPrimary;

  drawingContext.save();
  drawingContext.translate(centerX, centerY);

  const breathe = 1 + Math.sin(clockSeconds * 2.1) * 0.03;
  drawingContext.save();
  drawingContext.scale(breathe, breathe);
  drawingContext.beginPath();
  drawingContext.arc(0, 0, 40, 0, FULL_CIRCLE_RADIANS);
  drawingContext.fillStyle = isDark ? "rgba(18, 21, 27, 0.90)" : "rgba(255, 255, 255, 0.72)";
  drawingContext.fill();
  drawingContext.strokeStyle = isDark ? "rgba(245, 250, 255, 0.28)" : "rgba(46, 50, 56, 0.28)";
  drawingContext.lineWidth = 1.25;
  drawingContext.stroke();

  const arcPhase = clockSeconds * 0.85;
  drawingContext.beginPath();
  drawingContext.arc(0, 0, 44, arcPhase, arcPhase + Math.PI * 0.42);
  drawingContext.strokeStyle = accent;
  drawingContext.lineWidth = 2;
  drawingContext.lineCap = "butt";
  drawingContext.stroke();
  drawingContext.restore();

  if (modeId === "usb-dac") {
    const barCount = 5;
    const barWidth = 5;
    const barGap = 4;
    const totalWidth = barCount * barWidth + (barCount - 1) * barGap;
    for (let barIndex = 0; barIndex < barCount; barIndex += 1) {
      const amplitude =
        0.25 +
        0.75 * (0.5 + 0.5 * Math.sin(clockSeconds * (4.2 + barIndex * 0.7) + barIndex * 1.15));
      const barHeight = 10 + amplitude * 26;
      const barX = -totalWidth / 2 + barIndex * (barWidth + barGap);
      drawingContext.fillStyle = barIndex === 2 ? accent : ink;
      drawingContext.fillRect(barX, -barHeight / 2, barWidth, barHeight);
    }
  } else if (modeId === "bt-rx") {
    for (let waveIndex = 0; waveIndex < 3; waveIndex += 1) {
      const phase = (clockSeconds * 0.75 + waveIndex * 0.33) % 1;
      const waveRadius = 9 + phase * 22;
      const waveAlpha = (1 - phase) * 0.9;
      drawingContext.save();
      drawingContext.globalAlpha = waveAlpha;
      drawingContext.strokeStyle = waveIndex === 0 ? accent : ink;
      drawingContext.lineWidth = 1.8;
      drawingContext.beginPath();
      drawingContext.arc(0, 0, waveRadius, -Math.PI * 0.36, Math.PI * 0.36);
      drawingContext.stroke();
      drawingContext.beginPath();
      drawingContext.arc(0, 0, waveRadius, Math.PI * 0.64, Math.PI * 1.36);
      drawingContext.stroke();
      drawingContext.restore();
    }
    drawingContext.fillStyle = accent;
    drawingContext.fillRect(-4.5, -4.5, 9, 9);
  } else if (modeId === "usb-storage") {
    drawingContext.strokeStyle = ink;
    drawingContext.lineWidth = 1.8;
    drawingContext.strokeRect(-16, -18, 32, 36);
    drawingContext.fillStyle = isDark ? "rgba(245, 246, 248, 0.14)" : "rgba(46, 50, 56, 0.12)";
    drawingContext.fillRect(-16, -18, 32, 36);

    drawingContext.fillStyle = ink;
    drawingContext.fillRect(-10, -10, 20, 3);
    drawingContext.fillRect(-10, -3, 14, 3);

    const pulseX = -10 + ((clockSeconds * 14) % 14);
    drawingContext.fillStyle = accent;
    drawingContext.fillRect(pulseX, 7, 8, 5);
  } else {
    drawingContext.beginPath();
    drawingContext.arc(-3, 2, 20, 0, FULL_CIRCLE_RADIANS);
    drawingContext.fillStyle = isDark ? "#13171F" : "#282D36";
    drawingContext.fill();
    drawingContext.strokeStyle = ink;
    drawingContext.lineWidth = 1.4;
    drawingContext.stroke();

    drawingContext.beginPath();
    drawingContext.arc(-3, 2, 13, -0.8, 0.9);
    drawingContext.strokeStyle = "rgba(245, 246, 248, 0.30)";
    drawingContext.lineWidth = 1.1;
    drawingContext.stroke();

    drawingContext.beginPath();
    drawingContext.arc(-3, 2, 5.5, 0, FULL_CIRCLE_RADIANS);
    drawingContext.fillStyle = accent;
    drawingContext.fill();
    drawingContext.beginPath();
    drawingContext.arc(-3, 2, 2, 0, FULL_CIRCLE_RADIANS);
    drawingContext.fillStyle = "#060709";
    drawingContext.fill();

    drawingContext.strokeStyle = ink;
    drawingContext.lineWidth = 2;
    drawingContext.beginPath();
    drawingContext.moveTo(20, -16);
    drawingContext.lineTo(18, 6);
    drawingContext.lineTo(11, 14);
    drawingContext.stroke();
    drawingContext.fillStyle = accent;
    drawingContext.fillRect(17, -19, 6, 6);
  }

  drawingContext.restore();
}

function renderNowPlayingIdleOrMode(drawingContext, nowPlayingState, palette, appearance) {
  const modeId = nowPlayingState.operatingMode || "idle";
  const clockSeconds = Number(nowPlayingState.idleClockSeconds ?? 0);
  const isDark = appearance === "dark";

  drawLibraryPerspectiveFloor(drawingContext, clockSeconds * 0.08, 1, palette, appearance);

  drawingContext.save();
  drawingContext.lineCap = "round";
  drawingContext.beginPath();
  drawingContext.arc(
    DISPLAY_CENTER_PX,
    DISPLAY_CENTER_PX,
    RIM_SHEEN_RADIUS_PX,
    -Math.PI * 0.84,
    -Math.PI * 0.30,
  );
  drawingContext.strokeStyle = isDark ? "rgba(235, 244, 255, 0.15)" : "rgba(255, 255, 255, 0.72)";
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
  drawingContext.strokeStyle = isDark ? "rgba(192, 254, 5, 0.20)" : "rgba(232, 90, 36, 0.24)";
  drawingContext.lineWidth = 1.5;
  drawingContext.stroke();

  const markOffset = REGISTRATION_MARK_RADIUS_PX * DIAGONAL_MARK_COS_45;
  drawDiagonalMark(
    drawingContext,
    DISPLAY_CENTER_PX - markOffset,
    DISPLAY_CENTER_PX - markOffset,
    4,
    1.1,
    palette.hairlineStrong,
    0.55,
  );
  drawDiagonalMark(
    drawingContext,
    DISPLAY_CENTER_PX + markOffset,
    DISPLAY_CENTER_PX + markOffset,
    4,
    1.1,
    palette.hairlineStrong,
    0.55,
  );
  drawingContext.restore();

  const medallionY = modeId === "idle" ? 114 : 104;
  drawNowPlayingModeMedallionIcon(
    drawingContext,
    modeId,
    DISPLAY_CENTER_PX,
    medallionY,
    clockSeconds,
    palette,
    appearance,
  );

  const modeCopy = MODE_COPY_MAP[modeId] || MODE_COPY_MAP.idle;

  drawingContext.save();
  drawingContext.textAlign = "center";
  drawingContext.textBaseline = "middle";

  drawingContext.font = `800 22px ${FONT_DISPLAY}`;
  drawingContext.fillStyle = palette.inkPrimary;
  drawingContext.fillText(modeCopy.title, DISPLAY_CENTER_PX, modeId === "idle" ? 186 : 174);

  drawingContext.font = `600 9.5px ${FONT_MONO}`;
  drawingContext.fillStyle = palette.inkSecondary;
  drawingContext.fillText(modeCopy.subtitle, DISPLAY_CENTER_PX, modeId === "idle" ? 210 : 197);

  if (modeId === "usb-dac") {
    const meterWidth = 152;
    const meterX = DISPLAY_CENTER_PX - meterWidth / 2;
    const leftLevel = 0.52 + 0.36 * (0.5 + 0.5 * Math.sin(clockSeconds * 5.2));
    const rightLevel = 0.48 + 0.38 * (0.5 + 0.5 * Math.sin(clockSeconds * 4.6 + 0.8));

    drawingContext.font = `700 8px ${FONT_MONO}`;
    drawingContext.fillStyle = palette.inkSecondary;
    drawingContext.textAlign = "right";
    drawingContext.fillText("L", meterX - 7, 217.5);
    drawingContext.fillText("R", meterX - 7, 227.5);

    drawingContext.fillStyle = palette.hairline;
    drawingContext.fillRect(meterX, 215, meterWidth, 4);
    drawingContext.fillRect(meterX, 225, meterWidth, 4);

    drawingContext.fillStyle = palette.signalHighlight;
    drawingContext.fillRect(meterX, 215, meterWidth * leftLevel, 4);
    drawingContext.fillRect(meterX, 225, meterWidth * rightLevel, 4);
  } else if (modeId === "bt-rx" || modeId === "usb-storage") {
    const barWidth = 148;
    const barX = DISPLAY_CENTER_PX - barWidth / 2;
    const sweepRatio = (clockSeconds * (modeId === "bt-rx" ? 0.65 : 0.45)) % 1;
    drawingContext.fillStyle = palette.hairline;
    drawingContext.fillRect(barX, 218, barWidth, 4);
    drawingContext.fillStyle = palette.signalHighlight;
    drawingContext.fillRect(barX + sweepRatio * (barWidth - 38), 218, 38, 4);
  }

  const buttonWidth = 166;
  const buttonHeight = 36;
  const buttonX = DISPLAY_CENTER_PX - buttonWidth / 2;
  const buttonY = 248;
  const isCtaPressed = Boolean(nowPlayingState.pressedCta);

  traceChamferRect(drawingContext, buttonX, buttonY, buttonWidth, buttonHeight, 8);
  if (modeCopy.primaryCta) {
    drawingContext.fillStyle = palette.signalHighlight;
    drawingContext.fill();
  } else {
    drawingContext.fillStyle = isCtaPressed
      ? palette.signalHighlightSoft
      : isDark
        ? "rgba(18, 21, 27, 0.92)"
        : "rgba(255, 255, 255, 0.76)";
    drawingContext.fill();
    drawingContext.strokeStyle = isCtaPressed ? palette.signalHighlight : palette.inkPrimary;
    drawingContext.lineWidth = 1.4;
    drawingContext.stroke();
  }

  drawLibraryCornerBrackets(
    drawingContext,
    buttonX - 3,
    buttonY - 3,
    buttonWidth + 6,
    buttonHeight + 6,
    7,
    1.5,
    palette.signalHighlight,
    modeCopy.primaryCta || isCtaPressed ? 0.9 : 0.35,
  );

  drawingContext.textAlign = "center";
  drawingContext.font = `800 10.5px ${FONT_MONO}`;
  drawingContext.fillStyle = modeCopy.primaryCta ? palette.signalHighlightInk : palette.inkPrimary;
  drawingContext.fillText(
    modeCopy.ctaLabel,
    DISPLAY_CENTER_PX,
    buttonY + buttonHeight / 2 + 0.5,
  );

  drawingContext.restore();
}

function drawLyricsIsometricField(
  drawingContext,
  rotationAngle,
  ease,
  contentAlpha,
  lyricsFrame,
  palette,
) {
  const driftPhase =
    (((rotationAngle * LYRICS_FIELD_DRIFT_FACTOR) % 1) + 1) % 1;

  drawingContext.save();
  drawingContext.beginPath();
  drawingContext.arc(
    DISPLAY_CENTER_PX,
    DISPLAY_CENTER_PX,
    SAFE_CLIP_RADIUS_PX,
    0,
    FULL_CIRCLE_RADIANS,
  );
  drawingContext.clip();

  drawingContext.strokeStyle = "rgba(245, 250, 255, 0.085)";
  drawingContext.lineWidth = 1;
  drawingContext.globalAlpha = ease;

  for (
    let rayIndex = -LYRICS_FIELD_RAY_HALF_COUNT;
    rayIndex <= LYRICS_FIELD_RAY_HALF_COUNT;
    rayIndex += 1
  ) {
    const topX = DISPLAY_CENTER_PX + rayIndex * LYRICS_FIELD_TOP_RAY_STEP_PX;
    const bottomX = DISPLAY_CENTER_PX + rayIndex * LYRICS_FIELD_BOTTOM_RAY_STEP_PX;
    drawingContext.beginPath();
    drawingContext.moveTo(topX, LYRICS_FIELD_LOWER_HORIZON_Y_PX);
    drawingContext.lineTo(bottomX, LYRICS_FIELD_LOWER_EDGE_Y_PX);
    drawingContext.stroke();

    drawingContext.beginPath();
    drawingContext.moveTo(topX, LYRICS_FIELD_UPPER_HORIZON_Y_PX);
    drawingContext.lineTo(bottomX, LYRICS_FIELD_UPPER_EDGE_Y_PX);
    drawingContext.stroke();
  }

  for (let rowIndex = 0; rowIndex < LYRICS_FIELD_DEPTH_STEPS; rowIndex += 1) {
    const rawDepth = ((rowIndex + driftPhase) % LYRICS_FIELD_DEPTH_STEPS) / LYRICS_FIELD_DEPTH_STEPS;
    const curvedDepth = rawDepth * rawDepth;
    const lowerY = lerp(
      LYRICS_FIELD_LOWER_HORIZON_Y_PX,
      LYRICS_FIELD_LOWER_EDGE_Y_PX,
      curvedDepth,
    );
    const upperY = lerp(
      LYRICS_FIELD_UPPER_HORIZON_Y_PX,
      LYRICS_FIELD_UPPER_EDGE_Y_PX,
      curvedDepth,
    );
    drawingContext.globalAlpha = ease * (0.05 + curvedDepth * 0.07);
    drawingContext.beginPath();
    drawingContext.moveTo(LYRICS_FIELD_LINE_MARGIN_PX, lowerY);
    drawingContext.lineTo(DISPLAY_DIAMETER_PX - LYRICS_FIELD_LINE_MARGIN_PX, lowerY);
    drawingContext.moveTo(LYRICS_FIELD_LINE_MARGIN_PX, upperY);
    drawingContext.lineTo(DISPLAY_DIAMETER_PX - LYRICS_FIELD_LINE_MARGIN_PX, upperY);
    drawingContext.stroke();
  }
  drawingContext.restore();

  const vignetteCenterY = DISPLAY_CENTER_PX + LYRICS_VIGNETTE_CENTER_Y_OFFSET_PX;
  const centerVignette = drawingContext.createRadialGradient(
    DISPLAY_CENTER_PX,
    vignetteCenterY,
    LYRICS_VIGNETTE_INNER_RADIUS_PX,
    DISPLAY_CENTER_PX,
    vignetteCenterY,
    LYRICS_VIGNETTE_OUTER_RADIUS_PX,
  );
  centerVignette.addColorStop(0, "rgba(6, 7, 10, 0.62)");
  centerVignette.addColorStop(0.68, "rgba(6, 7, 10, 0.30)");
  centerVignette.addColorStop(1, "rgba(6, 7, 10, 0)");
  drawingContext.save();
  drawingContext.globalAlpha = ease;
  drawingContext.fillStyle = centerVignette;
  drawingContext.fillRect(0, 0, DISPLAY_DIAMETER_PX, DISPLAY_DIAMETER_PX);
  drawingContext.restore();

  const hasAccentedIndex =
    lyricsFrame.kind !== "none" &&
    !lyricsFrame.isIntroCue &&
    Number.isInteger(lyricsFrame.activeIndex) &&
    lyricsFrame.activeIndex >= 0;
  const accentedMarkIndex = hasAccentedIndex
    ? lyricsFrame.activeIndex % LYRICS_OUTER_CROSSHAIRS.length
    : -1;

  for (let markIndex = 0; markIndex < LYRICS_OUTER_CROSSHAIRS.length; markIndex += 1) {
    const marker = LYRICS_OUTER_CROSSHAIRS[markIndex];
    const isAccented = markIndex === accentedMarkIndex;
    const markerColor = isAccented ? palette.signalHighlight : "rgba(245, 250, 255, 0.65)";
    const markerAlpha = contentAlpha * (isAccented ? 0.68 : 0.24);
    if (marker.kind === "plus") {
      drawCrosshairMark(
        drawingContext,
        marker.centerX,
        marker.centerY,
        LYRICS_CROSSHAIR_PLUS_HALF_LENGTH_PX,
        LYRICS_CROSSHAIR_LINE_WIDTH_PX,
        markerColor,
        markerAlpha,
      );
    } else {
      drawDiagonalMark(
        drawingContext,
        marker.centerX,
        marker.centerY,
        LYRICS_CROSSHAIR_CROSS_HALF_SPAN_PX,
        LYRICS_CROSSHAIR_LINE_WIDTH_PX,
        markerColor,
        markerAlpha,
      );
    }
  }
}

function drawLyricsHorizonDivider(drawingContext, activeBlockBottomY, contentAlpha) {
  const dividerY = clamp(
    activeBlockBottomY + LYRICS_DIVIDER_OFFSET_Y_PX,
    LYRICS_DIVIDER_MIN_Y_PX,
    LYRICS_DIVIDER_MAX_Y_PX,
  );
  drawingContext.save();
  drawingContext.globalAlpha = contentAlpha;
  drawingContext.strokeStyle = "rgba(245, 250, 255, 0.14)";
  drawingContext.lineWidth = 1;
  drawingContext.beginPath();
  drawingContext.moveTo(LYRICS_DIVIDER_LEFT_X_PX, dividerY);
  drawingContext.lineTo(LYRICS_DIVIDER_RIGHT_X_PX, dividerY);
  drawingContext.stroke();
  drawingContext.restore();

  drawDiagonalMark(
    drawingContext,
    LYRICS_DIVIDER_LEFT_CAP_X_PX,
    dividerY,
    LYRICS_DIVIDER_CAP_SPAN_PX,
    1,
    "rgba(245, 250, 255, 0.34)",
    contentAlpha,
  );
  drawDiagonalMark(
    drawingContext,
    LYRICS_DIVIDER_RIGHT_CAP_X_PX,
    dividerY,
    LYRICS_DIVIDER_CAP_SPAN_PX,
    1,
    "rgba(245, 250, 255, 0.34)",
    contentAlpha,
  );
}

function renderNowPlayingLyricsOverlay(
  drawingContext,
  nowPlayingState,
  track,
  albumSpec,
  palette,
  appearance,
  animationTimeSeconds = 0,
) {
  const progress = clamp(Number(nowPlayingState.lyricsProgress ?? 0), 0, 1);
  if (progress <= LAYER_VISIBILITY_THRESHOLD) {
    return;
  }
  const ease = easeInOutCubic(progress);
  const isDark = appearance === "dark";
  const motifKey =
    typeof albumSpec?.motif === "string" && albumSpec.motif in NOW_PLAYING_MOTIF_GLOW_PALETTES
      ? albumSpec.motif
      : "prism";
  const glowPalette = NOW_PLAYING_MOTIF_GLOW_PALETTES[motifKey];
  const primaryGlowRgb = isDark ? glowPalette.primaryRgb : glowPalette.lightPrimaryRgb;
  const secondaryGlowRgb = glowPalette.secondaryRgb;
  const rotationAngle = Number(nowPlayingState.vinylRotationRadians ?? 0);
  const clockSeconds = Number(nowPlayingState.idleClockSeconds ?? 0);
  const elapsedSeconds = Number(nowPlayingState.trackElapsedSeconds ?? 0);
  const safeDurationSeconds = Math.max(
    1,
    Number(nowPlayingState.trackDurationSeconds || track?.durationSeconds || 1),
  );
  const lyricsFrame = nowPlayingState.lyricsFrame || {
    kind: "none",
    modeLabel: "NO LYRICS",
    note: "NO EMBEDDED OR .LRC FILE",
    activeIndex: -1,
    activeText: "",
    upcomingText: "",
    lineProgress: 0,
    isIntroCue: false,
  };
  const activeLyricText = lyricsFrame.activeText || lyricsFrame.currentText || "";

  drawingContext.save();

  const frostedVeil = drawingContext.createRadialGradient(
    DISPLAY_CENTER_PX,
    DISPLAY_CENTER_PX,
    24,
    DISPLAY_CENTER_PX,
    DISPLAY_CENTER_PX,
    DISPLAY_CENTER_PX,
  );
  frostedVeil.addColorStop(0, isDark ? "rgba(6, 8, 12, 0.74)" : "rgba(10, 13, 18, 0.75)");
  frostedVeil.addColorStop(0.7, isDark ? "rgba(6, 7, 10, 0.85)" : "rgba(8, 10, 14, 0.86)");
  frostedVeil.addColorStop(1, "rgba(5, 6, 9, 0.93)");
  drawingContext.globalAlpha = ease;
  drawingContext.fillStyle = frostedVeil;
  drawingContext.fillRect(0, 0, DISPLAY_DIAMETER_PX, DISPLAY_DIAMETER_PX);

  const primaryGlowX = DISPLAY_CENTER_PX + Math.cos(rotationAngle * 0.45 - 0.8) * 58;
  const primaryGlowY = DISPLAY_CENTER_PX + Math.sin(rotationAngle * 0.45 - 0.8) * 54;
  const primaryGlow = drawingContext.createRadialGradient(
    primaryGlowX,
    primaryGlowY,
    8,
    primaryGlowX,
    primaryGlowY,
    152,
  );
  primaryGlow.addColorStop(0, `rgba(${primaryGlowRgb}, 0.19)`);
  primaryGlow.addColorStop(0.55, `rgba(${primaryGlowRgb}, 0.057)`);
  primaryGlow.addColorStop(1, `rgba(${primaryGlowRgb}, 0)`);
  drawingContext.fillStyle = primaryGlow;
  drawingContext.fillRect(0, 0, DISPLAY_DIAMETER_PX, DISPLAY_DIAMETER_PX);

  const secondaryGlowX = DISPLAY_CENTER_PX + Math.cos(rotationAngle * 0.35 + 2.2) * 68;
  const secondaryGlowY = DISPLAY_CENTER_PX + Math.sin(rotationAngle * 0.35 + 2.2) * 62;
  const secondaryGlow = drawingContext.createRadialGradient(
    secondaryGlowX,
    secondaryGlowY,
    8,
    secondaryGlowX,
    secondaryGlowY,
    146,
  );
  secondaryGlow.addColorStop(0, `rgba(${secondaryGlowRgb}, 0.228)`);
  secondaryGlow.addColorStop(0.6, `rgba(${secondaryGlowRgb}, 0.066)`);
  secondaryGlow.addColorStop(1, `rgba(${secondaryGlowRgb}, 0)`);
  drawingContext.fillStyle = secondaryGlow;
  drawingContext.fillRect(0, 0, DISPLAY_DIAMETER_PX, DISPLAY_DIAMETER_PX);

  const volumeHudAlpha = clamp(Number(nowPlayingState.volumeHudAlpha ?? 0), 0, 1);

  const overlayOccupancy = Math.max(
    Number(nowPlayingState.quickActionsProgress ?? 0) * 0.85,
    Number(nowPlayingState.queueSheetProgress ?? 0),
    Number(nowPlayingState.detailsSheetProgress ?? 0),
    Number(nowPlayingState.playlistPickerProgress ?? 0),
    Number(nowPlayingState.seekExpandProgress ?? 0) * 0.72,
    volumeHudAlpha * 0.65,
  );
  const contentAlpha = ease * clamp(1 - overlayOccupancy, 0.12, 1);

  drawLyricsIsometricField(
    drawingContext,
    rotationAngle,
    ease,
    contentAlpha,
    lyricsFrame,
    palette,
  );

  drawingContext.save();
  drawingContext.globalAlpha = contentAlpha;
  drawingContext.textAlign = "center";
  drawingContext.textBaseline = "middle";

  const headerLiftY = lerp(-8, 0, ease);
  drawingContext.font = `700 11px ${FONT_DISPLAY}`;
  drawingContext.fillStyle = "rgba(245, 246, 248, 0.76)";
  drawMarqueeCanvasText(
    drawingContext,
    track.title || "No Track Loaded",
    DISPLAY_CENTER_PX,
    50 + headerLiftY,
    186,
    animationTimeSeconds,
  );

  const syncTag = `${nowPlayingState.isPlaying ? lyricsFrame.modeLabel : "PAUSED"} \u00b7 ${formatClockTime(elapsedSeconds)}`;
  drawingContext.font = `700 8.5px ${FONT_MONO}`;
  const tagWidth = drawingContext.measureText(syncTag).width;
  const dotX = DISPLAY_CENTER_PX - tagWidth / 2 - 7;
  const dotY = 66 + headerLiftY;

  drawingContext.fillStyle =
    lyricsFrame.kind === "none"
      ? "rgba(245, 246, 248, 0.40)"
      : nowPlayingState.isPlaying
        ? palette.signalHighlight
        : "rgba(245, 246, 248, 0.50)";
  drawingContext.fillRect(dotX - 2, dotY - 2, 4, 4);

  drawingContext.fillStyle = "rgba(245, 246, 248, 0.56)";
  drawingContext.fillText(syncTag, DISPLAY_CENTER_PX + 3, dotY);
  drawingContext.restore();

  if (lyricsFrame.kind === "none") {
    const breathe = 0.5 + 0.5 * Math.sin(clockSeconds * 2.1);
    const medallionY = NO_LYRICS_MEDALLION_CENTER_Y_PX + lerp(10, 0, ease);

    drawingContext.save();
    drawingContext.globalAlpha = contentAlpha;

    drawingContext.beginPath();
    drawingContext.arc(
      DISPLAY_CENTER_PX,
      medallionY,
      NO_LYRICS_MEDALLION_BASE_RADIUS_PX + breathe * 1.5,
      0,
      FULL_CIRCLE_RADIANS,
    );
    drawingContext.fillStyle = "rgba(18, 22, 30, 0.72)";
    drawingContext.fill();
    drawingContext.strokeStyle = "rgba(245, 250, 255, 0.22)";
    drawingContext.lineWidth = 1.2;
    drawingContext.stroke();

    drawLyricsBubbleGlyph(
      drawingContext,
      DISPLAY_CENTER_PX,
      medallionY,
      NO_LYRICS_ICON_RADIUS_PX,
      {
        bubbleColor: "rgba(245, 246, 248, 0.62)",
        quoteColor: "rgba(245, 246, 248, 0.48)",
        bubbleLineWidth: 2,
        quoteLineWidth: 1.8,
      },
    );

    drawingContext.lineCap = "butt";
    drawingContext.strokeStyle = "rgba(18, 22, 30, 0.92)";
    drawingContext.lineWidth = NO_LYRICS_SLASH_KNOCKOUT_WIDTH_PX;
    drawingContext.beginPath();
    drawingContext.moveTo(
      DISPLAY_CENTER_PX - NO_LYRICS_SLASH_HALF_SPAN_X_PX,
      medallionY + NO_LYRICS_SLASH_HALF_SPAN_Y_PX,
    );
    drawingContext.lineTo(
      DISPLAY_CENTER_PX + NO_LYRICS_SLASH_HALF_SPAN_X_PX,
      medallionY - NO_LYRICS_SLASH_HALF_SPAN_Y_PX,
    );
    drawingContext.stroke();

    drawingContext.strokeStyle = palette.signalHighlight;
    drawingContext.lineWidth = NO_LYRICS_SLASH_LINE_WIDTH_PX;
    drawingContext.beginPath();
    drawingContext.moveTo(
      DISPLAY_CENTER_PX - NO_LYRICS_SLASH_HALF_SPAN_X_PX,
      medallionY + NO_LYRICS_SLASH_HALF_SPAN_Y_PX,
    );
    drawingContext.lineTo(
      DISPLAY_CENTER_PX + NO_LYRICS_SLASH_HALF_SPAN_X_PX,
      medallionY - NO_LYRICS_SLASH_HALF_SPAN_Y_PX,
    );
    drawingContext.stroke();

    drawingContext.textAlign = "center";
    drawingContext.textBaseline = "middle";
    drawingContext.font = `700 18.5px ${FONT_DISPLAY}`;
    drawingContext.fillStyle = "#F5F6F8";
    drawingContext.fillText("No lyrics found", DISPLAY_CENTER_PX, 206 + lerp(10, 0, ease));

    drawingContext.font = `600 9px ${FONT_MONO}`;
    drawingContext.fillStyle = "rgba(245, 246, 248, 0.52)";
    drawingContext.fillText(
      lyricsFrame.note || "NO EMBEDDED OR .LRC FILE",
      DISPLAY_CENTER_PX,
      230 + lerp(10, 0, ease),
    );

    drawingContext.restore();
    drawingContext.restore();
    return;
  }

  const lineTransition = clamp(Number(nowPlayingState.lyricsLineTransitionProgress ?? 1), 0, 1);
  const lineEase = easeOutExpo(lineTransition);
  const activeChordWidthPx = 264;
  const previewChordWidthPx = 224;

  drawingContext.save();
  drawingContext.textAlign = "center";
  drawingContext.textBaseline = "middle";

  if (lineTransition < 0.65 && nowPlayingState.lyricsPreviousLineText) {
    const pastFade = 1 - normalizeRange(lineTransition, 0, 0.65);
    const pastLiftY = lerp(0, -24, 1 - pastFade);
    drawingContext.save();
    drawingContext.font = `700 20px ${FONT_DISPLAY}`;
    const pastLines = wrapCanvasTextLines(
      drawingContext,
      nowPlayingState.lyricsPreviousLineText,
      activeChordWidthPx,
      3,
    );
    const pastLineHeight = 25;
    const pastStartY = 162 - ((pastLines.length - 1) * pastLineHeight) / 2 + pastLiftY;
    for (let lineIndex = 0; lineIndex < pastLines.length; lineIndex += 1) {
      drawSoftDefocusedText(
        drawingContext,
        pastLines[lineIndex],
        DISPLAY_CENTER_PX,
        pastStartY + lineIndex * pastLineHeight,
        contentAlpha * pastFade * 0.58,
        lerp(0, 1.4, 1 - pastFade),
        "#F5F6F8",
      );
    }
    drawingContext.restore();
  }

  let activeBlockBottomY = 194;
  if (lyricsFrame.isIntroCue) {
    const pulseBaseY = 166 + lerp(12, 0, ease);
    drawingContext.save();
    for (let dotIndex = 0; dotIndex < 3; dotIndex += 1) {
      const dotWave = 0.35 + 0.65 * (0.5 + 0.5 * Math.sin(clockSeconds * 3.4 - dotIndex * 0.85));
      const dotX = DISPLAY_CENTER_PX + (dotIndex - 1) * 18;
      drawingContext.globalAlpha = contentAlpha * dotWave;
      drawingContext.fillStyle = dotIndex === 1 ? palette.signalHighlight : "#F5F6F8";
      drawingContext.beginPath();
      drawingContext.arc(dotX, pulseBaseY, 3.6, 0, FULL_CIRCLE_RADIANS);
      drawingContext.fill();
    }
    drawingContext.restore();
    activeBlockBottomY = pulseBaseY + 26;
  } else {
    drawingContext.font = `700 22px ${FONT_DISPLAY}`;
    let activeWrapped = wrapCanvasTextLines(drawingContext, activeLyricText, activeChordWidthPx, 3);
    let activeFontSize = 22;
    let activeLineHeight = 28;
    if (activeWrapped.length >= 3) {
      activeFontSize = 19.5;
      activeLineHeight = 24.5;
      drawingContext.font = `700 ${activeFontSize}px ${FONT_DISPLAY}`;
      activeWrapped = wrapCanvasTextLines(drawingContext, activeLyricText, activeChordWidthPx, 3);
    }

    const enterOffsetY = lerp(34, 0, lineEase) + lerp(12, 0, ease);
    const focusAnchorY = activeWrapped.length === 1 ? 166 : activeWrapped.length === 2 ? 162 : 158;
    const firstLineY = focusAnchorY - ((activeWrapped.length - 1) * activeLineHeight) / 2 + enterOffsetY;
    const activeAlpha = contentAlpha * lerp(0.42, 1, lineEase);
    const focusDefocusPx = lerp(1.35, 0, lineEase);

    for (let lineIndex = 0; lineIndex < activeWrapped.length; lineIndex += 1) {
      drawSoftDefocusedText(
        drawingContext,
        activeWrapped[lineIndex],
        DISPLAY_CENTER_PX,
        firstLineY + lineIndex * activeLineHeight,
        activeAlpha,
        focusDefocusPx,
        "#F5F6F8",
      );
    }
    activeBlockBottomY =
      firstLineY + (activeWrapped.length - 1) * activeLineHeight + activeLineHeight * 0.55;
  }

  if (lyricsFrame.upcomingText) {
    drawLyricsHorizonDivider(drawingContext, activeBlockBottomY, contentAlpha);
    drawingContext.font = `600 14.5px ${FONT_DISPLAY}`;
    const previewWrapped = wrapCanvasTextLines(
      drawingContext,
      lyricsFrame.upcomingText,
      previewChordWidthPx,
      2,
    );
    const previewLineHeight = 19.5;
    const previewReveal = normalizeRange(lineTransition, 0.16, 1.0);
    const previewStartY =
      Math.max(232, Math.min(254, activeBlockBottomY + 26)) +
      lerp(12, 0, easeOutCubic(previewReveal));
    const previewAlpha = contentAlpha * 0.44 * easeOutCubic(previewReveal);

    for (let previewIndex = 0; previewIndex < previewWrapped.length; previewIndex += 1) {
      drawSoftDefocusedText(
        drawingContext,
        previewWrapped[previewIndex],
        DISPLAY_CENTER_PX,
        previewStartY + previewIndex * previewLineHeight,
        previewAlpha,
        1.45,
        "#F5F6F8",
      );
    }
  }

  drawingContext.restore();
  drawingContext.restore();
}

function drawNowPlayingTransientFeedback(
  drawingContext,
  nowPlayingState,
  palette,
  animationTimeSeconds = 0,
) {
  const pulseAlpha = clamp(Number(nowPlayingState.playPausePulseAlpha ?? 0), 0, 1);
  if (pulseAlpha > LAYER_VISIBILITY_THRESHOLD) {
    const pulseScale = lerp(1.18, 0.94, pulseAlpha);
    drawingContext.save();
    drawingContext.globalAlpha = pulseAlpha;
    drawingContext.translate(DISPLAY_CENTER_PX, DISPLAY_CENTER_PX);
    drawingContext.scale(pulseScale, pulseScale);

    drawingContext.beginPath();
    drawingContext.arc(0, 0, 38, 0, FULL_CIRCLE_RADIANS);
    drawingContext.fillStyle = "rgba(6, 7, 9, 0.84)";
    drawingContext.fill();
    drawingContext.strokeStyle = palette.signalHighlight;
    drawingContext.lineWidth = 1.8;
    drawingContext.stroke();

    drawingContext.fillStyle = palette.signalHighlight;
    if (nowPlayingState.isPlaying) {
      drawingContext.beginPath();
      drawingContext.moveTo(-6, -11);
      drawingContext.lineTo(12, 0);
      drawingContext.lineTo(-6, 11);
      drawingContext.closePath();
      drawingContext.fill();
    } else {
      drawingContext.fillRect(-9, -10, 6, 20);
      drawingContext.fillRect(3, -10, 6, 20);
    }
    drawingContext.restore();
  }

  const bannerAlpha = clamp(Number(nowPlayingState.trackBannerAlpha ?? 0), 0, 1);
  if (
    bannerAlpha > LAYER_VISIBILITY_THRESHOLD &&
    nowPlayingState.trackBannerTitle &&
    Number(nowPlayingState.lyricsProgress ?? 0) < 0.05 &&
    Number(nowPlayingState.quickActionsProgress ?? 0) < 0.05 &&
    Number(nowPlayingState.queueSheetProgress ?? 0) < 0.05 &&
    Number(nowPlayingState.detailsSheetProgress ?? 0) < 0.05 &&
    Number(nowPlayingState.playlistPickerProgress ?? 0) < 0.05
  ) {
    const entryShiftY =
      nowPlayingState.trackBannerPhase === "in" ? lerp(-6, 0, easeOutCubic(bannerAlpha)) : 0;
    const bannerWidth = 178;
    const bannerHeight = 42;
    const bannerX = DISPLAY_CENTER_PX - bannerWidth / 2;
    const bannerY = 46 + entryShiftY;

    drawingContext.save();
    drawingContext.globalAlpha = bannerAlpha;

    traceChamferRect(drawingContext, bannerX, bannerY, bannerWidth, bannerHeight, 8);
    drawingContext.fillStyle = "rgba(6, 7, 9, 0.90)";
    drawingContext.fill();
    drawingContext.strokeStyle = "rgba(245, 250, 255, 0.26)";
    drawingContext.lineWidth = 1.1;
    drawingContext.stroke();

    drawingContext.fillStyle = palette.signalHighlight;
    drawingContext.fillRect(DISPLAY_CENTER_PX - 12, bannerY, 24, 2);

    drawingContext.textAlign = "center";
    drawingContext.textBaseline = "middle";
    drawingContext.font = `800 13px ${FONT_DISPLAY}`;
    drawingContext.fillStyle = "#F5F6F8";
    drawMarqueeCanvasText(
      drawingContext,
      nowPlayingState.trackBannerTitle,
      DISPLAY_CENTER_PX,
      bannerY + 16,
      bannerWidth - 22,
      animationTimeSeconds,
    );

    drawingContext.font = `600 8.5px ${FONT_MONO}`;
    drawingContext.fillStyle = "rgba(245, 246, 248, 0.68)";
    drawMarqueeCanvasText(
      drawingContext,
      nowPlayingState.trackBannerMeta || "",
      DISPLAY_CENTER_PX,
      bannerY + 31,
      bannerWidth - 22,
      animationTimeSeconds,
    );

    drawingContext.restore();
  }

  const toastAlpha = clamp(Number(nowPlayingState.toastAlpha ?? 0), 0, 1);
  if (toastAlpha > LAYER_VISIBILITY_THRESHOLD && nowPlayingState.toastText) {
    const toastEase = easeOutCubic(toastAlpha);
    drawingContext.save();
    drawingContext.globalAlpha = toastEase;
    drawingContext.font = `700 9.5px ${FONT_MONO}`;
    const textWidth = Math.min(206, drawingContext.measureText(nowPlayingState.toastText).width);
    const pillWidth = textWidth + 24;
    const pillHeight = 24;
    const pillX = DISPLAY_CENTER_PX - pillWidth / 2;
    const pillY = lerp(24, 34, toastEase);

    traceChamferRect(drawingContext, pillX, pillY, pillWidth, pillHeight, 6);
    drawingContext.fillStyle = "rgba(6, 7, 9, 0.90)";
    drawingContext.fill();
    drawingContext.strokeStyle = palette.signalHighlight;
    drawingContext.lineWidth = 1.15;
    drawingContext.stroke();

    drawingContext.textAlign = "center";
    drawingContext.textBaseline = "middle";
    drawingContext.fillStyle = "#F5F6F8";
    drawMarqueeCanvasText(
      drawingContext,
      nowPlayingState.toastText,
      DISPLAY_CENTER_PX,
      pillY + pillHeight / 2 + 0.5,
      204,
      animationTimeSeconds,
    );
    drawingContext.restore();
  }
}

/**
 * Render the DialOS 04 NOW PLAYING / 05 LYRICS screen from deterministic controller state.
 *
 * @param {CanvasRenderingContext2D} drawingContext Canvas 2D context.
 * @param {object} nowPlayingState Snapshot from createNowPlayingController().getRenderSnapshot().
 * @param {object} [renderContext={}] Appearance, palette, coverCache, and catalogAlbums.
 */
export function renderNowPlayingScreen(drawingContext, nowPlayingState, renderContext = {}) {
  if (!drawingContext || !nowPlayingState) {
    return;
  }

  const appearance = resolveAppearance(renderContext?.appearance);
  const palette = resolvePalette(appearance, renderContext?.palette);
  const coverCache = renderContext?.coverCache || null;
  const catalogAlbums = Array.isArray(renderContext?.catalogAlbums)
    ? renderContext.catalogAlbums
    : [];
  const animationTimeSeconds = Number.isFinite(renderContext?.animationTimeSeconds)
    ? renderContext.animationTimeSeconds
    : Number(nowPlayingState.idleClockSeconds ?? 0);

  if (nowPlayingState.operatingMode !== "playing") {
    renderNowPlayingIdleOrMode(drawingContext, nowPlayingState, palette, appearance);
  } else {
    const queueTracks =
      Array.isArray(nowPlayingState.queueTracks) && nowPlayingState.queueTracks.length > 0
        ? nowPlayingState.queueTracks
        : nowPlayingState.activeTrack
          ? [nowPlayingState.activeTrack]
          : [];
    const safeIndex =
      queueTracks.length > 0
        ? ((Number(nowPlayingState.queueIndex ?? 0) % queueTracks.length) + queueTracks.length) %
          queueTracks.length
        : 0;
    const activeTrack = nowPlayingState.activeTrack || queueTracks[safeIndex] || {};
    const albumSpec = resolveAlbumDescriptor(
      nowPlayingState.activeAlbum,
      activeTrack,
      catalogAlbums,
    );
    const trackDetails = nowPlayingState.trackDetails || activeTrack;
    const trackDurationSeconds = Math.max(
      1,
      Number(nowPlayingState.trackDurationSeconds || activeTrack.durationSeconds || 1),
    );
    const openProgress = clamp(
      Number(nowPlayingState.enterTransitionProgress ?? nowPlayingState.openTransitionProgress ?? 1),
      0,
      1,
    );
    const openEase = easeInOutCubic(openProgress);
    const isAlbumCrateEntrance = nowPlayingState.launchOrigin === "album-crate" && openProgress < 1;
    const discRadius = isAlbumCrateEntrance
      ? lerp(ALBUM_CRATE_START_DISC_RADIUS_PX, DISPLAY_CENTER_PX, openEase)
      : lerp(HOME_ENTRANCE_START_DISC_RADIUS_PX, DISPLAY_CENTER_PX, openEase);
    const rotationRadians =
      Number(nowPlayingState.vinylRotationRadians ?? 0) +
      Number(nowPlayingState.vinylNudgeRadians ?? nowPlayingState.vinylTransitionAngleRadians ?? 0);
    const isLyricsTransitioning =
      Boolean(nowPlayingState.lyricsOpen) ||
      Number(nowPlayingState.lyricsProgress ?? 0) > LAYER_VISIBILITY_THRESHOLD;

    const skipTransitionProgress = clamp(Number(nowPlayingState.skipTransitionProgress ?? 1), 0, 1);
    const skipDirection = Number(nowPlayingState.skipDirection ?? 1) >= 0 ? 1 : -1;
    const dragOffsetX = Number(nowPlayingState.skipDragOffsetX ?? 0);

    if (skipTransitionProgress < 1 && nowPlayingState.previousTrack) {
      const skipEase = easeOutExpo(skipTransitionProgress);
      const previousAlbum = resolveAlbumDescriptor(
        nowPlayingState.previousAlbum,
        nowPlayingState.previousTrack,
        catalogAlbums,
      );
      const outgoingX = DISPLAY_CENTER_PX - skipDirection * skipEase * DISC_SKIP_SLIDE_DISTANCE_PX;
      const incomingX =
        DISPLAY_CENTER_PX + skipDirection * (1 - skipEase) * DISC_SKIP_SLIDE_DISTANCE_PX;

      drawNowPlayingOpticalVinyl(
        drawingContext,
        previousAlbum,
        outgoingX,
        DISPLAY_CENTER_PX,
        discRadius,
        rotationRadians - skipDirection * skipEase * 0.55,
        palette,
        appearance,
        1 - skipEase * 0.35,
        coverCache,
      );
      drawNowPlayingOpticalVinyl(
        drawingContext,
        albumSpec,
        incomingX,
        DISPLAY_CENTER_PX,
        discRadius,
        rotationRadians + skipDirection * (1 - skipEase) * 0.45,
        palette,
        appearance,
        1,
        coverCache,
      );
    } else if (isAlbumCrateEntrance) {
      const sleeveSlideEase = easeInOutCubic(
        normalizeRange(openProgress, 0.0, ALBUM_CRATE_SLEEVE_SLIDE_END_PROGRESS),
      );
      const sleeveCenterX = lerp(
        ALBUM_CRATE_SLEEVE_START_X_PX,
        ALBUM_CRATE_SLEEVE_EXIT_X_PX,
        sleeveSlideEase,
      );
      const sleeveAlpha =
        1 -
        easeOutCubic(
          normalizeRange(
            openProgress,
            ALBUM_CRATE_SLEEVE_FADE_START_PROGRESS,
            ALBUM_CRATE_SLEEVE_SLIDE_END_PROGRESS,
          ),
        );
      const discSlipOutX =
        openProgress <= ALBUM_CRATE_SLIP_OUT_SPLIT_PROGRESS
          ? lerp(
              ALBUM_CRATE_DISC_PEEK_X_PX,
              ALBUM_CRATE_DISC_APEX_X_PX,
              easeOutCubic(openProgress / ALBUM_CRATE_SLIP_OUT_SPLIT_PROGRESS),
            )
          : lerp(
              ALBUM_CRATE_DISC_APEX_X_PX,
              DISPLAY_CENTER_PX,
              easeInOutCubic(
                (openProgress - ALBUM_CRATE_SLIP_OUT_SPLIT_PROGRESS) /
                  (1 - ALBUM_CRATE_SLIP_OUT_SPLIT_PROGRESS),
              ),
            );
      const discCenterY = lerp(ALBUM_CRATE_SLEEVE_CENTER_Y_PX, DISPLAY_CENTER_PX, openEase);

      drawNowPlayingOpticalVinyl(
        drawingContext,
        albumSpec,
        discSlipOutX,
        discCenterY,
        discRadius,
        rotationRadians,
        palette,
        appearance,
        1,
        coverCache,
      );

      if (sleeveAlpha > LAYER_VISIBILITY_THRESHOLD) {
        drawCrateAlbumSleeveBox(drawingContext, albumSpec, {
          centerX: sleeveCenterX,
          centerY: ALBUM_CRATE_SLEEVE_CENTER_Y_PX,
          sleeveSize: ALBUM_CRATE_SLEEVE_SIZE_PX,
          focusWeight: 1,
          delta: 0,
          alpha: sleeveAlpha,
          palette,
          appearance,
          coverCache,
        });
      }
    } else {
      const peekDelta = dragOffsetX < 0 ? 1 : -1;
      const peekOutcome =
        Math.abs(dragOffsetX) > 6
          ? resolveSkipOutcome(
              {
                queueTracks,
                queueIndex: safeIndex,
                elapsedSeconds: Number(nowPlayingState.trackElapsedSeconds ?? 0),
                repeatMode: nowPlayingState.repeatMode || "off",
                shuffleEnabled: Boolean(nowPlayingState.shuffleEnabled),
                queueSource: nowPlayingState.queueContext?.sourceId || "album",
                ephemeralCurrentTrackId: nowPlayingState.ephemeralCurrentTrackId || null,
                baseTrackCount: nowPlayingState.baseTrackCount ?? queueTracks.length,
              },
              peekDelta,
            )
          : null;
      const peekTrack = peekOutcome && peekOutcome.action === "jump" ? peekOutcome.targetTrack : null;
      const isBoundaryBlocked = Boolean(peekOutcome && peekOutcome.action === "boundary-bounce");
      const peekChangesCover = Boolean(
        Math.abs(dragOffsetX) > 6 && peekTrack && peekTrack.albumId !== activeTrack.albumId,
      );
      const appliedOffsetX = peekChangesCover ? dragOffsetX : 0;
      const appliedTwist = peekChangesCover
        ? dragOffsetX * 0.004
        : isBoundaryBlocked
          ? dragOffsetX * 0.0011
          : dragOffsetX * 0.0022;

      drawNowPlayingOpticalVinyl(
        drawingContext,
        albumSpec,
        DISPLAY_CENTER_PX + appliedOffsetX,
        DISPLAY_CENTER_PX,
        discRadius,
        rotationRadians + appliedTwist,
        palette,
        appearance,
        1,
        coverCache,
      );

      if (peekChangesCover && Math.abs(dragOffsetX) > 12) {
        const peekAlbum = resolveAlbumDescriptor(null, peekTrack, catalogAlbums);
        const peekX = DISPLAY_CENTER_PX + dragOffsetX + peekDelta * DISC_SKIP_SLIDE_DISTANCE_PX;
        drawNowPlayingOpticalVinyl(
          drawingContext,
          peekAlbum,
          peekX,
          DISPLAY_CENTER_PX,
          discRadius,
          rotationRadians * 0.6,
          palette,
          appearance,
          0.88,
          coverCache,
        );
      }
    }

    if (Number(nowPlayingState.lyricsProgress ?? 0) > LAYER_VISIBILITY_THRESHOLD) {
      renderNowPlayingLyricsOverlay(
        drawingContext,
        nowPlayingState,
        activeTrack,
        albumSpec,
        palette,
        appearance,
        animationTimeSeconds,
      );
    }

    const openWaveOne =
      openProgress >= 0.99 ? 1 : easeOutExpo(normalizeRange(openProgress, 0.48, 0.84));
    const openWaveTwo = clamp(Number(nowPlayingState.openChromeHoldAlpha ?? 0), 0, 1);

    const seekActiveOverLyrics =
      Boolean(nowPlayingState.isSeeking) ||
      Number(nowPlayingState.seekHoldProgress ?? 0) > 0.02 ||
      Number(nowPlayingState.seekExpandProgress ?? 0) > 0.02;
    const centerOccupied = Math.max(
      Number(nowPlayingState.quickActionsProgress ?? 0),
      Number(nowPlayingState.queueSheetProgress ?? 0),
      Number(nowPlayingState.detailsSheetProgress ?? 0),
      Number(nowPlayingState.playlistPickerProgress ?? 0),
      Number(nowPlayingState.volumeHudAlpha ?? 0),
      !seekActiveOverLyrics && isLyricsTransitioning ? 1 : 0,
    );
    const spindleVisibility = openWaveOne * (1 - normalizeRange(centerOccupied, 0.12, 0.65));
    if (spindleVisibility > LAYER_VISIBILITY_THRESHOLD) {
      drawNowPlayingSpindleAndSeek(
        drawingContext,
        nowPlayingState,
        trackDurationSeconds,
        spindleVisibility,
        palette,
      );
    }

    if (Number(nowPlayingState.quickActionsProgress ?? 0) > LAYER_VISIBILITY_THRESHOLD) {
      renderNowPlayingQuickActions(
        drawingContext,
        nowPlayingState,
        activeTrack,
        albumSpec,
        palette,
        appearance,
        animationTimeSeconds,
      );
    }

    if (Number(nowPlayingState.playlistPickerProgress ?? 0) > LAYER_VISIBILITY_THRESHOLD) {
      renderNowPlayingPlaylistPicker(
        drawingContext,
        nowPlayingState,
        activeTrack,
        palette,
        appearance,
        animationTimeSeconds,
      );
    }

    if (Number(nowPlayingState.detailsSheetProgress ?? 0) > LAYER_VISIBILITY_THRESHOLD) {
      renderNowPlayingTrackDetails(
        drawingContext,
        nowPlayingState,
        trackDetails,
        albumSpec,
        palette,
        appearance,
        coverCache,
        animationTimeSeconds,
      );
    }

    if (Number(nowPlayingState.queueSheetProgress ?? 0) > LAYER_VISIBILITY_THRESHOLD) {
      renderNowPlayingQueueSheet(
        drawingContext,
        nowPlayingState,
        nowPlayingState.queueContext,
        palette,
        appearance,
        animationTimeSeconds,
      );
    }

    drawNowPlayingVolumeHud(drawingContext, nowPlayingState, openWaveTwo, palette);
    drawNowPlayingTransientFeedback(
      drawingContext,
      nowPlayingState,
      palette,
      animationTimeSeconds,
    );
  }

  const backSwipeProgress = Number(nowPlayingState.backSwipeProgress ?? 0);
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
 * Determine whether display coordinates `(pointX, pointY)` hover an actionable Now Playing target.
 *
 * @param {number} pointX Display coordinate X in [0, 360].
 * @param {number} pointY Display coordinate Y in [0, 360].
 * @param {object} nowPlayingState Snapshot from createNowPlayingController().getRenderSnapshot().
 * @returns {boolean} True when hovering a clickable Now Playing element.
 */
export function hitTestNowPlayingInteractive(pointX, pointY, nowPlayingState) {
  if (!nowPlayingState || !Number.isFinite(pointX) || !Number.isFinite(pointY)) {
    return false;
  }

  if (nowPlayingState.operatingMode !== "playing") {
    return (
      pointX >= IDLE_CTA_LEFT_X_PX &&
      pointX <= IDLE_CTA_RIGHT_X_PX &&
      pointY >= IDLE_CTA_TOP_Y_PX &&
      pointY <= IDLE_CTA_BOTTOM_Y_PX
    );
  }

  if (
    nowPlayingState.playlistPickerOpen ||
    Number(nowPlayingState.playlistPickerProgress ?? 0) > 0.5
  ) {
    const playlists = Array.isArray(nowPlayingState.playlists) ? nowPlayingState.playlists : [];
    const visibleCount = Math.min(PICKER_MAX_VISIBLE_ROWS, playlists.length);
    for (let playlistIndex = 0; playlistIndex < visibleCount; playlistIndex += 1) {
      const rowTop = PICKER_ROW_START_Y_PX + playlistIndex * PICKER_ROW_PITCH_PX;
      if (
        pointX >= PICKER_ROW_LEFT_X_PX &&
        pointX <= PICKER_ROW_RIGHT_X_PX &&
        pointY >= rowTop &&
        pointY <= rowTop + PICKER_ROW_HEIGHT_PX
      ) {
        return true;
      }
    }
    return false;
  }

  if (
    nowPlayingState.detailsSheetOpen ||
    Number(nowPlayingState.detailsSheetProgress ?? 0) > 0.5
  ) {
    return false;
  }

  if (
    nowPlayingState.queueSheetOpen ||
    Number(nowPlayingState.queueSheetProgress ?? 0) > 0.5
  ) {
    if (
      pointY < QUEUE_LIST_CLIP_TOP_Y ||
      pointY > QUEUE_LIST_CLIP_TOP_Y + QUEUE_LIST_CLIP_HEIGHT_PX
    ) {
      return false;
    }
    const queueTracks = Array.isArray(nowPlayingState.queueTracks)
      ? nowPlayingState.queueTracks
      : Array.isArray(nowPlayingState.queueContext?.tracks)
        ? nowPlayingState.queueContext.tracks
        : [];
    return (
      hitTestNowPlayingQueueRow(
        pointX,
        pointY,
        Number(nowPlayingState.queueScrollPosition ?? 0),
        queueTracks,
      ) !== null
    );
  }

  if (
    nowPlayingState.quickActionsOpen ||
    Number(nowPlayingState.quickActionsProgress ?? 0) > 0.5
  ) {
    for (let buttonIndex = 0; buttonIndex < NOW_PLAYING_QUICK_BUTTONS.length; buttonIndex += 1) {
      const buttonSpec = NOW_PLAYING_QUICK_BUTTONS[buttonIndex];
      const hitDistance = Math.hypot(pointX - buttonSpec.centerX, pointY - buttonSpec.centerY);
      if (hitDistance <= buttonSpec.radius + QUICK_ACTION_HIT_PAD_PX) {
        return true;
      }
    }
    return false;
  }

  const distanceFromCenter = Math.hypot(
    pointX - DISPLAY_CENTER_PX,
    pointY - DISPLAY_CENTER_PX,
  );
  return distanceFromCenter <= NOW_PLAYING_CONSTANTS.SPINDLE_BASE_RADIUS_PX;
}
