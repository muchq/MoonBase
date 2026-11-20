import { stepId, tiers } from './registry';
import type { Step } from './types';
import type { ProgressState } from '../state/progress';

export const stepPath = (step: Step): string =>
  step.kind === 'lesson' ? `/lesson/${step.lesson.id}` : `/challenge/${step.challenge.id}`;
export const stepTitle = (step: Step): string =>
  step.kind === 'lesson' ? step.lesson.title : step.challenge.title;
export function learningPath(track: ProgressState['track']): Step[] {
  return tiers
    .filter((t) => t.number <= 3 || (track === 'formatter' ? t.id === 'tier6' : t.id !== 'tier6'))
    .flatMap((t) => t.steps);
}
export function stepFinished(step: Step, progress: ProgressState): boolean {
  return (
    progress.completed[stepId(step)] !== undefined ||
    (step.kind === 'lesson' && progress.read[step.lesson.id] !== undefined)
  );
}
export function pathForStep(id: string, track: ProgressState['track']): Step[] {
  const tier = tiers.find((t) => t.steps.some((s) => stepId(s) === id));
  return learningPath(
    tier?.id === 'tier6' ? 'formatter' : (tier?.number ?? 0) >= 4 ? 'engine' : track,
  );
}
