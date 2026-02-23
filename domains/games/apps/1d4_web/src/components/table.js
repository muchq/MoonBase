/**
 * Reusable sortable table for game rows.
 * @typedef {Object} GameRow
 * @property {string} gameUrl
 * @property {string} platform
 * @property {string} whiteUsername
 * @property {string} blackUsername
 * @property {number|null} whiteElo
 * @property {number|null} blackElo
 * @property {string} timeClass
 * @property {string} eco
 * @property {string} result
 * @property {string|number} playedAt
 * @property {number} numMoves
 * @property {boolean} hasPin
 * @property {boolean} hasCrossPin
 * @property {boolean} hasFork
 * @property {boolean} hasSkewer
 * @property {boolean} hasDiscoveredAttack
 */

import { renderMotifs } from './motifs.js';

const COLUMNS = [
  { id: 'gameUrl', label: 'Game', sort: false, link: true },
  { id: 'whiteUsername', label: 'White', sort: true },
  { id: 'blackUsername', label: 'Black', sort: true },
  { id: 'whiteElo', label: 'White ELO', sort: true },
  { id: 'blackElo', label: 'Black ELO', sort: true },
  { id: 'timeClass', label: 'Time', sort: true },
  { id: 'eco', label: 'ECO', sort: true },
  { id: 'result', label: 'Result', sort: true },
  { id: 'playedAt', label: 'Played', sort: true },
  { id: 'motifs', label: 'Motifs', sort: false },
];

function formatElo(v) {
  return v != null ? String(v) : '—';
}

function formatDate(playedAt) {
  if (playedAt == null) return '—';
  const date = typeof playedAt === 'number' ? new Date(playedAt * 1000) : new Date(playedAt);
  return isNaN(date.getTime()) ? '—' : date.toISOString().slice(0, 10);
}

/**
 * @param {GameRow[]} games
 * @param {Object} options
 * @param {string} [options.sortBy]
 * @param {'asc'|'desc'} [options.sortDir]
 * @param {function(string): void} [options.onSort]
 * @returns {HTMLTableElement}
 */
export function renderGamesTable(games, options = {}) {
  const { sortBy, sortDir = 'asc', onSort } = options;
  const table = document.createElement('table');
  const thead = document.createElement('thead');
  const tr = document.createElement('tr');
  for (const col of COLUMNS) {
    const th = document.createElement('th');
    th.textContent = col.label;
    if (col.sort && onSort) {
      th.classList.toggle('sorted-asc', sortBy === col.id && sortDir === 'asc');
      th.classList.toggle('sorted-desc', sortBy === col.id && sortDir === 'desc');
      th.appendChild(document.createElement('span')).className = 'sort-icon';
      th.addEventListener('click', () => onSort(col.id));
    }
    tr.appendChild(th);
  }
  thead.appendChild(tr);
  table.appendChild(thead);
  const tbody = document.createElement('tbody');
  for (const game of games) {
    const row = document.createElement('tr');
    for (const col of COLUMNS) {
      const td = document.createElement('td');
      if (col.id === 'gameUrl') {
        if (col.link && game.gameUrl) {
          const a = document.createElement('a');
          a.href = game.gameUrl;
          a.target = '_blank';
          a.rel = 'noopener noreferrer';
          a.className = 'external';
          a.textContent = 'View';
          td.appendChild(a);
        } else {
          td.textContent = '—';
        }
      } else if (col.id === 'motifs') {
        td.appendChild(renderMotifs(game));
      } else if (col.id === 'whiteElo' || col.id === 'blackElo') {
        td.textContent = formatElo(game[col.id]);
      } else if (col.id === 'playedAt') {
        td.textContent = formatDate(game.playedAt);
      } else {
        td.textContent = game[col.id] ?? '—';
      }
      row.appendChild(td);
    }
    tbody.appendChild(row);
  }
  table.appendChild(tbody);
  return table;
}

export { COLUMNS };
