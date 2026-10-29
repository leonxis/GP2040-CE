import { useCallback, useContext, useEffect, useMemo, useState } from 'react';
import { Card, Row, Col, Button, ButtonGroup, Form } from 'react-bootstrap';
import { useTranslation } from 'react-i18next';
import { omit } from 'lodash';
import { MultiValue, SingleValue } from 'react-select';

import { AppContext } from '../../../Contexts/AppContext';
import useProfilesStore, { MaskPayload } from '../../../Store/useProfilesStore';
import WebApi from '../../../Services/WebApi';
import CustomSelect from '../../../Components/CustomSelect';
import { getButtonLabels } from '../../../Data/Buttons';
import {
	OptionType,
	groupedMappingOptions,
	groupedLayerMappingOptions,
	isDisabled,
	mappingOptions,
	mouseKeyOptions,
	keyboardKeyOptions,
} from './ActionMappingOptions';
import GpioProfileTabs, { ProfileTabKey } from './GpioProfileTabs';
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
	{ rowId: '16', labelKey: 'hml-pin-left', pinKey: getPinKey(16) },
	{ rowId: '17', labelKey: 'hml-pin-right', pinKey: getPinKey(17) },
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

/** 映射层激活器三态按钮组：OFF / HOLD / TOGGLE */
export function ActivatorButtons({
	value,
	onSelect,
}: {
	value: number;
	onSelect: (value: number) => void;
}) {
	const { t } = useTranslation('PinMapping');
	const items = [
		{ value: 0, label: t('activator-off'), title: t('activator-off-title') },
		{ value: 1, label: t('activator-hold'), title: t('activator-hold-title') },
		{ value: 2, label: t('activator-toggle'), title: t('activator-toggle-title') },
	];
	return (
		<ButtonGroup size="sm" className="ms-2 flex-shrink-0">
			{items.map((item) => (
				<Button
					key={item.value}
					variant={value === item.value ? 'primary' : 'outline-secondary'}
					title={item.title}
					onClick={() => onSelect(item.value)}
				>
					{item.label}
				</Button>
			))}
		</ButtonGroup>
	);
}

function clampProfileTabIndex(profileNumber: number, profileCount: number): number {
	if (profileCount <= 0) return 0;
	return Math.min(Math.max(profileNumber - 1, 0), profileCount - 1);
}

function KeySwapSettingsBody({ profileIndex }: { profileIndex: number }) {
	const { t } = useTranslation(['SettingsPage', 'Common', 'PinMapping']);
	const appContext = useContext(AppContext);
	const profile = useProfilesStore((state) => state.profiles[profileIndex]);
	const setProfilePin = useProfilesStore((state) => state.setProfilePin);
	const saveProfiles = useProfilesStore((state) => state.saveProfiles);
	const saveProfilesAndActivate = useProfilesStore(
		(state) => state.saveProfilesAndActivate,
	);
	const toggleProfileEnabled = useProfilesStore(
		(state) => state.toggleProfileEnabled,
	);
	const [saveMessage, setSaveMessage] = useState('');
	const [isLoading, setIsLoading] = useState(false);

	// 0/1=基础映射预设，2/3=热切按键映射层
	const isLayer = profileIndex >= 2;

	const pins = useMemo(
		() =>
			omit(profile || {}, ['profileLabel', 'enabled']) as Record<string, MaskPayload>,
		[profile],
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
				// 合并当前行 activatorMode，动作下拉不覆盖该字段
				const current = pins[pin] || defaultPinData;
				setProfilePin(profileIndex, pin, {
					...getPayloadFromSelected(selected),
					activatorMode: current.activatorMode ?? 0,
				});
			},
		[setProfilePin, profileIndex, pins],
	);

	const onActivatorSelect = useCallback(
		(pin: string, mode: number) => {
			const current = pins[pin] || defaultPinData;
			setProfilePin(profileIndex, pin, { ...current, activatorMode: mode });
		},
		[setProfilePin, profileIndex, pins],
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

	const handleSave = useCallback(async () => {
		setSaveMessage('');
		setIsLoading(true);
		try {
			let ok: boolean;
			if (isLayer) {
				// 层槽：只保存，不改变当前 profileNumber
				await saveProfiles();
				ok = true;
			} else {
				const result = await saveProfilesAndActivate(profileIndex);
				ok = result.mappingsOk && result.activateOk;
				if (ok && appContext) {
					const { updateUsedPins } = appContext as AppContextShape;
					if (updateUsedPins) {
						await updateUsedPins();
					}
				}
			}
			setSaveMessage(
				ok ? t('Common:saved-success-message') : t('Common:saved-error-message'),
			);
			setTimeout(() => setSaveMessage(''), 3000);
		} catch (error) {
			console.error('Failed to save pin mappings:', error);
			setSaveMessage(t('Common:saved-error-message'));
			setTimeout(() => setSaveMessage(''), 3000);
		} finally {
			setIsLoading(false);
		}
	}, [
		saveProfiles,
		saveProfilesAndActivate,
		profileIndex,
		isLayer,
		appContext,
		t,
	]);

	return (
		<>
			<Row className="g-3">
				{SWAP_GPIO_ROWS.map((row) => {
					const pinKey = 'pinKey' in row ? row.pinKey : undefined;
					const pinData = pinKey ? pins[pinKey] || defaultPinData : defaultPinData;
					const selectLocked = 'selectDisabled' in row && row.selectDisabled;
					const mappingDisabled =
						selectLocked || (pinKey ? isDisabled(pinData.action) : true);
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
										!mappingOptions.some(
											(opt) => opt.value === pinData.action && opt.type === 'action',
										)
									}
									options={
										isLayer ? groupedLayerMappingOptions : groupedMappingOptions
									}
									isDisabled={mappingDisabled}
									getOptionLabel={getOptionLabel}
									onChange={pinKey ? onChange(pinKey) : undefined}
									value={getMultiValue(pinData)}
								/>
								{!isLayer && pinKey && !selectLocked && (
									<ActivatorButtons
										value={pinData.activatorMode ?? 0}
										onSelect={(mode) => onActivatorSelect(pinKey, mode)}
									/>
								)}
							</div>
						</Col>
					);
				})}
			</Row>
			<Row className="mt-3">
				<Col sm={12} className="d-flex align-items-center justify-content-between gap-3">
					<div className="d-flex align-items-center gap-3">
						<Button variant="primary" onClick={handleSave} disabled={isLoading}>
							{t('Common:button-save-label')}
						</Button>
						{saveMessage && (
							<span
								className={
									saveMessage === t('Common:saved-success-message')
										? 'text-success'
										: 'text-danger'
								}
							>
								{saveMessage}
							</span>
						)}
					</div>
					{isLayer && (
						<Form.Check
							type="switch"
							id={`layer-master-switch-${profileIndex}`}
							label={t('PinMapping:layer-enable-switch')}
							checked={Boolean(profile?.enabled)}
							onChange={() => toggleProfileEnabled(profileIndex)}
							className="mb-0"
						/>
					)}
				</Col>
			</Row>
		</>
	);
}

