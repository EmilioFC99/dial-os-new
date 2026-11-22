import { describe, test } from "node:test";
import assert from "node:assert/strict";

import { createAudioEngine } from "../core/audio_engine.js";
import {
  LIBRARY_CONSTANTS,
  LIBRARY_HUB_SECTIONS,
  buildAlphabeticalLetterIndex,
  compareAlphabetically,
  computeLibraryCurvedRowLayout,
  computeLibraryHubTileRect,
  computeWindowedCrateBars,
  createLibraryController,
  resolveLibraryRimSliceIndex,
  shortestSliceDistance,
  sortAlphabeticallyBy,
} from "../core/library_controller.js";

const DISPLAY_CENTER_PX = 180;
const STEP_DELTA_SECONDS = 0.05;
const SETTLE_STEP_COUNT = 25;
const FLOAT_TOLERANCE = 1e-6;

const RIM_DRAG_RADIUS_PX = 150;
const RIM_START_ANGLE_RADIANS = -Math.PI / 2;
const RIM_ARC_STEP_RADIANS = Math.PI / 18;
const RIM_STEP_EPSILON_RADIANS = 0.02;

const CRATE_SLEEVE_TAP_X = 165;
const CRATE_PEEK_DISC_TAP_X = 248;
const CRATE_CENTER_Y = 152;
const CRATE_NOTE_BUTTON_X = 180;
const CRATE_NOTE_BUTTON_Y = 325;
const CRATE_NOTE_BUTTON_RADIUS_PX = 22;

const EMPTY_BACK_BUTTON_X = 180;
const EMPTY_BACK_BUTTON_Y = 260;
const DELETE_BUTTON_X = 180;
const DELETE_BUTTON_Y = 250;
const DELETE_OUTSIDE_Y = 80;

const BACK_SWIPE_START_X = 24;
const BACK_SWIPE_Y = 180;
const BACK_SWIPE_COMMIT_END_X = 90;

const MOCK_AUDIO_DURATION_SECONDS = 180;
const MOCK_AUDIO_READY_STATE = 4;

/**
 * Create an HTMLAudioElement-compatible stub for headless Node audio engine tests.
 *
 * @returns {object} Stub audio element with call counters and event emission helpers.
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
    playCalls: 0,
    pauseCalls: 0,
    loadCalls: 0,
    listeners,
    addEventListener(eventName, handler) {
      if (!Array.isArray(listeners[eventName])) {
        listeners[eventName] = [];
      }
      listeners[eventName].push(handler);
    },
    load() {
      stubAudioElement.loadCalls += 1;
    },
    play() {
      stubAudioElement.paused = false;
      stubAudioElement.playCalls += 1;
      return Promise.resolve();
    },
    pause() {
      stubAudioElement.paused = true;
      stubAudioElement.pauseCalls += 1;
    },
    emit(eventName) {
      const eventHandlers = listeners[eventName] || [];
      for (const handler of eventHandlers) {
        handler({ type: eventName });
      }
    },
  };
  return stubAudioElement;
}

/**
 * Build a multi-album catalog fixture for audio engine playback tests.
 *
 * @returns {object} Multi-album catalog payload.
 */
function buildAudioTestCatalog() {
  const trackOne = {
    id: "t1",
    title: "01 Dawn",
    artist: "Aurora Band",
    albumId: "a1",
    albumTitle: "Alpha Waves",
    genre: "Ambient",
    duration: "03:00",
    durationSeconds: 180,
    isFavorite: false,
    trackNumber: "01",
    audioUrl: "/api/audio/t1",
  };
  const trackTwo = {
    id: "t2",
    title: "02 Dusk",
    artist: "Aurora Band",
    albumId: "a1",
    albumTitle: "Alpha Waves",
    genre: "Ambient",
    duration: "03:20",
    durationSeconds: 200,
    isFavorite: true,
    trackNumber: "02",
    audioUrl: "/api/audio/t2",
  };
  const trackThree = {
    id: "t3",
    title: "01 Horizon",
    artist: "Boreal Quartet",
    albumId: "a2",
    albumTitle: "Zenith Echo",
    genre: "Jazz",
    duration: "04:00",
    durationSeconds: 240,
    isFavorite: false,
    trackNumber: "01",
    audioUrl: "/api/audio/t3",
  };

  return {
    albums: [
      {
        id: "a1",
        title: "Alpha Waves",
        artist: "Aurora Band",
        genre: "Ambient",
        year: 2024,
        tracks: [
          {
            id: trackOne.id,
            title: trackOne.title,
            artist: trackOne.artist,
            duration: trackOne.duration,
            durationSeconds: trackOne.durationSeconds,
            isFavorite: trackOne.isFavorite,
            audioUrl: trackOne.audioUrl,
          },
          {
            id: trackTwo.id,
            title: trackTwo.title,
            artist: trackTwo.artist,
            duration: trackTwo.duration,
            durationSeconds: trackTwo.durationSeconds,
            isFavorite: trackTwo.isFavorite,
            audioUrl: trackTwo.audioUrl,
          },
        ],
      },
      {
        id: "a2",
        title: "Zenith Echo",
        artist: "Boreal Quartet",
        genre: "Jazz",
        year: 2023,
        tracks: [
          {
            id: trackThree.id,
            title: trackThree.title,
            artist: trackThree.artist,
            duration: trackThree.duration,
            durationSeconds: trackThree.durationSeconds,
            isFavorite: trackThree.isFavorite,
            audioUrl: trackThree.audioUrl,
          },
        ],
      },
    ],
    allTracks: [trackOne, trackTwo, trackThree],
    playlists: [{ id: "p1", name: "Morning Mix", trackIds: ["t1", "t3"] }],
  };
}

/**
 * Build an intentionally unsorted multi-letter catalog fixture for library controller tests.
 *
 * @param {object} [overrides] Optional top-level field overrides such as playlists.
 * @returns {object} Library catalog fixture.
 */
