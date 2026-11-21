import { Link } from 'react-router';
import { stepId } from '../curriculum/registry';
import { pathForStep, stepPath, stepTitle } from '../curriculum/path';
import { useProgress } from '../state/progress';

export default function StepNav({ currentId }: { currentId: string }) {
  const { track } = useProgress();
  const path = pathForStep(currentId, track);
  const index = path.findIndex((s) => stepId(s) === currentId);
  const prev = index > 0 ? path[index - 1] : null;
  const next = index >= 0 && index < path.length - 1 ? path[index + 1] : null;
  return (
    <>
      {currentId === 'sql-select-parse' && (
        <aside className="branch-callout">
          <h3>Your foundations are ready. Choose a direction.</h3>
          <p>
            Build a query engine, or work directly with syntax in the formatter track. Both remain
            open.
          </p>
          <div className="branch-links">
            <Link to="/lesson/resolution">Query engine and optimizer</Link>
            <Link to="/lesson/layout">Code formatter</Link>
          </div>
        </aside>
      )}
      <nav className="step-nav" aria-label="Learning path">
        <Link to={prev ? stepPath(prev) : '/'}>
          {prev ? `Previous: ${stepTitle(prev)}` : 'Curriculum'}
        </Link>
        {next ? (
          <Link to={stepPath(next)}>Next: {stepTitle(next)}</Link>
        ) : (
          <Link to="/">Track complete · explore the other branch</Link>
        )}
      </nav>
    </>
  );
}
