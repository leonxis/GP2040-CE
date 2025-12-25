import { useState, useEffect, useContext, useRef, Dispatch, SetStateAction } from 'react';
import WebApi from '../../../Services/WebApi';
import { AppContext } from '../../../Contexts/AppContext';

interface UseGamepadOptionsReturn {
	values: any;
	setValues: Dispatch<SetStateAction<any>>;
	inputMode: number;
	setInputMode: Dispatch<SetStateAction<number>>;
	isLoading: boolean;
	error: string | null;
}

export function useGamepadOptions(): UseGamepadOptionsReturn {
	const [values, setValues] = useState<any>({});
	const [inputMode, setInputMode] = useState(0);
	const [isLoading, setIsLoading] = useState(true);
	const [error, setError] = useState<string | null>(null);
	const appContext = useContext(AppContext);
	const isInitialMountRef = useRef(true);

	useEffect(() => {
		// 检查AppContext是否可用
		if (!appContext) {
			setError('应用上下文未初始化');
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
				const { setLoading, setButtonLabels } = appContext;
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
					setValues(options);
					if (setButtonLabels) {
						setButtonLabels({
							swapTpShareLabels:
								options.switchTpShareForDs4 === 1 && options.inputMode === 4,
						});
					}
					isInitialMountRef.current = false;
				} else {
					setError('无法获取游戏手柄选项数据');
				}
			} catch (err: any) {
				console.error('获取游戏手柄选项失败:', err);
				setError(
					err?.message || '获取游戏手柄选项失败，请刷新页面重试',
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

