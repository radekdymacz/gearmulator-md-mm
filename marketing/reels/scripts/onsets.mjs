#!/usr/bin/env node
// Finds the music grid in new footage: prints audio onsets (RMS jumps) so you
// can set music.downbeat in a spec, and checks them against a tempo.
// usage: node scripts/onsets.mjs ../footage/<file>.mov [--from 8] [--to 20] [--bpm 120]
import {spawnSync} from 'node:child_process';

const args = process.argv.slice(2);
const file = args[0];
const opt = (k, d) => (args.includes(k) ? Number(args[args.indexOf(k) + 1]) : d);
const from = opt('--from', 0);
const to = opt('--to', from + 12);
const bpm = opt('--bpm', 0);
if (!file) {
	console.error('usage: node scripts/onsets.mjs <footage> [--from s] [--to s] [--bpm n]');
	process.exit(2);
}
const r = spawnSync(
	'ffmpeg',
	['-v', 'error', '-ss', String(from), '-t', String(to - from), '-i', file, '-af',
		'asetnsamples=n=480,astats=metadata=1:reset=1,ametadata=print:key=lavfi.astats.Overall.RMS_level:file=-', '-f', 'null', '-'],
	{encoding: 'utf8', maxBuffer: 1 << 26},
);
const lines = r.stdout.split('\n');
let prev = -120;
const onsets = [];
for (let i = 0; i + 1 < lines.length; i++) {
	const t = /pts_time:([\d.]+)/.exec(lines[i]);
	const v = /RMS_level=(-?[\d.]+|-inf)/.exec(lines[i + 1]);
	if (!t || !v) continue;
	const db = v[1] === '-inf' ? -120 : Number(v[1]);
	if (db - prev > 6 && db > -40) onsets.push(from + Number(t[1]));
	prev = db;
}
if (!onsets.length) {
	console.log('no onsets: silence in that range?');
	process.exit(0);
}
console.log(`first sound at ${onsets[0].toFixed(3)} s`);
const beat = bpm ? 60 / bpm : 0;
for (const o of onsets) {
	const b = beat ? ((o - onsets[0]) / beat).toFixed(2) : '';
	console.log(`${o.toFixed(3)}${beat ? `  beat ${b}` : ''}`);
}
if (beat) console.log('onsets near whole beats confirm the downbeat; set music.downbeat to the first sound on step 1.');
