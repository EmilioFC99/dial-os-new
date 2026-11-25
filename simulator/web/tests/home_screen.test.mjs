import { describe, test } from "node:test";
import assert from "node:assert/strict";

import { HOME_CONSTANTS, createHomeController } from "../core/home_controller.js";
import { APPEARANCE_PALETTES } from "../core/tokens.js";
import * as homeScreen from "../screens/home_screen.js";

const DISPLAY_CENTER_PX = 180;
const FLOAT_TOLERANCE = 1e-4;
const RETURN_ROTATION_TWENTY_DEG_RADIANS = (20 * Math.PI) / 180;
const SUBTITLE_TARGET_Y = 272;

/**
 * Compute world coordinates from a local 2D point and affine transformation matrix.
 *
 * @param {object} matrix Affine transform `{ scaleX, skewY, skewX, scaleY, translateX, translateY }`.
 * @param {number} localX Local X coordinate.
 * @param {number} localY Local Y coordinate.
 * @returns {{ worldX: number, worldY: number }} Transformed coordinates.
 */
function transformPoint(matrix, localX, localY) {
  return {
    worldX: matrix.scaleX * localX + matrix.skewX * localY + matrix.translateX,
    worldY: matrix.skewY * localX + matrix.scaleY * localY + matrix.translateY,
  };
}

/**
 * Parse a pixel font size from a CSS font string for deterministic `measureText()`.
 *
 * @param {string} fontSpec Canvas font string.
 * @returns {number} Font size in pixels.
 */
function parseFontSizePx(fontSpec) {
  const sizeMatch = /(\d+(?:\.\d+)?)px/.exec(String(fontSpec || ""));
  return sizeMatch ? Number(sizeMatch[1]) : 12;
}

/**
 * Create a deterministic Canvas 2D recording mock capturing text, geometry, clips,
 * strokes, fills, rotations, translations, and state stack operations.
 *
 * @returns {object} Mock Canvas 2D context with recorded call arrays.
 */
