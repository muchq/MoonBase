import { parseSelect, parseSqlExpr, tokenizeSql } from '../lang/sql';
import type { ChallengeDef, LessonDef, Tier } from './types';

/** Tier 3 — Parsing AstQL: the course's SQL subset, tokens to statements. */

const sqlSubsetLesson: LessonDef = {
  id: 'sql-subset',
  title: 'AstQL: a SQL worth parsing',
  summary: 'The course subset — SELECT, JOIN … ON, WHERE, ORDER BY, LIMIT — pinned down exactly.',
  reading: [
    {
      title: 'SQLite — SELECT',
      url: 'https://sqlite.org/lang_select.html',
      note: 'a production grammar for the statement AstQL trims down',
    },
    {
      title: 'PostgreSQL — SELECT reference',
      url: 'https://www.postgresql.org/docs/current/sql-select.html',
      note: 'how much a full dialect carries beyond the subset',
    },
    {
      title: 'ANTLR grammars-v4 — SQL grammars',
      url: 'https://github.com/antlr/grammars-v4/tree/master/sql',
      note: 'open-source grammar files for real dialects',
    },
  ],
};

const prattLesson: LessonDef = {
  id: 'pratt',
  title: 'Pratt parsing',
  summary: 'Binding power instead of a function ladder — the tool for operator-heavy grammars.',
  reading: [
    {
      title: 'matklad — Simple but Powerful Pratt Parsing',
      url: 'https://matklad.github.io/2020/04/13/simple-but-powerful-pratt-parsing.html',
      note: 'the clearest modern treatment; rust-analyzer’s author',
    },
    {
      title: 'Crafting Interpreters — Compiling Expressions',
      url: 'https://craftinginterpreters.com/compiling-expressions.html',
      note: 'a Pratt parser in production shape (clox)',
    },
    {
      title: 'Wikipedia — Operator-precedence parser',
      url: 'https://en.wikipedia.org/wiki/Operator-precedence_parser',
    },
  ],
};

const clausesLesson: LessonDef = {
  id: 'clauses',
  title: 'Statements: clauses in a row',
  summary: 'Statement parsing is sequencing: each clause is a straight-line sub-parser.',
  reading: [
    {
      title: 'SQLite — syntax diagrams',
      url: 'https://sqlite.org/syntaxdiagrams.html',
      note: 'clause sequencing drawn as railroad diagrams',
    },
    {
      title: 'Crafting Interpreters — Statements and State',
      url: 'https://craftinginterpreters.com/statements-and-state.html',
      note: 'the same expression-parser-inside-statement-parser layering',
    },
  ],
};

