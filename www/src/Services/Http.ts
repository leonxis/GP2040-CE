// This is modeled after Axios to make an easier transition, so this may need more buildout
// if more than simple GET and POST requests are required.

// 默认请求超时：后端 httpd 在 i2c0/显示启用时响应变慢，并发请求易挂起。
// 超时可避免前端 Promise 永远 pending。
const DEFAULT_TIMEOUT = 8000;

type GetOptions = {
	headers?: Record<string, string>;
	signal?: AbortSignal;
	timeout?: number;
};

class Http {
	// 创建带超时的 AbortSignal，同时支持外部 signal 联动中止
	private createSignal(timeout: number, externalSignal?: AbortSignal): AbortSignal {
		const controller = new AbortController();
		const timer = setTimeout(() => controller.abort(), timeout);
		if (externalSignal) {
			if (externalSignal.aborted) {
				controller.abort();
			} else {
				externalSignal.addEventListener('abort', () => controller.abort(), { once: true });
			}
		}
		controller.signal.addEventListener('abort', () => clearTimeout(timer), { once: true });
		return controller.signal;
	}

	async get(url: string, { headers = {}, signal, timeout = DEFAULT_TIMEOUT }: GetOptions = {}) {
		try {
			const response = await fetch(url, {
				method: 'GET',
				headers: {
					'Content-Type': 'application/json',
					...headers,
				},
				signal: this.createSignal(timeout, signal),
			});

			const json = await response.json();
			return Promise.resolve({ data: json });
		} catch (err) {
			return Promise.reject(err);
		}
	}

	async post(url: string, body: unknown, headers: object = {}, timeout: number = DEFAULT_TIMEOUT) {
		try {
			const response = await fetch(url, {
				method: 'POST',
				headers: {
					'Content-Type': 'application/json',
					...headers,
				},
				body: JSON.stringify(body),
				signal: this.createSignal(timeout),
			});

			const json = await response.json();
			return Promise.resolve({ data: json });
		} catch (err) {
			return Promise.reject(err);
		}
	}
}

export default new Http();
