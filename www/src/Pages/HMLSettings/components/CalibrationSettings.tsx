import { useContext, useEffect, useState } from 'react';
import { Button, Form } from 'react-bootstrap';
import { Formik, FormikErrors, FormikHandlers, FormikHelpers, useFormikContext } from 'formik';
import * as yup from 'yup';
import { useTranslation } from 'react-i18next';
import get from 'lodash/get';
import set from 'lodash/set';

import { AppContext } from '../../../Contexts/AppContext';
import { hexToInt } from '../../../Services/Utilities';
import WebApi from '../../../Services/WebApi';
import Section from '../../../Components/Section';
import JoystickCalibration from './JoystickCalibration';
import JoystickCurveSettings from './JoystickCurveSettings';
import Analog, { analogScheme, analogState } from '../../../Addons/Analog';
import Analog1256, {
	analog1256Scheme,
	analog1256State,
} from '../../../Addons/Analog1256';
import Bootsel, { bootselScheme, bootselState } from '../../../Addons/Bootsel';
import Buzzer, { buzzerScheme, buzzerState } from '../../../Addons/Buzzer';
import DualDirection, {
	dualDirectionScheme,
	dualDirectionState,
} from '../../../Addons/DualDirection';
import I2CAnalog1219, {
	i2cAnalogScheme,
	i2cAnalogState,
} from '../../../Addons/I2CAnalog1219';
import OnBoardLed, {
	onBoardLedScheme,
	onBoardLedState,
} from '../../../Addons/OnBoardLed';
import Reverse, { reverseScheme, reverseState } from '../../../Addons/Reverse';
import SOCD, { socdScheme, socdState } from '../../../Addons/SOCD';
import Tilt, { tiltScheme, tiltState } from '../../../Addons/Tilt';
import Turbo, { turboScheme, turboState } from '../../../Addons/Turbo';
import Wii, { wiiScheme, wiiState } from '../../../Addons/Wii';
import SNES, { snesState } from '../../../Addons/SNES';
import FocusMode, {
	focusModeScheme,
	focusModeState,
} from '../../../Addons/FocusMode';
import Keyboard, { keyboardScheme, keyboardState } from '../../../Addons/Keyboard';
import GamepadUSBHost, {
	gamepadUSBHostScheme,
	gamepadUSBHostState,
} from '../../../Addons/GamepadUSBHost';
import Rotary, { rotaryScheme, rotaryState } from '../../../Addons/Rotary';
import PCF8575, { pcf8575Scheme, pcf8575State } from '../../../Addons/PCF8575';
import DRV8833Rumble, {
	drv8833RumbleScheme,
	drv8833RumbleState,
} from '../../../Addons/DRV8833';
import ReactiveLED, {
	reactiveLEDScheme,
	reactiveLEDState,
} from '../../../Addons/ReactiveLED';
import TG16, { tg16State } from '../../../Addons/TG16';
import HETrigger, {
	HETriggerScheme,
	HETriggerState,
} from '../../../Addons/HETrigger';

export type AddonPropTypes = {
	values: typeof DEFAULT_VALUES;
	errors: FormikErrors<typeof DEFAULT_VALUES>;
	handleChange: FormikHandlers['handleChange'];
	handleCheckbox: (name: keyof typeof DEFAULT_VALUES) => void;
	setFieldValue: FormikHelpers<typeof DEFAULT_VALUES>['setFieldValue'];
};

const schema = yup.object().shape({
	...analogScheme,
	...analog1256Scheme,
	...bootselScheme,
	...onBoardLedScheme,
	...turboScheme,
	...reverseScheme,
	...i2cAnalogScheme,
	...dualDirectionScheme,
	...tiltScheme,
	...buzzerScheme,
	...socdScheme,
	...wiiScheme,
	...focusModeScheme,
	...keyboardScheme,
	...gamepadUSBHostScheme,
	...rotaryScheme,
	...pcf8575Scheme,
	...drv8833RumbleScheme,
	...reactiveLEDScheme,
	...HETriggerScheme,
});

export const DEFAULT_VALUES = {
	...analogState,
	...analog1256State,
	...bootselState,
	...onBoardLedState,
	...turboState,
	...reverseState,
	...i2cAnalogState,
	...dualDirectionState,
	...tiltState,
	...buzzerState,
	...socdState,
	...wiiState,
	...snesState,
	...tg16State,
	...focusModeState,
	...keyboardState,
	...rotaryState,
	...pcf8575State,
	...drv8833RumbleState,
	...reactiveLEDState,
	...gamepadUSBHostState,
	...HETriggerState,
} as const;

