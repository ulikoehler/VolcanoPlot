import { test } from '@playwright/test';
import { createServer } from 'node:http';
import { readFileSync } from 'node:fs';
import { join } from 'node:path';

const ROOT = new URL('..', import.meta.url).pathname;
const MIME: Record<string, string> = { '.html':'text/html', '.js':'text/javascript',
    '.mjs':'text/javascript', '.wasm':'application/wasm' };

for (const q of ['stage=0&pass=1', 'stage=0']) {
    test(`device survival ${q}`, async ({ page }) => {
        page.on('console', m => { const t = m.text();
            if (!/404|favicon/.test(t)) console.log('CON:', t.slice(0,250)); });
        const server = createServer((req, res) => {
            const p = join(ROOT, decodeURIComponent((req.url ?? '/').split('?')[0]));
            try { const b = readFileSync(p);
                  res.writeHead(200, {'content-type': MIME[p.slice(p.lastIndexOf('.'))] ?? 'application/octet-stream'});
                  res.end(b); } catch { res.writeHead(404); res.end(); }
        });
        await new Promise<void>(r => server.listen(0, r));
        const port = (server.address() as any).port;
        try {
            await page.goto(`http://localhost:${port}/demo/probe.html?${q}`);
            await page.waitForFunction(() => (window as any).vpResult || (window as any).vpError, {timeout: 30000});
            const r = await page.evaluate(async () => {
                const vp = (window as any).vp;
                const out: any = { vpResult: (window as any).vpResult,
                                   vpError: (window as any).vpError };
                if (!vp) return out;
                const d: GPUDevice = vp.device;
                try {
                    const b = d.createBuffer({size: 256,
                        usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ});
                    d.queue.writeBuffer(b, 0, new Uint8Array(256).fill(5));
                    await b.mapAsync(GPUMapMode.READ);
                    out.map = 'ok ' + new Uint8Array(b.getMappedRange())[0];
                } catch (e) { out.map = String(e); }
                out.lost = await Promise.race([d.lost.then(i=>i.reason), new Promise(r=>setTimeout(()=>r('alive'),500))]);
                return out;
            });
            console.log(`STAGE ${q}:`, JSON.stringify(r));
        } finally { server.close(); }
    });
}
