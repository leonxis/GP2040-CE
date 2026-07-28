import { AppContext } from '../Contexts/AppContext';
import { useContext } from 'react';
import { useTranslation } from 'react-i18next';
import * as yup from 'yup';

import Section from '../Components/Section';
import FormCheck from 'react-bootstrap/FormCheck';
import FormLabel from 'react-bootstrap/FormLabel';
import { AddonPropTypes } from '../Pages/AddonsConfigPage';

export const mcp3208Scheme = {
	MCP3208AddonEnabled: yup.number().label('MCP3208 Addon Enabled'),
};

export const mcp3208State = {
	MCP3208AddonEnabled: 0,
};

const MCP3208 = ({ values, errors, handleCheckbox, setFieldValue }: AddonPropTypes) => {
	const { getAvailablePeripherals } = useContext(AppContext);
	const { t } = useTranslation();

	// MCP3208 和 ADS8332 互斥：启用 MCP3208 时自动禁用 ADS8332
	const handleMCP3208Toggle = () => {
		handleCheckbox('MCP3208AddonEnabled');
		if (!values.MCP3208AddonEnabled && values.ADS8332AddonEnabled) {
			setFieldValue('ADS8332AddonEnabled', 0);
		}
	};

	return (
		<Section title={t('AddonsConfig:mcp3208-header-text')}>
			{getAvailablePeripherals?.('spi') ? (
				<FormCheck
					label={t('Common:switch-enabled')}
					type="switch"
					id="MCP3208AddonEnabled"
					reverse
					isInvalid={false}
					checked={
						Boolean(values.MCP3208AddonEnabled) && getAvailablePeripherals('spi')
					}
					onChange={handleMCP3208Toggle}
				/>
			) : (
				<FormLabel disabled={true}>{t('AddonsConfig:spi-peripheral-note')}</FormLabel>
			)}
		</Section>
	);
};

export default MCP3208;