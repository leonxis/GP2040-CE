import { useCallback, useContext, useEffect, useMemo, useState } from 'react';
import { Card, Row, Col, Button } from 'react-bootstrap';
import { useTranslation } from 'react-i18next';
import { useShallow } from 'zustand/react/shallow';
import { omit } from 'lodash';
import { MultiValue, SingleValue } from 'react-select';

import { AppContext } from '../../../Contexts/AppContext';
import useProfilesStore, { MaskPayload } from '../../../Store/useProfilesStore';
import CustomSelect from '../../../Components/CustomSelect';
import { getButtonLabels } from '../../../Data/Buttons';
import {
	OptionType,
	groupedMappingOptions,
	isDisabled,
	mappingOptions,
	mouseKeyOptions,
	keyboardKeyOptions,
} from './ActionMappingOptions';
import MappingPresetShell from './MappingPresetShell';
import {
	defaultPinData,
	getMultiValue,
	getPayloadFromSelected,
	getPinKey,
	SwapPinRow,
} from './backMappingShared';

type AppContextShape = {
	updateUsedPins?: () => void | Promise<void>;
	buttonLabels?: { buttonLabelType?: string; swapTpShareLabels?: boolean };
};

const SWAP_GPIO_ROWS: SwapPinRow[] = [
	{ rowId: '18', labelKey: 'hml-pin-share', pinKey: getPinKey(18) },
	{ rowId: '19', labelKey: 'hml-pin-options', pinKey: getPinKey(19) },
	{ rowId: 'ps', labelKey: 'hml-pin-ps', selectDisabled: true },
	{ rowId: '12', labelKey: 'hml-pin-touchpad', pinKey: getPinKey(12) },
	{ rowId: '16', labelKey: 'hml-pin-mouse-left', pinKey: getPinKey(16) },
	{ rowId: '17', labelKey: 'hml-pin-mouse-right', pinKey: getPinKey(17) },
	{ rowId: '23', labelKey: 'hml-pin-up', pinKey: getPinKey(23) },
	{ rowId: '8', labelKey: 'hml-pin-down', pinKey: getPinKey(8) },
	{ rowId: '9', labelKey: 'hml-pin-circle', pinKey: getPinKey(9) },
	{ rowId: '15', labelKey: 'hml-pin-cross', pinKey: getPinKey(15) },
	{ rowId: '14', labelKey: 'hml-pin-triangle', pinKey: getPinKey(14) },
	{ rowId: '13', labelKey: 'hml-pin-square', pinKey: getPinKey(13) },
	{ rowId: '22', labelKey: 'hml-pin-l1', pinKey: getPinKey(22) },
	{ rowId: '21', labelKey: 'hml-pin-r1', pinKey: getPinKey(21) },
	{ rowId: '29', labelKey: 'hml-pin-l2', pinKey: getPinKey(29) },
	{ rowId: '28', labelKey: 'hml-pin-r2', pinKey: getPinKey(28) },
];

