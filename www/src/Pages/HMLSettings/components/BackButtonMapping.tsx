import * as React from 'react';
import { Card, Row, Col, Button } from 'react-bootstrap';
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

/** Minimal AppContext shape used by this component (full context is defined in JS). */
type AppContextShape = {
	updateUsedPins?: () => void | Promise<void>;
	buttonLabels?: { buttonLabelType?: string; swapTpShareLabels?: boolean };
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
	return (
		action >= BUTTON_ACTIONS.KEYBOARD_KEY_A &&
		action <= BUTTON_ACTIONS.KEYBOARD_KEY_9
	);
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

// Mouse button options (only in back-key mapping; GPIO pin mapping does not show these)
const mouseButtonValues = BUTTON_ACTIONS as Record<string, number>;
const mouseKeyOptions: OptionType[] = [
	{ label: 'MOUSE_LEFT_BUTTON', value: (mouseButtonValues.MOUSE_LEFT_BUTTON ?? 170) as PinActionValues, type: 'mouse', customButtonMask: 0, customDpadMask: 0 },
	{ label: 'MOUSE_RIGHT_BUTTON', value: (mouseButtonValues.MOUSE_RIGHT_BUTTON ?? 171) as PinActionValues, type: 'mouse', customButtonMask: 0, customDpadMask: 0 },
	{ label: 'MOUSE_MIDDLE_BUTTON', value: (mouseButtonValues.MOUSE_MIDDLE_BUTTON ?? 172) as PinActionValues, type: 'mouse', customButtonMask: 0, customDpadMask: 0 },
];

// 背键映射页 Actions 组仅保留：功能键(FN)、连发、宏1～宏6、菜单返回（其余在 GPIO 引脚映射页使用）
const BACK_KEY_ALLOWED_ACTIONS = new Set([
	'BUTTON_PRESS_FN',
	'BUTTON_PRESS_TURBO',
	'BUTTON_PRESS_MACRO_1',
	'BUTTON_PRESS_MACRO_2',
	'BUTTON_PRESS_MACRO_3',
	'BUTTON_PRESS_MACRO_4',
	'BUTTON_PRESS_MACRO_5',
	'BUTTON_PRESS_MACRO_6',
	'MENU_NAVIGATION_BACK',
]);

const groupedOptions = [
	{
		label: 'Buttons',
		options: options.filter(({ type }) => type !== 'action'),
	},
	{
		label: 'Actions',
		options: options.filter((opt) => opt.type === 'action' && BACK_KEY_ALLOWED_ACTIONS.has(opt.label)),
	},
	{
		label: 'Keyboard Keys',
		options: keyboardKeyOptions,
	},
	{
		label: 'Mouse',
		options: mouseKeyOptions,
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
	// Check if it's a mouse button
	const mouseOption = mouseKeyOptions.find((opt) => opt.value === pinData.action);
	if (mouseOption) {
		return [mouseOption];
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
	// 由硬件配置-2键触摸板开关决定触摸板映射栏显示内容
	const [twoKeyTouchpadEnabled, setTwoKeyTouchpadEnabled] = useState(false);
	// 2键触摸板映射（默认 NONE，固件侧由 BoardConfig 的 HML_TWOKEY_LEFT_ACTION/HML_TWOKEY_RIGHT_ACTION 配置）
	const [twoKeyOptions, setTwoKeyOptions] = useState<Record<string, MaskPayload>>({
		leftKey:  { action: BUTTON_ACTIONS.NONE as PinActionValues, customButtonMask: 0, customDpadMask: 0 },
		rightKey: { action: BUTTON_ACTIONS.NONE as PinActionValues, customButtonMask: 0, customDpadMask: 0 },
	});
	const [twoKeySaveMsg, setTwoKeySaveMsg] = useState('');
	const [twoKeySaving, setTwoKeySaving] = useState(false);
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

	// 背键设置插件：逻辑背键映射（不对应 GPIO，供插件读取）
	const [backAddonOptions, setBackAddonOptions] = useState<Record<string, MaskPayload>>({
		leftBack1:  { action: BUTTON_ACTIONS.NONE, customButtonMask: 0, customDpadMask: 0 },
		rightBack1: { action: BUTTON_ACTIONS.NONE, customButtonMask: 0, customDpadMask: 0 },
		leftBack2:  { action: BUTTON_ACTIONS.NONE, customButtonMask: 0, customDpadMask: 0 },
		rightBack2: { action: BUTTON_ACTIONS.NONE, customButtonMask: 0, customDpadMask: 0 },
	});

	const defaultPinData: MaskPayload = {
		action: BUTTON_ACTIONS.NONE,
		customButtonMask: 0,
		customDpadMask: 0,
	};

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
		const { buttonLabels } = appContext as AppContextShape;
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
			// Handle keyboard keys
			if (option.type === 'keyboard') {
				const keyName = option.label?.replace('KEYBOARD_KEY_', '');
				if (keyName === 'ALT_F4') {
					return 'KB: Alt+F4';
				}
				// Add 'KB: ' prefix to distinguish keyboard keys from gamepad buttons
				return `KB: ${keyName || option.label}`;
			}
			// Handle mouse buttons (use Proto translation)
			if (option.type === 'mouse') {
				return t(`Proto:GpioAction.${option.label}`);
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

	// 进入背键映射时刷新：GPIO 映射 + 2键触摸板/FN 映射
	React.useEffect(() => {
		useProfilesStore.getState().fetchProfiles();
		const toPayload = (m: { action?: number; customButtonMask?: number; customDpadMask?: number } | undefined): MaskPayload =>
			m ? { action: (m.action ?? BUTTON_ACTIONS.NONE) as PinActionValues, customButtonMask: m.customButtonMask ?? 0, customDpadMask: m.customDpadMask ?? 0 } : defaultPinData;
		Promise.all([WebApi.getTwoKeyTouchpadOptions(), WebApi.getFnKeyMappingOptions(), WebApi.getBackButtonAddonOptions()]).then(([twoKey, fn, backAddon]) => {
			if (twoKey) {
				setTwoKeyTouchpadEnabled(Boolean(twoKey.enabled));
				setTwoKeyOptions({
					leftKey:  twoKey.leftKey  ? toPayload(twoKey.leftKey)  : { action: BUTTON_ACTIONS.NONE as PinActionValues, customButtonMask: 0, customDpadMask: 0 },
					rightKey: twoKey.rightKey ? toPayload(twoKey.rightKey) : { action: BUTTON_ACTIONS.NONE as PinActionValues, customButtonMask: 0, customDpadMask: 0 },
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
			if (backAddon) {
				setBackAddonOptions({
					leftBack1:  toPayload(backAddon.leftBack1),
					rightBack1: toPayload(backAddon.rightBack1),
					leftBack2:  toPayload(backAddon.leftBack2),
					rightBack2: toPayload(backAddon.rightBack2),
				});
			}
		});
	}, []);

	const handleSaveTwoKey = useCallback(async () => {
		setTwoKeySaveMsg('');
		setTwoKeySaving(true);
		try {
			await WebApi.setTwoKeyTouchpadOptions({
				leftKey:  twoKeyOptions.leftKey,
				rightKey: twoKeyOptions.rightKey,
			});
			setTwoKeySaveMsg(t('Common:saved-success-message'));
			setTimeout(() => setTwoKeySaveMsg(''), 3000);
		} catch (e) {
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

	// Back/面向 FN 的一次性保存：确保背键插件（右背键2）与 FN 键映射（Ext右扳机）能随同保存生效。
	const handleSaveBackCard = useCallback(async () => {
		setSaveMessage('');
		setIsLoading(true);
		try {
			// 1) 保存 GPIO 引脚映射（与 profile 相关）
			await saveProfiles();

			// 2) 保存逻辑背键映射（插件：rightBack2 等）
			await WebApi.setBackButtonAddonOptions({
				leftBack1: backAddonOptions.leftBack1,
				rightBack1: backAddonOptions.rightBack1,
				leftBack2: backAddonOptions.leftBack2,
				rightBack2: backAddonOptions.rightBack2,
			});

			// 3) 保存 FN 键映射（插件：extRightTrigger 等）
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

	// 生成pinKey的工具函数
	const getPinKey = (pin: number) => `pin${pin < 10 ? '0' : ''}${pin}`;

	// 背键映射的GPIO引脚列表（仅保留物理 EL/ER，逻辑背键1/2 由“背键映射（插件）”管理）
	// - 左背键EL=GPIO25
	// - 右背键ER=GPIO24
	const gpioPins = [25, 24];
	
	const backGpioLabelKey: Record<number, string> = {
		25: 'hml-paddle-left-el',
		24: 'hml-paddle-right-er',
	};

	const swapGpioPins = [
		{ pin: 18, labelKey: 'hml-pin-share', pinKey: getPinKey(18) },
		{ pin: 19, labelKey: 'hml-pin-options', pinKey: getPinKey(19) },
		{ pin: 8, labelKey: 'hml-pin-ps', pinKey: getPinKey(8) },
		{ pin: 12, labelKey: 'hml-pin-touchpad', pinKey: getPinKey(12) },
		{ pin: 16, labelKey: 'hml-pin-mouse-left', pinKey: getPinKey(16) },
		{ pin: 17, labelKey: 'hml-pin-mouse-right', pinKey: getPinKey(17) },
		{ pin: 23, labelKey: 'hml-pin-up', pinKey: getPinKey(23) },
		{ pin: 7, labelKey: 'hml-pin-down', pinKey: getPinKey(7) },
		{ pin: 9, labelKey: 'hml-pin-circle', pinKey: getPinKey(9) },
		{ pin: 15, labelKey: 'hml-pin-cross', pinKey: getPinKey(15) },
		{ pin: 14, labelKey: 'hml-pin-triangle', pinKey: getPinKey(14) },
		{ pin: 13, labelKey: 'hml-pin-square', pinKey: getPinKey(13) },
		{ pin: 22, labelKey: 'hml-pin-l1', pinKey: getPinKey(22) },
		{ pin: 21, labelKey: 'hml-pin-r1', pinKey: getPinKey(21) },
		{ pin: 29, labelKey: 'hml-pin-l2', pinKey: getPinKey(29) },
		{ pin: 28, labelKey: 'hml-pin-r2', pinKey: getPinKey(28) },
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
											isMulti={!isDisabled(pinData.action) &&
												!keyboardKeyOptions.some(opt => opt.value === pinData.action) &&
												!mouseKeyOptions.some(opt => opt.value === pinData.action) &&
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
					{/* 逻辑背键映射（插件使用）：与 GPIO 解耦，仅持久化动作 */}
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
											isMulti={!isDisabled(mappingData.action) &&
												!keyboardKeyOptions.some((opt) => opt.value === mappingData.action) &&
												!mouseKeyOptions.some((opt) => opt.value === mappingData.action) &&
												!options.some((opt) => opt.value === mappingData.action && opt.type === 'action')}
											options={groupedOptions}
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
							<Button
								variant="primary"
								onClick={handleSaveBackCard}
								disabled={isLoading}
								className="me-3"
							>
								{t('Common:button-save-label')}
							</Button>
							{saveMessage && (
								<span
									className={`me-3 ${
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
				{/* 2键触摸板开启：显示左触摸键/右触摸键（默认L3/R3） */}
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
												isMulti={!isDisabled(mappingData.action) &&
													!keyboardKeyOptions.some((opt) => opt.value === mappingData.action) &&
													!mouseKeyOptions.some((opt) => opt.value === mappingData.action) &&
													!options.some((opt) => opt.value === mappingData.action && opt.type === 'action')}
												options={groupedOptions}
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
									<span className={`ms-3 ${twoKeySaveMsg === t('Common:saved-success-message') ? 'text-success' : 'text-danger'}`}>
										{twoKeySaveMsg}
									</span>
								)}
							</Col>
						</Row>
					</>
				)}

				{/* 2键触摸板开关关闭：显示禁用提示 */}
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
											isMulti={!isDisabled(mappingData.action) &&
												!keyboardKeyOptions.some((opt) => opt.value === mappingData.action) &&
												!mouseKeyOptions.some((opt) => opt.value === mappingData.action) &&
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
				<Card.Header>{t('SettingsPage:hml-key-swap-title')}</Card.Header>
				<Card.Body>
					<Row className="g-3">
						{swapGpioPins.map(({ pin, labelKey, pinKey }) => {
							const pinData = pins[pinKey] || defaultPinData;
							return (
								<Col sm={6} md={6} key={`swap-gpio-${pin}`}>
									<div className="d-flex align-items-center">
										<div className="d-flex flex-shrink-0" style={{ width: '8rem' }}>
											<label>{t(`CalibrationSettings:${labelKey}`)}</label>
										</div>
										<CustomSelect
											isClearable
											isMulti={!isDisabled(pinData.action) &&
												!keyboardKeyOptions.some(opt => opt.value === pinData.action) &&
												!mouseKeyOptions.some(opt => opt.value === pinData.action) &&
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

