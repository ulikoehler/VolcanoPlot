// web/src/replay.test.ts — replay a real WASM-captured frame through the
// Interpreter against a recording mock GPUDevice. No real GPU needed:
// the mock is a recursive Proxy that returns callable objects and logs
// every method invocation, so we can assert the replay executed every
// op without throwing and produced a sane call sequence.
import { describe, it, expect } from 'vitest';
import { readFileSync } from 'node:fs';
import { Interpreter, OpReader, Op } from './interpreter';

interface Call { path: string[]; args: any[] }

function mockDevice(calls: Call[], path: string[] = []): any {
    const fn: any = function () {};
    return new Proxy(fn, {
        get(_t, prop) {
            if (prop === 'then') return undefined;          // not a promise
            if (prop === 'size') return 8 << 20;             // scratch.size
            if (prop === 'format') return 'rgba8unorm';
            return mockDevice(calls, [...path, String(prop)]);
        },
        apply(_t, _this, args) {
            calls.push({ path, args });
            if (path[path.length - 1] === 'getCompilationInfo')
                return Promise.resolve({ messages: [] });
            return mockDevice(calls, [...path, '()']);
        },
    });
}

// Node has no WebGPU globals — provide the numeric constants the
// interpreter reads (values match the WebGPU spec).
Object.assign(globalThis, {
    GPUBufferUsage: { MAP_READ:1, MAP_WRITE:2, COPY_SRC:4, COPY_DST:8,
                      INDEX:16, VERTEX:32, UNIFORM:64, STORAGE:128,
                      INDIRECT:256, QUERY_RESOLVE:512 },
    GPUTextureUsage: { COPY_SRC:1, COPY_DST:2, TEXTURE_BINDING:4,
                       STORAGE_BINDING:8, RENDER_ATTACHMENT:16 },
    GPUShaderStage: { VERTEX:1, FRAGMENT:2, COMPUTE:4 },
});

const FIXTURE = new URL('../test/fixtures/line_frame.bin', import.meta.url);

describe('Interpreter replay (mock GPU)', () => {
    const frameBytes = () =>
        new Uint8Array(readFileSync(FIXTURE).buffer.slice(0)) as
            Uint8Array<ArrayBuffer>;

    it('fixture decodes as a valid VPOP frame', () => {
        const r = new OpReader(frameBytes());
        expect(r.header.canvasW).toBe(320);
        expect(r.header.canvasH).toBe(240);
        const ops = [...r.ops()].map(o => o.op);
        // real line+scatter frame must contain resource + draw ops
        expect(ops).toContain(Op.CreateBuffer);
        expect(ops).toContain(Op.WriteBuffer);
        expect(ops.some(o => o >= Op.DrawTrisPx && o <= Op.DrawGrid3D))
            .toBe(true);
    });

    it('replays every op without throwing', () => {
        const calls: Call[] = [];
        const device = mockDevice(calls);
        const interp = new Interpreter(device, device, 'rgba8unorm');
        interp.init();
        expect(() => interp.draw(frameBytes())).not.toThrow();
        // one render pass, submitted
        const names = calls.map(c => c.path.join('.'));
        expect(names).toContain('createCommandEncoder');
        expect(names.some(n => n.endsWith('beginRenderPass'))).toBe(true);
        expect(names.filter(n => n.endsWith('.draw')).length)
            .toBeGreaterThan(3);
        expect(names.filter(n => n === 'queue.submit')).toHaveLength(1);
        // resource ops became real device calls
        expect(names.filter(n => n === 'createBuffer').length)
            .toBeGreaterThanOrEqual(3);
        expect(names.filter(n => n === 'queue.writeBuffer').length)
            .toBeGreaterThanOrEqual(3);
    });
});
