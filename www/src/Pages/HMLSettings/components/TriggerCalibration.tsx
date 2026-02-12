import { useEffect, useRef, useState } from 'react';
import { Button, Form, Modal } from 'react-bootstrap';
import type { FormikHelpers } from 'formik';
import { useFormikContext } from 'formik';
import { useTranslation } from 'react-i18next';
import * as yup from 'yup';

export const triggerCalibrationScheme = {
	leftTriggerDeadzone: yup.number().min(0).max(99).label('左扳机死区'),
	rightTriggerDeadzone: yup.number().min(0).max(99).label('右扳机死区'),
	leftTriggerTravel: yup.number().min(1).max(100).label('左扳机行程'),
	rightTriggerTravel: yup.number().min(1).max(100).label('右扳机行程'),
};

export const triggerCalibrationState = {
	leftTriggerDeadzone: 5,
	rightTriggerDeadzone: 5,
	leftTriggerTravel: 95,
	rightTriggerTravel: 95,
};

export type TriggerCalibrationValues = typeof triggerCalibrationState;

const TRIGGER_CANVAS_W = 120;
const TRIGGER_CANVAS_H = 250;
const CANVAS_COLUMN_WIDTH = 250;
const CONTROLS_COLUMN_WIDTH = 250;
const MIN_TRAVEL_ABOVE_DEADZONE = 5; // 扳机行程 > 扳机死区 + 5%

type TriggerCalibrationBlockProps = {
	values: TriggerCalibrationValues & Record<string, unknown>;
	setFieldValue: FormikHelpers<TriggerCalibrationValues & Record<string, unknown>>['setFieldValue'];
};

