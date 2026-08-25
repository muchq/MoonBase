import { describe, expect, it } from 'vitest';
import {
  buildPlan,
  demoDb,
  executePlan,
  executeWithStats,
  parseSelect,
  parseSqlExpr,
  resolve,
  shopCatalog,
  tokenizeSql,
  validatePlan,
  type Plan,
  type Select,
} from '../lang/sql';

const parseSql = (source: string): Select => parseSelect(tokenizeSql(source));
const expr = (source: string) => parseSqlExpr(tokenizeSql(source));

function planOf(source: string): Plan {
  const { select, errors } = resolve(parseSql(source), shopCatalog);
  expect(errors).toEqual([]);
  return buildPlan(select!, shopCatalog);
}

describe('tokenizeSql', () => {
  it('canonicalizes keywords to uppercase and identifiers to lowercase', () => {
    expect(tokenizeSql('Select Name From USERS')).toEqual([
      { kind: 'keyword', text: 'SELECT', pos: 0 },
      { kind: 'ident', text: 'name', pos: 7 },
      { kind: 'keyword', text: 'FROM', pos: 12 },
      { kind: 'ident', text: 'users', pos: 17 },
    ]);
  });

  it('decodes doubled quotes in strings, keeping the raw lexeme', () => {
    expect(tokenizeSql("'it''s'")).toEqual([
      { kind: 'string', text: "'it''s'", pos: 0, value: "it's" },
    ]);
  });

  it('throws on an unterminated string with its start position', () => {
    expect(() => tokenizeSql("name = 'oops")).toThrow('Unterminated string starting at 7');
  });

  it('applies maximal munch to two-character operators', () => {
    expect(tokenizeSql('a<=b<>c').map((t) => t.text)).toEqual(['a', '<=', 'b', '<>', 'c']);
  });

  it('skips -- comments to end of line', () => {
    expect(tokenizeSql('select -- everything\n *').map((t) => t.text)).toEqual(['SELECT', '*']);
  });

  it('reports unexpected characters with their position', () => {
    expect(() => tokenizeSql('select ?')).toThrow("Unexpected character '?' at 7");
  });
});

describe('parseSqlExpr', () => {
  it('layers OR under AND under NOT under comparison', () => {
    expect(expr('NOT a = 1 AND b = 2 OR c = 3')).toEqual({
      type: 'Binary',
      op: 'OR',
      left: {
        type: 'Binary',
        op: 'AND',
        left: { type: 'Not', operand: expr('a = 1') },
        right: expr('b = 2'),
      },
      right: expr('c = 3'),
    });
  });

  it('parses IS NULL and IS NOT NULL as postfix at comparison strength', () => {
    expect(expr('total IS NULL')).toEqual({
      type: 'IsNull',
      operand: { type: 'Column', table: null, name: 'total' },
      negated: false,
    });
    expect(expr('a + 1 IS NOT NULL')).toEqual({
      type: 'IsNull',
      operand: expr('a + 1'),
      negated: true,
    });
  });

  it('parses qualified columns and literals', () => {
    expect(expr("u.city = 'seattle'")).toEqual({
      type: 'Binary',
      op: '=',
      left: { type: 'Column', table: 'u', name: 'city' },
      right: { type: 'Lit', value: 'seattle' },
    });
    expect(expr('NULL')).toEqual({ type: 'Lit', value: null });
    expect(expr('TRUE')).toEqual({ type: 'Lit', value: true });
  });

  it('gives arithmetic its usual precedence under comparisons', () => {
    expect(expr('price * quantity > 100 + 20')).toEqual({
      type: 'Binary',
      op: '>',
      left: expr('price * quantity'),
      right: expr('100 + 20'),
    });
  });

  it('parses unary minus', () => {
    expect(expr('-price < -5 - 3')).toEqual({
      type: 'Binary',
      op: '<',
      left: { type: 'Unary', op: '-', operand: { type: 'Column', table: null, name: 'price' } },
      right: expr('-5 - 3'),
    });
  });

  it('rejects trailing tokens', () => {
    expect(() => expr('a = 1 b')).toThrow("Unexpected token 'b' at 6");
  });
});

