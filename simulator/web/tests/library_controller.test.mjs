import { describe, test } from "node:test";
import assert from "node:assert/strict";

import { createAudioEngine } from "../core/audio_engine.js";
import {
  LIBRARY_CONSTANTS,
  LIBRARY_HUB_SECTIONS,
  buildAlbumSeekIndex,
  buildAlphabeticalLetterIndex,
  buildCollectionSeekIndex,
  compareAlphabetically,
  computeLibraryCurvedRowLayout,
  computeLibraryHubTileRect,
  computeSortWheelSlots,
  computeWindowedCrateBars,
  createLibraryController,
  resolveLibraryRimSliceIndex,
  resolveSortWheelHoveredMode,
  shortestSliceDistance,
  sortAlbumsByMode,
  sortAlphabeticallyBy,
  sortArtistsByMode,
  sortCollectionByMode,
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
const CRATE_SORT_BUTTON_X = 152;
const CRATE_SORT_BUTTON_Y = 325;
const ARTISTS_SORT_BUTTON_X = 180;
const ARTISTS_SORT_BUTTON_Y = 325;
const CRATE_NOTE_BUTTON_X = 208;
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
 * Drag smoothly along the outer rim in either clockwise or counter-clockwise direction.
 *
 * @param {object} libraryController Library controller instance.
 * @param {number} startAngleRadians Initial angle already pressed or to move from.
 * @param {number} endAngleRadians Target angle to reach.
 */
function dragAlongRim(libraryController, startAngleRadians, endAngleRadians) {
  const direction = Math.sign(endAngleRadians - startAngleRadians);
  if (direction === 0) {
    return;
  }
  let currentAngle = startAngleRadians;
  while (
    direction > 0
      ? currentAngle + RIM_ARC_STEP_RADIANS < endAngleRadians
      : currentAngle - RIM_ARC_STEP_RADIANS > endAngleRadians
  ) {
    currentAngle += direction * RIM_ARC_STEP_RADIANS;
    const rimPoint = computeRimPoint(currentAngle);
    libraryController.handlePointerMove(rimPoint.pointX, rimPoint.pointY);
  }
  const finalRimPoint = computeRimPoint(endAngleRadians);
  libraryController.handlePointerMove(finalRimPoint.pointX, finalRimPoint.pointY);
}

/**
 * Compute shortest unsigned angular distance between two angles in radians.
 *
 * @param {number} firstAngleRadians First angle in radians.
 * @param {number} secondAngleRadians Second angle in radians.
 * @returns {number} Shortest angular distance in [0, Math.PI].
 */
function angularDistanceRadians(firstAngleRadians, secondAngleRadians) {
  return Math.abs(
    Math.atan2(
      Math.sin(firstAngleRadians - secondAngleRadians),
      Math.cos(firstAngleRadians - secondAngleRadians),
    ),
  );
}

/**
 * Drag smoothly clockwise along the outer rim in small angle increments.
 *
 * @param {object} libraryController Library controller instance.
 * @param {number} startAngleRadians Initial angle already pressed or to move from.
 * @param {number} endAngleRadians Target angle to reach clockwise.
 */
function dragClockwiseAlongRim(libraryController, startAngleRadians, endAngleRadians) {
  dragAlongRim(libraryController, startAngleRadians, endAngleRadians);
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
  const currentPairRow = Math.round(libraryController.getRenderSnapshot().scrollPosition ?? 0);
  const detentDelta = targetPairRow - currentPairRow;
  const detentDirection = Math.sign(detentDelta);
  for (let detentStep = 0; detentStep < Math.abs(detentDelta); detentStep += 1) {
    libraryController.stepDetent(detentDirection);
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
  const nextNeighborPull = pullSnapshot.rimSlabPullLevels[(hoveredSliceIndex + 1) % sliceCount];
  const prevNeighborPull =
    pullSnapshot.rimSlabPullLevels[(hoveredSliceIndex - 1 + sliceCount) % sliceCount];
  assert.ok(nextNeighborPull >= 0.20 && nextNeighborPull <= 0.28);
  assert.ok(prevNeighborPull >= 0.20 && prevNeighborPull <= 0.28);
  assert.equal(pullSnapshot.rimSlabPullLevels[(hoveredSliceIndex + 2) % sliceCount], 0);
  assert.equal(
    pullSnapshot.rimSlabPullLevels[(hoveredSliceIndex - 2 + sliceCount) % sliceCount],
    0,
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

    // Tapping outside the 22px floating music-note button radius (e.g. far right) does not open the tracklist.
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

  test("returnFromNowPlaying transitions vinyl back into album crate rotating 20 degrees clockwise when playing and gates spin on active playback", () => {
    const TWENTY_DEG_RADIANS = (20 * Math.PI) / 180;
    const HANDOFF_ANGLE_RADIANS = 1.6;
    const libraryController = createSettledLibraryController();

    openHubSectionTile(libraryController, 3);
    libraryController.setActiveTrackId("t-z1");
    libraryController.returnFromNowPlaying({
      vinylRotationRadians: HANDOFF_ANGLE_RADIANS,
      albumId: "album-z",
      launchOrigin: "album-crate",
      isPlaying: true,
    });

    const returnStart = libraryController.getRenderSnapshot();
    assert.equal(returnStart.vinylLaunchProgress, 1);
    assert.equal(returnStart.vinylLaunchTarget, 0);
    assert.equal(returnStart.vinylLaunchSource, "albums");
    assert.equal(returnStart.vinylLaunchAlbumId, "album-z");
    assert.equal(returnStart.vinylNudgeRadians, 0);
    assert.equal(returnStart.isPlaying, true);
    assert.equal(returnStart.currentView.scrollPosition, 3);
    assert.equal(returnStart.currentView.targetIndex, 3);
    assert.ok(Math.abs(returnStart.vinylRotationRadians - HANDOFF_ANGLE_RADIANS) < FLOAT_TOLERANCE);

    // Midpoint: vinylLaunchProgress = 0.5 and +10 deg clockwise rotation
    libraryController.step(LIBRARY_CONSTANTS.VINYL_LAUNCH_DURATION_SECONDS / 2);
    const returnMid = libraryController.getRenderSnapshot();
    assert.equal(returnMid.vinylLaunchProgress, 0.5);
    assert.equal(returnMid.vinylNudgeRadians, 0);
    assert.ok(
      Math.abs(returnMid.vinylRotationRadians - (HANDOFF_ANGLE_RADIANS + TWENTY_DEG_RADIANS / 2)) <
        1e-4,
    );

    // Full return completion: vinylLaunchProgress = 0 and +20 deg clockwise rotation
    libraryController.step(LIBRARY_CONSTANTS.VINYL_LAUNCH_DURATION_SECONDS / 2);
    const returnEnd = libraryController.getRenderSnapshot();
    assert.equal(returnEnd.vinylLaunchProgress, 0);
    assert.ok(
      Math.abs(returnEnd.vinylRotationRadians - (HANDOFF_ANGLE_RADIANS + TWENTY_DEG_RADIANS)) < 1e-4,
    );

    // Steady angular velocity after return matches the 20-degree transition speed
    libraryController.step(LIBRARY_CONSTANTS.VINYL_LAUNCH_DURATION_SECONDS);
    const postReturn = libraryController.getRenderSnapshot();
    assert.ok(
      Math.abs(postReturn.vinylRotationRadians - (HANDOFF_ANGLE_RADIANS + TWENTY_DEG_RADIANS * 2)) <
        1e-4,
    );

    // Paused boundary: vinylRotationRadians stays stationary when isPlaying is false
    const pausedAngle = postReturn.vinylRotationRadians;
    libraryController.setPlaybackState(false);
    assert.equal(libraryController.getRenderSnapshot().isPlaying, false);
    libraryController.step(LIBRARY_CONSTANTS.VINYL_LAUNCH_DURATION_SECONDS);
    assert.ok(
      Math.abs(libraryController.getRenderSnapshot().vinylRotationRadians - pausedAngle) <
        FLOAT_TOLERANCE,
    );

    // Idle boundary: activeTrackId = null keeps vinyl stationary even if setPlaybackState(true) is called
    libraryController.setActiveTrackId(null);
    libraryController.setPlaybackState(true);
    libraryController.step(LIBRARY_CONSTANTS.VINYL_LAUNCH_DURATION_SECONDS);
    assert.equal(libraryController.getRenderSnapshot().isPlaying, false);
    assert.ok(
      Math.abs(libraryController.getRenderSnapshot().vinylRotationRadians - pausedAngle) <
        FLOAT_TOLERANCE,
    );

    // Fallback / negative guard: invalid rotation and unknown albumId keep state finite
    libraryController.setActiveTrackId("t-a1");
    libraryController.returnFromNowPlaying({
      vinylRotationRadians: Number.NaN,
      albumId: "unknown-album",
      launchOrigin: "album-crate",
      isPlaying: false,
    });
    const fallbackSnapshot = libraryController.getRenderSnapshot();
    assert.ok(Number.isFinite(fallbackSnapshot.vinylRotationRadians));
    assert.equal(fallbackSnapshot.vinylLaunchProgress, 1);
    assert.equal(fallbackSnapshot.vinylLaunchTarget, 0);
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

  test("Hub Albums tile triggers 0.52s shared-element transition forward and mirrors back while preserving Hub scroll position", () => {
    assert.equal(LIBRARY_CONSTANTS.ALBUMS_TRANSITION_DURATION_SECONDS, 0.52);

    const libraryController = createLibraryController();
    libraryController.hydrateCatalog(buildLibraryTestCatalog());

    /* enterFromHome with skipEntryTransition starts settled at transitionProgress 1 */
    libraryController.enterFromHome({ skipEntryTransition: true });
    const settledEntrySnapshot = libraryController.getRenderSnapshot();
    assert.equal(settledEntrySnapshot.transitionProgress, 1);
    assert.equal(settledEntrySnapshot.albumsTransitionMode, "none");

    /* 1. Tap Albums tile while scrolled to row 1 (focused) */
    libraryController.stepDetent(1);
    advanceLibraryController(libraryController, STEP_DELTA_SECONDS, SETTLE_STEP_COUNT);
    const focusedAlbumsRect = computeLibraryHubTileRect(3, 1);
    tapPoint(libraryController, focusedAlbumsRect.centerX, focusedAlbumsRect.centerY);

    const forwardStart = libraryController.getRenderSnapshot();
    assert.equal(forwardStart.currentView.kind, "albums");
    assert.equal(forwardStart.albumsTransitionMode, "hub-to-albums");
    assert.equal(forwardStart.albumsTransitionProgress, 0);
    assert.equal(forwardStart.albumsTransitionHubScroll, 1);
    assert.equal(forwardStart.albumsTransitionAlbumId, "album-a");
    assert.equal(forwardStart.transitionProgress, 1);

    libraryController.step(LIBRARY_CONSTANTS.ALBUMS_TRANSITION_DURATION_SECONDS / 2);
    const forwardMid = libraryController.getRenderSnapshot();
    assert.equal(forwardMid.albumsTransitionMode, "hub-to-albums");
    assert.ok(forwardMid.albumsTransitionProgress > 0.45 && forwardMid.albumsTransitionProgress < 0.55);

    libraryController.step(LIBRARY_CONSTANTS.ALBUMS_TRANSITION_DURATION_SECONDS / 2);
    const forwardEnd = libraryController.getRenderSnapshot();
    assert.equal(forwardEnd.albumsTransitionMode, "none");
    assert.equal(forwardEnd.albumsTransitionProgress, 1);

    /* Scroll to album index 2 ("Midnight Suite") then step back to Hub */
    libraryController.stepDetent(1);
    libraryController.stepDetent(1);
    advanceLibraryController(libraryController, STEP_DELTA_SECONDS, SETTLE_STEP_COUNT);
    libraryController.stepBack();

    const reverseStart = libraryController.getRenderSnapshot();
    assert.equal(reverseStart.currentView.kind, "hub");
    assert.equal(reverseStart.scrollPosition, 1, "returning from Albums must preserve Hub scroll row");
    assert.equal(reverseStart.albumsTransitionMode, "albums-to-hub");
    assert.equal(reverseStart.albumsTransitionProgress, 0);
    assert.equal(reverseStart.albumsTransitionHubScroll, 1);
    assert.equal(reverseStart.albumsTransitionAlbumId, "album-m");
    assert.equal(reverseStart.albumsTransitionCrateScroll, 2);

    libraryController.step(LIBRARY_CONSTANTS.ALBUMS_TRANSITION_DURATION_SECONDS);
    const reverseEnd = libraryController.getRenderSnapshot();
    assert.equal(reverseEnd.albumsTransitionMode, "none");
    assert.equal(reverseEnd.albumsTransitionProgress, 1);
    assert.equal(reverseEnd.scrollPosition, 1);

    /* 2. Peeking Albums tile at row 0 captures hubScroll 0 */
    libraryController.stepDetent(-1);
    advanceLibraryController(libraryController, STEP_DELTA_SECONDS, SETTLE_STEP_COUNT);
    const peekingAlbumsRect = computeLibraryHubTileRect(3, 0);
    tapPoint(libraryController, peekingAlbumsRect.centerX, peekingAlbumsRect.centerY);
    const peekingForward = libraryController.getRenderSnapshot();
    assert.equal(peekingForward.albumsTransitionMode, "hub-to-albums");
    assert.equal(peekingForward.albumsTransitionHubScroll, 0);
    libraryController.step(LIBRARY_CONSTANTS.ALBUMS_TRANSITION_DURATION_SECONDS);
    libraryController.stepBack();
    assert.equal(libraryController.getRenderSnapshot().albumsTransitionMode, "albums-to-hub");
    assert.equal(libraryController.getRenderSnapshot().albumsTransitionHubScroll, 0);
    libraryController.step(LIBRARY_CONSTANTS.ALBUMS_TRANSITION_DURATION_SECONDS);

    /* 3. Artists -> Artist Albums must NOT trigger Hub Albums shared-element transition */
    openHubSectionTile(libraryController, 2);
    libraryController.activateFocused(0);
    const artistAlbumsSnapshot = libraryController.getRenderSnapshot();
    assert.equal(artistAlbumsSnapshot.currentView.kind, "albums");
    assert.equal(artistAlbumsSnapshot.albumsTransitionMode, "none");

    /* 4. Empty Albums catalog pushes empty state without albumsTransitionMode */
    const emptyController = createLibraryController();
    emptyController.hydrateCatalog({ albums: [], artists: [], genres: [], allTracks: [], playlists: [] });
    emptyController.enterFromHome({ skipEntryTransition: true });
    tapPoint(emptyController, peekingAlbumsRect.centerX, peekingAlbumsRect.centerY);
    const emptySnapshot = emptyController.getRenderSnapshot();
    assert.equal(emptySnapshot.currentView.kind, "empty");
    assert.equal(emptySnapshot.albumsTransitionMode, "none");
  });

  test("preserves exact playlist order without Letter Seek, filters genres per-track, and passes shuffleOverride from Playlist Action Sheet", () => {
    const launchPayloads = [];
    const mixedCatalog = buildLibraryTestCatalog({
      playlists: [
        {
          id: "pl-ordered",
          name: "Ordered Crate",
          trackIds: ["t-z1", "t-a1", "t-m1", "t-c1"],
        },
      ],
    });
    // Tag individual tracks across albums to verify strict per-track genres filtering
    mixedCatalog.allTracks.forEach((trackEntry) => {
      if (trackEntry.id === "t-z1" || trackEntry.id === "t-a2") {
        trackEntry.genres = ["Electronic", "House"];
      } else {
        trackEntry.genres = [trackEntry.genre];
      }
    });

    const libraryController = createSettledLibraryController({
      catalog: mixedCatalog,
      onLaunchTrack: (sourceSpec) => launchPayloads.push(sourceSpec),
    });

    // 1. Playlists view -> playlist-tracks preserves custom playlist.trackIds order and omits Letter Seek
    openHubSectionTile(libraryController, 1);
    const playlistRowLayout = computeLibraryCurvedRowLayout(0, 0);
    tapPoint(libraryController, playlistRowLayout.centerX, playlistRowLayout.centerY);
    const playlistTracksSnapshot = libraryController.getRenderSnapshot();
    assert.equal(playlistTracksSnapshot.currentView.kind, "tracks");
    assert.equal(playlistTracksSnapshot.currentView.sourceType, "playlist");
    assert.deepEqual(
      playlistTracksSnapshot.currentRows.map((rowEntry) => rowEntry.id),
      ["t-z1", "t-a1", "t-m1", "t-c1"],
    );
    assert.deepEqual(playlistTracksSnapshot.letterEntries, []);

    // Step back to Playlists view and long-press to open Playlist Action Sheet
    libraryController.stepBack();
    libraryController.handlePointerDown(playlistRowLayout.centerX, playlistRowLayout.centerY);
    libraryController.step(0.50);
    libraryController.handlePointerUp(playlistRowLayout.centerX, playlistRowLayout.centerY);
    assert.equal(libraryController.getRenderSnapshot().playlistSheetOpen, true);

    // Tapping PLAY button (center y=165) launches trackIds[0] with shuffleOverride: false
    tapPoint(libraryController, DISPLAY_CENTER_PX, 165);
    assert.equal(launchPayloads.length, 1);
    assert.equal(launchPayloads[0].queueSource, "playlist");
    assert.equal(launchPayloads[0].playlistId, "pl-ordered");
    assert.equal(launchPayloads[0].trackId, "t-z1");
    assert.equal(launchPayloads[0].shuffleOverride, false);

    // Return to playlists, reopen sheet, and tap SHUFFLE MIX button (center y=207)
    libraryController.resetVinylLaunch();
    libraryController.handlePointerDown(playlistRowLayout.centerX, playlistRowLayout.centerY);
    libraryController.step(0.50);
    libraryController.handlePointerUp(playlistRowLayout.centerX, playlistRowLayout.centerY);
    tapPoint(libraryController, DISPLAY_CENTER_PX, 207);
    assert.equal(launchPayloads.length, 2);
    assert.equal(launchPayloads[1].queueSource, "playlist");
    assert.equal(launchPayloads[1].playlistId, "pl-ordered");
    assert.equal(launchPayloads[1].shuffleOverride, true);

    // Return to Hub and inspect Genres filtering on strict per-track `genres`
    libraryController.resetVinylLaunch();
    libraryController.stepBack();
    const genresSnapshot = openHubSectionTile(libraryController, 4);
    const synthRowIndex = genresSnapshot.currentRows.findIndex(
      (rowEntry) => rowEntry.title.toLowerCase() === "synth",
    );
    assert.ok(synthRowIndex >= 0);
    libraryController.activateFocused(synthRowIndex);
    const synthTracksSnapshot = libraryController.getRenderSnapshot();
    assert.equal(synthTracksSnapshot.currentView.sourceType, "genre");
    // Only album-z's t-z2 remains tagged "Synth"; t-z1 was retagged ["Electronic", "House"]
    assert.deepEqual(
      synthTracksSnapshot.currentRows.map((rowEntry) => rowEntry.id),
      ["t-z2"],
    );
  });

  test("Artist header segmented toggle switches Albums <-> Songs in-place, remembers session tab, preserves scroll, supports Letter Seek, and launches separate album vs artist contexts", () => {
    const launchedPayloads = [];
    const multiAlbumCatalog = buildLibraryTestCatalog();
    // Give Amber Duo a second album ("Amber Encore") with 2 tracks ("Delta Echo", "Beta Sunrise") so Amber Duo has 4 tracks across 2 albums
    multiAlbumCatalog.albums.push({
      id: "album-a2",
      title: "Amber Encore",
      artist: "Amber Duo",
      genre: "Acoustic",
      motif: "prism",
      tracks: [
        {
          id: "t-a3",
          title: "Delta Echo",
          artist: "Amber Duo feat. Guest",
          albumId: "album-a2",
          albumTitle: "Amber Encore",
          duration: "3:15",
          durationSeconds: 195,
          isFavorite: false,
          trackNumber: "01",
        },
        {
          id: "t-a4",
          title: "Beta Sunrise",
          artist: "Amber Duo",
          albumId: "album-a2",
          albumTitle: "Amber Encore",
          duration: "2:50",
          durationSeconds: 170,
          isFavorite: false,
          trackNumber: "02",
        },
      ],
    });
    multiAlbumCatalog.allTracks.push(
      ...multiAlbumCatalog.albums[multiAlbumCatalog.albums.length - 1].tracks,
    );
    const amberArtistIndex = multiAlbumCatalog.artists.findIndex(
      (artistEntry) => artistEntry.id === "artist-a",
    );
    multiAlbumCatalog.artists[amberArtistIndex] = {
      ...multiAlbumCatalog.artists[amberArtistIndex],
      countLabel: "2",
      albumIds: ["album-a", "album-a2"],
    };

    const libraryController = createSettledLibraryController({
      catalog: multiAlbumCatalog,
      onLaunchTrack: (sourceSpec) => launchedPayloads.push(sourceSpec),
    });

    // Fresh session opens Amber Duo in Albums mode
    openHubSectionTile(libraryController, 2);
    const firstArtistRow = computeLibraryCurvedRowLayout(0, 0);
    tapPoint(libraryController, firstArtistRow.centerX, firstArtistRow.centerY);
    advanceLibraryController(libraryController, STEP_DELTA_SECONDS, SETTLE_STEP_COUNT);

    const initialArtist = libraryController.getRenderSnapshot();
    assert.equal(initialArtist.viewStack.length, 3);
    assert.equal(initialArtist.currentView.isArtistView, true);
    assert.equal(initialArtist.currentView.artistViewMode, "albums");
    assert.equal(initialArtist.currentView.kind, "albums");
    assert.equal(initialArtist.artistModeBlend, 0);

    // Scroll Albums crate to index 1, then tap right half of header segmented toggle (x=216, y=63)
    libraryController.stepDetent(1);
    advanceLibraryController(libraryController, STEP_DELTA_SECONDS, SETTLE_STEP_COUNT);
    assert.equal(libraryController.getRenderSnapshot().scrollPosition, 1);

    tapPoint(libraryController, 216, 63);
    advanceLibraryController(libraryController, STEP_DELTA_SECONDS, SETTLE_STEP_COUNT);

    const songsSnapshot = libraryController.getRenderSnapshot();
    assert.equal(songsSnapshot.viewStack.length, 3, "toggling Songs must switch in-place");
    assert.equal(songsSnapshot.currentView.isArtistView, true);
    assert.equal(songsSnapshot.currentView.artistViewMode, "songs");
    assert.equal(songsSnapshot.currentView.kind, "tracks");
    assert.equal(songsSnapshot.currentView.sourceType, "artist");
    assert.equal(songsSnapshot.artistModeBlend, 1);
    assert.equal(songsSnapshot.preferredArtistViewMode, "songs");
    assert.equal(songsSnapshot.currentRows.length, 4);
    assert.equal(songsSnapshot.currentRows[0].showAlbumSubtitle, true);
    // Alphabetical A-Z order across Amber Duo's 2 albums:
    // "Amber Sunrise" (t-a1), "Beta Sunrise" (t-a4), "Daybreak Pulse" (t-a2), "Delta Echo" (t-a3)
    assert.deepEqual(
      songsSnapshot.currentRows.map((rowEntry) => rowEntry.id),
      ["t-a1", "t-a4", "t-a2", "t-a3"],
    );
    // >= 3 tracks across >= 2 letters enables Letter Seek
    assert.deepEqual(
      songsSnapshot.letterEntries.map((entry) => entry.letter),
      ["A", "B", "D"],
    );

    // Scroll Songs to index 2, tap a song -> launches with queueSource: "artist"
    libraryController.stepDetent(1);
    libraryController.stepDetent(1);
    advanceLibraryController(libraryController, STEP_DELTA_SECONDS, SETTLE_STEP_COUNT);
    assert.equal(libraryController.getRenderSnapshot().scrollPosition, 2);

    const firstVisibleRow = computeLibraryCurvedRowLayout(2, 2);
    tapPoint(libraryController, firstVisibleRow.centerX, firstVisibleRow.centerY);
    assert.equal(launchedPayloads.length, 1);
    assert.deepEqual(launchedPayloads[0], {
      trackId: "t-a2",
      queueSource: "artist",
      artistName: "Amber Duo",
      artistAlbumIds: ["album-a", "album-a2"],
      albumId: "album-a",
    });
    libraryController.resetVinylLaunch();

    // Dragging across header toggle (> TAP_DRIFT_LIMIT_PX) must NOT switch tabs
    libraryController.handlePointerDown(144, 63);
    libraryController.handlePointerMove(164, 63);
    libraryController.handlePointerUp(164, 63);
    assert.equal(libraryController.getRenderSnapshot().currentView.artistViewMode, "songs");

    // Tap left half of header toggle (x=144, y=63) -> switches back to Albums in-place and restores Albums scroll (1)
    tapPoint(libraryController, 144, 63);
    advanceLibraryController(libraryController, STEP_DELTA_SECONDS, SETTLE_STEP_COUNT);
    const backToAlbums = libraryController.getRenderSnapshot();
    assert.equal(backToAlbums.currentView.artistViewMode, "albums");
    assert.equal(backToAlbums.currentView.kind, "albums");
    assert.equal(backToAlbums.scrollPosition, 1);

    // Opening album tracklist overlay pushes viewStack[3] and stepBack pops back to Artist Albums
    tapPoint(libraryController, CRATE_NOTE_BUTTON_X, CRATE_NOTE_BUTTON_Y);
    advanceLibraryController(libraryController, STEP_DELTA_SECONDS, SETTLE_STEP_COUNT);
    assert.equal(libraryController.getRenderSnapshot().viewStack.length, 4);
    assert.equal(libraryController.getRenderSnapshot().currentView.sourceType, "album");
    libraryController.stepBack();
    advanceLibraryController(libraryController, STEP_DELTA_SECONDS, SETTLE_STEP_COUNT);
    assert.equal(libraryController.getRenderSnapshot().viewStack.length, 3);
    assert.equal(libraryController.getRenderSnapshot().currentView.artistViewMode, "albums");

    // Switch back to Songs, then swipe back (stepBack) -> returns directly to Artists list in 1 step
    tapPoint(libraryController, 216, 63);
    advanceLibraryController(libraryController, STEP_DELTA_SECONDS, SETTLE_STEP_COUNT);
    libraryController.stepBack();
    advanceLibraryController(libraryController, STEP_DELTA_SECONDS, SETTLE_STEP_COUNT);
    assert.equal(libraryController.getRenderSnapshot().viewStack.length, 2);
    assert.equal(libraryController.getRenderSnapshot().currentView.kind, "artists");

    // Opening a different (1-album) artist remembers "songs" preference for the session
    const secondArtistRow = computeLibraryCurvedRowLayout(1, 0);
    tapPoint(libraryController, secondArtistRow.centerX, secondArtistRow.centerY);
    advanceLibraryController(libraryController, STEP_DELTA_SECONDS, SETTLE_STEP_COUNT);
    const secondArtistSnapshot = libraryController.getRenderSnapshot();
    assert.equal(secondArtistSnapshot.currentView.isArtistView, true);
    assert.equal(secondArtistSnapshot.currentView.artistViewMode, "songs");
    assert.equal(secondArtistSnapshot.currentView.kind, "tracks");
    assert.equal(secondArtistSnapshot.currentRows.length, 1);

    // Negative guards: invalid mode or non-artist view setArtistViewMode is a no-op
    libraryController.setArtistViewMode("invalid");
    assert.equal(libraryController.getRenderSnapshot().currentView.artistViewMode, "songs");
    libraryController.stepBack();
    libraryController.setArtistViewMode("albums");
    assert.equal(libraryController.getRenderSnapshot().currentView.kind, "artists");
  });

  test("sortAlbumsByMode, sortArtistsByMode, and sortCollectionByMode order by all 5 modes without mutation", () => {
    const fixtureAlbums = [
      { id: "a3", title: "Zebra", isFavorite: false, addedAt: 300 },
      { id: "a1", title: "Alpha", isFavorite: true, addedAt: 100 },
      { id: "a2", title: "Beta", isFavorite: true, addedAt: 200 },
    ];
    const snapshotBefore = fixtureAlbums.map((entry) => entry.id);

    assert.deepEqual(
      sortAlbumsByMode(fixtureAlbums, "alphabetical").map((entry) => entry.id),
      ["a1", "a2", "a3"],
    );
    assert.deepEqual(
      sortAlbumsByMode(fixtureAlbums, "reverse-alphabetical").map((entry) => entry.id),
      ["a3", "a2", "a1"],
    );
    assert.deepEqual(
      sortAlbumsByMode(fixtureAlbums, "liked-first").map((entry) => entry.id),
      ["a1", "a2", "a3"],
    );
    assert.deepEqual(
      sortAlbumsByMode(fixtureAlbums, "recent").map((entry) => entry.id),
      ["a3", "a2", "a1"],
    );
    assert.deepEqual(
      sortAlbumsByMode(fixtureAlbums, "oldest").map((entry) => entry.id),
      ["a1", "a2", "a3"],
    );
    assert.deepEqual(
      fixtureAlbums.map((entry) => entry.id),
      snapshotBefore,
    );
    assert.deepEqual(sortAlbumsByMode(null, "recent"), []);

    const fixtureArtists = [
      { id: "ar-z", name: "Zeta", isFavorite: false, addedAt: 300 },
      { id: "ar-b", name: "Beta", isFavorite: true, addedAt: 100 },
      { id: "ar-a", name: "Alpha", isFavorite: false, addedAt: 200 },
      { id: "ar-c", name: "Cobalt", isFavorite: true, addedAt: 200 },
    ];
    assert.deepEqual(
      sortArtistsByMode(fixtureArtists, "alphabetical").map((entry) => entry.id),
      ["ar-a", "ar-b", "ar-c", "ar-z"],
    );
    assert.deepEqual(
      sortArtistsByMode(fixtureArtists, "reverse-alphabetical").map((entry) => entry.id),
      ["ar-z", "ar-c", "ar-b", "ar-a"],
    );
    assert.deepEqual(
      sortArtistsByMode(fixtureArtists, "liked-first").map((entry) => entry.id),
      ["ar-b", "ar-c", "ar-a", "ar-z"],
    );
    assert.deepEqual(
      sortArtistsByMode(fixtureArtists, "recent").map((entry) => entry.id),
      ["ar-z", "ar-a", "ar-c", "ar-b"],
    );
    assert.deepEqual(
      sortArtistsByMode(fixtureArtists, "oldest").map((entry) => entry.id),
      ["ar-b", "ar-a", "ar-c", "ar-z"],
    );
    assert.deepEqual(
      sortCollectionByMode(fixtureArtists, "unknown-mode", (entry) => entry.name).map(
        (entry) => entry.id,
      ),
      ["ar-a", "ar-b", "ar-c", "ar-z"],
    );
    assert.deepEqual(sortArtistsByMode(null, "liked-first"), []);
  });

  test("bottom Sort button cycles all 5 modes, jumps to index 0, persists across Views, and gates Letter Seek", () => {
    const libraryController = createLibraryController();
    libraryController.hydrateCatalog({
      albums: [
        {
          id: "album-old",
          title: "Alpha",
          artist: "Artist A",
          addedAt: 100,
          isFavorite: false,
          tracks: [{ id: "t1", title: "One" }],
        },
        {
          id: "album-mid",
          title: "Beta",
          artist: "Artist A",
          addedAt: 200,
          isFavorite: true,
          tracks: [{ id: "t2", title: "Two" }],
        },
        {
          id: "album-new",
          title: "Zebra",
          artist: "Artist B",
          addedAt: 300,
          isFavorite: false,
          tracks: [{ id: "t3", title: "Three" }],
        },
      ],
      artists: [
        { id: "art-a", name: "Artist A", albumIds: ["album-old", "album-mid"] },
      ],
      genres: [],
      playlists: [],
    });
    libraryController.enterFromHome({ skipEntryTransition: true });

    // Open Library / Albums
    openHubSectionTile(libraryController, 3);
    assert.equal(libraryController.getRenderSnapshot().albumSortMode, "alphabetical");
    assert.ok(libraryController.getRenderSnapshot().letterEntries.length >= 2);

    // Move focus to index 2 (Zebra)
    libraryController.stepDetent(1);
    libraryController.stepDetent(1);
    advanceLibraryController(libraryController, STEP_DELTA_SECONDS, SETTLE_STEP_COUNT);
    assert.equal(Math.round(libraryController.getRenderSnapshot().scrollPosition), 2);

    // Tap Sort button -> reverse-alphabetical (Z-A), jumps to index 0 (Zebra)
    tapPoint(libraryController, CRATE_SORT_BUTTON_X, CRATE_SORT_BUTTON_Y);
    const reverseState = libraryController.getRenderSnapshot();
    assert.equal(reverseState.albumSortMode, "reverse-alphabetical");
    assert.equal(reverseState.sortToastLabel, "Z\u2013A");
    assert.equal(reverseState.scrollPosition, 0);
    assert.equal(reverseState.currentCrateAlbums[0].id, "album-new");
    assert.deepEqual(
      reverseState.letterEntries.map((entry) => entry.letter),
      ["Z", "B", "A"],
    );

    // Tap Sort button -> liked-first, jumps to index 0 (Beta) and provides liked-first seek anchors
    tapPoint(libraryController, CRATE_SORT_BUTTON_X, CRATE_SORT_BUTTON_Y);
    const likedFirstState = libraryController.getRenderSnapshot();
    assert.equal(likedFirstState.albumSortMode, "liked-first");
    assert.equal(likedFirstState.sortToastLabel, "LIKED FIRST");
    assert.equal(likedFirstState.scrollPosition, 0);
    assert.equal(likedFirstState.currentCrateAlbums[0].id, "album-mid");
    assert.deepEqual(
      likedFirstState.letterEntries.map((entry) => ({
        letter: entry.letter,
        isFavorite: entry.isFavorite,
        itemIndex: entry.itemIndex,
      })),
      [
        { letter: "B", isFavorite: true, itemIndex: 0 },
        { letter: "A", isFavorite: false, itemIndex: 1 },
        { letter: "Z", isFavorite: false, itemIndex: 2 },
      ],
    );

    // Tap Sort button -> recent (Zebra first); all timestamps share the same day so Date Seek collapses to []
    tapPoint(libraryController, CRATE_SORT_BUTTON_X, CRATE_SORT_BUTTON_Y);
    const recentState = libraryController.getRenderSnapshot();
    assert.equal(recentState.albumSortMode, "recent");
    assert.equal(recentState.sortToastLabel, "MOST RECENT");
    assert.equal(recentState.currentCrateAlbums[0].id, "album-new");
    assert.deepEqual(recentState.letterEntries, []);

    // Tap Sort button -> oldest (Alpha first)
    tapPoint(libraryController, CRATE_SORT_BUTTON_X, CRATE_SORT_BUTTON_Y);
    const oldestState = libraryController.getRenderSnapshot();
    assert.equal(oldestState.albumSortMode, "oldest");
    assert.equal(oldestState.sortToastLabel, "OLDEST FIRST");
    assert.equal(oldestState.currentCrateAlbums[0].id, "album-old");

    // Step back to Hub and open Artists -> Artist A crate: sort mode ("oldest") persists
    libraryController.stepBack();
    advanceLibraryController(libraryController, STEP_DELTA_SECONDS, SETTLE_STEP_COUNT);
    openHubSectionTile(libraryController, 2);
    const artistRow = computeLibraryCurvedRowLayout(0, 0);
    tapPoint(libraryController, artistRow.centerX, artistRow.centerY);
    advanceLibraryController(libraryController, STEP_DELTA_SECONDS, SETTLE_STEP_COUNT);
    const artistCrate = libraryController.getRenderSnapshot();
    assert.equal(artistCrate.albumSortMode, "oldest");
    assert.equal(artistCrate.currentCrateAlbums[0].id, "album-old");

    // Tap Sort button once more -> wraps back to alphabetical
    tapPoint(libraryController, CRATE_SORT_BUTTON_X, CRATE_SORT_BUTTON_Y);
    assert.equal(libraryController.getRenderSnapshot().albumSortMode, "alphabetical");
  });

  test("long-tapping an album sleeve opens Quick Action menu, toggles album favorite independently, and auto-closes", () => {
    const toggledCalls = [];
    const libraryController = createLibraryController({
      onToggleAlbumFavorite: (albumId, nextState) => {
        toggledCalls.push({ albumId, nextState });
      },
    });
    libraryController.hydrateCatalog(buildLibraryTestCatalog());
    libraryController.enterFromHome({ skipEntryTransition: true });
    openHubSectionTile(libraryController, 3);

    // Drift > TAP_DRIFT_LIMIT_PX cancels long-press
    libraryController.handlePointerDown(CRATE_SLEEVE_TAP_X, CRATE_CENTER_Y);
    libraryController.handlePointerMove(CRATE_SLEEVE_TAP_X + 18, CRATE_CENTER_Y);
    advanceLibraryController(libraryController, 0.1, 6);
    assert.equal(libraryController.getRenderSnapshot().albumQuickActionsOpen, false);
    libraryController.handlePointerUp(CRATE_SLEEVE_TAP_X + 18, CRATE_CENTER_Y);

    // Long-press on focused album sleeve (>= 0.48s) opens Album Quick Actions
    libraryController.handlePointerDown(CRATE_SLEEVE_TAP_X, CRATE_CENTER_Y);
    advanceLibraryController(libraryController, 0.1, 6);
    const openSnapshot = libraryController.getRenderSnapshot();
    assert.equal(openSnapshot.albumQuickActionsOpen, true);
    assert.equal(openSnapshot.albumQuickActionsAlbumId, "album-a");
    libraryController.handlePointerUp(CRATE_SLEEVE_TAP_X, CRATE_CENTER_Y);

    // Tap Heart medallion (180, 196) -> likes album-a without liking its tracks and auto-closes after ~0.24s
    tapPoint(libraryController, 180, LIBRARY_CONSTANTS.ALBUM_QUICK_ACTION_BUTTON_CENTER_Y);
    const afterLike = libraryController.getRenderSnapshot();
    assert.deepEqual(toggledCalls, [{ albumId: "album-a", nextState: true }]);
    assert.equal(afterLike.counts.favoriteAlbums, 1);
    assert.equal(afterLike.counts.favoriteTracks, 0);
    assert.equal(afterLike.counts.favorites, 1);
    assert.equal(
      afterLike.catalog.albums.find((entry) => entry.id === "album-a").isFavorite,
      true,
    );
    assert.equal(
      afterLike.catalog.allTracks
        .filter((trackEntry) => trackEntry.albumId === "album-a")
        .some((trackEntry) => trackEntry.isFavorite),
      false,
    );

    advanceLibraryController(libraryController, 0.05, 6);
    assert.equal(libraryController.getRenderSnapshot().albumQuickActionsOpen, false);

    // Liking a track inside album-a keeps album-a liked independently; unliking album-a keeps track liked
    libraryController.setTrackFavorite("t-a1", true);
    assert.equal(libraryController.getRenderSnapshot().counts.favoriteAlbums, 1);
    assert.equal(libraryController.getRenderSnapshot().counts.favoriteTracks, 1);
    assert.equal(libraryController.getRenderSnapshot().counts.favorites, 2);

    libraryController.setAlbumFavorite("album-a", false);
    const afterAlbumUnlike = libraryController.getRenderSnapshot();
    assert.equal(afterAlbumUnlike.counts.favoriteAlbums, 0);
    assert.equal(afterAlbumUnlike.counts.favoriteTracks, 1);
    assert.equal(afterAlbumUnlike.counts.favorites, 1);
    assert.equal(
      afterAlbumUnlike.catalog.allTracks.find((entry) => entry.id === "t-a1").isFavorite,
      true,
    );
  });

  test("Library / Favorites uses ALBUMS | SONGS toggle, defaults to ALBUMS, and keeps tabs independent", () => {
    const libraryController = createSettledLibraryController();

    // Only album-c is liked initially -> Favorites opens on ALBUMS with 1 album and inline empty SONGS
    libraryController.setAlbumFavorite("album-c", true);
    const favAlbumsSnapshot = openHubSectionTile(libraryController, 0);
    assert.equal(favAlbumsSnapshot.currentView.isFavoritesView, true);
    assert.equal(favAlbumsSnapshot.currentView.favoritesViewMode, "albums");
    assert.equal(favAlbumsSnapshot.currentView.kind, "albums");
    assert.equal(favAlbumsSnapshot.currentCrateAlbums.length, 1);
    assert.equal(favAlbumsSnapshot.currentCrateAlbums[0].id, "album-c");

    // Switch to SONGS via right-half header toggle tap -> shows 0 liked tracks cleanly
    tapPoint(libraryController, 216, 63);
    advanceLibraryController(libraryController, STEP_DELTA_SECONDS, SETTLE_STEP_COUNT);
    const favSongsEmpty = libraryController.getRenderSnapshot();
    assert.equal(favSongsEmpty.currentView.favoritesViewMode, "songs");
    assert.equal(favSongsEmpty.currentView.kind, "tracks");
    assert.equal(favSongsEmpty.currentRows.length, 0);

    // Like a track in album-a -> Favorites/Songs populates immediately; album-c stays in Favorites/Albums
    libraryController.setTrackFavorite("t-a1", true);
    const favSongsPopulated = libraryController.getRenderSnapshot();
    assert.equal(favSongsPopulated.currentRows.length, 1);
    assert.equal(favSongsPopulated.currentRows[0].id, "t-a1");
    assert.equal(favSongsPopulated.counts.favoriteAlbums, 1);
    assert.equal(favSongsPopulated.counts.favoriteTracks, 1);

    // Switch back to ALBUMS -> unlike album-c via Quick Action -> crate becomes empty inline
    tapPoint(libraryController, 144, 63);
    advanceLibraryController(libraryController, STEP_DELTA_SECONDS, SETTLE_STEP_COUNT);
    libraryController.openAlbumQuickActions("album-c");
    tapPoint(libraryController, 180, LIBRARY_CONSTANTS.ALBUM_QUICK_ACTION_BUTTON_CENTER_Y);
    advanceLibraryController(libraryController, 0.05, 6);
    const afterUnlikeInFav = libraryController.getRenderSnapshot();
    assert.equal(afterUnlikeInFav.currentView.isFavoritesView, true);
    assert.equal(afterUnlikeInFav.currentView.favoritesViewMode, "albums");
    assert.equal(afterUnlikeInFav.currentCrateAlbums.length, 0);
    assert.equal(afterUnlikeInFav.counts.favoriteTracks, 1);
  });

  test("computeSortWheelSlots and resolveSortWheelHoveredMode map 5 upper arc slots and cancel zones cleanly for both Albums and Artists anchors", () => {
    for (const [anchorX, anchorY] of [
      [CRATE_SORT_BUTTON_X, CRATE_SORT_BUTTON_Y],
      [ARTISTS_SORT_BUTTON_X, ARTISTS_SORT_BUTTON_Y],
    ]) {
      const slots = computeSortWheelSlots(anchorX, anchorY);
      assert.equal(slots.length, 5);
      assert.deepEqual(
        slots.map((slot) => slot.mode),
        ["alphabetical", "reverse-alphabetical", "liked-first", "recent", "oldest"],
      );

      for (const slot of slots) {
        const distanceFromDisplayCenter = Math.hypot(
          slot.centerX - DISPLAY_CENTER_PX,
          slot.centerY - DISPLAY_CENTER_PX,
        );
        assert.ok(
          distanceFromDisplayCenter + slot.radius <= DISPLAY_CENTER_PX,
          `slot ${slot.mode} at (${anchorX}, ${anchorY}) must stay inside 360x360 circle`,
        );
        assert.equal(
          resolveSortWheelHoveredMode(slot.centerX, slot.centerY, anchorX, anchorY),
          slot.mode,
        );
      }

      // Center cancel hub and downward drag return null
      assert.equal(resolveSortWheelHoveredMode(anchorX, anchorY, anchorX, anchorY), null);
      assert.equal(resolveSortWheelHoveredMode(anchorX, anchorY + 32, anchorX, anchorY), null);
    }
    assert.equal(resolveSortWheelHoveredMode(Number.NaN, 250), null);
  });

  test("long-pressing Sort button opens radial half-wheel and drag-release commits selected mode without lifting", () => {
    const libraryController = createLibraryController();
    libraryController.hydrateCatalog(buildLibraryTestCatalog());
    libraryController.enterFromHome({ skipEntryTransition: true });
    openHubSectionTile(libraryController, 3);

    // Long-press on Sort button (152, 325) opens sortWheelOpen with neutral hover
    libraryController.handlePointerDown(CRATE_SORT_BUTTON_X, CRATE_SORT_BUTTON_Y);
    advanceLibraryController(libraryController, 0.05, 8);
    const wheelOpenState = libraryController.getRenderSnapshot();
    assert.equal(wheelOpenState.sortWheelOpen, true);
    assert.equal(wheelOpenState.sortWheelHoveredMode, null);

    // Drag without lifting onto "oldest" slot -> updates sortWheelHoveredMode live
    const slots = computeSortWheelSlots();
    const oldestSlot = slots.find((slot) => slot.mode === "oldest");
    const recentSlot = slots.find((slot) => slot.mode === "recent");
    libraryController.handlePointerMove(oldestSlot.centerX, oldestSlot.centerY);
    assert.equal(libraryController.getRenderSnapshot().sortWheelHoveredMode, "oldest");

    // Drag to "recent" slot and release -> commits "recent", jumps to index 0, closes wheel
    libraryController.handlePointerMove(recentSlot.centerX, recentSlot.centerY);
    assert.equal(libraryController.getRenderSnapshot().sortWheelHoveredMode, "recent");
    libraryController.handlePointerUp(recentSlot.centerX, recentSlot.centerY);

    const committedState = libraryController.getRenderSnapshot();
    assert.equal(committedState.sortWheelOpen, false);
    assert.equal(committedState.albumSortMode, "recent");
    assert.equal(committedState.sortToastLabel, "MOST RECENT");
    assert.equal(committedState.scrollPosition, 0);

    // Hold and release back inside center cancel hub keeps albumSortMode unchanged
    libraryController.handlePointerDown(CRATE_SORT_BUTTON_X, CRATE_SORT_BUTTON_Y);
    advanceLibraryController(libraryController, 0.05, 8);
    assert.equal(libraryController.getRenderSnapshot().sortWheelOpen, true);
    libraryController.handlePointerMove(oldestSlot.centerX, oldestSlot.centerY);
    libraryController.handlePointerMove(CRATE_SORT_BUTTON_X, CRATE_SORT_BUTTON_Y);
    libraryController.handlePointerUp(CRATE_SORT_BUTTON_X, CRATE_SORT_BUTTON_Y);
    const cancelledState = libraryController.getRenderSnapshot();
    assert.equal(cancelledState.sortWheelOpen, false);
    assert.equal(cancelledState.albumSortMode, "recent");
  });

  test("reversing direction midway on outer rim restarts sweep at turn-around peak and requires 180° in the new direction", () => {
    const libraryController = createSettledLibraryController({
      catalog: buildLetterSeekTestCatalog(),
    });
    openHubSectionTile(libraryController, 5); // All songs

    const startPoint = computeRimPoint(RIM_START_ANGLE_RADIANS);
    libraryController.handlePointerDown(startPoint.pointX, startPoint.pointY);

    const turnAroundAngle = RIM_START_ANGLE_RADIANS + (110 * Math.PI) / 180;
    dragAlongRim(libraryController, RIM_START_ANGLE_RADIANS, turnAroundAngle);
    assert.equal(libraryController.getRenderSnapshot().letterSearchActive, false);

    // Reverse CCW 110° back to start (total unsigned travel 220° > 180°): must NOT activate Letter Seek.
    dragAlongRim(libraryController, turnAroundAngle, RIM_START_ANGLE_RADIANS);
    const reversedToStart = libraryController.getRenderSnapshot();
    assert.equal(reversedToStart.letterSearchActive, false);
    assert.ok(
      angularDistanceRadians(reversedToStart.arcStartAngle, turnAroundAngle) < 0.01,
      "arcStartAngle must re-anchor to the CW turn-around peak after reversing >= ARC_RADIANS_PER_STEP",
    );
    assert.ok(reversedToStart.arcSignedSweepRadians < 0);
    assert.ok(
      Math.abs(
        reversedToStart.arcAbsoluteSweepRadians - Math.abs(reversedToStart.arcSignedSweepRadians),
      ) < FLOAT_TOLERANCE,
    );

    // Continuing CCW to 185° from the turn-around peak DOES activate Letter Seek.
    const ccwPastThresholdAngle = turnAroundAngle - (185 * Math.PI) / 180;
    dragAlongRim(libraryController, RIM_START_ANGLE_RADIANS, ccwPastThresholdAngle);
    const activatedState = libraryController.getRenderSnapshot();
    assert.equal(activatedState.letterSearchActive, true);
    libraryController.handlePointerUp(
      computeRimPoint(ccwPastThresholdAngle).pointX,
      computeRimPoint(ccwPastThresholdAngle).pointY,
    );
  });

  test("unidirectional CCW outer-ring drag activates Letter Seek and reversing while active steps letters CW/CCW", () => {
    const libraryController = createSettledLibraryController({
      catalog: buildLetterSeekTestCatalog(),
    });
    openHubSectionTile(libraryController, 5); // All songs

    // Scroll to the bottom so CCW Letter Seek still has room to step backward after activation.
    for (let stepIndex = 0; stepIndex < 10; stepIndex += 1) {
      libraryController.stepDetent(1);
    }
    advanceLibraryController(libraryController, STEP_DELTA_SECONDS, SETTLE_STEP_COUNT);

    const startPoint = computeRimPoint(RIM_START_ANGLE_RADIANS);
    libraryController.handlePointerDown(startPoint.pointX, startPoint.pointY);

    const ccwActivateAngle = RIM_START_ANGLE_RADIANS - Math.PI * 1.1;
    dragAlongRim(libraryController, RIM_START_ANGLE_RADIANS, ccwActivateAngle);
    const activeState = libraryController.getRenderSnapshot();
    assert.equal(activeState.letterSearchActive, true);
    assert.ok(activeState.arcSignedSweepRadians <= -Math.PI);

    const initialLetterIndex = activeState.letterSearchIndex;
    const stepBackCcwAngle =
      ccwActivateAngle - LIBRARY_CONSTANTS.LETTER_STEP_RADIANS - RIM_STEP_EPSILON_RADIANS;
    dragAlongRim(libraryController, ccwActivateAngle, stepBackCcwAngle);
    const steppedPrevState = libraryController.getRenderSnapshot();
    assert.equal(steppedPrevState.letterSearchActive, true);
    assert.ok(steppedPrevState.letterSearchIndex < initialLetterIndex);

    // Reversing CW while Letter Seek is already active steps forward without exiting Letter Seek.
    const stepForwardCwAngle =
      stepBackCcwAngle + LIBRARY_CONSTANTS.LETTER_STEP_RADIANS * 2 + RIM_STEP_EPSILON_RADIANS;
    dragAlongRim(libraryController, stepBackCcwAngle, stepForwardCwAngle);
    const steppedNextState = libraryController.getRenderSnapshot();
    assert.equal(steppedNextState.letterSearchActive, true);
    assert.ok(steppedNextState.letterSearchIndex > steppedPrevState.letterSearchIndex);

    libraryController.handlePointerUp(
      computeRimPoint(stepForwardCwAngle).pointX,
      computeRimPoint(stepForwardCwAngle).pointY,
    );
  });

  test("small opposite jitter (< ARC_RADIANS_PER_STEP) tracks bead without resetting anchor and oscillations never accumulate to 180°", () => {
    const libraryController = createSettledLibraryController({
      catalog: buildLetterSeekTestCatalog(),
    });
    openHubSectionTile(libraryController, 3); // Albums Crate

    const startPoint = computeRimPoint(RIM_START_ANGLE_RADIANS);
    libraryController.handlePointerDown(startPoint.pointX, startPoint.pointY);

    const angle150 = RIM_START_ANGLE_RADIANS + (150 * Math.PI) / 180;
    const angle140 = RIM_START_ANGLE_RADIANS + (140 * Math.PI) / 180;
    dragAlongRim(libraryController, RIM_START_ANGLE_RADIANS, angle150);
    dragAlongRim(libraryController, angle150, angle140);

    const jitterState = libraryController.getRenderSnapshot();
    assert.equal(jitterState.letterSearchActive, false);
    assert.ok(
      angularDistanceRadians(jitterState.arcStartAngle, RIM_START_ANGLE_RADIANS) < 0.01,
      "pullback < ARC_RADIANS_PER_STEP must preserve original arcStartAngle",
    );
    assert.ok(Math.abs(jitterState.arcSignedSweepRadians - (140 * Math.PI) / 180) < 0.02);

    // Resuming CW past 185° activates Letter Seek cleanly.
    const angle185 = RIM_START_ANGLE_RADIANS + (185 * Math.PI) / 180;
    dragAlongRim(libraryController, angle140, angle185);
    assert.equal(libraryController.getRenderSnapshot().letterSearchActive, true);
    libraryController.handlePointerUp(
      computeRimPoint(angle185).pointX,
      computeRimPoint(angle185).pointY,
    );

    // Repeated +-10° oscillations around 165° (> 300° total unsigned travel) must never trigger Letter Seek.
    libraryController.handlePointerDown(startPoint.pointX, startPoint.pointY);
    const angle165 = RIM_START_ANGLE_RADIANS + (165 * Math.PI) / 180;
    const angle155 = RIM_START_ANGLE_RADIANS + (155 * Math.PI) / 180;
    dragAlongRim(libraryController, RIM_START_ANGLE_RADIANS, angle165);
    for (let cycleIndex = 0; cycleIndex < 10; cycleIndex += 1) {
      dragAlongRim(libraryController, angle165, angle155);
      dragAlongRim(libraryController, angle155, angle165);
    }
    assert.equal(libraryController.getRenderSnapshot().letterSearchActive, false);
    libraryController.handlePointerUp(
      computeRimPoint(angle165).pointX,
      computeRimPoint(angle165).pointY,
    );
  });

  test("early zero-crossing (< ARC_RADIANS_PER_STEP) flips direction cleanly from original arcStartAngle", () => {
    const libraryController = createSettledLibraryController({
      catalog: buildLetterSeekTestCatalog(),
    });
    openHubSectionTile(libraryController, 5); // All songs

    const startPoint = computeRimPoint(RIM_START_ANGLE_RADIANS);
    libraryController.handlePointerDown(startPoint.pointX, startPoint.pointY);

    const earlyCwAngle = RIM_START_ANGLE_RADIANS + (10 * Math.PI) / 180;
    const pastStartCcwAngle = RIM_START_ANGLE_RADIANS - (30 * Math.PI) / 180;
    dragAlongRim(libraryController, RIM_START_ANGLE_RADIANS, earlyCwAngle);
    dragAlongRim(libraryController, earlyCwAngle, pastStartCcwAngle);

    const flippedState = libraryController.getRenderSnapshot();
    assert.equal(flippedState.letterSearchActive, false);
    assert.ok(
      angularDistanceRadians(flippedState.arcStartAngle, RIM_START_ANGLE_RADIANS) < 0.01,
      "early zero-crossing must keep original arcStartAngle",
    );
    assert.ok(flippedState.arcSignedSweepRadians < 0);
    libraryController.handlePointerUp(
      computeRimPoint(pastStartCcwAngle).pointX,
      computeRimPoint(pastStartCcwAngle).pointY,
    );
  });

  test("non-alphabetical view (Hub) applies identical reversal re-anchoring on outer-ring drag", () => {
    const libraryController = createSettledLibraryController({
      catalog: buildLetterSeekTestCatalog(),
    });

    const startPoint = computeRimPoint(RIM_START_ANGLE_RADIANS);
    libraryController.handlePointerDown(startPoint.pointX, startPoint.pointY);

    const turnAroundAngle = RIM_START_ANGLE_RADIANS + (100 * Math.PI) / 180;
    const reversedAngle = turnAroundAngle - (40 * Math.PI) / 180;
    dragAlongRim(libraryController, RIM_START_ANGLE_RADIANS, turnAroundAngle);
    dragAlongRim(libraryController, turnAroundAngle, reversedAngle);

    const hubState = libraryController.getRenderSnapshot();
    assert.equal(hubState.currentView.kind, "hub");
    assert.equal(hubState.letterSearchActive, false);
    assert.ok(
      angularDistanceRadians(hubState.arcStartAngle, turnAroundAngle) < 0.01,
      "Hub must re-anchor arcStartAngle to turn-around peak after >= ARC_RADIANS_PER_STEP reversal",
    );
    assert.ok(hubState.arcSignedSweepRadians < 0);
    libraryController.handlePointerUp(
      computeRimPoint(reversedAngle).pointX,
      computeRimPoint(reversedAngle).pointY,
    );
  });

  test("buildAlbumSeekIndex builds sort-aware anchors for alphabetical, liked-first, recent, and oldest", () => {
    const epochSecondsForLocalNoon = (year, monthIndex, dayOfMonth) =>
      Math.floor(new Date(year, monthIndex, dayOfMonth, 12, 0, 0).getTime() / 1000);

    // 1. Alphabetical
    const alphaAlbums = [
      { id: "a1", title: "1984", isFavorite: false, addedAt: 100 },
      { id: "a2", title: "Alpha", isFavorite: true, addedAt: 200 },
      { id: "a3", title: "Apollo", isFavorite: false, addedAt: 300 },
      { id: "a4", title: "Zebra", isFavorite: false, addedAt: 400 },
    ];
    assert.deepEqual(buildAlbumSeekIndex(alphaAlbums, "alphabetical"), [
      { letter: "#", subLabel: "", shortLabel: "#", itemIndex: 0, label: "1984", isFavorite: false, kind: "letter" },
      { letter: "A", subLabel: "", shortLabel: "A", itemIndex: 1, label: "Alpha", isFavorite: false, kind: "letter" },
      { letter: "Z", subLabel: "", shortLabel: "Z", itemIndex: 3, label: "Zebra", isFavorite: false, kind: "letter" },
    ]);

    // 2. Liked First (duplicate initial letters across Liked and Unliked halves remain distinct)
    const likedFirstAlbums = sortAlbumsByMode(
      [
        { id: "l1", title: "Alpha", isFavorite: true },
        { id: "l2", title: "Apex", isFavorite: true },
        { id: "l3", title: "Beta", isFavorite: true },
        { id: "u1", title: "Amber", isFavorite: false },
        { id: "u2", title: "Zebra", isFavorite: false },
      ],
      "liked-first",
    );
    assert.deepEqual(buildAlbumSeekIndex(likedFirstAlbums, "liked-first"), [
      { letter: "A", subLabel: "", shortLabel: "A", itemIndex: 0, label: "Alpha", isFavorite: true, kind: "liked-letter" },
      { letter: "B", subLabel: "", shortLabel: "B", itemIndex: 2, label: "Beta", isFavorite: true, kind: "liked-letter" },
      { letter: "A", subLabel: "", shortLabel: "A", itemIndex: 3, label: "Amber", isFavorite: false, kind: "letter" },
      { letter: "Z", subLabel: "", shortLabel: "Z", itemIndex: 4, label: "Zebra", isFavorite: false, kind: "letter" },
    ]);

    // 3. Liked First boundary: all liked or none liked
    assert.equal(
      buildAlbumSeekIndex(
        [
          { id: "f1", title: "Alpha", isFavorite: true },
          { id: "f2", title: "Beta", isFavorite: true },
        ],
        "liked-first",
      ).every((entry) => entry.isFavorite === true && entry.kind === "liked-letter"),
      true,
    );
    assert.equal(
      buildAlbumSeekIndex(
        [
          { id: "n1", title: "Alpha", isFavorite: false },
          { id: "n2", title: "Beta", isFavorite: false },
        ],
        "liked-first",
      ).every((entry) => entry.isFavorite === false && entry.kind === "letter"),
      true,
    );

    // 4. Recent & Oldest: Month + Year granularity (2..12 distinct months) + undated album doesn't distort
    const monthSpanAlbums = sortAlbumsByMode(
      [
        { id: "m1", title: "Nov Album", addedAt: epochSecondsForLocalNoon(2026, 10, 15) },
        { id: "m2", title: "Nov Earlier", addedAt: epochSecondsForLocalNoon(2026, 10, 2) },
        { id: "m3", title: "Oct Album", addedAt: epochSecondsForLocalNoon(2026, 9, 20) },
        { id: "m4", title: "Undated", addedAt: 0 },
      ],
      "recent",
    );
    assert.deepEqual(buildAlbumSeekIndex(monthSpanAlbums, "recent"), [
      {
        letter: "NOV",
        subLabel: "2026",
        shortLabel: "NOV \u201926",
        itemIndex: 0,
        label: "Nov Album",
        isFavorite: false,
        kind: "date-month",
      },
      {
        letter: "OCT",
        subLabel: "2026",
        shortLabel: "OCT \u201926",
        itemIndex: 2,
        label: "Oct Album",
        isFavorite: false,
        kind: "date-month",
      },
      {
        letter: "\u2014",
        subLabel: "NO DATE",
        shortLabel: "\u2014",
        itemIndex: 3,
        label: "Undated",
        isFavorite: false,
        kind: "date-unknown",
      },
    ]);

    // 5. Boundary: exactly 12 distinct months stays Month + Year; 13 switches to Year
    const twelveMonths = Array.from({ length: 12 }, (_, monthIndex) => ({
      id: `tm-${monthIndex}`,
      title: `Album ${monthIndex}`,
      addedAt: epochSecondsForLocalNoon(2026, monthIndex, 10),
    }));
    const twelveAnchors = buildAlbumSeekIndex(sortAlbumsByMode(twelveMonths, "oldest"), "oldest");
    assert.equal(twelveAnchors.length, 12);
    assert.equal(twelveAnchors[0].kind, "date-month");
    assert.equal(twelveAnchors[0].letter, "JAN");

    const thirteenMonths = [
      ...twelveMonths,
      { id: "tm-12", title: "Album 12", addedAt: epochSecondsForLocalNoon(2025, 11, 10) },
    ];
    const thirteenAnchors = buildAlbumSeekIndex(sortAlbumsByMode(thirteenMonths, "recent"), "recent");
    assert.deepEqual(thirteenAnchors, [
      {
        letter: "2026",
        subLabel: "",
        shortLabel: "2026",
        itemIndex: 0,
        label: "Album 11",
        isFavorite: false,
        kind: "date-year",
      },
      {
        letter: "2025",
        subLabel: "",
        shortLabel: "2025",
        itemIndex: 12,
        label: "Album 12",
        isFavorite: false,
        kind: "date-year",
      },
    ]);

    // 6. Day granularity when all dated albums share one month across >= 2 days
    const sameMonthAlbums = sortAlbumsByMode(
      [
        { id: "d1", title: "Day 10", addedAt: epochSecondsForLocalNoon(2026, 10, 10) },
        { id: "d2", title: "Day 25", addedAt: epochSecondsForLocalNoon(2026, 10, 25) },
      ],
      "recent",
    );
    assert.deepEqual(buildAlbumSeekIndex(sameMonthAlbums, "recent"), [
      {
        letter: "25",
        subLabel: "NOV \u201926",
        shortLabel: "NOV 25",
        itemIndex: 0,
        label: "Day 25",
        isFavorite: false,
        kind: "date-day",
      },
      {
        letter: "10",
        subLabel: "NOV \u201926",
        shortLabel: "NOV 10",
        itemIndex: 1,
        label: "Day 10",
        isFavorite: false,
        kind: "date-day",
      },
    ]);

    // 7. Fallback & negative guards
    assert.deepEqual(buildAlbumSeekIndex(null, "recent"), []);
    assert.deepEqual(buildAlbumSeekIndex([], "liked-first"), []);
    assert.equal(buildAlbumSeekIndex(alphaAlbums, "unknown-mode")[0].kind, "letter");
  });

  test("outer-rim drag activates sort-aware seek in liked-first, recent, and oldest modes and disables on single-day crates", () => {
    const epochSecondsForLocalNoon = (year, monthIndex, dayOfMonth) =>
      Math.floor(new Date(year, monthIndex, dayOfMonth, 12, 0, 0).getTime() / 1000);

    const libraryController = createSettledLibraryController({
      catalog: {
        albums: [
          {
            id: "alb-1",
            title: "Alpha",
            artist: "Artist",
            isFavorite: true,
            addedAt: epochSecondsForLocalNoon(2026, 8, 5),
            tracks: [{ id: "t1", title: "Track 1" }],
          },
          {
            id: "alb-2",
            title: "Beta",
            artist: "Artist",
            isFavorite: true,
            addedAt: epochSecondsForLocalNoon(2026, 9, 12),
            tracks: [{ id: "t2", title: "Track 2" }],
          },
          {
            id: "alb-3",
            title: "Amber",
            artist: "Artist",
            isFavorite: false,
            addedAt: epochSecondsForLocalNoon(2026, 10, 20),
            tracks: [{ id: "t3", title: "Track 3" }],
          },
          {
            id: "alb-4",
            title: "Zebra",
            artist: "Artist",
            isFavorite: false,
            addedAt: epochSecondsForLocalNoon(2026, 10, 25),
            tracks: [{ id: "t4", title: "Track 4" }],
          },
        ],
        artists: [],
        genres: [],
        playlists: [],
      },
    });

    openHubSectionTile(libraryController, 3); // Albums Crate

    // Liked First: ♥A (0), ♥B (1), A (2), Z (3)
    // Drag CCW >= 180 deg from index 0 so scrollPosition stays at 0, then reverse CW to step to index 2 (A)
    libraryController.setAlbumSortMode("liked-first");
    const startPoint = computeRimPoint(RIM_START_ANGLE_RADIANS);
    libraryController.handlePointerDown(startPoint.pointX, startPoint.pointY);
    const pastCcwThresholdAngle = RIM_START_ANGLE_RADIANS - Math.PI - 0.12;
    dragAlongRim(libraryController, RIM_START_ANGLE_RADIANS, pastCcwThresholdAngle);
    assert.equal(libraryController.getRenderSnapshot().letterSearchActive, true);
    assert.equal(libraryController.getRenderSnapshot().letterSearchIndex, 0);
    const nextLetterAngle = pastCcwThresholdAngle + LIBRARY_CONSTANTS.LETTER_STEP_RADIANS * 2.2;
    dragAlongRim(libraryController, pastCcwThresholdAngle, nextLetterAngle);
    const likedSeekSnapshot = libraryController.getRenderSnapshot();
    assert.equal(likedSeekSnapshot.letterSearchIndex, 2);
    assert.equal(likedSeekSnapshot.scrollPosition, 2);
    assert.equal(likedSeekSnapshot.letterEntries[2].isFavorite, false);
    libraryController.handlePointerUp(
      computeRimPoint(nextLetterAngle).pointX,
      computeRimPoint(nextLetterAngle).pointY,
    );

    // Recent: NOV (0), OCT (2), SEP (3)
    libraryController.setAlbumSortMode("recent");
    libraryController.handlePointerDown(startPoint.pointX, startPoint.pointY);
    dragAlongRim(libraryController, RIM_START_ANGLE_RADIANS, pastCcwThresholdAngle);
    const recentSeekStart = libraryController.getRenderSnapshot();
    assert.equal(recentSeekStart.letterSearchActive, true);
    assert.equal(recentSeekStart.letterEntries[0].letter, "NOV");
    assert.equal(recentSeekStart.letterSearchIndex, 0);
    const stepOneAngle = pastCcwThresholdAngle + LIBRARY_CONSTANTS.LETTER_STEP_RADIANS * 1.1;
    dragAlongRim(libraryController, pastCcwThresholdAngle, stepOneAngle);
    const recentStepped = libraryController.getRenderSnapshot();
    assert.equal(recentStepped.letterSearchIndex, 1);
    assert.equal(recentStepped.scrollPosition, 2);
    libraryController.handlePointerUp(
      computeRimPoint(stepOneAngle).pointX,
      computeRimPoint(stepOneAngle).pointY,
    );

    // Single-day crate in Oldest mode stays disabled
    const sameDayController = createSettledLibraryController({
      catalog: {
        albums: [
          { id: "sd-1", title: "Alpha", addedAt: epochSecondsForLocalNoon(2026, 10, 25), tracks: [{ id: "st1", title: "One" }] },
          { id: "sd-2", title: "Beta", addedAt: epochSecondsForLocalNoon(2026, 10, 25), tracks: [{ id: "st2", title: "Two" }] },
          { id: "sd-3", title: "Gamma", addedAt: epochSecondsForLocalNoon(2026, 10, 25), tracks: [{ id: "st3", title: "Three" }] },
        ],
        artists: [],
        genres: [],
        playlists: [],
      },
    });
    openHubSectionTile(sameDayController, 3);
    sameDayController.setAlbumSortMode("oldest");
    assert.deepEqual(sameDayController.getRenderSnapshot().letterEntries, []);
    sameDayController.handlePointerDown(startPoint.pointX, startPoint.pointY);
    dragAlongRim(sameDayController, RIM_START_ANGLE_RADIANS, pastCcwThresholdAngle);
    assert.equal(sameDayController.getRenderSnapshot().letterSearchActive, false);
    sameDayController.handlePointerUp(
      computeRimPoint(pastCcwThresholdAngle).pointX,
      computeRimPoint(pastCcwThresholdAngle).pointY,
    );
  });

  test("Artists view supports independent 5-mode sorting, centered radial sort wheel, and sort-aware seek", () => {
    const epochSecondsForLocalNoon = (year, monthIndex, dayOfMonth) =>
      Math.floor(new Date(year, monthIndex, dayOfMonth, 12, 0, 0).getTime() / 1000);

    const libraryController = createSettledLibraryController({
      catalog: {
        albums: [
          {
            id: "alb-a",
            title: "Alpha",
            artist: "Amber Duo",
            addedAt: epochSecondsForLocalNoon(2026, 8, 5),
            tracks: [{ id: "t-a", title: "One" }],
          },
          {
            id: "alb-m",
            title: "Midnight",
            artist: "Mono Quartet",
            addedAt: epochSecondsForLocalNoon(2026, 9, 12),
            tracks: [{ id: "t-m", title: "Two" }],
          },
          {
            id: "alb-z",
            title: "Zebra",
            artist: "Zeta Band",
            addedAt: epochSecondsForLocalNoon(2026, 10, 20),
            tracks: [{ id: "t-z", title: "Three" }],
          },
        ],
        artists: [
          {
            id: "artist-z",
            name: "Zeta Band",
            countLabel: "1",
            albumIds: ["alb-z"],
            isFavorite: false,
            addedAt: epochSecondsForLocalNoon(2026, 10, 20),
          },
          {
            id: "artist-a",
            name: "Amber Duo",
            countLabel: "1",
            albumIds: ["alb-a"],
            isFavorite: false,
            addedAt: epochSecondsForLocalNoon(2026, 8, 5),
          },
          {
            id: "artist-m",
            name: "Mono Quartet",
            countLabel: "1",
            albumIds: ["alb-m"],
            isFavorite: true,
            addedAt: epochSecondsForLocalNoon(2026, 9, 12),
          },
        ],
        genres: [],
        playlists: [],
      },
    });

    openHubSectionTile(libraryController, 2); // Artists
    assert.equal(libraryController.getRenderSnapshot().artistSortMode, "alphabetical");
    assert.equal(libraryController.getRenderSnapshot().albumSortMode, "alphabetical");
    assert.deepEqual(
      libraryController.getRenderSnapshot().currentRows.map((row) => row.id),
      ["artist-a", "artist-m", "artist-z"],
    );

    // Scroll down then tap Sort button (180, 325) -> reverse-alphabetical, resets scroll to 0, leaves albumSortMode untouched
    libraryController.stepDetent(1);
    advanceLibraryController(libraryController, STEP_DELTA_SECONDS, SETTLE_STEP_COUNT);
    assert.ok(libraryController.getRenderSnapshot().scrollPosition > 0);

    tapPoint(libraryController, ARTISTS_SORT_BUTTON_X, ARTISTS_SORT_BUTTON_Y);
    const reverseArtists = libraryController.getRenderSnapshot();
    assert.equal(reverseArtists.artistSortMode, "reverse-alphabetical");
    assert.equal(reverseArtists.albumSortMode, "alphabetical");
    assert.equal(reverseArtists.sortToastLabel, "Z\u2013A");
    assert.equal(reverseArtists.scrollPosition, 0);
    assert.deepEqual(
      reverseArtists.currentRows.map((row) => row.id),
      ["artist-z", "artist-m", "artist-a"],
    );
    assert.deepEqual(
      buildCollectionSeekIndex(reverseArtists.currentRows, "reverse-alphabetical").map(
        (anchor) => anchor.letter,
      ),
      ["Z", "M", "A"],
    );

    // Tap Sort button -> liked-first
    tapPoint(libraryController, ARTISTS_SORT_BUTTON_X, ARTISTS_SORT_BUTTON_Y);
    const likedArtists = libraryController.getRenderSnapshot();
    assert.equal(likedArtists.artistSortMode, "liked-first");
    assert.deepEqual(
      likedArtists.currentRows.map((row) => row.id),
      ["artist-m", "artist-a", "artist-z"],
    );

    // Tap Sort button -> recent -> oldest -> alphabetical
    tapPoint(libraryController, ARTISTS_SORT_BUTTON_X, ARTISTS_SORT_BUTTON_Y);
    assert.equal(libraryController.getRenderSnapshot().artistSortMode, "recent");
    assert.deepEqual(
      libraryController.getRenderSnapshot().currentRows.map((row) => row.id),
      ["artist-z", "artist-m", "artist-a"],
    );
    assert.equal(libraryController.getRenderSnapshot().letterEntries[0].letter, "NOV");

    tapPoint(libraryController, ARTISTS_SORT_BUTTON_X, ARTISTS_SORT_BUTTON_Y);
    assert.equal(libraryController.getRenderSnapshot().artistSortMode, "oldest");
    assert.deepEqual(
      libraryController.getRenderSnapshot().currentRows.map((row) => row.id),
      ["artist-a", "artist-m", "artist-z"],
    );

    // Long-pressing (180, 325) opens radial sort wheel scoped to "artists"
    libraryController.handlePointerDown(ARTISTS_SORT_BUTTON_X, ARTISTS_SORT_BUTTON_Y);
    advanceLibraryController(libraryController, 0.05, 8);
    const wheelOpen = libraryController.getRenderSnapshot();
    assert.equal(wheelOpen.sortWheelOpen, true);
    assert.equal(wheelOpen.sortWheelScope, "artists");
    assert.equal(wheelOpen.sortWheelHoveredMode, null);

    const artistWheelSlots = computeSortWheelSlots(ARTISTS_SORT_BUTTON_X, ARTISTS_SORT_BUTTON_Y);
    const recentSlot = artistWheelSlots.find((slot) => slot.mode === "recent");
    libraryController.handlePointerMove(recentSlot.centerX, recentSlot.centerY);
    assert.equal(libraryController.getRenderSnapshot().sortWheelHoveredMode, "recent");
    libraryController.handlePointerUp(recentSlot.centerX, recentSlot.centerY);
    assert.equal(libraryController.getRenderSnapshot().sortWheelOpen, false);
    assert.equal(libraryController.getRenderSnapshot().artistSortMode, "recent");
    assert.equal(libraryController.getRenderSnapshot().albumSortMode, "alphabetical");

    // Cancel back in center hub leaves artistSortMode unchanged
    libraryController.handlePointerDown(ARTISTS_SORT_BUTTON_X, ARTISTS_SORT_BUTTON_Y);
    advanceLibraryController(libraryController, 0.05, 8);
    libraryController.handlePointerMove(recentSlot.centerX, recentSlot.centerY);
    libraryController.handlePointerMove(ARTISTS_SORT_BUTTON_X, ARTISTS_SORT_BUTTON_Y);
    libraryController.handlePointerUp(ARTISTS_SORT_BUTTON_X, ARTISTS_SORT_BUTTON_Y);
    assert.equal(libraryController.getRenderSnapshot().sortWheelOpen, false);
    assert.equal(libraryController.getRenderSnapshot().artistSortMode, "recent");
  });

  test("long-tapping an Artist row opens Artist Quick Actions, toggles like without adding to Favorites, and supports liked-first", () => {
    const toggledCalls = [];
    const libraryController = createSettledLibraryController({
      onToggleArtistFavorite: (artistId, nextState) => {
        toggledCalls.push({ artistId, nextState });
      },
    });
    openHubSectionTile(libraryController, 2); // Artists (Amber Duo is row 0)

    const rowZeroLayout = computeLibraryCurvedRowLayout(0, 0);

    // Drift >= TAP_DRIFT_LIMIT_PX cancels long-press
    libraryController.handlePointerDown(rowZeroLayout.centerX, rowZeroLayout.rowY);
    libraryController.handlePointerMove(rowZeroLayout.centerX + 16, rowZeroLayout.rowY);
    advanceLibraryController(libraryController, 0.1, 6);
    assert.equal(libraryController.getRenderSnapshot().artistQuickActionsOpen, false);
    libraryController.handlePointerUp(rowZeroLayout.centerX + 16, rowZeroLayout.rowY);

    // Long-press on row 0 opens Artist Quick Actions
    libraryController.handlePointerDown(rowZeroLayout.centerX, rowZeroLayout.rowY);
    advanceLibraryController(libraryController, 0.1, 6);
    const openState = libraryController.getRenderSnapshot();
    assert.equal(openState.artistQuickActionsOpen, true);
    assert.equal(openState.artistQuickActionsArtistId, "artist-a");
    assert.equal(openState.albumQuickActionsOpen, false);
    libraryController.handlePointerUp(rowZeroLayout.centerX, rowZeroLayout.rowY);

    // Tap heart medallion (180, 196) -> likes artist-a, notifies callback, auto-closes, keeps Favorites count at 0
    tapPoint(libraryController, 180, LIBRARY_CONSTANTS.ALBUM_QUICK_ACTION_BUTTON_CENTER_Y);
    const afterLike = libraryController.getRenderSnapshot();
    assert.deepEqual(toggledCalls, [{ artistId: "artist-a", nextState: true }]);
    assert.equal(afterLike.counts.favorites, 0);
    assert.equal(afterLike.counts.favoriteAlbums, 0);
    assert.equal(afterLike.counts.favoriteTracks, 0);
    assert.equal(
      afterLike.catalog.artists.find((entry) => entry.id === "artist-a").isFavorite,
      true,
    );

    advanceLibraryController(libraryController, 0.05, 6);
    assert.equal(libraryController.getRenderSnapshot().artistQuickActionsOpen, false);

    // Verify Library/Favorites still sees 0 favorites (opens empty state)
    libraryController.stepBack();
    advanceLibraryController(libraryController, STEP_DELTA_SECONDS, SETTLE_STEP_COUNT);
    const favoritesView = openHubSectionTile(libraryController, 0);
    assert.equal(favoritesView.currentView.kind, "empty");

    // Return to Artists, tap outside to close after opening, and verify stepBack closes it
    libraryController.stepBack();
    advanceLibraryController(libraryController, STEP_DELTA_SECONDS, SETTLE_STEP_COUNT);
    openHubSectionTile(libraryController, 2);
    libraryController.openArtistQuickActions("artist-z");
    assert.equal(libraryController.getRenderSnapshot().artistQuickActionsOpen, true);
    tapPoint(libraryController, 180, 316);
    assert.equal(libraryController.getRenderSnapshot().artistQuickActionsOpen, false);

    libraryController.openArtistQuickActions("artist-z");
    libraryController.stepBack();
    assert.equal(libraryController.getRenderSnapshot().artistQuickActionsOpen, false);

    // Liked-first floats artist-a to the top
    libraryController.setArtistSortMode("liked-first");
    assert.equal(libraryController.getRenderSnapshot().currentRows[0].id, "artist-a");
  });

  test("tapping Album icon during post-like auto-close window or at top-right sleeve corner launches playback in Albums and Artists -> Albums", () => {
    for (const viewMode of ["albums", "artist-albums"]) {
      for (const tapCoords of [
        [172, CRATE_CENTER_Y],
        [180, LIBRARY_CONSTANTS.ALBUM_QUICK_ACTION_BUTTON_CENTER_Y],
        [228, 92],
      ]) {
        const launchedSpecs = [];
        const libraryController = createSettledLibraryController({
          onLaunchTrack: (launchSpec) => {
            launchedSpecs.push(launchSpec);
          },
        });

        if (viewMode === "albums") {
          openHubSectionTile(libraryController, 3);
        } else {
          openHubSectionTile(libraryController, 2);
          const artistRow = computeLibraryCurvedRowLayout(0, 0);
          tapPoint(libraryController, artistRow.centerX, artistRow.rowY);
          advanceLibraryController(libraryController, STEP_DELTA_SECONDS, SETTLE_STEP_COUNT);
        }

        const expectedAlbumId = libraryController.getRenderSnapshot().currentCrateAlbums[0].id;

        // Long-press sleeve to open Album Quick Actions
        libraryController.handlePointerDown(CRATE_SLEEVE_TAP_X, CRATE_CENTER_Y);
        advanceLibraryController(libraryController, 0.1, 6);
        libraryController.handlePointerUp(CRATE_SLEEVE_TAP_X, CRATE_CENTER_Y);
        assert.equal(libraryController.getRenderSnapshot().albumQuickActionsOpen, true);

        // Tap Heart medallion to like album (starts 0.24s auto-close timer)
        tapPoint(libraryController, 180, LIBRARY_CONSTANTS.ALBUM_QUICK_ACTION_BUTTON_CENTER_Y);
        assert.equal(
          libraryController.getRenderSnapshot().catalog.albums.find((entry) => entry.id === expectedAlbumId).isFavorite,
          true,
        );

        // Immediately tap Album icon within the 0.24s auto-close window (only 0.05s elapsed)
        libraryController.step(0.05);
        tapPoint(libraryController, tapCoords[0], tapCoords[1]);

        const afterTap = libraryController.getRenderSnapshot();
        assert.equal(afterTap.albumQuickActionsOpen, false);
        assert.equal(
          afterTap.catalog.albums.find((entry) => entry.id === expectedAlbumId).isFavorite,
          true,
        );
        assert.equal(launchedSpecs.length, 1);
        assert.equal(launchedSpecs[0].albumId, expectedAlbumId);
      }
    }
  });
});
