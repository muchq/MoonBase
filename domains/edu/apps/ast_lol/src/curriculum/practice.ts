import { parseExpr, tokenizeExpr } from '../lang/expr';
import { evalRowExpr, exprRefs, isPredicate, parseSqlExpr, tokenizeSql, type SqlExpr, type Row } from '../lang/sql';
import { tier4 } from './tier4';
import type { ChallengeDef } from './types';

const expr = (source: string) => parseExpr(tokenizeExpr(source));
const sqlExpr = (source: string) => parseSqlExpr(tokenizeSql(source));

export const walkChallenge: ChallengeDef = {
  id: 'expr-walk', title: 'Trace enter and exit order',
  summary: 'Build a reusable traversal trace, then use it to reason about bottom-up passes.',
  signature: 'trace(ast) → string[]', entry: 'trace', difficulty: 2, immutableInputs: true,
  starter: `function trace(ast) {
  // Emit "enter:" + node.type before visiting children, "exit:" + node.type after.
  // Visit Binary.left before Binary.right; Unary has one operand.
  // TODO
}`,
  solution: `function trace(ast) {
  const events = [];
  function visit(node) {
    events.push('enter:' + node.type);
    if (node.type === 'Unary') visit(node.operand);
    if (node.type === 'Binary') { visit(node.left); visit(node.right); }
    events.push('exit:' + node.type);
  }
  visit(ast);
  return events;
}`,
  tests: [
    { name: 'a leaf enters and exits', args: [expr('2')], expected: ['enter:Num', 'exit:Num'] },
    { name: 'variables are leaves too', args: [expr('x')], expected: ['enter:Var', 'exit:Var'] },
    { name: 'unary exits after its operand', args: [expr('-x')], expected: ['enter:Unary', 'enter:Var', 'exit:Var', 'exit:Unary'], hint: 'Push the exit event after the recursive call.' },
    { name: 'left child precedes right child', args: [expr('x + 1')], expected: ['enter:Binary', 'enter:Var', 'exit:Var', 'enter:Num', 'exit:Num', 'exit:Binary'] },
    { name: 'nested children finish before their parent', args: [expr('x + 2 * -y')], expected: ['enter:Binary', 'enter:Var', 'exit:Var', 'enter:Binary', 'enter:Num', 'exit:Num', 'enter:Unary', 'enter:Var', 'exit:Var', 'exit:Unary', 'exit:Binary', 'exit:Binary'] },
  ],
  custom: { describe: 'An Expr source string; it is parsed before tracing.', placeholder: '"x + 2 * -y"', toArgs: v => [expr(String(v[0]))] },
};

// Reuse the executor's revealable evaluator as the solution for its earlier
// milestone, and as provided code for the operator drills. No learner draft
// is silently imported; these helpers always have the documented behavior.
const executeStep = tier4.steps.find(s => s.kind === 'challenge' && s.challenge.id === 'sql-execute');
if (executeStep?.kind !== 'challenge') throw new Error('Executor milestone requires sql-execute');
const executeSource = executeStep.challenge.solution;
export const ROW_EVAL_HELPERS = executeSource.slice(executeSource.indexOf('  // AND'), executeSource.indexOf('  switch (plan.type)'));

