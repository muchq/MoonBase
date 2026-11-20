import { useMemo, useRef, useState } from 'react';
import { Link } from 'react-router';
import TreeValue from '../components/TreeValue';
import { showPretty } from '../grader/deepEqual';
import { tokenizeExpr, parseExpr, evaluateExpr, foldExpr, printExpr } from '../lang/expr';
import { tokenizeSql, parseSelect, resolve, buildPlan, optimizePlan, executeWithStats, formatSelect, demoDb, shopCatalog } from '../lang/sql';

const examples = [
  {label:'Precedence',language:'expr',source:'x + 2 * 3'},
  {label:'Null semantics',language:'sql',source:"SELECT name, city FROM users WHERE city IS NULL"},
  {label:'Filter pushdown',language:'sql',source:"SELECT u.name, o.total FROM users u JOIN orders o ON u.id = o.user_id WHERE o.year = 2024 ORDER BY o.total DESC"},
] as const;
export default function LabView() {
  const [language,setLanguage] = useState<'expr'|'sql'>('expr');
  const [source,setSource] = useState<string>(examples[0].source);
  const [submitted,setSubmitted] = useState({language,source});
  const [stage,setStage] = useState('ast');
  const [width,setWidth] = useState(60);
  const editor = useRef<HTMLTextAreaElement>(null);
  const result = useMemo(() => {
    try {
      if (submitted.source.length > 3000) throw new Error('Keep lab examples under 3,000 characters.');
      if (submitted.language === 'expr') {
        const tokens = tokenizeExpr(submitted.source), ast = parseExpr(tokens), folded = foldExpr(ast);
        return {tokens,ast,folded,value:evaluateExpr(ast,{x:3,y:2}),printed:printExpr(ast),error:null};
      }
      const tokens = tokenizeSql(submitted.source), ast = parseSelect(tokens);
      const resolved = resolve(ast,shopCatalog);
      if (!resolved.select) return {tokens,ast,resolved,error:null};
      const plan = buildPlan(resolved.select,shopCatalog);
      // The lab runs on the small demo database. Bound joins so a teaching
      // example cannot accidentally allocate an enormous cross product.
      if (resolved.select.joins.length > 2) throw new Error('The lab supports at most two joins; use challenges for larger queries.');
      const optimized = optimizePlan(plan);
      return {tokens,ast,resolved,plan,optimized,before:executeWithStats(plan,demoDb),after:executeWithStats(optimized,demoDb),error:null};
    } catch (e) { return {error:e instanceof Error ? e.message : String(e)}; }
  },[submitted]);
  const sql = submitted.language === 'sql';
  const stages = sql ? ['tokens','ast','resolved','plan','optimized','rows','cost','format'] : ['tokens','ast','folded','value'];
  const active = stages.includes(stage) ? stage : 'ast';
  const data = active === 'rows' ? result.after?.rows : active === 'value' ? {environment:{x:3,y:2},value:result.value,printed:result.printed} : result[active as 'ast'|'plan'|'optimized'|'resolved'|'folded'];
  return <>
    <nav className="crumbs"><Link to="/">Curriculum</Link> / Pipeline lab</nav>
    <section className="hero"><span className="eyebrow">Observe before you implement</span><h2>One input, every representation</h2><p>Predict a result, then inspect the tokens, trees, and intermediate plans. This lab uses the reference implementations; it does not grade your code.</p></section>
    <div className="lab-examples">{examples.map(example=><button className="btn" key={example.label} onClick={()=>{setLanguage(example.language);setSource(example.source);setSubmitted({language:example.language,source:example.source});setStage('ast');}}>{example.label}</button>)}</div>
    <form className="lab-input" onSubmit={e=>{e.preventDefault();setSubmitted({language,source});}}>
      <label>Language <select aria-label="Lab language" value={language} onChange={e=>setLanguage(e.target.value as 'expr'|'sql')}><option value="expr">Expr</option><option value="sql">AstQL</option></select></label>
      <label htmlFor="lab-source">Source</label><textarea id="lab-source" className="code" ref={editor} value={source} onChange={e=>setSource(e.target.value)} rows={4} spellCheck={false} />
      <button className="btn primary" type="submit">Inspect pipeline</button>
      {(submitted.source !== source || submitted.language !== language) && <span className="note">Showing the last inspected input.</span>}
    </form>
    {result.error ? <p role="alert" className="compile-error">{result.error}</p> : <>
      <div className="pipeline-stages" role="group" aria-label="Pipeline stage">{stages.map(name=><button className={`btn ${active===name?'primary':''}`} key={name} aria-pressed={active===name} onClick={()=>setStage(name)}>{name}</button>)}</div>
      <section className="lab-result" aria-label={`${active} representation`}>
        {active === 'tokens' ? <><p>Select a token to locate its characters in the inspected source.</p><div className="token-list">{result.tokens?.map((token,i)=><button className="token" key={i} onClick={()=>{setSource(submitted.source);setLanguage(submitted.language);requestAnimationFrame(()=>{editor.current?.focus();editor.current?.setSelectionRange(token.pos,token.pos+token.text.length);});}}><code>{token.text}</code><span>{token.kind} · {token.pos}</span></button>)}</div></> :
        active === 'format' ? <><label htmlFor="format-width">Line width: {width}</label><input id="format-width" type="range" min="20" max="140" value={width} onChange={e=>setWidth(Number(e.target.value))}/><pre>{result.ast && result.ast.type === 'Select' ? formatSelect(result.ast,width) : ''}</pre><p>The tree stays the same as the width changes.</p></> :
        active === 'cost' ? result.before && result.after ? <><p>Demo database · naive: <strong>{result.before.cost.toLocaleString()}</strong> cells · optimized: <strong>{result.after.cost.toLocaleString()}</strong> cells</p><div className="cost-grid">{[['Before',result.before],['After',result.after]].map(([label,stats])=><div key={String(label)}><h3>{String(label)}</h3><table><thead><tr><th>Operator</th><th>Cells</th></tr></thead><tbody>{typeof stats !== 'string' && stats.operators.map((op,i)=><tr key={i}><td>{op.label}</td><td>{op.cells.toLocaleString()}</td></tr>)}</tbody></table></div>)}</div><p>This teaching meter counts cells, not elapsed time. Compare plans and explain where work disappeared.</p></> : <p>Resolve the query before inspecting execution cost.</p> :
        data === undefined ? <p>Resolve the query before inspecting this stage. Check the resolved tab for errors.</p> : <><TreeValue value={data} /><details><summary>Text representation</summary><pre>{showPretty(data)}</pre></details></>}
      </section>
    </>}
    <aside className="reading"><h3>Try a counterexample</h3><p>Inspect <code>SELECT id FROM users WHERE (TRUE AND id) = id</code>. Does a boolean identity apply to a numeric column? Compare the original and optimized plans.</p><Link to="/challenge/sql-counterexample">Build your own counterexample search</Link></aside>
  </>;
}
