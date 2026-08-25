import type { Expr } from './types';
import { evaluateExpr } from './evaluate';

/**
 * Reference constant folder for Expr; the spec the `expr-fold` challenge is
 * graded against.
 *
 * Folds bottom-up, and only when the result is a finite number: `1 / 0`
 * stays a division rather than becoming a `Num` holding Infinity, which no
 * source text could round-trip. Variables and everything touching them are
 * left intact.
 */
export function foldExpr(ast: Expr): Expr {
  switch (ast.type) {
    case 'Num':
    case 'Var':
      return ast;
    case 'Unary': {
      const operand = foldExpr(ast.operand);
      if (operand.type === 'Num') {
        const value = -operand.value;
        if (Number.isFinite(value)) return { type: 'Num', value };
      }
      return { type: 'Unary', op: ast.op, operand };
    }
    case 'Binary': {
      const left = foldExpr(ast.left);
      const right = foldExpr(ast.right);
      if (left.type === 'Num' && right.type === 'Num') {
        const value = evaluateExpr({ type: 'Binary', op: ast.op, left, right }, {});
        if (Number.isFinite(value)) return { type: 'Num', value };
      }
      return { type: 'Binary', op: ast.op, left, right };
    }
  }
}
