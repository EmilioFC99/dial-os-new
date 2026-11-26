/**
 * Pure deterministic Now Playing controller, queue row layout, lyrics cue frame
 * resolution, and track detail enrichment for the DialOS 360x360 display simulator.
 */

import {
  REPEAT_MODES,
  resolveSkipOutcome,
} from "./audio_engine.js";
import {
  DISPLAY_CENTER_PX,
  FULL_CIRCLE_RADIANS,
  clamp,
  formatClockTime,
  lerp,
  normalizeRange,
} from "./geometry.js";

const SECONDS_PER_MINUTE = 60;
const DEFAULT_TRACK_DURATION_SECONDS = 180;
const DEFAULT_VOLUME_LEVEL = 0.62;
const FALLBACK_MB_PER_MINUTE = 7.8;

const DEFAULT_TECH_SPEC = Object.freeze({
  format: "FLAC 16-BIT",
  sampleRate: "44.1 KHZ",
  bitrate: "1,024 KBPS",
  mbPerMin: FALLBACK_MB_PER_MINUTE,
});

const LYRICS_MODE_BADGE_LABELS = Object.freeze({
  auto: "TRACK",
  timed: "TIMED SYNC",
  untimed: "AUTO PACE",
  none: "NO LYRICS",
});

const NO_LYRICS_DEFAULT_NOTE = "NO EMBEDDED OR .LRC FILE";
const TIMED_SYNTH_START_PADDING_SECONDS = 6;
const TIMED_SYNTH_END_PADDING_SECONDS = 18;
const TIMED_SYNTH_MIN_SPAN_SECONDS = 24;
const UNTIMED_START_OFFSET_RATIO = 0.03;
const UNTIMED_SPAN_RATIO = 0.93;
const UNTIMED_MAX_PROGRESS_RATIO = 0.9999;

const QUEUE_CHORD_RADIUS_PX = 152;
const QUEUE_SAFE_DELTA_Y_MAX_PX = 138;
const QUEUE_CHORD_MIN_SQUARED = 100;
const QUEUE_CHORD_INSET_PX = 4;
const QUEUE_MAX_HALF_WIDTH_PX = 126;
const QUEUE_NEG_FADE_RATE = 1.65;
const QUEUE_POS_FADE_START_SLOTS = 1.35;
const QUEUE_POS_FADE_RATE = 0.72;
const QUEUE_HIT_MIN_ALPHA = 0.22;
const QUEUE_HIT_PADDING_Y_PX = 2;
const QUEUE_VISIBLE_PADDING_ROWS = 2;
const QUEUE_VISIBLE_FORWARD_ROWS = 4;
const QUEUE_VISIBLE_TAIL_SLOTS = 2;
const QUEUE_OVERSCROLL_TOP_SLOTS = 0.38;
const QUEUE_OVERSCROLL_BOTTOM_SLOTS = 0.35;
const QUEUE_FLICK_VELOCITY_WEIGHT = 0.04;
const QUEUE_FLICK_MAX_SLOTS = 0.45;
const QUEUE_SNAP_SPRING_RATE = 13.5;
const QUEUE_SNAP_SETTLE_EPSILON = 0.002;
const QUEUE_CLOSE_PULL_DELTA_Y_PX = 68;
const QUEUE_CLOSE_START_SCROLL_LIMIT = 0.05;
const QUEUE_HEADER_MAX_Y_PX = 104;
const QUEUE_HEADER_CLOSE_DELTA_Y_PX = 40;
const QUEUE_HEADER_BACK_MIN_DELTA_X_PX = 24;
const QUEUE_HEADER_BACK_SLOPE_RATIO = 1.35;
const QUEUE_LIST_BACK_START_MAX_X_PX = 110;
const QUEUE_LIST_BACK_MIN_DELTA_X_PX = 28;
const QUEUE_LIST_BACK_SLOPE_RATIO = 1.45;

const TRANSITION_ROTATION_DEGREES = 20;
const DEGREES_PER_HALF_TURN = 180;
const TRANSITION_ROTATION_RADIANS = (TRANSITION_ROTATION_DEGREES * Math.PI) / DEGREES_PER_HALF_TURN;
const OPEN_TRANSITION_DURATION_SECONDS = 0.62;
const VINYL_SPIN_RADIANS_PER_SECOND =
  TRANSITION_ROTATION_RADIANS / OPEN_TRANSITION_DURATION_SECONDS;
const VINYL_SPIN_ATTACK_RATE = 5.2;
const VINYL_SPIN_COAST_RATE = 3.8;
const VINYL_SPIN_ZERO_THRESHOLD = 0.002;
const VINYL_NUDGE_SPRING_RATE = 8.5;
const VINYL_PLAY_NUDGE_RADIANS = 0.24;
const VINYL_SKIP_CROSSFADE_NUDGE_RADIANS = 0.42;
const VINYL_SKIP_SAME_ALBUM_NUDGE_RADIANS = 0.14;
const VINYL_QUEUE_SELECT_NUDGE_RADIANS = 0.36;

const OPEN_CHROME_HOLD_SECONDS = 1.65;
const OPEN_CHROME_APPEAR_PROGRESS = 0.65;
const OPEN_CHROME_SETTLE_PROGRESS = 0.99;
const OPEN_CHROME_ATTACK_RATE = 10;
const OPEN_CHROME_DECAY_RATE = 2.2;
const SKIP_DRAG_SETTLE_RATE = 15;
const SKIP_DRAG_CLAMP_PX = 180;
const SKIP_DRAG_MIN_OFFSET_PX = 0.2;
const SKIP_DRAG_SNAP_ZERO_PX = 0.5;

const SEEK_HOLD_DELAY_SECONDS = 0.10;
const SEEK_HOLD_DECAY_RATE = 5.5;
const SEEK_HOLD_MAX_DRIFT_PX = 12;
const SEEK_EXPAND_ATTACK_RATE = 18;
const SEEK_EXPAND_RELEASE_RATE = 13;
const SEEK_EXPAND_ZERO_THRESHOLD = 0.006;
const SEEK_ARC_MIN_RADIUS_PX = 22;
const SEEK_ARC_MIN_ANGLE_STEP = 0.0005;
const SEEK_ARC_SPIN_FACTOR = 0.65;
const SEEK_SPINDLE_SENSITIVITY = 0.35;
const SEEK_SPINDLE_SPIN_FACTOR = 0.012;
const SEEK_END_GUARD_SECONDS = 0.5;

const VOLUME_BEZEL_START_MOVE_PX = 8;
const VOLUME_HUD_ATTACK_RATE = 20;
const VOLUME_HUD_DECAY_RATE = 3.2;
const WHEEL_VOLUME_STEP = 0.04;

const LEFT_EDGE_SWIPE_MAX_X_PX = 68;
const LEFT_EDGE_MIN_BACK_DELTA_X_PX = 12;
const LEFT_EDGE_BACK_SLOPE_RATIO = 1.25;
const LEFT_EDGE_ACTIONS_MIN_DELTA_X_PX = 14;
const LEFT_EDGE_ACTIONS_SLOPE_RATIO = 1.2;

const VINYL_QUEUE_MIN_DELTA_Y_PX = -20;
const VINYL_QUEUE_SLOPE_RATIO = 1.3;
const VINYL_SKIP_MIN_DELTA_X_PX = 14;
const VINYL_SKIP_SLOPE_RATIO = 1.15;

const QUICK_ACTION_HIT_PAD_PX = 5;
const QUICK_ACTION_QUEUE_MIN_DELTA_Y_PX = -36;
const QUICK_ACTIONS_SPRING_RATE = 14;
const PICKER_SPRING_RATE = 15;
const DETAILS_SPRING_RATE = 14;
const QUEUE_SHEET_SPRING_RATE = 14;
const LYRICS_SPRING_RATE = 10.5;
const SHEET_SETTLE_EPSILON = 0.005;
const LYRICS_SETTLE_EPSILON = 0.004;
const LYRICS_ACTIVE_VISIBILITY_THRESHOLD = 0.01;

const IDLE_CTA_LEFT_X_PX = 96;
const IDLE_CTA_RIGHT_X_PX = 264;
const IDLE_CTA_TOP_Y_PX = 246;
const IDLE_CTA_BOTTOM_Y_PX = 288;
const IDLE_CTA_MAX_DRIFT_PX = 12;

const PICKER_CARD_LEFT_X_PX = 66;
const PICKER_CARD_RIGHT_X_PX = 294;
const PICKER_CARD_TOP_Y_PX = 78;
const PICKER_CARD_BOTTOM_Y_PX = 280;
const PICKER_ROW_LEFT_X_PX = 78;
const PICKER_ROW_RIGHT_X_PX = 282;
const PICKER_ROW_START_Y_PX = 132;
const PICKER_ROW_PITCH_PX = 35;
const PICKER_ROW_HEIGHT_PX = 32;
const PICKER_MAX_VISIBLE_ROWS = 4;

const QUEUE_HEADER_CLOSE_SLOPE_RATIO = 1.2;

const PULSE_DECAY_RATE = 2.5;
const BANNER_FADE_IN_SECONDS = 0.24;
const BANNER_HOLD_SECONDS = 2.0;
const BANNER_FADE_OUT_SECONDS = 0.42;
const TOAST_HOLD_SECONDS = 1.6;
const TOAST_DECAY_RATE = 3.2;
const BACK_SWIPE_DECAY_RATE = 4.5;

export const NOW_PLAYING_CONSTANTS = Object.freeze({
  OPEN_DURATION_SECONDS: OPEN_TRANSITION_DURATION_SECONDS,
  SKIP_DURATION_SECONDS: 0.42,
  QUICK_ACTIONS_TIMEOUT_SECONDS: 4.6,
  VOLUME_HOLD_SECONDS: 1.45,
  SPINDLE_BASE_RADIUS_PX: 13.5,
  SPINDLE_SEEK_RADIUS_PX: 62,
  SEEK_HOLD_RADIUS_PX: 90,
  SEEK_HOLD_DURATION_SECONDS: 0.42,
  VOLUME_START_MIN_RADIUS_PX: 135,
  VOLUME_START_MAX_ANGLE_RADIANS: Math.PI / 4,
  VOLUME_LIMIT_RATIO: 0.85,
  VOLUME_FULL_ARC_RADIANS: Math.PI,
  DOUBLE_TAP_WINDOW_SECONDS: 0.25,
  QUEUE_ROW_PITCH_PX: 58,
  QUEUE_ROW_HEIGHT_PX: 48,
  QUEUE_TOP_Y: 136,
  QUEUE_START_MIN_Y_PX: 245,
  LYRICS_TRANSITION_DURATION_SECONDS: 0.44,
  SKIP_DRAG_COMMIT_PX: 56,
  QUEUE_PULL_COMMIT_DELTA_Y: -38,
  BACK_SWIPE_DISTANCE_PX: 96,
  BACK_SWIPE_COMMIT_THRESHOLD: 0.45,
  TAP_DRIFT_LIMIT_PX: 10,
});

export const NOW_PLAYING_REPEAT_MODES = Object.freeze([
  Object.freeze({ id: "off", label: "REPEAT OFF", caption: "REPEAT" }),
  Object.freeze({ id: "all", label: "REPEAT ALL", caption: "REP ALL" }),
  Object.freeze({ id: "one", label: "REPEAT ONE", caption: "REP 1" }),
]);

export const NOW_PLAYING_QUICK_BUTTONS = Object.freeze([
  Object.freeze({ id: "favorite", label: "FAV", centerX: 104, centerY: 168, radius: 24 }),
  Object.freeze({ id: "shuffle", label: "SHUFFLE", centerX: 180, centerY: 168, radius: 24 }),
  Object.freeze({ id: "repeat", label: "REPEAT", centerX: 256, centerY: 168, radius: 24 }),
  Object.freeze({ id: "playlist", label: "MIX+", centerX: 104, centerY: 238, radius: 24 }),
  Object.freeze({ id: "lyrics", label: "LYRICS", centerX: 180, centerY: 238, radius: 24 }),
  Object.freeze({ id: "info", label: "INFO", centerX: 256, centerY: 238, radius: 24 }),
]);

export const NOW_PLAYING_LYRICS_MODES = Object.freeze(["auto", "timed", "untimed", "none"]);

function normalizeAngleDelta(nextAngle, previousAngle) {
  return Math.atan2(Math.sin(nextAngle - previousAngle), Math.cos(nextAngle - previousAngle));
}

function clonePlaylists(playlists) {
  if (!Array.isArray(playlists)) {
    return [];
  }
  return playlists
    .filter((playlistEntry) => playlistEntry && typeof playlistEntry === "object")
    .map((playlistEntry) => ({
      ...playlistEntry,
      name: String(playlistEntry.name ?? ""),
      trackIds: Array.isArray(playlistEntry.trackIds) ? playlistEntry.trackIds.slice() : [],
    }));
}

function resolveSafeTrackDurationSeconds(track) {
  if (
    track &&
    typeof track.durationSeconds === "number" &&
    Number.isFinite(track.durationSeconds) &&
    track.durationSeconds > 0
  ) {
    return track.durationSeconds;
  }
  return DEFAULT_TRACK_DURATION_SECONDS;
}

