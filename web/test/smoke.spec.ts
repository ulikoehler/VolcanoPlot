// test/smoke.spec.ts — real-browser WebGPU smoke test (Chrome +
// SwiftShader). Serves web/ statically, renders a sine wave, and
// asserts the canvas actually contains non-background pixels.
import { test, expect } from '@playwright/test';
import { createServer } from 'node:http';
import { readFileSync } from 'node:fs';
import { join } from 'node:path';

const ROOT = new URL('..', import.meta.url).pathname;
const MIME: Record<string, string> = {
    '.html': 'text/html', '.js': 'text/javascript', '.mjs': 'text/javascript',
    '.wasm': 'application/wasm', '.ts': 'text/javascript' };

let server: ReturnType<typeof createServer>;
let port = 0;

test.beforeAll(async () => {
    server = createServer((req, res) => {
        const p = join(ROOT, decodeURIComponent((req.url ?? '/').split('?')[0]));
        try {
            const body = readFileSync(p);
            res.writeHead(200, {
                'content-type': MIME[p.slice(p.lastIndexOf('.'))] ??
                                'application/octet-stream' });
            res.end(body);
        } catch { res.writeHead(404); res.end(); }
    });
    await new Promise<void>(r => server.listen(0, r));
    port = (server.address() as any).port;
});
test.afterAll(() => server.close());

test('renders a line plot to canvas pixels', async ({ page }) => {
    const errors: string[] = [];
    page.on('pageerror', e => errors.push(String(e)));
    page.on('console', m => { const t = m.text();
        if (!/404|favicon/.test(t)) errors.push('CON: ' + t.slice(0, 300)); });
    await page.goto(`http://localhost:${port}/demo/smoke.html`);
    await page.waitForFunction(
        () => (window as any).vpResult !== undefined ||
              (window as any).vpError !== undefined,
        { timeout: 30_000 });
    const res = await page.evaluate(() =>
        ({ ok: !(window as any).vpError,
           v: (window as any).vpResult ?? (window as any).vpError }));
    console.log('STATS0'); expect(res.ok, `page error: ${res.v} ${errors}`).toBe(true);

    // Read back the real render texture via copyTextureToBuffer —
    // compositing a WebGPU canvas is unreliable headless.
    const stats = await page.evaluate(async () => {
        const px = await (window as any).vpCapture();
        let nonWhite = 0, red = 0, white = 0;
        for (let i = 0; i < px.length; i += 4) {
            if (px[i]<245||px[i+1]<245||px[i+2]<245) nonWhite++;
            else white++;
            if (px[i]>150&&px[i+1]<100&&px[i+2]<100) red++;
        }
        return { nonWhite, red, white, total: px.length/4,
                 corner: [px[0],px[1],px[2],px[3]] };
    });
    console.log('PIXSTATS', JSON.stringify(stats));
    console.log('ERRS', JSON.stringify(errors));
    expect(stats.white).toBeGreaterThan(stats.total * 0.3);
    expect(stats.nonWhite).toBeGreaterThan(500);
    expect(stats.red).toBeGreaterThan(50);   // the '#e00000' sine curve
});

test('setData re-renders a changed curve (streaming path)',
     async ({ page }) => {
    await page.goto(`about:blank`);
    const errors: string[] = [];
    page.on('pageerror', e => errors.push(String(e)));
    await page.goto(`http://localhost:${port}/demo/smoke.html`);
    await page.waitForFunction(
        () => (window as any).vpResult !== undefined ||
              (window as any).vpError !== undefined,
        { timeout: 30_000 });
    const diff = await page.evaluate(async () => {
        const vp = (window as any).vp;
        const N = 256;
        const xs = new Float32Array(N), ys = new Float32Array(N);
        // shift the sine by half a period — pixels must change
        for (let i = 0; i < N; i++) {
            xs[i] = i/(N-1); ys[i] = Math.sin(i*0.08 + Math.PI);
        }
        const h = (window as any).vpLineHandle;
        vp.setData(h, xs, ys);
        const px = await vp.capture();
        const prev = (window as any).vpPixels;
        let changed = 0;
        for (let i = 0; i < px.length; i += 4)
            if (px[i] !== prev[i] || px[i+1] !== prev[i+1]) changed++;
        return changed;
    });
    expect(errors).toEqual([]);
    expect(diff).toBeGreaterThan(1000);
});

