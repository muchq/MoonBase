import { describe, expect, it } from 'vitest';
import { lessonPractice } from '../curriculum/learning';
import { learningPath, pathForStep } from '../curriculum/path';
import { lessons, stepId } from '../curriculum/registry';

describe('learning paths', () => {
  const engine = learningPath('engine').map(stepId);
  const formatter = learningPath('formatter').map(stepId);

  it('branches to formatting right after the shared SQL parser', () => {
    expect(formatter[formatter.indexOf('sql-select-parse') + 1]).toBe('layout');
  });

  it('keeps each branch free of the other branch', () => {
    expect(formatter).not.toContain('sql-execute');
    expect(engine).not.toContain('sql-format');
  });

  it('follows the branch a step belongs to, whatever the chosen track', () => {
    expect(pathForStep('sql-format', 'engine').map(stepId)).toEqual(formatter);
  });
});

describe('lesson checks', () => {
  it('every lesson has checks, and no checks exist for a missing lesson', () => {
    expect(Object.keys(lessonPractice).sort()).toEqual(lessons.map((l) => l.id).sort());
  });

  it.each(Object.entries(lessonPractice))(
    '%s: at least two questions, each with a real answer and explanation',
    (_lesson, questions) => {
      expect(questions.length).toBeGreaterThanOrEqual(2);
      for (const question of questions) {
        expect(question.options[question.answer]).toBeDefined();
        expect(question.explanation.length).toBeGreaterThan(30);
      }
    },
  );
});
