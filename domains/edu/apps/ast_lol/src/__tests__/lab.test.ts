import { describe, expect, it } from 'vitest';
import { inspectPipeline, labLimits } from '../lab/pipeline';

describe('inspectPipeline', () => {
  it('shows every Expr stage, evaluated with the lab environment', () => {
    const lab = inspectPipeline('expr', 'x + 2 * 3');
    if (!('value' in lab)) throw new Error(`no value: ${lab.error}`);
    expect(lab.error).toBeNull();
    expect(lab.value).toBe(9);
    expect(lab.printed).toBe('x + 2 * 3');
    expect(lab.folded).toEqual({
      type: 'Binary',
      op: '+',
      left: { type: 'Var', name: 'x' },
      right: { type: 'Num', value: 6 },
    });
  });

  it('runs the pushdown example naive and optimized: same rows, less work', () => {
    const lab = inspectPipeline(
      'sql',
      'SELECT u.name, o.total FROM users u JOIN orders o ON u.id = o.user_id ' +
        'WHERE o.year = 2024 ORDER BY o.total DESC',
    );
    if (!('after' in lab) || !lab.before || !lab.after) throw new Error(`no results: ${lab.error}`);
    expect(Object.keys(lab).sort()).toEqual([
      'after', 'ast', 'before', 'error', 'optimized', 'plan', 'resolved', 'tokens',
    ]);
    const rows = [
      { name: 'grace', total: 500 },
      { name: 'barbara', total: 320 },
      { name: 'grace', total: 120 },
    ];
    expect(lab.before.rows).toEqual(rows);
    expect(lab.after.rows).toEqual(rows);
    expect(lab.before.operators).toEqual([
      { label: 'Scan(users AS u)', cells: 24 },
      { label: 'Scan(orders AS o)', cells: 60 },
      { label: 'Join', cells: 600 },
      { label: 'Filter', cells: 100 },
      { label: 'Sort', cells: 30 },
      { label: 'Project', cells: 30 },
    ]);
    // The filter and projections move below the join, which shrinks to 90 cells.
    expect(lab.after.operators).toEqual([
      { label: 'Scan(users AS u)', cells: 24 },
      { label: 'Project', cells: 24 },
      { label: 'Scan(orders AS o)', cells: 60 },
      { label: 'Project', cells: 60 },
      { label: 'Filter', cells: 30 },
      { label: 'Join', cells: 90 },
      { label: 'Sort', cells: 15 },
      { label: 'Project', cells: 15 },
    ]);
    expect([lab.before.cost, lab.after.cost]).toEqual([844, 318]);
  });

  it('stops at resolution when a query names an unknown table', () => {
    const lab = inspectPipeline('sql', 'SELECT x FROM nowhere');
    expect(lab.error).toBeNull();
    expect(Object.keys(lab).sort()).toEqual(['ast', 'error', 'resolved', 'tokens']);
  });

  it('reports a parse error rather than throwing', () => {
    expect(inspectPipeline('expr', '1 +')).toEqual({ error: 'Unexpected end of input' });
  });

  describe('input bounds', () => {
    const crossJoin = (joins: number) =>
      'SELECT a0.id FROM orders a0' +
      Array.from({ length: joins }, (_, i) => ` JOIN orders a${i + 1} ON TRUE`).join('');

    it('handles the largest query the limits allow: a thousand rows', () => {
      const lab = inspectPipeline('sql', crossJoin(labLimits.maxJoins));
      if (!('before' in lab) || !lab.before) throw new Error(`no results: ${lab.error}`);
      expect(lab.before.rows).toHaveLength(1000);
    });

    it('refuses one more join than that', () => {
      expect(inspectPipeline('sql', crossJoin(labLimits.maxJoins + 1))).toEqual({
        error: 'The lab supports at most 2 joins; use challenges for larger queries.',
      });
    });

    it('refuses input over the length limit', () => {
      const long = `1${' + 1'.repeat(labLimits.maxSourceLength)}`;
      expect(inspectPipeline('expr', long)).toEqual({
        error: 'Keep lab examples under 3,000 characters.',
      });
    });
  });
});