function buildLibraryTestCatalog(overrides = {}) {
  const zebraAlbum = {
    id: "album-z",
    title: "Zebra Echoes",
    artist: "Zeta Band",
    genre: "Synth",
    year: 2022,
    tracks: [
      {
        id: "t-z1",
        title: "Zebra Prelude",
        artist: "Zeta Band",
        duration: "03:00",
        durationSeconds: 180,
        isFavorite: false,
        audioUrl: "/api/audio/t-z1",
      },
      {
        id: "t-z2",
        title: "Alpha Coda",
        artist: "Zeta Band",
        duration: "03:15",
        durationSeconds: 195,
        isFavorite: false,
        audioUrl: "/api/audio/t-z2",
      },
    ],
  };

  const midnightAlbum = {
    id: "album-m",
    title: "Midnight Suite",
    artist: "Mono Quartet",
    genre: "Jazz",
    year: 2021,
    tracks: [
      {
        id: "t-m1",
        title: "Midnight Theme",
        artist: "Mono Quartet",
        duration: "03:30",
        durationSeconds: 210,
        isFavorite: false,
        audioUrl: "/api/audio/t-m1",
      },
      {
        id: "t-m2",
        title: "Boreal Drift",
        artist: "Mono Quartet",
        duration: "03:45",
        durationSeconds: 225,
        isFavorite: false,
        audioUrl: "/api/audio/t-m2",
      },
    ],
  };

  const crystalAlbum = {
    id: "album-c",
    title: "Crystal Horizon",
    artist: "Cobalt Trio",
    genre: "Ambient",
    year: 2023,
    tracks: [
      {
        id: "t-c1",
        title: "Crystal Spark",
        artist: "Cobalt Trio",
        duration: "02:50",
        durationSeconds: 170,
        isFavorite: false,
        audioUrl: "/api/audio/t-c1",
      },
    ],
  };

  const alphaAlbum = {
    id: "album-a",
    title: "Alpha Dawn",
    artist: "Amber Duo",
    genre: "Acoustic",
    year: 2024,
    tracks: [
      {
        id: "t-a1",
        title: "Amber Sunrise",
        artist: "Amber Duo",
        duration: "02:40",
        durationSeconds: 160,
        isFavorite: false,
        audioUrl: "/api/audio/t-a1",
      },
      {
        id: "t-a2",
        title: "Daybreak Pulse",
        artist: "Amber Duo",
        duration: "02:55",
        durationSeconds: 175,
        isFavorite: false,
        audioUrl: "/api/audio/t-a2",
      },
    ],
  };

  const unsortedAlbums = [zebraAlbum, midnightAlbum, crystalAlbum, alphaAlbum];
  const unsortedAllTracks = [];
  for (const albumEntry of unsortedAlbums) {
    albumEntry.tracks.forEach((trackEntry, trackPosition) => {
      unsortedAllTracks.push({
        id: trackEntry.id,
        title: trackEntry.title,
        artist: trackEntry.artist,
        albumId: albumEntry.id,
        albumTitle: albumEntry.title,
        genre: albumEntry.genre,
        duration: trackEntry.duration,
        durationSeconds: trackEntry.durationSeconds,
        isFavorite: trackEntry.isFavorite,
        trackNumber: String(trackPosition + 1).padStart(2, "0"),
        audioUrl: trackEntry.audioUrl,
      });
    });
  }

  return {
    counts: {
      tracks: unsortedAllTracks.length,
      albums: unsortedAlbums.length,
      artists: 4,
      genres: 4,
      favorites: 0,
      playlists: Array.isArray(overrides.playlists) ? overrides.playlists.length : 0,
    },
    albums: unsortedAlbums,
    artists: [
      { id: "artist-z", name: "Zeta Band", countLabel: "1", albumIds: ["album-z"] },
      { id: "artist-a", name: "Amber Duo", countLabel: "1", albumIds: ["album-a"] },
      { id: "artist-m", name: "Mono Quartet", countLabel: "1", albumIds: ["album-m"] },
      { id: "artist-c", name: "Cobalt Trio", countLabel: "1", albumIds: ["album-c"] },
    ],
    genres: [
      { id: "genre-s", name: "Synth", countLabel: "2", albumIds: ["album-z"] },
      { id: "genre-a", name: "Acoustic", countLabel: "2", albumIds: ["album-a"] },
      { id: "genre-j", name: "Jazz", countLabel: "2", albumIds: ["album-m"] },
      { id: "genre-m", name: "Ambient", countLabel: "1", albumIds: ["album-c"] },
    ],
    allTracks: unsortedAllTracks,
    playlists: [],
    ...overrides,
  };
}

/**
 * Build a multi-letter catalog spanning twelve distinct initial letters for rim letter-seek tests.
 *
 * @returns {object} Dense multi-letter library catalog fixture.
 */
function buildLetterSeekTestCatalog() {
  const albumTitles = [
    "Alpha Dawn",
    "Boreal Wind",
    "Crystal Horizon",
    "Delta Pulse",
    "Echo Ridge",
    "Frost Mirror",
    "Glass Harbor",
    "Harbor Lights",
    "Jupiter Field",
    "Kestrel Flight",
    "Midnight Suite",
    "Zebra Echoes",
  ];
  const albums = [];
  const allTracks = [];
  const artists = [];

  albumTitles.forEach((albumTitle, entryIndex) => {
    const albumId = `seek-album-${entryIndex}`;
    const trackId = `seek-track-${entryIndex}`;
    const artistName = `${albumTitle} Ensemble`;
    const trackEntry = {
      id: trackId,
      title: albumTitle,
      artist: artistName,
      albumId,
      albumTitle,
      genre: "Ambient",
      duration: "03:00",
      durationSeconds: 180,
      isFavorite: false,
      trackNumber: "01",
      audioUrl: `/api/audio/${trackId}`,
    };
    albums.push({
      id: albumId,
      title: albumTitle,
      artist: artistName,
      genre: "Ambient",
      year: 2024,
      tracks: [trackEntry],
    });
    allTracks.push(trackEntry);
    artists.push({
      id: `seek-artist-${entryIndex}`,
      name: artistName,
      countLabel: "1",
      albumIds: [albumId],
    });
  });

  return {
    counts: {
      tracks: allTracks.length,
      albums: albums.length,
      artists: artists.length,
      genres: 1,
      favorites: 0,
      playlists: 0,
    },
    albums,
    artists,
    genres: [
      {
        id: "seek-genre-ambient",
        name: "Ambient",
        countLabel: String(allTracks.length),
        albumIds: albums.map((albumEntry) => albumEntry.id),
      },
    ],
    allTracks,
    playlists: [],
  };
}

/**
 * Advance a library controller over fixed simulation steps and return the latest snapshot.
 *
 * @param {object} libraryController Library controller instance.
 * @param {number} stepDeltaSeconds Simulation step in seconds.
 * @param {number} stepCount Number of steps to advance.
 * @returns {object} Render snapshot after stepping.
 */
function advanceLibraryController(libraryController, stepDeltaSeconds, stepCount) {
  for (let stepIndex = 0; stepIndex < stepCount; stepIndex += 1) {
    libraryController.step(stepDeltaSeconds);
  }
  return libraryController.getRenderSnapshot();
}

/**
 * Create a library controller hydrated with a test catalog and settled on the Hub view.
 *
 * @param {object} [options] Optional controller callbacks and catalog overrides.
 * @returns {object} Settled library controller instance.
 */
function createSettledLibraryController(options = {}) {
  const { catalog = buildLibraryTestCatalog(), ...controllerCallbacks } = options;
  const libraryController = createLibraryController(controllerCallbacks);
  libraryController.hydrateCatalog(catalog);
  libraryController.enterFromHome();
  libraryController.step(LIBRARY_CONSTANTS.TRANSITION_DURATION_SECONDS);
  return libraryController;
}

/**
 * Perform a tap gesture at the specified canvas coordinates.
 *
 * @param {object} libraryController Library controller instance.
 * @param {number} pointX Tap X coordinate in pixels.
 * @param {number} pointY Tap Y coordinate in pixels.
 */
function tapPoint(libraryController, pointX, pointY) {
  libraryController.handlePointerDown(pointX, pointY);
  libraryController.handlePointerUp(pointX, pointY);
}

/**
 * Execute a left-edge rightward swipe gesture to trigger back navigation.
 *
 * @param {object} libraryController Library controller instance.
 */
function performLeftEdgeBackSwipe(libraryController) {
  libraryController.handlePointerDown(BACK_SWIPE_START_X, BACK_SWIPE_Y);
  libraryController.handlePointerMove(BACK_SWIPE_COMMIT_END_X, BACK_SWIPE_Y);
  libraryController.handlePointerUp(BACK_SWIPE_COMMIT_END_X, BACK_SWIPE_Y);
}

/**
 * Compute Cartesian coordinates on the outer rim arc for a given angle in radians.
 *
 * @param {number} angleRadians Angle around display center.
 * @returns {{ pointX: number, pointY: number }} Outer rim coordinates.
 */
function computeRimPoint(angleRadians) {
  return {
    pointX: DISPLAY_CENTER_PX + RIM_DRAG_RADIUS_PX * Math.cos(angleRadians),
    pointY: DISPLAY_CENTER_PX + RIM_DRAG_RADIUS_PX * Math.sin(angleRadians),
  };
}

/**
 * Drag smoothly clockwise along the outer rim in small angle increments.
 *
 * @param {object} libraryController Library controller instance.
 * @param {number} startAngleRadians Initial angle already pressed or to move from.
 * @param {number} endAngleRadians Target angle to reach clockwise.
 */
