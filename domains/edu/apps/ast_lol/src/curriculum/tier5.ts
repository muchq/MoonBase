import { firstDiff, show, showPretty } from '../grader/deepEqual';
import type { CheckFn } from '../grader/types';
import {
  benchDb,
  buildPlan,
  demoDb,
  executePlan,
  executeWithStats,
  optimizePlan,
  parseSelect,
  pruneColumns,
  pushDownFilters,
  resolve,
  shopCatalog,
  simplifyPredicates,
  tokenizeSql,
  validatePlan,
  type Plan,
  type Select,
} from '../lang/sql';
import type { ChallengeDef, LessonDef, Tier } from './types';

/** Tier 5 — Optimization: meaning-preserving rewrites judged by a cost model. */

function resolved(q: string): Select {
  const r = resolve(parseSelect(tokenizeSql(q)), shopCatalog);
  if (r.select === null) {
    throw new Error(`fixture query failed to resolve: ${q} — ${JSON.stringify(r.errors)}`);
  }
  return r.select;
}

const planOf = (q: string): Plan => buildPlan(resolved(q), shopCatalog);

const optimizersLesson: LessonDef = {
  id: 'optimizers',
  title: 'Optimizers: rewrites that pay rent',
  summary: 'Equivalence obligations, rule pipelines, and a cost model to keep score.',
  reading: [
    {
      title: 'SQLite — The SQLite Query Optimizer Overview',
      url: 'https://sqlite.org/optoverview.html',
      note: 'production pushdown and friends, plainly written',
    },
    {
      title: 'SQLite — The Next-Generation Query Planner',
      url: 'https://sqlite.org/queryplanner-ng.html',
      note: 'what changes when cost estimates drive the search',
    },
    {
      title: 'PostgreSQL — Planner/Optimizer',
      url: 'https://www.postgresql.org/docs/current/planner-optimizer.html',
    },
    {
      title: 'Wikipedia — Query optimization',
      url: 'https://en.wikipedia.org/wiki/Query_optimization',
    },
    {
      title: 'Apache Calcite — Algebra',
      url: 'https://calcite.apache.org/docs/algebra.html',
      note: 'rule-based rewriting as a reusable library',
    },
  ],
};

/**
 * Traversal helpers earlier challenges already had users write; provided
 * here so the optimizer challenges are about the rewrites, not the walks.
 */
const OPTIMIZER_PRELUDE = `// ---- provided helpers ----
// exprRefs(expr)     -> Set of 'binding.column' strings the expr references
// exprBindings(expr) -> Set of binding names the expr references
// planBindings(plan) -> Set of Scan bindings anywhere in the subtree
function exprRefs(expr) {
  const out = new Set();
  (function walk(e) {
    if (e.type === 'Column') out.add(e.table + '.' + e.name);
    else if (e.type === 'Binary') { walk(e.left); walk(e.right); }
    else if (e.type === 'Not' || e.type === 'IsNull' || e.type === 'Unary') walk(e.operand);
  })(expr);
  return out;
}
function exprBindings(expr) {
  const out = new Set();
  for (const ref of exprRefs(expr)) out.add(ref.slice(0, ref.indexOf('.')));
  return out;
}
function planBindings(plan) {
  if (plan.type === 'Scan') return new Set([plan.binding]);
  if (plan.type === 'Join') {
    const left = planBindings(plan.left);
    for (const b of planBindings(plan.right)) left.add(b);
    return left;
  }
  return planBindings(plan.input);
}
// ---- end provided ----`;

/** Rows-preserved + cost-reduced check, with the per-operator bill on failure. */
function equivalenceAndCostCheck(opts: { label: string; mustReduce: boolean }): CheckFn {
  return (actual, ctx) => {
    const naive = ctx.args[0] as Plan;
    const plan = actual as Plan;
    const problems = validatePlan(plan, shopCatalog);
    if (problems.length > 0) {
      return {
        pass: false,
        message: `Your plan is not well-formed: ${problems.join('; ')}.`,
      };
    }
    const expectedRows = executePlan(naive, benchDb);
    let stats;
    try {
      stats = executeWithStats(plan, benchDb);
    } catch (e) {
      return {
        pass: false,
        message: `Executing your plan failed: ${e instanceof Error ? e.message : show(e)}`,
      };
    }
    const diff = firstDiff(expectedRows, stats.rows);
    if (diff !== null) {
      return {
        pass: false,
        message: `Your plan changes the result on the bench database: first difference at rows${diff.path} (${diff.reason}). An optimizer's first obligation is equivalence.`,
        expectedText: showPretty(expectedRows.slice(0, 5)) + '\n… (first rows shown)',
        actualText: showPretty(stats.rows.slice(0, 5)) + '\n… (first rows shown)',
      };
    }
    const naiveCost = executeWithStats(naive, benchDb).cost;
    const bill = stats.operators.map((op) => `${op.label}: ${op.cells}`).join('  ·  ');
    if (opts.mustReduce && stats.cost >= naiveCost) {
      return {
        pass: false,
        message: `Rows are right, but nothing got cheaper: your plan processes ${stats.cost} cells on the bench database; the naive plan processes ${naiveCost}. ${opts.label} Your plan's bill — ${bill}`,
      };
    }
    return { pass: true };
  };
}

