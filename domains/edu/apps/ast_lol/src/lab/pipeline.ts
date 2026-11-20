import { evaluateExpr, foldExpr, parseExpr, printExpr, tokenizeExpr } from '../lang/expr';
import {
  buildPlan,
  demoDb,
  executeWithStats,
  optimizePlan,
  parseSelect,
  resolve,
  shopCatalog,
  tokenizeSql,
} from '../lang/sql';

export type LabLanguage = 'expr' | 'sql';
export type ExecutionStats = ReturnType<typeof executeWithStats>;

/**
 * The lab runs the reference pipeline on the page's main thread, so its input
 * is bounded instead. At these limits the largest result is three copies of
 * the demo database's biggest table cross-joined: a thousand rows.
 */
export const labLimits = { maxSourceLength: 3000, maxJoins: 2 };

/** The variables an Expr input is evaluated with. */
export const exprEnvironment = { x: 3, y: 2 };

/**
 * Every intermediate representation of one input, from tokens to results. A
 * stage that could not be reached is absent; `error` says why.
 */
export function inspectPipeline(language: LabLanguage, source: string) {
  try {
    if (source.length > labLimits.maxSourceLength) {
      throw new Error(
        `Keep lab examples under ${labLimits.maxSourceLength.toLocaleString()} characters.`,
      );
    }
    return language === 'expr' ? inspectExpr(source) : inspectSql(source);
  } catch (e) {
    return { error: e instanceof Error ? e.message : String(e) };
  }
}

function inspectExpr(source: string) {
  const tokens = tokenizeExpr(source);
  const ast = parseExpr(tokens);
  return {
    tokens,
    ast,
    folded: foldExpr(ast),
    value: evaluateExpr(ast, exprEnvironment),
    printed: printExpr(ast),
    error: null,
  };
}

function inspectSql(source: string) {
  const tokens = tokenizeSql(source);
  const ast = parseSelect(tokens);
  const resolved = resolve(ast, shopCatalog);
  if (!resolved.select) return { tokens, ast, resolved, error: null };
  if (resolved.select.joins.length > labLimits.maxJoins) {
    throw new Error(
      `The lab supports at most ${labLimits.maxJoins} joins; use challenges for larger queries.`,
    );
  }
  const plan = buildPlan(resolved.select, shopCatalog);
  const optimized = optimizePlan(plan);
  return {
    tokens,
    ast,
    resolved,
    plan,
    optimized,
    before: executeWithStats(plan, demoDb),
    after: executeWithStats(optimized, demoDb),
    error: null,
  };
}
