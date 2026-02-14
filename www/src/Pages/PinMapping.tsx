import {
	FormEvent,
	memo,
	useCallback,
	useContext,
	useEffect,
	useState,
} from 'react';
import { NavLink } from 'react-router-dom';
import { useShallow } from 'zustand/react/shallow';
import {
	Alert,
	Button,
	Col,
	Form,
	FormCheck,
	Nav,
	OverlayTrigger,
	Row,
	Tab,
	Tooltip,
} from 'react-bootstrap';
import { Trans, useTranslation } from 'react-i18next';
import invert from 'lodash/invert';
import omit from 'lodash/omit';

import { AppContext } from '../Contexts/AppContext';
import useProfilesStore, {
	MaskPayload,
	MAX_PROFILES,
} from '../Store/useProfilesStore';

import Section from '../Components/Section';
import CustomSelect from '../Components/CustomSelect';
import CaptureButton from '../Components/CaptureButton';

import { BUTTON_MASKS, DPAD_MASKS, getButtonLabels } from '../Data/Buttons';
import { BUTTON_ACTIONS, PinActionKeys, PinActionValues } from '../Data/Pins';
import './PinMapping.scss';
import { MultiValue, SingleValue } from 'react-select';
import InfoCircle from '../Icons/InfoCircle';
import WebApi from '../Services/WebApi';

type OptionType = {
	label: string;
	value: PinActionValues;
	type: string;
	customButtonMask: number;
	customDpadMask: number;
};

const disabledOptions = [
	BUTTON_ACTIONS.RESERVED,
	BUTTON_ACTIONS.ASSIGNED_TO_ADDON,
] as PinActionValues[];

const getMask = (maskArr: { label: string; value: number }[], key: string) =>
	maskArr.find(
		({ label }) => label?.toUpperCase() === key.split('BUTTON_PRESS_')?.pop(),
	);

const isNonSelectable = (action: PinActionValues) =>
	[
		BUTTON_ACTIONS.NONE,
		BUTTON_ACTIONS.CUSTOM_BUTTON_COMBO,
		...disabledOptions,
	].includes(action);

const isDisabled = (action: PinActionValues) =>
	disabledOptions.includes(action);

// Check if action is a keyboard key (KEYBOARD_KEY_* actions)
const isKeyboardKey = (action: PinActionValues) => {
	// Keyboard key actions range from KEYBOARD_KEY_A (131) to KEYBOARD_KEY_9 (169)
	return action >= BUTTON_ACTIONS.KEYBOARD_KEY_A && 
	       action <= BUTTON_ACTIONS.KEYBOARD_KEY_9;
};

const options = Object.entries(BUTTON_ACTIONS)
	.filter(([, value]) => !isNonSelectable(value) && !isKeyboardKey(value))
	.map(([key, value]) => {
		const buttonMask = getMask(BUTTON_MASKS, key);
		const dpadMask = getMask(DPAD_MASKS, key);

		return {
			label: key,
			value,
			type: buttonMask
				? 'customButtonMask'
				: dpadMask
				? 'customDpadMask'
				: 'action',
			customButtonMask: buttonMask?.value || 0,
			customDpadMask: dpadMask?.value || 0,
		};
	});

