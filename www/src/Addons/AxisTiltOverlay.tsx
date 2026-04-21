import { useCallback, useContext, useMemo } from 'react';
import { useTranslation } from 'react-i18next';
import { Form, FormCheck, Row } from 'react-bootstrap';
import { omit } from 'lodash';
import * as yup from 'yup';

import Section from '../Components/Section';
import CustomSelect from '../Components/CustomSelect';
import FormControl from '../Components/FormControl';
import { AppContext } from '../Contexts/AppContext';
import { BUTTON_MASKS, getButtonLabels } from '../Data/Buttons';
import { AddonPropTypes } from '../Pages/AddonsConfigPage';

/** Minimal AppContext shape for button label mode (full context is JS). */
type AppContextShape = {
	buttonLabels?: { buttonLabelType?: string; swapTpShareLabels?: boolean };
};

const BUTTON_OPTIONS = [{ label: 'None', value: 0 }, ...BUTTON_MASKS];

/** 与背键映射等一致：react-select + 手柄标签；value 为按键位掩码（非 GpioAction）。 */
type AxisTiltTriggerMaskOption = { label: string; value: number };

const TRIGGER_MASK_SELECT_OPTIONS: AxisTiltTriggerMaskOption[] = [
	{ label: 'NONE', value: 0 },
	...BUTTON_MASKS.map(({ label, value }) => ({
		label: `BUTTON_PRESS_${label}`,
		value,
	})),
];

export const axisTiltOverlayScheme = {
	AxisTiltOverlayInputEnabled: yup
		.number()
		.required()
		.label('Axis Tilt Overlay Input Enabled'),
	axisTiltOverlayLeftYTriggerButtonMask: yup
		.number()
		.label('Axis Tilt Overlay Left Y Trigger Button')
		.validateSelectionWhenValue(
			'AxisTiltOverlayInputEnabled',
			BUTTON_OPTIONS,
		),
	axisTiltOverlayRightYTriggerButtonMask: yup
		.number()
		.label('Axis Tilt Overlay Right Y Trigger Button')
		.validateSelectionWhenValue(
			'AxisTiltOverlayInputEnabled',
			BUTTON_OPTIONS,
		),
	axisTiltOverlayLeftYPercent1: yup
		.number()
		.label('Axis Tilt Overlay Left Y Percent 1')
		.validateRangeWhenValue('AxisTiltOverlayInputEnabled', -100, 100),
	axisTiltOverlayLeftYPercent2: yup
		.number()
		.label('Axis Tilt Overlay Left Y Percent 2')
		.validateRangeWhenValue('AxisTiltOverlayInputEnabled', -100, 100),
	axisTiltOverlayRightYPercent1: yup
		.number()
		.label('Axis Tilt Overlay Right Y Percent 1')
		.validateRangeWhenValue('AxisTiltOverlayInputEnabled', -100, 100),
	axisTiltOverlayRightYPercent2: yup
		.number()
		.label('Axis Tilt Overlay Right Y Percent 2')
		.validateRangeWhenValue('AxisTiltOverlayInputEnabled', -100, 100),
	axisTiltOverlayLeftYActivePreset: yup
		.number()
		.label('Axis Tilt Overlay Left Y Active Preset')
		.validateRangeWhenValue('AxisTiltOverlayInputEnabled', 1, 2),
	axisTiltOverlayRightYActivePreset: yup
		.number()
		.label('Axis Tilt Overlay Right Y Active Preset')
		.validateRangeWhenValue('AxisTiltOverlayInputEnabled', 1, 2),
};

export const axisTiltOverlayState = {
	AxisTiltOverlayInputEnabled: 0,
	axisTiltOverlayLeftYTriggerButtonMask: 0,
	axisTiltOverlayRightYTriggerButtonMask: 0,
	axisTiltOverlayLeftYPercent1: 0,
	axisTiltOverlayLeftYPercent2: 0,
	axisTiltOverlayRightYPercent1: 0,
	axisTiltOverlayRightYPercent2: 0,
	axisTiltOverlayLeftYActivePreset: 1,
	axisTiltOverlayRightYActivePreset: 1,
};

function percentFieldValue(v: unknown): number | string {
	if (v === undefined || v === null || v === '') {
		return 0;
	}
	return v as number | string;
}

