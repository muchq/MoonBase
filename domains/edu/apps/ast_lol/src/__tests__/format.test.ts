import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import { describe, expect, it } from 'vitest';
import {
  formatSelect,
  formatSelectFlat,
  parseSelect,
  parseSqlExpr,
  printSqlExpr,
  tokenizeSql,
  type SqlExpr,
} from '../lang/sql';

const expr = (source: string): SqlExpr => parseSqlExpr(tokenizeSql(source));
const parseSql = (q: string) => parseSelect(tokenizeSql(q));

describe('printSqlExpr', () => {
  it.each([
    ['a = 1 AND b = 2 OR c = 3', 'a = 1 AND b = 2 OR c = 3'],
    ['(a = 1 OR b = 2) AND c = 3', '(a = 1 OR b = 2) AND c = 3'],
    ['NOT a = 1', 'NOT a = 1'],
    ['NOT (a AND b)', 'NOT (a AND b)'],
    ['NOT (NOT a)', 'NOT (NOT a)'],
    ['a + 1 IS NULL', 'a + 1 IS NULL'],
    ['total IS NOT NULL', 'total IS NOT NULL'],
    ['(a AND b) IS NULL', '(a AND b) IS NULL'],
    ['a = (b IS NULL)', 'a = (b IS NULL)'],
    ['a - b - c', 'a - b - c'],
    ['a - (b - c)', 'a - (b - c)'],
    ['price * (quantity + 1)', 'price * (quantity + 1)'],
    ['-price < -5 - 3', '-price < -5 - 3'],
    ['-(-a)', '-(-a)'],
    ['-(a + b)', '-(a + b)'],
    ["name = 'o''brien'", "name = 'o''brien'"],
    ['u.city = NULL', 'u.city = NULL'],
    ['ok = TRUE AND bad = FALSE', 'ok = TRUE AND bad = FALSE'],
  ])('prints %s canonically', (source, expected) => {
    expect(printSqlExpr(expr(source))).toBe(expected);
  });

  it('round-trips every corpus query expression-by-expression', () => {
    const corpus = JSON.parse(
      readFileSync(join(process.cwd(), 'src/__tests__/corpus/sql-corpus.json'), 'utf-8'),
    ) as string[];
    for (const query of corpus) {
      const select = parseSql(query);
      const exprs: SqlExpr[] = [
        ...(select.columns === '*' ? [] : select.columns.map((c) => c.expr)),
        ...select.joins.map((j) => j.on),
        ...(select.where === null ? [] : [select.where]),
        ...select.orderBy.map((k) => k.expr),
      ];
      for (const e of exprs) {
        expect(parseSqlExpr(tokenizeSql(printSqlExpr(e))), printSqlExpr(e)).toEqual(e);
      }
    }
  });
});

describe('formatSelectFlat', () => {
  it('renders every clause canonically on one line', () => {
    expect(
      formatSelectFlat(
        parseSql(
          "select u.name as who, o.total from users u join orders o on u.id = o.user_id where o.year >= 2024 and u.city <> 'boston' order by o.total desc, u.name limit 10",
        ),
      ),
    ).toBe(
      "SELECT u.name AS who, o.total FROM users AS u JOIN orders AS o ON u.id = o.user_id WHERE o.year >= 2024 AND u.city <> 'boston' ORDER BY o.total DESC, u.name LIMIT 10",
    );
  });

  it('renders * and omits absent clauses and default ASC', () => {
    expect(formatSelectFlat(parseSql('select * from users order by name asc'))).toBe(
      'SELECT * FROM users ORDER BY name',
    );
  });

  it('round-trips: the flat form reparses to the same AST', () => {
    const corpus = JSON.parse(
      readFileSync(join(process.cwd(), 'src/__tests__/corpus/sql-corpus.json'), 'utf-8'),
    ) as string[];
    for (const query of corpus) {
      const select = parseSql(query);
      expect(parseSql(formatSelectFlat(select)), query).toEqual(select);
    }
  });
});

