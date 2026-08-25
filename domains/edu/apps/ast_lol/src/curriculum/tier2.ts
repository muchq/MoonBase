import { foldExpr, parseExpr, printExpr, tokenizeExpr } from '../lang/expr';
import { firstDiff, show } from '../grader/deepEqual';
import type { ChallengeDef, LessonDef, Tier } from './types';

/** Tier 2 — Working the tree: traversal, printing, and rewriting Expr ASTs. */

const parse = (s: string) => parseExpr(tokenizeExpr(s));

const traversalLesson: LessonDef = {
  id: 'traversal',
  title: 'Traversal: visitors and folds',
  summary: 'Walking trees without losing your place: recursion patterns, visitors, rebuilds.',
  reading: [
    {
      title: 'Babel Plugin Handbook — Visitors',
      url: 'https://github.com/jamiebuilds/babel-handbook/blob/master/translations/en/plugin-handbook.md',
      note: 'the visitor pattern in an industrial AST toolkit',
    },
    {
      title: 'ESLint — Custom rules',
      url: 'https://eslint.org/docs/latest/extend/custom-rules',
      note: 'node-type callbacks over ESTree: traversal as a product API',
    },
    {
      title: 'Wikipedia — Visitor pattern',
      url: 'https://en.wikipedia.org/wiki/Visitor_pattern',
    },
  ],
};

const rewritesLesson: LessonDef = {
  id: 'rewrites',
  title: 'Rewrites that preserve meaning',
  summary: 'Bottom-up transformation, and why the burden of proof is on the rewrite.',
  reading: [
    {
      title: 'Wikipedia — Constant folding',
      url: 'https://en.wikipedia.org/wiki/Constant_folding',
      note: 'this tier’s transform, as compilers do it',
    },
    {
      title: 'Wikipedia — Rewriting',
      url: 'https://en.wikipedia.org/wiki/Rewriting',
      note: 'term rewriting: the formal frame around “apply rules until done”',
    },
    {
      title: 'Crafting Interpreters — A Map of the Territory',
      url: 'https://craftinginterpreters.com/a-map-of-the-territory.html',
      note: 'where tree transforms sit in a real compiler pipeline',
    },
  ],
};

const exprVars: ChallengeDef = {
  id: 'expr-vars',
  title: 'Collect the variables',
  summary: 'A traversal warm-up: every Var name, deduplicated and sorted.',
  signature: 'collectVars(ast) → string[]',
  entry: 'collectVars',
  difficulty: 1,
  starter: `// Return the names of all Var nodes, without duplicates,
// sorted ascending (default string order).
function collectVars(ast) {
  // TODO
}`,
  solution: `function collectVars(ast) {
  const names = new Set();
  function visit(e) {
    if (e.type === 'Var') names.add(e.name);
    if (e.type === 'Unary') visit(e.operand);
    if (e.type === 'Binary') { visit(e.left); visit(e.right); }
  }
  visit(ast);
  return Array.from(names).sort();
}`,
  tests: [
    {
      name: 'finds variables at any depth',
      args: [parse('(a + 1) * -(b / c)')],
      expected: ['a', 'b', 'c'],
    },
    {
      name: 'deduplicates repeated names',
      args: [parse('x * x + x')],
      expected: ['x'],
      hint: 'A Set (or an object used as one) deduplicates as you collect.',
    },
    {
      name: 'sorts the result',
      args: [parse('zeta + alpha * mid')],
      expected: ['alpha', 'mid', 'zeta'],
    },
    {
      name: 'a constant expression has no variables',
      args: [parse('1 + 2 ^ 3')],
      expected: [],
    },
    {
      name: 'a lone variable',
      args: [parse('solo')],
      expected: ['solo'],
    },
  ],
  custom: {
    describe: 'An Expr source string; the reference traversal supplies the expected list.',
    placeholder: '"b + a * b - c"',
    toArgs: (values) => [parse(String(values[0]))],
  },
};

/**
 * Printer grading is two-layered: exact-string equality against the spec's
 * formatting, plus a round-trip check through the reference parser so a
 * wrong string also explains *which tree* it actually encodes.
 */
