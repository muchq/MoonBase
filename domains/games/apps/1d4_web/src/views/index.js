/**
 * Enqueue index form + request status list. Submit calls POST api.1d4.net/v1/index.
 * Status table populated from GET api.1d4.net/v1/index (list of recent requests).
 */

import { createIndex, listIndexRequests } from '../api.js';

const POLL_INTERVAL_MS = 3000;

function normalizeMonth(value) {
  const m = value.match(/^(\d{4})-(\d{1,2})$/);
  if (!m) return value;
  const month = m[2].padStart(2, '0');
  if (parseInt(month, 10) > 12) return value;
  return `${m[1]}-${month}`;
}

export function renderIndex(container) {
  let requests = [];
  let pollTimer = null;

  function loadRequests() {
    return listIndexRequests()
      .then((res) => {
        requests = Array.isArray(res) ? res : [];
        render();
      })
      .catch(() => {
        render();
      });
  }

  function hasActive() {
    return requests.some((r) => ['PENDING', 'PROCESSING'].includes(r.status));
  }

  function startPolling() {
    if (pollTimer) return;
    if (!hasActive()) return;
    pollTimer = setInterval(() => {
      loadRequests();
    }, POLL_INTERVAL_MS);
  }

  function stopPolling() {
    if (pollTimer) {
      clearInterval(pollTimer);
      pollTimer = null;
    }
  }

  function render() {
    if (!hasActive()) stopPolling();
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
        <input id="startMonth" name="startMonth" type="text" placeholder="2024-03" required>
      </div>
      <div class="form-group">
        <label for="endMonth">End month (YYYY-MM)</label>
        <input id="endMonth" name="endMonth" type="text" placeholder="2024-03" required>
      </div>
      <button type="submit" class="btn">Submit</button>
    `;
    form.addEventListener('submit', (e) => {
      e.preventDefault();
      const fd = new FormData(form);
      const player = String(fd.get('player') ?? '').trim();
      const platform = String(fd.get('platform') ?? 'CHESS_COM').trim() || 'CHESS_COM';
      const startMonth = normalizeMonth(String(fd.get('startMonth') ?? '').trim());
      const endMonth = normalizeMonth(String(fd.get('endMonth') ?? '').trim());
      if (!player || !startMonth || !endMonth) {
        const msg = document.createElement('div');
        msg.className = 'message error';
        msg.textContent = 'Please fill in username and both months (YYYY-MM).';
        container.insertBefore(msg, container.firstChild);
        return;
      }
      createIndex({ player, platform, startMonth, endMonth })
        .then(() => {
          return loadRequests();
        })
        .then(() => {
          const msg = document.createElement('div');
          msg.className = 'message success';
          msg.textContent = 'Request created.';
          container.insertBefore(msg, container.firstChild);
        })
        .catch((err) => {
          const msg = document.createElement('div');
          msg.className = 'message error';
          let text = err.message || 'Request failed';
          if (err.body) {
            try {
              const parsed = JSON.parse(err.body);
              text = parsed.message ?? parsed.error ?? err.body;
            } catch {
              text = err.body;
            }
          }
          msg.textContent = text;
          container.insertBefore(msg, container.firstChild);
        });
    });
    formPanel.appendChild(form);
    container.appendChild(formPanel);

    const statusPanel = document.createElement('div');
    statusPanel.className = 'panel';
    statusPanel.innerHTML = '<h2>Request status</h2>';
    if (requests.length === 0) {
      const p = document.createElement('p');
      p.className = 'empty';
      p.textContent = 'No recent requests. Submit a request above.';
      statusPanel.appendChild(p);
    } else {
      const table = document.createElement('table');
      table.innerHTML = `
        <thead><tr>
          <th>Player</th>
          <th>Months</th>
          <th>Status</th>
          <th>Games</th>
          <th>Error</th>
        </tr></thead>
        <tbody></tbody>
      `;
      const tbody = table.querySelector('tbody');
      for (const row of requests) {
        const tr = document.createElement('tr');
        const statusClass = (row.status || '').toLowerCase().replace(' ', '-');
        tr.innerHTML = `
          <td>${row.player}</td>
          <td>${row.startMonth} – ${row.endMonth}</td>
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

  loadRequests();
}
