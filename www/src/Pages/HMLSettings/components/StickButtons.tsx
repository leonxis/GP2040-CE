import { Button } from 'react-bootstrap';
import { useTranslation } from 'react-i18next';

interface StickButtonsProps {
	onCenterCalibration: () => void;
	onRangeCalibration: () => void;
	onFinetuneCenter: () => void;
	finetuneCenterActive: boolean;
	onJitterSampling: () => void;
}

const StickButtons: React.FC<StickButtonsProps> = ({
	onCenterCalibration,
	onRangeCalibration,
	onFinetuneCenter,
	finetuneCenterActive,
	onJitterSampling,
}) => {
	const { t } = useTranslation();

	return (
		<>
			<div className="d-flex gap-2 justify-content-center flex-wrap">
				<Button
					variant="primary"
					size="sm"
					onClick={onCenterCalibration}
				>
					{t('AddonsConfig:joystick-calibration-center-button')}
				</Button>
				<Button
					variant="primary"
					size="sm"
					onClick={onRangeCalibration}
				>
					{t('AddonsConfig:joystick-calibration-range-button')}
				</Button>
			</div>
			<div
				className="d-flex gap-2 justify-content-center flex-wrap"
				data-finetune-active={finetuneCenterActive ? '1' : '0'}
			>
				<Button
					variant="warning"
					size="sm"
					onClick={onFinetuneCenter}
				>
					{t('AddonsConfig:joystick-calibration-finetune-center-button')}
				</Button>
				<Button
					variant="warning"
					size="sm"
					onClick={onJitterSampling}
				>
					{t('CalibrationSettings:hml-stick-step-settings')}
				</Button>
			</div>
		</>
	);
};

export default StickButtons;
