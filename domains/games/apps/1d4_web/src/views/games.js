/**
 * Indexed data browser — browse games with sortable columns, motif badges, pagination.
 */

import { query } from '../api.js';
import { renderGamesTable } from '../components/table.js';

const DEFAULT_QUERY = 'num.moves >= 0';
const PAGE_SIZES = [10, 25, 50, 100];

function escapeChessQLString(s) {
  return s.replace(/\\/g, '\\\\').replace(/"/g, '\\"');
}

function buildQuery(username) {
  const u = username.trim();
  if (!u) return DEFAULT_QUERY;
  const escaped = escapeChessQLString(u);
  return `white.username = "${escaped}" OR black.username = "${escaped}"`;
}

export function renderGames(container) {
  let limit = 25;
  let offset = 0;
  let sortBy = 'playedAt';
  let sortDir = 'desc';
  let usernameFilter = '';
  let games = [];
  let totalCount = 0;
  let loading = true;
  let error = null;

  function sortGames(list) {
    if (!sortBy || sortBy === 'motifs') return list;
    const key = sortBy;
    return [...list].sort((a, b) => {
      let va = a[key];
      let vb = b[key];
      if (key === 'playedAt') {
        va = typeof va === 'number' ? va : (va ? new Date(va).getTime() / 1000 : 0);
        vb = typeof vb === 'number' ? vb : (vb ? new Date(vb).getTime() / 1000 : 0);
      }
      if (va == null) return sortDir === 'asc' ? -1 : 1;
      if (vb == null) return sortDir === 'asc' ? 1 : -1;
      const cmp = va < vb ? -1 : va > vb ? 1 : 0;
      return sortDir === 'asc' ? cmp : -cmp;
    });
  }

  function fetchPage() {
    loading = true;
    error = null;
    render();
    const q = buildQuery(usernameFilter);
    query({ query: q, limit: 500, offset: 0 })
      .then((res) => {
        games = res.games || [];
        totalCount = res.count ?? games.length;
        loading = false;
        render();
      })
      .catch((e) => {
        error = e.body || e.message || 'Failed to load games';
        loading = false;
        render();
      });
  }

  function render() {
    container.innerHTML = '';
    const panel = document.createElement('div');
    panel.className = 'panel';
    panel.innerHTML = '<h2>Indexed games</h2><p class="text-muted">Browse indexed games. Optionally filter by username (exact match, white or black).</p>';
    const searchRow = document.createElement('div');
    searchRow.className = 'form-group';
    searchRow.style.marginTop = '0.75rem';
    searchRow.style.display = 'flex';
    searchRow.style.flexWrap = 'wrap';
    searchRow.style.gap = '0.5rem';
    searchRow.style.alignItems = 'center';
    const label = document.createElement('label');
    label.htmlFor = 'games-username';
    label.textContent = 'Username';
    label.style.marginBottom = 0;
    const input = document.createElement('input');
    input.id = 'games-username';
    input.type = 'text';
    input.placeholder = 'e.g. Hikaru';
    input.value = usernameFilter;
    input.style.maxWidth = '200px';
    input.addEventListener('input', () => { usernameFilter = input.value; });
    input.addEventListener('keydown', (e) => { if (e.key === 'Enter') { e.preventDefault(); fetchPage(); } });
    const btn = document.createElement('button');
    btn.type = 'button';
    btn.className = 'btn';
    btn.textContent = 'Search';
    btn.addEventListener('click', () => { usernameFilter = input.value; fetchPage(); });
    searchRow.appendChild(label);
    searchRow.appendChild(input);
    searchRow.appendChild(btn);
    panel.appendChild(searchRow);
    container.appendChild(panel);

    if (error) {
      const msg = document.createElement('div');
      msg.className = 'message error';
      msg.textContent = error;
      container.appendChild(msg);
    }

    if (loading) {
      const load = document.createElement('div');
      load.className = 'loading';
      load.textContent = 'Loading…';
      container.appendChild(load);
      return;
    }

    const sorted = sortGames(games);
    const page = sorted.slice(offset, offset + limit);

    function onSort(col) {
      if (sortBy === col) sortDir = sortDir === 'asc' ? 'desc' : 'asc';
      else sortBy = col;
      sortDir = sortBy === 'playedAt' ? 'desc' : sortDir;
      render();
    }

    const wrap = document.createElement('div');
    wrap.className = 'table-wrap';
    wrap.appendChild(renderGamesTable(page, { sortBy, sortDir, onSort }));
    container.appendChild(wrap);

    const pag = document.createElement('div');
    pag.className = 'pagination';
    const limitSelect = document.createElement('select');
    limitSelect.innerHTML = PAGE_SIZES.map((n) => `<option value="${n}" ${n === limit ? 'selected' : ''}>${n} per page</option>`).join('');
    limitSelect.addEventListener('change', () => {
      limit = Number(limitSelect.value);
      offset = 0;
      render();
    });
    pag.appendChild(limitSelect);
    const prev = document.createElement('button');
    prev.className = 'btn';
    prev.textContent = 'Previous';
    prev.disabled = offset === 0;
    prev.addEventListener('click', () => {
      offset = Math.max(0, offset - limit);
      render();
    });
    pag.appendChild(prev);
    const next = document.createElement('button');
    next.className = 'btn';
    next.textContent = 'Next';
    next.disabled = offset + limit >= sorted.length;
    next.addEventListener('click', () => {
      offset = Math.min(offset + limit, sorted.length - limit);
      render();
    });
    pag.appendChild(next);
    const info = document.createElement('span');
    info.style.color = 'var(--text-muted)';
    info.textContent = `${offset + 1}–${Math.min(offset + limit, sorted.length)} of ${sorted.length}`;
    pag.appendChild(info);
    container.appendChild(pag);
  }

  fetchPage();
}
