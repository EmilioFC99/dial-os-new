/**
 * Pure Canvas 2D renderer and hit-tester for the DialOS 02 HOME screen:
 * 3D optical-glass carousel cubes with living icons, Boot -> Home emblem morph,
 * launched destination overlays, and left-edge back-swipe arc feedback.
 */

import {
  DISPLAY_CENTER_PX,
  FULL_CIRCLE_RADIANS,
  TOP_ANGLE_RADIANS,
  clamp,
  easeInOutCubic,
  easeOutCubic,
  easeOutExpo,
  lerp,
  normalizeRange,
  projectPoint3d,
  rotatePoint3d,
} from "../core/geometry.js";
import {
  HOME_APPS,
  HOME_CONSTANTS,
  computeHomeGlassSlotLayout,
  cyclicFocusWeight,
  resolveHomeAppSubtitle,
  wrapAppIndex,
} from "../core/home_controller.js";
import { drawDialOsEmblem } from "../core/primitives.js";
import { APPEARANCE_PALETTES, FONT_DISPLAY, FONT_MONO } from "../core/tokens.js";
import { drawLibraryVinylDisc, drawMarqueeCanvasText } from "./library_screen.js";

const ALPHA_VISIBILITY_THRESHOLD = 0.005;
const PERCENT_SCALE = 100;
const DEFAULT_VOLUME_RATIO = 0.62;
const DEFAULT_BATTERY_PERCENT = 94;
const DEFAULT_TRACK_DURATION_SECONDS = 248;

const HOME_EMBLEM_MORPH_END_PROGRESS = 0.44;
const HOME_EMBLEM_FADE_END_PROGRESS = 0.42;
const HOME_EMBLEM_SCALE_RATIO = 0.82;
const LAUNCH_OVERLAY_MIN_PROGRESS = 0.25;
const BACK_SWIPE_ARC_MIN_PROGRESS = 0.02;
const BACK_SWIPE_ARC_RADIUS_PX = 168;
const BACK_SWIPE_ARC_SPAN_RADIANS = 0.34;

const CUBE_ICON_BASE_HALF_SIZE_PX = 45;
const CUBE_INNER_WALL_RATIO = 0.85;
const CUBE_HIT_SCALE = 1.25;
const CUBE_HIT_PADDING_PX = 8;

const RIM_SHEEN_RADIUS_PX = 175;
const GLASS_FLOOR_TOP_Y = 202;
const GLASS_FLOOR_BOTTOM_Y = 232;
const GLASS_FLOOR_RAIL_SPACING_PX = 36;
const CAUSTIC_CENTER_Y = 216;
const TOP_HANDLE_HALF_WIDTH_PX = 14;
const TOP_HANDLE_Y = 24;
const TOP_HANDLE_HEIGHT_PX = 3;

const BROWSE_LIBRARY_CTA = Object.freeze({
  topY: 236,
  width: 158,
  height: 36,
  halfWidth: 79,
});

const BACK_TO_HOME_CTA = Object.freeze({
  topY: 240,
  width: 148,
  height: 34,
  halfWidth: 74,
});

const CUBE_LOCAL_VERTICES = Object.freeze([
  [-1, -1, -1],
  [1, -1, -1],
  [1, 1, -1],
  [-1, 1, -1],
  [-1, -1, 1],
  [1, -1, 1],
  [1, 1, 1],
  [-1, 1, 1],
]);

const CUBE_FACES = Object.freeze([
  { indices: [4, 5, 6, 7], normal: [0, 0, 1] },
  { indices: [1, 0, 3, 2], normal: [0, 0, -1] },
  { indices: [5, 1, 2, 6], normal: [1, 0, 0] },
  { indices: [0, 4, 7, 3], normal: [-1, 0, 0] },
  { indices: [0, 1, 5, 4], normal: [0, -1, 0] },
  { indices: [7, 6, 2, 3], normal: [0, 1, 0] },
]);

const HOME_CUBE_EDGES = Object.freeze([
  [0, 1],
  [1, 2],
  [2, 3],
  [3, 0],
  [4, 5],
  [5, 6],
  [6, 7],
  [7, 4],
  [0, 4],
  [1, 5],
  [2, 6],
  [3, 7],
]);

function resolveSafeCounts(counts) {
  const sourceCounts = counts && typeof counts === "object" ? counts : {};
  const normalizeCount = (countValue) =>
    Number.isFinite(countValue) ? Math.max(0, Math.round(countValue)) : 0;

  return {
    tracks: normalizeCount(sourceCounts.tracks),
    albums: normalizeCount(sourceCounts.albums),
    artists: normalizeCount(sourceCounts.artists),
    genres: normalizeCount(sourceCounts.genres),
    favorites: normalizeCount(sourceCounts.favorites),
    playlists: normalizeCount(sourceCounts.playlists),
  };
}

function resolveRenderContext(renderContext) {
  const sourceContext = renderContext && typeof renderContext === "object" ? renderContext : {};
  const appearance = sourceContext.appearance === "light" ? "light" : "dark";
  const palette = sourceContext.palette || APPEARANCE_PALETTES[appearance];
  const counts = resolveSafeCounts(sourceContext.counts);
  const trackTitle =
    typeof sourceContext.trackTitle === "string" && sourceContext.trackTitle.trim().length > 0
      ? sourceContext.trackTitle.trim()
      : "MIDNIGHT GROOVE";
  const trackElapsedSeconds =
    Number.isFinite(sourceContext.trackElapsedSeconds) && sourceContext.trackElapsedSeconds >= 0
      ? sourceContext.trackElapsedSeconds
      : 0;
  const trackDurationSeconds =
    Number.isFinite(sourceContext.trackDurationSeconds) && sourceContext.trackDurationSeconds > 0
      ? sourceContext.trackDurationSeconds
      : DEFAULT_TRACK_DURATION_SECONDS;

  const activeAlbum =
    sourceContext.activeAlbum && typeof sourceContext.activeAlbum === "object"
      ? sourceContext.activeAlbum
      : null;
  const coverCache = sourceContext.coverCache || null;
  const animationTimeSeconds = Number.isFinite(sourceContext.animationTimeSeconds)
    ? sourceContext.animationTimeSeconds
    : null;

  return {
    appearance,
    palette,
    counts,
    trackTitle,
    trackElapsedSeconds,
    trackDurationSeconds,
    activeAlbum,
    coverCache,
    animationTimeSeconds,
  };
}

function resolveFaceTone(rotatedNormal, facePalette) {
  if (rotatedNormal.y < -0.35) {
    return facePalette.top;
  }
  if (rotatedNormal.x >= 0) {
    return facePalette.right;
  }
  return facePalette.left;
}

function projectLocalPoint(
  localX,
  localY,
  localZ,
  centerX,
  centerY,
  cubeScale,
  angleX,
  angleY,
  angleZ,
) {
  const rotated = rotatePoint3d(
    localX * cubeScale,
    localY * cubeScale,
    localZ * cubeScale,
    angleX,
    angleY,
    angleZ,
  );
  return projectPoint3d(
    rotated.x,
    rotated.y,
    rotated.z,
    centerX,
    centerY,
    HOME_CONSTANTS.PERSPECTIVE_FOCAL_PX,
  );
}

function traceFacePolygon(drawingContext, verticesList, indices) {
  drawingContext.beginPath();
  for (let cornerIndex = 0; cornerIndex < indices.length; cornerIndex += 1) {
    const point = verticesList[indices[cornerIndex]];
    if (cornerIndex === 0) {
      drawingContext.moveTo(point.screenX, point.screenY);
    } else {
      drawingContext.lineTo(point.screenX, point.screenY);
    }
  }
  drawingContext.closePath();
}

/**
 * Draw the pure-2D geometric vinyl album artwork disc inside a circular clip.
 *
 * @param {CanvasRenderingContext2D} drawingContext Canvas 2D context.
 * @param {number} centerX Disc center X coordinate.
 * @param {number} centerY Disc center Y coordinate.
 * @param {number} radius Disc outer radius in pixels.
 * @param {number} rotationRadians Vinyl spin angle in radians.
 * @param {object} palette Semantic Dark/Light appearance palette.
 * @param {string} appearance Semantic appearance ("dark" | "light").
 * @param {number} alpha Layer opacity.
 * @param {object | null} [activeAlbum=null] Optional active album spec for real cover art.
 * @param {object | null} [coverCache=null] Optional cover image cache.
 */
