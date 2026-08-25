import type { Database, ExecStats, OperatorStats, Plan, Row, SqlExpr, SqlValue } from './types';

/**
 * Reference executor for AstQL logical plans; the spec the `sql-execute`
 * challenge is graded against, and the oracle the optimizer challenges use
 * to prove result equivalence.
 *
 * Three-valued logic throughout: strict operators (arithmetic, comparison)
 * return null when any operand is null, comparisons across mismatched types
 * return null, division by zero returns null (SQL flavor, unlike Expr's JS
 * semantics), AND/OR are Kleene, and Filter/Join keep a row only when the
 * predicate is exactly true. Sort is stable, and null keys order last
 * regardless of direction.
 */
export function evalRowExpr(expr: SqlExpr, row: Row): SqlValue {
  switch (expr.type) {
    case 'Lit':
      return expr.value;
    case 'Column': {
      const key = `${expr.table}.${expr.name}`;
      const value = row[key];
      return value === undefined ? null : value;
    }
    case 'Unary': {
      const v = evalRowExpr(expr.operand, row);
      return v === null || typeof v !== 'number' ? null : -v;
    }
    case 'Not': {
      const v = evalRowExpr(expr.operand, row);
      return v === null ? null : v !== true;
    }
    case 'IsNull': {
      const v = evalRowExpr(expr.operand, row);
      return (v === null) !== expr.negated;
    }
    case 'Binary': {
      const l = evalRowExpr(expr.left, row);
      const r = evalRowExpr(expr.right, row);
      switch (expr.op) {
        case 'AND':
          if (l === false || r === false) return false;
          if (l === null || r === null) return null;
          return l === true && r === true;
        case 'OR':
          if (l === true || r === true) return true;
          if (l === null || r === null) return null;
          return false;
        case '+':
        case '-':
        case '*':
        case '/': {
          if (typeof l !== 'number' || typeof r !== 'number') return null;
          if (expr.op === '/') return r === 0 ? null : l / r;
          if (expr.op === '+') return l + r;
          if (expr.op === '-') return l - r;
          return l * r;
        }
        default: {
          if (l === null || r === null) return null;
          if (typeof l !== typeof r) return null;
          switch (expr.op) {
            case '=':
              return l === r;
            case '<>':
              return l !== r;
            case '<':
              return l < r;
            case '<=':
              return l <= r;
            case '>':
              return l > r;
            case '>=':
              return l >= r;
          }
        }
      }
    }
  }
}

function compareValues(a: SqlValue, b: SqlValue): number {
  // Nulls last regardless of direction; the caller applies direction only
  // to non-null comparisons.
  if (a === null || b === null) throw new Error('compareValues does not order nulls');
  if (a < b) return -1;
  if (a > b) return 1;
  return 0;
}

function sortRows(rows: Row[], keys: { expr: SqlExpr; dir: 'ASC' | 'DESC' }[]): Row[] {
  const decorated = rows.map((row) => ({ row, keys: keys.map((k) => evalRowExpr(k.expr, row)) }));
  // Array.prototype.sort is stable, so equal keys keep input order.
  decorated.sort((a, b) => {
    for (let i = 0; i < keys.length; i++) {
      const av = a.keys[i];
      const bv = b.keys[i];
      if (av === null && bv === null) continue;
      if (av === null) return 1;
      if (bv === null) return -1;
      const cmp = compareValues(av, bv);
      if (cmp !== 0) return keys[i].dir === 'DESC' ? -cmp : cmp;
    }
    return 0;
  });
  return decorated.map((d) => d.row);
}

const width = (row: Row) => Object.keys(row).length;

function run(plan: Plan, db: Database, stats: OperatorStats[] | null): Row[] {
  const record = (label: string, cells: number) => {
    if (stats !== null) stats.push({ label, cells });
  };

  switch (plan.type) {
    case 'Scan': {
      const table = db[plan.table];
      if (table === undefined) throw new Error(`Unknown table '${plan.table}'`);
      const rows = table.map((r) => {
        const out: Row = {};
        for (const column of plan.columns) {
          const value = r[column];
          out[`${plan.binding}.${column}`] = value === undefined ? null : value;
        }
        return out;
      });
      record(`Scan(${plan.table} AS ${plan.binding})`, rows.length * plan.columns.length);
      return rows;
    }
    case 'Join': {
      const left = run(plan.left, db, stats);
      const right = run(plan.right, db, stats);
      const rows: Row[] = [];
      let cells = 0;
      for (const l of left) {
        for (const r of right) {
          // Every pair the nested loop examines is paid for, kept or not:
          // that is what makes filtering below the join cheaper than at it.
          cells += width(l) + width(r);
          const merged = { ...l, ...r };
          if (evalRowExpr(plan.on, merged) === true) rows.push(merged);
        }
      }
      record('Join', cells);
      return rows;
    }
    case 'Filter': {
      const input = run(plan.input, db, stats);
      record('Filter', input.reduce((n, r) => n + width(r), 0));
      return input.filter((r) => evalRowExpr(plan.predicate, r) === true);
    }
    case 'Sort': {
      const input = run(plan.input, db, stats);
      record('Sort', input.reduce((n, r) => n + width(r), 0));
      return sortRows(input, plan.keys);
    }
    case 'Limit': {
      const input = run(plan.input, db, stats);
      record('Limit', input.reduce((n, r) => n + width(r), 0));
      return input.slice(0, plan.count);
    }
    case 'Project': {
      const input = run(plan.input, db, stats);
      record('Project', input.reduce((n, r) => n + width(r), 0));
      return input.map((row) => {
        const out: Row = {};
        for (const col of plan.columns) out[col.name] = evalRowExpr(col.expr, row);
        return out;
      });
    }
  }
}

export function executePlan(plan: Plan, db: Database): Row[] {
  return run(plan, db, null);
}

/** Execute and report the course's cost model: cells entering each operator. */
export function executeWithStats(plan: Plan, db: Database): ExecStats {
  const operators: OperatorStats[] = [];
  const rows = run(plan, db, operators);
  return { rows, cost: operators.reduce((n, op) => n + op.cells, 0), operators };
}