// Keyboard key options
const keyboardKeyOptions: OptionType[] = [
	{ label: 'KEYBOARD_KEY_A', value: BUTTON_ACTIONS.KEYBOARD_KEY_A, type: 'keyboard', customButtonMask: 0, customDpadMask: 0 },
	{ label: 'KEYBOARD_KEY_B', value: BUTTON_ACTIONS.KEYBOARD_KEY_B, type: 'keyboard', customButtonMask: 0, customDpadMask: 0 },
	{ label: 'KEYBOARD_KEY_C', value: BUTTON_ACTIONS.KEYBOARD_KEY_C, type: 'keyboard', customButtonMask: 0, customDpadMask: 0 },
	{ label: 'KEYBOARD_KEY_D', value: BUTTON_ACTIONS.KEYBOARD_KEY_D, type: 'keyboard', customButtonMask: 0, customDpadMask: 0 },
	{ label: 'KEYBOARD_KEY_E', value: BUTTON_ACTIONS.KEYBOARD_KEY_E, type: 'keyboard', customButtonMask: 0, customDpadMask: 0 },
	{ label: 'KEYBOARD_KEY_F', value: BUTTON_ACTIONS.KEYBOARD_KEY_F, type: 'keyboard', customButtonMask: 0, customDpadMask: 0 },
	{ label: 'KEYBOARD_KEY_G', value: BUTTON_ACTIONS.KEYBOARD_KEY_G, type: 'keyboard', customButtonMask: 0, customDpadMask: 0 },
	{ label: 'KEYBOARD_KEY_H', value: BUTTON_ACTIONS.KEYBOARD_KEY_H, type: 'keyboard', customButtonMask: 0, customDpadMask: 0 },
	{ label: 'KEYBOARD_KEY_I', value: BUTTON_ACTIONS.KEYBOARD_KEY_I, type: 'keyboard', customButtonMask: 0, customDpadMask: 0 },
	{ label: 'KEYBOARD_KEY_J', value: BUTTON_ACTIONS.KEYBOARD_KEY_J, type: 'keyboard', customButtonMask: 0, customDpadMask: 0 },
	{ label: 'KEYBOARD_KEY_K', value: BUTTON_ACTIONS.KEYBOARD_KEY_K, type: 'keyboard', customButtonMask: 0, customDpadMask: 0 },
	{ label: 'KEYBOARD_KEY_L', value: BUTTON_ACTIONS.KEYBOARD_KEY_L, type: 'keyboard', customButtonMask: 0, customDpadMask: 0 },
	{ label: 'KEYBOARD_KEY_M', value: BUTTON_ACTIONS.KEYBOARD_KEY_M, type: 'keyboard', customButtonMask: 0, customDpadMask: 0 },
	{ label: 'KEYBOARD_KEY_N', value: BUTTON_ACTIONS.KEYBOARD_KEY_N, type: 'keyboard', customButtonMask: 0, customDpadMask: 0 },
	{ label: 'KEYBOARD_KEY_O', value: BUTTON_ACTIONS.KEYBOARD_KEY_O, type: 'keyboard', customButtonMask: 0, customDpadMask: 0 },
	{ label: 'KEYBOARD_KEY_P', value: BUTTON_ACTIONS.KEYBOARD_KEY_P, type: 'keyboard', customButtonMask: 0, customDpadMask: 0 },
	{ label: 'KEYBOARD_KEY_Q', value: BUTTON_ACTIONS.KEYBOARD_KEY_Q, type: 'keyboard', customButtonMask: 0, customDpadMask: 0 },
	{ label: 'KEYBOARD_KEY_R', value: BUTTON_ACTIONS.KEYBOARD_KEY_R, type: 'keyboard', customButtonMask: 0, customDpadMask: 0 },
	{ label: 'KEYBOARD_KEY_S', value: BUTTON_ACTIONS.KEYBOARD_KEY_S, type: 'keyboard', customButtonMask: 0, customDpadMask: 0 },
	{ label: 'KEYBOARD_KEY_T', value: BUTTON_ACTIONS.KEYBOARD_KEY_T, type: 'keyboard', customButtonMask: 0, customDpadMask: 0 },
	{ label: 'KEYBOARD_KEY_U', value: BUTTON_ACTIONS.KEYBOARD_KEY_U, type: 'keyboard', customButtonMask: 0, customDpadMask: 0 },
	{ label: 'KEYBOARD_KEY_V', value: BUTTON_ACTIONS.KEYBOARD_KEY_V, type: 'keyboard', customButtonMask: 0, customDpadMask: 0 },
	{ label: 'KEYBOARD_KEY_W', value: BUTTON_ACTIONS.KEYBOARD_KEY_W, type: 'keyboard', customButtonMask: 0, customDpadMask: 0 },
	{ label: 'KEYBOARD_KEY_X', value: BUTTON_ACTIONS.KEYBOARD_KEY_X, type: 'keyboard', customButtonMask: 0, customDpadMask: 0 },
	{ label: 'KEYBOARD_KEY_Y', value: BUTTON_ACTIONS.KEYBOARD_KEY_Y, type: 'keyboard', customButtonMask: 0, customDpadMask: 0 },
	{ label: 'KEYBOARD_KEY_Z', value: BUTTON_ACTIONS.KEYBOARD_KEY_Z, type: 'keyboard', customButtonMask: 0, customDpadMask: 0 },
	{ label: 'KEYBOARD_KEY_CTRL', value: BUTTON_ACTIONS.KEYBOARD_KEY_CTRL, type: 'keyboard', customButtonMask: 0, customDpadMask: 0 },
	{ label: 'KEYBOARD_KEY_SHIFT', value: BUTTON_ACTIONS.KEYBOARD_KEY_SHIFT, type: 'keyboard', customButtonMask: 0, customDpadMask: 0 },
	{ label: 'KEYBOARD_KEY_ALT_F4', value: BUTTON_ACTIONS.KEYBOARD_KEY_ALT_F4, type: 'keyboard', customButtonMask: 0, customDpadMask: 0 },
	{ label: 'KEYBOARD_KEY_0', value: BUTTON_ACTIONS.KEYBOARD_KEY_0, type: 'keyboard', customButtonMask: 0, customDpadMask: 0 },
	{ label: 'KEYBOARD_KEY_1', value: BUTTON_ACTIONS.KEYBOARD_KEY_1, type: 'keyboard', customButtonMask: 0, customDpadMask: 0 },
	{ label: 'KEYBOARD_KEY_2', value: BUTTON_ACTIONS.KEYBOARD_KEY_2, type: 'keyboard', customButtonMask: 0, customDpadMask: 0 },
	{ label: 'KEYBOARD_KEY_3', value: BUTTON_ACTIONS.KEYBOARD_KEY_3, type: 'keyboard', customButtonMask: 0, customDpadMask: 0 },
	{ label: 'KEYBOARD_KEY_4', value: BUTTON_ACTIONS.KEYBOARD_KEY_4, type: 'keyboard', customButtonMask: 0, customDpadMask: 0 },
	{ label: 'KEYBOARD_KEY_5', value: BUTTON_ACTIONS.KEYBOARD_KEY_5, type: 'keyboard', customButtonMask: 0, customDpadMask: 0 },
	{ label: 'KEYBOARD_KEY_6', value: BUTTON_ACTIONS.KEYBOARD_KEY_6, type: 'keyboard', customButtonMask: 0, customDpadMask: 0 },
	{ label: 'KEYBOARD_KEY_7', value: BUTTON_ACTIONS.KEYBOARD_KEY_7, type: 'keyboard', customButtonMask: 0, customDpadMask: 0 },
	{ label: 'KEYBOARD_KEY_8', value: BUTTON_ACTIONS.KEYBOARD_KEY_8, type: 'keyboard', customButtonMask: 0, customDpadMask: 0 },
	{ label: 'KEYBOARD_KEY_9', value: BUTTON_ACTIONS.KEYBOARD_KEY_9, type: 'keyboard', customButtonMask: 0, customDpadMask: 0 },
];