/**
 * Compute curved circular-chord row geometry and fade opacity for the Context Queue sheet.
 *
 * @param {number} rowIndex Zero-based track index in the queue.
 * @param {number} [scrollPosition=0] Continuous vertical queue scroll offset.
 * @returns {{
 *   rowIndex: number,
 *   centerX: number,
 *   centerY: number,
 *   width: number,
 *   height: number,
 *   rowY: number,
 *   rowHalfWidth: number,
 *   rowHeight: number,
 *   alpha: number
 * }} Row layout geometry descriptor.
 */
export function computeNowPlayingQueueRowLayout(rowIndex, scrollPosition = 0) {
  const safeRowIndex = Number.isFinite(rowIndex) ? rowIndex : 0;
  const safeScrollPosition = Number.isFinite(scrollPosition) ? scrollPosition : 0;
  const relativeSlot = safeRowIndex - safeScrollPosition;
  const rowY =
    NOW_PLAYING_CONSTANTS.QUEUE_TOP_Y + relativeSlot * NOW_PLAYING_CONSTANTS.QUEUE_ROW_PITCH_PX;

  let alpha = 1;
  if (relativeSlot < 0) {
    alpha = clamp(1 + relativeSlot * QUEUE_NEG_FADE_RATE, 0, 1);
  } else if (relativeSlot > QUEUE_POS_FADE_START_SLOTS) {
    alpha = clamp(1 - (relativeSlot - QUEUE_POS_FADE_START_SLOTS) * QUEUE_POS_FADE_RATE, 0, 1);
  }

  const safeDeltaY = Math.min(QUEUE_SAFE_DELTA_Y_MAX_PX, Math.abs(rowY - DISPLAY_CENTER_PX));
  const chordHalfWidth = Math.sqrt(
    Math.max(
      QUEUE_CHORD_MIN_SQUARED,
      QUEUE_CHORD_RADIUS_PX * QUEUE_CHORD_RADIUS_PX - safeDeltaY * safeDeltaY,
    ),
  );
  const rowHalfWidth = Math.min(QUEUE_MAX_HALF_WIDTH_PX, chordHalfWidth - QUEUE_CHORD_INSET_PX);

  return {
    rowIndex: safeRowIndex,
    centerX: DISPLAY_CENTER_PX,
    centerY: rowY,
    width: rowHalfWidth * 2,
    height: NOW_PLAYING_CONSTANTS.QUEUE_ROW_HEIGHT_PX,
    rowY,
    rowHalfWidth,
    rowHeight: NOW_PLAYING_CONSTANTS.QUEUE_ROW_HEIGHT_PX,
    alpha,
  };
}

/**
 * Hit-test visible Context Queue sheet rows at display coordinates `(pointX, pointY)`.
 *
 * @param {number} pointX Display coordinate X in [0, 360].
 * @param {number} pointY Display coordinate Y in [0, 360].
 * @param {number | { queueScrollPosition?: number }} scrollPosition Queue scroll offset or state object.
 * @param {Array<object>} tracks Active queue tracks.
 * @returns {number | null} Matching track row index, or `null` when missed.
 */
export function hitTestNowPlayingQueueRow(pointX, pointY, scrollPosition, tracks) {
  if (
    !Number.isFinite(pointX) ||
    !Number.isFinite(pointY) ||
    !Array.isArray(tracks) ||
    tracks.length === 0
  ) {
    return null;
  }

  const resolvedScroll =
    typeof scrollPosition === "number"
      ? scrollPosition
      : Number(scrollPosition?.queueScrollPosition ?? 0);
  if (!Number.isFinite(resolvedScroll)) {
    return null;
  }

  const startRow = Math.max(0, Math.floor(resolvedScroll) - QUEUE_VISIBLE_PADDING_ROWS);
  const endRow = Math.min(tracks.length - 1, Math.ceil(resolvedScroll) + QUEUE_VISIBLE_FORWARD_ROWS);

  for (let rowIndex = startRow; rowIndex <= endRow; rowIndex += 1) {
    const rowLayout = computeNowPlayingQueueRowLayout(rowIndex, resolvedScroll);
    if (rowLayout.alpha < QUEUE_HIT_MIN_ALPHA) {
      continue;
    }
    const insideX = Math.abs(pointX - DISPLAY_CENTER_PX) <= rowLayout.rowHalfWidth;
    const insideY =
      Math.abs(pointY - rowLayout.rowY) <= rowLayout.rowHeight / 2 + QUEUE_HIT_PADDING_Y_PX;
    if (insideX && insideY) {
      return rowIndex;
    }
  }
  return null;
}

/**
 * Resolve normalized lyrics source cues for `track` and `lyricsEntry` under `modeOverride`.
 *
 * @param {object | null} track Active track metadata.
 * @param {object | null} lyricsEntry Catalog lyrics descriptor for the track.
 * @param {string} [modeOverride="auto"] Lyrics mode (`auto`, `timed`, `untimed`, `none`).
 * @returns {{ kind: string, modeLabel: string, note: string, lines: Array<object | string> }} Resolved lyrics source.
 */
export function resolveTrackLyricsSource(track, lyricsEntry, modeOverride = "auto") {
  const effectiveMode = NOW_PLAYING_LYRICS_MODES.includes(modeOverride) ? modeOverride : "auto";
  const hasLines = Array.isArray(lyricsEntry?.lines) && lyricsEntry.lines.length > 0;
  const entryKind =
    lyricsEntry && (lyricsEntry.kind === "timed" || lyricsEntry.kind === "untimed") && hasLines
      ? lyricsEntry.kind
      : "none";
  const noteText =
    typeof lyricsEntry?.note === "string" && lyricsEntry.note.trim().length > 0
      ? lyricsEntry.note.trim()
      : NO_LYRICS_DEFAULT_NOTE;

  if (!track || effectiveMode === "none" || !hasLines || entryKind === "none") {
    return {
      kind: "none",
      modeLabel: LYRICS_MODE_BADGE_LABELS.none,
      note: noteText,
      lines: [],
    };
  }

  const effectiveKind = effectiveMode === "auto" ? entryKind : effectiveMode;

  if (effectiveKind === "timed") {
    if (entryKind === "timed") {
      const normalizedCues = lyricsEntry.lines
        .filter(
          (cueEntry) =>
            cueEntry &&
            typeof cueEntry.text === "string" &&
            typeof cueEntry.timeSeconds === "number" &&
            Number.isFinite(cueEntry.timeSeconds),
        )
        .map((cueEntry) => ({
          timeSeconds: Math.max(0, cueEntry.timeSeconds),
          text: cueEntry.text,
        }))
        .sort((firstCue, secondCue) => firstCue.timeSeconds - secondCue.timeSeconds);

      if (normalizedCues.length > 0) {
        return {
          kind: "timed",
          modeLabel: LYRICS_MODE_BADGE_LABELS.timed,
          note: "",
          lines: normalizedCues,
        };
      }
    }

    const plainLines = lyricsEntry.lines.map((lineEntry) =>
      typeof lineEntry === "string" ? lineEntry : String(lineEntry?.text ?? ""),
    );
    const trackDuration = resolveSafeTrackDurationSeconds(track);
    const usableSpan = Math.max(
      TIMED_SYNTH_MIN_SPAN_SECONDS,
      trackDuration - TIMED_SYNTH_END_PADDING_SECONDS,
    );
    const cueStepSeconds = usableSpan / Math.max(1, plainLines.length);
    const synthesizedCues = plainLines.map((lineText, lineIndex) => ({
      timeSeconds: TIMED_SYNTH_START_PADDING_SECONDS + lineIndex * cueStepSeconds,
      text: lineText,
    }));
    return {
      kind: "timed",
      modeLabel: LYRICS_MODE_BADGE_LABELS.timed,
      note: "",
      lines: synthesizedCues,
    };
  }

  const untimedLines = lyricsEntry.lines.map((lineEntry) =>
    typeof lineEntry === "string" ? lineEntry : String(lineEntry?.text ?? ""),
  );
  return {
    kind: "untimed",
    modeLabel: LYRICS_MODE_BADGE_LABELS.untimed,
    note: "",
    lines: untimedLines,
  };
}

/**
 * Compute the active and upcoming lyric line state for `track` at `elapsedSeconds`.
 *
 * @param {object | null} track Active track metadata.
 * @param {object | null} lyricsEntry Catalog lyrics descriptor for the track.
 * @param {number} elapsedSeconds Current track playback offset in seconds.
 * @param {string} [modeOverride="auto"] Lyrics mode (`auto`, `timed`, `untimed`, `none`).
 * @returns {{
 *   kind: string,
 *   modeLabel: string,
 *   note: string,
 *   activeIndex: number,
 *   currentText: string,
 *   activeText: string,
 *   upcomingText: string,
 *   lineProgress: number,
 *   isIntroCue: boolean
 * }} Active lyrics render frame.
 */
export function resolveActiveLyricsFrame(
  track,
  lyricsEntry,
  elapsedSeconds,
  modeOverride = "auto",
) {
  const lyricsSource = resolveTrackLyricsSource(track, lyricsEntry, modeOverride);
  if (lyricsSource.kind === "none" || lyricsSource.lines.length === 0) {
    return {
      kind: "none",
      modeLabel: lyricsSource.modeLabel,
      note: lyricsSource.note,
      activeIndex: -1,
      currentText: "",
      activeText: "",
      upcomingText: "",
      lineProgress: 0,
      isIntroCue: false,
    };
  }

  const safeElapsed =
    typeof elapsedSeconds === "number" && Number.isFinite(elapsedSeconds)
      ? Math.max(0, elapsedSeconds)
      : 0;
  const trackDuration = resolveSafeTrackDurationSeconds(track);

  if (lyricsSource.kind === "timed") {
    const cues = lyricsSource.lines;
    if (safeElapsed < cues[0].timeSeconds) {
      return {
        kind: "timed",
        modeLabel: lyricsSource.modeLabel,
        note: "",
        activeIndex: -1,
        currentText: "",
        activeText: "",
        upcomingText: cues[0].text,
        lineProgress: clamp(safeElapsed / Math.max(1, cues[0].timeSeconds), 0, 1),
        isIntroCue: true,
      };
    }

    let activeIndex = 0;
    for (let cueIndex = 0; cueIndex < cues.length; cueIndex += 1) {
      if (safeElapsed >= cues[cueIndex].timeSeconds) {
        activeIndex = cueIndex;
      } else {
        break;
      }
    }

    const currentStart = cues[activeIndex].timeSeconds;
    const nextStart =
      activeIndex + 1 < cues.length ? cues[activeIndex + 1].timeSeconds : trackDuration;
    const cueDuration = Math.max(1, nextStart - currentStart);
    const activeText = cues[activeIndex].text;
    const upcomingText = activeIndex + 1 < cues.length ? cues[activeIndex + 1].text : "";

    return {
      kind: "timed",
      modeLabel: lyricsSource.modeLabel,
      note: "",
      activeIndex,
      currentText: activeText,
      activeText,
      upcomingText,
      lineProgress: clamp((safeElapsed - currentStart) / cueDuration, 0, 1),
      isIntroCue: false,
    };
  }

  const plainLines = lyricsSource.lines;
  const normalizedRatio = clamp(
    (safeElapsed / Math.max(1, trackDuration) - UNTIMED_START_OFFSET_RATIO) / UNTIMED_SPAN_RATIO,
    0,
    UNTIMED_MAX_PROGRESS_RATIO,
  );
  const scaledPosition = normalizedRatio * plainLines.length;
  const activeIndex = clamp(Math.floor(scaledPosition), 0, plainLines.length - 1);
  const activeText = plainLines[activeIndex];
  const upcomingText = activeIndex + 1 < plainLines.length ? plainLines[activeIndex + 1] : "";

  return {
    kind: "untimed",
    modeLabel: lyricsSource.modeLabel,
    note: "",
    activeIndex,
    currentText: activeText,
    activeText,
    upcomingText,
    lineProgress: clamp(scaledPosition - activeIndex, 0, 1),
    isIntroCue: false,
  };
}

/**
 * Enrich track metadata and audio technical fields with safe fallbacks and formatted
 * queue position labels for the Track Details sheet.
 *
 * @param {object | null} track Active track object.
 * @param {object | null} album Owning album object.
 * @param {number} [queueTrackCount=1] Total tracks in the active queue.
 * @param {number} [queuePositionOneBased=1] One-based position in the active queue.
 * @param {object | null} [albumTechSpec=null] Optional album-level technical spec fallback.
 * @returns {object} Enriched track detail payload.
 */
