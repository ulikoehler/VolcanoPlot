// web/src/interpreter.ts — op-stream interpreter for @volcanoplot/web
//
// Walks a VolcanoPlot frame produced by the WASM engine and replays it
// through WebGPU. Wire format: see WEBGPU-PLAN.md §2 and
// src/web/OpStream.hpp / OpPayloads.hpp. Record order == execution
// order; compute ops are recorded into a compute pass that is closed
// before the render pass opens.

import TRANSFORM_WGSL from './shaders/transform.wgsl?raw';
import PX_WGSL from './shaders/px.wgsl?raw';
import LINES_WGSL from './shaders/DrawLines.wgsl?raw';
import POINTS_WGSL from './shaders/DrawPoints.wgsl?raw';
import TRISDATA_WGSL from './shaders/DrawTrisData.wgsl?raw';
import TRISGPU_WGSL from './shaders/DrawTrisGpu.wgsl?raw';
import INSTANCED_WGSL from './shaders/DrawInstanced.wgsl?raw';
import IMAGE_WGSL from './shaders/DrawImage.wgsl?raw';
import TEXT_WGSL from './shaders/DrawTextQuads.wgsl?raw';
import TESS_WGSL from './shaders/TessLines.wgsl?raw';

export const VPOP_MAGIC = 0x564f5050; // 'VPOP'
export const OP_VERSION = 1;
export const HEADER_BYTES = 40;
export const XFORM_BYTES = 128;       // TransformUBO / Xform (8×vec4)

// Values must match `enum class Op` in src/web/OpStream.hpp — the
// numbering is sparse (pixel draws 10-15, data draws 20-28, compute
// 40-47); do NOT rely on auto-increment.
export enum Op {
    CreateBuffer = 1, WriteBuffer = 2, ReleaseBuffer = 3,
    CreateTexture = 4, WriteTexture = 5, ReleaseTexture = 6,

    DrawTrisPx = 10, DrawTrisPxVC = 11, DrawLineStripPx = 12,
    DrawSegmentsPx = 13, DrawTextQuads = 14, DrawInstanced = 15,

    DrawLines = 20, DrawLineSegs = 21, DrawPoints = 22,
    DrawTrisData = 23, DrawTrisGpu = 24, DrawPie = 25, DrawImage = 26,
    DrawSurface = 27, DrawGrid3D = 28,

    TessLines = 40, EvalFunc = 41, FuncDef = 42, ReduceMinMax = 43,
    KdeEval2D = 44, HistBins = 45, PcmTess = 46, ViolinKde = 47,
}

export interface FrameHeader {
    frameSeq: bigint; canvasW: number; canvasH: number;
    opCount: number; arenaOffset: number; clearColor: number;
    loadOp: number;
}

const align = (n: number, a: number) => (n + a - 1) & ~(a - 1);
const align4 = (n: number) => align(n, 4);

export class OpReader {
    private dv: DataView<ArrayBuffer>;
    readonly header: FrameHeader;
    readonly arena: Uint8Array<ArrayBuffer>;

    constructor(buf: Uint8Array<ArrayBuffer>) {
        this.dv = new DataView(buf.buffer, buf.byteOffset, buf.byteLength);
        const d = this.dv;
        if (d.getUint32(0, true) !== VPOP_MAGIC)
            throw new Error('bad VPOP magic');
        if (d.getUint16(4, true) !== OP_VERSION)
            throw new Error('unsupported op-stream version');
        this.header = {
            frameSeq: d.getBigUint64(8, true),
            canvasW: d.getUint32(16, true), canvasH: d.getUint32(20, true),
            opCount: d.getUint32(24, true),
            arenaOffset: d.getUint32(28, true),
            clearColor: d.getUint32(32, true),
            loadOp: d.getUint16(6, true) & 1,  // flags bit0
        };
        this.arena = buf.subarray(this.header.arenaOffset) as Uint8Array<ArrayBuffer>;
    }

    *ops(): Generator<{ op: number; p: DataView<ArrayBuffer> }> {
        const d = this.dv;
        let off = HEADER_BYTES;
        for (let i = 0; i < this.header.opCount; i++) {
            const op = d.getUint16(off, true);
            const len = d.getUint32(off + 4, true);
            yield { op, p: new DataView(d.buffer, d.byteOffset + off + 8, len) };
            off += 8 + align4(len);
        }
    }

