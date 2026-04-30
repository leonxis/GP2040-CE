import { useEffect, useState, useRef } from 'react';
import { Button, Modal, ProgressBar } from 'react-bootstrap';
import { useTranslation } from 'react-i18next';

import Section from '../../../Components/Section';
import WebApi from '../../../Services/WebApi';
import FirmwareUpgradeService from '../../../Services/FirmwareUpgrade';

const FILE_EXTENSION = '.gp2040';
const FILENAME = 'gp2040ce_backup_{DATE}' + FILE_EXTENSION;
type AnyRecord = Record<string, any>;

async function saveBlobWithUserPicker(blob: Blob, fileName: string): Promise<boolean> {
	const pickerWindow = window as unknown as Window & {
		showSaveFilePicker?: (options?: {
			suggestedName?: string;
			types?: Array<{
				description?: string;
				accept: Record<string, string[]>;
			}>;
		}) => Promise<FileSystemFileHandle>;
	};

	// Prefer native save picker so user can explicitly choose RPI-RP2.
	if (typeof pickerWindow.showSaveFilePicker === 'function') {
		try {
			const fileHandle = await pickerWindow.showSaveFilePicker({
				suggestedName: fileName,
				types: [
					{
						description: 'UF2 Firmware',
						accept: { 'application/octet-stream': ['.uf2'] },
					},
				],
			});
			const writable = await fileHandle.createWritable();
			await writable.write(blob);
			await writable.close();
			return true;
		} catch (error) {
			if (error instanceof DOMException && error.name === 'AbortError') {
				return false;
			}
		}
	}

	const url = URL.createObjectURL(blob);
	try {
		const a = document.createElement('a');
		a.href = url;
		a.download = fileName;
		document.body.appendChild(a);
		a.click();
		a.remove();
		return true;
	} finally {
		URL.revokeObjectURL(url);
	}
}

function deepClone<T>(value: T): T {
	if (typeof structuredClone === 'function') {
		return structuredClone(value);
	}
	return JSON.parse(JSON.stringify(value));
}

function mergeDeep(target: AnyRecord, source: AnyRecord): AnyRecord {
	const output: AnyRecord = { ...target };
	for (const key of Object.keys(source)) {
		const sourceValue = source[key];
		const targetValue = output[key];
		if (
			sourceValue &&
			typeof sourceValue === 'object' &&
			!Array.isArray(sourceValue) &&
			targetValue &&
			typeof targetValue === 'object' &&
			!Array.isArray(targetValue)
		) {
			output[key] = mergeDeep(targetValue, sourceValue);
		} else {
			output[key] = sourceValue;
		}
	}
	return output;
}

const API_BINDING = {
	display: {
		get: WebApi.getDisplayOptions,
		set: WebApi.setDisplayOptions,
	},
	splash: {
		get: WebApi.getSplashImage,
		set: WebApi.setSplashImage,
	},
	gamepad: {
		get: WebApi.getGamepadOptions,
		set: WebApi.setGamepadOptions,
	},
	led: { get: WebApi.getLedOptions, set: WebApi.setLedOptions },
	ledTheme: {
		get: WebApi.getCustomTheme,
		set: WebApi.setCustomTheme,
	},
	macros: {
		get: WebApi.getMacroAddonOptions,
		set: WebApi.setMacroAddonOptions,
	},
	pins: {
		get: WebApi.getPinMappings,
		set: WebApi.setPinMappings,
	},
	profiles: {
		get: WebApi.getProfileOptions,
		set: WebApi.setProfileOptions,
	},
	heTrigger: {
		get: WebApi.getHETriggerOptions,
		set: WebApi.setHETriggerOptions,
	},
	addons: {
		get: WebApi.getAddonsOptions,
		set: WebApi.setAddonsOptions,
	},
};

