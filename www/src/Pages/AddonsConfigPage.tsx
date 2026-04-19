import { useContext, useEffect, useState } from 'react';
import { Button, Form } from 'react-bootstrap';
import { Formik, FormikErrors, FormikHandlers, FormikHelpers, useFormikContext } from 'formik';
import * as yup from 'yup';
import { useTranslation } from 'react-i18next';

import get from 'lodash/get';
import set from 'lodash/set';

import { AppContext } from '../Contexts/AppContext';

import { hexToInt } from '../Services/Utilities';

import WebApi from '../Services/WebApi';
import Analog, { analogScheme, analogState } from '../Addons/Analog';
import ADS8332, { ads8332Scheme, ads8332State } from '../Addons/ADS8332';
import LSM6DSR, { lsm6dsrScheme, lsm6dsrState } from '../Addons/LSM6DSR';
import Bootsel, { bootselScheme, bootselState } from '../Addons/Bootsel';
import Buzzer, { buzzerScheme, buzzerState } from '../Addons/Buzzer';
import DualDirection, {
	dualDirectionScheme,
	dualDirectionState,
} from '../Addons/DualDirection';
import OnBoardLed, {
	onBoardLedScheme,
	onBoardLedState,
} from '../Addons/OnBoardLed';
import Reverse, { reverseScheme, reverseState } from '../Addons/Reverse';
import SOCD, { socdScheme, socdState } from '../Addons/SOCD';
import Tilt, { tiltScheme, tiltState } from '../Addons/Tilt';
import Turbo, { turboScheme, turboState } from '../Addons/Turbo';
import Wii, { wiiScheme, wiiState } from '../Addons/Wii';
import SNES, { snesState } from '../Addons/SNES';
import FocusMode, {
	focusModeScheme,
	focusModeState,
} from '../Addons/FocusMode';
import Keyboard, { keyboardScheme, keyboardState } from '../Addons/Keyboard';
import GamepadUSBHost, {
	gamepadUSBHostScheme,
	gamepadUSBHostState,
} from '../Addons/GamepadUSBHost';
import Rotary, { rotaryScheme, rotaryState } from '../Addons/Rotary';
import PCF8575, { pcf8575Scheme, pcf8575State } from '../Addons/PCF8575';
import DRV8833Rumble, {
	drv8833RumbleScheme,
	drv8833RumbleState,
} from '../Addons/DRV8833';
import ReactiveLED, {
	reactiveLEDScheme,
	reactiveLEDState,
} from '../Addons/ReactiveLED';
import TG16, { tg16State } from '../Addons/TG16';
import HETrigger, {
	HETriggerScheme,
	HETriggerState,
} from '../Addons/HETrigger';

export type AddonPropTypes = {
	values: typeof DEFAULT_VALUES;
	errors: FormikErrors<typeof DEFAULT_VALUES>;
	handleChange: FormikHandlers['handleChange'];
	handleCheckbox: (name: keyof typeof DEFAULT_VALUES) => void;
	setFieldValue: FormikHelpers<typeof DEFAULT_VALUES>['setFieldValue'];
};

const schema = yup.object().shape({
	...analogScheme,
	...ads8332Scheme,
	...lsm6dsrScheme,
	...bootselScheme,
	...onBoardLedScheme,
	...turboScheme,
	...reverseScheme,
	...dualDirectionScheme,
	...tiltScheme,
	...buzzerScheme,
	...socdScheme,
	...wiiScheme,
	...focusModeScheme,
	...keyboardScheme,
	...rotaryScheme,
	...pcf8575Scheme,
	...drv8833RumbleScheme,
	...reactiveLEDScheme,
	...gamepadUSBHostScheme,
	...HETriggerScheme,
});

