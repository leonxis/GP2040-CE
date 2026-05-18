import type { ReactNode } from 'react';
import { Nav, Tab } from 'react-bootstrap';
import { useTranslation } from 'react-i18next';

const PRESET_TAB_KEY = 'preset-0';

/**
 * 映射预设横向 TAB 占位（单 Tab），样式与宏页一致；后续多预设时扩展 Shell state。
 */
export default function MappingPresetShell({ children }: { children: ReactNode }) {
	const { t } = useTranslation('SettingsPage');

	return (
		<Tab.Container defaultActiveKey={PRESET_TAB_KEY}>
			<Nav variant="tabs" className="macro-settings-top-tabs mb-3 w-100">
				<Nav.Item>
					<Nav.Link eventKey={PRESET_TAB_KEY}>
						{t('hml-mapping-preset-tab-placeholder')}
					</Nav.Link>
				</Nav.Item>
			</Nav>
			<Tab.Content>
				<Tab.Pane eventKey={PRESET_TAB_KEY} className="pt-0">
					{children}
				</Tab.Pane>
			</Tab.Content>
		</Tab.Container>
	);
}
