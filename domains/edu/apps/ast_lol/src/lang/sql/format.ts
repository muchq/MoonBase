import type { Select, SelectColumn, SqlExpr, TableRef } from './types';

/**
 * Reference formatter for AstQL; the specs the `sql-expr-print` and
 * `sql-format` challenges are graded against.
 *
 * The canonical style, exactly:
 * - keywords uppercase; single spaces; `AS` for both column and table
 *   aliases; `ASC` omitted (it is the default); strings re-escape quotes
 *   by doubling.
 * - expressions carry the fewest parentheses the uniform rule admits:
 *   parenthesize a child whose precedence is below what its position
 *   requires. Prefix operators require one level above their own, so
 *   `NOT (NOT x)` and `-(-x)` keep parens (the parser would accept the
 *   first bare; the second also dodges `--`, which would lex as a
 *   comment).
 * - a query renders flat when it fits the width; otherwise clause-per-line
 *   with two-space indents: long SELECT / ORDER BY lists break one item
 *   per line (trailing commas), and a WHERE / ON predicate that overflows
 *   splits its top-level AND/OR chain with the operator leading each
 *   continuation line. A split operand keeps the parentheses its position
 *   requires — the same uniform rule, applied at the seam, so the lines
 *   rejoin to the identical tree. Expressions themselves never wrap: a
 *   line holding one unsplittable unit (an expression, a chain operand, a
 *   list item) may exceed the width as unavoidable overflow. A string
 *   literal containing a raw newline necessarily spans physical lines.
 */

const PREC: Record<string, number> = {
  OR: 1,
  AND: 2,
  '=': 4,
  '<>': 4,
  '<': 4,
  '<=': 4,
  '>': 4,
  '>=': 4,
  '+': 5,
  '-': 5,
  '*': 6,
  '/': 6,
};
const NOT_PREC = 3;
const IS_PREC = 4;
const NEGATE_PREC = 7;
const ATOM_PREC = 8;

function prec(e: SqlExpr): number {
  switch (e.type) {
    case 'Binary':
      return PREC[e.op];
    case 'Not':
      return NOT_PREC;
    case 'IsNull':
      return IS_PREC;
    case 'Unary':
      return NEGATE_PREC;
    default:
      return ATOM_PREC;
  }
}

function wrap(child: SqlExpr, min: number): string {
  const s = printSqlExpr(child);
  return prec(child) < min ? `(${s})` : s;
}

/**
 * Numbers as plain digits: String() switches to exponent notation at the
 * extremes (1e-7, 1e+23), which the tokenizer cannot read back.
 */
export function plainNumber(v: number): string {
  const s = String(v);
  const m = /^(-?)(\d+)(?:\.(\d+))?e([+-]\d+)$/.exec(s);
  if (m === null) return s;
  const sign = m[1];
  const int = m[2];
  const frac = m[3] ?? '';
  const exp = Number(m[4]);
  const digits = int + frac;
  const point = int.length + exp;
  if (point <= 0) return `${sign}0.${'0'.repeat(-point)}${digits}`;
  if (point >= digits.length) return `${sign}${digits}${'0'.repeat(point - digits.length)}`;
  return `${sign}${digits.slice(0, point)}.${digits.slice(point)}`;
}

/** Minimal-parens flat rendering of an AstQL expression. */
export function printSqlExpr(e: SqlExpr): string {
  switch (e.type) {
    case 'Lit': {
      const v = e.value;
      if (v === null) return 'NULL';
      if (v === true) return 'TRUE';
      if (v === false) return 'FALSE';
      if (typeof v === 'number') return plainNumber(v);
      return `'${v.replaceAll("'", "''")}'`;
    }
    case 'Column':
      return e.table === null ? e.name : `${e.table}.${e.name}`;
    case 'Not':
      // The operand parses at comparison strength, so anything looser —
      // AND, OR, another NOT — gets parens under the uniform rule.
      return `NOT ${wrap(e.operand, NOT_PREC + 1)}`;
    case 'Unary':
      return `-${wrap(e.operand, NEGATE_PREC + 1)}`;
    case 'IsNull':
      return `${wrap(e.operand, IS_PREC)} IS ${e.negated ? 'NOT ' : ''}NULL`;
    case 'Binary': {
      const p = PREC[e.op];
      // All binary operators associate left: equal precedence stays bare
      // on the left and needs parens on the right.
      return `${wrap(e.left, p)} ${e.op} ${wrap(e.right, p + 1)}`;
    }
  }
}

