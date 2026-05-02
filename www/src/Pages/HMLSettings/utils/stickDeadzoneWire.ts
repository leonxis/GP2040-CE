/**
 * Persisted inner/anti deadzone (uint32): 0–200 = tenths of a percent (display = value/10).
 * Normalized dead zone on device = value/1000.
 *
 * Migrates old profiles: raw ≤20 → legacy whole percent ×10; 200–400 → subtract 200 (prior 200+tenths wire).
 */
function migrateToTenths(r: number): number {
	if (r <= 20) return r * 10;
	if (r >= 200 && r <= 400) return r - 200;
	return Math.min(200, r);
}

export function decodeStickDeadzoneFromDevice(raw: unknown): number {
	const r = Math.floor(Number(raw));
	if (Number.isNaN(r) || r < 0) return 0;
	const t = migrateToTenths(r);
	return Math.min(20, t / 10);
}

/** Writes canonical 0–200 tenths (e.g. 5.5% → 55). */
export function encodeStickDeadzoneToDevice(percent: number): number {
	const p = Math.min(20, Math.max(0, percent));
	return Math.round(p * 10);
}
