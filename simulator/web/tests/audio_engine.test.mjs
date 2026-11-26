import { describe, test } from "node:test";
import assert from "node:assert/strict";

import {
  PREVIOUS_RESTART_THRESHOLD_SECONDS,
  REPEAT_MODES,
  buildShuffledTrackIds,
  createAudioEngine,
  resolveContextBadge,
  resolveSkipOutcome,
  trackMatchesGenreName,
} from "../core/audio_engine.js";

const MOCK_AUDIO_DURATION_SECONDS = 180;
const MOCK_AUDIO_READY_STATE = 4;
const ACTIVE_TRACK_ELAPSED_SECONDS = 48.5;
const PLAYLIST_TRACK_ELAPSED_SECONDS = 64;
const QUEUE_TRACK_ELAPSED_SECONDS = 54.25;
const HEADLESS_STEP_SECONDS = 37.5;

/**
 * Create an HTMLAudioElement-compatible stub tracking `src` reassignments and method calls.
 *
 * @returns {object} Stub audio element with call counters and `src` assignment tracking.
 */
function createMockAudioStub() {
  const listeners = {};
  let currentSourceUrl = "";

  const stubAudioElement = {
    preload: "",
    currentTime: 0,
    duration: MOCK_AUDIO_DURATION_SECONDS,
    paused: true,
    volume: 1,
    readyState: MOCK_AUDIO_READY_STATE,
    playCalls: 0,
    pauseCalls: 0,
    loadCalls: 0,
    sourceAssignmentCount: 0,
    assignedSources: [],
    listeners,
    get src() {
      return currentSourceUrl;
    },
    set src(nextSourceUrl) {
      currentSourceUrl = nextSourceUrl;
      stubAudioElement.sourceAssignmentCount += 1;
      stubAudioElement.assignedSources.push(nextSourceUrl);
    },
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
 * Build a multi-album and multi-playlist catalog fixture for playback continuity tests.
 *
 * @returns {object} Test catalog payload.
 */
function buildAudioContinuityCatalog() {
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
        tracks: [{ ...trackOne }, { ...trackTwo }],
      },
      {
        id: "a2",
        title: "Zenith Echo",
        artist: "Boreal Quartet",
        genre: "Jazz",
        year: 2023,
        tracks: [{ ...trackThree }],
      },
    ],
    allTracks: [trackOne, trackTwo, trackThree],
    playlists: [{ id: "p1", name: "Morning Mix", trackIds: ["t1", "t2", "t3"] }],
  };
}

