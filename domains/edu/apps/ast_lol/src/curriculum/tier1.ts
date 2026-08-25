import { parseExpr, tokenizeExpr } from '../lang/expr';
import type { ChallengeDef, LessonDef, Tier } from './types';

/** Tier 1 — Text to tree: tokenize, parse, evaluate the Expr language. */

const tokensLesson: LessonDef = {
  id: 'tokens',
  title: 'Tokenization',
  summary: 'Turning a character stream into lexemes: kinds, positions, maximal munch.',
  reading: [
    {
      title: 'Crafting Interpreters — Scanning',
      url: 'https://craftinginterpreters.com/scanning.html',
      note: 'the same machine, built for Lox',
    },
    {
      title: 'Wikipedia — Lexical analysis',
      url: 'https://en.wikipedia.org/wiki/Lexical_analysis',
      note: 'the vocabulary: lexeme, token, maximal munch',
    },
    {
      title: 'Stanford CS143 — Compilers',
      url: 'https://web.stanford.edu/class/cs143/',
      note: 'lexical analysis handouts, with the theory underneath',
    },
  ],
};

const grammarsLesson: LessonDef = {
  id: 'grammars',
  title: 'Grammars and recursive descent',
  summary: 'Precedence and associativity as grammar shape; one function per level.',
  reading: [
    {
      title: 'Crafting Interpreters — Parsing Expressions',
      url: 'https://craftinginterpreters.com/parsing-expressions.html',
      note: 'recursive descent with the same precedence-ladder trick',
    },
    {
      title: 'Wikipedia — Recursive descent parser',
      url: 'https://en.wikipedia.org/wiki/Recursive_descent_parser',
    },
    {
      title: 'Wikipedia — Context-free grammar',
      url: 'https://en.wikipedia.org/wiki/Context-free_grammar',
      note: 'what the ladder is a special case of',
    },
    {
      title: 'AST Explorer',
      url: 'https://astexplorer.net/',
      note: 'paste JS and watch industrial parsers build trees',
    },
  ],
};

const interpretersLesson: LessonDef = {
  id: 'interpreters',
  title: 'Giving the tree meaning',
  summary: 'Tree-walking evaluation and environments — the smallest possible interpreter.',
  reading: [
    {
      title: 'Crafting Interpreters — Evaluating Expressions',
      url: 'https://craftinginterpreters.com/evaluating-expressions.html',
    },
    {
      title: 'Wikipedia — Interpreter (computing)',
      url: 'https://en.wikipedia.org/wiki/Interpreter_(computing)',
      note: 'where tree-walking sits among interpreter designs',
    },
  ],
};

