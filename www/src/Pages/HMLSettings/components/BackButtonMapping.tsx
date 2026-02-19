import * as React from 'react';
import { Card, Row, Col, Button, Collapse } from 'react-bootstrap';
import { useTranslation } from 'react-i18next';
import { useCallback, useContext, useMemo, useState } from 'react';
import { useShallow } from 'zustand/react/shallow';
import { invert, omit } from 'lodash';
import { MultiValue, SingleValue } from 'react-select';

import { AppContext } from '../../../Contexts/AppContext';
import useProfilesStore, { MaskPayload } from '../../../Store/useProfilesStore';
import CustomSelect from '../../../Components/CustomSelect';
import { BUTTON_MASKS, DPAD_MASKS, getButtonLabels } from '../../../Data/Buttons';
import { BUTTON_ACTIONS, PinActionValues } from '../../../Data/Pins';
import WebApi from '../../../Services/WebApi';

type OptionType = {
	label: string;
	value: PinActionValues;
	type: string;
	customButtonMask: number;
	customDpadMask: number;
};

const disabledOptions = [
	BUTTON_ACTIONS.RESERVED,
	BUTTON_ACTIONS.ASSIGNED_TO_ADDON,
] as PinActionValues[];

const getMask = (maskArr: { label: string; value: number }[], key: string) =>
	maskArr.find(
		({ label }) => label?.toUpperCase() === key.split('BUTTON_PRESS_')?.pop(),
	);

const isNonSelectable = (action: PinActionValues) =>
	[
		BUTTON_ACTIONS.NONE,
		BUTTON_ACTIONS.CUSTOM_BUTTON_COMBO,
		...disabledOptions,
	].includes(action);

const isDisabled = (action: PinActionValues) =>
	disabledOptions.includes(action);

// Check if action is a keyboard key (KEYBOARD_KEY_* actions)
const isKeyboardKey = (action: PinActionValues) => {
	// Keyboard key actions range from KEYBOARD_KEY_A (131) to KEYBOARD_KEY_9 (169)
	return action >= BUTTON_ACTIONS.KEYBOARD_KEY_A && 
	       action <= BUTTON_ACTIONS.KEYBOARD_KEY_9;
};

const options = Object.entries(BUTTON_ACTIONS)
	.filter(([, value]) => !isNonSelectable(value) && !isKeyboardKey(value))
	.map(([key, value]) => {
		const buttonMask = getMask(BUTTON_MASKS, key);
		const dpadMask = getMask(DPAD_MASKS, key);

		return {
			label: key,
			value,
			type: buttonMask
				? 'customButtonMask'
				: dpadMask
				? 'customDpadMask'
				: 'action',
			customButtonMask: buttonMask?.value || 0,
			customDpadMask: dpadMask?.value || 0,
		};
	});

