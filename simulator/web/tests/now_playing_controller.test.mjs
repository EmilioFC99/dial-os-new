import { describe, test } from "node:test";
import assert from "node:assert/strict";

import { PLAY_MODES, createAudioEngine } from "../core/audio_engine.js";
import {
  NOW_PLAYING_CONSTANTS,
  NOW_PLAYING_LYRICS_MODES,
  NOW_PLAYING_PLAY_MODES,
  NOW_PLAYING_QUICK_BUTTONS,
  computeNowPlayingQueueRowLayout,
  createNowPlayingController,
  enrichTrackDetails,
  hitTestNowPlayingQueueRow,
  resolveActiveLyricsFrame,
  resolveTrackLyricsSource,
} from "../core/now_playing_controller.js";

const DISPLAY_CENTER_PX = 180;
const STEP_DELTA_SECONDS = 0.05;
const SETTLE_STEP_COUNT = 30;
const FLOAT_TOLERANCE = 1e-6;

const MOCK_AUDIO_DURATION_SECONDS = 180;
const MOCK_AUDIO_READY_STATE = 4;

const IDLE_BROWSE_CTA_X = 180;
const IDLE_BROWSE_CTA_Y = 266;

const VINYL_CENTER_TAP_X = 180;
const VINYL_CENTER_TAP_Y = 180;

const QUICK_FAVORITE_X = 104;
const QUICK_FAVORITE_Y = 176;
const QUICK_PLAY_MODE_X = 180;
const QUICK_PLAY_MODE_Y = 176;
const QUICK_PLAYLIST_X = 256;
const QUICK_PLAYLIST_Y = 176;
const QUICK_LYRICS_X = 142;
const QUICK_LYRICS_Y = 248;
const QUICK_DETAILS_X = 218;
const QUICK_DETAILS_Y = 248;

const SEEK_HOLD_POINT_X = 180;
const SEEK_HOLD_POINT_Y = 120;
const SEEK_ARC_THREE_OCLOCK_X = 240;
const SEEK_ARC_THREE_OCLOCK_Y = 180;
const SEEK_ARC_SIX_OCLOCK_X = 180;
const SEEK_ARC_SIX_OCLOCK_Y = 240;
const SEEK_SPINDLE_START_X = 172;
const SEEK_SPINDLE_END_X = 194;
const SEEK_SPINDLE_Y = 180;

const VOLUME_BEZEL_X = 330;
const VOLUME_BEZEL_START_Y = 180;
const VOLUME_BEZEL_UP_END_Y = 97;
const VOLUME_BEZEL_MAX_UP_Y = 10;
const VOLUME_BEZEL_MAX_DOWN_Y = 350;

const SKIP_LEFT_START_X = 230;
const SKIP_LEFT_END_X = 150;
const SKIP_RIGHT_START_X = 140;
const SKIP_RIGHT_END_X = 215;

const QUEUE_PULL_START_Y = 260;
const QUEUE_PULL_END_Y = 200;
const QUEUE_CLOSE_START_Y = 140;
const QUEUE_CLOSE_END_Y = 224;

const BACK_SWIPE_START_X = 24;
const BACK_SWIPE_PARTIAL_END_X = 50;
const BACK_SWIPE_COMMIT_END_X = 90;
const BACK_SWIPE_Y = 180;

const SHUFFLE_TRIAL_ROUNDS = 16;

/**
 * Create an HTMLAudioElement-compatible stub for deterministic audio engine tests.
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
 * Build a multi-album catalog fixture with rich technical fields and synced lyrics.
 *
 * @param {object} [overrides] Optional top-level field overrides such as playlists or lyricsCatalog.
 * @returns {object} Complete simulator catalog fixture.
 */
function buildNowPlayingTestCatalog(overrides = {}) {
  const trackOne = {
    id: "t1",
    title: "01 Dawn",
    artist: "Aurora Band",
    albumId: "a1",
    albumTitle: "Alpha Waves",
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
    title: "02 Dusk",
    artist: "Aurora Band",
    albumId: "a1",
    albumTitle: "Alpha Waves",
    genre: "Ambient",
    year: 2024,
    duration: "03:20",
    durationSeconds: 200,
    isFavorite: true,
    trackNumber: "02",
    audioUrl: "/api/audio/t2",
    format: "FLAC 24-BIT",
    sampleRate: "96.0 KHZ",
    bitrate: "2,640 KBPS",
    fileSize: "39.6 MB",
  };
  const trackThree = {
    id: "t3",
    title: "01 Horizon",
    artist: "Boreal Quartet",
    albumId: "a2",
    albumTitle: "Zenith Echo",
    genre: "Jazz",
    year: 2023,
    duration: "04:00",
    durationSeconds: 240,
    isFavorite: false,
    trackNumber: "01",
    audioUrl: "/api/audio/t3",
    format: "MP3 320K",
    sampleRate: "44.1 KHZ",
    bitrate: "320 KBPS",
    fileSize: "9.4 MB",
  };
  const trackFour = {
    id: "t4",
    title: "02 Midnight",
    artist: "Boreal Quartet",
    albumId: "a2",
    albumTitle: "Zenith Echo",
    genre: "Jazz",
    year: 2023,
    duration: "02:30",
    durationSeconds: 150,
    isFavorite: false,
    trackNumber: "02",
    audioUrl: "/api/audio/t4",
    format: "MP3 320K",
    sampleRate: "44.1 KHZ",
    bitrate: "320 KBPS",
    fileSize: "6.0 MB",
  };
  const trackFive = {
    id: "t5",
    title: "03 Starlight",
    artist: "Boreal Quartet",
    albumId: "a2",
    albumTitle: "Zenith Echo",
    genre: "Jazz",
    year: 2023,
    duration: "02:00",
    durationSeconds: 120,
    isFavorite: true,
    trackNumber: "03",
    audioUrl: "/api/audio/t5",
    format: "MP3 320K",
    sampleRate: "44.1 KHZ",
    bitrate: "320 KBPS",
    fileSize: "4.8 MB",
  };

  const defaultLyricsCatalog = {
    t1: {
      kind: "timed",
      lines: [
        { timeSeconds: 5.0, text: "First light across the vinyl" },
        { timeSeconds: 12.5, text: "Spinning quietly in zero gravity" },
        { timeSeconds: 21.0, text: "Until the final groove fades" },
      ],
    },
    t2: {
      kind: "untimed",
      lines: [
        "Evening shadows settle softly",
        "Echoes linger over glass",
        "Steady pulse beneath the dial",
        "Night unfolds across the turntable",
      ],
    },
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
          { ...trackOne },
          { ...trackTwo },
        ],
      },
      {
        id: "a2",
        title: "Zenith Echo",
        artist: "Boreal Quartet",
        genre: "Jazz",
        year: 2023,
        tracks: [
          { ...trackThree },
          { ...trackFour },
          { ...trackFive },
        ],
      },
    ],
    allTracks: [trackOne, trackTwo, trackThree, trackFour, trackFive],
    playlists: [{ id: "p1", name: "Morning Mix", trackIds: ["t2", "t3"] }],
    lyricsCatalog: defaultLyricsCatalog,
    ...overrides,
  };
}

/**
 * Tap a point on a controller without drag displacement.
 *
 * @param {object} controller Controller instance under test.
 * @param {number} pointX Display coordinate X in pixels.
 * @param {number} pointY Display coordinate Y in pixels.
 */
function tapPoint(controller, pointX, pointY) {
  controller.handlePointerDown(pointX, pointY);
  controller.handlePointerUp(pointX, pointY);
}

/**
 * Advance a controller for a fixed number of ticks.
 *
 * @param {object} controller Controller instance under test.
 * @param {number} stepDeltaSeconds Frame delta in seconds.
 * @param {number} stepCount Number of ticks to simulate.
 * @returns {object} Final render snapshot.
 */
function advanceSteps(controller, stepDeltaSeconds, stepCount) {
  for (let stepIndex = 0; stepIndex < stepCount; stepIndex += 1) {
    controller.step(stepDeltaSeconds);
  }
  return controller.getRenderSnapshot();
}

/**
 * Open the 5-action Quick Menu via single tap past the double-tap window.
 *
 * @param {object} nowPlayingController Settled Now Playing controller in playing mode.
 */
function openQuickActionsViaTap(nowPlayingController) {
  tapPoint(nowPlayingController, VINYL_CENTER_TAP_X, VINYL_CENTER_TAP_Y);
  nowPlayingController.step(NOW_PLAYING_CONSTANTS.DOUBLE_TAP_WINDOW_SECONDS + 0.03);
}

/**
 * Execute a full rightward left-edge back-swipe gesture.
 *
 * @param {object} controller Controller instance under test.
 */
function performLeftEdgeBackSwipe(controller) {
  controller.handlePointerDown(BACK_SWIPE_START_X, BACK_SWIPE_Y);
  controller.handlePointerMove(BACK_SWIPE_COMMIT_END_X, BACK_SWIPE_Y);
  controller.handlePointerUp(BACK_SWIPE_COMMIT_END_X, BACK_SWIPE_Y);
}

