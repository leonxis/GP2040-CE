import { useCallback, useContext, useEffect, useMemo, useRef, useState } from 'react';
import { Card, Row, Col, Button } from 'react-bootstrap';
import { useTranslation } from 'react-i18next';
import { omit } from 'lodash';
import { MultiValue } from 'react-select';

import { AppContext } from '../../../Contexts/AppContext';
import { MaskPayload } from '../../../Store/useProfilesStore';
import CustomSelect from '../../../Components/CustomSelect';
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
	defaultPinData,
	getMultiValue,
	getPayloadFromSelected,
	toMaskPayload,
} from './backMappingShared';

type AppContextShape = {
	updateUsedPins?: () => void | Promise<void>;
	buttonLabels?: { buttonLabelType?: string; swapTpShareLabels?: boolean };
};

type PresetBundle = {
	back: Record<string, MaskPayload>;
	fn: Record<string, MaskPayload>;
	twoKey: Record<string, MaskPayload>;
};

const emptyBack = (): Record<string, MaskPayload> => ({
	leftEl: { ...defaultPinData },
	rightEr: { ...defaultPinData },
	leftBack1: { ...defaultPinData },
	rightBack1: { ...defaultPinData },
	leftBack2: { ...defaultPinData },
	rightBack2: { ...defaultPinData },
});

const emptyFn = (): Record<string, MaskPayload> => ({
	leftFn: { ...defaultPinData },
	rightFn: { ...defaultPinData },
	leftMt: { ...defaultPinData },
	rightMt: { ...defaultPinData },
	extLeftTrigger: { ...defaultPinData },
	extRightTrigger: { ...defaultPinData },
});

const emptyTwoKey = (): Record<string, MaskPayload> => ({
	leftKey: { action: BUTTON_ACTIONS.NONE as PinActionValues, customButtonMask: 0, customDpadMask: 0 },
	rightKey: { action: BUTTON_ACTIONS.NONE as PinActionValues, customButtonMask: 0, customDpadMask: 0 },
});

const cloneBundle = (b: PresetBundle): PresetBundle => ({
	back: Object.fromEntries(Object.entries(b.back).map(([k, v]) => [k, { ...v }])),
	fn: Object.fromEntries(Object.entries(b.fn).map(([k, v]) => [k, { ...v }])),
	twoKey: Object.fromEntries(Object.entries(b.twoKey).map(([k, v]) => [k, { ...v }])),
});

const parseBack = (data: Record<string, { action?: number; customButtonMask?: number; customDpadMask?: number }>) => ({
	leftEl: toMaskPayload(data.leftEl),
	rightEr: toMaskPayload(data.rightEr),
	leftBack1: toMaskPayload(data.leftBack1),
	rightBack1: toMaskPayload(data.rightBack1),
	leftBack2: toMaskPayload(data.leftBack2),
	rightBack2: toMaskPayload(data.rightBack2),
});

const parseFn = (data: Record<string, { action?: number; customButtonMask?: number; customDpadMask?: number }>) => ({
	leftFn: toMaskPayload(data.leftFn),
	rightFn: toMaskPayload(data.rightFn),
	leftMt: toMaskPayload(data.leftMt),
	rightMt: toMaskPayload(data.rightMt),
	extLeftTrigger: toMaskPayload(data.extLeftTrigger),
	extRightTrigger: toMaskPayload(data.extRightTrigger),
});