// Keyboard key options
const keyboardKeyOptions: OptionType[] = [
	{ label: 'KEYBOARD_KEY_A', value: BUTTON_ACTIONS.KEYBOARD_KEY_A, type: 'keyboard', customButtonMask: 0, customDpadMask: 0 },
	{ label: 'KEYBOARD_KEY_B', value: BUTTON_ACTIONS.KEYBOARD_KEY_B, type: 'keyboard', customButtonMask: 0, customDpadMask: 0 },
	{ label: 'KEYBOARD_KEY_C', value: BUTTON_ACTIONS.KEYBOARD_KEY_C, type: 'keyboard', customButtonMask: 0, customDpadMask: 0 },
	{ label: 'KEYBOARD_KEY_D', value: BUTTON_ACTIONS.KEYBOARD_KEY_D, type: 'keyboard', customButtonMask: 0, customDpadMask: 0 },
	{ label: 'KEYBOARD_KEY_E', value: BUTTON_ACTIONS.KEYBOARD_KEY_E, type: 'keyboard', customButtonMask: 0, customDpadMask: 0 },
	{ label: 'KEYBOARD_KEY_F', value: BUTTON_ACTIONS.KEYBOARD_KEY_F, type: 'keyboard', customButtonMask: 0, customDpadMask: 0 },
	{ label: 'KEYBOARD_KEY_G', value: BUTTON_ACTIONS.KEYBOARD_KEY_G, type: 'keyboard', customButtonMask: 0, customDpadMask: 0 },
	{ label: 'KEYBOARD_KEY_H', value: BUTTON_ACTIONS.KEYBOARD_KEY_H, type: 'keyboard', customButtonMask: 0, customDpadMask: 0 },
	{ label: 'KEYBOARD_KEY_I', value: BUTTON_ACTIONS.KEYBOARD_KEY_I, type: 'keyboard', customButtonMask: 0, customDpadMask: 0 },
	{ label: 'KEYBOARD_KEY_J', value: BUTTON_ACTIONS.KEYBOARD_KEY_J, type: 'keyboard', customButtonMask: 0, customDpadMask: 0 },
	{ label: 'KEYBOARD_KEY_K', value: BUTTON_ACTIONS.KEYBOARD_KEY_K, type: 'keyboard', customButtonMask: 0, customDpadMask: 0 },
	{ label: 'KEYBOARD_KEY_L', value: BUTTON_ACTIONS.KEYBOARD_KEY_L, type: 'keyboard', customButtonMask: 0, customDpadMask: 0 },
	{ label: 'KEYBOARD_KEY_M', value: BUTTON_ACTIONS.KEYBOARD_KEY_M, type: 'keyboard', customButtonMask: 0, customDpadMask: 0 },
	{ label: 'KEYBOARD_KEY_N', value: BUTTON_ACTIONS.KEYBOARD_KEY_N, type: 'keyboard', customButtonMask: 0, customDpadMask: 0 },
	{ label: 'KEYBOARD_KEY_O', value: BUTTON_ACTIONS.KEYBOARD_KEY_O, type: 'keyboard', customButtonMask: 0, customDpadMask: 0 },
	{ label: 'KEYBOARD_KEY_P', value: BUTTON_ACTIONS.KEYBOARD_KEY_P, type: 'keyboard', customButtonMask: 0, customDpadMask: 0 },
	{ label: 'KEYBOARD_KEY_Q', value: BUTTON_ACTIONS.KEYBOARD_KEY_Q, type: 'keyboard', customButtonMask: 0, customDpadMask: 0 },
	{ label: 'KEYBOARD_KEY_R', value: BUTTON_ACTIONS.KEYBOARD_KEY_R, type: 'keyboard', customButtonMask: 0, customDpadMask: 0 },
	{ label: 'KEYBOARD_KEY_S', value: BUTTON_ACTIONS.KEYBOARD_KEY_S, type: 'keyboard', customButtonMask: 0, customDpadMask: 0 },
	{ label: 'KEYBOARD_KEY_T', value: BUTTON_ACTIONS.KEYBOARD_KEY_T, type: 'keyboard', customButtonMask: 0, customDpadMask: 0 },
	{ label: 'KEYBOARD_KEY_U', value: BUTTON_ACTIONS.KEYBOARD_KEY_U, type: 'keyboard', customButtonMask: 0, customDpadMask: 0 },
	{ label: 'KEYBOARD_KEY_V', value: BUTTON_ACTIONS.KEYBOARD_KEY_V, type: 'keyboard', customButtonMask: 0, customDpadMask: 0 },
	{ label: 'KEYBOARD_KEY_W', value: BUTTON_ACTIONS.KEYBOARD_KEY_W, type: 'keyboard', customButtonMask: 0, customDpadMask: 0 },
	{ label: 'KEYBOARD_KEY_X', value: BUTTON_ACTIONS.KEYBOARD_KEY_X, type: 'keyboard', customButtonMask: 0, customDpadMask: 0 },
	{ label: 'KEYBOARD_KEY_Y', value: BUTTON_ACTIONS.KEYBOARD_KEY_Y, type: 'keyboard', customButtonMask: 0, customDpadMask: 0 },
	{ label: 'KEYBOARD_KEY_Z', value: BUTTON_ACTIONS.KEYBOARD_KEY_Z, type: 'keyboard', customButtonMask: 0, customDpadMask: 0 },
	{ label: 'KEYBOARD_KEY_CTRL', value: BUTTON_ACTIONS.KEYBOARD_KEY_CTRL, type: 'keyboard', customButtonMask: 0, customDpadMask: 0 },
	{ label: 'KEYBOARD_KEY_SHIFT', value: BUTTON_ACTIONS.KEYBOARD_KEY_SHIFT, type: 'keyboard', customButtonMask: 0, customDpadMask: 0 },
	{ label: 'KEYBOARD_KEY_ALT_F4', value: BUTTON_ACTIONS.KEYBOARD_KEY_ALT_F4, type: 'keyboard', customButtonMask: 0, customDpadMask: 0 },
	{ label: 'KEYBOARD_KEY_0', value: BUTTON_ACTIONS.KEYBOARD_KEY_0, type: 'keyboard', customButtonMask: 0, customDpadMask: 0 },
	{ label: 'KEYBOARD_KEY_1', value: BUTTON_ACTIONS.KEYBOARD_KEY_1, type: 'keyboard', customButtonMask: 0, customDpadMask: 0 },
	{ label: 'KEYBOARD_KEY_2', value: BUTTON_ACTIONS.KEYBOARD_KEY_2, type: 'keyboard', customButtonMask: 0, customDpadMask: 0 },
	{ label: 'KEYBOARD_KEY_3', value: BUTTON_ACTIONS.KEYBOARD_KEY_3, type: 'keyboard', customButtonMask: 0, customDpadMask: 0 },
	{ label: 'KEYBOARD_KEY_4', value: BUTTON_ACTIONS.KEYBOARD_KEY_4, type: 'keyboard', customButtonMask: 0, customDpadMask: 0 },
	{ label: 'KEYBOARD_KEY_5', value: BUTTON_ACTIONS.KEYBOARD_KEY_5, type: 'keyboard', customButtonMask: 0, customDpadMask: 0 },
	{ label: 'KEYBOARD_KEY_6', value: BUTTON_ACTIONS.KEYBOARD_KEY_6, type: 'keyboard', customButtonMask: 0, customDpadMask: 0 },
	{ label: 'KEYBOARD_KEY_7', value: BUTTON_ACTIONS.KEYBOARD_KEY_7, type: 'keyboard', customButtonMask: 0, customDpadMask: 0 },
	{ label: 'KEYBOARD_KEY_8', value: BUTTON_ACTIONS.KEYBOARD_KEY_8, type: 'keyboard', customButtonMask: 0, customDpadMask: 0 },
	{ label: 'KEYBOARD_KEY_9', value: BUTTON_ACTIONS.KEYBOARD_KEY_9, type: 'keyboard', customButtonMask: 0, customDpadMask: 0 },
];

