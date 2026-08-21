import '@testing-library/jest-dom/vitest';
import { afterEach, beforeEach, vi } from 'vitest';
import { cleanup } from '@testing-library/react';

// Node 25+ exposes an experimental localStorage that is undefined without
// --localstorage-file and shadows jsdom's Storage. CI runs Node 24; local
// Node 26 otherwise reds every storage test. Install a real in-memory
// Storage when the global is unusable.
if (typeof globalThis.localStorage?.clear !== 'function') {
  const data = new Map<string, string>();
  const memory: Storage = {
    get length() {
      return data.size;
    },
    clear() {
      data.clear();
    },
    getItem(key) {
      return data.has(key) ? data.get(key)! : null;
    },
    key(index) {
      return [...data.keys()][index] ?? null;
    },
    removeItem(key) {
      data.delete(key);
    },
    setItem(key, value) {
      data.set(String(key), String(value));
    },
  };
  Reflect.deleteProperty(globalThis, 'localStorage');
  Object.defineProperty(globalThis, 'localStorage', {
    configurable: true,
    writable: true,
    value: memory,
  });
  if (typeof window !== 'undefined') {
    Object.defineProperty(window, 'localStorage', {
      configurable: true,
      writable: true,
      value: memory,
    });
  }
}

// A test that reaches fetch forgot to arrange a response; fail loud rather
// than let CI make a live API call. api.test stubs over this per test.
beforeEach(() => {
  globalThis.fetch = (() => {
    throw new Error('unexpected network call — arrange a response');
  }) as unknown as typeof fetch;
});

afterEach(() => {
  cleanup();
  vi.useRealTimers();
});
