import { useTranslation } from 'react-i18next';
import FormSelect from './FormSelect';
import { KEY_CODES } from '../Data/Keyboard';
import { getButtonLabels } from '../Data/Buttons';

const KeyboardMapper = ({
	buttonLabels,
	handleKeyChange,
	getKeyMappingForButton,
	excludeButtons = [''],
}) => {
	const { buttonLabelType, swapTpShareLabels } = buttonLabels;
	const buttons = getButtonLabels(buttonLabelType, swapTpShareLabels);

	const { t } = useTranslation('Components', { keyPrefix: 'keyboard-mapper' });

	return (
		<table className="table table-sm pin-mapping-table">
			<thead className="table">
				<tr>
					<th className="table-header-button-label">
						{buttons.label}
					</th>
					<th>{t('key-header')}</th>
				</tr>
			</thead>
			<tbody>
				{Object.keys(buttons)
					?.filter((btn) => !['label', 'value', 'Fn'].includes(btn))
					.filter((btn) => !excludeButtons?.find((x) => x === btn))
					.map((button, i) => {
						const label = buttons[button];
						const keyMapping = getKeyMappingForButton(button);
						return (
							<tr key={`button-map-${i}`}>
								<td>{label}</td>
								<td>
									<FormSelect
										className="form-select-sm sm-3"
										value={keyMapping.key}
										onChange={(e) =>
											handleKeyChange(
												e.target.value ? parseInt(e.target.value) : '',
												button,
											)
										}
									>
										{KEY_CODES.map((o, i) => (
											<option key={`button-key-option-${i}`} value={o.value}>
												{o.label}
											</option>
										))}
									</FormSelect>
								</td>
							</tr>
						);
					})}
			</tbody>
		</table>
	);
};

export default KeyboardMapper;
