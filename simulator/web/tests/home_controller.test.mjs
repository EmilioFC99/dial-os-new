import { describe, test } from "node:test";
import assert from "node:assert/strict";

import { formatClockTime, projectPoint3d } from "../core/geometry.js";
import {
  HOME_APPS,
  HOME_CONSTANTS,
  computeHomeGlassSlotLayout,
  createHomeController,
  cyclicFocusWeight,
  resolveHomeAppSubtitle,
  shortestCyclicDelta,
  wrapAppIndex,
} from "../core/home_controller.js";

const DISPLAY_CENTER_PX = 180;
const STEP_DELTA_SECONDS = 0.05;
const SETTLE_STEP_COUNT = 40;
const WHEEL_STRESS_DETENT_COUNT = 20;
const FLOAT_TOLERANCE = 1e-6;
const TRANSITION_ROTATION_RADIANS = (20 * Math.PI) / 180;
const TRANSITION_SETTLE_TOLERANCE = 1e-4;

const CENTER_TAP_X = 180;
const CENTER_TAP_Y = 148;
const CTA_TAP_X = 180;
const CTA_TAP_Y = 254;
const CAROUSEL_DRAG_START_X = 240;
const CAROUSEL_DRAG_MID_X = 184;
const CAROUSEL_DRAG_END_X = 128;
const CAROUSEL_DRAG_Y = 148;

const BACK_SWIPE_START_X = 24;
const BACK_SWIPE_Y = 180;
const BACK_SWIPE_COMMIT_END_X = 90;
const BACK_SWIPE_PARTIAL_END_X = 50;

const SAMPLE_LIBRARY_COUNTS = Object.freeze({
  tracks: 1461,
  albums: 271,
  artists: 86,
  genres: 29,
});

const EMPTY_LIBRARY_COUNTS = Object.freeze({
  tracks: 0,
  albums: 0,
  artists: 0,
  genres: 0,
});

const EXPECTED_LAYOUT_FIELDS = Object.freeze([
  "virtualSlot",
  "appIndex",
  "centerX",
  "centerY",
  "halfSize",
  "angleX",
  "angleY",
  "angleZ",
  "bobOffsetY",
  "focusWeight",
  "alpha",
]);

/**
 * Advance a home controller over fixed simulation ticks until `stepCount` completes.
 *
 * @param {object} homeController Controller instance under test.
 * @param {number} stepDeltaSeconds Fixed frame step in seconds.
 * @param {number} stepCount Number of ticks to execute.
 * @returns {object} Render snapshot after the final tick.
 */
function advanceController(homeController, stepDeltaSeconds, stepCount) {
  for (let stepIndex = 0; stepIndex < stepCount; stepIndex += 1) {
    homeController.step(stepDeltaSeconds);
  }
  return homeController.getRenderSnapshot();
}

/**
 * Create a home controller that has completed Boot -> Home entry and settled at slot 0.
 *
 * @returns {object} Settled home controller instance.
 */
function createSettledHomeController() {
  const homeController = createHomeController();
  homeController.enterFromBoot();
  homeController.step(HOME_CONSTANTS.ENTRY_DURATION_SECONDS);
  return homeController;
}

describe("geometry.js Home helpers", () => {
  test("projectPoint3d projects 3D coordinates with perspectiveScale = focalLength / (focalLength - pointZ)", () => {
    const focalLength = HOME_CONSTANTS.PERSPECTIVE_FOCAL_PX;

    const planePoint = projectPoint3d(12, -8, 0, DISPLAY_CENTER_PX, HOME_CONSTANTS.CUBE_CENTER_Y, focalLength);
    assert.deepEqual(planePoint, {
      screenX: 192,
      screenY: 140,
      depthZ: 0,
      scale: 1,
    });

    const nearPoint = projectPoint3d(20, -10, 205, DISPLAY_CENTER_PX, HOME_CONSTANTS.CUBE_CENTER_Y, focalLength);
    assert.deepEqual(nearPoint, {
      screenX: 220,
      screenY: 128,
      depthZ: 205,
      scale: 2,
    });

    const farPoint = projectPoint3d(40, 20, -410, DISPLAY_CENTER_PX, HOME_CONSTANTS.CUBE_CENTER_Y, focalLength);
    assert.deepEqual(farPoint, {
      screenX: 200,
      screenY: 158,
      depthZ: -410,
      scale: 0.5,
    });
  });

  test("formatClockTime formats valid seconds as MM:SS and guards negative or non-finite inputs", () => {
    assert.equal(formatClockTime(0), "00:00");
    assert.equal(formatClockTime(65), "01:05");
    assert.equal(formatClockTime(185), "03:05");

    const invalidSeconds = [-1, -120, Number.NaN, Number.POSITIVE_INFINITY, Number.NEGATIVE_INFINITY];
    for (const candidateSeconds of invalidSeconds) {
      assert.equal(formatClockTime(candidateSeconds), "00:00");
    }
  });
});