describe('formatSelect', () => {
  const query =
    "SELECT u.name AS who, o.total, o.year FROM users AS u JOIN orders AS o ON u.id = o.user_id AND o.total > 100 WHERE u.city = 'seattle' AND o.year >= 2024 AND u.signup_year < o.year ORDER BY o.total DESC, u.name LIMIT 10";

  it('stays flat when the query fits', () => {
    expect(formatSelect(parseSql(query), 300)).toBe(formatSelectFlat(parseSql(query)));
  });

  it('breaks into clause-per-line at medium width, keeping fitting clauses inline', () => {
    expect(formatSelect(parseSql(query), 60)).toBe(
      [
        'SELECT u.name AS who, o.total, o.year',
        'FROM users AS u',
        'JOIN orders AS o ON u.id = o.user_id AND o.total > 100',
        "WHERE u.city = 'seattle'",
        '  AND o.year >= 2024',
        '  AND u.signup_year < o.year',
        'ORDER BY o.total DESC, u.name',
        'LIMIT 10',
      ].join('\n'),
    );
  });

  it('breaks lists and chains fully at narrow width', () => {
    expect(formatSelect(parseSql(query), 28)).toBe(
      [
        'SELECT',
        '  u.name AS who,',
        '  o.total,',
        '  o.year',
        'FROM users AS u',
        'JOIN orders AS o ON u.id = o.user_id',
        '  AND o.total > 100',
        "WHERE u.city = 'seattle'",
        '  AND o.year >= 2024',
        '  AND u.signup_year < o.year',
        'ORDER BY',
        '  o.total DESC,',
        '  u.name',
        'LIMIT 10',
      ].join('\n'),
    );
  });

  it('splits only the loosest operator chain — AND groups under OR stay inline', () => {
    const q = "SELECT name FROM users WHERE city = 'a' AND signup_year > 2000 OR city = 'b' AND signup_year < 1990";
    expect(formatSelect(parseSql(q), 40)).toBe(
      [
        'SELECT name',
        'FROM users',
        "WHERE city = 'a' AND signup_year > 2000",
        "  OR city = 'b' AND signup_year < 1990",
      ].join('\n'),
    );
  });

  it('accepts unavoidable overflow: a single wide expression stays inline', () => {
    const q = 'SELECT name FROM users WHERE signup_year * signup_year * signup_year > 99999999';
    const out = formatSelect(parseSql(q), 20);
    expect(out.split('\n')).toEqual([
      'SELECT name',
      'FROM users',
      'WHERE signup_year * signup_year * signup_year > 99999999',
    ]);
  });

  it('a split operand keeps the parens its position requires', () => {
    // Bare reprinting here once rejoined OR groups into the AND chain —
    // a different query. Two lenses of the review panel found it; this
    // literal is the regression pin.
    expect(
      formatSelect(
        parseSql(
          'SELECT o.id FROM orders o WHERE o.total > 100 AND (o.year = 2024 OR o.year = 2025) AND o.quantity <> 1',
        ),
        45,
      ),
    ).toBe(
      [
        'SELECT o.id',
        'FROM orders AS o',
        'WHERE o.total > 100',
        '  AND (o.year = 2024 OR o.year = 2025)',
        '  AND o.quantity <> 1',
      ].join('\n'),
    );
  });

  it('a right-nested same-operator group keeps its parens too', () => {
    expect(
      formatSelect(parseSql("SELECT name FROM users WHERE city = 'a' AND (city = 'b' AND city = 'c')"), 30),
    ).toBe(
      ['SELECT name', 'FROM users', "WHERE city = 'a'", "  AND (city = 'b' AND city = 'c')"].join(
        '\n',
      ),
    );
  });

  it('SELECT * with stacked joins breaks without touching the star', () => {
    expect(
      formatSelect(
        parseSql(
          'SELECT * FROM users u JOIN orders o ON u.id = o.user_id JOIN products p ON o.product_id = p.id',
        ),
        40,
      ),
    ).toBe(
      [
        'SELECT *',
        'FROM users AS u',
        'JOIN orders AS o ON u.id = o.user_id',
        'JOIN products AS p ON o.product_id = p.id',
      ].join('\n'),
    );
  });

  it('extreme numbers render as plain digits everywhere, LIMIT included', () => {
    const q = 'SELECT id FROM orders WHERE total > 100000000000000000000000 AND quantity <> 0.0000001 LIMIT 100000000000000000000000';
    const flat = formatSelectFlat(parseSql(q));
    expect(flat).toBe(
      'SELECT id FROM orders WHERE total > 100000000000000000000000 AND quantity <> 0.0000001 LIMIT 100000000000000000000000',
    );
    expect(parseSql(flat)).toEqual(parseSql(q));
  });

  it('a raw newline inside a string literal spans lines but still reparses', () => {
    const select = parseSql("SELECT 'a\nb' FROM users");
    const flat = formatSelectFlat(select);
    expect(flat).toContain('\n');
    expect(parseSql(flat)).toEqual(select);
  });

  it('every broken form of every corpus query reparses to the same AST', () => {
    const corpus = JSON.parse(
      readFileSync(join(process.cwd(), 'src/__tests__/corpus/sql-corpus.json'), 'utf-8'),
    ) as string[];
    for (const q of [...corpus, query]) {
      const select = parseSql(q);
      for (const width of [10, 25, 40, 60, 100]) {
        const formatted = formatSelect(select, width);
        expect(parseSql(formatted.replaceAll('\n', ' ')), `${q} @ ${width}`).toEqual(select);
      }
    }
  });
});
