import { describe, test } from "node:test";
import assert from "node:assert/strict";

import { BOOT_TIMING, createBootController } from "../core/boot_controller.js";
import { createRouter } from "../core/router.js";

const STEP_DELTA_SECONDS = 0.05;
const FINE_STEP_DELTA_SECONDS = 0.01;
const NOMINAL_READY_AT_SECONDS = 0.7;
const NOMINAL_COMPLETE_MIN_SECONDS = 9.65;
const NOMINAL_COMPLETE_MAX_SECONDS = 9.95;
const SLOW_HOLD_SECONDS = 8.2;
const PRE_TIMEOUT_SECONDS = 11.5;
const POST_TIMEOUT_BUFFER_MARGIN_SECONDS = 1.5;
const MAX_SIMULATION_STEPS = 1200;
const TRANSITION_STEP_SECONDS = 0.1;
const MAX_TRANSITION_STEPS = 50;

const SWEEP_AUDIO_INIT_BAND_START = 0.20;
const SWEEP_TRACKS_BAND_START = 0.42;
const SWEEP_INDEX_BAND_START = 0.68;
const SWEEP_READY_BAND_START = 0.92;

const SAMPLE_DISCOVERED_FILES = 1461;
const SAMPLE_SCANNED_FILES = 420;
const SAMPLE_SLOW_SCANNED_FILES = 730;
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

const ORDERED_BOOT_PHASES = Object.freeze([
  "wake",
  "buffer",
  "signal-lock",
  "handoff",
  "completed",
]);

/**
 * Build a ready backend status snapshot with optional overrides.
 *
 * @param {object} overrides Optional fields to override on the ready payload.
 * @returns {object} Backend status payload suitable for ingestBackendStatus().
 */
function buildReadyStatus(overrides = {}) {
  return {
    phase: "ready",
    ready: true,
    discoveredFiles: SAMPLE_DISCOVERED_FILES,
    scannedFiles: SAMPLE_DISCOVERED_FILES,
    progress: 1.0,
    musicRootExists: true,
    counts: { ...SAMPLE_LIBRARY_COUNTS },
    error: null,
    ...overrides,
  };
}

/**
 * Advance a boot controller in fixed steps until a predicate is met or step limit expires.
 *
 * @param {object} bootController Controller instance under test.
 * @param {number} stepDeltaSeconds Step increment in seconds.
 * @param {number} maxSteps Maximum number of steps to run.
 * @param {(snapshot: object, stepIndex: number) => boolean} stopPredicate Predicate returning true to stop.
 * @returns {object[]} Collected render snapshots after each step.
 */
function stepWhile(bootController, stepDeltaSeconds, maxSteps, stopPredicate) {
  const snapshots = [];
  for (let stepIndex = 0; stepIndex < maxSteps; stepIndex += 1) {
    bootController.step(stepDeltaSeconds);
    const renderSnapshot = bootController.getRenderSnapshot();
    snapshots.push(renderSnapshot);
    if (stopPredicate(renderSnapshot, stepIndex)) {
      break;
    }
  }
  return snapshots;
}

/**
 * Deduplicate adjacent phase names while preserving transition order.
 *
 * @param {string[]} phaseTrace Sequence of sampled phase strings.
 * @returns {string[]} Deduplicated transition sequence.
 */
function collapsePhaseTransitions(phaseTrace) {
  const collapsed = [];
  for (const phaseName of phaseTrace) {
    if (collapsed.length === 0 || collapsed[collapsed.length - 1] !== phaseName) {
      collapsed.push(phaseName);
    }
  }
  return collapsed;
}

describe("BOOT_TIMING", () => {
  test("exports canonical boot act durations and stall thresholds", () => {
    assert.equal(BOOT_TIMING.WAKE_DURATION_SECONDS, 0.95);
    assert.equal(BOOT_TIMING.BUFFER_MIN_DURATION_SECONDS, 6.85);
    assert.equal(BOOT_TIMING.BUFFER_EXIT_DURATION_SECONDS, 0.35);
    assert.equal(BOOT_TIMING.SIGNAL_LOCK_DURATION_SECONDS, 1.05);
    assert.equal(BOOT_TIMING.HANDOFF_DURATION_SECONDS, 0.55);
    assert.equal(BOOT_TIMING.ENGINE_DELAY_TIMEOUT_SECONDS, 12.0);
    assert.equal(BOOT_TIMING.SWEEP_HOLD_CAP_BEFORE_READY, 0.95);
  });
});

