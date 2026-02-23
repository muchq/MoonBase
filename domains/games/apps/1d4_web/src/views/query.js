/**
 * ChessQL query interface — query input, example chips, results table, limit selector.
 */

import { query } from '../api.js';
import { renderGamesTable } from '../components/table.js';

const EXAMPLE_QUERIES = [
  'motif(fork)',
  'white.elo >= 2500 AND motif(pin)',
  'eco = "B90"',
  'motif(skewer) OR motif(discovered_attack)',
  'time.class = "blitz"',
];

const LIMIT_OPTIONS = [10, 25, 50, 100, 250, 500];

export function renderQuery(container) {
  let queryText = '';
  let limit = 50;
  let offset = 0;
  let results = [];
  let count = 0;
  let loading = false;
  let error = null;

  function runQuery() {
    const q = queryText.trim();
    if (!q) return;
    loading = true;
    error = null;
    render();
    query({ query: q, limit, offset })
      .then((res) => {
        results = res.games || [];
        count = res.count ?? results.length;
        loading = false;
        render();
      })
      .catch((e) => {
        error = e.body || e.message || 'Query failed';
        loading = false;
        render();
      });
  }

  function render() {
    container.innerHTML = '';

    const panel = document.createElement('div');
    panel.className = 'panel';
    panel.innerHTML = '<h2>ChessQL query</h2>';
    const textarea = document.createElement('textarea');
    textarea.placeholder = 'e.g. motif(fork) AND white.elo >= 2500';
    textarea.value = queryText;
    textarea.addEventListener('input', () => { queryText = textarea.value; });
    panel.appendChild(textarea);

    const chipsWrap = document.createElement('div');
    chipsWrap.innerHTML = '<span style="color: var(--text-muted); font-size: 0.875rem;">Examples:</span>';
    const chips = document.createElement('div');
    chips.className = 'chips';
    for (const ex of EXAMPLE_QUERIES) {
      const chip = document.createElement('button');
      chip.type = 'button';
      chip.className = 'chip';
      chip.textContent = ex;
      chip.addEventListener('click', () => {
        queryText = ex;
        textarea.value = ex;
      });
      chips.appendChild(chip);
    }
    chipsWrap.appendChild(chips);
    panel.appendChild(chipsWrap);

    const bar = document.createElement('div');
    bar.className = 'pagination';
    bar.style.marginTop = '0.75rem';
    const limitSelect = document.createElement('select');
    limitSelect.innerHTML = LIMIT_OPTIONS.map((n) => `<option value="${n}" ${n === limit ? 'selected' : ''}>Limit ${n}</option>`).join('');
    limitSelect.addEventListener('change', () => {
      limit = Number(limitSelect.value);
      offset = 0;
      runQuery();
    });
    bar.appendChild(limitSelect);
    const runBtn = document.createElement('button');
    runBtn.type = 'button';
    runBtn.className = 'btn';
    runBtn.textContent = 'Run query';
    runBtn.addEventListener('click', runQuery);
    bar.appendChild(runBtn);
    panel.appendChild(bar);
    container.appendChild(panel);

    const help = document.createElement('div');
    help.className = 'syntax-help';
    help.innerHTML = `
      <strong>ChessQL</strong> — Fields: <code>white.elo</code>, <code>black.elo</code>, <code>white.username</code>, <code>black.username</code>, <code>time.class</code>, <code>num.moves</code>, <code>eco</code>, <code>result</code>, <code>platform</code>, <code>game.url</code>, <code>played.at</code>.
      Motifs: <code>motif(pin)</code>, <code>motif(cross_pin)</code>, <code>motif(fork)</code>, <code>motif(skewer)</code>, <code>motif(discovered_attack)</code>.
      Combine with <code>AND</code>, <code>OR</code>, <code>NOT</code>. Strings in double quotes, e.g. <code>eco = "B90"</code>. Do not use SELECT or *.
    `;
    container.appendChild(help);

    if (error) {
      const msg = document.createElement('div');
      msg.className = 'message error';
      msg.textContent = error;
      container.appendChild(msg);
    }

    if (loading) {
      container.appendChild(document.createElement('div')).className = 'loading';
      container.querySelector('.loading').textContent = 'Loading…';
      return;
    }

    if (results.length > 0) {
      const wrap = document.createElement('div');
      wrap.className = 'table-wrap';
      wrap.appendChild(renderGamesTable(results));
      container.appendChild(wrap);
      const info = document.createElement('p');
      info.className = 'empty';
      info.style.textAlign = 'left';
      info.textContent = `Showing ${results.length} result(s).`;
      container.appendChild(info);
    } else if (queryText.trim() && !loading) {
      const empty = document.createElement('p');
      empty.className = 'empty';
      empty.textContent = 'No results. Try another query or increase the limit.';
      container.appendChild(empty);
    }
  }

  render();
}
