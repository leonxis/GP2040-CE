import { useEffect, useRef, useState } from 'react';
import { Button, Modal } from 'react-bootstrap';
import { useTranslation } from 'react-i18next';
import WebApi from '../Services/WebApi';

type CaptureButtonTypes = {
	labels: string[];
	onChange: (label: string, pin: number) => void;
	small?: boolean;
	buttonLabel?: string;
};

type HeldPinsResponse = {
	heldPins?: number[];
	canceled?: boolean;
};

// 固件侧等待窗口 5s、含按住阶段总超时 10s，前端看门狗留出余量
const FIRMWARE_WATCHDOG_MS = 11500;
// 本地中止后给 abort 请求留出的收发缓冲
const ABORT_SETTLE_MS = 50;

const CaptureButton = ({
	labels,
	onChange,
	small = false,
	buttonLabel,
}: CaptureButtonTypes) => {
	const { t } = useTranslation('');
	const controllerRef = useRef<null | AbortController>(null);
	const inFlightRef = useRef(false);
	const stopRef = useRef(false);
	const aliveRef = useRef(true);
	const watchdogRef = useRef<ReturnType<typeof setTimeout> | null>(null);
	const [showModal, setShowModal] = useState(false);
	const [labelIndex, setLabelIndex] = useState(0);
	const [triggerCapture, setTriggerCapture] = useState(false);

	useEffect(() => {
		aliveRef.current = true;
		return () => {
			aliveRef.current = false;
			controllerRef.current?.abort();
			if (watchdogRef.current) clearTimeout(watchdogRef.current);
			// 页面切走时确保设备端阻塞循环退出
			WebApi.abortGetHeldPins();
		};
	}, []);

	const currentLabel = labels[labelIndex] || '';
	const hasNext = Boolean(labels[labelIndex + 1]);

	const timeout = (ms: number) => new Promise((resolve) => setTimeout(resolve, ms));

	const clearWatchdog = () => {
		if (watchdogRef.current) {
			clearTimeout(watchdogRef.current);
			watchdogRef.current = null;
		}
	};

	const closeAndReset = () => {
		stopRef.current = false;
		inFlightRef.current = false;
		controllerRef.current = null;
		setShowModal(false);
		setLabelIndex(0);
	};

	const getHeldPins = async () => {
		setTriggerCapture(false);
		if (inFlightRef.current) return;
		inFlightRef.current = true;

		const controller = new AbortController();
		controllerRef.current = controller;

		// 固件异常未按时返回（如 USB 丢包、阻塞循环未退出）时本地强制收尾，
		// 防止弹窗永久转圈导致整个页面失去响应
		let timedOut = false;
		watchdogRef.current = setTimeout(() => {
			timedOut = true;
			controller.abort();
		}, FIRMWARE_WATCHDOG_MS);

		let data: HeldPinsResponse | undefined;
		try {
			data = (await WebApi.getHeldPins(controller.signal)) as
				| HeldPinsResponse
				| undefined;
		} catch {
			data = undefined;
		} finally {
			clearWatchdog();
		}

		if (!aliveRef.current) return;

		const pin = data?.heldPins?.at(0);
		if (typeof pin === 'number' && !Number.isNaN(pin)) {
			onChange(currentLabel, pin);
		}

		// 仅在本地主动中止（跳过/停止/看门狗/卸载）时通知固件退出阻塞循环；
		// 固件自行返回 canceled 时说明其循环已结束，无需再置标志，避免残留 abort 影响下一轮
		if (controller.signal.aborted) {
			await timeout(ABORT_SETTLE_MS);
			await WebApi.abortGetHeldPins();
			await timeout(ABORT_SETTLE_MS);
		}

		inFlightRef.current = false;

		if (!aliveRef.current) return;
		if (stopRef.current || timedOut || !hasNext) {
			closeAndReset();
			return;
		}

		setLabelIndex((index) => index + 1);
		setTriggerCapture(true);
	};

	const stopCapture = () => {
		stopRef.current = true;
		controllerRef.current?.abort();
	};

	const skipButton = () => {
		controllerRef.current?.abort();
	};

	const startCapture = () => {
		// 防止上一轮尚未结束时重复点击堆积请求
		if (inFlightRef.current) return;
		stopRef.current = false;
		setLabelIndex(0);
		setTriggerCapture(true);
	};

	useEffect(() => {
		if (triggerCapture) {
			setShowModal(true);
			getHeldPins();
		}
	}, [triggerCapture]);

	return (
		<>
			<Modal centered show={showModal} onHide={() => stopCapture()}>
				<Modal.Header closeButton>
					<Modal.Title className="me-auto">{`${t(
						'CaptureButton:capture-button-modal-title',
					)} ${currentLabel}`}</Modal.Title>
				</Modal.Header>
				<Modal.Body className="row">
					<span className="col-sm-10">
						{t('CaptureButton:capture-button-modal-content')}
					</span>
					<span className="col-sm-1">
						<span className="spinner-border" />
					</span>
				</Modal.Body>
				<Modal.Footer>
					{hasNext && (
						<Button onClick={() => skipButton()}>
							{t('CaptureButton:capture-button-modal-skip')}
						</Button>
					)}
					<Button variant="danger" onClick={() => stopCapture()}>
						{t('CaptureButton:capture-button-modal-stop')}
					</Button>
				</Modal.Footer>
			</Modal>
			<Button onClick={() => startCapture()}>
				{small
					? '🎮'
					: `${
							buttonLabel
								? buttonLabel
								: t('CaptureButton:capture-button-button-label')
						} 🎮`}
			</Button>
		</>
	);
};

export default CaptureButton;
