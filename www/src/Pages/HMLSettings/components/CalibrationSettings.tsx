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
import { lsm6dsrScheme, lsm6dsrState } from '../../../Addons/LSM6DSR';
import { bootselScheme, bootselState } from '../../../Addons/Bootsel';
import { buzzerScheme, buzzerState } from '../../../Addons/Buzzer';
import {
	dualDirectionScheme,
	dualDirectionState,
} from '../../../Addons/DualDirection';
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
import AxisTiltOverlaySettings, {
	axisTiltOverlaySettingsScheme,
	axisTiltOverlaySettingsState,
} from './AxisTiltOverlaySettings';

/** Inner/anti deadzone wire: uint32 0–200 = tenths of a percent; UI uses value/10 (device norm value/1000). */
function decodeStickDeadzoneFromDevice(raw: unknown): number {
	const n = Number(raw);
	if (Number.isNaN(n)) return 0;
	const r = Math.min(200, Math.max(0, Math.floor(n)));
	return Math.min(20, r / 10);
}

function encodeStickDeadzoneToDevice(percent: number): number {
	const p = Math.min(20, Math.max(0, percent));
	return Math.round(p * 10);
}

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
	...lsm6dsrScheme,
	...bootselScheme,
	...turboScheme,
	...reverseScheme,
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
	...axisTiltOverlaySettingsScheme,
});

export const DEFAULT_VALUES = {
	...analogState,
	...lsm6dsrState,
	...bootselState,
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
	...axisTiltOverlaySettingsState,
} as const;

export const FormContext = ({ setStoredData }) => {
	const { values, setValues } = useFormikContext();
	const { setLoading } = useContext(AppContext);

	useEffect(() => {
		async function fetchData() {
			const data = await WebApi.getAddonsOptions(setLoading);
			// 合并默认值，避免 API 未返回的字段（如扳机校准）丢失默认配置
			const merged = { ...DEFAULT_VALUES, ...data };
			const r3 = merged.axisTiltOverlayRcGainReserved3;
			if (typeof r3 === 'number' && !Number.isNaN(r3) && r3 < 3) {
				merged.axisTiltOverlayRcGainReserved3 = 3;
			}
			merged.inner_deadzone = decodeStickDeadzoneFromDevice(merged.inner_deadzone);
			merged.inner_deadzone2 = decodeStickDeadzoneFromDevice(merged.inner_deadzone2);
			merged.anti_deadzone = decodeStickDeadzoneFromDevice(merged.anti_deadzone);
			merged.anti_deadzone2 = decodeStickDeadzoneFromDevice(merged.anti_deadzone2);
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
	'inner_deadzone',
	'inner_deadzone2',
	'anti_deadzone',
	'anti_deadzone2',
	'axisTiltOverlayRightYPercent1',
	'axisTiltOverlayRightYPercent2',
	'axisTiltOverlayRightYPercent3',
	'axisTiltOverlayRcGainReserved1',
	'axisTiltOverlayRcGainReserved2',
	'axisTiltOverlayRcGainReserved3',
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

type SaveSection = 'joystick' | 'curve' | 'axisOverlay';

export default function CalibrationSettings() {
	const { updateUsedPins } = useContext(AppContext);
	const [saveMessageJoystick, setSaveMessageJoystick] = useState('');
	const [saveMessageCurve, setSaveMessageCurve] = useState('');
	const [saveMessageAxisOverlay, setSaveMessageAxisOverlay] = useState('');
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
		resultObject.inner_deadzone = encodeStickDeadzoneToDevice(Number(values.inner_deadzone ?? 0));
		resultObject.inner_deadzone2 = encodeStickDeadzoneToDevice(Number(values.inner_deadzone2 ?? 0));
		resultObject.anti_deadzone = encodeStickDeadzoneToDevice(Number(values.anti_deadzone ?? 0));
		resultObject.anti_deadzone2 = encodeStickDeadzoneToDevice(Number(values.anti_deadzone2 ?? 0));
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
			else if (section === 'axisOverlay') setSaveMessageAxisOverlay(msg);
			return;
		}
		setStoredData(JSON.parse(JSON.stringify(values)));
		const msg = t('Common:saved-success-message');
		if (section === 'joystick') setSaveMessageJoystick(msg);
		else if (section === 'curve') setSaveMessageCurve(msg);
		else if (section === 'axisOverlay') setSaveMessageAxisOverlay(msg);
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
						setFieldValue={setFieldValue}
						saveMessage={saveMessageCurve}
						onSaveClick={() => {
							lastSaveSectionRef.current = 'curve';
							handleSubmit();
						}}
					/>

					<AxisTiltOverlaySettings
						values={values}
						errors={errors}
						handleChange={handleChange}
						handleCheckbox={(name: keyof typeof DEFAULT_VALUES) => {
							setFieldValue(name, values[name] === 1 ? 0 : 1);
						}}
						setFieldValue={setFieldValue}
						saveMessage={saveMessageAxisOverlay}
						onSaveClick={() => {
							lastSaveSectionRef.current = 'axisOverlay';
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
