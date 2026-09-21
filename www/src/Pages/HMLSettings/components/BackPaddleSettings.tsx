import { useCallback, useContext, useEffect, useMemo, useRef, useState } from 'react';
import { Alert, Card, Row, Col, Button } from 'react-bootstrap';
import { useTranslation } from 'react-i18next';
import { omit } from 'lodash';

import { AppContext } from '../../../Contexts/AppContext';
import { MaskPayload } from '../../../Store/useProfilesStore';
import CustomSelect from '../../../Components/CustomSelect';
import CaptureButton from '../../../Components/CaptureButton';
import { getButtonLabels } from '../../../Data/Buttons';
import { BUTTON_ACTIONS, PinActionValues } from '../../../Data/Pins';
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
import {
	getMultiValue,
	getPayloadFromSelected,
	toMaskPayload,
} from './backMappingShared';

type AppContextShape = {
	updateUsedPins?: () => void | Promise<void>;
	buttonLabels?: { buttonLabelType?: string; swapTpShareLabels?: boolean };
};

type PresetBundle = {
	twoKey: Record<string, MaskPayload>;
	backKeys: Record<string, MaskPayload>;
};

const nonePayload = (): MaskPayload => ({
	action: BUTTON_ACTIONS.NONE as PinActionValues,
	customButtonMask: 0,
	customDpadMask: 0,
});

// 背键映射 10 项，顺序即界面行序（一行两个）
const BACK_KEY_FIELDS = [
	'leftBack1',
	'rightBack1',
	'leftBack2',
	'rightBack2',
	'leftBack3',
	'rightBack3',
	'leftFn',
	'rightFn',
	'leftMt',
	'rightMt',
] as const;

const BACK_KEY_LABELS: Record<string, string> = {
	leftBack1: 'hml-back-left1',
	rightBack1: 'hml-back-right1',
	leftBack2: 'hml-back-left2',
	rightBack2: 'hml-back-right2',
	leftBack3: 'hml-back-left3',
	rightBack3: 'hml-back-right3',
	leftFn: 'hml-back-leftfn',
	rightFn: 'hml-back-rightfn',
	leftMt: 'hml-back-leftmt',
	rightMt: 'hml-back-rightmt',
};

const emptyTwoKey = (): Record<string, MaskPayload> => ({
	leftKey: nonePayload(),
	rightKey: nonePayload(),
});

const emptyBackKeys = (): Record<string, MaskPayload> =>
	Object.fromEntries(BACK_KEY_FIELDS.map((k) => [k, nonePayload()]));

const cloneBundle = (b: PresetBundle): PresetBundle => ({
	twoKey: Object.fromEntries(Object.entries(b.twoKey).map(([k, v]) => [k, { ...v }])),
	backKeys: Object.fromEntries(Object.entries(b.backKeys).map(([k, v]) => [k, { ...v }])),
});

type MappingData = Record<string, { action?: number; customButtonMask?: number; customDpadMask?: number }>;

const parseMappingGroup = (
	data: MappingData,
	fields: readonly string[],
	empty: () => Record<string, MaskPayload>,
): Record<string, MaskPayload> => {
	const result = empty();
	for (const key of fields) {
		result[key] = data[key] ? toMaskPayload(data[key]) : nonePayload();
	}
	return result;
};

