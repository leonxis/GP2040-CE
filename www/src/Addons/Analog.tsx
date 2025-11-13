import { useContext, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { FormCheck, Row, Tab, Tabs, Form } from 'react-bootstrap';
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

const ANALOG_ERROR_RATES = [
	{ label: '0%', value: 1000 },
	{ label: '1%', value: 990 },
	{ label: '2%', value: 979 },
	{ label: '3%', value: 969 },
	{ label: '4%', value: 958 },
	{ label: '5%', value: 946 },
	{ label: '6%', value: 934 },
	{ label: '7%', value: 922 },
	{ label: '8%', value: 911 },
	{ label: '9%', value: 900 },
	{ label: '10%', value: 890 },
	{ label: '11%', value: 876 },
	{ label: '12%', value: 863 },
	{ label: '13%', value: 848 },
	{ label: '14%', value: 834 },
	{ label: '15%', value: 821 },
];

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

	forced_circularity: yup
		.number()
		.label('Force Circularity')
		.validateRangeWhenValue('AnalogInputEnabled', 0, 1),
	forced_circularity2: yup
		.number()
		.label('Force Circularity')
		.validateRangeWhenValue('AnalogInputEnabled', 0, 1),
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
	outer_deadzone: yup
		.number()
		.label('Outer Deadzone Size (%)')
		.validateRangeWhenValue('AnalogInputEnabled', 0, 100),
	outer_deadzone2: yup
		.number()
		.label('Outer Deadzone Size (%)')
		.validateRangeWhenValue('AnalogInputEnabled', 0, 100),
	auto_calibrate: yup
		.number()
		.label('Auto Calibration')
		.validateRangeWhenValue('AnalogInputEnabled', 0, 1),
	auto_calibrate2: yup
		.number()
		.label('Auto Calibration')
		.validateRangeWhenValue('AnalogInputEnabled', 0, 1),
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
	smoothing_alpha_max: yup
		.number()
		.label('Dynamic Smoothing Alpha Max')
		.validateRangeWhenValue('AnalogInputEnabled', 0, 100),
	smoothing_delta_max: yup
		.number()
		.label('Dynamic Smoothing Delta Max')
		.validateRangeWhenValue('AnalogInputEnabled', 0.1, 1),
	smoothing_alpha_max2: yup
		.number()
		.label('Dynamic Smoothing Alpha Max 2')
		.validateRangeWhenValue('AnalogInputEnabled', 0, 100),
	smoothing_delta_max2: yup
		.number()
		.label('Dynamic Smoothing Delta Max 2')
		.validateRangeWhenValue('AnalogInputEnabled', 0.1, 1),
	analog_error: yup
		.number()
		.label('Error Rate')
		.validateSelectionWhenValue('AnalogInputEnabled', ANALOG_ERROR_RATES),
	analog_error2: yup
		.number()
		.label('Error Rate 2')
		.validateSelectionWhenValue('AnalogInputEnabled', ANALOG_ERROR_RATES),
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
	forced_circularity: 0,
	forced_circularity2: 0,
	inner_deadzone: 5,
	anti_deadzone: 0,
	inner_deadzone2: 5,
	anti_deadzone2: 0,
	outer_deadzone: 95,
	outer_deadzone2: 95,
	auto_calibrate: 0,
	auto_calibrate2: 0,
	joystickCenterX: 0,
	joystickCenterY: 0,
	joystickCenterX2: 0,
	joystickCenterY2: 0,
	analog_smoothing: 0,
	analog_smoothing2: 0,
	smoothing_factor: 50,
	smoothing_factor2: 50,
	smoothing_alpha_max: 95,
	smoothing_delta_max: 0.2,
	smoothing_alpha_max2: 95,
	smoothing_delta_max2: 0.2,
	analog_error: 1,
	analog_error2: 1,
};

// Helper function to convert error rate value to percentage (0-15)
const errorRateToPercent = (value: number): number => {
	const index = ANALOG_ERROR_RATES.findIndex(rate => rate.value === value);
	return index >= 0 ? index : 0;
};

// Helper function to convert percentage (0-15) to error rate value
const percentToErrorRate = (percent: number): number => {
	const index = Math.round(Math.max(0, Math.min(15, percent)));
	return ANALOG_ERROR_RATES[index]?.value || ANALOG_ERROR_RATES[0].value;
};

