import { useState, useEffect, useRef } from 'react';
import { Modal, Button, Spinner } from 'react-bootstrap';
import { useTranslation } from 'react-i18next';

interface RangeCalibrationModalProps {
	show: boolean;
	onHide: () => void;
	onComplete: (rangeData: number[]) => void;
	stickNumber: 1 | 2;
	stickLabel: string;
	centerX?: number;
	centerY?: number;
}

const CIRCULARITY_DATA_SIZE = 48;

const RangeCalibrationModal = ({
	show,
	onHide,
	onComplete,
	stickNumber,
	stickLabel,
	centerX,
	centerY,
}: RangeCalibrationModalProps) => {
	const { t } = useTranslation();
	const [isCollecting, setIsCollecting] = useState(false);
	const [isCompleted, setIsCompleted] = useState(false);
	const [errorMsg, setErrorMsg] = useState('');
	const abortRef = useRef(false);

	// Reset state when modal opens/closes.
	// 注意：不在 show=true 时重置 abortRef，避免上一轮未完成的 fetch 在重开后
	// 返回并写入过期数据。abortRef 仅在 startCalibration 中重置为 false。
	useEffect(() => {
		if (show) {
			setIsCollecting(false);
			setIsCompleted(false);
			setErrorMsg('');
		} else {
			abortRef.current = true;
		}
	}, [show]);

	const startCalibration = async () => {
		setIsCollecting(true);
		setErrorMsg('');
		abortRef.current = false;

		try {
			// 后端高频采样：按轮统计（每轮需48角度全覆盖），完成3轮后返回跨轮最大值
			const res = await fetch('/api/calibrateJoystickRange', {
				method: 'POST',
				headers: { 'Content-Type': 'application/json' },
				body: JSON.stringify({
					stick: stickNumber,
					centerX: centerX ?? 0,
					centerY: centerY ?? 0,
				}),
			});

			if (abortRef.current) return;

			if (!res.ok) {
				throw new Error(`HTTP error! status: ${res.status}`);
			}

			const data = await res.json();
			if (abortRef.current) return;

			if (!data.success) {
				throw new Error(data.error || 'Calibration incomplete');
			}

			// 校验返回的 rangeData 必须是完整的48个非零值
			const rangeData: number[] = Array.isArray(data.rangeData) ? data.rangeData : [];
			if (rangeData.length !== CIRCULARITY_DATA_SIZE ||
				rangeData.some(v => typeof v !== 'number' || !(v > 0))) {
				throw new Error('Invalid calibration data received');
			}

			setIsCompleted(true);
			// 仅在后端返回成功且数据完整时写入校准数据
			onComplete(rangeData);
		} catch (error) {
			if (abortRef.current) return;
			console.error('Range calibration error:', error);
			const msg = error instanceof Error ? error.message : String(error);
			setErrorMsg(msg);
			alert(t('AddonsConfig:analog-calibration-failed', { error: msg }));
		} finally {
			if (!abortRef.current) {
				setIsCollecting(false);
			}
		}
	};

	const handleCancel = () => {
		abortRef.current = true;
		setIsCollecting(false);
		setIsCompleted(false);
		onHide();
	};

	const getContent = () => {
		if (isCompleted) {
			return (
				<div>
					<h4>{t('AddonsConfig:joystick-range-calibration-modal-done') || '采样完成'}</h4>
					<p>外圈校准数据已采集完毕，已写入配置。</p>
				</div>
			);
		}
		if (isCollecting) {
			return (
				<div className="text-center py-3">
					<Spinner animation="border" role="status" className="mb-3">
						<span className="visually-hidden">Loading...</span>
					</Spinner>
					<h4>{t('AddonsConfig:joystick-range-calibration-modal-collecting-title') || '正在采样'}</h4>
					<p>
						请缓慢转动{stickLabel}画圈，覆盖所有方向。
					</p>
					<p className="small text-muted">
						系统将在所有角度采样完成后自动结束。
					</p>
				</div>
			);
		}
		return (
			<div>
				<h4>{t('AddonsConfig:joystick-range-calibration-modal-welcome-title')}</h4>
				<p>{t('AddonsConfig:joystick-range-calibration-modal-welcome-text', { stick: stickLabel })}</p>
				<p>
					<em>{t('AddonsConfig:joystick-range-calibration-modal-warning')}</em>
				</p>
				<p>{t('AddonsConfig:joystick-range-calibration-modal-welcome-instruction')}</p>
				{errorMsg && <p className="text-danger small">{errorMsg}</p>}
			</div>
		);
	};

	return (
		<Modal
			show={show}
			onHide={handleCancel}
			backdrop={isCollecting ? 'static' : true}
			keyboard={!isCollecting}
			size="lg"
			centered
		>
			<Modal.Header closeButton={!isCollecting}>
				<Modal.Title>{t('AddonsConfig:joystick-range-calibration-modal-title', { stick: stickLabel })}</Modal.Title>
			</Modal.Header>
			<Modal.Body>
				{getContent()}
			</Modal.Body>
			<Modal.Footer>
				{!isCompleted ? (
					<>
						<Button variant="secondary" onClick={handleCancel} disabled={isCollecting}>
							{t('AddonsConfig:joystick-range-calibration-modal-cancel')}
						</Button>
						{!isCollecting && (
							<Button variant="primary" onClick={startCalibration}>
								{t('AddonsConfig:joystick-range-calibration-modal-start')}
							</Button>
						)}
					</>
				) : (
					<Button variant="primary" onClick={onHide}>
						{t('AddonsConfig:joystick-range-calibration-modal-done') || '完成'}
					</Button>
				)}
			</Modal.Footer>
		</Modal>
	);
};

export default RangeCalibrationModal;
