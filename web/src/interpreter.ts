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
import REDUCE_WGSL from './shaders/ReduceMinMax.wgsl?raw';
import TESS_WGSL from './shaders/TessLines.wgsl?raw';
import SURFACE_WGSL from './shaders/DrawSurface.wgsl?raw';
import GRID3D_WGSL from './shaders/DrawGrid3D.wgsl?raw';
import KDE_WGSL from './shaders/KdeEval2D.wgsl?raw';
import PCM_WGSL from './shaders/PcmTess.wgsl?raw';
import KDE1_WGSL from './shaders/KdeEval1D.wgsl?raw';
import BINS_WGSL from './shaders/HistBins.wgsl?raw';
import BINS2D_WGSL from './shaders/HistBins2D.wgsl?raw';
import HEXBINS_WGSL from './shaders/HexBins.wgsl?raw';
import CONTOUR_WGSL from './shaders/ContourTess.wgsl?raw';
import FFT_WGSL from './shaders/FftSegments.wgsl?raw';
import ENVELOPE_WGSL from './shaders/EnvelopeCols.wgsl?raw';
import TRICONTOUR_WGSL from './shaders/TriContourTess.wgsl?raw';
import QUIVER_WGSL from './shaders/QuiverTess.wgsl?raw';
import FILLBETWEEN_WGSL from './shaders/FillBetweenTess.wgsl?raw';
import STREAMLINES_WGSL from './shaders/Streamlines.wgsl?raw';
import DEPTHSORT_WGSL from './shaders/DepthSort.wgsl?raw';
import SPLAT_WGSL from './shaders/ScatterSplat.wgsl?raw';
import XCORR_WGSL from './shaders/XCorr.wgsl?raw';
import SORTF_WGSL from './shaders/SortFloats.wgsl?raw';
import TRIPCOLOR_WGSL from './shaders/TripcolorTess.wgsl?raw';
import XFORMPTS_WGSL from './shaders/TransformPoints.wgsl?raw';
import BARBS_WGSL from './shaders/BarbsTess.wgsl?raw';
import POLYFILL_WGSL from './shaders/PolyFillMask.wgsl?raw';
import DRAW3D_WGSL from './shaders/Draw3D.wgsl?raw';
import MARKERS_WGSL from './shaders/markers.wgsl?raw';

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
    DrawSegs3D = 29, DrawTris3D = 30, DrawPoints3D = 31,
    DrawBoxes3D = 32,

    TessLines = 40, EvalFunc = 41, FuncDef = 42, ReduceMinMax = 43,
    KdeEval2D = 44, HistBins = 45, PcmTess = 46, ViolinKde = 47,
    HistBins2D = 48, HexBins = 49, ContourTess = 50, FftSegments = 51,
    EnvelopeCols = 52, TriContourTess = 53, DepthSort = 54,
    ScatterSplat = 55, Streamlines = 56, FillBetweenTess = 57,
    QuiverTess = 58, XCorr = 59, SortFloats = 60,
    TripcolorTess = 61, TransformPoints = 62, BarbsTess = 63,
    PolyFillMask = 64,
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
    markers?: boolean;         // prepend markers.wgsl (needs transform)
    topology: GPUPrimitiveTopology;
    bindings: GPUBindGroupLayoutEntry[];
    depth?: boolean;           // enable depth32float test+write (3D)
    blend?: boolean;           // default true (surface disables)
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
/// r32float grids aren't float-filterable without an optional feature —
/// bind as unfilterable + non-filtering sampler.
const TU = (b: number): GPUBindGroupLayoutEntry => ({
    binding: b, visibility: GPUShaderStage.FRAGMENT,
    texture: { sampleType: 'unfilterable-float' },
});
const SAMP_NF = (b: number): GPUBindGroupLayoutEntry => ({
    binding: b, visibility: GPUShaderStage.FRAGMENT,
    sampler: { type: 'non-filtering' },
});