/**
 * Create a wired audio engine + Now Playing controller test harness capturing all callbacks.
 *
 * @param {object} [options] Harness setup overrides.
 * @param {object} [options.catalog] Custom catalog fixture.
 * @param {object} [options.sourceSpec] Initial playback source descriptor; null stays idle.
 * @param {string} [options.returnScreen="library"] Initial entry return screen.
 * @param {() => object | null} [options.createAudioElement] Optional audio stub factory.
 * @returns {object} Harness holding engine, controller, catalog, and callback call logs.
 */
function createNowPlayingTestHarness({
  catalog = buildNowPlayingTestCatalog(),
  sourceSpec = { trackId: "t1", queueSource: "all-songs" },
  returnScreen = "library",
  createAudioElement,
} = {}) {
  const audioEngine = createAudioEngine({ createAudioElement });
  if (sourceSpec) {
    audioEngine.playTrackFromSource(sourceSpec, catalog);
  }

  const callbackLogs = {
    browseLibraryCalls: 0,
    togglePlayPauseCalls: 0,
    seekTargets: [],
    volumeChanges: [],
    skipDirections: [],
    favoriteToggles: [],
    playModeChanges: [],
    selectedQueueIndices: [],
    persistedPlaylists: [],
    exitReturnScreens: [],
  };

  let nowPlayingController = null;
  nowPlayingController = createNowPlayingController({
    onBrowseLibrary: () => {
      callbackLogs.browseLibraryCalls += 1;
    },
    onTogglePlayPause: () => {
      callbackLogs.togglePlayPauseCalls += 1;
      audioEngine.togglePlayPause();
      nowPlayingController.syncAudioState(audioEngine.getState());
    },
    onSeek: (targetSeconds) => {
      callbackLogs.seekTargets.push(targetSeconds);
      audioEngine.seekTo(targetSeconds);
      nowPlayingController.syncAudioState(audioEngine.getState());
    },
    onVolumeChange: (nextVolume) => {
      callbackLogs.volumeChanges.push(nextVolume);
      audioEngine.setVolume(nextVolume);
      nowPlayingController.syncAudioState(audioEngine.getState());
    },
    onSkipTrack: (direction) => {
      callbackLogs.skipDirections.push(direction);
      audioEngine.skipTrack(direction);
      nowPlayingController.syncAudioState(audioEngine.getState());
    },
    onToggleFavorite: (trackId, nextState) => {
      callbackLogs.favoriteToggles.push({ trackId, isFavorite: nextState });
      audioEngine.updateTrackFavorite(trackId, nextState);
      nowPlayingController.syncAudioState(audioEngine.getState());
    },
    onPlayModeChange: (nextModeId) => {
      callbackLogs.playModeChanges.push(nextModeId);
      audioEngine.setPlayMode(nextModeId);
      nowPlayingController.syncAudioState(audioEngine.getState());
    },
    onSelectQueueIndex: (rowIndex) => {
      callbackLogs.selectedQueueIndices.push(rowIndex);
      audioEngine.selectQueueIndex(rowIndex);
      nowPlayingController.syncAudioState(audioEngine.getState());
    },
    onPersistPlaylists: (updatedPlaylists) => {
      callbackLogs.persistedPlaylists.push(updatedPlaylists);
    },
    onExit: (screenId) => {
      callbackLogs.exitReturnScreens.push(screenId);
    },
  });

  nowPlayingController.hydrateCatalog(catalog);
  if (sourceSpec) {
    if (returnScreen === "home") {
      nowPlayingController.enterFromHome({
        returnScreen: "home",
        audioState: audioEngine.getState(),
      });
    } else {
      nowPlayingController.enterFromLibrary({
        returnScreen: "library",
        audioState: audioEngine.getState(),
      });
    }
    nowPlayingController.step(NOW_PLAYING_CONSTANTS.OPEN_DURATION_SECONDS);
  }

  return {
    audioEngine,
    nowPlayingController,
    catalog,
    callbackLogs,
  };
}

