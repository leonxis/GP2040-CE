import { useEffect, useRef, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { useFormikContext } from 'formik';
import { Row, Col, Button, FormCheck, Modal, Table, Form } from 'react-bootstrap';

import Section from '../Components/Section';
import StickCalibrationModal from '../Components/StickCalibrationModal';
import RangeCalibrationModal from '../Components/RangeCalibrationModal';
import { AddonPropTypes } from '../Pages/AddonsConfigPage';

const CIRCULARITY_DATA_SIZE = 48; // Number of angular positions to sample
const ADC_MAX = 4095;
const ADC_CENTER = ADC_MAX / 2;

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
 * Applies finetune shape percentage adjustments to range data with angle interpolation
 * @param rangeData - Original range calibration data array
 * @param xTopPercent - X axis top percentage (default 100.0)
 * @param xBottomPercent - X axis bottom percentage (default 100.0)
 * @param yLeftPercent - Y axis left percentage (default 100.0)
 * @param yRightPercent - Y axis right percentage (default 100.0)
 * @param forceCircular - Force circular flag
 * @param amplify - Amplify factor (default 0.0)
 * @returns Adjusted range data array
 */
/**
 * Apply finetune shape adjustments to range_data (matches backend logic)
 * @param rangeData Original calibration data array
 * @param xTopPercent Top percentage adjustment
 * @param xBottomPercent Bottom percentage adjustment
 * @param yLeftPercent Left percentage adjustment
 * @param yRightPercent Right percentage adjustment
 * @param forceCircular Whether force circular is enabled
 * @param amplify Amplify factor (when force circular is enabled)
 * @returns Adjusted range data array
 */
const applyFinetuneShapeAdjustments = (
	rangeData: number[],
	xTopPercent: number,
	xBottomPercent: number,
	yLeftPercent: number,
	yRightPercent: number,
	forceCircular: boolean,
	amplify: number
): number[] => {
	// Index mapping: angle = (index * 2π / 48) - π
	// 0° (right): index = 24
	// 90° (top): index = 36  
	// 180° (left): index = 0
	// 270° (bottom): index = 12
	
	// Create a copy of the range data to avoid mutating the original
	const adjustedData = [...rangeData];
	
	if (!forceCircular) {
		// Step 1: When force circular is disabled, set all scaling ratios to the minimum value
		// Find the minimum scaling ratio
		// Since has_range_calibration ensures all 48 indices have valid (non-zero) data,
		// we can use range_data[0] as the initial minScale and compare with remaining indices
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
		
		// Step 2: Apply percentage adjustments to cardinal indices (0, 12, 24, 36)
		for (let i = 0; i < adjustedData.length; i += 12) {
			let percentFactor = 0.0;
			if (i === 0) {
				percentFactor = yLeftPercent / 100.0;      // Left (180°)
			} else if (i === 12) {
				percentFactor = xBottomPercent / 100.0;    // Bottom (270°)
			} else if (i === 24) {
				percentFactor = yRightPercent / 100.0;    // Right (0°)
			} else if (i === 36) {
				percentFactor = xTopPercent / 100.0;      // Top (90°)
			}
			if (percentFactor > 0.0) {
				adjustedData[i] /= percentFactor;
			}
		}
	}
	// Note: When force_circular is true, scaling ratios remain unchanged at this point
	
	// Step 3: Apply amplify factor to all scaling ratios (regardless of force_circular setting)
	const amplifyFactor = 1.0 + amplify / 100.0;
	if (amplifyFactor > 0.0) {
		for (let i = 0; i < adjustedData.length; i++) {
			adjustedData[i] /= amplifyFactor;
		}
	}
	
	return adjustedData;
};

/**
 * Processes joystick data through coordinate transformation pipeline
 * @param rawX - Raw ADC X value
 * @param rawY - Raw ADC Y value
 * @param centerX - Calibrated center X value
 * @param centerY - Calibrated center Y value
 * @param rangeData - Range calibration data array
 * @returns Processed stick data and detail information
 */
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
	const [leftStickData, setLeftStickData] = useState({ x: 0, y: 0, rawX: 0, rawY: 0 });
	const [rightStickData, setRightStickData] = useState({ x: 0, y: 0, rawX: 0, rawY: 0 });
	const [showLeftCalibrationModal, setShowLeftCalibrationModal] = useState(false);
	const [showRightCalibrationModal, setShowRightCalibrationModal] = useState(false);
	const [showLeftRangeModal, setShowLeftRangeModal] = useState(false);
	const [showRightRangeModal, setShowRightRangeModal] = useState(false);
	const [showLeftFinetuneShapeModal, setShowLeftFinetuneShapeModal] = useState(false);
	const [showRightFinetuneShapeModal, setShowRightFinetuneShapeModal] = useState(false);
	const [showLeftRangeDataModal, setShowLeftRangeDataModal] = useState(false);
	const [showRightRangeDataModal, setShowRightRangeDataModal] = useState(false);
	const [leftRangeDataSnapshot, setLeftRangeDataSnapshot] = useState<number[]>([]);
	const [rightRangeDataSnapshot, setRightRangeDataSnapshot] = useState<number[]>([]);
	const [leftAngleIndexSnapshot, setLeftAngleIndexSnapshot] = useState(0);
	const [rightAngleIndexSnapshot, setRightAngleIndexSnapshot] = useState(0);
	const [leftFinetuneCenterActive, setLeftFinetuneCenterActive] = useState(false);
	const [rightFinetuneCenterActive, setRightFinetuneCenterActive] = useState(false);
	const [showRangeCalibrationWarning, setShowRangeCalibrationWarning] = useState(false);
	
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
	const leftJitterTimeoutRef = useRef<any>(null);
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
	const rightJitterTimeoutRef = useRef<any>(null);
	const [rightJitterFilter, setRightJitterFilter] = useState<number>(0);
	const [rightJitterFilterOriginal, setRightJitterFilterOriginal] = useState<number>(0);
	
	// Finetune shape modal state - initialize from values
	const getPercentValue = (val: any, defaultVal: number) => {
		if (val === undefined || val === null) return defaultVal;
		const numVal = typeof val === 'number' ? val : parseFloat(val);
		return isNaN(numVal) || numVal === 0 ? defaultVal : numVal;
	};
	
	const [leftFinetuneShapeXTopPercent, setLeftFinetuneShapeXTopPercent] = useState(getPercentValue((values as any)?.joystickFinetuneShapeXTopPercent1, 100.0));
	const [leftFinetuneShapeXBottomPercent, setLeftFinetuneShapeXBottomPercent] = useState(getPercentValue((values as any)?.joystickFinetuneShapeXBottomPercent1, 100.0));
	const [leftFinetuneShapeYLeftPercent, setLeftFinetuneShapeYLeftPercent] = useState(getPercentValue((values as any)?.joystickFinetuneShapeYLeftPercent1, 100.0));
	const [leftFinetuneShapeYRightPercent, setLeftFinetuneShapeYRightPercent] = useState(getPercentValue((values as any)?.joystickFinetuneShapeYRightPercent1, 100.0));
	const [leftFinetuneShapeForceCircular, setLeftFinetuneShapeForceCircular] = useState((values as any)?.joystickFinetuneShapeForceCircular1 ?? false);
	const [leftFinetuneShapeAmplify, setLeftFinetuneShapeAmplify] = useState((values as any)?.joystickFinetuneShapeAmplify1 ?? 0.0);
	const [rightFinetuneShapeXTopPercent, setRightFinetuneShapeXTopPercent] = useState(getPercentValue((values as any)?.joystickFinetuneShapeXTopPercent2, 100.0));
	const [rightFinetuneShapeXBottomPercent, setRightFinetuneShapeXBottomPercent] = useState(getPercentValue((values as any)?.joystickFinetuneShapeXBottomPercent2, 100.0));
	const [rightFinetuneShapeYLeftPercent, setRightFinetuneShapeYLeftPercent] = useState(getPercentValue((values as any)?.joystickFinetuneShapeYLeftPercent2, 100.0));
	const [rightFinetuneShapeYRightPercent, setRightFinetuneShapeYRightPercent] = useState(getPercentValue((values as any)?.joystickFinetuneShapeYRightPercent2, 100.0));
	const [rightFinetuneShapeForceCircular, setRightFinetuneShapeForceCircular] = useState((values as any)?.joystickFinetuneShapeForceCircular2 ?? false);
	const [rightFinetuneShapeAmplify, setRightFinetuneShapeAmplify] = useState((values as any)?.joystickFinetuneShapeAmplify2 ?? 0.0);
	
	// Update state when modal opens (load saved values from server)
	useEffect(() => {
		if (showLeftFinetuneShapeModal) {
			setLeftFinetuneShapeXTopPercent(getPercentValue((values as any)?.joystickFinetuneShapeXTopPercent1, 100.0));
			setLeftFinetuneShapeXBottomPercent(getPercentValue((values as any)?.joystickFinetuneShapeXBottomPercent1, 100.0));
			setLeftFinetuneShapeYLeftPercent(getPercentValue((values as any)?.joystickFinetuneShapeYLeftPercent1, 100.0));
			setLeftFinetuneShapeYRightPercent(getPercentValue((values as any)?.joystickFinetuneShapeYRightPercent1, 100.0));
			setLeftFinetuneShapeForceCircular((values as any)?.joystickFinetuneShapeForceCircular1 ?? false);
			setLeftFinetuneShapeAmplify((values as any)?.joystickFinetuneShapeAmplify1 ?? 0.0);
		}
		if (showRightFinetuneShapeModal) {
			setRightFinetuneShapeXTopPercent(getPercentValue((values as any)?.joystickFinetuneShapeXTopPercent2, 100.0));
			setRightFinetuneShapeXBottomPercent(getPercentValue((values as any)?.joystickFinetuneShapeXBottomPercent2, 100.0));
			setRightFinetuneShapeYLeftPercent(getPercentValue((values as any)?.joystickFinetuneShapeYLeftPercent2, 100.0));
			setRightFinetuneShapeYRightPercent(getPercentValue((values as any)?.joystickFinetuneShapeYRightPercent2, 100.0));
			setRightFinetuneShapeForceCircular((values as any)?.joystickFinetuneShapeForceCircular2 ?? false);
			setRightFinetuneShapeAmplify((values as any)?.joystickFinetuneShapeAmplify2 ?? 0.0);
		}
	}, [showLeftFinetuneShapeModal, showRightFinetuneShapeModal, (values as any)?.joystickFinetuneShapeXTopPercent1, (values as any)?.joystickFinetuneShapeXBottomPercent1, (values as any)?.joystickFinetuneShapeYLeftPercent1, (values as any)?.joystickFinetuneShapeYRightPercent1, (values as any)?.joystickFinetuneShapeForceCircular1, (values as any)?.joystickFinetuneShapeAmplify1, (values as any)?.joystickFinetuneShapeXTopPercent2, (values as any)?.joystickFinetuneShapeXBottomPercent2, (values as any)?.joystickFinetuneShapeYLeftPercent2, (values as any)?.joystickFinetuneShapeYRightPercent2, (values as any)?.joystickFinetuneShapeForceCircular2, (values as any)?.joystickFinetuneShapeAmplify2]);
	
	// Load jitter filter values when modal opens
	useEffect(() => {
		if (showLeftJitterDataModal) {
			const savedValue = (values as any)?.joystickJitterFilter1 ?? 0;
			setLeftJitterFilter(savedValue);
			setLeftJitterFilterOriginal(savedValue);
		}
	}, [showLeftJitterDataModal, values]);
	
	useEffect(() => {
		if (showRightJitterDataModal) {
			const savedValue = (values as any)?.joystickJitterFilter2 ?? 0;
			setRightJitterFilter(savedValue);
			setRightJitterFilterOriginal(savedValue);
		}
	}, [showRightJitterDataModal, values]);
	
	const leftFinetuneShapeCanvasRef = useRef<HTMLCanvasElement>(null);
	const rightFinetuneShapeCanvasRef = useRef<HTMLCanvasElement>(null);
	const [leftFinetuneShapeStickData, setLeftFinetuneShapeStickData] = useState({ x: 0, y: 0 });
	const [rightFinetuneShapeStickData, setRightFinetuneShapeStickData] = useState({ x: 0, y: 0 });
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
							const originalRangeData = (values as any).joystickRangeData1 || [];
							
							// Apply finetune shape adjustments from saved values
							const xTopPercent = getPercentValue((values as any)?.joystickFinetuneShapeXTopPercent1, 100.0);
							const xBottomPercent = getPercentValue((values as any)?.joystickFinetuneShapeXBottomPercent1, 100.0);
							const yLeftPercent = getPercentValue((values as any)?.joystickFinetuneShapeYLeftPercent1, 100.0);
							const yRightPercent = getPercentValue((values as any)?.joystickFinetuneShapeYRightPercent1, 100.0);
							const forceCircular = (values as any)?.joystickFinetuneShapeForceCircular1 ?? false;
							const amplify = (values as any)?.joystickFinetuneShapeAmplify1 ?? 0.0;
							
							const adjustedRangeData = applyFinetuneShapeAdjustments(
								originalRangeData,
								xTopPercent,
								xBottomPercent,
								yLeftPercent,
								yRightPercent,
								forceCircular,
								amplify
							);
							
							// Apply jitter filter for visualization using configured threshold
							const jitterThreshold1 = (values as any)?.joystickJitterFilter1 ?? 0;
							const filtered1 = applyJitterFilterToAdc(
								data1.x,
								data1.y,
								jitterThreshold1,
								leftCanvasJitterLastRef
							);

							const { stickX, stickY, detailData } = processJoystickData(
								filtered1.x,
								filtered1.y,
								centerX,
								centerY,
								adjustedRangeData
							);
							
							setLeftStickData({
								x: stickX,
								y: stickY,
								rawX: filtered1.x,
								rawY: filtered1.y,
							});
							
							setLeftStickDetailData(detailData);
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
							const originalRangeData = (values as any).joystickRangeData2 || [];
							
							// Apply finetune shape adjustments from saved values
							const xTopPercent = getPercentValue((values as any)?.joystickFinetuneShapeXTopPercent2, 100.0);
							const xBottomPercent = getPercentValue((values as any)?.joystickFinetuneShapeXBottomPercent2, 100.0);
							const yLeftPercent = getPercentValue((values as any)?.joystickFinetuneShapeYLeftPercent2, 100.0);
							const yRightPercent = getPercentValue((values as any)?.joystickFinetuneShapeYRightPercent2, 100.0);
							const forceCircular = (values as any)?.joystickFinetuneShapeForceCircular2 ?? false;
							const amplify = (values as any)?.joystickFinetuneShapeAmplify2 ?? 0.0;
							
							const adjustedRangeData = applyFinetuneShapeAdjustments(
								originalRangeData,
								xTopPercent,
								xBottomPercent,
								yLeftPercent,
								yRightPercent,
								forceCircular,
								amplify
							);
							
							// Apply jitter filter for visualization using configured threshold
							const jitterThreshold2 = (values as any)?.joystickJitterFilter2 ?? 0;
							const filtered2 = applyJitterFilterToAdc(
								data2.x,
								data2.y,
								jitterThreshold2,
								rightCanvasJitterLastRef
							);

							const { stickX, stickY, detailData } = processJoystickData(
								filtered2.x,
								filtered2.y,
								centerX,
								centerY,
								adjustedRangeData
							);
							
							setRightStickData({
								x: stickX,
								y: stickY,
								rawX: filtered2.x,
								rawY: filtered2.y,
							});
							
							setRightStickDetailData(detailData);
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
	}, [values.AnalogInputEnabled, values.analogAdc1PinX, values.analogAdc1PinY, values.analogAdc2PinX, values.analogAdc2PinY, values.joystickCenterX, values.joystickCenterY, values.joystickCenterX2, values.joystickCenterY2, values.joystickRangeData1, values.joystickRangeData2, (values as any)?.joystickFinetuneShapeXTopPercent1, (values as any)?.joystickFinetuneShapeXBottomPercent1, (values as any)?.joystickFinetuneShapeYLeftPercent1, (values as any)?.joystickFinetuneShapeYRightPercent1, (values as any)?.joystickFinetuneShapeForceCircular1, (values as any)?.joystickFinetuneShapeAmplify1, (values as any)?.joystickFinetuneShapeXTopPercent2, (values as any)?.joystickFinetuneShapeXBottomPercent2, (values as any)?.joystickFinetuneShapeYLeftPercent2, (values as any)?.joystickFinetuneShapeYRightPercent2, (values as any)?.joystickFinetuneShapeForceCircular2, (values as any)?.joystickFinetuneShapeAmplify2]);

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
						null, // No circularity data on main page canvas
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
						null, // No circularity data on main page canvas
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
	}, [leftStickData, rightStickData, leftFinetuneCenterActive, rightFinetuneCenterActive]);

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

	// Reset finetune shape data when modals open/close
	useEffect(() => {
		if (showLeftFinetuneShapeModal) {
			setLeftFinetuneShapeCircularityData(new Array(CIRCULARITY_DATA_SIZE).fill(0));
		}
		if (showRightFinetuneShapeModal) {
			setRightFinetuneShapeCircularityData(new Array(CIRCULARITY_DATA_SIZE).fill(0));
		}
	}, [showLeftFinetuneShapeModal, showRightFinetuneShapeModal]);

	// Reset circularity data when percentage values change (to reflect new coverage)
	useEffect(() => {
		if (showLeftFinetuneShapeModal) {
			setLeftFinetuneShapeCircularityData(new Array(CIRCULARITY_DATA_SIZE).fill(0));
		}
	}, [showLeftFinetuneShapeModal, leftFinetuneShapeXTopPercent, leftFinetuneShapeXBottomPercent, leftFinetuneShapeYLeftPercent, leftFinetuneShapeYRightPercent, leftFinetuneShapeForceCircular, leftFinetuneShapeAmplify]);

	useEffect(() => {
		if (showRightFinetuneShapeModal) {
			setRightFinetuneShapeCircularityData(new Array(CIRCULARITY_DATA_SIZE).fill(0));
		}
	}, [showRightFinetuneShapeModal, rightFinetuneShapeXTopPercent, rightFinetuneShapeXBottomPercent, rightFinetuneShapeYLeftPercent, rightFinetuneShapeYRightPercent, rightFinetuneShapeForceCircular, rightFinetuneShapeAmplify]);

	// Use refs to store latest percentage values for real-time updates
	const leftFinetuneShapePercentRef = useRef({
		xTop: leftFinetuneShapeXTopPercent,
		xBottom: leftFinetuneShapeXBottomPercent,
		yLeft: leftFinetuneShapeYLeftPercent,
		yRight: leftFinetuneShapeYRightPercent,
		forceCircular: leftFinetuneShapeForceCircular,
		amplify: leftFinetuneShapeAmplify
	});
	const rightFinetuneShapePercentRef = useRef({
		xTop: rightFinetuneShapeXTopPercent,
		xBottom: rightFinetuneShapeXBottomPercent,
		yLeft: rightFinetuneShapeYLeftPercent,
		yRight: rightFinetuneShapeYRightPercent,
		forceCircular: rightFinetuneShapeForceCircular,
		amplify: rightFinetuneShapeAmplify
	});

	// Update refs when percentage values change
	useEffect(() => {
		leftFinetuneShapePercentRef.current = {
			xTop: leftFinetuneShapeXTopPercent,
			xBottom: leftFinetuneShapeXBottomPercent,
			yLeft: leftFinetuneShapeYLeftPercent,
			yRight: leftFinetuneShapeYRightPercent,
			forceCircular: leftFinetuneShapeForceCircular,
			amplify: leftFinetuneShapeAmplify
		};
	}, [leftFinetuneShapeXTopPercent, leftFinetuneShapeXBottomPercent, leftFinetuneShapeYLeftPercent, leftFinetuneShapeYRightPercent, leftFinetuneShapeForceCircular, leftFinetuneShapeAmplify]);

	useEffect(() => {
		rightFinetuneShapePercentRef.current = {
			xTop: rightFinetuneShapeXTopPercent,
			xBottom: rightFinetuneShapeXBottomPercent,
			yLeft: rightFinetuneShapeYLeftPercent,
			yRight: rightFinetuneShapeYRightPercent,
			forceCircular: rightFinetuneShapeForceCircular,
			amplify: rightFinetuneShapeAmplify
		};
	}, [rightFinetuneShapeXTopPercent, rightFinetuneShapeXBottomPercent, rightFinetuneShapeYLeftPercent, rightFinetuneShapeYRightPercent, rightFinetuneShapeForceCircular, rightFinetuneShapeAmplify]);

	// Fetch joystick data for finetune shape modals
	useEffect(() => {
		if (!values || !values.AnalogInputEnabled) {
			return;
		}

		const fetchFinetuneShapeData = async () => {
			if (showLeftFinetuneShapeModal) {
				try {
					const res = await fetch('/api/getJoystickCenter');
					if (res.ok) {
						const data = await res.json();
						if (data.success) {
							const centerX = values.joystickCenterX || ADC_CENTER;
							const centerY = values.joystickCenterY || ADC_CENTER;
							const originalRangeData = (values as any).joystickRangeData1 || [];
							
							// Use ref to get latest percentage values for real-time updates
							const percentRef = leftFinetuneShapePercentRef.current;
							const adjustedRangeData = applyFinetuneShapeAdjustments(
								originalRangeData,
								percentRef.xTop,
								percentRef.xBottom,
								percentRef.yLeft,
								percentRef.yRight,
								percentRef.forceCircular,
								percentRef.amplify
							);
							
							// Apply jitter filter for visualization using configured threshold
							const jitterThreshold1 = (values as any)?.joystickJitterFilter1 ?? 0;
							const filtered1 = applyJitterFilterToAdc(
								data.x,
								data.y,
								jitterThreshold1,
								leftCanvasJitterLastRef
							);

							const { stickX, stickY } = processJoystickData(
								filtered1.x,
								filtered1.y,
								centerX,
								centerY,
								adjustedRangeData
							);
							
							setLeftFinetuneShapeStickData({ x: stickX, y: stickY });

							// Collect circularity data (always enabled in finetune shape modal)
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
				} catch (error) {
					console.error('Failed to fetch left stick data for finetune shape:', error);
				}
			}

			if (showRightFinetuneShapeModal) {
				try {
					const res = await fetch('/api/getJoystickCenter2');
					if (res.ok) {
						const data = await res.json();
						if (data.success) {
							const centerX = values.joystickCenterX2 || ADC_CENTER;
							const centerY = values.joystickCenterY2 || ADC_CENTER;
							const originalRangeData = (values as any).joystickRangeData2 || [];
							
							// Use ref to get latest percentage values for real-time updates
							const percentRef = rightFinetuneShapePercentRef.current;
							const adjustedRangeData = applyFinetuneShapeAdjustments(
								originalRangeData,
								percentRef.xTop,
								percentRef.xBottom,
								percentRef.yLeft,
								percentRef.yRight,
								percentRef.forceCircular,
								percentRef.amplify
							);
							
							// Apply jitter filter for visualization using configured threshold
							const jitterThreshold2 = (values as any)?.joystickJitterFilter2 ?? 0;
							const filtered2 = applyJitterFilterToAdc(
								data.x,
								data.y,
								jitterThreshold2,
								rightCanvasJitterLastRef
							);

							const { stickX, stickY } = processJoystickData(
								filtered2.x,
								filtered2.y,
								centerX,
								centerY,
								adjustedRangeData
							);
							
							setRightFinetuneShapeStickData({ x: stickX, y: stickY });

							// Collect circularity data (always enabled in finetune shape modal)
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
				} catch (error) {
					console.error('Failed to fetch right stick data for finetune shape:', error);
				}
			}
		};

		if (showLeftFinetuneShapeModal || showRightFinetuneShapeModal) {
			const intervalId = setInterval(fetchFinetuneShapeData, 33);
			return () => {
				clearInterval(intervalId);
			};
		}
	}, [values?.AnalogInputEnabled, values?.joystickCenterX, values?.joystickCenterY, values?.joystickCenterX2, values?.joystickCenterY2, (values as any)?.joystickRangeData1, (values as any)?.joystickRangeData2, (values as any)?.joystickJitterFilter1, (values as any)?.joystickJitterFilter2, showLeftFinetuneShapeModal, showRightFinetuneShapeModal]);

	// Update finetune shape canvas when stick data changes
	useEffect(() => {
		if (!showLeftFinetuneShapeModal && !showRightFinetuneShapeModal) {
			return;
		}

		let animationFrameId: number | null = null;

		const updateCanvas = () => {
			// Draw left finetune shape canvas
			if (leftFinetuneShapeCanvasRef.current && showLeftFinetuneShapeModal) {
				const ctx = leftFinetuneShapeCanvasRef.current.getContext('2d');
				if (ctx) {
					const canvas = leftFinetuneShapeCanvasRef.current;
					const centerX = canvas.width / 2;
					const centerY = canvas.height / 2;
					const radius = Math.min(centerX, centerY) - 10;
					
					// Always show circularity data in finetune shape modal
					drawStickPosition(
						ctx,
						centerX,
						centerY,
						radius,
						leftFinetuneShapeStickData.x,
						leftFinetuneShapeStickData.y,
						leftFinetuneShapeCircularityData, // Always show coverage and error rate
						false,
					);
				}
			}

			// Draw right finetune shape canvas
			if (rightFinetuneShapeCanvasRef.current && showRightFinetuneShapeModal) {
				const ctx = rightFinetuneShapeCanvasRef.current.getContext('2d');
				if (ctx) {
					const canvas = rightFinetuneShapeCanvasRef.current;
					const centerX = canvas.width / 2;
					const centerY = canvas.height / 2;
					const radius = Math.min(centerX, centerY) - 10;
					
					// Always show circularity data in finetune shape modal
					drawStickPosition(
						ctx,
						centerX,
						centerY,
						radius,
						rightFinetuneShapeStickData.x,
						rightFinetuneShapeStickData.y,
						rightFinetuneShapeCircularityData, // Always show coverage and error rate
						false,
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
	}, [showLeftFinetuneShapeModal, showRightFinetuneShapeModal, leftFinetuneShapeStickData, rightFinetuneShapeStickData, leftFinetuneShapeCircularityData, rightFinetuneShapeCircularityData]);

	return (
		<Section title={t('AddonsConfig:joystick-calibration-header-text')}>
			<div id="JoystickCalibrationOptions" hidden={!values || !values.AnalogInputEnabled || values.AnalogInputEnabled === 0}>
				{/* First row: Canvas visualization for left and right sticks */}
				<Row className="mb-3">
					<Col md={6} className="text-center mb-3">
						<div>
							<canvas
								ref={leftStickCanvasRef}
								width={300}
								height={300}
								style={{ border: '1px solid #ccc', borderRadius: '4px' }}
							/>
							<div className="mt-2 small">
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
							{/* Left stick calibration buttons - First row */}
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
							{/* Left stick finetune buttons - Second row */}
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
									onClick={() => {
										const rangeData = (values as any)?.joystickRangeData1;
										const hasCalibration =
											Array.isArray(rangeData) &&
											rangeData.length === CIRCULARITY_DATA_SIZE &&
											rangeData.some((v: number) => v > 0);
										if (!hasCalibration) {
											setShowRangeCalibrationWarning(true);
											return;
										}
										setShowLeftFinetuneShapeModal(true);
									}}
								>
									{t('AddonsConfig:joystick-calibration-finetune-shape-button')}
								</Button>
							</div>
							{/* Jitter data correction and view calibration data buttons */}
							<div className="mt-2 d-flex gap-2 justify-content-center flex-wrap">
								<Button
									variant="info"
									size="sm"
									disabled={leftJitterSampling}
									onClick={handleStartJitterSampling1}
								>
									{leftJitterSampling 
										? `抖动数据修正 (${leftJitterSamples.length}/30)` 
										: '抖动数据修正'}
								</Button>
								<Button
									variant="info"
									size="sm"
									onClick={() => {
										const rangeData = (values as any)?.joystickRangeData1;
										setLeftRangeDataSnapshot(Array.isArray(rangeData) ? rangeData : []);
										setLeftAngleIndexSnapshot(leftStickDetailData.angleIndex);
										setShowLeftRangeDataModal(true);
									}}
								>
									查看校准数据
								</Button>
							</div>
						</div>
					</Col>
					<Col md={6} className="text-center mb-3">
						<div>
							<canvas
								ref={rightStickCanvasRef}
								width={300}
								height={300}
								style={{ border: '1px solid #ccc', borderRadius: '4px' }}
							/>
							<div className="mt-2 small">
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
							{/* Right stick calibration buttons - First row */}
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
							{/* Right stick finetune buttons - Second row */}
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
									onClick={() => {
										const rangeData = (values as any)?.joystickRangeData2;
										const hasCalibration =
											Array.isArray(rangeData) &&
											rangeData.length === CIRCULARITY_DATA_SIZE &&
											rangeData.some((v: number) => v > 0);
										if (!hasCalibration) {
											setShowRangeCalibrationWarning(true);
											return;
										}
										setShowRightFinetuneShapeModal(true);
									}}
								>
									{t('AddonsConfig:joystick-calibration-finetune-shape-button')}
								</Button>
							</div>
							{/* Jitter data correction and view calibration data buttons */}
							<div className="mt-2 d-flex gap-2 justify-content-center flex-wrap">
								<Button
									variant="info"
									size="sm"
									disabled={rightJitterSampling}
									onClick={handleStartJitterSampling2}
								>
									{rightJitterSampling 
										? `抖动数据修正 (${rightJitterSamples.length}/30)` 
										: '抖动数据修正'}
								</Button>
								<Button
									variant="info"
									size="sm"
									onClick={() => {
										const rangeData = (values as any)?.joystickRangeData2;
										setRightRangeDataSnapshot(Array.isArray(rangeData) ? rangeData : []);
										setRightAngleIndexSnapshot(rightStickDetailData.angleIndex);
										setShowRightRangeDataModal(true);
									}}
								>
									查看校准数据
								</Button>
							</div>
						</div>
					</Col>
				</Row>
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
			
			{/* Finetune Shape Modals */}
			<Modal
				show={showLeftFinetuneShapeModal}
				onHide={() => {
					setShowLeftFinetuneShapeModal(false);
					// Reset to saved values on cancel
					setLeftFinetuneShapeXTopPercent(getPercentValue((values as any)?.joystickFinetuneShapeXTopPercent1, 100.0));
					setLeftFinetuneShapeXBottomPercent(getPercentValue((values as any)?.joystickFinetuneShapeXBottomPercent1, 100.0));
					setLeftFinetuneShapeYLeftPercent(getPercentValue((values as any)?.joystickFinetuneShapeYLeftPercent1, 100.0));
					setLeftFinetuneShapeYRightPercent(getPercentValue((values as any)?.joystickFinetuneShapeYRightPercent1, 100.0));
					setLeftFinetuneShapeForceCircular((values as any)?.joystickFinetuneShapeForceCircular1 ?? false);
					setLeftFinetuneShapeAmplify((values as any)?.joystickFinetuneShapeAmplify1 ?? 0.0);
				}}
				size="lg"
			>
				<Modal.Header closeButton>
					<Modal.Title>{t('AddonsConfig:joystick-calibration-finetune-shape-button')} - {t('AddonsConfig:joystick-calibration-left-stick')}</Modal.Title>
				</Modal.Header>
				<Modal.Body>
					<Row>
						{/* Left side: Canvas */}
						<Col md={6}>
							<div style={{ position: 'relative', display: 'inline-block', padding: '40px 60px' }}>
								<canvas
									ref={leftFinetuneShapeCanvasRef}
									width={250}
									height={250}
									style={{ border: '1px solid #ccc', borderRadius: '4px', display: 'block' }}
								/>
								{/* X-axis controls (top) - horizontal layout: + on left, value in middle, - on right */}
								{!leftFinetuneShapeForceCircular && (
									<>
										<div style={{ position: 'absolute', left: '50%', top: '0px', transform: 'translateX(-50%)', display: 'flex', alignItems: 'center', gap: '4px' }}>
											<Button
												variant="light"
												size="sm"
												style={finetuneButtonStyle}
												onClick={() => setLeftFinetuneShapeXBottomPercent(prev => prev + 0.2)}
											>
												+
											</Button>
											<span style={{ minWidth: '60px', textAlign: 'center', fontSize: '14px' }}>
												{leftFinetuneShapeXBottomPercent.toFixed(1)}%
											</span>
											<Button
												variant="light"
												size="sm"
												style={finetuneButtonStyle}
												onClick={() => setLeftFinetuneShapeXBottomPercent(prev => Math.max(0, prev - 0.2))}
											>
												−
											</Button>
										</div>
										{/* X-axis controls (bottom) - horizontal layout: + on left, value in middle, - on right */}
										<div style={{ position: 'absolute', left: '50%', bottom: '0px', transform: 'translateX(-50%)', display: 'flex', alignItems: 'center', gap: '4px' }}>
											<Button
												variant="light"
												size="sm"
												style={finetuneButtonStyle}
												onClick={() => setLeftFinetuneShapeXTopPercent(prev => prev + 0.2)}
											>
												+
											</Button>
											<span style={{ minWidth: '60px', textAlign: 'center', fontSize: '14px' }}>
												{leftFinetuneShapeXTopPercent.toFixed(1)}%
											</span>
											<Button
												variant="light"
												size="sm"
												style={finetuneButtonStyle}
												onClick={() => setLeftFinetuneShapeXTopPercent(prev => Math.max(0, prev - 0.2))}
											>
												−
											</Button>
										</div>
										{/* Y-axis controls (left) - vertical layout: + on top, value in middle, - on bottom */}
										<div style={{ position: 'absolute', left: '0px', top: '50%', transform: 'translateY(-50%)', display: 'flex', flexDirection: 'column', alignItems: 'center', gap: '2px' }}>
											<Button
												variant="light"
												size="sm"
												style={finetuneButtonStyle}
												onClick={() => setLeftFinetuneShapeYLeftPercent(prev => prev + 0.2)}
											>
												+
											</Button>
											<span style={{ minWidth: '60px', textAlign: 'center', fontSize: '14px' }}>
												{leftFinetuneShapeYLeftPercent.toFixed(1)}%
											</span>
											<Button
												variant="light"
												size="sm"
												style={finetuneButtonStyle}
												onClick={() => setLeftFinetuneShapeYLeftPercent(prev => Math.max(0, prev - 0.2))}
											>
												−
											</Button>
										</div>
										{/* Y-axis controls (right) - vertical layout: + on top, value in middle, - on bottom */}
										<div style={{ position: 'absolute', right: '0px', top: '50%', transform: 'translateY(-50%)', display: 'flex', flexDirection: 'column', alignItems: 'center', gap: '2px' }}>
											<Button
												variant="light"
												size="sm"
												style={finetuneButtonStyle}
												onClick={() => setLeftFinetuneShapeYRightPercent(prev => prev + 0.2)}
											>
												+
											</Button>
											<span style={{ minWidth: '60px', textAlign: 'center', fontSize: '14px' }}>
												{leftFinetuneShapeYRightPercent.toFixed(1)}%
											</span>
											<Button
												variant="light"
												size="sm"
												style={finetuneButtonStyle}
												onClick={() => setLeftFinetuneShapeYRightPercent(prev => Math.max(0, prev - 0.2))}
											>
												−
											</Button>
										</div>
									</>
								)}
							</div>
						</Col>
						{/* Right side: Controls */}
						<Col md={6}>
							<div style={{ display: 'flex', flexDirection: 'column', gap: '20px' }}>
								{/* Force Circular Switch */}
								<div>
									<FormCheck
										type="switch"
										id="leftFinetuneShapeForceCircular"
										label="强制圆形"
										checked={leftFinetuneShapeForceCircular}
										onChange={(e) => setLeftFinetuneShapeForceCircular(e.target.checked)}
									/>
									<p className="text-muted small mt-2 mb-0">
										{leftFinetuneShapeForceCircular
											? "强制圆形会将摇杆外圈移动半径严格归一到圆形，关闭强制圆形会直接利用摇杆原生移动距离而产生不规则外圈形状。"
											: "关闭强制圆形时将产生反映摇杆真实形状的不规则的外圈与误差率，4个方向的百分比可以调节轴向覆盖程度避免打不满。"}
									</p>
								</div>
								{/* Amplify Slider */}
								<div>
									<Form.Label>外圈放大系数: {leftFinetuneShapeAmplify.toFixed(1)}%</Form.Label>
									<Form.Range
										min={0}
										max={20}
										step={0.1}
										value={leftFinetuneShapeAmplify}
										onChange={(e) => setLeftFinetuneShapeAmplify(parseFloat(e.target.value))}
									/>
									<p className="text-muted small mt-2 mb-0">
										扩大系数滑块可以放大摇杆覆盖范围，加快摇杆快速移动响应速度。一定程度等同于误差率调整。
									</p>
								</div>
							</div>
						</Col>
					</Row>
				</Modal.Body>
				<Modal.Footer>
					<Button variant="secondary" onClick={() => {
						setShowLeftFinetuneShapeModal(false);
						// Reset to original values on cancel
						setLeftFinetuneShapeXTopPercent(100.0);
						setLeftFinetuneShapeXBottomPercent(100.0);
						setLeftFinetuneShapeYLeftPercent(100.0);
						setLeftFinetuneShapeYRightPercent(100.0);
						setLeftFinetuneShapeForceCircular(false);
						setLeftFinetuneShapeAmplify(0.0);
					}}>
						取消
					</Button>
					<Button variant="primary" onClick={() => {
						// Save percentage values only (do not modify original calibration data)
						// Backend will apply these adjustments at runtime
						setFieldValue('joystickFinetuneShapeXTopPercent1', leftFinetuneShapeXTopPercent);
						setFieldValue('joystickFinetuneShapeXBottomPercent1', leftFinetuneShapeXBottomPercent);
						setFieldValue('joystickFinetuneShapeYLeftPercent1', leftFinetuneShapeYLeftPercent);
						setFieldValue('joystickFinetuneShapeYRightPercent1', leftFinetuneShapeYRightPercent);
						setFieldValue('joystickFinetuneShapeForceCircular1', leftFinetuneShapeForceCircular);
						setFieldValue('joystickFinetuneShapeAmplify1', leftFinetuneShapeAmplify);
						setShowLeftFinetuneShapeModal(false);
					}}>
						确定
					</Button>
				</Modal.Footer>
			</Modal>
			<Modal
				show={showRightFinetuneShapeModal}
				onHide={() => {
					setShowRightFinetuneShapeModal(false);
					// Reset to saved values on cancel
					setRightFinetuneShapeXTopPercent(getPercentValue((values as any)?.joystickFinetuneShapeXTopPercent2, 100.0));
					setRightFinetuneShapeXBottomPercent(getPercentValue((values as any)?.joystickFinetuneShapeXBottomPercent2, 100.0));
					setRightFinetuneShapeYLeftPercent(getPercentValue((values as any)?.joystickFinetuneShapeYLeftPercent2, 100.0));
					setRightFinetuneShapeYRightPercent(getPercentValue((values as any)?.joystickFinetuneShapeYRightPercent2, 100.0));
					setRightFinetuneShapeForceCircular((values as any)?.joystickFinetuneShapeForceCircular2 ?? false);
					setRightFinetuneShapeAmplify((values as any)?.joystickFinetuneShapeAmplify2 ?? 0.0);
				}}
				size="lg"
			>
				<Modal.Header closeButton>
					<Modal.Title>{t('AddonsConfig:joystick-calibration-finetune-shape-button')} - {t('AddonsConfig:joystick-calibration-right-stick')}</Modal.Title>
				</Modal.Header>
				<Modal.Body>
					<Row>
						{/* Left side: Canvas */}
						<Col md={6}>
							<div style={{ position: 'relative', display: 'inline-block', padding: '40px 60px' }}>
								<canvas
									ref={rightFinetuneShapeCanvasRef}
									width={250}
									height={250}
									style={{ border: '1px solid #ccc', borderRadius: '4px', display: 'block' }}
								/>
								{/* X-axis controls (top) - horizontal layout: + on left, value in middle, - on right */}
								{!rightFinetuneShapeForceCircular && (
									<>
										<div style={{ position: 'absolute', left: '50%', top: '0px', transform: 'translateX(-50%)', display: 'flex', alignItems: 'center', gap: '4px' }}>
											<Button
												variant="light"
												size="sm"
												style={finetuneButtonStyle}
												onClick={() => setRightFinetuneShapeXBottomPercent(prev => prev + 0.2)}
											>
												+
											</Button>
											<span style={{ minWidth: '60px', textAlign: 'center', fontSize: '14px' }}>
												{rightFinetuneShapeXBottomPercent.toFixed(1)}%
											</span>
											<Button
												variant="light"
												size="sm"
												style={finetuneButtonStyle}
												onClick={() => setRightFinetuneShapeXBottomPercent(prev => Math.max(0, prev - 0.2))}
											>
												−
											</Button>
										</div>
										{/* X-axis controls (bottom) - horizontal layout: + on left, value in middle, - on right */}
										<div style={{ position: 'absolute', left: '50%', bottom: '0px', transform: 'translateX(-50%)', display: 'flex', alignItems: 'center', gap: '4px' }}>
											<Button
												variant="light"
												size="sm"
												style={finetuneButtonStyle}
												onClick={() => setRightFinetuneShapeXTopPercent(prev => prev + 0.2)}
											>
												+
											</Button>
											<span style={{ minWidth: '60px', textAlign: 'center', fontSize: '14px' }}>
												{rightFinetuneShapeXTopPercent.toFixed(1)}%
											</span>
											<Button
												variant="light"
												size="sm"
												style={finetuneButtonStyle}
												onClick={() => setRightFinetuneShapeXTopPercent(prev => Math.max(0, prev - 0.2))}
											>
												−
											</Button>
										</div>
										{/* Y-axis controls (left) - vertical layout: + on top, value in middle, - on bottom */}
										<div style={{ position: 'absolute', left: '0px', top: '50%', transform: 'translateY(-50%)', display: 'flex', flexDirection: 'column', alignItems: 'center', gap: '2px' }}>
											<Button
												variant="light"
												size="sm"
												style={finetuneButtonStyle}
												onClick={() => setRightFinetuneShapeYLeftPercent(prev => prev + 0.2)}
											>
												+
											</Button>
											<span style={{ minWidth: '60px', textAlign: 'center', fontSize: '14px' }}>
												{rightFinetuneShapeYLeftPercent.toFixed(1)}%
											</span>
											<Button
												variant="light"
												size="sm"
												style={finetuneButtonStyle}
												onClick={() => setRightFinetuneShapeYLeftPercent(prev => Math.max(0, prev - 0.2))}
											>
												−
											</Button>
										</div>
										{/* Y-axis controls (right) - vertical layout: + on top, value in middle, - on bottom */}
										<div style={{ position: 'absolute', right: '0px', top: '50%', transform: 'translateY(-50%)', display: 'flex', flexDirection: 'column', alignItems: 'center', gap: '2px' }}>
											<Button
												variant="light"
												size="sm"
												style={finetuneButtonStyle}
												onClick={() => setRightFinetuneShapeYRightPercent(prev => prev + 0.2)}
											>
												+
											</Button>
											<span style={{ minWidth: '60px', textAlign: 'center', fontSize: '14px' }}>
												{rightFinetuneShapeYRightPercent.toFixed(1)}%
											</span>
											<Button
												variant="light"
												size="sm"
												style={finetuneButtonStyle}
												onClick={() => setRightFinetuneShapeYRightPercent(prev => Math.max(0, prev - 0.2))}
											>
												−
											</Button>
										</div>
									</>
								)}
							</div>
						</Col>
						{/* Right side: Controls */}
						<Col md={6}>
							<div style={{ display: 'flex', flexDirection: 'column', gap: '20px' }}>
								{/* Force Circular Switch */}
								<div>
									<FormCheck
										type="switch"
										id="rightFinetuneShapeForceCircular"
										label="强制圆形"
										checked={rightFinetuneShapeForceCircular}
										onChange={(e) => setRightFinetuneShapeForceCircular(e.target.checked)}
									/>
									<p className="text-muted small mt-2 mb-0">
										{rightFinetuneShapeForceCircular
											? "强制圆形会将摇杆外圈移动半径严格归一到圆形，关闭强制圆形会直接利用摇杆原生移动距离而产生不规则外圈形状。"
											: "关闭强制圆形时将产生反映摇杆真实形状的不规则的外圈与误差率，4个方向的百分比可以调节轴向覆盖程度避免打不满。"}
									</p>
								</div>
								{/* Amplify Slider */}
								<div>
									<Form.Label>外圈放大系数: {rightFinetuneShapeAmplify.toFixed(1)}%</Form.Label>
									<Form.Range
										min={0}
										max={20}
										step={0.1}
										value={rightFinetuneShapeAmplify}
										onChange={(e) => setRightFinetuneShapeAmplify(parseFloat(e.target.value))}
									/>
									<p className="text-muted small mt-2 mb-0">
										扩大系数滑块可以放大摇杆覆盖范围，加快摇杆快速移动响应速度。一定程度等同于误差率调整。
									</p>
								</div>
							</div>
						</Col>
					</Row>
				</Modal.Body>
				<Modal.Footer>
					<Button variant="secondary" onClick={() => {
						setShowRightFinetuneShapeModal(false);
						// Reset to original values on cancel
						setRightFinetuneShapeXTopPercent(100.0);
						setRightFinetuneShapeXBottomPercent(100.0);
						setRightFinetuneShapeYLeftPercent(100.0);
						setRightFinetuneShapeYRightPercent(100.0);
						setRightFinetuneShapeForceCircular(false);
						setRightFinetuneShapeAmplify(0.0);
					}}>
						取消
					</Button>
					<Button variant="primary" onClick={() => {
						// Save percentage values only (do not modify original calibration data)
						// Backend will apply these adjustments at runtime
						setFieldValue('joystickFinetuneShapeXTopPercent2', rightFinetuneShapeXTopPercent);
						setFieldValue('joystickFinetuneShapeXBottomPercent2', rightFinetuneShapeXBottomPercent);
						setFieldValue('joystickFinetuneShapeYLeftPercent2', rightFinetuneShapeYLeftPercent);
						setFieldValue('joystickFinetuneShapeYRightPercent2', rightFinetuneShapeYRightPercent);
						setFieldValue('joystickFinetuneShapeForceCircular2', rightFinetuneShapeForceCircular);
						setFieldValue('joystickFinetuneShapeAmplify2', rightFinetuneShapeAmplify);
						setShowRightFinetuneShapeModal(false);
					}}>
						确定
					</Button>
				</Modal.Footer>
			</Modal>
			
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