export const DEFAULT_VALUES = {
	...analogState,
	...ads8332State,
	...lsm6dsrState,
	...bootselState,
	...onBoardLedState,
	...turboState,
	...reverseState,
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

const ADDONS = [
	Bootsel,
	OnBoardLed,
	Analog,
	Turbo,
	Reverse,
	ADS8332,
	LSM6DSR,
	DualDirection,
	Tilt,
	Buzzer,
	SOCD,
	Wii,
	SNES,
	TG16,
	FocusMode,
	Keyboard,
	GamepadUSBHost,
	Rotary,
	PCF8575,
	DRV8833Rumble,
	ReactiveLED,
	HETrigger,
];

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
	const keys = Object.keys(values).filter(
		(key) => !key.includes('keyboardHostMap'),
	);
	for (const prop of keys) {
		// Skip arrays - don't convert them to integers
		if (Array.isArray(values[prop])) {
			continue;
		}
		if (values[prop] !== undefined && values[prop] !== null && values[prop] !== '') {
			const parsed = parseInt(values[prop], 10);
			if (!Number.isNaN(parsed)) {
				values[prop] = parsed;
			}
		}
	}
};

function flattenObject(object) {
	const toReturn = {};

	for (const i in object) {
		if (!object.hasOwnProperty(i)) continue;

		// Handle arrays - keep them as arrays, don't flatten
		if (Array.isArray(object[i])) {
			toReturn[i] = object[i];
		} else if (typeof object[i] == 'object' && object[i] !== null) {
			const flatObject = flattenObject(object[i]);
			for (const x in flatObject) {
				if (!flatObject.hasOwnProperty(x)) continue;

				toReturn[i + '.' + x] = flatObject[x];
			}
		} else {
			toReturn[i] = object[i];
		}
	}
	return toReturn;
}

export default function AddonsConfigPage() {
	const { updateUsedPins, updatePeripherals } = useContext(AppContext);
	const [saveMessage, setSaveMessage] = useState('');
	const [storedData, setStoredData] = useState({});

	const { t } = useTranslation();

	useEffect(() => {
		updatePeripherals();
	}, []);

	const onSuccess = async (values: typeof DEFAULT_VALUES) => {
		const flattened = flattenObject(storedData);

		// Convert turbo LED color if available
		const data = {
			...values,
			turboLedColor: hexToInt(values.turboLedColor || '#000000'),
		};
		const valuesSchema = schema.cast(data); // Strip invalid values

		// Compare what's changed and set it to resultObject
		const resultObject = {};
		Object.entries(flattened)?.map((entry) => {
			const [key, oldVal] = entry;
			const newVal = get(valuesSchema, key);
			// For arrays, use deep comparison
			if (Array.isArray(newVal) && Array.isArray(oldVal)) {
				if (JSON.stringify(newVal) !== JSON.stringify(oldVal)) {
					set(resultObject, key, newVal);
				}
			} else if (newVal !== oldVal) {
				set(resultObject, key, newVal);
			}
		});
		// Also check for array fields that might not be in flattened (if they were empty before)
		const arrayFields = ['joystickRangeData1', 'joystickRangeData2', 'joystickCurvePoints1', 'joystickCurvePoints2'];
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
		<Formik
			enableReinitialize={true}
			validationSchema={schema}
			onSubmit={onSuccess}
			initialValues={DEFAULT_VALUES}
		>
			{({ handleSubmit, handleChange, values, errors, setFieldValue }) => (
				<Form noValidate onSubmit={handleSubmit}>
					<h1>{t('AddonsConfig:header-text')}</h1>
					<p>{t('AddonsConfig:sub-header-text')}</p>
					{ADDONS.map((Addon, index) => (
						<Addon
							key={`addon-${index}`}
							values={values}
							errors={errors}
							handleChange={handleChange}
							handleCheckbox={(name: keyof typeof DEFAULT_VALUES) => {
								// Support both number (0/1) and boolean from API so toggle works
								const on = values[name] === 1 || values[name] === true;
								setFieldValue(name, on ? 0 : 1);
							}}
							setFieldValue={setFieldValue}
						/>
					))}

					<div className="mt-3">
						<Button type="submit" id="save">
							{t('Common:button-save-label')}
						</Button>
						{saveMessage ? <span className="alert">{saveMessage}</span> : null}
					</div>
					<FormContext setStoredData={setStoredData} />
				</Form>
			)}
		</Formik>
	);
}
