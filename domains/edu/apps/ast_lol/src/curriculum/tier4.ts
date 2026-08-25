import {
  buildPlan,
  demoDb,
  executePlan,
  parseSelect,
  resolve,
  shopCatalog,
  tokenizeSql,
  type Catalog,
  type Database,
  type Plan,
  type Select,
} from '../lang/sql';
import type { ChallengeDef, LessonDef, Tier } from './types';

/** Tier 4 — Meaning: name resolution, logical plans, execution. */

const parseSql = (q: string): Select => parseSelect(tokenizeSql(q));

function resolved(q: string, catalog: Catalog = shopCatalog): Select {
  const r = resolve(parseSql(q), catalog);
  if (r.select === null) {
    throw new Error(`fixture query failed to resolve: ${q} — ${JSON.stringify(r.errors)}`);
  }
  return r.select;
}

function planOf(q: string): Plan {
  return buildPlan(resolved(q), shopCatalog);
}

const resolutionLesson: LessonDef = {
  id: 'resolution',
  title: 'Name resolution',
  summary: 'Catalogs, bindings, ambiguity — and errors as data, not exceptions.',
  reading: [
    {
      title: 'PostgreSQL — The Parser Stage',
      url: 'https://www.postgresql.org/docs/current/parser-stage.html',
      note: 'parse vs. analyze in a production database: resolution is the second half',
    },
    {
      title: 'Wikipedia — Symbol table',
      url: 'https://en.wikipedia.org/wiki/Symbol_table',
      note: 'the compiler-side name for the bindings map',
    },
    {
      title: 'CMU 15-445 — Database Systems',
      url: 'https://15445.courses.cs.cmu.edu/',
      note: 'the open course this tier keeps pointing into',
    },
  ],
};

const plansLesson: LessonDef = {
  id: 'logical-plans',
  title: 'Logical plans',
  summary: 'Relational algebra: turning what a query says into what an engine does.',
  reading: [
    {
      title: 'Wikipedia — Relational algebra',
      url: 'https://en.wikipedia.org/wiki/Relational_algebra',
      note: 'the operators, in their original notation',
    },
    {
      title: 'Apache Calcite — Algebra',
      url: 'https://calcite.apache.org/docs/algebra.html',
      note: 'an industrial optimizer’s plan nodes and rewrite rules',
    },
    {
      title: 'PostgreSQL — EXPLAIN',
      url: 'https://www.postgresql.org/docs/current/using-explain.html',
      note: 'read real plans for the queries you type',
    },
  ],
};

const executionLesson: LessonDef = {
  id: 'execution',
  title: 'Executing plans',
  summary: 'Rows through operators: three-valued logic, nested loops, and the cost of it all.',
  reading: [
    {
      title: 'Wikipedia — Three-valued logic',
      url: 'https://en.wikipedia.org/wiki/Three-valued_logic',
      note: 'Kleene logic: the algebra NULL drags in',
    },
    {
      title: 'Wikipedia — Nested loop join',
      url: 'https://en.wikipedia.org/wiki/Nested_loop_join',
    },
    {
      title: 'Berkeley CS186 — Introduction to Database Systems',
      url: 'https://cs186berkeley.net/',
      note: 'iterators, joins, and query processing, as a full open course',
    },
  ],
};

