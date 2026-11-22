/**
 * DOM controller for the DialOS simulator housing, bottom controls, Dev popover,
 * outside-click dismissal, and keyboard shortcuts.
 */

import { formatIntegerCount } from "../core/geometry.js";

function isEditableEventTarget(target) {
  if (!(target instanceof HTMLElement)) {
    return false;
  }
  const tagName = target.tagName;
  return (
    target.isContentEditable ||
    tagName === "INPUT" ||
    tagName === "TEXTAREA" ||
    tagName === "SELECT"
  );
}

function formatTelemetryPhase(telemetry) {
  if (typeof telemetry?.phaseLabel === "string" && telemetry.phaseLabel.length > 0) {
    return telemetry.phaseLabel;
  }
  const phase = telemetry?.phase;
  if (phase === "warning") {
    return "WARNING";
  }
  if (phase === "completed") {
    return telemetry?.musicRootExists !== false ? "HOME READY" : "HOME (NO FOLDER)";
  }
  return typeof phase === "string" && phase.length > 0 ? phase.toUpperCase() : "WAKE";
}

function formatTelemetryCounts(counts) {
  const safeCounts = counts && typeof counts === "object" ? counts : {};
  return (
    `${formatIntegerCount(safeCounts.tracks)} TRK \u00b7 ` +
    `${formatIntegerCount(safeCounts.albums)} ALB \u00b7 ` +
    `${formatIntegerCount(safeCounts.artists)} ART`
  );
}

/**
 * Bind UI control buttons, Dev popover interactions, and global keyboard shortcuts.
 *
 * @param {object} callbacks Action handlers supplied by main.js.
 * @returns {{
 *   syncView: (shellState: object) => void,
 *   setDevMenuOpen: (open: boolean) => void,
 *   destroy: () => void
 * }} Shell controller API.
 */
