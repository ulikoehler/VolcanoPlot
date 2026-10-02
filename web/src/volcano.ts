// web/src/volcano.ts — public API for @volcanoplot/web
//
//   import { createCanvas } from '@volcanoplot/web';
//   const vp = await createCanvas(document.querySelector('canvas')!);
//   vp.plot(xs, ys, { color: 'tab:blue' });
//   vp.renderIfStale();              // call from rAF or on data change

import { Interpreter } from './interpreter';
import { Canvas2DInterpreter } from './fallback';
import { SvgExporter } from './svg';

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
    _vp_mailboxDest(slot: number, bytes: number): number;
    _vp_mailboxDone(slot: number): void;
    _vp_mailbox(slot: number, v0: number, v1: number,
                v2: number, v3: number): void;
    _vp_setData(handle: number, xs: Float32Array,
                ys: Float32Array): void;
    _vp_function(axes: number, body: string, xMin: number,
                 xMax: number, color: string): number;
    _vp_bar(axes: number, heights: Float32Array, labels: string[],
            color: string): number;
    _vp_hist(axes: number, samples: Float32Array, bins: number,
             color: string): number;
    _vp_pie(axes: number, values: Float32Array, labels: string[]): number;
    _vp_heatmap(axes: number, values: Float32Array, w: number,
                h: number, cmap: string): number;
    _vp_surface(axes: number, values: Float32Array, w: number,
                h: number, elev: number, azim: number): number;
    _vp_errorbar(axes: number, xs: Float32Array, ys: Float32Array,
                 yerr: Float32Array, color: string): number;
    _vp_stem(axes: number, xs: Float32Array,
             ys: Float32Array): number;
    _vp_step(axes: number, xs: Float32Array, ys: Float32Array,
             where: string): number;
    _vp_ecdf(axes: number, samples: Float32Array): number;
    _vp_fillBetween(axes: number, xs: Float32Array, y1: Float32Array,
                    y2: Float32Array, color: string): number;
    _vp_boxplot(axes: number, groups: Float32Array[]): number;
    _vp_hist2d(axes: number, xs: Float32Array, ys: Float32Array,
               bins: number, cmap: string): number;
    _vp_hexbin(axes: number, xs: Float32Array,
               ys: Float32Array): number;
    _vp_quiver(axes: number, xs: Float32Array, ys: Float32Array,
               us: Float32Array, vs: Float32Array): number;
    _vp_contour(axes: number, values: Float32Array, w: number,
                h: number, levels: number, cmap: string): number;
    _vp_pcolormesh(axes: number, xs: Float32Array, ys: Float32Array,
                   cs: Float32Array, nCols: number,
                   nRows: number): number;
    _vp_kde(axes: number, xs: Float32Array, ys: Float32Array,
            cmap: string): number;
    _vp_violin(axes: number, groups: Float32Array[], width: number,
               showBox: boolean, color: string): number;
    _vp_stackplot(axes: number, xs: Float32Array,
                  ys: Float32Array[]): number;
    _vp_fill(axes: number, xs: Float32Array, ys: Float32Array,
             color: string): number;
    _vp_spy(axes: number, data: Float32Array, nrows: number,
            ncols: number): number;
    _vp_tripcolor(axes: number, xs: Float32Array, ys: Float32Array,
                  zs: Float32Array): number;
    _vp_streamplot(axes: number, us: Float32Array, vs: Float32Array,
                   w: number, h: number): number;
    _vp_subplot(nrows: number, ncols: number, index: number): number;
    _vp_setInteractive(on: boolean): void;
    _vp_dispatch(type: number, x: number, y: number,
                 button: number, step: number): boolean;
    _vp_xlim(a: number, lo: number, hi: number): void;
    _vp_ylim(a: number, lo: number, hi: number): void;
    _vp_xscale(a: number, name: string): void;
    _vp_yscale(a: number, name: string): void;
    _vp_title(a: number, t: string): void;
    _vp_xlabel(a: number, t: string): void;
    _vp_ylabel(a: number, t: string): void;
    _vp_grid(a: number, on: boolean): void;
    _vp_suptitle(t: string): void;
    _vp_matshow(a: number, data: Float32Array, nrows: number,
                ncols: number): number;
    _vp_pcolorfast(a: number, C: Float32Array, nCols: number, nRows: number,
                   x0: number, x1: number, y0: number, y1: number): number;
    _vp_brokenBarh(a: number, segs: Float32Array): number;
    _vp_tricontour(a: number, xs: Float32Array, ys: Float32Array,
                   zs: Float32Array): number;
    _vp_triplot(a: number, xs: Float32Array, ys: Float32Array): number;
    _vp_specgram(a: number, signal: Float32Array, fs: number): number;
    _vp_spectrum(a: number, signal: Float32Array, fs: number): number;
    _vp_psd(a: number, signal: Float32Array, fs: number): number;
    _vp_csd(a: number, xs: Float32Array, ys: Float32Array,
            fs: number): number;
    _vp_xcorr(a: number, xs: Float32Array, ys: Float32Array): number;
    _vp_cohere(a: number, xs: Float32Array, ys: Float32Array,
               fs: number): number;
    _vp_wireframe(a: number, values: Float32Array, w: number, h: number,
                  elev: number, azim: number): number;
    _vp_trisurf(a: number, xs: Float32Array, ys: Float32Array,
                zs: Float32Array, elev: number, azim: number): number;
    _vp_axhline(a: number, y: number, color: string, width: number): void;
    _vp_axvline(a: number, x: number, color: string, width: number): void;
    _vp_axhspan(a: number, y1: number, y2: number, color: string): void;
    _vp_axvspan(a: number, x1: number, x2: number, color: string): void;
    _vp_hlines(a: number, ys: Float32Array, xMin: number, xMax: number,
               color: string, width: number): void;
    _vp_vlines(a: number, xs: Float32Array, yMin: number, yMax: number,
               color: string, width: number): void;
    _vp_legend(a: number, loc: string): void;
    _vp_colorbar(a: number): void;
    _vp_text(a: number, x: number, y: number, txt: string,
             coords: number): void;
    HEAPU8: Uint8Array<ArrayBuffer>;
}
type ModuleFactory = (opts?: unknown) => Promise<VolcanoModule>;

