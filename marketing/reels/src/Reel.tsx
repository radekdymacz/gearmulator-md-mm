import React, {useEffect, useState} from 'react';
import {
	AbsoluteFill,
	Audio,
	Easing,
	OffthreadVideo,
	Sequence,
	continueRender,
	delayRender,
	interpolate,
	spring,
	staticFile,
	useCurrentFrame,
	useVideoConfig,
} from 'remotion';
import '@fontsource/inter/latin-500.css';
import '@fontsource/inter/latin-700.css';
import '@fontsource/inter/latin-800.css';
import '@fontsource/inter/latin-900.css';
import '@fontsource/jetbrains-mono/latin-600.css';
import {C, FONT_MONO, FONT_SANS, LAYOUT, type Layout} from './brand';
import {fit, project, regionAt} from './camera';
import {FORMATS, type FormatId, type ReelSpec, type Subtitle, type TimedShot, timeline, wordsOf} from './spec';

export type ReelProps = {spec: ReelSpec; format: FormatId};

const useFonts = () => {
	const [handle] = useState(() => delayRender('fonts'));
	useEffect(() => {
		const faces = ['500 1em Inter', '700 1em Inter', '800 1em Inter', '900 1em Inter', '600 1em "JetBrains Mono"'];
		Promise.all(faces.map((f) => document.fonts.load(f)))
			.then(() => continueRender(handle))
			.catch(() => continueRender(handle));
	}, [handle]);
};

export const Reel: React.FC<ReelProps> = ({spec, format}) => {
	useFonts();
	const tl = timeline(spec);
	const layout = LAYOUT[format];
	const src = staticFile(spec.footage.file);
	const fadeFrames = Math.round(spec.audio.fadeOutSeconds * spec.fps);
	return (
		<AbsoluteFill style={{backgroundColor: C.hw, fontFamily: FONT_SANS}}>
			{tl.shots.map((shot) => (
				<Sequence key={shot.id} from={shot.from} durationInFrames={shot.frames} name={shot.id}>
					<ShotView spec={spec} shot={shot} format={format} layout={layout} src={src} />
				</Sequence>
			))}
			<Sequence from={tl.endCard.from} durationInFrames={tl.endCard.frames} name="end card">
				<EndCardView spec={spec} format={format} layout={layout} />
				{tl.endCard.audioStart !== null ? (
					<Audio
						src={src}
						trimBefore={Math.round(tl.endCard.audioStart * spec.fps)}
						volume={(f) =>
							interpolate(f, [tl.endCard.frames - fadeFrames, tl.endCard.frames - 1], [1, 0], {
								extrapolateLeft: 'clamp',
								extrapolateRight: 'clamp',
							})
						}
					/>
				) : null}
			</Sequence>
			<Sequence from={0} durationInFrames={Math.round(spec.hook.seconds * spec.fps)} name="hook">
				<Hook text={spec.hook.text} layout={layout} />
			</Sequence>
		</AbsoluteFill>
	);
};

const ShotView: React.FC<{spec: ReelSpec; shot: TimedShot; format: FormatId; layout: Layout; src: string}> = ({
	spec,
	shot,
	format,
	layout,
	src,
}) => {
	const frame = useCurrentFrame();
	const {fps} = useVideoConfig();
	const t = frame / fps;
	const size = FORMATS[format];
	const tr = fit(regionAt(shot.camera, t, format), layout.camera, spec.footage.ui, size, layout.cover);
	// a short punch-in on every cut, so the cut lands on the beat visibly
	const punch = interpolate(frame, [0, 5], [1.045, 1], {extrapolateRight: 'clamp', easing: Easing.out(Easing.cubic)});
	const cx = size.width / 2;
	const cy = layout.camera[1] + layout.camera[3] / 2;
	const k = spec.footage.uiScale;
	return (
		<AbsoluteFill>
			<AbsoluteFill style={{transform: `translate(${cx}px, ${cy}px) scale(${punch}) translate(${-cx}px, ${-cy}px)`}}>
				<div
					style={{
						position: 'absolute',
						left: 0,
						top: 0,
						width: spec.footage.ui[0] * k,
						height: spec.footage.ui[1] * k,
						transformOrigin: '0 0',
						transform: `translate(${tr.x}px, ${tr.y}px) scale(${tr.scale / k})`,
						boxShadow: '0 30px 80px rgba(0,0,0,0.55)',
					}}
				>
					<OffthreadVideo
						src={src}
						trimBefore={Math.round(shot.sourceStart * spec.fps)}
						style={{width: '100%', height: '100%', display: 'block'}}
					/>
				</div>
				{shot.spotlight ? <Spotlight rect={project(shot.spotlight, tr)} at={shot.spotlightAt ?? 0} /> : null}
			</AbsoluteFill>
			{(shot.subtitles ?? []).map((s, i) => (
				<Sequence
					key={i}
					from={Math.round(s.at * fps)}
					durationInFrames={Math.max(1, Math.round((s.until - s.at) * fps))}
					layout="none"
				>
					<SubtitleLine sub={s} layout={layout} />
				</Sequence>
			))}
		</AbsoluteFill>
	);
};