const printCheck = (expectedText: string) => (actual: unknown, ctx: { args: unknown[] }) => {
  if (typeof actual !== 'string') {
    return {
      pass: false,
      message: `Expected a string, got ${show(actual)}.`,
      expectedText,
    };
  }
  if (actual === expectedText) return { pass: true };
  let reparsed: unknown = null;
  try {
    reparsed = parseExpr(tokenizeExpr(actual));
  } catch {
    return {
      pass: false,
      message: 'Your output does not parse as an Expr at all.',
      expectedText,
      actualText: actual,
    };
  }
  const diff = firstDiff(ctx.args[0], reparsed);
  if (diff === null) {
    return {
      pass: false,
      message:
        'Your output parses back to the right tree but the formatting is off — the spec fixes single spaces around binary operators, a tight unary minus, and *minimal* parentheses.',
      expectedText,
      actualText: actual,
    };
  }
  return {
    pass: false,
    message: `Reparsing your output yields a different tree (first difference at result${diff.path}: expected ${show(diff.expected)}, got ${show(diff.actual)}) — parentheses are missing where precedence needs them.`,
    expectedText,
    actualText: actual,
  };
};

function printTest(source: string, name: string, hint?: string) {
  const ast = parse(source);
  const expected = printExpr(ast);
  return { name, args: [ast] as unknown[], check: printCheck(expected), hint };
}

const exprPrint: ChallengeDef = {
  id: 'expr-print',
  title: 'Write a pretty-printer',
  summary: 'AST → source with minimal parentheses — precedence in reverse.',
  signature: 'print(ast) → string',
  entry: 'print',
  difficulty: 3,
  starter: `// Turn an AST back into source text.
// Formatting: single spaces around binary ops ("a + b"), unary minus
// tight against its operand ("-a"), no spaces inside parens.
// Parenthesize a child only when reparsing would otherwise change the
// tree: lower precedence than the parent, or equal precedence on the
// side the parent does not associate to.
//   + -  level 1     * /  level 2     unary -  level 3     ^  level 4
//   ^ associates right; its right operand re-enters the unary level,
//   so -x and 2 ^ -3 print without parens.
function print(ast) {
  // TODO
}`,
  solution: `function print(ast) {
  const PREC = { '+': 1, '-': 1, '*': 2, '/': 2, '^': 4 };
  const UNARY = 3;
  function prec(e) {
    if (e.type === 'Binary') return PREC[e.op];
    if (e.type === 'Unary') return UNARY;
    return 5;
  }
  function wrap(child, minPrec) {
    const s = print(child);
    return prec(child) < minPrec ? '(' + s + ')' : s;
  }
  switch (ast.type) {
    case 'Num':
      return String(ast.value);
    case 'Var':
      return ast.name;
    case 'Unary':
      return '-' + wrap(ast.operand, UNARY);
    case 'Binary': {
      const p = PREC[ast.op];
      const leftMin = ast.op === '^' ? p + 1 : p;
      const rightMin = ast.op === '^' ? UNARY : p + 1;
      return wrap(ast.left, leftMin) + ' ' + ast.op + ' ' + wrap(ast.right, rightMin);
    }
  }
}`,
  tests: [
    printTest('1 + 2 * 3', 'no parens when precedence already says it'),
    printTest(
      '(1 + 2) * 3',
      'parens where the tree needs them',
      'Compare the child’s precedence to the parent’s: lower means parens.',
    ),
    printTest(
      '1 - (2 - 3)',
      'right side of a left-associative op needs parens at equal precedence',
      '1 - 2 - 3 and 1 - (2 - 3) are different trees; only the second gets parens.',
    ),
    printTest('1 - 2 - 3', 'left side of a left-associative op stays bare'),
    printTest(
      '(2 ^ 3) ^ 2',
      'left side of ^ needs parens at equal precedence',
      '^ associates right, so the association rule mirrors: parens go on the left.',
    ),
    printTest('2 ^ 3 ^ 2', 'right side of ^ stays bare'),
    printTest(
      '-(a * b)',
      'unary minus parenthesizes looser operands',
      '-a * b reparses as (-a) * b — a different tree — so -(a * b) keeps its parens.',
    ),
    printTest('-a ^ b', 'unary minus over ^ stays bare (unary binds looser)'),
    printTest('(-a) ^ b', 'a negated base needs parens'),
    printTest('2 ^ -3', 'a negative exponent stays bare'),
    printTest('a + -b', 'unary minus as a right operand'),
    printTest('a / (b * c)', 'division keeps required parens on the right'),
  ],
  custom: {
    describe:
      'An Expr source string; it is parsed, and your printer’s output is compared with the reference printer’s.',
    placeholder: '"-(x + 1) * y ^ 2"',
    toArgs: (values) => {
      const ast = parse(String(values[0]));
      return [ast];
    },
  },
  check: (actual, ctx) => printCheck(printExpr(ctx.args[0] as never))(actual, ctx),
};

