import { Button } from 'react-bootstrap';
import {
	finetuneButtonStyle,
	positionInfoInnerStyle,
	positionInfoRowStyle,
	positionValueStyle,
	positionNormalizedStyle
} from './JoystickCalibration';

interface StickPositionInfoProps {
	stickData: { x: number; y: number; rawX: number; rawY: number };
	finetuneCenterActive: boolean;
	centerX: number;
	centerY: number;
	onCenterXChange: (value: number) => void;
	onCenterYChange: (value: number) => void;
	convertToDS4Normalized: (value: number) => string;
}

const StickPositionInfo: React.FC<StickPositionInfoProps> = ({
	stickData,
	finetuneCenterActive,
	centerX,
	centerY,
	onCenterXChange,
	onCenterYChange,
	convertToDS4Normalized,
}) => {
	return (
		<div className="small" style={positionInfoInnerStyle}>
			<div style={positionInfoRowStyle}>
				<span>X:</span>
				{finetuneCenterActive && (
					<Button
						variant="light"
						size="sm"
						style={finetuneButtonStyle}
						onClick={() => onCenterXChange(centerX + 2)}
					>
						+
					</Button>
				)}
				<span style={positionValueStyle}>
					{stickData.rawX}
				</span>
				{finetuneCenterActive && (
					<Button
						variant="light"
						size="sm"
						style={finetuneButtonStyle}
						onClick={() => onCenterXChange(centerX - 2)}
					>
						−
					</Button>
				)}
				<span style={positionNormalizedStyle}>
					({convertToDS4Normalized(stickData.x)})
				</span>
			</div>
			<div style={positionInfoRowStyle}>
				<span>Y:</span>
				{finetuneCenterActive && (
					<Button
						variant="light"
						size="sm"
						style={finetuneButtonStyle}
						onClick={() => onCenterYChange(centerY + 2)}
					>
						+
					</Button>
				)}
				<span style={positionValueStyle}>
					{stickData.rawY}
				</span>
				{finetuneCenterActive && (
					<Button
						variant="light"
						size="sm"
						style={finetuneButtonStyle}
						onClick={() => onCenterYChange(centerY - 2)}
					>
						−
					</Button>
				)}
				<span style={positionNormalizedStyle}>
					({convertToDS4Normalized(stickData.y)})
				</span>
			</div>
		</div>
	);
};

export default StickPositionInfo;

