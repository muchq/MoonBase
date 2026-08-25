import { describe, expect, it } from 'vitest';
import { deepEqual, firstDiff, show, showPretty } from '../grader/deepEqual';
import { gradeSubmission } from '../grader/harness';
import type { GradableChallenge } from '../grader/types';

const challenge = (over: Partial<GradableChallenge>): GradableChallenge => ({
  id: 'test-challenge',
  entry: 'f',
  solution: 'function f(x) { return x + 1; }',
  tests: [{ name: 'adds one', args: [1], expected: 2 }],
  custom: null,
  ...over,
});

const js = (code: string) => ({ language: 'javascript' as const, code });

describe('firstDiff', () => {
  it('reports the path to the first mismatch inside arrays and objects', () => {
    const diff = firstDiff(
      [{ kind: 'number', text: '12' }, { kind: 'op', text: '+' }],
      [{ kind: 'number', text: '12' }, { kind: 'ident', text: '+' }],
    );
    expect(diff).toMatchObject({ path: '[1].kind', expected: 'op', actual: 'ident' });
  });

  it('distinguishes a missing key from a key holding undefined', () => {
    expect(firstDiff({ alias: null }, {})).toMatchObject({
      path: '.alias',
      reason: "missing key 'alias'",
    });
    expect(firstDiff({}, { alias: undefined })).toMatchObject({
      path: '.alias',
      reason: "unexpected key 'alias'",
    });
  });

  it('reports array length differences at the array itself', () => {
    expect(firstDiff([1, 2, 3], [1, 2])).toMatchObject({
      path: '',
      reason: 'expected 3 element(s), got 2',
    });
  });

  it('treats NaN as equal to itself and key order as irrelevant', () => {
    expect(deepEqual({ a: NaN, b: 1 }, { b: 1, a: NaN })).toBe(true);
  });

  it('type mismatches name both types', () => {
    expect(firstDiff([], {})).toMatchObject({ reason: 'expected array, got object' });
    expect(firstDiff(null, 'x')).toMatchObject({ reason: 'expected null, got string' });
  });
});

describe('show / showPretty', () => {
  it('renders undefined, NaN, and Infinity readably inside structures', () => {
    expect(show({ a: undefined, b: NaN, c: Infinity })).toBe('{"a":undefined,"b":NaN,"c":Infinity}');
    expect(show(Infinity)).toBe('Infinity');
  });

  it('truncates very long values', () => {
    expect(show('x'.repeat(500)).length).toBeLessThan(250);
    expect(showPretty(Array.from({ length: 5000 }, (_, i) => i)).endsWith('… (truncated)')).toBe(
      true,
    );
  });
});