const sqlTokenize: ChallengeDef = {
  id: 'sql-tokenize',
  title: 'Tokenize SQL',
  summary: 'Keywords, case folding, string escapes, comments, two-char operators.',
  signature: 'tokenizeSql(source) → Token[]',
  entry: 'tokenizeSql',
  difficulty: 2,
  starter: `// Token: { kind: 'keyword'|'ident'|'number'|'string'|'op'|'punct',
//          text: string, pos: number, value?: string }
// Keywords (case-insensitive in source, uppercase in text):
//   SELECT FROM WHERE AND OR NOT AS ORDER BY ASC DESC LIMIT
//   JOIN ON IS NULL TRUE FALSE
// Other identifiers fold to lowercase in text.
// Strings: '...' with '' as an escaped quote. text keeps the raw lexeme
//   (quotes included); value carries the decoded contents.
//   Unterminated: throw new Error('Unterminated string starting at ' + start)
// Numbers: digits, optionally '.' + digits.
// Ops: = <> < <= > >= + - * /   (maximal munch: <= is one token)
// Punct: , ( ) .
// Comments: -- to end of line. Whitespace separates.
// Anything else: throw new Error("Unexpected character '" + c + "' at " + i)
function tokenizeSql(source) {
  const tokens = [];
  // TODO
  return tokens;
}`,
  solution: `function tokenizeSql(source) {
  const KEYWORDS = new Set(['SELECT','FROM','WHERE','AND','OR','NOT','AS','ORDER','BY',
    'ASC','DESC','LIMIT','JOIN','ON','IS','NULL','TRUE','FALSE']);
  const isDigit = (c) => c >= '0' && c <= '9';
  const isIdentStart = (c) => /[A-Za-z_]/.test(c);
  const isIdentPart = (c) => /[A-Za-z0-9_]/.test(c);
  const tokens = [];
  let i = 0;
  while (i < source.length) {
    const c = source[i];
    if (c === ' ' || c === '\\t' || c === '\\n' || c === '\\r') { i++; continue; }
    if (c === '-' && source[i + 1] === '-') {
      while (i < source.length && source[i] !== '\\n') i++;
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
      if (KEYWORDS.has(upper)) tokens.push({ kind: 'keyword', text: upper, pos: start });
      else tokens.push({ kind: 'ident', text: word.toLowerCase(), pos: start });
      continue;
    }
    if (c === "'") {
      const start = i;
      i++;
      let value = '';
      for (;;) {
        if (i >= source.length) throw new Error('Unterminated string starting at ' + start);
        if (source[i] === "'") {
          if (source[i + 1] === "'") { value += "'"; i += 2; continue; }
          i++;
          break;
        }
        value += source[i];
        i++;
      }
      tokens.push({ kind: 'string', text: source.slice(start, i), pos: start, value: value });
      continue;
    }
    const two = source.slice(i, i + 2);
    if (two === '<>' || two === '<=' || two === '>=') {
      tokens.push({ kind: 'op', text: two, pos: i });
      i += 2;
      continue;
    }
    if ('=<>+-*/'.includes(c)) { tokens.push({ kind: 'op', text: c, pos: i }); i++; continue; }
    if (c === ',' || c === '(' || c === ')' || c === '.') {
      tokens.push({ kind: 'punct', text: c, pos: i });
      i++;
      continue;
    }
    throw new Error("Unexpected character '" + c + "' at " + i);
  }
  return tokens;
}`,
  tests: [
    {
      name: 'keywords canonicalize to uppercase, identifiers to lowercase',
      args: ['Select Name From USERS'],
      expected: tokenizeSql('Select Name From USERS'),
      hint: 'Scan the word first, then decide: uppercase it and check a keyword set; non-keywords fold to lowercase.',
    },
    {
      name: 'strings keep the raw lexeme in text and the decoded contents in value',
      args: ["'it''s'"],
      expected: [{ kind: 'string', text: "'it''s'", pos: 0, value: "it's" }],
      hint: "'' inside a string is one escaped quote. Peek one character ahead when you hit a quote.",
    },
    {
      name: 'an unterminated string reports where it started',
      args: ["name = 'oops"],
      throws: { messageIncludes: 'Unterminated string starting at 7' },
    },
    {
      name: 'maximal munch: <= and <> are single tokens',
      args: ['a<=b<>c'],
      expected: tokenizeSql('a<=b<>c'),
      hint: 'Try the two-character operators before the one-character ones.',
    },
    {
      name: '-- comments run to end of line',
      args: ['select -- everything\n *'],
      expected: tokenizeSql('select -- everything\n *'),
    },
    {
      name: 'a comment at end of input terminates',
      args: ['select 1 --done'],
      expected: tokenizeSql('select 1 --done'),
    },
    {
      name: 'minus is only a comment when doubled: 1 - -2',
      args: ['1 - -2'],
      expected: tokenizeSql('1 - -2'),
      hint: 'Check source[i + 1] before treating - as a comment starter.',
    },
    {
      name: 'but 1 --2 is a comment',
      args: ['1 --2'],
      expected: [{ kind: 'number', text: '1', pos: 0 }],
    },
    {
      name: 'punctuation: commas, parens, dots',
      args: ['(u.id, o.total)'],
      expected: tokenizeSql('(u.id, o.total)'),
    },
    {
      name: 'IS, NULL, TRUE, FALSE are keywords',
      args: ['total IS NOT NULL AND ok = TRUE'],
      expected: tokenizeSql('total IS NOT NULL AND ok = TRUE'),
    },
    {
      name: 'unexpected characters throw with their position',
      args: ['select ?'],
      throws: { messageIncludes: "'?' at 7" },
    },
  ],
  custom: {
    describe: 'A SQL source string; the reference tokenizer supplies the expected tokens.',
    placeholder: '"SELECT name FROM users WHERE city = \'seattle\'"',
    toArgs: (values) => [String(values[0])],
  },
};

