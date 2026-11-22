/**
 * Pure deterministic Library controller, Hub tile geometry, curved-list layout,
 * rim-arc / Crystal-Slab Letter Seek state, and Playlist Action Sheet / Hold-to-Delete
 * mechanics for the DialOS 360x360 display simulator.
 */

import {
  DISPLAY_CENTER_PX,
  FULL_CIRCLE_RADIANS,
  TOP_ANGLE_RADIANS,
  clamp,
  easeInOutCubic,
  formatIntegerCount,
  lerp,
} from "./geometry.js";

const LETTER_STEP_RADIANS = FULL_CIRCLE_RADIANS * 0.06;
const RIM_SLICE_COUNT = Math.round(FULL_CIRCLE_RADIANS / LETTER_STEP_RADIANS);

export const LIBRARY_CONSTANTS = Object.freeze({
  LIST_TOP_SLOT_Y: 126,
  LIST_ROW_PITCH_PX: 74,
  LIST_FULL_SLOTS: 2,
  CRATE_CENTER_Y: 152,
  CRATE_SPACING_PX: 130,
  DRAG_PIXELS_PER_ROW: 74,
  DRAG_PIXELS_PER_PAIR: 116,
  DRAG_PIXELS_PER_ALBUM: 108,
  ARC_RIM_MIN_RADIUS_PX: 112,
  ARC_RADIANS_PER_STEP: 0.36,
  LETTER_SEARCH_THRESHOLD_RADIANS: Math.PI,
  LETTER_STEP_RADIANS,
  RIM_SLICE_COUNT,
  TRANSITION_DURATION_SECONDS: 0.28,
  VINYL_LAUNCH_DURATION_SECONDS: 0.62,
  DELETE_HOLD_DURATION_SECONDS: 1.10,
  LONG_PRESS_DURATION_SECONDS: 0.48,
  CRATE_SWIPE_UP_MIN_DELTA_Y: 26,
  CRATE_SWIPE_UP_SLOPE_RATIO: 1.15,
  BACK_SWIPE_DISTANCE_PX: 96,
  BACK_SWIPE_COMMIT_THRESHOLD: 0.45,
  TAP_DRIFT_LIMIT_PX: 10,
});

export const LIBRARY_HUB_SECTIONS = Object.freeze([
  Object.freeze({ id: "favorites", title: "Favorites" }),
  Object.freeze({ id: "playlists", title: "Playlists" }),
  Object.freeze({ id: "artists", title: "Artists" }),
  Object.freeze({ id: "albums", title: "Albums" }),
  Object.freeze({ id: "genres", title: "Genres" }),
  Object.freeze({ id: "all-songs", title: "All songs" }),
]);

const HUB_ROW_TOP_CENTER_Y = 152;
const HUB_ROW_PITCH_PX = 126;
const HUB_PEEK_DELTA_THRESHOLD = 0.2;
const HUB_FULL_TILE_WIDTH_PX = 114;
const HUB_PEEK_TILE_WIDTH_PX = 94;
const HUB_FULL_TILE_HEIGHT_PX = 112;
const HUB_PEEK_TILE_HEIGHT_PX = 88;
const HUB_FULL_HALF_SPREAD_PX = 64;
const HUB_PEEK_HALF_SPREAD_PX = 54;
const HUB_EXIT_FADE_ROWS = 0.65;
const HUB_BEYOND_FADE_ROWS = 0.55;
const HUB_PEEK_ALPHA = 0.38;

const LIST_CURVE_CHORD_RADIUS_PX = 154;
const LIST_CHORD_INSET_PX = 18;
const LIST_MIN_HALF_WIDTH_PX = 66;
const LIST_MAX_HALF_WIDTH_PX = 126;
const LIST_FULL_ROW_HEIGHT_PX = 62;
const LIST_COMPACT_ROW_HEIGHT_PX = 46;
const LIST_EXIT_FADE_SLOTS = 0.68;
const LIST_BEYOND_FADE_SLOTS = 0.62;
const LIST_PREVIEW_WIDTH_REDUCTION_PX = 8;
const LIST_PREVIEW_ALPHA = 0.42;
const LIST_HIT_PADDING_Y_PX = 3;
const LIST_HIT_MIN_ALPHA = 0.22;
const HUB_HIT_MIN_ALPHA = 0.25;

const SLICE_BOUNDARY_EPSILON = 1e-9;
const MIN_ALPHABETICAL_INDEX_COUNT = 3;
const MIN_LETTER_GROUPS_FOR_SEEK = 2;
const CRATE_BAR_MIN_WIDTH_PX = 6;
const CRATE_BAR_MAX_WIDTH_PX = 18;
const DEFAULT_CRATE_BAR_LIMIT = 7;

const VINYL_SPIN_RADIANS_PER_SECOND = 0.55;
const VINYL_NUDGE_SPRING_RATE = 8.5;
const VINYL_LAUNCH_NUDGE_RADIANS = 0.44;
const VINYL_CLOSE_NUDGE_RADIANS = 0.34;
const SHEET_SPRING_RATE = 14;
const SHEET_SETTLE_EPSILON = 0.004;
const PRESS_ATTACK_RATE = 32;
const PRESS_RELEASE_RATE = 18;
const PRESS_INITIAL_LEVEL = 0.35;
const PRESS_CLEAR_THRESHOLD = 0.01;
const PRESS_FLASH_DECAY_RATE = 7.5;
const DELETE_HOLD_DECAY_RATE = 3.8;
const DELETE_FLASH_DECAY_RATE = 2.8;

const RIM_SLAB_NEIGHBOR_ONE_PULL = 0.34;
const RIM_SLAB_NEIGHBOR_TWO_PULL = 0.10;
const RIM_SLAB_RISE_RATE = 30;
const RIM_SLAB_FALL_RATE = 11;
const RIM_SLAB_PEAK_SNAP_THRESHOLD = 0.94;
const RIM_SLAB_ZERO_THRESHOLD = 0.005;

const LETTER_HUD_ATTACK_RATE = 18;
const LETTER_HUD_RELEASE_RATE = 11;
const ARC_FEEDBACK_DECAY_RATE = 3.8;
const SCROLL_SNAP_RATE = 13.5;
const SCROLL_SETTLE_EPSILON = 0.001;
const BACK_SWIPE_DECAY_RATE = 4.5;

const RIM_LEFT_SWIPE_START_MAX_X = 68;
const RIM_LEFT_SWIPE_MIN_DELTA_X = 12;
const RIM_LEFT_SWIPE_SLOPE_RATIO = 1.35;
const RIM_ENGAGE_MIN_RADIUS_PX = 94;
const RIM_ENGAGE_MIN_ANGLE_DELTA = 0.03;
const ARC_MIN_ANGLE_STEP = 0.0005;

const LIST_BACK_SWIPE_START_MAX_X = 116;
const LIST_BACK_SWIPE_MIN_DELTA_X = 26;
const LIST_BACK_SWIPE_SLOPE_RATIO = 1.45;
const OVERLAY_LEFT_EDGE_SWIPE_MAX_X = 74;
const OVERLAY_BACK_SWIPE_MIN_DELTA_X = 18;

const DRAG_OVERSCROLL_SLOTS = 0.35;
const LIST_FLICK_VELOCITY_WEIGHT = 0.04;
const CRATE_FLICK_VELOCITY_WEIGHT = 0.045;
const FLICK_MAX_PROJECTED_SLOTS = 0.45;

const DELETE_BUTTON_LEFT_X = 82;
const DELETE_BUTTON_RIGHT_X = 278;
const DELETE_BUTTON_TOP_Y = 232;
const DELETE_BUTTON_BOTTOM_Y = 268;
const DELETE_HOLD_GUARD_LEFT_X = 74;
const DELETE_HOLD_GUARD_RIGHT_X = 286;
const DELETE_HOLD_GUARD_TOP_Y = 224;
const DELETE_HOLD_GUARD_BOTTOM_Y = 278;

const SHEET_PLAY_TOP_Y = 146;
const SHEET_PLAY_BOTTOM_Y = 184;
const SHEET_SHUFFLE_TOP_Y = 188;
const SHEET_SHUFFLE_BOTTOM_Y = 226;
const SHEET_CARD_LEFT_X = 68;
const SHEET_CARD_RIGHT_X = 292;
const SHEET_CARD_TOP_Y = 84;
const SHEET_CARD_BOTTOM_Y = 282;

const EMPTY_BACK_CTA_TOP_Y = 236;
const EMPTY_BACK_CTA_BOTTOM_Y = 284;
const EMPTY_BACK_CTA_HALF_WIDTH_PX = 88;

const CRATE_PEEK_LEFT_X = 224;
const CRATE_PEEK_RIGHT_X = 272;
const CRATE_PEEK_TOP_Y = 96;
const CRATE_PEEK_BOTTOM_Y = 212;
const CRATE_SLEEVE_LEFT_X = 108;
const CRATE_SLEEVE_TOP_Y = 90;
const CRATE_SLEEVE_BOTTOM_Y = 216;
const CRATE_PULL_UP_PREVIEW_DISTANCE_PX = 72;
const CRATE_PULL_UP_DECAY_RATE = 7.5;
const CRATE_SWIPE_UP_HINT_HALF_WIDTH_PX = 58;
const CRATE_SWIPE_UP_HINT_TOP_Y = 302;
const CRATE_SWIPE_UP_HINT_BOTTOM_Y = 346;

const EMPTY_COPY_BY_SECTION = Object.freeze({
  favorites: Object.freeze({
    title: "No Favorites Yet",
    message: "HEART A TRACK WHILE LISTENING",
  }),
  playlists: Object.freeze({
    title: "No Playlists Yet",
    message: "ADD SONGS TO BUILD A MIX",
  }),
  artists: Object.freeze({
    title: "No Artists Found",
    message: "SYNC MUSIC VIA USB STORAGE",
  }),
  albums: Object.freeze({
    title: "No Albums Found",
    message: "SYNC MUSIC VIA USB STORAGE",
  }),
  genres: Object.freeze({
    title: "No Genres Found",
    message: "RESCAN LIBRARY IN SETTINGS",
  }),
  "all-songs": Object.freeze({
    title: "Library Is Empty",
    message: "COPY TRACKS TO MICROSD CARD",
  }),
});

/**
 * Compare two text strings ascending with locale base sensitivity and numeric order.
 *
 * @param {string} firstText First label.
 * @param {string} secondText Second label.
 * @returns {number} Comparison sign (-1, 0, or 1).
 */
export function compareAlphabetically(firstText, secondText) {
  return String(firstText ?? "").localeCompare(String(secondText ?? ""), undefined, {
    sensitivity: "base",
    numeric: true,
  });
}