    /** Resolve BufSrc {kind u8 @0, off u64 @8, len u64 @16} → bytes. */
    bulk(p: DataView<ArrayBuffer>, at: number): Uint8Array<ArrayBuffer> {
        const kind = p.getUint8(at);
        const off = Number(p.getBigUint64(at + 8, true));
        const len = Number(p.getBigUint64(at + 16, true));
        if (kind !== 0) throw new Error('heap bulk src not yet supported');
        return this.arena.subarray(off, off + len) as Uint8Array<ArrayBuffer>;
    }

    clearRGBA(): [number, number, number, number] {
        const c = this.header.clearColor;
        return [c & 255, (c >> 8) & 255, (c >> 16) & 255, (c >>> 24) & 255]
            .map(v => v / 255) as [number, number, number, number];
    }
}

// ── WGSL mini-preprocessor (#ifdef/#else/#endif + transform include) ──

function buildWgsl(src: string, defines: Set<string>): string {
    const lines = src.split('\n');
    const out: string[] = [];
    const stack: boolean[] = [];   // active?
    let active = true;
    for (const l of lines) {
        const t = l.trim();
        if (t.startsWith('#ifdef')) {
            stack.push(active);
            active = active && defines.has(t.slice(6).trim());
        } else if (t.startsWith('#else')) {
            active = stack[stack.length - 1] && !active;
        } else if (t.startsWith('#endif')) {
            active = stack.pop() ?? true;
        } else if (t.startsWith('#define')) {
            // flag marker — already in `defines`
        } else if (active) {
            out.push(l);
        }
    }
    return out.join('\n');
}

interface PipeSpec {
    src: string;               // shader source (pre-transform include)
    defines?: string[];
    transform?: boolean;       // prepend transform.wgsl
    topology: GPUPrimitiveTopology;
    bindings: GPUBindGroupLayoutEntry[];
}

const U = (dyn: boolean): GPUBindGroupLayoutEntry => ({
    binding: 0, visibility: GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT,
    buffer: { type: 'uniform', hasDynamicOffset: dyn },
});
const S = (b: number): GPUBindGroupLayoutEntry => ({
    binding: b, visibility: GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT,
    buffer: { type: 'read-only-storage' },
});
const T = (b: number): GPUBindGroupLayoutEntry => ({
    binding: b, visibility: GPUShaderStage.FRAGMENT,
    texture: { sampleType: 'float' },
});
const SAMP = (b: number): GPUBindGroupLayoutEntry => ({
    binding: b, visibility: GPUShaderStage.FRAGMENT,
    sampler: { type: 'filtering' },
});

// Buffer kind bits (OpGpuServices.cpp)
const K_VERTEX = 1, K_STORAGE = 2, K_INDEX = 4, K_UNIFORM = 8;

interface BufEntry { buf: GPUBuffer; size: number }
interface TexEntry { tex: GPUTexture; view: GPUTextureView }

const TEXFMTS: GPUTextureFormat[] = ['r8unorm', 'rgba8unorm', 'r32float'];

export class Interpreter {
    private buffers = new Map<number, BufEntry>();
    private textures = new Map<number, TexEntry>();
    private pipelines = new Map<string, GPURenderPipeline>();
    private bgls = new Map<string, GPUBindGroupLayout>();
    private sampler!: GPUSampler;

    // Uniform ring — 256 B-aligned slots (min dynamic-offset alignment)
    private uniformRing!: GPUBuffer;
    private uniformCursor = 0;
    // Frame scratch: arena-embedded vertex data uploaded here
    private scratch!: GPUBuffer;
    private scratchCursor = 0;

    constructor(
        private device: GPUDevice,
        private ctx: GPUCanvasContext,
        private format: GPUTextureFormat,
    ) {}