describe("audio_engine.js playback continuity", () => {
  test("playTrackFromSource keeps elapsedSeconds, avoids src/load resets, preserves queue context, and resumes when paused for active track", () => {
    const stubAudio = createMockAudioStub();
    const audioEngine = createAudioEngine({
      createAudioElement: () => stubAudio,
    });
    const catalog = buildAudioContinuityCatalog();

    audioEngine.playTrackFromSource(
      { trackId: "t1", queueSource: "album", albumId: "a1" },
      catalog,
    );
    assert.equal(stubAudio.loadCalls, 1);
    assert.equal(stubAudio.sourceAssignmentCount, 1);
    assert.equal(stubAudio.playCalls, 1);

    audioEngine.seekTo(ACTIVE_TRACK_ELAPSED_SECONDS);
    stubAudio.currentTime = ACTIVE_TRACK_ELAPSED_SECONDS;

    // Re-invoking playTrackFromSource for the active playing track must not reload or reset position.
    audioEngine.playTrackFromSource(
      { trackId: "t1", queueSource: "album", albumId: "a1" },
      catalog,
    );
    const sameAlbumContinueState = audioEngine.getState();
    assert.equal(sameAlbumContinueState.activeTrack.id, "t1");
    assert.equal(sameAlbumContinueState.elapsedSeconds, ACTIVE_TRACK_ELAPSED_SECONDS);
    assert.equal(stubAudio.currentTime, ACTIVE_TRACK_ELAPSED_SECONDS);
    assert.equal(stubAudio.src, "/api/audio/t1");
    assert.equal(stubAudio.sourceAssignmentCount, 1);
    assert.equal(stubAudio.loadCalls, 1);
    assert.equal(sameAlbumContinueState.queueSource, "album");
    assert.equal(sameAlbumContinueState.queueAlbumId, "a1");
    assert.equal(sameAlbumContinueState.queueIndex, 0);
    assert.equal(sameAlbumContinueState.queueTracks.length, 2);

    // Switch to track t2 in a playlist queue, pause at 64s, and re-invoke with the active track ID.
    audioEngine.playTrackFromSource(
      { trackId: "t2", queueSource: "playlist", playlistId: "p1" },
      catalog,
    );
    audioEngine.seekTo(PLAYLIST_TRACK_ELAPSED_SECONDS);
    stubAudio.currentTime = PLAYLIST_TRACK_ELAPSED_SECONDS;
    const loadCallsBeforePause = stubAudio.loadCalls;
    const sourceAssignmentsBeforePause = stubAudio.sourceAssignmentCount;

    audioEngine.pause();
    const playCallsBeforeResume = stubAudio.playCalls;
    assert.equal(audioEngine.getState().playbackState, "paused");
    assert.equal(audioEngine.getState().isPlaying, false);
    assert.equal(audioEngine.getState().elapsedSeconds, PLAYLIST_TRACK_ELAPSED_SECONDS);

    audioEngine.playTrackFromSource(
      { trackId: "t2", queueSource: "playlist", playlistId: "p1" },
      catalog,
    );
    const resumedPlaylistState = audioEngine.getState();
    assert.equal(resumedPlaylistState.playbackState, "playing");
    assert.equal(resumedPlaylistState.isPlaying, true);
    assert.equal(stubAudio.paused, false);
    assert.equal(stubAudio.playCalls, playCallsBeforeResume + 1);
    assert.equal(resumedPlaylistState.elapsedSeconds, PLAYLIST_TRACK_ELAPSED_SECONDS);
    assert.equal(stubAudio.currentTime, PLAYLIST_TRACK_ELAPSED_SECONDS);
    assert.equal(stubAudio.loadCalls, loadCallsBeforePause);
    assert.equal(stubAudio.sourceAssignmentCount, sourceAssignmentsBeforePause);
    assert.equal(resumedPlaylistState.queueSource, "playlist");
    assert.equal(resumedPlaylistState.queuePlaylistId, "p1");
    assert.equal(resumedPlaylistState.queueIndex, 1);
    assert.deepEqual(
      resumedPlaylistState.queueTracks.map((trackEntry) => trackEntry.id),
      ["t1", "t2", "t3"],
    );

    // Re-invoking the same active track from another source (e.g. Albums crate) preserves elapsed time and avoids reload.
    audioEngine.playTrackFromSource(
      { trackId: "t2", queueSource: "album", albumId: "a1" },
      catalog,
    );
    const crossSourceContinueState = audioEngine.getState();
    assert.equal(crossSourceContinueState.activeTrack.id, "t2");
    assert.equal(crossSourceContinueState.elapsedSeconds, PLAYLIST_TRACK_ELAPSED_SECONDS);
    assert.equal(stubAudio.currentTime, PLAYLIST_TRACK_ELAPSED_SECONDS);
    assert.equal(stubAudio.loadCalls, loadCallsBeforePause);
    assert.equal(stubAudio.sourceAssignmentCount, sourceAssignmentsBeforePause);

    // Selecting a different track ID still resets elapsedSeconds and reloads the new source.
    audioEngine.playTrackFromSource(
      { trackId: "t3", queueSource: "album", albumId: "a2" },
      catalog,
    );
    const switchedTrackState = audioEngine.getState();
    assert.equal(switchedTrackState.activeTrack.id, "t3");
    assert.equal(switchedTrackState.elapsedSeconds, 0);
    assert.equal(stubAudio.src, "/api/audio/t3");
    assert.equal(stubAudio.loadCalls, loadCallsBeforePause + 1);

    // Headless engine preserves elapsedSeconds and resumes when paused for active track ID.
    const headlessEngine = createAudioEngine();
    headlessEngine.playTrackFromSource(
      { trackId: "t1", queueSource: "album", albumId: "a1" },
      catalog,
    );
    headlessEngine.step(HEADLESS_STEP_SECONDS);
    headlessEngine.pause();
    assert.equal(headlessEngine.getState().elapsedSeconds, HEADLESS_STEP_SECONDS);

    headlessEngine.playTrackFromSource(
      { trackId: "t1", queueSource: "album", albumId: "a1" },
      catalog,
    );
    assert.equal(headlessEngine.getState().isPlaying, true);
    assert.equal(headlessEngine.getState().playbackState, "playing");
    assert.equal(headlessEngine.getState().elapsedSeconds, HEADLESS_STEP_SECONDS);
  });

  test("selectQueueIndex keeps elapsedSeconds untouched for currentIndex and restarts when selecting a different index", () => {
    const stubAudio = createMockAudioStub();
    const audioEngine = createAudioEngine({
      createAudioElement: () => stubAudio,
    });
    const catalog = buildAudioContinuityCatalog();

    audioEngine.playTrackFromSource(
      { trackId: "t1", queueSource: "all-songs" },
      catalog,
    );
    audioEngine.seekTo(QUEUE_TRACK_ELAPSED_SECONDS);
    stubAudio.currentTime = QUEUE_TRACK_ELAPSED_SECONDS;

    const loadCallsBeforeSameSelect = stubAudio.loadCalls;
    const sourceAssignmentsBeforeSameSelect = stubAudio.sourceAssignmentCount;

    // Tapping the currently playing track index inside Context Queue must not restart at 0:00.
    audioEngine.selectQueueIndex(0);
    const sameIndexPlayingState = audioEngine.getState();
    assert.equal(sameIndexPlayingState.queueIndex, 0);
    assert.equal(sameIndexPlayingState.activeTrack.id, "t1");
    assert.equal(sameIndexPlayingState.elapsedSeconds, QUEUE_TRACK_ELAPSED_SECONDS);
    assert.equal(stubAudio.currentTime, QUEUE_TRACK_ELAPSED_SECONDS);
    assert.equal(stubAudio.loadCalls, loadCallsBeforeSameSelect);
    assert.equal(stubAudio.sourceAssignmentCount, sourceAssignmentsBeforeSameSelect);
    assert.equal(sameIndexPlayingState.isPlaying, true);

    // If paused on the current index, selectQueueIndex(currentIndex) resumes without resetting to 0:00.
    audioEngine.pause();
    const playCallsBeforeQueueResume = stubAudio.playCalls;
    audioEngine.selectQueueIndex(0);
    const sameIndexResumedState = audioEngine.getState();
    assert.equal(sameIndexResumedState.isPlaying, true);
    assert.equal(sameIndexResumedState.playbackState, "playing");
    assert.equal(stubAudio.playCalls, playCallsBeforeQueueResume + 1);
    assert.equal(sameIndexResumedState.elapsedSeconds, QUEUE_TRACK_ELAPSED_SECONDS);
    assert.equal(stubAudio.currentTime, QUEUE_TRACK_ELAPSED_SECONDS);
    assert.equal(stubAudio.loadCalls, loadCallsBeforeSameSelect);
    assert.equal(stubAudio.sourceAssignmentCount, sourceAssignmentsBeforeSameSelect);

    // Selecting a different queue index still jumps and resets elapsedSeconds to 0:00.
    audioEngine.selectQueueIndex(2);
    const jumpedTrackState = audioEngine.getState();
    assert.equal(jumpedTrackState.queueIndex, 2);
    assert.equal(jumpedTrackState.activeTrack.id, "t3");
    assert.equal(jumpedTrackState.elapsedSeconds, 0);
    assert.equal(stubAudio.currentTime, 0);
    assert.equal(stubAudio.src, "/api/audio/t3");
    assert.equal(stubAudio.loadCalls, loadCallsBeforeSameSelect + 1);

    // Headless queue selection behaves identically for same vs different indices.
    const headlessEngine = createAudioEngine();
    headlessEngine.playTrackFromSource(
      { trackId: "t1", queueSource: "all-songs" },
      catalog,
    );
    headlessEngine.step(HEADLESS_STEP_SECONDS);
    headlessEngine.selectQueueIndex(0);
    assert.equal(headlessEngine.getState().elapsedSeconds, HEADLESS_STEP_SECONDS);
    assert.equal(headlessEngine.getState().activeTrack.id, "t1");

    headlessEngine.selectQueueIndex(1);
    assert.equal(headlessEngine.getState().elapsedSeconds, 0);
    assert.equal(headlessEngine.getState().activeTrack.id, "t2");
  });
});