const Spotlight: React.FC<{rect: [number, number, number, number]; at: number}> = ({rect, at}) => {
	const frame = useCurrentFrame();
	const {fps} = useVideoConfig();
	const o = interpolate(frame, [at * fps, at * fps + 8], [0, 1], {extrapolateLeft: 'clamp', extrapolateRight: 'clamp'});
	const pad = 10;
	return (
		<div
			style={{
				position: 'absolute',
				left: rect[0] - pad,
				top: rect[1] - pad,
				width: rect[2] + pad * 2,
				height: rect[3] + pad * 2,
				boxShadow: `0 0 0 4000px rgba(10,10,9,${0.62 * o})`,
				outline: `${4 * o}px solid ${C.accent}`,
				opacity: 1,
			}}
		/>
	);
};

/** Big, bold, high contrast; words pop in one by one; *word* takes the accent. */
const SubtitleLine: React.FC<{sub: Subtitle; layout: Layout}> = ({sub, layout}) => {
	const frame = useCurrentFrame();
	const {fps} = useVideoConfig();
	const words = wordsOf(sub.text);
	const [x, y, w, h] = layout.subs;
	const out = interpolate(frame, [(sub.until - sub.at) * fps - 4, (sub.until - sub.at) * fps], [1, 0], {
		extrapolateLeft: 'clamp',
		extrapolateRight: 'clamp',
	});
	return (
		<div
			style={{
				position: 'absolute',
				left: x,
				top: y,
				width: w,
				height: h,
				display: 'flex',
				alignItems: 'center',
				justifyContent: 'center',
				opacity: out,
			}}
		>
			<div
				style={{
					background: 'rgba(8,8,7,0.88)',
					padding: `${layout.subSize * 0.22}px ${layout.subSize * 0.4}px`,
					boxShadow: `inset 0 -${Math.round(layout.subSize * 0.07)}px 0 ${C.led}`,
					fontWeight: 800,
					fontSize: layout.subSize,
					lineHeight: 1.12,
					letterSpacing: '-0.02em',
					color: C.white,
					textAlign: 'center',
					maxWidth: w,
				}}
			>
				{words.map((raw, i) => {
					const accent = /^\*.*\*[.,!?:]?$/.test(raw);
					const word = raw.replace(/\*/g, '');
					const s = spring({frame: frame - i * 2, fps, config: {damping: 14, stiffness: 220, mass: 0.6}});
					return (
						<span
							key={i}
							style={{
								display: 'inline-block',
								marginRight: i === words.length - 1 ? 0 : '0.26em',
								transform: `translateY(${(1 - s) * 0.35}em) scale(${0.85 + 0.15 * s})`,
								opacity: Math.min(1, s * 1.4),
								color: accent ? C.accent : C.white,
							}}
						>
							{word}
						</span>
					);
				})}
			</div>
		</div>
	);
};

const Hook: React.FC<{text: string; layout: Layout}> = ({text, layout}) => {
	const frame = useCurrentFrame();
	const {fps, durationInFrames} = useVideoConfig();
	const words = wordsOf(text);
	const [x, y, w, h] = layout.hook;
	const out = interpolate(frame, [durationInFrames - 5, durationInFrames - 1], [1, 0], {
		extrapolateLeft: 'clamp',
		extrapolateRight: 'clamp',
	});
	return (
		<AbsoluteFill style={{opacity: out}}>
			{layout.hookOnScrim ? (
				<AbsoluteFill
					style={{background: 'linear-gradient(180deg, rgba(10,10,9,0.9) 0%, rgba(10,10,9,0.7) 38%, rgba(10,10,9,0) 62%)'}}
				/>
			) : null}
			<div
				style={{
					position: 'absolute',
					left: x,
					top: y,
					width: w,
					height: h,
					display: 'flex',
					flexDirection: 'column',
					justifyContent: 'center',
					alignItems: 'flex-start',
				}}
			>
				<div
					style={{
						fontFamily: FONT_MONO,
						fontWeight: 600,
						fontSize: layout.hookSize * 0.26,
						letterSpacing: '0.18em',
						textTransform: 'uppercase',
						color: C.accent,
						marginBottom: layout.hookSize * 0.22,
						display: 'flex',
						alignItems: 'center',
						gap: layout.hookSize * 0.14,
						opacity: interpolate(frame, [0, 6], [0, 1], {extrapolateRight: 'clamp'}),
					}}
				>
					<span
						style={{
							width: layout.hookSize * 0.13,
							height: layout.hookSize * 0.13,
							borderRadius: '50%',
							background: C.led,
							boxShadow: `0 0 ${layout.hookSize * 0.15}px ${C.led}`,
						}}
					/>
					Machinedrum Editor
				</div>
				<div style={{fontWeight: 900, fontSize: layout.hookSize, lineHeight: 1.02, letterSpacing: '-0.035em', color: C.white}}>
					{words.map((raw, i) => {
						const accent = /^\*.*\*[.,!?:]?$/.test(raw);
						const s = spring({frame: frame - 2 - i * 3, fps, config: {damping: 13, stiffness: 240, mass: 0.6}});
						return (
							<span
								key={i}
								style={{
									display: 'inline-block',
									marginRight: '0.24em',
									transform: `translateY(${(1 - s) * 0.4}em)`,
									opacity: Math.min(1, s * 1.5),
									color: accent ? C.accent : C.white,
								}}
							>
								{raw.replace(/\*/g, '')}
							</span>
						);
					})}
				</div>
			</div>
		</AbsoluteFill>
	);
};

