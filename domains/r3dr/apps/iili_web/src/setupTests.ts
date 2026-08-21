import '@testing-library/jest-dom/vitest';
import { afterEach, beforeEach, vi } from 'vitest';
import { cleanup } from '@testing-library/react';

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