// Buffer kind bits (OpGpuServices.cpp)
const K_VERTEX = 1, K_STORAGE = 2, K_INDEX = 4, K_UNIFORM = 8,
      K_COPYSRC = 16, K_INDIRECT = 32;

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
        /// Mailbox delivery: ReduceMinMax results land here → forwarded
        /// to module._vp_mailbox by the embedding.
        private onMailbox?: (slot: number,
                             v: [number, number, number, number]) => void,
        /// Bulk mailbox results (KDE grids, …): raw readback bytes →
        /// module._vp_mailboxDest/_vp_mailboxDone by the embedding.
        private onMailboxBytes?: (slot: number,
                                  bytes: Uint8Array) => void,
    ) {}

    init() {
        this.sampler = this.device.createSampler({
            magFilter: 'nearest', minFilter: 'nearest',
        });
        this.uniformRing = this.device.createBuffer({
            size: 4 << 20,
            usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
        });
        // 64 MB — contour3d and other segment-heavy plots can push
        // many MB of CPU-tessellated vertex data through one frame.
        this.scratch = this.device.createBuffer({
            size: 64 << 20,
            usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST,
        });
        const mk = (key: string, s: PipeSpec,
                    into: Map<string, GPURenderPipeline> = this.pipelines,
                    depthMode = false) => {
            if (!depthMode) this.specList.push([key, s]);
            let bgl = this.bgls.get(key);
            if (!bgl) {
                bgl = this.device.createBindGroupLayout({
                    entries: s.bindings,
                });
                this.bgls.set(key, bgl);
            }
            const src = (s.transform ? TRANSFORM_WGSL + '\n' : '') +
                (s.markers ? MARKERS_WGSL + '\n' : '') +
                buildWgsl(s.src, new Set(s.defines ?? []));
            const mod = this.device.createShaderModule({ code: src });
            // A pass with a depth attachment requires every pipeline it
            // runs to declare depthStencil state — 2D pipelines get a
            // test-off variant, 'surface' gets real depth.
            const ds: GPUDepthStencilState | undefined =
                (s.depth || depthMode) ? {
                    format: 'depth32float',
                    depthWriteEnabled: s.depth === true && depthMode,
                    // 'less-equal': first frames can contain a repeated
                    // draw group (atlas-init repaint), so a surface may be
                    // drawn twice at identical depth — 'less' would reject
                    // the second draw and expose ops painted in between.
                    depthCompare: s.depth && depthMode ? 'less-equal' : 'always',
                } : undefined;
            into.set(key, this.device.createRenderPipeline({
                layout: this.device.createPipelineLayout({
                    bindGroupLayouts: [bgl],
                }),
                vertex: { module: mod, entryPoint: 'vs' },
                fragment: { module: mod, entryPoint: 'fs',
                            targets: [{
                                format: this.format,
                                blend: s.blend === false ? undefined : {
                                    color: { srcFactor: 'src-alpha',
                                             dstFactor: 'one-minus-src-alpha',
                                             operation: 'add' },
                                    alpha: { srcFactor: 'zero',
                                             dstFactor: 'one',
                                             operation: 'add' },
                                },
                            }] },
                primitive: { topology: s.topology },
                depthStencil: ds,
            }));
        };
        this.mkPipe = mk;

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
        mk('points.c.s', { src: POINTS_WGSL, transform: true,
                          markers: true,
                          defines: ['HAS_COL', 'HAS_SIZE'],
                          topology: 'triangle-list',
                          bindings: [ud, S(1), S(2), S(3)] });
        mk('points.c', { src: POINTS_WGSL, transform: true,
                         markers: true,
                         defines: ['HAS_COL'], topology: 'triangle-list',
                         bindings: [ud, S(1), S(2)] });
        mk('points.s', { src: POINTS_WGSL, transform: true,
                         markers: true,
                         defines: ['HAS_SIZE'], topology: 'triangle-list',
                         bindings: [ud, S(1), S(3)] });
        mk('points', { src: POINTS_WGSL, transform: true, markers: true,
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
                      bindings: [ud, TU(1), T(2), SAMP_NF(3)] });
        mk('instanced', { src: INSTANCED_WGSL, topology: 'triangle-list',
                          bindings: [ub, S(1), S(2)] });
        mk('text', { src: TEXT_WGSL, topology: 'triangle-list',
                     bindings: [ub, S(1), T(2), SAMP(3)] });
        mk('surface', { src: SURFACE_WGSL, topology: 'triangle-list',
                        depth: true, blend: false,
                        bindings: [ub, S(1), S(2)] });
        // Vertex-pull grid (surfacemesh offload): no index buffer.
        mk('surface.pull', { src: SURFACE_WGSL, topology: 'triangle-list',
                             depth: true, blend: false,
                             defines: ['PULL_GRID'],
                             bindings: [ub, S(1)] });
        mk('grid3d', { src: GRID3D_WGSL, topology: 'triangle-list',
                       bindings: [ub] });
        // GPU-projected 3D primitives (projection3d offload). Painter's
        // order, constant z — same raster semantics as the CPU path.
        mk('segs3d', { src: DRAW3D_WGSL, defines: ['MODE_SEGS'],
                       topology: 'line-list', bindings: [ub, S(1)] });
        mk('tris3d', { src: DRAW3D_WGSL, defines: ['MODE_TRIS'],
                       topology: 'triangle-list', bindings: [ub, S(1)] });
        mk('tris3d.c', { src: DRAW3D_WGSL,
                         defines: ['MODE_TRIS', 'HAS_COL'],
                         topology: 'triangle-list',
                         bindings: [ub, S(1), S(2)] });
        // Depth-sorted variants: draw through the DepthSort index buffer.
        mk('tris3d.i', { src: DRAW3D_WGSL, defines: ['MODE_TRIS', 'IDX'],
                         topology: 'triangle-list',
                         bindings: [ub, S(1), S(4)] });
        mk('tris3d.ci', { src: DRAW3D_WGSL,
                          defines: ['MODE_TRIS', 'HAS_COL', 'IDX'],
                          topology: 'triangle-list',
                          bindings: [ub, S(1), S(2), S(4)] });
        mk('points3d.c.s', { src: DRAW3D_WGSL, markers: true,
                             defines: ['MODE_POINTS', 'HAS_COL',
                                       'HAS_SIZE'],
                             topology: 'triangle-list',
                             bindings: [ub, S(1), S(2), S(3)] });
        mk('points3d.c', { src: DRAW3D_WGSL, markers: true,
                           defines: ['MODE_POINTS', 'HAS_COL'],
                           topology: 'triangle-list',
                           bindings: [ub, S(1), S(2)] });
        mk('points3d.s', { src: DRAW3D_WGSL, markers: true,
                           defines: ['MODE_POINTS', 'HAS_SIZE'],
                           topology: 'triangle-list',
                           bindings: [ub, S(1), S(3)] });
        mk('points3d', { src: DRAW3D_WGSL, markers: true,
                         defines: ['MODE_POINTS'],
                         topology: 'triangle-list',
                         bindings: [ub, S(1)] });
        // Instanced 3-D boxes (instancing offload): real clip z +
        // depth test — occlusion without a CPU painter's sort.
        mk('boxes3d', { src: DRAW3D_WGSL, defines: ['MODE_BOXES'],
                        depth: true, blend: false,
                        topology: 'triangle-list',
                        bindings: [ub, S(1)] });
    }

    /** Pipeline set for passes that carry a depth attachment — every
     * pipeline must declare a (possibly inert) depthStencil state to be
     * attachment-compatible. Built lazily on the first 3D frame. */
    private depthPipes?: Map<string, GPURenderPipeline>;
    private specList: [string, PipeSpec][] = [];
    private mkPipe!: (key: string, s: PipeSpec,
                      into: Map<string, GPURenderPipeline>,
                      depthMode: boolean) => void;
    private activePipes: Map<string, GPURenderPipeline> = this.pipelines;

    private pipesFor(depth: boolean) {
        if (!depth) return this.pipelines;
        if (!this.depthPipes) {
            this.depthPipes = new Map();
            for (const [k, s] of this.specList)
                this.mkPipe(k, s, this.depthPipes, true);
        }
        return this.depthPipes;
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
        // Release ops are deferred to after submit: a release recorded
        // mid-stream (a renderer replacing its own buffer) must not
        // invalidate a handle still referenced by earlier draw ops —
        // WebGPU keeps resources referenced by recorded commands alive.
        const releases: { op: number; p: DataView<ArrayBuffer> }[] = [];
        for (const { op, p } of r.ops()) {
            if (op <= Op.ReleaseTexture) {
                if (op === Op.ReleaseBuffer || op === Op.ReleaseTexture)
                    releases.push({ op, p });
                else this.execResource(r, op, p);
            }
            else if (op >= Op.TessLines && !((globalThis as any).VP_NO_COMPUTE)) this.execCompute(r, enc, op, p);
            else {
            const only = (globalThis as any).VP_ONLY as Set<number> | undefined;
            if (!(globalThis as any).VP_NO_DRAW && (!only || only.has(op)))
                drawOps.push({ op, p });
        }
        }

        let depthTex: GPUTexture | undefined;
        if (drawOps.length) {
            const [cr, cg, cb, ca] = r.clearRGBA();
            // 3D ops need a depth buffer sized to the target.
            const needDepth = drawOps.some(d => d.op === Op.DrawSurface ||
                                                d.op === Op.DrawBoxes3D);
            this.activePipes = this.pipesFor(needDepth);
            let depthAttachment: GPURenderPassDepthStencilAttachment | undefined;
            if (needDepth) {
                depthTex = this.device.createTexture({
                    size: { width: r.header.canvasW || 1,
                            height: r.header.canvasH || 1 },
                    format: 'depth32float',
                    usage: GPUTextureUsage.RENDER_ATTACHMENT,
                });
                depthAttachment = {
                    view: depthTex.createView(),
                    depthClearValue: 1.0,
                    depthLoadOp: 'clear',
                    depthStoreOp: 'discard',
                };
            }
            const pass = enc.beginRenderPass({
                colorAttachments: [{
                    view: target ?? this.ctx.getCurrentTexture().createView(),
                    clearValue: { r: cr, g: cg, b: cb, a: ca },
                    loadOp: r.header.loadOp ? 'load' : 'clear',
                    storeOp: 'store',
                }],
                depthStencilAttachment: depthAttachment,
            });
            for (const { op, p } of drawOps)
                this.dispatchDraw(pass, r, canvasWH, op, p);
            pass.end();
            // destroy() is deferred until after submit below.
        }
        this.device.queue.submit([enc.finish()]);
        depthTex?.destroy();
        for (const { op, p } of releases) this.execResource(r, op, p);
        this.flushMailbox();
        this.uniformCursor = 0;
        this.scratchCursor = 0;
    }

    // ── function evaluation (FuncDef/EvalFunc) ───────────────────────
    private funcPipes = new Map<number, GPUComputePipeline>();
    private evalBgl?: GPUBindGroupLayout;

    private ensureEval() {
        if (this.evalBgl) return;
        this.evalBgl = this.device.createBindGroupLayout({ entries: [
            { binding: 0, visibility: GPUShaderStage.COMPUTE,
              buffer: { type: 'storage' } },
            { binding: 1, visibility: GPUShaderStage.COMPUTE,
              buffer: { type: 'uniform' } },
        ]});
    }

    /** Loose GLSL→WGSL for user bodies: bare expressions are wrapped as
     * `y = (expr);` (mirroring EvalRendererVk::wrapBody); common builtin
     * names coincide between the two languages. */
    private glslToWgslBody(body: string): string {
        let b = body.trim();
        if (!/[=;{]/.test(b)) b = `y = (${b});`;
        return b
            .replace(/\bmod\s*\(/g, 'vpMod(')
            .replace(/\batan\s*\(\s*([^,()]+)\s*,/g, 'atan2($1,')
            .replace(/\bfloat\s*\(/g, 'f32(')
            .replace(/\bfloat\s+/g, 'var ')
            .replace(/\bint\s*\(/g, 'i32(')
            .replace(/\bbool\b/g, 'bool')
            .replace(/\bvec([234])\s*\(/g, 'vec$1f(');
    }

    private funcDef(r: OpReader, p: DataView<ArrayBuffer>) {
        const funcId = p.getUint16(0, true);
        const body = new TextDecoder().decode(r.bulk(p, 3));
        const wgsl = `
fn vpMod(a : f32, b : f32) -> f32 { return a - b * floor(a / b); }
struct EvalPC { xBase : f32, xStep : f32, count : u32, pad : u32 };
@group(0) @binding(0) var<storage, read_write> outBuf : array<vec2f>;
@group(0) @binding(1) var<uniform> pc : EvalPC;
@compute @workgroup_size(256)
fn cs(@builtin(global_invocation_id) gid : vec3u) {
    let i = gid.x;
    if (i >= pc.count) { return; }
    let x = pc.xBase + f32(i) * pc.xStep;
    var y = 0.0;
    ${this.glslToWgslBody(body)}
    outBuf[i] = vec2f(x, y);
}`;
        this.ensureEval();
        const mod = this.device.createShaderModule({ code: wgsl });
        void mod.getCompilationInfo().then(info =>
            info.messages.forEach(m => console.warn(
                `[VP WGSL func] ${m.lineNum}:${m.linePos} ${m.message}`)));
        this.funcPipes.set(funcId, this.device.createComputePipeline({
            layout: this.device.createPipelineLayout({
                bindGroupLayouts: [this.evalBgl!] }),
            compute: { module: mod, entryPoint: 'cs' } }));
    }

    // ── mailbox (async compute results → C++) ────────────────────────
    private pendingBulk: { buf: GPUBuffer; slot: number;
                           bytes: number }[] = [];
    private kdePipe?: GPUComputePipeline;
    private kdeBgl?: GPUBindGroupLayout;

    private ensureKde() {
        if (this.kdePipe) return;
        this.kdeBgl = this.device.createBindGroupLayout({ entries: [
            { binding: 0, visibility: GPUShaderStage.COMPUTE,
              buffer: { type: 'uniform' } },
            { binding: 1, visibility: GPUShaderStage.COMPUTE,
              buffer: { type: 'read-only-storage' } },
            { binding: 2, visibility: GPUShaderStage.COMPUTE,
              buffer: { type: 'storage' } },
        ]});
        const mod = this.device.createShaderModule({ code: KDE_WGSL });
        this.kdePipe = this.device.createComputePipeline({
            layout: this.device.createPipelineLayout({
                bindGroupLayouts: [this.kdeBgl] }),
            compute: { module: mod, entryPoint: 'main' } });
    }

    private kde1Pipe?: GPUComputePipeline;
    private kde1Bgl?: GPUBindGroupLayout;

    private ensureKde1() {
        if (this.kde1Pipe) return;
        this.kde1Bgl = this.device.createBindGroupLayout({ entries: [
            { binding: 0, visibility: GPUShaderStage.COMPUTE,
              buffer: { type: 'uniform' } },
            { binding: 1, visibility: GPUShaderStage.COMPUTE,
              buffer: { type: 'read-only-storage' } },
            { binding: 2, visibility: GPUShaderStage.COMPUTE,
              buffer: { type: 'storage' } },
        ]});
        const mod = this.device.createShaderModule({ code: KDE1_WGSL });
        this.kde1Pipe = this.device.createComputePipeline({
            layout: this.device.createPipelineLayout({
                bindGroupLayouts: [this.kde1Bgl] }),
            compute: { module: mod, entryPoint: 'main' } });
    }

    private pcmPipe?: GPUComputePipeline;
    private pcmBgl?: GPUBindGroupLayout;

    private ensurePcm() {
        if (this.pcmPipe) return;
        const sb = (w: boolean): GPUBufferBindingLayout =>
            ({ type: w ? 'storage' : 'read-only-storage' });
        this.pcmBgl = this.device.createBindGroupLayout({ entries: [
            { binding: 0, visibility: GPUShaderStage.COMPUTE,
              buffer: { type: 'uniform' } },
            ...[1, 2, 3, 4].map(binding =>
                ({ binding, visibility: GPUShaderStage.COMPUTE,
                   buffer: sb(false) })),
            { binding: 5, visibility: GPUShaderStage.COMPUTE,
              buffer: sb(true) },
            { binding: 6, visibility: GPUShaderStage.COMPUTE,
              buffer: sb(true) },
        ]});
        const mod = this.device.createShaderModule({ code: PCM_WGSL });
        this.pcmPipe = this.device.createComputePipeline({
            layout: this.device.createPipelineLayout({
                bindGroupLayouts: [this.pcmBgl] }),
            compute: { module: mod, entryPoint: 'main' } });
    }

    private triContPipe?: GPUComputePipeline;
    private triContBgl?: GPUBindGroupLayout;

    private ensureTriContour() {
        if (this.triContPipe) return;
        this.triContBgl = this.device.createBindGroupLayout({ entries: [
            { binding: 0, visibility: GPUShaderStage.COMPUTE,
              buffer: { type: 'uniform' } },
            ...[1, 2, 3, 4, 5].map(binding =>
                ({ binding, visibility: GPUShaderStage.COMPUTE,
                   buffer: { type: 'read-only-storage' as
                             GPUBufferBindingType } })),
            ...[6, 7].map(binding =>
                ({ binding, visibility: GPUShaderStage.COMPUTE,
                   buffer: { type: 'storage' as
                             GPUBufferBindingType } })),
        ]});
        const mod = this.device.createShaderModule({ code: TRICONTOUR_WGSL });
        this.triContPipe = this.device.createComputePipeline({
            layout: this.device.createPipelineLayout({
                bindGroupLayouts: [this.triContBgl] }),
            compute: { module: mod, entryPoint: 'cs' } });
    }

    private quiverPipe?: GPUComputePipeline;
    private quiverBgl?: GPUBindGroupLayout;

    private ensureQuiver() {
        if (this.quiverPipe) return;
        this.quiverBgl = this.device.createBindGroupLayout({ entries: [
            { binding: 0, visibility: GPUShaderStage.COMPUTE,
              buffer: { type: 'uniform' } },
            { binding: 1, visibility: GPUShaderStage.COMPUTE,
              buffer: { type: 'read-only-storage' } },
            { binding: 2, visibility: GPUShaderStage.COMPUTE,
              buffer: { type: 'storage' } },
            { binding: 3, visibility: GPUShaderStage.COMPUTE,
              buffer: { type: 'storage' } },
        ]});
        const mod = this.device.createShaderModule({ code: QUIVER_WGSL });
        this.quiverPipe = this.device.createComputePipeline({
            layout: this.device.createPipelineLayout({
                bindGroupLayouts: [this.quiverBgl] }),
            compute: { module: mod, entryPoint: 'cs' } });
    }

    private fbPipe?: GPUComputePipeline;
    private fbBgl?: GPUBindGroupLayout;

    private ensureFillBetween() {
        if (this.fbPipe) return;
        this.fbBgl = this.device.createBindGroupLayout({ entries: [
            { binding: 0, visibility: GPUShaderStage.COMPUTE,
              buffer: { type: 'uniform' } },
            ...[1, 2, 3, 4].map(binding =>
                ({ binding, visibility: GPUShaderStage.COMPUTE,
                   buffer: { type: 'read-only-storage' as
                             GPUBufferBindingType } })),
            ...[5, 6].map(binding =>
                ({ binding, visibility: GPUShaderStage.COMPUTE,
                   buffer: { type: 'storage' as GPUBufferBindingType } })),
        ]});
        const mod = this.device.createShaderModule({ code: FILLBETWEEN_WGSL });
        this.fbPipe = this.device.createComputePipeline({
            layout: this.device.createPipelineLayout({
                bindGroupLayouts: [this.fbBgl] }),
            compute: { module: mod, entryPoint: 'cs' } });
    }

    private slPipe?: GPUComputePipeline;
    private slBgl?: GPUBindGroupLayout;

    private ensureStreamlines() {
        if (this.slPipe) return;
        this.slBgl = this.device.createBindGroupLayout({ entries: [
            { binding: 0, visibility: GPUShaderStage.COMPUTE,
              buffer: { type: 'uniform' } },
            ...[1, 2, 3].map(binding =>
                ({ binding, visibility: GPUShaderStage.COMPUTE,
                   buffer: { type: 'read-only-storage' as
                             GPUBufferBindingType } })),
            ...[4, 5].map(binding =>
                ({ binding, visibility: GPUShaderStage.COMPUTE,
                   buffer: { type: 'storage' as GPUBufferBindingType } })),
        ]});
        const mod = this.device.createShaderModule({ code: STREAMLINES_WGSL });
        this.slPipe = this.device.createComputePipeline({
            layout: this.device.createPipelineLayout({
                bindGroupLayouts: [this.slBgl] }),
            compute: { module: mod, entryPoint: 'cs' } });
    }

    // ── xcorr (op 59) — same layout shape as the FFT pipeline ───────
    private xcBgl?: GPUBindGroupLayout;
    private xcPipe?: GPUComputePipeline;

    private ensureXcorr() {
        if (this.xcPipe) return;
        this.xcBgl = this.device.createBindGroupLayout({ entries: [
            { binding: 0, visibility: GPUShaderStage.COMPUTE,
              buffer: { type: 'uniform' } },
            ...[1, 2].map(binding =>
                ({ binding, visibility: GPUShaderStage.COMPUTE,
                   buffer: { type: 'read-only-storage' as
                             GPUBufferBindingType } })),
            { binding: 3, visibility: GPUShaderStage.COMPUTE,
              buffer: { type: 'storage' } },
        ]});
        const mod = this.device.createShaderModule({ code: XCORR_WGSL });
        this.xcPipe = this.device.createComputePipeline({
            layout: this.device.createPipelineLayout({
                bindGroupLayouts: [this.xcBgl] }),
            compute: { module: mod, entryPoint: 'cs' } });
    }

    // ── bitonic value sort (op 60) ───────────────────────────────────
    private sfBgl?: GPUBindGroupLayout;
    private sfPipe?: GPUComputePipeline;

    private ensureSortFloats() {
        if (this.sfPipe) return;
        this.sfBgl = this.device.createBindGroupLayout({ entries: [
            { binding: 0, visibility: GPUShaderStage.COMPUTE,
              buffer: { type: 'uniform' } },
            { binding: 1, visibility: GPUShaderStage.COMPUTE,
              buffer: { type: 'storage' } },
        ]});
        const mod = this.device.createShaderModule({ code: SORTF_WGSL });
        this.sfPipe = this.device.createComputePipeline({
            layout: this.device.createPipelineLayout({
                bindGroupLayouts: [this.sfBgl] }),
            compute: { module: mod, entryPoint: 'cs' } });
    }

    // ── tripcolor expansion (op 61) ─────────────────────────────────
    private tcBgl?: GPUBindGroupLayout;
    private tcPipe?: GPUComputePipeline;

    private ensureTripcolor() {
        if (this.tcPipe) return;
        this.tcBgl = this.device.createBindGroupLayout({ entries: [
            { binding: 0, visibility: GPUShaderStage.COMPUTE,
              buffer: { type: 'uniform' } },
            ...[1, 2, 3, 4].map(binding =>
                ({ binding, visibility: GPUShaderStage.COMPUTE,
                   buffer: { type: 'read-only-storage' as
                             GPUBufferBindingType } })),
            ...[5, 6].map(binding =>
                ({ binding, visibility: GPUShaderStage.COMPUTE,
                   buffer: { type: 'storage' as GPUBufferBindingType } })),
        ]});
        const mod = this.device.createShaderModule({ code: TRIPCOLOR_WGSL });
        this.tcPipe = this.device.createComputePipeline({
            layout: this.device.createPipelineLayout({
                bindGroupLayouts: [this.tcBgl] }),
            compute: { module: mod, entryPoint: 'cs' } });
    }

    // ── data→pixel point transform (op 62) ──────────────────────────
    private xfBgl?: GPUBindGroupLayout;
    private xfPipe?: GPUComputePipeline;

    private ensureXform() {
        if (this.xfPipe) return;
        this.xfBgl = this.device.createBindGroupLayout({ entries: [
            { binding: 0, visibility: GPUShaderStage.COMPUTE,
              buffer: { type: 'uniform' } },
            { binding: 1, visibility: GPUShaderStage.COMPUTE,
              buffer: { type: 'read-only-storage' } },
            { binding: 2, visibility: GPUShaderStage.COMPUTE,
              buffer: { type: 'storage' } },
        ]});
        const mod = this.device.createShaderModule({ code: XFORMPTS_WGSL });
        this.xfPipe = this.device.createComputePipeline({
            layout: this.device.createPipelineLayout({
                bindGroupLayouts: [this.xfBgl] }),
            compute: { module: mod, entryPoint: 'cs' } });
    }

    // ── wind-barb expansion (op 63) ─────────────────────────────────
    private barbsBgl?: GPUBindGroupLayout;
    private barbsPipe?: GPUComputePipeline;

    private ensureBarbs() {
        if (this.barbsPipe) return;
        this.barbsBgl = this.device.createBindGroupLayout({ entries: [
            { binding: 0, visibility: GPUShaderStage.COMPUTE,
              buffer: { type: 'uniform' } },
            ...[1, 2, 3, 4, 5].map(binding =>
                ({ binding, visibility: GPUShaderStage.COMPUTE,
                   buffer: { type: 'read-only-storage' as
                             GPUBufferBindingType } })),
            { binding: 6, visibility: GPUShaderStage.COMPUTE,
              buffer: { type: 'storage' } },
        ]});
        const mod = this.device.createShaderModule({ code: BARBS_WGSL });
        this.barbsPipe = this.device.createComputePipeline({
            layout: this.device.createPipelineLayout({
                bindGroupLayouts: [this.barbsBgl] }),
            compute: { module: mod, entryPoint: 'cs' } });
    }

    // ── even-odd scanline polygon fill (op 64) ──────────────────────
    private pfBgl?: GPUBindGroupLayout;
    private pfPipes = new Map<string, GPUComputePipeline>();

    private ensurePolyFill(entry: string) {
        if (this.pfPipes.has(entry)) return;
        if (!this.pfBgl) this.pfBgl = this.device.createBindGroupLayout({
            entries: [
                { binding: 0, visibility: GPUShaderStage.COMPUTE,
                  buffer: { type: 'uniform' } },
                { binding: 1, visibility: GPUShaderStage.COMPUTE,
                  buffer: { type: 'read-only-storage' } },
                { binding: 2, visibility: GPUShaderStage.COMPUTE,
                  buffer: { type: 'read-only-storage' } },
                { binding: 3, visibility: GPUShaderStage.COMPUTE,
                  buffer: { type: 'storage' } },
                { binding: 4, visibility: GPUShaderStage.COMPUTE,
                  buffer: { type: 'storage' } },
            ]});
        const mod = this.device.createShaderModule({ code: POLYFILL_WGSL });
        this.pfPipes.set(entry, this.device.createComputePipeline({
            layout: this.device.createPipelineLayout({
                bindGroupLayouts: [this.pfBgl] }),
            compute: { module: mod, entryPoint: entry } }));
    }

    private splatBgl?: GPUBindGroupLayout;
    private splatPipes = new Map<string, GPUComputePipeline>();

    private ensureSplat(entry: string) {
        if (this.splatPipes.has(entry)) return;
        if (!this.splatBgl) this.splatBgl = this.device.createBindGroupLayout({
            entries: [
                { binding: 0, visibility: GPUShaderStage.COMPUTE,
                  buffer: { type: 'uniform' } },
                { binding: 1, visibility: GPUShaderStage.COMPUTE,
                  buffer: { type: 'read-only-storage' } },
                { binding: 2, visibility: GPUShaderStage.COMPUTE,
                  buffer: { type: 'storage' } },
                { binding: 3, visibility: GPUShaderStage.COMPUTE,
                  buffer: { type: 'storage' } },
            ]});
        const mod = this.device.createShaderModule({ code: SPLAT_WGSL });
        this.splatPipes.set(entry, this.device.createComputePipeline({
            layout: this.device.createPipelineLayout({
                bindGroupLayouts: [this.splatBgl] }),
            compute: { module: mod, entryPoint: entry } }));
    }

    private dsBgl?: GPUBindGroupLayout;
    private dsPipes = new Map<string, GPUComputePipeline>();

    private ensureDepthSort(entry: string) {
        if (this.dsPipes.has(entry)) return;
        if (!this.dsBgl) this.dsBgl = this.device.createBindGroupLayout({
            entries: [
                { binding: 0, visibility: GPUShaderStage.COMPUTE,
                  buffer: { type: 'uniform' } },
                { binding: 1, visibility: GPUShaderStage.COMPUTE,
                  buffer: { type: 'read-only-storage' } },
                { binding: 2, visibility: GPUShaderStage.COMPUTE,
                  buffer: { type: 'storage' } },
                { binding: 3, visibility: GPUShaderStage.COMPUTE,
                  buffer: { type: 'storage' } },
            ]});
        const mod = this.device.createShaderModule({ code: DEPTHSORT_WGSL });
        this.dsPipes.set(entry, this.device.createComputePipeline({
            layout: this.device.createPipelineLayout({
                bindGroupLayouts: [this.dsBgl] }),
            compute: { module: mod, entryPoint: entry } }));
    }

    // ── binning (hist / hist2d / hexbin) ─────────────────────────────
    private binsBgl?: GPUBindGroupLayout;
    private binsPipes = new Map<number, GPUComputePipeline>();

    /** Atomic-count compute pipelines — one per binning opcode. */
    private ensureBins(op: number, src: string) {
        if (!this.binsBgl) {
            this.binsBgl = this.device.createBindGroupLayout({ entries: [
                { binding: 0, visibility: GPUShaderStage.COMPUTE,
                  buffer: { type: 'uniform' } },
                { binding: 1, visibility: GPUShaderStage.COMPUTE,
                  buffer: { type: 'read-only-storage' } },
                { binding: 2, visibility: GPUShaderStage.COMPUTE,
                  buffer: { type: 'storage' } },
            ]});
        }
        if (this.binsPipes.has(op)) return;
        const mod = this.device.createShaderModule({ code: src });
        this.binsPipes.set(op, this.device.createComputePipeline({
            layout: this.device.createPipelineLayout({
                bindGroupLayouts: [this.binsBgl] }),
            compute: { module: mod, entryPoint: 'cs' } }));
    }

    /** Zero a count buffer, run the binning pass, and queue the u32
     * readback for the bulk mailbox. */
    private dispatchBins(enc: GPUCommandEncoder, op: number, src: string,
                         pc: Uint32Array<ArrayBuffer>, srcBuf: number,
                         outBuf: number,
                         n: number, count: number, slot: number) {
        this.ensureBins(op, src);
        const off = this.uboWrite(pc);
        const cpass = enc.beginComputePass();
        cpass.setPipeline(this.binsPipes.get(op)!);
        cpass.setBindGroup(0, this.device.createBindGroup({
            layout: this.binsBgl!, entries: [
                { binding: 0, resource: { buffer: this.uniformRing,
                                          offset: off, size: 32 } },
                { binding: 1, resource: { buffer: this.bufRef(srcBuf) } },
                { binding: 2, resource: { buffer: this.bufRef(outBuf) } },
            ]}));
        cpass.dispatchWorkgroups(Math.ceil(n / 256));
        cpass.end();
        const bytes = count * 4;
        const staging = this.device.createBuffer({
            size: bytes,
            usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST });
        enc.copyBufferToBuffer(this.bufRef(outBuf), 0, staging, 0, bytes);
        this.pendingBulk.push({ buf: staging, slot, bytes });
    }

    // ── batched FFT (spectrum family) ────────────────────────────────
    private fftBgl?: GPUBindGroupLayout;
    private fftPipe?: GPUComputePipeline;
    private fftPrepPipe?: GPUComputePipeline;
    private fftBflyPipe?: GPUComputePipeline;

    private ensureFft() {
        if (this.fftPipe) return;
        this.fftBgl = this.device.createBindGroupLayout({ entries: [
            { binding: 0, visibility: GPUShaderStage.COMPUTE,
              buffer: { type: 'uniform' } },
            ...[1, 2].map((binding): GPUBindGroupLayoutEntry =>
                ({ binding, visibility: GPUShaderStage.COMPUTE,
                   buffer: { type: 'read-only-storage' } })),
            { binding: 3, visibility: GPUShaderStage.COMPUTE,
              buffer: { type: 'storage' } },
        ]});
        const mod = this.device.createShaderModule({ code: FFT_WGSL });
        const pl = this.device.createPipelineLayout({
            bindGroupLayouts: [this.fftBgl] });
        this.fftPipe = this.device.createComputePipeline({
            layout: pl, compute: { module: mod, entryPoint: 'cs' } });
        this.fftPrepPipe = this.device.createComputePipeline({
            layout: pl, compute: { module: mod,
                                   entryPoint: 'segprep' } });
        this.fftBflyPipe = this.device.createComputePipeline({
            layout: pl, compute: { module: mod,
                                   entryPoint: 'bfly' } });
    }

    // ── contour tessellation (marching squares + stroke expansion) ──
    private contourBgl?: GPUBindGroupLayout;
    private contourPipe?: GPUComputePipeline;

    private ensureContour() {
        if (this.contourPipe) return;
        this.contourBgl = this.device.createBindGroupLayout({ entries: [
            { binding: 0, visibility: GPUShaderStage.COMPUTE,
              buffer: { type: 'uniform' } },
            ...[1, 2, 3, 6].map((binding): GPUBindGroupLayoutEntry =>
                ({ binding, visibility: GPUShaderStage.COMPUTE,
                   buffer: { type: 'read-only-storage' } })),
            { binding: 4, visibility: GPUShaderStage.COMPUTE,
              buffer: { type: 'storage' } },
            { binding: 5, visibility: GPUShaderStage.COMPUTE,
              buffer: { type: 'storage' } },
        ]});
        const mod = this.device.createShaderModule({ code: CONTOUR_WGSL });
        this.contourPipe = this.device.createComputePipeline({
            layout: this.device.createPipelineLayout({
                bindGroupLayouts: [this.contourBgl] }),
            compute: { module: mod, entryPoint: 'cs' } });
    }

    private pendingMaps: { buf: GPUBuffer; out: GPUBuffer;
                           slot: number }[] = [];
    private reducePipe?: GPUComputePipeline;
    private reduceBgl?: GPUBindGroupLayout;

    private ensureReduce() {
        if (this.reducePipe) return;
        this.reduceBgl = this.device.createBindGroupLayout({ entries: [
            { binding: 0, visibility: GPUShaderStage.COMPUTE,
              buffer: { type: 'uniform' } },
            { binding: 1, visibility: GPUShaderStage.COMPUTE,
              buffer: { type: 'read-only-storage' } },
            { binding: 2, visibility: GPUShaderStage.COMPUTE,
              buffer: { type: 'storage' } },
        ]});
        const mod = this.device.createShaderModule({ code: REDUCE_WGSL });
        this.reducePipe = this.device.createComputePipeline({
            layout: this.device.createPipelineLayout({
                bindGroupLayouts: [this.reduceBgl] }),
            compute: { module: mod, entryPoint: 'cs' } });
    }

    /** After submit: map staging buffers and deliver mailbox results. */
    private flushMailbox() {
        const jobs = this.pendingMaps.splice(0);
        const bulk = this.pendingBulk.splice(0);
        for (const { buf, slot, bytes } of bulk) {
            void buf.mapAsync(GPUMapMode.READ).then(() => {
                this.onMailboxBytes?.(slot,
                    new Uint8Array(buf.getMappedRange())
                        .slice(0, bytes));
                buf.unmap(); buf.destroy();
            });
        }
        for (const { buf, out, slot } of jobs) {
            void buf.mapAsync(GPUMapMode.READ).then(() => {
                const u = new Uint32Array(buf.getMappedRange());
                const dv = new DataView(new ArrayBuffer(4));
                const dec = (k: number) => {
                    // inverse of the WGSL enc() key map
                    dv.setUint32(0,
                        (k & 0x80000000) ? (k & 0x7fffffff)
                                         : (~k >>> 0), true);
                    return dv.getFloat32(0, true);
                };
                this.onMailbox?.(slot,
                    [dec(u[0]), dec(u[1]), dec(u[2]), dec(u[3])]);
                buf.unmap(); buf.destroy(); out.destroy();
            });
        }
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
            if (kind & K_COPYSRC) usage |= GPUBufferUsage.COPY_SRC;
            if (kind & K_INDIRECT) usage |= GPUBufferUsage.INDIRECT;
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

    /** Zero-filled storage buffer bound where a shader declares a
     * storage binding the current draw never reads (the dash buffers on
     * a solid stroke). */
    private dummyStorage?: GPUBuffer;

    private ensureTess() {
        if (this.tessPipe) return;
        this.tessBgl = this.device.createBindGroupLayout({ entries: [
            { binding: 0, visibility: GPUShaderStage.COMPUTE,
              buffer: { type: 'uniform' } },
            { binding: 1, visibility: GPUShaderStage.COMPUTE,
              buffer: { type: 'read-only-storage' } },
            { binding: 2, visibility: GPUShaderStage.COMPUTE,
              buffer: { type: 'storage' } },
            { binding: 3, visibility: GPUShaderStage.COMPUTE,
              buffer: { type: 'read-only-storage' } },
            { binding: 4, visibility: GPUShaderStage.COMPUTE,
              buffer: { type: 'read-only-storage' } },
        ]});
        // A solid stroke never reads the dash buffers, but the layout
        // declares them — bind a zeroed dummy instead of leaving the
        // entry undefined (WebGPU rejects undefined members).
        this.dummyStorage = this.device.createBuffer({
            size: 256,
            usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST });
        this.device.queue.writeBuffer(this.dummyStorage, 0,
            new Uint8Array(256));
        const mod = this.device.createShaderModule({
            code: TRANSFORM_WGSL + '\n' + TESS_WGSL });
        this.tessPipe = this.device.createComputePipeline({
            layout: this.device.createPipelineLayout({
                bindGroupLayouts: [this.tessBgl] }),
            compute: { module: mod, entryPoint: 'cs' } });
    }

    private execCompute(r: OpReader, enc: GPUCommandEncoder,
                        op: number, p: DataView<ArrayBuffer>) {
        // HistBins: CPU covers it (GPU binning loses to threads).
        if (op === Op.ViolinKde) {
            // PViolinKde {inBuf, n, outBuf, ne, lo, step, bw, mailbox}
            this.ensureKde1();
            const ne = p.getUint32(12, true);
            const bytes = ne * 4;
            if (!p.getUint32(4, true) || !bytes) return;
            const pc = new DataView(new ArrayBuffer(32));
            pc.setUint32(0, p.getUint32(4, true), true);   // ns
            pc.setUint32(4, ne, true);                     // ne
            pc.setFloat32(8,  p.getFloat32(16, true), true); // lo
            pc.setFloat32(12, p.getFloat32(20, true), true); // step
            pc.setFloat32(16, p.getFloat32(24, true), true); // bw
            const off = this.uboWrite(new Uint8Array(pc.buffer));
            const cpass = enc.beginComputePass();
            cpass.setPipeline(this.kde1Pipe!);
            cpass.setBindGroup(0, this.device.createBindGroup({
                layout: this.kde1Bgl!, entries: [
                    { binding: 0, resource: { buffer: this.uniformRing,
                                              offset: off, size: 48 } },
                    { binding: 1, resource: { buffer:
                        this.bufRef(p.getUint32(0, true)) } },
                    { binding: 2, resource: { buffer:
                        this.bufRef(p.getUint32(8, true)) } },
                ]}));
            cpass.dispatchWorkgroups(Math.ceil(ne / 256));
            cpass.end();
            const staging = this.device.createBuffer({
                size: bytes,
                usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST });
            enc.copyBufferToBuffer(
                this.bufRef(p.getUint32(8, true)), 0, staging, 0, bytes);
            this.pendingBulk.push({ buf: staging,
                slot: p.getUint32(28, true), bytes });
            return;
        }
        if (op === Op.PcmTess) {
            // PPcmTess {xBuf,yBuf,tBuf,lutBuf,posBuf,colBuf,
            //           nCols,nRows,gouraud,flags}
            this.ensurePcm();
            const gouraud = p.getUint32(32, true);
            const cells = gouraud
                ? (p.getUint32(24, true) - 1) * (p.getUint32(28, true) - 1)
                : p.getUint32(24, true) * p.getUint32(28, true);
            if (!cells) return;
            const pc = new DataView(new ArrayBuffer(16));
            for (let k = 0; k < 4; k++)
                pc.setUint32(k * 4, p.getUint32(24 + k * 4, true), true);
            const off = this.uboWrite(new Uint8Array(pc.buffer));
            const cpass = enc.beginComputePass();
            cpass.setPipeline(this.pcmPipe!);
            cpass.setBindGroup(0, this.device.createBindGroup({
                layout: this.pcmBgl!, entries: [
                    { binding: 0, resource: { buffer: this.uniformRing,
                                              offset: off, size: 16 } },
                    ...[1, 2, 3, 4, 5, 6].map(binding =>
                        ({ binding, resource: { buffer:
                            this.bufRef(p.getUint32((binding - 1) * 4,
                                                    true)) } })),
                ]}));
            cpass.dispatchWorkgroups(Math.ceil(cells / 256));
            cpass.end();
            return;
        }
        if (op === Op.FuncDef) { this.funcDef(r, p); return; }
        if (op === Op.KdeEval2D) {
            // PKdeEval2D {inBuf, n, outBuf, gridW, gridH, xMin, xStep,
            //             yMin, yStep, inv2bwX2, inv2bwY2, norm, mailbox}
            this.ensureKde();
            const n = p.getUint32(4, true);
            const gridW = p.getUint32(12, true),
                  gridH = p.getUint32(16, true);
            const bytes = gridW * gridH * 4;
            if (!n || !bytes) return;
            const pc = new DataView(new ArrayBuffer(48));
            pc.setUint32(0, n, true);
            pc.setUint32(4, gridW, true);
            pc.setUint32(8, gridH, true);
            // @12 unused (mailbox stays CPU-side)
            for (let k = 0; k < 6; k++)
                pc.setFloat32(16 + k * 4,
                              p.getFloat32(20 + k * 4, true), true);
            const off = this.uboWrite(new Uint8Array(pc.buffer));
            const cpass = enc.beginComputePass();
            cpass.setPipeline(this.kdePipe!);
            cpass.setBindGroup(0, this.device.createBindGroup({
                layout: this.kdeBgl!, entries: [
                    { binding: 0, resource: { buffer: this.uniformRing,
                                              offset: off, size: 48 } },
                    { binding: 1, resource: { buffer:
                        this.bufRef(p.getUint32(0, true)) } },
                    { binding: 2, resource: { buffer:
                        this.bufRef(p.getUint32(8, true)) } },
                ]}));
            cpass.dispatchWorkgroups(Math.ceil(gridW / 8),
                                   Math.ceil(gridH / 8));
            cpass.end();
            const staging = this.device.createBuffer({
                size: bytes,
                usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST });
            enc.copyBufferToBuffer(
                this.bufRef(p.getUint32(8, true)), 0, staging, 0, bytes);
            this.pendingBulk.push({ buf: staging,
                slot: p.getUint32(48, true), bytes });
            return;
        }
        if (op === Op.EvalFunc) {
            // PEvalFunc {outBuf u32, xMin f64, xMax f64, count u32,
            //            funcId u16}
            const pipe = this.funcPipes.get(p.getUint16(24, true));
            const count = p.getUint32(20, true);
            if (!pipe || !count) return;
            const xMin = p.getFloat64(4, true);
            const xMax = p.getFloat64(12, true);
            const pc = new DataView(new ArrayBuffer(16));
            pc.setFloat32(0, xMin, true);
            pc.setFloat32(4, count > 1
                ? (xMax - xMin) / (count - 1) : 0, true);
            pc.setUint32(8, count, true);
            const off = this.uboWrite(new Uint8Array(pc.buffer));
            const cpass = enc.beginComputePass();
            cpass.setPipeline(pipe);
            cpass.setBindGroup(0, this.device.createBindGroup({
                layout: this.evalBgl!, entries: [
                    { binding: 0, resource: { buffer:
                        this.bufRef(p.getUint32(0, true)) } },
                    { binding: 1, resource: { buffer: this.uniformRing,
                                              offset: off, size: 16 } },
                ]}));
            cpass.dispatchWorkgroups(Math.ceil(count / 256));
            cpass.end();
            return;
        }
        if (op === Op.ReduceMinMax) {
            // PReduceMinMax {inBuf, count, mailbox}
            this.ensureReduce();
            const inBuf = p.getUint32(0, true);
            const count = p.getUint32(4, true);
            const slot = p.getUint32(8, true);
            const out = this.device.createBuffer({
                size: 16,
                usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC |
                       GPUBufferUsage.COPY_DST });
            // Sentinel init: {u32::MAX, 0, u32::MAX, 0} key space.
            this.device.queue.writeBuffer(out, 0,
                new Uint32Array([0xffffffff, 0, 0xffffffff, 0]));
            const staging = this.device.createBuffer({
                size: 16,
                usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST });
            const pc = new Uint32Array(4); pc[0] = count;
            const off = this.uboWrite(pc);
            const cpass = enc.beginComputePass();
            cpass.setPipeline(this.reducePipe!);
            cpass.setBindGroup(0, this.device.createBindGroup({
                layout: this.reduceBgl!, entries: [
                    { binding: 0, resource: { buffer: this.uniformRing,
                                              offset: off, size: 16 } },
                    { binding: 1, resource: { buffer:
                        this.bufRef(inBuf) } },
                    { binding: 2, resource: { buffer: out } },
                ]}));
            cpass.dispatchWorkgroups(1);
            cpass.end();
            enc.copyBufferToBuffer(out, 0, staging, 0, 16);
            this.pendingMaps.push({ buf: staging, out, slot });
            return;
        }
        if (op === Op.ContourTess) {
            // PContourTess {gridBuf, levelsBuf, colBuf, outBuf,
            //               counterBuf, gridW, gridH, nLevels,
            //               bx, ax, by, ay, hwidth, pad, maxVerts, pad}
            const w = p.getUint32(20, true), h = p.getUint32(24, true);
            const nLevels = p.getUint32(28, true);
            if (w < 2 || h < 2 || !nLevels) return;
            this.ensureContour();
            const pc = new DataView(new ArrayBuffer(64));
            pc.setUint32(0, w, true); pc.setUint32(4, h, true);
            pc.setUint32(8, nLevels, true);
            pc.setFloat32(16, p.getFloat32(32, true), true);  // bx
            pc.setFloat32(20, p.getFloat32(36, true), true);  // ax
            pc.setFloat32(24, p.getFloat32(40, true), true);  // by
            pc.setFloat32(28, p.getFloat32(44, true), true);  // ay
            pc.setFloat32(32, p.getFloat32(48, true), true);  // hwidth
            // PContourTess: maxVerts@56, dashBuf@60, dashMul@64.
            pc.setUint32(48, p.getUint32(56, true), true);    // maxVerts
            pc.setUint32(60, p.getUint32(64, true), true);    // dashMul
            const off = this.uboWrite(new Uint8Array(pc.buffer));
            const pass = enc.beginComputePass();
            pass.setPipeline(this.contourPipe!);
            pass.setBindGroup(0, this.device.createBindGroup({
                layout: this.contourBgl!, entries: [
                    { binding: 0, resource: { buffer: this.uniformRing,
                                              offset: off, size: 64 } },
                    { binding: 1, resource: { buffer:
                        this.bufRef(p.getUint32(0, true)) } },
                    { binding: 2, resource: { buffer:
                        this.bufRef(p.getUint32(4, true)) } },
                    { binding: 3, resource: { buffer:
                        this.bufRef(p.getUint32(8, true)) } },
                    { binding: 4, resource: { buffer:
                        this.bufRef(p.getUint32(12, true)) } },
                    { binding: 5, resource: { buffer:
                        this.bufRef(p.getUint32(16, true)) } },
                    { binding: 6, resource: { buffer:
                        this.bufRef(p.getUint32(60, true)) } },
                ]}));
            const cells = (w - 1) * (h - 1) * nLevels;
            pass.dispatchWorkgroups(Math.ceil(cells / 64));
            pass.end();
            return;
        }
        if (op === Op.FftSegments) {
            // PFftSegments {sigBuf, winBuf, outBuf, n, step, numSegs,
            //               sigLen, mailbox}
            const n = p.getUint32(12, true);
            const numSegs = p.getUint32(20, true);
            if (n < 2 || (n & (n - 1)) !== 0 || !numSegs) return;
            this.ensureFft();
            const mkBg = (off: number) => this.device.createBindGroup({
                layout: this.fftBgl!, entries: [
                    { binding: 0, resource: { buffer: this.uniformRing,
                                              offset: off, size: 32 } },
                    { binding: 1, resource: { buffer:
                        this.bufRef(p.getUint32(0, true)) } },
                    { binding: 2, resource: { buffer:
                        this.bufRef(p.getUint32(4, true)) } },
                    { binding: 3, resource: { buffer:
                        this.bufRef(p.getUint32(8, true)) } },
                ]});
            const mkPc = (stage: number) => {
                const pcv = new Uint32Array(8);
                pcv[0] = n; pcv[1] = p.getUint32(16, true);
                pcv[2] = numSegs; pcv[3] = p.getUint32(24, true);
                // detrend mode — payloads written before the field
                // existed stop at 32 bytes; treat as "no detrend".
                pcv[4] = p.byteLength >= 36
                    ? p.getUint32(32, true) : 0;
                pcv[5] = stage;
                return this.uboWrite(pcv);
            };
            if (n <= 1024) {
                // Workgroup path: one workgroup per segment.
                const pass = enc.beginComputePass();
                pass.setPipeline(this.fftPipe!);
                pass.setBindGroup(0, mkBg(mkPc(0)));
                pass.dispatchWorkgroups(numSegs);
                pass.end();
            } else {
                // Global-memory path: serial per-segment prep, then
                // one disjoint-pair butterfly pass per FFT stage.
                let pass = enc.beginComputePass();
                pass.setPipeline(this.fftPrepPipe!);
                pass.setBindGroup(0, mkBg(mkPc(0)));
                pass.dispatchWorkgroups(Math.ceil(numSegs / 64));
                pass.end();
                const stages = Math.log2(n) | 0;
                for (let s = 0; s < stages; s++) {
                    pass = enc.beginComputePass();
                    pass.setPipeline(this.fftBflyPipe!);
                    pass.setBindGroup(0, mkBg(mkPc(s)));
                    pass.dispatchWorkgroups(
                        Math.ceil(numSegs * n / 2 / 256));
                    pass.end();
                }
            }
            const bytes = numSegs * n * 2 * 4;
            const staging = this.device.createBuffer({
                size: bytes,
                usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST });
            enc.copyBufferToBuffer(
                this.bufRef(p.getUint32(8, true)), 0, staging, 0, bytes);
            this.pendingBulk.push({ buf: staging,
                slot: p.getUint32(28, true), bytes });
            return;
        }
        if (op === Op.HistBins) {
            // PHistBins {srcBuf, n, binsBuf, e0, invW, nBins, mailbox}
            const n = p.getUint32(4, true);
            const nBins = p.getUint32(20, true);
            if (!n || !nBins) return;
            const outBuf = p.getUint32(8, true);
            // Atomic accumulation needs a zeroed target.
            this.device.queue.writeBuffer(this.bufRef(outBuf), 0,
                new Uint32Array(nBins));
            const pc = new Uint32Array(8);
            pc[0] = n; pc[1] = nBins;
            const dv = new DataView(pc.buffer);
            dv.setFloat32(16, p.getFloat32(12, true), true);  // e0
            dv.setFloat32(20, p.getFloat32(16, true), true);  // invW
            this.dispatchBins(enc, op, BINS_WGSL, pc, p.getUint32(0, true),
                              outBuf, n, nBins, p.getUint32(24, true));
            return;
        }
        if (op === Op.HistBins2D) {
            // PHistBins2D {xyBuf, n, binsBuf, x0, invWX, y0, invWY,
            //              nBinsX, nBinsY, mailbox}
            const n = p.getUint32(4, true);
            const nBinsX = p.getUint32(28, true);
            const nBinsY = p.getUint32(32, true);
            if (!n || !nBinsX || !nBinsY) return;
            const outBuf = p.getUint32(8, true);
            const count = nBinsX * nBinsY;
            this.device.queue.writeBuffer(this.bufRef(outBuf), 0,
                new Uint32Array(count));
            const pc = new Uint32Array(8);
            pc[0] = n; pc[1] = nBinsX; pc[2] = nBinsY;
            const dv = new DataView(pc.buffer);
            dv.setFloat32(16, p.getFloat32(12, true), true);  // x0
            dv.setFloat32(20, p.getFloat32(16, true), true);  // invWX
            dv.setFloat32(24, p.getFloat32(20, true), true);  // y0
            dv.setFloat32(28, p.getFloat32(24, true), true);  // invWY
            this.dispatchBins(enc, op, BINS2D_WGSL, pc,
                              p.getUint32(0, true), outBuf, n, count,
                              p.getUint32(36, true));
            return;
        }
        if (op === Op.HexBins) {
            // PHexBins {xyBuf, n, outBuf, xMin, yMin, sx, sy, nx, ny,
            //           mailbox}
            const n = p.getUint32(4, true);
            const nx = p.getUint32(28, true);
            const ny = p.getUint32(32, true);
            if (!n || !nx || !ny) return;
            const outBuf = p.getUint32(8, true);
            const count = (nx + 1) * (ny + 1) + nx * ny;
            this.device.queue.writeBuffer(this.bufRef(outBuf), 0,
                new Uint32Array(count));
            const pc = new Uint32Array(8);
            pc[0] = n; pc[1] = nx; pc[2] = ny;
            const dv = new DataView(pc.buffer);
            dv.setFloat32(16, p.getFloat32(12, true), true);  // xMin
            dv.setFloat32(20, p.getFloat32(16, true), true);  // yMin
            dv.setFloat32(24, p.getFloat32(20, true), true);  // sx
            dv.setFloat32(28, p.getFloat32(24, true), true);  // sy
            this.dispatchBins(enc, op, HEXBINS_WGSL, pc,
                              p.getUint32(0, true), outBuf, n, count,
                              p.getUint32(36, true));
            return;
        }
        if (op === Op.EnvelopeCols) {
            // PEnvelopeCols {xyBuf, n, outBuf, ax, kx, cx0, cx1,
            //                nCols, mailbox}
            const n = p.getUint32(4, true);
            const W = p.getUint32(28, true);
            if (!n || !W) return;
            const pc = new DataView(new ArrayBuffer(32));
            pc.setFloat32(0, p.getFloat32(12, true), true);  // ax
            pc.setFloat32(4, p.getFloat32(16, true), true);  // kx
            pc.setInt32(8, p.getInt32(20, true), true);      // cx0
            pc.setInt32(12, p.getInt32(24, true), true);     // cx1
            pc.setUint32(16, n, true);                       // n
            pc.setUint32(20, W, true);                       // nCols —
            // point count drives the loop bound, column count the
            // mn/mx region split.
            const outBuf = p.getUint32(8, true);
            this.dispatchBins(enc, op, ENVELOPE_WGSL,
                              new Uint32Array(pc.buffer),
                              p.getUint32(0, true), outBuf, n, W * 2,
                              p.getUint32(36, true));
            return;
        }
        if (op === Op.TriContourTess) {
            // PTriContourTess {xyzBuf, trisBuf, levelsBuf, colBuf,
            //   outBuf, counterBuf, dashBuf, nTris, nLevels,
            //   bx,ax,by,ay, hwidth, mode, maxVerts, dashMul}
            const nTris = p.getUint32(28, true);
            const nLv = p.getUint32(32, true);
            if (!nTris || !nLv) return;
            this.ensureTriContour();
            const pc = new DataView(new ArrayBuffer(64));
            pc.setUint32(0, nTris, true);
            pc.setUint32(4, nLv, true);
            pc.setUint32(8, p.getUint32(56, true), true);   // mode
            pc.setFloat32(16, p.getFloat32(36, true), true); // bx
            pc.setFloat32(20, p.getFloat32(40, true), true); // ax
            pc.setFloat32(24, p.getFloat32(44, true), true); // by
            pc.setFloat32(28, p.getFloat32(48, true), true); // ay
            pc.setFloat32(32, p.getFloat32(52, true), true); // hwidth
            pc.setUint32(48, p.getUint32(60, true), true);   // maxVerts
            pc.setUint32(52, p.getUint32(64, true), true);   // dashMul
            const off = this.uboWrite(new Uint8Array(pc.buffer));
            const cpass = enc.beginComputePass();
            cpass.setPipeline(this.triContPipe!);
            cpass.setBindGroup(0, this.device.createBindGroup({
                layout: this.triContBgl!, entries: [
                    { binding: 0, resource: { buffer: this.uniformRing,
                                              offset: off, size: 64 } },
                    // Shader order is xyz, tris, levels, colors,
                    // dashes, soup, args; the payload stores
                    // out/counter/dash at offsets 16/20/24.
                    ...[0, 4, 8, 12, 24, 16, 20].map((off2, i) =>
                        ({ binding: i + 1, resource: { buffer:
                            this.bufRef(p.getUint32(off2, true)) } })),
                ]}));
            cpass.dispatchWorkgroups(Math.ceil(nTris * nLv / 64));
            cpass.end();
            return;
        }
        if (op === Op.ScatterSplat) {
            // PScatterSplat {xyBuf, densBuf, packBuf, n, W, H, rowStride,
            //   tex, bx, ax, by, ay, radius, maxDensity}
            const n = p.getUint32(12, true);
            const W = p.getUint32(16, true);
            const H = p.getUint32(20, true);
            const rowStride = p.getUint32(24, true);
            const tex = p.getUint32(28, true);
            if (!n || !W || !H || !rowStride) return;
            this.ensureSplat('splat');
            this.ensureSplat('pack');
            const xyBuf = p.getUint32(0, true);
            const densBuf = p.getUint32(4, true);
            const packBuf = p.getUint32(8, true);
            // Density grid starts empty — the splat pass accumulates.
            this.device.queue.writeBuffer(this.bufRef(densBuf), 0,
                new Uint32Array(W * H));
            const pc = new DataView(new ArrayBuffer(48));
            pc.setUint32(0, n, true);
            pc.setUint32(4, W, true);
            pc.setUint32(8, H, true);
            pc.setUint32(12, rowStride, true);
            pc.setFloat32(16, p.getFloat32(32, true), true);  // radius
            pc.setFloat32(20, p.getFloat32(36, true), true);  // maxDensity
            pc.setFloat32(24, NaN, true);                     // empty cell
            pc.setFloat32(28, p.getFloat32(40, true), true);  // bx
            pc.setFloat32(32, p.getFloat32(44, true), true);  // ax
            pc.setFloat32(36, p.getFloat32(48, true), true);  // by
            pc.setFloat32(40, p.getFloat32(52, true), true);  // ay
            const off = this.uboWrite(new Uint8Array(pc.buffer));
            const bind = (entry: string, count: number) => {
                const cpass = enc.beginComputePass();
                cpass.setPipeline(this.splatPipes.get(entry)!);
                cpass.setBindGroup(0, this.device.createBindGroup({
                    layout: this.splatBgl!, entries: [
                        { binding: 0, resource: { buffer: this.uniformRing,
                                                  offset: off, size: 48 } },
                        { binding: 1, resource: { buffer:
                            this.bufRef(xyBuf) } },
                        { binding: 2, resource: { buffer:
                            this.bufRef(densBuf) } },
                        { binding: 3, resource: { buffer:
                            this.bufRef(packBuf) } },
                    ]}));
                cpass.dispatchWorkgroups(Math.ceil(count / 64));
                cpass.end();
            };
            bind('splat', n);
            bind('pack', W * H);
            // Blit the padded density into the r32float texture the image
            // draw samples through the colormap.
            const e2 = this.textures.get(tex);
            if (e2) enc.copyBufferToTexture(
                { buffer: this.bufRef(packBuf),
                  bytesPerRow: rowStride * 4, rowsPerImage: H },
                { texture: e2.tex },
                { width: W, height: H, depthOrArrayLayers: 1 });
            return;
        }
        if (op === Op.DepthSort) {
            // PDepthSort {posBuf, idxBuf, keyBuf, nTris, nPad, vp[16]}
            const nTris = p.getUint32(12, true);
            const nPad = p.getUint32(16, true);
            if (!nTris || !nPad) return;
            this.ensureDepthSort('keygen');
            this.ensureDepthSort('bitonic');
            const posBuf = p.getUint32(0, true);
            const idxBuf = p.getUint32(4, true);
            const keyBuf = p.getUint32(8, true);
            const pc = new DataView(new ArrayBuffer(80));
            const run = (entry: string, k: number, j: number) => {
                pc.setUint32(0, nTris, true);
                pc.setUint32(4, nPad, true);
                pc.setUint32(8, k, true);
                pc.setUint32(12, j, true);
                for (let i = 0; i < 16; i++)
                    pc.setFloat32(16 + i * 4, p.getFloat32(20 + i * 4, true),
                                  true);
                const off = this.uboWrite(new Uint8Array(pc.buffer));
                const cpass = enc.beginComputePass();
                cpass.setPipeline(this.dsPipes.get(entry)!);
                cpass.setBindGroup(0, this.device.createBindGroup({
                    layout: this.dsBgl!, entries: [
                        { binding: 0, resource: { buffer: this.uniformRing,
                                                  offset: off, size: 80 } },
                        { binding: 1, resource: { buffer:
                            this.bufRef(posBuf) } },
                        { binding: 2, resource: { buffer:
                            this.bufRef(idxBuf) } },
                        { binding: 3, resource: { buffer:
                            this.bufRef(keyBuf) } },
                    ]}));
                cpass.dispatchWorkgroups(Math.ceil(nPad / 64));
                cpass.end();
            };
            run('keygen', 0, 0);
            for (let k = 2; k <= nPad; k <<= 1)
                for (let j = k >> 1; j > 0; j >>= 1) run('bitonic', k, j);
            return;
        }
        if (op === Op.Streamlines) {
            // PStreamlines {uBuf, vBuf, seedBuf, outPts, outCnt, w, h,
            //   maxPoints, flags, nSeeds, xMin, xSpan, yMin, ySpan,
            //   stepSize, slot}
            const w = p.getUint32(20, true);
            const h = p.getUint32(24, true);
            const maxPoints = p.getUint32(28, true);
            const nSeeds = p.getUint32(36, true);
            const slot = p.getUint32(60, true);
            if (!nSeeds || !w || !h || !maxPoints) return;
            this.ensureStreamlines();
            const pc = new DataView(new ArrayBuffer(48));
            pc.setUint32(0, w, true);
            pc.setUint32(4, h, true);
            pc.setUint32(8, maxPoints, true);
            pc.setUint32(12, p.getUint32(32, true), true);   // flags
            pc.setFloat32(16, p.getFloat32(40, true), true);  // xMin
            pc.setFloat32(20, p.getFloat32(44, true), true);  // xSpan
            pc.setFloat32(24, p.getFloat32(48, true), true);  // yMin
            pc.setFloat32(28, p.getFloat32(52, true), true);  // ySpan
            pc.setFloat32(32, p.getFloat32(56, true), true);  // stepSize
            pc.setUint32(36, nSeeds, true);
            const off = this.uboWrite(new Uint8Array(pc.buffer));
            const cpass = enc.beginComputePass();
            cpass.setPipeline(this.slPipe!);
            cpass.setBindGroup(0, this.device.createBindGroup({
                layout: this.slBgl!, entries: [
                    { binding: 0, resource: { buffer: this.uniformRing,
                                              offset: off, size: 48 } },
                    ...[0, 4, 8, 12, 16].map((off2, i) =>
                        ({ binding: i + 1, resource: { buffer:
                            this.bufRef(p.getUint32(off2, true)) } })),
                ]}));
            cpass.dispatchWorkgroups(Math.ceil(nSeeds / 64));
            cpass.end();
            // Deliver counts then points as one blob — the host replays
            // the ordered seed accept/reject over the traces.
            const cntBytes = nSeeds * 2 * 4;
            const ptBytes = nSeeds * 2 * maxPoints * 2 * 4;
            const staging = this.device.createBuffer({
                size: cntBytes + ptBytes,
                usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST });
            enc.copyBufferToBuffer(this.bufRef(p.getUint32(16, true)), 0,
                                   staging, 0, cntBytes);
            enc.copyBufferToBuffer(this.bufRef(p.getUint32(12, true)), 0,
                                   staging, cntBytes, ptBytes);
            this.pendingBulk.push({ buf: staging, slot,
                                    bytes: cntBytes + ptBytes });
            return;
        }
        if (op === Op.XCorr) {
            // PXCorr {xBuf, yBuf, outBuf, n, maxLag, flags, invNorm,
            //         mailbox}
            const n = p.getUint32(12, true);
            const maxLag = p.getUint32(16, true);
            if (!n || !maxLag) return;
            this.ensureXcorr();
            const pc = new DataView(new ArrayBuffer(16));
            pc.setUint32(0, n, true);
            pc.setUint32(4, maxLag, true);
            pc.setUint32(8, p.getUint32(20, true), true);   // flags
            pc.setFloat32(12, p.getFloat32(24, true), true); // invNorm
            const off = this.uboWrite(new Uint8Array(pc.buffer));
            const cpass = enc.beginComputePass();
            cpass.setPipeline(this.xcPipe!);
            cpass.setBindGroup(0, this.device.createBindGroup({
                layout: this.xcBgl!, entries: [
                    { binding: 0, resource: { buffer: this.uniformRing,
                                              offset: off, size: 16 } },
                    { binding: 1, resource: { buffer:
                        this.bufRef(p.getUint32(0, true)) } },
                    { binding: 2, resource: { buffer:
                        this.bufRef(p.getUint32(4, true)) } },
                    { binding: 3, resource: { buffer:
                        this.bufRef(p.getUint32(8, true)) } },
                ]}));
            cpass.dispatchWorkgroups(Math.ceil((maxLag * 2 + 1) / 64));
            cpass.end();
            const bytes = (maxLag * 2 + 1) * 4;
            const staging = this.device.createBuffer({
                size: bytes,
                usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST });
            enc.copyBufferToBuffer(this.bufRef(p.getUint32(8, true)), 0,
                                   staging, 0, bytes);
            this.pendingBulk.push({ buf: staging,
                slot: p.getUint32(28, true), bytes });
            return;
        }
        if (op === Op.SortFloats) {
            // PSortFloats {buf, nReal, nPad, mailbox}
            const buf = p.getUint32(0, true);
            const nReal = p.getUint32(4, true);
            const nPad = p.getUint32(8, true);
            if (!nPad) return;
            this.ensureSortFloats();
            const pc = new DataView(new ArrayBuffer(16));
            pc.setUint32(0, nPad, true);
            const run = (k: number, j: number) => {
                pc.setUint32(4, k, true);
                pc.setUint32(8, j, true);
                const off = this.uboWrite(new Uint8Array(pc.buffer));
                const cpass = enc.beginComputePass();
                cpass.setPipeline(this.sfPipe!);
                cpass.setBindGroup(0, this.device.createBindGroup({
                    layout: this.sfBgl!, entries: [
                        { binding: 0, resource: { buffer: this.uniformRing,
                                                  offset: off, size: 16 } },
                        { binding: 1, resource: { buffer:
                            this.bufRef(buf) } },
                    ]}));
                cpass.dispatchWorkgroups(Math.ceil(nPad / 64));
                cpass.end();
            };
            for (let k = 2; k <= nPad; k <<= 1)
                for (let j = k >> 1; j > 0; j >>= 1) run(k, j);
            const bytes = nReal * 4;
            const staging = this.device.createBuffer({
                size: bytes,
                usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST });
            enc.copyBufferToBuffer(this.bufRef(buf), 0, staging, 0, bytes);
            this.pendingBulk.push({ buf: staging,
                slot: p.getUint32(12, true), bytes });
            return;
        }
        if (op === Op.TripcolorTess) {
            // PTripcolorTess {xyBuf, trisBuf, zBuf, lutBuf, outBuf,
            //   counterBuf, nTris, mode, maxVerts, pad, bx,ax,by,ay}
            const nTris = p.getUint32(24, true);
            if (!nTris) return;
            this.ensureTripcolor();
            const pc = new DataView(new ArrayBuffer(32));
            pc.setUint32(0, nTris, true);
            pc.setUint32(4, p.getUint32(28, true), true);   // mode
            pc.setUint32(8, p.getUint32(32, true), true);   // maxVerts
            pc.setFloat32(16, p.getFloat32(40, true), true); // bx
            pc.setFloat32(20, p.getFloat32(44, true), true); // ax
            pc.setFloat32(24, p.getFloat32(48, true), true); // by
            pc.setFloat32(28, p.getFloat32(52, true), true); // ay
            const off = this.uboWrite(new Uint8Array(pc.buffer));
            const cpass = enc.beginComputePass();
            cpass.setPipeline(this.tcPipe!);
            cpass.setBindGroup(0, this.device.createBindGroup({
                layout: this.tcBgl!, entries: [
                    { binding: 0, resource: { buffer: this.uniformRing,
                                              offset: off, size: 32 } },
                    ...[0, 4, 8, 12, 16, 20].map((off2, i) =>
                        ({ binding: i + 1, resource: { buffer:
                            this.bufRef(p.getUint32(off2, true)) } })),
                ]}));
            cpass.dispatchWorkgroups(Math.ceil(nTris / 64));
            cpass.end();
            return;
        }
        if (op === Op.TransformPoints) {
            // PTransformPoints {dataBuf, outBuf, n, flags, ubo(128B)}
            const n = p.getUint32(8, true);
            if (!n) return;
            this.ensureXform();
            // Uniform block = the 128 B TransformUBO plus the trailing
            // point count (padded to the 16-byte uniform stride).
            const ub = new Uint8Array(144);
            ub.set(new Uint8Array(p.buffer, p.byteOffset + 16, 128), 0);
            new DataView(ub.buffer).setUint32(128, n, true);
            const off = this.uboWrite(ub);
            const cpass = enc.beginComputePass();
            cpass.setPipeline(this.xfPipe!);
            cpass.setBindGroup(0, this.device.createBindGroup({
                layout: this.xfBgl!, entries: [
                    { binding: 0, resource: { buffer: this.uniformRing,
                                              offset: off, size: 144 } },
                    { binding: 1, resource: { buffer:
                        this.bufRef(p.getUint32(0, true)) } },
                    { binding: 2, resource: { buffer:
                        this.bufRef(p.getUint32(4, true)) } },
                ]}));
            cpass.dispatchWorkgroups(Math.ceil(n / 64));
            cpass.end();
            return;
        }
        if (op === Op.BarbsTess) {
            // PBarbsTess {xBuf, yBuf, uBuf, vBuf, offBuf, outBuf, n,
            //   flags, totalVerts, length, ubo(128B)}
            const n = p.getUint32(24, true);
            if (!n) return;
            this.ensureBarbs();
            const ub = new Uint8Array(144);
            ub.set(new Uint8Array(p.buffer, p.byteOffset + 40, 128), 0);
            const dv = new DataView(ub.buffer);
            dv.setUint32(128, n, true);
            dv.setUint32(132, p.getUint32(28, true), true);   // flags
            dv.setUint32(136, p.getUint32(32, true), true);   // totalVerts
            dv.setFloat32(140, p.getFloat32(36, true), true); // length
            const off = this.uboWrite(ub);
            const cpass = enc.beginComputePass();
            cpass.setPipeline(this.barbsPipe!);
            cpass.setBindGroup(0, this.device.createBindGroup({
                layout: this.barbsBgl!, entries: [
                    { binding: 0, resource: { buffer: this.uniformRing,
                                              offset: off, size: 144 } },
                    ...[0, 4, 8, 12, 16, 20].map((o, i) =>
                        ({ binding: i + 1, resource: { buffer:
                            this.bufRef(p.getUint32(o, true)) } })),
                ]}));
            cpass.dispatchWorkgroups(Math.ceil(n / 64));
            cpass.end();
            return;
        }
        if (op === Op.PolyFillMask) {
            // PPolyFillMask {ringPts, ringOffs, deltaBuf, maskBuf, tex,
            //   nPts, nRings, W, H, rowStride, rgba}
            const nPts = p.getUint32(20, true);
            const W = p.getUint32(28, true);
            const H = p.getUint32(32, true);
            const rowStride = p.getUint32(36, true);
            if (!nPts || !W || !H || !rowStride) return;
            this.ensurePolyFill('edges');
            this.ensurePolyFill('scan');
            const pc = new DataView(new ArrayBuffer(48));
            pc.setUint32(0, W, true);
            pc.setUint32(4, H, true);
            pc.setUint32(8, nPts, true);
            pc.setUint32(12, p.getUint32(24, true), true);   // nRings
            pc.setFloat32(16, p.getFloat32(40, true), true); // r
            pc.setFloat32(20, p.getFloat32(44, true), true); // g
            pc.setFloat32(24, p.getFloat32(48, true), true); // b
            pc.setFloat32(28, p.getFloat32(52, true), true); // a
            pc.setUint32(32, rowStride, true);
            const off = this.uboWrite(new Uint8Array(pc.buffer));
            const bind = (entry: string, count: number) => {
                const cpass = enc.beginComputePass();
                cpass.setPipeline(this.pfPipes.get(entry)!);
                cpass.setBindGroup(0, this.device.createBindGroup({
                    layout: this.pfBgl!, entries: [
                        { binding: 0, resource: { buffer: this.uniformRing,
                                                  offset: off, size: 48 } },
                        ...[0, 4, 8, 12].map((o, i) =>
                            ({ binding: i + 1, resource: { buffer:
                                this.bufRef(p.getUint32(o, true)) } })),
                    ]}));
                cpass.dispatchWorkgroups(Math.ceil(count / 64));
                cpass.end();
            };
            bind('edges', nPts);
            bind('scan', H);
            // Blit the row-padded rgba8 coverage into the image texture.
            const e2 = this.textures.get(p.getUint32(16, true));
            if (e2) enc.copyBufferToTexture(
                { buffer: this.bufRef(p.getUint32(12, true)),
                  bytesPerRow: rowStride, rowsPerImage: H },
                { texture: e2.tex },
                { width: W, height: H, depthOrArrayLayers: 1 });
            return;
        }
        if (op === Op.FillBetweenTess) {
            // PFillBetweenTess {xBuf, y1Buf, y2Buf, maskBuf, outBuf,
            //   counterBuf, n, flags, maxVerts, bx, ax, by, ay, rgba}
            const n = p.getUint32(24, true);
            if (n < 2) return;
            this.ensureFillBetween();
            const pc = new DataView(new ArrayBuffer(48));
            pc.setUint32(0, n, true);
            pc.setUint32(4, p.getUint32(28, true), true);    // flags
            pc.setUint32(8, p.getUint32(32, true), true);    // maxVerts
            pc.setFloat32(16, p.getFloat32(36, true), true);  // bx
            pc.setFloat32(20, p.getFloat32(40, true), true);  // ax
            pc.setFloat32(24, p.getFloat32(44, true), true);  // by
            pc.setFloat32(28, p.getFloat32(48, true), true);  // ay
            for (let i = 0; i < 4; i++)
                pc.setFloat32(32 + i * 4, p.getFloat32(52 + i * 4, true),
                              true);
            const off = this.uboWrite(new Uint8Array(pc.buffer));
            const cpass = enc.beginComputePass();
            cpass.setPipeline(this.fbPipe!);
            cpass.setBindGroup(0, this.device.createBindGroup({
                layout: this.fbBgl!, entries: [
                    { binding: 0, resource: { buffer: this.uniformRing,
                                              offset: off, size: 48 } },
                    ...[0, 4, 8, 12, 16, 20].map((off2, i) =>
                        ({ binding: i + 1, resource: { buffer:
                            this.bufRef(p.getUint32(off2, true)) } })),
                ]}));
            cpass.dispatchWorkgroups(Math.ceil((n - 1) / 64));
            cpass.end();
            return;
        }
        if (op === Op.QuiverTess) {
            // PQuiverTess {segBuf, outBuf, counterBuf, n, mode,
            //   maxVerts, hw2, hl, hal, pad, r, g, b, a}
            const n = p.getUint32(12, true);
            if (!n) return;
            this.ensureQuiver();
            const pc = new DataView(new ArrayBuffer(48));
            pc.setUint32(0, n, true);
            pc.setUint32(4, p.getUint32(16, true), true);   // mode
            pc.setUint32(8, p.getUint32(20, true), true);   // maxVerts
            pc.setFloat32(16, p.getFloat32(24, true), true); // hw2
            pc.setFloat32(20, p.getFloat32(28, true), true); // hl
            pc.setFloat32(24, p.getFloat32(32, true), true); // hal
            for (let i = 0; i < 4; i++)
                pc.setFloat32(32 + i * 4, p.getFloat32(40 + i * 4, true),
                              true);
            const off = this.uboWrite(new Uint8Array(pc.buffer));
            const cpass = enc.beginComputePass();
            cpass.setPipeline(this.quiverPipe!);
            cpass.setBindGroup(0, this.device.createBindGroup({
                layout: this.quiverBgl!, entries: [
                    { binding: 0, resource: { buffer: this.uniformRing,
                                              offset: off, size: 48 } },
                    ...[0, 4, 8].map((off2, i) =>
                        ({ binding: i + 1, resource: { buffer:
                            this.bufRef(p.getUint32(off2, true)) } })),
                ]}));
            cpass.dispatchWorkgroups(Math.ceil(n / 64));
            cpass.end();
            return;
        }
        if (op !== Op.TessLines) return;
        this.ensureTess();
        // PTessLines {inBuf,inBase,outBuf,outBase,n,nSeg,hwidth,
        //             join u8, cap u8, miterLimit, r,g,b,a}
        // PTessLines is packed: dash fields follow the colour at 50/54/
        // 58/62 and the offset at 66.
        const pc = new DataView(new ArrayBuffer(80));
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
        // Dash fields (offsets 50–78) were added after the original
        // 50-byte payload — replayed frames may stop at the colour.
        const hasDash = p.byteLength >= 80;
        pc.setUint32(48, hasDash ? p.getUint32(62, true) : 1, true); // dashMul
        pc.setUint32(52, hasDash ? p.getUint32(58, true) : 0, true); // dashCount
        // lenBase/dashBase are *array offsets* inside their buffers —
        // each buffer carries exactly this stroke's data, so both are 0.
        // (The buffer handles live in the payload, not the uniform.)
        pc.setUint32(56, 0, true);                       // lenBase
        pc.setUint32(60, 0, true);                       // dashBase
        pc.setFloat32(64, hasDash ? p.getFloat32(66, true) : 0,
                      true);                                 // dashOffset
        const off = this.uboWrite(new Uint8Array(pc.buffer));
        const n = p.getUint32(16, true), nSeg = p.getUint32(20, true);
        if (!n || !nSeg) return;
        const pass = enc.beginComputePass();
        pass.setPipeline(this.tessPipe!);
        const entries: GPUBindGroupEntry[] = [
            { binding: 0, resource: { buffer: this.uniformRing,
                                      offset: off, size: 80 } },
            { binding: 1, resource: { buffer:
                this.bufRef(p.getUint32(0, true)) } },
            { binding: 2, resource: { buffer:
                this.bufRef(p.getUint32(8, true)) } },
        ];
        // Dash bindings are optional: a solid stroke never touches them,
        // but the layout always declares them, so bind a dummy when the
        // stroke carries no pattern.
        const lenH = hasDash ? p.getUint32(50, true) : 0,
              dashH = hasDash ? p.getUint32(54, true) : 0;
        const dummy = this.dummyStorage!;
        entries.push({ binding: 3, resource: { buffer: lenH
            ? this.bufRef(lenH) : dummy } });
        entries.push({ binding: 4, resource: { buffer: dashH
            ? this.bufRef(dashH) : dummy } });
        pass.setBindGroup(0, this.device.createBindGroup({
            layout: this.tessBgl!, entries }));
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
            // PxUBO: vec2f canvasWH @0, vec4f color @16 (vec4 alignment).
            const ubo = new Float32Array(8);
            ubo.set(canvasWH, 0);
            ubo.set([p.getFloat32(40, true), p.getFloat32(44, true),
                     p.getFloat32(48, true), p.getFloat32(52, true)], 4);
            const off = this.uboWrite(ubo);
            pass.setPipeline(this.activePipes.get('px.tris')!);
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
            pass.setPipeline(this.activePipes.get('px.trisvc')!);
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
            const ubo = new Float32Array(8);
            ubo.set(canvasWH, 0);
            ubo.set([p.getFloat32(40, true), p.getFloat32(44, true),
                     p.getFloat32(48, true), p.getFloat32(52, true)], 4);
            const off = this.uboWrite(ubo);
            const key = op === Op.DrawLineStripPx ? 'px.lineStrip'
                                                  : 'px.segs';
            pass.setPipeline(this.activePipes.get(key)!);
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
            pass.setPipeline(this.activePipes.get('text')!);
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
            pass.setPipeline(this.activePipes.get('instanced')!);
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
            pass.setPipeline(this.activePipes.get(key)!);
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
            pass.setPipeline(this.activePipes.get(key)!);
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
            // ubo.rect (payload +32) is the axes pixel rect — NDC must
            // map into it, not the full canvas (DrawPie computes its own
            // pixel affine and must NOT take this branch).
            this.viewport(pass, p, 32);
            const off = this.uboWrite(new Uint8Array<ArrayBuffer>(
                p.buffer, p.byteOffset + 16, XFORM_BYTES));
            const colBuf = p.getUint32(148, true);
            const key = colBuf ? 'trisData' : 'trisData.flat';
            pass.setPipeline(this.activePipes.get(key)!);
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
            // viewport state persists — restore full-canvas for the
            // pixel-space ops that follow.
            pass.setViewport(0, 0, canvasWH[0], canvasWH[1], 0, 1);
            break;
        }
        case Op.DrawTrisGpu: {    // {clip, resW, resH, buf, byteOff, n}
            this.scissor(pass, p);
            // The soup shader maps px→NDC itself, so it needs the
            // identity viewport — the preceding line draw leaves the
            // axes-rect viewport set, and inheriting it would scale and
            // shift every draw that follows in the pass.
            pass.setViewport(0, 0, canvasWH[0], canvasWH[1], 0, 1);
            const ubo = new Float32Array(4);
            ubo[0] = p.getFloat32(16, true);      // resW
            ubo[1] = p.getFloat32(20, true);      // resH
            const dv = new DataView(ubo.buffer);
            dv.setUint32(8, p.getUint32(28, true), true);  // byteOff lo
            const off = this.uboWrite(ubo);
            pass.setPipeline(this.activePipes.get('trisGpu')!);
            pass.setBindGroup(0, this.bindGroup('trisGpu', [
                { binding: 0, resource: { buffer: this.uniformRing,
                                          offset: off, size: 16 } },
                { binding: 1, resource: { buffer:
                    this.bufRef(p.getUint32(24, true)) } },
            ]));
            const countBuf = p.byteLength >= 44 ? p.getUint32(40, true) : 0;
            if (countBuf) pass.drawIndirect(this.bufRef(countBuf), 0);
            else pass.draw(p.getUint32(36, true));
            break;
        }
        case Op.DrawPie: {        // PDrawTrisData + rect={cx,cy,sc,sc}
            this.scissor(pass, p);
            const uboBytes = new Uint8Array<ArrayBuffer>(
                p.buffer, p.byteOffset + 16, XFORM_BYTES).slice();
            // extra2 = canvasWH for the MODE_PIE shader
            // TransformUBO extra2 (vec4f) starts at byte 112.
            new DataView(uboBytes.buffer).setFloat32(112, canvasWH[0], true);
            new DataView(uboBytes.buffer).setFloat32(116, canvasWH[1], true);
            const off = this.uboWrite(uboBytes);
            pass.setPipeline(this.activePipes.get('pie')!);
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
            pass.setPipeline(this.activePipes.get('image')!);
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
        case Op.DrawSurface: {  // PDrawSurface: clip@0, ubo@16(128B), bufs@128
            const cnt = p.getUint32(136, true);
            if (!cnt) break;
            this.scissor(pass, p); this.viewport(pass, p, 0);
            const ubo = new Uint8Array(
                p.buffer, p.byteOffset + 16, 128).slice();
            // gridW/gridH (@140/144) → SurfUBO gridDim (bytes 96..104);
            // non-zero marks the vertex-pull grid mode.
            new DataView(ubo.buffer).setUint32(104, p.getUint32(140, true),
                                               true);
            new DataView(ubo.buffer).setUint32(108,
                                               p.getUint32(144, true),
                                               true);
            const off = this.uboWrite(ubo);
            const pull = p.getUint32(140, true) !== 0;
            const key = pull ? 'surface.pull' : 'surface';
            pass.setPipeline(this.activePipes.get(key)!);
            const surfEntries: GPUBindGroupEntry[] = [
                { binding: 0, resource: { buffer: this.uniformRing,
                                          offset: off, size: 128 } },
                { binding: 1, resource: { buffer:
                    this.bufRef(p.getUint32(128, true)) } },
            ];
            if (!pull)
                surfEntries.push({ binding: 2, resource: { buffer:
                    this.bufRef(p.getUint32(132, true)) } });
            pass.setBindGroup(0, this.bindGroup(key, surfEntries));
            pass.draw(cnt);
            break;
        }
        case Op.DrawGrid3D: {   // PDrawGrid3D: clip@0, pc[44]@16 (176B)
            this.scissor(pass, p); this.viewport(pass, p, 0);
            const off = this.uboWrite(new Uint8Array(
                p.buffer, p.byteOffset + 16, 176));
            pass.setPipeline(this.activePipes.get('grid3d')!);
            pass.setBindGroup(0, this.bindGroup('grid3d', [
                { binding: 0, resource: { buffer: this.uniformRing,
                                          offset: off, size: 176 } },
            ]));
            pass.draw(3);   // fullscreen triangle
            break;
        }
        case Op.DrawSegs3D:
        case Op.DrawTris3D:
        case Op.DrawPoints3D: {  // PDraw3D — see OpPayloads.hpp
            const count = p.getUint32(108, true);
            if (!count) break;
            this.scissor(pass, p);
            this.viewport(pass, p, 16);          // view = axes px rect
            // ProjU @ubo 0..127: vp @32, rect=view @16, rgba @112,
            // width @128 → misc.x, marker @136.
            const ubo = new Uint8Array(128);
            ubo.set(new Uint8Array(p.buffer, p.byteOffset + 32, 64), 0);
            ubo.set(new Uint8Array(p.buffer, p.byteOffset + 16, 16), 64);
            ubo.set(new Uint8Array(p.buffer, p.byteOffset + 112, 16), 80);
            new DataView(ubo.buffer)
                .setFloat32(96, p.getFloat32(128, true), true);
            ubo.set(new Uint8Array(p.buffer, p.byteOffset + 136, 16),
                    112);
            const off = this.uboWrite(ubo);
            const posBuf = p.getUint32(96, true);
            const colBuf = p.getUint32(100, true);
            const auxBuf = p.getUint32(104, true);
            const flags  = p.getUint32(132, true);
            let key: string, drawVerts = count, instances = 1;
            const entries: GPUBindGroupEntry[] = [
                { binding: 0, resource: { buffer: this.uniformRing,
                                          offset: off, size: 128 } },
                { binding: 1, resource: { buffer:
                    this.bufRef(posBuf) } }];
            if (op === Op.DrawSegs3D) key = 'segs3d';
            else if (op === Op.DrawTris3D) {
                // auxBuf carries the DepthSort index buffer when present.
                const sorted = auxBuf !== 0;
                key = sorted ? (colBuf ? 'tris3d.ci' : 'tris3d.i')
                             : (colBuf ? 'tris3d.c' : 'tris3d');
            } else {
                key = 'points3d' + (flags & 1 ? '.c' : '') +
                                 (flags & 2 ? '.s' : '');
                drawVerts = 6; instances = count;
            }
            if (op === Op.DrawTris3D && colBuf)
                entries.push({ binding: 2, resource: { buffer:
                    this.bufRef(colBuf) } });
            if (op === Op.DrawTris3D && auxBuf)
                entries.push({ binding: 4, resource: { buffer:
                    this.bufRef(auxBuf) } });
            if (op === Op.DrawPoints3D) {
                if (flags & 1)
                    entries.push({ binding: 2, resource: { buffer:
                        this.bufRef(colBuf) } });
                if (flags & 2)
                    entries.push({ binding: 3, resource: { buffer:
                        this.bufRef(auxBuf) } });
            }
            pass.setPipeline(this.activePipes.get(key)!);
            pass.setBindGroup(0, this.bindGroup(key, entries));
            pass.draw(drawVerts, instances);
            // viewport persists — restore full-canvas for px ops.
            pass.setViewport(0, 0, canvasWH[0], canvasWH[1], 0, 1);
            break;
        }
        case Op.DrawBoxes3D: {   // PDraw3D — instBuf=posBuf, count=insts
            const count = p.getUint32(108, true);
            if (!count) break;
            this.scissor(pass, p);
            this.viewport(pass, p, 16);
            // ProjU: vp @0, rect @64, rgba @80, misc @96, marker @112.
            const ubo = new Uint8Array(128);
            ubo.set(new Uint8Array(p.buffer, p.byteOffset + 32, 64), 0);
            ubo.set(new Uint8Array(p.buffer, p.byteOffset + 16, 16), 64);
            ubo.set(new Uint8Array(p.buffer, p.byteOffset + 112, 16), 80);
            new DataView(ubo.buffer)
                .setFloat32(96, p.getFloat32(128, true), true);
            ubo.set(new Uint8Array(p.buffer, p.byteOffset + 136, 16),
                    112);
            const off = this.uboWrite(ubo);
            const key = 'boxes3d';
            pass.setPipeline(this.activePipes.get(key)!);
            pass.setBindGroup(0, this.bindGroup(key, [
                { binding: 0, resource: { buffer: this.uniformRing,
                                          offset: off, size: 128 } },
                { binding: 1, resource: { buffer:
                    this.bufRef(p.getUint32(96, true)) } },
            ]));
            pass.draw(36, count);
            pass.setViewport(0, 0, canvasWH[0], canvasWH[1], 0, 1);
            break;
        }
        default: break;
        }
    }
}
