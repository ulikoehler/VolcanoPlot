// web/src/fallback.ts — Canvas2D interpreter for browsers without
// WebGPU. Decodes the same VPOP stream and rasterizes the pixel-space
// ops directly; data-space ops run the transform math in JS (port of
// transform.wgsl).
//
// Coverage gaps vs the GPU interpreter: no 3D (DrawSurface/DrawGrid3D),
// no GPU function eval (EvalFunc), marker SDFs approximated by
// circles/squares, line joins approximated with butt caps.

import { Op, OpReader } from './interpreter.js';

// ── transform port (transform.wgsl) ──────────────────────────────────

const asinh = (x: number) => Math.log(x + Math.sqrt(x * x + 1));

function scaleFwd(v: number, s: number[]): number {
    const c = Math.round(s[0]);
    if (c === 1) return Math.log(Math.max(v, 1e-30)) / Math.LN10;
    if (c === 2) {
        const [lt, ls, b0] = [s[1], s[2], Math.max(s[3], 1.000001)];
        const adj = ls / (1 - Math.pow(b0, -1)), a = Math.abs(v);
        if (a <= lt) return adj * v;
        return Math.sign(v) * lt * (adj + Math.log(a / lt) / Math.log(b0));
    }
    if (c === 3) {
        const q = Math.min(Math.max(v, 1e-7), 1 - 1e-7);
        return Math.log(q / (1 - q));
    }
    if (c === 4) { const a = Math.max(s[1], 1e-30); return a * asinh(v / a); }
    if (c === 5) {
        const phi = Math.min(Math.max(v, -1.48442223), 1.48442223);
        return Math.log(Math.tan(0.7853981633974483 + phi * 0.5));
    }
    return v;
}

function projFwd(x: number, y: number, pr: number[]): [number, number] {
    const c = Math.round(pr[0]);
    if (c === 0) return [x, y];
    if (c === 1) {
        const th = pr[2] * x + pr[1];
        return [y * Math.cos(th), y * Math.sin(th)];
    }
    const lon = Math.min(Math.max(x, -Math.PI), Math.PI);
    const lat = Math.min(Math.max(y, -Math.PI / 2), Math.PI / 2);
    if (c === 2) {
        const al = Math.acos(Math.min(Math.max(
            Math.cos(lat) * Math.cos(lon * 0.5), -1), 1));
        const sa = Math.abs(al) < 1e-7 ? 1 : Math.sin(al) / al;
        return [2 * Math.cos(lat) * Math.sin(lon * 0.5) / sa,
                Math.sin(lat) / sa];
    }
    if (c === 3) {
        const z = Math.sqrt(Math.max(
            1 + Math.cos(lat) * Math.cos(lon * 0.5), 1e-12));
        return [2.8284271 * Math.cos(lat) * Math.sin(lon * 0.5) / z,
                1.41421356 * Math.sin(lat) / z];
    }
    if (c === 4) {
        const k = Math.sqrt(Math.max(
            2 / (1 + Math.cos(lat) * Math.cos(lon)), 0));
        return [k * Math.cos(lat) * Math.sin(lon), k * Math.sin(lat)];
    }
    if (c === 5) {
        const s = Math.min(Math.max(Math.sin(lat), -1), 1);
        let th = lat;
        for (let i = 0; i < 8; i++) {
            const fp = 2 + 2 * Math.cos(2 * th);
            if (Math.abs(fp) < 1e-12) break;
            th -= (2 * th + Math.sin(2 * th) - Math.PI * s) / fp;
        }
        return [0.9003163 * lon * Math.cos(th), 1.41421356 * Math.sin(th)];
    }
    return [x, y];
}

