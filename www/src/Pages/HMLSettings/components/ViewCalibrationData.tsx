import { Button, FormCheck } from 'react-bootstrap';
import { useTranslation } from 'react-i18next';

interface ViewCalibrationDataProps {
	errorRateEnabled: boolean;
	onErrorRateChange: (value: boolean) => void;
	onViewData: () => void;
	circularityDataSize: number;
	onClearCircularityData: () => void;
}

const ViewCalibrationData: React.FC<ViewCalibrationDataProps> = ({
	errorRateEnabled,
	onErrorRateChange,
	onViewData,
	onClearCircularityData,
}) => {
	const { t } = useTranslation();
	return (
		<>
			<div className="mt-3">
				<FormCheck
					type="switch"
					label={t('CalibrationSettings:hml-error-rate-label')}
					checked={errorRateEnabled}
					onChange={(e) => {
						const newValue = e.target.checked;
						onErrorRateChange(newValue);
						if (!newValue) {
							onClearCircularityData();
						}
					}}
				/>
			</div>
			<div className="d-flex gap-2 justify-content-center flex-wrap">
				<Button
					variant="info"
					size="sm"
					onClick={onViewData}
				>
					{t('CalibrationSettings:hml-view-calibration-data-button')}
				</Button>
			</div>
		</>
	);
};

export default ViewCalibrationData;