describe("createBootController", () => {
  test("starts in wake phase with zeroed progress and complete render snapshot shape", () => {
    const bootController = createBootController();
    const initialSnapshot = bootController.getRenderSnapshot();

    assert.equal(initialSnapshot.phase, "wake");
    assert.equal(initialSnapshot.elapsedSeconds, 0);
    assert.equal(initialSnapshot.warningElapsedSeconds, 0);
    assert.equal(initialSnapshot.actProgress, 0);
    assert.equal(initialSnapshot.handoffProgress, 0);
    assert.equal(initialSnapshot.sweepProgress, 0);
    assert.equal(initialSnapshot.statusLabel, "MEM // CHECK");
    assert.equal(initialSnapshot.completed, false);

    const expectedFields = [
      "phase",
      "actProgress",
      "bufferExitProgress",
      "handoffProgress",
      "elapsedSeconds",
      "warningElapsedSeconds",
      "sweepProgress",
      "statusLabel",
      "summaryLine",
      "completed",
      "backendCounts",
      "musicRootExists",
    ];
    for (const fieldName of expectedFields) {
      assert.ok(fieldName in initialSnapshot, `missing snapshot field: ${fieldName}`);
    }
  });

  test("clamps negative and non-finite step deltas to zero without producing NaN", () => {
    const bootController = createBootController();
    const invalidDeltas = [-1, -0.05, Number.NaN, Number.POSITIVE_INFINITY, Number.NEGATIVE_INFINITY, undefined, null];

    for (const invalidDelta of invalidDeltas) {
      bootController.step(invalidDelta);
      const renderSnapshot = bootController.getRenderSnapshot();
      assert.equal(renderSnapshot.phase, "wake");
      assert.equal(renderSnapshot.elapsedSeconds, 0);
      assert.equal(renderSnapshot.sweepProgress, 0);
      assert.equal(renderSnapshot.actProgress, 0);
      assert.equal(renderSnapshot.handoffProgress, 0);
      assert.equal(renderSnapshot.warningElapsedSeconds, 0);
      assert.ok(Number.isFinite(renderSnapshot.elapsedSeconds));
      assert.ok(Number.isFinite(renderSnapshot.sweepProgress));
    }

    bootController.step(0.25);
    const recoveredSnapshot = bootController.getRenderSnapshot();
    assert.equal(recoveredSnapshot.elapsedSeconds, 0.25);
    assert.equal(recoveredSnapshot.phase, "wake");
  });

  test("reports Act 02 telemetry across idle, scanning, indexing, ready tracks, and system lock states", () => {
    const idleController = createBootController();
    stepWhile(
      idleController,
      STEP_DELTA_SECONDS,
      MAX_SIMULATION_STEPS,
      (snapshot) => snapshot.phase === "buffer",
    );
    assert.equal(idleController.getRenderSnapshot().statusLabel, "INIT // SYSTEM");

    idleController.ingestBackendStatus({
      phase: "scanning",
      ready: false,
      discoveredFiles: SAMPLE_DISCOVERED_FILES,
      scannedFiles: SAMPLE_SCANNED_FILES,
      progress: SAMPLE_SCANNED_FILES / SAMPLE_DISCOVERED_FILES,
      musicRootExists: true,
      counts: { ...EMPTY_LIBRARY_COUNTS },
      error: null,
    });
    assert.equal(idleController.getRenderSnapshot().statusLabel, "SCAN // 420 / 1,461");

    idleController.ingestBackendStatus({
      phase: "indexing",
      ready: false,
      discoveredFiles: SAMPLE_DISCOVERED_FILES,
      scannedFiles: SAMPLE_DISCOVERED_FILES,
      progress: 0.85,
      musicRootExists: true,
      counts: { ...SAMPLE_LIBRARY_COUNTS },
      error: null,
    });
    assert.equal(idleController.getRenderSnapshot().statusLabel, "INDEX // 271 ALBUMS");

    const readyController = createBootController();
    const bufferSnapshots = [];
    let sentReady = false;
    for (let stepIndex = 0; stepIndex < MAX_SIMULATION_STEPS; stepIndex += 1) {
      readyController.step(FINE_STEP_DELTA_SECONDS);
      const currentSnapshot = readyController.getRenderSnapshot();
      if (!sentReady && currentSnapshot.elapsedSeconds >= NOMINAL_READY_AT_SECONDS) {
        readyController.ingestBackendStatus(buildReadyStatus());
        sentReady = true;
      }
      if (currentSnapshot.phase === "buffer") {
        bufferSnapshots.push(readyController.getRenderSnapshot());
      } else if (currentSnapshot.phase === "signal-lock") {
        break;
      }
    }

    const audioInitBandSnapshots = bufferSnapshots.filter(
      (snapshot) =>
        snapshot.sweepProgress >= SWEEP_AUDIO_INIT_BAND_START &&
        snapshot.sweepProgress < SWEEP_TRACKS_BAND_START,
    );
    const tracksBandSnapshots = bufferSnapshots.filter(
      (snapshot) =>
        snapshot.sweepProgress >= SWEEP_TRACKS_BAND_START &&
        snapshot.sweepProgress < SWEEP_INDEX_BAND_START,
    );
    const indexBandSnapshots = bufferSnapshots.filter(
      (snapshot) =>
        snapshot.sweepProgress >= SWEEP_INDEX_BAND_START &&
        snapshot.sweepProgress < SWEEP_READY_BAND_START,
    );
    const lockBandSnapshots = bufferSnapshots.filter(
      (snapshot) => snapshot.sweepProgress >= SWEEP_READY_BAND_START,
    );

    assert.ok(audioInitBandSnapshots.length > 0, "expected buffer samples in audio init band [0.20, 0.42)");
    assert.ok(tracksBandSnapshots.length > 0, "expected buffer samples in tracks band [0.42, 0.68)");
    assert.ok(indexBandSnapshots.length > 0, "expected buffer samples in index band [0.68, 0.92)");
    assert.ok(lockBandSnapshots.length > 0, "expected buffer samples in lock band [0.92, 1.0]");

    for (const snapshot of audioInitBandSnapshots) {
      assert.equal(snapshot.statusLabel, "INIT // AUDIO ENGINE");
    }
    for (const snapshot of tracksBandSnapshots) {
      assert.equal(snapshot.statusLabel, "SCAN // 1,461 TRACKS");
    }
    for (const snapshot of indexBandSnapshots) {
      assert.equal(snapshot.statusLabel, "INDEX // 271 ALBUMS");
    }
    for (const snapshot of lockBandSnapshots) {
      assert.equal(snapshot.statusLabel, "LOCK // SYSTEM READY");
    }
  });

  test("formats Act 03 and Act 04 summaryLine for normal, empty, and missing library states", () => {
    const scenarios = [
      {
        label: "normal library",
        status: buildReadyStatus({
          musicRootExists: true,
          counts: { ...SAMPLE_LIBRARY_COUNTS },
        }),
        expectedSummary: "V1.0  \u00b7  LIB OK  \u00b7  1,461 TRACKS",
        expectedMusicRootExists: true,
        expectedTracks: 1461,
      },
      {
        label: "empty library",
        status: buildReadyStatus({
          discoveredFiles: 0,
          scannedFiles: 0,
          musicRootExists: true,
          counts: { ...EMPTY_LIBRARY_COUNTS },
        }),
        expectedSummary: "V1.0  \u00b7  EMPTY LIB  \u00b7  0 TRACKS",
        expectedMusicRootExists: true,
        expectedTracks: 0,
      },
      {
        label: "missing folder",
        status: buildReadyStatus({
          discoveredFiles: 0,
          scannedFiles: 0,
          musicRootExists: false,
          counts: { ...EMPTY_LIBRARY_COUNTS },
        }),
        expectedSummary: "V1.0  \u00b7  NO FOLDER  \u00b7  0 TRACKS",
        expectedMusicRootExists: false,
        expectedTracks: 0,
      },
    ];

    for (const scenario of scenarios) {
      const bootController = createBootController();
      bootController.ingestBackendStatus(scenario.status);

      const snapshots = stepWhile(
        bootController,
        STEP_DELTA_SECONDS,
        MAX_SIMULATION_STEPS,
        (snapshot) => snapshot.completed,
      );

      const signalLockSnapshots = snapshots.filter((snapshot) => snapshot.phase === "signal-lock");
      const handoffSnapshots = snapshots.filter((snapshot) => snapshot.phase === "handoff");

      assert.ok(signalLockSnapshots.length > 0, `${scenario.label}: expected signal-lock snapshots`);
      assert.ok(handoffSnapshots.length > 0, `${scenario.label}: expected handoff snapshots`);

      for (const snapshot of [...signalLockSnapshots, ...handoffSnapshots]) {
        assert.equal(snapshot.summaryLine, scenario.expectedSummary);
        assert.equal(snapshot.musicRootExists, scenario.expectedMusicRootExists);
        assert.equal(snapshot.backendCounts.tracks, scenario.expectedTracks);
      }
    }
  });

  test("progresses strictly wake -> buffer -> signal-lock -> handoff -> completed near 9.75s", () => {
    const bootController = createBootController();
    const phaseTrace = [bootController.getRenderSnapshot().phase];
    let previousSweepProgress = bootController.getRenderSnapshot().sweepProgress;
    let ingestedReady = false;
    let completionElapsedSeconds = null;

    for (let stepIndex = 0; stepIndex < MAX_SIMULATION_STEPS; stepIndex += 1) {
      bootController.step(STEP_DELTA_SECONDS);
      const currentSnapshot = bootController.getRenderSnapshot();

      if (!ingestedReady && currentSnapshot.elapsedSeconds >= NOMINAL_READY_AT_SECONDS) {
        bootController.ingestBackendStatus(buildReadyStatus());
        ingestedReady = true;
      }

      const refreshedSnapshot = bootController.getRenderSnapshot();
      assert.ok(
        refreshedSnapshot.sweepProgress >= previousSweepProgress,
        `sweepProgress decreased from ${previousSweepProgress} to ${refreshedSnapshot.sweepProgress}`,
      );
      assert.ok(refreshedSnapshot.sweepProgress <= 1, "sweepProgress must not exceed 1");
      previousSweepProgress = refreshedSnapshot.sweepProgress;
      phaseTrace.push(refreshedSnapshot.phase);

      if (refreshedSnapshot.completed) {
        completionElapsedSeconds = refreshedSnapshot.elapsedSeconds;
        break;
      }
    }

    assert.deepEqual(collapsePhaseTransitions(phaseTrace), ORDERED_BOOT_PHASES);
    assert.notEqual(completionElapsedSeconds, null, "boot should reach completed");
    assert.ok(
      completionElapsedSeconds >= NOMINAL_COMPLETE_MIN_SECONDS &&
        completionElapsedSeconds <= NOMINAL_COMPLETE_MAX_SECONDS,
      `expected completion near 4.50s, observed ${completionElapsedSeconds}s`,
    );
    assert.equal(bootController.getRenderSnapshot().sweepProgress, 1);
  });

  test("holds in buffer at <= 0.95 when backend stays slow past WAKE + BUFFER_MIN, then completes when ready", () => {
    const bootController = createBootController();
    const slowStatus = {
      phase: "scanning",
      ready: false,
      discoveredFiles: SAMPLE_DISCOVERED_FILES,
      scannedFiles: SAMPLE_SLOW_SCANNED_FILES,
      progress: 0.5,
      musicRootExists: true,
      counts: { ...EMPTY_LIBRARY_COUNTS },
      error: null,
    };

    const slowStepCount = Math.round(SLOW_HOLD_SECONDS / STEP_DELTA_SECONDS);
    for (let stepIndex = 0; stepIndex < slowStepCount; stepIndex += 1) {
      bootController.step(STEP_DELTA_SECONDS);
      bootController.ingestBackendStatus(slowStatus);
      const snapshot = bootController.getRenderSnapshot();
      assert.notEqual(snapshot.phase, "signal-lock");
      assert.ok(snapshot.sweepProgress <= BOOT_TIMING.SWEEP_HOLD_CAP_BEFORE_READY);
    }

    const heldSnapshot = bootController.getRenderSnapshot();
    assert.equal(heldSnapshot.phase, "buffer");
    assert.equal(heldSnapshot.completed, false);
    assert.ok(heldSnapshot.sweepProgress <= BOOT_TIMING.SWEEP_HOLD_CAP_BEFORE_READY);

    bootController.ingestBackendStatus({
      phase: "ready",
      ready: true,
      progress: 1.0,
      musicRootExists: true,
      counts: { ...SAMPLE_LIBRARY_COUNTS },
    });

    const postReadyPhases = [];
    let previousSweep = heldSnapshot.sweepProgress;
    for (let stepIndex = 0; stepIndex < MAX_SIMULATION_STEPS; stepIndex += 1) {
      bootController.step(STEP_DELTA_SECONDS);
      const snapshot = bootController.getRenderSnapshot();
      assert.ok(snapshot.sweepProgress >= previousSweep);
      previousSweep = snapshot.sweepProgress;
      postReadyPhases.push(snapshot.phase);
      if (snapshot.completed) {
        break;
      }
    }

    const transitionSequence = collapsePhaseTransitions(postReadyPhases);
    assert.ok(transitionSequence.includes("signal-lock"));
    assert.ok(transitionSequence.includes("handoff"));
    assert.equal(transitionSequence[transitionSequence.length - 1], "completed");
    assert.equal(bootController.getRenderSnapshot().sweepProgress, 1);
    assert.equal(bootController.getRenderSnapshot().completed, true);
  });

  test("transitions to warning after ENGINE_DELAY_TIMEOUT_SECONDS stall and finishes boot on recovery", () => {
    const bootController = createBootController();
    const stalledStatus = {
      phase: "scanning",
      ready: false,
      discoveredFiles: SAMPLE_DISCOVERED_FILES,
      scannedFiles: 100,
      progress: 0.1,
      musicRootExists: true,
      counts: { ...EMPTY_LIBRARY_COUNTS },
      error: null,
    };

    const preTimeoutSteps = Math.round(PRE_TIMEOUT_SECONDS / STEP_DELTA_SECONDS);
    for (let stepIndex = 0; stepIndex < preTimeoutSteps; stepIndex += 1) {
      bootController.step(STEP_DELTA_SECONDS);
      bootController.ingestBackendStatus(stalledStatus);
    }
    assert.equal(bootController.getRenderSnapshot().phase, "buffer");
    assert.equal(bootController.getRenderSnapshot().warningElapsedSeconds, 0);

    const pastTimeoutSeconds =
      BOOT_TIMING.ENGINE_DELAY_TIMEOUT_SECONDS + POST_TIMEOUT_BUFFER_MARGIN_SECONDS;
    const stepsToTimeout = Math.round(
      (pastTimeoutSeconds - PRE_TIMEOUT_SECONDS) / STEP_DELTA_SECONDS,
    );
    for (let stepIndex = 0; stepIndex < stepsToTimeout; stepIndex += 1) {
      bootController.step(STEP_DELTA_SECONDS);
      bootController.ingestBackendStatus(stalledStatus);
    }

    const warningStartSnapshot = bootController.getRenderSnapshot();
    assert.equal(warningStartSnapshot.phase, "warning");
    const firstWarningElapsed = warningStartSnapshot.warningElapsedSeconds;

    bootController.step(0.5);
    const warningLaterSnapshot = bootController.getRenderSnapshot();
    assert.equal(warningLaterSnapshot.phase, "warning");
    assert.ok(
      warningLaterSnapshot.warningElapsedSeconds > firstWarningElapsed,
      "warningElapsedSeconds should increment live during warning",
    );

    bootController.ingestBackendStatus(buildReadyStatus());
    const recoverySnapshots = stepWhile(
      bootController,
      STEP_DELTA_SECONDS,
      MAX_SIMULATION_STEPS,
      (snapshot) => snapshot.completed,
    );

    const finalSnapshot = recoverySnapshots[recoverySnapshots.length - 1];
    assert.equal(finalSnapshot.completed, true);
    assert.equal(finalSnapshot.phase, "completed");
    assert.equal(finalSnapshot.sweepProgress, 1);

    const recoveryPhases = collapsePhaseTransitions(
      recoverySnapshots.map((snapshot) => snapshot.phase),
    );
    assert.equal(recoveryPhases[recoveryPhases.length - 1], "completed");
    assert.ok(recoveryPhases.includes("signal-lock"));
    assert.ok(recoveryPhases.includes("handoff"));
  });

  test("enters warning on setSimulatedDelay(true), advances warning timer, and recovers cleanly when cleared", () => {
    const bootController = createBootController();
    bootController.ingestBackendStatus(buildReadyStatus());

    stepWhile(
      bootController,
      STEP_DELTA_SECONDS,
      MAX_SIMULATION_STEPS,
      (snapshot) => snapshot.phase === "buffer",
    );
    assert.equal(bootController.getRenderSnapshot().phase, "buffer");

    bootController.setSimulatedDelay(true);
    assert.equal(bootController.getRenderSnapshot().phase, "warning");

    bootController.step(0.25);
    const firstWarningSeconds = bootController.getRenderSnapshot().warningElapsedSeconds;
    bootController.step(0.25);
    const secondWarningSeconds = bootController.getRenderSnapshot().warningElapsedSeconds;
    assert.ok(firstWarningSeconds > 0);
    assert.ok(secondWarningSeconds > firstWarningSeconds);

    bootController.setSimulatedDelay(false);
    const resumedSnapshot = bootController.getRenderSnapshot();
    assert.equal(resumedSnapshot.phase, "buffer");

    const postClearSnapshots = stepWhile(
      bootController,
      STEP_DELTA_SECONDS,
      MAX_SIMULATION_STEPS,
      (snapshot) => snapshot.completed,
    );
    const resumedPhases = collapsePhaseTransitions([
      resumedSnapshot.phase,
      ...postClearSnapshots.map((snapshot) => snapshot.phase),
    ]);

    assert.deepEqual(resumedPhases, ["buffer", "signal-lock", "handoff", "completed"]);
    assert.equal(bootController.getRenderSnapshot().completed, true);
  });

  test("enters warning on error snapshot and resumes buffer polling on retry()", () => {
    const bootController = createBootController();
    stepWhile(
      bootController,
      STEP_DELTA_SECONDS,
      MAX_SIMULATION_STEPS,
      (snapshot) => snapshot.phase === "buffer",
    );

    bootController.ingestBackendStatus({ error: "scan_failed", ready: false });
    assert.equal(bootController.getRenderSnapshot().phase, "warning");

    bootController.step(0.3);
    assert.equal(bootController.getRenderSnapshot().phase, "warning");
    assert.ok(bootController.getRenderSnapshot().warningElapsedSeconds > 0);

    bootController.retry();
    const retriedSnapshot = bootController.getRenderSnapshot();
    assert.equal(retriedSnapshot.phase, "buffer");
    assert.equal(retriedSnapshot.warningElapsedSeconds, 0);

    bootController.step(STEP_DELTA_SECONDS);
    assert.equal(bootController.getRenderSnapshot().phase, "buffer");

    bootController.ingestBackendStatus(buildReadyStatus());
    const finishSnapshots = stepWhile(
      bootController,
      STEP_DELTA_SECONDS,
      MAX_SIMULATION_STEPS,
      (snapshot) => snapshot.completed,
    );
    assert.equal(finishSnapshots[finishSnapshots.length - 1].completed, true);
  });
});

