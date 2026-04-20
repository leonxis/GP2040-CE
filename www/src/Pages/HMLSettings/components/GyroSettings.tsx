import { useCallback, useContext, useEffect, useMemo, useRef, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { MultiValue } from 'react-select';
import { Button, Form, Modal } from 'react-bootstrap';
import Section from '../../../Components/Section';
import CustomSelect from '../../../Components/CustomSelect';
import { AppContext } from '../../../Contexts/AppContext';
import {
	LSM6DSR_OUTPUT_DS4,
	LSM6DSR_OUTPUT_MOUSE,
} from '../../../Addons/LSM6DSR';
import { BUTTON_ACTIONS } from '../../../Data/Pins';
import { BUTTON_MASKS, DPAD_MASKS, getButtonLabels } from '../../../Data/Buttons';
import WebApi from '../../../Services/WebApi';

type EngageKeyOption = { value: number; label: string };
const GYRO_BUTTON_WIDTH = 120;
const IMU_POLL_INTERVAL_MS = 150;

const GYRO_ENGAGE_ALWAYS = 0;
const GYRO_ENGAGE_ON_KEY = 1;
const GYRO_ENGAGE_PAUSE_ON_KEY = 2;

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

	const enabled = Boolean(values.LSM6DSRAddonEnabled);
	const outputMode = Number(values.lsm6dsrOutputMode) ?? 0;
	const engageMode = Number(values.lsm6dsrEngageMode) ?? 0;
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
		setCalibrateMessage(t('CalibrationSettings:gyro-calibrate-in-progress'));
		setCalibrateOk(null);
		try {
			const data = await WebApi.calibrateLSM6DSRGyro();
			if (data?.ok) {
				setFieldValue('lsm6dsrOffsetGyroX', data.offsetGyroX ?? 0);
				setFieldValue('lsm6dsrOffsetGyroY', data.offsetGyroY ?? 0);
				setFieldValue('lsm6dsrOffsetGyroZ', data.offsetGyroZ ?? 0);
				setCalibrateMessage(t('CalibrationSettings:gyro-calibrate-success'));
				setCalibrateOk(true);
			} else {
				setCalibrateMessage(t('CalibrationSettings:gyro-calibrate-fail'));
				setCalibrateOk(false);
			}
		} catch {
			setCalibrateMessage(t('CalibrationSettings:gyro-calibrate-fail'));
			setCalibrateOk(false);
		}
	};
	const handleCalibrateAccel = async () => {
		setCalibrateMessage('');
		setCalibrateOk(null);
		try {
			const data = await WebApi.calibrateLSM6DSRAccel();
			if (data?.ok) {
				setFieldValue('lsm6dsrOffsetAccelX', data.offsetAccelX ?? 0);
				setFieldValue('lsm6dsrOffsetAccelY', data.offsetAccelY ?? 0);
				setFieldValue('lsm6dsrOffsetAccelZ', data.offsetAccelZ ?? 0);
				setCalibrateMessage(t('CalibrationSettings:accel-calibrate-success'));
				setCalibrateOk(true);
			} else {
				setCalibrateMessage(t('CalibrationSettings:accel-calibrate-fail'));
				setCalibrateOk(false);
			}
		} catch {
			setCalibrateMessage(t('CalibrationSettings:accel-calibrate-fail'));
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

	const onOutputModeChange = (e: React.ChangeEvent<HTMLSelectElement>) => {
		handleChange(e);
	};

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
						<Button variant="outline-secondary" size="sm" style={{ width: GYRO_BUTTON_WIDTH }} onClick={handleCalibrateAccel}>
							{t('CalibrationSettings:gyro-accel-calibrate-button')}
						</Button>
						<Button variant="outline-secondary" size="sm" style={{ width: GYRO_BUTTON_WIDTH }} onClick={() => setShowGyroModal(true)}>
							{t('CalibrationSettings:gyro-view-gyro-button')}
						</Button>
					</div>
					{calibrateMessage ? (
						<span
							className={
								calibrateOk === false
									? 'text-danger'
									: calibrateOk === true
										? 'text-success'
										: 'text-muted'
							}
							style={{ fontSize: '0.875rem' }}
						>
							{calibrateMessage}
						</span>
					) : null}
				</div>
				{/* 2 行 3 列：一欧元滤波开关 */}
				<div style={{ width: '100%', display: 'flex', flexDirection: 'column', alignItems: 'flex-start' }}>
					<Form.Label className="mb-0">{t('CalibrationSettings:gyro-one-euro-filter-label')}</Form.Label>
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
						<span className={saveMessage === t('Common:saved-success-message') ? 'text-success' : 'text-danger'}>
							{saveMessage}
						</span>
					) : null}
				</div>
				<div style={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
					<Form.Check
						type="switch"
						id="gyro-enable-switch"
						label={t('CalibrationSettings:gyro-enable-label')}
						checked={enabled}
						onChange={(e) => {
							setFieldValue('LSM6DSRAddonEnabled', e.target.checked ? 1 : 0);
						}}
					/>
				</div>
			</div>
		</Section>

		{outputMode === LSM6DSR_OUTPUT_MOUSE && (
		<Section title={t('CalibrationSettings:gyro-mouse-options-title')}>
			<div style={{ display: 'grid', gridTemplateColumns: '400px 400px', gap: '12px 24px', marginBottom: '12px' }}>
				<div>
					<Form.Label className="mb-0">{t('CalibrationSettings:gyro-mouse-map-mode-label')}</Form.Label>
					<Form.Select
						className="form-select-sm mt-1"
						style={{ width: '350px' }}
						value={Number(values.lsm6dsrGyroMouseMapMode) ?? 0}
						onChange={(e) => setFieldValue('lsm6dsrGyroMouseMapMode', Number(e.target.value))}
					>
						<option value={0}>{t('CalibrationSettings:gyro-mouse-map-xy')}</option>
						<option value={1}>{t('CalibrationSettings:gyro-mouse-map-xz')}</option>
					</Form.Select>
				</div>
				<div>
					<Form.Label className="mb-0">{t('CalibrationSettings:gyro-mouse-invert-label')}</Form.Label>
					<Form.Select
						className="form-select-sm mt-1"
						style={{ width: '350px' }}
						value={Number(values.lsm6dsrGyroMouseInvert) ?? 0}
						onChange={(e) => setFieldValue('lsm6dsrGyroMouseInvert', Number(e.target.value))}
					>
						<option value={0}>{t('CalibrationSettings:gyro-mouse-invert-none')}</option>
						<option value={1}>{t('CalibrationSettings:gyro-mouse-invert-lr')}</option>
						<option value={2}>{t('CalibrationSettings:gyro-mouse-invert-ud')}</option>
						<option value={3}>{t('CalibrationSettings:gyro-mouse-invert-both')}</option>
					</Form.Select>
				</div>
				<div>
					<Form.Label className="mb-0">{t('CalibrationSettings:gyro-mouse-sens-lr-label')} {(Math.max(0.1, Math.min(3, Number(values.lsm6dsrGyroMouseSensLR) || 1))).toFixed(1)}</Form.Label>
					<Form.Range
						min={0.1}
						max={3}
						step={0.1}
						value={Math.max(0.1, Math.min(3, Number(values.lsm6dsrGyroMouseSensLR) || 1))}
						onChange={(e) => setFieldValue('lsm6dsrGyroMouseSensLR', Number(e.target.value))}
						style={{ width: '100%' }}
					/>
				</div>
				<div>
					<Form.Label className="mb-0">{t('CalibrationSettings:gyro-mouse-sens-ud-label')} {(Math.max(0.1, Math.min(3, Number(values.lsm6dsrGyroMouseSensUD) || 1))).toFixed(1)}</Form.Label>
					<Form.Range
						min={0.1}
						max={3}
						step={0.1}
						value={Math.max(0.1, Math.min(3, Number(values.lsm6dsrGyroMouseSensUD) || 1))}
						onChange={(e) => setFieldValue('lsm6dsrGyroMouseSensUD', Number(e.target.value))}
						style={{ width: '100%' }}
					/>
				</div>
				<div>
					<Form.Label className="mb-0">
						{t('CalibrationSettings:gyro-mouse-deadzone-label')}
						{' '}
						{Math.max(0, Math.min(80, Number(values.lsm6dsrGyroMouseDeadzone ?? 12))).toFixed(0)}
					</Form.Label>
					<Form.Range
						min={0}
						max={80}
						step={1}
						value={Math.max(0, Math.min(80, Number(values.lsm6dsrGyroMouseDeadzone ?? 12)))}
						onChange={(e) => setFieldValue('lsm6dsrGyroMouseDeadzone', Number(e.target.value))}
						style={{ width: '100%' }}
					/>
				</div>
			</div>
		</Section>
		)}
		</>
	);
}
