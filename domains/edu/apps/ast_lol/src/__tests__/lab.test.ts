import { describe, expect, it } from 'vitest';
import { inspectPipeline, labLimits } from '../lab/pipeline';

describe('inspectPipeline', () => {
  it('shows every Expr stage, evaluated with the lab environment', () => {
    const lab = inspectPipeline('expr', 'x + 2 * 3');
    expect(lab).toMatchObject({ error: null, value: 9, printed: 'x + 2 * 3' });
    expect(lab).toHaveProperty('folded');
  });

  it('runs an AstQL query naive and optimized, with the same rows', () => {
    const lab = inspectPipeline('sql', 'SELECT u.name FROM users u WHERE u.id = 1');
    if (!('after' in lab) || !lab.before || !lab.after) throw new Error(`no results: ${lab.error}`);
    expect(lab.after.rows).toEqual(lab.before.rows);
    expect(lab.after.rows).toHaveLength(1);
  });

  it('stops at resolution when a query names an unknown table', () => {
    const lab = inspectPipeline('sql', 'SELECT x FROM nowhere');
    expect(lab).toMatchObject({ error: null });
    expect(lab).not.toHaveProperty('plan');
  });

  it('reports a parse error rather than throwing', () => {
    expect(inspectPipeline('expr', '1 +')).toMatchObject({ error: expect.any(String) });
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
      expect(inspectPipeline('sql', crossJoin(labLimits.maxJoins + 1))).toMatchObject({
        error: expect.stringContaining('at most 2 joins'),
      });
    });

    it('refuses input over the length limit', () => {
      const long = `1${' + 1'.repeat(labLimits.maxSourceLength)}`;
      expect(inspectPipeline('expr', long)).toMatchObject({
        error: expect.stringContaining('under 3,000 characters'),
      });
    });
  });
});