describe("audio_engine.js play modes, queue selection, and metadata preservation", () => {
  test("PLAY_MODES and cyclePlayMode cycle canonically while setPlayMode ignores invalid values", () => {
    assert.deepEqual(PLAY_MODES, ["sequential", "shuffle", "repeat-all", "repeat-one"]);

    const audioEngine = createAudioEngine();
    assert.equal(audioEngine.getState().playMode, "sequential");

    audioEngine.cyclePlayMode();
    assert.equal(audioEngine.getState().playMode, "shuffle");
    audioEngine.cyclePlayMode();
    assert.equal(audioEngine.getState().playMode, "repeat-all");
    audioEngine.cyclePlayMode();
    assert.equal(audioEngine.getState().playMode, "repeat-one");
    audioEngine.cyclePlayMode();
    assert.equal(audioEngine.getState().playMode, "sequential");

    audioEngine.setPlayMode("repeat-all");
    assert.equal(audioEngine.getState().playMode, "repeat-all");

    for (const invalidMode of ["unknown", "", null, undefined, 42, {}]) {
      audioEngine.setPlayMode(invalidMode);
      assert.equal(audioEngine.getState().playMode, "repeat-all");
    }
  });

  test("selectQueueIndex jumps cleanly, resets elapsedSeconds, clamps bounds, and guards empty queues", () => {
    const emptyEngine = createAudioEngine();
    emptyEngine.selectQueueIndex(0);
    emptyEngine.selectQueueIndex(-2);
    emptyEngine.selectQueueIndex(9);
    emptyEngine.selectQueueIndex(Number.NaN);
    assert.equal(emptyEngine.getState().playbackState, "idle");
    assert.equal(emptyEngine.getState().activeTrack, null);

    const stubAudio = createMockAudioStub();
    const audioEngine = createAudioEngine({ createAudioElement: () => stubAudio });
    const catalog = buildNowPlayingTestCatalog();
    audioEngine.playTrackFromSource({ trackId: "t1", queueSource: "all-songs" }, catalog);

    audioEngine.seekTo(48);
    assert.equal(audioEngine.getState().elapsedSeconds, 48);

    audioEngine.selectQueueIndex(2);
    const jumpedState = audioEngine.getState();
    assert.equal(jumpedState.queueIndex, 2);
    assert.equal(jumpedState.activeTrack.id, "t3");
    assert.equal(jumpedState.activeAlbum.id, "a2");
    assert.equal(jumpedState.elapsedSeconds, 0);
    assert.equal(stubAudio.currentTime, 0);
    assert.equal(stubAudio.src, "/api/audio/t3");

    audioEngine.selectQueueIndex(-10);
    assert.equal(audioEngine.getState().queueIndex, 0);
    assert.equal(audioEngine.getState().activeTrack.id, "t1");

    audioEngine.selectQueueIndex(999);
    assert.equal(audioEngine.getState().queueIndex, 4);
    assert.equal(audioEngine.getState().activeTrack.id, "t5");

    audioEngine.selectQueueIndex(Number.NaN);
    assert.equal(audioEngine.getState().queueIndex, 4);
  });

  test("updateTrackFavorite updates activeTrack and queueTracks without throwing on invalid ids", () => {
    const audioEngine = createAudioEngine();
    const catalog = buildNowPlayingTestCatalog();
    audioEngine.playTrackFromSource({ trackId: "t1", queueSource: "all-songs" }, catalog);

    assert.equal(audioEngine.getState().activeTrack.isFavorite, false);
    audioEngine.updateTrackFavorite("t1", true);
    assert.equal(audioEngine.getState().activeTrack.isFavorite, true);
    assert.equal(
      audioEngine.getState().queueTracks.find((trackEntry) => trackEntry.id === "t1").isFavorite,
      true,
    );

    audioEngine.updateTrackFavorite("t2", false);
    assert.equal(
      audioEngine.getState().queueTracks.find((trackEntry) => trackEntry.id === "t2").isFavorite,
      false,
    );
    assert.equal(audioEngine.getState().activeTrack.isFavorite, true);

    audioEngine.updateTrackFavorite("nonexistent", true);
    audioEngine.updateTrackFavorite(null, true);
    assert.equal(audioEngine.getState().activeTrack.id, "t1");
  });

  test("playTrackFromSource preserves queue context fields and track technical metadata", () => {
    const audioEngine = createAudioEngine();
    const catalog = buildNowPlayingTestCatalog();

    audioEngine.playTrackFromSource({ trackId: "t1", queueSource: "album", albumId: "a1" }, catalog);
    const albumState = audioEngine.getState();
    assert.equal(albumState.queueAlbumId, "a1");
    assert.equal(albumState.queuePlaylistId, null);
    assert.equal(albumState.queueGenreTitle, null);
    assert.equal(albumState.activeTrack.format, "FLAC 24-BIT");
    assert.equal(albumState.activeTrack.sampleRate, "96.0 KHZ");
    assert.equal(albumState.activeTrack.bitrate, "2,822 KBPS");
    assert.equal(albumState.activeTrack.fileSize, "42.3 MB");
    assert.equal(albumState.activeTrack.year, 2024);
    assert.equal(albumState.activeTrack.trackNumber, "01");
    assert.equal(albumState.activeTrack.isFavorite, false);

    audioEngine.playTrackFromSource(
      { trackId: "t2", queueSource: "playlist", playlistId: "p1" },
      catalog,
    );
    const playlistState = audioEngine.getState();
    assert.equal(playlistState.queuePlaylistId, "p1");
    assert.equal(playlistState.activeTrack.isFavorite, true);

    audioEngine.playTrackFromSource(
      {
        trackId: "t3",
        queueSource: "genre",
        genreTitle: "Jazz",
        genreAlbumIds: ["a2"],
      },
      catalog,
    );
    const genreState = audioEngine.getState();
    assert.equal(genreState.queueGenreTitle, "Jazz");
    assert.deepEqual(genreState.queueGenreAlbumIds, ["a2"]);
  });

  test("end-of-track handling follows playMode across both DOM ended events and headless step()", () => {
    const catalog = buildNowPlayingTestCatalog();

    // 1. DOM ended event paths across repeat-one, sequential, repeat-all, and shuffle
    const stubAudio = createMockAudioStub();
    const domEngine = createAudioEngine({ createAudioElement: () => stubAudio });
    domEngine.playTrackFromSource({ trackId: "t1", queueSource: "album", albumId: "a1" }, catalog);

    domEngine.setPlayMode("repeat-one");
    domEngine.seekTo(120);
    stubAudio.emit("ended");
    assert.equal(domEngine.getState().queueIndex, 0);
    assert.equal(domEngine.getState().activeTrack.id, "t1");
    assert.equal(domEngine.getState().elapsedSeconds, 0);
    assert.equal(domEngine.getState().isPlaying, true);

    domEngine.setPlayMode("sequential");
    stubAudio.emit("ended");
    assert.equal(domEngine.getState().queueIndex, 1);
    assert.equal(domEngine.getState().activeTrack.id, "t2");
    assert.equal(domEngine.getState().isPlaying, true);

    stubAudio.emit("ended");
    assert.equal(domEngine.getState().isPlaying, false);
    assert.equal(domEngine.getState().playbackState, "paused");

    domEngine.play();
    domEngine.setPlayMode("repeat-all");
    stubAudio.emit("ended");
    assert.equal(domEngine.getState().queueIndex, 0);
    assert.equal(domEngine.getState().activeTrack.id, "t1");
    assert.equal(domEngine.getState().elapsedSeconds, 0);
    assert.equal(domEngine.getState().isPlaying, true);

    // Shuffle across multi-track queue never repeats the current index consecutively
    domEngine.playTrackFromSource({ trackId: "t1", queueSource: "all-songs" }, catalog);
    domEngine.setPlayMode("shuffle");
    let previousDomIndex = domEngine.getState().queueIndex;
    for (let roundIndex = 0; roundIndex < SHUFFLE_TRIAL_ROUNDS; roundIndex += 1) {
      stubAudio.emit("ended");
      const shuffledState = domEngine.getState();
      assert.equal(shuffledState.isPlaying, true);
      assert.ok(shuffledState.queueIndex >= 0 && shuffledState.queueIndex < shuffledState.queueTracks.length);
      assert.notEqual(shuffledState.queueIndex, previousDomIndex);
      previousDomIndex = shuffledState.queueIndex;
    }

    // 2. Headless step() reaching durationSeconds across all four play modes
    const headlessEngine = createAudioEngine();
    headlessEngine.playTrackFromSource({ trackId: "t1", queueSource: "album", albumId: "a1" }, catalog);

    headlessEngine.setPlayMode("repeat-one");
    headlessEngine.step(180);
    assert.equal(headlessEngine.getState().queueIndex, 0);
    assert.equal(headlessEngine.getState().elapsedSeconds, 0);
    assert.equal(headlessEngine.getState().isPlaying, true);

    headlessEngine.setPlayMode("sequential");
    headlessEngine.step(180);
    assert.equal(headlessEngine.getState().queueIndex, 1);
    assert.equal(headlessEngine.getState().activeTrack.id, "t2");
    assert.equal(headlessEngine.getState().isPlaying, true);

    headlessEngine.step(200);
    assert.equal(headlessEngine.getState().isPlaying, false);
    assert.equal(headlessEngine.getState().playbackState, "paused");

    headlessEngine.play();
    headlessEngine.setPlayMode("repeat-all");
    headlessEngine.step(200);
    assert.equal(headlessEngine.getState().queueIndex, 0);
    assert.equal(headlessEngine.getState().elapsedSeconds, 0);
    assert.equal(headlessEngine.getState().isPlaying, true);

    headlessEngine.playTrackFromSource({ trackId: "t1", queueSource: "all-songs" }, catalog);
    headlessEngine.setPlayMode("shuffle");
    let previousHeadlessIndex = headlessEngine.getState().queueIndex;
    for (let roundIndex = 0; roundIndex < SHUFFLE_TRIAL_ROUNDS; roundIndex += 1) {
      const currentDuration = headlessEngine.getState().durationSeconds;
      headlessEngine.step(currentDuration);
      const nextHeadlessState = headlessEngine.getState();
      assert.equal(nextHeadlessState.isPlaying, true);
      assert.equal(nextHeadlessState.elapsedSeconds, 0);
      assert.ok(
        nextHeadlessState.queueIndex >= 0 &&
          nextHeadlessState.queueIndex < nextHeadlessState.queueTracks.length,
      );
      assert.notEqual(nextHeadlessState.queueIndex, previousHeadlessIndex);
      previousHeadlessIndex = nextHeadlessState.queueIndex;
    }

    // Single-track queue under shuffle remains stable on queueIndex 0
    const singleTrackCatalog = buildNowPlayingTestCatalog({
      albums: [
        {
          id: "single-album",
          title: "Solo",
          artist: "Solo Artist",
          genre: "Ambient",
          year: 2024,
          tracks: [catalog.allTracks[0]],
        },
      ],
      allTracks: [catalog.allTracks[0]],
    });
    headlessEngine.playTrackFromSource(
      { trackId: "t1", queueSource: "album", albumId: "single-album" },
      singleTrackCatalog,
    );
    headlessEngine.setPlayMode("shuffle");
    headlessEngine.step(180);
    assert.equal(headlessEngine.getState().queueIndex, 0);
    assert.equal(headlessEngine.getState().isPlaying, true);
  });

  test("defers seekTo until loadedmetadata when readyState is 0 and locks clock to attached audio element", () => {
    const catalog = buildNowPlayingTestCatalog();
    const stubAudio = createMockAudioStub();
    stubAudio.readyState = 0;

    const audioEngine = createAudioEngine({ createAudioElement: () => stubAudio });
    audioEngine.playTrackFromSource({ trackId: "t1", queueSource: "album", albumId: "a1" }, catalog);

    // Attached element at currentTime === 0 must not drift synthetically during buffering or pause
    audioEngine.step(0.5);
    assert.equal(audioEngine.getState().elapsedSeconds, 0);

    stubAudio.paused = true;
    audioEngine.step(0.5);
    assert.equal(audioEngine.getState().elapsedSeconds, 0);

    // Seeking while readyState === 0 defers audioElement.currentTime until loadedmetadata
    audioEngine.seekTo(42);
    assert.equal(audioEngine.getState().elapsedSeconds, 42);
    assert.equal(stubAudio.currentTime, 0);

    audioEngine.step(0.5);
    assert.equal(audioEngine.getState().elapsedSeconds, 42);
    assert.equal(stubAudio.currentTime, 0);

    stubAudio.readyState = 1;
    stubAudio.duration = 180;
    stubAudio.emit("loadedmetadata");
    assert.equal(stubAudio.currentTime, 42);
    assert.equal(audioEngine.getState().elapsedSeconds, 42);

    // Deferred seek also clamps against updated loadedmetadata duration and resets on track switch
    stubAudio.readyState = 0;
    stubAudio.currentTime = 0;
    audioEngine.seekTo(160);
    assert.equal(audioEngine.getState().elapsedSeconds, 160);
    assert.equal(stubAudio.currentTime, 0);

    stubAudio.readyState = 1;
    stubAudio.duration = 120;
    stubAudio.emit("loadedmetadata");
    assert.equal(stubAudio.currentTime, 120);
    assert.equal(audioEngine.getState().elapsedSeconds, 120);

    // Pending seek is cleared when loading another track via skipTrack or playTrackFromSource
    stubAudio.readyState = 0;
    audioEngine.seekTo(55);
    audioEngine.skipTrack(1);
    stubAudio.readyState = 1;
    stubAudio.duration = 200;
    stubAudio.emit("loadedmetadata");
    assert.equal(stubAudio.currentTime, 0);
    assert.equal(audioEngine.getState().elapsedSeconds, 0);

    // Live playback syncs elapsedSeconds from attached element once currentTime > 0
    stubAudio.paused = false;
    stubAudio.currentTime = 17.5;
    audioEngine.step(0.5);
    assert.equal(audioEngine.getState().elapsedSeconds, 17.5);
  });
});

