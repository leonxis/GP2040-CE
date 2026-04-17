import { useEffect, useRef, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { useFormikContext } from 'formik';
import { Button, Modal, Table, Form } from 'react-bootstrap';

import Section from '../../../Components/Section';
import StickCalibrationModal from '../../../Components/StickCalibrationModal';
import RangeCalibrationModal from '../../../Components/RangeCalibrationModal';
import type { AddonPropTypes } from './CalibrationSettings';
import StickPositionInfo from './StickPositionInfo';
import FinetuneShapeControls from './FinetuneShapeControls';
import StickButtons from './StickButtons';
import ViewCalibrationData from './ViewCalibrationData';

// Type definitions
type CurvePoint = { x: number; y: number };

const CIRCULARITY_DATA_SIZE = 48; // Number of angular positions to sample
const DEFAULT_ADC_MAX = 4095;
const DEFAULT_ADC_CENTER = DEFAULT_ADC_MAX / 2.0;
const ADS8332_IIR_STRENGTH_OPTIONS = [0.125, 0.25, 0.5, 1.0];

// Layout constants
const COLUMN_WIDTH = '260px';
const CANVAS_SIZE = 260;

// Common layout styles
const canvasContainerStyle: React.CSSProperties = {
	display: 'flex',
	flexDirection: 'column',
	alignItems: 'center',
	justifyContent: 'flex-start',
	width: COLUMN_WIDTH
};

const canvasWrapperStyle: React.CSSProperties = {
	position: 'relative',
	width: COLUMN_WIDTH,
	height: COLUMN_WIDTH
};

const canvasStyle: React.CSSProperties = {
	display: 'block'
};

export const finetuneControlsContainerStyle: React.CSSProperties = {
	width: COLUMN_WIDTH,
	display: 'flex',
	justifyContent: 'center',
	alignItems: 'flex-start'
};

export const finetuneControlsBoxStyle: React.CSSProperties = {
	width: COLUMN_WIDTH,
	textAlign: 'left',
	border: '1px solid #dee2e6',
	borderRadius: '4px',
	padding: '8px'
};

export const finetuneControlsTitleStyle: React.CSSProperties = {
	fontWeight: 'bold',
	marginBottom: '8px',
	textAlign: 'center'
};

const positionInfoContainerStyle: React.CSSProperties = {
	display: 'flex',
	justifyContent: 'center',
	alignItems: 'center',
	width: COLUMN_WIDTH
};

const buttonsContainerStyle: React.CSSProperties = {
	display: 'flex',
	flexDirection: 'column',
	alignItems: 'center',
	gap: '8px',
	width: COLUMN_WIDTH
};

export const positionInfoInnerStyle: React.CSSProperties = {
	display: 'block',
	width: '100%'
};

export const positionInfoRowStyle: React.CSSProperties = {
	display: 'flex',
	alignItems: 'center',
	justifyContent: 'center',
	gap: '4px'
};

export const positionValueStyle: React.CSSProperties = {
	minWidth: '60px',
	textAlign: 'center',
	display: 'inline-block'
};

export const positionNormalizedStyle: React.CSSProperties = {
	marginLeft: '8px',
	minWidth: '90px',
	textAlign: 'left',
	display: 'inline-block'
};

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
 * Converts stick value (-1 to 1) to XINPUT-quantized normalized value.
 * @param stickValue - Stick value in range -1 to 1
 * @returns XINPUT-quantized normalized value in range -1 to 1
 */
const convertToXInputNormalized = (stickValue: number): string => {
	const clamped = Math.max(-1, Math.min(1, stickValue));
	const xinputRaw = clamped >= 0
		? Math.round(clamped * 32767)
		: Math.round(clamped * 32768);
	const normalized = xinputRaw >= 0
		? xinputRaw / 32767
		: xinputRaw / 32768;
	return normalized.toFixed(5);
};

/**
 * Common button style for finetune center adjustment buttons
 */
export const finetuneButtonStyle: React.CSSProperties = {
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
 * @returns Interpolated scale value, or 0.65 if no calibration data (default scaling)
 */
const getInterpolatedScale = (angle: number, rangeData: number[]): number => {
	// Check if we have calibration data
	// If no calibration data, return 0.65 for default scaling (matches backend logic)
	if (!rangeData || rangeData.length === 0 || rangeData.every(v => v <= 0)) {
		return 0.65;
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
/**
 * Processes joystick data through coordinate transformation pipeline (matches backend logic)
 * This function implements backend Steps 1-3:
 * - Step 1: Transform to center-relative coordinates (cx, cy)
 * - Step 2: Range calibration scaling (sx, sy)
 * - Step 3: Normalize to [-1, 1] range (nx, ny) - WITHOUT inversion and square trimming
 * 
 * Inversion, deadzone/anti-deadzone, square trimming, and curve are applied separately
 * to match the exact backend processing order.
 * 
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
	rangeData: number[],
	adcMax: number
) => {
	const adcCenter = adcMax / 2.0;
	// Step 1: Transform to center-relative coordinates (matches backend Step 1)
	// Backend: cx = readPin(...) - x_center
	// Frontend: offset_center_x = (rawX - (centerX - adcCenter)) - adcCenter = rawX - centerX
	const dX_value = centerX - adcCenter;
	const dY_value = centerY - adcCenter;
	const offset_x = rawX - dX_value;
	const offset_y = rawY - dY_value;
	const offset_center_x = offset_x - adcCenter;  // Equivalent to rawX - centerX
	const offset_center_y = offset_y - adcCenter;  // Equivalent to rawY - centerY
	
	// Step 2: Range calibration scaling (matches backend Step 2)
	// Backend: scale = getInterpolatedScale(i, atan2(cy, cx)); sx = cx / scale
	const current_distance = Math.sqrt(offset_center_x * offset_center_x + offset_center_y * offset_center_y);
	const angle = Math.atan2(offset_center_y, offset_center_x);
	const scale = getInterpolatedScale(angle, rangeData);
	const angleIndex = Math.round((angle + Math.PI) * CIRCULARITY_DATA_SIZE / (2 * Math.PI)) % CIRCULARITY_DATA_SIZE;
	
	// Backend directly divides by scale: sx = cx / scale (no zero distance check needed, as 0/scale = 0)
	const scaled_center_x = offset_center_x / scale;
	const scaled_center_y = offset_center_y / scale;
	
	// Step 3: Normalize to [-1, 1] range (matches backend Step 3, before inversion)
	// Backend: nx = sx / ADC_MAX_HALF
	// Note: Inversion, deadzone/anti-deadzone, square trimming, and curve are applied separately
	const stickX = scaled_center_x / adcCenter;  // Equivalent to scaled_center_x / ADC_MAX_HALF
	const stickY = scaled_center_y / adcCenter;  // Equivalent to scaled_center_y / ADC_MAX_HALF
	
		return {
			stickX,
			stickY,
			detailData: {
				centerX,
				centerY,
				angleIndex,
				scale,
				currentDistance: current_distance, // Distance before range calibration scaling
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
 * Pre-computed angle values for circularity visualization (performance optimization)
 */
const CIRCULARITY_ANGLES: { cos: number; sin: number }[] = (() => {
	const angles: { cos: number; sin: number }[] = [];
	const MAX_N = CIRCULARITY_DATA_SIZE;
	for (let i = 0; i < MAX_N; i++) {
		const ka = i * Math.PI * 2 / MAX_N;
		angles.push({ cos: Math.cos(ka), sin: Math.sin(ka) });
	}
	return angles;
})();

/**
 * Cache for static background canvas (performance optimization)
 */
interface StaticCanvasCache {
	canvas: HTMLCanvasElement;
	ctx: CanvasRenderingContext2D;
	key: string;
}

const staticCanvasCache = new Map<string, StaticCanvasCache>();

/**
 * Generates a cache key for static canvas
 */
const getStaticCanvasKey = (
	width: number,
	height: number,
	centerX: number,
	centerY: number,
	radius: number,
	zoom10x: boolean,
	circularityData: number[] | null | undefined,
	showErrorRate: boolean,
): string => {
	const circularityHash = circularityData ? circularityData.join(',') : 'null';
	return `${width}x${height}_${centerX}_${centerY}_${radius}_${zoom10x}_${showErrorRate}_${circularityHash}`;
};

/**
 * Draws static background on offscreen canvas (cached for performance)
 */
const drawStaticBackground = (
	width: number,
	height: number,
	centerX: number,
	centerY: number,
	radius: number,
	zoom10x: boolean,
	circularityData: number[] | null | undefined,
	showErrorRate: boolean,
): HTMLCanvasElement => {
	const cacheKey = getStaticCanvasKey(width, height, centerX, centerY, radius, zoom10x, circularityData, showErrorRate);
	
	// Check cache
	if (staticCanvasCache.has(cacheKey)) {
		return staticCanvasCache.get(cacheKey)!.canvas;
	}

	// Create new offscreen canvas
	const canvas = document.createElement('canvas');
	canvas.width = width;
	canvas.height = height;
	const ctx = canvas.getContext('2d')!;

	// Use integer coordinates for better performance
	const intCenterX = Math.round(centerX);
	const intCenterY = Math.round(centerY);
	const intRadius = Math.round(radius);

	// Keep canvas background transparent so card background can show through.
	ctx.clearRect(0, 0, canvas.width, canvas.height);

	// Calculate effective radius and scale based on zoom mode
	let effectiveRadius = intRadius;
	let scale = 1.0;
	if (zoom10x) {
		scale = 0.1;
		effectiveRadius = intRadius;
	}

	// Draw base circle (outer boundary)
	ctx.lineWidth = 2;
	ctx.strokeStyle = '#d0d0d0';
	ctx.beginPath();
	ctx.arc(intCenterX, intCenterY, effectiveRadius, 0, 2 * Math.PI);
	ctx.closePath();
	ctx.stroke();

	// Draw red dashed circle at 0.03528 radius in zoom mode
	if (zoom10x) {
		const redCircleRadius = Math.round((0.03528 / scale) * effectiveRadius);
		ctx.strokeStyle = '#ff0000';
		ctx.lineWidth = 1;
		ctx.setLineDash([5, 5]);
		ctx.beginPath();
		ctx.arc(intCenterX, intCenterY, redCircleRadius, 0, 2 * Math.PI);
		ctx.closePath();
		ctx.stroke();
		ctx.setLineDash([]);
	}

	// Draw circularity visualization if data provided
	if (showErrorRate !== false && circularityData && circularityData.length > 0) {
		const MAX_N = CIRCULARITY_DATA_SIZE;
		for (let i = 0; i < MAX_N; i++) {
			const kd = circularityData[i];
			const kd1 = circularityData[(i + 1) % CIRCULARITY_DATA_SIZE];
			if (kd === undefined || kd1 === undefined || kd === 0) continue;
			
			const angle = CIRCULARITY_ANGLES[i];
			const angle1 = CIRCULARITY_ANGLES[(i + 1) % MAX_N];

			// Use pre-computed cos/sin values
			const kx = angle.cos * kd;
			const ky = angle.sin * kd;
			const kx1 = angle1.cos * kd1;
			const ky1 = angle1.sin * kd1;

			ctx.beginPath();
			ctx.moveTo(intCenterX, intCenterY);
			ctx.lineTo(Math.round(intCenterX + kx * intRadius), Math.round(intCenterY + ky * intRadius));
			ctx.lineTo(Math.round(intCenterX + kx1 * intRadius), Math.round(intCenterY + ky1 * intRadius));
			ctx.lineTo(intCenterX, intCenterY);
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
	ctx.moveTo(intCenterX - intRadius, intCenterY);
	ctx.lineTo(intCenterX + intRadius, intCenterY);
	ctx.closePath();
	ctx.stroke();

	ctx.beginPath();
	ctx.moveTo(intCenterX, intCenterY - intRadius);
	ctx.lineTo(intCenterX, intCenterY + intRadius);
	ctx.closePath();
	ctx.stroke();

	// Draw circularity error text if enough data provided
	if (showErrorRate !== false && circularityData && circularityData.filter(n => n > 0.3).length > 10) {
		const circularityError = calculateCircularityError(circularityData);

		ctx.fillStyle = '#fff';
		ctx.strokeStyle = '#444';
		ctx.lineWidth = 3;
		ctx.textAlign = 'center';
		ctx.textBaseline = 'middle';

		ctx.font = '24px Arial';
		const textY = Math.round(intCenterY + intRadius * 0.5);
		const text = `${circularityError.toFixed(1)} %`;

		ctx.strokeText(text, intCenterX, textY);
		ctx.fillText(text, intCenterX, textY);
	}

	// Cache the result
	staticCanvasCache.set(cacheKey, { canvas, ctx, key: cacheKey });

	// Limit cache size to prevent memory issues
	if (staticCanvasCache.size > 10) {
		const firstKey = staticCanvasCache.keys().next().value;
		if (firstKey !== undefined) {
		staticCanvasCache.delete(firstKey);
		}
	}

	return canvas;
};

/**
 * Draws analog stick position on a canvas (optimized version)
 * Uses offscreen canvas caching for static background
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
	showErrorRate?: boolean, // If true, show coverage range and error rate
) => {
	const canvas = ctx.canvas;
	const width = canvas.width;
	const height = canvas.height;

	// Draw cached static background
	const staticCanvas = drawStaticBackground(
		width,
		height,
		centerX,
		centerY,
		radius,
		zoom10x || false,
		circularityData,
		showErrorRate !== false,
	);
	ctx.clearRect(0, 0, width, height);
	ctx.drawImage(staticCanvas, 0, 0);

	// Use integer coordinates for better performance
	const intCenterX = Math.round(centerX);
	const intCenterY = Math.round(centerY);
	const intRadius = Math.round(radius);

	// Calculate effective radius and scale based on zoom mode
	let effectiveRadius = intRadius;
	let scale = 1.0;
	if (zoom10x) {
		scale = 0.1;
		effectiveRadius = intRadius;
	}

	// Draw stick line from center to position (scaled for zoom mode) - dynamic part only
	const scaledStickX = zoom10x ? stickX / scale : stickX;
	const scaledStickY = zoom10x ? stickY / scale : stickY;
	const stickPosX = Math.round(intCenterX + scaledStickX * effectiveRadius);
	const stickPosY = Math.round(intCenterY + scaledStickY * effectiveRadius);

	ctx.strokeStyle = '#d0d0d0';
	ctx.lineWidth = 2;
	ctx.beginPath();
	ctx.moveTo(intCenterX, intCenterY);
	ctx.lineTo(stickPosX, stickPosY);
	ctx.stroke();

	// Draw filled circle at stick position
	ctx.beginPath();
	ctx.arc(stickPosX, stickPosY, 4, 0, 2 * Math.PI);
	ctx.fillStyle = '#ffa500';
	ctx.fill();
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
	const sortedPoints = [...points].sort((a, b) => a.x - b.x);
	const fullPoints: CurvePoint[] = [
		{x: 0, y: 0},
		...sortedPoints, // Sort by x coordinate without mutating original
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
 * Quantize raw ADC to nearest multiple of step (matches analog.cpp / mcp3208 getStickRaw).
 * step 0 = full 12-bit; step = 2^(16-b) from config.
 */
const quantizeAdc = (adc: number, step: number, adcMax: number): number => {
	if (!step || step <= 0) return adc;
	// Match firmware: 12-bit integer ADC; API may return floats.
	const adcInt = Math.round(adc);
	const maxStep = adcMax + 1;
	const s = Math.min(Math.max(1, Math.floor(step)), maxStep);
	const half = Math.floor(s / 2);
	let q = Math.floor((adcInt + half) / s) * s;
	if (q > adcMax) q = adcMax;
	return q;
};

const quantizeAdcPair = (
	rawX: number,
	rawY: number,
	step: number,
	adcMax: number
): { x: number; y: number } => ({
	x: quantizeAdc(rawX, step, adcMax),
	y: quantizeAdc(rawY, step, adcMax),
});

/** Firmware stores quantize step; 0 = off. UI uses bits b∈[4,16] with step = 2^(16-b). */
const JITTER_BITS_MIN = 4;
const JITTER_BITS_MAX = 16;

const bitsToStoredThreshold = (bits: number): number => {
	const b = Math.round(bits);
	const clamped = Math.min(JITTER_BITS_MAX, Math.max(JITTER_BITS_MIN, b));
	return Math.round(Math.pow(2, 16 - clamped));
};

/** Map stored threshold to UI bits; 0 (legacy off) displays as 16 bit. */
const storedThresholdToBits = (threshold: number): number => {
	if (threshold == null || threshold <= 0) return JITTER_BITS_MAX;
	const raw = 16 - Math.log2(threshold) / Math.LN2;
	return Math.min(JITTER_BITS_MAX, Math.max(JITTER_BITS_MIN, Math.round(raw)));
};

const JoystickCalibration = ({
	values,
	setFieldValue,
	saveMessage = '',
	onSaveClick,
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
	const [showLeftRangeDataModal, setShowLeftRangeDataModal] = useState(false);
	const [showRightRangeDataModal, setShowRightRangeDataModal] = useState(false);
	const [leftRangeDataSnapshot, setLeftRangeDataSnapshot] = useState<number[]>([]);
	const [rightRangeDataSnapshot, setRightRangeDataSnapshot] = useState<number[]>([]);
	const [leftAngleIndexSnapshot, setLeftAngleIndexSnapshot] = useState(0);
	const [rightAngleIndexSnapshot, setRightAngleIndexSnapshot] = useState(0);
	const [leftFinetuneCenterActive, setLeftFinetuneCenterActive] = useState(false);
	const [rightFinetuneCenterActive, setRightFinetuneCenterActive] = useState(false);
	const [showRangeCalibrationWarning, setShowRangeCalibrationWarning] = useState(false);
	// Curve control points: array of {x, y} where x and y are in [0, 1] range
	// Maximum 3 points (plus start (0,0) and end (1,1)) = 4 segments
	// Load from config if available - these are used for curve application in stick position canvas
	
	// Jitter filter state for stick 1 (slider = bits 4–16)
	const [showLeftJitterDataModal, setShowLeftJitterDataModal] = useState(false);
	const [leftJitterFilter, setLeftJitterFilter] = useState<number>(JITTER_BITS_MAX);
	const [leftJitterFilterOriginal, setLeftJitterFilterOriginal] = useState<number>(JITTER_BITS_MAX);
	const [leftJitterSliderDirty, setLeftJitterSliderDirty] = useState(false);

	// Jitter filter state for stick 2
	const [showRightJitterDataModal, setShowRightJitterDataModal] = useState(false);
	const [rightJitterFilter, setRightJitterFilter] = useState<number>(JITTER_BITS_MAX);
	const [rightJitterFilterOriginal, setRightJitterFilterOriginal] = useState<number>(JITTER_BITS_MAX);
	const [rightJitterSliderDirty, setRightJitterSliderDirty] = useState(false);
	
	// Finetune shape modal state - initialize from values
	const [leftFinetuneShapeForceCircular, setLeftFinetuneShapeForceCircular] = useState(values?.joystickFinetuneShapeForceCircular1 ?? false);
	const [leftFinetuneShapeAmplify, setLeftFinetuneShapeAmplify] = useState(values?.joystickFinetuneShapeAmplify1 ?? 0.0);
	const [rightFinetuneShapeForceCircular, setRightFinetuneShapeForceCircular] = useState(values?.joystickFinetuneShapeForceCircular2 ?? false);
	const [rightFinetuneShapeAmplify, setRightFinetuneShapeAmplify] = useState(values?.joystickFinetuneShapeAmplify2 ?? 0.0);
	
	// Error rate display toggle state
	const [leftShowErrorRate, setLeftShowErrorRate] = useState(false);
	const [rightShowErrorRate, setRightShowErrorRate] = useState(false);
	
	// Update state when values change
	useEffect(() => {
			setLeftFinetuneShapeForceCircular(values?.joystickFinetuneShapeForceCircular1 ?? false);
			setLeftFinetuneShapeAmplify(values?.joystickFinetuneShapeAmplify1 ?? 0.0);
			setRightFinetuneShapeForceCircular(values?.joystickFinetuneShapeForceCircular2 ?? false);
			setRightFinetuneShapeAmplify(values?.joystickFinetuneShapeAmplify2 ?? 0.0);
	}, [values?.joystickFinetuneShapeForceCircular1, values?.joystickFinetuneShapeAmplify1, values?.joystickFinetuneShapeForceCircular2, values?.joystickFinetuneShapeAmplify2]);
	
	// Load jitter filter (threshold → bits) when modal opens
	useEffect(() => {
		if (showLeftJitterDataModal) {
			setLeftJitterSliderDirty(false);
			const savedBits = storedThresholdToBits(values?.joystickJitterFilter1 ?? 0);
			setLeftJitterFilter(savedBits);
			setLeftJitterFilterOriginal(savedBits);
		}
	}, [showLeftJitterDataModal, values?.joystickJitterFilter1]);
	
	useEffect(() => {
		if (showRightJitterDataModal) {
			setRightJitterSliderDirty(false);
			const savedBits = storedThresholdToBits(values?.joystickJitterFilter2 ?? 0);
			setRightJitterFilter(savedBits);
			setRightJitterFilterOriginal(savedBits);
		}
	}, [showRightJitterDataModal, values?.joystickJitterFilter2]);

	// Circularity data for main canvas (used when finetune shape is active)
	const [leftFinetuneShapeCircularityData, setLeftFinetuneShapeCircularityData] = useState<number[]>(new Array(CIRCULARITY_DATA_SIZE).fill(0));
	const [rightFinetuneShapeCircularityData, setRightFinetuneShapeCircularityData] = useState<number[]>(new Array(CIRCULARITY_DATA_SIZE).fill(0));
	
	// Detailed data for display
	const [leftStickDetailData, setLeftStickDetailData] = useState({
		centerX: 0,
		centerY: 0,
		angleIndex: 0,
		scale: 0,
		currentDistance: 0,
	});
	const [rightStickDetailData, setRightStickDetailData] = useState({
		centerX: 0,
		centerY: 0,
		angleIndex: 0,
		scale: 0,
		currentDistance: 0,
	});

	// Fetch joystick data periodically from unified joystick endpoints.
	useEffect(() => {
		if (!values) {
			return;
		}

		const fetchJoystickData = async () => {
			try {
				// Fetch left stick (stick 1) from unified API.
				const res1 = await fetch('/api/getJoystickRaw');
				if (res1.ok) {
					const data1 = await res1.json();
					if (data1.success) {
							const adcMax1 = Number(data1?.adcMax) > 0 ? Number(data1.adcMax) : DEFAULT_ADC_MAX;
							const adcCenter1 = adcMax1 / 2.0;
							const centerX = values.joystickCenterX || adcCenter1;
							const centerY = values.joystickCenterY || adcCenter1;
							const originalRangeData = values?.joystickRangeData1 || [];
							
							// Use current state values for real-time updates
							const forceCircular = leftFinetuneShapeForceCircular;
							const amplify = leftFinetuneShapeAmplify;
							
							const adjustedRangeData = applyFinetuneShapeAdjustments(
								originalRangeData,
								forceCircular,
								amplify
							);
							
							// ADC quantize for visualization (same step as firmware)
							const adcStep1 = values?.joystickJitterFilter1 ?? 0;
							const filtered1 = quantizeAdcPair(data1.x, data1.y, adcStep1, adcMax1);

							const { stickX: rawStickX, stickY: rawStickY, detailData } = processJoystickData(
								filtered1.x,
								filtered1.y,
								centerX,
								centerY,
								adjustedRangeData,
								adcMax1
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
							
							if (dist_sq < deadzone_sq) {
								// Inside deadzone: set to center (matches backend)
								stickX = 0.0;
								stickY = 0.0;
							} else if (antiDeadzone > 0.0) {
								// Anti-deadzone enabled: compute sqrt and apply if needed
								const dist = Math.sqrt(dist_sq);
								const baseline = antiDeadzone;
								const fixedAntiDeadzone = values?.fixed_anti_deadzone || false;
								
								if (fixedAntiDeadzone) {
									// Fixed anti-deadzone mode: scale distance to baseline (fixed output)
									// Only applies when dist < baseline to provide a fixed minimum output
									// When dist >= baseline, no anti-deadzone is applied (normal output)
									if (dist > 0.0 && dist < baseline) {
										const scale_factor = baseline / dist;
										stickX = stickX * scale_factor;
										stickY = stickY * scale_factor;
									}
								} else {
									// Linear anti-deadzone mode: add baseline to distance across the entire range
									// This maintains linear feel by adding a constant offset to all movements
									// Unlike fixed mode, this applies regardless of distance magnitude
									if (dist > 0.0) {
										const new_dist = dist + baseline;
										const scale_factor = new_dist / dist;
										stickX = stickX * scale_factor;
										stickY = stickY * scale_factor;
									}
								}
							}
							
							// Step 5: Square trimming (matches backend Step 5)
							stickX = Math.max(-1.0, Math.min(1.0, stickX));
							stickY = Math.max(-1.0, Math.min(1.0, stickY));
							
							// Step 6: Apply response curve if configured (matches backend Step 6)
							// Backend checks: curve_points_sorted_count > 0 (which is set only when curveEnabled && points_count > 0)
							// Backend directly calculates magnitude from normalizedX and normalizedY (after square trimming)
							const curveEnabled = values?.joystickCurveEnabled ?? false;
							const leftCurvePoints: CurvePoint[] = Array.isArray(values?.joystickCurvePoints1) ? values.joystickCurvePoints1 as CurvePoint[] : [];
							if (curveEnabled && leftCurvePoints.length > 0) {
								// Backend logic: directly calculate magnitude from coordinates (matches applyResponseCurveToCoordinates)
								if (stickX === 0.0 && stickY === 0.0) {
									// At center point: no scaling needed (matches backend early return)
								} else {
									const magnitude_sq = stickX * stickX + stickY * stickY;
									if (magnitude_sq > 0.0) {
										const magnitude = Math.sqrt(magnitude_sq);
										
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
							}
							
							// Calculate progress ratio for curve visualization
							// rawDist = currentDistance - distance from center in ADC units (before range calibration scaling)
							// This is the raw distance before applying range calibration
							const rawDist = detailData.currentDistance;
							// l = scale * adcCenter - outer calibration ADC value length for this direction
							// rangeData stores scale = distance / adcCenter for each angle
							// So when stick reaches outer boundary: distance = scale * adcCenter
							const l = detailData.scale > 0 ? detailData.scale * adcCenter1 : adcCenter1;
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
							
							// Collect circularity data only when error rate is enabled
							if (leftShowErrorRate) {
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

				// Fetch right stick (stick 2) from unified API.
				const res2 = await fetch('/api/getJoystickRaw2');
				if (res2.ok) {
					const data2 = await res2.json();
					if (data2.success) {
							const adcMax2 = Number(data2?.adcMax) > 0 ? Number(data2.adcMax) : DEFAULT_ADC_MAX;
							const adcCenter2 = adcMax2 / 2.0;
							const centerX = values.joystickCenterX2 || adcCenter2;
							const centerY = values.joystickCenterY2 || adcCenter2;
							const originalRangeData = values?.joystickRangeData2 || [];
							
							// Use current state values for real-time updates
							const forceCircular = rightFinetuneShapeForceCircular;
							const amplify = rightFinetuneShapeAmplify;
							
							const adjustedRangeData = applyFinetuneShapeAdjustments(
								originalRangeData,
								forceCircular,
								amplify
							);
							
							const adcStep2 = values?.joystickJitterFilter2 ?? 0;
							const filtered2 = quantizeAdcPair(data2.x, data2.y, adcStep2, adcMax2);

							const { stickX: rawStickX, stickY: rawStickY, detailData } = processJoystickData(
								filtered2.x,
								filtered2.y,
								centerX,
								centerY,
								adjustedRangeData,
								adcMax2
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
							
							if (dist_sq < deadzone_sq) {
								// Inside deadzone: set to center (matches backend)
								stickX = 0.0;
								stickY = 0.0;
							} else if (antiDeadzone > 0.0) {
								// Anti-deadzone enabled: compute sqrt and apply if needed
								const dist = Math.sqrt(dist_sq);
								const baseline = antiDeadzone;
								const fixedAntiDeadzone = values?.fixed_anti_deadzone2 || false;
								
								if (fixedAntiDeadzone) {
									// Fixed anti-deadzone mode: scale distance to baseline (fixed output)
									// Only applies when dist < baseline to provide a fixed minimum output
									// When dist >= baseline, no anti-deadzone is applied (normal output)
									if (dist > 0.0 && dist < baseline) {
										const scale_factor = baseline / dist;
										stickX = stickX * scale_factor;
										stickY = stickY * scale_factor;
									}
								} else {
									// Linear anti-deadzone mode: add baseline to distance across the entire range
									// This maintains linear feel by adding a constant offset to all movements
									// Unlike fixed mode, this applies regardless of distance magnitude
									if (dist > 0.0) {
										const new_dist = dist + baseline;
										const scale_factor = new_dist / dist;
										stickX = stickX * scale_factor;
										stickY = stickY * scale_factor;
									}
								}
							}
							
							// Step 5: Square trimming (matches backend Step 5)
							stickX = Math.max(-1.0, Math.min(1.0, stickX));
							stickY = Math.max(-1.0, Math.min(1.0, stickY));
							
							// Step 6: Apply response curve if configured (matches backend Step 6)
							// Backend checks: curve_points_sorted_count > 0 (which is set only when curveEnabled && points_count > 0)
							// Backend directly calculates magnitude from normalizedX and normalizedY (after square trimming)
							const curveEnabled = values?.joystickCurveEnabled ?? false;
							const rightCurvePoints: CurvePoint[] = Array.isArray(values?.joystickCurvePoints2) ? values.joystickCurvePoints2 as CurvePoint[] : [];
							if (curveEnabled && rightCurvePoints.length > 0) {
								// Backend logic: directly calculate magnitude from coordinates (matches applyResponseCurveToCoordinates)
								if (stickX === 0.0 && stickY === 0.0) {
									// At center point: no scaling needed (matches backend early return)
								} else {
									const magnitude_sq = stickX * stickX + stickY * stickY;
									if (magnitude_sq > 0.0) {
										const magnitude = Math.sqrt(magnitude_sq);
										
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
							}
							
							// Calculate progress ratio for curve visualization
							// rawDist = currentDistance - distance from center in ADC units (before range calibration scaling)
							// This is the raw distance before applying range calibration
							const rawDist = detailData.currentDistance;
							// l = scale * adcCenter - outer calibration ADC value length for this direction
							const l = detailData.scale > 0 ? detailData.scale * adcCenter2 : adcCenter2;
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
							
							// Collect circularity data only when error rate is enabled
							if (rightShowErrorRate) {
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
			} catch (error) {
				console.error('Failed to fetch joystick data:', error);
			}
		};

		// Update at ~30fps (every 33ms)
		const intervalId = setInterval(fetchJoystickData, 33);

		return () => {
			clearInterval(intervalId);
		};
	}, [
		values?.analogAdc1PinX, 
		values.analogAdc1PinY, 
		values.analogAdc2PinX, 
		values.analogAdc2PinY, 
		values.joystickCenterX, 
		values.joystickCenterY, 
		values.joystickCenterX2, 
		values.joystickCenterY2, 
		values.joystickRangeData1, 
		values.joystickRangeData2, 
		leftFinetuneShapeForceCircular, 
		leftFinetuneShapeAmplify, 
		rightFinetuneShapeForceCircular, 
		rightFinetuneShapeAmplify, 
		values?.joystickCurvePoints1, 
		values?.joystickCurvePoints2, 
		values?.joystickCurveEnabled,
		values?.inner_deadzone, 
		values?.anti_deadzone, 
		values?.inner_deadzone2, 
		values?.anti_deadzone2,
		values?.fixed_anti_deadzone,
		values?.fixed_anti_deadzone2,
		values?.analogAdc1Invert,
		values?.analogAdc2Invert,
		values?.joystickJitterFilter1,
		values?.joystickJitterFilter2,
		leftShowErrorRate,
		rightShowErrorRate
	]);

	// Cache canvas dimensions and center/radius calculations (performance optimization)
	const leftCanvasMetricsRef = useRef<{ centerX: number; centerY: number; radius: number } | null>(null);
	const rightCanvasMetricsRef = useRef<{ centerX: number; centerY: number; radius: number } | null>(null);

	// Update canvas when stick data changes (optimized with requestAnimationFrame throttling)
	useEffect(() => {
		let animationFrameId: number | null = null;
		let lastUpdateTime = 0;
		const TARGET_FPS = 60; // Target 60 FPS
		const FRAME_INTERVAL = 1000 / TARGET_FPS;

		const updateCanvas = (currentTime: number) => {
			// Throttle updates to target FPS
			if (currentTime - lastUpdateTime < FRAME_INTERVAL) {
				animationFrameId = requestAnimationFrame(updateCanvas);
				return;
			}
			lastUpdateTime = currentTime;

			// Draw left stick
			if (leftStickCanvasRef.current) {
				const ctx = leftStickCanvasRef.current.getContext('2d');
				if (ctx) {
					const canvas = leftStickCanvasRef.current;
					
					// Cache dimensions calculation
					if (!leftCanvasMetricsRef.current) {
						const centerX = canvas.width / 2;
						const centerY = canvas.height / 2;
						const radius = Math.min(centerX, centerY) - 10;
						leftCanvasMetricsRef.current = { centerX, centerY, radius };
					}
					
					const { centerX, centerY, radius } = leftCanvasMetricsRef.current;
					
					drawStickPosition(
						ctx,
						centerX,
						centerY,
						radius,
						leftStickData.x,
						leftStickData.y,
						leftFinetuneShapeCircularityData,
						leftFinetuneCenterActive,
						leftShowErrorRate,
					);
				}
			}

			// Draw right stick
			if (rightStickCanvasRef.current) {
				const ctx = rightStickCanvasRef.current.getContext('2d');
				if (ctx) {
					const canvas = rightStickCanvasRef.current;
					
					// Cache dimensions calculation
					if (!rightCanvasMetricsRef.current) {
						const centerX = canvas.width / 2;
						const centerY = canvas.height / 2;
						const radius = Math.min(centerX, centerY) - 10;
						rightCanvasMetricsRef.current = { centerX, centerY, radius };
					}
					
					const { centerX, centerY, radius } = rightCanvasMetricsRef.current;
					
					drawStickPosition(
						ctx,
						centerX,
						centerY,
						radius,
						rightStickData.x,
						rightStickData.y,
						rightFinetuneShapeCircularityData,
						rightFinetuneCenterActive,
						rightShowErrorRate,
					);
				}
			}

			animationFrameId = requestAnimationFrame(updateCanvas);
		};

		// Start animation loop
		animationFrameId = requestAnimationFrame(updateCanvas);

		return () => {
			if (animationFrameId !== null) {
				cancelAnimationFrame(animationFrameId);
			}
		};
	}, [leftStickData, rightStickData, leftFinetuneCenterActive, rightFinetuneCenterActive, leftFinetuneShapeCircularityData, rightFinetuneShapeCircularityData, leftShowErrorRate, rightShowErrorRate]);

	// Clear canvas metrics cache when canvas size changes
	useEffect(() => {
		leftCanvasMetricsRef.current = null;
		rightCanvasMetricsRef.current = null;
		// Clear static canvas cache when dimensions might change
		staticCanvasCache.clear();
	}, []);




	// Cleanup timeouts when sampling stops

	// Reset finetune shape data when values change
	useEffect(() => {
			setLeftFinetuneShapeCircularityData(new Array(CIRCULARITY_DATA_SIZE).fill(0));
	}, [leftFinetuneShapeForceCircular, leftFinetuneShapeAmplify]);

	useEffect(() => {
		setRightFinetuneShapeCircularityData(new Array(CIRCULARITY_DATA_SIZE).fill(0));
	}, [rightFinetuneShapeForceCircular, rightFinetuneShapeAmplify]);


	return (
		<Section title={t('AddonsConfig:joystick-calibration-header-text')}>
			<div id="JoystickCalibrationOptions" hidden={!values} style={{ overflowX: 'auto' }}>
				{/* 4 columns x 2 rows grid layout */}
				<div className="mb-3" style={{ display: 'grid', gridTemplateColumns: `repeat(4, ${COLUMN_WIDTH})`, gridTemplateRows: '270px auto auto', gap: '16px', justifyContent: 'center', alignItems: 'start', width: 'max-content', margin: '0 auto' }}>
					{/* Row 1, Column 1: Left stick canvas (position or curve) */}
					<div className="text-center" style={canvasContainerStyle}>
						<div style={canvasWrapperStyle}>
							<canvas
								ref={leftStickCanvasRef}
								width={CANVAS_SIZE}
								height={CANVAS_SIZE}
								style={canvasStyle}
							/>
						</div>
					</div>

					{/* Row 1, Column 2: Left finetune shape controls */}
					<FinetuneShapeControls
						title={t('CalibrationSettings:hml-outer-tune-left-title')}
						forceCircular={leftFinetuneShapeForceCircular}
						amplify={leftFinetuneShapeAmplify}
						onForceCircularChange={(value) => {
							setLeftFinetuneShapeForceCircular(value);
							setFieldValue('joystickFinetuneShapeForceCircular1', value);
						}}
						onAmplifyChange={(value) => {
							setLeftFinetuneShapeAmplify(value);
							setFieldValue('joystickFinetuneShapeAmplify1', value);
						}}
					/>

					{/* Row 1, Column 3: Right finetune shape controls */}
					<FinetuneShapeControls
						title={t('CalibrationSettings:hml-outer-tune-right-title')}
						forceCircular={rightFinetuneShapeForceCircular}
						amplify={rightFinetuneShapeAmplify}
						onForceCircularChange={(value) => {
							setRightFinetuneShapeForceCircular(value);
							setFieldValue('joystickFinetuneShapeForceCircular2', value);
						}}
						onAmplifyChange={(value) => {
							setRightFinetuneShapeAmplify(value);
							setFieldValue('joystickFinetuneShapeAmplify2', value);
						}}
					/>

					{/* Row 1, Column 4: Right stick canvas (position or curve) */}
					<div className="text-center" style={canvasContainerStyle}>
						<div style={canvasWrapperStyle}>
							<canvas
								ref={rightStickCanvasRef}
								width={CANVAS_SIZE}
								height={CANVAS_SIZE}
								style={canvasStyle}
							/>
						</div>
					</div>

					{/* Row 2, Column 1: Left stick XY position info */}
					<div style={{ ...positionInfoContainerStyle, gridColumn: '1' }}>
						<StickPositionInfo
							stickData={leftStickData}
							finetuneCenterActive={leftFinetuneCenterActive}
							centerX={values?.joystickCenterX || DEFAULT_ADC_CENTER}
							centerY={values?.joystickCenterY || DEFAULT_ADC_CENTER}
							onCenterXChange={(value) => setFieldValue('joystickCenterX', value)}
							onCenterYChange={(value) => setFieldValue('joystickCenterY', value)}
							convertToXInputNormalized={convertToXInputNormalized}
						/>
					</div>


					{/* Row 2, Column 4: Right stick XY position info */}
					<div style={{ ...positionInfoContainerStyle, gridColumn: '4' }}>
						<StickPositionInfo
							stickData={rightStickData}
							finetuneCenterActive={rightFinetuneCenterActive}
							centerX={values?.joystickCenterX2 || DEFAULT_ADC_CENTER}
							centerY={values?.joystickCenterY2 || DEFAULT_ADC_CENTER}
							onCenterXChange={(value) => setFieldValue('joystickCenterX2', value)}
							onCenterYChange={(value) => setFieldValue('joystickCenterY2', value)}
							convertToXInputNormalized={convertToXInputNormalized}
						/>
					</div>

					{/* Row 3, Column 1: Left stick buttons */}
					<div style={{ ...buttonsContainerStyle, gridColumn: '1' }}>
						<StickButtons
							onCenterCalibration={() => setShowLeftCalibrationModal(true)}
							onRangeCalibration={() => setShowLeftRangeModal(true)}
							onFinetuneCenter={() => setLeftFinetuneCenterActive(!leftFinetuneCenterActive)}
							finetuneCenterActive={leftFinetuneCenterActive}
							onJitterSampling={() => setShowLeftJitterDataModal(true)}
						/>
					</div>

					{/* Row 3, Column 2: Left stick view calibration data button and error rate switch */}
					<div style={{ ...buttonsContainerStyle, gridColumn: '2' }}>
						<ViewCalibrationData
							errorRateEnabled={leftShowErrorRate}
							onErrorRateChange={(value) => setLeftShowErrorRate(value)}
							onViewData={() => {
								const rangeData = values?.joystickRangeData1;
								setLeftRangeDataSnapshot(Array.isArray(rangeData) ? rangeData : []);
								setLeftAngleIndexSnapshot(leftStickDetailData.angleIndex);
								setShowLeftRangeDataModal(true);
							}}
							circularityDataSize={CIRCULARITY_DATA_SIZE}
							onClearCircularityData={() => setLeftFinetuneShapeCircularityData(new Array(CIRCULARITY_DATA_SIZE).fill(0))}
						/>
					</div>


					{/* Row 3, Column 3: Right stick view calibration data button and error rate switch */}
					<div style={{ ...buttonsContainerStyle, gridColumn: '3' }}>
						<ViewCalibrationData
							errorRateEnabled={rightShowErrorRate}
							onErrorRateChange={(value) => setRightShowErrorRate(value)}
							onViewData={() => {
								const rangeData = values?.joystickRangeData2;
								setRightRangeDataSnapshot(Array.isArray(rangeData) ? rangeData : []);
								setRightAngleIndexSnapshot(rightStickDetailData.angleIndex);
								setShowRightRangeDataModal(true);
							}}
							circularityDataSize={CIRCULARITY_DATA_SIZE}
							onClearCircularityData={() => setRightFinetuneShapeCircularityData(new Array(CIRCULARITY_DATA_SIZE).fill(0))}
						/>
					</div>

					{/* Row 3, Column 4: Right stick buttons */}
					<div style={{ ...buttonsContainerStyle, gridColumn: '4' }}>
						<StickButtons
							onCenterCalibration={() => setShowRightCalibrationModal(true)}
							onRangeCalibration={() => setShowRightRangeModal(true)}
							onFinetuneCenter={() => setRightFinetuneCenterActive(!rightFinetuneCenterActive)}
							finetuneCenterActive={rightFinetuneCenterActive}
							onJitterSampling={() => setShowRightJitterDataModal(true)}
						/>
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
					<Modal.Title>{t('CalibrationSettings:hml-modal-outer-ring-left-title')}</Modal.Title>
				</Modal.Header>
				<Modal.Body>
					<div className="mb-3">
						<strong>{t('CalibrationSettings:hml-stick-center-data')}</strong> ({leftStickDetailData.centerX.toFixed(1)}, {leftStickDetailData.centerY.toFixed(1)})
					</div>
					<div className="mb-2 small text-muted">
						{t('CalibrationSettings:hml-data-count', { n: leftRangeDataSnapshot.length, max: CIRCULARITY_DATA_SIZE })}
					</div>
					<Table striped bordered hover size="sm">
						<thead>
							<tr>
								<th>{t('CalibrationSettings:hml-table-index')}</th>
								<th>{t('CalibrationSettings:hml-table-angle-range')}</th>
								<th>{t('CalibrationSettings:hml-table-scale')}</th>
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
										<td>{t('CalibrationSettings:hml-angle-range-value', { start: angleStart, end: angleEnd })}</td>
										<td>{scale !== undefined && scale !== null && scale > 0 ? scale.toFixed(4) : 'N/A'}</td>
									</tr>
								);
							})}
						</tbody>
					</Table>
				</Modal.Body>
				<Modal.Footer>
					<Button variant="secondary" onClick={() => setShowLeftRangeDataModal(false)}>
						{t('CalibrationSettings:hml-button-close')}
					</Button>
				</Modal.Footer>
			</Modal>

			<Modal show={showRightRangeDataModal} onHide={() => setShowRightRangeDataModal(false)} size="lg">
				<Modal.Header closeButton>
					<Modal.Title>{t('CalibrationSettings:hml-modal-outer-ring-right-title')}</Modal.Title>
				</Modal.Header>
				<Modal.Body>
					<div className="mb-3">
						<strong>{t('CalibrationSettings:hml-stick-center-data')}</strong> ({rightStickDetailData.centerX.toFixed(1)}, {rightStickDetailData.centerY.toFixed(1)})
					</div>
					<div className="mb-2 small text-muted">
						{t('CalibrationSettings:hml-data-count', { n: rightRangeDataSnapshot.length, max: CIRCULARITY_DATA_SIZE })}
					</div>
					<Table striped bordered hover size="sm">
						<thead>
							<tr>
								<th>{t('CalibrationSettings:hml-table-index')}</th>
								<th>{t('CalibrationSettings:hml-table-angle-range')}</th>
								<th>{t('CalibrationSettings:hml-table-scale')}</th>
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
										<td>{t('CalibrationSettings:hml-angle-range-value', { start: angleStart, end: angleEnd })}</td>
										<td>{scale !== undefined && scale !== null && scale > 0 ? scale.toFixed(4) : 'N/A'}</td>
									</tr>
								);
							})}
						</tbody>
					</Table>
				</Modal.Body>
				<Modal.Footer>
					<Button variant="secondary" onClick={() => setShowRightRangeDataModal(false)}>
						{t('CalibrationSettings:hml-button-close')}
					</Button>
				</Modal.Footer>
			</Modal>

			{/* Left Jitter Data Modal */}
			<Modal
				show={showLeftJitterDataModal}
				onHide={() => {
					setLeftJitterFilter(leftJitterFilterOriginal);
					setShowLeftJitterDataModal(false);
				}}
				size="lg"
			>
				<Modal.Header closeButton>
					<Modal.Title>{t('CalibrationSettings:hml-modal-stick-step-left-title')}</Modal.Title>
				</Modal.Header>
				<Modal.Body>
					{/* Jitter Filter Slider */}
					<div className="mb-4">
						<Form.Label>
							{t('CalibrationSettings:hml-stick-resolution-bit', { bits: leftJitterFilter, step: Math.round(Math.pow(2, 16 - leftJitterFilter)) })}
						</Form.Label>
						<Form.Range
							min={JITTER_BITS_MIN}
							max={JITTER_BITS_MAX}
							step={1}
							value={leftJitterFilter}
							onChange={(e) => {
								setLeftJitterSliderDirty(true);
								setLeftJitterFilter(parseInt(e.target.value, 10));
							}}
						/>
						<div className="mt-3 small text-muted">
							{t('CalibrationSettings:hml-stick-step-help')}
						</div>
					</div>
					<div className="mb-4">
						<Form.Check
							type="switch"
							id="ads8332IirFilterEnabled-left"
							label={t('CalibrationSettings:hml-ads8332-iir-enable')}
							checked={Boolean(values?.ads8332IirFilterEnabled)}
							onChange={(e) => setFieldValue('ads8332IirFilterEnabled', e.target.checked ? 1 : 0)}
						/>
						<Form.Label className="mt-3">
							{t('CalibrationSettings:hml-ads8332-iir-strength', {
								value: Number(values?.ads8332IirStrength ?? 0.5).toFixed(3),
							})}
						</Form.Label>
						<Form.Select
							size="sm"
							value={String(values?.ads8332IirStrength ?? 0.5)}
							onChange={(e) => setFieldValue('ads8332IirStrength', parseFloat(e.target.value))}
							disabled={!Boolean(values?.ads8332IirFilterEnabled)}
						>
							{ADS8332_IIR_STRENGTH_OPTIONS.map((v) => (
								<option key={`ads8332-iir-left-${v}`} value={v}>
									{v}
								</option>
							))}
						</Form.Select>
						<div className="mt-2 small text-muted">
							{t('CalibrationSettings:hml-ads8332-iir-hint')}
						</div>
					</div>
				</Modal.Body>
				<Modal.Footer>
					<Button variant="secondary" onClick={() => {
						// Cancel: restore original value
						setLeftJitterFilter(leftJitterFilterOriginal);
						setShowLeftJitterDataModal(false);
					}}>
						{t('CalibrationSettings:hml-button-cancel')}
					</Button>
					<Button variant="primary" onClick={() => {
						// Save: store ADC threshold; keep legacy 0 if user never moved slider from "off"
						const next =
							(values?.joystickJitterFilter1 ?? 0) === 0 && !leftJitterSliderDirty
								? 0
								: bitsToStoredThreshold(leftJitterFilter);
						setFieldValue('joystickJitterFilter1', next);
						setLeftJitterFilterOriginal(leftJitterFilter);
						setShowLeftJitterDataModal(false);
					}}>
						{t('CalibrationSettings:hml-button-ok')}
					</Button>
				</Modal.Footer>
			</Modal>

			{/* Right Jitter Data Modal */}
			<Modal
				show={showRightJitterDataModal}
				onHide={() => {
					setRightJitterFilter(rightJitterFilterOriginal);
					setShowRightJitterDataModal(false);
				}}
				size="lg"
			>
				<Modal.Header closeButton>
					<Modal.Title>{t('CalibrationSettings:hml-modal-stick-step-right-title')}</Modal.Title>
				</Modal.Header>
				<Modal.Body>
					{/* Jitter Filter Slider */}
					<div className="mb-4">
						<Form.Label>
							{t('CalibrationSettings:hml-stick-resolution-bit', { bits: rightJitterFilter, step: Math.round(Math.pow(2, 16 - rightJitterFilter)) })}
						</Form.Label>
						<Form.Range
							min={JITTER_BITS_MIN}
							max={JITTER_BITS_MAX}
							step={1}
							value={rightJitterFilter}
							onChange={(e) => {
								setRightJitterSliderDirty(true);
								setRightJitterFilter(parseInt(e.target.value, 10));
							}}
						/>
						<div className="mt-3 small text-muted">
							{t('CalibrationSettings:hml-stick-step-help')}
						</div>
					</div>
					<div className="mb-4">
						<Form.Check
							type="switch"
							id="ads8332IirFilterEnabled-right"
							label={t('CalibrationSettings:hml-ads8332-iir-enable')}
							checked={Boolean(values?.ads8332IirFilterEnabled)}
							onChange={(e) => setFieldValue('ads8332IirFilterEnabled', e.target.checked ? 1 : 0)}
						/>
						<Form.Label className="mt-3">
							{t('CalibrationSettings:hml-ads8332-iir-strength', {
								value: Number(values?.ads8332IirStrength ?? 0.5).toFixed(3),
							})}
						</Form.Label>
						<Form.Select
							size="sm"
							value={String(values?.ads8332IirStrength ?? 0.5)}
							onChange={(e) => setFieldValue('ads8332IirStrength', parseFloat(e.target.value))}
							disabled={!Boolean(values?.ads8332IirFilterEnabled)}
						>
							{ADS8332_IIR_STRENGTH_OPTIONS.map((v) => (
								<option key={`ads8332-iir-right-${v}`} value={v}>
									{v}
								</option>
							))}
						</Form.Select>
						<div className="mt-2 small text-muted">
							{t('CalibrationSettings:hml-ads8332-iir-hint')}
						</div>
					</div>
				</Modal.Body>
				<Modal.Footer>
					<Button variant="secondary" onClick={() => {
						// Cancel: restore original value
						setRightJitterFilter(rightJitterFilterOriginal);
						setShowRightJitterDataModal(false);
					}}>
						{t('CalibrationSettings:hml-button-cancel')}
					</Button>
					<Button variant="primary" onClick={() => {
						const next =
							(values?.joystickJitterFilter2 ?? 0) === 0 && !rightJitterSliderDirty
								? 0
								: bitsToStoredThreshold(rightJitterFilter);
						setFieldValue('joystickJitterFilter2', next);
						setRightJitterFilterOriginal(rightJitterFilter);
						setShowRightJitterDataModal(false);
					}}>
						{t('CalibrationSettings:hml-button-ok')}
					</Button>
				</Modal.Footer>
			</Modal>
			
			{/* Save Button */}
			<div className="mt-3" style={{ display: 'flex', alignItems: 'center', gap: '12px' }}>
				<Button type="button" onClick={() => (onSaveClick ? onSaveClick() : handleSubmit())}>
					{t('Common:button-save-label')}
				</Button>
				{saveMessage && (
					<span className={saveMessage === t('Common:saved-success-message') ? 'text-success' : 'text-danger'}>
						{saveMessage}
					</span>
				)}
			</div>

			{/* Range calibration warning modal */}
			<Modal
				show={showRangeCalibrationWarning}
				onHide={() => setShowRangeCalibrationWarning(false)}
				centered
			>
				<Modal.Header closeButton>
					<Modal.Title>{t('CalibrationSettings:hml-modal-title-hint')}</Modal.Title>
				</Modal.Header>
				<Modal.Body>
					<p className="mb-0">{t('CalibrationSettings:hml-modal-calibrate-outer-first')}</p>
				</Modal.Body>
				<Modal.Footer>
					<Button variant="primary" onClick={() => setShowRangeCalibrationWarning(false)}>
						{t('CalibrationSettings:hml-button-ok')}
					</Button>
				</Modal.Footer>
			</Modal>
		</Section>
	);
};

export default JoystickCalibration;
