import { useContext, useState, useEffect, useRef } from 'react';
import { useTranslation } from 'react-i18next';
import { FormCheck, Row, Tab, Tabs, Form, Modal, Table, Button } from 'react-bootstrap';
import * as yup from 'yup';

import Section from '../Components/Section';
import FormSelect from '../Components/FormSelect';
import { ANALOG_PINS } from '../Data/Buttons';
import { BUTTON_ACTIONS } from '../Data/Pins';
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
		.validateRangeWhenValue('AnalogInputEnabled', 0, 20),
	anti_deadzone: yup
		.number()
		.label('Inner Anti-Deadzone Size (%)')
		.validateRangeWhenValue('AnalogInputEnabled', 0, 20),
	fixed_anti_deadzone: yup
		.boolean()
		.label('Fixed Anti-Deadzone Mode'),
	inner_deadzone2: yup
		.number()
		.label('Inner Deadzone Size (%)')
		.validateRangeWhenValue('AnalogInputEnabled', 0, 20),
	anti_deadzone2: yup
		.number()
		.label('Inner Anti-Deadzone Size (%)')
		.validateRangeWhenValue('AnalogInputEnabled', 0, 20),
	fixed_anti_deadzone2: yup
		.boolean()
		.label('Fixed Anti-Deadzone Mode 2'),
	// Auto calibration removed - deprecated fields
	// Outer deadzone removed - replaced by range calibration
	// EMA smoothing removed - deprecated fields
	joystickCenterX: yup
		.number()
		.label('Joystick Center X')
		.validateRangeWhenValue('AnalogInputEnabled', 0, 65535),
	joystickCenterY: yup
		.number()
		.label('Joystick Center Y')
		.validateRangeWhenValue('AnalogInputEnabled', 0, 65535),
	joystickCenterX2: yup
		.number()
		.label('Joystick Center X2')
		.validateRangeWhenValue('AnalogInputEnabled', 0, 65535),
	joystickCenterY2: yup
		.number()
		.label('Joystick Center Y2')
		.validateRangeWhenValue('AnalogInputEnabled', 0, 65535),
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
	joystickCurvePoints1: yup
		.array()
		.of(yup.object().shape({
			x: yup.number().min(0).max(1),
			y: yup.number().min(0).max(1)
		}))
		.label('Joystick Curve Points 1'),
	joystickCurvePoints2: yup
		.array()
		.of(yup.object().shape({
			x: yup.number().min(0).max(1),
			y: yup.number().min(0).max(1)
		}))
		.label('Joystick Curve Points 2'),
	joystickCurveEnabled: yup.boolean().label('Joystick Curve Enabled'),
	// Curve point presets (array of presets, max 4 presets, each with name and up to 3 points)
	joystickCurvePresets: yup
		.array()
		.of(yup.object().shape({
			name: yup.string().label('Preset Name'),
			points: yup
				.array()
				.of(yup.object().shape({
					x: yup.number().min(0).max(1),
					y: yup.number().min(0).max(1)
				}))
				.max(3)
				.label('Preset Points')
		}))
		.max(4)
		.label('Joystick Curve Presets'),
	joystickTravelButtonAction: yup.number().label('Joystick Travel Button Action'),
	joystickTravelButtonCustomDpadMask: yup.number().min(0).label('Joystick Travel Button Custom Dpad Mask'),
	joystickTravelButtonCustomButtonMask: yup.number().min(0).label('Joystick Travel Button Custom Button Mask'),
	joystickTravelButtonThreshold: yup
		.number()
		.label('Joystick Travel Button Threshold (%)')
		.validateRangeWhenValue('AnalogInputEnabled', 0, 99),
	joystickTravelButtonAction2: yup.number().label('Joystick Travel Button Action 2'),
	joystickTravelButtonCustomDpadMask2: yup.number().min(0).label('Joystick Travel Button Custom Dpad Mask 2'),
	joystickTravelButtonCustomButtonMask2: yup.number().min(0).label('Joystick Travel Button Custom Button Mask 2'),
	joystickTravelButtonThreshold2: yup
		.number()
		.label('Joystick Travel Button Threshold 2 (%)')
		.validateRangeWhenValue('AnalogInputEnabled', 0, 99),
	joystickJitterFilter1: yup
		.number()
		.min(0)
		.max(4096)
		.label('Joystick Jitter Filter 1'),
	joystickJitterFilter2: yup
		.number()
		.min(0)
		.max(4096)
		.label('Joystick Jitter Filter 2'),
	ads8332JitterBoostIntervalMs1: yup
		.number()
		.min(0)
		.max(100)
		.label('ADS8332 Jitter Boost Interval Ms 1'),
	ads8332JitterBoostIntervalMs2: yup
		.number()
		.min(0)
		.max(100)
		.label('ADS8332 Jitter Boost Interval Ms 2'),
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
	fixed_anti_deadzone: false,
	inner_deadzone2: 5,
	anti_deadzone2: 0,
	fixed_anti_deadzone2: false,
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
	joystickCurvePoints1: [],
	joystickCurvePoints2: [],
	joystickCurveEnabled: false,
	// Curve point presets (default: empty array)
	joystickCurvePresets: [],
	joystickTravelButtonAction: BUTTON_ACTIONS.NONE,
	joystickTravelButtonCustomDpadMask: 0,
	joystickTravelButtonCustomButtonMask: 0,
	joystickTravelButtonThreshold: 0,
	joystickTravelButtonAction2: BUTTON_ACTIONS.NONE,
	joystickTravelButtonCustomDpadMask2: 0,
	joystickTravelButtonCustomButtonMask2: 0,
	joystickTravelButtonThreshold2: 0,
	joystickJitterFilter1: 0,
	joystickJitterFilter2: 0,
	ads8332JitterBoostIntervalMs1: 0,
	ads8332JitterBoostIntervalMs2: 0,
};

// errorRateToPercent and percentToErrorRate removed - no longer used after removing forced_circularity

const Analog = ({ values, errors, handleChange, handleCheckbox, setFieldValue }: AddonPropTypes) => {
	const { usedPins } = useContext(AppContext) as any;
	const { t } = useTranslation();
	const availableAnalogPins = ANALOG_PINS.filter(
		(pin) => !usedPins?.includes(pin),
	);


	return (
		<Section title={
			<a
				href="https://gp2040-ce.info/add-ons/analog"
				target="_blank"
				className="text-reset text-decoration-none" rel="noreferrer"
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
