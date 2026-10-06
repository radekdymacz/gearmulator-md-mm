// The reel contract (schema "mdmm.reel/1"). A reel is data: this file holds the
// types and the pure timeline maths; the components only draw what it returns.
// Times in a spec are seconds unless the field name says beats.
// Regions are in UI points of the recorded editor window (1440 x 810 for the
// standard recorder window); footage.uiScale maps points to footage pixels.

export type Region = [x: number, y: number, w: number, h: number];

export type FormatId = '9x16' | '1x1' | '16x9';

export type CameraKey = {
	/** seconds from the start of the shot */
	at: number;
	/** the UI region to frame (points) */
	region: Region;
	/** optional per-format override, e.g. a tighter frame for 9:16 */
	byFormat?: Partial<Record<FormatId, Region>>;
};

export type Subtitle = {
	/** seconds from the start of the shot */
	at: number;
	until: number;
	/** at most 6 words; wrap a word in *stars* to colour it with the accent */
	text: string;
};

export type Shot = {
	id: string;
	/** where the shot starts in the footage: a beat index on the music grid ... */
	inBeat?: number;
	/** ... or plain seconds (for footage before the music starts) */
	inSeconds?: number;
	/** shot length in beats (cuts land on the beat) */
	beats: number;
	camera: CameraKey[];
	/** dim everything but this region (points), fading in at spotlightAt */
	spotlight?: Region;
	spotlightAt?: number;
	subtitles?: Subtitle[];
};

export type EndCard = {
	beats: number;
	/** where the end card's audio comes from (a beat index); omit for silence */
	audioInBeat?: number;
	title: string;
	line: string;
	/** optional and off by default (Radek, 2026-10-06: no website link in the reels yet).
	 *  When set: a bare domain (no https, no UTM); the card is read, not clicked. */
	url?: string;
	/** small honest print under the URL */
	small?: string;
};

export type ReelSpec = {
	schema: 'mdmm.reel/1';
	id: string;
	title: string;
	status: 'idea' | 'needs-footage' | 'draft' | 'approved' | 'posted';
	machine: 'md' | 'mm';
	fps: number;
	footage: {
		/** file name inside marketing/footage/ (gitignored; a symlink or a copy) */
		file: string;
		uiScale: number;
		/** UI size in points (the recorded window) */
		ui: [w: number, h: number];
	};
	music: {bpm: number; downbeat: number};
	hook: {text: string; seconds: number};
	shots: Shot[];
	endCard: EndCard;
	audio: {fadeOutSeconds: number};
	/** planning notes; the renderer ignores them */
	brief?: {
		idea: string;
		/** id in marketing/FOOTAGE-REQUESTS.md */
		footageRequest?: string;
		/** "paid-ok" or "organic-only" (never boosted or used in an ad) */
		use?: 'paid-ok' | 'organic-only';
		openQuestions?: string[];
	};
	/** everything a post needs; never posted by the pipeline */
	post?: {
		link: string;
		caption?: string;
		notes?: string;
	};
};

export const FORMATS: Record<FormatId, {width: number; height: number}> = {
	'9x16': {width: 1080, height: 1920},
	'1x1': {width: 1080, height: 1080},
	'16x9': {width: 1920, height: 1080},
};

export const beatSeconds = (spec: ReelSpec) => 60 / spec.music.bpm;

export const shotSourceStart = (spec: ReelSpec, shot: Shot) =>
	shot.inSeconds ?? spec.music.downbeat + (shot.inBeat ?? 0) * beatSeconds(spec);

export type TimedShot = Shot & {from: number; frames: number; sourceStart: number};

export type Timeline = {
	shots: TimedShot[];
	endCard: {from: number; frames: number; audioStart: number | null};
	totalFrames: number;
};

/** Frame positions of every shot and the end card. Cut frames are rounded
 *  from the cumulative beat time, so rounding never drifts across the reel. */
export const timeline = (spec: ReelSpec): Timeline => {
	const b = beatSeconds(spec);
	const toFrame = (s: number) => Math.round(s * spec.fps);
	let beatsSoFar = 0;
	const shots = spec.shots.map((shot) => {
		const from = toFrame(beatsSoFar * b);
		beatsSoFar += shot.beats;
		const frames = toFrame(beatsSoFar * b) - from;
		return {...shot, from, frames, sourceStart: shotSourceStart(spec, shot)};
	});
	const endFrom = toFrame(beatsSoFar * b);
	beatsSoFar += spec.endCard.beats;
	const endFrames = toFrame(beatsSoFar * b) - endFrom;
	const audioStart =
		spec.endCard.audioInBeat === undefined ? null : spec.music.downbeat + spec.endCard.audioInBeat * b;
	return {shots, endCard: {from: endFrom, frames: endFrames, audioStart}, totalFrames: endFrom + endFrames};
};

export const wordsOf = (text: string) => text.trim().split(/\s+/).filter(Boolean);
