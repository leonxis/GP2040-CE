import React, { useEffect, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { NavLink, useLocation } from 'react-router-dom';
import { Row, Col, Nav } from 'react-bootstrap';
import ModeSettings from './HMLSettings/components/ModeSettings';
import BackButtonMapping from './HMLSettings/components/BackButtonMapping';
import FunctionButtons from './HMLSettings/components/FunctionButtons';
import CalibrationSettings from './HMLSettings/components/CalibrationSettings';
import MotionSettings from './HMLSettings/components/MotionSettings';
import HardwareConfig from './HMLSettings/components/HardwareConfig';
import BackupReset from './HMLSettings/components/BackupReset';
import MacroSettings from './HMLSettings/components/MacroSettings';
import { TABS } from './HMLSettings/constants/hmlInputModes';

const VALID_PANEL_KEYS = new Set(
	TABS.filter((tab) => !('navigate' in tab)).map((tab) => tab.key),
);

function normalizeActiveKey(key) {
	return typeof key === 'string' && VALID_PANEL_KEYS.has(key) ? key : 'mode';
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
			case 'back-buttons':
				return <BackButtonMapping />;
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
						activeKey={activeKey}
						onSelect={(k) => {
							if (k) setActiveKey(k);
						}}
					>
						{TABS.map((tab) => (
							<Nav.Item key={tab.key}>
								{'navigate' in tab ? (
									<Nav.Link
										as={NavLink}
										to={tab.navigate.to}
										state={tab.navigate.state}
									>
										{t(tab.labelKey)}
									</Nav.Link>
								) : (
									<Nav.Link eventKey={tab.key}>{t(tab.labelKey)}</Nav.Link>
								)}
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
