import { describe, expect, it } from 'vitest';
import {
  benchDb,
  buildPlan,
  conjunctsOf,
  demoDb,
  executePlan,
  executeWithStats,
  optimizePlan,
  parseSelect,
  parseSqlExpr,
  pruneColumns,
  pushDownFilters,
  resolve,
  shopCatalog,
  simplifyExpr,
  simplifyPredicates,
  tokenizeSql,
  validatePlan,
  type Plan,
  type SqlExpr,
} from '../lang/sql';

const expr = (source: string): SqlExpr => parseSqlExpr(tokenizeSql(source));

function planOf(source: string): Plan {
  const { select, errors } = resolve(parseSelect(tokenizeSql(source)), shopCatalog);
  expect(errors).toEqual([]);
  return buildPlan(select!, shopCatalog);
}

describe('simplifyExpr', () => {
  it.each([
    ['TRUE AND a = 1', 'a = 1'],
    ['a = 1 AND TRUE', 'a = 1'],
    ['FALSE OR a = 1', 'a = 1'],
    ['1 = 1 AND a = 1', 'a = 1'],
    ['1 + 2 * 3 = 7', 'TRUE'],
    ['NOT TRUE', 'FALSE'],
    ['NULL IS NULL', 'TRUE'],
    ['NULL IS NOT NULL', 'FALSE'],
    ['a = 1 OR TRUE', 'TRUE'],
    ['FALSE AND a = 1', 'FALSE'],
  ])('simplifies %s to %s', (input, output) => {
    expect(simplifyExpr(expr(input))).toEqual(expr(output));
  });

  it('is Kleene-safe: FALSE absorbs AND even against NULL', () => {
    expect(simplifyExpr(expr('NULL AND FALSE'))).toEqual({ type: 'Lit', value: false });
  });

  it('folds strict operators with a literal NULL operand to NULL', () => {
    expect(simplifyExpr(expr('a + NULL'))).toEqual({ type: 'Lit', value: null });
    expect(simplifyExpr(expr('a = NULL'))).toEqual({ type: 'Lit', value: null });
  });

  it('leaves column comparisons alone', () => {
    const e = expr('a = b');
    expect(simplifyExpr(e)).toEqual(e);
  });
});

describe('simplifyPredicates', () => {
  it('removes a Filter whose predicate becomes TRUE', () => {
    const plan = planOf('SELECT name FROM users WHERE 1 = 1');
    expect(simplifyPredicates(plan)).toEqual(planOf('SELECT name FROM users'));
  });

  it('keeps a Filter whose predicate becomes FALSE', () => {
    const simplified = simplifyPredicates(planOf('SELECT name FROM users WHERE 1 = 2'));
    expect(simplified).toEqual({
      type: 'Project',
      columns: [{ expr: { type: 'Column', table: 'users', name: 'name' }, name: 'name' }],
      input: {
        type: 'Filter',
        predicate: { type: 'Lit', value: false },
        input: { type: 'Scan', table: 'users', binding: 'users', columns: shopCatalog.users },
      },
    });
    expect(executePlan(simplified, demoDb)).toEqual([]);
  });

  it('simplifies inside Join ON conditions', () => {
    const plan = planOf('SELECT u.name FROM users u JOIN orders o ON u.id = o.user_id AND TRUE');
    const simplified = simplifyPredicates(plan);
    expect(JSON.stringify(simplified)).toContain('"op":"="');
    expect(JSON.stringify(simplified)).not.toContain('"AND"');
  });
});

