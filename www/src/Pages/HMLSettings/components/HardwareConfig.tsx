import { useEffect, useState } from 'react';
import { useNavigate } from 'react-router-dom';
import { Button, Form } from 'react-bootstrap';
import { useTranslation } from 'react-i18next';

import Section from '../../../Components/Section';
import ColorPicker from '../../../Components/ColorPicker';
import WebApi from '../../../Services/WebApi';
import { hexToInt } from '../../../Services/Utilities';

export default function HardwareConfig() {
	const navigate = useNavigate();
	const { t } = useTranslation('');

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
	const [fourKeyTouchpadOptions, setFourKeyTouchpadOptions] = useState({ enabled: 0 });
	const [twoKeyTouchpadOptions, setTwoKeyTouchpadOptions] = useState({ enabled: 0 });
	const [reportRate, setReportRate] = useState(1000);
	const [ledOptions, setLedOptions] = useState({
		dataPin: -1,
		brightnessMaximum: 255,
		ledColor: '#00ff00',
		ledFormat: 0,
		ledLayout: 0,
		ledsPerButton: 2,
	});

	const [hostSaveMessage, setHostSaveMessage] = useState('');
	const [ledSaveMessage, setLedSaveMessage] = useState('');
	const [colorPickerTarget, setColorPickerTarget] = useState(null);
	const [showColorPicker, setShowColorPicker] = useState(false);

	useEffect(() => {
		async function fetchData() {
			const [peripheral, display, fourKeyTouchpad, twoKeyTouchpad, led, addons] = await Promise.all([
				WebApi.getPeripheralOptions(),
				WebApi.getDisplayOptions(),
				WebApi.getFourKeyTouchpadOptions(),
				WebApi.getTwoKeyTouchpadOptions(),
				WebApi.getLedOptions(),
				WebApi.getAddonsOptions(),
			]);
			setPeripheralOptions(peripheral);
			setDisplayOptions(display);
			setFourKeyTouchpadOptions(fourKeyTouchpad || { enabled: 0 });
			setTwoKeyTouchpadOptions(twoKeyTouchpad || { enabled: 0 });
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
			
			// pledColor is already converted to hex string by WebApi.getLedOptions()
			setLedOptions({
				dataPin: led.dataPin !== undefined ? led.dataPin : -1,
				brightnessMaximum: led.brightnessMaximum || 255,
				ledColor: led.pledColor || '#00ff00',
				ledFormat: led.ledFormat || 0,
				ledLayout: led.ledLayout || 0,
				ledsPerButton: led.ledsPerButton || 2,
			});
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
				WebApi.setFourKeyTouchpadOptions(fourKeyTouchpadOptions),
				WebApi.setTwoKeyTouchpadOptions(twoKeyTouchpadOptions),
				WebApi.setAddonsOptions({ reportRate }),
			]);
			setHostSaveMessage('保存成功！请重启设备');
			setTimeout(() => setHostSaveMessage(''), 5000);
		} catch (error) {
			console.error('Failed to save host options:', error);
			setHostSaveMessage('保存失败');
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
				pledColor: hexToInt(ledOptions.ledColor || '#00ff00'),
				ledFormat: ledOptions.ledFormat,
				ledLayout: ledOptions.ledLayout,
				ledsPerButton: ledOptions.ledsPerButton,
			};
			
			await WebApi.setLedOptions(dataToSave);
			setLedSaveMessage('保存成功！请重启设备');
			setTimeout(() => setLedSaveMessage(''), 5000);
		} catch (error) {
			console.error('Failed to save LED options:', error);
			setLedSaveMessage('保存失败');
			setTimeout(() => setLedSaveMessage(''), 5000);
		}
	};

	const handleSplashImage = () => {
		navigate('/display-config');
	};

	return (
		<div>
			{/* 主机配置栏 */}
			<Section title="主机配置">
				<div>
					<div style={{ display: 'flex', flexDirection: 'column', gap: '16px', alignItems: 'flex-start' }}>
						{/* USB验证器开关 */}
						<div style={{ display: 'flex', alignItems: 'center', gap: '12px' }}>
							<Form.Check
								type="switch"
								id="usb-auth-switch"
								label="USB验证器"
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
								当设置Xinput模式且使用主机USB认证，或者设置为PS5General模式时，需要打开本开关并插入验证器
							</span>
						</div>

						{/* 显示屏开关 */}
					<div style={{ display: 'flex', alignItems: 'center', gap: '12px' }}>
						<Form.Check
							type="switch"
							id="display-switch"
							label="显示屏"
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
								// 打开显示屏时自动关闭 4键触摸板 和 2键触摸板
								if (isEnabled) {
									setFourKeyTouchpadOptions((prev) => ({ ...prev, enabled: 0 }));
									setTwoKeyTouchpadOptions((prev) => ({ ...prev, enabled: 0 }));
								}
							}}
						/>
						<span className="text-muted">
							将关闭显示器以及对应接口，PS5G模式建议关闭显示屏获得1000Hz回报率
						</span>
					</div>

					{/* 4键触摸板开关 */}
					<div style={{ display: 'flex', alignItems: 'center', gap: '12px' }}>
						<Form.Check
							type="switch"
							id="four-key-touchpad-switch"
							label="4键触摸板"
							checked={Boolean(fourKeyTouchpadOptions.enabled)}
							onChange={(e) => {
								const isEnabled = e.target.checked ? 1 : 0;
								setFourKeyTouchpadOptions((prev) => ({ ...prev, enabled: isEnabled }));
								// 打开 4 键触摸板时自动关闭显示屏、I2C1 和 2键触摸板
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
									setTwoKeyTouchpadOptions((prev) => ({ ...prev, enabled: 0 }));
								}
							}}
						/>
						<span className="text-muted">
							需要使用触摸板按键请将显示屏替换为触摸板
						</span>
					</div>

					{/* 2键触摸板开关 */}
					<div style={{ display: 'flex', alignItems: 'center', gap: '12px' }}>
						<Form.Check
							type="switch"
							id="two-key-touchpad-switch"
							label="2键触摸板"
							checked={Boolean(twoKeyTouchpadOptions.enabled)}
							onChange={(e) => {
								const isEnabled = e.target.checked ? 1 : 0;
								setTwoKeyTouchpadOptions((prev) => ({ ...prev, enabled: isEnabled }));
								// 打开 2 键触摸板时自动关闭显示屏、I2C1 和 4键触摸板
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
									setFourKeyTouchpadOptions((prev) => ({ ...prev, enabled: 0 }));
								}
							}}
						/>
						<span className="text-muted">
							同4键触摸板，将会禁用显示屏
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
								<option value={250}>250Hz</option>
								<option value={500}>500Hz</option>
								<option value={1000}>1KHz</option>
								<option value={2000}>2KHz</option>
								<option value={4000}>4KHz</option>
								<option value={8000}>8KHz</option>
							</Form.Select>
							<span className="mb-0">回报率</span>
							<span className="text-muted">
								调整主机连接回报率，若开启陀螺仪可能造成回报率降低。
							</span>
						</div>

						{/* 屏幕个性化按键 */}
						<div style={{ display: 'flex', alignItems: 'center', gap: '12px' }}>
							<Button variant="primary" onClick={handleSplashImage} style={{ minWidth: '120px' }}>
								屏幕个性化
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
										hostSaveMessage.includes('成功') ? 'text-success' : 'text-danger'
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
			<Section title="灯光配置">
				<div>
					<div style={{ display: 'flex', flexDirection: 'column', gap: '16px', alignItems: 'flex-start' }}>
						{/* LED灯条开关 */}
						<div style={{ display: 'flex', alignItems: 'center', gap: '12px' }}>
							<Form.Check
								type="switch"
								id="led-strip-switch"
								label="LED灯条"
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
									? 'LED灯条已关闭' 
									: `LED数据引脚: GPIO${ledOptions.dataPin}`}
							</span>
						</div>

						{/* 颜色取色框 */}
						<div style={{ display: 'flex', alignItems: 'center', gap: '12px' }}>
							<label style={{ minWidth: '80px' }}>颜色：</label>
							<div
								ref={(el) => {
									if (el && !colorPickerTarget) {
										setColorPickerTarget(el);
									}
								}}
								style={{
									width: '40px',
									height: '40px',
									backgroundColor: ledOptions.ledColor,
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
									types={[{ label: 'LED Color', value: ledOptions.ledColor }]}
									onChange={(color) => {
										setLedOptions((prev) => ({
											...prev,
											ledColor: color,
										}));
									}}
									onDismiss={() => setShowColorPicker(false)}
									placement="top"
									show={showColorPicker}
									target={colorPickerTarget}
								/>
							)}
						</div>

						{/* 亮度调节滑块 */}
						<div style={{ display: 'flex', alignItems: 'center', gap: '12px', width: '100%', maxWidth: '400px' }}>
							<label style={{ minWidth: '80px' }}>亮度：</label>
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
										ledSaveMessage.includes('成功') ? 'text-success' : 'text-danger'
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
