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
	smoothing_alpha_max: yup
		.number()
		.label('Dynamic Smoothing Alpha Max')
		.validateRangeWhenValue('AnalogInputEnabled', 0, 100),
	smoothing_delta_max: yup
		.number()
		.label('Dynamic Smoothing Delta Max')
		.validateRangeWhenValue('AnalogInputEnabled', 0, 5),
	smoothing_alpha_max2: yup
		.number()
		.label('Dynamic Smoothing Alpha Max 2')
		.validateRangeWhenValue('AnalogInputEnabled', 0, 100),
	smoothing_delta_max2: yup
		.number()
		.label('Dynamic Smoothing Delta Max 2')
		.validateRangeWhenValue('AnalogInputEnabled', 0, 5),
	// analog_error and analog_error2 validation simplified - no longer used for forced_circularity
	analog_error: yup
		.number()
		.label('Error Rate')
		.validateRangeWhenValue('AnalogInputEnabled', 0, 1000),
	analog_error2: yup
		.number()
		.label('Error Rate 2')
		.validateRangeWhenValue('AnalogInputEnabled', 0, 1000),
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
	joystickRangeData1: [],
	joystickRangeData2: [],
	analog_smoothing: 0,
	analog_smoothing2: 0,
	smoothing_factor: 15,
	smoothing_factor2: 15,
	smoothing_alpha_max: 95,
	smoothing_delta_max: 1.5,
	smoothing_alpha_max2: 95,
	smoothing_delta_max2: 1.5,
	analog_error: 1,
	analog_error2: 1,
};

// errorRateToPercent and percentToErrorRate removed - no longer used after removing forced_circularity

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
											min={0}
											max={5}
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
											<Form.Label className="mb-0">{t('AddonsConfig:smoothing-delta-max')}: {values.smoothing_delta_max2}%</Form.Label>
											<span className="text-muted small">{t('AddonsConfig:smoothing-delta-max-desc')}</span>
										</div>
										<Form.Range
											name="smoothing_delta_max2"
											min={0}
											max={5}
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
		</Section>
	);
};

export default Analog;
