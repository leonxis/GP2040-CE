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
	const [sampleCount, setSampleCount] = useState(0);
	const [buttonText, setButtonText] = useState('');

	// Reset state when modal opens/closes
	useEffect(() => {
		if (show) {
			setIsCompleted(false);
			setCalibrationResult(null);
			setIsLoading(false);
			setSampleCount(0);
			setButtonText(t('AddonsConfig:joystick-calibration-modal-start'));
		}
	}, [show, t]);

	const handleStart = async () => {
		if (isCompleted) {
			// Completed, close modal
			onHide();
			return;
		}

		// Start calibration - sample 10 times and calculate average
		setIsLoading(true);
		setButtonText(t('AddonsConfig:joystick-calibration-modal-sampling'));
		
		try {
			const apiEndpoint = stickNumber === 1 ? '/api/getJoystickCenter' : '/api/getJoystickCenter2';
			const SAMPLE_COUNT = 10; // Number of samples to take for averaging
			const SAMPLE_INTERVAL = 50; // Milliseconds between samples
			
			// Collect multiple samples
			const samples: Array<{ x: number; y: number }> = [];
			for (let i = 0; i < SAMPLE_COUNT; i++) {
				const res = await fetch(apiEndpoint);
				
				if (!res.ok) {
					throw new Error(`HTTP error! status: ${res.status}`);
				}
				
				const data = await res.json();
				
				if (!data.success || data.error) {
					throw new Error(data.error || 'Unknown error');
				}
				
				samples.push({ x: data.x || 0, y: data.y || 0 });
				setSampleCount(i + 1);
				
				// Wait between samples (except for the last one)
				if (i < SAMPLE_COUNT - 1) {
					await new Promise(resolve => setTimeout(resolve, SAMPLE_INTERVAL));
				}
			}
			
			// Calculate average of all samples
			const avgX = Math.round(samples.reduce((sum, sample) => sum + sample.x, 0) / samples.length);
			const avgY = Math.round(samples.reduce((sum, sample) => sum + sample.y, 0) / samples.length);
			
			setCalibrationResult({ x: avgX, y: avgY });
			setButtonText(t('AddonsConfig:joystick-calibration-modal-storing'));
			await new Promise(resolve => setTimeout(resolve, 300));
			
			// Complete calibration
			setIsCompleted(true);
			setButtonText(t('AddonsConfig:joystick-calibration-modal-done'));
			onComplete(avgX, avgY);
		} catch (error) {
			console.error('Calibration error:', error);
			alert(t('AddonsConfig:analog-calibration-failed', { error: error instanceof Error ? error.message : String(error) }));
			onHide();
			return;
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
					<p>正在采样中... ({sampleCount}/10)</p>
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
					<p>请将摇杆保持在中心位置，然后点击开始按钮。系统将自动采样10次并计算平均值。</p>
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

