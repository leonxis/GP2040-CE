import { useState, useEffect } from 'react';
import { Modal, Button, Spinner } from 'react-bootstrap';
import { useTranslation } from 'react-i18next';

interface StickCalibrationModalProps {
	show: boolean;
	onHide: () => void;
	onComplete: (centerX: number, centerY: number) => void;
	stickNumber: 1 | 2;
	stickLabel: string;
}

const StickCalibrationModal = ({
	show,
	onHide,
	onComplete,
	stickNumber,
	stickLabel,
}: StickCalibrationModalProps) => {
	const { t } = useTranslation();
	const [isLoading, setIsLoading] = useState(false);
	const [isCompleted, setIsCompleted] = useState(false);
	const [calibrationResult, setCalibrationResult] = useState<{ x: number; y: number } | null>(null);
	const [buttonText, setButtonText] = useState('');
	const [errorMsg, setErrorMsg] = useState('');

	// Reset state when modal opens/closes
	useEffect(() => {
		if (show) {
			setIsCompleted(false);
			setCalibrationResult(null);
			setIsLoading(false);
			setErrorMsg('');
			setButtonText(t('AddonsConfig:joystick-calibration-modal-start'));
		}
	}, [show, t]);

	const handleStart = async () => {
		if (isCompleted) {
			// Completed, close modal
			onHide();
			return;
		}

		setIsLoading(true);
		setErrorMsg('');
		setButtonText(t('AddonsConfig:joystick-calibration-modal-sampling'));

		try {
			// 后端单次阻塞采样求平均，前端仅等待结果不做实际采样
			const res = await fetch('/api/calibrateJoystickCenter', {
				method: 'POST',
				headers: { 'Content-Type': 'application/json' },
				body: JSON.stringify({ stick: stickNumber }),
			});

			if (!res.ok) {
				throw new Error(`HTTP error! status: ${res.status}`);
			}

			const data = await res.json();
			if (!data.success) {
				throw new Error(data.error || 'Unknown error');
			}

			const avgX = Number(data.x) || 0;
			const avgY = Number(data.y) || 0;

			setCalibrationResult({ x: avgX, y: avgY });
			setIsCompleted(true);
			setButtonText(t('AddonsConfig:joystick-calibration-modal-done'));
			// 仅在后端返回成功时写入校准数据
			onComplete(avgX, avgY);
		} catch (error) {
			console.error('Calibration error:', error);
			setErrorMsg(error instanceof Error ? error.message : String(error));
			alert(t('AddonsConfig:analog-calibration-failed', { error: error instanceof Error ? error.message : String(error) }));
			onHide();
		} finally {
			setIsLoading(false);
		}
	};

	const getContent = () => {
		if (isCompleted) {
			return (
				<div>
					<h4>{t('AddonsConfig:joystick-calibration-modal-completed-title')}</h4>
					<p>{t('AddonsConfig:joystick-calibration-modal-completed-text')}</p>
					{calibrationResult && (
						<div className="mt-3">
							<p className="small text-muted">{t('AddonsConfig:analog-calibration-data')}</p>
							<p className="small">
								中心值: X={calibrationResult.x}, Y={calibrationResult.y}
							</p>
						</div>
					)}
				</div>
			);
		} else if (isLoading) {
			return (
				<div>
					<h4>{t('AddonsConfig:joystick-calibration-modal-welcome-title')}</h4>
					<p>正在采样中...</p>
					<p>
						<em>请保持摇杆在中心位置不动</em>
					</p>
				</div>
			);
		} else {
			return (
				<div>
					<h4>{t('AddonsConfig:joystick-calibration-modal-welcome-title')}</h4>
					<p>此工具将引导您重新校准{stickLabel}的中心位置，请拨动一次摇杆使其正确回中。</p>
					<p>
						<em>{t('AddonsConfig:joystick-calibration-modal-warning')}</em>
					</p>
					<p>请将摇杆保持在中心位置，然后点击开始按钮。系统将自动采样并计算平均值。</p>
					{errorMsg && <p className="text-danger small">{errorMsg}</p>}
				</div>
			);
		}
	};

	return (
		<Modal
			show={show}
			onHide={!isLoading ? onHide : undefined}
			backdrop={!isLoading ? true : 'static'}
			keyboard={!isLoading}
			size="lg"
			centered
		>
			<Modal.Header closeButton={!isLoading}>
				<Modal.Title>{t('AddonsConfig:joystick-calibration-modal-title', { stick: stickLabel })}</Modal.Title>
			</Modal.Header>
			<Modal.Body>
				{getContent()}
			</Modal.Body>
			<Modal.Footer>
				<Button
					variant="primary"
					onClick={handleStart}
					disabled={isLoading}
				>
					{isLoading && (
						<Spinner
							as="span"
							animation="border"
							size="sm"
							role="status"
							aria-hidden="true"
							className="me-2"
						/>
					)}
					{buttonText}
				</Button>
			</Modal.Footer>
		</Modal>
	);
};

export default StickCalibrationModal;
