// Pure camera maths: from keyframed UI regions to a scale + translate of the
// whole footage. No React here.
import {Easing, interpolate} from 'remotion';
import type {CameraKey, FormatId, Region} from './spec';

export type Transform = {scale: number; x: number; y: number};

const regionFor = (k: CameraKey, format: FormatId): Region => k.byFormat?.[format] ?? k.region;

/** Interpolate the region at time t (seconds into the shot): centre linearly,
 *  size in log space so zooms feel even, eased in and out. */
export const regionAt = (keys: CameraKey[], t: number, format: FormatId): Region => {
	if (keys.length === 0) throw new Error('a shot needs at least one camera key');
	const sorted = [...keys].sort((a, b) => a.at - b.at);
	if (t <= sorted[0].at) return regionFor(sorted[0], format);
	const last = sorted[sorted.length - 1];
	if (t >= last.at) return regionFor(last, format);
	const i = sorted.findIndex((k) => k.at > t) - 1;
	const a = sorted[i];
	const b = sorted[i + 1];
	const p = interpolate(t, [a.at, b.at], [0, 1], {easing: Easing.inOut(Easing.cubic)});
	const ra = regionFor(a, format);
	const rb = regionFor(b, format);
	const lerp = (x: number, y: number) => x + (y - x) * p;
	const loglerp = (x: number, y: number) => Math.exp(lerp(Math.log(x), Math.log(y)));
	const w = loglerp(ra[2], rb[2]);
	const h = loglerp(ra[3], rb[3]);
	const cx = lerp(ra[0] + ra[2] / 2, rb[0] + rb[2] / 2);
	const cy = lerp(ra[1] + ra[3] / 2, rb[1] + rb[3] / 2);
	return [cx - w / 2, cy - h / 2, w, h];
};

/** Fit region (points) into box (output px), centred. Where the scaled footage
 *  is larger than the frame, clamp so no empty edge shows. */
export const fit = (
	region: Region,
	box: [number, number, number, number],
	ui: [number, number],
	frame: {width: number; height: number},
	cover = false,
): Transform => {
	const [rx, ry, rw, rh] = region;
	const [bx, by, bw, bh] = box;
	const contain = Math.min(bw / rw, bh / rh);
	// cover: never let the footage shrink below the frame (for 16:9, where the UI fills it)
	const scale = cover ? Math.max(contain, frame.width / ui[0], frame.height / ui[1]) : contain;
	let x = bx + bw / 2 - (rx + rw / 2) * scale;
	let y = by + bh / 2 - (ry + rh / 2) * scale;
	const fw = ui[0] * scale;
	const fh = ui[1] * scale;
	if (fw >= frame.width - 0.5) x = Math.min(0, Math.max(frame.width - fw, x));
	if (fh >= frame.height - 0.5) y = Math.min(0, Math.max(frame.height - fh, y));
	return {scale, x, y};
};

/** Map a region (points) to output pixels under a transform. */
export const project = (r: Region, t: Transform): Region => [t.x + r[0] * t.scale, t.y + r[1] * t.scale, r[2] * t.scale, r[3] * t.scale];