/**
 * Build a deterministic multi-context catalog with mixed per-track genres for context tests.
 *
 * @returns {object} Test catalog fixture.
 */
function buildContextIsolationCatalog() {
  const trackElectronicOne = {
    id: "g-e1",
    title: "01 Pulse Grid",
    artist: "Signal Unit",
    albumId: "alb-mixed",
    albumTitle: "Split Spectrum",
    genre: "Electronic",
    genres: ["Electronic", "House"],
    duration: "03:00",
    durationSeconds: 180,
    isFavorite: true,
    trackNumber: "01",
    audioUrl: "/api/audio/g-e1",
  };
  const trackJazz = {
    id: "g-j1",
    title: "02 Velvet Brass",
    artist: "Signal Unit",
    albumId: "alb-mixed",
    albumTitle: "Split Spectrum",
    genre: "Jazz",
    genres: ["Jazz"],
    duration: "03:10",
    durationSeconds: 190,
    isFavorite: true,
    trackNumber: "02",
    audioUrl: "/api/audio/g-j1",
  };
  const trackElectronicTwo = {
    id: "g-e2",
    title: "03 Neon Sub",
    artist: "Signal Unit",
    albumId: "alb-mixed",
    albumTitle: "Split Spectrum",
    genre: "Electronic",
    genres: ["Electronic"],
    duration: "03:20",
    durationSeconds: 200,
    isFavorite: true,
    trackNumber: "03",
    audioUrl: "/api/audio/g-e2",
  };
  const trackHouseOnly = {
    id: "g-h1",
    title: "01 Midnight Floor",
    artist: "Club Theory",
    albumId: "alb-house",
    albumTitle: "Late Room",
    genre: "House",
    genres: ["House", "Electronic"],
    duration: "03:30",
    durationSeconds: 210,
    isFavorite: false,
    trackNumber: "01",
    audioUrl: "/api/audio/g-h1",
  };

  return {
    albums: [
      {
        id: "alb-mixed",
        title: "Split Spectrum",
        artist: "Signal Unit",
        genre: "Electronic",
        year: 2025,
        tracks: [trackElectronicOne, trackJazz, trackElectronicTwo],
      },
      {
        id: "alb-house",
        title: "Late Room",
        artist: "Club Theory",
        genre: "House",
        year: 2025,
        tracks: [trackHouseOnly],
      },
    ],
    allTracks: [trackElectronicOne, trackJazz, trackElectronicTwo, trackHouseOnly],
    genres: [
      { id: "genre-electronic", name: "Electronic", albumIds: ["alb-mixed", "alb-house"] },
      { id: "genre-house", name: "House", albumIds: ["alb-mixed", "alb-house"] },
      { id: "genre-jazz", name: "Jazz", albumIds: ["alb-mixed"] },
    ],
    playlists: [
      { id: "pl-custom", name: "Late Drive", trackIds: ["g-h1", "g-e1", "g-j1", "missing-id"] },
    ],
  };
}

