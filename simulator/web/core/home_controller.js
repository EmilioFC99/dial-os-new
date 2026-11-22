/**
 * Pure deterministic Home infinite-wheel state machine, layout geometry,
 * and state-aware subtitle formatting for the DialOS 360x360 display simulator.
 */

import {
  DISPLAY_CENTER_PX,
  clamp,
  easeInOutCubic,
  easeOutExpo,
  formatClockTime,
  formatIntegerCount,
  lerp,
  normalizeRange,
} from "./geometry.js";

export const HOME_APPS = Object.freeze([
  Object.freeze({ id: "now-playing", title: "Now Playing" }),
  Object.freeze({ id: "library", title: "Library" }),
  Object.freeze({ id: "modes", title: "Modes" }),
  Object.freeze({ id: "settings", title: "Settings" }),
]);

export const HOME_CONSTANTS = Object.freeze({
  CUBE_CENTER_Y: 148,
  FOCUSED_HALF_SIZE_PX: 45,
  NEIGHBOR_HALF_SIZE_PX: 25,
  DISTANT_HALF_SIZE_PX: 16,
  WHEEL_SPACING_PX: 128,
  PERSPECTIVE_FOCAL_PX: 410,
  DRAG_PIXELS_PER_DETENT: 112,
  ENTRY_DURATION_SECONDS: 0.78,
  LAUNCH_DURATION_SECONDS: 0.62,
  BACK_SWIPE_DISTANCE_PX: 96,
  BACK_SWIPE_COMMIT_THRESHOLD: 0.45,
  TAP_DRIFT_LIMIT_PX: 10,
});

const VINYL_SPIN_RADIANS_PER_SECOND = 0.55;
const VINYL_NUDGE_SPRING_RATE = 8.5;
const VINYL_LAUNCH_NUDGE_RADIANS = 0.35;
const CAROUSEL_SNAP_SPRING_RATE = 13.5;
const CAROUSEL_SNAP_SETTLE_EPSILON = 0.001;
const CAROUSEL_NORMALIZE_REVOLUTION_THRESHOLD = 4;
const BACK_SWIPE_DECAY_PER_SECOND = 4.5;
const JUMP_LIBRARY_MIN_LAUNCH_PROGRESS = 0.28;
const CAROUSEL_FLICK_VELOCITY_WEIGHT = 0.045;
const CAROUSEL_FLICK_MAX_SLOTS = 0.45;
const CUBE_HIT_SCALE = 1.25;
const CUBE_HIT_PADDING_PX = 8;
const CTA_HIT_TOP_Y = 230;
const CTA_HIT_BOTTOM_Y = 306;
const CTA_HIT_HALF_WIDTH_PX = 94;

const ENTRY_SIDE_SPREAD_PX = 110;
const ENTRY_FOCUSED_START_HALF_SIZE_PX = 30;
const LAUNCH_PART_END_PROGRESS = 0.52;
const LAUNCH_SIDE_SPREAD_PX = 165;
const DELTA_Y_DROP_CAP = 1.7;
const DELTA_Y_DROP_PX = 14;
const BOB_TIME_SPEED = 1.9;
const BOB_SLOT_PHASE = 1.3;
const BOB_MIN_AMPLITUDE_PX = 1.6;
const BOB_MAX_AMPLITUDE_PX = 3.2;
const SWAY_YAW_TIME_SPEED = 1.25;
const SWAY_YAW_SLOT_PHASE = 1.7;
const SWAY_YAW_MIN_RADIANS = 0.18;
const SWAY_YAW_MAX_RADIANS = 0.28;
const ENTRY_SPIN_HALF_TURN_RADIANS = Math.PI * 0.5;
const SCROLL_YAW_RADIANS_PER_SLOT = Math.PI * 0.52;
const BASE_TILT_X_RADIANS = 0.16;
const SWAY_PITCH_TIME_SPEED = 0.95;
const SWAY_PITCH_AMPLITUDE_RADIANS = 0.055;
const SWAY_ROLL_TIME_SPEED = 0.75;
const SWAY_ROLL_AMPLITUDE_RADIANS = 0.025;
const SCROLL_ROLL_RADIANS_PER_SLOT = 0.06;
const ALPHA_FADE_START_DELTA = 1.05;
const ALPHA_FADE_END_DELTA = 1.62;

