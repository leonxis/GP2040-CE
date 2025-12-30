import { useEffect, useRef, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { Button, Form } from 'react-bootstrap';
import { useFormikContext } from 'formik';

import Section from '../../../Components/Section';
import type { AddonPropTypes } from './CalibrationSettings';

// Type definitions
type CurvePoint = { x: number; y: number };
type CurvePointInput = { x: string; y: string };

const ADC_MAX = 4095;
const ADC_CENTER = ADC_MAX / 2.0;  // 2047.5, matches backend
const CIRCULARITY_DATA_SIZE = 48;

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
	rangeData: number[]
) => {
	// Step 1: Transform to center-relative coordinates
	const dX_value = centerX - ADC_CENTER;
	const dY_value = centerY - ADC_CENTER;
	const offset_x = rawX - dX_value;
	const offset_y = rawY - dY_value;
	const offset_center_x = offset_x - ADC_CENTER;
	const offset_center_y = offset_y - ADC_CENTER;
	
	// Step 2: Range calibration scaling
	const current_distance = Math.sqrt(offset_center_x * offset_center_x + offset_center_y * offset_center_y);
	const angle = Math.atan2(offset_center_y, offset_center_x);
	const scale = getInterpolatedScale(angle, rangeData);
	
	const scaled_center_x = offset_center_x / scale;
	const scaled_center_y = offset_center_y / scale;
	
	// Step 3: Normalize to [-1, 1] range
	const stickX = scaled_center_x / (ADC_MAX / 2.0);
	const stickY = scaled_center_y / (ADC_MAX / 2.0);
	
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
	
	const fullPoints: CurvePoint[] = [
		{x: 0, y: 0},
		...points.sort((a, b) => a.x - b.x),
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
	const ctx = canvas.getContext('2d', { alpha: false })!;

	// Use integer coordinates for better performance
	const intWidth = Math.round(width);
	const intHeight = Math.round(height);

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
		curveStaticCanvasCache.delete(firstKey);
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
	const sortedPoints = [...points].sort((a, b) => a.x - b.x);
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
	ctx.strokeStyle = '#000000';
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
	const sorted = [...points].sort((a, b) => {
		if (a.x === b.x) {
			return a.y - b.y;
		}
		return a.x - b.x;
	});
	
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

interface JoystickCurveSettingsProps {
	values: AddonPropTypes['values'];
	setFieldValue: AddonPropTypes['setFieldValue'];
}

const JoystickCurveSettings = ({
	values,
	setFieldValue,
}: JoystickCurveSettingsProps) => {
	const { t } = useTranslation();
	const { handleSubmit } = useFormikContext();
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
			return (saved as CurvePoint[]).map(p => ({ x: parseFloat(p.x.toFixed(4)).toString(), y: parseFloat(p.y.toFixed(4)).toString() }));
		}
		return [];
	});
	const [leftDraggingPointIndex, setLeftDraggingPointIndex] = useState<number | null>(null);
	const [leftDragOffset, setLeftDragOffset] = useState<{ x: number; y: number } | null>(null);
	const leftCurveCanvasRef = useRef<HTMLCanvasElement>(null);
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
			return (saved as CurvePoint[]).map(p => ({ x: parseFloat(p.x.toFixed(4)).toString(), y: parseFloat(p.y.toFixed(4)).toString() }));
		}
		return [];
	});
	const [rightDraggingPointIndex, setRightDraggingPointIndex] = useState<number | null>(null);
	const [rightDragOffset, setRightDragOffset] = useState<{ x: number; y: number } | null>(null);
	const rightCurveCanvasRef = useRef<HTMLCanvasElement>(null);
	const [rightLightX, setRightLightX] = useState<number | undefined>(undefined); // Physical distance (sqrt(dist_sq)) as percentage
	const [rightLightY, setRightLightY] = useState<number | undefined>(undefined); // Output distance (sqrt(stickX*stickX + stickY*stickY)) as percentage
	
	// Load curve points from values when they change
	useEffect(() => {
		const saved = values?.joystickCurvePoints1;
		if (Array.isArray(saved)) {
			setLeftCurvePoints(saved as CurvePoint[]);
			setLeftCurveInputValues((saved as CurvePoint[]).map(p => ({ x: parseFloat(p.x.toFixed(4)).toString(), y: parseFloat(p.y.toFixed(4)).toString() })));
		} else {
			setLeftCurvePoints([]);
			setLeftCurveInputValues([]);
		}
	}, [values?.joystickCurvePoints1]);
	
	useEffect(() => {
		const saved = values?.joystickCurvePoints2;
		if (Array.isArray(saved)) {
			setRightCurvePoints(saved as CurvePoint[]);
			setRightCurveInputValues((saved as CurvePoint[]).map(p => ({ x: parseFloat(p.x.toFixed(4)).toString(), y: parseFloat(p.y.toFixed(4)).toString() })));
		} else {
			setRightCurvePoints([]);
			setRightCurveInputValues([]);
		}
	}, [values?.joystickCurvePoints2]);
	
	// Fetch joystick data to get progress ratio for highlight line
	useEffect(() => {
		let intervalId: ReturnType<typeof setInterval> | null = null;
		
		const fetchJoystickData = async () => {
			if (values.AnalogInputEnabled !== 1) return;
			
			// Fetch left stick
			if (values.analogAdc1PinX != null && values.analogAdc1PinX >= 0 && values.analogAdc1PinY != null && values.analogAdc1PinY >= 0) {
				try {
					const res = await fetch('/api/getJoystickCenter');
					if (res.ok) {
						const data = await res.json();
						if (data.success) {
							const centerX = values.joystickCenterX || 2047.5;
							const centerY = values.joystickCenterY || 2047.5;
							const rawX = data.x;
							const rawY = data.y;
							const rangeData = values?.joystickRangeData1 || [];
							
							// Process joystick data through pipeline (Steps 1-3)
							// Use default scale of 1.0 if no range data
							const { stickX: rawStickX, stickY: rawStickY } = rangeData.length > 0
								? processJoystickData(rawX, rawY, centerX, centerY, rangeData)
								: (() => {
									// Simple processing without range calibration
									const offset_center_x = rawX - centerX;
									const offset_center_y = rawY - centerY;
									const stickX = offset_center_x / (ADC_MAX / 2.0);
									const stickY = offset_center_y / (ADC_MAX / 2.0);
									return { stickX, stickY };
								})();
							
							// Step 3.5: Apply inversion
							const invert1 = values?.analogAdc1Invert ?? 0;
							let stickX = (invert1 === 1 || invert1 === 3) ? -rawStickX : rawStickX;
							let stickY = (invert1 === 2 || invert1 === 3) ? -rawStickY : rawStickY;
							
							// Step 4: Apply deadzone and anti-deadzone
							const innerDeadzone = (values?.inner_deadzone || 0) / 100.0;
							const antiDeadzone = (values?.anti_deadzone || 0) / 100.0;
							const dist_sq = stickX * stickX + stickY * stickY;
							const deadzone_sq = innerDeadzone * innerDeadzone;
							
							// Calculate lightX = sqrt(dist_sq) as percentage (before deadzone/anti-deadzone)
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
										stickX = stickX * scale_factor;
										stickY = stickY * scale_factor;
									}
								} else {
									if (dist > 0.0) {
										const new_dist = dist + baseline;
										const scale_factor = new_dist / dist;
										stickX = stickX * scale_factor;
										stickY = stickY * scale_factor;
									}
								}
							}
							
							// Step 5: Square trimming
							stickX = Math.max(-1.0, Math.min(1.0, stickX));
							stickY = Math.max(-1.0, Math.min(1.0, stickY));
							
							// Step 6: Apply response curve if configured
							// Use current state leftCurvePoints instead of values to reflect real-time editing
							const curveEnabled = values?.joystickCurveEnabled ?? false;
							if (curveEnabled && leftCurvePoints.length > 0) {
								if (stickX !== 0.0 || stickY !== 0.0) {
									const magnitude_sq = stickX * stickX + stickY * stickY;
									if (magnitude_sq > 0.0) {
										const magnitude = Math.sqrt(magnitude_sq);
										const curvedMagnitude = applyResponseCurve(magnitude, leftCurvePoints);
										if (magnitude > 0) {
											const scale = curvedMagnitude / magnitude;
											stickX = stickX * scale;
											stickY = stickY * scale;
										}
									}
								}
							}
							
							// Calculate lightY = sqrt(stickX*stickX + stickY*stickY) as percentage
							const lightY = Math.sqrt(stickX * stickX + stickY * stickY);
							
							setLeftLightX(lightX);
							setLeftLightY(lightY);
						}
					}
				} catch (e) {
					// Ignore errors
				}
			}
			
			// Fetch right stick
			if (values.analogAdc2PinX != null && values.analogAdc2PinX >= 0 && values.analogAdc2PinY != null && values.analogAdc2PinY >= 0) {
				try {
					const res = await fetch('/api/getJoystickCenter2');
					if (res.ok) {
						const data = await res.json();
						if (data.success) {
							const centerX = values.joystickCenterX2 || 2047.5;
							const centerY = values.joystickCenterY2 || 2047.5;
							const rawX = data.x;
							const rawY = data.y;
							const rangeData = values?.joystickRangeData2 || [];
							
							// Process joystick data through pipeline (Steps 1-3)
							// Use default scale of 1.0 if no range data
							const { stickX: rawStickX, stickY: rawStickY } = rangeData.length > 0
								? processJoystickData(rawX, rawY, centerX, centerY, rangeData)
								: (() => {
									// Simple processing without range calibration
									const offset_center_x = rawX - centerX;
									const offset_center_y = rawY - centerY;
									const stickX = offset_center_x / (ADC_MAX / 2.0);
									const stickY = offset_center_y / (ADC_MAX / 2.0);
									return { stickX, stickY };
								})();
							
							// Step 3.5: Apply inversion
							const invert2 = values?.analogAdc2Invert ?? 0;
							let stickX = (invert2 === 1 || invert2 === 3) ? -rawStickX : rawStickX;
							let stickY = (invert2 === 2 || invert2 === 3) ? -rawStickY : rawStickY;
							
							// Step 4: Apply deadzone and anti-deadzone
							const innerDeadzone = (values?.inner_deadzone2 || 0) / 100.0;
							const antiDeadzone = (values?.anti_deadzone2 || 0) / 100.0;
							const dist_sq = stickX * stickX + stickY * stickY;
							const deadzone_sq = innerDeadzone * innerDeadzone;
							
							// Calculate lightX = sqrt(dist_sq) as percentage (before deadzone/anti-deadzone)
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
										stickX = stickX * scale_factor;
										stickY = stickY * scale_factor;
									}
								} else {
									if (dist > 0.0) {
										const new_dist = dist + baseline;
										const scale_factor = new_dist / dist;
										stickX = stickX * scale_factor;
										stickY = stickY * scale_factor;
									}
								}
							}
							
							// Step 5: Square trimming
							stickX = Math.max(-1.0, Math.min(1.0, stickX));
							stickY = Math.max(-1.0, Math.min(1.0, stickY));
							
							// Step 6: Apply response curve if configured
							// Use current state rightCurvePoints instead of values to reflect real-time editing
							const curveEnabled = values?.joystickCurveEnabled ?? false;
							if (curveEnabled && rightCurvePoints.length > 0) {
								if (stickX !== 0.0 || stickY !== 0.0) {
									const magnitude_sq = stickX * stickX + stickY * stickY;
									if (magnitude_sq > 0.0) {
										const magnitude = Math.sqrt(magnitude_sq);
										const curvedMagnitude = applyResponseCurve(magnitude, rightCurvePoints);
										if (magnitude > 0) {
											const scale = curvedMagnitude / magnitude;
											stickX = stickX * scale;
											stickY = stickY * scale;
										}
									}
								}
							}
							
							// Calculate lightY = sqrt(stickX*stickX + stickY*stickY) as percentage
							const lightY = Math.sqrt(stickX * stickX + stickY * stickY);
							
							setRightLightX(lightX);
							setRightLightY(lightY);
						}
					}
				} catch (e) {
					// Ignore errors
				}
			}
		};
		
		intervalId = setInterval(fetchJoystickData, 100);
		
		return () => {
			if (intervalId) clearInterval(intervalId);
		};
	}, [values.AnalogInputEnabled, values.analogAdc1PinX, values.analogAdc1PinY, values.analogAdc2PinX, values.analogAdc2PinY, values.joystickCenterX, values.joystickCenterY, values.joystickCenterX2, values.joystickCenterY2, values.joystickRangeData1, values.joystickRangeData2, values?.analogAdc1Invert, values?.analogAdc2Invert, values?.inner_deadzone, values?.inner_deadzone2, values?.anti_deadzone, values?.anti_deadzone2, values?.fixed_anti_deadzone, values?.fixed_anti_deadzone2, values?.joystickCurveEnabled, leftCurvePoints, rightCurvePoints]);
	
	// Draw left curve canvas (optimized with requestAnimationFrame throttling)
	useEffect(() => {
		if (!isExpanded || !leftCurveCanvasRef.current) return;

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

			const ctx = leftCurveCanvasRef.current?.getContext('2d', { alpha: false });
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
	}, [isExpanded, leftCurvePoints, leftLightX, leftLightY, values?.inner_deadzone, values?.anti_deadzone]);
	
	// Draw right curve canvas (optimized with requestAnimationFrame throttling)
	useEffect(() => {
		if (!isExpanded || !rightCurveCanvasRef.current) return;

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

			const ctx = rightCurveCanvasRef.current?.getContext('2d', { alpha: false });
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
	}, [isExpanded, rightCurvePoints, rightLightX, rightLightY, values?.inner_deadzone2, values?.anti_deadzone2]);
	
	// Sync input values with curve points
	useEffect(() => {
		if (leftCurvePoints.length === leftCurveInputValues.length) {
			const needsUpdate = leftCurvePoints.some((p, i) => {
				const inputVal = leftCurveInputValues[i];
				return !inputVal || Math.abs(parseFloat(inputVal.x || '0') - p.x) > 0.0001 || Math.abs(parseFloat(inputVal.y || '0') - p.y) > 0.0001;
			});
			if (needsUpdate) {
				setLeftCurveInputValues(leftCurvePoints.map(p => ({ x: parseFloat(p.x.toFixed(4)).toString(), y: parseFloat(p.y.toFixed(4)).toString() })));
			}
		} else {
			setLeftCurveInputValues(leftCurvePoints.map(p => ({ x: parseFloat(p.x.toFixed(4)).toString(), y: parseFloat(p.y.toFixed(4)).toString() })));
		}
	}, [leftCurvePoints]);
	
	useEffect(() => {
		if (rightCurvePoints.length === rightCurveInputValues.length) {
			const needsUpdate = rightCurvePoints.some((p, i) => {
				const inputVal = rightCurveInputValues[i];
				return !inputVal || Math.abs(parseFloat(inputVal.x || '0') - p.x) > 0.0001 || Math.abs(parseFloat(inputVal.y || '0') - p.y) > 0.0001;
			});
			if (needsUpdate) {
				setRightCurveInputValues(rightCurvePoints.map(p => ({ x: parseFloat(p.x.toFixed(4)).toString(), y: parseFloat(p.y.toFixed(4)).toString() })));
			}
		} else {
			setRightCurveInputValues(rightCurvePoints.map(p => ({ x: parseFloat(p.x.toFixed(4)).toString(), y: parseFloat(p.y.toFixed(4)).toString() })));
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
		
		// Add new point if less than 3 points
		if (leftCurvePoints.length < 3) {
			const innerDeadzone = (values?.inner_deadzone || 0) / 100.0;
			const antiDeadzone = (values?.anti_deadzone || 0) / 100.0;
			const startPoint = innerDeadzone > 0 || antiDeadzone > 0 ? { x: innerDeadzone, y: antiDeadzone } : { x: 0, y: 0 };
			const allReferencePoints = [startPoint, ...leftCurvePoints, { x: 1, y: 1 }];
			const validatedPoint = validatePointMonotonicity(clickedPoint, allReferencePoints);
			const allPoints = [...leftCurvePoints, validatedPoint];
			const validatedAll = validateAllPointsMonotonicity(allPoints);
			setLeftCurvePoints(validatedAll);
			// For new points, no offset needed (point is created at mouse position)
			setLeftDragOffset({ x: 0, y: 0 });
			setLeftDraggingPointIndex(validatedAll.length - 1);
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
		setLeftCurvePoints(updatedPoints);
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
		
		if (rightCurvePoints.length < 3) {
			const innerDeadzone = (values?.inner_deadzone2 || 0) / 100.0;
			const antiDeadzone = (values?.anti_deadzone2 || 0) / 100.0;
			const startPoint = innerDeadzone > 0 || antiDeadzone > 0 ? { x: innerDeadzone, y: antiDeadzone } : { x: 0, y: 0 };
			const allReferencePoints = [startPoint, ...rightCurvePoints, { x: 1, y: 1 }];
			const validatedPoint = validatePointMonotonicity(clickedPoint, allReferencePoints);
			const allPoints = [...rightCurvePoints, validatedPoint];
			const validatedAll = validateAllPointsMonotonicity(allPoints);
			setRightCurvePoints(validatedAll);
			// For new points, no offset needed (point is created at mouse position)
			setRightDragOffset({ x: 0, y: 0 });
			setRightDraggingPointIndex(validatedAll.length - 1);
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
		setRightCurvePoints(updatedPoints);
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
		setLeftCurvePoints(validatedAll);
		// Update input values with formatted display (4 decimal places)
		const updatedInputValues = [...leftCurveInputValues];
		updatedInputValues[index] = { x: parseFloat(validatedAll[index].x.toFixed(4)).toString(), y: parseFloat(validatedAll[index].y.toFixed(4)).toString() };
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
		setRightCurvePoints(validatedAll);
		// Update input values with formatted display (4 decimal places)
		const updatedInputValues = [...rightCurveInputValues];
		updatedInputValues[index] = { x: parseFloat(validatedAll[index].x.toFixed(4)).toString(), y: parseFloat(validatedAll[index].y.toFixed(4)).toString() };
		setRightCurveInputValues(updatedInputValues);
	};
	
	// Handle delete for left stick
	const handleLeftDelete = (index: number) => {
		const updated = leftCurvePoints.filter((_, i) => i !== index);
		setLeftCurvePoints(updated);
		setLeftCurveInputValues(updated.map(p => ({ x: parseFloat(p.x.toFixed(4)).toString(), y: parseFloat(p.y.toFixed(4)).toString() })));
	};
	
	// Handle delete for right stick
	const handleRightDelete = (index: number) => {
		const updated = rightCurvePoints.filter((_, i) => i !== index);
		setRightCurvePoints(updated);
		setRightCurveInputValues(updated.map(p => ({ x: parseFloat(p.x.toFixed(4)).toString(), y: parseFloat(p.y.toFixed(4)).toString() })));
	};
	
	// Handle reset for left stick
	const handleLeftReset = () => {
		setLeftCurvePoints([]);
		setLeftCurveInputValues([]);
	};
	
	// Handle reset for right stick
	const handleRightReset = () => {
		setRightCurvePoints([]);
		setRightCurveInputValues([]);
	};
	
	// Handle confirm for left stick
	const handleLeftConfirm = () => {
		const validated = validateAllPointsMonotonicity(leftCurvePoints);
		const sorted = validated.sort((a, b) => a.x - b.x);
		setFieldValue('joystickCurvePoints1', sorted);
	};
	
	// Handle confirm for right stick
	const handleRightConfirm = () => {
		const validated = validateAllPointsMonotonicity(rightCurvePoints);
		const sorted = validated.sort((a, b) => a.x - b.x);
		setFieldValue('joystickCurvePoints2', sorted);
	};
	
	// Preset state management
	type PresetInput = {
		name: string;
		points: Array<{ x: string; y: string }>;
	};
	
	const [presetInputs, setPresetInputs] = useState<PresetInput[]>([
		{ name: '', points: [{ x: '0', y: '0' }, { x: '0', y: '0' }, { x: '0', y: '0' }] },
		{ name: '', points: [{ x: '0', y: '0' }, { x: '0', y: '0' }, { x: '0', y: '0' }] },
		{ name: '', points: [{ x: '0', y: '0' }, { x: '0', y: '0' }, { x: '0', y: '0' }] },
		{ name: '', points: [{ x: '0', y: '0' }, { x: '0', y: '0' }, { x: '0', y: '0' }] },
	]);
	
	// Load presets from values on mount and when values change
	useEffect(() => {
		const presets = (values?.joystickCurvePresets as Array<{ name: string; points: CurvePoint[] }>) || [];
		
		const loadPreset = (presetIndex: number) => {
			if (presetIndex < presets.length) {
				const preset = presets[presetIndex];
				const name = preset.name || '';
				const points = preset.points || [];
				
				const presetPoints: Array<{ x: string; y: string }> = [
					{ x: '0', y: '0' },
					{ x: '0', y: '0' },
					{ x: '0', y: '0' },
				];
				
				// Fill in existing points, pad with zeros if less than 3
				for (let i = 0; i < 3; i++) {
					if (i < points.length) {
						presetPoints[i] = {
							x: parseFloat(points[i].x.toFixed(4)).toString(),
							y: parseFloat(points[i].y.toFixed(4)).toString(),
						};
					}
				}
				
				return { name, points: presetPoints };
			}
			
			// Default empty preset
			return { name: '', points: [{ x: '0', y: '0' }, { x: '0', y: '0' }, { x: '0', y: '0' }] };
		};
		
		setPresetInputs([
			loadPreset(0),
			loadPreset(1),
			loadPreset(2),
			loadPreset(3),
		]);
	}, [values?.joystickCurvePresets]);
	
	// Handle preset name change
	const handlePresetNameChange = (presetIndex: number, value: string) => {
		const newPresets = [...presetInputs];
		newPresets[presetIndex].name = value;
		setPresetInputs(newPresets);
		
		// Update formik with new preset array format
		const allPresets: Array<{ name: string; points: CurvePoint[] }> = [];
		for (let i = 0; i < 4; i++) {
			const currentPreset = i === presetIndex 
				? { name: value, points: newPresets[presetIndex].points }
				: presetInputs[i];
			
			// Convert current preset to points array
			const currentPoints: CurvePoint[] = [];
			for (let j = 0; j < currentPreset.points.length; j++) {
				const x = parseFloat(String(currentPreset.points[j].x)) || 0;
				const y = parseFloat(String(currentPreset.points[j].y)) || 0;
				if (x > 0 || y > 0) {
					currentPoints.push({ x: Math.max(0, Math.min(1, x)), y: Math.max(0, Math.min(1, y)) });
				}
			}
			
			// Only include preset if it has name or points
			if (currentPreset.name.trim() || currentPoints.length > 0) {
				allPresets.push({
					name: currentPreset.name.trim(),
					points: currentPoints
				});
			}
		}
		
		setFieldValue('joystickCurvePresets', allPresets);
	};
	
	// Handle preset point change
	const handlePresetPointChange = (presetIndex: number, pointIndex: number, field: 'x' | 'y', value: string) => {
		const newPresets = [...presetInputs];
		newPresets[presetIndex].points[pointIndex][field] = value;
		setPresetInputs(newPresets);
	};
	
	// Handle preset point blur (save to formik)
	const handlePresetPointBlur = (presetIndex: number) => {
		const preset = presetInputs[presetIndex];
		const points: CurvePoint[] = [];
		
		// Convert string inputs to numbers, filter out zero points
		for (let i = 0; i < preset.points.length; i++) {
			const x = parseFloat(preset.points[i].x) || 0;
			const y = parseFloat(preset.points[i].y) || 0;
			if (x > 0 || y > 0) {
				points.push({ x: Math.max(0, Math.min(1, x)), y: Math.max(0, Math.min(1, y)) });
			}
		}
		
		// Apply validation logic (same as control points): ensure 1% gap between points
		const validated = validateAllPointsMonotonicity(points);
		
		// Update presetInputs state to reflect validated values
		const newPresetInputs = [...presetInputs];
		const validatedPointsForInput: Array<{ x: string; y: string }> = [
			{ x: '0', y: '0' },
			{ x: '0', y: '0' },
			{ x: '0', y: '0' },
		];
		
		// Fill in validated points, pad with zeros if less than 3
		for (let i = 0; i < 3; i++) {
			if (i < validated.length) {
				validatedPointsForInput[i] = {
					x: parseFloat(validated[i].x.toFixed(4)).toString(),
					y: parseFloat(validated[i].y.toFixed(4)).toString(),
				};
			}
		}
		
		newPresetInputs[presetIndex].points = validatedPointsForInput;
		setPresetInputs(newPresetInputs);
		
		// Save to formik in new array format
		// Build presets array from all presetInputs, only include presets with name or points
		const allPresets: Array<{ name: string; points: CurvePoint[] }> = [];
		for (let i = 0; i < 4; i++) {
			const currentPreset = i === presetIndex 
				? { name: preset.name, points: validated }
				: presetInputs[i];
			
			// Convert current preset to points array
			const currentPoints: CurvePoint[] = [];
			if (i === presetIndex) {
				// Use validated points directly
				currentPoints.push(...validated);
			} else {
				// Convert string inputs to numbers
				for (let j = 0; j < currentPreset.points.length; j++) {
					const x = parseFloat(String(currentPreset.points[j].x)) || 0;
					const y = parseFloat(String(currentPreset.points[j].y)) || 0;
					if (x > 0 || y > 0) {
						currentPoints.push({ x: Math.max(0, Math.min(1, x)), y: Math.max(0, Math.min(1, y)) });
					}
				}
			}
			
			// Only include preset if it has name or points
			if (currentPreset.name.trim() || currentPoints.length > 0) {
				allPresets.push({
					name: currentPreset.name.trim(),
					points: currentPoints
				});
			}
		}
		
		setFieldValue('joystickCurvePresets', allPresets);
	};
	
	// Handle apply preset to left stick
	const handleApplyPresetToLeft = (presetIndex: number) => {
		const preset = presetInputs[presetIndex];
		const points: CurvePoint[] = [];
		
		for (let i = 0; i < preset.points.length; i++) {
			const x = parseFloat(preset.points[i].x) || 0;
			const y = parseFloat(preset.points[i].y) || 0;
			if (x > 0 || y > 0) {
				points.push({ x: Math.max(0, Math.min(1, x)), y: Math.max(0, Math.min(1, y)) });
			}
		}
		
		// Sort by X value and validate
		const sorted = points.sort((a, b) => a.x - b.x);
		const validated = validateAllPointsMonotonicity(sorted);
		setLeftCurvePoints(validated);
		setLeftCurveInputValues(validated.map(p => ({ x: parseFloat(p.x.toFixed(4)).toString(), y: parseFloat(p.y.toFixed(4)).toString() })));
	};
	
	// Handle apply preset to right stick
	const handleApplyPresetToRight = (presetIndex: number) => {
		const preset = presetInputs[presetIndex];
		const points: CurvePoint[] = [];
		
		for (let i = 0; i < preset.points.length; i++) {
			const x = parseFloat(preset.points[i].x) || 0;
			const y = parseFloat(preset.points[i].y) || 0;
			if (x > 0 || y > 0) {
				points.push({ x: Math.max(0, Math.min(1, x)), y: Math.max(0, Math.min(1, y)) });
			}
		}
		
		// Sort by X value and validate
		const sorted = points.sort((a, b) => a.x - b.x);
		const validated = validateAllPointsMonotonicity(sorted);
		setRightCurvePoints(validated);
		setRightCurveInputValues(validated.map(p => ({ x: parseFloat(p.x.toFixed(4)).toString(), y: parseFloat(p.y.toFixed(4)).toString() })));
	};
	
	return (
		<Section title="摇杆曲线设置">
			{isExpanded && (
			<div className="mb-3" style={{ display: 'grid', gridTemplateColumns: 'repeat(4, 260px)', gridTemplateRows: 'auto auto auto', gap: '16px', justifyContent: 'center', alignItems: 'start', width: 'max-content', margin: '0 auto' }}>
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

				{/* Row 1, Column 2: Left stick deadzone/anti-deadzone sliders and control points */}
				<div style={{ width: '260px', display: 'flex', flexDirection: 'column', justifyContent: 'flex-start', alignItems: 'flex-start' }}>
					<div style={{ width: '260px', textAlign: 'left', marginBottom: '16px' }}>
						<div style={{ marginBottom: '6px' }}>
							<Form.Label className="mb-0" style={{ textAlign: 'left', display: 'block', width: '100%', fontSize: '0.875rem', marginBottom: '4px' }}>
								内部死区: {(values?.inner_deadzone || 0).toFixed(1)}%
							</Form.Label>
							<Form.Range
								min={0}
								max={10}
								step={0.5}
								value={values?.inner_deadzone || 0}
								onChange={(e) => setFieldValue('inner_deadzone', Math.round(parseFloat(e.target.value) * 2) / 2)}
							/>
						</div>
						<div style={{ marginBottom: '6px' }}>
							<div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', marginBottom: '4px' }}>
								<Form.Label className="mb-0" style={{ textAlign: 'left', fontSize: '0.875rem', marginBottom: '0' }}>
									反死区: {(values?.anti_deadzone || 0).toFixed(1)}%
								</Form.Label>
								<div style={{ display: 'flex', alignItems: 'center', gap: '6px' }}>
									<span style={{ fontSize: '0.75rem', color: '#6c757d' }}>
										{values?.fixed_anti_deadzone ? '固定' : '线性'}
									</span>
									<Form.Check
										type="switch"
										id="fixed-anti-deadzone-1"
										label=""
										checked={values?.fixed_anti_deadzone || false}
										onChange={(e) => setFieldValue('fixed_anti_deadzone', e.target.checked)}
									/>
								</div>
							</div>
							<Form.Range
								min={0}
								max={10}
								step={0.5}
								value={values?.anti_deadzone || 0}
								onChange={(e) => setFieldValue('anti_deadzone', Math.round(parseFloat(e.target.value) * 2) / 2)}
							/>
						</div>
					</div>
					<div style={{ width: '260px', textAlign: 'left' }}>
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
													onChange={(e) => handleLeftInputChange(originalIndex, 'x', e.target.value)}
													onBlur={() => handleLeftInputBlur(originalIndex)}
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
													onChange={(e) => handleLeftInputChange(originalIndex, 'y', e.target.value)}
													onBlur={() => handleLeftInputBlur(originalIndex)}
												/>
												<Button
													variant="danger"
													size="sm"
													onClick={() => handleLeftDelete(originalIndex)}
													style={{ padding: '2px 8px', fontSize: '0.75rem' }}
												>
													删除
												</Button>
											</div>
										);
									})}
							</div>
						) : (
							<div style={{ fontSize: '0.875rem', color: '#6c757d' }}>暂无控制点</div>
						)}
					</div>
				</div>

				{/* Row 1, Column 3: Right stick deadzone/anti-deadzone sliders and control points */}
				<div style={{ width: '260px', display: 'flex', flexDirection: 'column', justifyContent: 'flex-start', alignItems: 'flex-start' }}>
					<div style={{ width: '260px', textAlign: 'left', marginBottom: '16px' }}>
						<div style={{ marginBottom: '6px' }}>
							<Form.Label className="mb-0" style={{ textAlign: 'left', display: 'block', width: '100%', fontSize: '0.875rem', marginBottom: '4px' }}>
								内部死区: {(values?.inner_deadzone2 || 0).toFixed(1)}%
							</Form.Label>
							<Form.Range
								min={0}
								max={10}
								step={0.5}
								value={values?.inner_deadzone2 || 0}
								onChange={(e) => setFieldValue('inner_deadzone2', Math.round(parseFloat(e.target.value) * 2) / 2)}
							/>
						</div>
						<div style={{ marginBottom: '6px' }}>
							<div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', marginBottom: '4px' }}>
								<Form.Label className="mb-0" style={{ textAlign: 'left', fontSize: '0.875rem', marginBottom: '0' }}>
									反死区: {(values?.anti_deadzone2 || 0).toFixed(1)}%
								</Form.Label>
								<div style={{ display: 'flex', alignItems: 'center', gap: '6px' }}>
									<span style={{ fontSize: '0.75rem', color: '#6c757d' }}>
										{values?.fixed_anti_deadzone2 ? '固定' : '线性'}
									</span>
									<Form.Check
										type="switch"
										id="fixed-anti-deadzone-2"
										label=""
										checked={values?.fixed_anti_deadzone2 || false}
										onChange={(e) => setFieldValue('fixed_anti_deadzone2', e.target.checked)}
									/>
								</div>
							</div>
							<Form.Range
								min={0}
								max={10}
								step={0.5}
								value={values?.anti_deadzone2 || 0}
								onChange={(e) => setFieldValue('anti_deadzone2', Math.round(parseFloat(e.target.value) * 2) / 2)}
							/>
						</div>
					</div>
					<div style={{ width: '260px', textAlign: 'left' }}>
						<div style={{ fontWeight: 'bold', marginBottom: '4px', textAlign: 'left', fontSize: '0.875rem' }}>控制点</div>
						{rightCurvePoints.length > 0 ? (
							<div style={{ fontSize: '0.875rem' }}>
								{rightCurvePoints.map((point, originalIndex) => ({ point, originalIndex }))
									.sort((a, b) => a.point.x - b.point.x)
									.map(({ point, originalIndex }) => {
										const inputValue = rightCurveInputValues[originalIndex] || { x: parseFloat(point.x.toFixed(4)).toString(), y: parseFloat(point.y.toFixed(4)).toString() };
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
													onChange={(e) => handleRightInputChange(originalIndex, 'x', e.target.value)}
													onBlur={() => handleRightInputBlur(originalIndex)}
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
													onChange={(e) => handleRightInputChange(originalIndex, 'y', e.target.value)}
													onBlur={() => handleRightInputBlur(originalIndex)}
												/>
												<Button
													variant="danger"
													size="sm"
													onClick={() => handleRightDelete(originalIndex)}
													style={{ padding: '2px 8px', fontSize: '0.75rem' }}
												>
													删除
												</Button>
											</div>
										);
									})}
							</div>
						) : (
							<div style={{ fontSize: '0.875rem', color: '#6c757d' }}>暂无控制点</div>
						)}
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

				{/* Row 2, Column 1: Left stick physical/output distance */}
				<div style={{ display: 'flex', flexDirection: 'column', justifyContent: 'center', alignItems: 'center', width: '260px', gridColumn: '1', gap: '8px' }}>
					<div style={{ fontSize: '0.875rem', textAlign: 'center' }}>
						<div>当前摇杆物理距离：{(leftLightX !== undefined ? leftLightX * 100 : 0).toFixed(1)}%</div>
						<div>当前摇杆输出距离：{(leftLightY !== undefined ? leftLightY * 100 : 0).toFixed(1)}%</div>
					</div>
				</div>

				{/* Row 2, Column 2: Left stick reset and confirm buttons */}
				<div style={{ display: 'flex', flexDirection: 'column', justifyContent: 'center', alignItems: 'center', width: '260px', gridColumn: '2', gap: '8px' }}>
					<div className="d-flex gap-2 justify-content-center">
						<Button
							variant="secondary"
							size="sm"
							onClick={handleLeftReset}
						>
							重置
						</Button>
						<Button
							variant="primary"
							size="sm"
							onClick={handleLeftConfirm}
						>
							确定
						</Button>
					</div>
				</div>

				{/* Row 2, Column 3: Right stick reset and confirm buttons */}
				<div style={{ display: 'flex', flexDirection: 'column', justifyContent: 'center', alignItems: 'center', width: '260px', gridColumn: '3', gap: '8px' }}>
					<div className="d-flex gap-2 justify-content-center">
						<Button
							variant="secondary"
							size="sm"
							onClick={handleRightReset}
						>
							重置
						</Button>
						<Button
							variant="primary"
							size="sm"
							onClick={handleRightConfirm}
						>
							确定
						</Button>
					</div>
				</div>

				{/* Row 2, Column 4: Right stick physical/output distance */}
				<div style={{ display: 'flex', flexDirection: 'column', justifyContent: 'center', alignItems: 'center', width: '260px', gridColumn: '4', gap: '8px' }}>
					<div style={{ fontSize: '0.875rem', textAlign: 'center' }}>
						<div>当前摇杆物理距离：{(rightLightX !== undefined ? rightLightX * 100 : 0).toFixed(1)}%</div>
						<div>当前摇杆输出距离：{(rightLightY !== undefined ? rightLightY * 100 : 0).toFixed(1)}%</div>
					</div>
				</div>

				{/* Row 3, Column 1: Preset 1 */}
				<div style={{ display: 'flex', flexDirection: 'column', width: '260px', gridColumn: '1', gap: '8px', padding: '8px', border: '1px solid #ddd', borderRadius: '4px' }}>
					{/* Row 1: Name input and apply buttons */}
					<div style={{ display: 'flex', gap: '4px', alignItems: 'center', marginBottom: '4px' }}>
						<Form.Control
							type="text"
							size="sm"
							placeholder="预设名称"
							value={presetInputs[0].name}
							onChange={(e) => handlePresetNameChange(0, e.target.value)}
							style={{ flex: 1, fontSize: '0.875rem' }}
						/>
						<Button
							variant="outline-primary"
							size="sm"
							onClick={() => handleApplyPresetToLeft(0)}
							style={{ fontSize: '0.75rem', padding: '2px 8px' }}
						>
							应用左
						</Button>
						<Button
							variant="outline-primary"
							size="sm"
							onClick={() => handleApplyPresetToRight(0)}
							style={{ fontSize: '0.75rem', padding: '2px 8px' }}
						>
							应用右
						</Button>
					</div>
					{/* Row 2-4: Control points */}
					{['P1', 'P2', 'P3'].map((label, idx) => (
						<div key={idx} style={{ display: 'flex', gap: '4px', alignItems: 'center', fontSize: '0.875rem' }}>
							<span style={{ minWidth: '24px' }}>{label}:</span>
							<Form.Control
								type="number"
								size="sm"
								min={0}
								max={1}
								step={0.001}
								placeholder="X"
								value={presetInputs[0].points[idx].x}
								onChange={(e) => handlePresetPointChange(0, idx, 'x', e.target.value)}
								onBlur={() => handlePresetPointBlur(0)}
								style={{ width: '80px', fontSize: '0.8rem', padding: '2px 6px' }}
							/>
							<Form.Control
								type="number"
								size="sm"
								min={0}
								max={1}
								step={0.001}
								placeholder="Y"
								value={presetInputs[0].points[idx].y}
								onChange={(e) => handlePresetPointChange(0, idx, 'y', e.target.value)}
								onBlur={() => handlePresetPointBlur(0)}
								style={{ width: '80px', fontSize: '0.8rem', padding: '2px 6px' }}
							/>
						</div>
					))}
				</div>

				{/* Row 3, Column 2: Preset 2 */}
				<div style={{ display: 'flex', flexDirection: 'column', width: '260px', gridColumn: '2', gap: '8px', padding: '8px', border: '1px solid #ddd', borderRadius: '4px' }}>
					<div style={{ display: 'flex', gap: '4px', alignItems: 'center', marginBottom: '4px' }}>
						<Form.Control
							type="text"
							size="sm"
							placeholder="预设名称"
							value={presetInputs[1].name}
							onChange={(e) => handlePresetNameChange(1, e.target.value)}
							style={{ flex: 1, fontSize: '0.875rem' }}
						/>
						<Button
							variant="outline-primary"
							size="sm"
							onClick={() => handleApplyPresetToLeft(1)}
							style={{ fontSize: '0.75rem', padding: '2px 8px' }}
						>
							应用左
						</Button>
						<Button
							variant="outline-primary"
							size="sm"
							onClick={() => handleApplyPresetToRight(1)}
							style={{ fontSize: '0.75rem', padding: '2px 8px' }}
						>
							应用右
						</Button>
					</div>
					{['P1', 'P2', 'P3'].map((label, idx) => (
						<div key={idx} style={{ display: 'flex', gap: '4px', alignItems: 'center', fontSize: '0.875rem' }}>
							<span style={{ minWidth: '24px' }}>{label}:</span>
							<Form.Control
								type="number"
								size="sm"
								min={0}
								max={1}
								step={0.001}
								placeholder="X"
								value={presetInputs[1].points[idx].x}
								onChange={(e) => handlePresetPointChange(1, idx, 'x', e.target.value)}
								onBlur={() => handlePresetPointBlur(1)}
								style={{ width: '80px', fontSize: '0.8rem', padding: '2px 6px' }}
							/>
							<Form.Control
								type="number"
								size="sm"
								min={0}
								max={1}
								step={0.001}
								placeholder="Y"
								value={presetInputs[1].points[idx].y}
								onChange={(e) => handlePresetPointChange(1, idx, 'y', e.target.value)}
								onBlur={() => handlePresetPointBlur(1)}
								style={{ width: '80px', fontSize: '0.8rem', padding: '2px 6px' }}
							/>
						</div>
					))}
				</div>

				{/* Row 3, Column 3: Preset 3 */}
				<div style={{ display: 'flex', flexDirection: 'column', width: '260px', gridColumn: '3', gap: '8px', padding: '8px', border: '1px solid #ddd', borderRadius: '4px' }}>
					<div style={{ display: 'flex', gap: '4px', alignItems: 'center', marginBottom: '4px' }}>
						<Form.Control
							type="text"
							size="sm"
							placeholder="预设名称"
							value={presetInputs[2].name}
							onChange={(e) => handlePresetNameChange(2, e.target.value)}
							style={{ flex: 1, fontSize: '0.875rem' }}
						/>
						<Button
							variant="outline-primary"
							size="sm"
							onClick={() => handleApplyPresetToLeft(2)}
							style={{ fontSize: '0.75rem', padding: '2px 8px' }}
						>
							应用左
						</Button>
						<Button
							variant="outline-primary"
							size="sm"
							onClick={() => handleApplyPresetToRight(2)}
							style={{ fontSize: '0.75rem', padding: '2px 8px' }}
						>
							应用右
						</Button>
					</div>
					{['P1', 'P2', 'P3'].map((label, idx) => (
						<div key={idx} style={{ display: 'flex', gap: '4px', alignItems: 'center', fontSize: '0.875rem' }}>
							<span style={{ minWidth: '24px' }}>{label}:</span>
							<Form.Control
								type="number"
								size="sm"
								min={0}
								max={1}
								step={0.001}
								placeholder="X"
								value={presetInputs[2].points[idx].x}
								onChange={(e) => handlePresetPointChange(2, idx, 'x', e.target.value)}
								onBlur={() => handlePresetPointBlur(2)}
								style={{ width: '80px', fontSize: '0.8rem', padding: '2px 6px' }}
							/>
							<Form.Control
								type="number"
								size="sm"
								min={0}
								max={1}
								step={0.001}
								placeholder="Y"
								value={presetInputs[2].points[idx].y}
								onChange={(e) => handlePresetPointChange(2, idx, 'y', e.target.value)}
								onBlur={() => handlePresetPointBlur(2)}
								style={{ width: '80px', fontSize: '0.8rem', padding: '2px 6px' }}
							/>
						</div>
					))}
				</div>

				{/* Row 3, Column 4: Preset 4 */}
				<div style={{ display: 'flex', flexDirection: 'column', width: '260px', gridColumn: '4', gap: '8px', padding: '8px', border: '1px solid #ddd', borderRadius: '4px' }}>
					<div style={{ display: 'flex', gap: '4px', alignItems: 'center', marginBottom: '4px' }}>
						<Form.Control
							type="text"
							size="sm"
							placeholder="预设名称"
							value={presetInputs[3].name}
							onChange={(e) => handlePresetNameChange(3, e.target.value)}
							style={{ flex: 1, fontSize: '0.875rem' }}
						/>
						<Button
							variant="outline-primary"
							size="sm"
							onClick={() => handleApplyPresetToLeft(3)}
							style={{ fontSize: '0.75rem', padding: '2px 8px' }}
						>
							应用左
						</Button>
						<Button
							variant="outline-primary"
							size="sm"
							onClick={() => handleApplyPresetToRight(3)}
							style={{ fontSize: '0.75rem', padding: '2px 8px' }}
						>
							应用右
						</Button>
					</div>
					{['P1', 'P2', 'P3'].map((label, idx) => (
						<div key={idx} style={{ display: 'flex', gap: '4px', alignItems: 'center', fontSize: '0.875rem' }}>
							<span style={{ minWidth: '24px' }}>{label}:</span>
							<Form.Control
								type="number"
								size="sm"
								min={0}
								max={1}
								step={0.001}
								placeholder="X"
								value={presetInputs[3].points[idx].x}
								onChange={(e) => handlePresetPointChange(3, idx, 'x', e.target.value)}
								onBlur={() => handlePresetPointBlur(3)}
								style={{ width: '80px', fontSize: '0.8rem', padding: '2px 6px' }}
							/>
							<Form.Control
								type="number"
								size="sm"
								min={0}
								max={1}
								step={0.001}
								placeholder="Y"
								value={presetInputs[3].points[idx].y}
								onChange={(e) => handlePresetPointChange(3, idx, 'y', e.target.value)}
								onBlur={() => handlePresetPointBlur(3)}
								style={{ width: '80px', fontSize: '0.8rem', padding: '2px 6px' }}
							/>
						</div>
					))}
				</div>
			</div>
			)}
			
			{/* Bottom section: Save button (left) and Toggle switch (right) */}
			<div style={{ marginTop: '16px', display: 'flex', justifyContent: 'space-between', alignItems: 'center', width: '100%' }}>
				{/* Save button on the left */}
				<div>
					<Button
						variant="primary"
						onClick={(e) => {
							e.preventDefault();
							handleSubmit();
						}}
					>
						{t('Common:button-save-label')}
					</Button>
				</div>
				{/* Toggle switch on the right */}
				<div style={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
					<Form.Check
						type="switch"
						id="joystick-curve-enabled"
						label="启用摇杆曲线"
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
