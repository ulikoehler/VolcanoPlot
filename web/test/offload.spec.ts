// test/offload.spec.ts — GPU-offload correctness.
//
// Each offloadable workload must produce the same image on the GPU path
// as on the CPU path. Two things are checked per workload:
//   1. the GPU compute pass actually ran (dispatch counter), and
//   2. the rendered pixels match the CPU path.
//
// The binning ops deliver counts through the bulk mailbox one frame
// later, so GPU captures are taken after a couple of re-renders.
//
// Instrumentation has to be in place before the page's first render —
// plots cache their prepared state, so the binning compute is only
// emitted when the plot is (re)prepared. `addInitScript` installs a
// `window.vp` setter that patches the interpreter the moment the demo
// assigns it, and applies the requested offload policy before the
// demo's first render.
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

const INIT = (mode: string) => {
    (window as any).__bins = 0;
    (window as any).__contours = 0;
    (window as any).__fft = 0;
    (window as any).__p3d = 0;
    (window as any).__inst = 0;
    (window as any).__wantMode = mode;
    let stored: any;
    Object.defineProperty(window, 'vp', {
        configurable: true,
        get() { return stored; },
        set(v) {
            stored = v;
            // Apply the policy before the demo renders anything.
            if ((window as any).__wantMode)
                v.setOffload({ binning: (window as any).__wantMode,
                               contours: (window as any).__wantMode,
                               dashes: (window as any).__wantMode,
                               fft: (window as any).__wantMode,
                               instancing: (window as any).__wantMode,
                               projection3d: (window as any).__wantMode });
            const interp = v.interp;
            const orig = interp.dispatchBins.bind(interp);
            interp.dispatchBins = (...a: any[]) => {
                if (a[1] >= 45 && a[1] <= 49) (window as any).__bins++;
                return orig(...a);
            };
            // Contour extraction rides the generic compute walk.
            const oe = interp.execCompute.bind(interp);
            interp.execCompute = (...a: any[]) => {
                if (a[2] === 50) (window as any).__contours++;
                if (a[2] === 51) (window as any).__fft++;
                return oe(...a);
            };
            // 3D GPU-projection ops are draw calls (29–31).
            const od = interp.dispatchDraw.bind(interp);
            interp.dispatchDraw = (...a: any[]) => {
                if (a[3] >= 29 && a[3] <= 31) (window as any).__p3d++;
                if (a[3] === 32) (window as any).__inst++;
                return od(...a);
            };
        },
    });
};

/** Load a demo kind under a binning policy and capture after `frames`
 * re-renders. */
async function captureWith(page: any, kind: string, mode: string,
                           frames: number) {
    await page.addInitScript(INIT, mode);
    await page.goto(`http://localhost:${port}/demo/multi.html?p=${kind}`);
    await page.waitForFunction(
        () => (window as any).vpResult !== undefined ||
              (window as any).vpError !== undefined, { timeout: 30_000 });
    const err = await page.evaluate(() => String((window as any).vpError ?? ''));
    expect(err).toBe('');
    return page.evaluate(async (f: number) => {
        const vp = (window as any).vp;
        let px = await vp.capture();
        for (let i = 1; i < f; i++) px = await vp.capture();
        let nonWhite = 0, chroma = 0, sum = 0;
        for (let i = 0; i < px.length; i += 4) {
            if (px[i] < 245 || px[i + 1] < 245 || px[i + 2] < 245) nonWhite++;
            const mx = Math.max(px[i], px[i+1], px[i+2]);
            const mn = Math.min(px[i], px[i+1], px[i+2]);
            if (mx - mn > 40) chroma++;
            sum += px[i] + px[i+1] + px[i+2];
        }
        return { nonWhite, chroma, mean: sum / (px.length / 4 * 3),
                 dispatched: (window as any).__bins,
                 contours: (window as any).__contours,
                 fft: (window as any).__fft,
                 p3d: (window as any).__p3d,
                 inst: (window as any).__inst };
    }, frames);
}

