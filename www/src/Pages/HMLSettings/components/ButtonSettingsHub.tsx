import { useCallback } from 'react';
import { Button, Card } from 'react-bootstrap';
import { useTranslation } from 'react-i18next';
import { useNavigate, useLocation } from 'react-router-dom';

/** 与备份重置页操作区一致的间距与按钮宽度 */
const HUB_PAD_LEFT = '100px';
const HUB_GAP = '12px';
const hubOuterStyle = { paddingLeft: HUB_PAD_LEFT } as const;
const hubColumnStyle = {
	display: 'flex',
	flexDirection: 'column',
	gap: HUB_GAP,
	alignItems: 'flex-start',
} as const;
const hubRowStyle = {
	display: 'flex',
	alignItems: 'center',
	gap: HUB_GAP,
} as const;
const hubButtonStyle = { minWidth: '120px' } as const;

/**
 * 按键设置 Hub：四行入口（按键交换 / 背键 / 热键外链 / 宏键）。
 * 布局与备份重置页一致：保存按钮同款绿色（variant success）+ 右侧说明文案。
 */
export default function ButtonSettingsHub() {
	const { t } = useTranslation('SettingsPage');
	const navigate = useNavigate();
	const location = useLocation();

	const goHml = useCallback(
		(panelKey: string) => {
			navigate(location.pathname, { state: { activeKey: panelKey } });
		},
		[navigate, location.pathname],
	);

	return (
		<Card>
			<Card.Header>{t('hml-tab-button-settings')}</Card.Header>
			<Card.Body>
				<div style={hubOuterStyle}>
					<div style={hubColumnStyle}>
						<div style={hubRowStyle}>
							<Button variant="success" onClick={() => goHml('key-swap')} style={hubButtonStyle}>
								{t('hml-button-settings-row-key-settings')}
							</Button>
							<span className="text-muted">{t('hml-button-settings-hub-desc-key-swap')}</span>
						</div>
						<div style={hubRowStyle}>
							<Button variant="success" onClick={() => goHml('back-button-settings')} style={hubButtonStyle}>
								{t('hml-button-settings-row-back-settings')}
							</Button>
							<span className="text-muted">{t('hml-button-settings-hub-desc-back-paddle')}</span>
						</div>
						<div style={hubRowStyle}>
							<Button
								variant="success"
								onClick={() => navigate('/settings', { state: { activeTab: 'hotkey' } })}
								style={hubButtonStyle}
							>
								{t('hml-button-settings-row-hotkey-settings')}
							</Button>
							<span className="text-muted">{t('hml-button-settings-hub-desc-hotkey')}</span>
						</div>
						<div style={hubRowStyle}>
							<Button variant="success" onClick={() => goHml('macros')} style={hubButtonStyle}>
								{t('hml-button-settings-row-macro-settings')}
							</Button>
							<span className="text-muted">{t('hml-button-settings-hub-desc-macro')}</span>
						</div>
					</div>
				</div>
			</Card.Body>
		</Card>
	);
}
