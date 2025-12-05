import { useEffect, useRef, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { Row, Col, Button, FormCheck, Modal, Table } from 'react-bootstrap';

import Section from '../Components/Section';
import StickCalibrationModal from '../Components/StickCalibrationModal';
import RangeCalibrationModal from '../Components/RangeCalibrationModal';
import { AddonPropTypes } from '../Pages/AddonsConfigPage';

const CIRCULARITY_DATA_SIZE = 48; // Number of angular positions to sample

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
	rawX: number, // Raw ADC value
	rawY: number, // Raw ADC value
	circularityData?: number[] | null,
) => {
	// Fill entire canvas with white background
	ctx.fillStyle = '#ffffff';
	ctx.fillRect(0, 0, ctx.canvas.width, ctx.canvas.height);

	// Draw base circle (outer boundary)
	ctx.lineWidth = 2;
	ctx.fillStyle = '#ffffff';
	ctx.strokeStyle = '#000000';
	ctx.beginPath();
	ctx.arc(centerX, centerY, radius, 0, 2 * Math.PI);
	ctx.closePath();
	ctx.fill();
	ctx.stroke();

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

	// Draw stick line from center to position
	ctx.strokeStyle = '#000000';
	ctx.lineWidth = 2;
	ctx.beginPath();
	ctx.moveTo(centerX, centerY);
	ctx.lineTo(centerX + stickX * radius, centerY + stickY * radius);
	ctx.stroke();

	// Draw filled circle at stick position
	ctx.beginPath();
	ctx.arc(
		centerX + stickX * radius,
		centerY + stickY * radius,
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
							const ADC_MAX = 4095;
							const ADC_CENTER = ADC_MAX / 2;
							const centerX = values.joystickCenterX || ADC_CENTER;
							const centerY = values.joystickCenterY || ADC_CENTER;
							
							// Step 2: Coordinate translation (offset transformation)
							const dX_value = centerX - ADC_CENTER;
							const dY_value = centerY - ADC_CENTER;
							const offset_x = data1.x - dX_value;
							const offset_y = data1.y - dY_value;
							
							// Step 3: Move to adc_offset_center coordinate system
							const offset_center_x = offset_x - ADC_CENTER;
							const offset_center_y = offset_y - ADC_CENTER;
							
							// Calculate angle and get scale
							const angle = Math.atan2(offset_center_y, offset_center_x);
							const angleIndex = Math.round((angle + Math.PI) * CIRCULARITY_DATA_SIZE / (2 * Math.PI)) % CIRCULARITY_DATA_SIZE;
							const rangeData = (values as any).joystickRangeData1 || [];
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
							
							setLeftStickData({
								x: stickX,
								y: stickY,
								rawX: data1.x,
								rawY: data1.y,
							});
							
							// Store detailed data
							setLeftStickDetailData({
								centerX: centerX,
								centerY: centerY,
								rawAdcX: data1.x,
								rawAdcY: data1.y,
								angleIndex: angleIndex,
								scale: scale,
								offsetCenterX: offset_center_x,
								offsetCenterY: offset_center_y,
								scaledCenterX: scaled_center_x,
								scaledCenterY: scaled_center_y,
								normalizedX: normalizedX,
								normalizedY: normalizedY,
							});

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
							const ADC_MAX = 4095;
							const ADC_CENTER = ADC_MAX / 2;
							const centerX = values.joystickCenterX2 || ADC_CENTER;
							const centerY = values.joystickCenterY2 || ADC_CENTER;
							
							// Step 2: Coordinate translation (offset transformation)
							const dX_value = centerX - ADC_CENTER;
							const dY_value = centerY - ADC_CENTER;
							const offset_x = data2.x - dX_value;
							const offset_y = data2.y - dY_value;
							
							// Step 3: Move to adc_offset_center coordinate system
							const offset_center_x = offset_x - ADC_CENTER;
							const offset_center_y = offset_y - ADC_CENTER;
							
							// Calculate angle and get scale
							const angle = Math.atan2(offset_center_y, offset_center_x);
							const angleIndex = Math.round((angle + Math.PI) * CIRCULARITY_DATA_SIZE / (2 * Math.PI)) % CIRCULARITY_DATA_SIZE;
							const rangeData = (values as any).joystickRangeData2 || [];
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
							
							setRightStickData({
								x: stickX,
								y: stickY,
								rawX: data2.x,
								rawY: data2.y,
							});
							
							// Store detailed data
							setRightStickDetailData({
								centerX: centerX,
								centerY: centerY,
								rawAdcX: data2.x,
								rawAdcY: data2.y,
								angleIndex: angleIndex,
								scale: scale,
								offsetCenterX: offset_center_x,
								offsetCenterY: offset_center_y,
								scaledCenterX: scaled_center_x,
								scaledCenterY: scaled_center_y,
								normalizedX: normalizedX,
								normalizedY: normalizedY,
							});

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
	}, [values.AnalogInputEnabled, values.analogAdc1PinX, values.analogAdc1PinY, values.analogAdc2PinX, values.analogAdc2PinY, values.joystickCenterX, values.joystickCenterY, values.joystickCenterX2, values.joystickCenterY2, leftCircularityEnabled, rightCircularityEnabled]);

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
						leftStickData.rawX,
						leftStickData.rawY,
						leftCircularityEnabled ? leftCircularityData : null,
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
						rightStickData.rawX,
						rightStickData.rawY,
						rightCircularityEnabled ? rightCircularityData : null,
					);
				}
			}
		};

		updateCanvas();
	}, [leftStickData, rightStickData, leftCircularityData, rightCircularityData, leftCircularityEnabled, rightCircularityEnabled]);

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
								width={200}
								height={200}
								style={{ border: '1px solid #ccc', borderRadius: '4px' }}
							/>
							<div className="mt-2 small">
								<div>
									X: {leftStickData.rawX} ({leftStickData.x.toFixed(3)})
								</div>
								<div>
									Y: {leftStickData.rawY} ({leftStickData.y.toFixed(3)})
								</div>
							</div>
							{/* Detailed data display */}
							<div className="mt-2 small text-start" style={{ fontSize: '0.75rem', maxWidth: '200px', margin: '0 auto' }}>
								<div>1. 原始中心: ({leftStickDetailData.centerX.toFixed(1)}, {leftStickDetailData.centerY.toFixed(1)})</div>
								<div>2. 校准索引: {leftStickDetailData.angleIndex}, 缩放比: {leftStickDetailData.scale > 0 ? leftStickDetailData.scale.toFixed(4) : 'N/A'}</div>
								<div>3. 原始ADC: ({leftStickDetailData.rawAdcX.toFixed(1)}, {leftStickDetailData.rawAdcY.toFixed(1)})</div>
								<div>4. 平移后: ({leftStickDetailData.offsetCenterX.toFixed(1)}, {leftStickDetailData.offsetCenterY.toFixed(1)})</div>
								<div>5. Scale后: ({leftStickDetailData.scaledCenterX.toFixed(1)}, {leftStickDetailData.scaledCenterY.toFixed(1)})</div>
								<div>6. 归一化: ({leftStickDetailData.normalizedX.toFixed(4)}, {leftStickDetailData.normalizedY.toFixed(4)})</div>
							</div>
							{/* Left stick calibration buttons */}
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
								<Button
									variant="info"
									size="sm"
									onClick={() => {
										const rangeData = (values as any)?.joystickRangeData1;
										console.log('Left range data:', rangeData, 'Type:', typeof rangeData, 'IsArray:', Array.isArray(rangeData));
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
								width={200}
								height={200}
								style={{ border: '1px solid #ccc', borderRadius: '4px' }}
							/>
							<div className="mt-2 small">
								<div>
									X: {rightStickData.rawX} ({rightStickData.x.toFixed(3)})
								</div>
								<div>
									Y: {rightStickData.rawY} ({rightStickData.y.toFixed(3)})
								</div>
							</div>
							{/* Detailed data display */}
							<div className="mt-2 small text-start" style={{ fontSize: '0.75rem', maxWidth: '200px', margin: '0 auto' }}>
								<div>1. 原始中心: ({rightStickDetailData.centerX.toFixed(1)}, {rightStickDetailData.centerY.toFixed(1)})</div>
								<div>2. 校准索引: {rightStickDetailData.angleIndex}, 缩放比: {rightStickDetailData.scale > 0 ? rightStickDetailData.scale.toFixed(4) : 'N/A'}</div>
								<div>3. 原始ADC: ({rightStickDetailData.rawAdcX.toFixed(1)}, {rightStickDetailData.rawAdcY.toFixed(1)})</div>
								<div>4. 平移后: ({rightStickDetailData.offsetCenterX.toFixed(1)}, {rightStickDetailData.offsetCenterY.toFixed(1)})</div>
								<div>5. Scale后: ({rightStickDetailData.scaledCenterX.toFixed(1)}, {rightStickDetailData.scaledCenterY.toFixed(1)})</div>
								<div>6. 归一化: ({rightStickDetailData.normalizedX.toFixed(4)}, {rightStickDetailData.normalizedY.toFixed(4)})</div>
							</div>
							{/* Right stick calibration buttons */}
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
								<Button
									variant="info"
									size="sm"
									onClick={() => {
										const rangeData = (values as any)?.joystickRangeData2;
										console.log('Right range data:', rangeData);
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