const groupedOptions = [
	{
		label: 'Buttons',
		options: options.filter(({ type }) => type !== 'action'),
	},
	{
		label: 'Actions',
		options: options.filter(({ type }) => type === 'action'),
	},
	{
		label: 'Keyboard Keys',
		options: keyboardKeyOptions,
	},
];

const getMultiValue = (pinData: MaskPayload) => {
	if (pinData.action === BUTTON_ACTIONS.NONE) return;
	if (isDisabled(pinData.action)) {
		const actionKey = invert(BUTTON_ACTIONS)[pinData.action];
		return [
			{
				label: actionKey,
				value: pinData.action,
				type: 'action',
				customButtonMask: pinData.customButtonMask,
				customDpadMask: pinData.customDpadMask,
			},
		];
	}

	// Check if it's a keyboard key
	const keyboardOption = keyboardKeyOptions.find((opt) => opt.value === pinData.action);
	if (keyboardOption) {
		return [keyboardOption];
	}

	return pinData.action === BUTTON_ACTIONS.CUSTOM_BUTTON_COMBO
		? options.filter(
				({ type, customButtonMask, customDpadMask }) =>
					(pinData.customButtonMask & customButtonMask &&
						type === 'customButtonMask') ||
					(pinData.customDpadMask & customDpadMask &&
						type === 'customDpadMask'),
		  )
		: options.filter((option) => option.value === pinData.action);
};