const parseTwoKey = (data: Record<string, { action?: number; customButtonMask?: number; customDpadMask?: number }>) => ({
	leftKey: data.leftKey
		? toMaskPayload(data.leftKey)
		: { action: BUTTON_ACTIONS.NONE as PinActionValues, customButtonMask: 0, customDpadMask: 0 },
	rightKey: data.rightKey
		? toMaskPayload(data.rightKey)
		: { action: BUTTON_ACTIONS.NONE as PinActionValues, customButtonMask: 0, customDpadMask: 0 },
});

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
	const [isLoading, setIsLoading] = useState(false);
	const [twoKeySaveMsg, setTwoKeySaveMsg] = useState('');
	const [twoKeySaving, setTwoKeySaving] = useState(false);
	const [fnSaveMsg, setFnSaveMsg] = useState('');
	const [fnSaving, setFnSaving] = useState(false);

	const backAddonOptions = bundle.back;
	const fnOptions = bundle.fn;
	const twoKeyOptions = bundle.twoKey;

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

	const patchBack = (key: string, payload: MaskPayload) =>
		onBundleChange({ ...bundle, back: { ...bundle.back, [key]: payload } });
	const patchFn = (key: string, payload: MaskPayload) =>
		onBundleChange({ ...bundle, fn: { ...bundle.fn, [key]: payload } });
	const patchTwoKey = (key: string, payload: MaskPayload) =>
		onBundleChange({ ...bundle, twoKey: { ...bundle.twoKey, [key]: payload } });

	const handleSaveBackCard = useCallback(async () => {
		setSaveMessage('');
		setIsLoading(true);
		try {
			await WebApi.setBackButtonAddonOptions({
				presetIndex,
				leftEl: backAddonOptions.leftEl,
				rightEr: backAddonOptions.rightEr,
				leftBack1: backAddonOptions.leftBack1,
				rightBack1: backAddonOptions.rightBack1,
				leftBack2: backAddonOptions.leftBack2,
				rightBack2: backAddonOptions.rightBack2,
			});
			if (appContext) {
				const { updateUsedPins } = appContext as AppContextShape;
				if (updateUsedPins) updateUsedPins();
			}
			onSaved();
			setSaveMessage(t('Common:saved-success-message'));
			setTimeout(() => setSaveMessage(''), 3000);
		} catch (error) {
			console.error('保存背键映射失败:', error);
			setSaveMessage(t('Common:saved-error-message'));
			setTimeout(() => setSaveMessage(''), 3000);
		} finally {
			setIsLoading(false);
		}
	}, [appContext, backAddonOptions, presetIndex, onSaved, t]);

	const handleSaveFn = useCallback(async () => {
		setFnSaveMsg('');
		setFnSaving(true);
		try {
			await WebApi.setFnKeyMappingOptions({
				presetIndex,
				leftFn: fnOptions.leftFn,
				rightFn: fnOptions.rightFn,
				leftMt: fnOptions.leftMt,
				rightMt: fnOptions.rightMt,
				extLeftTrigger: fnOptions.extLeftTrigger,
				extRightTrigger: fnOptions.extRightTrigger,
			});
			onSaved();
			setFnSaveMsg(t('Common:saved-success-message'));
			setTimeout(() => setFnSaveMsg(''), 3000);
		} catch {
			setFnSaveMsg(t('Common:saved-error-message'));
			setTimeout(() => setFnSaveMsg(''), 3000);
		} finally {
			setFnSaving(false);
		}
	}, [fnOptions, presetIndex, onSaved, t]);

	const handleSaveTwoKey = useCallback(async () => {
		setTwoKeySaveMsg('');
		setTwoKeySaving(true);
		try {
			await WebApi.setTwoKeyTouchpadOptions({
				presetIndex,
				section: 'twoKey',
				leftKey: twoKeyOptions.leftKey,
				rightKey: twoKeyOptions.rightKey,
			});
			onSaved();
			setTwoKeySaveMsg(t('Common:saved-success-message'));
			setTimeout(() => setTwoKeySaveMsg(''), 3000);
		} catch {
			setTwoKeySaveMsg(t('Common:saved-error-message'));
			setTimeout(() => setTwoKeySaveMsg(''), 3000);
		} finally {
			setTwoKeySaving(false);
		}
	}, [twoKeyOptions, presetIndex, onSaved, t]);

	const backMappingRows = [
		{ key: 'leftEl', labelKey: 'hml-paddle-left-el' },
		{ key: 'rightEr', labelKey: 'hml-paddle-right-er' },
		{ key: 'leftBack1', labelKey: 'hml-paddle-left-1' },
		{ key: 'rightBack1', labelKey: 'hml-paddle-right-1' },
		{ key: 'leftBack2', labelKey: 'hml-paddle-left-2' },
		{ key: 'rightBack2', labelKey: 'hml-paddle-right-2' },
	];

	const selectRow = (
		key: string,
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
					<Row className="g-3">
						{backMappingRows.map(({ key, labelKey }) =>
							selectRow(key, labelKey, backAddonOptions[key] || defaultPinData, (p) => patchBack(key, p), `back-${key}`),
						)}
					</Row>
					<Row className="mt-3">
						<Col sm={4} className="mb-2">
							<Button variant="primary" onClick={handleSaveBackCard} disabled={isLoading} className="me-3">
								{t('Common:button-save-label')}
							</Button>
							{saveMessage && (
								<span
									className={`me-3 ${saveMessage === t('Common:saved-success-message') ? 'text-success' : 'text-danger'}`}
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
						<>
							<Row className="g-3">
								{selectRow('leftKey', 'hml-touch-left', twoKeyOptions.leftKey, (p) => patchTwoKey('leftKey', p), 'tk-l')}
								{selectRow('rightKey', 'hml-touch-right', twoKeyOptions.rightKey, (p) => patchTwoKey('rightKey', p), 'tk-r')}
							</Row>
							<Row className="mt-3">
								<Col sm={4}>
									<Button variant="primary" onClick={handleSaveTwoKey} disabled={twoKeySaving}>
										{t('Common:button-save-label')}
									</Button>
									{twoKeySaveMsg && (
										<span
											className={`ms-3 ${twoKeySaveMsg === t('Common:saved-success-message') ? 'text-success' : 'text-danger'}`}
										>
											{twoKeySaveMsg}
										</span>
									)}
								</Col>
							</Row>
						</>
					) : (
						<p className="text-muted mb-0">{t('CalibrationSettings:hml-touchpad-disabled-hint')}</p>
					)}
				</Card.Body>
			</Card>

			<Card style={{ marginBottom: '1rem' }}>
				<Card.Header>{t('SettingsPage:hml-fn-key-mapping-title')}</Card.Header>
				<Card.Body>
					<Row className="g-3">
						{selectRow('leftFn', 'hml-fn-left', fnOptions.leftFn, (p) => patchFn('leftFn', p), 'fn-l')}
						{selectRow('rightFn', 'hml-fn-right', fnOptions.rightFn, (p) => patchFn('rightFn', p), 'fn-r')}
						{selectRow('leftMt', 'hml-mt-left', fnOptions.leftMt, (p) => patchFn('leftMt', p), 'mt-l')}
						{selectRow('rightMt', 'hml-mt-right', fnOptions.rightMt, (p) => patchFn('rightMt', p), 'mt-r')}
						{selectRow('extLeftTrigger', 'hml-ext-l2', fnOptions.extLeftTrigger, (p) => patchFn('extLeftTrigger', p), 'ex-l')}
						{selectRow('extRightTrigger', 'hml-ext-r2', fnOptions.extRightTrigger, (p) => patchFn('extRightTrigger', p), 'ex-r')}
					</Row>
					<Row className="mt-3">
						<Col sm={4}>
							<Button variant="primary" onClick={handleSaveFn} disabled={fnSaving}>
								{t('Common:button-save-label')}
							</Button>
							{fnSaveMsg && (
								<span className={`ms-3 ${fnSaveMsg === t('Common:saved-success-message') ? 'text-success' : 'text-danger'}`}>
									{fnSaveMsg}
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
	const [twoKeyTouchpadEnabled, setTwoKeyTouchpadEnabled] = useState(false);
	const [presets, setPresets] = useState<PresetBundle[]>([
		{ back: emptyBack(), fn: emptyFn(), twoKey: emptyTwoKey() },
		{ back: emptyBack(), fn: emptyFn(), twoKey: emptyTwoKey() },
		{ back: emptyBack(), fn: emptyFn(), twoKey: emptyTwoKey() },
	]);
	const snapshotRef = useRef<PresetBundle[]>([
		{ back: emptyBack(), fn: emptyFn(), twoKey: emptyTwoKey() },
		{ back: emptyBack(), fn: emptyFn(), twoKey: emptyTwoKey() },
		{ back: emptyBack(), fn: emptyFn(), twoKey: emptyTwoKey() },
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
				const [back, fn, twoKey] = await Promise.all([
					WebApi.getBackButtonAddonOptions(i),
					WebApi.getFnKeyMappingOptions(i),
					WebApi.getTwoKeyTouchpadOptions(i),
				]);
				loaded.push({
					back: back ? parseBack(back as Parameters<typeof parseBack>[0]) : emptyBack(),
					fn: fn ? parseFn(fn as Parameters<typeof parseFn>[0]) : emptyFn(),
					twoKey: twoKey ? parseTwoKey(twoKey as Parameters<typeof parseTwoKey>[0]) : emptyTwoKey(),
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
					onSaved={() => {
						commitSnapshot(index);
						setSelectedTab(PRESET_TAB_KEYS[index] ?? 'preset-0');
					}}
				/>
			)}
		</MappingPresetShell>
	);
}