export const rowExprChallenge: ChallengeDef = {
  id: 'sql-row-expr', title: 'Evaluate one row',
  summary: 'Practice SQL coercion and three-valued logic before building an executor.',
  signature: 'evaluateRow(expr, row) → SqlValue', entry: 'evaluateRow', difficulty: 3, immutableInputs: true,
  starter: `function evaluateRow(expr, row) {
  // Row keys are qualified: { 'u.id': 1, 'u.city': null }.
  // Implement the expression semantics from the execution lesson.
  // TODO
}`,
  solution: `function evaluateRow(expr, row) {\n${ROW_EVAL_HELPERS}\nreturn evalExpr(expr, row);\n}`,
  tests: [
    ...[true, false, null].flatMap(a => [true, false, null].flatMap(b => ['AND', 'OR'].map(op => {
      const e = sqlExpr(`${String(a)} ${op} ${String(b)}`);
      return { name: `${a} ${op} ${b}`, args: [e, {}], expected: evalRowExpr(e, {}), hint: 'For AND, false absorbs null. For OR, true absorbs null. Coerce before applying the truth table.' };
    }))),
    ...['NOT NULL', 'NOT 5', 'TRUE AND 5', 'FALSE OR 5', '4 / 0', "1 = '1'", 'u.city IS NULL', 'u.missing + 2', 'u.id * 3 + 1'].map(source => {
      const e = sqlExpr(source); const row = { 'u.id': 2, 'u.city': null };
      return { name: source, args: [e, row], expected: evalRowExpr(e, row) };
    }),
  ],
  custom: { describe: 'An AstQL expression and a row with qualified keys.', placeholder: '"u.id > 1 AND u.city IS NULL", { "u.id": 2, "u.city": null }', toArgs: v => [sqlExpr(String(v[0])), v[1] ?? {}] },
};

export const joinChallenge: ChallengeDef = {
  id: 'sql-join', title: 'Join two row streams',
  summary: 'Isolate pair generation, duplicate preservation, and exact-true filtering.',
  signature: 'joinRows(left, right, on) → Row[]', entry: 'joinRows', difficulty: 2, immutableInputs: true,
  prelude: ROW_EVAL_HELPERS,
  starter: `function joinRows(left, right, on) {
  // evalExpr(on, mergedRow) is provided.
  // Preserve left-row order, then right-row order. Only true matches.
  // TODO
}`,
  solution: `function joinRows(left, right, on) {
  const out = [];
  for (const l of left) for (const r of right) {
    const row = { ...l, ...r };
    if (evalExpr(on, row) === true) out.push(row);
  }
  return out;
}`,
  tests: [
    { name: 'one match', args: [[{'l.id': 1}], [{'r.id': 1}], sqlExpr('l.id = r.id')], expected: [{'l.id':1,'r.id':1}] },
    { name: 'null is not a match', args: [[{'l.id': null}], [{'r.id': null}], sqlExpr('l.id = r.id')], expected: [], hint: 'NULL = NULL is unknown; joins keep exactly true.' },
    { name: 'empty left stream', args: [[], [{'r.id':1}], sqlExpr('TRUE')], expected: [] },
    { name: 'empty right stream', args: [[{'l.id':1}], [], sqlExpr('TRUE')], expected: [] },
    { name: 'duplicates survive, in left then right order', args: [[{'l.id':2},{'l.id':1}], [{'r.id':7},{'r.id':7}], sqlExpr('TRUE')], expected: [{'l.id':2,'r.id':7},{'l.id':2,'r.id':7},{'l.id':1,'r.id':7},{'l.id':1,'r.id':7}] },
    { name: 'truthy numbers are not true', args: [[{'l.id':1}], [{'r.id':1}], sqlExpr('1')], expected: [] },
  ],
  custom: { describe: 'Left rows, right rows, and an ON expression string.', placeholder: '[{"l.id":1}], [{"r.id":1}], "l.id = r.id"', toArgs: v => [v[0], v[1], sqlExpr(String(v[2]))] },
};

