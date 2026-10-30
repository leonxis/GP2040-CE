import { useContext, useEffect, useRef, useState, type ChangeEvent } from 'react';
import { useTranslation } from 'react-i18next';
import { Button, Form, OverlayTrigger, Tooltip } from 'react-bootstrap';
import { useFormikContext } from 'formik';

import InfoCircle from '../../../Icons/InfoCircle';
import Section from '../../../Components/Section';
import type { AddonPropTypes } from './CalibrationSettings';
import { BUTTON_MASKS_OPTIONS, getButtonLabels } from '../../../Data/Buttons';
import { AppContext } from '../../../Contexts/AppContext';

// Type definitions
type CurvePoint = { x: number; y: number };
type CurvePointInput = { x: string; y: string };
const DEFAULT_ADC_MAX = 4095;
const CIRCULARITY_DATA_SIZE = 48;

/** Custom stick curve presets in Web config / firmware (indexed 0..3). */
const CURVE_PRESET_COUNT = 4;
/** Intermediate control points between inner dead zone and (1,1); not counting endpoints appended in firmware. */
const CURVE_POINT_COUNT = 6;
/** Matches `CurvePreset.name` max_length in config.proto (bytes; ASCII subset ⇒ same char limit). */
const CURVE_PRESET_NAME_MAX_LEN = 64;

function sortCurvePointsByX(pts: CurvePoint[]): CurvePoint[] {
	return [...pts].sort((a, b) => (a.x === b.x ? a.y - b.y : a.x - b.x));
}

function emptyPresetPointSlots(): CurvePointInput[] {
	return Array.from({ length: CURVE_POINT_COUNT }, () => ({ x: '0', y: '0' }));
}

/** Preset display/storage name: English ASCII subset only; collapse spaces; max 64 chars (device limit). */
function sanitizePresetNameForPreset(raw: unknown): string {
	const filtered = String(raw ?? '').replace(/[^A-Za-z0-9 ]/g, '');
	const collapsed = filtered.replace(/\s+/g, ' ').trim();
	return collapsed.slice(0, CURVE_PRESET_NAME_MAX_LEN);
}

/** While typing: strip disallowed chars + enforce max length, keep spaces (full sanitize happens on save/load). */
function sanitizePresetNameWhileTyping(raw: unknown): string {
	return String(raw ?? '').replace(/[^A-Za-z0-9 ]/g, '').slice(0, CURVE_PRESET_NAME_MAX_LEN);
}

function enforceImportedPresetPoints(points: CurvePointInput[]): void {
	const epsilon = 0.01;
	let prevX = 0;
	let prevY = 0;
	for (let pointIndex = 0; pointIndex < points.length; pointIndex++) {
		const inputValueX = parseFloat(points[pointIndex].x) || 0;
		const inputValueY = parseFloat(points[pointIndex].y) || 0;
		if (inputValueX === 0 && inputValueY === 0) {
			continue;
		}
		let correctedX = inputValueX;
		let correctedY = inputValueY;
		if (prevX > 0 && correctedX < prevX) {
			correctedX = Math.min(1, prevX + epsilon);
		}
		if (prevY > 0 && correctedY < prevY) {
			correctedY = Math.min(1, prevY + epsilon);
		}
		correctedX = Math.max(0, Math.min(1, correctedX));
		correctedY = Math.max(0, Math.min(1, correctedY));
		points[pointIndex] = {
			x: parseFloat(correctedX.toFixed(4)).toString(),
			y: parseFloat(correctedY.toFixed(4)).toString(),
		};
		prevX = correctedX;
		prevY = correctedY;
	}
}

/**
 * Parse ControllerMeta / 「动感指尖」curve JSON. Returns null if invalid (caller shows unified alert).
 */
function parseCurvePresetImportJSON(text: string): { name: string; points: CurvePointInput[] } | null {
	let parsed: unknown;
	try {
		parsed = JSON.parse(text);
	} catch {
		return null;
	}
	if (parsed === null || typeof parsed !== 'object' || Array.isArray(parsed)) {
		return null;
	}
	const o = parsed as Record<string, unknown>;
	if (!Object.prototype.hasOwnProperty.call(o, 'name') || !Object.prototype.hasOwnProperty.call(o, 'data')) {
		return null;
	}
	if (o.name === undefined || o.name === null || typeof o.name !== 'string') {
		return null;
	}
	if (o.data === undefined || o.data === null || !Array.isArray(o.data) || o.data.length !== 16) {
		return null;
	}
	const nums = o.data.map((v) => Number(v));
	if (!nums.every((n) => Number.isFinite(n))) {
		return null;
	}
	const sanitizedName = sanitizePresetNameForPreset(o.name);
	if (sanitizedName === '') {
		return null;
	}
	const slice = nums.slice(2, 14);
	const points: CurvePointInput[] = [];
	for (let i = 0; i < CURVE_POINT_COUNT; i++) {
		const xPct = slice[i * 2];
		const yPct = slice[i * 2 + 1];
		const xf = Math.max(0, Math.min(1, xPct / 100));
		const yf = Math.max(0, Math.min(1, yPct / 100));
		points.push({
			x: parseFloat(xf.toFixed(4)).toString(),
			y: parseFloat(yf.toFixed(4)).toString(),
		});
	}
	enforceImportedPresetPoints(points);
	return { name: sanitizedName, points };
}

/** Formik/firmware preset slot (profile 1..2). Always length CURVE_PRESET_COUNT so UI index matches API index. */
type PresetSlotSerialize = {
	name: string;
	points: Array<{ x: string; y: string }>;
	activationButtonMask?: number;
};

function joystickCurvePresetsSlotsForFormik(
	presetSlots: PresetSlotSerialize[],
): Array<{ name: string; points: CurvePoint[]; activationButtonMask: number }> {
	const out: Array<{ name: string; points: CurvePoint[]; activationButtonMask: number }> = [];
	for (let i = 0; i < CURVE_PRESET_COUNT; i++) {
		const currentPreset = presetSlots[i];
		const currentPoints: CurvePoint[] = [];
		const pts = currentPreset?.points ?? [];
		for (let j = 0; j < pts.length; j++) {
			const x = parseFloat(String(pts[j].x)) || 0;
			const y = parseFloat(String(pts[j].y)) || 0;
			if (x > 0 || y > 0) {
				currentPoints.push({
					x: Math.max(0, Math.min(1, x)),
					y: Math.max(0, Math.min(1, y)),
				});
			}
		}
		out.push({
			name: sanitizePresetNameForPreset(currentPreset?.name ?? ''),
			points: sortCurvePointsByX(currentPoints),
			activationButtonMask: currentPreset?.activationButtonMask ?? 0,
		});
	}
	return out;
}

type QuickSwitchHeadingProps = { t: (key: string) => string; tooltipIdSuffix: string };

const RightStickQuickSwitchHeading = ({ t, tooltipIdSuffix }: QuickSwitchHeadingProps) => {
	const tip = t('CalibrationSettings:hml-right-stick-quick-switch-tooltip');
	return (
		<>
			<span style={{ fontSize: '0.875rem', lineHeight: 1.25 }}>{t('CalibrationSettings:hml-right-stick-quick-switch')}</span>
			<OverlayTrigger placement="top" overlay={<Tooltip id={`right-stick-quick-switch-tip-${tooltipIdSuffix}`}>{tip}</Tooltip>}>
				<span style={{ display: 'inline-flex', cursor: 'help', alignItems: 'center' }} aria-label={tip}>
					<InfoCircle />
				</span>
			</OverlayTrigger>
		</>
	);
};

const areCurvePointsEqual = (a: CurvePoint[], b: CurvePoint[]) => {
	if (a.length !== b.length) return false;
	for (let i = 0; i < a.length; i++) {
		if (Math.abs(a[i].x - b[i].x) > 0.0001 || Math.abs(a[i].y - b[i].y) > 0.0001) {
			return false;
		}
	}
	return true;
};

/**
 * Get interpolated scale for a given angle using range calibration data
 */
const getInterpolatedScale = (angle: number, rangeData: number[]): number => {
	if (rangeData.length === 0) return 1.0;
	
	const normalizedAngle = (angle + Math.PI) / (2.0 * Math.PI);
	const index = normalizedAngle * CIRCULARITY_DATA_SIZE;
	const i0 = Math.floor(index) % CIRCULARITY_DATA_SIZE;
	const i1 = (i0 + 1) % CIRCULARITY_DATA_SIZE;
	const t = index - Math.floor(index);
	
	const r0 = rangeData[i0] || 0;
	const r1 = rangeData[i1] || 0;
	
	return r0 * (1.0 - t) + r1 * t;
};

/**
 * Processes joystick data through coordinate transformation pipeline (matches backend logic)
 */
const processJoystickData = (
	rawX: number,
	rawY: number,
	centerX: number,
	centerY: number,
	rangeData: number[],
	adcMax: number
) => {
	const adcCenter = adcMax / 2.0;
	// Step 1: Transform to center-relative coordinates
	const dX_value = centerX - adcCenter;
	const dY_value = centerY - adcCenter;
	const offset_x = rawX - dX_value;
	const offset_y = rawY - dY_value;
	const offset_center_x = offset_x - adcCenter;
	const offset_center_y = offset_y - adcCenter;
	
	// Step 2: Range calibration scaling
	const current_distance = Math.sqrt(offset_center_x * offset_center_x + offset_center_y * offset_center_y);
	const angle = Math.atan2(offset_center_y, offset_center_x);
	const rawScale = getInterpolatedScale(angle, rangeData);
	const scale = rawScale > 0 ? rawScale : 1.0;
	
	const scaled_center_x = offset_center_x / scale;
	const scaled_center_y = offset_center_y / scale;
	
	// Step 3: Normalize to [-1, 1] range
	const stickX = scaled_center_x / adcCenter;
	const stickY = scaled_center_y / adcCenter;
	
	return {
		stickX,
		stickY,
		detailData: {
			centerX,
			centerY,
			scale,
			currentDistance: current_distance,
		}
	};
};

/**
 * Applies response curve to a normalized value (0-1)
 */
const applyResponseCurve = (value: number, points: CurvePoint[]): number => {
	if (points.length === 0) {
		return value;
	}
	
	const sortedPoints = sortCurvePointsByX(points);
	const fullPoints: CurvePoint[] = [
		{x: 0, y: 0},
		...sortedPoints,
		{x: 1, y: 1}
	];
	
	for (let i = 0; i < fullPoints.length - 1; i++) {
		const p1 = fullPoints[i];
		const p2 = fullPoints[i + 1];
		
		if (value >= p1.x && value <= p2.x) {
			if (p2.x === p1.x) {
				return p1.y;
			}
			const t = (value - p1.x) / (p2.x - p1.x);
			return p1.y + t * (p2.y - p1.y);
		}
	}
	
	return value;
};

/**
 * Cache for static background canvas (performance optimization)
 */
interface CurveStaticCanvasCache {
	canvas: HTMLCanvasElement;
	ctx: CanvasRenderingContext2D;
	key: string;
}

const curveStaticCanvasCache = new Map<string, CurveStaticCanvasCache>();

/**
 * Generates a cache key for static curve canvas
 */
const getCurveStaticCanvasKey = (
	width: number,
	height: number,
	innerDeadzone: number,
	antiDeadzone: number,
): string => {
	return `${width}x${height}_${innerDeadzone}_${antiDeadzone}`;
};

/**
 * Draws static background on offscreen canvas (cached for performance)
 */
const drawCurveStaticBackground = (
	width: number,
	height: number,
	innerDeadzone: number,
	antiDeadzone: number,
): HTMLCanvasElement => {
	const cacheKey = getCurveStaticCanvasKey(width, height, innerDeadzone, antiDeadzone);
	
	// Check cache
	if (curveStaticCanvasCache.has(cacheKey)) {
		return curveStaticCanvasCache.get(cacheKey)!.canvas;
	}

	// Create new offscreen canvas
	const canvas = document.createElement('canvas');
	canvas.width = width;
	canvas.height = height;
	const ctx = canvas.getContext('2d')!;

	// Use integer coordinates for better performance
	const intWidth = Math.round(width);
	const intHeight = Math.round(height);

	// Keep canvas background transparent so card background can show through.
	ctx.clearRect(0, 0, intWidth, intHeight);

	// Draw border
	ctx.strokeStyle = '#000000';
	ctx.lineWidth = 1;
	ctx.strokeRect(0, 0, intWidth, intHeight);
	
	// Draw light gray grid
	ctx.strokeStyle = '#b0b0b0';
	ctx.lineWidth = 1;
	const gridSize = 10;
	for (let i = 0; i <= gridSize; i++) {
		const pos = Math.round((i / gridSize) * intWidth);
		// Vertical lines
		ctx.beginPath();
		ctx.moveTo(pos, 0);
		ctx.lineTo(pos, intHeight);
		ctx.stroke();
		// Horizontal lines
		ctx.beginPath();
		ctx.moveTo(0, pos);
		ctx.lineTo(intWidth, pos);
		ctx.stroke();
	}
	
	// Draw red dots at origin (0,0) and (1,1)
	ctx.fillStyle = '#ff0000';
	// Origin (0,0) at bottom-left
	ctx.beginPath();
	ctx.arc(0, intHeight, 6, 0, 2 * Math.PI);
	ctx.fill();
	// End point (1,1) at top-right
	ctx.beginPath();
	ctx.arc(intWidth, 0, 6, 0, 2 * Math.PI);
	ctx.fill();
	
	// Draw gray start point at (deadzone, anti-deadzone)
	const startPoint = { x: innerDeadzone, y: antiDeadzone };
	if (innerDeadzone > 0 || antiDeadzone > 0) {
		const startPx = Math.round(startPoint.x * intWidth);
		const startPy = Math.round(intHeight - startPoint.y * intHeight);
		ctx.fillStyle = '#808080';
		ctx.beginPath();
		ctx.arc(startPx, startPy, 6, 0, 2 * Math.PI);
		ctx.fill();
		ctx.strokeStyle = '#ffffff';
		ctx.lineWidth = 2;
		ctx.stroke();
	}
	
	// Draw dashed reference lines and purple mask
	ctx.strokeStyle = '#999999';
	ctx.lineWidth = 1;
	ctx.setLineDash([5, 5]);
	
	if (innerDeadzone > 0 || antiDeadzone > 0) {
		const startPx = Math.round(startPoint.x * intWidth);
		const startPy = Math.round(intHeight - startPoint.y * intHeight);
		
		// Vertical line (deadzone boundary)
		if (innerDeadzone > 0) {
			ctx.beginPath();
			ctx.moveTo(startPx, 0);
			ctx.lineTo(startPx, intHeight);
			ctx.stroke();
		}
		
		// Horizontal line (anti-deadzone boundary)
		if (antiDeadzone > 0) {
			ctx.beginPath();
			ctx.moveTo(0, startPy);
			ctx.lineTo(intWidth, startPy);
			ctx.stroke();
		}
		
		// Dashed line from start point to (1,1)
		ctx.beginPath();
		ctx.moveTo(startPx, startPy);
		ctx.lineTo(intWidth, 0);
		ctx.stroke();
		
		// Purple mask: (deadzone, anti-deadzone) - (deadzone, 1) - (1,1) - (1, anti-deadzone)
		ctx.fillStyle = 'rgba(128, 0, 128, 0.15)';
		ctx.beginPath();
		ctx.moveTo(startPx, startPy);
		ctx.lineTo(startPx, 0); // (deadzone, 1)
		ctx.lineTo(intWidth, 0); // (1, 1)
		ctx.lineTo(intWidth, startPy); // (1, anti-deadzone)
		ctx.closePath();
		ctx.fill();
	}
	
	ctx.setLineDash([]);

	// Cache the result
	curveStaticCanvasCache.set(cacheKey, { canvas, ctx, key: cacheKey });

	// Limit cache size to prevent memory issues
	if (curveStaticCanvasCache.size > 10) {
		const firstKey = curveStaticCanvasCache.keys().next().value;
		if (firstKey !== undefined) {
		curveStaticCanvasCache.delete(firstKey);
		}
	}

	return canvas;
};

