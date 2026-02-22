/**
 * Enqueue index form + request status list. Submit calls POST api.1d4.net/index.
 * Recent requests stored in sessionStorage; poll GET api.1d4.net/index/{id}.
 */

import { createIndex, getIndexStatus } from '../api.js';

const STORAGE_KEY = '1d4_index_request_ids';
const POLL_INTERVAL_MS = 3000;

function getStoredIds() {
  try {
    const raw = sessionStorage.getItem(STORAGE_KEY);
    return raw ? JSON.parse(raw) : [];
  } catch {
    return [];
  }
}

function addStoredId(id) {
  const ids = getStoredIds();
  if (!ids.includes(id)) ids.unshift(id);
  sessionStorage.setItem(STORAGE_KEY, JSON.stringify(ids.slice(0, 50)));
}

export function renderIndex(container) {
  let requestStatuses = {};
  let pollTimer = null;

  function loadStatuses() {
    const ids = getStoredIds();
    if (ids.length === 0) return;
    ids.forEach((id) => {
      getIndexStatus(id)
        .then((res) => {
          requestStatuses[id] = res;
          render();
        })
        .catch(() => {
          requestStatuses[id] = { id, status: 'FAILED', errorMessage: 'Failed to fetch' };
          render();
        });
    });
  }

  function startPolling() {
    if (pollTimer) return;
    const hasActive = getStoredIds().some(
      (id) => requestStatuses[id] && ['PENDING', 'PROCESSING'].includes(requestStatuses[id].status)
    );
    if (!hasActive) return;
    pollTimer = setInterval(() => {
      loadStatuses();
    }, POLL_INTERVAL_MS);
  }

  function stopPolling() {
    if (pollTimer) {
      clearInterval(pollTimer);
      pollTimer = null;
    }
  }

  function render() {
    const hasActive = getStoredIds().some(
      (id) => requestStatuses[id] && ['PENDING', 'PROCESSING'].includes(requestStatuses[id].status)
    );
    if (!hasActive) stopPolling();
    else startPolling();

    container.innerHTML = '';

    const formPanel = document.createElement('div');
    formPanel.className = 'panel';
    formPanel.innerHTML = '<h2>Enqueue index request</h2>';
    const form = document.createElement('form');
    form.innerHTML = `
      <div class="form-group">
        <label for="player">Username</label>
        <input id="player" name="player" type="text" placeholder="e.g. hikaru" required>
      </div>
      <div class="form-group">
        <label for="platform">Platform</label>
        <select id="platform" name="platform">
          <option value="CHESS_COM">chess.com</option>
        </select>
      </div>
      <div class="form-group">
        <label for="startMonth">Start month (YYYY-MM)</label>
        <input id="startMonth" name="startMonth" type="text" placeholder="2024-01" required>
      </div>
      <div class="form-group">
        <label for="endMonth">End month (YYYY-MM)</label>
        <input id="endMonth" name="endMonth" type="text" placeholder="2024-01" required>
      </div>
      <button type="submit" class="btn">Submit</button>
    `;
    form.addEventListener('submit', (e) => {
      e.preventDefault();
      const fd = new FormData(form);
      const player = fd.get('player').trim();
      const platform = fd.get('platform');
      const startMonth = fd.get('startMonth').trim();
      const endMonth = fd.get('endMonth').trim();
      createIndex({ player, platform, startMonth, endMonth })
        .then((res) => {
          addStoredId(res.id);
          requestStatuses[res.id] = res;
          render();
          const msg = document.createElement('div');
          msg.className = 'message success';
          msg.innerHTML = `Request created. ID: <strong>${res.id}</strong> — <a href="/index" class="external">View status below</a>.`;
          container.insertBefore(msg, container.firstChild);
        })
        .catch((err) => {
          const msg = document.createElement('div');
          msg.className = 'message error';
          msg.textContent = err.body || err.message || 'Request failed';
          container.insertBefore(msg, container.firstChild);
        });
    });
    formPanel.appendChild(form);
    container.appendChild(formPanel);

    const statusPanel = document.createElement('div');
    statusPanel.className = 'panel';
    statusPanel.innerHTML = '<h2>Request status</h2>';
    const ids = getStoredIds();
    if (ids.length === 0) {
      statusPanel.appendChild(document.createElement('p')).className = 'empty';
      statusPanel.querySelector('.empty').textContent = 'No recent requests. Submit a request above.';
    } else {
      const table = document.createElement('table');
      table.innerHTML = `
        <thead><tr>
          <th>Request ID</th>
          <th>Status</th>
          <th>Games</th>
          <th>Error</th>
        </tr></thead>
        <tbody></tbody>
      `;
      const tbody = table.querySelector('tbody');
      for (const id of ids) {
        const row = requestStatuses[id] || { id, status: '…', gamesIndexed: 0, errorMessage: null };
        const tr = document.createElement('tr');
        const statusClass = (row.status || '').toLowerCase().replace(' ', '-');
        tr.innerHTML = `
          <td><a href="/index" class="external">${id}</a></td>
          <td class="status-${statusClass}">${row.status || '—'}</td>
          <td>${row.gamesIndexed ?? 0}</td>
          <td>${row.errorMessage || '—'}</td>
        `;
        tbody.appendChild(tr);
      }
      statusPanel.appendChild(table);
    }
    container.appendChild(statusPanel);
  }

  loadStatuses();
  render();
}