const sqlExprParse: ChallengeDef = {
  id: 'sql-expr-parse',
  title: 'Pratt-parse SQL expressions',
  summary: 'One loop, one binding-power table — OR to unary minus, IS NULL included.',
  signature: 'parseExpr(tokens) → SqlExpr',
  entry: 'parseExpr',
  difficulty: 3,
  starter: `// AST:
//   { type: 'Column', table: string|null, name: string }   u.id, name
//   { type: 'Lit', value: number|string|boolean|null }
//   { type: 'Binary', op, left, right }
//        op: 'AND'|'OR'|'='|'<>'|'<'|'<='|'>'|'>='|'+'|'-'|'*'|'/'
//   { type: 'Not', operand }
//   { type: 'Unary', op: '-', operand }
//   { type: 'IsNull', operand, negated: boolean }          x IS [NOT] NULL
// Binding powers, loosest to tightest (all binary ops left-associative):
//   OR: 1   AND: 2   NOT: 3   = <> < <= > >= and IS [NOT] NULL: 4
//   + -: 5   * /: 6   unary -: 7
// NOT binds looser than comparison: NOT a = b  is  NOT (a = b).
// IS [NOT] NULL is postfix at comparison strength.
// Atoms: number/string literals, NULL/TRUE/FALSE, ident[.ident], ( expr ).
// Consume the whole stream; errors as in the tokenizer challenge:
//   throw new Error("Unexpected token '" + t.text + "' at " + t.pos)
//   throw new Error('Unexpected end of input')
function parseExpr(tokens) {
  let i = 0;
  // TODO: exprBp(minBp) — parse a prefix, then loop while the next
  // operator's binding power is >= minBp.
}`,
  solution: `function parseExpr(tokens) {
  const BP = { OR: 1, AND: 2, '=': 4, '<>': 4, '<': 4, '<=': 4, '>': 4, '>=': 4,
    '+': 5, '-': 5, '*': 6, '/': 6 };
  let i = 0;
  function fail(t) {
    if (t === undefined) throw new Error('Unexpected end of input');
    throw new Error("Unexpected token '" + t.text + "' at " + t.pos);
  }
  function atKeyword(word) {
    const t = tokens[i];
    return t !== undefined && t.kind === 'keyword' && t.text === word;
  }
  function atom() {
    const t = tokens[i++];
    if (t === undefined) fail(t);
    if (t.kind === 'number') return { type: 'Lit', value: Number(t.text) };
    if (t.kind === 'string') return { type: 'Lit', value: t.value };
    if (t.kind === 'keyword' && t.text === 'NULL') return { type: 'Lit', value: null };
    if (t.kind === 'keyword' && t.text === 'TRUE') return { type: 'Lit', value: true };
    if (t.kind === 'keyword' && t.text === 'FALSE') return { type: 'Lit', value: false };
    if (t.kind === 'ident') {
      const dot = tokens[i];
      if (dot !== undefined && dot.kind === 'punct' && dot.text === '.') {
        i++;
        const name = tokens[i++];
        if (name === undefined || name.kind !== 'ident') fail(name);
        return { type: 'Column', table: t.text, name: name.text };
      }
      return { type: 'Column', table: null, name: t.text };
    }
    if (t.kind === 'punct' && t.text === '(') {
      const inner = exprBp(1);
      const close = tokens[i++];
      if (close === undefined || !(close.kind === 'punct' && close.text === ')')) fail(close);
      return inner;
    }
    fail(t);
  }
  function prefix() {
    const t = tokens[i];
    if (t === undefined) fail(t);
    if (t.kind === 'keyword' && t.text === 'NOT') {
      i++;
      return { type: 'Not', operand: exprBp(4) };
    }
    if (t.kind === 'op' && t.text === '-') {
      i++;
      return { type: 'Unary', op: '-', operand: exprBp(8) };
    }
    return atom();
  }
  function exprBp(minBp) {
    let left = prefix();
    for (;;) {
      const t = tokens[i];
      if (t === undefined) break;
      if (t.kind === 'keyword' && t.text === 'IS') {
        if (4 < minBp) break;
        i++;
        let negated = false;
        if (atKeyword('NOT')) { i++; negated = true; }
        const n = tokens[i++];
        if (n === undefined || n.kind !== 'keyword' || n.text !== 'NULL') fail(n);
        left = { type: 'IsNull', operand: left, negated: negated };
        continue;
      }
      const isBinary = (t.kind === 'op' && BP[t.text] !== undefined) ||
        (t.kind === 'keyword' && (t.text === 'AND' || t.text === 'OR'));
      if (!isBinary) break;
      const bp = BP[t.text];
      if (bp < minBp) break;
      i++;
      const right = exprBp(bp + 1);
      left = { type: 'Binary', op: t.text, left: left, right: right };
    }
    return left;
  }
  const result = exprBp(1);
  if (i < tokens.length) fail(tokens[i]);
  return result;
}`,
  tests: [
    {
      name: 'OR under AND under NOT under comparison',
      args: [tokenizeSql('NOT a = 1 AND b = 2 OR c = 3')],
      expected: parseSqlExpr(tokenizeSql('NOT a = 1 AND b = 2 OR c = 3')),
      hint: 'One binding-power table drives it: OR 1, AND 2, NOT 3, comparisons 4. NOT’s operand parses at power 4, so it captures the comparison but stops before AND.',
    },
    {
      name: 'IS NULL is postfix and grabs the whole additive operand',
      args: [tokenizeSql('a + 1 IS NULL')],
      expected: parseSqlExpr(tokenizeSql('a + 1 IS NULL')),
      hint: 'Handle IS inside the operator loop (like a binary op at power 4), wrapping what you have parsed so far.',
    },
    {
      name: 'IS NOT NULL sets negated',
      args: [tokenizeSql('total IS NOT NULL')],
      expected: parseSqlExpr(tokenizeSql('total IS NOT NULL')),
    },
    {
      name: 'qualified and bare columns',
      args: [tokenizeSql('u.city = city')],
      expected: parseSqlExpr(tokenizeSql('u.city = city')),
    },
    {
      name: 'literals: numbers, strings, NULL, TRUE, FALSE',
      args: [tokenizeSql("name = 'ada' AND ok = TRUE AND gone = NULL AND n = 4 AND f = FALSE")],
      expected: parseSqlExpr(
        tokenizeSql("name = 'ada' AND ok = TRUE AND gone = NULL AND n = 4 AND f = FALSE"),
      ),
    },
    {
      name: 'arithmetic keeps its usual precedence under comparison',
      args: [tokenizeSql('price * quantity > 100 + 20')],
      expected: parseSqlExpr(tokenizeSql('price * quantity > 100 + 20')),
    },
    {
      name: 'binary operators associate left',
      args: [tokenizeSql('a - b - c')],
      expected: parseSqlExpr(tokenizeSql('a - b - c')),
      hint: 'Left associativity in a Pratt loop: parse the right side at bp + 1.',
    },
    {
      name: 'unary minus binds tightest',
      args: [tokenizeSql('-a * b')],
      expected: parseSqlExpr(tokenizeSql('-a * b')),
    },
    {
      name: 'parens group',
      args: [tokenizeSql('(a OR b) AND c')],
      expected: parseSqlExpr(tokenizeSql('(a OR b) AND c')),
    },
    {
      name: 'IS must be followed by [NOT] NULL',
      args: [tokenizeSql('a IS 1')],
      throws: { messageIncludes: "'1' at 5" },
    },
    {
      name: 'trailing tokens are an error',
      args: [tokenizeSql('a = 1 b')],
      throws: { messageIncludes: "'b' at 6" },
    },
    {
      name: 'empty input reports end of input',
      args: [[]],
      throws: { messageIncludes: 'Unexpected end of input' },
    },
  ],
  custom: {
    describe:
      'A SQL expression (not a whole query); it is tokenized for you, and the reference parser supplies the expected AST.',
    placeholder: '"NOT (a.x + 1 IS NULL) AND b <> \'z\'"',
    toArgs: (values) => [tokenizeSql(String(values[0]))],
  },
};

