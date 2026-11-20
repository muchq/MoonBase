// Browser checks for what jsdom cannot run: the CodeMirror editor, the grading
// worker and its timeout, real CSS for both themes, and phone-width layout.
//
//   npm run test:browser                       # needs `npx playwright install chromium`
//   SCREENSHOT_DIR=/tmp/shots npm run test:browser   # also saves screenshots
import { strict as assert } from 'node:assert';
import { spawn } from 'node:child_process';
import { mkdir } from 'node:fs/promises';
import { join } from 'node:path';
import { setTimeout as delay } from 'node:timers/promises';
import { chromium } from 'playwright';

const port = 4177;
const base = `http://127.0.0.1:${port}`;
const lightBackground = 'rgb(247, 249, 252)';
const darkBackground = 'rgb(13, 16, 23)';
// Drafts save on a debounce; a reload sooner than this can lose the last edit.
const draftSaveDelayMs = 500;

const server = startDevServer();
let browser;
try {
  await waitForServer(server);
  browser = await chromium.launch({ headless: true });
  const page = await (
    await browser.newContext({ viewport: { width: 1440, height: 1000 }, colorScheme: 'dark' })
  ).newPage();
  const pageErrors = [];
  page.on('pageerror', (e) => pageErrors.push(e.message));

  const theme = () => page.getByRole('combobox', { name: 'Theme', exact: true });
  const background = () =>
    page.locator('body').evaluate((e) => getComputedStyle(e).backgroundColor);
  const saved = () => page.evaluate(() => JSON.parse(localStorage.getItem('astlol:v1')));
  const runTests = () => page.getByRole('button', { name: 'Run tests', exact: true }).click();
  const editor = () => page.getByRole('textbox', { name: 'Code editor', exact: true });

  await step('a chosen theme applies, survives a reload, and System follows the OS', async () => {
    await page.goto(base);
    await page
      .getByRole('heading', { name: 'Abstract syntax trees, for working programmers' })
      .waitFor();
    await theme().selectOption('light');
    assert.equal(await page.locator('html').getAttribute('data-theme'), 'light');
    assert.equal(await background(), lightBackground);
    await screenshot(page, 'home-light');
    await page.reload();
    assert.equal(await theme().inputValue(), 'light');
    await theme().selectOption('dark');
    await screenshot(page, 'home-dark');
    await theme().selectOption('system');
    await page.emulateMedia({ colorScheme: 'light' });
    assert.equal(await background(), lightBackground);
    await page.emulateMedia({ colorScheme: 'dark' });
    assert.equal(await background(), darkBackground);
  });

  await step('the formatter track leads from the SQL parser to the layout lesson', async () => {
    await page.getByRole('radio', { name: 'Code formatter' }).check();
    await page.goto(`${base}/challenge/sql-select-parse`);
    const next = page.getByRole('link', { name: 'Next: Formatting: layout as language' });
    assert.equal(await next.getAttribute('href'), '/lesson/layout');
  });

  await step('a lesson completes only after its reading checks are answered', async () => {
    await page.goto(`${base}/lesson/tokens`);
    assert((await saved()).visited.tokens, 'visit not recorded');
    assert(!(await saved()).completed.tokens, 'a visit alone completed the lesson');
    await page.getByRole('button', { name: 'Mark as read' }).click();
    const questions = page.locator('.practice-question');
    const answer = async (i, option) => {
      await questions.nth(i).getByRole('radio', { name: option, exact: true }).check();
      await questions.nth(i).getByRole('button', { name: 'Check reasoning' }).click();
    };
    await answer(0, 'Three');
    await answer(1, 'To consume a decimal point only when a digit follows');
    assert((await saved()).completed.tokens, 'correct answers did not complete the lesson');
  });

  await step('the editor and grading worker grade a submission, and keep the draft', async () => {
    await page.goto(`${base}/challenge/expr-walk`);
    await runTests();
    await page.getByText('0/5 tests pass', { exact: true }).waitFor();
    await page.getByText(/Reference solution —/).click();
    await page.getByRole('button', { name: 'Load into editor' }).click();
    await runTests();
    await page.getByText('Solved — 5/5 tests pass', { exact: true }).waitFor();
    await delay(draftSaveDelayMs);
    await page.reload();
    assert((await editor().innerText()).includes('events.push'), 'draft lost on reload');
    assert((await saved()).completed['expr-walk'], 'solve not recorded');
    assert((await saved()).studied['expr-walk'], 'reference consultation not recorded');
  });

  await step('a submission that never returns times out without freezing the page', async () => {
    await editor().fill('function trace(ast) { while (true) {} }');
    await runTests();
    await page.getByText(/Time budget exceeded/).waitFor({ timeout: 15000 });
    await theme().selectOption('light');
    await screenshot(page, 'challenge-light');
  });

  await step('the lab shows plan costs and reformats SQL at a narrow width', async () => {
    await page.goto(`${base}/lab`);
    await page.getByRole('button', { name: 'Filter pushdown', exact: true }).click();
    await page.getByRole('button', { name: 'cost', exact: true }).click();
    await page.getByText(/Demo database · naive/).waitFor();
    await page.getByRole('button', { name: 'format', exact: true }).click();
    await page.getByRole('slider', { name: /Line width/ }).fill('28');
    assert((await page.locator('.lab-result pre').innerText()).includes('\n'), 'did not wrap');
    await screenshot(page, 'lab-light');
  });

  await step('no page scrolls sideways at phone width', async () => {
    await page.setViewportSize({ width: 390, height: 844 });
    for (const path of ['/', '/lesson/tokens', '/challenge/sql-row-expr', '/lab']) {
      await page.goto(base + path);
      await page.locator('main h2').first().waitFor();
      const fits = await page.evaluate(
        () => document.documentElement.scrollWidth <= window.innerWidth,
      );
      assert(fits, `horizontal overflow on ${path}`);
    }
    await page.goto(base);
    await screenshot(page, 'home-mobile');
  });

  await step('no uncaught errors on any page', async () => {
    assert.deepEqual(pageErrors, []);
  });
  console.log('All browser checks passed.');
} finally {
  await browser?.close();
  server.kill('SIGTERM');
}

async function step(name, check) {
  try {
    await check();
    console.log(`  ok    ${name}`);
  } catch (e) {
    console.error(`  FAIL  ${name}`);
    throw e;
  }
}

async function screenshot(page, name) {
  const dir = process.env.SCREENSHOT_DIR;
  if (!dir) return;
  await mkdir(dir, { recursive: true });
  await page.screenshot({ path: join(dir, `${name}.png`) });
}

// The Vite dev server, without the Cloudflare plugin (VITEST skips it), which
// would otherwise start workerd for a page that needs none of it.
function startDevServer() {
  const vite = 'node_modules/vite/bin/vite.js';
  const child = spawn(
    process.execPath,
    [vite, '--host', '127.0.0.1', '--port', `${port}`, '--strictPort'],
    {
      env: { ...process.env, VITEST: '1' },
      stdio: ['ignore', 'pipe', 'pipe'],
    },
  );
  child.output = '';
  child.stdout.on('data', (chunk) => (child.output += chunk));
  child.stderr.on('data', (chunk) => (child.output += chunk));
  return child;
}

async function waitForServer(child) {
  for (let i = 0; i < 100; i++) {
    if (child.exitCode !== null) throw new Error(`Vite exited:\n${child.output}`);
    const up = await fetch(base).then(
      (r) => r.ok,
      () => false,
    );
    if (up) return;
    await delay(100);
  }
  throw new Error(`Vite did not start within 10 s:\n${child.output}`);
}