function dragClockwiseAlongRim(libraryController, startAngleRadians, endAngleRadians) {
  let currentAngle = startAngleRadians;
  while (currentAngle + RIM_ARC_STEP_RADIANS < endAngleRadians) {
    currentAngle += RIM_ARC_STEP_RADIANS;
    const rimPoint = computeRimPoint(currentAngle);
    libraryController.handlePointerMove(rimPoint.pointX, rimPoint.pointY);
  }
  const finalRimPoint = computeRimPoint(endAngleRadians);
  libraryController.handlePointerMove(finalRimPoint.pointX, finalRimPoint.pointY);
}

/**
 * Open a Hub section tile cleanly by scrolling to its pair row and tapping the tile center.
 *
 * @param {object} libraryController Settled library controller on Hub.
 * @param {number} tileIndex Section tile index in [0, 5].
 * @returns {object} Settled render snapshot of the opened view.
 */
function openHubSectionTile(libraryController, tileIndex) {
  const targetPairRow = Math.floor(tileIndex / 2);
  for (let detentStep = 0; detentStep < targetPairRow; detentStep += 1) {
    libraryController.stepDetent(1);
  }
  const settledHubSnapshot = advanceLibraryController(
    libraryController,
    STEP_DELTA_SECONDS,
    SETTLE_STEP_COUNT,
  );
  const tileRect = computeLibraryHubTileRect(tileIndex, settledHubSnapshot.scrollPosition);
  tapPoint(libraryController, tileRect.centerX, tileRect.centerY);
  return advanceLibraryController(libraryController, STEP_DELTA_SECONDS, SETTLE_STEP_COUNT);
}

/**
 * Assert that a rim drag past 180 degrees triggers Crystal-Slab Letter Seek and neighbor pull weights.
 *
 * @param {object} libraryController Controller currently positioned on a scrollable list or crate.
 */
function assertRimLetterSeekBehavior(libraryController) {
  const startPoint = computeRimPoint(RIM_START_ANGLE_RADIANS);
  libraryController.handlePointerDown(startPoint.pointX, startPoint.pointY);

  const midpointAngle = RIM_START_ANGLE_RADIANS + Math.PI * 0.5;
  dragClockwiseAlongRim(libraryController, RIM_START_ANGLE_RADIANS, midpointAngle);
  const midpointSnapshot = libraryController.getRenderSnapshot();
  assert.equal(midpointSnapshot.letterSearchActive, false);
  assert.ok(midpointSnapshot.scrollPosition > 0);

  const preThresholdAngle = RIM_START_ANGLE_RADIANS + Math.PI * 0.94;
  dragClockwiseAlongRim(libraryController, midpointAngle, preThresholdAngle);
  assert.equal(libraryController.getRenderSnapshot().letterSearchActive, false);

  const pastThresholdAngle = RIM_START_ANGLE_RADIANS + Math.PI * 1.15;
  dragClockwiseAlongRim(libraryController, preThresholdAngle, pastThresholdAngle);
  const activeSearchSnapshot = libraryController.getRenderSnapshot();
  assert.equal(activeSearchSnapshot.letterSearchActive, true);
  assert.ok(activeSearchSnapshot.letterEntries.length >= 2);
  const initialLetterIndex = activeSearchSnapshot.letterSearchIndex;

  const steppedAngle =
    pastThresholdAngle + LIBRARY_CONSTANTS.LETTER_STEP_RADIANS + RIM_STEP_EPSILON_RADIANS;
  dragClockwiseAlongRim(libraryController, pastThresholdAngle, steppedAngle);
  const jumpedSnapshot = libraryController.getRenderSnapshot();
  assert.ok(jumpedSnapshot.letterSearchIndex > initialLetterIndex);

  const selectedLetterEntry = jumpedSnapshot.letterEntries[jumpedSnapshot.letterSearchIndex];
  assert.equal(jumpedSnapshot.targetIndex, selectedLetterEntry.itemIndex);
  assert.equal(jumpedSnapshot.scrollPosition, selectedLetterEntry.itemIndex);

  libraryController.step(0.1);
  const pullSnapshot = libraryController.getRenderSnapshot();
  const sliceCount = LIBRARY_CONSTANTS.RIM_SLICE_COUNT;
  const hoveredSliceIndex = resolveLibraryRimSliceIndex(steppedAngle);

  assert.equal(pullSnapshot.rimSlabPullLevels.length, sliceCount);
  assert.equal(pullSnapshot.rimSlabPullLevels[hoveredSliceIndex], 1);
  assert.ok(pullSnapshot.rimSlabPullLevels[(hoveredSliceIndex + 1) % sliceCount] >= 0.30);
  assert.ok(
    pullSnapshot.rimSlabPullLevels[(hoveredSliceIndex - 1 + sliceCount) % sliceCount] >= 0.30,
  );
  assert.ok(pullSnapshot.rimSlabPullLevels[(hoveredSliceIndex + 2) % sliceCount] >= 0.08);
  assert.ok(
    pullSnapshot.rimSlabPullLevels[(hoveredSliceIndex - 2 + sliceCount) % sliceCount] >= 0.08,
  );

  const endPoint = computeRimPoint(steppedAngle);
  libraryController.handlePointerUp(endPoint.pointX, endPoint.pointY);
}