const exprTokenize: ChallengeDef = {
  id: 'expr-tokenize',
  title: 'Write a tokenizer',
  summary: 'Expr source → Token[]: numbers, identifiers, operators, positions, errors.',
  signature: 'tokenize(source) → Token[]',
  entry: 'tokenize',
  difficulty: 1,
  starter: `// Token: { kind: 'number' | 'ident' | 'op' | 'lparen' | 'rparen',
//          text: string, pos: number }
//   number: digits, optionally '.' + digits  (3, 3.14 — but not .5 or 3.)
//   ident:  [A-Za-z_][A-Za-z0-9_]*
//   op:     + - * / ^      lparen: (      rparen: )
//   pos:    index of the lexeme's first character in source
// Skip spaces, tabs, newlines. Any other character:
//   throw new Error("Unexpected character '" + c + "' at " + i)
function tokenize(source) {
  const tokens = [];
  let i = 0;
  // TODO
  return tokens;
}`,
  solution: `function tokenize(source) {
  const isDigit = (c) => c >= '0' && c <= '9';
  const isIdentStart = (c) => /[A-Za-z_]/.test(c);
  const isIdentPart = (c) => /[A-Za-z0-9_]/.test(c);
  const tokens = [];
  let i = 0;
  while (i < source.length) {
    const c = source[i];
    if (c === ' ' || c === '\\t' || c === '\\n' || c === '\\r') { i++; continue; }
    if (isDigit(c)) {
      const start = i;
      while (i < source.length && isDigit(source[i])) i++;
      if (source[i] === '.' && isDigit(source[i + 1])) {
        i++;
        while (i < source.length && isDigit(source[i])) i++;
      }
      tokens.push({ kind: 'number', text: source.slice(start, i), pos: start });
      continue;
    }
    if (isIdentStart(c)) {
      const start = i;
      while (i < source.length && isIdentPart(source[i])) i++;
      tokens.push({ kind: 'ident', text: source.slice(start, i), pos: start });
      continue;
    }
    if ('+-*/^'.includes(c)) { tokens.push({ kind: 'op', text: c, pos: i }); i++; continue; }
    if (c === '(') { tokens.push({ kind: 'lparen', text: c, pos: i }); i++; continue; }
    if (c === ')') { tokens.push({ kind: 'rparen', text: c, pos: i }); i++; continue; }
    throw new Error("Unexpected character '" + c + "' at " + i);
  }
  return tokens;
}`,
  tests: [
    {
      name: 'a single number',
      args: ['42'],
      expected: [{ kind: 'number', text: '42', pos: 0 }],
    },
    {
      name: 'maximal munch: a multi-digit number is one token',
      args: ['123 + 4'],
      expected: [
        { kind: 'number', text: '123', pos: 0 },
        { kind: 'op', text: '+', pos: 4 },
        { kind: 'number', text: '4', pos: 6 },
      ],
      hint: 'Consume digits greedily in a loop before emitting the token — one token per digit is the classic first bug.',
    },
    {
      name: 'decimal numbers',
      args: ['3.14'],
      expected: [{ kind: 'number', text: '3.14', pos: 0 }],
    },
    {
      name: 'a trailing dot is not part of a number',
      args: ['1.'],
      throws: { messageIncludes: "'.' at 1" },
      hint: "Only consume '.' when a digit follows it; a bare '.' then falls through to the unexpected-character error.",
    },
    {
      name: 'identifiers may contain underscores and digits',
      args: ['x_1 * foo'],
      expected: [
        { kind: 'ident', text: 'x_1', pos: 0 },
        { kind: 'op', text: '*', pos: 4 },
        { kind: 'ident', text: 'foo', pos: 6 },
      ],
    },
    {
      name: 'operators and parens carry positions',
      args: ['(a + b) ^ 2'],
      expected: [
        { kind: 'lparen', text: '(', pos: 0 },
        { kind: 'ident', text: 'a', pos: 1 },
        { kind: 'op', text: '+', pos: 3 },
        { kind: 'ident', text: 'b', pos: 5 },
        { kind: 'rparen', text: ')', pos: 6 },
        { kind: 'op', text: '^', pos: 8 },
        { kind: 'number', text: '2', pos: 10 },
      ],
    },
    {
      name: 'positions index the original source, not the token stream',
      args: ['  7'],
      expected: [{ kind: 'number', text: '7', pos: 2 }],
      hint: 'pos is where the lexeme starts in the input string — skipped whitespace still advances it.',
    },
    {
      name: 'whitespace-only input yields no tokens',
      args: [' \t\n'],
      expected: [],
    },
    {
      name: 'empty input yields no tokens',
      args: [''],
      expected: [],
    },
    {
      name: 'unknown characters throw with their position',
      args: ['a $ b'],
      throws: { messageIncludes: "'$' at 2" },
    },
  ],
  custom: {
    describe: 'An Expr source string. The reference tokenizer supplies the expected answer.',
    placeholder: '"(x + 41) * f_2"',
    toArgs: (values) => [String(values[0])],
  },
};

