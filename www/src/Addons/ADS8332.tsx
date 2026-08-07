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

const ADS8332 = ({ values, setFieldValue }: AddonPropTypes) => {
	const { getAvailablePeripherals } = useContext(AppContext);
	const { t } = useTranslation();

	const handleADS8332Toggle = () => {
                const ads8332Enabled = !Boolean(values.ADS8332AddonEnabled);
                setFieldValue('ADS8332AddonEnabled', ads8332Enabled ? 1 : 0);
                setFieldValue('MCP3208AddonEnabled', ads8332Enabled ? 0 : 1);
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
