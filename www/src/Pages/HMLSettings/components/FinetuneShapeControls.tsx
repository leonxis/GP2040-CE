import { FormCheck, Form } from 'react-bootstrap';
import { useTranslation } from 'react-i18next';
import { finetuneControlsContainerStyle, finetuneControlsBoxStyle, finetuneControlsTitleStyle } from './JoystickCalibration';

interface FinetuneShapeControlsProps {
	title: string;
	forceCircular: boolean;
	amplify: number;
	onForceCircularChange: (value: boolean) => void;
	onAmplifyChange: (value: number) => void;
}

const FinetuneShapeControls: React.FC<FinetuneShapeControlsProps> = ({
	title,
	forceCircular,
	amplify,
	onForceCircularChange,
	onAmplifyChange,
}) => {
	const { t } = useTranslation();
	return (
		<div style={finetuneControlsContainerStyle}>
			<div style={finetuneControlsBoxStyle}>
				<div style={finetuneControlsTitleStyle}>{title}</div>
				<div className="p-2">
					<div className="mb-3">
						<FormCheck
							type="switch"
							label={t('CalibrationSettings:hml-force-circular-label')}
							checked={forceCircular}
							onChange={(e) => onForceCircularChange(e.target.checked)}
						/>
						<p className="text-muted small mt-1 mb-0">
							{forceCircular
								? t('CalibrationSettings:hml-force-circular-hint-on')
								: t('CalibrationSettings:hml-force-circular-hint-off')}
						</p>
					</div>
					<div className="mb-2">
						<Form.Label className="mb-1">{t('CalibrationSettings:hml-outer-amplify-label', { value: amplify.toFixed(1) })}</Form.Label>
						<Form.Range
							min={-20}
							max={20}
							step={0.1}
							value={amplify}
							onChange={(e) => onAmplifyChange(parseFloat(e.target.value))}
							onKeyDown={(e) => {
								const step = 0.1;
								if (e.key === 'ArrowRight' || e.key === 'ArrowUp') {
									e.preventDefault();
									onAmplifyChange(Math.min(20, Math.round((amplify + step) * 10) / 10));
								} else if (e.key === 'ArrowLeft' || e.key === 'ArrowDown') {
									e.preventDefault();
									onAmplifyChange(Math.max(-20, Math.round((amplify - step) * 10) / 10));
								}
							}}
						/>
						<p className="text-muted small mt-1 mb-0">
							{t('CalibrationSettings:hml-outer-amplify-hint')}
						</p>
					</div>
				</div>
			</div>
		</div>
	);
};

export default FinetuneShapeControls;








