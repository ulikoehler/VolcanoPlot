// web/src/volcano.ts — public API for @volcanoplot/web
//
//   import { createCanvas } from '@volcanoplot/web';
//   const vp = await createCanvas(document.querySelector('canvas')!);
//   vp.plot(xs, ys, { color: 'tab:blue' });
//   vp.renderIfStale();              // call from rAF or on data change

import { Interpreter } from './interpreter';

/// Emscripten module factory type (produced by --bind MODULARIZE build).
interface VolcanoModule {
    _vp_framePtr(): number;
    _vp_frameLen(): number;
    _vp_resize(w: number, h: number): void;
    _vp_render(): boolean;
    _vp_renderIfStale(): boolean;
    _vp_line(axes: number, xs: Float32Array, ys: Float32Array,
             color: string): number;
    _vp_scatter(axes: number, xs: Float32Array, ys: Float32Array,
                color: string): number;
    _vp_alloc(nbytes: number): number;
    _vp_free(ptr: number): void;
    _vp_mailbox(slot: number, v0: number, v1: number,
                v2: number, v3: number): void;
    _vp_setData(handle: number, xs: Float32Array,
                ys: Float32Array): void;
    HEAPU8: Uint8Array<ArrayBuffer>;
}
type ModuleFactory = (opts?: unknown) => Promise<VolcanoModule>;

export class VolcanoCanvas {
    private interp: Interpreter;
    private devPixelRatio = 1;
    // Retain the adapter: in Dawn's wire client, GC'ing the GPUAdapter
    // can destroy the device ("external Instance reference no longer
    // exists" on later mapAsync).
    private adapter: GPUAdapter | null = null;

    constructor(
        private mod: VolcanoModule,
        private canvas: HTMLCanvasElement,
        private device: GPUDevice,
        private gpuCtx: GPUCanvasContext,
        adapter?: GPUAdapter,
    ) {
        this.adapter = adapter ?? null;
        this.interp = new Interpreter(
            device, gpuCtx, navigator.gpu.getPreferredCanvasFormat(),
            (slot, v) => mod._vp_mailbox(slot, v[0], v[1], v[2], v[3]));
        this.interp.init();
        this.syncSize();
    }

    /** Follow DPR — call on resize/orientation change. */
    syncSize() {
        this.devPixelRatio = globalThis.devicePixelRatio || 1;
        const w = Math.max(1, Math.round(
            this.canvas.clientWidth * this.devPixelRatio));
        const h = Math.max(1, Math.round(
            this.canvas.clientHeight * this.devPixelRatio));
        if (this.canvas.width !== w || this.canvas.height !== h) {
            this.canvas.width = w; this.canvas.height = h;
        }
        this.mod._vp_resize(w, h);
    }

    /** Render if the figure is stale; replays the op stream. */
    renderIfStale(): boolean {
        if (!this.mod._vp_renderIfStale()) return false;
        this.replay();
        return true;
    }

    render() { this.mod._vp_render(); this.replay(); }

    /** Render to an offscreen texture and read back RGBA8 pixels —
     * test/debug path; does not touch the canvas. */
    async capture(): Promise<Uint8Array<ArrayBuffer>> {
        this.mod._vp_render();
        const ptr = this.mod._vp_framePtr(), len = this.mod._vp_frameLen();
        const frame = this.mod.HEAPU8.subarray(ptr, ptr + len).slice();
        return this.interp.capture(frame, this.canvas.width,
                                   this.canvas.height);
    }

    /** Stage JS data into the WASM heap (zero-copy for C++). */
    private stage(data: ArrayLike<number>): Float32Array {
        const ptr = this.mod._vp_alloc(data.length * 4);
        const view = new Float32Array(
            this.mod.HEAPU8.buffer, ptr, data.length);
        view.set(data);
        return view;   // caller must _vp_free(view.byteOffset)
    }

    line(xs: ArrayLike<number>, ys: ArrayLike<number>,
         color = ''): number {
        const vx = this.stage(xs), vy = this.stage(ys);
        try { return this.mod._vp_line(0, vx, vy, color); }
        finally {
            this.mod._vp_free(vx.byteOffset);
            this.mod._vp_free(vy.byteOffset);
        }
    }

    scatter(xs: ArrayLike<number>, ys: ArrayLike<number>,
            color = ''): number {
        const vx = this.stage(xs), vy = this.stage(ys);
        try { return this.mod._vp_scatter(0, vx, vy, color); }
        finally {
            this.mod._vp_free(vx.byteOffset);
            this.mod._vp_free(vy.byteOffset);
        }
    }

    /** In-place update for a handle from line()/scatter() — the
     * ring-buffer/streaming path; call renderIfStale() after updating. */
    setData(handle: number, xs: ArrayLike<number>,
            ys: ArrayLike<number>) {
        const vx = this.stage(xs), vy = this.stage(ys);
        try { this.mod._vp_setData(handle, vx, vy); }
        finally {
            this.mod._vp_free(vx.byteOffset);
            this.mod._vp_free(vy.byteOffset);
        }
    }

    private replay() {
        const ptr = this.mod._vp_framePtr();
        const len = this.mod._vp_frameLen();
        if (!ptr || !len) return;
        // Frame bytes live in WASM memory — view, don't copy.
        const frame = this.mod.HEAPU8.subarray(ptr, ptr + len);
        this.interp.draw(frame);
    }
}

export async function createCanvas(
    canvas: HTMLCanvasElement,
    moduleFactory: ModuleFactory,
): Promise<VolcanoCanvas> {
    if (!navigator.gpu) throw new Error('WebGPU unavailable');
    const adapter = await navigator.gpu.requestAdapter();
    if (!adapter) throw new Error('no WebGPU adapter');
    const device = await adapter.requestDevice();
    const gpuCtx = canvas.getContext('webgpu');
    if (!gpuCtx) throw new Error('no webgpu canvas context');
    gpuCtx.configure({
        device, format: navigator.gpu.getPreferredCanvasFormat(),
        alphaMode: 'opaque',
    });
    // Emscripten resolves side files (.wasm/.data) against the page URL;
    // point it at the module's own directory instead.
    const mod = await moduleFactory({
        locateFile: (p: string) =>
            new URL(p, import.meta.resolve('./volcanoplot.js')).href,
    });
    return new VolcanoCanvas(mod, canvas, device, gpuCtx, adapter);
}
