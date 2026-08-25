import type { Select, SelectColumn, SqlExpr, TableRef } from './types';

/**
 * Reference formatter for AstQL; the specs the `sql-expr-print` and
 * `sql-format` challenges are graded against.
 *
 * The canonical style, exactly:
 * - keywords uppercase; single spaces; `AS` for both column and table
 *   aliases; `ASC` omitted (it is the default); strings re-escape quotes
 *   by doubling.
 * - expressions carry the fewest parentheses that reparse to the same
 *   tree, under one uniform rule: parenthesize a child whose precedence
 *   is below what its position requires. Prefix operators require their
 *   operand's own level, so `NOT (NOT x)` and `-(-x)` keep parens — the
 *   latter also dodging `--`, which would lex as a comment.
 * - a query renders flat when it fits the width; otherwise clause-per-line
 *   with two-space indents: long SELECT / ORDER BY lists break one item
 *   per line (trailing commas), and a WHERE / ON predicate that overflows
 *   splits its top-level AND/OR chain with the operator leading each
 *   continuation line. Expressions themselves never wrap — a single
 *   expression wider than the limit is accepted as unavoidable overflow.
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

/** Minimal-parens flat rendering of an AstQL expression. */
export function printSqlExpr(e: SqlExpr): string {
  switch (e.type) {
    case 'Lit': {
      const v = e.value;
      if (v === null) return 'NULL';
      if (v === true) return 'TRUE';
      if (v === false) return 'FALSE';
      if (typeof v === 'number') return String(v);
      return `'${v.replaceAll("'", "''")}'`;
    }
    case 'Column':
      return e.table === null ? e.name : `${e.table}.${e.name}`;
    case 'Not':
      // The operand parses at comparison strength, so anything looser —
      // AND, OR, another NOT — needs parens to reparse identically.
      return `NOT ${wrap(e.operand, IS_PREC)}`;
    case 'Unary':
      return `-${wrap(e.operand, ATOM_PREC)}`;
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
  if (select.limit !== null) parts.push(`LIMIT ${select.limit}`);
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
      lines.push(`${head} ${printSqlExpr(parts[0])}`);
      for (const part of parts.slice(1)) {
        lines.push(`  ${op} ${printSqlExpr(part)}`);
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
  if (select.limit !== null) lines.push(`LIMIT ${select.limit}`);
  return lines.join('\n');
}
