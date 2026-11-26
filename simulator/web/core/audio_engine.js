/**
 * Headless-safe HTMLAudioElement playback engine for the DialOS simulator.
 * Manages context-scoped queue resolution, independent Shuffle + Repeat modes,
 * Fisher-Yates shuffle decks, live context mutations, and deterministic elapsed-time
 * stepping in browser and Node environments.
 */

import { clamp } from "./geometry.js";

const DEFAULT_TRACK_DURATION_SECONDS = 180;
const DEFAULT_VOLUME_LEVEL = 0.62;
const AUDIO_READY_STATE_METADATA = 1;
const AUDIO_NETWORK_STATE_NO_SOURCE = 3;

export const PREVIOUS_RESTART_THRESHOLD_SECONDS = 3;
export const REPEAT_MODES = Object.freeze(["off", "all", "one"]);

const CONTEXT_BADGE_MAP = Object.freeze({
  album: "ALBUM",
  artist: "ARTIST",
  playlist: "PLAYLIST",
  favorites: "FAVORITES",
  genre: "GENRE",
  "all-songs": "ALL SONGS",
});

/**
 * Resolve the uppercase display badge label for a playback context source.
 *
 * @param {string} queueSource Context source identifier.
 * @returns {string} Display badge string.
 */
export function resolveContextBadge(queueSource) {
  return CONTEXT_BADGE_MAP[queueSource] || "ALBUM";
}

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
 * Determine whether `trackEntry` belongs to `targetGenreTitle` via case-insensitive
 * per-track `genres` (falling back to single `genre`).
 *
 * @param {object | null} trackEntry Track record.
 * @param {string | null} targetGenreTitle Target genre label.
 * @returns {boolean} True when the track belongs to the requested genre.
 */
export function trackMatchesGenreName(trackEntry, targetGenreTitle) {
  if (!trackEntry || typeof targetGenreTitle !== "string") {
    return false;
  }
  const normalizedTarget = targetGenreTitle.trim().toLowerCase();
  if (normalizedTarget.length === 0) {
    return false;
  }
  const trackGenres =
    Array.isArray(trackEntry.genres) && trackEntry.genres.length > 0
      ? trackEntry.genres
      : trackEntry.genre
        ? [trackEntry.genre]
        : [];
  for (let genreIndex = 0; genreIndex < trackGenres.length; genreIndex += 1) {
    if (String(trackGenres[genreIndex] ?? "").trim().toLowerCase() === normalizedTarget) {
      return true;
    }
  }
  return false;
}

/**
 * Build a Fisher-Yates shuffled array of unique track IDs.
 * Anchors `anchorTrackId` at slot 0 when provided, or avoids placing `avoidFirstTrackId`
 * at slot 0 when wrapping into a fresh deck with more than one track.
 *
 * @param {Array<string>} trackIds Candidate track identifiers.
 * @param {string | null} [anchorTrackId=null] Optional track ID pinned at index 0.
 * @param {string | null} [avoidFirstTrackId=null] Optional track ID avoided at index 0 when unanchored.
 * @param {() => number} [randomFn=Math.random] RNG returning [0, 1).
 * @returns {Array<string>} Shuffled unique track IDs.
 */
export function buildShuffledTrackIds(
  trackIds,
  anchorTrackId = null,
  avoidFirstTrackId = null,
  randomFn = Math.random,
) {
  if (!Array.isArray(trackIds) || trackIds.length === 0) {
    return [];
  }
  const rng = typeof randomFn === "function" ? randomFn : Math.random;
  const uniqueIds = [];
  const seenIds = new Set();

  for (let index = 0; index < trackIds.length; index += 1) {
    const candidateId = trackIds[index];
    if (typeof candidateId === "string" && candidateId.length > 0 && !seenIds.has(candidateId)) {
      seenIds.add(candidateId);
      uniqueIds.push(candidateId);
    }
  }

  if (uniqueIds.length <= 1) {
    return uniqueIds;
  }

  const hasAnchor = Boolean(anchorTrackId && seenIds.has(anchorTrackId));
  const pool = hasAnchor ? uniqueIds.filter((id) => id !== anchorTrackId) : uniqueIds.slice();

  for (let currentIndex = pool.length - 1; currentIndex > 0; currentIndex -= 1) {
    const swapIndex = Math.floor(rng() * (currentIndex + 1));
    const temporaryId = pool[currentIndex];
    pool[currentIndex] = pool[swapIndex];
    pool[swapIndex] = temporaryId;
  }

  if (hasAnchor) {
    return [anchorTrackId, ...pool];
  }

  if (avoidFirstTrackId && pool.length > 1 && pool[0] === avoidFirstTrackId) {
    const swapIndex = pool.length === 2 ? 1 : 1 + Math.floor(rng() * (pool.length - 1));
    const temporaryId = pool[0];
    pool[0] = pool[swapIndex];
    pool[swapIndex] = temporaryId;
  }
  return pool;
}

