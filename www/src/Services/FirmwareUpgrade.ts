const FIRMWARE_DOWNLOAD_URL = 'https://home.chobits.site:51000/GNS/updata.uf2';
const LATEST_INFO_URL = 'https://home.chobits.site:51000/GNS/latest.json';
const PROXY_FIRMWARE_URL = '/api/proxyFirmware';
const PROXY_LATEST_INFO_URL = '/api/proxyLatestInfo';
const LATEST_INFO_KEY = 'GNS2040';
const LATEST_REQUEST_TIMEOUT_MS = 10000;
const FIRMWARE_REQUEST_TIMEOUT_MS = 60000;
const TARGET_UF2_FILENAME = 'GP2040-CE.uf2';
const CACHE_DB_NAME = 'gp2040_firmware_cache';
const CACHE_DB_VERSION = 1;
const CACHE_STORE = 'firmware';
const CACHE_VERSION_KEY = 'version';
const CACHE_BLOB_KEY = 'blob';

export type LatestInfo = {
	version: string;
	releaseNote: string;
};

export type ProgressCallback = (progressPercent: number) => void;

export function supportsFileSystemAccess(): boolean {
	return typeof window !== 'undefined' && 'showDirectoryPicker' in window;
}

function compareVersionParts(left: string, right: string): number {
	const leftParts = left.split('.').map((part) => Number(part));
	const rightParts = right.split('.').map((part) => Number(part));
	const maxLength = Math.max(leftParts.length, rightParts.length);

	for (let i = 0; i < maxLength; i += 1) {
		const lv = Number.isFinite(leftParts[i]) ? leftParts[i] : 0;
		const rv = Number.isFinite(rightParts[i]) ? rightParts[i] : 0;
		if (lv > rv) return 1;
		if (lv < rv) return -1;
	}
	return 0;
}

function isValidPercent(value: number) {
	return Number.isFinite(value) && value >= 0 && value <= 100;
}

function emitProgress(callback: ProgressCallback | undefined, value: number) {
	if (!callback) return;
	callback(isValidPercent(value) ? value : 0);
}

async function fetchWithTimeout(
	url: string,
	options: RequestInit = {},
	timeoutMs: number,
): Promise<Response> {
	const controller = new AbortController();
	const timer = setTimeout(() => controller.abort(), timeoutMs);

	try {
		return await fetch(url, { ...options, signal: controller.signal });
	} finally {
		clearTimeout(timer);
	}
}

async function openCacheDb(): Promise<IDBDatabase> {
	return await new Promise((resolve, reject) => {
		const request = indexedDB.open(CACHE_DB_NAME, CACHE_DB_VERSION);
		request.onupgradeneeded = () => {
			const db = request.result;
			if (!db.objectStoreNames.contains(CACHE_STORE)) {
				db.createObjectStore(CACHE_STORE);
			}
		};
		request.onsuccess = () => resolve(request.result);
		request.onerror = () => reject(request.error ?? new Error('Failed to open cache DB'));
	});
}

async function getCacheValue<T>(key: string): Promise<T | null> {
	const db = await openCacheDb();
	return await new Promise((resolve, reject) => {
		const tx = db.transaction(CACHE_STORE, 'readonly');
		const store = tx.objectStore(CACHE_STORE);
		const request = store.get(key);
		request.onsuccess = () => resolve((request.result as T | undefined) ?? null);
		request.onerror = () => reject(request.error ?? new Error('Failed to read cache'));
		tx.oncomplete = () => db.close();
	});
}

async function setCacheValue(key: string, value: unknown): Promise<void> {
	const db = await openCacheDb();
	await new Promise<void>((resolve, reject) => {
		const tx = db.transaction(CACHE_STORE, 'readwrite');
		const store = tx.objectStore(CACHE_STORE);
		store.put(value, key);
		tx.oncomplete = () => resolve();
		tx.onerror = () => reject(tx.error ?? new Error('Failed to write cache'));
	});
	db.close();
}

function parseLatestResponseData(data: unknown): LatestInfo {
	if (typeof data === 'object' && data !== null) {
		const top = data as { version?: unknown; release_note?: unknown };
		if (typeof top.version === 'string' && typeof top.release_note === 'string') {
			return { version: top.version, releaseNote: top.release_note };
		}
	}
	const nested = data as Record<string, { version?: unknown; release_note?: unknown }>;
	const item = nested?.[LATEST_INFO_KEY];
	const version = item?.version;
	const releaseNote = item?.release_note;
	if (typeof version !== 'string' || typeof releaseNote !== 'string') {
		throw new Error('Invalid latest.json format');
	}
	return { version, releaseNote };
}

export async function checkLatestInfo(url = LATEST_INFO_URL): Promise<LatestInfo> {
	const urls = [PROXY_LATEST_INFO_URL, url];
	let lastError: Error | null = null;
	for (const candidateUrl of urls) {
		try {
			const response = await fetchWithTimeout(
				candidateUrl,
				{ method: 'GET' },
				LATEST_REQUEST_TIMEOUT_MS,
			);
			if (!response.ok) {
				throw new Error(`Latest info request failed: ${response.status}`);
			}
			const data = await response.json();
			return parseLatestResponseData(data);
		} catch (error) {
			lastError = error instanceof Error ? error : new Error('Unknown latest info error');
		}
	}
	throw lastError ?? new Error('Failed to request latest info');
}

