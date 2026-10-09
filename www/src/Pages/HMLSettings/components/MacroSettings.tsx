import { useContext, useEffect, useState } from 'react';
import {
	Badge,
	Button,
	Col,
	Form,
	InputGroup,
	Nav,
	Row,
	Tab,
	Tabs,
	Table,
	Alert,
} from 'react-bootstrap';

import { Formik, useFormikContext } from 'formik';
import * as yup from 'yup';
import { Trans, useTranslation } from 'react-i18next';

import { AppContext } from '../../../Contexts/AppContext';
import Section from '../../../Components/Section';
import WebApi from '../../../Services/WebApi';
import {
	BUTTONS,
	BUTTON_MASKS_OPTIONS,
} from '../../../Data/Buttons';
import { BUTTON_ACTIONS } from '../../../Data/Pins';

const MACRO_TYPES = [
	{
		label: 'InputMacroAddon:input-macro-type-press',
		desc: 'InputMacroAddon:input-macro-type-press-desc',
		value: 1,
	},
	{
		label: 'InputMacroAddon:input-macro-type-hold-repeat',
		desc: 'InputMacroAddon:input-macro-type-hold-repeat-desc',
		value: 2,
	},
	{
		label: 'InputMacroAddon:input-macro-type-toggle',
		desc: 'InputMacroAddon:input-macro-type-toggle-desc',
		value: 3,
	},
];
const MACRO_INPUTS_MAX = 30;
const MACRO_LIMIT = 6;
// Macro 1/2 (indices 0/1) are pure recorded macros; macro 3-6 are edited macros.
const MACRO_REC_SLOT_COUNT = 2;

// Stick-direction sentinels (must match the firmware switch in input_macro.cpp).
const STICK_DIRECTION_LEFT_CENTER = 0xfffffffe;
const STICK_DIRECTION_RIGHT_CENTER = 0xfffffffd;

// Left/right stick selectors are separate fields (stickDirection /
// stickDirectionR) so one macro step can drive both sticks at once. Option
// labels are InputMacroAddon i18n keys (up/down/left/right/center).
const LEFT_STICK_DIRECTION_OPTIONS = [
	{ label: 'input-macro-stick-direction-left-up', value: BUTTON_ACTIONS.ANALOG_DIRECTION_LS_Y_NEG },
	{ label: 'input-macro-stick-direction-left-down', value: BUTTON_ACTIONS.ANALOG_DIRECTION_LS_Y_POS },
	{ label: 'input-macro-stick-direction-left-left', value: BUTTON_ACTIONS.ANALOG_DIRECTION_LS_X_NEG },
	{ label: 'input-macro-stick-direction-left-right', value: BUTTON_ACTIONS.ANALOG_DIRECTION_LS_X_POS },
	{ label: 'input-macro-stick-direction-left-center', value: STICK_DIRECTION_LEFT_CENTER },
];

const RIGHT_STICK_DIRECTION_OPTIONS = [
	{ label: 'input-macro-stick-direction-right-up', value: BUTTON_ACTIONS.ANALOG_DIRECTION_RS_Y_NEG },
	{ label: 'input-macro-stick-direction-right-down', value: BUTTON_ACTIONS.ANALOG_DIRECTION_RS_Y_POS },
	{ label: 'input-macro-stick-direction-right-left', value: BUTTON_ACTIONS.ANALOG_DIRECTION_RS_X_NEG },
	{ label: 'input-macro-stick-direction-right-right', value: BUTTON_ACTIONS.ANALOG_DIRECTION_RS_X_POS },
	{ label: 'input-macro-stick-direction-right-center', value: STICK_DIRECTION_RIGHT_CENTER },
];

// Legacy single-selector values that targeted the right stick; on load these
// are migrated from stickDirection into stickDirectionR.
const LEGACY_RIGHT_STICK_VALUES = [
	BUTTON_ACTIONS.ANALOG_DIRECTION_RS_X_NEG,
	BUTTON_ACTIONS.ANALOG_DIRECTION_RS_X_POS,
	BUTTON_ACTIONS.ANALOG_DIRECTION_RS_Y_NEG,
	BUTTON_ACTIONS.ANALOG_DIRECTION_RS_Y_POS,
	STICK_DIRECTION_RIGHT_CENTER,
];

