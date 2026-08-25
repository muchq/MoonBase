import { describe, expect, it } from 'vitest';
import { challengeById } from '../curriculum/registry';
import { gradeSubmission } from '../grader/harness';
import { formatSelect, formatSelectFlat, parseSelect, tokenizeSql } from '../lang/sql';
import { plainNumber } from '../lang/sql/format';

/**
 * The formatter track grades by exact match, so its checkers' layered
 * failure diagnoses ARE the debugging support — each layer is an observable
 * behavior a learner relies on, pinned here through the same challenge-level
 * check functions that grade custom tests.
 */

const printChallenge = challengeById('sql-expr-print')!;
const formatChallenge = challengeById('sql-format')!;
const parseSql = (q: string) => parseSelect(tokenizeSql(q));

const checkPrint = (actual: unknown, source: string) =>
  printChallenge.check!(actual, { args: printChallenge.custom!.toArgs([source]) });

describe('sql-expr-print diagnostics', () => {
  it('rejects a non-string with the expected text alongside', () => {
    const r = checkPrint(42, 'a AND b');
    expect(r.pass).toBe(false);
    expect(r.message).toMatch(/Expected a string/);
    expect(r.expectedText).toBe('a AND b');
  });

  it('surfaces the parser error when the output does not lex or parse', () => {
    const r = checkPrint('AND AND', '(a OR b) AND c');
    expect(r.pass).toBe(false);
    expect(r.message).toMatch(/does not parse as an AstQL expression: .+/);
  });

  it('diagnoses extra parens as right tree, wrong style', () => {
    const r = checkPrint('((a OR b) AND c)', '(a OR b) AND c');
    expect(r.pass).toBe(false);
    expect(r.message).toMatch(/right tree but the style is off/);
    expect(r.expectedText).toBe('(a OR b) AND c');
    expect(r.actualText).toBe('((a OR b) AND c)');
  });

  it('diagnoses missing parens as a different tree, with the first-difference path', () => {
    const r = checkPrint('a OR b AND c', '(a OR b) AND c');
    expect(r.pass).toBe(false);
    expect(r.message).toMatch(/different tree/);
    expect(r.message).toMatch(/first difference at result/);
  });
});

const Q =
  "SELECT u.name, o.total FROM users u JOIN orders o ON u.id = o.user_id WHERE u.city = 'seattle' AND o.year = 2024";

const checkFormat = (actual: unknown, q: string, width: number) =>
  formatChallenge.check!(actual, { args: formatChallenge.custom!.toArgs([q, width]) });

describe('sql-format diagnostics', () => {
  it('rejects a non-string, showing the reference layout as expected text', () => {
    const r = checkFormat(null, Q, 40);
    expect(r.pass).toBe(false);
    expect(r.message).toMatch(/Expected a string/);
    expect(r.expectedText).toBe(formatSelect(parseSql(Q), 40));
  });

  it('surfaces the parser error when the output does not reparse', () => {
    const r = checkFormat('SELECT FROM', Q, 40);
    expect(r.pass).toBe(false);
    expect(r.message).toMatch(/does not reparse as AstQL \(.+\)/);
  });

  it('diagnoses a wrong query before layout, with the first-difference path', () => {
    const withoutWhere = 'SELECT u.name, o.total FROM users u JOIN orders o ON u.id = o.user_id';
    const r = checkFormat(formatSelect(parseSql(withoutWhere), 40), Q, 40);
    expect(r.pass).toBe(false);
    expect(r.message).toMatch(/reparses to a different query \(first difference at result/);
  });

  it('diagnoses a layout difference by first differing line, without a width callout when the line fits', () => {
    const misindented = formatSelect(parseSql(Q), 40).replace('  AND', '    AND');
    const r = checkFormat(misindented, Q, 40);
    expect(r.pass).toBe(false);
    expect(r.message).toMatch(/Same query, different layout: first difference at line \d+/);
    expect(r.message).not.toMatch(/against a width/);
  });

  it('calls out the width when the first differing line overflows it', () => {
    const r = checkFormat(formatSelectFlat(parseSql(Q)), Q, 40);
    expect(r.pass).toBe(false);
    expect(r.message).toMatch(/Same query, different layout: first difference at line 1/);
    expect(r.message).toMatch(/chars against a width of 40\./);
  });
});

describe('sql-format custom-test adapter', () => {
  it('defaults the width to 40 when omitted or not a number', () => {
    expect(formatChallenge.custom!.toArgs(['SELECT name FROM users'])[1]).toBe(40);
    expect(formatChallenge.custom!.toArgs(['SELECT name FROM users', '60'])[1]).toBe(40);
    expect(formatChallenge.custom!.toArgs(['SELECT name FROM users', 60])[1]).toBe(60);
  });
});

describe('expr-print diagnostics', () => {
  it('surfaces the parser error when the output does not parse as an Expr', () => {
    const exprPrint = challengeById('expr-print')!;
    const r = exprPrint.check!('1 +', { args: exprPrint.custom!.toArgs(['x + 1']) });
    expect(r.pass).toBe(false);
    expect(r.message).toMatch(/does not parse as an Expr: .+/);
  });
});

describe('plainNumber', () => {
  it.each([
    [0.0000001, '0.0000001'],
    [1.5e-7, '0.00000015'],
    [-0.0000001, '-0.0000001'],
    [1e21, '1000000000000000000000'],
    [1.2345e22, '12345000000000000000000'],
    [123.45, '123.45'],
    [0, '0'],
  ])('expands %s to %s, losslessly', (v, s) => {
    expect(plainNumber(v)).toBe(s);
    expect(Number(s)).toBe(v);
  });
});

describe('checker results through the grader', () => {
  it('a check-based failure carries message, expected, and actual into the report', () => {
    const report = gradeSubmission(printChallenge, {
      language: 'javascript',
      code: 'function printExpr(e) { return "x"; }',
    });
    expect(report.status).toBe('fail');
    const first = report.tests[0];
    expect(first.status).toBe('fail');
    expect(first.message).toMatch(/different tree/);
    expect(first.expectedText).toBe('a = 1 AND b = 2 OR c = 3');
    expect(first.actualText).toBe('x');
  });
});
