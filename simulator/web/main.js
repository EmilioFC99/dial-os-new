/**
 * Main entrypoint for the DialOS simulator web application: manages the
 * 60fps Canvas render loop, backend boot polling, Home 3D carousel interaction,
 * Library navigation & playback orchestration, pointer/wheel/keyboard hit-testing,
 * and shell controls.
 */

import {
  fetchBootStatus,
  fetchLibraryCatalog,
  savePlaylists,
  setTrackFavorite,
  triggerBootScan,
  triggerLibraryRescan,
} from "./core/api_client.js";
import { createAudioEngine } from "./core/audio_engine.js";
import { createBootController } from "./core/boot_controller.js";
import { createCoverCache } from "./core/cover_cache.js";
import {
  DISPLAY_CENTER_PX,
  DISPLAY_DIAMETER_PX,
  FULL_CIRCLE_RADIANS,
  MILLISECONDS_PER_SECOND,
} from "./core/geometry.js";
import { HOME_APPS, createHomeController, wrapAppIndex } from "./core/home_controller.js";
import { createLibraryController } from "./core/library_controller.js";
import { createNowPlayingController } from "./core/now_playing_controller.js";
import { drawSafeAreaGuides } from "./core/primitives.js";
import { createRouter } from "./core/router.js";
import { APPEARANCE_PALETTES } from "./core/tokens.js";
import { hitTestBootScreen, renderBootScreen } from "./screens/boot_screen.js";
import { hitTestHomeInteractive, renderHomeScreen } from "./screens/home_screen.js";
import { hitTestLibraryInteractive, renderLibraryScreen } from "./screens/library_screen.js";
import {
  hitTestNowPlayingInteractive,
  renderNowPlayingScreen,
} from "./screens/now_playing_screen.js";
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
const audioEngine = createAudioEngine();
const coverCache = createCoverCache();
const homeController = createHomeController();
const libraryController = createLibraryController({
  onLaunchTrack: handleLaunchLibraryTrack,
  onExitToHome: handleExitLibraryToHome,
  onPersistPlaylists: handlePersistPlaylists,
});
const nowPlayingController = createNowPlayingController({
  onBrowseLibrary: handleNowPlayingBrowseLibrary,
  onTogglePlayPause: handleNowPlayingTogglePlayPause,
  onSeek: handleNowPlayingSeek,
  onVolumeChange: handleNowPlayingVolumeChange,
  onSkipTrack: handleNowPlayingSkipTrack,
  onSelectQueueIndex: handleNowPlayingSelectQueueIndex,
  onToggleFavorite: handleNowPlayingToggleFavorite,
  onPlayModeChange: handleNowPlayingPlayModeChange,
  onPersistPlaylists: handlePersistPlaylists,
  onExit: handleExitNowPlaying,
});
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

function releaseActivePointerCapture(pointerId) {
  if (typeof displayCanvas.releasePointerCapture === "function" && pointerId !== null) {
    try {
      displayCanvas.releasePointerCapture(pointerId);
    } catch (releaseError) {
      /* Pointer capture already ended. */
    }
  }
}

function clearActiveCanvasPointer() {
  if (activePointerId !== null) {
    const releasedPointerId = activePointerId;
    activePointerId = null;
    pointerDownPoint = null;
    releaseActivePointerCapture(releasedPointerId);
  }
}

function buildHomePlaybackTelemetry(audioState) {
  const hasActiveTrack = Boolean(audioState?.activeTrack);
  const playbackState = hasActiveTrack
    ? audioState.isPlaying
      ? "playing"
      : "paused"
    : "idle";
  return {
    playbackState,
    hasActiveTrack,
    volumeLevel: audioState?.volumeLevel,
  };
}

function syncPlaybackAcrossControllers() {
  const audioState = audioEngine.getState();
  nowPlayingController.syncAudioState(audioState);
  libraryController.setActiveTrackId(audioState.activeTrack?.id || null);
  libraryController.setPlaybackState(audioState.isPlaying);
  homeController.syncPlaybackState(buildHomePlaybackTelemetry(audioState));
  syncShellIfNeeded(false);
}