const schema = yup.object().shape({
	macroList: yup.array().of(
		yup.object().shape({
			macroType: yup.number(),
			macroLabel: yup.string(),
			enabled: yup.number(),
			exclusive: yup.number(),
			interruptible: yup.number(),
			showFrames: yup.number(),
			useMacroTriggerButton: yup.number(),
			macroTriggerButton: yup.number(),
			hasRecording: yup.number().optional(),
			recFrames: yup.number().optional(),
			macroInputs: yup
				.array()
				.max(MACRO_INPUTS_MAX, 'Exceeded maximum inputs')
				.of(
					yup.object().shape({
						buttonMask: yup.number().required(),
						duration: yup.number().required(),
						waitDuration: yup.number().required(),
						stickDirection: yup.number().optional(),
						stickDirectionR: yup.number().optional(),
					}),
				),
		}),
	),
});

const defaultMacroInput = {
	buttonMask: 0,
	duration: 16666,
	waitDuration: 0,
	stickDirection: 0,
	stickDirectionR: 0,
};

const createDefaultMacroItem = (index?: number) => ({
	macroType: 1,
	macroLabel: '',
	enabled: 0,
	exclusive: 1,
	interruptible: 1,
	showFrames: 1,
	useMacroTriggerButton: 0,
	macroTriggerButton: 0,
	hasRecording: 0,
	recFrames: 0,
	clearRecording: 0,
	macroInputs:
		index != null && index < MACRO_REC_SLOT_COUNT
			? []
			: [{ ...defaultMacroInput }],
});

const defaultValues = {
	macroList: Array.from({ length: MACRO_LIMIT }, (_, i) =>
		createDefaultMacroItem(i),
	),
};

const ONE_FRAME_US = 16666;

function formatRecDuration(frames: number, t): string {
	const totalSeconds = (frames ?? 0) * 0.004;
	const minutes = Math.floor(totalSeconds / 60);
	const seconds = (totalSeconds - minutes * 60).toFixed(1).padStart(4, '0');
	return `${minutes}:${seconds}`;
}

const FormContext = () => {
	const { setValues } = useFormikContext();
	const { setLoading } = useContext(AppContext);

	useEffect(() => {
		async function fetchData() {
			const options = await WebApi.getMacroAddonOptions(setLoading);
			if (options == null || typeof options !== 'object') {
				return;
			}
			const macroListSrc = options.macroList;
			const next = {
				...options,
				macroList: Array.isArray(macroListSrc)
					? macroListSrc.map((macro) => ({
							...macro,
							macroLabel:
								macro.macroLabel == null ? '' : String(macro.macroLabel),
							hasRecording: macro.hasRecording ?? 0,
							recFrames: macro.recFrames ?? 0,
							clearRecording: 0,
							macroInputs: macro.macroInputs
								? macro.macroInputs.map((input) => {
										// Legacy configs stored a right-stick action in the
										// single stickDirection field; move it to the R selector.
										const legacyDir = input.stickDirection || 0;
										const isLegacyRight =
											LEGACY_RIGHT_STICK_VALUES.includes(legacyDir);
										return {
											buttonMask: input.buttonMask ?? 0,
											duration: input.duration ?? 16666,
											waitDuration: input.waitDuration ?? 0,
											stickDirection: isLegacyRight ? 0 : legacyDir,
											stickDirectionR: isLegacyRight
												? legacyDir
												: input.stickDirectionR || 0,
										};
									})
								: [],
						}))
					: defaultValues.macroList,
			};
			setValues(next);
		}
		fetchData();
	}, [setValues]);

	return null;
};