for (const kind of ['hist2d', 'hexbin']) {
    test(`GPU binning matches the CPU path for ${kind}`, async ({ page }) => {
        const errs: string[] = [];
        page.on('pageerror', e => errs.push(String(e)));
        page.on('console', m => {
            const t = m.text();
            if (!/404|favicon/.test(t)) errs.push('CON: ' + t.slice(0, 200));
        });
        const cpu = await captureWith(page, kind, 'cpu', 4);
        const gpu = await captureWith(page, kind, 'gpu', 4);
        console.log(`${kind.toUpperCase()} cpu=${JSON.stringify(cpu)} ` +
                    `gpu=${JSON.stringify(gpu)}`);
        expect(errs, errs.join('\n')).toEqual([]);
        // The GPU count pass must run for 'gpu' and never for 'cpu'.
        expect(gpu.dispatched).toBeGreaterThan(0);
        expect(cpu.dispatched).toBe(0);
        // The GPU bin index is a uniform-edge affine
        // `(v - e0) * nBins / span`; a handful of samples sitting within
        // one f32 ulp of an edge can round into the neighbouring bin, so
        // the images agree exactly in coverage and to within a
        // negligible colour delta.
        expect(gpu.nonWhite).toBe(cpu.nonWhite);
        expect(gpu.chroma).toBe(cpu.chroma);
        expect(Math.abs(gpu.mean - cpu.mean)).toBeLessThan(0.05);
    });
}

for (const kind of ['contour']) {
    test(`GPU contour extraction matches the CPU path for ${kind}`,
         async ({ page }) => {
        const errs: string[] = [];
        page.on('pageerror', e => errs.push(String(e)));
        page.on('console', m => {
            const t = m.text();
            if (!/404|favicon/.test(t)) errs.push('CON: ' + t.slice(0, 200));
        });
        const cpu = await captureWith(page, kind, 'cpu', 1);
        const gpu = await captureWith(page, kind, 'gpu', 2);
        console.log(`${kind.toUpperCase()} cpu=${JSON.stringify(cpu)} ` +
                    `gpu=${JSON.stringify(gpu)}`);
        expect(errs, errs.join('\n')).toEqual([]);
        // Marching squares ran on the device only in the gpu run.
        expect(gpu.contours).toBeGreaterThan(0);
        expect(cpu.contours).toBe(0);
        // Same contour geometry → the same ink.
        expect(Math.abs(gpu.nonWhite - cpu.nonWhite) /
               Math.max(1, cpu.nonWhite)).toBeLessThan(0.05);
    });
}

test('GPU dash expansion matches the CPU stroker', async ({ page }) => {
    const errs: string[] = [];
    page.on('pageerror', e => errs.push(String(e)));
    page.on('console', m => {
        const t = m.text();
        if (!/404|favicon/.test(t)) errs.push('CON: ' + t.slice(0, 200));
    });
    const cpu = await captureWith(page, 'dashed', 'cpu', 1);
    const gpu = await captureWith(page, 'dashed', 'gpu', 2);
    console.log(`DASHED cpu=${JSON.stringify(cpu)} gpu=${JSON.stringify(gpu)}`);
    expect(errs, errs.join('\n')).toEqual([]);
    // Same dash pattern → the same ink, within a pixel or two where a
    // dash run lands on a cell boundary.
    expect(Math.abs(gpu.nonWhite - cpu.nonWhite) /
           Math.max(1, cpu.nonWhite)).toBeLessThan(0.02);
    expect(Math.abs(gpu.chroma - cpu.chroma) /
           Math.max(1, cpu.chroma)).toBeLessThan(0.02);
});

