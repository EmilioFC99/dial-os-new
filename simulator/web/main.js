/**
 * Main entrypoint for the DialOS Step 1 simulator web application: manages the
 * 60fps Canvas render loop, backend boot polling, pointer hit-testing, and shell controls.
 */

import {
  fetchBootStatus,
  fetchLibraryCatalog,
  triggerBootScan,
  triggerLibraryRescan,
} from "./core/api_client.js";
import { createBootController } from "./core/boot_controller.js";
import {
  DISPLAY_CENTER_PX,
  DISPLAY_DIAMETER_PX,
  FULL_CIRCLE_RADIANS,
  MILLISECONDS_PER_SECOND,
} from "./core/geometry.js";
import { drawSafeAreaGuides } from "./core/primitives.js";
import { createRouter } from "./core/router.js";
import { APPEARANCE_PALETTES } from "./core/tokens.js";
import { hitTestBootScreen, renderBootScreen } from "./screens/boot_screen.js";
import {
  hitTestHomeHandoffPlaceholder,
  renderHomeHandoffPlaceholder,
} from "./screens/home_handoff_placeholder.js";
import { createShellController } from "./shell/shell_controller.js";

const MAX_PIXEL_RATIO = 2;
const MAX_FRAME_DELTA_SECONDS = 0.05;
const SCAN_POLL_INTERVAL_MS = 90;
const WARNING_POLL_INTERVAL_MS = 1000;
const TAP_MAX_DRIFT_PX = 14;

const displayCanvas = document.getElementById("display-canvas");
const drawingContext = displayCanvas.getContext("2d");

let bootController = createBootController();
const router = createRouter({ initialScreen: "boot" });

const shellState = {
  appearance: "dark",
  magnified: false,
  safeGuides: false,
  opticalGlass: true,
  engineDelayFault: false,
  rescanning: false,
};

let bootSessionId = 0;
let pollTimerId = null;
let pollInFlight = false;
let backendReady = false;
let catalogLoaded = false;
let catalogFetchInFlight = false;
let latestCatalog = null;
let musicRootPath = "";

let activePointerId = null;
let pointerDownPoint = null;
let lastRenderedTimestampMs = null;
let lastTelemetrySignature = "";

/**
 * Resize the canvas backing store cleanly for the current devicePixelRatio.
 *
 * @returns {number} Active backing scale factor in [1, 2].
 */
function syncCanvasBackingResolution() {
  const pixelRatio = Math.min(window.devicePixelRatio || 1, MAX_PIXEL_RATIO);
  const targetSizePx = Math.round(DISPLAY_DIAMETER_PX * pixelRatio);
  if (displayCanvas.width !== targetSizePx || displayCanvas.height !== targetSizePx) {
    displayCanvas.width = targetSizePx;
    displayCanvas.height = targetSizePx;
  }
  return pixelRatio;
}

function clearScheduledPoll() {
  if (pollTimerId !== null) {
    window.clearTimeout(pollTimerId);
    pollTimerId = null;
  }
}

function buildTelemetrySnapshot(bootSnapshot) {
  const counts = latestCatalog?.counts || bootSnapshot.backendCounts;
  return {
    phase: bootSnapshot.phase,
    musicRoot: musicRootPath,
    musicRootExists: bootSnapshot.musicRootExists,
    counts,
  };
}

function buildTelemetrySignature(bootSnapshot) {
  const telemetry = buildTelemetrySnapshot(bootSnapshot);
  const counts = telemetry.counts || {};
  return [
    shellState.appearance,
    shellState.magnified,
    shellState.safeGuides,
    shellState.opticalGlass,
    shellState.engineDelayFault,
    shellState.rescanning,
    telemetry.phase,
    telemetry.musicRoot,
    telemetry.musicRootExists,
    counts.tracks || 0,
    counts.albums || 0,
    counts.artists || 0,
  ].join("|");
}

function syncShellIfNeeded(forceSync = false) {
  const bootSnapshot = bootController.getRenderSnapshot();
  const nextSignature = buildTelemetrySignature(bootSnapshot);
  if (!forceSync && nextSignature === lastTelemetrySignature) {
    return;
  }
  lastTelemetrySignature = nextSignature;
  shellController.syncView({
    ...shellState,
    telemetry: buildTelemetrySnapshot(bootSnapshot),
  });
}

