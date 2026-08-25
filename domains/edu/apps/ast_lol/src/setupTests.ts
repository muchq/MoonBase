import '@testing-library/jest-dom/vitest';
import { cleanup } from '@testing-library/react';
import { afterEach } from 'vitest';

// Without vitest globals, testing-library cannot register its own
// auto-cleanup; an accumulated DOM makes every getBy* ambiguous.
afterEach(cleanup);