function drawAlbumArtworkDisc(
  drawingContext,
  centerX,
  centerY,
  radius,
  rotationRadians,
  palette,
  appearance,
  alpha,
  activeAlbum = null,
  coverCache = null,
) {
  if (alpha <= ALPHA_VISIBILITY_THRESHOLD || radius <= 2) {
    return;
  }

  if (activeAlbum) {
    drawLibraryVinylDisc(
      drawingContext,
      activeAlbum,
      centerX,
      centerY,
      radius,
      rotationRadians,
      palette,
      appearance,
      alpha,
      coverCache,
    );
    return;
  }

  drawingContext.save();
  drawingContext.globalAlpha = alpha;
  drawingContext.translate(centerX, centerY);

  drawingContext.beginPath();
  drawingContext.arc(0, 0, radius, 0, FULL_CIRCLE_RADIANS);
  drawingContext.clip();

  drawingContext.rotate(rotationRadians);

  const isDark = appearance === "dark";
  const discBaseColor = isDark ? "#11151C" : "#242830";
  const discGrooveColor = isDark ? "rgba(245, 246, 248, 0.08)" : "rgba(245, 246, 248, 0.11)";
  const discStarkBlock = "#F5F6F8";
  const discContrastSlate = isDark ? "#1E2430" : "#353B46";

  drawingContext.fillStyle = discBaseColor;
  drawingContext.fillRect(-radius, -radius, radius * 2, radius * 2);

  drawingContext.fillStyle = discContrastSlate;
  drawingContext.beginPath();
  drawingContext.moveTo(-radius, -radius * 0.18);
  drawingContext.lineTo(radius * 0.35, -radius);
  drawingContext.lineTo(radius, -radius);
  drawingContext.lineTo(radius, radius * 0.25);
  drawingContext.lineTo(-radius * 0.42, radius);
  drawingContext.lineTo(-radius, radius);
  drawingContext.closePath();
  drawingContext.fill();

  drawingContext.fillStyle = palette.signalHighlight;
  drawingContext.beginPath();
  drawingContext.moveTo(-radius * 0.14, -radius);
  drawingContext.lineTo(radius * 0.56, -radius);
  drawingContext.lineTo(-radius * 0.24, radius);
  drawingContext.lineTo(-radius * 0.68, radius);
  drawingContext.closePath();
  drawingContext.fill();

  drawingContext.strokeStyle = discStarkBlock;
  drawingContext.lineWidth = Math.max(3, radius * 0.14);
  drawingContext.lineCap = "butt";
  drawingContext.beginPath();
  drawingContext.arc(0, 0, radius * 0.62, -Math.PI * 0.22, Math.PI * 0.48);
  drawingContext.stroke();

  drawingContext.fillStyle = discStarkBlock;
  const barHeight = Math.max(2, radius * 0.045);
  const barPitch = Math.max(4, radius * 0.095);
  for (let barIndex = 0; barIndex < 4; barIndex += 1) {
    const barWidth = radius * (0.48 - barIndex * 0.08);
    drawingContext.fillRect(-radius * 0.78, -radius * 0.58 + barIndex * barPitch, barWidth, barHeight);
  }

  drawingContext.strokeStyle = discGrooveColor;
  drawingContext.lineWidth = Math.max(0.8, radius * 0.008);
  const grooveRatios = [0.88, 0.78, 0.68, 0.48];
  for (let grooveIndex = 0; grooveIndex < grooveRatios.length; grooveIndex += 1) {
    drawingContext.beginPath();
    drawingContext.arc(0, 0, radius * grooveRatios[grooveIndex], 0, FULL_CIRCLE_RADIANS);
    drawingContext.stroke();
  }

  const labelRadius = radius * 0.28;
  drawingContext.beginPath();
  drawingContext.arc(0, 0, labelRadius, 0, FULL_CIRCLE_RADIANS);
  drawingContext.fillStyle = discBaseColor;
  drawingContext.fill();
  drawingContext.strokeStyle = discStarkBlock;
  drawingContext.lineWidth = Math.max(1.2, radius * 0.018);
  drawingContext.stroke();

  const labelBlockSize = Math.max(3, radius * 0.075);
  drawingContext.fillStyle = palette.signalHighlight;
  drawingContext.fillRect(labelRadius * 0.22, -labelBlockSize / 2, labelBlockSize, labelBlockSize);

  drawingContext.beginPath();
  drawingContext.arc(0, 0, Math.max(2.5, radius * 0.075), 0, FULL_CIRCLE_RADIANS);
  drawingContext.fillStyle = "#060709";
  drawingContext.fill();

  drawingContext.restore();
}

/**
 * Draw the Home spatial optical-glass stage: peripheral rim sheen arcs,
 * perspective floor grid, caustic reflection, top handle bar, and status beacon.
 */
function drawHomeGlassStage(
  drawingContext,
  scrollPosition,
  bobOffsetY,
  playbackState,
  volumeLevel,
  appearance,
  chromeAlpha,
  palette,
) {
  if (chromeAlpha <= ALPHA_VISIBILITY_THRESHOLD) {
    return;
  }

  const isDark = appearance === "dark";
  drawingContext.save();
  drawingContext.globalAlpha = chromeAlpha;

  /* 1. Peripheral optical glass rim sheen (zero clock/compass ticks) */
  drawingContext.lineCap = "round";
  drawingContext.beginPath();
  drawingContext.arc(
    DISPLAY_CENTER_PX,
    DISPLAY_CENTER_PX,
    RIM_SHEEN_RADIUS_PX,
    -Math.PI * 0.84,
    -Math.PI * 0.30,
  );
  drawingContext.strokeStyle = isDark ? "rgba(235, 244, 255, 0.16)" : "rgba(255, 255, 255, 0.72)";
  drawingContext.lineWidth = 2;
  drawingContext.stroke();

  drawingContext.beginPath();
  drawingContext.arc(
    DISPLAY_CENTER_PX,
    DISPLAY_CENTER_PX,
    RIM_SHEEN_RADIUS_PX,
    Math.PI * 0.14,
    Math.PI * 0.44,
  );
  drawingContext.strokeStyle = isDark ? "rgba(192, 254, 5, 0.22)" : "rgba(232, 90, 36, 0.24)";
  drawingContext.lineWidth = 1.6;
  drawingContext.stroke();

  /* 2. Perspective glass floor plane scrolling continuously with the infinite wheel */
  const gridColor = isDark ? "rgba(196, 218, 250, 0.14)" : "rgba(46, 52, 62, 0.13)";
  drawingContext.save();
  drawingContext.strokeStyle = gridColor;
  drawingContext.lineWidth = 1;

  const floorRowRatios = [0.0, 0.36, 0.74, 1.0];
  for (let rowIndex = 0; rowIndex < floorRowRatios.length; rowIndex += 1) {
    const rowRatio = floorRowRatios[rowIndex];
    const rowY = lerp(GLASS_FLOOR_TOP_Y, GLASS_FLOOR_BOTTOM_Y, rowRatio);
    const halfSpan = lerp(96, 134, rowRatio);
    drawingContext.globalAlpha = chromeAlpha * lerp(0.35, 0.85, 1 - Math.abs(rowRatio - 0.5) * 1.4);
    drawingContext.beginPath();
    drawingContext.moveTo(DISPLAY_CENTER_PX - halfSpan, rowY);
    drawingContext.lineTo(DISPLAY_CENTER_PX + halfSpan, rowY);
    drawingContext.stroke();
  }

  const fractionalShift = ((scrollPosition % 1) + 1) % 1;
  for (let railIndex = -5; railIndex <= 5; railIndex += 1) {
    const baseOffset = (railIndex - fractionalShift) * GLASS_FLOOR_RAIL_SPACING_PX;
    const topX = DISPLAY_CENTER_PX + baseOffset * 0.56;
    const bottomX = DISPLAY_CENTER_PX + baseOffset * 1.18;
    const edgeFade = 1 - normalizeRange(Math.abs(baseOffset), 86, 162);
    if (edgeFade <= 0.02) {
      continue;
    }
    drawingContext.globalAlpha = chromeAlpha * edgeFade * 0.75;
    drawingContext.beginPath();
    drawingContext.moveTo(topX, GLASS_FLOOR_TOP_Y);
    drawingContext.lineTo(bottomX, GLASS_FLOOR_BOTTOM_Y);
    drawingContext.stroke();
  }
  drawingContext.restore();

  /* 3. Floating crystal floor caustic reflection beneath the hero glass cube */
  const reflectionScale = 1 - bobOffsetY * 0.025;
  drawingContext.save();
  drawingContext.translate(DISPLAY_CENTER_PX, CAUSTIC_CENTER_Y);
  drawingContext.scale(reflectionScale, reflectionScale);

  drawingContext.beginPath();
  drawingContext.ellipse(0, 0, 40, 8.5, 0, 0, FULL_CIRCLE_RADIANS);
  drawingContext.fillStyle = isDark ? "rgba(192, 254, 5, 0.07)" : "rgba(46, 52, 62, 0.07)";
  drawingContext.fill();
  drawingContext.strokeStyle = palette.signalHighlight;
  drawingContext.globalAlpha = chromeAlpha * 0.42;
  drawingContext.lineWidth = 1;
  drawingContext.stroke();

  drawingContext.beginPath();
  drawingContext.ellipse(0, 0, 21, 4.2, 0, 0, FULL_CIRCLE_RADIANS);
  drawingContext.fillStyle = isDark ? "rgba(245, 246, 248, 0.14)" : "rgba(255, 255, 255, 0.45)";
  drawingContext.globalAlpha = chromeAlpha * 0.7;
  drawingContext.fill();
  drawingContext.restore();

  /* 4. Top pull-down handle bar & minimal status beacon */
  drawingContext.globalAlpha = chromeAlpha * 0.65;
  drawingContext.fillStyle = palette.hairlineStrong;
  drawingContext.fillRect(
    DISPLAY_CENTER_PX - TOP_HANDLE_HALF_WIDTH_PX,
    TOP_HANDLE_Y,
    TOP_HANDLE_HALF_WIDTH_PX * 2,
    TOP_HANDLE_HEIGHT_PX,
  );

  drawingContext.globalAlpha = chromeAlpha;
  const volumePercent = Math.round(clamp(Number(volumeLevel ?? DEFAULT_VOLUME_RATIO), 0, 1) * PERCENT_SCALE);
  const statusText =
    playbackState === "storage"
      ? `USB MODE \u00b7 BAT ${DEFAULT_BATTERY_PERCENT}%`
      : playbackState === "playing"
        ? `PLAY \u00b7 VOL ${volumePercent} \u00b7 ${DEFAULT_BATTERY_PERCENT}%`
        : playbackState === "paused"
          ? `PAUSE \u00b7 VOL ${volumePercent} \u00b7 ${DEFAULT_BATTERY_PERCENT}%`
          : `IDLE \u00b7 VOL ${volumePercent} \u00b7 ${DEFAULT_BATTERY_PERCENT}%`;

  drawingContext.font = `600 9.5px ${FONT_MONO}`;
  const statusWidth = drawingContext.measureText(statusText).width;
  const beaconSize = 5;
  const beaconGap = 6;
  const originX = DISPLAY_CENTER_PX - (beaconSize + beaconGap + statusWidth) / 2;

  drawingContext.fillStyle = playbackState === "idle" ? palette.hairlineStrong : palette.signalHighlight;
  drawingContext.fillRect(originX, 46.5, beaconSize, beaconSize);

  drawingContext.fillStyle = palette.inkSecondary;
  drawingContext.textAlign = "left";
  drawingContext.textBaseline = "middle";
  drawingContext.fillText(statusText, originX + beaconSize + beaconGap, 49);

  drawingContext.restore();
}