describe("now_playing_controller.js constants and pure helpers", () => {
  test("exports exact NOW_PLAYING_CONSTANTS, play modes, quick buttons, and lyrics modes", () => {
    assert.equal(NOW_PLAYING_CONSTANTS.OPEN_DURATION_SECONDS, 0.78);
    assert.equal(NOW_PLAYING_CONSTANTS.SKIP_DURATION_SECONDS, 0.42);
    assert.equal(NOW_PLAYING_CONSTANTS.QUICK_ACTIONS_TIMEOUT_SECONDS, 4.6);
    assert.equal(NOW_PLAYING_CONSTANTS.VOLUME_HOLD_SECONDS, 1.45);
    assert.equal(NOW_PLAYING_CONSTANTS.SPINDLE_BASE_RADIUS_PX, 27);
    assert.equal(NOW_PLAYING_CONSTANTS.SPINDLE_SEEK_RADIUS_PX, 62);
    assert.equal(NOW_PLAYING_CONSTANTS.SEEK_HOLD_RADIUS_PX, 124);
    assert.equal(NOW_PLAYING_CONSTANTS.SEEK_HOLD_DURATION_SECONDS, 0.42);
    assert.equal(NOW_PLAYING_CONSTANTS.VOLUME_LIMIT_RATIO, 0.85);
    assert.equal(NOW_PLAYING_CONSTANTS.DOUBLE_TAP_WINDOW_SECONDS, 0.25);
    assert.equal(NOW_PLAYING_CONSTANTS.QUEUE_ROW_PITCH_PX, 58);
    assert.equal(NOW_PLAYING_CONSTANTS.QUEUE_ROW_HEIGHT_PX, 48);
    assert.equal(NOW_PLAYING_CONSTANTS.QUEUE_TOP_Y, 136);
    assert.equal(NOW_PLAYING_CONSTANTS.LYRICS_TRANSITION_DURATION_SECONDS, 0.44);
    assert.equal(NOW_PLAYING_CONSTANTS.SKIP_DRAG_COMMIT_PX, 56);
    assert.equal(NOW_PLAYING_CONSTANTS.QUEUE_PULL_COMMIT_DELTA_Y, -38);
    assert.equal(NOW_PLAYING_CONSTANTS.BACK_SWIPE_DISTANCE_PX, 96);
    assert.equal(NOW_PLAYING_CONSTANTS.BACK_SWIPE_COMMIT_THRESHOLD, 0.45);
    assert.equal(NOW_PLAYING_CONSTANTS.TAP_DRIFT_LIMIT_PX, 10);

    assert.deepEqual(
      NOW_PLAYING_PLAY_MODES.map((modeEntry) => modeEntry.id),
      ["sequential", "shuffle", "repeat-all", "repeat-one"],
    );
    for (const modeEntry of NOW_PLAYING_PLAY_MODES) {
      assert.equal(typeof modeEntry.label, "string");
      assert.ok(modeEntry.label.length > 0);
    }

    assert.deepEqual(
      NOW_PLAYING_QUICK_BUTTONS.map((buttonEntry) => ({
        id: buttonEntry.id,
        centerX: buttonEntry.centerX,
        centerY: buttonEntry.centerY,
      })),
      [
        { id: "favorite", centerX: QUICK_FAVORITE_X, centerY: QUICK_FAVORITE_Y },
        { id: "play-mode", centerX: QUICK_PLAY_MODE_X, centerY: QUICK_PLAY_MODE_Y },
        { id: "playlist", centerX: QUICK_PLAYLIST_X, centerY: QUICK_PLAYLIST_Y },
        { id: "lyrics", centerX: QUICK_LYRICS_X, centerY: QUICK_LYRICS_Y },
        { id: "info", centerX: QUICK_DETAILS_X, centerY: QUICK_DETAILS_Y },
      ],
    );
    for (const buttonEntry of NOW_PLAYING_QUICK_BUTTONS) {
      assert.equal(typeof buttonEntry.label, "string");
      assert.ok(buttonEntry.label.length > 0);
      assert.ok(Number.isFinite(buttonEntry.radius) && buttonEntry.radius > 0);
    }

    assert.deepEqual(NOW_PLAYING_LYRICS_MODES, ["auto", "timed", "untimed", "none"]);
  });

  test("computeNowPlayingQueueRowLayout and hitTestNowPlayingQueueRow calculate finite geometry and row hits", () => {
    const firstRowLayout = computeNowPlayingQueueRowLayout(0, 0);
    const secondRowLayout = computeNowPlayingQueueRowLayout(1, 0);
    const distantRowLayout = computeNowPlayingQueueRowLayout(6, 0);

    assert.equal(firstRowLayout.centerX, DISPLAY_CENTER_PX);
    assert.equal(firstRowLayout.centerY, NOW_PLAYING_CONSTANTS.QUEUE_TOP_Y);
    assert.equal(firstRowLayout.height, NOW_PLAYING_CONSTANTS.QUEUE_ROW_HEIGHT_PX);
    assert.ok(firstRowLayout.width > 0);
    assert.equal(firstRowLayout.alpha, 1);

    assert.equal(
      secondRowLayout.centerY - firstRowLayout.centerY,
      NOW_PLAYING_CONSTANTS.QUEUE_ROW_PITCH_PX,
    );

    for (const rowLayout of [firstRowLayout, secondRowLayout, distantRowLayout]) {
      assert.ok(Number.isFinite(rowLayout.centerX));
      assert.ok(Number.isFinite(rowLayout.centerY));
      assert.ok(Number.isFinite(rowLayout.width) && rowLayout.width > 0);
      assert.ok(Number.isFinite(rowLayout.height) && rowLayout.height > 0);
      assert.ok(Number.isFinite(rowLayout.alpha) && rowLayout.alpha >= 0 && rowLayout.alpha <= 1);
    }

    const sampleTracks = buildNowPlayingTestCatalog().allTracks;
    assert.equal(
      hitTestNowPlayingQueueRow(firstRowLayout.centerX, firstRowLayout.centerY, 0, sampleTracks),
      0,
    );
    assert.equal(
      hitTestNowPlayingQueueRow(secondRowLayout.centerX, secondRowLayout.centerY, 0, sampleTracks),
      1,
    );
    assert.equal(hitTestNowPlayingQueueRow(DISPLAY_CENTER_PX, 24, 0, sampleTracks), null);
    assert.equal(hitTestNowPlayingQueueRow(12, DISPLAY_CENTER_PX, 0, sampleTracks), null);
    assert.equal(hitTestNowPlayingQueueRow(firstRowLayout.centerX, firstRowLayout.centerY, 0, []), null);
    assert.equal(hitTestNowPlayingQueueRow(Number.NaN, firstRowLayout.centerY, 0, sampleTracks), null);
  });

  test("resolveTrackLyricsSource and resolveActiveLyricsFrame handle timed, untimed, and missing lyrics", () => {
    const catalog = buildNowPlayingTestCatalog();
    const timedTrack = catalog.allTracks[0];
    const timedEntry = catalog.lyricsCatalog.t1;
    const untimedTrack = catalog.allTracks[1];
    const untimedEntry = catalog.lyricsCatalog.t2;

    assert.equal(resolveTrackLyricsSource(timedTrack, timedEntry, "auto").kind, "timed");
    assert.equal(resolveTrackLyricsSource(timedTrack, timedEntry, "untimed").kind, "untimed");
    assert.equal(resolveTrackLyricsSource(timedTrack, timedEntry, "none").kind, "none");
    assert.equal(resolveTrackLyricsSource(timedTrack, null, "auto").kind, "none");

    const introFrame = resolveActiveLyricsFrame(timedTrack, timedEntry, 2.0, "auto");
    assert.equal(introFrame.kind, "timed");
    assert.equal(introFrame.isIntroCue, true);
    assert.equal(introFrame.upcomingText, "First light across the vinyl");

    const cueZeroBoundaryFrame = resolveActiveLyricsFrame(timedTrack, timedEntry, 5.0, "auto");
    assert.equal(cueZeroBoundaryFrame.kind, "timed");
    assert.equal(cueZeroBoundaryFrame.isIntroCue, false);
    assert.equal(cueZeroBoundaryFrame.currentText, "First light across the vinyl");
    assert.equal(cueZeroBoundaryFrame.upcomingText, "Spinning quietly in zero gravity");

    const cueOneBoundaryFrame = resolveActiveLyricsFrame(timedTrack, timedEntry, 12.5, "auto");
    assert.equal(cueOneBoundaryFrame.currentText, "Spinning quietly in zero gravity");
    assert.equal(cueOneBoundaryFrame.upcomingText, "Until the final groove fades");

    const finalCueFrame = resolveActiveLyricsFrame(timedTrack, timedEntry, 21.0, "auto");
    assert.equal(finalCueFrame.currentText, "Until the final groove fades");
    assert.equal(finalCueFrame.upcomingText, "");

    const untimedStartFrame = resolveActiveLyricsFrame(untimedTrack, untimedEntry, 0, "auto");
    assert.equal(untimedStartFrame.kind, "untimed");
    assert.equal(untimedStartFrame.currentText, "Evening shadows settle softly");

    const untimedMidFrame = resolveActiveLyricsFrame(untimedTrack, untimedEntry, 110, "auto");
    assert.equal(untimedMidFrame.kind, "untimed");
    assert.notEqual(untimedMidFrame.currentText, untimedStartFrame.currentText);

    const untimedEndFrame = resolveActiveLyricsFrame(untimedTrack, untimedEntry, 195, "auto");
    assert.equal(untimedEndFrame.currentText, "Night unfolds across the turntable");
    assert.equal(untimedEndFrame.upcomingText, "");

    const missingFrame = resolveActiveLyricsFrame(timedTrack, null, 10, "auto");
    assert.equal(missingFrame.kind, "none");
    assert.equal(missingFrame.modeLabel, "NO LYRICS");
    assert.equal(missingFrame.note, "NO EMBEDDED OR .LRC FILE");

    const forcedNoneFrame = resolveActiveLyricsFrame(timedTrack, timedEntry, 10, "none");
    assert.equal(forcedNoneFrame.kind, "none");
    assert.equal(forcedNoneFrame.modeLabel, "NO LYRICS");
    assert.equal(forcedNoneFrame.note, "NO EMBEDDED OR .LRC FILE");
  });

  test("enrichTrackDetails preserves tech specs, derives trackIndexLabel, and handles missing inputs", () => {
    const catalog = buildNowPlayingTestCatalog();
    const trackEntry = catalog.allTracks[0];
    const albumEntry = catalog.albums[0];

    const enrichedDetails = enrichTrackDetails(trackEntry, albumEntry, 8, 2);
    assert.equal(enrichedDetails.format, "FLAC 24-BIT");
    assert.equal(enrichedDetails.sampleRate, "96.0 KHZ");
    assert.equal(enrichedDetails.bitrate, "2,822 KBPS");
    assert.equal(enrichedDetails.fileSize, "42.3 MB");
    assert.equal(typeof enrichedDetails.trackIndexLabel, "string");
    assert.ok(enrichedDetails.trackIndexLabel.includes("2"));
    assert.ok(enrichedDetails.trackIndexLabel.includes("8"));

    const fallbackDetails = enrichTrackDetails(null, null, 0, 0);
    assert.equal(typeof fallbackDetails.format, "string");
    assert.equal(typeof fallbackDetails.sampleRate, "string");
    assert.equal(typeof fallbackDetails.bitrate, "string");
    assert.equal(typeof fallbackDetails.fileSize, "string");
    assert.equal(typeof fallbackDetails.trackIndexLabel, "string");
  });
});

