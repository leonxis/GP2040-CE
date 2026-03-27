import { useContext, useEffect, useRef, useState } from 'react';
import { Button, Form, Modal } from 'react-bootstrap';
import { Formik, FormikErrors, FormikHandlers, FormikHelpers, useFormikContext } from 'formik';
import * as yup from 'yup';
import { useTranslation } from 'react-i18next';
import get from 'lodash/get';
import set from 'lodash/set';

import { AppContext } from '../../../Contexts/AppContext';
import { hexToInt } from '../../../Services/Utilities';
import WebApi from '../../../Services/WebApi';
import JoystickCalibration from './JoystickCalibration';
import JoystickCurveSettings from './JoystickCurveSettings';
import { analogScheme, analogState } from '../../../Addons/Analog';
import {
	analog1256Scheme,
	analog1256State,
} from '../../../Addons/Analog1256';
import { mcp3208Scheme, mcp3208State } from '../../../Addons/MCP3208';
import { lsm6dsrScheme, lsm6dsrState } from '../../../Addons/LSM6DSR';
import { bootselScheme, bootselState } from '../../../Addons/Bootsel';
import { buzzerScheme, buzzerState } from '../../../Addons/Buzzer';
import {
	dualDirectionScheme,
	dualDirectionState,
} from '../../../Addons/DualDirection';
import {
	i2cAnalogScheme,
	i2cAnalogState,
} from '../../../Addons/I2CAnalog1219';
import {
	onBoardLedScheme,
	onBoardLedState,
} from '../../../Addons/OnBoardLed';
import { reverseScheme, reverseState } from '../../../Addons/Reverse';
import { socdScheme, socdState } from '../../../Addons/SOCD';
import { tiltScheme, tiltState } from '../../../Addons/Tilt';
import { turboScheme, turboState } from '../../../Addons/Turbo';
import { wiiScheme, wiiState } from '../../../Addons/Wii';
import { snesState } from '../../../Addons/SNES';
import {
	focusModeScheme,
	focusModeState,
} from '../../../Addons/FocusMode';
import { keyboardScheme, keyboardState } from '../../../Addons/Keyboard';
import {
	gamepadUSBHostScheme,
	gamepadUSBHostState,
} from '../../../Addons/GamepadUSBHost';
import { rotaryScheme, rotaryState } from '../../../Addons/Rotary';
import { pcf8575Scheme, pcf8575State } from '../../../Addons/PCF8575';
import {
	drv8833RumbleScheme,
	drv8833RumbleState,
} from '../../../Addons/DRV8833';
import {
	reactiveLEDScheme,
	reactiveLEDState,
} from '../../../Addons/ReactiveLED';
import { tg16State } from '../../../Addons/TG16';
import {
	HETriggerScheme,
	HETriggerState,
} from '../../../Addons/HETrigger';
import TriggerCalibrationSettings, {
	triggerCalibrationScheme,
	triggerCalibrationState,
} from './TriggerCalibration';
export type AddonPropTypes = {
	values: typeof DEFAULT_VALUES;
	errors: FormikErrors<typeof DEFAULT_VALUES>;
	handleChange: FormikHandlers['handleChange'];
	handleCheckbox: (name: keyof typeof DEFAULT_VALUES) => void;
	setFieldValue: FormikHelpers<typeof DEFAULT_VALUES>['setFieldValue'];
	saveMessage?: string;
	onSaveClick?: () => void;
};

export const schema = yup.object().shape({
	...analogScheme,
	...analog1256Scheme,
	...mcp3208Scheme,
	...lsm6dsrScheme,
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
	...triggerCalibrationScheme,
});

export const DEFAULT_VALUES = {
	...analogState,
	...analog1256State,
	...mcp3208State,
	...lsm6dsrState,
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
	...triggerCalibrationState,
} as const;

export const FormContext = ({ setStoredData }) => {
	const { values, setValues } = useFormikContext();
	const { setLoading } = useContext(AppContext);

	useEffect(() => {
		async function fetchData() {
			const data = await WebApi.getAddonsOptions(setLoading);
			// 合并默认值，避免 API 未返回的字段（如扳机校准）丢失默认配置
			const merged = { ...DEFAULT_VALUES, ...data };
			setValues(merged);
			setStoredData(JSON.parse(JSON.stringify(merged)));
		}
		fetchData();
	}, [setValues]);

	useEffect(() => {
		sanitizeData(values);
	}, [values, setValues]);

	return null;
};

// 需要保留小数的字段（如外圈放大系数 0.1% 步长），不做 parseInt
const FLOAT_KEYS = [
	'joystickFinetuneShapeAmplify1',
	'joystickFinetuneShapeAmplify2',
	'lsm6dsrGyroMouseSensLR',
	'lsm6dsrGyroMouseSensUD',
];

