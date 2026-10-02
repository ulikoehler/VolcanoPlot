// build.mjs — bundle the web package (TS + WGSL + the emscripten
// module) into dist/bundle.mjs for browser use / smoke tests.
import esbuild from 'esbuild';
import { readFileSync } from 'node:fs';
import { join } from 'node:path';

const wgslRaw = {
    name: 'wgsl-raw',
    setup(b) {
        b.onResolve({ filter: /\.wgsl\?raw$/ }, a => ({
            path: join(a.resolveDir, a.path.replace(/\?raw$/, '')),
            namespace: 'wgsl',
        }));
        b.onLoad({ filter: /.*/, namespace: 'wgsl' }, a => ({
            contents: readFileSync(a.path, 'utf8'),
            loader: 'text',
        }));
    },
};

esbuild.build({
    entryPoints: ['src/volcano.ts'],
    bundle: true,
    format: 'esm',
    outfile: 'dist/bundle.mjs',
    plugins: [wgslRaw],
    external: ['./volcanoplot.js'],
    logLevel: 'info',
});
