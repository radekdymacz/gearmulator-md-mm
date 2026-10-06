// Studio settings (`npm run studio`). Rendering goes through scripts/render.mjs,
// which sets the same public dir.
import {Config} from '@remotion/cli/config';

Config.setPublicDir('../footage');
Config.setVideoImageFormat('jpeg');