function BackPaddleSettingsBody({
	presetIndex,
	twoKeyTouchpadEnabled,
	bundle,
	onBundleChange,
	onSaved,
}: {
	presetIndex: number;
	twoKeyTouchpadEnabled: boolean;
	bundle: PresetBundle;
	onBundleChange: (next: PresetBundle) => void;
	onSaved: () => void;
}) {
	const { t } = useTranslation();
	const appContext = useContext(AppContext);
	const [saveMessage, setSaveMessage] = useState('');
	const [isSaving, setIsSaving] = useState(false);
	const [backKeyPinMap, setBackKeyPinMap] = useState<Map<number, string>>(new Map());
	const [detectResult, setDetectResult] = useState<
		{ kind: 'back'; field: string } | { kind: 'other' } | null
	>(null);

	// 拉取背键引脚→字段映射（引脚号固件内硬编码，仅 RP2350B 版型有数据）
	useEffect(() => {
		(async () => {
			const data = await WebApi.getHmlBackKeyPins();
			const entries: [number, string][] = Array.isArray(data?.pins)
				? data.pins
					.filter((p: { pin?: unknown; field?: unknown }) =>
						typeof p?.pin === 'number' && typeof p?.field === 'string')
					.map((p: { pin: number; field: string }) => [p.pin, p.field])
				: [];
			setBackKeyPinMap(new Map(entries));
		})();
	}, []);

	const handleDetectBackKey = useCallback(
		(_label: string, pin: number) => {
			if (pin === undefined || pin === null || Number.isNaN(pin)) return;
			const field = backKeyPinMap.get(pin);
			setDetectResult(field ? { kind: 'back', field } : { kind: 'other' });
		},
		[backKeyPinMap],
	);

	const twoKeyOptions = bundle.twoKey;
	const backKeyOptions = bundle.backKeys;

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

	const patchTwoKey = (key: string, payload: MaskPayload) =>
		onBundleChange({ ...bundle, twoKey: { ...bundle.twoKey, [key]: payload } });

	const patchBackKey = (key: string, payload: MaskPayload) =>
		onBundleChange({ ...bundle, backKeys: { ...bundle.backKeys, [key]: payload } });

	const handleSaveAll = useCallback(async () => {
		const { twoKey, backKeys } = bundle;
		setSaveMessage('');
		setIsSaving(true);
		try {
			const requests: Promise<unknown>[] = [
				WebApi.setTwoKeyTouchpadOptions({
					presetIndex,
					setActive: true,
					section: 'twoKey',
					leftKey: twoKey.leftKey,
					rightKey: twoKey.rightKey,
				}),
				WebApi.setTwoKeyTouchpadOptions({
					presetIndex,
					setActive: true,
					section: 'backKeys',
					...backKeys,
				}),
			];
			const results = await Promise.all(requests);
			// WebApi setters return false/null on failure instead of throwing
			if (results.some((r) => r == null)) {
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
			console.error('Failed to save HML back key mapping:', error);
			setSaveMessage(t('Common:saved-error-message'));
			setTimeout(() => setSaveMessage(''), 3000);
		} finally {
			setIsSaving(false);
		}
	}, [appContext, bundle, onSaved, presetIndex, t]);

	const selectRow = (
		labelKey: string,
		data: MaskPayload,
		onChange: (p: MaskPayload) => void,
		id: string,
	) => (
		<Col sm={6} md={6} key={id}>
			<div className="d-flex align-items-center">
				<div className="d-flex flex-shrink-0" style={{ width: '10rem' }}>
					<label>{t(`CalibrationSettings:${labelKey}`)}</label>
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
					onChange={(sel) => onChange(getPayloadFromSelected(sel))}
					value={getMultiValue(data)}
				/>
			</div>
		</Col>
	);

	return (
		<div>
			<Card style={{ marginBottom: '1rem' }}>
				<Card.Header>{t('SettingsPage:hml-tab-back-buttons')}</Card.Header>
				<Card.Body>
					<div className="d-flex align-items-center gap-3 mb-3 flex-wrap">
						{backKeyPinMap.size > 0 && (
							<CaptureButton
								buttonLabel={t('CalibrationSettings:hml-back-detect-button')}
								labels={['']}
								onChange={handleDetectBackKey}
							/>
						)}
						{detectResult?.kind === 'back' && (
							<Alert variant="info" className="mb-0 py-2 px-3">
								{t('CalibrationSettings:hml-back-detected', {
									name: t(`CalibrationSettings:${BACK_KEY_LABELS[detectResult.field]}`),
								})}
							</Alert>
						)}
						{detectResult?.kind === 'other' && (
							<Alert variant="warning" className="mb-0 py-2 px-3">
								{t('CalibrationSettings:hml-back-detected-other')}
							</Alert>
						)}
					</div>
					<Row className="g-3">
						{BACK_KEY_FIELDS.map((field) =>
							selectRow(
								BACK_KEY_LABELS[field],
								backKeyOptions[field],
								(p) => patchBackKey(field, p),
								`bk-${field}`,
							),
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

			<Card style={{ marginBottom: '1rem' }}>
				<Card.Header>{t('SettingsPage:hml-touchpad-mapping-title')}</Card.Header>
				<Card.Body>
					{twoKeyTouchpadEnabled ? (
						<Row className="g-3">
							{selectRow('hml-touch-left', twoKeyOptions.leftKey, (p) => patchTwoKey('leftKey', p), 'tk-l')}
							{selectRow('hml-touch-right', twoKeyOptions.rightKey, (p) => patchTwoKey('rightKey', p), 'tk-r')}
						</Row>
					) : (
						<p className="text-muted mb-0">{t('CalibrationSettings:hml-touchpad-disabled-hint')}</p>
					)}
				</Card.Body>
			</Card>
		</div>
	);
}

export default function BackPaddleSettings() {
	const [selectedTab, setSelectedTab] = useState<PresetTabKey>('preset-0');
	const [twoKeyTouchpadEnabled, setTwoKeyTouchpadEnabled] = useState(false);
	const [presets, setPresets] = useState<PresetBundle[]>([
		{ twoKey: emptyTwoKey(), backKeys: emptyBackKeys() },
		{ twoKey: emptyTwoKey(), backKeys: emptyBackKeys() },
		{ twoKey: emptyTwoKey(), backKeys: emptyBackKeys() },
	]);
	const snapshotRef = useRef<PresetBundle[]>([
		{ twoKey: emptyTwoKey(), backKeys: emptyBackKeys() },
		{ twoKey: emptyTwoKey(), backKeys: emptyBackKeys() },
		{ twoKey: emptyTwoKey(), backKeys: emptyBackKeys() },
	]);

	useEffect(() => {
		(async () => {
			const global = await WebApi.getTwoKeyTouchpadOptions();
			if (global) {
				setTwoKeyTouchpadEnabled(Boolean(global.enabled));
				const ap = Number(global.activePreset ?? 0);
				setSelectedTab(PRESET_TAB_KEYS[ap] ?? 'preset-0');
			}
			const loaded: PresetBundle[] = [];
			for (let i = 0; i < 3; i++) {
				const data = await WebApi.getTwoKeyTouchpadOptions(i);
				const mappingData = (data ?? {}) as Parameters<typeof parseMappingGroup>[0];
				loaded.push({
					twoKey: parseMappingGroup(mappingData, ['leftKey', 'rightKey'], emptyTwoKey),
					backKeys: parseMappingGroup(mappingData, BACK_KEY_FIELDS, emptyBackKeys),
				});
			}
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
					twoKeyTouchpadEnabled={twoKeyTouchpadEnabled}
					bundle={presets[index]}
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
