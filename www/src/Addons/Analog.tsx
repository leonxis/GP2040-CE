import { useContext, useState, useEffect, useRef } from 'react';
import { useTranslation } from 'react-i18next';
import { FormCheck, Row, Tab, Tabs, Form, Modal, Table, Button } from 'react-bootstrap';
import * as yup from 'yup';

import Section from '../Components/Section';
import FormSelect from '../Components/FormSelect';
import { ANALOG_PINS } from '../Data/Buttons';
import AnalogPinOptions from '../Components/AnalogPinOptions';
import { AppContext } from '../Contexts/AppContext';
import FormControl from '../Components/FormControl';
import { AddonPropTypes } from '../Pages/AddonsConfigPage';

const ANALOG_STICK_MODES = [
	{ label: 'Left Analog', value: 1 },
	{ label: 'Right Analog', value: 2 },
];

const INVERT_MODES = [
	{ label: 'None', value: 0 },
	{ label: 'X Axis', value: 1 },
	{ label: 'Y Axis', value: 2 },
	{ label: 'X/Y Axis', value: 3 },
];

// ANALOG_ERROR_RATES removed - no longer used after removing forced_circularity

export const analogScheme = {
	AnalogInputEnabled: yup.number().required().label('Analog Input Enabled'),
	analogAdc1PinX: yup
		.number()
		.label('Analog Stick 1 Pin X')
		.validatePinWhenValue('AnalogInputEnabled'),
	analogAdc1PinY: yup
		.number()
		.label('Analog Stick 1 Pin Y')
		.validatePinWhenValue('AnalogInputEnabled'),
	analogAdc1Mode: yup
		.number()
		.label('Analog Stick 1 Mode')
		.validateSelectionWhenValue('AnalogInputEnabled', ANALOG_STICK_MODES),
	analogAdc1Invert: yup
		.number()
		.label('Analog Stick 1 Invert')
		.validateSelectionWhenValue('AnalogInputEnabled', INVERT_MODES),
	analogAdc2PinX: yup
		.number()
		.label('Analog Stick 2 Pin X')
		.validatePinWhenValue('AnalogInputEnabled'),
	analogAdc2PinY: yup
		.number()
		.label('Analog Stick 2 Pin Y')
		.validatePinWhenValue('AnalogInputEnabled'),
	analogAdc2Mode: yup
		.number()
		.label('Analog Stick 2 Mode')
		.validateSelectionWhenValue('AnalogInputEnabled', ANALOG_STICK_MODES),
	analogAdc2Invert: yup
		.number()
		.label('Analog Stick 2 Invert')
		.validateSelectionWhenValue('AnalogInputEnabled', INVERT_MODES),

	// forced_circularity and forced_circularity2 removed - replaced by range calibration
	inner_deadzone: yup
		.number()
		.label('Inner Deadzone Size (%)')
		.validateRangeWhenValue('AnalogInputEnabled', 0, 100),
	anti_deadzone: yup
		.number()
		.label('Inner Anti-Deadzone Size (%)')
		.validateRangeWhenValue('AnalogInputEnabled', 0, 100),
	inner_deadzone2: yup
		.number()
		.label('Inner Deadzone Size (%)')
		.validateRangeWhenValue('AnalogInputEnabled', 0, 100),
	anti_deadzone2: yup
		.number()
		.label('Inner Anti-Deadzone Size (%)')
		.validateRangeWhenValue('AnalogInputEnabled', 0, 100),
	// Auto calibration removed - deprecated fields
	// Outer deadzone removed - replaced by range calibration
	analog_smoothing: yup
		.number()
		.label('Analog Smoothing')
		.validateRangeWhenValue('AnalogInputEnabled', 0, 1),
	analog_smoothing2: yup
		.number()
		.label('Analog Smoothing 2')
		.validateRangeWhenValue('AnalogInputEnabled', 0, 1),
	smoothing_factor: yup
		.number()
		.label('Smoothing Factor')
		.validateRangeWhenValue('AnalogInputEnabled', 0, 100),
	smoothing_factor2: yup
		.number()
		.label('Smoothing Factor 2')
		.validateRangeWhenValue('AnalogInputEnabled', 0, 100),
	joystickCenterX: yup
		.number()
		.label('Joystick Center X')
		.validateRangeWhenValue('AnalogInputEnabled', 0, 4095),
	joystickCenterY: yup
		.number()
		.label('Joystick Center Y')
		.validateRangeWhenValue('AnalogInputEnabled', 0, 4095),
	joystickCenterX2: yup
		.number()
		.label('Joystick Center X2')
		.validateRangeWhenValue('AnalogInputEnabled', 0, 4095),
	joystickCenterY2: yup
		.number()
		.label('Joystick Center Y2')
		.validateRangeWhenValue('AnalogInputEnabled', 0, 4095),
	joystickRangeData1: yup
		.array()
		.of(yup.number())
		.label('Joystick Range Data 1'),
	joystickRangeData2: yup
		.array()
		.of(yup.number())
		.label('Joystick Range Data 2'),
	// Finetune shape adjustment percentages
	joystickFinetuneShapeXTopPercent1: yup.number().label('Joystick Finetune Shape X Top Percent 1'),
	joystickFinetuneShapeXBottomPercent1: yup.number().label('Joystick Finetune Shape X Bottom Percent 1'),
	joystickFinetuneShapeYLeftPercent1: yup.number().label('Joystick Finetune Shape Y Left Percent 1'),
	joystickFinetuneShapeYRightPercent1: yup.number().label('Joystick Finetune Shape Y Right Percent 1'),
	joystickFinetuneShapeForceCircular1: yup.boolean().label('Joystick Finetune Shape Force Circular 1'),
	joystickFinetuneShapeAmplify1: yup.number().label('Joystick Finetune Shape Amplify 1'),
	joystickFinetuneShapeXTopPercent2: yup.number().label('Joystick Finetune Shape X Top Percent 2'),
	joystickFinetuneShapeXBottomPercent2: yup.number().label('Joystick Finetune Shape X Bottom Percent 2'),
	joystickFinetuneShapeYLeftPercent2: yup.number().label('Joystick Finetune Shape Y Left Percent 2'),
	joystickFinetuneShapeYRightPercent2: yup.number().label('Joystick Finetune Shape Y Right Percent 2'),
	joystickFinetuneShapeForceCircular2: yup.boolean().label('Joystick Finetune Shape Force Circular 2'),
	joystickFinetuneShapeAmplify2: yup.number().label('Joystick Finetune Shape Amplify 2'),
};