describe("createNowPlayingController entry, operating modes, and tap gestures", () => {
  test("starts idle, invokes onBrowseLibrary on CTA tap and activateFocused, and enters playing with bloom", () => {
    const idleHarness = createNowPlayingTestHarness({ sourceSpec: null });
    const defaultSnapshot = idleHarness.nowPlayingController.getRenderSnapshot();
    assert.equal(defaultSnapshot.operatingMode, "idle");
    assert.equal(defaultSnapshot.quickActionsOpen, false);
    assert.equal(defaultSnapshot.queueSheetOpen, false);
    assert.equal(defaultSnapshot.detailsSheetOpen, false);
    assert.equal(defaultSnapshot.playlistPickerOpen, false);
    assert.equal(defaultSnapshot.lyricsOpen, false);

    tapPoint(idleHarness.nowPlayingController, IDLE_BROWSE_CTA_X, IDLE_BROWSE_CTA_Y);
    assert.equal(idleHarness.callbackLogs.browseLibraryCalls, 1);

    idleHarness.nowPlayingController.activateFocused();
    assert.equal(idleHarness.callbackLogs.browseLibraryCalls, 2);

    idleHarness.nowPlayingController.enterFromHome({
      returnScreen: "home",
      audioState: idleHarness.audioEngine.getState(),
    });
    assert.equal(idleHarness.nowPlayingController.getRenderSnapshot().operatingMode, "idle");
    assert.equal(idleHarness.nowPlayingController.getRenderSnapshot().returnScreen, "home");

    idleHarness.audioEngine.playTrackFromSource(
      { trackId: "t1", queueSource: "album", albumId: "a1" },
      idleHarness.catalog,
    );
    idleHarness.nowPlayingController.enterFromLibrary({
      returnScreen: "library",
      audioState: idleHarness.audioEngine.getState(),
    });
    const entryStartSnapshot = idleHarness.nowPlayingController.getRenderSnapshot();
    assert.equal(entryStartSnapshot.operatingMode, "playing");
    assert.equal(entryStartSnapshot.returnScreen, "library");
    assert.equal(entryStartSnapshot.openTransitionProgress, 0);
    assert.equal(entryStartSnapshot.quickActionsOpen, false);
    assert.equal(entryStartSnapshot.queueSheetOpen, false);
    assert.equal(entryStartSnapshot.detailsSheetOpen, false);
    assert.equal(entryStartSnapshot.playlistPickerOpen, false);
    assert.equal(entryStartSnapshot.lyricsOpen, false);

    const halfBloom = NOW_PLAYING_CONSTANTS.OPEN_DURATION_SECONDS / 2;
    idleHarness.nowPlayingController.step(halfBloom);
    const midBloomSnapshot = idleHarness.nowPlayingController.getRenderSnapshot();
    assert.ok(midBloomSnapshot.openTransitionProgress > 0 && midBloomSnapshot.openTransitionProgress < 1);

    idleHarness.nowPlayingController.step(halfBloom);
    assert.equal(idleHarness.nowPlayingController.getRenderSnapshot().openTransitionProgress, 1);
  });

  test("distinguishes double-tap play/pause from single-tap Quick Actions with 4.6s auto-dismiss", () => {
    const { nowPlayingController, callbackLogs } = createNowPlayingTestHarness();

    tapPoint(nowPlayingController, VINYL_CENTER_TAP_X, VINYL_CENTER_TAP_Y);
    nowPlayingController.step(0.10);
    tapPoint(nowPlayingController, VINYL_CENTER_TAP_X, VINYL_CENTER_TAP_Y);

    const pausedSnapshot = nowPlayingController.getRenderSnapshot();
    assert.equal(callbackLogs.togglePlayPauseCalls, 1);
    assert.equal(pausedSnapshot.playPausePulseAlpha, 1);
    assert.equal(pausedSnapshot.toastText, "PAUSED");
    assert.equal(pausedSnapshot.quickActionsOpen, false);

    nowPlayingController.step(0.30);
    assert.equal(nowPlayingController.getRenderSnapshot().quickActionsOpen, false);

    tapPoint(nowPlayingController, VINYL_CENTER_TAP_X, VINYL_CENTER_TAP_Y);
    nowPlayingController.step(0.10);
    tapPoint(nowPlayingController, VINYL_CENTER_TAP_X, VINYL_CENTER_TAP_Y);
    assert.equal(callbackLogs.togglePlayPauseCalls, 2);
    assert.equal(nowPlayingController.getRenderSnapshot().toastText, "PLAYING");
    assert.equal(nowPlayingController.getRenderSnapshot().quickActionsOpen, false);

    tapPoint(nowPlayingController, VINYL_CENTER_TAP_X, VINYL_CENTER_TAP_Y);
    nowPlayingController.step(NOW_PLAYING_CONSTANTS.DOUBLE_TAP_WINDOW_SECONDS + 0.03);
    assert.equal(nowPlayingController.getRenderSnapshot().quickActionsOpen, true);

    nowPlayingController.step(NOW_PLAYING_CONSTANTS.QUICK_ACTIONS_TIMEOUT_SECONDS - 0.5);
    assert.equal(nowPlayingController.getRenderSnapshot().quickActionsOpen, true);

    nowPlayingController.step(0.6);
    assert.equal(nowPlayingController.getRenderSnapshot().quickActionsOpen, false);
  });
});

