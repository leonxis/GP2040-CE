import { useEffect, useRef, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { useFormikContext } from 'formik';
import { Button, FormCheck, Modal, Table, Form } from 'react-bootstrap';

import Section from '../Components/Section';
import StickCalibrationModal from '../Components/StickCalibrationModal';
import RangeCalibrationModal from '../Components/RangeCalibrationModal';
import { AddonPropTypes } from '../Pages/AddonsConfigPage';

// Type definitions
type CurvePoint = { x: number; y: number };
type CurvePointInput = { x: string; y: string };

const CIRCULARITY_DATA_SIZE = 48; // Number of angular positions to sample
const ADC_MAX = 4095;
const ADC_CENTER = ADC_MAX / 2.0;  // 2047.5, matches backend

/**
 * Calculates circularity error for stick movement data.
 * @param data - Array of distance values at different angular positions
 * @returns RMS deviation as percentage
 */
const calculateCircularityError = (data: number[]): number => {
	// Sum of squared deviations from ideal distance of 1.0, only for values > 0.2
	const sumSquaredDeviations = data.reduce((acc, val) =>
		val > 0.2 ? acc + Math.pow(val - 1, 2) : acc, 0);

	// Calculate RMS deviation as percentage
	const validDataCount = data.filter(val => val > 0.2).length;
	return validDataCount > 0 ? Math.sqrt(sumSquaredDeviations / validDataCount) * 100 : 0;
};

/**
 * Converts stick value (-1 to 1) to DS4 normalized value with 255-level quantization
 * @param stickValue - Stick value in range -1 to 1
 * @returns DS4 normalized value in range -1 to 1 with 255-level resolution
 */
const convertToDS4Normalized = (stickValue: number): string => {
	// Convert from -1 to 1 range to 0 to 1 range
	const normalized = (stickValue + 1) / 2;
	// Quantize to DS4 255 levels (1-255)
	const ds4Value = Math.max(1, Math.min(255, Math.round(normalized * 254) + 1));
	// Convert back to -1 to 1 range with DS4 resolution (center at 0)
	const ds4Normalized = (ds4Value / 255) * 2 - 1;
	return ds4Normalized.toFixed(5);
};

/**
 * Common button style for finetune center adjustment buttons
 */
const finetuneButtonStyle: React.CSSProperties = {
	width: '18px',
	height: '18px',
	padding: 0,
	fontSize: '12px',
	lineHeight: '1',
	display: 'flex',
	alignItems: 'center',
	justifyContent: 'center',
	color: '#666',
	borderColor: '#ccc'
};

/**
 * Apply finetune shape adjustments to range_data (matches backend logic)
 * @param rangeData Original calibration data array
 * @param forceCircular Whether force circular is enabled
 * @param amplify Amplify factor
 * @returns Adjusted range data array
 */
const applyFinetuneShapeAdjustments = (
	rangeData: number[],
	forceCircular: boolean,
	amplify: number
): number[] => {
	// Create a copy of the range data to avoid mutating the original
	const adjustedData = [...rangeData];
	
	if (!forceCircular) {
		// When force circular is disabled, set all scaling ratios to the minimum value
		// Find the minimum scaling ratio
		let minScale = adjustedData[0];
		for (let i = 1; i < adjustedData.length; i++) {
			if (adjustedData[i] < minScale) {
				minScale = adjustedData[i];
			}
		}
		
		// Set all scaling ratios to the minimum value
		for (let i = 0; i < adjustedData.length; i++) {
			adjustedData[i] = minScale;
		}
	}
	// Note: When force_circular is true, scaling ratios remain unchanged at this point
	
	// Apply amplify factor to all scaling ratios (regardless of force_circular setting)
	const amplifyFactor = 1.0 + amplify / 100.0;
	if (amplifyFactor > 0.0) {
		for (let i = 0; i < adjustedData.length; i++) {
			adjustedData[i] /= amplifyFactor;
		}
	}
	
	return adjustedData;
};

/**
 * Get interpolated scale for a given angle using range calibration data (matches backend logic)
 * @param angle Angle in radians (-PI to PI)
 * @param rangeData Range calibration data array (already adjusted by applyFinetuneShapeAdjustments)
 * @returns Interpolated scale value, or 0.0 if no calibration data
 */
const getInterpolatedScale = (angle: number, rangeData: number[]): number => {
	// Check if we have calibration data
	// If no calibration data, return 1.0 for 1:1 native output (matches backend logic)
	if (!rangeData || rangeData.length === 0 || rangeData.every(v => v <= 0)) {
		return 1.0;
	}
	
	// Convert angle from [-PI, PI] to [0, 2*PI] then to [0, CIRCULARITY_DATA_SIZE]
	const normalizedAngle = (angle + Math.PI) / (2.0 * Math.PI);  // 0.0 to 1.0
	const index = normalizedAngle * CIRCULARITY_DATA_SIZE;
	
	// Get the two adjacent indices for interpolation
	const i0 = Math.floor(index) % CIRCULARITY_DATA_SIZE;
	const i1 = (i0 + 1) % CIRCULARITY_DATA_SIZE;
	const t = index - Math.floor(index);  // Fractional part (0.0 to 1.0)
	
	// Linear interpolation of adjusted calibration data
	const r0 = rangeData[i0] || 0;
	const r1 = rangeData[i1] || 0;
	return r0 * (1.0 - t) + r1 * t;
};

/**
 * Trim cartesian coordinates to square [-1, 1] boundary (DS4-style square trimming)
 * @param x Input X coordinate
 * @param y Input Y coordinate
 * @returns Trimmed coordinates {x, y}
 */
const trimToSquare = (x: number, y: number): { x: number; y: number } => {
	// Trim to -1,-1 to 1,1 square
	return {
		x: Math.max(-1.0, Math.min(1.0, x)),
		y: Math.max(-1.0, Math.min(1.0, y))
	};
};

/**
 * Processes joystick data through coordinate transformation pipeline (matches backend logic)
 * @param rawX - Raw ADC X value
 * @param rawY - Raw ADC Y value
 * @param centerX - Calibrated center X value
 * @param centerY - Calibrated center Y value
 * @param rangeData - Range calibration data array (already adjusted by applyFinetuneShapeAdjustments)
 * @returns Processed stick data and detail information
 */
const processJoystickData = (
	rawX: number,
	rawY: number,
	centerX: number,
	centerY: number,
	rangeData: number[]
) => {
	// Step 2: Coordinate translation (offset transformation)
	const dX_value = centerX - ADC_CENTER;
	const dY_value = centerY - ADC_CENTER;
	const offset_x = rawX - dX_value;
	const offset_y = rawY - dY_value;
	
	// Step 3: Move to adc_offset_center coordinate system
	const offset_center_x = offset_x - ADC_CENTER;
	const offset_center_y = offset_y - ADC_CENTER;
	
	// Step 4: Range calibration scaling (radial scaling) with interpolation
	const current_distance = Math.sqrt(offset_center_x * offset_center_x + offset_center_y * offset_center_y);
	const angle = Math.atan2(offset_center_y, offset_center_x);
	const scale = getInterpolatedScale(angle, rangeData);
	
	const angleIndex = Math.round((angle + Math.PI) * CIRCULARITY_DATA_SIZE / (2 * Math.PI)) % CIRCULARITY_DATA_SIZE;
	
	let scaled_center_x = 0;
	let scaled_center_y = 0;
	
	// Apply radial scaling (scale is always > 0: 1.0 when uncalibrated, calibrated value when calibrated)
	if (current_distance > 0.0) {
		scaled_center_x = offset_center_x / scale;
		scaled_center_y = offset_center_y / scale;
	} else {
		// Zero distance: no scaling needed
		scaled_center_x = offset_center_x;
		scaled_center_y = offset_center_y;
	}
	
	// Step 5: Normalize to [0.0, 1.0] range
	const normalized_x = scaled_center_x / ADC_MAX + 0.5;
	const normalized_y = scaled_center_y / ADC_MAX + 0.5;
	
	// Step 6: Apply square trimming to prevent output values > 1.0 (DS4-style)
	// Convert from [0.0, 1.0] range (center 0.5) to [-1, 1] range (center 0) for trimming
	const x_normalized = (normalized_x - 0.5) * 2.0;  // [0.0, 1.0] -> [-1, 1]
	const y_normalized = (normalized_y - 0.5) * 2.0;  // [0.0, 1.0] -> [-1, 1]
	
	// Trim to square [-1, 1] boundary
	const trimmed = trimToSquare(x_normalized, y_normalized);
	
	// Convert to display format (-1 to 1) - trimmed values are already in [-1, 1] range
	const stickX = trimmed.x;
	const stickY = trimmed.y;
	
	return {
		stickX,
		stickY,
		detailData: {
			centerX,
			centerY,
			rawAdcX: rawX,
			rawAdcY: rawY,
			angleIndex,
			scale,
			offsetCenterX: offset_center_x,
			offsetCenterY: offset_center_y,
			scaledCenterX: scaled_center_x,
			scaledCenterY: scaled_center_y,
			currentDistance: current_distance, // Distance before range calibration scaling
			normalizedX: trimmed.x * 0.5 + 0.5,
			normalizedY: trimmed.y * 0.5 + 0.5,
		}
	};
};

/**
 * Converts circularity value to color (hue)
 */
const ccToColor = (cc: number): number => {
	const hh = Math.max(0, Math.min(120, (1 - cc) * 120));
	return hh;
};

/**
 * Draws analog stick position on a canvas
 * Based on stick-renderer.js from ds4 project
 */
const drawStickPosition = (
	ctx: CanvasRenderingContext2D,
	centerX: number,
	centerY: number,
	radius: number,
	stickX: number, // -1 to 1
	stickY: number, // -1 to 1
	circularityData?: number[] | null,
	zoom10x?: boolean, // If true, zoom to -0.1 to 0.1 range
) => {
	// Fill entire canvas with white background
	ctx.fillStyle = '#ffffff';
	ctx.fillRect(0, 0, ctx.canvas.width, ctx.canvas.height);

	// Calculate effective radius and scale based on zoom mode
	let effectiveRadius = radius;
	let scale = 1.0;
	if (zoom10x) {
		// In zoom mode, map -0.1 to 0.1 range to full canvas
		scale = 0.1; // Scale factor: 0.1 range maps to full radius
		effectiveRadius = radius; // Keep full radius for drawing
	}

	// Draw base circle (outer boundary) - scaled for zoom mode
	ctx.lineWidth = 2;
	ctx.fillStyle = '#ffffff';
	ctx.strokeStyle = '#000000';
	ctx.beginPath();
	ctx.arc(centerX, centerY, effectiveRadius, 0, 2 * Math.PI);
	ctx.closePath();
	ctx.fill();
	ctx.stroke();

	// Draw red dashed circle at 0.03528 radius in zoom mode
	if (zoom10x) {
		const redCircleRadius = (0.03528 / scale) * effectiveRadius;
		ctx.strokeStyle = '#ff0000';
		ctx.lineWidth = 1;
		ctx.setLineDash([5, 5]);
		ctx.beginPath();
		ctx.arc(centerX, centerY, redCircleRadius, 0, 2 * Math.PI);
		ctx.closePath();
		ctx.stroke();
		ctx.setLineDash([]);
	}

	// Draw circularity visualization if data provided (draw before stick position)
	if (circularityData && circularityData.length > 0) {
		const MAX_N = CIRCULARITY_DATA_SIZE;

		for (let i = 0; i < MAX_N; i++) {
			const kd = circularityData[i];
			const kd1 = circularityData[(i + 1) % CIRCULARITY_DATA_SIZE];
			if (kd === undefined || kd1 === undefined || kd === 0) continue;
			
			const ka = i * Math.PI * 2 / MAX_N;
			const ka1 = ((i + 1) % MAX_N) * 2 * Math.PI / MAX_N;

			const kx = Math.cos(ka) * kd;
			const ky = Math.sin(ka) * kd;
			const kx1 = Math.cos(ka1) * kd1;
			const ky1 = Math.sin(ka1) * kd1;

			ctx.beginPath();
			ctx.moveTo(centerX, centerY);
			ctx.lineTo(centerX + kx * radius, centerY + ky * radius);
			ctx.lineTo(centerX + kx1 * radius, centerY + ky1 * radius);
			ctx.lineTo(centerX, centerY);
			ctx.closePath();

			const cc = (kd + kd1) / 2;
			const hh = ccToColor(cc);
			ctx.fillStyle = `hsla(${Math.round(hh)}, 100%, 50%, 0.5)`;
			ctx.fill();
		}
	}

	// Draw crosshairs
	ctx.strokeStyle = '#aaaaaa';
	ctx.lineWidth = 1;
	ctx.beginPath();
	ctx.moveTo(centerX - radius, centerY);
	ctx.lineTo(centerX + radius, centerY);
	ctx.closePath();
	ctx.stroke();

	ctx.beginPath();
	ctx.moveTo(centerX, centerY - radius);
	ctx.lineTo(centerX, centerY + radius);
	ctx.closePath();
	ctx.stroke();

	// Draw stick line from center to position (scaled for zoom mode)
	const scaledStickX = zoom10x ? stickX / scale : stickX;
	const scaledStickY = zoom10x ? stickY / scale : stickY;
	ctx.strokeStyle = '#000000';
	ctx.lineWidth = 2;
	ctx.beginPath();
	ctx.moveTo(centerX, centerY);
	ctx.lineTo(centerX + scaledStickX * effectiveRadius, centerY + scaledStickY * effectiveRadius);
	ctx.stroke();

	// Draw filled circle at stick position
	ctx.beginPath();
	ctx.arc(
		centerX + scaledStickX * effectiveRadius,
		centerY + scaledStickY * effectiveRadius,
		4,
		0,
		2 * Math.PI,
	);
	ctx.fillStyle = '#030b84ff';
	ctx.fill();

	// Draw center point
	ctx.beginPath();
	ctx.arc(centerX, centerY, 2, 0, 2 * Math.PI);
	ctx.fillStyle = '#ff0000';
	ctx.fill();

	// Draw circularity error text if enough data provided
	if (circularityData && circularityData.filter(n => n > 0.3).length > 10) {
		const circularityError = calculateCircularityError(circularityData);

		ctx.fillStyle = '#fff';
		ctx.strokeStyle = '#444';
		ctx.lineWidth = 3;
		ctx.textAlign = 'center';
		ctx.textBaseline = 'middle';

		ctx.font = '24px Arial';
		const textY = centerY + radius * 0.5;
		const text = `${circularityError.toFixed(1)} %`;

		ctx.strokeText(text, centerX, textY);
		ctx.fillText(text, centerX, textY);
	}
};

/**
 * Draws curve editor on canvas
 * @param ctx Canvas context
 * @param width Canvas width
 * @param height Canvas height
 * @param points Control points (excluding start (0,0) and end (1,1))
 * @param progressRatio Progress ratio (0-1) for orange highlight line, or undefined to hide
 * @param innerDeadzone Inner deadzone value (0-1), X-axis intercept
 * @param antiDeadzone Anti-deadzone value (0-1), Y-axis intercept
 */
const drawCurveEditor = (
	ctx: CanvasRenderingContext2D,
	width: number,
	height: number,
	points: CurvePoint[],
	progressRatio?: number,
	innerDeadzone: number = 0,
	antiDeadzone: number = 0
) => {
	// Clear canvas
	ctx.fillStyle = '#ffffff';
	ctx.fillRect(0, 0, width, height);
	
	// Draw grid
	ctx.strokeStyle = '#e0e0e0';
	ctx.lineWidth = 1;
	const gridSize = 10;
	for (let i = 0; i <= gridSize; i++) {
		const pos = (i / gridSize) * width;
		// Vertical lines
		ctx.beginPath();
		ctx.moveTo(pos, 0);
		ctx.lineTo(pos, height);
		ctx.stroke();
		// Horizontal lines
		ctx.beginPath();
		ctx.moveTo(0, pos);
		ctx.lineTo(width, pos);
		ctx.stroke();
	}
	
	// Draw axes
	ctx.strokeStyle = '#000000';
	ctx.lineWidth = 2;
	// X axis
	ctx.beginPath();
	ctx.moveTo(0, height);
	ctx.lineTo(width, height);
	ctx.stroke();
	// Y axis
	ctx.beginPath();
	ctx.moveTo(0, height);
	ctx.lineTo(0, 0);
	ctx.stroke();
	
	// Draw axis labels
	ctx.fillStyle = '#000000';
	ctx.font = '12px Arial';
	ctx.textAlign = 'left';
	ctx.textBaseline = 'top';
	ctx.fillText('0', 2, height - 14);
	ctx.textAlign = 'right';
	ctx.fillText('1', width - 2, height - 14);
	ctx.textAlign = 'left';
	ctx.textBaseline = 'bottom';
	ctx.fillText('1', 2, 2);
	ctx.textBaseline = 'top';
	ctx.fillText('0', 2, height - 2);
	
	// Apply deadzone and anti-deadzone to points
	// Deadzone: X-axis intercept - input values < deadzone produce output = antiDeadzone
	// Anti-deadzone: Y-axis intercept - all outputs start from antiDeadzone
	// 
	// Visualization logic:
	// 1. Start point: (innerDeadzone, 0) - curve starts at deadzone position on X-axis
	// 2. Vertical segment: from (innerDeadzone, 0) to (innerDeadzone, antiDeadzone)
	//    This represents the jump from 0 output to antiDeadzone output when input reaches deadzone
	// 3. Actual curve: from (innerDeadzone, antiDeadzone) to (1, 1)
	//    Points are remapped: x stays in [deadzone, 1], y is remapped from [0, 1] to [antiDeadzone, 1]
	const applyDeadzones = (x: number, y: number): CurvePoint => {
		if (x < innerDeadzone) {
			// Input is below deadzone threshold: output = antiDeadzone
			// But for visualization, we don't show points before deadzone
			// Return a point that will be filtered out
			return { x: -1, y: -1 }; // Invalid point, will be filtered
		} else {
			// Input is above deadzone: keep x in [deadzone, 1] range
			// Apply anti-deadzone: remap y from [0, 1] to [antiDeadzone, 1]
			const remappedY = antiDeadzone + y * (1 - antiDeadzone);
			return { x, y: remappedY };
		}
	};
	
	// Build full point list with deadzone/anti-deadzone applied
	// Start point: (innerDeadzone, 0) - curve starts at deadzone position
	// Vertical segment end: (innerDeadzone, antiDeadzone)
	// Actual curve points: from (innerDeadzone, antiDeadzone) onwards
	const startPoint = { x: innerDeadzone, y: 0 };
	const verticalEndPoint = { x: innerDeadzone, y: antiDeadzone };
	const adjustedPoints = points
		.map(p => applyDeadzones(p.x, p.y))
		.filter(p => p.x >= 0 && p.y >= 0) // Filter out invalid points (x < deadzone)
		.filter(p => p.x >= innerDeadzone) // Only keep points at or after deadzone
		.sort((a, b) => a.x - b.x);
	const endPoint = applyDeadzones(1, 1);
	
	// Build full points: start -> vertical end -> adjusted curve points -> end
	// When deadzone = 0 but antiDeadzone > 0, start directly from (0, antiDeadzone)
	// When deadzone > 0, add vertical segment from (deadzone, 0) to (deadzone, antiDeadzone)
	const fullPoints: CurvePoint[] = [];
	if (innerDeadzone > 0) {
		// Deadzone > 0: add start point and vertical segment
		fullPoints.push(startPoint, verticalEndPoint);
	} else if (antiDeadzone > 0) {
		// Deadzone = 0 but antiDeadzone > 0: start from (0, antiDeadzone)
		fullPoints.push(verticalEndPoint);
	} else {
		// Both deadzone and antiDeadzone = 0: start from (0, 0)
		fullPoints.push(startPoint);
	}
	fullPoints.push(...adjustedPoints, endPoint);
	
	// Step 2: Draw deadzone and anti-deadzone reference lines (dashed)
	ctx.strokeStyle = '#999999';
	ctx.lineWidth = 1;
	ctx.setLineDash([5, 5]); // Dashed line pattern
	
	// Draw deadzone vertical line (if deadzone > 0)
	if (innerDeadzone > 0) {
		ctx.beginPath();
		const deadzoneX = innerDeadzone * width;
		ctx.moveTo(deadzoneX, 0);
		ctx.lineTo(deadzoneX, height);
		ctx.stroke();
	}
	
	// Draw anti-deadzone horizontal line (if antiDeadzone > 0)
	if (antiDeadzone > 0) {
		ctx.beginPath();
		const antiDeadzoneY = height - antiDeadzone * height;
		ctx.moveTo(0, antiDeadzoneY);
		ctx.lineTo(width, antiDeadzoneY);
		ctx.stroke();
	}
	
	// Draw reference diagonal line from (deadzone, antiDeadzone) to (1, 1)
	if (innerDeadzone > 0 || antiDeadzone > 0) {
		ctx.beginPath();
		const refStartX = innerDeadzone * width;
		const refStartY = height - antiDeadzone * height;
		ctx.moveTo(refStartX, refStartY);
		ctx.lineTo(width, 0); // (1, 1) in canvas coordinates
		ctx.stroke();
	}
	
	ctx.setLineDash([]); // Reset to solid line
	
	// Step 3: Draw light gray mask area (rectangle with diagonal from (deadzone, antiDeadzone) to (1,1))
	// The mask area is a rectangle: from (deadzone, antiDeadzone) to (1, 1)
	// Rectangle corners: (deadzone, antiDeadzone), (1, antiDeadzone), (1, 1), (deadzone, 1)
	if (innerDeadzone > 0 || antiDeadzone > 0) {
		ctx.fillStyle = 'rgba(200, 200, 200, 0.3)'; // Light gray with transparency
		ctx.beginPath();
		const deadzoneX = innerDeadzone * width;
		const antiDeadzoneY = height - antiDeadzone * height;
		// Rectangle: (deadzone, antiDeadzone) -> (1, antiDeadzone) -> (1, 1) -> (deadzone, 1) -> (deadzone, antiDeadzone)
		ctx.moveTo(deadzoneX, antiDeadzoneY); // (deadzone, antiDeadzone)
		ctx.lineTo(width, antiDeadzoneY); // (1, antiDeadzone)
		ctx.lineTo(width, 0); // (1, 1)
		ctx.lineTo(deadzoneX, 0); // (deadzone, 1)
		ctx.closePath();
		ctx.fill();
	}
	
	// Step 4: Draw curve
	// Build curve points from gray origin to (1, 1)
	// Curve starts from (deadzone, antiDeadzone) and goes to (1, 1)
	const grayOriginPoint = { x: innerDeadzone, y: antiDeadzone };
	const curveStartPoint = (innerDeadzone > 0 || antiDeadzone > 0) ? grayOriginPoint : { x: 0, y: 0 };
	const curvePoints: CurvePoint[] = [curveStartPoint, ...adjustedPoints, endPoint];
	
	// Calculate total curve length along the path (from gray origin to end)
	let totalCurveLength = 0;
	const segmentLengths: number[] = [];
	for (let i = 0; i < curvePoints.length - 1; i++) {
		const dx = (curvePoints[i + 1].x - curvePoints[i].x) * width;
		const dy = (curvePoints[i + 1].y - curvePoints[i].y) * height;
		const segmentLength = Math.sqrt(dx * dx + dy * dy);
		segmentLengths.push(segmentLength);
		totalCurveLength += segmentLength;
	}
	
	// Draw full curve line in dark gray (from gray origin to end)
	ctx.strokeStyle = '#404040'; // Dark gray
	ctx.lineWidth = 2;
	ctx.beginPath();
	for (let i = 0; i < curvePoints.length; i++) {
		const px = curvePoints[i].x * width;
		const py = height - curvePoints[i].y * height; // Flip Y axis
		if (i === 0) {
			ctx.moveTo(px, py);
		} else {
			ctx.lineTo(px, py);
		}
	}
	ctx.stroke();
	
	// Step 5: Draw orange highlight line if progressRatio is provided
	// Highlight line: from origin (0,0) -> (deadzone, 0) -> jump to (deadzone, antiDeadzone) -> continue along curve
	if (progressRatio !== undefined && progressRatio >= 0) {
		ctx.strokeStyle = '#FFA500'; // Orange
		ctx.lineWidth = 2;
		ctx.beginPath();
		
		// Always start from origin (0, 0)
		ctx.moveTo(0, height);
		
		// If deadzone > 0
		if (innerDeadzone > 0) {
			const deadzoneX = innerDeadzone * width;
			if (progressRatio < innerDeadzone) {
				// progressRatio < deadzone: only draw horizontal line from (0, 0) to (progressRatio, 0)
				const progressX = progressRatio * width;
				ctx.lineTo(progressX, height);
				ctx.stroke();
				// Don't return early - continue to draw points in Step 6
			} else {
				// progressRatio >= deadzone: draw horizontal line from (0, 0) to (deadzone, 0)
				ctx.lineTo(deadzoneX, height); // (deadzone, 0) in canvas coordinates
				// Jump to (deadzone, antiDeadzone) and draw vertical line
				const antiDeadzoneY = height - antiDeadzone * height;
				ctx.lineTo(deadzoneX, antiDeadzoneY); // Vertical line from (deadzone, 0) to (deadzone, antiDeadzone)
				
				// Continue along the curve from gray origin
				if (totalCurveLength > 0) {
					// Find the point on the curve where x coordinate equals progressRatio
					const targetX = Math.min(progressRatio, 1.0);
					let found = false;
					
					// Search through curve segments to find the one containing targetX
					for (let i = 0; i < curvePoints.length - 1; i++) {
						const p1 = curvePoints[i];
						const p2 = curvePoints[i + 1];
						
						if (targetX >= p1.x && targetX <= p2.x) {
							// targetX is within this segment, interpolate Y
							const t = p2.x !== p1.x ? (targetX - p1.x) / (p2.x - p1.x) : 0;
							const targetY = p1.y + t * (p2.y - p1.y);
							
							// Draw to this point
							const targetPx = targetX * width;
							const targetPy = height - targetY * height;
							ctx.lineTo(targetPx, targetPy);
							found = true;
							break;
						} else if (targetX > p2.x) {
							// Draw to the end of this segment, continue to next
							const px2 = p2.x * width;
							const py2 = height - p2.y * height;
							ctx.lineTo(px2, py2);
						}
					}
					
					// If progressRatio >= 1.0 and we haven't found the point, draw to end point
					if (progressRatio >= 1.0 && !found) {
						const endPx = endPoint.x * width;
						const endPy = height - endPoint.y * height;
						ctx.lineTo(endPx, endPy);
					}
				}
				ctx.stroke();
			}
		} else if (antiDeadzone > 0) {
			// No deadzone but antiDeadzone > 0: start from (0, antiDeadzone)
			const antiDeadzoneY = height - antiDeadzone * height;
			ctx.moveTo(0, antiDeadzoneY);
			
			// Continue along the curve from gray origin
			if (totalCurveLength > 0 && progressRatio > 0) {
				// Find the point on the curve where x coordinate equals progressRatio
				const targetX = Math.min(progressRatio, 1.0);
				let found = false;
				
				// Search through curve segments to find the one containing targetX
				for (let i = 0; i < curvePoints.length - 1; i++) {
					const p1 = curvePoints[i];
					const p2 = curvePoints[i + 1];
					
					if (targetX >= p1.x && targetX <= p2.x) {
						// targetX is within this segment, interpolate Y
						const t = p2.x !== p1.x ? (targetX - p1.x) / (p2.x - p1.x) : 0;
						const targetY = p1.y + t * (p2.y - p1.y);
						
						// Draw to this point
						const targetPx = targetX * width;
						const targetPy = height - targetY * height;
						ctx.lineTo(targetPx, targetPy);
						found = true;
						break;
					} else if (targetX > p2.x) {
						// Draw to the end of this segment, continue to next
						const px2 = p2.x * width;
						const py2 = height - p2.y * height;
						ctx.lineTo(px2, py2);
					}
				}
				
				// If progressRatio >= 1.0 and we haven't found the point, draw to end point
				if (progressRatio >= 1.0 && !found) {
					const endPx = endPoint.x * width;
					const endPy = height - endPoint.y * height;
					ctx.lineTo(endPx, endPy);
				}
			}
			ctx.stroke();
		} else {
			// No deadzone and no antiDeadzone: draw directly along curve from origin
			if (totalCurveLength > 0 && progressRatio > 0) {
				// Find the point on the curve where x coordinate equals progressRatio
				const targetX = Math.min(progressRatio, 1.0);
				let found = false;
				
				// Search through curve segments to find the one containing targetX
				for (let i = 0; i < curvePoints.length - 1; i++) {
					const p1 = curvePoints[i];
					const p2 = curvePoints[i + 1];
					
					if (targetX >= p1.x && targetX <= p2.x) {
						// targetX is within this segment, interpolate Y
						const t = p2.x !== p1.x ? (targetX - p1.x) / (p2.x - p1.x) : 0;
						const targetY = p1.y + t * (p2.y - p1.y);
						
						// Draw to this point
						const targetPx = targetX * width;
						const targetPy = height - targetY * height;
						ctx.lineTo(targetPx, targetPy);
						found = true;
						break;
					} else if (targetX > p2.x) {
						// Draw to the end of this segment, continue to next
						const px2 = p2.x * width;
						const py2 = height - p2.y * height;
						ctx.lineTo(px2, py2);
					}
				}
				
				// If progressRatio >= 1.0 and we haven't found the point, draw to end point
				if (progressRatio >= 1.0 && !found) {
					const endPx = endPoint.x * width;
					const endPy = height - endPoint.y * height;
					ctx.lineTo(endPx, endPy);
				}
			}
			ctx.stroke();
		}
	}
	
	// Step 6: Draw all points (origin, gray origin, control points, end point)
	// Draw origin point at (0, 0) - always visible
	ctx.fillStyle = '#00ff00'; // Green
	ctx.beginPath();
	ctx.arc(0, height, 6, 0, 2 * Math.PI);
	ctx.fill();
	ctx.strokeStyle = '#ffffff';
	ctx.lineWidth = 2;
	ctx.stroke();
	
	// Draw gray origin point at (deadzone, antiDeadzone) if deadzone or antiDeadzone > 0
	if (innerDeadzone > 0 || antiDeadzone > 0) {
		const grayOriginX = grayOriginPoint.x * width;
		const grayOriginY = height - grayOriginPoint.y * height;
		ctx.fillStyle = '#808080'; // Gray
		ctx.beginPath();
		ctx.arc(grayOriginX, grayOriginY, 6, 0, 2 * Math.PI);
		ctx.fill();
		ctx.strokeStyle = '#ffffff';
		ctx.lineWidth = 2;
		ctx.stroke();
	}
	
	// Draw control points (red)
	// Apply deadzone/anti-deadzone transformation to control points for correct display
	for (let i = 0; i < points.length; i++) {
		// Apply the same transformation as used for the curve
		const transformedPoint = applyDeadzones(points[i].x, points[i].y);
		// Only draw if point is valid (x >= deadzone)
		if (transformedPoint.x >= innerDeadzone && transformedPoint.x >= 0 && transformedPoint.y >= 0) {
			const px = transformedPoint.x * width;
			const py = height - transformedPoint.y * height;
		
			// Draw point circle
			ctx.fillStyle = '#ff0000'; // Red
			ctx.beginPath();
			ctx.arc(px, py, 6, 0, 2 * Math.PI);
			ctx.fill();
			ctx.strokeStyle = '#ffffff';
			ctx.lineWidth = 2;
			ctx.stroke();
		}
	}
	
	// Draw end point at (1, 1)
	const endPx = endPoint.x * width;
	const endPy = height - endPoint.y * height;
	ctx.fillStyle = '#00ff00'; // Green
	ctx.beginPath();
	ctx.arc(endPx, endPy, 6, 0, 2 * Math.PI);
	ctx.fill();
	ctx.strokeStyle = '#ffffff';
	ctx.lineWidth = 2;
	ctx.stroke();
};

/**
 * Validates and corrects a point's Y value to ensure strict monotonicity
 * Each point's Y value must be > all points with smaller X values (strictly increasing)
 * @param point The point to validate
 * @param allPoints All existing points (excluding start (0,0) and end (1,1))
 * @returns Corrected point with valid Y value
 */
const validatePointMonotonicity = (point: CurvePoint, allPoints: CurvePoint[]): CurvePoint => {
	// Find the maximum Y value among all points with X < point.x
	const maxYBefore = allPoints
		.filter(p => p.x < point.x)
		.reduce((max, p) => Math.max(max, p.y), 0); // Start with 0 (from start point (0,0))
	
	// Ensure point.y > maxYBefore (strictly greater, not equal)
	// Add a small epsilon to ensure strict inequality
	const epsilon = 0.01; // 1% to ensure strict inequality
	const correctedY = Math.max(point.y, maxYBefore + epsilon);
	
	// Clamp to [0, 1] range
	const clampedY = Math.max(0, Math.min(1, correctedY));
	
	return { x: point.x, y: clampedY };
};

/**
 * Validates and corrects all points to ensure strict monotonicity
 * Points are sorted by X, and Y values must be strictly increasing
 * @param points Array of points to validate
 * @returns Array of corrected points, sorted by X with strictly increasing Y values
 */
const validateAllPointsMonotonicity = (points: CurvePoint[]): CurvePoint[] => {
	// Sort by X first
	const sorted = [...points].sort((a, b) => {
		// If X values are equal, sort by Y to ensure consistent ordering
		if (a.x === b.x) {
			return a.y - b.y;
		}
		return a.x - b.x;
	});
	
	// Validate each point in order, ensuring Y is strictly increasing
	const validated: CurvePoint[] = [];
	const epsilon = 0.01; // 1% to ensure strict inequality
	
	for (let i = 0; i < sorted.length; i++) {
		const point = sorted[i];
		
		// Find the maximum Y value among all previously validated points
		const maxYBefore = validated.length > 0
			? validated.reduce((max, p) => Math.max(max, p.y), 0)
			: 0; // Start with 0 (from start point (0,0))
		
		// Ensure point.y > maxYBefore (strictly greater)
		let correctedY = Math.max(point.y, maxYBefore + epsilon);
		
		// Clamp to [0, 1] range
		correctedY = Math.max(0, Math.min(1, correctedY));
		
		// If X is same as previous point, ensure Y is strictly greater
		if (validated.length > 0 && point.x === validated[validated.length - 1].x) {
			// Same X value: ensure Y is strictly greater than previous
			const prevY = validated[validated.length - 1].y;
			correctedY = Math.max(correctedY, prevY + epsilon);
			correctedY = Math.max(0, Math.min(1, correctedY));
		}
		
		validated.push({ x: point.x, y: correctedY });
	}
	
	return validated;
};

/**
 * Applies response curve to a normalized value (0-1)
 * @param value Input value in [0, 1]
 * @param points Control points (excluding start (0,0) and end (1,1))
 * @returns Output value in [0, 1]
 */
const applyResponseCurve = (value: number, points: CurvePoint[]): number => {
	if (points.length === 0) {
		// No curve: linear mapping
		return value;
	}
	
	// Build full point list: start (0,0) + control points + end (1,1)
	const fullPoints: CurvePoint[] = [
		{x: 0, y: 0},
		...points.sort((a, b) => a.x - b.x), // Sort by x coordinate
		{x: 1, y: 1}
	];
	
	// Find the segment containing the input value
	for (let i = 0; i < fullPoints.length - 1; i++) {
		const p1 = fullPoints[i];
		const p2 = fullPoints[i + 1];
		
		if (value >= p1.x && value <= p2.x) {
			// Linear interpolation within this segment
			if (p2.x === p1.x) {
				return p1.y;
			}
			const t = (value - p1.x) / (p2.x - p1.x);
			return p1.y + t * (p2.y - p1.y);
		}
	}
	
	// Should not reach here, but return value as fallback
	return value;
};

/**
 * Calculates output distance from physical distance based on curve, deadzone, and anti-deadzone
 * This matches the logic used in the curve canvas highlight line
 * @param physicalDistance Physical distance in [0, 1]
 * @param curvePoints Control points for the response curve (excluding start and end)
 * @param innerDeadzone Inner deadzone value
 * @param antiDeadzone Anti-deadzone value
 * @returns Output distance in [0, 1]
 */
const calculateOutputDistanceFromCurve = (
	physicalDistance: number,
	curvePoints: CurvePoint[],
	innerDeadzone: number,
	antiDeadzone: number
): number => {
	// If physical distance < deadzone, output distance = 0
	if (physicalDistance < innerDeadzone) {
		return 0;
	}
	
	// If deadzone <= physical distance <= antiDeadzone, output distance = antiDeadzone
	if (physicalDistance <= antiDeadzone) {
		return antiDeadzone;
	}
	
	// If physical distance > antiDeadzone, find Y value on curve where X = physicalDistance
	// Curve starts from gray origin (innerDeadzone, antiDeadzone) and goes to (1, 1)
	// Build curve points: gray origin + control points + end point
	const grayOriginPoint = { x: innerDeadzone, y: antiDeadzone };
	const curveStartPoint = (innerDeadzone > 0 || antiDeadzone > 0) ? grayOriginPoint : { x: 0, y: 0 };
	
	// Apply deadzone/anti-deadzone transformation to control points
	const applyDeadzones = (x: number, y: number): CurvePoint => {
		if (x < innerDeadzone) {
			return { x: -1, y: -1 }; // Invalid point
		} else {
			const remappedY = antiDeadzone + y * (1 - antiDeadzone);
			return { x, y: remappedY };
		}
	};
	
	const adjustedPoints = curvePoints
		.map(p => applyDeadzones(p.x, p.y))
		.filter(p => p.x >= 0 && p.y >= 0 && p.x >= innerDeadzone)
		.sort((a, b) => a.x - b.x);
	
	const endPoint = applyDeadzones(1, 1);
	const fullCurvePoints: CurvePoint[] = [curveStartPoint, ...adjustedPoints, endPoint];
	
	// Find the point on the curve where x = physicalDistance
	const targetX = Math.min(physicalDistance, 1.0);
	
	for (let i = 0; i < fullCurvePoints.length - 1; i++) {
		const p1 = fullCurvePoints[i];
		const p2 = fullCurvePoints[i + 1];
		
		if (targetX >= p1.x && targetX <= p2.x) {
			// Interpolate Y value
			if (p2.x === p1.x) {
				return p1.y;
			}
			const t = (targetX - p1.x) / (p2.x - p1.x);
			return p1.y + t * (p2.y - p1.y);
		} else if (targetX > p2.x) {
			// Continue to next segment
			continue;
		}
	}
	
	// If physicalDistance >= 1.0, return end point Y value
	if (physicalDistance >= 1.0) {
		return endPoint.y;
	}
	
	// Fallback: return physicalDistance (linear mapping)
	return physicalDistance;
};

/**
 * Applies jitter filter to raw ADC values for visualization.
 * Mirrors backend logic: if |current - last| < threshold, keep last value;
 * otherwise accept current value and update last.
 */
const applyJitterFilterToAdc = (
	rawX: number,
	rawY: number,
	threshold: number,
	lastRef: React.MutableRefObject<{ x: number; y: number } | null>
): { x: number; y: number } => {
	// Threshold <= 0 means "no filtering"
	if (!threshold || threshold <= 0) {
		lastRef.current = { x: rawX, y: rawY };
		return { x: rawX, y: rawY };
	}

	const last = lastRef.current ?? { x: rawX, y: rawY };

	let filteredX = rawX;
	let filteredY = rawY;

	if (Math.abs(rawX - last.x) < threshold) {
		filteredX = last.x;
	} else {
		last.x = rawX;
	}

	if (Math.abs(rawY - last.y) < threshold) {
		filteredY = last.y;
	} else {
		last.y = rawY;
	}

	lastRef.current = last;
	return { x: filteredX, y: filteredY };
};

const JoystickCalibration = ({
	values,
	setFieldValue,
}: AddonPropTypes) => {
	const { t } = useTranslation();
	const { handleSubmit } = useFormikContext();
	const leftStickCanvasRef = useRef<HTMLCanvasElement>(null);
	const rightStickCanvasRef = useRef<HTMLCanvasElement>(null);
	const [leftStickData, setLeftStickData] = useState<{ x: number; y: number; rawX: number; rawY: number; progressRatio?: number }>({ x: 0, y: 0, rawX: 0, rawY: 0 });
	const [rightStickData, setRightStickData] = useState<{ x: number; y: number; rawX: number; rawY: number; progressRatio?: number }>({ x: 0, y: 0, rawX: 0, rawY: 0 });
	const [showLeftCalibrationModal, setShowLeftCalibrationModal] = useState(false);
	const [showRightCalibrationModal, setShowRightCalibrationModal] = useState(false);
	const [showLeftRangeModal, setShowLeftRangeModal] = useState(false);
	const [showRightRangeModal, setShowRightRangeModal] = useState(false);
	const [leftFinetuneShapeActive, setLeftFinetuneShapeActive] = useState(false);
	const [rightFinetuneShapeActive, setRightFinetuneShapeActive] = useState(false);
	const [showLeftRangeDataModal, setShowLeftRangeDataModal] = useState(false);
	const [showRightRangeDataModal, setShowRightRangeDataModal] = useState(false);
	const [leftRangeDataSnapshot, setLeftRangeDataSnapshot] = useState<number[]>([]);
	const [rightRangeDataSnapshot, setRightRangeDataSnapshot] = useState<number[]>([]);
	const [leftAngleIndexSnapshot, setLeftAngleIndexSnapshot] = useState(0);
	const [rightAngleIndexSnapshot, setRightAngleIndexSnapshot] = useState(0);
	const [leftFinetuneCenterActive, setLeftFinetuneCenterActive] = useState(false);
	const [rightFinetuneCenterActive, setRightFinetuneCenterActive] = useState(false);
	const [showRangeCalibrationWarning, setShowRangeCalibrationWarning] = useState(false);
	const [leftCurveActive, setLeftCurveActive] = useState(false);
	const [rightCurveActive, setRightCurveActive] = useState(false);
	// Curve control points: array of {x, y} where x and y are in [0, 1] range
	// Maximum 3 points (plus start (0,0) and end (1,1)) = 4 segments
	// Load from config if available
	const [leftCurvePoints, setLeftCurvePoints] = useState<CurvePoint[]>(() => {
		const saved = values?.joystickCurvePoints1;
		return Array.isArray(saved) ? saved as CurvePoint[] : [];
	});
	const [rightCurvePoints, setRightCurvePoints] = useState<CurvePoint[]>(() => {
		const saved = values?.joystickCurvePoints2;
		return Array.isArray(saved) ? saved as CurvePoint[] : [];
	});
	// Temporary input values for curve points (only update state on blur)
	const [leftCurveInputValues, setLeftCurveInputValues] = useState<CurvePointInput[]>(() => {
		const saved = values?.joystickCurvePoints1;
		if (Array.isArray(saved)) {
			return (saved as CurvePoint[]).map(p => ({ x: p.x.toString(), y: p.y.toString() }));
		}
		return [];
	});
	const [rightCurveInputValues, setRightCurveInputValues] = useState<CurvePointInput[]>(() => {
		const saved = values?.joystickCurvePoints2;
		if (Array.isArray(saved)) {
			return (saved as CurvePoint[]).map(p => ({ x: p.x.toString(), y: p.y.toString() }));
		}
		return [];
	});
	const [draggingPointIndex, setDraggingPointIndex] = useState<{stick: 'left' | 'right', index: number} | null>(null);
	const leftCurveCanvasRef = useRef<HTMLCanvasElement>(null);
	const rightCurveCanvasRef = useRef<HTMLCanvasElement>(null);
	
	// Jitter filter state for main canvas visualization (per stick)
	const leftCanvasJitterLastRef = useRef<{ x: number; y: number } | null>(null);
	const rightCanvasJitterLastRef = useRef<{ x: number; y: number } | null>(null);

	// Jitter data correction sampling state for stick 1
	const [leftJitterSampling, setLeftJitterSampling] = useState(false);
	const [leftJitterSamples, setLeftJitterSamples] = useState<Array<{ x: number; y: number }>>([]);
	const [showLeftJitterDataModal, setShowLeftJitterDataModal] = useState(false);
	const [leftJitterStats, setLeftJitterStats] = useState<{
		meanX: number;
		meanY: number;
		varianceX: number;
		varianceY: number;
		meanDeviationX: number;
		meanDeviationY: number;
		deviationRateX: number;
		deviationRateY: number;
		upperDeviationX: number;
		upperDeviationY: number;
		lowerDeviationX: number;
		lowerDeviationY: number;
	} | null>(null);
	const leftJitterSamplingAbortRef = useRef<boolean>(false);
	const leftJitterLastSampleRef = useRef<{ x: number; y: number } | null>(null);
	const leftJitterTimeoutRef = useRef<ReturnType<typeof setTimeout> | null>(null);
	const [leftJitterFilter, setLeftJitterFilter] = useState<number>(0);
	const [leftJitterFilterOriginal, setLeftJitterFilterOriginal] = useState<number>(0);
	
	// Jitter data correction sampling state for stick 2
	const [rightJitterSampling, setRightJitterSampling] = useState(false);
	const [rightJitterSamples, setRightJitterSamples] = useState<Array<{ x: number; y: number }>>([]);
	const [showRightJitterDataModal, setShowRightJitterDataModal] = useState(false);
	const [rightJitterStats, setRightJitterStats] = useState<{
		meanX: number;
		meanY: number;
		varianceX: number;
		varianceY: number;
		meanDeviationX: number;
		meanDeviationY: number;
		deviationRateX: number;
		deviationRateY: number;
		upperDeviationX: number;
		upperDeviationY: number;
		lowerDeviationX: number;
		lowerDeviationY: number;
	} | null>(null);
	const rightJitterSamplingAbortRef = useRef<boolean>(false);
	const rightJitterLastSampleRef = useRef<{ x: number; y: number } | null>(null);
	const rightJitterTimeoutRef = useRef<ReturnType<typeof setTimeout> | null>(null);
	const [rightJitterFilter, setRightJitterFilter] = useState<number>(0);
	const [rightJitterFilterOriginal, setRightJitterFilterOriginal] = useState<number>(0);
	
	// Finetune shape modal state - initialize from values
	const [leftFinetuneShapeForceCircular, setLeftFinetuneShapeForceCircular] = useState(values?.joystickFinetuneShapeForceCircular1 ?? false);
	const [leftFinetuneShapeAmplify, setLeftFinetuneShapeAmplify] = useState(values?.joystickFinetuneShapeAmplify1 ?? 0.0);
	const [rightFinetuneShapeForceCircular, setRightFinetuneShapeForceCircular] = useState(values?.joystickFinetuneShapeForceCircular2 ?? false);
	const [rightFinetuneShapeAmplify, setRightFinetuneShapeAmplify] = useState(values?.joystickFinetuneShapeAmplify2 ?? 0.0);
	
	// Update state when finetune shape becomes active (load saved values from server)
	useEffect(() => {
		if (leftFinetuneShapeActive) {
			setLeftFinetuneShapeForceCircular(values?.joystickFinetuneShapeForceCircular1 ?? false);
			setLeftFinetuneShapeAmplify(values?.joystickFinetuneShapeAmplify1 ?? 0.0);
		}
		if (rightFinetuneShapeActive) {
			setRightFinetuneShapeForceCircular(values?.joystickFinetuneShapeForceCircular2 ?? false);
			setRightFinetuneShapeAmplify(values?.joystickFinetuneShapeAmplify2 ?? 0.0);
		}
	}, [leftFinetuneShapeActive, rightFinetuneShapeActive, values?.joystickFinetuneShapeForceCircular1, values?.joystickFinetuneShapeAmplify1, values?.joystickFinetuneShapeForceCircular2, values?.joystickFinetuneShapeAmplify2]);
	
	// Load jitter filter values when modal opens
	useEffect(() => {
		if (showLeftJitterDataModal) {
			const savedValue = values?.joystickJitterFilter1 ?? 0;
			setLeftJitterFilter(savedValue);
			setLeftJitterFilterOriginal(savedValue);
		}
	}, [showLeftJitterDataModal, values]);
	
	useEffect(() => {
		if (showRightJitterDataModal) {
			const savedValue = values?.joystickJitterFilter2 ?? 0;
			setRightJitterFilter(savedValue);
			setRightJitterFilterOriginal(savedValue);
		}
	}, [showRightJitterDataModal, values]);
	
	// Circularity data for main canvas (used when finetune shape is active)
	const [leftFinetuneShapeCircularityData, setLeftFinetuneShapeCircularityData] = useState<number[]>(new Array(CIRCULARITY_DATA_SIZE).fill(0));
	const [rightFinetuneShapeCircularityData, setRightFinetuneShapeCircularityData] = useState<number[]>(new Array(CIRCULARITY_DATA_SIZE).fill(0));
	
	// Detailed data for display
	const [leftStickDetailData, setLeftStickDetailData] = useState({
		centerX: 0,
		centerY: 0,
		rawAdcX: 0,
		rawAdcY: 0,
		angleIndex: 0,
		scale: 0,
		offsetCenterX: 0,
		offsetCenterY: 0,
		scaledCenterX: 0,
		scaledCenterY: 0,
		currentDistance: 0,
		normalizedX: 0,
		normalizedY: 0,
	});
	const [rightStickDetailData, setRightStickDetailData] = useState({
		centerX: 0,
		centerY: 0,
		rawAdcX: 0,
		rawAdcY: 0,
		angleIndex: 0,
		scale: 0,
		offsetCenterX: 0,
		offsetCenterY: 0,
		scaledCenterX: 0,
		scaledCenterY: 0,
		currentDistance: 0,
		normalizedX: 0,
		normalizedY: 0,
	});

	// Fetch joystick data periodically and update canvas
	useEffect(() => {
		if (!values || !values.AnalogInputEnabled) {
			return;
		}

		const fetchJoystickData = async () => {
			try {
				// Fetch left stick (stick 1)
				if (values.analogAdc1PinX != null && values.analogAdc1PinX >= 0 && values.analogAdc1PinY != null && values.analogAdc1PinY >= 0) {
					const res1 = await fetch('/api/getJoystickCenter');
					if (res1.ok) {
						const data1 = await res1.json();
						if (data1.success) {
							const centerX = values.joystickCenterX || ADC_CENTER;
							const centerY = values.joystickCenterY || ADC_CENTER;
							const originalRangeData = values?.joystickRangeData1 || [];
							
							// Use ref values when finetune shape is active, otherwise use saved values
							const percentRef = leftFinetuneShapePercentRef.current;
							const forceCircular = leftFinetuneShapeActive 
								? percentRef.forceCircular 
								: (values?.joystickFinetuneShapeForceCircular1 ?? false);
							const amplify = leftFinetuneShapeActive 
								? percentRef.amplify 
								: (values?.joystickFinetuneShapeAmplify1 ?? 0.0);
							
							const adjustedRangeData = applyFinetuneShapeAdjustments(
								originalRangeData,
								forceCircular,
								amplify
							);
							
							// Apply jitter filter for visualization using configured threshold
							const jitterThreshold1 = values?.joystickJitterFilter1 ?? 0;
							const filtered1 = applyJitterFilterToAdc(
								data1.x,
								data1.y,
								jitterThreshold1,
								leftCanvasJitterLastRef
							);

							const { stickX: rawStickX, stickY: rawStickY, detailData } = processJoystickData(
								filtered1.x,
								filtered1.y,
								centerX,
								centerY,
								adjustedRangeData
							);
							
							// Apply invert settings (0=None, 1=X, 2=Y, 3=X/Y) - matches backend Step 3
							const invert1 = values?.analogAdc1Invert ?? 0;
							let stickX = (invert1 === 1 || invert1 === 3) ? -rawStickX : rawStickX;
							let stickY = (invert1 === 2 || invert1 === 3) ? -rawStickY : rawStickY;
							
							// Step 4: Apply deadzone and anti-deadzone (matches backend Step 4)
							const innerDeadzone = (values?.inner_deadzone || 0) / 100.0;
							const antiDeadzone = (values?.anti_deadzone || 0) / 100.0;
							const dist_sq = stickX * stickX + stickY * stickY;
							const deadzone_sq = innerDeadzone * innerDeadzone;
							let dist = 0.0;
							let scale_factor = 0.0;
							
							if (dist_sq < deadzone_sq) {
								// Inside deadzone: set to center (matches backend)
								stickX = 0.0;
								stickY = 0.0;
							} else if (antiDeadzone > 0.0) {
								// Anti-deadzone enabled: compute sqrt and apply if needed
								dist = Math.sqrt(dist_sq);
								const baseline = antiDeadzone;
								if (dist > 0.0 && dist < baseline) {
									scale_factor = baseline / dist;
									stickX = stickX * scale_factor;
									stickY = stickY * scale_factor;
									dist = dist * scale_factor; // Update dist for curve application
								} else {
									dist = dist; // Keep original dist
								}
							} else {
								// No anti-deadzone: dist remains 0, will compute in curve if needed
								dist = Math.sqrt(dist_sq);
							}
							
							// Step 5: Square trimming (matches backend Step 5)
							const nx_before = stickX;
							const ny_before = stickY;
							stickX = Math.max(-1.0, Math.min(1.0, stickX));
							stickY = Math.max(-1.0, Math.min(1.0, stickY));
							const coords_changed = (stickX !== nx_before) || (stickY !== ny_before);
							
							// Step 6: Apply response curve if configured (matches backend Step 6)
							if (leftCurvePoints.length > 0) {
								let magnitude_sq;
								let magnitude = -1.0;
								
								if (coords_changed) {
									// Square trimming changed coordinates: recalculate from stickX/stickY
									magnitude_sq = stickX * stickX + stickY * stickY;
								} else {
									// Square trimming didn't change coordinates: reuse dist
									if (antiDeadzone > 0.0 && scale_factor > 0.0) {
										magnitude_sq = dist_sq * scale_factor * scale_factor;
										magnitude = dist; // Already updated above
									} else if (antiDeadzone > 0.0) {
										magnitude_sq = dist_sq;
										magnitude = dist;
									} else {
										magnitude_sq = dist_sq;
										// magnitude remains -1.0, will be computed below
									}
								}
								
								if (magnitude_sq > 0.0) {
									if (magnitude < 0.0) {
										magnitude = Math.sqrt(magnitude_sq);
									}
									
									// Apply curve to magnitude
									const curvedMagnitude = applyResponseCurve(magnitude, leftCurvePoints);
									
									// Apply curve to output, preserving direction
									if (magnitude > 0) {
										const scale = curvedMagnitude / magnitude;
										stickX = stickX * scale;
										stickY = stickY * scale;
									}
								}
							}
							
							// Calculate progress ratio for curve visualization
							// rawDist = currentDistance - distance from center in ADC units (before range calibration scaling)
							// This is the raw distance before applying range calibration
							const rawDist = detailData.currentDistance;
							// l = scale * ADC_CENTER - outer calibration ADC value length for this direction
							// rangeData stores scale = distance / ADC_CENTER for each angle
							// So when stick reaches outer boundary: distance = scale * ADC_CENTER
							// Therefore: l = scale * ADC_CENTER (the maximum distance for this direction)
							// When scale = 1.0 (no calibration), l = ADC_CENTER
							// When scale > 1.0 (calibrated), l > ADC_CENTER (larger outer boundary)
							// When scale < 1.0 (calibrated), l < ADC_CENTER (smaller outer boundary)
							const l = detailData.scale > 0 ? detailData.scale * ADC_CENTER : ADC_CENTER;
							// progressRatio = rawDist / l (clamped to [0, 1])
							// This represents how far along the outer boundary the stick has reached
							// When rawDist = l, the stick has reached the outer boundary, progressRatio = 1
							const progressRatio = l > 0 ? Math.min(1.0, Math.max(0.0, rawDist / l)) : 0;
							
							setLeftStickData({
								x: stickX,
								y: stickY,
								rawX: filtered1.x,
								rawY: filtered1.y,
								progressRatio: progressRatio,
							});
							
							setLeftStickDetailData(detailData);
							
							// Collect circularity data when finetune shape is active
							if (leftFinetuneShapeActive) {
								const distance = Math.sqrt(stickX * stickX + stickY * stickY);
								const circAngleIndex = (Math.round(Math.atan2(stickY, stickX) * CIRCULARITY_DATA_SIZE / 2.0 / Math.PI) + CIRCULARITY_DATA_SIZE) % CIRCULARITY_DATA_SIZE;
								setLeftFinetuneShapeCircularityData(prev => {
									const newData = [...prev];
									if (distance > newData[circAngleIndex]) {
										newData[circAngleIndex] = distance;
									}
									return newData;
								});
							}
						}
					}
				}

				// Fetch right stick (stick 2)
				if (values.analogAdc2PinX != null && values.analogAdc2PinX >= 0 && values.analogAdc2PinY != null && values.analogAdc2PinY >= 0) {
					const res2 = await fetch('/api/getJoystickCenter2');
					if (res2.ok) {
						const data2 = await res2.json();
						if (data2.success) {
							const centerX = values.joystickCenterX2 || ADC_CENTER;
							const centerY = values.joystickCenterY2 || ADC_CENTER;
							const originalRangeData = values?.joystickRangeData2 || [];
							
							// Use ref values when finetune shape is active, otherwise use saved values
							const percentRef = rightFinetuneShapePercentRef.current;
							const forceCircular = rightFinetuneShapeActive 
								? percentRef.forceCircular 
								: (values?.joystickFinetuneShapeForceCircular2 ?? false);
							const amplify = rightFinetuneShapeActive 
								? percentRef.amplify 
								: (values?.joystickFinetuneShapeAmplify2 ?? 0.0);
							
							const adjustedRangeData = applyFinetuneShapeAdjustments(
								originalRangeData,
								forceCircular,
								amplify
							);
							
							// Apply jitter filter for visualization using configured threshold
							const jitterThreshold2 = values?.joystickJitterFilter2 ?? 0;
							const filtered2 = applyJitterFilterToAdc(
								data2.x,
								data2.y,
								jitterThreshold2,
								rightCanvasJitterLastRef
							);

							const { stickX: rawStickX, stickY: rawStickY, detailData } = processJoystickData(
								filtered2.x,
								filtered2.y,
								centerX,
								centerY,
								adjustedRangeData
							);
							
							// Apply invert settings (0=None, 1=X, 2=Y, 3=X/Y) - matches backend Step 3
							const invert2 = values?.analogAdc2Invert ?? 0;
							let stickX = (invert2 === 1 || invert2 === 3) ? -rawStickX : rawStickX;
							let stickY = (invert2 === 2 || invert2 === 3) ? -rawStickY : rawStickY;
							
							// Step 4: Apply deadzone and anti-deadzone (matches backend Step 4)
							const innerDeadzone = (values?.inner_deadzone2 || 0) / 100.0;
							const antiDeadzone = (values?.anti_deadzone2 || 0) / 100.0;
							const dist_sq = stickX * stickX + stickY * stickY;
							const deadzone_sq = innerDeadzone * innerDeadzone;
							let dist = 0.0;
							let scale_factor = 0.0;
							
							if (dist_sq < deadzone_sq) {
								// Inside deadzone: set to center (matches backend)
								stickX = 0.0;
								stickY = 0.0;
							} else if (antiDeadzone > 0.0) {
								// Anti-deadzone enabled: compute sqrt and apply if needed
								dist = Math.sqrt(dist_sq);
								const baseline = antiDeadzone;
								if (dist > 0.0 && dist < baseline) {
									scale_factor = baseline / dist;
									stickX = stickX * scale_factor;
									stickY = stickY * scale_factor;
									dist = dist * scale_factor; // Update dist for curve application
								} else {
									dist = dist; // Keep original dist
								}
							} else {
								// No anti-deadzone: dist remains 0, will compute in curve if needed
								dist = Math.sqrt(dist_sq);
							}
							
							// Step 5: Square trimming (matches backend Step 5)
							const nx_before = stickX;
							const ny_before = stickY;
							stickX = Math.max(-1.0, Math.min(1.0, stickX));
							stickY = Math.max(-1.0, Math.min(1.0, stickY));
							const coords_changed = (stickX !== nx_before) || (stickY !== ny_before);
							
							// Step 6: Apply response curve if configured (matches backend Step 6)
							if (rightCurvePoints.length > 0) {
								let magnitude_sq;
								let magnitude = -1.0;
								
								if (coords_changed) {
									// Square trimming changed coordinates: recalculate from stickX/stickY
									magnitude_sq = stickX * stickX + stickY * stickY;
								} else {
									// Square trimming didn't change coordinates: reuse dist
									if (antiDeadzone > 0.0 && scale_factor > 0.0) {
										magnitude_sq = dist_sq * scale_factor * scale_factor;
										magnitude = dist; // Already updated above
									} else if (antiDeadzone > 0.0) {
										magnitude_sq = dist_sq;
										magnitude = dist;
									} else {
										magnitude_sq = dist_sq;
										// magnitude remains -1.0, will be computed below
									}
								}
								
								if (magnitude_sq > 0.0) {
									if (magnitude < 0.0) {
										magnitude = Math.sqrt(magnitude_sq);
									}
									
									// Apply curve to magnitude
									const curvedMagnitude = applyResponseCurve(magnitude, rightCurvePoints);
									
									// Apply curve to output, preserving direction
									if (magnitude > 0) {
										const scale = curvedMagnitude / magnitude;
										stickX = stickX * scale;
										stickY = stickY * scale;
									}
								}
							}
							
							// Calculate progress ratio for curve visualization
							// rawDist = currentDistance - distance from center in ADC units (before range calibration scaling)
							// This is the raw distance before applying range calibration
							const rawDist = detailData.currentDistance;
							// l = scale * ADC_CENTER - outer calibration ADC value length for this direction
							// rangeData stores scale = distance / ADC_CENTER for each angle
							// So when stick reaches outer boundary: distance = scale * ADC_CENTER
							// Therefore: l = scale * ADC_CENTER (the maximum distance for this direction)
							// When scale = 1.0 (no calibration), l = ADC_CENTER
							// When scale > 1.0 (calibrated), l > ADC_CENTER (larger outer boundary)
							// When scale < 1.0 (calibrated), l < ADC_CENTER (smaller outer boundary)
							const l = detailData.scale > 0 ? detailData.scale * ADC_CENTER : ADC_CENTER;
							// progressRatio = rawDist / l (clamped to [0, 1])
							// This represents how far along the outer boundary the stick has reached
							// When rawDist = l, the stick has reached the outer boundary, progressRatio = 1
							const progressRatio = l > 0 ? Math.min(1.0, Math.max(0.0, rawDist / l)) : 0;
							
							setRightStickData({
								x: stickX,
								y: stickY,
								rawX: filtered2.x,
								rawY: filtered2.y,
								progressRatio: progressRatio,
							});
							
							setRightStickDetailData(detailData);
							
							// Collect circularity data when finetune shape is active
							if (rightFinetuneShapeActive) {
								const distance = Math.sqrt(stickX * stickX + stickY * stickY);
								const circAngleIndex = (Math.round(Math.atan2(stickY, stickX) * CIRCULARITY_DATA_SIZE / 2.0 / Math.PI) + CIRCULARITY_DATA_SIZE) % CIRCULARITY_DATA_SIZE;
								setRightFinetuneShapeCircularityData(prev => {
									const newData = [...prev];
									if (distance > newData[circAngleIndex]) {
										newData[circAngleIndex] = distance;
									}
									return newData;
								});
							}
						}
					}
				}
			} catch (error) {
				console.error('Failed to fetch joystick data:', error);
			}
		};

		// Update at ~30fps (every 33ms)
		const intervalId = setInterval(fetchJoystickData, 33);

		return () => {
			clearInterval(intervalId);
		};
	}, [values.AnalogInputEnabled, values.analogAdc1PinX, values.analogAdc1PinY, values.analogAdc2PinX, values.analogAdc2PinY, values.joystickCenterX, values.joystickCenterY, values.joystickCenterX2, values.joystickCenterY2, values.joystickRangeData1, values.joystickRangeData2, values?.joystickFinetuneShapeForceCircular1, values?.joystickFinetuneShapeAmplify1, values?.joystickFinetuneShapeForceCircular2, values?.joystickFinetuneShapeAmplify2, leftFinetuneShapeActive, rightFinetuneShapeActive, leftCurvePoints, rightCurvePoints, values?.inner_deadzone, values?.anti_deadzone, values?.inner_deadzone2, values?.anti_deadzone2]);

	// Update canvas when stick data changes
	useEffect(() => {
		let animationFrameId: number | null = null;

		const updateCanvas = () => {
			// Draw left stick
			if (leftStickCanvasRef.current) {
				const ctx = leftStickCanvasRef.current.getContext('2d');
				if (ctx) {
					const canvas = leftStickCanvasRef.current;
					const centerX = canvas.width / 2;
					const centerY = canvas.height / 2;
					const radius = Math.min(centerX, centerY) - 10;
					
					drawStickPosition(
						ctx,
						centerX,
						centerY,
						radius,
						leftStickData.x,
						leftStickData.y,
						leftFinetuneShapeActive ? leftFinetuneShapeCircularityData : null, // Show circularity when active
						leftFinetuneCenterActive,
					);
				}
			}

			// Draw right stick
			if (rightStickCanvasRef.current) {
				const ctx = rightStickCanvasRef.current.getContext('2d');
				if (ctx) {
					const canvas = rightStickCanvasRef.current;
					const centerX = canvas.width / 2;
					const centerY = canvas.height / 2;
					const radius = Math.min(centerX, centerY) - 10;
					
					drawStickPosition(
						ctx,
						centerX,
						centerY,
						radius,
						rightStickData.x,
						rightStickData.y,
						rightFinetuneShapeActive ? rightFinetuneShapeCircularityData : null, // Show circularity when active
						rightFinetuneCenterActive,
					);
				}
			}
		};

		// Use requestAnimationFrame to throttle canvas updates
		animationFrameId = requestAnimationFrame(updateCanvas);

		return () => {
			if (animationFrameId !== null) {
				cancelAnimationFrame(animationFrameId);
			}
		};
	}, [leftStickData, rightStickData, leftFinetuneCenterActive, rightFinetuneCenterActive, leftFinetuneShapeActive, rightFinetuneShapeActive, leftFinetuneShapeCircularityData, rightFinetuneShapeCircularityData]);

	// Sync input values with curve points when points change from other sources (e.g., canvas dragging, adding points)
	useEffect(() => {
		if (leftCurvePoints.length === leftCurveInputValues.length) {
			// Only update if there's a mismatch (excluding user editing)
			const needsUpdate = leftCurvePoints.some((p, i) => {
				const inputVal = leftCurveInputValues[i];
				return !inputVal || Math.abs(parseFloat(inputVal.x || '0') - p.x) > 0.0001 || Math.abs(parseFloat(inputVal.y || '0') - p.y) > 0.0001;
			});
			if (needsUpdate) {
				setLeftCurveInputValues(leftCurvePoints.map(p => ({ x: p.x.toString(), y: p.y.toString() })));
			}
		} else {
			// Length mismatch - full sync
			setLeftCurveInputValues(leftCurvePoints.map(p => ({ x: p.x.toString(), y: p.y.toString() })));
		}
	}, [leftCurvePoints]);

	useEffect(() => {
		if (rightCurvePoints.length === rightCurveInputValues.length) {
			// Only update if there's a mismatch (excluding user editing)
			const needsUpdate = rightCurvePoints.some((p, i) => {
				const inputVal = rightCurveInputValues[i];
				return !inputVal || Math.abs(parseFloat(inputVal.x || '0') - p.x) > 0.0001 || Math.abs(parseFloat(inputVal.y || '0') - p.y) > 0.0001;
			});
			if (needsUpdate) {
				setRightCurveInputValues(rightCurvePoints.map(p => ({ x: p.x.toString(), y: p.y.toString() })));
			}
		} else {
			// Length mismatch - full sync
			setRightCurveInputValues(rightCurvePoints.map(p => ({ x: p.x.toString(), y: p.y.toString() })));
		}
	}, [rightCurvePoints]);

	// Update curve editor canvas
	useEffect(() => {
		if (leftCurveActive && leftCurveCanvasRef.current) {
			const ctx = leftCurveCanvasRef.current.getContext('2d');
			if (ctx) {
				const innerDeadzone = (values?.inner_deadzone || 0) / 100.0; // Convert from percentage to 0-1
				const antiDeadzone = (values?.anti_deadzone || 0) / 100.0; // Convert from percentage to 0-1
				drawCurveEditor(ctx, 300, 300, leftCurvePoints, leftStickData.progressRatio, innerDeadzone, antiDeadzone);
			}
		}
	}, [leftCurveActive, leftCurvePoints, leftStickData.progressRatio, values?.inner_deadzone, values?.anti_deadzone]);

	useEffect(() => {
		if (rightCurveActive && rightCurveCanvasRef.current) {
			const ctx = rightCurveCanvasRef.current.getContext('2d');
			if (ctx) {
				const innerDeadzone = (values?.inner_deadzone2 || 0) / 100.0; // Convert from percentage to 0-1
				const antiDeadzone = (values?.anti_deadzone2 || 0) / 100.0; // Convert from percentage to 0-1
				drawCurveEditor(ctx, 300, 300, rightCurvePoints, rightStickData.progressRatio, innerDeadzone, antiDeadzone);
			}
		}
	}, [rightCurveActive, rightCurvePoints, rightStickData.progressRatio, values?.inner_deadzone2, values?.anti_deadzone2]);

	// Calculate statistics for samples (helper function)
	const calculateJitterStats = (samples: Array<{ x: number; y: number }>) => {
		if (samples.length === 0) return null;

		// Calculate mean
		const meanX = samples.reduce((sum, s) => sum + s.x, 0) / samples.length;
		const meanY = samples.reduce((sum, s) => sum + s.y, 0) / samples.length;

		// Calculate variance
		const varianceX = samples.reduce((sum, s) => sum + Math.pow(s.x - meanX, 2), 0) / samples.length;
		const varianceY = samples.reduce((sum, s) => sum + Math.pow(s.y - meanY, 2), 0) / samples.length;

		// Calculate standard deviation
		const stdDevX = Math.sqrt(varianceX);
		const stdDevY = Math.sqrt(varianceY);

		// Calculate mean absolute deviation
		const meanDeviationX = samples.reduce((sum, s) => sum + Math.abs(s.x - meanX), 0) / samples.length;
		const meanDeviationY = samples.reduce((sum, s) => sum + Math.abs(s.y - meanY), 0) / samples.length;

		// Calculate deviation rate (coefficient of variation)
		const deviationRateX = meanX !== 0 ? (stdDevX / meanX) * 100 : 0;
		const deviationRateY = meanY !== 0 ? (stdDevY / meanY) * 100 : 0;

		// Calculate upper and lower deviations
		const upperDeviationX = Math.max(...samples.map(s => s.x - meanX));
		const lowerDeviationX = Math.min(...samples.map(s => s.x - meanX));
		const upperDeviationY = Math.max(...samples.map(s => s.y - meanY));
		const lowerDeviationY = Math.min(...samples.map(s => s.y - meanY));

		return {
			meanX,
			meanY,
			varianceX,
			varianceY,
			meanDeviationX,
			meanDeviationY,
			deviationRateX,
			deviationRateY,
			upperDeviationX,
			upperDeviationY,
			lowerDeviationX,
			lowerDeviationY,
		};
	};

	// Start jitter sampling for stick 1
	const handleStartJitterSampling1 = () => {
		if (leftJitterSampling) return;
		// Clear any existing timeout
		if (leftJitterTimeoutRef.current) {
			clearTimeout(leftJitterTimeoutRef.current);
			leftJitterTimeoutRef.current = null;
		}
		setLeftJitterSampling(true);
		setLeftJitterSamples([]);
		leftJitterSamplingAbortRef.current = false;
		leftJitterLastSampleRef.current = null;
		
		const maxSamples = 30;
		
		const fetchData = async (): Promise<void> => {
			if (leftJitterSamplingAbortRef.current) {
				return;
			}

			try {
				const controller = new AbortController();
				const timeoutId = setTimeout(() => controller.abort(), 300);

				const res = await fetch('/api/getJoystickCenter', { signal: controller.signal });
				clearTimeout(timeoutId);
				if (res.ok) {
					const data = await res.json();
					if (data.success) {
						// Calculate difference from last sample
						if (leftJitterLastSampleRef.current !== null) {
							const deltaX = Math.abs(data.x - leftJitterLastSampleRef.current.x);
							const deltaY = Math.abs(data.y - leftJitterLastSampleRef.current.y);
							
							// Update last sample before state update
							leftJitterLastSampleRef.current = { x: data.x, y: data.y };
							
							setLeftJitterSamples(prev => {
								const newSamples = [...prev, { x: deltaX, y: deltaY }];
								if (newSamples.length >= maxSamples) {
									setLeftJitterSampling(false);
									// Calculate statistics automatically when sampling completes
									const stats = calculateJitterStats(newSamples);
									if (stats) {
										setLeftJitterStats(stats);
									}
									setShowLeftJitterDataModal(true);
									return newSamples;
								}
								// Continue sampling if not reached max samples
								if (!leftJitterSamplingAbortRef.current) {
									if (leftJitterTimeoutRef.current) clearTimeout(leftJitterTimeoutRef.current);
									leftJitterTimeoutRef.current = setTimeout(fetchData, 5);
								}
								return newSamples;
							});
						} else {
							// First sample: just store it, don't add to samples array
							leftJitterLastSampleRef.current = { x: data.x, y: data.y };
							// Continue sampling with slight delay
							if (!leftJitterSamplingAbortRef.current) {
								if (leftJitterTimeoutRef.current) clearTimeout(leftJitterTimeoutRef.current);
								leftJitterTimeoutRef.current = setTimeout(fetchData, 5);
							}
						}
					} else {
						if (!leftJitterSamplingAbortRef.current) {
							if (leftJitterTimeoutRef.current) clearTimeout(leftJitterTimeoutRef.current);
							leftJitterTimeoutRef.current = setTimeout(fetchData, 10);
						}
					}
				} else {
					if (!leftJitterSamplingAbortRef.current) {
						if (leftJitterTimeoutRef.current) clearTimeout(leftJitterTimeoutRef.current);
						leftJitterTimeoutRef.current = setTimeout(fetchData, 10);
					}
				}
			} catch (error) {
				console.error('Failed to fetch stick 1 jitter data:', error);
				if (!leftJitterSamplingAbortRef.current) {
					if (leftJitterTimeoutRef.current) clearTimeout(leftJitterTimeoutRef.current);
					leftJitterTimeoutRef.current = setTimeout(fetchData, 10);
				}
			}
		};

		fetchData();
	};

	// Start jitter sampling for stick 2
	const handleStartJitterSampling2 = () => {
		if (rightJitterSampling) return;
		// Clear any existing timeout
		if (rightJitterTimeoutRef.current) {
			clearTimeout(rightJitterTimeoutRef.current);
			rightJitterTimeoutRef.current = null;
		}
		setRightJitterSampling(true);
		setRightJitterSamples([]);
		rightJitterSamplingAbortRef.current = false;
		rightJitterLastSampleRef.current = null;
		
		const maxSamples = 30;
		
		const fetchData = async (): Promise<void> => {
			if (rightJitterSamplingAbortRef.current) {
				return;
			}

			try {
				const controller = new AbortController();
				const timeoutId = setTimeout(() => controller.abort(), 300);

				const res = await fetch('/api/getJoystickCenter2', { signal: controller.signal });
				clearTimeout(timeoutId);
				if (res.ok) {
					const data = await res.json();
					if (data.success) {
						// Calculate difference from last sample
						if (rightJitterLastSampleRef.current !== null) {
							const deltaX = Math.abs(data.x - rightJitterLastSampleRef.current.x);
							const deltaY = Math.abs(data.y - rightJitterLastSampleRef.current.y);
							
							// Update last sample before state update
							rightJitterLastSampleRef.current = { x: data.x, y: data.y };
							
							setRightJitterSamples(prev => {
								const newSamples = [...prev, { x: deltaX, y: deltaY }];
								if (newSamples.length >= maxSamples) {
									setRightJitterSampling(false);
									// Calculate statistics automatically when sampling completes
									const stats = calculateJitterStats(newSamples);
									if (stats) {
										setRightJitterStats(stats);
									}
									setShowRightJitterDataModal(true);
									return newSamples;
								}
								// Continue sampling if not reached max samples
								if (!rightJitterSamplingAbortRef.current) {
									if (rightJitterTimeoutRef.current) clearTimeout(rightJitterTimeoutRef.current);
									rightJitterTimeoutRef.current = setTimeout(fetchData, 5);
								}
								return newSamples;
							});
						} else {
							// First sample: just store it, don't add to samples array
							rightJitterLastSampleRef.current = { x: data.x, y: data.y };
							// Continue sampling with slight delay
							if (!rightJitterSamplingAbortRef.current) {
								if (rightJitterTimeoutRef.current) clearTimeout(rightJitterTimeoutRef.current);
								rightJitterTimeoutRef.current = setTimeout(fetchData, 5);
							}
						}
					} else {
						if (!rightJitterSamplingAbortRef.current) {
							if (rightJitterTimeoutRef.current) clearTimeout(rightJitterTimeoutRef.current);
							rightJitterTimeoutRef.current = setTimeout(fetchData, 10);
						}
					}
				} else {
					if (!rightJitterSamplingAbortRef.current) {
						if (rightJitterTimeoutRef.current) clearTimeout(rightJitterTimeoutRef.current);
						rightJitterTimeoutRef.current = setTimeout(fetchData, 10);
					}
				}
			} catch (error) {
				console.error('Failed to fetch stick 2 jitter data:', error);
				if (!rightJitterSamplingAbortRef.current) {
					if (rightJitterTimeoutRef.current) clearTimeout(rightJitterTimeoutRef.current);
					rightJitterTimeoutRef.current = setTimeout(fetchData, 10);
				}
			}
		};

		fetchData();
	};

	// Cleanup jitter sampling on unmount
	useEffect(() => {
		return () => {
			leftJitterSamplingAbortRef.current = true;
			rightJitterSamplingAbortRef.current = true;
			if (leftJitterTimeoutRef.current) {
				clearTimeout(leftJitterTimeoutRef.current);
				leftJitterTimeoutRef.current = null;
			}
			if (rightJitterTimeoutRef.current) {
				clearTimeout(rightJitterTimeoutRef.current);
				rightJitterTimeoutRef.current = null;
			}
		};
	}, []);

	// Cleanup timeouts when sampling stops
	useEffect(() => {
		if (!leftJitterSampling && leftJitterTimeoutRef.current) {
			clearTimeout(leftJitterTimeoutRef.current);
			leftJitterTimeoutRef.current = null;
		}
	}, [leftJitterSampling]);

	useEffect(() => {
		if (!rightJitterSampling && rightJitterTimeoutRef.current) {
			clearTimeout(rightJitterTimeoutRef.current);
			rightJitterTimeoutRef.current = null;
		}
	}, [rightJitterSampling]);

	// Reset finetune shape data when active state changes or values change
	useEffect(() => {
		if (leftFinetuneShapeActive) {
			setLeftFinetuneShapeCircularityData(new Array(CIRCULARITY_DATA_SIZE).fill(0));
		}
	}, [leftFinetuneShapeActive, leftFinetuneShapeForceCircular, leftFinetuneShapeAmplify]);

	useEffect(() => {
		if (rightFinetuneShapeActive) {
			setRightFinetuneShapeCircularityData(new Array(CIRCULARITY_DATA_SIZE).fill(0));
		}
	}, [rightFinetuneShapeActive, rightFinetuneShapeForceCircular, rightFinetuneShapeAmplify]);

	// Use refs to store latest values for real-time updates
	const leftFinetuneShapePercentRef = useRef({
		forceCircular: leftFinetuneShapeForceCircular,
		amplify: leftFinetuneShapeAmplify
	});
	const rightFinetuneShapePercentRef = useRef({
		forceCircular: rightFinetuneShapeForceCircular,
		amplify: rightFinetuneShapeAmplify
	});

	// Update refs when values change
	useEffect(() => {
		leftFinetuneShapePercentRef.current = {
			forceCircular: leftFinetuneShapeForceCircular,
			amplify: leftFinetuneShapeAmplify
		};
	}, [leftFinetuneShapeForceCircular, leftFinetuneShapeAmplify]);

	useEffect(() => {
		rightFinetuneShapePercentRef.current = {
			forceCircular: rightFinetuneShapeForceCircular,
			amplify: rightFinetuneShapeAmplify
		};
	}, [rightFinetuneShapeForceCircular, rightFinetuneShapeAmplify]);


	return (
		<Section title={t('AddonsConfig:joystick-calibration-header-text')}>
			<div id="JoystickCalibrationOptions" hidden={!values || !values.AnalogInputEnabled || values.AnalogInputEnabled === 0} style={{ overflowX: 'auto' }}>
				{/* 4 columns x 2 rows grid layout */}
				<div className="mb-3" style={{ display: 'grid', gridTemplateColumns: 'repeat(4, 300px)', gridTemplateRows: '310px auto auto', gap: '16px', justifyContent: 'center', alignItems: 'start', width: 'max-content', margin: '0 auto' }}>
					{/* Row 1, Column 1: Left stick canvas (position or curve) */}
					<div className="text-center" style={{ display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'flex-start', width: '300px' }}>
						<div style={{ position: 'relative', width: '300px', height: '300px' }}>
						<canvas
							ref={leftStickCanvasRef}
							width={300}
							height={300}
								style={{ 
									border: '1px solid #ccc', 
									borderRadius: '4px',
									display: leftCurveActive ? 'none' : 'block'
								}}
							/>
										<canvas
											ref={leftCurveCanvasRef}
								width={300}
								height={300}
								style={{ 
									border: '1px solid #ccc', 
									borderRadius: '4px', 
									cursor: 'crosshair',
									display: leftCurveActive ? 'block' : 'none',
									position: leftCurveActive ? 'absolute' : 'relative',
									top: leftCurveActive ? 0 : 'auto',
									left: leftCurveActive ? 0 : 'auto'
								}}
											onMouseDown={(e) => {
											if (!leftCurveCanvasRef.current) return;
											const rect = leftCurveCanvasRef.current.getBoundingClientRect();
											const x = (e.clientX - rect.left) / rect.width;
											const y = 1 - (e.clientY - rect.top) / rect.height; // Flip Y axis
											
											// Check if clicking on an existing point
											// Apply deadzone/anti-deadzone transformation to match displayed positions
											const innerDeadzone = (values?.inner_deadzone || 0) / 100.0;
											const antiDeadzone = (values?.anti_deadzone || 0) / 100.0;
											const applyDeadzones = (px: number, py: number) => {
												if (px < innerDeadzone) {
													return { x: -1, y: -1 }; // Invalid point
												} else {
													const remappedY = antiDeadzone + py * (1 - antiDeadzone);
													return { x: px, y: remappedY };
												}
											};
											const pointRadius = 6 / rect.width;
											for (let i = 0; i < leftCurvePoints.length; i++) {
												const transformedPoint = applyDeadzones(leftCurvePoints[i].x, leftCurvePoints[i].y);
												if (transformedPoint.x >= innerDeadzone && transformedPoint.x >= 0 && transformedPoint.y >= 0) {
													const px = transformedPoint.x;
													const py = transformedPoint.y;
													const dist = Math.sqrt((x - px) ** 2 + (y - py) ** 2);
													if (dist < pointRadius * 2) {
														setDraggingPointIndex({ stick: 'left', index: i });
														return;
													}
												}
											}
											
											// Check if clicking on the line to add a new point
											// Mouse position (x, y) is in displayed coordinates (with deadzone/anti-deadzone applied)
											// Need to check against the displayed curve, then reverse transform to get original coordinates
											if (leftCurvePoints.length < 3) {
												// Build the displayed curve points (with transformation applied)
												const sortedPoints = [...leftCurvePoints].sort((a, b) => a.x - b.x);
												const grayOriginPoint = { x: innerDeadzone, y: antiDeadzone };
												const curveStartPoint = (innerDeadzone > 0 || antiDeadzone > 0) ? grayOriginPoint : { x: 0, y: 0 };
												
												// Transform all points to displayed coordinates
												const displayedPoints: CurvePoint[] = [curveStartPoint];
												for (const pt of sortedPoints) {
													const transformed = applyDeadzones(pt.x, pt.y);
													if (transformed.x >= innerDeadzone && transformed.x >= 0 && transformed.y >= 0) {
														displayedPoints.push(transformed);
													}
												}
												const endPoint = applyDeadzones(1, 1);
												displayedPoints.push(endPoint);
												
												// Check if mouse is near any segment of the displayed curve
												for (let i = 0; i < displayedPoints.length - 1; i++) {
													const p1 = displayedPoints[i];
													const p2 = displayedPoints[i + 1];
													
													// Check if mouse X is within this segment's X range
													if (x >= p1.x && x <= p2.x) {
														// Calculate Y on the displayed line segment
														const t = p2.x !== p1.x ? (x - p1.x) / (p2.x - p1.x) : 0;
														const lineY = p1.y + t * (p2.y - p1.y);
														const dist = Math.abs(y - lineY);
														if (dist < 0.05) { // Within 5% of line
															// Reverse transform: convert displayed coordinates back to original coordinates
															let originalX = x;
															let originalY = y;
															
															// Clamp X to be at least deadzone
															if (originalX < innerDeadzone) {
																originalX = innerDeadzone;
															}
															
															// Reverse anti-deadzone transformation for Y
															if (antiDeadzone > 0 && antiDeadzone < 1) {
																// y = antiDeadzone + originalY * (1 - antiDeadzone)
																// originalY = (y - antiDeadzone) / (1 - antiDeadzone)
																originalY = (y - antiDeadzone) / (1 - antiDeadzone);
																// Clamp to [0, 1]
																originalY = Math.max(0, Math.min(1, originalY));
															}
															
															const newPoint = { x: originalX, y: originalY };
															// Validate monotonicity before adding
															const validatedPoint = validatePointMonotonicity(newPoint, leftCurvePoints);
															const updatedPoints = [...leftCurvePoints, validatedPoint];
															// Re-validate all points to ensure monotonicity
															const finalPoints = validateAllPointsMonotonicity(updatedPoints);
															setLeftCurvePoints(finalPoints);
													setLeftCurveInputValues(finalPoints.map(p => ({ x: p.x.toString(), y: p.y.toString() })));
															// Redraw immediately
															const ctx = leftCurveCanvasRef.current.getContext('2d');
															if (ctx) {
														drawCurveEditor(ctx, 300, 300, finalPoints, undefined, innerDeadzone, antiDeadzone);
															}
															return;
														}
													}
												}
											}
										}}
											onMouseMove={(e) => {
												if (draggingPointIndex?.stick === 'left' && leftCurveCanvasRef.current) {
													const rect = leftCurveCanvasRef.current.getBoundingClientRect();
													// Mouse position in canvas coordinates [0, 1]
													const mouseX = Math.max(0, Math.min(1, (e.clientX - rect.left) / rect.width));
													const mouseY = Math.max(0, Math.min(1, 1 - (e.clientY - rect.top) / rect.height));
													
													// Reverse transform: from displayed position back to original point coordinates
													const innerDeadzone = (values?.inner_deadzone || 0) / 100.0;
													const antiDeadzone = (values?.anti_deadzone || 0) / 100.0;
													
													// Reverse the deadzone/anti-deadzone transformation
													// Displayed: x = originalX (if x >= deadzone), y = antiDeadzone + originalY * (1 - antiDeadzone)
													// Reverse: originalX = x (if x >= deadzone), originalY = (y - antiDeadzone) / (1 - antiDeadzone)
													let originalX = mouseX;
													let originalY = mouseY;
													
													// Clamp X to be at least deadzone
													if (originalX < innerDeadzone) {
														originalX = innerDeadzone;
													}
													
													// Reverse anti-deadzone transformation for Y
													if (antiDeadzone > 0 && antiDeadzone < 1) {
														// y = antiDeadzone + originalY * (1 - antiDeadzone)
														// originalY = (y - antiDeadzone) / (1 - antiDeadzone)
														originalY = (mouseY - antiDeadzone) / (1 - antiDeadzone);
														// Clamp to [0, 1]
														originalY = Math.max(0, Math.min(1, originalY));
													}
													
													const newPoints = [...leftCurvePoints];
													const updatedPoint = { x: originalX, y: originalY };
													// Validate monotonicity for the dragged point
													const otherPoints = newPoints.filter((_, i) => i !== draggingPointIndex.index);
													const validatedPoint = validatePointMonotonicity(updatedPoint, otherPoints);
													newPoints[draggingPointIndex.index] = validatedPoint;
													// Re-validate all points to ensure monotonicity
													const finalPoints = validateAllPointsMonotonicity(newPoints);
													setLeftCurvePoints(finalPoints);
										// Update input values to match (use final validated points)
										setLeftCurveInputValues(finalPoints.map(p => ({ x: p.x.toString(), y: p.y.toString() })));
													// Redraw immediately
													const ctx = leftCurveCanvasRef.current.getContext('2d');
													if (ctx) {
											drawCurveEditor(ctx, 300, 300, finalPoints, undefined, innerDeadzone, antiDeadzone);
													}
												}
											}}
											onMouseUp={() => {
												setDraggingPointIndex(null);
											}}
											onMouseLeave={() => {
												setDraggingPointIndex(null);
											}}
										/>
									</div>
									{/* Display current stick physical and output distance */}
									{leftCurveActive && (() => {
										const physicalDistance = leftStickData.progressRatio !== undefined ? leftStickData.progressRatio : 0;
										// Output distance is the Y value on the curve at the position where X = physicalDistance
										// This matches the highlight line position on the curve canvas
										const innerDeadzone = (values?.inner_deadzone || 0) / 100.0;
										const antiDeadzone = (values?.anti_deadzone || 0) / 100.0;
										const outputDistance = calculateOutputDistanceFromCurve(
											physicalDistance,
											leftCurvePoints,
											innerDeadzone,
											antiDeadzone
										);
										
										return (
											<div style={{ marginTop: '8px', fontSize: '0.875rem', textAlign: 'center' }}>
												<div>当前摇杆物理距离：{(physicalDistance * 100).toFixed(1)}%</div>
												<div>当前摇杆输出距离：{(outputDistance * 100).toFixed(1)}%</div>
											</div>
										);
									})()}
									</div>

					{/* Row 1, Column 2: Left finetune shape controls or left curve info box */}
					<div style={{ width: '300px', display: 'flex', justifyContent: 'center', alignItems: 'flex-start' }}>
						{leftFinetuneShapeActive && !leftCurveActive && (
							<div style={{ width: '300px', textAlign: 'left', border: '1px solid #dee2e6', borderRadius: '4px', padding: '8px' }}>
								<div style={{ fontWeight: 'bold', marginBottom: '8px', textAlign: 'center' }}>左摇杆外圈调教</div>
								<div className="p-2">
									<div className="mb-3">
										<FormCheck
											type="switch"
											id="leftFinetuneShapeForceCircular"
											label="强制圆形"
											checked={leftFinetuneShapeForceCircular}
											onChange={(e) => setLeftFinetuneShapeForceCircular(e.target.checked)}
										/>
										<p className="text-muted small mt-1 mb-0">
											{leftFinetuneShapeForceCircular
												? "强制圆形会将摇杆外圈移动半径严格归一到圆形。"
												: "关闭强制圆形时将产生反映摇杆真实形状的外圈与误差率。"}
										</p>
									</div>
									<div className="mb-2">
										<Form.Label className="mb-1">外圈放大系数: {leftFinetuneShapeAmplify.toFixed(1)}%</Form.Label>
										<Form.Range
											min={-20}
											max={20}
											step={0.1}
											value={leftFinetuneShapeAmplify}
											onChange={(e) => setLeftFinetuneShapeAmplify(parseFloat(e.target.value))}
										/>
										<p className="text-muted small mt-1 mb-0">
											扩大系数可以放大摇杆覆盖范围，加快移动响应速度。
										</p>
									</div>
									<div className="mt-3 text-end">
										<Button
											variant="danger"
											size="sm"
											onClick={() => {
												setFieldValue('joystickFinetuneShapeForceCircular1', leftFinetuneShapeForceCircular);
												setFieldValue('joystickFinetuneShapeAmplify1', leftFinetuneShapeAmplify);
												setLeftFinetuneShapeActive(false);
											}}
										>
											确定
										</Button>
									</div>
								</div>
							</div>
						)}
						{leftCurveActive && (
							<div style={{ width: '300px', textAlign: 'left' }}>
								<div style={{ fontWeight: 'bold', marginBottom: '8px', textAlign: 'center', border: '1px solid #dee2e6', borderRadius: '4px', padding: '8px', height: '300px', display: 'flex', flexDirection: 'column' }}>
									<div style={{ marginBottom: '6px', fontSize: '0.95rem' }}>左摇杆曲线设置</div>
									<div style={{ marginBottom: '6px' }}>
										<Form.Label className="mb-0" style={{ textAlign: 'left', display: 'block', width: '100%', fontSize: '0.875rem', marginBottom: '4px' }}>内部死区: {(values?.inner_deadzone || 0).toFixed(1)}%</Form.Label>
										<Form.Range
											min={0}
											max={15}
											step={0.1}
											value={values?.inner_deadzone || 0}
											onChange={(e) => setFieldValue('inner_deadzone', Math.round(parseFloat(e.target.value)))}
										/>
									</div>
									<div style={{ marginBottom: '6px' }}>
										<Form.Label className="mb-0" style={{ textAlign: 'left', display: 'block', width: '100%', fontSize: '0.875rem', marginBottom: '4px' }}>反死区: {(values?.anti_deadzone || 0).toFixed(1)}%</Form.Label>
										<Form.Range
											min={0}
											max={15}
											step={0.1}
											value={values?.anti_deadzone || 0}
											onChange={(e) => setFieldValue('anti_deadzone', Math.round(parseFloat(e.target.value)))}
										/>
									</div>
									<div style={{ flex: 1, overflowY: 'auto' }}>
										<div style={{ fontWeight: 'bold', marginBottom: '4px', textAlign: 'left', fontSize: '0.875rem' }}>控制点</div>
										{leftCurvePoints.length > 0 ? (
											<div style={{ fontSize: '0.875rem' }}>
												{leftCurvePoints.map((point, originalIndex) => ({ point, originalIndex }))
													.sort((a, b) => a.point.x - b.point.x)
													.map(({ point, originalIndex }) => {
														const inputValue = leftCurveInputValues[originalIndex] || { x: point.x.toString(), y: point.y.toString() };
														return (
															<div key={originalIndex} style={{ marginBottom: '4px', display: 'flex', gap: '6px', alignItems: 'center' }}>
																<span style={{ minWidth: '16px', fontSize: '0.8rem' }}>X:</span>
																<Form.Control
																	type="number"
																	size="sm"
																	min={0}
																	max={1}
																	step={0.001}
																	value={inputValue.x}
																	style={{ width: '75px', fontSize: '0.8rem', padding: '2px 6px' }}
																	onKeyDown={(e) => {
																		// Prevent form submission on Enter key
																		if (e.key === 'Enter') {
																			e.preventDefault();
																			e.currentTarget.blur(); // Trigger onBlur to save value
																		}
																	}}
																	onChange={(e) => {
																		// Only update input display value, don't update actual state
																		const newInputValues = [...leftCurveInputValues];
																		if (originalIndex >= 0 && originalIndex < newInputValues.length) {
																			newInputValues[originalIndex] = { ...newInputValues[originalIndex], x: e.target.value };
																			setLeftCurveInputValues(newInputValues);
																		}
																	}}
																	onBlur={(e) => {
																		// Only update actual state on blur
																		const inputValue = e.target.value;
																		const numValue = parseFloat(inputValue);
																		if (!isNaN(numValue) && isFinite(numValue)) {
																			const newX = Math.max(0, Math.min(1, numValue));
																			const newPoints = [...leftCurvePoints];
																			if (originalIndex >= 0 && originalIndex < newPoints.length) {
																				newPoints[originalIndex] = { ...newPoints[originalIndex], x: newX };
																				// Re-validate all points to ensure monotonicity
																				const finalPoints = validateAllPointsMonotonicity(newPoints);
																				setLeftCurvePoints(finalPoints);
																				// Update input value to match
																				setLeftCurveInputValues(finalPoints.map(p => ({ x: p.x.toString(), y: p.y.toString() })));
																				// Redraw canvas
												if (leftCurveCanvasRef.current) {
													const ctx = leftCurveCanvasRef.current.getContext('2d');
													if (ctx) {
																						const innerDeadzone = (values?.inner_deadzone || 0) / 100.0;
																						const antiDeadzone = (values?.anti_deadzone || 0) / 100.0;
																						drawCurveEditor(ctx, 300, 300, finalPoints, undefined, innerDeadzone, antiDeadzone);
																					}
																				}
																			}
																		} else {
																			// Restore original value if invalid
																			const newInputValues = [...leftCurveInputValues];
																			newInputValues[originalIndex] = { ...newInputValues[originalIndex], x: point.x.toString() };
																			setLeftCurveInputValues(newInputValues);
																		}
																	}}
																/>
																<span style={{ minWidth: '16px', fontSize: '0.8rem' }}>Y:</span>
																<Form.Control
																	type="number"
											size="sm"
																	min={0}
																	max={1}
																	step={0.001}
																	value={inputValue.y}
																	style={{ width: '75px', fontSize: '0.8rem', padding: '2px 6px' }}
																	onKeyDown={(e) => {
																		// Prevent form submission on Enter key
																		if (e.key === 'Enter') {
																			e.preventDefault();
																			e.currentTarget.blur(); // Trigger onBlur to save value
																		}
																	}}
																	onChange={(e) => {
																		// Only update input display value, don't update actual state
																		const newInputValues = [...leftCurveInputValues];
																		if (originalIndex >= 0 && originalIndex < newInputValues.length) {
																			newInputValues[originalIndex] = { ...newInputValues[originalIndex], y: e.target.value };
																			setLeftCurveInputValues(newInputValues);
																		}
																	}}
																	onBlur={(e) => {
																		// Only update actual state on blur
																		const inputValue = e.target.value;
																		const numValue = parseFloat(inputValue);
																		if (!isNaN(numValue) && isFinite(numValue)) {
																			const newY = Math.max(0, Math.min(1, numValue));
													const newPoints = [...leftCurvePoints];
																			if (originalIndex >= 0 && originalIndex < newPoints.length) {
																				newPoints[originalIndex] = { ...newPoints[originalIndex], y: newY };
													setLeftCurvePoints(newPoints);
																				// Update input value to match
																				const newInputValues = [...leftCurveInputValues];
																				newInputValues[originalIndex] = { ...newInputValues[originalIndex], y: newY.toString() };
																				setLeftCurveInputValues(newInputValues);
																				// Redraw canvas
																				if (leftCurveCanvasRef.current) {
													const ctx = leftCurveCanvasRef.current.getContext('2d');
													if (ctx) {
																						drawCurveEditor(ctx, 300, 300, newPoints);
																					}
																				}
																			}
																		} else {
																			// Restore original value if invalid
																			const newInputValues = [...leftCurveInputValues];
																			newInputValues[originalIndex] = { ...newInputValues[originalIndex], y: point.y.toString() };
																			setLeftCurveInputValues(newInputValues);
																		}
																	}}
										/>
									</div>
														);
													})}
									</div>
										) : (
											<div style={{ fontSize: '0.875rem', color: '#6c757d' }}>暂无控制点</div>
										)}
									</div>
								</div>
							</div>
						)}
					</div>

					{/* Row 1, Column 3: Right finetune shape controls or right curve info box */}
					<div style={{ width: '300px', display: 'flex', justifyContent: 'center', alignItems: 'flex-start' }}>
						{rightFinetuneShapeActive && !rightCurveActive && (
							<div style={{ width: '300px', textAlign: 'left', border: '1px solid #dee2e6', borderRadius: '4px', padding: '8px' }}>
								<div style={{ fontWeight: 'bold', marginBottom: '8px', textAlign: 'center' }}>右摇杆外圈调教</div>
								<div className="p-2">
									<div className="mb-3">
										<FormCheck
											type="switch"
											id="rightFinetuneShapeForceCircular"
											label="强制圆形"
											checked={rightFinetuneShapeForceCircular}
											onChange={(e) => setRightFinetuneShapeForceCircular(e.target.checked)}
										/>
										<p className="text-muted small mt-1 mb-0">
											{rightFinetuneShapeForceCircular
												? "强制圆形会将摇杆外圈移动半径严格归一到圆形。"
												: "关闭强制圆形时将产生反映摇杆真实形状的外圈与误差率。"}
										</p>
									</div>
									<div className="mb-2">
										<Form.Label className="mb-1">外圈放大系数: {rightFinetuneShapeAmplify.toFixed(1)}%</Form.Label>
										<Form.Range
											min={-20}
											max={20}
											step={0.1}
											value={rightFinetuneShapeAmplify}
											onChange={(e) => setRightFinetuneShapeAmplify(parseFloat(e.target.value))}
										/>
										<p className="text-muted small mt-1 mb-0">
											扩大系数可以放大摇杆覆盖范围，加快移动响应速度。
										</p>
									</div>
									<div className="mt-3 text-end">
										<Button
											variant="danger"
											size="sm"
											onClick={() => {
												setFieldValue('joystickFinetuneShapeForceCircular2', rightFinetuneShapeForceCircular);
												setFieldValue('joystickFinetuneShapeAmplify2', rightFinetuneShapeAmplify);
												setRightFinetuneShapeActive(false);
											}}
										>
											确定
										</Button>
									</div>
								</div>
							</div>
						)}
						{rightCurveActive && (
							<div style={{ width: '300px', textAlign: 'left' }}>
								<div style={{ fontWeight: 'bold', marginBottom: '8px', textAlign: 'center', border: '1px solid #dee2e6', borderRadius: '4px', padding: '8px', height: '300px', display: 'flex', flexDirection: 'column' }}>
									<div style={{ marginBottom: '6px', fontSize: '0.95rem' }}>右摇杆曲线设置</div>
									<div style={{ marginBottom: '6px' }}>
										<Form.Label className="mb-0" style={{ textAlign: 'left', display: 'block', width: '100%', fontSize: '0.875rem', marginBottom: '4px' }}>内部死区: {(values?.inner_deadzone2 || 0).toFixed(1)}%</Form.Label>
										<Form.Range
											min={0}
											max={15}
											step={0.1}
											value={values?.inner_deadzone2 || 0}
											onChange={(e) => setFieldValue('inner_deadzone2', Math.round(parseFloat(e.target.value)))}
										/>
									</div>
									<div style={{ marginBottom: '6px' }}>
										<Form.Label className="mb-0" style={{ textAlign: 'left', display: 'block', width: '100%', fontSize: '0.875rem', marginBottom: '4px' }}>反死区: {(values?.anti_deadzone2 || 0).toFixed(1)}%</Form.Label>
										<Form.Range
											min={0}
											max={15}
											step={0.1}
											value={values?.anti_deadzone2 || 0}
											onChange={(e) => setFieldValue('anti_deadzone2', Math.round(parseFloat(e.target.value)))}
										/>
									</div>
									<div style={{ flex: 1, overflowY: 'auto' }}>
										<div style={{ fontWeight: 'bold', marginBottom: '4px', textAlign: 'left', fontSize: '0.875rem' }}>控制点</div>
										{rightCurvePoints.length > 0 ? (
											<div style={{ fontSize: '0.875rem' }}>
												{rightCurvePoints.map((point, originalIndex) => ({ point, originalIndex }))
													.sort((a, b) => a.point.x - b.point.x)
													.map(({ point, originalIndex }) => {
														const inputValue = rightCurveInputValues[originalIndex] || { x: point.x.toString(), y: point.y.toString() };
														return (
															<div key={originalIndex} style={{ marginBottom: '4px', display: 'flex', gap: '6px', alignItems: 'center' }}>
																<span style={{ minWidth: '16px', fontSize: '0.8rem' }}>X:</span>
																<Form.Control
																	type="number"
																	size="sm"
																	min={0}
																	max={1}
																	step={0.001}
																	value={inputValue.x}
																	style={{ width: '75px', fontSize: '0.8rem', padding: '2px 6px' }}
																	onKeyDown={(e) => {
																		// Prevent form submission on Enter key
																		if (e.key === 'Enter') {
																			e.preventDefault();
																			e.currentTarget.blur(); // Trigger onBlur to save value
																		}
																	}}
																	onChange={(e) => {
																		// Only update input display value, don't update actual state
																		const newInputValues = [...rightCurveInputValues];
																		if (originalIndex >= 0 && originalIndex < newInputValues.length) {
																			newInputValues[originalIndex] = { ...newInputValues[originalIndex], x: e.target.value };
																			setRightCurveInputValues(newInputValues);
																		}
																	}}
																	onBlur={(e) => {
																		// Only update actual state on blur
																		const inputValue = e.target.value;
																		const numValue = parseFloat(inputValue);
																		if (!isNaN(numValue) && isFinite(numValue)) {
																				const newX = Math.max(0, Math.min(1, numValue));
																				const newPoints = [...rightCurvePoints];
																				if (originalIndex >= 0 && originalIndex < newPoints.length) {
																					newPoints[originalIndex] = { ...newPoints[originalIndex], x: newX };
																				// Re-validate all points to ensure monotonicity
																				const finalPoints = validateAllPointsMonotonicity(newPoints);
																				setRightCurvePoints(finalPoints);
																				// Update input value to match
																				setRightCurveInputValues(finalPoints.map(p => ({ x: p.x.toString(), y: p.y.toString() })));
																				// Redraw canvas
																				if (rightCurveCanvasRef.current) {
																					const ctx = rightCurveCanvasRef.current.getContext('2d');
																					if (ctx) {
																						const innerDeadzone = (values?.inner_deadzone2 || 0) / 100.0;
																						const antiDeadzone = (values?.anti_deadzone2 || 0) / 100.0;
																						drawCurveEditor(ctx, 300, 300, finalPoints, undefined, innerDeadzone, antiDeadzone);
																					}
																				}
																			}
																		} else {
																			// Restore original value if invalid
																			const newInputValues = [...rightCurveInputValues];
																			newInputValues[originalIndex] = { ...newInputValues[originalIndex], x: point.x.toString() };
																			setRightCurveInputValues(newInputValues);
																		}
																	}}
																/>
																<span style={{ minWidth: '16px', fontSize: '0.8rem' }}>Y:</span>
																<Form.Control
																	type="number"
																	size="sm"
																	min={0}
																	max={1}
																	step={0.001}
																	value={inputValue.y}
																	style={{ width: '75px', fontSize: '0.8rem', padding: '2px 6px' }}
																	onKeyDown={(e) => {
																		// Prevent form submission on Enter key
																		if (e.key === 'Enter') {
																			e.preventDefault();
																			e.currentTarget.blur(); // Trigger onBlur to save value
																		}
																	}}
																	onChange={(e) => {
																		// Only update input display value, don't update actual state
																		const newInputValues = [...rightCurveInputValues];
																		if (originalIndex >= 0 && originalIndex < newInputValues.length) {
																			newInputValues[originalIndex] = { ...newInputValues[originalIndex], y: e.target.value };
																			setRightCurveInputValues(newInputValues);
																		}
																	}}
																	onBlur={(e) => {
																		// Only update actual state on blur
																		const inputValue = e.target.value;
																		const numValue = parseFloat(inputValue);
																		if (!isNaN(numValue) && isFinite(numValue)) {
																				const newY = Math.max(0, Math.min(1, numValue));
																				const newPoints = [...rightCurvePoints];
																				if (originalIndex >= 0 && originalIndex < newPoints.length) {
																					const updatedPoint = { ...newPoints[originalIndex], y: newY };
																				// Validate monotonicity for this point
																				const otherPoints = newPoints.filter((_, i) => i !== originalIndex);
																				const validatedPoint = validatePointMonotonicity(updatedPoint, otherPoints);
																				newPoints[originalIndex] = validatedPoint;
																				// Re-validate all points to ensure monotonicity
																				const finalPoints = validateAllPointsMonotonicity(newPoints);
																				setRightCurvePoints(finalPoints);
																				// Update input value to match
																				setRightCurveInputValues(finalPoints.map(p => ({ x: p.x.toString(), y: p.y.toString() })));
																				// Redraw canvas
																				if (rightCurveCanvasRef.current) {
																					const ctx = rightCurveCanvasRef.current.getContext('2d');
																					if (ctx) {
																						const innerDeadzone = (values?.inner_deadzone2 || 0) / 100.0;
																						const antiDeadzone = (values?.anti_deadzone2 || 0) / 100.0;
																						drawCurveEditor(ctx, 300, 300, finalPoints, undefined, innerDeadzone, antiDeadzone);
																					}
																				}
																			}
																		} else {
																			// Restore original value if invalid
																			const newInputValues = [...rightCurveInputValues];
																			newInputValues[originalIndex] = { ...newInputValues[originalIndex], y: point.y.toString() };
																			setRightCurveInputValues(newInputValues);
																		}
																	}}
																/>
															</div>
														);
													})}
											</div>
										) : (
											<div style={{ fontSize: '0.875rem', color: '#6c757d' }}>暂无控制点</div>
										)}
									</div>
								</div>
							</div>
						)}
					</div>

					{/* Row 1, Column 4: Right stick canvas (position or curve) */}
					<div className="text-center" style={{ display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'flex-start', width: '300px' }}>
						<div style={{ position: 'relative', width: '300px', height: '300px' }}>
							<canvas
								ref={rightStickCanvasRef}
								width={300}
								height={300}
								style={{ 
									border: '1px solid #ccc', 
									borderRadius: '4px',
									display: rightCurveActive ? 'none' : 'block'
								}}
							/>
										<canvas
											ref={rightCurveCanvasRef}
								width={300}
								height={300}
								style={{ 
									border: '1px solid #ccc', 
									borderRadius: '4px', 
									cursor: 'crosshair',
									display: rightCurveActive ? 'block' : 'none',
									position: rightCurveActive ? 'absolute' : 'relative',
									top: rightCurveActive ? 0 : 'auto',
									left: rightCurveActive ? 0 : 'auto'
								}}
											onMouseDown={(e) => {
											if (!rightCurveCanvasRef.current) return;
											const rect = rightCurveCanvasRef.current.getBoundingClientRect();
											const x = (e.clientX - rect.left) / rect.width;
											const y = 1 - (e.clientY - rect.top) / rect.height; // Flip Y axis
											
											// Check if clicking on an existing point
											// Apply deadzone/anti-deadzone transformation to match displayed positions
											const innerDeadzone = (values?.inner_deadzone2 || 0) / 100.0;
											const antiDeadzone = (values?.anti_deadzone2 || 0) / 100.0;
											const applyDeadzones = (px: number, py: number) => {
												if (px < innerDeadzone) {
													return { x: -1, y: -1 }; // Invalid point
												} else {
													const remappedY = antiDeadzone + py * (1 - antiDeadzone);
													return { x: px, y: remappedY };
												}
											};
											const pointRadius = 6 / rect.width;
											for (let i = 0; i < rightCurvePoints.length; i++) {
												const transformedPoint = applyDeadzones(rightCurvePoints[i].x, rightCurvePoints[i].y);
												if (transformedPoint.x >= innerDeadzone && transformedPoint.x >= 0 && transformedPoint.y >= 0) {
													const px = transformedPoint.x;
													const py = transformedPoint.y;
													const dist = Math.sqrt((x - px) ** 2 + (y - py) ** 2);
													if (dist < pointRadius * 2) {
														setDraggingPointIndex({ stick: 'right', index: i });
														return;
													}
												}
											}
											
											// Check if clicking on the line to add a new point
											// Mouse position (x, y) is in displayed coordinates (with deadzone/anti-deadzone applied)
											// Need to check against the displayed curve, then reverse transform to get original coordinates
											if (rightCurvePoints.length < 3) {
												// Build the displayed curve points (with transformation applied)
												const sortedPoints = [...rightCurvePoints].sort((a, b) => a.x - b.x);
												const grayOriginPoint = { x: innerDeadzone, y: antiDeadzone };
												const curveStartPoint = (innerDeadzone > 0 || antiDeadzone > 0) ? grayOriginPoint : { x: 0, y: 0 };
												
												// Transform all points to displayed coordinates
												const displayedPoints: CurvePoint[] = [curveStartPoint];
												for (const pt of sortedPoints) {
													const transformed = applyDeadzones(pt.x, pt.y);
													if (transformed.x >= innerDeadzone && transformed.x >= 0 && transformed.y >= 0) {
														displayedPoints.push(transformed);
													}
												}
												const endPoint = applyDeadzones(1, 1);
												displayedPoints.push(endPoint);
												
												// Check if mouse is near any segment of the displayed curve
												for (let i = 0; i < displayedPoints.length - 1; i++) {
													const p1 = displayedPoints[i];
													const p2 = displayedPoints[i + 1];
													
													// Check if mouse X is within this segment's X range
													if (x >= p1.x && x <= p2.x) {
														// Calculate Y on the displayed line segment
														const t = p2.x !== p1.x ? (x - p1.x) / (p2.x - p1.x) : 0;
														const lineY = p1.y + t * (p2.y - p1.y);
														const dist = Math.abs(y - lineY);
														if (dist < 0.05) { // Within 5% of line
															// Reverse transform: convert displayed coordinates back to original coordinates
															let originalX = x;
															let originalY = y;
															
															// Clamp X to be at least deadzone
															if (originalX < innerDeadzone) {
																originalX = innerDeadzone;
															}
															
															// Reverse anti-deadzone transformation for Y
															if (antiDeadzone > 0 && antiDeadzone < 1) {
																// y = antiDeadzone + originalY * (1 - antiDeadzone)
																// originalY = (y - antiDeadzone) / (1 - antiDeadzone)
																originalY = (y - antiDeadzone) / (1 - antiDeadzone);
																// Clamp to [0, 1]
																originalY = Math.max(0, Math.min(1, originalY));
															}
															
															const newPoint = { x: originalX, y: originalY };
															// Validate monotonicity before adding
															const validatedPoint = validatePointMonotonicity(newPoint, rightCurvePoints);
															const updatedPoints = [...rightCurvePoints, validatedPoint];
															// Re-validate all points to ensure monotonicity
															const finalPoints = validateAllPointsMonotonicity(updatedPoints);
															setRightCurvePoints(finalPoints);
													setRightCurveInputValues(finalPoints.map(p => ({ x: p.x.toString(), y: p.y.toString() })));
															// Redraw immediately
															const ctx = rightCurveCanvasRef.current.getContext('2d');
															if (ctx) {
														drawCurveEditor(ctx, 300, 300, finalPoints, undefined, innerDeadzone, antiDeadzone);
															}
															return;
														}
													}
												}
											}
										}}
											onMouseMove={(e) => {
												if (draggingPointIndex?.stick === 'right' && rightCurveCanvasRef.current) {
													const rect = rightCurveCanvasRef.current.getBoundingClientRect();
													// Mouse position in canvas coordinates [0, 1]
													const mouseX = Math.max(0, Math.min(1, (e.clientX - rect.left) / rect.width));
													const mouseY = Math.max(0, Math.min(1, 1 - (e.clientY - rect.top) / rect.height));
													
													// Reverse transform: from displayed position back to original point coordinates
													const innerDeadzone = (values?.inner_deadzone2 || 0) / 100.0;
													const antiDeadzone = (values?.anti_deadzone2 || 0) / 100.0;
													
													// Reverse the deadzone/anti-deadzone transformation
													// Displayed: x = originalX (if x >= deadzone), y = antiDeadzone + originalY * (1 - antiDeadzone)
													// Reverse: originalX = x (if x >= deadzone), originalY = (y - antiDeadzone) / (1 - antiDeadzone)
													let originalX = mouseX;
													let originalY = mouseY;
													
													// Clamp X to be at least deadzone
													if (originalX < innerDeadzone) {
														originalX = innerDeadzone;
													}
													
													// Reverse anti-deadzone transformation for Y
													if (antiDeadzone > 0 && antiDeadzone < 1) {
														// y = antiDeadzone + originalY * (1 - antiDeadzone)
														// originalY = (y - antiDeadzone) / (1 - antiDeadzone)
														originalY = (mouseY - antiDeadzone) / (1 - antiDeadzone);
														// Clamp to [0, 1]
														originalY = Math.max(0, Math.min(1, originalY));
													}
													
													const newPoints = [...rightCurvePoints];
													const updatedPoint = { x: originalX, y: originalY };
													// Validate monotonicity for the dragged point
													const otherPoints = newPoints.filter((_, i) => i !== draggingPointIndex.index);
													const validatedPoint = validatePointMonotonicity(updatedPoint, otherPoints);
													newPoints[draggingPointIndex.index] = validatedPoint;
													// Re-validate all points to ensure monotonicity
													const finalPoints = validateAllPointsMonotonicity(newPoints);
													setRightCurvePoints(finalPoints);
										// Update input values to match (use final validated points)
										setRightCurveInputValues(finalPoints.map(p => ({ x: p.x.toString(), y: p.y.toString() })));
													// Redraw immediately
													const ctx = rightCurveCanvasRef.current.getContext('2d');
													if (ctx) {
											drawCurveEditor(ctx, 300, 300, finalPoints, undefined, innerDeadzone, antiDeadzone);
													}
												}
											}}
											onMouseUp={() => {
												setDraggingPointIndex(null);
											}}
											onMouseLeave={() => {
												setDraggingPointIndex(null);
											}}
										/>
									</div>
								{/* Display current stick physical and output distance */}
								{rightCurveActive && (() => {
									const physicalDistance = rightStickData.progressRatio !== undefined ? rightStickData.progressRatio : 0;
									// Output distance is the Y value on the curve at the position where X = physicalDistance
									// This matches the highlight line position on the curve canvas
									const innerDeadzone = (values?.inner_deadzone2 || 0) / 100.0;
									const antiDeadzone = (values?.anti_deadzone2 || 0) / 100.0;
									const outputDistance = calculateOutputDistanceFromCurve(
										physicalDistance,
										rightCurvePoints,
										innerDeadzone,
										antiDeadzone
									);
									
									return (
										<div style={{ marginTop: '8px', fontSize: '0.875rem', textAlign: 'center' }}>
											<div>当前摇杆物理距离：{(physicalDistance * 100).toFixed(1)}%</div>
											<div>当前摇杆输出距离：{(outputDistance * 100).toFixed(1)}%</div>
										</div>
									);
								})()}
									</div>

					{/* Row 2, Column 1: Left stick XY position info */}
					<div style={{ display: 'flex', justifyContent: 'center', alignItems: 'center', width: '300px' }}>
						<div className="small" style={{ display: leftCurveActive ? 'none' : 'block', width: '100%' }}>
							<div style={{ display: 'flex', alignItems: 'center', justifyContent: 'center', gap: '4px' }}>
								<span>X:</span>
								{leftFinetuneCenterActive && (
									<Button
										variant="light"
										size="sm"
										style={finetuneButtonStyle}
										onClick={() => {
											const currentCenterX = values?.joystickCenterX || ADC_CENTER;
											setFieldValue('joystickCenterX', currentCenterX + 2);
										}}
									>
										+
									</Button>
								)}
								<span style={{ minWidth: '60px', textAlign: 'center', display: 'inline-block' }}>
									{leftStickData.rawX}
								</span>
								{leftFinetuneCenterActive && (
									<Button
										variant="light"
										size="sm"
										style={finetuneButtonStyle}
										onClick={() => {
											const currentCenterX = values?.joystickCenterX || ADC_CENTER;
											setFieldValue('joystickCenterX', currentCenterX - 2);
										}}
									>
										−
									</Button>
								)}
								<span style={{ marginLeft: '8px', minWidth: '90px', textAlign: 'left', display: 'inline-block' }}>
									({convertToDS4Normalized(leftStickData.x)})
								</span>
							</div>
							<div style={{ display: 'flex', alignItems: 'center', justifyContent: 'center', gap: '4px' }}>
								<span>Y:</span>
								{leftFinetuneCenterActive && (
									<Button
										variant="light"
										size="sm"
										style={finetuneButtonStyle}
										onClick={() => {
											const currentCenterY = values?.joystickCenterY || ADC_CENTER;
											setFieldValue('joystickCenterY', currentCenterY + 2);
										}}
									>
										+
									</Button>
								)}
								<span style={{ minWidth: '60px', textAlign: 'center', display: 'inline-block' }}>
									{leftStickData.rawY}
								</span>
								{leftFinetuneCenterActive && (
									<Button
										variant="light"
										size="sm"
										style={finetuneButtonStyle}
										onClick={() => {
											const currentCenterY = values?.joystickCenterY || ADC_CENTER;
											setFieldValue('joystickCenterY', currentCenterY - 2);
										}}
									>
										−
									</Button>
								)}
								<span style={{ marginLeft: '8px', minWidth: '90px', textAlign: 'left', display: 'inline-block' }}>
									({convertToDS4Normalized(leftStickData.y)})
								</span>
							</div>
						</div>
					</div>

					{/* Row 2, Column 2: Left curve buttons (reset and confirm) */}
					<div style={{ width: '300px', display: 'flex', justifyContent: 'flex-end', alignItems: 'center' }}>
						{leftCurveActive && (
							<div className="d-flex gap-2">
								<Button
									variant="secondary"
									size="sm"
									onClick={() => {
										setLeftCurvePoints([]);
										// Redraw immediately
										if (leftCurveCanvasRef.current) {
											const ctx = leftCurveCanvasRef.current.getContext('2d');
											if (ctx) {
												const innerDeadzone = (values?.inner_deadzone || 0) / 100.0;
												const antiDeadzone = (values?.anti_deadzone || 0) / 100.0;
												drawCurveEditor(ctx, 300, 300, [], undefined, innerDeadzone, antiDeadzone);
											}
										}
									}}
								>
									重置
								</Button>
								<Button
									variant="danger"
									size="sm"
									onClick={() => {
										// Save curve points to config (sorted by x coordinate)
										// Validate and clamp values to [0, 1] range
										const validatedPoints = leftCurvePoints.map(p => ({
											x: Math.max(0, Math.min(1, p.x)),
											y: Math.max(0, Math.min(1, p.y))
										}));
										const sortedPoints = validatedPoints.sort((a, b) => a.x - b.x);
										setFieldValue('joystickCurvePoints1', sortedPoints);
										setLeftCurveActive(false);
									}}
								>
									确定
								</Button>
							</div>
						)}
					</div>

					{/* Row 2, Column 3: Right curve buttons (reset and confirm) */}
					<div style={{ width: '300px', display: 'flex', justifyContent: 'flex-end', alignItems: 'center' }}>
						{rightCurveActive && (
							<div className="d-flex gap-2">
										<Button
											variant="secondary"
											size="sm"
											onClick={() => {
												setRightCurvePoints([]);
										setRightCurveInputValues([]);
												// Redraw immediately
												if (rightCurveCanvasRef.current) {
													const ctx = rightCurveCanvasRef.current.getContext('2d');
													if (ctx) {
												const innerDeadzone = (values?.inner_deadzone2 || 0) / 100.0;
												const antiDeadzone = (values?.anti_deadzone2 || 0) / 100.0;
												drawCurveEditor(ctx, 300, 300, [], undefined, innerDeadzone, antiDeadzone);
													}
												}
											}}
										>
											重置
										</Button>
										<Button
											variant="danger"
											size="sm"
											onClick={() => {
												// Save curve points to config (sorted by x coordinate)
												// Validate and clamp values to [0, 1] range
												const validatedPoints = rightCurvePoints.map(p => ({
													x: Math.max(0, Math.min(1, p.x)),
													y: Math.max(0, Math.min(1, p.y))
												}));
												const sortedPoints = validatedPoints.sort((a, b) => a.x - b.x);
												setFieldValue('joystickCurvePoints2', sortedPoints);
												setRightCurveActive(false);
											}}
										>
											确定
										</Button>
							</div>
						)}
					</div>

					{/* Row 2, Column 4: Right stick XY position info */}
					<div style={{ display: 'flex', justifyContent: 'center', alignItems: 'center', width: '300px' }}>
						<div className="small" style={{ display: rightCurveActive ? 'none' : 'block', width: '100%' }}>
							<div style={{ display: 'flex', alignItems: 'center', justifyContent: 'center', gap: '4px' }}>
								<span>X:</span>
								{rightFinetuneCenterActive && (
									<Button
										variant="light"
										size="sm"
										style={finetuneButtonStyle}
										onClick={() => {
											const currentCenterX = values?.joystickCenterX2 || ADC_CENTER;
											setFieldValue('joystickCenterX2', currentCenterX + 2);
										}}
									>
										+
									</Button>
								)}
								<span style={{ minWidth: '60px', textAlign: 'center', display: 'inline-block' }}>
									{rightStickData.rawX}
								</span>
								{rightFinetuneCenterActive && (
									<Button
										variant="light"
										size="sm"
										style={finetuneButtonStyle}
										onClick={() => {
											const currentCenterX = values?.joystickCenterX2 || ADC_CENTER;
											setFieldValue('joystickCenterX2', currentCenterX - 2);
										}}
									>
										−
									</Button>
								)}
								<span style={{ marginLeft: '8px', minWidth: '90px', textAlign: 'left', display: 'inline-block' }}>
									({convertToDS4Normalized(rightStickData.x)})
								</span>
							</div>
							<div style={{ display: 'flex', alignItems: 'center', justifyContent: 'center', gap: '4px' }}>
								<span>Y:</span>
								{rightFinetuneCenterActive && (
									<Button
										variant="light"
										size="sm"
										style={finetuneButtonStyle}
										onClick={() => {
											const currentCenterY = values?.joystickCenterY2 || ADC_CENTER;
											setFieldValue('joystickCenterY2', currentCenterY + 2);
										}}
									>
										+
									</Button>
								)}
								<span style={{ minWidth: '60px', textAlign: 'center', display: 'inline-block' }}>
									{rightStickData.rawY}
								</span>
								{rightFinetuneCenterActive && (
									<Button
										variant="light"
										size="sm"
										style={finetuneButtonStyle}
										onClick={() => {
											const currentCenterY = values?.joystickCenterY2 || ADC_CENTER;
											setFieldValue('joystickCenterY2', currentCenterY - 2);
										}}
									>
										−
									</Button>
								)}
								<span style={{ marginLeft: '8px', minWidth: '90px', textAlign: 'left', display: 'inline-block' }}>
									({convertToDS4Normalized(rightStickData.y)})
								</span>
							</div>
						</div>
					</div>

					{/* Row 3, Column 1: Left stick buttons */}
					<div style={{ display: 'flex', flexDirection: 'column', alignItems: 'center', gap: '8px', width: '300px' }}>
						<div className="mt-3 d-flex gap-2 justify-content-center flex-wrap">
							<Button
								variant="primary"
								size="sm"
								onClick={() => setShowLeftCalibrationModal(true)}
							>
								{t('AddonsConfig:joystick-calibration-center-button')}
							</Button>
							<Button
								variant="primary"
								size="sm"
								onClick={() => setShowLeftRangeModal(true)}
							>
								{t('AddonsConfig:joystick-calibration-range-button')}
							</Button>
						</div>
						<div className="mt-2 d-flex gap-2 justify-content-center flex-wrap">
							<Button
								variant="warning"
								size="sm"
								onClick={() => setLeftFinetuneCenterActive(!leftFinetuneCenterActive)}
							>
								{t('AddonsConfig:joystick-calibration-finetune-center-button')}
							</Button>
							<Button
								variant="warning"
								size="sm"
								disabled={leftJitterSampling}
								onClick={handleStartJitterSampling1}
							>
								{leftJitterSampling 
									? `抖动数据修正 (${leftJitterSamples.length}/30)` 
									: '抖动数据修正'}
							</Button>
						</div>
						<div className="mt-2 d-flex gap-2 justify-content-center flex-wrap">
							<Button
								variant={leftFinetuneShapeActive ? "success" : "warning"}
								size="sm"
								onClick={() => {
									const rangeData = values?.joystickRangeData1;
									const hasCalibration =
										Array.isArray(rangeData) &&
										rangeData.length === CIRCULARITY_DATA_SIZE &&
										rangeData.some((v: number) => v > 0);
									if (!hasCalibration && !leftFinetuneShapeActive) {
										setShowRangeCalibrationWarning(true);
										return;
									}
									// Reset to saved values when closing (cancel effect)
									if (leftFinetuneShapeActive) {
										setLeftFinetuneShapeForceCircular(values?.joystickFinetuneShapeForceCircular1 ?? false);
										setLeftFinetuneShapeAmplify(values?.joystickFinetuneShapeAmplify1 ?? 0.0);
									}
									setLeftFinetuneShapeActive(!leftFinetuneShapeActive);
								}}
							>
								{t('AddonsConfig:joystick-calibration-finetune-shape-button')}
							</Button>
							<Button
								variant={leftCurveActive ? "success" : "warning"}
								size="sm"
								onClick={() => {
									// If finetune shape is active, close it
									if (leftFinetuneShapeActive) {
										setLeftFinetuneShapeActive(false);
									}
									if (!leftCurveActive) {
										// Load curve points from config when opening
										const saved = values?.joystickCurvePoints1;
										if (Array.isArray(saved)) {
											setLeftCurvePoints(saved);
											setLeftCurveInputValues(saved.map(p => ({ x: p.x.toString(), y: p.y.toString() })));
										} else {
											setLeftCurveInputValues([]);
										}
									}
									setLeftCurveActive(!leftCurveActive);
								}}
							>
								{t('AddonsConfig:joystick-calibration-curve-button')}
							</Button>
						</div>
						<div className="mt-2 d-flex gap-2 justify-content-center flex-wrap">
							<Button
								variant="info"
								size="sm"
								onClick={() => {
									const rangeData = values?.joystickRangeData1;
									setLeftRangeDataSnapshot(Array.isArray(rangeData) ? rangeData : []);
									setLeftAngleIndexSnapshot(leftStickDetailData.angleIndex);
									setShowLeftRangeDataModal(true);
								}}
							>
								查看校准数据
							</Button>
						</div>
					</div>

					{/* Row 3, Column 2: Left curve instructions */}
					<div style={{ width: '300px', display: 'flex', justifyContent: 'center', alignItems: 'center' }}>
						{leftCurveActive && (
							<div className="small text-muted" style={{ textAlign: 'left' }}>
								1、点击线条添加控制点（最多3个）<br />
								2、横向为摇杆物理距离，纵向为输出距离<br />
								3、在虚线上方反映更灵敏，下方更迟钝
								<br /><br />
								<div style={{ fontSize: '0.75rem', color: '#666', marginTop: '8px' }}>
									调试信息：<br />
									高亮线条比例：{(leftStickData.progressRatio !== undefined ? (leftStickData.progressRatio * 100).toFixed(1) : '0.0')}%<br />
									当前摇杆输出值：X: {leftStickData.x.toFixed(3)}, Y: {leftStickData.y.toFixed(3)}<br />
									当前摇杆距离中心距离：{leftStickDetailData.currentDistance.toFixed(1)}<br />
									scale: {leftStickDetailData.scale.toFixed(3)}, l: {(leftStickDetailData.scale > 0 ? (ADC_CENTER / leftStickDetailData.scale).toFixed(1) : ADC_CENTER.toFixed(1))}<br />
									offsetCenter: X: {leftStickDetailData.offsetCenterX.toFixed(1)}, Y: {leftStickDetailData.offsetCenterY.toFixed(1)}
								</div>
							</div>
						)}
					</div>

					{/* Row 3, Column 3: Right curve instructions */}
					<div style={{ width: '300px', display: 'flex', justifyContent: 'center', alignItems: 'center' }}>
						{rightCurveActive && (
							<div className="small text-muted" style={{ textAlign: 'left' }}>
								1、点击线条添加控制点（最多3个）<br />
								2、横向为摇杆物理距离，纵向为输出距离<br />
								3、在虚线上方反映更灵敏，下方更迟钝
								<br /><br />
								<div style={{ fontSize: '0.75rem', color: '#666', marginTop: '8px' }}>
									调试信息：<br />
									高亮线条比例：{(rightStickData.progressRatio !== undefined ? (rightStickData.progressRatio * 100).toFixed(1) : '0.0')}%<br />
									当前摇杆输出值：X: {rightStickData.x.toFixed(3)}, Y: {rightStickData.y.toFixed(3)}<br />
									当前摇杆距离中心距离：{rightStickDetailData.currentDistance.toFixed(1)}<br />
									scale: {rightStickDetailData.scale.toFixed(3)}, l: {(rightStickDetailData.scale > 0 ? (ADC_CENTER / rightStickDetailData.scale).toFixed(1) : ADC_CENTER.toFixed(1))}<br />
									offsetCenter: X: {rightStickDetailData.offsetCenterX.toFixed(1)}, Y: {rightStickDetailData.offsetCenterY.toFixed(1)}
								</div>
							</div>
						)}
					</div>

					{/* Row 3, Column 4: Right stick buttons */}
					<div style={{ display: 'flex', flexDirection: 'column', alignItems: 'center', gap: '8px', width: '300px' }}>
						<div className="mt-3 d-flex gap-2 justify-content-center flex-wrap">
							<Button
								variant="primary"
								size="sm"
								onClick={() => setShowRightCalibrationModal(true)}
							>
								{t('AddonsConfig:joystick-calibration-center-button')}
							</Button>
							<Button
								variant="primary"
								size="sm"
								onClick={() => setShowRightRangeModal(true)}
							>
								{t('AddonsConfig:joystick-calibration-range-button')}
							</Button>
						</div>
						<div className="mt-2 d-flex gap-2 justify-content-center flex-wrap">
							<Button
								variant="warning"
								size="sm"
								onClick={() => setRightFinetuneCenterActive(!rightFinetuneCenterActive)}
							>
								{t('AddonsConfig:joystick-calibration-finetune-center-button')}
							</Button>
							<Button
								variant="warning"
								size="sm"
								disabled={rightJitterSampling}
								onClick={handleStartJitterSampling2}
							>
								{rightJitterSampling 
									? `抖动数据修正 (${rightJitterSamples.length}/30)` 
									: '抖动数据修正'}
							</Button>
						</div>
						<div className="mt-2 d-flex gap-2 justify-content-center flex-wrap">
							<Button
								variant={rightFinetuneShapeActive ? "success" : "warning"}
								size="sm"
								onClick={() => {
									const rangeData = values?.joystickRangeData2;
									const hasCalibration =
										Array.isArray(rangeData) &&
										rangeData.length === CIRCULARITY_DATA_SIZE &&
										rangeData.some((v: number) => v > 0);
									if (!hasCalibration && !rightFinetuneShapeActive) {
										setShowRangeCalibrationWarning(true);
										return;
									}
									// Reset to saved values when closing (cancel effect)
									if (rightFinetuneShapeActive) {
										setRightFinetuneShapeForceCircular(values?.joystickFinetuneShapeForceCircular2 ?? false);
										setRightFinetuneShapeAmplify(values?.joystickFinetuneShapeAmplify2 ?? 0.0);
									}
									setRightFinetuneShapeActive(!rightFinetuneShapeActive);
								}}
							>
								{t('AddonsConfig:joystick-calibration-finetune-shape-button')}
							</Button>
							<Button
								variant={rightCurveActive ? "success" : "warning"}
								size="sm"
								onClick={() => {
									// If finetune shape is active, close it
									if (rightFinetuneShapeActive) {
										setRightFinetuneShapeActive(false);
									}
									if (!rightCurveActive) {
										// Load curve points from config when opening
										const saved = (values as any)?.joystickCurvePoints2;
										if (Array.isArray(saved)) {
											setRightCurvePoints(saved);
											setRightCurveInputValues(saved.map(p => ({ x: p.x.toString(), y: p.y.toString() })));
										} else {
											setRightCurveInputValues([]);
										}
									}
									setRightCurveActive(!rightCurveActive);
								}}
							>
								{t('AddonsConfig:joystick-calibration-curve-button')}
							</Button>
						</div>
						<div className="mt-2 d-flex gap-2 justify-content-center flex-wrap">
							<Button
								variant="info"
								size="sm"
								onClick={() => {
									const rangeData = values?.joystickRangeData2;
									setRightRangeDataSnapshot(Array.isArray(rangeData) ? rangeData : []);
									setRightAngleIndexSnapshot(rightStickDetailData.angleIndex);
									setShowRightRangeDataModal(true);
								}}
							>
								查看校准数据
							</Button>
						</div>
					</div>
				</div>
			</div>
			
			{/* Calibration Modals */}
			<StickCalibrationModal
				show={showLeftCalibrationModal}
				onHide={() => setShowLeftCalibrationModal(false)}
				onComplete={(centerX, centerY) => {
					setFieldValue('joystickCenterX', centerX);
					setFieldValue('joystickCenterY', centerY);
				}}
				stickNumber={1}
				stickLabel={t('AddonsConfig:joystick-calibration-left-stick')}
			/>
			<StickCalibrationModal
				show={showRightCalibrationModal}
				onHide={() => setShowRightCalibrationModal(false)}
				onComplete={(centerX, centerY) => {
					setFieldValue('joystickCenterX2', centerX);
					setFieldValue('joystickCenterY2', centerY);
				}}
				stickNumber={2}
				stickLabel={t('AddonsConfig:joystick-calibration-right-stick')}
			/>
			
			{/* Range Calibration Modals */}
			<RangeCalibrationModal
				show={showLeftRangeModal}
				onHide={() => setShowLeftRangeModal(false)}
				onComplete={(rangeData) => {
					setFieldValue('joystickRangeData1', rangeData);
				}}
				stickNumber={1}
				stickLabel={t('AddonsConfig:joystick-calibration-left-stick')}
				centerX={values?.joystickCenterX}
				centerY={values?.joystickCenterY}
			/>
			<RangeCalibrationModal
				show={showRightRangeModal}
				onHide={() => setShowRightRangeModal(false)}
				onComplete={(rangeData) => {
					setFieldValue('joystickRangeData2', rangeData);
				}}
				stickNumber={2}
				stickLabel={t('AddonsConfig:joystick-calibration-right-stick')}
				centerX={values?.joystickCenterX2}
				centerY={values?.joystickCenterY2}
			/>
			
			
			
			{/* Range Data Detail Modals */}
			<Modal show={showLeftRangeDataModal} onHide={() => setShowLeftRangeDataModal(false)} size="lg">
				<Modal.Header closeButton>
					<Modal.Title>左摇杆外圈校准数据</Modal.Title>
				</Modal.Header>
				<Modal.Body>
					<div className="mb-3">
						<strong>摇杆中心数据:</strong> ({leftStickDetailData.centerX.toFixed(1)}, {leftStickDetailData.centerY.toFixed(1)})
					</div>
					<div className="mb-2 small text-muted">
						数据条目数: {leftRangeDataSnapshot.length} / {CIRCULARITY_DATA_SIZE}
					</div>
					<Table striped bordered hover size="sm">
						<thead>
							<tr>
								<th>序号</th>
								<th>角度范围</th>
								<th>缩放比</th>
							</tr>
						</thead>
						<tbody>
							{Array.from({ length: CIRCULARITY_DATA_SIZE }, (_, index) => {
								const scale = leftRangeDataSnapshot[index];
								const angleStart = ((index * 360 / CIRCULARITY_DATA_SIZE) - 180).toFixed(1);
								const angleEnd = (((index + 1) * 360 / CIRCULARITY_DATA_SIZE) - 180).toFixed(1);
								return (
									<tr key={index} className={index === leftAngleIndexSnapshot ? 'table-primary' : ''}>
										<td>{index}</td>
										<td>{angleStart}° ~ {angleEnd}°</td>
										<td>{scale !== undefined && scale !== null && scale > 0 ? scale.toFixed(4) : 'N/A'}</td>
									</tr>
								);
							})}
						</tbody>
					</Table>
				</Modal.Body>
				<Modal.Footer>
					<Button variant="secondary" onClick={() => setShowLeftRangeDataModal(false)}>
						关闭
					</Button>
				</Modal.Footer>
			</Modal>
			
			<Modal show={showRightRangeDataModal} onHide={() => setShowRightRangeDataModal(false)} size="lg">
				<Modal.Header closeButton>
					<Modal.Title>右摇杆外圈校准数据</Modal.Title>
				</Modal.Header>
				<Modal.Body>
					<div className="mb-3">
						<strong>摇杆中心数据:</strong> ({rightStickDetailData.centerX.toFixed(1)}, {rightStickDetailData.centerY.toFixed(1)})
					</div>
					<div className="mb-2 small text-muted">
						数据条目数: {rightRangeDataSnapshot.length} / {CIRCULARITY_DATA_SIZE}
					</div>
					<Table striped bordered hover size="sm">
						<thead>
							<tr>
								<th>序号</th>
								<th>角度范围</th>
								<th>缩放比</th>
							</tr>
						</thead>
						<tbody>
							{Array.from({ length: CIRCULARITY_DATA_SIZE }, (_, index) => {
								const scale = rightRangeDataSnapshot[index];
								const angleStart = ((index * 360 / CIRCULARITY_DATA_SIZE) - 180).toFixed(1);
								const angleEnd = (((index + 1) * 360 / CIRCULARITY_DATA_SIZE) - 180).toFixed(1);
								return (
									<tr key={index} className={index === rightAngleIndexSnapshot ? 'table-primary' : ''}>
										<td>{index}</td>
										<td>{angleStart}° ~ {angleEnd}°</td>
										<td>{scale !== undefined && scale !== null && scale > 0 ? scale.toFixed(4) : 'N/A'}</td>
									</tr>
								);
							})}
						</tbody>
					</Table>
				</Modal.Body>
				<Modal.Footer>
					<Button variant="secondary" onClick={() => setShowRightRangeDataModal(false)}>
						关闭
					</Button>
				</Modal.Footer>
			</Modal>

			{/* Left Jitter Data Modal */}
			<Modal show={showLeftJitterDataModal} onHide={() => setShowLeftJitterDataModal(false)} size="lg">
				<Modal.Header closeButton>
					<Modal.Title>抖动数据修正 - 左摇杆</Modal.Title>
				</Modal.Header>
				<Modal.Body style={{ maxHeight: '70vh', overflowY: 'auto' }}>
					{/* Jitter Filter Slider */}
					<div className="mb-4">
						<Form.Label>抖动过滤值: {leftJitterFilter}</Form.Label>
						<Form.Range
							min={0}
							max={30}
							step={1}
							value={leftJitterFilter}
							onChange={(e) => setLeftJitterFilter(parseInt(e.target.value))}
						/>
					</div>
					{/* Statistics Section */}
					{leftJitterStats && (
						<div className="mb-4">
							<h5>抖动统计</h5>
							<p><strong>样本数量: {leftJitterSamples.length}</strong></p>
							<Table striped bordered size="sm" className="mb-3">
								<thead>
									<tr>
										<th>统计项</th>
										<th>X</th>
										<th>Y</th>
									</tr>
								</thead>
								<tbody>
									<tr>
										<td><strong>平均值</strong></td>
										<td>{leftJitterStats.meanX.toFixed(2)}</td>
										<td>{leftJitterStats.meanY.toFixed(2)}</td>
									</tr>
									<tr>
										<td><strong>方差</strong></td>
										<td>{leftJitterStats.varianceX.toFixed(2)}</td>
										<td>{leftJitterStats.varianceY.toFixed(2)}</td>
									</tr>
									<tr>
										<td><strong>平均绝对偏差</strong></td>
										<td>{leftJitterStats.meanDeviationX.toFixed(2)}</td>
										<td>{leftJitterStats.meanDeviationY.toFixed(2)}</td>
									</tr>
									<tr>
										<td><strong>偏差率</strong></td>
										<td>{leftJitterStats.deviationRateX.toFixed(2)}%</td>
										<td>{leftJitterStats.deviationRateY.toFixed(2)}%</td>
									</tr>
									<tr>
										<td><strong>上偏差</strong></td>
										<td>{leftJitterStats.upperDeviationX.toFixed(2)}</td>
										<td>{leftJitterStats.upperDeviationY.toFixed(2)}</td>
									</tr>
									<tr>
										<td><strong>下偏差</strong></td>
										<td>{leftJitterStats.lowerDeviationX.toFixed(2)}</td>
										<td>{leftJitterStats.lowerDeviationY.toFixed(2)}</td>
									</tr>
								</tbody>
							</Table>
						</div>
					)}
					{/* Sampling Data Section */}
					<h5>取样数据</h5>
					<Table striped bordered hover size="sm">
						<thead>
							<tr>
								<th>#</th>
								<th>X (ADC)</th>
								<th>Y (ADC)</th>
							</tr>
						</thead>
						<tbody>
							{leftJitterSamples.map((sample, index) => (
								<tr key={index}>
									<td>{index + 1}</td>
									<td>{sample.x.toFixed(2)}</td>
									<td>{sample.y.toFixed(2)}</td>
								</tr>
							))}
						</tbody>
					</Table>
				</Modal.Body>
				<Modal.Footer>
					<Button variant="secondary" onClick={() => {
						// Cancel: restore original value
						setLeftJitterFilter(leftJitterFilterOriginal);
						setShowLeftJitterDataModal(false);
					}}>
						取消
					</Button>
					<Button variant="primary" onClick={() => {
						// Save: update field value
						setFieldValue('joystickJitterFilter1', leftJitterFilter);
						setLeftJitterFilterOriginal(leftJitterFilter);
						setShowLeftJitterDataModal(false);
					}}>
						确定
					</Button>
				</Modal.Footer>
			</Modal>

			{/* Right Jitter Data Modal */}
			<Modal show={showRightJitterDataModal} onHide={() => setShowRightJitterDataModal(false)} size="lg">
				<Modal.Header closeButton>
					<Modal.Title>抖动数据修正 - 右摇杆</Modal.Title>
				</Modal.Header>
				<Modal.Body style={{ maxHeight: '70vh', overflowY: 'auto' }}>
					{/* Jitter Filter Slider */}
					<div className="mb-4">
						<Form.Label>抖动过滤值: {rightJitterFilter}</Form.Label>
						<Form.Range
							min={0}
							max={30}
							step={1}
							value={rightJitterFilter}
							onChange={(e) => setRightJitterFilter(parseInt(e.target.value))}
						/>
					</div>
					{/* Statistics Section */}
					{rightJitterStats && (
						<div className="mb-4">
							<h5>抖动统计</h5>
							<p><strong>样本数量: {rightJitterSamples.length}</strong></p>
							<Table striped bordered size="sm" className="mb-3">
								<thead>
									<tr>
										<th>统计项</th>
										<th>X</th>
										<th>Y</th>
									</tr>
								</thead>
								<tbody>
									<tr>
										<td><strong>平均值</strong></td>
										<td>{rightJitterStats.meanX.toFixed(2)}</td>
										<td>{rightJitterStats.meanY.toFixed(2)}</td>
									</tr>
									<tr>
										<td><strong>方差</strong></td>
										<td>{rightJitterStats.varianceX.toFixed(2)}</td>
										<td>{rightJitterStats.varianceY.toFixed(2)}</td>
									</tr>
									<tr>
										<td><strong>平均绝对偏差</strong></td>
										<td>{rightJitterStats.meanDeviationX.toFixed(2)}</td>
										<td>{rightJitterStats.meanDeviationY.toFixed(2)}</td>
									</tr>
									<tr>
										<td><strong>偏差率</strong></td>
										<td>{rightJitterStats.deviationRateX.toFixed(2)}%</td>
										<td>{rightJitterStats.deviationRateY.toFixed(2)}%</td>
									</tr>
									<tr>
										<td><strong>上偏差</strong></td>
										<td>{rightJitterStats.upperDeviationX.toFixed(2)}</td>
										<td>{rightJitterStats.upperDeviationY.toFixed(2)}</td>
									</tr>
									<tr>
										<td><strong>下偏差</strong></td>
										<td>{rightJitterStats.lowerDeviationX.toFixed(2)}</td>
										<td>{rightJitterStats.lowerDeviationY.toFixed(2)}</td>
									</tr>
								</tbody>
							</Table>
						</div>
					)}
					{/* Sampling Data Section */}
					<h5>取样数据</h5>
					<Table striped bordered hover size="sm">
						<thead>
							<tr>
								<th>#</th>
								<th>X (ADC)</th>
								<th>Y (ADC)</th>
							</tr>
						</thead>
						<tbody>
							{rightJitterSamples.map((sample, index) => (
								<tr key={index}>
									<td>{index + 1}</td>
									<td>{sample.x.toFixed(2)}</td>
									<td>{sample.y.toFixed(2)}</td>
								</tr>
							))}
						</tbody>
					</Table>
				</Modal.Body>
				<Modal.Footer>
					<Button variant="secondary" onClick={() => {
						// Cancel: restore original value
						setRightJitterFilter(rightJitterFilterOriginal);
						setShowRightJitterDataModal(false);
					}}>
						取消
					</Button>
					<Button variant="primary" onClick={() => {
						// Save: update field value
						setFieldValue('joystickJitterFilter2', rightJitterFilter);
						setRightJitterFilterOriginal(rightJitterFilter);
						setShowRightJitterDataModal(false);
					}}>
						确定
					</Button>
				</Modal.Footer>
			</Modal>
			
			{/* Save Button */}
			<div className="mt-3">
				<Button type="button" onClick={() => handleSubmit()}>
					{t('Common:button-save-label')}
				</Button>
			</div>

			{/* Range calibration warning modal */}
			<Modal
				show={showRangeCalibrationWarning}
				onHide={() => setShowRangeCalibrationWarning(false)}
				centered
			>
				<Modal.Header closeButton>
					<Modal.Title>提示</Modal.Title>
				</Modal.Header>
				<Modal.Body>
					<p className="mb-0">请先进行外圈校准。</p>
				</Modal.Body>
				<Modal.Footer>
					<Button variant="primary" onClick={() => setShowRangeCalibrationWarning(false)}>
						确定
					</Button>
				</Modal.Footer>
			</Modal>
		</Section>
	);
};

export default JoystickCalibration;