const ButtonMasksComponent = (props) => {
	const {
		id: key,
		value,
		onChange,
		isInvalid,
		buttonLabelType,
		buttonMasks,
	} = props;
	return (
		<Form.Select
			size="sm"
			name={`${key}.buttonMask`}
			value={value}
			isInvalid={isInvalid}
			onChange={onChange}
		>
			{buttonMasks.map((o, i2) => (
				<option key={`${key}.mask[${i2}]`} value={o.value}>
					{(buttonLabelType && BUTTONS[buttonLabelType][o.label]) || o.label}
				</option>
			))}
		</Form.Select>
	);
};

const MacroInputComponent = (props) => {
	const {
		value,
		buttonLabelType,
		showFrames,
		errors,
		id: key,
		translation: t,
		deleteMacroInput,
		setFieldValue,
	} = props;
	const input = value ?? {};
	const duration = input.duration ?? 16666;
	const buttonMask = input.buttonMask ?? 0;
	const waitDuration = input.waitDuration ?? 0;
	const stickDirection = input.stickDirection ?? 0;
	const stickDirectionR = input.stickDirectionR ?? 0;

	return (
		<Row className="align-content-start align-items-center row-gap-2 gx-2 pb-2">
			<Col xs="auto" style={{ width: 150 }}>
				<InputGroup size="sm">
					<Form.Control
						className="text-center"
						type="number"
						placeholder={t('InputMacroAddon:input-macro-duration-label')}
						name={`${key}.duration`}
						value={duration / (showFrames ? ONE_FRAME_US : 1000)}
						step="any"
						isInvalid={errors?.duration}
						onChange={(e) => {
							setFieldValue(
								`${key}.duration`,
								e.target.value * (showFrames ? ONE_FRAME_US : 1000),
							);
						}}
						min={0}
					/>
					<InputGroup.Text>
						{t(
							showFrames
								? 'InputMacroAddon:input-macro-time-label-frames'
								: 'InputMacroAddon:input-macro-time-label-ms',
						)}
					</InputGroup.Text>
				</InputGroup>
			</Col>
			{BUTTON_MASKS_OPTIONS.filter((mask) => buttonMask & mask.value).map(
				(mask, i1) => (
					<Col xs="auto" key={`${key}.buttonMask[${i1}]`}>
						<ButtonMasksComponent
							id={`${key}.buttonMask[${i1}]`}
							value={buttonMask & mask.value}
							onChange={(e) => {
								setFieldValue(
									`${key}.buttonMask`,
									(buttonMask ^ mask.value) | e.target.value,
								);
							}}
							isInvalid={errors?.buttonMask}
							buttonLabelType={buttonLabelType}
							buttonMasks={BUTTON_MASKS_OPTIONS}
						/>
					</Col>
				),
			)}
			<Col xs="auto">
				<ButtonMasksComponent
					id={`${key}.buttonMaskPlaceholder`}
					value={0}
					onChange={(e) => {
						setFieldValue(`${key}.buttonMask`, buttonMask | e.target.value);
					}}
					isInvalid={errors?.buttonMask}
					buttonLabelType={buttonLabelType}
					buttonMasks={BUTTON_MASKS_OPTIONS}
				/>
			</Col>
			<Col xs="auto">
				<InputGroup size="sm">
					<InputGroup.Text>
						{t('InputMacroAddon:input-macro-stick-direction-left-label')}
					</InputGroup.Text>
					<Form.Select
						size="sm"
						name={`${key}.stickDirection`}
						value={stickDirection || 0}
						isInvalid={errors?.stickDirection}
						onChange={(e) => {
							setFieldValue(`${key}.stickDirection`, parseInt(e.target.value) || 0);
						}}
					>
						<option value={0}>
							{t('InputMacroAddon:input-macro-stick-direction-none')}
						</option>
						{LEFT_STICK_DIRECTION_OPTIONS.map((option) => (
							<option
								key={`${key}.stickDirection.${option.value}`}
								value={option.value}
							>
								{t(`InputMacroAddon:${option.label}`)}
							</option>
						))}
					</Form.Select>
				</InputGroup>
			</Col>
			<Col xs="auto">
				<InputGroup size="sm">
					<InputGroup.Text>
						{t('InputMacroAddon:input-macro-stick-direction-right-label')}
					</InputGroup.Text>
					<Form.Select
						size="sm"
						name={`${key}.stickDirectionR`}
						value={stickDirectionR || 0}
						isInvalid={errors?.stickDirectionR}
						onChange={(e) => {
							setFieldValue(`${key}.stickDirectionR`, parseInt(e.target.value) || 0);
						}}
					>
						<option value={0}>
							{t('InputMacroAddon:input-macro-stick-direction-none')}
						</option>
						{RIGHT_STICK_DIRECTION_OPTIONS.map((option) => (
							<option
								key={`${key}.stickDirectionR.${option.value}`}
								value={option.value}
							>
								{t(`InputMacroAddon:${option.label}`)}
							</option>
						))}
					</Form.Select>
				</InputGroup>
			</Col>
			<Col xs="auto" style={{ width: 290 }}>
				<InputGroup size="sm">
					<InputGroup.Text>
						{t('InputMacroAddon:input-macro-release-and-wait-label')}
					</InputGroup.Text>
					<Form.Control
						className="text-center d-flex"
						type="number"
						placeholder={t('InputMacroAddon:input-macro-wait-duration-label')}
						name={`${key}.waitDuration`}
						value={waitDuration / (showFrames ? ONE_FRAME_US : 1000)}
						step="any"
						isInvalid={errors?.waitDuration}
						onChange={(e) => {
							setFieldValue(
								`${key}.waitDuration`,
								e.target.value * (showFrames ? ONE_FRAME_US : 1000),
							);
						}}
						min={0}
					/>
					<InputGroup.Text>
						{t(
							showFrames
								? 'InputMacroAddon:input-macro-time-label-frames'
								: 'InputMacroAddon:input-macro-time-label-ms',
						)}
					</InputGroup.Text>
				</InputGroup>
			</Col>
			<Col xs="auto">
				<Button size="sm" onClick={deleteMacroInput}>
					{'✕'}
				</Button>
			</Col>
		</Row>
	);
};

