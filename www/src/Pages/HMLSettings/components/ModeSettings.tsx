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

type ModeValues = Record<string, number>;
type InputModeConfig = {
	labelKey: string;
	value: number;
	authentication?: string[];
};
type QuickModeHotkeyField =
	| 'runtimeModeHotkeyX'
	| 'runtimeModeHotkeyO'
	| 'runtimeModeHotkeySquare'
	| 'runtimeModeHotkeyTriangle';
type AppContextShape = {
	buttonLabels?: unknown;
	setButtonLabels?: (args: { swapTpShareLabels: boolean }) => void;
};

const QUICK_MODE_SWITCH_FIELDS: { key: QuickModeHotkeyField; labelKey: string }[] = [
	{ key: 'runtimeModeHotkeyX', labelKey: 'hml-quick-mode-label-x' },
	{ key: 'runtimeModeHotkeyO', labelKey: 'hml-quick-mode-label-o' },
	{ key: 'runtimeModeHotkeySquare', labelKey: 'hml-quick-mode-label-square' },
	{ key: 'runtimeModeHotkeyTriangle', labelKey: 'hml-quick-mode-label-triangle' },
];

export default function ModeSettings() {
	const { t } = useTranslation();
	const appContext = React.useContext(AppContext) as AppContextShape | null;
	const { values, setValues, inputMode, setInputMode, isLoading, error } =
		useGamepadOptions();
	const { keyMappings, handleKeyChange, getKeyMappingForButton } = useKeyMappings();
	const [saveMessage, setSaveMessage] = React.useState('');
	const buttonLabels = appContext?.buttonLabels;
	const setButtonLabels = appContext?.setButtonLabels;

	if (!appContext) {
		return (
			<Card>
				<Card.Header>{t('SettingsPage:hml-mode-settings-title')}</Card.Header>
				<Card.Body>
					<p className="text-danger">{t('SettingsPage:hml-error-app-context')}</p>
				</Card.Body>
			</Card>
		);
	}

	// 如果正在加载或出错，显示相应信息
	if (isLoading) {
		return (
			<Card>
				<Card.Header>{t('SettingsPage:hml-mode-settings-title')}</Card.Header>
				<Card.Body>
					<p className="text-muted">{t('SettingsPage:hml-loading')}</p>
				</Card.Body>
			</Card>
		);
	}

	if (error) {
		return (
			<Card>
				<Card.Header>{t('SettingsPage:hml-mode-settings-title')}</Card.Header>
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
		setValues((prev) => ({
			...prev,
			[name]: parseInt(value, 10),
		}));
	};

	const handleQuickModeHotkeyChange = (e: React.ChangeEvent<HTMLSelectElement>) => {
		const { name, value } = e.target;
		setValues((prev) => ({
			...prev,
			[name]: parseInt(value, 10),
		}));
	};

	// 生成认证选择框
	const generateAuthSelection = (
		inputModeConfig: InputModeConfig,
		label: string,
		name: string,
		value: number | undefined,
		hasError: boolean | undefined,
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
						isInvalid={hasError}
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
		modeValues: ModeValues,
		handleChange: (e: React.ChangeEvent<HTMLSelectElement>) => void,
		inputModeConfig: InputModeConfig,
	) => {
		return (
			<div>
				{generateAuthSelection(
					inputModeConfig,
					t('SettingsPage:auth-settings-label'),
					'xinputAuthType',
					modeValues.xinputAuthType,
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
	const ps4ModeSpecifics = (modeValues: ModeValues) => {
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
							checked={Boolean(modeValues.switchTpShareForDs4)}
							onChange={(e) => {
								const checked = e.target.checked;
								setValues((prev) => ({
									...prev,
									switchTpShareForDs4: checked ? 1 : 0,
								}));
								setButtonLabels?.({ swapTpShareLabels: checked });
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
							value={modeValues.ps4ControllerIDMode || 0}
							onChange={(e) => {
								const newIDMode = parseInt(e.target.value);
								setValues((prev) => ({
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
				{modeValues.ps4ControllerIDMode === 0 && (
					<Row className="mb-3">
						<Col sm={10}>
							<span className="text-info">{t('SettingsPage:hml-ps4-auto-console-auth-hint')}</span>
						</Col>
					</Row>
				)}
			</div>
		);
	};

	// PS4B模式特定配置（固定使用电脑主机模式，不需要识别模式选择）
	const ps4bModeSpecifics = () => {
		return (
			<div>
				<Row className="mb-3">
					<Col sm={10}>
						<span className="text-info">
							{t('SettingsPage:hml-ps4b-mode-hint')}
						</span>
					</Col>
				</Row>
			</div>
		);
	};

	// XINPUTB模式特定配置（固定为电脑模式，不支持认证设置）
	const xinputbModeSpecifics = () => {
		return (
			<div>
				<Row className="mb-3">
					<Col sm={10}>
						<span className="text-info">
							{t('SettingsPage:hml-xinputb-mode-hint')}
						</span>
					</Col>
				</Row>
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
						{t('SettingsPage:hml-p5general-mode-hint')}
					</span>
				</Col>
			</Row>
		);
	};

	// 根据输入模式显示特定配置
	const inputModeSpecifics = (
		modeValues: ModeValues,
		handleChange: (e: React.ChangeEvent<HTMLSelectElement>) => void,
	) => {
		if (Object.keys(modeValues).length === 0) {
			return null;
		}

		const inputModeConfig = HML_INPUT_MODES.find(
			(o) => o.value === modeValues.inputMode,
		) as InputModeConfig | undefined;
		if (!inputModeConfig) {
			return null;
		}

		switch (inputModeConfig.labelKey) {
			case 'input-mode-options.keyboard':
				return keyboardModeSpecifics();
			case 'input-mode-options.ps4':
				return ps4ModeSpecifics(modeValues);
			case 'input-mode-options.ps4b':
				return ps4bModeSpecifics();
			case 'input-mode-options.xinput':
				return xinputModeSpecifics(modeValues, handleChange, inputModeConfig);
			case 'input-mode-options.xinputb':
				return xinputbModeSpecifics();
			case 'input-mode-options.p5general':
				return p5generalModeSpecifics();
			default:
				return null;
		}
	};

	// 保存设置
	const handleSave = async () => {
		setSaveMessage('');
		const data: ModeValues = { ...values, inputMode };
		
		// 当PS4模式且识别模式为控制台时，确保认证类型为使用密钥
		if (inputMode === 4 && data.ps4ControllerIDMode === 0) {
			data.ps4AuthType = 1; // 使用密钥
		}
		// XINPUT电脑模式固定无认证
		if (inputMode === 18) {
			data.xinputAuthType = 0;
		}
		
		if (inputMode === 3) {
			// 键盘模式需要保存键盘映射
			await WebApi.setKeyMappings(keyMappings);
		}
		const success = await WebApi.setGamepadOptions(data);
		if (success) {
			setSaveMessage(t('SettingsPage:hml-save-success'));
			setTimeout(() => setSaveMessage(''), 3000);
		} else {
			setSaveMessage(t('SettingsPage:hml-save-failed-retry'));
		}
	};

	const currentValues: ModeValues = { ...values, inputMode };
	const getQuickModeValue = (value: number | undefined) =>
		translatedInputModes.some((mode) => mode.value === value) ? value : -1;

	return (
		<div>
			<Card>
				<Card.Header>{t('SettingsPage:hml-mode-settings-title')}</Card.Header>
				<Card.Body>
				<Row className="mb-3">
					<Col sm={4}>
						<Form.Label>{t('SettingsPage:hml-gamepad-mode-label')}</Form.Label>
						<Form.Select
							name="inputMode"
							className="form-select-sm"
							value={inputMode}
							onChange={(e) => {
								const newInputMode = parseInt(e.target.value);
								setInputMode(newInputMode);
								setValues((prev) => ({
									...prev,
									inputMode: newInputMode,
									xinputAuthType: newInputMode === 18 ? 0 : prev.xinputAuthType,
								}));
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
				</Row>
			</Card.Body>
		</Card>
			<Card className="mt-3">
				<Card.Header>{t('SettingsPage:hml-quick-mode-title')}</Card.Header>
				<Card.Body>
					<Row className="mb-2">
						<Col sm={12}>
							<div>{t('SettingsPage:hml-quick-mode-line-firmware')}</div>
							<div>{t('SettingsPage:hml-quick-mode-line-webconfig')}</div>
							<div>{t('SettingsPage:hml-quick-mode-line-inputmode')}</div>
						</Col>
					</Row>
					<Row className="mb-3">
						{QUICK_MODE_SWITCH_FIELDS.map((field) => (
							<Col sm={6} key={field.key} className="mb-3">
								<Form.Label>{t(`SettingsPage:${field.labelKey}`)}</Form.Label>
								<Form.Select
									name={field.key}
									className="form-select-sm"
									value={getQuickModeValue(currentValues[field.key])}
									onChange={handleQuickModeHotkeyChange}
								>
									<option value={-1}>
										{t('SettingsPage:hml-quick-mode-disabled')}
									</option>
									{translatedInputModes.map((mode, i) => (
										<option key={`hml-quick-${field.key}-${i}`} value={mode.value}>
											{mode.label}
										</option>
									))}
								</Form.Select>
							</Col>
						))}
					</Row>
					<Row className="mb-3">
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
					</Row>
				</Card.Body>
			</Card>
		</div>
	);
}