export default function KeySwapSettings() {
	const { t } = useTranslation();
	const [activeKey, setActiveKey] = useState<ProfileTabKey>('profile-0');
	const [initialTabReady, setInitialTabReady] = useState(false);
	const [loadFailed, setLoadFailed] = useState(false);
	const profileCount = useProfilesStore((state) => state.profiles.length);

	useEffect(() => {
		let cancelled = false;
		(async () => {
			const loaded = await useProfilesStore.getState().fetchProfiles();
			if (cancelled) return;
			if (!loaded) {
				setLoadFailed(true);
				setInitialTabReady(true);
				return;
			}
			const gamepadOptions = await WebApi.getGamepadOptions();
			if (cancelled) return;
			const count = useProfilesStore.getState().profiles.length;
			const index = clampProfileTabIndex(Number(gamepadOptions?.profileNumber ?? 1), count);
			setActiveKey(`profile-${index}`);
			setInitialTabReady(true);
		})();
		return () => {
			cancelled = true;
		};
	}, []);

	useEffect(() => {
		if (!initialTabReady || profileCount === 0) return;
		const match = /^profile-(\d+)$/.exec(activeKey);
		if (!match) return;
		const index = parseInt(match[1], 10);
		if (index >= profileCount) {
			setActiveKey(`profile-${profileCount - 1}`);
		}
	}, [activeKey, profileCount, initialTabReady]);

	if (!initialTabReady) {
		return (
			<div className="d-flex justify-content-center py-4">
				<span className="spinner-border" />
			</div>
		);
	}

	if (loadFailed || profileCount === 0) {
		return (
			<Card style={{ marginBottom: '1rem' }}>
				<Card.Header>{t('SettingsPage:hml-key-swap-title')}</Card.Header>
				<Card.Body>
					<p className="text-danger mb-0">{t('Common:saved-error-message')}</p>
				</Card.Body>
			</Card>
		);
	}

	return (
		<GpioProfileTabs activeKey={activeKey} onSelectProfile={setActiveKey}>
			{(profileIndex) => (
				<Card style={{ marginBottom: '1rem' }}>
					<Card.Header>{t('SettingsPage:hml-key-swap-title')}</Card.Header>
					<Card.Body>
						<p className="text-muted small mb-3">
							{profileIndex >= 2
								? t('PinMapping:layer-save-hint')
								: t('SettingsPage:hml-key-swap-profile-manage-hint')}
						</p>
						<KeySwapSettingsBody profileIndex={profileIndex} />
					</Card.Body>
				</Card>
			)}
		</GpioProfileTabs>
	);
}
