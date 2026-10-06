// Resolves bar positions (shot.inBar, endCard.audioInBar) against the demo
// recorder's timeline.json into footage seconds (inSeconds, audioInSeconds).
// The recorder measures every bar line from the engine, so a reel cut by bars
// follows the real song even where the tempo drifts or the transport stopped.
//
// timeline.json (feat/demo-recorder): { raw: { offset }, bars: [{ bar, t }] }
// where t is on the video clock and raw time = t + raw.offset.
import {readFileSync} from 'node:fs';
import {join} from 'node:path';

export const loadBars = (footageDir, spec) => {
	if (!spec.footage.timeline) return null;
	const tl = JSON.parse(readFileSync(join(footageDir, spec.footage.timeline), 'utf8'));
	const offset = (tl.raw?.offset ?? 0) + (spec.footage.syncOffset ?? 0);
	const bars = [...tl.bars].sort((a, b) => a.bar - b.bar).map((b) => ({bar: b.bar, raw: b.t + offset}));
	return bars;
};

/** Footage seconds of a (fractional) bar: linear between the measured bar lines;
 *  past the last bar, the last bar's length continues. */
export const barToSeconds = (bars, bar) => {
	const i = bars.findIndex((b) => b.bar > bar);
	const lo = i === -1 ? bars.length - 2 : Math.max(0, i - 1);
	const a = bars[lo];
	const b = bars[lo + 1];
	if (!a || !b) throw new Error(`bar ${bar}: the timeline has too few bars`);
	return a.raw + (bar - a.bar) * (b.raw - a.raw) / (b.bar - a.bar);
};

/** A copy of the spec with every inBar / audioInBar turned into seconds. */
export const resolveBars = (footageDir, spec) => {
	const usesBars = spec.shots.some((s) => s.inBar !== undefined) || spec.endCard.audioInBar !== undefined;
	if (!usesBars) return spec;
	const bars = loadBars(footageDir, spec);
	if (!bars) throw new Error('shots use inBar but footage.timeline is not set');
	const out = structuredClone(spec);
	for (const s of out.shots) if (s.inBar !== undefined) s.inSeconds = barToSeconds(bars, s.inBar);
	if (out.endCard.audioInBar !== undefined) out.endCard.audioInSeconds = barToSeconds(bars, out.endCard.audioInBar);
	return out;
};
