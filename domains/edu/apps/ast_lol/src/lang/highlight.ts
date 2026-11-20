import { javascriptLanguage, typescriptLanguage } from '@codemirror/lang-javascript';
import { classHighlighter, highlightTree } from '@lezer/highlight';

/** A run of source text, with the `tok-*` classes to style it by, if any. */
export interface Span {
  text: string;
  classes?: string;
}

/** Fence languages that get syntax highlighting; anything else is plain text. */
export const highlightedLanguages = ['js', 'javascript', 'ts', 'typescript'] as const;

/**
 * Splits source into styled spans, for both the markdown renderer and the
 * challenge view. Concatenating the spans' text gives back `code` exactly.
 */
export function highlightSpans(code: string, lang = 'js'): Span[] {
  const language = lang === 'ts' || lang === 'typescript' ? typescriptLanguage : javascriptLanguage;
  const spans: Span[] = [];
  let offset = 0;
  highlightTree(language.parser.parse(code), classHighlighter, (from, to, classes) => {
    if (from > offset) spans.push({ text: code.slice(offset, from) });
    spans.push({ text: code.slice(from, to), classes });
    offset = to;
  });
  if (offset < code.length) spans.push({ text: code.slice(offset) });
  return spans;
}