const groupedOptions = [
	{
		label: 'Buttons',
		options: options.filter(({ type }) => type !== 'action'),
	},
	{
		label: 'Actions',
		options: options.filter(({ type }) => type === 'action'),
	},
	{
		label: 'Keyboard Keys',
		options: keyboardKeyOptions,
	},
];

const getMultiValue = (pinData: MaskPayload) => {
	if (pinData.action === BUTTON_ACTIONS.NONE) return;
	if (isDisabled(pinData.action)) {
		const actionKey = invert(BUTTON_ACTIONS)[pinData.action];
		return [
			{
				label: actionKey,
				value: pinData.action,
				type: 'action',
				customButtonMask: pinData.customButtonMask,
				customDpadMask: pinData.customDpadMask,
			},
		];
	}

	// Check if it's a keyboard key
	const keyboardOption = keyboardKeyOptions.find((opt) => opt.value === pinData.action);
	if (keyboardOption) {
		return [keyboardOption];
	}

	return pinData.action === BUTTON_ACTIONS.CUSTOM_BUTTON_COMBO
		? options.filter(
				({ type, customButtonMask, customDpadMask }) =>
					(pinData.customButtonMask & customButtonMask &&
						type === 'customButtonMask') ||
					(pinData.customDpadMask & customDpadMask &&
						type === 'customDpadMask'),
		  )
		: options.filter((option) => option.value === pinData.action);
};

