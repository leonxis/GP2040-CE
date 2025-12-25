import * as React from 'react';
import { Row, Col, Card, Form, Button } from 'react-bootstrap';
import { Trans, useTranslation } from 'react-i18next';
import KeyboardMapper from '../../../Components/KeyboardMapper';
import ContextualHelpOverlay from '../../../Components/ContextualHelpOverlay';
import { HML_INPUT_MODES, AUTHENTICATION_TYPES, PS4_ID_MODES } from '../constants/hmlInputModes';
import { useGamepadOptions } from '../hooks/useGamepadOptions';
import { useKeyMappings } from '../hooks/useKeyMappings';
import { AppContext } from '../../../Contexts/AppContext';
import WebApi from '../../../Services/WebApi';

export default function ModeSettings() {
	const { t } = useTranslation();
	const appContext = React.useContext(AppContext);
	if (!appContext) {
		return null;
	}
	const { buttonLabels, setButtonLabels } = appContext;
	const { values, setValues, inputMode, setInputMode, isLoading, error } =
		useGamepadOptions();
	const { keyMappings, handleKeyChange, getKeyMappingForButton } = useKeyMappings();
	const [saveMessage, setSaveMessage] = React.useState('');

	// 如果正在加载或出错，显示相应信息
	if (isLoading) {
		return (
			<Card>
				<Card.Header>模式设置</Card.Header>
				<Card.Body>
					<p className="text-muted">正在加载...</p>
				</Card.Body>
			</Card>
		);
	}

	if (error) {
		return (
			<Card>
				<Card.Header>模式设置</Card.Header>
				<Card.Body>
					<p className="text-danger">{error}</p>
				</Card.Body>
			</Card>
		);
	}

	// 翻译输入模式选项
	const translatedInputModes = HML_INPUT_MODES.map(({ labelKey, value }) => ({
		label: t(`SettingsPage:${labelKey}`),
		value,
	}));

	// 翻译认证类型
	const translatedInputModeAuthentications = AUTHENTICATION_TYPES.map(
		({ labelKey, value }) => ({
			label: t(`SettingsPage:${labelKey}`),
			value,
		}),
	);

	// 处理字段变化
	const handleChange = (e: React.ChangeEvent<HTMLSelectElement>) => {
		const { name, value } = e.target;
		setValues((prev: any) => ({
			...prev,
			[name]: parseInt(value),
		}));
	};

	// 生成认证选择框
	const generateAuthSelection = (
		inputModeConfig: any,
		label: string,
		name: string,
		value: number | undefined,
		error: any,
		handleChange: (e: React.ChangeEvent<HTMLSelectElement>) => void,
	) => {
		if (!inputModeConfig || !inputModeConfig.authentication) {
			return null;
		}

		return (
			<Row className="mb-3">
				<Col sm={4}>
					<Form.Label>{label}</Form.Label>
					<Form.Select
						name={name}
						className="form-select-sm"
						value={value ?? 0}
						onChange={handleChange}
						isInvalid={error}
					>
						{inputModeConfig.authentication.map((authType: string) => {
							const authOption = AUTHENTICATION_TYPES.find(
								(mode) => mode.labelKey === `input-mode-authentication.${authType}`,
							);
							if (!authOption) return null;
							const translated = translatedInputModeAuthentications.find(
								(t) => t.value === authOption.value,
							);
							return (
								<option key={`hml-${name}-option-${authOption.value}`} value={authOption.value}>
									{translated?.label || ''}
								</option>
							);
						})}
					</Form.Select>
				</Col>
			</Row>
		);
	};


	// Xinput模式特定配置
	const xinputModeSpecifics = (
		values: any,
		handleChange: (e: React.ChangeEvent<HTMLSelectElement>) => void,
		inputModeConfig: any,
	) => {
		return (
			<div>
				{generateAuthSelection(
					inputModeConfig,
					t('SettingsPage:auth-settings-label'),
					'xinputAuthType',
					values.xinputAuthType,
					undefined,
					handleChange,
				)}
				<Row className="mb-3">
					<Col sm={10}>
						<Trans
							ns="SettingsPage"
							i18nKey="xinput-mode-text"
							components={{ span: <span className="text-success" /> }}
						/>
					</Col>
				</Row>
			</div>
		);
	};

	// PS4模式特定配置
	const ps4ModeSpecifics = (
		values: any,
		handleChange: (e: React.ChangeEvent<HTMLSelectElement>) => void,
	) => {
		return (
			<div>
				<Row className="mb-3">
					<Col sm={10}>{t('SettingsPage:ps4-mode-explanation-text')}</Col>
				</Row>
				<Row className="mb-3">
					<Col sm={10}>
						<Form.Check
							label={t('SettingsPage:input-mode-extra-label')}
							type="switch"
							name="switchTpShareForDs4"
							checked={Boolean(values.switchTpShareForDs4)}
							onChange={(e) => {
								const checked = e.target.checked;
								setValues((prev: any) => ({
									...prev,
									switchTpShareForDs4: checked ? 1 : 0,
								}));
								setButtonLabels({
									swapTpShareLabels: checked,
								});
							}}
						/>
					</Col>
				</Row>
				<Row className="mb-3">
					<Col sm={3}>
						<Form.Label>
							{t('SettingsPage:ps4-id-mode-label')}
							<ContextualHelpOverlay
								title={t('SettingsPage:ps4-id-mode-label')}
								body={
									<Trans
										ns="SettingsPage"
										i18nKey="ps4-id-mode-explanation-text"
										components={{ ul: <ul />, li: <li /> }}
									/>
								}
							/>
						</Form.Label>
						<Form.Select
							name="ps4ControllerIDMode"
							className="form-select-sm"
							value={values.ps4ControllerIDMode || 0}
							onChange={(e) => {
								const newIDMode = parseInt(e.target.value);
								setValues((prev: any) => ({
									...prev,
									ps4ControllerIDMode: newIDMode,
									// 当识别模式为控制台（0）时，自动设置认证类型为使用密钥（1）
									ps4AuthType: newIDMode === 0 ? 1 : prev.ps4AuthType,
								}));
							}}
						>
							{PS4_ID_MODES.map((o) => (
								<option key={`hml-ps4-id-option-${o.value}`} value={o.value}>
									{t('SettingsPage:' + o.labelKey)}
								</option>
							))}
						</Form.Select>
					</Col>
				</Row>
				{values.ps4ControllerIDMode === 0 && (
					<Row className="mb-3">
						<Col sm={10}>
							<span className="text-info">已自动使用主机密钥认证</span>
						</Col>
					</Row>
				)}
			</div>
		);
	};

	// 键盘模式特定配置
	const keyboardModeSpecifics = () => {
		return (
			<div>
				<Row className="mb-3">
					<Col sm={6}>
						<div className="fs-3 fw-bold">
							{t('SettingsPage:keyboard-mapping-header-text')}
						</div>
					</Col>
				</Row>
				<Row className="mb-3">
					<Col sm={6}>
						<div>{t('SettingsPage:keyboard-mapping-sub-header-text')}</div>
					</Col>
				</Row>
				<KeyboardMapper
					buttonLabels={buttonLabels}
					handleKeyChange={handleKeyChange}
					getKeyMappingForButton={getKeyMappingForButton}
				/>
			</div>
		);
	};

	// P5General模式特定配置
	const p5generalModeSpecifics = () => {
		return (
			<Row className="mb-3">
				<Col sm={10}>
					<span className="text-success">
						使用P5General验证器选择本模式，需要在硬件配置中开启USB验证器，并在手柄内置USB接口插入P5General验证器
					</span>
				</Col>
			</Row>
		);
	};

	// 根据输入模式显示特定配置
	const inputModeSpecifics = (
		values: any,
		handleChange: (e: React.ChangeEvent<HTMLSelectElement>) => void,
	) => {
		if (Object.keys(values).length === 0) {
			return null;
		}

		const inputModeConfig = HML_INPUT_MODES.find(
			(o) => o.value == values.inputMode,
		);
		if (!inputModeConfig) {
			return null;
		}

		switch (inputModeConfig.labelKey) {
			case 'input-mode-options.keyboard':
				return keyboardModeSpecifics();
			case 'input-mode-options.ps4':
				return ps4ModeSpecifics(values, handleChange);
			case 'input-mode-options.xinput':
				return xinputModeSpecifics(values, handleChange, inputModeConfig);
			case 'input-mode-options.p5general':
				return p5generalModeSpecifics();
			default:
				return null;
		}
	};

	// 保存设置
	const handleSave = async () => {
		setSaveMessage('');
		const data = { ...values, inputMode };
		
		// 当PS4模式且识别模式为控制台时，确保认证类型为使用密钥
		if (inputMode === 4 && data.ps4ControllerIDMode === 0) {
			data.ps4AuthType = 1; // 使用密钥
		}
		
		if (inputMode === 3) {
			// 键盘模式需要保存键盘映射
			await WebApi.setKeyMappings(keyMappings);
		}
		const success = await WebApi.setGamepadOptions(data);
		if (success) {
			setSaveMessage('保存成功！');
			setTimeout(() => setSaveMessage(''), 3000);
		} else {
			setSaveMessage('保存失败，请重试。');
		}
	};

	const currentValues = { ...values, inputMode };

	return (
		<div>
			<Card>
				<Card.Header>模式设置</Card.Header>
				<Card.Body>
				<Row className="mb-3">
					<Col sm={4}>
						<Form.Label>手柄模式</Form.Label>
						<Form.Select
							name="inputMode"
							className="form-select-sm"
							value={inputMode}
							onChange={(e) => {
								const newInputMode = parseInt(e.target.value);
								setInputMode(newInputMode);
								setValues((prev: any) => ({ ...prev, inputMode: newInputMode }));
							}}
						>
							{translatedInputModes.map((mode, i) => (
								<option key={`hml-input-mode-option-${i}`} value={mode.value}>
									{mode.label}
								</option>
							))}
						</Form.Select>
					</Col>
				</Row>
				{inputModeSpecifics(currentValues, handleChange)}
				<Row className="mb-3">
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
				</Row>
			</Card.Body>
		</Card>
		</div>
	);
}

