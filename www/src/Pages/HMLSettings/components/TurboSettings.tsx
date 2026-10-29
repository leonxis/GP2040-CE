import { useContext, useEffect, useState } from 'react';
import { Button, Form, FormCheck, Row } from 'react-bootstrap';
import { Formik } from 'formik';
import * as yup from 'yup';
import { useTranslation } from 'react-i18next';

import { AppContext } from '../../../Contexts/AppContext';
import Section from '../../../Components/Section';
import FormSelect from '../../../Components/FormSelect';
import FormControl from '../../../Components/FormControl';
import WebApi from '../../../Services/WebApi';
import { BUTTON_MASKS_OPTIONS } from '../../../Data/Buttons';
import { DUAL_STICK_MODES } from '../../../Data/Addons';

const SHMUP_MIXED_MODES = [
	{ label: 'Turbo Priority', value: 0 },
	{ label: 'Charge Priority', value: 1 },
];

const TURBO_MASKS = [
	{ label: 'None', value: 0 },
	{ label: 'B1', value: 1 << 0 },
	{ label: 'B2', value: 1 << 1 },
	{ label: 'B3', value: 1 << 2 },
	{ label: 'B4', value: 1 << 3 },
	{ label: 'L1', value: 1 << 4 },
	{ label: 'R1', value: 1 << 5 },
	{ label: 'L2', value: 1 << 6 },
	{ label: 'R2', value: 1 << 7 },
	{ label: 'L3', value: 1 << 10 },
	{ label: 'R3', value: 1 << 11 },
];

export const turboScheme = {
	TurboInputEnabled: yup.number().required().label('Turbo Input Enabled'),
	pinShmupBtn1: yup
		.number()
		.label('Charge Shot 1 Pin')
		.validatePinWhenValue('TurboInputEnabled'),
	pinShmupBtn2: yup
		.number()
		.label('Charge Shot 2 Pin')
		.validatePinWhenValue('TurboInputEnabled'),
	pinShmupBtn3: yup
		.number()
		.label('Charge Shot 3 Pin')
		.validatePinWhenValue('TurboInputEnabled'),
	pinShmupBtn4: yup
		.number()
		.label('Charge Shot 4 Pin')
		.validatePinWhenValue('TurboInputEnabled'),
	turboShotCount: yup
		.number()
		.label('Turbo Shot Count')
		.validateRangeWhenValue('TurboInputEnabled', 2, 30),
	shmupMode: yup
		.number()
		.label('Shmup Mode Enabled')
		.validateRangeWhenValue('TurboInputEnabled', 0, 1),
	shmupMixMode: yup
		.number()
		.label('Shmup Mix Priority')
		.validateSelectionWhenValue('TurboInputEnabled', DUAL_STICK_MODES),
	shmupAlwaysOn1: yup
		.number()
		.label('Turbo-Button 1 (Always On)')
		.validateSelectionWhenValue('TurboInputEnabled', BUTTON_MASKS_OPTIONS),
	shmupAlwaysOn2: yup
		.number()
		.label('Turbo-Button 2 (Always On)')
		.validateSelectionWhenValue('TurboInputEnabled', BUTTON_MASKS_OPTIONS),
	shmupAlwaysOn3: yup
		.number()
		.label('Turbo-Button 3 (Always On)')
		.validateSelectionWhenValue('TurboInputEnabled', BUTTON_MASKS_OPTIONS),
	shmupAlwaysOn4: yup
		.number()
		.label('Turbo-Button 4 (Always On)')
		.validateSelectionWhenValue('TurboInputEnabled', BUTTON_MASKS_OPTIONS),
	shmupBtnMask1: yup
		.number()
		.label('Charge Shot Button 1 Map')
		.validateSelectionWhenValue('TurboInputEnabled', BUTTON_MASKS_OPTIONS),
	shmupBtnMask2: yup
		.number()
		.label('Charge Shot Button 2 Map')
		.validateSelectionWhenValue('TurboInputEnabled', BUTTON_MASKS_OPTIONS),
	shmupBtnMask3: yup
		.number()
		.label('Charge Shot Button 3 Map')
		.validateSelectionWhenValue('TurboInputEnabled', BUTTON_MASKS_OPTIONS),
	shmupBtnMask4: yup
		.number()
		.label('Charge Shot Button 4 Map')
		.validateSelectionWhenValue('TurboInputEnabled', BUTTON_MASKS_OPTIONS),
};