export const analogState = {
	AnalogInputEnabled: 0,
	analogAdc1PinX: -1,
	analogAdc1PinY: -1,
	analogAdc1Mode: 1,
	analogAdc1Invert: 0,
	analogAdc2PinX: -1,
	analogAdc2PinY: -1,
	analogAdc2Mode: 2,
	analogAdc2Invert: 0,
	// forced_circularity and forced_circularity2 removed - replaced by range calibration
	inner_deadzone: 5,
	anti_deadzone: 0,
	inner_deadzone2: 5,
	anti_deadzone2: 0,
	// Auto calibration removed - deprecated fields
	// Outer deadzone removed - replaced by range calibration
	joystickCenterX: 0,
	joystickCenterY: 0,
	joystickCenterX2: 0,
	joystickCenterY2: 0,
	// Finetune shape adjustment percentages (default 100% = no adjustment)
	joystickFinetuneShapeXTopPercent1: 100.0,
	joystickFinetuneShapeXBottomPercent1: 100.0,
	joystickFinetuneShapeYLeftPercent1: 100.0,
	joystickFinetuneShapeYRightPercent1: 100.0,
	joystickFinetuneShapeForceCircular1: false,
	joystickFinetuneShapeAmplify1: 0.0,
	joystickFinetuneShapeXTopPercent2: 100.0,
	joystickFinetuneShapeXBottomPercent2: 100.0,
	joystickFinetuneShapeYLeftPercent2: 100.0,
	joystickFinetuneShapeYRightPercent2: 100.0,
	joystickFinetuneShapeForceCircular2: false,
	joystickFinetuneShapeAmplify2: 0.0,
	joystickRangeData1: [],
	joystickRangeData2: [],
	analog_smoothing: 0,
	analog_smoothing2: 0,
	smoothing_factor: 15,
	smoothing_factor2: 15,
};

// errorRateToPercent and percentToErrorRate removed - no longer used after removing forced_circularity