const sqlSimplify: ChallengeDef = {
  id: 'sql-simplify',
  title: 'Simplify predicates',
  summary: 'Constant folding with three-valued caution: TRUE AND p, FALSE absorption, strict nulls.',
  signature: 'simplifyPlan(plan) → Plan',
  entry: 'simplifyPlan',
  difficulty: 3,
  starter: `// Simplify every predicate expression in the plan — Filter predicates
// and Join ON conditions — bottom-up:
//   1. Strict ops (+ - * / and the comparisons): both sides Lit -> fold
//      to a Lit using the executor's semantics (x/0 -> null, mismatched
//      types compare to null). EITHER side Lit null -> Lit null (strict
//      ops are null-in, null-out).
//   2. AND: a Lit false side -> Lit false (even if the other side is
//      null — Kleene). A Lit true side -> the other side. Both Lit ->
//      fold.
//   3. OR: mirror image (true absorbs, false drops out).
//   4. NOT / unary - / IS [NOT] NULL over a Lit -> fold.
// Then: a Filter whose predicate became Lit true disappears; a Filter
// that became Lit false or Lit null STAYS (it keeps zero rows — there is
// no empty-relation node to replace it with).
// Rebuild, never mutate.
function simplifyPlan(plan) {
  // TODO: an expression simplifier + a plan walker around it.
}`,
  solution: `function simplifyPlan(plan) {
  function evalConst(expr) {
    switch (expr.type) {
      case 'Lit': return expr.value;
      case 'Unary': {
        const v = evalConst(expr.operand);
        return typeof v === 'number' ? -v : null;
      }
      case 'Not': {
        const v = evalConst(expr.operand);
        return v === null ? null : v !== true;
      }
      case 'IsNull': {
        const v = evalConst(expr.operand);
        return (v === null) !== expr.negated;
      }
      case 'Binary': {
        const l = evalConst(expr.left);
        const r = evalConst(expr.right);
        switch (expr.op) {
          case 'AND':
            if (l === false || r === false) return false;
            if (l === null || r === null) return null;
            return l === true && r === true;
          case 'OR':
            if (l === true || r === true) return true;
            if (l === null || r === null) return null;
            return false;
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
  function isLit(e) { return e.type === 'Lit'; }
  function simplify(expr) {
    switch (expr.type) {
      case 'Lit':
      case 'Column':
        return expr;
      case 'Not': {
        const operand = simplify(expr.operand);
        const node = { type: 'Not', operand: operand };
        return isLit(operand) ? { type: 'Lit', value: evalConst(node) } : node;
      }
      case 'Unary': {
        const operand = simplify(expr.operand);
        const node = { type: 'Unary', op: '-', operand: operand };
        return isLit(operand) ? { type: 'Lit', value: evalConst(node) } : node;
      }
      case 'IsNull': {
        const operand = simplify(expr.operand);
        const node = { type: 'IsNull', operand: operand, negated: expr.negated };
        return isLit(operand) ? { type: 'Lit', value: evalConst(node) } : node;
      }
      case 'Binary': {
        const left = simplify(expr.left);
        const right = simplify(expr.right);
        const node = { type: 'Binary', op: expr.op, left: left, right: right };
        if (expr.op === 'AND') {
          if ((isLit(left) && left.value === false) || (isLit(right) && right.value === false)) {
            return { type: 'Lit', value: false };
          }
          if (isLit(left) && left.value === true) return right;
          if (isLit(right) && right.value === true) return left;
          if (isLit(left) && isLit(right)) return { type: 'Lit', value: evalConst(node) };
          return node;
        }
        if (expr.op === 'OR') {
          if ((isLit(left) && left.value === true) || (isLit(right) && right.value === true)) {
            return { type: 'Lit', value: true };
          }
          if (isLit(left) && left.value === false) return right;
          if (isLit(right) && right.value === false) return left;
          if (isLit(left) && isLit(right)) return { type: 'Lit', value: evalConst(node) };
          return node;
        }
        if ((isLit(left) && left.value === null) || (isLit(right) && right.value === null)) {
          return { type: 'Lit', value: null };
        }
        if (isLit(left) && isLit(right)) return { type: 'Lit', value: evalConst(node) };
        return node;
      }
    }
  }
  function walk(node) {
    switch (node.type) {
      case 'Scan':
        return node;
      case 'Join':
        return { type: 'Join', left: walk(node.left), right: walk(node.right),
          on: simplify(node.on) };
      case 'Filter': {
        const input = walk(node.input);
        const predicate = simplify(node.predicate);
        if (predicate.type === 'Lit' && predicate.value === true) return input;
        return { type: 'Filter', input: input, predicate: predicate };
      }
      case 'Sort':
        return { type: 'Sort', input: walk(node.input), keys: node.keys };
      case 'Limit':
        return { type: 'Limit', input: walk(node.input), count: node.count };
      case 'Project':
        return { type: 'Project', input: walk(node.input), columns: node.columns };
    }
  }
  return walk(plan);
}`,
  tests: [
    {
      name: 'TRUE AND p drops to p, and the Filter survives',
      args: [planOf("SELECT name FROM users WHERE TRUE AND city = 'london'")],
      expected: simplifyPredicates(planOf("SELECT name FROM users WHERE TRUE AND city = 'london'")),
    },
    {
      name: 'a tautology removes the Filter node entirely',
      args: [planOf('SELECT name FROM users WHERE 1 = 1')],
      expected: simplifyPredicates(planOf('SELECT name FROM users WHERE 1 = 1')),
      hint: 'After simplifying to Lit true, return the Filter’s input instead of the Filter.',
    },
    {
      name: 'a contradiction keeps its Filter (there is no empty-relation node)',
      args: [planOf("SELECT name FROM users WHERE 1 = 2 AND city = 'london'")],
      expected: simplifyPredicates(planOf("SELECT name FROM users WHERE 1 = 2 AND city = 'london'")),
      hint: 'FALSE AND p folds to Lit false — but Filter(false) stays: it still keeps zero rows correctly.',
    },
    {
      name: 'Kleene absorption: p AND FALSE is false even though p might be null',
      args: [planOf('SELECT name FROM users WHERE city IS NULL AND FALSE')],
      expected: simplifyPredicates(planOf('SELECT name FROM users WHERE city IS NULL AND FALSE')),
    },
    {
      name: 'strict operators fold a literal NULL to NULL',
      args: [planOf('SELECT name FROM users WHERE signup_year + NULL > 2000')],
      expected: simplifyPredicates(planOf('SELECT name FROM users WHERE signup_year + NULL > 2000')),
      hint: 'signup_year + NULL is null for every row — the whole comparison folds to Lit null without looking at the column.',
    },
    {
      name: 'OR mirrors AND: FALSE OR p is p, TRUE OR p is true',
      args: [planOf("SELECT name FROM users WHERE FALSE OR city = 'boston' OR 2 < 1")],
      expected: simplifyPredicates(
        planOf("SELECT name FROM users WHERE FALSE OR city = 'boston' OR 2 < 1"),
      ),
    },
    {
      name: 'arithmetic folds with executor semantics: 4 / 0 is null',
      args: [planOf('SELECT name FROM users WHERE 4 / 0 IS NULL')],
      expected: simplifyPredicates(planOf('SELECT name FROM users WHERE 4 / 0 IS NULL')),
      hint: 'Fold with the Tier 4 semantics, not JavaScript’s: division by zero folds to Lit null, and NULL IS NULL folds to true — this Filter disappears.',
    },
    {
      name: 'Join ON conditions simplify too',
      args: [planOf('SELECT u.name FROM users u JOIN orders o ON u.id = o.user_id AND TRUE')],
      expected: simplifyPredicates(
        planOf('SELECT u.name FROM users u JOIN orders o ON u.id = o.user_id AND TRUE'),
      ),
    },
    {
      name: 'nothing constant, nothing changed',
      args: [planOf("SELECT name FROM users WHERE city = 'london' AND signup_year > 2020")],
      expected: simplifyPredicates(
        planOf("SELECT name FROM users WHERE city = 'london' AND signup_year > 2020"),
      ),
    },
  ],
  custom: {
    describe:
      'An AstQL query (shop catalog); its naive plan is built for you, and the reference simplifier supplies the expected plan.',
    placeholder: '"SELECT name FROM users WHERE TRUE AND 2 * 2 = 4 AND city IS NULL"',
    toArgs: (values) => [planOf(String(values[0]))],
  },
};

