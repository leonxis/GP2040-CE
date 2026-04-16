import ReactSelect, {
	GroupBase,
	Props,
	type StylesConfig,
} from 'react-select';

import './CustomSelect.scss';

/** Above Bootstrap 5 modal backdrop (1055); menu must paint over HML nav/cards. */
const DEFAULT_MENU_PORTAL_Z_INDEX = 1100;

function CustomSelect<
	Option,
	IsMulti extends boolean = false,
	Group extends GroupBase<Option> = GroupBase<Option>,
>(props: Props<Option, IsMulti, Group>) {
	const { styles: userStyles, menuPortalTarget, menuPlacement, ...rest } = props;

	type MenuPortalState = Parameters<
		NonNullable<StylesConfig<Option, IsMulti, Group>['menuPortal']>
	>[1];

	const mergedStyles: StylesConfig<Option, IsMulti, Group> = {
		...userStyles,
		menuPortal: (provided, state: MenuPortalState) => ({
			...provided,
			zIndex: DEFAULT_MENU_PORTAL_Z_INDEX,
			...(userStyles?.menuPortal ? userStyles.menuPortal(provided, state) : {}),
		}),
	};

	return (
		<ReactSelect
			className="react-select__container"
			classNamePrefix="react-select"
			{...rest}
			menuPortalTarget={
				menuPortalTarget !== undefined
					? menuPortalTarget
					: typeof document !== 'undefined'
						? document.body
						: undefined
			}
			menuPlacement={menuPlacement !== undefined ? menuPlacement : 'auto'}
			styles={mergedStyles}
		/>
	);
}

export default CustomSelect;
