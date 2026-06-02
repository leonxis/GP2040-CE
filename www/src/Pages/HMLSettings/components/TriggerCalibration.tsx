import { useEffect, useRef, useState } from 'react';
import { Button, Form, Modal } from 'react-bootstrap';
import { useFormikContext } from 'formik';
import { useTranslation } from 'react-i18next';
import * as yup from 'yup';

import Section from '../../../Components/Section';
import WebApi from '../../../Services/WebApi';
import i18n from '../../../i18n';

const MIN_TRAVEL_ABOVE_DEADZONE = 1; // 扳机行程须大于死区，如死区 4% 则行程最小 5%
const ADC_MAX = 4095;

const tc = (key: string) => i18n.t(`CalibrationSettings:${key}`);

export const triggerCalibrationScheme = {
	linearTriggerEnabled: yup.number().min(0).max(1).label(tc('hml-yup-linear-trigger')),
	leftTriggerDeadzone: yup.number().min(0).max(99).label(tc('hml-yup-left-deadzone')),
	rightTriggerDeadzone: yup.number().min(0).max(99).label(tc('hml-yup-right-deadzone')),
	// 校准得到的原始 ADC：松开时的值、按到底时的值（0–4095），供后端校准使用，与行程滑块独立
	leftTriggerReleasedRaw: yup.number().min(-1).max(ADC_MAX).label(tc('hml-yup-left-released')),
	rightTriggerReleasedRaw: yup.number().min(-1).max(ADC_MAX).label(tc('hml-yup-right-released')),
	leftTriggerMaxRaw: yup.number().min(-1).max(ADC_MAX).label(tc('hml-yup-left-max')),
	rightTriggerMaxRaw: yup.number().min(-1).max(ADC_MAX).label(tc('hml-yup-right-max')),
	leftTriggerTravel: yup
		.number()
		.min(1)
		.max(100)
		.test(
			'travel-above-deadzone',
			tc('hml-yup-travel-above-deadzone'),
			(value, ctx) =>
				Number(value) > Number(ctx.parent?.leftTriggerDeadzone ?? 0),
		)
		.label(tc('hml-yup-left-travel')),
	rightTriggerTravel: yup
		.number()
		.min(1)
		.max(100)
		.test(
			'travel-above-deadzone',
			tc('hml-yup-travel-above-deadzone'),
			(value, ctx) =>
				Number(value) > Number(ctx.parent?.rightTriggerDeadzone ?? 0),
		)
		.label(tc('hml-yup-right-travel')),
	leftTriggerInvert: yup.number().min(0).max(1),
	rightTriggerInvert: yup.number().min(0).max(1),
};

// 默认：按下 ADC 降低；极性由校准写入 left/rightTriggerInvert
export const triggerCalibrationState = {
	linearTriggerEnabled: 0,
	leftTriggerDeadzone: 5,
	rightTriggerDeadzone: 5,
	leftTriggerTravel: 95,
	rightTriggerTravel: 95,
	leftTriggerReleasedRaw: 4095,
	rightTriggerReleasedRaw: 4095,
	leftTriggerMaxRaw: 0,
	rightTriggerMaxRaw: 0,
	leftTriggerInvert: 0,
	rightTriggerInvert: 0,
};

const TRIGGER_CANVAS_W = 120;
const TRIGGER_CANVAS_H = 250;
const CANVAS_COLUMN_WIDTH = 250;
const CONTROLS_COLUMN_WIDTH = 250;

type TriggerCalibrationBlockProps = {
	values: Record<string, unknown>;
	setFieldValue: (field: string, value: unknown) => void;
};

