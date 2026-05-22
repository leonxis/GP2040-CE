import type { ReactNode } from 'react';
import { Nav, Tab } from 'react-bootstrap';
import { useTranslation } from 'react-i18next';

const PRESET_TAB_KEYS = ['preset-0', 'preset-1', 'preset-2'] as const;

export type PresetTabKey = (typeof PRESET_TAB_KEYS)[number];

type MappingPresetShellProps = {
	children: (activePresetIndex: number) => ReactNode;
	activeKey: PresetTabKey;
	onSelectPreset: (key: PresetTabKey) => void;
};

/**
 * 背键映射三套方案横向 TAB；切换 Tab 不保存，由父组件丢弃未保存编辑。
 */
export default function MappingPresetShell({ children, activeKey, onSelectPreset }: MappingPresetShellProps) {
	const { t } = useTranslation('SettingsPage');

	return (
		<Tab.Container activeKey={activeKey} onSelect={(key) => key && onSelectPreset(key as PresetTabKey)}>
			<Nav variant="tabs" className="macro-settings-top-tabs mb-3 w-100">
				{PRESET_TAB_KEYS.map((tabKey, index) => (
					<Nav.Item key={tabKey}>
						<Nav.Link eventKey={tabKey}>{t(`hml-back-scheme-tab-${index + 1}`)}</Nav.Link>
					</Nav.Item>
				))}
			</Nav>
			<Tab.Content>
				{PRESET_TAB_KEYS.map((tabKey, index) => (
					<Tab.Pane key={tabKey} eventKey={tabKey} className="pt-0">
						{activeKey === tabKey ? children(index) : null}
					</Tab.Pane>
				))}
			</Tab.Content>
		</Tab.Container>
	);
}

export { PRESET_TAB_KEYS };