export class VolcanoCanvas {
    private interp: Interpreter | Canvas2DInterpreter;
    private ctx2d?: CanvasRenderingContext2D;
    private devPixelRatio = 1;
    // Retain the adapter: in Dawn's wire client, GC'ing the GPUAdapter
    // can destroy the device ("external Instance reference no longer
    // exists" on later mapAsync).
    private adapter: GPUAdapter | null = null;
    private dead = false;
    /** Called when the WebGPU device is lost. The canvas is then dead:
     * render calls throw until a new VolcanoCanvas is created. */
    onDeviceLost?: (reason: string, message: string) => void;
    /** Undefined on the Canvas2D fallback path. */
    readonly device?: GPUDevice;
    readonly gpuCtx?: GPUCanvasContext;

    constructor(
        private mod: VolcanoModule,
        private canvas: HTMLCanvasElement,
        device?: GPUDevice,
        gpuCtx?: GPUCanvasContext,
        adapter?: GPUAdapter,
    ) {
        this.adapter = adapter ?? null;
        this.device = device;
        this.gpuCtx = gpuCtx;
        const mb = (slot: number,
                    v: [number, number, number, number]) =>
            mod._vp_mailbox(slot, v[0], v[1], v[2], v[3]);
        const mbBytes = (slot: number, bytes: Uint8Array) => {
            const dst = mod._vp_mailboxDest(slot, bytes.length);
            mod.HEAPU8.set(bytes, dst);
            mod._vp_mailboxDone(slot);
        };
        if (device && gpuCtx) {
            const gpu = new Interpreter(
                device, gpuCtx,
                navigator.gpu.getPreferredCanvasFormat(), mb, mbBytes);
            gpu.init();
            this.interp = gpu;
        } else {
            this.ctx2d = canvas.getContext('2d')!;
            this.interp = new Canvas2DInterpreter(mb);
        }
        device?.lost.then(info => {
            if (info.reason === 'destroyed') return; // explicit destroy()
            this.dead = true;
            this.onDeviceLost?.(info.reason, info.message);
        });
        this.syncSize();
    }