export const sanitizeData = (values) => {
	const keys = Object.keys(values).filter(
		(key) => !key.includes('keyboardHostMap'),
	);
	for (const prop of keys) {
		// Skip arrays - don't convert them to integers
		if (Array.isArray(values[prop])) {
			continue;
		}
		if (values[prop] !== undefined && values[prop] !== null && values[prop] !== '') {
			if (FLOAT_KEYS.includes(prop)) {
				const parsed = parseFloat(values[prop]);
				if (!Number.isNaN(parsed)) {
					values[prop] = parsed;
				}
			} else {
				const parsed = parseInt(values[prop], 10);
				if (!Number.isNaN(parsed)) {
					values[prop] = parsed;
				}
			}
		}
	}
};

export function flattenObject(object) {
	const toReturn = {};

	for (const i in object) {
		if (!Object.prototype.hasOwnProperty.call(object, i)) continue;

		// Handle arrays - keep them as arrays, don't flatten
		if (Array.isArray(object[i])) {
			toReturn[i] = object[i];
		} else if (typeof object[i] == 'object' && object[i] !== null) {
			const flatObject = flattenObject(object[i]);
			for (const x in flatObject) {
				if (!Object.prototype.hasOwnProperty.call(flatObject, x)) continue;

				toReturn[i + '.' + x] = flatObject[x];
			}
		} else {
			toReturn[i] = object[i];
		}
	}
	return toReturn;
}

type SaveSection = 'joystick' | 'curve' | 'trigger';

export default function CalibrationSettings() {
	const { updateUsedPins } = useContext(AppContext);
	const [saveMessageJoystick, setSaveMessageJoystick] = useState('');
	const [saveMessageCurve, setSaveMessageCurve] = useState('');
	const [saveMessageTrigger, setSaveMessageTrigger] = useState('');
	const [storedData, setStoredData] = useState({});
	const [triggerErrorModalShow, setTriggerErrorModalShow] = useState(false);
	const lastSaveSectionRef = useRef<SaveSection | null>(null);

	const { t } = useTranslation();

	const onSuccess = async (values: typeof DEFAULT_VALUES) => {
		const section = lastSaveSectionRef.current;
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
		const arrayFields = ['joystickRangeData1', 'joystickRangeData2', 'joystickCurvePoints1', 'joystickCurvePoints2', 'joystickCurvePresets', 'lsm6dsrEngageKeys'];
		const resultObject = { ...data };
		arrayFields.forEach(field => {
			const newVal = get(valuesSchema, field);
			const oldVal = get(flattened, field);
			if (Array.isArray(newVal) && (!Array.isArray(oldVal) || JSON.stringify(newVal) !== JSON.stringify(oldVal))) {
				set(resultObject, field, newVal);
			}
		});
		sanitizeData(resultObject);
		const result = await WebApi.setAddonsOptions(resultObject);
		if (result && result.error) {
			setTriggerErrorModalShow(true);
			return;
		}
		if (!result) {
			const msg = t('Common:saved-error-message');
			if (section === 'joystick') setSaveMessageJoystick(msg);
			else if (section === 'curve') setSaveMessageCurve(msg);
			else if (section === 'trigger') setSaveMessageTrigger(msg);
			return;
		}
		setStoredData(JSON.parse(JSON.stringify(values)));
		const msg = t('Common:saved-success-message');
		if (section === 'joystick') setSaveMessageJoystick(msg);
		else if (section === 'curve') setSaveMessageCurve(msg);
		else if (section === 'trigger') setSaveMessageTrigger(msg);
		updateUsedPins();
	};

	return (
		<div>
			<Modal show={triggerErrorModalShow} onHide={() => setTriggerErrorModalShow(false)} centered>
				<Modal.Header closeButton>
					<Modal.Title>{t('CalibrationSettings:hml-modal-title-hint')}</Modal.Title>
				</Modal.Header>
				<Modal.Body>{t('CalibrationSettings:hml-trigger-mapping-error')}</Modal.Body>
				<Modal.Footer>
					<Button variant="primary" onClick={() => setTriggerErrorModalShow(false)}>
						{t('CalibrationSettings:hml-button-ok')}
					</Button>
				</Modal.Footer>
			</Modal>
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
						saveMessage={saveMessageJoystick}
						onSaveClick={() => {
							lastSaveSectionRef.current = 'joystick';
							handleSubmit();
						}}
					/>

					<JoystickCurveSettings
						values={values}
						errors={errors}
						handleChange={handleChange}
						setFieldValue={setFieldValue}
						saveMessage={saveMessageCurve}
						onSaveClick={() => {
							lastSaveSectionRef.current = 'curve';
							handleSubmit();
						}}
					/>

					<TriggerCalibrationSettings
						values={values}
						setFieldValue={setFieldValue}
						saveMessage={saveMessageTrigger}
						onSaveClick={() => {
							lastSaveSectionRef.current = 'trigger';
							handleSubmit();
						}}
					/>

					<FormContext setStoredData={setStoredData} />
				</Form>
			)}
		</Formik>
		</div>
	);
}
