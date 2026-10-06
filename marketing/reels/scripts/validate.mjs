#!/usr/bin/env node
// Checks a reel spec against the house rules before anything renders.
// usage: node scripts/validate.mjs specs/<id>.json [...]
// Exit 1 on an error; warnings only print.
import {existsSync, readFileSync} from 'node:fs';
import {dirname, join, resolve} from 'node:path';
import {fileURLToPath} from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
export const FOOTAGE_DIR = resolve(ROOT, '..', 'footage');

const words = (t) => t.replace(/\*/g, '').trim().split(/\s+/).filter(Boolean);

export const validateSpec = (spec, {requireFootage = true} = {}) => {
	const errors = [];
	const warnings = [];
	const e = (m) => errors.push(m);
	const w = (m) => warnings.push(m);
	if (spec.schema !== 'mdmm.reel/1') e(`schema must be "mdmm.reel/1", got ${spec.schema}`);
	for (const k of ['id', 'title', 'status', 'machine', 'fps', 'footage', 'music', 'hook', 'shots', 'endCard', 'audio'])
		if (spec[k] === undefined) e(`missing ${k}`);
	if (errors.length) return {errors, warnings};

	const b = 60 / spec.music.bpm;
	const beats = spec.shots.reduce((n, s) => n + s.beats, 0) + spec.endCard.beats;
	const seconds = beats * b;
	if (seconds < 15 || seconds > 20) w(`length ${seconds.toFixed(1)} s (aim for 15–20 s)`);
	if (spec.hook.seconds > 2.05) e(`hook is ${spec.hook.seconds} s; the hook must land within 2 s`);
	if (words(spec.hook.text).length > 7) e(`hook "${spec.hook.text}" is over 7 words`);
	const firstShot = spec.shots[0];
	if (firstShot && firstShot.beats * b < spec.hook.seconds) e('the first shot must last at least as long as the hook');

	let prevOut = null;
	for (const s of spec.shots) {
		if (!s.camera?.length) e(`shot ${s.id}: no camera keys`);
		if (s.inBeat === undefined && s.inSeconds === undefined) e(`shot ${s.id}: needs inBeat or inSeconds`);
		const len = s.beats * b;
		for (const sub of s.subtitles ?? []) {
			const n = words(sub.text).length;
			if (n > 6) e(`shot ${s.id}: subtitle "${sub.text}" has ${n} words (max 6)`);
			if (sub.until > len + 0.01) e(`shot ${s.id}: subtitle "${sub.text}" ends after the shot (${sub.until} > ${len.toFixed(2)})`);
			if (sub.until - sub.at < 0.6) w(`shot ${s.id}: subtitle "${sub.text}" is on screen under 0.6 s`);
		}
		// the groove stays in time across a cut when the source jumps by whole bars
		if (prevOut !== null && s.inBeat !== undefined && (((s.inBeat - prevOut) % 4) + 4) % 4 !== 0)
			w(`shot ${s.id}: source jumps ${s.inBeat - prevOut} beats (not whole bars); the groove will skip at the cut`);
		prevOut = s.inBeat !== undefined ? s.inBeat + s.beats : null;
	}
	const ec = spec.endCard;
	if (prevOut !== null && ec.audioInBeat !== undefined && (((ec.audioInBeat - prevOut) % 4) + 4) % 4 !== 0)
		w(`end card audio jumps ${ec.audioInBeat - prevOut} beats (not whole bars)`);
	// endCard.url is optional and off by default (no website link in the reels yet)
	if (ec.url !== undefined && !/^[a-z0-9.-]+\.[a-z]+$/.test(ec.url)) e(`end card url "${ec.url}" should be a bare domain (no https://, no UTM: the card is read, not clicked)`);
	if (ec.beats * b < 2.5) w('end card under 2.5 s: too short to read');
	if (spec.post?.link && !/utm_source=.+&utm_medium=.+&utm_campaign=.+&utm_content=.+/.test(spec.post.link))
		e('post.link must carry utm_source, utm_medium, utm_campaign and utm_content (marketing/UTM.md)');
	const banned = /\b(official|endorsed|approved by elektron|by elektron)\b/i;
	for (const t of [spec.hook.text, ec.title, ec.line, ...spec.shots.flatMap((s) => (s.subtitles ?? []).map((x) => x.text))])
		if (banned.test(t)) e(`"${t}" implies an endorsement (BRAND.md)`);
	if (requireFootage && !existsSync(join(FOOTAGE_DIR, spec.footage.file)))
		e(`footage ${spec.footage.file} not in marketing/footage/ (symlink or copy it; see FOOTAGE-REQUESTS.md)`);
	return {errors, warnings, seconds};
};

if (process.argv[1] === fileURLToPath(import.meta.url)) {
	const files = process.argv.slice(2);
	if (!files.length) {
		console.error('usage: node scripts/validate.mjs specs/<id>.json [...]');
		process.exit(2);
	}
	let bad = false;
	for (const f of files) {
		const spec = JSON.parse(readFileSync(f, 'utf8'));
		const needFootage = !['idea', 'needs-footage'].includes(spec.status);
		const {errors, warnings, seconds} = validateSpec(spec, {requireFootage: needFootage});
		console.log(`${f}: ${errors.length ? 'FAIL' : 'ok'}${seconds ? ` (${seconds.toFixed(1)} s)` : ''}`);
		for (const m of errors) console.log(`  error: ${m}`);
		for (const m of warnings) console.log(`  warn:  ${m}`);
		bad ||= errors.length > 0;
	}
	process.exit(bad ? 1 : 0);
}
