import { firstDiff, show } from '../grader/deepEqual';
import type { CheckFn } from '../grader/types';
import {
  formatSelect,
  parseSelect,
  parseSqlExpr,
  printSqlExpr,
  tokenizeSql,
  type Select,
  type SqlExpr,
} from '../lang/sql';
import type { ChallengeDef, LessonDef, Tier } from './types';

/**
 * Tier 6 — The formatter: a parallel capstone track. Prerequisite: Tier 3
 * (parsing) only; Tiers 4–5 are not needed here, so this track can be
 * taken before, after, or instead of the optimizer.
 */

const expr = (source: string): SqlExpr => parseSqlExpr(tokenizeSql(source));
const parseSql = (q: string): Select => parseSelect(tokenizeSql(q));

const layoutLesson: LessonDef = {
  id: 'layout',
  title: 'Formatting: layout as language',
  summary: 'Canonical style, the reparse obligation, and width-aware breaking — the other capstone.',
  reading: [
    {
      title: 'Wadler — A prettier printer',
      url: 'https://homepages.inf.ed.ac.uk/wadler/papers/prettier/prettier.pdf',
      note: 'the classic paper behind doc-based pretty printing',
    },
    {
      title: 'Prettier — Rationale',
      url: 'https://prettier.io/docs/rationale',
      note: 'why a formatter prints from the AST and owns all layout decisions',
    },
    {
      title: 'Bob Nystrom — The Hardest Program I’ve Ever Written',
      url: 'https://journal.stuffwithstuff.com/2015/09/08/the-hardest-program-ive-ever-written/',
      note: 'what an industrial formatter (dartfmt) is actually up against',
    },
    {
      title: 'Black — The Black code style',
      url: 'https://black.readthedocs.io/en/stable/the_black_code_style/current_style.html',
      note: 'a canonical style pinned down in prose, decision by decision',
    },
  ],
};

/**
 * The expression printer as a revealable solution — a single source: it is
 * the `sql-expr-print` challenge's solution AND the capstone's provided
 * prelude, so the revealed answer and the provided code cannot drift. (If
 * they could, a perfect capstone layout would fail on expression bytes.)
 */
const PRINT_EXPR_SOLUTION = `// String() switches to exponent notation at the extremes (1e-7, 1e+23),
// which the tokenizer cannot read back — expand to plain digits.
function plainNumber(v) {
  const s = String(v);
  const m = /^(-?)(\\d+)(?:\\.(\\d+))?e([+-]\\d+)$/.exec(s);
  if (m === null) return s;
  const digits = m[2] + (m[3] || '');
  const point = m[2].length + Number(m[4]);
  if (point <= 0) return m[1] + '0.' + '0'.repeat(-point) + digits;
  if (point >= digits.length) return m[1] + digits + '0'.repeat(point - digits.length);
  return m[1] + digits.slice(0, point) + '.' + digits.slice(point);
}

function printExpr(expr) {
  const PREC = { OR: 1, AND: 2, '=': 4, '<>': 4, '<': 4, '<=': 4, '>': 4, '>=': 4,
    '+': 5, '-': 5, '*': 6, '/': 6 };
  function prec(e) {
    if (e.type === 'Binary') return PREC[e.op];
    if (e.type === 'Not') return 3;
    if (e.type === 'IsNull') return 4;
    if (e.type === 'Unary') return 7;
    return 8;
  }
  function wrap(child, min) {
    const s = printExpr(child);
    return prec(child) < min ? '(' + s + ')' : s;
  }
  switch (expr.type) {
    case 'Lit': {
      const v = expr.value;
      if (v === null) return 'NULL';
      if (v === true) return 'TRUE';
      if (v === false) return 'FALSE';
      if (typeof v === 'number') return plainNumber(v);
      return "'" + v.replaceAll("'", "''") + "'";
    }
    case 'Column':
      return expr.table === null ? expr.name : expr.table + '.' + expr.name;
    case 'Not':
      return 'NOT ' + wrap(expr.operand, 4);
    case 'Unary':
      return '-' + wrap(expr.operand, 8);
    case 'IsNull':
      return wrap(expr.operand, 4) + ' IS ' + (expr.negated ? 'NOT ' : '') + 'NULL';
    case 'Binary': {
      const p = PREC[expr.op];
      return wrap(expr.left, p) + ' ' + expr.op + ' ' + wrap(expr.right, p + 1);
    }
  }
}`;