describe("createNowPlayingController seek, volume arc, and wheel controls", () => {
  test("long-press activates seek mode, scrubs angularly and near spindle clamped to duration, and collapses on release", () => {
    const { audioEngine, nowPlayingController, callbackLogs } = createNowPlayingTestHarness();
    audioEngine.seekTo(20);
    nowPlayingController.syncAudioState(audioEngine.getState());

    nowPlayingController.handlePointerDown(SEEK_HOLD_POINT_X, SEEK_HOLD_POINT_Y);
    nowPlayingController.step(0.20);
    const chargingSnapshot = nowPlayingController.getRenderSnapshot();
    assert.ok(chargingSnapshot.seekHoldProgress > 0 && chargingSnapshot.seekHoldProgress < 1);
    assert.equal(chargingSnapshot.isSeeking, false);

    nowPlayingController.step(0.25);
    const activeSeekSnapshot = nowPlayingController.getRenderSnapshot();
    assert.equal(activeSeekSnapshot.isSeeking, true);
    assert.equal(activeSeekSnapshot.pointerMode, "seek");

    nowPlayingController.step(0.15);
    assert.ok(nowPlayingController.getRenderSnapshot().seekExpandProgress > 0);

    nowPlayingController.handlePointerMove(SEEK_ARC_THREE_OCLOCK_X, SEEK_ARC_THREE_OCLOCK_Y);
    nowPlayingController.handlePointerMove(SEEK_ARC_SIX_OCLOCK_X, SEEK_ARC_SIX_OCLOCK_Y);
    const arcScrubbedSnapshot = nowPlayingController.getRenderSnapshot();
    assert.ok(callbackLogs.seekTargets.length > 0);
    assert.ok(arcScrubbedSnapshot.trackElapsedSeconds > 20);
    assert.ok(arcScrubbedSnapshot.trackElapsedSeconds <= 180);

    nowPlayingController.handlePointerUp(SEEK_ARC_SIX_OCLOCK_X, SEEK_ARC_SIX_OCLOCK_Y);
    assert.equal(nowPlayingController.getRenderSnapshot().isSeeking, false);

    advanceSteps(nowPlayingController, STEP_DELTA_SECONDS, SETTLE_STEP_COUNT);
    assert.equal(nowPlayingController.getRenderSnapshot().seekExpandProgress, 0);

    // Near-spindle horizontal scrub clamped within [0, durationSeconds]
    const beforeSpindleElapsed = nowPlayingController.getRenderSnapshot().trackElapsedSeconds;
    nowPlayingController.handlePointerDown(SEEK_SPINDLE_START_X, SEEK_SPINDLE_Y);
    nowPlayingController.step(NOW_PLAYING_CONSTANTS.SEEK_HOLD_DURATION_SECONDS + 0.03);
    assert.equal(nowPlayingController.getRenderSnapshot().isSeeking, true);

    nowPlayingController.handlePointerMove(DISPLAY_CENTER_PX, SEEK_SPINDLE_Y);
    nowPlayingController.handlePointerMove(SEEK_SPINDLE_END_X, SEEK_SPINDLE_Y);
    const spindleScrubbedSnapshot = nowPlayingController.getRenderSnapshot();
    assert.ok(spindleScrubbedSnapshot.trackElapsedSeconds > beforeSpindleElapsed);
    assert.ok(
      spindleScrubbedSnapshot.trackElapsedSeconds >= 0 &&
        spindleScrubbedSnapshot.trackElapsedSeconds <= 180,
    );

    nowPlayingController.handlePointerMove(-400, SEEK_SPINDLE_Y);
    assert.ok(nowPlayingController.getRenderSnapshot().trackElapsedSeconds >= 0);

    nowPlayingController.handlePointerMove(900, SEEK_SPINDLE_Y);
    assert.ok(nowPlayingController.getRenderSnapshot().trackElapsedSeconds <= 180);

    nowPlayingController.handlePointerUp(900, SEEK_SPINDLE_Y);
    assert.equal(nowPlayingController.getRenderSnapshot().isSeeking, false);
  });

  test("right-edge bezel arc adjusts volume clamped to [0, 1], holds HUD for 1.45s, and wheel adapts to active layer", () => {
    const { nowPlayingController, callbackLogs } = createNowPlayingTestHarness();
    const initialVolume = nowPlayingController.getRenderSnapshot().volumeLevel;

    nowPlayingController.handlePointerDown(VOLUME_BEZEL_X, VOLUME_BEZEL_START_Y);
    nowPlayingController.handlePointerMove(VOLUME_BEZEL_X, VOLUME_BEZEL_UP_END_Y);
    const raisedSnapshot = nowPlayingController.getRenderSnapshot();
    assert.ok(raisedSnapshot.volumeLevel > initialVolume);
    assert.ok(raisedSnapshot.volumeLevel <= 1);
    assert.equal(raisedSnapshot.volumeHudAlpha, 1);
    assert.ok(callbackLogs.volumeChanges.length >= 1);

    nowPlayingController.handlePointerMove(VOLUME_BEZEL_X, VOLUME_BEZEL_MAX_UP_Y);
    assert.ok(nowPlayingController.getRenderSnapshot().volumeLevel <= 1);

    nowPlayingController.handlePointerMove(VOLUME_BEZEL_X, VOLUME_BEZEL_MAX_DOWN_Y);
    assert.ok(nowPlayingController.getRenderSnapshot().volumeLevel >= 0);

    nowPlayingController.handlePointerMove(VOLUME_BEZEL_X, VOLUME_BEZEL_UP_END_Y);
    nowPlayingController.handlePointerUp(VOLUME_BEZEL_X, VOLUME_BEZEL_UP_END_Y);

    nowPlayingController.step(1.0);
    assert.ok(nowPlayingController.getRenderSnapshot().volumeHudAlpha > 0);

    nowPlayingController.step(1.2);
    assert.equal(nowPlayingController.getRenderSnapshot().volumeHudAlpha, 0);

    const beforeWheelVolume = nowPlayingController.getRenderSnapshot().volumeLevel;
    nowPlayingController.handleWheel(-120);
    const afterWheelUpVolume = nowPlayingController.getRenderSnapshot().volumeLevel;
    assert.ok(afterWheelUpVolume > beforeWheelVolume || Math.abs(beforeWheelVolume - 1) < FLOAT_TOLERANCE);

    nowPlayingController.handleWheel(120);
    assert.ok(nowPlayingController.getRenderSnapshot().volumeLevel < afterWheelUpVolume);

    // Wheel scrolls Queue Sheet instead of altering volume when Queue Sheet is open
    nowPlayingController.handlePointerDown(DISPLAY_CENTER_PX, QUEUE_PULL_START_Y);
    nowPlayingController.handlePointerMove(DISPLAY_CENTER_PX, QUEUE_PULL_END_Y);
    nowPlayingController.handlePointerUp(DISPLAY_CENTER_PX, QUEUE_PULL_END_Y);
    assert.equal(nowPlayingController.getRenderSnapshot().queueSheetOpen, true);

    const queueVolumeBeforeWheel = nowPlayingController.getRenderSnapshot().volumeLevel;
    const queueScrollBeforeWheel = nowPlayingController.getRenderSnapshot().queueScrollPosition;
    nowPlayingController.handleWheel(120);
    advanceSteps(nowPlayingController, STEP_DELTA_SECONDS, SETTLE_STEP_COUNT);
    const queueAfterWheelSnapshot = nowPlayingController.getRenderSnapshot();
    assert.equal(queueAfterWheelSnapshot.volumeLevel, queueVolumeBeforeWheel);
    assert.ok(queueAfterWheelSnapshot.queueScrollPosition > queueScrollBeforeWheel);
  });
});

