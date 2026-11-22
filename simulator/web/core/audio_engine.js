/**
 * Headless-safe HTMLAudioElement playback engine for the DialOS simulator.
 * Manages catalog queue resolution, shuffle preservation, transport controls,
 * and deterministic elapsed-time stepping in browser and Node environments.
 */

import { clamp } from "./geometry.js";

const DEFAULT_TRACK_DURATION_SECONDS = 180;
const DEFAULT_VOLUME_LEVEL = 0.62;

export const PLAY_MODES = Object.freeze(["sequential", "shuffle", "repeat-all", "repeat-one"]);

/**
 * Compare two track labels ascending using locale base sensitivity and numeric collation.
 *
 * @param {object} firstTrack First track entry.
 * @param {object} secondTrack Second track entry.
 * @returns {number} Comparison sign (-1, 0, or 1).
 */
function compareTrackTitles(firstTrack, secondTrack) {
  return String(firstTrack?.title ?? "").localeCompare(
    String(secondTrack?.title ?? ""),
    undefined,
    { sensitivity: "base", numeric: true },
  );
}

/**
 * Return a Fisher-Yates shuffled copy of `entries` without mutating the input array.
 *
 * @param {Array<object>} entries Track entries to shuffle.
 * @returns {Array<object>} Shuffled copy.
 */
function shuffleTracks(entries) {
  const shuffled = entries.slice();
  for (let currentIndex = shuffled.length - 1; currentIndex > 0; currentIndex -= 1) {
    const swapIndex = Math.floor(Math.random() * (currentIndex + 1));
    const temporaryTrack = shuffled[currentIndex];
    shuffled[currentIndex] = shuffled[swapIndex];
    shuffled[swapIndex] = temporaryTrack;
  }
  return shuffled;
}

/**
 * Merge two track records while ignoring explicit `undefined` values on the override record.
 *
 * @param {object | undefined} fallbackTrack Base track metadata.
 * @param {object | undefined} primaryTrack Authoritative track fields.
 * @returns {object} Combined track object.
 */
function mergeTrackProperties(fallbackTrack, primaryTrack) {
  const mergedTrack = fallbackTrack && typeof fallbackTrack === "object" ? { ...fallbackTrack } : {};
  if (!primaryTrack || typeof primaryTrack !== "object") {
    return mergedTrack;
  }
  for (const [propertyKey, propertyValue] of Object.entries(primaryTrack)) {
    if (propertyValue !== undefined) {
      mergedTrack[propertyKey] = propertyValue;
    }
  }
  return mergedTrack;
}

/**
 * Index catalog albums and tracks by identifier for deterministic queue resolution.
 *
 * @param {object} catalog Hydrated or raw catalog payload.
 * @returns {{ albumByIdMap: Map<string, object>, trackByIdMap: Map<string, object>, allTracks: Array<object> }} Lookup maps.
 */
