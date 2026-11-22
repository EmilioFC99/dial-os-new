import { describe, test } from "node:test";
import assert from "node:assert/strict";

import { createAudioEngine } from "../core/audio_engine.js";

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