const MAX_SUBTITLE_TITLE_LENGTH = 18;
const TRUNCATED_TITLE_LENGTH = 17;
const DEFAULT_PLAYBACK_VOLUME = 0.62;
const LIBRARY_SLOT_INDEX = 1;
const VALID_PLAYBACK_STATES = new Set(["idle", "playing", "storage"]);

/**
 * Wrap any positive or negative virtual slot index into [0, HOME_APPS.length - 1].
 *
 * @param {number} slotIndex Virtual carousel slot index.
 * @returns {number} Wrapped app index in [0, 3].
 */
export function wrapAppIndex(slotIndex) {
  if (!Number.isFinite(slotIndex)) {
    return 0;
  }
  const appCount = HOME_APPS.length;
  const integerSlot = Math.trunc(slotIndex);
  return ((integerSlot % appCount) + appCount) % appCount;
}

/**
 * Compute the signed shortest step delta around the 4-app ring from `fromSlot`
 * to `desiredAppIndex`.
 *
 * @param {number} fromSlot Current virtual slot position.
 * @param {number} desiredAppIndex Target app index in [0, 3].
 * @returns {number} Signed shortest delta in [-2, 2].
 */
export function shortestCyclicDelta(fromSlot, desiredAppIndex) {
  const appCount = HOME_APPS.length;
  const currentWrapped = wrapAppIndex(Math.round(fromSlot));
  const targetWrapped = wrapAppIndex(Math.round(desiredAppIndex));
  let delta = targetWrapped - currentWrapped;
  if (delta > appCount / 2) {
    delta -= appCount;
  } else if (delta < -appCount / 2) {
    delta += appCount;
  }
  return delta;
}

/**
 * Compute normalized focus weight [0, 1] for an app index against `scrollPosition`.
 *
 * @param {number} appIndex App index in [0, 3].
 * @param {number} scrollPosition Continuous virtual wheel position.
 * @returns {number} Focus weight (1 when aligned, 0 when >= 1 slot away).
 */
export function cyclicFocusWeight(appIndex, scrollPosition) {
  const appCount = HOME_APPS.length;
  const wrappedScroll = ((scrollPosition % appCount) + appCount) % appCount;
  let distance = Math.abs(wrappedScroll - wrapAppIndex(appIndex));
  if (distance > appCount / 2) {
    distance = appCount - distance;
  }
  return clamp(1 - distance, 0, 1);
}

/**
 * Compute 3D glass cube placement, scale, orientation, bobbing, and opacity
 * for a virtual slot on the Home infinite wheel.
 *
 * @param {number} virtualSlot Virtual slot index to lay out.
 * @param {number} scrollPosition Current continuous wheel position.
 * @param {number} entryProgress Boot-to-Home handoff progress in [0, 1].
 * @param {number} launchProgress App launch progress in [0, 1].
 * @param {number} focusedSlot Currently targeted virtual slot.
 * @param {number} [clockSeconds=0] Idle clock time in seconds.
 * @returns {{
 *   virtualSlot: number,
 *   appIndex: number,
 *   centerX: number,
 *   centerY: number,
 *   halfSize: number,
 *   angleX: number,
 *   angleY: number,
 *   angleZ: number,
 *   bobOffsetY: number,
 *   focusWeight: number,
 *   alpha: number
 * }} Slot layout descriptor.
 */