const sqlResolve: ChallengeDef = {
  id: 'sql-resolve',
  title: 'Resolve names against a catalog',
  summary: 'Bindings, * expansion, and precise errors — semantic analysis in one function.',
  signature: 'resolve(select, catalog) → { select, errors }',
  entry: 'resolve',
  difficulty: 3,
  starter: `// catalog: { tableName: [columnName, ...] }
// Return { select, errors }.
//
// Phase 1 — tables, in FROM-then-JOIN order:
//   unknown table            -> { kind: 'unknown-table', name: table }
//   duplicate binding name   -> { kind: 'duplicate-binding', name: binding }
//   (binding = alias if present, else the table name; a table with an
//    error contributes no binding)
//   Any table error: return { select: null, errors } without checking
//   columns.
// Phase 2 — columns, in clause order: SELECT list, each JOIN's ON,
//   WHERE, ORDER BY:
//   qualified, unknown binding or column -> { kind: 'unknown-column', name: 'q.name' }
//   bare, owned by no binding            -> { kind: 'unknown-column', name: name }
//   bare, owned by 2+ bindings           -> { kind: 'ambiguous-column', name: name }
//   Any column error: return { select: null, errors }.
// Success: return a NEW Select (do not mutate the input) where every
// Column.table is its binding name and '*' is expanded to every binding's
// columns in binding order, each { expr: Column, alias: null }.
function resolve(select, catalog) {
  const errors = [];
  // TODO
}`,
  solution: `function resolve(select, catalog) {
  const errors = [];
  const refs = [select.from].concat(select.joins);
  const bindings = new Map();
  for (const ref of refs) {
    const binding = ref.alias !== null ? ref.alias : ref.table;
    const columns = catalog[ref.table];
    if (columns === undefined) {
      errors.push({ kind: 'unknown-table', name: ref.table });
      continue;
    }
    if (bindings.has(binding)) {
      errors.push({ kind: 'duplicate-binding', name: binding });
      continue;
    }
    bindings.set(binding, columns);
  }
  if (errors.length > 0) return { select: null, errors: errors };

  function resolveExpr(expr) {
    switch (expr.type) {
      case 'Lit':
        return expr;
      case 'Column': {
        if (expr.table !== null) {
          const columns = bindings.get(expr.table);
          if (columns === undefined || !columns.includes(expr.name)) {
            errors.push({ kind: 'unknown-column', name: expr.table + '.' + expr.name });
          }
          return { type: 'Column', table: expr.table, name: expr.name };
        }
        const owners = [];
        for (const entry of bindings) {
          if (entry[1].includes(expr.name)) owners.push(entry[0]);
        }
        if (owners.length === 0) {
          errors.push({ kind: 'unknown-column', name: expr.name });
          return { type: 'Column', table: null, name: expr.name };
        }
        if (owners.length > 1) {
          errors.push({ kind: 'ambiguous-column', name: expr.name });
          return { type: 'Column', table: null, name: expr.name };
        }
        return { type: 'Column', table: owners[0], name: expr.name };
      }
      case 'Binary':
        return { type: 'Binary', op: expr.op,
          left: resolveExpr(expr.left), right: resolveExpr(expr.right) };
      case 'Not':
        return { type: 'Not', operand: resolveExpr(expr.operand) };
      case 'Unary':
        return { type: 'Unary', op: '-', operand: resolveExpr(expr.operand) };
      case 'IsNull':
        return { type: 'IsNull', operand: resolveExpr(expr.operand), negated: expr.negated };
    }
  }

  let columns;
  if (select.columns === '*') {
    columns = [];
    for (const entry of bindings) {
      for (const name of entry[1]) {
        columns.push({ expr: { type: 'Column', table: entry[0], name: name }, alias: null });
      }
    }
  } else {
    columns = select.columns.map(function (c) {
      return { expr: resolveExpr(c.expr), alias: c.alias };
    });
  }
  const joins = select.joins.map(function (j) {
    return { table: j.table, alias: j.alias, on: resolveExpr(j.on) };
  });
  const where = select.where === null ? null : resolveExpr(select.where);
  const orderBy = select.orderBy.map(function (k) {
    return { expr: resolveExpr(k.expr), dir: k.dir };
  });

  if (errors.length > 0) return { select: null, errors: errors };
  return {
    select: { type: 'Select', columns: columns,
      from: { table: select.from.table, alias: select.from.alias },
      joins: joins, where: where, orderBy: orderBy, limit: select.limit },
    errors: [],
  };
}`,
  tests: [
    {
      name: 'annotates every column with its binding',
      args: [parseSql('SELECT name FROM users WHERE city IS NULL ORDER BY signup_year'), shopCatalog],
      expected: resolve(
        parseSql('SELECT name FROM users WHERE city IS NULL ORDER BY signup_year'),
        shopCatalog,
      ),
    },
    {
      name: 'expands * to all bindings’ columns in binding order',
      args: [parseSql('SELECT * FROM users u JOIN orders o ON u.id = o.user_id'), shopCatalog],
      expected: resolve(
        parseSql('SELECT * FROM users u JOIN orders o ON u.id = o.user_id'),
        shopCatalog,
      ),
      hint: 'FROM’s binding first, then each JOIN in order; within a binding, the catalog’s column order.',
    },
    {
      name: 'a bare column owned by one binding resolves to it',
      args: [parseSql('SELECT city FROM users JOIN orders o ON user_id = 1'), shopCatalog],
      expected: resolve(parseSql('SELECT city FROM users JOIN orders o ON user_id = 1'), shopCatalog),
    },
    {
      name: 'an unknown table stops before column checking',
      args: [parseSql('SELECT nope FROM userz'), shopCatalog],
      expected: { select: null, errors: [{ kind: 'unknown-table', name: 'userz' }] },
      hint: 'Phase 1 errors return immediately — `nope` is never reported.',
    },
    {
      name: 'duplicate bindings are phase-1 errors',
      args: [
        parseSql('SELECT 1 FROM users u JOIN orders u ON 1 = 1'),
        shopCatalog,
      ],
      expected: {
        select: null,
        errors: [{ kind: 'duplicate-binding', name: 'u' }],
      },
    },
    {
      name: 'a qualified column with the wrong binding or column errors by full name',
      args: [parseSql('SELECT u.nope FROM users u'), shopCatalog],
      expected: { select: null, errors: [{ kind: 'unknown-column', name: 'u.nope' }] },
    },
    {
      name: 'an ambiguous bare column names itself',
      args: [parseSql('SELECT id FROM users u JOIN orders o ON u.id = o.user_id'), shopCatalog],
      expected: { select: null, errors: [{ kind: 'ambiguous-column', name: 'id' }] },
      hint: 'Both users and orders own an `id`; a bare `id` cannot pick one.',
    },
    {
      name: 'column errors report in clause order',
      args: [
        parseSql(
          'SELECT u.nope, mystery FROM users u JOIN orders o ON u.id = o.user_id WHERE id > 0 ORDER BY ghost',
        ),
        shopCatalog,
      ],
      expected: {
        select: null,
        errors: [
          { kind: 'unknown-column', name: 'u.nope' },
          { kind: 'unknown-column', name: 'mystery' },
          { kind: 'ambiguous-column', name: 'id' },
          { kind: 'unknown-column', name: 'ghost' },
        ],
      },
      hint: 'Walk SELECT list, then each JOIN’s ON, then WHERE, then ORDER BY — collecting, not throwing.',
    },
    {
      name: 'aliases shadow table names as bindings',
      args: [parseSql('SELECT users.id FROM users u'), shopCatalog],
      expected: { select: null, errors: [{ kind: 'unknown-column', name: 'users.id' }] },
      hint: 'Once aliased to u, the binding `users` no longer exists.',
    },
  ],
  custom: {
    describe:
      'An AstQL query (against the shop catalog: users, orders, products); the reference resolver supplies the expected result.',
    placeholder: '"SELECT u.name, total FROM users u JOIN orders o ON u.id = o.user_id"',
    toArgs: (values) => [parseSql(String(values[0])), shopCatalog],
  },
};

