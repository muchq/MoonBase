import { chromium } from 'playwright';
import { spawn } from 'node:child_process';
import { strict as assert } from 'node:assert';
import { mkdtemp } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { setTimeout as delay } from 'node:timers/promises';

const port = 4177;
const base = `http://127.0.0.1:${port}`;
const server = spawn(
  process.execPath,
  ['node_modules/vite/bin/vite.js', '--host', '127.0.0.1', '--port', String(port), '--strictPort'],
  {
    cwd: process.cwd(),
    env: { ...process.env, VITEST: '1' },
    stdio: ['ignore', 'pipe', 'pipe'],
  },
);
let output = '';
server.stdout.on('data', (chunk) => {
  output += chunk;
});
server.stderr.on('data', (chunk) => {
  output += chunk;
});
let browser;
try {
  let ready = false;
  for (let i = 0; i < 100; i++) {
    if (server.exitCode !== null) throw new Error(`Preview exited: ${output}`);
    try {
      if ((await fetch(base)).ok) {
        ready = true;
        break;
      }
    } catch {}
    await delay(100);
  }
  assert(ready, `Preview did not start: ${output}`);
  browser = await chromium.launch({ headless: true });
  const context = await browser.newContext({
    viewport: { width: 1440, height: 1000 },
    colorScheme: 'dark',
  });
  const page = await context.newPage();
  const errors = [];
  page.on('pageerror', (e) => errors.push(e.message));
  const screenshots = await mkdtemp(join(tmpdir(), 'astlol-review-'));
  await page.goto(base);
  await page
    .getByRole('heading', { name: 'Abstract syntax trees, for working programmers' })
    .waitFor();
  await page.getByRole('combobox', { name: 'Theme', exact: true }).selectOption('light');
  assert.equal(await page.locator('html').getAttribute('data-theme'), 'light');
  assert.equal(
    await page.locator('body').evaluate((e) => getComputedStyle(e).backgroundColor),
    'rgb(247, 249, 252)',
  );
  await page.screenshot({ path: join(screenshots, 'home-light.png'), fullPage: false });
  await page.reload();
  assert.equal(
    await page.getByRole('combobox', { name: 'Theme', exact: true }).inputValue(),
    'light',
  );
  await page.getByRole('combobox', { name: 'Theme', exact: true }).selectOption('dark');
  await page.screenshot({ path: join(screenshots, 'home-dark.png'), fullPage: false });
  await page.getByRole('combobox', { name: 'Theme', exact: true }).selectOption('system');
  await page.emulateMedia({ colorScheme: 'light' });
  assert.equal(
    await page.locator('body').evaluate((e) => getComputedStyle(e).backgroundColor),
    'rgb(247, 249, 252)',
  );
  await page.emulateMedia({ colorScheme: 'dark' });
  assert.equal(
    await page.locator('body').evaluate((e) => getComputedStyle(e).backgroundColor),
    'rgb(13, 16, 23)',
  );

  await page.getByRole('radio', { name: 'Code formatter' }).check();
  await page.goto(`${base}/challenge/sql-select-parse`);
  assert.equal(
    await page
      .getByRole('link', { name: 'Next: Formatting: layout as language' })
      .getAttribute('href'),
    '/lesson/layout',
  );
  await page.goto(`${base}/lesson/tokens`);
  let saved = await page.evaluate(() => JSON.parse(localStorage.getItem('astlol:v1')));
  assert(saved.visited.tokens);
  assert(!saved.completed.tokens);
  await page.getByRole('button', { name: 'Mark as read' }).click();
  const questions = page.locator('.practice-question');
  await questions.nth(0).getByRole('radio', { name: 'Three', exact: true }).check();
  await questions.nth(0).getByRole('button', { name: 'Check reasoning' }).click();
  await questions
    .nth(1)
    .getByRole('radio', { name: 'To consume a decimal point only when a digit follows' })
    .check();
  await questions.nth(1).getByRole('button', { name: 'Check reasoning' }).click();
  saved = await page.evaluate(() => JSON.parse(localStorage.getItem('astlol:v1')));
  assert(saved.completed.tokens);

  // Exercise the actual CodeMirror editor and real module worker.
  await page.goto(`${base}/challenge/expr-walk`);
  await page.getByRole('button', { name: 'Run tests', exact: true }).click();
  await page.getByText('0/5 tests pass', { exact: true }).waitFor();
  await page.getByText(/Reference solution —/).click();
  await page.getByRole('button', { name: 'Load into editor' }).click();
  await page.getByRole('button', { name: 'Run tests', exact: true }).click();
  await page.getByText('Solved — 5/5 tests pass', { exact: true }).waitFor();
  await delay(500);
  await page.reload();
  assert(
    (await page.getByRole('textbox', { name: 'Code editor', exact: true }).innerText()).includes(
      'events.push',
    ),
  );
  saved = await page.evaluate(() => JSON.parse(localStorage.getItem('astlol:v1')));
  assert(saved.completed['expr-walk']);
  assert(saved.studied['expr-walk']);
  // A nonterminating submission must not freeze the page.
  const editor = page.getByRole('textbox', { name: 'Code editor', exact: true });
  await editor.fill('function trace(ast) { while (true) {} }');
  await page.getByRole('button', { name: 'Run tests', exact: true }).click();
  await page.getByText(/Time budget exceeded/).waitFor({ timeout: 15000 });
  await page.getByRole('combobox', { name: 'Theme', exact: true }).selectOption('light');
  await page.screenshot({ path: join(screenshots, 'challenge-light.png') });

  await page.goto(`${base}/lab`);
  await page.getByRole('button', { name: 'Filter pushdown', exact: true }).click();
  await page.getByRole('button', { name: 'cost', exact: true }).click();
  await page.getByText(/Demo database · naive/).waitFor();
  await page.getByRole('button', { name: 'format', exact: true }).click();
  await page.getByRole('slider', { name: /Line width/ }).fill('28');
  assert((await page.locator('.lab-result pre').innerText()).includes('\n'));
  await page.screenshot({ path: join(screenshots, 'lab-light.png') });
  for (const path of ['/', '/lesson/tokens', '/challenge/sql-row-expr', '/lab']) {
    await page.setViewportSize({ width: 390, height: 844 });
    await page.goto(base + path);
    await page.locator('main h2').first().waitFor();
    assert(
      await page.evaluate(() => document.documentElement.scrollWidth <= window.innerWidth),
      `Horizontal overflow: ${path}`,
    );
  }
  await page.goto(base);
  await page.screenshot({ path: join(screenshots, 'home-mobile.png') });
  assert.deepEqual(errors, []);
  console.log(`Browser checks passed. Screenshots: ${screenshots}`);
} finally {
  await browser?.close();
  server.kill('SIGTERM');
}
