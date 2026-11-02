import { useState, useEffect, useRef } from 'react';
import { Modal, Button, Spinner } from 'react-bootstrap';
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
	const [isCompleted, setIsCompleted] = useState(false);
	const [isFailed, setIsFailed] = useState(false);
	const [failMsg, setFailMsg] = useState('');
	const aliveRef = useRef(true);
	// isCollecting 的 ref 镜像，供卸载清理判断是否需要通知后端退出采样
	const collectingRef = useRef(false);

	useEffect(() => {
		aliveRef.current = true;
		return () => {
			aliveRef.current = false;
			// 页面切走时若仍在采样，通知设备端退出采样循环（后端无超时，否则会一直空转）
			if (collectingRef.current) {
				fetch('/api/finishCalibrateJoystickRange').catch(() => {});
			}
		};
	}, []);

	// Reset state when modal opens/closes.
	// 采样中模态框无法关闭，挂起的 fetch 只会随采样结束而返回，不存在过期数据写入问题
	useEffect(() => {
		if (show) {
			setIsCollecting(false);
			setIsCompleted(false);
			setIsFailed(false);
			setFailMsg('');
		}
	}, [show]);

	const startCalibration = async () => {
		setIsCollecting(true);
		collectingRef.current = true;

		try {
			// 后端高频采样：按轮统计（每轮需48角度全覆盖，轮内取最小值），跨轮取最大值并
			// 随持续采样不断完善。采样无超时不自动结束，用户点击确定后后端结束采样并校验
			// 数据（满3轮48点），本请求随之返回
			const res = await fetch('/api/calibrateJoystickRange', {
				method: 'POST',
				headers: { 'Content-Type': 'application/json' },
				body: JSON.stringify({
					stick: stickNumber,
					centerX: centerX ?? 0,
					centerY: centerY ?? 0,
				}),
			});

			if (!aliveRef.current) return;

			if (!res.ok) {
				throw new Error(`HTTP error! status: ${res.status}`);
			}

			const data = await res.json();
			if (!aliveRef.current) return;

			if (!data.success) {
				// 采样未满3轮48点：不写入校准数据，进入失败态提示
				setFailMsg(t('AddonsConfig:joystick-range-calibration-modal-incomplete'));
				setIsFailed(true);
				return;
			}

			// 校验返回的 rangeData 必须是完整的48个非零值
			const rangeData: number[] = Array.isArray(data.rangeData) ? data.rangeData : [];
			if (rangeData.length !== CIRCULARITY_DATA_SIZE ||
				rangeData.some(v => typeof v !== 'number' || !(v > 0))) {
				throw new Error('Invalid calibration data received');
			}

			setIsCompleted(true);
			// 仅在后端返回成功且数据完整时写入校准数据
			onComplete(rangeData);
		} catch (error) {
			if (!aliveRef.current) return;
			console.error('Range calibration error:', error);
			setFailMsg(error instanceof Error ? error.message : String(error));
			setIsFailed(true);
		} finally {
			collectingRef.current = false;
			if (aliveRef.current) {
				setIsCollecting(false);
			}
		}
	};

	// 确定：通知后端结束采样并校验数据，结果由 startCalibration 中挂起的请求返回
	const handleConfirm = () => {
		fetch('/api/finishCalibrateJoystickRange').catch((e) =>
			console.error('Failed to finish range calibration:', e)
		);
	};

	// 采样中不允许直接关闭，须点击确定结束采样
	const handleCancel = () => {
		if (isCollecting) return;
		onHide();
	};

	const getContent = () => {
		if (isCompleted) {
			return (
				<div>
					<h4>{t('AddonsConfig:joystick-range-calibration-modal-done') || '采样完成'}</h4>
					<p>外圈校准数据已采集完毕，已写入配置。</p>
				</div>
			);
		}
		if (isFailed) {
			return (
				<div>
					<h4>{t('AddonsConfig:joystick-range-calibration-modal-failed-title')}</h4>
					<p className="text-danger">{failMsg}</p>
				</div>
			);
		}
		if (isCollecting) {
			return (
				<div className="text-center py-3">
					<Spinner animation="border" role="status" className="mb-3">
						<span className="visually-hidden">Loading...</span>
					</Spinner>
					<h4>{t('AddonsConfig:joystick-range-calibration-modal-collecting-title') || '正在采样'}</h4>
					<p>
						{t('AddonsConfig:joystick-range-calibration-modal-collecting-rotate', { stick: stickLabel })}
					</p>
					<p className="small text-muted">
						{t('AddonsConfig:joystick-range-calibration-modal-collecting-hint')}
					</p>
				</div>
			);
		}
		return (
			<div>
				<h4>{t('AddonsConfig:joystick-range-calibration-modal-welcome-title')}</h4>
				<p>{t('AddonsConfig:joystick-range-calibration-modal-welcome-text', { stick: stickLabel })}</p>
				<p>
					<em>{t('AddonsConfig:joystick-range-calibration-modal-warning')}</em>
				</p>
				<p>{t('AddonsConfig:joystick-range-calibration-modal-welcome-instruction')}</p>
			</div>
		);
	};

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
				{getContent()}
			</Modal.Body>
			<Modal.Footer>
				{isCompleted ? (
					<Button variant="primary" onClick={onHide}>
						{t('AddonsConfig:joystick-range-calibration-modal-done') || '完成'}
					</Button>
				) : isFailed ? (
					<Button variant="primary" onClick={onHide}>
						{t('AddonsConfig:joystick-range-calibration-modal-confirm')}
					</Button>
				) : isCollecting ? (
					<Button variant="primary" onClick={handleConfirm}>
						{t('AddonsConfig:joystick-range-calibration-modal-confirm')}
					</Button>
				) : (
					<>
						<Button variant="secondary" onClick={handleCancel}>
							{t('AddonsConfig:joystick-range-calibration-modal-cancel')}
						</Button>
						<Button variant="primary" onClick={startCalibration}>
							{t('AddonsConfig:joystick-range-calibration-modal-start')}
						</Button>
					</>
				)}
			</Modal.Footer>
		</Modal>
	);
};

export default RangeCalibrationModal;
