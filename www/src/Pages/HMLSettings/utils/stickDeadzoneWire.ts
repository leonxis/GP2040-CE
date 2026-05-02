/**
 * Wire format for inner/anti deadzone (AnalogOptions inner_deadzone et al., uint32):
 * - Legacy: 0–20 = whole percent (5 → 5%)
 * - Fine:    ≥200 → stored = 200 + round(percent × 10), max 20.0% → raw 400
 */
export function decodeStickDeadzoneFromDevice(raw: unknown): number {
	const r = Math.floor(Number(raw));
	if (Number.isNaN(r) || r < 0) return 0;
	if (r <= 20) return r;
	if (r >= 200) return Math.round((r - 200) / 10) / 10;
	return r;
}

export function encodeStickDeadzoneToDevice(percent: number): number {
	const p = Math.min(20, Math.max(0, percent));
	const tenths = Math.round(p * 10);
	return 200 + tenths;
}