/**
 * Project and paint a solid 3D box using pure 2D painter-sorted polygon faces.
 */
function drawProjected3dBox(
  drawingContext,
  boxCenterX,
  boxCenterY,
  boxCenterZ,
  halfWidth,
  halfHeight,
  halfDepth,
  originX,
  originY,
  cubeScale,
  angleX,
  angleY,
  angleZ,
  faceColors,
  edgeColor,
) {
  const transformedVertices = [];
  for (let vertexIndex = 0; vertexIndex < CUBE_LOCAL_VERTICES.length; vertexIndex += 1) {
    const unit = CUBE_LOCAL_VERTICES[vertexIndex];
    transformedVertices.push(
      projectLocalPoint(
        boxCenterX + unit[0] * halfWidth,
        boxCenterY + unit[1] * halfHeight,
        boxCenterZ + unit[2] * halfDepth,
        originX,
        originY,
        cubeScale,
        angleX,
        angleY,
        angleZ,
      ),
    );
  }

  const visibleFaces = [];
  for (let faceIndex = 0; faceIndex < CUBE_FACES.length; faceIndex += 1) {
    const normal = CUBE_FACES[faceIndex].normal;
    const rotatedNormal = rotatePoint3d(normal[0], normal[1], normal[2], angleX, angleY, angleZ);
    if (rotatedNormal.z <= 0.01) {
      continue;
    }
    const indices = CUBE_FACES[faceIndex].indices;
    let avgDepth = 0;
    for (let cornerIndex = 0; cornerIndex < indices.length; cornerIndex += 1) {
      avgDepth += transformedVertices[indices[cornerIndex]].depthZ;
    }
    visibleFaces.push({ indices, rotatedNormal, avgDepth: avgDepth / indices.length });
  }

  visibleFaces.sort((firstFace, secondFace) => firstFace.avgDepth - secondFace.avgDepth);

  for (let faceIndex = 0; faceIndex < visibleFaces.length; faceIndex += 1) {
    const face = visibleFaces[faceIndex];
    traceFacePolygon(drawingContext, transformedVertices, face.indices);
    drawingContext.fillStyle = resolveFaceTone(face.rotatedNormal, faceColors);
    drawingContext.fill();
    if (edgeColor) {
      drawingContext.strokeStyle = edgeColor;
      drawingContext.lineWidth = 0.85;
      drawingContext.stroke();
    }
  }
}

/**
 * Render the living animated 3D sculptural icon suspended inside an app glass cube.
 */
