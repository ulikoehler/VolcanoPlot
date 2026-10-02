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
    _vp_mailbox(slot: number, v0: number, v1: number,
                v2: number, v3: number): void;
    HEAPU8: Uint8Array<ArrayBuffer>;
}
type ModuleFactory = (opts?: unknown) => Promise<VolcanoModule>;

export class VolcanoCanvas {
    private interp: Interpreter;
    private devPixelRatio = 1;

    constructor(
        private mod: VolcanoModule,
        private canvas: HTMLCanvasElement,
        private device: GPUDevice,
        private gpuCtx: GPUCanvasContext,
    ) {
        this.interp = new Interpreter(
            device, gpuCtx, navigator.gpu.getPreferredCanvasFormat());
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
    const mod = await moduleFactory();
    return new VolcanoCanvas(mod, canvas, device, gpuCtx);
}