export function computeHomeGlassSlotLayout(
  virtualSlot,
  scrollPosition,
  entryProgress,
  launchProgress,
  focusedSlot,
  clockSeconds = 0,
) {
  const deltaSlot = virtualSlot - scrollPosition;
  const absDelta = Math.abs(deltaSlot);
  const focusWeight = clamp(1 - absDelta, 0, 1);
  const isFocusedTarget = virtualSlot === focusedSlot;

  const entryEase = easeOutExpo(entryProgress);
  const entrySideSpread =
    (1 - entryEase) * (isFocusedTarget ? 0 : Math.sign(deltaSlot || 1) * ENTRY_SIDE_SPREAD_PX);

  const launchPartFactor = easeInOutCubic(
    normalizeRange(launchProgress, 0, LAUNCH_PART_END_PROGRESS),
  );
  const launchSideSpread = !isFocusedTarget
    ? Math.sign(deltaSlot || (virtualSlot < focusedSlot ? -1 : 1)) *
      LAUNCH_SIDE_SPREAD_PX *
      launchPartFactor
    : 0;

  const centerX =
    DISPLAY_CENTER_PX +
    deltaSlot * HOME_CONSTANTS.WHEEL_SPACING_PX +
    entrySideSpread +
    launchSideSpread;

  const bobOffsetY =
    Math.sin(clockSeconds * BOB_TIME_SPEED + virtualSlot * BOB_SLOT_PHASE) *
    lerp(BOB_MIN_AMPLITUDE_PX, BOB_MAX_AMPLITUDE_PX, focusWeight);

  let centerY =
    HOME_CONSTANTS.CUBE_CENTER_Y +
    Math.min(absDelta, DELTA_Y_DROP_CAP) * DELTA_Y_DROP_PX +
    bobOffsetY * (1 - launchPartFactor);

  let halfSize;
  if (absDelta <= 1) {
    halfSize = lerp(
      HOME_CONSTANTS.FOCUSED_HALF_SIZE_PX,
      HOME_CONSTANTS.NEIGHBOR_HALF_SIZE_PX,
      easeInOutCubic(absDelta),
    );
  } else {
    halfSize = lerp(
      HOME_CONSTANTS.NEIGHBOR_HALF_SIZE_PX,
      HOME_CONSTANTS.DISTANT_HALF_SIZE_PX,
      clamp(absDelta - 1, 0, 1),
    );
  }

  if (isFocusedTarget && entryProgress < 1) {
    centerY = lerp(DISPLAY_CENTER_PX, centerY, entryEase);
    halfSize = lerp(ENTRY_FOCUSED_START_HALF_SIZE_PX, halfSize, entryEase);
  }

  const swayYaw =
    Math.sin(clockSeconds * SWAY_YAW_TIME_SPEED + virtualSlot * SWAY_YAW_SLOT_PHASE) *
    lerp(SWAY_YAW_MIN_RADIANS, SWAY_YAW_MAX_RADIANS, focusWeight);
  const entrySpinYaw = isFocusedTarget ? (1 - entryEase) * ENTRY_SPIN_HALF_TURN_RADIANS : 0;
  const scrollYaw = deltaSlot * SCROLL_YAW_RADIANS_PER_SLOT;

  const angleY = swayYaw + scrollYaw + entrySpinYaw;
  const angleX =
    BASE_TILT_X_RADIANS +
    Math.cos(clockSeconds * SWAY_PITCH_TIME_SPEED + virtualSlot) * SWAY_PITCH_AMPLITUDE_RADIANS;
  const angleZ =
    Math.sin(clockSeconds * SWAY_ROLL_TIME_SPEED + virtualSlot) * SWAY_ROLL_AMPLITUDE_RADIANS +
    deltaSlot * SCROLL_ROLL_RADIANS_PER_SLOT;

  let alpha = 1 - normalizeRange(absDelta, ALPHA_FADE_START_DELTA, ALPHA_FADE_END_DELTA);
  if (!isFocusedTarget) {
    alpha *= 1 - launchPartFactor;
  }

  return {
    virtualSlot,
    appIndex: wrapAppIndex(virtualSlot),
    centerX,
    centerY,
    halfSize,
    angleX,
    angleY,
    angleZ,
    bobOffsetY,
    focusWeight,
    alpha: clamp(alpha, 0, 1),
  };
}

/**
 * Format state-aware mono telemetry subtitle text for a Home carousel app.
 *
 * @param {{ id: string }} appSpec Home app descriptor from HOME_APPS.
 * @param {object} [context={}] Live playback, library count, and appearance telemetry.
 * @returns {string} Uppercase telemetry subtitle line.
 */
