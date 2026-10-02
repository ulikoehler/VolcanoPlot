import { test } from '@playwright/test';
import { createServer } from 'node:http';
import { readFileSync } from 'node:fs';
import { join } from 'node:path';

const ROOT = new URL('..', import.meta.url).pathname;
const MIME: Record<string, string> = {
    '.html': 'text/html', '.js': 'text/javascript', '.mjs': 'text/javascript',
    '.wasm': 'application/wasm' };

test('debug webgpu + module load', async ({ page }) => {
    page.on('console', m => console.log('CONSOLE:', m.type(), m.text()));
    page.on('pageerror', e => console.log('PAGEERR:', String(e)));
    page.on('requestfailed', r => console.log('REQFAIL:', r.url(), r.failure()?.errorText));
    const server = createServer((req, res) => {
        const p = join(ROOT, decodeURIComponent(req.url ?? '/'));
        try {
            const body = readFileSync(p);
            res.writeHead(200, { 'content-type': MIME[p.slice(p.lastIndexOf('.'))] ?? 'application/octet-stream' });
            res.end(body);
        } catch (e) { res.writeHead(404); res.end(String(e)); }
    });
    await new Promise<void>(r => server.listen(0, r));
    const port = (server.address() as any).port;
    try {
        await page.goto(`http://localhost:${port}/demo/smoke.html`);
        await page.waitForTimeout(8000);
        console.log('gpu?', await page.evaluate(() => !!navigator.gpu));
        console.log('vpError:', await page.evaluate(() => (window as any).vpError));
        const r = await Promise.race([
            page.evaluate(async () => String(await (window as any).vpDone)),
            new Promise<string>(r => setTimeout(() => r('PENDING'), 2000))]);
        console.log('vpDone:', r);
    } finally { server.close(); }
});