    init() {
        this.sampler = this.device.createSampler({
            magFilter: 'nearest', minFilter: 'nearest',
        });
        this.uniformRing = this.device.createBuffer({
            size: 4 << 20,
            usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
        });
        this.scratch = this.device.createBuffer({
            size: 8 << 20,
            usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST,
        });
        const mk = (key: string, s: PipeSpec) => {
            const bgl = this.device.createBindGroupLayout({
                entries: s.bindings,
            });
            this.bgls.set(key, bgl);
            const src = (s.transform ? TRANSFORM_WGSL + '\n' : '') +
                buildWgsl(s.src, new Set(s.defines ?? []));
            const mod = this.device.createShaderModule({ code: src });
            this.pipelines.set(key, this.device.createRenderPipeline({
                layout: this.device.createPipelineLayout({
                    bindGroupLayouts: [bgl],
                }),
                vertex: { module: mod, entryPoint: 'vs' },
                fragment: { module: mod, entryPoint: 'fs',
                            targets: [{
                                format: this.format,
                                blend: {
                                    color: { srcFactor: 'src-alpha',
                                             dstFactor: 'one-minus-src-alpha',
                                             operation: 'add' },
                                    alpha: { srcFactor: 'zero',
                                             dstFactor: 'one',
                                             operation: 'add' },
                                },
                            }] },
                primitive: { topology: s.topology },
            }));
        };

        const ub = U(false), ud = U(false);
        mk('px.tris', { src: PX_WGSL, topology: 'triangle-list',
                        bindings: [ub, S(1)] });
        mk('px.trisvc', { src: PX_WGSL, defines: ['VERTEX_COLOR'],
                          topology: 'triangle-list',
                          bindings: [ub, S(1), S(2)] });
        mk('px.lineStrip', { src: PX_WGSL, topology: 'line-strip',
                             bindings: [ub, S(1)] });
        mk('px.segs', { src: PX_WGSL, topology: 'line-list',
                        bindings: [ub, S(1)] });
        mk('lines', { src: LINES_WGSL, transform: true,
                      topology: 'line-strip', bindings: [ud, S(1)] });
        mk('linesegs', { src: LINES_WGSL, transform: true,
                         topology: 'line-list', bindings: [ud, S(1)] });
        mk('points.cs', { src: POINTS_WGSL, transform: true,
                          defines: ['HAS_COL', 'HAS_SIZE'],
                          topology: 'triangle-list',
                          bindings: [ud, S(1), S(2), S(3)] });
        mk('points.c', { src: POINTS_WGSL, transform: true,
                         defines: ['HAS_COL'], topology: 'triangle-list',
                         bindings: [ud, S(1), S(2)] });
        mk('points.s', { src: POINTS_WGSL, transform: true,
                         defines: ['HAS_SIZE'], topology: 'triangle-list',
                         bindings: [ud, S(1), S(3)] });
        mk('points', { src: POINTS_WGSL, transform: true,
                       topology: 'triangle-list',
                       bindings: [ud, S(1)] });
        mk('trisData', { src: TRISDATA_WGSL, transform: true,
                         defines: ['HAS_COL'], topology: 'triangle-list',
                         bindings: [ud, S(1), S(2)] });
        mk('trisData.flat', { src: TRISDATA_WGSL, transform: true,
                              topology: 'triangle-list',
                              bindings: [ud, S(1)] });
        mk('trisGpu', { src: TRISGPU_WGSL, topology: 'triangle-list',
                        bindings: [ub, S(1)] });
        mk('pie', { src: TRISDATA_WGSL, transform: true,
                    defines: ['MODE_PIE', 'HAS_COL'],
                    topology: 'triangle-list',
                    bindings: [ud, S(1), S(2)] });
        mk('image', { src: IMAGE_WGSL, transform: true,
                      topology: 'triangle-list',
                      bindings: [ud, T(1), T(2), SAMP(3)] });
        mk('instanced', { src: INSTANCED_WGSL, topology: 'triangle-list',
                          bindings: [ub, S(1), S(2)] });
        mk('text', { src: TEXT_WGSL, topology: 'triangle-list',
                     bindings: [ub, S(1), T(2), SAMP(3)] });
    }

    // ── helpers ──────────────────────────────────────────────────────

    /** Copy `bytes` into the uniform ring; returns dynamic offset. */
    private uboWrite(bytes: ArrayBufferView<ArrayBuffer>): number {
        const off = align(this.uniformCursor, 256);
        this.device.queue.writeBuffer(
            this.uniformRing, off,
            bytes.buffer as ArrayBuffer, bytes.byteOffset,
            bytes.byteLength);
        this.uniformCursor = off + Math.max(bytes.byteLength, 256);
        return off;
    }

    /** Copy arena bytes into the frame scratch storage buffer. */
    private scratchWrite(bytes: Uint8Array<ArrayBuffer>): number {
        const off = align(this.scratchCursor, 256);
        if (off + bytes.byteLength > this.scratch.size)
            throw new Error('frame scratch exhausted');
        this.device.queue.writeBuffer(this.scratch, off, bytes);
        this.scratchCursor = off + bytes.byteLength;
        return off;
    }

    private bindGroup(key: string,
                      entries: GPUBindGroupEntry[]): GPUBindGroup {
        return this.device.createBindGroup({
            layout: this.bgls.get(key)!, entries,
        });
    }