const RecordedMacroPanel = (props) => {
	const {
		id: key,
		value: macroValue,
		macroNumber,
		setFieldValue,
		translation: t,
	} = props;
	const hasRecording = Boolean(macroValue?.hasRecording);
	const recFrames = macroValue?.recFrames ?? 0;

	return (
		<div className="mt-3">
			<Alert variant="info" className="small">
				{t('InputMacroAddon:input-macro-record-hint', { macroNumber })}
				<br />
				{t('InputMacroAddon:input-macro-record-forced')}
			</Alert>
			<Row className="align-items-center mb-2">
				<Col sm="auto" className="fw-bold">
					{t('InputMacroAddon:input-macro-record-title')}:
				</Col>
				{hasRecording ? (
					<>
						<Col sm="auto">
							<Badge bg="success">
								{t('InputMacroAddon:input-macro-record-duration')}:{' '}
								{formatRecDuration(recFrames, t)}
							</Badge>
						</Col>
						<Col sm="auto">
							{t('InputMacroAddon:input-macro-record-frames')}: {recFrames}
						</Col>
						<Col sm="auto">
							<Button
								variant="outline-danger"
								size="sm"
								onClick={() => {
									if (
										window.confirm(
											t(
												'InputMacroAddon:input-macro-record-clear-confirm',
												{ macroNumber },
											),
										)
									) {
										setFieldValue(`${key}.hasRecording`, 0);
										setFieldValue(`${key}.recFrames`, 0);
										setFieldValue(`${key}.clearRecording`, 1);
									}
								}}
							>
								{t('InputMacroAddon:input-macro-record-clear')}
							</Button>
						</Col>
					</>
				) : (
					<Col sm="auto">
						<em>
							{t('InputMacroAddon:input-macro-record-none', { macroNumber })}
						</em>
					</Col>
				)}
			</Row>
		</div>
	);
};

