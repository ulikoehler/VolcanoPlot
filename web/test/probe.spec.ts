import { test } from '@playwright/test';
import { createServer } from 'node:http';

// Isolated probe: fresh device, submit trivial copy, mapAsync — no
// volcano code involved at all.
test('raw webgpu mapAsync probe', async ({ page }) => {
    page.on('console', m => console.log('CON:', m.text().slice(0, 200)));
    const server = createServer((_q, res) => {
        res.writeHead(200, {'content-type': 'text/html'});
        res.end('<html><body>probe</body></html>');
    });
    await new Promise<void>(r => server.listen(0, r));
    const port = (server.address() as any).port;
    await page.goto(`http://localhost:${port}/`);
    const r = await page.evaluate(async () => {
        try {
            if (!navigator.gpu) return { ok: false, err: 'no navigator.gpu' };
            const a = await navigator.gpu.requestAdapter();
            if (!a) return { ok: false, err: 'no adapter' };
            const d = await a.requestDevice();
            (window as any).__d = d; (window as any).__a = a;
            const src = d.createBuffer({ size: 256,
                usage: GPUBufferUsage.COPY_SRC | GPUBufferUsage.COPY_DST });
            const dst = d.createBuffer({ size: 256,
                usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
            d.queue.writeBuffer(src, 0, new Uint8Array(256).fill(9));
            const e = d.createCommandEncoder();
            e.copyBufferToBuffer(src, 0, dst, 0, 256);
            d.queue.submit([e.finish()]);
            await dst.mapAsync(GPUMapMode.READ);
            return { ok: true, v: new Uint8Array(dst.getMappedRange())[0],
                     info: JSON.stringify(a.info) };
        } catch (e) { return { ok: false, err: String(e) }; }
    });
    console.log('RAWPROBE', JSON.stringify(r));
    // device still alive a few seconds later?
    await page.waitForTimeout(3000);
    const r2 = await page.evaluate(async () => {
        const d = (window as any).__d as GPUDevice;
        const b = d.createBuffer({ size: 256,
            usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
        try { await b.mapAsync(GPUMapMode.READ); return 'map ok'; }
        catch (e) { return String(e); }
    });
    console.log('RAWPROBE2', r2);
    server.close();
});
