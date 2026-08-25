import { useCallback, useEffect, useRef, useState } from 'react';
import { Link, Navigate, useParams } from 'react-router';
import { challengeHtml } from 'virtual:astlol-content';
import CodeEditor from '../components/CodeEditor';
import CustomTests from '../components/CustomTests';
import StepNav from '../components/StepNav';
import TestResults from '../components/TestResults';
import { challengeById, tierOfStep } from '../curriculum/registry';
import type { ChallengeDef } from '../curriculum/types';
import { runGrader, type RunningTest } from '../grader/client';
import type { GradeReport } from '../grader/types';
import {
  clearDraft,
  getProgress,
  markCompleted,
  saveDraft,
  setCustomTests,
  useProgress,
} from '../state/progress';

function Workspace({ challenge }: { challenge: ChallengeDef }) {
  const progress = useProgress();
  const [code, setCode] = useState(progress.drafts[challenge.id] ?? challenge.starter);
  const [report, setReport] = useState<GradeReport | null>(null);
  const [running, setRunning] = useState<RunningTest | null>(null);
  const [solutionShown, setSolutionShown] = useState(false);
  const customTests = progress.customTests[challenge.id] ?? [];
  const runningRef = useRef(false);

  // Drafts persist per challenge, debounced against typing speed. Editing
  // back to the exact starter clears the draft — otherwise the stale draft
  // would resurrect on the next visit.
  useEffect(() => {
    const t = setTimeout(() => {
      if (code === challenge.starter) {
        if (getProgress().drafts[challenge.id] !== undefined) clearDraft(challenge.id);
      } else {
        saveDraft(challenge.id, code);
      }
    }, 400);
    return () => clearTimeout(t);
  }, [challenge.id, challenge.starter, code]);

  const run = useCallback(() => {
    if (runningRef.current) return;
    runningRef.current = true;
    const planned = [
      ...challenge.tests.map((t, i) => ({ id: `builtin-${i}`, name: t.name, custom: false })),
      ...customTests.map((t) => ({ id: t.id, name: t.name, custom: true })),
    ];
    setRunning({ id: 'starting', name: 'compiling…' });
    setReport(null);
    void runGrader(
      challenge.id,
      { language: 'javascript', code },
      customTests,
      planned,
      {
        timeoutMs: challenge.timeoutMs,
        onProgress: (current) => setRunning(current),
      },
    ).then(
      (result) => {
        runningRef.current = false;
        setRunning(null);
        setReport(result);
        if (result.status === 'pass') markCompleted(challenge.id);
      },
      (e: unknown) => {
        runningRef.current = false;
        setRunning(null);
        setReport({
          challengeId: challenge.id,
          status: 'error',
          compileError: `The grader failed unexpectedly: ${e instanceof Error ? e.message : String(e)}`,
          tests: [],
        });
      },
    );
  }, [challenge, code, customTests]);

  const reset = () => {
    setCode(challenge.starter);
    clearDraft(challenge.id);
    setReport(null);
  };

  return (
    <div>
      {challenge.prelude !== undefined && (
        <details className="provided">
          <summary>Provided code (compiled ahead of yours — call it freely)</summary>
          <pre>{challenge.prelude}</pre>
        </details>
      )}
      <div className="panel">
        <div className="panel-header">
          <span>
            Define <code>{challenge.entry}</code> — JavaScript
          </span>
          <span className="spacer" />
          <span className="kbd">⌘/Ctrl-Enter runs</span>
          <button type="button" className="btn" onClick={reset}>
            Reset
          </button>
          <button type="button" className="btn primary" onClick={run} disabled={running !== null}>
            {running !== null ? 'Running…' : 'Run tests'}
          </button>
        </div>
        <CodeEditor value={code} onChange={setCode} onRun={run} />
      </div>
      <div className="panel-gap" style={{ height: '1rem' }} />
      <TestResults report={report} running={running} />
      <div style={{ height: '1rem' }} />
      <CustomTests
        challenge={challenge}
        tests={customTests}
        onChange={(tests) => setCustomTests(challenge.id, tests)}
      />
      <div style={{ height: '1rem' }} />
      <details
        className="panel solution"
        open={solutionShown}
        onToggle={(e) => setSolutionShown((e.target as HTMLDetailsElement).open)}
      >
        <summary>Reference solution — the grading oracle; best read after your own green run</summary>
        {solutionShown && (
          <>
            <pre>{challenge.solution}</pre>
            <div className="panel-header">
              <span className="spacer" />
              <button type="button" className="btn" onClick={() => setCode(challenge.solution)}>
                Load into editor
              </button>
            </div>
          </>
        )}
      </details>
    </div>
  );
}

export default function ChallengeView() {
  const { id = '' } = useParams();
  const challenge = challengeById(id);
  if (challenge === undefined) return <Navigate to="/" replace />;
  const tier = tierOfStep(challenge.id);

  return (
    <>
      <nav className="crumbs">
        <Link to="/">curriculum</Link>
        {tier !== undefined && ` / tier ${tier.number} — ${tier.title}`}
      </nav>
      <div className="challenge-layout">
        <div className="challenge-statement">
          <article className="prose">
            <h2>{challenge.title}</h2>
            <div className="signature">{challenge.signature}</div>
            {/* Course-authored markdown rendered at build time; no user content. */}
            <div dangerouslySetInnerHTML={{ __html: challengeHtml[challenge.id] ?? '' }} />
          </article>
        </div>
        {/* Remount the workspace per challenge so drafts/reports never bleed across. */}
        <Workspace key={challenge.id} challenge={challenge} />
      </div>
      <StepNav currentId={challenge.id} />
    </>
  );
}
