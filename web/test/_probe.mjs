import { chromium } from 'playwright';
import { createServer } from 'node:http';
import { readFileSync } from 'node:fs';
import { join } from 'node:path';
const ROOT = new URL('.', import.meta.url).pathname;
const MIME = {'.html':'text/html','.js':'text/javascript','.mjs':'text/javascript','.wasm':'application/wasm','.data':'application/octet-stream'};
const server = createServer((req,res)=>{
  const p = join(ROOT, decodeURIComponent((req.url??'/').split('?')[0]));
  try { res.writeHead(200,{'content-type':MIME[p.slice(p.lastIndexOf('.'))]??'application/octet-stream'}); res.end(readFileSync(p)); }
  catch { res.writeHead(404); res.end(); }
});
await new Promise(r=>server.listen(0,r));
const port = server.address().port;
const browser = await chromium.launch({args:['--enable-unsafe-webgpu','--enable-features=Vulkan','--use-webgpu-adapter=swiftshader']});
const page = await browser.newPage();
page.on('console', m => console.log('CON:', m.text().slice(0,200)));
await page.goto(`http://localhost:${port}/demo/smoke.html`);
await page.waitForFunction(()=>window.vpResult!==undefined||window.vpError!==undefined,{timeout:30000});
const r = await page.evaluate(async () => {
  const vp = window.vp, mod = vp.mod;
  let calls = [];
  const orig = mod._vp_mailbox.bind(mod);
  mod._vp_mailbox = (s,a,b,c,d)=>{ calls.push([s,a,b,c,d]); orig(s,a,b,c,d); };
  await vp.capture();           // render + flush mailbox
  await new Promise(r=>setTimeout(r,200));
  await vp.capture();           // second frame: reduceResult should hit
  return { calls, vpErr: window.vpError };
});
console.log('MAILBOX', JSON.stringify(r));
await browser.close(); server.close();