    private bufRef(h: number): GPUBuffer {
        const e = this.buffers.get(h);
        if (!e) throw new Error(`bad buffer handle ${h}`);
        return e.buf;
    }
    private texView(h: number): GPUTextureView {
        const e = this.textures.get(h);
        if (!e) throw new Error(`bad texture handle ${h}`);
        return e.view;
    }

    private scissor(pass: GPURenderPassEncoder, p: DataView<ArrayBuffer>, at = 0) {
        pass.setScissorRect(
            p.getFloat32(at, true), p.getFloat32(at + 4, true),
            Math.max(p.getFloat32(at + 8, true), 1),
            Math.max(p.getFloat32(at + 12, true), 1));
    }
    private viewport(pass: GPURenderPassEncoder, p: DataView<ArrayBuffer>, at: number) {
        pass.setViewport(
            p.getFloat32(at, true), p.getFloat32(at + 4, true),
            Math.max(p.getFloat32(at + 8, true), 1),
            Math.max(p.getFloat32(at + 12, true), 1), 0, 1);
    }

    // ── frame replay ─────────────────────────────────────────────────

    /** Render the frame into `target` (canvas texture by default). */
    draw(frame: Uint8Array<ArrayBuffer>, target?: GPUTextureView) {
        const r = new OpReader(frame);
        const enc = this.device.createCommandEncoder();
        const canvasWH = new Float32Array(
            [r.header.canvasW, r.header.canvasH]);

        // Resource ops run first in the walk — but they must not sit
        // inside an open pass, so we do two passes over the op list:
        // resources+computes into `enc`, then draws into the render
        // pass. (Resource ops are queue-level, not pass-scoped.)
        const drawOps: { op: number; p: DataView<ArrayBuffer> }[] = [];
        for (const { op, p } of r.ops()) {
            if (op <= Op.ReleaseTexture) this.execResource(r, op, p);
            else if (op >= Op.TessLines && !((globalThis as any).VP_NO_COMPUTE)) this.execCompute(r, enc, op, p);
            else {
            const only = (globalThis as any).VP_ONLY as Set<number> | undefined;
            if (!(globalThis as any).VP_NO_DRAW && (!only || only.has(op)))
                drawOps.push({ op, p });
        }
        }

        if (drawOps.length) {
            const [cr, cg, cb, ca] = r.clearRGBA();
            const pass = enc.beginRenderPass({
                colorAttachments: [{
                    view: target ?? this.ctx.getCurrentTexture().createView(),
                    clearValue: { r: cr, g: cg, b: cb, a: ca },
                    loadOp: r.header.loadOp ? 'load' : 'clear',
                    storeOp: 'store',
                }],
            });
            for (const { op, p } of drawOps)
                this.dispatchDraw(pass, r, canvasWH, op, p);
            pass.end();
        }
        this.device.queue.submit([enc.finish()]);
        this.uniformCursor = 0;
        this.scratchCursor = 0;
    }