const sqlPushdown: ChallengeDef = {
  id: 'sql-pushdown',
  title: 'Push filters down',
  summary: 'The optimizer classic: split conjuncts, move each below the join it belongs under.',
  signature: 'pushDown(plan) → Plan',
  entry: 'pushDown',
  prelude: OPTIMIZER_PRELUDE,
  difficulty: 4,
  starter: `// For every Filter sitting directly on a Join:
//   1. Split its predicate into AND-conjuncts (flatten the left-leaning
//      AND tree, left to right).
//   2. Classify each conjunct by exprBindings(conjunct) against
//      planBindings(join.left) / planBindings(join.right):
//        all on the left  -> push directly above the left child
//        all on the right -> push directly above the right child
//        both sides, or NO bindings at all -> stays at the join
//   3. Pushed conjuncts re-combine with AND in their original order,
//      as one Filter per side. Remaining conjuncts re-combine above the
//      join the same way (or the Filter disappears if none remain).
//   4. Recurse: a pushed Filter may now sit on a lower Join and split
//      again.
// Filters not sitting on a Join just recurse into their input.
// Rebuild, never mutate. (Helpers provided — see the panel above.)
function pushDown(plan) {
  // TODO
}`,
  solution: `function pushDown(plan) {
  function conjunctsOf(expr) {
    if (expr.type === 'Binary' && expr.op === 'AND') {
      return conjunctsOf(expr.left).concat(conjunctsOf(expr.right));
    }
    return [expr];
  }
  function andAll(list) {
    return list.reduce(function (left, right) {
      return { type: 'Binary', op: 'AND', left: left, right: right };
    });
  }
  function subset(a, b) {
    for (const x of a) if (!b.has(x)) return false;
    return true;
  }
  switch (plan.type) {
    case 'Scan':
      return plan;
    case 'Join':
      return { type: 'Join', left: pushDown(plan.left), right: pushDown(plan.right), on: plan.on };
    case 'Filter': {
      if (plan.input.type !== 'Join') {
        return { type: 'Filter', input: pushDown(plan.input), predicate: plan.predicate };
      }
      const join = plan.input;
      const leftBindings = planBindings(join.left);
      const rightBindings = planBindings(join.right);
      const toLeft = [];
      const toRight = [];
      const staying = [];
      for (const conjunct of conjunctsOf(plan.predicate)) {
        const bindings = exprBindings(conjunct);
        if (bindings.size === 0) staying.push(conjunct);
        else if (subset(bindings, leftBindings)) toLeft.push(conjunct);
        else if (subset(bindings, rightBindings)) toRight.push(conjunct);
        else staying.push(conjunct);
      }
      let left = join.left;
      if (toLeft.length > 0) left = { type: 'Filter', input: left, predicate: andAll(toLeft) };
      let right = join.right;
      if (toRight.length > 0) right = { type: 'Filter', input: right, predicate: andAll(toRight) };
      const rebuilt = { type: 'Join', left: pushDown(left), right: pushDown(right), on: join.on };
      if (staying.length === 0) return rebuilt;
      return { type: 'Filter', input: rebuilt, predicate: andAll(staying) };
    }
    case 'Sort':
      return { type: 'Sort', input: pushDown(plan.input), keys: plan.keys };
    case 'Limit':
      return { type: 'Limit', input: pushDown(plan.input), count: plan.count };
    case 'Project':
      return { type: 'Project', input: pushDown(plan.input), columns: plan.columns };
  }
}`,
  tests: [
    {
      name: 'single-side conjuncts drop below the join; the cross-side one stays',
      args: [
        planOf(
          "SELECT u.name, o.total FROM users u JOIN orders o ON u.id = o.user_id WHERE u.city = 'seattle' AND o.year = 2024 AND u.signup_year < o.year",
        ),
      ],
      expected: pushDownFilters(
        planOf(
          "SELECT u.name, o.total FROM users u JOIN orders o ON u.id = o.user_id WHERE u.city = 'seattle' AND o.year = 2024 AND u.signup_year < o.year",
        ),
      ),
      hint: 'Three conjuncts, three destinations: u.city left, o.year right, and the two-sided comparison stays at the join.',
    },
    {
      name: 'a whole predicate on one side moves as a unit',
      args: [
        planOf("SELECT u.name FROM users u JOIN orders o ON u.id = o.user_id WHERE u.city = 'london'"),
      ],
      expected: pushDownFilters(
        planOf("SELECT u.name FROM users u JOIN orders o ON u.id = o.user_id WHERE u.city = 'london'"),
      ),
    },
    {
      name: 'no Join below the Filter — nothing moves',
      args: [planOf("SELECT name FROM users WHERE city = 'london'")],
      expected: pushDownFilters(planOf("SELECT name FROM users WHERE city = 'london'")),
    },
    {
      name: 'constant conjuncts stay at the join',
      args: [
        planOf('SELECT u.name FROM users u JOIN orders o ON u.id = o.user_id WHERE 1 = 1 AND u.id > 2'),
      ],
      expected: pushDownFilters(
        planOf('SELECT u.name FROM users u JOIN orders o ON u.id = o.user_id WHERE 1 = 1 AND u.id > 2'),
      ),
      hint: 'An empty binding set is technically a subset of both sides — classify “no bindings” explicitly as staying put (simplification’s job, not pushdown’s).',
    },
    {
      name: 'filters cascade through stacked joins',
      args: [
        planOf(
          "SELECT u.name FROM users u JOIN orders o ON u.id = o.user_id JOIN products p ON o.product_id = p.id WHERE p.category = 'hardware' AND u.city = 'london' AND o.quantity > 1",
        ),
      ],
      expected: pushDownFilters(
        planOf(
          "SELECT u.name FROM users u JOIN orders o ON u.id = o.user_id JOIN products p ON o.product_id = p.id WHERE p.category = 'hardware' AND u.city = 'london' AND o.quantity > 1",
        ),
      ),
      hint: 'u.city belongs to the lower join’s left side: after landing above Join(users, orders) it must split again. That is what the recursion is for.',
    },
    {
      name: 'OR does not split: a disjunction is one conjunct',
      args: [
        planOf(
          "SELECT u.name FROM users u JOIN orders o ON u.id = o.user_id WHERE u.city = 'london' OR o.year = 2024",
        ),
      ],
      expected: pushDownFilters(
        planOf(
          "SELECT u.name FROM users u JOIN orders o ON u.id = o.user_id WHERE u.city = 'london' OR o.year = 2024",
        ),
      ),
      hint: 'Only AND flattens. This whole OR references both sides, so it stays at the join.',
    },
    {
      name: 'ORDER BY and LIMIT stay where they were',
      args: [
        planOf(
          "SELECT u.name FROM users u JOIN orders o ON u.id = o.user_id WHERE o.year = 2025 ORDER BY u.name LIMIT 3",
        ),
      ],
      expected: pushDownFilters(
        planOf(
          "SELECT u.name FROM users u JOIN orders o ON u.id = o.user_id WHERE o.year = 2025 ORDER BY u.name LIMIT 3",
        ),
      ),
    },
    {
      name: 'the payoff: same rows, fewer cells on the bench database',
      args: [
        planOf(
          "SELECT u.name, o.total FROM users u JOIN orders o ON u.id = o.user_id WHERE u.city = 'seattle' AND o.year = 2024",
        ),
      ],
      check: equivalenceAndCostCheck({
        label: 'Pushing a filter below a join shrinks the nested loop itself.',
        mustReduce: true,
      }),
    },
  ],
  custom: {
    describe:
      'An AstQL query (shop catalog); its naive plan is built for you, and the reference pushdown supplies the expected plan.',
    placeholder:
      '"SELECT u.name FROM users u JOIN orders o ON u.id = o.user_id WHERE u.city = \'tokyo\' AND o.total > 100"',
    toArgs: (values) => [planOf(String(values[0]))],
  },
};

