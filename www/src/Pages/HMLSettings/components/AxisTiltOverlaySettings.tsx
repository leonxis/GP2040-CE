import { useCallback, useContext, useMemo } from 'react';
import { useTranslation } from 'react-i18next';
import { Button, Form, FormCheck, OverlayTrigger, Tooltip } from 'react-bootstrap';
import { useFormikContext } from 'formik';
import { MultiValue, SingleValue } from 'react-select';
import { omit } from 'lodash';
import * as yup from 'yup';

import InfoCircle from '../../../Icons/InfoCircle';
import Section from '../../../Components/Section';
import CustomSelect from '../../../Components/CustomSelect';
import { AppContext } from '../../../Contexts/AppContext';
import { BUTTON_MASKS, getButtonLabels } from '../../../Data/Buttons';
import { BUTTON_ACTIONS, PinActionValues } from '../../../Data/Pins';
import type { AddonPropTypes } from './CalibrationSettings';
import {
	OptionType,
	groupedMappingOptions,
	isDisabled,
	keyboardKeyOptions,
	mappingOptions,
	mouseKeyOptions,
} from './ActionMappingOptions';

type AppContextShape = {
	buttonLabels?: { buttonLabelType?: string; swapTpShareLabels?: boolean };
};

const BUTTON_OPTIONS = [{ label: 'None', value: 0 }, ...BUTTON_MASKS];
const joystickTravelGroupedOptions = groupedMappingOptions;
type TriggerMaskOption = { label: string; value: number };
const TRIGGER_MASK_SELECT_OPTIONS: TriggerMaskOption[] = [
	{ label: 'NONE', value: 0 },
	...BUTTON_MASKS.map(({ label, value }) => ({ label: `BUTTON_PRESS_${label}`, value })),
];
const yupEx = yup as any;

const getPayloadFromSelected = (
	selected: MultiValue<OptionType> | SingleValue<OptionType>,
) => {
	if (!selected || (Array.isArray(selected) && !selected.length)) {
		return { action: BUTTON_ACTIONS.NONE, customButtonMask: 0, customDpadMask: 0 };
	}
	if (Array.isArray(selected) && selected.length > 1) {
		const hasKeyboard = selected.some((opt) => opt.type === 'keyboard');
		const hasAction = selected.some((opt) => opt.type === 'action');
		if (hasKeyboard || hasAction) {
			const last = selected[selected.length - 1];
			return { action: last.value, customButtonMask: 0, customDpadMask: 0 };
		}
		return selected.reduce(
			(acc, option) => ({
				...acc,
				customButtonMask:
					option.type === 'customButtonMask'
						? acc.customButtonMask ^ option.customButtonMask
						: acc.customButtonMask,
				customDpadMask:
					option.type === 'customDpadMask'
						? acc.customDpadMask ^ option.customDpadMask
						: acc.customDpadMask,
			}),
			{ action: BUTTON_ACTIONS.CUSTOM_BUTTON_COMBO, customButtonMask: 0, customDpadMask: 0 },
		);
	}
	const single = Array.isArray(selected) ? selected[0] : selected;
	return { action: single.value, customButtonMask: 0, customDpadMask: 0 };
};

const getMultiValue = (mappingData: { action: PinActionValues; customButtonMask: number; customDpadMask: number }) => {
	if (mappingData.action === BUTTON_ACTIONS.NONE) return;
	if (isDisabled(mappingData.action)) {
		const actionKey = Object.entries(BUTTON_ACTIONS).find(([, value]) => value === mappingData.action)?.[0] || 'NONE';
		return [{ label: actionKey, value: mappingData.action, type: 'action', customButtonMask: mappingData.customButtonMask, customDpadMask: mappingData.customDpadMask }];
	}
	const keyboardOption = keyboardKeyOptions.find((opt) => opt.value === mappingData.action);
	if (keyboardOption) return [keyboardOption];
	const mouseOption = mouseKeyOptions.find((opt) => opt.value === mappingData.action);
	if (mouseOption) return [mouseOption];
	return mappingData.action === BUTTON_ACTIONS.CUSTOM_BUTTON_COMBO
		? mappingOptions.filter(
			({ type, customButtonMask, customDpadMask }) =>
				(mappingData.customButtonMask & customButtonMask && type === 'customButtonMask') ||
				(mappingData.customDpadMask & customDpadMask && type === 'customDpadMask'),
		)
		: mappingOptions.filter((option) => option.value === mappingData.action);
};

