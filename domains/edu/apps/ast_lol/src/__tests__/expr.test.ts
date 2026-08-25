import { describe, expect, it } from 'vitest';
import {
  collectVars,
  evaluateExpr,
  foldExpr,
  parseExpr,
  printExpr,
  tokenizeExpr,
  type Expr,
} from '../lang/expr';

const parse = (source: string): Expr => parseExpr(tokenizeExpr(source));

describe('tokenizeExpr', () => {
  it('tokenizes with maximal munch and positions', () => {
    expect(tokenizeExpr('12 + x_1*3.25')).toEqual([
      { kind: 'number', text: '12', pos: 0 },
      { kind: 'op', text: '+', pos: 3 },
      { kind: 'ident', text: 'x_1', pos: 5 },
      { kind: 'op', text: '*', pos: 8 },
      { kind: 'number', text: '3.25', pos: 9 },
    ]);
  });

  it('does not treat a bare dot as part of a number', () => {
    expect(() => tokenizeExpr('1.')).toThrow("Unexpected character '.' at 1");
  });

  it('reports unexpected characters with their position', () => {
    expect(() => tokenizeExpr('1 + $')).toThrow("Unexpected character '$' at 4");
  });

  it('returns no tokens for whitespace-only input', () => {
    expect(tokenizeExpr('  \n\t ')).toEqual([]);
  });
});

describe('parseExpr', () => {
  it('gives * tighter binding than +', () => {
    expect(parse('1 + 2 * 3')).toEqual({
      type: 'Binary',
      op: '+',
      left: { type: 'Num', value: 1 },
      right: {
        type: 'Binary',
        op: '*',
        left: { type: 'Num', value: 2 },
        right: { type: 'Num', value: 3 },
      },
    });
  });

  it('associates - to the left', () => {
    expect(parse('1 - 2 - 3')).toEqual({
      type: 'Binary',
      op: '-',
      left: {
        type: 'Binary',
        op: '-',
        left: { type: 'Num', value: 1 },
        right: { type: 'Num', value: 2 },
      },
      right: { type: 'Num', value: 3 },
    });
  });

  it('associates ^ to the right', () => {
    expect(parse('2 ^ 3 ^ 2')).toEqual({
      type: 'Binary',
      op: '^',
      left: { type: 'Num', value: 2 },
      right: {
        type: 'Binary',
        op: '^',
        left: { type: 'Num', value: 3 },
        right: { type: 'Num', value: 2 },
      },
    });
  });

  it('binds unary minus looser than ^ (-2^2 is -(2^2))', () => {
    expect(parse('-2 ^ 2')).toEqual({
      type: 'Unary',
      op: '-',
      operand: {
        type: 'Binary',
        op: '^',
        left: { type: 'Num', value: 2 },
        right: { type: 'Num', value: 2 },
      },
    });
  });

  it('allows unary minus in an exponent', () => {
    expect(parse('2 ^ -3')).toEqual({
      type: 'Binary',
      op: '^',
      left: { type: 'Num', value: 2 },
      right: { type: 'Unary', op: '-', operand: { type: 'Num', value: 3 } },
    });
  });

  it('rejects trailing tokens', () => {
    expect(() => parse('1 2')).toThrow("Unexpected token '2' at 2");
  });

  it('rejects an unclosed paren', () => {
    expect(() => parse('(1 + 2')).toThrow('Unexpected end of input');
  });

  it('rejects empty input', () => {
    expect(() => parse('')).toThrow('Unexpected end of input');
  });
});

describe('evaluateExpr', () => {
  it('evaluates with variables from the environment', () => {
    expect(evaluateExpr(parse('x ^ 2 + y * 3'), { x: 4, y: 2 })).toBe(22);
  });

  it('throws on unknown variables', () => {
    expect(() => evaluateExpr(parse('a + b'), { a: 1 })).toThrow("Unknown variable 'b'");
  });

  it('keeps JS division semantics (1/0 is Infinity)', () => {
    expect(evaluateExpr(parse('1 / 0'), {})).toBe(Infinity);
  });
});

describe('printExpr', () => {
  it.each([
    ['1 + 2 * 3', '1 + 2 * 3'],
    ['(1 + 2) * 3', '(1 + 2) * 3'],
    ['1 - (2 - 3)', '1 - (2 - 3)'],
    ['1 - 2 - 3', '1 - 2 - 3'],
    ['a / (b * c)', 'a / (b * c)'],
    ['(2 ^ 3) ^ 2', '(2 ^ 3) ^ 2'],
    ['2 ^ 3 ^ 2', '2 ^ 3 ^ 2'],
    ['-(a * b)', '-(a * b)'],
    ['-a * b', '-a * b'],
    ['-a ^ b', '-a ^ b'],
    ['(-a) ^ b', '(-a) ^ b'],
    ['a + -b', 'a + -b'],
    ['2 ^ -3', '2 ^ -3'],
    ['2 ^ (a * b)', '2 ^ (a * b)'],
    ['--a', '--a'],
  ])('prints %s with minimal parens as %s', (source, expected) => {
    expect(printExpr(parse(source))).toBe(expected);
  });

  it('round-trips: parse(print(ast)) equals ast', () => {
    const sources = [
      '1 + 2 * 3 - 4 / 5',
      '((a))',
      'a * (b + c) * d',
      '-(a + b) ^ 2',
      '2 ^ -x ^ 2',
      'x / y / z',
      'a - (b + c) - d',
    ];
    for (const source of sources) {
      const ast = parse(source);
      expect(parse(printExpr(ast))).toEqual(ast);
    }
  });
});

describe('foldExpr', () => {
  it('folds constant subtrees bottom-up', () => {
    expect(foldExpr(parse('1 + 2 * 3 + x'))).toEqual({
      type: 'Binary',
      op: '+',
      left: { type: 'Num', value: 7 },
      right: { type: 'Var', name: 'x' },
    });
  });

  it('folds unary minus over a number', () => {
    expect(foldExpr(parse('-(2 + 3)'))).toEqual({ type: 'Num', value: -5 });
  });

  it('leaves non-finite results unfolded (1/0)', () => {
    expect(foldExpr(parse('1 / 0'))).toEqual(parse('1 / 0'));
  });

  it('leaves variables and their contexts intact', () => {
    const ast = parse('x * 2');
    expect(foldExpr(ast)).toEqual(ast);
  });
});

describe('collectVars', () => {
  it('returns sorted unique names', () => {
    expect(collectVars(parse('b + a * b - c ^ a'))).toEqual(['a', 'b', 'c']);
  });

  it('returns empty for constant expressions', () => {
    expect(collectVars(parse('1 + 2'))).toEqual([]);
  });
});