/**
 * Exact-match grading with reparse-explained failures: a wrong string is
 * diagnosed as "different tree" (parens) or "right tree, wrong formatting"
 * (style), the same two-layer trick the Expr printer used.
 */
const exprPrintCheck =
  (expected: string): CheckFn =>
  (actual, ctx) => {
    if (typeof actual !== 'string') {
      return { pass: false, message: `Expected a string, got ${show(actual)}.`, expectedText: expected };
    }
    if (actual === expected) return { pass: true };
    let reparsed: SqlExpr;
    try {
      reparsed = parseSqlExpr(tokenizeSql(actual));
    } catch (e) {
      return {
        pass: false,
        message: `Your output does not parse as an AstQL expression: ${e instanceof Error ? e.message : show(e)}`,
        expectedText: expected,
        actualText: actual,
      };
    }
    const diff = firstDiff(ctx.args[0], reparsed);
    if (diff === null) {
      return {
        pass: false,
        message:
          'Your output parses back to the right tree but the style is off — canonical form fixes uppercase keywords, single spaces, quote doubling, and *minimal* parens under the uniform rule.',
        expectedText: expected,
        actualText: actual,
      };
    }
    return {
      pass: false,
      message: `Reparsing your output yields a different tree (first difference at result${diff.path}: expected ${show(diff.expected)}, got ${show(diff.actual)}) — parentheses are missing or extra where precedence needs them.`,
      expectedText: expected,
      actualText: actual,
    };
  };

function printTest(source: string, name: string, hint?: string) {
  const ast = expr(source);
  return { name, args: [ast] as unknown[], check: exprPrintCheck(printSqlExpr(ast)), hint };
}

const sqlExprPrint: ChallengeDef = {
  id: 'sql-expr-print',
  title: 'Print SQL expressions',
  summary: 'Minimal parens over SQL’s precedence system — NOT, IS NULL, and the comment trap.',
  signature: 'printExpr(expr) → string',
  entry: 'printExpr',
  difficulty: 3,
  starter: `// Render an AstQL expression as canonical source text.
// Style: uppercase keywords (AND OR NOT IS NULL TRUE FALSE), single
// spaces around binary operators, unary minus tight, strings in single
// quotes with ' doubled to ''. Numbers print as plain digits: where
// String(v) gives exponent notation (String(0.0000001) is '1e-7' — text
// the tokenizer cannot read back), expand it yourself.
// Parenthesize under ONE uniform rule: wrap a child whose precedence is
// below what its position requires. Levels:
//   OR 1  AND 2  NOT 3  comparisons & IS [NOT] NULL 4  + - 5  * / 6
//   unary - 7  atoms 8
// Positions:
//   Binary op at level p: left requires p, right requires p + 1
//     (everything associates left).
//   NOT's operand requires 4.  IS [NOT] NULL's operand requires 4.
//   Unary -'s operand requires 8 — so -(-a) keeps its parens, which
//   also dodges '--' lexing as a comment.
function printExpr(expr) {
  // TODO
}`,
  solution: PRINT_EXPR_SOLUTION,
  tests: [
    printTest('a = 1 AND b = 2 OR c = 3', 'the logical ladder stays bare'),
    printTest(
      '(a = 1 OR b = 2) AND c = 3',
      'OR under AND keeps its parens',
      'OR (1) is below AND’s requirement (2) on the left — the uniform rule covers every such case.',
    ),
    printTest('NOT a = 1', 'NOT over a comparison stays bare'),
    printTest(
      'NOT (a AND b)',
      'NOT over AND needs parens',
      'NOT’s operand position requires level 4; AND is level 2.',
    ),
    printTest(
      'NOT (NOT a)',
      'stacked NOTs keep parens (the uniform rule, not an exception)',
      'NOT itself is level 3 — below the 4 its own operand position requires.',
    ),
    printTest('a + 1 IS NULL', 'IS NULL grabs the additive operand bare'),
    printTest('(a AND b) IS NULL', 'IS NULL over AND needs parens'),
    printTest(
      'a = (b IS NULL)',
      'IS NULL as a right comparison operand needs parens',
      'A comparison’s right side requires level 5; IS NULL is level 4.',
    ),
    printTest('a - b - c', 'left association prints flat'),
    printTest(
      'a - (b - c)',
      'right-nested subtraction keeps parens',
      'The right side of a level-5 operator requires level 6.',
    ),
    printTest(
      '-(-a)',
      'double negation keeps parens — and dodges the comment trap',
      "Bare '--a' would lex as a comment; the uniform rule (unary operand requires 8) already prevents it.",
    ),
    printTest("name = 'o''brien'", 'strings re-escape their quotes', "Emit ' as '' — the tokenizer decoded it; you re-encode it."),
    printTest('u.city = NULL AND ok = TRUE', 'literals print as keywords'),
    printTest('-price < -5 - 3', 'unary minus over atoms stays tight and bare'),
    printTest(
      'a = 0.0000001 OR price > 100000000000000000000000',
      'extreme numbers print as plain digits, never exponent notation',
      "String(0.0000001) is '1e-7' — text the tokenizer cannot read back. Expand the exponent form into digits.",
    ),
  ],
  custom: {
    describe:
      'An AstQL expression; it is parsed for you, and the reference printer supplies the canonical text.',
    placeholder: '"NOT (a.x + 1 IS NULL) AND b <> \'z\'"',
    toArgs: (values) => [expr(String(values[0]))],
  },
  check: (actual, ctx) => exprPrintCheck(printSqlExpr(ctx.args[0] as SqlExpr))(actual, ctx),
};