export class XformView {
    readonly f: number[];
    constructor(p: DataView<ArrayBuffer>, at: number) {
        this.f = [];
        for (let i = 0; i < 32; i++) this.f.push(p.getFloat32(at + i * 4, true));
    }
    /** data coords → pixel coords (Y-down) */
    toPx(x: number, y: number): [number, number] {
        const f = this.f;
        const [px, py] = projFwd(
            scaleFwd(x, f.slice(12, 16)),
            scaleFwd(y, f.slice(16, 20)), f.slice(20, 23));
        const tx = (px - f[0]) / f[2], ty = (py - f[1]) / f[3];
        return [f[4] + tx * f[6], f[5] + f[7] - ty * f[7]];
    }
    get color(): [number, number, number, number] {
        return [this.f[8], this.f[9], this.f[10], this.f[11]];
    }
}

export const css = (c: ArrayLike<number>) =>
    `rgba(${c[0] * 255 | 0},${c[1] * 255 | 0},${c[2] * 255 | 0},${c[3]})`;

// ── interpreter ──────────────────────────────────────────────────────

interface Tex { w: number; h: number; fmt: number;
                data: Uint8Array<ArrayBuffer>; canvas?: HTMLCanvasElement }

export class Canvas2DInterpreter {
    private buffers = new Map<number, Uint8Array<ArrayBuffer>>();
    private textures = new Map<number, Tex>();
    private atlasCanvas: HTMLCanvasElement | undefined;

    constructor(
        private onMailbox?: (slot: number,
                             v: [number, number, number, number]) => void,
    ) {}

    draw(frame: Uint8Array<ArrayBuffer>, ctx: CanvasRenderingContext2D) {
        const r = new OpReader(frame);
        const [cr, cg, cb, ca] = r.clearRGBA().map(v => v * 255);
        if (!r.header.loadOp) {
            ctx.fillStyle = `rgba(${cr},${cg},${cb},${ca / 255})`;
            ctx.fillRect(0, 0, r.header.canvasW, r.header.canvasH);
        }
        for (const { op, p } of r.ops()) {
            if (op <= Op.ReleaseTexture) this.execResource(r, op, p);
            else if (op >= Op.TessLines) this.execCompute(r, p, op);
            else this.dispatchDraw(r, ctx, op, p);
        }
    }

    private execResource(r: OpReader, op: number,
                         p: DataView<ArrayBuffer>) {
        switch (op) {
        case Op.CreateBuffer:
            this.buffers.set(p.getUint32(0, true),
                new Uint8Array(Number(p.getBigUint64(4, true))));
            break;
        case Op.WriteBuffer: {
            const h = p.getUint32(0, true);
            const off = Number(p.getBigUint64(4, true));
            const buf = this.buffers.get(h);
            if (buf) buf.set(r.bulk(p, 12), off);
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
            const h = p.getUint32(0, true);
            const t = this.textures.get(h);
            if (!t) break;
            const x = p.getUint32(4, true), y = p.getUint32(8, true),
                  w = p.getUint32(12, true), hh = p.getUint32(16, true);
            const src = r.bulk(p, 20);
            const bpt = t.fmt === 0 ? 1 : t.fmt === 1 ? 4 : 4;
            if (t.data.length !== t.w * t.h * bpt)
                t.data = new Uint8Array(t.w * t.h * bpt);
            for (let row = 0; row < hh; row++)
                t.data.set(
                    src.subarray(row * w * bpt, (row + 1) * w * bpt),
                    ((y + row) * t.w + x) * bpt);
            t.canvas = undefined;   // atlas canvas rebuilt lazily
            break;
        }
        case Op.ReleaseTexture:
            this.textures.delete(p.getUint32(0, true)); break;
        }
    }

    // ── compute (CPU equivalents) ────────────────────────────────────