export async function checkFirmwareAvailable(url = FIRMWARE_DOWNLOAD_URL): Promise<boolean> {
	try {
		const headResponse = await fetchWithTimeout(
			url,
			{ method: 'HEAD' },
			FIRMWARE_REQUEST_TIMEOUT_MS,
		);
		if (headResponse.ok) {
			return true;
		}
	} catch {
		// Fallback to GET request, as some servers reject HEAD.
	}

	try {
		const getResponse = await fetchWithTimeout(
			url,
			{ method: 'GET', headers: { Range: 'bytes=0-0' } },
			FIRMWARE_REQUEST_TIMEOUT_MS,
		);
		return getResponse.ok;
	} catch {
		return false;
	}
}

export async function getCachedFirmwareVersion(): Promise<string | null> {
	return await getCacheValue<string>(CACHE_VERSION_KEY);
}

export async function getCachedFirmwareBlob(): Promise<Blob | null> {
	return await getCacheValue<Blob>(CACHE_BLOB_KEY);
}

export async function cacheFirmware(blob: Blob, version: string): Promise<void> {
	await setCacheValue(CACHE_BLOB_KEY, blob);
	await setCacheValue(CACHE_VERSION_KEY, version);
}

export async function shouldDownloadFirmware(serverVersion: string): Promise<boolean> {
	const localVersion = await getCachedFirmwareVersion();
	const cachedBlob = await getCachedFirmwareBlob();
	if (!localVersion || !cachedBlob) {
		return true;
	}
	return compareVersionParts(localVersion, serverVersion) < 0;
}

export async function pickBootDrive(): Promise<FileSystemDirectoryHandle> {
	if (!supportsFileSystemAccess()) {
		throw new Error('Browser does not support File System Access API');
	}

	const pickerWindow = window as unknown as Window & {
		showDirectoryPicker: (options?: { mode?: 'read' | 'readwrite' }) => Promise<FileSystemDirectoryHandle>;
	};

	return await pickerWindow.showDirectoryPicker({
		mode: 'readwrite',
	});
}

export async function validateBootDrive(dirHandle: FileSystemDirectoryHandle): Promise<boolean> {
	try {
		const infoFileHandle = await dirHandle.getFileHandle('INFO_UF2.TXT');
		const infoFile = await infoFileHandle.getFile();
		const text = (await infoFile.text()).toUpperCase();
		return text.includes('UF2') || text.includes('RP2040') || text.includes('RPI-RP2');
	} catch {
		return false;
	}
}

export async function downloadFirmwareWithProgress(
	url = FIRMWARE_DOWNLOAD_URL,
	onProgress?: ProgressCallback,
): Promise<Blob> {
	const urls = [PROXY_FIRMWARE_URL, url];
	let response: Response | null = null;
	for (const candidateUrl of urls) {
		try {
			const current = await fetchWithTimeout(
				candidateUrl,
				{ method: 'GET' },
				FIRMWARE_REQUEST_TIMEOUT_MS,
			);
			if (current.ok && current.body) {
				response = current;
				break;
			}
		} catch {
			// Try next URL.
		}
	}
	if (!response || !response.body) {
		throw new Error('Firmware download failed');
	}

	const contentLength = Number(response.headers.get('Content-Length') || 0);
	const reader = response.body.getReader();
	const chunks: Uint8Array[] = [];
	let received = 0;

	emitProgress(onProgress, 0);

	let done = false;
	while (!done) {
		const { done: readDone, value } = await reader.read();
		if (readDone) {
			done = true;
			continue;
		}
		if (value) {
			chunks.push(value);
			received += value.length;
			if (contentLength > 0) {
				emitProgress(onProgress, Math.min((received / contentLength) * 100, 100));
			}
		}
	}

	emitProgress(onProgress, 100);
	return new Blob(chunks as BlobPart[], { type: 'application/octet-stream' });
}

export async function writeUf2ToBootDrive(
	dirHandle: FileSystemDirectoryHandle,
	blob: Blob,
	onProgress?: ProgressCallback,
	fileName = TARGET_UF2_FILENAME,
): Promise<void> {
	const fileHandle = await dirHandle.getFileHandle(fileName, { create: true });
	const writable = await fileHandle.createWritable();
	const reader = blob.stream().getReader();
	const total = blob.size;
	let written = 0;

	emitProgress(onProgress, 0);

	try {
		let done = false;
		while (!done) {
			const { done: readDone, value } = await reader.read();
			if (readDone) {
				done = true;
				continue;
			}
			if (value) {
				await writable.write(value);
				written += value.length;
				if (total > 0) {
					emitProgress(onProgress, Math.min((written / total) * 100, 100));
				}
			}
		}
		await writable.close();
		emitProgress(onProgress, 100);
	} catch (error) {
		await writable.abort();
		throw error;
	}
}

export default {
	FIRMWARE_DOWNLOAD_URL,
	LATEST_INFO_URL,
	PROXY_FIRMWARE_URL,
	PROXY_LATEST_INFO_URL,
	TARGET_UF2_FILENAME,
	supportsFileSystemAccess,
	getCachedFirmwareVersion,
	getCachedFirmwareBlob,
	cacheFirmware,
	shouldDownloadFirmware,
	checkLatestInfo,
	checkFirmwareAvailable,
	pickBootDrive,
	validateBootDrive,
	downloadFirmwareWithProgress,
	writeUf2ToBootDrive,
};
