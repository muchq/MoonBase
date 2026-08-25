import { Link } from 'react-router';
import { challenges, orderedSteps, stepId, tiers } from '../curriculum/registry';
import type { Step } from '../curriculum/types';
import { useProgress } from '../state/progress';

const stepPath = (step: Step): string =>
  step.kind === 'lesson' ? `/lesson/${step.lesson.id}` : `/challenge/${step.challenge.id}`;

function StepCard({ step, done }: { step: Step; done: boolean }) {
  const title = step.kind === 'lesson' ? step.lesson.title : step.challenge.title;
  const summary = step.kind === 'lesson' ? step.lesson.summary : step.challenge.summary;
  return (
    <Link className="step-card" to={stepPath(step)}>
      <span className={`step-kind ${step.kind}`}>{step.kind === 'lesson' ? 'read' : 'solve'}</span>
      <span>
        <span className="step-title">{title}</span>
        {step.kind === 'challenge' && (
          <span className="difficulty" title={`difficulty ${step.challenge.difficulty}/5`}>
            {' '}
            {'●'.repeat(step.challenge.difficulty)}
            {'○'.repeat(5 - step.challenge.difficulty)}
          </span>
        )}
        <div className="step-summary">{summary}</div>
      </span>
      {done && (
        <span className="done" aria-label="completed">
          ✓
        </span>
      )}
    </Link>
  );
}

export default function HomeView() {
  const progress = useProgress();
  const anyDone = orderedSteps.some((s) => progress.completed[stepId(s)] !== undefined);
  const firstOpen = orderedSteps.find((s) => progress.completed[stepId(s)] === undefined);

  return (
    <>
      <section className="hero">
        <h2>Abstract syntax trees, for working programmers</h2>
        <p>
          Parse, transform, and optimize — from arithmetic expressions to SQL query plans, with an
          auto-grader that runs entirely in your browser. Eighteen challenges build on each other
          toward two capstones: a query optimizer with a real cost model, and a width-aware code
          formatter. No compiler background assumed.
        </p>
        {firstOpen !== undefined && (
          <Link className="step-card" to={stepPath(firstOpen)} style={{ maxWidth: '24rem' }}>
            <span className="step-kind challenge">{anyDone ? 'continue' : 'start here'}</span>
            <span>
              <span className="step-title">
                {firstOpen.kind === 'lesson' ? firstOpen.lesson.title : firstOpen.challenge.title}
              </span>
            </span>
          </Link>
        )}
      </section>
      {tiers.map((tier) => (
        <section className="tier" key={tier.id}>
          <div className="tier-heading">
            <span className="tier-number">tier {tier.number}</span>
            <h3>{tier.title}</h3>
            <span className="subtitle">{tier.subtitle}</span>
          </div>
          <div className="step-list">
            {tier.steps.map((step) => (
              <StepCard
                key={stepId(step)}
                step={step}
                done={progress.completed[stepId(step)] !== undefined}
              />
            ))}
          </div>
        </section>
      ))}
    </>
  );
}