const tableRef = (ref: TableRef): string =>
  ref.alias === null ? ref.table : `${ref.table} AS ${ref.alias}`;

const selectColumn = (c: SelectColumn): string =>
  c.alias === null ? printSqlExpr(c.expr) : `${printSqlExpr(c.expr)} AS ${c.alias}`;

const orderKey = (k: Select['orderBy'][number]): string =>
  k.dir === 'DESC' ? `${printSqlExpr(k.expr)} DESC` : printSqlExpr(k.expr);

/** The whole query on one line, canonical style. */
export function formatSelectFlat(select: Select): string {
  const parts: string[] = [];
  parts.push(
    `SELECT ${select.columns === '*' ? '*' : select.columns.map(selectColumn).join(', ')}`,
  );
  parts.push(`FROM ${tableRef(select.from)}`);
  for (const join of select.joins) {
    parts.push(`JOIN ${tableRef(join)} ON ${printSqlExpr(join.on)}`);
  }
  if (select.where !== null) parts.push(`WHERE ${printSqlExpr(select.where)}`);
  if (select.orderBy.length > 0) {
    parts.push(`ORDER BY ${select.orderBy.map(orderKey).join(', ')}`);
  }
  if (select.limit !== null) parts.push(`LIMIT ${plainNumber(select.limit)}`);
  return parts.join(' ');
}

/** Flatten the top-level chain of one left-associated AND/OR operator. */
function chainOf(expr: SqlExpr, op: 'AND' | 'OR'): SqlExpr[] {
  if (expr.type === 'Binary' && expr.op === op) {
    return [...chainOf(expr.left, op), expr.right];
  }
  return [expr];
}

/** Width-aware canonical formatting: flat when it fits, clause-per-line when it does not. */
export function formatSelect(select: Select, width: number): string {
  const flat = formatSelectFlat(select);
  if (flat.length <= width) return flat;

  const lines: string[] = [];

  const list = (head: string, items: string[]) => {
    const inline = `${head} ${items.join(', ')}`;
    if (inline.length <= width) {
      lines.push(inline);
      return;
    }
    lines.push(head);
    items.forEach((item, i) => {
      lines.push(`  ${item}${i < items.length - 1 ? ',' : ''}`);
    });
  };

  const predicate = (head: string, expr: SqlExpr) => {
    const inline = `${head} ${printSqlExpr(expr)}`;
    if (inline.length <= width) {
      lines.push(inline);
      return;
    }
    if (expr.type === 'Binary' && (expr.op === 'AND' || expr.op === 'OR')) {
      const op = expr.op;
      const parts = chainOf(expr, op);
      // A split operand sits in an operator position and keeps the parens
      // that position requires — bare printSqlExpr here would let
      // `p AND (q OR r)` rejoin as a different tree.
      lines.push(`${head} ${wrap(parts[0], PREC[op])}`);
      for (const part of parts.slice(1)) {
        lines.push(`  ${op} ${wrap(part, PREC[op] + 1)}`);
      }
      return;
    }
    // A single over-wide expression is unavoidable overflow.
    lines.push(inline);
  };

  if (select.columns === '*') {
    lines.push('SELECT *');
  } else {
    list('SELECT', select.columns.map(selectColumn));
  }
  lines.push(`FROM ${tableRef(select.from)}`);
  for (const join of select.joins) {
    predicate(`JOIN ${tableRef(join)} ON`, join.on);
  }
  if (select.where !== null) predicate('WHERE', select.where);
  if (select.orderBy.length > 0) list('ORDER BY', select.orderBy.map(orderKey));
  if (select.limit !== null) lines.push(`LIMIT ${plainNumber(select.limit)}`);
  return lines.join('\n');
}
