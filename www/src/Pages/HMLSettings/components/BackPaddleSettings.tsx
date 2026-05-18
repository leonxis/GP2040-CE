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
import MappingPresetShell from './MappingPresetShell';
import {
	defaultPinData,
	getMultiValue,
	getPayloadFromSelected,
	getPinKey,
	toMaskPayload,
} from './backMappingShared';

type AppContextShape = {
	updateUsedPins?: () => void | Promise<void>;
	buttonLabels?: { buttonLabelType?: string; swapTpShareLabels?: boolean };
};

function BackPaddleSettingsBody() {
	const { t } = useTranslation();
	const appContext = useContext(AppContext);
	const setProfilePin = useProfilesStore((state) => state.setProfilePin);
	const saveProfiles = useProfilesStore((state) => state.saveProfiles);
	const [saveMessage, setSaveMessage] = useState('');
	const [isLoading, setIsLoading] = useState(false);
	const [twoKeyTouchpadEnabled, setTwoKeyTouchpadEnabled] = useState(false);
	const [twoKeyOptions, setTwoKeyOptions] = useState<Record<string, MaskPayload>>({
		leftKey: { action: BUTTON_ACTIONS.NONE as PinActionValues, customButtonMask: 0, customDpadMask: 0 },
		rightKey: { action: BUTTON_ACTIONS.NONE as PinActionValues, customButtonMask: 0, customDpadMask: 0 },
	});
	const [twoKeySaveMsg, setTwoKeySaveMsg] = useState('');
	const [twoKeySaving, setTwoKeySaving] = useState(false);
	const [fnOptions, setFnOptions] = useState<Record<string, MaskPayload>>({
		leftFn: { action: BUTTON_ACTIONS.NONE, customButtonMask: 0, customDpadMask: 0 },
		rightFn: { action: BUTTON_ACTIONS.NONE, customButtonMask: 0, customDpadMask: 0 },
		leftMt: { action: BUTTON_ACTIONS.NONE, customButtonMask: 0, customDpadMask: 0 },
		rightMt: { action: BUTTON_ACTIONS.NONE, customButtonMask: 0, customDpadMask: 0 },
		extLeftTrigger: { action: BUTTON_ACTIONS.NONE, customButtonMask: 0, customDpadMask: 0 },
		extRightTrigger: { action: BUTTON_ACTIONS.NONE, customButtonMask: 0, customDpadMask: 0 },
	});
	const [fnSaveMsg, setFnSaveMsg] = useState('');
	const [fnSaving, setFnSaving] = useState(false);

	const [backAddonOptions, setBackAddonOptions] = useState<Record<string, MaskPayload>>({
		leftBack1: { action: BUTTON_ACTIONS.NONE, customButtonMask: 0, customDpadMask: 0 },
		rightBack1: { action: BUTTON_ACTIONS.NONE, customButtonMask: 0, customDpadMask: 0 },
		leftBack2: { action: BUTTON_ACTIONS.NONE, customButtonMask: 0, customDpadMask: 0 },
		rightBack2: { action: BUTTON_ACTIONS.NONE, customButtonMask: 0, customDpadMask: 0 },
	});

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

	const onTwoKeyChange = useCallback((key: string) => (selected: MultiValue<OptionType> | SingleValue<OptionType>) => {
		setTwoKeyOptions((prev) => ({ ...prev, [key]: getPayloadFromSelected(selected) }));
	}, []);
	const onFnChange = useCallback((key: string) => (selected: MultiValue<OptionType> | SingleValue<OptionType>) => {
		setFnOptions((prev) => ({ ...prev, [key]: getPayloadFromSelected(selected) }));
	}, []);
	const onBackAddonChange = useCallback((key: string) => (selected: MultiValue<OptionType> | SingleValue<OptionType>) => {
		setBackAddonOptions((prev) => ({ ...prev, [key]: getPayloadFromSelected(selected) }));
	}, []);

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
		Promise.all([WebApi.getTwoKeyTouchpadOptions(), WebApi.getFnKeyMappingOptions(), WebApi.getBackButtonAddonOptions()]).then(
			([twoKey, fn, backAddon]) => {
				if (twoKey) {
					setTwoKeyTouchpadEnabled(Boolean(twoKey.enabled));
					setTwoKeyOptions({
						leftKey: twoKey.leftKey
							? toMaskPayload(twoKey.leftKey)
							: { action: BUTTON_ACTIONS.NONE as PinActionValues, customButtonMask: 0, customDpadMask: 0 },
						rightKey: twoKey.rightKey
							? toMaskPayload(twoKey.rightKey)
							: { action: BUTTON_ACTIONS.NONE as PinActionValues, customButtonMask: 0, customDpadMask: 0 },
					});
				}
				if (fn) {
					setFnOptions({
						leftFn: toMaskPayload(fn.leftFn),
						rightFn: toMaskPayload(fn.rightFn),
						leftMt: toMaskPayload(fn.leftMt),
						rightMt: toMaskPayload(fn.rightMt),
						extLeftTrigger: toMaskPayload(fn.extLeftTrigger),
						extRightTrigger: toMaskPayload(fn.extRightTrigger),
					});
				}
				if (backAddon) {
					setBackAddonOptions({
						leftBack1: toMaskPayload(backAddon.leftBack1),
						rightBack1: toMaskPayload(backAddon.rightBack1),
						leftBack2: toMaskPayload(backAddon.leftBack2),
						rightBack2: toMaskPayload(backAddon.rightBack2),
					});
				}
			},
		);
	}, []);

	const handleSaveTwoKey = useCallback(async () => {
		setTwoKeySaveMsg('');
		setTwoKeySaving(true);
		try {
			await WebApi.setTwoKeyTouchpadOptions({
				leftKey: twoKeyOptions.leftKey,
				rightKey: twoKeyOptions.rightKey,
			});
			setTwoKeySaveMsg(t('Common:saved-success-message'));
			setTimeout(() => setTwoKeySaveMsg(''), 3000);
		} catch {
			setTwoKeySaveMsg(t('Common:saved-error-message'));
			setTimeout(() => setTwoKeySaveMsg(''), 3000);
		} finally {
			setTwoKeySaving(false);
		}
	}, [twoKeyOptions, t]);

	const handleSaveFn = useCallback(async () => {
		setFnSaveMsg('');
		setFnSaving(true);
		try {
			const payload = {
				leftFn: fnOptions.leftFn,
				rightFn: fnOptions.rightFn,
				leftMt: fnOptions.leftMt,
				rightMt: fnOptions.rightMt,
				extLeftTrigger: fnOptions.extLeftTrigger,
				extRightTrigger: fnOptions.extRightTrigger,
			};
			await WebApi.setFnKeyMappingOptions(payload);
			setFnSaveMsg(t('Common:saved-success-message'));
			setTimeout(() => setFnSaveMsg(''), 3000);
		} catch {
			setFnSaveMsg(t('Common:saved-error-message'));
			setTimeout(() => setFnSaveMsg(''), 3000);
		} finally {
			setFnSaving(false);
		}
	}, [fnOptions, t]);

	const handleSaveBackCard = useCallback(async () => {
		setSaveMessage('');
		setIsLoading(true);
		try {
			await saveProfiles();
			await WebApi.setBackButtonAddonOptions({
				leftBack1: backAddonOptions.leftBack1,
				rightBack1: backAddonOptions.rightBack1,
				leftBack2: backAddonOptions.leftBack2,
				rightBack2: backAddonOptions.rightBack2,
			});
			await WebApi.setFnKeyMappingOptions({
				leftFn: fnOptions.leftFn,
				rightFn: fnOptions.rightFn,
				leftMt: fnOptions.leftMt,
				rightMt: fnOptions.rightMt,
				extLeftTrigger: fnOptions.extLeftTrigger,
				extRightTrigger: fnOptions.extRightTrigger,
			});

			if (appContext) {
				const { updateUsedPins } = appContext as AppContextShape;
				if (updateUsedPins) {
					updateUsedPins();
				}
			}
			setSaveMessage(t('Common:saved-success-message'));
			setTimeout(() => setSaveMessage(''), 3000);
		} catch (error) {
			console.error('保存背键/FN 映射失败:', error);
			setSaveMessage(t('Common:saved-error-message'));
			setTimeout(() => setSaveMessage(''), 3000);
		} finally {
			setIsLoading(false);
		}
	}, [saveProfiles, appContext, t, backAddonOptions, fnOptions]);

	const gpioPins = [25, 24];
	const backGpioLabelKey: Record<number, string> = {
		25: 'hml-paddle-left-el',
		24: 'hml-paddle-right-er',
	};

	return (
		<div>
			<Card style={{ marginBottom: '1rem' }}>
				<Card.Header>{t('SettingsPage:hml-tab-back-buttons')}</Card.Header>
				<Card.Body>
					<Row className="g-3 mb-3">
						{gpioPins.map((pin) => {
							const pinKey = getPinKey(pin);
							const pinData = pins[pinKey] || defaultPinData;
							const label = backGpioLabelKey[pin]
								? t(`CalibrationSettings:${backGpioLabelKey[pin]}`)
								: `GPIO${pin}`;
							return (
								<Col sm={6} md={6} key={`gpio-${pin}`}>
									<div className="d-flex align-items-center">
										<div className="d-flex flex-shrink-0" style={{ width: '10rem' }}>
											<label>{label}</label>
										</div>
										<CustomSelect
											isClearable
											isMulti={
												!isDisabled(pinData.action) &&
												!keyboardKeyOptions.some((opt) => opt.value === pinData.action) &&
												!mouseKeyOptions.some((opt) => opt.value === pinData.action) &&
												!mappingOptions.some((opt) => opt.value === pinData.action && opt.type === 'action')
											}
											options={groupedMappingOptions}
											isDisabled={isDisabled(pinData.action)}
											getOptionLabel={getOptionLabel}
											onChange={onChange(pinKey)}
											value={getMultiValue(pinData)}
										/>
									</div>
								</Col>
							);
						})}
					</Row>
					<Row className="g-3">
						{[
							{ key: 'leftBack1', labelKey: 'hml-paddle-left-1' },
							{ key: 'rightBack1', labelKey: 'hml-paddle-right-1' },
							{ key: 'leftBack2', labelKey: 'hml-paddle-left-2' },
							{ key: 'rightBack2', labelKey: 'hml-paddle-right-2' },
						].map(({ key, labelKey }) => {
							const label = t(`CalibrationSettings:${labelKey}`);
							const mappingData = backAddonOptions[key] || defaultPinData;
							return (
								<Col sm={6} md={6} key={`back-addon-${key}`}>
									<div className="d-flex align-items-center">
										<div className="d-flex flex-shrink-0" style={{ width: '10rem' }}>
											<label>{label}</label>
										</div>
										<CustomSelect
											isClearable
											isMulti={
												!isDisabled(mappingData.action) &&
												!keyboardKeyOptions.some((opt) => opt.value === mappingData.action) &&
												!mouseKeyOptions.some((opt) => opt.value === mappingData.action) &&
												!mappingOptions.some((opt) => opt.value === mappingData.action && opt.type === 'action')
											}
											options={groupedMappingOptions}
											isDisabled={isDisabled(mappingData.action)}
											getOptionLabel={getOptionLabel}
											onChange={onBackAddonChange(key)}
											value={getMultiValue(mappingData)}
										/>
									</div>
								</Col>
							);
						})}
					</Row>
					<Row className="mt-3">
						<Col sm={4} className="mb-2">
							<Button variant="primary" onClick={handleSaveBackCard} disabled={isLoading} className="me-3">
								{t('Common:button-save-label')}
							</Button>
							{saveMessage && (
								<span
									className={`me-3 ${
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

			<Card style={{ marginBottom: '1rem' }}>
				<Card.Header>{t('SettingsPage:hml-touchpad-mapping-title')}</Card.Header>
				<Card.Body>
					{twoKeyTouchpadEnabled && (
						<>
							<Row className="g-3">
								{[
									{ key: 'leftKey', labelKey: 'hml-touch-left' },
									{ key: 'rightKey', labelKey: 'hml-touch-right' },
								].map(({ key, labelKey }) => {
									const mappingData = twoKeyOptions[key] || defaultPinData;
									return (
										<Col sm={6} md={6} key={`twokey-${key}`}>
											<div className="d-flex align-items-center">
												<div className="d-flex flex-shrink-0" style={{ width: '10rem' }}>
													<label>{t(`CalibrationSettings:${labelKey}`)}</label>
												</div>
												<CustomSelect
													isClearable
													isMulti={
														!isDisabled(mappingData.action) &&
														!keyboardKeyOptions.some((opt) => opt.value === mappingData.action) &&
														!mouseKeyOptions.some((opt) => opt.value === mappingData.action) &&
														!mappingOptions.some((opt) => opt.value === mappingData.action && opt.type === 'action')
													}
													options={groupedMappingOptions}
													isDisabled={isDisabled(mappingData.action)}
													getOptionLabel={getOptionLabel}
													onChange={onTwoKeyChange(key)}
													value={getMultiValue(mappingData)}
												/>
											</div>
										</Col>
									);
								})}
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
					)}
					{!twoKeyTouchpadEnabled && (
						<p className="text-muted mb-0">{t('CalibrationSettings:hml-touchpad-disabled-hint')}</p>
					)}
				</Card.Body>
			</Card>
			<Card style={{ marginBottom: '1rem' }}>
				<Card.Header>{t('SettingsPage:hml-fn-key-mapping-title')}</Card.Header>
				<Card.Body>
					<Row className="g-3">
						{[
							{ key: 'leftFn', labelKey: 'hml-fn-left' },
							{ key: 'rightFn', labelKey: 'hml-fn-right' },
							{ key: 'leftMt', labelKey: 'hml-mt-left' },
							{ key: 'rightMt', labelKey: 'hml-mt-right' },
							{ key: 'extLeftTrigger', labelKey: 'hml-ext-l2' },
							{ key: 'extRightTrigger', labelKey: 'hml-ext-r2' },
						].map(({ key, labelKey }) => {
							const mappingData = fnOptions[key] || defaultPinData;
							return (
								<Col sm={6} md={6} key={`fn-${key}`}>
									<div className="d-flex align-items-center">
										<div className="d-flex flex-shrink-0" style={{ width: '10rem' }}>
											<label>{t(`CalibrationSettings:${labelKey}`)}</label>
										</div>
										<CustomSelect
											isClearable
											isMulti={
												!isDisabled(mappingData.action) &&
												!keyboardKeyOptions.some((opt) => opt.value === mappingData.action) &&
												!mouseKeyOptions.some((opt) => opt.value === mappingData.action) &&
												!mappingOptions.some((opt) => opt.value === mappingData.action && opt.type === 'action')
											}
											options={groupedMappingOptions}
											isDisabled={isDisabled(mappingData.action)}
											getOptionLabel={getOptionLabel}
											onChange={onFnChange(key)}
											value={getMultiValue(mappingData)}
										/>
									</div>
								</Col>
							);
						})}
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
	return (
		<MappingPresetShell>
			<BackPaddleSettingsBody />
		</MappingPresetShell>
	);
}