function createRecordingCanvasContext() {
  const stateStack = [];
  let activeState = {
    matrix: {
      scaleX: 1,
      skewY: 0,
      skewX: 0,
      scaleY: 1,
      translateX: 0,
      translateY: 0,
    },
    font: "600 12px sans-serif",
    fillStyle: "#FFFFFF",
    strokeStyle: "#FFFFFF",
    lineWidth: 1,
    lineCap: "butt",
    lineJoin: "miter",
    globalAlpha: 1,
    textAlign: "left",
    textBaseline: "alphabetic",
  };

  let currentPathArcs = [];
  let currentPathRects = [];
  let currentPathPoints = [];

  const recordedTextCalls = [];
  const recordedArcs = [];
  const recordedClips = [];
  const recordedStrokes = [];
  const recordedFills = [];
  const recordedRects = [];
  const recordedRotations = [];
  const recordedTranslations = [];
  let saveCount = 0;
  let restoreCount = 0;
  let clipCount = 0;

  function cloneState(sourceState) {
    return {
      ...sourceState,
      matrix: { ...sourceState.matrix },
    };
  }

  function snapshotActiveScale() {
    return Math.hypot(activeState.matrix.scaleX, activeState.matrix.skewY) || 1;
  }

  function clearCurrentPath() {
    currentPathArcs = [];
    currentPathRects = [];
    currentPathPoints = [];
  }

  const drawingContext = {
    get font() {
      return activeState.font;
    },
    set font(nextFont) {
      activeState.font = nextFont;
    },
    get fillStyle() {
      return activeState.fillStyle;
    },
    set fillStyle(nextStyle) {
      activeState.fillStyle = nextStyle;
    },
    get strokeStyle() {
      return activeState.strokeStyle;
    },
    set strokeStyle(nextStyle) {
      activeState.strokeStyle = nextStyle;
    },
    get lineWidth() {
      return activeState.lineWidth;
    },
    set lineWidth(nextWidth) {
      activeState.lineWidth = nextWidth;
    },
    get lineCap() {
      return activeState.lineCap;
    },
    set lineCap(nextCap) {
      activeState.lineCap = nextCap;
    },
    get lineJoin() {
      return activeState.lineJoin;
    },
    set lineJoin(nextJoin) {
      activeState.lineJoin = nextJoin;
    },
    get globalAlpha() {
      return activeState.globalAlpha;
    },
    set globalAlpha(nextAlpha) {
      activeState.globalAlpha = nextAlpha;
    },
    get textAlign() {
      return activeState.textAlign;
    },
    set textAlign(nextAlign) {
      activeState.textAlign = nextAlign;
    },
    get textBaseline() {
      return activeState.textBaseline;
    },
    set textBaseline(nextBaseline) {
      activeState.textBaseline = nextBaseline;
    },
    save() {
      saveCount += 1;
      stateStack.push(cloneState(activeState));
    },
    restore() {
      restoreCount += 1;
      const previousState = stateStack.pop();
      if (previousState) {
        activeState = previousState;
      }
    },
    translate(offsetX, offsetY) {
      recordedTranslations.push({ offsetX, offsetY });
      const prevMatrix = activeState.matrix;
      activeState.matrix = {
        ...prevMatrix,
        translateX:
          prevMatrix.scaleX * offsetX + prevMatrix.skewX * offsetY + prevMatrix.translateX,
        translateY:
          prevMatrix.skewY * offsetX + prevMatrix.scaleY * offsetY + prevMatrix.translateY,
      };
    },
    rotate(angleRadians) {
      recordedRotations.push(angleRadians);
      const cosAngle = Math.cos(angleRadians);
      const sinAngle = Math.sin(angleRadians);
      const prevMatrix = activeState.matrix;
      activeState.matrix = {
        scaleX: prevMatrix.scaleX * cosAngle + prevMatrix.skewX * sinAngle,
        skewY: prevMatrix.skewY * cosAngle + prevMatrix.scaleY * sinAngle,
        skewX: prevMatrix.scaleX * -sinAngle + prevMatrix.skewX * cosAngle,
        scaleY: prevMatrix.skewY * -sinAngle + prevMatrix.scaleY * cosAngle,
        translateX: prevMatrix.translateX,
        translateY: prevMatrix.translateY,
      };
    },
    scale(factorX, factorY) {
      const prevMatrix = activeState.matrix;
      activeState.matrix = {
        ...prevMatrix,
        scaleX: prevMatrix.scaleX * factorX,
        skewY: prevMatrix.skewY * factorX,
        skewX: prevMatrix.skewX * factorY,
        scaleY: prevMatrix.scaleY * factorY,
      };
    },
    beginPath() {
      clearCurrentPath();
    },
    closePath() {},
    moveTo(pointX, pointY) {
      currentPathPoints.push(transformPoint(activeState.matrix, pointX, pointY));
    },
    lineTo(pointX, pointY) {
      currentPathPoints.push(transformPoint(activeState.matrix, pointX, pointY));
    },
    rect(leftX, topY, width, height) {
      const transformed = transformPoint(activeState.matrix, leftX, topY);
      const scaleFactor = snapshotActiveScale();
      const rectEntry = {
        kind: "rect",
        leftX,
        topY,
        worldLeftX: transformed.worldX,
        worldTopY: transformed.worldY,
        width: width * scaleFactor,
        height: height * scaleFactor,
      };
      currentPathRects.push(rectEntry);
      recordedRects.push(rectEntry);
    },
    arc(centerX, centerY, radius, startAngle, endAngle, counterClockwise = false) {
      const transformed = transformPoint(activeState.matrix, centerX, centerY);
      const worldRadius = radius * snapshotActiveScale();
      const arcEntry = {
        localCenterX: centerX,
        localCenterY: centerY,
        worldCenterX: transformed.worldX,
        worldCenterY: transformed.worldY,
        radius,
        worldRadius,
        startAngle,
        endAngle,
        counterClockwise,
      };
      currentPathArcs.push(arcEntry);
      recordedArcs.push(arcEntry);
    },
    ellipse(centerX, centerY, radiusX, radiusY, rotation, startAngle, endAngle) {
      const transformed = transformPoint(activeState.matrix, centerX, centerY);
      currentPathArcs.push({
        localCenterX: centerX,
        localCenterY: centerY,
        worldCenterX: transformed.worldX,
        worldCenterY: transformed.worldY,
        radius: radiusX,
        worldRadius: radiusX * snapshotActiveScale(),
        radiusY,
        rotation,
        startAngle,
        endAngle,
        counterClockwise: false,
      });
    },
    clip() {
      clipCount += 1;
      recordedClips.push({
        rects: currentPathRects.slice(),
        arcs: currentPathArcs.slice(),
      });
    },
    stroke() {
      recordedStrokes.push({
        arcs: currentPathArcs.slice(),
        rects: currentPathRects.slice(),
        points: currentPathPoints.slice(),
        lineWidth: activeState.lineWidth,
        strokeStyle: activeState.strokeStyle,
        globalAlpha: activeState.globalAlpha,
      });
    },
    fill() {
      recordedFills.push({
        arcs: currentPathArcs.slice(),
        rects: currentPathRects.slice(),
        points: currentPathPoints.slice(),
        fillStyle: activeState.fillStyle,
        globalAlpha: activeState.globalAlpha,
      });
    },
    fillRect(leftX, topY, width, height) {
      const transformed = transformPoint(activeState.matrix, leftX, topY);
      const scaleFactor = snapshotActiveScale();
      recordedRects.push({
        kind: "fillRect",
        leftX,
        topY,
        worldLeftX: transformed.worldX,
        worldTopY: transformed.worldY,
        width: width * scaleFactor,
        height: height * scaleFactor,
        fillStyle: activeState.fillStyle,
        globalAlpha: activeState.globalAlpha,
      });
    },
    strokeRect(leftX, topY, width, height) {
      const transformed = transformPoint(activeState.matrix, leftX, topY);
      const scaleFactor = snapshotActiveScale();
      recordedRects.push({
        kind: "strokeRect",
        leftX,
        topY,
        worldLeftX: transformed.worldX,
        worldTopY: transformed.worldY,
        width: width * scaleFactor,
        height: height * scaleFactor,
        strokeStyle: activeState.strokeStyle,
        lineWidth: activeState.lineWidth,
        globalAlpha: activeState.globalAlpha,
      });
    },
    clearRect() {},
    drawImage() {},
    setLineDash() {},
    createLinearGradient() {
      return { addColorStop() {} };
    },
    createRadialGradient() {
      return { addColorStop() {} };
    },
    measureText(labelText) {
      const fontSizePx = parseFontSizePx(activeState.font);
      const textContent = String(labelText ?? "");
      return {
        width: textContent.length * fontSizePx * 0.56,
      };
    },
    fillText(labelText, localX, localY) {
      const transformed = transformPoint(activeState.matrix, localX, localY);
      const textContent = String(labelText ?? "");
      const measuredWidth = drawingContext.measureText(textContent).width;
      recordedTextCalls.push({
        text: textContent,
        localX,
        localY,
        worldX: transformed.worldX,
        worldY: transformed.worldY,
        textAlign: activeState.textAlign,
        textBaseline: activeState.textBaseline,
        font: activeState.font,
        fillStyle: activeState.fillStyle,
        globalAlpha: activeState.globalAlpha,
        measuredWidth,
      });
    },
    recordedTextCalls,
    recordedArcs,
    recordedClips,
    recordedStrokes,
    recordedFills,
    recordedRects,
    recordedRotations,
    recordedTranslations,
    getCounts() {
      return { saveCount, restoreCount, clipCount };
    },
  };

  return drawingContext;
}

