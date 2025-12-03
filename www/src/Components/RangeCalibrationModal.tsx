import { useState, useEffect, useRef } from 'react';
import { Modal, Button, Spinner, ProgressBar } from 'react-bootstrap';
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
const REQUIRED_FULL_CYCLES = 4; // Number of full rotations required
const JOYSTICK_EXTREME_THRESHOLD = 0.95; // Minimum distance to count as valid data (must be pushed to extreme)
const CIRCLE_FILL_THRESHOLD = 0.95; // Percentage of angles that must have data to complete a cycle

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
	const [progress, setProgress] = useState(0);
	const [buttonText, setButtonText] = useState('');
	const [countdown, setCountdown] = useState(0);
	const rangeDataRef = useRef<number[]>(new Array(CIRCULARITY_DATA_SIZE).fill(0)); // Accumulated max values across all cycles
	const cycleDataRef = useRef<number[]>(new Array(CIRCULARITY_DATA_SIZE).fill(0)); // Current cycle data
	const nonZeroCountRef = useRef(0);
	const fullCyclesRef = useRef(0);
	const countdownIntervalRef = useRef<NodeJS.Timeout | null>(null);
	const progressIntervalRef = useRef<NodeJS.Timeout | null>(null);

	// Reset state when modal opens/closes
	useEffect(() => {
		if (show) {
			setIsCollecting(false);
			setProgress(0);
			setCountdown(0);
			setButtonText(t('AddonsConfig:joystick-range-calibration-modal-start'));
			rangeDataRef.current = new Array(CIRCULARITY_DATA_SIZE).fill(0);
			cycleDataRef.current = new Array(CIRCULARITY_DATA_SIZE).fill(0);
			nonZeroCountRef.current = 0;
			fullCyclesRef.current = 0;
		} else {
			// Cleanup intervals
			if (countdownIntervalRef.current) {
				clearInterval(countdownIntervalRef.current);
				countdownIntervalRef.current = null;
			}
			if (progressIntervalRef.current) {
				clearInterval(progressIntervalRef.current);
				progressIntervalRef.current = null;
			}
		}
	}, [show, t]);

	const startCountdown = () => {
		setCountdown(15); // 15 seconds countdown to unlock Done button
		countdownIntervalRef.current = setInterval(() => {
			setCountdown(prev => {
				if (prev <= 1) {
					if (countdownIntervalRef.current) {
						clearInterval(countdownIntervalRef.current);
						countdownIntervalRef.current = null;
					}
					return 0;
				}
				return prev - 1;
			});
		}, 1000);
	};

	const checkDataProgress = async () => {
		try {
			const apiEndpoint = stickNumber === 1 ? '/api/getJoystickCenter' : '/api/getJoystickCenter2';
			const res = await fetch(apiEndpoint);
			
			if (!res.ok) {
				return;
			}
			
			const data = await res.json();
			
			if (!data.success) {
				return;
			}

			// Get center values from props or use default
			const ADC_MAX = 4095;
			const centerXValue = centerX !== undefined ? centerX : ADC_MAX / 2;
			const centerYValue = centerY !== undefined ? centerY : ADC_MAX / 2;
			
			// Convert raw ADC to normalized (-1 to 1)
			const stickX = ((data.x - centerXValue) / (ADC_MAX / 2));
			const stickY = ((data.y - centerYValue) / (ADC_MAX / 2));
			
			// Calculate distance and angle
			const distance = Math.sqrt(stickX * stickX + stickY * stickY);
			const angle = Math.atan2(stickY, stickX);
			
			// Only collect data if stick is pushed to extreme (near maximum range)
			if (distance > JOYSTICK_EXTREME_THRESHOLD) {
				// Calculate angle index (0 to CIRCULARITY_DATA_SIZE-1)
				const angleIndex = Math.round((angle + Math.PI) * CIRCULARITY_DATA_SIZE / (2 * Math.PI)) % CIRCULARITY_DATA_SIZE;
				
				// Update current cycle data (keep maximum distance for each angle in this cycle)
				const oldCycleValue = cycleDataRef.current[angleIndex] || 0;
				if (distance > oldCycleValue) {
					cycleDataRef.current[angleIndex] = distance;
					// Also update accumulated max values
					const oldMaxValue = rangeDataRef.current[angleIndex] || 0;
					if (distance > oldMaxValue) {
						rangeDataRef.current[angleIndex] = distance;
					}
				}
			}
			
			// Check progress: count how many angles have data above threshold in current cycle
			const currentNonZeroCount = cycleDataRef.current.filter(v => v > JOYSTICK_EXTREME_THRESHOLD).length;
			const fillRatio = currentNonZeroCount / CIRCULARITY_DATA_SIZE;
			
			// If we've filled enough angles, complete a cycle
			if (fillRatio >= CIRCLE_FILL_THRESHOLD) {
				// Only increment if we haven't already counted this cycle
				if (currentNonZeroCount > nonZeroCountRef.current || nonZeroCountRef.current === 0) {
					fullCyclesRef.current++;
					// Reset cycle data for next cycle (but keep accumulated max values)
					cycleDataRef.current.fill(0);
					nonZeroCountRef.current = 0;
				}
			} else {
				nonZeroCountRef.current = currentNonZeroCount;
			}
			
			// Update progress based on completed cycles
			const cycleProgress = (fullCyclesRef.current / REQUIRED_FULL_CYCLES) * 100;
			const currentCycleProgress = (currentNonZeroCount / CIRCULARITY_DATA_SIZE) * (100 / REQUIRED_FULL_CYCLES);
			setProgress(Math.min(100, cycleProgress + currentCycleProgress));
		} catch (error) {
			console.error('Failed to fetch joystick data:', error);
		}
	};

	const startCalibration = () => {
		setIsCollecting(true);
		setButtonText(t('AddonsConfig:joystick-range-calibration-modal-collecting'));
		startCountdown();
		
		// Start collecting data
		progressIntervalRef.current = setInterval(checkDataProgress, 100); // Check every 100ms
	};

	const handleComplete = () => {
		// Stop intervals
		if (countdownIntervalRef.current) {
			clearInterval(countdownIntervalRef.current);
			countdownIntervalRef.current = null;
		}
		if (progressIntervalRef.current) {
			clearInterval(progressIntervalRef.current);
			progressIntervalRef.current = null;
		}
		
		// Check if we have at least one complete cycle (minimum requirement)
		const currentNonZeroCount = cycleDataRef.current.filter(v => v > JOYSTICK_EXTREME_THRESHOLD).length;
		const hasMinimumData = fullCyclesRef.current >= 1 || 
			(currentNonZeroCount / CIRCULARITY_DATA_SIZE) >= CIRCLE_FILL_THRESHOLD;
		
		if (!hasMinimumData) {
			// If we don't have at least one complete cycle, treat as cancel
			onHide();
			return;
		}
		
		// Get the final range data (use accumulated max values from all cycles)
		const finalData = [...rangeDataRef.current];
		
		// Complete the calibration with available data
		onComplete(finalData);
		onHide();
	};

	const handleCancel = () => {
		// Stop intervals
		if (countdownIntervalRef.current) {
			clearInterval(countdownIntervalRef.current);
			countdownIntervalRef.current = null;
		}
		if (progressIntervalRef.current) {
			clearInterval(progressIntervalRef.current);
			progressIntervalRef.current = null;
		}
		onHide();
	};

	// Button is enabled after countdown reaches 0 (15 seconds)
	const canComplete = countdown === 0;

	useEffect(() => {
		if (isCollecting) {
			if (countdown === 0) {
				setButtonText(t('AddonsConfig:joystick-range-calibration-modal-done'));
			} else {
				setButtonText(t('AddonsConfig:joystick-range-calibration-modal-collecting'));
			}
		}
	}, [countdown, isCollecting, t]);

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
				{!isCollecting ? (
					<div>
						<h4>{t('AddonsConfig:joystick-range-calibration-modal-welcome-title')}</h4>
						<p>{t('AddonsConfig:joystick-range-calibration-modal-welcome-text', { stick: stickLabel })}</p>
						<p>
							<em>{t('AddonsConfig:joystick-range-calibration-modal-warning')}</em>
						</p>
						<p>{t('AddonsConfig:joystick-range-calibration-modal-welcome-instruction')}</p>
					</div>
				) : (
					<div>
						<h4>{t('AddonsConfig:joystick-range-calibration-modal-collecting-title')}</h4>
						<p>{t('AddonsConfig:joystick-range-calibration-modal-collecting-text', { 
							stick: stickLabel,
							cycles: fullCyclesRef.current,
							required: REQUIRED_FULL_CYCLES
						})}</p>
						
						{countdown > 0 && (
							<div className="alert alert-info">
								{t('AddonsConfig:joystick-range-calibration-modal-countdown', { seconds: countdown })}
							</div>
						)}
						
						{countdown === 0 && fullCyclesRef.current < REQUIRED_FULL_CYCLES && (
							<div className="alert alert-info mt-2">
								{t('AddonsConfig:joystick-range-calibration-modal-can-complete', {
									cycles: fullCyclesRef.current,
									required: REQUIRED_FULL_CYCLES
								})}
							</div>
						)}
						
						<ProgressBar 
							now={progress} 
							label={`${Math.round(progress)}%`}
							className="mt-3"
						/>
						
						<p className="mt-3 small text-muted">
							{t('AddonsConfig:joystick-range-calibration-modal-progress-hint')}
						</p>
					</div>
				)}
			</Modal.Body>
			<Modal.Footer>
				{!isCollecting ? (
					<>
						<Button variant="secondary" onClick={handleCancel}>
							{t('AddonsConfig:joystick-range-calibration-modal-cancel')}
						</Button>
						<Button variant="primary" onClick={startCalibration}>
							{buttonText}
						</Button>
					</>
				) : (
					<>
						<Button variant="secondary" onClick={handleCancel}>
							{t('AddonsConfig:joystick-range-calibration-modal-cancel')}
						</Button>
						<Button 
							variant="primary" 
							onClick={handleComplete}
							disabled={!canComplete}
						>
							{canComplete ? (
								buttonText
							) : (
								<>
									<Spinner
										as="span"
										animation="border"
										size="sm"
										role="status"
										aria-hidden="true"
										className="me-2"
									/>
									{t('AddonsConfig:joystick-range-calibration-modal-waiting', { seconds: countdown })}
								</>
							)}
						</Button>
					</>
				)}
			</Modal.Footer>
		</Modal>
	);
};

export default RangeCalibrationModal;