function buildCatalogLookup(catalog) {
  const albumByIdMap = new Map();
  const trackByIdMap = new Map();
  const albums = Array.isArray(catalog?.albums) ? catalog.albums : [];
  const catalogAllTracks = Array.isArray(catalog?.allTracks) ? catalog.allTracks : [];

  for (const albumEntry of albums) {
    if (!albumEntry || typeof albumEntry.id !== "string") {
      continue;
    }
    albumByIdMap.set(albumEntry.id, albumEntry);
    const albumTracks = Array.isArray(albumEntry.tracks) ? albumEntry.tracks : [];
    albumTracks.forEach((trackEntry, trackPosition) => {
      if (!trackEntry || typeof trackEntry.id !== "string") {
        return;
      }
      trackByIdMap.set(trackEntry.id, {
        ...trackEntry,
        artist: trackEntry.artist || albumEntry.artist || "",
        albumId: trackEntry.albumId || albumEntry.id,
        albumTitle: trackEntry.albumTitle || albumEntry.title || "",
        genre: trackEntry.genre || albumEntry.genre || "",
        ...(trackEntry.year !== undefined || albumEntry.year !== undefined
          ? { year: trackEntry.year ?? albumEntry.year }
          : {}),
        isFavorite: Boolean(trackEntry.isFavorite),
        trackNumber: trackEntry.trackNumber || String(trackPosition + 1).padStart(2, "0"),
      });
    });
  }

  const allTracks = [];
  for (const trackEntry of catalogAllTracks) {
    if (!trackEntry || typeof trackEntry.id !== "string") {
      continue;
    }
    const existingTrack = trackByIdMap.get(trackEntry.id);
    const mergedTrack = mergeTrackProperties(existingTrack, trackEntry);
    mergedTrack.isFavorite = Boolean(mergedTrack.isFavorite);
    trackByIdMap.set(mergedTrack.id, mergedTrack);
    allTracks.push(mergedTrack);
  }

  if (allTracks.length === 0 && trackByIdMap.size > 0) {
    for (const trackEntry of trackByIdMap.values()) {
      allTracks.push(trackEntry);
    }
  }

  return { albumByIdMap, trackByIdMap, allTracks };
}

/**
 * Create an audio playback engine that lazily acquires an audio element and
 * degrades gracefully to deterministic elapsed tracking in headless Node.
 *
 * @param {object} [options] Engine configuration options.
 * @param {() => object | null} [options.createAudioElement] Optional audio element factory.
 * @returns {object} Audio engine control API.
 */
