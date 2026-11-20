/** Short retrieval and reasoning checks; distinct from implementation tests. */
export interface PracticeCheck {
  kind: 'Predict' | 'Trace' | 'Repair' | 'Transfer';
  question: string;
  options: string[];
  answer: number;
  explanation: string;
}
const check = (
  kind: PracticeCheck['kind'],
  question: string,
  options: string[],
  answer: number,
  explanation: string,
): PracticeCheck => ({ kind, question, options, answer, explanation });
export const lessonPractice: Record<string, PracticeCheck[]> = {
  welcome: [
    check(
      'Predict',
      'Which representation should a constant folder consume?',
      ['Raw source text', 'An expression tree', 'A list of characters'],
      1,
      'The parser already resolved precedence. A folder can operate directly on structured children.',
    ),
    check(
      'Transfer',
      'Does a green optimizer test bank prove equivalence for every database?',
      [
        'Yes, if it uses enough rows',
        'No; it supplies evidence over the tested inputs',
        'Yes, if the reference solution also passes',
      ],
      1,
      'A single counterexample disproves equivalence. Finite examples do not establish a universal statement.',
    ),
  ],
  tokens: [
    check(
      'Predict',
      'How many tokens does 123 + x contain?',
      ['Five', 'Three', 'Seven'],
      1,
      'Maximal munch consumes all digits into one number token, then + and x.',
    ),
    check(
      'Repair',
      'Why must a tokenizer peek beyond the dot in 1.?',
      [
        'To accept 1. as a decimal',
        'To reject all integers',
        'To consume a decimal point only when a digit follows',
      ],
      2,
      'Expr scans 1 as a number, then reports the unsupported dot at its position.',
    ),
  ],
  grammars: [
    check(
      'Trace',
      'Which tree does 8 - 3 - 1 produce?',
      ['8 - (3 - 1)', '(8 - 3) - 1', 'Either tree is equivalent'],
      1,
      'Subtraction associates left. Accumulating the left subtree in a loop encodes that decision.',
    ),
    check(
      'Predict',
      'What does -2 ^ 2 evaluate to in Expr?',
      ['4', '-4', 'A syntax error'],
      1,
      'Power binds tighter than unary minus: the tree is Unary(-, Power(2,2)).',
    ),
  ],
  interpreters: [
    check(
      'Repair',
      'Which environment lookup preserves a binding of x to zero?',
      ['if (env.x) ...', 'Check that x is an own property, then read it', 'Use env.x || an error'],
      1,
      'Presence differs from truthiness, and inherited properties are not bindings.',
    ),
    check(
      'Transfer',
      'Which stage decides whether 1 + 2 * 3 groups multiplication first?',
      ['Evaluator', 'Tokenizer', 'Parser'],
      2,
      'The evaluator follows the tree; precedence was already encoded by parsing.',
    ),
  ],
  traversal: [
    check(
      'Trace',
      'When should a bottom-up rewrite process the parent?',
      ['Before either child', 'After both children', 'Only at the root'],
      1,
      'Exit order lets the parent inspect the already-rewritten children.',
    ),
    check(
      'Repair',
      'A variable collector misses variables only on the right. What is the likely bug?',
      ['It forgot to visit Binary.right', 'It sorts names incorrectly', 'It used a Set'],
      0,
      'Traversal must cover every child field. Sorting and deduplication do not recover unvisited nodes.',
    ),
  ],
  rewrites: [
    check(
      'Predict',
      'Which print preserves the tree a - (b - c)?',
      ['a - b - c', 'a - (b - c)', '(a - b) - c'],
      1,
      'The right child at equal precedence needs parentheses for a left-associative operator.',
    ),
    check(
      'Transfer',
      'Why keep 1 / 0 as a division in the folder?',
      [
        'Infinity is not finite and has no Expr numeric literal',
        'All division must remain unfolded',
        'Division is left-associative',
      ],
      0,
      'A rewrite must respect the downstream representation contract as well as evaluation.',
    ),
  ],
  'sql-subset': [
    check(
      'Predict',
      'What does the tokenizer do with 1 --2?',
      [
        'Emits two minus operators',
        'Emits 1 and discards the comment',
        'Throws on the second minus',
      ],
      1,
      'Adjacent minuses start a line comment. A separating space changes the tokenization.',
    ),
    check(
      'Transfer',
      'Why keep both raw string text and decoded value?',
      [
        'They must always be equal',
        'The value supports evaluation; the raw lexeme supports diagnostics',
        'The parser needs duplicate tokens',
      ],
      1,
      "'it''s' and its decoded value carry different useful information.",
    ),
  ],
  pratt: [
    check(
      'Trace',
      'How does NOT a = b AND c group?',
      ['NOT (a = b AND c)', '(NOT (a = b)) AND c', '((NOT a) = b) AND c'],
      1,
      'NOT binds less tightly than comparison and more tightly than AND.',
    ),
    check(
      'Repair',
      'For a left-associative binary operator of power p, what power parses its right operand?',
      ['p + 1', 'p - 1', 'Always zero'],
      0,
      'The right side must not swallow another operator of equal binding power.',
    ),
  ],
  clauses: [
    check(
      'Predict',
      'What should the expression parser do when it reaches ORDER?',
      [
        'Consume it and continue',
        'Return without consuming it',
        'Reject every query with ORDER BY',
      ],
      1,
      'The statement parser owns clause keywords; Pratt parsing stops at non-operators.',
    ),
    check(
      'Repair',
      'Why check that the final token was consumed?',
      ['To reject silently ignored trailing garbage', 'To sort the tokens', 'To resolve columns'],
      0,
      'Accepting a valid prefix can silently change what the author meant.',
    ),
  ],
  resolution: [
    check(
      'Predict',
      'Both users and orders contain id. What should an unqualified id resolve to?',
      ['The first table', 'The last table', 'An ambiguous-column error'],
      2,
      'Binding choice must not depend on table order when multiple owners exist.',
    ),
    check(
      'Transfer',
      'Does SELECT nam FROM users require a parser fix?',
      [
        'Yes, change the grammar',
        'No, resolution checks the name against the catalog',
        'No, silently rename it',
      ],
      1,
      'The syntax is valid. Semantic analysis reports that the column does not exist.',
    ),
  ],
  'logical-plans': [
    check(
      'Predict',
      'Why is Sort below Project for SELECT name FROM products ORDER BY price?',
      [
        'Sort needs price before projection drops it',
        'Projection is always cheaper',
        'SQL requires alphabetical column order',
      ],
      0,
      'Operators need their inputs available. Later pruning must preserve sort dependencies.',
    ),
    check(
      'Trace',
      'What shape represents A JOIN B JOIN C in the naive planner?',
      ['Join(A, Join(B,C))', 'Join(Join(A,B),C)', 'Three independent scans'],
      1,
      'The planner builds a left-deep stack in source order.',
    ),
  ],
  execution: [
    check(
      'Predict',
      'What is NULL AND FALSE?',
      ['null', 'true', 'false'],
      2,
      'False absorbs unknown in Kleene AND. Returning immediately on null is wrong.',
    ),
    check(
      'Repair',
      'DESC sends nulls to the front. Where should the direction flip occur?',
      [
        'After null placement returns, on non-null comparisons only',
        'Around the entire comparator',
        'Before evaluating keys',
      ],
      0,
      'AstQL keeps nulls last in both directions, so their placement cannot be negated.',
    ),
  ],
  optimizers: [
    check(
      'Predict',
      'Can TRUE AND u.id always be replaced by u.id?',
      [
        'Yes, boolean identity',
        'Only when u.id is proven boolean-or-null',
        'Only when id is nonzero',
      ],
      1,
      'AND coerces numeric ids to false; removing it can change a surrounding comparison.',
    ),
    check(
      'Transfer',
      'A predicate refers to both sides of a join. Where can it move under the taught rule?',
      ['To either scan', 'To both scans independently', 'It must stay above the join'],
      2,
      'Neither side alone has the columns needed to evaluate it.',
    ),
  ],
  layout: [
    check(
      'Predict',
      'A formatter output reparses to a different tree. What should you fix first?',
      ['Indentation', 'Parentheses or token rendering', 'The line width'],
      1,
      'Semantic identity comes before layout style.',
    ),
    check(
      'Transfer',
      'What should format(parse(format(tree, w)), w) produce?',
      [
        'The same formatted text',
        'A more compact layout each time',
        'The original author’s whitespace',
      ],
      0,
      'Canonical formatting is idempotent at a fixed width.',
    ),
  ],
};