/**
 * Return an alphabetically sorted shallow copy of `entries` without mutating the source array.
 *
 * @param {Array<object>} entries Source entries.
 * @param {(entry: object) => string} [labelSelector] Selector extracting sortable label text.
 * @returns {Array<object>} Sorted shallow copy.
 */
export function sortAlphabeticallyBy(
  entries,
  labelSelector = (entry) => entry?.title ?? entry?.name ?? entry,
) {
  if (!Array.isArray(entries)) {
    return [];
  }
  return entries.slice().sort((firstEntry, secondEntry) =>
    compareAlphabetically(labelSelector(firstEntry), labelSelector(secondEntry)),
  );
}

/**
 * Compute 2-up Hub crystal tile geometry and visibility alpha for `tileIndex`.
 *
 * @param {number} tileIndex Section tile index in [0, 5].
 * @param {number} scrollPairPosition Continuous vertical pair row offset.
 * @returns {{ centerX: number, centerY: number, width: number, height: number, isPeeking: boolean, alpha: number }} Tile rectangle descriptor.
 */
export function computeLibraryHubTileRect(tileIndex, scrollPairPosition = 0) {
  const pairRow = Math.floor(tileIndex / 2);
  const columnIndex = tileIndex % 2;
  const deltaRow = pairRow - scrollPairPosition;
  const centerY = HUB_ROW_TOP_CENTER_Y + deltaRow * HUB_ROW_PITCH_PX;
  const isPeeking = deltaRow > HUB_PEEK_DELTA_THRESHOLD;
  const clampedForwardDelta = clamp(deltaRow, 0, 1);
  const width = lerp(HUB_FULL_TILE_WIDTH_PX, HUB_PEEK_TILE_WIDTH_PX, clampedForwardDelta);
  const height = lerp(HUB_FULL_TILE_HEIGHT_PX, HUB_PEEK_TILE_HEIGHT_PX, clampedForwardDelta);
  const halfSpread = lerp(HUB_FULL_HALF_SPREAD_PX, HUB_PEEK_HALF_SPREAD_PX, clampedForwardDelta);
  const centerX = DISPLAY_CENTER_PX + (columnIndex === 0 ? -halfSpread : halfSpread);

  let alpha = 1;
  if (deltaRow < 0) {
    alpha = 1 - easeInOutCubic(clamp(-deltaRow / HUB_EXIT_FADE_ROWS, 0, 1));
  } else if (deltaRow > 0) {
    const previewRamp = clamp(deltaRow, 0, 1);
    const beyondPreview = clamp((deltaRow - 1.0) / HUB_BEYOND_FADE_ROWS, 0, 1);
    alpha =
      lerp(1.0, HUB_PEEK_ALPHA, easeInOutCubic(previewRamp)) *
      (1 - easeInOutCubic(beyondPreview));
  }

  return {
    centerX,
    centerY,
    width,
    height,
    isPeeking,
    alpha: clamp(alpha, 0, 1),
  };
}

/**
 * Compute curved circular-chord row geometry and fade opacity for list views.
 *
 * @param {number} rowIndex Row index in the list.
 * @param {number} scrollPosition Continuous vertical scroll offset.
 * @returns {{
 *   rowY: number,
 *   centerX: number,
 *   centerY: number,
 *   rowHalfWidth: number,
 *   width: number,
 *   rowHeight: number,
 *   height: number,
 *   previewWeight: number,
 *   alpha: number
 * }} Row layout descriptor.
 */
export function computeLibraryCurvedRowLayout(rowIndex, scrollPosition = 0) {
  const slotOffset = rowIndex - scrollPosition;
  const rowY =
    LIBRARY_CONSTANTS.LIST_TOP_SLOT_Y + slotOffset * LIBRARY_CONSTANTS.LIST_ROW_PITCH_PX;
  const offsetFromCenterY = rowY - DISPLAY_CENTER_PX;
  const chordHalfWidth = Math.sqrt(
    Math.max(
      0,
      LIST_CURVE_CHORD_RADIUS_PX * LIST_CURVE_CHORD_RADIUS_PX -
        offsetFromCenterY * offsetFromCenterY,
    ),
  );
  let rowHalfWidth = clamp(
    chordHalfWidth - LIST_CHORD_INSET_PX,
    LIST_MIN_HALF_WIDTH_PX,
    LIST_MAX_HALF_WIDTH_PX,
  );
  let rowHeight = LIST_FULL_ROW_HEIGHT_PX;
  let previewWeight = 0;
  let alpha = 1;

  if (slotOffset < 0) {
    const exitRatio = clamp(-slotOffset / LIST_EXIT_FADE_SLOTS, 0, 1);
    alpha = 1 - easeInOutCubic(exitRatio);
    rowHeight = lerp(LIST_FULL_ROW_HEIGHT_PX, LIST_COMPACT_ROW_HEIGHT_PX, exitRatio);
  } else if (slotOffset > 1) {
    const previewRamp = clamp(slotOffset - 1, 0, 1);
    const beyondPreview = clamp((slotOffset - 2.0) / LIST_BEYOND_FADE_SLOTS, 0, 1);
    previewWeight = previewRamp;
    rowHeight = lerp(
      LIST_FULL_ROW_HEIGHT_PX,
      LIST_COMPACT_ROW_HEIGHT_PX,
      easeInOutCubic(previewRamp),
    );
    rowHalfWidth = Math.max(
      LIST_MIN_HALF_WIDTH_PX,
      rowHalfWidth - LIST_PREVIEW_WIDTH_REDUCTION_PX * previewRamp,
    );
    alpha =
      lerp(1.0, LIST_PREVIEW_ALPHA, easeInOutCubic(previewRamp)) *
      (1 - easeInOutCubic(beyondPreview));
  }

  return {
    rowY,
    centerX: DISPLAY_CENTER_PX,
    centerY: rowY,
    rowHalfWidth,
    width: rowHalfWidth * 2,
    rowHeight,
    height: rowHeight,
    previewWeight,
    alpha: clamp(alpha, 0, 1),
  };
}

/**
 * Map a bezel angle in radians (relative to 12 o'clock) into a discrete 17-slice index.
 *
 * @param {number} angleRadians Polar angle around display center.
 * @returns {number} Slice index in [0, RIM_SLICE_COUNT - 1].
 */
export function resolveLibraryRimSliceIndex(angleRadians) {
  if (!Number.isFinite(angleRadians)) {
    return 0;
  }
  const relativeAngle =
    (((angleRadians - TOP_ANGLE_RADIANS) % FULL_CIRCLE_RADIANS) + FULL_CIRCLE_RADIANS) %
    FULL_CIRCLE_RADIANS;
  const sliceCount = LIBRARY_CONSTANTS.RIM_SLICE_COUNT;
  // Tiny positive bias stabilizes exact boundary multiples across +/- 2*PI wraps.
  return Math.floor((relativeAngle / FULL_CIRCLE_RADIANS) * sliceCount + SLICE_BOUNDARY_EPSILON) % sliceCount;
}

/**
 * Compute shortest modular distance between two slice indices on a circular ring.
 *
 * @param {number} firstIndex First slice index.
 * @param {number} secondIndex Second slice index.
 * @param {number} [totalCount=RIM_SLICE_COUNT] Ring slice count.
 * @returns {number} Non-negative wrap-aware slice distance.
 */
export function shortestSliceDistance(
  firstIndex,
  secondIndex,
  totalCount = LIBRARY_CONSTANTS.RIM_SLICE_COUNT,
) {
  if (!Number.isFinite(totalCount) || totalCount <= 0) {
    return 0;
  }
  let distance = Math.abs(firstIndex - secondIndex) % totalCount;
  if (distance > totalCount / 2) {
    distance = totalCount - distance;
  }
  return distance;
}

/**
 * Group sorted entries by initial character (A-Z or `#`) for Crystal-Slab Letter Seek.
 *
 * @param {Array<object>} entries Sorted collection entries.
 * @param {(entry: object) => string} [labelSelector] Label accessor for each entry.
 * @returns {Array<{ letter: string, itemIndex: number, label: string }>} Distinct starting letter anchors.
 */
export function buildAlphabeticalLetterIndex(
  entries,
  labelSelector = (entry) => entry?.title ?? entry?.name ?? entry,
) {
  if (!Array.isArray(entries) || entries.length === 0) {
    return [];
  }
  const letterEntries = [];
  const seenLetters = new Set();
  for (let entryIndex = 0; entryIndex < entries.length; entryIndex += 1) {
    const labelText = String(labelSelector(entries[entryIndex]) ?? "").trim();
    const firstCharacter = labelText.charAt(0).toUpperCase();
    const normalizedLetter = /[A-Z]/.test(firstCharacter) ? firstCharacter : "#";
    if (!seenLetters.has(normalizedLetter)) {
      seenLetters.add(normalizedLetter);
      letterEntries.push({
        letter: normalizedLetter,
        itemIndex: entryIndex,
        label: labelText,
      });
    }
  }
  return letterEntries;
}

/**
 * Compute windowed crate indicator bar widths and focus weights around `scrollPosition`.
 *
 * @param {number} scrollPosition Continuous horizontal crate scroll offset.
 * @param {number} totalAlbums Total number of albums in the crate.
 * @param {number} [maxBars=7] Maximum indicator bars displayed simultaneously.
 * @returns {Array<{ albumIndex: number, width: number, focus: number }>} Windowed bar descriptors.
 */
export function computeWindowedCrateBars(
  scrollPosition,
  totalAlbums,
  maxBars = DEFAULT_CRATE_BAR_LIMIT,
) {
  if (
    !Number.isFinite(totalAlbums) ||
    totalAlbums <= 0 ||
    !Number.isFinite(maxBars) ||
    maxBars <= 0
  ) {
    return [];
  }
  const safeTotal = Math.floor(totalAlbums);
  const windowCount = Math.min(safeTotal, Math.floor(maxBars));
  if (windowCount <= 0) {
    return [];
  }
  const safeScroll = Number.isFinite(scrollPosition) ? scrollPosition : 0;
  const centeredAnchor = Math.round(safeScroll);
  const maxStartIndex = safeTotal - windowCount;
  const startIndex = clamp(centeredAnchor - Math.floor(windowCount / 2), 0, maxStartIndex);

  const bars = [];
  for (let offset = 0; offset < windowCount; offset += 1) {
    const albumIndex = startIndex + offset;
    const barFocus = clamp(1 - Math.abs(safeScroll - albumIndex), 0, 1);
    const barWidth = lerp(
      CRATE_BAR_MIN_WIDTH_PX,
      CRATE_BAR_MAX_WIDTH_PX,
      easeInOutCubic(barFocus),
    );
    bars.push({
      albumIndex,
      width: barWidth,
      focus: barFocus,
    });
  }
  return bars;
}

