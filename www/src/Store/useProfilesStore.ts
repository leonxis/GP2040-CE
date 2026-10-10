import { create } from 'zustand';
import WebApi from '../Services/WebApi';
import { BUTTON_ACTIONS, PinActionValues } from '../Data/Pins';

// 固定槽位数：基础映射1 / 基础映射2 / 热切按键映射(层1) / 热切按键映射(层2)
export const MAX_PROFILES = 4;

// 固定槽位显示名（数组下标即槽位索引，不再可编辑）
// index 0=基础映射1 1=基础映射2 2=热切按键映射(层1) 3=热切按键映射(层2)
export const PROFILE_NAMES = ['基础映射1', '基础映射2', '热切按键映射', '热切按键映射'];

// tab 显示顺序：层紧随其所属基础映射（与数组下标解耦）
export const PROFILE_DISPLAY_ORDER = [0, 2, 1, 3];

type CustomMasks = {
	customButtonMask: number;
	customDpadMask: number;
};

export type MaskPayload = {
	action: PinActionValues;
	activatorMode: number;
} & CustomMasks;

export type PinsType = {
	[key: `pin${number}`]: MaskPayload;
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
	payload: MaskPayload,
) => void;

type SaveProfilesAndActivateResult = {
	mappingsOk: boolean;
	activateOk: boolean;
};

type Actions = {
	fetchProfiles: () => Promise<boolean>;
	saveProfiles: () => Promise<void>;
	saveProfilesAndActivate: (profileIndex: number) => Promise<SaveProfilesAndActivateResult>;
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
	fetchProfiles: async () => {
		set({ loadingProfiles: true });
		try {
			const baseProfile = await WebApi.getPinMappings();
			if (!baseProfile) {
				throw new Error('Failed to load base pin mappings');
			}
			const profileOptions = await WebApi.getProfileOptions();
			const merged = [baseProfile, ...(profileOptions ?? [])];
			const pinKeys = Object.keys(baseProfile).filter((key) => /^pin\d{2}$/.test(key));

			// 固定 4 槽：不足补空槽，多余丢弃；标签与槽位语义固定
			const profiles = PROFILE_NAMES.map((label, i) => {
				const source = merged[i];
				const pins = Object.fromEntries(
					pinKeys.map((key) => {
						const pin = source?.[key];
						return [
							key,
							{
								action: pin?.action ?? BUTTON_ACTIONS.NONE,
								customButtonMask: pin?.customButtonMask ?? 0,
								customDpadMask: pin?.customDpadMask ?? 0,
								activatorMode: pin?.activatorMode ?? 0,
							} as MaskPayload,
						];
					}),
				);
				return {
					profileLabel: label,
					// 基础映射槽默认启用；层槽默认关（左下角总闸）
					enabled: source?.enabled ?? i < 2,
					...pins,
				} as PinsType;
			});

			set({ profiles, loadingProfiles: false });
			return true;
		} catch (error) {
			console.error('Failed to load GPIO profiles:', error);
			set({ profiles: [], loadingProfiles: false });
			return false;
		}
	},
	setProfilePin: (profileIndex, pin, payload) =>
		set((state) => {
			const profiles = [...state.profiles];
			profiles[profileIndex] = {
				...profiles[profileIndex],
				[pin]: { ...payload },
			};
			return { profiles };
		}),
	saveProfiles: async () => {
		const { profiles } = get();
		if (profiles.length === 0) {
			throw new Error('No profiles loaded');
		}
		// 必须串行发送：设备端 lwIP HTTP POST 使用单一全局接收缓冲区，
		// 并发 POST 会互相覆盖导致保存静默失败
		await WebApi.setPinMappings(profiles[0]);
		await WebApi.setProfileOptions(profiles.slice(1, MAX_PROFILES));
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
