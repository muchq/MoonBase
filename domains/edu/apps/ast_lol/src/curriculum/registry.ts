import { tier1 } from './tier1';
import { tier2 } from './tier2';
import { tier3 } from './tier3';
import { tier4 } from './tier4';
import { tier5 } from './tier5';
import { tier6 } from './tier6';
import type { ChallengeDef, LessonDef, Step, Tier } from './types';

const tier0: Tier = {
  id: 'tier0',
  number: 0,
  title: 'Orientation',
  subtitle: 'What an AST buys you, and where this course is going.',
  steps: [
    {
      kind: 'lesson',
      lesson: {
        id: 'welcome',
        title: 'Why ASTs',
        summary: 'The course map: from arithmetic expressions to SQL plans and a working optimizer.',
        reading: [
          {
            title: 'AST Explorer',
            url: 'https://astexplorer.net/',
            note: 'paste code, see the tree — the fastest intuition builder there is',
          },
          {
            title: 'Wikipedia — Abstract syntax tree',
            url: 'https://en.wikipedia.org/wiki/Abstract_syntax_tree',
          },
          {
            title: 'Crafting Interpreters',
            url: 'https://craftinginterpreters.com/',
            note: 'the free book this course keeps citing; superb and self-contained',
          },
        ],
      },
    },
  ],
};

export const tiers: Tier[] = [tier0, tier1, tier2, tier3, tier4, tier5, tier6];

export const orderedSteps: Step[] = tiers.flatMap((t) => t.steps);

export const stepId = (step: Step): string =>
  step.kind === 'lesson' ? step.lesson.id : step.challenge.id;

export const challenges: ChallengeDef[] = orderedSteps
  .filter((s): s is Extract<Step, { kind: 'challenge' }> => s.kind === 'challenge')
  .map((s) => s.challenge);

export const lessons: LessonDef[] = orderedSteps
  .filter((s): s is Extract<Step, { kind: 'lesson' }> => s.kind === 'lesson')
  .map((s) => s.lesson);

const challengeIndex = new Map(challenges.map((c) => [c.id, c]));
const lessonIndex = new Map(lessons.map((l) => [l.id, l]));

export const challengeById = (id: string): ChallengeDef | undefined => challengeIndex.get(id);
export const lessonById = (id: string): LessonDef | undefined => lessonIndex.get(id);

/** The tier a step belongs to, for breadcrumbs and next/prev navigation. */
export function tierOfStep(id: string): Tier | undefined {
  return tiers.find((t) => t.steps.some((s) => stepId(s) === id));
}

export function stepPosition(id: string): number {
  return orderedSteps.findIndex((s) => stepId(s) === id);
}
