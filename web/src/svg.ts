// web/src/svg.ts — vector export: replays the same VPOP op stream into
// SVG markup. Geometry ops map to <path>/<circle>/<polyline>; heatmap
// grids embed as raster <image> (PNG data URL); glyph quads are skipped
// (exporting tick text as outlines would need the atlas as paths —
// Canvas2D/SVG parity for text is a known gap). 3D ops are skipped.

import { Op, OpReader } from './interpreter.js';
import { XformView, css } from './fallback.js';

const hex = (c: ArrayLike<number>) =>
    '#' + [c[0], c[1], c[2]]
        .map(v => Math.round(Math.min(Math.max(v, 0), 1) * 255)
            .toString(16).padStart(2, '0')).join('');
const alpha = (c: ArrayLike<number>) => (c[3] ?? 1);

interface Tex { w: number; h: number; fmt: number;
                data: Uint8Array<ArrayBuffer> }

export class SvgExporter {
    private buffers = new Map<number, Uint8Array<ArrayBuffer>>();
    private textures = new Map<number, Tex>();
    private out: string[] = [];
    private clips = new Map<string, number>();
    private nClip = 0;

    toSvg(frame: Uint8Array<ArrayBuffer>): string {
        this.out = []; this.clips.clear(); this.nClip = 0;
        this.buffers.clear(); this.textures.clear();
        const r = new OpReader(frame);
        for (const { op, p } of r.ops()) {
            if (op <= Op.ReleaseTexture) this.execResource(r, op, p);
            else if (op >= Op.TessLines) this.execCompute(r, p, op);
            else this.draw(r, op, p);
        }
        let defs = '';
        for (const [rc, id] of this.clips) {
            const [x, y, w, h] = rc.split(',').map(Number);
            defs += `<clipPath id="c${id}"><rect x="${x}" y="${y}" ` +
                    `width="${w}" height="${h}"/></clipPath>`;
        }
        const [cr, cg, cb, ca] = r.clearRGBA();
        const bg = `<rect width="100%" height="100%" fill="${hex(
            [cr, cg, cb])}" fill-opacity="${ca}"/>`;
        return `<svg xmlns="http://www.w3.org/2000/svg" ` +
               `width="${r.header.canvasW}" height="${r.header.canvasH}" ` +
               `viewBox="0 0 ${r.header.canvasW} ${r.header.canvasH}">` +
               (defs ? `<defs>${defs}</defs>` : '') + bg +
               this.out.join('') + `</svg>`;
    }

    // ── resources / compute — same as Canvas2D path ─────────────────

    private execResource(r: OpReader, op: number,
                         p: DataView<ArrayBuffer>) {
        switch (op) {
        case Op.CreateBuffer:
            this.buffers.set(p.getUint32(0, true),
                new Uint8Array(Number(p.getBigUint64(4, true))));
            break;
        case Op.WriteBuffer: {
            const b = this.buffers.get(p.getUint32(0, true));
            if (b) b.set(r.bulk(p, 12),
                       Number(p.getBigUint64(4, true)));
            break;
        }
        case Op.ReleaseBuffer:
            this.buffers.delete(p.getUint32(0, true)); break;
        case Op.CreateTexture:
            this.textures.set(p.getUint32(0, true), {
                w: p.getUint32(4, true), h: p.getUint32(8, true),
                fmt: p.getUint8(12), data: new Uint8Array(0) });
            break;
        case Op.WriteTexture: {
            const t = this.textures.get(p.getUint32(0, true));
            if (!t) break;
            const x = p.getUint32(4, true), y = p.getUint32(8, true),
                  w = p.getUint32(12, true), hh = p.getUint32(16, true);
            const src = r.bulk(p, 20);
            const bpt = t.fmt === 0 ? 1 : 4;
            if (t.data.length !== t.w * t.h * bpt)
                t.data = new Uint8Array(t.w * t.h * bpt);
            for (let row = 0; row < hh; row++)
                t.data.set(src.subarray(row * w * bpt, (row + 1) * w * bpt),
                           ((y + row) * t.w + x) * bpt);
            break;
        }
        case Op.ReleaseTexture:
            this.textures.delete(p.getUint32(0, true)); break;
        }
    }