/** Provided to the formatter capstone: the expression printer built in the previous challenge. */
const PRINT_EXPR_PRELUDE = `// ---- provided: from the previous challenge ----
// printExpr(expr)  -> canonical flat text with minimal parentheses.
// plainNumber(v)   -> a number as plain digits (String() goes exponential
//                     at the extremes; the tokenizer cannot read '1e+23').
${PRINT_EXPR_SOLUTION}
// ---- end provided ----`;

/**
 * Exact-match against the reference formatter, with failures diagnosed in
 * layers: does it even reparse to the same query? same query, then which
 * line first differs — and is that line over the width?
 */
const formatCheck: CheckFn = (actual, ctx) => {
  const [select, width] = ctx.args as [Select, number];
  const expected = formatSelect(select, width);
  if (typeof actual !== 'string') {
    return { pass: false, message: `Expected a string, got ${show(actual)}.`, expectedText: expected };
  }
  if (actual === expected) return { pass: true };

  // The tokenizer reads newlines as whitespace (and keeps them inside string
  // literals), so the multi-line output reparses as-is — collapsing newlines
  // first would corrupt newline-bearing literals into a wrong-query diagnosis.
  let reparsed: Select | null = null;
  try {
    reparsed = parseSelect(tokenizeSql(actual));
  } catch (e) {
    return {
      pass: false,
      message: `Your output does not reparse as AstQL (${e instanceof Error ? e.message : show(e)}) — fix the rendering before the layout.`,
      expectedText: expected,
      actualText: actual,
    };
  }
  const diff = firstDiff(select, reparsed);
  if (diff !== null) {
    return {
      pass: false,
      message: `Your output reparses to a different query (first difference at result${diff.path}: ${diff.reason}) — check parens and clause rendering before layout.`,
      expectedText: expected,
      actualText: actual,
    };
  }
  const eLines = expected.split('\n');
  const aLines = actual.split('\n');
  for (let i = 0; i < Math.max(eLines.length, aLines.length); i++) {
    if (eLines[i] !== aLines[i]) {
      const overflow =
        aLines[i] !== undefined && aLines[i].length > width
          ? ` Your line is ${aLines[i].length} chars against a width of ${width}.`
          : '';
      return {
        pass: false,
        message: `Same query, different layout: first difference at line ${i + 1} — expected ${JSON.stringify(eLines[i] ?? '(end of output)')}, got ${JSON.stringify(aLines[i] ?? '(end of output)')}.${overflow}`,
        expectedText: expected,
        actualText: actual,
      };
    }
  }
  // Unreachable when the strings differ (splitting on '\n' is injective, so
  // some line must differ), kept as the total-function fallback.
  return { pass: false, message: 'Outputs differ.', expectedText: expected, actualText: actual };
};

const KITCHEN_SINK =
  "SELECT u.name AS who, o.total, o.year FROM users u JOIN orders o ON u.id = o.user_id AND o.total > 100 WHERE u.city = 'seattle' AND o.year >= 2024 AND u.signup_year < o.year ORDER BY o.total DESC, u.name LIMIT 10";