// 从下拉选中值生成 MaskPayload（与背键/按键交换共用）
function getPayloadFromSelected(
	selected: MultiValue<OptionType> | SingleValue<OptionType>,
): MaskPayload {
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
}

export default function BackButtonMapping() {
	const { t } = useTranslation();
	const appContext = useContext(AppContext);
	const setProfilePin = useProfilesStore((state) => state.setProfilePin);
	const saveProfiles = useProfilesStore((state) => state.saveProfiles);
	const [saveMessage, setSaveMessage] = useState('');
	const [isLoading, setIsLoading] = useState(false);
	// 触摸板映射（KEY1=左上, KEY4=右上, KEY2=左下, KEY3=右下）
	const [touchpadOptions, setTouchpadOptions] = useState<Record<string, MaskPayload>>({
		key1: { action: BUTTON_ACTIONS.NONE, customButtonMask: 0, customDpadMask: 0 },
		key4: { action: BUTTON_ACTIONS.NONE, customButtonMask: 0, customDpadMask: 0 },
		key2: { action: BUTTON_ACTIONS.NONE, customButtonMask: 0, customDpadMask: 0 },
		key3: { action: BUTTON_ACTIONS.NONE, customButtonMask: 0, customDpadMask: 0 },
	});
	const [touchpadSaveMsg, setTouchpadSaveMsg] = useState('');
	const [touchpadSaving, setTouchpadSaving] = useState(false);
	// 由硬件配置-4键触摸板开关决定触摸板映射栏折叠/展开
	const [fourKeyTouchpadEnabled, setFourKeyTouchpadEnabled] = useState(false);
	// FN键映射（左FN、右FN、左MT、右MT、Ext左扳机、Ext右扳机；引脚先留空）
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

	// 使用useProfilesStore获取base profile（索引0）的引脚映射
	const pins = useProfilesStore(
		useShallow((state) =>
			omit(state.profiles[0] || {}, ['profileLabel', 'enabled']) as Record<string, MaskPayload>,
		),
	);

	// 从AppContext获取按钮标签类型，根据网页顶层选择的手柄格式动态获取按钮标签
	const buttonNames = useMemo(() => {
		const defaultButtons = getButtonLabels('gp2040', false);
		if (!appContext) {
			return omit(defaultButtons, ['label', 'value']);
		}
		const { buttonLabels } = appContext as any;
		if (!buttonLabels) {
			return omit(defaultButtons, ['label', 'value']);
		}
		const { buttonLabelType, swapTpShareLabels } = buttonLabels;
		const currentButtons = getButtonLabels(buttonLabelType, swapTpShareLabels);
		return omit(currentButtons, ['label', 'value']);
	}, [appContext]);

	// 处理引脚变化，与PinMapping页面使用相同的逻辑
	const onChange = useCallback(
		(pin: string) =>
			(selected: MultiValue<OptionType> | SingleValue<OptionType>) => {
				setProfilePin(0, pin, getPayloadFromSelected(selected));
			},
		[setProfilePin],
	);

	// 触摸板映射 / FN 键映射 变更
	const onTouchpadChange = useCallback((key: string) => (selected: MultiValue<OptionType> | SingleValue<OptionType>) => {
		setTouchpadOptions((prev) => ({ ...prev, [key]: getPayloadFromSelected(selected) }));
	}, []);
	const onFnChange = useCallback((key: string) => (selected: MultiValue<OptionType> | SingleValue<OptionType>) => {
		setFnOptions((prev) => ({ ...prev, [key]: getPayloadFromSelected(selected) }));
	}, []);

	const getOptionLabel = useCallback(
		(option: OptionType) => {
			// Handle keyboard keys
			if (option.type === 'keyboard') {
				const keyName = option.label?.replace('KEYBOARD_KEY_', '');
				if (keyName === 'ALT_F4') {
					return 'KB: Alt+F4';
				}
				// Add 'KB: ' prefix to distinguish keyboard keys from gamepad buttons
				return `KB: ${keyName || option.label}`;
			}
			// Handle regular buttons
			const labelKey = option.label?.split('BUTTON_PRESS_')?.pop();
			return (
				(labelKey && buttonNames[labelKey]) ||
				t(`Proto:GpioAction.${option.label}`)
			);
		},
		[buttonNames, t],
	);

	// 确保profiles已加载
	React.useEffect(() => {
		const fetchProfiles = useProfilesStore.getState().fetchProfiles;
		if (useProfilesStore.getState().profiles.length === 0) {
			fetchProfiles();
		}
	}, []);

	// 加载触摸板映射与 FN 键映射
	React.useEffect(() => {
		const load = async () => {
			const [touchpad, fn] = await Promise.all([
				WebApi.getFourKeyTouchpadOptions(),
				WebApi.getFnKeyMappingOptions(),
			]);
			const toPayload = (m: { action?: number; customButtonMask?: number; customDpadMask?: number } | undefined): MaskPayload =>
				m ? { action: (m.action ?? BUTTON_ACTIONS.NONE) as PinActionValues, customButtonMask: m.customButtonMask ?? 0, customDpadMask: m.customDpadMask ?? 0 } : defaultPinData;
			if (touchpad) {
				setFourKeyTouchpadEnabled(Boolean(touchpad.enabled));
				setTouchpadOptions({
					key1: toPayload(touchpad.key1),
					key4: toPayload(touchpad.key4),
					key2: toPayload(touchpad.key2),
					key3: toPayload(touchpad.key3),
				});
			}
			if (fn) {
				setFnOptions({
					leftFn: toPayload(fn.leftFn),
					rightFn: toPayload(fn.rightFn),
					leftMt: toPayload(fn.leftMt),
					rightMt: toPayload(fn.rightMt),
					extLeftTrigger: toPayload(fn.extLeftTrigger),
					extRightTrigger: toPayload(fn.extRightTrigger),
				});
			}
		};
		load();
	}, []);

	const handleSaveTouchpad = useCallback(async () => {
		setTouchpadSaveMsg('');
		setTouchpadSaving(true);
		try {
			const payload = {
				key1: touchpadOptions.key1,
				key2: touchpadOptions.key2,
				key3: touchpadOptions.key3,
				key4: touchpadOptions.key4,
			};
			await WebApi.setFourKeyTouchpadOptions(payload);
			setTouchpadSaveMsg(t('Common:saved-success-message'));
			setTimeout(() => setTouchpadSaveMsg(''), 3000);
		} catch (e) {
			setTouchpadSaveMsg(t('Common:saved-error-message'));
			setTimeout(() => setTouchpadSaveMsg(''), 3000);
		} finally {
			setTouchpadSaving(false);
		}
	}, [touchpadOptions, t]);

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
		} catch (e) {
			setFnSaveMsg(t('Common:saved-error-message'));
			setTimeout(() => setFnSaveMsg(''), 3000);
		} finally {
			setFnSaving(false);
		}
	}, [fnOptions, t]);

	// 保存引脚映射
	const handleSave = useCallback(async () => {
		setSaveMessage('');
		setIsLoading(true);
		try {
			await saveProfiles();
			if (appContext) {
				const { updateUsedPins } = appContext as any;
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

	// 默认引脚数据
	const defaultPinData: MaskPayload = {
		action: BUTTON_ACTIONS.NONE,
		customButtonMask: 0,
		customDpadMask: 0,
	};

	// 生成pinKey的工具函数
	const getPinKey = (pin: number) => `pin${pin < 10 ? '0' : ''}${pin}`;

	// 背键映射的GPIO引脚列表（左背键1=24, 右背键1=25, 左背键2=26, 右背键2=27）
	const gpioPins = [24, 25, 26, 27];
	
	// 背键GPIO引脚标签映射
	const backButtonLabels: Record<number, string> = {
		24: '左背键1',
		25: '右背键1',
		26: '左背键2',
		27: '右背键2',
	};
	
	// 按键交换的GPIO引脚列表（上方的分享/选项/PS/触摸板，再为其余按键）
	const swapGpioPins = [
		{ pin: 18, label: '分享键', pinKey: getPinKey(18) },
		{ pin: 19, label: '选项键', pinKey: getPinKey(19) },
		{ pin: 8, label: 'PS键', pinKey: getPinKey(8) },
		{ pin: 12, label: '触摸板', pinKey: getPinKey(12) },
		{ pin: 16, label: '左键', pinKey: getPinKey(16) },
		{ pin: 17, label: '右键', pinKey: getPinKey(17) },
		{ pin: 23, label: '上键', pinKey: getPinKey(23) },
		{ pin: 7, label: '下键', pinKey: getPinKey(7) },
		{ pin: 9, label: '圆圈', pinKey: getPinKey(9) },
		{ pin: 15, label: '叉叉', pinKey: getPinKey(15) },
		{ pin: 14, label: '三角', pinKey: getPinKey(14) },
		{ pin: 13, label: '方块', pinKey: getPinKey(13) },
		{ pin: 22, label: '左肩键', pinKey: getPinKey(22) },
		{ pin: 21, label: '右肩键', pinKey: getPinKey(21) },
		{ pin: 29, label: '左扳机', pinKey: getPinKey(29) },
		{ pin: 28, label: '右扳机', pinKey: getPinKey(28) },
	];

	// 保存按钮组件（避免重复代码）
	const SaveButtonSection = (
		<Row className="mt-3">
			<Col sm={4}>
				<Button
					variant="primary"
					onClick={handleSave}
					disabled={isLoading}
				>
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
	);

	return (
		<div>
			<Card style={{ marginBottom: '1rem' }}>
				<Card.Header>背键映射</Card.Header>
				<Card.Body>
					<Row className="g-3">
						{gpioPins.map((pin) => {
							const pinKey = getPinKey(pin);
							const pinData = pins[pinKey] || defaultPinData;
							const label = backButtonLabels[pin] || `GPIO${pin}`;
							return (
								<Col sm={6} md={6} key={`gpio-${pin}`}>
									<div className="d-flex align-items-center">
										<div className="d-flex flex-shrink-0" style={{ width: '10rem' }}>
											<label>{label}</label>
										</div>
										<CustomSelect
											isClearable
											isMulti={!isDisabled(pinData.action) && 
												// Disable multi-select for keyboard keys
												!keyboardKeyOptions.some(opt => opt.value === pinData.action) &&
												// Disable multi-select for action types (non-button actions)
												!options.some(opt => opt.value === pinData.action && opt.type === 'action')}
											options={groupedOptions}
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
					{SaveButtonSection}
				</Card.Body>
			</Card>
			<Card style={{ marginBottom: '1rem' }}>
				<Card.Header>触摸板映射</Card.Header>
				<Collapse in={fourKeyTouchpadEnabled}>
					<Card.Body>
						<Row className="g-3">
							{[
								{ key: 'key4', label: '左上触摸键' },
								{ key: 'key1', label: '右上触摸键' },
								{ key: 'key3', label: '左下触摸键' },
								{ key: 'key2', label: '右下触摸键' },
							].map(({ key, label }) => {
								const mappingData = touchpadOptions[key] || defaultPinData;
								return (
									<Col sm={6} md={6} key={`touchpad-${key}`}>
										<div className="d-flex align-items-center">
											<div className="d-flex flex-shrink-0" style={{ width: '12rem' }}>
												<label>{label}</label>
											</div>
											<CustomSelect
												isClearable
												isMulti={!isDisabled(mappingData.action) &&
													!keyboardKeyOptions.some((opt) => opt.value === mappingData.action) &&
													!options.some((opt) => opt.value === mappingData.action && opt.type === 'action')}
												options={groupedOptions}
												isDisabled={isDisabled(mappingData.action)}
												getOptionLabel={getOptionLabel}
												onChange={onTouchpadChange(key)}
												value={getMultiValue(mappingData)}
											/>
										</div>
									</Col>
								);
							})}
						</Row>
						<Row className="mt-3">
							<Col sm={4}>
								<Button variant="primary" onClick={handleSaveTouchpad} disabled={touchpadSaving}>
									{t('Common:button-save-label')}
								</Button>
								{touchpadSaveMsg && (
									<span className={`ms-3 ${touchpadSaveMsg === t('Common:saved-success-message') ? 'text-success' : 'text-danger'}`}>
										{touchpadSaveMsg}
									</span>
								)}
							</Col>
						</Row>
					</Card.Body>
				</Collapse>
			</Card>
			<Card style={{ marginBottom: '1rem' }}>
				<Card.Header>FN键映射</Card.Header>
				<Card.Body>
					<Row className="g-3">
						{[
							{ key: 'leftFn', label: '左FN键' },
							{ key: 'rightFn', label: '右FN键' },
							{ key: 'leftMt', label: '左MT键' },
							{ key: 'rightMt', label: '右MT键' },
							{ key: 'extLeftTrigger', label: 'Ext左扳机' },
							{ key: 'extRightTrigger', label: 'Ext右扳机' },
						].map(({ key, label }) => {
							const mappingData = fnOptions[key] || defaultPinData;
							return (
								<Col sm={6} md={6} key={`fn-${key}`}>
									<div className="d-flex align-items-center">
										<div className="d-flex flex-shrink-0" style={{ width: '10rem' }}>
											<label>{label}</label>
										</div>
										<CustomSelect
											isClearable
											isMulti={!isDisabled(mappingData.action) &&
												!keyboardKeyOptions.some((opt) => opt.value === mappingData.action) &&
												!options.some((opt) => opt.value === mappingData.action && opt.type === 'action')}
											options={groupedOptions}
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
					<Row className="mt-2">
						<Col>
							<small className="text-muted">
								CH2（MCP3208）：0V→左MT键，0.82V→L3(固定)，1.65V→Ext左扳机，2.48V→左FN键；
								CH5：0V→右MT键，0.82V→R3(固定)，1.65V→Ext右扳机，2.48V→右FN键。
							</small>
						</Col>
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
			<Card style={{ marginBottom: '1rem' }}>
				<Card.Header>按键交换</Card.Header>
				<Card.Body>
					<Row className="g-3">
						{swapGpioPins.map(({ pin, label, pinKey }) => {
							const pinData = pins[pinKey] || defaultPinData;
							return (
								<Col sm={6} md={6} key={`swap-gpio-${pin}`}>
									<div className="d-flex align-items-center">
										<div className="d-flex flex-shrink-0" style={{ width: '8rem' }}>
											<label>{label}</label>
										</div>
										<CustomSelect
											isClearable
											isMulti={!isDisabled(pinData.action) && 
												// Disable multi-select for keyboard keys
												!keyboardKeyOptions.some(opt => opt.value === pinData.action) &&
												// Disable multi-select for action types (non-button actions)
												!options.some(opt => opt.value === pinData.action && opt.type === 'action')}
											options={groupedOptions}
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
					{SaveButtonSection}
				</Card.Body>
			</Card>
		</div>
	);
}