describe('pushDownFilters', () => {
  const query =
    "SELECT u.name, o.total FROM users u JOIN orders o ON u.id = o.user_id WHERE u.city = 'seattle' AND o.year = 2024 AND u.signup_year < o.year";

  it('pushes single-side conjuncts below the join, keeping cross-side ones above it', () => {
    const pushed = pushDownFilters(planOf(query));
    expect(pushed).toEqual({
      type: 'Project',
      columns: [
        { expr: { type: 'Column', table: 'u', name: 'name' }, name: 'name' },
        { expr: { type: 'Column', table: 'o', name: 'total' }, name: 'total' },
      ],
      input: {
        type: 'Filter',
        predicate: expr('u.signup_year < o.year'),
        input: {
          type: 'Join',
          left: {
            type: 'Filter',
            predicate: expr("u.city = 'seattle'"),
            input: { type: 'Scan', table: 'users', binding: 'u', columns: shopCatalog.users },
          },
          right: {
            type: 'Filter',
            predicate: expr('o.year = 2024'),
            input: { type: 'Scan', table: 'orders', binding: 'o', columns: shopCatalog.orders },
          },
          on: expr('u.id = o.user_id'),
        },
      },
    });
  });

  it('preserves results and reduces cost on the bench database', () => {
    const naive = planOf(query);
    const pushed = pushDownFilters(naive);
    expect(validatePlan(pushed, shopCatalog)).toEqual([]);
    const rows = executePlan(naive, benchDb);
    // Equivalence over an empty result would prove nothing.
    expect(rows.length).toBeGreaterThan(0);
    expect(executePlan(pushed, benchDb)).toEqual(rows);
    expect(executeWithStats(pushed, benchDb).cost).toBeLessThan(
      executeWithStats(naive, benchDb).cost,
    );
  });

  it('cascades through stacked joins', () => {
    const threeWay = planOf(
      "SELECT u.name FROM users u JOIN orders o ON u.id = o.user_id JOIN products p ON o.product_id = p.id WHERE p.category = 'hardware' AND u.city = 'london' AND o.quantity > 1",
    );
    const pushed = pushDownFilters(threeWay);
    expect(validatePlan(pushed, shopCatalog)).toEqual([]);
    expect(executePlan(pushed, demoDb)).toEqual(executePlan(threeWay, demoDb));
    // u.city lands on the users scan two joins down.
    const json = JSON.stringify(pushed);
    expect(json).toContain('"table":"users"');
    const usersScanFilter = (
      ((pushed as Extract<Plan, { type: 'Project' }>).input as Extract<Plan, { type: 'Join' }>)
        .left as Extract<Plan, { type: 'Join' }>
    ).left;
    expect(usersScanFilter.type).toBe('Filter');
  });

  it('leaves constant conjuncts at the join', () => {
    const pushed = pushDownFilters(
      planOf('SELECT u.name FROM users u JOIN orders o ON u.id = o.user_id WHERE 1 = 1 AND u.id > 2'),
    );
    const top = (pushed as Extract<Plan, { type: 'Project' }>).input;
    expect(top.type).toBe('Filter');
    expect((top as Extract<Plan, { type: 'Filter' }>).predicate).toEqual(expr('1 = 1'));
  });
});

describe('pruneColumns', () => {
  const query = 'SELECT u.name FROM users u JOIN orders o ON u.id = o.user_id';

  it('projects joined scans down to referenced columns with qualified names', () => {
    const pruned = pruneColumns(planOf(query));
    expect(pruned).toEqual({
      type: 'Project',
      columns: [{ expr: { type: 'Column', table: 'u', name: 'name' }, name: 'name' }],
      input: {
        type: 'Join',
        left: {
          type: 'Project',
          columns: [
            { expr: { type: 'Column', table: 'u', name: 'id' }, name: 'u.id' },
            { expr: { type: 'Column', table: 'u', name: 'name' }, name: 'u.name' },
          ],
          input: { type: 'Scan', table: 'users', binding: 'u', columns: shopCatalog.users },
        },
        right: {
          type: 'Project',
          columns: [{ expr: { type: 'Column', table: 'o', name: 'user_id' }, name: 'o.user_id' }],
          input: { type: 'Scan', table: 'orders', binding: 'o', columns: shopCatalog.orders },
        },
        on: expr('u.id = o.user_id'),
      },
    });
  });

  it('preserves results and reduces cost', () => {
    const naive = planOf(query);
    const pruned = pruneColumns(naive);
    expect(validatePlan(pruned, shopCatalog)).toEqual([]);
    const rows = executePlan(naive, benchDb);
    expect(rows.length).toBeGreaterThan(0);
    expect(executePlan(pruned, benchDb)).toEqual(rows);
    expect(executeWithStats(pruned, benchDb).cost).toBeLessThan(
      executeWithStats(naive, benchDb).cost,
    );
  });

  it('does not touch a single-table plan or wrap a scan needing every column', () => {
    const single = planOf('SELECT name FROM users WHERE city IS NULL');
    expect(pruneColumns(single)).toEqual(single);
    const allNeeded = planOf(
      'SELECT p.id, p.name, p.category, p.price FROM products p JOIN orders o ON p.id = o.product_id',
    );
    const pruned = pruneColumns(allNeeded);
    const left = ((pruned as Extract<Plan, { type: 'Project' }>).input as Extract<Plan, { type: 'Join' }>).left;
    expect(left.type).toBe('Scan');
  });
});