export const axisTiltOverlaySettingsScheme = {
	axisTiltOverlayPressEnabled: yupEx.number().label('Axis Tilt Overlay Press Enabled'),
	axisTiltOverlayRightYTriggerButtonMask: yupEx.number().label('Axis Tilt Overlay Right Y Trigger Button').validateSelectionWhenValue('axisTiltOverlayPressEnabled', BUTTON_OPTIONS),
	axisTiltOverlayRightYPercent1: yupEx.number().label('Right stick press preset 1').validateRangeWhenValue('axisTiltOverlayPressEnabled', -100, 100),
	axisTiltOverlayRightYPercent2: yupEx.number().label('Right stick press preset 2').validateRangeWhenValue('axisTiltOverlayPressEnabled', -100, 100),
	axisTiltOverlayRightYPercent3: yupEx.number().label('Right stick press preset 3').validateRangeWhenValue('axisTiltOverlayPressEnabled', -100, 100),
	axisTiltOverlayRightYActivePreset: yupEx.number().label('Axis Tilt Overlay Right Y Active Preset').validateRangeWhenValue('axisTiltOverlayPressEnabled', 0, 3),
	axisTiltOverlayRcGainEnabled: yupEx.number().label('RC Gain Enabled'),
	axisTiltOverlayRcGainAlwaysOn: yupEx.number().label('RC Gain Always On'),
	axisTiltOverlayRcGainRadialAttenuationEnabled: yupEx.number().label('RC radial attenuation'),
	axisTiltOverlayRcGainTriggerButtonMask: yupEx.number().label('RC Gain Trigger Button').validateSelectionWhenValue('axisTiltOverlayRcGainEnabled', BUTTON_OPTIONS),
	axisTiltOverlayRcGainReserved1: yupEx.number().validateRangeWhenValue('axisTiltOverlayRcGainEnabled', 0, 100),
	axisTiltOverlayRcGainReserved2: yupEx.number().validateRangeWhenValue('axisTiltOverlayRcGainEnabled', 0, 100),
	axisTiltOverlayRcGainReserved3: yupEx.number().validateRangeWhenValue('axisTiltOverlayRcGainEnabled', 0, 100),
};

export const axisTiltOverlaySettingsState = {
	axisTiltOverlayPressEnabled: 0,
	axisTiltOverlayRightYTriggerButtonMask: 0,
	axisTiltOverlayRightYPercent1: 0,
	axisTiltOverlayRightYPercent2: 0,
	axisTiltOverlayRightYPercent3: 0,
	axisTiltOverlayRightYActivePreset: 0,
	axisTiltOverlayRcGainEnabled: 0,
	axisTiltOverlayRcGainAlwaysOn: 0,
	axisTiltOverlayRcGainRadialAttenuationEnabled: 0,
	axisTiltOverlayRcGainTriggerButtonMask: 0,
	axisTiltOverlayRcGainReserved1: 0,
	axisTiltOverlayRcGainReserved2: 0,
	axisTiltOverlayRcGainReserved3: 0,
};

const clampPercent = (value: number) => Math.min(100, Math.max(-100, value));
const clampRcPercent = (value: number) => Math.min(100, Math.max(0, value));