function handleNowPlayingBrowseLibrary() {
  clearActiveCanvasPointer();
  libraryController.enterFromHome();
  router.navigateTo("library");
  syncDisplayCanvasCursorClasses(lastHoverPoint);
  syncShellIfNeeded(true);
}

function handleNowPlayingTogglePlayPause() {
  audioEngine.togglePlayPause();
  syncPlaybackAcrossControllers();
}

function handleNowPlayingSeek(targetSeconds) {
  audioEngine.seekTo(targetSeconds);
  syncPlaybackAcrossControllers();
}

function handleNowPlayingVolumeChange(nextVolume) {
  audioEngine.setVolume(nextVolume);
  syncPlaybackAcrossControllers();
}

function handleNowPlayingSkipTrack(direction) {
  audioEngine.skipTrack(direction);
  syncPlaybackAcrossControllers();
}

function handleNowPlayingSelectQueueIndex(rowIndex) {
  audioEngine.selectQueueIndex(rowIndex);
  syncPlaybackAcrossControllers();
}

function handleNowPlayingToggleFavorite(trackId, nextState) {
  audioEngine.updateTrackFavorite(trackId, nextState);
  libraryController.setTrackFavorite(trackId, nextState);
  if (latestCatalog && typeof latestCatalog === "object") {
    const nextFavorite = Boolean(nextState);
    const nextAllTracks = Array.isArray(latestCatalog.allTracks)
      ? latestCatalog.allTracks.map((trackEntry) =>
          trackEntry.id === trackId ? { ...trackEntry, isFavorite: nextFavorite } : trackEntry,
        )
      : [];
    const nextAlbums = Array.isArray(latestCatalog.albums)
      ? latestCatalog.albums.map((albumEntry) => {
          if (!Array.isArray(albumEntry.tracks)) {
            return albumEntry;
          }
          return {
            ...albumEntry,
            tracks: albumEntry.tracks.map((trackEntry) =>
              trackEntry.id === trackId
                ? { ...trackEntry, isFavorite: nextFavorite }
                : trackEntry,
            ),
          };
        })
      : [];
    latestCatalog = {
      ...latestCatalog,
      allTracks: nextAllTracks,
      albums: nextAlbums,
      counts: {
        ...(latestCatalog.counts || {}),
        favorites: nextAllTracks.filter((trackEntry) => Boolean(trackEntry.isFavorite)).length,
      },
    };
  }
  void setTrackFavorite(trackId, nextState);
  syncPlaybackAcrossControllers();
  syncShellIfNeeded(true);
}

function handleNowPlayingPlayModeChange(nextModeId) {
  audioEngine.setPlayMode(nextModeId);
  syncPlaybackAcrossControllers();
}

function resolveTotalVinylRotationRadians(controllerState) {
  const baseRotationRadians = Number.isFinite(controllerState?.vinylRotationRadians)
    ? controllerState.vinylRotationRadians
    : 0;
  const nudgeRotationRadians = Number.isFinite(controllerState?.vinylNudgeRadians)
    ? controllerState.vinylNudgeRadians
    : 0;
  return baseRotationRadians + nudgeRotationRadians;
}

function handleExitNowPlaying(exitReturnScreen) {
  const nowPlayingState = nowPlayingController.getRenderSnapshot();
  const handoffRotationRadians = resolveTotalVinylRotationRadians(nowPlayingState);
  const audioState = audioEngine.getState();
  clearActiveCanvasPointer();
  if (exitReturnScreen === "library") {
    libraryController.setActiveTrackId(audioState.activeTrack?.id || null);
    libraryController.returnFromNowPlaying({
      vinylRotationRadians: handoffRotationRadians,
      albumId: audioState.activeAlbum?.id || nowPlayingState.activeAlbum?.id || null,
      launchOrigin: nowPlayingState.launchOrigin,
      isPlaying: audioState.isPlaying,
    });
    router.navigateTo("library");
  } else {
    router.navigateTo("home");
    homeController.returnFromScreen("now-playing", {
      vinylRotationRadians: handoffRotationRadians,
    });
  }
  syncDisplayCanvasCursorClasses(lastHoverPoint);
  syncShellIfNeeded(true);
}

