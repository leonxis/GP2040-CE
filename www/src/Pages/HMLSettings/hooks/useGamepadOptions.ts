import { useState, useEffect, useContext, useRef, Dispatch, SetStateAction } from 'react';
import WebApi from '../../../Services/WebApi';
import { AppContext } from '../../../Contexts/AppContext';
import i18n from '../../../i18n';

type GamepadOptionsState = Record<string, number>;

type AppContextShape = {
	setLoading?: (loading: boolean) => void;
	setButtonLabels?: (labels: { swapTpShareLabels: boolean }) => void;
};

interface UseGamepadOptionsReturn {
	values: GamepadOptionsState;
	setValues: Dispatch<SetStateAction<GamepadOptionsState>>;
	inputMode: number;
	setInputMode: Dispatch<SetStateAction<number>>;
	isLoading: boolean;
	error: string | null;
}

export function useGamepadOptions(): UseGamepadOptionsReturn {
	const [values, setValues] = useState<GamepadOptionsState>({});
	const [inputMode, setInputMode] = useState(0);
	const [isLoading, setIsLoading] = useState(true);
	const [error, setError] = useState<string | null>(null);
	const appContext = useContext(AppContext);
	const isInitialMountRef = useRef(true);

	useEffect(() => {
		// 检查AppContext是否可用
		if (!appContext) {
			setError(i18n.t('SettingsPage:hml-error-app-context'));
			setIsLoading(false);
			return;
		}

		// 只在组件首次挂载时加载数据，避免在用户操作时重新加载
		// 页面刷新时组件会重新挂载，isInitialMountRef会被重置，所以会重新加载
		if (!isInitialMountRef.current) {
			return;
		}

		async function fetchData() {
			try {
				setIsLoading(true);
				setError(null);
				const { setLoading, setButtonLabels } = appContext as AppContextShape;
				const options = await WebApi.getGamepadOptions(setLoading);
				if (options) {
					// 转换数值类型
					if (options.inputMode !== undefined) {
						options.inputMode = parseInt(options.inputMode);
						setInputMode(options.inputMode);
					}
					if (options.xinputAuthType !== undefined) {
						options.xinputAuthType = parseInt(options.xinputAuthType);
					}
					if (options.ps4AuthType !== undefined) {
						options.ps4AuthType = parseInt(options.ps4AuthType);
					}
					if (options.ps4ControllerIDMode !== undefined) {
						options.ps4ControllerIDMode = parseInt(options.ps4ControllerIDMode);
					}
					if (options.switchTpShareForDs4 !== undefined) {
						options.switchTpShareForDs4 = parseInt(options.switchTpShareForDs4);
					}
					setValues(options as GamepadOptionsState);
					if (setButtonLabels) {
						setButtonLabels({
							swapTpShareLabels:
								options.switchTpShareForDs4 === 1 && options.inputMode === 4,
						});
					}
					isInitialMountRef.current = false;
				} else {
					setError(i18n.t('SettingsPage:hml-error-gamepad-options'));
				}
			} catch (err: unknown) {
				const errorMessage = err instanceof Error ? err.message : undefined;
				console.error('获取游戏手柄选项失败:', err);
				setError(
					errorMessage || i18n.t('SettingsPage:hml-error-gamepad-options-detail'),
				);
			} finally {
				setIsLoading(false);
			}
		}
		fetchData();
		// eslint-disable-next-line react-hooks/exhaustive-deps
	}, []); // 只在组件挂载时执行一次

	return { values, setValues, inputMode, setInputMode, isLoading, error };
}

