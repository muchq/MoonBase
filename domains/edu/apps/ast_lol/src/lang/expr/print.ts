import type { Expr } from './types';

/**
 * Reference precedence-aware printer for Expr; the spec the `expr-print`
 * challenge is graded against.
 *
 * Emits the fewest parentheses that reparse to the same tree: a child is
 * wrapped when its precedence is lower than its parent's, or equal on the
 * side the parent does not associate to. Binary operators get single spaces;
 * unary minus is tight against its operand.
 */

// Unary minus sits between `* /` and `^`: `-a * b` reparses as `(-a) * b`
// but `-a ^ b` reparses as `-(a ^ b)`.
const PREC: Record<string, number> = { '+': 1, '-': 1, '*': 2, '/': 2, '^': 4 };
const UNARY_PREC = 3;
const ATOM_PREC = 5;

function prec(e: Expr): number {
  switch (e.type) {
    case 'Binary':
      return PREC[e.op];
    case 'Unary':
      return UNARY_PREC;
    default:
      return ATOM_PREC;
  }
}

function wrap(child: Expr, minPrec: number): string {
  const s = printExpr(child);
  return prec(child) < minPrec ? `(${s})` : s;
}

export function printExpr(ast: Expr): string {
  switch (ast.type) {
    case 'Num':
      return String(ast.value);
    case 'Var':
      return ast.name;
    case 'Unary':
      return `-${wrap(ast.operand, UNARY_PREC)}`;
    case 'Binary': {
      const p = PREC[ast.op];
      // `^` associates right: equal precedence needs parens on the left,
      // and its right operand re-enters unary in the grammar, so anything
      // at unary strength or above stays bare there (`2 ^ -3`). Everything
      // else associates left: equal precedence needs parens on the right.
      const leftMin = ast.op === '^' ? p + 1 : p;
      const rightMin = ast.op === '^' ? UNARY_PREC : p + 1;
      return `${wrap(ast.left, leftMin)} ${ast.op} ${wrap(ast.right, rightMin)}`;
    }
  }
}