const MacroComponent = (props) => {
	const {
		value: macroValue,
		errors,
		id: key,
		translation: t,
		index,
		buttonLabelType,
		deleteMacroInput,
		setFieldValue,
		macroList,
	} = props;

	if (macroValue == null || typeof macroValue !== 'object') {
		return null;
	}

	const {
		macroLabel = '',
		macroType,
		macroInputs = [],
		enabled,
		exclusive,
		interruptible,
		showFrames,
		useMacroTriggerButton,
		macroTriggerButton,
	} = macroValue;

	const isRecordSlot = index < MACRO_REC_SLOT_COUNT;
	const macroTypeEntry = MACRO_TYPES.find((o) => o.value === macroType);

	return (
		<div key={key}>
			<Row>
				<Col sm={'auto'}>
					<Form.Check
						name={`${key}.enabled`}
						label={t('InputMacroAddon:input-macro-macro-enabled')}
						type="switch"
						className="form-select-sm"
						checked={Boolean(enabled)}
						onChange={(e) => {
							setFieldValue(`${key}.enabled`, e.target.checked ? 1 : 0);
						}}
						isInvalid={false}
					/>
				</Col>
				{isRecordSlot && (
					<Col sm="auto" className="d-flex align-items-center">
						<Badge bg="info">
							{t('InputMacroAddon:input-macro-record-badge')}
						</Badge>
					</Col>
				)}
			</Row>
			<Row className="my-2">
				<Col sm={'auto'}>{t('InputMacroAddon:macro-name')}:</Col>
				<Col sm={'auto'}>
					<Form.Control
						size="sm"
						type="text"
						placeholder={t('InputMacroAddon:input-macro-macro-label-label')}
						name={`${key}.macroLabel`}
						value={macroLabel}
						isInvalid={errors?.macroLabel}
						onChange={(e) =>
							setFieldValue(`${key}.macroLabel`, e.target.value)}
						maxLength={256}
					/>
				</Col>
			</Row>
			<Row className="my-2 align-items-center">
				<Col sm="auto" className="mb-2">
					{t('InputMacroAddon:macro-activation-type')}:
				</Col>
				<Col sm={'auto'} className="mb-2">
					<Form.Select
						name={`${key}.macroType`}
						className="form-select-sm sm-1"
						value={macroType}
						onChange={(e) => {
							setFieldValue(`${key}.macroType`, parseInt(e.target.value));
						}}
					>
						{MACRO_TYPES.map((o, i2) => (
							<option key={`${key}-macroType${i2}`} value={o.value}>
								{t(o.label)}
							</option>
						))}
					</Form.Select>
				</Col>
				{macroTypeEntry && (
					<Col sm="auto" className="mb-2">
						<Form.Text className="text-muted">
							{t(macroTypeEntry.desc)}
						</Form.Text>
					</Col>
				)}
			</Row>

			<hr className="mt-4" />

			<Row>
				<Col sm={'auto'}>
					<Form.Check
						name={`${key}.interruptible`}
						label={t('InputMacroAddon:input-macro-macro-interruptible')}
						type="switch"
						className="form-select-sm"
						checked={Boolean(interruptible)}
						onChange={(e) => {
							setFieldValue(`${key}.interruptible`, e.target.checked ? 1 : 0);
						}}
						isInvalid={false}
					/>
				</Col>
			</Row>
			<Row>
				<Col sm={'auto'}>
					<Form.Check
						name={`${key}.exclusive`}
						label={t('InputMacroAddon:input-macro-macro-exclusive')}
						type="switch"
						className="form-select-sm"
						// Recorded slots are always exclusive (enforced in firmware).
						disabled={isRecordSlot}
						checked={isRecordSlot ? true : Boolean(exclusive)}
						onChange={(e) => {
							setFieldValue(`${key}.exclusive`, e.target.checked ? 1 : 0);
						}}
						isInvalid={false}
					/>
				</Col>
			</Row>
			<Row className="mt-2 align-items-center">
				<Col sm={'auto'}>
					<Form.Check
						name={`${key}.useMacroTriggerButton`}
						label={t('InputMacroAddon:input-macro-macro-uses-buttons')}
						type="switch"
						className="form-select-sm"
						checked={Boolean(useMacroTriggerButton)}
						onChange={(e) => {
							setFieldValue(
								`${key}.useMacroTriggerButton`,
								e.target.checked ? 1 : 0,
							);
						}}
						isInvalid={false}
					/>
				</Col>
				{useMacroTriggerButton == true && (
					<Col sm="auto">
						<Row className="g-2 align-items-center">
							<Col sm={'auto'}>
								{t('InputMacroAddon:input-macro-macro-button-pin-plus')}
							</Col>
							<Col sm={'auto'}>
								<ButtonMasksComponent
									id={`${key}.macroTriggerButton`}
									value={macroTriggerButton}
									onChange={(e) => {
										setFieldValue(
											`${key}.macroTriggerButton`,
											parseInt(e.target.value),
										);
									}}
									buttonLabelType={buttonLabelType}
									buttonMasks={BUTTON_MASKS_OPTIONS.filter(
										(b) =>
											macroList.find(
												(m, macroIdx) =>
													index != macroIdx &&
													m.useMacroTriggerButton &&
													m.macroTriggerButton === b.value,
											) === undefined,
									)}
								/>
							</Col>
						</Row>
					</Col>
				)}
			</Row>
			{isRecordSlot ? (
				<RecordedMacroPanel
					id={key}
					value={macroValue}
					macroNumber={index + 1}
					setFieldValue={setFieldValue}
					translation={t}
				/>
			) : (
				<Tabs defaultActiveKey="editor" className="mt-3 mb-3 pb-0" fill>
					<Tab
						eventKey="editor"
						title={t('InputMacroAddon:input-macro-editor-tab')}
					>
						<Row>
							<Col sm={'auto'}>
								<Form.Check
									name={`${key}.showFrames`}
									label={t('InputMacroAddon:input-macro-macro-show-frames')}
									type="switch"
									className="form-select-sm"
									checked={Boolean(showFrames)}
									onChange={(e) => {
										setFieldValue(`${key}.showFrames`, e.target.checked ? 1 : 0);
									}}
									isInvalid={false}
								/>
							</Col>
						</Row>
						{macroInputs.map((macroInput, a) => (
							<MacroInputComponent
								key={`${key}.macroInputs[${a}]`}
								id={`${key}.macroInputs[${a}]`}
								value={macroInput}
								errors={errors?.macroInputs?.at(a)}
								showFrames={showFrames}
								translation={t}
								buttonLabelType={buttonLabelType}
								deleteMacroInput={() => deleteMacroInput(a)}
								setFieldValue={setFieldValue}
							/>
						))}
						{!Array.isArray(errors?.macroInputs) && errors?.macroInputs && (
							<Alert variant="danger" className="mt-2">
								{errors.macroInputs}
							</Alert>
						)}
						<Row>
							<Col sm={3}>
								{macroInputs.length < MACRO_INPUTS_MAX && (
									<Button
										variant="success"
										className="col px-2"
										size="sm"
										onClick={() => {
											setFieldValue(`${key}.macroInputs[${macroInputs.length}]`, {
												...defaultMacroInput,
											});
										}}
									>
										<Trans
											ns="InputMacroAddon"
											i18nKey="input-macro-add-input-label"
										/>
									</Button>
								)}
							</Col>
						</Row>
					</Tab>
					<Tab
						eventKey="advanced"
						title={t('InputMacroAddon:input-macro-advanced-tab')}
					>
						<Form.Control
							as="textarea"
							value={JSON.stringify(macroInputs) || ''}
							isInvalid={errors?.macroInputs}
							isValid={!errors?.macroInputs}
							onChange={(e) => {
								e.preventDefault();
								if (!e.target.value.length) {
									setFieldValue(`${key}.macroInputs`, []);
									return;
								}
								try {
									const parsed = JSON.parse(e.target.value);
									if (!Array.isArray(parsed)) {
										console.error('macroInputs JSON must be an array');
										return;
									}
									setFieldValue(`${key}.macroInputs`, parsed);
								} catch (error) {
									console.error('Invalid JSON', error);
								}
							}}
							rows={15}
						/>
					</Tab>
				</Tabs>
			)}
		</div>
	);
};