describe("HOME_APPS and HOME_CONSTANTS", () => {
  test("HOME_APPS lists four apps in canonical order with pure titles", () => {
    assert.equal(HOME_APPS.length, 4);
    assert.deepEqual(
      HOME_APPS.map((appEntry) => ({ id: appEntry.id, title: appEntry.title })),
      [
        { id: "now-playing", title: "Now Playing" },
        { id: "library", title: "Library" },
        { id: "modes", title: "Modes" },
        { id: "settings", title: "Settings" },
      ],
    );

    for (const appEntry of HOME_APPS) {
      assert.equal(/^\d/.test(appEntry.title), false, `unexpected numeric prefix in ${appEntry.title}`);
    }
  });

  test("HOME_CONSTANTS exports exact spatial, timing, and gesture thresholds", () => {
    assert.equal(HOME_CONSTANTS.CUBE_CENTER_Y, 148);
    assert.equal(HOME_CONSTANTS.FOCUSED_HALF_SIZE_PX, 45);
    assert.equal(HOME_CONSTANTS.NEIGHBOR_HALF_SIZE_PX, 25);
    assert.equal(HOME_CONSTANTS.DISTANT_HALF_SIZE_PX, 16);
    assert.equal(HOME_CONSTANTS.WHEEL_SPACING_PX, 128);
    assert.equal(HOME_CONSTANTS.PERSPECTIVE_FOCAL_PX, 410);
    assert.equal(HOME_CONSTANTS.DRAG_PIXELS_PER_DETENT, 112);
    assert.equal(HOME_CONSTANTS.ENTRY_DURATION_SECONDS, 0.78);
    assert.equal(HOME_CONSTANTS.LAUNCH_DURATION_SECONDS, 0.62);
    assert.equal(HOME_CONSTANTS.BACK_SWIPE_DISTANCE_PX, 96);
    assert.equal(HOME_CONSTANTS.BACK_SWIPE_COMMIT_THRESHOLD, 0.45);
    assert.equal(HOME_CONSTANTS.TAP_DRIFT_LIMIT_PX, 10);
  });
});

