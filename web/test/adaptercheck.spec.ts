import { test } from '@playwright/test';
import { createServer } from 'node:http';

test('adapter is intel', async ({ page }) => {
    const server = createServer((_, res) => {
        res.writeHead(200, { 'content-type': 'text/html' });
        res.end('<html><body>ok</body></html>');
    });
    await new Promise<void>(r => server.listen(0, r));
    const port = (server.address() as any).port;
    await page.goto(`http://localhost:${port}/`);
    const info = await page.evaluate(async () => {
        const a = await (navigator as any).gpu?.requestAdapter();
        if (!a) return 'NO ADAPTER';
        const i = a.info ?? await a.requestAdapterInfo?.();
        return JSON.stringify(i ?? {});
    });
    console.log('ADAPTER', info);
    server.close();
});
