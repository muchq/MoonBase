import { beforeEach, describe, expect, it } from 'vitest';
import {
  clearDraft,
  getProgress,
  markCompleted,
  reloadProgressForTests,
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

describe('loading saved progress', () => {
  const loadSaved = (saved: unknown) => {
    localStorage.setItem('astlol:v1', typeof saved === 'string' ? saved : JSON.stringify(saved));
    reloadProgressForTests();
    return getProgress();
  };

  // What the store saved before lessons had reading checks and tracks.
  const beforeLearningPaths = {
    version: 1,
    completed: { tokens: '2026-01-01T00:00:00.000Z', 'expr-tokenize': '2026-01-02T00:00:00.000Z' },
    drafts: { 'expr-parse': 'draft' },
    customTests: {},
  };
  const empty = {
    version: 1,
    completed: {},
    visited: {},
    read: {},
    practiced: {},
    studied: {},
    track: 'engine',
    theme: 'system',
    drafts: {},
    customTests: {},
  };

  it('keeps completions and drafts saved before learning paths, adding defaults', () => {
    expect(loadSaved(beforeLearningPaths)).toEqual({ ...empty, ...beforeLearningPaths });
  });

  it('keeps a valid track and theme', () => {
    const saved = { ...beforeLearningPaths, track: 'formatter', theme: 'dark' };
    expect(loadSaved(saved)).toEqual({ ...empty, ...saved });
  });

  it('falls back to the default for an unknown track or theme', () => {
    const saved = { ...beforeLearningPaths, track: 'gardening', theme: 'blue' };
    expect(loadSaved(saved)).toEqual({ ...empty, ...beforeLearningPaths });
  });

  it('starts empty from another version or unreadable storage', () => {
    expect(loadSaved({ ...beforeLearningPaths, version: 2 })).toEqual(empty);
    expect(loadSaved('{not json')).toEqual(empty);
  });
});
