import { useEffect, useRef, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { Row, Col, Button, FormCheck, Modal, Table } from 'react-bootstrap';

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
 * Processes joystick data through coordinate transformation pipeline
 * @param rawX - Raw ADC X value
 * @param rawY - Raw ADC Y value
 * @param centerX - Calibrated center X value
 * @param centerY - Calibrated center Y value
 * @param rangeData - Range calibration data array
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
	
	// Calculate angle and get scale
	const angle = Math.atan2(offset_center_y, offset_center_x);
	const angleIndex = Math.round((angle + Math.PI) * CIRCULARITY_DATA_SIZE / (2 * Math.PI)) % CIRCULARITY_DATA_SIZE;
	const scale = rangeData[angleIndex] > 0 ? rangeData[angleIndex] : 0;
	
	// Step 4: Apply scale
	let scaled_center_x = 0;
	let scaled_center_y = 0;
	if (scale > 0 && (offset_center_x !== 0 || offset_center_y !== 0)) {
		scaled_center_x = offset_center_x / scale;
		scaled_center_y = offset_center_y / scale;
	} else {
		scaled_center_x = offset_center_x;
		scaled_center_y = offset_center_y;
	}
	
	// Step 6: Normalize
	const normalizedX = scaled_center_x / ADC_MAX + 0.5;
	const normalizedY = scaled_center_y / ADC_MAX + 0.5;
	
	// Convert to display format (-1 to 1)
	const stickX = Math.max(-1, Math.min(1, (normalizedX - 0.5) * 2));
	const stickY = Math.max(-1, Math.min(1, (normalizedY - 0.5) * 2));
	
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
			normalizedX,
			normalizedY,
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

const JoystickCalibration = ({
	values,
	setFieldValue,
}: AddonPropTypes) => {
	const { t } = useTranslation();
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
	const [leftCircularityEnabled, setLeftCircularityEnabled] = useState(false);
	const [rightCircularityEnabled, setRightCircularityEnabled] = useState(false);
	const [leftCircularityData, setLeftCircularityData] = useState<number[]>(new Array(CIRCULARITY_DATA_SIZE).fill(0));
	const [rightCircularityData, setRightCircularityData] = useState<number[]>(new Array(CIRCULARITY_DATA_SIZE).fill(0));
	const [showLeftRangeDataModal, setShowLeftRangeDataModal] = useState(false);
	const [showRightRangeDataModal, setShowRightRangeDataModal] = useState(false);
	const [leftRangeDataSnapshot, setLeftRangeDataSnapshot] = useState<number[]>([]);
	const [rightRangeDataSnapshot, setRightRangeDataSnapshot] = useState<number[]>([]);
	const [leftAngleIndexSnapshot, setLeftAngleIndexSnapshot] = useState(0);
	const [rightAngleIndexSnapshot, setRightAngleIndexSnapshot] = useState(0);
	const [leftFinetuneCenterActive, setLeftFinetuneCenterActive] = useState(false);
	const [rightFinetuneCenterActive, setRightFinetuneCenterActive] = useState(false);
	
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
							const rangeData = (values as any).joystickRangeData1 || [];
							
							const { stickX, stickY, detailData } = processJoystickData(
								data1.x,
								data1.y,
								centerX,
								centerY,
								rangeData
							);
							
							setLeftStickData({
								x: stickX,
								y: stickY,
								rawX: data1.x,
								rawY: data1.y,
							});
							
							setLeftStickDetailData(detailData);

							// Collect circularity data if enabled
							if (leftCircularityEnabled) {
								const distance = Math.sqrt(stickX * stickX + stickY * stickY);
								const circAngleIndex = (Math.round(Math.atan2(stickY, stickX) * CIRCULARITY_DATA_SIZE / 2.0 / Math.PI) + CIRCULARITY_DATA_SIZE) % CIRCULARITY_DATA_SIZE;
								setLeftCircularityData(prev => {
									const newData = [...prev];
									const oldValue = newData[circAngleIndex] ?? 0;
									newData[circAngleIndex] = Math.max(oldValue, distance);
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
							const rangeData = (values as any).joystickRangeData2 || [];
							
							const { stickX, stickY, detailData } = processJoystickData(
								data2.x,
								data2.y,
								centerX,
								centerY,
								rangeData
							);
							
							setRightStickData({
								x: stickX,
								y: stickY,
								rawX: data2.x,
								rawY: data2.y,
							});
							
							setRightStickDetailData(detailData);

							// Collect circularity data if enabled
							if (rightCircularityEnabled) {
								const distance = Math.sqrt(stickX * stickX + stickY * stickY);
								const circAngleIndex = (Math.round(Math.atan2(stickY, stickX) * CIRCULARITY_DATA_SIZE / 2.0 / Math.PI) + CIRCULARITY_DATA_SIZE) % CIRCULARITY_DATA_SIZE;
								setRightCircularityData(prev => {
									const newData = [...prev];
									const oldValue = newData[circAngleIndex] ?? 0;
									newData[circAngleIndex] = Math.max(oldValue, distance);
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
	}, [values.AnalogInputEnabled, values.analogAdc1PinX, values.analogAdc1PinY, values.analogAdc2PinX, values.analogAdc2PinY, values.joystickCenterX, values.joystickCenterY, values.joystickCenterX2, values.joystickCenterY2, values.joystickRangeData1, values.joystickRangeData2, leftCircularityEnabled, rightCircularityEnabled]);

	// Update canvas when stick data changes
	useEffect(() => {
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
						leftCircularityEnabled ? leftCircularityData : null,
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
						rightCircularityEnabled ? rightCircularityData : null,
						rightFinetuneCenterActive,
					);
				}
			}
		};

		updateCanvas();
	}, [leftStickData, rightStickData, leftCircularityData, rightCircularityData, leftCircularityEnabled, rightCircularityEnabled, leftFinetuneCenterActive, rightFinetuneCenterActive]);

	return (
		<Section title={t('AddonsConfig:joystick-calibration-header-text')}>
			<div id="JoystickCalibrationOptions" hidden={!values || !values.AnalogInputEnabled || values.AnalogInputEnabled === 0}>
				{/* First row: Canvas visualization for left and right sticks */}
				<Row className="mb-3">
					<Col md={6} className="text-center mb-3">
						<div>
							<div className="mb-2 d-flex align-items-center justify-content-center gap-2">
								<FormCheck
									type="switch"
									id="leftCircularityToggle"
									label={t('AddonsConfig:joystick-calibration-left-stick-error-rate')}
									checked={leftCircularityEnabled}
									onChange={(e) => {
										setLeftCircularityEnabled(e.target.checked);
										if (!e.target.checked) {
											setLeftCircularityData(new Array(CIRCULARITY_DATA_SIZE).fill(0));
										}
									}}
								/>
							</div>
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
									onClick={() => setShowLeftFinetuneShapeModal(true)}
								>
									{t('AddonsConfig:joystick-calibration-finetune-shape-button')}
								</Button>
							</div>
							{/* Detailed data display */}
							<div className="mt-2 small text-start" style={{ fontSize: '0.75rem', maxWidth: '300px', margin: '0 auto' }}>
								<div>1. 原始中心: ({leftStickDetailData.centerX.toFixed(1)}, {leftStickDetailData.centerY.toFixed(1)})</div>
								<div>2. 校准索引: {leftStickDetailData.angleIndex}, 缩放比: {leftStickDetailData.scale > 0 ? leftStickDetailData.scale.toFixed(4) : 'N/A'}</div>
								<div>3. 原始ADC: ({leftStickDetailData.rawAdcX.toFixed(1)}, {leftStickDetailData.rawAdcY.toFixed(1)})</div>
								<div>4. 平移后: ({leftStickDetailData.offsetCenterX.toFixed(1)}, {leftStickDetailData.offsetCenterY.toFixed(1)})</div>
								<div>5. Scale后: ({leftStickDetailData.scaledCenterX.toFixed(1)}, {leftStickDetailData.scaledCenterY.toFixed(1)})</div>
								<div>6. 归一化: ({leftStickDetailData.normalizedX.toFixed(4)}, {leftStickDetailData.normalizedY.toFixed(4)})</div>
							</div>
							{/* View calibration data button */}
							<div className="mt-2 d-flex gap-2 justify-content-center flex-wrap">
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
							<div className="mb-2 d-flex align-items-center justify-content-center gap-2">
								<FormCheck
									type="switch"
									id="rightCircularityToggle"
									label={t('AddonsConfig:joystick-calibration-right-stick-error-rate')}
									checked={rightCircularityEnabled}
									onChange={(e) => {
										setRightCircularityEnabled(e.target.checked);
										if (!e.target.checked) {
											setRightCircularityData(new Array(CIRCULARITY_DATA_SIZE).fill(0));
										}
									}}
								/>
							</div>
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
									onClick={() => setShowRightFinetuneShapeModal(true)}
								>
									{t('AddonsConfig:joystick-calibration-finetune-shape-button')}
								</Button>
							</div>
							{/* Detailed data display */}
							<div className="mt-2 small text-start" style={{ fontSize: '0.75rem', maxWidth: '300px', margin: '0 auto' }}>
								<div>1. 原始中心: ({rightStickDetailData.centerX.toFixed(1)}, {rightStickDetailData.centerY.toFixed(1)})</div>
								<div>2. 校准索引: {rightStickDetailData.angleIndex}, 缩放比: {rightStickDetailData.scale > 0 ? rightStickDetailData.scale.toFixed(4) : 'N/A'}</div>
								<div>3. 原始ADC: ({rightStickDetailData.rawAdcX.toFixed(1)}, {rightStickDetailData.rawAdcY.toFixed(1)})</div>
								<div>4. 平移后: ({rightStickDetailData.offsetCenterX.toFixed(1)}, {rightStickDetailData.offsetCenterY.toFixed(1)})</div>
								<div>5. Scale后: ({rightStickDetailData.scaledCenterX.toFixed(1)}, {rightStickDetailData.scaledCenterY.toFixed(1)})</div>
								<div>6. 归一化: ({rightStickDetailData.normalizedX.toFixed(4)}, {rightStickDetailData.normalizedY.toFixed(4)})</div>
							</div>
							{/* View calibration data button */}
							<div className="mt-2 d-flex gap-2 justify-content-center flex-wrap">
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
				onHide={() => setShowLeftFinetuneShapeModal(false)}
				size="lg"
			>
				<Modal.Header closeButton>
					<Modal.Title>{t('AddonsConfig:joystick-calibration-finetune-shape-button')} - {t('AddonsConfig:joystick-calibration-left-stick')}</Modal.Title>
				</Modal.Header>
				<Modal.Body>
					<p>调整外圈形状功能待实现</p>
				</Modal.Body>
				<Modal.Footer>
					<Button variant="secondary" onClick={() => setShowLeftFinetuneShapeModal(false)}>
						关闭
					</Button>
				</Modal.Footer>
			</Modal>
			<Modal
				show={showRightFinetuneShapeModal}
				onHide={() => setShowRightFinetuneShapeModal(false)}
				size="lg"
			>
				<Modal.Header closeButton>
					<Modal.Title>{t('AddonsConfig:joystick-calibration-finetune-shape-button')} - {t('AddonsConfig:joystick-calibration-right-stick')}</Modal.Title>
				</Modal.Header>
				<Modal.Body>
					<p>调整外圈形状功能待实现</p>
				</Modal.Body>
				<Modal.Footer>
					<Button variant="secondary" onClick={() => setShowRightFinetuneShapeModal(false)}>
						关闭
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
		</Section>
	);
};

export default JoystickCalibration;


