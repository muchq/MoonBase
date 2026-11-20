import { describe, expect, it } from 'vitest';
import { challenges, lessons, orderedSteps, stepId } from '../curriculum/registry';
import { CAPSTONE_HEADROOM, CAPSTONE_QUERIES } from '../curriculum/tier5';
import { compileSubmission, gradeSubmission } from '../grader/harness';
import {
  benchDb,
  buildPlan,
  executePlan,
  executeWithStats,
  optimizePlan,
  parseSelect,
  parseSqlExpr,
  resolve,
  shopCatalog,
  tokenizeSql,
  type Plan,
} from '../lang/sql';

const expr = (source: string) => parseSqlExpr(tokenizeSql(source));

/**
 * The curriculum's grading contract, proven in CI:
 *  - every reference solution passes its own challenge (so the oracle that
 *    grades custom tests is itself green);
 *  - every starter fails (so the test bank actually bites);
 *  - every custom-test placeholder builds a runnable input and the
 *    reference solution passes it (so the first thing a user tries works).
 */

describe('curriculum registry', () => {
  it('step ids are unique across lessons and challenges', () => {
    const ids = orderedSteps.map(stepId);
    expect(new Set(ids).size).toBe(ids.length);
  });

  it('has the promised shape: 24 challenges, 14 lessons', () => {
    expect(challenges.length).toBe(24);
    expect(lessons.length).toBe(14);
  });

  it('every lesson links further reading over https', () => {
    for (const lesson of lessons) {
      expect(lesson.reading.length, lesson.id).toBeGreaterThanOrEqual(1);
      for (const r of lesson.reading) {
        expect(r.url, `${lesson.id}: ${r.title}`).toMatch(/^https:\/\//);
      }
    }
  });

  it('every challenge has a substantial test bank and custom-test support', () => {
    for (const challenge of challenges) {
      expect(challenge.tests.length, challenge.id).toBeGreaterThanOrEqual(5);
      expect(challenge.custom, challenge.id).not.toBeNull();
      const hints = challenge.tests.filter((t) => t.hint !== undefined).length;
      expect(hints, `${challenge.id} should carry debugging hints`).toBeGreaterThanOrEqual(1);
    }
  });
});

describe('reference solutions', () => {
  it.each(challenges.map((c) => [c.id, c] as const))('%s passes its own test bank', (_id, c) => {
    const report = gradeSubmission(c, { language: 'javascript', code: c.solution });
    const failing = report.tests.filter((t) => t.status !== 'pass');
    expect(failing, JSON.stringify(failing, null, 2)).toEqual([]);
    expect(report.status).toBe('pass');
  });
});

describe('starter code', () => {
  it.each(challenges.map((c) => [c.id, c] as const))('%s starter does not pass', (_id, c) => {
    const report = gradeSubmission(c, { language: 'javascript', code: c.starter });
    expect(report.status).not.toBe('pass');
  });
});

describe('custom-test placeholders', () => {
  it.each(challenges.map((c) => [c.id, c] as const))(
    '%s placeholder input grades green against the reference solution',
    (_id, c) => {
      const report = gradeSubmission(
        c,
        { language: 'javascript', code: c.solution },
        [{ id: 'custom-placeholder', name: 'placeholder', source: c.custom!.placeholder }],
      );
      const custom = report.tests.find((t) => t.id === 'custom-placeholder');
      expect(custom, JSON.stringify(custom, null, 2)).toMatchObject({ status: 'pass' });
    },
  );

  // The green run above cannot fail on oracle-graded challenges (the graded
  // submission IS the oracle), so also pin the half that matters: the
  // placeholder builds a real input, and the reference solution *returns*
  // for it rather than throwing — a first experience that works.
  it.each(challenges.map((c) => [c.id, c] as const))(
    '%s placeholder builds an input the reference solution accepts',
    (_id, c) => {
      // eslint-disable-next-line @typescript-eslint/no-implied-eval
      const values = new Function(`"use strict"; return [\n${c.custom!.placeholder}\n];`)() as unknown[];
      const args = c.custom!.toArgs(values);
      const oracle = compileSubmission(
        { language: 'javascript', code: c.solution },
        c.entry,
        c.prelude ?? '',
      );
      expect(oracle.call, c.id).not.toBeNull();
      expect(() => oracle.call!(...structuredClone(args)), c.id).not.toThrow();
    },
  );
});

describe('capstone battery', () => {
  const planOf = (q: string): Plan => {
    const r = resolve(parseSelect(tokenizeSql(q)), shopCatalog);
    expect(r.errors).toEqual([]);
    return buildPlan(r.select!, shopCatalog);
  };

  it.each(CAPSTONE_QUERIES.map((e, i) => [`q${i + 1}`, e] as const))(
    '%s: pinned budget and naive cost match the formula',
    (_label, entry) => {
      const naive = planOf(entry.q);
      const naiveCost = executeWithStats(naive, benchDb).cost;
      const refCost = executeWithStats(optimizePlan(naive), benchDb).cost;
      expect(entry.naive, 'naive cost drifted — paste the fresh number').toBe(naiveCost);
      if (entry.mode === 'reduce') {
        expect(entry.budget).toBe(Math.ceil(refCost * CAPSTONE_HEADROOM));
        // A budget at or above the naive cost would let a no-op optimizer pass.
        expect(entry.budget).toBeLessThan(naiveCost);
      } else {
        expect(refCost, 'a guard query must be unimprovable by the pipeline').toBe(naiveCost);
        expect(entry.budget).toBe(naiveCost);
      }
    },
  );

  it('every query except the deliberate contradiction returns rows on the bench database', () => {
    for (const entry of CAPSTONE_QUERIES) {
      const rows = executePlan(planOf(entry.q), benchDb).length;
      if (entry.q.includes('1 = 2')) {
        expect(rows, entry.q).toBe(0);
      } else {
        // Equivalence over an empty result proves nothing.
        expect(rows, entry.q).toBeGreaterThan(0);
      }
    }
  });
});

/**
 * Each test bank must fail the plausible wrong answer a learner is most likely
 * to write, not just the empty starter. Each case edits the reference solution
 * into that mistake, checks the edit applied, and expects the grade to fail.
 */
describe('test banks catch common mistakes', () => {
  const challenge = (id: string) => {
    const found = challenges.find((c) => c.id === id);
    if (!found) throw new Error(`no challenge ${id}`);
    return found;
  };

  const gradeMistake = (id: string, correct: string, mistaken: string) => {
    const c = challenge(id);
    expect(c.solution, `${id}'s solution no longer contains the line to edit`).toContain(correct);
    const code = c.solution.replaceAll(correct, mistaken);
    return gradeSubmission(c, { language: 'javascript', code }).status;
  };

  it('expr-fold: folding a result that is not finite', () => {
    expect(gradeMistake('expr-fold', 'Number.isFinite(value)', 'true')).toBe('fail');
  });

  it('sql-row-expr: using JavaScript truthiness for AND/OR/NOT', () => {
    expect(
      gradeMistake(
        'sql-row-expr',
        'return v === true ? true : v === null ? null : false;',
        'return v === null ? null : !!v;',
      ),
    ).toBe('fail');
  });

  it('sql-predicate-type: assuming a column holds a boolean', () => {
    expect(
      gradeMistake(
        'sql-predicate-type',
        "if (expr.type === 'Lit')",
        "if (expr.type === 'Column') return true; if (expr.type === 'Lit')",
      ),
    ).toBe('fail');
  });

  it('sql-simplify: dropping an identity next to a non-boolean operand', () => {
    expect(gradeMistake('sql-simplify', ' && isPredicate(right)', '')).toBe('fail');
  });

  it('sql-counterexample: accepts any witness, and only a witness', () => {
    const { check } = challenge('sql-counterexample');
    const args = [expr('TRUE AND u.x'), expr('u.x')];
    expect(check!({ 'u.x': 'x' }, { args }).pass).toBe(true);
    expect(check!({ 'u.x': 0 }, { args }).pass).toBe(true);
    expect(check!({ 'u.x': true }, { args }).pass).toBe(false);
    expect(check!(null, { args }).pass).toBe(false);
  });

  // A submission that returns the reference answer, then marks its first
  // argument: correct output, broken promise.
  const mutatesItsInput = ({ solution, entry }: { solution: string; entry: string }) => `
    const reference = (() => { ${solution}; return ${entry}; })();
    function ${entry}(...args) {
      const result = reference(...structuredClone(args));
      args[0].mutated = true;
      return result;
    }`;

  it.each(challenges.filter((c) => c.immutableInputs).map((c) => [c.id, c] as const))(
    '%s: changing an input it promises not to change',
    (_id, c) => {
      const report = gradeSubmission(c, { language: 'javascript', code: mutatesItsInput(c) });
      expect(report.status).toBe('fail');
      expect(report.tests.some((t) => t.message?.includes('Input changed'))).toBe(true);
    },
  );
});
