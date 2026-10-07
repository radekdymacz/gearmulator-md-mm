#!/usr/bin/env node
// One command, one reel, three formats:
//   npm run render -- specs/<id>.json [--formats 9x16,1x1,16x9] [--stills-only]
// For each format: Remotion renders out/<id>/<id>-<fmt>.raw.mp4, ffmpeg normalises
// the audio to -14 LUFS integrated with true peak at or below -1 dBTP (two-pass
// loudnorm), writes out/<id>/<id>-<fmt>.mp4, measures it again into
// out/<id>/check.txt and pulls stills (hook, each shot's middle, end card).
// Nothing is posted or uploaded: the output is files on disk.
import {spawnSync} from 'node:child_process';
import {copyFileSync, linkSync, mkdirSync, readFileSync, realpathSync, rmSync, writeFileSync} from 'node:fs';
import {basename, dirname, join, resolve} from 'node:path';
import {fileURLToPath} from 'node:url';
import {bundle} from '@remotion/bundler';
import {renderMedia, selectComposition} from '@remotion/renderer';
import {resolveBars} from './bars.mjs';
import {FOOTAGE_DIR, validateSpec} from './validate.mjs';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const LUFS = -14;
const TP_TARGET = -2.0; // aim under the -1 dBTP ceiling: AAC encoding adds a little
const TP_CEILING = -1.0;

const args = process.argv.slice(2);
const specPath = args.find((a) => a.endsWith('.json'));
if (!specPath) {
	console.error('usage: npm run render -- specs/<id>.json [--formats 9x16,1x1,16x9]');
	process.exit(2);
}
const fmtArg = args.includes('--formats') ? args[args.indexOf('--formats') + 1] : '9x16,1x1,16x9';
const formats = fmtArg.split(',');

const authored = JSON.parse(readFileSync(resolve(specPath), 'utf8'));
const {errors, warnings} = validateSpec(authored);
for (const w of warnings) console.log(`warn: ${w}`);
if (errors.length) {
	for (const m of errors) console.error(`error: ${m}`);
	process.exit(1);
}
// inBar / audioInBar → footage seconds, from the recorder's timeline.json
const spec = resolveBars(FOOTAGE_DIR, authored);

const outDir = join(ROOT, 'out', spec.id);
mkdirSync(join(outDir, 'stills'), {recursive: true});

// The bundler copies the public dir without following symlinks, and footage is
// usually a symlink into a recorder run: stage the one file the spec needs.
const stage = join(ROOT, 'out', '.public');
rmSync(stage, {recursive: true, force: true});
mkdirSync(stage, {recursive: true});
const footage = realpathSync(join(FOOTAGE_DIR, spec.footage.file));
try {
	linkSync(footage, join(stage, spec.footage.file));
} catch {
	copyFileSync(footage, join(stage, spec.footage.file));
}

console.log('bundling…');
const serveUrl = await bundle({entryPoint: join(ROOT, 'src', 'index.ts'), publicDir: stage});

const ff = (argv) => spawnSync('ffmpeg', ['-hide_banner', '-nostats', ...argv], {encoding: 'utf8', maxBuffer: 1 << 26});

const measure = (file) => {
	const r = ff(['-i', file, '-af', 'ebur128=peak=true', '-f', 'null', '-']);
	const tail = r.stderr.slice(r.stderr.lastIndexOf('Summary:'));
	const I = Number(/I:\s+(-?[\d.]+) LUFS/.exec(tail)?.[1]);
	const TP = Number(/Peak:\s+(-?[\d.]+) dBFS/.exec(tail)?.[1]);
	return {I, TP};
};

const normalise = (src, dst) => {
	const base = `loudnorm=I=${LUFS}:TP=${TP_TARGET}:LRA=11`;
	const p1 = ff(['-i', src, '-af', `${base}:print_format=json`, '-f', 'null', '-']);
	const m = JSON.parse(p1.stderr.slice(p1.stderr.lastIndexOf('{'), p1.stderr.lastIndexOf('}') + 1));
	const filter = `${base}:measured_I=${m.input_i}:measured_TP=${m.input_tp}:measured_LRA=${m.input_lra}:measured_thresh=${m.input_thresh}:offset=${m.target_offset}:linear=true,aresample=48000`;
	const p2 = ff(['-y', '-i', src, '-c:v', 'copy', '-af', filter, '-c:a', 'aac', '-b:a', '256k', '-ar', '48000', '-movflags', '+faststart', dst]);
	if (p2.status !== 0) throw new Error(`loudnorm failed: ${p2.stderr.slice(-800)}`);
};

