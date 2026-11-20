import { describe, expect, it } from 'vitest';
import { highlightSpans } from '../lang/highlight';
import { renderDoc } from '../../vite-plugin-content';

const classesOf = (code: string, lang: string, text: string) =>
  highlightSpans(code, lang).find((span) => span.text === text)?.classes;

describe('highlightSpans', () => {
  it('splits code into spans that reassemble to the source exactly', () => {
    const code = 'function f(x) {\n  return x + 1; // done\n}\n';
    expect(highlightSpans(code).map((span) => span.text).join('')).toBe(code);
  });

  it('styles JavaScript keywords', () => {
    expect(classesOf('const x = 1;', 'js', 'const')).toContain('tok-keyword');
  });

  // The JavaScript parser recovers from a simple annotation like `: number`,
  // but not from TypeScript-only declarations.
  it('parses TypeScript as TypeScript, so its declarations are keywords', () => {
    expect(classesOf('interface P { x: string }', 'ts', 'interface')).toBe('tok-keyword');
  });
});

describe('renderDoc code fences', () => {
  it('highlights a JavaScript fence with the shared spans, escaping its text', () => {
    const html = renderDoc('```js\nif (a < b) return;\n```');
    expect(html).toContain('<span class="tok-keyword">if</span>');
    expect(html).toContain('&lt;');
    expect(html).not.toContain('a < b');
  });

  it('highlights a TypeScript fence as TypeScript', () => {
    expect(renderDoc('```ts\ninterface P { x: string }\n```')).toContain(
      '<span class="tok-keyword">interface</span>',
    );
  });

  it('leaves other fences as escaped plain text', () => {
    expect(renderDoc('```sql\nSELECT a FROM t WHERE a < 1\n```')).toBe(
      '<pre><code>SELECT a FROM t WHERE a &lt; 1</code></pre>',
    );
  });
});