describe("Home wheel math and layout helpers", () => {
  test("wrapAppIndex wraps positive and negative virtual slots into [0, 3]", () => {
    assert.equal(wrapAppIndex(0), 0);
    assert.equal(wrapAppIndex(3), 3);
    assert.equal(wrapAppIndex(4), 0);
    assert.equal(wrapAppIndex(5), 1);
    assert.equal(wrapAppIndex(-1), 3);
    assert.equal(wrapAppIndex(-4), 0);
    assert.equal(wrapAppIndex(-5), 3);
  });

  test("shortestCyclicDelta computes signed shortest path across the 4-app ring", () => {
    assert.equal(shortestCyclicDelta(0, 3), -1);
    assert.equal(shortestCyclicDelta(3, 0), 1);
    assert.equal(shortestCyclicDelta(0, 1), 1);
    assert.equal(shortestCyclicDelta(1, 0), -1);
    assert.equal(shortestCyclicDelta(4, 3), -1);
    assert.equal(shortestCyclicDelta(-1, 0), 1);
    assert.equal(shortestCyclicDelta(2, 2), 0);
  });

  test("cyclicFocusWeight returns 1 at alignment, 0.5 at midpoint, and 0 when 1+ slots away", () => {
    assert.equal(cyclicFocusWeight(0, 0), 1);
    assert.equal(cyclicFocusWeight(1, 1), 1);
    assert.equal(cyclicFocusWeight(0, 4), 1);
    assert.equal(cyclicFocusWeight(3, -1), 1);
    assert.equal(cyclicFocusWeight(0, 0.5), 0.5);
    assert.equal(cyclicFocusWeight(0, 1), 0);
    assert.equal(cyclicFocusWeight(0, 2), 0);
    assert.equal(cyclicFocusWeight(0, 3), 0);
  });

  test("computeHomeGlassSlotLayout returns full geometry and centers Boot -> Home transition cleanly", () => {
    const entryStartLayout = computeHomeGlassSlotLayout(0, 0, 0, 0, 0, 0);
    for (const fieldName of EXPECTED_LAYOUT_FIELDS) {
      assert.ok(fieldName in entryStartLayout, `missing layout field: ${fieldName}`);
      assert.ok(Number.isFinite(entryStartLayout[fieldName]), `non-finite layout field: ${fieldName}`);
    }
    assert.equal(entryStartLayout.virtualSlot, 0);
    assert.equal(entryStartLayout.appIndex, 0);
    assert.equal(entryStartLayout.centerX, DISPLAY_CENTER_PX);
    assert.equal(entryStartLayout.centerY, DISPLAY_CENTER_PX);

    const settledFocusedLayout = computeHomeGlassSlotLayout(0, 0, 1, 0, 0, 0);
    assert.equal(settledFocusedLayout.centerX, DISPLAY_CENTER_PX);
    assert.equal(settledFocusedLayout.centerY, HOME_CONSTANTS.CUBE_CENTER_Y);
    assert.equal(settledFocusedLayout.halfSize, HOME_CONSTANTS.FOCUSED_HALF_SIZE_PX);
    assert.equal(settledFocusedLayout.focusWeight, 1);
    assert.equal(settledFocusedLayout.alpha, 1);

    const neighborLayout = computeHomeGlassSlotLayout(1, 0, 1, 0, 0, 0);
    assert.equal(neighborLayout.appIndex, 1);
    assert.equal(neighborLayout.centerX, DISPLAY_CENTER_PX + HOME_CONSTANTS.WHEEL_SPACING_PX);
    assert.equal(neighborLayout.halfSize, HOME_CONSTANTS.NEIGHBOR_HALF_SIZE_PX);
    assert.equal(neighborLayout.focusWeight, 0);
    assert.equal(neighborLayout.alpha, 1);

    const distantLayout = computeHomeGlassSlotLayout(2, 0, 1, 0, 0, 0);
    assert.equal(distantLayout.halfSize, HOME_CONSTANTS.DISTANT_HALF_SIZE_PX);
    assert.equal(distantLayout.alpha, 0);
  });

  test("resolveHomeAppSubtitle formats state-aware mono telemetry for every Home app", () => {
    const nowPlayingSpec = HOME_APPS[0];
    const librarySpec = HOME_APPS[1];
    const modesSpec = HOME_APPS[2];
    const settingsSpec = HOME_APPS[3];

    assert.equal(
      resolveHomeAppSubtitle(nowPlayingSpec, { playbackState: "idle", appearance: "dark" }),
      "NOT PLAYING \u00b7 BROWSE LIBRARY",
    );
    assert.equal(
      resolveHomeAppSubtitle(nowPlayingSpec, {
        playbackState: "playing",
        trackTitle: "Midnight Groove",
        trackElapsedSeconds: 65,
        trackDurationSeconds: 185,
        appearance: "dark",
      }),
      "MIDNIGHT GROOVE \u00b7 01:05 / 03:05",
    );
    assert.equal(
      resolveHomeAppSubtitle(nowPlayingSpec, { playbackState: "storage", appearance: "dark" }),
      "USB STORAGE \u00b7 42.6 GB FREE",
    );

    assert.equal(
      resolveHomeAppSubtitle(librarySpec, { counts: { ...SAMPLE_LIBRARY_COUNTS } }),
      "1,461 TRACKS \u00b7 271 ALBUMS",
    );
    assert.equal(
      resolveHomeAppSubtitle(librarySpec, { counts: { ...EMPTY_LIBRARY_COUNTS } }),
      "EMPTY LIBRARY \u00b7 0 TRACKS",
    );

    assert.equal(resolveHomeAppSubtitle(modesSpec, {}), "STORAGE \u00b7 DAC \u00b7 WIRELESS");

    assert.equal(
      resolveHomeAppSubtitle(settingsSpec, { appearance: "dark" }),
      "DARK MODE \u00b7 VOL LIMIT 85%",
    );
    assert.equal(
      resolveHomeAppSubtitle(settingsSpec, { appearance: "light" }),
      "LIGHT MODE \u00b7 VOL LIMIT 85%",
    );
  });
});