const sqlPlan: ChallengeDef = {
  id: 'sql-plan',
  title: 'Build the logical plan',
  summary: 'Resolved AST → canonical operator tree: Scan, Join, Filter, Sort, Project, Limit.',
  signature: 'buildPlan(select, catalog) → Plan',
  entry: 'buildPlan',
  difficulty: 3,
  starter: `// Plan nodes:
//   { type: 'Scan', table, binding, columns }   // columns = catalog[table]
//   { type: 'Join', left, right, on }           // left-deep, in JOIN order
//   { type: 'Filter', input, predicate }
//   { type: 'Sort', input, keys }               // keys = orderBy entries
//   { type: 'Project', input, columns: [{ expr, name }] }
//   { type: 'Limit', input, count }
// Canonical order, bottom to top:
//   Scan(from) → Join per JOIN clause → Filter (if where) →
//   Sort (if orderBy) → Project (always) → Limit (if limit)
// Sort sits BELOW Project so ORDER BY can use any resolved column,
// including ones the SELECT list drops.
// Project output names: alias if present; else a Column's own name;
// else 'col' + (1-based index).
// The input Select is already resolved: '*' is expanded, every
// Column.table is a binding.
function buildPlan(select, catalog) {
  // TODO
}`,
  solution: `function buildPlan(select, catalog) {
  function scan(table, alias) {
    return { type: 'Scan', table: table,
      binding: alias !== null ? alias : table,
      columns: catalog[table] };
  }
  let plan = scan(select.from.table, select.from.alias);
  for (const join of select.joins) {
    plan = { type: 'Join', left: plan, right: scan(join.table, join.alias), on: join.on };
  }
  if (select.where !== null) {
    plan = { type: 'Filter', input: plan, predicate: select.where };
  }
  if (select.orderBy.length > 0) {
    plan = { type: 'Sort', input: plan, keys: select.orderBy };
  }
  plan = {
    type: 'Project',
    input: plan,
    columns: select.columns.map(function (c, i) {
      let name;
      if (c.alias !== null) name = c.alias;
      else if (c.expr.type === 'Column') name = c.expr.name;
      else name = 'col' + (i + 1);
      return { expr: c.expr, name: name };
    }),
  };
  if (select.limit !== null) {
    plan = { type: 'Limit', input: plan, count: select.limit };
  }
  return plan;
}`,
  tests: [
    {
      name: 'the full canonical stack',
      args: [
        resolved(
          'SELECT u.name FROM users u JOIN orders o ON u.id = o.user_id WHERE o.year = 2024 ORDER BY u.name LIMIT 5',
        ),
        shopCatalog,
      ],
      expected: planOf(
        'SELECT u.name FROM users u JOIN orders o ON u.id = o.user_id WHERE o.year = 2024 ORDER BY u.name LIMIT 5',
      ),
      hint: 'Build bottom-up and wrap: Scan, then each Join, then Filter, Sort, Project, Limit.',
    },
    {
      name: 'absent clauses contribute no node — but Project is always there',
      args: [resolved('SELECT name FROM users'), shopCatalog],
      expected: planOf('SELECT name FROM users'),
    },
    {
      name: 'joins nest left-deep in source order',
      args: [
        resolved(
          'SELECT u.name FROM users u JOIN orders o ON u.id = o.user_id JOIN products p ON o.product_id = p.id',
        ),
        shopCatalog,
      ],
      expected: planOf(
        'SELECT u.name FROM users u JOIN orders o ON u.id = o.user_id JOIN products p ON o.product_id = p.id',
      ),
      hint: 'The second Join’s left child is the first Join, not a Scan.',
    },
    {
      name: 'Sort sits below Project (ORDER BY may use dropped columns)',
      args: [resolved('SELECT name FROM products ORDER BY price DESC'), shopCatalog],
      expected: planOf('SELECT name FROM products ORDER BY price DESC'),
      hint: 'Sorting after projecting would lose `price` before the comparator sees it.',
    },
    {
      name: 'output naming: alias, column name, colN',
      args: [
        resolved('SELECT price * 2, price AS doubled, name FROM products'),
        shopCatalog,
      ],
      expected: planOf('SELECT price * 2, price AS doubled, name FROM products'),
      hint: 'col1, col2 … count positions in the SELECT list (1-based), not just the unnamed ones.',
    },
    {
      name: 'scans carry the binding and the catalog column list',
      args: [resolved('SELECT u.id FROM users u'), shopCatalog],
      expected: planOf('SELECT u.id FROM users u'),
    },
    {
      name: 'a resolved * projects every qualified column',
      args: [
        resolved('SELECT * FROM users u JOIN orders o ON u.id = o.user_id LIMIT 1'),
        shopCatalog,
      ],
      expected: planOf('SELECT * FROM users u JOIN orders o ON u.id = o.user_id LIMIT 1'),
    },
  ],
  custom: {
    describe:
      'An AstQL query (shop catalog); it is parsed and resolved for you, and the reference planner supplies the expected plan.',
    placeholder: '"SELECT name FROM users WHERE city = \'london\' ORDER BY name"',
    toArgs: (values) => [resolved(String(values[0])), shopCatalog],
  },
};

