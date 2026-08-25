import { evalRowExpr } from './execute';
import { exprRefs } from './plan';
import type { Plan, SqlExpr, SqlValue } from './types';

/**
 * Reference rule-based optimizer for AstQL logical plans; the specs the
 * `sql-simplify`, `sql-pushdown`, `sql-prune`, and `sql-optimize`
 * challenges are graded against.
 */

/** Flatten a left-associated AND tree into its conjuncts, left to right. */
export function conjunctsOf(expr: SqlExpr): SqlExpr[] {
  if (expr.type === 'Binary' && expr.op === 'AND') {
    return [...conjunctsOf(expr.left), ...conjunctsOf(expr.right)];
  }
  return [expr];
}

/** Rebuild a left-associated AND tree; the list must be non-empty. */
export function andAll(conjuncts: SqlExpr[]): SqlExpr {
  return conjuncts.reduce((left, right) => ({ type: 'Binary', op: 'AND', left, right }));
}

/** The set of bindings (table aliases) an expression's columns reference. */
export function refBindings(expr: SqlExpr): Set<string> {
  const bindings = new Set<string>();
  for (const ref of exprRefs(expr)) bindings.add(ref.slice(0, ref.indexOf('.')));
  return bindings;
}

/** Scan bindings appearing anywhere in a subtree. */
export function planBindings(plan: Plan): Set<string> {
  switch (plan.type) {
    case 'Scan':
      return new Set([plan.binding]);
    case 'Join':
      return new Set([...planBindings(plan.left), ...planBindings(plan.right)]);
    case 'Filter':
    case 'Sort':
    case 'Limit':
    case 'Project':
      return planBindings(plan.input);
  }
}

const lit = (value: SqlValue): SqlExpr => ({ type: 'Lit', value });

const isLit = (e: SqlExpr): e is Extract<SqlExpr, { type: 'Lit' }> => e.type === 'Lit';

/**
 * Simplify a predicate bottom-up:
 * - strict operators (arithmetic, comparison) fold when both sides are
 *   literals, and to NULL when either side is literal NULL;
 * - AND/OR absorb their identity and dominant literals (Kleene-safe:
 *   FALSE AND x is FALSE even when x is NULL);
 * - NOT, unary minus, and IS [NOT] NULL fold over literals.
 */
export function simplifyExpr(expr: SqlExpr): SqlExpr {
  switch (expr.type) {
    case 'Lit':
    case 'Column':
      return expr;
    case 'Not': {
      const operand = simplifyExpr(expr.operand);
      if (isLit(operand)) return lit(evalRowExpr({ type: 'Not', operand }, {}));
      return { type: 'Not', operand };
    }
    case 'Unary': {
      const operand = simplifyExpr(expr.operand);
      if (isLit(operand)) return lit(evalRowExpr({ type: 'Unary', op: '-', operand }, {}));
      return { type: 'Unary', op: '-', operand };
    }
    case 'IsNull': {
      const operand = simplifyExpr(expr.operand);
      if (isLit(operand)) return lit((operand.value === null) !== expr.negated);
      return { type: 'IsNull', operand, negated: expr.negated };
    }
    case 'Binary': {
      const left = simplifyExpr(expr.left);
      const right = simplifyExpr(expr.right);
      const node: SqlExpr = { type: 'Binary', op: expr.op, left, right };
      if (expr.op === 'AND') {
        if ((isLit(left) && left.value === false) || (isLit(right) && right.value === false)) {
          return lit(false);
        }
        if (isLit(left) && left.value === true) return right;
        if (isLit(right) && right.value === true) return left;
        if (isLit(left) && isLit(right)) return lit(evalRowExpr(node, {}));
        return node;
      }
      if (expr.op === 'OR') {
        if ((isLit(left) && left.value === true) || (isLit(right) && right.value === true)) {
          return lit(true);
        }
        if (isLit(left) && left.value === false) return right;
        if (isLit(right) && right.value === false) return left;
        if (isLit(left) && isLit(right)) return lit(evalRowExpr(node, {}));
        return node;
      }
      // Arithmetic and comparison are null-strict: a literal NULL operand
      // decides the answer without the other side.
      if ((isLit(left) && left.value === null) || (isLit(right) && right.value === null)) {
        return lit(null);
      }
      if (isLit(left) && isLit(right)) return lit(evalRowExpr(node, {}));
      return node;
    }
  }
}

/**
 * Simplify every predicate in the plan (Filter predicates and Join ON
 * conditions), and drop Filter nodes whose predicate became literal TRUE.
 * Filters that became FALSE or NULL keep zero rows and stay in place —
 * removing them would need an empty-relation operator the course's algebra
 * does not have.
 */
export function simplifyPredicates(plan: Plan): Plan {
  switch (plan.type) {
    case 'Scan':
      return plan;
    case 'Join':
      return {
        type: 'Join',
        left: simplifyPredicates(plan.left),
        right: simplifyPredicates(plan.right),
        on: simplifyExpr(plan.on),
      };
    case 'Filter': {
      const input = simplifyPredicates(plan.input);
      const predicate = simplifyExpr(plan.predicate);
      if (isLit(predicate) && predicate.value === true) return input;
      return { type: 'Filter', input, predicate };
    }
    case 'Sort':
      return { ...plan, input: simplifyPredicates(plan.input) };
    case 'Limit':
      return { ...plan, input: simplifyPredicates(plan.input) };
    case 'Project':
      return { ...plan, input: simplifyPredicates(plan.input) };
  }
}

