import { useNavigate } from 'react-router-dom';
import { Button, Form, Row, Col } from 'react-bootstrap';
import { useTranslation } from 'react-i18next';
import { useState, useEffect, useMemo } from 'react';

import Section from '../../../Components/Section';
import { useGamepadOptions } from '../hooks/useGamepadOptions';
import WebApi from '../../../Services/WebApi';

const DPAD_MODES = [
	{ labelKey: 'd-pad-mode-options.d-pad', value: 0 },
	{ labelKey: 'd-pad-mode-options.left-analog', value: 1 },
	{ labelKey: 'd-pad-mode-options.right-analog', value: 2 },
];

// 与功能配置-插件配置-模拟摇杆 一致：左/右摇杆模式、反转（文案见 SettingsPage hml-*）
const ANALOG_STICK_MODES = [
	{ labelKey: 'hml-analog-stick-left', value: 1 },
	{ labelKey: 'hml-analog-stick-right', value: 2 },
];
const INVERT_MODES = [
	{ labelKey: 'hml-invert-none', value: 0 },
	{ labelKey: 'hml-invert-x', value: 1 },
	{ labelKey: 'hml-invert-y', value: 2 },
	{ labelKey: 'hml-invert-xy', value: 3 },
];

export default function FunctionButtons() {
	const navigate = useNavigate();
	const { t } = useTranslation();
	const { values, setValues, isLoading } = useGamepadOptions();
	const [saveMessage, setSaveMessage] = useState('');
	const [addonOptions, setAddonOptions] = useState<any>(null);

	useEffect(() => {
		let cancelled = false;
		async function loadAddons() {
			const data = await WebApi.getAddonsOptions();
			if (!cancelled && data) setAddonOptions(data);
		}
		loadAddons();
		return () => { cancelled = true; };
	}, []);

	// 翻译方向键模式选项
	const translatedDpadModes = DPAD_MODES.map(({ labelKey, value }) => ({
		label: t(`SettingsPage:${labelKey}`),
		value,
	}));

	const translatedAnalogStickModes = useMemo(
		() =>
			ANALOG_STICK_MODES.map(({ labelKey, value }) => ({
				label: t(`SettingsPage:${labelKey}`),
				value,
			})),
		[t],
	);
	const translatedInvertModes = useMemo(
		() =>
			INVERT_MODES.map(({ labelKey, value }) => ({
				label: t(`SettingsPage:${labelKey}`),
				value,
			})),
		[t],
	);

	const handleHotkeySettings = () => {
		// Navigate to settings page with hotkey tab
		navigate('/settings', { state: { activeTab: 'hotkey' } });
	};

	const handleMacroSettings = () => {
		// Navigate to macro configuration page
		navigate('/macro');
	};

	const handleDpadModeChange = (e: React.ChangeEvent<HTMLSelectElement>) => {
		const newValue = parseInt(e.target.value);
		setValues((prev: any) => ({ ...prev, dpadMode: newValue }));
	};

	const handleFourWayModeChange = (e: React.ChangeEvent<HTMLInputElement>) => {
		const newValue = e.target.checked ? 1 : 0;
		setValues((prev: any) => ({ ...prev, fourWayMode: newValue }));
	};

	const handleDpadTriggerThresholdChange = (e: React.ChangeEvent<HTMLInputElement>) => {
		const newValue = parseInt(e.target.value);
		setValues((prev: any) => ({ ...prev, dpadTriggerThreshold: newValue }));
	};

	const handleDpadDeadzoneChange = (e: React.ChangeEvent<HTMLInputElement>) => {
		const newValue = parseInt(e.target.value);
		setValues((prev: any) => ({ ...prev, dpadDeadzone: newValue }));
	};

	const handleLeftStickModeChange = (e: React.ChangeEvent<HTMLSelectElement>) => {
		const v = parseInt(e.target.value);
		setAddonOptions((prev: any) => prev ? { ...prev, analogAdc1Mode: v } : prev);
	};
	const handleRightStickModeChange = (e: React.ChangeEvent<HTMLSelectElement>) => {
		const v = parseInt(e.target.value);
		setAddonOptions((prev: any) => prev ? { ...prev, analogAdc2Mode: v } : prev);
	};
	const handleLeftStickInvertChange = (e: React.ChangeEvent<HTMLSelectElement>) => {
		const v = parseInt(e.target.value);
		setAddonOptions((prev: any) => prev ? { ...prev, analogAdc1Invert: v } : prev);
	};
	const handleRightStickInvertChange = (e: React.ChangeEvent<HTMLSelectElement>) => {
		const v = parseInt(e.target.value);
		setAddonOptions((prev: any) => prev ? { ...prev, analogAdc2Invert: v } : prev);
	};

	const handleSave = async () => {
		setSaveMessage('');
		try {
			const gamepadOk = await WebApi.setGamepadOptions(values);
			const addonOk = addonOptions ? await WebApi.setAddonsOptions(addonOptions) : true;
			if (gamepadOk && addonOk) {
				setSaveMessage(t('SettingsPage:hml-save-success'));
				setTimeout(() => setSaveMessage(''), 3000);
			} else {
				setSaveMessage(t('SettingsPage:hml-save-failed-retry'));
			}
		} catch (error) {
			console.error('保存失败:', error);
			setSaveMessage(t('SettingsPage:hml-save-failed-retry'));
		}
	};

	if (isLoading) {
		return (
			<div>
				<Section title={t('SettingsPage:hml-section-function-buttons')}>
					<p className="text-muted">{t('SettingsPage:hml-loading')}</p>
				</Section>
			</div>
		);
	}

	return (
		<div>
			<Section title={t('SettingsPage:hml-section-function-buttons')}>
				<div style={{ display: 'flex', flexDirection: 'column', gap: '12px', alignItems: 'flex-start' }}>
					<div style={{ display: 'flex', alignItems: 'center', gap: '12px' }}>
						<Button
							variant="primary"
							onClick={handleHotkeySettings}
							style={{ minWidth: '120px' }}
						>
							{t('SettingsPage:hml-hotkey-settings-button')}
						</Button>
						<span className="text-muted">
							{t('SettingsPage:hml-hotkey-settings-hint')}
						</span>
					</div>
					<Button
						variant="primary"
						onClick={handleMacroSettings}
						style={{ minWidth: '120px' }}
					>
						{t('SettingsPage:hml-macro-settings-button')}
					</Button>
				</div>
			</Section>

			<Section title={t('SettingsPage:hml-section-stick-config')}>
				<Form.Group className="mb-3">
					<Row>
						<Col sm={6} md={3}>
							<Form.Label>{t('SettingsPage:hml-stick-mode-left')}</Form.Label>
							<Form.Select
								className="form-select-sm"
								value={addonOptions?.analogAdc1Mode ?? 1}
								onChange={handleLeftStickModeChange}
							>
								{translatedAnalogStickModes.map((o, i) => (
									<option key={`hml-left-mode-${i}`} value={o.value}>{o.label}</option>
								))}
							</Form.Select>
						</Col>
						<Col sm={6} md={3}>
							<Form.Label>{t('SettingsPage:hml-stick-mode-right')}</Form.Label>
							<Form.Select
								className="form-select-sm"
								value={addonOptions?.analogAdc2Mode ?? 2}
								onChange={handleRightStickModeChange}
							>
								{translatedAnalogStickModes.map((o, i) => (
									<option key={`hml-right-mode-${i}`} value={o.value}>{o.label}</option>
								))}
							</Form.Select>
						</Col>
					</Row>
					<Row className="mt-2">
						<Col sm={6} md={3}>
							<Form.Label>{t('SettingsPage:hml-stick-invert-left')}</Form.Label>
							<Form.Select
								className="form-select-sm"
								value={addonOptions?.analogAdc1Invert ?? 0}
								onChange={handleLeftStickInvertChange}
							>
								{translatedInvertModes.map((o, i) => (
									<option key={`hml-left-invert-${i}`} value={o.value}>{o.label}</option>
								))}
							</Form.Select>
						</Col>
						<Col sm={6} md={3}>
							<Form.Label>{t('SettingsPage:hml-stick-invert-right')}</Form.Label>
							<Form.Select
								className="form-select-sm"
								value={addonOptions?.analogAdc2Invert ?? 0}
								onChange={handleRightStickInvertChange}
							>
								{translatedInvertModes.map((o, i) => (
									<option key={`hml-right-invert-${i}`} value={o.value}>{o.label}</option>
								))}
							</Form.Select>
						</Col>
					</Row>
				</Form.Group>
			</Section>

			<Section title={t('SettingsPage:hml-section-dpad-config')}>
				<Form.Group className="mb-3">
					<Form.Label>
						{t('SettingsPage:d-pad-mode-label')}
					</Form.Label>
					<div className="row">
						<Col sm={3}>
							<Form.Select
								name="dpadMode"
								className="form-select-sm"
								value={values.dpadMode ?? 0}
								onChange={handleDpadModeChange}
							>
								{translatedDpadModes.map((o, i) => (
									<option
										key={`hml-dpadMode-option-${i}`}
										value={o.value}
									>
										{o.label}
									</option>
								))}
							</Form.Select>
						</Col>
						<Col sm={3}>
							<Form.Check
								type="switch"
								id="hml-fourWayMode"
								label={t('SettingsPage:4-way-joystick-mode-label')}
								checked={Boolean(values.fourWayMode)}
								onChange={handleFourWayModeChange}
							/>
						</Col>
					</div>
				</Form.Group>
				<Form.Group className="mb-3">
					<div className="row">
						<Col sm={6}>
							<Form.Label>
								{t('SettingsPage:dpad-trigger-threshold-label')} {values.dpadTriggerThreshold ?? 10}%
							</Form.Label>
							<div style={{ marginTop: '8px' }}>
								<Form.Range
									min="0"
									max="90"
									value={values.dpadTriggerThreshold ?? 10}
									onChange={handleDpadTriggerThresholdChange}
									style={{ width: '300px' }}
								/>
							</div>
						</Col>
						<Col sm={6}>
							<Form.Label>
								{t('SettingsPage:dpad-deadzone-label')} {values.dpadDeadzone ?? 10}%
							</Form.Label>
							<div style={{ marginTop: '8px' }}>
								<Form.Range
									min="0"
									max="90"
									value={values.dpadDeadzone ?? 10}
									onChange={handleDpadDeadzoneChange}
									style={{ width: '300px' }}
								/>
							</div>
						</Col>
			</div>
				</Form.Group>
				<Form.Group className="row mb-3">
					<Col sm={4}>
						<Button variant="primary" onClick={handleSave}>
							{t('Common:button-save-label')}
						</Button>
						{saveMessage && (
							<span
								className={`ms-3 ${
									saveMessage === t('SettingsPage:hml-save-success')
										? 'text-success'
										: 'text-danger'
								}`}
							>
								{saveMessage}
							</span>
						)}
					</Col>
				</Form.Group>
		</Section>
		</div>
	);
}

