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

export const LSM6DSR_OUTPUT_DS4 = 0;
export const LSM6DSR_OUTPUT_DS4_STICK = 1;
export const LSM6DSR_OUTPUT_XBOX_STICK = 2;
export const LSM6DSR_OUTPUT_MOUSE = 3;

export const lsm6dsrScheme = {
	LSM6DSRAddonEnabled: yup.number().label('LSM6DSR Addon Enabled'),
	lsm6dsrBlock: yup
		.number()
		.label('LSM6DSR SPI Block')
		.validateSelectionWhenValue('LSM6DSRAddonEnabled', SPI_BLOCKS),
	lsm6dsrCsPin: yup.number().label('LSM6DSR CS Pin'),
	lsm6dsrOutputMode: yup.number().label('LSM6DSR Output Mode'),
	lsm6dsrOutputStick: yup.number().label('LSM6DSR Output Stick'),
	lsm6dsrOffsetGyroX: yup.number().label('LSM6DSR Offset Gyro X'),
	lsm6dsrOffsetGyroY: yup.number().label('LSM6DSR Offset Gyro Y'),
	lsm6dsrOffsetGyroZ: yup.number().label('LSM6DSR Offset Gyro Z'),
	lsm6dsrCalibrateGyroRequested: yup.number().label('LSM6DSR Calibrate Requested'),
	lsm6dsrEngageMode: yup.number().label('LSM6DSR Engage Mode'),
	lsm6dsrEngageKeys: yup.array().of(yup.number()).label('LSM6DSR Engage Keys'),
};

export const lsm6dsrState = {
	LSM6DSRAddonEnabled: 0,
	lsm6dsrBlock: 0,
	lsm6dsrCsPin: -1,
	lsm6dsrOutputMode: 0,
	lsm6dsrOutputStick: 1,
	lsm6dsrOffsetGyroX: 0,
	lsm6dsrOffsetGyroY: 0,
	lsm6dsrOffsetGyroZ: 0,
	lsm6dsrCalibrateGyroRequested: 0,
	lsm6dsrEngageMode: 0,
	lsm6dsrEngageKeys: [],
};

const LSM6DSR = ({ values, errors, handleChange, handleCheckbox }: AddonPropTypes) => {
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
			const pins = await getAvailableCsPins(values.lsm6dsrBlock ?? 0);
			setCsPins(pins);
		}
		fetchData();
	}, [values.lsm6dsrBlock, usedPins]);

	return (
		<Section title={t('AddonsConfig:lsm6dsr-header-text')}>
			<div
				id="LSM6DSRInputOptions"
				hidden={!(values.LSM6DSRAddonEnabled && getAvailablePeripherals?.('spi'))}
			>
				<div className="alert alert-info" role="alert">
					{t('AddonsConfig:lsm6dsr-peripheral-note')}{' '}
					<NavLink to="/peripheral-mapping" className="alert-link">
						{t('PeripheralMapping:header-text')}
					</NavLink>
				</div>
				<Row className="mb-3">
					{getAvailablePeripherals?.('spi') ? (
						<FormSelect
							label={t('AddonsConfig:lsm6dsr-block-label')}
							name="lsm6dsrBlock"
							className="form-select-sm"
							groupClassName="col-sm-3 mb-3"
							value={values.lsm6dsrBlock ?? 0}
							error={errors.lsm6dsrBlock}
							isInvalid={Boolean(errors.lsm6dsrBlock)}
							onChange={handlePeripheralChange}
						>
							{getAvailablePeripherals('spi').map((o, i) => (
								<option key={`lsm6dsr-spi-option-${i}`} value={o.value}>
									{o.label}
								</option>
							))}
						</FormSelect>
					) : null}
					<FormSelect
						label={t('AddonsConfig:lsm6dsr-cs-pin')}
						name="lsm6dsrCsPin"
						className="form-select-sm"
						groupClassName="col-sm-3 mb-3"
						value={values.lsm6dsrCsPin ?? -1}
						error={errors.lsm6dsrCsPin}
						isInvalid={Boolean(errors.lsm6dsrCsPin)}
						onChange={handleChange}
					>
						{csPins.map((p, i) => (
							<option key={`lsm6dsr-cs-${i}`} value={p.pin}>
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
					id="LSM6DSRAddonEnabled"
					reverse
					isInvalid={false}
					checked={
						Boolean(values.LSM6DSRAddonEnabled) && getAvailablePeripherals('spi')
					}
					onChange={() => {
						handleCheckbox('LSM6DSRAddonEnabled');
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

export default LSM6DSR;
