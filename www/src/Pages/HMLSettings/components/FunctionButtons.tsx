import { useNavigate } from 'react-router-dom';
import { Button, Form, Row, Col } from 'react-bootstrap';
import { useTranslation } from 'react-i18next';
import { useState } from 'react';

import Section from '../../../Components/Section';
import { useGamepadOptions } from '../hooks/useGamepadOptions';
import WebApi from '../../../Services/WebApi';

const DPAD_MODES = [
	{ labelKey: 'd-pad-mode-options.d-pad', value: 0 },
	{ labelKey: 'd-pad-mode-options.left-analog', value: 1 },
	{ labelKey: 'd-pad-mode-options.right-analog', value: 2 },
];

export default function FunctionButtons() {
	const navigate = useNavigate();
	const { t } = useTranslation();
	const { values, setValues, isLoading } = useGamepadOptions();
	const [saveMessage, setSaveMessage] = useState('');

	// 翻译方向键模式选项
	const translatedDpadModes = DPAD_MODES.map(({ labelKey, value }) => ({
		label: t(`SettingsPage:${labelKey}`),
		value,
	}));

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


	const handleSave = async () => {
		setSaveMessage('');
		try {
			const success = await WebApi.setGamepadOptions(values);
			if (success) {
				setSaveMessage('保存成功！');
				setTimeout(() => setSaveMessage(''), 3000);
			} else {
				setSaveMessage('保存失败，请重试。');
			}
		} catch (error) {
			console.error('保存失败:', error);
			setSaveMessage('保存失败，请重试。');
		}
	};

	if (isLoading) {
		return (
			<div>
				<Section title="功能按键">
					<p className="text-muted">正在加载...</p>
				</Section>
			</div>
		);
	}

	return (
		<div>
			<Section title="功能按键">
				<div style={{ display: 'flex', flexDirection: 'column', gap: '12px', alignItems: 'flex-start' }}>
					<div style={{ display: 'flex', alignItems: 'center', gap: '12px' }}>
						<Button
							variant="primary"
							onClick={handleHotkeySettings}
							style={{ minWidth: '120px' }}
						>
							热键设置
						</Button>
						<span className="text-muted">
							热键需要在将某个按键设为FN键以实现热键功能。
						</span>
					</div>
					<Button
						variant="primary"
						onClick={handleMacroSettings}
						style={{ minWidth: '120px' }}
					>
						宏设置
					</Button>
				</div>
			</Section>

			<Section title="方向键与摇杆">
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
							保存
						</Button>
						{saveMessage && (
							<span
								className={`ms-3 ${
									saveMessage.includes('成功') ? 'text-success' : 'text-danger'
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