function drawAliveCubeIcon(
  drawingContext,
  appId,
  centerX,
  centerY,
  halfSize,
  angleX,
  angleY,
  angleZ,
  focusWeight,
  playbackState,
  clockSeconds,
  vinylRotationRadians,
  appearance,
  palette,
  activeAlbum = null,
  coverCache = null,
) {
  const cubeScale = halfSize / CUBE_ICON_BASE_HALF_SIZE_PX;
  const highlightActive = focusWeight > 0.38;
  const primaryInk = highlightActive ? palette.inkPrimary : palette.inkSecondary;
  const accentColor = highlightActive ? palette.signalHighlight : palette.hairlineStrong;
  const primaryFaces = highlightActive
    ? palette.cubeInkFaces
    : {
        top: palette.inkSecondary,
        right: palette.hairlineStrong,
        left: palette.hairline,
      };
  const accentFaces = highlightActive ? palette.cubeHighlightFaces : primaryFaces;

  drawingContext.save();
  drawingContext.lineCap = "butt";
  drawingContext.lineJoin = "miter";

  if (appId === "now-playing") {
    if (playbackState === "playing" || playbackState === "paused") {
      const discRadius = halfSize * 0.64;
      const foreshortenX = clamp(Math.cos(angleY) * 0.92, 0.24, 1.0);
      const foreshortenY = clamp(Math.cos(angleX) * 0.96, 0.55, 1.0);
      const discCenterY = centerY - halfSize * 0.08;

      drawingContext.save();
      drawingContext.translate(
        centerX + Math.sin(angleY) * 3.5 * cubeScale,
        discCenterY + 2.2 * cubeScale,
      );
      drawingContext.scale(foreshortenX, foreshortenY);
      drawingContext.beginPath();
      drawingContext.arc(0, 0, discRadius, 0, FULL_CIRCLE_RADIANS);
      drawingContext.fillStyle = appearance === "dark" ? "#2A303C" : "#1B1E24";
      drawingContext.fill();
      drawingContext.restore();

      drawingContext.save();
      drawingContext.translate(centerX, discCenterY);
      drawingContext.rotate(angleZ * 0.6);
      drawingContext.scale(foreshortenX, foreshortenY);
      drawAlbumArtworkDisc(
        drawingContext,
        0,
        0,
        discRadius,
        vinylRotationRadians,
        palette,
        appearance,
        1.0,
        activeAlbum,
        coverCache,
      );

      drawingContext.strokeStyle = highlightActive ? palette.signalHighlight : palette.hairlineStrong;
      drawingContext.lineWidth = highlightActive ? 2.1 : 1.3;
      drawingContext.beginPath();
      drawingContext.arc(0, 0, discRadius + 1, 0, FULL_CIRCLE_RADIANS);
      drawingContext.stroke();
      drawingContext.restore();

      if (cubeScale > 0.45) {
        const barCount = 4;
        const barWidth = 4.2 * cubeScale;
        const barGap = 4.5 * cubeScale;
        const totalBarWidth = barCount * barWidth + (barCount - 1) * barGap;
        const baseLineY = centerY + halfSize * 0.72;
        for (let barIndex = 0; barIndex < barCount; barIndex += 1) {
          const waveLevel =
            playbackState === "playing"
              ? 0.28 +
                0.72 * (0.5 + 0.5 * Math.sin(clockSeconds * (4.6 + barIndex * 1.1) + barIndex * 1.4))
              : 0.28;
          const barHeight = (3.5 + waveLevel * 9.5) * cubeScale;
          const barX = centerX - totalBarWidth / 2 + barIndex * (barWidth + barGap);
          drawingContext.fillStyle = barIndex % 2 === 0 ? accentColor : primaryInk;
          drawingContext.fillRect(barX, baseLineY - barHeight, barWidth, barHeight);
        }
      }
    } else if (playbackState === "storage") {
      const hoverOffset = Math.sin(clockSeconds * 2.4) * 2.2;
      drawProjected3dBox(
        drawingContext,
        0,
        hoverOffset,
        0,
        15,
        18,
        7,
        centerX,
        centerY,
        cubeScale,
        angleX,
        angleY,
        angleZ,
        primaryFaces,
        palette.cubeEdgeStroke,
      );
      drawProjected3dBox(
        drawingContext,
        0,
        -21 + hoverOffset,
        0,
        8,
        4,
        4,
        centerX,
        centerY,
        cubeScale,
        angleX,
        angleY,
        angleZ,
        accentFaces,
        palette.cubeEdgeStroke,
      );
      for (let packetIndex = 0; packetIndex < 2; packetIndex += 1) {
        const packetPhase = (clockSeconds * 0.9 + packetIndex * 0.5) % 1;
        const packetY = lerp(11, -11, packetPhase) + hoverOffset;
        const packetLeft = projectLocalPoint(
          -8,
          packetY,
          8.5,
          centerX,
          centerY,
          cubeScale,
          angleX,
          angleY,
          angleZ,
        );
        const packetRight = projectLocalPoint(
          8,
          packetY,
          8.5,
          centerX,
          centerY,
          cubeScale,
          angleX,
          angleY,
          angleZ,
        );
        drawingContext.strokeStyle = accentColor;
        drawingContext.lineWidth = Math.max(1.8, 3 * cubeScale);
        drawingContext.beginPath();
        drawingContext.moveTo(packetLeft.screenX, packetLeft.screenY);
        drawingContext.lineTo(packetRight.screenX, packetRight.screenY);
        drawingContext.stroke();
      }
    } else {
      const foreshortenX = clamp(Math.cos(angleY) * 0.92, 0.28, 1.0);
      const foreshortenY = clamp(Math.cos(angleX) * 0.95, 0.55, 1.0);
      const idleSpin = clockSeconds * 0.35;
      const armSweep = Math.sin(clockSeconds * 1.6) * 0.08;

      drawingContext.save();
      drawingContext.translate(centerX - 3 * cubeScale, centerY + 1 * cubeScale);
      drawingContext.scale(foreshortenX, foreshortenY);

      drawingContext.strokeStyle = primaryInk;
      drawingContext.lineWidth = 2.6 * cubeScale;
      drawingContext.beginPath();
      drawingContext.arc(0, 0, 21 * cubeScale, idleSpin, idleSpin + Math.PI * 1.65);
      drawingContext.stroke();

      drawingContext.strokeStyle = palette.hairlineStrong;
      drawingContext.lineWidth = 1.2 * cubeScale;
      drawingContext.beginPath();
      drawingContext.arc(0, 0, 12 * cubeScale, 0, FULL_CIRCLE_RADIANS);
      drawingContext.stroke();

      drawingContext.fillStyle = primaryInk;
      drawingContext.beginPath();
      drawingContext.arc(0, 0, 4.2 * cubeScale, 0, FULL_CIRCLE_RADIANS);
      drawingContext.fill();

      drawingContext.save();
      drawingContext.translate(21 * cubeScale, -16 * cubeScale);
      drawingContext.rotate(armSweep);
      drawingContext.strokeStyle = primaryInk;
      drawingContext.lineWidth = 2.3 * cubeScale;
      drawingContext.beginPath();
      drawingContext.moveTo(0, 0);
      drawingContext.lineTo(0, 18 * cubeScale);
      drawingContext.lineTo(-8 * cubeScale, 26 * cubeScale);
      drawingContext.stroke();

      drawingContext.fillStyle = primaryInk;
      drawingContext.beginPath();
      drawingContext.arc(0, 0, 3.2 * cubeScale, 0, FULL_CIRCLE_RADIANS);
      drawingContext.fill();

      const stylusPulse = 0.55 + 0.45 * Math.sin(clockSeconds * 4.2);
      drawingContext.globalAlpha = stylusPulse;
      drawingContext.fillStyle = accentColor;
      drawingContext.fillRect(-11 * cubeScale, 23 * cubeScale, 6.5 * cubeScale, 6.5 * cubeScale);
      drawingContext.restore();

      drawingContext.restore();
    }
  } else if (appId === "library") {
    const discTiltX = angleX + 0.42;
    const spindleTop = projectLocalPoint(
      0,
      -24,
      0,
      centerX,
      centerY,
      cubeScale,
      discTiltX,
      angleY,
      angleZ,
    );
    const spindleBottom = projectLocalPoint(
      0,
      24,
      0,
      centerX,
      centerY,
      cubeScale,
      discTiltX,
      angleY,
      angleZ,
    );

    drawingContext.strokeStyle = palette.hairlineStrong;
    drawingContext.lineWidth = Math.max(1.2, 2.2 * cubeScale);
    drawingContext.beginPath();
    drawingContext.moveTo(spindleTop.screenX, spindleTop.screenY);
    drawingContext.lineTo(spindleBottom.screenX, spindleBottom.screenY);
    drawingContext.stroke();

    const discLayers = [
      { baseY: 14, radius: 22, spinSpeed: 0.75, phase: 0.0, isHero: false },
      { baseY: 0, radius: 24, spinSpeed: -1.15, phase: 2.1, isHero: true },
      { baseY: -14, radius: 20, spinSpeed: 0.95, phase: 4.2, isHero: false },
    ];

    for (let layerIndex = 0; layerIndex < discLayers.length; layerIndex += 1) {
      const layer = discLayers[layerIndex];
      const levitateY = layer.baseY + Math.sin(clockSeconds * 2.2 + layer.phase) * 2.6;
      const spinAngle = clockSeconds * layer.spinSpeed + layer.phase;
      const ringSegments = 24;

      drawingContext.beginPath();
      for (let segmentIndex = 0; segmentIndex <= ringSegments; segmentIndex += 1) {
        const theta = (segmentIndex / ringSegments) * FULL_CIRCLE_RADIANS;
        const projectedPoint = projectLocalPoint(
          Math.cos(theta) * layer.radius,
          levitateY,
          Math.sin(theta) * layer.radius,
          centerX,
          centerY,
          cubeScale,
          discTiltX,
          angleY,
          angleZ,
        );
        if (segmentIndex === 0) {
          drawingContext.moveTo(projectedPoint.screenX, projectedPoint.screenY);
        } else {
          drawingContext.lineTo(projectedPoint.screenX, projectedPoint.screenY);
        }
      }
      drawingContext.closePath();

      if (layer.isHero) {
        drawingContext.fillStyle = highlightActive ? palette.signalHighlight : palette.hairlineStrong;
        drawingContext.fill();
        drawingContext.strokeStyle = palette.cubeEdgeStroke;
        drawingContext.lineWidth = Math.max(0.9, 1.3 * cubeScale);
        drawingContext.stroke();
      } else {
        drawingContext.fillStyle = appearance === "dark" ? "#252B36" : "#424854";
        drawingContext.fill();
        drawingContext.strokeStyle = primaryInk;
        drawingContext.lineWidth = Math.max(1.0, 1.6 * cubeScale);
        drawingContext.stroke();
      }

      const notchInner = projectLocalPoint(
        Math.cos(spinAngle) * (layer.radius * 0.35),
        levitateY,
        Math.sin(spinAngle) * (layer.radius * 0.35),
        centerX,
        centerY,
        cubeScale,
        discTiltX,
        angleY,
        angleZ,
      );
      const notchOuter = projectLocalPoint(
        Math.cos(spinAngle) * (layer.radius * 0.86),
        levitateY,
        Math.sin(spinAngle) * (layer.radius * 0.86),
        centerX,
        centerY,
        cubeScale,
        discTiltX,
        angleY,
        angleZ,
      );
      drawingContext.strokeStyle = layer.isHero ? palette.signalHighlightInk : accentColor;
      drawingContext.lineWidth = Math.max(1.4, 2.5 * cubeScale);
      drawingContext.beginPath();
      drawingContext.moveTo(notchInner.screenX, notchInner.screenY);
      drawingContext.lineTo(notchOuter.screenX, notchOuter.screenY);
      drawingContext.stroke();

      const hubCenter = projectLocalPoint(
        0,
        levitateY,
        0,
        centerX,
        centerY,
        cubeScale,
        discTiltX,
        angleY,
        angleZ,
      );
      drawingContext.fillStyle = palette.canvas;
      drawingContext.beginPath();
      drawingContext.ellipse(
        hubCenter.screenX,
        hubCenter.screenY,
        4.2 * cubeScale,
        2.2 * cubeScale,
        0,
        0,
        FULL_CIRCLE_RADIANS,
      );
      drawingContext.fill();
    }
  } else if (appId === "modes") {
    const nodeSpan = 14;
    const drawBusLine = (startPoint, endPoint) => {
      const startProjected = projectLocalPoint(
        startPoint[0],
        startPoint[1],
        startPoint[2],
        centerX,
        centerY,
        cubeScale,
        angleX,
        angleY,
        angleZ,
      );
      const endProjected = projectLocalPoint(
        endPoint[0],
        endPoint[1],
        endPoint[2],
        centerX,
        centerY,
        cubeScale,
        angleX,
        angleY,
        angleZ,
      );
      drawingContext.beginPath();
      drawingContext.moveTo(startProjected.screenX, startProjected.screenY);
      drawingContext.lineTo(endProjected.screenX, endProjected.screenY);
      drawingContext.stroke();
    };

    drawingContext.strokeStyle = palette.hairlineStrong;
    drawingContext.lineWidth = Math.max(1.3, 2.2 * cubeScale);
    drawBusLine([-nodeSpan, -nodeSpan, 0], [nodeSpan, -nodeSpan, 0]);
    drawBusLine([nodeSpan, -nodeSpan, 0], [nodeSpan, nodeSpan, 0]);
    drawBusLine([-nodeSpan, nodeSpan, 0], [-nodeSpan, 0, 0]);
    drawBusLine([-nodeSpan, 0, 0], [0, 0, 0]);
    drawBusLine([0, 0, 0], [0, -nodeSpan, 0]);

    const routeLoop = [
      [-nodeSpan, nodeSpan, 0],
      [-nodeSpan, 0, 0],
      [0, 0, 0],
      [0, -nodeSpan, 0],
      [nodeSpan, -nodeSpan, 0],
      [nodeSpan, nodeSpan, 0],
    ];
    const segmentCount = routeLoop.length - 1;
    const routeCursor = (clockSeconds * 1.45) % segmentCount;
    const activeSegment = Math.floor(routeCursor);
    const segmentFraction = routeCursor - activeSegment;
    const pulseX = lerp(routeLoop[activeSegment][0], routeLoop[activeSegment + 1][0], segmentFraction);
    const pulseY = lerp(routeLoop[activeSegment][1], routeLoop[activeSegment + 1][1], segmentFraction);
    const activeCornerIndex = Math.floor((clockSeconds * 0.9) % 4);

    const cornerNodes = [
      { offsetX: -nodeSpan, offsetY: -nodeSpan },
      { offsetX: nodeSpan, offsetY: -nodeSpan },
      { offsetX: nodeSpan, offsetY: nodeSpan },
      { offsetX: -nodeSpan, offsetY: nodeSpan },
    ];
    for (let nodeIndex = 0; nodeIndex < cornerNodes.length; nodeIndex += 1) {
      const node = cornerNodes[nodeIndex];
      const isHot = nodeIndex === activeCornerIndex;
      const nodeHalf = isHot ? 6.4 : 5.2;
      drawProjected3dBox(
        drawingContext,
        node.offsetX,
        node.offsetY,
        isHot ? 3 : 0,
        nodeHalf,
        nodeHalf,
        isHot ? 5 : 3.5,
        centerX,
        centerY,
        cubeScale,
        angleX,
        angleY,
        angleZ,
        isHot ? accentFaces : primaryFaces,
        palette.cubeEdgeStroke,
      );
    }

    const pulseProjected = projectLocalPoint(
      pulseX,
      pulseY,
      3,
      centerX,
      centerY,
      cubeScale,
      angleX,
      angleY,
      angleZ,
    );
    drawingContext.fillStyle = accentColor;
    drawingContext.fillRect(
      pulseProjected.screenX - 3.2 * cubeScale,
      pulseProjected.screenY - 3.2 * cubeScale,
      6.4 * cubeScale,
      6.4 * cubeScale,
    );
  } else if (appId === "settings") {
    const faderTracks = [
      { trackY: -14, centerOffset: -4, amplitude: 9, speed: 1.35, phase: 0.2, isAccent: false },
      { trackY: 0, centerOffset: 2, amplitude: 11, speed: 1.05, phase: 2.1, isAccent: true },
      { trackY: 14, centerOffset: -2, amplitude: 8, speed: 1.55, phase: 4.0, isAccent: false },
    ];

    for (let trackIndex = 0; trackIndex < faderTracks.length; trackIndex += 1) {
      const track = faderTracks[trackIndex];
      const railStart = projectLocalPoint(
        -19,
        track.trackY,
        -2,
        centerX,
        centerY,
        cubeScale,
        angleX,
        angleY,
        angleZ,
      );
      const railEnd = projectLocalPoint(
        19,
        track.trackY,
        -2,
        centerX,
        centerY,
        cubeScale,
        angleX,
        angleY,
        angleZ,
      );

      drawingContext.strokeStyle = palette.hairlineStrong;
      drawingContext.lineWidth = Math.max(1.6, 2.6 * cubeScale);
      drawingContext.beginPath();
      drawingContext.moveTo(railStart.screenX, railStart.screenY);
      drawingContext.lineTo(railEnd.screenX, railEnd.screenY);
      drawingContext.stroke();

      const liveCapX =
        track.centerOffset + Math.sin(clockSeconds * track.speed + track.phase) * track.amplitude;
      drawProjected3dBox(
        drawingContext,
        liveCapX,
        track.trackY,
        2.5,
        5.4,
        4.6,
        4.2,
        centerX,
        centerY,
        cubeScale,
        angleX,
        angleY,
        angleZ,
        track.isAccent ? accentFaces : primaryFaces,
        palette.cubeEdgeStroke,
      );
    }
  }

  drawingContext.restore();
}

