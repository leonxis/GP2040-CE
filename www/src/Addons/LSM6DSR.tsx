import { AppContext } from '../Contexts/AppContext';
import { useContext } from 'react';
import { useTranslation, Trans } from 'react-i18next';
import { NavLink } from 'react-router-dom';
import * as yup from 'yup';

import Section from '../Components/Section';
import FormCheck from 'react-bootstrap/FormCheck';
import FormLabel from 'react-bootstrap/FormLabel';
import { AddonPropTypes } from '../Pages/AddonsConfigPage';

export const LSM6DSR_OUTPUT_DS4 = 0;
export const LSM6DSR_OUTPUT_MOUSE = 3;

/** Used by AddonsConfig + HML calibration / motion forms (SPI block & CS are fixed in firmware). */
export const lsm6dsrScheme = {
	LSM6DSRAddonEnabled: yup.number().label('LSM6DSR Addon Enabled'),
	lsm6dsrOutputMode: yup.number().label('LSM6DSR Output Mode'),
	lsm6dsrOffsetGyroX: yup.number().label('LSM6DSR Offset Gyro X'),
	lsm6dsrOffsetGyroY: yup.number().label('LSM6DSR Offset Gyro Y'),
	lsm6dsrOffsetGyroZ: yup.number().label('LSM6DSR Offset Gyro Z'),
	lsm6dsrCalibrateGyroRequested: yup.number().label('LSM6DSR Calibrate Requested'),
	lsm6dsrEngageMode: yup.number().label('LSM6DSR Engage Mode'),
	lsm6dsrSpikeFilterEnabled: yup.number().label('LSM6DSR Spike Filter'),
	lsm6dsrOneEuroFilterEnabled: yup.number().label('LSM6DSR One Euro Filter'),
	lsm6dsrEngageKeys: yup.array().of(yup.number()).label('LSM6DSR Engage Keys'),
	lsm6dsrOffsetAccelX: yup.number().label('LSM6DSR Offset Accel X'),
	lsm6dsrOffsetAccelY: yup.number().label('LSM6DSR Offset Accel Y'),
	lsm6dsrOffsetAccelZ: yup.number().label('LSM6DSR Offset Accel Z'),
	lsm6dsrGyroMouseMapMode: yup.number().label('LSM6DSR Gyro Mouse Map Mode'),
	lsm6dsrGyroMouseInvert: yup.number().label('LSM6DSR Gyro Mouse Invert'),
	lsm6dsrGyroMouseSensLR: yup.number().label('LSM6DSR Gyro Mouse Sens LR'),
	lsm6dsrGyroMouseSensUD: yup.number().label('LSM6DSR Gyro Mouse Sens UD'),
	lsm6dsrGyroMouseDeadzone: yup.number().label('LSM6DSR Gyro Mouse Deadzone'),
};

export const lsm6dsrState = {
	LSM6DSRAddonEnabled: 0,
	lsm6dsrOutputMode: 0,
	lsm6dsrOffsetGyroX: 0,
	lsm6dsrOffsetGyroY: 0,
	lsm6dsrOffsetGyroZ: 0,
	lsm6dsrCalibrateGyroRequested: 0,
	lsm6dsrEngageMode: 0,
	lsm6dsrSpikeFilterEnabled: 0,
	lsm6dsrOneEuroFilterEnabled: 1,
	lsm6dsrEngageKeys: [],
	lsm6dsrOffsetAccelX: 0,
	lsm6dsrOffsetAccelY: 0,
	lsm6dsrOffsetAccelZ: 0,
	lsm6dsrGyroMouseMapMode: 0,
	lsm6dsrGyroMouseInvert: 0,
	lsm6dsrGyroMouseSensLR: 1,
	lsm6dsrGyroMouseSensUD: 1,
	lsm6dsrGyroMouseDeadzone: 12,
};

const LSM6DSR = ({ values, handleCheckbox }: AddonPropTypes) => {
	const { getAvailablePeripherals } = useContext(AppContext);
	const { t } = useTranslation();

	return (
		<Section title={t('AddonsConfig:lsm6dsr-header-text')}>
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
