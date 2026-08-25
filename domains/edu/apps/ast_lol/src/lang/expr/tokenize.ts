import type { ExprToken } from './types';

const isDigit = (c: string) => c >= '0' && c <= '9';
const isIdentStart = (c: string) => /[A-Za-z_]/.test(c);
const isIdentPart = (c: string) => /[A-Za-z0-9_]/.test(c);

/** Reference tokenizer for Expr; the spec the `expr-tokenize` challenge is graded against. */
export function tokenizeExpr(source: string): ExprToken[] {
  const tokens: ExprToken[] = [];
  let i = 0;
  while (i < source.length) {
    const c = source[i];
    if (c === ' ' || c === '\t' || c === '\n' || c === '\r') {
      i++;
      continue;
    }
    if (isDigit(c)) {
      const start = i;
      while (i < source.length && isDigit(source[i])) i++;
      if (source[i] === '.' && isDigit(source[i + 1])) {
        i++;
        while (i < source.length && isDigit(source[i])) i++;
      }
      tokens.push({ kind: 'number', text: source.slice(start, i), pos: start });
      continue;
    }
    if (isIdentStart(c)) {
      const start = i;
      while (i < source.length && isIdentPart(source[i])) i++;
      tokens.push({ kind: 'ident', text: source.slice(start, i), pos: start });
      continue;
    }
    if (c === '+' || c === '-' || c === '*' || c === '/' || c === '^') {
      tokens.push({ kind: 'op', text: c, pos: i });
      i++;
      continue;
    }
    if (c === '(') {
      tokens.push({ kind: 'lparen', text: c, pos: i });
      i++;
      continue;
    }
    if (c === ')') {
      tokens.push({ kind: 'rparen', text: c, pos: i });
      i++;
      continue;
    }
    throw new Error(`Unexpected character '${c}' at ${i}`);
  }
  return tokens;
}