/**
 * Draws curve editor on canvas (optimized version)
 * Uses offscreen canvas caching for static background
 * @param ctx Canvas context
 * @param width Canvas width
 * @param height Canvas height
 * @param points Control points (excluding start (0,0) and end (1,1))
 * @param innerDeadzone Inner deadzone value (0-1), X-axis intercept
 * @param antiDeadzone Anti-deadzone value (0-1), Y-axis intercept
 */
const drawCurveEditor = (
	ctx: CanvasRenderingContext2D,
	width: number,
	height: number,
	points: CurvePoint[],
	lightX?: number, // Physical distance (sqrt(dist_sq)) as percentage
	lightY?: number, // Output distance (sqrt(stickX*stickX + stickY*stickY)) as percentage
	innerDeadzone: number = 0,
	antiDeadzone: number = 0
) => {
	// Draw cached static background
	const staticCanvas = drawCurveStaticBackground(width, height, innerDeadzone, antiDeadzone);
	ctx.clearRect(0, 0, width, height);
	ctx.drawImage(staticCanvas, 0, 0);

	// Use integer coordinates for better performance
	const intWidth = Math.round(width);
	const intHeight = Math.round(height);

	// Step 4: Draw black curve from start point - control points (if any) - (1,1)
	// All coordinates relative to origin
	// Cache sorted points to avoid repeated sorting
	const sortedPoints = sortCurvePointsByX(points);
	const curvePoints: CurvePoint[] = [];
	
	// Add start point
	const startPoint = { x: innerDeadzone, y: antiDeadzone };
	if (innerDeadzone > 0 || antiDeadzone > 0) {
		curvePoints.push(startPoint);
	} else {
		curvePoints.push({ x: 0, y: 0 });
	}
	
	// Add control points (already sorted)
	curvePoints.push(...sortedPoints);
	
	// Add end point (1,1)
	curvePoints.push({ x: 1, y: 1 });
	
	// Draw curve
	ctx.strokeStyle = '#000000'; // Black
	ctx.lineWidth = 2;
	ctx.beginPath();
	for (let i = 0; i < curvePoints.length; i++) {
		const point = curvePoints[i];
		const px = Math.round(point.x * intWidth);
		const py = Math.round(intHeight - point.y * intHeight);
		if (i === 0) {
			ctx.moveTo(px, py);
		} else {
			ctx.lineTo(px, py);
		}
	}
	ctx.stroke();
	
	// Step 5: Draw orange highlight line from start point along curve to X=lightX
	if (lightX !== undefined && lightX >= 0) {
		// Clamp lightX to [0, 1]
		const targetX = Math.max(0, Math.min(1, lightX));
		const startPointX = curvePoints[0].x;
		
		// Only draw if targetX >= startPointX (can't draw backwards)
		if (targetX >= startPointX) {
			// Draw highlight line from start point along curve to X=targetX
			ctx.strokeStyle = '#FFA500';
			ctx.lineWidth = 2;
			ctx.beginPath();
			
			// Start from start point
			const startPx = Math.round(startPointX * intWidth);
			const startPy = Math.round(intHeight - curvePoints[0].y * intHeight);
			ctx.moveTo(startPx, startPy);
			
			// Traverse curve segments until we reach X=targetX
			let found = false;
			
			for (let i = 0; i < curvePoints.length - 1; i++) {
				const p1 = curvePoints[i];
				const p2 = curvePoints[i + 1];
				
				// Check if targetX is within this segment
				if (targetX >= p1.x && targetX <= p2.x) {
					// Interpolate Y value on the curve at targetX
					const t = p2.x !== p1.x ? (targetX - p1.x) / (p2.x - p1.x) : 0;
					const curveY = p1.y + t * (p2.y - p1.y);
					
					// Draw to (targetX, curveY) on the curve
					const targetPx = Math.round(targetX * intWidth);
					const targetPy = Math.round(intHeight - curveY * intHeight);
					ctx.lineTo(targetPx, targetPy);
					found = true;
					break;
				} else if (targetX > p2.x) {
					// Draw to end of this segment
					const px2 = Math.round(p2.x * intWidth);
					const py2 = Math.round(intHeight - p2.y * intHeight);
					ctx.lineTo(px2, py2);
				} else {
					// targetX < p1.x, should not happen if targetX >= startPointX
					break;
				}
			}
			
			// If targetX >= 1.0 and not found, draw to end point
			if (targetX >= 1.0 && !found) {
				ctx.lineTo(intWidth, 0);
			}
			
			ctx.stroke();
		}
	}
	
	// Step 6: Draw orange control points (if any exist)
	// All coordinates relative to origin, no mapping
	// Draw after curve so control points appear on top
	for (let i = 0; i < sortedPoints.length; i++) {
		const point = sortedPoints[i];
		if (point.x >= 0 && point.x <= 1 && point.y >= 0 && point.y <= 1) {
			const px = Math.round(point.x * intWidth);
			const py = Math.round(intHeight - point.y * intHeight);
			ctx.fillStyle = '#FFA500'; // Orange
			ctx.beginPath();
			ctx.arc(px, py, 6, 0, 2 * Math.PI);
			ctx.fill();
			ctx.strokeStyle = '#ffffff';
			ctx.lineWidth = 2;
			ctx.stroke();
		}
	}
};

/**
 * Validates and corrects a point's X and Y values to ensure strict monotonicity
 * When dragging a point:
 * - Cannot be less than 101% of smaller points' X/Y values (smaller_point.x/y + 0.01)
 * - Cannot be greater than 99% of larger points' X/Y values (larger_point.x/y - 0.01)
 * - If limit is reached, cannot continue moving in that direction
 * - Cannot push other points to allow further movement
 * - Must maintain 1% gap from start point (deadzone, anti-deadzone) and end point (1, 1)
 * - Cannot skip over other points when dragging
 * @param point The point to validate
 * @param allReferencePoints All reference points including start point, other control points, and end point
 * @param currentX Optional current X value of the point being dragged (to prevent skipping)
 */
const validatePointMonotonicity = (
	point: CurvePoint,
	allReferencePoints: CurvePoint[],
	currentX?: number // Current X value of the point being dragged (to prevent skipping)
): CurvePoint => {
	const epsilon = 0.01; // 1% gap
	
	// Use current X value if provided, otherwise use point.x
	// This prevents skipping over other points when dragging
	const referenceX = currentX !== undefined ? currentX : point.x;
	
	// Validate X value: must be at least 1% greater than points with smaller X values
	// and at most 1% less than points with larger X values
	// Use referenceX to determine which points are smaller/larger, not the new point.x
	const smallerXPoints = allReferencePoints.filter(p => p.x < referenceX);
	const largerXPoints = allReferencePoints.filter(p => p.x > referenceX);
	
	// X value lower bound: max(smaller_points.x) + 0.01
	const xLowerBound = smallerXPoints.length > 0
		? smallerXPoints.reduce((max, p) => Math.max(max, p.x), 0) + epsilon
		: 0;
	
	// X value upper bound: min(larger_points.x) - 0.01
	const xUpperBound = largerXPoints.length > 0
		? largerXPoints.reduce((min, p) => Math.min(min, p.x), 1) - epsilon
		: 1;
	
	// Clamp X value to bounds, but don't allow it to skip over other points
	let correctedX = point.x;
	
	// If trying to move left (decrease X) and new X is less than lower bound, clamp to lower bound
	if (correctedX < xLowerBound) {
		correctedX = xLowerBound;
	}
	// If trying to move right (increase X) and new X is greater than upper bound, clamp to upper bound
	else if (correctedX > xUpperBound) {
		correctedX = xUpperBound;
	}
	
	correctedX = Math.max(0, Math.min(1, correctedX));
	
	// Validate Y value: must be at least 1% greater than points with smaller X values
	// and at most 1% less than points with larger X values
	const smallerYPoints = allReferencePoints.filter(p => p.x < correctedX);
	const largerYPoints = allReferencePoints.filter(p => p.x > correctedX);
	
	// Y value lower bound: max(smaller_points.y) + 0.01
	const yLowerBound = smallerYPoints.length > 0
		? smallerYPoints.reduce((max, p) => Math.max(max, p.y), 0) + epsilon
		: 0;
	
	// Y value upper bound: min(larger_points.y) - 0.01
	const yUpperBound = largerYPoints.length > 0
		? largerYPoints.reduce((min, p) => Math.min(min, p.y), 1) - epsilon
		: 1;
	
	let correctedY = Math.max(point.y, yLowerBound);
	correctedY = Math.min(correctedY, yUpperBound);
	correctedY = Math.max(0, Math.min(1, correctedY));
	
	return { x: correctedX, y: correctedY };
};


/**
 * Validates and corrects all points to ensure strict monotonicity
 * X values must be at least 1% apart, Y values must be strictly increasing
 */
const validateAllPointsMonotonicity = (points: CurvePoint[]): CurvePoint[] => {
	const sorted = sortCurvePointsByX(points);
	
	const validated: CurvePoint[] = [];
	const xEpsilon = 0.01; // 1% minimum gap for X values
	const yEpsilon = 0.01; // 1% minimum gap for Y values
	
	for (let i = 0; i < sorted.length; i++) {
		const point = sorted[i];
		
		// Validate X value: must be at least 1% greater than previous point's X
		const maxXBefore = validated.length > 0
			? validated.reduce((max, p) => Math.max(max, p.x), 0)
			: 0;
		let correctedX = Math.max(point.x, maxXBefore + xEpsilon);
		correctedX = Math.max(0, Math.min(1, correctedX));
		
		// Validate Y value: must be at least 1% greater than points with smaller X values
		const maxYBefore = validated
			.filter(p => p.x < correctedX)
			.reduce((max, p) => Math.max(max, p.y), 0);
		let correctedY = Math.max(point.y, maxYBefore + yEpsilon);
		correctedY = Math.max(0, Math.min(1, correctedY));
		
		// If X value is the same as previous point, ensure Y is at least 1% greater
		if (validated.length > 0 && Math.abs(correctedX - validated[validated.length - 1].x) < xEpsilon) {
			const prevY = validated[validated.length - 1].y;
			correctedY = Math.max(correctedY, prevY + yEpsilon);
			correctedY = Math.max(0, Math.min(1, correctedY));
			// Also ensure X is at least 1% greater
			correctedX = Math.max(correctedX, validated[validated.length - 1].x + xEpsilon);
			correctedX = Math.max(0, Math.min(1, correctedX));
		}
		
		validated.push({ x: correctedX, y: correctedY });
	}
	
	return validated;
};

/**
 * Converts mouse coordinates to curve point coordinates (relative to origin)
 * All coordinates are relative to (0,0), no mapping applied
 */
const mouseToCurvePoint = (
	mouseX: number,
	mouseY: number,
	canvasWidth: number,
	canvasHeight: number
): CurvePoint => {
	const x = mouseX / canvasWidth;
	const y = 1.0 - (mouseY / canvasHeight);

	return { x: Math.max(0, Math.min(1, x)), y: Math.max(0, Math.min(1, y)) };
};

// ---------------------------------------------------------------------------
// Bezier curve (single cubic, fixed endpoints P0=(0,0) / P3=(1,1), C1/C2 only)
// ---------------------------------------------------------------------------

type BezierControl = { c1x: number; c1y: number; c2x: number; c2y: number };
type BezierControlInput = { c1x: string; c1y: string; c2x: string; c2y: string };

/** Shared bezier preset count (left/right sticks share one list). Matches config.proto max_count. */
const BEZIER_PRESET_COUNT = 4;
/** Matches `BezierPreset.name` max_length in config.proto. */
const BEZIER_PRESET_NAME_MAX_LEN = 64;

const BEZIER_LINEAR_DEFAULT: BezierControl = { c1x: 1 / 3, c1y: 1 / 3, c2x: 2 / 3, c2y: 2 / 3 };

/** Clamp control points to [0,1]^2 and enforce C1 <= C2 component-wise (keeps curve monotonic). */
function clampBezierControl(b: BezierControl): BezierControl {
	const c1x = Math.max(0, Math.min(1, Number.isFinite(b.c1x) ? b.c1x : BEZIER_LINEAR_DEFAULT.c1x));
	const c1y = Math.max(0, Math.min(1, Number.isFinite(b.c1y) ? b.c1y : BEZIER_LINEAR_DEFAULT.c1y));
	const c2x = Math.max(0, Math.min(1, Number.isFinite(b.c2x) ? b.c2x : BEZIER_LINEAR_DEFAULT.c2x));
	const c2y = Math.max(0, Math.min(1, Number.isFinite(b.c2y) ? b.c2y : BEZIER_LINEAR_DEFAULT.c2y));
	return {
		c1x: Math.min(c1x, c2x),
		c1y: Math.min(c1y, c2y),
		c2x: Math.max(c1x, c2x),
		c2y: Math.max(c1y, c2y),
	};
}

function normalizeBezier(raw: unknown): BezierControl {
	if (raw && typeof raw === 'object') {
		const o = raw as Record<string, unknown>;
		return clampBezierControl({
			c1x: Number(o.c1x),
			c1y: Number(o.c1y),
			c2x: Number(o.c2x),
			c2y: Number(o.c2y),
		});
	}
	return { ...BEZIER_LINEAR_DEFAULT };
}

function bezierToInputStrings(b: BezierControl): BezierControlInput {
	return {
		c1x: parseFloat(b.c1x.toFixed(4)).toString(),
		c1y: parseFloat(b.c1y.toFixed(4)).toString(),
		c2x: parseFloat(b.c2x.toFixed(4)).toString(),
		c2y: parseFloat(b.c2y.toFixed(4)).toString(),
	};
}

const areBezierEqual = (a: BezierControl, b: BezierControl) =>
	Math.abs(a.c1x - b.c1x) <= 0.0001 && Math.abs(a.c1y - b.c1y) <= 0.0001 &&
	Math.abs(a.c2x - b.c2x) <= 0.0001 && Math.abs(a.c2y - b.c2y) <= 0.0001;

/** Cubic bezier axis value with endpoints 0 and 1: B(t) = 3(1-t)^2 t c1 + 3(1-t) t^2 c2 + t^3 */
const bezierEvalAxis = (c1: number, c2: number, t: number): number => {
	const mt = 1 - t;
	return 3 * mt * mt * t * c1 + 3 * mt * t * t * c2 + t * t * t;
};

/** dB/dt for the axis polynomial above */
const bezierDerivAxis = (c1: number, c2: number, t: number): number => {
	const mt = 1 - t;
	return 3 * mt * mt * c1 + 6 * mt * t * (c2 - c1) + 3 * t * t * (1 - c2);
};

/** Newton solve x(t) = xTarget with clamped monotonic curve (single root guaranteed). */
const bezierFindT = (xTarget: number, c1x: number, c2x: number, seed = 0.5): number => {
	let t = Math.max(0, Math.min(1, seed));
	for (let i = 0; i < 8; i++) {
		const err = bezierEvalAxis(c1x, c2x, t) - xTarget;
		if (Math.abs(err) < 1e-5) break;
		const d = bezierDerivAxis(c1x, c2x, t);
		if (Math.abs(d) < 1e-6) break;
		t = Math.max(0, Math.min(1, t - err / d));
	}
	return t;
};

type BezierSegment = { p0: CurvePoint; c1: CurvePoint; c2: CurvePoint; p3: CurvePoint };

