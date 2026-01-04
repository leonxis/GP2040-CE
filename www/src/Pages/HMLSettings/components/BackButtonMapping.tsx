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
	// Keyboard key actions range from KEYBOARD_KEY_A (131) to KEYBOARD_KEY_ALT_F4 (159)
	return action >= BUTTON_ACTIONS.KEYBOARD_KEY_A && 
	       action <= BUTTON_ACTIONS.KEYBOARD_KEY_ALT_F4;
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

export default function BackButtonMapping() {
	const { t } = useTranslation();
	const appContext = useContext(AppContext);
	const setProfilePin = useProfilesStore((state) => state.setProfilePin);
	const saveProfiles = useProfilesStore((state) => state.saveProfiles);
	const [saveMessage, setSaveMessage] = useState('');
	const [isLoading, setIsLoading] = useState(false);

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
				// Handle clearing
				if (!selected || (Array.isArray(selected) && !selected.length)) {
					setProfilePin(0, pin, {
						action: BUTTON_ACTIONS.NONE,
						customButtonMask: 0,
						customDpadMask: 0,
					});
				} else if (Array.isArray(selected) && selected.length > 1) {
					const lastSelected = selected[selected.length - 1];
					// Revert to single option if choosing action type or keyboard type
					if (lastSelected.type === 'action' || lastSelected.type === 'keyboard') {
						setProfilePin(0, pin, {
							action: lastSelected.value,
							customButtonMask: 0,
							customDpadMask: 0,
						});
					} else {
						setProfilePin(
							0,
							pin,
							selected.reduce(
								(masks, option) => ({
									...masks,
									customButtonMask:
										option.type === 'customButtonMask'
											? masks.customButtonMask ^ option.customButtonMask
											: masks.customButtonMask,
									customDpadMask:
										option.type === 'customDpadMask'
											? masks.customDpadMask ^ option.customDpadMask
											: masks.customDpadMask,
								}),
								{
									action: BUTTON_ACTIONS.CUSTOM_BUTTON_COMBO,
									customButtonMask: 0,
									customDpadMask: 0,
								},
							),
						);
					}
				} else {
					const singleSelected = Array.isArray(selected) ? selected[0] : selected;
					setProfilePin(0, pin, {
						action: singleSelected.value,
						customButtonMask: 0,
						customDpadMask: 0,
					});
				}
			},
		[setProfilePin],
	);

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

	// 背键映射的GPIO引脚列表
	const gpioPins = [15, 14, 22, 25];
	
	// 背键GPIO引脚标签映射
	const backButtonLabels: Record<number, string> = {
		14: '右背键1（GPIO14）',
		15: '左背键1（GPIO15）',
		22: '左背键2（GPIO22）',
		25: '右背键2（GPIO25）',
	};
	
	// 按键交换的GPIO引脚列表
	const swapGpioPins = [
		{ pin: 2, label: '右', pinKey: getPinKey(2) },
		{ pin: 3, label: '左', pinKey: getPinKey(3) },
		{ pin: 12, label: '下', pinKey: getPinKey(12) },
		{ pin: 13, label: '上', pinKey: getPinKey(13) },
		{ pin: 10, label: '圆', pinKey: getPinKey(10) },
		{ pin: 4, label: '叉', pinKey: getPinKey(4) },
		{ pin: 5, label: '三角', pinKey: getPinKey(5) },
		{ pin: 9, label: '方块', pinKey: getPinKey(9) },
		{ pin: 19, label: 'L1', pinKey: getPinKey(19) },
		{ pin: 18, label: 'L2', pinKey: getPinKey(18) },
		{ pin: 6, label: 'R1', pinKey: getPinKey(6) },
		{ pin: 7, label: 'R2', pinKey: getPinKey(7) },
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
												pinData.action !== BUTTON_ACTIONS.CUSTOM_BUTTON_COMBO && 
												!keyboardKeyOptions.some(opt => opt.value === pinData.action)}
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
				<Card.Header>按键交换</Card.Header>
				<Card.Body>
					<Row className="g-3">
						{swapGpioPins.map(({ pin, label, pinKey }) => {
							const pinData = pins[pinKey] || defaultPinData;
							return (
								<Col sm={6} md={6} key={`swap-gpio-${pin}`}>
									<div className="d-flex align-items-center">
										<div className="d-flex flex-shrink-0" style={{ width: '8rem' }}>
											<label>{label}（GPIO{pin}）</label>
										</div>
										<CustomSelect
											isClearable
											isMulti={!isDisabled(pinData.action) && 
												pinData.action !== BUTTON_ACTIONS.CUSTOM_BUTTON_COMBO && 
												!keyboardKeyOptions.some(opt => opt.value === pinData.action)}
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

