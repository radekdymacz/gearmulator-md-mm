import React from 'react';
import {Composition, type CalculateMetadataFunction} from 'remotion';
import defaultSpec from '../specs/md-one-screen.json';
import {Reel, type ReelProps} from './Reel';
import {FORMATS, type FormatId, type ReelSpec, timeline} from './spec';

const meta: CalculateMetadataFunction<ReelProps> = ({props}) => ({
	durationInFrames: timeline(props.spec).totalFrames,
	fps: props.spec.fps,
});

// One composition per format; the spec arrives as input props (scripts/render.mjs),
// the default spec is only for the Studio preview.
export const Root: React.FC = () => (
	<>
		{(Object.keys(FORMATS) as FormatId[]).map((format) => (
			<Composition
				key={format}
				id={`reel-${format}`}
				component={Reel}
				width={FORMATS[format].width}
				height={FORMATS[format].height}
				fps={30}
				durationInFrames={1}
				defaultProps={{spec: defaultSpec as ReelSpec, format}}
				calculateMetadata={meta}
			/>
		))}
	</>
);