const exprFold: ChallengeDef = {
  id: 'expr-fold',
  title: 'Write a constant folder',
  summary: 'Your first meaning-preserving rewrite — with a guard rail.',
  signature: 'fold(ast) → Expr',
  entry: 'fold',
  difficulty: 2,
  starter: `// Rebuild the tree bottom-up. When a Binary's two children are both
// Num — or a Unary's child is — replace the node with a Num holding the
// result (same operator semantics as your evaluator).
// Guard rail: only fold when the result is a finite number
// (Number.isFinite). 1/0 must stay a division: no source text could
// round-trip a Num holding Infinity.
// Never mutate the input tree — return a new one where anything changed.
function fold(ast) {
  // TODO
}`,
  solution: `function fold(ast) {
  function apply(op, l, r) {
    if (op === '+') return l + r;
    if (op === '-') return l - r;
    if (op === '*') return l * r;
    if (op === '/') return l / r;
    return l ** r;
  }
  switch (ast.type) {
    case 'Num':
    case 'Var':
      return ast;
    case 'Unary': {
      const operand = fold(ast.operand);
      if (operand.type === 'Num' && Number.isFinite(-operand.value)) {
        return { type: 'Num', value: -operand.value };
      }
      return { type: 'Unary', op: '-', operand: operand };
    }
    case 'Binary': {
      const left = fold(ast.left);
      const right = fold(ast.right);
      if (left.type === 'Num' && right.type === 'Num') {
        const value = apply(ast.op, left.value, right.value);
        if (Number.isFinite(value)) return { type: 'Num', value: value };
      }
      return { type: 'Binary', op: ast.op, left: left, right: right };
    }
  }
}`,
  tests: [
    {
      name: 'a fully constant tree folds to one Num',
      args: [parse('1 + 2 * 3')],
      expected: { type: 'Num', value: 7 },
    },
    {
      name: 'folding is bottom-up: constants fold under a variable',
      args: [parse('x + 2 * 3')],
      expected: foldExpr(parse('x + 2 * 3')),
      hint: 'Fold children first, then look at what came back — 2 * 3 becomes 6 even though x + … cannot fold.',
    },
    {
      name: 'unary minus folds',
      args: [parse('-(2 + 3)')],
      expected: { type: 'Num', value: -5 },
    },
    {
      name: 'nested constants fold through several levels',
      args: [parse('(1 + 1) ^ (2 + 1)')],
      expected: { type: 'Num', value: 8 },
    },
    {
      name: 'variables block folding without breaking it around them',
      args: [parse('(a + 1) * (2 + 3)')],
      expected: foldExpr(parse('(a + 1) * (2 + 3)')),
    },
    {
      name: 'the guard rail: 1 / 0 stays a division',
      args: [parse('1 / 0')],
      expected: parse('1 / 0'),
      hint: 'Check Number.isFinite before folding — Infinity and NaN have no literal in Expr, so folding them breaks print-ability.',
    },
    {
      name: '0 / 0 stays a division too (NaN is not finite)',
      args: [parse('0 / 0')],
      expected: parse('0 / 0'),
    },
    {
      name: 'already-minimal trees come back equal',
      args: [parse('x * y')],
      expected: parse('x * y'),
    },
  ],
  custom: {
    describe: 'An Expr source string; the reference folder supplies the expected tree.',
    placeholder: '"x + (3 - 1) ^ 4"',
    toArgs: (values) => [parse(String(values[0]))],
  },
};

export const tier2: Tier = {
  id: 'tier2',
  number: 2,
  title: 'Working the tree',
  subtitle: 'Traverse, print, and rewrite ASTs without changing what they mean.',
  steps: [
    { kind: 'lesson', lesson: traversalLesson },
    { kind: 'challenge', challenge: exprVars },
    { kind: 'challenge', challenge: exprPrint },
    { kind: 'lesson', lesson: rewritesLesson },
    { kind: 'challenge', challenge: exprFold },
  ],
};
