import type { Expr, ExprToken } from './types';

/**
 * Reference recursive-descent parser for Expr; the spec the `expr-parse`
 * challenge is graded against.
 *
 * Precedence, loosest to tightest: `+ -`, `* /` (left-associative), unary
 * `-`, `^` (right-associative). Unary minus binds looser than `^`, so
 * `-2 ^ 2` is `-(2 ^ 2)` — the mathematical convention.
 */
export function parseExpr(tokens: ExprToken[]): Expr {
  let i = 0;

  const peek = () => tokens[i];
  const next = () => tokens[i++];
  const atOp = (op: string) => {
    const t = peek();
    return t !== undefined && t.kind === 'op' && t.text === op;
  };

  function fail(t: ExprToken | undefined): never {
    if (t === undefined) throw new Error('Unexpected end of input');
    throw new Error(`Unexpected token '${t.text}' at ${t.pos}`);
  }

  function additive(): Expr {
    let left = multiplicative();
    while (atOp('+') || atOp('-')) {
      const op = next().text as '+' | '-';
      left = { type: 'Binary', op, left, right: multiplicative() };
    }
    return left;
  }

  function multiplicative(): Expr {
    let left = unary();
    while (atOp('*') || atOp('/')) {
      const op = next().text as '*' | '/';
      left = { type: 'Binary', op, left, right: unary() };
    }
    return left;
  }

  function unary(): Expr {
    if (atOp('-')) {
      next();
      return { type: 'Unary', op: '-', operand: unary() };
    }
    return power();
  }

  function power(): Expr {
    const base = atom();
    if (atOp('^')) {
      next();
      // Right operand re-enters unary so `2 ^ -3` parses and `a ^ b ^ c`
      // associates to the right.
      return { type: 'Binary', op: '^', left: base, right: unary() };
    }
    return base;
  }

  function atom(): Expr {
    const t = next();
    if (t === undefined) fail(t);
    if (t.kind === 'number') return { type: 'Num', value: Number(t.text) };
    if (t.kind === 'ident') return { type: 'Var', name: t.text };
    if (t.kind === 'lparen') {
      const inner = additive();
      const close = next();
      if (close === undefined || close.kind !== 'rparen') fail(close);
      return inner;
    }
    fail(t);
  }

  const result = additive();
  if (i < tokens.length) fail(tokens[i]);
  return result;
}
