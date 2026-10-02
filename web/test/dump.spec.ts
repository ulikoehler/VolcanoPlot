import { test } from '@playwright/test';
import { createServer } from 'node:http';
import { readFileSync } from 'node:fs';
import { join } from 'node:path';

const ROOT = new URL('..', import.meta.url).pathname;
const MIME: Record<string, string> = { '.html':'text/html', '.js':'text/javascript',
    '.mjs':'text/javascript', '.wasm':'application/wasm' };

test('dump frame ops', async ({ page }) => {
    const server = createServer((req, res) => {
        const p = join(ROOT, decodeURIComponent(req.url ?? '/'));
        try { const body = readFileSync(p);
              res.writeHead(200, {'content-type': MIME[p.slice(p.lastIndexOf('.'))] ?? 'application/octet-stream'});
              res.end(body); }
        catch { res.writeHead(404); res.end(); }
    });
    await new Promise<void>(r => server.listen(0, r));
    const port = (server.address() as any).port;
    try {
        await page.goto(`http://localhost:${port}/demo/smoke.html`);
        await page.waitForFunction(() => (window as any).vpResult || (window as any).vpError, {timeout: 30000});
        const info = await page.evaluate(async () => {
            const V = await (await import('/dist/volcanoplot.js')).default();
            V._vp_resize(640, 480);
            const N = 64;
            const px = V._vp_alloc(N*4), py = V._vp_alloc(N*4);
            const xs = new Float32Array(V.HEAPU8.buffer, px, N);
            const ys = new Float32Array(V.HEAPU8.buffer, py, N);
            for (let i=0;i<N;i++){xs[i]=i/(N-1);ys[i]=Math.sin(i*0.2);}
            V._vp_line(0, xs, ys, '#e00000');
            V._vp_render();
            const len = V._vp_frameLen(), ptr = V._vp_framePtr();
            const d = new DataView(V.HEAPU8.buffer, ptr, len);
            const ops = d.getUint32(24, true);
            const out: any[] = [];
            for (let off=40, i=0; i<ops; i++) {
                const op = d.getUint16(off,true), n = d.getUint32(off+4,true);
                const rec: any = {op, len:n};
                if (op===24) { // DrawTrisGpu
                    rec.buf=d.getUint32(off+8+24,true);
                    rec.byteOff=Number(d.getBigUint64(off+8+28,true));
                    rec.count=d.getUint32(off+8+36,true);
                    rec.clip=[0,1,2,3].map(k=>d.getFloat32(off+8+k*4,true));
                }
                if (op===40) { // TessLines
                    rec.inBuf=d.getUint32(off+8,true); rec.outBuf=d.getUint32(off+8+8,true);
                    rec.n=d.getUint32(off+8+16,true); rec.nSeg=d.getUint32(off+8+20,true);
                    rec.hwidth=d.getFloat32(off+8+24,true);
                    rec.rgba=[34,38,42,46].map(o=>d.getFloat32(off+8+o,true));
                }
                out.push(rec);
                off += 8 + ((n+3)&~3);
            }
            return {ops: out, canvasW: d.getUint32(16,true), canvasH: d.getUint32(20,true)};
        });
        console.log(JSON.stringify(info, null, 1));
        await page.screenshot({ path: 'test-results/dump.png' });
        // also dump pixel stats of the canvas
        const px = await page.evaluate(() => {
            const c = document.getElementById('c') as HTMLCanvasElement;
            const c2 = document.createElement('canvas');
            c2.width = c.width; c2.height = c.height;
            const g = c2.getContext('2d')!;
            g.drawImage(c, 0, 0);
            const im = g.getImageData(0, 0, c2.width, c2.height).data;
            let nonWhite = 0, red = 0, dark = 0;
            for (let i = 0; i < im.length; i += 4) {
                if (im[i]<245||im[i+1]<245||im[i+2]<245) nonWhite++;
                if (im[i]>150&&im[i+1]<100&&im[i+2]<100) red++;
                if (im[i]<80&&im[i+1]<80&&im[i+2]<80) dark++;
            }
            return {nonWhite, red, dark};
        });
        console.log('PIXELS', JSON.stringify(px));
    } finally { server.close(); }
});
