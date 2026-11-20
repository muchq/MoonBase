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

  it('runs an AstQL query naive and optimized, with the same rows', () => {
    const lab = inspectPipeline('sql', 'SELECT u.name FROM users u WHERE u.id = 1');
    if (!('after' in lab) || !lab.before || !lab.after) throw new Error(`no results: ${lab.error}`);
    expect(lab.before.rows).toEqual([{ name: 'ada' }]);
    expect(lab.after.rows).toEqual([{ name: 'ada' }]);
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