function TriggerCalibrationBlock({ values, setFieldValue }: TriggerCalibrationBlockProps) {
	const { t } = useTranslation();
	const leftCanvasRef = useRef<HTMLCanvasElement>(null);
	const rightCanvasRef = useRef<HTMLCanvasElement>(null);
	const [showCalibrateModal, setShowCalibrateModal] = useState(false);
	const [calibrateSide, setCalibrateSide] = useState<'left' | 'right'>('left');
	const [calibrateStep, setCalibrateStep] = useState<1 | 2>(1); // 1=松开扳机 2=按到底
	const [releasedRawCurrent, setReleasedRawCurrent] = useState<number | null>(null);
	// 仅第二步点击确定且未取消时才写入表单；取消或异步期间关闭时不再写入
	const calibrationActiveRef = useRef(false);
	// 当前扳机 ADC 原始值，用于 canvas 实时填充高度
	const [leftTriggerCurrentRaw, setLeftTriggerCurrentRaw] = useState<number>(0);
	const [rightTriggerCurrentRaw, setRightTriggerCurrentRaw] = useState<number>(0);

	const leftDeadzone = Number(values.leftTriggerDeadzone) ?? 5;
	const rightDeadzone = Number(values.rightTriggerDeadzone) ?? 5;
	const leftTravel = Number(values.leftTriggerTravel) ?? 95;
	const rightTravel = Number(values.rightTriggerTravel) ?? 95;
	const leftReleasedRaw = Number(values.leftTriggerReleasedRaw) ?? 0;
	const rightReleasedRaw = Number(values.rightTriggerReleasedRaw) ?? 0;
	const leftMaxRaw = Number(values.leftTriggerMaxRaw) ?? 4095;
	const rightMaxRaw = Number(values.rightTriggerMaxRaw) ?? 4095;
	const leftInvert = Boolean(Number(values.leftTriggerInvert));
	const rightInvert = Boolean(Number(values.rightTriggerInvert));

	// 约束：扳机行程必须大于死区（如死区 4% 则行程最小 5%）。调整时如不满足则推动另一滑块
	const applyLeftDeadzone = (v: number) => {
		setFieldValue('leftTriggerDeadzone', v);
		if (leftTravel <= v) {
			setFieldValue('leftTriggerTravel', Math.min(100, v + MIN_TRAVEL_ABOVE_DEADZONE));
		}
	};
	const applyRightDeadzone = (v: number) => {
		setFieldValue('rightTriggerDeadzone', v);
		if (rightTravel <= v) {
			setFieldValue('rightTriggerTravel', Math.min(100, v + MIN_TRAVEL_ABOVE_DEADZONE));
		}
	};
	const applyLeftTravel = (v: number) => {
		setFieldValue('leftTriggerTravel', v);
		if (leftDeadzone >= v) {
			setFieldValue('leftTriggerDeadzone', Math.max(0, v - MIN_TRAVEL_ABOVE_DEADZONE));
		}
	};
	const applyRightTravel = (v: number) => {
		setFieldValue('rightTriggerTravel', v);
		if (rightDeadzone >= v) {
			setFieldValue('rightTriggerDeadzone', Math.max(0, v - MIN_TRAVEL_ABOVE_DEADZONE));
		}
	};

	// 轮询当前扳机 ADC，用于 canvas 实时显示（异步请求，不阻塞主线程，与同页摇杆轮询/绘制互不阻塞）
	useEffect(() => {
		let cancelled = false;
		const poll = async () => {
			if (cancelled) return;
			try {
				const data = await WebApi.getTriggerAdcValues();
				if (cancelled || !data) return;
				if (typeof data.leftTriggerRaw === 'number') setLeftTriggerCurrentRaw(data.leftTriggerRaw);
				if (typeof data.rightTriggerRaw === 'number') setRightTriggerCurrentRaw(data.rightTriggerRaw);
			} catch {
				// ignore
			}
		};
		poll();
		const id = setInterval(poll, 80);
		return () => {
			cancelled = true;
			clearInterval(id);
		};
	}, []);

	// 绘制扳机行程 canvas：Y 轴=行程（底=松开，顶=按到底），死区/行程横线，按当前输出比例填充（范围外浅灰 40%，死区-行程段紫色 40%）
	const drawTriggerCanvas = (
		ctx: CanvasRenderingContext2D,
		deadzonePct: number,
		travelPct: number,
		releasedRaw: number,
		maxRaw: number,
		currentRaw: number,
		invert: boolean,
	) => {
		const w = TRIGGER_CANVAS_W;
		const h = TRIGGER_CANVAS_H;
		ctx.clearRect(0, 0, w, h);
		// Y 轴：底部=0%（松开），顶部=100%（按到底）；方向由持久化极性标记决定
		const yDeadzone = h * (1 - deadzonePct / 100); // 死区横线（靠近底部）
		const yTravel = h * (1 - travelPct / 100); // 行程横线（靠近顶部）
		const range = Math.max(1, Math.abs(maxRaw - releasedRaw));
		const currentPercent = invert
			? ((currentRaw - releasedRaw) / range) * 100
			: ((releasedRaw - currentRaw) / range) * 100;
		const currentPercentClamped = Math.min(100, Math.max(0, currentPercent));
		const fillTop = h * (1 - currentPercentClamped / 100); // 填充上边界（canvas y 向下为正）

		// 1. 死区横线（深灰）
		ctx.strokeStyle = '#4a4a4a';
		ctx.lineWidth = 1.5;
		ctx.beginPath();
		ctx.moveTo(0, yDeadzone);
		ctx.lineTo(w, yDeadzone);
		ctx.stroke();

		// 2. 行程横线（黄色）
		ctx.strokeStyle = '#d4a800';
		ctx.beginPath();
		ctx.moveTo(0, yTravel);
		ctx.lineTo(w, yTravel);
		ctx.stroke();

		// 3. 从底部到当前输出高度的填充：横线范围内紫色 40%，范围外浅灰 40%
		// 填充区域 y: [fillTop, h]。分段： [fillTop, yTravel] 浅灰；[yTravel, yDeadzone] 紫；[yDeadzone, h] 浅灰
		const lightGray = 'rgba(200, 200, 200, 0.4)';
		const purple = 'rgba(128, 0, 128, 0.4)';

		// 浅灰色：从 fillTop 到 yTravel（若存在）
		const grayTopH = Math.max(0, Math.min(yTravel, h) - fillTop);
		if (grayTopH > 0) {
			ctx.fillStyle = lightGray;
			ctx.fillRect(0, fillTop, w, grayTopH);
		}
		// 紫色：yTravel 到 yDeadzone 与填充区域交集
		const bandTop = Math.max(fillTop, yTravel);
		const bandBottom = Math.min(yDeadzone, h);
		const bandH = Math.max(0, bandBottom - bandTop);
		if (bandH > 0) {
			ctx.fillStyle = purple;
			ctx.fillRect(0, bandTop, w, bandH);
		}
		// 浅灰色：yDeadzone 到 h（若存在）
		const grayBottomY = Math.max(fillTop, yDeadzone);
		const grayBottomH = Math.max(0, h - grayBottomY);
		if (grayBottomH > 0) {
			ctx.fillStyle = lightGray;
			ctx.fillRect(0, grayBottomY, w, grayBottomH);
		}
	};

	useEffect(() => {
		const canvas = leftCanvasRef.current;
		if (!canvas) return;
		const ctx = canvas.getContext('2d');
		if (!ctx) return;
		drawTriggerCanvas(ctx, leftDeadzone, leftTravel, leftReleasedRaw, leftMaxRaw, leftTriggerCurrentRaw, leftInvert);
	}, [
		leftDeadzone,
		leftTravel,
		leftReleasedRaw,
		leftMaxRaw,
		leftTriggerCurrentRaw,
		leftInvert,
	]);

	useEffect(() => {
		const canvas = rightCanvasRef.current;
		if (!canvas) return;
		const ctx = canvas.getContext('2d');
		if (!ctx) return;
		drawTriggerCanvas(ctx, rightDeadzone, rightTravel, rightReleasedRaw, rightMaxRaw, rightTriggerCurrentRaw, rightInvert);
	}, [
		rightDeadzone,
		rightTravel,
		rightReleasedRaw,
		rightMaxRaw,
		rightTriggerCurrentRaw,
		rightInvert,
	]);

	const openCalibrateModal = (side: 'left' | 'right') => {
		setCalibrateSide(side);
		setCalibrateStep(1);
		setReleasedRawCurrent(null);
		calibrationActiveRef.current = true;
		setShowCalibrateModal(true);
	};

	const handleCalibrateClose = () => {
		calibrationActiveRef.current = false;
		setShowCalibrateModal(false);
		setCalibrateStep(1);
		setReleasedRawCurrent(null);
	};

	const handleCalibrateConfirm = async () => {
		const data = await WebApi.getTriggerAdcValues();
		if (!data || (data.leftTriggerRaw == null && data.rightTriggerRaw == null)) {
			return;
		}
		const raw = calibrateSide === 'left' ? data.leftTriggerRaw : data.rightTriggerRaw;
		const rawNum = Number(raw);

		if (calibrateStep === 1) {
			if (!calibrationActiveRef.current) return;
			setReleasedRawCurrent(rawNum);
			setCalibrateStep(2);
			return;
		}

		// Step 2: 仅在本步点击确定且用户未点击取消时才写入表单
		if (!calibrationActiveRef.current) return;
		const released = releasedRawCurrent ?? 0;
		const maxRaw = rawNum;
		let invert: number | undefined;
		if (released > maxRaw) {
			invert = 0;
		} else if (released < maxRaw) {
			invert = 1;
		}
		if (calibrateSide === 'left') {
			setFieldValue('leftTriggerReleasedRaw', released);
			setFieldValue('leftTriggerMaxRaw', maxRaw);
			if (invert !== undefined) {
				setFieldValue('leftTriggerInvert', invert);
			}
		} else {
			setFieldValue('rightTriggerReleasedRaw', released);
			setFieldValue('rightTriggerMaxRaw', maxRaw);
			if (invert !== undefined) {
				setFieldValue('rightTriggerInvert', invert);
			}
		}
		calibrationActiveRef.current = false;
		handleCalibrateClose();
	};

	return (
		<>
			<div
				style={{
					display: 'flex',
					flexDirection: 'column',
					alignItems: 'center',
					width: '100%',
				}}
			>
				<div
					style={{
						display: 'grid',
						gridTemplateColumns: `${CANVAS_COLUMN_WIDTH}px ${CONTROLS_COLUMN_WIDTH}px ${CONTROLS_COLUMN_WIDTH}px ${CANVAS_COLUMN_WIDTH}px`,
						gridTemplateRows: '1fr',
						gap: '16px',
						alignItems: 'start',
						justifyItems: 'center',
						maxWidth: `${CANVAS_COLUMN_WIDTH * 2 + CONTROLS_COLUMN_WIDTH * 2 + 16 * 3}px`,
					}}
				>
					{/* 第1列：左扳机 canvas（固定 250px，canvas 居中） */}
					<div style={{ width: CANVAS_COLUMN_WIDTH, display: 'flex', justifyContent: 'center', alignItems: 'flex-start' }}>
						<canvas
							ref={leftCanvasRef}
							width={TRIGGER_CANVAS_W}
							height={TRIGGER_CANVAS_H}
							style={{ display: 'block', border: '1px solid #dee2e6', borderRadius: '4px', background: 'transparent' }}
						/>
					</div>

					{/* 第2列：左扳机控制 - 死区标题、死区滑块、行程标题、行程滑块、校准按键 */}
					<div
						style={{
							width: '100%',
							maxWidth: CONTROLS_COLUMN_WIDTH,
							display: 'flex',
							flexDirection: 'column',
							gap: '8px',
						}}
					>
						<div style={{ marginBottom: '2px' }}>{t('CalibrationSettings:hml-left-trigger-deadzone', { pct: leftDeadzone })}</div>
						<Form.Range
							min={0}
							max={99}
							value={leftDeadzone}
							onChange={(e) => applyLeftDeadzone(Number(e.target.value))}
						/>
						<div style={{ marginBottom: '2px', marginTop: '4px' }}>{t('CalibrationSettings:hml-left-trigger-travel', { pct: leftTravel })}</div>
						<Form.Range
							min={1}
							max={100}
							value={leftTravel}
							onChange={(e) => applyLeftTravel(Number(e.target.value))}
						/>
						<div style={{ marginTop: '8px', display: 'flex', justifyContent: 'center' }}>
							<Button variant="primary" size="sm" onClick={() => openCalibrateModal('left')}>
								{t('CalibrationSettings:hml-calibrate-left-trigger')}
							</Button>
						</div>
					</div>

					{/* 第3列：右扳机控制 - 死区标题、死区滑块、行程标题、行程滑块、校准按键 */}
					<div
						style={{
							width: '100%',
							maxWidth: CONTROLS_COLUMN_WIDTH,
							display: 'flex',
							flexDirection: 'column',
							gap: '8px',
						}}
					>
						<div style={{ marginBottom: '2px' }}>{t('CalibrationSettings:hml-right-trigger-deadzone', { pct: rightDeadzone })}</div>
						<Form.Range
							min={0}
							max={99}
							value={rightDeadzone}
							onChange={(e) => applyRightDeadzone(Number(e.target.value))}
						/>
						<div style={{ marginBottom: '2px', marginTop: '4px' }}>{t('CalibrationSettings:hml-right-trigger-travel', { pct: rightTravel })}</div>
						<Form.Range
							min={1}
							max={100}
							value={rightTravel}
							onChange={(e) => applyRightTravel(Number(e.target.value))}
						/>
						<div style={{ marginTop: '8px', display: 'flex', justifyContent: 'center' }}>
							<Button variant="primary" size="sm" onClick={() => openCalibrateModal('right')}>
								{t('CalibrationSettings:hml-calibrate-right-trigger')}
							</Button>
						</div>
					</div>

					{/* 第4列：右扳机 canvas（固定 250px，canvas 居中） */}
					<div style={{ width: CANVAS_COLUMN_WIDTH, display: 'flex', justifyContent: 'center', alignItems: 'flex-start' }}>
						<canvas
							ref={rightCanvasRef}
							width={TRIGGER_CANVAS_W}
							height={TRIGGER_CANVAS_H}
							style={{ display: 'block', border: '1px solid #dee2e6', borderRadius: '4px', background: 'transparent' }}
						/>
					</div>
				</div>
			</div>

			{/* 扳机校准模态框：第一步松开扳机点确定，第二步按到底点确定，得到松开值与最大行程两段数据 */}
			<Modal show={showCalibrateModal} onHide={handleCalibrateClose} centered>
				<Modal.Header closeButton>
					<Modal.Title>
						{calibrateSide === 'left'
							? t('CalibrationSettings:hml-modal-calibrate-left-title')
							: t('CalibrationSettings:hml-modal-calibrate-right-title')}
					</Modal.Title>
				</Modal.Header>
				<Modal.Body>
					{calibrateStep === 1
						? (calibrateSide === 'left'
							? t('CalibrationSettings:hml-modal-calibrate-step1-left')
							: t('CalibrationSettings:hml-modal-calibrate-step1-right'))
						: (calibrateSide === 'left'
							? t('CalibrationSettings:hml-modal-calibrate-step2-left')
							: t('CalibrationSettings:hml-modal-calibrate-step2-right'))}
				</Modal.Body>
				<Modal.Footer style={{ justifyContent: 'flex-end' }}>
					<Button variant="secondary" onClick={handleCalibrateClose}>
						{t('CalibrationSettings:hml-button-cancel')}
					</Button>
					<Button variant="primary" onClick={handleCalibrateConfirm}>
						{t('CalibrationSettings:hml-button-ok')}
					</Button>
				</Modal.Footer>
			</Modal>
		</>
	);
}