/**
 * Draw a 4-layer 3D optical-glass cube enclosing a living app icon.
 */
function drawGlassAppCube(drawingContext, options) {
  const {
    appId,
    centerX,
    centerY,
    halfSize,
    angleX,
    angleY,
    angleZ,
    focusWeight,
    alpha,
    shellAlpha,
    playbackState,
    clockSeconds,
    vinylRotationRadians,
    appearance,
    palette,
    activeAlbum = null,
    coverCache = null,
  } = options;

  if (alpha <= ALPHA_VISIBILITY_THRESHOLD || halfSize <= 3) {
    return;
  }

  const isDark = appearance === "dark";
  const outerVertices = [];
  const innerVertices = [];

  for (let vertexIndex = 0; vertexIndex < CUBE_LOCAL_VERTICES.length; vertexIndex += 1) {
    const unit = CUBE_LOCAL_VERTICES[vertexIndex];
    const rotatedOuter = rotatePoint3d(
      unit[0] * halfSize,
      unit[1] * halfSize,
      unit[2] * halfSize,
      angleX,
      angleY,
      angleZ,
    );
    outerVertices.push(
      projectPoint3d(
        rotatedOuter.x,
        rotatedOuter.y,
        rotatedOuter.z,
        centerX,
        centerY,
        HOME_CONSTANTS.PERSPECTIVE_FOCAL_PX,
      ),
    );

    const rotatedInner = rotatePoint3d(
      unit[0] * halfSize * CUBE_INNER_WALL_RATIO,
      unit[1] * halfSize * CUBE_INNER_WALL_RATIO,
      unit[2] * halfSize * CUBE_INNER_WALL_RATIO,
      angleX,
      angleY,
      angleZ,
    );
    innerVertices.push(
      projectPoint3d(
        rotatedInner.x,
        rotatedInner.y,
        rotatedInner.z,
        centerX,
        centerY,
        HOME_CONSTANTS.PERSPECTIVE_FOCAL_PX,
      ),
    );
  }

  const backFaces = [];
  const frontFaces = [];

  for (let faceIndex = 0; faceIndex < CUBE_FACES.length; faceIndex += 1) {
    const faceSpec = CUBE_FACES[faceIndex];
    const rotatedNormal = rotatePoint3d(
      faceSpec.normal[0],
      faceSpec.normal[1],
      faceSpec.normal[2],
      angleX,
      angleY,
      angleZ,
    );
    let depthSum = 0;
    for (let cornerIndex = 0; cornerIndex < faceSpec.indices.length; cornerIndex += 1) {
      depthSum += outerVertices[faceSpec.indices[cornerIndex]].depthZ;
    }
    const faceEntry = {
      indices: faceSpec.indices,
      rotatedNormal,
      avgDepth: depthSum / faceSpec.indices.length,
    };
    if (rotatedNormal.z > 0) {
      frontFaces.push(faceEntry);
    } else {
      backFaces.push(faceEntry);
    }
  }

  backFaces.sort((firstFace, secondFace) => firstFace.avgDepth - secondFace.avgDepth);
  frontFaces.sort((firstFace, secondFace) => firstFace.avgDepth - secondFace.avgDepth);

  drawingContext.save();
  drawingContext.globalAlpha = alpha;

  /* Layer 1 & 2: Back translucent glass chamber faces + 0.85x inset wall-thickness wireframe */
  if (shellAlpha > 0.01) {
    drawingContext.save();
    drawingContext.globalAlpha = alpha * shellAlpha;
    for (let faceIndex = 0; faceIndex < backFaces.length; faceIndex += 1) {
      const face = backFaces[faceIndex];
      traceFacePolygon(drawingContext, outerVertices, face.indices);
      const shadeFactor = 0.55 + 0.45 * Math.abs(face.rotatedNormal.y);
      drawingContext.fillStyle = isDark
        ? `rgba(176, 206, 248, ${(0.045 + 0.045 * shadeFactor).toFixed(3)})`
        : `rgba(46, 52, 62, ${(0.06 + 0.055 * shadeFactor).toFixed(3)})`;
      drawingContext.fill();
      drawingContext.strokeStyle = isDark ? "rgba(195, 220, 255, 0.16)" : "rgba(46, 52, 62, 0.16)";
      drawingContext.lineWidth = 0.9;
      drawingContext.stroke();
    }

    drawingContext.strokeStyle = isDark ? "rgba(218, 234, 255, 0.16)" : "rgba(255, 255, 255, 0.48)";
    drawingContext.lineWidth = 0.75;
    for (let edgeIndex = 0; edgeIndex < HOME_CUBE_EDGES.length; edgeIndex += 1) {
      const edge = HOME_CUBE_EDGES[edgeIndex];
      const startPoint = innerVertices[edge[0]];
      const endPoint = innerVertices[edge[1]];
      drawingContext.beginPath();
      drawingContext.moveTo(startPoint.screenX, startPoint.screenY);
      drawingContext.lineTo(endPoint.screenX, endPoint.screenY);
      drawingContext.stroke();
    }
    drawingContext.restore();
  }

  /* Layer 3: Living animated 3D core suspended inside the crystal volume */
  drawAliveCubeIcon(
    drawingContext,
    appId,
    centerX,
    centerY,
    halfSize,
    angleX,
    angleY,
    angleZ,
    focusWeight,
    playbackState,
    clockSeconds,
    vinylRotationRadians,
    appearance,
    palette,
    activeAlbum,
    coverCache,
  );

  /* Layer 4: Front translucent crystal facets, specular refraction glare & corner brackets */
  if (shellAlpha > 0.01) {
    drawingContext.save();
    drawingContext.globalAlpha = alpha * shellAlpha;
    for (let faceIndex = 0; faceIndex < frontFaces.length; faceIndex += 1) {
      const face = frontFaces[faceIndex];
      const facingIntensity = clamp(face.rotatedNormal.z, 0, 1);

      traceFacePolygon(drawingContext, outerVertices, face.indices);
      if (isDark) {
        const glassAlpha =
          lerp(0.055, 0.135, 1 - facingIntensity) * lerp(0.75, 1.0, focusWeight);
        drawingContext.fillStyle = `rgba(220, 236, 255, ${glassAlpha.toFixed(3)})`;
      } else {
        const glassAlpha =
          lerp(0.16, 0.36, 1 - facingIntensity) * lerp(0.75, 1.0, focusWeight);
        drawingContext.fillStyle = `rgba(255, 255, 255, ${glassAlpha.toFixed(3)})`;
      }
      drawingContext.fill();

      if (facingIntensity > 0.55) {
        drawingContext.save();
        traceFacePolygon(drawingContext, outerVertices, face.indices);
        drawingContext.clip();

        const glareShift = Math.sin(angleY) * halfSize * 1.15;
        drawingContext.fillStyle = isDark
          ? `rgba(255, 255, 255, ${(0.095 * focusWeight + 0.04).toFixed(3)})`
          : `rgba(255, 255, 255, ${(0.42 * focusWeight + 0.18).toFixed(3)})`;

        drawingContext.beginPath();
        drawingContext.moveTo(centerX - halfSize * 1.1 + glareShift, centerY - halfSize * 1.4);
        drawingContext.lineTo(centerX - halfSize * 0.45 + glareShift, centerY - halfSize * 1.4);
        drawingContext.lineTo(centerX - halfSize * 1.45 + glareShift, centerY + halfSize * 1.4);
        drawingContext.lineTo(centerX - halfSize * 2.1 + glareShift, centerY + halfSize * 1.4);
        drawingContext.closePath();
        drawingContext.fill();

        drawingContext.beginPath();
        drawingContext.moveTo(centerX - halfSize * 0.25 + glareShift, centerY - halfSize * 1.4);
        drawingContext.lineTo(centerX - halfSize * 0.08 + glareShift, centerY - halfSize * 1.4);
        drawingContext.lineTo(centerX - halfSize * 1.08 + glareShift, centerY + halfSize * 1.4);
        drawingContext.lineTo(centerX - halfSize * 1.25 + glareShift, centerY + halfSize * 1.4);
        drawingContext.closePath();
        drawingContext.fill();
        drawingContext.restore();
      }

      traceFacePolygon(drawingContext, outerVertices, face.indices);
      drawingContext.strokeStyle = isDark
        ? focusWeight > 0.5
          ? "rgba(245, 250, 255, 0.78)"
          : "rgba(200, 216, 240, 0.34)"
        : focusWeight > 0.5
          ? "rgba(46, 50, 56, 0.62)"
          : "rgba(46, 50, 56, 0.28)";
      drawingContext.lineWidth = focusWeight > 0.5 ? 1.55 : 1.05;
      drawingContext.stroke();
    }

    if (focusWeight > 0.45 && frontFaces.length > 0) {
      const frontMostFace = frontFaces[frontFaces.length - 1];
      const firstCorner = outerVertices[frontMostFace.indices[0]];
      const secondCorner = outerVertices[frontMostFace.indices[1]];
      const thirdCorner = outerVertices[frontMostFace.indices[2]];
      const fourthCorner = outerVertices[frontMostFace.indices[3]];
      const bracketFraction = 0.24;

      drawingContext.strokeStyle = palette.signalHighlight;
      drawingContext.lineWidth = 2.3;
      drawingContext.globalAlpha = alpha * shellAlpha * focusWeight;

      drawingContext.beginPath();
      drawingContext.moveTo(
        lerp(firstCorner.screenX, fourthCorner.screenX, bracketFraction),
        lerp(firstCorner.screenY, fourthCorner.screenY, bracketFraction),
      );
      drawingContext.lineTo(firstCorner.screenX, firstCorner.screenY);
      drawingContext.lineTo(
        lerp(firstCorner.screenX, secondCorner.screenX, bracketFraction),
        lerp(firstCorner.screenY, secondCorner.screenY, bracketFraction),
      );
      drawingContext.stroke();

      drawingContext.beginPath();
      drawingContext.moveTo(
        lerp(thirdCorner.screenX, secondCorner.screenX, bracketFraction),
        lerp(thirdCorner.screenY, secondCorner.screenY, bracketFraction),
      );
      drawingContext.lineTo(thirdCorner.screenX, thirdCorner.screenY);
      drawingContext.lineTo(
        lerp(thirdCorner.screenX, fourthCorner.screenX, bracketFraction),
        lerp(thirdCorner.screenY, fourthCorner.screenY, bracketFraction),
      );
      drawingContext.stroke();
    }

    drawingContext.restore();
  }

  drawingContext.restore();
}

