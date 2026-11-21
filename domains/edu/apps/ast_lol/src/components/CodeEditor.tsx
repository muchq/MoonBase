import { useEffect, useRef } from 'react';
import { EditorState } from '@codemirror/state';
import { EditorView, keymap, lineNumbers, highlightActiveLine } from '@codemirror/view';
import { defaultKeymap, history, historyKeymap, indentWithTab } from '@codemirror/commands';
import { bracketMatching, indentOnInput, syntaxHighlighting, HighlightStyle } from '@codemirror/language';
import { javascript } from '@codemirror/lang-javascript';
import { tags } from '@lezer/highlight';

const theme = EditorView.theme(
  {
    '&': { backgroundColor: 'transparent', color: 'var(--text)' },
    '.cm-content': { caretColor: 'var(--accent)', padding: '0.6rem 0' },
    '.cm-gutters': {
      backgroundColor: 'var(--surface)',
      color: 'var(--text-muted)',
      border: 'none',
    },
    '.cm-activeLine': { backgroundColor: 'rgba(126, 231, 135, 0.05)' },
    '.cm-selectionBackground, &.cm-focused .cm-selectionBackground': {
      backgroundColor: 'rgba(121, 184, 255, 0.18)',
    },
    '.cm-cursor': { borderLeftColor: 'var(--accent)' },
  },
  { dark: true },
);

const highlight = HighlightStyle.define([
  { tag: tags.keyword, color: 'var(--syntax-keyword)' },
  { tag: tags.comment, color: 'var(--text-muted)', fontStyle: 'italic' },
  { tag: [tags.string, tags.special(tags.string)], color: 'var(--syntax-string)' },
  { tag: tags.number, color: 'var(--syntax-number)' },
  {
    tag: [tags.function(tags.variableName), tags.function(tags.propertyName)],
    color: 'var(--syntax-function)',
  },
  { tag: tags.propertyName, color: 'var(--accent)' },
  { tag: [tags.operator, tags.punctuation], color: 'var(--text)' },
  { tag: tags.bool, color: 'var(--syntax-number)' },
]);

export interface CodeEditorProps {
  value: string;
  onChange: (code: string) => void;
  /** Bound to Mod-Enter inside the editor. */
  onRun: () => void;
}

/** CodeMirror wrapper. Uncontrolled internally; `value` changes from outside (reset, load solution) replace the document. */
export default function CodeEditor({ value, onChange, onRun }: CodeEditorProps) {
  const hostRef = useRef<HTMLDivElement | null>(null);
  const viewRef = useRef<EditorView | null>(null);
  const callbacks = useRef({ onChange, onRun });
  callbacks.current = { onChange, onRun };

  useEffect(() => {
    if (hostRef.current === null) return;
    const view = new EditorView({
      parent: hostRef.current,
      state: EditorState.create({
        doc: value,
        extensions: [
          EditorView.contentAttributes.of({ "aria-label": "Code editor" }),
          lineNumbers(),
          history(),
          bracketMatching(),
          indentOnInput(),
          highlightActiveLine(),
          javascript(),
          syntaxHighlighting(highlight),
          theme,
          keymap.of([
            {
              key: 'Mod-Enter',
              run: () => {
                callbacks.current.onRun();
                return true;
              },
            },
            indentWithTab,
            ...defaultKeymap,
            ...historyKeymap,
          ]),
          EditorView.updateListener.of((update) => {
            if (update.docChanged) callbacks.current.onChange(update.state.doc.toString());
          }),
          EditorView.lineWrapping,
        ],
      }),
    });
    viewRef.current = view;
    return () => {
      view.destroy();
      viewRef.current = null;
    };
    // The editor owns its document after mount; value-prop changes are
    // handled by the effect below.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  useEffect(() => {
    const view = viewRef.current;
    if (view === null) return;
    const current = view.state.doc.toString();
    if (current !== value) {
      view.dispatch({ changes: { from: 0, to: current.length, insert: value } });
    }
  }, [value]);

  return <div className="editor-host" ref={hostRef} />;
}
