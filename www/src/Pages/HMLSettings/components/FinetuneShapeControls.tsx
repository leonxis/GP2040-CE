import { FormCheck, Form } from 'react-bootstrap';
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
	return (
		<div style={finetuneControlsContainerStyle}>
			<div style={finetuneControlsBoxStyle}>
				<div style={finetuneControlsTitleStyle}>{title}</div>
				<div className="p-2">
					<div className="mb-3">
						<FormCheck
							type="switch"
							label="强制圆形"
							checked={forceCircular}
							onChange={(e) => onForceCircularChange(e.target.checked)}
						/>
						<p className="text-muted small mt-1 mb-0">
							{forceCircular
								? "强制圆形会将摇杆外圈移动半径严格归一到圆形。"
								: "关闭强制圆形时将产生反映摇杆真实形状的外圈与误差率。"}
						</p>
					</div>
					<div className="mb-2">
						<Form.Label className="mb-1">外圈放大系数: {amplify.toFixed(1)}%</Form.Label>
						<Form.Range
							min={-20}
							max={20}
							step={0.1}
							value={amplify}
							onChange={(e) => onAmplifyChange(parseFloat(e.target.value))}
						/>
						<p className="text-muted small mt-1 mb-0">
							扩大系数可以放大摇杆覆盖范围，加快移动响应速度。
						</p>
					</div>
				</div>
			</div>
		</div>
	);
};

export default FinetuneShapeControls;








