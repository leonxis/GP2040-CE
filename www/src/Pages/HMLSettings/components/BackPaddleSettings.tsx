import { useCallback, useContext, useEffect, useMemo, useRef, useState } from 'react';
import { Card, Row, Col, Button } from 'react-bootstrap';
import { useTranslation } from 'react-i18next';
import { omit } from 'lodash';

import { AppContext } from '../../../Contexts/AppContext';
import { MaskPayload } from '../../../Store/useProfilesStore';
import CustomSelect from '../../../Components/CustomSelect';
import { getButtonLabels } from '../../../Data/Buttons';
import { BUTTON_ACTIONS } from '../../../Data/Pins';
import WebApi from '../../../Services/WebApi';
import {
	OptionType,
	groupedMappingOptions,
	isDisabled,
	mappingOptions,
	mouseKeyOptions,
	keyboardKeyOptions,
} from './ActionMappingOptions';
import MappingPresetShell, { PRESET_TAB_KEYS, PresetTabKey } from './MappingPresetShell';
import { ActivatorButtons } from './KeySwapSettings';
import {
	defaultPinData,
	getMultiValue,
	getPayloadFromSelected,
	toMaskPayload,
} from './backMappingShared';

type AppContextShape = {
	updateUsedPins?: () => void | Promise<void>;
	buttonLabels?: { buttonLabelType?: string; swapTpShareLabels?: boolean };
};

type PresetBundle = Record<string, MaskPayload>;

const emptyBack = (): PresetBundle => ({
	leftEl: { ...defaultPinData },
	rightEr: { ...defaultPinData },
	leftBack1: { ...defaultPinData },
	rightBack1: { ...defaultPinData },
	leftBack2: { ...defaultPinData },
	rightBack2: { ...defaultPinData },
});

const cloneBundle = (b: PresetBundle): PresetBundle =>
	Object.fromEntries(Object.entries(b).map(([k, v]) => [k, { ...v }]));

const parseBack = (data: Record<string, { action?: number; customButtonMask?: number; customDpadMask?: number }>) => ({
	leftEl: toMaskPayload(data.leftEl),
	rightEr: toMaskPayload(data.rightEr),
	leftBack1: toMaskPayload(data.leftBack1),
	rightBack1: toMaskPayload(data.rightBack1),
	leftBack2: toMaskPayload(data.leftBack2),
	rightBack2: toMaskPayload(data.rightBack2),
});

