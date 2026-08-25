import type {
  Catalog,
  ResolveError,
  ResolveResult,
  Select,
  SelectColumn,
  SqlExpr,
} from './types';

/**
 * Reference name resolution for AstQL; the spec the `sql-resolve` challenge
 * is graded against.
 *
 * Two phases. Tables first: every FROM/JOIN table must exist in the catalog
 * and every binding name (alias, or table name when unaliased) must be
 * unique; any table error stops resolution with select null. Then columns,
 * in clause order — SELECT list, each JOIN's ON, WHERE, ORDER BY — where a
 * qualified column must name a binding that has the column, and an
 * unqualified column must be owned by exactly one binding.
 *
 * On success the returned Select is a fresh tree with `*` expanded (all
 * bindings' columns in binding order) and every Column's `table` filled
 * with its binding name. The input is not mutated.
 */
export function resolve(select: Select, catalog: Catalog): ResolveResult {
  const errors: ResolveError[] = [];

  const refs = [select.from, ...select.joins];
  const bindings = new Map<string, string[]>();
  for (const ref of refs) {
    const binding = ref.alias ?? ref.table;
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
  if (errors.length > 0) return { select: null, errors };

  function resolveExpr(expr: SqlExpr): SqlExpr {
    switch (expr.type) {
      case 'Lit':
        return expr;
      case 'Column': {
        if (expr.table !== null) {
          const columns = bindings.get(expr.table);
          if (columns === undefined || !columns.includes(expr.name)) {
            errors.push({ kind: 'unknown-column', name: `${expr.table}.${expr.name}` });
          }
          return { ...expr };
        }
        const owners = [...bindings.entries()]
          .filter(([, columns]) => columns.includes(expr.name))
          .map(([binding]) => binding);
        if (owners.length === 0) {
          errors.push({ kind: 'unknown-column', name: expr.name });
          return { ...expr };
        }
        if (owners.length > 1) {
          errors.push({ kind: 'ambiguous-column', name: expr.name });
          return { ...expr };
        }
        return { type: 'Column', table: owners[0], name: expr.name };
      }
      case 'Binary':
        return { ...expr, left: resolveExpr(expr.left), right: resolveExpr(expr.right) };
      case 'Not':
        return { ...expr, operand: resolveExpr(expr.operand) };
      case 'Unary':
        return { ...expr, operand: resolveExpr(expr.operand) };
      case 'IsNull':
        return { ...expr, operand: resolveExpr(expr.operand) };
    }
  }

  const columns: SelectColumn[] =
    select.columns === '*'
      ? [...bindings.entries()].flatMap(([binding, cols]) =>
          cols.map((name) => ({
            expr: { type: 'Column', table: binding, name } as SqlExpr,
            alias: null,
          })),
        )
      : select.columns.map((c) => ({ expr: resolveExpr(c.expr), alias: c.alias }));

  const joins = select.joins.map((j) => ({ ...j, on: resolveExpr(j.on) }));
  const where = select.where === null ? null : resolveExpr(select.where);
  const orderBy = select.orderBy.map((k) => ({ ...k, expr: resolveExpr(k.expr) }));

  if (errors.length > 0) return { select: null, errors };
  return {
    select: { type: 'Select', columns, from: { ...select.from }, joins, where, orderBy, limit: select.limit },
    errors: [],
  };
}