const ProfileLabel = memo(function ProfileLabel({
	profileIndex,
}: {
	profileIndex: number;
}) {
	const { t } = useTranslation('');
	const setProfileLabel = useProfilesStore((state) => state.setProfileLabel);
	const profileLabel = useProfilesStore(
		(state) => state.profiles[profileIndex].profileLabel,
	);

	const onLabelChange = useCallback(
		(event: React.ChangeEvent<HTMLInputElement>) =>
			setProfileLabel(
				profileIndex,
				event.target.value.replace(/[^a-zA-Z0-9\s]/g, ''),
			),
		[],
	);

	return (
		<div className="pin-grid">
			<Form.Label>{t('PinMapping:profile-label-title')}</Form.Label>
			<Form.Control
				type="text"
				value={profileLabel}
				placeholder={t('PinMapping:profile-label-default', {
					profileNumber: profileIndex + 1,
				})}
				onChange={onLabelChange}
				maxLength={16}
				pattern="[a-zA-Z0-9\s]+"
			/>
			<Form.Text muted>{t('PinMapping:profile-label-description')}</Form.Text>
		</div>
	);
});

const PinSelectList = memo(function PinSelectList({
	profileIndex,
}: {
	profileIndex: number;
}) {
	const setProfilePin = useProfilesStore((state) => state.setProfilePin);

	const pins = useProfilesStore(
		useShallow((state) =>
			omit(state.profiles[profileIndex], ['profileLabel', 'enabled']),
		),
	);
	const { t } = useTranslation('');
	const { buttonLabels } = useContext(AppContext);
	const { buttonLabelType, swapTpShareLabels } = buttonLabels;
	const CURRENT_BUTTONS = getButtonLabels(buttonLabelType, swapTpShareLabels);
	const buttonNames = omit(CURRENT_BUTTONS, ['label', 'value']);

	const onChange = useCallback(
		(pin: string) =>
			(selected: MultiValue<OptionType> | SingleValue<OptionType>) => {
				// Handle clearing
				if (!selected || (Array.isArray(selected) && !selected.length)) {
					setProfilePin(profileIndex, pin, {
						action: BUTTON_ACTIONS.NONE,
						customButtonMask: 0,
						customDpadMask: 0,
					});
				} else if (Array.isArray(selected) && selected.length > 1) {
					// Check if selected contains keyboard keys or action types
					const hasKeyboard = selected.some(opt => opt.type === 'keyboard');
					const hasAction = selected.some(opt => opt.type === 'action');
					
					// If contains keyboard or action, only allow single selection (prevent combinations)
					if (hasKeyboard || hasAction) {
						const lastSelected = selected[selected.length - 1];
						setProfilePin(profileIndex, pin, {
							action: lastSelected.value,
							customButtonMask: 0,
							customDpadMask: 0,
						});
					} else {
						// Allow button combinations (only customButtonMask and customDpadMask types)
						setProfilePin(
							profileIndex,
							pin,
							selected.reduce(
								(masks, option) => ({
									...masks,
									customButtonMask:
										option.type === 'customButtonMask'
											? masks.customButtonMask ^ option.customButtonMask
											: masks.customButtonMask,
									customDpadMask:
										option.type === 'customDpadMask'
											? masks.customDpadMask ^ option.customDpadMask
											: masks.customDpadMask,
								}),
								{
									action: BUTTON_ACTIONS.CUSTOM_BUTTON_COMBO,
									customButtonMask: 0,
									customDpadMask: 0,
								},
							),
						);
					}
				} else {
					const singleSelected = Array.isArray(selected) ? selected[0] : selected;
					setProfilePin(profileIndex, pin, {
						action: singleSelected.value,
						customButtonMask: 0,
						customDpadMask: 0,
					});
				}
			},
		[profileIndex, setProfilePin],
	);

	const getOptionLabel = useCallback(
		(option: OptionType) => {
			// Handle keyboard keys
			if (option.type === 'keyboard') {
				const keyName = option.label?.replace('KEYBOARD_KEY_', '');
				if (keyName === 'ALT_F4') {
					return 'KB: Alt+F4';
				}
				// Add 'KB: ' prefix to distinguish keyboard keys from gamepad buttons
				return `KB: ${keyName || option.label}`;
			}
			// Handle regular buttons
			const labelKey = option.label?.split('BUTTON_PRESS_')?.pop();
			// Need to fallback as some button actions are not part of button names
			return (
				(labelKey && buttonNames[labelKey]) ||
				t(`Proto:GpioAction.${option.label}`)
			);
		},
		[buttonNames, t],
	);
	return (
		<div className="pin-grid gap-3 mt-2">
			{Object.entries(pins).map(([pin, pinData], index) => (
				<div key={`select-${index}`} className="d-flex align-items-center">
					<div className="d-flex flex-shrink-0" style={{ width: '3.5rem' }}>
						<label>GP{index}</label>
					</div>
					{index === 0 || index === 1 ? (
						<span className="text-muted">
							{t('PinMapping:pin-not-modifiable', { defaultValue: '不可修改' })}
						</span>
					) : (
						<CustomSelect
							isClearable
							isMulti={!isDisabled(pinData.action) && 
								// Disable multi-select for keyboard keys
								!keyboardKeyOptions.some(opt => opt.value === pinData.action) &&
								// Disable multi-select for action types (non-button actions)
								!options.some(opt => opt.value === pinData.action && opt.type === 'action')}
							options={groupedOptions}
							isDisabled={isDisabled(pinData.action)}
							getOptionLabel={getOptionLabel}
							onChange={onChange(pin)}
							value={getMultiValue(pinData)}
						/>
					)}
				</div>
			))}
		</div>
	);
});

