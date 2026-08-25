import type { GradeReport, TestResult } from '../grader/types';
import type { RunningTest } from '../grader/client';

function ResultRow({ result }: { result: TestResult }) {
  const failed = result.status !== 'pass';
  const hasPanes = result.expectedText !== undefined || result.actualText !== undefined;
  return (
    <li className="test-row">
      <details open={failed && result.status !== 'skipped'}>
        <summary>
          <span className={`badge ${result.status}`}>
            {result.status === 'pass' ? '✓ pass' : result.status === 'fail' ? '✗ fail' : result.status}
          </span>
          {result.custom && <span className="custom-mark">yours</span>}
          <span>{result.name}</span>
        </summary>
        {(failed || result.logs.length > 0) && (
          <div className="test-detail">
            {result.message !== undefined && <div className="message">{result.message}</div>}
            {result.hint !== undefined && <div className="hint">{result.hint}</div>}
            {hasPanes && (
              <div className={`diff-panes ${result.expectedText && result.actualText ? 'two' : ''}`}>
                {result.expectedText !== undefined && (
                  <div>
                    <div className="pane-label">expected</div>
                    <pre>{result.expectedText}</pre>
                  </div>
                )}
                {result.actualText !== undefined && (
                  <div>
                    <div className="pane-label">yours</div>
                    <pre>{result.actualText}</pre>
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
            <ResultRow key={t.id} result={t} />
          ))}
        </ul>
      )}
    </div>
  );
}
