import { invert } from 'lodash';
import { MultiValue, SingleValue } from 'react-select';

import { MaskPayload } from '../../../Store/useProfilesStore';
import { BUTTON_ACTIONS, PinActionValues } from '../../../Data/Pins';
import {
	OptionType,
	isDisabled,
	mappingOptions,
	mouseKeyOptions,
	keyboardKeyOptions,
} from './ActionMappingOptions';

/** HML 按键交换表格行：可映射引脚，或仅展示的锁定行（如 PS 键）。 */
export type SwapPinRow =
	| { rowId: string; labelKey: string; pinKey: string }
	| { rowId: string; labelKey: string; selectDisabled: true };

export const defaultPinData: MaskPayload = {
	action: BUTTON_ACTIONS.NONE,
	customButtonMask: 0,
	customDpadMask: 0,
};

export function getMultiValue(pinData: MaskPayload) {
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

	const keyboardOption = keyboardKeyOptions.find((opt) => opt.value === pinData.action);
	if (keyboardOption) {
		return [keyboardOption];
	}
	const mouseOption = mouseKeyOptions.find((opt) => opt.value === pinData.action);
	if (mouseOption) {
		return [mouseOption];
	}

	return pinData.action === BUTTON_ACTIONS.CUSTOM_BUTTON_COMBO
		? mappingOptions.filter(
				({ type, customButtonMask, customDpadMask }) =>
					(pinData.customButtonMask & customButtonMask && type === 'customButtonMask') ||
					(pinData.customDpadMask & customDpadMask && type === 'customDpadMask'),
			)
		: mappingOptions.filter((option) => option.value === pinData.action);
}

/** 从下拉选中值生成 MaskPayload（与背键/按键交换共用） */
export function getPayloadFromSelected(
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

export function getPinKey(pin: number) {
	return `pin${pin < 10 ? '0' : ''}${pin}`;
}

export function toMaskPayload(
	m: { action?: number; customButtonMask?: number; customDpadMask?: number } | undefined,
): MaskPayload {
	return m
		? {
				action: (m.action ?? BUTTON_ACTIONS.NONE) as PinActionValues,
				customButtonMask: m.customButtonMask ?? 0,
				customDpadMask: m.customDpadMask ?? 0,
			}
		: defaultPinData;
}