    private execCompute(r: OpReader, p: DataView<ArrayBuffer>,
                        op: number) {
        // Only TessLines produces geometry the exporter needs — reuse
        // the Canvas2D CPU tessellator logic (butt-cap segment quads).
        if (op !== Op.TessLines) return;
        const inB = this.buffers.get(p.getUint32(0, true));
        const out = this.buffers.get(p.getUint32(8, true));
        const nSeg = p.getUint32(20, true);
        const hw = p.getFloat32(24, true);
        const color = [p.getFloat32(34, true), p.getFloat32(38, true),
                       p.getFloat32(42, true), p.getFloat32(46, true)];
        if (!inB || !out) return;
        const iv = new Float32Array(inB.buffer, inB.byteOffset +
                                    p.getUint32(4, true));
        const ov = new DataView(out.buffer, out.byteOffset +
                                p.getUint32(12, true));
        for (let s = 0; s < nSeg; s++) {
            const x0 = iv[s * 2], y0 = iv[s * 2 + 1];
            const x1 = iv[s * 2 + 2], y1 = iv[s * 2 + 3];
            const dx = x1 - x0, dy = y1 - y0;
            const len = Math.hypot(dx, dy) || 1;
            const nx = -dy / len * hw, ny = dx / len * hw;
            const q = [x0 - nx, y0 - ny, x1 - nx, y1 - ny, x1 + nx, y1 + ny,
                       x0 - nx, y0 - ny, x1 + nx, y1 + ny, x0 + nx, y0 + ny];
            for (let v = 0; v < 6; v++) {
                const o = (s * 6 + v) * 24;
                ov.setFloat32(o, q[v * 2], true);
                ov.setFloat32(o + 4, q[v * 2 + 1], true);
                for (let k = 0; k < 4; k++)
                    ov.setFloat32(o + 8 + k * 4, color[k], true);
            }
        }
    }

    // ── SVG emission ────────────────────────────────────────────────

    /** Open a clip group for the payload's clip rect; returns '' or
     * the closing tag handling is via clipEnd(). */
    private clipStart(p: DataView<ArrayBuffer>): string {
        const x = p.getFloat32(0, true), y = p.getFloat32(4, true),
              w = p.getFloat32(8, true), h = p.getFloat32(12, true);
        const key = `${x},${y},${w},${h}`;
        let id = this.clips.get(key);
        if (id === undefined) {
            id = this.nClip++; this.clips.set(key, id);
        }
        return `<g clip-path="url(#c${id})">`;
    }

    private f32(buf: Uint8Array<ArrayBuffer>): Float32Array<ArrayBuffer> {
        return new Float32Array(buf.buffer, buf.byteOffset,
                                buf.length / 4);
    }

    private triPath(pts: number[], col: string, a: number): string {
        return `<path d="M${pts.join('L')}Z" fill="${col}"` +
               (a < 1 ? ` fill-opacity="${a}"` : '') + '/>';
    }

