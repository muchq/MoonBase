import { readdirSync, readFileSync } from 'node:fs';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { Marked } from 'marked';
import { gfmHeadingId } from 'marked-gfm-heading-id';
import type { Plugin } from 'vite';

/**
 * Serves the course's markdown (lessons and challenge statements) to the app
 * as HTML, converted here rather than in the browser: the documents are
 * static, so a markdown renderer in the bundle would be a parser shipped to
 * every reader to produce the same bytes every time. Converting at build time
 * keeps marked a devDependency.
 */
export const CONTENT_ID = 'virtual:astlol-content';
const RESOLVED_ID = `\0${CONTENT_ID}`;

const CONTENT_DIR = fileURLToPath(new URL('./src/content', import.meta.url));

// gfmHeadingId so in-document links to sections resolve; the slugs match
// GitHub's, which is what the author of a markdown link is writing against.
const renderer = new Marked({ gfm: true }, gfmHeadingId());

export function renderDoc(markdown: string): string {
  const html = renderer.parse(markdown, { async: false });
  // Tables (token rosters, precedence tables) are wider than a phone; the
  // wrapper is what the CSS scrolls. Markdown tables cannot nest, and marked
  // escapes anything inside a fence, so these are the generated table
  // elements and only those.
  return html
    .replaceAll('<table>', '<div class="table-scroll"><table>')
    .replaceAll('</table>', '</table></div>');
}

function renderDir(dir: string, addWatch: (f: string) => void): Record<string, string> {
  const out: Record<string, string> = {};
  for (const file of readdirSync(dir).sort()) {
    if (!file.endsWith('.md')) continue;
    const path = join(dir, file);
    addWatch(path);
    out[file.slice(0, -'.md'.length)] = renderDoc(readFileSync(path, 'utf-8'));
  }
  return out;
}

export default function astlolContent(): Plugin {
  return {
    name: 'astlol-content',
    resolveId(id) {
      return id === CONTENT_ID ? RESOLVED_ID : null;
    },
    load(id) {
      if (id !== RESOLVED_ID) return null;
      const watch = (f: string) => this.addWatchFile(f);
      const lessons = renderDir(join(CONTENT_DIR, 'lessons'), watch);
      const challenges = renderDir(join(CONTENT_DIR, 'challenges'), watch);
      return [
        `export const lessonHtml = ${JSON.stringify(lessons)};`,
        `export const challengeHtml = ${JSON.stringify(challenges)};`,
      ].join('\n');
    },
  };
}
