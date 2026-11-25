import { describe, test } from "node:test";
import assert from "node:assert/strict";

import { createLibraryController } from "../core/library_controller.js";
import { APPEARANCE_PALETTES } from "../core/tokens.js";
import * as libraryScreen from "../screens/library_screen.js";

const DISPLAY_CENTER_PX = 180;
const FLOAT_TOLERANCE = 1e-4;
const CRATE_NOTE_BUTTON_X = 180;
const CRATE_NOTE_BUTTON_Y = 325;
const CRATE_NOTE_BUTTON_EDGE_X = 196;
const CRATE_OLD_HINT_OUTSIDE_X = 220;

const MARQUEE_SAMPLE_STEP_SECONDS = 0.05;
const MARQUEE_MAX_CYCLE_SECONDS = 30;
const MARQUEE_TEXT_FONT = "700 14px sans-serif";
const MARQUEE_MAX_WIDTH_PX = 120;
const VINYL_FULL_RADIUS_PX = 180;
const VINYL_CENTER_LABEL_RATIO = 0.28;

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
      const transformed = transformPoint(activeState.matrix, pointX, pointY);
      currentPathPoints.push(transformed);
    },
    lineTo(pointX, pointY) {
      const transformed = transformPoint(activeState.matrix, pointX, pointY);
      currentPathPoints.push(transformed);
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
 * Build a multi-album test catalog with both short and overflowing names for Library screen tests.
 *
 * @returns {object} Hydratable catalog payload.
 */
function buildLibraryScreenCatalog() {
  const trackAlphaOne = {
    id: "t-a1",
    title: "01 Dawn Over Glass",
    artist: "Aurora Ensemble",
    albumId: "album-a",
    albumTitle: "Alpha Horizons",
    genre: "Ambient",
    duration: "03:00",
    durationSeconds: 180,
    isFavorite: true,
    trackNumber: "01",
    audioUrl: "/api/audio/t-a1",
  };
  const trackAlphaTwo = {
    id: "t-a2",
    title: "02 Night Horizon",
    artist: "Aurora Ensemble",
    albumId: "album-a",
    albumTitle: "Alpha Horizons",
    genre: "Ambient",
    duration: "03:40",
    durationSeconds: 220,
    isFavorite: false,
    trackNumber: "02",
    audioUrl: "/api/audio/t-a2",
  };
  const trackBetaOne = {
    id: "t-b1",
    title: "01 Short Signal",
    artist: "Beta Trio",
    albumId: "album-b",
    albumTitle: "Beta Reflections",
    genre: "Jazz",
    duration: "02:50",
    durationSeconds: 170,
    isFavorite: false,
    trackNumber: "01",
    audioUrl: "/api/audio/t-b1",
  };
  const trackGammaOne = {
    id: "t-c1",
    title: "01 Long Overflowing Track Movement In C Sharp Minor Opus Ninety Nine",
    artist: "Extraordinarily Long Philharmonic Orchestra Name For Marquee Testing",
    albumId: "album-c",
    albumTitle: "Gamma Nocturnes With An Extraordinarily Long Symphony Title For Marquee",
    genre: "Extraordinarily Long Contemporary Classical & Electroacoustic Genre Title",
    duration: "05:12",
    durationSeconds: 312,
    isFavorite: false,
    trackNumber: "01",
    audioUrl: "/api/audio/t-c1",
  };

  return {
    allTracks: [trackAlphaOne, trackAlphaTwo, trackBetaOne, trackGammaOne],
    albums: [
      {
        id: "album-a",
        title: "Alpha Horizons",
        artist: "Aurora Ensemble",
        motif: "prism",
        tracks: [trackAlphaOne, trackAlphaTwo],
      },
      {
        id: "album-b",
        title: "Beta Reflections",
        artist: "Beta Trio",
        motif: "grid",
        tracks: [trackBetaOne],
      },
      {
        id: "album-c",
        title: "Gamma Nocturnes With An Extraordinarily Long Symphony Title For Marquee",
        artist: "Extraordinarily Long Philharmonic Orchestra Name For Marquee Testing",
        motif: "optical",
        tracks: [trackGammaOne],
      },
    ],
    artists: [
      {
        id: "artist-a",
        name: "Aurora Ensemble",
        tracks: [trackAlphaOne, trackAlphaTwo],
        albumIds: ["album-a"],
      },
      {
        id: "artist-c",
        name: "Extraordinarily Long Philharmonic Orchestra Name For Marquee Testing",
        tracks: [trackGammaOne],
        albumIds: ["album-c"],
      },
    ],
    genres: [
      {
        id: "genre-c",
        name: "Extraordinarily Long Contemporary Classical & Electroacoustic Genre Title",
        tracks: [trackGammaOne],
      },
    ],
    playlists: [
      {
        id: "pl-long",
        name: "Midnight Late Night Synthesizer & Ambient Listening Mix Tape Volume One",
        trackIds: ["t-a1", "t-c1"],
      },
    ],
  };
}

/**
 * Create a settled Library controller positioned on the specified view stack kind.
 *
 * @param {string} [targetViewKind="albums"] Hub section id (`albums`, `all-songs`, `artists`, `playlists`).
 * @returns {object} Hydrated and settled Library controller.
 */
function createSettledLibraryViewController(targetViewKind = "albums") {
  const controller = createLibraryController({});
  controller.hydrateCatalog(buildLibraryScreenCatalog());
  controller.enterFromHome();
  for (let stepIndex = 0; stepIndex < 30; stepIndex += 1) {
    controller.step(0.05);
  }

  if (targetViewKind !== "hub") {
    const sectionIndexMap = {
      favorites: 0,
      playlists: 1,
      artists: 2,
      albums: 3,
      genres: 4,
      "all-songs": 5,
    };
    const sectionIndex = sectionIndexMap[targetViewKind] ?? 3;
    const scrollPair = Math.floor(sectionIndex / 2);
    for (let pairIndex = 0; pairIndex < scrollPair; pairIndex += 1) {
      controller.stepDetent(1);
    }
    for (let stepIndex = 0; stepIndex < 30; stepIndex += 1) {
      controller.step(0.05);
    }
    const slotInPair = sectionIndex % 2;
    const tileX = slotInPair === 0 ? 124 : 236;
    const tileY = 176;
    controller.handlePointerDown(tileX, tileY);
    controller.handlePointerUp(tileX, tileY);
    for (let stepIndex = 0; stepIndex < 30; stepIndex += 1) {
      controller.step(0.05);
    }
  }

  return controller;
}

describe("library_screen.js marquee and helper exports", () => {
  test("exports drawMarqueeCanvasText and draws fitting text statically without clip or horizontal translation", () => {
    assert.equal(typeof libraryScreen.drawMarqueeCanvasText, "function");

    const fitText = "Short Title";
    const sampleTimes = [0, 2.5, 6.0];
    for (const alignMode of ["left", "center"]) {
      const anchorX = alignMode === "left" ? 64 : DISPLAY_CENTER_PX;
      for (const sampleTimeSeconds of sampleTimes) {
        const drawingContext = createRecordingCanvasContext();
        drawingContext.font = MARQUEE_TEXT_FONT;
        drawingContext.textAlign = alignMode;
        drawingContext.textBaseline = "middle";

        libraryScreen.drawMarqueeCanvasText(
          drawingContext,
          fitText,
          anchorX,
          120,
          MARQUEE_MAX_WIDTH_PX,
          sampleTimeSeconds,
        );

        assert.equal(drawingContext.recordedClips.length, 0);
        assert.equal(drawingContext.recordedTranslations.length, 0);
        assert.equal(drawingContext.recordedTextCalls.length, 1);
        assert.equal(drawingContext.recordedTextCalls[0].text, fitText);
        assert.ok(
          Math.abs(drawingContext.recordedTextCalls[0].worldX - anchorX) < FLOAT_TOLERANCE,
        );
      }
    }
  });

  test("drawMarqueeCanvasText clips overflowing text to bounds and advances X smoothly with start pause, gap, and seamless loop", () => {
    assert.equal(typeof libraryScreen.drawMarqueeCanvasText, "function");

    const overflowText = "Symphony No. 9 In D Minor Choral Movement";
    for (const alignMode of ["left", "center"]) {
      const anchorX = alignMode === "left" ? 60 : DISPLAY_CENTER_PX;
      const leftBoundX =
        alignMode === "center" ? anchorX - MARQUEE_MAX_WIDTH_PX / 2 : anchorX;
      const rightBoundX = leftBoundX + MARQUEE_MAX_WIDTH_PX;

      const observedFrames = [];
      for (
        let sampleTimeSeconds = 0;
        sampleTimeSeconds <= MARQUEE_MAX_CYCLE_SECONDS;
        sampleTimeSeconds = Number((sampleTimeSeconds + MARQUEE_SAMPLE_STEP_SECONDS).toFixed(2))
      ) {
        const drawingContext = createRecordingCanvasContext();
        drawingContext.font = MARQUEE_TEXT_FONT;
        drawingContext.textAlign = alignMode;
        drawingContext.textBaseline = "middle";

        libraryScreen.drawMarqueeCanvasText(
          drawingContext,
          overflowText,
          anchorX,
          140,
          MARQUEE_MAX_WIDTH_PX,
          sampleTimeSeconds,
        );

        assert.ok(
          drawingContext.recordedClips.length >= 1,
          `expected clip at t=${sampleTimeSeconds}s for ${alignMode}`,
        );
        const clipRect = drawingContext.recordedClips[0].rects[0];
        assert.ok(clipRect, "expected rectangular clip bounds for overflowing marquee");
        assert.ok(clipRect.worldLeftX >= leftBoundX - 4 && clipRect.worldLeftX <= leftBoundX + 4);
        assert.ok(clipRect.width <= MARQUEE_MAX_WIDTH_PX + 6);

        const matchingCalls = drawingContext.recordedTextCalls.filter(
          (call) => call.text === overflowText,
        );
        assert.ok(matchingCalls.length >= 1);
        observedFrames.push({
          timeSeconds: sampleTimeSeconds,
          measuredWidth: matchingCalls[0].measuredWidth,
          leftEdges: matchingCalls.map(computeRenderedLeftEdgeX),
        });
      }

      const frameAtZero = observedFrames[0];
      const frameAtPointTwo = observedFrames.find(
        (frame) => Math.abs(frame.timeSeconds - 0.2) < FLOAT_TOLERANCE,
      );
      assert.ok(Math.abs(frameAtZero.leftEdges[0] - leftBoundX) <= 1.5);
      assert.ok(
        Math.abs(frameAtPointTwo.leftEdges[0] - frameAtZero.leftEdges[0]) <= 0.5,
        "initial start pause should keep text stationary between t=0 and t=0.2s",
      );

      const primaryEdges = observedFrames.map((frame) => frame.leftEdges[0]);
      const distinctScrolledPositions = new Set(
        primaryEdges
          .filter((edgeX) => edgeX < leftBoundX - 2)
          .map((edgeX) => Math.round(edgeX * 2) / 2),
      );
      assert.ok(
        distinctScrolledPositions.size >= 4,
        "marquee should advance X smoothly across multiple positions",
      );

      const minLeftEdge = Math.min(...primaryEdges);
      assert.ok(
        minLeftEdge + frameAtZero.measuredWidth <= rightBoundX + 6,
        "marquee should scroll far enough for the end of overflowing text to reach view",
      );

      const firstScrollAwayIndex = observedFrames.findIndex(
        (frame) => frame.leftEdges[0] < leftBoundX - 15,
      );
      assert.ok(firstScrollAwayIndex > 0);
      const loopedBack = observedFrames
        .slice(firstScrollAwayIndex + 1)
        .some((frame) =>
          frame.leftEdges.some((edgeX) => Math.abs(edgeX - leftBoundX) <= 3.5),
        );
      assert.equal(loopedBack, true, "marquee should loop smoothly back to start position");
    }
  });

  test("exports drawCrateAlbumSleeveBox and renders sleeve geometry without vinyl disc arcs", () => {
    assert.equal(typeof libraryScreen.drawCrateAlbumSleeveBox, "function");

    const drawingContext = createRecordingCanvasContext();
    const albumSpec = {
      id: "album-a",
      title: "Alpha Horizons",
      artist: "Aurora Ensemble",
      motif: "prism",
    };

    libraryScreen.drawCrateAlbumSleeveBox(drawingContext, albumSpec, {
      centerX: 172,
      centerY: 152,
      sleeveSize: 120,
      focusWeight: 1,
      delta: 0,
      baseAlpha: 1,
      alpha: 1,
      palette: APPEARANCE_PALETTES.dark,
      appearance: "dark",
      coverCache: null,
    });

    assert.ok(drawingContext.recordedRects.length > 0);
    assert.ok(drawingContext.recordedStrokes.length > 0);
    const peekingDiscArcs = drawingContext.recordedArcs.filter(
      (arcEntry) => arcEntry.worldCenterX > 195 && arcEntry.radius >= 45 && arcEntry.radius <= 56,
    );
    assert.equal(peekingDiscArcs.length, 0);
  });

  test("drawLibraryVinylDisc supports { showCenterLabel: false } without drawing inner white label ring or spindle dot", () => {
    const labelRadius = VINYL_FULL_RADIUS_PX * VINYL_CENTER_LABEL_RATIO;
    const prismAlbum = {
      id: "album-a",
      title: "Alpha Horizons",
      artist: "Aurora Ensemble",
      motif: "prism",
    };

    const defaultContext = createRecordingCanvasContext();
    libraryScreen.drawLibraryVinylDisc(
      defaultContext,
      prismAlbum,
      DISPLAY_CENTER_PX,
      DISPLAY_CENTER_PX,
      VINYL_FULL_RADIUS_PX,
      0.5,
      APPEARANCE_PALETTES.dark,
      "dark",
      1,
      null,
    );
    const defaultLabelStrokes = defaultContext.recordedStrokes.filter((strokeEntry) =>
      strokeEntry.arcs.some(
        (arcEntry) => Math.abs(arcEntry.radius - labelRadius) <= 2,
      ),
    );
    assert.ok(defaultLabelStrokes.length >= 1, "default drawLibraryVinylDisc draws center label");

    const labelFreeContext = createRecordingCanvasContext();
    libraryScreen.drawLibraryVinylDisc(
      labelFreeContext,
      prismAlbum,
      DISPLAY_CENTER_PX,
      DISPLAY_CENTER_PX,
      VINYL_FULL_RADIUS_PX,
      0.5,
      APPEARANCE_PALETTES.dark,
      "dark",
      1,
      null,
      { showCenterLabel: false },
    );

    const hiddenLabelStrokes = labelFreeContext.recordedStrokes.filter((strokeEntry) =>
      strokeEntry.arcs.some(
        (arcEntry) => Math.abs(arcEntry.radius - labelRadius) <= 4,
      ),
    );
    const hiddenSpindleFills = labelFreeContext.recordedFills.filter((fillEntry) =>
      fillEntry.arcs.some(
        (arcEntry) => arcEntry.radius <= labelRadius + 2,
      ),
    );
    assert.equal(hiddenLabelStrokes.length, 0, "must omit white center label ring");
    assert.equal(hiddenSpindleFills.length, 0, "must omit center label fill and spindle dot");
  });
});

describe("library_screen.js Albums Crate peeking disc, floating note button, and chrome removals", () => {
  test("renderLibraryAlbumCrate hides peeking vinyl disc on idle and non-playing albums and rotates only when playing", () => {
    const controller = createSettledLibraryViewController("albums");

    function findPeekingDiscArcs(drawingContext) {
      return drawingContext.recordedArcs.filter(
        (arcEntry) =>
          arcEntry.worldCenterX > 195 &&
          arcEntry.worldCenterX < 250 &&
          arcEntry.worldRadius >= 44 &&
          arcEntry.worldRadius <= 58,
      );
    }

    // 1. Idle (activeTrackId === null): focused album must NOT show peeking disc
    const idleState = controller.getRenderSnapshot();
    assert.equal(idleState.activeTrackId, null);
    const idleContext = createRecordingCanvasContext();
    libraryScreen.renderLibraryScreen(idleContext, idleState, {
      appearance: "dark",
      palette: APPEARANCE_PALETTES.dark,
      playback: { isPlaying: false, elapsedSeconds: 0, durationSeconds: 180, volumeLevel: 0.6 },
    });
    assert.equal(
      findPeekingDiscArcs(idleContext).length,
      0,
      "idle album crate must hide peeking vinyl disc",
    );

    // 2. Non-playing album focused (activeTrackId belongs to album-c while album-a is focused)
    controller.setActiveTrackId("t-c1");
    const nonPlayingFocusState = controller.getRenderSnapshot();
    const nonPlayingContext = createRecordingCanvasContext();
    libraryScreen.renderLibraryScreen(nonPlayingContext, nonPlayingFocusState, {
      appearance: "dark",
      palette: APPEARANCE_PALETTES.dark,
      playback: { isPlaying: true, elapsedSeconds: 30, durationSeconds: 180, volumeLevel: 0.6 },
    });
    assert.equal(
      findPeekingDiscArcs(nonPlayingContext).length,
      0,
      "non-playing focused album must hide peeking vinyl disc",
    );

    // 3. Playing album focused while PAUSED: disc peeks but must NOT rotate
    controller.setActiveTrackId("t-a1");
    const pausedPlayingAlbumState = {
      ...controller.getRenderSnapshot(),
      vinylRotationRadians: 1.35,
      isPlaying: false,
    };
    const pausedContext = createRecordingCanvasContext();
    libraryScreen.renderLibraryScreen(pausedContext, pausedPlayingAlbumState, {
      appearance: "dark",
      palette: APPEARANCE_PALETTES.dark,
      playback: { isPlaying: false, elapsedSeconds: 30, durationSeconds: 180, volumeLevel: 0.6 },
    });
    assert.ok(
      findPeekingDiscArcs(pausedContext).length >= 1,
      "active paused album should show peeking vinyl disc",
    );
    assert.equal(
      pausedContext.recordedRotations.some(
        (angleRadians) => Math.abs(angleRadians - 1.35) < FLOAT_TOLERANCE,
      ),
      false,
      "paused playing album peeking disc must not rotate",
    );

    // 4. Playing album focused while PLAYING: disc peeks and rotates by vinylRotationRadians
    const playingAlbumState = {
      ...controller.getRenderSnapshot(),
      vinylRotationRadians: 1.35,
      isPlaying: true,
    };
    const playingContext = createRecordingCanvasContext();
    libraryScreen.renderLibraryScreen(playingContext, playingAlbumState, {
      appearance: "dark",
      palette: APPEARANCE_PALETTES.dark,
      playback: { isPlaying: true, elapsedSeconds: 30, durationSeconds: 180, volumeLevel: 0.6 },
    });
    assert.ok(
      findPeekingDiscArcs(playingContext).length >= 1,
      "currently playing album should show peeking vinyl disc",
    );
    assert.equal(
      playingContext.recordedRotations.some(
        (angleRadians) => Math.abs(angleRadians - 1.35) < FLOAT_TOLERANCE,
      ),
      true,
      "playing album peeking disc should rotate with vinylRotationRadians",
    );
  });

  test("renderLibraryAlbumCrate omits SWIPE UP · TRACKS, draws floating music-note button near (180, 325), and hitTestLibraryInteractive matches it", () => {
    const controller = createSettledLibraryViewController("albums");
    const idleCrateState = controller.getRenderSnapshot();
    assert.equal(idleCrateState.activeTrackId, null);

    const drawingContext = createRecordingCanvasContext();
    libraryScreen.renderLibraryScreen(drawingContext, idleCrateState, {
      appearance: "dark",
      palette: APPEARANCE_PALETTES.dark,
      playback: { isPlaying: false, elapsedSeconds: 0, durationSeconds: 180, volumeLevel: 0.6 },
    });

    const swipeUpHintTexts = drawingContext.recordedTextCalls.filter((call) =>
      /SWIPE UP/i.test(call.text),
    );
    assert.equal(swipeUpHintTexts.length, 0, "must not draw SWIPE UP · TRACKS anywhere");

    const noteButtonCircles = drawingContext.recordedArcs.filter(
      (arcEntry) =>
        Math.hypot(
          arcEntry.worldCenterX - CRATE_NOTE_BUTTON_X,
          arcEntry.worldCenterY - CRATE_NOTE_BUTTON_Y,
        ) <= 4 &&
        arcEntry.worldRadius >= 14 &&
        arcEntry.worldRadius <= 26,
    );
    assert.ok(
      noteButtonCircles.length >= 1,
      "must draw bottom-center floating music-note button near (180, 325) even on non-playing album",
    );

    assert.equal(
      libraryScreen.hitTestLibraryInteractive(
        CRATE_NOTE_BUTTON_X,
        CRATE_NOTE_BUTTON_Y,
        idleCrateState,
      ),
      true,
    );
    assert.equal(
      libraryScreen.hitTestLibraryInteractive(
        CRATE_NOTE_BUTTON_EDGE_X,
        CRATE_NOTE_BUTTON_Y,
        idleCrateState,
      ),
      true,
    );
    assert.equal(
      libraryScreen.hitTestLibraryInteractive(
        CRATE_OLD_HINT_OUTSIDE_X,
        CRATE_NOTE_BUTTON_Y,
        idleCrateState,
      ),
      false,
      "hit testing must scope to circular floating note button instead of old wide SWIPE UP hint box",
    );
  });
});

describe("library_screen.js global marquee across curved lists, crate, tracklists, and folder rows", () => {
  /**
   * Assert that a specific overflowing label renders untruncated inside a bounded clip
   * and shifts horizontal X position between t=0 and later animation timestamps.
   *
   * @param {(timeSeconds: number) => object} renderAtTime Render callback returning mock context.
   * @param {string} targetText Full expected overflowing text string.
   * @param {number} maxExpectedClipWidth Maximum expected clip rectangle width in pixels.
   */
  function assertRenderedTextMarquees(renderAtTime, targetText, maxExpectedClipWidth) {
    const startContext = renderAtTime(0);
    const startCalls = startContext.recordedTextCalls.filter((call) => call.text === targetText);
    assert.ok(
      startCalls.length >= 1,
      `expected untruncated marquee text "${targetText}" instead of ellipsis truncation`,
    );

    const targetWorldY = startCalls[0].worldY;
    const boundedClips = startContext.recordedClips.filter((clipEntry) =>
      clipEntry.rects.some(
        (rectEntry) =>
          rectEntry.width <= maxExpectedClipWidth &&
          targetWorldY >= rectEntry.worldTopY - 4 &&
          targetWorldY <= rectEntry.worldTopY + rectEntry.height + 4,
      ),
    );
    assert.ok(boundedClips.length >= 1, `expected bounded marquee clip around "${targetText}"`);

    const initialLeftEdgeX = computeRenderedLeftEdgeX(startCalls[0]);
    const shifted = [2.5, 3.5, 5.0].some((laterTimeSeconds) => {
      const laterContext = renderAtTime(laterTimeSeconds);
      const laterCalls = laterContext.recordedTextCalls.filter((call) => call.text === targetText);
      return laterCalls.some(
        (call) => Math.abs(computeRenderedLeftEdgeX(call) - initialLeftEdgeX) > 2,
      );
    });
    assert.equal(shifted, true, `expected "${targetText}" X offset to advance over time`);
  }

  test("curved list track rows, album tracklist overlay, folder rows, and album crate use marquee scrolling when overflowing", () => {
    // 1. All-songs curved list track row (overflowing title and artist)
    const songsController = createSettledLibraryViewController("all-songs");
    songsController.stepDetent(1);
    for (let stepIndex = 0; stepIndex < 30; stepIndex += 1) {
      songsController.step(0.05);
    }
    const songsState = songsController.getRenderSnapshot();
    const renderSongsAtTime = (timeSeconds) => {
      const drawingContext = createRecordingCanvasContext();
      libraryScreen.renderLibraryScreen(
        drawingContext,
        { ...songsState, idleClockSeconds: timeSeconds },
        {
          appearance: "dark",
          palette: APPEARANCE_PALETTES.dark,
          animationTimeSeconds: timeSeconds,
        },
      );
      return drawingContext;
    };
    assertRenderedTextMarquees(
      renderSongsAtTime,
      "01 Long Overflowing Track Movement In C Sharp Minor Opus Ninety Nine",
      240,
    );
    assertRenderedTextMarquees(
      renderSongsAtTime,
      "EXTRAORDINARILY LONG PHILHARMONIC ORCHESTRA NAME FOR MARQUEE TESTING",
      240,
    );

    // 2. Folder/category row (artists view)
    const artistsController = createSettledLibraryViewController("artists");
    artistsController.stepDetent(1);
    for (let stepIndex = 0; stepIndex < 30; stepIndex += 1) {
      artistsController.step(0.05);
    }
    const artistsState = artistsController.getRenderSnapshot();
    assertRenderedTextMarquees(
      (timeSeconds) => {
        const drawingContext = createRecordingCanvasContext();
        libraryScreen.renderLibraryScreen(
          drawingContext,
          { ...artistsState, idleClockSeconds: timeSeconds },
          {
            appearance: "dark",
            palette: APPEARANCE_PALETTES.dark,
            animationTimeSeconds: timeSeconds,
          },
        );
        return drawingContext;
      },
      "Extraordinarily Long Philharmonic Orchestra Name For Marquee Testing",
      240,
    );

    // 3. Albums crate overflowing album title + artist metadata and album tracklist overlay
    const albumsController = createSettledLibraryViewController("albums");
    albumsController.stepDetent(1);
    albumsController.stepDetent(1);
    for (let stepIndex = 0; stepIndex < 30; stepIndex += 1) {
      albumsController.step(0.05);
    }
    const crateState = albumsController.getRenderSnapshot();
    assertRenderedTextMarquees(
      (timeSeconds) => {
        const drawingContext = createRecordingCanvasContext();
        libraryScreen.renderLibraryScreen(
          drawingContext,
          { ...crateState, idleClockSeconds: timeSeconds },
          {
            appearance: "dark",
            palette: APPEARANCE_PALETTES.dark,
            animationTimeSeconds: timeSeconds,
          },
        );
        return drawingContext;
      },
      "Gamma Nocturnes With An Extraordinarily Long Symphony Title For Marquee",
      240,
    );

    albumsController.openFocusedAlbumTracks();
    for (let stepIndex = 0; stepIndex < 30; stepIndex += 1) {
      albumsController.step(0.05);
    }
    const albumTracksState = albumsController.getRenderSnapshot();
    assert.equal(albumTracksState.albumTracksOverlayOpen, true);
    assertRenderedTextMarquees(
      (timeSeconds) => {
        const drawingContext = createRecordingCanvasContext();
        libraryScreen.renderLibraryScreen(
          drawingContext,
          { ...albumTracksState, idleClockSeconds: timeSeconds },
          {
            appearance: "dark",
            palette: APPEARANCE_PALETTES.dark,
            animationTimeSeconds: timeSeconds,
          },
        );
        return drawingContext;
      },
      "01 Long Overflowing Track Movement In C Sharp Minor Opus Ninety Nine",
      240,
    );
  });
});
