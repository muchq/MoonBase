import { Link } from 'react-router';
import { challenges, stepId, tiers } from '../curriculum/registry';
import { learningPath, stepFinished, stepPath, stepTitle } from '../curriculum/path';
import { lessonPractice, practiceKey, tierGuide } from '../curriculum/learning';
import type { Step, Tier } from '../curriculum/types';
import { setTrack, useProgress } from '../state/progress';

function StepCard({ step }: { step: Step }) {
  const progress = useProgress();
  const id = stepId(step);
  const solved = progress.completed[id] !== undefined;
  const read = progress.read[id] !== undefined;
  const visited = progress.visited[id] !== undefined;
  const studied = progress.studied[id] !== undefined;
  const practiced = (lessonPractice[id] ?? []).every(
    (check) => progress.practiced[practiceKey(id, check)] !== undefined,
  );
  const status = solved
    ? step.kind === 'lesson'
      ? practiced
        ? 'Practiced'
        : 'Previously completed'
      : studied
        ? 'Solved · reference consulted'
        : 'Solved'
    : read
      ? 'Read'
      : studied
        ? 'Reference consulted'
        : visited
          ? 'Visited'
          : '';
  return (
    <Link className="step-card" to={stepPath(step)}>
      <span className={`step-kind ${step.kind}`}>
        {step.kind === 'lesson' ? 'read + practice' : 'solve'}
      </span>
      <span className="step-copy">
        <span className="step-title">{stepTitle(step)}</span>
        <span className="step-summary">
          {step.kind === 'lesson' ? step.lesson.summary : step.challenge.summary}
        </span>
        {status && (
          <span className="step-status" aria-label={solved ? 'completed' : undefined}>
            {status}
          </span>
        )}
      </span>
      {step.kind === 'challenge' && (
        <span className="difficulty" aria-label={`Difficulty ${step.challenge.difficulty} of 5`}>
          {
            ['', 'Introductory', 'Practice', 'Intermediate', 'Advanced', 'Capstone'][
              step.challenge.difficulty
            ]
          }
        </span>
      )}
    </Link>
  );
}
function Module({ tier }: { tier: Tier }) {
  const guide = tierGuide[tier.id];
  return (
    <section className="tier">
      <div className="tier-heading">
        <span className="tier-number">tier {tier.number}</span>
        <h3>{tier.title}</h3>
      </div>
      <p className="module-outcome">{guide.outcome}</p>
      <p className="module-meta">
        {guide.prerequisites} <span>Estimated {guide.effort}, at your pace.</span>
      </p>
      <div className="step-list">
        {tier.steps.map((step) => (
          <StepCard key={stepId(step)} step={step} />
        ))}
      </div>
    </section>
  );
}
export default function HomeView() {
  const progress = useProgress();
  const path = learningPath(progress.track);
  const firstOpen = path.find((step) => !stepFinished(step, progress));
  const anyDone = path.some((step) => stepFinished(step, progress));
  return (
    <>
      <section className="hero">
        <span className="eyebrow">An interactive course for working programmers</span>
        <h2>Abstract syntax trees, for working programmers</h2>
        <p>
          Build an interpreter. Turn SQL into a query engine or a code formatter. Predict, trace,
          repair, and implement across {challenges.length} challenges, with feedback that runs in
          your browser.
        </p>
        <div className="hero-actions">
          {firstOpen && (
            <Link className="resume-link" to={stepPath(firstOpen)}>
              <span>{anyDone ? 'continue' : 'start here'}</span> {stepTitle(firstOpen)}
            </Link>
          )}
          <Link to="/lab">Try a working pipeline</Link>
        </div>
      </section>
      <div className="course-layout">
        <aside className="course-map" aria-label="Course map">
          <h3>Your learning path</h3>
          <ol>
            <li>Expressions: text to value</li>
            <li>Trees: traversal and transformation</li>
            <li>SQL: tokens to queries</li>
          </ol>
          <fieldset className="track-choice">
            <legend>Then choose a destination</legend>
            <label>
              <input
                type="radio"
                name="track"
                checked={progress.track === 'engine'}
                onChange={() => setTrack('engine')}
              />{' '}
              Query engine & optimizer
            </label>
            <label>
              <input
                type="radio"
                name="track"
                checked={progress.track === 'formatter'}
                onChange={() => setTrack('formatter')}
              />{' '}
              Code formatter
            </label>
          </fieldset>
          <p>Your choice sets Continue and next-step navigation. Every exercise stays open.</p>
          <p>
            <strong>Read · Practice · Solve</strong>
            <br />
            Opening a page records a visit. Quick checks and passing code record practice and
            solutions separately.
          </p>
        </aside>
        <div className="course-modules">
          {tiers
            .filter((t) => t.number <= 3)
            .map((tier) => (
              <Module key={tier.id} tier={tier} />
            ))}
          <div className="branch-callout">
            <h3>Two paths from the same foundation</h3>
            <p>
              The formatter needs SQL syntax only. The engine path adds meaning, execution, and
              optimization.
            </p>
          </div>
          {(progress.track === 'engine' ? ['engine', 'formatter'] : ['formatter', 'engine']).map(
            (track) => (
              <section className="track-section" key={track}>
                <p className="eyebrow">
                  {track === progress.track ? 'Your selected path' : 'Explore the other path'}
                </p>
                <h2>{track === 'engine' ? 'Query engine & optimizer' : 'Code formatter'}</h2>
                {tiers
                  .filter((t) =>
                    track === 'engine' ? t.number === 4 || t.number === 5 : t.number === 6,
                  )
                  .map((tier) => (
                    <Module key={tier.id} tier={tier} />
                  ))}
              </section>
            ),
          )}
        </div>
      </div>
    </>
  );
}
