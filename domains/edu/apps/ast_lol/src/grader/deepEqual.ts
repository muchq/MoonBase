/**
 * Structural comparison with debugging-quality reporting: not just "equal or
 * not" but *where* two values first disagree, as a path a user can follow
 * into their own output.
 */

export interface Diff {
  /** e.g. `[3].kind`, `.from.alias`, or '' for the root. */
  path: string;
  expected: unknown;
  actual: unknown;
  /** One-line classification used to phrase the failure message. */
  reason: string;
}

const isPlainObject = (v: unknown): v is Record<string, unknown> =>
  typeof v === 'object' && v !== null && !Array.isArray(v);

function typeName(v: unknown): string {
  if (v === null) return 'null';
  if (Array.isArray(v)) return 'array';
  return typeof v;
}

/**
 * First structural difference between expected and actual, or null when
 * deep-equal. Object key order is insignificant; a key holding undefined
 * and a missing key are different (that distinction bites in AST shapes:
 * `alias: null` is part of the contract, `alias: undefined` is a bug).
 */
export function firstDiff(expected: unknown, actual: unknown, path = ''): Diff | null {
  if (Object.is(expected, actual)) return null;

  if (typeName(expected) !== typeName(actual)) {
    return {
      path,
      expected,
      actual,
      reason: `expected ${typeName(expected)}, got ${typeName(actual)}`,
    };
  }

  if (Array.isArray(expected) && Array.isArray(actual)) {
    if (expected.length !== actual.length) {
      return {
        path,
        expected,
        actual,
        reason: `expected ${expected.length} element(s), got ${actual.length}`,
      };
    }
    for (let i = 0; i < expected.length; i++) {
      const diff = firstDiff(expected[i], actual[i], `${path}[${i}]`);
      if (diff !== null) return diff;
    }
    return null;
  }

  if (isPlainObject(expected) && isPlainObject(actual)) {
    for (const key of Object.keys(expected)) {
      if (!(key in actual)) {
        return {
          path: `${path}.${key}`,
          expected: expected[key],
          actual: undefined,
          reason: `missing key '${key}'`,
        };
      }
    }
    for (const key of Object.keys(actual)) {
      if (!(key in expected)) {
        return {
          path: `${path}.${key}`,
          expected: undefined,
          actual: actual[key],
          reason: `unexpected key '${key}'`,
        };
      }
    }
    for (const key of Object.keys(expected)) {
      const diff = firstDiff(expected[key], actual[key], `${path}.${key}`);
      if (diff !== null) return diff;
    }
    return null;
  }

  return { path, expected, actual, reason: 'values differ' };
}

export const deepEqual = (a: unknown, b: unknown): boolean => firstDiff(a, b) === null;

const LIMIT = 4000;

function raw(value: unknown, indent: number): string {
  if (value === undefined) return 'undefined';
  if (typeof value === 'number' && !Number.isFinite(value)) return String(value);
  if (typeof value === 'function') return '[function]';
  const json = JSON.stringify(
    value,
    (_k, v: unknown) => {
      if (v === undefined) return '«undefined»';
      if (typeof v === 'number' && !Number.isFinite(v)) return `«${String(v)}»`;
      if (typeof v === 'function') return '«function»';
      return v;
    },
    indent,
  );
  if (json === undefined) return String(value);
  return json.replaceAll('"«undefined»"', 'undefined').replaceAll(/"«(NaN|-?Infinity)»"/g, '$1');
}

/** Compact single-line rendering for messages and log lines. */
export function show(value: unknown): string {
  const s = raw(value, 0);
  return s.length > 200 ? `${s.slice(0, 200)}…` : s;
}

/** Multi-line rendering for the expected/actual panes. */
export function showPretty(value: unknown): string {
  const s = raw(value, 2);
  return s.length > LIMIT ? `${s.slice(0, LIMIT)}\n… (truncated)` : s;
}
