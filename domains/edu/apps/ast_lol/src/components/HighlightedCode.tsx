import { useMemo } from 'react';
import { javascriptLanguage } from '@codemirror/lang-javascript';
import { classHighlighter, highlightTree } from '@lezer/highlight';

export default function HighlightedCode({ code }: { code: string }) {
  const spans = useMemo(() => {
    const out: {text:string;classes?:string}[] = [];
    let offset=0;
    highlightTree(javascriptLanguage.parser.parse(code),classHighlighter,(from,to,classes)=>{
      if(from>offset) out.push({text:code.slice(offset,from)});
      out.push({text:code.slice(from,to),classes}); offset=to;
    });
    out.push({text:code.slice(offset)});
    return out;
  },[code]);
  return <pre><code>{spans.map((span,i)=><span key={i} className={span.classes}>{span.text}</span>)}</code></pre>;
}
