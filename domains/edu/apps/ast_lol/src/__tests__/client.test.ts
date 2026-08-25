import { describe, expect, it, vi } from 'vitest';
import { runGrader } from '../grader/client';
import type { GradeEvent, GradeReport } from '../grader/types';

/** A worker double the client drives exactly like the real one. */
class FakeWorker {
  onmessage: ((e: MessageEvent<GradeEvent>) => void) | null = null;
  onerror: ((e: ErrorEvent) => void) | null = null;
  terminated = false;
  posted: unknown[] = [];
  script: (requestId: number, emit: (e: GradeEvent) => void) => void;

  constructor(script: FakeWorker['script']) {
    this.script = script;
  }

  postMessage(msg: { requestId: number }): void {
    this.posted.push(msg);
    this.script(msg.requestId, (e) => this.onmessage?.({ data: e } as MessageEvent<GradeEvent>));
  }

  terminate(): void {
    this.terminated = true;
  }
}

const report = (over: Partial<GradeReport>): GradeReport => ({
  challengeId: 'x',
  status: 'pass',
  tests: [],
  ...over,
});

describe('runGrader', () => {
  it('resolves with the report and terminates the worker', async () => {
    const worker = new FakeWorker((requestId, emit) => {
      emit({ type: 'test-start', requestId, id: 'builtin-0', name: 't' });
      emit({
        type: 'test-result',
        requestId,
        result: { id: 'builtin-0', name: 't', custom: false, status: 'pass', logs: [] },
      });
      emit({ type: 'done', requestId, report: report({}) });
    });
    const result = await runGrader(
      'x',
      { language: 'javascript', code: '' },
      [],
      [{ id: 'builtin-0', name: 't' }],
      { createWorker: () => worker as unknown as Worker },
    );
    expect(result.status).toBe('pass');
    expect(worker.terminated).toBe(true);
  });

  it('streams progress to the caller', async () => {
    const seen: string[] = [];
    const worker = new FakeWorker((requestId, emit) => {
      emit({ type: 'test-start', requestId, id: 'builtin-0', name: 'first' });
      emit({ type: 'test-start', requestId, id: 'builtin-1', name: 'second' });
      emit({ type: 'done', requestId, report: report({}) });
    });
    await runGrader('x', { language: 'javascript', code: '' }, [], [], {
      createWorker: () => worker as unknown as Worker,
      onProgress: (running) => seen.push(running.name),
    });
    expect(seen).toEqual(['first', 'second']);
  });

  it('on timeout, marks the hung test and skips the rest', async () => {
    vi.useFakeTimers();
    try {
      const worker = new FakeWorker((requestId, emit) => {
        emit({ type: 'test-start', requestId, id: 'builtin-0', name: 'ok' });
        emit({
          type: 'test-result',
          requestId,
          result: { id: 'builtin-0', name: 'ok', custom: false, status: 'pass', logs: [] },
        });
        emit({ type: 'test-start', requestId, id: 'builtin-1', name: 'spins forever' });
        // …and never finishes.
      });
      const promise = runGrader(
        'x',
        { language: 'javascript', code: '' },
        [],
        [
          { id: 'builtin-0', name: 'ok' },
          { id: 'builtin-1', name: 'spins forever' },
          { id: 'builtin-2', name: 'never started' },
        ],
        { createWorker: () => worker as unknown as Worker, timeoutMs: 100 },
      );
      await vi.advanceTimersByTimeAsync(150);
      const result = await promise;
      expect(result.status).toBe('timeout');
      expect(result.tests.map((t) => [t.id, t.status])).toEqual([
        ['builtin-0', 'pass'],
        ['builtin-1', 'timeout'],
        ['builtin-2', 'skipped'],
      ]);
      expect(result.tests[1].message).toContain('infinite loop');
      expect(worker.terminated).toBe(true);
    } finally {
      vi.useRealTimers();
    }
  });

  it('does not blame a test that finished just under the wire, and keeps custom flags', async () => {
    vi.useFakeTimers();
    try {
      const worker = new FakeWorker((requestId, emit) => {
        emit({ type: 'test-start', requestId, id: 'builtin-0', name: 'finished' });
        emit({
          type: 'test-result',
          requestId,
          result: { id: 'builtin-0', name: 'finished', custom: false, status: 'pass', logs: [] },
        });
        emit({ type: 'test-start', requestId, id: 'custom-1', name: 'mine, spins' });
        // custom-1 never reports; done never arrives.
      });
      const promise = runGrader(
        'x',
        { language: 'javascript', code: '' },
        [],
        [
          { id: 'builtin-0', name: 'finished', custom: false },
          { id: 'custom-1', name: 'mine, spins', custom: true },
          { id: 'custom-2', name: 'mine, never started', custom: true },
        ],
        { createWorker: () => worker as unknown as Worker, timeoutMs: 100 },
      );
      await vi.advanceTimersByTimeAsync(150);
      const result = await promise;
      const ids = result.tests.map((t) => t.id);
      expect(new Set(ids).size, 'no id may appear twice').toBe(ids.length);
      expect(result.tests.map((t) => [t.id, t.status, t.custom])).toEqual([
        ['builtin-0', 'pass', false],
        ['custom-1', 'timeout', true],
        ['custom-2', 'skipped', true],
      ]);
    } finally {
      vi.useRealTimers();
    }
  });

  it('a run whose every started test reported never synthesizes a timeout row', async () => {
    vi.useFakeTimers();
    try {
      const worker = new FakeWorker((requestId, emit) => {
        emit({ type: 'test-start', requestId, id: 'builtin-0', name: 'only' });
        emit({
          type: 'test-result',
          requestId,
          result: { id: 'builtin-0', name: 'only', custom: false, status: 'pass', logs: [] },
        });
        // The worker hangs after the last result, before done.
      });
      const promise = runGrader(
        'x',
        { language: 'javascript', code: '' },
        [],
        [{ id: 'builtin-0', name: 'only' }],
        { createWorker: () => worker as unknown as Worker, timeoutMs: 100 },
      );
      await vi.advanceTimersByTimeAsync(150);
      const result = await promise;
      expect(result.tests).toHaveLength(1);
      expect(result.tests[0]).toMatchObject({ id: 'builtin-0', status: 'pass' });
      expect(result.status).toBe('timeout');
    } finally {
      vi.useRealTimers();
    }
  });

  it('surfaces a fatal worker error as a compile-style error', async () => {
    const worker = new FakeWorker((requestId, emit) => {
      emit({ type: 'fatal', requestId, error: 'kaboom' });
    });
    const result = await runGrader('x', { language: 'javascript', code: '' }, [], [], {
      createWorker: () => worker as unknown as Worker,
    });
    expect(result.status).toBe('error');
    expect(result.compileError).toBe('kaboom');
  });
});
