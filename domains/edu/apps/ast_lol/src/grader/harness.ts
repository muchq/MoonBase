import { firstDiff, show, showPretty } from './deepEqual';
import type {
  CheckFn,
  CustomTestSpec,
  GradableChallenge,
  GradeReport,
  Submission,
  TestResult,
  TestSpec,
} from './types';

/**
 * The grading engine, as a pure synchronous function so the same code runs
 * in the browser worker and directly under vitest. Preemption (infinite
 * loops) is the worker client's job: it terminates the worker on timeout.
 */

type Entry = (...args: unknown[]) => unknown;

interface Compiled {
  call: Entry | null;
  error: string | null;
  setSink: (sink: string[] | null) => void;
}

/**
 * Where user line 1 lands inside the Function-constructor wrapper, probed
 * at runtime because engines format the synthesized `function anonymous`
 * header differently. Null when the engine's stacks don't say.
 */
let lineOffset: number | null | undefined;

function probeLineOffset(): number | null {
  if (lineOffset !== undefined) return lineOffset;
  lineOffset = null;
  try {
    // eslint-disable-next-line @typescript-eslint/no-implied-eval
    new Function('console', '"use strict";\nthrow new Error("probe");')(makeConsole(() => null));
  } catch (e) {
    const line = stackLine(e);
    // The throw sits on body line 2.
    if (line !== null) lineOffset = line - 2;
  }
  return lineOffset;
}

function stackLine(e: unknown): number | null {
  const stack = e instanceof Error ? `${e.stack ?? ''}\n${e.message}` : String(e);
  const m = /<anonymous>:(\d+):\d+/.exec(stack) ?? /Function:(\d+):\d+/.exec(stack);
  return m === null ? null : Number(m[1]);
}

function describeError(e: unknown, codeLines: number, headerLines: number): string {
  const base = e instanceof Error ? `${e.name}: ${e.message}` : `threw ${show(e)}`;
  const offset = probeLineOffset();
  const line = stackLine(e);
  if (offset !== null && line !== null) {
    const userLine = line - offset - headerLines;
    if (userLine >= 1 && userLine <= codeLines) return `${base} (line ${userLine} of your code)`;
  }
  return base;
}

function makeConsole(getSink: () => string[] | null): Console {
  const write = (...args: unknown[]) => {
    const sink = getSink();
    if (sink === null) return;
    if (sink.length >= 50) {
      if (sink[sink.length - 1] !== '… output truncated') sink.push('… output truncated');
      return;
    }
    sink.push(args.map((a) => (typeof a === 'string' ? a : show(a))).join(' '));
  };
  const c = { log: write, info: write, warn: write, error: write, debug: write } as unknown as {
    [k: string]: (...args: unknown[]) => void;
  };
  return new Proxy(c, {
    get: (target, prop: string) => target[prop] ?? (() => undefined),
  }) as unknown as Console;
}

export function compileSubmission(submission: Submission, entry: string, prelude = ''): Compiled {
  let sink: string[] | null = null;
  const setSink = (s: string[] | null) => {
    sink = s;
  };
  if (submission.language !== 'javascript') {
    return { call: null, error: `Language '${submission.language}' is not supported yet`, setSink };
  }
  // Body lines before the user's line 1: "use strict" plus the prelude.
  const parts = ['"use strict";'];
  if (prelude !== '') parts.push(prelude);
  const headerLines = 1 + (prelude === '' ? 0 : prelude.split('\n').length);
  parts.push(submission.code, `;return typeof ${entry} === 'function' ? ${entry} : undefined;`);
  const codeLines = submission.code.split('\n').length;
  let fn: unknown;
  try {
    // eslint-disable-next-line @typescript-eslint/no-implied-eval
    const factory = new Function('console', parts.join('\n'));
    fn = factory(makeConsole(() => sink));
  } catch (e) {
    return { call: null, error: describeError(e, codeLines, headerLines), setSink };
  }
  if (typeof fn !== 'function') {
    return {
      call: null,
      error: `Define a function named '${entry}' at the top level — the grader calls it directly.`,
      setSink,
    };
  }
  return { call: fn as Entry, error: null, setSink };
}

interface RunOutcome {
  threw: boolean;
  value?: unknown;
  error?: unknown;
}

function runEntry(call: Entry, args: unknown[]): RunOutcome {
  try {
    return { threw: false, value: call(...structuredClone(args)) };
  } catch (e) {
    return { threw: true, error: e };
  }
}

