import type { SqlToken } from './types';
import { SQL_KEYWORDS } from './types';

const KEYWORDS = new Set<string>(SQL_KEYWORDS);

const isDigit = (c: string) => c >= '0' && c <= '9';
const isIdentStart = (c: string) => /[A-Za-z_]/.test(c);
const isIdentPart = (c: string) => /[A-Za-z0-9_]/.test(c);

/** Reference tokenizer for AstQL; the spec the `sql-tokenize` challenge is graded against. */
export function tokenizeSql(source: string): SqlToken[] {
  const tokens: SqlToken[] = [];
  let i = 0;
  while (i < source.length) {
    const c = source[i];
    if (c === ' ' || c === '\t' || c === '\n' || c === '\r') {
      i++;
      continue;
    }
    // -- comment runs to end of line.
    if (c === '-' && source[i + 1] === '-') {
      while (i < source.length && source[i] !== '\n') i++;
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
      const word = source.slice(start, i);
      const upper = word.toUpperCase();
      if (KEYWORDS.has(upper)) {
        tokens.push({ kind: 'keyword', text: upper, pos: start });
      } else {
        tokens.push({ kind: 'ident', text: word.toLowerCase(), pos: start });
      }
      continue;
    }
    if (c === "'") {
      const start = i;
      i++;
      let value = '';
      for (;;) {
        if (i >= source.length) {
          throw new Error(`Unterminated string starting at ${start}`);
        }
        if (source[i] === "'") {
          // '' inside a string is an escaped quote.
          if (source[i + 1] === "'") {
            value += "'";
            i += 2;
            continue;
          }
          i++;
          break;
        }
        value += source[i];
        i++;
      }
      tokens.push({ kind: 'string', text: source.slice(start, i), pos: start, value });
      continue;
    }
    // Two-character operators before one-character ones: maximal munch.
    const two = source.slice(i, i + 2);
    if (two === '<>' || two === '<=' || two === '>=') {
      tokens.push({ kind: 'op', text: two, pos: i });
      i += 2;
      continue;
    }
    if ('=<>+-*/'.includes(c)) {
      tokens.push({ kind: 'op', text: c, pos: i });
      i++;
      continue;
    }
    if (c === ',' || c === '(' || c === ')' || c === '.') {
      tokens.push({ kind: 'punct', text: c, pos: i });
      i++;
      continue;
    }
    throw new Error(`Unexpected character '${c}' at ${i}`);
  }
  return tokens;
}