export default function MacroSettings() {
	const { buttonLabels } = useContext(AppContext);
	const [saveMessage, setSaveMessage] = useState('');
	const { buttonLabelType } = buttonLabels;
	const { t } = useTranslation('');

	const saveSettings = async (values) => {
		const cleanedValues = {
			...values,
			macroList: values.macroList.map((macro, mi) => {
				const common = {
					macroType: macro.macroType,
					macroLabel: macro.macroLabel ?? '',
					useMacroTriggerButton: macro.useMacroTriggerButton,
					macroTriggerButton: macro.macroTriggerButton,
					enabled: macro.enabled,
					interruptible: macro.interruptible,
					showFrames: macro.showFrames,
				};

				if (mi < MACRO_REC_SLOT_COUNT) {
					// Recorded slot: no edited steps; exclusive is fixed by firmware.
					return {
						...common,
						exclusive: 1,
						macroInputs: [],
						hasRecording: macro.hasRecording ? 1 : 0,
						recFrames: macro.recFrames ?? 0,
						clearRecording: macro.clearRecording ? 1 : 0,
					};
				}

				return {
					...common,
					exclusive: macro.exclusive,
					macroInputs: (macro.macroInputs ?? []).map((input) => ({
						buttonMask: input.buttonMask ?? 0,
						duration: input.duration ?? 16666,
						waitDuration: input.waitDuration ?? 0,
						stickDirection: input.stickDirection || 0,
						stickDirectionR: input.stickDirectionR || 0,
					})),
				};
			}),
		};
		const success = await WebApi.setMacroAddonOptions(cleanedValues);
		setSaveMessage(
			success
				? t('Common:saved-success-message')
				: t('Common:saved-error-message'),
		);
	};

	const onSuccess = async (values) => await saveSettings(values);

	return (
		<Formik
			validationSchema={schema}
			onSubmit={onSuccess}
			initialValues={defaultValues}
		>
			{({
				handleSubmit,
				values,
				errors,
				setFieldValue,
			}) => (
				<div>
					<Form noValidate onSubmit={handleSubmit}>
						<Tab.Container defaultActiveKey="settings">
							<Nav variant="tabs" className="macro-settings-top-tabs mb-3 w-100">
								<Nav.Item key="tabs-header-overview">
									<Nav.Link eventKey="settings">
										{t('InputMacroAddon:input-macro-header-text')}
									</Nav.Link>
								</Nav.Item>
								{values.macroList.map((macro, i) => (
									<Nav.Item key={`tabs-item-macro-${i}`}>
										<Nav.Link eventKey={`macro-${i}`}>
											{(() => {
												const label = macro.macroLabel ?? '';
												return label.length === 0
													? t('InputMacroAddon:input-macro-macro-list-txt', {
															macroNumber: i + 1,
														})
													: label.length > 24
														? `${label.slice(0, 24)}...`
														: label;
											})()}
										</Nav.Link>
									</Nav.Item>
								))}
							</Nav>
							<Tab.Content>
										<Tab.Pane eventKey="settings">
											<Section
												title={t('InputMacroAddon:input-macro-header-text')}
											>
												<Row>
													<Col>
														<Table
															striped
															bordered
															hover
															className="text-center"
														>
															<thead>
																<tr>
																	<th>#</th>
																	<th>
																		{t('InputMacroAddon:table-thread-label')}
																	</th>
																	<th>
																		{t('InputMacroAddon:table-thread-type')}
																	</th>
																	<th>
																		{t(
																			'InputMacroAddon:table-thread-assigned-to',
																		)}
																	</th>
																	<th>
																		{t('InputMacroAddon:table-thread-button')}
																	</th>
																	<th>
																		{t('InputMacroAddon:table-thread-actions')}
																	</th>
																	<th>
																		{t('InputMacroAddon:table-thread-status')}
																	</th>
																</tr>
															</thead>
															<tbody>
																{values.macroList.map((macro, i) => (
																	<tr key={`macro-list-item-${i}`}>
																		<td>{i + 1}</td>
																		<td>
																			{(() => {
																				const label = macro.macroLabel ?? '';
																				return (
																					<>
																						{label.length === 0 && (
																							<em>None</em>
																						)}
																						{label.length > 0 &&
																							label.slice(0, 32)}
																						{label.length > 32 && '...'}
																					</>
																				);
																			})()}
																		</td>
																		<td>
																			{i < MACRO_REC_SLOT_COUNT ? (
																				<Badge bg="info">
																					{t(
																						'InputMacroAddon:input-macro-record-badge',
																					)}
																				</Badge>
																			) : (
																				(() => {
																					const entry = MACRO_TYPES.find(
																						(m) =>
																							m.value === macro.macroType,
																					);
																					return entry
																						? t(entry.label)
																						: `(${macro.macroType})`;
																				})()
																			)}
																		</td>
																		<td>
																			{macro.useMacroTriggerButton == 1
																				? t(
																						'InputMacroAddon:input-macro-macro-trigger-type-button',
																					)
																				: t(
																						'InputMacroAddon:input-macro-macro-trigger-type-pin',
																					)}
																		</td>
																		{macro.useMacroTriggerButton == 0 ? (
																			<td>
																				<em>---</em>
																			</td>
																		) : (
																			<td>
																				{(() => {
																					const opt =
																						BUTTON_MASKS_OPTIONS.find(
																							(b) =>
																								b.value ==
																								macro.macroTriggerButton,
																						);
																					return opt
																						? opt.label
																						: `(${macro.macroTriggerButton})`;
																				})()}
																			</td>
																		)}
																		<td>
																			{i < MACRO_REC_SLOT_COUNT
																				? (macro.hasRecording
																					? formatRecDuration(macro.recFrames, t)
																					: '---')
																				: (macro.macroInputs ?? []).length}
																		</td>
																		<td>
																			{Boolean(macro.enabled) ? (
																				<Badge bg="success">
																					{t(
																						'InputMacroAddon:input-macro-macro-enabled-badge',
																					)}
																				</Badge>
																			) : (
																				<Badge bg="danger">
																					{t(
																						'InputMacroAddon:input-macro-macro-disabled-badge',
																					)}
																				</Badge>
																			)}
																		</td>
																	</tr>
																))}
															</tbody>
														</Table>
													</Col>
												</Row>
												<hr className="mt-3" />
												<Row>
													<Col>
														<Form.Label>
															<em>
																{t('InputMacroAddon:input-macro-sub-header')}
															</em>
														</Form.Label>
													</Col>
												</Row>
												<hr className="mt-3" />
												<Row>
													<Col sm={10}>
														<Button type="submit">
															{t('Common:button-save-label')}
														</Button>
														{saveMessage ? (
															<span className="alert">{saveMessage}</span>
														) : null}
													</Col>
												</Row>
											</Section>
										</Tab.Pane>
										{values.macroList.map((macro, i) => (
											<Tab.Pane
												key={`macro-list-tab-pane-${i}`}
												eventKey={`macro-${i}`}
											>
												<Section
													title={t(
														'InputMacroAddon:input-macro-macro-list-txt',
														{ macroNumber: i + 1 },
													)}
												>
													<MacroComponent
														key={`macroList[${i}]`}
														id={`macroList[${i}]`}
														value={values.macroList?.at(i)}
														errors={errors?.macroList?.at(i)}
														translation={t}
														buttonLabelType={buttonLabelType}
														index={i}
														setFieldValue={setFieldValue}
														deleteMacroInput={(inputIdx) => {
															const inputs =
																values.macroList[i].macroInputs ?? [];
															setFieldValue(
																`macroList[${i}].macroInputs`,
																inputs.filter((_, idx) => idx !== inputIdx),
															);
														}}
														macroList={values.macroList}
													/>
													<hr className="mt-3" />
													<Button type="submit">
														{t('Common:button-save-label')}
													</Button>
													{saveMessage ? (
														<span className="alert">{saveMessage}</span>
													) : null}
												</Section>
											</Tab.Pane>
										))}
							</Tab.Content>
						</Tab.Container>
						<FormContext />
					</Form>
				</div>
			)}
		</Formik>
	);
}
