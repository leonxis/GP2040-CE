import { useEffect, useRef, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { Row, Col, Button, FormCheck } from 'react-bootstrap';

import Section from '../Components/Section';
import StickCalibrationModal from '../Components/StickCalibrationModal';
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
	const [leftCircularityEnabled, setLeftCircularityEnabled] = useState(false);
	const [rightCircularityEnabled, setRightCircularityEnabled] = useState(false);
	const [leftCircularityData, setLeftCircularityData] = useState<number[]>(new Array(CIRCULARITY_DATA_SIZE).fill(0));
	const [rightCircularityData, setRightCircularityData] = useState<number[]>(new Array(CIRCULARITY_DATA_SIZE).fill(0));

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
							// Convert raw ADC (0-4095) to normalized (-1 to 1)
							const ADC_MAX = 4095;
							const centerX = values.joystickCenterX || ADC_MAX / 2;
							const centerY = values.joystickCenterY || ADC_MAX / 2;
							
							const normalizedX = ((data1.x - centerX) / (ADC_MAX / 2));
							const normalizedY = ((data1.y - centerY) / (ADC_MAX / 2));
							
							const stickX = Math.max(-1, Math.min(1, normalizedX));
							const stickY = Math.max(-1, Math.min(1, normalizedY));
							
							setLeftStickData({
								x: stickX,
								y: stickY,
								rawX: data1.x,
								rawY: data1.y,
							});

							// Collect circularity data if enabled
							if (leftCircularityEnabled) {
								const distance = Math.sqrt(stickX * stickX + stickY * stickY);
								const angleIndex = (Math.round(Math.atan2(stickY, stickX) * CIRCULARITY_DATA_SIZE / 2.0 / Math.PI) + CIRCULARITY_DATA_SIZE) % CIRCULARITY_DATA_SIZE;
								setLeftCircularityData(prev => {
									const newData = [...prev];
									const oldValue = newData[angleIndex] ?? 0;
									newData[angleIndex] = Math.max(oldValue, distance);
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
							// Convert raw ADC (0-4095) to normalized (-1 to 1)
							const ADC_MAX = 4095;
							const centerX = values.joystickCenterX2 || ADC_MAX / 2;
							const centerY = values.joystickCenterY2 || ADC_MAX / 2;
							
							const normalizedX = ((data2.x - centerX) / (ADC_MAX / 2));
							const normalizedY = ((data2.y - centerY) / (ADC_MAX / 2));
							
							const stickX = Math.max(-1, Math.min(1, normalizedX));
							const stickY = Math.max(-1, Math.min(1, normalizedY));
							
							setRightStickData({
								x: stickX,
								y: stickY,
								rawX: data2.x,
								rawY: data2.y,
							});

							// Collect circularity data if enabled
							if (rightCircularityEnabled) {
								const distance = Math.sqrt(stickX * stickX + stickY * stickY);
								const angleIndex = (Math.round(Math.atan2(stickY, stickX) * CIRCULARITY_DATA_SIZE / 2.0 / Math.PI) + CIRCULARITY_DATA_SIZE) % CIRCULARITY_DATA_SIZE;
								setRightCircularityData(prev => {
									const newData = [...prev];
									const oldValue = newData[angleIndex] ?? 0;
									newData[angleIndex] = Math.max(oldValue, distance);
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
							{/* Left stick calibration buttons */}
							<div className="mt-3 d-flex gap-2 justify-content-center">
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
									onClick={() => {
										// TODO: Implement left stick range calibration
										alert(t('AddonsConfig:joystick-calibration-range-not-implemented'));
									}}
								>
									{t('AddonsConfig:joystick-calibration-range-button')}
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
							{/* Right stick calibration buttons */}
							<div className="mt-3 d-flex gap-2 justify-content-center">
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
									onClick={() => {
										// TODO: Implement right stick range calibration
										alert(t('AddonsConfig:joystick-calibration-range-not-implemented'));
									}}
								>
									{t('AddonsConfig:joystick-calibration-range-button')}
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
		</Section>
	);
};

export default JoystickCalibration;