const PinSection = memo(function PinSection({
	profileIndex,
}: {
	profileIndex: number;
}) {
	const { t } = useTranslation('');
	const copyBaseProfile = useProfilesStore((state) => state.copyBaseProfile);
	const setProfilePin = useProfilesStore((state) => state.setProfilePin);
	const saveProfiles = useProfilesStore((state) => state.saveProfiles);
	const toggleProfileEnabled = useProfilesStore(
		(state) => state.toggleProfileEnabled,
	);
	const enabled = useProfilesStore(
		(state) => state.profiles[profileIndex].enabled,
	);
	const profileLabel =
		useProfilesStore((state) => state.profiles[profileIndex].profileLabel) ||
		t('PinMapping:profile-label-default', {
			profileNumber: profileIndex + 1,
		});

	const [activeProfile, setActiveProfile] = useState(0);

	const { updateUsedPins, buttonLabels, setLoading } = useContext(AppContext);
	const { buttonLabelType, swapTpShareLabels } = buttonLabels;
	const CURRENT_BUTTONS = getButtonLabels(buttonLabelType, swapTpShareLabels);
	const buttonNames = omit(CURRENT_BUTTONS, ['label', 'value']);

	const [saveMessage, setSaveMessage] = useState('');

	const handleSubmit = useCallback(async (e: FormEvent<HTMLFormElement>) => {
		e.preventDefault();
		e.stopPropagation();
		try {
			await saveProfiles();
			updateUsedPins();
			setSaveMessage(t('Common:saved-success-message'));
		} catch (error) {
			setSaveMessage(t('Common:saved-error-message'));
		}
	}, []);

	useEffect(() => {
		async function getActiveProfile() {
			const { profileNumber } = await WebApi.getGamepadOptions(setLoading);
			setActiveProfile(profileNumber - 1);
		}
		getActiveProfile();
	}, []);

	return (
		<>
			<div className="alert alert-warning">
				<Trans ns="PinMapping" i18nKey="alert-text">
					Mapping buttons to pins that aren&apos;t connected or available can
					leave the device in non-functional state. To clear the invalid
					configuration go to the{' '}
					<NavLink to="/reset-settings">Reset Settings</NavLink> page.
				</Trans>
				<br />
				<br />
				{t(`PinMapping:profile-pins-warning`)}
			</div>
			<Section
				title={t('PinMapping:profile-pin-mapping-title', {
					profileLabel,
				})}
			>
				<Form onSubmit={handleSubmit}>
					<div className="d-flex justify-content-between">
						<ProfileLabel profileIndex={profileIndex} />
						{profileIndex > 0 && (
							<div className="d-flex">
								<FormCheck
									disabled={profileIndex === activeProfile}
									size={3}
									label={
										<OverlayTrigger
											overlay={
												<Tooltip>
													{profileIndex === activeProfile
														? t('PinMapping:profile-enabled-active-tooltip')
														: t('PinMapping:profile-enabled-tooltip')}
												</Tooltip>
											}
										>
											<div className="d-flex gap-1">
												<label>{t('Common:switch-enabled')} </label>
												<InfoCircle />
											</div>
										</OverlayTrigger>
									}
									type="switch"
									reverse
									checked={enabled}
									onChange={() => {
										toggleProfileEnabled(profileIndex);
									}}
								/>
							</div>
						)}
					</div>
					<hr />

					<PinSelectList profileIndex={profileIndex} />
					<div className="d-flex gap-3 my-3">
						<CaptureButton
							labels={Object.values(buttonNames)}
							onChange={(label, pin) =>
								setProfilePin(
									profileIndex,
									// Convert getHeldPins format to setPinMappings format
									pin < 10 ? `pin0${pin}` : `pin${pin}`,
									{
										// Maps current mode buttons to actions
										action:
											BUTTON_ACTIONS[
												`BUTTON_PRESS_${invert(buttonNames)[
													label
												].toUpperCase()}` as PinActionKeys
											],
										customButtonMask: 0,
										customDpadMask: 0,
									},
								)
							}
						/>
						{profileIndex > 0 && (
							<Button onClick={() => copyBaseProfile(profileIndex)}>
								{t(`PinMapping:profile-copy-base`)}
							</Button>
						)}
						<Button type="submit">{t('Common:button-save-label')}</Button>
					</div>
					{saveMessage && <Alert variant="info">{saveMessage}</Alert>}
				</Form>
			</Section>
		</>
	);
});

