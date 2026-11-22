/**
 * Main entrypoint for the DialOS simulator web application: manages the
 * 60fps Canvas render loop, backend boot polling, Home 3D carousel interaction,
 * pointer/wheel/keyboard hit-testing, and shell controls.
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
import { HOME_APPS, createHomeController, wrapAppIndex } from "./core/home_controller.js";
import { drawSafeAreaGuides } from "./core/primitives.js";
import { createRouter } from "./core/router.js";
import { APPEARANCE_PALETTES } from "./core/tokens.js";
import { hitTestBootScreen, renderBootScreen } from "./screens/boot_screen.js";
import { hitTestHomeInteractive, renderHomeScreen } from "./screens/home_screen.js";
import { createShellController } from "./shell/shell_controller.js";

const MAX_PIXEL_RATIO = 2;
const MAX_FRAME_DELTA_SECONDS = 0.05;
const SCAN_POLL_INTERVAL_MS = 90;
const WARNING_POLL_INTERVAL_MS = 1000;
const TAP_MAX_DRIFT_PX = 14;
const WHEEL_COOLDOWN_MS = 130;
const WHEEL_MIN_DELTA_PX = 8;

const displayCanvas = document.getElementById("display-canvas");
const drawingContext = displayCanvas.getContext("2d");

let bootController = createBootController();
const homeController = createHomeController();
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
let lastHoverPoint = null;
let lastWheelDetentTimestampMs = 0;
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

function resolveFocusedHomeApp(homeState) {
  if (homeState?.focusedApp) {
    return homeState.focusedApp;
  }
  return HOME_APPS[wrapAppIndex(homeState?.targetSlot ?? 0)];
}

function resolveLaunchedHomeAppTitle(homeState) {
  if (homeState?.launchedAppId) {
    const matchedApp = HOME_APPS.find((appEntry) => appEntry.id === homeState.launchedAppId);
    if (matchedApp) {
      return matchedApp.title;
    }
  }
  return resolveFocusedHomeApp(homeState).title;
}

function formatTelemetryPhaseLabel(activeScreen, bootSnapshot, homeState) {
  if (activeScreen === "boot") {
    if (bootSnapshot.phase === "warning") {
      return "WARNING";
    }
    const bootPhase =
      typeof bootSnapshot.phase === "string" && bootSnapshot.phase.length > 0
        ? bootSnapshot.phase.toUpperCase()
        : "WAKE";
    return `BOOT: ${bootPhase}`;
  }

  if (homeState?.launchTarget === 1) {
    const launchedAppTitle = resolveLaunchedHomeAppTitle(homeState);
    return `OPEN \u00b7 ${launchedAppTitle.toUpperCase()}`;
  }

  const focusedApp = resolveFocusedHomeApp(homeState);
  return `HOME \u00b7 ${focusedApp.title.toUpperCase()}`;
}

function buildTelemetrySnapshot(bootSnapshot, routerState, homeState) {
  const counts = latestCatalog?.counts || bootSnapshot.backendCounts;
  const activeScreen = routerState?.activeScreen || "boot";
  const phaseLabel = formatTelemetryPhaseLabel(activeScreen, bootSnapshot, homeState);

  return {
    phase: activeScreen === "boot" ? bootSnapshot.phase : activeScreen,
    phaseLabel,
    activeScreen,
    musicRoot: musicRootPath,
    musicRootExists: bootSnapshot.musicRootExists,
    counts,
  };
}

function buildTelemetrySignature(bootSnapshot, routerState, homeState) {
  const telemetry = buildTelemetrySnapshot(bootSnapshot, routerState, homeState);
  const counts = telemetry.counts || {};
  return [
    shellState.appearance,
    shellState.magnified,
    shellState.safeGuides,
    shellState.opticalGlass,
    shellState.engineDelayFault,
    shellState.rescanning,
    telemetry.activeScreen,
    telemetry.phase,
    telemetry.phaseLabel,
    telemetry.musicRoot,
    telemetry.musicRootExists,
    homeState?.launchTarget || 0,
    homeState?.launchedAppId || "",
    homeState?.focusedApp?.id || "",
    counts.tracks || 0,
    counts.albums || 0,
    counts.artists || 0,
    counts.genres || 0,
    counts.favorites || 0,
  ].join("|");
}

function syncShellIfNeeded(forceSync = false) {
  const bootSnapshot = bootController.getRenderSnapshot();
  const routerState = router.getState();
  const homeState = homeController.getRenderSnapshot();
  const nextSignature = buildTelemetrySignature(bootSnapshot, routerState, homeState);
  if (!forceSync && nextSignature === lastTelemetrySignature) {
    return;
  }
  lastTelemetrySignature = nextSignature;
  shellController.syncView({
    ...shellState,
    telemetry: buildTelemetrySnapshot(bootSnapshot, routerState, homeState),
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

function syncDisplayCanvasCursorClasses(
  hoverPoint = lastHoverPoint,
  routerState = router.getState(),
  bootSnapshot = bootController.getRenderSnapshot(),
  homeState = homeController.getRenderSnapshot(),
) {
  if (routerState.activeScreen === "boot") {
    displayCanvas.classList.remove("is-interactive");
    displayCanvas.classList.remove("is-dragging");
    const hoveringRetry = Boolean(
      hoverPoint && hitTestBootScreen(hoverPoint.pointX, hoverPoint.pointY, bootSnapshot) !== null,
    );
    displayCanvas.classList.toggle("is-clickable", hoveringRetry);
    return;
  }

  const isCarouselInteractive = homeState.launchTarget === 0;
  const isPointerDragging = Boolean(homeState.pointerActive);
  const isHoveringTarget =
    !isPointerDragging &&
    Boolean(hoverPoint && hitTestHomeInteractive(hoverPoint.pointX, hoverPoint.pointY, homeState));

  displayCanvas.classList.toggle("is-interactive", isCarouselInteractive);
  displayCanvas.classList.toggle("is-dragging", isPointerDragging);
  displayCanvas.classList.toggle("is-clickable", isHoveringTarget);
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

  activePointerId = null;
  pointerDownPoint = null;
  bootController = createBootController();
  router.reboot();

  if (keepDelayFault) {
    shellState.engineDelayFault = true;
    bootController.setSimulatedDelay(true);
  } else {
    shellState.engineDelayFault = false;
  }

  syncDisplayCanvasCursorClasses(lastHoverPoint);
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

function handleNavigateLeft() {
  const routerState = router.getState();
  const homeState = homeController.getRenderSnapshot();
  if (routerState.activeScreen === "home" && homeState.launchTarget === 0) {
    homeController.stepDetent(-1);
    syncShellIfNeeded(false);
  }
}

function handleNavigateRight() {
  const routerState = router.getState();
  const homeState = homeController.getRenderSnapshot();
  if (routerState.activeScreen === "home" && homeState.launchTarget === 0) {
    homeController.stepDetent(1);
    syncShellIfNeeded(false);
  }
}

function handleActivate() {
  const routerState = router.getState();
  if (routerState.activeScreen !== "home") {
    return;
  }
  const homeState = homeController.getRenderSnapshot();
  if (homeState.launchTarget === 0) {
    homeController.launchFocusedApp();
  } else if (homeState.launchedAppId === "now-playing" && homeState.playbackState === "idle") {
    homeController.jumpFromNotPlayingToLibrary();
  }
  syncDisplayCanvasCursorClasses(lastHoverPoint);
  syncShellIfNeeded(false);
}

function handleNavigateBack() {
  const routerState = router.getState();
  const homeState = homeController.getRenderSnapshot();
  if (routerState.activeScreen === "home" && homeState.launchTarget === 1) {
    homeController.closeLaunchedApp();
    syncDisplayCanvasCursorClasses(lastHoverPoint);
    syncShellIfNeeded(false);
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
  onNavigateLeft: handleNavigateLeft,
  onNavigateRight: handleNavigateRight,
  onActivate: handleActivate,
  onNavigateBack: handleNavigateBack,
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

function releaseActivePointerCapture(pointerId) {
  if (typeof displayCanvas.releasePointerCapture === "function" && pointerId !== null) {
    try {
      displayCanvas.releasePointerCapture(pointerId);
    } catch (releaseError) {
      /* Pointer capture already ended. */
    }
  }
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
  lastHoverPoint = displayPoint;

  if (typeof displayCanvas.setPointerCapture === "function") {
    try {
      displayCanvas.setPointerCapture(pointerEvent.pointerId);
    } catch (captureError) {
      /* Pointer capture unavailable for synthetic or detached events. */
    }
  }

  if (router.getState().activeScreen === "home") {
    homeController.handlePointerDown(displayPoint.pointX, displayPoint.pointY);
  }
  syncDisplayCanvasCursorClasses(displayPoint);
});