describe("audio_engine.js context-scoped Shuffle & Repeat matrix and live mutations", () => {
  test("covers all 6 shuffle x repeat combinations across auto-end and manual skip", () => {
    const catalog = buildContextIsolationCatalog();
    const expectedModes = ["off", "all", "one"];
    assert.deepEqual(REPEAT_MODES, expectedModes);

    // Deterministic seeded PRNG (LCG)
    function createSeededRandom(initialSeed = 13) {
      let currentSeed = initialSeed;
      return () => {
        currentSeed = (currentSeed * 1103515245 + 12345) & 0x7fffffff;
        return currentSeed / 0x80000000;
      };
    }

    // Check every combination in album context (3 tracks: g-e1, g-j1, g-e2)
    for (const shuffleEnabled of [false, true]) {
      for (const repeatMode of REPEAT_MODES) {
        const stubAudio = createMockAudioStub();
        const audioEngine = createAudioEngine({
          createAudioElement: () => stubAudio,
          random: createSeededRandom(42),
        });
        audioEngine.setShuffleEnabled(shuffleEnabled);
        audioEngine.setRepeatMode(repeatMode);
        audioEngine.playTrackFromSource(
          { trackId: "g-e1", queueSource: "album", albumId: "alb-mixed" },
          catalog,
        );

        const initialDeck = audioEngine.getState().queueTracks.map((trackEntry) => trackEntry.id);
        assert.equal(initialDeck[0], "g-e1");
        assert.equal(initialDeck.length, 3);
        if (!shuffleEnabled) {
          assert.deepEqual(initialDeck, ["g-e1", "g-j1", "g-e2"]);
        }

        // Track 0 at <= 3s manual Prev: bounces at START OF ALBUM unless Repeat All (wraps to end)
        audioEngine.seekTo(PREVIOUS_RESTART_THRESHOLD_SECONDS);
        const startPrevOutcome = audioEngine.skipTrack(-1);
        if (repeatMode === "all") {
          assert.equal(startPrevOutcome.action, "jump");
          assert.equal(audioEngine.getState().activeTrack.id, initialDeck[2]);
          audioEngine.selectQueueIndex(0);
        } else {
          assert.equal(startPrevOutcome.action, "boundary-bounce");
          assert.equal(startPrevOutcome.boundary, "start");
          assert.equal(audioEngine.getState().activeTrack.id, initialDeck[0]);
        }

        // Track 0 at > 3s manual Prev restarts current track at 0:00
        audioEngine.seekTo(PREVIOUS_RESTART_THRESHOLD_SECONDS + 1);
        const restartOutcome = audioEngine.skipTrack(-1);
        assert.equal(restartOutcome.action, "restart-current");
        assert.equal(audioEngine.getState().elapsedSeconds, 0);
        assert.equal(audioEngine.getState().activeTrack.id, initialDeck[0]);

        // Manual Next walks deck forward and Keep Repeat Mode intact (even repeat-one)
        const nextOneOutcome = audioEngine.skipTrack(1);
        assert.equal(nextOneOutcome.action, "jump");
        assert.equal(audioEngine.getState().activeTrack.id, initialDeck[1]);
        assert.equal(audioEngine.getState().repeatMode, repeatMode);

        // Manual Prev at <= 3s reverses along exact deck (even under shuffle)
        audioEngine.seekTo(1);
        const backOutcome = audioEngine.skipTrack(-1);
        assert.equal(backOutcome.action, "jump");
        assert.equal(audioEngine.getState().activeTrack.id, initialDeck[0]);

        // Jump to last slot
        audioEngine.selectQueueIndex(2);
        assert.equal(audioEngine.getState().activeTrack.id, initialDeck[2]);

        // Manual skip behavior on last track
        const manualEndOutcome = audioEngine.skipTrack(1);
        if (repeatMode === "all") {
          if (shuffleEnabled) {
            // Reshuffled deck avoids repeating initialDeck[2] at slot 0
            assert.equal(manualEndOutcome.action, "reshuffle-wrap");
            assert.notEqual(audioEngine.getState().activeTrack.id, initialDeck[2]);
          } else {
            assert.equal(manualEndOutcome.action, "jump");
            assert.equal(audioEngine.getState().activeTrack.id, initialDeck[0]);
          }
        } else {
          // Both repeat-off and repeat-one bounce at boundary on manual Next
          assert.equal(manualEndOutcome.action, "boundary-bounce");
          assert.equal(manualEndOutcome.boundary, "end");
          assert.equal(audioEngine.getState().activeTrack.id, initialDeck[2]);
        }

        // Auto-advance behavior on last slot via DOM ended event
        audioEngine.selectQueueIndex(2);
        const trackBeforeEnd = audioEngine.getState().activeTrack.id;
        audioEngine.seekTo(150);
        stubAudio.emit("ended");
        const afterAutoEnd = audioEngine.getState();

        if (repeatMode === "one") {
          assert.equal(afterAutoEnd.activeTrack.id, trackBeforeEnd);
          assert.equal(afterAutoEnd.elapsedSeconds, 0);
          assert.equal(afterAutoEnd.isPlaying, true);
        } else if (repeatMode === "off") {
          assert.equal(afterAutoEnd.isPlaying, false);
          assert.equal(afterAutoEnd.playbackState, "paused");
          assert.equal(afterAutoEnd.queueIndex, 0);
          assert.equal(afterAutoEnd.elapsedSeconds, 0);
          assert.equal(afterAutoEnd.lastEvent.type, "context-ended");
        } else if (repeatMode === "all") {
          assert.equal(afterAutoEnd.isPlaying, true);
          assert.equal(afterAutoEnd.elapsedSeconds, 0);
          if (shuffleEnabled) {
            assert.notEqual(afterAutoEnd.activeTrack.id, trackBeforeEnd);
          } else {
            assert.equal(afterAutoEnd.queueIndex, 0);
            assert.equal(afterAutoEnd.activeTrack.id, initialDeck[0]);
          }
        }
      }
    }
  });

  test("toggling Shuffle OFF restores natural context order anchored on active track and handles single-track contexts", () => {
    const catalog = buildContextIsolationCatalog();
    const audioEngine = createAudioEngine({ random: () => 0.15 });
    audioEngine.playTrackFromSource({ trackId: "g-e1", queueSource: "all-songs" }, catalog);

    audioEngine.setShuffleEnabled(true);
    const shuffledDeck = audioEngine.getState().queueTracks.map((trackEntry) => trackEntry.id);
    assert.equal(shuffledDeck[0], "g-e1");

    // Advance to whatever track landed at slot 2 in shuffled deck
    audioEngine.selectQueueIndex(2);
    const midTrackId = audioEngine.getState().activeTrack.id;

    // Turning Shuffle OFF restores full natural all-songs order with queueIndex anchored on midTrackId
    audioEngine.setShuffleEnabled(false);
    const restoredState = audioEngine.getState();
    assert.deepEqual(
      restoredState.queueTracks.map((trackEntry) => trackEntry.id),
      ["g-e1", "g-j1", "g-e2", "g-h1"],
    );
    assert.equal(restoredState.activeTrack.id, midTrackId);
    assert.equal(restoredState.queueTracks[restoredState.queueIndex].id, midTrackId);

    // buildShuffledTrackIds avoids finished track first when count > 1
    const reshuffledIds = buildShuffledTrackIds(["a", "b", "c"], null, "a", () => 0.0);
    assert.notEqual(reshuffledIds[0], "a");
    assert.equal(new Set(reshuffledIds).size, 3);

    // Single-track context under all modes never crashes or duplicates
    for (const shuffleEnabled of [false, true]) {
      for (const repeatMode of REPEAT_MODES) {
        const singleEngine = createAudioEngine();
        singleEngine.setShuffleEnabled(shuffleEnabled);
        singleEngine.setRepeatMode(repeatMode);
        singleEngine.playTrackFromSource(
          { trackId: "g-h1", queueSource: "album", albumId: "alb-house" },
          catalog,
        );
        assert.equal(
          singleEngine.skipTrack(1).action,
          repeatMode === "all" ? "restart-current" : "boundary-bounce",
        );
        singleEngine.seekTo(1);
        assert.equal(
          singleEngine.skipTrack(-1).action,
          repeatMode === "all" ? "restart-current" : "boundary-bounce",
        );
        singleEngine.step(250);
        const endState = singleEngine.getState();
        assert.equal(endState.activeTrack.id, "g-h1");
        assert.equal(endState.isPlaying, repeatMode !== "off");
      }
    }
  });

  test("isolates genre, playlist, favorites, album, and all-songs contexts without ever crossing", () => {
    const catalog = buildContextIsolationCatalog();
    const audioEngine = createAudioEngine();

    // Mixed-genre album: selecting genre "House" includes tracks with "House" in genres across albums, excludes "Jazz"
    assert.equal(trackMatchesGenreName(catalog.allTracks[0], "house"), true);
    assert.equal(trackMatchesGenreName(catalog.allTracks[1], "house"), false);
    audioEngine.playTrackFromSource(
      { trackId: "g-e1", queueSource: "genre", genreTitle: "House" },
      catalog,
    );
    assert.deepEqual(
      audioEngine.getState().queueTracks.map((trackEntry) => trackEntry.id),
      ["g-h1", "g-e1"],
    );
    assert.equal(resolveContextBadge(audioEngine.getState().queueSource), "GENRE");

    // Playlist preserves exact trackIds order and skips stale/missing track ids
    audioEngine.playTrackFromSource(
      { trackId: "g-e1", queueSource: "playlist", playlistId: "pl-custom" },
      catalog,
    );
    assert.deepEqual(
      audioEngine.getState().queueTracks.map((trackEntry) => trackEntry.id),
      ["g-h1", "g-e1", "g-j1"],
    );
    assert.equal(audioEngine.getState().queueIndex, 1);

    // Shuffle override behavior on Playlist PLAY vs SHUFFLE MIX vs regular launch
    audioEngine.setShuffleEnabled(true);
    audioEngine.playTrackFromSource(
      { trackId: "g-h1", queueSource: "playlist", playlistId: "pl-custom", shuffleOverride: false },
      catalog,
    );
    assert.equal(audioEngine.getState().shuffleEnabled, false);
    assert.deepEqual(
      audioEngine.getState().queueTracks.map((trackEntry) => trackEntry.id),
      ["g-h1", "g-e1", "g-j1"],
    );

    audioEngine.playTrackFromSource(
      { trackId: "g-e1", queueSource: "playlist", playlistId: "pl-custom", shuffleOverride: true },
      catalog,
    );
    assert.equal(audioEngine.getState().shuffleEnabled, true);
    assert.equal(audioEngine.getState().activeTrack.id, "g-e1");

    // Regular library launch preserves active Shuffle and Repeat state
    audioEngine.setRepeatMode("all");
    audioEngine.playTrackFromSource(
      { trackId: "g-e1", queueSource: "favorites" },
      catalog,
    );
    assert.equal(audioEngine.getState().shuffleEnabled, true);
    assert.equal(audioEngine.getState().repeatMode, "all");
  });

  test("handles live Favorites and Playlist mutations safely including ephemeral finish and empty context transition to idle", () => {
    const catalog = buildContextIsolationCatalog();
    const audioEngine = createAudioEngine();

    // Start playing g-j1 from Favorites: [g-e1, g-j1, g-e2]
    audioEngine.playTrackFromSource({ trackId: "g-j1", queueSource: "favorites" }, catalog);
    assert.deepEqual(
      audioEngine.getState().queueTracks.map((trackEntry) => trackEntry.id),
      ["g-e1", "g-j1", "g-e2"],
    );

    // Live add non-playing g-h1 to favorites -> inserted in alphabetical title order while playing g-j1
    catalog.allTracks[3].isFavorite = true;
    audioEngine.updateTrackFavorite("g-h1", true, catalog);
    assert.deepEqual(
      audioEngine.getState().queueTracks.map((trackEntry) => trackEntry.id),
      ["g-h1", "g-e1", "g-j1", "g-e2"],
    );
    assert.equal(audioEngine.getState().activeTrack.id, "g-j1");

    // Un-favorite CURRENT track g-j1: track keeps playing as ephemeral without jumping
    catalog.allTracks[1].isFavorite = false;
    audioEngine.updateTrackFavorite("g-j1", false, catalog);
    const ephemeralState = audioEngine.getState();
    assert.equal(ephemeralState.activeTrack.id, "g-j1");
    assert.equal(ephemeralState.ephemeralCurrentTrackId, "g-j1");
    assert.equal(ephemeralState.baseTrackCount, 3);

    // When g-j1 ends, it drops cleanly and advances straight to g-e2 (does NOT skip g-e2!)
    audioEngine.step(200);
    const droppedState = audioEngine.getState();
    assert.equal(droppedState.activeTrack.id, "g-e2");
    assert.equal(droppedState.ephemeralCurrentTrackId, null);
    assert.deepEqual(
      droppedState.queueTracks.map((trackEntry) => trackEntry.id),
      ["g-h1", "g-e1", "g-e2"],
    );

    // Un-favorite remaining other tracks then finish last favorite g-e2 -> idle cleanly
    catalog.allTracks[0].isFavorite = false;
    audioEngine.updateTrackFavorite("g-e1", false, catalog);
    catalog.allTracks[3].isFavorite = false;
    audioEngine.updateTrackFavorite("g-h1", false, catalog);
    catalog.allTracks[2].isFavorite = false;
    audioEngine.updateTrackFavorite("g-e2", false, catalog);
    assert.equal(audioEngine.getState().baseTrackCount, 0);
    assert.equal(audioEngine.getState().activeTrack.id, "g-e2");

    audioEngine.step(220);
    const idleAfterLastFav = audioEngine.getState();
    assert.equal(idleAfterLastFav.playbackState, "idle");
    assert.equal(idleAfterLastFav.activeTrack, null);
    assert.equal(idleAfterLastFav.lastEvent.type, "idle");

    // Deleting active playlist transitions immediately to idle cleanly
    audioEngine.playTrackFromSource(
      { trackId: "g-e1", queueSource: "playlist", playlistId: "pl-custom" },
      catalog,
    );
    assert.equal(audioEngine.getState().playbackState, "playing");
    audioEngine.syncPlaylists([], catalog);
    const idleAfterDelete = audioEngine.getState();
    assert.equal(idleAfterDelete.playbackState, "idle");
    assert.equal(idleAfterDelete.activeTrack, null);
    assert.equal(idleAfterDelete.lastEvent.type, "idle");

    // Guard resolveSkipOutcome against null snapshot or invalid direction
    assert.equal(resolveSkipOutcome(null, 1).action, "idle");
  });

  test("resolves artist context A-Z across artistAlbumIds, isolates skips/repeats, switches album<->artist without resetting elapsedSeconds, and handles 1-album/empty fallbacks", () => {
    const artistCatalog = buildContextIsolationCatalog();
    // Add a second album for "Signal Unit" with a featured-artist track to verify albumIds-based collection + A-Z sort
    artistCatalog.albums.push({
      id: "alb-signal-2",
      title: "Second Signal",
      artist: "Signal Unit",
      genre: "Electronic",
      tracks: [
        {
          id: "g-s2",
          title: "Alpha Signal",
          artist: "Signal Unit feat. Guest",
          albumId: "alb-signal-2",
          albumTitle: "Second Signal",
          genre: "Electronic",
          genres: ["Electronic"],
          durationSeconds: 195,
          audioUrl: "/api/audio/g-s2",
        },
      ],
    });
    artistCatalog.allTracks.push({
      id: "g-s2",
      title: "Alpha Signal",
      artist: "Signal Unit feat. Guest",
      albumId: "alb-signal-2",
      albumTitle: "Second Signal",
      genre: "Electronic",
      genres: ["Electronic"],
      durationSeconds: 195,
      audioUrl: "/api/audio/g-s2",
    });

    assert.equal(resolveContextBadge("artist"), "ARTIST");

    const audioEngine = createAudioEngine({ random: () => 0.25 });
    audioEngine.playTrackFromSource(
      {
        trackId: "g-e1",
        queueSource: "artist",
        artistName: "Signal Unit",
        artistAlbumIds: ["alb-mixed", "alb-signal-2"],
      },
      artistCatalog,
    );

    const artistState = audioEngine.getState();
    assert.equal(artistState.queueSource, "artist");
    assert.equal(artistState.queueArtistName, "Signal Unit");
    assert.deepEqual(artistState.queueArtistAlbumIds, ["alb-mixed", "alb-signal-2"]);
    // A-Z titles: "01 Pulse Grid" (g-e1), "02 Velvet Brass" (g-j1), "03 Drift Echo" (g-e2), "Alpha Signal" (g-s2)
    assert.deepEqual(
      artistState.queueTracks.map((trackEntry) => trackEntry.id),
      ["g-e1", "g-j1", "g-e2", "g-s2"],
    );
    assert.equal(artistState.queueIndex, 0);

    // Boundary bounce at start emits START OF ARTIST
    const startBounce = audioEngine.skipTrack(-1);
    assert.equal(startBounce.action, "boundary-bounce");
    assert.equal(audioEngine.getState().lastEvent.message, "START OF ARTIST");

    // Advance to end and verify END OF ARTIST boundary bounce without crossing into alb-house (g-h1)
    audioEngine.skipTrack(1);
    audioEngine.skipTrack(1);
    audioEngine.skipTrack(1);
    assert.equal(audioEngine.getState().activeTrack.id, "g-s2");
    const endBounce = audioEngine.skipTrack(1);
    assert.equal(endBounce.action, "boundary-bounce");
    assert.equal(audioEngine.getState().lastEvent.message, "END OF ARTIST");

    // Repeat All wraps strictly within artist tracks
    audioEngine.setRepeatMode("all");
    audioEngine.skipTrack(1);
    assert.equal(audioEngine.getState().activeTrack.id, "g-e1");

    // Switching currently playing track between artist <-> album preserves elapsedSeconds
    audioEngine.step(12);
    assert.equal(audioEngine.getState().elapsedSeconds, 12);
    audioEngine.playTrackFromSource(
      { trackId: "g-e1", queueSource: "album", albumId: "alb-mixed" },
      artistCatalog,
    );
    const switchedToAlbum = audioEngine.getState();
    assert.equal(switchedToAlbum.queueSource, "album");
    assert.equal(switchedToAlbum.elapsedSeconds, 12);
    assert.deepEqual(
      switchedToAlbum.queueTracks.map((trackEntry) => trackEntry.id),
      ["g-e1", "g-j1", "g-e2"],
    );

    audioEngine.playTrackFromSource(
      {
        trackId: "g-e1",
        queueSource: "artist",
        artistName: "Signal Unit",
        artistAlbumIds: ["alb-mixed", "alb-signal-2"],
      },
      artistCatalog,
    );
    const switchedBackToArtist = audioEngine.getState();
    assert.equal(switchedBackToArtist.queueSource, "artist");
    assert.equal(switchedBackToArtist.elapsedSeconds, 12);
    assert.equal(switchedBackToArtist.queueTracks.length, 4);

    // Single-album artist produces artist context with that album's tracks sorted A-Z
    audioEngine.playTrackFromSource(
      {
        trackId: "g-h1",
        queueSource: "artist",
        artistName: "Club Theory",
        artistAlbumIds: ["alb-house"],
      },
      artistCatalog,
    );
    assert.equal(audioEngine.getState().queueSource, "artist");
    assert.deepEqual(
      audioEngine.getState().queueTracks.map((trackEntry) => trackEntry.id),
      ["g-h1"],
    );

    // Unknown/empty artistAlbumIds falls back cleanly to [targetTrack]
    audioEngine.playTrackFromSource(
      {
        trackId: "g-h1",
        queueSource: "artist",
        artistName: "Club Theory",
        artistAlbumIds: [],
      },
      artistCatalog,
    );
    assert.deepEqual(
      audioEngine.getState().queueTracks.map((trackEntry) => trackEntry.id),
      ["g-h1"],
    );
  });
});