const Analog = ({ values, errors, handleChange, handleCheckbox, setFieldValue }: AddonPropTypes) => {
	const { usedPins } = useContext(AppContext) as any;
	const { t } = useTranslation();
	const availableAnalogPins = ANALOG_PINS.filter(
		(pin) => !usedPins?.includes(pin),
	);

	// EMA sampling state for stick 1
	const [stick1Sampling, setStick1Sampling] = useState(false);
	const [stick1Samples, setStick1Samples] = useState<Array<{ x: number; y: number }>>([]);
	const [showStick1DataModal, setShowStick1DataModal] = useState(false);
	const [showStick1StatsModal, setShowStick1StatsModal] = useState(false);
	const [stick1Stats, setStick1Stats] = useState<{
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
	const stick1SamplingAbortRef = useRef<boolean>(false);
	const stick1LastSampleRef = useRef<{ x: number; y: number } | null>(null);

	// EMA sampling state for stick 2
	const [stick2Sampling, setStick2Sampling] = useState(false);
	const [stick2Samples, setStick2Samples] = useState<Array<{ x: number; y: number }>>([]);
	const [showStick2DataModal, setShowStick2DataModal] = useState(false);
	const [showStick2StatsModal, setShowStick2StatsModal] = useState(false);
	const [stick2Stats, setStick2Stats] = useState<{
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
	const stick2SamplingAbortRef = useRef<boolean>(false);
	const stick2LastSampleRef = useRef<{ x: number; y: number } | null>(null);

	// Start sampling for stick 1 (native sampling speed)
	const handleStartSampling1 = () => {
		if (stick1Sampling) return;
		setStick1Sampling(true);
		setStick1Samples([]);
		stick1SamplingAbortRef.current = false;
		stick1LastSampleRef.current = null; // Reset last sample
		
		const maxSamples = 200;
		
		const fetchData = async (): Promise<void> => {
			if (stick1SamplingAbortRef.current) {
				return;
			}

			try {
				// Add a timeout to avoid hanging forever if the device stops responding
				const controller = new AbortController();
				const timeoutId = setTimeout(() => controller.abort(), 300);

				const res = await fetch('/api/getJoystickCenter', { signal: controller.signal });
				clearTimeout(timeoutId);
				if (res.ok) {
					const data = await res.json();
					if (data.success) {
						// Calculate difference from last sample
						if (stick1LastSampleRef.current !== null) {
							const deltaX = Math.abs(data.x - stick1LastSampleRef.current.x);
							const deltaY = Math.abs(data.y - stick1LastSampleRef.current.y);
							
							let shouldContinue = false;
							setStick1Samples(prev => {
								const newSamples = [...prev, { x: deltaX, y: deltaY }];
								if (newSamples.length >= maxSamples) {
									setStick1Sampling(false);
									setShowStick1DataModal(true);
									return newSamples;
								}
								shouldContinue = true;
								return newSamples;
							});
							// Continue sampling immediately (native speed) with slight delay
							if (shouldContinue && !stick1SamplingAbortRef.current) {
								stick1LastSampleRef.current = { x: data.x, y: data.y };
								// Slight delay to avoid overloading device
								setTimeout(fetchData, 6);
							} else {
								stick1LastSampleRef.current = { x: data.x, y: data.y };
							}
						} else {
							// First sample: just store it, don't add to samples array
							stick1LastSampleRef.current = { x: data.x, y: data.y };
							// Continue sampling with slight delay
							if (!stick1SamplingAbortRef.current) {
								setTimeout(fetchData, 6);
							}
						}
					} else {
						// Continue sampling even if this request failed
						if (!stick1SamplingAbortRef.current) {
							// Add small delay on error to avoid tight loop
							setTimeout(fetchData, 10);
						}
					}
				} else {
					// Continue sampling even if this request failed
					if (!stick1SamplingAbortRef.current) {
						setTimeout(fetchData, 10);
					}
				}
			} catch (error) {
				console.error('Failed to fetch stick 1 data:', error);
				// Do not abort on a single error, keep trying until maxSamples is reached or user aborts
				if (!stick1SamplingAbortRef.current) {
					setTimeout(fetchData, 10);
				}
			}
		};

		// Start sampling immediately
		fetchData();
	};

	// Start sampling for stick 2 (native sampling speed)
	const handleStartSampling2 = () => {
		if (stick2Sampling) return;
		setStick2Sampling(true);
		setStick2Samples([]);
		stick2SamplingAbortRef.current = false;
		stick2LastSampleRef.current = null; // Reset last sample
		
		const maxSamples = 200;
		
		const fetchData = async (): Promise<void> => {
			if (stick2SamplingAbortRef.current) {
				return;
			}

			try {
				// Add a timeout to avoid hanging forever if the device stops responding
				const controller = new AbortController();
				const timeoutId = setTimeout(() => controller.abort(), 300);

				const res = await fetch('/api/getJoystickCenter2', { signal: controller.signal });
				clearTimeout(timeoutId);
				if (res.ok) {
					const data = await res.json();
					if (data.success) {
						// Calculate difference from last sample
						if (stick2LastSampleRef.current !== null) {
							const deltaX = Math.abs(data.x - stick2LastSampleRef.current.x);
							const deltaY = Math.abs(data.y - stick2LastSampleRef.current.y);
							
							let shouldContinue = false;
							setStick2Samples(prev => {
								const newSamples = [...prev, { x: deltaX, y: deltaY }];
								if (newSamples.length >= maxSamples) {
									setStick2Sampling(false);
									setShowStick2DataModal(true);
									return newSamples;
								}
								shouldContinue = true;
								return newSamples;
							});
							// Continue sampling immediately (native speed) with slight delay
							if (shouldContinue && !stick2SamplingAbortRef.current) {
								stick2LastSampleRef.current = { x: data.x, y: data.y };
								// Slight delay to avoid overloading device
								setTimeout(fetchData, 6);
							} else {
								stick2LastSampleRef.current = { x: data.x, y: data.y };
							}
						} else {
							// First sample: just store it, don't add to samples array
							stick2LastSampleRef.current = { x: data.x, y: data.y };
							// Continue sampling with slight delay
							if (!stick2SamplingAbortRef.current) {
								setTimeout(fetchData, 6);
							}
						}
					} else {
						// Continue sampling even if this request failed
						if (!stick2SamplingAbortRef.current) {
							// Add small delay on error to avoid tight loop
							setTimeout(fetchData, 10);
						}
					}
				} else {
					// Continue sampling even if this request failed
					if (!stick2SamplingAbortRef.current) {
						setTimeout(fetchData, 10);
					}
				}
			} catch (error) {
				console.error('Failed to fetch stick 2 data:', error);
				// Do not abort on a single error, keep trying until maxSamples is reached or user aborts
				if (!stick2SamplingAbortRef.current) {
					setTimeout(fetchData, 10);
				}
			}
		};

		// Start sampling immediately
		fetchData();
	};

	// Calculate statistics for stick 1
	const handleCalculateJitterStep1 = () => {
		if (stick1Samples.length === 0) return;

		// Calculate mean
		const meanX = stick1Samples.reduce((sum, s) => sum + s.x, 0) / stick1Samples.length;
		const meanY = stick1Samples.reduce((sum, s) => sum + s.y, 0) / stick1Samples.length;

		// Calculate variance
		const varianceX = stick1Samples.reduce((sum, s) => sum + Math.pow(s.x - meanX, 2), 0) / stick1Samples.length;
		const varianceY = stick1Samples.reduce((sum, s) => sum + Math.pow(s.y - meanY, 2), 0) / stick1Samples.length;

		// Calculate standard deviation
		const stdDevX = Math.sqrt(varianceX);
		const stdDevY = Math.sqrt(varianceY);

		// Calculate mean absolute deviation (average difference)
		const meanDeviationX = stick1Samples.reduce((sum, s) => sum + Math.abs(s.x - meanX), 0) / stick1Samples.length;
		const meanDeviationY = stick1Samples.reduce((sum, s) => sum + Math.abs(s.y - meanY), 0) / stick1Samples.length;

		// Calculate deviation rate (coefficient of variation)
		const deviationRateX = meanX !== 0 ? (stdDevX / meanX) * 100 : 0;
		const deviationRateY = meanY !== 0 ? (stdDevY / meanY) * 100 : 0;

		// Calculate upper and lower deviations
		const upperDeviationX = Math.max(...stick1Samples.map(s => s.x - meanX));
		const lowerDeviationX = Math.min(...stick1Samples.map(s => s.x - meanX));
		const upperDeviationY = Math.max(...stick1Samples.map(s => s.y - meanY));
		const lowerDeviationY = Math.min(...stick1Samples.map(s => s.y - meanY));

		setStick1Stats({
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
		});
		setShowStick1StatsModal(true);
	};

	// Calculate statistics for stick 2
	const handleCalculateJitterStep2 = () => {
		if (stick2Samples.length === 0) return;

		// Calculate mean
		const meanX = stick2Samples.reduce((sum, s) => sum + s.x, 0) / stick2Samples.length;
		const meanY = stick2Samples.reduce((sum, s) => sum + s.y, 0) / stick2Samples.length;

		// Calculate variance
		const varianceX = stick2Samples.reduce((sum, s) => sum + Math.pow(s.x - meanX, 2), 0) / stick2Samples.length;
		const varianceY = stick2Samples.reduce((sum, s) => sum + Math.pow(s.y - meanY, 2), 0) / stick2Samples.length;

		// Calculate standard deviation
		const stdDevX = Math.sqrt(varianceX);
		const stdDevY = Math.sqrt(varianceY);

		// Calculate mean absolute deviation (average difference)
		const meanDeviationX = stick2Samples.reduce((sum, s) => sum + Math.abs(s.x - meanX), 0) / stick2Samples.length;
		const meanDeviationY = stick2Samples.reduce((sum, s) => sum + Math.abs(s.y - meanY), 0) / stick2Samples.length;

		// Calculate deviation rate (coefficient of variation)
		const deviationRateX = meanX !== 0 ? (stdDevX / meanX) * 100 : 0;
		const deviationRateY = meanY !== 0 ? (stdDevY / meanY) * 100 : 0;

		// Calculate upper and lower deviations
		const upperDeviationX = Math.max(...stick2Samples.map(s => s.x - meanX));
		const lowerDeviationX = Math.min(...stick2Samples.map(s => s.x - meanX));
		const upperDeviationY = Math.max(...stick2Samples.map(s => s.y - meanY));
		const lowerDeviationY = Math.min(...stick2Samples.map(s => s.y - meanY));

		setStick2Stats({
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
		});
		setShowStick2StatsModal(true);
	};

	// Cleanup sampling on unmount
	useEffect(() => {
		return () => {
			stick1SamplingAbortRef.current = true;
			stick2SamplingAbortRef.current = true;
		};
	}, []);

	return (
		<Section title={
			<a
				href="https://gp2040-ce.info/add-ons/analog"
				target="_blank"
				className="text-reset text-decoration-none"
			>
				{t('AddonsConfig:analog-header-text')}
			</a>
		}
		>
			<div id="AnalogInputOptions" hidden={!values.AnalogInputEnabled}>
				<div className="alert alert-info" role="alert">
					{t('AddonsConfig:analog-warning')}
				</div>
				<div className="alert alert-success" role="alert">
					{t('AddonsConfig:analog-available-pins-text', {
						pins: availableAnalogPins.join(', '),
					})}
				</div>
					<Tabs
						defaultActiveKey="analog1Config"
						id="analogConfigTabs"
						className="mb-3 pb-0"
						fill
					>
						<Tab
							key="analog1Config"
							eventKey="analog1Config"
							title={t('AddonsConfig:analog-adc-1')}
						>
							<Row className="mb-3">
								<FormSelect
									label={t('AddonsConfig:analog-adc-1-pin-x-label')}
									name="analogAdc1PinX"
									className="form-select-sm"
									groupClassName="col-sm-3 mb-3"
									value={values.analogAdc1PinX}
									error={errors.analogAdc1PinX}
									isInvalid={Boolean(errors.analogAdc1PinX)}
									onChange={handleChange}
								>
									<AnalogPinOptions />
								</FormSelect>
								<FormSelect
									label={t('AddonsConfig:analog-adc-1-pin-y-label')}
									name="analogAdc1PinY"
									className="form-select-sm"
									groupClassName="col-sm-3 mb-3"
									value={values.analogAdc1PinY}
									error={errors.analogAdc1PinY}
									isInvalid={Boolean(errors.analogAdc1PinY)}
									onChange={handleChange}
								>
									<AnalogPinOptions />
								</FormSelect>
							</Row>
								<Row className="mb-3">
									<FormSelect
										label={t('AddonsConfig:analog-adc-1-mode-label')}
										name="analogAdc1Mode"
										className="form-select-sm"
										groupClassName="col-sm-3 mb-3"
										value={values.analogAdc1Mode}
										error={errors.analogAdc1Mode}
										isInvalid={Boolean(errors.analogAdc1Mode)}
										onChange={handleChange}
									>
										{ANALOG_STICK_MODES.map((o, i) => (
											<option key={`button-analogAdc1Mode-option-${i}`} value={o.value}>
												{o.label}
											</option>
										))}
									</FormSelect>
									<FormSelect
										label={t('AddonsConfig:analog-adc-1-invert-label')}
										name="analogAdc1Invert"
										className="form-select-sm"
										groupClassName="col-sm-3 mb-3"
										value={values.analogAdc1Invert}
										error={errors.analogAdc1Invert}
										isInvalid={Boolean(errors.analogAdc1Invert)}
										onChange={handleChange}
									>
										{INVERT_MODES.map((o, i) => (
											<option
												key={`button-analogAdc1Invert-option-${i}`}
												value={o.value}
											>
												{o.label}
											</option>
										))}
									</FormSelect>
								</Row>
								<Row className="mb-3">
									<FormControl
										type="number"
										label={t('AddonsConfig:inner-deadzone-size')}
										name="inner_deadzone"
										className="form-control-sm"
										groupClassName="col-sm-3 mb-3"
										value={values.inner_deadzone}
										error={errors.inner_deadzone}
										isInvalid={Boolean(errors.inner_deadzone)}
										onChange={handleChange}
										min={0}
										max={100}
									/>
									<FormControl
										type="number"
										label={t('AddonsConfig:inner-anti-deadzone-size')}
										name="anti_deadzone"
										className="form-control-sm"
										groupClassName="col-sm-3 mb-3"
										value={values.anti_deadzone}
										error={errors.anti_deadzone}
										isInvalid={Boolean(errors.anti_deadzone)}
										onChange={handleChange}
										min={0}
										max={100}
									/>
								</Row>
								<Row className="mb-3">
									<FormCheck
										label={t('AddonsConfig:analog-smoothing')}
										type="switch"
										id="Analog_smoothing"
										className="col-sm-3 ms-3"
										isInvalid={false}
										checked={Boolean(values.analog_smoothing)}
										onChange={(e) => {
											handleCheckbox('analog_smoothing');
											handleChange(e);
										}}
									/>
								</Row>
								<Row className="mb-3" hidden={!values.analog_smoothing}>
									<div className="col-sm-12 mb-2">
										<div className="d-flex justify-content-between align-items-center mb-1">
											<Form.Label className="mb-0">{t('AddonsConfig:smoothing-factor')}: {values.smoothing_factor}</Form.Label>
											<span className="text-muted small">{t('AddonsConfig:smoothing-factor-desc')}</span>
										</div>
										<Form.Range
											name="smoothing_factor"
											min={0}
											max={100}
											step={1}
											value={values.smoothing_factor}
											onChange={handleChange}
										/>
									</div>
								</Row>
								<Row className="mb-3" hidden={!values.analog_smoothing}>
									<div className="col-sm-12">
										<button
											type="button"
											className="btn btn-primary me-2"
											disabled={stick1Sampling}
											onClick={handleStartSampling1}
										>
											{stick1Sampling 
												? `${t('AddonsConfig:ema-sampling')} (${stick1Samples.length}/200)` 
												: t('AddonsConfig:ema-start-sampling')}
										</button>
										<button
											type="button"
											className="btn btn-secondary"
											disabled={stick1Samples.length === 0}
											onClick={handleCalculateJitterStep1}
										>
											{t('AddonsConfig:ema-calculate-jitter-step')}
										</button>
									</div>
								</Row>
								{/* forced_circularity and error_rate UI removed - replaced by range calibration */}
						</Tab>
						<Tab
							key="analog2Config"
							eventKey="analog2Config"
							title={t('AddonsConfig:analog-adc-2')}
						>
							<Row className="mb-3">
								<FormSelect
									label={t('AddonsConfig:analog-adc-2-pin-x-label')}
									name="analogAdc2PinX"
									className="form-select-sm"
									groupClassName="col-sm-3 mb-3"
									value={values.analogAdc2PinX}
									error={errors.analogAdc2PinX}
									isInvalid={Boolean(errors.analogAdc2PinX)}
									onChange={handleChange}
								>
									<AnalogPinOptions />
								</FormSelect>
								<FormSelect
									label={t('AddonsConfig:analog-adc-2-pin-y-label')}
									name="analogAdc2PinY"
									className="form-select-sm"
									groupClassName="col-sm-3 mb-3"
									value={values.analogAdc2PinY}
									error={errors.analogAdc2PinY}
									isInvalid={Boolean(errors.analogAdc2PinY)}
									onChange={handleChange}
								>
									<AnalogPinOptions />
								</FormSelect>
							</Row>
								<Row className="mb-3">
									<FormSelect
										label={t('AddonsConfig:analog-adc-2-mode-label')}
										name="analogAdc2Mode"
										className="form-select-sm"
										groupClassName="col-sm-3 mb-3"
										value={values.analogAdc2Mode}
										error={errors.analogAdc2Mode}
										isInvalid={Boolean(errors.analogAdc2Mode)}
										onChange={handleChange}
									>
										{ANALOG_STICK_MODES.map((o, i) => (
											<option key={`button-analogAdc2Mode-option-${i}`} value={o.value}>
												{o.label}
											</option>
										))}
									</FormSelect>
									<FormSelect
										label={t('AddonsConfig:analog-adc-2-invert-label')}
										name="analogAdc2Invert"
										className="form-select-sm"
										groupClassName="col-sm-3 mb-3"
										value={values.analogAdc2Invert}
										error={errors.analogAdc2Invert}
										isInvalid={Boolean(errors.analogAdc2Invert)}
										onChange={handleChange}
									>
										{INVERT_MODES.map((o, i) => (
											<option
												key={`button-analogAdc2Invert-option-${i}`}
												value={o.value}
											>
												{o.label}
											</option>
										))}
									</FormSelect>
								</Row>
								<Row className="mb-3">
									<FormControl
										type="number"
										label={t('AddonsConfig:inner-deadzone-size')}
										name="inner_deadzone2"
										className="form-control-sm"
										groupClassName="col-sm-3 mb-3"
										value={values.inner_deadzone2}
										error={errors.inner_deadzone2}
										isInvalid={Boolean(errors.inner_deadzone2)}
										onChange={handleChange}
										min={0}
										max={100}
									/>
									<FormControl
										type="number"
										label={t('AddonsConfig:inner-anti-deadzone-size')}
										name="anti_deadzone2"
										className="form-control-sm"
										groupClassName="col-sm-3 mb-3"
										value={values.anti_deadzone2}
										error={errors.anti_deadzone2}
										isInvalid={Boolean(errors.anti_deadzone2)}
										onChange={handleChange}
										min={0}
										max={100}
									/>
								</Row>
								<Row className="mb-3">
									<FormCheck
										label={t('AddonsConfig:analog-smoothing')}
										type="switch"
										id="Analog_smoothing2"
										className="col-sm-3 ms-3"
										isInvalid={false}
										checked={Boolean(values.analog_smoothing2)}
										onChange={(e) => {
											handleCheckbox('analog_smoothing2');
											handleChange(e);
										}}
									/>
								</Row>
								<Row className="mb-3" hidden={!values.analog_smoothing2}>
									<div className="col-sm-12 mb-2">
										<div className="d-flex justify-content-between align-items-center mb-1">
											<Form.Label className="mb-0">{t('AddonsConfig:smoothing-factor')}: {values.smoothing_factor2}</Form.Label>
											<span className="text-muted small">{t('AddonsConfig:smoothing-factor-desc')}</span>
										</div>
										<Form.Range
											name="smoothing_factor2"
											min={0}
											max={100}
											step={1}
											value={values.smoothing_factor2}
											onChange={handleChange}
										/>
									</div>
								</Row>
								<Row className="mb-3" hidden={!values.analog_smoothing2}>
									<div className="col-sm-12">
										<button
											type="button"
											className="btn btn-primary me-2"
											disabled={stick2Sampling}
											onClick={handleStartSampling2}
										>
											{stick2Sampling 
												? `${t('AddonsConfig:ema-sampling')} (${stick2Samples.length}/200)` 
												: t('AddonsConfig:ema-start-sampling')}
										</button>
										<button
											type="button"
											className="btn btn-secondary"
											disabled={stick2Samples.length === 0}
											onClick={handleCalculateJitterStep2}
										>
											{t('AddonsConfig:ema-calculate-jitter-step')}
										</button>
									</div>
								</Row>
								{/* forced_circularity2 and error_rate2 UI removed - replaced by range calibration */}
						</Tab>
					</Tabs>
			</div>
			<FormCheck
				label={t('Common:switch-enabled')}
				type="switch"
				id="AnalogInputButton"
				reverse
				isInvalid={false}
				checked={Boolean(values.AnalogInputEnabled)}
				onChange={(e) => {
					handleCheckbox('AnalogInputEnabled');
					handleChange(e);
				}}
			/>

			{/* Stick 1 Data Modal */}
			<Modal show={showStick1DataModal} onHide={() => setShowStick1DataModal(false)} size="lg">
				<Modal.Header closeButton>
					<Modal.Title>{t('AddonsConfig:ema-sampling-data')} - {t('AddonsConfig:analog-adc-1')}</Modal.Title>
				</Modal.Header>
				<Modal.Body style={{ maxHeight: '70vh', overflowY: 'auto' }}>
					<Table striped bordered hover size="sm">
						<thead>
							<tr>
								<th>#</th>
								<th>X (ADC)</th>
								<th>Y (ADC)</th>
							</tr>
						</thead>
						<tbody>
							{stick1Samples.map((sample, index) => (
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
					<Button variant="secondary" onClick={() => setShowStick1DataModal(false)}>
						Close
					</Button>
				</Modal.Footer>
			</Modal>

			{/* Stick 1 Statistics Modal */}
			<Modal show={showStick1StatsModal} onHide={() => setShowStick1StatsModal(false)}>
				<Modal.Header closeButton>
					<Modal.Title>{t('AddonsConfig:ema-jitter-statistics')} - {t('AddonsConfig:analog-adc-1')}</Modal.Title>
				</Modal.Header>
				<Modal.Body>
					{stick1Stats && (
						<div className="mb-3">
							<p><strong>{t('AddonsConfig:ema-sample-count')}: {stick1Samples.length}</strong></p>
						</div>
					)}
					{stick1Stats && (
						<Table striped bordered>
							<thead>
								<tr>
									<th>{t('AddonsConfig:ema-statistic')}</th>
									<th>X</th>
									<th>Y</th>
								</tr>
							</thead>
							<tbody>
								<tr>
									<td><strong>{t('AddonsConfig:ema-mean')}</strong></td>
									<td>{stick1Stats.meanX.toFixed(2)}</td>
									<td>{stick1Stats.meanY.toFixed(2)}</td>
								</tr>
								<tr>
									<td><strong>{t('AddonsConfig:ema-variance')}</strong></td>
									<td>{stick1Stats.varianceX.toFixed(2)}</td>
									<td>{stick1Stats.varianceY.toFixed(2)}</td>
								</tr>
								<tr>
									<td><strong>{t('AddonsConfig:ema-mean-deviation')}</strong></td>
									<td>{stick1Stats.meanDeviationX.toFixed(2)}</td>
									<td>{stick1Stats.meanDeviationY.toFixed(2)}</td>
								</tr>
								<tr>
									<td><strong>{t('AddonsConfig:ema-deviation-rate')}</strong></td>
									<td>{stick1Stats.deviationRateX.toFixed(2)}%</td>
									<td>{stick1Stats.deviationRateY.toFixed(2)}%</td>
								</tr>
								<tr>
									<td><strong>{t('AddonsConfig:ema-upper-deviation')}</strong></td>
									<td>{stick1Stats.upperDeviationX.toFixed(2)}</td>
									<td>{stick1Stats.upperDeviationY.toFixed(2)}</td>
								</tr>
								<tr>
									<td><strong>{t('AddonsConfig:ema-lower-deviation')}</strong></td>
									<td>{stick1Stats.lowerDeviationX.toFixed(2)}</td>
									<td>{stick1Stats.lowerDeviationY.toFixed(2)}</td>
								</tr>
							</tbody>
						</Table>
					)}
				</Modal.Body>
				<Modal.Footer>
					<Button variant="secondary" onClick={() => setShowStick1StatsModal(false)}>
						{t('Common:button-close')}
					</Button>
				</Modal.Footer>
			</Modal>

			{/* Stick 2 Data Modal */}
			<Modal show={showStick2DataModal} onHide={() => setShowStick2DataModal(false)} size="lg">
				<Modal.Header closeButton>
					<Modal.Title>{t('AddonsConfig:ema-sampling-data')} - {t('AddonsConfig:analog-adc-2')}</Modal.Title>
				</Modal.Header>
				<Modal.Body style={{ maxHeight: '70vh', overflowY: 'auto' }}>
					<Table striped bordered hover size="sm">
						<thead>
							<tr>
								<th>#</th>
								<th>X (ADC)</th>
								<th>Y (ADC)</th>
							</tr>
						</thead>
						<tbody>
							{stick2Samples.map((sample, index) => (
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
					<Button variant="secondary" onClick={() => setShowStick2DataModal(false)}>
						{t('Common:button-close') || 'Close'}
					</Button>
				</Modal.Footer>
			</Modal>

			{/* Stick 2 Statistics Modal */}
			<Modal show={showStick2StatsModal} onHide={() => setShowStick2StatsModal(false)}>
				<Modal.Header closeButton>
					<Modal.Title>{t('AddonsConfig:ema-jitter-statistics')} - {t('AddonsConfig:analog-adc-2')}</Modal.Title>
				</Modal.Header>
				<Modal.Body>
					{stick2Stats && (
						<div className="mb-3">
							<p><strong>{t('AddonsConfig:ema-sample-count')}: {stick2Samples.length}</strong></p>
						</div>
					)}
					{stick2Stats && (
						<Table striped bordered>
							<thead>
								<tr>
									<th>{t('AddonsConfig:ema-statistic')}</th>
									<th>X</th>
									<th>Y</th>
								</tr>
							</thead>
							<tbody>
								<tr>
									<td><strong>{t('AddonsConfig:ema-mean')}</strong></td>
									<td>{stick2Stats.meanX.toFixed(2)}</td>
									<td>{stick2Stats.meanY.toFixed(2)}</td>
								</tr>
								<tr>
									<td><strong>{t('AddonsConfig:ema-variance')}</strong></td>
									<td>{stick2Stats.varianceX.toFixed(2)}</td>
									<td>{stick2Stats.varianceY.toFixed(2)}</td>
								</tr>
								<tr>
									<td><strong>{t('AddonsConfig:ema-mean-deviation')}</strong></td>
									<td>{stick2Stats.meanDeviationX.toFixed(2)}</td>
									<td>{stick2Stats.meanDeviationY.toFixed(2)}</td>
								</tr>
								<tr>
									<td><strong>{t('AddonsConfig:ema-deviation-rate')}</strong></td>
									<td>{stick2Stats.deviationRateX.toFixed(2)}%</td>
									<td>{stick2Stats.deviationRateY.toFixed(2)}%</td>
								</tr>
								<tr>
									<td><strong>{t('AddonsConfig:ema-upper-deviation')}</strong></td>
									<td>{stick2Stats.upperDeviationX.toFixed(2)}</td>
									<td>{stick2Stats.upperDeviationY.toFixed(2)}</td>
								</tr>
								<tr>
									<td><strong>{t('AddonsConfig:ema-lower-deviation')}</strong></td>
									<td>{stick2Stats.lowerDeviationX.toFixed(2)}</td>
									<td>{stick2Stats.lowerDeviationY.toFixed(2)}</td>
								</tr>
							</tbody>
						</Table>
					)}
				</Modal.Body>
				<Modal.Footer>
					<Button variant="secondary" onClick={() => setShowStick2StatsModal(false)}>
						{t('Common:button-close')}
					</Button>
				</Modal.Footer>
			</Modal>
		</Section>
	);
};

export default Analog;