/**
 * Render the centered Home app title, mono telemetry subtitle, and cyclic page bars.
 */
function drawHomeBottomChord(
  drawingContext,
  scrollPosition,
  playbackState,
  entryProgress,
  launchProgress,
  resolvedContext,
) {
  const entryAlpha = easeOutCubic(normalizeRange(entryProgress, 0.25, 0.92));
  const exitProgress = easeInOutCubic(normalizeRange(launchProgress, 0.0, 0.42));
  const chordAlpha = entryAlpha * (1 - exitProgress);

  if (chordAlpha <= ALPHA_VISIBILITY_THRESHOLD) {
    return;
  }

  const { appearance, palette, counts, trackTitle, trackElapsedSeconds, trackDurationSeconds } =
    resolvedContext;
  const nearestSlot = Math.round(scrollPosition);
  const nearestAppIndex = wrapAppIndex(nearestSlot);
  const offsetFromNearest = scrollPosition - nearestSlot;
  const crossfadeAlpha = clamp(1 - Math.abs(offsetFromNearest) * 1.75, 0, 1);
  const activeApp = HOME_APPS[nearestAppIndex];
  const subtitleText = resolveHomeAppSubtitle(activeApp, {
    playbackState,
    appearance,
    counts,
    trackTitle,
    trackElapsedSeconds,
    trackDurationSeconds,
  });

  const verticalDriftY =
    lerp(14, 0, easeOutExpo(normalizeRange(entryProgress, 0.20, 0.92))) +
    lerp(0, 24, exitProgress);
  const horizontalShiftX = -offsetFromNearest * 26;

  drawingContext.save();
  drawingContext.translate(0, verticalDriftY);

  drawingContext.save();
  drawingContext.globalAlpha = chordAlpha * crossfadeAlpha;
  drawingContext.translate(horizontalShiftX, 0);

  drawingContext.font = `700 22px ${FONT_DISPLAY}`;
  drawingContext.fillStyle = palette.inkPrimary;
  drawingContext.textAlign = "center";
  drawingContext.textBaseline = "middle";
  drawingContext.fillText(activeApp.title, DISPLAY_CENTER_PX, 248);

  drawingContext.font = `600 9.5px ${FONT_MONO}`;
  drawingContext.fillStyle = palette.inkSecondary;
  drawMarqueeCanvasText(
    drawingContext,
    subtitleText,
    DISPLAY_CENTER_PX,
    272,
    232,
    resolvedContext.animationTimeSeconds ?? 0,
  );
  drawingContext.restore();

  drawingContext.save();
  drawingContext.globalAlpha = chordAlpha;
  const pipHeight = 5;
  const pipGap = 7;
  const pipSpecs = [];
  let totalStripWidth = 0;

  for (let appIndex = 0; appIndex < HOME_APPS.length; appIndex += 1) {
    const pipFocus = cyclicFocusWeight(appIndex, scrollPosition);
    const pipWidth = lerp(6, 20, easeInOutCubic(pipFocus));
    pipSpecs.push({ width: pipWidth, focus: pipFocus });
    totalStripWidth += pipWidth;
    if (appIndex < HOME_APPS.length - 1) {
      totalStripWidth += pipGap;
    }
  }

  let cursorX = DISPLAY_CENTER_PX - totalStripWidth / 2;
  const pipY = 296;
  for (let appIndex = 0; appIndex < pipSpecs.length; appIndex += 1) {
    const pipSpec = pipSpecs[appIndex];
    drawingContext.fillStyle =
      pipSpec.focus > 0.5 ? palette.signalHighlight : palette.hairlineStrong;
    drawingContext.globalAlpha = chordAlpha * lerp(0.55, 1.0, pipSpec.focus);
    drawingContext.fillRect(cursorX, pipY, pipSpec.width, pipHeight);
    cursorX += pipSpec.width + pipGap;
  }
  drawingContext.restore();

  drawingContext.restore();
}

/**
 * Render the launched Now Playing spindle progress ring, volume arc, and handle chrome.
 */
function drawNowPlayingExpandedChrome(
  drawingContext,
  launchProgress,
  trackElapsedSeconds,
  trackDurationSeconds,
  volumeLevel,
  palette,
) {
  const waveOne = easeOutExpo(normalizeRange(launchProgress, 0.52, 0.86));
  if (waveOne > 0.01) {
    const spindleRadius = lerp(12, 27, waveOne);
    const activeDuration = Math.max(1, trackDurationSeconds);
    const trackRatio = clamp((trackElapsedSeconds % activeDuration) / activeDuration, 0, 1);

    drawingContext.save();
    drawingContext.globalAlpha = waveOne;

    drawingContext.beginPath();
    drawingContext.arc(DISPLAY_CENTER_PX, DISPLAY_CENTER_PX, spindleRadius, 0, FULL_CIRCLE_RADIANS);
    drawingContext.fillStyle = "rgba(6, 7, 9, 0.88)";
    drawingContext.fill();

    drawingContext.beginPath();
    drawingContext.arc(
      DISPLAY_CENTER_PX,
      DISPLAY_CENTER_PX,
      spindleRadius - 4,
      0,
      FULL_CIRCLE_RADIANS,
    );
    drawingContext.strokeStyle = "rgba(245, 246, 248, 0.22)";
    drawingContext.lineWidth = 3;
    drawingContext.stroke();

    drawingContext.beginPath();
    drawingContext.arc(
      DISPLAY_CENTER_PX,
      DISPLAY_CENTER_PX,
      spindleRadius - 4,
      TOP_ANGLE_RADIANS,
      TOP_ANGLE_RADIANS + trackRatio * FULL_CIRCLE_RADIANS,
    );
    drawingContext.strokeStyle = palette.signalHighlight;
    drawingContext.lineWidth = 3.2;
    drawingContext.lineCap = "butt";
    drawingContext.stroke();

    drawingContext.beginPath();
    drawingContext.arc(DISPLAY_CENTER_PX, DISPLAY_CENTER_PX, 5, 0, FULL_CIRCLE_RADIANS);
    drawingContext.fillStyle = "#F5F6F8";
    drawingContext.fill();
    drawingContext.restore();
  }

  const waveTwo = easeOutCubic(normalizeRange(launchProgress, 0.68, 1.0));
  if (waveTwo > 0.01) {
    drawingContext.save();
    drawingContext.globalAlpha = waveTwo;

    const volumeArcRadius = 166;
    const volumeStart = -0.46;
    const volumeEnd = 0.46;
    const clampedVolume = clamp(Number(volumeLevel ?? DEFAULT_VOLUME_RATIO), 0, 1);

    drawingContext.beginPath();
    drawingContext.arc(DISPLAY_CENTER_PX, DISPLAY_CENTER_PX, volumeArcRadius, volumeStart, volumeEnd);
    drawingContext.strokeStyle = "rgba(245, 246, 248, 0.26)";
    drawingContext.lineWidth = 4;
    drawingContext.lineCap = "butt";
    drawingContext.stroke();

    drawingContext.beginPath();
    drawingContext.arc(
      DISPLAY_CENTER_PX,
      DISPLAY_CENTER_PX,
      volumeArcRadius,
      lerp(volumeEnd, volumeStart, clampedVolume),
      volumeEnd,
    );
    drawingContext.strokeStyle = palette.signalHighlight;
    drawingContext.lineWidth = 4.2;
    drawingContext.stroke();

    drawingContext.fillStyle = "rgba(245, 246, 248, 0.72)";
    drawingContext.fillRect(
      DISPLAY_CENTER_PX - TOP_HANDLE_HALF_WIDTH_PX,
      TOP_HANDLE_Y,
      TOP_HANDLE_HALF_WIDTH_PX * 2,
      TOP_HANDLE_HEIGHT_PX,
    );

    drawingContext.strokeStyle = "rgba(245, 246, 248, 0.82)";
    drawingContext.lineWidth = 2;
    drawingContext.beginPath();
    drawingContext.moveTo(DISPLAY_CENTER_PX - 6, 332);
    drawingContext.lineTo(DISPLAY_CENTER_PX, 326);
    drawingContext.lineTo(DISPLAY_CENTER_PX + 6, 332);
    drawingContext.stroke();

    drawingContext.restore();
  }
}

function buildDestinationRows(appId, appearance) {
  if (appId === "modes") {
    return [
      "USB STORAGE \u00b7 READY",
      "USB DAC \u00b7 V1.X",
      "WIRELESS RX \u00b7 V1.X",
    ];
  }
  return [
    `DISPLAY \u00b7 ${appearance === "light" ? "LIGHT MODE" : "DARK MODE"}`,
    "AUDIO \u00b7 EQ FLAT",
    "SYSTEM \u00b7 V1.0 SIM",
  ];
}

