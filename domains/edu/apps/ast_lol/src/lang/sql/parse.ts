import type {
  OrderKey,
  Select,
  SelectColumn,
  SqlBinaryOp,
  SqlExpr,
  SqlToken,
  TableRef,
} from './types';

/**
 * Reference Pratt parser for AstQL expressions and recursive-descent parser
 * for SELECT statements; the specs the `sql-expr-parse` and
 * `sql-select-parse` challenges are graded against.
 *
 * Binding powers, loosest to tightest: OR, AND, NOT, comparison and IS
 * [NOT] NULL, `+ -`, `* /`, unary `-`. All binary operators associate left.
 */

const BINARY_BP: Record<string, number> = {
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
const NOT_BP = 3;
const IS_BP = 4;
const NEGATE_BP = 7;

class Cursor {
  i = 0;
  constructor(readonly tokens: SqlToken[]) {}

  peek(): SqlToken | undefined {
    return this.tokens[this.i];
  }

  next(): SqlToken | undefined {
    return this.tokens[this.i++];
  }

  atKeyword(word: string): boolean {
    const t = this.peek();
    return t !== undefined && t.kind === 'keyword' && t.text === word;
  }

  atPunct(text: string): boolean {
    const t = this.peek();
    return t !== undefined && t.kind === 'punct' && t.text === text;
  }

  expectKeyword(word: string): void {
    if (!this.atKeyword(word)) this.fail(this.peek());
    this.next();
  }

  fail(t: SqlToken | undefined): never {
    if (t === undefined) throw new Error('Unexpected end of input');
    throw new Error(`Unexpected token '${t.text}' at ${t.pos}`);
  }
}

function parseExprBp(c: Cursor, minBp: number): SqlExpr {
  let left = parsePrefix(c);
  for (;;) {
    const t = c.peek();
    if (t === undefined) break;
    // IS [NOT] NULL is a postfix operator at comparison strength.
    if (t.kind === 'keyword' && t.text === 'IS') {
      if (IS_BP < minBp) break;
      c.next();
      let negated = false;
      if (c.atKeyword('NOT')) {
        c.next();
        negated = true;
      }
      c.expectKeyword('NULL');
      left = { type: 'IsNull', operand: left, negated };
      continue;
    }
    const isBinary =
      (t.kind === 'op' && t.text in BINARY_BP) ||
      (t.kind === 'keyword' && (t.text === 'AND' || t.text === 'OR'));
    if (!isBinary) break;
    const bp = BINARY_BP[t.text];
    if (bp < minBp) break;
    c.next();
    // Left-associative: the right side must bind strictly tighter.
    const right = parseExprBp(c, bp + 1);
    left = { type: 'Binary', op: t.text as SqlBinaryOp, left, right };
  }
  return left;
}

function parsePrefix(c: Cursor): SqlExpr {
  const t = c.peek();
  if (t === undefined) c.fail(t);
  if (t.kind === 'keyword' && t.text === 'NOT') {
    c.next();
    // NOT binds looser than comparisons: NOT a = b is NOT (a = b).
    return { type: 'Not', operand: parseExprBp(c, NOT_BP + 1) };
  }
  if (t.kind === 'op' && t.text === '-') {
    c.next();
    return { type: 'Unary', op: '-', operand: parseExprBp(c, NEGATE_BP + 1) };
  }
  return parseAtom(c);
}

function parseAtom(c: Cursor): SqlExpr {
  const t = c.next();
  if (t === undefined) c.fail(t);
  if (t.kind === 'number') return { type: 'Lit', value: Number(t.text) };
  if (t.kind === 'string') return { type: 'Lit', value: t.value ?? '' };
  if (t.kind === 'keyword') {
    if (t.text === 'NULL') return { type: 'Lit', value: null };
    if (t.text === 'TRUE') return { type: 'Lit', value: true };
    if (t.text === 'FALSE') return { type: 'Lit', value: false };
    c.fail(t);
  }
  if (t.kind === 'ident') {
    if (c.atPunct('.')) {
      c.next();
      const name = c.next();
      if (name === undefined || name.kind !== 'ident') c.fail(name);
      return { type: 'Column', table: t.text, name: name.text };
    }
    return { type: 'Column', table: null, name: t.text };
  }
  if (t.kind === 'punct' && t.text === '(') {
    const inner = parseExprBp(c, 1);
    const close = c.next();
    if (close === undefined || !(close.kind === 'punct' && close.text === ')')) c.fail(close);
    return inner;
  }
  c.fail(t);
}

/** Parse a complete AstQL expression from a token stream; errors on trailing tokens. */
export function parseSqlExpr(tokens: SqlToken[]): SqlExpr {
  const c = new Cursor(tokens);
  const expr = parseExprBp(c, 1);
  if (c.i < tokens.length) c.fail(c.peek());
  return expr;
}

function parseTableRef(c: Cursor): TableRef {
  const t = c.next();
  if (t === undefined || t.kind !== 'ident') c.fail(t);
  let alias: string | null = null;
  if (c.atKeyword('AS')) {
    c.next();
    const a = c.next();
    if (a === undefined || a.kind !== 'ident') c.fail(a);
    alias = a.text;
  } else if (c.peek()?.kind === 'ident') {
    alias = c.next()!.text;
  }
  return { table: t.text, alias };
}

function parseSelectColumns(c: Cursor): Select['columns'] {
  const t = c.peek();
  if (t !== undefined && t.kind === 'op' && t.text === '*') {
    c.next();
    return '*';
  }
  const columns: SelectColumn[] = [];
  for (;;) {
    const expr = parseExprBp(c, 1);
    let alias: string | null = null;
    // Aliases in the SELECT list require AS; a bare identifier after an
    // expression is a syntax error (unlike table aliases).
    if (c.atKeyword('AS')) {
      c.next();
      const a = c.next();
      if (a === undefined || a.kind !== 'ident') c.fail(a);
      alias = a.text;
    }
    columns.push({ expr, alias });
    if (c.atPunct(',')) {
      c.next();
      continue;
    }
    break;
  }
  return columns;
}

/** Reference SELECT parser; consumes the whole token stream and errors on trailing tokens. */
export function parseSelect(tokens: SqlToken[]): Select {
  const c = new Cursor(tokens);
  c.expectKeyword('SELECT');
  const columns = parseSelectColumns(c);
  c.expectKeyword('FROM');
  const from = parseTableRef(c);

  const joins: Select['joins'] = [];
  while (c.atKeyword('JOIN')) {
    c.next();
    const ref = parseTableRef(c);
    c.expectKeyword('ON');
    const on = parseExprBp(c, 1);
    joins.push({ ...ref, on });
  }

  let where: SqlExpr | null = null;
  if (c.atKeyword('WHERE')) {
    c.next();
    where = parseExprBp(c, 1);
  }

  const orderBy: OrderKey[] = [];
  if (c.atKeyword('ORDER')) {
    c.next();
    c.expectKeyword('BY');
    for (;;) {
      const expr = parseExprBp(c, 1);
      let dir: 'ASC' | 'DESC' = 'ASC';
      if (c.atKeyword('ASC')) {
        c.next();
      } else if (c.atKeyword('DESC')) {
        c.next();
        dir = 'DESC';
      }
      orderBy.push({ expr, dir });
      if (c.atPunct(',')) {
        c.next();
        continue;
      }
      break;
    }
  }

  let limit: number | null = null;
  if (c.atKeyword('LIMIT')) {
    c.next();
    const n = c.next();
    if (n === undefined || n.kind !== 'number' || !Number.isInteger(Number(n.text))) {
      c.fail(n);
    } else {
      limit = Number(n.text);
    }
  }

  if (c.i < tokens.length) c.fail(c.peek());
  return { type: 'Select', columns, from, joins, where, orderBy, limit };
}