function BackPaddleSettingsBody({
	presetIndex,
	bundle,
	linkOccupied,
	onBundleChange,
	onSaved,
}: {
	presetIndex: number;
	bundle: PresetBundle;
	linkOccupied: boolean;
	onBundleChange: (next: PresetBundle) => void;
	onSaved: () => void;
}) {
	const { t } = useTranslation();
	const appContext = useContext(AppContext);
	const [saveMessage, setSaveMessage] = useState('');
	const [isSaving, setIsSaving] = useState(false);

	const backAddonOptions = bundle;
	const isBack1 = (key: string) => key === 'leftBack1' || key === 'rightBack1';
	// USB 验证器 / 无线扩展占用 GPIO15/14 时，左右背键1 固定显示为「已分配给插件」
	const getRowData = (key: string): MaskPayload => {
		const stored = backAddonOptions[key] ?? defaultPinData;
		if (!isBack1(key)) return stored;
		if (linkOccupied) {
			return { ...defaultPinData, action: BUTTON_ACTIONS.ASSIGNED_TO_ADDON };
		}
		// 关闭占用后清理历史遗留的 ASSIGNED，避免下拉框被永久锁死
		return stored.action === BUTTON_ACTIONS.ASSIGNED_TO_ADDON
			? { ...defaultPinData }
			: stored;
	};

	const buttonNames = useMemo(() => {
		const defaultButtons = getButtonLabels('gp2040', false);
		if (!appContext) return omit(defaultButtons, ['label', 'value']);
		const { buttonLabels } = appContext as AppContextShape;
		if (!buttonLabels) return omit(defaultButtons, ['label', 'value']);
		return omit(getButtonLabels(buttonLabels.buttonLabelType, buttonLabels.swapTpShareLabels), ['label', 'value']);
	}, [appContext]);

	const getOptionLabel = useCallback(
		(option: OptionType) => {
			if (option.type === 'keyboard') {
				const keyName = option.label?.replace('KEYBOARD_KEY_', '');
				if (keyName === 'ALT_F4') return 'KB: Alt+F4';
				return `KB: ${keyName || option.label}`;
			}
			if (option.type === 'mouse') return t(`Proto:GpioAction.${option.label}`);
			const labelKey = option.label?.split('BUTTON_PRESS_')?.pop();
			return (labelKey && buttonNames[labelKey]) || t(`Proto:GpioAction.${option.label}`);
		},
		[buttonNames, t],
	);

	const patchBack = (key: string, payload: MaskPayload) => {
		if (isBack1(key) && linkOccupied) return;
		onBundleChange({ ...bundle, [key]: payload });
	};

	const handleSaveAll = useCallback(async () => {
		setSaveMessage('');
		setIsSaving(true);
		try {
			const backOk = await WebApi.setBackButtonAddonOptions({
				presetIndex,
				setActive: true,
				leftEl: bundle.leftEl,
				rightEr: bundle.rightEr,
				leftBack1: getRowData('leftBack1'),
				rightBack1: getRowData('rightBack1'),
				leftBack2: bundle.leftBack2,
				rightBack2: bundle.rightBack2,
			});
			// WebApi setters return false/null on failure instead of throwing
			if (backOk !== true) {
				setSaveMessage(t('Common:saved-error-message'));
				setTimeout(() => setSaveMessage(''), 3000);
				return;
			}
			if (appContext) {
				const { updateUsedPins } = appContext as AppContextShape;
				if (updateUsedPins) await updateUsedPins();
			}
			onSaved();
			setSaveMessage(t('Common:saved-success-message'));
			setTimeout(() => setSaveMessage(''), 3000);
		} catch (error) {
			console.error('Failed to save HML paddle mappings:', error);
			setSaveMessage(t('Common:saved-error-message'));
			setTimeout(() => setSaveMessage(''), 3000);
		} finally {
			setIsSaving(false);
		}
	}, [appContext, bundle, linkOccupied, onSaved, presetIndex, t]);

	const backMappingRows = [
		{ key: 'leftBack1', labelKey: 'hml-paddle-left-1', gpio: 'GPIO15' },
		{ key: 'rightBack1', labelKey: 'hml-paddle-right-1', gpio: 'GPIO14' },
		{ key: 'leftBack2', labelKey: 'hml-paddle-left-2', gpio: 'GPIO22' },
		{ key: 'rightBack2', labelKey: 'hml-paddle-right-2', gpio: 'GPIO25' },
		{ key: 'leftEl', labelKey: 'hml-paddle-headphone', gpio: 'GPIO23' },
	];

	const selectRow = (
		labelKey: string,
		data: MaskPayload,
		onChange: (p: MaskPayload) => void,
		id: string,
		showActivator: boolean,
		gpio?: string,
	) => (
		<Col sm={6} md={6} key={id}>
			<div className="d-flex align-items-center">
				<div className="d-flex flex-shrink-0" style={{ width: '10rem' }}>
					<label>
						{t(`CalibrationSettings:${labelKey}`)}
						{gpio ? ` (${gpio})` : ''}
					</label>
				</div>
				<CustomSelect
					isClearable
					isMulti={
						!isDisabled(data.action) &&
						!keyboardKeyOptions.some((opt) => opt.value === data.action) &&
						!mouseKeyOptions.some((opt) => opt.value === data.action) &&
						!mappingOptions.some((opt) => opt.value === data.action && opt.type === 'action')
					}
					options={groupedMappingOptions}
					isDisabled={isDisabled(data.action)}
					getOptionLabel={getOptionLabel}
					onChange={(sel) => {
						// 合并当前行 activatorMode，动作下拉不覆盖该字段
						onChange({ ...getPayloadFromSelected(sel), activatorMode: data.activatorMode ?? 0 });
					}}
					value={getMultiValue(data)}
				/>
				{showActivator && (
					<ActivatorButtons
						value={data.activatorMode ?? 0}
						onSelect={(mode) => onChange({ ...data, activatorMode: mode })}
					/>
				)}
			</div>
		</Col>
	);

	return (
		<div>
			<Card style={{ marginBottom: '1rem' }}>
				<Card.Header>{t('SettingsPage:hml-tab-back-buttons')}</Card.Header>
				<Card.Body>
					<Row className="g-3">
						{backMappingRows.map(({ key, labelKey, gpio }) =>
							selectRow(labelKey, getRowData(key), (p) => patchBack(key, p), `back-${key}`, true, gpio),
						)}
					</Row>
					<Row className="mt-3">
						<Col sm={4}>
							<Button variant="primary" onClick={handleSaveAll} disabled={isSaving}>
								{t('Common:button-save-label')}
							</Button>
							{saveMessage && (
								<span
									className={`ms-3 ${
										saveMessage === t('Common:saved-success-message')
											? 'text-success'
											: 'text-danger'
									}`}
								>
									{saveMessage}
								</span>
							)}
						</Col>
					</Row>
				</Card.Body>
			</Card>
		</div>
	);
}