/** De Casteljau split at t; returns the first half segment (P0 fixed at (0,0), P3 fixed at (1,1)). */
const splitBezierAt = (b: BezierControl, t: number): BezierSegment => {
	const p0: CurvePoint = { x: 0, y: 0 };
	const p1: CurvePoint = { x: b.c1x, y: b.c1y };
	const p2: CurvePoint = { x: b.c2x, y: b.c2y };
	const p3: CurvePoint = { x: 1, y: 1 };
	const lerp = (a: CurvePoint, c: CurvePoint): CurvePoint => ({
		x: a.x + (c.x - a.x) * t,
		y: a.y + (c.y - a.y) * t,
	});
	const q0 = lerp(p0, p1);
	const q1 = lerp(p1, p2);
	const q2 = lerp(p2, p3);
	const r0 = lerp(q0, q1);
	const r1 = lerp(q1, q2);
	const s0 = lerp(r0, r1);
	return { p0, c1: q0, c2: r0, p3: s0 };
};

/**
 * Parse bezier curve JSON file. Accepted shapes:
 * { name, bezier: { c1x, c1y, c2x, c2y } } (name ignored), { c1x, c1y, c2x, c2y }, [c1x, c1y, c2x, c2y].
 * Returns null if invalid (caller shows unified alert).
 */
function parseBezierImportJSON(text: string): BezierControl | null {
	let parsed: unknown;
	try {
		parsed = JSON.parse(text);
	} catch {
		return null;
	}
	const readNums = (vals: unknown[]): BezierControl | null => {
		if (vals.length !== 4) return null;
		const nums = vals.map((v) => Number(v));
		if (!nums.every((n) => Number.isFinite(n))) return null;
		return clampBezierControl({ c1x: nums[0], c1y: nums[1], c2x: nums[2], c2y: nums[3] });
	};
	if (Array.isArray(parsed)) {
		return readNums(parsed);
	}
	if (parsed !== null && typeof parsed === 'object') {
		const o = parsed as Record<string, unknown>;
		if (o.bezier !== undefined) {
			if (o.bezier === null || typeof o.bezier !== 'object' || Array.isArray(o.bezier)) return null;
			const b = o.bezier as Record<string, unknown>;
			return readNums([b.c1x, b.c1y, b.c2x, b.c2y]);
		}
		return readNums([o.c1x, o.c1y, o.c2x, o.c2y]);
	}
	return null;
}

/**
 * Draws bezier curve editor on canvas. Static background (grid / dots / dashed refs / purple mask)
 * is fully reused from the polyline editor via drawCurveStaticBackground.
 */
const drawBezierEditor = (
	ctx: CanvasRenderingContext2D,
	width: number,
	height: number,
	bezier: BezierControl,
	lightX: number | undefined, // Physical distance (sqrt(dist_sq)) as 0-1
	innerDeadzone: number = 0,
	antiDeadzone: number = 0
) => {
	const staticCanvas = drawCurveStaticBackground(width, height, innerDeadzone, antiDeadzone);
	ctx.clearRect(0, 0, width, height);
	ctx.drawImage(staticCanvas, 0, 0);

	const intWidth = Math.round(width);
	const intHeight = Math.round(height);
	const toPx = (x: number) => Math.round(x * intWidth);
	const toPy = (y: number) => Math.round(intHeight - y * intHeight);

	// Handle lines P0 -> C1 and C2 -> P3 (gray dashed)
	ctx.strokeStyle = '#999999';
	ctx.lineWidth = 1;
	ctx.setLineDash([5, 5]);
	ctx.beginPath();
	ctx.moveTo(toPx(0), toPy(0));
	ctx.lineTo(toPx(bezier.c1x), toPy(bezier.c1y));
	ctx.stroke();
	ctx.beginPath();
	ctx.moveTo(toPx(bezier.c2x), toPy(bezier.c2y));
	ctx.lineTo(toPx(1), toPy(1));
	ctx.stroke();
	ctx.setLineDash([]);

	// Main bezier curve (black solid)
	ctx.strokeStyle = '#000000';
	ctx.lineWidth = 2;
	ctx.beginPath();
	ctx.moveTo(toPx(0), toPy(0));
	ctx.bezierCurveTo(toPx(bezier.c1x), toPy(bezier.c1y), toPx(bezier.c2x), toPy(bezier.c2y), toPx(1), toPy(1));
	ctx.stroke();

	// Progress highlight: physical dist -> t -> first-half segment in orange
	if (lightX !== undefined && lightX > 0) {
		const targetX = Math.max(0, Math.min(1, lightX));
		const tParam = bezierFindT(targetX, bezier.c1x, bezier.c2x);
		const seg = splitBezierAt(bezier, tParam);

		ctx.strokeStyle = '#FFA500';
		ctx.lineWidth = 2;
		ctx.beginPath();
		ctx.moveTo(toPx(seg.p0.x), toPy(seg.p0.y));
		ctx.bezierCurveTo(toPx(seg.c1.x), toPy(seg.c1.y), toPx(seg.c2.x), toPy(seg.c2.y), toPx(seg.p3.x), toPy(seg.p3.y));
		ctx.stroke();

		// Progress endpoint dot
		ctx.fillStyle = '#FFA500';
		ctx.beginPath();
		ctx.arc(toPx(seg.p3.x), toPy(seg.p3.y), 5, 0, 2 * Math.PI);
		ctx.fill();

		// Light orange dashed projections to X/Y axes
		ctx.strokeStyle = 'rgba(255, 165, 0, 0.5)';
		ctx.lineWidth = 1;
		ctx.setLineDash([4, 4]);
		ctx.beginPath();
		ctx.moveTo(toPx(seg.p3.x), toPy(seg.p3.y));
		ctx.lineTo(toPx(seg.p3.x), toPy(0));
		ctx.stroke();
		ctx.beginPath();
		ctx.moveTo(toPx(seg.p3.x), toPy(seg.p3.y));
		ctx.lineTo(toPx(0), toPy(seg.p3.y));
		ctx.stroke();
		ctx.setLineDash([]);
	}

	// C1/C2 handles (orange hollow circles)
	const handles: CurvePoint[] = [
		{ x: bezier.c1x, y: bezier.c1y },
		{ x: bezier.c2x, y: bezier.c2y },
	];
	for (const pt of handles) {
		ctx.beginPath();
		ctx.arc(toPx(pt.x), toPy(pt.y), 6, 0, 2 * Math.PI);
		ctx.fillStyle = '#ffffff';
		ctx.fill();
		ctx.strokeStyle = '#FFA500';
		ctx.lineWidth = 2;
		ctx.stroke();
	}
};

/** Collapsed placeholder shown for the curve mode that is not selected. */
const DisabledCurveBar = ({ label }: { label: string }) => (
	<div
		style={{
			width: '100%',
			background: '#ECECEE',
			borderRadius: 8,
			padding: '8px 12px',
			display: 'flex',
			alignItems: 'center',
			justifyContent: 'flex-start',
			fontSize: '0.875rem',
			color: '#666',
		}}
	>
		{label}
	</div>
);

interface JoystickCurveSettingsProps {
	values: AddonPropTypes['values'];
	setFieldValue: AddonPropTypes['setFieldValue'];
	saveMessage?: string;
	onSaveClick?: () => void;
}

type AppContextShape = {
	buttonLabels?: { buttonLabelType?: string; swapTpShareLabels?: boolean };
};