const sqlPrune: ChallengeDef = {
  id: 'sql-prune',
  title: 'Prune unused columns',
  summary: 'Rows are cheap to count, wide rows are not: project early at the base of joins.',
  signature: 'prune(plan) → Plan',
  entry: 'prune',
  prelude: OPTIMIZER_PRELUDE,
  difficulty: 4,
  starter: `// 1. Collect the needed set: every 'binding.column' referenced by ANY
//    expression in the plan — Project columns, Filter predicates, Join
//    ONs, Sort keys (exprRefs helps).
// 2. Rewrite: above every Scan that sits under at least one Join, insert
//      { type: 'Project', input: scan,
//        columns: needed columns of that binding, in Scan column order,
//                 each { expr: { type: 'Column', table: binding, name },
//                        name: binding + '.' + name } }
//    — qualified output names, so the rows keep their plan-internal keys.
// 3. Skip the Project when it would keep every column (a no-op Project
//    only adds cost), and leave Scans outside any Join alone.
// Rebuild, never mutate. (Helpers provided — see the panel above.)
function prune(plan) {
  // TODO
}`,
  solution: `function prune(plan) {
  const needed = new Set();
  (function collect(node) {
    switch (node.type) {
      case 'Scan':
        return;
      case 'Join':
        for (const ref of exprRefs(node.on)) needed.add(ref);
        collect(node.left);
        collect(node.right);
        return;
      case 'Filter':
        for (const ref of exprRefs(node.predicate)) needed.add(ref);
        collect(node.input);
        return;
      case 'Sort':
        for (const key of node.keys) for (const ref of exprRefs(key.expr)) needed.add(ref);
        collect(node.input);
        return;
      case 'Limit':
        collect(node.input);
        return;
      case 'Project':
        for (const col of node.columns) for (const ref of exprRefs(col.expr)) needed.add(ref);
        collect(node.input);
        return;
    }
  })(plan);
  function rewrite(node, underJoin) {
    switch (node.type) {
      case 'Scan': {
        if (!underJoin) return node;
        const keep = node.columns.filter(function (c) {
          return needed.has(node.binding + '.' + c);
        });
        if (keep.length === node.columns.length) return node;
        return {
          type: 'Project',
          input: node,
          columns: keep.map(function (name) {
            return { expr: { type: 'Column', table: node.binding, name: name },
              name: node.binding + '.' + name };
          }),
        };
      }
      case 'Join':
        return { type: 'Join', left: rewrite(node.left, true),
          right: rewrite(node.right, true), on: node.on };
      case 'Filter':
        return { type: 'Filter', input: rewrite(node.input, underJoin),
          predicate: node.predicate };
      case 'Sort':
        return { type: 'Sort', input: rewrite(node.input, underJoin), keys: node.keys };
      case 'Limit':
        return { type: 'Limit', input: rewrite(node.input, underJoin), count: node.count };
      case 'Project':
        return { type: 'Project', input: rewrite(node.input, underJoin), columns: node.columns };
    }
  }
  return rewrite(plan, false);
}`,
  tests: [
    {
      name: 'each joined scan keeps only what the plan above it touches',
      args: [planOf('SELECT u.name FROM users u JOIN orders o ON u.id = o.user_id')],
      expected: pruneColumns(planOf('SELECT u.name FROM users u JOIN orders o ON u.id = o.user_id')),
      hint: 'users needs id (the ON) and name (the projection); orders needs only user_id.',
    },
    {
      name: 'WHERE and ORDER BY columns count as needed',
      args: [
        planOf(
          'SELECT u.name FROM users u JOIN orders o ON u.id = o.user_id WHERE o.year = 2024 ORDER BY o.total',
        ),
      ],
      expected: pruneColumns(
        planOf(
          'SELECT u.name FROM users u JOIN orders o ON u.id = o.user_id WHERE o.year = 2024 ORDER BY o.total',
        ),
      ),
      hint: 'Collect from every expression in the plan — dropping o.total breaks the Sort above.',
    },
    {
      name: 'a single-table plan is left alone',
      args: [planOf('SELECT name FROM users WHERE city IS NULL')],
      expected: pruneColumns(planOf('SELECT name FROM users WHERE city IS NULL')),
    },
    {
      name: 'a scan needing every column gets no Project',
      args: [
        planOf(
          'SELECT p.id, p.name, p.category, p.price FROM products p JOIN orders o ON p.id = o.product_id',
        ),
      ],
      expected: pruneColumns(
        planOf(
          'SELECT p.id, p.name, p.category, p.price FROM products p JOIN orders o ON p.id = o.product_id',
        ),
      ),
    },
    {
      name: 'three-way joins prune every base',
      args: [
        planOf(
          'SELECT u.name FROM users u JOIN orders o ON u.id = o.user_id JOIN products p ON o.product_id = p.id',
        ),
      ],
      expected: pruneColumns(
        planOf(
          'SELECT u.name FROM users u JOIN orders o ON u.id = o.user_id JOIN products p ON o.product_id = p.id',
        ),
      ),
    },
    {
      name: 'SELECT * needs everything: no pruning',
      args: [planOf('SELECT * FROM users u JOIN orders o ON u.id = o.user_id')],
      expected: pruneColumns(planOf('SELECT * FROM users u JOIN orders o ON u.id = o.user_id')),
    },
    {
      name: 'the payoff: same rows, fewer cells on the bench database',
      args: [planOf('SELECT u.name FROM users u JOIN orders o ON u.id = o.user_id')],
      check: equivalenceAndCostCheck({
        label: 'Narrow rows make every pair the nested loop touches cheaper.',
        mustReduce: true,
      }),
    },
  ],
  custom: {
    describe:
      'An AstQL query (shop catalog); its naive plan is built for you, and the reference pruner supplies the expected plan.',
    placeholder: '"SELECT u.name FROM users u JOIN orders o ON u.id = o.user_id ORDER BY o.total"',
    toArgs: (values) => [planOf(String(values[0]))],
  },
};

