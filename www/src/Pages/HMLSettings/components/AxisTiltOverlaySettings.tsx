import { useCallback, useContext, useMemo } from 'react';
import { useTranslation } from 'react-i18next';
import { Button, Form, FormCheck } from 'react-bootstrap';
import { useFormikContext } from 'formik';
import { omit } from 'lodash';
import * as yup from 'yup';

import Section from '../../../Components/Section';
import CustomSelect from '../../../Components/CustomSelect';
import { AppContext } from '../../../Contexts/AppContext';
import { BUTTON_MASKS, getButtonLabels } from '../../../Data/Buttons';
import type { AddonPropTypes } from './CalibrationSettings';

/** Minimal AppContext shape for button labels. */
type AppContextShape = {
	buttonLabels?: { buttonLabelType?: string; swapTpShareLabels?: boolean };
};

const BUTTON_OPTIONS = [{ label: 'None', value: 0 }, ...BUTTON_MASKS];

type TriggerMaskOption = { label: string; value: number };

const TRIGGER_MASK_SELECT_OPTIONS: TriggerMaskOption[] = [
	{ label: 'NONE', value: 0 },
	...BUTTON_MASKS.map(({ label, value }) => ({
		label: `BUTTON_PRESS_${label}`,
		value,
	})),
];

const yupEx = yup as any;

export const axisTiltOverlaySettingsScheme = {
	AxisTiltOverlayInputEnabled: yupEx.number().required().label('Axis Tilt Overlay Input Enabled'),
	axisTiltOverlayPressEnabled: yupEx.number().label('Axis Tilt Overlay Press Enabled'),
	axisTiltOverlayRightYTriggerButtonMask: yupEx
		.number()
		.label('Axis Tilt Overlay Right Y Trigger Button')
		.validateSelectionWhenValue('AxisTiltOverlayInputEnabled', BUTTON_OPTIONS),
	axisTiltOverlayRightYPercent1: yupEx
		.number()
		.label('Right stick press preset 1')
		.validateRangeWhenValue('AxisTiltOverlayInputEnabled', -100, 100),
	axisTiltOverlayRightYPercent2: yupEx
		.number()
		.label('Right stick press preset 2')
		.validateRangeWhenValue('AxisTiltOverlayInputEnabled', -100, 100),
	axisTiltOverlayRightYPercent3: yupEx
		.number()
		.label('Right stick press preset 3')
		.validateRangeWhenValue('AxisTiltOverlayInputEnabled', -100, 100),
	axisTiltOverlayRightYActivePreset: yupEx
		.number()
		.label('Axis Tilt Overlay Right Y Active Preset')
		.validateRangeWhenValue('AxisTiltOverlayInputEnabled', 0, 3),
	axisTiltOverlayRcGainEnabled: yupEx.number().label('RC Gain Enabled'),
	axisTiltOverlayRcGainAlwaysOn: yupEx.number().label('RC Gain Always On'),
	axisTiltOverlayRcGainTriggerButtonMask: yupEx
		.number()
		.label('RC Gain Trigger Button')
		.validateSelectionWhenValue('AxisTiltOverlayInputEnabled', BUTTON_OPTIONS),
	axisTiltOverlayRcGainReserved1: yupEx
		.number()
		.validateRangeWhenValue('AxisTiltOverlayInputEnabled', 0, 100),
	axisTiltOverlayRcGainReserved2: yupEx
		.number()
		.validateRangeWhenValue('AxisTiltOverlayInputEnabled', 0, 100),
	axisTiltOverlayRcGainReserved3: yupEx
		.number()
		.validateRangeWhenValue('AxisTiltOverlayInputEnabled', 0, 100),
};

export const axisTiltOverlaySettingsState = {
	AxisTiltOverlayInputEnabled: 0,
	axisTiltOverlayPressEnabled: 0,
	axisTiltOverlayRightYTriggerButtonMask: 0,
	axisTiltOverlayRightYPercent1: 0,
	axisTiltOverlayRightYPercent2: 0,
	axisTiltOverlayRightYPercent3: 0,
	axisTiltOverlayRightYActivePreset: 0,
	axisTiltOverlayRcGainEnabled: 0,
	axisTiltOverlayRcGainAlwaysOn: 0,
	axisTiltOverlayRcGainTriggerButtonMask: 0,
	axisTiltOverlayRcGainReserved1: 0,
	axisTiltOverlayRcGainReserved2: 0,
	axisTiltOverlayRcGainReserved3: 0,
};