async function loadCatalogWhenReady(sessionId) {
  if (catalogLoaded || catalogFetchInFlight) {
    return;
  }
  catalogFetchInFlight = true;
  try {
    const catalogResponse = await fetchLibraryCatalog();
    if (sessionId !== bootSessionId) {
      return;
    }
    if (catalogResponse.ok && catalogResponse.payload) {
      latestCatalog = catalogResponse.payload;
      catalogLoaded = true;
      if (typeof catalogResponse.payload.musicRoot === "string") {
        musicRootPath = catalogResponse.payload.musicRoot;
      }
      syncShellIfNeeded(true);
    }
  } finally {
    if (sessionId === bootSessionId) {
      catalogFetchInFlight = false;
    }
  }
}

function applyBackendStatus(payload, sessionId) {
  if (sessionId !== bootSessionId || !payload) {
    return;
  }
  if (typeof payload.musicRoot === "string") {
    musicRootPath = payload.musicRoot;
  }
  backendReady = Boolean(payload.ready) && !payload.error;
  bootController.ingestBackendStatus(payload);

  if (backendReady) {
    void loadCatalogWhenReady(sessionId);
  }
  syncShellIfNeeded(true);
}

function applyBackendError(errorMessage, sessionId) {
  if (sessionId !== bootSessionId) {
    return;
  }
  backendReady = false;
  bootController.ingestBackendStatus({
    error: errorMessage || "bus_unreachable",
  });
  syncShellIfNeeded(true);
}

function scheduleNextPoll(sessionId) {
  clearScheduledPoll();
  if (sessionId !== bootSessionId) {
    return;
  }
  const bootSnapshot = bootController.getRenderSnapshot();
  if (bootSnapshot.phase === "warning") {
    pollTimerId = window.setTimeout(() => {
      void runStatusPoll(sessionId);
    }, WARNING_POLL_INTERVAL_MS);
    return;
  }

  if (!backendReady && bootSnapshot.phase !== "completed") {
    pollTimerId = window.setTimeout(() => {
      void runStatusPoll(sessionId);
    }, SCAN_POLL_INTERVAL_MS);
  }
}

async function runStatusPoll(sessionId) {
  if (sessionId !== bootSessionId || pollInFlight) {
    return;
  }
  pollInFlight = true;
  try {
    const statusResponse = await fetchBootStatus();
    if (sessionId !== bootSessionId) {
      return;
    }
    if (statusResponse.ok && statusResponse.payload) {
      applyBackendStatus(statusResponse.payload, sessionId);
    } else {
      applyBackendError(statusResponse.error, sessionId);
    }
  } finally {
    pollInFlight = false;
    scheduleNextPoll(sessionId);
  }
}

async function startBootScan(sessionId, { rescan }) {
  const bootResponse = await triggerBootScan({ rescan });
  if (sessionId !== bootSessionId) {
    return;
  }
  if (bootResponse.ok && bootResponse.payload) {
    applyBackendStatus(bootResponse.payload, sessionId);
  } else {
    applyBackendError(bootResponse.error, sessionId);
  }
  scheduleNextPoll(sessionId);
}

/**
 * Reset the boot state machine, return to the Boot screen, and trigger a fresh backend scan.
 *
 * @param {object} [options] Reboot options.
 * @param {boolean} [options.rescan=true] Whether backend should force a full rescan.
 * @param {boolean} [options.keepDelayFault=false] Whether to keep Engine Delay Fault active.
 */
function reboot({ rescan = true, keepDelayFault = false } = {}) {
  clearScheduledPoll();
  bootSessionId += 1;
  const currentSessionId = bootSessionId;

  backendReady = false;
  catalogLoaded = false;
  catalogFetchInFlight = false;
  if (rescan) {
    latestCatalog = null;
  }

  bootController = createBootController();
  router.reboot();

  if (keepDelayFault) {
    shellState.engineDelayFault = true;
    bootController.setSimulatedDelay(true);
  } else {
    shellState.engineDelayFault = false;
  }

  syncShellIfNeeded(true);
  void startBootScan(currentSessionId, { rescan });
}

