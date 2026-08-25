/**
 * Expr — the course's toy arithmetic language. The shapes here are the
 * public contract of every Tier 1–2 challenge: challenge statements quote
 * them and the grader deep-equals against them, so a change here is a
 * curriculum change, not a refactor.
 */

export type ExprTokenKind = 'number' | 'ident' | 'op' | 'lparen' | 'rparen';

export interface ExprToken {
  kind: ExprTokenKind;
  text: string;
  /** Index of the first character of the lexeme in the source string. */
  pos: number;
}

export type BinaryOp = '+' | '-' | '*' | '/' | '^';

export type Expr =
  | { type: 'Num'; value: number }
  | { type: 'Var'; name: string }
  | { type: 'Unary'; op: '-'; operand: Expr }
  | { type: 'Binary'; op: BinaryOp; left: Expr; right: Expr };

export type Env = Record<string, number>;