test('func() evaluates a GLSL body via WGSL compute', async ({ page }) => {
    const errors: string[] = [];
    page.on('pageerror', e => errors.push(String(e)));
    page.on('console', m => { const t = m.text();
        if (!/404|favicon/.test(t)) errors.push('CON: ' + t.slice(0, 300)); });
    await page.goto(`http://localhost:${port}/demo/func.html`);
    await page.waitForFunction(
        () => (window as any).vpResult !== undefined ||
              (window as any).vpError !== undefined,
        { timeout: 30_000 });
    const res = await page.evaluate(() =>
        ({ ok: !(window as any).vpError,
           v: (window as any).vpResult ?? (window as any).vpError }));
    expect(res.ok, `page error: ${res.v} ${errors}`).toBe(true);
    const stats = await page.evaluate(() => {
        const px = (window as any).vpPixels;
        let red = 0;
        for (let i = 0; i < px.length; i += 4)
            if (px[i]>150 && px[i+1]<100 && px[i+2]<100) red++;
        return { red };
    });
    console.log('FUNC', JSON.stringify(stats), JSON.stringify(errors));
    expect(stats.red).toBeGreaterThan(500);   // sin(10x) curve pixels
});

test('interaction: scroll-zoom changes the viewport', async ({ page }) => {
    const errors: string[] = [];
    page.on('pageerror', e => errors.push(String(e)));
    await page.goto(`http://localhost:${port}/demo/smoke.html`);
    await page.waitForFunction(() => (window as any).vpResult !== undefined,
        { timeout: 30_000 });
    const diff = await page.evaluate(async () => {
        const vp = (window as any).vp;
        const prev = (window as any).vpPixels;
        // mpl Scroll event (type 3), step +1 at canvas center → zoom in.
        vp.enableInteraction();
        const cv = document.getElementById('c') as HTMLCanvasElement;
        (vp as any).mod._vp_dispatch(3, cv.width / 2,
                                     cv.height / 2, 0, 1);
        const px = await vp.capture();
        let changed = 0;
        for (let i = 0; i < px.length; i += 4)
            if (px[i] !== prev[i] || px[i+1] !== prev[i+1]) changed++;
        return changed;
    });
    expect(errors).toEqual([]);
    expect(diff).toBeGreaterThan(100); // zoomed curve occupies new pixels
});

test('styling setters affect the render', async ({ page }) => {
    const errors: string[] = [];
    page.on('pageerror', e => errors.push(String(e)));
    await page.goto(`http://localhost:${port}/demo/smoke.html`);
    await page.waitForFunction(() => (window as any).vpResult !== undefined,
        { timeout: 30_000 });
    const res = await page.evaluate(async () => {
        const vp = (window as any).vp;
        const prev = (window as any).vpPixels;
        vp.title('Sine'); vp.xlabel('t'); vp.ylabel('y');
        vp.grid(); vp.ylim(-2, 2); vp.xscale('linear');
        const px = await vp.capture();
        let changed = 0;
        for (let i = 0; i < px.length; i += 4)
            if (px[i] !== prev[i] || px[i+1] !== prev[i+1]) changed++;
        return changed;
    });
    expect(errors).toEqual([]);
    expect(res).toBeGreaterThan(1000);  // labels + grid + wider ylim
});

