import { useContext } from 'react';
import { Trans, useTranslation } from 'react-i18next';
import { FormCheck, FormLabel } from 'react-bootstrap';
import { NavLink } from 'react-router-dom';
import * as yup from 'yup';

import Section from '../Components/Section';

import { AppContext } from '../Contexts/AppContext';
import { AddonPropTypes } from '../Pages/AddonsConfigPage';

export const gamepadUSBHostScheme = {
	GamepadUSBHostAddonEnabled: yup
		.number()
		.required()
		.label('Gamepad USB Host Add-On Enabled'),
};

export const gamepadUSBHostState = {
	GamepadUSBHostAddonEnabled: 0,
};

const GamepadUSBHost = ({ values, handleChange, handleCheckbox }: AddonPropTypes) => {
	const { getAvailablePeripherals } = useContext(AppContext);
	const { t } = useTranslation();
	return (
		<Section title={
			<a
				href="https://gp2040-ce.info/add-ons/gamepad-usb-host"
				target="_blank"
				className="text-reset text-decoration-none" rel="noreferrer"
			>
				{t('AddonsConfig:gamepad-usb-host-header-text')}
			</a>
		}
		>
			<div id="GamepadUSBHostOptions" hidden={!values.GamepadUSBHostAddonEnabled}>
				<div className="alert alert-info" role="alert">
					{t('AddonsConfig:gamepad-usb-host-incompatible-hint')}
				</div>
			</div>
			{getAvailablePeripherals('usb') ? (
					<FormCheck
						label={t('Common:switch-enabled')}
						type="switch"
						id="GamepadUSBHostAddonButton"
						reverse
						isInvalid={false}
						checked={Boolean(values.GamepadUSBHostAddonEnabled)}
						onChange={(e) => {
							handleCheckbox('GamepadUSBHostAddonEnabled');
							handleChange(e);
						}}
					/>
				) : (
					<FormLabel>
						<Trans
							ns="PeripheralMapping"
							i18nKey="peripheral-toggle-unavailable"
							values={{ name: 'USB' }}
						>
							<NavLink to="/peripheral-mapping">
								{t('PeripheralMapping:header-text')}
							</NavLink>
						</Trans>
					</FormLabel>
				)}
		</Section>
	);
};

export default GamepadUSBHost;