describe("createAudioEngine", () => {
  test("plays track from album source, syncs elapsedSeconds, auto-advances on ended, and pauses at queue end", () => {
    const stubAudio = createMockAudioStub();
    const audioEngine = createAudioEngine({
      createAudioElement: () => stubAudio,
    });
    const catalog = buildAudioTestCatalog();
    const albumOne = catalog.albums[0];

    audioEngine.playTrackFromSource(
      { trackId: "t1", queueSource: "album", albumId: "a1" },
      catalog,
    );

    const playingState = audioEngine.getState();
    assert.equal(playingState.playbackState, "playing");
    assert.equal(playingState.isPlaying, true);
    assert.equal(playingState.activeTrack.id, "t1");
    assert.equal(playingState.activeAlbum.id, "a1");
    assert.equal(playingState.queueTracks.length, albumOne.tracks.length);
    assert.equal(playingState.queueIndex, 0);
    assert.equal(stubAudio.src, "/api/audio/t1");
    assert.ok(stubAudio.playCalls >= 1);

    audioEngine.step(0.5);
    const steppedState = audioEngine.getState();
    assert.ok(steppedState.elapsedSeconds >= 0 && Number.isFinite(steppedState.elapsedSeconds));

    stubAudio.currentTime = 12.5;
    audioEngine.step(0.05);
    assert.equal(audioEngine.getState().elapsedSeconds, 12.5);

    stubAudio.emit("ended");
    const advancedState = audioEngine.getState();
    assert.equal(advancedState.queueIndex, 1);
    assert.equal(advancedState.activeTrack.id, "t2");
    assert.equal(advancedState.playbackState, "playing");
    assert.equal(advancedState.isPlaying, true);
    assert.equal(stubAudio.src, "/api/audio/t2");

    stubAudio.emit("ended");
    const finishedState = audioEngine.getState();
    assert.equal(finishedState.playbackState, "paused");
    assert.equal(finishedState.isPlaying, false);
  });

  test("supports pause, play, togglePlayPause, clamped seekTo, and clamped setVolume", () => {
    const stubAudio = createMockAudioStub();
    const audioEngine = createAudioEngine({
      createAudioElement: () => stubAudio,
    });
    const catalog = buildAudioTestCatalog();

    audioEngine.playTrackFromSource(
      { trackId: "t1", queueSource: "album", albumId: "a1" },
      catalog,
    );

    audioEngine.pause();
    assert.equal(audioEngine.getState().playbackState, "paused");
    assert.equal(audioEngine.getState().isPlaying, false);
    assert.equal(stubAudio.paused, true);
    assert.ok(stubAudio.pauseCalls >= 1);

    audioEngine.play();
    assert.equal(audioEngine.getState().playbackState, "playing");
    assert.equal(audioEngine.getState().isPlaying, true);
    assert.equal(stubAudio.paused, false);

    audioEngine.togglePlayPause();
    assert.equal(audioEngine.getState().playbackState, "paused");
    assert.equal(audioEngine.getState().isPlaying, false);

    audioEngine.togglePlayPause();
    assert.equal(audioEngine.getState().playbackState, "playing");
    assert.equal(audioEngine.getState().isPlaying, true);

    audioEngine.seekTo(45);
    assert.equal(audioEngine.getState().elapsedSeconds, 45);
    assert.equal(stubAudio.currentTime, 45);

    audioEngine.seekTo(-10);
    assert.equal(audioEngine.getState().elapsedSeconds, 0);
    assert.equal(stubAudio.currentTime, 0);

    audioEngine.seekTo(999);
    assert.equal(audioEngine.getState().elapsedSeconds, 180);
    assert.equal(stubAudio.currentTime, 180);

    audioEngine.setVolume(0.65);
    assert.equal(audioEngine.getState().volume, 0.65);
    assert.equal(stubAudio.volume, 0.65);

    audioEngine.setVolume(-0.25);
    assert.equal(audioEngine.getState().volume, 0);
    assert.equal(stubAudio.volume, 0);

    audioEngine.setVolume(1.5);
    assert.equal(audioEngine.getState().volume, 1);
    assert.equal(stubAudio.volume, 1);
  });

  test("falls back gracefully in headless Node without createAudioElement and ignores unknown trackId", () => {
    const headlessEngine = createAudioEngine();
    const catalog = buildAudioTestCatalog();

    headlessEngine.playTrackFromSource(
      { trackId: "unknown-track", queueSource: "album", albumId: "a1" },
      catalog,
    );
    const idleState = headlessEngine.getState();
    assert.equal(idleState.playbackState, "idle");
    assert.equal(idleState.isPlaying, false);
    assert.equal(idleState.activeTrack, null);

    headlessEngine.playTrackFromSource(
      { trackId: "t1", queueSource: "album", albumId: "a1" },
      catalog,
    );
    assert.equal(headlessEngine.getState().playbackState, "playing");
    assert.equal(headlessEngine.getState().activeTrack.id, "t1");

    headlessEngine.step(1.25);
    assert.ok(headlessEngine.getState().elapsedSeconds >= 1.25);
    assert.ok(Number.isFinite(headlessEngine.getState().elapsedSeconds));

    headlessEngine.step(-1);
    headlessEngine.step(Number.NaN);
    headlessEngine.step(Number.POSITIVE_INFINITY);
    assert.ok(Number.isFinite(headlessEngine.getState().elapsedSeconds));

    headlessEngine.playTrackFromSource(
      { trackId: "missing-track-id", queueSource: "album", albumId: "a1" },
      catalog,
    );
    assert.equal(headlessEngine.getState().activeTrack.id, "t1");
    assert.equal(headlessEngine.getState().playbackState, "playing");
  });
});

describe("library_controller.js pure helpers", () => {
  test("LIBRARY_HUB_SECTIONS and LIBRARY_CONSTANTS expose canonical order and thresholds", () => {
    assert.deepEqual(
      LIBRARY_HUB_SECTIONS.map((sectionEntry) => sectionEntry.id),
      ["favorites", "playlists", "artists", "albums", "genres", "all-songs"],
    );

    assert.equal(LIBRARY_CONSTANTS.LIST_TOP_SLOT_Y, 126);
    assert.equal(LIBRARY_CONSTANTS.LIST_ROW_PITCH_PX, 74);
    assert.equal(LIBRARY_CONSTANTS.LIST_FULL_SLOTS, 2);
    assert.equal(LIBRARY_CONSTANTS.CRATE_CENTER_Y, 152);
    assert.equal(LIBRARY_CONSTANTS.CRATE_SPACING_PX, 130);
    assert.equal(LIBRARY_CONSTANTS.ARC_RIM_MIN_RADIUS_PX, 112);
    assert.equal(LIBRARY_CONSTANTS.LETTER_SEARCH_THRESHOLD_RADIANS, Math.PI);
    assert.equal(LIBRARY_CONSTANTS.DELETE_HOLD_DURATION_SECONDS, 1.10);
    assert.equal(LIBRARY_CONSTANTS.LONG_PRESS_DURATION_SECONDS, 0.48);
    assert.equal(LIBRARY_CONSTANTS.VINYL_LAUNCH_DURATION_SECONDS, 0.62);
    assert.equal(LIBRARY_CONSTANTS.TRANSITION_DURATION_SECONDS, 0.28);
    assert.equal(LIBRARY_CONSTANTS.RIM_SLICE_COUNT, 17);
    assert.ok(
      Number.isFinite(LIBRARY_CONSTANTS.LETTER_STEP_RADIANS) &&
        LIBRARY_CONSTANTS.LETTER_STEP_RADIANS > 0,
    );
  });

  test("compareAlphabetically and sortAlphabeticallyBy order labels ascending without mutating source array", () => {
    assert.ok(compareAlphabetically("Alpha", "Zebra") < 0);
    assert.ok(compareAlphabetically("Zebra", "Alpha") > 0);
    assert.equal(compareAlphabetically("Amber", "amber"), 0);

    const originalEntries = [
      { id: "three", title: "Zebra" },
      { id: "one", title: "Alpha" },
      { id: "two", title: "Midnight" },
    ];
    const sortedEntries = sortAlphabeticallyBy(originalEntries, (entry) => entry.title);

    assert.deepEqual(
      sortedEntries.map((entry) => entry.title),
      ["Alpha", "Midnight", "Zebra"],
    );
    assert.deepEqual(
      originalEntries.map((entry) => entry.title),
      ["Zebra", "Alpha", "Midnight"],
    );
    assert.deepEqual(
      sortAlphabeticallyBy([], (entry) => entry.title),
      [],
    );
  });

  test("computeLibraryHubTileRect and computeLibraryCurvedRowLayout return finite coordinates and bounded alpha", () => {
    for (let tileIndex = 0; tileIndex < LIBRARY_HUB_SECTIONS.length; tileIndex += 1) {
      const tileRect = computeLibraryHubTileRect(tileIndex, 0);
      assert.ok(Number.isFinite(tileRect.centerX));
      assert.ok(Number.isFinite(tileRect.centerY));
      assert.ok(Number.isFinite(tileRect.width) && tileRect.width > 0);
      assert.ok(Number.isFinite(tileRect.height) && tileRect.height > 0);
      assert.ok(Number.isFinite(tileRect.alpha) && tileRect.alpha >= 0 && tileRect.alpha <= 1);
    }

    const leftTileRect = computeLibraryHubTileRect(0, 0);
    const rightTileRect = computeLibraryHubTileRect(1, 0);
    assert.ok(leftTileRect.centerX < DISPLAY_CENTER_PX);
    assert.ok(rightTileRect.centerX > DISPLAY_CENTER_PX);
    assert.ok(Math.abs(leftTileRect.centerY - rightTileRect.centerY) < FLOAT_TOLERANCE);

    const topRowLayout = computeLibraryCurvedRowLayout(0, 0);
    const nextRowLayout = computeLibraryCurvedRowLayout(1, 0);
    const distantRowLayout = computeLibraryCurvedRowLayout(8, 0);

    for (const rowLayout of [topRowLayout, nextRowLayout, distantRowLayout]) {
      assert.ok(Number.isFinite(rowLayout.centerX));
      assert.ok(Number.isFinite(rowLayout.centerY));
      assert.ok(Number.isFinite(rowLayout.width) && rowLayout.width > 0);
      assert.ok(Number.isFinite(rowLayout.height) && rowLayout.height > 0);
      assert.ok(Number.isFinite(rowLayout.alpha) && rowLayout.alpha >= 0 && rowLayout.alpha <= 1);
    }
    assert.ok(nextRowLayout.centerY > topRowLayout.centerY);
    assert.ok(topRowLayout.alpha >= distantRowLayout.alpha);
  });

  test("resolveLibraryRimSliceIndex and shortestSliceDistance wrap circularly across 17 slices", () => {
    const sliceCount = LIBRARY_CONSTANTS.RIM_SLICE_COUNT;
    assert.equal(sliceCount, 17);

    const observedSlices = new Set();
    const sliceStepRadians = (Math.PI * 2) / sliceCount;
    for (let sliceOffset = 0; sliceOffset < sliceCount; sliceOffset += 1) {
      const sampleAngle = RIM_START_ANGLE_RADIANS + sliceOffset * sliceStepRadians;
      const sliceIndex = resolveLibraryRimSliceIndex(sampleAngle);
      assert.ok(Number.isInteger(sliceIndex));
      assert.ok(sliceIndex >= 0 && sliceIndex < sliceCount);
      observedSlices.add(sliceIndex);

      assert.equal(resolveLibraryRimSliceIndex(sampleAngle + Math.PI * 2), sliceIndex);
      assert.equal(resolveLibraryRimSliceIndex(sampleAngle - Math.PI * 2), sliceIndex);
    }
    assert.equal(observedSlices.size, sliceCount);

    assert.equal(shortestSliceDistance(0, 0), 0);
    assert.equal(shortestSliceDistance(0, 1), 1);
    assert.equal(shortestSliceDistance(0, 16), 1);
    assert.equal(shortestSliceDistance(16, 0), 1);
    assert.equal(shortestSliceDistance(2, 15), 4);
    assert.equal(shortestSliceDistance(0, 8), 8);
    assert.equal(shortestSliceDistance(0, 9), 8);
  });

  test("buildAlphabeticalLetterIndex groups sorted entries by starting letter", () => {
    const sortedEntries = [
      { title: "Alpha Dawn" },
      { title: "Amber Sunrise" },
      { title: "Midnight Theme" },
      { title: "Zebra Echoes" },
    ];
    const letterEntries = buildAlphabeticalLetterIndex(sortedEntries, (entry) => entry.title);
    assert.deepEqual(
      letterEntries.map((entry) => ({ letter: entry.letter, itemIndex: entry.itemIndex })),
      [
        { letter: "A", itemIndex: 0 },
        { letter: "M", itemIndex: 2 },
        { letter: "Z", itemIndex: 3 },
      ],
    );
    assert.deepEqual(
      buildAlphabeticalLetterIndex([], (entry) => entry.title),
      [],
    );
  });

  test("computeWindowedCrateBars returns focused 7-bar window for 271 albums and shrinks cleanly for small catalogs", () => {
    const startWindow = computeWindowedCrateBars(0, 271, 7);
    assert.equal(startWindow.length, 7);
    assert.deepEqual(
      startWindow.map((barEntry) => barEntry.albumIndex),
      [0, 1, 2, 3, 4, 5, 6],
    );
    assert.ok(startWindow[0].focus > startWindow[1].focus);
    assert.ok(startWindow[0].width > startWindow[1].width);

    const midWindow = computeWindowedCrateBars(135, 271, 7);
    assert.equal(midWindow.length, 7);
    assert.deepEqual(
      midWindow.map((barEntry) => barEntry.albumIndex),
      [132, 133, 134, 135, 136, 137, 138],
    );
    const centerBar = midWindow[3];
    for (const barEntry of midWindow) {
      if (barEntry.albumIndex !== 135) {
        assert.ok(centerBar.focus > barEntry.focus);
        assert.ok(centerBar.width > barEntry.width);
      }
    }

    const endWindow = computeWindowedCrateBars(270, 271, 7);
    assert.equal(endWindow.length, 7);
    assert.deepEqual(
      endWindow.map((barEntry) => barEntry.albumIndex),
      [264, 265, 266, 267, 268, 269, 270],
    );
    assert.ok(endWindow[6].focus > endWindow[5].focus);
    assert.ok(endWindow[6].width > endWindow[5].width);

    const smallWindow = computeWindowedCrateBars(1, 4, 7);
    assert.equal(smallWindow.length, 4);
    assert.deepEqual(
      smallWindow.map((barEntry) => barEntry.albumIndex),
      [0, 1, 2, 3],
    );

    assert.deepEqual(computeWindowedCrateBars(0, 0, 7), []);
  });
});