function KeySwapSettingsBody() {
	const { t } = useTranslation();
	const appContext = useContext(AppContext);
	const setProfilePin = useProfilesStore((state) => state.setProfilePin);
	const saveProfiles = useProfilesStore((state) => state.saveProfiles);
	const [saveMessage, setSaveMessage] = useState('');
	const [isLoading, setIsLoading] = useState(false);

	const pins = useProfilesStore(
		useShallow((state) =>
			omit(state.profiles[0] || {}, ['profileLabel', 'enabled']) as Record<string, MaskPayload>,
		),
	);

	const buttonNames = useMemo(() => {
		const defaultButtons = getButtonLabels('gp2040', false);
		if (!appContext) {
			return omit(defaultButtons, ['label', 'value']);
		}
		const { buttonLabels } = appContext as AppContextShape;
		if (!buttonLabels) {
			return omit(defaultButtons, ['label', 'value']);
		}
		const { buttonLabelType, swapTpShareLabels } = buttonLabels;
		const currentButtons = getButtonLabels(buttonLabelType, swapTpShareLabels);
		return omit(currentButtons, ['label', 'value']);
	}, [appContext]);

	const onChange = useCallback(
		(pin: string) =>
			(selected: MultiValue<OptionType> | SingleValue<OptionType>) => {
				setProfilePin(0, pin, getPayloadFromSelected(selected));
			},
		[setProfilePin],
	);

	const getOptionLabel = useCallback(
		(option: OptionType) => {
			if (option.type === 'keyboard') {
				const keyName = option.label?.replace('KEYBOARD_KEY_', '');
				if (keyName === 'ALT_F4') {
					return 'KB: Alt+F4';
				}
				return `KB: ${keyName || option.label}`;
			}
			if (option.type === 'mouse') {
				return t(`Proto:GpioAction.${option.label}`);
			}
			const labelKey = option.label?.split('BUTTON_PRESS_')?.pop();
			return (
				(labelKey && buttonNames[labelKey]) ||
				t(`Proto:GpioAction.${option.label}`)
			);
		},
		[buttonNames, t],
	);

	useEffect(() => {
		useProfilesStore.getState().fetchProfiles();
	}, []);

	const handleSave = useCallback(async () => {
		setSaveMessage('');
		setIsLoading(true);
		try {
			await saveProfiles();
			if (appContext) {
				const { updateUsedPins } = appContext as AppContextShape;
				if (updateUsedPins) {
					updateUsedPins();
				}
			}
			setSaveMessage(t('Common:saved-success-message'));
			setTimeout(() => setSaveMessage(''), 3000);
		} catch (error) {
			console.error('保存引脚映射失败:', error);
			setSaveMessage(t('Common:saved-error-message'));
			setTimeout(() => setSaveMessage(''), 3000);
		} finally {
			setIsLoading(false);
		}
	}, [saveProfiles, appContext, t]);

	return (
		<Card style={{ marginBottom: '1rem' }}>
			<Card.Header>{t('SettingsPage:hml-key-swap-title')}</Card.Header>
			<Card.Body>
				<Row className="g-3">
					{SWAP_GPIO_ROWS.map((row) => {
						const pinKey = 'pinKey' in row ? row.pinKey : undefined;
						const pinData = pinKey ? pins[pinKey] || defaultPinData : defaultPinData;
						const selectLocked = 'selectDisabled' in row && row.selectDisabled;
						const mappingDisabled = selectLocked || (pinKey ? isDisabled(pinData.action) : true);
						return (
							<Col sm={6} md={6} key={row.rowId}>
								<div className="d-flex align-items-center">
									<div className="d-flex flex-shrink-0" style={{ width: '8rem' }}>
										<label>{t(`CalibrationSettings:${row.labelKey}`)}</label>
									</div>
									<CustomSelect
										isClearable={!selectLocked}
										isMulti={
											!mappingDisabled &&
											!keyboardKeyOptions.some((opt) => opt.value === pinData.action) &&
											!mouseKeyOptions.some((opt) => opt.value === pinData.action) &&
											!mappingOptions.some((opt) => opt.value === pinData.action && opt.type === 'action')
										}
										options={groupedMappingOptions}
										isDisabled={mappingDisabled}
										getOptionLabel={getOptionLabel}
										onChange={pinKey ? onChange(pinKey) : undefined}
										value={getMultiValue(pinData)}
									/>
								</div>
							</Col>
						);
					})}
				</Row>
				<Row className="mt-3">
					<Col sm={4}>
						<Button variant="primary" onClick={handleSave} disabled={isLoading}>
							{t('Common:button-save-label')}
						</Button>
						{saveMessage && (
							<span
								className={`ms-3 ${
									saveMessage === t('Common:saved-success-message') ? 'text-success' : 'text-danger'
								}`}
							>
								{saveMessage}
							</span>
						)}
					</Col>
				</Row>
			</Card.Body>
		</Card>
	);
}

export default function KeySwapSettings() {
	return (
		<MappingPresetShell>
			<KeySwapSettingsBody />
		</MappingPresetShell>
	);
}
