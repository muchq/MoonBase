import type { GradableChallenge } from '../grader/types';

/** A curated pointer to well-regarded outside material. */
export interface Reading {
  title: string;
  url: string;
  /** Why this link, in a phrase. */
  note?: string;
}

export interface LessonDef {
  id: string;
  title: string;
  /** One line for curriculum cards. */
  summary: string;
  reading: Reading[];
}

export interface ChallengeDef extends GradableChallenge {
  title: string;
  summary: string;
  /** Display signature, e.g. `tokenize(source) → Token[]`. */
  signature: string;
  starter: string;
  /** 1 (warm-up) … 5 (capstone). */
  difficulty: 1 | 2 | 3 | 4 | 5;
}

export type Step =
  | { kind: 'lesson'; lesson: LessonDef }
  | { kind: 'challenge'; challenge: ChallengeDef };

export interface Tier {
  id: string;
  number: number;
  title: string;
  subtitle: string;
  steps: Step[];
}
