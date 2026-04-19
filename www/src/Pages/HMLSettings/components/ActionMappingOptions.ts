import { BUTTON_MASKS, DPAD_MASKS } from '../../../Data/Buttons';
import { BUTTON_ACTIONS, PinActionValues } from '../../../Data/Pins';

export type OptionType = {
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

export const isNonSelectable = (action: PinActionValues) =>
	[
		BUTTON_ACTIONS.NONE,
		BUTTON_ACTIONS.CUSTOM_BUTTON_COMBO,
		...disabledOptions,
	].includes(action);

export const isDisabled = (action: PinActionValues) =>
	disabledOptions.includes(action);

const isKeyboardKey = (action: PinActionValues) => {
	return (
		action >= BUTTON_ACTIONS.KEYBOARD_KEY_A &&
		action <= BUTTON_ACTIONS.KEYBOARD_KEY_9
	);
};

export const mappingOptions: OptionType[] = Object.entries(BUTTON_ACTIONS)
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

export const keyboardKeyOptions: OptionType[] = [
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

const mouseButtonValues = BUTTON_ACTIONS as Record<string, number>;
export const mouseKeyOptions: OptionType[] = [
	{ label: 'MOUSE_LEFT_BUTTON', value: (mouseButtonValues.MOUSE_LEFT_BUTTON ?? 170) as PinActionValues, type: 'mouse', customButtonMask: 0, customDpadMask: 0 },
	{ label: 'MOUSE_RIGHT_BUTTON', value: (mouseButtonValues.MOUSE_RIGHT_BUTTON ?? 171) as PinActionValues, type: 'mouse', customButtonMask: 0, customDpadMask: 0 },
	{ label: 'MOUSE_MIDDLE_BUTTON', value: (mouseButtonValues.MOUSE_MIDDLE_BUTTON ?? 172) as PinActionValues, type: 'mouse', customButtonMask: 0, customDpadMask: 0 },
];

export const BACK_KEY_ALLOWED_ACTIONS = new Set([
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

export const groupedMappingOptions = [
	{
		label: 'Buttons',
		options: mappingOptions.filter(({ type }) => type !== 'action'),
	},
	{
		label: 'Actions',
		options: mappingOptions.filter((opt) => opt.type === 'action' && BACK_KEY_ALLOWED_ACTIONS.has(opt.label)),
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
