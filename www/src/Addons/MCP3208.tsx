import { AppContext } from '../Contexts/AppContext';
import { useContext, useEffect, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { NavLink } from 'react-router-dom';
import * as yup from 'yup';

import Section from '../Components/Section';
import FormSelect from '../Components/FormSelect';
import FormCheck from 'react-bootstrap/FormCheck';
import FormLabel from 'react-bootstrap/FormLabel';
import Row from 'react-bootstrap/Row';
import boards from '../Data/Boards.json';
import { SPI_BLOCKS } from '../Data/Peripherals';
import WebApi from '../Services/WebApi';
import { AddonPropTypes } from '../Pages/AddonsConfigPage';
import { Trans } from 'react-i18next';

export const mcp3208Scheme = {
	MCP3208AddonEnabled: yup.number().label('MCP3208 Addon Enabled'),
	mcp3208Block: yup
		.number()
		.label('MCP3208 SPI Block')
		.validateSelectionWhenValue('MCP3208AddonEnabled', SPI_BLOCKS),
	mcp3208CsPin: yup.number().label('MCP3208 CS Pin'),
};

export const mcp3208State = {
	MCP3208AddonEnabled: 0,
	mcp3208Block: 0,
	mcp3208CsPin: -1,
};

const MCP3208 = ({ values, errors, handleChange, handleCheckbox }: AddonPropTypes) => {
	const { getAvailablePeripherals, setLoading, usedPins } = useContext(AppContext);
	const [csPins, setCsPins] = useState<Array<{ pin: number; hwcs: boolean }>>([]);
	const { t } = useTranslation();

	const handlePeripheralChange = (e) => {
		handleChange(e);
	};

	const getAvailableCsPins = async (spiBlock: number) => {
		const csPins: Array<{ pin: number; hwcs: boolean }> = [];
		const peripheralOptions = await WebApi.getPeripheralOptions(setLoading);
		if (
			peripheralOptions?.peripheral?.[`spi${spiBlock}`] &&
			peripheralOptions.peripheral[`spi${spiBlock}`].cs > -1
		) {
			csPins.push({
				pin: peripheralOptions.peripheral[`spi${spiBlock}`].cs,
				hwcs: true,
			});
		}
		const availablePins = [
			...Array(boards[import.meta.env.VITE_GP2040_BOARD].maxPin + 1).keys(),
		].filter((p) => (usedPins || []).indexOf(p) === -1);
		csPins.push(...availablePins.map((pin) => ({ pin, hwcs: false })));
		return csPins;
	};

	useEffect(() => {
		async function fetchData() {
			const pins = await getAvailableCsPins(values.mcp3208Block ?? 0);
			setCsPins(pins);
		}
		fetchData();
	}, [values.mcp3208Block, usedPins]);

	return (
		<Section title={t('AddonsConfig:mcp3208-header-text')}>
			<div
				id="MCP3208InputOptions"
				hidden={!(values.MCP3208AddonEnabled && getAvailablePeripherals?.('spi'))}
			>
				<div className="alert alert-info" role="alert">
					{t('AddonsConfig:mcp3208-peripheral-note')}{' '}
					<NavLink to="/peripheral-mapping" className="alert-link">
						{t('PeripheralMapping:header-text')}
					</NavLink>
				</div>
				<Row className="mb-3">
					{getAvailablePeripherals?.('spi') ? (
						<FormSelect
							label={t('AddonsConfig:mcp3208-block-label')}
							name="mcp3208Block"
							className="form-select-sm"
							groupClassName="col-sm-3 mb-3"
							value={values.mcp3208Block ?? 0}
							error={errors.mcp3208Block}
							isInvalid={Boolean(errors.mcp3208Block)}
							onChange={handlePeripheralChange}
						>
							{getAvailablePeripherals('spi').map((o, i) => (
								<option key={`mcp3208-spi-option-${i}`} value={o.value}>
									{o.label}
								</option>
							))}
						</FormSelect>
					) : null}
					<FormSelect
						label={t('AddonsConfig:mcp3208-cs-pin')}
						name="mcp3208CsPin"
						className="form-select-sm"
						groupClassName="col-sm-3 mb-3"
						value={values.mcp3208CsPin ?? -1}
						error={errors.mcp3208CsPin}
						isInvalid={Boolean(errors.mcp3208CsPin)}
						onChange={handleChange}
					>
						{csPins.map((p, i) => (
							<option key={`mcp3208-cs-${i}`} value={p.pin}>
								{p.pin}
								{p.hwcs ? ' (HW)' : ''}
							</option>
						))}
					</FormSelect>
				</Row>
			</div>
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
					onChange={() => {
						handleCheckbox('MCP3208AddonEnabled');
					}}
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

export default MCP3208;
