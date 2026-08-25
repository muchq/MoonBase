import { beforeEach, describe, expect, it } from 'vitest';
import {
  clearDraft,
  getProgress,
  markCompleted,
  resetProgressForTests,
  saveDraft,
  setCustomTests,
} from '../state/progress';

beforeEach(() => resetProgressForTests());

describe('progress store', () => {
  it('starts empty and resets cleanly', () => {
    expect(getProgress().completed).toEqual({});
    markCompleted('expr-tokenize');
    saveDraft('expr-tokenize', 'function tokenize() {}');
    resetProgressForTests();
    expect(getProgress().completed['expr-tokenize']).toBeUndefined();
  });

  it('persists across a reload (fresh read of localStorage)', () => {
    markCompleted('expr-parse');
    saveDraft('expr-parse', 'draft!');
    setCustomTests('expr-parse', [{ id: 'c1', name: 'mine', source: '"1 + 2"' }]);
    // Simulate a reload by re-reading storage without clearing it.
    const raw = localStorage.getItem('astlol:v1');
    expect(raw).not.toBeNull();
    const parsed = JSON.parse(raw!) as ReturnType<typeof getProgress>;
    expect(parsed.completed['expr-parse']).toBeDefined();
    expect(parsed.drafts['expr-parse']).toBe('draft!');
    expect(parsed.customTests['expr-parse']).toHaveLength(1);
  });

  it('completion keeps the first timestamp', () => {
    markCompleted('expr-eval');
    const first = getProgress().completed['expr-eval'];
    markCompleted('expr-eval');
    expect(getProgress().completed['expr-eval']).toBe(first);
  });

  it('clearDraft removes only that draft', () => {
    saveDraft('a', 'aa');
    saveDraft('b', 'bb');
    clearDraft('a');
    expect(getProgress().drafts).toEqual({ b: 'bb' });
  });
});