describe("createLibraryController nominal behavior", () => {
  test("hydrateCatalog sorts top-level lists alphabetically while preserving album track order", () => {
    const libraryController = createSettledLibraryController();
    const snapshot = libraryController.getRenderSnapshot();

    assert.deepEqual(
      snapshot.catalog.albums.map((albumEntry) => albumEntry.title),
      ["Alpha Dawn", "Crystal Horizon", "Midnight Suite", "Zebra Echoes"],
    );
    assert.deepEqual(
      snapshot.catalog.artists.map((artistEntry) => artistEntry.name),
      ["Amber Duo", "Cobalt Trio", "Mono Quartet", "Zeta Band"],
    );
    assert.deepEqual(
      snapshot.catalog.genres.map((genreEntry) => genreEntry.name),
      ["Acoustic", "Ambient", "Jazz", "Synth"],
    );
    assert.deepEqual(
      snapshot.catalog.allTracks.map((trackEntry) => trackEntry.title),
      [
        "Alpha Coda",
        "Amber Sunrise",
        "Boreal Drift",
        "Crystal Spark",
        "Daybreak Pulse",
        "Midnight Theme",
        "Zebra Prelude",
      ],
    );

    const zebraAlbum = snapshot.catalog.albums.find((albumEntry) => albumEntry.id === "album-z");
    assert.deepEqual(
      zebraAlbum.tracks.map((trackEntry) => trackEntry.title),
      ["Zebra Prelude", "Alpha Coda"],
    );
  });

  test("enterFromHome initializes Hub viewStack and accurate section counts", () => {
    const libraryController = createLibraryController();
    libraryController.hydrateCatalog(buildLibraryTestCatalog());
    libraryController.enterFromHome();

    const hubSnapshot = libraryController.getRenderSnapshot();
    assert.equal(hubSnapshot.viewStack.length, 1);
    assert.equal(hubSnapshot.viewStack[0].kind, "hub");

    const countsById = Object.fromEntries(
      hubSnapshot.currentRows.map((rowEntry) => [rowEntry.id, rowEntry.countLabel]),
    );
    assert.deepEqual(countsById, {
      favorites: "0",
      playlists: "0",
      artists: "4",
      albums: "4",
      genres: "4",
      "all-songs": "7",
    });
  });

  test("pointer drill-down from Hub Artists -> Artist -> Album -> Track launches vinyl and fires onLaunchTrack", () => {
    const launchedPayloads = [];
    const libraryController = createSettledLibraryController({
      onLaunchTrack: (launchPayload) => launchedPayloads.push(launchPayload),
    });

    const artistsSnapshot = openHubSectionTile(libraryController, 2);
    assert.equal(artistsSnapshot.viewStack.length, 2);
    assert.equal(artistsSnapshot.viewStack[1].kind, "artists");

    const firstArtistRow = computeLibraryCurvedRowLayout(0, 0);
    tapPoint(libraryController, firstArtistRow.centerX, firstArtistRow.centerY);
    const artistAlbumsSnapshot = advanceLibraryController(
      libraryController,
      STEP_DELTA_SECONDS,
      SETTLE_STEP_COUNT,
    );
    assert.equal(artistAlbumsSnapshot.viewStack.length, 3);
    assert.equal(artistAlbumsSnapshot.viewStack[2].kind, "albums");
    assert.deepEqual(artistAlbumsSnapshot.viewStack[2].albumIds, ["album-a"]);

    tapPoint(libraryController, CRATE_NOTE_BUTTON_X, CRATE_NOTE_BUTTON_Y);
    const tracksSnapshot = advanceLibraryController(
      libraryController,
      STEP_DELTA_SECONDS,
      SETTLE_STEP_COUNT,
    );
    assert.equal(tracksSnapshot.albumTracksOverlayOpen, true);
    assert.equal(tracksSnapshot.viewStack.length, 4);
    assert.equal(tracksSnapshot.viewStack[3].kind, "tracks");
    assert.equal(tracksSnapshot.viewStack[3].sourceType, "album");

    const firstTrackRow = computeLibraryCurvedRowLayout(0, 0);
    tapPoint(libraryController, firstTrackRow.centerX, firstTrackRow.centerY);
    const launchedSnapshot = libraryController.getRenderSnapshot();

    assert.equal(launchedSnapshot.vinylLaunchTarget, 1);
    assert.equal(launchedSnapshot.activeTrackId, "t-a1");
    assert.deepEqual(launchedPayloads, [
      {
        trackId: "t-a1",
        queueSource: "album",
        albumId: "album-a",
      },
    ]);
  });

  test("tapping album sleeve or peeking vinyl disc in Albums Crate launches with album-crate origin, swipe-up does not open tracklist, and floating note button toggles albumTracksOverlayOpen", () => {
    const launchedPayloads = [];
    const libraryController = createSettledLibraryController({
      onLaunchTrack: (launchPayload) => launchedPayloads.push(launchPayload),
    });

    const albumsSnapshot = openHubSectionTile(libraryController, 3);
    assert.equal(albumsSnapshot.viewStack[albumsSnapshot.viewStack.length - 1].kind, "albums");
    assert.equal(albumsSnapshot.albumTracksOverlayOpen, false);

    tapPoint(libraryController, CRATE_SLEEVE_TAP_X, CRATE_CENTER_Y);
    const sleeveLaunchedSnapshot = libraryController.getRenderSnapshot();

    assert.equal(sleeveLaunchedSnapshot.vinylLaunchTarget, 1);
    assert.equal(sleeveLaunchedSnapshot.activeTrackId, "t-a1");
    assert.deepEqual(launchedPayloads, [
      {
        trackId: "t-a1",
        queueSource: "album",
        albumId: "album-a",
        launchOrigin: "album-crate",
      },
    ]);

    libraryController.stepBack();
    advanceLibraryController(libraryController, STEP_DELTA_SECONDS, SETTLE_STEP_COUNT);

    tapPoint(libraryController, CRATE_PEEK_DISC_TAP_X, CRATE_CENTER_Y);
    const discLaunchedSnapshot = libraryController.getRenderSnapshot();

    assert.equal(discLaunchedSnapshot.vinylLaunchTarget, 1);
    assert.equal(launchedPayloads.length, 2);
    assert.deepEqual(launchedPayloads[1], {
      trackId: "t-a1",
      queueSource: "album",
      albumId: "album-a",
      launchOrigin: "album-crate",
    });

    libraryController.stepBack();
    advanceLibraryController(libraryController, STEP_DELTA_SECONDS, SETTLE_STEP_COUNT);

    // Vertical swipe-up inside Albums crate must NOT open the album tracklist anymore.
    libraryController.handlePointerDown(180, 296);
    libraryController.handlePointerMove(180, 240);
    libraryController.handlePointerMove(180, 180);
    libraryController.handlePointerUp(180, 180);
    const afterSwipeUpSnapshot = advanceLibraryController(
      libraryController,
      STEP_DELTA_SECONDS,
      SETTLE_STEP_COUNT,
    );

    assert.equal(afterSwipeUpSnapshot.currentView.kind, "albums");
    assert.equal(afterSwipeUpSnapshot.albumTracksOverlayOpen, false);
    assert.equal(afterSwipeUpSnapshot.cratePullUpProgress, 0);
    assert.equal(afterSwipeUpSnapshot.viewStack.length, 2);

    // Swipe-up starting directly on the album sleeve must also stay in Albums crate.
    libraryController.handlePointerDown(CRATE_SLEEVE_TAP_X, 210);
    libraryController.handlePointerMove(CRATE_SLEEVE_TAP_X, 130);
    libraryController.handlePointerUp(CRATE_SLEEVE_TAP_X, 130);
    const afterSleeveSwipeSnapshot = advanceLibraryController(
      libraryController,
      STEP_DELTA_SECONDS,
      SETTLE_STEP_COUNT,
    );
    assert.equal(afterSleeveSwipeSnapshot.currentView.kind, "albums");
    assert.equal(afterSleeveSwipeSnapshot.albumTracksOverlayOpen, false);

    // Tapping outside the 22px floating music-note button radius does not open the tracklist.
    tapPoint(libraryController, CRATE_NOTE_BUTTON_X + CRATE_NOTE_BUTTON_RADIUS_PX + 8, CRATE_NOTE_BUTTON_Y);
    assert.equal(libraryController.getRenderSnapshot().albumTracksOverlayOpen, false);
    assert.equal(libraryController.getRenderSnapshot().currentView.kind, "albums");

    // Tapping the floating music-note icon button opens the album tracklist overlay.
    tapPoint(libraryController, CRATE_NOTE_BUTTON_X, CRATE_NOTE_BUTTON_Y);
    const tracksSnapshot = advanceLibraryController(
      libraryController,
      STEP_DELTA_SECONDS,
      SETTLE_STEP_COUNT,
    );

    assert.equal(tracksSnapshot.albumTracksOverlayOpen, true);
    assert.equal(tracksSnapshot.currentView.kind, "tracks");
    assert.equal(tracksSnapshot.currentView.sourceType, "album");
    assert.equal(tracksSnapshot.currentView.albumId, "album-a");

    // Horizontal swipe inside the crystal album tracklist must stay locked to this album.
    libraryController.handlePointerDown(260, 180);
    libraryController.handlePointerMove(100, 180);
    libraryController.handlePointerUp(100, 180);
    const lockedTracksSnapshot = advanceLibraryController(
      libraryController,
      STEP_DELTA_SECONDS,
      SETTLE_STEP_COUNT,
    );
    assert.equal(lockedTracksSnapshot.albumTracksOverlayOpen, true);
    assert.equal(lockedTracksSnapshot.currentView.kind, "tracks");
    assert.equal(lockedTracksSnapshot.currentView.albumId, "album-a");

    // Pressing back closes the album tracklist overlay cleanly, and edge-radius note tap re-opens it.
    libraryController.stepBack();
    const closedOverlaySnapshot = advanceLibraryController(
      libraryController,
      STEP_DELTA_SECONDS,
      SETTLE_STEP_COUNT,
    );
    assert.equal(closedOverlaySnapshot.albumTracksOverlayOpen, false);
    assert.equal(closedOverlaySnapshot.currentView.kind, "albums");

    tapPoint(libraryController, CRATE_NOTE_BUTTON_X + 16, CRATE_NOTE_BUTTON_Y);
    const edgeButtonSnapshot = advanceLibraryController(
      libraryController,
      STEP_DELTA_SECONDS,
      SETTLE_STEP_COUNT,
    );
    assert.equal(edgeButtonSnapshot.albumTracksOverlayOpen, true);
    assert.equal(edgeButtonSnapshot.currentView.albumId, "album-a");

    performLeftEdgeBackSwipe(libraryController);
    const swipeClosedSnapshot = advanceLibraryController(
      libraryController,
      STEP_DELTA_SECONDS,
      SETTLE_STEP_COUNT,
    );
    assert.equal(swipeClosedSnapshot.albumTracksOverlayOpen, false);
    assert.equal(swipeClosedSnapshot.currentView.kind, "albums");
  });

  test("tapping album sleeve or peeking disc launches already-playing track of that album instead of tracks[0]", () => {
    const launchedPayloads = [];
    const libraryController = createSettledLibraryController({
      onLaunchTrack: (launchPayload) => launchedPayloads.push(launchPayload),
    });

    // Simulate second track of album-a ("t-a2") already active before entering Albums crate.
    libraryController.setActiveTrackId("t-a2");
    openHubSectionTile(libraryController, 3);

    tapPoint(libraryController, CRATE_SLEEVE_TAP_X, CRATE_CENTER_Y);
    const sleeveContinuedSnapshot = libraryController.getRenderSnapshot();
    assert.equal(sleeveContinuedSnapshot.vinylLaunchTarget, 1);
    assert.equal(sleeveContinuedSnapshot.activeTrackId, "t-a2");
    assert.deepEqual(launchedPayloads, [
      {
        trackId: "t-a2",
        queueSource: "album",
        albumId: "album-a",
        launchOrigin: "album-crate",
      },
    ]);

    libraryController.stepBack();
    advanceLibraryController(libraryController, STEP_DELTA_SECONDS, SETTLE_STEP_COUNT);

    tapPoint(libraryController, CRATE_PEEK_DISC_TAP_X, CRATE_CENTER_Y);
    const discContinuedSnapshot = libraryController.getRenderSnapshot();
    assert.equal(discContinuedSnapshot.vinylLaunchTarget, 1);
    assert.equal(discContinuedSnapshot.activeTrackId, "t-a2");
    assert.deepEqual(launchedPayloads[1], {
      trackId: "t-a2",
      queueSource: "album",
      albumId: "album-a",
      launchOrigin: "album-crate",
    });

    // When activeTrackId belongs to a different album, tapping album-a launches album-a's first track.
    libraryController.stepBack();
    advanceLibraryController(libraryController, STEP_DELTA_SECONDS, SETTLE_STEP_COUNT);
    libraryController.setActiveTrackId("t-m1");
    tapPoint(libraryController, CRATE_SLEEVE_TAP_X, CRATE_CENTER_Y);
    assert.deepEqual(launchedPayloads[2], {
      trackId: "t-a1",
      queueSource: "album",
      albumId: "album-a",
      launchOrigin: "album-crate",
    });
  });

  test("rim-arc drag triggers >180 degree Crystal-Slab Letter Seek and populates rimSlabPullLevels in All songs", () => {
    const libraryController = createSettledLibraryController({
      catalog: buildLetterSeekTestCatalog(),
    });
    const allSongsSnapshot = openHubSectionTile(libraryController, 5);
    assert.equal(allSongsSnapshot.viewStack[allSongsSnapshot.viewStack.length - 1].kind, "tracks");

    assertRimLetterSeekBehavior(libraryController);
  });

  test("rim-arc drag triggers >180 degree Crystal-Slab Letter Seek and populates rimSlabPullLevels in Albums Crate", () => {
    const libraryController = createSettledLibraryController({
      catalog: buildLetterSeekTestCatalog(),
    });
    const albumsSnapshot = openHubSectionTile(libraryController, 3);
    assert.equal(albumsSnapshot.viewStack[albumsSnapshot.viewStack.length - 1].kind, "albums");

    assertRimLetterSeekBehavior(libraryController);
  });

  test("back-navigation closes vinyl first, Playlist Action Sheet second, pops viewStack third, and exits to Home at Hub", () => {
    let exitCallCount = 0;
    const catalogWithPlaylist = buildLibraryTestCatalog({
      playlists: [{ id: "p1", name: "Mix One", trackIds: ["t-a1"] }],
    });
    const libraryController = createSettledLibraryController({
      catalog: catalogWithPlaylist,
      onExitToHome: () => {
        exitCallCount += 1;
      },
    });

    openHubSectionTile(libraryController, 3);
    tapPoint(libraryController, CRATE_PEEK_DISC_TAP_X, CRATE_CENTER_Y);
    assert.equal(libraryController.getRenderSnapshot().vinylLaunchTarget, 1);
    assert.equal(libraryController.getRenderSnapshot().viewStack.length, 2);

    libraryController.stepBack();
    const vinylClosedSnapshot = libraryController.getRenderSnapshot();
    assert.equal(vinylClosedSnapshot.vinylLaunchTarget, 0);
    assert.equal(vinylClosedSnapshot.viewStack.length, 2);
    assert.equal(exitCallCount, 0);

    performLeftEdgeBackSwipe(libraryController);
    const poppedToHubSnapshot = libraryController.getRenderSnapshot();
    assert.equal(poppedToHubSnapshot.viewStack.length, 1);
    assert.equal(poppedToHubSnapshot.viewStack[0].kind, "hub");
    assert.equal(exitCallCount, 0);

    openHubSectionTile(libraryController, 1);
    const playlistRowLayout = computeLibraryCurvedRowLayout(0, 0);
    libraryController.handlePointerDown(playlistRowLayout.centerX, playlistRowLayout.centerY);
    libraryController.step(0.50);
    libraryController.handlePointerUp(playlistRowLayout.centerX, playlistRowLayout.centerY);
    assert.equal(libraryController.getRenderSnapshot().playlistSheetOpen, true);
    assert.equal(libraryController.getRenderSnapshot().viewStack.length, 2);

    performLeftEdgeBackSwipe(libraryController);
    const sheetClosedSnapshot = libraryController.getRenderSnapshot();
    assert.equal(sheetClosedSnapshot.playlistSheetOpen, false);
    assert.equal(sheetClosedSnapshot.viewStack.length, 2);
    assert.equal(exitCallCount, 0);

    libraryController.stepBack();
    assert.equal(libraryController.getRenderSnapshot().viewStack.length, 1);
    assert.equal(exitCallCount, 0);

    libraryController.stepBack();
    assert.equal(exitCallCount, 1);
    assert.equal(libraryController.getRenderSnapshot().viewStack.length, 1);

    performLeftEdgeBackSwipe(libraryController);
    assert.equal(exitCallCount, 2);
  });
});