const sqlExecute: ChallengeDef = {
  id: 'sql-execute',
  title: 'Execute the plan',
  summary: 'A working query engine: 3VL predicates, nested-loop joins, stable sorts.',
  signature: 'execute(plan, db) → Row[]',
  entry: 'execute',
  difficulty: 4,
  timeoutMs: 8000,
  starter: `// db: { tableName: [{ column: value, ... }, ...] }
// Rows inside the plan use binding-qualified keys: Scan(users AS u)
// emits { 'u.id': 1, 'u.name': 'ada', ... }. Project emits its output
// names as the keys.
//
// Expression evaluation over a row (three-valued logic):
//   Column       -> row[table + '.' + name]
//   Lit          -> its value
//   + - *        -> null if either side is not a number (null included)
//   /            -> like + - *, and x / 0 is null  (SQL flavor, not JS!)
//   = <> < <= > >= -> null if either side is null OR types differ;
//                     otherwise plain JS comparison
//   AND/OR/NOT coerce each operand first — true stays true, null stays
//   null, anything else counts as false — and always return true, false,
//   or null. Then, over the coerced values:
//   AND  -> false if either side is false; else null if either is null;
//           else true                                (Kleene)
//   OR   -> true if either side is true; else null if either is null;
//           else false
//   NOT  -> null stays null; else the flipped boolean
//   -x   -> null unless x is a number
//   x IS [NOT] NULL -> always true or false, never null
//
// Operators:
//   Scan    -> one row per table row, keys binding-qualified; a column
//              missing from a row reads as null
//   Join    -> nested loop over left then right; merge {...l, ...r};
//              keep the pair only when ON evaluates to exactly true
//   Filter  -> keep rows where the predicate is exactly true
//   Sort    -> stable; compare keys in order; DESC flips the comparison
//              but nulls sort LAST regardless of direction
//   Limit   -> first count rows
//   Project -> evaluate each column expr, output under its name
function execute(plan, db) {
  // TODO
}`,
  solution: `function execute(plan, db) {
  // AND/OR/NOT operand coercion: true, null, or (anything else) false.
  function truth(v) {
    return v === true ? true : v === null ? null : false;
  }
  function evalExpr(expr, row) {
    switch (expr.type) {
      case 'Lit':
        return expr.value;
      case 'Column': {
        const v = row[expr.table + '.' + expr.name];
        return v === undefined ? null : v;
      }
      case 'Unary': {
        const v = evalExpr(expr.operand, row);
        return typeof v === 'number' ? -v : null;
      }
      case 'Not': {
        const v = truth(evalExpr(expr.operand, row));
        return v === null ? null : !v;
      }
      case 'IsNull': {
        const v = evalExpr(expr.operand, row);
        return (v === null) !== expr.negated;
      }
      case 'Binary': {
        const l = evalExpr(expr.left, row);
        const r = evalExpr(expr.right, row);
        switch (expr.op) {
          case 'AND': {
            const lt = truth(l);
            const rt = truth(r);
            if (lt === false || rt === false) return false;
            if (lt === null || rt === null) return null;
            return true;
          }
          case 'OR': {
            const lt = truth(l);
            const rt = truth(r);
            if (lt === true || rt === true) return true;
            if (lt === null || rt === null) return null;
            return false;
          }
          case '+': case '-': case '*': case '/': {
            if (typeof l !== 'number' || typeof r !== 'number') return null;
            if (expr.op === '+') return l + r;
            if (expr.op === '-') return l - r;
            if (expr.op === '*') return l * r;
            return r === 0 ? null : l / r;
          }
          default: {
            if (l === null || r === null) return null;
            if (typeof l !== typeof r) return null;
            if (expr.op === '=') return l === r;
            if (expr.op === '<>') return l !== r;
            if (expr.op === '<') return l < r;
            if (expr.op === '<=') return l <= r;
            if (expr.op === '>') return l > r;
            return l >= r;
          }
        }
      }
    }
  }
  switch (plan.type) {
    case 'Scan': {
      return db[plan.table].map(function (r) {
        const out = {};
        for (const column of plan.columns) {
          const v = r[column];
          out[plan.binding + '.' + column] = v === undefined ? null : v;
        }
        return out;
      });
    }
    case 'Join': {
      const left = execute(plan.left, db);
      const right = execute(plan.right, db);
      const rows = [];
      for (const l of left) {
        for (const r of right) {
          const merged = Object.assign({}, l, r);
          if (evalExpr(plan.on, merged) === true) rows.push(merged);
        }
      }
      return rows;
    }
    case 'Filter':
      return execute(plan.input, db).filter(function (r) {
        return evalExpr(plan.predicate, r) === true;
      });
    case 'Sort': {
      const input = execute(plan.input, db);
      const decorated = input.map(function (row) {
        return { row: row, keys: plan.keys.map(function (k) { return evalExpr(k.expr, row); }) };
      });
      decorated.sort(function (a, b) {
        for (let i = 0; i < plan.keys.length; i++) {
          const av = a.keys[i];
          const bv = b.keys[i];
          if (av === null && bv === null) continue;
          if (av === null) return 1;
          if (bv === null) return -1;
          let cmp = 0;
          if (av < bv) cmp = -1;
          else if (av > bv) cmp = 1;
          if (cmp !== 0) return plan.keys[i].dir === 'DESC' ? -cmp : cmp;
        }
        return 0;
      });
      return decorated.map(function (d) { return d.row; });
    }
    case 'Limit':
      return execute(plan.input, db).slice(0, plan.count);
    case 'Project':
      return execute(plan.input, db).map(function (row) {
        const out = {};
        for (const col of plan.columns) out[col.name] = evalExpr(col.expr, row);
        return out;
      });
  }
}`,
  tests: [
    {
      name: 'scan + project with qualified keys in between',
      args: [planOf('SELECT name FROM users'), demoDb],
      expected: executePlan(planOf('SELECT name FROM users'), demoDb),
    },
    {
      name: '3VL: a null city matches neither = nor <>',
      args: [planOf("SELECT name FROM users WHERE city <> 'london'"), demoDb],
      expected: executePlan(planOf("SELECT name FROM users WHERE city <> 'london'"), demoDb),
      hint: 'null <> anything is null, and Filter keeps only exactly-true rows — edsger (null city) is in neither the = nor the <> result.',
    },
    {
      name: 'IS NULL is how you find the null city',
      args: [planOf('SELECT name FROM users WHERE city IS NULL'), demoDb],
      expected: [{ name: 'edsger' }],
    },
    {
      name: 'division by zero is null here, not Infinity',
      args: [planOf('SELECT name FROM users WHERE id / 0 = 1'), demoDb],
      expected: [],
      hint: 'This is the deliberate contrast with Expr: SQL semantics say x / 0 has no value. null = 1 is null, and the Filter drops it.',
    },
    {
      name: 'Kleene: NULL AND FALSE is false, so NOT(…) can still be true',
      args: [
        planOf("SELECT name FROM users WHERE NOT (city = 'nowhere' AND 1 = 2)"),
        demoDb,
      ],
      expected: executePlan(
        planOf("SELECT name FROM users WHERE NOT (city = 'nowhere' AND 1 = 2)"),
        demoDb,
      ),
      hint: 'For edsger, city = \'nowhere\' is null — but null AND false is false, and NOT false is true. Short-circuiting on the null loses this row.',
    },
    {
      name: 'nested-loop join: left drives, right rescans, ON keeps exact-true',
      args: [
        planOf('SELECT u.name, o.total FROM users u JOIN orders o ON u.id = o.user_id WHERE o.year = 2023'),
        demoDb,
      ],
      expected: executePlan(
        planOf('SELECT u.name, o.total FROM users u JOIN orders o ON u.id = o.user_id WHERE o.year = 2023'),
        demoDb,
      ),
      hint: 'Row order is part of the contract: for each left row in order, each right row in order.',
    },
    {
      name: 'the null total joins nothing through o.total > 0',
      args: [
        planOf('SELECT o.id FROM orders o JOIN users u ON u.id = o.user_id AND o.total > 0'),
        demoDb,
      ],
      expected: executePlan(
        planOf('SELECT o.id FROM orders o JOIN users u ON u.id = o.user_id AND o.total > 0'),
        demoDb,
      ),
    },
    {
      name: 'sort is stable and DESC still puts nulls last',
      args: [planOf('SELECT id, total FROM orders ORDER BY total DESC'), demoDb],
      expected: executePlan(planOf('SELECT id, total FROM orders ORDER BY total DESC'), demoDb),
      hint: 'Handle null keys before applying the direction flip — DESC negates the comparison, and a naive flip sends nulls first.',
    },
    {
      name: 'multi-key sort: later keys break ties only',
      args: [planOf('SELECT id FROM orders ORDER BY year DESC, total ASC'), demoDb],
      expected: executePlan(planOf('SELECT id FROM orders ORDER BY year DESC, total ASC'), demoDb),
    },
    {
      name: 'ORDER BY can use a column the projection drops',
      args: [planOf('SELECT name FROM products ORDER BY price DESC LIMIT 2'), demoDb],
      expected: [{ name: 'compiler license' }, { name: 'desk' }],
    },
    {
      name: 'limit truncates after everything else',
      args: [planOf('SELECT id FROM orders ORDER BY id DESC LIMIT 3'), demoDb],
      expected: executePlan(planOf('SELECT id FROM orders ORDER BY id DESC LIMIT 3'), demoDb),
    },
    {
      name: 'computed projections evaluate per row',
      args: [planOf('SELECT quantity * 2 AS double_qty, id FROM orders LIMIT 3'), demoDb],
      expected: executePlan(planOf('SELECT quantity * 2 AS double_qty, id FROM orders LIMIT 3'), demoDb),
    },
    {
      name: 'a three-way join stays in plan order',
      args: [
        planOf(
          "SELECT u.name, p.name AS product FROM users u JOIN orders o ON u.id = o.user_id JOIN products p ON o.product_id = p.id WHERE p.category = 'hardware'",
        ),
        demoDb,
      ],
      expected: executePlan(
        planOf(
          "SELECT u.name, p.name AS product FROM users u JOIN orders o ON u.id = o.user_id JOIN products p ON o.product_id = p.id WHERE p.category = 'hardware'",
        ),
        demoDb,
      ),
    },
  ],
  custom: {
    describe:
      'An AstQL query (shop catalog); it is planned for you and run against the demo database. The reference engine supplies the expected rows.',
    placeholder: '"SELECT name, city FROM users WHERE signup_year > 2020 ORDER BY name"',
    toArgs: (values) => [planOf(String(values[0])), demoDb as Database],
  },
};

export const tier4: Tier = {
  id: 'tier4',
  number: 4,
  title: 'Meaning',
  subtitle: 'Resolve names, build logical plans, and execute them over real rows.',
  steps: [
    { kind: 'lesson', lesson: resolutionLesson },
    { kind: 'challenge', challenge: sqlResolve },
    { kind: 'lesson', lesson: plansLesson },
    { kind: 'challenge', challenge: sqlPlan },
    { kind: 'lesson', lesson: executionLesson },
    { kind: 'challenge', challenge: sqlExecute },
  ],
};