describe('parseSelect', () => {
  it('parses every clause', () => {
    const ast = parseSql(
      "SELECT u.name AS who, o.total FROM users AS u JOIN orders o ON u.id = o.user_id WHERE o.year >= 2024 AND u.city <> 'boston' ORDER BY o.total DESC, u.name LIMIT 10",
    );
    expect(ast).toEqual({
      type: 'Select',
      columns: [
        { expr: { type: 'Column', table: 'u', name: 'name' }, alias: 'who' },
        { expr: { type: 'Column', table: 'o', name: 'total' }, alias: null },
      ],
      from: { table: 'users', alias: 'u' },
      joins: [
        {
          table: 'orders',
          alias: 'o',
          on: expr('u.id = o.user_id'),
        },
      ],
      where: expr("o.year >= 2024 AND u.city <> 'boston'"),
      orderBy: [
        { expr: { type: 'Column', table: 'o', name: 'total' }, dir: 'DESC' },
        { expr: { type: 'Column', table: 'u', name: 'name' }, dir: 'ASC' },
      ],
      limit: 10,
    });
  });

  it('parses SELECT * with defaults for optional clauses', () => {
    expect(parseSql('SELECT * FROM users')).toEqual({
      type: 'Select',
      columns: '*',
      from: { table: 'users', alias: null },
      joins: [],
      where: null,
      orderBy: [],
      limit: null,
    });
  });

  it('requires AS for column aliases but not table aliases', () => {
    expect(parseSql('SELECT name FROM users u').from.alias).toBe('u');
    expect(() => parseSql('SELECT name who FROM users')).toThrow("Unexpected token 'who' at 12");
  });

  it('rejects a missing FROM', () => {
    expect(() => parseSql('SELECT name')).toThrow('Unexpected end of input');
  });

  it('rejects trailing tokens', () => {
    expect(() => parseSql('SELECT name FROM users users2 extra')).toThrow(
      "Unexpected token 'extra' at 30",
    );
  });
});

describe('resolve', () => {
  it('annotates every column with its binding and expands *', () => {
    const { select, errors } = resolve(
      parseSql('SELECT * FROM users u JOIN orders o ON u.id = o.user_id'),
      shopCatalog,
    );
    expect(errors).toEqual([]);
    expect(select!.columns).toEqual(
      [
        ['u', 'id'],
        ['u', 'name'],
        ['u', 'city'],
        ['u', 'signup_year'],
        ['o', 'id'],
        ['o', 'user_id'],
        ['o', 'product_id'],
        ['o', 'quantity'],
        ['o', 'total'],
        ['o', 'year'],
      ].map(([table, name]) => ({ expr: { type: 'Column', table, name }, alias: null })),
    );
  });

  it('resolves an unqualified column owned by exactly one binding', () => {
    const { select, errors } = resolve(
      parseSql('SELECT city FROM users JOIN orders o ON user_id = 1'),
      shopCatalog,
    );
    expect(errors).toEqual([]);
    expect(select!.columns).toEqual([
      { expr: { type: 'Column', table: 'users', name: 'city' }, alias: null },
    ]);
    expect(select!.joins[0].on).toEqual({
      type: 'Binary',
      op: '=',
      left: { type: 'Column', table: 'o', name: 'user_id' },
      right: { type: 'Lit', value: 1 },
    });
  });

  it('reports table errors first and stops', () => {
    const { select, errors } = resolve(
      parseSql('SELECT nope FROM userz JOIN orders o ON 1 = 1 JOIN orders o ON 1 = 1'),
      shopCatalog,
    );
    expect(select).toBeNull();
    expect(errors).toEqual([
      { kind: 'unknown-table', name: 'userz' },
      { kind: 'duplicate-binding', name: 'o' },
    ]);
  });

  it('reports column errors in clause order', () => {
    const { select, errors } = resolve(
      parseSql(
        'SELECT u.nope, mystery FROM users u JOIN orders o ON u.id = o.user_id WHERE id > 0 ORDER BY ghost',
      ),
      shopCatalog,
    );
    expect(select).toBeNull();
    expect(errors).toEqual([
      { kind: 'unknown-column', name: 'u.nope' },
      { kind: 'unknown-column', name: 'mystery' },
      { kind: 'ambiguous-column', name: 'id' },
      { kind: 'unknown-column', name: 'ghost' },
    ]);
  });

  it('does not mutate its input', () => {
    const ast = parseSql('SELECT name FROM users');
    const copy = structuredClone(ast);
    resolve(ast, shopCatalog);
    expect(ast).toEqual(copy);
  });
});