export function resolveHomeAppSubtitle(appSpec, context = {}) {
  const appId = appSpec?.id;
  if (appId === "now-playing") {
    const playbackState = context?.playbackState || "idle";
    if (playbackState === "playing") {
      const titleSource =
        typeof context?.trackTitle === "string" && context.trackTitle.trim().length > 0
          ? context.trackTitle.trim()
          : "MIDNIGHT GROOVE";
      const upperTitle = titleSource.toUpperCase();
      const compactTitle =
        upperTitle.length > MAX_SUBTITLE_TITLE_LENGTH
          ? `${upperTitle.slice(0, TRUNCATED_TITLE_LENGTH).trimEnd()}\u2026`
          : upperTitle;
      const elapsedClock = formatClockTime(context?.trackElapsedSeconds);
      const durationClock = formatClockTime(context?.trackDurationSeconds);
      return `${compactTitle} \u00b7 ${elapsedClock} / ${durationClock}`;
    }
    if (playbackState === "storage") {
      return "USB STORAGE \u00b7 42.6 GB FREE";
    }
    return "NOT PLAYING \u00b7 BROWSE LIBRARY";
  }

  if (appId === "library") {
    const counts = context?.counts && typeof context.counts === "object" ? context.counts : {};
    const trackCount = Number.isFinite(counts.tracks) ? Math.max(0, Math.round(counts.tracks)) : 0;
    if (trackCount === 0) {
      return "EMPTY LIBRARY \u00b7 0 TRACKS";
    }
    const albumCount = Number.isFinite(counts.albums) ? Math.max(0, Math.round(counts.albums)) : 0;
    return `${formatIntegerCount(trackCount)} TRACKS \u00b7 ${formatIntegerCount(albumCount)} ALBUMS`;
  }

  if (appId === "modes") {
    return "STORAGE \u00b7 DAC \u00b7 WIRELESS";
  }

  if (appId === "settings") {
    const modeLabel = context?.appearance === "light" ? "LIGHT MODE" : "DARK MODE";
    return `${modeLabel} \u00b7 VOL LIMIT 85%`;
  }

  return "";
}

/**
 * Create an isolated Home infinite-wheel and app-launch state controller.
 *
 * @returns {object} Home controller API.
 */
