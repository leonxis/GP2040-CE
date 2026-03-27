import { useState, useEffect, useContext } from 'react';
import WebApi, { baseButtonMappings } from '../../../Services/WebApi';
import { AppContext } from '../../../Contexts/AppContext';

export function useKeyMappings() {
	const [keyMappings, setKeyMappings] = useState(baseButtonMappings);
	const appContext = useContext(AppContext);

	useEffect(() => {
		async function fetchData() {
			if (!appContext) {
				return;
			}
			const { setLoading } = appContext;
			try {
				const mappings = await WebApi.getKeyMappings(setLoading);
				if (mappings) {
					setKeyMappings(mappings);
				}
			} catch (err) {
				console.error('获取键盘映射失败:', err);
			}
		}
		fetchData();

	}, []); // 只在组件挂载时执行一次

	const handleKeyChange = (value: number, button: string) => {
		const newMappings = { ...keyMappings };
		newMappings[button].key = value;
		setKeyMappings(newMappings);
	};

	const getKeyMappingForButton = (button: string) => keyMappings[button];

	return {
		keyMappings,
		setKeyMappings,
		handleKeyChange,
		getKeyMappingForButton,
	};
}

