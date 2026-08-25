import type { Catalog, Plan, Select, SqlExpr } from './types';

/**
 * Reference naive planner for AstQL; the spec the `sql-plan` challenge is
 * graded against.
 *
 * Canonical operator order, bottom to top: Scan for FROM, a left-deep Join
 * per JOIN clause in source order, Filter for WHERE, Sort for ORDER BY
 * (below Project so keys can reference any resolved column), Project for
 * the SELECT list, Limit. Optional clauses contribute no node.
 *
 * Takes a *resolved* Select: `*` already expanded and every Column
 * annotated with its binding.
 */
export function buildPlan(select: Select, catalog: Catalog): Plan {
  if (select.columns === '*') {
    throw new Error('buildPlan requires a resolved Select (columns still *)');
  }

  const scan = (table: string, alias: string | null): Plan => ({
    type: 'Scan',
    table,
    binding: alias ?? table,
    columns: catalog[table] ?? [],
  });

  let plan: Plan = scan(select.from.table, select.from.alias);
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
    columns: select.columns.map((c, i) => ({ expr: c.expr, name: outputName(c.expr, c.alias, i) })),
  };
  if (select.limit !== null) {
    plan = { type: 'Limit', input: plan, count: select.limit };
  }
  return plan;
}

/** Output column name: the alias, else a Column's own name, else col<N> (1-based). */
export function outputName(expr: SqlExpr, alias: string | null, index: number): string {
  if (alias !== null) return alias;
  if (expr.type === 'Column') return expr.name;
  return `col${index + 1}`;
}

/** The row keys a plan node emits: binding-qualified below the final Project, output names after it. */
export function planColumns(plan: Plan): string[] {
  switch (plan.type) {
    case 'Scan':
      return plan.columns.map((c) => `${plan.binding}.${c}`);
    case 'Join':
      return [...planColumns(plan.left), ...planColumns(plan.right)];
    case 'Filter':
    case 'Sort':
      return planColumns(plan.input);
    case 'Limit':
      return planColumns(plan.input);
    case 'Project':
      return plan.columns.map((c) => c.name);
  }
}

function exprColumnKeys(expr: SqlExpr, out: Set<string>): void {
  switch (expr.type) {
    case 'Column':
      out.add(`${expr.table}.${expr.name}`);
      return;
    case 'Lit':
      return;
    case 'Binary':
      exprColumnKeys(expr.left, out);
      exprColumnKeys(expr.right, out);
      return;
    case 'Not':
    case 'IsNull':
      exprColumnKeys(expr.operand, out);
      return;
    case 'Unary':
      exprColumnKeys(expr.operand, out);
      return;
  }
}

/** Binding-qualified keys (`u.id`) referenced anywhere in an expression. */
export function exprRefs(expr: SqlExpr): Set<string> {
  const out = new Set<string>();
  exprColumnKeys(expr, out);
  return out;
}

/**
 * Structural validity check used by the optimizer graders: every referenced
 * column must be available from the node's input. Returns human-readable
 * problems, empty when the plan is well-formed.
 */
export function validatePlan(plan: Plan, catalog: Catalog): string[] {
  const problems: string[] = [];

  function checkRefs(where: string, expr: SqlExpr, available: Set<string>): void {
    for (const ref of exprRefs(expr)) {
      if (!available.has(ref)) {
        problems.push(`${where} references '${ref}', which its input does not produce`);
      }
    }
  }

  function visit(node: Plan): Set<string> {
    switch (node.type) {
      case 'Scan': {
        const columns = catalog[node.table];
        if (columns === undefined) {
          problems.push(`Scan of unknown table '${node.table}'`);
          return new Set();
        }
        return new Set(columns.map((c) => `${node.binding}.${c}`));
      }
      case 'Join': {
        const left = visit(node.left);
        const right = visit(node.right);
        const merged = new Set([...left, ...right]);
        checkRefs('Join ON', node.on, merged);
        return merged;
      }
      case 'Filter': {
        const available = visit(node.input);
        checkRefs('Filter', node.predicate, available);
        return available;
      }
      case 'Sort': {
        const available = visit(node.input);
        for (const key of node.keys) checkRefs('Sort key', key.expr, available);
        return available;
      }
      case 'Limit': {
        if (!Number.isInteger(node.count) || node.count < 0) {
          problems.push(`Limit count must be a non-negative integer, got ${node.count}`);
        }
        return visit(node.input);
      }
      case 'Project': {
        const available = visit(node.input);
        for (const col of node.columns) checkRefs('Project', col.expr, available);
        return new Set(node.columns.map((c) => c.name));
      }
      default: {
        problems.push(`Unknown plan node type '${(node as { type?: string }).type}'`);
        return new Set();
      }
    }
  }

  visit(plan);
  return problems;
}
