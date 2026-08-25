import { useSyncExternalStore } from 'react';
import type { CustomTestSpec } from '../grader/types';

/**
 * All learner state lives in localStorage: which steps are done, code
 * drafts, and user-authored custom tests. Fully client-side, like the
 * grader itself — clearing site data resets the course.
 */

export interface ProgressState {
  version: 1;
  /** Step id → ISO timestamp. Lessons complete on visit, challenges on a green run. */
  completed: Record<string, string>;
  /** Challenge id → editor draft. */
  drafts: Record<string, string>;
  /** Challenge id → user-authored tests. */
  customTests: Record<string, CustomTestSpec[]>;
}

const KEY = 'astlol:v1';

const empty = (): ProgressState => ({ version: 1, completed: {}, drafts: {}, customTests: {} });

let cached: ProgressState = load();
const listeners = new Set<() => void>();

function load(): ProgressState {
  try {
    const raw = localStorage.getItem(KEY);
    if (raw === null) return empty();
    const parsed = JSON.parse(raw) as ProgressState;
    if (parsed.version !== 1) return empty();
    return { ...empty(), ...parsed };
  } catch {
    return empty();
  }
}

function save(next: ProgressState): void {
  cached = next;
  try {
    localStorage.setItem(KEY, JSON.stringify(next));
  } catch {
    // Storage may be unavailable (private mode); the session still works.
  }
  for (const l of listeners) l();
}

export const getProgress = (): ProgressState => cached;

export function subscribeProgress(listener: () => void): () => void {
  listeners.add(listener);
  return () => listeners.delete(listener);
}

export const useProgress = (): ProgressState =>
  useSyncExternalStore(subscribeProgress, getProgress, getProgress);

export function markCompleted(stepId: string): void {
  if (cached.completed[stepId] !== undefined) return;
  save({ ...cached, completed: { ...cached.completed, [stepId]: new Date().toISOString() } });
}

export function saveDraft(challengeId: string, code: string): void {
  save({ ...cached, drafts: { ...cached.drafts, [challengeId]: code } });
}

export function clearDraft(challengeId: string): void {
  const drafts = { ...cached.drafts };
  delete drafts[challengeId];
  save({ ...cached, drafts });
}

export function setCustomTests(challengeId: string, tests: CustomTestSpec[]): void {
  save({ ...cached, customTests: { ...cached.customTests, [challengeId]: tests } });
}

/** Test-only: drop state and re-read storage. */
export function resetProgressForTests(): void {
  try {
    localStorage.removeItem(KEY);
  } catch {
    // jsdom always has localStorage; guard for symmetry.
  }
  cached = load();
  for (const l of listeners) l();
}