export default function BackupReset() {
	const inputFileSelect = useRef<HTMLInputElement | null>(null);
	const { t } = useTranslation();

	const [optionState, setOptionStateData] = useState<AnyRecord>({});
	const [noticeMessage, setNoticeMessage] = useState('');
	const [saveMessage, setSaveMessage] = useState('');
	const [loadMessage, setLoadMessage] = useState('');
	const [showUpgradeModal, setShowUpgradeModal] = useState(false);
	const [showManualCopyModal, setShowManualCopyModal] = useState(false);
	const [versionInfo, setVersionInfo] = useState('');
	const [versionUpdate, setVersionUpdate] = useState('');
	const [upgradeStepMessage, setUpgradeStepMessage] = useState('');
	const [downloadProgress, setDownloadProgress] = useState(0);
	const [isCheckingUpdate, setIsCheckingUpdate] = useState(false);
	const [isEnteringUpgradeMode, setIsEnteringUpgradeMode] = useState(false);
	const [isUpgradingFirmware, setIsUpgradingFirmware] = useState(false);

	const resetUpgradeModalState = () => {
		setVersionInfo(t('SettingsPage:hml-upgrade-pending'));
		setVersionUpdate(t('SettingsPage:hml-upgrade-pending'));
		setUpgradeStepMessage('');
		setDownloadProgress(0);
	};

	useEffect(() => {
		async function fetchData() {
			const exportData: AnyRecord = {};
			for (const [key, func] of Object.entries(API_BINDING)) {
				exportData[key] = await func.get();
			}
			setOptionStateData(exportData);
		}
		fetchData();
	}, []);

	const validateValues = (data: AnyRecord, nextData: AnyRecord) => {
		// Handle array cases - always use backup data for arrays to allow clearing
		if (Array.isArray(nextData)) {
			return nextData;
		}

		return mergeDeep(deepClone(data), nextData);
	};

	const setOptionsToAPIStorage = async (options: AnyRecord) => {
		for (const [key, func] of Object.entries(API_BINDING)) {
			const values = options[key];
			if (values) {
				try {
					await func.set(values);
				} catch (error: unknown) {
					const message = error instanceof Error ? error.message : 'unknown error';
					setNoticeMessage(`Failed to set ${key} options: ${message}`);
				}
			}
		}
	};

	// Reset settings function (from ResetSettingsPage)
	const handleReset = async (e: React.MouseEvent<HTMLButtonElement>) => {
		e.preventDefault();
		e.stopPropagation();

		if (window.confirm(t('ResetSettings:confirm-text'))) {
			const result = await WebApi.resetSettings();
			console.log(result);
			setTimeout(() => {
				window.location.reload();
			}, 2000);
		}
	};

	// Save settings function (from BackupPage)
	const handleSave = async () => {
		const exportData: AnyRecord = {};
		for (const [key] of Object.entries(API_BINDING)) {
			if (optionState[key] !== undefined) {
				exportData[key] = optionState[key];
			}
		}

		const fileDate = new Date().toISOString().replace(/[^0-9]/g, '');
		const name = FILENAME.replace('{DATE}', fileDate);
		const json = JSON.stringify(exportData);
		const file = new Blob([json], { type: 'text/json;charset=utf-8' });

		const a = document.createElement('a');
		a.href = URL.createObjectURL(file);
		a.download = name;
		a.innerHTML = 'Save Backup';

		const container = document.getElementById('root');
		if (!container) {
			setNoticeMessage('Could not find app root element.');
			return;
		}
		container.appendChild(a);

		a.click();
		a.remove();

		setSaveMessage(t('BackupPage:saved-success-message', { name }));

		setTimeout(() => {
			setSaveMessage('');
		}, 5000);
	};

	// Load settings function (from BackupPage)
	const handleFileSelect = (ev: React.ChangeEvent<HTMLInputElement>) => {
		const input = ev.target;
		if (!input) {
			setNoticeMessage(`Unknown browser error, missing event data!`);
			return;
		}
		const files = input.files;
		if (!files || files.length === 0) {
			setNoticeMessage(`No files are loaded.`);
			return;
		}

		const fileName = files[0].name;

		const reader = new FileReader();
		reader.onload = function () {
			let fileData: AnyRecord | undefined = undefined;
			try {
				fileData = JSON.parse(String(reader.result));
			} catch (e) {
				// error dialog
				setNoticeMessage(`Failed to parse data for ${fileName}!`);
				return;
			}
			if (!fileData) {
				setNoticeMessage(`No file data found for ${fileName}`);
				return;
			}

			const filteredData: AnyRecord = {};
			for (const [key] of Object.entries(API_BINDING)) {
				if (fileData[key] !== undefined) {
					const validData = validateValues(optionState[key], fileData[key]);
					filteredData[key] = validData;
				}
			}

			if (Object.keys(filteredData).length > 0) {
				const nextOptions = { ...optionState, ...filteredData };
				setOptionStateData(nextOptions);

				// write to internal storage
				setOptionsToAPIStorage(filteredData);

				setLoadMessage(`Loaded ${fileName}`);
				setNoticeMessage('');

				setTimeout(() => {
					setLoadMessage('');
				}, 5000);
			}
		};
		reader.onerror = () => {
			setNoticeMessage(`Error occured while reading ${fileName}.`);
		};
		reader.readAsText(files[0]);
	};

	const handleLoad = () => {
		inputFileSelect.current?.click();
	};

	const handleOpenUpgradeModal = () => {
		resetUpgradeModalState();
		setShowManualCopyModal(false);
		setShowUpgradeModal(true);
	};

	const handleCloseUpgradeModal = () => {
		if (isCheckingUpdate || isEnteringUpgradeMode || isUpgradingFirmware) return;
		setShowUpgradeModal(false);
	};

	const handleCheckUpdate = async () => {
		setIsCheckingUpdate(true);
		setUpgradeStepMessage(t('SettingsPage:hml-upgrade-step-checking-update'));
		setDownloadProgress(0);
		try {
			const latest = await FirmwareUpgradeService.checkLatestInfo();
			setVersionInfo(latest.version);
			setVersionUpdate(latest.releaseNote);

			const needDownload = await FirmwareUpgradeService.shouldDownloadFirmware(
				latest.version,
			);
			if (needDownload) {
				setUpgradeStepMessage(t('SettingsPage:hml-upgrade-step-downloading'));
				const firmwareBlob =
					await FirmwareUpgradeService.downloadFirmwareWithProgress(
						FirmwareUpgradeService.FIRMWARE_DOWNLOAD_URL,
						(progress) => setDownloadProgress(progress),
					);
				await FirmwareUpgradeService.cacheFirmware(firmwareBlob, latest.version);
				setUpgradeStepMessage(t('SettingsPage:hml-upgrade-cache-updated'));
			} else {
				setDownloadProgress(100);
				setUpgradeStepMessage(t('SettingsPage:hml-upgrade-cache-up-to-date'));
			}
		} catch (error) {
			console.error('Failed to check latest firmware info:', error);
			setVersionInfo(t('SettingsPage:hml-upgrade-check-failed'));
			setVersionUpdate(t('SettingsPage:hml-upgrade-check-failed'));
			setUpgradeStepMessage(t('SettingsPage:hml-upgrade-check-failed'));
		} finally {
			setIsCheckingUpdate(false);
		}
	};

	const handleEnterUpgradeMode = async () => {
		setIsEnteringUpgradeMode(true);
		try {
			await WebApi.reboot(2);
			setUpgradeStepMessage(t('SettingsPage:hml-upgrade-mode-triggered'));
		} catch (error) {
			console.error('Failed to enter upgrade mode:', error);
			setUpgradeStepMessage(t('SettingsPage:hml-upgrade-generic-failed'));
		} finally {
			setIsEnteringUpgradeMode(false);
		}
	};

	const handleFirmwareUpgrade = async () => {
		setIsUpgradingFirmware(true);
		setUpgradeStepMessage(t('SettingsPage:hml-upgrade-step-preparing-copy'));

		try {
			const firmwareBlob = await FirmwareUpgradeService.getCachedFirmwareBlob();
			if (!firmwareBlob) {
				throw new Error('Cached firmware not found');
			}

			setShowManualCopyModal(true);
			const saved = await saveBlobWithUserPicker(
				firmwareBlob,
				FirmwareUpgradeService.TARGET_UF2_FILENAME,
			);
			setUpgradeStepMessage(
				saved
					? t('SettingsPage:hml-upgrade-manual-copy-hint')
					: t('SettingsPage:hml-upgrade-user-cancelled'),
			);
		} catch (error: unknown) {
			console.error('Firmware upgrade failed:', error);
			const errorName = error instanceof DOMException ? error.name : '';
			const errorMessage = error instanceof Error ? error.message : '';
			if (errorName === 'AbortError') {
				setUpgradeStepMessage(t('SettingsPage:hml-upgrade-user-cancelled'));
			} else if (errorName === 'NotAllowedError') {
				setUpgradeStepMessage(t('SettingsPage:hml-upgrade-user-cancelled'));
			} else if (errorMessage === 'Cached firmware not found') {
				setUpgradeStepMessage(t('SettingsPage:hml-upgrade-no-cached-firmware'));
			} else {
				setUpgradeStepMessage(t('SettingsPage:hml-upgrade-generic-failed'));
			}
		} finally {
			setIsUpgradingFirmware(false);
		}
	};

	return (
		<div>
			<Section title={t('SettingsPage:hml-section-backup-reset')}>
			<div style={{ paddingLeft: '100px' }}>
				<div style={{ display: 'flex', flexDirection: 'column', gap: '12px', alignItems: 'flex-start' }}>
					<div style={{ display: 'flex', alignItems: 'center', gap: '12px' }}>
						<Button
							variant="danger"
							onClick={handleReset}
							style={{ minWidth: '120px' }}
						>
							{t('Common:button-reset-settings-label')}
						</Button>
						<span className="text-muted">
							{t('SettingsPage:hml-reset-hint')}
						</span>
					</div>
					<div style={{ display: 'flex', alignItems: 'center', gap: '12px' }}>
						<Button
							variant="success"
							onClick={handleSave}
							style={{ minWidth: '120px' }}
						>
							{t('Common:button-save-label')}
						</Button>
						<span className="text-muted">
							{t('SettingsPage:hml-backup-hint')}
						</span>
					</div>
					<div style={{ display: 'flex', alignItems: 'center', gap: '12px' }}>
						<Button
							variant="warning"
							onClick={handleLoad}
							style={{ minWidth: '120px' }}
						>
							{t('Common:button-load-label')}
						</Button>
						<span className="text-muted">
							{t('SettingsPage:hml-load-hint')}
						</span>
					</div>
					<div style={{ display: 'flex', alignItems: 'center', gap: '12px' }}>
						<Button
							variant="secondary"
							onClick={handleOpenUpgradeModal}
							style={{ minWidth: '120px' }}
						>
							{t('SettingsPage:hml-upgrade-open-modal-button')}
						</Button>
						<span className="text-muted">
							{t('SettingsPage:hml-upgrade-entry-hint')}
						</span>
					</div>
					{/* Hidden file input for load */}
					<input
						ref={inputFileSelect}
						type={'file'}
						accept={FILE_EXTENSION}
						style={{ display: 'none' }}
						onChange={handleFileSelect}
					/>
					{/* Messages */}
					{(saveMessage || loadMessage || noticeMessage) && (
						<div style={{ marginTop: '8px', fontSize: '0.875rem', minWidth: '200px' }}>
							{saveMessage && (
								<div style={{ color: 'darkcyan', fontWeight: 600 }}>
									{saveMessage}
								</div>
							)}
							{loadMessage && (
								<div style={{ color: 'darkcyan', fontWeight: 600 }}>
									{loadMessage}
								</div>
							)}
							{noticeMessage && (
								<div style={{ color: 'red', fontWeight: 'bold' }}>
									{noticeMessage}
								</div>
							)}
						</div>
					)}
				</div>
			</div>
		</Section>

		<Modal show={showUpgradeModal} onHide={handleCloseUpgradeModal} centered>
			<Modal.Header closeButton={!isCheckingUpdate && !isEnteringUpgradeMode && !isUpgradingFirmware}>
				<Modal.Title>{t('SettingsPage:hml-upgrade-modal-title')}</Modal.Title>
			</Modal.Header>
			<Modal.Body>
				<div style={{ display: 'flex', flexDirection: 'column', gap: '10px' }}>
					<div>
						<strong>{t('SettingsPage:hml-upgrade-version-info-label')}:</strong> {versionInfo}
					</div>
					<div>
						<strong>{t('SettingsPage:hml-upgrade-release-note-label')}:</strong> {versionUpdate}
					</div>
					<div className="text-muted">
						<div>{t('SettingsPage:hml-upgrade-instruction-1')}</div>
						<div>{t('SettingsPage:hml-upgrade-instruction-2')}</div>
						<div>{t('SettingsPage:hml-upgrade-instruction-3')}</div>
					</div>

					<div style={{ minHeight: '52px' }}>
						{upgradeStepMessage && (
							<div style={{ marginBottom: '8px' }}>{upgradeStepMessage}</div>
						)}
						{(isCheckingUpdate || downloadProgress > 0) && (
							<>
								<div style={{ fontSize: '0.9rem', marginBottom: '4px' }}>
									{t('SettingsPage:hml-upgrade-download-progress', {
										progress: Math.round(downloadProgress),
									})}
								</div>
								<ProgressBar
									now={downloadProgress}
									style={{ marginBottom: '8px' }}
									animated={isCheckingUpdate && downloadProgress < 100}
								/>
							</>
						)}
					</div>
				</div>
			</Modal.Body>
			<Modal.Footer>
				<Button
					variant="secondary"
					onClick={handleCloseUpgradeModal}
					disabled={isCheckingUpdate || isEnteringUpgradeMode || isUpgradingFirmware}
				>
					{t('SettingsPage:hml-upgrade-cancel-button')}
				</Button>
				<Button
					variant="primary"
					onClick={handleCheckUpdate}
					disabled={isCheckingUpdate || isEnteringUpgradeMode || isUpgradingFirmware}
				>
					{t('SettingsPage:hml-upgrade-check-button')}
				</Button>
				<Button
					variant="warning"
					onClick={handleEnterUpgradeMode}
					disabled={isCheckingUpdate || isEnteringUpgradeMode || isUpgradingFirmware}
				>
					{t('SettingsPage:hml-upgrade-mode-button')}
				</Button>
				<Button
					variant="success"
					onClick={handleFirmwareUpgrade}
					disabled={isCheckingUpdate || isEnteringUpgradeMode || isUpgradingFirmware}
				>
					{t('SettingsPage:hml-upgrade-firmware-button')}
				</Button>
			</Modal.Footer>
		</Modal>

		<Modal
			show={showManualCopyModal}
			onHide={() => setShowManualCopyModal(false)}
			centered
		>
			<Modal.Header closeButton>
				<Modal.Title>固件操作提示</Modal.Title>
			</Modal.Header>
			<Modal.Body>
				<div>1、浏览器将自动下载升级固件。</div>
				<div>2、请手动将下载的固件 GNS.uf2 复制到电脑中 RPI-RP2 磁盘中完成升级。</div>
			</Modal.Body>
			<Modal.Footer>
				<Button variant="secondary" onClick={() => setShowManualCopyModal(false)}>
					{t('SettingsPage:hml-upgrade-cancel-button')}
				</Button>
			</Modal.Footer>
		</Modal>
		</div>
	);
}