interface TriggerCalibrationSettingsProps {
	values: Record<string, unknown>;
	setFieldValue: (field: string, value: unknown) => void;
	saveMessage?: string;
	onSaveClick?: () => void;
}

/**
 * 扳机校准栏（与摇杆曲线设置栏结构一致）：可折叠内容 + 底部一行（保存按钮左、启用开关右），保存与开关始终显示。
 */
export default function TriggerCalibrationSettings({
	values,
	setFieldValue,
	saveMessage = '',
	onSaveClick,
}: TriggerCalibrationSettingsProps) {
	const { t } = useTranslation();
	const { handleSubmit } = useFormikContext();
	const saveOk = saveMessage === t('Common:saved-success-message');

	return (
		<Section title={t('CalibrationSettings:hml-section-trigger-calibration')}>
			{values?.linearTriggerEnabled ? (
				<TriggerCalibrationBlock values={values} setFieldValue={setFieldValue} />
			) : null}
			<div style={{ marginTop: '16px', display: 'flex', justifyContent: 'space-between', alignItems: 'center', width: '100%' }}>
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
				<div style={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
					<Form.Check
						type="switch"
						id="linear-trigger-enabled"
						label={t('CalibrationSettings:hml-enable-linear-trigger-label')}
						checked={Boolean(values?.linearTriggerEnabled)}
						onChange={(e) => {
							const enabled = e.target.checked ? 1 : 0;
							setFieldValue('linearTriggerEnabled', enabled);
						}}
					/>
				</div>
			</div>
		</Section>
	);
}
