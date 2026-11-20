import { describe, expect, it } from 'vitest';
import { challenges, lessons, stepId } from '../curriculum/registry';
import { learningPath, pathForStep } from '../curriculum/path';
import { lessonPractice } from '../curriculum/learning';
import { gradeSubmission } from '../grader/harness';
import { seededDatabase } from '../curriculum/validation';
import { parseExpr, tokenizeExpr, printExpr, foldExpr, evaluateExpr } from '../lang/expr';
import { parseSelect, parseSqlExpr, tokenizeSql, resolve, buildPlan, optimizePlan, executePlan, evalRowExpr, simplifyExpr, shopCatalog, demoDb, formatSelect } from '../lang/sql';

const challenge = (id:string) => challenges.find(c=>c.id===id)!;
const sqlExpr = (s:string) => parseSqlExpr(tokenizeSql(s));
const expr = (s:string) => parseExpr(tokenizeExpr(s));
const plan = (s:string) => buildPlan(resolve(parseSelect(tokenizeSql(s)),shopCatalog).select!,shopCatalog);

describe('learning paths',()=>{
  it('forks after shared syntax, with no engine prerequisite for formatting',()=>{
    const formatter=learningPath('formatter').map(stepId);
    expect(formatter[formatter.indexOf('sql-select-parse')+1]).toBe('layout');
    expect(formatter).not.toContain('sql-execute');
    expect(learningPath('engine').map(stepId)).not.toContain('sql-format');
    expect(pathForStep('sql-format','engine').map(stepId)).toEqual(formatter);
  });
  it('gives every lesson two valid checks',()=>{
    expect(Object.keys(lessonPractice).sort()).toEqual(lessons.map(l=>l.id).sort());
    for(const items of Object.values(lessonPractice)) {
      expect(items.length).toBeGreaterThanOrEqual(2);
      for(const item of items) { expect(item.options[item.answer]).toBeDefined(); expect(item.explanation.length).toBeGreaterThan(30); }
    }
  });
});

describe('rewrite semantics',()=>{
  it.each(['(TRUE AND id) = id','(id AND TRUE) = id','(FALSE OR id) = id','(id OR FALSE) = id','(TRUE AND 5) = FALSE'])(
    'preserves coercion in %s', predicate=>{
      const before=plan(`SELECT id FROM users WHERE ${predicate}`);
      for(const db of [demoDb,seededDatabase(7),seededDatabase(42)]) expect(executePlan(optimizePlan(before),db)).toEqual(executePlan(before,db));
      const c=challenge('sql-optimize');
      expect(gradeSubmission(c,{language:'javascript',code:c.solution},[{id:'regression',name:'coercion',source:JSON.stringify(`SELECT id FROM users WHERE ${predicate}`)}]).status).toBe('pass');
    });
  it('checks identity rewrites in nested contexts across mixed scalar domains',()=>{
    const values=[null,false,true,0,1,-1,'x'];
    const children=['u.x','u.x > 0','u.x + 1','NOT u.x','u.x IS NULL'];
    for(const child of children) for(const identity of [`TRUE AND (${child})`,`FALSE OR (${child})`,`(${child}) AND TRUE`,`(${child}) OR FALSE`]) {
      for(const source of [identity,`(${identity}) = u.y`,`NOT (${identity})`,`(${identity}) IS NULL`]) {
        const before=sqlExpr(source),after=simplifyExpr(before);
        for(const x of values) for(const y of values) {
          const row={'u.x':x,'u.y':y};
          expect(evalRowExpr(after,row),`${source} at ${JSON.stringify(row)}`).toBe(evalRowExpr(before,row));
        }
      }
    }
  });
});

describe('contracts and misconception tests',()=>{
  it.each(challenges.filter(c=>c.immutableInputs).map(c=>[c.id,c] as const))('%s rejects mutation even with correct output',(_id,c)=>{
    const code=`const reference = (() => { ${c.solution}; return ${c.entry}; })();
      function ${c.entry}(...args) { const result=reference(...structuredClone(args)); args[0].unexpectedMutation=true; return result; }`;
    const report=gradeSubmission(c,{language:'javascript',code});
    expect(report.status).toBe('fail');
    expect(report.tests.some(t=>t.message?.includes('Input changed'))).toBe(true);
  });
  it('rejects JavaScript truthiness in SQL evaluation',()=>{
    const c=challenge('sql-row-expr');
    const code=c.solution.replace('return v === true ? true : v === null ? null : false;', 'return v === null ? null : !!v;');
    expect(code).not.toBe(c.solution);
    expect(gradeSubmission(c,{language:'javascript',code}).status).toBe('fail');
  });
  it('rejects a type analysis that guesses column types',()=>{
    const c=challenge('sql-predicate-type');
    const code=c.solution.replace("if (expr.type === 'Lit')", "if (expr.type === 'Column') return true; if (expr.type === 'Lit')");
    expect(gradeSubmission(c,{language:'javascript',code}).status).toBe('fail');
  });
  it('rejects a folder that folds infinity',()=>{
    const c=challenge('expr-fold');
    const code=c.solution.replace('Number.isFinite(value)','true');
    expect(gradeSubmission(c,{language:'javascript',code}).status).toBe('fail');
  });
  it('accepts alternate counterexamples but rejects a non-witness',()=>{
    const c=challenge('sql-counterexample');
    const args=[sqlExpr('TRUE AND u.x'),sqlExpr('u.x')];
    expect(c.check!({'u.x':'x'},{args}).pass).toBe(true);
    expect(c.check!({'u.x':true},{args}).pass).toBe(false);
    expect(c.check!(null,{args}).pass).toBe(false);
  });
});

describe('generated invariants',()=>{
  it('round-trips parser-produced trees and preserves folding values',()=>{
    let seed=17;
    const random=()=>{seed=(Math.imul(seed,1664525)+1013904223)>>>0;return seed;};
    const generate=(depth:number):string=>depth===0 ? ['x','y','0','1','2'][random()%5] : `(${generate(depth-1)} ${['+','-','*','/'][random()%4]} ${generate(depth-1)})`;
    for(let i=0;i<80;i++) {
      const tree=expr(generate(3));
      expect(expr(printExpr(tree))).toEqual(tree);
      for(const x of [0,1,-3,Infinity,NaN]) {
        const env={x,y:2};
        expect(evaluateExpr(foldExpr(tree),env)).toBe(evaluateExpr(tree,env));
      }
    }
  });
  it('formats idempotently at boundary widths',()=>{
    for(const source of ['SELECT name FROM users',"SELECT u.name, o.total FROM users u JOIN orders o ON u.id = o.user_id WHERE o.total > 2 AND u.city IS NOT NULL ORDER BY o.total DESC"]) {
      const ast=parseSelect(tokenizeSql(source));
      const flat=formatSelect(ast,1000);
      for(const width of [20,40,60,flat.length-1,flat.length,flat.length+1]) {
        const rendered=formatSelect(ast,width);
        const reparsed=parseSelect(tokenizeSql(rendered));
        expect(reparsed).toEqual(ast);
        expect(formatSelect(reparsed,width)).toBe(rendered);
      }
    }
  });
});