describe('gradeSubmission', () => {
  // Line numbers are attached to runtime errors only: V8 reports Function-
  // constructor syntax errors without a stack position to map.
  it('reports a compile error for a syntax error', () => {
    const report = gradeSubmission(challenge({}), js('function f(x) {\n  return x +;\n}'));
    expect(report.status).toBe('error');
    expect(report.compileError).toContain('SyntaxError');
  });

  it('reports a missing entry function by name', () => {
    const report = gradeSubmission(challenge({}), js('function g(x) { return x; }'));
    expect(report.status).toBe('error');
    expect(report.compileError).toContain("Define a function named 'f'");
  });

  it('maps runtime error lines back to the user code', () => {
    const report = gradeSubmission(
      challenge({}),
      js('function f(x) {\n  const y = x;\n  throw new Error("boom");\n}'),
    );
    expect(report.tests[0].status).toBe('error');
    expect(report.tests[0].message).toContain('boom');
    expect(report.tests[0].message).toContain('(line 3 of your code)');
  });

  it('maps error lines across a prelude', () => {
    const report = gradeSubmission(
      challenge({ prelude: '// one\n// two\n// three' }),
      js('function f(x) {\n  throw new Error("boom");\n}'),
    );
    expect(report.tests[0].message).toContain('(line 2 of your code)');
  });

  it('lets submissions call prelude functions', () => {
    const report = gradeSubmission(
      challenge({ prelude: 'function helper(x) { return x + 1; }' }),
      js('function f(x) { return helper(x); }'),
    );
    expect(report.status).toBe('pass');
  });

  it('captures console output per test', () => {
    const report = gradeSubmission(
      challenge({
        tests: [
          { name: 'one', args: [1], expected: 2 },
          { name: 'two', args: [2], expected: 3 },
        ],
      }),
      js('function f(x) { console.log("saw", { x }); return x + 1; }'),
    );
    expect(report.tests[0].logs).toEqual(['saw {"x":1}']);
    expect(report.tests[1].logs).toEqual(['saw {"x":2}']);
  });

  it('explains a wrong value with the first-difference path and both values', () => {
    const report = gradeSubmission(
      challenge({
        tests: [{ name: 'shape', args: [0], expected: { list: [{ n: 1 }, { n: 2 }] } }],
        solution: 'function f() { return { list: [{ n: 1 }, { n: 2 }] }; }',
      }),
      js('function f() { return { list: [{ n: 1 }, { n: 3 }] }; }'),
    );
    const t = report.tests[0];
    expect(t.status).toBe('fail');
    expect(t.message).toContain('result.list[1].n');
    expect(t.message).toContain('expected 2, got 3');
    expect(t.diffPath).toBe('.list[1].n');
    expect(t.expectedText).toContain('"n": 2');
  });

  it('grades throws-tests on the message content, both directions', () => {
    const c = challenge({
      tests: [{ name: 'rejects', args: [1], throws: { messageIncludes: 'at 4' } }],
    });
    expect(gradeSubmission(c, js('function f() { throw new Error("bad at 4"); }')).status).toBe(
      'pass',
    );
    const wrongMessage = gradeSubmission(c, js('function f() { throw new Error("nope"); }'));
    expect(wrongMessage.tests[0].status).toBe('fail');
    expect(wrongMessage.tests[0].message).toContain('does not contain "at 4"');
    const returns = gradeSubmission(c, js('function f() { return 7; }'));
    expect(returns.tests[0].status).toBe('fail');
    expect(returns.tests[0].message).toContain('returned a value');
  });

  it('keeps running the bank after one test errors', () => {
    const report = gradeSubmission(
      challenge({
        tests: [
          { name: 'boom', args: [0], expected: 1 },
          { name: 'fine', args: [1], expected: 2 },
        ],
      }),
      js('function f(x) { if (x === 0) throw new Error("boom"); return x + 1; }'),
    );
    expect(report.tests.map((t) => t.status)).toEqual(['error', 'pass']);
  });

  it('clones args per test so mutation cannot leak between tests', () => {
    const shared = { n: 1 };
    const report = gradeSubmission(
      challenge({
        tests: [
          { name: 'first mutates', args: [shared], expected: 1 },
          { name: 'second sees original', args: [shared], expected: 1 },
        ],
        solution: 'function f(o) { return o.n; }',
      }),
      js('function f(o) { const n = o.n; o.n = 99; return n; }'),
    );
    expect(report.status).toBe('pass');
    expect(shared.n).toBe(1);
  });

  it('flags a returned Promise as asynchronous', () => {
    const report = gradeSubmission(challenge({}), js('async function f(x) { return x + 1; }'));
    expect(report.tests[0].status).toBe('fail');
    expect(report.tests[0].message).toContain('Promise');
  });

  it('grades custom tests through the oracle, including expected throws', () => {
    const c = challenge({
      solution: 'function f(x) { if (x < 0) throw new Error("negative input"); return x + 1; }',
      custom: {
        describe: 'a number',
        placeholder: '5',
        toArgs: (values) => [values[0]],
      },
    });
    const good = gradeSubmission(c, js('function f(x) { if (x < 0) throw new Error("negative input"); return x + 1; }'), [
      { id: 'u1', name: 'mine', source: '41' },
      { id: 'u2', name: 'negative', source: '-1' },
    ]);
    expect(good.tests.filter((t) => t.custom).map((t) => t.status)).toEqual(['pass', 'pass']);

    const bad = gradeSubmission(c, js('function f(x) { return x + 2; }'), [
      { id: 'u1', name: 'mine', source: '41' },
    ]);
    const custom = bad.tests.find((t) => t.id === 'u1');
    expect(custom?.status).toBe('fail');
    expect(custom?.message).toContain('expected 42, got 43');
  });

  it('reports uncloneable custom-test args as an input error, never a pass', () => {
    // A clone failure inside the grading try would look like "submission
    // and oracle threw the same error" and pass ANY submission.
    const c = challenge({
      custom: {
        describe: 'an env',
        placeholder: '{}',
        toArgs: (values) => [1, values[0]],
      },
    });
    const report = gradeSubmission(
      c,
      js('function f() { return "wrong"; }'),
      [{ id: 'u1', name: 'function in env', source: '{ x: () => 1 }' }],
    );
    const custom = report.tests.find((t) => t.id === 'u1');
    expect(custom?.status).toBe('error');
    expect(custom?.message).toContain("Could not build this test's input");
    expect(report.status).not.toBe('pass');
  });

  it('reports an unbuildable custom input as that test erroring, not the run dying', () => {
    const c = challenge({
      custom: {
        describe: 'a number',
        placeholder: '5',
        toArgs: () => {
          throw new Error('no such table');
        },
      },
    });
    const report = gradeSubmission(c, js('function f(x) { return x + 1; }'), [
      { id: 'u1', name: 'broken', source: '1' },
    ]);
    expect(report.tests.find((t) => t.id === 'u1')).toMatchObject({
      status: 'error',
      message: "Could not build this test's input: no such table",
    });
    expect(report.tests[0].status).toBe('pass');
  });

  it('rejects non-JavaScript submissions for now', () => {
    const report = gradeSubmission(challenge({}), {
      language: 'typescript' as never,
      code: 'const f = (x: number) => x + 1;',
    });
    expect(report.status).toBe('error');
    expect(report.compileError).toContain('not supported yet');
  });
});
