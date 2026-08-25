import { beforeEach, describe, expect, it, vi } from 'vitest';
import { render, screen, waitFor, within } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { MemoryRouter } from 'react-router';
import App from '../App';
import { challengeById, challenges, lessons, tiers } from '../curriculum/registry';
import type { GradeReport } from '../grader/types';
import { getProgress, markCompleted, resetProgressForTests } from '../state/progress';

// CodeMirror needs real layout APIs jsdom lacks; a textarea keeps the
// view's contract (value/onChange/onRun) testable.
vi.mock('../components/CodeEditor', () => ({
  default: ({ value, onChange }: { value: string; onChange: (v: string) => void }) => (
    <textarea
      aria-label="code editor"
      value={value}
      onChange={(e) => onChange(e.target.value)}
    />
  ),
}));

vi.mock('../grader/client', () => ({
  runGrader: vi.fn(),
}));

import { runGrader } from '../grader/client';

const mockRunGrader = vi.mocked(runGrader);

const at = (path: string) =>
  render(
    <MemoryRouter initialEntries={[path]}>
      <App />
    </MemoryRouter>,
  );

beforeEach(() => resetProgressForTests());

describe('HomeView', () => {
  it('renders every tier with its steps', () => {
    at('/');
    for (const tier of tiers) {
      expect(screen.getByText(tier.title)).toBeInTheDocument();
    }
    for (const lesson of lessons) {
      expect(screen.getAllByText(lesson.title).length).toBeGreaterThanOrEqual(1);
    }
    for (const challenge of challenges) {
      expect(screen.getAllByText(challenge.title).length).toBeGreaterThanOrEqual(1);
    }
  });

  it('marks completed steps and moves the continue chip past them', () => {
    markCompleted('welcome');
    at('/');
    expect(screen.getAllByLabelText('completed').length).toBe(1);
    // The chip must point at the first *incomplete* step, not the one just done.
    const chip = screen.getByText('continue');
    expect(chip.closest('a')).toHaveAttribute('href', '/lesson/tokens');
  });

  it('shows solved count in the header', () => {
    markCompleted('expr-tokenize');
    at('/');
    expect(screen.getByText(`1/${challenges.length} solved`)).toBeInTheDocument();
  });
});

describe('LessonView', () => {
  it('renders the document, further reading, and completes the lesson on visit', () => {
    at('/lesson/tokens');
    expect(screen.getByRole('heading', { name: 'Tokenization' })).toBeInTheDocument();
    expect(screen.getByRole('link', { name: /Crafting Interpreters — Scanning/ })).toHaveAttribute(
      'href',
      'https://craftinginterpreters.com/scanning.html',
    );
    expect(getProgress().completed['tokens']).toBeDefined();
  });

  it('redirects an unknown lesson id home', () => {
    at('/lesson/nope');
    expect(screen.getByText('Abstract syntax trees, for working programmers')).toBeInTheDocument();
  });
});

