import { test, expect } from '@playwright/test';
import { createServer } from 'node:http';
import { readFileSync } from 'node:fs';
import { join } from 'node:path';

const ROOT = new URL('..', import.meta.url).pathname;
const MIME: Record<string, string> = { '.html':'text/html', '.js':'text/javascript',
    '.mjs':'text/javascript', '.wasm':'application/wasm' };

test('capture pixels', async ({ page }) => {
    page.on('console', m => { const t = m.text(); if (!/404|favicon/.test(t)) console.log('CON:', t.slice(0, 300)); });
    page.on('pageerror', e => console.log('PAGEERR:', String(e)));
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
        // minimal probe: reuse the LIVE device from the page's vp
        const probe = await page.evaluate(async () => {
            try {
                const vp = (window as any).vp;
                const d: GPUDevice = vp.device;
                const lostP = d.lost.then(i => 'LOST:' + i.reason + ':' + i.message);
                const info = JSON.stringify(await (await navigator.gpu
                    .requestAdapter())?.info ?? 'no-adapter');
                const b = d.createBuffer({size: 256,
                    usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ});
                d.queue.writeBuffer(b, 0, new Uint8Array(256).fill(7));
                const r = await Promise.race([
                    b.mapAsync(GPUMapMode.READ).then(() => 'mapped'),
                    lostP]);
                if (r !== 'mapped') return {ok: false, err: r, info};
                const v = new Uint8Array(b.getMappedRange())[0];
                return {ok: true, v, info};
            } catch (e) { return {ok: false, err: String(e)}; }
        });
        console.log('PROBE', JSON.stringify(probe));
        const stats = await page.evaluate(async () => {
            try {
                const px = await (window as any).vpCapture();
                let nonWhite=0, red=0;
                for (let i=0;i<px.length;i+=4){
                    if(px[i]<245||px[i+1]<245||px[i+2]<245)nonWhite++;
                    if(px[i]>150&&px[i+1]<100&&px[i+2]<100)red++;
                }
                return {ok:true, nonWhite, red, len:px.length};
            } catch(e) { return {ok:false, err:String(e && (e as any).stack || e)}; }
        });
        console.log('STATS', JSON.stringify(stats));
        expect(stats.ok).toBe(true);
    } finally { server.close(); }
});