/**
 * Render launched destination screen overlays for idle/storage Now Playing,
 * Modes, and Settings.
 */
function drawDestinationPreviewContent(
  drawingContext,
  focusedApp,
  playbackState,
  launchProgress,
  clockSeconds,
  resolvedContext,
) {
  if (focusedApp.id === "library") {
    return;
  }

  const contentReveal = easeOutCubic(normalizeRange(launchProgress, 0.36, 0.92));
  if (contentReveal <= 0.01) {
    return;
  }

  const { appearance, palette } = resolvedContext;
  const slideOffsetY = lerp(22, 0, easeOutExpo(normalizeRange(launchProgress, 0.36, 0.95)));

  drawingContext.save();
  drawingContext.globalAlpha = contentReveal;
  drawingContext.translate(0, slideOffsetY);

  if (focusedApp.id === "now-playing" && playbackState === "idle") {
    drawingContext.font = `700 22px ${FONT_DISPLAY}`;
    drawingContext.fillStyle = palette.inkPrimary;
    drawingContext.textAlign = "center";
    drawingContext.textBaseline = "middle";
    drawingContext.fillText("Not Playing", DISPLAY_CENTER_PX, 178);

    drawingContext.font = `600 10px ${FONT_MONO}`;
    drawingContext.fillStyle = palette.inkSecondary;
    drawingContext.fillText("NO TRACK QUEUED ON DISC", DISPLAY_CENTER_PX, 202);

    drawingContext.fillStyle = palette.signalHighlight;
    drawingContext.fillRect(
      DISPLAY_CENTER_PX - BROWSE_LIBRARY_CTA.halfWidth,
      BROWSE_LIBRARY_CTA.topY,
      BROWSE_LIBRARY_CTA.width,
      BROWSE_LIBRARY_CTA.height,
    );

    drawingContext.font = `800 11px ${FONT_MONO}`;
    drawingContext.fillStyle = palette.signalHighlightInk;
    drawingContext.fillText("BROWSE LIBRARY", DISPLAY_CENTER_PX, 254.5);

    drawingContext.font = `600 9.5px ${FONT_MONO}`;
    drawingContext.fillStyle = palette.inkSecondary;
    drawingContext.fillText("SWIPE FROM LEFT EDGE", DISPLAY_CENTER_PX, 294);
  } else if (focusedApp.id === "now-playing" && playbackState === "storage") {
    drawingContext.font = `700 21px ${FONT_DISPLAY}`;
    drawingContext.fillStyle = palette.inkPrimary;
    drawingContext.textAlign = "center";
    drawingContext.textBaseline = "middle";
    drawingContext.fillText("Storage Mode", DISPLAY_CENTER_PX, 168);

    drawingContext.font = `600 10px ${FONT_MONO}`;
    drawingContext.fillStyle = palette.inkSecondary;
    drawingContext.fillText("USB CONNECTED \u00b7 42.6 GB FREE", DISPLAY_CENTER_PX, 192);

    const barWidth = 132;
    const barX = DISPLAY_CENTER_PX - barWidth / 2;
    const sweepRatio = (clockSeconds * 0.45) % 1;
    drawingContext.fillStyle = palette.hairline;
    drawingContext.fillRect(barX, 212, barWidth, 4);
    drawingContext.fillStyle = palette.signalHighlight;
    drawingContext.fillRect(barX + sweepRatio * (barWidth - 36), 212, 36, 4);

    drawingContext.fillStyle = palette.surface;
    drawingContext.fillRect(
      DISPLAY_CENTER_PX - BACK_TO_HOME_CTA.halfWidth,
      BACK_TO_HOME_CTA.topY,
      BACK_TO_HOME_CTA.width,
      BACK_TO_HOME_CTA.height,
    );
    drawingContext.strokeStyle = palette.inkPrimary;
    drawingContext.lineWidth = 1.5;
    drawingContext.strokeRect(
      DISPLAY_CENTER_PX - BACK_TO_HOME_CTA.halfWidth,
      BACK_TO_HOME_CTA.topY,
      BACK_TO_HOME_CTA.width,
      BACK_TO_HOME_CTA.height,
    );

    drawingContext.font = `700 10.5px ${FONT_MONO}`;
    drawingContext.fillStyle = palette.inkPrimary;
    drawingContext.fillText("BACK TO HOME", DISPLAY_CENTER_PX, 257.5);
  } else {
    drawingContext.font = `700 20px ${FONT_DISPLAY}`;
    drawingContext.fillStyle = palette.inkPrimary;
    drawingContext.textAlign = "center";
    drawingContext.textBaseline = "middle";
    drawingContext.fillText(focusedApp.title, DISPLAY_CENTER_PX, 146);

    const previewRows = buildDestinationRows(focusedApp.id, appearance);
    const rowWidth = 178;
    const rowHeight = 24;
    for (let rowIndex = 0; rowIndex < previewRows.length; rowIndex += 1) {
      const rowY = 170 + rowIndex * 29;
      drawingContext.fillStyle = palette.surface;
      drawingContext.fillRect(DISPLAY_CENTER_PX - rowWidth / 2, rowY, rowWidth, rowHeight);
      drawingContext.strokeStyle = rowIndex === 0 ? palette.signalHighlight : palette.hairline;
      drawingContext.lineWidth = rowIndex === 0 ? 1.5 : 1;
      drawingContext.strokeRect(DISPLAY_CENTER_PX - rowWidth / 2, rowY, rowWidth, rowHeight);

      drawingContext.font = `600 9.5px ${FONT_MONO}`;
      drawingContext.fillStyle = rowIndex === 0 ? palette.inkPrimary : palette.inkSecondary;
      drawingContext.textAlign = "center";
      drawingContext.fillText(
        previewRows[rowIndex],
        DISPLAY_CENTER_PX,
        rowY + rowHeight / 2 + 0.5,
        rowWidth - 10,
      );
    }

    drawingContext.font = `600 9.5px ${FONT_MONO}`;
    drawingContext.fillStyle = palette.inkSecondary;
    drawingContext.textAlign = "center";
    drawingContext.fillText("SWIPE FROM LEFT EDGE", DISPLAY_CENTER_PX, 282);
  }

  drawingContext.restore();
}

/**
 * Render the DialOS 02 HOME screen from deterministic Home controller state and
 * live telemetry context.
 *
 * @param {CanvasRenderingContext2D} drawingContext Canvas 2D context.
 * @param {object} homeState Snapshot from createHomeController().getRenderSnapshot().
 * @param {object} [renderContext={}] Live theme, palette, library counts, and track context.
 */