function normalizeAngleDelta(nextAngle, previousAngle) {
  return Math.atan2(Math.sin(nextAngle - previousAngle), Math.cos(nextAngle - previousAngle));
}

function clonePlaylists(playlists) {
  return playlists.map((playlistEntry) => ({
    ...playlistEntry,
    trackIds: Array.isArray(playlistEntry.trackIds) ? playlistEntry.trackIds.slice() : [],
  }));
}

function normalizeCatalogPayload(incomingCatalog) {
  const sourceCatalog =
    incomingCatalog && typeof incomingCatalog === "object" ? incomingCatalog : {};
  const rawAlbums = Array.isArray(sourceCatalog.albums) ? sourceCatalog.albums : [];
  const rawArtists = Array.isArray(sourceCatalog.artists) ? sourceCatalog.artists : [];
  const rawGenres = Array.isArray(sourceCatalog.genres) ? sourceCatalog.genres : [];
  const rawAllTracks = Array.isArray(sourceCatalog.allTracks) ? sourceCatalog.allTracks : [];
  const rawPlaylists = Array.isArray(sourceCatalog.playlists) ? sourceCatalog.playlists : [];

  const normalizedAlbums = sortAlphabeticallyBy(
    rawAlbums.map((albumEntry) => {
      const albumTracks = Array.isArray(albumEntry?.tracks) ? albumEntry.tracks : [];
      return {
        ...albumEntry,
        title: String(albumEntry?.title ?? ""),
        artist: String(albumEntry?.artist ?? ""),
        genre: String(albumEntry?.genre ?? ""),
        // Preserve each album's internal track sequencing while sorting albums A-Z.
        tracks: albumTracks.map((trackEntry, trackPosition) => ({
          ...trackEntry,
          title: String(trackEntry?.title ?? ""),
          artist: String(trackEntry?.artist || albumEntry?.artist || ""),
          albumId: trackEntry?.albumId || albumEntry?.id || "",
          albumTitle: trackEntry?.albumTitle || albumEntry?.title || "",
          genre: String(trackEntry?.genre || albumEntry?.genre || ""),
          isFavorite: Boolean(trackEntry?.isFavorite),
          trackNumber: trackEntry?.trackNumber || String(trackPosition + 1).padStart(2, "0"),
          audioUrl: trackEntry?.audioUrl || null,
        })),
      };
    }),
    (albumEntry) => albumEntry.title,
  );

  const albumByIdMap = new Map();
  const trackByIdMap = new Map();
  for (const albumEntry of normalizedAlbums) {
    if (albumEntry.id) {
      albumByIdMap.set(albumEntry.id, albumEntry);
    }
    for (const trackEntry of albumEntry.tracks) {
      if (trackEntry.id) {
        trackByIdMap.set(trackEntry.id, trackEntry);
      }
    }
  }

  const normalizedArtists = sortAlphabeticallyBy(
    rawArtists.map((artistEntry) => ({
      ...artistEntry,
      name: String(artistEntry?.name ?? ""),
      albumIds: Array.isArray(artistEntry?.albumIds) ? artistEntry.albumIds.slice() : [],
      countLabel: String(
        artistEntry?.countLabel ??
          (Array.isArray(artistEntry?.albumIds) ? artistEntry.albumIds.length : 0),
      ),
    })),
    (artistEntry) => artistEntry.name,
  );

  const normalizedGenres = sortAlphabeticallyBy(
    rawGenres.map((genreEntry) => ({
      ...genreEntry,
      name: String(genreEntry?.name ?? ""),
      albumIds: Array.isArray(genreEntry?.albumIds) ? genreEntry.albumIds.slice() : [],
      countLabel: String(genreEntry?.countLabel ?? "0"),
    })),
    (genreEntry) => genreEntry.name,
  );

  const allTrackSource =
    rawAllTracks.length > 0 ? rawAllTracks : Array.from(trackByIdMap.values());
  const normalizedAllTracks = sortAlphabeticallyBy(
    allTrackSource.map((trackEntry) => {
      const existingTrack = trackByIdMap.get(trackEntry?.id) || {};
      const mergedTrack = {
        ...existingTrack,
        ...trackEntry,
        title: String(trackEntry?.title ?? existingTrack.title ?? ""),
        artist: String(trackEntry?.artist ?? existingTrack.artist ?? ""),
        albumId: trackEntry?.albumId || existingTrack.albumId || "",
        albumTitle: trackEntry?.albumTitle || existingTrack.albumTitle || "",
        genre: String(trackEntry?.genre ?? existingTrack.genre ?? ""),
        isFavorite: Boolean(trackEntry?.isFavorite),
        audioUrl: trackEntry?.audioUrl || existingTrack.audioUrl || null,
      };
      if (mergedTrack.id) {
        trackByIdMap.set(mergedTrack.id, mergedTrack);
      }
      return mergedTrack;
    }),
    (trackEntry) => trackEntry.title,
  );

  const normalizedPlaylists = sortAlphabeticallyBy(
    rawPlaylists.map((playlistEntry) => ({
      ...playlistEntry,
      name: String(playlistEntry?.name ?? ""),
      trackIds: Array.isArray(playlistEntry?.trackIds) ? playlistEntry.trackIds.slice() : [],
    })),
    (playlistEntry) => playlistEntry.name,
  );

  return {
    catalog: {
      ...sourceCatalog,
      albums: normalizedAlbums,
      artists: normalizedArtists,
      genres: normalizedGenres,
      allTracks: normalizedAllTracks,
      playlists: normalizedPlaylists,
    },
    albumByIdMap,
    trackByIdMap,
    playlists: normalizedPlaylists,
  };
}

/**
 * Create an isolated Library controller managing catalog hydration, hierarchical
 * drill-down views, Vinyl Crate & curved-list physics, bezel Crystal-Slab Letter Seek,
 * and Playlist Action Sheet / Hold-to-Delete friction.
 *
 * @param {object} [callbacks] Host integration callbacks.
 * @param {(launchPayload: object) => void} [callbacks.onLaunchTrack] Invoked when a track/vinyl launches.
 * @param {() => void} [callbacks.onExitToHome] Invoked when stepping back at the Hub root.
 * @param {(playlists: Array<object>) => void} [callbacks.onPersistPlaylists] Invoked after playlist deletion.
 * @returns {object} Library controller API.
 */
