import type { Env, Expr } from './types';

/** Reference tree-walking evaluator for Expr; the spec the `expr-eval` challenge is graded against. */
export function evaluateExpr(ast: Expr, env: Env): number {
  switch (ast.type) {
    case 'Num':
      return ast.value;
    case 'Var':
      if (!Object.prototype.hasOwnProperty.call(env, ast.name)) {
        throw new Error(`Unknown variable '${ast.name}'`);
      }
      return env[ast.name];
    case 'Unary':
      return -evaluateExpr(ast.operand, env);
    case 'Binary': {
      const l = evaluateExpr(ast.left, env);
      const r = evaluateExpr(ast.right, env);
      switch (ast.op) {
        case '+':
          return l + r;
        case '-':
          return l - r;
        case '*':
          return l * r;
        case '/':
          return l / r;
        case '^':
          return l ** r;
      }
    }
  }
}
