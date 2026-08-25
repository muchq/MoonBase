import type { Expr } from './types';

/** Reference variable collector for Expr; the spec the `expr-vars` challenge is graded against. */
export function collectVars(ast: Expr): string[] {
  const names = new Set<string>();
  const visit = (e: Expr): void => {
    switch (e.type) {
      case 'Num':
        return;
      case 'Var':
        names.add(e.name);
        return;
      case 'Unary':
        visit(e.operand);
        return;
      case 'Binary':
        visit(e.left);
        visit(e.right);
        return;
    }
  };
  visit(ast);
  return [...names].sort();
}