export default function PinMapping() {
	const fetchProfiles = useProfilesStore((state) => state.fetchProfiles);
	const addProfile = useProfilesStore((state) => state.addProfile);
	const profiles = useProfilesStore((state) => state.profiles);
	const loadingProfiles = useProfilesStore((state) => state.loadingProfiles);

	const [pressedPin, setPressedPin] = useState<number | null>(null);
	const { t } = useTranslation('');

	useEffect(() => {
		fetchProfiles();
	}, []);

	return (
		<Tab.Container defaultActiveKey="profile-0">
			<Row>
				<Col md={3}>
					{loadingProfiles && (
						<div className="d-flex justify-content-center">
							<span className="spinner-border" />
						</div>
					)}
					<Nav variant="pills" className="flex-column">
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
						{profiles.length !== MAX_PROFILES && (
							<Button
								type="button"
								className="mt-1"
								variant="outline"
								onClick={addProfile}
							>
								{t('PinMapping:profile-add-button')}
							</Button>
						)}
					</Nav>
					<hr />
					<p className="text-center">{t('PinMapping:sub-header-text')}</p>
					<div className="d-flex justify-content-center pb-3">
						<CaptureButton
							buttonLabel={t('PinMapping:pin-viewer')}
							labels={['']}
							onChange={(_, pin) => setPressedPin(pin)}
						/>
					</div>
					{pressedPin !== null && (
						<div className="alert alert-info mt-3">
							<strong>{t('PinMapping:pin-pressed', { pressedPin })}</strong>
						</div>
					)}
				</Col>
				<Col md={9}>
					<Tab.Content>
						{profiles.map((_, index) => (
							<Tab.Pane key={`profile-${index}`} eventKey={`profile-${index}`}>
								<PinSection profileIndex={index} />
							</Tab.Pane>
						))}
					</Tab.Content>
				</Col>
			</Row>
		</Tab.Container>
	);
}