function handleLaunchLibraryTrack(selectionSpec) {
  const preLaunchLibraryState = libraryController.getRenderSnapshot();
  const handoffRotationRadians = resolveTotalVinylRotationRadians(preLaunchLibraryState);
  const activeCatalog = latestCatalog || preLaunchLibraryState.catalog;
  audioEngine.playTrackFromSource(selectionSpec, activeCatalog);
  const audioState = audioEngine.getState();
  libraryController.setActiveTrackId(audioState.activeTrack?.id || null);
  libraryController.setPlaybackState(audioState.isPlaying);
  libraryController.resetVinylLaunch();
  homeController.setVinylRotationRadians(handoffRotationRadians);
  homeController.syncPlaybackState(buildHomePlaybackTelemetry(audioState));
  nowPlayingController.enterFromLibrary({
    returnScreen: "library",
    launchOrigin: selectionSpec?.launchOrigin || "library",
    audioState,
    vinylRotationRadians: handoffRotationRadians,
  });
  clearActiveCanvasPointer();
  router.navigateTo("now-playing");
  syncDisplayCanvasCursorClasses(lastHoverPoint);
  syncShellIfNeeded(true);
}

function handleExitLibraryToHome() {
  const libraryState = libraryController.getRenderSnapshot();
  const handoffRotationRadians = resolveTotalVinylRotationRadians(libraryState);
  clearActiveCanvasPointer();
  router.navigateTo("home");
  homeController.returnFromScreen("library", {
    vinylRotationRadians: handoffRotationRadians,
  });
  syncDisplayCanvasCursorClasses(lastHoverPoint);
  syncShellIfNeeded(true);
}

/**
 * Immediately switch from Home into launched destination apps (`library` or `now-playing`).
 *
 * @returns {boolean} True when control transferred from Home to a dedicated screen.
 */
function handoffHomeAppIfLaunched() {
  if (router.getState().activeScreen !== "home") {
    return false;
  }
  const launchedHomeState = homeController.getRenderSnapshot();
  if (launchedHomeState.launchTarget !== 1) {
    return false;
  }
  const handoffRotationRadians = resolveTotalVinylRotationRadians(launchedHomeState);
  if (launchedHomeState.launchedAppId === "library") {
    const audioState = audioEngine.getState();
    clearActiveCanvasPointer();
    libraryController.enterFromHome({
      vinylRotationRadians: handoffRotationRadians,
      activeTrackId: audioState.activeTrack?.id || null,
      isPlaying: audioState.isPlaying,
    });
    router.navigateTo("library");
    return true;
  }
  if (launchedHomeState.launchedAppId === "now-playing") {
    const hasActiveVinyl = Boolean(
      launchedHomeState.hasActiveTrack ||
        launchedHomeState.playbackState === "playing" ||
        launchedHomeState.playbackState === "paused",
    );
    if (hasActiveVinyl && launchedHomeState.launchProgress < 1) {
      return false;
    }
    clearActiveCanvasPointer();
    nowPlayingController.enterFromHome({
      returnScreen: "home",
      audioState: audioEngine.getState(),
      vinylRotationRadians: handoffRotationRadians,
      skipOpenTransition: hasActiveVinyl,
    });
    router.navigateTo("now-playing");
    return true;
  }
  return false;
}

