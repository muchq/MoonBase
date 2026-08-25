/**
 * The auto-grader's contract. The grader is a pure engine: it knows how to
 * compile a submission, run a test bank against it, and explain failures.
 * Everything course-specific lives in the challenge definitions.
 */

/**
 * Submissions are JavaScript today. The field exists so adding TypeScript
 * (a transpile step in `compile`) or a functional language later is a new
 * case, not a reshape.
 */
export type Language = 'javascript';

export interface Submission {
  language: Language;
  code: string;
}

export interface CheckContext {
  /** The arguments the entry function was called with (already cloned). */
  args: unknown[];
}

export interface CheckResult {
  pass: boolean;
  /** Failure explanation; the most important sentence the user reads. */
  message?: string;
  /** Display overrides for the expected/actual panes. */
  expectedText?: string;
  actualText?: string;
}

/**
 * Semantic grading hook: judges the actual value directly (round-trip
 * through the reference parser, plan equivalence + cost budget, ...) when
 * deep-equality against a single expected value is the wrong contract.
 */
export type CheckFn = (actual: unknown, ctx: CheckContext) => CheckResult;

export interface TestSpec {
  /** Short, descriptive, named after the property it pins. */
  name: string;
  args: unknown[];
  /**
   * Literal expected value. When omitted, the grader computes it by running
   * the reference solution on the same args (the oracle) — which is also
   * how user-added custom tests are graded.
   */
  expected?: unknown;
  /** Error-expectation test: the entry must throw, and the message must contain this. */
  throws?: { messageIncludes: string };
  /** Semantic check overriding structural grading for this test. */
  check?: CheckFn;
  /** Shown on failure: why submissions usually get this one wrong. */
  hint?: string;
}

/** A user-authored test: a JS expression list evaluated into challenge inputs. */
export interface CustomTestSpec {
  id: string;
  name: string;
  /** e.g. `"1 + 2 * x", { x: 3 }` — evaluated as `[<source>]`. */
  source: string;
}

/** What the grader needs to know about a challenge; the curriculum adds pedagogy on top. */
export interface GradableChallenge {
  id: string;
  /** Name of the function the submission must define. */
  entry: string;
  /**
   * Provided code compiled ahead of the submission (and of the solution):
   * layers from earlier challenges the user should not have to re-paste,
   * e.g. the expression parser inside the SELECT parser challenge.
   */
  prelude?: string;
  /** Reference solution source; the grading oracle and the revealable answer. */
  solution: string;
  tests: TestSpec[];
  /** Challenge-wide semantic check, applied where a test doesn't bring its own. */
  check?: CheckFn;
  /**
   * Custom-test adapter: maps the values a user typed into entry args.
   * Null disables custom tests for the challenge.
   */
  custom: {
    /** What to type, e.g. "an AstQL query in quotes". */
    describe: string;
    placeholder: string;
    toArgs: (values: unknown[]) => unknown[];
  } | null;
  /** Whole-run budget enforced by the worker client; default 5000. */
  timeoutMs?: number;
}

export type TestStatus = 'pass' | 'fail' | 'error' | 'timeout' | 'skipped';

export interface TestResult {
  id: string;
  name: string;
  custom: boolean;
  status: TestStatus;
  /** Failure or error explanation, already line-mapped for runtime errors. */
  message?: string;
  expectedText?: string;
  actualText?: string;
  /** Path to the first structural mismatch, e.g. `[3].kind` or `.from.alias`. */
  diffPath?: string;
  hint?: string;
  /** console output captured while this test ran. */
  logs: string[];
}

export interface GradeReport {
  challengeId: string;
  status: 'pass' | 'fail' | 'error' | 'timeout';
  /** Present when the submission failed to compile (syntax error, missing entry). */
  compileError?: string;
  tests: TestResult[];
}

/** Worker protocol. */
export type GradeRequest = {
  type: 'grade';
  requestId: number;
  challengeId: string;
  submission: Submission;
  customTests: CustomTestSpec[];
};

export type GradeEvent =
  | { type: 'test-start'; requestId: number; id: string; name: string }
  | { type: 'test-result'; requestId: number; result: TestResult }
  | { type: 'done'; requestId: number; report: GradeReport }
  | { type: 'fatal'; requestId: number; error: string };
