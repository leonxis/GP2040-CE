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

// SHMUP 自动连发按键（None + 10 游戏按键，含 L3/R3），label 为逻辑按键名，运行时按显示模式翻译
const TURBO_MASKS = [
	{ label: 'None', value: 0 },
	{ label: 'B1', value: 1 << 0 },
	{ label: 'B2', value: 1 << 1 },
	{ label: 'B3', value: 1 << 2 },
	{ label: 'B4', value: 1 << 3 },
	{ label: 'L1', value: 1 << 4 },
	{ label: 'R1', value: 1 << 5 },
	{ label: 'L2', value: 1 << 6 },
	{ label: 'R2', value: 1 << 7 },
	{ label: 'L3', value: 1 << 10 },
	{ label: 'R3', value: 1 << 11 },
];

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

// 判定是否存在连发键：基础映射槽标准 GPIO 或当前背键 preset 任意键映射为 BUTTON_PRESS_TURBO
// 返回 null=加载中/失败（不显示提示），false=无连发键（显示提示），true=有连发键
function useHasTurboKey(): boolean | null {
	const [hasTurboKey, setHasTurboKey] = useState<boolean | null>(null);

	useEffect(() => {
		let cancelled = false;
		(async () => {
			try {
				let found = false;
				// 标准 GPIO 基础映射槽
				const pins = await WebApi.getPinMappings();
				if (!cancelled && pins) {
					found = Object.keys(pins)
						.filter((k) => /^pin\d{2}$/.test(k))
						.some((k) => Number(pins[k]?.action) === BUTTON_ACTIONS.BUTTON_PRESS_TURBO);
				}
				// 背键插件当前 preset 10 键
				if (!found && !cancelled) {
					const global = await WebApi.getTwoKeyTouchpadOptions();
					if (global) {
						const activePreset = Number(global.activePreset ?? 0);
						const preset = await WebApi.getTwoKeyTouchpadOptions(activePreset);
						if (preset) {
							found = BACK_KEY_FIELDS.some(
								(f) => Number(preset[f]?.action) === BUTTON_ACTIONS.BUTTON_PRESS_TURBO,
							);
						}
					}
				}
				if (!cancelled) setHasTurboKey(found);
			} catch {
				if (!cancelled) setHasTurboKey(null);
			}
		})();
		return () => {
			cancelled = true;
		};
	}, []);

	return hasTurboKey;
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
	const hasTurboKey = useHasTurboKey();
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
										{TURBO_MASKS.map((o, i) => (
											<option key={`hml-turbo-mask-${n}-${i}`} value={o.value}>
												{o.label === 'None' ? o.label : buttonNames[o.label]}
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