describe('optimizePlan', () => {
  const queries = [
    'SELECT name FROM users WHERE TRUE AND signup_year > 2020',
    "SELECT u.name, o.total FROM users u JOIN orders o ON u.id = o.user_id WHERE u.city = 'seattle' AND o.year = 2024",
    "SELECT u.name FROM users u JOIN orders o ON u.id = o.user_id JOIN products p ON o.product_id = p.id WHERE p.category = 'hardware' AND u.signup_year < 2023 ORDER BY u.name LIMIT 5",
    'SELECT name FROM users WHERE 1 = 2 AND city = \'london\'',
  ];

  it.each(queries)('preserves results on both databases: %s', (query) => {
    const naive = planOf(query);
    const optimized = optimizePlan(naive);
    expect(validatePlan(optimized, shopCatalog)).toEqual([]);
    expect(executePlan(optimized, demoDb)).toEqual(executePlan(naive, demoDb));
    expect(executePlan(optimized, benchDb)).toEqual(executePlan(naive, benchDb));
  });

  it('cuts cost by at least 5x on a pushdown-and-prune-friendly join', () => {
    const naive = planOf(queries[1]);
    const optimized = optimizePlan(naive);
    const naiveCost = executeWithStats(naive, benchDb).cost;
    const optimizedCost = executeWithStats(optimized, benchDb).cost;
    expect(optimizedCost * 5).toBeLessThan(naiveCost);
  });
});

describe('optimizer soundness under ill-typed logical operands', () => {
  // TRUE AND x → x is only meaning-preserving because the executor coerces
  // AND/OR/NOT operands consistently (non-true, non-null counts as false).
  // This query made the pre-coercion engine diverge: for the null city,
  // city = 'x' is null, and the inner AND folded differently than it ran.
  it.each([
    "SELECT name FROM users WHERE NOT ((TRUE AND signup_year) AND (city = 'x'))",
    'SELECT name FROM users WHERE signup_year AND TRUE',
    'SELECT name FROM users WHERE FALSE OR signup_year',
    'SELECT name FROM users WHERE 1 = 1 AND signup_year',
  ])('%s: optimized plan returns identical rows', (query) => {
    const naive = planOf(query);
    const optimized = optimizePlan(naive);
    expect(validatePlan(optimized, shopCatalog)).toEqual([]);
    expect(executePlan(optimized, demoDb)).toEqual(executePlan(naive, demoDb));
  });
});

describe('the bench database', () => {
  it('covers the full domains its generator promises', () => {
    const distinct = (rows: Record<string, unknown>[], key: string) =>
      new Set(rows.map((r) => r[key]));
    // The original LCG's low bit alternated, silently confining these
    // columns to half their domains (and emptying two capstone queries).
    expect(distinct(benchDb.products, 'category')).toEqual(
      new Set(['hardware', 'software', 'consumable', 'furniture']),
    );
    expect(distinct(benchDb.orders, 'year')).toEqual(
      new Set([2020, 2021, 2022, 2023, 2024, 2025]),
    );
    const quantities = distinct(benchDb.orders, 'quantity');
    expect([...quantities].some((q) => (q as number) % 2 === 0)).toBe(true);
    expect([...quantities].some((q) => (q as number) % 2 === 1)).toBe(true);
    const productIds = distinct(benchDb.orders, 'product_id');
    expect([...productIds].some((p) => (p as number) % 2 === 0)).toBe(true);
    expect([...productIds].some((p) => (p as number) % 2 === 1)).toBe(true);
    expect(benchDb.users.filter((u) => u.city === null).length).toBeGreaterThan(0);
    expect(benchDb.orders.filter((o) => o.total === null).length).toBeGreaterThan(0);
  });

  it('non-null totals derive from the ordered product', () => {
    for (const order of benchDb.orders) {
      if (order.total === null) continue;
      const product = benchDb.products[(order.product_id as number) - 1];
      expect(order.total).toBe((order.quantity as number) * (product.price as number));
    }
  });
});

describe('conjunctsOf', () => {
  it('flattens left-associated ANDs in order and leaves OR intact', () => {
    expect(conjunctsOf(expr('a = 1 AND b = 2 AND c = 3'))).toEqual([
      expr('a = 1'),
      expr('b = 2'),
      expr('c = 3'),
    ]);
    expect(conjunctsOf(expr('a = 1 OR b = 2'))).toEqual([expr('a = 1 OR b = 2')]);
  });
});
