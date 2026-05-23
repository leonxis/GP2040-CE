import type { ReactNode } from 'react';
import { useCallback } from 'react';
import { Nav, Tab } from 'react-bootstrap';
import { useTranslation } from 'react-i18next';

import useProfilesStore, { MAX_PROFILES } from '../../../Store/useProfilesStore';

const ADD_PROFILE_TAB_KEY = '__add_profile__';

export type ProfileTabKey = `profile-${number}` | typeof ADD_PROFILE_TAB_KEY;

function profileIndexFromTabKey(activeKey: ProfileTabKey): number {
	if (activeKey === ADD_PROFILE_TAB_KEY) return -1;
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
	const { t } = useTranslation();
	const profiles = useProfilesStore((state) => state.profiles);
	const addProfile = useProfilesStore((state) => state.addProfile);

	const handleSelect = useCallback(
		(key: string | null) => {
			if (!key) return;
			if (key === ADD_PROFILE_TAB_KEY) {
				const newIndex = profiles.length;
				if (newIndex >= MAX_PROFILES) return;
				addProfile();
				onSelectProfile(`profile-${newIndex}`);
				return;
			}
			onSelectProfile(key as ProfileTabKey);
		},
		[addProfile, onSelectProfile, profiles.length],
	);

	const activeProfileIndex = profileIndexFromTabKey(activeKey);

	return (
		<Tab.Container activeKey={activeKey} onSelect={handleSelect}>
			<Nav variant="tabs" className="macro-settings-top-tabs mb-3 w-100">
				{profiles.map(({ profileLabel, enabled }, index) => (
					<Nav.Item key={`profile-${index}`}>
						<Nav.Link eventKey={`profile-${index}`}>
							{profileLabel ||
								t('PinMapping:profile-label-default', {
									profileNumber: index + 1,
								})}
							{!enabled && index > 0 && (
								<span>{t('PinMapping:profile-disabled')}</span>
							)}
						</Nav.Link>
					</Nav.Item>
				))}
				{profiles.length < MAX_PROFILES && (
					<Nav.Item>
						<Nav.Link eventKey={ADD_PROFILE_TAB_KEY}>
							{t('PinMapping:profile-add-button')}
						</Nav.Link>
					</Nav.Item>
				)}
			</Nav>
			<Tab.Content>
				{profiles.map((_, index) => (
					<Tab.Pane key={`profile-${index}`} eventKey={`profile-${index}`} className="pt-0">
						{activeProfileIndex === index ? children(index) : null}
					</Tab.Pane>
				))}
			</Tab.Content>
		</Tab.Container>
	);
}
