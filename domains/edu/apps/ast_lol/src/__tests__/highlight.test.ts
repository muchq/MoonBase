import { describe, expect, it } from 'vitest';
import { highlightSpans } from '../lang/highlight';
import { renderDoc } from '../../vite-plugin-content';

const span = (classes: string, text: string) => `<span class="${classes}">${text}</span>`;

describe('highlightSpans', () => {
  it('splits code into styled tokens and the plain text between them', () => {
    expect(highlightSpans('const x = 1;')).toEqual([
      { text: 'const', classes: 'tok-keyword' },
      { text: ' ' },
      { text: 'x', classes: 'tok-variableName tok-definition' },
      { text: ' ' },
      { text: '=', classes: 'tok-operator' },
      { text: ' ' },
      { text: '1', classes: 'tok-number' },
      { text: ';', classes: 'tok-punctuation' },
    ]);
  });

  it('reassembles to the source exactly, comments and newlines included', () => {
    const code = 'function f(x) {\n  return x + 1; // done\n}\n';
    const text = highlightSpans(code).map((s) => s.text);
    expect(text.join('')).toBe(code);
  });

  // The JavaScript parser recovers from a simple annotation like `: number`,
  // but reads TypeScript-only declarations as errors.
  it('reads `interface` as a keyword in TypeScript, and as a name in JavaScript', () => {
    const code = 'interface P { x: string }';
    expect(highlightSpans(code, 'ts')[0]).toEqual({ text: 'interface', classes: 'tok-keyword' });
    expect(highlightSpans(code, 'js')[0]).toEqual({
      text: 'interface ',
      classes: 'tok-variableName',
    });
  });
});

describe('renderDoc code fences', () => {
  it('renders a JavaScript fence as highlighted, escaped HTML', () => {
    expect(renderDoc('```js\nif (a < b) return;\n```')).toBe(
      '<pre><code>' +
        `${span('tok-keyword', 'if')} ${span('tok-punctuation', '(')}` +
        `${span('tok-variableName', 'a')} ${span('tok-operator', '&lt;')} ` +
        `${span('tok-variableName', 'b')}${span('tok-punctuation', ')')} ` +
        `${span('tok-keyword', 'return')}${span('tok-punctuation', ';')}` +
        '</code></pre>',
    );
  });

  it('renders a TypeScript fence with the TypeScript parser', () => {
    expect(renderDoc('```ts\ninterface P { x: string }\n```')).toBe(
      '<pre><code>' +
        `${span('tok-keyword', 'interface')} ${span('tok-typeName', 'P')} ` +
        `${span('tok-punctuation', '{')} ${span('tok-propertyName tok-definition', 'x')}` +
        `${span('tok-punctuation', ':')} ${span('tok-typeName', 'string')} ` +
        `${span('tok-punctuation', '}')}` +
        '</code></pre>',
    );
  });

  it('leaves any other fence as escaped plain text', () => {
    expect(renderDoc('```sql\nSELECT a FROM t WHERE a < 1\n```')).toBe(
      '<pre><code>SELECT a FROM t WHERE a &lt; 1</code></pre>',
    );
  });
});