/** The end card: the site's hardware ground, a row of step keys running on the
 *  beat, the product name, the price line and the URL, large. */
const EndCardView: React.FC<{spec: ReelSpec; format: FormatId; layout: Layout}> = ({spec, format, layout}) => {
	const frame = useCurrentFrame();
	const {fps} = useVideoConfig();
	const size = FORMATS[format];
	const sc = layout.endScale * (format === '16x9' ? 1 : size.width / 1080);
	const beatFrames = (60 / spec.music.bpm / 4) * fps; // sixteenth notes
	const step = Math.floor(frame / beatFrames) % 16;
	const enter = (delay: number) => spring({frame: frame - delay, fps, config: {damping: 16, stiffness: 180}});
	const e = spec.endCard;
	const keyW = (format === "9x16" ? 52 : 44) * sc;
	return (
		<AbsoluteFill style={{background: C.hw, alignItems: 'center', justifyContent: 'center'}}>
			<div
				style={{
					display: 'flex',
					flexDirection: 'column',
					alignItems: 'center',
					textAlign: 'center',
					// keep the 9:16 card above the platform's bottom overlay
					marginTop: format === '9x16' ? -170 : 0,
				}}
			>
				<div style={{display: 'flex', gap: 8 * sc, marginBottom: 60 * sc, opacity: enter(0)}}>
					{Array.from({length: 16}, (_, i) => (
						<div
							key={i}
							style={{
								width: keyW,
								height: keyW * 0.62,
								borderRadius: 4 * sc,
								background: i % 4 === 0 ? C.hwKey2 : C.hwKey,
								boxShadow: 'inset 0 -3px 0 rgba(0,0,0,.4)',
								display: 'flex',
								alignItems: 'center',
								justifyContent: 'center',
							}}
						>
							<div
								style={{
									width: keyW * 0.5,
									height: keyW * 0.12,
									borderRadius: 2,
									background: i === step ? C.led : 'rgba(255,75,43,0.16)',
									boxShadow: i === step ? `0 0 ${10 * sc}px ${C.led}` : 'none',
								}}
							/>
						</div>
					))}
				</div>
				<div
					style={{
						fontWeight: 900,
						fontSize: (format === "9x16" ? 96 : 112) * sc,
						letterSpacing: '-0.04em',
						lineHeight: 1,
						color: C.hwText,
						transform: `translateY(${(1 - enter(3)) * 30}px)`,
						opacity: enter(3),
					}}
				>
					{e.title}
				</div>
				<div
					style={{
						marginTop: 34 * sc,
						fontWeight: 700,
						fontSize: 54 * sc,
						color: C.hwText,
						display: 'flex',
						alignItems: 'center',
						gap: 18 * sc,
						opacity: enter(8),
					}}
				>
					<span style={{width: 18 * sc, height: 18 * sc, borderRadius: '50%', background: C.led}} />
					{e.line}
				</div>
				<div
					style={{
						marginTop: 70 * sc,
						fontWeight: 800,
						fontSize: 84 * sc,
						letterSpacing: '-0.02em',
						color: C.black,
						background: C.accent,
						padding: `${20 * sc}px ${36 * sc}px`,
						transform: `scale(${0.9 + 0.1 * enter(12)})`,
						opacity: enter(12),
					}}
				>
					{e.url}
				</div>
				{e.small ? (
					<div
						style={{
							marginTop: 44 * sc,
							fontFamily: FONT_MONO,
							fontWeight: 600,
							fontSize: 26 * sc,
							letterSpacing: '0.06em',
							color: C.hwMuted,
							opacity: enter(16),
							maxWidth: 940 * sc,
							lineHeight: 1.5,
						}}
					>
						{e.small}
					</div>
				) : null}
			</div>
		</AbsoluteFill>
	);
};