function handlePersistPlaylists(updatedPlaylists) {
  const nextPlaylists = Array.isArray(updatedPlaylists) ? updatedPlaylists.slice() : [];
  void savePlaylists(nextPlaylists);
  libraryController.setPlaylists(nextPlaylists);
  nowPlayingController.setPlaylists(nextPlaylists);
  if (latestCatalog && typeof latestCatalog === "object") {
    latestCatalog = {
      ...latestCatalog,
      playlists: nextPlaylists,
      counts: {
        ...(latestCatalog.counts || {}),
        playlists: nextPlaylists.length,
      },
    };
  }
  syncShellIfNeeded(true);
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

function formatTelemetryPhaseLabel(
  activeScreen,
  bootSnapshot,
  homeState,
  libraryState,
  nowPlayingState,
  audioState,
) {
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

  if (activeScreen === "now-playing") {
    if (nowPlayingState?.operatingMode !== "playing") {
      return "NOW \u00b7 IDLE";
    }
    if (nowPlayingState?.playlistPickerOpen) {
      return "NOW \u00b7 PLAYLIST PICKER";
    }
    if (nowPlayingState?.detailsSheetOpen) {
      return "NOW \u00b7 TRACK DETAILS";
    }
    if (nowPlayingState?.queueSheetOpen) {
      return "NOW \u00b7 QUEUE";
    }
    if (nowPlayingState?.quickActionsOpen) {
      return "NOW \u00b7 QUICK ACTIONS";
    }
    if (nowPlayingState?.lyricsOpen) {
      const lyricsModeLabel = nowPlayingState?.lyricsFrame?.modeLabel || "TRACK";
      return `LYRICS \u00b7 ${lyricsModeLabel}`;
    }
    if (nowPlayingState?.isSeeking) {
      return "NOW \u00b7 SEEK";
    }
    const trackTitle = nowPlayingState?.activeTrack?.title || audioState?.activeTrack?.title;
    if (typeof trackTitle === "string" && trackTitle.trim().length > 0) {
      return `NOW \u00b7 ${trackTitle.trim().toUpperCase()}`;
    }
    return nowPlayingState?.isPlaying ? "NOW \u00b7 PLAYING" : "NOW \u00b7 PAUSED";
  }

  if (activeScreen === "library") {
    if (
      libraryState?.vinylLaunchTarget === 1 ||
      Number(libraryState?.vinylLaunchProgress ?? 0) > 0.5
    ) {
      const trackTitle = audioState?.activeTrack?.title;
      if (typeof trackTitle === "string" && trackTitle.trim().length > 0) {
        return `PLAY \u00b7 ${trackTitle.trim().toUpperCase()}`;
      }
      return "LIB \u00b7 VINYL LAUNCH";
    }
    if (libraryState?.playlistSheetOpen) {
      return "LIB \u00b7 PLAYLIST SHEET";
    }
    const currentView = libraryState?.currentView;
    if (currentView?.kind === "hub") {
      return "LIB \u00b7 MAIN HUB";
    }
    if (typeof currentView?.title === "string" && currentView.title.length > 0) {
      return `LIB \u00b7 ${currentView.title.toUpperCase()}`;
    }
    return "LIB \u00b7 COLLECTION";
  }

  if (homeState?.launchTarget === 1) {
    const launchedAppTitle = resolveLaunchedHomeAppTitle(homeState);
    return `OPEN \u00b7 ${launchedAppTitle.toUpperCase()}`;
  }

  const focusedApp = resolveFocusedHomeApp(homeState);
  return `HOME \u00b7 ${focusedApp.title.toUpperCase()}`;
}

function buildTelemetrySnapshot(
  bootSnapshot,
  routerState,
  homeState,
  libraryState,
  nowPlayingState,
  audioState,
) {
  const counts = latestCatalog?.counts || bootSnapshot.backendCounts;
  const activeScreen = routerState?.activeScreen || "boot";
  const phaseLabel = formatTelemetryPhaseLabel(
    activeScreen,
    bootSnapshot,
    homeState,
    libraryState,
    nowPlayingState,
    audioState,
  );

  return {
    phase: activeScreen === "boot" ? bootSnapshot.phase : activeScreen,
    phaseLabel,
    activeScreen,
    musicRoot: musicRootPath,
    musicRootExists: bootSnapshot.musicRootExists,
    counts,
  };
}

function buildTelemetrySignature(
  bootSnapshot,
  routerState,
  homeState,
  libraryState,
  nowPlayingState,
  audioState,
) {
  const telemetry = buildTelemetrySnapshot(
    bootSnapshot,
    routerState,
    homeState,
    libraryState,
    nowPlayingState,
    audioState,
  );
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
    libraryState?.currentView?.kind || "",
    libraryState?.currentView?.title || "",
    libraryState?.vinylLaunchTarget || 0,
    libraryState?.playlistSheetOpen ? 1 : 0,
    nowPlayingState?.operatingMode || "",
    nowPlayingState?.lyricsOpen ? 1 : 0,
    nowPlayingState?.lyricsMode || "",
    nowPlayingState?.quickActionsOpen ? 1 : 0,
    nowPlayingState?.queueSheetOpen ? 1 : 0,
    nowPlayingState?.detailsSheetOpen ? 1 : 0,
    nowPlayingState?.playlistPickerOpen ? 1 : 0,
    nowPlayingState?.isSeeking ? 1 : 0,
    nowPlayingState?.playMode || "",
    audioState?.activeTrack?.id || "",
    audioState?.isPlaying ? 1 : 0,
    counts.tracks || 0,
    counts.albums || 0,
    counts.artists || 0,
    counts.genres || 0,
    counts.favorites || 0,
    counts.playlists || 0,
  ].join("|");
}