function handleRetryTap() {
  shellState.engineDelayFault = false;
  bootController.retry();
  syncShellIfNeeded(true);
  clearScheduledPoll();
  const currentSessionId = bootSessionId;
  void startBootScan(currentSessionId, { rescan: false });
}

function handleToggleAppearance() {
  shellState.appearance = shellState.appearance === "dark" ? "light" : "dark";
  syncShellIfNeeded(true);
}

function handleToggleZoom() {
  shellState.magnified = !shellState.magnified;
  syncShellIfNeeded(true);
}

function handleToggleSafeGuides() {
  shellState.safeGuides = !shellState.safeGuides;
  syncShellIfNeeded(true);
}

function handleToggleOpticalGlass() {
  shellState.opticalGlass = !shellState.opticalGlass;
  syncShellIfNeeded(true);
}

function handleToggleEngineDelay() {
  const nextEnabled = !shellState.engineDelayFault;
  const activeScreen = router.getState().activeScreen;
  const bootSnapshot = bootController.getRenderSnapshot();

  if (nextEnabled) {
    if (activeScreen !== "boot" || bootSnapshot.completed) {
      reboot({ rescan: false, keepDelayFault: true });
      return;
    }
    shellState.engineDelayFault = true;
    bootController.setSimulatedDelay(true);
    syncShellIfNeeded(true);
    scheduleNextPoll(bootSessionId);
    return;
  }

  shellState.engineDelayFault = false;
  bootController.setSimulatedDelay(false);
  syncShellIfNeeded(true);
  clearScheduledPoll();
  void runStatusPoll(bootSessionId);
}

async function handleRescanLibrary() {
  if (shellState.rescanning) {
    return;
  }
  shellState.rescanning = true;
  syncShellIfNeeded(true);
  const currentSessionId = bootSessionId;

  try {
    const rescanResponse = await triggerLibraryRescan();
    if (currentSessionId !== bootSessionId) {
      return;
    }
    if (rescanResponse.ok && rescanResponse.payload) {
      latestCatalog = rescanResponse.payload;
      catalogLoaded = true;
      if (typeof rescanResponse.payload.musicRoot === "string") {
        musicRootPath = rescanResponse.payload.musicRoot;
      }

      const statusResponse = await fetchBootStatus();
      if (currentSessionId !== bootSessionId) {
        return;
      }
      if (statusResponse.ok && statusResponse.payload) {
        applyBackendStatus(statusResponse.payload, currentSessionId);
      } else {
        applyBackendStatus(
          {
            phase: "ready",
            ready: true,
            musicRoot: musicRootPath,
            musicRootExists: true,
            counts: rescanResponse.payload.counts,
            error: null,
          },
          currentSessionId,
        );
      }
    }
  } finally {
    shellState.rescanning = false;
    syncShellIfNeeded(true);
  }
}

const shellController = createShellController({
  onReboot: () => reboot({ rescan: true }),
  onToggleAppearance: handleToggleAppearance,
  onToggleZoom: handleToggleZoom,
  onToggleSafeGuides: handleToggleSafeGuides,
  onToggleOpticalGlass: handleToggleOpticalGlass,
  onToggleEngineDelay: handleToggleEngineDelay,
  onRescanLibrary: () => {
    void handleRescanLibrary();
  },
});

function clientPointToDisplayPoint(clientX, clientY) {
  const canvasRect = displayCanvas.getBoundingClientRect();
  if (canvasRect.width <= 0 || canvasRect.height <= 0) {
    return null;
  }
  return {
    pointX: ((clientX - canvasRect.left) / canvasRect.width) * DISPLAY_DIAMETER_PX,
    pointY: ((clientY - canvasRect.top) / canvasRect.height) * DISPLAY_DIAMETER_PX,
  };
}

function resolveCanvasActionAtPoint(point) {
  if (!point) {
    return null;
  }
  const routerState = router.getState();
  const bootSnapshot = bootController.getRenderSnapshot();
  if (routerState.activeScreen === "boot") {
    return hitTestBootScreen(point.pointX, point.pointY, bootSnapshot);
  }
  return hitTestHomeHandoffPlaceholder(point.pointX, point.pointY);
}

