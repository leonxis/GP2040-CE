import { useCallback, useContext, useEffect, useMemo, useRef, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { MultiValue } from 'react-select';
import { Button, Form, Modal } from 'react-bootstrap';
import Section from '../../../Components/Section';
import CustomSelect from '../../../Components/CustomSelect';
import { AppContext } from '../../../Contexts/AppContext';
import {
	LSM6DSR_OUTPUT_DS4,
	LSM6DSR_OUTPUT_LEFT_STICK,
	LSM6DSR_OUTPUT_RIGHT_STICK,
	LSM6DSR_OUTPUT_MOUSE,
} from '../../../Addons/LSM6DSR';
import { BUTTON_ACTIONS } from '../../../Data/Pins';
import { BUTTON_MASKS, DPAD_MASKS, getButtonLabels } from '../../../Data/Buttons';
import WebApi from '../../../Services/WebApi';

type EngageKeyOption = { value: number; label: string };
const DROPDOWN_WIDTH = 400;
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
	offsetGyroX?: number;
	offsetGyroY?: number;
	offsetGyroZ?: number;
	debug?: boolean;
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
	const [stickSensitivity, setStickSensitivity] = useState(0);
	const [stickThreshold, setStickThreshold] = useState(0);
	const [mouseSensitivity, setMouseSensitivity] = useState(0);
	const [mouseThreshold, setMouseThreshold] = useState(0);

	const enabled = Boolean(values.LSM6DSRAddonEnabled);
		const outputMode = Number(values.lsm6dsrOutputMode) ?? 0;
		const engageMode = Number(values.lsm6dsrEngageMode) ?? 0;
		const spikeFilterEnabled = Number(values.lsm6dsrSpikeFilterEnabled) !== 0;
		const oneEuroFilterEnabled = Number(values.lsm6dsrOneEuroFilterEnabled) !== 0;
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

	const [calibrateMessage, setCalibrateMessage] = useState('');
	const [calibrateOk, setCalibrateOk] = useState<boolean | null>(null);
	const [showGyroModal, setShowGyroModal] = useState(false);
	const handleCalibrate = async () => {
		setCalibrateMessage('');
		setCalibrateOk(null);
		try {
			const data = await WebApi.calibrateLSM6DSRGyro();
			if (data?.ok) {
				setFieldValue('lsm6dsrOffsetGyroX', data.offsetGyroX ?? 0);
				setFieldValue('lsm6dsrOffsetGyroY', data.offsetGyroY ?? 0);
				setFieldValue('lsm6dsrOffsetGyroZ', data.offsetGyroZ ?? 0);
				setCalibrateMessage(t('CalibrationSettings:gyro-calibrate-success') || '校准完成，请点击保存写入配置');
				setCalibrateOk(true);
			} else {
				setCalibrateMessage(t('CalibrationSettings:gyro-calibrate-fail') || '校准失败，请确认设备静止且 IMU 正常');
				setCalibrateOk(false);
			}
		} catch {
			setCalibrateMessage(t('CalibrationSettings:gyro-calibrate-fail') || '校准失败');
			setCalibrateOk(false);
		}
	};

	const handleSave = () => {
		onSaveClick?.();
	};

	const fetchImuData = useCallback(async () => {
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
	}, []);

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
	}, [enabled, fetchImuData]);

	useEffect(() => {
		if (!enabled) setShowGyroModal(false);
	}, [enabled]);

	const engageKeysValue: EngageKeyOption[] = useMemo(
		() => engageKeyOptions.filter((opt) => engageKeys.includes(opt.value)),
		[engageKeyOptions, engageKeys],
	);

	const onEngageKeysChange = (selected: MultiValue<EngageKeyOption>) => {
		setFieldValue('lsm6dsrEngageKeys', selected ? selected.map((o) => o.value) : []);
	};

	// 左摇杆(1)、右摇杆(2) 均进入模拟摇杆逻辑，由 outputMode 区分
	const onOutputModeChange = (e: React.ChangeEvent<HTMLSelectElement>) => {
		handleChange(e);
	};

	const twoColStyle = { display: 'flex' as const, gap: COLUMN_GAP };

	return (
		<>
		<Section title={t('CalibrationSettings:gyro-settings-title')}>
			{/* 2 行 3 列：列宽 400px, 400px, 150px；内部下拉框宽 350px */}
			<div
				style={{
					display: 'grid',
					gridTemplateColumns: '400px 400px 150px',
					alignItems: 'start',
					justifyItems: 'start',
					gap: '8px 12px',
					marginBottom: '16px',
				}}
			>
				{/* 1 行 1 列：陀螺仪模拟方式 */}
				<div style={{ width: '100%' }}>
					<Form.Label className="mb-0">{t('CalibrationSettings:gyro-simulation-mode-label')}</Form.Label>
					<Form.Select
						name="lsm6dsrOutputMode"
						className="form-select-sm mt-1"
						style={{ width: '350px' }}
						value={outputMode}
						isInvalid={Boolean(errors.lsm6dsrOutputMode)}
						onChange={onOutputModeChange}
					>
						<option value={LSM6DSR_OUTPUT_DS4}>{t('CalibrationSettings:gyro-mode-ds4')}</option>
						<option value={LSM6DSR_OUTPUT_LEFT_STICK}>{t('CalibrationSettings:gyro-mode-left-stick')}</option>
						<option value={LSM6DSR_OUTPUT_RIGHT_STICK}>{t('CalibrationSettings:gyro-mode-right-stick')}</option>
						<option value={LSM6DSR_OUTPUT_MOUSE}>{t('CalibrationSettings:gyro-mode-mouse')}</option>
					</Form.Select>
				</div>
				{/* 1 行 2 列：生效方式 */}
				<div style={{ width: '100%' }}>
					<Form.Label className="mb-0">{t('CalibrationSettings:gyro-engage-mode-label-short')}</Form.Label>
					<Form.Select
						name="lsm6dsrEngageMode"
						className="form-select-sm mt-1"
						style={{ width: '350px' }}
						value={engageMode}
						onChange={handleChange}
					>
						<option value={GYRO_ENGAGE_ALWAYS}>{t('CalibrationSettings:gyro-engage-always')}</option>
						<option value={GYRO_ENGAGE_ON_KEY}>{t('CalibrationSettings:gyro-engage-on-key')}</option>
						<option value={GYRO_ENGAGE_PAUSE_ON_KEY}>{t('CalibrationSettings:gyro-engage-pause-on-key')}</option>
					</Form.Select>
				</div>
				{/* 1 行 3 列：尖峰滤波开关 */}
				<div style={{ width: '100%', display: 'flex', flexDirection: 'column', alignItems: 'flex-start' }}>
					<Form.Label className="mb-0">尖峰滤波</Form.Label>
					<Form.Check
						type="switch"
						id="lsm6dsrSpikeFilterEnabled"
						label=""
						checked={spikeFilterEnabled}
						onChange={() => setFieldValue('lsm6dsrSpikeFilterEnabled', spikeFilterEnabled ? 0 : 1)}
						className="mt-1"
					/>
				</div>
				{/* 2 行 1 列：生效按键 */}
				<div style={{ width: '100%' }}>
					<Form.Label className="mb-0">{t('CalibrationSettings:gyro-engage-keys-label-short')}</Form.Label>
					<div className="mt-1" style={{ width: '350px' }}>
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
				</div>
				{/* 2 行 2 列：校准陀螺仪、水平面校准、查看陀螺仪 */}
				<div style={{ width: '100%', display: 'flex', flexDirection: 'column', alignItems: 'flex-start', gap: '6px' }}>
					<Form.Label className="mb-0">&nbsp;</Form.Label>
					<div className="mt-1" style={{ display: 'flex', flexWrap: 'wrap', gap: '8px' }}>
						<Button variant="primary" size="sm" style={{ width: GYRO_BUTTON_WIDTH }} onClick={handleCalibrate}>
							{t('CalibrationSettings:gyro-calibrate-button')}
						</Button>
						<Button variant="outline-secondary" size="sm" style={{ width: GYRO_BUTTON_WIDTH }} onClick={() => {}}>
							水平面校准
						</Button>
						<Button variant="outline-secondary" size="sm" style={{ width: GYRO_BUTTON_WIDTH }} onClick={() => setShowGyroModal(true)}>
							{t('CalibrationSettings:gyro-view-gyro-button')}
						</Button>
					</div>
					{calibrateMessage ? (
						<span className={calibrateOk === false ? 'text-danger' : 'text-success'} style={{ fontSize: '0.875rem' }}>
							{calibrateMessage}
						</span>
					) : null}
				</div>
				{/* 2 行 3 列：一欧元滤波开关 */}
				<div style={{ width: '100%', display: 'flex', flexDirection: 'column', alignItems: 'flex-start' }}>
					<Form.Label className="mb-0">一欧元滤波</Form.Label>
					<Form.Check
						type="switch"
						id="lsm6dsrOneEuroFilterEnabled"
						label=""
						checked={oneEuroFilterEnabled}
						onChange={() => setFieldValue('lsm6dsrOneEuroFilterEnabled', oneEuroFilterEnabled ? 0 : 1)}
						className="mt-1"
					/>
				</div>
			</div>

					{/* 查看陀螺仪模态框：调试信息移入此处 */}
					<Modal show={showGyroModal} onHide={() => setShowGyroModal(false)} size="lg" centered>
						<Modal.Header closeButton>
							<Modal.Title>{t('CalibrationSettings:gyro-view-data-title')}</Modal.Title>
						</Modal.Header>
						<Modal.Body>
							<p className="text-muted small mb-2">{t('CalibrationSettings:gyro-view-data-realtime-hint')}</p>
							<div className="p-2 rounded small bg-dark text-white">
								<div className="fw-semibold mb-1">{t('CalibrationSettings:gyro-debug-title')}</div>
								{imuDataError && (
									<p className="text-danger mb-0 small">{t('CalibrationSettings:gyro-view-data-error')}</p>
								)}
								{!imuDataError && !imuData && <span>{t('Common:loading-text')}</span>}
								{!imuDataError && imuData && (
									<pre className="mb-0 small" style={{ fontSize: '0.8rem' }}>
										{`${t('CalibrationSettings:gyro-calibration-offset')}\nOffset X: ${imuData.offsetGyroX ?? '-'}  Y: ${imuData.offsetGyroY ?? '-'}  Z: ${imuData.offsetGyroZ ?? '-'}\n\nGyro X (RAW): ${imuData.gyroX ?? '-'}\nGyro Y (RAW): ${imuData.gyroY ?? '-'}\nGyro Z (RAW): ${imuData.gyroZ ?? '-'}\nAccel X (RAW): ${imuData.accelX ?? '-'}\nAccel Y (RAW): ${imuData.accelY ?? '-'}\nAccel Z (RAW): ${imuData.accelZ ?? '-'}`}
									</pre>
								)}
							</div>
					</Modal.Body>
				</Modal>
			<div style={{ marginTop: '16px', display: 'flex', justifyContent: 'space-between', alignItems: 'center', width: '100%', flexWrap: 'wrap', gap: '12px' }}>
				<div style={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
					<Button
						variant="primary"
						onClick={handleSave}
						disabled={!onSaveClick}
					>
						{t('Common:button-save-label')}
					</Button>
					{saveMessage ? (
						<span className={saveMessage.includes('成功') || saveMessage.includes('success') ? 'text-success' : 'text-danger'}>
							{saveMessage}
						</span>
					) : null}
				</div>
				<div style={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
					<Form.Check
						type="switch"
						id="gyro-enable-switch"
						label={t('CalibrationSettings:gyro-enable-label', '启用陀螺仪')}
						checked={enabled}
						onChange={(e) => {
							setFieldValue('LSM6DSRAddonEnabled', e.target.checked ? 1 : 0);
						}}
					/>
				</div>
			</div>
		</Section>

		<Section title="陀螺仪模拟设置">
			<div style={{ ...twoColStyle, marginBottom: '16px' }}>
				<div style={{ width: DROPDOWN_WIDTH }}>
					<Form.Label className="mb-1">摇杆灵敏度 {stickSensitivity}</Form.Label>
					<Form.Range min={0} max={10} step={0.1} value={stickSensitivity} onChange={(e) => setStickSensitivity(Number(e.target.value))} style={{ width: '100%' }} />
				</div>
				<div style={{ width: DROPDOWN_WIDTH }}>
					<Form.Label className="mb-1">摇杆阈值 {stickThreshold}</Form.Label>
					<Form.Range min={0} max={10} step={0.1} value={stickThreshold} onChange={(e) => setStickThreshold(Number(e.target.value))} style={{ width: '100%' }} />
				</div>
			</div>
			<div style={{ ...twoColStyle, marginBottom: '16px' }}>
				<div style={{ width: DROPDOWN_WIDTH }}>
					<Form.Label className="mb-1">鼠标灵敏度 {mouseSensitivity}</Form.Label>
					<Form.Range min={0} max={10} step={0.1} value={mouseSensitivity} onChange={(e) => setMouseSensitivity(Number(e.target.value))} style={{ width: '100%' }} />
				</div>
				<div style={{ width: DROPDOWN_WIDTH }}>
					<Form.Label className="mb-1">鼠标阈值 {mouseThreshold}</Form.Label>
					<Form.Range min={0} max={10} step={0.1} value={mouseThreshold} onChange={(e) => setMouseThreshold(Number(e.target.value))} style={{ width: '100%' }} />
				</div>
			</div>
			<div style={{ ...twoColStyle, marginBottom: '0' }}>
				<div style={{ width: DROPDOWN_WIDTH }}>
					<Form.Label className="mb-1">控制模式</Form.Label>
					<Form.Select size="sm" style={{ width: '100%' }}>
						<option>自由俯仰角</option>
						<option>自由横滚角</option>
					</Form.Select>
				</div>
				<div style={{ width: DROPDOWN_WIDTH }}>
					<Form.Label className="mb-1">摇杆反转</Form.Label>
					<Form.Select size="sm" style={{ width: '100%' }}>
						<option>水平反转</option>
						<option>垂直反转</option>
					</Form.Select>
				</div>
			</div>
		</Section>
		</>
	);
}