const JoystickCurveSettings = ({
	values,
	setFieldValue,
	saveMessage = '',
	onSaveClick,
}: JoystickCurveSettingsProps) => {
	const { t } = useTranslation();
	const { handleSubmit } = useFormikContext();
	const appContext = useContext(AppContext) as AppContextShape | null;
	
	// Get button label type from AppContext
	const buttonLabelType = appContext?.buttonLabels?.buttonLabelType || 'ps4';
	const swapTpShareLabels = appContext?.buttonLabels?.swapTpShareLabels || false;
	const currentButtonLabels = getButtonLabels(buttonLabelType, swapTpShareLabels);
	
	const [isExpanded, setIsExpanded] = useState(() => {
		// Default to disabled (collapsed) if not set
		return Boolean(values?.joystickCurveEnabled ?? false);
	});

	// Sync isExpanded with values when they change
	useEffect(() => {
		setIsExpanded(Boolean(values?.joystickCurveEnabled ?? false));
	}, [values?.joystickCurveEnabled]);
	
	// State for left stick
	const [leftCurvePoints, setLeftCurvePoints] = useState<CurvePoint[]>(() => {
		const saved = values?.joystickCurvePoints1;
		return Array.isArray(saved) ? saved as CurvePoint[] : [];
	});
	const [leftCurveInputValues, setLeftCurveInputValues] = useState<CurvePointInput[]>(() => {
		const saved = values?.joystickCurvePoints1;
		if (Array.isArray(saved)) {
			return (saved as CurvePoint[]).map(p => ({ 
				x: parseFloat(p.x.toFixed(4)).toString(), 
				y: parseFloat(p.y.toFixed(4)).toString(),
			}));
		}
		return [];
	});
	const [leftDraggingPointIndex, setLeftDraggingPointIndex] = useState<number | null>(null);
	const [leftDragOffset, setLeftDragOffset] = useState<{ x: number; y: number } | null>(null);
	const leftCurveCanvasRef = useRef<HTMLCanvasElement>(null);
	const leftCurveDirtyRef = useRef(false);
	const [leftLightX, setLeftLightX] = useState<number | undefined>(undefined); // Physical distance (sqrt(dist_sq)) as percentage
	const [leftLightY, setLeftLightY] = useState<number | undefined>(undefined); // Output distance (sqrt(stickX*stickX + stickY*stickY)) as percentage
	
	// State for right stick
	const [rightCurvePoints, setRightCurvePoints] = useState<CurvePoint[]>(() => {
		const saved = values?.joystickCurvePoints2;
		return Array.isArray(saved) ? saved as CurvePoint[] : [];
	});
	const [rightCurveInputValues, setRightCurveInputValues] = useState<CurvePointInput[]>(() => {
		const saved = values?.joystickCurvePoints2;
		if (Array.isArray(saved)) {
			return (saved as CurvePoint[]).map(p => ({ 
				x: parseFloat(p.x.toFixed(4)).toString(), 
				y: parseFloat(p.y.toFixed(4)).toString(),
			}));
		}
		return [];
	});
	const [rightDraggingPointIndex, setRightDraggingPointIndex] = useState<number | null>(null);
	const [rightDragOffset, setRightDragOffset] = useState<{ x: number; y: number } | null>(null);
	const rightCurveCanvasRef = useRef<HTMLCanvasElement>(null);
	const rightCurveDirtyRef = useRef(false);
	const [rightLightX, setRightLightX] = useState<number | undefined>(undefined); // Physical distance (sqrt(dist_sq)) as percentage
	const [rightLightY, setRightLightY] = useState<number | undefined>(undefined); // Output distance (sqrt(stickX*stickX + stickY*stickY)) as percentage

	// ---------------- Bezier state (mode 1) ----------------
	// Curve mode: 0 = polyline (existing behavior), 1 = bezier. Derived from formik values.
	const curveMode = Number(values?.joystickCurveMode ?? 0) === 1 ? 1 : 0;

	// Left stick bezier
	const [leftBezier, setLeftBezier] = useState<BezierControl>(() => normalizeBezier(values?.joystickBezier1));
	const [leftBezierInputs, setLeftBezierInputs] = useState<BezierControlInput>(() => bezierToInputStrings(normalizeBezier(values?.joystickBezier1)));
	const leftBezierDirtyRef = useRef(false);
	const leftBezierCanvasRef = useRef<HTMLCanvasElement>(null);
	const [leftDraggingBezier, setLeftDraggingBezier] = useState<'c1' | 'c2' | null>(null);
	const [leftBezierDragOffset, setLeftBezierDragOffset] = useState<{ x: number; y: number } | null>(null);

	// Right stick bezier
	const [rightBezier, setRightBezier] = useState<BezierControl>(() => normalizeBezier(values?.joystickBezier2));
	const [rightBezierInputs, setRightBezierInputs] = useState<BezierControlInput>(() => bezierToInputStrings(normalizeBezier(values?.joystickBezier2)));
	const rightBezierDirtyRef = useRef(false);
	const rightBezierCanvasRef = useRef<HTMLCanvasElement>(null);
	const [rightDraggingBezier, setRightDraggingBezier] = useState<'c1' | 'c2' | null>(null);
	const [rightBezierDragOffset, setRightBezierDragOffset] = useState<{ x: number; y: number } | null>(null);

	const setLeftBezierLocal = (next: BezierControl) => {
		leftBezierDirtyRef.current = true;
		setLeftBezier(next);
	};

	const setRightBezierLocal = (next: BezierControl) => {
		rightBezierDirtyRef.current = true;
		setRightBezier(next);
	};

	// Load bezier control points from values when they change (unless locally dirty)
	useEffect(() => {
		if (leftBezierDirtyRef.current) return;
		const next = normalizeBezier(values?.joystickBezier1);
		setLeftBezier(next);
		setLeftBezierInputs(bezierToInputStrings(next));
	}, [values?.joystickBezier1]);

	useEffect(() => {
		if (rightBezierDirtyRef.current) return;
		const next = normalizeBezier(values?.joystickBezier2);
		setRightBezier(next);
		setRightBezierInputs(bezierToInputStrings(next));
	}, [values?.joystickBezier2]);

	// Keep formik values in sync with local bezier edits
	useEffect(() => {
		if (!leftBezierDirtyRef.current) return;
		const formikBezier = normalizeBezier(values?.joystickBezier1);
		if (!areBezierEqual(leftBezier, formikBezier)) {
			setFieldValue('joystickBezier1', leftBezier);
		} else {
			leftBezierDirtyRef.current = false;
		}
	}, [leftBezier, setFieldValue, values?.joystickBezier1]);

	useEffect(() => {
		if (!rightBezierDirtyRef.current) return;
		const formikBezier = normalizeBezier(values?.joystickBezier2);
		if (!areBezierEqual(rightBezier, formikBezier)) {
			setFieldValue('joystickBezier2', rightBezier);
		} else {
			rightBezierDirtyRef.current = false;
		}
	}, [rightBezier, setFieldValue, values?.joystickBezier2]);

	const setLeftCurvePointsLocal = (nextPoints: CurvePoint[]) => {
		leftCurveDirtyRef.current = true;
		setLeftCurvePoints(nextPoints);
	};
	const setRightCurvePointsLocal = (nextPoints: CurvePoint[]) => {
		rightCurveDirtyRef.current = true;
		setRightCurvePoints(nextPoints);
	};
	
	// Load curve points from values when they change
	useEffect(() => {
		const saved = values?.joystickCurvePoints1;
		if (leftCurveDirtyRef.current) {
			return;
		}
		if (Array.isArray(saved)) {
			setLeftCurvePoints(saved as CurvePoint[]);
			setLeftCurveInputValues((saved as CurvePoint[]).map(p => ({ 
				x: parseFloat(p.x.toFixed(4)).toString(), 
				y: parseFloat(p.y.toFixed(4)).toString(),
			})));
		} else {
			setLeftCurvePoints([]);
			setLeftCurveInputValues([]);
		}
	}, [values?.joystickCurvePoints1]);
	
	useEffect(() => {
		const saved = values?.joystickCurvePoints2;
		if (rightCurveDirtyRef.current) {
			return;
		}
		if (Array.isArray(saved)) {
			setRightCurvePoints(saved as CurvePoint[]);
			setRightCurveInputValues((saved as CurvePoint[]).map(p => ({ 
				x: parseFloat(p.x.toFixed(4)).toString(), 
				y: parseFloat(p.y.toFixed(4)).toString(),
			})));
		} else {
			setRightCurvePoints([]);
			setRightCurveInputValues([]);
		}
	}, [values?.joystickCurvePoints2]);

	// Keep Formik values in sync with current editor points (x ascending for persistence).
	useEffect(() => {
		if (!leftCurveDirtyRef.current) {
			return;
		}
		const sorted = sortCurvePointsByX(leftCurvePoints);
		if (!areCurvePointsEqual(sorted, leftCurvePoints)) {
			setLeftCurvePoints(sorted);
			setLeftCurveInputValues(
				sorted.map((p) => ({
					x: parseFloat(p.x.toFixed(4)).toString(),
					y: parseFloat(p.y.toFixed(4)).toString(),
				})),
			);
			return;
		}
		const formikPoints = Array.isArray(values?.joystickCurvePoints1)
			? (values.joystickCurvePoints1 as CurvePoint[])
			: [];
		if (!areCurvePointsEqual(sorted, formikPoints)) {
			setFieldValue('joystickCurvePoints1', sorted);
		} else {
			leftCurveDirtyRef.current = false;
		}
	}, [leftCurvePoints, setFieldValue, values?.joystickCurvePoints1]);

	useEffect(() => {
		if (!rightCurveDirtyRef.current) {
			return;
		}
		const sorted = sortCurvePointsByX(rightCurvePoints);
		if (!areCurvePointsEqual(sorted, rightCurvePoints)) {
			setRightCurvePoints(sorted);
			setRightCurveInputValues(
				sorted.map((p) => ({
					x: parseFloat(p.x.toFixed(4)).toString(),
					y: parseFloat(p.y.toFixed(4)).toString(),
				})),
			);
			return;
		}
		const formikPoints = Array.isArray(values?.joystickCurvePoints2)
			? (values.joystickCurvePoints2 as CurvePoint[])
			: [];
		if (!areCurvePointsEqual(sorted, formikPoints)) {
			setFieldValue('joystickCurvePoints2', sorted);
		} else {
			rightCurveDirtyRef.current = false;
		}
	}, [rightCurvePoints, setFieldValue, values?.joystickCurvePoints2]);
	
	// Fetch joystick data to get progress ratio for highlight line
	useEffect(() => {
		if (!isExpanded) return;

		let intervalId: ReturnType<typeof setInterval> | null = null;
		
		const fetchJoystickData = async () => {
			// Fetch left stick from unified joystick API.
			try {
				const res = await fetch('/api/getJoystickRaw');
				if (res.ok) {
					const data = await res.json();
					if (data.success) {
						const adcMax1 = Number(data?.adcMax) > 0 ? Number(data.adcMax) : DEFAULT_ADC_MAX;
						const adcCenter1 = adcMax1 / 2.0;
						const centerX = values.joystickCenterX || adcCenter1;
						const centerY = values.joystickCenterY || adcCenter1;
						const rawX = data.x;
						const rawY = data.y;
						const rangeData = values?.joystickRangeData1 || [];

						const { stickX: rawStickX, stickY: rawStickY } = rangeData.length > 0
							? processJoystickData(rawX, rawY, centerX, centerY, rangeData, adcMax1)
							: (() => {
								const offset_center_x = rawX - centerX;
								const offset_center_y = rawY - centerY;
								const stickX = offset_center_x / adcCenter1;
								const stickY = offset_center_y / adcCenter1;
								return { stickX, stickY };
							})();

						const invert1 = values?.analogAdc1Invert ?? 0;
						let stickX = (invert1 === 1 || invert1 === 3) ? -rawStickX : rawStickX;
						let stickY = (invert1 === 2 || invert1 === 3) ? -rawStickY : rawStickY;

						const innerDeadzone = (values?.inner_deadzone || 0) / 100.0;
						const antiDeadzone = (values?.anti_deadzone || 0) / 100.0;
						const dist_sq = stickX * stickX + stickY * stickY;
						const deadzone_sq = innerDeadzone * innerDeadzone;
						const lightX = Math.sqrt(dist_sq);

						if (dist_sq < deadzone_sq) {
							stickX = 0.0;
							stickY = 0.0;
						} else if (antiDeadzone > 0.0) {
							const dist = Math.sqrt(dist_sq);
							const baseline = antiDeadzone;
							const fixedAntiDeadzone = values?.fixed_anti_deadzone || false;

							if (fixedAntiDeadzone) {
								if (dist > 0.0 && dist < baseline) {
									const scale_factor = baseline / dist;
									stickX *= scale_factor;
									stickY *= scale_factor;
								}
							} else if (dist > 0.0) {
								const new_dist = dist + baseline;
								const scale_factor = new_dist / dist;
								stickX *= scale_factor;
								stickY *= scale_factor;
							}
						}

						stickX = Math.max(-1.0, Math.min(1.0, stickX));
						stickY = Math.max(-1.0, Math.min(1.0, stickY));

						const curveEnabled = values?.joystickCurveEnabled ?? false;
						if (curveEnabled && (stickX !== 0.0 || stickY !== 0.0)) {
							const magnitude_sq = stickX * stickX + stickY * stickY;
							if (magnitude_sq > 0.0) {
								const magnitude = Math.sqrt(magnitude_sq);
								let curvedMagnitude = magnitude;
								if (curveMode === 1) {
									const tParam = bezierFindT(Math.min(magnitude, 1), leftBezier.c1x, leftBezier.c2x);
									curvedMagnitude = bezierEvalAxis(leftBezier.c1y, leftBezier.c2y, tParam);
								} else if (leftCurvePoints.length > 0) {
									curvedMagnitude = applyResponseCurve(Math.min(magnitude, 1), leftCurvePoints);
								}
								if (magnitude > 0) {
									const scale = curvedMagnitude / magnitude;
									stickX *= scale;
									stickY *= scale;
								}
							}
						}

						const lightY = Math.sqrt(stickX * stickX + stickY * stickY);
						setLeftLightX(lightX);
						setLeftLightY(lightY);
					}
				}
			} catch (e) {
				// Ignore errors
			}
			
			// Fetch right stick from unified joystick API.
			try {
				const res = await fetch('/api/getJoystickRaw2');
				if (res.ok) {
					const data = await res.json();
					if (data.success) {
						const adcMax2 = Number(data?.adcMax) > 0 ? Number(data.adcMax) : DEFAULT_ADC_MAX;
						const adcCenter2 = adcMax2 / 2.0;
						const centerX = values.joystickCenterX2 || adcCenter2;
						const centerY = values.joystickCenterY2 || adcCenter2;
						const rawX = data.x;
						const rawY = data.y;
						const rangeData = values?.joystickRangeData2 || [];

						const { stickX: rawStickX, stickY: rawStickY } = rangeData.length > 0
							? processJoystickData(rawX, rawY, centerX, centerY, rangeData, adcMax2)
							: (() => {
								const offset_center_x = rawX - centerX;
								const offset_center_y = rawY - centerY;
								const stickX = offset_center_x / adcCenter2;
								const stickY = offset_center_y / adcCenter2;
								return { stickX, stickY };
							})();

						const invert2 = values?.analogAdc2Invert ?? 0;
						let stickX = (invert2 === 1 || invert2 === 3) ? -rawStickX : rawStickX;
						let stickY = (invert2 === 2 || invert2 === 3) ? -rawStickY : rawStickY;

						const innerDeadzone = (values?.inner_deadzone2 || 0) / 100.0;
						const antiDeadzone = (values?.anti_deadzone2 || 0) / 100.0;
						const dist_sq = stickX * stickX + stickY * stickY;
						const deadzone_sq = innerDeadzone * innerDeadzone;
						const lightX = Math.sqrt(dist_sq);

						if (dist_sq < deadzone_sq) {
							stickX = 0.0;
							stickY = 0.0;
						} else if (antiDeadzone > 0.0) {
							const dist = Math.sqrt(dist_sq);
							const baseline = antiDeadzone;
							const fixedAntiDeadzone = values?.fixed_anti_deadzone2 || false;

							if (fixedAntiDeadzone) {
								if (dist > 0.0 && dist < baseline) {
									const scale_factor = baseline / dist;
									stickX *= scale_factor;
									stickY *= scale_factor;
								}
							} else if (dist > 0.0) {
								const new_dist = dist + baseline;
								const scale_factor = new_dist / dist;
								stickX *= scale_factor;
								stickY *= scale_factor;
							}
						}

						stickX = Math.max(-1.0, Math.min(1.0, stickX));
						stickY = Math.max(-1.0, Math.min(1.0, stickY));

						const curveEnabled = values?.joystickCurveEnabled ?? false;
						if (curveEnabled && (stickX !== 0.0 || stickY !== 0.0)) {
							const magnitude_sq = stickX * stickX + stickY * stickY;
							if (magnitude_sq > 0.0) {
								const magnitude = Math.sqrt(magnitude_sq);
								let curvedMagnitude = magnitude;
								if (curveMode === 1) {
									const tParam = bezierFindT(Math.min(magnitude, 1), rightBezier.c1x, rightBezier.c2x);
									curvedMagnitude = bezierEvalAxis(rightBezier.c1y, rightBezier.c2y, tParam);
								} else if (rightCurvePoints.length > 0) {
									curvedMagnitude = applyResponseCurve(Math.min(magnitude, 1), rightCurvePoints);
								}
								if (magnitude > 0) {
									const scale = curvedMagnitude / magnitude;
									stickX *= scale;
									stickY *= scale;
								}
							}
						}

						const lightY = Math.sqrt(stickX * stickX + stickY * stickY);
						setRightLightX(lightX);
						setRightLightY(lightY);
					}
				}
			} catch (e) {
				// Ignore errors
			}
		};
		
		intervalId = setInterval(fetchJoystickData, 100);
		
		return () => {
			if (intervalId) clearInterval(intervalId);
		};
	}, [isExpanded, values.analogAdc1PinX, values.analogAdc1PinY, values.analogAdc2PinX, values.analogAdc2PinY, values.joystickCenterX, values.joystickCenterY, values.joystickCenterX2, values.joystickCenterY2, values.joystickRangeData1, values.joystickRangeData2, values?.analogAdc1Invert, values?.analogAdc2Invert, values?.inner_deadzone, values?.inner_deadzone2, values?.anti_deadzone, values?.anti_deadzone2, values?.fixed_anti_deadzone, values?.fixed_anti_deadzone2, values?.joystickCurveEnabled, curveMode, leftCurvePoints, rightCurvePoints, leftBezier, rightBezier]);
	
	// Draw left curve canvas (optimized with requestAnimationFrame throttling)
	useEffect(() => {
		if (!isExpanded || curveMode !== 0 || !leftCurveCanvasRef.current) return;

		let animationFrameId: number | null = null;
		let lastUpdateTime = 0;
		const TARGET_FPS = 60; // Target 60 FPS
		const FRAME_INTERVAL = 1000 / TARGET_FPS;

		const updateCanvas = (currentTime: number) => {
			// Throttle updates to target FPS (only when lightX/lightY changes frequently)
			if (currentTime - lastUpdateTime < FRAME_INTERVAL && (leftLightX !== undefined || leftLightY !== undefined)) {
				animationFrameId = requestAnimationFrame(updateCanvas);
				return;
			}
			lastUpdateTime = currentTime;

			const ctx = leftCurveCanvasRef.current?.getContext('2d');
			if (ctx) {
				const innerDeadzone = (values?.inner_deadzone || 0) / 100.0;
				const antiDeadzone = (values?.anti_deadzone || 0) / 100.0;
				drawCurveEditor(ctx, 260, 260, leftCurvePoints, leftLightX, leftLightY, innerDeadzone, antiDeadzone);
			}

			// Only continue animation loop if light indicators are active
			if (leftLightX !== undefined || leftLightY !== undefined) {
				animationFrameId = requestAnimationFrame(updateCanvas);
			}
		};

		// Update immediately, then continue if needed
		animationFrameId = requestAnimationFrame(updateCanvas);

		return () => {
			if (animationFrameId !== null) {
				cancelAnimationFrame(animationFrameId);
			}
		};
	}, [isExpanded, curveMode, leftCurvePoints, leftLightX, leftLightY, values?.inner_deadzone, values?.anti_deadzone]);
	
	// Draw right curve canvas (optimized with requestAnimationFrame throttling)
	useEffect(() => {
		if (!isExpanded || curveMode !== 0 || !rightCurveCanvasRef.current) return;

		let animationFrameId: number | null = null;
		let lastUpdateTime = 0;
		const TARGET_FPS = 60; // Target 60 FPS
		const FRAME_INTERVAL = 1000 / TARGET_FPS;

		const updateCanvas = (currentTime: number) => {
			// Throttle updates to target FPS (only when lightX/lightY changes frequently)
			if (currentTime - lastUpdateTime < FRAME_INTERVAL && (rightLightX !== undefined || rightLightY !== undefined)) {
				animationFrameId = requestAnimationFrame(updateCanvas);
				return;
			}
			lastUpdateTime = currentTime;

			const ctx = rightCurveCanvasRef.current?.getContext('2d');
			if (ctx) {
				const innerDeadzone = (values?.inner_deadzone2 || 0) / 100.0;
				const antiDeadzone = (values?.anti_deadzone2 || 0) / 100.0;
				drawCurveEditor(ctx, 260, 260, rightCurvePoints, rightLightX, rightLightY, innerDeadzone, antiDeadzone);
			}

			// Only continue animation loop if light indicators are active
			if (rightLightX !== undefined || rightLightY !== undefined) {
				animationFrameId = requestAnimationFrame(updateCanvas);
			}
		};

		// Update immediately, then continue if needed
		animationFrameId = requestAnimationFrame(updateCanvas);

		return () => {
			if (animationFrameId !== null) {
				cancelAnimationFrame(animationFrameId);
			}
		};
	}, [isExpanded, curveMode, rightCurvePoints, rightLightX, rightLightY, values?.inner_deadzone2, values?.anti_deadzone2]);

	// Draw left bezier canvas (mode 1)
	useEffect(() => {
		if (!isExpanded || curveMode !== 1 || !leftBezierCanvasRef.current) return;

		let animationFrameId: number | null = null;
		let lastUpdateTime = 0;
		const TARGET_FPS = 60;
		const FRAME_INTERVAL = 1000 / TARGET_FPS;

		const updateCanvas = (currentTime: number) => {
			if (currentTime - lastUpdateTime < FRAME_INTERVAL && leftLightX !== undefined) {
				animationFrameId = requestAnimationFrame(updateCanvas);
				return;
			}
			lastUpdateTime = currentTime;

			const ctx = leftBezierCanvasRef.current?.getContext('2d');
			if (ctx) {
				const innerDeadzone = (values?.inner_deadzone || 0) / 100.0;
				const antiDeadzone = (values?.anti_deadzone || 0) / 100.0;
				drawBezierEditor(ctx, 260, 260, leftBezier, leftLightX, innerDeadzone, antiDeadzone);
			}

			if (leftLightX !== undefined) {
				animationFrameId = requestAnimationFrame(updateCanvas);
			}
		};

		animationFrameId = requestAnimationFrame(updateCanvas);

		return () => {
			if (animationFrameId !== null) {
				cancelAnimationFrame(animationFrameId);
			}
		};
	}, [isExpanded, curveMode, leftBezier, leftLightX, values?.inner_deadzone, values?.anti_deadzone]);

	// Draw right bezier canvas (mode 1)
	useEffect(() => {
		if (!isExpanded || curveMode !== 1 || !rightBezierCanvasRef.current) return;

		let animationFrameId: number | null = null;
		let lastUpdateTime = 0;
		const TARGET_FPS = 60;
		const FRAME_INTERVAL = 1000 / TARGET_FPS;

		const updateCanvas = (currentTime: number) => {
			if (currentTime - lastUpdateTime < FRAME_INTERVAL && rightLightX !== undefined) {
				animationFrameId = requestAnimationFrame(updateCanvas);
				return;
			}
			lastUpdateTime = currentTime;

			const ctx = rightBezierCanvasRef.current?.getContext('2d');
			if (ctx) {
				const innerDeadzone = (values?.inner_deadzone2 || 0) / 100.0;
				const antiDeadzone = (values?.anti_deadzone2 || 0) / 100.0;
				drawBezierEditor(ctx, 260, 260, rightBezier, rightLightX, innerDeadzone, antiDeadzone);
			}

			if (rightLightX !== undefined) {
				animationFrameId = requestAnimationFrame(updateCanvas);
			}
		};

		animationFrameId = requestAnimationFrame(updateCanvas);

		return () => {
			if (animationFrameId !== null) {
				cancelAnimationFrame(animationFrameId);
			}
		};
	}, [isExpanded, curveMode, rightBezier, rightLightX, values?.inner_deadzone2, values?.anti_deadzone2]);
	
	// Sync input values with curve points
	useEffect(() => {
		if (leftCurvePoints.length === leftCurveInputValues.length) {
			const needsUpdate = leftCurvePoints.some((p, i) => {
				const inputVal = leftCurveInputValues[i];
				return !inputVal || Math.abs(parseFloat(inputVal.x || '0') - p.x) > 0.0001 || Math.abs(parseFloat(inputVal.y || '0') - p.y) > 0.0001;
			});
			if (needsUpdate) {
				setLeftCurveInputValues(leftCurvePoints.map((p) => ({ 
					x: parseFloat(p.x.toFixed(4)).toString(), 
					y: parseFloat(p.y.toFixed(4)).toString(),
				})));
			}
		} else {
			setLeftCurveInputValues(leftCurvePoints.map((p) => ({ 
				x: parseFloat(p.x.toFixed(4)).toString(), 
				y: parseFloat(p.y.toFixed(4)).toString(),
			})));
		}
	}, [leftCurvePoints]);
	
	useEffect(() => {
		if (rightCurvePoints.length === rightCurveInputValues.length) {
			const needsUpdate = rightCurvePoints.some((p, i) => {
				const inputVal = rightCurveInputValues[i];
				return !inputVal || Math.abs(parseFloat(inputVal.x || '0') - p.x) > 0.0001 || Math.abs(parseFloat(inputVal.y || '0') - p.y) > 0.0001;
			});
			if (needsUpdate) {
				setRightCurveInputValues(rightCurvePoints.map((p) => ({ 
					x: parseFloat(p.x.toFixed(4)).toString(), 
					y: parseFloat(p.y.toFixed(4)).toString(),
				})));
			}
		} else {
			setRightCurveInputValues(rightCurvePoints.map((p) => ({ 
				x: parseFloat(p.x.toFixed(4)).toString(), 
				y: parseFloat(p.y.toFixed(4)).toString(),
			})));
		}
	}, [rightCurvePoints]);
	
	// Handle mouse events for left stick
	const handleLeftMouseDown = (e: React.MouseEvent<HTMLCanvasElement>) => {
		if (!leftCurveCanvasRef.current) return;
		const rect = leftCurveCanvasRef.current.getBoundingClientRect();
		const mouseX = e.clientX - rect.left;
		const mouseY = e.clientY - rect.top;
		const clickedPoint = mouseToCurvePoint(mouseX, mouseY, 260, 260);
		
		// Check if clicking on existing point (use stored coordinates directly, no mapping)
		// Control points are displayed at their stored coordinates relative to (0,0)
		const threshold = 10;
		for (let i = 0; i < leftCurvePoints.length; i++) {
			const point = leftCurvePoints[i];
			// Control points are stored and displayed relative to (0,0) without any mapping
			if (point.x >= 0 && point.x <= 1 && point.y >= 0 && point.y <= 1) {
				const px = point.x * 260;
				const py = 260 - point.y * 260;
				const dist = Math.sqrt(Math.pow(mouseX - px, 2) + Math.pow(mouseY - py, 2));
				if (dist < threshold) {
					// Record offset between mouse and control point
					setLeftDragOffset({ x: mouseX - px, y: mouseY - py });
					setLeftDraggingPointIndex(i);
					return;
				}
			}
		}
		
		// Add new point up to CURVE_POINT_COUNT intermediate points
		if (leftCurvePoints.length < CURVE_POINT_COUNT) {
			const innerDeadzone = (values?.inner_deadzone || 0) / 100.0;
			const antiDeadzone = (values?.anti_deadzone || 0) / 100.0;
			const startPoint = innerDeadzone > 0 || antiDeadzone > 0 ? { x: innerDeadzone, y: antiDeadzone } : { x: 0, y: 0 };
			const allReferencePoints = [startPoint, ...leftCurvePoints, { x: 1, y: 1 }];
			const validatedPoint = validatePointMonotonicity(clickedPoint, allReferencePoints);
			const allPoints = [...leftCurvePoints, validatedPoint];
			const validatedAll = validateAllPointsMonotonicity(allPoints);
			setLeftCurvePointsLocal(validatedAll);
			// Don't auto-enter drag mode after add; user can drag with next click.
			setLeftDragOffset(null);
			setLeftDraggingPointIndex(null);
		}
	};
	
	const handleLeftMouseMove = (e: React.MouseEvent<HTMLCanvasElement>) => {
		if (leftDraggingPointIndex === null || !leftCurveCanvasRef.current) return;
		const rect = leftCurveCanvasRef.current.getBoundingClientRect();
		const mouseX = e.clientX - rect.left;
		const mouseY = e.clientY - rect.top;
		// Apply drag offset to maintain relative position between mouse and control point
		const adjustedMouseX = leftDragOffset ? mouseX - leftDragOffset.x : mouseX;
		const adjustedMouseY = leftDragOffset ? mouseY - leftDragOffset.y : mouseY;
		const newPoint = mouseToCurvePoint(adjustedMouseX, adjustedMouseY, 260, 260);
		
		// Validate point with upper and lower bounds (no pushing other points)
		// Include start point (deadzone, anti-deadzone) as a reference point
		const innerDeadzone = (values?.inner_deadzone || 0) / 100.0;
		const antiDeadzone = (values?.anti_deadzone || 0) / 100.0;
		const startPoint = innerDeadzone > 0 || antiDeadzone > 0 ? { x: innerDeadzone, y: antiDeadzone } : { x: 0, y: 0 };
		const otherPoints = leftCurvePoints.filter((_, i) => i !== leftDraggingPointIndex);
		const allReferencePoints = [startPoint, ...otherPoints, { x: 1, y: 1 }]; // Include start and end points
		const currentPoint = leftCurvePoints[leftDraggingPointIndex];
		const validatedPoint = validatePointMonotonicity(newPoint, allReferencePoints, currentPoint?.x);
		const updatedPoints = [...leftCurvePoints];
		updatedPoints[leftDraggingPointIndex] = validatedPoint;
		// Don't call validateAllPointsMonotonicity during dragging to avoid pushing other points
		setLeftCurvePointsLocal(updatedPoints);
	};
	
	const handleLeftMouseUp = () => {
		setLeftDraggingPointIndex(null);
		setLeftDragOffset(null);
	};
	
	const handleLeftMouseLeave = () => {
		setLeftDraggingPointIndex(null);
		setLeftDragOffset(null);
	};
	
	// Handle mouse events for right stick
	const handleRightMouseDown = (e: React.MouseEvent<HTMLCanvasElement>) => {
		if (!rightCurveCanvasRef.current) return;
		const rect = rightCurveCanvasRef.current.getBoundingClientRect();
		const mouseX = e.clientX - rect.left;
		const mouseY = e.clientY - rect.top;
		const clickedPoint = mouseToCurvePoint(mouseX, mouseY, 260, 260);
		
		const threshold = 10;
		for (let i = 0; i < rightCurvePoints.length; i++) {
			const point = rightCurvePoints[i];
			// Control points are stored and displayed relative to (0,0) without any mapping
			if (point.x >= 0 && point.x <= 1 && point.y >= 0 && point.y <= 1) {
				const px = point.x * 260;
				const py = 260 - point.y * 260;
				const dist = Math.sqrt(Math.pow(mouseX - px, 2) + Math.pow(mouseY - py, 2));
				if (dist < threshold) {
					// Record offset between mouse and control point
					setRightDragOffset({ x: mouseX - px, y: mouseY - py });
					setRightDraggingPointIndex(i);
					return;
				}
			}
		}
		
		if (rightCurvePoints.length < CURVE_POINT_COUNT) {
			const innerDeadzone = (values?.inner_deadzone2 || 0) / 100.0;
			const antiDeadzone = (values?.anti_deadzone2 || 0) / 100.0;
			const startPoint = innerDeadzone > 0 || antiDeadzone > 0 ? { x: innerDeadzone, y: antiDeadzone } : { x: 0, y: 0 };
			const allReferencePoints = [startPoint, ...rightCurvePoints, { x: 1, y: 1 }];
			const validatedPoint = validatePointMonotonicity(clickedPoint, allReferencePoints);
			const allPoints = [...rightCurvePoints, validatedPoint];
			const validatedAll = validateAllPointsMonotonicity(allPoints);
			setRightCurvePointsLocal(validatedAll);
			// Don't auto-enter drag mode after add; user can drag with next click.
			setRightDragOffset(null);
			setRightDraggingPointIndex(null);
		}
	};
	
	const handleRightMouseMove = (e: React.MouseEvent<HTMLCanvasElement>) => {
		if (rightDraggingPointIndex === null || !rightCurveCanvasRef.current) return;
		const rect = rightCurveCanvasRef.current.getBoundingClientRect();
		const mouseX = e.clientX - rect.left;
		const mouseY = e.clientY - rect.top;
		// Apply drag offset to maintain relative position between mouse and control point
		const adjustedMouseX = rightDragOffset ? mouseX - rightDragOffset.x : mouseX;
		const adjustedMouseY = rightDragOffset ? mouseY - rightDragOffset.y : mouseY;
		const newPoint = mouseToCurvePoint(adjustedMouseX, adjustedMouseY, 260, 260);
		
		// Validate point with upper and lower bounds (no pushing other points)
		// Include start point (deadzone, anti-deadzone) as a reference point
		const innerDeadzone = (values?.inner_deadzone2 || 0) / 100.0;
		const antiDeadzone = (values?.anti_deadzone2 || 0) / 100.0;
		const startPoint = innerDeadzone > 0 || antiDeadzone > 0 ? { x: innerDeadzone, y: antiDeadzone } : { x: 0, y: 0 };
		const otherPoints = rightCurvePoints.filter((_, i) => i !== rightDraggingPointIndex);
		const allReferencePoints = [startPoint, ...otherPoints, { x: 1, y: 1 }]; // Include start and end points
		const currentPoint = rightCurvePoints[rightDraggingPointIndex];
		const validatedPoint = validatePointMonotonicity(newPoint, allReferencePoints, currentPoint?.x);
		const updatedPoints = [...rightCurvePoints];
		updatedPoints[rightDraggingPointIndex] = validatedPoint;
		// Don't call validateAllPointsMonotonicity during dragging to avoid pushing other points
		setRightCurvePointsLocal(updatedPoints);
	};
	
	const handleRightMouseUp = () => {
		setRightDraggingPointIndex(null);
		setRightDragOffset(null);
	};
	
	const handleRightMouseLeave = () => {
		setRightDraggingPointIndex(null);
		setRightDragOffset(null);
	};
	
	// Handle input changes for left stick
	const handleLeftInputChange = (index: number, field: 'x' | 'y', value: string) => {
		const updated = [...leftCurveInputValues];
		updated[index] = { ...updated[index], [field]: value };
		setLeftCurveInputValues(updated);
	};
	
	const handleLeftInputBlur = (index: number) => {
		const inputVal = leftCurveInputValues[index];
		if (!inputVal) return;
		const x = Math.max(0, Math.min(1, parseFloat(inputVal.x) || 0));
		const y = Math.max(0, Math.min(1, parseFloat(inputVal.y) || 0));
		const point: CurvePoint = { x, y };
		const otherPoints = leftCurvePoints.filter((_, i) => i !== index);
		const currentPoint = leftCurvePoints[index];
		const innerDeadzone = (values?.inner_deadzone || 0) / 100.0;
		const antiDeadzone = (values?.anti_deadzone || 0) / 100.0;
		const startPoint = innerDeadzone > 0 || antiDeadzone > 0 ? { x: innerDeadzone, y: antiDeadzone } : { x: 0, y: 0 };
		const allReferencePoints = [startPoint, ...otherPoints, { x: 1, y: 1 }];
		const validatedPoint = validatePointMonotonicity(point, allReferencePoints, currentPoint?.x);
		const updatedPoints = [...leftCurvePoints];
		updatedPoints[index] = validatedPoint;
		const validatedAll = validateAllPointsMonotonicity(updatedPoints);
		setLeftCurvePointsLocal(validatedAll);
		// Update input values with formatted display (4 decimal places)
		const updatedInputValues = [...leftCurveInputValues];
		updatedInputValues[index] = { 
			x: parseFloat(validatedAll[index].x.toFixed(4)).toString(), 
			y: parseFloat(validatedAll[index].y.toFixed(4)).toString(),
		};
		setLeftCurveInputValues(updatedInputValues);
	};
	
	// Handle input changes for right stick
	const handleRightInputChange = (index: number, field: 'x' | 'y', value: string) => {
		const updated = [...rightCurveInputValues];
		updated[index] = { ...updated[index], [field]: value };
		setRightCurveInputValues(updated);
	};
	
	const handleRightInputBlur = (index: number) => {
		const inputVal = rightCurveInputValues[index];
		if (!inputVal) return;
		const x = Math.max(0, Math.min(1, parseFloat(inputVal.x) || 0));
		const y = Math.max(0, Math.min(1, parseFloat(inputVal.y) || 0));
		const point: CurvePoint = { x, y };
		const otherPoints = rightCurvePoints.filter((_, i) => i !== index);
		const currentPoint = rightCurvePoints[index];
		const innerDeadzone = (values?.inner_deadzone2 || 0) / 100.0;
		const antiDeadzone = (values?.anti_deadzone2 || 0) / 100.0;
		const startPoint = innerDeadzone > 0 || antiDeadzone > 0 ? { x: innerDeadzone, y: antiDeadzone } : { x: 0, y: 0 };
		const allReferencePoints = [startPoint, ...otherPoints, { x: 1, y: 1 }];
		const validatedPoint = validatePointMonotonicity(point, allReferencePoints, currentPoint?.x);
		const updatedPoints = [...rightCurvePoints];
		updatedPoints[index] = validatedPoint;
		const validatedAll = validateAllPointsMonotonicity(updatedPoints);
		setRightCurvePointsLocal(validatedAll);
		// Update input values with formatted display (4 decimal places)
		const updatedInputValues = [...rightCurveInputValues];
		updatedInputValues[index] = { 
			x: parseFloat(validatedAll[index].x.toFixed(4)).toString(), 
			y: parseFloat(validatedAll[index].y.toFixed(4)).toString(),
		};
		setRightCurveInputValues(updatedInputValues);
	};
	
	// Handle delete for left stick
	const handleLeftDelete = (index: number) => {
		const updated = leftCurvePoints.filter((_, i) => i !== index);
		setLeftCurvePointsLocal(updated);
		setLeftCurveInputValues(updated.map(p => ({ 
			x: parseFloat(p.x.toFixed(4)).toString(), 
			y: parseFloat(p.y.toFixed(4)).toString(),
		})));
	};
	
	// Handle delete for right stick
	const handleRightDelete = (index: number) => {
		const updated = rightCurvePoints.filter((_, i) => i !== index);
		setRightCurvePointsLocal(updated);
		setRightCurveInputValues(updated.map(p => ({ 
			x: parseFloat(p.x.toFixed(4)).toString(), 
			y: parseFloat(p.y.toFixed(4)).toString(),
		})));
	};
	
	// Preset state management
	type PresetInput = {
		name: string;
		points: Array<{ x: string; y: string }>;
		activationButtonMask?: number;
	};
	
	const [presetInputs, setPresetInputs] = useState<PresetInput[]>(() =>
		Array.from({ length: CURVE_PRESET_COUNT }, () => ({
			name: '',
			points: emptyPresetPointSlots(),
			activationButtonMask: 0,
		}))
	);
	
	// Load presets from values on mount and when values change
	useEffect(() => {
		const presets =
			(values?.joystickCurvePresets as Array<{
				name: string;
				points: CurvePoint[];
				activationButtonMask?: number;
			}>) || [];
		
		const loadPreset = (presetIndex: number) => {
			if (presetIndex < presets.length) {
				const preset = presets[presetIndex];
				const name = sanitizePresetNameForPreset(preset.name || '');
				const points = preset.points || [];
				const activationButtonMask = preset.activationButtonMask ?? 0;
				
				const presetPoints = emptyPresetPointSlots();
				
				for (let i = 0; i < CURVE_POINT_COUNT; i++) {
					if (i < points.length) {
						const px = Number(points[i]?.x);
						const py = Number(points[i]?.y);
						if (!Number.isFinite(px) || !Number.isFinite(py)) continue;
						presetPoints[i] = {
							x: parseFloat(px.toFixed(4)).toString(),
							y: parseFloat(py.toFixed(4)).toString(),
						};
					}
				}
				
				return { name, points: presetPoints, activationButtonMask };
			}
			
			return { name: '', points: emptyPresetPointSlots(), activationButtonMask: 0 };
		};
		
		setPresetInputs(Array.from({ length: CURVE_PRESET_COUNT }, (_, i) => loadPreset(i)));
	}, [values?.joystickCurvePresets]);
	
	// Handle preset name change
	const handlePresetNameChange = (presetIndex: number, value: string) => {
		const newPresets = [...presetInputs];
		newPresets[presetIndex].name = sanitizePresetNameWhileTyping(value);
		setPresetInputs(newPresets);

		setFieldValue('joystickCurvePresets', joystickCurvePresetsSlotsForFormik(newPresets));
	};
	
	// Helper function to check if activation button mask is already used by another preset
	const isActivationButtonMaskUsed = (presetIndex: number, buttonMask: number, presetInputsToCheck: PresetInput[]): boolean => {
		// Allow multiple presets to use 0 (NONE)
		if (buttonMask === 0) {
			return false;
		}
		
		// Check if buttonMask is used by any other preset (excluding current preset)
		for (let i = 0; i < presetInputsToCheck.length; i++) {
			if (i !== presetIndex) {
				const otherButtonMask = presetInputsToCheck[i].activationButtonMask ?? 0;
				if (otherButtonMask === buttonMask) {
					return true;
				}
			}
		}
		
		return false;
	};
	
	// Helper function to save preset to formik
	const savePresetToFormik = (presetInputsToSave: PresetInput[]) => {
		setFieldValue('joystickCurvePresets', joystickCurvePresetsSlotsForFormik(presetInputsToSave));
	};

	const polylineImportFileRefs = useRef<Array<HTMLInputElement | null>>([]);

	const showPresetImportFormatError = () => {
		alert(t('CalibrationSettings:hml-preset-import-invalid-format'));
	};

	// Apply string-slot points to a stick canvas (shared by preset apply + JSON file import)
	const applyStringPointsToStick = (stickIndex: number, slots: Array<{ x: string; y: string }>) => {
		const points: CurvePoint[] = [];
		for (let i = 0; i < slots.length; i++) {
			const x = parseFloat(slots[i].x) || 0;
			const y = parseFloat(slots[i].y) || 0;
			if (x > 0 || y > 0) {
				points.push({
					x: Math.max(0, Math.min(1, x)),
					y: Math.max(0, Math.min(1, y)),
				});
			}
		}
		const validated = validateAllPointsMonotonicity(sortCurvePointsByX(points));
		const inputValues = validated.map(p => ({
			x: parseFloat(p.x.toFixed(4)).toString(),
			y: parseFloat(p.y.toFixed(4)).toString(),
		}));
		if (stickIndex === 0) {
			setLeftCurvePointsLocal(validated);
			setLeftCurveInputValues(inputValues);
		} else {
			setRightCurvePointsLocal(validated);
			setRightCurveInputValues(inputValues);
		}
	};

	const handlePolylineImportClick = (stickIndex: number) => {
		polylineImportFileRefs.current[stickIndex]?.click();
	};

	const handlePolylineImportFileChange = (stickIndex: number, e: ChangeEvent<HTMLInputElement>) => {
		const inputEl = e.target;
		const file = inputEl.files?.[0];
		inputEl.value = '';
		if (!file) return;

		const reader = new FileReader();
		reader.onload = () => {
			const text = typeof reader.result === 'string' ? reader.result : '';
			const parsed = parseCurvePresetImportJSON(text);
			if (!parsed) {
				showPresetImportFormatError();
				return;
			}
			applyStringPointsToStick(stickIndex, parsed.points);
		};
		reader.onerror = () => {
			showPresetImportFormatError();
		};
		reader.readAsText(file);
	};

	const handlePresetActivationSelectChange = (presetIndex: number, e: ChangeEvent<HTMLSelectElement>) => {
		const newPresets = [...presetInputs];
		const buttonMask = parseInt(e.target.value, 10);
		const finalButtonMask = Number.isNaN(buttonMask) ? 0 : buttonMask;

		const tempPresets = [...newPresets];
		tempPresets[presetIndex].activationButtonMask = finalButtonMask;

		if (isActivationButtonMaskUsed(presetIndex, finalButtonMask, tempPresets)) {
			alert(t('CalibrationSettings:hml-alert-preset-key-in-use'));
			return;
		}

		newPresets[presetIndex].activationButtonMask = finalButtonMask;
		setPresetInputs(newPresets);
		savePresetToFormik(newPresets);
	};
	
	// Handle apply preset to left/right stick
	const handleApplyPresetToLeft = (presetIndex: number) => {
		applyStringPointsToStick(0, presetInputs[presetIndex].points);
	};

	const handleApplyPresetToRight = (presetIndex: number) => {
		applyStringPointsToStick(1, presetInputs[presetIndex].points);
	};

	// Import current stick canvas curve into preset slot (name unchanged)
	const handlePresetImportFromStick = (presetIndex: number, stickIndex: number) => {
		const canvasPoints = sortCurvePointsByX(stickIndex === 0 ? leftCurvePoints : rightCurvePoints);
		const slots = emptyPresetPointSlots();
		for (let i = 0; i < canvasPoints.length && i < CURVE_POINT_COUNT; i++) {
			slots[i] = {
				x: parseFloat(canvasPoints[i].x.toFixed(4)).toString(),
				y: parseFloat(canvasPoints[i].y.toFixed(4)).toString(),
			};
		}
		setPresetInputs((prev) => {
			const newPresets = [...prev];
			newPresets[presetIndex] = { ...newPresets[presetIndex], points: slots };
			savePresetToFormik(newPresets);
			return newPresets;
		});
	};

	// ---------------- Bezier canvas mouse handlers (mode 1) ----------------
	const handleLeftBezierMouseDown = (e: React.MouseEvent<HTMLCanvasElement>) => {
		if (!leftBezierCanvasRef.current) return;
		const rect = leftBezierCanvasRef.current.getBoundingClientRect();
		const mouseX = e.clientX - rect.left;
		const mouseY = e.clientY - rect.top;
		const threshold = 10;
		const handles: Array<{ which: 'c1' | 'c2'; x: number; y: number }> = [
			{ which: 'c2', x: leftBezier.c2x, y: leftBezier.c2y },
			{ which: 'c1', x: leftBezier.c1x, y: leftBezier.c1y },
		];
		for (const h of handles) {
			const px = h.x * 260;
			const py = 260 - h.y * 260;
			const dist = Math.sqrt(Math.pow(mouseX - px, 2) + Math.pow(mouseY - py, 2));
			if (dist < threshold) {
				setLeftBezierDragOffset({ x: mouseX - px, y: mouseY - py });
				setLeftDraggingBezier(h.which);
				return;
			}
		}
	};

	const handleLeftBezierMouseMove = (e: React.MouseEvent<HTMLCanvasElement>) => {
		if (leftDraggingBezier === null || !leftBezierCanvasRef.current) return;
		const rect = leftBezierCanvasRef.current.getBoundingClientRect();
		const mouseX = e.clientX - rect.left;
		const mouseY = e.clientY - rect.top;
		const adjustedMouseX = leftBezierDragOffset ? mouseX - leftBezierDragOffset.x : mouseX;
		const adjustedMouseY = leftBezierDragOffset ? mouseY - leftBezierDragOffset.y : mouseY;
		const newPoint = mouseToCurvePoint(adjustedMouseX, adjustedMouseY, 260, 260);
		const next: BezierControl = leftDraggingBezier === 'c1'
			? { ...leftBezier, c1x: newPoint.x, c1y: newPoint.y }
			: { ...leftBezier, c2x: newPoint.x, c2y: newPoint.y };
		const clamped = clampBezierControl(next);
		setLeftBezierLocal(clamped);
		setLeftBezierInputs(bezierToInputStrings(clamped));
	};

	const handleLeftBezierMouseUp = () => {
		setLeftDraggingBezier(null);
		setLeftBezierDragOffset(null);
	};

	const handleRightBezierMouseDown = (e: React.MouseEvent<HTMLCanvasElement>) => {
		if (!rightBezierCanvasRef.current) return;
		const rect = rightBezierCanvasRef.current.getBoundingClientRect();
		const mouseX = e.clientX - rect.left;
		const mouseY = e.clientY - rect.top;
		const threshold = 10;
		const handles: Array<{ which: 'c1' | 'c2'; x: number; y: number }> = [
			{ which: 'c2', x: rightBezier.c2x, y: rightBezier.c2y },
			{ which: 'c1', x: rightBezier.c1x, y: rightBezier.c1y },
		];
		for (const h of handles) {
			const px = h.x * 260;
			const py = 260 - h.y * 260;
			const dist = Math.sqrt(Math.pow(mouseX - px, 2) + Math.pow(mouseY - py, 2));
			if (dist < threshold) {
				setRightBezierDragOffset({ x: mouseX - px, y: mouseY - py });
				setRightDraggingBezier(h.which);
				return;
			}
		}
	};

	const handleRightBezierMouseMove = (e: React.MouseEvent<HTMLCanvasElement>) => {
		if (rightDraggingBezier === null || !rightBezierCanvasRef.current) return;
		const rect = rightBezierCanvasRef.current.getBoundingClientRect();
		const mouseX = e.clientX - rect.left;
		const mouseY = e.clientY - rect.top;
		const adjustedMouseX = rightBezierDragOffset ? mouseX - rightBezierDragOffset.x : mouseX;
		const adjustedMouseY = rightBezierDragOffset ? mouseY - rightBezierDragOffset.y : mouseY;
		const newPoint = mouseToCurvePoint(adjustedMouseX, adjustedMouseY, 260, 260);
		const next: BezierControl = rightDraggingBezier === 'c1'
			? { ...rightBezier, c1x: newPoint.x, c1y: newPoint.y }
			: { ...rightBezier, c2x: newPoint.x, c2y: newPoint.y };
		const clamped = clampBezierControl(next);
		setRightBezierLocal(clamped);
		setRightBezierInputs(bezierToInputStrings(clamped));
	};

	const handleRightBezierMouseUp = () => {
		setRightDraggingBezier(null);
		setRightBezierDragOffset(null);
	};

	// Bezier control point input handlers (blur commits with clamping)
	const handleLeftBezierInputChange = (field: keyof BezierControlInput, value: string) => {
		setLeftBezierInputs((prev) => ({ ...prev, [field]: value }));
	};

	const handleLeftBezierInputBlur = () => {
		const clamped = clampBezierControl({
			c1x: parseFloat(leftBezierInputs.c1x) || 0,
			c1y: parseFloat(leftBezierInputs.c1y) || 0,
			c2x: parseFloat(leftBezierInputs.c2x) || 0,
			c2y: parseFloat(leftBezierInputs.c2y) || 0,
		});
		setLeftBezierLocal(clamped);
		setLeftBezierInputs(bezierToInputStrings(clamped));
	};

	const handleRightBezierInputChange = (field: keyof BezierControlInput, value: string) => {
		setRightBezierInputs((prev) => ({ ...prev, [field]: value }));
	};

	const handleRightBezierInputBlur = () => {
		const clamped = clampBezierControl({
			c1x: parseFloat(rightBezierInputs.c1x) || 0,
			c1y: parseFloat(rightBezierInputs.c1y) || 0,
			c2x: parseFloat(rightBezierInputs.c2x) || 0,
			c2y: parseFloat(rightBezierInputs.c2y) || 0,
		});
		setRightBezierLocal(clamped);
		setRightBezierInputs(bezierToInputStrings(clamped));
	};

	// Bezier curve JSON file import (writes into the stick canvas curve, not presets)
	const bezierImportFileRefs = useRef<Array<HTMLInputElement | null>>([]);

	const handleBezierImportClick = (stickIndex: number) => {
		bezierImportFileRefs.current[stickIndex]?.click();
	};

	const handleBezierImportFileChange = (stickIndex: number, e: ChangeEvent<HTMLInputElement>) => {
		const inputEl = e.target;
		const file = inputEl.files?.[0];
		inputEl.value = '';
		if (!file) return;

		const reader = new FileReader();
		reader.onload = () => {
			const text = typeof reader.result === 'string' ? reader.result : '';
			const parsed = parseBezierImportJSON(text);
			if (!parsed) {
				showPresetImportFormatError();
				return;
			}
			if (stickIndex === 0) {
				setLeftBezierLocal(parsed);
				setLeftBezierInputs(bezierToInputStrings(parsed));
			} else {
				setRightBezierLocal(parsed);
				setRightBezierInputs(bezierToInputStrings(parsed));
			}
		};
		reader.onerror = () => {
			showPresetImportFormatError();
		};
		reader.readAsText(file);
	};

	// ---------------- Bezier presets (shared by both sticks, 4 slots) ----------------
	type BezierPresetInput = {
		name: string;
		bezier: BezierControl;
		activationButtonMask: number;
	};

	const emptyBezierPresetSlots = (): BezierPresetInput[] =>
		Array.from({ length: BEZIER_PRESET_COUNT }, () => ({
			name: '',
			bezier: { ...BEZIER_LINEAR_DEFAULT },
			activationButtonMask: 0,
		}));

	const [bezierPresetInputs, setBezierPresetInputs] = useState<BezierPresetInput[]>(emptyBezierPresetSlots);

	/** Serialize preset slots for formik/firmware. Always length BEZIER_PRESET_COUNT so UI index matches API index. */
	const bezierPresetsSlotsForFormik = (slots: BezierPresetInput[]) =>
		Array.from({ length: BEZIER_PRESET_COUNT }, (_, i) => {
			const slot = slots[i];
			return {
				name: sanitizePresetNameForPreset(slot?.name ?? ''),
				bezier: clampBezierControl(slot?.bezier ?? { ...BEZIER_LINEAR_DEFAULT }),
				activationButtonMask: slot?.activationButtonMask ?? 0,
			};
		});

	// Load bezier presets from values on mount and when values change
	useEffect(() => {
		const presets =
			(values?.joystickBezierPresets as Array<{
				name: string;
				bezier: BezierControl;
				activationButtonMask?: number;
			}>) || [];
		setBezierPresetInputs(
			Array.from({ length: BEZIER_PRESET_COUNT }, (_, i) => {
				const preset = presets[i];
				if (!preset) {
					return { name: '', bezier: { ...BEZIER_LINEAR_DEFAULT }, activationButtonMask: 0 };
				}
				return {
					name: sanitizePresetNameForPreset(preset.name || ''),
					bezier: normalizeBezier(preset.bezier),
					activationButtonMask: preset.activationButtonMask ?? 0,
				};
			}),
		);
	}, [values?.joystickBezierPresets]);

	const saveBezierPresetsToFormik = (slots: BezierPresetInput[]) => {
		setFieldValue('joystickBezierPresets', bezierPresetsSlotsForFormik(slots));
	};

	const handleBezierPresetNameChange = (presetIndex: number, value: string) => {
		const newPresets = [...bezierPresetInputs];
		newPresets[presetIndex] = { ...newPresets[presetIndex], name: sanitizePresetNameWhileTyping(value) };
		setBezierPresetInputs(newPresets);
		saveBezierPresetsToFormik(newPresets);
	};

	// Apply preset bezier to left/right stick canvas (marks the stick curve dirty)
	const handleApplyBezierPresetToLeft = (presetIndex: number) => {
		const preset = bezierPresetInputs[presetIndex];
		if (!preset) return;
		const clamped = clampBezierControl(preset.bezier);
		setLeftBezierLocal(clamped);
		setLeftBezierInputs(bezierToInputStrings(clamped));
	};

	const handleApplyBezierPresetToRight = (presetIndex: number) => {
		const preset = bezierPresetInputs[presetIndex];
		if (!preset) return;
		const clamped = clampBezierControl(preset.bezier);
		setRightBezierLocal(clamped);
		setRightBezierInputs(bezierToInputStrings(clamped));
	};

	// Import stick canvas curve into preset slot (no file dialog; name unchanged)
	const handleBezierPresetImportLeft = (presetIndex: number) => {
		const next = [...bezierPresetInputs];
		next[presetIndex] = { ...next[presetIndex], bezier: clampBezierControl(leftBezier) };
		setBezierPresetInputs(next);
		saveBezierPresetsToFormik(next);
	};

	const handleBezierPresetImportRight = (presetIndex: number) => {
		const next = [...bezierPresetInputs];
		next[presetIndex] = { ...next[presetIndex], bezier: clampBezierControl(rightBezier) };
		setBezierPresetInputs(next);
		saveBezierPresetsToFormik(next);
	};

	const isBezierActivationButtonMaskUsed = (presetIndex: number, buttonMask: number, slots: BezierPresetInput[]): boolean => {
		if (buttonMask === 0) return false;
		for (let i = 0; i < slots.length; i++) {
			if (i !== presetIndex && (slots[i].activationButtonMask ?? 0) === buttonMask) {
				return true;
			}
		}
		return false;
	};

	const handleBezierPresetActivationSelectChange = (presetIndex: number, e: ChangeEvent<HTMLSelectElement>) => {
		const buttonMask = parseInt(e.target.value, 10);
		const finalButtonMask = Number.isNaN(buttonMask) ? 0 : buttonMask;
		const newPresets = [...bezierPresetInputs];
		newPresets[presetIndex] = { ...newPresets[presetIndex], activationButtonMask: finalButtonMask };

		if (isBezierActivationButtonMaskUsed(presetIndex, finalButtonMask, newPresets)) {
			alert(t('CalibrationSettings:hml-alert-preset-key-in-use'));
			return;
		}

		setBezierPresetInputs(newPresets);
		saveBezierPresetsToFormik(newPresets);
	};

	return (
		<Section title={t('CalibrationSettings:hml-stick-curve-title')}>
		{isExpanded && (
			<>
			{curveMode === 0 ? (
			<div className="curve-polyline-container">
			<div className="mb-3" style={{ display: 'grid', gridTemplateColumns: 'repeat(4, 260px)', gap: '16px', justifyContent: 'center', alignItems: 'start', width: 'max-content', margin: '0 auto' }}>
				{/* Row 1, Column 1: Left stick curve canvas */}
				<div className="text-center" style={{ display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'flex-start', width: '260px' }}>
					<div style={{ position: 'relative', width: '260px', height: '260px' }}>
						<canvas
							ref={leftCurveCanvasRef}
							width={260}
							height={260}
							style={{
								border: '1px solid #ccc',
								borderRadius: '4px',
								cursor: 'crosshair',
								display: 'block'
							}}
							onMouseDown={handleLeftMouseDown}
							onMouseMove={handleLeftMouseMove}
							onMouseUp={handleLeftMouseUp}
							onMouseLeave={handleLeftMouseLeave}
						/>
					</div>
				</div>

				{/* Row 1, Column 2: Left stick control points */}
				<div style={{ width: '260px', minHeight: '260px', display: 'flex', flexDirection: 'column', justifyContent: 'space-between', alignItems: 'flex-start' }}>
					<div style={{ width: '260px', textAlign: 'left' }}>
						<div style={{ fontWeight: 'bold', marginBottom: '4px', textAlign: 'left', fontSize: '0.875rem' }}>{t('CalibrationSettings:hml-control-points')}</div>
						{leftCurvePoints.length > 0 ? (
							<div style={{ fontSize: '0.875rem' }}>
								{leftCurvePoints.map((point, originalIndex) => ({ point, originalIndex }))
									.sort((a, b) => a.point.x - b.point.x)
									.map(({ point, originalIndex }) => {
										const inputValue = leftCurveInputValues[originalIndex] || { x: point.x.toString(), y: point.y.toString() };
										return (
											<div key={originalIndex} style={{ marginBottom: '8px' }}>
												<div style={{ display: 'flex', gap: '6px', alignItems: 'center', marginBottom: '4px' }}>
													<span style={{ width: '16px', fontSize: '0.8rem' }}>{t('CalibrationSettings:hml-axis-x-short')}</span>
												<Form.Control
													type="number"
													size="sm"
													min={0}
													max={1}
													step={0.001}
													value={inputValue.x}
													style={{ width: '75px', fontSize: '0.8rem', padding: '2px 6px' }}
													onChange={(e) => handleLeftInputChange(originalIndex, 'x', e.target.value)}
													onBlur={() => handleLeftInputBlur(originalIndex)}
												/>
													<span style={{ width: '16px', fontSize: '0.8rem' }}>{t('CalibrationSettings:hml-axis-y-short')}</span>
												<Form.Control
													type="number"
													size="sm"
													min={0}
													max={1}
													step={0.001}
													value={inputValue.y}
													style={{ width: '75px', fontSize: '0.8rem', padding: '2px 6px' }}
													onChange={(e) => handleLeftInputChange(originalIndex, 'y', e.target.value)}
													onBlur={() => handleLeftInputBlur(originalIndex)}
												/>
												<Button
													variant="danger"
													size="sm"
													onClick={() => handleLeftDelete(originalIndex)}
													style={{ padding: '2px 8px', fontSize: '0.75rem' }}
												>
													{t('CalibrationSettings:hml-delete')}
												</Button>
												</div>
											</div>
										);
									})}
							</div>
						) : (
							<div style={{ fontSize: '0.875rem', color: '#6c757d' }}>{t('CalibrationSettings:hml-no-control-points')}</div>
						)}
						<input
							ref={(el) => {
								polylineImportFileRefs.current[0] = el;
							}}
							type="file"
							accept=".json,application/json"
							style={{ display: 'none' }}
							aria-hidden
							onChange={(e) => handlePolylineImportFileChange(0, e)}
						/>
						<Button
							type="button"
							variant="outline-secondary"
							size="sm"
							onClick={() => handlePolylineImportClick(0)}
							style={{ fontSize: '0.75rem', padding: '2px 8px', marginTop: '8px' }}
						>
							{t('CalibrationSettings:hml-import-file')}
						</Button>
					</div>
					<div style={{ width: '100%', fontSize: '0.875rem', textAlign: 'center', alignSelf: 'center', paddingTop: '8px' }}>
						<div>{t('CalibrationSettings:hml-phys-distance-left', { pct: (leftLightX !== undefined ? leftLightX * 100 : 0).toFixed(1) })}</div>
						<div>{t('CalibrationSettings:hml-out-distance-left', { pct: (leftLightY !== undefined ? leftLightY * 100 : 0).toFixed(1) })}</div>
					</div>
				</div>

				{/* Row 1, Column 3: Right stick control points */}
				<div style={{ width: '260px', minHeight: '260px', display: 'flex', flexDirection: 'column', justifyContent: 'space-between', alignItems: 'flex-start' }}>
					<div style={{ width: '260px', textAlign: 'left' }}>
						<div style={{ fontWeight: 'bold', marginBottom: '4px', textAlign: 'left', fontSize: '0.875rem' }}>{t('CalibrationSettings:hml-control-points')}</div>
						{rightCurvePoints.length > 0 ? (
							<div style={{ fontSize: '0.875rem' }}>
								{rightCurvePoints.map((point, originalIndex) => ({ point, originalIndex }))
									.sort((a, b) => a.point.x - b.point.x)
									.map(({ point, originalIndex }) => {
										const inputValue = rightCurveInputValues[originalIndex] || { x: parseFloat(point.x.toFixed(4)).toString(), y: parseFloat(point.y.toFixed(4)).toString() };
										return (
											<div key={originalIndex} style={{ marginBottom: '8px' }}>
												<div style={{ display: 'flex', gap: '6px', alignItems: 'center', marginBottom: '4px' }}>
													<span style={{ width: '16px', fontSize: '0.8rem' }}>{t('CalibrationSettings:hml-axis-x-short')}</span>
												<Form.Control
													type="number"
													size="sm"
													min={0}
													max={1}
													step={0.001}
													value={inputValue.x}
													style={{ width: '75px', fontSize: '0.8rem', padding: '2px 6px' }}
													onChange={(e) => handleRightInputChange(originalIndex, 'x', e.target.value)}
													onBlur={() => handleRightInputBlur(originalIndex)}
												/>
													<span style={{ width: '16px', fontSize: '0.8rem' }}>{t('CalibrationSettings:hml-axis-y-short')}</span>
												<Form.Control
													type="number"
													size="sm"
													min={0}
													max={1}
													step={0.001}
													value={inputValue.y}
													style={{ width: '75px', fontSize: '0.8rem', padding: '2px 6px' }}
													onChange={(e) => handleRightInputChange(originalIndex, 'y', e.target.value)}
													onBlur={() => handleRightInputBlur(originalIndex)}
												/>
												<Button
													variant="danger"
													size="sm"
													onClick={() => handleRightDelete(originalIndex)}
													style={{ padding: '2px 8px', fontSize: '0.75rem' }}
												>
													{t('CalibrationSettings:hml-delete')}
												</Button>
												</div>
											</div>
										);
									})}
							</div>
						) : (
							<div style={{ fontSize: '0.875rem', color: '#6c757d' }}>{t('CalibrationSettings:hml-no-control-points')}</div>
						)}
						<input
							ref={(el) => {
								polylineImportFileRefs.current[1] = el;
							}}
							type="file"
							accept=".json,application/json"
							style={{ display: 'none' }}
							aria-hidden
							onChange={(e) => handlePolylineImportFileChange(1, e)}
						/>
						<Button
							type="button"
							variant="outline-secondary"
							size="sm"
							onClick={() => handlePolylineImportClick(1)}
							style={{ fontSize: '0.75rem', padding: '2px 8px', marginTop: '8px' }}
						>
							{t('CalibrationSettings:hml-import-file')}
						</Button>
					</div>
					<div style={{ width: '100%', fontSize: '0.875rem', textAlign: 'center', alignSelf: 'center', paddingTop: '8px' }}>
						<div>{t('CalibrationSettings:hml-phys-distance-right', { pct: (rightLightX !== undefined ? rightLightX * 100 : 0).toFixed(1) })}</div>
						<div>{t('CalibrationSettings:hml-out-distance-right', { pct: (rightLightY !== undefined ? rightLightY * 100 : 0).toFixed(1) })}</div>
					</div>
				</div>

				{/* Row 1, Column 4: Right stick curve canvas */}
				<div className="text-center" style={{ display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'flex-start', width: '260px' }}>
					<div style={{ position: 'relative', width: '260px', height: '260px' }}>
						<canvas
							ref={rightCurveCanvasRef}
							width={260}
							height={260}
							style={{
								border: '1px solid #ccc',
								borderRadius: '4px',
								cursor: 'crosshair',
								display: 'block'
							}}
							onMouseDown={handleRightMouseDown}
							onMouseMove={handleRightMouseMove}
							onMouseUp={handleRightMouseUp}
							onMouseLeave={handleRightMouseLeave}
						/>
					</div>
				</div>
			</div>

			<div className="mb-3" style={{ display: 'grid', gridTemplateColumns: 'repeat(4, minmax(0, 1fr))', gap: '16px', alignItems: 'start', width: '100%', marginTop: '16px', paddingBottom: '8px', minWidth: 0 }}>
				{[0, 1, 2, 3].map((presetIndex) => (
					<div
						key={presetIndex}
						style={{
							display: 'flex',
							flexDirection: 'column',
							width: '100%',
							minWidth: 0,
							gap: '8px',
							padding: '8px',
							border: '1px solid #ddd',
							borderRadius: '4px',
						}}
					>
						{/* Row 1: preset name */}
						<Form.Control
							type="text"
							size="sm"
							maxLength={CURVE_PRESET_NAME_MAX_LEN}
							placeholder={t('CalibrationSettings:hml-preset-name-placeholder')}
							value={presetInputs[presetIndex].name}
							onChange={(e) => handlePresetNameChange(presetIndex, e.target.value)}
							style={{ width: '100%', fontSize: '0.875rem' }}
						/>
						{/* Row 2: apply to left/right stick */}
						<div style={{ display: 'flex', gap: '4px', alignItems: 'center', width: '100%' }}>
							<Button
								variant="outline-primary"
								size="sm"
								onClick={() => handleApplyPresetToLeft(presetIndex)}
								style={{ fontSize: '0.75rem', padding: '2px 4px', flex: '1 1 0', minWidth: 0 }}
							>
								{t('CalibrationSettings:hml-apply-left')}
							</Button>
							<Button
								variant="outline-primary"
								size="sm"
								onClick={() => handleApplyPresetToRight(presetIndex)}
								style={{ fontSize: '0.75rem', padding: '2px 4px', flex: '1 1 0', minWidth: 0 }}
							>
								{t('CalibrationSettings:hml-apply-right')}
							</Button>
						</div>
						{/* Row 3: import stick canvas curve into preset slot */}
						<div style={{ display: 'flex', gap: '4px', alignItems: 'center', width: '100%' }}>
							<Button
								type="button"
								variant="outline-secondary"
								size="sm"
								onClick={() => handlePresetImportFromStick(presetIndex, 0)}
								style={{ fontSize: '0.75rem', padding: '2px 4px', flex: '1 1 0', minWidth: 0 }}
							>
								{t('CalibrationSettings:hml-import-left')}
							</Button>
							<Button
								type="button"
								variant="outline-secondary"
								size="sm"
								onClick={() => handlePresetImportFromStick(presetIndex, 1)}
								style={{ fontSize: '0.75rem', padding: '2px 4px', flex: '1 1 0', minWidth: 0 }}
							>
								{t('CalibrationSettings:hml-import-right')}
							</Button>
						</div>
						{/* Row 4: right stick quick hot-switch key */}
						<div style={{ display: 'flex', flexDirection: 'column', gap: '4px', width: '100%', minWidth: 0 }}>
							<div style={{ display: 'flex', gap: '4px', alignItems: 'center', minWidth: 0 }}>
								<RightStickQuickSwitchHeading t={t} tooltipIdSuffix={String(presetIndex)} />
							</div>
							<Form.Select
								size="sm"
								value={presetInputs[presetIndex].activationButtonMask ?? 0}
								onChange={(e) => handlePresetActivationSelectChange(presetIndex, e)}
								style={{ width: '100%', minWidth: 0, fontSize: '0.75rem', padding: '2px 6px' }}
							>
								{BUTTON_MASKS_OPTIONS.map((o) => (
									<option key={o.value} value={o.value}>
										{(currentButtonLabels && currentButtonLabels[o.label]) || o.label}
									</option>
								))}
							</Form.Select>
						</div>
					</div>
				))}
			</div>
			</div>
			) : (
				<DisabledCurveBar label={t('CalibrationSettings:hml-polyline-disabled')} />
			)}
			{curveMode === 1 ? (
			<div className="curve-bezier-container" style={{ marginTop: '16px' }}>
				<div className="mb-3" style={{ display: 'grid', gridTemplateColumns: 'repeat(4, 260px)', gap: '16px', justifyContent: 'center', alignItems: 'start', width: 'max-content', margin: '0 auto' }}>
					{/* Row 1, Column 1: Left stick bezier canvas */}
					<div className="text-center" style={{ display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'flex-start', width: '260px' }}>
						<div style={{ position: 'relative', width: '260px', height: '260px' }}>
							<canvas
								ref={leftBezierCanvasRef}
								width={260}
								height={260}
								style={{
									border: '1px solid #ccc',
									borderRadius: '4px',
									cursor: 'crosshair',
									display: 'block'
								}}
								onMouseDown={handleLeftBezierMouseDown}
								onMouseMove={handleLeftBezierMouseMove}
								onMouseUp={handleLeftBezierMouseUp}
								onMouseLeave={handleLeftBezierMouseUp}
							/>
						</div>
					</div>

					{/* Row 1, Column 2: Left stick bezier control points + import file */}
					<div style={{ width: '260px', minHeight: '260px', display: 'flex', flexDirection: 'column', justifyContent: 'space-between', alignItems: 'flex-start' }}>
						<div style={{ width: '260px', textAlign: 'left' }}>
							<div style={{ fontWeight: 'bold', marginBottom: '4px', textAlign: 'left', fontSize: '0.875rem' }}>{t('CalibrationSettings:hml-bezier-points-title')}</div>
							<div style={{ fontSize: '0.875rem' }}>
								<div style={{ marginBottom: '8px' }}>
									<div style={{ display: 'flex', gap: '6px', alignItems: 'center', marginBottom: '4px' }}>
										<span style={{ width: '24px', fontSize: '0.8rem' }}>{t('CalibrationSettings:hml-bezier-c1')}</span>
										<Form.Control
											type="number"
											size="sm"
											min={0}
											max={1}
											step={0.001}
											placeholder="X"
											value={leftBezierInputs.c1x}
											style={{ width: '75px', fontSize: '0.8rem', padding: '2px 6px' }}
											onChange={(e) => handleLeftBezierInputChange('c1x', e.target.value)}
											onBlur={handleLeftBezierInputBlur}
										/>
										<Form.Control
											type="number"
											size="sm"
											min={0}
											max={1}
											step={0.001}
											placeholder="Y"
											value={leftBezierInputs.c1y}
											style={{ width: '75px', fontSize: '0.8rem', padding: '2px 6px' }}
											onChange={(e) => handleLeftBezierInputChange('c1y', e.target.value)}
											onBlur={handleLeftBezierInputBlur}
										/>
									</div>
								</div>
								<div style={{ marginBottom: '8px' }}>
									<div style={{ display: 'flex', gap: '6px', alignItems: 'center', marginBottom: '4px' }}>
										<span style={{ width: '24px', fontSize: '0.8rem' }}>{t('CalibrationSettings:hml-bezier-c2')}</span>
										<Form.Control
											type="number"
											size="sm"
											min={0}
											max={1}
											step={0.001}
											placeholder="X"
											value={leftBezierInputs.c2x}
											style={{ width: '75px', fontSize: '0.8rem', padding: '2px 6px' }}
											onChange={(e) => handleLeftBezierInputChange('c2x', e.target.value)}
											onBlur={handleLeftBezierInputBlur}
										/>
										<Form.Control
											type="number"
											size="sm"
											min={0}
											max={1}
											step={0.001}
											placeholder="Y"
											value={leftBezierInputs.c2y}
											style={{ width: '75px', fontSize: '0.8rem', padding: '2px 6px' }}
											onChange={(e) => handleLeftBezierInputChange('c2y', e.target.value)}
											onBlur={handleLeftBezierInputBlur}
										/>
									</div>
								</div>
							</div>
							<input
								ref={(el) => {
									bezierImportFileRefs.current[0] = el;
								}}
								type="file"
								accept=".json,application/json"
								style={{ display: 'none' }}
								aria-hidden
								onChange={(e) => handleBezierImportFileChange(0, e)}
							/>
							<Button
								type="button"
								variant="outline-secondary"
								size="sm"
								onClick={() => handleBezierImportClick(0)}
								style={{ fontSize: '0.75rem', padding: '2px 8px' }}
							>
								{t('CalibrationSettings:hml-import-file')}
							</Button>
						</div>
						<div style={{ width: '100%', fontSize: '0.875rem', textAlign: 'center', alignSelf: 'center', paddingTop: '8px' }}>
							<div>{t('CalibrationSettings:hml-phys-distance-left', { pct: (leftLightX !== undefined ? leftLightX * 100 : 0).toFixed(1) })}</div>
							<div>{t('CalibrationSettings:hml-out-distance-left', { pct: (leftLightY !== undefined ? leftLightY * 100 : 0).toFixed(1) })}</div>
						</div>
					</div>

					{/* Row 1, Column 3: Right stick bezier control points + import file */}
					<div style={{ width: '260px', minHeight: '260px', display: 'flex', flexDirection: 'column', justifyContent: 'space-between', alignItems: 'flex-start' }}>
						<div style={{ width: '260px', textAlign: 'left' }}>
							<div style={{ fontWeight: 'bold', marginBottom: '4px', textAlign: 'left', fontSize: '0.875rem' }}>{t('CalibrationSettings:hml-bezier-points-title')}</div>
							<div style={{ fontSize: '0.875rem' }}>
								<div style={{ marginBottom: '8px' }}>
									<div style={{ display: 'flex', gap: '6px', alignItems: 'center', marginBottom: '4px' }}>
										<span style={{ width: '24px', fontSize: '0.8rem' }}>{t('CalibrationSettings:hml-bezier-c1')}</span>
										<Form.Control
											type="number"
											size="sm"
											min={0}
											max={1}
											step={0.001}
											placeholder="X"
											value={rightBezierInputs.c1x}
											style={{ width: '75px', fontSize: '0.8rem', padding: '2px 6px' }}
											onChange={(e) => handleRightBezierInputChange('c1x', e.target.value)}
											onBlur={handleRightBezierInputBlur}
										/>
										<Form.Control
											type="number"
											size="sm"
											min={0}
											max={1}
											step={0.001}
											placeholder="Y"
											value={rightBezierInputs.c1y}
											style={{ width: '75px', fontSize: '0.8rem', padding: '2px 6px' }}
											onChange={(e) => handleRightBezierInputChange('c1y', e.target.value)}
											onBlur={handleRightBezierInputBlur}
										/>
									</div>
								</div>
								<div style={{ marginBottom: '8px' }}>
									<div style={{ display: 'flex', gap: '6px', alignItems: 'center', marginBottom: '4px' }}>
										<span style={{ width: '24px', fontSize: '0.8rem' }}>{t('CalibrationSettings:hml-bezier-c2')}</span>
										<Form.Control
											type="number"
											size="sm"
											min={0}
											max={1}
											step={0.001}
											placeholder="X"
											value={rightBezierInputs.c2x}
											style={{ width: '75px', fontSize: '0.8rem', padding: '2px 6px' }}
											onChange={(e) => handleRightBezierInputChange('c2x', e.target.value)}
											onBlur={handleRightBezierInputBlur}
										/>
										<Form.Control
											type="number"
											size="sm"
											min={0}
											max={1}
											step={0.001}
											placeholder="Y"
											value={rightBezierInputs.c2y}
											style={{ width: '75px', fontSize: '0.8rem', padding: '2px 6px' }}
											onChange={(e) => handleRightBezierInputChange('c2y', e.target.value)}
											onBlur={handleRightBezierInputBlur}
										/>
									</div>
								</div>
							</div>
							<input
								ref={(el) => {
									bezierImportFileRefs.current[1] = el;
								}}
								type="file"
								accept=".json,application/json"
								style={{ display: 'none' }}
								aria-hidden
								onChange={(e) => handleBezierImportFileChange(1, e)}
							/>
							<Button
								type="button"
								variant="outline-secondary"
								size="sm"
								onClick={() => handleBezierImportClick(1)}
								style={{ fontSize: '0.75rem', padding: '2px 8px' }}
							>
								{t('CalibrationSettings:hml-import-file')}
							</Button>
						</div>
						<div style={{ width: '100%', fontSize: '0.875rem', textAlign: 'center', alignSelf: 'center', paddingTop: '8px' }}>
							<div>{t('CalibrationSettings:hml-phys-distance-right', { pct: (rightLightX !== undefined ? rightLightX * 100 : 0).toFixed(1) })}</div>
							<div>{t('CalibrationSettings:hml-out-distance-right', { pct: (rightLightY !== undefined ? rightLightY * 100 : 0).toFixed(1) })}</div>
						</div>
					</div>

					{/* Row 1, Column 4: Right stick bezier canvas */}
					<div className="text-center" style={{ display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'flex-start', width: '260px' }}>
						<div style={{ position: 'relative', width: '260px', height: '260px' }}>
							<canvas
								ref={rightBezierCanvasRef}
								width={260}
								height={260}
								style={{
									border: '1px solid #ccc',
									borderRadius: '4px',
									cursor: 'crosshair',
									display: 'block'
								}}
								onMouseDown={handleRightBezierMouseDown}
								onMouseMove={handleRightBezierMouseMove}
								onMouseUp={handleRightBezierMouseUp}
								onMouseLeave={handleRightBezierMouseUp}
							/>
						</div>
					</div>
				</div>

				{/* Bezier presets: shared by both sticks, 4 slots */}
				<div className="mb-3" style={{ display: 'grid', gridTemplateColumns: 'repeat(4, minmax(0, 1fr))', gap: '16px', alignItems: 'start', width: '100%', marginTop: '16px', paddingBottom: '8px', minWidth: 0 }}>
					{[0, 1, 2, 3].map((presetIndex) => (
						<div
							key={presetIndex}
							style={{
								display: 'flex',
								flexDirection: 'column',
								width: '100%',
								minWidth: 0,
								gap: '8px',
								padding: '8px',
								border: '1px solid #ddd',
								borderRadius: '4px',
							}}
						>
							{/* Row 1: preset name */}
							<Form.Control
								type="text"
								size="sm"
								maxLength={BEZIER_PRESET_NAME_MAX_LEN}
								placeholder={t('CalibrationSettings:hml-preset-name-placeholder')}
								value={bezierPresetInputs[presetIndex].name}
								onChange={(e) => handleBezierPresetNameChange(presetIndex, e.target.value)}
								style={{ width: '100%', fontSize: '0.875rem' }}
							/>
							{/* Row 2: apply to left/right stick */}
							<div style={{ display: 'flex', gap: '4px', alignItems: 'center', width: '100%' }}>
								<Button
									variant="outline-primary"
									size="sm"
									onClick={() => handleApplyBezierPresetToLeft(presetIndex)}
									style={{ fontSize: '0.75rem', padding: '2px 4px', flex: '1 1 0', minWidth: 0 }}
								>
									{t('CalibrationSettings:hml-apply-left')}
								</Button>
								<Button
									variant="outline-primary"
									size="sm"
									onClick={() => handleApplyBezierPresetToRight(presetIndex)}
									style={{ fontSize: '0.75rem', padding: '2px 4px', flex: '1 1 0', minWidth: 0 }}
								>
									{t('CalibrationSettings:hml-apply-right')}
								</Button>
							</div>
							{/* Row 3: import stick canvas curve into preset slot */}
							<div style={{ display: 'flex', gap: '4px', alignItems: 'center', width: '100%' }}>
								<Button
									type="button"
									variant="outline-secondary"
									size="sm"
									onClick={() => handleBezierPresetImportLeft(presetIndex)}
									style={{ fontSize: '0.75rem', padding: '2px 4px', flex: '1 1 0', minWidth: 0 }}
								>
									{t('CalibrationSettings:hml-import-left')}
								</Button>
								<Button
									type="button"
									variant="outline-secondary"
									size="sm"
									onClick={() => handleBezierPresetImportRight(presetIndex)}
									style={{ fontSize: '0.75rem', padding: '2px 4px', flex: '1 1 0', minWidth: 0 }}
								>
									{t('CalibrationSettings:hml-import-right')}
								</Button>
							</div>
							{/* Row 4: quick-switch key */}
							<div style={{ display: 'flex', flexDirection: 'column', gap: '4px', width: '100%', minWidth: 0 }}>
								<div style={{ display: 'flex', gap: '4px', alignItems: 'center', minWidth: 0 }}>
									<RightStickQuickSwitchHeading t={t} tooltipIdSuffix={`bezier-${presetIndex}`} />
								</div>
								<Form.Select
									size="sm"
									value={bezierPresetInputs[presetIndex].activationButtonMask ?? 0}
									onChange={(e) => handleBezierPresetActivationSelectChange(presetIndex, e)}
									style={{ width: '100%', minWidth: 0, fontSize: '0.75rem', padding: '2px 6px' }}
								>
									{BUTTON_MASKS_OPTIONS.map((o) => (
										<option key={o.value} value={o.value}>
											{(currentButtonLabels && currentButtonLabels[o.label]) || o.label}
										</option>
									))}
								</Form.Select>
							</div>
						</div>
					))}
				</div>
			</div>
			) : (
				<div style={{ marginTop: '16px' }}>
					<DisabledCurveBar label={t('CalibrationSettings:hml-bezier-disabled')} />
				</div>
			)}
			</>
			)}

			{/* Bottom section: Save button (left) and Toggle switch (right) */}
			<div style={{ marginTop: '16px', display: 'flex', justifyContent: 'space-between', alignItems: 'center', width: '100%' }}>
				{/* Save button on the left */}
				<div style={{ display: 'flex', alignItems: 'center', gap: '12px' }}>
					<Button
						variant="primary"
						onClick={(e) => {
							e.preventDefault();
							onSaveClick ? onSaveClick() : handleSubmit();
						}}
					>
						{t('Common:button-save-label')}
					</Button>
					{saveMessage && (
						<span className={saveMessage === t('Common:saved-success-message') ? 'text-success' : 'text-danger'}>
							{saveMessage}
						</span>
					)}
					{/* Curve mode switch: 0 = polyline, 1 = bezier (text on the right follows the mode) */}
					<Form.Check
						type="switch"
						id="joystick-curve-mode"
						checked={curveMode === 1}
						onChange={(e) => {
							setFieldValue('joystickCurveMode', e.target.checked ? 1 : 0);
						}}
						style={{ marginLeft: '12px' }}
					/>
					<span style={{ fontSize: '0.875rem' }}>
						{curveMode === 1
							? t('CalibrationSettings:hml-curve-mode-bezier')
							: t('CalibrationSettings:hml-curve-mode-polyline')}
					</span>
				</div>
				{/* Toggle switch on the right */}
				<div style={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
					<Form.Check
						type="switch"
						id="joystick-curve-enabled"
						label={t('CalibrationSettings:hml-enable-stick-curve-label')}
						checked={isExpanded}
						onChange={(e) => {
							const enabled = e.target.checked;
							setIsExpanded(enabled);
							setFieldValue('joystickCurveEnabled', enabled ? 1 : 0);
						}}
					/>
				</div>
			</div>
		</Section>
	);
};

export default JoystickCurveSettings;