for (const kind of ['plot3d', 'scatter3d', 'bar3d', 'wireframe',
                    'quiver3d', 'errorbar3d', 'voxels', 'trisurf',
                    'mexicanhat', 'contourf3d', 'tricontour3d',
                    'tricontourf3d']) {
    test(`GPU 3D projection matches the CPU path (${kind})`,
         async ({ page }) => {
        const errs: string[] = [];
        page.on('pageerror', e => errs.push(String(e)));
        page.on('console', m => {
            const t = m.text();
            if (!/404|favicon/.test(t)) errs.push('CON: ' + t.slice(0, 200));
        });
        const cpu = await captureWith(page, kind, 'cpu', 2);
        const gpu = await captureWith(page, kind, 'gpu', 2);
        console.log(`${kind.toUpperCase()} cpu=${JSON.stringify(cpu)} ` +
                    `gpu=${JSON.stringify(gpu)}`);
        expect(errs, errs.join('\n')).toEqual([]);
        // Ops 29–31 (raw vec3 buffers projected in the VS) run only
        // under the gpu policy; 'cpu' projects on the WASM side.
        expect(gpu.p3d).toBeGreaterThan(0);
        expect(cpu.p3d).toBe(0);
        // Same geometry → same ink. Painter's order is preserved in
        // both paths; a rasterisation ulp can flip an edge pixel.
        expect(Math.abs(gpu.nonWhite - cpu.nonWhite) /
               Math.max(1, cpu.nonWhite)).toBeLessThan(0.05);
        expect(Math.abs(gpu.mean - cpu.mean)).toBeLessThan(1.0);
    });
}

for (const kind of ['bar3d', 'voxels']) {
    test(`GPU instanced boxes match the CPU face path (${kind})`,
         async ({ page }) => {
        const errs: string[] = [];
        page.on('pageerror', e => errs.push(String(e)));
        page.on('console', m => {
            const t = m.text();
            if (!/404|favicon/.test(t)) errs.push('CON: ' + t.slice(0, 200));
        });
        const cpu = await captureWith(page, kind, 'cpu', 2);
        const gpu = await captureWith(page, kind, 'gpu', 2);
        console.log(`INST-${kind.toUpperCase()} cpu=${JSON.stringify(cpu)} ` +
                    `gpu=${JSON.stringify(gpu)}`);
        expect(errs, errs.join('\n')).toEqual([]);
        // Op 32 (one 48 B record per box, cube expanded in the VS with
        // real depth) runs only under the gpu policy; 'cpu' expands +
        // painter-sorts on the WASM side.
        expect(gpu.inst).toBeGreaterThan(0);
        expect(cpu.inst).toBe(0);
        // Depth-tested occlusion replaces painter's sort — the visible
        // surface is the same, so coverage tracks closely. Face
        // shading is the same orientation table on both paths.
        expect(Math.abs(gpu.nonWhite - cpu.nonWhite) /
               Math.max(1, cpu.nonWhite)).toBeLessThan(0.08);
        expect(Math.abs(gpu.mean - cpu.mean)).toBeLessThan(1.5);
    });
}

for (const kind of ['psd', 'csd', 'cohere', 'spectrum', 'specgram']) {
    test(`GPU batched FFT matches the CPU transform (${kind})`,
         async ({ page }) => {
        const errs: string[] = [];
        page.on('pageerror', e => errs.push(String(e)));
        page.on('console', m => {
            const t = m.text();
            if (!/404|favicon/.test(t)) errs.push('CON: ' + t.slice(0, 200));
        });
        const cpu = await captureWith(page, kind, 'cpu', 2);
        const gpu = await captureWith(page, kind, 'gpu', 4);
        console.log(`${kind.toUpperCase()} cpu=${JSON.stringify(cpu)} ` +
                    `gpu=${JSON.stringify(gpu)}`);
        expect(errs, errs.join('\n')).toEqual([]);
        // The transform ran on the device only for the gpu run.
        expect(gpu.fft).toBeGreaterThan(0);
        expect(cpu.fft).toBe(0);
        // Same spectrum → the same curve/image.
        expect(Math.abs(gpu.nonWhite - cpu.nonWhite) /
               Math.max(1, cpu.nonWhite)).toBeLessThan(0.02);
        expect(Math.abs(gpu.mean - cpu.mean)).toBeLessThan(1.0);
    });
}
