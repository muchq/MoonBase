import { useSyncExternalStore } from 'react';
import type { CustomTestSpec } from '../grader/types';

/**
 * All learner state lives in localStorage: which steps are done, code
 * drafts, and user-authored custom tests. Fully client-side, like the
 * grader itself — clearing site data resets the course.
 */

export interface ProgressState {
  version: 1;
  /** Step id → ISO timestamp. Lessons complete after checks; challenges on a green run. */
  completed: Record<string, string>;
  visited: Record<string, string>;
  read: Record<string, string>;
  practiced: Record<string, string>;
  studied: Record<string, string>;
  track: 'engine' | 'formatter';
  theme: 'system' | 'light' | 'dark';
  /** Challenge id → editor draft. */
  drafts: Record<string, string>;
  /** Challenge id → user-authored tests. */
  customTests: Record<string, CustomTestSpec[]>;
}

const KEY = 'astlol:v1';

const empty = (): ProgressState => ({
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
});

let cached: ProgressState = load();
const listeners = new Set<() => void>();

function load(): ProgressState {
  try {
    const raw = localStorage.getItem(KEY);
    if (raw === null) return empty();
    const parsed = JSON.parse(raw) as ProgressState;
    if (parsed.version !== 1) return empty();
    return {
      ...empty(),
      ...parsed,
      track: parsed.track === 'formatter' ? 'formatter' : 'engine',
      theme: parsed.theme === 'light' || parsed.theme === 'dark' ? parsed.theme : 'system',
    };
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

function mark(field: 'visited' | 'read' | 'practiced' | 'studied', id: string): void {
  if (cached[field][id] !== undefined) return;
  save({ ...cached, [field]: { ...cached[field], [id]: new Date().toISOString() } });
}
export const markVisited = (id: string): void => mark('visited', id);
export const markRead = (id: string): void => mark('read', id);
export const markPracticed = (id: string): void => mark('practiced', id);
export const markStudied = (id: string): void => mark('studied', id);
export const setTrack = (track: ProgressState['track']): void => save({ ...cached, track });
export const setTheme = (theme: ProgressState['theme']): void => save({ ...cached, theme });

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
