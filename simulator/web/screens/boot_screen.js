/**
 * Pure Canvas 2D boot sequence renderer (Acts 01-04 and Engine Delay warning)
 * for the DialOS 360x360 round display simulator.
 */

import {
  BUFFER_ARC_RADIUS_PX,
  DISPLAY_CENTER_PX,
  FULL_CIRCLE_RADIANS,
  OUTER_TICK_RADIUS_PX,
  REGISTRATION_MARK_RADIUS_PX,
  TOP_ANGLE_RADIANS,
  clamp,
  easeInOutCubic,
  easeOutCubic,
  easeOutExpo,
  lerp,
  normalizeRange,
  rotatePoint3d,
} from "../core/geometry.js";
import {
  drawCrosshairMark,
  drawDiagonalMark,
  drawDialOsEmblem,
  drawHazardStripeBand,
  drawPerimeterTicks,
  drawSimulatorBadge,
} from "../core/primitives.js";
import { FONT_DISPLAY, FONT_MONO } from "../core/tokens.js";

const SIMULATOR_BADGE_CENTER_Y = 66;
const RIM_SEGMENT_COUNT = 48;
const MATRIX_GRID_SIZE = 5;
const MATRIX_CELL_SIZE_PX = 18;
const MATRIX_CELL_GAP_PX = 5;
const ALPHA_VISIBILITY_THRESHOLD = 0.005;
const BUFFER_EXIT_HOLD_RATIO = 0.43;

const RETRY_HIT_TOP_Y = 248;
const RETRY_HIT_BOTTOM_Y = 306;
const RETRY_HIT_HALF_WIDTH_PX = 90;

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

const LATTICE_SUBCUBE_OFFSETS = buildSortedLatticeOffsets();

/* 5x5 Wake Matrix pattern: 1 = accent square, 2 = structural disc cell */
const WAKE_MATRIX_PATTERN = Object.freeze([
  [0, 2, 2, 2, 0],
  [2, 0, 0, 1, 2],
  [2, 0, 1, 0, 2],
  [2, 0, 0, 0, 2],
  [0, 2, 2, 2, 0],
]);

/**
 * Build the 27 sub-cube offsets sorted diagonally from (-1, +1, -1) to (+1, -1, +1).
 *
 * @returns {Array<{ gridX: number, gridY: number, gridZ: number, diagonalLayer: number, fillOrder: number }>} Sorted sub-cubes.
 */