describe("createNowPlayingController skip transitions, quick actions, and queue sheet", () => {
  test("triggers same-cover banner on intra-album skip and vinyl crossfade on cross-album skip", () => {
    const { nowPlayingController, callbackLogs } = createNowPlayingTestHarness({
      sourceSpec: { trackId: "t1", queueSource: "all-songs" },
    });

    // Swipe left (t1 -> t2, same albumId "a1")
    nowPlayingController.handlePointerDown(SKIP_LEFT_START_X, DISPLAY_CENTER_PX);
    nowPlayingController.handlePointerMove(SKIP_LEFT_END_X, DISPLAY_CENTER_PX);
    nowPlayingController.handlePointerUp(SKIP_LEFT_END_X, DISPLAY_CENTER_PX);

    assert.deepEqual(callbackLogs.skipDirections, [1]);
    const sameAlbumSnapshot = nowPlayingController.getRenderSnapshot();
    assert.equal(sameAlbumSnapshot.previousTrack, null);
    assert.equal(sameAlbumSnapshot.skipTransitionProgress, 1);
    assert.equal(sameAlbumSnapshot.trackBannerPhase, "in");
    assert.equal(sameAlbumSnapshot.trackBannerTitle.toUpperCase(), "02 DUSK");

    const observedBannerPhases = [sameAlbumSnapshot.trackBannerPhase];
    for (let stepIndex = 0; stepIndex < 90; stepIndex += 1) {
      nowPlayingController.step(STEP_DELTA_SECONDS);
      const sampledPhase = nowPlayingController.getRenderSnapshot().trackBannerPhase;
      if (observedBannerPhases[observedBannerPhases.length - 1] !== sampledPhase) {
        observedBannerPhases.push(sampledPhase);
      }
    }
    assert.ok(observedBannerPhases.includes("in"));
    assert.ok(observedBannerPhases.includes("hold"));
    assert.ok(observedBannerPhases.includes("out"));

    // skipTrack(+1) across album boundary (t2 in a1 -> t3 in a2)
    nowPlayingController.skipTrack(1);
    assert.deepEqual(callbackLogs.skipDirections, [1, 1]);
    const crossAlbumStartSnapshot = nowPlayingController.getRenderSnapshot();
    assert.ok(crossAlbumStartSnapshot.previousTrack !== null);
    assert.equal(crossAlbumStartSnapshot.previousTrack.id, "t2");
    assert.equal(crossAlbumStartSnapshot.skipTransitionProgress, 0);

    const halfSkip = NOW_PLAYING_CONSTANTS.SKIP_DURATION_SECONDS / 2;
    nowPlayingController.step(halfSkip);
    const crossAlbumMidSnapshot = nowPlayingController.getRenderSnapshot();
    assert.ok(
      crossAlbumMidSnapshot.skipTransitionProgress > 0 &&
        crossAlbumMidSnapshot.skipTransitionProgress < 1,
    );

    nowPlayingController.step(halfSkip);
    assert.equal(nowPlayingController.getRenderSnapshot().skipTransitionProgress, 1);

    // Swipe right from center-left vinyl skips backward (t3 -> t2)
    nowPlayingController.handlePointerDown(SKIP_RIGHT_START_X, DISPLAY_CENTER_PX);
    nowPlayingController.handlePointerMove(SKIP_RIGHT_END_X, DISPLAY_CENTER_PX);
    nowPlayingController.handlePointerUp(SKIP_RIGHT_END_X, DISPLAY_CENTER_PX);
    assert.deepEqual(callbackLogs.skipDirections, [1, 1, -1]);
  });

  test("Quick Actions trigger favorite, play mode, playlist picker with default crate & deduping, lyrics, and details sheet", () => {
    const emptyPlaylistsCatalog = buildNowPlayingTestCatalog({ playlists: [] });
    const { nowPlayingController, callbackLogs } = createNowPlayingTestHarness({
      catalog: emptyPlaylistsCatalog,
      sourceSpec: { trackId: "t1", queueSource: "album", albumId: "a1" },
    });

    // 1. Favorite toggle on & off
    openQuickActionsViaTap(nowPlayingController);
    tapPoint(nowPlayingController, QUICK_FAVORITE_X, QUICK_FAVORITE_Y);
    assert.deepEqual(callbackLogs.favoriteToggles, [{ trackId: "t1", isFavorite: true }]);
    assert.equal(nowPlayingController.getRenderSnapshot().toastText, "ADDED TO FAVORITES");

    tapPoint(nowPlayingController, QUICK_FAVORITE_X, QUICK_FAVORITE_Y);
    assert.deepEqual(callbackLogs.favoriteToggles, [
      { trackId: "t1", isFavorite: true },
      { trackId: "t1", isFavorite: false },
    ]);
    assert.equal(nowPlayingController.getRenderSnapshot().toastText, "REMOVED FROM FAVORITES");

    // 2. Play mode cycling
    tapPoint(nowPlayingController, QUICK_PLAY_MODE_X, QUICK_PLAY_MODE_Y);
    assert.deepEqual(callbackLogs.playModeChanges, ["shuffle"]);
    const shuffleToast = nowPlayingController.getRenderSnapshot().toastText;
    assert.ok(shuffleToast.startsWith("MODE // "));
    assert.ok(
      shuffleToast.toUpperCase().includes(NOW_PLAYING_PLAY_MODES[1].label.toUpperCase()),
    );

    // 3. Playlist picker creates default "DialOS Crate" when empty, adds track, and dedupes
    tapPoint(nowPlayingController, QUICK_PLAYLIST_X, QUICK_PLAYLIST_Y);
    const pickerOpenSnapshot = nowPlayingController.getRenderSnapshot();
    assert.equal(pickerOpenSnapshot.playlistPickerOpen, true);
    assert.equal(pickerOpenSnapshot.playlists.length, 1);
    assert.equal(pickerOpenSnapshot.playlists[0].name, "DialOS Crate");

    const firstPlaylistRow = computeNowPlayingQueueRowLayout(0, 0);
    tapPoint(nowPlayingController, firstPlaylistRow.centerX, firstPlaylistRow.centerY);

    const afterAddSnapshot = nowPlayingController.getRenderSnapshot();
    assert.equal(afterAddSnapshot.playlistPickerOpen, false);
    assert.equal(afterAddSnapshot.toastText.toUpperCase(), "ADDED TO DIALOS CRATE");
    assert.equal(callbackLogs.persistedPlaylists.length, 1);
    assert.deepEqual(callbackLogs.persistedPlaylists[0][0].trackIds, ["t1"]);

    openQuickActionsViaTap(nowPlayingController);
    tapPoint(nowPlayingController, QUICK_PLAYLIST_X, QUICK_PLAYLIST_Y);
    tapPoint(nowPlayingController, firstPlaylistRow.centerX, firstPlaylistRow.centerY);

    const afterDuplicateSnapshot = nowPlayingController.getRenderSnapshot();
    assert.equal(afterDuplicateSnapshot.playlistPickerOpen, false);
    assert.equal(afterDuplicateSnapshot.toastText.toUpperCase(), "IN DIALOS CRATE");
    assert.deepEqual(afterDuplicateSnapshot.playlists[0].trackIds, ["t1"]);

    // 4. Lyrics quick button opens lyrics and closes Quick Actions
    openQuickActionsViaTap(nowPlayingController);
    tapPoint(nowPlayingController, QUICK_LYRICS_X, QUICK_LYRICS_Y);
    const lyricsOpenedSnapshot = nowPlayingController.getRenderSnapshot();
    assert.equal(lyricsOpenedSnapshot.lyricsOpen, true);
    assert.equal(lyricsOpenedSnapshot.quickActionsOpen, false);

    nowPlayingController.stepBack();
    assert.equal(nowPlayingController.getRenderSnapshot().lyricsOpen, false);

    // 5. Info quick button opens details sheet with enriched metadata
    openQuickActionsViaTap(nowPlayingController);
    tapPoint(nowPlayingController, QUICK_DETAILS_X, QUICK_DETAILS_Y);
    const detailsSnapshot = nowPlayingController.getRenderSnapshot();
    assert.equal(detailsSnapshot.detailsSheetOpen, true);
    assert.equal(detailsSnapshot.trackDetails.format, "FLAC 24-BIT");
    assert.equal(detailsSnapshot.trackDetails.sampleRate, "96.0 KHZ");
    assert.equal(detailsSnapshot.trackDetails.bitrate, "2,822 KBPS");
    assert.equal(detailsSnapshot.trackDetails.fileSize, "42.3 MB");
    assert.equal(typeof detailsSnapshot.trackDetails.trackIndexLabel, "string");
    assert.ok(detailsSnapshot.trackDetails.trackIndexLabel.includes("1"));
    assert.ok(detailsSnapshot.trackDetails.trackIndexLabel.includes("2"));
  });

  test("swipe-up opens Context Queue Sheet at active queueIndex, selects rows, snaps drags, and closes on pull-down", () => {
    const { nowPlayingController, callbackLogs } = createNowPlayingTestHarness({
      sourceSpec: { trackId: "t3", queueSource: "all-songs" },
    });

    nowPlayingController.handlePointerDown(DISPLAY_CENTER_PX, QUEUE_PULL_START_Y);
    nowPlayingController.handlePointerMove(DISPLAY_CENTER_PX, QUEUE_PULL_END_Y);
    nowPlayingController.handlePointerUp(DISPLAY_CENTER_PX, QUEUE_PULL_END_Y);

    const openedQueueSnapshot = nowPlayingController.getRenderSnapshot();
    assert.equal(openedQueueSnapshot.queueSheetOpen, true);
    assert.equal(openedQueueSnapshot.queueScrollPosition, 2);

    // Drag up to scroll deeper, release, and settle to integer snap index
    nowPlayingController.handlePointerDown(DISPLAY_CENTER_PX, 210);
    nowPlayingController.handlePointerMove(DISPLAY_CENTER_PX, 145);
    nowPlayingController.handlePointerUp(DISPLAY_CENTER_PX, 145);
    const snappedQueueSnapshot = advanceSteps(
      nowPlayingController,
      STEP_DELTA_SECONDS,
      SETTLE_STEP_COUNT,
    );
    assert.ok(snappedQueueSnapshot.queueScrollPosition >= 2);
    assert.ok(
      Math.abs(
        snappedQueueSnapshot.queueScrollPosition -
          Math.round(snappedQueueSnapshot.queueScrollPosition),
      ) < FLOAT_TOLERANCE,
    );

    // Select row 1 from scroll 0
    nowPlayingController.stepBack();
    const zeroQueueHarness = createNowPlayingTestHarness({
      sourceSpec: { trackId: "t1", queueSource: "all-songs" },
    });
    zeroQueueHarness.nowPlayingController.handlePointerDown(DISPLAY_CENTER_PX, QUEUE_PULL_START_Y);
    zeroQueueHarness.nowPlayingController.handlePointerMove(DISPLAY_CENTER_PX, QUEUE_PULL_END_Y);
    zeroQueueHarness.nowPlayingController.handlePointerUp(DISPLAY_CENTER_PX, QUEUE_PULL_END_Y);
    assert.equal(zeroQueueHarness.nowPlayingController.getRenderSnapshot().queueScrollPosition, 0);

    // Pull down at scroll 0 (> 68px) closes Queue Sheet
    zeroQueueHarness.nowPlayingController.handlePointerDown(DISPLAY_CENTER_PX, QUEUE_CLOSE_START_Y);
    zeroQueueHarness.nowPlayingController.handlePointerMove(DISPLAY_CENTER_PX, QUEUE_CLOSE_END_Y);
    zeroQueueHarness.nowPlayingController.handlePointerUp(DISPLAY_CENTER_PX, QUEUE_CLOSE_END_Y);
    assert.equal(zeroQueueHarness.nowPlayingController.getRenderSnapshot().queueSheetOpen, false);

    // Re-open at scroll 0 and tap row 1
    zeroQueueHarness.nowPlayingController.handlePointerDown(DISPLAY_CENTER_PX, QUEUE_PULL_START_Y);
    zeroQueueHarness.nowPlayingController.handlePointerMove(DISPLAY_CENTER_PX, QUEUE_PULL_END_Y);
    zeroQueueHarness.nowPlayingController.handlePointerUp(DISPLAY_CENTER_PX, QUEUE_PULL_END_Y);

    const secondQueueRow = computeNowPlayingQueueRowLayout(1, 0);
    tapPoint(zeroQueueHarness.nowPlayingController, secondQueueRow.centerX, secondQueueRow.centerY);
    assert.deepEqual(zeroQueueHarness.callbackLogs.selectedQueueIndices, [1]);
    assert.equal(zeroQueueHarness.nowPlayingController.getRenderSnapshot().queueSheetOpen, false);
  });
});