function clampPercent(value: number): number {
	return Math.min(100, Math.max(-100, value));
}

function clampRcPercent(value: number): number {
	return Math.min(100, Math.max(0, value));
}

export default function AxisTiltOverlaySettings({
	values,
	errors,
	setFieldValue,
	saveMessage = '',
	onSaveClick,
}: AddonPropTypes) {
	const { t } = useTranslation();
	const { handleSubmit } = useFormikContext();
	const appContext = useContext(AppContext) as AppContextShape | null;
	const saveOk = saveMessage === t('Common:saved-success-message');

	const buttonNames = useMemo(() => {
		const defaultButtons = getButtonLabels('gp2040', false);
		if (!appContext?.buttonLabels) {
			return omit(defaultButtons, ['label', 'value']) as Record<string, string>;
		}
		const { buttonLabelType, swapTpShareLabels } = appContext.buttonLabels;
		return omit(getButtonLabels(buttonLabelType, swapTpShareLabels), ['label', 'value']) as Record<
			string,
			string
		>;
	}, [appContext]);

	const getMaskOptionLabel = useCallback(
		(option: TriggerMaskOption) => {
			if (option.value === 0) {
				return t('Proto:GpioAction.NONE');
			}
			const labelKey = option.label.replace('BUTTON_PRESS_', '');
			return buttonNames[labelKey] || t(`Proto:GpioAction.${option.label}`);
		},
		[buttonNames, t],
	);

	const gridHidden = !values.AxisTiltOverlayInputEnabled;

	return (
		<Section title={t('AddonsConfig:axis-tilt-overlay-header-text')}>
			<div id="AxisTiltOverlayOptions" hidden={gridHidden}>
				<div
					style={{
						display: 'grid',
						gridTemplateColumns: '1fr 1fr',
						gridTemplateRows: 'repeat(5, auto)',
						gap: '12px 24px',
						alignItems: 'start',
						maxWidth: '720px',
						margin: '0 auto',
					}}
				>
					{/* Column 1 — 摇杆下压 */}
					<div>
						<FormCheck
							type="switch"
							id="axisTiltOverlayPressEnabled"
							label={t('AddonsConfig:axis-tilt-overlay-press-enabled-label')}
							checked={Boolean(values.axisTiltOverlayPressEnabled)}
							onChange={(e) =>
								setFieldValue('axisTiltOverlayPressEnabled', e.target.checked ? 1 : 0)
							}
						/>
					</div>
					<div style={{ display: 'flex', alignItems: 'center', gap: '16px', flexWrap: 'wrap' }}>
						<FormCheck
							type="switch"
							id="axisTiltOverlayRcGainEnabled"
							label={t('AddonsConfig:axis-tilt-overlay-rc-gain-enabled-label')}
							checked={Boolean(values.axisTiltOverlayRcGainEnabled)}
							onChange={(e) =>
								setFieldValue('axisTiltOverlayRcGainEnabled', e.target.checked ? 1 : 0)
							}
						/>
						<FormCheck
							type="switch"
							id="axisTiltOverlayRcGainAlwaysOn"
							label={t(
								Boolean(values.axisTiltOverlayRcGainAlwaysOn)
									? 'AddonsConfig:axis-tilt-overlay-rc-gain-mode-always-on-label'
									: 'AddonsConfig:axis-tilt-overlay-rc-gain-mode-trigger-label',
							)}
							checked={Boolean(values.axisTiltOverlayRcGainAlwaysOn)}
							onChange={(e) =>
								setFieldValue('axisTiltOverlayRcGainAlwaysOn', e.target.checked ? 1 : 0)
							}
						/>
					</div>

					<div>
						<Form.Label htmlFor="axisTiltOverlayRightYTriggerButtonMask" className="mb-1">
							{t('AddonsConfig:axis-tilt-overlay-right-y-trigger-button-label')}
						</Form.Label>
						<CustomSelect<TriggerMaskOption, false>
							inputId="axisTiltOverlayRightYTriggerButtonMask"
							isClearable={false}
							isSearchable
							options={TRIGGER_MASK_SELECT_OPTIONS}
							value={
								TRIGGER_MASK_SELECT_OPTIONS.find(
									(o) => o.value === (values.axisTiltOverlayRightYTriggerButtonMask ?? 0),
								) ?? TRIGGER_MASK_SELECT_OPTIONS[0]
							}
							getOptionLabel={getMaskOptionLabel}
							onChange={(opt) => {
								void setFieldValue('axisTiltOverlayRightYTriggerButtonMask', opt?.value ?? 0);
							}}
						/>
						{errors.axisTiltOverlayRightYTriggerButtonMask && (
							<div className="text-danger small mt-1">{errors.axisTiltOverlayRightYTriggerButtonMask}</div>
						)}
					</div>
					<div>
						<Form.Label htmlFor="axisTiltOverlayRcGainTriggerButtonMask" className="mb-1">
							{t('AddonsConfig:axis-tilt-overlay-rc-gain-trigger-button-label')}
						</Form.Label>
						<CustomSelect<TriggerMaskOption, false>
							inputId="axisTiltOverlayRcGainTriggerButtonMask"
							isClearable={false}
							isSearchable
							options={TRIGGER_MASK_SELECT_OPTIONS}
							value={
								TRIGGER_MASK_SELECT_OPTIONS.find(
									(o) => o.value === (values.axisTiltOverlayRcGainTriggerButtonMask ?? 0),
								) ?? TRIGGER_MASK_SELECT_OPTIONS[0]
							}
							getOptionLabel={getMaskOptionLabel}
							onChange={(opt) => {
								void setFieldValue('axisTiltOverlayRcGainTriggerButtonMask', opt?.value ?? 0);
							}}
						/>
					</div>

					{(
						[
							['axisTiltOverlayRightYPercent1', 'axisTiltOverlayRcGainReserved1', 1],
							['axisTiltOverlayRightYPercent2', 'axisTiltOverlayRcGainReserved2', 2],
							['axisTiltOverlayRightYPercent3', 'axisTiltOverlayRcGainReserved3', 3],
						] as const
					).map(([pctName, rcName, idx]) => (
						<div key={pctName} style={{ display: 'contents' }}>
							<div>
								<Form.Label>
									{t(`AddonsConfig:axis-tilt-overlay-right-y-percent-${idx}-label`)}{' '}
									{Number(values[pctName] ?? 0).toFixed(1)}%
								</Form.Label>
								<Form.Range
									min={-100}
									max={100}
									step={0.1}
									value={Number(values[pctName] ?? 0)}
									onChange={(e) =>
										setFieldValue(pctName, clampPercent(parseFloat(e.target.value)))
									}
								/>
							</div>
							<div>
								<Form.Label>
									{t(`AddonsConfig:axis-tilt-overlay-rc-gain-reserved-${idx}-label`)}{' '}
									{Number(values[rcName] ?? 0).toFixed(1)}%
								</Form.Label>
								<Form.Range
									min={0}
									max={100}
									step={0.1}
									value={Number(values[rcName] ?? 0)}
									onChange={(e) =>
										setFieldValue(rcName, clampRcPercent(parseFloat(e.target.value)))
									}
								/>
							</div>
						</div>
					))}
				</div>
			</div>

			<div style={{ marginTop: '16px', display: 'flex', justifyContent: 'space-between', alignItems: 'center', flexWrap: 'wrap', gap: '8px' }}>
				<div style={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
					<Button
						variant="primary"
						type="button"
						onClick={(e) => {
							e.preventDefault();
							onSaveClick ? onSaveClick() : handleSubmit();
						}}
					>
						{t('Common:button-save-label')}
					</Button>
					{saveMessage && (
						<span className={saveOk ? 'text-success' : 'text-danger'}>{saveMessage}</span>
					)}
				</div>
				<FormCheck
					label={t('AddonsConfig:axis-tilt-overlay-enable-label')}
					type="switch"
					id="AxisTiltOverlayInputEnabled"
					checked={Boolean(values.AxisTiltOverlayInputEnabled)}
					onChange={(e) => {
						setFieldValue('AxisTiltOverlayInputEnabled', e.target.checked ? 1 : 0);
					}}
				/>
			</div>
		</Section>
	);
}