export function createHomeController() {
  let scrollPosition = 0;
  let targetSlot = 0;
  let focusedSlot = 0;
  let entryProgress = 1;
  let isEnteringFromBoot = false;
  let launchProgress = 0;
  let launchTarget = 0;
  let launchedAppId = null;
  let playbackState = "idle";
  let volumeLevel = DEFAULT_PLAYBACK_VOLUME;
  let idleClockSeconds = 0;
  let vinylRotationRadians = 0;
  let vinylNudgeRadians = 0;
  let vinylNudgeTarget = 0;
  let backSwipeProgress = 0;

  let pointerActive = false;
  let pointerMode = "none";
  let pointerStartX = 0;
  let pointerStartY = 0;
  let pointerLastX = 0;
  let pointerVelocityX = 0;
  let pointerStartScroll = 0;
  let pointerMovedDistance = 0;

  function clearPointerState() {
    pointerActive = false;
    pointerMode = "none";
    pointerStartX = 0;
    pointerStartY = 0;
    pointerLastX = 0;
    pointerVelocityX = 0;
    pointerStartScroll = 0;
    pointerMovedDistance = 0;
  }

  /**
   * Reset Home into the Boot-to-Home handoff entry state at slot 0.
   */
  function enterFromBoot() {
    scrollPosition = 0;
    targetSlot = 0;
    focusedSlot = 0;
    entryProgress = 0;
    isEnteringFromBoot = true;
    launchProgress = 0;
    launchTarget = 0;
    launchedAppId = null;
    vinylNudgeRadians = 0;
    vinylNudgeTarget = 0;
    backSwipeProgress = 0;
    clearPointerState();
  }

  /**
   * Advance Home animations, transitions, critically damped wheel snapping,
   * and released back-swipe decay by `deltaSeconds`.
   *
   * @param {number} deltaSeconds Frame elapsed time in seconds.
   */
  function step(deltaSeconds) {
    if (typeof deltaSeconds !== "number" || !Number.isFinite(deltaSeconds) || deltaSeconds <= 0) {
      return;
    }

    idleClockSeconds += deltaSeconds;

    if (playbackState === "playing") {
      vinylRotationRadians += deltaSeconds * VINYL_SPIN_RADIANS_PER_SECOND;
    }

    const nudgeFollow = 1 - Math.exp(-VINYL_NUDGE_SPRING_RATE * deltaSeconds);
    vinylNudgeRadians = lerp(vinylNudgeRadians, vinylNudgeTarget, nudgeFollow);

    if (isEnteringFromBoot) {
      entryProgress = clamp(
        entryProgress + deltaSeconds / HOME_CONSTANTS.ENTRY_DURATION_SECONDS,
        0,
        1,
      );
      if (entryProgress >= 1) {
        isEnteringFromBoot = false;
      }
    }

    if (launchTarget === 1 && launchProgress < 1) {
      launchProgress = clamp(
        launchProgress + deltaSeconds / HOME_CONSTANTS.LAUNCH_DURATION_SECONDS,
        0,
        1,
      );
    } else if (launchTarget === 0 && launchProgress > 0) {
      launchProgress = clamp(
        launchProgress - deltaSeconds / HOME_CONSTANTS.LAUNCH_DURATION_SECONDS,
        0,
        1,
      );
    }

    if (!pointerActive || pointerMode !== "carousel") {
      const snapFollow = 1 - Math.exp(-CAROUSEL_SNAP_SPRING_RATE * deltaSeconds);
      scrollPosition = lerp(scrollPosition, targetSlot, snapFollow);
      if (Math.abs(scrollPosition - targetSlot) < CAROUSEL_SNAP_SETTLE_EPSILON) {
        scrollPosition = targetSlot;
        if (Math.abs(scrollPosition) >= HOME_APPS.length * CAROUSEL_NORMALIZE_REVOLUTION_THRESHOLD) {
          const normalizedSlot = wrapAppIndex(targetSlot);
          scrollPosition = normalizedSlot;
          targetSlot = normalizedSlot;
          focusedSlot = normalizedSlot;
        }
      }
    }

    if (!pointerActive) {
      backSwipeProgress = Math.max(0, backSwipeProgress - deltaSeconds * BACK_SWIPE_DECAY_PER_SECOND);
    }
  }

  /**
   * Target a specific virtual slot on the infinite wheel when no app is launched.
   *
   * @param {number} slotIndex Desired virtual slot index.
   */
  function selectSlot(slotIndex) {
    if (launchTarget === 1 || !Number.isFinite(slotIndex)) {
      return;
    }
    const roundedSlot = Math.round(slotIndex);
    targetSlot = roundedSlot;
    focusedSlot = roundedSlot;
  }

  /**
   * Rotate via shortest cyclic path to an app identified by its canonical id.
   *
   * @param {string} appId App identifier (`now-playing`, `library`, `modes`, `settings`).
   */
  function selectAppById(appId) {
    if (launchTarget === 1 || typeof appId !== "string" || appId.length === 0) {
      return;
    }
    const appIndex = HOME_APPS.findIndex((appEntry) => appEntry.id === appId);
    if (appIndex < 0) {
      return;
    }
    const delta = shortestCyclicDelta(targetSlot, appIndex);
    selectSlot(targetSlot + delta);
  }

  /**
   * Step the infinite wheel by one rotary detent in the given signed direction.
   *
   * @param {number} direction Positive for clockwise, negative for counter-clockwise.
   */
  function stepDetent(direction) {
    if (launchTarget === 1 || typeof direction !== "number" || !Number.isFinite(direction)) {
      return;
    }
    const stepDirection = Math.sign(direction);
    if (stepDirection === 0) {
      return;
    }
    selectSlot(targetSlot + stepDirection);
  }

  /**
   * Launch the currently targeted Home app.
   */
  function launchFocusedApp() {
    focusedSlot = targetSlot;
    const focusedApp = HOME_APPS[wrapAppIndex(targetSlot)];
    if (launchTarget !== 1 && focusedApp.id === "now-playing" && playbackState === "playing") {
      vinylNudgeTarget += VINYL_LAUNCH_NUDGE_RADIANS;
    }
    launchTarget = 1;
    launchedAppId = focusedApp.id;
  }

  /**
   * Begin closing the currently launched app so `launchProgress` decays smoothly.
   */
  function closeLaunchedApp() {
    launchTarget = 0;
    launchedAppId = null;
    backSwipeProgress = 0;
  }

  /**
   * Sync external playback telemetry (`idle` | `playing` | `storage`) and clamped volume level.
   *
   * @param {object} [nextTelemetry={}] Updated playback state and/or normalized volume level.
   */
  function syncPlaybackState(nextTelemetry = {}) {
    if (!nextTelemetry || typeof nextTelemetry !== "object") {
      return;
    }
    if (
      typeof nextTelemetry.playbackState === "string" &&
      VALID_PLAYBACK_STATES.has(nextTelemetry.playbackState)
    ) {
      playbackState = nextTelemetry.playbackState;
    }
    if (
      typeof nextTelemetry.volumeLevel === "number" &&
      Number.isFinite(nextTelemetry.volumeLevel)
    ) {
      volumeLevel = clamp(nextTelemetry.volumeLevel, 0, 1);
    }
  }

  /**
   * Re-anchor Home on `appId`'s slot at full launch depth and animate zoom-back to 0.
   *
   * @param {string} [appId="library"] App identifier being exited back to Home.
   */
  function returnFromScreen(appId = "library") {
    const matchedIndex = HOME_APPS.findIndex((appEntry) => appEntry.id === appId);
    const slotIndex = matchedIndex >= 0 ? matchedIndex : 0;
    scrollPosition = slotIndex;
    targetSlot = slotIndex;
    focusedSlot = slotIndex;
    launchProgress = 1;
    launchTarget = 0;
    launchedAppId = null;
    backSwipeProgress = 0;
    clearPointerState();
  }

  /**
   * Jump directly from the Not Playing CTA into the launched Library app.
   */
  function jumpFromNotPlayingToLibrary() {
    targetSlot = LIBRARY_SLOT_INDEX;
    scrollPosition = LIBRARY_SLOT_INDEX;
    focusedSlot = LIBRARY_SLOT_INDEX;
    launchedAppId = HOME_APPS[LIBRARY_SLOT_INDEX].id;
    launchTarget = 1;
    launchProgress = Math.max(launchProgress, JUMP_LIBRARY_MIN_LAUNCH_PROGRESS);
    backSwipeProgress = 0;
  }

  /**
   * Begin a pointer interaction for either wheel dragging or launched-app back-swipe.
   *
   * @param {number} pointX Display coordinate X in [0, 360].
   * @param {number} pointY Display coordinate Y in [0, 360].
   */
  function handlePointerDown(pointX, pointY) {
    if (!Number.isFinite(pointX) || !Number.isFinite(pointY)) {
      return;
    }
    pointerActive = true;
    pointerStartX = pointX;
    pointerStartY = pointY;
    pointerLastX = pointX;
    pointerVelocityX = 0;
    pointerStartScroll = scrollPosition;
    pointerMovedDistance = 0;
    pointerMode = launchTarget === 1 ? "back-swipe" : "carousel";
  }

  /**
   * Update active wheel scroll or back-swipe progress during pointer movement.
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

    pointerMovedDistance = Math.max(pointerMovedDistance, Math.hypot(deltaX, deltaY));
    pointerVelocityX = stepX;
    pointerLastX = pointX;

    if (pointerMode === "carousel") {
      scrollPosition = pointerStartScroll - deltaX / HOME_CONSTANTS.DRAG_PIXELS_PER_DETENT;
    } else if (pointerMode === "back-swipe") {
      backSwipeProgress =
        deltaX > 0 ? clamp(deltaX / HOME_CONSTANTS.BACK_SWIPE_DISTANCE_PX, 0, 1) : 0;
    }
  }

  /**
   * Finalize pointer interaction: commit taps, velocity-assisted wheel snaps,
   * CTA switches, or back-swipe exits.
   *
   * @param {number} pointX Display coordinate X in [0, 360].
   * @param {number} pointY Display coordinate Y in [0, 360].
   */
  function handlePointerUp(pointX, pointY) {
    if (!pointerActive) {
      return;
    }

    const releaseX = Number.isFinite(pointX) ? pointX : pointerLastX;
    const releaseY = Number.isFinite(pointY) ? pointY : pointerStartY;
    const finalDistance = Math.hypot(releaseX - pointerStartX, releaseY - pointerStartY);
    const movedDistance = Math.max(pointerMovedDistance, finalDistance);
    const wasTap = movedDistance < HOME_CONSTANTS.TAP_DRIFT_LIMIT_PX;

    const activeMode = pointerMode;
    const releaseVelocityX = pointerVelocityX;
    clearPointerState();

    if (activeMode === "back-swipe") {
      if (backSwipeProgress >= HOME_CONSTANTS.BACK_SWIPE_COMMIT_THRESHOLD) {
        closeLaunchedApp();
        return;
      }
      if (wasTap && launchedAppId === "now-playing") {
        const tappedBottomCta =
          releaseY >= CTA_HIT_TOP_Y &&
          releaseY <= CTA_HIT_BOTTOM_Y &&
          Math.abs(releaseX - DISPLAY_CENTER_PX) <= CTA_HIT_HALF_WIDTH_PX;
        if (tappedBottomCta) {
          if (playbackState === "idle") {
            jumpFromNotPlayingToLibrary();
          } else if (playbackState === "storage") {
            closeLaunchedApp();
          }
        }
      }
      return;
    }

    if (wasTap) {
      const centerSlot = Math.round(scrollPosition);
      let tappedVirtualSlot = null;
      for (let slotOffset = -1; slotOffset <= 1; slotOffset += 1) {
        const candidateSlot = centerSlot + slotOffset;
        const layout = computeHomeGlassSlotLayout(
          candidateSlot,
          scrollPosition,
          entryProgress,
          launchProgress,
          focusedSlot,
          idleClockSeconds,
        );
        const hitDistance = Math.hypot(releaseX - layout.centerX, releaseY - layout.centerY);
        if (hitDistance <= layout.halfSize * CUBE_HIT_SCALE + CUBE_HIT_PADDING_PX) {
          tappedVirtualSlot = candidateSlot;
          break;
        }
      }

      if (tappedVirtualSlot === centerSlot) {
        targetSlot = centerSlot;
        launchFocusedApp();
      } else if (tappedVirtualSlot !== null) {
        selectSlot(tappedVirtualSlot);
      } else {
        selectSlot(centerSlot);
      }
      return;
    }

    const projectedPosition =
      scrollPosition -
      clamp(
        releaseVelocityX * CAROUSEL_FLICK_VELOCITY_WEIGHT,
        -CAROUSEL_FLICK_MAX_SLOTS,
        CAROUSEL_FLICK_MAX_SLOTS,
      );
    selectSlot(Math.round(projectedPosition));
  }

  /**
   * Cancel active pointer tracking without committing taps or back-swipes.
   */
  function handlePointerCancel() {
    clearPointerState();
  }

  /**
   * Return a fresh snapshot of the Home controller state for rendering.
   *
   * @returns {object} Current Home render snapshot.
   */
  function getRenderSnapshot() {
    return {
      scrollPosition,
      targetSlot,
      focusedSlot,
      focusedApp: HOME_APPS[wrapAppIndex(targetSlot)],
      entryProgress,
      isEnteringFromBoot,
      launchProgress,
      launchTarget,
      launchedAppId,
      playbackState,
      volumeLevel,
      idleClockSeconds,
      vinylRotationRadians,
      vinylNudgeRadians,
      backSwipeProgress,
      pointerActive,
      pointerMode,
    };
  }

  return {
    enterFromBoot,
    step,
    selectSlot,
    selectAppById,
    stepDetent,
    launchFocusedApp,
    closeLaunchedApp,
    syncPlaybackState,
    returnFromScreen,
    jumpFromNotPlayingToLibrary,
    handlePointerDown,
    handlePointerMove,
    handlePointerUp,
    handlePointerCancel,
    getRenderSnapshot,
    getState: getRenderSnapshot,
  };
}
