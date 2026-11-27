/**
 * Pure deterministic boot state machine driving Acts 01-04 and Engine Delay /
 * error warning recovery for the DialOS simulator.
 */

import { clamp, formatIntegerCount } from "./geometry.js";

export const BOOT_TIMING = Object.freeze({
  WAKE_DURATION_SECONDS: 0.95,
  BUFFER_MIN_DURATION_SECONDS: 6.85,
  BUFFER_EXIT_DURATION_SECONDS: 0.35,
  SIGNAL_LOCK_DURATION_SECONDS: 1.05,
  HANDOFF_DURATION_SECONDS: 0.55,
  ENGINE_DELAY_TIMEOUT_SECONDS: 12.0,
  SWEEP_HOLD_CAP_BEFORE_READY: 0.95,
});

const SWEEP_AUDIO_INIT_BAND_START = 0.20;
const SWEEP_TRACKS_BAND_START = 0.42;
const SWEEP_INDEX_BAND_START = 0.68;
const SWEEP_READY_BAND_START = 0.92;
const SWEEP_CATCHUP_RATE_PER_SECOND = 0.65;

function createEmptyCounts() {
  return {
    tracks: 0,
    albums: 0,
    artists: 0,
    genres: 0,
  };
}

function createInitialBackendStatus() {
  return {
    phase: "idle",
    ready: false,
    discoveredFiles: 0,
    scannedFiles: 0,
    progress: 0,
    musicRootExists: true,
    counts: createEmptyCounts(),
    error: null,
  };
}

function cloneCounts(counts) {
  const sourceCounts = counts && typeof counts === "object" ? counts : {};
  return {
    tracks: Number.isFinite(sourceCounts.tracks) ? Math.max(0, sourceCounts.tracks) : 0,
    albums: Number.isFinite(sourceCounts.albums) ? Math.max(0, sourceCounts.albums) : 0,
    artists: Number.isFinite(sourceCounts.artists) ? Math.max(0, sourceCounts.artists) : 0,
    genres: Number.isFinite(sourceCounts.genres) ? Math.max(0, sourceCounts.genres) : 0,
  };
}

function normalizeBackendStatus(incomingStatus, currentStatus) {
  if (!incomingStatus || typeof incomingStatus !== "object") {
    return currentStatus;
  }

  return {
    phase: typeof incomingStatus.phase === "string" ? incomingStatus.phase : currentStatus.phase,
    ready:
      typeof incomingStatus.ready === "boolean"
        ? incomingStatus.ready
        : Boolean(currentStatus.ready),
    discoveredFiles: Number.isFinite(incomingStatus.discoveredFiles)
      ? Math.max(0, incomingStatus.discoveredFiles)
      : currentStatus.discoveredFiles,
    scannedFiles: Number.isFinite(incomingStatus.scannedFiles)
      ? Math.max(0, incomingStatus.scannedFiles)
      : currentStatus.scannedFiles,
    progress: Number.isFinite(incomingStatus.progress)
      ? clamp(incomingStatus.progress, 0, 1)
      : currentStatus.progress,
    musicRootExists:
      typeof incomingStatus.musicRootExists === "boolean"
        ? incomingStatus.musicRootExists
        : currentStatus.musicRootExists,
    counts: incomingStatus.counts ? cloneCounts(incomingStatus.counts) : cloneCounts(currentStatus.counts),
    error: incomingStatus.error !== undefined ? incomingStatus.error : null,
  };
}

function formatBufferStatusLabel(backendStatus, sweepProgress) {
  if (backendStatus.ready) {
    if (sweepProgress >= SWEEP_READY_BAND_START) {
      return "LOCK // SYSTEM READY";
    }
    if (sweepProgress >= SWEEP_INDEX_BAND_START) {
      return `INDEX // ${formatIntegerCount(backendStatus.counts.albums)} ALBUMS`;
    }
    if (sweepProgress >= SWEEP_TRACKS_BAND_START) {
      return `SCAN // ${formatIntegerCount(backendStatus.counts.tracks)} TRACKS`;
    }
    if (sweepProgress >= SWEEP_AUDIO_INIT_BAND_START) {
      return "INIT // AUDIO ENGINE";
    }
    return "INIT // SYSTEM";
  }

  if (backendStatus.phase === "indexing") {
    return `INDEX // ${formatIntegerCount(backendStatus.counts.albums)} ALBUMS`;
  }
  if (
    backendStatus.discoveredFiles > 0 &&
    backendStatus.scannedFiles < backendStatus.discoveredFiles
  ) {
    return `SCAN // ${formatIntegerCount(backendStatus.scannedFiles)} / ${formatIntegerCount(
      backendStatus.discoveredFiles,
    )}`;
  }
  if (backendStatus.phase === "discovering") {
    return "SCAN // DISCOVERING";
  }
  return "INIT // SYSTEM";
}

