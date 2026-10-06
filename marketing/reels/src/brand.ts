// Colours from site/public/design/tokens.css + site/public/assets/site.css
// (mdmm.nativekloud.com). The reels use the site's dark "hardware" surfaces and
// the dark-theme accent, which reads on near-black. Change the site first, then here.
import type {FormatId} from './spec';

export const C = {
	hw: '#161614', // --hw
	hw2: '#242421', // --hw-2
	hwKey: '#30312f', // --hw-key
	hwKey2: '#3d3f3b',
	hwText: '#ecebe4', // --hw-text
	hwMuted: '#a3a29b', // --hw-muted
	hwLine: '#34342f', // --hw-line
	led: '#ff4b2b', // --led
	accent: '#ff6a3d', // --accent (dark theme)
	accentLight: '#d63f1e', // --accent (light theme)
	cream: '#ece6d6',
	white: '#ffffff',
	black: '#000000',
};

export const FONT_SANS = "'Inter', -apple-system, 'Helvetica Neue', Arial, sans-serif";
export const FONT_MONO = "'JetBrains Mono', ui-monospace, Menlo, monospace";

/** Where things go in each format. Boxes are [x, y, w, h] in output pixels.
 *  9:16 keeps text out of the Reels/TikTok/Shorts overlays: top 250 px,
 *  bottom 420 px, right 150 px (the like/share column), left 60 px. */
export type Layout = {
	/** the box the camera fits its region into */
	camera: [number, number, number, number];
	/** the hook's text box */
	hook: [number, number, number, number];
	/** the subtitle baseline area */
	subs: [number, number, number, number];
	/** text on a scrim (it sits over footage) or on the dark ground */
	hookOnScrim: boolean;
	hookSize: number;
	subSize: number;
	endScale: number;
	/** keep the footage covering the whole frame (16:9 only: same aspect as the UI) */
	cover: boolean;
};

export const LAYOUT: Record<FormatId, Layout> = {
	'9x16': {
		camera: [0, 280, 1080, 1120],
		hook: [60, 250, 870, 280],
		subs: [60, 1330, 870, 170],
		hookOnScrim: false,
		hookSize: 80,
		subSize: 68,
		endScale: 1,
		cover: false,
	},
	'1x1': {
		camera: [0, 150, 1080, 760],
		hook: [60, 40, 960, 200],
		subs: [60, 900, 960, 140],
		hookOnScrim: true,
		hookSize: 76,
		subSize: 56,
		endScale: 0.82,
		cover: false,
	},
	'16x9': {
		camera: [0, 0, 1920, 1080],
		hook: [160, 90, 1600, 260],
		subs: [160, 860, 1600, 150],
		hookOnScrim: true,
		hookSize: 96,
		subSize: 64,
		endScale: 0.9,
		cover: true,
	},
};