export default function AxisTiltOverlaySettings({ values, errors, setFieldValue, saveMessage = '', onSaveClick }: AddonPropTypes) {
	const { t } = useTranslation();
	const { handleSubmit } = useFormikContext();
	const appContext = useContext(AppContext) as AppContextShape | null;
	const saveOk = saveMessage === t('Common:saved-success-message');
	const buttonNames = useMemo(() => {
		const defaultButtons = getButtonLabels('gp2040', false);
		if (!appContext?.buttonLabels) return omit(defaultButtons, ['label', 'value']) as Record<string, string>;
		const { buttonLabelType, swapTpShareLabels } = appContext.buttonLabels;
		return omit(getButtonLabels(buttonLabelType, swapTpShareLabels), ['label', 'value']) as Record<string, string>;
	}, [appContext]);
	const getMaskOptionLabel = useCallback((option: TriggerMaskOption) => {
		if (option.value === 0) return t('Proto:GpioAction.NONE');
		const labelKey = option.label.replace('BUTTON_PRESS_', '');
		return buttonNames[labelKey] || t(`Proto:GpioAction.${option.label}`);
	}, [buttonNames, t]);
	const joystickTravelButtonMappingLeft = {
		action: (values?.joystickTravelButtonAction ?? BUTTON_ACTIONS.NONE) as PinActionValues,
		customButtonMask: values?.joystickTravelButtonCustomButtonMask ?? 0,
		customDpadMask: values?.joystickTravelButtonCustomDpadMask ?? 0,
	};
	const joystickTravelButtonMappingRight = {
		action: (values?.joystickTravelButtonAction2 ?? BUTTON_ACTIONS.NONE) as PinActionValues,
		customButtonMask: values?.joystickTravelButtonCustomButtonMask2 ?? 0,
		customDpadMask: values?.joystickTravelButtonCustomDpadMask2 ?? 0,
	};
	const joystickTravelButtonThresholdLeft = Math.max(0, Math.min(99, values?.joystickTravelButtonThreshold ?? 0));
	const joystickTravelButtonThresholdRight = Math.max(0, Math.min(99, values?.joystickTravelButtonThreshold2 ?? 0));
	const getOptionLabel = (option: OptionType) => {
		if (option.type === 'keyboard') {
			const keyName = option.label?.replace('KEYBOARD_KEY_', '');
			return keyName === 'ALT_F4' ? 'KB: Alt+F4' : `KB: ${keyName || option.label}`;
		}
		if (option.type === 'mouse') return t(`Proto:GpioAction.${option.label}`);
		const labelKey = option.label?.split('BUTTON_PRESS_')?.pop();
		return (labelKey && buttonNames[labelKey]) || t(`Proto:GpioAction.${option.label}`);
	};
	const handleJoystickTravelMappingChange = (
		selected: MultiValue<OptionType> | SingleValue<OptionType>,
		stick: 'left' | 'right',
	) => {
		const payload = getPayloadFromSelected(selected);
		if (stick === 'left') {
			setFieldValue('joystickTravelButtonAction', payload.action);
			setFieldValue('joystickTravelButtonCustomButtonMask', payload.customButtonMask);
			setFieldValue('joystickTravelButtonCustomDpadMask', payload.customDpadMask);
			return;
		}
		setFieldValue('joystickTravelButtonAction2', payload.action);
		setFieldValue('joystickTravelButtonCustomButtonMask2', payload.customButtonMask);
		setFieldValue('joystickTravelButtonCustomDpadMask2', payload.customDpadMask);
	};
	/** Full grid cell width, left-aligned — keeps columns 1–4 row edges aligned */
	const columnCellStyle = { width: '100%', minWidth: 0 };
	const row1Style = { display: 'flex', alignItems: 'center', minHeight: '42px', width: '100%' };
	const sliderBlockStyle = { marginTop: '10px' };

	return (
		<Section title={t('AddonsConfig:axis-tilt-overlay-header-text')}>
			<div id="AxisTiltOverlayOptions">
				<div style={{ display: 'grid', gridTemplateColumns: 'repeat(4, minmax(0, 1fr))', gap: '12px 16px', alignItems: 'start', width: 'min(100%, 1120px)', margin: '0 auto' }}>
					<div style={columnCellStyle}>
						<div style={row1Style}>
							<FormCheck className="mb-0" type="switch" id="axisTiltOverlayPressEnabled" label={t('AddonsConfig:axis-tilt-overlay-press-enabled-label')} checked={Boolean(values.axisTiltOverlayPressEnabled)} onChange={(e) => setFieldValue('axisTiltOverlayPressEnabled', e.target.checked ? 1 : 0)} />
						</div>
						<Form.Label htmlFor="axisTiltOverlayRightYTriggerButtonMask" className="mt-3 mb-1">{t('AddonsConfig:axis-tilt-overlay-right-y-trigger-button-label')}</Form.Label>
						<CustomSelect<TriggerMaskOption, false> inputId="axisTiltOverlayRightYTriggerButtonMask" isClearable={false} isSearchable options={TRIGGER_MASK_SELECT_OPTIONS} value={TRIGGER_MASK_SELECT_OPTIONS.find((o) => o.value === (values.axisTiltOverlayRightYTriggerButtonMask ?? 0)) ?? TRIGGER_MASK_SELECT_OPTIONS[0]} getOptionLabel={getMaskOptionLabel} onChange={(opt) => void setFieldValue('axisTiltOverlayRightYTriggerButtonMask', opt?.value ?? 0)} />
						{errors.axisTiltOverlayRightYTriggerButtonMask && <div className="text-danger small mt-1">{errors.axisTiltOverlayRightYTriggerButtonMask}</div>}
						{([['axisTiltOverlayRightYPercent1', 1], ['axisTiltOverlayRightYPercent2', 2], ['axisTiltOverlayRightYPercent3', 3]] as const).map(([pctName, idx]) => (
							<div key={pctName} style={sliderBlockStyle}>
								<Form.Label className="mb-1">{t(`AddonsConfig:axis-tilt-overlay-right-y-percent-${idx}-label`)} {Number(values[pctName] ?? 0).toFixed(1)}%</Form.Label>
								<Form.Range min={-100} max={100} step={0.1} value={Number(values[pctName] ?? 0)} onChange={(e) => setFieldValue(pctName, clampPercent(parseFloat(e.target.value)))} />
							</div>
						))}
					</div>
					<div style={columnCellStyle}>
						<div style={{ ...row1Style, gap: '10px', flexWrap: 'nowrap', minWidth: 0 }}>
							<FormCheck className="mb-0 flex-shrink-0" type="switch" id="axisTiltOverlayRcGainEnabled" label={t('AddonsConfig:axis-tilt-overlay-rc-gain-enabled-label')} checked={Boolean(values.axisTiltOverlayRcGainEnabled)} onChange={(e) => setFieldValue('axisTiltOverlayRcGainEnabled', e.target.checked ? 1 : 0)} />
							<FormCheck className="mb-0 flex-shrink-0" type="switch" id="axisTiltOverlayRcGainAlwaysOn" label={t(Boolean(values.axisTiltOverlayRcGainAlwaysOn) ? 'AddonsConfig:axis-tilt-overlay-rc-gain-mode-always-on-label' : 'AddonsConfig:axis-tilt-overlay-rc-gain-mode-trigger-label')} checked={Boolean(values.axisTiltOverlayRcGainAlwaysOn)} onChange={(e) => setFieldValue('axisTiltOverlayRcGainAlwaysOn', e.target.checked ? 1 : 0)} />
							<FormCheck className="mb-0 flex-shrink-0" type="switch" id="axisTiltOverlayRcGainRadialAttenuationEnabled" label={t('AddonsConfig:axis-tilt-overlay-rc-gain-radial-attenuation-label')} checked={Boolean(values.axisTiltOverlayRcGainRadialAttenuationEnabled)} onChange={(e) => setFieldValue('axisTiltOverlayRcGainRadialAttenuationEnabled', e.target.checked ? 1 : 0)} />
						</div>
						<Form.Label htmlFor="axisTiltOverlayRcGainTriggerButtonMask" className="mt-3 mb-1">{t('AddonsConfig:axis-tilt-overlay-rc-gain-trigger-button-label')}</Form.Label>
						<CustomSelect<TriggerMaskOption, false> inputId="axisTiltOverlayRcGainTriggerButtonMask" isClearable={false} isSearchable options={TRIGGER_MASK_SELECT_OPTIONS} value={TRIGGER_MASK_SELECT_OPTIONS.find((o) => o.value === (values.axisTiltOverlayRcGainTriggerButtonMask ?? 0)) ?? TRIGGER_MASK_SELECT_OPTIONS[0]} getOptionLabel={getMaskOptionLabel} onChange={(opt) => void setFieldValue('axisTiltOverlayRcGainTriggerButtonMask', opt?.value ?? 0)} />
						{([['axisTiltOverlayRcGainReserved1', 1], ['axisTiltOverlayRcGainReserved2', 2], ['axisTiltOverlayRcGainReserved3', 3]] as const).map(([rcName, idx]) => (
							<div key={rcName} style={sliderBlockStyle}>
								<Form.Label className="mb-1">{t(`AddonsConfig:axis-tilt-overlay-rc-gain-reserved-${idx}-label`)} {Number(values[rcName] ?? 0).toFixed(1)}%</Form.Label>
								<Form.Range min={0} max={100} step={0.1} value={Number(values[rcName] ?? 0)} onChange={(e) => setFieldValue(rcName, clampRcPercent(parseFloat(e.target.value)))} />
							</div>
						))}
					</div>
					<div style={columnCellStyle}>
						<div style={{ ...row1Style, justifyContent: 'flex-end', gap: '8px' }}>
							<span className="mb-0 text-nowrap">{t('CalibrationSettings:hml-anti-deadzone-mode-label')}</span>
							<span>{values?.fixed_anti_deadzone ? t('CalibrationSettings:hml-fixed') : t('CalibrationSettings:hml-linear')}</span>
							<Form.Check type="switch" id="fixed-anti-deadzone-1" label="" checked={values?.fixed_anti_deadzone || false} onChange={(e) => setFieldValue('fixed_anti_deadzone', e.target.checked)} />
							<OverlayTrigger
								placement="top"
								overlay={(
									<Tooltip id="fixed-anti-deadzone-1-tip">
										{t('CalibrationSettings:hml-anti-deadzone-overlay-mode-tooltip')}
									</Tooltip>
								)}
							>
								<span style={{ display: 'inline-flex', cursor: 'help' }}><InfoCircle /></span>
							</OverlayTrigger>
						</div>
						<Form.Label className="mt-3 mb-1">{t('CalibrationSettings:hml-joystick-travel-mapping-key-left')}</Form.Label>
						<CustomSelect isClearable isMulti={!isDisabled(joystickTravelButtonMappingLeft.action) && !keyboardKeyOptions.some((opt) => opt.value === joystickTravelButtonMappingLeft.action) && !mouseKeyOptions.some((opt) => opt.value === joystickTravelButtonMappingLeft.action) && !mappingOptions.some((opt) => opt.value === joystickTravelButtonMappingLeft.action && opt.type === 'action')} options={joystickTravelGroupedOptions} isDisabled={isDisabled(joystickTravelButtonMappingLeft.action)} getOptionLabel={getOptionLabel} onChange={(selected: MultiValue<OptionType> | SingleValue<OptionType>) => handleJoystickTravelMappingChange(selected, 'left')} value={getMultiValue(joystickTravelButtonMappingLeft)} />
						<div style={sliderBlockStyle}>
							<Form.Label className="mb-1">{t('CalibrationSettings:hml-joystick-travel-button-threshold-left', { pct: joystickTravelButtonThresholdLeft })}</Form.Label>
							<Form.Range min={0} max={99} step={1} value={joystickTravelButtonThresholdLeft} onChange={(e) => setFieldValue('joystickTravelButtonThreshold', parseInt(e.target.value, 10))} />
						</div>
						<div style={sliderBlockStyle}>
							<Form.Label className="mb-1">{t('CalibrationSettings:hml-inner-deadzone-left', { pct: (values?.inner_deadzone || 0).toFixed(1) })}</Form.Label>
							<Form.Range min={0} max={20} step={1} value={values?.inner_deadzone || 0} onChange={(e) => setFieldValue('inner_deadzone', parseFloat(e.target.value))} />
						</div>
						<div style={sliderBlockStyle}>
							<Form.Label className="mb-1">{t('CalibrationSettings:hml-anti-deadzone-left', { pct: (values?.anti_deadzone || 0).toFixed(1) })}</Form.Label>
							<Form.Range min={0} max={20} step={1} value={values?.anti_deadzone || 0} onChange={(e) => setFieldValue('anti_deadzone', parseFloat(e.target.value))} />
						</div>
					</div>
					<div style={columnCellStyle}>
						<div style={{ ...row1Style, justifyContent: 'flex-end', gap: '8px' }}>
							<span className="mb-0 text-nowrap">{t('CalibrationSettings:hml-anti-deadzone-mode-label')}</span>
							<span>{values?.fixed_anti_deadzone2 ? t('CalibrationSettings:hml-fixed') : t('CalibrationSettings:hml-linear')}</span>
							<Form.Check type="switch" id="fixed-anti-deadzone-2" label="" checked={values?.fixed_anti_deadzone2 || false} onChange={(e) => setFieldValue('fixed_anti_deadzone2', e.target.checked)} />
							<OverlayTrigger
								placement="top"
								overlay={(
									<Tooltip id="fixed-anti-deadzone-2-tip">
										{t('CalibrationSettings:hml-anti-deadzone-overlay-mode-tooltip')}
									</Tooltip>
								)}
							>
								<span style={{ display: 'inline-flex', cursor: 'help' }}><InfoCircle /></span>
							</OverlayTrigger>
						</div>
						<Form.Label className="mt-3 mb-1">{t('CalibrationSettings:hml-joystick-travel-mapping-key-right')}</Form.Label>
						<CustomSelect isClearable isMulti={!isDisabled(joystickTravelButtonMappingRight.action) && !keyboardKeyOptions.some((opt) => opt.value === joystickTravelButtonMappingRight.action) && !mouseKeyOptions.some((opt) => opt.value === joystickTravelButtonMappingRight.action) && !mappingOptions.some((opt) => opt.value === joystickTravelButtonMappingRight.action && opt.type === 'action')} options={joystickTravelGroupedOptions} isDisabled={isDisabled(joystickTravelButtonMappingRight.action)} getOptionLabel={getOptionLabel} onChange={(selected: MultiValue<OptionType> | SingleValue<OptionType>) => handleJoystickTravelMappingChange(selected, 'right')} value={getMultiValue(joystickTravelButtonMappingRight)} />
						<div style={sliderBlockStyle}>
							<Form.Label className="mb-1">{t('CalibrationSettings:hml-joystick-travel-button-threshold-right', { pct: joystickTravelButtonThresholdRight })}</Form.Label>
							<Form.Range min={0} max={99} step={1} value={joystickTravelButtonThresholdRight} onChange={(e) => setFieldValue('joystickTravelButtonThreshold2', parseInt(e.target.value, 10))} />
						</div>
						<div style={sliderBlockStyle}>
							<Form.Label className="mb-1">{t('CalibrationSettings:hml-inner-deadzone-right', { pct: (values?.inner_deadzone2 || 0).toFixed(1) })}</Form.Label>
							<Form.Range min={0} max={20} step={1} value={values?.inner_deadzone2 || 0} onChange={(e) => setFieldValue('inner_deadzone2', parseFloat(e.target.value))} />
						</div>
						<div style={sliderBlockStyle}>
							<Form.Label className="mb-1">{t('CalibrationSettings:hml-anti-deadzone-right', { pct: (values?.anti_deadzone2 || 0).toFixed(1) })}</Form.Label>
							<Form.Range min={0} max={20} step={1} value={values?.anti_deadzone2 || 0} onChange={(e) => setFieldValue('anti_deadzone2', parseFloat(e.target.value))} />
						</div>
					</div>
				</div>
			</div>
			<div style={{ marginTop: '16px', display: 'flex', justifyContent: 'flex-start', alignItems: 'center', gap: '8px' }}>
				<Button variant="primary" type="button" onClick={(e) => { e.preventDefault(); onSaveClick ? onSaveClick() : handleSubmit(); }}>
					{t('Common:button-save-label')}
				</Button>
				{saveMessage && <span className={saveOk ? 'text-success' : 'text-danger'}>{saveMessage}</span>}
			</div>
		</Section>
	);
}