/** The capstone battery: queries whose naive plans leave real money on the table. */
const CAPSTONE_QUERIES: { q: string; hint?: string }[] = [
  {
    q: 'SELECT name FROM users WHERE TRUE AND signup_year > 2020',
    hint: 'Simplification alone: TRUE AND p, then the Filter stays put (no join to push through).',
  },
  {
    q: "SELECT u.name, o.total FROM users u JOIN orders o ON u.id = o.user_id WHERE u.city = 'seattle' AND o.year = 2024",
    hint: 'The bread-and-butter case: one conjunct to each side, then prune both scans.',
  },
  {
    q: "SELECT u.name FROM users u JOIN orders o ON u.id = o.user_id JOIN products p ON o.product_id = p.id WHERE p.category = 'hardware' AND u.city = 'london' AND o.quantity > 1",
    hint: 'Three tables, three destinations — the cascade through stacked joins has to work.',
  },
  {
    q: "SELECT u.name FROM users u JOIN orders o ON u.id = o.user_id WHERE o.total > 100 AND u.signup_year < o.year",
    hint: 'One conjunct pushes; the cross-side comparison must stay at the join and still hold.',
  },
  {
    q: 'SELECT u.name, o.total FROM users u JOIN orders o ON u.id = o.user_id WHERE 1 = 2 AND o.year = 2024',
    hint: 'Simplify first: the whole predicate collapses to FALSE, and almost everything downstream is free.',
  },
  {
    q: "SELECT u.name FROM users u JOIN orders o ON u.id = o.user_id WHERE o.year >= 2024 ORDER BY u.name LIMIT 5",
    hint: 'Sort and Limit stay above; pushdown and pruning work below them.',
  },
  {
    q: "SELECT p.name, o.quantity FROM products p JOIN orders o ON p.id = o.product_id WHERE p.price > 200 AND o.year = 2025 ORDER BY o.quantity DESC",
    hint: 'Both sides filter and both sides prune; the ORDER BY column must survive pruning.',
  },
  {
    q: "SELECT u.city, o.total FROM users u JOIN orders o ON u.id = o.user_id WHERE u.city IS NOT NULL AND o.total IS NOT NULL AND o.total / o.quantity > 20",
    hint: 'IS NOT NULL predicates push like any other single-side conjunct; the division is per-row work you cannot remove, only shrink.',
  },
];

