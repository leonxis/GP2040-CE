import { InputModeDeviceType } from '@proto/enums';

// 过滤后的输入模式选项（仅包含用户指定的8个选项），需要包含完整的配置信息
export const HML_INPUT_MODES = [
	{
		labelKey: 'input-mode-options.xinput',
		value: 0,
		authentication: ['none', 'usb'],
		deviceTypes: [
			InputModeDeviceType.INPUT_MODE_DEVICE_TYPE_GAMEPAD,
			InputModeDeviceType.INPUT_MODE_DEVICE_TYPE_WHEEL,
			InputModeDeviceType.INPUT_MODE_DEVICE_TYPE_GUITAR,
			InputModeDeviceType.INPUT_MODE_DEVICE_TYPE_DRUM,
		],
	},
	{ labelKey: 'input-mode-options.xbone', value: 5 },
	{ labelKey: 'input-mode-options.ps3', value: 2 },
	{
		labelKey: 'input-mode-options.ps4',
		value: 4,
		authentication: ['none', 'key', 'usb'],
		deviceTypes: [
			InputModeDeviceType.INPUT_MODE_DEVICE_TYPE_GAMEPAD,
			InputModeDeviceType.INPUT_MODE_DEVICE_TYPE_WHEEL,
			InputModeDeviceType.INPUT_MODE_DEVICE_TYPE_HOTAS,
			InputModeDeviceType.INPUT_MODE_DEVICE_TYPE_GUITAR,
			InputModeDeviceType.INPUT_MODE_DEVICE_TYPE_DRUM,
		],
	},
	{
		labelKey: 'input-mode-options.ps4b',
		value: 17,
		authentication: ['none'],
		deviceTypes: [
			InputModeDeviceType.INPUT_MODE_DEVICE_TYPE_GAMEPAD,
		],
	},
	{
		labelKey: 'input-mode-options.p5general',
		value: 16,
		authentication: ['usb'],
	},
	{ labelKey: 'input-mode-options.nintendo-switch-pro', value: 15 },
	{ labelKey: 'input-mode-options.keyboard', value: 3 },
	{ labelKey: 'input-mode-options.generic', value: 14 },
];

export const AUTHENTICATION_TYPES = [
	{ labelKey: 'input-mode-authentication.none', value: 0 },
	{ labelKey: 'input-mode-authentication.key', value: 1 },
	{ labelKey: 'input-mode-authentication.usb', value: 2 },
	{ labelKey: 'input-mode-authentication.i2c', value: 3 },
];

export const PS4_ID_MODES = [
	{ labelKey: 'ps4-id-mode-options.console', value: 0 },
	{ labelKey: 'ps4-id-mode-options.emulation', value: 1 },
];

export const TABS = [
	{ key: 'mode', label: '模式设置' },
	{ key: 'back-buttons', label: '背键映射' },
	{ key: 'calibration', label: '校准设置' },
	{ key: 'motion', label: '体感设置' },
	{ key: 'function-buttons', label: '其他功能' },
	{ key: 'hardware', label: '硬件配置' },
	{ key: 'backup-reset', label: '备份重置' },
];