const FormContext = ({ setStoredData }) => {
	const { values, setValues } = useFormikContext();
	const { setLoading } = useContext(AppContext);

	useEffect(() => {
		async function fetchData() {
			const data = await WebApi.getAddonsOptions(setLoading);

			setValues(data);
			setStoredData(JSON.parse(JSON.stringify(data))); // Do a deep copy to keep the original
		}
		fetchData();
	}, [setValues]);

	useEffect(() => {
		sanitizeData(values);
	}, [values, setValues]);

	return null;
};

const sanitizeData = (values) => {
	for (const prop in Object.keys(values).filter(
		(key) => !!!key.includes('keyboardHostMap'),
	)) {
		// Skip arrays - don't convert them to integers
		if (Array.isArray(values[prop])) {
			continue;
		}
		if (!!values[prop]) values[prop] = parseInt(values[prop]);
	}
};

function flattenObject(object) {
	var toReturn = {};

	for (var i in object) {
		if (!object.hasOwnProperty(i)) continue;

		// Handle arrays - keep them as arrays, don't flatten
		if (Array.isArray(object[i])) {
			toReturn[i] = object[i];
		} else if (typeof object[i] == 'object' && object[i] !== null) {
			var flatObject = flattenObject(object[i]);
			for (var x in flatObject) {
				if (!flatObject.hasOwnProperty(x)) continue;

				toReturn[i + '.' + x] = flatObject[x];
			}
		} else {
			toReturn[i] = object[i];
		}
	}
	return toReturn;
}

export default function CalibrationSettings() {
	const { updateUsedPins } = useContext(AppContext);
	const [saveMessage, setSaveMessage] = useState('');
	const [storedData, setStoredData] = useState({});

	const { t } = useTranslation();

	const onSuccess = async (values: typeof DEFAULT_VALUES) => {
		const flattened = flattenObject(storedData);

		// Convert turbo LED color if available
		const data = {
			...flattened,
			...flattenObject(values),
		};

		if (data.turboLedColor && typeof data.turboLedColor === 'string') {
			data.turboLedColor = hexToInt(data.turboLedColor);
		}

		// Handle array fields specially - only update if changed
		const valuesSchema = flattenObject(values);
		const arrayFields = ['joystickRangeData1', 'joystickRangeData2', 'joystickCurvePoints1', 'joystickCurvePoints2', 'joystickCurvePresets'];
		const resultObject = { ...data };
		arrayFields.forEach(field => {
			const newVal = get(valuesSchema, field);
			const oldVal = get(flattened, field);
			if (Array.isArray(newVal) && (!Array.isArray(oldVal) || JSON.stringify(newVal) !== JSON.stringify(oldVal))) {
				set(resultObject, field, newVal);
			}
		});
		sanitizeData(resultObject);
		const success = await WebApi.setAddonsOptions(resultObject);
		setStoredData(JSON.parse(JSON.stringify(values))); // Update to reflect saved data
		setSaveMessage(
			success
				? t('Common:saved-success-message')
				: t('Common:saved-error-message'),
		);
		if (success) updateUsedPins();
	};

	return (
		<div>
			<Formik
			enableReinitialize={true}
			validationSchema={schema}
			onSubmit={onSuccess}
			initialValues={DEFAULT_VALUES}
		>
			{({ handleSubmit, handleChange, values, errors, setFieldValue }) => (
				<Form noValidate onSubmit={handleSubmit}>
					<JoystickCalibration
						values={values}
						errors={errors}
						handleChange={handleChange}
						handleCheckbox={(name: keyof typeof DEFAULT_VALUES) => {
							setFieldValue(name, values[name] === 1 ? 0 : 1);
						}}
						setFieldValue={setFieldValue}
					/>

					<JoystickCurveSettings
						values={values}
						errors={errors}
						handleChange={handleChange}
						setFieldValue={setFieldValue}
					/>

					<Section title="扳机校准">
						{/* 扳机校准内容占位 */}
					</Section>

					<FormContext setStoredData={setStoredData} />
				</Form>
			)}
		</Formik>
		</div>
	);
}
