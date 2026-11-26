import { describe, test } from "node:test";
import assert from "node:assert/strict";

import { computeSortWheelSlots, createLibraryController } from "../core/library_controller.js";
import { APPEARANCE_PALETTES } from "../core/tokens.js";
import * as libraryScreen from "../screens/library_screen.js";

const DISPLAY_CENTER_PX = 180;
const FLOAT_TOLERANCE = 1e-4;
const CRATE_SORT_BUTTON_X = 152;
const ARTISTS_SORT_BUTTON_X = 180;
const CRATE_NOTE_BUTTON_X = 208;
const CRATE_NOTE_BUTTON_Y = 325;
const CRATE_NOTE_BUTTON_EDGE_X = 224;
const CRATE_OLD_HINT_OUTSIDE_X = 248;

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

    const peekDiscGreenArcs = playingContext.recordedStrokes.filter(
      (strokeEntry) =>
        String(strokeEntry.strokeStyle || "").toUpperCase() ===
          APPEARANCE_PALETTES.dark.signalHighlight.toUpperCase() &&
        strokeEntry.arcs.some(
          (arcEntry) => arcEntry.worldCenterX > 195 && arcEntry.worldRadius >= 44,
        ),
    );
    assert.equal(
      peekDiscGreenArcs.length,
      0,
      "peeking vinyl in Albums crate must omit green outer rim arc",
    );

    const sleeveGreenCornerBrackets = playingContext.recordedStrokes.filter(
      (strokeEntry) =>
        String(strokeEntry.strokeStyle || "").toUpperCase() ===
          APPEARANCE_PALETTES.dark.signalHighlight.toUpperCase() &&
        strokeEntry.arcs.length === 0 &&
        strokeEntry.points.length >= 4,
    );
    assert.ok(
      sleeveGreenCornerBrackets.length >= 1,
      "focused album sleeve must preserve green corner brackets",
    );

    const peekDiscCenterLabels = playingContext.recordedStrokes.filter((strokeEntry) =>
      strokeEntry.arcs.some(
        (arcEntry) =>
          arcEntry.worldCenterX > 195 &&
          arcEntry.worldRadius >= 10 &&
          arcEntry.worldRadius <= 22 &&
          /#F5F6F8/i.test(String(strokeEntry.strokeStyle || "")),
      ),
    );
    assert.equal(
      peekDiscCenterLabels.length,
      0,
      "peeking vinyl in Albums crate must pass { showCenterLabel: false }",
    );
  });

  test("renderLibraryAlbumCrate omits SWIPE UP · TRACKS, draws Sort (152, 325) and Note (208, 325) buttons, and hitTestLibraryInteractive matches both", () => {
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

    const sortButtonCircles = drawingContext.recordedArcs.filter(
      (arcEntry) =>
        Math.hypot(
          arcEntry.worldCenterX - CRATE_SORT_BUTTON_X,
          arcEntry.worldCenterY - CRATE_NOTE_BUTTON_Y,
        ) <= 4 &&
        arcEntry.worldRadius >= 14 &&
        arcEntry.worldRadius <= 26,
    );
    assert.ok(
      sortButtonCircles.length >= 1,
      "must draw bottom-left floating sort button near (152, 325)",
    );

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
      "must draw bottom-right floating music-note button near (208, 325) even on non-playing album",
    );

    assert.equal(
      libraryScreen.hitTestLibraryInteractive(
        CRATE_SORT_BUTTON_X,
        CRATE_NOTE_BUTTON_Y,
        idleCrateState,
      ),
      true,
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

  test("Now Playing -> Albums return transition slides album sleeve rightward, shrinks vinyl back to peeking disc, and rotates +20 deg clockwise", () => {
    const TWENTY_DEG_RADIANS = (20 * Math.PI) / 180;
    const RETURN_HANDOFF_ANGLE_RADIANS = 1.15;
    const controller = createSettledLibraryViewController("albums");
    controller.setActiveTrackId("t-a1");

    function renderCrateReturnFrame(stepFraction) {
      controller.returnFromNowPlaying({
        vinylRotationRadians: RETURN_HANDOFF_ANGLE_RADIANS,
        albumId: "album-a",
        launchOrigin: "album-crate",
        isPlaying: true,
      });
      if (stepFraction > 0) {
        controller.step(stepFraction * 0.62);
      }
      const snapshot = controller.getRenderSnapshot();
      const drawingContext = createRecordingCanvasContext();
      libraryScreen.renderLibraryScreen(drawingContext, snapshot, {
        appearance: "dark",
        palette: APPEARANCE_PALETTES.dark,
        playback: { isPlaying: true, elapsedSeconds: 30, durationSeconds: 180, volumeLevel: 0.6 },
      });

      const sleeveRects = drawingContext.recordedRects.filter(
        (rectEntry) =>
          rectEntry.kind === "strokeRect" &&
          rectEntry.width >= 115 &&
          rectEntry.width <= 125 &&
          rectEntry.height >= 115 &&
          rectEntry.height <= 125,
      );
      const discClipArcs = drawingContext.recordedClips
        .flatMap((clipEntry) => clipEntry.arcs)
        .filter((arcEntry) => arcEntry.worldRadius >= 45);

      const activeDiscClipArc = discClipArcs[discClipArcs.length - 1] || null;
      const greenDiscRimArcStrokes = drawingContext.recordedStrokes.filter(
        (strokeEntry) =>
          String(strokeEntry.strokeStyle || "").toUpperCase() ===
            APPEARANCE_PALETTES.dark.signalHighlight.toUpperCase() &&
          activeDiscClipArc &&
          strokeEntry.arcs.some(
            (arcEntry) =>
              Math.abs(arcEntry.worldRadius - activeDiscClipArc.worldRadius) <= 3 &&
              Math.hypot(
                arcEntry.worldCenterX - activeDiscClipArc.worldCenterX,
                arcEntry.worldCenterY - activeDiscClipArc.worldCenterY,
              ) <= 3,
          ),
      );

      return {
        snapshot,
        sleeveRect: sleeveRects[0] || null,
        discClipArc: activeDiscClipArc,
        discRotationRadians: drawingContext.recordedRotations[0],
        greenArcStrokes: greenDiscRimArcStrokes,
      };
    }

    const startFrame = renderCrateReturnFrame(0.0);
    const earlyReturnFrame = renderCrateReturnFrame(0.45);
    const midReturnFrame = renderCrateReturnFrame(0.5);
    const lateReturnFrame = renderCrateReturnFrame(0.8);
    const settledFrame = renderCrateReturnFrame(1.0);

    for (const sampledFrame of [
      startFrame,
      earlyReturnFrame,
      midReturnFrame,
      lateReturnFrame,
      settledFrame,
    ]) {
      assert.equal(
        sampledFrame.greenArcStrokes.length,
        0,
        "Now Playing -> Albums return transition must not draw a green outer ring on top of the vinyl or sleeve",
      );
    }

    assert.ok(startFrame.discClipArc, "start of return must draw full-bleed vinyl disc");
    assert.ok(Math.abs(startFrame.discClipArc.worldRadius - VINYL_FULL_RADIUS_PX) <= 1);
    assert.ok(earlyReturnFrame.sleeveRect, "sleeve must become visible sliding back rightward");
    assert.ok(midReturnFrame.discClipArc, "mid return must draw shrinking vinyl disc");
    assert.ok(lateReturnFrame.sleeveRect, "late return must draw focused sleeve");

    const earlySleeveCenterX =
      earlyReturnFrame.sleeveRect.worldLeftX + earlyReturnFrame.sleeveRect.width / 2;
    const lateSleeveCenterX =
      lateReturnFrame.sleeveRect.worldLeftX + lateReturnFrame.sleeveRect.width / 2;
    assert.ok(
      lateSleeveCenterX > earlySleeveCenterX && Math.abs(lateSleeveCenterX - 172) <= 25,
      "album sleeve must translate rightward back toward settled crate position (x=172)",
    );

    assert.ok(
      startFrame.discClipArc.worldRadius > midReturnFrame.discClipArc.worldRadius &&
        midReturnFrame.discClipArc.worldRadius > settledFrame.discClipArc.worldRadius,
      "vinyl radius must shrink smoothly from 180px back to peeking radius (~52.8px)",
    );
    assert.ok(Math.abs(settledFrame.discClipArc.worldRadius - 52.8) <= 1.5);
    assert.ok(Math.abs(settledFrame.discClipArc.worldCenterX - 219.2) <= 2);

    assert.ok(
      Math.abs(startFrame.discRotationRadians - RETURN_HANDOFF_ANGLE_RADIANS) < FLOAT_TOLERANCE,
    );
    assert.ok(
      Math.abs(
        midReturnFrame.discRotationRadians - (RETURN_HANDOFF_ANGLE_RADIANS + TWENTY_DEG_RADIANS / 2),
      ) < 0.002,
      "Now Playing -> Albums midpoint must advance +10 deg clockwise",
    );
    assert.ok(
      Math.abs(
        settledFrame.discRotationRadians - (RETURN_HANDOFF_ANGLE_RADIANS + TWENTY_DEG_RADIANS),
      ) < 0.002,
      "Now Playing -> Albums completion must advance +20 deg clockwise cleanly",
    );
  });

  test("Now Playing -> Albums return never draws a center spindle puck or inner volume arc over the vinyl", () => {
    const CENTER_TOLERANCE_PX = 2.5;
    const SPINDLE_MIN_RADIUS_PX = 3;
    const SPINDLE_MAX_RADIUS_PX = 30;
    const VOLUME_ARC_MIN_RADIUS_PX = 164;
    const VOLUME_ARC_MAX_RADIUS_PX = 168;

    function isCenteredArc(arcEntry) {
      return (
        Math.hypot(
          arcEntry.worldCenterX - DISPLAY_CENTER_PX,
          arcEntry.worldCenterY - DISPLAY_CENTER_PX,
        ) <= CENTER_TOLERANCE_PX
      );
    }

    function renderReturnFrame(stepFraction) {
      const controller = createSettledLibraryViewController("albums");
      controller.setActiveTrackId("t-a1");
      controller.returnFromNowPlaying({
        vinylRotationRadians: 1.15,
        albumId: "album-a",
        launchOrigin: "album-crate",
        isPlaying: true,
      });
      if (stepFraction > 0) {
        controller.step(stepFraction * 0.62);
      }
      const drawingContext = createRecordingCanvasContext();
      libraryScreen.renderLibraryScreen(drawingContext, controller.getRenderSnapshot(), {
        appearance: "dark",
        palette: APPEARANCE_PALETTES.dark,
        playback: { isPlaying: true, elapsedSeconds: 30, durationSeconds: 180, volumeLevel: 0.6 },
      });
      return [
        ...drawingContext.recordedFills.flatMap((fillEntry) => fillEntry.arcs),
        ...drawingContext.recordedStrokes.flatMap((strokeEntry) => strokeEntry.arcs),
      ].filter(isCenteredArc);
    }

    for (const stepFraction of [0, 0.1, 0.2, 0.3]) {
      const centeredArcs = renderReturnFrame(stepFraction);
      const spindleArcs = centeredArcs.filter(
        (arcEntry) =>
          arcEntry.worldRadius >= SPINDLE_MIN_RADIUS_PX &&
          arcEntry.worldRadius <= SPINDLE_MAX_RADIUS_PX,
      );
      assert.equal(spindleArcs.length, 0, `no center spindle puck at step fraction ${stepFraction}`);

      const volumeArcs = centeredArcs.filter(
        (arcEntry) =>
          arcEntry.worldRadius >= VOLUME_ARC_MIN_RADIUS_PX &&
          arcEntry.worldRadius <= VOLUME_ARC_MAX_RADIUS_PX,
      );
      assert.equal(volumeArcs.length, 0, `no inner volume arc at step fraction ${stepFraction}`);
    }
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

describe("library_screen.js Shared-Element Albums Tile <-> Albums Roulette transition", () => {
  test("exports renderLibraryHubTransitionLayer and guards missing context or zero reveal cleanly", () => {
    assert.equal(typeof libraryScreen.renderLibraryHubTransitionLayer, "function");
    assert.doesNotThrow(() => libraryScreen.renderLibraryHubTransitionLayer(null, {}));

    const silentContext = createRecordingCanvasContext();
    libraryScreen.renderLibraryHubTransitionLayer(silentContext, {
      revealProgress: 0,
      counts: { tracks: 10, albums: 3, artists: 2, genres: 1, favorites: 1, playlists: 1 },
      palette: APPEARANCE_PALETTES.dark,
      appearance: "dark",
    });
    assert.equal(silentContext.recordedTextCalls.length, 0);
  });

  test("hub-to-albums blooms 1:1 hero sleeve monotonically from Albums tile to crate center (172, 152) and albums-to-hub reverses cleanly in Dark and Light modes", () => {
    const controller = createLibraryController({});
    controller.hydrateCatalog(buildLibraryScreenCatalog());
    controller.enterFromHome({ skipEntryTransition: true });
    controller.stepDetent(1);
    for (let stepIndex = 0; stepIndex < 30; stepIndex += 1) {
      controller.step(0.05);
    }

    /* Tap focused Albums tile (row 1 right column: x=244, y=152) */
    controller.handlePointerDown(244, 152);
    controller.handlePointerUp(244, 152);

    function findHeroSleeveRect(drawingContext) {
      const squareStrokes = drawingContext.recordedRects.filter(
        (rectEntry) =>
          rectEntry.kind === "strokeRect" &&
          Math.abs(rectEntry.width - rectEntry.height) <= 1.5 &&
          rectEntry.width >= 20 &&
          rectEntry.width <= 122,
      );
      return squareStrokes.reduce(
        (largest, candidate) => (!largest || candidate.width > largest.width ? candidate : largest),
        null,
      );
    }

    const startContext = createRecordingCanvasContext();
    libraryScreen.renderLibraryScreen(startContext, controller.getRenderSnapshot(), {
      appearance: "dark",
      palette: APPEARANCE_PALETTES.dark,
    });
    const startSleeve = findHeroSleeveRect(startContext);
    assert.ok(startSleeve, "start of hub-to-albums must render hero square sleeve at Albums tile");

    controller.step(0.26);
    const midSnapshot = controller.getRenderSnapshot();
    assert.equal(midSnapshot.albumsTransitionMode, "hub-to-albums");
    const midContext = createRecordingCanvasContext();
    libraryScreen.renderLibraryScreen(midContext, midSnapshot, {
      appearance: "dark",
      palette: APPEARANCE_PALETTES.dark,
    });
    const midSleeve = findHeroSleeveRect(midContext);
    assert.ok(midSleeve, "midpoint of hub-to-albums must render growing 1:1 hero sleeve");
    assert.ok(
      midSleeve.width > startSleeve.width + 15 && midSleeve.width < 118,
      `expected monotonic 1:1 growth, start=${startSleeve.width}, mid=${midSleeve.width}`,
    );
    assert.ok(
      Math.abs(midSleeve.width - midSleeve.height) <= 1.5,
      "hero sleeve must stay strictly 1:1 undeformed during transition",
    );

    const startCenterX = startSleeve.worldLeftX + startSleeve.width / 2;
    const midCenterX = midSleeve.worldLeftX + midSleeve.width / 2;
    assert.ok(
      midCenterX < startCenterX && midCenterX > 170,
      `hero sleeve should glide smoothly leftward from tile (${startCenterX}) toward crate center (172), got ${midCenterX}`,
    );
    assert.equal(midContext.getCounts().saveCount, midContext.getCounts().restoreCount);

    controller.step(0.30);
    const settledContext = createRecordingCanvasContext();
    libraryScreen.renderLibraryScreen(settledContext, controller.getRenderSnapshot(), {
      appearance: "dark",
      palette: APPEARANCE_PALETTES.dark,
    });
    const settledSleeve = findHeroSleeveRect(settledContext);
    assert.ok(settledSleeve && Math.abs(settledSleeve.width - 119) <= 2);

    /* Reverse (albums-to-hub) in Light Mode */
    controller.stepBack();
    controller.step(0.26);
    const reverseSnapshot = controller.getRenderSnapshot();
    assert.equal(reverseSnapshot.albumsTransitionMode, "albums-to-hub");
    const reverseContext = createRecordingCanvasContext();
    libraryScreen.renderLibraryScreen(reverseContext, reverseSnapshot, {
      appearance: "light",
      palette: APPEARANCE_PALETTES.light,
    });
    const reverseSleeve = findHeroSleeveRect(reverseContext);
    assert.ok(
      reverseSleeve && reverseSleeve.width > 30 && reverseSleeve.width < 118,
      `reverse albums-to-hub should shrink 1:1 sleeve back toward Albums tile, got ${reverseSleeve?.width}`,
    );
    assert.equal(reverseContext.getCounts().saveCount, reverseContext.getCounts().restoreCount);
  });

  test("renders Artist header segmented toggle (ALBUMS | SONGS) in Dark and Light Mode, omits it on non-artist views, shows album subtitle in Artist Songs, and hit-tests cleanly", () => {
    const controller = createLibraryController({});
    controller.hydrateCatalog(buildLibraryScreenCatalog());
    controller.enterFromHome({ skipEntryTransition: true });
    // Open Artists (tile index 2), then first artist ("Alpha Ensemble")
    controller.stepDetent(1);
    for (let stepIndex = 0; stepIndex < 20; stepIndex += 1) {
      controller.step(0.05);
    }
    controller.activateFocused(2);
    controller.activateFocused(0);
    for (let stepIndex = 0; stepIndex < 30; stepIndex += 1) {
      controller.step(0.05);
    }

    const artistAlbumsState = controller.getRenderSnapshot();
    assert.equal(artistAlbumsState.currentView.isArtistView, true);

    const albumsDarkContext = createRecordingCanvasContext();
    libraryScreen.renderLibraryScreen(albumsDarkContext, artistAlbumsState, {
      appearance: "dark",
      palette: APPEARANCE_PALETTES.dark,
    });
    const albumsLabel = albumsDarkContext.recordedTextCalls.find((call) => call.text === "ALBUMS");
    const songsLabel = albumsDarkContext.recordedTextCalls.find((call) => call.text === "SONGS");
    assert.ok(albumsLabel && songsLabel, "Artist view must render ALBUMS and SONGS segmented labels");
    assert.ok(albumsLabel.worldY >= 53 && albumsLabel.worldY <= 73);
    assert.ok(songsLabel.worldY >= 53 && songsLabel.worldY <= 73);
    assert.equal(libraryScreen.hitTestLibraryInteractive(144, 63, artistAlbumsState), true);
    assert.equal(libraryScreen.hitTestLibraryInteractive(216, 63, artistAlbumsState), true);

    // Switch to Songs mode and render in Light Mode: verify albumTitle subtitle is shown
    controller.setArtistViewMode("songs");
    for (let stepIndex = 0; stepIndex < 30; stepIndex += 1) {
      controller.step(0.05);
    }
    const artistSongsState = controller.getRenderSnapshot();
    const songsLightContext = createRecordingCanvasContext();
    libraryScreen.renderLibraryScreen(songsLightContext, artistSongsState, {
      appearance: "light",
      palette: APPEARANCE_PALETTES.light,
    });
    assert.ok(songsLightContext.recordedTextCalls.some((call) => call.text === "ALBUMS"));
    assert.ok(songsLightContext.recordedTextCalls.some((call) => call.text === "SONGS"));
    assert.ok(
      songsLightContext.recordedTextCalls.some((call) => call.text === "ALPHA HORIZONS"),
      "Artist Songs rows must display uppercase albumTitle subtitle",
    );
    assert.equal(libraryScreen.hitTestLibraryInteractive(144, 63, artistSongsState), true);
    assert.equal(libraryScreen.hitTestLibraryInteractive(216, 63, artistSongsState), true);

    // Global Albums crate must NOT render segmented toggle or hit-test header toggle
    const globalAlbumsController = createSettledLibraryViewController("albums");
    const globalAlbumsState = globalAlbumsController.getRenderSnapshot();
    const globalAlbumsContext = createRecordingCanvasContext();
    libraryScreen.renderLibraryScreen(globalAlbumsContext, globalAlbumsState, {
      appearance: "dark",
      palette: APPEARANCE_PALETTES.dark,
    });
    assert.equal(
      globalAlbumsContext.recordedTextCalls.some((call) => call.text === "SONGS"),
      false,
    );
    assert.equal(libraryScreen.hitTestLibraryInteractive(216, 63, globalAlbumsState), false);
  });

  test("liked album sleeve renders top-right badge, sort toast & quick actions overlay render, and Favorites renders ALBUMS | SONGS + inline empty states", () => {
    const controller = createSettledLibraryViewController("albums");
    controller.setAlbumFavorite("album-a", true);
    controller.cycleAlbumSortMode();
    for (let stepIndex = 0; stepIndex < 15; stepIndex += 1) {
      controller.step(0.05);
    }

    const sortedReverseState = controller.getRenderSnapshot();
    const reverseCrateContext = createRecordingCanvasContext();
    libraryScreen.renderLibraryScreen(reverseCrateContext, sortedReverseState, {
      appearance: "dark",
      palette: APPEARANCE_PALETTES.dark,
    });
    assert.ok(
      reverseCrateContext.recordedTextCalls.some((call) => call.text === "SORT \u00b7 Z\u2013A"),
      "must render Z-A sort toast pill on first cycle",
    );

    controller.cycleAlbumSortMode();
    for (let stepIndex = 0; stepIndex < 15; stepIndex += 1) {
      controller.step(0.05);
    }
    const sortedLikedState = controller.getRenderSnapshot();
    const darkCrateContext = createRecordingCanvasContext();
    libraryScreen.renderLibraryScreen(darkCrateContext, sortedLikedState, {
      appearance: "dark",
      palette: APPEARANCE_PALETTES.dark,
    });
    assert.ok(
      darkCrateContext.recordedTextCalls.some((call) => call.text === "SORT \u00b7 LIKED FIRST"),
      "must render sort toast pill when cycling to LIKED FIRST",
    );

    // Open Album Quick Action overlay and verify ALBUM ACTIONS + FAVORITED + hit-test isolation
    controller.openAlbumQuickActions("album-a");
    for (let stepIndex = 0; stepIndex < 20; stepIndex += 1) {
      controller.step(0.05);
    }
    const quickActionState = controller.getRenderSnapshot();
    const quickActionContext = createRecordingCanvasContext();
    libraryScreen.renderLibraryScreen(quickActionContext, quickActionState, {
      appearance: "light",
      palette: APPEARANCE_PALETTES.light,
    });
    assert.ok(quickActionContext.recordedTextCalls.some((call) => call.text === "ALBUM ACTIONS"));
    assert.ok(quickActionContext.recordedTextCalls.some((call) => call.text === "FAVORITED"));
    assert.equal(libraryScreen.hitTestLibraryInteractive(180, 196, quickActionState), true);
    assert.equal(libraryScreen.hitTestLibraryInteractive(CRATE_NOTE_BUTTON_X, CRATE_NOTE_BUTTON_Y, quickActionState), false);

    // Favorites dual-mode view with liked album & 0 liked songs -> renders ALBUMS | SONGS and inline empty SONGS state
    const favoritesController = createLibraryController();
    const favCatalog = buildLibraryScreenCatalog();
    favoritesController.hydrateCatalog(favCatalog);
    for (const trackEntry of favCatalog.allTracks) {
      favoritesController.setTrackFavorite(trackEntry.id, false);
    }
    favoritesController.setAlbumFavorite("album-a", true);
    favoritesController.enterFromHome({ skipEntryTransition: true });
    favoritesController.activateFocused(0);
    for (let stepIndex = 0; stepIndex < 20; stepIndex += 1) {
      favoritesController.step(0.05);
    }
    favoritesController.setFavoritesViewMode("songs");
    for (let stepIndex = 0; stepIndex < 20; stepIndex += 1) {
      favoritesController.step(0.05);
    }
    const favEmptySongsState = favoritesController.getRenderSnapshot();
    const favContext = createRecordingCanvasContext();
    libraryScreen.renderLibraryScreen(favContext, favEmptySongsState, {
      appearance: "dark",
      palette: APPEARANCE_PALETTES.dark,
    });
    assert.ok(favContext.recordedTextCalls.some((call) => call.text === "ALBUMS"));
    assert.ok(favContext.recordedTextCalls.some((call) => call.text === "SONGS"));
    assert.ok(favContext.recordedTextCalls.some((call) => call.text === "No Favorite Songs"));
    assert.equal(libraryScreen.hitTestLibraryInteractive(144, 63, favEmptySongsState), true);
  });

  test("renderLibrarySortWheel draws 5 radial medallions, live mode readout, and hitTestLibraryInteractive matches slots", () => {
    const controller = createSettledLibraryViewController("albums");
    controller.handlePointerDown(CRATE_SORT_BUTTON_X, CRATE_NOTE_BUTTON_Y);
    for (let stepIndex = 0; stepIndex < 12; stepIndex += 1) {
      controller.step(0.05);
    }

    const neutralWheelState = controller.getRenderSnapshot();
    assert.equal(neutralWheelState.sortWheelOpen, true);
    const neutralContext = createRecordingCanvasContext();
    libraryScreen.renderLibraryScreen(neutralContext, neutralWheelState, {
      appearance: "dark",
      palette: APPEARANCE_PALETTES.dark,
    });
    assert.ok(
      neutralContext.recordedTextCalls.some((call) => call.text === "DRAG TO SORT"),
      "neutral sort wheel must display DRAG TO SORT readout",
    );

    const slots = computeSortWheelSlots();
    assert.equal(slots.length, 5);
    for (const slot of slots) {
      const matchedArc = neutralContext.recordedArcs.some(
        (arcEntry) =>
          Math.hypot(arcEntry.worldCenterX - slot.centerX, arcEntry.worldCenterY - slot.centerY) <=
            3 && arcEntry.worldRadius >= 14,
      );
      assert.ok(matchedArc, `must render medallion arc for ${slot.mode}`);
      assert.equal(
        libraryScreen.hitTestLibraryInteractive(slot.centerX, slot.centerY, neutralWheelState),
        true,
      );
    }

    const reverseSlot = slots.find((slot) => slot.mode === "reverse-alphabetical");
    controller.handlePointerMove(reverseSlot.centerX, reverseSlot.centerY);
    controller.step(0.05);
    const reverseHoveredContext = createRecordingCanvasContext();
    libraryScreen.renderLibraryScreen(reverseHoveredContext, controller.getRenderSnapshot(), {
      appearance: "dark",
      palette: APPEARANCE_PALETTES.dark,
    });
    assert.ok(
      reverseHoveredContext.recordedTextCalls.some((call) => call.text === "Z\u2013A"),
      "hovering Z-A slot must update readout pill to Z-A",
    );

    const oldestSlot = slots.find((slot) => slot.mode === "oldest");
    controller.handlePointerMove(oldestSlot.centerX, oldestSlot.centerY);
    controller.step(0.05);
    const hoveredWheelState = controller.getRenderSnapshot();
    const hoveredContext = createRecordingCanvasContext();
    libraryScreen.renderLibraryScreen(hoveredContext, hoveredWheelState, {
      appearance: "light",
      palette: APPEARANCE_PALETTES.light,
    });
    assert.ok(
      hoveredContext.recordedTextCalls.some((call) => call.text === "OLDEST FIRST"),
      "hovering oldest slot must update readout pill to OLDEST FIRST",
    );
  });

  test("renderLibraryScreen adapts Seek HUD for alphabetical, liked-first, and date buckets in Dark and Light modes", () => {
    const epochSecondsForLocalNoon = (year, monthIndex, dayOfMonth) =>
      Math.floor(new Date(year, monthIndex, dayOfMonth, 12, 0, 0).getTime() / 1000);

    const albums = [
      {
        id: "alb-1",
        title: "Alpha",
        artist: "Artist A",
        isFavorite: true,
        addedAt: epochSecondsForLocalNoon(2026, 10, 25),
        tracks: [{ id: "t1", title: "One" }],
      },
      {
        id: "alb-2",
        title: "Beta",
        artist: "Artist A",
        isFavorite: false,
        addedAt: epochSecondsForLocalNoon(2026, 9, 10),
        tracks: [{ id: "t2", title: "Two" }],
      },
      {
        id: "alb-3",
        title: "Gamma",
        artist: "Artist A",
        isFavorite: false,
        addedAt: epochSecondsForLocalNoon(2025, 4, 2),
        tracks: [{ id: "t3", title: "Three" }],
      },
    ];

    const baseState = {
      currentView: { kind: "albums", title: "Albums", subtitle: "VINYL CRATE", scrollPosition: 0, targetIndex: 0 },
      currentCrateAlbums: albums,
      letterSearchActive: true,
      letterSearchHudAlpha: 1,
      rimSlabPullLevels: new Array(17).fill(0),
      idleClockSeconds: 0,
    };

    // 1. Alphabetical HUD
    const alphaContext = createRecordingCanvasContext();
    libraryScreen.renderLibraryScreen(
      alphaContext,
      {
        ...baseState,
        albumSortMode: "alphabetical",
        letterSearchIndex: 1,
        letterEntries: [
          { letter: "A", subLabel: "", shortLabel: "A", itemIndex: 0, label: "Alpha", isFavorite: false, kind: "letter" },
          { letter: "B", subLabel: "", shortLabel: "B", itemIndex: 1, label: "Beta", isFavorite: false, kind: "letter" },
          { letter: "G", subLabel: "", shortLabel: "G", itemIndex: 2, label: "Gamma", isFavorite: false, kind: "letter" },
        ],
      },
      { appearance: "dark", palette: APPEARANCE_PALETTES.dark },
    );
    assert.equal(
      alphaContext.recordedTextCalls.some((call) =>
        ["LETTER SEEK", "LIKED SEEK", "DATE SEEK"].includes(call.text),
      ),
      false,
    );
    const alphaPrev = alphaContext.recordedTextCalls.find((call) => call.text === "A");
    assert.ok(alphaPrev && Math.abs(alphaPrev.worldX - (DISPLAY_CENTER_PX - 40)) <= 1);

    // 2. Liked First HUD: favorite anchor draws heart badge inside lower lens; non-favorite omits it
    const likedActiveContext = createRecordingCanvasContext();
    libraryScreen.renderLibraryScreen(
      likedActiveContext,
      {
        ...baseState,
        albumSortMode: "liked-first",
        letterSearchIndex: 0,
        letterEntries: [
          { letter: "A", subLabel: "", shortLabel: "A", itemIndex: 0, label: "Alpha", isFavorite: true, kind: "liked-letter" },
          { letter: "B", subLabel: "", shortLabel: "B", itemIndex: 1, label: "Beta", isFavorite: false, kind: "letter" },
          { letter: "G", subLabel: "", shortLabel: "G", itemIndex: 2, label: "Gamma", isFavorite: false, kind: "letter" },
        ],
      },
      { appearance: "light", palette: APPEARANCE_PALETTES.light },
    );
    assert.equal(
      likedActiveContext.recordedTextCalls.some((call) =>
        ["LETTER SEEK", "LIKED SEEK", "DATE SEEK"].includes(call.text),
      ),
      false,
    );
    assert.ok(
      likedActiveContext.recordedTextCalls.some((call) => call.text === "A" && Math.abs(call.worldX - DISPLAY_CENTER_PX) <= 1),
    );

    // 3. Date Seek HUD: stacked NOV + 2026, flanking shortLabel outside 62px lens rim (x <= 118, x >= 242)
    const dateMonthContext = createRecordingCanvasContext();
    libraryScreen.renderLibraryScreen(
      dateMonthContext,
      {
        ...baseState,
        albumSortMode: "recent",
        letterSearchIndex: 1,
        letterEntries: [
          { letter: "NOV", subLabel: "2026", shortLabel: "NOV \u201926", itemIndex: 0, label: "Alpha", isFavorite: false, kind: "date-month" },
          { letter: "OCT", subLabel: "2026", shortLabel: "OCT \u201926", itemIndex: 1, label: "Beta", isFavorite: false, kind: "date-month" },
          { letter: "MAY", subLabel: "2025", shortLabel: "MAY \u201925", itemIndex: 2, label: "Gamma", isFavorite: false, kind: "date-month" },
        ],
      },
      { appearance: "dark", palette: APPEARANCE_PALETTES.dark },
    );
    assert.equal(
      dateMonthContext.recordedTextCalls.some((call) =>
        ["LETTER SEEK", "LIKED SEEK", "DATE SEEK"].includes(call.text),
      ),
      false,
    );
    assert.ok(dateMonthContext.recordedTextCalls.some((call) => call.text === "OCT"));
    assert.ok(dateMonthContext.recordedTextCalls.some((call) => call.text === "2026"));
    const prevDateCall = dateMonthContext.recordedTextCalls.find((call) => call.text === "NOV \u201926");
    const nextDateCall = dateMonthContext.recordedTextCalls.find((call) => call.text === "MAY \u201925");
    assert.ok(prevDateCall && prevDateCall.worldX <= DISPLAY_CENTER_PX - 62);
    assert.ok(nextDateCall && nextDateCall.worldX >= DISPLAY_CENTER_PX + 62);

    // 4. Fallback path when letterEntries is omitted in libraryState
    const fallbackDateContext = createRecordingCanvasContext();
    libraryScreen.renderLibraryScreen(
      fallbackDateContext,
      {
        ...baseState,
        albumSortMode: "recent",
        letterSearchIndex: 0,
      },
      { appearance: "dark", palette: APPEARANCE_PALETTES.dark },
    );
    assert.equal(
      fallbackDateContext.recordedTextCalls.some((call) =>
        ["LETTER SEEK", "LIKED SEEK", "DATE SEEK"].includes(call.text),
      ),
      false,
    );
    assert.ok(fallbackDateContext.recordedTextCalls.some((call) => call.text === "NOV"));
  });

  test("Artists view renders centered Sort button, row heart badge for liked artists, Artist Quick Actions, centered 5-slot radial wheel, and Seek HUD", () => {
    const controller = createSettledLibraryViewController("artists");

    // 1. Centered Sort button at (180, 325) in Dark and Light modes + hit test
    const unlikedArtistsState = controller.getRenderSnapshot();
    assert.equal(
      libraryScreen.hitTestLibraryInteractive(ARTISTS_SORT_BUTTON_X, CRATE_NOTE_BUTTON_Y, unlikedArtistsState),
      true,
    );
    const unlikedContext = createRecordingCanvasContext();
    libraryScreen.renderLibraryScreen(unlikedContext, unlikedArtistsState, {
      appearance: "dark",
      palette: APPEARANCE_PALETTES.dark,
    });
    assert.ok(
      unlikedContext.recordedArcs.some(
        (arcEntry) =>
          Math.hypot(arcEntry.worldCenterX - ARTISTS_SORT_BUTTON_X, arcEntry.worldCenterY - CRATE_NOTE_BUTTON_Y) <= 3 &&
          arcEntry.worldRadius >= 15,
      ),
      "Artists view must render floating Sort button centered at (180, 325)",
    );

    // 2. Liked Artist row renders filled accent heart beside countLabel on the right
    const unlikedFilledHearts = unlikedContext.recordedFills.filter(
      (fillEntry) => fillEntry.fillStyle === APPEARANCE_PALETTES.dark.signalHighlight && fillEntry.points.length >= 6,
    ).length;

    controller.setArtistFavorite("artist-a", true);
    const likedArtistsState = controller.getRenderSnapshot();
    const likedContext = createRecordingCanvasContext();
    libraryScreen.renderLibraryScreen(likedContext, likedArtistsState, {
      appearance: "light",
      palette: APPEARANCE_PALETTES.light,
    });
    const likedFilledHearts = likedContext.recordedFills.filter(
      (fillEntry) => fillEntry.fillStyle === APPEARANCE_PALETTES.light.signalHighlight && fillEntry.points.length >= 6,
    ).length;
    assert.ok(
      likedFilledHearts > unlikedFilledHearts,
      "Liked artist row must draw filled accent heart badge",
    );

    // 3. Artist Quick Actions renders ARTIST ACTIONS, artist name, album/track count, LIKE/LIKED, and isolates hit-test
    controller.openArtistQuickActions("artist-c");
    for (let stepIndex = 0; stepIndex < 20; stepIndex += 1) {
      controller.step(0.05);
    }
    const unlikedQuickState = controller.getRenderSnapshot();
    const unlikedQuickContext = createRecordingCanvasContext();
    libraryScreen.renderLibraryScreen(unlikedQuickContext, unlikedQuickState, {
      appearance: "dark",
      palette: APPEARANCE_PALETTES.dark,
    });
    assert.ok(unlikedQuickContext.recordedTextCalls.some((call) => call.text === "ARTIST ACTIONS"));
    assert.ok(unlikedQuickContext.recordedTextCalls.some((call) => call.text === "1 Album \u00b7 1 Track"));
    assert.ok(unlikedQuickContext.recordedTextCalls.some((call) => call.text === "LIKE"));
    assert.equal(libraryScreen.hitTestLibraryInteractive(180, 196, unlikedQuickState), true);
    assert.equal(
      libraryScreen.hitTestLibraryInteractive(ARTISTS_SORT_BUTTON_X, CRATE_NOTE_BUTTON_Y, unlikedQuickState),
      false,
    );

    controller.setArtistFavorite("artist-c", true);
    const likedQuickContext = createRecordingCanvasContext();
    libraryScreen.renderLibraryScreen(likedQuickContext, controller.getRenderSnapshot(), {
      appearance: "dark",
      palette: APPEARANCE_PALETTES.dark,
    });
    assert.ok(likedQuickContext.recordedTextCalls.some((call) => call.text === "LIKED"));
    controller.stepBack();
    for (let stepIndex = 0; stepIndex < 20; stepIndex += 1) {
      controller.step(0.05);
    }

    // 4. Long-pressing Sort button on Artists opens centered 5-slot radial wheel around (180, 325)
    controller.handlePointerDown(ARTISTS_SORT_BUTTON_X, CRATE_NOTE_BUTTON_Y);
    for (let stepIndex = 0; stepIndex < 12; stepIndex += 1) {
      controller.step(0.05);
    }
    const artistsWheelState = controller.getRenderSnapshot();
    assert.equal(artistsWheelState.sortWheelOpen, true);
    assert.equal(artistsWheelState.sortWheelScope, "artists");
    const artistsWheelContext = createRecordingCanvasContext();
    libraryScreen.renderLibraryScreen(artistsWheelContext, artistsWheelState, {
      appearance: "dark",
      palette: APPEARANCE_PALETTES.dark,
    });
    const artistSlots = computeSortWheelSlots(ARTISTS_SORT_BUTTON_X, CRATE_NOTE_BUTTON_Y);
    for (const slot of artistSlots) {
      const matchedArc = artistsWheelContext.recordedArcs.some(
        (arcEntry) =>
          Math.hypot(arcEntry.worldCenterX - slot.centerX, arcEntry.worldCenterY - slot.centerY) <=
            3 && arcEntry.worldRadius >= 14,
      );
      assert.ok(matchedArc, `Artists wheel must render medallion arc for ${slot.mode}`);
      assert.equal(
        libraryScreen.hitTestLibraryInteractive(slot.centerX, slot.centerY, artistsWheelState),
        true,
      );
    }
    controller.handlePointerUp(ARTISTS_SORT_BUTTON_X, CRATE_NOTE_BUTTON_Y);

    // 5. Seek HUD adapts title for Artists sort modes (LIKED SEEK / DATE SEEK)
    const artistHudBase = {
      currentView: { kind: "artists", title: "Artists", subtitle: "3 ARTISTS", scrollPosition: 0, targetIndex: 0 },
      letterSearchActive: true,
      letterSearchHudAlpha: 1,
      letterSearchIndex: 0,
      rimSlabPullLevels: new Array(17).fill(0),
      idleClockSeconds: 0,
    };
    const artistLikedHudContext = createRecordingCanvasContext();
    libraryScreen.renderLibraryScreen(
      artistLikedHudContext,
      {
        ...artistHudBase,
        artistSortMode: "liked-first",
        letterEntries: [
          { letter: "A", subLabel: "", shortLabel: "A", itemIndex: 0, label: "Aurora", isFavorite: true, kind: "liked-letter" },
          { letter: "B", subLabel: "", shortLabel: "B", itemIndex: 1, label: "Beta", isFavorite: false, kind: "letter" },
        ],
      },
      { appearance: "dark", palette: APPEARANCE_PALETTES.dark },
    );
    assert.equal(
      artistLikedHudContext.recordedTextCalls.some((call) =>
        ["LETTER SEEK", "LIKED SEEK", "DATE SEEK"].includes(call.text),
      ),
      false,
    );
    assert.ok(artistLikedHudContext.recordedTextCalls.some((call) => call.text === "A"));

    const artistDateHudContext = createRecordingCanvasContext();
    libraryScreen.renderLibraryScreen(
      artistDateHudContext,
      {
        ...artistHudBase,
        artistSortMode: "recent",
        letterEntries: [
          { letter: "NOV", subLabel: "2026", shortLabel: "NOV \u201926", itemIndex: 0, label: "Aurora", isFavorite: false, kind: "date-month" },
          { letter: "OCT", subLabel: "2026", shortLabel: "OCT \u201926", itemIndex: 1, label: "Beta", isFavorite: false, kind: "date-month" },
        ],
      },
      { appearance: "light", palette: APPEARANCE_PALETTES.light },
    );
    assert.equal(
      artistDateHudContext.recordedTextCalls.some((call) =>
        ["LETTER SEEK", "LIKED SEEK", "DATE SEEK"].includes(call.text),
      ),
      false,
    );
    assert.ok(artistDateHudContext.recordedTextCalls.some((call) => call.text === "NOV"));
  });

  test("Letter-Seek outer ring renders 50% compact crystal slabs (R161-R173) and R171.5 idle arcs", () => {
    const pullLevels = new Array(17).fill(0);
    pullLevels[2] = 1.0;
    pullLevels[1] = 0.24;
    pullLevels[3] = 0.24;

    const seekState = {
      currentView: { kind: "artists", title: "Artists", subtitle: "3 ARTISTS", scrollPosition: 0, targetIndex: 0 },
      letterSearchActive: true,
      letterSearchHudAlpha: 1,
      letterSearchIndex: 0,
      rimSlabPullLevels: pullLevels,
      letterEntries: [
        { letter: "A", subLabel: "", shortLabel: "A", itemIndex: 0, label: "Aurora", isFavorite: false, kind: "letter" },
        { letter: "B", subLabel: "", shortLabel: "B", itemIndex: 1, label: "Beta", isFavorite: false, kind: "letter" },
      ],
      idleClockSeconds: 0,
    };

    for (const appearance of ["dark", "light"]) {
      const recordingContext = createRecordingCanvasContext();
      libraryScreen.renderLibraryScreen(recordingContext, seekState, {
        appearance,
        palette: APPEARANCE_PALETTES[appearance],
      });
      const rimArcs = recordingContext.recordedArcs.filter((arcEntry) => arcEntry.worldRadius >= 145);
      assert.ok(rimArcs.length > 0, `Expected rim arcs in ${appearance} mode`);
      assert.ok(
        rimArcs.every((arcEntry) => arcEntry.worldRadius >= 160),
        `All compact slab rim arcs must remain >= R160 in ${appearance} mode`,
      );
      assert.ok(
        rimArcs.some((arcEntry) => Math.abs(arcEntry.worldRadius - 171.5) <= 0.5),
        `Idle rim arcs must sit on R171.5 in ${appearance} mode`,
      );
    }
  });
});