function syncShellIfNeeded(forceSync = false) {
  const bootSnapshot = bootController.getRenderSnapshot();
  const routerState = router.getState();
  const homeState = homeController.getRenderSnapshot();
  const libraryState = libraryController.getRenderSnapshot();
  const nowPlayingState = nowPlayingController.getRenderSnapshot();
  const audioState = audioEngine.getState();
  const nextSignature = buildTelemetrySignature(
    bootSnapshot,
    routerState,
    homeState,
    libraryState,
    nowPlayingState,
    audioState,
  );
  if (!forceSync && nextSignature === lastTelemetrySignature) {
    return;
  }
  lastTelemetrySignature = nextSignature;
  shellController.syncView({
    ...shellState,
    telemetry: buildTelemetrySnapshot(
      bootSnapshot,
      routerState,
      homeState,
      libraryState,
      nowPlayingState,
      audioState,
    ),
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
      libraryController.hydrateCatalog(latestCatalog);
      nowPlayingController.hydrateCatalog(latestCatalog);
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
  libraryState = libraryController.getRenderSnapshot(),
  nowPlayingState = nowPlayingController.getRenderSnapshot(),
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

  if (routerState.activeScreen === "now-playing") {
    const isInteractiveSurface =
      nowPlayingState?.operatingMode === "playing" &&
      !nowPlayingState?.detailsSheetOpen &&
      !nowPlayingState?.playlistPickerOpen &&
      !nowPlayingState?.quickActionsOpen;
    const isPointerDragging = Boolean(
      nowPlayingState?.pointerActive &&
        (nowPlayingState?.pointerMode === "seek" ||
          nowPlayingState?.pointerMode === "volume" ||
          nowPlayingState?.pointerMode === "skip-drag" ||
          nowPlayingState?.pointerMode === "queue-pull" ||
          nowPlayingState?.pointerMode === "back-swipe" ||
          (nowPlayingState?.pointerMode === "queue-list" &&
            nowPlayingState?.pressedQueueRowIndex === null)),
    );
    const isHoveringTarget =
      !isPointerDragging &&
      Boolean(
        hoverPoint &&
          hitTestNowPlayingInteractive(hoverPoint.pointX, hoverPoint.pointY, nowPlayingState),
      );

    displayCanvas.classList.toggle("is-interactive", isInteractiveSurface);
    displayCanvas.classList.toggle("is-dragging", isPointerDragging);
    displayCanvas.classList.toggle("is-clickable", isHoveringTarget);
    return;
  }

  if (routerState.activeScreen === "library") {
    const currentKind = libraryState?.currentView?.kind;
    const isScrollableView =
      libraryState?.vinylLaunchTarget !== 1 &&
      !libraryState?.playlistSheetOpen &&
      currentKind !== "empty";
    const isPointerDragging = Boolean(
      libraryState?.pointerActive &&
        (libraryState?.pointerMode === "arc-scroll" ||
          libraryState?.pointerMode === "back-swipe" ||
          libraryState?.pointerMode === "crate" ||
          (libraryState?.pointerMode === "list" && libraryState?.pressedRowIndex === null)),
    );
    const isHoveringTarget =
      !isPointerDragging &&
      Boolean(
        hoverPoint &&
          hitTestLibraryInteractive(hoverPoint.pointX, hoverPoint.pointY, libraryState),
      );

    displayCanvas.classList.toggle("is-interactive", isScrollableView);
    displayCanvas.classList.toggle("is-dragging", isPointerDragging);
    displayCanvas.classList.toggle("is-clickable", isHoveringTarget);
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
  clearActiveCanvasPointer();
  bootSessionId += 1;
  const currentSessionId = bootSessionId;

  backendReady = false;
  catalogLoaded = false;
  catalogFetchInFlight = false;
  if (rescan) {
    latestCatalog = null;
    coverCache.clear();
  }

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
      coverCache.clear();
      libraryController.hydrateCatalog(latestCatalog);
      nowPlayingController.hydrateCatalog(latestCatalog);
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
  if (routerState.activeScreen === "now-playing") {
    nowPlayingController.skipTrack(-1);
    syncDisplayCanvasCursorClasses(lastHoverPoint);
    syncShellIfNeeded(false);
    return;
  }
  if (routerState.activeScreen === "library") {
    libraryController.stepDetent(-1);
    syncShellIfNeeded(false);
    return;
  }
  const homeState = homeController.getRenderSnapshot();
  if (routerState.activeScreen === "home" && homeState.launchTarget === 0) {
    homeController.stepDetent(-1);
    syncShellIfNeeded(false);
  }
}

function handleNavigateRight() {
  const routerState = router.getState();
  if (routerState.activeScreen === "now-playing") {
    nowPlayingController.skipTrack(1);
    syncDisplayCanvasCursorClasses(lastHoverPoint);
    syncShellIfNeeded(false);
    return;
  }
  if (routerState.activeScreen === "library") {
    libraryController.stepDetent(1);
    syncShellIfNeeded(false);
    return;
  }
  const homeState = homeController.getRenderSnapshot();
  if (routerState.activeScreen === "home" && homeState.launchTarget === 0) {
    homeController.stepDetent(1);
    syncShellIfNeeded(false);
  }
}

function handleNavigateUp() {
  const activeScreen = router.getState().activeScreen;
  if (activeScreen === "now-playing") {
    nowPlayingController.handleWheel(-1);
    syncShellIfNeeded(false);
    return;
  }
  if (activeScreen === "library") {
    if (libraryController.getRenderSnapshot().currentView?.kind === "albums") {
      libraryController.openFocusedAlbumTracks();
    } else {
      libraryController.stepDetent(-1);
    }
    syncShellIfNeeded(false);
  }
}

function handleNavigateDown() {
  const activeScreen = router.getState().activeScreen;
  if (activeScreen === "now-playing") {
    nowPlayingController.handleWheel(1);
    syncShellIfNeeded(false);
    return;
  }
  if (activeScreen === "library") {
    libraryController.stepDetent(1);
    syncShellIfNeeded(false);
  }
}

function handleActivate() {
  const routerState = router.getState();
  if (routerState.activeScreen === "now-playing") {
    nowPlayingController.activateFocused();
    syncDisplayCanvasCursorClasses(lastHoverPoint);
    syncShellIfNeeded(false);
    return;
  }
  if (routerState.activeScreen === "library") {
    libraryController.activateFocused();
    syncDisplayCanvasCursorClasses(lastHoverPoint);
    syncShellIfNeeded(false);
    return;
  }
  if (routerState.activeScreen !== "home") {
    return;
  }
  const homeState = homeController.getRenderSnapshot();
  if (homeState.launchTarget === 0) {
    homeController.launchFocusedApp();
  } else if (homeState.launchedAppId === "now-playing" && homeState.playbackState === "idle") {
    homeController.jumpFromNotPlayingToLibrary();
  }
  handoffHomeAppIfLaunched();
  syncDisplayCanvasCursorClasses(lastHoverPoint);
  syncShellIfNeeded(false);
}

function handleSpaceKey() {
  if (router.getState().activeScreen === "now-playing") {
    nowPlayingController.togglePlayPause();
    syncDisplayCanvasCursorClasses(lastHoverPoint);
    syncShellIfNeeded(false);
    return;
  }
  handleActivate();
}

function handleToggleLyrics() {
  if (router.getState().activeScreen === "now-playing") {
    nowPlayingController.toggleLyrics();
    syncDisplayCanvasCursorClasses(lastHoverPoint);
    syncShellIfNeeded(false);
  }
}

function handleToggleTrackInfo() {
  if (router.getState().activeScreen === "now-playing") {
    nowPlayingController.toggleTrackDetails();
    syncDisplayCanvasCursorClasses(lastHoverPoint);
    syncShellIfNeeded(false);
  }
}

function handleToggleQueue() {
  if (router.getState().activeScreen === "now-playing") {
    nowPlayingController.toggleQueueSheet();
    syncDisplayCanvasCursorClasses(lastHoverPoint);
    syncShellIfNeeded(false);
  }
}

function handleNavigateBack() {
  const routerState = router.getState();
  if (routerState.activeScreen === "now-playing") {
    nowPlayingController.stepBack();
    syncDisplayCanvasCursorClasses(lastHoverPoint);
    syncShellIfNeeded(false);
    return;
  }
  if (routerState.activeScreen === "library") {
    libraryController.stepBack();
    syncDisplayCanvasCursorClasses(lastHoverPoint);
    syncShellIfNeeded(false);
    return;
  }
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
  onNavigateUp: handleNavigateUp,
  onNavigateDown: handleNavigateDown,
  onActivate: handleActivate,
  onSpaceKey: handleSpaceKey,
  onToggleLyrics: handleToggleLyrics,
  onToggleTrackInfo: handleToggleTrackInfo,
  onToggleQueue: handleToggleQueue,
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

  const activeScreen = router.getState().activeScreen;
  if (activeScreen === "home") {
    homeController.handlePointerDown(displayPoint.pointX, displayPoint.pointY);
  } else if (activeScreen === "library") {
    libraryController.handlePointerDown(displayPoint.pointX, displayPoint.pointY);
  } else if (activeScreen === "now-playing") {
    nowPlayingController.handlePointerDown(displayPoint.pointX, displayPoint.pointY);
  }
  syncDisplayCanvasCursorClasses(displayPoint);
});

displayCanvas.addEventListener("pointermove", (pointerEvent) => {
  const hoverPoint = clientPointToDisplayPoint(pointerEvent.clientX, pointerEvent.clientY);
  lastHoverPoint = hoverPoint;

  if (activePointerId === pointerEvent.pointerId && hoverPoint) {
    const activeScreen = router.getState().activeScreen;
    if (activeScreen === "home") {
      homeController.handlePointerMove(hoverPoint.pointX, hoverPoint.pointY);
    } else if (activeScreen === "library") {
      libraryController.handlePointerMove(hoverPoint.pointX, hoverPoint.pointY);
    } else if (activeScreen === "now-playing") {
      nowPlayingController.handlePointerMove(hoverPoint.pointX, hoverPoint.pointY);
    }
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
      handoffHomeAppIfLaunched();
      lastHoverPoint = releasePoint;
    } else {
      homeController.handlePointerCancel();
    }
    syncDisplayCanvasCursorClasses(lastHoverPoint);
    syncShellIfNeeded(false);
    return;
  }

  if (routerState.activeScreen === "library") {
    if (releasePoint) {
      libraryController.handlePointerUp(releasePoint.pointX, releasePoint.pointY);
      lastHoverPoint = releasePoint;
    } else {
      libraryController.handlePointerCancel();
    }
    syncDisplayCanvasCursorClasses(lastHoverPoint);
    syncShellIfNeeded(false);
    return;
  }

  if (routerState.activeScreen === "now-playing") {
    if (releasePoint) {
      nowPlayingController.handlePointerUp(releasePoint.pointX, releasePoint.pointY);
      lastHoverPoint = releasePoint;
    } else {
      nowPlayingController.handlePointerCancel();
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

  const activeScreen = router.getState().activeScreen;
  if (activeScreen === "home") {
    homeController.handlePointerCancel();
  } else if (activeScreen === "library") {
    libraryController.handlePointerCancel();
  } else if (activeScreen === "now-playing") {
    nowPlayingController.handlePointerCancel();
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
    if (routerState.activeScreen === "now-playing") {
      const nowPlayingState = nowPlayingController.getRenderSnapshot();
      if (
        nowPlayingState.operatingMode !== "playing" ||
        nowPlayingState.detailsSheetOpen ||
        nowPlayingState.playlistPickerOpen
      ) {
        return;
      }
      wheelEvent.preventDefault();
      const wheelDelta = wheelEvent.deltaY !== 0 ? wheelEvent.deltaY : wheelEvent.deltaX;
      if (wheelDelta !== 0) {
        nowPlayingController.handleWheel(wheelDelta);
        syncShellIfNeeded(false);
      }
      return;
    }

    const homeState = homeController.getRenderSnapshot();
    const canWheelHome = routerState.activeScreen === "home" && homeState.launchTarget === 0;
    const canWheelLibrary = routerState.activeScreen === "library";
    if (!canWheelHome && !canWheelLibrary) {
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
    const stepDirection = dominantDelta > 0 ? 1 : -1;
    if (canWheelLibrary) {
      libraryController.stepDetent(stepDirection);
    } else {
      homeController.stepDetent(stepDirection);
    }
    syncShellIfNeeded(false);
  },
  { passive: false },
);

function renderDisplayFrame(
  bootSnapshot,
  routerState,
  homeState,
  libraryState,
  nowPlayingState,
  audioState,
) {
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
  } else if (routerState.activeScreen === "now-playing") {
    const catalogAlbums = latestCatalog?.albums || libraryState?.catalog?.albums || [];
    renderNowPlayingScreen(drawingContext, nowPlayingState, {
      appearance: shellState.appearance,
      palette,
      coverCache,
      catalogAlbums,
      animationTimeSeconds: nowPlayingState.idleClockSeconds,
    });
  } else if (routerState.activeScreen === "library") {
    renderLibraryScreen(drawingContext, libraryState, {
      appearance: shellState.appearance,
      palette,
      coverCache,
      animationTimeSeconds: libraryState.idleClockSeconds,
      playback: {
        isPlaying: audioState.isPlaying,
        elapsedSeconds: audioState.elapsedSeconds,
        durationSeconds: audioState.durationSeconds,
        volumeLevel: audioState.volumeLevel,
      },
    });
  } else {
    const counts = latestCatalog?.counts || bootSnapshot.backendCounts;
    renderHomeScreen(drawingContext, homeState, {
      appearance: shellState.appearance,
      palette,
      counts,
      activeAlbum: audioState.activeAlbum,
      activeTrack: audioState.activeTrack,
      trackTitle: audioState.activeTrack?.title,
      trackElapsedSeconds: audioState.elapsedSeconds,
      trackDurationSeconds: audioState.durationSeconds,
      coverCache,
      animationTimeSeconds: homeState.idleClockSeconds,
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

  audioEngine.step(frameDeltaSeconds);
  const audioState = audioEngine.getState();
  libraryController.setActiveTrackId(audioState.activeTrack?.id || null);
  libraryController.setPlaybackState(audioState.isPlaying);
  homeController.syncPlaybackState(buildHomePlaybackTelemetry(audioState));

  router.stepTransition(frameDeltaSeconds);
  let routerState = router.getState();

  if (routerState.activeScreen === "home") {
    homeController.step(frameDeltaSeconds);
    if (handoffHomeAppIfLaunched()) {
      routerState = router.getState();
    }
  } else if (routerState.activeScreen === "library") {
    libraryController.step(frameDeltaSeconds);
  } else if (routerState.activeScreen === "now-playing") {
    nowPlayingController.step(frameDeltaSeconds);
    if (!nowPlayingController.getRenderSnapshot().isSeeking) {
      nowPlayingController.syncAudioState(audioState);
    }
  }

  const homeState = homeController.getRenderSnapshot();
  const libraryState = libraryController.getRenderSnapshot();
  const nowPlayingState = nowPlayingController.getRenderSnapshot();
  renderDisplayFrame(
    bootSnapshot,
    routerState,
    homeState,
    libraryState,
    nowPlayingState,
    audioState,
  );
  syncDisplayCanvasCursorClasses(
    lastHoverPoint,
    routerState,
    bootSnapshot,
    homeState,
    libraryState,
    nowPlayingState,
  );
  syncShellIfNeeded(false);

  window.requestAnimationFrame(animationTick);
}

/* Kick off initial boot scan and enter the 60fps Canvas loop. */
syncShellIfNeeded(true);
void startBootScan(bootSessionId, { rescan: true });
window.requestAnimationFrame(animationTick);