const AxisTiltOverlay = ({
	values,
	errors,
	handleChange,
	handleCheckbox,
	setFieldValue,
}: AddonPropTypes) => {
	const { t } = useTranslation();
	const appContext = useContext(AppContext) as AppContextShape | null;

	const buttonNames = useMemo(() => {
		const defaultButtons = getButtonLabels('gp2040', false);
		if (!appContext?.buttonLabels) {
			return omit(defaultButtons, ['label', 'value']) as Record<string, string>;
		}
		const { buttonLabelType, swapTpShareLabels } = appContext.buttonLabels;
		return omit(getButtonLabels(buttonLabelType, swapTpShareLabels), [
			'label',
			'value',
		]) as Record<string, string>;
	}, [appContext]);

	const getMaskOptionLabel = useCallback(
		(option: AxisTiltTriggerMaskOption) => {
			if (option.value === 0) {
				return t('Proto:GpioAction.NONE');
			}
			const labelKey = option.label.replace('BUTTON_PRESS_', '');
			return (
				buttonNames[labelKey] || t(`Proto:GpioAction.${option.label}`)
			);
		},
		[buttonNames, t],
	);

	return (
		<Section title={t('AddonsConfig:axis-tilt-overlay-header-text')}>
			<div
				id="AxisTiltOverlayOptions"
				hidden={!values.AxisTiltOverlayInputEnabled}
			>
				<Row className="mb-3">
					<Form.Group className="col-sm-4 mb-3">
						<Form.Label htmlFor="axisTiltOverlayLeftYTriggerButtonMask">
							{t('AddonsConfig:axis-tilt-overlay-left-y-trigger-button-label')}
						</Form.Label>
						<CustomSelect<AxisTiltTriggerMaskOption, false>
							inputId="axisTiltOverlayLeftYTriggerButtonMask"
							isClearable={false}
							isSearchable
							options={TRIGGER_MASK_SELECT_OPTIONS}
							value={
								TRIGGER_MASK_SELECT_OPTIONS.find(
									(o) =>
										o.value ===
										(values.axisTiltOverlayLeftYTriggerButtonMask ?? 0),
								) ?? TRIGGER_MASK_SELECT_OPTIONS[0]
							}
							getOptionLabel={getMaskOptionLabel}
							onChange={(opt) => {
								void setFieldValue(
									'axisTiltOverlayLeftYTriggerButtonMask',
									opt?.value ?? 0,
								);
							}}
						/>
						<Form.Control.Feedback
							type="invalid"
							style={{
								display: errors.axisTiltOverlayLeftYTriggerButtonMask
									? 'block'
									: undefined,
							}}
						>
							{errors.axisTiltOverlayLeftYTriggerButtonMask}
						</Form.Control.Feedback>
					</Form.Group>
					<FormControl
						type="number"
						label={t('AddonsConfig:axis-tilt-overlay-left-y-percent-1-label')}
						name="axisTiltOverlayLeftYPercent1"
						className="form-select-sm"
						groupClassName="col-sm-4 mb-3"
						value={percentFieldValue(values.axisTiltOverlayLeftYPercent1)}
						error={errors.axisTiltOverlayLeftYPercent1}
						isInvalid={Boolean(errors.axisTiltOverlayLeftYPercent1)}
						onChange={handleChange}
						min={-100}
						max={100}
						step={0.1}
					/>
					<FormControl
						type="number"
						label={t('AddonsConfig:axis-tilt-overlay-left-y-percent-2-label')}
						name="axisTiltOverlayLeftYPercent2"
						className="form-select-sm"
						groupClassName="col-sm-4 mb-3"
						value={percentFieldValue(values.axisTiltOverlayLeftYPercent2)}
						error={errors.axisTiltOverlayLeftYPercent2}
						isInvalid={Boolean(errors.axisTiltOverlayLeftYPercent2)}
						onChange={handleChange}
						min={-100}
						max={100}
						step={0.1}
					/>
				</Row>
				<Row className="mb-3">
					<Form.Group className="col-sm-4 mb-3">
						<Form.Label htmlFor="axisTiltOverlayRightYTriggerButtonMask">
							{t('AddonsConfig:axis-tilt-overlay-right-y-trigger-button-label')}
						</Form.Label>
						<CustomSelect<AxisTiltTriggerMaskOption, false>
							inputId="axisTiltOverlayRightYTriggerButtonMask"
							isClearable={false}
							isSearchable
							options={TRIGGER_MASK_SELECT_OPTIONS}
							value={
								TRIGGER_MASK_SELECT_OPTIONS.find(
									(o) =>
										o.value ===
										(values.axisTiltOverlayRightYTriggerButtonMask ?? 0),
								) ?? TRIGGER_MASK_SELECT_OPTIONS[0]
							}
							getOptionLabel={getMaskOptionLabel}
							onChange={(opt) => {
								void setFieldValue(
									'axisTiltOverlayRightYTriggerButtonMask',
									opt?.value ?? 0,
								);
							}}
						/>
						<Form.Control.Feedback
							type="invalid"
							style={{
								display: errors.axisTiltOverlayRightYTriggerButtonMask
									? 'block'
									: undefined,
							}}
						>
							{errors.axisTiltOverlayRightYTriggerButtonMask}
						</Form.Control.Feedback>
					</Form.Group>
					<FormControl
						type="number"
						label={t('AddonsConfig:axis-tilt-overlay-right-y-percent-1-label')}
						name="axisTiltOverlayRightYPercent1"
						className="form-select-sm"
						groupClassName="col-sm-4 mb-3"
						value={percentFieldValue(values.axisTiltOverlayRightYPercent1)}
						error={errors.axisTiltOverlayRightYPercent1}
						isInvalid={Boolean(errors.axisTiltOverlayRightYPercent1)}
						onChange={handleChange}
						min={-100}
						max={100}
						step={0.1}
					/>
					<FormControl
						type="number"
						label={t('AddonsConfig:axis-tilt-overlay-right-y-percent-2-label')}
						name="axisTiltOverlayRightYPercent2"
						className="form-select-sm"
						groupClassName="col-sm-4 mb-3"
						value={percentFieldValue(values.axisTiltOverlayRightYPercent2)}
						error={errors.axisTiltOverlayRightYPercent2}
						isInvalid={Boolean(errors.axisTiltOverlayRightYPercent2)}
						onChange={handleChange}
						min={-100}
						max={100}
						step={0.1}
					/>
				</Row>
			</div>
			<FormCheck
				label={t('Common:switch-enabled')}
				type="switch"
				id="AxisTiltOverlayInputButton"
				reverse
				isInvalid={false}
				checked={Boolean(values.AxisTiltOverlayInputEnabled)}
				onChange={(e) => {
					handleCheckbox('AxisTiltOverlayInputEnabled');
					handleChange(e);
				}}
			/>
		</Section>
	);
};

export default AxisTiltOverlay;
