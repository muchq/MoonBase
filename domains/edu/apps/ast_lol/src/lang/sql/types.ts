/**
 * AstQL — the course's SQL subset. The shapes here are the public contract
 * of every Tier 3–5 challenge: challenge statements quote them and the
 * grader deep-equals against them, so a change here is a curriculum change,
 * not a refactor.
 */

export type SqlTokenKind = 'keyword' | 'ident' | 'number' | 'string' | 'op' | 'punct';

export interface SqlToken {
  kind: SqlTokenKind;
  /**
   * The lexeme. Keywords are canonicalized to uppercase and unquoted
   * identifiers fold to lowercase; for strings this is the raw lexeme
   * including quotes, with the decoded value in `value`.
   */
  text: string;
  /** Index of the first character of the lexeme in the source string. */
  pos: number;
  /** Decoded string contents; present only when kind is 'string'. */
  value?: string;
}

export const SQL_KEYWORDS = [
  'SELECT',
  'FROM',
  'WHERE',
  'AND',
  'OR',
  'NOT',
  'AS',
  'ORDER',
  'BY',
  'ASC',
  'DESC',
  'LIMIT',
  'JOIN',
  'ON',
  'IS',
  'NULL',
  'TRUE',
  'FALSE',
] as const;

export type SqlValue = number | string | boolean | null;

export type ComparisonOp = '=' | '<>' | '<' | '<=' | '>' | '>=';
export type ArithmeticOp = '+' | '-' | '*' | '/';
export type SqlBinaryOp = ComparisonOp | ArithmeticOp | 'AND' | 'OR';

export type SqlExpr =
  | { type: 'Column'; table: string | null; name: string }
  | { type: 'Lit'; value: SqlValue }
  | { type: 'Binary'; op: SqlBinaryOp; left: SqlExpr; right: SqlExpr }
  | { type: 'Not'; operand: SqlExpr }
  | { type: 'Unary'; op: '-'; operand: SqlExpr }
  | { type: 'IsNull'; operand: SqlExpr; negated: boolean };

export interface SelectColumn {
  expr: SqlExpr;
  alias: string | null;
}

export interface TableRef {
  table: string;
  alias: string | null;
}

export interface Join extends TableRef {
  on: SqlExpr;
}

export interface OrderKey {
  expr: SqlExpr;
  dir: 'ASC' | 'DESC';
}

export interface Select {
  type: 'Select';
  columns: '*' | SelectColumn[];
  from: TableRef;
  joins: Join[];
  where: SqlExpr | null;
  orderBy: OrderKey[];
  limit: number | null;
}

/** Table name → column names, in table order. */
export type Catalog = Record<string, string[]>;

export type ResolveErrorKind =
  | 'unknown-table'
  | 'duplicate-binding'
  | 'unknown-column'
  | 'ambiguous-column';

export interface ResolveError {
  kind: ResolveErrorKind;
  /** The offending name as written: a table name, binding, or (possibly qualified) column. */
  name: string;
}

export interface ResolveResult {
  /** Fully annotated Select (`*` expanded, every Column qualified), or null when there are errors. */
  select: Select | null;
  errors: ResolveError[];
}

export type Plan =
  | { type: 'Scan'; table: string; binding: string; columns: string[] }
  | { type: 'Join'; left: Plan; right: Plan; on: SqlExpr }
  | { type: 'Filter'; input: Plan; predicate: SqlExpr }
  | { type: 'Project'; input: Plan; columns: { expr: SqlExpr; name: string }[] }
  | { type: 'Sort'; input: Plan; keys: OrderKey[] }
  | { type: 'Limit'; input: Plan; count: number };

/** A row during execution: binding-qualified keys (`u.id`) below the final Project, output names after it. */
export type Row = Record<string, SqlValue>;

/** Table name → rows keyed by plain column names. */
export type Database = Record<string, Record<string, SqlValue>[]>;

export interface OperatorStats {
  label: string;
  /** Cells (row values) that entered this operator. */
  cells: number;
}

export interface ExecStats {
  rows: Row[];
  /** Total cells processed across all operators — the course's cost model. */
  cost: number;
  operators: OperatorStats[];
}