const limit = (src, dst, gainDb) => {
	const ceiling = 10 ** ((TP_TARGET - 0.5) / 20);
	const filter = `volume=${gainDb.toFixed(2)}dB,alimiter=limit=${ceiling.toFixed(4)}:attack=1:release=60:level=false,aresample=48000`;
	const r = ff(['-y', '-i', src, '-c:v', 'copy', '-af', filter, '-c:a', 'aac', '-b:a', '256k', '-ar', '48000', '-movflags', '+faststart', dst]);
	if (r.status !== 0) throw new Error(`limiter failed: ${r.stderr.slice(-800)}`);
};

const fps = spec.fps;
const beat = 60 / spec.music.bpm;
// still times (seconds): mid-hook, the middle of each shot, the end card once it has settled
const stillTimes = () => {
	const t = [{name: 'hook', at: Math.min(1.2, spec.hook.seconds - 0.3)}];
	let s = 0;
	for (const shot of spec.shots) {
		const len = shot.beats * beat;
		if (s > 0 || len * 0.6 > spec.hook.seconds) t.push({name: shot.id, at: s + Math.max(len * 0.6, s === 0 ? spec.hook.seconds + 0.3 : 0)});
		s += len;
	}
	t.push({name: 'endcard', at: s + spec.endCard.beats * beat - 0.6});
	return t;
};

const report = [`${spec.id} — rendered ${new Date().toISOString()}`, `target ${LUFS} LUFS integrated, true peak <= ${TP_CEILING} dBTP`];
let failed = false;
for (const format of formats) {
	const inputProps = {spec, format};
	const composition = await selectComposition({serveUrl, id: `reel-${format}`, inputProps});
	const raw = join(outDir, `${spec.id}-${format}.raw.mp4`);
	const out = join(outDir, `${spec.id}-${format}.mp4`);
	console.log(`rendering ${format} (${composition.width}x${composition.height}, ${(composition.durationInFrames / fps).toFixed(2)} s)…`);
	let last = -1;
	await renderMedia({
		composition,
		serveUrl,
		codec: 'h264',
		crf: 18,
		pixelFormat: 'yuv420p',
		audioCodec: 'aac',
		audioBitrate: '320k',
		outputLocation: raw,
		inputProps,
		onProgress: ({progress}) => {
			const p = Math.floor(progress * 10);
			if (p !== last) {
				last = p;
				process.stdout.write(`${p * 10}% `);
			}
		},
	});
	process.stdout.write('\n');
	normalise(raw, out);
	let {I, TP} = measure(out);
	// loudnorm stays linear only while the peaks allow it; a quiet break before a
	// loud drop can leave it short. Then: gain plus a brick-wall limiter, nudged twice.
	let gain = LUFS - measure(raw).I;
	for (let pass = 0; pass < 3 && Math.abs(I - LUFS) > 0.3; pass++) {
		limit(raw, out, gain);
		({I, TP} = measure(out));
		gain += LUFS - I;
	}
	rmSync(raw);
	const ok = Math.abs(I - LUFS) <= 0.5 && TP <= TP_CEILING;
	failed ||= !ok;
	report.push(`${basename(out)}: ${I} LUFS, true peak ${TP} dBTP — ${ok ? 'ok' : 'OUT OF SPEC'}`);
	for (const {name, at} of stillTimes()) {
		const png = join(outDir, 'stills', `${format}-${name}.png`);
		ff(['-y', '-ss', at.toFixed(3), '-i', out, '-frames:v', '1', png]);
	}
}
writeFileSync(join(outDir, 'check.txt'), `${report.join('\n')}\n`);
console.log(report.join('\n'));
console.log(`files: ${outDir}`);
process.exit(failed ? 1 : 0);