export function createShellController(callbacks = {}) {
  const screenViewport = document.getElementById("screen-viewport");
  const glassSheen = document.getElementById("glass-sheen");
  const rebootButton = document.getElementById("reboot-button");
  const appearanceToggle = document.getElementById("appearance-toggle");
  const appearanceLabel = document.getElementById("appearance-label");
  const zoomToggle = document.getElementById("zoom-toggle");
  const devMenuAnchor = document.getElementById("dev-menu-anchor");
  const devMenuButton = document.getElementById("dev-menu-button");
  const devPopover = document.getElementById("dev-popover");
  const safeGuidesToggle = document.getElementById("safe-guides-toggle");
  const safeGuidesPill = document.getElementById("safe-guides-pill");
  const opticalGlassToggle = document.getElementById("optical-glass-toggle");
  const opticalGlassPill = document.getElementById("optical-glass-pill");
  const engineDelayToggle = document.getElementById("engine-delay-toggle");
  const engineDelayPill = document.getElementById("engine-delay-pill");
  const rescanLibraryButton = document.getElementById("rescan-library-button");
  const rescanStatusPill = document.getElementById("rescan-status-pill");
  const telemetryPhase = document.getElementById("telemetry-phase");
  const telemetryMusicRoot = document.getElementById("telemetry-music-root");
  const telemetryCounts = document.getElementById("telemetry-counts");

  let devMenuOpen = false;

  /**
   * Open or close the compact Dev popover menu.
   *
   * @param {boolean} shouldOpen Whether popover should be visible.
   */
  function setDevMenuOpen(shouldOpen) {
    devMenuOpen = Boolean(shouldOpen);
    if (devPopover) {
      devPopover.classList.toggle("is-hidden", !devMenuOpen);
    }
    if (devMenuButton) {
      devMenuButton.setAttribute("aria-expanded", String(devMenuOpen));
      devMenuButton.classList.toggle("is-toggled", devMenuOpen);
    }
  }

  function handleDocumentPointerDown(pointerEvent) {
    if (!devMenuOpen || !devMenuAnchor) {
      return;
    }
    const targetNode = pointerEvent.target;
    if (targetNode instanceof Node && !devMenuAnchor.contains(targetNode)) {
      setDevMenuOpen(false);
    }
  }

  function handleWindowKeyDown(keyboardEvent) {
    if (
      keyboardEvent.isComposing ||
      keyboardEvent.metaKey ||
      keyboardEvent.ctrlKey ||
      keyboardEvent.altKey ||
      isEditableEventTarget(keyboardEvent.target)
    ) {
      return;
    }

    const pressedKey = keyboardEvent.key;
    if (pressedKey === "Escape") {
      if (devMenuOpen) {
        keyboardEvent.preventDefault();
        setDevMenuOpen(false);
      } else if (callbacks.onNavigateBack) {
        keyboardEvent.preventDefault();
        callbacks.onNavigateBack();
      }
      return;
    }

    if (pressedKey === "ArrowLeft") {
      keyboardEvent.preventDefault();
      callbacks.onNavigateLeft?.();
      return;
    }

    if (pressedKey === "ArrowRight") {
      keyboardEvent.preventDefault();
      callbacks.onNavigateRight?.();
      return;
    }

    if (pressedKey === "ArrowUp") {
      keyboardEvent.preventDefault();
      callbacks.onNavigateUp?.();
      return;
    }

    if (pressedKey === "ArrowDown") {
      keyboardEvent.preventDefault();
      callbacks.onNavigateDown?.();
      return;
    }

    if (pressedKey === "Enter" || pressedKey === " ") {
      if (
        !isEditableEventTarget(keyboardEvent.target) &&
        !(keyboardEvent.target instanceof HTMLButtonElement) &&
        !devMenuOpen
      ) {
        keyboardEvent.preventDefault();
        callbacks.onActivate?.();
      }
      return;
    }

    if (pressedKey === "Backspace") {
      keyboardEvent.preventDefault();
      callbacks.onNavigateBack?.();
      return;
    }

    const normalizedKey = pressedKey.toLowerCase();
    if (normalizedKey === "r") {
      keyboardEvent.preventDefault();
      callbacks.onReboot?.();
    } else if (normalizedKey === "t") {
      keyboardEvent.preventDefault();
      callbacks.onToggleAppearance?.();
    } else if (normalizedKey === "z") {
      keyboardEvent.preventDefault();
      callbacks.onToggleZoom?.();
    } else if (normalizedKey === "g") {
      keyboardEvent.preventDefault();
      callbacks.onToggleSafeGuides?.();
    }
  }

  function handleRebootClick() {
    callbacks.onReboot?.();
  }

  function handleAppearanceClick() {
    callbacks.onToggleAppearance?.();
  }

  function handleZoomClick() {
    callbacks.onToggleZoom?.();
  }

  function handleDevMenuClick() {
    setDevMenuOpen(!devMenuOpen);
  }

  function handleSafeGuidesClick() {
    callbacks.onToggleSafeGuides?.();
  }

  function handleOpticalGlassClick() {
    callbacks.onToggleOpticalGlass?.();
  }

  function handleEngineDelayClick() {
    callbacks.onToggleEngineDelay?.();
  }

  function handleRescanClick() {
    callbacks.onRescanLibrary?.();
  }

  rebootButton?.addEventListener("click", handleRebootClick);
  appearanceToggle?.addEventListener("click", handleAppearanceClick);
  zoomToggle?.addEventListener("click", handleZoomClick);
  devMenuButton?.addEventListener("click", handleDevMenuClick);
  safeGuidesToggle?.addEventListener("click", handleSafeGuidesClick);
  opticalGlassToggle?.addEventListener("click", handleOpticalGlassClick);
  engineDelayToggle?.addEventListener("click", handleEngineDelayClick);
  rescanLibraryButton?.addEventListener("click", handleRescanClick);
  document.addEventListener("pointerdown", handleDocumentPointerDown);
  window.addEventListener("keydown", handleWindowKeyDown);

  /**
   * Synchronize shell DOM controls and Dev telemetry from simulator state.
   *
   * @param {object} shellState Current UI and backend telemetry snapshot.
   */
  function syncView(shellState) {
    const isLightMode = shellState.appearance === "light";
    document.documentElement.dataset.mode = isLightMode ? "light" : "dark";

    if (appearanceLabel) {
      appearanceLabel.textContent = isLightMode ? "Light Mode" : "Dark Mode";
    }
    if (appearanceToggle) {
      appearanceToggle.setAttribute("aria-pressed", String(isLightMode));
    }

    const isMagnified = Boolean(shellState.magnified);
    if (screenViewport) {
      screenViewport.classList.toggle("is-magnified", isMagnified);
    }
    if (zoomToggle) {
      zoomToggle.textContent = isMagnified ? "1.4\u00d7" : "1:1";
      zoomToggle.classList.toggle("is-toggled", isMagnified);
      zoomToggle.setAttribute("aria-pressed", String(isMagnified));
    }

    const opticalGlassEnabled = Boolean(shellState.opticalGlass);
    if (glassSheen) {
      glassSheen.classList.toggle("is-visible", opticalGlassEnabled);
    }
    if (opticalGlassToggle && opticalGlassPill) {
      opticalGlassToggle.setAttribute("aria-pressed", String(opticalGlassEnabled));
      opticalGlassPill.textContent = opticalGlassEnabled ? "ON" : "OFF";
      opticalGlassPill.classList.toggle("is-on", opticalGlassEnabled);
    }

    const safeGuidesEnabled = Boolean(shellState.safeGuides);
    if (safeGuidesToggle && safeGuidesPill) {
      safeGuidesToggle.setAttribute("aria-pressed", String(safeGuidesEnabled));
      safeGuidesPill.textContent = safeGuidesEnabled ? "ON" : "OFF";
      safeGuidesPill.classList.toggle("is-on", safeGuidesEnabled);
    }

    const engineDelayEnabled = Boolean(shellState.engineDelayFault);
    if (engineDelayToggle && engineDelayPill) {
      engineDelayToggle.setAttribute("aria-pressed", String(engineDelayEnabled));
      engineDelayPill.textContent = engineDelayEnabled ? "FAULT" : "OFF";
      engineDelayPill.classList.toggle("is-fault", engineDelayEnabled);
    }

    const rescanning = Boolean(shellState.rescanning);
    if (rescanLibraryButton && rescanStatusPill) {
      rescanLibraryButton.disabled = rescanning;
      rescanStatusPill.textContent = rescanning ? "SCAN..." : "RUN";
      rescanStatusPill.classList.toggle("is-busy", rescanning);
    }

    const telemetry = shellState.telemetry || {};
    const phaseLabel = formatTelemetryPhase(telemetry);
    if (telemetryPhase) {
      telemetryPhase.textContent = phaseLabel;
      telemetryPhase.classList.toggle("is-alert", telemetry.phase === "warning");
    }

    if (telemetryMusicRoot) {
      const musicRootText =
        typeof telemetry.musicRoot === "string" && telemetry.musicRoot.length > 0
          ? telemetry.musicRootExists === false
            ? `${telemetry.musicRoot} (missing)`
            : telemetry.musicRoot
          : "--";
      telemetryMusicRoot.textContent = musicRootText;
      telemetryMusicRoot.title = musicRootText;
      telemetryMusicRoot.classList.toggle("is-alert", telemetry.musicRootExists === false);
    }

    if (telemetryCounts) {
      telemetryCounts.textContent = formatTelemetryCounts(telemetry.counts);
    }
  }

  /**
   * Unregister DOM event listeners attached by the shell controller.
   */
  function destroy() {
    rebootButton?.removeEventListener("click", handleRebootClick);
    appearanceToggle?.removeEventListener("click", handleAppearanceClick);
    zoomToggle?.removeEventListener("click", handleZoomClick);
    devMenuButton?.removeEventListener("click", handleDevMenuClick);
    safeGuidesToggle?.removeEventListener("click", handleSafeGuidesClick);
    opticalGlassToggle?.removeEventListener("click", handleOpticalGlassClick);
    engineDelayToggle?.removeEventListener("click", handleEngineDelayClick);
    rescanLibraryButton?.removeEventListener("click", handleRescanClick);
    document.removeEventListener("pointerdown", handleDocumentPointerDown);
    window.removeEventListener("keydown", handleWindowKeyDown);
  }

  return {
    syncView,
    setDevMenuOpen,
    destroy,
  };
}
