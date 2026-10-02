// Capture PNG renders of every demo kind via real Chrome + WebGPU.
// Usage: node scripts/capture_gallery.mjs <out_dir> [kind1,kind2,...]
import { chromium } from 'playwright';
import { createServer } from 'node:http';
import { readFile, mkdir, writeFile } from 'node:fs/promises';
import { join, extname } from 'node:path';

const ROOT = new URL('..', import.meta.url).pathname;
const OUT = process.argv[2] || join(ROOT, 'gallery', 'web');
const KINDS = process.argv[3]?.split(',') ?? [
    'bar', 'hist', 'pie', 'heat', 'surface', 'contour', 'hist2d', 'kde',
    'box', 'stem', 'quiver', 'subplot', 'pcm', 'violin', 'stackplot',
    'fill', 'spy', 'tripcolor', 'streamplot', 'matshow', 'pcolorfast',
    'brokenbarh', 'tricontour', 'triplot', 'specgram', 'spectrum', 'psd',
    'csd', 'xcorr', 'cohere', 'wireframe', 'trisurf', 'annotate',
    'plot3d', 'scatter3d', 'bar3d', 'quiver3d', 'errorbar3d',
    'contour3d', 'contourf3d', 'voxels', 'text3d', 'barbs',
    'groupedbar', 'figimage', 'chirp', 'mexicanhat', 'barlabel',
];

const MIME = {
    '.html': 'text/html', '.mjs': 'text/javascript',
    '.js': 'text/javascript', '.wasm': 'application/wasm',
    '.data': 'application/octet-stream',
};

const server = createServer((req, res) => {
    const p = join(ROOT, decodeURIComponent((req.url ?? '/').split('?')[0]));
    readFile(p).then(d => {
        res.writeHead(200, { 'content-type': MIME[extname(p)] ?? 'text/plain' });
        res.end(d);
    }).catch(() => { res.writeHead(404); res.end(); });
});
await new Promise(r => server.listen(0, r));
const port = server.address().port;

const browser = await chromium.launch({
    channel: 'chrome',
    args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan',
           '--no-sandbox'],
    env: {
        ...process.env,
        // Pin WebGPU to the Intel iGPU (SwiftShader uses too much RAM).
        VK_DRIVER_FILES: '/usr/share/vulkan/icd.d/intel_icd.json',
    },
});
const page = await browser.newPage();
page.on('pageerror', e => console.error('PAGEERR:', String(e)));

await mkdir(OUT, { recursive: true });
let ok = 0;
for (const kind of KINDS) {
    await page.goto(`http://localhost:${port}/demo/multi.html?p=${kind}`);
    try {
        await page.waitForFunction(
            () => window.vpResult !== undefined || window.vpError !== undefined,
            { timeout: 30_000 });
        const err = await page.evaluate(() => window.vpError);
        if (err) { console.error(`${kind}: ${err}`); continue; }
        const url = await page.evaluate(async () => {
            const px = window.vpPixels, W = 640, H = 480;
            const c = new OffscreenCanvas(W, H);
            const ctx = c.getContext('2d');
            const img = ctx.createImageData(W, H);
            img.data.set(px);
            ctx.putImageData(img, 0, 0);
            const blob = await c.convertToBlob({ type: 'image/png' });
            return await new Promise(res => {
                const r = new FileReader();
                r.onload = () => res(r.result);
                r.readAsDataURL(blob);
            });
        });
        await writeFile(join(OUT, `${kind}.png`),
                        Buffer.from(url.split(',')[1], 'base64'));
        console.log(`${kind}.png`);
        ok++;
    } catch (e) {
        console.error(`${kind}: ${String(e).split('\n')[0]}`);
    }
}
await browser.close();
server.close();
console.log(`${ok}/${KINDS.length} captured → ${OUT}`);
process.exit(ok === KINDS.length ? 0 : 1);