export const sortChallenge: ChallengeDef = {
  id: 'sql-sort', title: 'Repair a nulls-last sort',
  summary: 'Fix a plausible comparator: DESC must not move nulls to the front.',
  signature: 'sortRows(rows, keys) → Row[]', entry: 'sortRows', difficulty: 3, immutableInputs: true,
  prelude: ROW_EVAL_HELPERS,
  starter: `function sortRows(rows, keys) {
  // This implementation has a direction bug. Repair it, keeping stable ties.
  return [...rows].sort((a, b) => {
    for (const key of keys) {
      const x = evalExpr(key.expr, a), y = evalExpr(key.expr, b);
      const cmp = x === y ? 0 : x === null ? 1 : y === null ? -1 : x < y ? -1 : 1;
      if (cmp) return key.dir === 'DESC' ? -cmp : cmp;
    }
    return 0;
  });
}`,
  solution: `function sortRows(rows, keys) {
  return [...rows].sort((a, b) => {
    for (const key of keys) {
      const x = evalExpr(key.expr, a), y = evalExpr(key.expr, b);
      if (x === y) continue;
      if (x === null) return 1;
      if (y === null) return -1;
      const cmp = x < y ? -1 : x > y ? 1 : 0;
      if (cmp) return key.dir === 'DESC' ? -cmp : cmp;
    }
    return 0;
  });
}`,
  tests: [
    ...(['ASC','DESC'] as const).map(dir => ({ name: `nulls stay last in ${dir}`, args: [[{'t.x':null},{'t.x':2},{'t.x':1}], [{expr: sqlExpr('t.x'),dir}]], expected: (dir === 'ASC' ? [1,2,null] : [2,1,null]).map(x=>({'t.x':x})), hint: 'Handle null placement before applying the direction to the non-null comparison.' })),
    { name: 'ties preserve input order', args: [[{'t.x':1,'t.id':2},{'t.x':1,'t.id':1}], [{expr:sqlExpr('t.x'),dir:'DESC'}]], expected: [{'t.x':1,'t.id':2},{'t.x':1,'t.id':1}] },
    { name: 'later keys break ties', args: [[{'t.x':1,'t.id':1},{'t.x':1,'t.id':2}], [{expr:sqlExpr('t.x'),dir:'ASC'},{expr:sqlExpr('t.id'),dir:'DESC'}]], expected: [{'t.x':1,'t.id':2},{'t.x':1,'t.id':1}] },
    { name: 'empty stream', args: [[], [{expr:sqlExpr('t.x'),dir:'ASC'}]], expected: [] },
    { name: 'no keys leaves order intact', args: [[{'t.x':2},{'t.x':1}], []], expected: [{'t.x':2},{'t.x':1}] },
  ],
  custom: { describe: 'Rows, a sort expression string, and optional ASC/DESC.', placeholder: '[{"t.x":null},{"t.x":1},{"t.x":3}], "t.x", "DESC"', toArgs: v => [v[0], [{expr:sqlExpr(String(v[1])),dir:v[2] === 'DESC' ? 'DESC' : 'ASC'}]] },
};

export const predicateChallenge: ChallengeDef = {
  id: 'sql-predicate-type', title: 'Prove a rewrite precondition',
  summary: 'Conservatively identify expressions that return only boolean or null.',
  signature: 'isPredicate(expr) → boolean', entry: 'isPredicate', difficulty: 2, immutableInputs: true,
  starter: `function isPredicate(expr) {
  // Return true only for a boolean/null literal, comparison, AND/OR,
  // NOT, or IS NULL. Columns have unknown types; arithmetic is not a predicate.
  // TODO
}`,
  solution: `function isPredicate(expr) {
  if (expr.type === 'Lit') return expr.value === null || typeof expr.value === 'boolean';
  if (expr.type === 'Not' || expr.type === 'IsNull') return true;
  return expr.type === 'Binary' && ['AND','OR','=','<>','<','<=','>','>='].includes(expr.op);
}`,
  tests: ['TRUE','FALSE','NULL','5',"'true'",'u.id','u.id + 1','-u.id','u.id = 1','u.id <> 1','u.id < 1','u.id <= 1','u.id > 1','u.id >= 1','NOT u.id','u.id IS NULL','u.id AND 5','FALSE OR u.id'].map(source => ({name:source,args:[sqlExpr(source)],expected:isPredicate(sqlExpr(source)),hint:'Classify the result of the outer operator. Even an AND with numeric operands returns boolean or null.'})),
  custom: { describe: 'An AstQL expression string.', placeholder: '"TRUE AND u.id"', toArgs: v => [sqlExpr(String(v[0]))] },
};

