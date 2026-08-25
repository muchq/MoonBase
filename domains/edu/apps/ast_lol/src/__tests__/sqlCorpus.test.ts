import { readFileSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { describe, expect, it } from 'vitest';
import { parseSelect, tokenizeSql } from '../lang/sql';
import corpus from './corpus/sql-corpus.json';

/**
 * Frozen-corpus test: every query in sql-corpus.json parses to exactly the
 * AST pinned in sql-corpus.expected.json, so a parser change that reshapes
 * output fails here rather than silently re-speccing every Tier 3+
 * challenge. After an *intended* grammar change, regenerate with:
 *
 *   UPDATE_SQL_CORPUS=1 npm test
 *
 * and review the expected-file diff like any other spec change.
 */
// jsdom rewrites import.meta.url to an http URL, so locate the file from
// the package root (vitest always runs from it).
const EXPECTED_PATH = join(process.cwd(), 'src/__tests__/corpus/sql-corpus.expected.json');

describe('sql parser corpus', () => {
  const actual = corpus.map((query) => parseSelect(tokenizeSql(query)));

  if (process.env.UPDATE_SQL_CORPUS === '1') {
    it('regenerates the expected file', () => {
      writeFileSync(EXPECTED_PATH, `${JSON.stringify(actual, null, 2)}\n`);
      expect(actual.length).toBe(corpus.length);
    });
    return;
  }

  const expected = JSON.parse(readFileSync(EXPECTED_PATH, 'utf-8')) as unknown[];

  it('covers every corpus query', () => {
    expect(expected.length).toBe(corpus.length);
  });

  corpus.forEach((query, i) => {
    it(`parses: ${query.replaceAll('\n', ' ')}`, () => {
      expect(actual[i]).toEqual(expected[i]);
    });
  });
});