/**
 * Push filters through joins. A Filter directly above a Join is split into
 * AND-conjuncts; each conjunct whose columns all come from one side moves
 * directly above that side (conjuncts keep their left-to-right order and
 * are re-joined with AND). Conjuncts spanning both sides — or referencing
 * no columns at all — stay at the join. The rewrite recurses, so a pushed
 * filter cascades through a stack of joins.
 */
export function pushDownFilters(plan: Plan): Plan {
  switch (plan.type) {
    case 'Scan':
      return plan;
    case 'Join':
      return {
        type: 'Join',
        left: pushDownFilters(plan.left),
        right: pushDownFilters(plan.right),
        on: plan.on,
      };
    case 'Filter': {
      if (plan.input.type !== 'Join') {
        return { type: 'Filter', input: pushDownFilters(plan.input), predicate: plan.predicate };
      }
      const join = plan.input;
      const leftBindings = planBindings(join.left);
      const rightBindings = planBindings(join.right);
      const toLeft: SqlExpr[] = [];
      const toRight: SqlExpr[] = [];
      const staying: SqlExpr[] = [];
      for (const conjunct of conjunctsOf(plan.predicate)) {
        const bindings = refBindings(conjunct);
        if (bindings.size === 0) {
          staying.push(conjunct);
        } else if ([...bindings].every((b) => leftBindings.has(b))) {
          toLeft.push(conjunct);
        } else if ([...bindings].every((b) => rightBindings.has(b))) {
          toRight.push(conjunct);
        } else {
          staying.push(conjunct);
        }
      }
      const left: Plan =
        toLeft.length > 0 ? { type: 'Filter', input: join.left, predicate: andAll(toLeft) } : join.left;
      const right: Plan =
        toRight.length > 0
          ? { type: 'Filter', input: join.right, predicate: andAll(toRight) }
          : join.right;
      const rebuilt: Plan = {
        type: 'Join',
        left: pushDownFilters(left),
        right: pushDownFilters(right),
        on: join.on,
      };
      if (staying.length === 0) return rebuilt;
      return { type: 'Filter', input: rebuilt, predicate: andAll(staying) };
    }
    case 'Sort':
      return { ...plan, input: pushDownFilters(plan.input) };
    case 'Limit':
      return { ...plan, input: pushDownFilters(plan.input) };
    case 'Project':
      return { ...plan, input: pushDownFilters(plan.input) };
  }
}

function collectNeeded(plan: Plan, needed: Set<string>): void {
  switch (plan.type) {
    case 'Scan':
      return;
    case 'Join':
      for (const ref of exprRefs(plan.on)) needed.add(ref);
      collectNeeded(plan.left, needed);
      collectNeeded(plan.right, needed);
      return;
    case 'Filter':
      for (const ref of exprRefs(plan.predicate)) needed.add(ref);
      collectNeeded(plan.input, needed);
      return;
    case 'Sort':
      for (const key of plan.keys) for (const ref of exprRefs(key.expr)) needed.add(ref);
      collectNeeded(plan.input, needed);
      return;
    case 'Limit':
      collectNeeded(plan.input, needed);
      return;
    case 'Project':
      for (const col of plan.columns) for (const ref of exprRefs(col.expr)) needed.add(ref);
      collectNeeded(plan.input, needed);
      return;
  }
}

/**
 * Prune unused columns at the base of joins: above every Scan that sits
 * under a Join, project down to the columns the rest of the plan actually
 * references (in Scan column order, keeping their qualified names). Scans
 * outside any join are left alone, as are scans whose every column is
 * needed — a no-op Project would only add cost.
 */
export function pruneColumns(plan: Plan): Plan {
  const needed = new Set<string>();
  collectNeeded(plan, needed);

  function rewrite(node: Plan, underJoin: boolean): Plan {
    switch (node.type) {
      case 'Scan': {
        if (!underJoin) return node;
        const keep = node.columns.filter((c) => needed.has(`${node.binding}.${c}`));
        if (keep.length === node.columns.length) return node;
        return {
          type: 'Project',
          input: node,
          columns: keep.map((name) => ({
            expr: { type: 'Column', table: node.binding, name },
            name: `${node.binding}.${name}`,
          })),
        };
      }
      case 'Join':
        return {
          type: 'Join',
          left: rewrite(node.left, true),
          right: rewrite(node.right, true),
          on: node.on,
        };
      case 'Filter':
        return { ...node, input: rewrite(node.input, underJoin) };
      case 'Sort':
        return { ...node, input: rewrite(node.input, underJoin) };
      case 'Limit':
        return { ...node, input: rewrite(node.input, underJoin) };
      case 'Project':
        return { ...node, input: rewrite(node.input, underJoin) };
    }
  }

  return rewrite(plan, false);
}

/** The whole pipeline: simplify, then push down, then prune. */
export function optimizePlan(plan: Plan): Plan {
  return pruneColumns(pushDownFilters(simplifyPredicates(plan)));
}
