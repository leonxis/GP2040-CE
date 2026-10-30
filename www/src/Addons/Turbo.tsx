import { useContext, useEffect, useMemo, useState } from 'react';
import { Button, Col, Form, Row } from 'react-bootstrap';
import { useFormikContext } from 'formik';
import { useTranslation } from 'react-i18next';
import * as yup from 'yup';

import Section from '../Components/Section';
import { AppContext } from '../Contexts/AppContext';
import { BUTTON_ACTIONS } from '../Data/Pins';
import { getButtonLabels } from '../Data/Buttons';
import WebApi from '../Services/WebApi';

// SHMUP 常开连发选项来源：BoardConfig 中映射为游戏按键的 GPIO 引脚。
// 选项值 = GPIO 引脚号，显示名沿用该引脚对应逻辑按键的本地化名称
const SHMUP_PIN_ACTIONS = [
	{ action: BUTTON_ACTIONS.BUTTON_PRESS_B1, labelKey: 'B1' },
	{ action: BUTTON_ACTIONS.BUTTON_PRESS_B2, labelKey: 'B2' },
	{ action: BUTTON_ACTIONS.BUTTON_PRESS_B3, labelKey: 'B3' },
	{ action: BUTTON_ACTIONS.BUTTON_PRESS_B4, labelKey: 'B4' },
	{ action: BUTTON_ACTIONS.BUTTON_PRESS_L1, labelKey: 'L1' },
	{ action: BUTTON_ACTIONS.BUTTON_PRESS_R1, labelKey: 'R1' },
	{ action: BUTTON_ACTIONS.BUTTON_PRESS_L2, labelKey: 'L2' },
	{ action: BUTTON_ACTIONS.BUTTON_PRESS_R2, labelKey: 'R2' },
	{ action: BUTTON_ACTIONS.BUTTON_PRESS_L3, labelKey: 'L3' },
	{ action: BUTTON_ACTIONS.BUTTON_PRESS_R3, labelKey: 'R3' },
];

interface ShmupPinOption {
	value: number; // GPIO 引脚号
	labelKey: string; // 逻辑按键名，用于本地化显示
}

// 背键插件 10 键（与后端 kHmlBackKeyDefs 字段一致）
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

// 读取当前映射：SHMUP 常开选项（游戏按键对应的 GPIO 引脚）+ 是否存在连发键
// hasTurboKey 返回 null=加载中/失败（不显示提示），false=无连发键（显示提示），true=有连发键
function useTurboPinData(): {
	hasTurboKey: boolean | null;
	shmupOptions: ShmupPinOption[];
} {
	const [hasTurboKey, setHasTurboKey] = useState<boolean | null>(null);
	const [shmupOptions, setShmupOptions] = useState<ShmupPinOption[]>([]);

	useEffect(() => {
		let cancelled = false;
		(async () => {
			try {
			let found = false;
			let anyFailed = false;
			const options: ShmupPinOption[] = [];
			// 标准 GPIO 基础映射槽
			const pins = await WebApi.getPinMappings();
			if (pins) {
				const pinKeys = Object.keys(pins).filter((k) => /^pin\d{2}$/.test(k));
				found = pinKeys.some(
					(k) => Number(pins[k]?.action) === BUTTON_ACTIONS.BUTTON_PRESS_TURBO,
				);
				// 按 BoardConfig 的 GPIO→按键映射收集常开选项，值取引脚号
				for (const { action, labelKey } of SHMUP_PIN_ACTIONS) {
					for (const k of pinKeys) {
						if (Number(pins[k]?.action) !== action) continue;
						const pin = Number(k.slice(3));
						if (pin > 0) options.push({ value: pin, labelKey });
					}
				}
			} else {
				anyFailed = true;
			}
			// 背键插件当前 preset 10 键
			if (!found && !anyFailed) {
				const global = await WebApi.getTwoKeyTouchpadOptions();
				if (global) {
					const activePreset = Number(global.activePreset ?? 0);
					const preset = await WebApi.getTwoKeyTouchpadOptions(activePreset);
					if (preset) {
						found = BACK_KEY_FIELDS.some(
							(f) => Number(preset[f]?.action) === BUTTON_ACTIONS.BUTTON_PRESS_TURBO,
						);
					} else {
						anyFailed = true;
					}
				} else {
					anyFailed = true;
				}
			}
			if (!cancelled) {
				setHasTurboKey(anyFailed ? null : found);
				setShmupOptions(options);
			}
		} catch {
			if (!cancelled) setHasTurboKey(null);
		}
		})();
		return () => {
			cancelled = true;
		};
	}, []);

	return { hasTurboKey, shmupOptions };
}

export const turboHmlScheme = {
	TurboInputEnabled: yup.number().min(0).max(1),
	turboShotCount: yup.number().min(1).max(50),
	turboLongPressEnabled: yup.number().min(0).max(1),
	shmupMode: yup.number().min(0).max(1),
	shmupAlwaysOn1: yup.number(),
	shmupAlwaysOn2: yup.number(),
	shmupAlwaysOn3: yup.number(),
	shmupAlwaysOn4: yup.number(),
};

