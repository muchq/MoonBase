import type { CustomTestSpec, GradeEvent, GradeReport, Submission, TestResult } from './types';

/**
 * Main-thread grader client. Each run gets a fresh worker; on timeout the
 * worker is terminated and the partial report is assembled from the
 * per-test events received so far, with the in-flight test marked
 * `timeout` and the rest `skipped` — so "which test hung" is part of the
 * feedback, not lost with the worker.
 */

const DEFAULT_TIMEOUT_MS = 5000;

export interface RunningTest {
  id: string;
  name: string;
}

export interface GraderClientOptions {
  timeoutMs?: number;
  /** Streamed progress for the UI. */
  onProgress?: (running: RunningTest, completed: TestResult[]) => void;
  /** Injectable for tests; defaults to the real worker. */
  createWorker?: () => Worker;
}

function defaultWorker(): Worker {
  return new Worker(new URL('./worker.ts', import.meta.url), { type: 'module' });
}

export function runGrader(
  challengeId: string,
  submission: Submission,
  customTests: CustomTestSpec[],
  plannedTests: { id: string; name: string; custom?: boolean }[],
  options: GraderClientOptions = {},
): Promise<GradeReport> {
  const { timeoutMs = DEFAULT_TIMEOUT_MS, onProgress, createWorker = defaultWorker } = options;
  const requestId = Date.now() + Math.random();
  const plannedById = new Map(plannedTests.map((p) => [p.id, p]));

  return new Promise((resolve) => {
    let worker: Worker;
    try {
      worker = createWorker();
    } catch (e) {
      resolve({
        challengeId,
        status: 'error',
        compileError: `The grader could not start: ${e instanceof Error ? e.message : String(e)}`,
        tests: [],
      });
      return;
    }
    const completed: TestResult[] = [];
    let running: RunningTest | null = null;
    let settled = false;

    const finish = (report: GradeReport) => {
      if (settled) return;
      settled = true;
      clearTimeout(timer);
      worker.terminate();
      resolve(report);
    };

    const timer = setTimeout(() => {
      const tests: TestResult[] = [...completed];
      const done = new Set(tests.map((t) => t.id));
      // A test is blamed for the timeout only if it started and never
      // reported — one that finished just under the wire (its result in,
      // the done message still queued behind this timer) stays passed.
      if (running !== null && !done.has(running.id)) {
        tests.push({
          id: running.id,
          name: running.name,
          custom: plannedById.get(running.id)?.custom ?? false,
          status: 'timeout',
          message: `Still running after ${timeoutMs}ms — check this test's input for an infinite loop in your code.`,
          logs: [],
        });
        done.add(running.id);
      }
      for (const planned of plannedTests) {
        if (!done.has(planned.id)) {
          tests.push({
            id: planned.id,
            name: planned.name,
            custom: planned.custom ?? false,
            status: 'skipped',
            message: 'Not run: an earlier test exceeded the time budget.',
            logs: [],
          });
        }
      }
      finish({ challengeId, status: 'timeout', tests });
    }, timeoutMs);

    worker.onmessage = (e: MessageEvent<GradeEvent>) => {
      const event = e.data;
      if (event.requestId !== requestId) return;
      switch (event.type) {
        case 'test-start':
          running = { id: event.id, name: event.name };
          onProgress?.(running, [...completed]);
          return;
        case 'test-result':
          completed.push(event.result);
          if (running !== null && running.id === event.result.id) running = null;
          return;
        case 'done':
          finish(event.report);
          return;
        case 'fatal':
          finish({
            challengeId,
            status: 'error',
            compileError: event.error,
            tests: completed,
          });
          return;
      }
    };

    worker.onerror = (e) => {
      finish({
        challengeId,
        status: 'error',
        compileError: `The grader worker failed: ${e.message}`,
        tests: completed,
      });
    };

    worker.postMessage({
      type: 'grade',
      requestId,
      challengeId,
      submission,
      customTests,
    });
  });
}