    private draw(r: OpReader, op: number, p: DataView<ArrayBuffer>) {
        const E = this.out, clip = this.clipStart(p), CE = '</g>';
        switch (op) {
        case Op.DrawTrisPx: {
            const vf = this.f32(r.bulk(p, 16));
            const col = [p.getFloat32(40, true), p.getFloat32(44, true),
                         p.getFloat32(48, true), p.getFloat32(52, true)];
            let d = '';
            for (let i = 0; i + 5 < vf.length; i += 6)
                d += `M${vf[i]} ${vf[i+1]}L${vf[i+2]} ${vf[i+3]}` +
                     `L${vf[i+4]} ${vf[i+5]}Z`;
            E.push(clip + `<path d="${d}" fill="${hex(col)}"` +
                   ` fill-opacity="${alpha(col)}"/>` + CE);
            break;
        }
        case Op.DrawTrisPxVC: {
            const vf = this.f32(r.bulk(p, 16));
            const cf = this.f32(r.bulk(p, 40));
            let s = clip;
            for (let i = 0; i + 5 < vf.length; i += 6) {
                const c = cf.subarray(i * 2, i * 2 + 4);
                s += this.triPath(
                    [vf[i], vf[i+1], vf[i+2], vf[i+3], vf[i+4], vf[i+5]],
                    hex(c), alpha(c));
            }
            E.push(s + CE); break;
        }
        case Op.DrawLineStripPx:
        case Op.DrawSegmentsPx: {
            const vf = this.f32(r.bulk(p, 16));
            const col = [p.getFloat32(40, true), p.getFloat32(44, true),
                         p.getFloat32(48, true), p.getFloat32(52, true)];
            const lw = Math.max(p.getFloat32(56, true), 0.5);
            let d = '';
            if (op === Op.DrawLineStripPx) {
                d = `M${vf[0]} ${vf[1]}`;
                for (let i = 2; i < vf.length; i += 2)
                    d += `L${vf[i]} ${vf[i+1]}`;
            } else
                for (let i = 0; i + 3 < vf.length; i += 4)
                    d += `M${vf[i]} ${vf[i+1]}L${vf[i+2]} ${vf[i+3]}`;
            E.push(clip + `<path d="${d}" fill="none" stroke="${hex(col)}"` +
                   ` stroke-width="${lw}"` +
                   (alpha(col) < 1 ? ` stroke-opacity="${alpha(col)}"` : '') +
                   `/>` + CE);
            break;
        }
        case Op.DrawLines:
        case Op.DrawLineSegs: {
            const xf = new XformView(p, 32);
            const buf = this.buffers.get(p.getUint32(160, true));
            const n = p.getUint32(164, true);
            if (!buf) break;
            const vf = new Float32Array(buf.buffer, buf.byteOffset, n * 2);
            const col = [p.getFloat32(168, true), p.getFloat32(172, true),
                         p.getFloat32(176, true), p.getFloat32(180, true)];
            const lw = Math.max(p.getFloat32(184, true), 0.5);
            let d = '';
            if (op === Op.DrawLines) {
                const [x, y] = xf.toPx(vf[0], vf[1]);
                d = `M${x} ${y}`;
                for (let i = 1; i < n; i++) {
                    const [px, py] = xf.toPx(vf[i*2], vf[i*2+1]);
                    d += `L${px} ${py}`;
                }
            } else
                for (let i = 0; i + 3 < n * 2; i += 4) {
                    const [a, b] = xf.toPx(vf[i], vf[i+1]);
                    const [c, dd] = xf.toPx(vf[i+2], vf[i+3]);
                    d += `M${a} ${b}L${c} ${dd}`;
                }
            E.push(clip + `<path d="${d}" fill="none" stroke="${hex(col)}"` +
                   ` stroke-width="${lw}"/>` + CE);
            break;
        }
        case Op.DrawPoints: {
            const xf = new XformView(p, 32);
            const pos = this.buffers.get(p.getUint32(160, true));
            const col = this.buffers.get(p.getUint32(164, true));
            const siz = this.buffers.get(p.getUint32(168, true));
            const n = p.getUint32(172, true);
            const flags = p.getUint32(176, true);
            if (!pos) break;
            const pv = new Float32Array(pos.buffer, pos.byteOffset, n * 2);
            const cv = col && (flags & 1)
                ? new Float32Array(col.buffer, col.byteOffset, n * 4)
                : undefined;
            const sv = siz && (flags & 2)
                ? new Float32Array(siz.buffer, siz.byteOffset, n)
                : undefined;
            let s = clip;
            for (let i = 0; i < n; i++) {
                const [x, y] = xf.toPx(pv[i*2], pv[i*2+1]);
                const c = cv ? cv.subarray(i*4, i*4+4) : xf.color;
                s += `<circle cx="${x}" cy="${y}" r="${(sv ? sv[i] : 6)/2}"` +
                     ` fill="${hex(c)}"` +
                     (alpha(c) < 1 ? ` fill-opacity="${alpha(c)}"` : '') +
                     '/>';
            }
            E.push(s + CE); break;
        }
        case Op.DrawTrisData:
        case Op.DrawPie: {
            const xf = new XformView(p, 16);
            const pos = this.buffers.get(p.getUint32(144, true));
            const col = this.buffers.get(p.getUint32(148, true));
            const n = p.getUint32(152, true);
            if (!pos) break;
            const pv = new Float32Array(pos.buffer, pos.byteOffset, n * 2);
            const cv = col
                ? new Float32Array(col.buffer, col.byteOffset, n * 4)
                : undefined;
            let s = clip;
            for (let i = 0; i + 2 < n; i += 3) {
                const pts: number[] = [];
                for (let k = 0; k < 3; k++) {
                    const [x, y] = xf.toPx(pv[(i+k)*2], pv[(i+k)*2+1]);
                    pts.push(x, y);
                }
                const c = cv ? cv.subarray(i*4, i*4+4)
                    : [p.getFloat32(156, true), p.getFloat32(160, true),
                       p.getFloat32(164, true), p.getFloat32(168, true)];
                s += this.triPath(pts, hex(c), alpha(c));
            }
            E.push(s + CE); break;
        }
        case Op.DrawTrisGpu: {
            const buf = this.buffers.get(p.getUint32(24, true));
            if (!buf) break;
            const off = Number(p.getBigUint64(28, true));
            const cnt = p.getUint32(36, true);
            const vf = new Float32Array(buf.buffer,
                buf.byteOffset + off, cnt * 6);
            let s = clip;
            for (let i = 0; i + 17 < vf.length; i += 18) {
                const c = vf.subarray(i + 2, i + 6);
                s += this.triPath(
                    [vf[i], vf[i+1], vf[i+6], vf[i+7], vf[i+12], vf[i+13]],
                    hex(c), alpha(c));
            }
            E.push(s + CE); break;
        }
        case Op.DrawInstanced: {
            const tpl = this.f32(r.bulk(p, 16));
            const inst = this.f32(r.bulk(p, 40));
            let s = clip;
            for (let i = 0; i + 7 < inst.length; i += 8) {
                const [ox, oy, sx, sy] = [inst[i], inst[i+1],
                                          inst[i+2], inst[i+3]];
                let d = '';
                for (let k = 0; k + 1 < tpl.length; k += 2)
                    d += `${k ? 'L' : 'M'}${ox + tpl[k]*sx} ${oy + tpl[k+1]*sy}`;
                const c = inst.subarray(i + 4, i + 8);
                s += `<path d="${d}Z" fill="${hex(c)}"` +
                     ` fill-opacity="${alpha(c)}"/>`;
            }
            E.push(s + CE); break;
        }
        case Op.DrawImage: {
            // raster → embedded PNG data URL (heatmap grids etc.)
            const xf = new XformView(p, 16);
            const gt = this.textures.get(p.getUint32(144, true));
            const lt = this.textures.get(p.getUint32(148, true));
            if (!gt || typeof document === 'undefined') break;
            const rgba = gt.fmt === 1 || p.getFloat32(152, true) > 0.5;
            const nanT = p.getFloat32(160, true) > 0.5;
            const vMin = p.getFloat32(164, true),
                  vMax = p.getFloat32(168, true);
            const cv = document.createElement('canvas');
            cv.width = gt.w; cv.height = gt.h;
            const cc = cv.getContext('2d')!;
            const img = cc.createImageData(gt.w, gt.h);
            const lut = lt ? lt.data : undefined;
            const gf = rgba ? undefined
                : new Float32Array(gt.data.buffer, gt.data.byteOffset,
                                   gt.data.length / 4);
            const span = Math.max(vMax - vMin, 1e-30);
            for (let i = 0; i < gt.w * gt.h; i++) {
                if (rgba) {
                    img.data.set(gt.data.subarray(i*4, i*4+4), i*4);
                    continue;
                }
                const v = gf![i];
                if (Number.isNaN(v)) {
                    if (!nanT) img.data.set([255,0,255,255], i*4);
                    continue;
                }
                const t = Math.min(Math.max((v - vMin)/span, 0), 1);
                const li = lut ? Math.min(t*255, 255)|0 : 0;
                img.data.set(lut
                    ? [lut[li*4], lut[li*4+1], lut[li*4+2], lut[li*4+3]]
                    : [t*255, t*255, t*255, 255], i*4);
            }
            cc.putImageData(img, 0, 0);
            const f = xf.f;
            E.push(clip + `<image x="${f[4]}" y="${f[5]}" width="${f[6]}"` +
                   ` height="${f[7]}" preserveAspectRatio="none"` +
                   ` image-rendering="pixelated"` +
                   ` href="${cv.toDataURL('image/png')}"/>` + CE);
            break;
        }
        default: break;  // DrawTextQuads/DrawSurface/DrawGrid3D — skipped
        }
    }
}
