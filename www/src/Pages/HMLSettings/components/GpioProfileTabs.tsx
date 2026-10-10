import type { ReactNode } from 'react';
import { useCallback } from 'react';
import { Nav, Tab } from 'react-bootstrap';

import useProfilesStore, { PROFILE_DISPLAY_ORDER } from '../../../Store/useProfilesStore';

export type ProfileTabKey = `profile-${number}`;

function profileIndexFromTabKey(activeKey: ProfileTabKey): number {
	const index = parseInt(activeKey.replace('profile-', ''), 10);
	return Number.isFinite(index) ? index : -1;
}

type GpioProfileTabsProps = {
	activeKey: ProfileTabKey;
	onSelectProfile: (key: ProfileTabKey) => void;
	children: (profileIndex: number) => ReactNode;
};

export default function GpioProfileTabs({
	activeKey,
	onSelectProfile,
	children,
}: GpioProfileTabsProps) {
	const profiles = useProfilesStore((state) => state.profiles);

	const handleSelect = useCallback(
		(key: string | null) => {
			if (!key) return;
			onSelectProfile(key as ProfileTabKey);
		},
		[onSelectProfile],
	);

	const activeProfileIndex = profileIndexFromTabKey(activeKey);

	return (
		<Tab.Container activeKey={activeKey} onSelect={handleSelect}>
			<Nav variant="tabs" className="macro-settings-top-tabs mb-3 w-100">
				{PROFILE_DISPLAY_ORDER.map((index) => {
					const { profileLabel, enabled } = profiles[index];
					return (
						<Nav.Item key={`profile-${index}`}>
							<Nav.Link eventKey={`profile-${index}`}>
								{profileLabel}
								{index === 1 && !enabled && (
									<span>{' '}未启用</span>
								)}
							</Nav.Link>
						</Nav.Item>
					);
				})}
			</Nav>
			<Tab.Content>
				{PROFILE_DISPLAY_ORDER.map((index) => (
					<Tab.Pane key={`profile-${index}`} eventKey={`profile-${index}`} className="pt-0">
						{activeProfileIndex === index ? children(index) : null}
					</Tab.Pane>
				))}
			</Tab.Content>
		</Tab.Container>
	);
}
