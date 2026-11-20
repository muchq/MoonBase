import { useMemo, useRef, useState } from 'react';
import { Link } from 'react-router';
import TreeValue from '../components/TreeValue';
import { showPretty } from '../grader/deepEqual';
import {
  exprEnvironment,
  inspectPipeline,
  type ExecutionStats,
  type LabLanguage,
} from '../lab/pipeline';
import { formatSelect } from '../lang/sql';

const examples: { label: string; language: LabLanguage; source: string }[] = [
  { label: 'Precedence', language: 'expr', source: 'x + 2 * 3' },
  {
    label: 'Null semantics',
    language: 'sql',
    source: 'SELECT name, city FROM users WHERE city IS NULL',
  },
  {
    label: 'Filter pushdown',
    language: 'sql',
    source:
      'SELECT u.name, o.total FROM users u JOIN orders o ON u.id = o.user_id ' +
      'WHERE o.year = 2024 ORDER BY o.total DESC',
  },
];

const exprStages = ['tokens', 'ast', 'folded', 'value'];
const sqlStages = ['tokens', 'ast', 'resolved', 'plan', 'optimized', 'rows', 'cost', 'format'];

export default function LabView() {
  const [language, setLanguage] = useState<LabLanguage>('expr');
  const [source, setSource] = useState(examples[0].source);
  const [submitted, setSubmitted] = useState({ language, source });
  const [stage, setStage] = useState('ast');
  const [width, setWidth] = useState(60);
  const editor = useRef<HTMLTextAreaElement>(null);
  const lab = useMemo(
    () => inspectPipeline(submitted.language, submitted.source),
    [submitted],
  );

  const stages = submitted.language === 'sql' ? sqlStages : exprStages;
  const active = stages.includes(stage) ? stage : 'ast';
  const edited = submitted.source !== source || submitted.language !== language;

  const inspect = (next: { language: LabLanguage; source: string }) => {
    setLanguage(next.language);
    setSource(next.source);
    setSubmitted(next);
  };

  // Restores the inspected input to the editor and selects the token's text.
  const locate = (pos: number, length: number) => {
    setSource(submitted.source);
    setLanguage(submitted.language);
    requestAnimationFrame(() => {
      editor.current?.focus();
      editor.current?.setSelectionRange(pos, pos + length);
    });
  };

  return (
    <>
      <nav className="crumbs">
        <Link to="/">Curriculum</Link> / Pipeline lab
      </nav>
      <section className="hero">
        <span className="eyebrow">Observe before you implement</span>
        <h2>One input, every representation</h2>
        <p>
          Predict a result, then inspect the tokens, trees, and intermediate plans. This lab uses
          the reference implementations; it does not grade your code.
        </p>
      </section>

      <div className="lab-examples">
        {examples.map((example) => (
          <button
            className="btn"
            key={example.label}
            onClick={() => {
              inspect(example);
              setStage('ast');
            }}
          >
            {example.label}
          </button>
        ))}
      </div>

      <form
        className="lab-input"
        onSubmit={(e) => {
          e.preventDefault();
          setSubmitted({ language, source });
        }}
      >
        <label>
          Language{' '}
          <select
            aria-label="Lab language"
            value={language}
            onChange={(e) => setLanguage(e.target.value as LabLanguage)}
          >
            <option value="expr">Expr</option>
            <option value="sql">AstQL</option>
          </select>
        </label>
        <label htmlFor="lab-source">Source</label>
        <textarea
          id="lab-source"
          className="code"
          ref={editor}
          value={source}
          onChange={(e) => setSource(e.target.value)}
          rows={4}
          spellCheck={false}
        />
        <button className="btn primary" type="submit">
          Inspect pipeline
        </button>
        {edited && <span className="note">Showing the last inspected input.</span>}
      </form>

      {lab.error ? (
        <p role="alert" className="compile-error">
          {lab.error}
        </p>
      ) : (
        <>
          <div className="pipeline-stages" role="group" aria-label="Pipeline stage">
            {stages.map((name) => (
              <button
                className={`btn ${active === name ? 'primary' : ''}`}
                key={name}
                aria-pressed={active === name}
                onClick={() => setStage(name)}
              >
                {name}
              </button>
            ))}
          </div>
          <section className="lab-result" aria-label={`${active} representation`}>
            <Stage name={active} lab={lab} width={width} setWidth={setWidth} locate={locate} />
          </section>
        </>
      )}

      <aside className="reading">
        <h3>Try a counterexample</h3>
        <p>
          Inspect <code>SELECT id FROM users WHERE (TRUE AND id) = id</code>. Does a boolean
          identity apply to a numeric column? Compare the original and optimized plans.
        </p>
        <Link to="/challenge/sql-counterexample">Build your own counterexample search</Link>
      </aside>
    </>
  );
}

