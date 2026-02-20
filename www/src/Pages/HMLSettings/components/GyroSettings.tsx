import { useContext, useEffect, useMemo, useRef, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { MultiValue } from 'react-select';
import { Button, Form } from 'react-bootstrap';
import Section from '../../../Components/Section';
import CustomSelect from '../../../Components/CustomSelect';
import { AppContext } from '../../../Contexts/AppContext';
import {
	LSM6DSR_OUTPUT_DS4,
	LSM6DSR_OUTPUT_DS4_STICK,
	LSM6DSR_OUTPUT_XBOX_STICK,
	LSM6DSR_OUTPUT_MOUSE,
} from '../../../Addons/LSM6DSR';
import { BUTTON_ACTIONS } from '../../../Data/Pins';
import { BUTTON_MASKS, DPAD_MASKS, getButtonLabels } from '../../../Data/Buttons';
import WebApi from '../../../Services/WebApi';

type EngageKeyOption = { value: number; label: string };
const DROPDOWN_WIDTH = 400;
const OUTPUT_STICK_WIDTH = 250;
const GYRO_BUTTON_WIDTH = 120;
const COLUMN_GAP = 24;
const IMU_POLL_INTERVAL_MS = 150;

export const GYRO_ENGAGE_ALWAYS = 0;
export const GYRO_ENGAGE_ON_KEY = 1;
export const GYRO_ENGAGE_PAUSE_ON_KEY = 2;

const EXCLUDED_KEYS = ['NONE', 'RESERVED', 'ASSIGNED_TO_ADDON', 'CUSTOM_BUTTON_COMBO'];

function getButtonPart(key: string): string {
	return key.replace(/^(BUTTON_PRESS_|SUSTAIN_)/, '') || key.split('_').pop() || key;
}

function isButtonOnlyKey(key: string): boolean {
	const part = getButtonPart(key);
	return (
		BUTTON_MASKS.some((m) => m.label === part) ||
		DPAD_MASKS.some((m) => m.label === part)
	);
}

type GyroSettingsProps = {
	values: Record<string, unknown>;
	errors: Record<string, unknown>;
	handleChange: (e: React.ChangeEvent<unknown>) => void;
	setFieldValue: (field: string, value: unknown) => void;
	saveMessage?: string;
	onSaveClick?: () => void;
};

type ImuData = {
	gyroX?: number;
	gyroY?: number;
	gyroZ?: number;
	accelX?: number;
	accelY?: number;
	accelZ?: number;
	// 调试信息
	debug?: boolean;
	whoAmI?: number;
	expectedWhoAmI?: number;
	spiOk?: boolean;
	imuOk?: boolean;
};

export default function GyroSettings({
	values,
	errors,
	handleChange,
	setFieldValue,
	saveMessage = '',
	onSaveClick,
}: GyroSettingsProps) {
	const { t } = useTranslation();
	const appContext = useContext(AppContext);
	const [imuData, setImuData] = useState<ImuData | null>(null);
	const [imuDataError, setImuDataError] = useState(false);
	const imuPollRef = useRef<ReturnType<typeof setInterval> | null>(null);

	const enabled = Boolean(values.LSM6DSRAddonEnabled);
	const outputMode = Number(values.lsm6dsrOutputMode) ?? 0;
	const outputStick = Number(values.lsm6dsrOutputStick) ?? 1;
	const stickDropdownEnabled = outputMode === LSM6DSR_OUTPUT_DS4_STICK || outputMode === LSM6DSR_OUTPUT_XBOX_STICK;
	const engageMode = Number(values.lsm6dsrEngageMode) ?? 0;
	const engageKeys: number[] = Array.isArray(values.lsm6dsrEngageKeys)
		? (values.lsm6dsrEngageKeys as number[]).filter((k) => typeof k === 'number')
		: [];

	const buttonNames = useMemo(() => {
		const { buttonLabels } = (appContext || {}) as { buttonLabels?: { buttonLabelType?: string; swapTpShareLabels?: boolean } };
		const labelType = buttonLabels?.buttonLabelType || 'gp2040';
		const swap = !!buttonLabels?.swapTpShareLabels;
		return getButtonLabels(labelType, swap);
	}, [appContext]);

	const engageKeyOptions = useMemo(() => {
		const opts: { value: number; label: string }[] = [];
		Object.entries(BUTTON_ACTIONS).forEach(([key, value]) => {
			if (EXCLUDED_KEYS.includes(key)) return;
			if (!key.startsWith('BUTTON_PRESS_') && !key.startsWith('SUSTAIN_')) return;
			if (!isButtonOnlyKey(key)) return;
			const v = Number(value);
			if (Number.isNaN(v)) return;
			const buttonPart = getButtonPart(key);
			const label = (buttonNames && (buttonNames as Record<string, string>)[buttonPart]) || buttonPart;
			opts.push({ value: v, label });
		});
		opts.sort((a, b) => a.label.localeCompare(b.label));
		return opts;
	}, [buttonNames]);

	const handleCalibrate = () => {
		setFieldValue('lsm6dsrCalibrateGyroRequested', 1);
		setTimeout(() => setFieldValue('lsm6dsrCalibrateGyroRequested', 0), 1000);
	};

	const handleSave = () => {
		onSaveClick?.();
	};

	const fetchImuData = async () => {
		try {
			const data = await WebApi.getLSM6DSRImuData();
			if (data && typeof data === 'object') {
				setImuData(data as ImuData);
				setImuDataError(false);
			} else {
				setImuDataError(true);
			}
		} catch {
			setImuDataError(true);
		}
	};

	useEffect(() => {
		if (!enabled) {
			if (imuPollRef.current) {
				clearInterval(imuPollRef.current);
				imuPollRef.current = null;
			}
			setImuData(null);
			return;
		}
		setImuDataError(false);
		fetchImuData();
		imuPollRef.current = setInterval(fetchImuData, IMU_POLL_INTERVAL_MS);
		return () => {
			if (imuPollRef.current) {
				clearInterval(imuPollRef.current);
				imuPollRef.current = null;
			}
		};
	}, [enabled]);

	const engageKeysValue: EngageKeyOption[] = useMemo(
		() => engageKeyOptions.filter((opt) => engageKeys.includes(opt.value)),
		[engageKeyOptions, engageKeys],
	);

	const onEngageKeysChange = (selected: MultiValue<EngageKeyOption>) => {
		setFieldValue('lsm6dsrEngageKeys', selected ? selected.map((o) => o.value) : []);
	};

	const twoColStyle = { display: 'flex' as const, gap: COLUMN_GAP };

	return (
		<Section title={t('CalibrationSettings:gyro-settings-title')}>
			{enabled ? (
				<>
					{/* 第一行：陀螺仪模拟方式： | 模拟左/右摇杆：（标题，与第二行下拉框上下左对齐） */}
					<div style={{ ...twoColStyle, marginBottom: '6px' }}>
						<div style={{ width: DROPDOWN_WIDTH }}>
							<Form.Label className="mb-0">{t('CalibrationSettings:gyro-simulation-mode-label')}</Form.Label>
						</div>
						<div style={{ width: OUTPUT_STICK_WIDTH }}>
							<Form.Label className="mb-0">{t('CalibrationSettings:gyro-output-stick-label')}</Form.Label>
						</div>
					</div>
					{/* 第二行：陀螺仪输出模式下拉框 | 左/右摇杆下拉框（仅 DS4摇杆/XBOX摇杆 时可选，宽度 200px） */}
					<div style={{ ...twoColStyle, marginBottom: '16px' }}>
						<div style={{ width: DROPDOWN_WIDTH }}>
							<Form.Select
								name="lsm6dsrOutputMode"
								className="form-select-sm"
								style={{ width: '100%' }}
								value={outputMode}
								isInvalid={Boolean(errors.lsm6dsrOutputMode)}
								onChange={handleChange}
							>
								<option value={LSM6DSR_OUTPUT_DS4}>
									{t('CalibrationSettings:gyro-mode-ds4')}
								</option>
								<option value={LSM6DSR_OUTPUT_DS4_STICK}>
									{t('CalibrationSettings:gyro-mode-ds4-stick')}
								</option>
								<option value={LSM6DSR_OUTPUT_XBOX_STICK}>
									{t('CalibrationSettings:gyro-mode-xbox-stick')}
								</option>
								<option value={LSM6DSR_OUTPUT_MOUSE}>
									{t('CalibrationSettings:gyro-mode-mouse')}
								</option>
							</Form.Select>
						</div>
						<div style={{ width: OUTPUT_STICK_WIDTH }}>
							<Form.Select
								name="lsm6dsrOutputStick"
								className="form-select-sm"
								style={{ width: '100%' }}
								value={outputStick}
								onChange={handleChange}
								disabled={!stickDropdownEnabled}
							>
								<option value={0}>{t('CalibrationSettings:gyro-stick-left')}</option>
								<option value={1}>{t('CalibrationSettings:gyro-stick-right')}</option>
							</Form.Select>
						</div>
					</div>

					{/* 生效方式：标题 + 下拉框，右侧为校准陀螺仪按键 */}
					<div className="mb-2">
						<Form.Label className="mb-0">{t('CalibrationSettings:gyro-engage-mode-label-short')}</Form.Label>
					</div>
					<div style={{ display: 'flex', alignItems: 'center', gap: COLUMN_GAP, marginBottom: '16px', flexWrap: 'wrap' }}>
						<div style={{ width: DROPDOWN_WIDTH }}>
							<Form.Select
								name="lsm6dsrEngageMode"
								className="form-select-sm"
								style={{ width: '100%' }}
								value={engageMode}
								onChange={handleChange}
							>
								<option value={GYRO_ENGAGE_ALWAYS}>
									{t('CalibrationSettings:gyro-engage-always')}
								</option>
								<option value={GYRO_ENGAGE_ON_KEY}>
									{t('CalibrationSettings:gyro-engage-on-key')}
								</option>
								<option value={GYRO_ENGAGE_PAUSE_ON_KEY}>
									{t('CalibrationSettings:gyro-engage-pause-on-key')}
								</option>
							</Form.Select>
						</div>
						<Button
							variant="primary"
							size="sm"
							style={{ width: GYRO_BUTTON_WIDTH }}
							onClick={handleCalibrate}
						>
							{t('CalibrationSettings:gyro-calibrate-button')}
						</Button>
					</div>
					{/* 生效按键：标题 + 下拉框（在生效方式下方） */}
					<div className="mb-2">
						<Form.Label className="mb-0">{t('CalibrationSettings:gyro-engage-keys-label-short')}</Form.Label>
					</div>
					<div style={{ width: DROPDOWN_WIDTH, marginBottom: '16px' }}>
						<CustomSelect<EngageKeyOption, true>
							isMulti
							options={engageKeyOptions}
							getOptionLabel={(opt) => opt.label}
							getOptionValue={(opt) => String(opt.value)}
							value={engageKeysValue}
							onChange={onEngageKeysChange}
							styles={{ control: (base) => ({ ...base, minHeight: '38px' }) }}
						/>
					</div>

					{/* 调试信息：6 轴 RAW、WHO_AM_I、SPI OK、IMU OK，启用时轮询显示 */}
					<div className="p-2 bg-light rounded small">
						<div className="fw-semibold mb-1">{t('CalibrationSettings:gyro-debug-title')}</div>
						{imuDataError && (
							<p className="text-danger mb-0 small">{t('CalibrationSettings:gyro-view-data-error')}</p>
						)}
						{!imuDataError && !imuData && <span className="text-muted">{t('Common:loading-text')}</span>}
						{!imuDataError && imuData && (
							<pre className="mb-0 small" style={{ fontSize: '0.8rem' }}>
								{`WHO_AM_I: 0x${(imuData.whoAmI ?? 0).toString(16).toUpperCase().padStart(2, '0')} (${t('CalibrationSettings:gyro-debug-expected')} 0x6B)\nSPI OK: ${imuData.spiOk ?? false}\nIMU OK: ${imuData.imuOk ?? false}\n\nGyro X (RAW): ${imuData.gyroX ?? '-'}\nGyro Y (RAW): ${imuData.gyroY ?? '-'}\nGyro Z (RAW): ${imuData.gyroZ ?? '-'}\nAccel X (RAW): ${imuData.accelX ?? '-'}\nAccel Y (RAW): ${imuData.accelY ?? '-'}\nAccel Z (RAW): ${imuData.accelZ ?? '-'}`}
							</pre>
						)}
					</div>
				</>
			) : null}
			<div style={{ marginTop: '16px', display: 'flex', justifyContent: 'flex-start', alignItems: 'center', width: '100%' }}>
				<div style={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
					<Button
						variant="primary"
						onClick={handleSave}
						disabled={!onSaveClick || !enabled}
					>
						{t('Common:button-save-label')}
					</Button>
					{saveMessage ? (
						<span className={saveMessage.includes('成功') || saveMessage.includes('success') ? 'text-success' : 'text-danger'}>
							{saveMessage}
						</span>
					) : null}
				</div>
			</div>
		</Section>
	);
}