describe('ChallengeView', () => {
  const passReport = (id: string): GradeReport => ({
    challengeId: id,
    status: 'pass',
    tests: [{ id: 'builtin-0', name: 'a single number', custom: false, status: 'pass', logs: [] }],
  });

  it('renders statement, signature, and starter code', () => {
    at('/challenge/expr-tokenize');
    expect(screen.getByRole('heading', { name: 'Write a tokenizer' })).toBeInTheDocument();
    expect(screen.getByText('tokenize(source) → Token[]')).toBeInTheDocument();
    const editor = screen.getByLabelText('code editor') as HTMLTextAreaElement;
    expect(editor.value).toContain('function tokenize(source)');
  });

  it('runs the grader and records completion on a green run', async () => {
    mockRunGrader.mockResolvedValueOnce(passReport('expr-tokenize'));
    const user = userEvent.setup();
    at('/challenge/expr-tokenize');
    await user.click(screen.getByRole('button', { name: 'Run tests' }));
    await waitFor(() => expect(screen.getByText(/Solved — 1\/1 tests pass/)).toBeInTheDocument());
    expect(getProgress().completed['expr-tokenize']).toBeDefined();
    const [id, submission, customTests, planned] = mockRunGrader.mock.calls[0];
    expect(id).toBe('expr-tokenize');
    expect(submission.language).toBe('javascript');
    expect(customTests).toEqual([]);
    expect(planned.length).toBe(challengeById('expr-tokenize')!.tests.length);
  });

  it('shows failure details: message, hint, expected/actual, logs', async () => {
    mockRunGrader.mockResolvedValueOnce({
      challengeId: 'expr-tokenize',
      status: 'fail',
      tests: [
        {
          id: 'builtin-1',
          name: 'maximal munch: a multi-digit number is one token',
          custom: false,
          status: 'fail',
          message: 'First difference at result[0].text: values differ — expected "123", got "1".',
          hint: 'Consume digits greedily in a loop before emitting the token — one token per digit is the classic first bug.',
          expectedText: '"123"',
          actualText: '"1"',
          logs: ['saw 1'],
        },
      ],
    });
    const user = userEvent.setup();
    at('/challenge/expr-tokenize');
    await user.click(screen.getByRole('button', { name: 'Run tests' }));
    await waitFor(() => expect(screen.getByText(/0\/1 tests pass/)).toBeInTheDocument());
    expect(screen.getByText(/First difference at result\[0\]\.text/)).toBeInTheDocument();
    expect(screen.getByText(/Consume digits greedily/)).toBeInTheDocument();
    expect(screen.getByText('expected')).toBeInTheDocument();
    expect(screen.getByText('saw 1')).toBeInTheDocument();
  });

  it('adds a custom test, persists it, and sends it to the grader', async () => {
    mockRunGrader.mockResolvedValueOnce(passReport('expr-tokenize'));
    const user = userEvent.setup();
    at('/challenge/expr-tokenize');
    await user.type(screen.getByLabelText('new test input'), '"7 * seven"');
    await user.click(screen.getByRole('button', { name: 'Add test' }));
    expect(getProgress().customTests['expr-tokenize']).toHaveLength(1);
    await user.click(screen.getByRole('button', { name: 'Run tests' }));
    await waitFor(() => expect(mockRunGrader).toHaveBeenCalled());
    const [, , customTests] = mockRunGrader.mock.calls[0];
    expect(customTests).toHaveLength(1);
    expect(customTests[0].source).toBe('"7 * seven"');
  });

  it('reveals the reference solution and can load it into the editor', async () => {
    const user = userEvent.setup();
    at('/challenge/expr-tokenize');
    await user.click(screen.getByText(/Reference solution/));
    const load = await screen.findByRole('button', { name: 'Load into editor' });
    await user.click(load);
    const editor = screen.getByLabelText('code editor') as HTMLTextAreaElement;
    expect(editor.value).toBe(challengeById('expr-tokenize')!.solution);
  });

  it('shows provided code for challenges with a prelude', () => {
    at('/challenge/sql-select-parse');
    const provided = screen.getByText(/Provided code/);
    expect(provided).toBeInTheDocument();
    expect(screen.getByText(/function parseExprFrom/)).toBeInTheDocument();
  });

  it('resets to starter and clears the draft', async () => {
    const user = userEvent.setup();
    at('/challenge/expr-eval');
    const editor = screen.getByLabelText('code editor') as HTMLTextAreaElement;
    await user.clear(editor);
    await user.type(editor, 'function evaluate() {{ return 1; }}');
    await user.click(screen.getByRole('button', { name: 'Reset' }));
    expect((screen.getByLabelText('code editor') as HTMLTextAreaElement).value).toBe(
      challengeById('expr-eval')!.starter,
    );
    expect(getProgress().drafts['expr-eval']).toBeUndefined();
  });
});