displayCanvas.addEventListener("pointermove", (pointerEvent) => {
  const hoverPoint = clientPointToDisplayPoint(pointerEvent.clientX, pointerEvent.clientY);
  lastHoverPoint = hoverPoint;

  if (
    router.getState().activeScreen === "home" &&
    activePointerId === pointerEvent.pointerId &&
    hoverPoint
  ) {
    homeController.handlePointerMove(hoverPoint.pointX, hoverPoint.pointY);
  }
  syncDisplayCanvasCursorClasses(hoverPoint);
});

displayCanvas.addEventListener("pointerup", (pointerEvent) => {
  if (pointerEvent.pointerId !== activePointerId) {
    return;
  }
  const releasePoint = clientPointToDisplayPoint(pointerEvent.clientX, pointerEvent.clientY);
  const startPoint = pointerDownPoint;
  const releasedPointerId = activePointerId;
  activePointerId = null;
  pointerDownPoint = null;
  releaseActivePointerCapture(releasedPointerId);

  const routerState = router.getState();
  if (routerState.activeScreen === "home") {
    if (releasePoint) {
      homeController.handlePointerUp(releasePoint.pointX, releasePoint.pointY);
      lastHoverPoint = releasePoint;
    } else {
      homeController.handlePointerCancel();
    }
    syncDisplayCanvasCursorClasses(lastHoverPoint);
    syncShellIfNeeded(false);
    return;
  }

  if (!startPoint || !releasePoint) {
    syncDisplayCanvasCursorClasses(lastHoverPoint);
    return;
  }

  const driftDistance = Math.hypot(
    releasePoint.pointX - startPoint.pointX,
    releasePoint.pointY - startPoint.pointY,
  );
  if (driftDistance <= TAP_MAX_DRIFT_PX) {
    const hitAction = hitTestBootScreen(
      releasePoint.pointX,
      releasePoint.pointY,
      bootController.getRenderSnapshot(),
    );
    if (hitAction === "retry") {
      handleRetryTap();
    }
  }
  syncDisplayCanvasCursorClasses(lastHoverPoint);
});