describe("createNowPlayingController lyrics playback, back navigation, and negative guards", () => {
  test("animates lyric cue transitions over 0.44s and cycles lyrics mode cleanly", () => {
    const { audioEngine, nowPlayingController } = createNowPlayingTestHarness({
      sourceSpec: { trackId: "t1", queueSource: "all-songs" },
    });

    openQuickActionsViaTap(nowPlayingController);
    tapPoint(nowPlayingController, QUICK_LYRICS_X, QUICK_LYRICS_Y);
    assert.equal(nowPlayingController.getRenderSnapshot().lyricsOpen, true);
    assert.equal(nowPlayingController.getRenderSnapshot().lyricsMode, "auto");

    audioEngine.seekTo(5.5);
    nowPlayingController.syncAudioState(audioEngine.getState());
    nowPlayingController.step(STEP_DELTA_SECONDS);

    audioEngine.seekTo(13.0);
    nowPlayingController.syncAudioState(audioEngine.getState());
    nowPlayingController.step(0.11);

    const transitioningLyricsSnapshot = nowPlayingController.getRenderSnapshot();
    assert.equal(transitioningLyricsSnapshot.lyricsPreviousLineText, "First light across the vinyl");
    assert.ok(
      transitioningLyricsSnapshot.lyricsLineTransitionProgress > 0 &&
        transitioningLyricsSnapshot.lyricsLineTransitionProgress < 1,
    );

    nowPlayingController.step(NOW_PLAYING_CONSTANTS.LYRICS_TRANSITION_DURATION_SECONDS);
    assert.equal(nowPlayingController.getRenderSnapshot().lyricsLineTransitionProgress, 1);

    nowPlayingController.cycleLyricsMode();
    assert.equal(nowPlayingController.getRenderSnapshot().lyricsMode, "timed");
    nowPlayingController.cycleLyricsMode();
    assert.equal(nowPlayingController.getRenderSnapshot().lyricsMode, "untimed");
    nowPlayingController.cycleLyricsMode();
    assert.equal(nowPlayingController.getRenderSnapshot().lyricsMode, "none");
    nowPlayingController.cycleLyricsMode();
    assert.equal(nowPlayingController.getRenderSnapshot().lyricsMode, "auto");
  });

  test("stepBack and left-edge swipe close layers in strict priority order and invoke onExit(returnScreen)", () => {
    const libraryHarness = createNowPlayingTestHarness({ returnScreen: "library" });
    const controller = libraryHarness.nowPlayingController;

    // 1. playlistPickerOpen
    openQuickActionsViaTap(controller);
    tapPoint(controller, QUICK_PLAYLIST_X, QUICK_PLAYLIST_Y);
    assert.equal(controller.getRenderSnapshot().playlistPickerOpen, true);
    controller.stepBack();
    assert.equal(controller.getRenderSnapshot().playlistPickerOpen, false);
    assert.deepEqual(libraryHarness.callbackLogs.exitReturnScreens, []);

    // 2. detailsSheetOpen
    openQuickActionsViaTap(controller);
    tapPoint(controller, QUICK_DETAILS_X, QUICK_DETAILS_Y);
    assert.equal(controller.getRenderSnapshot().detailsSheetOpen, true);
    performLeftEdgeBackSwipe(controller);
    assert.equal(controller.getRenderSnapshot().detailsSheetOpen, false);
    assert.deepEqual(libraryHarness.callbackLogs.exitReturnScreens, []);

    // 3. queueSheetOpen
    controller.handlePointerDown(DISPLAY_CENTER_PX, QUEUE_PULL_START_Y);
    controller.handlePointerMove(DISPLAY_CENTER_PX, QUEUE_PULL_END_Y);
    controller.handlePointerUp(DISPLAY_CENTER_PX, QUEUE_PULL_END_Y);
    assert.equal(controller.getRenderSnapshot().queueSheetOpen, true);
    controller.stepBack();
    assert.equal(controller.getRenderSnapshot().queueSheetOpen, false);
    assert.deepEqual(libraryHarness.callbackLogs.exitReturnScreens, []);

    // 4. quickActionsOpen
    openQuickActionsViaTap(controller);
    assert.equal(controller.getRenderSnapshot().quickActionsOpen, true);
    controller.stepBack();
    assert.equal(controller.getRenderSnapshot().quickActionsOpen, false);
    assert.deepEqual(libraryHarness.callbackLogs.exitReturnScreens, []);

    // 5. lyricsOpen
    openQuickActionsViaTap(controller);
    tapPoint(controller, QUICK_LYRICS_X, QUICK_LYRICS_Y);
    assert.equal(controller.getRenderSnapshot().lyricsOpen, true);
    performLeftEdgeBackSwipe(controller);
    assert.equal(controller.getRenderSnapshot().lyricsOpen, false);
    assert.deepEqual(libraryHarness.callbackLogs.exitReturnScreens, []);

    // 6. Bare vinyl exits with returnScreen ("library" via stepBack and "home" via swipe)
    controller.stepBack();
    assert.deepEqual(libraryHarness.callbackLogs.exitReturnScreens, ["library"]);

    const homeHarness = createNowPlayingTestHarness({ returnScreen: "home" });
    performLeftEdgeBackSwipe(homeHarness.nowPlayingController);
    assert.deepEqual(homeHarness.callbackLogs.exitReturnScreens, ["home"]);
  });

  test("handles non-finite deltas, unmatched pointer events, partial back-swipes, and empty catalogs safely", () => {
    const { nowPlayingController, callbackLogs } = createNowPlayingTestHarness();

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
      nowPlayingController.step(invalidDelta);
      const snapshot = nowPlayingController.getRenderSnapshot();
      assert.ok(Number.isFinite(snapshot.openTransitionProgress));
      assert.ok(Number.isFinite(snapshot.skipTransitionProgress));
      assert.ok(Number.isFinite(snapshot.volumeLevel));
      assert.ok(Number.isFinite(snapshot.trackElapsedSeconds));
      assert.ok(Number.isFinite(snapshot.seekHoldProgress));
      assert.ok(Number.isFinite(snapshot.seekExpandProgress));
    }

    nowPlayingController.handlePointerMove(DISPLAY_CENTER_PX, DISPLAY_CENTER_PX);
    nowPlayingController.handlePointerUp(DISPLAY_CENTER_PX, DISPLAY_CENTER_PX);
    nowPlayingController.handlePointerDown(Number.NaN, DISPLAY_CENTER_PX);
    nowPlayingController.handleWheel(Number.NaN);
    nowPlayingController.handleWheel(0);

    // Partial left-edge back-swipe below 0.45 threshold stays on Now Playing and decays
    nowPlayingController.handlePointerDown(BACK_SWIPE_START_X, BACK_SWIPE_Y);
    nowPlayingController.handlePointerMove(BACK_SWIPE_PARTIAL_END_X, BACK_SWIPE_Y);
    nowPlayingController.handlePointerUp(BACK_SWIPE_PARTIAL_END_X, BACK_SWIPE_Y);
    assert.deepEqual(callbackLogs.exitReturnScreens, []);
    advanceSteps(nowPlayingController, STEP_DELTA_SECONDS, SETTLE_STEP_COUNT);
    assert.equal(nowPlayingController.getRenderSnapshot().backSwipeProgress, 0);

    // Empty catalog and null state inputs remain safely in idle mode
    const standaloneController = createNowPlayingController();
    standaloneController.hydrateCatalog(null);
    standaloneController.hydrateCatalog({});
    standaloneController.syncAudioState(null);
    standaloneController.enterFromLibrary({ returnScreen: "library", audioState: null });
    assert.equal(standaloneController.getRenderSnapshot().operatingMode, "idle");
  });
});