function capstoneBudget(q: string): { budget: number; naiveCost: number; refCost: number } {
  const naive = planOf(q);
  const naiveCost = executeWithStats(naive, benchDb).cost;
  const refCost = executeWithStats(optimizePlan(naive), benchDb).cost;
  // 25% headroom over the reference pipeline: room for a different but
  // honest optimizer, none for skipping a whole technique.
  return { budget: Math.ceil(refCost * 1.25), naiveCost, refCost };
}

const capstoneCheck: CheckFn = (actual, ctx) => {
  const naive = ctx.args[0] as Plan;
  const plan = actual as Plan;
  const problems = validatePlan(plan, shopCatalog);
  if (problems.length > 0) {
    return { pass: false, message: `Your plan is not well-formed: ${problems.join('; ')}.` };
  }
  for (const db of [demoDb, benchDb]) {
    const expectedRows = executePlan(naive, db);
    let actualRows;
    try {
      actualRows = executePlan(plan, db);
    } catch (e) {
      return {
        pass: false,
        message: `Executing your plan failed: ${e instanceof Error ? e.message : show(e)}`,
      };
    }
    const diff = firstDiff(expectedRows, actualRows);
    if (diff !== null) {
      const which = db === demoDb ? 'demo' : 'bench';
      return {
        pass: false,
        message: `Your plan changes the result on the ${which} database: first difference at rows${diff.path} (${diff.reason}). Equivalence first, speed second.`,
        expectedText: `${showPretty(expectedRows.slice(0, 5))}\n… (first rows shown)`,
        actualText: `${showPretty(actualRows.slice(0, 5))}\n… (first rows shown)`,
      };
    }
  }
  const stats = executeWithStats(plan, benchDb);
  const naiveCost = executeWithStats(naive, benchDb).cost;
  const refCost = executeWithStats(optimizePlan(naive), benchDb).cost;
  const budget = Math.ceil(refCost * 1.25);
  if (stats.cost > budget) {
    const bill = stats.operators.map((op) => `${op.label}: ${op.cells}`).join('  ·  ');
    return {
      pass: false,
      message: `Correct rows, but over budget: your plan processes ${stats.cost} cells on the bench database; the budget is ${budget} (naive: ${naiveCost}, reference optimizer: ${refCost}). Your plan's bill — ${bill}`,
    };
  }
  return { pass: true };
};