export function createAudioEngine({ createAudioElement } = {}) {
  let audioElement = null;
  let listenersAttached = false;

  let playbackState = "idle";
  let isPlaying = false;
  let activeTrack = null;
  let activeAlbum = null;
  let queueTracks = [];
  let queueIndex = 0;
  let queueSource = "album";
  let queueAlbumId = null;
  let queuePlaylistId = null;
  let queueGenreTitle = null;
  let queueGenreAlbumIds = [];
  let playMode = PLAY_MODES[0];
  let elapsedSeconds = 0;
  let durationSeconds = 0;
  let volumeLevel = DEFAULT_VOLUME_LEVEL;
  let error = null;
  let cachedAlbumByIdMap = new Map();

  function applyAudioPlay(element) {
    if (!element || typeof element.play !== "function") {
      return;
    }
    try {
      const playPromise = element.play();
      if (playPromise && typeof playPromise.catch === "function") {
        playPromise.catch((playError) => {
          isPlaying = false;
          playbackState = activeTrack ? "paused" : "idle";
          error = playError instanceof Error ? playError.message : "audio_playback_blocked";
        });
      }
    } catch (playError) {
      isPlaying = false;
      playbackState = activeTrack ? "paused" : "idle";
      error = playError instanceof Error ? playError.message : "audio_playback_error";
    }
  }

  function loadQueueTrackAt(nextIndex, { autoplay }) {
    if (queueTracks.length === 0) {
      return;
    }
    const boundedIndex = clamp(Math.trunc(nextIndex), 0, queueTracks.length - 1);
    const nextTrack = queueTracks[boundedIndex];
    if (!nextTrack) {
      return;
    }

    queueIndex = boundedIndex;
    activeTrack = nextTrack;
    activeAlbum = cachedAlbumByIdMap.get(nextTrack.albumId) || null;
    elapsedSeconds = 0;
    durationSeconds =
      typeof nextTrack.durationSeconds === "number" &&
      Number.isFinite(nextTrack.durationSeconds) &&
      nextTrack.durationSeconds > 0
        ? nextTrack.durationSeconds
        : DEFAULT_TRACK_DURATION_SECONDS;
    error = null;
    isPlaying = Boolean(autoplay);
    playbackState = isPlaying ? "playing" : "paused";

    const element = ensureAudioElement();
    if (!element) {
      return;
    }

    element.volume = volumeLevel;
    element.src = nextTrack.audioUrl || "";
    try {
      element.currentTime = 0;
    } catch (seekError) {
      /* Ignore transient seek error before metadata loads */
    }
    if (typeof element.load === "function") {
      element.load();
    }
    if (isPlaying) {
      applyAudioPlay(element);
    } else if (typeof element.pause === "function" && !element.paused) {
      element.pause();
    }
  }

  function handleTrackEnded() {
    if (queueTracks.length === 0) {
      isPlaying = false;
      playbackState = "idle";
      return;
    }

    if (playMode === "repeat-one") {
      elapsedSeconds = 0;
      isPlaying = true;
      playbackState = "playing";
      if (audioElement) {
        try {
          audioElement.currentTime = 0;
        } catch (seekError) {
          /* Ignore transient seek error before metadata loads */
        }
        applyAudioPlay(audioElement);
      }
      return;
    }

    if (playMode === "repeat-all") {
      const nextIndex = (queueIndex + 1) % queueTracks.length;
      loadQueueTrackAt(nextIndex, { autoplay: true });
      return;
    }

    if (playMode === "shuffle") {
      if (queueTracks.length > 1) {
        const randomOffset = 1 + Math.floor(Math.random() * (queueTracks.length - 1));
        const nextIndex = (queueIndex + randomOffset) % queueTracks.length;
        loadQueueTrackAt(nextIndex, { autoplay: true });
      } else {
        loadQueueTrackAt(0, { autoplay: true });
      }
      return;
    }

    if (queueIndex + 1 < queueTracks.length) {
      loadQueueTrackAt(queueIndex + 1, { autoplay: true });
      return;
    }
    isPlaying = false;
    playbackState = activeTrack ? "paused" : "idle";
    if (audioElement && typeof audioElement.pause === "function" && !audioElement.paused) {
      audioElement.pause();
    }
  }

  function attachAudioListeners(element) {
    if (!element || listenersAttached || typeof element.addEventListener !== "function") {
      return;
    }
    listenersAttached = true;

    element.addEventListener("ended", () => {
      handleTrackEnded();
    });

    element.addEventListener("timeupdate", () => {
      if (
        typeof element.currentTime === "number" &&
        Number.isFinite(element.currentTime) &&
        element.currentTime >= 0
      ) {
        const upperLimit = durationSeconds > 0 ? durationSeconds : element.currentTime;
        elapsedSeconds = clamp(element.currentTime, 0, upperLimit);
      }
    });

    element.addEventListener("loadedmetadata", () => {
      if (
        typeof element.duration === "number" &&
        Number.isFinite(element.duration) &&
        element.duration > 0
      ) {
        durationSeconds = element.duration;
      }
    });

    element.addEventListener("error", () => {
      const elementErrorMessage =
        element.error && typeof element.error.message === "string"
          ? element.error.message
          : "audio_playback_error";
      error = elementErrorMessage;
      isPlaying = false;
      playbackState = activeTrack ? "paused" : "idle";
    });
  }

  function ensureAudioElement() {
    if (audioElement) {
      return audioElement;
    }
    let createdElement = null;
    if (typeof createAudioElement === "function") {
      createdElement = createAudioElement();
    } else if (typeof Audio === "function") {
      createdElement = new Audio();
    }
    if (!createdElement) {
      return null;
    }
    audioElement = createdElement;
    if (typeof audioElement.preload === "string" && audioElement.preload.length === 0) {
      audioElement.preload = "metadata";
    }
    attachAudioListeners(audioElement);
    return audioElement;
  }

  /**
   * Resolve a playback queue from `catalog` for `sourceSpec` and start playing `trackId`.
   * Ignores unknown or missing track identifiers without disrupting active playback.
   *
   * @param {object} sourceSpec Queue source descriptor and selected track identifier.
   * @param {object} catalog Library catalog payload.
   */
  function playTrackFromSource(sourceSpec, catalog) {
    if (!sourceSpec || typeof sourceSpec !== "object" || !catalog || typeof catalog !== "object") {
      return;
    }
    const {
      trackId,
      queueSource: requestedSource = "album",
      albumId,
      playlistId,
      genreTitle,
      genreAlbumIds,
      shuffle = false,
    } = sourceSpec;

    if (typeof trackId !== "string" || trackId.length === 0) {
      return;
    }

    const { albumByIdMap, trackByIdMap, allTracks } = buildCatalogLookup(catalog);
    const targetTrack = trackByIdMap.get(trackId);
    if (!targetTrack) {
      return;
    }

    let resolvedTracks = [];
    let resolvedAlbumId = null;
    if (requestedSource === "album") {
      resolvedAlbumId = albumId || targetTrack.albumId || null;
      const targetAlbum = albumByIdMap.get(resolvedAlbumId);
      if (targetAlbum && Array.isArray(targetAlbum.tracks)) {
        resolvedTracks = targetAlbum.tracks.map((trackEntry, trackPosition) => {
          const baseTrack = trackByIdMap.get(trackEntry.id) || {};
          const combinedTrack = mergeTrackProperties(baseTrack, trackEntry);
          return {
            ...combinedTrack,
            artist: combinedTrack.artist || targetAlbum.artist || "",
            albumId: targetAlbum.id,
            albumTitle: targetAlbum.title,
            genre: combinedTrack.genre || targetAlbum.genre || "",
            ...(combinedTrack.year !== undefined || targetAlbum.year !== undefined
              ? { year: combinedTrack.year ?? targetAlbum.year }
              : {}),
            isFavorite: Boolean(combinedTrack.isFavorite),
            trackNumber:
              combinedTrack.trackNumber || String(trackPosition + 1).padStart(2, "0"),
          };
        });
      }
    } else if (requestedSource === "playlist") {
      const playlists = Array.isArray(catalog.playlists) ? catalog.playlists : [];
      const targetPlaylist = playlists.find((playlistEntry) => playlistEntry.id === playlistId);
      const playlistTrackIds = Array.isArray(targetPlaylist?.trackIds)
        ? targetPlaylist.trackIds
        : [];
      resolvedTracks = playlistTrackIds
        .map((entryTrackId) => trackByIdMap.get(entryTrackId))
        .filter(Boolean);
    } else if (requestedSource === "favorites") {
      resolvedTracks = allTracks
        .filter((trackEntry) => Boolean(trackEntry.isFavorite))
        .sort(compareTrackTitles);
    } else if (requestedSource === "genre") {
      const albumIdSet = new Set(Array.isArray(genreAlbumIds) ? genreAlbumIds : []);
      resolvedTracks = allTracks
        .filter((trackEntry) => albumIdSet.has(trackEntry.albumId))
        .sort(compareTrackTitles);
    } else {
      resolvedTracks = allTracks.slice();
    }

    if (resolvedTracks.length === 0) {
      resolvedTracks = [targetTrack];
    }

    let selectedIndex = resolvedTracks.findIndex((trackEntry) => trackEntry.id === trackId);
    if (selectedIndex < 0) {
      resolvedTracks = [targetTrack, ...resolvedTracks];
      selectedIndex = 0;
    }

    if (shuffle) {
      const selectedTrack = resolvedTracks[selectedIndex];
      const remainingTracks = resolvedTracks.filter(
        (trackEntry, entryIndex) => entryIndex !== selectedIndex,
      );
      resolvedTracks = [selectedTrack, ...shuffleTracks(remainingTracks)];
      selectedIndex = 0;
    }

    cachedAlbumByIdMap = albumByIdMap;
    queueTracks = resolvedTracks;
    queueSource = typeof requestedSource === "string" && requestedSource.length > 0
      ? requestedSource
      : "album";
    queueAlbumId =
      queueSource === "album"
        ? resolvedAlbumId
        : typeof albumId === "string" && albumId.length > 0
          ? albumId
          : null;
    queuePlaylistId =
      queueSource === "playlist" && typeof playlistId === "string" && playlistId.length > 0
        ? playlistId
        : null;
    queueGenreTitle =
      queueSource === "genre" && typeof genreTitle === "string" && genreTitle.length > 0
        ? genreTitle
        : null;
    queueGenreAlbumIds =
      queueSource === "genre" && Array.isArray(genreAlbumIds)
        ? genreAlbumIds.slice()
        : [];

    loadQueueTrackAt(selectedIndex, { autoplay: true });
  }

  /**
   * Pause active playback without clearing the loaded track or position.
   */
  function pause() {
    if (activeTrack) {
      playbackState = "paused";
    }
    isPlaying = false;
    if (audioElement && typeof audioElement.pause === "function") {
      audioElement.pause();
    }
  }

  /**
   * Resume playback of the currently loaded track.
   */
  function play() {
    if (!activeTrack) {
      return;
    }
    error = null;
    isPlaying = true;
    playbackState = "playing";
    const element = ensureAudioElement();
    if (!element) {
      return;
    }
    element.volume = volumeLevel;
    if (!element.src && activeTrack.audioUrl) {
      element.src = activeTrack.audioUrl;
      if (typeof element.load === "function") {
        element.load();
      }
    }
    applyAudioPlay(element);
  }

  /**
   * Toggle between playing and paused states when a track is loaded.
   */
  function togglePlayPause() {
    if (isPlaying) {
      pause();
    } else {
      play();
    }
  }

  /**
   * Skip forward or backward across the active queue, wrapping cyclically.
   *
   * @param {number} direction Signed skip direction (+1 or -1).
   */
  function skipTrack(direction) {
    if (
      queueTracks.length === 0 ||
      typeof direction !== "number" ||
      !Number.isFinite(direction) ||
      direction === 0
    ) {
      return;
    }
    const stepDirection = Math.sign(direction);
    const trackCount = queueTracks.length;
    const nextIndex = (((queueIndex + stepDirection) % trackCount) + trackCount) % trackCount;
    loadQueueTrackAt(nextIndex, { autoplay: isPlaying || playbackState === "playing" });
  }

  /**
   * Jump directly to a clamped index in the active queue and start playback.
   *
   * @param {number} targetIndex Desired zero-based queue index.
   */
  function selectQueueIndex(targetIndex) {
    if (
      queueTracks.length === 0 ||
      typeof targetIndex !== "number" ||
      !Number.isFinite(targetIndex)
    ) {
      return;
    }
    const clampedIndex = clamp(Math.trunc(targetIndex), 0, queueTracks.length - 1);
    loadQueueTrackAt(clampedIndex, { autoplay: true });
  }

  /**
   * Set the active repeat/shuffle play mode when `modeId` is a valid PLAY_MODES value.
   *
   * @param {string} modeId Play mode identifier.
   */
  function setPlayMode(modeId) {
    if (typeof modeId === "string" && PLAY_MODES.includes(modeId)) {
      playMode = modeId;
    }
  }

  /**
   * Cycle forward through PLAY_MODES (`sequential` -> `shuffle` -> `repeat-all` -> `repeat-one`).
   */
  function cyclePlayMode() {
    const currentIndex = PLAY_MODES.indexOf(playMode);
    const nextIndex = currentIndex >= 0 ? (currentIndex + 1) % PLAY_MODES.length : 0;
    playMode = PLAY_MODES[nextIndex];
  }

  /**
   * Synchronize `isFavorite` across `queueTracks` and `activeTrack` for `trackId`.
   *
   * @param {string} trackId Track identifier to update.
   * @param {boolean} isFavorite Updated favorite state.
   */
  function updateTrackFavorite(trackId, isFavorite) {
    if (typeof trackId !== "string" || trackId.length === 0) {
      return;
    }
    const nextFavorite = Boolean(isFavorite);
    queueTracks = queueTracks.map((trackEntry) =>
      trackEntry.id === trackId ? { ...trackEntry, isFavorite: nextFavorite } : trackEntry,
    );
    if (activeTrack && activeTrack.id === trackId) {
      activeTrack = { ...activeTrack, isFavorite: nextFavorite };
    }
  }

  /**
   * Seek within `[0, durationSeconds]` and sync the underlying audio element time.
   *
   * @param {number} seconds Target playback offset in seconds.
   */
  function seekTo(seconds) {
    if (typeof seconds !== "number" || !Number.isFinite(seconds)) {
      return;
    }
    const upperDuration = durationSeconds > 0 ? durationSeconds : 0;
    const clampedSeconds = clamp(seconds, 0, upperDuration);
    elapsedSeconds = clampedSeconds;
    if (audioElement) {
      try {
        audioElement.currentTime = clampedSeconds;
      } catch (seekError) {
        /* Ignore transient seek error before metadata loads */
      }
    }
  }

  /**
   * Update normalized output volume clamped to `[0, 1]`.
   *
   * @param {number} level Target normalized volume level.
   */
  function setVolume(level) {
    if (typeof level !== "number" || !Number.isFinite(level)) {
      return;
    }
    volumeLevel = clamp(level, 0, 1);
    if (audioElement) {
      audioElement.volume = volumeLevel;
    }
  }

  /**
   * Synchronize playback progress from the live audio element or advance headless
   * elapsed seconds by `deltaSeconds` when playing.
   *
   * @param {number} deltaSeconds Frame elapsed time in seconds.
   */
  function step(deltaSeconds) {
    if (typeof deltaSeconds !== "number" || !Number.isFinite(deltaSeconds) || deltaSeconds <= 0) {
      return;
    }

    if (
      audioElement &&
      !audioElement.paused &&
      typeof audioElement.currentTime === "number" &&
      Number.isFinite(audioElement.currentTime) &&
      audioElement.currentTime > 0
    ) {
      if (
        typeof audioElement.duration === "number" &&
        Number.isFinite(audioElement.duration) &&
        audioElement.duration > 0
      ) {
        durationSeconds = audioElement.duration;
      }
      const upperDuration = durationSeconds > 0 ? durationSeconds : audioElement.currentTime;
      elapsedSeconds = clamp(audioElement.currentTime, 0, upperDuration);
      return;
    }

    if (isPlaying) {
      if (durationSeconds > 0 && elapsedSeconds + deltaSeconds >= durationSeconds) {
        elapsedSeconds = durationSeconds;
        handleTrackEnded();
        return;
      }
      const upperDuration = durationSeconds > 0 ? durationSeconds : elapsedSeconds + deltaSeconds;
      elapsedSeconds = clamp(elapsedSeconds + deltaSeconds, 0, upperDuration);
    }
  }

  /**
   * Return a shallow playback state snapshot suitable for UI telemetry and rendering.
   *
   * @returns {object} Current playback snapshot.
   */
  function getState() {
    return {
      playbackState,
      isPlaying,
      activeTrack,
      activeAlbum,
      queueTracks: queueTracks.slice(),
      queueIndex,
      queueSource,
      queueAlbumId,
      queuePlaylistId,
      queueGenreTitle,
      queueGenreAlbumIds: queueGenreAlbumIds.slice(),
      playMode,
      elapsedSeconds,
      durationSeconds,
      volume: volumeLevel,
      volumeLevel,
      error,
    };
  }

  return {
    playTrackFromSource,
    pause,
    play,
    togglePlayPause,
    skipTrack,
    selectQueueIndex,
    setPlayMode,
    cyclePlayMode,
    updateTrackFavorite,
    seekTo,
    setVolume,
    step,
    getState,
    getRenderSnapshot: getState,
  };
}