function formatSummaryLine(backendStatus) {
  const formattedTracks = formatIntegerCount(backendStatus.counts.tracks);
  if (!backendStatus.musicRootExists) {
    return `V1.0  \u00b7  NO FOLDER  \u00b7  ${formattedTracks} TRACKS`;
  }
  if (backendStatus.counts.tracks === 0) {
    return `V1.0  \u00b7  EMPTY LIB  \u00b7  ${formattedTracks} TRACKS`;
  }
  return `V1.0  \u00b7  LIB OK  \u00b7  ${formattedTracks} TRACKS`;
}

/**
 * Create an isolated boot state machine controller.
 *
 * @returns {{
 *   step: (deltaSeconds: number) => void,
 *   ingestBackendStatus: (incomingStatus: object) => void,
 *   setSimulatedDelay: (enabled: boolean) => void,
 *   retry: () => void,
 *   getRenderSnapshot: () => object
 * }} Boot controller API.
 */
export function createBootController() {
  let phase = "wake";
  let phaseBeforeWarning = "wake";
  let totalElapsedSeconds = 0;
  let phaseElapsedSeconds = 0;
  let bufferElapsedSeconds = 0;
  let bufferExitElapsedSeconds = 0;
  let stallTimerSeconds = 0;
  let warningElapsedSeconds = 0;
  let sweepProgress = 0;
  let simulatedDelayActive = false;
  let backendStatus = createInitialBackendStatus();

  function updateBufferSweepProgress(deltaSeconds) {
    const bufferTimeRatio = clamp(
      bufferElapsedSeconds / BOOT_TIMING.BUFFER_MIN_DURATION_SECONDS,
      0,
      1,
    );

    if (!backendStatus.ready) {
      const pacedBeforeReady = bufferTimeRatio * BOOT_TIMING.SWEEP_HOLD_CAP_BEFORE_READY;
      const backendBeforeReady =
        clamp(backendStatus.progress, 0, 1) * BOOT_TIMING.SWEEP_HOLD_CAP_BEFORE_READY;
      const cappedTarget = Math.min(
        BOOT_TIMING.SWEEP_HOLD_CAP_BEFORE_READY,
        Math.max(pacedBeforeReady, backendBeforeReady),
      );
      sweepProgress = clamp(
        Math.max(sweepProgress, cappedTarget),
        0,
        BOOT_TIMING.SWEEP_HOLD_CAP_BEFORE_READY,
      );
      return;
    }

    if (bufferElapsedSeconds < BOOT_TIMING.BUFFER_MIN_DURATION_SECONDS) {
      const catchupCap =
        sweepProgress +
        Math.max(
          deltaSeconds * SWEEP_CATCHUP_RATE_PER_SECOND,
          deltaSeconds / BOOT_TIMING.BUFFER_MIN_DURATION_SECONDS,
        );
      const nextTarget = Math.min(bufferTimeRatio, catchupCap);
      sweepProgress = clamp(Math.max(sweepProgress, nextTarget), 0, 1);
      return;
    }

    sweepProgress = clamp(
      Math.max(sweepProgress, sweepProgress + deltaSeconds * SWEEP_CATCHUP_RATE_PER_SECOND),
      0,
      1,
    );
  }

  function advanceWakePhase(remainingDelta) {
    const nextWakeElapsed = phaseElapsedSeconds + remainingDelta;
    if (nextWakeElapsed >= BOOT_TIMING.WAKE_DURATION_SECONDS) {
      const wakeUsed = Math.max(0, BOOT_TIMING.WAKE_DURATION_SECONDS - phaseElapsedSeconds);
      if (!backendStatus.ready) {
        stallTimerSeconds += wakeUsed;
      }
      phase = "buffer";
      phaseElapsedSeconds = 0;
      bufferElapsedSeconds = 0;
      bufferExitElapsedSeconds = 0;
      return Math.max(0, nextWakeElapsed - BOOT_TIMING.WAKE_DURATION_SECONDS);
    }

    if (!backendStatus.ready) {
      stallTimerSeconds += remainingDelta;
    }
    phaseElapsedSeconds = nextWakeElapsed;
    return 0;
  }

  function advanceBufferPhase(remainingDelta) {
    if (!backendStatus.ready) {
      stallTimerSeconds += remainingDelta;
      if (stallTimerSeconds >= BOOT_TIMING.ENGINE_DELAY_TIMEOUT_SECONDS) {
        const overflowPastTimeout = stallTimerSeconds - BOOT_TIMING.ENGINE_DELAY_TIMEOUT_SECONDS;
        phaseBeforeWarning = "buffer";
        phase = "warning";
        warningElapsedSeconds = overflowPastTimeout;
        return null;
      }
    }

    const previousBufferElapsed = bufferElapsedSeconds;
    const previousSweepProgress = sweepProgress;
    bufferElapsedSeconds += remainingDelta;
    updateBufferSweepProgress(remainingDelta);

    if (
      backendStatus.ready &&
      bufferElapsedSeconds >= BOOT_TIMING.BUFFER_MIN_DURATION_SECONDS &&
      sweepProgress >= 1
    ) {
      let exitDelta = remainingDelta;
      if (previousBufferElapsed < BOOT_TIMING.BUFFER_MIN_DURATION_SECONDS) {
        exitDelta = bufferElapsedSeconds - BOOT_TIMING.BUFFER_MIN_DURATION_SECONDS;
      } else if (previousSweepProgress < 1) {
        exitDelta = 0;
      }
      bufferExitElapsedSeconds += clamp(exitDelta, 0, remainingDelta);
      if (bufferExitElapsedSeconds >= BOOT_TIMING.BUFFER_EXIT_DURATION_SECONDS) {
        const exitOverflow = bufferExitElapsedSeconds - BOOT_TIMING.BUFFER_EXIT_DURATION_SECONDS;
        bufferExitElapsedSeconds = BOOT_TIMING.BUFFER_EXIT_DURATION_SECONDS;
        phase = "signal-lock";
        phaseElapsedSeconds = 0;
        return clamp(exitOverflow, 0, remainingDelta);
      }
    }

    return 0;
  }

  function advanceSignalLockPhase(remainingDelta) {
    const nextLockElapsed = phaseElapsedSeconds + remainingDelta;
    if (nextLockElapsed >= BOOT_TIMING.SIGNAL_LOCK_DURATION_SECONDS) {
      phase = "handoff";
      phaseElapsedSeconds = 0;
      return Math.max(0, nextLockElapsed - BOOT_TIMING.SIGNAL_LOCK_DURATION_SECONDS);
    }

    phaseElapsedSeconds = nextLockElapsed;
    return 0;
  }

  function advanceHandoffPhase(remainingDelta) {
    const nextHandoffElapsed = phaseElapsedSeconds + remainingDelta;
    if (nextHandoffElapsed >= BOOT_TIMING.HANDOFF_DURATION_SECONDS) {
      phase = "completed";
      phaseElapsedSeconds = BOOT_TIMING.HANDOFF_DURATION_SECONDS;
      sweepProgress = 1;
      return;
    }

    phaseElapsedSeconds = nextHandoffElapsed;
  }

  /**
   * Advance the boot timeline by deltaSeconds. Non-finite or negative deltas clamp to zero.
   *
   * @param {number} deltaSeconds Frame delta in seconds.
   */
  function step(deltaSeconds) {
    const safeDelta = Number.isFinite(deltaSeconds) && deltaSeconds > 0 ? deltaSeconds : 0;
    if (safeDelta === 0 || phase === "completed") {
      return;
    }

    totalElapsedSeconds += safeDelta;

    if (phase === "warning") {
      warningElapsedSeconds += safeDelta;
      return;
    }

    let remainingDelta = safeDelta;

    if (phase === "wake") {
      remainingDelta = advanceWakePhase(remainingDelta);
    }

    if (phase === "buffer") {
      const nextDelta = advanceBufferPhase(remainingDelta);
      if (nextDelta === null) {
        return;
      }
      remainingDelta = nextDelta;
    }

    if (phase === "signal-lock" && remainingDelta > 0) {
      remainingDelta = advanceSignalLockPhase(remainingDelta);
    }

    if (phase === "handoff" && remainingDelta > 0) {
      advanceHandoffPhase(remainingDelta);
    }
  }

  /**
   * Merge an incoming backend status snapshot and update warning/recovery state.
   *
   * @param {object} incomingStatus Payload from /api/boot or /api/boot/status.
   */
  function ingestBackendStatus(incomingStatus) {
    backendStatus = normalizeBackendStatus(incomingStatus, backendStatus);
    if (phase === "completed") {
      return;
    }

    if (backendStatus.error) {
      if (phase !== "warning") {
        phaseBeforeWarning = phase;
        phase = "warning";
        warningElapsedSeconds = 0;
      }
      return;
    }

    if (phase === "warning" && !simulatedDelayActive) {
      const timedOut = stallTimerSeconds >= BOOT_TIMING.ENGINE_DELAY_TIMEOUT_SECONDS;
      if (backendStatus.ready || !timedOut) {
        phase = phaseBeforeWarning === "wake" ? "wake" : "buffer";
        warningElapsedSeconds = 0;
        stallTimerSeconds = Math.min(stallTimerSeconds, BOOT_TIMING.WAKE_DURATION_SECONDS);
      }
    }
  }

  /**
   * Toggle simulated Engine Delay warning mode.
   *
   * @param {boolean} enabled Whether simulated delay is active.
   */
  function setSimulatedDelay(enabled) {
    const shouldEnable = Boolean(enabled);
    if (phase === "completed") {
      return;
    }

    if (shouldEnable) {
      simulatedDelayActive = true;
      if (phase !== "warning") {
        phaseBeforeWarning = phase;
        phase = "warning";
        warningElapsedSeconds = 0;
      }
      return;
    }

    simulatedDelayActive = false;
    if (phase === "warning") {
      warningElapsedSeconds = 0;
      stallTimerSeconds = Math.min(stallTimerSeconds, BOOT_TIMING.WAKE_DURATION_SECONDS);
      if (!backendStatus.error) {
        phase = phaseBeforeWarning === "wake" ? "wake" : "buffer";
      }
    }
  }

  /**
   * Clear transient warning/error state and resume Act 02 buffer polling.
   */
  function retry() {
    if (phase === "completed") {
      return;
    }
    simulatedDelayActive = false;
    backendStatus = {
      ...backendStatus,
      error: null,
    };
    warningElapsedSeconds = 0;
    stallTimerSeconds = BOOT_TIMING.WAKE_DURATION_SECONDS;
    phase = "buffer";
  }

  /**
   * Return an immutable snapshot describing the current boot frame for Canvas rendering.
   *
   * @returns {object} Render snapshot.
   */
  function getRenderSnapshot() {
    let actProgress = 0;
    let bufferExitProgress = 0;
    let handoffProgress = 0;
    let statusLabel = "MEM // CHECK";

    if (phase === "wake") {
      actProgress = clamp(phaseElapsedSeconds / BOOT_TIMING.WAKE_DURATION_SECONDS, 0, 1);
      statusLabel = "MEM // CHECK";
    } else if (phase === "buffer") {
      actProgress = clamp(bufferElapsedSeconds / BOOT_TIMING.BUFFER_MIN_DURATION_SECONDS, 0, 1);
      bufferExitProgress = clamp(
        bufferExitElapsedSeconds / BOOT_TIMING.BUFFER_EXIT_DURATION_SECONDS,
        0,
        1,
      );
      statusLabel = formatBufferStatusLabel(backendStatus, sweepProgress);
    } else if (phase === "signal-lock") {
      actProgress = clamp(phaseElapsedSeconds / BOOT_TIMING.SIGNAL_LOCK_DURATION_SECONDS, 0, 1);
      bufferExitProgress = 1;
      statusLabel = "LOCK // SYSTEM READY";
    } else if (phase === "handoff") {
      actProgress = 1;
      bufferExitProgress = 1;
      handoffProgress = clamp(phaseElapsedSeconds / BOOT_TIMING.HANDOFF_DURATION_SECONDS, 0, 1);
      statusLabel = "LOCK // SYSTEM READY";
    } else if (phase === "completed") {
      actProgress = 1;
      bufferExitProgress = 1;
      handoffProgress = 1;
      statusLabel = "LOCK // SYSTEM READY";
    } else if (phase === "warning") {
      actProgress = clamp(bufferElapsedSeconds / BOOT_TIMING.BUFFER_MIN_DURATION_SECONDS, 0, 1);
      statusLabel = backendStatus.error
        ? `ERR // ${String(backendStatus.error).toUpperCase()}`
        : "POLLING /UI BUS";
    }

    return {
      phase,
      actProgress,
      bufferExitProgress,
      handoffProgress,
      elapsedSeconds: totalElapsedSeconds,
      warningElapsedSeconds,
      sweepProgress,
      statusLabel,
      summaryLine: formatSummaryLine(backendStatus),
      completed: phase === "completed",
      backendCounts: cloneCounts(backendStatus.counts),
      musicRootExists: Boolean(backendStatus.musicRootExists),
    };
  }

  return {
    step,
    ingestBackendStatus,
    setSimulatedDelay,
    retry,
    getRenderSnapshot,
  };
}