displayCanvas.addEventListener("pointerdown", (pointerEvent) => {
  if (pointerEvent.button !== 0) {
    return;
  }
  const displayPoint = clientPointToDisplayPoint(pointerEvent.clientX, pointerEvent.clientY);
  if (!displayPoint) {
    return;
  }
  activePointerId = pointerEvent.pointerId;
  pointerDownPoint = displayPoint;
});

displayCanvas.addEventListener("pointerup", (pointerEvent) => {
  if (pointerEvent.pointerId !== activePointerId || !pointerDownPoint) {
    return;
  }
  const releasePoint = clientPointToDisplayPoint(pointerEvent.clientX, pointerEvent.clientY);
  const startPoint = pointerDownPoint;
  activePointerId = null;
  pointerDownPoint = null;

  if (!releasePoint) {
    return;
  }
  const driftDistance = Math.hypot(
    releasePoint.pointX - startPoint.pointX,
    releasePoint.pointY - startPoint.pointY,
  );
  if (driftDistance > TAP_MAX_DRIFT_PX) {
    return;
  }

  const hitAction = resolveCanvasActionAtPoint(releasePoint);
  if (hitAction === "retry") {
    handleRetryTap();
  } else if (hitAction === "reboot") {
    reboot({ rescan: true });
  }
});

displayCanvas.addEventListener("pointercancel", () => {
  activePointerId = null;
  pointerDownPoint = null;
});

displayCanvas.addEventListener("pointermove", (pointerEvent) => {
  const hoverPoint = clientPointToDisplayPoint(pointerEvent.clientX, pointerEvent.clientY);
  const hoverAction = resolveCanvasActionAtPoint(hoverPoint);
  displayCanvas.classList.toggle("is-clickable", hoverAction !== null);
});

displayCanvas.addEventListener("pointerleave", () => {
  displayCanvas.classList.remove("is-clickable");
});

function renderDisplayFrame(bootSnapshot, routerState) {
  const pixelRatio = syncCanvasBackingResolution();
  const palette = APPEARANCE_PALETTES[shellState.appearance];

  drawingContext.setTransform(pixelRatio, 0, 0, pixelRatio, 0, 0);
  drawingContext.fillStyle = palette.canvas;
  drawingContext.fillRect(0, 0, DISPLAY_DIAMETER_PX, DISPLAY_DIAMETER_PX);

  drawingContext.save();
  drawingContext.beginPath();
  drawingContext.arc(
    DISPLAY_CENTER_PX,
    DISPLAY_CENTER_PX,
    DISPLAY_DIAMETER_PX / 2,
    0,
    FULL_CIRCLE_RADIANS,
  );
  drawingContext.clip();

  if (routerState.activeScreen === "boot") {
    renderBootScreen(drawingContext, bootSnapshot, palette);
  } else {
    renderHomeHandoffPlaceholder(drawingContext, routerState, bootSnapshot, palette);
  }

  if (shellState.safeGuides) {
    drawSafeAreaGuides(drawingContext, palette);
  }

  drawingContext.restore();
}

function animationTick(timestampMs) {
  if (lastRenderedTimestampMs === null) {
    lastRenderedTimestampMs = timestampMs;
  }
  const elapsedDeltaSeconds = (timestampMs - lastRenderedTimestampMs) / MILLISECONDS_PER_SECOND;
  lastRenderedTimestampMs = timestampMs;
  const frameDeltaSeconds = Math.min(Math.max(elapsedDeltaSeconds, 0), MAX_FRAME_DELTA_SECONDS);

  bootController.step(frameDeltaSeconds);
  const bootSnapshot = bootController.getRenderSnapshot();

  if (bootSnapshot.completed && router.getState().activeScreen === "boot") {
    router.completeBootToHome();
  }

  router.stepTransition(frameDeltaSeconds);
  const routerState = router.getState();

  renderDisplayFrame(bootSnapshot, routerState);
  syncShellIfNeeded(false);

  window.requestAnimationFrame(animationTick);
}

/* Kick off initial boot scan and enter the 60fps Canvas loop. */
syncShellIfNeeded(true);
void startBootScan(bootSessionId, { rescan: true });
window.requestAnimationFrame(animationTick);