const Analog = ({ values, errors, handleChange, handleCheckbox, setFieldValue }: AddonPropTypes) => {
	const { usedPins } = useContext(AppContext);
	const { t } = useTranslation();
	const availableAnalogPins = ANALOG_PINS.filter(
		(pin) => !usedPins?.includes(pin),
	);

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
									<FormControl
										type="number"
										label={t('AddonsConfig:outer-deadzone-size')}
										name="outer_deadzone"
										className="form-control-sm"
										groupClassName="col-sm-3 mb-3"
										value={values.outer_deadzone}
										error={errors.outer_deadzone}
										isInvalid={Boolean(errors.outer_deadzone)}
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
											<Form.Label className="mb-0">{t('AddonsConfig:smoothing-delta-max')}: {values.smoothing_delta_max}%</Form.Label>
											<span className="text-muted small">{t('AddonsConfig:smoothing-delta-max-desc')}</span>
										</div>
										<Form.Range
											name="smoothing_delta_max"
											min={0.1}
											max={1}
											step={0.1}
											value={values.smoothing_delta_max}
											onChange={handleChange}
										/>
									</div>
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
									<div className="col-sm-12 mb-2">
										<div className="d-flex justify-content-between align-items-center mb-1">
											<Form.Label className="mb-0">{t('AddonsConfig:smoothing-alpha-max')}: {values.smoothing_alpha_max}</Form.Label>
											<span className="text-muted small">{t('AddonsConfig:smoothing-alpha-max-desc')}</span>
										</div>
										<Form.Range
											name="smoothing_alpha_max"
											min={0}
											max={100}
											step={1}
											value={values.smoothing_alpha_max}
											onChange={handleChange}
										/>
									</div>
								</Row>
								<Row className="mb-3" hidden={!values.analog_smoothing}>
									<div className="col-sm-12">
										<p className="text-muted small mb-0">{t('AddonsConfig:smoothing-dynamic-desc')}</p>
									</div>
								</Row>
								<Row className="mb-3">
									<FormCheck
										label={t('AddonsConfig:analog-force-circularity')}
										type="switch"
										id="Forced_circularity"
										className="col-sm-3 ms-3"
										isInvalid={false}
										checked={Boolean(values.forced_circularity)}
										onChange={(e) => {
											handleCheckbox('forced_circularity');
											handleChange(e);
										}}
									/>
									<div className="col-sm-9 mb-3" hidden={!values.forced_circularity}>
										<div className="d-flex justify-content-between align-items-center mb-1">
											<Form.Label className="mb-0">{t('AddonsConfig:analog-error-label')}: {errorRateToPercent(values.analog_error)}%</Form.Label>
										</div>
										<Form.Range
											name="analog_error"
											min={0}
											max={15}
											step={1}
											value={errorRateToPercent(values.analog_error)}
											onChange={(e) => {
												const percent = parseInt((e.target as HTMLInputElement).value);
												setFieldValue('analog_error', percentToErrorRate(percent));
											}}
										/>
									</div>
								</Row>
								<Row className="mb-3">
									<FormCheck
										label={Boolean(values.auto_calibrate) ? t('AddonsConfig:analog-auto-calibrate') : t('AddonsConfig:analog-manual-calibrate')}
										type="switch"
										id="Auto_calibrate"
										className="col-sm-3 ms-3"
										isInvalid={false}
										checked={Boolean(values.auto_calibrate)}
										onChange={(e) => {
											handleCheckbox('auto_calibrate');
											handleChange(e);
										}}
									/>
									<div className="col-sm-9 d-flex align-items-center">
										<button
											type="button"
											className="btn btn-sm btn-outline-secondary"
											hidden={Boolean(values.auto_calibrate)}
											onClick={async () => {
											try {
												// Multi-step calibration process
												const steps = [
													{ direction: t('AddonsConfig:analog-calibration-direction-top-left'), position: 'top-left' },
													{ direction: t('AddonsConfig:analog-calibration-direction-top-right'), position: 'top-right' },
													{ direction: t('AddonsConfig:analog-calibration-direction-bottom-left'), position: 'bottom-left' },
													{ direction: t('AddonsConfig:analog-calibration-direction-bottom-right'), position: 'bottom-right' }
												];
												
												const calibrationValues = [];
												
												for (let i = 0; i < steps.length; i++) {
													const step = steps[i];
													const stepNumber = i + 1;
													
													
													// Show confirmation dialog
													const userConfirmed = confirm(
														t('AddonsConfig:analog-calibration-step-title', { step: stepNumber }) + '\n\n' +
														t('AddonsConfig:analog-calibration-step-instruction', { stick: '1', direction: step.direction }) + '\n\n' +
														t('AddonsConfig:analog-calibration-step-confirm', { stick: '1', step: stepNumber })
													);
													
													if (!userConfirmed) {
														alert(t('AddonsConfig:analog-calibration-cancelled'));
														return;
													}
													
													
													// Read current center value
													console.log(`Fetching joystick 1 center for step ${stepNumber}...`);
												const res = await fetch('/api/getJoystickCenter');
												console.log('Response status:', res.status);
													
													if (!res.ok) {
														throw new Error(`HTTP error! status: ${res.status}`);
													}
													
												const data = await res.json();
												console.log('Response data:', data);
												
													if (!data.success || data.error) {
														alert(t('AddonsConfig:analog-calibration-failed', { error: data.error || 'Unknown error' }));
													console.error('API Error:', data.error);
													return;
												}
												
													calibrationValues.push({
														step: stepNumber,
														direction: step.direction,
														x: data.x || 0,
														y: data.y || 0
													});
													
													console.log(`Step ${stepNumber} completed:`, calibrationValues[i]);
												}
												

												// Calculate center value from four points
												const avgX = Math.round(calibrationValues.reduce((sum, val) => sum + val.x, 0) / 4);
												const avgY = Math.round(calibrationValues.reduce((sum, val) => sum + val.y, 0) / 4);
												
												// Update joystick 1 center values
												setFieldValue('joystickCenterX', avgX);
												setFieldValue('joystickCenterY', avgY);
												
												console.log('Calibration completed:', {
													values: calibrationValues,
													finalCenter: { x: avgX, y: avgY }
												});
												
												
												// Show success message
												alert(
													t('AddonsConfig:analog-calibration-success-stick-1') + '\n\n' +
													t('AddonsConfig:analog-calibration-data') + '\n' +
													`• ${t('AddonsConfig:analog-calibration-direction-top-left')}: X=${calibrationValues[0].x}, Y=${calibrationValues[0].y}\n` +
													`• ${t('AddonsConfig:analog-calibration-direction-top-right')}: X=${calibrationValues[1].x}, Y=${calibrationValues[1].y}\n` +
													`• ${t('AddonsConfig:analog-calibration-direction-bottom-left')}: X=${calibrationValues[2].x}, Y=${calibrationValues[2].y}\n` +
													`• ${t('AddonsConfig:analog-calibration-direction-bottom-right')}: X=${calibrationValues[3].x}, Y=${calibrationValues[3].y}\n\n` +
													t('AddonsConfig:analog-calibration-final-center', { x: avgX, y: avgY }) + '\n\n' +
													t('AddonsConfig:analog-calibration-save-notice')
												);
											} catch (err) {
												console.error('Failed to calibrate joystick 1', err);
												alert(t('AddonsConfig:analog-calibration-failed', { error: err instanceof Error ? err.message : String(err) }));
											}
										}}
									>
										{t('AddonsConfig:analog-calibrate-stick-1-button')}
									</button>
									<div className="ms-3 small text-muted" hidden={Boolean(values.auto_calibrate)}>
										{`Center: X=${values.joystickCenterX}, Y=${values.joystickCenterY}`}
									</div>
									</div>
								</Row>
								{Boolean(values.auto_calibrate) && (
									<div className="alert alert-info mt-2 mb-3">
										<small>
											<strong>{t('AddonsConfig:analog-auto-calibration-enabled-stick-1')}：</strong> {t('AddonsConfig:analog-calibration-auto-mode-instruction', { stick: '1' })}
										</small>
									</div>
								)}
								{!Boolean(values.auto_calibrate) && (
									<div className="alert alert-warning mt-2 mb-3">
										<small>
											<strong>{t('AddonsConfig:analog-manual-calibration-mode-stick-1')}：</strong> 
											<br />• {t('AddonsConfig:analog-calibration-manual-mode-instruction-1')}
											<br />• {t('AddonsConfig:analog-calibration-manual-mode-instruction-2')}
											<br />• {t('AddonsConfig:analog-calibration-manual-mode-instruction-3')}
											<br />• {t('AddonsConfig:analog-calibration-manual-mode-instruction-4')}
										</small>
									</div>
								)}
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
									<FormControl
										type="number"
										label={t('AddonsConfig:outer-deadzone-size')}
										name="outer_deadzone2"
										className="form-control-sm"
										groupClassName="col-sm-3 mb-3"
										value={values.outer_deadzone2}
										error={errors.outer_deadzone2}
										isInvalid={Boolean(errors.outer_deadzone2)}
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
											<Form.Label className="mb-0">{t('AddonsConfig:smoothing-delta-max')}: {values.smoothing_delta_max2}%</Form.Label>
											<span className="text-muted small">{t('AddonsConfig:smoothing-delta-max-desc')}</span>
										</div>
										<Form.Range
											name="smoothing_delta_max2"
											min={0.1}
											max={1}
											step={0.1}
											value={values.smoothing_delta_max2}
											onChange={handleChange}
										/>
									</div>
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
									<div className="col-sm-12 mb-2">
										<div className="d-flex justify-content-between align-items-center mb-1">
											<Form.Label className="mb-0">{t('AddonsConfig:smoothing-alpha-max')}: {values.smoothing_alpha_max2}</Form.Label>
											<span className="text-muted small">{t('AddonsConfig:smoothing-alpha-max-desc')}</span>
										</div>
										<Form.Range
											name="smoothing_alpha_max2"
											min={0}
											max={100}
											step={1}
											value={values.smoothing_alpha_max2}
											onChange={handleChange}
										/>
									</div>
								</Row>
								<Row className="mb-3" hidden={!values.analog_smoothing2}>
									<div className="col-sm-12">
										<p className="text-muted small mb-0">{t('AddonsConfig:smoothing-dynamic-desc')}</p>
									</div>
								</Row>
								<Row className="mb-3">
									<FormCheck
										label={t('AddonsConfig:analog-force-circularity')}
										type="switch"
										id="Forced_circularity2"
										className="col-sm-3 ms-3"
										isInvalid={false}
										checked={Boolean(values.forced_circularity2)}
										onChange={(e) => {
											handleCheckbox('forced_circularity2');
											handleChange(e);
										}}
									/>
									<div className="col-sm-9 mb-3" hidden={!values.forced_circularity2}>
										<div className="d-flex justify-content-between align-items-center mb-1">
											<Form.Label className="mb-0">{t('AddonsConfig:analog-error-label')}: {errorRateToPercent(values.analog_error2)}%</Form.Label>
										</div>
										<Form.Range
											name="analog_error2"
											min={0}
											max={15}
											step={1}
											value={errorRateToPercent(values.analog_error2)}
											onChange={(e) => {
												const percent = parseInt((e.target as HTMLInputElement).value);
												setFieldValue('analog_error2', percentToErrorRate(percent));
											}}
										/>
									</div>
								</Row>
								<Row className="mb-3">
									<FormCheck
										label={Boolean(values.auto_calibrate2) ? t('AddonsConfig:analog-auto-calibrate') : t('AddonsConfig:analog-manual-calibrate')}
										type="switch"
										id="Auto_calibrate2"
										className="col-sm-3 ms-3"
										isInvalid={false}
										checked={Boolean(values.auto_calibrate2)}
										onChange={(e) => {
											handleCheckbox('auto_calibrate2');
											handleChange(e);
										}}
									/>
									<div className="col-sm-9 d-flex align-items-center">
										<button
											type="button"
											className="btn btn-sm btn-outline-secondary"
											hidden={Boolean(values.auto_calibrate2)}
											onClick={async () => {
											try {
												// Multi-step calibration process
												const steps = [
													{ direction: t('AddonsConfig:analog-calibration-direction-top-left'), position: 'top-left' },
													{ direction: t('AddonsConfig:analog-calibration-direction-top-right'), position: 'top-right' },
													{ direction: t('AddonsConfig:analog-calibration-direction-bottom-left'), position: 'bottom-left' },
													{ direction: t('AddonsConfig:analog-calibration-direction-bottom-right'), position: 'bottom-right' }
												];
												
												const calibrationValues = [];
												
												for (let i = 0; i < steps.length; i++) {
													const step = steps[i];
													const stepNumber = i + 1;
													
													
													// Show confirmation dialog
													const userConfirmed = confirm(
														t('AddonsConfig:analog-calibration-step-title', { step: stepNumber }) + '\n\n' +
														t('AddonsConfig:analog-calibration-step-instruction', { stick: '2', direction: step.direction }) + '\n\n' +
														t('AddonsConfig:analog-calibration-step-confirm', { stick: '2', step: stepNumber })
													);
													
													if (!userConfirmed) {
														alert(t('AddonsConfig:analog-calibration-cancelled'));
														return;
													}
													
													
													// Read current center value
													console.log(`Fetching joystick 2 center for step ${stepNumber}...`);
													const res = await fetch('/api/getJoystickCenter2');
													console.log('Response status:', res.status);
													
													if (!res.ok) {
														throw new Error(`HTTP error! status: ${res.status}`);
													}
													
													const data = await res.json();
													console.log('Response data:', data);
													
													if (!data.success || data.error) {
														alert(t('AddonsConfig:analog-calibration-failed', { error: data.error || 'Unknown error' }));
														console.error('API Error:', data.error);
														return;
													}
													
													calibrationValues.push({
														step: stepNumber,
														direction: step.direction,
														x: data.x || 0,
														y: data.y || 0
													});
													
													console.log(`Step ${stepNumber} completed:`, calibrationValues[i]);
												}
												

												// Calculate center value from four points
												const avgX = Math.round(calibrationValues.reduce((sum, val) => sum + val.x, 0) / 4);
												const avgY = Math.round(calibrationValues.reduce((sum, val) => sum + val.y, 0) / 4);
												
												// Update joystick 2 center values
												setFieldValue('joystickCenterX2', avgX);
												setFieldValue('joystickCenterY2', avgY);
												
												console.log('Calibration completed:', {
													values: calibrationValues,
													finalCenter: { x: avgX, y: avgY }
												});
												
												
												// Show success message
												alert(
													t('AddonsConfig:analog-calibration-success-stick-2') + '\n\n' +
													t('AddonsConfig:analog-calibration-data') + '\n' +
													`• ${t('AddonsConfig:analog-calibration-direction-top-left')}: X=${calibrationValues[0].x}, Y=${calibrationValues[0].y}\n` +
													`• ${t('AddonsConfig:analog-calibration-direction-top-right')}: X=${calibrationValues[1].x}, Y=${calibrationValues[1].y}\n` +
													`• ${t('AddonsConfig:analog-calibration-direction-bottom-left')}: X=${calibrationValues[2].x}, Y=${calibrationValues[2].y}\n` +
													`• ${t('AddonsConfig:analog-calibration-direction-bottom-right')}: X=${calibrationValues[3].x}, Y=${calibrationValues[3].y}\n\n` +
													t('AddonsConfig:analog-calibration-final-center', { x: avgX, y: avgY }) + '\n\n' +
													t('AddonsConfig:analog-calibration-save-notice')
												);
											} catch (err) {
												console.error('Failed to calibrate joystick 2', err);
												alert(t('AddonsConfig:analog-calibration-failed', { error: err instanceof Error ? err.message : String(err) }));
											}
										}}
									>
										{t('AddonsConfig:analog-calibrate-stick-2-button')}
									</button>
									<div className="ms-3 small text-muted" hidden={Boolean(values.auto_calibrate2)}>
										{`Center: X=${values.joystickCenterX2}, Y=${values.joystickCenterY2}`}
									</div>
									</div>
								</Row>
								{Boolean(values.auto_calibrate2) && (
									<div className="alert alert-info mt-2 mb-3">
										<small>
											<strong>{t('AddonsConfig:analog-auto-calibration-enabled-stick-2')}：</strong> {t('AddonsConfig:analog-calibration-auto-mode-instruction', { stick: '2' })}
										</small>
									</div>
								)}
								{!Boolean(values.auto_calibrate2) && (
									<div className="alert alert-warning mt-2 mb-3">
										<small>
											<strong>{t('AddonsConfig:analog-manual-calibration-mode-stick-2')}：</strong> 
											<br />• {t('AddonsConfig:analog-calibration-manual-mode-instruction-1')}
											<br />• {t('AddonsConfig:analog-calibration-manual-mode-instruction-2')}
											<br />• {t('AddonsConfig:analog-calibration-manual-mode-instruction-3')}
											<br />• {t('AddonsConfig:analog-calibration-manual-mode-instruction-4')}
										</small>
									</div>
								)}
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
		</Section>
	);
};

export default Analog;
