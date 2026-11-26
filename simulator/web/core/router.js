/**
 * Screen routing state machine managing boot-to-home handoff, entry easing
 * progress, and reboot resets for the DialOS simulator.
 */

import { clamp } from "./geometry.js";

const SCREEN_ENTRY_DURATION_SECONDS = 0.45;

/**
 * Create a screen router initialized at `initialScreen`.
 *
 * @param {object} [options] Router initialization options.
 * @param {string} [options.initialScreen="boot"] Starting screen name.
 * @returns {{
 *   getState: () => {
 *     activeScreen: string,
 *     previousScreen: string | null,
 *     handoffProgress: number,
 *     entryProgress: number
 *   },
 *   completeBootToHome: () => void,
 *   navigateTo: (screenId: string) => void,
 *   stepTransition: (deltaSeconds: number) => void,
 *   reboot: () => void
 * }} Router control API.
 */
export function createRouter({ initialScreen = "boot" } = {}) {
  const startingScreen =
    typeof initialScreen === "string" && initialScreen.length > 0 ? initialScreen : "boot";

  let activeScreen = startingScreen;
  let previousScreen = null;
  let handoffProgress = startingScreen === "boot" ? 0 : 1;
  let entryProgress = startingScreen === "boot" ? 0 : 1;

  /**
   * Return the current router transition state.
   *
   * @returns {{
   *   activeScreen: string,
   *   previousScreen: string | null,
   *   handoffProgress: number,
   *   entryProgress: number
   * }} Shallow state snapshot.
   */
  function getState() {
    return {
      activeScreen,
      previousScreen,
      handoffProgress,
      entryProgress,
    };
  }

  /**
   * Transition from boot into the home screen with entryProgress reset to 0.
   */
  function completeBootToHome() {
    previousScreen = activeScreen;
    activeScreen = "home";
    handoffProgress = 1;
    entryProgress = 0;
  }

  /**
   * Switch directly to `screenId` with entryProgress reset to 0.
   * Ignores empty or non-string screen identifiers.
   *
   * @param {string} screenId Destination screen identifier.
   */
  function navigateTo(screenId) {
    if (typeof screenId !== "string" || screenId.trim().length === 0) {
      return;
    }
    previousScreen = activeScreen;
    activeScreen = screenId;
    handoffProgress = screenId === "boot" ? 0 : 1;
    entryProgress = 0;
  }

  /**
   * Advance active screen entry transition progress toward 1.
   * Negative or non-finite deltas are safely ignored.
   *
   * @param {number} deltaSeconds Elapsed frame time in seconds.
   */
  function stepTransition(deltaSeconds) {
    const safeDelta = Number.isFinite(deltaSeconds) && deltaSeconds > 0 ? deltaSeconds : 0;
    if (safeDelta === 0) {
      return;
    }
    if (activeScreen !== "boot" && entryProgress < 1) {
      entryProgress = clamp(entryProgress + safeDelta / SCREEN_ENTRY_DURATION_SECONDS, 0, 1);
    }
  }

  /**
   * Reset routing back to the boot screen.
   */
  function reboot() {
    previousScreen = activeScreen;
    activeScreen = "boot";
    handoffProgress = 0;
    entryProgress = 0;
  }

  return {
    getState,
    completeBootToHome,
    navigateTo,
    stepTransition,
    reboot,
  };
}