function formatTest(q: string, width: number, name: string, hint?: string) {
  return { name, args: [parseSql(q), width] as unknown[], hint };
}

const sqlFormat: ChallengeDef = {
  id: 'sql-format',
  title: 'Capstone: the formatter',
  summary: 'Width-aware canonical SQL: flat when it fits, broken where the style says.',
  signature: 'format(select, width) → string',
  entry: 'format',
  prelude: PRINT_EXPR_PRELUDE,
  difficulty: 5,
  starter: `// format(select, width) renders a parsed Select in the canonical style.
// printExpr(expr) is provided (see the panel above the editor).
//
// Flat form (one line, used whenever it fits in width):
//   SELECT cols | * FROM t [AS a] (JOIN t [AS a] ON expr)*
//   [WHERE expr] [ORDER BY key[, key]*] [LIMIT n]
//   - column alias: 'expr AS name'; table alias: 't AS a'
//   - ORDER BY: 'expr DESC' or bare expr (ASC is the default and omitted)
//
// When the flat form exceeds width, each clause starts its own line:
//   - SELECT and ORDER BY lists: inline if the clause line fits, else
//     the keyword alone, then one item per line indented 2, trailing
//     commas on all but the last.
//   - WHERE and JOIN ... ON predicates: inline if the clause line fits,
//     else split the TOP-LEVEL chain of the loosest AND/OR operator:
//     first operand stays on the clause line, each remaining operand on
//     its own line, indented 2, with the operator leading. A split
//     operand keeps the parens its position requires (first operand:
//     the chain operator's level; the rest: one tighter) — an OR group
//     inside a broken AND chain keeps its parens, so the lines rejoin
//     to the identical tree. Nested groups stay inline as units.
//   - A single expression wider than the limit is unavoidable overflow:
//     leave it inline.
//   - FROM and LIMIT always fit on their own lines (render the LIMIT
//     count with the provided plainNumber). 'SELECT *' never breaks.
//     Join lines with '\\n'.
function format(select, width) {
  // TODO
}`,
  solution: `function format(select, width) {
  function tableRef(ref) {
    return ref.alias === null ? ref.table : ref.table + ' AS ' + ref.alias;
  }
  function column(c) {
    return c.alias === null ? printExpr(c.expr) : printExpr(c.expr) + ' AS ' + c.alias;
  }
  function orderKey(k) {
    return k.dir === 'DESC' ? printExpr(k.expr) + ' DESC' : printExpr(k.expr);
  }
  function flat() {
    const parts = [];
    parts.push('SELECT ' + (select.columns === '*' ? '*' : select.columns.map(column).join(', ')));
    parts.push('FROM ' + tableRef(select.from));
    for (const join of select.joins) {
      parts.push('JOIN ' + tableRef(join) + ' ON ' + printExpr(join.on));
    }
    if (select.where !== null) parts.push('WHERE ' + printExpr(select.where));
    if (select.orderBy.length > 0) parts.push('ORDER BY ' + select.orderBy.map(orderKey).join(', '));
    if (select.limit !== null) parts.push('LIMIT ' + plainNumber(select.limit));
    return parts.join(' ');
  }
  const whole = flat();
  if (whole.length <= width) return whole;

  const lines = [];
  function list(head, items) {
    const inline = head + ' ' + items.join(', ');
    if (inline.length <= width) { lines.push(inline); return; }
    lines.push(head);
    items.forEach(function (item, i) {
      lines.push('  ' + item + (i < items.length - 1 ? ',' : ''));
    });
  }
  function chainOf(expr, op) {
    if (expr.type === 'Binary' && expr.op === op) {
      return chainOf(expr.left, op).concat([expr.right]);
    }
    return [expr];
  }
  function chainPart(part, min) {
    // A split operand keeps the parens its position requires. Only AND/OR
    // tops can bind looser than a chain position, so a three-way level
    // check is the whole rule here.
    const level = part.type === 'Binary' && part.op === 'OR' ? 1
      : part.type === 'Binary' && part.op === 'AND' ? 2 : 3;
    const s = printExpr(part);
    return level < min ? '(' + s + ')' : s;
  }
  function predicate(head, expr) {
    const inline = head + ' ' + printExpr(expr);
    if (inline.length <= width) { lines.push(inline); return; }
    if (expr.type === 'Binary' && (expr.op === 'AND' || expr.op === 'OR')) {
      const parts = chainOf(expr, expr.op);
      const level = expr.op === 'OR' ? 1 : 2;
      lines.push(head + ' ' + chainPart(parts[0], level));
      for (const part of parts.slice(1)) {
        lines.push('  ' + expr.op + ' ' + chainPart(part, level + 1));
      }
      return;
    }
    lines.push(inline);
  }
  if (select.columns === '*') lines.push('SELECT *');
  else list('SELECT', select.columns.map(column));
  lines.push('FROM ' + tableRef(select.from));
  for (const join of select.joins) predicate('JOIN ' + tableRef(join) + ' ON', join.on);
  if (select.where !== null) predicate('WHERE', select.where);
  if (select.orderBy.length > 0) list('ORDER BY', select.orderBy.map(orderKey));
  if (select.limit !== null) lines.push('LIMIT ' + plainNumber(select.limit));
  return lines.join('\\n');
}`,
  check: formatCheck,
  tests: [
    formatTest(
      KITCHEN_SINK,
      300,
      'a query that fits stays flat',
      'Build the flat form first and measure it — the whole width-aware path starts with that check.',
    ),
    formatTest(
      'SELECT name FROM users',
      10,
      'clause-per-line when even a short query cannot fit',
      'Once the flat form overflows, every clause starts its own line — even ones that would have fit together.',
    ),
    formatTest(
      KITCHEN_SINK,
      60,
      'medium width: fitting clauses stay inline, the WHERE chain breaks',
      'Each clause makes its own inline-or-break decision against the same width.',
    ),
    formatTest(
      KITCHEN_SINK,
      28,
      'narrow width: lists and chains break fully',
      'SELECT list items get trailing commas on all but the last; chain lines lead with the operator.',
    ),
    formatTest(
      "SELECT name FROM users WHERE city = 'a' AND signup_year > 2000 OR city = 'b' AND signup_year < 1990",
      40,
      'only the loosest operator chain splits',
      'The top of this WHERE is an OR; its AND groups stay inline on each OR line.',
    ),
    formatTest(
      'SELECT name FROM users WHERE signup_year * signup_year * signup_year > 99999999',
      20,
      'a single over-wide expression is unavoidable overflow',
      'Expressions never wrap internally — leave the WHERE inline even though it exceeds the width.',
    ),
    formatTest(
      'SELECT * FROM users u JOIN orders o ON u.id = o.user_id JOIN products p ON o.product_id = p.id',
      40,
      'SELECT * never breaks; each JOIN decides for itself',
    ),
    formatTest(
      'SELECT id, name, city, signup_year FROM users ORDER BY signup_year DESC, name',
      25,
      'ORDER BY breaks like the SELECT list, keeping DESC with its key',
    ),
    formatTest(
      "SELECT quantity * 2 AS double_qty FROM orders WHERE total IS NOT NULL LIMIT 3",
      200,
      'aliases, IS NOT NULL, and LIMIT in canonical flat form',
      'AS for aliases, ASC omitted, single spaces — the flat form is a spec, not a suggestion.',
    ),
    formatTest(
      "SELECT o.id FROM orders o WHERE o.total > 100 AND (o.year = 2024 OR o.year = 2025) AND o.quantity <> 1",
      45,
      'parenthesized groups ride along unbroken',
      'The OR group is one conjunct of the AND chain — it moves as a unit, parens intact.',
    ),
  ],
  custom: {
    describe:
      'An AstQL query, then optionally a width (default 40). The reference formatter supplies the expected layout.',
    placeholder:
      '"SELECT u.name, o.total FROM users u JOIN orders o ON u.id = o.user_id WHERE u.city = \'seattle\' AND o.year = 2024", 40',
    toArgs: (values) => [
      parseSql(String(values[0])),
      typeof values[1] === 'number' ? values[1] : 40,
    ],
  },
};

export const tier6: Tier = {
  id: 'tier6',
  number: 6,
  title: 'The formatter',
  subtitle:
    'A parallel capstone track — needs only Tier 3; take it before, after, or instead of the optimizer.',
  steps: [
    { kind: 'lesson', lesson: layoutLesson },
    { kind: 'challenge', challenge: sqlExprPrint },
    { kind: 'challenge', challenge: sqlFormat },
  ],
};