describe("createLibraryController boundaries and negative guards", () => {
  test("empty Favorites and Playlists show calm empty states and BACK TO LIBRARY plate returns to Hub", () => {
    const libraryController = createSettledLibraryController();

    const emptyFavoritesSnapshot = openHubSectionTile(libraryController, 0);
    assert.equal(emptyFavoritesSnapshot.viewStack.length, 2);
    assert.equal(emptyFavoritesSnapshot.viewStack[1].kind, "empty");
    assert.equal(emptyFavoritesSnapshot.viewStack[1].emptyTitle, "No Favorites Yet");

    tapPoint(libraryController, EMPTY_BACK_BUTTON_X, EMPTY_BACK_BUTTON_Y);
    const backFromFavoritesSnapshot = advanceLibraryController(
      libraryController,
      STEP_DELTA_SECONDS,
      SETTLE_STEP_COUNT,
    );
    assert.equal(backFromFavoritesSnapshot.viewStack.length, 1);
    assert.equal(backFromFavoritesSnapshot.viewStack[0].kind, "hub");

    const emptyPlaylistsSnapshot = openHubSectionTile(libraryController, 1);
    assert.equal(emptyPlaylistsSnapshot.viewStack.length, 2);
    assert.equal(emptyPlaylistsSnapshot.viewStack[1].kind, "empty");
    assert.equal(emptyPlaylistsSnapshot.viewStack[1].emptyTitle, "No Playlists Yet");

    tapPoint(libraryController, EMPTY_BACK_BUTTON_X, EMPTY_BACK_BUTTON_Y);
    const backFromPlaylistsSnapshot = advanceLibraryController(
      libraryController,
      STEP_DELTA_SECONDS,
      SETTLE_STEP_COUNT,
    );
    assert.equal(backFromPlaylistsSnapshot.viewStack.length, 1);
    assert.equal(backFromPlaylistsSnapshot.viewStack[0].kind, "hub");
  });

  test("ignores negative and non-finite step deltas without producing NaN", () => {
    const libraryController = createSettledLibraryController();
    const invalidDeltas = [
      -1,
      -0.05,
      Number.NaN,
      Number.POSITIVE_INFINITY,
      Number.NEGATIVE_INFINITY,
      undefined,
      null,
    ];

    for (const invalidDelta of invalidDeltas) {
      libraryController.step(invalidDelta);
      const snapshot = libraryController.getRenderSnapshot();
      assert.ok(Number.isFinite(snapshot.scrollPosition));
      assert.ok(Number.isFinite(snapshot.vinylLaunchProgress));
      assert.ok(Number.isFinite(snapshot.deleteHoldProgress));
      assert.ok(Number.isFinite(snapshot.deleteCompletedFlash));
    }
  });

  test("long-press opens Playlist Action Sheet, short or moved or cancelled delete hold guards deletion, and >=1.10s hold deletes playlist", () => {
    const persistedPayloads = [];
    const catalogWithPlaylist = buildLibraryTestCatalog({
      playlists: [{ id: "p1", name: "Mix One", trackIds: ["t-a1"] }],
    });
    const libraryController = createSettledLibraryController({
      catalog: catalogWithPlaylist,
      onPersistPlaylists: (updatedPlaylists) => persistedPayloads.push(updatedPlaylists),
    });

    const playlistsSnapshot = openHubSectionTile(libraryController, 1);
    assert.equal(playlistsSnapshot.viewStack[playlistsSnapshot.viewStack.length - 1].kind, "playlists");
    assert.equal(playlistsSnapshot.playlists.length, 1);

    const firstRowLayout = computeLibraryCurvedRowLayout(0, 0);
    libraryController.handlePointerDown(firstRowLayout.centerX, firstRowLayout.centerY);
    libraryController.step(0.50);
    libraryController.handlePointerUp(firstRowLayout.centerX, firstRowLayout.centerY);

    assert.equal(libraryController.getRenderSnapshot().playlistSheetOpen, true);

    libraryController.handlePointerDown(DELETE_BUTTON_X, DELETE_BUTTON_Y);
    libraryController.step(0.60);
    const partialHoldSnapshot = libraryController.getRenderSnapshot();
    assert.ok(
      partialHoldSnapshot.deleteHoldProgress > 0 && partialHoldSnapshot.deleteHoldProgress < 1,
    );
    libraryController.handlePointerUp(DELETE_BUTTON_X, DELETE_BUTTON_Y);

    assert.equal(libraryController.getRenderSnapshot().playlists.length, 1);
    assert.equal(persistedPayloads.length, 0);

    libraryController.step(0.3);
    const decayedHoldSnapshot = libraryController.getRenderSnapshot();
    assert.ok(decayedHoldSnapshot.deleteHoldProgress < partialHoldSnapshot.deleteHoldProgress);
    advanceLibraryController(libraryController, STEP_DELTA_SECONDS, SETTLE_STEP_COUNT);

    libraryController.handlePointerDown(DELETE_BUTTON_X, DELETE_BUTTON_Y);
    libraryController.step(0.60);
    libraryController.handlePointerMove(DELETE_BUTTON_X, DELETE_OUTSIDE_Y);
    libraryController.step(0.60);
    libraryController.handlePointerUp(DELETE_BUTTON_X, DELETE_OUTSIDE_Y);

    assert.equal(libraryController.getRenderSnapshot().playlists.length, 1);
    assert.equal(persistedPayloads.length, 0);
    advanceLibraryController(libraryController, STEP_DELTA_SECONDS, SETTLE_STEP_COUNT);

    libraryController.handlePointerDown(DELETE_BUTTON_X, DELETE_BUTTON_Y);
    libraryController.step(0.60);
    libraryController.handlePointerCancel();
    libraryController.step(0.60);
    assert.equal(libraryController.getRenderSnapshot().playlists.length, 1);
    assert.equal(persistedPayloads.length, 0);
    advanceLibraryController(libraryController, STEP_DELTA_SECONDS, SETTLE_STEP_COUNT);

    libraryController.handlePointerDown(DELETE_BUTTON_X, DELETE_BUTTON_Y);
    libraryController.step(0.60);
    libraryController.step(0.55);
    libraryController.handlePointerUp(DELETE_BUTTON_X, DELETE_BUTTON_Y);

    const deletedSnapshot = libraryController.getRenderSnapshot();
    assert.equal(deletedSnapshot.playlists.length, 0);
    assert.deepEqual(persistedPayloads, [[]]);
    assert.equal(deletedSnapshot.deleteCompletedFlash, 1);
    assert.equal(deletedSnapshot.playlistSheetOpen, false);

    const topView = deletedSnapshot.viewStack[deletedSnapshot.viewStack.length - 1];
    assert.equal(topView.kind, "empty");
    assert.equal(topView.emptyTitle, "No Playlists Left");
  });

  test("setTrackFavorite, setPlaylists, and resetVinylLaunch synchronize counts, catalog, and launch state safely", () => {
    const libraryController = createSettledLibraryController();

    assert.equal(libraryController.getRenderSnapshot().counts.favorites, 0);
    libraryController.setTrackFavorite("t-a1", true);
    const favoritedSnapshot = libraryController.getRenderSnapshot();
    assert.equal(favoritedSnapshot.counts.favorites, 1);
    assert.equal(
      favoritedSnapshot.catalog.allTracks.find((trackEntry) => trackEntry.id === "t-a1").isFavorite,
      true,
    );
    assert.equal(
      favoritedSnapshot.catalog.albums
        .find((albumEntry) => albumEntry.id === "album-a")
        .tracks.find((trackEntry) => trackEntry.id === "t-a1").isFavorite,
      true,
    );

    libraryController.setTrackFavorite("t-a1", false);
    libraryController.setTrackFavorite("nonexistent-track", true);
    libraryController.setTrackFavorite(null, true);
    assert.equal(libraryController.getRenderSnapshot().counts.favorites, 0);

    libraryController.setPlaylists([
      { id: "p-crate", name: "DialOS Crate", trackIds: ["t-a1", "t-m1"] },
      { id: "p-alpha", name: "Amber Mix", trackIds: ["t-c1"] },
    ]);
    const playlistsUpdatedSnapshot = libraryController.getRenderSnapshot();
    assert.equal(playlistsUpdatedSnapshot.counts.playlists, 2);
    assert.deepEqual(
      playlistsUpdatedSnapshot.playlists.map((playlistEntry) => playlistEntry.name),
      ["Amber Mix", "DialOS Crate"],
    );

    libraryController.setPlaylists(null);
    assert.equal(libraryController.getRenderSnapshot().counts.playlists, 0);
    assert.deepEqual(libraryController.getRenderSnapshot().playlists, []);

    openHubSectionTile(libraryController, 3);
    tapPoint(libraryController, CRATE_SLEEVE_TAP_X, CRATE_CENTER_Y);
    advanceLibraryController(libraryController, STEP_DELTA_SECONDS, SETTLE_STEP_COUNT);
    assert.equal(libraryController.getRenderSnapshot().vinylLaunchTarget, 1);
    assert.equal(libraryController.getRenderSnapshot().vinylLaunchProgress, 1);

    libraryController.handlePointerDown(BACK_SWIPE_START_X, BACK_SWIPE_Y);
    assert.equal(libraryController.getRenderSnapshot().pointerActive, true);
    libraryController.resetVinylLaunch();
    const resetSnapshot = libraryController.getRenderSnapshot();
    assert.equal(resetSnapshot.vinylLaunchTarget, 0);
    assert.equal(resetSnapshot.vinylLaunchProgress, 0);
    assert.equal(resetSnapshot.pointerActive, false);
    assert.equal(resetSnapshot.pointerMode, "none");
  });
});