const sqlOptimize: ChallengeDef = {
  id: 'sql-optimize',
  title: 'Capstone: the optimizer',
  summary: 'Simplify, push down, prune — graded on equivalence and a hard cost budget.',
  signature: 'optimize(plan) → Plan',
  entry: 'optimize',
  prelude: OPTIMIZER_PRELUDE,
  difficulty: 5,
  timeoutMs: 10000,
  starter: `// Produce ANY well-formed plan that (a) returns exactly the same rows
// as the input plan on every database, and (b) fits each test's cost
// budget on the bench database. The budgets assume all three techniques:
//   simplify predicates -> push filters down -> prune columns
// (pruning last, so it sees the pushed filters' columns).
// Your solutions to the three previous challenges compose directly —
// paste them in and wire them up, then improve if you like. Anything
// semantically equivalent under budget passes: this grader checks
// behavior and cost, not shape.
function optimize(plan) {
  // TODO
}`,
  solution: `function optimize(plan) {
${'  '}// simplify -> push down -> prune, exactly as built in the three
${'  '}// previous challenges.
  function evalConst(expr) {
    switch (expr.type) {
      case 'Lit': return expr.value;
      case 'Unary': {
        const v = evalConst(expr.operand);
        return typeof v === 'number' ? -v : null;
      }
      case 'Not': {
        const v = evalConst(expr.operand);
        return v === null ? null : v !== true;
      }
      case 'IsNull': {
        const v = evalConst(expr.operand);
        return (v === null) !== expr.negated;
      }
      case 'Binary': {
        const l = evalConst(expr.left);
        const r = evalConst(expr.right);
        switch (expr.op) {
          case 'AND':
            if (l === false || r === false) return false;
            if (l === null || r === null) return null;
            return l === true && r === true;
          case 'OR':
            if (l === true || r === true) return true;
            if (l === null || r === null) return null;
            return false;
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
  function isLit(e) { return e.type === 'Lit'; }
  function simplifyExprNode(expr) {
    switch (expr.type) {
      case 'Lit':
      case 'Column':
        return expr;
      case 'Not': {
        const operand = simplifyExprNode(expr.operand);
        const node = { type: 'Not', operand: operand };
        return isLit(operand) ? { type: 'Lit', value: evalConst(node) } : node;
      }
      case 'Unary': {
        const operand = simplifyExprNode(expr.operand);
        const node = { type: 'Unary', op: '-', operand: operand };
        return isLit(operand) ? { type: 'Lit', value: evalConst(node) } : node;
      }
      case 'IsNull': {
        const operand = simplifyExprNode(expr.operand);
        const node = { type: 'IsNull', operand: operand, negated: expr.negated };
        return isLit(operand) ? { type: 'Lit', value: evalConst(node) } : node;
      }
      case 'Binary': {
        const left = simplifyExprNode(expr.left);
        const right = simplifyExprNode(expr.right);
        const node = { type: 'Binary', op: expr.op, left: left, right: right };
        if (expr.op === 'AND') {
          if ((isLit(left) && left.value === false) || (isLit(right) && right.value === false)) {
            return { type: 'Lit', value: false };
          }
          if (isLit(left) && left.value === true) return right;
          if (isLit(right) && right.value === true) return left;
          if (isLit(left) && isLit(right)) return { type: 'Lit', value: evalConst(node) };
          return node;
        }
        if (expr.op === 'OR') {
          if ((isLit(left) && left.value === true) || (isLit(right) && right.value === true)) {
            return { type: 'Lit', value: true };
          }
          if (isLit(left) && left.value === false) return right;
          if (isLit(right) && right.value === false) return left;
          if (isLit(left) && isLit(right)) return { type: 'Lit', value: evalConst(node) };
          return node;
        }
        if ((isLit(left) && left.value === null) || (isLit(right) && right.value === null)) {
          return { type: 'Lit', value: null };
        }
        if (isLit(left) && isLit(right)) return { type: 'Lit', value: evalConst(node) };
        return node;
      }
    }
  }
  function simplifyPlan(node) {
    switch (node.type) {
      case 'Scan':
        return node;
      case 'Join':
        return { type: 'Join', left: simplifyPlan(node.left), right: simplifyPlan(node.right),
          on: simplifyExprNode(node.on) };
      case 'Filter': {
        const input = simplifyPlan(node.input);
        const predicate = simplifyExprNode(node.predicate);
        if (predicate.type === 'Lit' && predicate.value === true) return input;
        return { type: 'Filter', input: input, predicate: predicate };
      }
      case 'Sort':
        return { type: 'Sort', input: simplifyPlan(node.input), keys: node.keys };
      case 'Limit':
        return { type: 'Limit', input: simplifyPlan(node.input), count: node.count };
      case 'Project':
        return { type: 'Project', input: simplifyPlan(node.input), columns: node.columns };
    }
  }
  function conjunctsOf(expr) {
    if (expr.type === 'Binary' && expr.op === 'AND') {
      return conjunctsOf(expr.left).concat(conjunctsOf(expr.right));
    }
    return [expr];
  }
  function andAll(list) {
    return list.reduce(function (left, right) {
      return { type: 'Binary', op: 'AND', left: left, right: right };
    });
  }
  function subset(a, b) {
    for (const x of a) if (!b.has(x)) return false;
    return true;
  }
  function pushDown(node) {
    switch (node.type) {
      case 'Scan':
        return node;
      case 'Join':
        return { type: 'Join', left: pushDown(node.left), right: pushDown(node.right), on: node.on };
      case 'Filter': {
        if (node.input.type !== 'Join') {
          return { type: 'Filter', input: pushDown(node.input), predicate: node.predicate };
        }
        const join = node.input;
        const leftBindings = planBindings(join.left);
        const rightBindings = planBindings(join.right);
        const toLeft = [];
        const toRight = [];
        const staying = [];
        for (const conjunct of conjunctsOf(node.predicate)) {
          const bindings = exprBindings(conjunct);
          if (bindings.size === 0) staying.push(conjunct);
          else if (subset(bindings, leftBindings)) toLeft.push(conjunct);
          else if (subset(bindings, rightBindings)) toRight.push(conjunct);
          else staying.push(conjunct);
        }
        let left = join.left;
        if (toLeft.length > 0) left = { type: 'Filter', input: left, predicate: andAll(toLeft) };
        let right = join.right;
        if (toRight.length > 0) right = { type: 'Filter', input: right, predicate: andAll(toRight) };
        const rebuilt = { type: 'Join', left: pushDown(left), right: pushDown(right), on: join.on };
        if (staying.length === 0) return rebuilt;
        return { type: 'Filter', input: rebuilt, predicate: andAll(staying) };
      }
      case 'Sort':
        return { type: 'Sort', input: pushDown(node.input), keys: node.keys };
      case 'Limit':
        return { type: 'Limit', input: pushDown(node.input), count: node.count };
      case 'Project':
        return { type: 'Project', input: pushDown(node.input), columns: node.columns };
    }
  }
  function prune(root) {
    const needed = new Set();
    (function collect(node) {
      switch (node.type) {
        case 'Scan':
          return;
        case 'Join':
          for (const ref of exprRefs(node.on)) needed.add(ref);
          collect(node.left);
          collect(node.right);
          return;
        case 'Filter':
          for (const ref of exprRefs(node.predicate)) needed.add(ref);
          collect(node.input);
          return;
        case 'Sort':
          for (const key of node.keys) for (const ref of exprRefs(key.expr)) needed.add(ref);
          collect(node.input);
          return;
        case 'Limit':
          collect(node.input);
          return;
        case 'Project':
          for (const col of node.columns) for (const ref of exprRefs(col.expr)) needed.add(ref);
          collect(node.input);
          return;
      }
    })(root);
    function rewrite(node, underJoin) {
      switch (node.type) {
        case 'Scan': {
          if (!underJoin) return node;
          const keep = node.columns.filter(function (c) {
            return needed.has(node.binding + '.' + c);
          });
          if (keep.length === node.columns.length) return node;
          return {
            type: 'Project',
            input: node,
            columns: keep.map(function (name) {
              return { expr: { type: 'Column', table: node.binding, name: name },
                name: node.binding + '.' + name };
            }),
          };
        }
        case 'Join':
          return { type: 'Join', left: rewrite(node.left, true),
            right: rewrite(node.right, true), on: node.on };
        case 'Filter':
          return { type: 'Filter', input: rewrite(node.input, underJoin),
            predicate: node.predicate };
        case 'Sort':
          return { type: 'Sort', input: rewrite(node.input, underJoin), keys: node.keys };
        case 'Limit':
          return { type: 'Limit', input: rewrite(node.input, underJoin), count: node.count };
        case 'Project':
          return { type: 'Project', input: rewrite(node.input, underJoin), columns: node.columns };
      }
    }
    return rewrite(root, false);
  }
  return prune(pushDown(simplifyPlan(plan)));
}`,
  check: capstoneCheck,
  tests: CAPSTONE_QUERIES.map(({ q, hint }, i) => {
    const { budget, naiveCost } = capstoneBudget(q);
    return {
      name: `q${i + 1}: ${q} — same rows, ≤ ${budget} cells (naive: ${naiveCost})`,
      args: [planOf(q)] as unknown[],
      hint,
    };
  }),
  custom: {
    describe:
      'An AstQL query (shop catalog); its naive plan is built for you, and your optimizer is graded on equivalence and cost against the reference pipeline.',
    placeholder:
      '"SELECT u.name FROM users u JOIN orders o ON u.id = o.user_id WHERE u.city = \'berlin\' AND o.year = 2023"',
    toArgs: (values) => [planOf(String(values[0]))],
  },
};

export const tier5: Tier = {
  id: 'tier5',
  number: 5,
  title: 'Optimization',
  subtitle: 'Meaning-preserving rewrites, kept honest by execution and a cost model.',
  steps: [
    { kind: 'lesson', lesson: optimizersLesson },
    { kind: 'challenge', challenge: sqlSimplify },
    { kind: 'challenge', challenge: sqlPushdown },
    { kind: 'challenge', challenge: sqlPrune },
    { kind: 'challenge', challenge: sqlOptimize },
  ],
};
