import { useMemo } from 'react';
import { highlightSpans } from '../lang/highlight';

/** Read-only JavaScript, styled with the same spans as the course's markdown. */
export default function HighlightedCode({ code }: { code: string }) {
  const spans = useMemo(() => highlightSpans(code), [code]);
  return (
    <pre>
      <code>
        {spans.map((span, i) => (
          <span key={i} className={span.classes}>
            {span.text}
          </span>
        ))}
      </code>
    </pre>
  );
}