export function TriggerCalibrationBlock({ values, setFieldValue }: TriggerCalibrationBlockProps) {
	const { handleSubmit } = useFormikContext();
	const { t } = useTranslation();
	const leftCanvasRef = useRef<HTMLCanvasElement>(null);
	const rightCanvasRef = useRef<HTMLCanvasElement>(null);
	const [showCalibrateModal, setShowCalibrateModal] = useState(false);
	const [calibrateSide, setCalibrateSide] = useState<'left' | 'right'>('left');

	const leftDeadzone = Number(values.leftTriggerDeadzone) ?? 5;
	const rightDeadzone = Number(values.rightTriggerDeadzone) ?? 5;
	const leftTravel = Number(values.leftTriggerTravel) ?? 95;
	const rightTravel = Number(values.rightTriggerTravel) ?? 95;

	// 约束：扳机行程 > 扳机死区 + 5%。调整时如不满足则推动另一滑块
	const applyLeftDeadzone = (v: number) => {
		setFieldValue('leftTriggerDeadzone', v);
		if (leftTravel <= v + MIN_TRAVEL_ABOVE_DEADZONE) {
			setFieldValue('leftTriggerTravel', Math.min(100, v + MIN_TRAVEL_ABOVE_DEADZONE + 1));
		}
	};
	const applyRightDeadzone = (v: number) => {
		setFieldValue('rightTriggerDeadzone', v);
		if (rightTravel <= v + MIN_TRAVEL_ABOVE_DEADZONE) {
			setFieldValue('rightTriggerTravel', Math.min(100, v + MIN_TRAVEL_ABOVE_DEADZONE + 1));
		}
	};
	const applyLeftTravel = (v: number) => {
		setFieldValue('leftTriggerTravel', v);
		if (leftDeadzone >= v - MIN_TRAVEL_ABOVE_DEADZONE) {
			setFieldValue('leftTriggerDeadzone', Math.max(0, v - MIN_TRAVEL_ABOVE_DEADZONE - 1));
		}
	};
	const applyRightTravel = (v: number) => {
		setFieldValue('rightTriggerTravel', v);
		if (rightDeadzone >= v - MIN_TRAVEL_ABOVE_DEADZONE) {
			setFieldValue('rightTriggerDeadzone', Math.max(0, v - MIN_TRAVEL_ABOVE_DEADZONE - 1));
		}
	};

	// 扳机行程 canvas 占位（背景透明，显示逻辑后续补充）
	useEffect(() => {
		const canvas = leftCanvasRef.current;
		if (!canvas) return;
		const ctx = canvas.getContext('2d');
		if (!ctx) return;
		ctx.clearRect(0, 0, TRIGGER_CANVAS_W, TRIGGER_CANVAS_H);
		ctx.strokeStyle = '#999';
		ctx.strokeRect(0, 0, TRIGGER_CANVAS_W, TRIGGER_CANVAS_H);
		ctx.fillStyle = '#333';
		ctx.font = '12px sans-serif';
		ctx.textAlign = 'center';
		ctx.fillText('左扳机行程', TRIGGER_CANVAS_W / 2, TRIGGER_CANVAS_H / 2);
	}, []);
	useEffect(() => {
		const canvas = rightCanvasRef.current;
		if (!canvas) return;
		const ctx = canvas.getContext('2d');
		if (!ctx) return;
		ctx.clearRect(0, 0, TRIGGER_CANVAS_W, TRIGGER_CANVAS_H);
		ctx.strokeStyle = '#999';
		ctx.strokeRect(0, 0, TRIGGER_CANVAS_W, TRIGGER_CANVAS_H);
		ctx.fillStyle = '#333';
		ctx.font = '12px sans-serif';
		ctx.textAlign = 'center';
		ctx.fillText('右扳机行程', TRIGGER_CANVAS_W / 2, TRIGGER_CANVAS_H / 2);
	}, []);

	const openCalibrateModal = (side: 'left' | 'right') => {
		setCalibrateSide(side);
		setShowCalibrateModal(true);
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
						<div style={{ marginBottom: '2px' }}>左扳机死区：{leftDeadzone}%</div>
						<Form.Range
							min={0}
							max={99}
							value={leftDeadzone}
							onChange={(e) => applyLeftDeadzone(Number(e.target.value))}
						/>
						<div style={{ marginBottom: '2px', marginTop: '4px' }}>左扳机行程：{leftTravel}%</div>
						<Form.Range
							min={1}
							max={100}
							value={leftTravel}
							onChange={(e) => applyLeftTravel(Number(e.target.value))}
						/>
						<div style={{ marginTop: '8px', display: 'flex', justifyContent: 'center' }}>
							<Button variant="primary" size="sm" onClick={() => openCalibrateModal('left')}>
								校准左扳机
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
						<div style={{ marginBottom: '2px' }}>右扳机死区：{rightDeadzone}%</div>
						<Form.Range
							min={0}
							max={99}
							value={rightDeadzone}
							onChange={(e) => applyRightDeadzone(Number(e.target.value))}
						/>
						<div style={{ marginBottom: '2px', marginTop: '4px' }}>右扳机行程：{rightTravel}%</div>
						<Form.Range
							min={1}
							max={100}
							value={rightTravel}
							onChange={(e) => applyRightTravel(Number(e.target.value))}
						/>
						<div style={{ marginTop: '8px', display: 'flex', justifyContent: 'center' }}>
							<Button variant="primary" size="sm" onClick={() => openCalibrateModal('right')}>
								校准右扳机
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

			{/* 保存按钮：与摇杆校准等栏目相同，独立于 1 行 4 列布局 */}
			<div className="mt-3">
				<Button type="button" variant="primary" onClick={() => handleSubmit()}>
					{t('Common:button-save-label')}
				</Button>
			</div>

			{/* 扳机校准模态框（逻辑后续补充） */}
			<Modal show={showCalibrateModal} onHide={() => setShowCalibrateModal(false)} centered>
				<Modal.Header closeButton>
					<Modal.Title>{calibrateSide === 'left' ? '校准左扳机' : '校准右扳机'}</Modal.Title>
				</Modal.Header>
				<Modal.Body>扳机最大行程校准（逻辑后续补充）</Modal.Body>
				<Modal.Footer>
					<Button variant="secondary" onClick={() => setShowCalibrateModal(false)}>
						关闭
					</Button>
				</Modal.Footer>
			</Modal>
		</>
	);
}

export default TriggerCalibrationBlock;