displayCanvas.addEventListener("pointercancel", (pointerEvent) => {
  if (pointerEvent.pointerId !== activePointerId) {
    return;
  }
  const cancelledPointerId = activePointerId;
  activePointerId = null;
  pointerDownPoint = null;
  releaseActivePointerCapture(cancelledPointerId);

  if (router.getState().activeScreen === "home") {
    homeController.handlePointerCancel();
  }
  syncDisplayCanvasCursorClasses(lastHoverPoint);
});

displayCanvas.addEventListener("pointerleave", () => {
  lastHoverPoint = null;
  syncDisplayCanvasCursorClasses(null);
});

displayCanvas.addEventListener(
  "wheel",
  (wheelEvent) => {
    const routerState = router.getState();
    const homeState = homeController.getRenderSnapshot();
    if (routerState.activeScreen !== "home" || homeState.launchTarget !== 0) {
      return;
    }
    wheelEvent.preventDefault();
    const currentTimestampMs = performance.now();
    if (currentTimestampMs - lastWheelDetentTimestampMs < WHEEL_COOLDOWN_MS) {
      return;
    }
    const dominantDelta =
      Math.abs(wheelEvent.deltaX) > Math.abs(wheelEvent.deltaY)
        ? wheelEvent.deltaX
        : wheelEvent.deltaY;
    if (Math.abs(dominantDelta) < WHEEL_MIN_DELTA_PX) {
      return;
    }
    lastWheelDetentTimestampMs = currentTimestampMs;
    homeController.stepDetent(dominantDelta > 0 ? 1 : -1);
    syncShellIfNeeded(false);
  },
  { passive: false },
);

function renderDisplayFrame(bootSnapshot, routerState, homeState) {
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
    const counts = latestCatalog?.counts || bootSnapshot.backendCounts;
    renderHomeScreen(drawingContext, homeState, {
      appearance: shellState.appearance,
      palette,
      counts,
    });
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
    homeController.enterFromBoot();
    router.completeBootToHome();
  }

  router.stepTransition(frameDeltaSeconds);
  const routerState = router.getState();

  if (routerState.activeScreen === "home") {
    homeController.step(frameDeltaSeconds);
  }

  const homeState = homeController.getRenderSnapshot();
  renderDisplayFrame(bootSnapshot, routerState, homeState);
  syncDisplayCanvasCursorClasses(lastHoverPoint, routerState, bootSnapshot, homeState);
  syncShellIfNeeded(false);

  window.requestAnimationFrame(animationTick);
}

/* Kick off initial boot scan and enter the 60fps Canvas loop. */
syncShellIfNeeded(true);
void startBootScan(bootSessionId, { rescan: true });
window.requestAnimationFrame(animationTick);