const exprParse: ChallengeDef = {
  id: 'expr-parse',
  title: 'Write a recursive-descent parser',
  summary: 'Token[] → AST with the precedence ladder: + - | * / | unary - | ^.',
  signature: 'parse(tokens) → Expr',
  entry: 'parse',
  difficulty: 2,
  starter: `// AST:
//   { type: 'Num', value: number }
//   { type: 'Var', name: string }
//   { type: 'Unary', op: '-', operand: Expr }
//   { type: 'Binary', op: '+'|'-'|'*'|'/'|'^', left: Expr, right: Expr }
// Precedence, loosest to tightest:
//   + -        (left-assoc)
//   * /        (left-assoc)
//   unary -
//   ^          (right-assoc; binds tighter than unary -, so -2^2 = -(2^2))
// Errors:
//   throw new Error("Unexpected token '" + t.text + "' at " + t.pos)
//   throw new Error('Unexpected end of input')
// Consume the whole stream: trailing tokens are an error too.
function parse(tokens) {
  let i = 0;
  // TODO: one function per precedence level is the classic shape.
}`,
  solution: `function parse(tokens) {
  let i = 0;
  function fail(t) {
    if (t === undefined) throw new Error('Unexpected end of input');
    throw new Error("Unexpected token '" + t.text + "' at " + t.pos);
  }
  function atOp(op) {
    const t = tokens[i];
    return t !== undefined && t.kind === 'op' && t.text === op;
  }
  function additive() {
    let left = multiplicative();
    while (atOp('+') || atOp('-')) {
      const op = tokens[i++].text;
      left = { type: 'Binary', op: op, left: left, right: multiplicative() };
    }
    return left;
  }
  function multiplicative() {
    let left = unary();
    while (atOp('*') || atOp('/')) {
      const op = tokens[i++].text;
      left = { type: 'Binary', op: op, left: left, right: unary() };
    }
    return left;
  }
  function unary() {
    if (atOp('-')) {
      i++;
      return { type: 'Unary', op: '-', operand: unary() };
    }
    return power();
  }
  function power() {
    const base = atom();
    if (atOp('^')) {
      i++;
      // Re-enter unary so 2 ^ -3 parses and a ^ b ^ c leans right.
      return { type: 'Binary', op: '^', left: base, right: unary() };
    }
    return base;
  }
  function atom() {
    const t = tokens[i++];
    if (t === undefined) fail(t);
    if (t.kind === 'number') return { type: 'Num', value: Number(t.text) };
    if (t.kind === 'ident') return { type: 'Var', name: t.text };
    if (t.kind === 'lparen') {
      const inner = additive();
      const close = tokens[i++];
      if (close === undefined || close.kind !== 'rparen') fail(close);
      return inner;
    }
    fail(t);
  }
  const result = additive();
  if (i < tokens.length) fail(tokens[i]);
  return result;
}`,
  tests: [
    {
      name: 'a lone number',
      args: [tokenizeExpr('42')],
      expected: { type: 'Num', value: 42 },
    },
    {
      name: '* binds tighter than +',
      args: [tokenizeExpr('1 + 2 * 3')],
      expected: {
        type: 'Binary',
        op: '+',
        left: { type: 'Num', value: 1 },
        right: {
          type: 'Binary',
          op: '*',
          left: { type: 'Num', value: 2 },
          right: { type: 'Num', value: 3 },
        },
      },
      hint: 'Parse + at the outermost level and let it call down into the * / level for each operand.',
    },
    {
      name: '- associates left: 1 - 2 - 3 is (1 - 2) - 3',
      args: [tokenizeExpr('1 - 2 - 3')],
      expected: parseExpr(tokenizeExpr('1 - 2 - 3')),
      hint: 'Build left-associative chains with a while loop that folds into `left`; recursing on the right instead makes the tree lean the wrong way.',
    },
    {
      name: '^ associates right: 2 ^ 3 ^ 2 is 2 ^ (3 ^ 2)',
      args: [tokenizeExpr('2 ^ 3 ^ 2')],
      expected: parseExpr(tokenizeExpr('2 ^ 3 ^ 2')),
      hint: 'Right associativity is the mirror image: recurse for the right operand instead of looping.',
    },
    {
      name: 'parens override precedence',
      args: [tokenizeExpr('(1 + 2) * 3')],
      expected: parseExpr(tokenizeExpr('(1 + 2) * 3')),
    },
    {
      name: 'unary minus binds looser than ^: -2 ^ 2 is -(2 ^ 2)',
      args: [tokenizeExpr('-2 ^ 2')],
      expected: parseExpr(tokenizeExpr('-2 ^ 2')),
      hint: 'The unary level sits between * / and ^ — its operand is the ^ level, and ^ parses its base below unary.',
    },
    {
      name: 'unary minus is allowed in an exponent: 2 ^ -3',
      args: [tokenizeExpr('2 ^ -3')],
      expected: parseExpr(tokenizeExpr('2 ^ -3')),
      hint: "After consuming '^', parse the right side at the unary level, not the atom level.",
    },
    {
      name: 'unary minus stacks: --a',
      args: [tokenizeExpr('--a')],
      expected: parseExpr(tokenizeExpr('--a')),
    },
    {
      name: 'trailing tokens are an error',
      args: [tokenizeExpr('1 2')],
      throws: { messageIncludes: "'2' at 2" },
      hint: 'After the top-level expression returns, anything left in the stream is a syntax error.',
    },
    {
      name: 'an unclosed paren reports end of input',
      args: [tokenizeExpr('(1 + 2')],
      throws: { messageIncludes: 'Unexpected end of input' },
    },
    {
      name: 'empty input reports end of input',
      args: [[]],
      throws: { messageIncludes: 'Unexpected end of input' },
    },
    {
      name: 'a leading binary operator is an unexpected token',
      args: [tokenizeExpr('* 3')],
      throws: { messageIncludes: "'*' at 0" },
    },
  ],
  custom: {
    describe: 'An Expr source string; it is tokenized for you, and the reference parser supplies the expected AST.',
    placeholder: '"-x ^ 2 + 3 * (y - 1)"',
    toArgs: (values) => [tokenizeExpr(String(values[0]))],
  },
};