describe("createRouter", () => {
  test("initializes on boot by default and respects initialScreen override", () => {
    const defaultRouter = createRouter();
    const defaultState = defaultRouter.getState();

    assert.equal(defaultState.activeScreen, "boot");
    assert.ok("previousScreen" in defaultState);
    assert.ok("handoffProgress" in defaultState);
    assert.ok("entryProgress" in defaultState);
    assert.ok(Number.isFinite(defaultState.handoffProgress));
    assert.ok(Number.isFinite(defaultState.entryProgress));

    const homeRouter = createRouter({ initialScreen: "home" });
    assert.equal(homeRouter.getState().activeScreen, "home");
  });

  test("switches boot -> home with entryProgress 0, advances toward 1, and resets on reboot()", () => {
    const screenRouter = createRouter({ initialScreen: "boot" });

    screenRouter.completeBootToHome();
    const postHandoffState = screenRouter.getState();
    assert.equal(postHandoffState.activeScreen, "home");
    assert.equal(postHandoffState.previousScreen, "boot");
    assert.equal(postHandoffState.entryProgress, 0);

    screenRouter.stepTransition(-0.2);
    screenRouter.stepTransition(Number.NaN);
    assert.equal(screenRouter.getState().entryProgress, 0);

    let previousEntryProgress = 0;
    for (let stepIndex = 0; stepIndex < MAX_TRANSITION_STEPS; stepIndex += 1) {
      screenRouter.stepTransition(TRANSITION_STEP_SECONDS);
      const transitionState = screenRouter.getState();
      assert.ok(transitionState.entryProgress >= previousEntryProgress);
      assert.ok(transitionState.entryProgress <= 1);
      previousEntryProgress = transitionState.entryProgress;
      if (transitionState.entryProgress === 1) {
        break;
      }
    }

    assert.equal(screenRouter.getState().entryProgress, 1);

    screenRouter.reboot();
    const rebootedState = screenRouter.getState();
    assert.equal(rebootedState.activeScreen, "boot");
    assert.equal(rebootedState.entryProgress, 0);
  });
});
