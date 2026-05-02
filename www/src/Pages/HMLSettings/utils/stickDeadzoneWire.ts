/**
 * Inner/anti deadzone (uint32): 0–200 = tenths of a percent.
 * UI percent = value/10; device normalized dead zone = value/1000.
 */
export function decodeStickDeadzoneFromDevice(raw: unknown): number {
	const n = Number(raw);
	if (Number.isNaN(n)) return 0;
	const r = Math.min(200, Math.max(0, Math.floor(n)));
	return Math.min(20, r / 10);
}

export function encodeStickDeadzoneToDevice(percent: number): number {
	const p = Math.min(20, Math.max(0, percent));
	return Math.round(p * 10);
}
