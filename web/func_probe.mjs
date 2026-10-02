import { chromium } from 'playwright';
import { createServer } from 'node:http';
import { readFileSync } from 'node:fs';
import { join } from 'node:path';
const ROOT = new URL('.', import.meta.url).pathname;
const MIME = {'.html':'text/html','.js':'text/javascript','.mjs':'text/javascript','.wasm':'application/wasm','.data':'application/octet-stream'};
const server = createServer((req,res)=>{
  const p = join(ROOT, decodeURIComponent((req.url??'/').split('?')[0]));
  try { const b = readFileSync(p);
    res.writeHead(200,{'content-type':MIME[p.slice(p.lastIndexOf('.'))]??'application/octet-stream'}); res.end(b); }
  catch { res.writeHead(404); res.end(); }
});
await new Promise(r=>server.listen(0,r));
const browser = await chromium.launch({channel:'chrome', args:['--enable-unsafe-webgpu','--enable-features=Vulkan','--use-webgpu-adapter=swiftshader']});
const page = await browser.newPage();
page.on('console', m => console.log('CON:', m.text().slice(0,300)));
await page.goto(`http://localhost:${server.address().port}/demo/func.html`);
await page.waitForFunction(()=>window.vpResult!==undefined||window.vpError!==undefined,{timeout:30000});
const r = await page.evaluate(async () => {

  const mod = window.vp.mod;
  const len = mod._vp_frameLen(), d = new DataView(mod.HEAPU8.buffer, mod._vp_framePtr(), len);
  const out = [];
  let off = 40;
  const N = d.getUint32(24,true);
  for (let i = 0; i < N; i++) {
    const op = d.getUint16(off,true), plen = d.getUint32(off+4,true);
    if (op === 41) out.push({op:'EvalFunc', outBuf:d.getUint32(off+8,true), xMin:d.getFloat64(off+12,true), xMax:d.getFloat64(off+20,true), count:d.getUint32(off+28,true), funcId:d.getUint16(off+32,true)});
    if (op === 42) out.push({op:'FuncDef', id:d.getUint16(off+8,true), len:d.getBigUint64(off+8+3+16,true).toString()});
    if (op === 20) out.push({op:'DrawLines', buf:d.getUint32(off+8+160,true), count:d.getUint32(off+8+164,true), viewRect:[...Array(4)].map((_,k)=>d.getFloat32(off+8+16+k*4,true))});
    off += 8+plen+((4-(plen&3))&3);
  }
  let nw=0, red=0;
  const px = window.vpPixels || new Uint8Array(0);
  for (let i=0;i<px.length;i+=4){ if(px[i]<245||px[i+1]<245||px[i+2]<245) nw++; if(px[i]>150&&px[i+1]<100&&px[i+2]<100) red++; }
  // decode DrawLines xform ubo (PDrawLines ubo at +32, TransformUBO fields)
  const d2 = new DataView(mod.HEAPU8.buffer, mod._vp_framePtr(), len);
  let off2 = 40; let uboInfo = null;
  for (let i = 0; i < N; i++) {
    const op = d2.getUint16(off2,true), plen = d2.getUint32(off2+4,true);
    if (op === 20 && !uboInfo) {
      // TransformUBO at payload+32: viewMinSpan@0..15, rect@16..31, color@32..47, scaleX@48...
      const base = off2 + 8 + 32;
      uboInfo = {
        viewMinSpan: [...Array(4)].map((_,k)=>d2.getFloat32(base+k*4,true)),
        rect: [...Array(4)].map((_,k)=>d2.getFloat32(base+16+k*4,true)),
        color: [...Array(4)].map((_,k)=>d2.getFloat32(base+32+k*4,true)),
        scaleX: [...Array(4)].map((_,k)=>d2.getFloat32(base+48+k*4,true)),
        scaleY: [...Array(4)].map((_,k)=>d2.getFloat32(base+64+k*4,true)),
      };
    }
    off2 += 8+plen+((4-(plen&3))&3);
  }
  return {N, out, err: window.vpError, res: window.vpResult, nw, red, uboInfo};
});
console.log(JSON.stringify(r, null, 1));
await browser.close(); server.close();