/**
 * Provided to the SELECT-statement challenge so nobody re-pastes their
 * Pratt parser: parses the longest expression starting at `start` and
 * reports where it stopped.
 */
const PARSE_EXPR_FROM_PRELUDE = `// ---- provided: expression parsing (built in the previous challenge) ----
// parseExprFrom(tokens, start) -> { expr, end }
// Parses the longest expression starting at index 'start'; 'end' is the
// index of the first token it did not consume. A Pratt parser stops on its
// own at any token that is not an operator — a keyword like FROM, a comma,
// a closing paren — which is exactly what a statement parser needs.
function parseExprFrom(tokens, start) {
  const BP = { OR: 1, AND: 2, '=': 4, '<>': 4, '<': 4, '<=': 4, '>': 4, '>=': 4,
    '+': 5, '-': 5, '*': 6, '/': 6 };
  let i = start;
  function fail(t) {
    if (t === undefined) throw new Error('Unexpected end of input');
    throw new Error("Unexpected token '" + t.text + "' at " + t.pos);
  }
  function atom() {
    const t = tokens[i++];
    if (t === undefined) fail(t);
    if (t.kind === 'number') return { type: 'Lit', value: Number(t.text) };
    if (t.kind === 'string') return { type: 'Lit', value: t.value };
    if (t.kind === 'keyword' && t.text === 'NULL') return { type: 'Lit', value: null };
    if (t.kind === 'keyword' && t.text === 'TRUE') return { type: 'Lit', value: true };
    if (t.kind === 'keyword' && t.text === 'FALSE') return { type: 'Lit', value: false };
    if (t.kind === 'ident') {
      const dot = tokens[i];
      if (dot !== undefined && dot.kind === 'punct' && dot.text === '.') {
        i++;
        const name = tokens[i++];
        if (name === undefined || name.kind !== 'ident') fail(name);
        return { type: 'Column', table: t.text, name: name.text };
      }
      return { type: 'Column', table: null, name: t.text };
    }
    if (t.kind === 'punct' && t.text === '(') {
      const inner = exprBp(1);
      const close = tokens[i++];
      if (close === undefined || !(close.kind === 'punct' && close.text === ')')) fail(close);
      return inner;
    }
    fail(t);
  }
  function prefix() {
    const t = tokens[i];
    if (t === undefined) fail(t);
    if (t.kind === 'keyword' && t.text === 'NOT') { i++; return { type: 'Not', operand: exprBp(4) }; }
    if (t.kind === 'op' && t.text === '-') { i++; return { type: 'Unary', op: '-', operand: exprBp(8) }; }
    return atom();
  }
  function exprBp(minBp) {
    let left = prefix();
    for (;;) {
      const t = tokens[i];
      if (t === undefined) break;
      if (t.kind === 'keyword' && t.text === 'IS') {
        if (4 < minBp) break;
        i++;
        let negated = false;
        const maybeNot = tokens[i];
        if (maybeNot !== undefined && maybeNot.kind === 'keyword' && maybeNot.text === 'NOT') {
          i++;
          negated = true;
        }
        const n = tokens[i++];
        if (n === undefined || n.kind !== 'keyword' || n.text !== 'NULL') fail(n);
        left = { type: 'IsNull', operand: left, negated: negated };
        continue;
      }
      const isBinary = (t.kind === 'op' && BP[t.text] !== undefined) ||
        (t.kind === 'keyword' && (t.text === 'AND' || t.text === 'OR'));
      if (!isBinary) break;
      const bp = BP[t.text];
      if (bp < minBp) break;
      i++;
      const right = exprBp(bp + 1);
      left = { type: 'Binary', op: t.text, left: left, right: right };
    }
    return left;
  }
  const expr = exprBp(1);
  return { expr: expr, end: i };
}
// ---- end provided ----`;