const exprEval: ChallengeDef = {
  id: 'expr-eval',
  title: 'Write an evaluator',
  summary: 'AST + environment → number: the smallest tree-walking interpreter.',
  signature: 'evaluate(ast, env) → number',
  entry: 'evaluate',
  difficulty: 1,
  starter: `// evaluate(ast, env) walks the tree and returns a number.
//   env is a plain object: { x: 4 } gives Var 'x' the value 4.
//   ^ is exponentiation. Division follows JavaScript (1/0 is Infinity).
//   Unknown variable: throw new Error("Unknown variable '" + name + "'")
//   (check own properties, so { } with no 'x' throws even though
//    inherited names like 'constructor' exist on every object).
function evaluate(ast, env) {
  // TODO
}`,
  solution: `function evaluate(ast, env) {
  switch (ast.type) {
    case 'Num':
      return ast.value;
    case 'Var':
      if (!Object.prototype.hasOwnProperty.call(env, ast.name)) {
        throw new Error("Unknown variable '" + ast.name + "'");
      }
      return env[ast.name];
    case 'Unary':
      return -evaluate(ast.operand, env);
    case 'Binary': {
      const l = evaluate(ast.left, env);
      const r = evaluate(ast.right, env);
      if (ast.op === '+') return l + r;
      if (ast.op === '-') return l - r;
      if (ast.op === '*') return l * r;
      if (ast.op === '/') return l / r;
      return l ** r;
    }
  }
}`,
  tests: [
    {
      name: 'constants fold to themselves',
      args: [parseExpr(tokenizeExpr('42')), {}],
      expected: 42,
    },
    {
      name: 'precedence is already in the tree',
      args: [parseExpr(tokenizeExpr('1 + 2 * 3')), {}],
      expected: 7,
    },
    {
      name: 'variables come from the environment',
      args: [parseExpr(tokenizeExpr('x ^ 2 + y * 3')), { x: 4, y: 2 }],
      expected: 22,
    },
    {
      name: 'an unknown variable throws by name',
      args: [parseExpr(tokenizeExpr('a + b')), { a: 1 }],
      throws: { messageIncludes: "Unknown variable 'b'" },
    },
    {
      name: "inherited object properties are not variables ('constructor')",
      args: [parseExpr(tokenizeExpr('constructor')), {}],
      throws: { messageIncludes: "Unknown variable 'constructor'" },
      hint: "`'constructor' in env` is true for every plain object — check own properties (Object.prototype.hasOwnProperty.call).",
    },
    {
      name: 'unary minus negates',
      args: [parseExpr(tokenizeExpr('-(2 + 3)')), {}],
      expected: -5,
    },
    {
      name: '^ is exponentiation and leans right',
      args: [parseExpr(tokenizeExpr('2 ^ 3 ^ 2')), {}],
      expected: 512,
    },
    {
      name: 'division follows JavaScript: 1 / 0 is Infinity',
      args: [parseExpr(tokenizeExpr('1 / 0')), {}],
      expected: Infinity,
      hint: 'No special-casing here — this is Expr, not SQL. Tier 4 makes the opposite choice, on purpose.',
    },
    {
      name: 'a zero-valued variable is still a value',
      args: [parseExpr(tokenizeExpr('x + 1')), { x: 0 }],
      expected: 1,
      hint: 'Guard with hasOwnProperty, not truthiness — `env[name] || throw` loses 0.',
    },
  ],
  custom: {
    describe:
      'An Expr source string, then optionally an environment object. Example: "x + 1", { x: 41 }',
    placeholder: '"x ^ 2 - 1", { x: 12 }',
    toArgs: (values) => [parseExpr(tokenizeExpr(String(values[0]))), values[1] ?? {}],
  },
};

export const tier1: Tier = {
  id: 'tier1',
  number: 1,
  title: 'Text to tree',
  subtitle: 'Tokenize, parse, and evaluate a small expression language.',
  steps: [
    { kind: 'lesson', lesson: tokensLesson },
    { kind: 'challenge', challenge: exprTokenize },
    { kind: 'lesson', lesson: grammarsLesson },
    { kind: 'challenge', challenge: exprParse },
    { kind: 'lesson', lesson: interpretersLesson },
    { kind: 'challenge', challenge: exprEval },
  ],
};