export function createLibraryController({
  onLaunchTrack,
  onExitToHome,
  onPersistPlaylists,
} = {}) {
  let catalog = {
    albums: [],
    artists: [],
    genres: [],
    allTracks: [],
    playlists: [],
  };
  let albumByIdMap = new Map();
  let trackByIdMap = new Map();
  let playlists = [];
  let counts = computeCounts();

  let viewStack = [createHubView()];
  let activeTrackId = null;

  let idleClockSeconds = 0;
  let vinylRotationRadians = 0;
  let vinylNudgeRadians = 0;
  let vinylNudgeTarget = 0;
  let vinylLaunchProgress = 0;
  let vinylLaunchTarget = 0;
  let vinylLaunchSource = "albums";
  let vinylLaunchAlbumId = null;

  let transitionProgress = 1;
  let transitionDirection = 1;
  let backSwipeProgress = 0;
  let cratePullUpProgress = 0;

  let pressedRowIndex = null;
  let pressHighlightLevel = 0;
  let pressFlashRowIndex = null;
  let pressFlashLevel = 0;

  let arcStartAngle = 0;
  let arcLastAngle = 0;
  let arcCurrentAngle = 0;
  let arcSignedSweepRadians = 0;
  let arcAbsoluteSweepRadians = 0;
  let arcFeedbackAlpha = 0;

  let letterSearchActive = false;
  let letterSearchIndex = 0;
  let letterSearchAccumulator = 0;
  let letterSearchHudAlpha = 0;
  const rimSlabPullLevels = new Array(LIBRARY_CONSTANTS.RIM_SLICE_COUNT).fill(0);

  let playlistSheetOpen = false;
  let playlistSheetIndex = 0;
  let playlistSheetProgress = 0;
  let deleteHoldActive = false;
  let deleteHoldProgress = 0;
  let deleteCompletedFlash = 0;
  let longPressSeconds = 0;
  let longPressTriggered = false;

  let pointerActive = false;
  let pointerMode = "none";
  let pointerStartX = 0;
  let pointerStartY = 0;
  let pointerLastX = 0;
  let pointerLastY = 0;
  let pointerVelocity = 0;
  let pointerStartScroll = 0;
  let pointerMovedDistance = 0;

  function computeCounts() {
    const favoriteCount = catalog.allTracks.filter((trackEntry) => trackEntry.isFavorite).length;
    return {
      tracks: catalog.allTracks.length,
      albums: catalog.albums.length,
      artists: catalog.artists.length,
      genres: catalog.genres.length,
      favorites: favoriteCount,
      playlists: playlists.length,
    };
  }

  function formatHubSubtitle() {
    return `${formatIntegerCount(counts.tracks)} TRACKS \u00b7 ${formatIntegerCount(counts.albums)} ALBUMS`;
  }

  function createHubView() {
    return {
      kind: "hub",
      title: "Library",
      subtitle: formatHubSubtitle(),
      scrollPosition: 0,
      targetIndex: 0,
    };
  }

  function syncCounts() {
    counts = computeCounts();
    catalog = {
      ...catalog,
      playlists: clonePlaylists(playlists),
      counts: { ...counts },
    };
    if (viewStack.length > 0 && viewStack[0].kind === "hub") {
      viewStack[0].subtitle = formatHubSubtitle();
    }
  }

  function getCurrentView() {
    return viewStack[viewStack.length - 1];
  }

  function clearPointerState() {
    pointerActive = false;
    pointerMode = "none";
    pointerStartX = 0;
    pointerStartY = 0;
    pointerLastX = 0;
    pointerLastY = 0;
    pointerVelocity = 0;
    pointerStartScroll = 0;
    pointerMovedDistance = 0;
    longPressSeconds = 0;
    longPressTriggered = false;
    deleteHoldActive = false;
  }

  function resetTransientUiState() {
    playlistSheetOpen = false;
    playlistSheetProgress = 0;
    deleteHoldActive = false;
    deleteHoldProgress = 0;
    deleteCompletedFlash = 0;
    letterSearchActive = false;
    letterSearchIndex = 0;
    letterSearchAccumulator = 0;
    letterSearchHudAlpha = 0;
    arcFeedbackAlpha = 0;
    arcSignedSweepRadians = 0;
    arcAbsoluteSweepRadians = 0;
    rimSlabPullLevels.fill(0);
    pressedRowIndex = null;
    pressHighlightLevel = 0;
    pressFlashRowIndex = null;
    pressFlashLevel = 0;
    vinylLaunchTarget = 0;
    vinylLaunchProgress = 0;
    vinylLaunchAlbumId = null;
    vinylLaunchSource = "albums";
    backSwipeProgress = 0;
    cratePullUpProgress = 0;
    clearPointerState();
  }

  function resolveCrateAlbums(viewEntry) {
    if (Array.isArray(viewEntry.albumIds) && viewEntry.albumIds.length > 0) {
      const matchedAlbums = viewEntry.albumIds
        .map((albumId) => albumByIdMap.get(albumId))
        .filter(Boolean);
      return sortAlphabeticallyBy(matchedAlbums, (albumEntry) => albumEntry.title);
    }
    return catalog.albums;
  }

  function resolveViewRows(viewEntry) {
    if (viewEntry.kind === "hub") {
      return LIBRARY_HUB_SECTIONS.map((sectionSpec) => {
        let countNumber = 0;
        if (sectionSpec.id === "favorites") {
          countNumber = counts.favorites;
        } else if (sectionSpec.id === "playlists") {
          countNumber = playlists.length;
        } else if (sectionSpec.id === "artists") {
          countNumber = catalog.artists.length;
        } else if (sectionSpec.id === "albums") {
          countNumber = catalog.albums.length;
        } else if (sectionSpec.id === "genres") {
          countNumber = catalog.genres.length;
        } else if (sectionSpec.id === "all-songs") {
          countNumber = catalog.allTracks.length;
        }
        const countLabel = formatIntegerCount(countNumber);
        return {
          id: sectionSpec.id,
          rowType: "hub",
          title: sectionSpec.title,
          countLabel,
          countText: countLabel,
          sectionSpec,
        };
      });
    }

    if (viewEntry.kind === "artists") {
      return catalog.artists.map((artistEntry) => ({
        id: artistEntry.id,
        rowType: "artist",
        title: artistEntry.name,
        countLabel: artistEntry.countLabel,
        countText: artistEntry.countLabel,
        albumIds: artistEntry.albumIds.slice(),
      }));
    }

    if (viewEntry.kind === "genres") {
      return catalog.genres.map((genreEntry) => ({
        id: genreEntry.id,
        rowType: "genre",
        title: genreEntry.name,
        countLabel: genreEntry.countLabel,
        countText: genreEntry.countLabel,
        albumIds: genreEntry.albumIds.slice(),
      }));
    }

    if (viewEntry.kind === "playlists") {
      return sortAlphabeticallyBy(playlists, (playlistEntry) => playlistEntry.name).map(
        (playlistEntry) => {
          const countLabel = formatIntegerCount(playlistEntry.trackIds.length);
          return {
            id: playlistEntry.id,
            rowType: "playlist",
            title: playlistEntry.name,
            countLabel,
            countText: countLabel,
            trackIds: playlistEntry.trackIds.slice(),
          };
        },
      );
    }

    if (viewEntry.kind === "tracks") {
      let trackEntries = [];
      if (viewEntry.sourceType === "album") {
        const targetAlbum = albumByIdMap.get(viewEntry.albumId);
        trackEntries = targetAlbum ? targetAlbum.tracks : [];
      } else if (viewEntry.sourceType === "favorites") {
        trackEntries = catalog.allTracks.filter((trackEntry) => trackEntry.isFavorite);
      } else if (viewEntry.sourceType === "playlist") {
        const targetPlaylist = playlists.find(
          (playlistEntry) => playlistEntry.id === viewEntry.playlistId,
        );
        const playlistTrackIds = Array.isArray(targetPlaylist?.trackIds)
          ? targetPlaylist.trackIds
          : [];
        trackEntries = sortAlphabeticallyBy(
          playlistTrackIds.map((trackId) => trackByIdMap.get(trackId)).filter(Boolean),
          (trackEntry) => trackEntry.title,
        );
      } else if (viewEntry.sourceType === "genre") {
        const albumIdSet = new Set(Array.isArray(viewEntry.albumIds) ? viewEntry.albumIds : []);
        trackEntries = catalog.allTracks.filter((trackEntry) =>
          albumIdSet.has(trackEntry.albumId),
        );
      } else {
        trackEntries = catalog.allTracks;
      }

      return trackEntries.map((trackEntry) => ({
        id: trackEntry.id,
        rowType: "track",
        title: trackEntry.title,
        artist: trackEntry.artist,
        albumId: trackEntry.albumId,
        albumTitle: trackEntry.albumTitle,
        duration: trackEntry.duration,
        durationSeconds: trackEntry.durationSeconds,
        isFavorite: Boolean(trackEntry.isFavorite),
        trackNumber: trackEntry.trackNumber,
        audioUrl: trackEntry.audioUrl || null,
        showTrackNumbers: viewEntry.sourceType === "album",
      }));
    }

    return [];
  }

  function resolveViewLetterEntries(viewEntry) {
    const isAlphabeticalView =
      viewEntry.kind === "albums" ||
      viewEntry.kind === "artists" ||
      viewEntry.kind === "genres" ||
      viewEntry.kind === "playlists" ||
      (viewEntry.kind === "tracks" && viewEntry.sourceType !== "album");

    if (!isAlphabeticalView) {
      return [];
    }

    const entries =
      viewEntry.kind === "albums" ? resolveCrateAlbums(viewEntry) : resolveViewRows(viewEntry);
    if (entries.length < MIN_ALPHABETICAL_INDEX_COUNT) {
      return [];
    }

    const letters = buildAlphabeticalLetterIndex(
      entries,
      (entry) => entry?.title ?? entry?.name ?? entry,
    );
    return letters.length >= MIN_LETTER_GROUPS_FOR_SEEK ? letters : [];
  }

  function getViewMaxScroll(viewEntry) {
    if (viewEntry.kind === "hub") {
      return Math.max(0, Math.ceil(LIBRARY_HUB_SECTIONS.length / 2) - 1);
    }
    if (viewEntry.kind === "albums") {
      return Math.max(0, resolveCrateAlbums(viewEntry).length - 1);
    }
    if (viewEntry.kind === "empty") {
      return 0;
    }
    return Math.max(0, resolveViewRows(viewEntry).length - LIBRARY_CONSTANTS.LIST_FULL_SLOTS);
  }

  function hitTestListRow(pointX, pointY, viewEntry, rows) {
    if (viewEntry.kind === "hub") {
      for (let tileIndex = 0; tileIndex < rows.length; tileIndex += 1) {
        const tileRect = computeLibraryHubTileRect(tileIndex, viewEntry.scrollPosition);
        if (tileRect.alpha < HUB_HIT_MIN_ALPHA) {
          continue;
        }
        const insideX = Math.abs(pointX - tileRect.centerX) <= tileRect.width / 2;
        const insideY = Math.abs(pointY - tileRect.centerY) <= tileRect.height / 2;
        if (insideX && insideY) {
          return tileIndex;
        }
      }
      return null;
    }

    const startRow = Math.max(0, Math.floor(viewEntry.scrollPosition) - 2);
    const endRow = Math.min(rows.length - 1, Math.ceil(viewEntry.scrollPosition) + 4);
    for (let rowIndex = startRow; rowIndex <= endRow; rowIndex += 1) {
      const rowLayout = computeLibraryCurvedRowLayout(rowIndex, viewEntry.scrollPosition);
      if (rowLayout.alpha < LIST_HIT_MIN_ALPHA) {
        continue;
      }
      const insideX = Math.abs(pointX - DISPLAY_CENTER_PX) <= rowLayout.rowHalfWidth;
      const insideY = Math.abs(pointY - rowLayout.rowY) <= rowLayout.rowHeight / 2 + LIST_HIT_PADDING_Y_PX;
      if (insideX && insideY) {
        return rowIndex;
      }
    }
    return null;
  }

  function pushView(viewSpec) {
    playlistSheetOpen = false;
    deleteHoldActive = false;
    deleteHoldProgress = 0;
    letterSearchActive = false;
    letterSearchHudAlpha = 0;
    arcFeedbackAlpha = 0;
    pressedRowIndex = null;
    pressHighlightLevel = 0;
    pressFlashRowIndex = null;
    pressFlashLevel = 0;
    vinylLaunchTarget = 0;
    vinylLaunchProgress = 0;
    viewStack.push(viewSpec);
    transitionDirection = 1;
    transitionProgress = 0;
  }

  function openPlaylistActionSheet(playlistIndex) {
    if (playlists.length === 0) {
      return;
    }
    playlistSheetIndex = clamp(playlistIndex, 0, playlists.length - 1);
    playlistSheetOpen = true;
    pressedRowIndex = null;
    pressHighlightLevel = 0;
    deleteHoldActive = false;
    deleteHoldProgress = 0;
  }

  function launchVinyl(albumId, sourceKind, trackId, sourceSpec) {
    playlistSheetOpen = false;
    pressedRowIndex = null;
    pressHighlightLevel = 0;
    vinylLaunchAlbumId = albumId;
    vinylLaunchSource = sourceKind;
    vinylLaunchTarget = 1;
    vinylNudgeTarget += VINYL_LAUNCH_NUDGE_RADIANS;
    if (trackId) {
      activeTrackId = trackId;
      if (typeof onLaunchTrack === "function") {
        onLaunchTrack({
          trackId,
          ...(sourceSpec || { queueSource: "album", albumId }),
        });
      }
    }
  }

  function applyArcDelta(deltaAngle, currentView) {
    const maxScroll = getViewMaxScroll(currentView);
    const letterEntries = resolveViewLetterEntries(currentView);

    arcSignedSweepRadians += deltaAngle;
    arcAbsoluteSweepRadians += Math.abs(deltaAngle);
    arcFeedbackAlpha = 1;

    if (
      !letterSearchActive &&
      letterEntries.length > 0 &&
      arcAbsoluteSweepRadians >= LIBRARY_CONSTANTS.LETTER_SEARCH_THRESHOLD_RADIANS
    ) {
      letterSearchActive = true;
      letterSearchAccumulator = 0;
      const currentTopIndex = Math.round(currentView.scrollPosition);
      let matchedLetterIndex = 0;
      for (let letterIdx = 0; letterIdx < letterEntries.length; letterIdx += 1) {
        if (letterEntries[letterIdx].itemIndex <= currentTopIndex) {
          matchedLetterIndex = letterIdx;
        }
      }
      letterSearchIndex = matchedLetterIndex;
      const alignedItemIndex = clamp(letterEntries[letterSearchIndex].itemIndex, 0, maxScroll);
      currentView.scrollPosition = alignedItemIndex;
      currentView.targetIndex = alignedItemIndex;
      return;
    }

    if (letterSearchActive && letterEntries.length > 0) {
      letterSearchAccumulator += deltaAngle;
      while (letterSearchAccumulator >= LIBRARY_CONSTANTS.LETTER_STEP_RADIANS) {
        letterSearchAccumulator -= LIBRARY_CONSTANTS.LETTER_STEP_RADIANS;
        letterSearchIndex = clamp(letterSearchIndex + 1, 0, letterEntries.length - 1);
      }
      while (letterSearchAccumulator <= -LIBRARY_CONSTANTS.LETTER_STEP_RADIANS) {
        letterSearchAccumulator += LIBRARY_CONSTANTS.LETTER_STEP_RADIANS;
        letterSearchIndex = clamp(letterSearchIndex - 1, 0, letterEntries.length - 1);
      }
      const jumpedItemIndex = clamp(letterEntries[letterSearchIndex].itemIndex, 0, maxScroll);
      currentView.scrollPosition = jumpedItemIndex;
      currentView.targetIndex = jumpedItemIndex;
      return;
    }

    const nextScroll = clamp(
      currentView.scrollPosition + deltaAngle / LIBRARY_CONSTANTS.ARC_RADIANS_PER_STEP,
      0,
      maxScroll,
    );
    currentView.scrollPosition = nextScroll;
    currentView.targetIndex = clamp(Math.round(nextScroll), 0, maxScroll);
  }

  /**
   * Hydrate and normalize catalog collections, sorting top-level shelves
   * alphabetically while preserving each album's own track sequencing.
   *
   * @param {object} nextCatalog Backend or fixture catalog payload.
   */
  function hydrateCatalog(nextCatalog) {
    const normalized = normalizeCatalogPayload(nextCatalog);
    catalog = normalized.catalog;
    albumByIdMap = normalized.albumByIdMap;
    trackByIdMap = normalized.trackByIdMap;
    playlists = normalized.playlists;
    syncCounts();
  }

  /**
   * Reset Library navigation to the Hub root with entry animation progress at 0.
   */
  function enterFromHome() {
    resetTransientUiState();
    viewStack = [createHubView()];
    transitionDirection = 1;
    transitionProgress = 0;
  }

  /**
   * Update the active playing track identifier for row equalizer indicators.
   *
   * @param {string | null} trackId Active track identifier.
   */
  function setActiveTrackId(trackId) {
    activeTrackId = typeof trackId === "string" && trackId.length > 0 ? trackId : null;
  }

  /**
   * Navigate back one level: closes launched vinyl, then Playlist Action Sheet,
   * then pops the view stack, or invokes `onExitToHome` at the Hub root.
   */
  function stepBack() {
    pressedRowIndex = null;
    pressHighlightLevel = 0;
    if (vinylLaunchTarget === 1) {
      vinylLaunchTarget = 0;
      vinylNudgeTarget += VINYL_CLOSE_NUDGE_RADIANS;
      return;
    }
    if (playlistSheetOpen) {
      playlistSheetOpen = false;
      deleteHoldActive = false;
      deleteHoldProgress = 0;
      return;
    }
    if (viewStack.length > 1) {
      viewStack.pop();
      const restoredView = getCurrentView();
      if (restoredView.kind === "hub") {
        restoredView.scrollPosition = 0;
        restoredView.targetIndex = 0;
      }
      transitionDirection = -1;
      transitionProgress = 0;
      return;
    }
    if (typeof onExitToHome === "function") {
      onExitToHome();
    }
  }

  /**
   * Step focus target by one rotary detent along the active list or vinyl crate.
   *
   * @param {number} direction Signed detent direction (+1 or -1).
   */
  function stepDetent(direction) {
    if (
      vinylLaunchTarget === 1 ||
      playlistSheetOpen ||
      typeof direction !== "number" ||
      !Number.isFinite(direction)
    ) {
      return;
    }
    const stepDirection = Math.sign(direction);
    if (stepDirection === 0) {
      return;
    }
    const currentView = getCurrentView();
    if (currentView.kind === "empty") {
      return;
    }
    const maxIndex = getViewMaxScroll(currentView);
    currentView.targetIndex = clamp(currentView.targetIndex + stepDirection, 0, maxIndex);
  }

  /**
   * Activate the currently focused item or explicit row index in the top view.
   *
   * @param {number} [explicitRowIndex] Optional row index override from pointer hit testing.
   */
  function resolveFocusedCrateAlbum(currentView) {
    const crateAlbums = resolveCrateAlbums(currentView);
    if (crateAlbums.length === 0) {
      return null;
    }
    const focusedIndex = clamp(
      Math.round(currentView.scrollPosition),
      0,
      crateAlbums.length - 1,
    );
    return crateAlbums[focusedIndex] || null;
  }

  function launchFocusedCrateAlbum(currentView) {
    const selectedAlbum = resolveFocusedCrateAlbum(currentView);
    if (!selectedAlbum) {
      return;
    }
    const firstTrack = selectedAlbum.tracks[0];
    launchVinyl(
      selectedAlbum.id,
      "albums",
      firstTrack ? firstTrack.id : null,
      { queueSource: "album", albumId: selectedAlbum.id },
    );
  }

  /**
   * Open the tracklist view for the currently focused album in an Albums crate.
   */
  function openFocusedAlbumTracks() {
    if (vinylLaunchTarget === 1 || playlistSheetOpen) {
      return;
    }
    const currentView = getCurrentView();
    if (currentView.kind !== "albums") {
      return;
    }
    const selectedAlbum = resolveFocusedCrateAlbum(currentView);
    if (!selectedAlbum) {
      return;
    }
    cratePullUpProgress = 0;
    pushView({
      kind: "tracks",
      sourceType: "album",
      albumId: selectedAlbum.id,
      title: selectedAlbum.title,
      subtitle: `${selectedAlbum.artist.toUpperCase()} \u00b7 ${selectedAlbum.tracks.length} TRACKS`,
      scrollPosition: 0,
      targetIndex: 0,
    });
  }

  function activateFocused(explicitRowIndex) {
    if (vinylLaunchTarget === 1) {
      stepBack();
      return;
    }

    if (playlistSheetOpen) {
      const playlistEntry = playlists[playlistSheetIndex];
      if (playlistEntry && playlistEntry.trackIds.length > 0) {
        const firstTrack = trackByIdMap.get(playlistEntry.trackIds[0]);
        if (firstTrack) {
          launchVinyl(firstTrack.albumId, "tracks", firstTrack.id, {
            queueSource: "playlist",
            playlistId: playlistEntry.id,
            albumId: firstTrack.albumId,
          });
        }
      }
      return;
    }

    const currentView = getCurrentView();
    if (currentView.kind === "empty") {
      stepBack();
      return;
    }

    if (currentView.kind === "albums") {
      launchFocusedCrateAlbum(currentView);
      return;
    }

    const rows = resolveViewRows(currentView);
    if (rows.length === 0) {
      return;
    }
    const defaultIndex =
      currentView.kind === "hub"
        ? clamp(Math.round(currentView.scrollPosition) * 2, 0, rows.length - 1)
        : clamp(Math.round(currentView.scrollPosition), 0, rows.length - 1);
    const selectedIndex =
      typeof explicitRowIndex === "number"
        ? clamp(explicitRowIndex, 0, rows.length - 1)
        : defaultIndex;
    const selectedRow = rows[selectedIndex];

    if (currentView.kind === "hub") {
      const sectionId = selectedRow.id;
      const isSectionEmpty =
        (sectionId === "favorites" && counts.favorites === 0) ||
        (sectionId === "playlists" && playlists.length === 0) ||
        (sectionId === "artists" && catalog.artists.length === 0) ||
        (sectionId === "albums" && catalog.albums.length === 0) ||
        (sectionId === "genres" && catalog.genres.length === 0) ||
        (sectionId === "all-songs" && catalog.allTracks.length === 0);

      if (isSectionEmpty) {
        const emptyCopy = EMPTY_COPY_BY_SECTION[sectionId] || EMPTY_COPY_BY_SECTION["all-songs"];
        pushView({
          kind: "empty",
          title: selectedRow.title,
          subtitle: "0 ITEMS",
          emptyIcon: sectionId,
          emptyTitle: emptyCopy.title,
          emptyMessage: emptyCopy.message,
          scrollPosition: 0,
          targetIndex: 0,
        });
        return;
      }

      if (sectionId === "albums") {
        pushView({
          kind: "albums",
          title: "Albums",
          subtitle: "VINYL CRATE",
          scrollPosition: 0,
          targetIndex: 0,
        });
      } else if (sectionId === "artists") {
        pushView({
          kind: "artists",
          title: "Artists",
          subtitle: `${formatIntegerCount(catalog.artists.length)} ARTISTS`,
          scrollPosition: 0,
          targetIndex: 0,
        });
      } else if (sectionId === "genres") {
        pushView({
          kind: "genres",
          title: "Genres",
          subtitle: `${formatIntegerCount(catalog.genres.length)} GENRES`,
          scrollPosition: 0,
          targetIndex: 0,
        });
      } else if (sectionId === "playlists") {
        pushView({
          kind: "playlists",
          title: "Playlists",
          subtitle: `${formatIntegerCount(playlists.length)} MIXES`,
          scrollPosition: 0,
          targetIndex: 0,
        });
      } else if (sectionId === "favorites") {
        pushView({
          kind: "tracks",
          sourceType: "favorites",
          title: "Favorites",
          subtitle: `${formatIntegerCount(counts.favorites)} HEARTED TRACKS`,
          scrollPosition: 0,
          targetIndex: 0,
        });
      } else {
        pushView({
          kind: "tracks",
          sourceType: "all",
          title: "All songs",
          subtitle: `${formatIntegerCount(catalog.allTracks.length)} TRACKS \u00b7 A\u2013Z`,
          scrollPosition: 0,
          targetIndex: 0,
        });
      }
      return;
    }

    if (currentView.kind === "artists") {
      pushView({
        kind: "albums",
        title: selectedRow.title,
        subtitle: "ARTIST CRATE",
        albumIds: selectedRow.albumIds.slice(),
        scrollPosition: 0,
        targetIndex: 0,
      });
      return;
    }

    if (currentView.kind === "genres") {
      pushView({
        kind: "tracks",
        sourceType: "genre",
        albumIds: selectedRow.albumIds.slice(),
        title: selectedRow.title,
        subtitle: `${selectedRow.countLabel} TRACKS`,
        scrollPosition: 0,
        targetIndex: 0,
      });
      return;
    }

    if (currentView.kind === "playlists") {
      pushView({
        kind: "tracks",
        sourceType: "playlist",
        playlistId: selectedRow.id,
        title: selectedRow.title,
        subtitle: `${selectedRow.countLabel} TRACKS \u00b7 MIX`,
        scrollPosition: 0,
        targetIndex: 0,
      });
      return;
    }

    if (currentView.kind === "tracks") {
      let sourceSpec = { queueSource: "all-songs", albumId: selectedRow.albumId };
      if (currentView.sourceType === "album") {
        sourceSpec = {
          queueSource: "album",
          albumId: currentView.albumId || selectedRow.albumId,
        };
      } else if (currentView.sourceType === "playlist") {
        sourceSpec = {
          queueSource: "playlist",
          playlistId: currentView.playlistId,
          albumId: selectedRow.albumId,
        };
      } else if (currentView.sourceType === "favorites") {
        sourceSpec = {
          queueSource: "favorites",
          albumId: selectedRow.albumId,
        };
      } else if (currentView.sourceType === "genre") {
        sourceSpec = {
          queueSource: "genre",
          genreAlbumIds: Array.isArray(currentView.albumIds) ? currentView.albumIds.slice() : [],
          genreTitle: currentView.title,
          albumId: selectedRow.albumId,
        };
      }
      launchVinyl(selectedRow.albumId, "tracks", selectedRow.id, sourceSpec);
    }
  }

  /**
   * Advance Library timers, transitions, long-press detection, Hold-to-Delete guard,
   * Crystal-Slab rim waves, and critically damped scroll snapping by `deltaSeconds`.
   *
   * @param {number} deltaSeconds Elapsed frame duration in seconds.
   */
  function step(deltaSeconds) {
    if (typeof deltaSeconds !== "number" || !Number.isFinite(deltaSeconds) || deltaSeconds <= 0) {
      return;
    }

    const currentView = getCurrentView();
    idleClockSeconds += deltaSeconds;
    vinylRotationRadians += deltaSeconds * VINYL_SPIN_RADIANS_PER_SECOND;

    const nudgeFollow = 1 - Math.exp(-VINYL_NUDGE_SPRING_RATE * deltaSeconds);
    vinylNudgeRadians = lerp(vinylNudgeRadians, vinylNudgeTarget, nudgeFollow);

    if (transitionProgress < 1) {
      transitionProgress = clamp(
        transitionProgress + deltaSeconds / LIBRARY_CONSTANTS.TRANSITION_DURATION_SECONDS,
        0,
        1,
      );
    }

    if (vinylLaunchTarget === 1 && vinylLaunchProgress < 1) {
      vinylLaunchProgress = clamp(
        vinylLaunchProgress + deltaSeconds / LIBRARY_CONSTANTS.VINYL_LAUNCH_DURATION_SECONDS,
        0,
        1,
      );
    } else if (vinylLaunchTarget === 0 && vinylLaunchProgress > 0) {
      vinylLaunchProgress = clamp(
        vinylLaunchProgress - deltaSeconds / LIBRARY_CONSTANTS.VINYL_LAUNCH_DURATION_SECONDS,
        0,
        1,
      );
    }

    const sheetTarget = playlistSheetOpen ? 1 : 0;
    const sheetFollow = 1 - Math.exp(-SHEET_SPRING_RATE * deltaSeconds);
    playlistSheetProgress = lerp(playlistSheetProgress, sheetTarget, sheetFollow);
    if (Math.abs(playlistSheetProgress - sheetTarget) < SHEET_SETTLE_EPSILON) {
      playlistSheetProgress = sheetTarget;
    }

    const pressTarget = pressedRowIndex !== null ? 1 : 0;
    const pressRate = pressTarget > 0 ? PRESS_ATTACK_RATE : PRESS_RELEASE_RATE;
    pressHighlightLevel = lerp(
      pressHighlightLevel,
      pressTarget,
      1 - Math.exp(-pressRate * deltaSeconds),
    );
    if (pressTarget === 0 && pressHighlightLevel < PRESS_CLEAR_THRESHOLD) {
      pressHighlightLevel = 0;
    }
    if (pressFlashLevel > 0) {
      pressFlashLevel = Math.max(0, pressFlashLevel - deltaSeconds * PRESS_FLASH_DECAY_RATE);
      if (pressFlashLevel === 0) {
        pressFlashRowIndex = null;
      }
    }

    if (
      pointerActive &&
      (pointerMode === "list" || pointerMode === "rim-pending") &&
      currentView.kind === "playlists" &&
      !playlistSheetOpen &&
      pressedRowIndex !== null &&
      pointerMovedDistance < LIBRARY_CONSTANTS.TAP_DRIFT_LIMIT_PX &&
      !longPressTriggered
    ) {
      longPressSeconds += deltaSeconds;
      if (longPressSeconds >= LIBRARY_CONSTANTS.LONG_PRESS_DURATION_SECONDS) {
        longPressTriggered = true;
        pointerMode = "sheet";
        openPlaylistActionSheet(pressedRowIndex);
      }
    }

    let deleteTriggeredThisStep = false;
    if (deleteHoldActive && playlistSheetOpen) {
      deleteHoldProgress = clamp(
        deleteHoldProgress + deltaSeconds / LIBRARY_CONSTANTS.DELETE_HOLD_DURATION_SECONDS,
        0,
        1,
      );
      if (deleteHoldProgress >= 1) {
        deleteHoldActive = false;
        deleteHoldProgress = 0;
        playlistSheetOpen = false;
        deleteCompletedFlash = 1;
        deleteTriggeredThisStep = true;
        if (playlistSheetIndex >= 0 && playlistSheetIndex < playlists.length) {
          playlists.splice(playlistSheetIndex, 1);
        }
        syncCounts();
        if (typeof onPersistPlaylists === "function") {
          onPersistPlaylists(clonePlaylists(playlists));
        }
        if (playlists.length === 0) {
          viewStack[viewStack.length - 1] = {
            kind: "empty",
            title: "Playlists",
            subtitle: "0 MIXES",
            emptyIcon: "playlists",
            emptyTitle: "No Playlists Left",
            emptyMessage: "ADD SONGS TO BUILD A MIX",
            scrollPosition: 0,
            targetIndex: 0,
          };
        } else {
          const maxScroll = Math.max(
            0,
            playlists.length - LIBRARY_CONSTANTS.LIST_FULL_SLOTS,
          );
          const clampedScroll = clamp(currentView.targetIndex, 0, maxScroll);
          currentView.targetIndex = clampedScroll;
          currentView.scrollPosition = clampedScroll;
          currentView.subtitle = `${formatIntegerCount(playlists.length)} MIXES`;
        }
      }
    } else if (deleteHoldProgress > 0) {
      deleteHoldProgress = Math.max(0, deleteHoldProgress - deltaSeconds * DELETE_HOLD_DECAY_RATE);
    }

    if (!deleteTriggeredThisStep && deleteCompletedFlash > 0) {
      deleteCompletedFlash = Math.max(
        0,
        deleteCompletedFlash - deltaSeconds * DELETE_FLASH_DECAY_RATE,
      );
    }

    const hoveredSliceIndex = letterSearchActive
      ? resolveLibraryRimSliceIndex(arcCurrentAngle)
      : -1;
    const sliceCount = LIBRARY_CONSTANTS.RIM_SLICE_COUNT;
    for (let sliceIndex = 0; sliceIndex < sliceCount; sliceIndex += 1) {
      let targetPull = 0;
      if (hoveredSliceIndex >= 0) {
        const sliceDistance = shortestSliceDistance(sliceIndex, hoveredSliceIndex, sliceCount);
        if (sliceDistance === 0) {
          targetPull = 1.0;
        } else if (sliceDistance === 1) {
          targetPull = RIM_SLAB_NEIGHBOR_ONE_PULL;
        } else if (sliceDistance === 2) {
          targetPull = RIM_SLAB_NEIGHBOR_TWO_PULL;
        }
      }
      const previousPull = rimSlabPullLevels[sliceIndex] || 0;
      const pullRate = targetPull > previousPull ? RIM_SLAB_RISE_RATE : RIM_SLAB_FALL_RATE;
      let nextPull = lerp(previousPull, targetPull, 1 - Math.exp(-pullRate * deltaSeconds));
      if (targetPull === 1.0 && nextPull >= RIM_SLAB_PEAK_SNAP_THRESHOLD) {
        nextPull = 1.0;
      } else if (nextPull < RIM_SLAB_ZERO_THRESHOLD) {
        nextPull = 0;
      }
      rimSlabPullLevels[sliceIndex] = nextPull;
    }

    const isArcEngaged = pointerActive && pointerMode === "arc-scroll";
    const hudTarget = letterSearchActive ? 1 : 0;
    const hudRate = hudTarget > 0 ? LETTER_HUD_ATTACK_RATE : LETTER_HUD_RELEASE_RATE;
    letterSearchHudAlpha = lerp(
      letterSearchHudAlpha,
      hudTarget,
      1 - Math.exp(-hudRate * deltaSeconds),
    );
    if (hudTarget === 0 && letterSearchHudAlpha < PRESS_CLEAR_THRESHOLD) {
      letterSearchHudAlpha = 0;
    }
    if (!isArcEngaged) {
      arcFeedbackAlpha = Math.max(0, arcFeedbackAlpha - deltaSeconds * ARC_FEEDBACK_DECAY_RATE);
    }
    if (!pointerActive && cratePullUpProgress > 0) {
      cratePullUpProgress = Math.max(
        0,
        cratePullUpProgress - deltaSeconds * CRATE_PULL_UP_DECAY_RATE,
      );
    }

    const isDraggingScroll =
      pointerActive &&
      (pointerMode === "list" || pointerMode === "crate" || pointerMode === "arc-scroll");
    if (!isDraggingScroll && currentView.kind !== "empty") {
      const snapFollow = 1 - Math.exp(-SCROLL_SNAP_RATE * deltaSeconds);
      currentView.scrollPosition = lerp(
        currentView.scrollPosition,
        currentView.targetIndex,
        snapFollow,
      );
      if (Math.abs(currentView.scrollPosition - currentView.targetIndex) < SCROLL_SETTLE_EPSILON) {
        currentView.scrollPosition = currentView.targetIndex;
      }
    }

    if (!pointerActive) {
      backSwipeProgress = Math.max(0, backSwipeProgress - deltaSeconds * BACK_SWIPE_DECAY_RATE);
    }
  }

  /**
   * Begin a pointer interaction for Hub tiles, curved lists, vinyl crate, rim-arc scroll,
   * Playlist Action Sheet buttons, Hold-to-Delete, or back-swipe gestures.
   *
   * @param {number} pointX Display coordinate X in [0, 360].
   * @param {number} pointY Display coordinate Y in [0, 360].
   */
  function handlePointerDown(pointX, pointY) {
    if (!Number.isFinite(pointX) || !Number.isFinite(pointY)) {
      return;
    }
    const currentView = getCurrentView();
    const distanceFromCenter = Math.hypot(
      pointX - DISPLAY_CENTER_PX,
      pointY - DISPLAY_CENTER_PX,
    );
    const startAngle = Math.atan2(pointY - DISPLAY_CENTER_PX, pointX - DISPLAY_CENTER_PX);

    letterSearchActive = false;
    pointerActive = true;
    pointerStartX = pointX;
    pointerStartY = pointY;
    pointerLastX = pointX;
    pointerLastY = pointY;
    pointerVelocity = 0;
    pointerStartScroll = currentView.scrollPosition;
    pointerMovedDistance = 0;
    longPressSeconds = 0;
    longPressTriggered = false;
    arcStartAngle = startAngle;
    arcLastAngle = startAngle;
    arcCurrentAngle = startAngle;
    arcSignedSweepRadians = 0;
    arcAbsoluteSweepRadians = 0;
    letterSearchAccumulator = 0;

    if (vinylLaunchTarget === 1) {
      pointerMode = "back-swipe";
    } else if (playlistSheetOpen) {
      const hitDeleteButton =
        pointX >= DELETE_BUTTON_LEFT_X &&
        pointX <= DELETE_BUTTON_RIGHT_X &&
        pointY >= DELETE_BUTTON_TOP_Y &&
        pointY <= DELETE_BUTTON_BOTTOM_Y;
      if (hitDeleteButton) {
        pointerMode = "hold-delete";
        deleteHoldActive = true;
      } else if (pointX <= OVERLAY_LEFT_EDGE_SWIPE_MAX_X) {
        pointerMode = "back-swipe";
      } else {
        pointerMode = "sheet";
      }
    } else if (currentView.kind === "empty") {
      pointerMode = pointX <= OVERLAY_LEFT_EDGE_SWIPE_MAX_X ? "back-swipe" : "empty";
    } else if (distanceFromCenter >= LIBRARY_CONSTANTS.ARC_RIM_MIN_RADIUS_PX) {
      pointerMode = "rim-pending";
      if (currentView.kind !== "albums") {
        const rows = resolveViewRows(currentView);
        const hitIndex = hitTestListRow(pointX, pointY, currentView, rows);
        pressedRowIndex = hitIndex;
        if (hitIndex !== null) {
          pressHighlightLevel = PRESS_INITIAL_LEVEL;
        }
      }
    } else if (currentView.kind === "albums") {
      pointerMode = "crate";
    } else {
      pointerMode = "list";
      const rows = resolveViewRows(currentView);
      const hitIndex = hitTestListRow(pointX, pointY, currentView, rows);
      pressedRowIndex = hitIndex;
      if (hitIndex !== null) {
        pressHighlightLevel = PRESS_INITIAL_LEVEL;
      }
    }
  }

  /**
   * Update active scroll, rim-arc / Crystal-Slab seek, back-swipe, or Hold-to-Delete
   * bounds during pointer movement.
   *
   * @param {number} pointX Display coordinate X in [0, 360].
   * @param {number} pointY Display coordinate Y in [0, 360].
   */
  function handlePointerMove(pointX, pointY) {
    if (!pointerActive || !Number.isFinite(pointX) || !Number.isFinite(pointY)) {
      return;
    }
    const currentView = getCurrentView();
    const deltaX = pointX - pointerStartX;
    const deltaY = pointY - pointerStartY;
    const stepX = pointX - pointerLastX;
    const stepY = pointY - pointerLastY;

    pointerMovedDistance = Math.max(pointerMovedDistance, Math.hypot(deltaX, deltaY));
    pointerLastX = pointX;
    pointerLastY = pointY;

    if (pointerMode === "hold-delete") {
      const stayedInsideDeleteZone =
        pointX >= DELETE_HOLD_GUARD_LEFT_X &&
        pointX <= DELETE_HOLD_GUARD_RIGHT_X &&
        pointY >= DELETE_HOLD_GUARD_TOP_Y &&
        pointY <= DELETE_HOLD_GUARD_BOTTOM_Y;
      if (!stayedInsideDeleteZone) {
        deleteHoldActive = false;
      }
      return;
    }

    if (pointerMode === "back-swipe") {
      backSwipeProgress =
        deltaX > 0 ? clamp(deltaX / LIBRARY_CONSTANTS.BACK_SWIPE_DISTANCE_PX, 0, 1) : 0;
      return;
    }

    if (pointerMode === "rim-pending") {
      if (pointerMovedDistance < LIBRARY_CONSTANTS.TAP_DRIFT_LIMIT_PX) {
        return;
      }
      pressedRowIndex = null;
      if (
        pointerStartX <= RIM_LEFT_SWIPE_START_MAX_X &&
        deltaX > RIM_LEFT_SWIPE_MIN_DELTA_X &&
        deltaX > Math.abs(deltaY) * RIM_LEFT_SWIPE_SLOPE_RATIO
      ) {
        pointerMode = "back-swipe";
        backSwipeProgress = clamp(deltaX / LIBRARY_CONSTANTS.BACK_SWIPE_DISTANCE_PX, 0, 1);
        return;
      }
      if (
        currentView.kind === "albums" &&
        deltaY < -LIBRARY_CONSTANTS.TAP_DRIFT_LIMIT_PX &&
        Math.abs(deltaY) > Math.abs(deltaX) * LIBRARY_CONSTANTS.CRATE_SWIPE_UP_SLOPE_RATIO
      ) {
        pointerMode = "crate";
        currentView.scrollPosition = pointerStartScroll;
        pointerVelocity = 0;
        cratePullUpProgress = clamp(-deltaY / CRATE_PULL_UP_PREVIEW_DISTANCE_PX, 0, 1);
        return;
      }
      const currentDistance = Math.hypot(
        pointX - DISPLAY_CENTER_PX,
        pointY - DISPLAY_CENTER_PX,
      );
      const nextAngle = Math.atan2(pointY - DISPLAY_CENTER_PX, pointX - DISPLAY_CENTER_PX);
      const initialAngleDelta = normalizeAngleDelta(nextAngle, arcStartAngle);

      if (
        currentDistance >= RIM_ENGAGE_MIN_RADIUS_PX &&
        Math.abs(initialAngleDelta) > RIM_ENGAGE_MIN_ANGLE_DELTA
      ) {
        pointerMode = "arc-scroll";
        arcCurrentAngle = nextAngle;
        arcLastAngle = nextAngle;
        applyArcDelta(initialAngleDelta, currentView);
      } else {
        pointerMode = currentView.kind === "albums" ? "crate" : "list";
      }
      return;
    }

    if (pointerMode === "arc-scroll") {
      const nextAngle = Math.atan2(pointY - DISPLAY_CENTER_PX, pointX - DISPLAY_CENTER_PX);
      const deltaAngle = normalizeAngleDelta(nextAngle, arcLastAngle);
      arcLastAngle = nextAngle;
      arcCurrentAngle = nextAngle;
      if (Math.abs(deltaAngle) > ARC_MIN_ANGLE_STEP) {
        applyArcDelta(deltaAngle, currentView);
      }
      return;
    }

    if (pointerMode === "list") {
      if (pointerMovedDistance >= LIBRARY_CONSTANTS.TAP_DRIFT_LIMIT_PX) {
        pressedRowIndex = null;
      }
      if (
        pointerStartX <= LIST_BACK_SWIPE_START_MAX_X &&
        deltaX > LIST_BACK_SWIPE_MIN_DELTA_X &&
        Math.abs(deltaX) > Math.abs(deltaY) * LIST_BACK_SWIPE_SLOPE_RATIO
      ) {
        pressedRowIndex = null;
        pointerMode = "back-swipe";
        backSwipeProgress = clamp(deltaX / LIBRARY_CONSTANTS.BACK_SWIPE_DISTANCE_PX, 0, 1);
        return;
      }
      const maxScroll = getViewMaxScroll(currentView);
      const dragPitch =
        currentView.kind === "hub"
          ? LIBRARY_CONSTANTS.DRAG_PIXELS_PER_PAIR
          : LIBRARY_CONSTANTS.DRAG_PIXELS_PER_ROW;
      pointerVelocity = stepY;
      const targetScroll = pointerStartScroll - deltaY / dragPitch;
      currentView.scrollPosition = clamp(
        targetScroll,
        -DRAG_OVERSCROLL_SLOTS,
        maxScroll + DRAG_OVERSCROLL_SLOTS,
      );
      return;
    }

    if (pointerMode === "crate") {
      if (
        deltaY < 0 &&
        Math.abs(deltaY) > Math.abs(deltaX) * LIBRARY_CONSTANTS.CRATE_SWIPE_UP_SLOPE_RATIO
      ) {
        currentView.scrollPosition = pointerStartScroll;
        pointerVelocity = 0;
        cratePullUpProgress = clamp(-deltaY / CRATE_PULL_UP_PREVIEW_DISTANCE_PX, 0, 1);
        return;
      }
      cratePullUpProgress = 0;
      const albums = resolveCrateAlbums(currentView);
      const maxIndex = Math.max(0, albums.length - 1);
      pointerVelocity = stepX;
      const targetScroll = pointerStartScroll - deltaX / LIBRARY_CONSTANTS.DRAG_PIXELS_PER_ALBUM;
      currentView.scrollPosition = clamp(
        targetScroll,
        -DRAG_OVERSCROLL_SLOTS,
        maxIndex + DRAG_OVERSCROLL_SLOTS,
      );
      return;
    }

    if (pointerMode === "empty" || pointerMode === "sheet") {
      if (deltaX > OVERLAY_BACK_SWIPE_MIN_DELTA_X) {
        pointerMode = "back-swipe";
        backSwipeProgress = clamp(deltaX / LIBRARY_CONSTANTS.BACK_SWIPE_DISTANCE_PX, 0, 1);
      }
    }
  }

  /**
   * Finalize pointer interaction: commit taps, velocity-assisted scroll snapping,
   * crate disc launches, Playlist Action Sheet buttons, or back-swipe navigation.
   *
   * @param {number} pointX Display coordinate X in [0, 360].
   * @param {number} pointY Display coordinate Y in [0, 360].
   */
  function handlePointerUp(pointX, pointY) {
    if (!pointerActive) {
      return;
    }

    const releaseX = Number.isFinite(pointX) ? pointX : pointerLastX;
    const releaseY = Number.isFinite(pointY) ? pointY : pointerLastY;
    const totalDeltaX = releaseX - pointerStartX;
    const totalDeltaY = releaseY - pointerStartY;
    const releaseStartScroll = pointerStartScroll;
    const finalDistance = Math.hypot(totalDeltaX, totalDeltaY);
    const movedDistance = Math.max(pointerMovedDistance, finalDistance);
    const wasTap = movedDistance < LIBRARY_CONSTANTS.TAP_DRIFT_LIMIT_PX;
    const currentView = getCurrentView();

    const activeMode = pointerMode;
    const wasLongPressTriggered = longPressTriggered;
    const releaseVelocity = pointerVelocity;
    const previouslyPressedRow = pressedRowIndex;

    clearPointerState();

    if (wasLongPressTriggered) {
      return;
    }

    if (activeMode === "arc-scroll") {
      letterSearchActive = false;
      pressedRowIndex = null;
      return;
    }

    if (activeMode === "hold-delete") {
      return;
    }

    if (activeMode === "back-swipe") {
      if (backSwipeProgress >= LIBRARY_CONSTANTS.BACK_SWIPE_COMMIT_THRESHOLD) {
        stepBack();
      } else if (wasTap && currentView.kind === "empty") {
        const hitBackCta =
          releaseY >= EMPTY_BACK_CTA_TOP_Y &&
          releaseY <= EMPTY_BACK_CTA_BOTTOM_Y &&
          Math.abs(releaseX - DISPLAY_CENTER_PX) <= EMPTY_BACK_CTA_HALF_WIDTH_PX;
        if (hitBackCta) {
          stepBack();
        }
      }
      return;
    }

    if (activeMode === "sheet") {
      if (wasTap && playlistSheetOpen) {
        const hitPlayButton =
          releaseX >= DELETE_BUTTON_LEFT_X &&
          releaseX <= DELETE_BUTTON_RIGHT_X &&
          releaseY >= SHEET_PLAY_TOP_Y &&
          releaseY <= SHEET_PLAY_BOTTOM_Y;
        const hitShuffleButton =
          releaseX >= DELETE_BUTTON_LEFT_X &&
          releaseX <= DELETE_BUTTON_RIGHT_X &&
          releaseY >= SHEET_SHUFFLE_TOP_Y &&
          releaseY <= SHEET_SHUFFLE_BOTTOM_Y;
        const hitOutsideCard =
          releaseX < SHEET_CARD_LEFT_X ||
          releaseX > SHEET_CARD_RIGHT_X ||
          releaseY < SHEET_CARD_TOP_Y ||
          releaseY > SHEET_CARD_BOTTOM_Y;

        if (hitPlayButton || hitShuffleButton) {
          const playlistEntry = playlists[playlistSheetIndex];
          if (playlistEntry && playlistEntry.trackIds.length > 0) {
            const chosenTrackId = hitShuffleButton
              ? playlistEntry.trackIds[Math.floor(Math.random() * playlistEntry.trackIds.length)]
              : playlistEntry.trackIds[0];
            const chosenTrack = trackByIdMap.get(chosenTrackId);
            if (chosenTrack) {
              const sourceSpec = {
                queueSource: "playlist",
                playlistId: playlistEntry.id,
                albumId: chosenTrack.albumId,
              };
              if (hitShuffleButton) {
                sourceSpec.shuffle = true;
              }
              launchVinyl(chosenTrack.albumId, "tracks", chosenTrack.id, sourceSpec);
            }
          }
        } else if (hitOutsideCard) {
          playlistSheetOpen = false;
        }
      }
      return;
    }

    if (activeMode === "empty") {
      if (wasTap) {
        const hitBackCta =
          releaseY >= EMPTY_BACK_CTA_TOP_Y &&
          releaseY <= EMPTY_BACK_CTA_BOTTOM_Y &&
          Math.abs(releaseX - DISPLAY_CENTER_PX) <= EMPTY_BACK_CTA_HALF_WIDTH_PX;
        if (hitBackCta) {
          stepBack();
        }
      }
      return;
    }

    if (activeMode === "crate" || (activeMode === "rim-pending" && currentView.kind === "albums")) {
      const albums = resolveCrateAlbums(currentView);
      const maxIndex = Math.max(0, albums.length - 1);
      cratePullUpProgress = 0;
      if (wasTap) {
        const focusedIndex = clamp(Math.round(currentView.scrollPosition), 0, maxIndex);
        const hitSwipeUpIndicator =
          Math.abs(releaseX - DISPLAY_CENTER_PX) <= CRATE_SWIPE_UP_HINT_HALF_WIDTH_PX &&
          releaseY >= CRATE_SWIPE_UP_HINT_TOP_Y &&
          releaseY <= CRATE_SWIPE_UP_HINT_BOTTOM_Y;
        const hitPeekingVinyl =
          releaseX >= CRATE_PEEK_LEFT_X &&
          releaseX <= CRATE_PEEK_RIGHT_X &&
          releaseY >= CRATE_PEEK_TOP_Y &&
          releaseY <= CRATE_PEEK_BOTTOM_Y;
        const hitCenterSleeve =
          releaseX >= CRATE_SLEEVE_LEFT_X &&
          releaseX < CRATE_PEEK_LEFT_X &&
          releaseY >= CRATE_SLEEVE_TOP_Y &&
          releaseY <= CRATE_SLEEVE_BOTTOM_Y;

        if (hitSwipeUpIndicator && albums[focusedIndex]) {
          openFocusedAlbumTracks();
        } else if ((hitPeekingVinyl || hitCenterSleeve) && albums[focusedIndex]) {
          launchFocusedCrateAlbum(currentView);
        } else if (releaseX < CRATE_SLEEVE_LEFT_X && focusedIndex > 0) {
          currentView.targetIndex = focusedIndex - 1;
        } else if (releaseX > CRATE_PEEK_RIGHT_X && focusedIndex < maxIndex) {
          currentView.targetIndex = focusedIndex + 1;
        } else {
          currentView.targetIndex = focusedIndex;
        }
      } else if (
        totalDeltaY <= -LIBRARY_CONSTANTS.CRATE_SWIPE_UP_MIN_DELTA_Y &&
        Math.abs(totalDeltaY) > Math.abs(totalDeltaX) * LIBRARY_CONSTANTS.CRATE_SWIPE_UP_SLOPE_RATIO
      ) {
        const anchorIndex = clamp(Math.round(releaseStartScroll), 0, maxIndex);
        currentView.scrollPosition = anchorIndex;
        currentView.targetIndex = anchorIndex;
        openFocusedAlbumTracks();
      } else {
        const projected =
          currentView.scrollPosition -
          clamp(
            releaseVelocity * CRATE_FLICK_VELOCITY_WEIGHT,
            -FLICK_MAX_PROJECTED_SLOTS,
            FLICK_MAX_PROJECTED_SLOTS,
          );
        currentView.targetIndex = clamp(Math.round(projected), 0, maxIndex);
      }
      return;
    }

    if (activeMode === "list" || activeMode === "rim-pending") {
      const rows = resolveViewRows(currentView);
      const maxScroll = getViewMaxScroll(currentView);
      pressedRowIndex = null;

      if (wasTap) {
        const tappedRowIndex =
          previouslyPressedRow !== null
            ? previouslyPressedRow
            : hitTestListRow(releaseX, releaseY, currentView, rows);
        if (tappedRowIndex !== null) {
          pressFlashRowIndex = tappedRowIndex;
          pressFlashLevel = 1.0;
          activateFocused(tappedRowIndex);
        }
      } else {
        const projected =
          currentView.scrollPosition -
          clamp(
            releaseVelocity * LIST_FLICK_VELOCITY_WEIGHT,
            -FLICK_MAX_PROJECTED_SLOTS,
            FLICK_MAX_PROJECTED_SLOTS,
          );
        currentView.targetIndex = clamp(Math.round(projected), 0, maxScroll);
      }
      return;
    }

    pressedRowIndex = null;
  }

  /**
   * Cancel active pointer tracking without committing taps, drags, or holds.
   */
  function handlePointerCancel() {
    clearPointerState();
    pressedRowIndex = null;
    letterSearchActive = false;
  }

  /**
   * Return a full snapshot of the Library controller state for rendering and testing.
   *
   * @returns {object} Current Library render snapshot.
   */
  function getRenderSnapshot() {
    const activeView = getCurrentView();
    return {
      catalog: {
        ...catalog,
        counts: { ...counts },
      },
      viewStack: viewStack.map((viewEntry) => ({
        ...viewEntry,
        ...(Array.isArray(viewEntry.albumIds) ? { albumIds: viewEntry.albumIds.slice() } : {}),
      })),
      currentView: {
        ...activeView,
        ...(Array.isArray(activeView.albumIds) ? { albumIds: activeView.albumIds.slice() } : {}),
      },
      currentRows: resolveViewRows(activeView),
      currentCrateAlbums: activeView.kind === "albums" ? resolveCrateAlbums(activeView) : [],
      scrollPosition: activeView.scrollPosition,
      targetIndex: activeView.targetIndex,
      activeTrackId,
      playlists: clonePlaylists(playlists),
      playlistSheetOpen,
      playlistSheetIndex,
      playlistSheetProgress,
      deleteHoldActive,
      deleteHoldProgress,
      deleteCompletedFlash,
      vinylLaunchAlbumId,
      vinylLaunchSource,
      vinylLaunchProgress,
      vinylLaunchTarget,
      vinylRotationRadians,
      vinylNudgeRadians,
      idleClockSeconds,
      transitionDirection,
      transitionProgress,
      pressedRowIndex,
      pressHighlightLevel,
      pressFlashRowIndex,
      pressFlashLevel,
      arcFeedbackAlpha,
      arcStartAngle,
      arcCurrentAngle,
      arcSignedSweepRadians,
      arcAbsoluteSweepRadians,
      letterSearchActive,
      letterSearchIndex,
      letterSearchHudAlpha,
      letterEntries: resolveViewLetterEntries(activeView),
      rimSlabPullLevels: rimSlabPullLevels.slice(),
      cratePullUpProgress,
      backSwipeProgress,
      pointerActive,
      pointerMode,
      counts: { ...counts },
    };
  }

  return {
    hydrateCatalog,
    enterFromHome,
    setActiveTrackId,
    step,
    stepBack,
    stepDetent,
    activateFocused,
    openFocusedAlbumTracks,
    handlePointerDown,
    handlePointerMove,
    handlePointerUp,
    handlePointerCancel,
    getRenderSnapshot,
    getState: getRenderSnapshot,
  };
}
