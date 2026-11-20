import TreeValue from './TreeValue';
import type { GradeReport, TestResult } from '../grader/types';
import type { RunningTest } from '../grader/client';

function ValuePane({ text }: { text: string }) {
  let parsed: unknown;
  try { parsed = JSON.parse(text); } catch { /* Diagnostics need not be JSON. */ }
  return <><pre>{text}</pre>{typeof parsed === 'object' && parsed !== null && <details><summary>Inspect structure</summary><TreeValue value={parsed} /></details>}</>;
}

function ResultRow({ result, expanded }: { result: TestResult; expanded: boolean }) {
  const failed = result.status !== 'pass';
  const hasPanes = result.expectedText !== undefined || result.actualText !== undefined;
  return (
    <li className="test-row">
      <details open={expanded}>
        <summary>
          <span className={`badge ${result.status}`}>
            {result.status === 'pass' ? '✓ pass' : result.status === 'fail' ? '✗ fail' : result.status}
          </span>
          {result.custom && <span className="custom-mark">yours</span>}
          <span>{result.name}</span>
        </summary>
        {(failed || result.logs.length > 0 || result.inputText !== undefined) && (
          <div className="test-detail">
            {result.message !== undefined && <div className="message">{result.message}</div>}
            {result.hint !== undefined && <details className="hint"><summary>Show a debugging hint</summary><p>{result.hint}</p></details>}
            {result.inputText !== undefined && <details><summary>Test input</summary><ValuePane text={result.inputText} /></details>}
            {hasPanes && (
              <div className={`diff-panes ${result.expectedText && result.actualText ? 'two' : ''}`}>
                {result.expectedText !== undefined && (
                  <div>
                    <div className="pane-label">expected</div>
                    <ValuePane text={result.expectedText} />
                  </div>
                )}
                {result.actualText !== undefined && (
                  <div>
                    <div className="pane-label">yours</div>
                    <ValuePane text={result.actualText} />
                  </div>
                )}
              </div>
            )}
            {result.logs.length > 0 && <div className="logs">{result.logs.join('\n')}</div>}
          </div>
        )}
      </details>
    </li>
  );
}

export interface TestResultsProps {
  report: GradeReport | null;
  running: RunningTest | null;
}

export default function TestResults({ report, running }: TestResultsProps) {
  if (running !== null) {
    return (
      <div className="panel" aria-live="polite">
        <div className="panel-header">
          <span>
            running… <code>{running.name}</code>
          </span>
        </div>
      </div>
    );
  }
  if (report === null) {
    return (
      <div className="panel">
        <div className="panel-header">
          <span>
            Run your code to see results — failures show where expected and actual first
            diverge, plus your console output, and the classic mistakes carry hints.
          </span>
        </div>
      </div>
    );
  }
  const firstFailure = report.tests.find(t => t.status !== 'pass' && t.status !== 'skipped')?.id;
  const passed = report.tests.filter((t) => t.status === 'pass').length;
  const summaryClass = report.status === 'pass' ? 'pass' : report.status === 'timeout' ? 'timeout' : 'fail';
  return (
    <div className="panel" aria-live="polite">
      <div className="panel-header">
        <span className={`summary-line ${summaryClass}`}>
          {report.status === 'pass'
            ? `Solved — ${passed}/${report.tests.length} tests pass`
            : report.status === 'timeout'
              ? `Time budget exceeded — ${passed}/${report.tests.length} passed before the stop`
              : report.compileError !== undefined
                ? 'Your code did not compile'
                : `${passed}/${report.tests.length} tests pass`}
        </span>
      </div>
      {report.compileError !== undefined ? (
        <div className="compile-error">{report.compileError}</div>
      ) : (
        <ul className="test-list">
          {report.tests.map((t) => (
            <ResultRow key={t.id} result={t} expanded={t.id === firstFailure} />
          ))}
        </ul>
      )}
    </div>
  );
}
