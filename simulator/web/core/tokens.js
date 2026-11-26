/**
 * Semantic color palettes and typography stacks for the DialOS 360x360 display.
 * Uses only Dark Mode and Light Mode tokens defined for the DialOS visual system.
 */

export const APPEARANCE_PALETTES = Object.freeze({
  dark: Object.freeze({
    modeLabel: "DARK MODE",
    canvas: "#060709",
    surface: "#12151B",
    hairline: "#232833",
    hairlineStrong: "#485060",
    inkPrimary: "#F5F6F8",
    inkSecondary: "#8D96A6",
    signalHighlight: "#C0FE05",
    signalHighlightInk: "#060709",
    signalActive: "#FF2E17",
    signalActiveSoft: "rgba(255, 46, 23, 0.22)",
    signalHighlightSoft: "rgba(192, 254, 5, 0.16)",
    cubeHighlightFaces: Object.freeze({
      top: "#DAFF66",
      right: "#C0FE05",
      left: "#8BB803",
    }),
    cubeInkFaces: Object.freeze({
      top: "#FFFFFF",
      right: "#D6DCE6",
      left: "#8E97A6",
    }),
    cubeWireframe: "rgba(116, 128, 148, 0.34)",
    cubeEdgeStroke: "#060709",
    swatches: Object.freeze([
      Object.freeze({ role: "Canvas / Void", hex: "#060709" }),
      Object.freeze({ role: "Surface Plate", hex: "#12151B" }),
      Object.freeze({ role: "Hairline Grid", hex: "#232833" }),
      Object.freeze({ role: "Primary Ink", hex: "#F5F6F8" }),
      Object.freeze({ role: "Signal Highlight", hex: "#C0FE05" }),
      Object.freeze({ role: "Signal Alert", hex: "#FF2E17" }),
    ]),
  }),
  light: Object.freeze({
    modeLabel: "LIGHT MODE",
    canvas: "#E6E8EB",
    surface: "#D6D9DF",
    hairline: "#C0C5CE",
    hairlineStrong: "#828A96",
    inkPrimary: "#2E3238",
    inkSecondary: "#626A76",
    signalHighlight: "#E85A24",
    signalHighlightInk: "#FFFFFF",
    signalActive: "#CC3124",
    signalActiveSoft: "rgba(204, 49, 36, 0.16)",
    signalHighlightSoft: "rgba(232, 90, 36, 0.20)",
    cubeHighlightFaces: Object.freeze({
      top: "#F27B4C",
      right: "#E85A24",
      left: "#B84316",
    }),
    cubeInkFaces: Object.freeze({
      top: "#4E545E",
      right: "#343940",
      left: "#22252B",
    }),
    cubeWireframe: "rgba(114, 122, 134, 0.38)",
    cubeEdgeStroke: "#E6E8EB",
    swatches: Object.freeze([
      Object.freeze({ role: "Canvas / Matte Grey", hex: "#E6E8EB" }),
      Object.freeze({ role: "Surface Plate", hex: "#D6D9DF" }),
      Object.freeze({ role: "Hairline Grid", hex: "#C0C5CE" }),
      Object.freeze({ role: "Primary Ink / Dark Grey", hex: "#2E3238" }),
      Object.freeze({ role: "Signal Orange", hex: "#E85A24" }),
      Object.freeze({ role: "Signal Alert", hex: "#CC3124" }),
    ]),
  }),
});

export const FONT_DISPLAY =
  "'SF Pro Display', 'Helvetica Neue', 'Inter', -apple-system, BlinkMacSystemFont, sans-serif";

export const FONT_MONO = "'SF Mono', 'JetBrains Mono', 'Menlo', monospace";
