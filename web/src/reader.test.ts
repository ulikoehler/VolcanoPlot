// web/src/reader.test.ts — OpReader wire-format tests (no GPU needed).
// Builds synthetic frames byte-for-byte per src/web/OpStream.hpp.
import { describe, it, expect } from 'vitest';
import { OpReader, VPOP_MAGIC, HEADER_BYTES, Op } from './interpreter';

function makeFrame(recs: { op: number; payload: Uint8Array }[],
                   arena: Uint8Array, opts: {
                       frameSeq?: bigint; w?: number; h?: number;
                       clear?: number; load?: boolean } = {}) {
    let recBytes = 0;
    for (const r of recs) recBytes += 8 + ((r.payload.length + 3) & ~3);
    const buf = new ArrayBuffer(HEADER_BYTES + recBytes + arena.length);
    const d = new DataView(buf);
    d.setUint32(0, VPOP_MAGIC, true);
    d.setUint16(4, 1, true);
    d.setUint16(6, opts.load ? 1 : 0, true);
    d.setBigUint64(8, opts.frameSeq ?? 1n, true);
    d.setUint32(16, opts.w ?? 800, true);
    d.setUint32(20, opts.h ?? 600, true);
    d.setUint32(24, recs.length, true);
    d.setUint32(28, HEADER_BYTES + recBytes, true);
    d.setUint32(32, opts.clear ?? 0, true);
    let off = HEADER_BYTES;
    for (const r of recs) {
        d.setUint16(off, r.op, true);
        d.setUint32(off + 4, r.payload.length, true);
        new Uint8Array(buf).set(r.payload, off + 8);
        off += 8 + ((r.payload.length + 3) & ~3);
    }
    new Uint8Array(buf).set(arena, HEADER_BYTES + recBytes);
    return new Uint8Array(buf);
}

function rec(op: Op, payload: ArrayBuffer | Uint8Array) {
    return { op, payload: payload instanceof Uint8Array
        ? payload : new Uint8Array(payload) };
}

const u32 = (...v: number[]) => {
    const b = new Uint8Array(v.length * 4);
    const d = new DataView(b.buffer);
    v.forEach((x, i) => d.setUint32(i * 4, x, true));
    return b;
};

describe('OpReader', () => {
    it('parses the header', () => {
        const f = makeFrame([], new Uint8Array(0),
            { frameSeq: 7n, w: 1920, h: 1080, clear: 0x11223344 });
        const r = new OpReader(f);
        expect(r.header.frameSeq).toBe(7n);
        expect(r.header.canvasW).toBe(1920);
        expect(r.header.canvasH).toBe(1080);
        expect(r.header.opCount).toBe(0);
        expect(r.header.loadOp).toBe(0);
        expect(r.clearRGBA()[0]).toBeCloseTo(0x44 / 255);
        expect(r.clearRGBA()[3]).toBeCloseTo(0x11 / 255);
    });

    it('reads flags bit0 as loadOp', () => {
        const r = new OpReader(makeFrame([], new Uint8Array(0),
                                         { load: true }));
        expect(r.header.loadOp).toBe(1);
    });

    it('rejects bad magic', () => {
        const f = makeFrame([], new Uint8Array(0));
        f[0] = 0;
        expect(() => new OpReader(f)).toThrow('magic');
    });

    it('walks aligned records', () => {
        const f = makeFrame([
            rec(Op.CreateBuffer, u32(42, 0, 0, 1)),
            rec(Op.ReleaseBuffer, u32(42)),
        ], new Uint8Array(0));
        const ops = [...new OpReader(f).ops()];
        expect(ops.length).toBe(2);
        expect(ops[0].op).toBe(Op.CreateBuffer);
        expect(ops[0].p.getUint32(0, true)).toBe(42);
        expect(ops[1].op).toBe(Op.ReleaseBuffer);
    });

    it('skips record padding', () => {
        // 5-byte payload → 3 bytes pad; second record must still parse
        const f = makeFrame([
            rec(Op.CreateBuffer, u32(1).slice(0, 5)),
            rec(Op.ReleaseBuffer, u32(9)),
        ], new Uint8Array(0));
        const ops = [...new OpReader(f).ops()];
        expect(ops[1].op).toBe(Op.ReleaseBuffer);
        expect(ops[1].p.getUint32(0, true)).toBe(9);
    });

    it('resolves arena bulk srcs', () => {
        const arena = new Uint8Array([1, 2, 3, 4, 5, 6, 7, 8]);
        // BufSrc {kind u8, pad[7], off u64, len u64} = 24 B
        const payload = new Uint8Array(24);
        const d = new DataView(payload.buffer);
        d.setUint8(0, 0);
        d.setBigUint64(8, 2n, true);
        d.setBigUint64(16, 4n, true);
        const f = makeFrame([rec(Op.WriteBuffer, payload)], arena);
        const [o] = [...new OpReader(f).ops()];
        const bytes = new OpReader(f).bulk(o.p, 0);
        expect([...bytes]).toEqual([3, 4, 5, 6]);
    });
});