    private checkLive() {
        if (this.dead)
            throw new Error('WebGPU device lost — recreate the canvas');
    }

    /** Release the GPU device and mark this canvas unusable. */
    destroy() {
        this.dead = true;
        this.device?.destroy();
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
        this.checkLive();
        if (!this.mod._vp_renderIfStale()) return false;
        this.replay();
        return true;
    }

    render() { this.checkLive(); this.mod._vp_render(); this.replay(); }

    /** Export the last rendered frame as an SVG string — vector
     * geometry ops become <path>/<circle>/<polyline>; heatmaps embed
     * as raster PNGs; tick-label glyph quads are skipped (text is
     * atlas-rasterized upstream). */
    toSvg(): string {
        const ptr = this.mod._vp_framePtr(), len = this.mod._vp_frameLen();
        if (!ptr || !len) return '';
        const frame = this.mod.HEAPU8.subarray(ptr, ptr + len);
        return new SvgExporter().toSvg(frame.slice());
    }

    /** Render to an offscreen target and read back RGBA8 pixels —
     * test/debug path; does not touch the canvas. */
    async capture(): Promise<Uint8Array<ArrayBuffer>> {
        this.checkLive();
        this.mod._vp_render();
        const ptr = this.mod._vp_framePtr(), len = this.mod._vp_frameLen();
        const frame = this.mod.HEAPU8.subarray(ptr, ptr + len).slice();
        if (this.interp instanceof Canvas2DInterpreter) {
            const cv = document.createElement('canvas');
            cv.width = this.canvas.width; cv.height = this.canvas.height;
            const c = cv.getContext('2d')!;
            this.interp.draw(frame, c);
            const d = c.getImageData(0, 0, cv.width, cv.height).data;
            return new Uint8Array(d.buffer.slice(0));
        }
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

    /// Axes index all plot calls target (mpl "current axes").
    private cur = 0;

    /** mpl subplot(nrows, ncols, index) — creates the grid on first
     * use and selects the new axes for subsequent plot calls. */
    subplot(nrows: number, ncols: number, index: number): number {
        return (this.cur = this.mod._vp_subplot(nrows, ncols, index));
    }

    /** Select an existing axes by index (as returned by subplot()). */
    axes(i: number) { this.cur = i; }

    private detachInteraction?: () => void;

    /** mpl matshow — nearest-neighbor matrix display. */
    matshow(data: ArrayLike<number>, nrows: number, ncols: number): number {
        const d = this.stage(data);
        try { return this.mod._vp_matshow(this.cur, d, nrows, ncols); }
        finally { this.mod._vp_free(d.byteOffset); }
    }
    /** mpl pcolorfast on a regular grid with the given extent. */
    pcolorfast(C: ArrayLike<number>, nCols: number, nRows: number,
               x0 = 0, x1 = nCols, y0 = 0, y1 = nRows): number {
        const c = this.stage(C);
        try { return this.mod._vp_pcolorfast(this.cur, c, nCols, nRows,
                                           x0, x1, y0, y1); }
        finally { this.mod._vp_free(c.byteOffset); }
    }
    /** mpl broken_barh — flat [xStart, xWidth, yStart, yHeight]* tuples. */
    brokenBarh(segs: ArrayLike<number>): number {
        const s = this.stage(segs);
        try { return this.mod._vp_brokenBarh(this.cur, s); }
        finally { this.mod._vp_free(s.byteOffset); }
    }
    /** mpl tricontour — Delaunay-triangulates scattered (x,y,z). */
    tricontour(xs: ArrayLike<number>, ys: ArrayLike<number>,
               zs: ArrayLike<number>): number {
        const x = this.stage(xs), y = this.stage(ys), z = this.stage(zs);
        try { return this.mod._vp_tricontour(this.cur, x, y, z); }
        finally { this.mod._vp_free(x.byteOffset); this.mod._vp_free(y.byteOffset); this.mod._vp_free(z.byteOffset); }
    }
    /** mpl triplot — triangle edges + vertex markers. */
    triplot(xs: ArrayLike<number>, ys: ArrayLike<number>): number {
        const x = this.stage(xs), y = this.stage(ys);
        try { return this.mod._vp_triplot(this.cur, x, y); }
        finally { this.mod._vp_free(x.byteOffset); this.mod._vp_free(y.byteOffset); }
    }
    /** mpl specgram — spectrogram of a 1-D signal. */
    specgram(signal: ArrayLike<number>, fs = 2.0): number {
        const s = this.stage(signal);
        try { return this.mod._vp_specgram(this.cur, s, fs); }
        finally { this.mod._vp_free(s.byteOffset); }
    }
    /** mpl spectrum — magnitude spectrum of a 1-D signal. */
    spectrum(signal: ArrayLike<number>, fs = 2.0): number {
        const s = this.stage(signal);
        try { return this.mod._vp_spectrum(this.cur, s, fs); }
        finally { this.mod._vp_free(s.byteOffset); }
    }
    /** mpl psd — power spectral density. */
    psd(signal: ArrayLike<number>, fs = 2.0): number {
        const s = this.stage(signal);
        try { return this.mod._vp_psd(this.cur, s, fs); }
        finally { this.mod._vp_free(s.byteOffset); }
    }
    /** mpl csd — cross power spectral density of two signals. */
    csd(xs: ArrayLike<number>, ys: ArrayLike<number>, fs = 2.0): number {
        const x = this.stage(xs), y = this.stage(ys);
        try { return this.mod._vp_csd(this.cur, x, y, fs); }
        finally { this.mod._vp_free(x.byteOffset); this.mod._vp_free(y.byteOffset); }
    }
    /** mpl xcorr — cross-correlation of two signals. */
    xcorr(xs: ArrayLike<number>, ys: ArrayLike<number>): number {
        const x = this.stage(xs), y = this.stage(ys);
        try { return this.mod._vp_xcorr(this.cur, x, y); }
        finally { this.mod._vp_free(x.byteOffset); this.mod._vp_free(y.byteOffset); }
    }
    /** mpl cohere — coherence of two signals. */
    cohere(xs: ArrayLike<number>, ys: ArrayLike<number>, fs = 2.0): number {
        const x = this.stage(xs), y = this.stage(ys);
        try { return this.mod._vp_cohere(this.cur, x, y, fs); }
        finally { this.mod._vp_free(x.byteOffset); this.mod._vp_free(y.byteOffset); }
    }
    /** mpl wireframe — 3-D wireframe of a row-major height grid. */
    wireframe(values: ArrayLike<number>, w: number, h: number,
              elev = 30, azim = -60): number {
        const v = this.stage(values);
        try { return this.mod._vp_wireframe(this.cur, v, w, h, elev, azim); }
        finally { this.mod._vp_free(v.byteOffset); }
    }
    /** mpl plot_trisurf — triangulated 3-D surface of scattered points. */
    trisurf(xs: ArrayLike<number>, ys: ArrayLike<number>,
            zs: ArrayLike<number>, elev = 30, azim = -60): number {
        const x = this.stage(xs), y = this.stage(ys), z = this.stage(zs);
        try { return this.mod._vp_trisurf(this.cur, x, y, z, elev, azim); }
        finally { this.mod._vp_free(x.byteOffset); this.mod._vp_free(y.byteOffset); this.mod._vp_free(z.byteOffset); }
    }

    // ── mpl reference lines / spans / annotations ───────────────────
    axhline(y: number, color = '#000', width = 1) {
        this.mod._vp_axhline(this.cur, y, color, width);
    }
    axvline(x: number, color = '#000', width = 1) {
        this.mod._vp_axvline(this.cur, x, color, width);
    }
    axhspan(y1: number, y2: number, color = '#c8c8c880') {
        this.mod._vp_axhspan(this.cur, y1, y2, color);
    }
    axvspan(x1: number, x2: number, color = '#c8c8c880') {
        this.mod._vp_axvspan(this.cur, x1, x2, color);
    }
    hlines(ys: ArrayLike<number>, xMin: number, xMax: number,
           color = '#000', width = 1) {
        const v = this.stage(ys);
        try { this.mod._vp_hlines(this.cur, v, xMin, xMax, color, width); }
        finally { this.mod._vp_free(v.byteOffset); }
    }
    vlines(xs: ArrayLike<number>, yMin: number, yMax: number,
           color = '#000', width = 1) {
        const v = this.stage(xs);
        try { this.mod._vp_vlines(this.cur, v, yMin, yMax, color, width); }
        finally { this.mod._vp_free(v.byteOffset); }
    }
    /** mpl ax.legend — loc like "upper right", "best", "lower left". */
    legend(loc = 'best') { this.mod._vp_legend(this.cur, loc); }
    /** mpl fig.colorbar — adds the colorbar strip to the axes. */
    colorbar() { this.mod._vp_colorbar(this.cur); }
    /** mpl ax.text — coords: 'data' | 'axes' | 'figure'. */
    text(x: number, y: number, txt: string,
         coords: 'data'|'axes'|'figure' = 'data') {
        this.mod._vp_text(this.cur, x, y, txt,
            coords === 'data' ? 0 : coords === 'axes' ? 1 : 2);
    }

    /** mpl-style interaction: left-drag pans, scroll zooms about the
     * cursor (scale-aware). Event coordinates are converted to device
     * px; the figure re-renders whenever an event touches it. */
    enableInteraction(on = true) {
        this.mod._vp_setInteractive(on);
        this.detachInteraction?.();
        this.detachInteraction = undefined;
        if (!on) return;
        const cv = this.canvas;
        // Event types must match plot::Event::Type order.
        const PRESS = 0, RELEASE = 1, MOTION = 2, SCROLL = 3;
        const pos = (e: MouseEvent | WheelEvent) => {
            const r = cv.getBoundingClientRect();
            return [
                (e.clientX - r.left) * cv.width / r.width,
                (e.clientY - r.top) * cv.height / r.height,
            ];
        };
        const send = (type: number, x: number, y: number,
                      button = 0, step = 0) => {
            if (this.mod._vp_dispatch(type, x, y, button, step))
                this.renderIfStale();
        };
        const down = (e: MouseEvent) => {
            if (e.button > 2) return;
            const [x, y] = pos(e);
            send(PRESS, x, y, e.button + 1);
        };
        const move = (e: MouseEvent) => {
            const [x, y] = pos(e);
            send(MOTION, x, y);
        };
        const up = (e: MouseEvent) => {
            const [x, y] = pos(e);
            send(RELEASE, x, y, e.button + 1);
        };
        const wheel = (e: WheelEvent) => {
            e.preventDefault();
            const [x, y] = pos(e);
            // mpl step: +1 scroll-up (zoom in), -1 scroll-down.
            send(SCROLL, x, y, 0, e.deltaY < 0 ? 1 : -1);
        };
        cv.addEventListener('mousedown', down);
        cv.addEventListener('mousemove', move);
        window.addEventListener('mouseup', up);
        cv.addEventListener('wheel', wheel, { passive: false });
        cv.addEventListener('contextmenu', e => e.preventDefault());
        this.detachInteraction = () => {
            cv.removeEventListener('mousedown', down);
            cv.removeEventListener('mousemove', move);
            window.removeEventListener('mouseup', up);
            cv.removeEventListener('wheel', wheel);
        };
    }

    // ── mpl axes/figure styling ─────────────────────────────────────
    xlim(lo: number, hi: number) { this.mod._vp_xlim(this.cur, lo, hi); }
    ylim(lo: number, hi: number) { this.mod._vp_ylim(this.cur, lo, hi); }
    /** "linear"|"log"|"symlog"|"logit"|"asinh"|"mercator" */
    xscale(name: string) { this.mod._vp_xscale(this.cur, name); }
    yscale(name: string) { this.mod._vp_yscale(this.cur, name); }
    title(t: string)  { this.mod._vp_title(this.cur, t); }
    xlabel(t: string) { this.mod._vp_xlabel(this.cur, t); }
    ylabel(t: string) { this.mod._vp_ylabel(this.cur, t); }
    grid(on = true)   { this.mod._vp_grid(this.cur, on); }
    /** Figure-level suptitle. */
    suptitle(t: string) { this.mod._vp_suptitle(t); }

    line(xs: ArrayLike<number>, ys: ArrayLike<number>,
         color = ''): number {
        const vx = this.stage(xs), vy = this.stage(ys);
        try { return this.mod._vp_line(this.cur, vx, vy, color); }
        finally {
            this.mod._vp_free(vx.byteOffset);
            this.mod._vp_free(vy.byteOffset);
        }
    }

    scatter(xs: ArrayLike<number>, ys: ArrayLike<number>,
            color = ''): number {
        const vx = this.stage(xs), vy = this.stage(ys);
        try { return this.mod._vp_scatter(this.cur, vx, vy, color); }
        finally {
            this.mod._vp_free(vx.byteOffset);
            this.mod._vp_free(vy.byteOffset);
        }
    }

    /** GPU-evaluated function plot: `body` is a GLSL-ish expression or
     * statements assigning `y` from `x` (e.g. "sin(10.0*x)"). */
    func(body: string, xMin = 0, xMax = 1, color = ''): number {
        return this.mod._vp_function(this.cur, body, xMin, xMax, color);
    }

    bar(heights: ArrayLike<number>, labels: string[] = [],
        color = ''): number {
        const v = this.stage(heights);
        try { return this.mod._vp_bar(this.cur, v, labels, color); }
        finally { this.mod._vp_free(v.byteOffset); }
    }

    hist(samples: ArrayLike<number>, bins = 10, color = ''): number {
        const v = this.stage(samples);
        try { return this.mod._vp_hist(this.cur, v, bins, color); }
        finally { this.mod._vp_free(v.byteOffset); }
    }

    pie(values: ArrayLike<number>, labels: string[] = []): number {
        const v = this.stage(values);
        try { return this.mod._vp_pie(this.cur, v, labels); }
        finally { this.mod._vp_free(v.byteOffset); }
    }

    /** Row-major scalar grid rendered through a colormap. */
    heatmap(values: ArrayLike<number>, w: number, h: number,
            cmap = 'viridis'): number {
        const v = this.stage(values);
        try { return this.mod._vp_heatmap(this.cur, v, w, h, cmap); }
        finally { this.mod._vp_free(v.byteOffset); }
    }

    /** 3D surface from a row-major height grid. Camera uses mpl
     * viewInit angles (elev=30, azim=-60 by default). */
    surface(values: ArrayLike<number>, w: number, h: number,
            elev = 30, azim = -60): number {
        const v = this.stage(values);
        try { return this.mod._vp_surface(this.cur, v, w, h, elev, azim); }
        finally { this.mod._vp_free(v.byteOffset); }
    }

    errorbar(xs: ArrayLike<number>, ys: ArrayLike<number>,
             yerr: ArrayLike<number>, color = ''): number {
        const a = this.stage(xs), b = this.stage(ys),
              e = this.stage(yerr);
        try { return this.mod._vp_errorbar(this.cur, a, b, e, color); }
        finally { for (const v of [a, b, e])
                      this.mod._vp_free(v.byteOffset); }
    }

    stem(xs: ArrayLike<number>, ys: ArrayLike<number>): number {
        const a = this.stage(xs), b = this.stage(ys);
        try { return this.mod._vp_stem(this.cur, a, b); }
        finally { this.mod._vp_free(a.byteOffset);
                  this.mod._vp_free(b.byteOffset); }
    }

    step(xs: ArrayLike<number>, ys: ArrayLike<number>,
         where: 'pre' | 'post' | 'mid' = 'pre'): number {
        const a = this.stage(xs), b = this.stage(ys);
        try { return this.mod._vp_step(this.cur, a, b, where); }
        finally { this.mod._vp_free(a.byteOffset);
                  this.mod._vp_free(b.byteOffset); }
    }

    ecdf(samples: ArrayLike<number>): number {
        const v = this.stage(samples);
        try { return this.mod._vp_ecdf(this.cur, v); }
        finally { this.mod._vp_free(v.byteOffset); }
    }

    fillBetween(xs: ArrayLike<number>, y1: ArrayLike<number>,
                y2: ArrayLike<number>, color = ''): number {
        const a = this.stage(xs), b = this.stage(y1),
              c = this.stage(y2);
        try { return this.mod._vp_fillBetween(this.cur, a, b, c, color); }
        finally { for (const v of [a, b, c])
                      this.mod._vp_free(v.byteOffset); }
    }

    boxplot(groups: ArrayLike<number>[]): number {
        const staged = groups.map(g => this.stage(g));
        try { return this.mod._vp_boxplot(this.cur, staged); }
        finally { for (const v of staged)
                      this.mod._vp_free(v.byteOffset); }
    }

    hist2d(xs: ArrayLike<number>, ys: ArrayLike<number>, bins = 10,
           cmap = 'viridis'): number {
        const a = this.stage(xs), b = this.stage(ys);
        try { return this.mod._vp_hist2d(this.cur, a, b, bins, cmap); }
        finally { this.mod._vp_free(a.byteOffset);
                  this.mod._vp_free(b.byteOffset); }
    }

    hexbin(xs: ArrayLike<number>, ys: ArrayLike<number>): number {
        const a = this.stage(xs), b = this.stage(ys);
        try { return this.mod._vp_hexbin(this.cur, a, b); }
        finally { this.mod._vp_free(a.byteOffset);
                  this.mod._vp_free(b.byteOffset); }
    }

    quiver(xs: ArrayLike<number>, ys: ArrayLike<number>,
           us: ArrayLike<number>, vs: ArrayLike<number>): number {
        const s = [xs, ys, us, vs].map(a => this.stage(a));
        try { return this.mod._vp_quiver(this.cur, s[0], s[1], s[2], s[3]); }
        finally { for (const v of s) this.mod._vp_free(v.byteOffset); }
    }

    contour(values: ArrayLike<number>, w: number, h: number,
            levels = 10, cmap = 'viridis'): number {
        const v = this.stage(values);
        try { return this.mod._vp_contour(this.cur, v, w, h, levels, cmap); }
        finally { this.mod._vp_free(v.byteOffset); }
    }

    /** mpl violinplot — one Float32Array per group. */
    violin(groups: ArrayLike<number>[], width = 0.5,
           showBox = false, color = ''): number {
        const staged = groups.map(g => this.stage(g));
        try { return this.mod._vp_violin(this.cur, staged, width,
                                       showBox, color); }
        finally { for (const v of staged)
                      this.mod._vp_free(v.byteOffset); }
    }

    /** mpl stackplot — stacked area; one layer per element of `ys`. */
    stackplot(xs: ArrayLike<number>, ys: ArrayLike<number>[]): number {
        const a = this.stage(xs);
        const staged = ys.map(g => this.stage(g));
        try { return this.mod._vp_stackplot(this.cur, a, staged); }
        finally { this.mod._vp_free(a.byteOffset);
                  for (const v of staged)
                      this.mod._vp_free(v.byteOffset); }
    }

    /** mpl fill — filled polygon. */
    fill(xs: ArrayLike<number>, ys: ArrayLike<number>,
         color = ''): number {
        const a = this.stage(xs), b = this.stage(ys);
        try { return this.mod._vp_fill(this.cur, a, b, color); }
        finally { this.mod._vp_free(a.byteOffset);
                  this.mod._vp_free(b.byteOffset); }
    }

    /** mpl spy — sparsity pattern of a row-major matrix. */
    spy(data: ArrayLike<number>, nrows: number, ncols: number): number {
        const v = this.stage(data);
        try { return this.mod._vp_spy(this.cur, v, nrows, ncols); }
        finally { this.mod._vp_free(v.byteOffset); }
    }

    /** mpl tripcolor — Delaunay triangles colored by z. */
    tripcolor(xs: ArrayLike<number>, ys: ArrayLike<number>,
              zs: ArrayLike<number>): number {
        const s = [xs, ys, zs].map(a => this.stage(a));
        try { return this.mod._vp_tripcolor(this.cur, s[0], s[1], s[2]); }
        finally { for (const v of s) this.mod._vp_free(v.byteOffset); }
    }

    /** mpl streamplot — row-major w×h vector field. */
    streamplot(us: ArrayLike<number>, vs: ArrayLike<number>,
               w: number, h: number): number {
        const a = this.stage(us), b = this.stage(vs);
        try { return this.mod._vp_streamplot(this.cur, a, b, w, h); }
        finally { this.mod._vp_free(a.byteOffset);
                  this.mod._vp_free(b.byteOffset); }
    }

    pcolormesh(xs: ArrayLike<number>, ys: ArrayLike<number>,
               cs: ArrayLike<number>, nCols: number,
               nRows: number): number {
        const a = this.stage(xs), b = this.stage(ys),
              c = this.stage(cs);
        try { return this.mod._vp_pcolormesh(this.cur, a, b, c, nCols, nRows); }
        finally { for (const v of [a, b, c])
                      this.mod._vp_free(v.byteOffset); }
    }

    kde(xs: ArrayLike<number>, ys: ArrayLike<number>,
        cmap = 'viridis'): number {
        const a = this.stage(xs), b = this.stage(ys);
        try { return this.mod._vp_kde(this.cur, a, b, cmap); }
        finally { this.mod._vp_free(a.byteOffset);
                  this.mod._vp_free(b.byteOffset); }
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
        if (this.interp instanceof Canvas2DInterpreter)
            this.interp.draw(frame, this.ctx2d!);
        else this.interp.draw(frame);
    }
}

export async function createCanvas(
    canvas: HTMLCanvasElement,
    moduleFactory: ModuleFactory,
): Promise<VolcanoCanvas> {
    let device: GPUDevice | undefined;
    let gpuCtx: GPUCanvasContext | undefined;
    let adapter: GPUAdapter | undefined;
    if (navigator.gpu) {
        adapter = await navigator.gpu.requestAdapter() ?? undefined;
        if (adapter) {
            device = await adapter.requestDevice();
            gpuCtx = canvas.getContext('webgpu') ?? undefined;
            if (gpuCtx)
                gpuCtx.configure({
                    device,
                    format: navigator.gpu.getPreferredCanvasFormat(),
                    alphaMode: 'opaque',
                });
        }
    }
    // Falls back to the Canvas2D interpreter when WebGPU is absent.
    // Emscripten resolves side files (.wasm/.data) against the page URL;
    // point it at the module's own directory instead.
    const mod = await moduleFactory({
        locateFile: (p: string) =>
            new URL(p, import.meta.resolve('./volcanoplot.js')).href,
    });
    return new VolcanoCanvas(mod, canvas, device, gpuCtx, adapter);
}
