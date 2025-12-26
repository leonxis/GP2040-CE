import { useEffect, useRef, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { Button, Form } from 'react-bootstrap';
import { FormikErrors, FormikHelpers, useFormikContext } from 'formik';

import Section from '../../../Components/Section';
import type { AddonPropTypes } from './CalibrationSettings';

// Type definitions
type CurvePoint = { x: number; y: number };
type CurvePointInput = { x: string; y: string };

/**
 * Draws curve editor on canvas
 * @param ctx Canvas context
 * @param width Canvas width
 * @param height Canvas height
 * @param points Control points (excluding start (0,0) and end (1,1))
 * @param progressRatio Progress ratio (0-1) for orange highlight line, or undefined to hide
 * @param innerDeadzone Inner deadzone value (0-1), X-axis intercept
 * @param antiDeadzone Anti-deadzone value (0-1), Y-axis intercept
 */
const drawCurveEditor = (
	ctx: CanvasRenderingContext2D,
	width: number,
	height: number,
	points: CurvePoint[],
	progressRatio?: number,
	innerDeadzone: number = 0,
	antiDeadzone: number = 0
) => {
	// Clear canvas with transparent background
	ctx.clearRect(0, 0, width, height);
	
	// Draw grid
	ctx.strokeStyle = '#b0b0b0'; // Darker gray for better visibility
	ctx.lineWidth = 1;
	const gridSize = 10;
	for (let i = 0; i <= gridSize; i++) {
		const pos = (i / gridSize) * width;
		// Vertical lines
		ctx.beginPath();
		ctx.moveTo(pos, 0);
		ctx.lineTo(pos, height);
		ctx.stroke();
		// Horizontal lines
		ctx.beginPath();
		ctx.moveTo(0, pos);
		ctx.lineTo(width, pos);
		ctx.stroke();
	}
	
	// Draw axes
	ctx.strokeStyle = '#000000';
	ctx.lineWidth = 2;
	// X axis
	ctx.beginPath();
	ctx.moveTo(0, height);
	ctx.lineTo(width, height);
	ctx.stroke();
	// Y axis
	ctx.beginPath();
	ctx.moveTo(0, height);
	ctx.lineTo(0, 0);
	ctx.stroke();
	
	// Draw axis labels
	ctx.fillStyle = '#000000';
	ctx.font = '12px Arial';
	ctx.textAlign = 'left';
	ctx.textBaseline = 'top';
	ctx.fillText('0', 2, height - 14);
	ctx.textAlign = 'right';
	ctx.fillText('1', width - 2, height - 14);
	ctx.textAlign = 'left';
	ctx.textBaseline = 'bottom';
	ctx.fillText('1', 2, 2);
	ctx.textBaseline = 'top';
	ctx.fillText('0', 2, height - 2);
	
	// Apply deadzone and anti-deadzone to points
	const applyDeadzones = (x: number, y: number): CurvePoint => {
		if (x < innerDeadzone) {
			return { x: -1, y: -1 }; // Invalid point, will be filtered
		} else {
			const remappedY = antiDeadzone + y * (1 - antiDeadzone);
			return { x, y: remappedY };
		}
	};
	
	// Build full point list with deadzone/anti-deadzone applied
	const startPoint = { x: innerDeadzone, y: 0 };
	const verticalEndPoint = { x: innerDeadzone, y: antiDeadzone };
	const adjustedPoints = points
		.map(p => applyDeadzones(p.x, p.y))
		.filter(p => p.x >= 0 && p.y >= 0)
		.filter(p => p.x >= innerDeadzone)
		.sort((a, b) => a.x - b.x);
	const endPoint = applyDeadzones(1, 1);
	
	// Build full points: start -> vertical end -> adjusted curve points -> end
	const fullPoints: CurvePoint[] = [];
	if (innerDeadzone > 0) {
		fullPoints.push(startPoint, verticalEndPoint);
	} else if (antiDeadzone > 0) {
		fullPoints.push(verticalEndPoint);
	} else {
		fullPoints.push(startPoint);
	}
	fullPoints.push(...adjustedPoints, endPoint);
	
	// Step 2: Draw deadzone and anti-deadzone reference lines (dashed)
	ctx.strokeStyle = '#999999';
	ctx.lineWidth = 1;
	ctx.setLineDash([5, 5]);
	
	if (innerDeadzone > 0) {
		ctx.beginPath();
		const deadzoneX = innerDeadzone * width;
		ctx.moveTo(deadzoneX, 0);
		ctx.lineTo(deadzoneX, height);
		ctx.stroke();
	}
	
	if (antiDeadzone > 0) {
		ctx.beginPath();
		const antiDeadzoneY = height - antiDeadzone * height;
		ctx.moveTo(0, antiDeadzoneY);
		ctx.lineTo(width, antiDeadzoneY);
		ctx.stroke();
	}
	
	if (innerDeadzone > 0 || antiDeadzone > 0) {
		ctx.beginPath();
		const refStartX = innerDeadzone * width;
		const refStartY = height - antiDeadzone * height;
		ctx.moveTo(refStartX, refStartY);
		ctx.lineTo(width, 0);
		ctx.stroke();
	}
	
	ctx.setLineDash([]);
	
	// Step 3: Draw purple mask area
	if (innerDeadzone > 0 || antiDeadzone > 0) {
		ctx.fillStyle = 'rgba(128, 0, 128, 0.2)'; // Purple with 35% opacity
		ctx.beginPath();
		const deadzoneX = innerDeadzone * width;
		const antiDeadzoneY = height - antiDeadzone * height;
		ctx.moveTo(deadzoneX, antiDeadzoneY);
		ctx.lineTo(width, antiDeadzoneY);
		ctx.lineTo(width, 0);
		ctx.lineTo(deadzoneX, 0);
		ctx.closePath();
		ctx.fill();
	}
	
	// Step 4: Draw curve
	const grayOriginPoint = { x: innerDeadzone, y: antiDeadzone };
	const curveStartPoint = (innerDeadzone > 0 || antiDeadzone > 0) ? grayOriginPoint : { x: 0, y: 0 };
	const curvePoints: CurvePoint[] = [curveStartPoint, ...adjustedPoints, endPoint];
	
	let totalCurveLength = 0;
	const segmentLengths: number[] = [];
	for (let i = 0; i < curvePoints.length - 1; i++) {
		const dx = (curvePoints[i + 1].x - curvePoints[i].x) * width;
		const dy = (curvePoints[i + 1].y - curvePoints[i].y) * height;
		const segmentLength = Math.sqrt(dx * dx + dy * dy);
		segmentLengths.push(segmentLength);
		totalCurveLength += segmentLength;
	}
	
	ctx.strokeStyle = '#404040';
	ctx.lineWidth = 2;
	ctx.beginPath();
	for (let i = 0; i < curvePoints.length; i++) {
		const px = curvePoints[i].x * width;
		const py = height - curvePoints[i].y * height;
		if (i === 0) {
			ctx.moveTo(px, py);
		} else {
			ctx.lineTo(px, py);
		}
	}
	ctx.stroke();
	
	// Step 5: Draw orange highlight line if progressRatio is provided
	if (progressRatio !== undefined && progressRatio >= 0) {
		ctx.strokeStyle = '#FFA500';
		ctx.lineWidth = 2;
		ctx.beginPath();
		
		ctx.moveTo(0, height);
		
		if (innerDeadzone > 0) {
			const deadzoneX = innerDeadzone * width;
			if (progressRatio < innerDeadzone) {
				const progressX = progressRatio * width;
				ctx.lineTo(progressX, height);
				ctx.stroke();
			} else {
				ctx.lineTo(deadzoneX, height);
				const antiDeadzoneY = height - antiDeadzone * height;
				ctx.lineTo(deadzoneX, antiDeadzoneY);
				
				if (totalCurveLength > 0) {
					const targetX = Math.min(progressRatio, 1.0);
					let found = false;
					
					for (let i = 0; i < curvePoints.length - 1; i++) {
						const p1 = curvePoints[i];
						const p2 = curvePoints[i + 1];
						
						if (targetX >= p1.x && targetX <= p2.x) {
							const t = p2.x !== p1.x ? (targetX - p1.x) / (p2.x - p1.x) : 0;
							const targetY = p1.y + t * (p2.y - p1.y);
							const targetPx = targetX * width;
							const targetPy = height - targetY * height;
							ctx.lineTo(targetPx, targetPy);
							found = true;
							break;
						} else if (targetX > p2.x) {
							const px2 = p2.x * width;
							const py2 = height - p2.y * height;
							ctx.lineTo(px2, py2);
						}
					}
					
					if (progressRatio >= 1.0 && !found) {
						const endPx = endPoint.x * width;
						const endPy = height - endPoint.y * height;
						ctx.lineTo(endPx, endPy);
					}
				}
				ctx.stroke();
			}
		} else if (antiDeadzone > 0) {
			const antiDeadzoneY = height - antiDeadzone * height;
			ctx.moveTo(0, antiDeadzoneY);
			
			if (totalCurveLength > 0 && progressRatio > 0) {
				const targetX = Math.min(progressRatio, 1.0);
				let found = false;
				
				for (let i = 0; i < curvePoints.length - 1; i++) {
					const p1 = curvePoints[i];
					const p2 = curvePoints[i + 1];
					
					if (targetX >= p1.x && targetX <= p2.x) {
						const t = p2.x !== p1.x ? (targetX - p1.x) / (p2.x - p1.x) : 0;
						const targetY = p1.y + t * (p2.y - p1.y);
						const targetPx = targetX * width;
						const targetPy = height - targetY * height;
						ctx.lineTo(targetPx, targetPy);
						found = true;
						break;
					} else if (targetX > p2.x) {
						const px2 = p2.x * width;
						const py2 = height - p2.y * height;
						ctx.lineTo(px2, py2);
					}
				}
				
				if (progressRatio >= 1.0 && !found) {
					const endPx = endPoint.x * width;
					const endPy = height - endPoint.y * height;
					ctx.lineTo(endPx, endPy);
				}
			}
			ctx.stroke();
		} else {
			if (totalCurveLength > 0 && progressRatio > 0) {
				const targetX = Math.min(progressRatio, 1.0);
				let found = false;
				
				for (let i = 0; i < curvePoints.length - 1; i++) {
					const p1 = curvePoints[i];
					const p2 = curvePoints[i + 1];
					
					if (targetX >= p1.x && targetX <= p2.x) {
						const t = p2.x !== p1.x ? (targetX - p1.x) / (p2.x - p1.x) : 0;
						const targetY = p1.y + t * (p2.y - p1.y);
						const targetPx = targetX * width;
						const targetPy = height - targetY * height;
						ctx.lineTo(targetPx, targetPy);
						found = true;
						break;
					} else if (targetX > p2.x) {
						const px2 = p2.x * width;
						const py2 = height - p2.y * height;
						ctx.lineTo(px2, py2);
					}
				}
				
				if (progressRatio >= 1.0 && !found) {
					const endPx = endPoint.x * width;
					const endPy = height - endPoint.y * height;
					ctx.lineTo(endPx, endPy);
				}
			}
			ctx.stroke();
		}
	}
	
	// Step 6: Draw all points
	ctx.fillStyle = '#00ff00';
	ctx.beginPath();
	ctx.arc(0, height, 6, 0, 2 * Math.PI);
	ctx.fill();
	ctx.strokeStyle = '#ffffff';
	ctx.lineWidth = 2;
	ctx.stroke();
	
	if (innerDeadzone > 0 || antiDeadzone > 0) {
		const grayOriginX = grayOriginPoint.x * width;
		const grayOriginY = height - grayOriginPoint.y * height;
		ctx.fillStyle = '#808080';
		ctx.beginPath();
		ctx.arc(grayOriginX, grayOriginY, 6, 0, 2 * Math.PI);
		ctx.fill();
		ctx.strokeStyle = '#ffffff';
		ctx.lineWidth = 2;
		ctx.stroke();
	}
	
	for (let i = 0; i < points.length; i++) {
		const transformedPoint = applyDeadzones(points[i].x, points[i].y);
		if (transformedPoint.x >= innerDeadzone && transformedPoint.x >= 0 && transformedPoint.y >= 0) {
			const px = transformedPoint.x * width;
			const py = height - transformedPoint.y * height;
			ctx.fillStyle = '#ff0000';
			ctx.beginPath();
			ctx.arc(px, py, 6, 0, 2 * Math.PI);
			ctx.fill();
			ctx.strokeStyle = '#ffffff';
			ctx.lineWidth = 2;
			ctx.stroke();
		}
	}
	
	const endPx = endPoint.x * width;
	const endPy = height - endPoint.y * height;
	ctx.fillStyle = '#00ff00';
	ctx.beginPath();
	ctx.arc(endPx, endPy, 6, 0, 2 * Math.PI);
	ctx.fill();
	ctx.strokeStyle = '#ffffff';
	ctx.lineWidth = 2;
	ctx.stroke();
};

/**
 * Validates and corrects a point's Y value to ensure strict monotonicity
 */
const validatePointMonotonicity = (point: CurvePoint, allPoints: CurvePoint[]): CurvePoint => {
	const maxYBefore = allPoints
		.filter(p => p.x < point.x)
		.reduce((max, p) => Math.max(max, p.y), 0);
	const epsilon = 0.01;
	const correctedY = Math.max(point.y, maxYBefore + epsilon);
	const clampedY = Math.max(0, Math.min(1, correctedY));
	return { x: point.x, y: clampedY };
};

/**
 * Validates and corrects all points to ensure strict monotonicity
 */
const validateAllPointsMonotonicity = (points: CurvePoint[]): CurvePoint[] => {
	const sorted = [...points].sort((a, b) => {
		if (a.x === b.x) {
			return a.y - b.y;
		}
		return a.x - b.x;
	});
	
	const validated: CurvePoint[] = [];
	const epsilon = 0.01;
	
	for (let i = 0; i < sorted.length; i++) {
		const point = sorted[i];
		const maxYBefore = validated.length > 0
			? validated.reduce((max, p) => Math.max(max, p.y), 0)
			: 0;
		let correctedY = Math.max(point.y, maxYBefore + epsilon);
		correctedY = Math.max(0, Math.min(1, correctedY));
		
		if (validated.length > 0 && point.x === validated[validated.length - 1].x) {
			const prevY = validated[validated.length - 1].y;
			correctedY = Math.max(correctedY, prevY + epsilon);
			correctedY = Math.max(0, Math.min(1, correctedY));
		}
		
		validated.push({ x: point.x, y: correctedY });
	}
	
	return validated;
};

/**
 * Calculates output distance from physical distance based on curve, deadzone, and anti-deadzone
 */
const calculateOutputDistanceFromCurve = (
	physicalDistance: number,
	curvePoints: CurvePoint[],
	innerDeadzone: number,
	antiDeadzone: number
): number => {
	if (physicalDistance < innerDeadzone) {
		return 0;
	}
	
	if (physicalDistance <= antiDeadzone) {
		return antiDeadzone;
	}
	
	const grayOriginPoint = { x: innerDeadzone, y: antiDeadzone };
	const curveStartPoint = (innerDeadzone > 0 || antiDeadzone > 0) ? grayOriginPoint : { x: 0, y: 0 };
	
	const applyDeadzones = (x: number, y: number): CurvePoint => {
		if (x < innerDeadzone) {
			return { x: -1, y: -1 };
		} else {
			const remappedY = antiDeadzone + y * (1 - antiDeadzone);
			return { x, y: remappedY };
		}
	};
	
	const adjustedPoints = curvePoints
		.map(p => applyDeadzones(p.x, p.y))
		.filter(p => p.x >= 0 && p.y >= 0 && p.x >= innerDeadzone)
		.sort((a, b) => a.x - b.x);
	
	const endPoint = applyDeadzones(1, 1);
	const fullCurvePoints: CurvePoint[] = [curveStartPoint, ...adjustedPoints, endPoint];
	
	const targetX = Math.min(physicalDistance, 1.0);
	
	for (let i = 0; i < fullCurvePoints.length - 1; i++) {
		const p1 = fullCurvePoints[i];
		const p2 = fullCurvePoints[i + 1];
		
		if (targetX >= p1.x && targetX <= p2.x) {
			if (p2.x === p1.x) {
				return p1.y;
			}
			const t = (targetX - p1.x) / (p2.x - p1.x);
			return p1.y + t * (p2.y - p1.y);
		} else if (targetX > p2.x) {
			continue;
		}
	}
	
	if (physicalDistance >= 1.0) {
		return endPoint.y;
	}
	
	return physicalDistance;
};

/**
 * Converts mouse coordinates to curve point coordinates, accounting for deadzone/anti-deadzone
 */
const inverseApplyDeadzones = (
	mouseX: number,
	mouseY: number,
	canvasWidth: number,
	canvasHeight: number,
	innerDeadzone: number,
	antiDeadzone: number
): CurvePoint => {
	const x = mouseX / canvasWidth;
	const y = 1.0 - (mouseY / canvasHeight);
	
	// If point is before deadzone, clamp to deadzone
	if (x < innerDeadzone) {
		return { x: innerDeadzone, y: 0 };
	}
	
	// Reverse anti-deadzone remapping: y = antiDeadzone + originalY * (1 - antiDeadzone)
	// originalY = (y - antiDeadzone) / (1 - antiDeadzone)
	const originalY = antiDeadzone > 0 && antiDeadzone < 1
		? (y - antiDeadzone) / (1 - antiDeadzone)
		: y;
	
	return { x: Math.max(innerDeadzone, Math.min(1, x)), y: Math.max(0, Math.min(1, originalY)) };
};

interface JoystickCurveSettingsProps {
	values: AddonPropTypes['values'];
	errors: AddonPropTypes['errors'];
	handleChange: AddonPropTypes['handleChange'];
	setFieldValue: AddonPropTypes['setFieldValue'];
}

const JoystickCurveSettings = ({
	values,
	errors,
	handleChange,
	setFieldValue,
}: JoystickCurveSettingsProps) => {
	const { t } = useTranslation();
	const { handleSubmit } = useFormikContext();
	const [isExpanded, setIsExpanded] = useState(() => {
		// Default to enabled (expanded) if not set
		return values?.joystickCurveEnabled !== undefined ? Boolean(values.joystickCurveEnabled) : true;
	});

	// Sync isExpanded with values when they change
	useEffect(() => {
		if (values?.joystickCurveEnabled !== undefined) {
			setIsExpanded(Boolean(values.joystickCurveEnabled));
		}
	}, [values?.joystickCurveEnabled]);
	
	// State for left stick
	const [leftCurvePoints, setLeftCurvePoints] = useState<CurvePoint[]>(() => {
		const saved = values?.joystickCurvePoints1;
		return Array.isArray(saved) ? saved as CurvePoint[] : [];
	});
	const [leftCurveInputValues, setLeftCurveInputValues] = useState<CurvePointInput[]>(() => {
		const saved = values?.joystickCurvePoints1;
		if (Array.isArray(saved)) {
			return (saved as CurvePoint[]).map(p => ({ x: p.x.toString(), y: p.y.toString() }));
		}
		return [];
	});
	const [leftDraggingPointIndex, setLeftDraggingPointIndex] = useState<number | null>(null);
	const leftCurveCanvasRef = useRef<HTMLCanvasElement>(null);
	const [leftStickProgressRatio, setLeftStickProgressRatio] = useState<number | undefined>(undefined);
	
	// State for right stick
	const [rightCurvePoints, setRightCurvePoints] = useState<CurvePoint[]>(() => {
		const saved = values?.joystickCurvePoints2;
		return Array.isArray(saved) ? saved as CurvePoint[] : [];
	});
	const [rightCurveInputValues, setRightCurveInputValues] = useState<CurvePointInput[]>(() => {
		const saved = values?.joystickCurvePoints2;
		if (Array.isArray(saved)) {
			return (saved as CurvePoint[]).map(p => ({ x: p.x.toString(), y: p.y.toString() }));
		}
		return [];
	});
	const [rightDraggingPointIndex, setRightDraggingPointIndex] = useState<number | null>(null);
	const rightCurveCanvasRef = useRef<HTMLCanvasElement>(null);
	const [rightStickProgressRatio, setRightStickProgressRatio] = useState<number | undefined>(undefined);
	
	// Load curve points from values when they change
	useEffect(() => {
		const saved = values?.joystickCurvePoints1;
		if (Array.isArray(saved)) {
			setLeftCurvePoints(saved as CurvePoint[]);
			setLeftCurveInputValues((saved as CurvePoint[]).map(p => ({ x: p.x.toString(), y: p.y.toString() })));
		} else {
			setLeftCurvePoints([]);
			setLeftCurveInputValues([]);
		}
	}, [values?.joystickCurvePoints1]);
	
	useEffect(() => {
		const saved = values?.joystickCurvePoints2;
		if (Array.isArray(saved)) {
			setRightCurvePoints(saved as CurvePoint[]);
			setRightCurveInputValues((saved as CurvePoint[]).map(p => ({ x: p.x.toString(), y: p.y.toString() })));
		} else {
			setRightCurvePoints([]);
			setRightCurveInputValues([]);
		}
	}, [values?.joystickCurvePoints2]);
	
	// Fetch joystick data to get progress ratio for highlight line
	useEffect(() => {
		let animationFrameId: number | null = null;
		let intervalId: ReturnType<typeof setInterval> | null = null;
		
		const fetchJoystickData = async () => {
			if (values.AnalogInputEnabled !== 1) return;
			
			// Fetch left stick
			if (values.analogAdc1PinX != null && values.analogAdc1PinX >= 0 && values.analogAdc1PinY != null && values.analogAdc1PinY >= 0) {
				try {
					const res = await fetch('/api/getJoystickCenter1');
					if (res.ok) {
						const data = await res.json();
						if (data.success) {
							const centerX = values.joystickCenterX || 2047.5;
							const centerY = values.joystickCenterY || 2047.5;
							const rawX = data.x;
							const rawY = data.y;
							const dx = centerX - 2047.5;
							const dy = centerY - 2047.5;
							const offsetX = rawX - dx;
							const offsetY = rawY - dy;
							const offsetCenterX = offsetX - 2047.5;
							const offsetCenterY = offsetY - 2047.5;
							const currentDistance = Math.sqrt(offsetCenterX * offsetCenterX + offsetCenterY * offsetCenterY);
							const rangeData = values?.joystickRangeData1 || [];
							if (rangeData.length > 0) {
								const angle = Math.atan2(offsetCenterY, offsetCenterX);
								const normalizedAngle = (angle + Math.PI) / (2.0 * Math.PI);
								const index = normalizedAngle * 48;
								const i0 = Math.floor(index) % 48;
								const i1 = (i0 + 1) % 48;
								const t = index - Math.floor(index);
								const r0 = rangeData[i0] || 0;
								const r1 = rangeData[i1] || 0;
								const scale = r0 * (1.0 - t) + r1 * t;
								const l = scale > 0 ? scale * 2047.5 : 2047.5;
								const progressRatio = l > 0 ? Math.min(1.0, Math.max(0.0, currentDistance / l)) : 0;
								setLeftStickProgressRatio(progressRatio);
							} else {
								setLeftStickProgressRatio(undefined);
							}
						}
					}
				} catch (e) {
					// Ignore errors
				}
			}
			
			// Fetch right stick
			if (values.analogAdc2PinX != null && values.analogAdc2PinX >= 0 && values.analogAdc2PinY != null && values.analogAdc2PinY >= 0) {
				try {
					const res = await fetch('/api/getJoystickCenter2');
					if (res.ok) {
						const data = await res.json();
						if (data.success) {
							const centerX = values.joystickCenterX2 || 2047.5;
							const centerY = values.joystickCenterY2 || 2047.5;
							const rawX = data.x;
							const rawY = data.y;
							const dx = centerX - 2047.5;
							const dy = centerY - 2047.5;
							const offsetX = rawX - dx;
							const offsetY = rawY - dy;
							const offsetCenterX = offsetX - 2047.5;
							const offsetCenterY = offsetY - 2047.5;
							const currentDistance = Math.sqrt(offsetCenterX * offsetCenterX + offsetCenterY * offsetCenterY);
							const rangeData = values?.joystickRangeData2 || [];
							if (rangeData.length > 0) {
								const angle = Math.atan2(offsetCenterY, offsetCenterX);
								const normalizedAngle = (angle + Math.PI) / (2.0 * Math.PI);
								const index = normalizedAngle * 48;
								const i0 = Math.floor(index) % 48;
								const i1 = (i0 + 1) % 48;
								const t = index - Math.floor(index);
								const r0 = rangeData[i0] || 0;
								const r1 = rangeData[i1] || 0;
								const scale = r0 * (1.0 - t) + r1 * t;
								const l = scale > 0 ? scale * 2047.5 : 2047.5;
								const progressRatio = l > 0 ? Math.min(1.0, Math.max(0.0, currentDistance / l)) : 0;
								setRightStickProgressRatio(progressRatio);
							} else {
								setRightStickProgressRatio(undefined);
							}
						}
					}
				} catch (e) {
					// Ignore errors
				}
			}
		};
		
		intervalId = setInterval(fetchJoystickData, 100);
		
		return () => {
			if (intervalId) clearInterval(intervalId);
			if (animationFrameId) cancelAnimationFrame(animationFrameId);
		};
	}, [values.AnalogInputEnabled, values.analogAdc1PinX, values.analogAdc1PinY, values.analogAdc2PinX, values.analogAdc2PinY, values.joystickCenterX, values.joystickCenterY, values.joystickCenterX2, values.joystickCenterY2, values.joystickRangeData1, values.joystickRangeData2]);
	
	// Draw left curve canvas
	useEffect(() => {
		if (isExpanded && leftCurveCanvasRef.current) {
			const ctx = leftCurveCanvasRef.current.getContext('2d');
			if (ctx) {
				const innerDeadzone = (values?.inner_deadzone || 0) / 100.0;
				const antiDeadzone = (values?.anti_deadzone || 0) / 100.0;
				drawCurveEditor(ctx, 260, 260, leftCurvePoints, leftStickProgressRatio, innerDeadzone, antiDeadzone);
			}
		}
	}, [isExpanded, leftCurvePoints, leftStickProgressRatio, values?.inner_deadzone, values?.anti_deadzone]);
	
	// Draw right curve canvas
	useEffect(() => {
		if (isExpanded && rightCurveCanvasRef.current) {
			const ctx = rightCurveCanvasRef.current.getContext('2d');
			if (ctx) {
				const innerDeadzone = (values?.inner_deadzone2 || 0) / 100.0;
				const antiDeadzone = (values?.anti_deadzone2 || 0) / 100.0;
				drawCurveEditor(ctx, 260, 260, rightCurvePoints, rightStickProgressRatio, innerDeadzone, antiDeadzone);
			}
		}
	}, [isExpanded, rightCurvePoints, rightStickProgressRatio, values?.inner_deadzone2, values?.anti_deadzone2]);
	
	// Sync input values with curve points
	useEffect(() => {
		if (leftCurvePoints.length === leftCurveInputValues.length) {
			const needsUpdate = leftCurvePoints.some((p, i) => {
				const inputVal = leftCurveInputValues[i];
				return !inputVal || Math.abs(parseFloat(inputVal.x || '0') - p.x) > 0.0001 || Math.abs(parseFloat(inputVal.y || '0') - p.y) > 0.0001;
			});
			if (needsUpdate) {
				setLeftCurveInputValues(leftCurvePoints.map(p => ({ x: p.x.toString(), y: p.y.toString() })));
			}
		} else {
			setLeftCurveInputValues(leftCurvePoints.map(p => ({ x: p.x.toString(), y: p.y.toString() })));
		}
	}, [leftCurvePoints]);
	
	useEffect(() => {
		if (rightCurvePoints.length === rightCurveInputValues.length) {
			const needsUpdate = rightCurvePoints.some((p, i) => {
				const inputVal = rightCurveInputValues[i];
				return !inputVal || Math.abs(parseFloat(inputVal.x || '0') - p.x) > 0.0001 || Math.abs(parseFloat(inputVal.y || '0') - p.y) > 0.0001;
			});
			if (needsUpdate) {
				setRightCurveInputValues(rightCurvePoints.map(p => ({ x: p.x.toString(), y: p.y.toString() })));
			}
		} else {
			setRightCurveInputValues(rightCurvePoints.map(p => ({ x: p.x.toString(), y: p.y.toString() })));
		}
	}, [rightCurvePoints]);
	
	// Handle mouse events for left stick
	const handleLeftMouseDown = (e: React.MouseEvent<HTMLCanvasElement>) => {
		if (!leftCurveCanvasRef.current) return;
		const rect = leftCurveCanvasRef.current.getBoundingClientRect();
		const mouseX = e.clientX - rect.left;
		const mouseY = e.clientY - rect.top;
		const innerDeadzone = (values?.inner_deadzone || 0) / 100.0;
		const antiDeadzone = (values?.anti_deadzone || 0) / 100.0;
		const clickedPoint = inverseApplyDeadzones(mouseX, mouseY, 260, 260, innerDeadzone, antiDeadzone);
		
		// Check if clicking on existing point
		const threshold = 10;
		for (let i = 0; i < leftCurvePoints.length; i++) {
			const point = leftCurvePoints[i];
			const applyDeadzones = (x: number, y: number): CurvePoint => {
				if (x < innerDeadzone) return { x: -1, y: -1 };
				const remappedY = antiDeadzone + y * (1 - antiDeadzone);
				return { x, y: remappedY };
			};
			const transformedPoint = applyDeadzones(point.x, point.y);
			if (transformedPoint.x >= innerDeadzone && transformedPoint.x >= 0 && transformedPoint.y >= 0) {
				const px = transformedPoint.x * 260;
				const py = 260 - transformedPoint.y * 260;
				const dist = Math.sqrt(Math.pow(mouseX - px, 2) + Math.pow(mouseY - py, 2));
				if (dist < threshold) {
					setLeftDraggingPointIndex(i);
					return;
				}
			}
		}
		
		// Add new point if less than 3 points
		if (leftCurvePoints.length < 3) {
			const validatedPoint = validatePointMonotonicity(clickedPoint, leftCurvePoints);
			const allPoints = [...leftCurvePoints, validatedPoint];
			const validatedAll = validateAllPointsMonotonicity(allPoints);
			setLeftCurvePoints(validatedAll);
			setLeftDraggingPointIndex(validatedAll.length - 1);
		}
	};
	
	const handleLeftMouseMove = (e: React.MouseEvent<HTMLCanvasElement>) => {
		if (leftDraggingPointIndex === null || !leftCurveCanvasRef.current) return;
		const rect = leftCurveCanvasRef.current.getBoundingClientRect();
		const mouseX = e.clientX - rect.left;
		const mouseY = e.clientY - rect.top;
		const innerDeadzone = (values?.inner_deadzone || 0) / 100.0;
		const antiDeadzone = (values?.anti_deadzone || 0) / 100.0;
		const newPoint = inverseApplyDeadzones(mouseX, mouseY, 300, 300, innerDeadzone, antiDeadzone);
		const otherPoints = leftCurvePoints.filter((_, i) => i !== leftDraggingPointIndex);
		const validatedPoint = validatePointMonotonicity(newPoint, otherPoints);
		const updatedPoints = [...leftCurvePoints];
		updatedPoints[leftDraggingPointIndex] = validatedPoint;
		const validatedAll = validateAllPointsMonotonicity(updatedPoints);
		setLeftCurvePoints(validatedAll);
	};
	
	const handleLeftMouseUp = () => {
		setLeftDraggingPointIndex(null);
	};
	
	const handleLeftMouseLeave = () => {
		setLeftDraggingPointIndex(null);
	};
	
	// Handle mouse events for right stick
	const handleRightMouseDown = (e: React.MouseEvent<HTMLCanvasElement>) => {
		if (!rightCurveCanvasRef.current) return;
		const rect = rightCurveCanvasRef.current.getBoundingClientRect();
		const mouseX = e.clientX - rect.left;
		const mouseY = e.clientY - rect.top;
		const innerDeadzone = (values?.inner_deadzone2 || 0) / 100.0;
		const antiDeadzone = (values?.anti_deadzone2 || 0) / 100.0;
		const clickedPoint = inverseApplyDeadzones(mouseX, mouseY, 260, 260, innerDeadzone, antiDeadzone);
		
		const threshold = 10;
		for (let i = 0; i < rightCurvePoints.length; i++) {
			const point = rightCurvePoints[i];
			const applyDeadzones = (x: number, y: number): CurvePoint => {
				if (x < innerDeadzone) return { x: -1, y: -1 };
				const remappedY = antiDeadzone + y * (1 - antiDeadzone);
				return { x, y: remappedY };
			};
			const transformedPoint = applyDeadzones(point.x, point.y);
			if (transformedPoint.x >= innerDeadzone && transformedPoint.x >= 0 && transformedPoint.y >= 0) {
				const px = transformedPoint.x * 260;
				const py = 260 - transformedPoint.y * 260;
				const dist = Math.sqrt(Math.pow(mouseX - px, 2) + Math.pow(mouseY - py, 2));
				if (dist < threshold) {
					setRightDraggingPointIndex(i);
					return;
				}
			}
		}
		
		if (rightCurvePoints.length < 3) {
			const validatedPoint = validatePointMonotonicity(clickedPoint, rightCurvePoints);
			const allPoints = [...rightCurvePoints, validatedPoint];
			const validatedAll = validateAllPointsMonotonicity(allPoints);
			setRightCurvePoints(validatedAll);
			setRightDraggingPointIndex(validatedAll.length - 1);
		}
	};
	
	const handleRightMouseMove = (e: React.MouseEvent<HTMLCanvasElement>) => {
		if (rightDraggingPointIndex === null || !rightCurveCanvasRef.current) return;
		const rect = rightCurveCanvasRef.current.getBoundingClientRect();
		const mouseX = e.clientX - rect.left;
		const mouseY = e.clientY - rect.top;
		const innerDeadzone = (values?.inner_deadzone2 || 0) / 100.0;
		const antiDeadzone = (values?.anti_deadzone2 || 0) / 100.0;
		const newPoint = inverseApplyDeadzones(mouseX, mouseY, 300, 300, innerDeadzone, antiDeadzone);
		const otherPoints = rightCurvePoints.filter((_, i) => i !== rightDraggingPointIndex);
		const validatedPoint = validatePointMonotonicity(newPoint, otherPoints);
		const updatedPoints = [...rightCurvePoints];
		updatedPoints[rightDraggingPointIndex] = validatedPoint;
		const validatedAll = validateAllPointsMonotonicity(updatedPoints);
		setRightCurvePoints(validatedAll);
	};
	
	const handleRightMouseUp = () => {
		setRightDraggingPointIndex(null);
	};
	
	const handleRightMouseLeave = () => {
		setRightDraggingPointIndex(null);
	};
	
	// Handle input changes for left stick
	const handleLeftInputChange = (index: number, field: 'x' | 'y', value: string) => {
		const updated = [...leftCurveInputValues];
		updated[index] = { ...updated[index], [field]: value };
		setLeftCurveInputValues(updated);
	};
	
	const handleLeftInputBlur = (index: number) => {
		const inputVal = leftCurveInputValues[index];
		if (!inputVal) return;
		const x = Math.max(0, Math.min(1, parseFloat(inputVal.x) || 0));
		const y = Math.max(0, Math.min(1, parseFloat(inputVal.y) || 0));
		const point: CurvePoint = { x, y };
		const otherPoints = leftCurvePoints.filter((_, i) => i !== index);
		const validatedPoint = validatePointMonotonicity(point, otherPoints);
		const updatedPoints = [...leftCurvePoints];
		updatedPoints[index] = validatedPoint;
		const validatedAll = validateAllPointsMonotonicity(updatedPoints);
		setLeftCurvePoints(validatedAll);
	};
	
	// Handle input changes for right stick
	const handleRightInputChange = (index: number, field: 'x' | 'y', value: string) => {
		const updated = [...rightCurveInputValues];
		updated[index] = { ...updated[index], [field]: value };
		setRightCurveInputValues(updated);
	};
	
	const handleRightInputBlur = (index: number) => {
		const inputVal = rightCurveInputValues[index];
		if (!inputVal) return;
		const x = Math.max(0, Math.min(1, parseFloat(inputVal.x) || 0));
		const y = Math.max(0, Math.min(1, parseFloat(inputVal.y) || 0));
		const point: CurvePoint = { x, y };
		const otherPoints = rightCurvePoints.filter((_, i) => i !== index);
		const validatedPoint = validatePointMonotonicity(point, otherPoints);
		const updatedPoints = [...rightCurvePoints];
		updatedPoints[index] = validatedPoint;
		const validatedAll = validateAllPointsMonotonicity(updatedPoints);
		setRightCurvePoints(validatedAll);
	};
	
	// Handle delete for left stick
	const handleLeftDelete = (index: number) => {
		const updated = leftCurvePoints.filter((_, i) => i !== index);
		setLeftCurvePoints(updated);
		setLeftCurveInputValues(updated.map(p => ({ x: p.x.toString(), y: p.y.toString() })));
	};
	
	// Handle delete for right stick
	const handleRightDelete = (index: number) => {
		const updated = rightCurvePoints.filter((_, i) => i !== index);
		setRightCurvePoints(updated);
		setRightCurveInputValues(updated.map(p => ({ x: p.x.toString(), y: p.y.toString() })));
	};
	
	// Handle reset for left stick
	const handleLeftReset = () => {
		setLeftCurvePoints([]);
		setLeftCurveInputValues([]);
	};
	
	// Handle reset for right stick
	const handleRightReset = () => {
		setRightCurvePoints([]);
		setRightCurveInputValues([]);
	};
	
	// Handle confirm for left stick
	const handleLeftConfirm = () => {
		const validated = validateAllPointsMonotonicity(leftCurvePoints);
		const sorted = validated.sort((a, b) => a.x - b.x);
		setFieldValue('joystickCurvePoints1', sorted);
	};
	
	// Handle confirm for right stick
	const handleRightConfirm = () => {
		const validated = validateAllPointsMonotonicity(rightCurvePoints);
		const sorted = validated.sort((a, b) => a.x - b.x);
		setFieldValue('joystickCurvePoints2', sorted);
	};
	
	const leftOutputDistance = calculateOutputDistanceFromCurve(
		leftStickProgressRatio !== undefined ? leftStickProgressRatio : 0,
		leftCurvePoints,
		(values?.inner_deadzone || 0) / 100.0,
		(values?.anti_deadzone || 0) / 100.0
	);
	
	const rightOutputDistance = calculateOutputDistanceFromCurve(
		rightStickProgressRatio !== undefined ? rightStickProgressRatio : 0,
		rightCurvePoints,
		(values?.inner_deadzone2 || 0) / 100.0,
		(values?.anti_deadzone2 || 0) / 100.0
	);
	
	return (
		<Section title="摇杆曲线设置">
			{isExpanded && (
			<div className="mb-3" style={{ display: 'grid', gridTemplateColumns: 'repeat(4, 260px)', gridTemplateRows: 'auto auto', gap: '16px', justifyContent: 'center', alignItems: 'start', width: 'max-content', margin: '0 auto' }}>
				{/* Row 1, Column 1: Left stick curve canvas */}
				<div className="text-center" style={{ display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'flex-start', width: '260px' }}>
					<div style={{ position: 'relative', width: '260px', height: '260px' }}>
						<canvas
							ref={leftCurveCanvasRef}
							width={260}
							height={260}
							style={{
								border: '1px solid #ccc',
								borderRadius: '4px',
								cursor: 'crosshair',
								display: 'block'
							}}
							onMouseDown={handleLeftMouseDown}
							onMouseMove={handleLeftMouseMove}
							onMouseUp={handleLeftMouseUp}
							onMouseLeave={handleLeftMouseLeave}
						/>
					</div>
				</div>

				{/* Row 1, Column 2: Left stick deadzone/anti-deadzone sliders and control points */}
				<div style={{ width: '260px', display: 'flex', flexDirection: 'column', justifyContent: 'flex-start', alignItems: 'flex-start' }}>
					<div style={{ width: '260px', textAlign: 'left', marginBottom: '16px' }}>
						<div style={{ marginBottom: '6px' }}>
							<Form.Label className="mb-0" style={{ textAlign: 'left', display: 'block', width: '100%', fontSize: '0.875rem', marginBottom: '4px' }}>
								内部死区: {((values?.inner_deadzone || 0) / 100.0).toFixed(1)}%
							</Form.Label>
							<Form.Range
								min={0}
								max={15}
								step={0.1}
								value={values?.inner_deadzone || 0}
								onChange={(e) => setFieldValue('inner_deadzone', Math.round(parseFloat(e.target.value)))}
							/>
						</div>
						<div style={{ marginBottom: '6px' }}>
							<Form.Label className="mb-0" style={{ textAlign: 'left', display: 'block', width: '100%', fontSize: '0.875rem', marginBottom: '4px' }}>
								反死区: {((values?.anti_deadzone || 0) / 100.0).toFixed(1)}%
							</Form.Label>
							<Form.Range
								min={0}
								max={15}
								step={0.1}
								value={values?.anti_deadzone || 0}
								onChange={(e) => setFieldValue('anti_deadzone', Math.round(parseFloat(e.target.value)))}
							/>
						</div>
					</div>
					<div style={{ width: '260px', textAlign: 'left' }}>
						<div style={{ fontWeight: 'bold', marginBottom: '4px', textAlign: 'left', fontSize: '0.875rem' }}>控制点</div>
						{leftCurvePoints.length > 0 ? (
							<div style={{ fontSize: '0.875rem' }}>
								{leftCurvePoints.map((point, originalIndex) => ({ point, originalIndex }))
									.sort((a, b) => a.point.x - b.point.x)
									.map(({ point, originalIndex }) => {
										const inputValue = leftCurveInputValues[originalIndex] || { x: point.x.toString(), y: point.y.toString() };
										return (
											<div key={originalIndex} style={{ marginBottom: '4px', display: 'flex', gap: '6px', alignItems: 'center' }}>
												<span style={{ minWidth: '16px', fontSize: '0.8rem' }}>X:</span>
												<Form.Control
													type="number"
													size="sm"
													min={0}
													max={1}
													step={0.001}
													value={inputValue.x}
													style={{ width: '75px', fontSize: '0.8rem', padding: '2px 6px' }}
													onChange={(e) => handleLeftInputChange(originalIndex, 'x', e.target.value)}
													onBlur={() => handleLeftInputBlur(originalIndex)}
												/>
												<span style={{ minWidth: '16px', fontSize: '0.8rem' }}>Y:</span>
												<Form.Control
													type="number"
													size="sm"
													min={0}
													max={1}
													step={0.001}
													value={inputValue.y}
													style={{ width: '75px', fontSize: '0.8rem', padding: '2px 6px' }}
													onChange={(e) => handleLeftInputChange(originalIndex, 'y', e.target.value)}
													onBlur={() => handleLeftInputBlur(originalIndex)}
												/>
												<Button
													variant="danger"
													size="sm"
													onClick={() => handleLeftDelete(originalIndex)}
													style={{ padding: '2px 8px', fontSize: '0.75rem' }}
												>
													删除
												</Button>
											</div>
										);
									})}
							</div>
						) : (
							<div style={{ fontSize: '0.875rem', color: '#6c757d' }}>暂无控制点</div>
						)}
					</div>
				</div>

				{/* Row 1, Column 3: Right stick deadzone/anti-deadzone sliders and control points */}
				<div style={{ width: '260px', display: 'flex', flexDirection: 'column', justifyContent: 'flex-start', alignItems: 'flex-start' }}>
					<div style={{ width: '260px', textAlign: 'left', marginBottom: '16px' }}>
						<div style={{ marginBottom: '6px' }}>
							<Form.Label className="mb-0" style={{ textAlign: 'left', display: 'block', width: '100%', fontSize: '0.875rem', marginBottom: '4px' }}>
								内部死区: {((values?.inner_deadzone2 || 0) / 100.0).toFixed(1)}%
							</Form.Label>
							<Form.Range
								min={0}
								max={15}
								step={0.1}
								value={values?.inner_deadzone2 || 0}
								onChange={(e) => setFieldValue('inner_deadzone2', Math.round(parseFloat(e.target.value)))}
							/>
						</div>
						<div style={{ marginBottom: '6px' }}>
							<Form.Label className="mb-0" style={{ textAlign: 'left', display: 'block', width: '100%', fontSize: '0.875rem', marginBottom: '4px' }}>
								反死区: {((values?.anti_deadzone2 || 0) / 100.0).toFixed(1)}%
							</Form.Label>
							<Form.Range
								min={0}
								max={15}
								step={0.1}
								value={values?.anti_deadzone2 || 0}
								onChange={(e) => setFieldValue('anti_deadzone2', Math.round(parseFloat(e.target.value)))}
							/>
						</div>
					</div>
					<div style={{ width: '260px', textAlign: 'left' }}>
						<div style={{ fontWeight: 'bold', marginBottom: '4px', textAlign: 'left', fontSize: '0.875rem' }}>控制点</div>
						{rightCurvePoints.length > 0 ? (
							<div style={{ fontSize: '0.875rem' }}>
								{rightCurvePoints.map((point, originalIndex) => ({ point, originalIndex }))
									.sort((a, b) => a.point.x - b.point.x)
									.map(({ point, originalIndex }) => {
										const inputValue = rightCurveInputValues[originalIndex] || { x: point.x.toString(), y: point.y.toString() };
										return (
											<div key={originalIndex} style={{ marginBottom: '4px', display: 'flex', gap: '6px', alignItems: 'center' }}>
												<span style={{ minWidth: '16px', fontSize: '0.8rem' }}>X:</span>
												<Form.Control
													type="number"
													size="sm"
													min={0}
													max={1}
													step={0.001}
													value={inputValue.x}
													style={{ width: '75px', fontSize: '0.8rem', padding: '2px 6px' }}
													onChange={(e) => handleRightInputChange(originalIndex, 'x', e.target.value)}
													onBlur={() => handleRightInputBlur(originalIndex)}
												/>
												<span style={{ minWidth: '16px', fontSize: '0.8rem' }}>Y:</span>
												<Form.Control
													type="number"
													size="sm"
													min={0}
													max={1}
													step={0.001}
													value={inputValue.y}
													style={{ width: '75px', fontSize: '0.8rem', padding: '2px 6px' }}
													onChange={(e) => handleRightInputChange(originalIndex, 'y', e.target.value)}
													onBlur={() => handleRightInputBlur(originalIndex)}
												/>
												<Button
													variant="danger"
													size="sm"
													onClick={() => handleRightDelete(originalIndex)}
													style={{ padding: '2px 8px', fontSize: '0.75rem' }}
												>
													删除
												</Button>
											</div>
										);
									})}
							</div>
						) : (
							<div style={{ fontSize: '0.875rem', color: '#6c757d' }}>暂无控制点</div>
						)}
					</div>
				</div>

				{/* Row 1, Column 4: Right stick curve canvas */}
				<div className="text-center" style={{ display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'flex-start', width: '260px' }}>
					<div style={{ position: 'relative', width: '260px', height: '260px' }}>
						<canvas
							ref={rightCurveCanvasRef}
							width={260}
							height={260}
							style={{
								border: '1px solid #ccc',
								borderRadius: '4px',
								cursor: 'crosshair',
								display: 'block'
							}}
							onMouseDown={handleRightMouseDown}
							onMouseMove={handleRightMouseMove}
							onMouseUp={handleRightMouseUp}
							onMouseLeave={handleRightMouseLeave}
						/>
					</div>
				</div>

				{/* Row 2, Column 1: Left stick physical/output distance */}
				<div style={{ display: 'flex', flexDirection: 'column', justifyContent: 'center', alignItems: 'center', width: '260px', gridColumn: '1', gap: '8px' }}>
					<div style={{ fontSize: '0.875rem', textAlign: 'center' }}>
						<div>当前摇杆物理距离：{(leftStickProgressRatio !== undefined ? leftStickProgressRatio * 100 : 0).toFixed(1)}%</div>
						<div>当前摇杆输出距离：{(leftOutputDistance * 100).toFixed(1)}%</div>
					</div>
				</div>

				{/* Row 2, Column 2: Left stick reset and confirm buttons */}
				<div style={{ display: 'flex', flexDirection: 'column', justifyContent: 'center', alignItems: 'center', width: '260px', gridColumn: '2', gap: '8px' }}>
					<div className="d-flex gap-2 justify-content-center">
						<Button
							variant="secondary"
							size="sm"
							onClick={handleLeftReset}
						>
							重置
						</Button>
						<Button
							variant="primary"
							size="sm"
							onClick={handleLeftConfirm}
						>
							确定
						</Button>
					</div>
				</div>

				{/* Row 2, Column 3: Right stick reset and confirm buttons */}
				<div style={{ display: 'flex', flexDirection: 'column', justifyContent: 'center', alignItems: 'center', width: '260px', gridColumn: '3', gap: '8px' }}>
					<div className="d-flex gap-2 justify-content-center">
						<Button
							variant="secondary"
							size="sm"
							onClick={handleRightReset}
						>
							重置
						</Button>
						<Button
							variant="primary"
							size="sm"
							onClick={handleRightConfirm}
						>
							确定
						</Button>
					</div>
				</div>

				{/* Row 2, Column 4: Right stick physical/output distance */}
				<div style={{ display: 'flex', flexDirection: 'column', justifyContent: 'center', alignItems: 'center', width: '260px', gridColumn: '4', gap: '8px' }}>
					<div style={{ fontSize: '0.875rem', textAlign: 'center' }}>
						<div>当前摇杆物理距离：{(rightStickProgressRatio !== undefined ? rightStickProgressRatio * 100 : 0).toFixed(1)}%</div>
						<div>当前摇杆输出距离：{(rightOutputDistance * 100).toFixed(1)}%</div>
					</div>
				</div>
			</div>
			)}
			
			{/* Bottom section: Save button (left) and Toggle switch (right) */}
			<div style={{ marginTop: '16px', display: 'flex', justifyContent: 'space-between', alignItems: 'center', width: '100%' }}>
				{/* Save button on the left */}
				<div>
					<Button
						variant="primary"
						onClick={(e) => {
							e.preventDefault();
							handleSubmit();
						}}
					>
						{t('Common:button-save-label')}
					</Button>
				</div>
				{/* Toggle switch on the right */}
				<div style={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
					<Form.Check
						type="switch"
						id="joystick-curve-enabled"
						label="启用摇杆曲线"
						checked={isExpanded}
						onChange={(e) => {
							const enabled = e.target.checked;
							setIsExpanded(enabled);
							setFieldValue('joystickCurveEnabled', enabled ? 1 : 0);
						}}
					/>
				</div>
			</div>
		</Section>
	);
};

export default JoystickCurveSettings;