/**
 * Resolve the left visual X edge of a recorded text call regardless of alignment.
 *
 * @param {object} textCall Recorded `fillText` descriptor.
 * @returns {number} Leftmost X coordinate in world pixels.
 */
function computeRenderedLeftEdgeX(textCall) {
  if (textCall.textAlign === "center") {
    return textCall.worldX - textCall.measuredWidth / 2;
  }
  if (textCall.textAlign === "right" || textCall.textAlign === "end") {
    return textCall.worldX - textCall.measuredWidth;
  }
  return textCall.worldX;
}

/**
 * Create a settled Home controller at rest on slot 0.
 *
 * @returns {object} Settled Home controller.
 */
function createSettledHomeController() {
  const homeController = createHomeController();
  homeController.enterFromBoot();
  for (let stepIndex = 0; stepIndex < 30; stepIndex += 1) {
    homeController.step(0.05);
  }
  return homeController;
}

describe("home_screen.js Now Playing transition and subtitle marquee", () => {
  test("renderHomeScreen applies controller 20-degree vinylNudgeRadians rotation offset during Now Playing -> Home exit", () => {
    const homeController = createSettledHomeController();
    homeController.syncPlaybackState({
      playbackState: "playing",
      hasActiveTrack: true,
    });
    homeController.selectAppById("now-playing");
    homeController.launchFocusedApp();
    homeController.step(HOME_CONSTANTS.LAUNCH_DURATION_SECONDS);

    homeController.returnFromScreen("now-playing");
    const returnStartSnapshot = homeController.getRenderSnapshot();
    assert.ok(
      Math.abs(returnStartSnapshot.vinylNudgeRadians - RETURN_ROTATION_TWENTY_DEG_RADIANS) <
        FLOAT_TOLERANCE,
    );

    homeController.step(0.05);
    const exitMidSnapshot = homeController.getRenderSnapshot();
    assert.ok(exitMidSnapshot.launchProgress > 0.5 && exitMidSnapshot.launchProgress < 1);
    assert.ok(exitMidSnapshot.vinylNudgeRadians > 0.05);

    const exitContext = createRecordingCanvasContext();
    homeScreen.renderHomeScreen(exitContext, exitMidSnapshot, {
      appearance: "dark",
      palette: APPEARANCE_PALETTES.dark,
      activeAlbum: { id: "a1", title: "Alpha Horizons", motif: "prism" },
    });

    const baselineContext = createRecordingCanvasContext();
    homeScreen.renderHomeScreen(
      baselineContext,
      { ...exitMidSnapshot, vinylNudgeRadians: 0 },
      {
        appearance: "dark",
        palette: APPEARANCE_PALETTES.dark,
        activeAlbum: { id: "a1", title: "Alpha Horizons", motif: "prism" },
      },
    );

    assert.ok(exitContext.recordedRotations.length >= 1);
    assert.ok(baselineContext.recordedRotations.length >= 1);
    assert.ok(
      Math.abs(
        exitContext.recordedRotations[0] -
          baselineContext.recordedRotations[0] -
          exitMidSnapshot.vinylNudgeRadians,
      ) < FLOAT_TOLERANCE,
      "Now Playing -> Home exit must apply controller vinylNudgeRadians rotation offset",
    );
  });

  test("renderHomeScreen uses drawMarqueeCanvasText when the Home subtitle exceeds bounds and renders statically when fitting", () => {
    const homeController = createSettledHomeController();
    homeController.syncPlaybackState({
      playbackState: "playing",
      hasActiveTrack: true,
    });
    const baseSnapshot = homeController.getRenderSnapshot();

    // 1. Short fitting subtitle stays static without clip or horizontal shift
    const shortContext = createRecordingCanvasContext();
    homeScreen.renderHomeScreen(
      shortContext,
      { ...baseSnapshot, idleClockSeconds: 3.5 },
      {
        appearance: "dark",
        palette: APPEARANCE_PALETTES.dark,
        trackTitle: "SHORT",
        animationTimeSeconds: 3.5,
      },
    );
    const shortSubtitleCall = shortContext.recordedTextCalls.find((call) =>
      call.text.includes("SHORT"),
    );
    assert.ok(shortSubtitleCall);
    assert.ok(Math.abs(shortSubtitleCall.worldX - DISPLAY_CENTER_PX) < FLOAT_TOLERANCE);
    const shortSubtitleClips = shortContext.recordedClips.filter((clipEntry) =>
      clipEntry.rects.some(
        (rectEntry) =>
          SUBTITLE_TARGET_Y >= rectEntry.worldTopY - 6 &&
          SUBTITLE_TARGET_Y <= rectEntry.worldTopY + rectEntry.height + 6,
      ),
    );
    assert.equal(shortSubtitleClips.length, 0);

    // 2. Overflowing subtitle clips to bounds and scrolls across animationTimeSeconds
    const longTrackTitle =
      "A Very Long Symphonic Movement In D Minor Opus Number Forty Two";
    const expectedSubtitle = `${longTrackTitle.toUpperCase()} \u00b7 00:00 / 04:08`;

    const renderLongSubtitleAtTime = (timeSeconds) => {
      const drawingContext = createRecordingCanvasContext();
      homeScreen.renderHomeScreen(
        drawingContext,
        { ...baseSnapshot, idleClockSeconds: timeSeconds },
        {
          appearance: "dark",
          palette: APPEARANCE_PALETTES.dark,
          trackTitle: longTrackTitle,
          animationTimeSeconds: timeSeconds,
        },
      );
      return drawingContext;
    };

    const startContext = renderLongSubtitleAtTime(0);
    const startCalls = startContext.recordedTextCalls.filter(
      (call) => call.text === expectedSubtitle,
    );
    assert.ok(
      startCalls.length >= 1,
      "Home subtitle should render full overflowing text via drawMarqueeCanvasText",
    );

    const subtitleClips = startContext.recordedClips.filter((clipEntry) =>
      clipEntry.rects.some(
        (rectEntry) =>
          rectEntry.width <= 260 &&
          SUBTITLE_TARGET_Y >= rectEntry.worldTopY - 8 &&
          SUBTITLE_TARGET_Y <= rectEntry.worldTopY + rectEntry.height + 8,
      ),
    );
    assert.ok(
      subtitleClips.length >= 1,
      "overflowing Home subtitle must clip to horizontal bounds via drawMarqueeCanvasText",
    );

    const initialLeftEdgeX = computeRenderedLeftEdgeX(startCalls[0]);
    const shifted = [2.5, 3.5, 5.0].some((laterTimeSeconds) => {
      const laterContext = renderLongSubtitleAtTime(laterTimeSeconds);
      const laterCalls = laterContext.recordedTextCalls.filter(
        (call) => call.text === expectedSubtitle,
      );
      return laterCalls.some(
        (call) => Math.abs(computeRenderedLeftEdgeX(call) - initialLeftEdgeX) > 2,
      );
    });
    assert.equal(
      shifted,
      true,
      "overflowing Home subtitle must advance horizontal X across animationTimeSeconds",
    );
  });
});