describe("createHomeController nominal behavior", () => {
  test("enterFromBoot initializes slot 0 with zeroed transitions and settles entryProgress in 0.78s", () => {
    const homeController = createHomeController();
    homeController.enterFromBoot();

    const startSnapshot = homeController.getRenderSnapshot();
    assert.equal(startSnapshot.focusedApp.id, "now-playing");
    assert.equal(startSnapshot.entryProgress, 0);
    assert.equal(startSnapshot.launchTarget, 0);
    assert.equal(startSnapshot.launchProgress, 0);
    assert.equal(startSnapshot.launchedAppId, null);
    assert.equal(startSnapshot.backSwipeProgress, 0);

    const halfEntryDuration = HOME_CONSTANTS.ENTRY_DURATION_SECONDS / 2;
    homeController.step(halfEntryDuration);
    const midSnapshot = homeController.getRenderSnapshot();
    assert.ok(midSnapshot.entryProgress > 0 && midSnapshot.entryProgress < 1);

    homeController.step(halfEntryDuration);
    const settledSnapshot = homeController.getRenderSnapshot();
    assert.equal(settledSnapshot.entryProgress, 1);
  });

  test("infinite cyclic wheel wraps seamlessly across CW and CCW detents", () => {
    const homeController = createSettledHomeController();

    homeController.stepDetent(1);
    assert.equal(homeController.getRenderSnapshot().focusedApp.id, "library");

    homeController.stepDetent(1);
    assert.equal(homeController.getRenderSnapshot().focusedApp.id, "modes");

    homeController.stepDetent(1);
    assert.equal(homeController.getRenderSnapshot().focusedApp.id, "settings");

    homeController.stepDetent(1);
    assert.equal(homeController.getRenderSnapshot().focusedApp.id, "now-playing");

    const wrappedBackController = createSettledHomeController();
    wrappedBackController.stepDetent(-1);
    const wrappedSnapshot = wrappedBackController.getRenderSnapshot();
    assert.equal(wrappedSnapshot.focusedApp.id, "settings");
    assert.equal(wrapAppIndex(wrappedSnapshot.targetSlot), 3);
  });

  test("center tap launches focused app and reaches launchProgress 1 after 0.62s", () => {
    const homeController = createSettledHomeController();

    homeController.handlePointerDown(CENTER_TAP_X, CENTER_TAP_Y);
    homeController.handlePointerUp(CENTER_TAP_X, CENTER_TAP_Y);

    const launchedSnapshot = homeController.getRenderSnapshot();
    assert.equal(launchedSnapshot.launchTarget, 1);
    assert.equal(launchedSnapshot.launchedAppId, "now-playing");

    const halfLaunchDuration = HOME_CONSTANTS.LAUNCH_DURATION_SECONDS / 2;
    homeController.step(halfLaunchDuration);
    const midLaunchSnapshot = homeController.getRenderSnapshot();
    assert.ok(midLaunchSnapshot.launchProgress > 0 && midLaunchSnapshot.launchProgress < 1);

    homeController.step(halfLaunchDuration);
    const completeLaunchSnapshot = homeController.getRenderSnapshot();
    assert.equal(completeLaunchSnapshot.launchProgress, 1);
  });

  test("neighbor cube tap rotates wheel to slot 1 without launching", () => {
    const homeController = createSettledHomeController();
    const currentSnapshot = homeController.getRenderSnapshot();
    const neighborLayout = computeHomeGlassSlotLayout(
      1,
      currentSnapshot.scrollPosition,
      currentSnapshot.entryProgress,
      currentSnapshot.launchProgress,
      currentSnapshot.targetSlot,
      currentSnapshot.idleClockSeconds ?? 0,
    );

    homeController.handlePointerDown(neighborLayout.centerX, neighborLayout.centerY);
    homeController.handlePointerUp(neighborLayout.centerX, neighborLayout.centerY);

    const rotatedSnapshot = homeController.getRenderSnapshot();
    assert.equal(rotatedSnapshot.targetSlot, 1);
    assert.equal(rotatedSnapshot.focusedApp.id, "library");
    assert.equal(rotatedSnapshot.launchTarget, 0);
    assert.equal(rotatedSnapshot.launchedAppId, null);

    const settledSnapshot = advanceController(homeController, STEP_DELTA_SECONDS, SETTLE_STEP_COUNT);
    assert.ok(Math.abs(settledSnapshot.scrollPosition - 1) < FLOAT_TOLERANCE);
  });

  test("horizontal carousel drag shifts scrollPosition live and snaps to library on release", () => {
    const homeController = createSettledHomeController();

    homeController.handlePointerDown(CAROUSEL_DRAG_START_X, CAROUSEL_DRAG_Y);
    homeController.handlePointerMove(CAROUSEL_DRAG_MID_X, CAROUSEL_DRAG_Y);
    const midpointSnapshot = homeController.getRenderSnapshot();
    assert.ok(midpointSnapshot.scrollPosition > 0.25 && midpointSnapshot.scrollPosition < 0.75);

    homeController.handlePointerMove(CAROUSEL_DRAG_END_X, CAROUSEL_DRAG_Y);
    const endDragSnapshot = homeController.getRenderSnapshot();
    assert.ok(Math.abs(endDragSnapshot.scrollPosition - 1) < FLOAT_TOLERANCE);

    homeController.handlePointerUp(CAROUSEL_DRAG_END_X, CAROUSEL_DRAG_Y);
    const releasedSnapshot = homeController.getRenderSnapshot();
    assert.equal(releasedSnapshot.targetSlot, 1);
    assert.equal(releasedSnapshot.focusedApp.id, "library");
    assert.equal(releasedSnapshot.launchTarget, 0);

    const settledSnapshot = advanceController(homeController, STEP_DELTA_SECONDS, SETTLE_STEP_COUNT);
    assert.ok(Math.abs(settledSnapshot.scrollPosition - 1) < FLOAT_TOLERANCE);
  });

  test("BROWSE LIBRARY CTA tap and jumpFromNotPlayingToLibrary switch cleanly into launched Library", () => {
    const tapController = createSettledHomeController();
    tapController.handlePointerDown(CENTER_TAP_X, CENTER_TAP_Y);
    tapController.handlePointerUp(CENTER_TAP_X, CENTER_TAP_Y);
    tapController.step(HOME_CONSTANTS.LAUNCH_DURATION_SECONDS);

    tapController.handlePointerDown(CTA_TAP_X, CTA_TAP_Y);
    tapController.handlePointerUp(CTA_TAP_X, CTA_TAP_Y);

    const tapSwitchedSnapshot = tapController.getRenderSnapshot();
    assert.equal(tapSwitchedSnapshot.launchedAppId, "library");
    assert.equal(tapSwitchedSnapshot.focusedApp.id, "library");
    assert.equal(tapSwitchedSnapshot.launchTarget, 1);

    const directController = createSettledHomeController();
    directController.handlePointerDown(CENTER_TAP_X, CENTER_TAP_Y);
    directController.handlePointerUp(CENTER_TAP_X, CENTER_TAP_Y);
    directController.step(HOME_CONSTANTS.LAUNCH_DURATION_SECONDS);

    directController.jumpFromNotPlayingToLibrary();
    const directSnapshot = directController.getRenderSnapshot();
    assert.equal(directSnapshot.launchedAppId, "library");
    assert.equal(directSnapshot.focusedApp.id, "library");
    assert.equal(directSnapshot.launchTarget, 1);
  });

  test("left-edge rightward swipe >= 0.45 commits back-swipe and decays launchProgress to 0", () => {
    const homeController = createSettledHomeController();
    homeController.handlePointerDown(CENTER_TAP_X, CENTER_TAP_Y);
    homeController.handlePointerUp(CENTER_TAP_X, CENTER_TAP_Y);
    homeController.step(HOME_CONSTANTS.LAUNCH_DURATION_SECONDS);

    homeController.handlePointerDown(BACK_SWIPE_START_X, BACK_SWIPE_Y);
    homeController.handlePointerMove(BACK_SWIPE_COMMIT_END_X, BACK_SWIPE_Y);
    const draggingSnapshot = homeController.getRenderSnapshot();
    assert.ok(draggingSnapshot.backSwipeProgress >= HOME_CONSTANTS.BACK_SWIPE_COMMIT_THRESHOLD);

    homeController.handlePointerUp(BACK_SWIPE_COMMIT_END_X, BACK_SWIPE_Y);
    const closedSnapshot = homeController.getRenderSnapshot();
    assert.equal(closedSnapshot.launchTarget, 0);
    assert.equal(closedSnapshot.launchedAppId, null);

    homeController.step(HOME_CONSTANTS.LAUNCH_DURATION_SECONDS);
    const settledSnapshot = homeController.getRenderSnapshot();
    assert.equal(settledSnapshot.launchProgress, 0);
    assert.equal(settledSnapshot.backSwipeProgress, 0);
  });

  test("returnFromScreen('now-playing') sets a 20-degree smooth transition rotation angle that settles cleanly for playing and paused active tracks", () => {
    // 1. Playing state with active track
    const playingHomeController = createSettledHomeController();
    playingHomeController.syncPlaybackState({
      playbackState: "playing",
      hasActiveTrack: true,
    });
    playingHomeController.selectAppById("now-playing");
    playingHomeController.launchFocusedApp();
    playingHomeController.step(HOME_CONSTANTS.LAUNCH_DURATION_SECONDS);

    playingHomeController.returnFromScreen("now-playing");
    const playingReturnStart = playingHomeController.getRenderSnapshot();
    assert.equal(playingReturnStart.focusedApp.id, "now-playing");
    assert.equal(playingReturnStart.launchProgress, 1);
    assert.equal(playingReturnStart.launchTarget, 0);
    assert.ok(
      Math.abs(playingReturnStart.vinylNudgeRadians - TRANSITION_ROTATION_RADIANS) <
        FLOAT_TOLERANCE,
    );

    playingHomeController.step(HOME_CONSTANTS.LAUNCH_DURATION_SECONDS / 2);
    const playingReturnMid = playingHomeController.getRenderSnapshot();
    assert.ok(playingReturnMid.launchProgress > 0 && playingReturnMid.launchProgress < 1);
    assert.ok(
      playingReturnMid.vinylNudgeRadians > 0 &&
        playingReturnMid.vinylNudgeRadians < TRANSITION_ROTATION_RADIANS,
    );

    const playingSettled = advanceController(
      playingHomeController,
      STEP_DELTA_SECONDS,
      SETTLE_STEP_COUNT,
    );
    assert.equal(playingSettled.launchProgress, 0);
    assert.ok(Math.abs(playingSettled.vinylNudgeRadians) < TRANSITION_SETTLE_TOLERANCE);

    // 2. Paused state with active track
    const pausedHomeController = createSettledHomeController();
    pausedHomeController.syncPlaybackState({
      playbackState: "paused",
      hasActiveTrack: true,
    });
    pausedHomeController.selectAppById("now-playing");
    pausedHomeController.launchFocusedApp();
    pausedHomeController.step(HOME_CONSTANTS.LAUNCH_DURATION_SECONDS);

    pausedHomeController.returnFromScreen("now-playing");
    const pausedReturnStart = pausedHomeController.getRenderSnapshot();
    assert.equal(pausedReturnStart.focusedApp.id, "now-playing");
    assert.equal(pausedReturnStart.launchProgress, 1);
    assert.equal(pausedReturnStart.launchTarget, 0);
    assert.ok(
      Math.abs(pausedReturnStart.vinylNudgeRadians - TRANSITION_ROTATION_RADIANS) <
        FLOAT_TOLERANCE,
    );

    pausedHomeController.step(HOME_CONSTANTS.LAUNCH_DURATION_SECONDS / 2);
    const pausedReturnMid = pausedHomeController.getRenderSnapshot();
    assert.ok(pausedReturnMid.launchProgress > 0 && pausedReturnMid.launchProgress < 1);
    assert.ok(
      pausedReturnMid.vinylNudgeRadians > 0 &&
        pausedReturnMid.vinylNudgeRadians < TRANSITION_ROTATION_RADIANS,
    );

    const pausedSettled = advanceController(
      pausedHomeController,
      STEP_DELTA_SECONDS,
      SETTLE_STEP_COUNT,
    );
    assert.equal(pausedSettled.launchProgress, 0);
    assert.ok(Math.abs(pausedSettled.vinylNudgeRadians) < TRANSITION_SETTLE_TOLERANCE);

    // 3. Idle state with no active track returns cleanly without vinyl spin rotation offset
    const idleHomeController = createSettledHomeController();
    idleHomeController.syncPlaybackState({
      playbackState: "idle",
      hasActiveTrack: false,
    });
    idleHomeController.selectAppById("now-playing");
    idleHomeController.launchFocusedApp();
    idleHomeController.step(HOME_CONSTANTS.LAUNCH_DURATION_SECONDS);

    idleHomeController.returnFromScreen("now-playing");
    const idleReturnStart = idleHomeController.getRenderSnapshot();
    assert.equal(idleReturnStart.launchProgress, 1);
    assert.equal(idleReturnStart.vinylNudgeRadians, 0);
    idleHomeController.step(HOME_CONSTANTS.LAUNCH_DURATION_SECONDS);
    assert.equal(idleHomeController.getRenderSnapshot().launchProgress, 0);
  });
});

