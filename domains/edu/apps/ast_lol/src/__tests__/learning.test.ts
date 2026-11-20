import { describe, expect, it } from 'vitest';
import { lessonPractice } from '../curriculum/learning';
import { learningPath, pathForStep } from '../curriculum/path';
import { lessons, stepId } from '../curriculum/registry';

describe('learning paths', () => {
  const shared = [
    'welcome',
    'tokens',
    'expr-tokenize',
    'grammars',
    'expr-parse',
    'interpreters',
    'expr-eval',
    'traversal',
    'expr-walk',
    'expr-vars',
    'expr-print',
    'rewrites',
    'expr-fold',
    'sql-subset',
    'sql-tokenize',
    'pratt',
    'sql-expr-parse',
    'clauses',
    'sql-select-parse',
  ];
  const engineOnly = [
    'resolution',
    'sql-resolve',
    'logical-plans',
    'sql-plan',
    'execution',
    'sql-row-expr',
    'sql-join',
    'sql-sort',
    'sql-execute',
    'optimizers',
    'sql-predicate-type',
    'sql-simplify',
    'sql-pushdown',
    'sql-prune',
    'sql-optimize',
    'sql-counterexample',
  ];
  const formatterOnly = ['layout', 'sql-expr-print', 'sql-format'];

  it('runs the shared steps, then the engine branch', () => {
    expect(learningPath('engine').map(stepId)).toEqual([...shared, ...engineOnly]);
  });

  it('runs the shared steps, then the formatter branch', () => {
    expect(learningPath('formatter').map(stepId)).toEqual([...shared, ...formatterOnly]);
  });

  it.each([
    ['sql-format', 'engine', formatterOnly],
    ['sql-execute', 'formatter', engineOnly],
  ] as const)('keeps %s on its own branch even on the %s track', (id, track, branch) => {
    expect(pathForStep(id, track).map(stepId)).toEqual([...shared, ...branch]);
  });

  it.each([
    ['engine', engineOnly],
    ['formatter', formatterOnly],
  ] as const)('puts a shared step on the chosen track: %s', (track, branch) => {
    expect(pathForStep('expr-eval', track).map(stepId)).toEqual([...shared, ...branch]);
  });
});

describe('lesson checks', () => {
  it('every lesson has checks, and no checks exist for a missing lesson', () => {
    expect(Object.keys(lessonPractice).sort()).toEqual(lessons.map((l) => l.id).sort());
  });

  it.each(Object.entries(lessonPractice))(
    '%s: at least two questions, each answerable and explained',
    (_lesson, questions) => {
      expect(questions.length).toBeGreaterThanOrEqual(2);
      for (const { question, options, answer, explanation } of questions) {
        expect(new Set(options).size, `${question}: duplicate options`).toBe(options.length);
        expect(options[answer], `${question}: answer points past the options`).toBeDefined();
        // A sentence of its own, not the right option repeated back.
        expect(explanation, question).toMatch(/^[^a-z\s].*[.?!]$/s);
        expect(explanation, question).not.toBe(options[answer]);
      }
    },
  );
});