export function enrichTrackDetails(
  track,
  album,
  queueTrackCount = 1,
  queuePositionOneBased = 1,
  albumTechSpec = null,
) {
  const durationSeconds =
    track &&
    typeof track.durationSeconds === "number" &&
    Number.isFinite(track.durationSeconds) &&
    track.durationSeconds > 0
      ? track.durationSeconds
      : 0;
  const durationText =
    typeof track?.duration === "string" && track.duration.trim().length > 0
      ? track.duration.trim()
      : formatClockTime(durationSeconds);

  const safeQueueTotal =
    typeof queueTrackCount === "number" && Number.isFinite(queueTrackCount) && queueTrackCount > 0
      ? Math.trunc(queueTrackCount)
      : 0;
  const safeQueuePosition =
    safeQueueTotal > 0 &&
    typeof queuePositionOneBased === "number" &&
    Number.isFinite(queuePositionOneBased)
      ? clamp(Math.trunc(queuePositionOneBased), 1, safeQueueTotal)
      : 0;
  const trackIndexLabel = `${String(safeQueuePosition).padStart(2, "0")} / ${String(safeQueueTotal).padStart(2, "0")}`;

  const fallbackTechSpec =
    albumTechSpec && typeof albumTechSpec === "object"
      ? albumTechSpec
      : album?.techSpec && typeof album.techSpec === "object"
        ? album.techSpec
        : null;
  const effectiveMbPerMin =
    typeof track?.mbPerMin === "number" && Number.isFinite(track.mbPerMin) && track.mbPerMin > 0
      ? track.mbPerMin
      : typeof fallbackTechSpec?.mbPerMin === "number" &&
          Number.isFinite(fallbackTechSpec.mbPerMin) &&
          fallbackTechSpec.mbPerMin > 0
        ? fallbackTechSpec.mbPerMin
        : DEFAULT_TECH_SPEC.mbPerMin;
  const estimatedMegabytes = ((durationSeconds / SECONDS_PER_MINUTE) * effectiveMbPerMin).toFixed(1);

  return {
    id: typeof track?.id === "string" ? track.id : "",
    albumId:
      typeof track?.albumId === "string"
        ? track.albumId
        : typeof album?.id === "string"
          ? album.id
          : "",
    title: typeof track?.title === "string" && track.title.length > 0 ? track.title : "No Track Loaded",
    artist:
      typeof track?.artist === "string" && track.artist.length > 0
        ? track.artist
        : typeof album?.artist === "string" && album.artist.length > 0
          ? album.artist
          : "Unknown Artist",
    albumTitle:
      typeof track?.albumTitle === "string" && track.albumTitle.length > 0
        ? track.albumTitle
        : typeof album?.title === "string" && album.title.length > 0
          ? album.title
          : "Unknown Album",
    genre:
      typeof track?.genre === "string" && track.genre.length > 0
        ? track.genre
        : typeof album?.genre === "string" && album.genre.length > 0
          ? album.genre
          : "Unknown Genre",
    year: track?.year ?? album?.year ?? "",
    trackIndexLabel,
    durationSeconds,
    durationText,
    format:
      typeof track?.format === "string" && track.format.trim().length > 0
        ? track.format
        : typeof fallbackTechSpec?.format === "string" && fallbackTechSpec.format.trim().length > 0
          ? fallbackTechSpec.format
          : DEFAULT_TECH_SPEC.format,
    sampleRate:
      typeof track?.sampleRate === "string" && track.sampleRate.trim().length > 0
        ? track.sampleRate
        : typeof fallbackTechSpec?.sampleRate === "string" && fallbackTechSpec.sampleRate.trim().length > 0
          ? fallbackTechSpec.sampleRate
          : DEFAULT_TECH_SPEC.sampleRate,
    bitrate:
      typeof track?.bitrate === "string" && track.bitrate.trim().length > 0
        ? track.bitrate
        : typeof fallbackTechSpec?.bitrate === "string" && fallbackTechSpec.bitrate.trim().length > 0
          ? fallbackTechSpec.bitrate
          : DEFAULT_TECH_SPEC.bitrate,
    fileSize:
      typeof track?.fileSize === "string" && track.fileSize.trim().length > 0
        ? track.fileSize
        : `${estimatedMegabytes} MB`,
    isFavorite: Boolean(track?.isFavorite),
    audioUrl: track?.audioUrl || null,
  };
}

/**
 * Create an isolated Now Playing controller managing vinyl rotation inertia,
 * single/double-tap disambiguation, long-press seek mode, right-bezel volume arc,
 * swipe-skip transitions, Quick Actions, Playlist Picker, Track Details,
 * Context Queue sheet, and Live Lyrics cue transitions.
 *
 * @param {object} [callbacks] Host integration callbacks.
 * @param {() => void} [callbacks.onBrowseLibrary] Invoked when idle Browse Library CTA activates.
 * @param {() => void} [callbacks.onTogglePlayPause] Invoked on double-tap play/pause.
 * @param {(targetSeconds: number) => void} [callbacks.onSeek] Invoked when scrubbing seeks track time.
 * @param {(nextVolume: number) => void} [callbacks.onVolumeChange] Invoked when bezel or wheel changes volume.
 * @param {(direction: number) => void} [callbacks.onSkipTrack] Invoked on track skip (+1 or -1).
 * @param {(trackId: string, nextState: boolean) => void} [callbacks.onToggleFavorite] Invoked on favorite toggle.
 * @param {() => boolean | void} [callbacks.onToggleShuffle] Invoked when Shuffle is toggled.
 * @param {() => string | void} [callbacks.onCycleRepeatMode] Invoked when Repeat mode cycles.
 * @param {(rowIndex: number) => void} [callbacks.onSelectQueueIndex] Invoked when tapping a queue row.
 * @param {(updatedPlaylists: Array<object>) => void} [callbacks.onPersistPlaylists] Invoked when a playlist changes.
 * @param {(returnScreen: string) => void} [callbacks.onExit] Invoked when exiting bare Now Playing.
 * @returns {object} Now Playing controller API.
 */