export function renderHomeScreen(drawingContext, homeState, renderContext = {}) {
  if (!drawingContext || !homeState) {
    return;
  }

  const resolvedContext = resolveRenderContext(renderContext);
  const {
    appearance,
    palette,
    trackElapsedSeconds,
    trackDurationSeconds,
    activeAlbum,
    coverCache,
  } = resolvedContext;

  const entryProgress = clamp(Number(homeState.entryProgress ?? 1), 0, 1);
  const launchProgress = clamp(Number(homeState.launchProgress ?? 0), 0, 1);
  const scrollPosition = Number.isFinite(homeState.scrollPosition) ? homeState.scrollPosition : 0;
  const focusedSlot = Number.isFinite(homeState.focusedSlot)
    ? homeState.focusedSlot
    : Math.round(scrollPosition);
  const focusedApp = HOME_APPS[wrapAppIndex(focusedSlot)];
  const playbackState = homeState.playbackState || "idle";
  const volumeLevel = Number.isFinite(homeState.volumeLevel)
    ? homeState.volumeLevel
    : DEFAULT_VOLUME_RATIO;
  const clockSeconds = Number.isFinite(homeState.idleClockSeconds) ? homeState.idleClockSeconds : 0;
  resolvedContext.animationTimeSeconds = Number.isFinite(resolvedContext.animationTimeSeconds)
    ? resolvedContext.animationTimeSeconds
    : clockSeconds;
  const vinylRotationRadians =
    (Number.isFinite(homeState.vinylRotationRadians) ? homeState.vinylRotationRadians : 0) +
    (Number.isFinite(homeState.vinylNudgeRadians) ? homeState.vinylNudgeRadians : 0);

  const homeChromeAlpha =
    easeOutCubic(normalizeRange(entryProgress, 0.15, 0.85)) *
    (1 - easeInOutCubic(normalizeRange(launchProgress, 0.0, 0.45)));

  const centerSlot = Math.round(scrollPosition);
  const heroBobY = Math.sin(clockSeconds * 1.9 + centerSlot * 1.3) * 3.2;

  /* 1. Spatial optical glass stage */
  drawHomeGlassStage(
    drawingContext,
    scrollPosition,
    heroBobY,
    playbackState,
    volumeLevel,
    appearance,
    homeChromeAlpha,
    palette,
  );

  /* 2. Build 5 consecutive virtual slots sorted back-to-front by ascending focusWeight */
  const visibleSlots = [];
  for (let slotOffset = -2; slotOffset <= 2; slotOffset += 1) {
    const virtualSlot = centerSlot + slotOffset;
    visibleSlots.push(
      computeHomeGlassSlotLayout(
        virtualSlot,
        scrollPosition,
        entryProgress,
        launchProgress,
        focusedSlot,
        clockSeconds,
      ),
    );
  }
  visibleSlots.sort((firstSlot, secondSlot) => firstSlot.focusWeight - secondSlot.focusWeight);

  /* 3. Render floating translucent 3D glass cubes with living icons suspended inside */
  for (let slotIndex = 0; slotIndex < visibleSlots.length; slotIndex += 1) {
    const layout = visibleSlots[slotIndex];
    if (layout.alpha <= ALPHA_VISIBILITY_THRESHOLD) {
      continue;
    }

    const appSpec = HOME_APPS[layout.appIndex];
    const isFocusedTarget = layout.virtualSlot === focusedSlot;
    const isNowPlayingVinyl =
      appSpec.id === "now-playing" &&
      (playbackState === "playing" || playbackState === "paused");

    let cubeCenterX = layout.centerX;
    let cubeCenterY = layout.centerY;
    let cubeHalfSize = layout.halfSize;

    if (isFocusedTarget && launchProgress > 0) {
      const launchEase = easeInOutCubic(launchProgress);
      if (isNowPlayingVinyl) {
        const discCenterX = lerp(layout.centerX, DISPLAY_CENTER_PX, launchEase);
        const discCenterY = lerp(layout.centerY - layout.halfSize * 0.08, DISPLAY_CENTER_PX, launchEase);
        const discRadius = lerp(layout.halfSize * 0.64, DISPLAY_CENTER_PX, launchEase);
        const cubeDissolve = 1 - easeOutCubic(normalizeRange(launchProgress, 0.0, 0.36));

        if (cubeDissolve > 0.01) {
          drawGlassAppCube(drawingContext, {
            appId: appSpec.id,
            centerX: lerp(layout.centerX, DISPLAY_CENTER_PX, launchEase),
            centerY: lerp(layout.centerY, DISPLAY_CENTER_PX, launchEase),
            halfSize: lerp(layout.halfSize, layout.halfSize * 1.65, easeOutCubic(launchProgress)),
            angleX: layout.angleX * cubeDissolve,
            angleY: layout.angleY * cubeDissolve,
            angleZ: layout.angleZ * cubeDissolve,
            focusWeight: layout.focusWeight,
            alpha: cubeDissolve,
            shellAlpha: cubeDissolve,
            playbackState,
            clockSeconds,
            vinylRotationRadians,
            appearance,
            palette,
            activeAlbum,
            coverCache,
          });
        }

        drawAlbumArtworkDisc(
          drawingContext,
          discCenterX,
          discCenterY,
          discRadius,
          vinylRotationRadians,
          palette,
          appearance,
          easeOutCubic(normalizeRange(launchProgress, 0.06, 0.24)),
          activeAlbum,
          coverCache,
        );

        if (launchProgress > 0.14 && launchProgress < 0.96) {
          const outlineAlpha = (1 - normalizeRange(launchProgress, 0.55, 0.96)) * 0.65;
          drawingContext.save();
          drawingContext.globalAlpha = outlineAlpha;
          drawingContext.strokeStyle = palette.signalHighlight;
          drawingContext.lineWidth = 1.2;
          drawingContext.beginPath();
          drawingContext.arc(discCenterX, discCenterY, discRadius + 1.5, 0, FULL_CIRCLE_RADIANS);
          drawingContext.stroke();
          drawingContext.restore();
        }
        continue;
      }

      if (appSpec.id === "now-playing" && playbackState === "idle") {
        cubeCenterX = lerp(layout.centerX, DISPLAY_CENTER_PX, launchEase);
        cubeCenterY = lerp(layout.centerY, 108, launchEase);
        cubeHalfSize = lerp(layout.halfSize, 30, launchEase);
      } else if (appSpec.id === "now-playing" && playbackState === "storage") {
        cubeCenterX = lerp(layout.centerX, DISPLAY_CENTER_PX, launchEase);
        cubeCenterY = lerp(layout.centerY, 100, launchEase);
        cubeHalfSize = lerp(layout.halfSize, 29, launchEase);
      } else {
        cubeCenterX = lerp(layout.centerX, DISPLAY_CENTER_PX, launchEase);
        cubeCenterY = lerp(layout.centerY, 84, launchEase);
        cubeHalfSize = lerp(layout.halfSize, 25, launchEase);
      }
    }

    drawGlassAppCube(drawingContext, {
      appId: appSpec.id,
      centerX: cubeCenterX,
      centerY: cubeCenterY,
      halfSize: cubeHalfSize,
      angleX: layout.angleX,
      angleY: layout.angleY,
      angleZ: layout.angleZ,
      focusWeight: layout.focusWeight,
      alpha: layout.alpha,
      shellAlpha: 1.0,
      playbackState,
      clockSeconds,
      vinylRotationRadians,
      appearance,
      palette,
      activeAlbum,
      coverCache,
    });

    /* Boot -> Home morph: cross-dissolve Boot emblem cleanly into the settling crystal cube */
    if (isFocusedTarget && entryProgress < HOME_EMBLEM_MORPH_END_PROGRESS) {
      const emblemFade = 1 - easeOutCubic(normalizeRange(entryProgress, 0.0, HOME_EMBLEM_FADE_END_PROGRESS));
      drawDialOsEmblem(
        drawingContext,
        cubeCenterX,
        cubeCenterY,
        cubeHalfSize * HOME_EMBLEM_SCALE_RATIO,
        palette,
        emblemFade,
        1.0,
      );
    }
  }

  /* 4. Bottom pure title, mono telemetry subtitle & cyclic page bars */
  drawHomeBottomChord(
    drawingContext,
    scrollPosition,
    playbackState,
    entryProgress,
    launchProgress,
    resolvedContext,
  );

  /* 5. Destination screen overlays when launched */
  if (launchProgress > LAUNCH_OVERLAY_MIN_PROGRESS) {
    if (
      focusedApp.id === "now-playing" &&
      (playbackState === "playing" || playbackState === "paused")
    ) {
      drawNowPlayingExpandedChrome(
        drawingContext,
        launchProgress,
        trackElapsedSeconds,
        trackDurationSeconds,
        volumeLevel,
        palette,
      );
    } else {
      drawDestinationPreviewContent(
        drawingContext,
        focusedApp,
        playbackState,
        launchProgress,
        clockSeconds,
        resolvedContext,
      );
    }
  }

  /* 6. Left-edge swipe-back gesture arc feedback */
  const backSwipeProgress = Number.isFinite(homeState.backSwipeProgress)
    ? homeState.backSwipeProgress
    : 0;
  if (backSwipeProgress > BACK_SWIPE_ARC_MIN_PROGRESS) {
    const swipeAlpha = clamp(backSwipeProgress, 0, 1);
    drawingContext.save();
    drawingContext.globalAlpha = swipeAlpha * 0.9;
    drawingContext.strokeStyle = palette.signalHighlight;
    drawingContext.lineWidth = 4;
    drawingContext.lineCap = "butt";
    drawingContext.beginPath();
    drawingContext.arc(
      DISPLAY_CENTER_PX,
      DISPLAY_CENTER_PX,
      BACK_SWIPE_ARC_RADIUS_PX,
      Math.PI - BACK_SWIPE_ARC_SPAN_RADIANS * swipeAlpha,
      Math.PI + BACK_SWIPE_ARC_SPAN_RADIANS * swipeAlpha,
    );
    drawingContext.stroke();
    drawingContext.restore();
  }
}

/**
 * Determine whether display coordinates (pointX, pointY) hover over a clickable
 * Home target: either the launched Browse Library / Back to Home CTA button or
 * one of the 3 center-neighborhood glass cubes on the Home carousel.
 *
 * @param {number} pointX Display coordinate X in [0, 360].
 * @param {number} pointY Display coordinate Y in [0, 360].
 * @param {object} homeState Snapshot from createHomeController().getRenderSnapshot().
 * @returns {boolean} True when hovering an actionable Home target.
 */
export function hitTestHomeInteractive(pointX, pointY, homeState) {
  if (!homeState || !Number.isFinite(pointX) || !Number.isFinite(pointY)) {
    return false;
  }

  if (homeState.launchTarget === 1) {
    if ((homeState.launchProgress ?? 0) <= LAUNCH_OVERLAY_MIN_PROGRESS) {
      return false;
    }
    if (homeState.launchedAppId !== "now-playing") {
      return false;
    }
    const playbackState = homeState.playbackState || "idle";
    if (playbackState === "idle") {
      return (
        Math.abs(pointX - DISPLAY_CENTER_PX) <= BROWSE_LIBRARY_CTA.halfWidth &&
        pointY >= BROWSE_LIBRARY_CTA.topY &&
        pointY <= BROWSE_LIBRARY_CTA.topY + BROWSE_LIBRARY_CTA.height
      );
    }
    if (playbackState === "storage") {
      return (
        Math.abs(pointX - DISPLAY_CENTER_PX) <= BACK_TO_HOME_CTA.halfWidth &&
        pointY >= BACK_TO_HOME_CTA.topY &&
        pointY <= BACK_TO_HOME_CTA.topY + BACK_TO_HOME_CTA.height
      );
    }
    return false;
  }

  const scrollPosition = Number.isFinite(homeState.scrollPosition) ? homeState.scrollPosition : 0;
  const entryProgress = clamp(Number(homeState.entryProgress ?? 1), 0, 1);
  const launchProgress = clamp(Number(homeState.launchProgress ?? 0), 0, 1);
  const focusedSlot = Number.isFinite(homeState.focusedSlot)
    ? homeState.focusedSlot
    : Math.round(scrollPosition);
  const clockSeconds = Number.isFinite(homeState.idleClockSeconds) ? homeState.idleClockSeconds : 0;
  const centerSlot = Math.round(scrollPosition);

  for (let slotOffset = -1; slotOffset <= 1; slotOffset += 1) {
    const candidateSlot = centerSlot + slotOffset;
    const layout = computeHomeGlassSlotLayout(
      candidateSlot,
      scrollPosition,
      entryProgress,
      launchProgress,
      focusedSlot,
      clockSeconds,
    );
    if (layout.alpha <= ALPHA_VISIBILITY_THRESHOLD) {
      continue;
    }
    const hitDistance = Math.hypot(pointX - layout.centerX, pointY - layout.centerY);
    if (hitDistance <= layout.halfSize * CUBE_HIT_SCALE + CUBE_HIT_PADDING_PX) {
      return true;
    }
  }

  return false;
}
