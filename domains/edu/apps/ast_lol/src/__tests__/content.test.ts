import { describe, expect, it } from 'vitest';
import { challengeHtml, lessonHtml } from 'virtual:astlol-content';
import { challenges, lessons } from '../curriculum/registry';

/**
 * Content and registry must cover each other exactly: a challenge without a
 * statement renders an empty page, and an orphaned document is dead weight
 * nobody can reach.
 */
describe('course content', () => {
  it('every lesson has a rendered document, and no orphans exist', () => {
    expect(Object.keys(lessonHtml).sort()).toEqual(lessons.map((l) => l.id).sort());
  });

  it('every challenge has a rendered statement, and no orphans exist', () => {
    expect(Object.keys(challengeHtml).sort()).toEqual(challenges.map((c) => c.id).sort());
  });

  it('documents are real HTML, not stub paragraphs', () => {
    for (const [id, html] of [...Object.entries(lessonHtml), ...Object.entries(challengeHtml)]) {
      expect(html.length, id).toBeGreaterThan(500);
    }
  });

  it('challenge statements include a debugging section', () => {
    for (const challenge of challenges) {
      expect(challengeHtml[challenge.id], challenge.id).toContain('Debugging');
    }
  });
});
