import { describe, test } from "node:test";
import assert from "node:assert/strict";

import { createAudioEngine } from "../core/audio_engine.js";
import {
  NOW_PLAYING_CONSTANTS,
  createNowPlayingController,
} from "../core/now_playing_controller.js";
import { APPEARANCE_PALETTES } from "../core/tokens.js";
import * as nowPlayingScreen from "../screens/now_playing_screen.js";

const DISPLAY_CENTER_PX = 180;
const FULL_CIRCLE_RADIANS = Math.PI * 2;
const FLOAT_TOLERANCE = 1e-4;
const MOCK_AUDIO_DURATION_SECONDS = 180;
const MOCK_AUDIO_READY_STATE = 4;
const VINYL_CENTER_LABEL_RADIUS_PX = 50.4;
const PROGRESS_HALO_RADIUS_PX = 166;
const EDGE_PROGRESS_MIN_RADIUS_PX = 176;
const EDGE_PROGRESS_MAX_RADIUS_PX = 180;
const SEEK_CENTER_HOLD_MAX_RADIUS_PX = 90;

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
 * Create a headless HTMLAudioElement-compatible stub for controller hydration.
 *
 * @returns {object} Stub audio element.
 */
function createMockAudioStub() {
  const listeners = {};
  const stubAudioElement = {
    src: "",
    preload: "",
    currentTime: 0,
    duration: MOCK_AUDIO_DURATION_SECONDS,
    paused: true,
    volume: 1,
    readyState: MOCK_AUDIO_READY_STATE,
    addEventListener(eventName, handler) {
      if (!Array.isArray(listeners[eventName])) {
        listeners[eventName] = [];
      }
      listeners[eventName].push(handler);
    },
    load() {},
    play() {
      stubAudioElement.paused = false;
      return Promise.resolve();
    },
    pause() {
      stubAudioElement.paused = true;
    },
  };
  return stubAudioElement;
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
 * Build an initialized Now Playing controller + audio engine harness with customizable track copy.
 *
 * @param {object} [options] Optional track metadata overrides.
 * @returns {object} Harness `{ audioEngine, nowPlayingController, catalog }`.
 */
function createNowPlayingScreenHarness({
  trackTitle = "01 Dawn Over Glass",
  artistName = "Aurora Ensemble",
  albumTitle = "Alpha Horizons",
  includeLyrics = true,
} = {}) {
  const trackOne = {
    id: "t1",
    title: trackTitle,
    artist: artistName,
    albumId: "a1",
    albumTitle,
    genre: "Ambient",
    year: 2024,
    duration: "03:00",
    durationSeconds: 180,
    isFavorite: false,
    trackNumber: "01",
    audioUrl: "/api/audio/t1",
    format: "FLAC 24-BIT",
    sampleRate: "96.0 KHZ",
    bitrate: "2,822 KBPS",
    fileSize: "42.3 MB",
  };
  const trackTwo = {
    id: "t2",
    title: "02 Evening Prism Echoes",
    artist: artistName,
    albumId: "a1",
    albumTitle,
    genre: "Ambient",
    year: 2024,
    duration: "03:20",
    durationSeconds: 200,
    isFavorite: true,
    trackNumber: "02",
    audioUrl: "/api/audio/t2",
  };
  const albumAlpha = {
    id: "a1",
    title: albumTitle,
    artist: artistName,
    genre: "Ambient",
    year: 2024,
    motif: "prism",
    tracks: [{ ...trackOne }, { ...trackTwo }],
  };
  const catalog = {
    albums: [albumAlpha],
    allTracks: [trackOne, trackTwo],
    playlists: [{ id: "p1", name: "Late Night Listening Mix", trackIds: ["t1"] }],
    lyricsCatalog: includeLyrics
      ? {
          t1: {
            timed: [
              { timeSeconds: 0, text: "First light across the turntable glass" },
              { timeSeconds: 15, text: "Spinning steadily through quiet rooms" },
            ],
          },
        }
      : {},
  };

  const audioEngine = createAudioEngine({
    createAudioElement: () => createMockAudioStub(),
  });
  audioEngine.playTrackFromSource({ trackId: "t1", queueSource: "album", albumId: "a1" }, catalog);
  audioEngine.seekTo(60);

  const nowPlayingController = createNowPlayingController({
    onTogglePlayPause: () => audioEngine.togglePlayPause(),
    onSeek: (targetSeconds) => audioEngine.seekTo(targetSeconds),
    onExit() {},
  });
  nowPlayingController.hydrateCatalog(catalog);
  nowPlayingController.enterFromHome({
    returnScreen: "home",
    audioState: audioEngine.getState(),
  });
  for (let stepIndex = 0; stepIndex < 30; stepIndex += 1) {
    nowPlayingController.step(0.05);
  }
  nowPlayingController.syncAudioState(audioEngine.getState());

  return { audioEngine, nowPlayingController, catalog };
}

/**
 * Filter stroked arcs centered near display center `(180, 180)` inside `[minRadius, maxRadius]`.
 *
 * @param {object} drawingContext Mock recording Canvas context.
 * @param {number} minRadius Minimum radius in pixels.
 * @param {number} maxRadius Maximum radius in pixels.
 * @returns {Array<object>} Flattened stroke/arc records.
 */
function findCenteredStrokedArcs(drawingContext, minRadius, maxRadius) {
  const matched = [];
  for (const strokeEntry of drawingContext.recordedStrokes) {
    for (const arcEntry of strokeEntry.arcs) {
      const distanceFromCenter = Math.hypot(
        arcEntry.worldCenterX - DISPLAY_CENTER_PX,
        arcEntry.worldCenterY - DISPLAY_CENTER_PX,
      );
      if (
        distanceFromCenter <= 2.5 &&
        arcEntry.worldRadius >= minRadius &&
        arcEntry.worldRadius <= maxRadius
      ) {
        matched.push({ strokeEntry, arcEntry });
      }
    }
  }
  return matched;
}

describe("now_playing_screen.js full-bleed vinyl, spindle ring, and chrome removals", () => {
  test("full-bleed optical vinyl removes top-left/bottom-right white specular wedges, inner 50.4px label ring, vinyl grooves, and inner rim stroke", () => {
    const { nowPlayingController, catalog } = createNowPlayingScreenHarness();
    const state = {
      ...nowPlayingController.getRenderSnapshot(),
      openTransitionProgress: 1,
      openChromeHoldAlpha: 0,
    };

    const drawingContext = createRecordingCanvasContext();
    nowPlayingScreen.renderNowPlayingScreen(drawingContext, state, {
      appearance: "dark",
      palette: APPEARANCE_PALETTES.dark,
      catalogAlbums: catalog.albums,
    });

    // No top-left or bottom-right white specular light wedge sector fills
    const whiteWedgeFills = drawingContext.recordedFills.filter((fillEntry) => {
      const isWhiteAlpha = /rgba\(\s*255\s*,\s*255\s*,\s*255\s*,/i.test(
        String(fillEntry.fillStyle || ""),
      );
      const hasSectorArc = fillEntry.arcs.some((arcEntry) => {
        const angularSpan = Math.abs(arcEntry.endAngle - arcEntry.startAngle);
        return angularSpan > 0.05 && angularSpan < Math.PI && arcEntry.worldRadius >= 120;
      });
      return isWhiteAlpha && hasSectorArc;
    });
    assert.equal(
      whiteWedgeFills.length,
      0,
      "full-bleed vinyl must not draw top-left or bottom-right white specular light wedges",
    );

    // No inner r ≈ 50.4px white center label circle or label fill
    const centerLabelStrokes = findCenteredStrokedArcs(
      drawingContext,
      VINYL_CENTER_LABEL_RADIUS_PX - 6,
      VINYL_CENTER_LABEL_RADIUS_PX + 6,
    );
    assert.equal(
      centerLabelStrokes.length,
      0,
      "full-bleed vinyl must pass { showCenterLabel: false } and omit inner ~50.4px label ring",
    );

    const centerLabelFills = drawingContext.recordedFills.filter((fillEntry) =>
      fillEntry.arcs.some(
        (arcEntry) =>
          Math.abs(arcEntry.worldRadius - VINYL_CENTER_LABEL_RADIUS_PX) <= 6 &&
          Math.hypot(
            arcEntry.worldCenterX - DISPLAY_CENTER_PX,
            arcEntry.worldCenterY - DISPLAY_CENTER_PX,
          ) <= 2.5,
      ),
    );
    assert.equal(
      centerLabelFills.length,
      0,
      "full-bleed vinyl must omit inner center label fill plate",
    );

    // No concentric full-circle vinyl groove strokes in [75, 165] opposing pure full-bleed art
    const grooveStrokes = findCenteredStrokedArcs(drawingContext, 75, 165).filter(
      ({ strokeEntry, arcEntry }) =>
        Math.abs(arcEntry.endAngle - arcEntry.startAngle) >= FULL_CIRCLE_RADIANS - 0.1 &&
        strokeEntry.lineWidth <= 3,
    );
    assert.equal(
      grooveStrokes.length,
      0,
      "full-bleed now playing cover must omit concentric vinyl groove circles",
    );

    // No inner 177.5px rim stroke opposing full-bleed art
    const innerRimStrokes = findCenteredStrokedArcs(drawingContext, 177, 178).filter(
      ({ strokeEntry, arcEntry }) =>
        Math.abs(arcEntry.worldRadius - 177.5) <= 0.3 &&
        Math.abs(strokeEntry.lineWidth - 1) <= 0.1,
    );
    assert.equal(
      innerRimStrokes.length,
      0,
      "full-bleed now playing cover must omit inner rim border stroke",
    );
  });

  test("resting full-bleed playback draws zero center spindle rings/pucks and draws a thin outer-edge progress bar in [176, 180]", () => {
    const { nowPlayingController, catalog } = createNowPlayingScreenHarness();
    const restingState = {
      ...nowPlayingController.getRenderSnapshot(),
      openTransitionProgress: 1,
      trackElapsedSeconds: 60,
      trackDurationSeconds: 180,
      seekExpandProgress: 0,
      seekHoldProgress: 0,
      volumeHudAlpha: 0,
    };

    const drawingContext = createRecordingCanvasContext();
    nowPlayingScreen.renderNowPlayingScreen(drawingContext, restingState, {
      appearance: "dark",
      palette: APPEARANCE_PALETTES.dark,
      catalogAlbums: catalog.albums,
    });

    const centerRegionStrokes = findCenteredStrokedArcs(
      drawingContext,
      1,
      SEEK_CENTER_HOLD_MAX_RADIUS_PX,
    );
    assert.equal(
      centerRegionStrokes.length,
      0,
      "resting full-bleed view must not stroke any center spindle ring or circle (r <= 90px)",
    );

    const centerRegionFills = drawingContext.recordedFills.filter((fillEntry) =>
      fillEntry.arcs.some(
        (arcEntry) =>
          arcEntry.worldRadius <= 35 &&
          Math.hypot(
            arcEntry.worldCenterX - DISPLAY_CENTER_PX,
            arcEntry.worldCenterY - DISPLAY_CENTER_PX,
          ) <= 2.5,
      ),
    );
    assert.equal(
      centerRegionFills.length,
      0,
      "resting full-bleed view must not draw any center spindle puck or dot fills",
    );

    const outerEdgeStrokes = findCenteredStrokedArcs(
      drawingContext,
      EDGE_PROGRESS_MIN_RADIUS_PX,
      EDGE_PROGRESS_MAX_RADIUS_PX,
    );
    assert.ok(
      outerEdgeStrokes.length >= 2,
      "outer-edge progress bar should stroke background track and active progress arc in [176, 180]",
    );

    for (const { strokeEntry, arcEntry } of outerEdgeStrokes) {
      assert.ok(
        arcEntry.worldRadius >= EDGE_PROGRESS_MIN_RADIUS_PX &&
          arcEntry.worldRadius <= EDGE_PROGRESS_MAX_RADIUS_PX,
        `outer-edge progress ring must lie in [176, 180], got r=${arcEntry.worldRadius}`,
      );
      assert.ok(
        strokeEntry.lineWidth > 0 && strokeEntry.lineWidth <= 2.0,
        `outer-edge progress ring must be thin (<=2.0px), got lineWidth=${strokeEntry.lineWidth}`,
      );
    }

    const expectedProgressSweep = (60 / 180) * FULL_CIRCLE_RADIANS;
    const activeProgressArc = outerEdgeStrokes.find(({ arcEntry }) => {
      const sweep = Math.abs(arcEntry.endAngle - arcEntry.startAngle);
      return Math.abs(sweep - expectedProgressSweep) <= 0.15;
    });
    assert.ok(
      activeProgressArc,
      "outer-edge playback progress arc sweep must match (elapsed / duration) * 2*PI",
    );
  });

  test("Seek Mode preserves center hold-charge ring, expanded seek dial, tick marks, scrub arc, and SEEK readout", () => {
    const { nowPlayingController, catalog } = createNowPlayingScreenHarness();

    const holdingState = {
      ...nowPlayingController.getRenderSnapshot(),
      openTransitionProgress: 1,
      trackElapsedSeconds: 60,
      trackDurationSeconds: 180,
      seekHoldProgress: 0.6,
      seekExpandProgress: 0,
      volumeHudAlpha: 0,
    };
    const holdingContext = createRecordingCanvasContext();
    nowPlayingScreen.renderNowPlayingScreen(holdingContext, holdingState, {
      appearance: "dark",
      palette: APPEARANCE_PALETTES.dark,
      catalogAlbums: catalog.albums,
    });
    const holdChargeArcs = findCenteredStrokedArcs(holdingContext, 10, 40);
    assert.ok(
      holdChargeArcs.length >= 2,
      "hold-to-seek charge state must preserve center charge ring feedback",
    );

    const expandedSeekState = {
      ...nowPlayingController.getRenderSnapshot(),
      openTransitionProgress: 1,
      trackElapsedSeconds: 60,
      trackDurationSeconds: 180,
      isSeeking: true,
      seekHoldProgress: 1,
      seekExpandProgress: 1,
      volumeHudAlpha: 0,
    };
    const seekContext = createRecordingCanvasContext();
    nowPlayingScreen.renderNowPlayingScreen(seekContext, expandedSeekState, {
      appearance: "dark",
      palette: APPEARANCE_PALETTES.dark,
      catalogAlbums: catalog.albums,
    });

    const expandedDialArcs = findCenteredStrokedArcs(
      seekContext,
      NOW_PLAYING_CONSTANTS.SPINDLE_SEEK_RADIUS_PX - 10,
      NOW_PLAYING_CONSTANTS.SPINDLE_SEEK_RADIUS_PX + 10,
    );
    assert.ok(
      expandedDialArcs.length >= 2,
      "active Seek Mode must preserve expanded center seek dial and scrub ring",
    );

    const radialTickStrokes = seekContext.recordedStrokes.filter(
      (strokeEntry) => strokeEntry.points.length === 2 && Math.abs(strokeEntry.lineWidth - 1) < 0.1,
    );
    assert.ok(radialTickStrokes.length >= 12, "active Seek Mode must preserve center tick marks");

    const seekLabels = seekContext.recordedTextCalls.map((call) => call.text);
    assert.ok(seekLabels.includes("SEEK"), "active Seek Mode must render SEEK label");
    assert.ok(seekLabels.includes("01:00"), "active Seek Mode must render current elapsed time");
  });

  test("lyrics overlay omits vinyl inner white label circle, outer/spindle progress circle decorations, and DOUBLE-TAP TO RESUME text when paused", () => {
    for (const includeLyrics of [true, false]) {
      const { audioEngine, nowPlayingController, catalog } = createNowPlayingScreenHarness({
        includeLyrics,
      });
      audioEngine.pause();
      nowPlayingController.syncAudioState(audioEngine.getState());
      nowPlayingController.toggleLyrics();
      for (let stepIndex = 0; stepIndex < 30; stepIndex += 1) {
        nowPlayingController.step(0.05);
      }

      for (const lyricsProgressValue of [1, 0.45]) {
        const lyricsState = {
          ...nowPlayingController.getRenderSnapshot(),
          lyricsOpen: true,
          lyricsProgress: lyricsProgressValue,
          isPlaying: false,
        };
        const drawingContext = createRecordingCanvasContext();
        nowPlayingScreen.renderNowPlayingScreen(drawingContext, lyricsState, {
          appearance: "dark",
          palette: APPEARANCE_PALETTES.dark,
          catalogAlbums: catalog.albums,
        });

        const resumeTexts = drawingContext.recordedTextCalls.filter((call) =>
          /DOUBLE-TAP/i.test(call.text),
        );
        assert.equal(
          resumeTexts.length,
          0,
          `lyrics overlay must not draw DOUBLE-TAP TO RESUME when paused (includeLyrics=${includeLyrics})`,
        );

        const outerProgressCircles = findCenteredStrokedArcs(
          drawingContext,
          PROGRESS_HALO_RADIUS_PX - 6,
          PROGRESS_HALO_RADIUS_PX + 6,
        );
        assert.equal(
          outerProgressCircles.length,
          0,
          "lyrics overlay must not draw outer progress halo circle",
        );

        const spindleProgressCircles = findCenteredStrokedArcs(drawingContext, 8, 35);
        assert.equal(
          spindleProgressCircles.length,
          0,
          "lyrics overlay must not draw spindle progress circle decorations",
        );

        const innerLabelRings = findCenteredStrokedArcs(
          drawingContext,
          VINYL_CENTER_LABEL_RADIUS_PX - 6,
          VINYL_CENTER_LABEL_RADIUS_PX + 6,
        );
        assert.equal(
          innerLabelRings.length,
          0,
          "lyrics overlay background vinyl must omit inner white label ring",
        );
      }
    }
  });

  test("quick actions overlay omits top pull-down handle bar, bottom SWIPE QUEUE / HOLD VINYL SEEK text, and bottom upward chevron", () => {
    const { nowPlayingController, catalog } = createNowPlayingScreenHarness();
    nowPlayingController.handlePointerDown(DISPLAY_CENTER_PX, DISPLAY_CENTER_PX);
    nowPlayingController.handlePointerUp(DISPLAY_CENTER_PX, DISPLAY_CENTER_PX);
    nowPlayingController.step(NOW_PLAYING_CONSTANTS.DOUBLE_TAP_WINDOW_SECONDS + 0.05);
    for (let stepIndex = 0; stepIndex < 25; stepIndex += 1) {
      nowPlayingController.step(0.05);
    }

    const quickState = {
      ...nowPlayingController.getRenderSnapshot(),
      quickActionsOpen: true,
      quickActionsProgress: 1,
      openChromeHoldAlpha: 1,
    };
    const drawingContext = createRecordingCanvasContext();
    nowPlayingScreen.renderNowPlayingScreen(drawingContext, quickState, {
      appearance: "dark",
      palette: APPEARANCE_PALETTES.dark,
      catalogAlbums: catalog.albums,
    });

    const topHandleBars = drawingContext.recordedRects.filter(
      (rectEntry) =>
        rectEntry.kind === "fillRect" &&
        rectEntry.worldLeftX >= 150 &&
        rectEntry.worldLeftX <= 176 &&
        rectEntry.worldTopY >= 10 &&
        rectEntry.worldTopY <= 44 &&
        rectEntry.width >= 18 &&
        rectEntry.width <= 46 &&
        rectEntry.height <= 6,
    );
    assert.equal(topHandleBars.length, 0, "Quick Actions must omit top pull-down handle bar");

    const swipeQueueTexts = drawingContext.recordedTextCalls.filter((call) =>
      /SWIPE|HOLD VINYL SEEK/i.test(call.text),
    );
    assert.equal(
      swipeQueueTexts.length,
      0,
      "Quick Actions must omit bottom SWIPE ↑ QUEUE · HOLD VINYL SEEK hint text",
    );

    const bottomChevrons = drawingContext.recordedStrokes.filter(
      (strokeEntry) =>
        strokeEntry.points.length >= 3 &&
        strokeEntry.points.every(
          (point) =>
            point.worldX >= 165 &&
            point.worldX <= 195 &&
            point.worldY >= 316 &&
            point.worldY <= 342,
        ),
    );
    assert.equal(bottomChevrons.length, 0, "Quick Actions must omit bottom upward chevron");
  });

  test("track details sheet omits top pull-down handle bar and bottom SWIPE DOWN OR LEFT EDGE TO CLOSE text", () => {
    const { nowPlayingController, catalog } = createNowPlayingScreenHarness();
    nowPlayingController.toggleTrackDetails();
    for (let stepIndex = 0; stepIndex < 25; stepIndex += 1) {
      nowPlayingController.step(0.05);
    }

    const detailsState = {
      ...nowPlayingController.getRenderSnapshot(),
      detailsSheetOpen: true,
      detailsSheetProgress: 1,
    };
    const drawingContext = createRecordingCanvasContext();
    nowPlayingScreen.renderNowPlayingScreen(drawingContext, detailsState, {
      appearance: "dark",
      palette: APPEARANCE_PALETTES.dark,
      catalogAlbums: catalog.albums,
    });

    const topHandleBars = drawingContext.recordedRects.filter(
      (rectEntry) =>
        rectEntry.kind === "fillRect" &&
        rectEntry.worldLeftX >= 150 &&
        rectEntry.worldLeftX <= 176 &&
        rectEntry.worldTopY >= 12 &&
        rectEntry.worldTopY <= 48 &&
        rectEntry.width >= 18 &&
        rectEntry.width <= 46 &&
        rectEntry.height <= 6,
    );
    assert.equal(topHandleBars.length, 0, "Track Details must omit top pull-down handle bar");

    const closeHintTexts = drawingContext.recordedTextCalls.filter((call) =>
      /SWIPE DOWN|LEFT EDGE TO CLOSE/i.test(call.text),
    );
    assert.equal(
      closeHintTexts.length,
      0,
      "Track Details must omit bottom SWIPE DOWN OR LEFT EDGE TO CLOSE text",
    );
  });
});

describe("now_playing_screen.js marquee scrolling and entrance transitions", () => {
  /**
   * Assert that an overflowing text string renders untruncated inside a bounded clip
   * and advances horizontally across animation timestamps.
   *
   * @param {(timeSeconds: number) => object} renderAtTime Render callback returning mock context.
   * @param {string} targetText Expected untruncated text string.
   * @param {number} maxExpectedClipWidth Maximum expected clip rectangle width in pixels.
   */
  function assertTextMarqueesInOverlay(renderAtTime, targetText, maxExpectedClipWidth) {
    const startContext = renderAtTime(0);
    const startCalls = startContext.recordedTextCalls.filter((call) => call.text === targetText);
    assert.ok(
      startCalls.length >= 1,
      `expected untruncated marquee text "${targetText}" instead of truncation`,
    );

    const targetWorldY = startCalls[0].worldY;
    const boundedClips = startContext.recordedClips.filter((clipEntry) =>
      clipEntry.rects.some(
        (rectEntry) =>
          rectEntry.width <= maxExpectedClipWidth &&
          targetWorldY >= rectEntry.worldTopY - 6 &&
          targetWorldY <= rectEntry.worldTopY + rectEntry.height + 6,
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
    assert.equal(shifted, true, `expected "${targetText}" X offset to advance across animationTimeSeconds`);
  }

  test("overflowing title, artist, queue, details, and lyrics text advances X across animationTimeSeconds via marquee", () => {
    const longTitle = "Long Orchestral Overture In E Flat Major Opus Seventy Seven";
    const longArtist = "Grand Symphony Philharmonic Orchestra Of Vienna";
    const longAlbum = "Complete Nocturnes Collector Edition Anthology";
    const { nowPlayingController, catalog } = createNowPlayingScreenHarness({
      trackTitle: longTitle,
      artistName: longArtist,
      albumTitle: longAlbum,
    });

    // 1. Quick Actions title & artist · album
    nowPlayingController.handlePointerDown(DISPLAY_CENTER_PX, DISPLAY_CENTER_PX);
    nowPlayingController.handlePointerUp(DISPLAY_CENTER_PX, DISPLAY_CENTER_PX);
    nowPlayingController.step(NOW_PLAYING_CONSTANTS.DOUBLE_TAP_WINDOW_SECONDS + 0.05);
    for (let stepIndex = 0; stepIndex < 25; stepIndex += 1) {
      nowPlayingController.step(0.05);
    }
    const quickSnapshot = nowPlayingController.getRenderSnapshot();
    const renderQuickAtTime = (timeSeconds) => {
      const drawingContext = createRecordingCanvasContext();
      nowPlayingScreen.renderNowPlayingScreen(
        drawingContext,
        {
          ...quickSnapshot,
          quickActionsOpen: true,
          quickActionsProgress: 1,
          idleClockSeconds: timeSeconds,
        },
        {
          appearance: "dark",
          palette: APPEARANCE_PALETTES.dark,
          catalogAlbums: catalog.albums,
          animationTimeSeconds: timeSeconds,
        },
      );
      return drawingContext;
    };
    assertTextMarqueesInOverlay(renderQuickAtTime, longTitle, 240);
    assertTextMarqueesInOverlay(renderQuickAtTime, `${longArtist} \u00b7 ${longAlbum}`, 260);

    // 2. Track Details title & artist
    const detailsSnapshot = nowPlayingController.getRenderSnapshot();
    const renderDetailsAtTime = (timeSeconds) => {
      const drawingContext = createRecordingCanvasContext();
      nowPlayingScreen.renderNowPlayingScreen(
        drawingContext,
        {
          ...detailsSnapshot,
          quickActionsOpen: false,
          quickActionsProgress: 0,
          detailsSheetOpen: true,
          detailsSheetProgress: 1,
          idleClockSeconds: timeSeconds,
        },
        {
          appearance: "dark",
          palette: APPEARANCE_PALETTES.dark,
          catalogAlbums: catalog.albums,
          animationTimeSeconds: timeSeconds,
        },
      );
      return drawingContext;
    };
    assertTextMarqueesInOverlay(renderDetailsAtTime, longTitle, 190);
    assertTextMarqueesInOverlay(renderDetailsAtTime, longArtist, 190);

    // 3. Context Queue Sheet title & track row title/artist
    const queueSnapshot = nowPlayingController.getRenderSnapshot();
    const renderQueueAtTime = (timeSeconds) => {
      const drawingContext = createRecordingCanvasContext();
      nowPlayingScreen.renderNowPlayingScreen(
        drawingContext,
        {
          ...queueSnapshot,
          quickActionsOpen: false,
          quickActionsProgress: 0,
          detailsSheetOpen: false,
          detailsSheetProgress: 0,
          queueSheetOpen: true,
          queueSheetProgress: 1,
          queueContext: {
            sourceId: "album",
            badge: "ALBUM",
            title: longAlbum,
            tracks: queueSnapshot.queueTracks,
          },
          idleClockSeconds: timeSeconds,
        },
        {
          appearance: "dark",
          palette: APPEARANCE_PALETTES.dark,
          catalogAlbums: catalog.albums,
          animationTimeSeconds: timeSeconds,
        },
      );
      return drawingContext;
    };
    assertTextMarqueesInOverlay(renderQueueAtTime, longAlbum, 240);
    assertTextMarqueesInOverlay(renderQueueAtTime, longTitle, 240);
    assertTextMarqueesInOverlay(renderQueueAtTime, longArtist, 240);

    // 4. Lyrics overlay header track title
    const lyricsSnapshot = nowPlayingController.getRenderSnapshot();
    const renderLyricsAtTime = (timeSeconds) => {
      const drawingContext = createRecordingCanvasContext();
      nowPlayingScreen.renderNowPlayingScreen(
        drawingContext,
        {
          ...lyricsSnapshot,
          quickActionsOpen: false,
          quickActionsProgress: 0,
          lyricsOpen: true,
          lyricsProgress: 1,
          idleClockSeconds: timeSeconds,
        },
        {
          appearance: "dark",
          palette: APPEARANCE_PALETTES.dark,
          catalogAlbums: catalog.albums,
          animationTimeSeconds: timeSeconds,
        },
      );
      return drawingContext;
    };
    assertTextMarqueesInOverlay(renderLyricsAtTime, longTitle, 210);
  });

  test("album-crate take-out-the-disc transition translates album sleeve leftward while vinyl disc slips out rightward, centers smoothly, and rotates +20 deg clockwise", () => {
    const CRATE_HANDOFF_ANGLE_RADIANS = 0.75;
    const TWENTY_DEG_RADIANS = (20 * Math.PI) / 180;
    const { audioEngine, nowPlayingController, catalog } = createNowPlayingScreenHarness();

    function inspectCrateTransitionAt(progressRatio) {
      nowPlayingController.enterFromLibrary({
        returnScreen: "library",
        launchOrigin: "album-crate",
        audioState: audioEngine.getState(),
        vinylRotationRadians: CRATE_HANDOFF_ANGLE_RADIANS,
      });
      if (progressRatio > 0) {
        nowPlayingController.step(progressRatio * NOW_PLAYING_CONSTANTS.OPEN_DURATION_SECONDS);
      }
      const snapshot = {
        ...nowPlayingController.getRenderSnapshot(),
        enterTransitionProgress: progressRatio,
      };
      assert.equal(snapshot.launchOrigin, "album-crate");

      const drawingContext = createRecordingCanvasContext();
      nowPlayingScreen.renderNowPlayingScreen(drawingContext, snapshot, {
        appearance: "dark",
        palette: APPEARANCE_PALETTES.dark,
        catalogAlbums: catalog.albums,
      });

      const sleeveRects = drawingContext.recordedRects.filter(
        (rectEntry) =>
          rectEntry.kind === "strokeRect" &&
          rectEntry.width >= 75 &&
          rectEntry.width <= 150 &&
          rectEntry.height >= 75 &&
          rectEntry.height <= 150,
      );
      const discClipArcs = drawingContext.recordedClips
        .flatMap((clipEntry) => clipEntry.arcs)
        .filter((arcEntry) => arcEntry.worldRadius >= 45);

      return {
        snapshot,
        drawingContext,
        sleeveRect: sleeveRects[0] || null,
        discClipArc: discClipArcs[discClipArcs.length - 1] || null,
        discRotationRadians: drawingContext.recordedRotations[0],
      };
    }

    const startFrame = inspectCrateTransitionAt(0.0);
    const earlyFrame = inspectCrateTransitionAt(0.15);
    const midSlipFrame = inspectCrateTransitionAt(0.35);
    const midpointFrame = inspectCrateTransitionAt(0.5);
    const lateFrame = inspectCrateTransitionAt(0.7);
    const settledFrame = inspectCrateTransitionAt(1.0);

    assert.ok(earlyFrame.sleeveRect, "early album-crate transition should render album sleeve");
    assert.ok(midSlipFrame.sleeveRect, "mid album-crate transition (0.35) should render album sleeve");

    const earlySleeveCenterX = earlyFrame.sleeveRect.worldLeftX + earlyFrame.sleeveRect.width / 2;
    const midSleeveCenterX = midSlipFrame.sleeveRect.worldLeftX + midSlipFrame.sleeveRect.width / 2;
    assert.ok(
      midSleeveCenterX < 168 && midSleeveCenterX < earlySleeveCenterX,
      "album sleeve must translate leftward offscreen during album-crate take-out-the-disc transition",
    );

    assert.ok(midSlipFrame.discClipArc, "mid transition should draw vinyl disc");
    assert.ok(
      midSlipFrame.discClipArc.worldCenterX > midSleeveCenterX + 30 &&
        midSlipFrame.discClipArc.worldCenterX > DISPLAY_CENTER_PX,
      "vinyl disc must slip out rightward away from translating album sleeve",
    );

    assert.ok(
      earlyFrame.discClipArc.worldRadius < midSlipFrame.discClipArc.worldRadius &&
        midSlipFrame.discClipArc.worldRadius < lateFrame.discClipArc.worldRadius &&
        lateFrame.discClipArc.worldRadius < settledFrame.discClipArc.worldRadius,
      "vinyl disc radius must grow smoothly toward full-bleed 180px radius",
    );
    assert.ok(Math.abs(startFrame.discClipArc.worldCenterY - 152) <= 1);
    assert.ok(Math.abs(settledFrame.discClipArc.worldRadius - DISPLAY_CENTER_PX) <= 1);
    assert.ok(Math.abs(settledFrame.discClipArc.worldCenterX - DISPLAY_CENTER_PX) <= 1);
    assert.ok(Math.abs(settledFrame.discClipArc.worldCenterY - DISPLAY_CENTER_PX) <= 1);

    assert.ok(
      Math.abs(startFrame.discRotationRadians - CRATE_HANDOFF_ANGLE_RADIANS) < FLOAT_TOLERANCE,
    );
    assert.ok(
      Math.abs(midpointFrame.discRotationRadians - (CRATE_HANDOFF_ANGLE_RADIANS + TWENTY_DEG_RADIANS / 2)) <
        0.002,
      "album-crate vinyl must rotate +10 deg clockwise at midpoint without counter-clockwise freeze",
    );
    assert.ok(
      Math.abs(settledFrame.discRotationRadians - (CRATE_HANDOFF_ANGLE_RADIANS + TWENTY_DEG_RADIANS)) <
        0.002,
      "album-crate vinyl must rotate +20 deg clockwise at completion",
    );
  });

  test("home launch transition rotates vinyl +20 deg clockwise linearly across entrance and keeps center label hidden", () => {
    const HOME_HANDOFF_ANGLE_RADIANS = 0.95;
    const TWENTY_DEG_RADIANS = (20 * Math.PI) / 180;
    const { audioEngine, nowPlayingController, catalog } = createNowPlayingScreenHarness();

    nowPlayingController.enterFromHome({
      returnScreen: "home",
      audioState: audioEngine.getState(),
      vinylRotationRadians: HOME_HANDOFF_ANGLE_RADIANS,
    });

    function renderHomeLaunchFrame(snapshot) {
      const drawingContext = createRecordingCanvasContext();
      nowPlayingScreen.renderNowPlayingScreen(drawingContext, snapshot, {
        appearance: "dark",
        palette: APPEARANCE_PALETTES.dark,
        catalogAlbums: catalog.albums,
      });
      return drawingContext;
    }

    const startContext = renderHomeLaunchFrame(nowPlayingController.getRenderSnapshot());
    assert.ok(startContext.recordedRotations.length >= 1);
    assert.ok(
      Math.abs(startContext.recordedRotations[0] - HOME_HANDOFF_ANGLE_RADIANS) < FLOAT_TOLERANCE,
    );

    nowPlayingController.step(0.5 * NOW_PLAYING_CONSTANTS.OPEN_DURATION_SECONDS);
    const midSnapshot = nowPlayingController.getRenderSnapshot();
    assert.equal(midSnapshot.launchOrigin, "home");
    assert.equal(midSnapshot.vinylNudgeRadians, 0);
    const midContext = renderHomeLaunchFrame(midSnapshot);
    assert.ok(midContext.recordedRotations.length >= 1);
    assert.ok(
      Math.abs(midContext.recordedRotations[0] - (HOME_HANDOFF_ANGLE_RADIANS + TWENTY_DEG_RADIANS / 2)) <
        0.002,
      "Home -> Now Playing entrance must advance +10 deg clockwise at midpoint",
    );

    nowPlayingController.step(0.5 * NOW_PLAYING_CONSTANTS.OPEN_DURATION_SECONDS);
    const settledSnapshot = nowPlayingController.getRenderSnapshot();
    const settledContext = renderHomeLaunchFrame(settledSnapshot);
    assert.ok(settledContext.recordedRotations.length >= 1);
    assert.ok(
      Math.abs(settledContext.recordedRotations[0] - (HOME_HANDOFF_ANGLE_RADIANS + TWENTY_DEG_RADIANS)) <
        0.002,
      "Home -> Now Playing entrance must advance +20 deg clockwise at completion",
    );

    const transitionDiscClipArc = midContext.recordedClips
      .flatMap((clipEntry) => clipEntry.arcs)
      .find((arcEntry) => arcEntry.worldRadius >= 45);
    assert.ok(transitionDiscClipArc, "entrance transition should draw vinyl disc clip");
    const scaledLabelRadius = transitionDiscClipArc.worldRadius * 0.28;
    const innerLabelRings = findCenteredStrokedArcs(
      midContext,
      scaledLabelRadius - 3,
      scaledLabelRadius + 3,
    ).filter(({ strokeEntry }) => /#F5F6F8/i.test(String(strokeEntry.strokeStyle || "")));
    assert.equal(innerLabelRings.length, 0, "entrance vinyl must keep { showCenterLabel: false }");
  });
});