/**
 * Pure decision helper shared by audio engine, Now Playing controller, and vinyl drag-peek
 * to compute the exact outcome of a manual skip (`+1` or `-1`) inside the active context.
 *
 * @param {object} snapshot Current queue and playback mode snapshot.
 * @param {number} direction Signed skip direction (`+1` or `-1`).
 * @returns {object} Skip outcome (`idle`, `restart-current`, `jump`, `reshuffle-wrap`, `boundary-bounce`).
 */
export function resolveSkipOutcome(snapshot, direction) {
  const queueTracks = Array.isArray(snapshot?.queueTracks) ? snapshot.queueTracks : [];
  const contextBadge = resolveContextBadge(snapshot?.queueSource);
  if (queueTracks.length === 0) {
    return { action: "idle", contextBadge };
  }

  const stepDirection = direction >= 0 ? 1 : -1;
  const activeIndex = clamp(
    Math.trunc(Number(snapshot?.queueIndex ?? 0)),
    0,
    queueTracks.length - 1,
  );
  const currentTrack = queueTracks[activeIndex];
  const elapsedSeconds = Number(snapshot?.elapsedSeconds ?? 0);

  if (stepDirection < 0 && elapsedSeconds > PREVIOUS_RESTART_THRESHOLD_SECONDS) {
    return {
      action: "restart-current",
      targetIndex: activeIndex,
      targetTrack: currentTrack,
      droppedEphemeral: false,
      contextBadge,
    };
  }

  const ephemeralId = snapshot?.ephemeralCurrentTrackId || null;
  const isCurrentEphemeral = Boolean(ephemeralId && currentTrack?.id === ephemeralId);
  const postTracks = isCurrentEphemeral
    ? queueTracks.filter((_, index) => index !== activeIndex)
    : queueTracks;
  const baseTrackCount =
    typeof snapshot?.baseTrackCount === "number" ? snapshot.baseTrackCount : postTracks.length;

  if (postTracks.length === 0 || baseTrackCount === 0) {
    return { action: "idle", contextBadge };
  }

  const candidateIndex = isCurrentEphemeral
    ? stepDirection > 0
      ? activeIndex
      : activeIndex - 1
    : activeIndex + stepDirection;

  if (candidateIndex >= 0 && candidateIndex < postTracks.length) {
    return {
      action: "jump",
      targetIndex: candidateIndex,
      targetTrack: postTracks[candidateIndex],
      droppedEphemeral: isCurrentEphemeral,
      contextBadge,
    };
  }

  if (snapshot?.repeatMode === "all") {
    if (postTracks.length === 1) {
      return {
        action: "restart-current",
        targetIndex: 0,
        targetTrack: postTracks[0],
        droppedEphemeral: isCurrentEphemeral,
        contextBadge,
      };
    }
    if (stepDirection > 0 && snapshot?.shuffleEnabled) {
      return {
        action: "reshuffle-wrap",
        avoidFirstTrackId: currentTrack?.id || null,
        droppedEphemeral: isCurrentEphemeral,
        contextBadge,
      };
    }
    const wrappedIndex = stepDirection > 0 ? 0 : postTracks.length - 1;
    return {
      action: "jump",
      targetIndex: wrappedIndex,
      targetTrack: postTracks[wrappedIndex],
      droppedEphemeral: isCurrentEphemeral,
      contextBadge,
    };
  }

  return {
    action: "boundary-bounce",
    boundary: stepDirection > 0 ? "end" : "start",
    contextBadge,
  };
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

function resolveTrackGenresList(primaryTrack, fallbackAlbum) {
  if (Array.isArray(primaryTrack?.genres) && primaryTrack.genres.length > 0) {
    return primaryTrack.genres.slice();
  }
  if (typeof primaryTrack?.genre === "string" && primaryTrack.genre.length > 0) {
    return [primaryTrack.genre];
  }
  if (typeof fallbackAlbum?.genre === "string" && fallbackAlbum.genre.length > 0) {
    return [fallbackAlbum.genre];
  }
  return [];
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
      const genres = resolveTrackGenresList(trackEntry, albumEntry);
      trackByIdMap.set(trackEntry.id, {
        ...trackEntry,
        artist: trackEntry.artist || albumEntry.artist || "",
        albumId: trackEntry.albumId || albumEntry.id,
        albumTitle: trackEntry.albumTitle || albumEntry.title || "",
        genre: trackEntry.genre || genres[0] || albumEntry.genre || "",
        genres,
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
    const owningAlbum = albumByIdMap.get(mergedTrack.albumId);
    mergedTrack.genres = resolveTrackGenresList(mergedTrack, owningAlbum);
    if (!mergedTrack.genre && mergedTrack.genres.length > 0) {
      mergedTrack.genre = mergedTrack.genres[0];
    }
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
 * @param {() => number} [options.random] Optional RNG for deterministic shuffle testing.
 * @returns {object} Audio engine control API.
 */
export function createAudioEngine({ createAudioElement, random } = {}) {
  const randomFn = typeof random === "function" ? random : Math.random;

  let audioElement = null;
  let listenersAttached = false;
  let audioElementLoadFailed = false;

  let playbackState = "idle";
  let isPlaying = false;
  let activeTrack = null;
  let activeAlbum = null;
  let baseQueueTracks = [];
  let queueTracks = [];
  let queueIndex = 0;
  let queueSource = "album";
  let queueAlbumId = null;
  let queueArtistName = null;
  let queueArtistAlbumIds = [];
  let queuePlaylistId = null;
  let queueGenreTitle = null;
  let queueGenreAlbumIds = [];
  let shuffleEnabled = false;
  let repeatMode = "off";
  let ephemeralCurrentTrackId = null;
  let elapsedSeconds = 0;
  let durationSeconds = 0;
  let pendingSeekSeconds = null;
  let volumeLevel = DEFAULT_VOLUME_LEVEL;
  let error = null;
  let cachedAlbumByIdMap = new Map();
  let cachedTrackByIdMap = new Map();
  let eventSeq = 0;
  let lastEvent = null;

  function recordEvent(type, message = "", meta = null) {
    eventSeq += 1;
    lastEvent = {
      seq: eventSeq,
      type,
      message: String(message ?? ""),
      ...(meta && typeof meta === "object" ? meta : {}),
    };
  }

  function applyAudioPlay(element) {
    if (!element || typeof element.play !== "function") {
      return;
    }
    try {
      const playPromise = element.play();
      if (playPromise && typeof playPromise.catch === "function") {
        playPromise.catch((playError) => {
          if (
            playError?.name === "NotSupportedError" ||
            Boolean(element.error) ||
            element.networkState === AUDIO_NETWORK_STATE_NO_SOURCE
          ) {
            audioElementLoadFailed = true;
          }
          isPlaying = false;
          playbackState = activeTrack ? "paused" : "idle";
          error = playError instanceof Error ? playError.message : "audio_playback_blocked";
        });
      }
    } catch (playError) {
      if (Boolean(element.error) || element.networkState === AUDIO_NETWORK_STATE_NO_SOURCE) {
        audioElementLoadFailed = true;
      }
      isPlaying = false;
      playbackState = activeTrack ? "paused" : "idle";
      error = playError instanceof Error ? playError.message : "audio_playback_error";
    }
  }

  function resetAudioElementToZero(element) {
    if (!element) {
      return;
    }
    try {
      element.currentTime = 0;
    } catch (seekError) {
      /* Ignore transient seek error before metadata loads */
    }
  }

  function stopToIdle(toastMessage = "") {
    activeTrack = null;
    activeAlbum = null;
    baseQueueTracks = [];
    queueTracks = [];
    queueIndex = 0;
    ephemeralCurrentTrackId = null;
    elapsedSeconds = 0;
    durationSeconds = 0;
    pendingSeekSeconds = null;
    isPlaying = false;
    playbackState = "idle";
    if (audioElement && typeof audioElement.pause === "function" && !audioElement.paused) {
      audioElement.pause();
    }
    if (toastMessage) {
      recordEvent("idle", toastMessage);
    }
  }

  function dropEphemeralTrackIfPresent() {
    if (!ephemeralCurrentTrackId) {
      return false;
    }
    const targetId = ephemeralCurrentTrackId;
    ephemeralCurrentTrackId = null;
    const removedIndex = queueTracks.findIndex((trackEntry) => trackEntry.id === targetId);
    if (removedIndex >= 0) {
      queueTracks = queueTracks.filter((_, index) => index !== removedIndex);
      if (removedIndex < queueIndex) {
        queueIndex = Math.max(0, queueIndex - 1);
      } else if (queueIndex >= queueTracks.length) {
        queueIndex = Math.max(0, queueTracks.length - 1);
      }
      return removedIndex === queueIndex || true;
    }
    return false;
  }

  function buildShuffledQueueFromBase(anchorTrackId, avoidFirstTrackId = null) {
    const baseTrackMap = new Map();
    const baseIds = [];
    for (const trackEntry of baseQueueTracks) {
      baseTrackMap.set(trackEntry.id, trackEntry);
      baseIds.push(trackEntry.id);
    }
    const orderedIds = buildShuffledTrackIds(baseIds, anchorTrackId, avoidFirstTrackId, randomFn);
    return orderedIds.map((id) => baseTrackMap.get(id)).filter(Boolean);
  }

  function loadQueueTrackAt(nextIndex, { autoplay }) {
    if (queueTracks.length === 0) {
      stopToIdle();
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
    pendingSeekSeconds = null;
    durationSeconds =
      typeof nextTrack.durationSeconds === "number" &&
      Number.isFinite(nextTrack.durationSeconds) &&
      nextTrack.durationSeconds > 0
        ? nextTrack.durationSeconds
        : DEFAULT_TRACK_DURATION_SECONDS;
    error = null;
    audioElementLoadFailed = false;
    isPlaying = Boolean(autoplay);
    playbackState = isPlaying ? "playing" : "paused";

    const element = ensureAudioElement();
    if (!element) {
      return;
    }

    element.volume = volumeLevel;
    element.src = nextTrack.audioUrl || "";
    resetAudioElementToZero(element);
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
      stopToIdle("NO TRACKS LEFT");
      return;
    }

    const currentTrack = queueTracks[queueIndex] || activeTrack;
    const wasEphemeral = Boolean(
      ephemeralCurrentTrackId && currentTrack && currentTrack.id === ephemeralCurrentTrackId,
    );

    if (repeatMode === "one" && !wasEphemeral) {
      elapsedSeconds = 0;
      pendingSeekSeconds = null;
      isPlaying = true;
      playbackState = "playing";
      if (audioElement) {
        resetAudioElementToZero(audioElement);
        applyAudioPlay(audioElement);
      }
      return;
    }

    if (wasEphemeral) {
      const removedIndex = queueTracks.findIndex(
        (trackEntry) => trackEntry.id === ephemeralCurrentTrackId,
      );
      ephemeralCurrentTrackId = null;
      if (removedIndex >= 0) {
        queueTracks = queueTracks.filter((_, index) => index !== removedIndex);
      }
      if (queueTracks.length === 0 || baseQueueTracks.length === 0) {
        stopToIdle("NO TRACKS LEFT");
        return;
      }
      if (removedIndex >= 0 && removedIndex < queueTracks.length) {
        loadQueueTrackAt(removedIndex, { autoplay: true });
        return;
      }
    } else if (queueIndex + 1 < queueTracks.length) {
      loadQueueTrackAt(queueIndex + 1, { autoplay: true });
      return;
    }

    if (baseQueueTracks.length === 0 || queueTracks.length === 0) {
      stopToIdle("NO TRACKS LEFT");
      return;
    }

    if (repeatMode === "all") {
      if (shuffleEnabled && queueTracks.length > 1) {
        queueTracks = buildShuffledQueueFromBase(null, currentTrack?.id || null);
      }
      loadQueueTrackAt(0, { autoplay: true });
      return;
    }

    // Repeat OFF reached the end of the context: reset cleanly to track 0 at 0:00 paused.
    loadQueueTrackAt(0, { autoplay: false });
    recordEvent("context-ended", `END OF ${resolveContextBadge(queueSource)}`);
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
        pendingSeekSeconds === null &&
        typeof element.currentTime === "number" &&
        Number.isFinite(element.currentTime) &&
        element.currentTime >= 0
      ) {
        const upperLimit = durationSeconds > 0 ? durationSeconds : element.currentTime;
        elapsedSeconds = clamp(element.currentTime, 0, upperLimit);
      }
    });

    element.addEventListener("loadedmetadata", () => {
      audioElementLoadFailed = false;
      if (
        typeof element.duration === "number" &&
        Number.isFinite(element.duration) &&
        element.duration > 0
      ) {
        durationSeconds = element.duration;
      }
      if (pendingSeekSeconds !== null) {
        const upperDuration = durationSeconds > 0 ? durationSeconds : 0;
        const targetPendingSeek = clamp(pendingSeekSeconds, 0, upperDuration);
        pendingSeekSeconds = null;
        elapsedSeconds = targetPendingSeek;
        try {
          element.currentTime = targetPendingSeek;
        } catch (seekError) {
          /* Ignore transient seek error if audio element is not yet seekable */
        }
      }
    });

    element.addEventListener("error", () => {
      audioElementLoadFailed = true;
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

  function resolveTracksForContext(sourceSpec, { albumByIdMap, trackByIdMap, allTracks }, targetTrack) {
    const requestedSource = sourceSpec.queueSource || "album";
    let resolvedTracks = [];
    let resolvedAlbumId = null;

    if (requestedSource === "album") {
      resolvedAlbumId = sourceSpec.albumId || targetTrack.albumId || null;
      const targetAlbum = albumByIdMap.get(resolvedAlbumId);
      if (targetAlbum && Array.isArray(targetAlbum.tracks)) {
        resolvedTracks = targetAlbum.tracks.map((trackEntry, trackPosition) => {
          const baseTrack = trackByIdMap.get(trackEntry.id) || {};
          const combinedTrack = mergeTrackProperties(baseTrack, trackEntry);
          const genres = resolveTrackGenresList(combinedTrack, targetAlbum);
          return {
            ...combinedTrack,
            artist: combinedTrack.artist || targetAlbum.artist || "",
            albumId: targetAlbum.id,
            albumTitle: targetAlbum.title,
            genre: combinedTrack.genre || genres[0] || targetAlbum.genre || "",
            genres,
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
      const playlists = Array.isArray(sourceSpec.playlists)
        ? sourceSpec.playlists
        : [];
      const targetPlaylist = playlists.find(
        (playlistEntry) => playlistEntry.id === sourceSpec.playlistId,
      );
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
    } else if (requestedSource === "artist") {
      const albumIdSet = new Set(
        Array.isArray(sourceSpec.artistAlbumIds) ? sourceSpec.artistAlbumIds : [],
      );
      const seenTrackIds = new Set();
      for (const albumId of albumIdSet) {
        const targetAlbum = albumByIdMap.get(albumId);
        if (!targetAlbum || !Array.isArray(targetAlbum.tracks)) {
          continue;
        }
        targetAlbum.tracks.forEach((trackEntry, trackPosition) => {
          if (!trackEntry || typeof trackEntry.id !== "string" || seenTrackIds.has(trackEntry.id)) {
            return;
          }
          seenTrackIds.add(trackEntry.id);
          const baseTrack = trackByIdMap.get(trackEntry.id) || {};
          const combinedTrack = mergeTrackProperties(baseTrack, trackEntry);
          const genres = resolveTrackGenresList(combinedTrack, targetAlbum);
          resolvedTracks.push({
            ...combinedTrack,
            artist: combinedTrack.artist || targetAlbum.artist || "",
            albumId: targetAlbum.id,
            albumTitle: targetAlbum.title,
            genre: combinedTrack.genre || genres[0] || targetAlbum.genre || "",
            genres,
            ...(combinedTrack.year !== undefined || targetAlbum.year !== undefined
              ? { year: combinedTrack.year ?? targetAlbum.year }
              : {}),
            isFavorite: Boolean(combinedTrack.isFavorite),
            trackNumber:
              combinedTrack.trackNumber || String(trackPosition + 1).padStart(2, "0"),
          });
        });
      }
      for (const trackEntry of allTracks) {
        if (
          trackEntry &&
          typeof trackEntry.id === "string" &&
          albumIdSet.has(trackEntry.albumId) &&
          !seenTrackIds.has(trackEntry.id)
        ) {
          seenTrackIds.add(trackEntry.id);
          resolvedTracks.push(trackEntry);
        }
      }
      resolvedTracks.sort(compareTrackTitles);
    } else if (requestedSource === "genre") {
      if (typeof sourceSpec.genreTitle === "string" && sourceSpec.genreTitle.trim().length > 0) {
        resolvedTracks = allTracks
          .filter((trackEntry) => trackMatchesGenreName(trackEntry, sourceSpec.genreTitle))
          .sort(compareTrackTitles);
      } else {
        const albumIdSet = new Set(
          Array.isArray(sourceSpec.genreAlbumIds) ? sourceSpec.genreAlbumIds : [],
        );
        resolvedTracks = allTracks
          .filter((trackEntry) => albumIdSet.has(trackEntry.albumId))
          .sort(compareTrackTitles);
      }
    } else {
      resolvedTracks = allTracks.slice();
    }

    return { resolvedTracks, resolvedAlbumId };
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
      artistName,
      artistAlbumIds,
      playlistId,
      genreTitle,
      genreAlbumIds,
      shuffle,
      shuffleOverride,
    } = sourceSpec;

    if (typeof trackId !== "string" || trackId.length === 0) {
      return;
    }

    const lookup = buildCatalogLookup(catalog);
    const { albumByIdMap, trackByIdMap } = lookup;
    const targetTrack = trackByIdMap.get(trackId);
    if (!targetTrack) {
      return;
    }

    if (typeof shuffleOverride === "boolean") {
      shuffleEnabled = shuffleOverride;
    } else if (typeof shuffle === "boolean") {
      shuffleEnabled = shuffle;
    }

    const { resolvedTracks: contextTracks, resolvedAlbumId } = resolveTracksForContext(
      {
        queueSource: requestedSource,
        albumId,
        artistName,
        artistAlbumIds,
        playlistId,
        genreTitle,
        genreAlbumIds,
        playlists: catalog.playlists,
      },
      lookup,
      targetTrack,
    );

    let resolvedTracks = contextTracks.slice();
    if (resolvedTracks.length === 0) {
      resolvedTracks = [targetTrack];
    }

    let selectedIndex = resolvedTracks.findIndex((trackEntry) => trackEntry.id === trackId);
    if (selectedIndex < 0) {
      resolvedTracks = [targetTrack, ...resolvedTracks];
      selectedIndex = 0;
    }

    cachedAlbumByIdMap = albumByIdMap;
    cachedTrackByIdMap = trackByIdMap;
    baseQueueTracks = resolvedTracks.slice();
    ephemeralCurrentTrackId = null;

    if (shuffleEnabled) {
      queueTracks = buildShuffledQueueFromBase(targetTrack.id, null);
      selectedIndex = 0;
    } else {
      queueTracks = resolvedTracks.slice();
    }

    queueSource =
      typeof requestedSource === "string" && requestedSource.length > 0
        ? requestedSource
        : "album";
    queueAlbumId =
      queueSource === "album"
        ? resolvedAlbumId
        : typeof albumId === "string" && albumId.length > 0
          ? albumId
          : null;
    queueArtistName =
      queueSource === "artist" && typeof artistName === "string" && artistName.length > 0
        ? artistName
        : null;
    queueArtistAlbumIds =
      queueSource === "artist" && Array.isArray(artistAlbumIds)
        ? artistAlbumIds.slice()
        : [];
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

    if (activeTrack && targetTrack.id === activeTrack.id) {
      queueIndex = selectedIndex;
      activeTrack = queueTracks[selectedIndex];
      activeAlbum = cachedAlbumByIdMap.get(activeTrack.albumId) || null;
      if (!isPlaying || playbackState !== "playing" || (audioElement && audioElement.paused)) {
        play();
      }
      return;
    }

    pendingSeekSeconds = null;
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
    const needsSourceReload =
      !element.src ||
      audioElementLoadFailed ||
      Boolean(element.error) ||
      element.networkState === AUDIO_NETWORK_STATE_NO_SOURCE;
    if (needsSourceReload && activeTrack.audioUrl) {
      audioElementLoadFailed = false;
      element.src = activeTrack.audioUrl;
      if (elapsedSeconds > 0) {
        pendingSeekSeconds = elapsedSeconds;
      }
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
   * Skip forward or backward strictly within the active context, honoring the 3-second
   * restart rule, active Shuffle deck, Repeat modes, and hard context boundaries.
   *
   * @param {number} direction Signed skip direction (`+1` or `-1`).
   * @returns {object | null} Skip outcome descriptor for UI feedback.
   */
  function skipTrack(direction) {
    if (typeof direction !== "number" || !Number.isFinite(direction) || direction === 0) {
      return null;
    }
    const stepDirection = Math.sign(direction);
    const outcome = resolveSkipOutcome(
      {
        queueTracks,
        queueIndex,
        elapsedSeconds,
        repeatMode,
        shuffleEnabled,
        queueSource,
        ephemeralCurrentTrackId,
        baseTrackCount: baseQueueTracks.length,
      },
      stepDirection,
    );

    if (outcome.action === "idle") {
      stopToIdle("NO TRACKS LEFT");
      return outcome;
    }

    if (outcome.action === "restart-current") {
      if (outcome.droppedEphemeral) {
        dropEphemeralTrackIfPresent();
        if (queueTracks.length === 0) {
          stopToIdle("NO TRACKS LEFT");
          return { action: "idle", contextBadge: outcome.contextBadge };
        }
        loadQueueTrackAt(0, { autoplay: isPlaying || playbackState === "playing" });
        return outcome;
      }
      elapsedSeconds = 0;
      pendingSeekSeconds = null;
      if (audioElement) {
        resetAudioElementToZero(audioElement);
      }
      return outcome;
    }

    if (outcome.action === "boundary-bounce") {
      const boundaryMessage = `${outcome.boundary === "end" ? "END OF " : "START OF "}${outcome.contextBadge}`;
      recordEvent("boundary-bounce", boundaryMessage);
      return outcome;
    }

    if (outcome.droppedEphemeral) {
      dropEphemeralTrackIfPresent();
    }

    if (outcome.action === "reshuffle-wrap") {
      if (baseQueueTracks.length === 0) {
        stopToIdle("NO TRACKS LEFT");
        return { action: "idle", contextBadge: outcome.contextBadge };
      }
      queueTracks = buildShuffledQueueFromBase(null, outcome.avoidFirstTrackId);
      loadQueueTrackAt(0, { autoplay: isPlaying || playbackState === "playing" });
      return outcome;
    }

    loadQueueTrackAt(outcome.targetIndex, { autoplay: isPlaying || playbackState === "playing" });
    return outcome;
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
    const selectedTrack = queueTracks[clampedIndex];
    if (selectedTrack && activeTrack && selectedTrack.id === activeTrack.id) {
      queueIndex = clampedIndex;
      if (!isPlaying || playbackState !== "playing" || (audioElement && audioElement.paused)) {
        play();
      }
      return;
    }
    let resolvedIndex = clampedIndex;
    if (ephemeralCurrentTrackId && selectedTrack && selectedTrack.id !== ephemeralCurrentTrackId) {
      dropEphemeralTrackIfPresent();
      const postIndex = queueTracks.findIndex((trackEntry) => trackEntry.id === selectedTrack.id);
      if (postIndex < 0) {
        return;
      }
      resolvedIndex = postIndex;
    }
    loadQueueTrackAt(resolvedIndex, { autoplay: true });
  }

  /**
   * Enable or disable Fisher-Yates Shuffle while keeping the active track anchored.
   *
   * @param {boolean} nextEnabled Desired shuffle state.
   */
  function setShuffleEnabled(nextEnabled) {
    if (typeof nextEnabled !== "boolean") {
      return;
    }
    const targetEnabled = nextEnabled;
    if (shuffleEnabled === targetEnabled) {
      return;
    }
    shuffleEnabled = targetEnabled;
    if (queueTracks.length > 0 && activeTrack) {
      const currentId = activeTrack.id;
      const isEphemeral = Boolean(ephemeralCurrentTrackId && ephemeralCurrentTrackId === currentId);
      if (shuffleEnabled) {
        if (isEphemeral) {
          queueTracks = [activeTrack, ...buildShuffledQueueFromBase(null, null)];
        } else {
          queueTracks = buildShuffledQueueFromBase(currentId, null);
        }
        queueIndex = 0;
      } else {
        queueTracks = baseQueueTracks.slice();
        if (isEphemeral) {
          const insertIndex = clamp(queueIndex, 0, queueTracks.length);
          queueTracks.splice(insertIndex, 0, activeTrack);
        }
        const naturalIndex = queueTracks.findIndex((trackEntry) => trackEntry.id === currentId);
        queueIndex = naturalIndex >= 0 ? naturalIndex : 0;
      }
    }
    recordEvent("shuffle", shuffleEnabled ? "SHUFFLE // ON" : "SHUFFLE // OFF");
  }

  /**
   * Toggle Shuffle ON or OFF.
   *
   * @returns {boolean} Resulting shuffle state.
   */
  function toggleShuffle() {
    setShuffleEnabled(!shuffleEnabled);
    return shuffleEnabled;
  }

  /**
   * Set the active repeat mode when `modeId` is a valid REPEAT_MODES value.
   *
   * @param {string} modeId Repeat mode (`off` | `all` | `one`).
   */
  function setRepeatMode(modeId) {
    if (typeof modeId === "string" && REPEAT_MODES.includes(modeId)) {
      repeatMode = modeId;
      recordEvent("repeat", `REPEAT // ${modeId.toUpperCase()}`);
    }
  }

  /**
   * Cycle forward through REPEAT_MODES (`off` -> `all` -> `one` -> `off`).
   *
   * @returns {string} Resulting repeat mode.
   */
  function cycleRepeatMode() {
    const currentIndex = REPEAT_MODES.indexOf(repeatMode);
    const nextIndex = currentIndex >= 0 ? (currentIndex + 1) % REPEAT_MODES.length : 0;
    repeatMode = REPEAT_MODES[nextIndex];
    recordEvent("repeat", `REPEAT // ${repeatMode.toUpperCase()}`);
    return repeatMode;
  }

  /**
   * Synchronize `isFavorite` across active playback and live-update the `favorites` context
   * without prematurely interrupting the currently playing track.
   *
   * @param {string} trackId Track identifier to update.
   * @param {boolean} isFavorite Updated favorite state.
   * @param {object} [updatedCatalog=null] Optional refreshed catalog snapshot for newly hearted tracks.
   */
  function updateTrackFavorite(trackId, isFavorite, updatedCatalog = null) {
    if (typeof trackId !== "string" || trackId.length === 0) {
      return;
    }
    if (updatedCatalog && typeof updatedCatalog === "object") {
      const refreshedLookup = buildCatalogLookup(updatedCatalog);
      cachedAlbumByIdMap = refreshedLookup.albumByIdMap;
      cachedTrackByIdMap = refreshedLookup.trackByIdMap;
    }
    const nextFavorite = Boolean(isFavorite);
    const applyFlag = (trackEntry) =>
      trackEntry.id === trackId ? { ...trackEntry, isFavorite: nextFavorite } : trackEntry;

    baseQueueTracks = baseQueueTracks.map(applyFlag);
    queueTracks = queueTracks.map(applyFlag);
    if (activeTrack && activeTrack.id === trackId) {
      activeTrack = { ...activeTrack, isFavorite: nextFavorite };
    }
    const cachedTrack = cachedTrackByIdMap.get(trackId);
    if (cachedTrack) {
      cachedTrackByIdMap.set(trackId, { ...cachedTrack, isFavorite: nextFavorite });
    }

    if (queueSource !== "favorites" || !activeTrack) {
      return;
    }

    if (!nextFavorite) {
      baseQueueTracks = baseQueueTracks.filter((trackEntry) => trackEntry.id !== trackId);
      if (activeTrack.id === trackId) {
        ephemeralCurrentTrackId = trackId;
      } else {
        queueTracks = queueTracks.filter((trackEntry) => trackEntry.id !== trackId);
        const alignedIndex = queueTracks.findIndex(
          (trackEntry) => trackEntry.id === activeTrack.id,
        );
        if (alignedIndex >= 0) {
          queueIndex = alignedIndex;
        }
      }
      return;
    }

    if (ephemeralCurrentTrackId === trackId) {
      ephemeralCurrentTrackId = null;
    }
    const trackRecord = cachedTrackByIdMap.get(trackId) || (activeTrack.id === trackId ? activeTrack : null);
    if (!trackRecord) {
      return;
    }
    const updatedRecord = { ...trackRecord, isFavorite: true };
    if (!baseQueueTracks.some((trackEntry) => trackEntry.id === trackId)) {
      baseQueueTracks = [...baseQueueTracks, updatedRecord].sort(compareTrackTitles);
    }
    if (!queueTracks.some((trackEntry) => trackEntry.id === trackId)) {
      if (shuffleEnabled) {
        queueTracks = [...queueTracks, updatedRecord];
      } else {
        queueTracks = baseQueueTracks.slice();
        if (ephemeralCurrentTrackId) {
          const ephemeralTrack = cachedTrackByIdMap.get(ephemeralCurrentTrackId) || activeTrack;
          if (ephemeralTrack && !queueTracks.some((entry) => entry.id === ephemeralTrack.id)) {
            queueTracks.splice(clamp(queueIndex, 0, queueTracks.length), 0, ephemeralTrack);
          }
        }
      }
    }
    const alignedIndex = queueTracks.findIndex((trackEntry) => trackEntry.id === activeTrack.id);
    if (alignedIndex >= 0) {
      queueIndex = alignedIndex;
    }
  }

  /**
   * Reconcile an active `playlist` playback context against live playlist edits or deletion.
   *
   * @param {Array<object>} nextPlaylists Updated playlist collection.
   * @param {object} [updatedCatalog=null] Optional refreshed catalog snapshot.
   */
  function syncPlaylists(nextPlaylists, updatedCatalog = null) {
    if (updatedCatalog && typeof updatedCatalog === "object") {
      const refreshedLookup = buildCatalogLookup(updatedCatalog);
      cachedAlbumByIdMap = refreshedLookup.albumByIdMap;
      cachedTrackByIdMap = refreshedLookup.trackByIdMap;
    }
    if (queueSource !== "playlist" || !activeTrack) {
      return;
    }
    const playlistList = Array.isArray(nextPlaylists) ? nextPlaylists : [];
    const targetPlaylist = playlistList.find((entry) => entry && entry.id === queuePlaylistId);
    if (!targetPlaylist) {
      stopToIdle("PLAYLIST DELETED");
      return;
    }

    const nextIds = Array.isArray(targetPlaylist.trackIds) ? targetPlaylist.trackIds : [];
    const nextBaseTracks = nextIds
      .map((trackId) => cachedTrackByIdMap.get(trackId))
      .filter(Boolean);
    const nextBaseIds = new Set(nextBaseTracks.map((trackEntry) => trackEntry.id));
    baseQueueTracks = nextBaseTracks;

    const currentStillInBase = nextBaseIds.has(activeTrack.id);
    if (!currentStillInBase) {
      ephemeralCurrentTrackId = activeTrack.id;
    } else if (ephemeralCurrentTrackId === activeTrack.id) {
      ephemeralCurrentTrackId = null;
    }

    if (shuffleEnabled) {
      const retained = [];
      const seen = new Set();
      for (const trackEntry of queueTracks) {
        if ((nextBaseIds.has(trackEntry.id) || trackEntry.id === ephemeralCurrentTrackId) && !seen.has(trackEntry.id)) {
          retained.push(nextBaseTracks.find((entry) => entry.id === trackEntry.id) || trackEntry);
          seen.add(trackEntry.id);
        }
      }
      for (const baseEntry of nextBaseTracks) {
        if (!seen.has(baseEntry.id)) {
          retained.push(baseEntry);
          seen.add(baseEntry.id);
        }
      }
      queueTracks = retained;
    } else {
      queueTracks = nextBaseTracks.slice();
      if (ephemeralCurrentTrackId) {
        const insertIndex = clamp(queueIndex, 0, queueTracks.length);
        queueTracks.splice(insertIndex, 0, activeTrack);
      }
    }

    const alignedIndex = queueTracks.findIndex((trackEntry) => trackEntry.id === activeTrack.id);
    queueIndex = alignedIndex >= 0 ? alignedIndex : 0;
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
      const elementReady =
        typeof audioElement.readyState !== "number" ||
        audioElement.readyState >= AUDIO_READY_STATE_METADATA;
      if (elementReady) {
        pendingSeekSeconds = null;
        try {
          audioElement.currentTime = clampedSeconds;
        } catch (seekError) {
          /* Ignore transient seek error before metadata loads */
        }
      } else {
        pendingSeekSeconds = clampedSeconds;
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

    if (audioElement) {
      if (
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
      }
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
      baseTrackCount: baseQueueTracks.length,
      queueIndex,
      queueSource,
      queueAlbumId,
      queueArtistName,
      queueArtistAlbumIds: queueArtistAlbumIds.slice(),
      queuePlaylistId,
      queueGenreTitle,
      queueGenreAlbumIds: queueGenreAlbumIds.slice(),
      shuffleEnabled,
      repeatMode,
      ephemeralCurrentTrackId,
      elapsedSeconds,
      durationSeconds,
      volume: volumeLevel,
      volumeLevel,
      lastEvent,
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
    setShuffleEnabled,
    toggleShuffle,
    setRepeatMode,
    cycleRepeatMode,
    updateTrackFavorite,
    syncPlaylists,
    seekTo,
    setVolume,
    step,
    getState,
    getRenderSnapshot: getState,
  };
}
