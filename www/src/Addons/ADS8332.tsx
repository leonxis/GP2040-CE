import { AppContext } from '../Contexts/AppContext';
import { useContext, useEffect, useState } from 'react';
import { useTranslation, Trans } from 'react-i18next';
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

export const ads8332Scheme = {
	ADS8332AddonEnabled: yup.number().label('ADS8332 Addon Enabled'),
	ads8332Block: yup
		.number()
		.label('ADS8332 SPI Block')
		.validateSelectionWhenValue('ADS8332AddonEnabled', SPI_BLOCKS),
	ads8332CsPin: yup.number().label('ADS8332 CS Pin'),
	ads8332ConvstPin: yup.number().label('ADS8332 CONVST Pin'),
};

export const ads8332State = {
	ADS8332AddonEnabled: 0,
	ads8332Block: 0,
	ads8332CsPin: -1,
	ads8332ConvstPin: -1,
};

const ADS8332 = ({ values, errors, handleChange, handleCheckbox }: AddonPropTypes) => {
	const { getAvailablePeripherals, setLoading, usedPins } = useContext(AppContext);
	const [gpioPins, setGpioPins] = useState<Array<{ pin: number; hwcs: boolean }>>([]);
	const { t } = useTranslation();

	const getAvailablePins = async (spiBlock: number) => {
		const pins: Array<{ pin: number; hwcs: boolean }> = [];
		const peripheralOptions = await WebApi.getPeripheralOptions(setLoading);
		if (
			peripheralOptions?.peripheral?.[`spi${spiBlock}`] &&
			peripheralOptions.peripheral[`spi${spiBlock}`].cs > -1
		) {
			pins.push({
				pin: peripheralOptions.peripheral[`spi${spiBlock}`].cs,
				hwcs: true,
			});
		}
		const availablePins = [
			...Array(boards[import.meta.env.VITE_GP2040_BOARD].maxPin + 1).keys(),
		].filter((p) => (usedPins || []).indexOf(p) === -1);
		pins.push(...availablePins.map((pin) => ({ pin, hwcs: false })));
		return pins;
	};

	useEffect(() => {
		async function fetchData() {
			const pins = await getAvailablePins(values.ads8332Block ?? 0);
			setGpioPins(pins);
		}
		fetchData();
	}, [values.ads8332Block, usedPins]);

	return (
		<Section title={t('AddonsConfig:ads8332-header-text')}>
			<div
				id="ADS8332InputOptions"
				hidden={!(values.ADS8332AddonEnabled && getAvailablePeripherals?.('spi'))}
			>
				<div className="alert alert-info" role="alert">
					{t('AddonsConfig:ads8332-peripheral-note')}{' '}
					<NavLink to="/peripheral-mapping" className="alert-link">
						{t('PeripheralMapping:header-text')}
					</NavLink>
				</div>
				<Row className="mb-3">
					{getAvailablePeripherals?.('spi') ? (
						<FormSelect
							label={t('AddonsConfig:ads8332-block-label')}
							name="ads8332Block"
							className="form-select-sm"
							groupClassName="col-sm-3 mb-3"
							value={values.ads8332Block ?? 0}
							error={errors.ads8332Block}
							isInvalid={Boolean(errors.ads8332Block)}
							onChange={handleChange}
						>
							{getAvailablePeripherals('spi').map((o, i) => (
								<option key={`ads8332-spi-option-${i}`} value={o.value}>
									{o.label}
								</option>
							))}
						</FormSelect>
					) : null}
					<FormSelect
						label={t('AddonsConfig:ads8332-cs-pin')}
						name="ads8332CsPin"
						className="form-select-sm"
						groupClassName="col-sm-3 mb-3"
						value={values.ads8332CsPin ?? -1}
						error={errors.ads8332CsPin}
						isInvalid={Boolean(errors.ads8332CsPin)}
						onChange={handleChange}
					>
						{gpioPins.map((p, i) => (
							<option key={`ads8332-cs-${i}`} value={p.pin}>
								{p.pin}
								{p.hwcs ? ' (HW)' : ''}
							</option>
						))}
					</FormSelect>
					<FormSelect
						label={t('AddonsConfig:ads8332-convst-pin')}
						name="ads8332ConvstPin"
						className="form-select-sm"
						groupClassName="col-sm-3 mb-3"
						value={values.ads8332ConvstPin ?? -1}
						error={errors.ads8332ConvstPin}
						isInvalid={Boolean(errors.ads8332ConvstPin)}
						onChange={handleChange}
					>
						{gpioPins.map((p, i) => (
							<option key={`ads8332-convst-${i}`} value={p.pin}>
								{p.pin}
							</option>
						))}
					</FormSelect>
				</Row>
			</div>
			{getAvailablePeripherals?.('spi') ? (
				<FormCheck
					label={t('Common:switch-enabled')}
					type="switch"
					id="ADS8332AddonEnabled"
					reverse
					isInvalid={false}
					checked={Boolean(values.ADS8332AddonEnabled) && getAvailablePeripherals('spi')}
					onChange={() => {
						handleCheckbox('ADS8332AddonEnabled');
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

export default ADS8332;