function buildSortedLatticeOffsets() {
  const offsets = [];
  for (let gridZ = -1; gridZ <= 1; gridZ += 1) {
    for (let gridY = -1; gridY <= 1; gridY += 1) {
      for (let gridX = -1; gridX <= 1; gridX += 1) {
        const diagonalLayer = gridX + 1 + (1 - gridY) + (gridZ + 1);
        offsets.push({ gridX, gridY, gridZ, diagonalLayer, fillOrder: 0 });
      }
    }
  }

  offsets.sort((firstCell, secondCell) => {
    if (firstCell.diagonalLayer !== secondCell.diagonalLayer) {
      return firstCell.diagonalLayer - secondCell.diagonalLayer;
    }
    if (firstCell.gridY !== secondCell.gridY) {
      return secondCell.gridY - firstCell.gridY;
    }
    if (firstCell.gridX !== secondCell.gridX) {
      return firstCell.gridX - secondCell.gridX;
    }
    return firstCell.gridZ - secondCell.gridZ;
  });

  for (let orderIndex = 0; orderIndex < offsets.length; orderIndex += 1) {
    offsets[orderIndex].fillOrder = orderIndex;
  }

  return offsets;
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

/**
 * Trace a chamfered rectangle path (45-degree corner cuts, zero curved radius).
 *
 * @param {CanvasRenderingContext2D} drawingContext Canvas 2D context.
 * @param {number} leftX Left X coordinate.
 * @param {number} topY Top Y coordinate.
 * @param {number} width Plate width.
 * @param {number} height Plate height.
 * @param {number} chamfer Corner cut length in pixels.
 */
function traceChamferPlate(drawingContext, leftX, topY, width, height, chamfer) {
  const cut = Math.min(chamfer, width * 0.4, height * 0.4);
  const rightX = leftX + width;
  const bottomY = topY + height;
  drawingContext.beginPath();
  drawingContext.moveTo(leftX + cut, topY);
  drawingContext.lineTo(rightX - cut, topY);
  drawingContext.lineTo(rightX, topY + cut);
  drawingContext.lineTo(rightX, bottomY - cut);
  drawingContext.lineTo(rightX - cut, bottomY);
  drawingContext.lineTo(leftX + cut, bottomY);
  drawingContext.lineTo(leftX, bottomY - cut);
  drawingContext.lineTo(leftX, topY + cut);
  drawingContext.closePath();
}

/**
 * Draw the rotating 3x3x3 axonometric voxel matrix filling corner-to-corner.
 *
 * @param {CanvasRenderingContext2D} drawingContext Canvas 2D context.
 * @param {number} centerX Cube center X.
 * @param {number} centerY Cube center Y.
 * @param {number} cubeScale Scale multiplier.
 * @param {number} fillProgress Fill progress in [0, 1].
 * @param {number} elapsedSeconds Monotonic boot elapsed seconds.
 * @param {object} palette Semantic Dark/Light appearance palette.
 * @param {number} alpha Layer opacity.
 */
export function drawRotatingMatrixCube(
  drawingContext,
  centerX,
  centerY,
  cubeScale,
  fillProgress,
  elapsedSeconds,
  palette,
  alpha,
) {
  if (alpha <= ALPHA_VISIBILITY_THRESHOLD || cubeScale <= 0.01) {
    return;
  }

  const angleY = elapsedSeconds * 1.15 + 0.65;
  const angleX = 0.56 + Math.sin(elapsedSeconds * 0.85) * 0.16;
  const angleZ = 0.10;
  const cellPitch = 28 * cubeScale;
  const halfCubeSize = 10.2 * cubeScale;
  const totalSubCubes = LATTICE_SUBCUBE_OFFSETS.length;
  const clampedFill = clamp(fillProgress, 0, 1);
  const fillCursor = clampedFill * totalSubCubes;
  const activeLayerThreshold = Math.min(6, Math.floor(clampedFill * 7));

  const sortedSubCubes = [];
  for (let cubeIndex = 0; cubeIndex < totalSubCubes; cubeIndex += 1) {
    const offset = LATTICE_SUBCUBE_OFFSETS[cubeIndex];
    const rotatedCenter = rotatePoint3d(
      offset.gridX * cellPitch,
      offset.gridY * cellPitch,
      offset.gridZ * cellPitch,
      angleX,
      angleY,
      angleZ,
    );

    const activation = clamp(fillCursor - offset.fillOrder, 0, 1);
    const isFilled = activation > 0;
    const isLeadingWave =
      isFilled &&
      (offset.diagonalLayer === activeLayerThreshold || offset.fillOrder >= fillCursor - 3.0);
    const cubeState = !isFilled ? "wire" : isLeadingWave ? "highlight" : "ink";
    const popScale = isFilled ? lerp(0.45, 1.0, easeOutExpo(activation)) : 1.0;

    sortedSubCubes.push({
      rotatedCenter,
      cubeState,
      popScale,
    });
  }

  sortedSubCubes.sort((firstCube, secondCube) => firstCube.rotatedCenter.z - secondCube.rotatedCenter.z);

  const rotatedFaceNormals = [];
  for (let faceIndex = 0; faceIndex < CUBE_FACES.length; faceIndex += 1) {
    const normal = CUBE_FACES[faceIndex].normal;
    rotatedFaceNormals.push(rotatePoint3d(normal[0], normal[1], normal[2], angleX, angleY, angleZ));
  }

  const rotatedUnitCorners = [];
  for (let vertexIndex = 0; vertexIndex < CUBE_LOCAL_VERTICES.length; vertexIndex += 1) {
    const vertex = CUBE_LOCAL_VERTICES[vertexIndex];
    rotatedUnitCorners.push(
      rotatePoint3d(
        vertex[0] * halfCubeSize,
        vertex[1] * halfCubeSize,
        vertex[2] * halfCubeSize,
        angleX,
        angleY,
        angleZ,
      ),
    );
  }

  drawingContext.save();
  drawingContext.globalAlpha = alpha;
  drawingContext.lineJoin = "miter";

  for (let sortedIndex = 0; sortedIndex < sortedSubCubes.length; sortedIndex += 1) {
    const subCube = sortedSubCubes[sortedIndex];
    const originX = centerX + subCube.rotatedCenter.x;
    const originY = centerY + subCube.rotatedCenter.y;
    const isLit = subCube.cubeState !== "wire";
    const scaleFactor = subCube.popScale;
    const facePalette =
      subCube.cubeState === "highlight" ? palette.cubeHighlightFaces : palette.cubeInkFaces;

    for (let faceIndex = 0; faceIndex < CUBE_FACES.length; faceIndex += 1) {
      const rotatedNormal = rotatedFaceNormals[faceIndex];
      if (rotatedNormal.z <= 0.01) {
        continue;
      }

      const vertexIndices = CUBE_FACES[faceIndex].indices;
      drawingContext.beginPath();
      for (let cornerIndex = 0; cornerIndex < vertexIndices.length; cornerIndex += 1) {
        const cornerOffset = rotatedUnitCorners[vertexIndices[cornerIndex]];
        const projectedX = originX + cornerOffset.x * scaleFactor;
        const projectedY = originY + cornerOffset.y * scaleFactor;
        if (cornerIndex === 0) {
          drawingContext.moveTo(projectedX, projectedY);
        } else {
          drawingContext.lineTo(projectedX, projectedY);
        }
      }
      drawingContext.closePath();

      if (isLit) {
        drawingContext.fillStyle = resolveFaceTone(rotatedNormal, facePalette);
        drawingContext.fill();
        drawingContext.strokeStyle = palette.cubeEdgeStroke;
        drawingContext.lineWidth = 1;
        drawingContext.stroke();
      } else {
        drawingContext.strokeStyle = palette.cubeWireframe;
        drawingContext.lineWidth = 0.85;
        drawingContext.stroke();
      }
    }
  }

  drawingContext.restore();
}

/**
 * Render Boot Act 01: Wake Matrix (horizontal slit, ticks, 5x5 matrix, simulator badge).
 *
 * @param {CanvasRenderingContext2D} drawingContext Canvas 2D context.
 * @param {number} actProgress Normalized progress in [0, 1].
 * @param {object} palette Semantic Dark/Light appearance palette.
 */
export function renderActWakeMatrix(drawingContext, actProgress, palette) {
  const wakeSlitProgress = normalizeRange(actProgress, 0.0, 0.16);
  if (wakeSlitProgress < 1) {
    const slitHalfWidth = lerp(8, 146, easeOutExpo(wakeSlitProgress));
    const slitAlpha = 1 - normalizeRange(actProgress, 0.10, 0.18);
    drawingContext.save();
    drawingContext.globalAlpha = slitAlpha;
    drawingContext.strokeStyle = palette.signalHighlight;
    drawingContext.lineWidth = 2;
    drawingContext.beginPath();
    drawingContext.moveTo(DISPLAY_CENTER_PX - slitHalfWidth, DISPLAY_CENTER_PX);
    drawingContext.lineTo(DISPLAY_CENTER_PX + slitHalfWidth, DISPLAY_CENTER_PX);
    drawingContext.stroke();
    drawingContext.restore();
  }

  const tickRevealProgress = easeOutCubic(normalizeRange(actProgress, 0.08, 0.72));
  drawPerimeterTicks(
    drawingContext,
    RIM_SEGMENT_COUNT,
    OUTER_TICK_RADIUS_PX - 6,
    OUTER_TICK_RADIUS_PX,
    tickRevealProgress,
    palette,
    0.95,
  );

  const cornerAngles = [-Math.PI * 0.75, -Math.PI * 0.25, Math.PI * 0.25, Math.PI * 0.75];
  for (let cornerIndex = 0; cornerIndex < cornerAngles.length; cornerIndex += 1) {
    const markProgress = normalizeRange(
      actProgress,
      0.10 + cornerIndex * 0.06,
      0.22 + cornerIndex * 0.06,
    );
    const angleRadians = cornerAngles[cornerIndex];
    const markX = DISPLAY_CENTER_PX + Math.cos(angleRadians) * REGISTRATION_MARK_RADIUS_PX;
    const markY = DISPLAY_CENTER_PX + Math.sin(angleRadians) * REGISTRATION_MARK_RADIUS_PX;
    drawDiagonalMark(
      drawingContext,
      markX,
      markY,
      4.5,
      1.5,
      markProgress < 0.75 ? palette.signalHighlight : palette.inkSecondary,
      easeOutCubic(markProgress),
    );
  }

  const badgeAlpha =
    easeOutCubic(normalizeRange(actProgress, 0.08, 0.24)) *
    (1 - normalizeRange(actProgress, 0.86, 1.0));
  drawSimulatorBadge(drawingContext, DISPLAY_CENTER_PX, SIMULATOR_BADGE_CENTER_Y, palette, badgeAlpha);

  const collapseProgress = easeInOutCubic(normalizeRange(actProgress, 0.80, 1.0));
  const matrixScale = lerp(1.0, 0.26, collapseProgress);
  const matrixAlpha = 1 - normalizeRange(actProgress, 0.88, 1.0);

  const totalGridSpan =
    MATRIX_GRID_SIZE * MATRIX_CELL_SIZE_PX + (MATRIX_GRID_SIZE - 1) * MATRIX_CELL_GAP_PX;
  const matrixTopOffset = -totalGridSpan / 2;

  drawingContext.save();
  drawingContext.translate(DISPLAY_CENTER_PX, DISPLAY_CENTER_PX - 6);
  drawingContext.scale(matrixScale, matrixScale);
  drawingContext.globalAlpha = matrixAlpha;

  const bracketHalfSpan = totalGridSpan / 2 + 12;
  const bracketArm = 10;
  const bracketAlpha = easeOutCubic(normalizeRange(actProgress, 0.12, 0.30));
  drawingContext.strokeStyle = palette.hairlineStrong;
  drawingContext.lineWidth = 1.5;
  drawingContext.globalAlpha = matrixAlpha * bracketAlpha;

  const bracketCorners = [
    [-1, -1],
    [1, -1],
    [1, 1],
    [-1, 1],
  ];
  for (let cornerIndex = 0; cornerIndex < bracketCorners.length; cornerIndex += 1) {
    const signX = bracketCorners[cornerIndex][0];
    const signY = bracketCorners[cornerIndex][1];
    const cornerX = signX * bracketHalfSpan;
    const cornerY = signY * bracketHalfSpan;
    drawingContext.beginPath();
    drawingContext.moveTo(cornerX - signX * bracketArm, cornerY);
    drawingContext.lineTo(cornerX, cornerY);
    drawingContext.lineTo(cornerX, cornerY - signY * bracketArm);
    drawingContext.stroke();
  }

  for (let rowIndex = 0; rowIndex < MATRIX_GRID_SIZE; rowIndex += 1) {
    for (let columnIndex = 0; columnIndex < MATRIX_GRID_SIZE; columnIndex += 1) {
      const cellX = matrixTopOffset + columnIndex * (MATRIX_CELL_SIZE_PX + MATRIX_CELL_GAP_PX);
      const cellY = matrixTopOffset + rowIndex * (MATRIX_CELL_SIZE_PX + MATRIX_CELL_GAP_PX);
      const diagonalOrder = (rowIndex + columnIndex * 1.15) / 9.0;
      const cellActivateTime = 0.16 + diagonalOrder * 0.44;
      const cellFlashDelta = actProgress - cellActivateTime;
      const cellRole = WAKE_MATRIX_PATTERN[rowIndex][columnIndex];

      drawingContext.globalAlpha = matrixAlpha * easeOutCubic(normalizeRange(actProgress, 0.12, 0.28));
      drawingContext.strokeStyle = palette.hairlineStrong;
      drawingContext.lineWidth = 1;
      drawingContext.strokeRect(
        cellX + 0.5,
        cellY + 0.5,
        MATRIX_CELL_SIZE_PX - 1,
        MATRIX_CELL_SIZE_PX - 1,
      );

      if (cellFlashDelta >= 0) {
        const isFreshFlash = cellFlashDelta < 0.08;
        if (isFreshFlash || cellRole === 1) {
          drawingContext.globalAlpha = matrixAlpha;
          drawingContext.fillStyle = palette.signalHighlight;
          drawingContext.fillRect(cellX, cellY, MATRIX_CELL_SIZE_PX, MATRIX_CELL_SIZE_PX);
        } else if (cellRole === 2) {
          drawingContext.globalAlpha = matrixAlpha * 0.92;
          drawingContext.fillStyle = palette.inkPrimary;
          drawingContext.fillRect(
            cellX + 2,
            cellY + 2,
            MATRIX_CELL_SIZE_PX - 4,
            MATRIX_CELL_SIZE_PX - 4,
          );
        } else {
          drawingContext.globalAlpha = matrixAlpha * 0.22;
          drawingContext.fillStyle = palette.hairlineStrong;
          drawingContext.fillRect(
            cellX + 6,
            cellY + 6,
            MATRIX_CELL_SIZE_PX - 12,
            MATRIX_CELL_SIZE_PX - 12,
          );
        }
      }
    }
  }
  drawingContext.restore();

  const captionAlpha =
    easeOutCubic(normalizeRange(actProgress, 0.18, 0.34)) *
    (1 - normalizeRange(actProgress, 0.84, 0.98));
  if (captionAlpha > 0.01) {
    drawingContext.save();
    drawingContext.globalAlpha = captionAlpha;
    drawingContext.font = `600 10px ${FONT_MONO}`;
    const captionText = "MEM // CHECK";
    const captionWidth = drawingContext.measureText(captionText).width;
    const cursorWidth = 6;
    const cursorGap = 6;
    const captionOriginX = DISPLAY_CENTER_PX - (captionWidth + cursorGap + cursorWidth) / 2;

    drawingContext.fillStyle = palette.inkSecondary;
    drawingContext.textAlign = "left";
    drawingContext.textBaseline = "middle";
    drawingContext.fillText(captionText, captionOriginX, 286);

    const cursorVisible = Math.floor(actProgress * 18) % 2 === 0;
    if (cursorVisible) {
      drawingContext.fillStyle = palette.signalHighlight;
      drawingContext.fillRect(captionOriginX + captionWidth + cursorGap, 281, cursorWidth, 10);
    }
    drawingContext.restore();
  }
}

/**
 * Render Boot Act 02: Buffer Sweep driven by live backend sweepProgress and statusLabel.
 *
 * @param {CanvasRenderingContext2D} drawingContext Canvas 2D context.
 * @param {number} actProgress Act 02 time progress in [0, 1].
 * @param {number} elapsedSeconds Monotonic boot elapsed seconds.
 * @param {number} sweepProgress Live backend sweep fill progress in [0, 1].
 * @param {string} statusLabel Live boot readout string.
 * @param {object} palette Semantic Dark/Light appearance palette.
 */
export function renderActBufferSweep(
  drawingContext,
  actProgress,
  elapsedSeconds,
  sweepProgress,
  bufferExitProgress,
  statusLabel,
  palette,
) {
  const clampedSweep = clamp(sweepProgress, 0, 1);
  const entryScale = lerp(0.58, 1.0, easeOutExpo(normalizeRange(actProgress, 0.0, 0.14)));
  /* Hold 100% solid briefly before fading out so all 27 sub-cubes and 48 rim segments complete cleanly. */
  const exitFade = easeInOutCubic(normalizeRange(bufferExitProgress, BUFFER_EXIT_HOLD_RATIO, 1.0));
  const exitAlpha = 1 - exitFade;
  const cubeScale = entryScale * lerp(1.0, 0.86, exitFade);
  const cubeAlpha = easeOutCubic(normalizeRange(actProgress, 0.0, 0.12)) * exitAlpha;
  const badgeAlpha = easeOutCubic(normalizeRange(actProgress, 0.0, 0.12)) * exitAlpha;

  if (exitAlpha <= ALPHA_VISIBILITY_THRESHOLD) {
    return;
  }

  const filledSegmentCount = Math.round(clampedSweep * RIM_SEGMENT_COUNT);
  const segmentPitchAngle = FULL_CIRCLE_RADIANS / RIM_SEGMENT_COUNT;
  const segmentArcSpan = segmentPitchAngle * 0.68;

  drawingContext.save();
  drawingContext.lineCap = "butt";

  for (let segmentIndex = 0; segmentIndex < RIM_SEGMENT_COUNT; segmentIndex += 1) {
    const startAngle = TOP_ANGLE_RADIANS + segmentIndex * segmentPitchAngle;
    const endAngle = startAngle + segmentArcSpan;
    const isFilled = segmentIndex < filledSegmentCount;
    const isLeading = segmentIndex === filledSegmentCount - 1;

    drawingContext.beginPath();
    drawingContext.arc(
      DISPLAY_CENTER_PX,
      DISPLAY_CENTER_PX,
      OUTER_TICK_RADIUS_PX - 3,
      startAngle,
      endAngle,
    );

    if (isFilled) {
      const isHighlightBand = segmentIndex % 2 === 0;
      drawingContext.strokeStyle = isHighlightBand ? palette.signalHighlight : palette.inkPrimary;
      drawingContext.lineWidth = isLeading ? 9 : 6.5;
      drawingContext.globalAlpha = exitAlpha;
    } else {
      drawingContext.strokeStyle = palette.hairline;
      drawingContext.lineWidth = 2.5;
      drawingContext.globalAlpha = 0.85 * exitAlpha;
    }
    drawingContext.stroke();
  }
  drawingContext.restore();

  const cornerAngles = [-Math.PI * 0.75, -Math.PI * 0.25, Math.PI * 0.25, Math.PI * 0.75];
  const cornerDetentThresholds = [0.85, 0.15, 0.40, 0.65];
  for (let cornerIndex = 0; cornerIndex < cornerAngles.length; cornerIndex += 1) {
    const angleRadians = cornerAngles[cornerIndex];
    const markX = DISPLAY_CENTER_PX + Math.cos(angleRadians) * REGISTRATION_MARK_RADIUS_PX;
    const markY = DISPLAY_CENTER_PX + Math.sin(angleRadians) * REGISTRATION_MARK_RADIUS_PX;
    const isReached = clampedSweep >= cornerDetentThresholds[cornerIndex];
    drawDiagonalMark(
      drawingContext,
      markX,
      markY,
      4.5,
      isReached ? 1.8 : 1.3,
      isReached ? palette.signalHighlight : palette.hairlineStrong,
      (isReached ? 1.0 : 0.7) * exitAlpha,
    );
  }

  drawSimulatorBadge(drawingContext, DISPLAY_CENTER_PX, SIMULATOR_BADGE_CENTER_Y, palette, badgeAlpha);

  drawRotatingMatrixCube(
    drawingContext,
    DISPLAY_CENTER_PX,
    164,
    cubeScale,
    clampedSweep,
    elapsedSeconds,
    palette,
    cubeAlpha,
  );

  const readoutText = typeof statusLabel === "string" && statusLabel.length > 0 ? statusLabel : "INIT // SIMULATOR";
  const statusAlpha = easeOutCubic(normalizeRange(actProgress, 0.05, 0.16)) * exitAlpha;
  if (statusAlpha > 0.01) {
    drawingContext.save();
    drawingContext.globalAlpha = statusAlpha;
    drawingContext.font = `600 10px ${FONT_MONO}`;
    const statusWidth = drawingContext.measureText(readoutText).width;
    const cursorWidth = 6;
    const cursorGap = 6;
    const statusOriginX = DISPLAY_CENTER_PX - (statusWidth + cursorGap + cursorWidth) / 2;

    drawingContext.fillStyle = palette.inkPrimary;
    drawingContext.textAlign = "left";
    drawingContext.textBaseline = "middle";
    drawingContext.fillText(readoutText, statusOriginX, 286);

    const cursorVisible = Math.floor(elapsedSeconds * 6) % 2 === 0;
    if (cursorVisible) {
      drawingContext.fillStyle = palette.signalHighlight;
      drawingContext.fillRect(statusOriginX + statusWidth + cursorGap, 281, cursorWidth, 10);
    }
    drawingContext.restore();
  }
}

/**
 * Render Boot Acts 03 & 04: Signal Lock and Home Handoff.
 *
 * @param {CanvasRenderingContext2D} drawingContext Canvas 2D context.
 * @param {number} actProgress Act 03 progress in [0, 1].
 * @param {number} handoffProgress Act 04 handoff progress in [0, 1].
 * @param {string} summaryLine Live library summary chord string.
 * @param {object} palette Semantic Dark/Light appearance palette.
 */
export function renderActSignalLock(
  drawingContext,
  actProgress,
  handoffProgress,
  summaryLine,
  palette,
) {
  const lockSettle = easeOutExpo(normalizeRange(actProgress, 0.08, 0.55));
  const handoffEase = easeInOutCubic(handoffProgress);

  const rimRadius = lerp(OUTER_TICK_RADIUS_PX, 198, handoffEase);
  const rimAlpha = (1 - handoffEase) * easeOutCubic(normalizeRange(actProgress, 0.0, 0.25));

  if (rimAlpha > 0.01) {
    drawingContext.save();
    drawingContext.globalAlpha = rimAlpha;

    drawingContext.beginPath();
    drawingContext.arc(DISPLAY_CENTER_PX, DISPLAY_CENTER_PX, rimRadius, 0, FULL_CIRCLE_RADIANS);
    drawingContext.strokeStyle = palette.hairline;
    drawingContext.lineWidth = 1;
    drawingContext.stroke();

    drawingContext.beginPath();
    drawingContext.arc(
      DISPLAY_CENTER_PX,
      DISPLAY_CENTER_PX,
      rimRadius,
      TOP_ANGLE_RADIANS - Math.PI * 0.42,
      TOP_ANGLE_RADIANS + Math.PI * 0.14,
    );
    drawingContext.strokeStyle = palette.inkPrimary;
    drawingContext.lineWidth = 4;
    drawingContext.lineCap = "butt";
    drawingContext.stroke();

    drawingContext.beginPath();
    drawingContext.arc(
      DISPLAY_CENTER_PX,
      DISPLAY_CENTER_PX,
      rimRadius,
      Math.PI * 0.16,
      Math.PI * 0.44,
    );
    drawingContext.strokeStyle = palette.signalHighlight;
    drawingContext.lineWidth = 4;
    drawingContext.lineCap = "butt";
    drawingContext.stroke();

    const markAngles = [-Math.PI * 0.22, Math.PI * 0.78];
    for (let markIndex = 0; markIndex < markAngles.length; markIndex += 1) {
      const angleRadians = markAngles[markIndex];
      drawDiagonalMark(
        drawingContext,
        DISPLAY_CENTER_PX + Math.cos(angleRadians) * (rimRadius - 14),
        DISPLAY_CENTER_PX + Math.sin(angleRadians) * (rimRadius - 14),
        4.5,
        1.6,
        palette.inkSecondary,
        rimAlpha,
      );
    }

    drawingContext.restore();
  }

  const emblemY = lerp(124, DISPLAY_CENTER_PX, handoffEase);
  const emblemRadius = lerp(31, 38, handoffEase);
  const emblemAlpha = easeOutCubic(normalizeRange(actProgress, 0.10, 0.28));
  const detentPop = normalizeRange(actProgress, 0.24, 0.56);

  drawDialOsEmblem(
    drawingContext,
    DISPLAY_CENTER_PX,
    emblemY,
    emblemRadius,
    palette,
    emblemAlpha,
    detentPop,
  );

  if (handoffProgress > 0.15) {
    const haloProgress = normalizeRange(handoffProgress, 0.15, 1.0);
    drawingContext.save();
    drawingContext.beginPath();
    drawingContext.arc(
      DISPLAY_CENTER_PX,
      DISPLAY_CENTER_PX,
      lerp(42, 62, easeOutCubic(haloProgress)),
      0,
      FULL_CIRCLE_RADIANS,
    );
    drawingContext.strokeStyle = palette.signalHighlight;
    drawingContext.globalAlpha = (1 - haloProgress) * 0.65;
    drawingContext.lineWidth = 2;
    drawingContext.stroke();
    drawingContext.restore();
  }

  const contentExitAlpha = 1 - normalizeRange(handoffProgress, 0.0, 0.55);
  const contentDriftY = lerp(0, 18, handoffEase);
  const wordmarkReveal = normalizeRange(actProgress, 0.12, 0.30);
  const badgeAlpha = easeOutCubic(wordmarkReveal) * contentExitAlpha;

  drawSimulatorBadge(drawingContext, DISPLAY_CENTER_PX, SIMULATOR_BADGE_CENTER_Y, palette, badgeAlpha);

  if (contentExitAlpha > 0.01 && wordmarkReveal > 0.01) {
    drawingContext.save();
    drawingContext.globalAlpha = badgeAlpha;
    drawingContext.translate(0, contentDriftY);

    const wordmarkY = 196;

    drawingContext.font = `800 32px ${FONT_DISPLAY}`;
    const dialWordWidth = drawingContext.measureText("DIAL").width;
    const badgeGap = 8;
    const badgeWidth = 42;
    const badgeHeight = 29;
    const totalLockupWidth = dialWordWidth + badgeGap + badgeWidth;
    const lockupOriginX = DISPLAY_CENTER_PX - totalLockupWidth / 2;

    drawingContext.fillStyle = palette.inkPrimary;
    drawingContext.textAlign = "left";
    drawingContext.textBaseline = "middle";
    drawingContext.fillText("DIAL", lockupOriginX, wordmarkY);

    const badgeX = lockupOriginX + dialWordWidth + badgeGap;
    const badgeY = wordmarkY - 15;
    drawingContext.fillStyle = palette.signalHighlight;
    drawingContext.fillRect(badgeX, badgeY, badgeWidth, badgeHeight);

    drawingContext.font = `800 18px ${FONT_MONO}`;
    drawingContext.fillStyle = palette.signalHighlightInk;
    drawingContext.textAlign = "center";
    drawingContext.textBaseline = "middle";
    drawingContext.fillText("OS", badgeX + badgeWidth / 2, wordmarkY + 0.5);

    const dividerHalfWidth = lerp(0, 74, lockSettle);
    const dividerY = 228;
    drawingContext.strokeStyle = palette.hairlineStrong;
    drawingContext.lineWidth = 1;
    drawingContext.beginPath();
    drawingContext.moveTo(DISPLAY_CENTER_PX - dividerHalfWidth, dividerY);
    drawingContext.lineTo(DISPLAY_CENTER_PX + dividerHalfWidth, dividerY);
    drawingContext.stroke();

    if (lockSettle > 0.4) {
      drawCrosshairMark(
        drawingContext,
        DISPLAY_CENTER_PX - dividerHalfWidth - 6,
        dividerY,
        3,
        1,
        palette.inkSecondary,
        contentExitAlpha,
      );
      drawCrosshairMark(
        drawingContext,
        DISPLAY_CENTER_PX + dividerHalfWidth + 6,
        dividerY,
        3,
        1,
        palette.inkSecondary,
        contentExitAlpha,
      );
    }

    const statusAlpha = easeOutCubic(normalizeRange(actProgress, 0.30, 0.56)) * contentExitAlpha;
    drawingContext.globalAlpha = statusAlpha;
    drawingContext.font = `700 11px ${FONT_MONO}`;
    const welcomeLabel = "WELCOME";
    const welcomeLabelWidth = drawingContext.measureText(welcomeLabel).width;
    const indicatorSize = 7;
    const indicatorGap = 7;
    const statusOriginX =
      DISPLAY_CENTER_PX - (indicatorSize + indicatorGap + welcomeLabelWidth) / 2;

    drawingContext.fillStyle = palette.signalHighlight;
    drawingContext.fillRect(statusOriginX, 248.5, indicatorSize, indicatorSize);

    drawingContext.fillStyle = palette.inkPrimary;
    drawingContext.textAlign = "left";
    drawingContext.fillText(welcomeLabel, statusOriginX + indicatorSize + indicatorGap, 252);

    const resolvedSummary =
      typeof summaryLine === "string" && summaryLine.length > 0
        ? summaryLine
        : "V1.0  \u00b7  LIB OK  \u00b7  0 TRACKS";
    drawingContext.font = `600 10px ${FONT_MONO}`;
    drawingContext.fillStyle = palette.inkSecondary;
    drawingContext.textAlign = "center";
    drawingContext.fillText(resolvedSummary, DISPLAY_CENTER_PX, 276);

    drawingContext.restore();
  }

  if (actProgress < 0.32) {
    const shutterEnter = normalizeRange(actProgress, 0.0, 0.14);
    const shutterExit = normalizeRange(actProgress, 0.14, 0.32);
    const bandY = 166;
    const bandHeight = 56;
    const fullBandSpan = 280;
    const leftEdgeX =
      DISPLAY_CENTER_PX - fullBandSpan / 2 + easeInOutCubic(shutterExit) * fullBandSpan;
    const rightEdgeX =
      DISPLAY_CENTER_PX - fullBandSpan / 2 + easeOutExpo(shutterEnter) * fullBandSpan;
    const activeWidth = Math.max(0, rightEdgeX - leftEdgeX);

    if (activeWidth > 1) {
      drawingContext.save();
      drawingContext.fillStyle = palette.signalHighlight;
      drawingContext.fillRect(leftEdgeX, bandY, activeWidth, bandHeight);
      drawingContext.restore();
    }
  }
}

/**
 * Render the Engine Delay / bus warning screen with live timer and tappable retry chord.
 *
 * @param {CanvasRenderingContext2D} drawingContext Canvas 2D context.
 * @param {number} elapsedSeconds Monotonic boot elapsed seconds.
 * @param {number} warningElapsedSeconds Seconds spent in warning state.
 * @param {object} palette Semantic Dark/Light appearance palette.
 */
export function renderWarningScenario(
  drawingContext,
  elapsedSeconds,
  warningElapsedSeconds,
  palette,
) {
  const segmentCount = 24;
  const rotationRadians = elapsedSeconds * 0.45;

  drawingContext.save();
  drawingContext.translate(DISPLAY_CENTER_PX, DISPLAY_CENTER_PX);
  drawingContext.rotate(rotationRadians);

  for (let segmentIndex = 0; segmentIndex < segmentCount; segmentIndex += 1) {
    const startAngle = (segmentIndex / segmentCount) * FULL_CIRCLE_RADIANS;
    const sweepAngle = (FULL_CIRCLE_RADIANS / segmentCount) * 0.56;
    const isHotSegment = segmentIndex % 6 === 0;

    drawingContext.beginPath();
    drawingContext.arc(0, 0, BUFFER_ARC_RADIUS_PX, startAngle, startAngle + sweepAngle);
    drawingContext.strokeStyle = isHotSegment ? palette.signalActive : palette.hairlineStrong;
    drawingContext.lineWidth = isHotSegment ? 5 : 2.5;
    drawingContext.stroke();
  }
  drawingContext.restore();

  drawHazardStripeBand(
    drawingContext,
    DISPLAY_CENTER_PX - 68,
    72,
    136,
    12,
    elapsedSeconds * 28,
    palette.signalActive,
  );

  const plateWidth = 196;
  const plateHeight = 32;
  drawingContext.save();
  drawingContext.fillStyle = palette.signalActive;
  drawingContext.fillRect(DISPLAY_CENTER_PX - plateWidth / 2, 118, plateWidth, plateHeight);

  drawingContext.font = `800 13px ${FONT_MONO}`;
  drawingContext.fillStyle = "#FFFFFF";
  drawingContext.textAlign = "center";
  drawingContext.textBaseline = "middle";
  drawingContext.fillText("AUDIO ENGINE SLOW", DISPLAY_CENTER_PX, 134.5);

  const safeWarningSeconds = Number.isFinite(warningElapsedSeconds)
    ? Math.max(0, warningElapsedSeconds)
    : 0;
  drawingContext.font = `800 40px ${FONT_DISPLAY}`;
  drawingContext.fillStyle = palette.inkPrimary;
  drawingContext.fillText(`${safeWarningSeconds.toFixed(1)}s`, DISPLAY_CENTER_PX, 192);

  const busLabel = "POLLING LOCAL BUS";
  drawingContext.font = `600 10px ${FONT_MONO}`;
  const busLabelWidth = drawingContext.measureText(busLabel).width;
  const pulseSize = 7;
  const pulseGap = 7;
  const busOriginX = DISPLAY_CENTER_PX - (pulseSize + pulseGap + busLabelWidth) / 2;
  const pulseAlpha = 0.35 + 0.65 * (0.5 + 0.5 * Math.sin(elapsedSeconds * 5.2));

  drawingContext.globalAlpha = pulseAlpha;
  drawingContext.fillStyle = palette.signalHighlight;
  drawingContext.fillRect(busOriginX, 229.5, pulseSize, pulseSize);

  drawingContext.globalAlpha = 1;
  drawingContext.fillStyle = palette.inkSecondary;
  drawingContext.textAlign = "left";
  drawingContext.fillText(busLabel, busOriginX + pulseSize + pulseGap, 233);

  const retryPlateWidth = 142;
  const retryPlateHeight = 32;
  const retryLeftX = DISPLAY_CENTER_PX - retryPlateWidth / 2;
  const retryTopY = 262;
  const retryCenterY = retryTopY + retryPlateHeight / 2;

  traceChamferPlate(drawingContext, retryLeftX, retryTopY, retryPlateWidth, retryPlateHeight, 7);
  drawingContext.fillStyle = palette.surface;
  drawingContext.fill();
  drawingContext.strokeStyle = palette.hairlineStrong;
  drawingContext.lineWidth = 1;
  drawingContext.stroke();

  const retryLabel = "TAP TO RETRY";
  drawingContext.font = `700 10px ${FONT_MONO}`;
  const retryLabelWidth = drawingContext.measureText(retryLabel).width;
  const retrySquareSize = 5;
  const retryGap = 7;
  const retryOriginX =
    DISPLAY_CENTER_PX - (retrySquareSize + retryGap + retryLabelWidth) / 2;

  drawingContext.fillStyle = palette.signalHighlight;
  drawingContext.fillRect(
    retryOriginX,
    retryCenterY - retrySquareSize / 2,
    retrySquareSize,
    retrySquareSize,
  );

  drawingContext.fillStyle = palette.inkPrimary;
  drawingContext.textAlign = "left";
  drawingContext.fillText(retryLabel, retryOriginX + retrySquareSize + retryGap, retryCenterY + 0.5);
  drawingContext.restore();
}

/**
 * Render the active DialOS Boot screen from a deterministic boot snapshot.
 *
 * @param {CanvasRenderingContext2D} drawingContext Canvas 2D context.
 * @param {object} bootSnapshot Render snapshot from createBootController().
 * @param {object} palette Semantic Dark/Light appearance palette.
 */
export function renderBootScreen(drawingContext, bootSnapshot, palette) {
  if (!bootSnapshot) {
    return;
  }

  const phase = bootSnapshot.phase;
  const actProgress = clamp(Number(bootSnapshot.actProgress) || 0, 0, 1);
  const handoffProgress = clamp(Number(bootSnapshot.handoffProgress) || 0, 0, 1);
  const elapsedSeconds = Number(bootSnapshot.elapsedSeconds) || 0;

  if (phase === "warning") {
    renderWarningScenario(
      drawingContext,
      elapsedSeconds,
      Number(bootSnapshot.warningElapsedSeconds) || 0,
      palette,
    );
    return;
  }

  if (phase === "wake") {
    renderActWakeMatrix(drawingContext, actProgress, palette);
    return;
  }

  if (phase === "buffer") {
    renderActBufferSweep(
      drawingContext,
      actProgress,
      elapsedSeconds,
      Number(bootSnapshot.sweepProgress) || 0,
      clamp(Number(bootSnapshot.bufferExitProgress) || 0, 0, 1),
      bootSnapshot.statusLabel,
      palette,
    );
    return;
  }

  renderActSignalLock(
    drawingContext,
    actProgress,
    handoffProgress,
    bootSnapshot.summaryLine,
    palette,
  );
}

/**
 * Hit-test canvas coordinates against Boot screen interactive controls.
 *
 * @param {number} pointX Display coordinate X in [0, 360].
 * @param {number} pointY Display coordinate Y in [0, 360].
 * @param {object} bootSnapshot Render snapshot from createBootController().
 * @returns {"retry" | null} Action identifier when tapped inside active target.
 */
export function hitTestBootScreen(pointX, pointY, bootSnapshot) {
  if (!bootSnapshot || bootSnapshot.phase !== "warning") {
    return null;
  }
  if (!Number.isFinite(pointX) || !Number.isFinite(pointY)) {
    return null;
  }
  if (
    pointY >= RETRY_HIT_TOP_Y &&
    pointY <= RETRY_HIT_BOTTOM_Y &&
    Math.abs(pointX - DISPLAY_CENTER_PX) <= RETRY_HIT_HALF_WIDTH_PX
  ) {
    return "retry";
  }
  return null;
}