export default function BackPaddleSettings() {
	const [selectedTab, setSelectedTab] = useState<PresetTabKey>('preset-0');
	const [presets, setPresets] = useState<PresetBundle[]>([
		emptyBack(),
		emptyBack(),
		emptyBack(),
	]);
	const snapshotRef = useRef<PresetBundle[]>([
		emptyBack(),
		emptyBack(),
		emptyBack(),
	]);
	const [linkOccupied, setLinkOccupied] = useState(false);

	useEffect(() => {
		(async () => {
			// USB 验证器 / 无线扩展占用 GPIO15/14 时，左右背键1 显示为已分配给插件
			const [peripheral, gamepad] = await Promise.all([
				WebApi.getPeripheralOptions(),
				WebApi.getGamepadOptions(),
			]);
			setLinkOccupied(
				Boolean(peripheral?.peripheral?.usb0?.enabled) ||
				Boolean(gamepad?.wireless24gLinkEnabled) ||
				Boolean(gamepad?.bluetoothLinkEnabled),
			);

			const loaded: PresetBundle[] = [];
			let activePreset = 0;
			for (let i = 0; i < 3; i++) {
				const back = await WebApi.getBackButtonAddonOptions(i);
				if (i === 0) {
					activePreset = Number(back?.activePreset ?? 0);
				}
				loaded.push(back ? parseBack(back as Parameters<typeof parseBack>[0]) : emptyBack());
			}
			setSelectedTab(PRESET_TAB_KEYS[activePreset] ?? 'preset-0');
			setPresets(loaded);
			snapshotRef.current = loaded.map(cloneBundle);
		})();
	}, []);

	const handleSelectPreset = useCallback(
		(key: PresetTabKey) => {
			const prevIndex = PRESET_TAB_KEYS.indexOf(selectedTab);
			const nextIndex = PRESET_TAB_KEYS.indexOf(key);
			if (prevIndex >= 0 && prevIndex !== nextIndex) {
				setPresets((prev) => {
					const next = [...prev];
					next[prevIndex] = cloneBundle(snapshotRef.current[prevIndex]);
					return next;
				});
			}
			setSelectedTab(key);
		},
		[selectedTab],
	);

	const commitSnapshot = useCallback((index: number) => {
		setPresets((prev) => {
			snapshotRef.current[index] = cloneBundle(prev[index]);
			return prev;
		});
	}, []);

	return (
		<MappingPresetShell activeKey={selectedTab} onSelectPreset={handleSelectPreset}>
			{(index) => (
				<BackPaddleSettingsBody
					presetIndex={index}
					bundle={presets[index]}
					linkOccupied={linkOccupied}
					onBundleChange={(next) =>
						setPresets((prev) => {
							const copy = [...prev];
							copy[index] = next;
							return copy;
						})
					}
					onSaved={() => commitSnapshot(index)}
				/>
			)}
		</MappingPresetShell>
	);
}