describe("createHomeController boundaries and negative guards", () => {
  test("ignores negative and non-finite step deltas without producing NaN", () => {
    const homeController = createHomeController();
    homeController.enterFromBoot();

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
      homeController.step(invalidDelta);
      const snapshot = homeController.getRenderSnapshot();
      assert.equal(snapshot.entryProgress, 0);
      assert.equal(snapshot.launchProgress, 0);
      assert.equal(snapshot.scrollPosition, 0);
      assert.equal(snapshot.backSwipeProgress, 0);
      assert.ok(Number.isFinite(snapshot.entryProgress));
      assert.ok(Number.isFinite(snapshot.launchProgress));
      assert.ok(Number.isFinite(snapshot.scrollPosition));
      assert.ok(Number.isFinite(snapshot.backSwipeProgress));
    }

    homeController.step(HOME_CONSTANTS.ENTRY_DURATION_SECONDS);
    assert.equal(homeController.getRenderSnapshot().entryProgress, 1);
  });

  test("normalizes scrollPosition into [0, 3] after 20 CW detents and settling", () => {
    const homeController = createSettledHomeController();

    for (let detentStep = 0; detentStep < WHEEL_STRESS_DETENT_COUNT; detentStep += 1) {
      homeController.stepDetent(1);
    }

    const settledSnapshot = advanceController(homeController, STEP_DELTA_SECONDS, SETTLE_STEP_COUNT);
    assert.ok(Math.abs(settledSnapshot.scrollPosition) < HOME_APPS.length);
    assert.ok(settledSnapshot.scrollPosition >= 0 && settledSnapshot.scrollPosition <= 3);
    assert.equal(settledSnapshot.focusedApp.id, "now-playing");
  });

  test("partial back-swipe < 0.45 keeps app launched and decays backSwipeProgress back to 0", () => {
    const homeController = createSettledHomeController();
    homeController.handlePointerDown(CENTER_TAP_X, CENTER_TAP_Y);
    homeController.handlePointerUp(CENTER_TAP_X, CENTER_TAP_Y);
    homeController.step(HOME_CONSTANTS.LAUNCH_DURATION_SECONDS);

    homeController.handlePointerDown(BACK_SWIPE_START_X, BACK_SWIPE_Y);
    homeController.handlePointerMove(BACK_SWIPE_PARTIAL_END_X, BACK_SWIPE_Y);
    const partialSwipeSnapshot = homeController.getRenderSnapshot();
    assert.ok(partialSwipeSnapshot.backSwipeProgress > 0);
    assert.ok(partialSwipeSnapshot.backSwipeProgress < HOME_CONSTANTS.BACK_SWIPE_COMMIT_THRESHOLD);

    homeController.handlePointerUp(BACK_SWIPE_PARTIAL_END_X, BACK_SWIPE_Y);
    const unreleasedAppSnapshot = homeController.getRenderSnapshot();
    assert.equal(unreleasedAppSnapshot.launchTarget, 1);
    assert.equal(unreleasedAppSnapshot.launchedAppId, "now-playing");

    const decayedSnapshot = advanceController(homeController, STEP_DELTA_SECONDS, SETTLE_STEP_COUNT);
    assert.equal(decayedSnapshot.backSwipeProgress, 0);
    assert.equal(decayedSnapshot.launchTarget, 1);
    assert.equal(decayedSnapshot.launchProgress, 1);
  });

  test("ignores stepDetent and selectSlot while an app is launched", () => {
    const homeController = createSettledHomeController();
    homeController.handlePointerDown(CENTER_TAP_X, CENTER_TAP_Y);
    homeController.handlePointerUp(CENTER_TAP_X, CENTER_TAP_Y);

    homeController.stepDetent(1);
    homeController.stepDetent(-1);
    homeController.selectSlot(2);

    const guardedSnapshot = homeController.getRenderSnapshot();
    assert.equal(guardedSnapshot.targetSlot, 0);
    assert.equal(guardedSnapshot.focusedApp.id, "now-playing");
    assert.equal(guardedSnapshot.launchTarget, 1);
  });

  test("treats selectAppById with unknown or invalid ids as a safe no-op", () => {
    const homeController = createSettledHomeController();

    homeController.selectAppById("nonexistent");
    homeController.selectAppById("");
    homeController.selectAppById(null);

    const snapshot = homeController.getRenderSnapshot();
    assert.equal(snapshot.targetSlot, 0);
    assert.equal(snapshot.focusedApp.id, "now-playing");
    assert.equal(snapshot.launchTarget, 0);
    assert.equal(snapshot.launchedAppId, null);
  });

  test("handlePointerCancel resets pointer state without committing taps or back-swipes", () => {
    const carouselController = createSettledHomeController();
    carouselController.handlePointerDown(CENTER_TAP_X, CENTER_TAP_Y);
    assert.equal(carouselController.getRenderSnapshot().pointerActive, true);

    carouselController.handlePointerCancel();
    const cancelledCarouselSnapshot = carouselController.getRenderSnapshot();
    assert.equal(cancelledCarouselSnapshot.pointerActive, false);
    assert.equal(cancelledCarouselSnapshot.pointerMode, "none");
    assert.equal(cancelledCarouselSnapshot.launchTarget, 0);
    assert.equal(cancelledCarouselSnapshot.launchedAppId, null);

    const launchedController = createSettledHomeController();
    launchedController.handlePointerDown(CENTER_TAP_X, CENTER_TAP_Y);
    launchedController.handlePointerUp(CENTER_TAP_X, CENTER_TAP_Y);
    launchedController.step(HOME_CONSTANTS.LAUNCH_DURATION_SECONDS);

    launchedController.handlePointerDown(BACK_SWIPE_START_X, BACK_SWIPE_Y);
    launchedController.handlePointerMove(BACK_SWIPE_COMMIT_END_X, BACK_SWIPE_Y);
    assert.equal(launchedController.getRenderSnapshot().pointerActive, true);

    launchedController.handlePointerCancel();
    const cancelledSwipeSnapshot = launchedController.getRenderSnapshot();
    assert.equal(cancelledSwipeSnapshot.pointerActive, false);
    assert.equal(cancelledSwipeSnapshot.pointerMode, "none");
    assert.equal(cancelledSwipeSnapshot.launchTarget, 1);
    assert.equal(cancelledSwipeSnapshot.launchedAppId, "now-playing");
  });
});
