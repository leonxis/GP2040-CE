import { Button, FormCheck } from 'react-bootstrap';

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
	return (
		<>
			<div className="mt-3">
				<FormCheck
					type="switch"
					label="误差率"
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
					查看校准数据
				</Button>
			</div>
		</>
	);
};

export default ViewCalibrationData;







