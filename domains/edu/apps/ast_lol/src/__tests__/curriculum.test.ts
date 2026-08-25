import { describe, expect, it } from 'vitest';
import { challenges, lessons, orderedSteps, stepId } from '../curriculum/registry';
import { gradeSubmission } from '../grader/harness';

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

  it('has the promised shape: 16 challenges, 13 lessons', () => {
    expect(challenges.length).toBe(16);
    expect(lessons.length).toBe(13);
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
});
