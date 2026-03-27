import { useContext, useRef, useState } from 'react';
import { Button, Form, Modal } from 'react-bootstrap';
import { Formik } from 'formik';
import { useTranslation } from 'react-i18next';
import get from 'lodash/get';
import set from 'lodash/set';

import { AppContext } from '../../../Contexts/AppContext';
import { hexToInt } from '../../../Services/Utilities';
import WebApi from '../../../Services/WebApi';
import {
	DEFAULT_VALUES,
	FormContext,
	flattenObject,
	sanitizeData,
	schema,
} from './CalibrationSettings';
import GyroSettings from './GyroSettings';

export default function MotionSettings() {
	const { updateUsedPins } = useContext(AppContext);
	const [saveMessageGyro, setSaveMessageGyro] = useState('');
	const [storedData, setStoredData] = useState({});
	const [errorModalShow, setErrorModalShow] = useState(false);
	const lastSaveSectionRef = useRef<'gyro' | null>(null);
	const { t } = useTranslation();

	const onSuccess = async (values: typeof DEFAULT_VALUES) => {
		const section = lastSaveSectionRef.current;
		const flattened = flattenObject(storedData);
		const data = {
			...flattened,
			...flattenObject(values),
		};
		if (data.turboLedColor && typeof data.turboLedColor === 'string') {
			data.turboLedColor = hexToInt(data.turboLedColor);
		}
		const valuesSchema = flattenObject(values);
		const arrayFields = ['joystickRangeData1', 'joystickRangeData2', 'joystickCurvePoints1', 'joystickCurvePoints2', 'joystickCurvePresets', 'lsm6dsrEngageKeys'];
		const resultObject = { ...data };
		arrayFields.forEach((field) => {
			const newVal = get(valuesSchema, field);
			const oldVal = get(flattened, field);
			if (Array.isArray(newVal) && (!Array.isArray(oldVal) || JSON.stringify(newVal) !== JSON.stringify(oldVal))) {
				set(resultObject, field, newVal);
			}
		});
		sanitizeData(resultObject);
		const result = await WebApi.setAddonsOptions(resultObject);
		if (result && result.error) {
			setErrorModalShow(true);
			return;
		}
		if (!result) {
			if (section === 'gyro') setSaveMessageGyro(t('Common:saved-error-message'));
			return;
		}
		setStoredData(JSON.parse(JSON.stringify(values)));
		if (section === 'gyro') setSaveMessageGyro(t('Common:saved-success-message'));
		updateUsedPins();
	};

	return (
		<div>
			<Modal show={errorModalShow} onHide={() => setErrorModalShow(false)} centered>
				<Modal.Header closeButton>
					<Modal.Title>{t('CalibrationSettings:hml-modal-title-hint')}</Modal.Title>
				</Modal.Header>
				<Modal.Body>{t('Common:saved-error-message')}</Modal.Body>
				<Modal.Footer>
					<Button variant="primary" onClick={() => setErrorModalShow(false)}>
						{t('CalibrationSettings:hml-button-confirm')}
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
						<GyroSettings
							values={values}
							errors={errors}
							handleChange={handleChange}
							setFieldValue={setFieldValue}
							saveMessage={saveMessageGyro}
							onSaveClick={() => {
								lastSaveSectionRef.current = 'gyro';
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
