import React, { useEffect, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { useLocation } from 'react-router-dom';
import { Row, Col, Nav } from 'react-bootstrap';
import ModeSettings from './HMLSettings/components/ModeSettings';
import BackPaddleSettings from './HMLSettings/components/BackPaddleSettings';
import KeySwapSettings from './HMLSettings/components/KeySwapSettings';
import ButtonSettingsHub from './HMLSettings/components/ButtonSettingsHub';
import FunctionButtons from './HMLSettings/components/FunctionButtons';
import CalibrationSettings from './HMLSettings/components/CalibrationSettings';
import MotionSettings from './HMLSettings/components/MotionSettings';
import HardwareConfig from './HMLSettings/components/HardwareConfig';
import BackupReset from './HMLSettings/components/BackupReset';
import MacroSettings from './HMLSettings/components/MacroSettings';
import { TABS, HML_PANEL_KEY_SET } from './HMLSettings/constants/hmlInputModes';

function normalizeActiveKey(key) {
	return typeof key === 'string' && HML_PANEL_KEY_SET.has(key) ? key : 'mode';
}

/** 侧栏 pill 高亮：Hub 子页与宏页统一落在「按键设置」。 */
function navHighlightKeyForContent(contentKey) {
	if (contentKey === 'key-swap' || contentKey === 'back-button-settings' || contentKey === 'macros') {
		return 'button-settings';
	}
	return contentKey;
}

export default function HMLSettingsPage() {
	const { t } = useTranslation('SettingsPage');
	const location = useLocation();
	const [activeKey, setActiveKey] = useState(() =>
		normalizeActiveKey(location.state?.activeKey),
	);

	useEffect(() => {
		setActiveKey(normalizeActiveKey(location.state?.activeKey));
	}, [location.key]);

	const renderContent = () => {
		switch (activeKey) {
			case 'mode':
				return <ModeSettings />;
			case 'button-settings':
				return <ButtonSettingsHub />;
			case 'key-swap':
				return <KeySwapSettings />;
			case 'back-button-settings':
				return <BackPaddleSettings />;
			case 'function-buttons':
				return <FunctionButtons />;
			case 'calibration':
				return <CalibrationSettings />;
			case 'motion':
				return <MotionSettings />;
			case 'macros':
				return <MacroSettings />;
			case 'hardware':
				return <HardwareConfig />;
			case 'backup-reset':
				return <BackupReset />;
			default:
				return null;
		}
	};

	return (
		<div className="mt-4 hml-settings-page">
			<Row style={{ flexWrap: 'nowrap' }}>
				<Col style={{ width: '150px', flex: '0 0 150px', maxWidth: '150px' }}>
					<Nav
						variant="pills"
						className="flex-column"
						activeKey={navHighlightKeyForContent(activeKey)}
						onSelect={(k) => {
							if (!k) return;
							setActiveKey(k);
						}}
					>
						{TABS.map((tab) => (
							<Nav.Item key={tab.key}>
								<Nav.Link eventKey={tab.key}>{t(tab.labelKey)}</Nav.Link>
							</Nav.Item>
						))}
					</Nav>
				</Col>
				<Col style={{ minWidth: 0, flex: '1 1 0' }}>
					{renderContent()}
				</Col>
			</Row>
		</div>
	);
}