const sqlSelectParse: ChallengeDef = {
  id: 'sql-select-parse',
  title: 'Parse SELECT statements',
  summary: 'Sequence the clauses around your expression parser; produce the full Select AST.',
  signature: 'parseSelect(tokens) → Select',
  entry: 'parseSelect',
  prelude: PARSE_EXPR_FROM_PRELUDE,
  difficulty: 4,
  starter: `// Select AST:
//   { type: 'Select',
//     columns: '*' | [{ expr, alias: string|null }],   // alias needs AS
//     from: { table: string, alias: string|null },     // alias: AS or bare
//     joins: [{ table, alias, on }],                   // JOIN t [alias] ON expr
//     where: SqlExpr | null,
//     orderBy: [{ expr, dir: 'ASC'|'DESC' }],          // default ASC
//     limit: number | null }                           // integer literal
// Clause order: SELECT cols FROM t [JOIN ...]* [WHERE e]
//               [ORDER BY key [, key]*] [LIMIT n]
// parseExprFrom(tokens, i) is provided (see the panel above the editor):
//   const r = parseExprFrom(tokens, i); r.expr; i = r.end;
// Consume the whole stream; same error conventions as before.
function parseSelect(tokens) {
  let i = 0;
  // TODO
}`,
  solution: `function parseSelect(tokens) {
  let i = 0;
  function fail(t) {
    if (t === undefined) throw new Error('Unexpected end of input');
    throw new Error("Unexpected token '" + t.text + "' at " + t.pos);
  }
  function atKeyword(word) {
    const t = tokens[i];
    return t !== undefined && t.kind === 'keyword' && t.text === word;
  }
  function atPunct(text) {
    const t = tokens[i];
    return t !== undefined && t.kind === 'punct' && t.text === text;
  }
  function expectKeyword(word) {
    if (!atKeyword(word)) fail(tokens[i]);
    i++;
  }
  function expr() {
    const r = parseExprFrom(tokens, i);
    i = r.end;
    return r.expr;
  }
  function tableRef() {
    const t = tokens[i++];
    if (t === undefined || t.kind !== 'ident') fail(t);
    let alias = null;
    if (atKeyword('AS')) {
      i++;
      const a = tokens[i++];
      if (a === undefined || a.kind !== 'ident') fail(a);
      alias = a.text;
    } else if (tokens[i] !== undefined && tokens[i].kind === 'ident') {
      alias = tokens[i++].text;
    }
    return { table: t.text, alias: alias };
  }
  expectKeyword('SELECT');
  let columns;
  const star = tokens[i];
  if (star !== undefined && star.kind === 'op' && star.text === '*') {
    i++;
    columns = '*';
  } else {
    columns = [];
    for (;;) {
      const e = expr();
      let alias = null;
      if (atKeyword('AS')) {
        i++;
        const a = tokens[i++];
        if (a === undefined || a.kind !== 'ident') fail(a);
        alias = a.text;
      }
      columns.push({ expr: e, alias: alias });
      if (atPunct(',')) { i++; continue; }
      break;
    }
  }
  expectKeyword('FROM');
  const from = tableRef();
  const joins = [];
  while (atKeyword('JOIN')) {
    i++;
    const ref = tableRef();
    expectKeyword('ON');
    const on = expr();
    joins.push({ table: ref.table, alias: ref.alias, on: on });
  }
  let where = null;
  if (atKeyword('WHERE')) { i++; where = expr(); }
  const orderBy = [];
  if (atKeyword('ORDER')) {
    i++;
    expectKeyword('BY');
    for (;;) {
      const e = expr();
      let dir = 'ASC';
      if (atKeyword('ASC')) i++;
      else if (atKeyword('DESC')) { i++; dir = 'DESC'; }
      orderBy.push({ expr: e, dir: dir });
      if (atPunct(',')) { i++; continue; }
      break;
    }
  }
  let limit = null;
  if (atKeyword('LIMIT')) {
    i++;
    const n = tokens[i++];
    if (n === undefined || n.kind !== 'number' || !Number.isInteger(Number(n.text))) fail(n);
    limit = Number(n.text);
  }
  if (i < tokens.length) fail(tokens[i]);
  return { type: 'Select', columns: columns, from: from, joins: joins,
    where: where, orderBy: orderBy, limit: limit };
}`,
  tests: [
    {
      name: 'the kitchen sink: every clause at once',
      args: [
        tokenizeSql(
          "SELECT u.name AS who, o.total FROM users AS u JOIN orders o ON u.id = o.user_id WHERE o.year >= 2024 AND u.city <> 'boston' ORDER BY o.total DESC, u.name LIMIT 10",
        ),
      ],
      expected: parseSelect(
        tokenizeSql(
          "SELECT u.name AS who, o.total FROM users AS u JOIN orders o ON u.id = o.user_id WHERE o.year >= 2024 AND u.city <> 'boston' ORDER BY o.total DESC, u.name LIMIT 10",
        ),
      ),
    },
    {
      name: 'SELECT * with defaults for every optional clause',
      args: [tokenizeSql('SELECT * FROM users')],
      expected: parseSelect(tokenizeSql('SELECT * FROM users')),
      hint: 'Absent clauses still appear in the AST: joins [], where null, orderBy [], limit null.',
    },
    {
      name: 'table aliases: bare and with AS',
      args: [tokenizeSql('SELECT u.name FROM users u')],
      expected: parseSelect(tokenizeSql('SELECT u.name FROM users u')),
    },
    {
      name: 'column aliases require AS',
      args: [tokenizeSql('SELECT name who FROM users')],
      throws: { messageIncludes: "'who' at 12" },
      hint: 'A bare identifier after a select expression is a syntax error here — only table refs take bare aliases.',
    },
    {
      name: 'joins stack in order, each with its ON',
      args: [
        tokenizeSql(
          'SELECT u.name FROM users u JOIN orders o ON u.id = o.user_id JOIN products p ON o.product_id = p.id',
        ),
      ],
      expected: parseSelect(
        tokenizeSql(
          'SELECT u.name FROM users u JOIN orders o ON u.id = o.user_id JOIN products p ON o.product_id = p.id',
        ),
      ),
    },
    {
      name: 'JOIN without ON is an error',
      args: [tokenizeSql('SELECT * FROM users u JOIN orders o WHERE 1 = 1')],
      throws: { messageIncludes: "'WHERE' at 36" },
    },
    {
      name: 'ORDER BY defaults ASC per key',
      args: [tokenizeSql('SELECT * FROM users ORDER BY signup_year DESC, name')],
      expected: parseSelect(tokenizeSql('SELECT * FROM users ORDER BY signup_year DESC, name')),
    },
    {
      name: 'expressions are fine in the select list and ORDER BY',
      args: [tokenizeSql('SELECT quantity * 2 AS double_qty FROM orders ORDER BY total / quantity')],
      expected: parseSelect(
        tokenizeSql('SELECT quantity * 2 AS double_qty FROM orders ORDER BY total / quantity'),
      ),
    },
    {
      name: 'LIMIT takes an integer literal',
      args: [tokenizeSql('SELECT * FROM users LIMIT 2.5')],
      throws: { messageIncludes: "'2.5' at 26" },
    },
    {
      name: 'missing FROM reports the unexpected token',
      args: [tokenizeSql('SELECT name WHERE id = 1')],
      throws: { messageIncludes: "'WHERE' at 12" },
    },
    {
      name: 'trailing tokens are an error',
      args: [tokenizeSql('SELECT * FROM users LIMIT 1 1')],
      throws: { messageIncludes: "'1' at 28" },
    },
  ],
  custom: {
    describe:
      'A full AstQL query; it is tokenized for you, and the reference parser supplies the expected AST.',
    placeholder: '"SELECT u.name FROM users u ORDER BY u.name LIMIT 3"',
    toArgs: (values) => [tokenizeSql(String(values[0]))],
  },
};

export const tier3: Tier = {
  id: 'tier3',
  number: 3,
  title: 'Parsing SQL',
  subtitle: 'AstQL, the course subset: tokens, Pratt expressions, full SELECTs.',
  steps: [
    { kind: 'lesson', lesson: sqlSubsetLesson },
    { kind: 'challenge', challenge: sqlTokenize },
    { kind: 'lesson', lesson: prattLesson },
    { kind: 'challenge', challenge: sqlExprParse },
    { kind: 'lesson', lesson: clausesLesson },
    { kind: 'challenge', challenge: sqlSelectParse },
  ],
};
