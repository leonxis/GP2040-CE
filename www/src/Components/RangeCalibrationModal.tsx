import { useState, useEffect, useRef } from 'react';
import { Modal, Button, ProgressBar } from 'react-bootstrap';
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
const JOYSTICK_EXTREME_THRESHOLD = 0.50; // Minimum scale to count as valid data (must be pushed to extreme)
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
	const [dataProgress, setDataProgress] = useState(0); // Track number of indices with data
	const rangeDataRef = useRef<number[]>(new Array(CIRCULARITY_DATA_SIZE).fill(0)); // Accumulated max values across all cycles
	const cycleDataRef = useRef<number[]>(new Array(CIRCULARITY_DATA_SIZE).fill(0)); // Current cycle data
	const nonZeroCountRef = useRef(0);
	const fullCyclesRef = useRef(0);
	const progressIntervalRef = useRef<NodeJS.Timeout | null>(null);

	// Reset state when modal opens/closes
	useEffect(() => {
		if (show) {
			setIsCollecting(false);
			setProgress(0);
			setDataProgress(0);
			setButtonText(t('AddonsConfig:joystick-range-calibration-modal-start'));
			rangeDataRef.current = new Array(CIRCULARITY_DATA_SIZE).fill(0);
			cycleDataRef.current = new Array(CIRCULARITY_DATA_SIZE).fill(0);
			nonZeroCountRef.current = 0;
			fullCyclesRef.current = 0;
		} else {
			// Cleanup intervals
			if (progressIntervalRef.current) {
				clearInterval(progressIntervalRef.current);
				progressIntervalRef.current = null;
			}
		}
	}, [show, t]);


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
			const ADC_CENTER = ADC_MAX / 2;  // 2047.5
			const centerXValue = centerX !== undefined ? centerX : ADC_CENTER;
			const centerYValue = centerY !== undefined ? centerY : ADC_CENTER;
			
			// Step 2: Coordinate translation (offset transformation)
			// Calculate center offset values
			const dX_value = centerXValue - ADC_CENTER;
			const dY_value = centerYValue - ADC_CENTER;
			
			// Apply offset transformation
			const offset_x = data.x - dX_value;  // adc_offset coordinate system
			const offset_y = data.y - dY_value;  // adc_offset coordinate system
			
			// Step 3: Move to adc_offset_center coordinate system
			const offset_center_x = offset_x - ADC_CENTER;
			const offset_center_y = offset_y - ADC_CENTER;
			
			// Calculate distance and angle in adc_offset_center coordinate system
			const distance = Math.sqrt(offset_center_x * offset_center_x + offset_center_y * offset_center_y);
			const angle = Math.atan2(offset_center_y, offset_center_x);
			
			// Calculate scale: distance / standard_outer_radius (ADC_CENTER = 2047.5)
			const scale = distance / ADC_CENTER;
			
			// Only collect data if stick is pushed to extreme (near maximum range)
			// Check if scale is above threshold (scale = distance / ADC_CENTER)
			if (scale > JOYSTICK_EXTREME_THRESHOLD) {
				// Calculate angle index (0 to CIRCULARITY_DATA_SIZE-1)
				const angleIndex = Math.round((angle + Math.PI) * CIRCULARITY_DATA_SIZE / (2 * Math.PI)) % CIRCULARITY_DATA_SIZE;
				
				// Update current cycle data (keep maximum scale for each angle in this cycle)
				const oldCycleValue = cycleDataRef.current[angleIndex] || 0;
				if (scale > oldCycleValue) {
					cycleDataRef.current[angleIndex] = scale;
					// Also update accumulated max values
					const oldMaxValue = rangeDataRef.current[angleIndex] || 0;
					if (scale > oldMaxValue) {
						rangeDataRef.current[angleIndex] = scale;
					}
				}
			}
			
			// Check progress: count how many angles have scale above threshold in current cycle
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
			
			// Update data progress (number of indices with non-zero data)
			const collectedIndices = rangeDataRef.current.filter(v => v > 0.0).length;
			setDataProgress(collectedIndices);
		} catch (error) {
			console.error('Failed to fetch joystick data:', error);
		}
	};

	const startCalibration = () => {
		setIsCollecting(true);
		setButtonText(t('AddonsConfig:joystick-range-calibration-modal-collecting') || '采样中');
		
		// Start collecting data
		progressIntervalRef.current = setInterval(checkDataProgress, 100); // Check every 100ms
	};

	const handleComplete = () => {
		// Get the final range data (use accumulated max values from all cycles)
		const finalData = [...rangeDataRef.current];
		
		// Check if all 48 indices have non-zero data
		const nonZeroIndices = finalData.filter(v => v > 0.0).length;
		
		// This should not happen if button is only enabled when all data is collected
		// But keep as safety check
		if (nonZeroIndices < CIRCULARITY_DATA_SIZE) {
			return;
		}
		
		// All indices have data, stop intervals and complete calibration
		if (progressIntervalRef.current) {
			clearInterval(progressIntervalRef.current);
			progressIntervalRef.current = null;
		}
		
		// Complete calibration
		onComplete(finalData);
		onHide();
	};

	const handleCancel = () => {
		// Stop intervals and discard current sampling data
		if (progressIntervalRef.current) {
			clearInterval(progressIntervalRef.current);
			progressIntervalRef.current = null;
		}
		// Close modal and discard sampling data (don't call onComplete)
		onHide();
	};

	// Check if all 48 indices have non-zero data to enable complete button
	const checkAllDataCollected = () => {
		const nonZeroIndices = rangeDataRef.current.filter(v => v > 0.0).length;
		return nonZeroIndices >= CIRCULARITY_DATA_SIZE;
	};

	// Update button text based on data collection status
	useEffect(() => {
		if (isCollecting) {
			if (checkAllDataCollected()) {
				setButtonText(t('AddonsConfig:joystick-range-calibration-modal-done') || '完成');
			} else {
				setButtonText(t('AddonsConfig:joystick-range-calibration-modal-collecting') || '采样中');
			}
		}
	}, [isCollecting, t]);

	// Update button text when data changes
	useEffect(() => {
		if (isCollecting && progressIntervalRef.current) {
			// Check periodically if all data is collected
			const checkInterval = setInterval(() => {
				if (checkAllDataCollected()) {
					setButtonText(t('AddonsConfig:joystick-range-calibration-modal-done') || '完成');
				} else {
					setButtonText(t('AddonsConfig:joystick-range-calibration-modal-collecting') || '采样中');
				}
			}, 500); // Check every 500ms
			
			return () => clearInterval(checkInterval);
		}
	}, [isCollecting, t]);

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
						<p>
							{t('AddonsConfig:joystick-range-calibration-modal-collecting-text', { 
								stick: stickLabel,
								cycles: fullCyclesRef.current,
								required: REQUIRED_FULL_CYCLES
							})}
							{' '}
							{t('AddonsConfig:joystick-range-calibration-data-progress') || '已采样数据'}
							{' '}
							{dataProgress}/{CIRCULARITY_DATA_SIZE}
						</p>
						
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
							disabled={!checkAllDataCollected()}
						>
							{buttonText}
						</Button>
					</>
				)}
			</Modal.Footer>
		</Modal>
	);
};

export default RangeCalibrationModal;