export function createNowPlayingController({
  onBrowseLibrary,
  onTogglePlayPause,
  onSeek,
  onVolumeChange,
  onSkipTrack,
  onToggleFavorite,
  onToggleShuffle,
  onCycleRepeatMode,
  onSelectQueueIndex,
  onPersistPlaylists,
  onExit,
} = {}) {
  let albumByIdMap = new Map();
  let albumTechSpecsById = new Map();
  let trackByIdMap = new Map();
  let playlists = [];
  let lyricsCatalog = {};

  let operatingMode = "idle";
  let returnScreen = "home";
  let launchOrigin = "home";
  let activeTrack = null;
  let activeAlbum = null;
  let previousTrack = null;
  let previousAlbum = null;
  let queueTracks = [];
  let baseTrackCount = 0;
  let queueIndex = 0;
  let queueSource = "album";
  let queueAlbumId = null;
  let queueArtistName = null;
  let queueArtistAlbumIds = [];
  let queuePlaylistId = null;
  let queueGenreTitle = null;
  let queueGenreAlbumIds = [];
  let ephemeralCurrentTrackId = null;
  let lastAudioEventSeq = 0;

  let isPlaying = false;
  let spinAngularVelocity = 0;
  let vinylRotationRadians = 0;
  let vinylNudgeRadians = 0;
  let vinylNudgeTarget = 0;
  let trackElapsedSeconds = 0;
  let trackDurationSeconds = 0;
  let idleClockSeconds = 0;

  let shuffleEnabled = false;
  let repeatMode = NOW_PLAYING_REPEAT_MODES[0].id;

  let volumeLevel = DEFAULT_VOLUME_LEVEL;
  let volumeHudAlpha = 0;
  let volumeHoldSeconds = 0;
  let isAdjustingVolume = false;

  let isSeeking = false;
  let seekHoldSeconds = 0;
  let seekHoldProgress = 0;
  let seekHoldTriggered = false;
  let seekExpandProgress = 0;
  let seekLastAngle = 0;

  let openTransitionProgress = 1;
  let openChromeHoldSeconds = 0;
  let openChromeHoldAlpha = 0;

  let skipTransitionProgress = 1;
  let skipDirection = 1;
  let skipDragOffsetX = 0;

  let quickActionsOpen = false;
  let quickActionsProgress = 0;
  let quickActionsIdleSeconds = 0;
  let pressedActionId = null;

  let playlistPickerOpen = false;
  let playlistPickerProgress = 0;
  let pressedPlaylistIndex = null;

  let detailsSheetOpen = false;
  let detailsSheetProgress = 0;

  let queueSheetOpen = false;
  let queueSheetProgress = 0;
  let queueScrollPosition = 0;
  let queueTargetScroll = 0;
  let pressedQueueRowIndex = null;
  let queuePointerStartScroll = 0;
  let queuePointerVelocityY = 0;

  let lyricsOpen = false;
  let lyricsProgress = 0;
  let lyricsMode = "auto";
  let lyricsActiveTrackKey = null;
  let lyricsActiveLineIndex = -2;
  let lyricsPreviousLineText = "";
  let lyricsLineTransitionProgress = 1;

  let playPausePulseAlpha = 0;
  let trackBannerTitle = "";
  let trackBannerMeta = "";
  let trackBannerPhase = "hidden";
  let trackBannerHoldSeconds = 0;
  let trackBannerAlpha = 0;

  let toastText = "";
  let toastHoldSeconds = 0;
  let toastAlpha = 0;

  let backSwipeProgress = 0;
  let pressedCta = false;

  let pendingSingleTapActive = false;
  let pendingSingleTapSeconds = 0;
  let pendingTrackChangeTransition = null;

  let pointerActive = false;
  let pointerMode = "none";
  let pointerStartX = 0;
  let pointerStartY = 0;
  let pointerLastX = 0;
  let pointerLastY = 0;
  let pointerStartDistFromCenter = 0;
  let pointerMovedDistance = 0;
  let pointerHoldSeconds = 0;
  let volumeLastAngle = 0;

  function clearPendingSingleTap() {
    pendingSingleTapActive = false;
    pendingSingleTapSeconds = 0;
  }

  function clearPointerState() {
    pointerActive = false;
    pointerMode = "none";
    pointerStartX = 0;
    pointerStartY = 0;
    pointerLastX = 0;
    pointerLastY = 0;
    pointerStartDistFromCenter = 0;
    pointerMovedDistance = 0;
    pointerHoldSeconds = 0;
    volumeLastAngle = 0;
    seekHoldSeconds = 0;
    seekHoldTriggered = false;
    isSeeking = false;
    isAdjustingVolume = false;
    pressedActionId = null;
    pressedPlaylistIndex = null;
    pressedQueueRowIndex = null;
    pressedCta = false;
  }

  function showToast(messageText) {
    toastText = String(messageText ?? "");
    toastHoldSeconds = TOAST_HOLD_SECONDS;
    toastAlpha = 1;
  }

  function resolveQueueContext() {
    let badge = "ALBUM";
    let title = activeAlbum?.title || activeTrack?.albumTitle || "Album";

    if (queueSource === "playlist") {
      badge = "PLAYLIST";
      const matchedPlaylist = playlists.find(
        (playlistEntry) => playlistEntry.id === queuePlaylistId,
      );
      title = matchedPlaylist ? matchedPlaylist.name : "Playlist";
    } else if (queueSource === "artist") {
      badge = "ARTIST";
      title = queueArtistName || activeTrack?.artist || "Artist";
    } else if (queueSource === "favorites") {
      badge = "FAVORITES";
      title = "Favorites";
    } else if (queueSource === "genre") {
      badge = "GENRE";
      title = queueGenreTitle || "Genre";
    } else if (queueSource === "all-songs") {
      badge = "ALL SONGS";
      title = "All Songs";
    }

    return {
      sourceId: queueSource,
      badge,
      title,
      tracks: queueTracks.slice(),
    };
  }

  function showTrackBanner(track, queueContext, activeIndex) {
    if (!track) {
      return;
    }
    toastAlpha = 0;
    toastHoldSeconds = 0;
    const durationLabel =
      typeof track.duration === "string" && track.duration.trim().length > 0
        ? track.duration.trim()
        : formatClockTime(trackDurationSeconds);
    trackBannerTitle = String(track.title || "");
    if (queueContext.sourceId === "album") {
      const positionText = String(activeIndex + 1).padStart(2, "0");
      const totalText = String(Math.max(1, queueContext.tracks.length)).padStart(2, "0");
      trackBannerMeta = `${positionText} / ${totalText} \u00b7 ${durationLabel}`;
    } else if (queueContext.sourceId === "artist") {
      const albumLabel = String(track.albumTitle || "UNKNOWN ALBUM").toUpperCase();
      trackBannerMeta = `${albumLabel} \u00b7 ${durationLabel}`;
    } else {
      const artistLabel = String(track.artist || "UNKNOWN ARTIST").toUpperCase();
      trackBannerMeta = `${artistLabel} \u00b7 ${durationLabel}`;
    }
    trackBannerPhase = "in";
    trackBannerHoldSeconds = BANNER_HOLD_SECONDS;
    trackBannerAlpha = 0;
  }

  function applyTrackChangePresentation(priorTrack, priorAlbum, nextTrack, direction) {
    skipDirection = direction >= 0 ? 1 : -1;
    skipDragOffsetX = 0;
    trackElapsedSeconds = 0;

    const albumCoverChanged = Boolean(
      priorTrack && nextTrack && priorTrack.albumId !== nextTrack.albumId,
    );
    if (albumCoverChanged) {
      previousTrack = priorTrack;
      previousAlbum = priorAlbum;
      skipTransitionProgress = 0;
      vinylNudgeTarget += skipDirection * VINYL_SKIP_CROSSFADE_NUDGE_RADIANS;
      trackBannerPhase = "hidden";
      trackBannerAlpha = 0;
      trackBannerHoldSeconds = 0;
      return;
    }

    previousTrack = null;
    previousAlbum = null;
    skipTransitionProgress = 1;
    vinylNudgeTarget += skipDirection * VINYL_SKIP_SAME_ALBUM_NUDGE_RADIANS;
    if (nextTrack) {
      showTrackBanner(nextTrack, resolveQueueContext(), queueIndex);
    }
  }

  function applyAudioSnapshot(audioState, { suppressTransition = false } = {}) {
    if (!audioState || typeof audioState !== "object") {
      return;
    }

    const priorTrack = activeTrack;
    const priorAlbum = activeAlbum;
    const priorIndex = queueIndex;

    if (typeof audioState.volumeLevel === "number" && Number.isFinite(audioState.volumeLevel)) {
      volumeLevel = clamp(audioState.volumeLevel, 0, 1);
    } else if (typeof audioState.volume === "number" && Number.isFinite(audioState.volume)) {
      volumeLevel = clamp(audioState.volume, 0, 1);
    }

    if (typeof audioState.shuffleEnabled === "boolean") {
      shuffleEnabled = audioState.shuffleEnabled;
    }

    if (typeof audioState.repeatMode === "string" && REPEAT_MODES.includes(audioState.repeatMode)) {
      repeatMode = audioState.repeatMode;
    }

    ephemeralCurrentTrackId =
      typeof audioState.ephemeralCurrentTrackId === "string" && audioState.ephemeralCurrentTrackId.length > 0
        ? audioState.ephemeralCurrentTrackId
        : null;

    let incomingEvent = null;
    if (
      audioState.lastEvent &&
      typeof audioState.lastEvent.seq === "number" &&
      audioState.lastEvent.seq > lastAudioEventSeq
    ) {
      lastAudioEventSeq = audioState.lastEvent.seq;
      incomingEvent = audioState.lastEvent;
      if (incomingEvent.message) {
        showToast(incomingEvent.message);
      }
    }

    if (!audioState.activeTrack) {
      operatingMode = "idle";
      activeTrack = null;
      activeAlbum = null;
      queueTracks = [];
      baseTrackCount = 0;
      queueIndex = 0;
      isPlaying = false;
      trackElapsedSeconds = 0;
      trackDurationSeconds = 0;
      pendingTrackChangeTransition = null;
      return;
    }

    operatingMode = "playing";
    const incomingTrack = audioState.activeTrack;
    const catalogTrack = trackByIdMap.get(incomingTrack.id) || {};
    activeTrack = { ...catalogTrack, ...incomingTrack };
    activeAlbum = audioState.activeAlbum
      ? { ...audioState.activeAlbum }
      : albumByIdMap.get(activeTrack.albumId) || null;

    if (Array.isArray(audioState.queueTracks) && audioState.queueTracks.length > 0) {
      queueTracks = audioState.queueTracks.map((trackEntry) => ({
        ...(trackByIdMap.get(trackEntry.id) || {}),
        ...trackEntry,
      }));
    } else {
      queueTracks = [activeTrack];
    }
    baseTrackCount =
      typeof audioState.baseTrackCount === "number" && Number.isFinite(audioState.baseTrackCount)
        ? Math.max(0, Math.trunc(audioState.baseTrackCount))
        : queueTracks.length;

    queueIndex =
      typeof audioState.queueIndex === "number" && Number.isFinite(audioState.queueIndex)
        ? clamp(Math.trunc(audioState.queueIndex), 0, Math.max(0, queueTracks.length - 1))
        : 0;
    queueSource =
      typeof audioState.queueSource === "string" && audioState.queueSource.length > 0
        ? audioState.queueSource
        : "album";
    queueAlbumId = audioState.queueAlbumId ?? activeTrack.albumId ?? null;
    queueArtistName = audioState.queueArtistName ?? null;
    queueArtistAlbumIds = Array.isArray(audioState.queueArtistAlbumIds)
      ? audioState.queueArtistAlbumIds.slice()
      : [];
    queuePlaylistId = audioState.queuePlaylistId ?? null;
    queueGenreTitle = audioState.queueGenreTitle ?? null;
    queueGenreAlbumIds = Array.isArray(audioState.queueGenreAlbumIds)
      ? audioState.queueGenreAlbumIds.slice()
      : [];

    isPlaying = Boolean(audioState.isPlaying);
    trackDurationSeconds =
      typeof audioState.durationSeconds === "number" &&
      Number.isFinite(audioState.durationSeconds) &&
      audioState.durationSeconds > 0
        ? audioState.durationSeconds
        : resolveSafeTrackDurationSeconds(activeTrack);
    if (typeof audioState.elapsedSeconds === "number" && Number.isFinite(audioState.elapsedSeconds)) {
      trackElapsedSeconds = clamp(audioState.elapsedSeconds, 0, trackDurationSeconds);
    }

    if (suppressTransition || incomingEvent?.type === "context-ended") {
      pendingTrackChangeTransition = null;
      if (incomingEvent?.type === "context-ended") {
        previousTrack = null;
        previousAlbum = null;
        skipTransitionProgress = 1;
        trackBannerPhase = "hidden";
        trackBannerAlpha = 0;
      }
      return;
    }

    if (pendingTrackChangeTransition) {
      const transitionContext = pendingTrackChangeTransition;
      pendingTrackChangeTransition = null;
      if (
        !transitionContext.skipSameTrack ||
        !transitionContext.priorTrack ||
        transitionContext.priorTrack.id !== activeTrack.id ||
        priorIndex !== queueIndex
      ) {
        applyTrackChangePresentation(
          transitionContext.priorTrack,
          transitionContext.priorAlbum,
          activeTrack,
          transitionContext.direction,
        );
      }
      return;
    }

    if (priorTrack && activeTrack && priorTrack.id !== activeTrack.id) {
      applyTrackChangePresentation(priorTrack, priorAlbum, activeTrack, skipDirection);
    }
  }

  function enterScreen(
    resolvedReturnScreen,
    resolvedLaunchOrigin,
    audioState,
    initialVinylRotationRadians,
    skipOpenTransition = false,
  ) {
    returnScreen = resolvedReturnScreen;
    launchOrigin = resolvedLaunchOrigin;
    clearPendingSingleTap();
    clearPointerState();

    quickActionsOpen = false;
    quickActionsProgress = 0;
    quickActionsIdleSeconds = 0;
    playlistPickerOpen = false;
    playlistPickerProgress = 0;
    detailsSheetOpen = false;
    detailsSheetProgress = 0;
    queueSheetOpen = false;
    queueSheetProgress = 0;
    lyricsOpen = false;
    lyricsProgress = 0;
    lyricsActiveTrackKey = null;
    lyricsActiveLineIndex = -2;
    lyricsPreviousLineText = "";
    lyricsLineTransitionProgress = 1;
    seekHoldProgress = 0;
    seekExpandProgress = 0;
    previousTrack = null;
    previousAlbum = null;
    skipTransitionProgress = 1;
    skipDragOffsetX = 0;
    trackBannerPhase = "hidden";
    trackBannerAlpha = 0;
    trackBannerHoldSeconds = 0;
    toastAlpha = 0;
    toastHoldSeconds = 0;
    backSwipeProgress = 0;

    if (audioState && typeof audioState === "object") {
      applyAudioSnapshot(audioState, { suppressTransition: true });
    }
    if (Number.isFinite(initialVinylRotationRadians)) {
      vinylRotationRadians = initialVinylRotationRadians;
    }

    if (operatingMode === "playing") {
      openTransitionProgress = skipOpenTransition ? 1 : 0;
      openChromeHoldSeconds = skipOpenTransition ? 0 : OPEN_CHROME_HOLD_SECONDS;
      openChromeHoldAlpha = 0;
      vinylNudgeRadians = 0;
      vinylNudgeTarget = 0;
      spinAngularVelocity = isPlaying ? VINYL_SPIN_RADIANS_PER_SECOND : 0;
    } else {
      openTransitionProgress = 1;
      openChromeHoldSeconds = 0;
      openChromeHoldAlpha = 0;
      vinylNudgeRadians = 0;
      vinylNudgeTarget = 0;
      spinAngularVelocity = 0;
    }
  }

  function openLyricsOverlay() {
    if (operatingMode !== "playing") {
      return;
    }
    clearPendingSingleTap();
    quickActionsOpen = false;
    playlistPickerOpen = false;
    detailsSheetOpen = false;
    queueSheetOpen = false;
    trackBannerPhase = "hidden";
    trackBannerAlpha = 0;
    lyricsOpen = true;
    lyricsActiveTrackKey = null;
    lyricsPreviousLineText = "";
    lyricsLineTransitionProgress = 1;
  }

  function openQueueSheet() {
    if (operatingMode !== "playing") {
      return;
    }
    clearPendingSingleTap();
    const maxScroll = Math.max(0, queueTracks.length - QUEUE_VISIBLE_TAIL_SLOTS);
    const activeQueueIndex =
      queueTracks.length > 0
        ? ((queueIndex % queueTracks.length) + queueTracks.length) % queueTracks.length
        : 0;
    const initialScroll = clamp(activeQueueIndex, 0, maxScroll);

    quickActionsOpen = false;
    playlistPickerOpen = false;
    detailsSheetOpen = false;
    queueSheetOpen = true;
    queueScrollPosition = initialScroll;
    queueTargetScroll = initialScroll;
    pressedQueueRowIndex = null;
  }

  function adjustVolume(nextVolume) {
    if (operatingMode !== "playing" || !Number.isFinite(nextVolume)) {
      return;
    }
    volumeLevel = clamp(nextVolume, 0, 1);
    volumeHudAlpha = 1;
    volumeHoldSeconds = NOW_PLAYING_CONSTANTS.VOLUME_HOLD_SECONDS;
    if (typeof onVolumeChange === "function") {
      onVolumeChange(volumeLevel);
    }
  }

  function togglePlayPauseState() {
    if (operatingMode !== "playing") {
      return;
    }
    const expectedNextIsPlaying = !isPlaying;
    if (typeof onTogglePlayPause === "function") {
      onTogglePlayPause();
    } else {
      isPlaying = expectedNextIsPlaying;
    }
    playPausePulseAlpha = 1;
    if (isPlaying) {
      vinylNudgeTarget += VINYL_PLAY_NUDGE_RADIANS;
    }
  }

  function selectQueueTrack(targetIndex) {
    if (queueTracks.length === 0 || !Number.isFinite(targetIndex)) {
      queueSheetOpen = false;
      return;
    }
    const safeTargetIndex = clamp(Math.trunc(targetIndex), 0, queueTracks.length - 1);
    const priorTrack = activeTrack;
    const priorAlbum = activeAlbum;
    const priorIndex = queueIndex;
    const direction = safeTargetIndex >= priorIndex ? 1 : -1;

    queueSheetOpen = false;
    pendingTrackChangeTransition = {
      direction,
      priorTrack,
      priorAlbum,
      skipSameTrack: true,
    };

    if (typeof onSelectQueueIndex === "function") {
      onSelectQueueIndex(safeTargetIndex);
    }

    if (pendingTrackChangeTransition !== null) {
      pendingTrackChangeTransition = null;
      if (safeTargetIndex !== priorIndex) {
        queueIndex = safeTargetIndex;
        activeTrack = queueTracks[safeTargetIndex];
        activeAlbum = albumByIdMap.get(activeTrack?.albumId) || null;
        isPlaying = true;
        trackDurationSeconds = resolveSafeTrackDurationSeconds(activeTrack);
        applyTrackChangePresentation(priorTrack, priorAlbum, activeTrack, direction);
        vinylNudgeTarget += direction * VINYL_QUEUE_SELECT_NUDGE_RADIANS;
      } else {
        isPlaying = true;
      }
    }
  }

  /**
   * Hydrate albums, tracks, playlists, and lyrics catalog entries without mutating caller inputs.
   *
   * @param {object | null} nextCatalog Catalog payload.
   */
  function hydrateCatalog(nextCatalog) {
    const sourceCatalog = nextCatalog && typeof nextCatalog === "object" ? nextCatalog : {};
    const catalogAlbums = Array.isArray(sourceCatalog.albums) ? sourceCatalog.albums : [];
    const catalogAllTracks = Array.isArray(sourceCatalog.allTracks) ? sourceCatalog.allTracks : [];

    const nextAlbumMap = new Map();
    const nextTrackMap = new Map();

    for (const albumEntry of catalogAlbums) {
      if (!albumEntry || typeof albumEntry.id !== "string") {
        continue;
      }
      nextAlbumMap.set(albumEntry.id, albumEntry);
      const albumTracks = Array.isArray(albumEntry.tracks) ? albumEntry.tracks : [];
      for (const trackEntry of albumTracks) {
        if (!trackEntry || typeof trackEntry.id !== "string") {
          continue;
        }
        nextTrackMap.set(trackEntry.id, {
          ...trackEntry,
          artist: trackEntry.artist || albumEntry.artist || "",
          albumId: trackEntry.albumId || albumEntry.id,
          albumTitle: trackEntry.albumTitle || albumEntry.title || "",
          genre: trackEntry.genre || albumEntry.genre || "",
          year: trackEntry.year ?? albumEntry.year,
        });
      }
    }

    for (const trackEntry of catalogAllTracks) {
      if (!trackEntry || typeof trackEntry.id !== "string") {
        continue;
      }
      const existingTrack = nextTrackMap.get(trackEntry.id);
      nextTrackMap.set(
        trackEntry.id,
        existingTrack ? { ...existingTrack, ...trackEntry } : { ...trackEntry },
      );
    }

    const nextAlbumTechSpecsMap = new Map();
    if (sourceCatalog.albumTechSpecs && typeof sourceCatalog.albumTechSpecs === "object") {
      for (const [albumId, techSpec] of Object.entries(sourceCatalog.albumTechSpecs)) {
        if (
          typeof albumId === "string" &&
          albumId.length > 0 &&
          techSpec &&
          typeof techSpec === "object"
        ) {
          nextAlbumTechSpecsMap.set(albumId, { ...techSpec });
        }
      }
    }

    albumByIdMap = nextAlbumMap;
    albumTechSpecsById = nextAlbumTechSpecsMap;
    trackByIdMap = nextTrackMap;
    playlists = clonePlaylists(sourceCatalog.playlists);
    lyricsCatalog =
      sourceCatalog.lyricsCatalog && typeof sourceCatalog.lyricsCatalog === "object"
        ? { ...sourceCatalog.lyricsCatalog }
        : {};
  }

  /**
   * Synchronize controller playback, queue, volume, and play-mode state from audio engine state.
   *
   * @param {object | null} audioState Snapshot from `audioEngine.getState()`.
   */
  function syncAudioState(audioState) {
    applyAudioSnapshot(audioState, { suppressTransition: false });
  }

  /**
   * Enter Now Playing from the Home app wheel with optional return screen and launch origin overrides.
   *
   * @param {object} [options] Entry payload.
   * @param {string} [options.returnScreen="home"] Screen identifier for bare back exits.
   * @param {string} [options.launchOrigin="home"] Launch origin identifier (`home` | `album-crate` | `library`).
   * @param {object | null} [options.audioState] Current audio engine snapshot.
   * @param {number} [options.vinylRotationRadians] Synchronized vinyl angle in radians.
   * @param {boolean} [options.skipOpenTransition=false] Skip secondary zoom when Home already zoomed in.
   */
  function enterFromHome({
    returnScreen: nextReturnScreen = "home",
    launchOrigin: nextLaunchOrigin = "home",
    audioState,
    vinylRotationRadians: nextVinylRotationRadians,
    skipOpenTransition = false,
  } = {}) {
    const resolvedReturn =
      typeof nextReturnScreen === "string" && nextReturnScreen.length > 0
        ? nextReturnScreen
        : "home";
    const resolvedOrigin =
      nextLaunchOrigin === "album-crate" || nextLaunchOrigin === "library"
        ? nextLaunchOrigin
        : "home";
    enterScreen(
      resolvedReturn,
      resolvedOrigin,
      audioState,
      nextVinylRotationRadians,
      Boolean(skipOpenTransition),
    );
  }

  /**
   * Enter Now Playing from Library vinyl launch with optional return screen and launch origin overrides.
   *
   * @param {object} [options] Entry payload.
   * @param {string} [options.returnScreen="library"] Screen identifier for bare back exits.
   * @param {string} [options.launchOrigin="library"] Launch origin identifier (`home` | `album-crate` | `library`).
   * @param {object | null} [options.audioState] Current audio engine snapshot.
   * @param {number} [options.vinylRotationRadians] Synchronized vinyl angle in radians.
   */
  function enterFromLibrary({
    returnScreen: nextReturnScreen = "library",
    launchOrigin: nextLaunchOrigin = "library",
    audioState,
    vinylRotationRadians: nextVinylRotationRadians,
  } = {}) {
    const resolvedReturn =
      typeof nextReturnScreen === "string" && nextReturnScreen.length > 0
        ? nextReturnScreen
        : "library";
    const resolvedOrigin =
      nextLaunchOrigin === "album-crate" || nextLaunchOrigin === "home"
        ? nextLaunchOrigin
        : "library";
    enterScreen(resolvedReturn, resolvedOrigin, audioState, nextVinylRotationRadians);
  }

  /**
   * Skip forward or backward across the queue and initiate same-cover banner or vinyl crossfade.
   *
   * @param {number} direction Signed skip direction (+1 or -1).
   */
  function skipTrack(direction) {
    if (
      operatingMode !== "playing" ||
      typeof direction !== "number" ||
      !Number.isFinite(direction) ||
      direction === 0
    ) {
      return;
    }
    const stepDirection = direction >= 0 ? 1 : -1;
    const priorTrack = activeTrack;
    const priorAlbum = activeAlbum;

    pendingTrackChangeTransition = {
      direction: stepDirection,
      priorTrack,
      priorAlbum,
      skipSameTrack: false,
    };

    if (typeof onSkipTrack === "function") {
      const hostOutcome = onSkipTrack(stepDirection);
      if (hostOutcome && hostOutcome.action === "boundary-bounce") {
        pendingTrackChangeTransition = null;
        skipDragOffsetX = 0;
        previousTrack = null;
        previousAlbum = null;
        skipTransitionProgress = 1;
        vinylNudgeTarget += -stepDirection * VINYL_SKIP_SAME_ALBUM_NUDGE_RADIANS;
        showToast(`${hostOutcome.boundary === "end" ? "END OF " : "START OF "}${hostOutcome.contextBadge}`);
        return;
      }
      if (hostOutcome && hostOutcome.action === "restart-current") {
        pendingTrackChangeTransition = null;
        skipDragOffsetX = 0;
        trackElapsedSeconds = 0;
        previousTrack = null;
        previousAlbum = null;
        skipTransitionProgress = 1;
        vinylNudgeTarget += stepDirection * VINYL_SKIP_SAME_ALBUM_NUDGE_RADIANS;
        return;
      }
      if (hostOutcome && hostOutcome.action === "idle") {
        pendingTrackChangeTransition = null;
        return;
      }
    }

    if (pendingTrackChangeTransition !== null && queueTracks.length > 0) {
      pendingTrackChangeTransition = null;
      const outcome = resolveSkipOutcome(
        {
          queueTracks,
          queueIndex,
          elapsedSeconds: trackElapsedSeconds,
          repeatMode,
          shuffleEnabled,
          queueSource,
          ephemeralCurrentTrackId,
          baseTrackCount,
        },
        stepDirection,
      );
      if (outcome.action === "boundary-bounce") {
        skipDragOffsetX = 0;
        previousTrack = null;
        previousAlbum = null;
        skipTransitionProgress = 1;
        vinylNudgeTarget += -stepDirection * VINYL_SKIP_SAME_ALBUM_NUDGE_RADIANS;
        showToast(`${outcome.boundary === "end" ? "END OF " : "START OF "}${outcome.contextBadge}`);
        return;
      }
      if (outcome.action === "restart-current") {
        skipDragOffsetX = 0;
        trackElapsedSeconds = 0;
        previousTrack = null;
        previousAlbum = null;
        skipTransitionProgress = 1;
        vinylNudgeTarget += stepDirection * VINYL_SKIP_SAME_ALBUM_NUDGE_RADIANS;
        return;
      }
      if (outcome.action === "jump") {
        queueIndex = outcome.targetIndex;
        activeTrack = queueTracks[outcome.targetIndex];
        activeAlbum = albumByIdMap.get(activeTrack?.albumId) || null;
        trackDurationSeconds = resolveSafeTrackDurationSeconds(activeTrack);
        applyTrackChangePresentation(priorTrack, priorAlbum, activeTrack, stepDirection);
      }
    } else {
      pendingTrackChangeTransition = null;
    }
  }

  /**
   * Toggle Shuffle ON or OFF cleanly via host callback or local state.
   */
  function toggleShuffle() {
    if (typeof onToggleShuffle === "function") {
      onToggleShuffle();
    } else {
      shuffleEnabled = !shuffleEnabled;
      showToast(shuffleEnabled ? "SHUFFLE // ON" : "SHUFFLE // OFF");
    }
    quickActionsIdleSeconds = 0;
  }

  /**
   * Cycle Repeat mode (`off` -> `all` -> `one` -> `off`) cleanly via host callback or local state.
   */
  function cycleRepeatMode() {
    if (typeof onCycleRepeatMode === "function") {
      onCycleRepeatMode();
    } else {
      const currentIndex = REPEAT_MODES.indexOf(repeatMode);
      const nextIndex = currentIndex >= 0 ? (currentIndex + 1) % REPEAT_MODES.length : 0;
      repeatMode = REPEAT_MODES[nextIndex];
      showToast(`REPEAT // ${repeatMode.toUpperCase()}`);
    }
    quickActionsIdleSeconds = 0;
  }

  /**
   * Toggle playback between playing and paused when a track is loaded.
   */
  function togglePlayPause() {
    togglePlayPauseState();
  }

  /**
   * Close Quick Actions and modal sheets, then toggle the Live Lyrics overlay.
   */
  function toggleLyrics() {
    if (operatingMode !== "playing") {
      return;
    }
    clearPendingSingleTap();
    quickActionsOpen = false;
    playlistPickerOpen = false;
    detailsSheetOpen = false;
    queueSheetOpen = false;
    if (lyricsOpen) {
      lyricsOpen = false;
    } else {
      openLyricsOverlay();
    }
  }

  /**
   * Close Quick Actions, queue sheet, and playlist picker, then toggle Track Details.
   */
  function toggleTrackDetails() {
    if (operatingMode !== "playing") {
      return;
    }
    clearPendingSingleTap();
    quickActionsOpen = false;
    queueSheetOpen = false;
    playlistPickerOpen = false;
    detailsSheetOpen = !detailsSheetOpen;
  }

  /**
   * Toggle the Context Queue sheet open or closed.
   */
  function toggleQueueSheet() {
    if (operatingMode !== "playing") {
      return;
    }
    clearPendingSingleTap();
    if (queueSheetOpen) {
      queueSheetOpen = false;
    } else {
      openQueueSheet();
    }
  }

  /**
   * Replace internal cloned playlists when playlists change externally.
   *
   * @param {Array<object> | null} nextPlaylists Updated playlist list.
   */
  function setPlaylists(nextPlaylists) {
    playlists = clonePlaylists(nextPlaylists);
  }

  /**
   * Synchronize favorite flag for `trackId` across `activeTrack`, `queueTracks`, and cached tracks.
   *
   * @param {string} trackId Track identifier to update.
   * @param {boolean} isFavorite Updated favorite state.
   */
  function updateTrackFavorite(trackId, isFavorite) {
    if (typeof trackId !== "string" || trackId.length === 0) {
      return;
    }
    const nextFavorite = Boolean(isFavorite);
    if (activeTrack && activeTrack.id === trackId) {
      activeTrack = { ...activeTrack, isFavorite: nextFavorite };
    }
    queueTracks = queueTracks.map((trackEntry) =>
      trackEntry.id === trackId ? { ...trackEntry, isFavorite: nextFavorite } : trackEntry,
    );
    const cachedTrack = trackByIdMap.get(trackId);
    if (cachedTrack) {
      trackByIdMap.set(trackId, { ...cachedTrack, isFavorite: nextFavorite });
    }
  }

  /**
   * Cycle Lyrics overlay display mode (`auto` -> `timed` -> `untimed` -> `none`).
   */
  function cycleLyricsMode() {
    if (operatingMode !== "playing") {
      return;
    }
    const currentIndex = NOW_PLAYING_LYRICS_MODES.indexOf(lyricsMode);
    const nextIndex =
      currentIndex >= 0 ? (currentIndex + 1) % NOW_PLAYING_LYRICS_MODES.length : 0;
    const nextMode = NOW_PLAYING_LYRICS_MODES[nextIndex];
    lyricsMode = nextMode;
    openLyricsOverlay();
    showToast(`LYRICS // ${LYRICS_MODE_BADGE_LABELS[nextMode] || "TRACK"}`);
  }

  /**
   * Activate the focused idle Browse Library CTA or toggle Quick Actions in playing mode.
   */
  function activateFocused() {
    if (operatingMode !== "playing") {
      if (typeof onBrowseLibrary === "function") {
        onBrowseLibrary();
      }
      return;
    }
    if (!playlistPickerOpen && !detailsSheetOpen && !queueSheetOpen) {
      quickActionsOpen = !quickActionsOpen;
      quickActionsIdleSeconds = 0;
    }
  }

  /**
   * Close overlays in strict priority order, or exit Now Playing via `onExit(returnScreen)`.
   */
  function stepBack() {
    clearPendingSingleTap();
    backSwipeProgress = 0;

    if (playlistPickerOpen) {
      playlistPickerOpen = false;
      return;
    }
    if (detailsSheetOpen) {
      detailsSheetOpen = false;
      return;
    }
    if (queueSheetOpen) {
      queueSheetOpen = false;
      return;
    }
    if (quickActionsOpen) {
      quickActionsOpen = false;
      return;
    }
    if (lyricsOpen) {
      lyricsOpen = false;
      return;
    }
    if (typeof onExit === "function") {
      onExit(returnScreen);
    }
  }

  /**
   * Advance Now Playing timers, rotational inertia, transitions, seek hold detection,
   * lyrics cue transitions, queue snapping, and deterministic double-tap window by `deltaSeconds`.
   *
   * @param {number} deltaSeconds Elapsed frame duration in seconds.
   */
  function step(deltaSeconds) {
    if (typeof deltaSeconds !== "number" || !Number.isFinite(deltaSeconds) || deltaSeconds <= 0) {
      return;
    }

    idleClockSeconds += deltaSeconds;

    const targetSpinVelocity =
      operatingMode === "playing" && isPlaying && !isSeeking ? VINYL_SPIN_RADIANS_PER_SECOND : 0;
    const inertiaRate =
      targetSpinVelocity > spinAngularVelocity ? VINYL_SPIN_ATTACK_RATE : VINYL_SPIN_COAST_RATE;
    spinAngularVelocity = lerp(
      spinAngularVelocity,
      targetSpinVelocity,
      1 - Math.exp(-inertiaRate * deltaSeconds),
    );
    if (Math.abs(spinAngularVelocity) < VINYL_SPIN_ZERO_THRESHOLD) {
      spinAngularVelocity = 0;
    }
    vinylRotationRadians += spinAngularVelocity * deltaSeconds;

    if (operatingMode === "playing" && isPlaying && !isSeeking && trackDurationSeconds > 0) {
      trackElapsedSeconds = clamp(trackElapsedSeconds + deltaSeconds, 0, trackDurationSeconds);
    }

    const nudgeFollow = 1 - Math.exp(-VINYL_NUDGE_SPRING_RATE * deltaSeconds);
    vinylNudgeRadians = lerp(vinylNudgeRadians, vinylNudgeTarget, nudgeFollow);

    if (openTransitionProgress < 1) {
      openTransitionProgress = clamp(
        openTransitionProgress + deltaSeconds / NOW_PLAYING_CONSTANTS.OPEN_DURATION_SECONDS,
        0,
        1,
      );
    }
    if (openChromeHoldSeconds > 0) {
      const waveTwoTarget = openTransitionProgress > OPEN_CHROME_APPEAR_PROGRESS ? 1 : 0;
      openChromeHoldAlpha = lerp(
        openChromeHoldAlpha,
        waveTwoTarget,
        1 - Math.exp(-OPEN_CHROME_ATTACK_RATE * deltaSeconds),
      );
      if (openTransitionProgress >= OPEN_CHROME_SETTLE_PROGRESS) {
        openChromeHoldSeconds = Math.max(0, openChromeHoldSeconds - deltaSeconds);
      }
    } else if (openChromeHoldAlpha > 0) {
      openChromeHoldAlpha = Math.max(0, openChromeHoldAlpha - deltaSeconds * OPEN_CHROME_DECAY_RATE);
    }

    if (skipTransitionProgress < 1) {
      skipTransitionProgress = clamp(
        skipTransitionProgress + deltaSeconds / NOW_PLAYING_CONSTANTS.SKIP_DURATION_SECONDS,
        0,
        1,
      );
      if (skipTransitionProgress >= 1) {
        previousTrack = null;
        previousAlbum = null;
      }
    }
    if (!pointerActive && Math.abs(skipDragOffsetX) > SKIP_DRAG_MIN_OFFSET_PX) {
      skipDragOffsetX = lerp(
        skipDragOffsetX,
        0,
        1 - Math.exp(-SKIP_DRAG_SETTLE_RATE * deltaSeconds),
      );
      if (Math.abs(skipDragOffsetX) < SKIP_DRAG_SNAP_ZERO_PX) {
        skipDragOffsetX = 0;
      }
    }

    if (pointerActive) {
      pointerHoldSeconds += deltaSeconds;
    }

    if (
      pointerActive &&
      pointerMode === "vinyl" &&
      operatingMode === "playing" &&
      pointerStartDistFromCenter <= NOW_PLAYING_CONSTANTS.SEEK_HOLD_RADIUS_PX &&
      pointerMovedDistance < SEEK_HOLD_MAX_DRIFT_PX &&
      !seekHoldTriggered
    ) {
      seekHoldSeconds += deltaSeconds;
      seekHoldProgress = normalizeRange(
        seekHoldSeconds,
        SEEK_HOLD_DELAY_SECONDS,
        NOW_PLAYING_CONSTANTS.SEEK_HOLD_DURATION_SECONDS,
      );
      if (seekHoldSeconds >= NOW_PLAYING_CONSTANTS.SEEK_HOLD_DURATION_SECONDS) {
        seekHoldTriggered = true;
        seekHoldProgress = 0;
        pointerMode = "seek";
        isSeeking = true;
        seekLastAngle = Math.atan2(
          pointerLastY - DISPLAY_CENTER_PX,
          pointerLastX - DISPLAY_CENTER_PX,
        );
      }
    } else if (seekHoldProgress > 0) {
      seekHoldProgress = Math.max(0, seekHoldProgress - deltaSeconds * SEEK_HOLD_DECAY_RATE);
    }

    const seekTarget = isSeeking ? 1 : 0;
    const seekRate = seekTarget > 0 ? SEEK_EXPAND_ATTACK_RATE : SEEK_EXPAND_RELEASE_RATE;
    seekExpandProgress = lerp(
      seekExpandProgress,
      seekTarget,
      1 - Math.exp(-seekRate * deltaSeconds),
    );
    if (seekTarget === 0 && seekExpandProgress < SEEK_EXPAND_ZERO_THRESHOLD) {
      seekExpandProgress = 0;
    }

    if (isAdjustingVolume) {
      volumeHudAlpha = lerp(
        volumeHudAlpha,
        1,
        1 - Math.exp(-VOLUME_HUD_ATTACK_RATE * deltaSeconds),
      );
      volumeHoldSeconds = NOW_PLAYING_CONSTANTS.VOLUME_HOLD_SECONDS;
    } else if (volumeHoldSeconds > 0) {
      const consumedHoldSeconds = Math.min(volumeHoldSeconds, deltaSeconds);
      volumeHoldSeconds -= consumedHoldSeconds;
      const remainingFadeSeconds = deltaSeconds - consumedHoldSeconds;
      if (remainingFadeSeconds > 0 && volumeHudAlpha > 0) {
        volumeHudAlpha = Math.max(0, volumeHudAlpha - remainingFadeSeconds * VOLUME_HUD_DECAY_RATE);
      }
    } else if (volumeHudAlpha > 0) {
      volumeHudAlpha = Math.max(0, volumeHudAlpha - deltaSeconds * VOLUME_HUD_DECAY_RATE);
    }

    if (
      quickActionsOpen &&
      !playlistPickerOpen &&
      !detailsSheetOpen &&
      !pointerActive
    ) {
      quickActionsIdleSeconds += deltaSeconds;
      if (quickActionsIdleSeconds >= NOW_PLAYING_CONSTANTS.QUICK_ACTIONS_TIMEOUT_SECONDS) {
        quickActionsOpen = false;
      }
    }
    const actionsTarget = quickActionsOpen ? 1 : 0;
    quickActionsProgress = lerp(
      quickActionsProgress,
      actionsTarget,
      1 - Math.exp(-QUICK_ACTIONS_SPRING_RATE * deltaSeconds),
    );
    if (Math.abs(quickActionsProgress - actionsTarget) < SHEET_SETTLE_EPSILON) {
      quickActionsProgress = actionsTarget;
    }

    if (pendingSingleTapActive) {
      pendingSingleTapSeconds += deltaSeconds;
      if (pendingSingleTapSeconds >= NOW_PLAYING_CONSTANTS.DOUBLE_TAP_WINDOW_SECONDS) {
        clearPendingSingleTap();
        if (
          operatingMode === "playing" &&
          !detailsSheetOpen &&
          !queueSheetOpen &&
          !playlistPickerOpen
        ) {
          quickActionsOpen = true;
          quickActionsIdleSeconds = 0;
        }
      }
    }

    const pickerTarget = playlistPickerOpen ? 1 : 0;
    playlistPickerProgress = lerp(
      playlistPickerProgress,
      pickerTarget,
      1 - Math.exp(-PICKER_SPRING_RATE * deltaSeconds),
    );
    if (Math.abs(playlistPickerProgress - pickerTarget) < SHEET_SETTLE_EPSILON) {
      playlistPickerProgress = pickerTarget;
    }

    const detailsTarget = detailsSheetOpen ? 1 : 0;
    detailsSheetProgress = lerp(
      detailsSheetProgress,
      detailsTarget,
      1 - Math.exp(-DETAILS_SPRING_RATE * deltaSeconds),
    );
    if (Math.abs(detailsSheetProgress - detailsTarget) < SHEET_SETTLE_EPSILON) {
      detailsSheetProgress = detailsTarget;
    }

    const queueTarget = queueSheetOpen ? 1 : 0;
    queueSheetProgress = lerp(
      queueSheetProgress,
      queueTarget,
      1 - Math.exp(-QUEUE_SHEET_SPRING_RATE * deltaSeconds),
    );
    if (Math.abs(queueSheetProgress - queueTarget) < SHEET_SETTLE_EPSILON) {
      queueSheetProgress = queueTarget;
    }

    const lyricsTarget = operatingMode === "playing" && lyricsOpen ? 1 : 0;
    lyricsProgress = lerp(
      lyricsProgress,
      lyricsTarget,
      1 - Math.exp(-LYRICS_SPRING_RATE * deltaSeconds),
    );
    if (Math.abs(lyricsProgress - lyricsTarget) < LYRICS_SETTLE_EPSILON) {
      lyricsProgress = lyricsTarget;
    }

    if ((lyricsOpen || lyricsProgress > LYRICS_ACTIVE_VISIBILITY_THRESHOLD) && activeTrack) {
      const lyricsEntry = lyricsCatalog[activeTrack.id] || null;
      const activeLyricsFrame = resolveActiveLyricsFrame(
        activeTrack,
        lyricsEntry,
        trackElapsedSeconds,
        lyricsMode,
      );
      const trackLyricsKey = `${activeTrack.id}|${lyricsMode}`;
      if (lyricsActiveTrackKey !== trackLyricsKey) {
        lyricsActiveTrackKey = trackLyricsKey;
        lyricsActiveLineIndex = activeLyricsFrame.activeIndex;
        lyricsPreviousLineText = "";
        lyricsLineTransitionProgress = 1;
      } else if (activeLyricsFrame.activeIndex !== lyricsActiveLineIndex) {
        const lyricsSource = resolveTrackLyricsSource(activeTrack, lyricsEntry, lyricsMode);
        const previousIndex = lyricsActiveLineIndex;
        if (previousIndex >= 0 && previousIndex < lyricsSource.lines.length) {
          const previousEntry = lyricsSource.lines[previousIndex];
          lyricsPreviousLineText =
            typeof previousEntry === "string" ? previousEntry : String(previousEntry?.text ?? "");
        } else {
          lyricsPreviousLineText = "";
        }
        lyricsActiveLineIndex = activeLyricsFrame.activeIndex;
        lyricsLineTransitionProgress = 0;
      }
      if (lyricsLineTransitionProgress < 1) {
        lyricsLineTransitionProgress = clamp(
          lyricsLineTransitionProgress +
            deltaSeconds / NOW_PLAYING_CONSTANTS.LYRICS_TRANSITION_DURATION_SECONDS,
          0,
          1,
        );
      }
    }

    if (queueSheetOpen && (!pointerActive || pointerMode !== "queue-list")) {
      const maxQueueScroll = Math.max(0, queueTracks.length - QUEUE_VISIBLE_TAIL_SLOTS);
      queueTargetScroll = clamp(queueTargetScroll, 0, maxQueueScroll);
      queueScrollPosition = lerp(
        queueScrollPosition,
        queueTargetScroll,
        1 - Math.exp(-QUEUE_SNAP_SPRING_RATE * deltaSeconds),
      );
      if (Math.abs(queueScrollPosition - queueTargetScroll) < QUEUE_SNAP_SETTLE_EPSILON) {
        queueScrollPosition = queueTargetScroll;
      }
    }

    if (playPausePulseAlpha > 0) {
      playPausePulseAlpha = Math.max(0, playPausePulseAlpha - deltaSeconds * PULSE_DECAY_RATE);
    }

    if (trackBannerPhase === "in") {
      trackBannerAlpha = Math.min(1, trackBannerAlpha + deltaSeconds / BANNER_FADE_IN_SECONDS);
      if (trackBannerAlpha >= 1) {
        trackBannerPhase = "hold";
      }
    } else if (trackBannerPhase === "hold") {
      trackBannerHoldSeconds = Math.max(0, trackBannerHoldSeconds - deltaSeconds);
      if (trackBannerHoldSeconds <= 0) {
        trackBannerPhase = "out";
      }
    } else if (trackBannerPhase === "out") {
      trackBannerAlpha = Math.max(0, trackBannerAlpha - deltaSeconds / BANNER_FADE_OUT_SECONDS);
      if (trackBannerAlpha <= 0) {
        trackBannerPhase = "hidden";
      }
    }

    if (toastHoldSeconds > 0) {
      const consumedToastHold = Math.min(toastHoldSeconds, deltaSeconds);
      toastHoldSeconds -= consumedToastHold;
      const remainingToastFade = deltaSeconds - consumedToastHold;
      if (remainingToastFade > 0 && toastAlpha > 0) {
        toastAlpha = Math.max(0, toastAlpha - remainingToastFade * TOAST_DECAY_RATE);
      }
    } else if (toastAlpha > 0) {
      toastAlpha = Math.max(0, toastAlpha - deltaSeconds * TOAST_DECAY_RATE);
    }

    if (!pointerActive) {
      backSwipeProgress = Math.max(0, backSwipeProgress - deltaSeconds * BACK_SWIPE_DECAY_RATE);
    }
  }

  /**
   * Begin pointer tracking for vinyl tap/double-tap, seek hold, volume bezel arc,
   * skip drag, queue sheet pull, Quick Actions, or overlay interactions.
   *
   * @param {number} pointX Display coordinate X in [0, 360].
   * @param {number} pointY Display coordinate Y in [0, 360].
   */
  function handlePointerDown(pointX, pointY) {
    if (!Number.isFinite(pointX) || !Number.isFinite(pointY)) {
      return;
    }
    const distanceFromCenter = Math.hypot(
      pointX - DISPLAY_CENTER_PX,
      pointY - DISPLAY_CENTER_PX,
    );
    const startAngleFromCenter = Math.atan2(
      pointY - DISPLAY_CENTER_PX,
      pointX - DISPLAY_CENTER_PX,
    );

    pointerActive = true;
    pointerStartX = pointX;
    pointerStartY = pointY;
    pointerLastX = pointX;
    pointerLastY = pointY;
    pointerStartDistFromCenter = distanceFromCenter;
    pointerMovedDistance = 0;
    pointerHoldSeconds = 0;
    volumeLastAngle = startAngleFromCenter;
    seekHoldSeconds = 0;
    seekHoldProgress = 0;
    seekHoldTriggered = false;
    pressedActionId = null;
    pressedPlaylistIndex = null;
    pressedQueueRowIndex = null;
    pressedCta = false;

    if (operatingMode !== "playing") {
      const hitCta =
        pointX >= IDLE_CTA_LEFT_X_PX &&
        pointX <= IDLE_CTA_RIGHT_X_PX &&
        pointY >= IDLE_CTA_TOP_Y_PX &&
        pointY <= IDLE_CTA_BOTTOM_Y_PX;
      if (pointX <= LEFT_EDGE_SWIPE_MAX_X_PX) {
        pointerMode = "back-swipe";
      } else if (hitCta) {
        pressedCta = true;
        pointerMode = "idle-cta";
      } else {
        pointerMode = "idle-bg";
      }
      return;
    }

    if (playlistPickerOpen) {
      if (pointX <= LEFT_EDGE_SWIPE_MAX_X_PX) {
        pointerMode = "back-swipe";
      } else {
        pointerMode = "picker";
        const visibleCount = Math.min(PICKER_MAX_VISIBLE_ROWS, playlists.length);
        for (let playlistIndex = 0; playlistIndex < visibleCount; playlistIndex += 1) {
          const rowTop = PICKER_ROW_START_Y_PX + playlistIndex * PICKER_ROW_PITCH_PX;
          if (
            pointX >= PICKER_ROW_LEFT_X_PX &&
            pointX <= PICKER_ROW_RIGHT_X_PX &&
            pointY >= rowTop &&
            pointY <= rowTop + PICKER_ROW_HEIGHT_PX
          ) {
            pressedPlaylistIndex = playlistIndex;
            break;
          }
        }
      }
      return;
    }

    if (detailsSheetOpen) {
      pointerMode = pointX <= LEFT_EDGE_SWIPE_MAX_X_PX ? "back-swipe" : "details";
      return;
    }

    if (queueSheetOpen) {
      if (pointX <= LEFT_EDGE_SWIPE_MAX_X_PX) {
        pointerMode = "back-swipe";
      } else if (pointY <= QUEUE_HEADER_MAX_Y_PX) {
        pointerMode = "queue-header";
      } else {
        pointerMode = "queue-list";
        queuePointerStartScroll = queueScrollPosition;
        queuePointerVelocityY = 0;
        pressedQueueRowIndex = hitTestNowPlayingQueueRow(
          pointX,
          pointY,
          queueScrollPosition,
          queueTracks,
        );
      }
      return;
    }

    if (quickActionsOpen) {
      quickActionsIdleSeconds = 0;
      if (pointX <= LEFT_EDGE_SWIPE_MAX_X_PX) {
        pointerMode = "left-edge-actions";
      } else {
        pointerMode = "quick-actions";
        for (const buttonSpec of NOW_PLAYING_QUICK_BUTTONS) {
          const hitDistance = Math.hypot(
            pointX - buttonSpec.centerX,
            pointY - buttonSpec.centerY,
          );
          if (hitDistance <= buttonSpec.radius + QUICK_ACTION_HIT_PAD_PX) {
            pressedActionId = buttonSpec.id;
            break;
          }
        }
      }
      return;
    }

    if (
      distanceFromCenter >= NOW_PLAYING_CONSTANTS.VOLUME_START_MIN_RADIUS_PX &&
      Math.abs(startAngleFromCenter) <= NOW_PLAYING_CONSTANTS.VOLUME_START_MAX_ANGLE_RADIANS
    ) {
      pointerMode = "volume-pending";
    } else if (pointX <= LEFT_EDGE_SWIPE_MAX_X_PX) {
      pointerMode = "left-edge-pending";
    } else {
      pointerMode = "vinyl";
    }
  }

  /**
   * Update active seek scrub, bezel volume drag, skip slide, queue drag, or back-swipe
   * during pointer movement.
   *
   * @param {number} pointX Display coordinate X in [0, 360].
   * @param {number} pointY Display coordinate Y in [0, 360].
   */
  function handlePointerMove(pointX, pointY) {
    if (!pointerActive || !Number.isFinite(pointX) || !Number.isFinite(pointY)) {
      return;
    }

    const deltaX = pointX - pointerStartX;
    const deltaY = pointY - pointerStartY;
    const stepX = pointX - pointerLastX;
    const stepY = pointY - pointerLastY;
    pointerMovedDistance = Math.max(pointerMovedDistance, Math.hypot(deltaX, deltaY));
    pointerLastX = pointX;
    pointerLastY = pointY;

    if (pointerMode === "back-swipe") {
      backSwipeProgress =
        deltaX > 0 ? clamp(deltaX / NOW_PLAYING_CONSTANTS.BACK_SWIPE_DISTANCE_PX, 0, 1) : 0;
      return;
    }

    if (pointerMode === "left-edge-actions") {
      if (
        deltaX > LEFT_EDGE_ACTIONS_MIN_DELTA_X_PX &&
        deltaX > Math.abs(deltaY) * LEFT_EDGE_ACTIONS_SLOPE_RATIO
      ) {
        pointerMode = "back-swipe";
        backSwipeProgress = clamp(
          deltaX / NOW_PLAYING_CONSTANTS.BACK_SWIPE_DISTANCE_PX,
          0,
          1,
        );
      }
      return;
    }

    if (pointerMode === "left-edge-pending") {
      if (pointerMovedDistance < NOW_PLAYING_CONSTANTS.TAP_DRIFT_LIMIT_PX) {
        return;
      }
      if (
        deltaX > LEFT_EDGE_MIN_BACK_DELTA_X_PX &&
        deltaX > Math.abs(deltaY) * LEFT_EDGE_BACK_SLOPE_RATIO
      ) {
        pointerMode = "back-swipe";
        backSwipeProgress = clamp(
          deltaX / NOW_PLAYING_CONSTANTS.BACK_SWIPE_DISTANCE_PX,
          0,
          1,
        );
      }
      return;
    }

    if (pointerMode === "seek") {
      const safeDuration =
        trackDurationSeconds > 0
          ? trackDurationSeconds
          : resolveSafeTrackDurationSeconds(activeTrack);
      const maxSeekSeconds = Math.max(0, safeDuration - SEEK_END_GUARD_SECONDS);
      const currentRadius = Math.hypot(pointX - DISPLAY_CENTER_PX, pointY - DISPLAY_CENTER_PX);
      const nextAngle = Math.atan2(pointY - DISPLAY_CENTER_PX, pointX - DISPLAY_CENTER_PX);

      if (currentRadius >= SEEK_ARC_MIN_RADIUS_PX) {
        const deltaAngle = normalizeAngleDelta(nextAngle, seekLastAngle);
        seekLastAngle = nextAngle;
        if (Math.abs(deltaAngle) > SEEK_ARC_MIN_ANGLE_STEP) {
          const scrubSeconds = (deltaAngle / FULL_CIRCLE_RADIANS) * safeDuration;
          trackElapsedSeconds = clamp(trackElapsedSeconds + scrubSeconds, 0, maxSeekSeconds);
          vinylRotationRadians += deltaAngle * SEEK_ARC_SPIN_FACTOR;
          if (typeof onSeek === "function") {
            onSeek(trackElapsedSeconds);
          }
        }
      } else {
        seekLastAngle = nextAngle;
        const scrubSeconds = (stepX / DISPLAY_CENTER_PX) * safeDuration * SEEK_SPINDLE_SENSITIVITY;
        trackElapsedSeconds = clamp(trackElapsedSeconds + scrubSeconds, 0, maxSeekSeconds);
        vinylRotationRadians += stepX * SEEK_SPINDLE_SPIN_FACTOR;
        if (typeof onSeek === "function") {
          onSeek(trackElapsedSeconds);
        }
      }
      return;
    }

    if (pointerMode === "volume-pending") {
      if (pointerMovedDistance < VOLUME_BEZEL_START_MOVE_PX) {
        return;
      }
      pointerMode = "volume";
      isAdjustingVolume = true;
      volumeHudAlpha = 1;
    }

    if (pointerMode === "volume") {
      const currentAngle = Math.atan2(pointY - DISPLAY_CENTER_PX, pointX - DISPLAY_CENTER_PX);
      const deltaAngle = normalizeAngleDelta(currentAngle, volumeLastAngle);
      volumeLastAngle = currentAngle;
      adjustVolume(volumeLevel - deltaAngle / NOW_PLAYING_CONSTANTS.VOLUME_FULL_ARC_RADIANS);
      return;
    }

    if (pointerMode === "vinyl") {
      if (
        pointerStartY >= NOW_PLAYING_CONSTANTS.QUEUE_START_MIN_Y_PX &&
        deltaY < VINYL_QUEUE_MIN_DELTA_Y_PX &&
        Math.abs(deltaY) > Math.abs(deltaX) * VINYL_QUEUE_SLOPE_RATIO
      ) {
        seekHoldSeconds = 0;
        seekHoldProgress = 0;
        pointerMode = "queue-pull";
      } else if (
        Math.abs(deltaX) > VINYL_SKIP_MIN_DELTA_X_PX &&
        Math.abs(deltaX) > Math.abs(deltaY) * VINYL_SKIP_SLOPE_RATIO
      ) {
        seekHoldSeconds = 0;
        seekHoldProgress = 0;
        pointerMode = "skip-drag";
        skipDragOffsetX = clamp(deltaX, -SKIP_DRAG_CLAMP_PX, SKIP_DRAG_CLAMP_PX);
      }
      return;
    }

    if (pointerMode === "skip-drag") {
      skipDragOffsetX = clamp(deltaX, -SKIP_DRAG_CLAMP_PX, SKIP_DRAG_CLAMP_PX);
      return;
    }

    if (pointerMode === "quick-actions") {
      if (pointerMovedDistance >= NOW_PLAYING_CONSTANTS.TAP_DRIFT_LIMIT_PX) {
        pressedActionId = null;
      }
      if (
        pointerStartY >= NOW_PLAYING_CONSTANTS.QUEUE_START_MIN_Y_PX &&
        deltaY < QUICK_ACTION_QUEUE_MIN_DELTA_Y_PX &&
        Math.abs(deltaY) > Math.abs(deltaX) * VINYL_QUEUE_SLOPE_RATIO
      ) {
        openQueueSheet();
        pointerMode = "queue-header";
      }
      return;
    }

    if (pointerMode === "queue-header") {
      if (
        deltaY > QUEUE_HEADER_CLOSE_DELTA_Y_PX &&
        Math.abs(deltaY) > Math.abs(deltaX) * QUEUE_HEADER_CLOSE_SLOPE_RATIO
      ) {
        queueSheetOpen = false;
        pointerMode = "none";
      } else if (
        deltaX > QUEUE_HEADER_BACK_MIN_DELTA_X_PX &&
        Math.abs(deltaX) > Math.abs(deltaY) * QUEUE_HEADER_BACK_SLOPE_RATIO
      ) {
        pointerMode = "back-swipe";
        backSwipeProgress = clamp(
          deltaX / NOW_PLAYING_CONSTANTS.BACK_SWIPE_DISTANCE_PX,
          0,
          1,
        );
      }
      return;
    }

    if (pointerMode === "queue-list") {
      if (pointerMovedDistance >= NOW_PLAYING_CONSTANTS.TAP_DRIFT_LIMIT_PX) {
        pressedQueueRowIndex = null;
      }
      if (
        pointerStartX <= QUEUE_LIST_BACK_START_MAX_X_PX &&
        deltaX > QUEUE_LIST_BACK_MIN_DELTA_X_PX &&
        Math.abs(deltaX) > Math.abs(deltaY) * QUEUE_LIST_BACK_SLOPE_RATIO
      ) {
        pressedQueueRowIndex = null;
        pointerMode = "back-swipe";
        backSwipeProgress = clamp(
          deltaX / NOW_PLAYING_CONSTANTS.BACK_SWIPE_DISTANCE_PX,
          0,
          1,
        );
        return;
      }
      const maxScroll = Math.max(0, queueTracks.length - QUEUE_VISIBLE_TAIL_SLOTS);
      queuePointerVelocityY = stepY;
      const unclampedScroll =
        queuePointerStartScroll - deltaY / NOW_PLAYING_CONSTANTS.QUEUE_ROW_PITCH_PX;
      if (
        queuePointerStartScroll <= QUEUE_CLOSE_START_SCROLL_LIMIT &&
        deltaY > QUEUE_CLOSE_PULL_DELTA_Y_PX
      ) {
        queueSheetOpen = false;
        pointerMode = "none";
        return;
      }
      queueScrollPosition = clamp(
        unclampedScroll,
        -QUEUE_OVERSCROLL_TOP_SLOTS,
        maxScroll + QUEUE_OVERSCROLL_BOTTOM_SLOTS,
      );
      return;
    }

    if (pointerMode === "details") {
      return;
    }

    if (pointerMode === "picker" && pointerMovedDistance >= NOW_PLAYING_CONSTANTS.TAP_DRIFT_LIMIT_PX) {
      pressedPlaylistIndex = null;
    }
    if (pointerMode === "idle-cta" && pointerMovedDistance >= IDLE_CTA_MAX_DRIFT_PX) {
      pressedCta = false;
    }
  }

  /**
   * Finalize pointer interaction: commit taps, double-taps, Quick Actions, playlist picks,
   * queue row jumps, swipe skips, queue pulls, seeks, or back-swipe navigation.
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
    const finalDistance = Math.hypot(totalDeltaX, totalDeltaY);
    const movedDistance = Math.max(pointerMovedDistance, finalDistance);
    const wasTap = movedDistance < NOW_PLAYING_CONSTANTS.TAP_DRIFT_LIMIT_PX;

    const activeMode = pointerMode;
    const actionId = pressedActionId;
    const playlistRowIndex = pressedPlaylistIndex;
    const queueRowIndex = pressedQueueRowIndex;
    const wasCtaPressed = pressedCta;
    const wasSeekHold = seekHoldTriggered;
    const holdSecondsAtRelease = pointerHoldSeconds;

    clearPointerState();

    if (activeMode === "back-swipe") {
      if (backSwipeProgress >= NOW_PLAYING_CONSTANTS.BACK_SWIPE_COMMIT_THRESHOLD) {
        stepBack();
      }
      return;
    }

    if (activeMode === "idle-cta") {
      if (wasTap && wasCtaPressed && operatingMode !== "playing") {
        if (typeof onBrowseLibrary === "function") {
          onBrowseLibrary();
        }
      }
      return;
    }

    if (activeMode === "seek") {
      if (typeof onSeek === "function") {
        onSeek(trackElapsedSeconds);
      }
      return;
    }

    if (
      activeMode === "volume" ||
      activeMode === "volume-pending" ||
      activeMode === "idle-bg" ||
      activeMode === "queue-header" ||
      activeMode === "details"
    ) {
      return;
    }

    if (activeMode === "skip-drag") {
      if (Math.abs(skipDragOffsetX) >= NOW_PLAYING_CONSTANTS.SKIP_DRAG_COMMIT_PX) {
        skipTrack(skipDragOffsetX < 0 ? 1 : -1);
      }
      return;
    }

    if (activeMode === "queue-pull") {
      if (totalDeltaY <= NOW_PLAYING_CONSTANTS.QUEUE_PULL_COMMIT_DELTA_Y) {
        openQueueSheet();
      }
      return;
    }

    if (activeMode === "queue-list") {
      const maxScroll = Math.max(0, queueTracks.length - QUEUE_VISIBLE_TAIL_SLOTS);
      if (wasTap) {
        const tappedIndex =
          queueRowIndex !== null
            ? queueRowIndex
            : hitTestNowPlayingQueueRow(releaseX, releaseY, queueScrollPosition, queueTracks);
        if (tappedIndex !== null) {
          selectQueueTrack(tappedIndex);
        }
      } else {
        const projected =
          queueScrollPosition -
          clamp(
            queuePointerVelocityY * QUEUE_FLICK_VELOCITY_WEIGHT,
            -QUEUE_FLICK_MAX_SLOTS,
            QUEUE_FLICK_MAX_SLOTS,
          );
        queueTargetScroll = clamp(Math.round(projected), 0, maxScroll);
      }
      return;
    }

    if (activeMode === "picker") {
      if (wasTap) {
        if (playlistRowIndex !== null && playlists[playlistRowIndex]) {
          const targetPlaylist = playlists[playlistRowIndex];
          if (activeTrack) {
            if (!targetPlaylist.trackIds.includes(activeTrack.id)) {
              targetPlaylist.trackIds.push(activeTrack.id);
              if (typeof onPersistPlaylists === "function") {
                onPersistPlaylists(clonePlaylists(playlists));
              }
              showToast(`ADDED TO ${targetPlaylist.name.toUpperCase()}`);
            } else {
              showToast(`IN ${targetPlaylist.name.toUpperCase()}`);
            }
          }
          playlistPickerOpen = false;
          quickActionsIdleSeconds = 0;
        } else {
          const insideCard =
            releaseX >= PICKER_CARD_LEFT_X_PX &&
            releaseX <= PICKER_CARD_RIGHT_X_PX &&
            releaseY >= PICKER_CARD_TOP_Y_PX &&
            releaseY <= PICKER_CARD_BOTTOM_Y_PX;
          if (!insideCard) {
            playlistPickerOpen = false;
          }
        }
      }
      return;
    }

    if (activeMode === "quick-actions" || activeMode === "left-edge-actions") {
      if (wasTap) {
        if (actionId === "favorite") {
          if (activeTrack) {
            const nextFavorite = !activeTrack.isFavorite;
            updateTrackFavorite(activeTrack.id, nextFavorite);
            if (typeof onToggleFavorite === "function") {
              onToggleFavorite(activeTrack.id, nextFavorite);
            }
            showToast(nextFavorite ? "ADDED TO FAVORITES" : "REMOVED FROM FAVORITES");
          }
          quickActionsIdleSeconds = 0;
        } else if (actionId === "shuffle") {
          toggleShuffle();
        } else if (actionId === "repeat") {
          cycleRepeatMode();
        } else if (actionId === "playlist") {
          if (playlists.length === 0) {
            playlists = [{ id: "dialos-crate", name: "DialOS Crate", trackIds: [] }];
          }
          quickActionsOpen = false;
          playlistPickerOpen = true;
          quickActionsIdleSeconds = 0;
        } else if (actionId === "lyrics") {
          quickActionsOpen = false;
          if (lyricsOpen) {
            lyricsOpen = false;
          } else {
            openLyricsOverlay();
          }
        } else if (actionId === "info") {
          quickActionsOpen = false;
          detailsSheetOpen = true;
          quickActionsIdleSeconds = 0;
        } else {
          quickActionsOpen = false;
        }
      }
      return;
    }

    if (
      wasTap &&
      !wasSeekHold &&
      holdSecondsAtRelease < NOW_PLAYING_CONSTANTS.SEEK_HOLD_DURATION_SECONDS &&
      operatingMode === "playing"
    ) {
      if (
        pendingSingleTapActive &&
        pendingSingleTapSeconds < NOW_PLAYING_CONSTANTS.DOUBLE_TAP_WINDOW_SECONDS
      ) {
        clearPendingSingleTap();
        togglePlayPauseState();
      } else {
        pendingSingleTapActive = true;
        pendingSingleTapSeconds = 0;
      }
    }
  }

  /**
   * Cancel active pointer tracking without committing taps, skips, or back-swipes.
   */
  function handlePointerCancel() {
    clearPointerState();
  }

  /**
   * Route rotary wheel input to Context Queue scrolling when open, or output volume otherwise.
   *
   * @param {number} deltaY Signed rotary wheel delta.
   */
  function handleWheel(deltaY) {
    if (
      typeof deltaY !== "number" ||
      !Number.isFinite(deltaY) ||
      deltaY === 0 ||
      operatingMode !== "playing" ||
      detailsSheetOpen ||
      playlistPickerOpen
    ) {
      return;
    }

    if (queueSheetOpen) {
      const maxScroll = Math.max(0, queueTracks.length - QUEUE_VISIBLE_TAIL_SLOTS);
      const stepDirection = deltaY > 0 ? 1 : -1;
      queueTargetScroll = clamp(queueTargetScroll + stepDirection, 0, maxScroll);
      return;
    }

    const volumeStep = deltaY < 0 ? WHEEL_VOLUME_STEP : -WHEEL_VOLUME_STEP;
    adjustVolume(volumeLevel + volumeStep);
  }

  /**
   * Return a complete snapshot of Now Playing state, enriched track details,
   * queue context, and active lyrics frame for rendering and testing.
   *
   * @returns {object} Current Now Playing render snapshot.
   */
  function getRenderSnapshot() {
    const queueContext = resolveQueueContext();
    const activeLyricsEntry = activeTrack ? lyricsCatalog[activeTrack.id] || null : null;
    const lyricsSource = resolveTrackLyricsSource(activeTrack, activeLyricsEntry, lyricsMode);
    const lyricsFrame = resolveActiveLyricsFrame(
      activeTrack,
      activeLyricsEntry,
      trackElapsedSeconds,
      lyricsMode,
    );
    const resolvedAlbumId = activeTrack?.albumId || activeAlbum?.id || "";
    const albumTechSpec =
      (resolvedAlbumId ? albumTechSpecsById.get(resolvedAlbumId) : null) || null;
    const trackDetails = enrichTrackDetails(
      activeTrack,
      activeAlbum,
      queueTracks.length,
      queueTracks.length > 0 ? queueIndex + 1 : 0,
      albumTechSpec,
    );

    return {
      operatingMode,
      returnScreen,
      launchOrigin,
      activeTrack,
      activeAlbum,
      previousTrack,
      previousAlbum,
      queueContext,
      queueTracks: queueTracks.slice(),
      queueIndex,
      isPlaying,
      spinAngularVelocity,
      vinylRotationRadians,
      vinylNudgeRadians,
      trackElapsedSeconds,
      trackDurationSeconds,
      idleClockSeconds,
      shuffleEnabled,
      repeatMode,
      ephemeralCurrentTrackId,
      baseTrackCount,
      volumeLevel,
      volumeHudAlpha,
      isSeeking,
      seekHoldProgress,
      seekExpandProgress,
      openTransitionProgress,
      openChromeHoldAlpha,
      skipTransitionProgress,
      skipDirection,
      skipDragOffsetX,
      quickActionsOpen,
      quickActionsProgress,
      pressedActionId,
      playlistPickerOpen,
      playlistPickerProgress,
      pressedPlaylistIndex,
      playlists: clonePlaylists(playlists),
      detailsSheetOpen,
      detailsSheetProgress,
      trackDetails,
      queueSheetOpen,
      queueSheetProgress,
      queueScrollPosition,
      queueTargetScroll,
      pressedQueueRowIndex,
      lyricsOpen,
      lyricsProgress,
      lyricsMode,
      lyricsFrame,
      lyricsSource,
      lyricsActiveLineIndex,
      lyricsPreviousLineText,
      lyricsLineTransitionProgress,
      playPausePulseAlpha,
      trackBannerTitle,
      trackBannerMeta,
      trackBannerPhase,
      trackBannerAlpha,
      toastText,
      toastAlpha,
      backSwipeProgress,
      pointerActive,
      pointerMode,
      pressedCta,
    };
  }

  return {
    hydrateCatalog,
    syncAudioState,
    enterFromHome,
    enterFromLibrary,
    step,
    stepBack,
    activateFocused,
    skipTrack,
    cycleLyricsMode,
    togglePlayPause,
    toggleShuffle,
    cycleRepeatMode,
    toggleLyrics,
    toggleTrackDetails,
    toggleQueueSheet,
    setPlaylists,
    updateTrackFavorite,
    handlePointerDown,
    handlePointerMove,
    handlePointerUp,
    handlePointerCancel,
    handleWheel,
    getRenderSnapshot,
    getState: getRenderSnapshot,
  };
}
