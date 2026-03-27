import { useEffect, useState, useRef } from 'react';
import { Button } from 'react-bootstrap';
import { useTranslation } from 'react-i18next';
import merge from 'lodash/merge';
import cloneDeep from 'lodash/cloneDeep';

import Section from '../../../Components/Section';
import WebApi from '../../../Services/WebApi';

const FILE_EXTENSION = '.gp2040';
const FILENAME = 'gp2040ce_backup_{DATE}' + FILE_EXTENSION;

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
	const inputFileSelect = useRef();
	const { t } = useTranslation();

	const [optionState, setOptionStateData] = useState({});
	const [noticeMessage, setNoticeMessage] = useState('');
	const [saveMessage, setSaveMessage] = useState('');
	const [loadMessage, setLoadMessage] = useState('');

	useEffect(() => {
		async function fetchData() {
			const exportData = {};
			for (const [key, func] of Object.entries(API_BINDING)) {
				exportData[key] = await func.get();
			}
			setOptionStateData(exportData);
		}
		fetchData();
	}, []);

	const validateValues = (data, nextData) => {
		// Handle array cases - always use backup data for arrays to allow clearing
		if (Array.isArray(nextData)) {
			return nextData;
		}

		return merge(cloneDeep(data), nextData);
	};

	const setOptionsToAPIStorage = async (options) => {
		for (const [key, func] of Object.entries(API_BINDING)) {
			const values = options[key];
			if (values) {
				try {
					await func.set(values);
				} catch (error) {
					setNoticeMessage(`Failed to set ${key} options: ${error.message}`);
				}
			}
		}
	};

	// Reset settings function (from ResetSettingsPage)
	const handleReset = async (e) => {
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
		const exportData = {};
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
		container.appendChild(a);

		a.click();
		a.remove();

		setSaveMessage(t('BackupPage:saved-success-message', { name }));

		setTimeout(() => {
			setSaveMessage('');
		}, 5000);
	};

	// Load settings function (from BackupPage)
	const handleFileSelect = (ev) => {
		const input = ev.target;
		if (!input) {
			setNoticeMessage(`Unknown browser error, missing event data!`);
			return;
		}
		if (input.files.length === 0) {
			setNoticeMessage(`No files are loaded.`);
			return;
		}

		const fileName = input.files[0].name;

		const reader = new FileReader();
		reader.onload = function () {
			let fileData = undefined;
			try {
				fileData = JSON.parse(reader.result);
			} catch (e) {
				// error dialog
				setNoticeMessage(`Failed to parse data for ${fileName}!`);
				return;
			}
			if (!fileData) {
				setNoticeMessage(`No file data found for ${fileName}`);
				return;
			}

			const filteredData = {};
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
		reader.readAsText(input.files[0]);
	};

	const handleLoad = () => {
		inputFileSelect.current?.click();
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
		</div>
	);
}