type Lab = ReturnType<typeof inspectPipeline>;

function Stage({
  name,
  lab,
  width,
  setWidth,
  locate,
}: {
  name: string;
  lab: Lab;
  width: number;
  setWidth: (width: number) => void;
  locate: (pos: number, length: number) => void;
}) {
  if (name === 'tokens' && 'tokens' in lab) {
    return (
      <>
        <p>Select a token to locate its characters in the inspected source.</p>
        <div className="token-list">
          {lab.tokens.map((token, i) => (
            <button className="token" key={i} onClick={() => locate(token.pos, token.text.length)}>
              <code>{token.text}</code>
              <span>
                {token.kind} · {token.pos}
              </span>
            </button>
          ))}
        </div>
      </>
    );
  }

  if (name === 'format') {
    const ast = 'ast' in lab ? lab.ast : undefined;
    return (
      <>
        <label htmlFor="format-width">Line width: {width}</label>
        <input
          id="format-width"
          type="range"
          min="20"
          max="140"
          value={width}
          onChange={(e) => setWidth(Number(e.target.value))}
        />
        <pre>{ast && ast.type === 'Select' ? formatSelect(ast, width) : ''}</pre>
        <p>The tree stays the same as the width changes.</p>
      </>
    );
  }

  if (name === 'cost') {
    if (!('before' in lab) || !lab.before || !lab.after) {
      return <p>Resolve the query before inspecting execution cost.</p>;
    }
    return <Cost before={lab.before} after={lab.after} />;
  }

  const data = stageData(name, lab);
  if (data === undefined) {
    return <p>Resolve the query before inspecting this stage. Check the resolved tab for errors.</p>;
  }
  return (
    <>
      <TreeValue value={data} />
      <details>
        <summary>Text representation</summary>
        <pre>{showPretty(data)}</pre>
      </details>
    </>
  );
}

function stageData(name: string, lab: Lab): unknown {
  if (name === 'rows') return 'after' in lab ? lab.after?.rows : undefined;
  if (name === 'value' && 'value' in lab) {
    return { environment: exprEnvironment, value: lab.value, printed: lab.printed };
  }
  return (lab as Record<string, unknown>)[name];
}

function Cost({ before, after }: { before: ExecutionStats; after: ExecutionStats }) {
  return (
    <>
      <p>
        Demo database · naive: <strong>{before.cost.toLocaleString()}</strong> cells · optimized:{' '}
        <strong>{after.cost.toLocaleString()}</strong> cells
      </p>
      <div className="cost-grid">
        {(
          [
            ['Before', before],
            ['After', after],
          ] as const
        ).map(([label, stats]) => (
          <div key={label}>
            <h3>{label}</h3>
            <table>
              <thead>
                <tr>
                  <th>Operator</th>
                  <th>Cells</th>
                </tr>
              </thead>
              <tbody>
                {stats.operators.map((op, i) => (
                  <tr key={i}>
                    <td>{op.label}</td>
                    <td>{op.cells.toLocaleString()}</td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        ))}
      </div>
      <p>This teaching meter counts cells, not elapsed time. Compare plans and explain where work disappeared.</p>
    </>
  );
}