const DOMAIN = [null, false, true, 0, 1, 'x'];
function witness(before: SqlExpr, after: SqlExpr): Row | null {
  const names = [...new Set([...exprRefs(before), ...exprRefs(after)])];
  if (names.length > 2) throw new Error('Use at most two distinct qualified columns');
  function search(i: number, row: Row): Row | null {
    if (i === names.length) return Object.is(evalRowExpr(before,row),evalRowExpr(after,row)) ? null : {...row};
    for (const v of DOMAIN) { const found = search(i+1,{...row,[names[i]]:v}); if (found !== null) return found; }
    return null;
  }
  return search(0,{});
}
export const counterexampleChallenge: ChallengeDef = {
  id: 'sql-counterexample', title: 'Extension: find a counterexample',
  summary: 'Search a finite domain for a row that disproves an expression rewrite.',
  signature: 'counterexample(before, after) → Row | null', entry: 'counterexample', difficulty: 4, immutableInputs: true,
  prelude: ROW_EVAL_HELPERS + `\nfunction columnNames(expr) {
  const names = new Set();
  function walk(e) {
    if (e.type === 'Column') names.add(e.table + '.' + e.name);
    if (e.type === 'Binary') { walk(e.left); walk(e.right); }
    if (e.operand) walk(e.operand);
  }
  walk(expr); return [...names];
}`,
  starter: `function counterexample(before, after) {
  // evalExpr(expr, row) and columnNames(expr) are provided.
  // Search all assignments from [null, false, true, 0, 1, 'x'] to the
  // union of columns (at most two). Return ANY differing row, else null.
  // TODO
}`,
  solution: `function counterexample(before, after) {
  const names = [...new Set([...columnNames(before), ...columnNames(after)])];
  const values = [null, false, true, 0, 1, 'x'];
  function search(i, row) {
    if (i === names.length) return Object.is(evalExpr(before,row),evalExpr(after,row)) ? null : row;
    for (const value of values) {
      const found = search(i+1, {...row, [names[i]]:value});
      if (found !== null) return found;
    }
    return null;
  }
  return search(0, {});
}`,
  check: (actual, ctx) => {
    const [before, after] = ctx.args as [SqlExpr,SqlExpr];
    const names = [...new Set([...exprRefs(before),...exprRefs(after)])];
    if (actual === null) return {pass:witness(before,after) === null,message:'A counterexample exists in the specified domain. Search every assignment, including null and non-boolean values.'};
    if (typeof actual !== 'object' || Array.isArray(actual)) return {pass:false,message:'Return a row object, or null if the entire finite domain agrees.'};
    const row = actual as Row;
    if (Object.keys(row).length !== names.length || names.some(n=>!Object.hasOwn(row,n) || !DOMAIN.includes(row[n]))) return {pass:false,message:'Include exactly the referenced qualified columns, with values from the six-value domain.'};
    return {pass:!Object.is(evalRowExpr(before,row),evalRowExpr(after,row)),message:'Both expressions have the same value on this row. It is not a counterexample.'};
  },
  tests: [
    ['TRUE AND u.x','u.x'], ['FALSE OR u.x','u.x'], ['u.x = u.x','TRUE'],
    ['0 * u.x','0'], ['u.x OR NOT u.x','TRUE'], ['u.x = v.y','v.y = u.x'],
    ['TRUE AND u.x > 0','u.x > 0'], ['u.x + v.y','u.x - v.y'], ['TRUE','FALSE'], ['NULL','NULL'],
  ].map(([before,after])=>({name:`${before}  ≟  ${after}`,args:[sqlExpr(before),sqlExpr(after)],hint:'A finite search can find a counterexample; agreement over this domain is not a proof for all possible values.'})),
  custom: { describe: 'Two AstQL expressions using at most two distinct qualified columns.', placeholder: '"TRUE AND u.x", "u.x"', toArgs: v => {
    const pair = [sqlExpr(String(v[0])),sqlExpr(String(v[1]))];
    if (pair.some(e=>[...exprRefs(e)].some(n=>n.startsWith('null.')))) throw new Error('Qualify columns, for example u.x');
    if (new Set(pair.flatMap(e=>[...exprRefs(e)])).size > 2) throw new Error('Use at most two distinct qualified columns');
    return pair;
  } },
};
