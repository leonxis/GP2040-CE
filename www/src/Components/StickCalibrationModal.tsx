import { useState, useEffect } from 'react';
import { Modal, Button, Spinner, ListGroup } from 'react-bootstrap';
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
	const [currentStep, setCurrentStep] = useState(1); // 1=welcome, 2-5=steps, 6=completed
	const [isLoading, setIsLoading] = useState(false);
	const [calibrationValues, setCalibrationValues] = useState<Array<{ x: number; y: number }>>([]);
	const [buttonText, setButtonText] = useState('');

	// Reset state when modal opens/closes
	useEffect(() => {
		if (show) {
			setCurrentStep(1);
			setCalibrationValues([]);
			setIsLoading(false);
			setButtonText(t('AddonsConfig:joystick-calibration-modal-start'));
		}
	}, [show, t]);

	// Update button text based on current step
	useEffect(() => {
		if (currentStep === 1) {
			setButtonText(t('AddonsConfig:joystick-calibration-modal-start'));
		} else if (currentStep === 6) {
			setButtonText(t('AddonsConfig:joystick-calibration-modal-done'));
		} else {
			setButtonText(t('AddonsConfig:joystick-calibration-modal-continue'));
		}
	}, [currentStep, t]);

	const handleNext = async () => {
		if (currentStep === 1) {
			// Start calibration
			setCurrentStep(2);
			setIsLoading(true);
			await new Promise(resolve => setTimeout(resolve, 100));
			setIsLoading(false);
		} else if (currentStep >= 2 && currentStep <= 5) {
			// Sample calibration data with multiple readings to average out jitter
			setIsLoading(true);
			setButtonText(t('AddonsConfig:joystick-calibration-modal-sampling'));
			
			try {
				await new Promise(resolve => setTimeout(resolve, 150));
				
				const apiEndpoint = stickNumber === 1 ? '/api/getJoystickCenter' : '/api/getJoystickCenter2';
				const SAMPLE_COUNT = 20; // Number of samples to take for averaging
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
					
					// Wait between samples (except for the last one)
					if (i < SAMPLE_COUNT - 1) {
						await new Promise(resolve => setTimeout(resolve, SAMPLE_INTERVAL));
					}
				}
				
				// Calculate average of all samples
				const avgX = Math.round(samples.reduce((sum, sample) => sum + sample.x, 0) / samples.length);
				const avgY = Math.round(samples.reduce((sum, sample) => sum + sample.y, 0) / samples.length);
				
				const newValue = { x: avgX, y: avgY };
				const newValues = [...calibrationValues, newValue];
				setCalibrationValues(newValues);
				
				if (currentStep === 5) {
					// Last step, calculate and complete
					setButtonText(t('AddonsConfig:joystick-calibration-modal-storing'));
					await new Promise(resolve => setTimeout(resolve, 500));
					
					// Calculate center value from four points
					const finalAvgX = Math.round(newValues.reduce((sum, val) => sum + val.x, 0) / newValues.length);
					const finalAvgY = Math.round(newValues.reduce((sum, val) => sum + val.y, 0) / newValues.length);
					
					setCurrentStep(6);
					onComplete(finalAvgX, finalAvgY);
				} else {
					setCurrentStep(currentStep + 1);
				}
			} catch (error) {
				console.error('Calibration error:', error);
				alert(t('AddonsConfig:analog-calibration-failed', { error: error instanceof Error ? error.message : String(error) }));
				onHide();
				return;
			} finally {
				setIsLoading(false);
			}
		} else if (currentStep === 6) {
			// Completed
			onHide();
		}
	};

	const getStepContent = () => {
		switch (currentStep) {
			case 1:
				return (
					<div>
						<h4>{t('AddonsConfig:joystick-calibration-modal-welcome-title')}</h4>
						<p>{t('AddonsConfig:joystick-calibration-modal-welcome-text', { stick: stickLabel })}</p>
						<p>
							<em>{t('AddonsConfig:joystick-calibration-modal-warning')}</em>
						</p>
						<p>{t('AddonsConfig:joystick-calibration-modal-welcome-instruction')}</p>
					</div>
				);
			case 2:
				return (
					<div>
						<p>
							{t('AddonsConfig:joystick-calibration-modal-step-instruction', {
								stick: stickLabel,
								direction: t('AddonsConfig:analog-calibration-direction-top-left'),
							})}
						</p>
						<p>{t('AddonsConfig:joystick-calibration-modal-step-confirm')}</p>
					</div>
				);
			case 3:
				return (
					<div>
						<p>
							{t('AddonsConfig:joystick-calibration-modal-step-instruction', {
								stick: stickLabel,
								direction: t('AddonsConfig:analog-calibration-direction-top-right'),
							})}
						</p>
						<p>{t('AddonsConfig:joystick-calibration-modal-step-confirm')}</p>
					</div>
				);
			case 4:
				return (
					<div>
						<p>
							{t('AddonsConfig:joystick-calibration-modal-step-instruction', {
								stick: stickLabel,
								direction: t('AddonsConfig:analog-calibration-direction-bottom-left'),
							})}
						</p>
						<p>{t('AddonsConfig:joystick-calibration-modal-step-confirm')}</p>
					</div>
				);
			case 5:
				return (
					<div>
						<p>
							{t('AddonsConfig:joystick-calibration-modal-step-instruction', {
								stick: stickLabel,
								direction: t('AddonsConfig:analog-calibration-direction-bottom-right'),
							})}
						</p>
						<p>{t('AddonsConfig:joystick-calibration-modal-step-confirm')}</p>
					</div>
				);
			case 6:
				return (
					<div>
						<h4>{t('AddonsConfig:joystick-calibration-modal-completed-title')}</h4>
						<p>{t('AddonsConfig:joystick-calibration-modal-completed-text')}</p>
						{calibrationValues.length === 4 && (
							<div className="mt-3">
								<p className="small text-muted">{t('AddonsConfig:analog-calibration-data')}</p>
								<ul className="small">
									<li>
										{t('AddonsConfig:analog-calibration-direction-top-left')}: X={calibrationValues[0].x}, Y={calibrationValues[0].y}
									</li>
									<li>
										{t('AddonsConfig:analog-calibration-direction-top-right')}: X={calibrationValues[1].x}, Y={calibrationValues[1].y}
									</li>
									<li>
										{t('AddonsConfig:analog-calibration-direction-bottom-left')}: X={calibrationValues[2].x}, Y={calibrationValues[2].y}
									</li>
									<li>
										{t('AddonsConfig:analog-calibration-direction-bottom-right')}: X={calibrationValues[3].x}, Y={calibrationValues[3].y}
									</li>
								</ul>
							</div>
						)}
					</div>
				);
			default:
				return null;
		}
	};

	return (
		<Modal
			show={show}
			onHide={currentStep === 1 || currentStep === 6 ? onHide : undefined}
			backdrop={currentStep === 1 || currentStep === 6 ? true : 'static'}
			keyboard={currentStep === 1 || currentStep === 6}
			size="lg"
			centered
		>
			<Modal.Header closeButton={currentStep === 1 || currentStep === 6}>
				<Modal.Title>{t('AddonsConfig:joystick-calibration-modal-title', { stick: stickLabel })}</Modal.Title>
			</Modal.Header>
			<Modal.Body>
				<div className="row">
					<div className="col-4">
						<ListGroup>
							<ListGroup.Item action active={currentStep === 1}>
								{t('AddonsConfig:joystick-calibration-modal-step-welcome')}
							</ListGroup.Item>
							<ListGroup.Item action active={currentStep === 2}>
								{t('AddonsConfig:joystick-calibration-modal-step', { step: 1 })}
							</ListGroup.Item>
							<ListGroup.Item action active={currentStep === 3}>
								{t('AddonsConfig:joystick-calibration-modal-step', { step: 2 })}
							</ListGroup.Item>
							<ListGroup.Item action active={currentStep === 4}>
								{t('AddonsConfig:joystick-calibration-modal-step', { step: 3 })}
							</ListGroup.Item>
							<ListGroup.Item action active={currentStep === 5}>
								{t('AddonsConfig:joystick-calibration-modal-step', { step: 4 })}
							</ListGroup.Item>
							<ListGroup.Item action active={currentStep === 6}>
								{t('AddonsConfig:joystick-calibration-modal-step-completed')}
							</ListGroup.Item>
						</ListGroup>
					</div>
					<div className="col-8">
						{getStepContent()}
					</div>
				</div>
			</Modal.Body>
			<Modal.Footer>
				<Button
					variant="primary"
					onClick={handleNext}
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

