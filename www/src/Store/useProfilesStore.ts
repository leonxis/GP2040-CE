import { create } from 'zustand';
import WebApi from '../Services/WebApi';
import { PinActionValues } from '../Data/Pins';

// Max number of profiles that can be created, including the base profile
export const MAX_PROFILES = 6;

type CustomMasks = {
	customButtonMask: number;
	customDpadMask: number;
};

export type MaskPayload = {
	action: PinActionValues;
} & CustomMasks;

export type PinsType = {
	pin00: MaskPayload;
	pin01: MaskPayload;
	pin02: MaskPayload;
	pin03: MaskPayload;
	pin04: MaskPayload;
	pin05: MaskPayload;
	pin06: MaskPayload;
	pin07: MaskPayload;
	pin08: MaskPayload;
	pin09: MaskPayload;
	pin10: MaskPayload;
	pin11: MaskPayload;
	pin12: MaskPayload;
	pin13: MaskPayload;
	pin14: MaskPayload;
	pin15: MaskPayload;
	pin16: MaskPayload;
	pin17: MaskPayload;
	pin18: MaskPayload;
	pin19: MaskPayload;
	pin20: MaskPayload;
	pin21: MaskPayload;
	pin22: MaskPayload;
	pin23: MaskPayload;
	pin24: MaskPayload;
	pin25: MaskPayload;
	pin26: MaskPayload;
	pin27: MaskPayload;
	pin28: MaskPayload;
	pin29: MaskPayload;
	profileLabel: string;
	enabled: boolean;
};

type State = {
	profiles: PinsType[];
	loadingProfiles: boolean;
};

export type SetProfilePinType = (
	profileIndex: number,
	pin: string,
	{ action, customButtonMask, customDpadMask }: MaskPayload,
) => void;

type SaveProfilesAndActivateResult = {
	mappingsOk: boolean;
	activateOk: boolean;
};

type Actions = {
	addProfile: () => void;
	copyBaseProfile: (profileIndex: number) => void;
	fetchProfiles: () => Promise<boolean>;
	saveProfiles: () => Promise<void>;
	saveProfilesAndActivate: (profileIndex: number) => Promise<SaveProfilesAndActivateResult>;
	setProfileLabel: (profileIndex: number, profileLabel: string) => void;
	setProfilePin: SetProfilePinType;
	toggleProfileEnabled: (profileIndex: number) => void;
};

const INITIAL_STATE: State = {
	profiles: [
		// Profiles will be populated dynamically
	],
	loadingProfiles: false,
};

const useProfilesStore = create<State & Actions>()((set, get) => ({
	...INITIAL_STATE,
	addProfile: () => {
		if (get().profiles.length < MAX_PROFILES) {
			set((state) => ({
				profiles: [
					...state.profiles,
					{
						...state.profiles[0],
						profileLabel: `Profile ${state.profiles.length + 1}`,
					},
				],
			}));
		}
	},
	fetchProfiles: async () => {
		set({ loadingProfiles: true });
		try {
			const baseProfile = await WebApi.getPinMappings();
			if (!baseProfile) {
				throw new Error('Failed to load base pin mappings');
			}
			const profiles = await WebApi.getProfileOptions();
			set({
				profiles: [baseProfile, ...(profiles ?? [])],
				loadingProfiles: false,
			});
			return true;
		} catch (error) {
			console.error('Failed to load GPIO profiles:', error);
			set({ profiles: [], loadingProfiles: false });
			return false;
		}
	},
	copyBaseProfile: (profileIndex) =>
		set((state) => ({
			...state,
			profiles: state.profiles.map((profile, index) =>
				index === profileIndex
					? {
							...profile,
							...state.profiles[0],
							profileLabel: profile.profileLabel,
						}
					: profile,
			),
		})),
	setProfilePin: (
		profileIndex,
		pin,
		{ action, customButtonMask = 0, customDpadMask = 0 },
	) =>
		set((state) => {
			const profiles = [...state.profiles];
			profiles[profileIndex] = {
				...profiles[profileIndex],
				[pin]: {
					action,
					customButtonMask,
					customDpadMask,
				},
			};
			return { profiles };
		}),
	setProfileLabel: (profileIndex, profileLabel) =>
		set((state) => {
			const profiles = [...state.profiles];
			profiles[profileIndex] = { ...profiles[profileIndex], profileLabel };
			return { profiles };
		}),
	saveProfiles: async () => {
		const { profiles } = get();
		if (profiles.length === 0) {
			throw new Error('No profiles loaded');
		}
		const [baseProfile, ...alternatives] = profiles;
		return Promise.all([
			WebApi.setPinMappings(baseProfile),
			WebApi.setProfileOptions(alternatives),
		]);
	},
	saveProfilesAndActivate: async (profileIndex: number) => {
		if (profileIndex < 0 || profileIndex >= get().profiles.length) {
			return { mappingsOk: false, activateOk: false };
		}
		try {
			await get().saveProfiles();
		} catch {
			return { mappingsOk: false, activateOk: false };
		}
		const gamepadOptions = await WebApi.getGamepadOptions();
		if (!gamepadOptions) {
			return { mappingsOk: true, activateOk: false };
		}
		const activateOk = await WebApi.setGamepadOptions({
			...gamepadOptions,
			profileNumber: profileIndex + 1,
		});
		return { mappingsOk: true, activateOk: Boolean(activateOk) };
	},
	toggleProfileEnabled: (profileIndex) =>
		set((state) => {
			const profiles = [...state.profiles];
			profiles[profileIndex] = {
				...profiles[profileIndex],
				enabled: !profiles[profileIndex].enabled,
			};
			return { ...state, profiles };
		}),
}));

export default useProfilesStore;