export const turboHmlState = {
	TurboInputEnabled: 0,
	turboShotCount: 10,
	turboLongPressEnabled: 0,
	shmupMode: 0,
	shmupAlwaysOn1: 0,
	shmupAlwaysOn2: 0,
	shmupAlwaysOn3: 0,
	shmupAlwaysOn4: 0,
};

interface TurboSettingsProps {
	values: Record<string, unknown>;
	setFieldValue: (field: string, value: unknown) => void;
	saveMessage?: string;
	onSaveClick?: () => void;
}

export default function TurboSettings({
	values,
	setFieldValue,
	saveMessage = '',
	onSaveClick,
}: TurboSettingsProps) {
	const { t } = useTranslation();
	const { handleSubmit } = useFormikContext();
	const { buttonLabels } = useContext(AppContext);
	const { hasTurboKey, shmupOptions } = useTurboPinData();
	const saveOk = saveMessage === t('Common:saved-success-message');
	const enabled = Boolean(values?.TurboInputEnabled);

	// 按当前显示模式（ps4/xinput/switch 等）翻译按键名
	const buttonNames = useMemo(
		() => getButtonLabels(buttonLabels?.buttonLabelType, buttonLabels?.swapTpShareLabels),
		[buttonLabels],
	);

	return (
		<Section title={t('SettingsPage:hml-section-turbo')}>
			{enabled ? (
				<div style={{ display: 'flex', flexDirection: 'column', gap: '16px', alignItems: 'flex-start' }}>
					<div style={{ display: 'flex', alignItems: 'center', gap: '12px' }}>
						<Form.Check
							type="switch"
							id="hml-turbo-longpress"
							label={t('AddonsConfig:turbo-longpress-label')}
							checked={Boolean(values.turboLongPressEnabled)}
							onChange={(e) =>
								setFieldValue('turboLongPressEnabled', e.target.checked ? 1 : 0)
							}
						/>
						<span className="text-muted">
							{t('AddonsConfig:turbo-longpress-hint')}
						</span>
					</div>
					<div style={{ display: 'flex', alignItems: 'center', gap: '12px' }}>
						<Form.Check
							type="switch"
							id="hml-turbo-shmup"
							label={t('AddonsConfig:turbo-shmup-mode-label')}
							checked={Boolean(values.shmupMode)}
							onChange={(e) =>
								setFieldValue('shmupMode', e.target.checked ? 1 : 0)
							}
						/>
						<span className="text-muted">
							{t('AddonsConfig:turbo-shmup-hint')}
						</span>
					</div>
					<div>
						<Form.Label style={{ whiteSpace: 'nowrap' }}>
							{t('AddonsConfig:turbo-shot-count-label')}
							<span className="text-muted ms-1">
								（{t('AddonsConfig:turbo-shot-count-hint')}）
							</span>
						</Form.Label>
						<div style={{ width: '250px' }}>
							<Form.Control
								type="number"
								size="sm"
								min={1}
								max={50}
								value={(values.turboShotCount as number) ?? 10}
								onChange={(e) =>
									setFieldValue('turboShotCount', parseInt(e.target.value))
								}
							/>
						</div>
					</div>
					{values?.shmupMode ? (
						<Row>
							{[1, 2, 3, 4].map((n) => (
								<Col sm={6} md={3} key={`hml-turbo-alwayson-${n}`}>
									<Form.Label>
										{t(`AddonsConfig:turbo-shmup-always-on-${n}-label`)}
									</Form.Label>
									<Form.Select
										className="form-select-sm"
										value={(values[`shmupAlwaysOn${n}`] as number) ?? 0}
										onChange={(e) =>
											setFieldValue(`shmupAlwaysOn${n}`, parseInt(e.target.value))
										}
									>
										<option value={0}>None</option>
										{shmupOptions.map((o) => (
											<option key={`hml-turbo-pin-${n}-${o.value}`} value={o.value}>
												{buttonNames[o.labelKey] ?? o.labelKey}
											</option>
										))}
									</Form.Select>
								</Col>
							))}
						</Row>
					) : null}
				</div>
			) : null}
			{enabled && hasTurboKey === false ? (
				<div className="alert alert-warning mt-3 mb-0">
					{t('AddonsConfig:turbo-key-missing-warning')}
				</div>
			) : null}
			<div
				style={{
					marginTop: '16px',
					display: 'flex',
					justifyContent: 'space-between',
					alignItems: 'center',
					width: '100%',
				}}
			>
				<div style={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
					<Button
						variant="primary"
						onClick={(e) => {
							e.preventDefault();
							onSaveClick ? onSaveClick() : handleSubmit();
						}}
					>
						{t('Common:button-save-label')}
					</Button>
					{saveMessage && (
						<span className={saveOk ? 'text-success' : 'text-danger'}>
							{saveMessage}
						</span>
					)}
				</div>
				<Form.Check
					type="switch"
					id="hml-turbo-enabled"
					label={t('AddonsConfig:turbo-enable-label')}
					checked={enabled}
					onChange={(e) =>
						setFieldValue('TurboInputEnabled', e.target.checked ? 1 : 0)
					}
				/>
			</div>
		</Section>
	);
}
