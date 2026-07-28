import { AppContext } from '../Contexts/AppContext';
import { useContext } from 'react';
import { useTranslation, Trans } from 'react-i18next';
import { NavLink } from 'react-router-dom';
import * as yup from 'yup';

import Section from '../Components/Section';
import FormCheck from 'react-bootstrap/FormCheck';
import FormLabel from 'react-bootstrap/FormLabel';
import { AddonPropTypes } from '../Pages/AddonsConfigPage';

export const ads8332Scheme = {
	ADS8332AddonEnabled: yup.number().label('ADS8332 Addon Enabled'),
};

export const ads8332State = {
	ADS8332AddonEnabled: 0,
};

const ADS8332 = ({ values, handleCheckbox, setFieldValue }: AddonPropTypes) => {
	const { getAvailablePeripherals } = useContext(AppContext);
	const { t } = useTranslation();

	// ADS8332 和 MCP3208 互斥：启用 ADS8332 时自动禁用 MCP3208
	const handleADS8332Toggle = () => {
		handleCheckbox('ADS8332AddonEnabled');
		if (!values.ADS8332AddonEnabled && values.MCP3208AddonEnabled) {
			setFieldValue('MCP3208AddonEnabled', 0);
		}
	};

	return (
		<Section title={t('AddonsConfig:ads8332-header-text')}>
			{getAvailablePeripherals?.('spi') ? (
				<FormCheck
					label={t('Common:switch-enabled')}
					type="switch"
					id="ADS8332AddonEnabled"
					reverse
					isInvalid={false}
					checked={Boolean(values.ADS8332AddonEnabled) && getAvailablePeripherals('spi')}
					onChange={handleADS8332Toggle}
				/>
			) : (
				<FormLabel>
					<Trans
						ns="PeripheralMapping"
						i18nKey="peripheral-toggle-unavailable"
						values={{ name: 'SPI' }}
					>
						<NavLink to="/peripheral-mapping">{t('PeripheralMapping:header-text')}</NavLink>
					</Trans>
				</FormLabel>
			)}
		</Section>
	);
};

export default ADS8332;
