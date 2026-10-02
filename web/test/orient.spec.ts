// test/orient.spec.ts — Y-orientation probe for data-space draws.
import { test, expect } from '@playwright/test';
import { createServer } from 'node:http';
import { readFileSync } from 'node:fs';
import { join } from 'node:path';

const ROOT = new URL('..', import.meta.url).pathname;
const MIME: Record<string, string> = {
    '.html': 'text/html', '.js': 'text/javascript', '.mjs': 'text/javascript',
    '.wasm': 'application/wasm' };

let server: ReturnType<typeof createServer>;
let port = 0;

test.beforeAll(async () => {
    server = createServer((req, res) => {
        const p = join(ROOT, decodeURIComponent((req.url ?? '/').split('?')[0]));
        try {
            const body = readFileSync(p);
            res.writeHead(200, { 'content-type':
                MIME[p.slice(p.lastIndexOf('.'))] ?? 'application/octet-stream' });
            res.end(body);
        } catch { res.writeHead(404); res.end(); }
    });
    await new Promise<void>(r => server.listen(0, r));
    port = (server.address() as any).port;
});
test.afterAll(() => server.close());

const centroid = `
    function cen(px, pred) {
        let sx = 0, sy = 0, n = 0;
        for (let y = 0; y < 480; y++)
            for (let x = 0; x < 640; x++) {
                const i = (y * 640 + x) * 4;
                if (pred(px[i], px[i+1], px[i+2])) { sx += x; sy += y; n++; }
            }
        return n ? { x: sx / n, y: sy / n, n } : null;
    }
`;

test('line y=x centroid should be upper-right region after viewport',
     async ({ page }) => {
    await page.goto(`http://localhost:${port}/demo/smoke.html`);
    await page.waitForFunction(() => (window as any).vpResult !== undefined,
        { timeout: 30_000 });
    const res = await page.evaluate(async (cenSrc) => {
        eval(cenSrc);
        const vp = (window as any).vp;
        const N = 64;
        const xs = new Float32Array(N), ys = new Float32Array(N);
        for (let i = 0; i < N; i++) { xs[i] = i / (N - 1); ys[i] = xs[i]; }
        // y=x: right end at top of axes. Line centroid should be
        // above the axes center vertically.
        vp.line(xs, ys, '#00e000');
        const px = await vp.capture();
        return {
            line: cen(px, (r: number, g: number, b: number) =>
                g > 150 && r < 100 && b < 100),
        };
    }, centroid);
    console.log('LINE-CEN', JSON.stringify(res));
    // axes rect ~[80..576]x[58..428]; y=x line centroid must be above
    // canvas mid-height (y < 243)
    expect(res.line!.y).toBeLessThan(243);
});

test('axhspan(0.6,0.9) lands at top of axes, (-0.9,-0.6) at bottom',
     async ({ page }) => {
    await page.goto(`http://localhost:${port}/demo/smoke.html`);
    await page.waitForFunction(() => (window as any).vpResult !== undefined,
        { timeout: 30_000 });
    const res = await page.evaluate(async (cenSrc) => {
        eval(cenSrc);
        const vp = (window as any).vp;
        const N = 64;
        const xs = new Float32Array(N), ys = new Float32Array(N);
        for (let i = 0; i < N; i++) { xs[i] = i / (N - 1); ys[i] = xs[i]; }
        vp.ylim(-1, 1); vp.xlim(0, 1);
        vp.axhspan(0.6, 0.9, '#00e0e0');
        vp.axhspan(-0.9, -0.6, '#e000e0');
        const px = await vp.capture();
        return {
            cyan: cen(px, (r: number, g: number, b: number) =>
                g > 150 && b > 150 && r < 100),
            magenta: cen(px, (r: number, g: number, b: number) =>
                r > 150 && b > 150 && g < 100),
        };
    }, centroid);
    console.log('SPAN-CEN', JSON.stringify(res));
    // ylim [-1,1]: band 0.6..0.9 → frac 0.8..0.95 → top of axes
    // (rows ~58+0.05*370 .. 58+0.2*370 = 76..132); centroid < 243.
    expect(res.cyan!.y).toBeLessThan(243);
    expect(res.magenta!.y).toBeGreaterThan(243);
});