describe("audio_engine.js error recovery", () => {
  test("recovers from MEDIA_ERR_SRC_NOT_SUPPORTED / NETWORK_NO_SOURCE on togglePlayPause and preserves resume offset", async () => {
    const catalog = buildAudioContinuityCatalog();
    const listeners = {};
    let currentSourceUrl = "";
    let loadCalls = 0;
    let shouldFailNextPlay = true;

    const stubAudio = {
      preload: "",
      currentTime: 0,
      duration: MOCK_AUDIO_DURATION_SECONDS,
      paused: true,
      volume: 1,
      readyState: MOCK_AUDIO_READY_STATE,
      networkState: 1,
      error: null,
      get src() {
        return currentSourceUrl;
      },
      set src(nextSourceUrl) {
        currentSourceUrl = nextSourceUrl;
      },
      addEventListener(eventName, handler) {
        if (!Array.isArray(listeners[eventName])) {
          listeners[eventName] = [];
        }
        listeners[eventName].push(handler);
      },
      load() {
        loadCalls += 1;
        if (!shouldFailNextPlay) {
          stubAudio.error = null;
          stubAudio.networkState = 1;
        }
      },
      play() {
        if (shouldFailNextPlay) {
          stubAudio.paused = true;
          stubAudio.error = { message: "MEDIA_ERR_SRC_NOT_SUPPORTED" };
          stubAudio.networkState = 3;
          for (const handler of listeners.error || []) {
            handler({ type: "error" });
          }
          const notSupportedError = new Error("The element has no supported sources.");
          notSupportedError.name = "NotSupportedError";
          return Promise.reject(notSupportedError);
        }
        stubAudio.paused = false;
        return Promise.resolve();
      },
      pause() {
        stubAudio.paused = true;
      },
      emit(eventName) {
        for (const handler of listeners[eventName] || []) {
          handler({ type: eventName });
        }
      },
    };

    const audioEngine = createAudioEngine({ createAudioElement: () => stubAudio });
    audioEngine.playTrackFromSource({ trackId: "t1", queueSource: "album", albumId: "a1" }, catalog);
    await Promise.resolve();

    assert.equal(audioEngine.getState().isPlaying, false);
    assert.equal(audioEngine.getState().playbackState, "paused");

    // Simulate server recovery on retry via togglePlayPause (double-tap)
    shouldFailNextPlay = false;
    const loadsBeforeRetry = loadCalls;
    audioEngine.togglePlayPause();
    await Promise.resolve();

    assert.equal(loadCalls, loadsBeforeRetry + 1);
    assert.equal(stubAudio.src, "/api/audio/t1");
    assert.equal(audioEngine.getState().isPlaying, true);
    assert.equal(audioEngine.getState().playbackState, "playing");

    // Also recover when play() rejects with NotSupportedError without an error event, preserving elapsedSeconds
    audioEngine.seekTo(24);
    shouldFailNextPlay = true;
    stubAudio.play = () => {
      if (shouldFailNextPlay) {
        stubAudio.paused = true;
        const notSupportedError = new Error("No supported source");
        notSupportedError.name = "NotSupportedError";
        return Promise.reject(notSupportedError);
      }
      stubAudio.paused = false;
      return Promise.resolve();
    };
    audioEngine.pause();
    audioEngine.togglePlayPause();
    await Promise.resolve();
    assert.equal(audioEngine.getState().isPlaying, false);

    shouldFailNextPlay = false;
    const loadsBeforeSecondRetry = loadCalls;
    audioEngine.togglePlayPause();
    await Promise.resolve();
    assert.equal(loadCalls, loadsBeforeSecondRetry + 1);
    assert.equal(audioEngine.getState().isPlaying, true);
    stubAudio.emit("loadedmetadata");
    assert.equal(stubAudio.currentTime, 24);

    // Empty audioUrl fallback does not throw or crash
    const emptyUrlCatalog = {
      albums: [{ id: "a-empty", title: "Empty", tracks: [{ id: "t-empty", title: "No Url", audioUrl: "" }] }],
      allTracks: [{ id: "t-empty", title: "No Url", albumId: "a-empty", audioUrl: "" }],
    };
    audioEngine.playTrackFromSource({ trackId: "t-empty", queueSource: "album", albumId: "a-empty" }, emptyUrlCatalog);
    audioEngine.pause();
    audioEngine.togglePlayPause();
    assert.equal(audioEngine.getState().activeTrack.id, "t-empty");
  });
});