export const tierGuide: Record<string, { outcome: string; prerequisites: string; effort: string }> =
  {
    tier0: {
      outcome: 'Read the pipeline and choose a destination.',
      prerequisites: 'Comfort with JavaScript functions and objects.',
      effort: '10–15 min',
    },
    tier1: {
      outcome: 'Build a complete expression interpreter.',
      prerequisites: 'Orientation; recursion and arrays.',
      effort: '2–4 hours',
    },
    tier2: {
      outcome: 'Explain and implement traversals that preserve meaning.',
      prerequisites: 'Expression ASTs and evaluation.',
      effort: '2–4 hours',
    },
    tier3: {
      outcome: 'Turn SQL text into a structured query.',
      prerequisites: 'Tokenization, recursive descent, precedence.',
      effort: '3–6 hours',
    },
    tier4: {
      outcome: 'Execute queries, including nulls, joins, and stable sorting.',
      prerequisites: 'SQL syntax (through tier 3).',
      effort: '4–8 hours',
    },
    tier5: {
      outcome: 'Justify rewrites, reduce cost, and search for counterexamples.',
      prerequisites: 'Query plans and execution (tier 4).',
      effort: '4–8 hours',
    },
    tier6: {
      outcome: 'Build a canonical formatter with width-aware layout.',
      prerequisites: 'SQL syntax (tier 3); no execution or optimization required.',
      effort: '3–5 hours',
    },
  };