function gradeOutcome(
  spec: { name: string; expected?: unknown; throws?: { messageIncludes: string }; check?: CheckFn; hint?: string },
  outcome: RunOutcome,
  args: unknown[],
  oracle: Entry | null,
  codeLines: number,
  headerLines: number,
): Omit<TestResult, 'id' | 'custom' | 'logs'> {
  const { name, hint } = spec;

  if (spec.throws !== undefined) {
    const want = spec.throws.messageIncludes;
    if (!outcome.threw) {
      return {
        name,
        status: 'fail',
        message: `Expected a thrown Error whose message contains "${want}", but the function returned a value.`,
        expectedText: `throw new Error("… ${want} …")`,
        actualText: showPretty(outcome.value),
        hint,
      };
    }
    const message = outcome.error instanceof Error ? outcome.error.message : String(outcome.error);
    if (!message.includes(want)) {
      return {
        name,
        status: 'fail',
        message: `An error was thrown, but its message does not contain "${want}".`,
        expectedText: `throw new Error("… ${want} …")`,
        actualText: `threw: ${message}`,
        hint,
      };
    }
    return { name, status: 'pass' };
  }

  // A thrown error is not judged yet: when the expected value comes from
  // the oracle, the oracle may throw too — in which case throwing is the
  // *correct* behavior, graded below via the throws branch.
  const errored = (): Omit<TestResult, 'id' | 'custom' | 'logs'> => ({
    name,
    status: 'error',
    message: describeError(outcome.error, codeLines, headerLines),
    hint,
  });

  if (!outcome.threw && outcome.value instanceof Promise) {
    return {
      name,
      status: 'fail',
      message:
        'Your function returned a Promise. Solutions here are synchronous — return the value directly.',
      hint,
    };
  }

  if (spec.check !== undefined) {
    if (outcome.threw) return errored();
    let result;
    try {
      result = spec.check(outcome.value, { args: structuredClone(args) });
    } catch (e) {
      return {
        name,
        status: 'fail',
        message: `The checker could not process your output: ${e instanceof Error ? e.message : show(e)}`,
        actualText: showPretty(outcome.value),
        hint,
      };
    }
    if (result.pass) return { name, status: 'pass' };
    return {
      name,
      status: 'fail',
      message: result.message,
      expectedText: result.expectedText,
      actualText: result.actualText ?? showPretty(outcome.value),
      hint,
    };
  }

  let expected = spec.expected;
  if (expected === undefined) {
    if (oracle === null) {
      if (outcome.threw) return errored();
      return { name, status: 'error', message: 'Internal: no expected value and no oracle.' };
    }
    let oracleOutcome: RunOutcome;
    try {
      oracleOutcome = { threw: false, value: oracle(...structuredClone(args)) };
    } catch (e) {
      oracleOutcome = { threw: true, error: e };
    }
    if (oracleOutcome.threw) {
      // The reference solution rejects this input, so a correct submission
      // must too — with the same message.
      const message =
        oracleOutcome.error instanceof Error ? oracleOutcome.error.message : String(oracleOutcome.error);
      return gradeOutcome(
        { name, throws: { messageIncludes: message }, hint },
        outcome,
        args,
        null,
        codeLines,
        headerLines,
      );
    }
    expected = oracleOutcome.value;
  }

  if (outcome.threw) return errored();

  const diff = firstDiff(expected, outcome.value);
  if (diff === null) return { name, status: 'pass' };
  const where = diff.path === '' ? 'the result' : `result${diff.path}`;
  return {
    name,
    status: 'fail',
    message: `First difference at ${where}: ${diff.reason} — expected ${show(diff.expected)}, got ${show(diff.actual)}.`,
    diffPath: diff.path === '' ? undefined : diff.path,
    expectedText: showPretty(expected),
    actualText: showPretty(outcome.value),
    hint,
  };
}

export interface GradeCallbacks {
  onTestStart?: (id: string, name: string) => void;
  onTestResult?: (result: TestResult) => void;
}

export function gradeSubmission(
  challenge: GradableChallenge,
  submission: Submission,
  customTests: CustomTestSpec[] = [],
  callbacks: GradeCallbacks = {},
): GradeReport {
  const compiled = compileSubmission(submission, challenge.entry, challenge.prelude ?? '');
  if (compiled.call === null) {
    return {
      challengeId: challenge.id,
      status: 'error',
      compileError: compiled.error ?? 'Failed to compile.',
      tests: [],
    };
  }
  // The oracle is the reference solution compiled in the same environment;
  // CI proves it passes every built-in test.
  const oracleCompiled = compileSubmission(
    { language: 'javascript', code: challenge.solution },
    challenge.entry,
    challenge.prelude ?? '',
  );
  const oracle = oracleCompiled.call;
  const codeLines = submission.code.split('\n').length;
  const prelude = challenge.prelude ?? '';
  const headerLines = 1 + (prelude === '' ? 0 : prelude.split('\n').length);

  const results: TestResult[] = [];
  const emit = (result: TestResult) => {
    results.push(result);
    callbacks.onTestResult?.(result);
  };
  const inputError = (id: string, custom: boolean, name: string, e: unknown): TestResult => ({
    id,
    name,
    custom,
    status: 'error',
    message: `Could not build this test's input: ${e instanceof Error ? e.message : show(e)}`,
    logs: [],
  });

  const run = (id: string, custom: boolean, spec: TestSpec, args: unknown[]) => {
    callbacks.onTestStart?.(id, spec.name);
    // Validate cloneability up front: a clone failure inside the grading
    // try would read as "both the submission and the oracle threw the same
    // error" and pass every submission.
    try {
      structuredClone(args);
    } catch (e) {
      emit(inputError(id, custom, spec.name, e));
      return;
    }
    const logs: string[] = [];
    compiled.setSink(logs);
    const outcome = runEntry(compiled.call!, args);
    compiled.setSink(null);
    const graded = gradeOutcome(spec, outcome, args, oracle, codeLines, headerLines);
    emit({ id, custom, logs, ...graded });
  };

  challenge.tests.forEach((spec, i) => {
    run(`builtin-${i}`, false, { check: challenge.check, ...spec }, spec.args);
  });

  for (const custom of customTests) {
    let args: unknown[];
    try {
      if (challenge.custom === null) throw new Error('custom tests are not enabled here');
      // eslint-disable-next-line @typescript-eslint/no-implied-eval
      const values = new Function(`"use strict"; return [\n${custom.source}\n];`)() as unknown[];
      args = challenge.custom.toArgs(values);
    } catch (e) {
      callbacks.onTestStart?.(custom.id, custom.name);
      emit(inputError(custom.id, true, custom.name, e));
      continue;
    }
    run(custom.id, true, { name: custom.name, args, check: challenge.check }, args);
  }

  const allPass = results.every((r) => r.status === 'pass');
  return {
    challengeId: challenge.id,
    status: allPass ? 'pass' : 'fail',
    tests: results,
  };
}
