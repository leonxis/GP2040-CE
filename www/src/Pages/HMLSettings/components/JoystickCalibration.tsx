import { useEffect, useRef, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { useFormikContext } from 'formik';
import { Button, FormCheck, Modal, Table, Form } from 'react-bootstrap';

import Section from '../../../Components/Section';
import StickCalibrationModal from '../../../Components/StickCalibrationModal';
import RangeCalibrationModal from '../../../Components/RangeCalibrationModal';
import type { AddonPropTypes } from './CalibrationSettings';

// Type definitions
type CurvePoint = { x: number; y: number };

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
	
	// Update state when values change
	useEffect(() => {
			setLeftFinetuneShapeForceCircular(values?.joystickFinetuneShapeForceCircular1 ?? false);
			setLeftFinetuneShapeAmplify(values?.joystickFinetuneShapeAmplify1 ?? 0.0);
			setRightFinetuneShapeForceCircular(values?.joystickFinetuneShapeForceCircular2 ?? false);
			setRightFinetuneShapeAmplify(values?.joystickFinetuneShapeAmplify2 ?? 0.0);
	}, [values?.joystickFinetuneShapeForceCircular1, values?.joystickFinetuneShapeAmplify1, values?.joystickFinetuneShapeForceCircular2, values?.joystickFinetuneShapeAmplify2]);
	
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
							
							// Use current state values for real-time updates
							const forceCircular = leftFinetuneShapeForceCircular;
							const amplify = leftFinetuneShapeAmplify;
							
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
							const leftCurvePoints: CurvePoint[] = Array.isArray(values?.joystickCurvePoints1) ? values.joystickCurvePoints1 as CurvePoint[] : [];
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
							
							// Collect circularity data (always enabled)
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

				// Fetch right stick (stick 2)
				if (values.analogAdc2PinX != null && values.analogAdc2PinX >= 0 && values.analogAdc2PinY != null && values.analogAdc2PinY >= 0) {
					const res2 = await fetch('/api/getJoystickCenter2');
					if (res2.ok) {
						const data2 = await res2.json();
						if (data2.success) {
							const centerX = values.joystickCenterX2 || ADC_CENTER;
							const centerY = values.joystickCenterY2 || ADC_CENTER;
							const originalRangeData = values?.joystickRangeData2 || [];
							
							// Use current state values for real-time updates
							const forceCircular = rightFinetuneShapeForceCircular;
							const amplify = rightFinetuneShapeAmplify;
							
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
							const rightCurvePoints: CurvePoint[] = Array.isArray(values?.joystickCurvePoints2) ? values.joystickCurvePoints2 as CurvePoint[] : [];
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
							
							// Collect circularity data (always enabled)
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
	}, [values.AnalogInputEnabled, values.analogAdc1PinX, values.analogAdc1PinY, values.analogAdc2PinX, values.analogAdc2PinY, values.joystickCenterX, values.joystickCenterY, values.joystickCenterX2, values.joystickCenterY2, values.joystickRangeData1, values.joystickRangeData2, leftFinetuneShapeForceCircular, leftFinetuneShapeAmplify, rightFinetuneShapeForceCircular, rightFinetuneShapeAmplify, values?.joystickCurvePoints1, values?.joystickCurvePoints2, values?.inner_deadzone, values?.anti_deadzone, values?.inner_deadzone2, values?.anti_deadzone2]);

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
						leftFinetuneShapeCircularityData, // Always show circularity
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
						rightFinetuneShapeCircularityData, // Always show circularity
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
	}, [leftStickData, rightStickData, leftFinetuneCenterActive, rightFinetuneCenterActive, leftFinetuneShapeCircularityData, rightFinetuneShapeCircularityData]);



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

	// Reset finetune shape data when values change
	useEffect(() => {
			setLeftFinetuneShapeCircularityData(new Array(CIRCULARITY_DATA_SIZE).fill(0));
	}, [leftFinetuneShapeForceCircular, leftFinetuneShapeAmplify]);

	useEffect(() => {
		setRightFinetuneShapeCircularityData(new Array(CIRCULARITY_DATA_SIZE).fill(0));
	}, [rightFinetuneShapeForceCircular, rightFinetuneShapeAmplify]);


	return (
		<Section title={t('AddonsConfig:joystick-calibration-header-text')}>
			<div id="JoystickCalibrationOptions" hidden={!values || !values.AnalogInputEnabled || values.AnalogInputEnabled === 0} style={{ overflowX: 'auto' }}>
				{/* 4 columns x 2 rows grid layout */}
				<div className="mb-3" style={{ display: 'grid', gridTemplateColumns: 'repeat(4, 260px)', gridTemplateRows: '270px auto auto', gap: '16px', justifyContent: 'center', alignItems: 'start', width: 'max-content', margin: '0 auto' }}>
					{/* Row 1, Column 1: Left stick canvas (position or curve) */}
					<div className="text-center" style={{ display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'flex-start', width: '260px' }}>
						<div style={{ position: 'relative', width: '260px', height: '260px' }}>
						<canvas
							ref={leftStickCanvasRef}
							width={260}
							height={260}
								style={{ 
									border: '1px solid #ccc', 
									borderRadius: '4px',
									display: 'block'
											}}
										/>
						</div>
					</div>

					{/* Row 1, Column 2: Left finetune shape controls */}
					<div style={{ width: '260px', display: 'flex', justifyContent: 'center', alignItems: 'flex-start' }}>
						<div style={{ width: '260px', textAlign: 'left', border: '1px solid #dee2e6', borderRadius: '4px', padding: '8px' }}>
								<div style={{ fontWeight: 'bold', marginBottom: '8px', textAlign: 'center' }}>左摇杆外圈调教</div>
								<div className="p-2">
									<div className="mb-3">
										<FormCheck
											type="switch"
											id="leftFinetuneShapeForceCircular"
											label="强制圆形"
											checked={leftFinetuneShapeForceCircular}
										onChange={(e) => {
											setLeftFinetuneShapeForceCircular(e.target.checked);
											setFieldValue('joystickFinetuneShapeForceCircular1', e.target.checked);
										}}
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
										onChange={(e) => {
											const newValue = parseFloat(e.target.value);
											setLeftFinetuneShapeAmplify(newValue);
											setFieldValue('joystickFinetuneShapeAmplify1', newValue);
										}}
										/>
										<p className="text-muted small mt-1 mb-0">
											扩大系数可以放大摇杆覆盖范围，加快移动响应速度。
										</p>
									</div>
									</div>
								</div>
					</div>

					{/* Row 1, Column 3: Right finetune shape controls */}
					<div style={{ width: '260px', display: 'flex', justifyContent: 'center', alignItems: 'flex-start' }}>
						<div style={{ width: '260px', textAlign: 'left', border: '1px solid #dee2e6', borderRadius: '4px', padding: '8px' }}>
								<div style={{ fontWeight: 'bold', marginBottom: '8px', textAlign: 'center' }}>右摇杆外圈调教</div>
								<div className="p-2">
									<div className="mb-3">
										<FormCheck
											type="switch"
											id="rightFinetuneShapeForceCircular"
											label="强制圆形"
											checked={rightFinetuneShapeForceCircular}
										onChange={(e) => {
											setRightFinetuneShapeForceCircular(e.target.checked);
											setFieldValue('joystickFinetuneShapeForceCircular2', e.target.checked);
										}}
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
										onChange={(e) => {
											const newValue = parseFloat(e.target.value);
											setRightFinetuneShapeAmplify(newValue);
											setFieldValue('joystickFinetuneShapeAmplify2', newValue);
										}}
										/>
										<p className="text-muted small mt-1 mb-0">
											扩大系数可以放大摇杆覆盖范围，加快移动响应速度。
										</p>
									</div>
									</div>
								</div>
					</div>

					{/* Row 1, Column 4: Right stick canvas (position or curve) */}
					<div className="text-center" style={{ display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'flex-start', width: '260px' }}>
						<div style={{ position: 'relative', width: '260px', height: '260px' }}>
						<canvas
							ref={rightStickCanvasRef}
							width={260}
							height={260}
								style={{ 
									border: '1px solid #ccc', 
									borderRadius: '4px',
									display: 'block'
											}}
										/>
									</div>
									</div>

					{/* Row 2, Column 1: Left stick XY position info */}
					<div style={{ display: 'flex', justifyContent: 'center', alignItems: 'center', width: '260px', gridColumn: '1' }}>
						<div className="small" style={{ display: 'block', width: '100%' }}>
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


					{/* Row 2, Column 4: Right stick XY position info */}
					<div style={{ display: 'flex', justifyContent: 'center', alignItems: 'center', width: '260px', gridColumn: '4' }}>
						<div className="small" style={{ display: 'block', width: '100%' }}>
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
					<div style={{ display: 'flex', flexDirection: 'column', alignItems: 'center', gap: '8px', width: '260px', gridColumn: '1' }}>
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
						</div>

					{/* Row 3, Column 2: Left stick view calibration data button */}
					<div style={{ display: 'flex', flexDirection: 'column', alignItems: 'center', gap: '8px', width: '260px', gridColumn: '2' }}>
						<div className="mt-3 d-flex gap-2 justify-content-center flex-wrap">
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


					{/* Row 3, Column 3: Right stick view calibration data button */}
					<div style={{ display: 'flex', flexDirection: 'column', alignItems: 'center', gap: '8px', width: '260px', gridColumn: '3' }}>
						<div className="mt-3 d-flex gap-2 justify-content-center flex-wrap">
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

					{/* Row 3, Column 4: Right stick buttons */}
					<div style={{ display: 'flex', flexDirection: 'column', alignItems: 'center', gap: '8px', width: '260px', gridColumn: '4' }}>
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