describe('buildPlan', () => {
  it('builds the canonical operator order', () => {
    const plan = planOf(
      'SELECT u.name FROM users u JOIN orders o ON u.id = o.user_id WHERE o.year = 2024 ORDER BY u.name LIMIT 5',
    );
    expect(plan).toEqual({
      type: 'Limit',
      count: 5,
      input: {
        type: 'Project',
        columns: [{ expr: { type: 'Column', table: 'u', name: 'name' }, name: 'name' }],
        input: {
          type: 'Sort',
          keys: [{ expr: { type: 'Column', table: 'u', name: 'name' }, dir: 'ASC' }],
          input: {
            type: 'Filter',
            predicate: expr('o.year = 2024'),
            input: {
              type: 'Join',
              left: {
                type: 'Scan',
                table: 'users',
                binding: 'u',
                columns: ['id', 'name', 'city', 'signup_year'],
              },
              right: {
                type: 'Scan',
                table: 'orders',
                binding: 'o',
                columns: ['id', 'user_id', 'product_id', 'quantity', 'total', 'year'],
              },
              on: expr('u.id = o.user_id'),
            },
          },
        },
      },
    });
  });

  it('omits nodes for absent clauses and names computed columns colN', () => {
    const plan = planOf('SELECT price * 2, price AS doubled FROM products');
    expect(plan).toEqual({
      type: 'Project',
      columns: [
        {
          expr: {
            type: 'Binary',
            op: '*',
            left: { type: 'Column', table: 'products', name: 'price' },
            right: { type: 'Lit', value: 2 },
          },
          name: 'col1',
        },
        { expr: { type: 'Column', table: 'products', name: 'price' }, name: 'doubled' },
      ],
      input: {
        type: 'Scan',
        table: 'products',
        binding: 'products',
        columns: ['id', 'name', 'category', 'price'],
      },
    });
  });
});

describe('executePlan', () => {
  it('filters with three-valued logic: a null city matches neither = nor <>', () => {
    const matching = executePlan(planOf("SELECT name FROM users WHERE city = 'london'"), demoDb);
    expect(matching).toEqual([{ name: 'ada' }, { name: 'alan' }]);
    const notLondon = executePlan(planOf("SELECT name FROM users WHERE city <> 'london'"), demoDb);
    // edsger's null city is in neither result.
    expect(notLondon.map((r) => r.name)).toEqual(['grace', 'barbara', 'donald']);
  });

  it('IS NULL is the only way to match the null city', () => {
    expect(executePlan(planOf('SELECT name FROM users WHERE city IS NULL'), demoDb)).toEqual([
      { name: 'edsger' },
    ]);
  });

  it('treats division by zero as null (filtered out)', () => {
    expect(
      executePlan(planOf('SELECT name FROM users WHERE id / 0 = 1'), demoDb),
    ).toEqual([]);
  });

  it('joins with nested loops and evaluates computed projections', () => {
    const rows = executePlan(
      planOf(
        "SELECT u.name, o.total * 2 AS double_total FROM users u JOIN orders o ON u.id = o.user_id WHERE o.year = 2023",
      ),
      demoDb,
    );
    expect(rows).toEqual([
      { name: 'ada', double_total: 480 },
      { name: 'alan', double_total: 320 },
    ]);
  });

  it('sorts stably with nulls last in both directions', () => {
    const asc = executePlan(
      planOf('SELECT id, total FROM orders ORDER BY total ASC LIMIT 3'),
      demoDb,
    );
    expect(asc).toEqual([
      { id: 5, total: 60 },
      { id: 9, total: 80 },
      { id: 2, total: 120 },
    ]);
    const desc = executePlan(planOf('SELECT id, total FROM orders ORDER BY total DESC'), demoDb);
    // The null total is last even though DESC puts big values first.
    expect(desc[desc.length - 1]).toEqual({ id: 7, total: null });
    expect(desc[0]).toEqual({ id: 3, total: 500 });
  });

  it('ORDER BY may reference columns the projection drops', () => {
    const rows = executePlan(
      planOf('SELECT name FROM products ORDER BY price DESC LIMIT 2'),
      demoDb,
    );
    expect(rows).toEqual([{ name: 'compiler license' }, { name: 'desk' }]);
  });
});

describe('executeWithStats', () => {
  it('charges cells entering each operator', () => {
    const { rows, cost, operators } = executeWithStats(
      planOf('SELECT name FROM users WHERE signup_year > 2020'),
      demoDb,
    );
    expect(rows).toEqual([{ name: 'grace' }, { name: 'alan' }, { name: 'barbara' }]);
    // Scan: 6 rows × 4 columns; Filter: 6 rows × 4; Project: 3 rows × 4.
    expect(operators).toEqual([
      { label: 'Scan(users AS users)', cells: 24 },
      { label: 'Filter', cells: 24 },
      { label: 'Project', cells: 12 },
    ]);
    expect(cost).toBe(60);
  });
});

describe('validatePlan', () => {
  it('accepts every plan the planner builds', () => {
    expect(
      validatePlan(
        planOf('SELECT u.name FROM users u JOIN orders o ON u.id = o.user_id WHERE o.year = 2024'),
        shopCatalog,
      ),
    ).toEqual([]);
  });

  it('reports references a node input cannot supply', () => {
    const broken: Plan = {
      type: 'Filter',
      predicate: expr('o.year = 2024'),
      input: { type: 'Scan', table: 'users', binding: 'u', columns: shopCatalog.users },
    };
    expect(validatePlan(broken, shopCatalog)).toEqual([
      "Filter references 'o.year', which its input does not produce",
    ]);
  });
});