    /** Render a frame into an offscreen texture and read the pixels
     * back — used by tests (avoids compositing/readback ambiguity on
     * WebGPU canvases). Returns RGBA8 bytes. */
    async capture(frame: Uint8Array<ArrayBuffer>, w: number,
                  h: number): Promise<Uint8Array<ArrayBuffer>> {
        const tex = this.device.createTexture({
            size: { width: w, height: h },
            format: this.format,   // must match the pipeline targets
            usage: GPUTextureUsage.RENDER_ATTACHMENT |
                   GPUTextureUsage.COPY_SRC });
        this.draw(frame, tex.createView());
        const bpr = Math.ceil(w * 4 / 256) * 256;
        const rb = this.device.createBuffer({
            size: bpr * h,
            usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
        const enc = this.device.createCommandEncoder();
        enc.copyTextureToBuffer({ texture: tex },
            { buffer: rb, bytesPerRow: bpr }, { width: w, height: h });
        this.device.queue.submit([enc.finish()]);
        await rb.mapAsync(GPUMapMode.READ);
        const src = new Uint8Array(rb.getMappedRange());
        const out = new Uint8Array(w * h * 4) as Uint8Array<ArrayBuffer>;
        const bgra = this.format.startsWith('bgra');
        for (let y = 0; y < h; y++) {
            const row = src.subarray(y * bpr, y * bpr + w * 4);
            out.set(row, y * w * 4);
            if (bgra) {   // normalize BGRA→RGBA
                for (let i = y * w * 4; i < (y + 1) * w * 4; i += 4) {
                    const t = out[i];
                    out[i] = out[i + 2]; out[i + 2] = t;
                }
            }
        }
        rb.unmap(); rb.destroy(); tex.destroy();
        return out;
    }

    private execResource(r: OpReader, op: number, p: DataView<ArrayBuffer>) {
        switch (op) {
        case Op.CreateBuffer: {
            const h = p.getUint32(0, true);
            const size = Number(p.getBigUint64(4, true));
            const kind = p.getUint8(12);          // usage bitmask
            let usage = GPUBufferUsage.COPY_DST;
            if (kind & K_VERTEX) usage |= GPUBufferUsage.VERTEX;
            if (kind & K_STORAGE) usage |= GPUBufferUsage.STORAGE;
            if (kind & K_INDEX) usage |= GPUBufferUsage.INDEX;
            if (kind & K_UNIFORM) usage |= GPUBufferUsage.UNIFORM;
            // vertex-pulling shaders read vertex bufs as storage
            if (kind & K_VERTEX) usage |= GPUBufferUsage.STORAGE;
            // Per-frame resources are recreated each render — destroy the
            // previous handle's buffer so frames don't leak GPU memory.
            this.buffers.get(h)?.buf.destroy();
            this.buffers.set(h, {
                buf: this.device.createBuffer({ size, usage }), size });
            break;
        }
        case Op.WriteBuffer: {
            const h = p.getUint32(0, true);
            const off = Number(p.getBigUint64(4, true));
            this.device.queue.writeBuffer(
                this.bufRef(h), off, r.bulk(p, 12));
            break;
        }
        case Op.ReleaseBuffer:
            this.buffers.get(p.getUint32(0, true))?.buf.destroy();
            this.buffers.delete(p.getUint32(0, true));
            break;
        case Op.CreateTexture: {
            const h = p.getUint32(0, true);
            this.textures.get(h)?.tex.destroy();
            const w = p.getUint32(4, true), hh = p.getUint32(8, true);
            const fmt = TEXFMTS[p.getUint8(12)] ?? 'rgba8unorm';
            const tex = this.device.createTexture({
                size: { width: w, height: hh },
                format: fmt,
                usage: GPUTextureUsage.TEXTURE_BINDING |
                       GPUTextureUsage.COPY_DST,
            });
            this.textures.set(h, { tex, view: tex.createView() });
            break;
        }
        case Op.WriteTexture: {
            const h = p.getUint32(0, true);
            const w = p.getUint32(12, true), hh = p.getUint32(16, true);
            const fmt = this.textures.get(h)!.tex.format;
            const bpp = fmt === 'r32float' || fmt === 'rgba8unorm' ? 4 : 1;
            this.device.queue.writeTexture(
                { texture: this.textures.get(h)!.tex,
                  origin: [p.getUint32(4, true), p.getUint32(8, true), 0] },
                r.bulk(p, 20),
                { bytesPerRow: w * bpp, rowsPerImage: hh },
                { width: w, height: hh });
            break;
        }
        case Op.ReleaseTexture:
            this.textures.get(p.getUint32(0, true))?.tex.destroy();
            this.textures.delete(p.getUint32(0, true));
            break;
        }
    }

    private tessPipe: GPUComputePipeline | null = null;
    private tessBgl: GPUBindGroupLayout | null = null;

    private ensureTess() {
        if (this.tessPipe) return;
        this.tessBgl = this.device.createBindGroupLayout({ entries: [
            { binding: 0, visibility: GPUShaderStage.COMPUTE,
              buffer: { type: 'uniform' } },
            { binding: 1, visibility: GPUShaderStage.COMPUTE,
              buffer: { type: 'read-only-storage' } },
            { binding: 2, visibility: GPUShaderStage.COMPUTE,
              buffer: { type: 'storage' } },
        ]});
        const mod = this.device.createShaderModule({
            code: TRANSFORM_WGSL + '\n' + TESS_WGSL });
        this.tessPipe = this.device.createComputePipeline({
            layout: this.device.createPipelineLayout({
                bindGroupLayouts: [this.tessBgl] }),
            compute: { module: mod, entryPoint: 'cs' } });
    }

    private execCompute(r: OpReader, enc: GPUCommandEncoder,
                        op: number, p: DataView<ArrayBuffer>) {
        // TODO(M3): EvalFunc / FuncDef / ReduceMinMax / HistBins /
        // KdeEval2D / PcmTess / ViolinKde — mailbox results via
        // mapAsync → _vp_mailbox (WEBGPU-PLAN §6).
        if (op !== Op.TessLines) return;
        this.ensureTess();
        // PTessLines {inBuf,inBase,outBuf,outBase,n,nSeg,hwidth,
        //             join u8, cap u8, miterLimit, r,g,b,a}
        const pc = new DataView(new ArrayBuffer(48));
        pc.setUint32(0,  p.getUint32(16, true), true);   // n
        pc.setUint32(4,  p.getUint32(20, true), true);   // nSeg
        pc.setFloat32(8, p.getFloat32(24, true), true);  // hwidth
        pc.setUint32(12, p.getUint8(28));                // join
        pc.setUint32(16, p.getUint8(29));                // cap
        pc.setFloat32(20, p.getFloat32(30, true), true); // miterLimit
        pc.setUint32(24, p.getUint32(4,  true), true);   // inBase
        pc.setUint32(28, p.getUint32(12, true), true);   // outBase
        pc.setFloat32(32, p.getFloat32(34, true), true); // r
        pc.setFloat32(36, p.getFloat32(38, true), true); // g
        pc.setFloat32(40, p.getFloat32(42, true), true); // b
        pc.setFloat32(44, p.getFloat32(46, true), true); // a
        const off = this.uboWrite(new Uint8Array(pc.buffer));
        const n = p.getUint32(16, true), nSeg = p.getUint32(20, true);
        if (!n || !nSeg) return;
        const pass = enc.beginComputePass();
        pass.setPipeline(this.tessPipe!);
        pass.setBindGroup(0, this.device.createBindGroup({
            layout: this.tessBgl!, entries: [
                { binding: 0, resource: { buffer: this.uniformRing,
                                          offset: off, size: 48 } },
                { binding: 1, resource: { buffer:
                    this.bufRef(p.getUint32(0, true)) } },
                { binding: 2, resource: { buffer:
                    this.bufRef(p.getUint32(8, true)) } },
            ]}));
        pass.dispatchWorkgroups(Math.ceil((n + nSeg) / 256));
        pass.end();
    }

    private dispatchDraw(pass: GPURenderPassEncoder, r: OpReader,
                         canvasWH: Float32Array, op: number,
                         p: DataView<ArrayBuffer>) {
        switch (op) {
        // ── pixel-space ──────────────────────────────────────────────
        case Op.DrawTrisPx: {     // {clip, verts, rgba}
            this.scissor(pass, p);
            const vOff = this.scratchWrite(r.bulk(p, 16));
            const ubo = new Float32Array(6);
            ubo.set(canvasWH, 0);
            ubo.set([p.getFloat32(40, true), p.getFloat32(44, true),
                     p.getFloat32(48, true), p.getFloat32(52, true)], 2);
            const off = this.uboWrite(ubo);
            pass.setPipeline(this.pipelines.get('px.tris')!);
            pass.setBindGroup(0, this.bindGroup('px.tris', [
                { binding: 0, resource: { buffer: this.uniformRing,
                                          offset: off, size: 32 } },
                { binding: 1, resource: { buffer: this.scratch,
                                          offset: vOff } },
            ]));
            const nVerts = Number(p.getBigUint64(32, true)) / 8;
            pass.draw(nVerts);
            break;
        }
        case Op.DrawTrisPxVC: {   // {clip, verts, colors}
            this.scissor(pass, p);
            const vOff = this.scratchWrite(r.bulk(p, 16));
            const cOff = this.scratchWrite(r.bulk(p, 40));
            const ubo = new Float32Array(6); ubo.set(canvasWH, 0);
            const off = this.uboWrite(ubo);
            pass.setPipeline(this.pipelines.get('px.trisvc')!);
            pass.setBindGroup(0, this.bindGroup('px.trisvc', [
                { binding: 0, resource: { buffer: this.uniformRing,
                                          offset: off, size: 32 } },
                { binding: 1, resource: { buffer: this.scratch,
                                          offset: vOff, } },
                { binding: 2, resource: { buffer: this.scratch,
                                          offset: cOff, } },
            ]));
            pass.draw(Number(p.getBigUint64(24 + 8, true)) / 8);
            break;
        }
        case Op.DrawLineStripPx:
        case Op.DrawSegmentsPx: { // {clip, pts, rgba, wPx}
            this.scissor(pass, p);
            const vOff = this.scratchWrite(r.bulk(p, 16));
            const ubo = new Float32Array(6);
            ubo.set(canvasWH, 0);
            ubo.set([p.getFloat32(40, true), p.getFloat32(44, true),
                     p.getFloat32(48, true), p.getFloat32(52, true)], 2);
            const off = this.uboWrite(ubo);
            const key = op === Op.DrawLineStripPx ? 'px.lineStrip'
                                                  : 'px.segs';
            pass.setPipeline(this.pipelines.get(key)!);
            pass.setBindGroup(0, this.bindGroup(key, [
                { binding: 0, resource: { buffer: this.uniformRing,
                                          offset: off, size: 32 } },
                { binding: 1, resource: { buffer: this.scratch,
                                          offset: vOff, } },
            ]));
            pass.draw(Number(p.getBigUint64(24 + 8, true)) / 8);
            break;
        }
        case Op.DrawTextQuads: {  // {clip, atlasTex, quads}
            this.scissor(pass, p);
            const qOff = this.scratchWrite(r.bulk(p, 20));
            const ubo = new Float32Array(6); ubo.set(canvasWH, 0);
            const off = this.uboWrite(ubo);
            pass.setPipeline(this.pipelines.get('text')!);
            pass.setBindGroup(0, this.bindGroup('text', [
                { binding: 0, resource: { buffer: this.uniformRing,
                                          offset: off, size: 32 } },
                { binding: 1, resource: { buffer: this.scratch,
                                          offset: qOff, } },
                { binding: 2, resource:
                    this.texView(p.getUint32(16, true)) },
                { binding: 3, resource: this.sampler },
            ]));
            pass.draw(Number(p.getBigUint64(20 + 16, true)) / 32);
            break;
        }
        case Op.DrawInstanced: {  // {clip, tpl, inst, ubo}
            this.scissor(pass, p);
            const tOff = this.scratchWrite(r.bulk(p, 16));
            const iOff = this.scratchWrite(r.bulk(p, 40));
            const ubo = new Float32Array(4); ubo.set(canvasWH, 0);
            const off = this.uboWrite(ubo);
            pass.setPipeline(this.pipelines.get('instanced')!);
            pass.setBindGroup(0, this.bindGroup('instanced', [
                { binding: 0, resource: { buffer: this.uniformRing,
                                          offset: off, size: 16 } },
                { binding: 1, resource: { buffer: this.scratch,
                                          offset: tOff, } },
                { binding: 2, resource: { buffer: this.scratch,
                                          offset: iOff, } },
            ]));
            const tplVerts = Number(p.getBigUint64(24 + 8, true)) / 8;
            const nInst = Number(p.getBigUint64(48 + 8, true)) / 32;
            pass.draw(tplVerts, nInst);
            break;
        }
        // ── data-space ───────────────────────────────────────────────
        case Op.DrawLines:
        case Op.DrawLineSegs: {   // {clip, viewRect, ubo, buf, count,...}
            this.scissor(pass, p);
            this.viewport(pass, p, 16);
            const off = this.uboWrite(new Uint8Array<ArrayBuffer>(
                p.buffer, p.byteOffset + 32, XFORM_BYTES));
            const key = op === Op.DrawLines ? 'lines' : 'linesegs';
            pass.setPipeline(this.pipelines.get(key)!);
            pass.setBindGroup(0, this.bindGroup(key, [
                { binding: 0, resource: { buffer: this.uniformRing,
                                          offset: off,
                                          size: XFORM_BYTES } },
                { binding: 1, resource: { buffer:
                    this.bufRef(p.getUint32(160, true)) } },
            ]));
            pass.draw(p.getUint32(164, true));
            break;
        }
        case Op.DrawPoints: {     // {clip, viewRect, ubo, pos,col,size,count,flags,marker}
            this.scissor(pass, p);
            this.viewport(pass, p, 16);
            const uboBytes = new Uint8Array<ArrayBuffer>(
                p.buffer, p.byteOffset + 32, XFORM_BYTES).slice();
            // marker[4] @ +180 → ubo.extra (bytes 96..112)
            uboBytes.set(new Uint8Array(p.buffer, p.byteOffset + 180, 16),
                         96);
            const off = this.uboWrite(uboBytes);
            const posBuf = p.getUint32(160, true);
            const colBuf = p.getUint32(164, true);
            const sizeBuf = p.getUint32(168, true);
            const count = p.getUint32(172, true);
            const flags = p.getUint32(176, true);
            const key = 'points' + (flags & 1 ? '.c' : '') +
                                   (flags & 2 ? '.s' : '');
            pass.setPipeline(this.pipelines.get(key)!);
            const entries: GPUBindGroupEntry[] = [
                { binding: 0, resource: { buffer: this.uniformRing,
                                          offset: off,
                                          size: XFORM_BYTES } },
                { binding: 1, resource: { buffer: this.bufRef(posBuf) } },
            ];
            if (flags & 1)
                entries.push({ binding: 2, resource: {
                    buffer: this.bufRef(colBuf) } });
            if (flags & 2)
                entries.push({ binding: 3, resource: {
                    buffer: this.bufRef(sizeBuf) } });
            pass.setBindGroup(0, this.bindGroup(key, entries));
            pass.draw(6, count);
            break;
        }
        case Op.DrawTrisData: {   // {clip, ubo, posBuf, colBuf, n, rgba}
            this.scissor(pass, p);
            const off = this.uboWrite(new Uint8Array<ArrayBuffer>(
                p.buffer, p.byteOffset + 16, XFORM_BYTES));
            const colBuf = p.getUint32(148, true);
            const key = colBuf ? 'trisData' : 'trisData.flat';
            pass.setPipeline(this.pipelines.get(key)!);
            const entries: GPUBindGroupEntry[] = [
                { binding: 0, resource: { buffer: this.uniformRing,
                                          offset: off,
                                          size: XFORM_BYTES } },
                { binding: 1, resource: { buffer:
                    this.bufRef(p.getUint32(144, true)) } },
            ];
            if (colBuf)
                entries.push({ binding: 2, resource: {
                    buffer: this.bufRef(colBuf) } });
            pass.setBindGroup(0, this.bindGroup(key, entries));
            pass.draw(p.getUint32(152, true));
            break;
        }
        case Op.DrawTrisGpu: {    // {clip, resW, resH, buf, byteOff, n}
            this.scissor(pass, p);
            const ubo = new Float32Array(4);
            ubo[0] = p.getFloat32(16, true);      // resW
            ubo[1] = p.getFloat32(20, true);      // resH
            const dv = new DataView(ubo.buffer);
            dv.setUint32(8, p.getUint32(28, true), true);  // byteOff lo
            const off = this.uboWrite(ubo);
            pass.setPipeline(this.pipelines.get('trisGpu')!);
            pass.setBindGroup(0, this.bindGroup('trisGpu', [
                { binding: 0, resource: { buffer: this.uniformRing,
                                          offset: off, size: 16 } },
                { binding: 1, resource: { buffer:
                    this.bufRef(p.getUint32(24, true)) } },
            ]));
            pass.draw(p.getUint32(36, true));
            break;
        }
        case Op.DrawPie: {        // PDrawTrisData + rect={cx,cy,sc,sc}
            this.scissor(pass, p);
            const uboBytes = new Uint8Array<ArrayBuffer>(
                p.buffer, p.byteOffset + 16, XFORM_BYTES).slice();
            // extra2 = canvasWH for the MODE_PIE shader
            new DataView(uboBytes.buffer).setFloat32(120, canvasWH[0], true);
            new DataView(uboBytes.buffer).setFloat32(124, canvasWH[1], true);
            const off = this.uboWrite(uboBytes);
            pass.setPipeline(this.pipelines.get('pie')!);
            pass.setBindGroup(0, this.bindGroup('pie', [
                { binding: 0, resource: { buffer: this.uniformRing,
                                          offset: off,
                                          size: XFORM_BYTES } },
                { binding: 1, resource: { buffer:
                    this.bufRef(p.getUint32(144, true)) } },
                { binding: 2, resource: { buffer:
                    this.bufRef(p.getUint32(148, true)) } },
            ]));
            pass.draw(p.getUint32(152, true));
            break;
        }
        case Op.DrawImage: {      // {viewRect, ubo, gridTex, cmapTex, params}
            this.viewport(pass, p, 0);
            const uboBytes = new Uint8Array<ArrayBuffer>(
                p.buffer, p.byteOffset + 16, XFORM_BYTES).slice();
            // params[8] (32 B @ +152) → extra/extra2 (ubo bytes 96..128)
            uboBytes.set(new Uint8Array(p.buffer, p.byteOffset + 152, 32),
                         96);
            const off = this.uboWrite(uboBytes);
            pass.setPipeline(this.pipelines.get('image')!);
            pass.setBindGroup(0, this.bindGroup('image', [
                { binding: 0, resource: { buffer: this.uniformRing,
                                          offset: off,
                                          size: XFORM_BYTES } },
                { binding: 1, resource:
                    this.texView(p.getUint32(144, true)) },
                { binding: 2, resource:
                    this.texView(p.getUint32(148, true)) },
                { binding: 3, resource: this.sampler },
            ]));
            pass.draw(6);
            break;
        }
        default: break;  // DrawSurface/DrawGrid3D — M2
        }
    }
}