    private execCompute(r: OpReader, p: DataView<ArrayBuffer>,
                        op: number) {
        if (op === Op.TessLines) {
            // butt-cap segment quads → 24B records in outBuf
            const inB = this.buffers.get(p.getUint32(0, true));
            const outH = p.getUint32(8, true);
            const n = p.getUint32(16, true);
            const nSeg = p.getUint32(20, true);
            const hw = p.getFloat32(24, true);
            const color = [p.getFloat32(34, true), p.getFloat32(38, true),
                           p.getFloat32(42, true), p.getFloat32(46, true)];
            const out = this.buffers.get(outH);
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
                const quad = [
                    x0 - nx, y0 - ny, x1 - nx, y1 - ny, x1 + nx, y1 + ny,
                    x0 - nx, y0 - ny, x1 + nx, y1 + ny, x0 + nx, y0 + ny,
                ];
                for (let v = 0; v < 6; v++) {
                    const o = (s * 6 + v) * 24;
                    ov.setFloat32(o, quad[v * 2], true);
                    ov.setFloat32(o + 4, quad[v * 2 + 1], true);
                    for (let k = 0; k < 4; k++)
                        ov.setFloat32(o + 8 + k * 4, color[k], true);
                }
            }
            return;
        }
        if (op === Op.ReduceMinMax) {
            const inB = this.buffers.get(p.getUint32(0, true));
            const count = p.getUint32(4, true);
            if (!inB || !count) return;
            const iv = new Float32Array(inB.buffer, inB.byteOffset,
                                        Math.floor(inB.byteLength / 4));
            let x0 = Infinity, x1 = -Infinity, y0 = Infinity, y1 = -Infinity;
            for (let i = 0; i < count; i++) {
                const x = iv[i * 2], y = iv[i * 2 + 1];
                if (x < x0) x0 = x; if (x > x1) x1 = x;
                if (y < y0) y0 = y; if (y > y1) y1 = y;
            }
            this.onMailbox?.(p.getUint32(8, true), [x0, x1, y0, y1]);
            return;
        }
        // FuncDef/EvalFunc/KdeEval2D/HistBins/PcmTess/ViolinKde:
        // no Canvas2D path — the C++ side CPU-fallbacks cover results.
    }

    // ── draws ────────────────────────────────────────────────────────

    private clip(r: CanvasRenderingContext2D,
                 p: DataView<ArrayBuffer>): number {
        const x = p.getFloat32(0, true), y = p.getFloat32(4, true),
              w = p.getFloat32(8, true), h = p.getFloat32(12, true);
        r.save();
        r.beginPath(); r.rect(x, y, w, h); r.clip();
        return 0;
    }

    private tris(ctx: CanvasRenderingContext2D,
                 verts: Float32Array, stride: number,
                 colAt: (i: number) => ArrayLike<number>) {
        for (let i = 0; i + 2 * stride < verts.length; i += 3 * stride) {
            ctx.fillStyle = css(colAt(i));
            ctx.beginPath();
            ctx.moveTo(verts[i], verts[i + 1]);
            ctx.lineTo(verts[i + stride], verts[i + stride + 1]);
            ctx.lineTo(verts[i + stride * 2], verts[i + stride * 2 + 1]);
            ctx.closePath(); ctx.fill();
        }
    }

    private atlas(): HTMLCanvasElement | undefined {
        if (this.atlasCanvas) return this.atlasCanvas;
        // find the largest r8/rgba texture — the glyph atlas
        let at: Tex | undefined;
        for (const t of this.textures.values())
            if ((t.fmt === 0 || t.fmt === 1) && (!at || t.w * t.h > at.w * at.h))
                at = t;
        if (!at) return undefined;
        const cv = document.createElement('canvas');
        cv.width = at.w; cv.height = at.h;
        const c = cv.getContext('2d')!;
        const img = c.createImageData(at.w, at.h);
        if (at.fmt === 0)
            for (let i = 0; i < at.w * at.h; i++) {
                img.data[i * 4 + 3] = at.data[i];
                img.data[i * 4] = img.data[i * 4 + 1] =
                    img.data[i * 4 + 2] = 255;
            }
        else img.data.set(at.data);
        c.putImageData(img, 0, 0);
        this.atlasCanvas = cv;
        return cv;
    }

    private dispatchDraw(r: OpReader, ctx: CanvasRenderingContext2D,
                         op: number, p: DataView<ArrayBuffer>) {
        switch (op) {
        case Op.DrawTrisPx: {
            this.clip(ctx, p);
            const b = r.bulk(p, 16);
            this.tris(ctx,
                new Float32Array(b.buffer, b.byteOffset, b.length / 4), 2,
                () => [p.getFloat32(40, true), p.getFloat32(44, true),
                       p.getFloat32(48, true), p.getFloat32(52, true)]);
            ctx.restore(); break;
        }
        case Op.DrawTrisPxVC: {
            this.clip(ctx, p);
            const v = r.bulk(p, 16), c = r.bulk(p, 40);
            const vf = new Float32Array(v.buffer, v.byteOffset,
                                        v.length / 4);
            const cf = new Float32Array(c.buffer, c.byteOffset,
                                        c.length / 4);
            this.tris(ctx, vf, 2, i => cf.slice(i * 2, i * 2 + 4));
            ctx.restore(); break;
        }
        case Op.DrawLineStripPx:
        case Op.DrawSegmentsPx: {
            this.clip(ctx, p);
            const b = r.bulk(p, 16);
            const vf = new Float32Array(b.buffer, b.byteOffset,
                                        b.length / 4);
            ctx.strokeStyle = css([p.getFloat32(40, true),
                p.getFloat32(44, true), p.getFloat32(48, true),
                p.getFloat32(52, true)]);
            ctx.lineWidth = Math.max(p.getFloat32(56, true), 0.5);
            ctx.beginPath();
            if (op === Op.DrawLineStripPx) {
                ctx.moveTo(vf[0], vf[1]);
                for (let i = 2; i < vf.length; i += 2)
                    ctx.lineTo(vf[i], vf[i + 1]);
            } else
                for (let i = 0; i + 3 < vf.length; i += 4) {
                    ctx.moveTo(vf[i], vf[i + 1]);
                    ctx.lineTo(vf[i + 2], vf[i + 3]);
                }
            ctx.stroke(); ctx.restore(); break;
        }
        case Op.DrawTextQuads: {
            this.clip(ctx, p);
            const at = this.atlas();
            const b = r.bulk(p, 20);
            if (!at) { ctx.restore(); break; }
            const texH = this.textures.get(p.getUint32(16, true));
            const aw = texH?.w ?? at.width, ah = texH?.h ?? at.height;
            const q = new Float32Array(b.buffer, b.byteOffset,
                                       b.length / 4);
            const tmp = document.createElement('canvas');
            const tc = tmp.getContext('2d')!;
            // Quads arrive as triangle soup: 6 vertex records (8 f32
            // each) per glyph. Every group of 6 reconstructs one quad.
            for (let i = 0; i + 47 < q.length; i += 48) {
                const vts = q.slice(i, i + 48);
                let x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9,
                    u0 = 1e9, v0 = 1e9, u1 = -1e9, v1 = -1e9;
                for (let k = 0; k < 6; k++) {
                    const V = k * 8;
                    x0 = Math.min(x0, vts[V]); x1 = Math.max(x1, vts[V]);
                    y0 = Math.min(y0, vts[V + 1]);
                    y1 = Math.max(y1, vts[V + 1]);
                    u0 = Math.min(u0, vts[V + 2]);
                    u1 = Math.max(u1, vts[V + 2]);
                    v0 = Math.min(v0, vts[V + 3]);
                    v1 = Math.max(v1, vts[V + 3]);
                }
                const w = Math.max(x1 - x0, 0.5),
                      h = Math.max(y1 - y0, 0.5);
                tmp.width = Math.ceil(w); tmp.height = Math.ceil(h);
                tc.globalCompositeOperation = 'copy';
                tc.drawImage(at, u0 * aw, v0 * ah,
                    Math.max((u1 - u0) * aw, 1),
                    Math.max((v1 - v0) * ah, 1), 0, 0, w, h);
                tc.globalCompositeOperation = 'source-in';
                tc.fillStyle = css([q[i + 4], q[i + 5], q[i + 6], 1]);
                tc.globalAlpha = q[i + 7];
                tc.fillRect(0, 0, w, h);
                tc.globalAlpha = 1;
                ctx.drawImage(tmp, x0, y0);
            }
            ctx.restore(); break;
        }
        case Op.DrawInstanced: {
            this.clip(ctx, p);
            const tpl = new Float32Array(r.bulk(p, 16).buffer,
                r.bulk(p, 16).byteOffset, r.bulk(p, 16).length / 4);
            const inst = new Float32Array(r.bulk(p, 40).buffer,
                r.bulk(p, 40).byteOffset, r.bulk(p, 40).length / 4);
            for (let i = 0; i + 7 < inst.length; i += 8) {
                const [ox, oy, sx, sy] = [inst[i], inst[i + 1],
                                          inst[i + 2], inst[i + 3]];
                ctx.fillStyle = css(inst.slice(i + 4, i + 8));
                ctx.beginPath();
                for (let k = 0; k + 1 < tpl.length; k += 2) {
                    const X = ox + tpl[k] * sx, Y = oy + tpl[k + 1] * sy;
                    if (k === 0) ctx.moveTo(X, Y); else ctx.lineTo(X, Y);
                }
                ctx.closePath(); ctx.fill();
            }
            ctx.restore(); break;
        }
        case Op.DrawLines:
        case Op.DrawLineSegs: {
            this.clip(ctx, p);
            const xf = new XformView(p, 32);
            const buf = this.buffers.get(p.getUint32(160, true));
            const n = p.getUint32(164, true);
            if (!buf) { ctx.restore(); break; }
            const vf = new Float32Array(buf.buffer, buf.byteOffset, n * 2);
            ctx.strokeStyle = css([p.getFloat32(168, true),
                p.getFloat32(172, true), p.getFloat32(176, true),
                p.getFloat32(180, true)]);
            ctx.lineWidth = Math.max(p.getFloat32(184, true), 0.5);
            ctx.beginPath();
            if (op === Op.DrawLines) {
                const [x0, y0] = xf.toPx(vf[0], vf[1]);
                ctx.moveTo(x0, y0);
                for (let i = 1; i < n; i++) {
                    const [x, y] = xf.toPx(vf[i * 2], vf[i * 2 + 1]);
                    ctx.lineTo(x, y);
                }
            } else
                for (let i = 0; i + 3 < n * 2; i += 4) {
                    const [x0, y0] = xf.toPx(vf[i], vf[i + 1]);
                    const [x1, y1] = xf.toPx(vf[i + 2], vf[i + 3]);
                    ctx.moveTo(x0, y0); ctx.lineTo(x1, y1);
                }
            ctx.stroke(); ctx.restore(); break;
        }
        case Op.DrawPoints: {
            this.clip(ctx, p);
            const xf = new XformView(p, 32);
            const pos = this.buffers.get(p.getUint32(160, true));
            const col = this.buffers.get(p.getUint32(164, true));
            const siz = this.buffers.get(p.getUint32(168, true));
            const n = p.getUint32(172, true);
            const flags = p.getUint32(176, true);
            if (!pos) { ctx.restore(); break; }
            const pv = new Float32Array(pos.buffer, pos.byteOffset, n * 2);
            const cv = col && (flags & 1)
                ? new Float32Array(col.buffer, col.byteOffset, n * 4)
                : undefined;
            const sv = siz && (flags & 2)
                ? new Float32Array(siz.buffer, siz.byteOffset, n)
                : undefined;
            for (let i = 0; i < n; i++) {
                const [x, y] = xf.toPx(pv[i * 2], pv[i * 2 + 1]);
                const sz = sv ? Math.max(sv[i], 0.5) : 6;
                ctx.fillStyle = css(cv ? Array.from(cv.slice(i * 4, i * 4 + 4))
                                       : xf.color);
                ctx.beginPath(); ctx.arc(x, y, sz * 0.5, 0, 6.2832);
                ctx.fill();
            }
            ctx.restore(); break;
        }
        case Op.DrawTrisData:
        case Op.DrawPie: {
            this.clip(ctx, p);
            const xf = new XformView(p, 16);
            const pos = this.buffers.get(p.getUint32(144, true));
            const col = this.buffers.get(p.getUint32(148, true));
            const n = p.getUint32(152, true);
            if (!pos) { ctx.restore(); break; }
            const pv = new Float32Array(pos.buffer, pos.byteOffset, n * 2);
            const cv = col
                ? new Float32Array(col.buffer, col.byteOffset, n * 4)
                : undefined;
            for (let i = 0; i + 2 < n; i += 3) {
                const P = [[0, 0], [0, 0], [0, 0]].map((_, k) =>
                    xf.toPx(pv[(i + k) * 2], pv[(i + k) * 2 + 1]));
                ctx.fillStyle = css(cv ? Array.from(cv.slice(i * 4, i * 4 + 4))
                    : [p.getFloat32(156, true), p.getFloat32(160, true),
                       p.getFloat32(164, true), p.getFloat32(168, true)]);
                ctx.beginPath(); ctx.moveTo(P[0][0], P[0][1]);
                ctx.lineTo(P[1][0], P[1][1]); ctx.lineTo(P[2][0], P[2][1]);
                ctx.closePath(); ctx.fill();
            }
            ctx.restore(); break;
        }
        case Op.DrawTrisGpu: {
            this.clip(ctx, p);
            const buf = this.buffers.get(p.getUint32(24, true));
            if (!buf) { ctx.restore(); break; }
            const off = Number(p.getBigUint64(28, true));
            const cnt = p.getUint32(36, true);
            const vf = new Float32Array(buf.buffer,
                buf.byteOffset + off, cnt * 6);
            this.tris(ctx, vf, 6, i =>
                Array.from(vf.slice(i + 2, i + 6)));
            ctx.restore(); break;
        }
        case Op.DrawImage: {
            const xf = new XformView(p, 16);
            const gt = this.textures.get(p.getUint32(144, true));
            const lt = this.textures.get(p.getUint32(148, true));
            if (!gt) break;
            // PDrawImage.params = [rgbaMode, originLower, nanT, vMin,
            //                      vMax, xMin, yMin, gridW] @152
            const rgba = gt.fmt === 1 || p.getFloat32(152, true) > 0.5;
            const lower = p.getFloat32(156, true) > 0.5;
            const nanT = p.getFloat32(160, true) > 0.5;
            const vMin = p.getFloat32(164, true),
                  vMax = p.getFloat32(168, true);
            const cv = document.createElement('canvas');
            cv.width = gt.w; cv.height = gt.h;
            const cc = cv.getContext('2d')!;
            const img = cc.createImageData(gt.w, gt.h);
            const lut = lt ? lt.data : undefined;   // rgba8 256×4
            const gf = rgba ? undefined : new Float32Array(gt.data.buffer,
                gt.data.byteOffset, gt.data.length / 4);
            const span = Math.max(vMax - vMin, 1e-30);
            for (let i = 0; i < gt.w * gt.h; i++) {
                if (rgba) {
                    img.data.set(gt.data.subarray(i * 4, i * 4 + 4), i * 4);
                    continue;
                }
                const v = gf![i];
                if (Number.isNaN(v)) {
                    if (!nanT) img.data.set([255, 0, 255, 255], i * 4);
                    continue;
                }
                const t = Math.min(Math.max((v - vMin) / span, 0), 1);
                const li = lut ? Math.min(t * 255, 255) | 0 : 0;
                const px = lut
                    ? [lut[li * 4], lut[li * 4 + 1], lut[li * 4 + 2],
                       lut[li * 4 + 3]]
                    : [t * 255, t * 255, t * 255, 255];
                img.data.set(px, i * 4);
            }
            cc.putImageData(img, 0, 0);
            const f = xf.f;
            this.clip(ctx, p);
            ctx.save();
            ctx.imageSmoothingEnabled = false;
            if (lower) { ctx.translate(0, f[5] + f[7] * 2); ctx.scale(1, -1); }
            ctx.drawImage(cv, f[4], f[5], f[6], f[7]);
            ctx.restore(); ctx.restore();
            break;
        }
        default: break;  // DrawSurface/DrawGrid3D — no 2D fallback
        }
    }
}
