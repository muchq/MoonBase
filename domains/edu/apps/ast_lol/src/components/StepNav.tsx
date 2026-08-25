import { Link } from 'react-router';
import { orderedSteps, stepId, stepPosition } from '../curriculum/registry';
import type { Step } from '../curriculum/types';

const stepPath = (step: Step): string =>
  step.kind === 'lesson' ? `/lesson/${step.lesson.id}` : `/challenge/${step.challenge.id}`;

const stepTitle = (step: Step): string =>
  step.kind === 'lesson' ? step.lesson.title : step.challenge.title;

export default function StepNav({ currentId }: { currentId: string }) {
  const index = stepPosition(currentId);
  const prev = index > 0 ? orderedSteps[index - 1] : null;
  const next = index >= 0 && index < orderedSteps.length - 1 ? orderedSteps[index + 1] : null;
  return (
    <nav className="step-nav">
      {prev !== null ? (
        <Link to={stepPath(prev)}>← {stepTitle(prev)}</Link>
      ) : (
        <Link to="/">← Curriculum</Link>
      )}
      {next !== null ? <Link to={stepPath(next)}>{stepTitle(next)} →</Link> : <span />}
    </nav>
  );
}