test('toSvg() exports vector markup', async ({ page }) => {
    await page.goto(`http://localhost:${port}/demo/smoke.html`);
    await page.waitForFunction(() => (window as any).vpResult !== undefined,
        { timeout: 30_000 });
    const stats = await page.evaluate(() => {
        const svg = (window as any).vp.toSvg();
        const doc = new DOMParser().parseFromString(svg, 'image/svg+xml');
        const err = doc.querySelector('parsererror');
        const col = Array.from(doc.querySelectorAll('[stroke],[fill]'))
            .map(e => e.getAttribute('stroke') ?? e.getAttribute('fill'))
            .filter(c => c && c !== 'none');
        return {
            err: err ? err.textContent : null,
            paths: doc.querySelectorAll('path').length,
            // '#e00000' line → some stroke/fill must be red
            red: col.filter(c => c === '#e00000' || c === '#ff0000').length,
            len: svg.length,
        };
    });
    console.log('SVG', JSON.stringify(stats));
    expect(stats.err).toBeNull();
    expect(stats.paths).toBeGreaterThan(5);
    expect(stats.red).toBeGreaterThan(0);
});

for (const kind of ['bar', 'hist', 'pie', 'heat', 'surface',
                    'contour', 'hist2d', 'kde', 'box', 'stem',
                    'quiver', 'subplot', 'pcm', 'violin', 'stackplot',
                    'fill', 'spy', 'tripcolor', 'streamplot', 'matshow',
                    'pcolorfast', 'brokenbarh', 'tricontour', 'triplot',
                    'specgram', 'spectrum', 'psd', 'csd', 'xcorr',
                    'cohere', 'wireframe', 'trisurf']) {
    test(`renders ${kind} plot type`, async ({ page }) => {
        const errors: string[] = [];
        page.on('pageerror', e => errors.push(String(e)));
        page.on('console', m => { const t = m.text();
            if (!/404|favicon/.test(t)) errors.push('CON: ' + t.slice(0, 300)); });
        await page.goto(`http://localhost:${port}/demo/multi.html?p=${kind}`);
        await page.waitForFunction(
            () => (window as any).vpResult !== undefined ||
                  (window as any).vpError !== undefined,
            { timeout: 30_000 });
        const res = await page.evaluate(() => (window as any).vpError);
        expect(res, String(res)).toBeUndefined();
        const stats = await page.evaluate(() => {
            const px = (window as any).vpPixels;
            let nonWhite = 0, chroma = 0;
            for (let i = 0; i < px.length; i += 4) {
                if (px[i]<245||px[i+1]<245||px[i+2]<245) nonWhite++;
                const mx = Math.max(px[i],px[i+1],px[i+2]),
                          mn = Math.min(px[i],px[i+1],px[i+2]);
                if (mx - mn > 40) chroma++;
            }
            return { nonWhite, chroma };
        });
        console.log(kind.toUpperCase(), JSON.stringify(stats), JSON.stringify(errors));
        expect(stats.nonWhite).toBeGreaterThan(1000);
        // contour/quiver/streamplot/triplot/wireframe default to
        // black or monochrome — chroma n/a
        if (!['contour', 'quiver', 'streamplot', 'tricontour', 'triplot',
              'wireframe'].includes(kind))
            expect(stats.chroma).toBeGreaterThan(200);
    });
}

test('Canvas2D fallback renders without WebGPU', async ({ page }) => {
    const errors: string[] = [];
    page.on('pageerror', e => errors.push(String(e)));
    await page.goto(`http://localhost:${port}/demo/fallback.html`);
    await page.waitForFunction(
        () => (window as any).vpResult !== undefined ||
              (window as any).vpError !== undefined,
        { timeout: 30_000 });
    const res = await page.evaluate(() => (window as any).vpError);
    expect(res, String(res)).toBeUndefined();
    const stats = await page.evaluate(() => {
        const px = (window as any).vpPixels;
        let nonWhite = 0, red = 0;
        for (let i = 0; i < px.length; i += 4) {
            if (px[i]<245||px[i+1]<245||px[i+2]<245) nonWhite++;
            if (px[i]>150 && px[i+1]<100 && px[i+2]<100) red++;
        }
        return { nonWhite, red };
    });
    console.log('FALLBACK', JSON.stringify(stats), JSON.stringify(errors));
    expect(stats.nonWhite).toBeGreaterThan(1000);
    expect(stats.red).toBeGreaterThan(100);   // the '#e00000' sine curve
});