export const turboState = {
	pinShmupBtn1: -1,
	pinShmupBtn2: -1,
	pinShmupBtn3: -1,
	pinShmupBtn4: -1,
	shmupAlwaysOn1: 0,
	shmupAlwaysOn2: 0,
	shmupAlwaysOn3: 0,
	shmupAlwaysOn4: 0,
	shmupBtnMask1: 0,
	shmupBtnMask2: 0,
	shmupBtnMask3: 0,
	shmupBtnMask4: 0,
	shmupMixMode: 0,
	shmupMode: 0,
	TurboInputEnabled: 0,
	turboShotCount: 5,
};

const turboSchema = yup.object().shape(turboScheme);

export default function TurboSettings() {
	const { t } = useTranslation();
	const { updateUsedPins } = useContext(AppContext);
	const [initialValues, setInitialValues] = useState(turboState);
	const [saveMessage, setSaveMessage] = useState('');

	useEffect(() => {
		let cancelled = false;
		async function load() {
			const data = await WebApi.getAddonsOptions();
			if (!cancelled && data) {
				const merged = { ...turboState };
				(Object.keys(turboState) as (keyof typeof turboState)[]).forEach(
					(k) => {
						const v = data[k as string];
						if (v !== undefined && v !== null) {
							(merged as Record<string, unknown>)[k] = v;
						}
					},
				);
				setInitialValues(merged);
			}
		}
		load();
		return () => {
			cancelled = true;
		};
	}, []);

	const onSave = async (values: typeof turboState) => {
		setSaveMessage('');
		const casted = turboSchema.cast(values);
		const ok = await WebApi.setAddonsOptions({
			...casted,
		});
		setSaveMessage(
			ok
				? t('Common:saved-success-message')
				: t('Common:saved-error-message'),
		);
		if (ok) {
			updateUsedPins();
			setTimeout(() => setSaveMessage(''), 3000);
		}
	};

	const saveOk = saveMessage === t('Common:saved-success-message');

	return (
		<Formik
			enableReinitialize
			validationSchema={turboSchema}
			initialValues={initialValues}
			onSubmit={onSave}
		>
			{({ handleSubmit, handleChange, values, errors, setFieldValue }) => (
				<Form noValidate onSubmit={handleSubmit}>
					<Section title={t('AddonsConfig:turbo-header-text')}>
						<div id="TurboInputOptions" hidden={!values.TurboInputEnabled}>
							<Row className="mb-3">
								<FormControl
									type="number"
									label={t('AddonsConfig:turbo-shot-count-label')}
									name="turboShotCount"
									className="form-control-sm"
									groupClassName="col-sm-3 mb-3"
									value={values.turboShotCount}
									error={errors.turboShotCount}
									isInvalid={Boolean(errors.turboShotCount)}
									onChange={handleChange}
									min={2}
									max={30}
								/>
							</Row>
							<Row className="mb-3">
								<FormCheck
									label={t('AddonsConfig:turbo-shmup-mode-label')}
									type="switch"
									id="ShmupMode"
									className="col-sm-3 ms-3"
									isInvalid={false}
									checked={Boolean(values.shmupMode)}
									onChange={() =>
										setFieldValue('shmupMode', values.shmupMode ? 0 : 1)
									}
								/>
								<div id="ShmupOptions" hidden={!values.shmupMode}>
									<Row className="mb-3">
										{([1, 2, 3, 4] as const).map((i) => (
											<FormSelect
												key={`shmupAlwaysOn${i}`}
												label={t(`AddonsConfig:turbo-shmup-always-on-${i}-label`)}
												name={`shmupAlwaysOn${i}`}
												className="form-select-sm"
												groupClassName="col-sm-3 mb-3"
												value={values[`shmupAlwaysOn${i}`]}
												error={errors[`shmupAlwaysOn${i}`]}
												isInvalid={Boolean(errors[`shmupAlwaysOn${i}`])}
												onChange={handleChange}
											>
												{TURBO_MASKS.map((o, j) => (
													<option
														key={`shmupAlwaysOn${i}-option-${j}`}
														value={o.value}
													>
														{o.label}
													</option>
												))}
											</FormSelect>
										))}
									</Row>
									<Row className="mb-3">
										{([1, 2, 3, 4] as const).map((i) => (
											<FormControl
												key={`pinShmupBtn${i}`}
												type="number"
												label={t(`AddonsConfig:turbo-shmup-button-${i}-label`)}
												name={`pinShmupBtn${i}`}
												className="form-control-sm"
												groupClassName="col-sm-3 mb-3"
												value={values[`pinShmupBtn${i}`]}
												error={errors[`pinShmupBtn${i}`]}
												isInvalid={Boolean(errors[`pinShmupBtn${i}`])}
												onChange={handleChange}
												min={-1}
												max={29}
											/>
										))}
									</Row>
									<Row className="mb-3">
										{([1, 2, 3, 4] as const).map((i) => (
											<FormSelect
												key={`shmupBtnMask${i}`}
												label={t(`AddonsConfig:turbo-shmup-button-mask-${i}-label`)}
												name={`shmupBtnMask${i}`}
												className="form-select-sm"
												groupClassName="col-sm-3 mb-3"
												value={values[`shmupBtnMask${i}`]}
												error={errors[`shmupBtnMask${i}`]}
												isInvalid={Boolean(errors[`shmupBtnMask${i}`])}
												onChange={handleChange}
											>
												{TURBO_MASKS.map((o, j) => (
													<option
														key={`shmupBtnMask${i}-option-${j}`}
														value={o.value}
													>
														{o.label}
													</option>
												))}
											</FormSelect>
										))}
									</Row>
									<FormSelect
										label={t('AddonsConfig:turbo-shmup-mix-mode-label')}
										name="shmupMixMode"
										className="form-select-sm"
										groupClassName="col-sm-3 mb-3"
										value={values.shmupMixMode}
										error={errors.shmupMixMode}
										isInvalid={Boolean(errors.shmupMixMode)}
										onChange={handleChange}
									>
										{SHMUP_MIXED_MODES.map((o, i) => (
											<option
												key={`button-shmupMixedMode-option-${i}`}
												value={o.value}
											>
												{o.label}
											</option>
										))}
									</FormSelect>
								</div>
							</Row>
						</div>
						<div
							style={{
								marginTop: '16px',
								display: 'flex',
								justifyContent: 'space-between',
								alignItems: 'center',
								width: '100%',
							}}
						>
							<div
								style={{
									display: 'flex',
									alignItems: 'center',
									gap: '8px',
								}}
							>
								<Button variant="primary" type="submit">
									{t('Common:button-save-label')}
								</Button>
								{saveMessage && (
									<span className={saveOk ? 'text-success' : 'text-danger'}>
										{saveMessage}
									</span>
								)}
							</div>
							<div
								style={{
									display: 'flex',
									alignItems: 'center',
									gap: '8px',
								}}
							>
								<Form.Check
									type="switch"
									id="turbo-input-enabled"
									label={t('Common:switch-enabled')}
									checked={Boolean(values.TurboInputEnabled)}
									onChange={(e) =>
										setFieldValue(
											'TurboInputEnabled',
											e.target.checked ? 1 : 0,
										)
									}
								/>
							</div>
						</div>
					</Section>
				</Form>
			)}
		</Formik>
	);
}
