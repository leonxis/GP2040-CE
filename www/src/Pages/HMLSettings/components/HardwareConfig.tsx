import { useEffect, useState } from 'react';
import { useNavigate } from 'react-router-dom';
import { Button, Form } from 'react-bootstrap';
import { useTranslation } from 'react-i18next';

import Section from '../../../Components/Section';
import ColorPicker from '../../../Components/ColorPicker';
import WebApi from '../../../Services/WebApi';

const AMBIENT_EFFECTS = {
	GRADIENT: 1,
	CHASE: 2,
	BREATH: 3,
	STATIC_RGB: 4,
};

export default function HardwareConfig() {
	const navigate = useNavigate();
	const { t } = useTranslation();

	const [peripheralOptions, setPeripheralOptions] = useState({
		peripheral: {
			usb0: {
				enabled: 0,
			},
			i2c1: {
				enabled: 0,
			},
		},
	});
	const [displayOptions, setDisplayOptions] = useState({ enabled: 0 });
	const [twoKeyTouchpadOptions, setTwoKeyTouchpadOptions] = useState({
		enabled: 0,
		enableKey: { action: -10, customButtonMask: 0, customDpadMask: 0 },
	});
	const [reportRate, setReportRate] = useState(1000);
	const [ledOptions, setLedOptions] = useState({
		dataPin: -1,
		brightnessMaximum: 255,
		ledFormat: 0,
		ledLayout: 0,
		ledsPerButton: 2,
	});
	const [ambientOptions, setAmbientOptions] = useState({
		ambientLightEffectsCountIndex: AMBIENT_EFFECTS.STATIC_RGB,
		ambientColor: '#ffa500',
		alStaticBrightnessCustomThemeX: 1,
		ambientLightGradientSpeed: 2,
		alGradientBrightnessCustomX: 1,
		ambientLightChaseSpeed: 100,
		alChaseBrightnessCustomX: 1,
		ambientLightBreathSpeed: 0.01,
	});

	const [hostSaveMessage, setHostSaveMessage] = useState('');
	const [ledSaveMessage, setLedSaveMessage] = useState('');
	const [colorPickerTarget, setColorPickerTarget] = useState<any>(null);
	const [showColorPicker, setShowColorPicker] = useState(false);
	const [isLoading, setIsLoading] = useState(true);

	useEffect(() => {
		async function fetchData() {
			try {
				const [peripheral, display, twoKeyTouchpad, led, ambient, addons] = await Promise.all([
					WebApi.getPeripheralOptions(),
					WebApi.getDisplayOptions(),
					WebApi.getTwoKeyTouchpadOptions(),
					WebApi.getLedOptions(),
					WebApi.getAmbientOptions(),
					WebApi.getAddonsOptions(),
				]);
				setPeripheralOptions(peripheral);
				setDisplayOptions(display);
				setTwoKeyTouchpadOptions(
					twoKeyTouchpad || {
						enabled: 0,
						enableKey: { action: -10, customButtonMask: 0, customDpadMask: 0 },
					},
				);
				setReportRate(
					[250, 500, 1000, 2000, 4000, 8000].includes(Number(addons?.reportRate))
						? Number(addons.reportRate)
						: 1000
				);

				// 同步显示屏和I2C1的启用状态
				// 如果两者不一致，以显示屏的enabled为准
				if (display.enabled !== peripheral.peripheral?.i2c1?.enabled) {
					setPeripheralOptions((prev) => ({
						...prev,
						peripheral: {
							...prev.peripheral,
							i2c1: {
								...prev.peripheral?.i2c1,
								enabled: display.enabled,
							},
						},
					}));
				}
				
				setLedOptions({
					dataPin: led.dataPin !== undefined ? led.dataPin : -1,
					brightnessMaximum: led.brightnessMaximum || 255,
					ledFormat: led.ledFormat || 0,
					ledLayout: led.ledLayout || 0,
					ledsPerButton: led.ledsPerButton || 2,
				});
				setAmbientOptions({
					ambientLightEffectsCountIndex:
						ambient?.ambientLightEffectsCountIndex ??
						AMBIENT_EFFECTS.STATIC_RGB,
					ambientColor: ambient?.ambientColor || '#ffa500',
					alStaticBrightnessCustomThemeX:
						ambient?.alStaticBrightnessCustomThemeX ?? 1,
					ambientLightGradientSpeed: ambient?.ambientLightGradientSpeed ?? 2,
					alGradientBrightnessCustomX:
						ambient?.alGradientBrightnessCustomX ?? 1,
					ambientLightChaseSpeed: ambient?.ambientLightChaseSpeed ?? 100,
					alChaseBrightnessCustomX: ambient?.alChaseBrightnessCustomX ?? 1,
					ambientLightBreathSpeed: ambient?.ambientLightBreathSpeed ?? 0.01,
				});
			} catch (error) {
				console.error('Failed to fetch hardware config:', error);
			} finally {
				setIsLoading(false);
			}
		}
		fetchData();
	}, []);

	const handleHostSave = async () => {
		try {
			// Get current peripheral options to preserve other settings
			const currentPeripheralOptions = await WebApi.getPeripheralOptions();
			
			// Prepare data to save, preserving existing settings and updating usb0.enabled 和 i2c1.enabled
			const dataToSave = {
				...currentPeripheralOptions,
				peripheral: {
					...currentPeripheralOptions.peripheral,
					usb0: {
						...currentPeripheralOptions.peripheral.usb0,
						enabled: peripheralOptions.peripheral?.usb0?.enabled || 0,
					},
					i2c1: {
						...currentPeripheralOptions.peripheral.i2c1,
						enabled: peripheralOptions.peripheral?.i2c1?.enabled || 0,
					},
				},
			};
			
			await Promise.all([
				WebApi.setPeripheralOptions(dataToSave),
				WebApi.setDisplayOptions(displayOptions),
				WebApi.setTwoKeyTouchpadOptions(twoKeyTouchpadOptions),
				WebApi.setAddonsOptions({ reportRate }),
			]);
			setHostSaveMessage(t('SettingsPage:hml-save-success-reboot'));
			setTimeout(() => setHostSaveMessage(''), 5000);
		} catch (error) {
			console.error('Failed to save host options:', error);
			setHostSaveMessage(t('SettingsPage:hml-save-failed'));
			setTimeout(() => setHostSaveMessage(''), 5000);
		}
	};

	const handleLedSave = async () => {
		try {
			// Get current LED options to preserve other settings
			const currentLedOptions = await WebApi.getLedOptions();
			
			// Prepare data to save, preserving existing settings
			const dataToSave = {
				...currentLedOptions,
				dataPin: ledOptions.dataPin,
				brightnessMaximum: ledOptions.brightnessMaximum,
				ledFormat: ledOptions.ledFormat,
				ledLayout: ledOptions.ledLayout,
				ledsPerButton: ledOptions.ledsPerButton,
			};
			
			const ambientToSave = {
				...ambientOptions,
				ambientColor: ambientOptions.ambientColor || '#ffa500',
			};

			const ledSaved = await WebApi.setLedOptions(dataToSave);
			const ambientSaved = await WebApi.setAmbientOptions(ambientToSave);
			if (!ledSaved || !ambientSaved) {
				throw new Error('Failed to save LED or ambient options');
			}
			setLedSaveMessage(t('SettingsPage:hml-save-success-reboot'));
			setTimeout(() => setLedSaveMessage(''), 5000);
		} catch (error) {
			console.error('Failed to save LED options:', error);
			setLedSaveMessage(t('SettingsPage:hml-save-failed'));
			setTimeout(() => setLedSaveMessage(''), 5000);
		}
	};

	const handleSplashImage = () => {
		navigate('/display-config');
	};

	if (isLoading) {
		return <div className="text-muted">{t('SettingsPage:hml-loading')}</div>;
	}

	return (
		<div>
			{/* 主机配置栏 */}
			<Section title={t('SettingsPage:hml-section-host-config')}>
				<div>
					<div style={{ display: 'flex', flexDirection: 'column', gap: '16px', alignItems: 'flex-start' }}>
						{/* USB验证器开关 */}
						<div style={{ display: 'flex', alignItems: 'center', gap: '12px' }}>
							<Form.Check
								type="switch"
								id="usb-auth-switch"
								label={t('SettingsPage:hml-usb-authenticator-label')}
								checked={Boolean(peripheralOptions.peripheral?.usb0?.enabled)}
								onChange={(e) => {
									setPeripheralOptions((prev) => ({
										...prev,
										peripheral: {
											...prev.peripheral,
											usb0: {
												...prev.peripheral?.usb0,
												enabled: e.target.checked ? 1 : 0,
											},
										},
									}));
								}}
							/>
							<span className="text-muted">
								{t('SettingsPage:hml-usb-authenticator-hint')}
							</span>
						</div>

						{/* 显示屏开关 */}
					<div style={{ display: 'flex', alignItems: 'center', gap: '12px' }}>
						<Form.Check
							type="switch"
							id="display-switch"
							label={t('SettingsPage:hml-display-label')}
							checked={Boolean(displayOptions.enabled)}
							onChange={(e) => {
								const isEnabled = e.target.checked ? 1 : 0;
								setDisplayOptions((prev) => ({ ...prev, enabled: isEnabled }));
								setPeripheralOptions((prev) => ({
									...prev,
									peripheral: {
										...prev.peripheral,
										i2c1: {
											...prev.peripheral?.i2c1,
											enabled: isEnabled,
										},
									},
								}));
								// 打开显示屏时自动关闭 2键触摸板
								if (isEnabled) {
									setTwoKeyTouchpadOptions((prev) => ({ ...prev, enabled: 0 }));
								}
							}}
						/>
						<span className="text-muted">
							{t('SettingsPage:hml-display-hint')}
						</span>
					</div>

					{/* 2键触摸板开关 */}
					<div style={{ display: 'flex', alignItems: 'center', gap: '12px' }}>
						<Form.Check
							type="switch"
							id="two-key-touchpad-switch"
							label={t('SettingsPage:hml-two-key-touchpad-label')}
							checked={Boolean(twoKeyTouchpadOptions.enabled)}
							onChange={(e) => {
								const isEnabled = e.target.checked ? 1 : 0;
								setTwoKeyTouchpadOptions((prev) => ({ ...prev, enabled: isEnabled }));
								// 打开 2 键触摸板时自动关闭显示屏、I2C1
								if (isEnabled) {
									setDisplayOptions((prev) => ({ ...prev, enabled: 0 }));
									setPeripheralOptions((prev) => ({
										...prev,
										peripheral: {
											...prev.peripheral,
											i2c1: {
												...prev.peripheral?.i2c1,
												enabled: 0,
											},
										},
									}));
								}
							}}
						/>
						<span className="text-muted">
							{t('SettingsPage:hml-two-key-touchpad-hint')}
						</span>
					</div>

						{/* 回报率：下拉框 → 标题在右侧 → 说明 */}
						<div style={{ display: 'flex', alignItems: 'center', gap: '12px' }}>
							<Form.Select
								id="report-rate-select"
								value={reportRate}
								onChange={(e) => setReportRate(Number(e.target.value))}
								style={{ width: '120px' }}
							>
								<option value={250}>{t('SettingsPage:hml-report-rate-250hz')}</option>
								<option value={500}>{t('SettingsPage:hml-report-rate-500hz')}</option>
								<option value={1000}>{t('SettingsPage:hml-report-rate-1khz')}</option>
								<option value={2000}>{t('SettingsPage:hml-report-rate-2khz')}</option>
								<option value={4000}>{t('SettingsPage:hml-report-rate-4khz')}</option>
								<option value={8000}>{t('SettingsPage:hml-report-rate-8khz')}</option>
							</Form.Select>
							<span className="mb-0">{t('SettingsPage:hml-report-rate-label')}</span>
							<span className="text-muted">
								{t('SettingsPage:hml-report-rate-hint')}
							</span>
						</div>

						{/* 屏幕个性化按键 */}
						<div style={{ display: 'flex', alignItems: 'center', gap: '12px' }}>
							<Button variant="primary" onClick={handleSplashImage} style={{ minWidth: '120px' }}>
								{t('SettingsPage:hml-screen-customization-button')}
							</Button>
						</div>

						{/* 保存按键 */}
						<div style={{ display: 'flex', alignItems: 'center', gap: '12px' }}>
							<Button variant="primary" onClick={handleHostSave}>
								{t('Common:button-save-label')}
							</Button>
							{hostSaveMessage && (
								<span
									className={`ms-3 ${
										hostSaveMessage === t('SettingsPage:hml-save-success-reboot')
											? 'text-success'
											: 'text-danger'
									}`}
								>
									{hostSaveMessage}
								</span>
							)}
						</div>
					</div>
				</div>
			</Section>

			{/* 灯光配置栏 */}
			<Section title={t('SettingsPage:hml-section-led-config')}>
				<div>
					<div style={{ display: 'flex', flexDirection: 'column', gap: '16px', alignItems: 'flex-start' }}>
						{/* LED灯条开关 */}
						<div style={{ display: 'flex', alignItems: 'center', gap: '12px' }}>
							<Form.Check
								type="switch"
								id="led-strip-switch"
								label={t('SettingsPage:hml-led-strip-label')}
								checked={ledOptions.dataPin !== -1}
								onChange={(e) => {
									setLedOptions((prev) => ({
										...prev,
										dataPin: e.target.checked ? (prev.dataPin === -1 ? 16 : prev.dataPin) : -1,
									}));
								}}
							/>
							<span className="text-muted">
								{ledOptions.dataPin === -1 
									? t('SettingsPage:hml-led-strip-off') 
									: t('SettingsPage:hml-led-data-pin', { pin: ledOptions.dataPin })}
							</span>
						</div>

						{/* 环境光模式选择 */}
						<div style={{ display: 'flex', alignItems: 'center', gap: '12px' }}>
							<label style={{ minWidth: '120px' }}>
								{t('SettingsPage:hml-ambient-mode-label')}
							</label>
							<Form.Select
								value={ambientOptions.ambientLightEffectsCountIndex}
								onChange={(e) =>
									setAmbientOptions((prev) => ({
										...prev,
										ambientLightEffectsCountIndex: Number(e.target.value),
									}))
								}
								style={{ width: '180px' }}
							>
								<option value={AMBIENT_EFFECTS.STATIC_RGB}>
									{t('SettingsPage:hml-ambient-mode-static-rgb')}
								</option>
								<option value={AMBIENT_EFFECTS.GRADIENT}>
									{t('SettingsPage:hml-ambient-mode-gradient')}
								</option>
								<option value={AMBIENT_EFFECTS.CHASE}>
									{t('SettingsPage:hml-ambient-mode-chase')}
								</option>
								<option value={AMBIENT_EFFECTS.BREATH}>
									{t('SettingsPage:hml-ambient-mode-breath')}
								</option>
							</Form.Select>
						</div>

						{/* 颜色取色框（静态与呼吸共用） */}
						<div style={{ display: 'flex', alignItems: 'center', gap: '12px' }}>
							<label style={{ minWidth: '80px' }}>
								{t('SettingsPage:hml-color-label')}
							</label>
							<div
								ref={(el) => {
									if (el && !colorPickerTarget) {
										setColorPickerTarget(el);
									}
								}}
								style={{
									width: '40px',
									height: '40px',
									backgroundColor: ambientOptions.ambientColor,
									border: '1px solid #ccc',
									cursor: 'pointer',
									borderRadius: '4px',
								}}
								onClick={(e) => {
									e.stopPropagation();
									e.preventDefault();
									setColorPickerTarget(e.currentTarget);
									setShowColorPicker(true);
								}}
							></div>
							{showColorPicker && colorPickerTarget && (
								<ColorPicker
									types={[{ label: t('SettingsPage:hml-led-strip-label'), value: ambientOptions.ambientColor }]}
									onChange={(color: string) => {
										setAmbientOptions((prev) => ({
											...prev,
											ambientColor: color,
										}));
									}}
									onDismiss={() => setShowColorPicker(false)}
									pickerOnly={false}
									placement="top"
									show={showColorPicker}
									target={colorPickerTarget}
									title=""
								/>
							)}
						</div>

						{/* 当前模式参数 */}
						{ambientOptions.ambientLightEffectsCountIndex === AMBIENT_EFFECTS.STATIC_RGB && (
							<div style={{ display: 'flex', alignItems: 'center', gap: '12px', width: '100%', maxWidth: '400px' }}>
								<label style={{ minWidth: '120px' }}>
									{t('SettingsPage:hml-ambient-brightness-label')}
								</label>
								<input
									type="range"
									min="0"
									max="100"
									value={Math.round(ambientOptions.alStaticBrightnessCustomThemeX * 100)}
									onChange={(e) =>
										setAmbientOptions((prev) => ({
											...prev,
											alStaticBrightnessCustomThemeX: Number(e.target.value) / 100,
										}))
									}
									style={{ flex: 1 }}
								/>
							</div>
						)}

						{ambientOptions.ambientLightEffectsCountIndex === AMBIENT_EFFECTS.GRADIENT && (
							<>
								<div style={{ display: 'flex', alignItems: 'center', gap: '12px', width: '100%', maxWidth: '400px' }}>
									<label style={{ minWidth: '120px' }}>
										{t('SettingsPage:hml-ambient-brightness-label')}
									</label>
									<input
										type="range"
										min="0"
										max="100"
										value={Math.round(ambientOptions.alGradientBrightnessCustomX * 100)}
										onChange={(e) =>
											setAmbientOptions((prev) => ({
												...prev,
												alGradientBrightnessCustomX: Number(e.target.value) / 100,
											}))
										}
										style={{ flex: 1 }}
									/>
								</div>
								<div style={{ display: 'flex', alignItems: 'center', gap: '12px', width: '100%', maxWidth: '400px' }}>
									<label style={{ minWidth: '120px' }}>
										{t('SettingsPage:hml-speed-label')}
									</label>
									<input
										type="range"
										min="1"
										max="6"
										value={ambientOptions.ambientLightGradientSpeed}
										onChange={(e) =>
											setAmbientOptions((prev) => ({
												...prev,
												ambientLightGradientSpeed: Number(e.target.value),
											}))
										}
										style={{ flex: 1 }}
									/>
								</div>
							</>
						)}

						{ambientOptions.ambientLightEffectsCountIndex === AMBIENT_EFFECTS.CHASE && (
							<>
								<div style={{ display: 'flex', alignItems: 'center', gap: '12px', width: '100%', maxWidth: '400px' }}>
									<label style={{ minWidth: '120px' }}>
										{t('SettingsPage:hml-ambient-brightness-label')}
									</label>
									<input
										type="range"
										min="0"
										max="100"
										value={Math.round(ambientOptions.alChaseBrightnessCustomX * 100)}
										onChange={(e) =>
											setAmbientOptions((prev) => ({
												...prev,
												alChaseBrightnessCustomX: Number(e.target.value) / 100,
											}))
										}
										style={{ flex: 1 }}
									/>
								</div>
								<div style={{ display: 'flex', alignItems: 'center', gap: '12px', width: '100%', maxWidth: '400px' }}>
									<label style={{ minWidth: '120px' }}>
										{t('SettingsPage:hml-speed-label')}
									</label>
									<input
										type="range"
										min="0"
										max="100"
										value={ambientOptions.ambientLightChaseSpeed}
										onChange={(e) =>
											setAmbientOptions((prev) => ({
												...prev,
												ambientLightChaseSpeed: Number(e.target.value),
											}))
										}
										style={{ flex: 1 }}
									/>
								</div>
							</>
						)}

						{ambientOptions.ambientLightEffectsCountIndex === AMBIENT_EFFECTS.BREATH && (
							<div style={{ display: 'flex', alignItems: 'center', gap: '12px', width: '100%', maxWidth: '400px' }}>
								<label style={{ minWidth: '120px' }}>
									{t('SettingsPage:hml-speed-label')}
								</label>
								<input
									type="range"
									min="1"
									max="5"
									step="1"
									value={Math.round((ambientOptions.ambientLightBreathSpeed || 0.01) * 100)}
									onChange={(e) =>
										setAmbientOptions((prev) => ({
											...prev,
											ambientLightBreathSpeed: Number(e.target.value) / 100,
										}))
									}
									style={{ flex: 1 }}
								/>
							</div>
						)}

						{/* 亮度调节滑块 */}
						<div style={{ display: 'flex', alignItems: 'center', gap: '12px', width: '100%', maxWidth: '400px' }}>
							<label style={{ minWidth: '80px' }}>{t('SettingsPage:hml-brightness-label')}</label>
							<input
								type="range"
								min="0"
								max="255"
								value={ledOptions.brightnessMaximum}
								onChange={(e) => {
									setLedOptions((prev) => ({
										...prev,
										brightnessMaximum: parseInt(e.target.value, 10),
									}));
								}}
								style={{ flex: 1 }}
							/>
							<span style={{ minWidth: '50px', textAlign: 'right' }}>{ledOptions.brightnessMaximum}</span>
						</div>

						{/* 保存按键 */}
						<div style={{ display: 'flex', alignItems: 'center', gap: '12px' }}>
							<Button variant="primary" onClick={handleLedSave}>
								{t('Common:button-save-label')}
							</Button>
							{ledSaveMessage && (
								<span
									className={`ms-3 ${
										ledSaveMessage === t('SettingsPage:hml-save-success-reboot')
											? 'text-success'
											: 'text-danger'
									}`}
								>
									{ledSaveMessage}
								</span>
							)}
						</div>
					</div>
				</div>
			</Section>
		</div>
	);
}
