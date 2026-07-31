import { useState } from 'react';
import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query';
import { listIndexRequests, createIndex } from '../api';
import DataBadge from '../components/DataBadge';
import MonthPicker, { currentMonth, formatMonth } from '../components/MonthPicker';
import type { IndexRequest } from '../types';

/**
 * "Jul 2026" for a single month, "Jan – Mar 2026" within one year, otherwise
 * both years. The raw `2026-07 – 2026-07` wrapped to three lines on a phone.
 */
function formatMonthRange(row: IndexRequest): string {
  if (row.startMonth === row.endMonth) return formatMonth(row.startMonth);
  const start = formatMonth(row.startMonth);
  const end = formatMonth(row.endMonth);
  const sameYear = row.startMonth.slice(0, 4) === row.endMonth.slice(0, 4);
  return sameYear ? `${start.split(' ')[0]} – ${end}` : `${start} – ${end}`;
}

export default function IndexView() {
  const queryClient = useQueryClient();
  const [message, setMessage] = useState<{
    text: string;
    type: 'error' | 'success';
  } | null>(null);
  const [player, setPlayer] = useState('');
  const [platform, setPlatform] = useState('CHESS_COM');
  const [thisMonth] = useState(currentMonth);
  const [startMonth, setStartMonth] = useState(thisMonth);
  const [endMonth, setEndMonth] = useState(thisMonth);
  const [excludeBullet, setExcludeBullet] = useState(true);

  const { data: requests = [] } = useQuery<IndexRequest[]>({
    queryKey: ['indexRequests'],
    queryFn: listIndexRequests,
    refetchInterval: (query) =>
      (query.state.data as IndexRequest[] | undefined)?.some((r) =>
        ['PENDING', 'PROCESSING'].includes(r.status)
      )
        ? 3000
        : false,
  });

  const mutation = useMutation({
    mutationFn: (body: Parameters<typeof createIndex>[0]) => createIndex(body),
    onSuccess: () => {
      void queryClient.invalidateQueries({ queryKey: ['indexRequests'] });
      setMessage({ text: 'Request created.', type: 'success' });
    },
    onError: (err: Error & { body?: string | null }) => {
      let text = err.message || 'Request failed';
      if (err.body) {
        try {
          const parsed = JSON.parse(err.body) as Record<string, unknown>;
          text =
            (parsed.message as string) ??
            (parsed.error as string) ??
            err.body;
        } catch {
          text = err.body;
        }
      }
      setMessage({ text, type: 'error' });
    },
  });

  function handleSubmit(e: React.FormEvent) {
    e.preventDefault();
    const p = player.trim();
    if (!p) {
      setMessage({ text: 'Please enter a username.', type: 'error' });
      return;
    }
    setMessage(null);
    // Both months come from MonthPicker, so they are always canonical YYYY-MM
    // and start is never after end.
    mutation.mutate({
      player: p,
      platform,
      startMonth,
      endMonth,
      excludeBullet,
    });
  }

  function handleStartMonthChange(value: string) {
    setStartMonth(value);
    // Dragging start past end would leave an empty range; carry end along
    // rather than blocking the choice the user just made.
    if (value > endMonth) setEndMonth(value);
  }

  return (
    <>
      <div className="panel">
        <h2>Enqueue index request</h2>
        <form onSubmit={handleSubmit} className="enqueue-form">
          <div className="enqueue-form-row">
            <div className="form-group enqueue-username">
              <label htmlFor="player">Username</label>
              <input
                id="player"
                type="text"
                placeholder="e.g. hikaru"
                value={player}
                onChange={(e) => setPlayer(e.target.value)}
                required
              />
            </div>
            <div className="form-group enqueue-platform">
              <label htmlFor="platform">Platform</label>
              <select
                id="platform"
                value={platform}
                onChange={(e) => setPlatform(e.target.value)}
              >
                <option value="CHESS_COM">chess.com</option>
              </select>
            </div>
          </div>
          <div className="enqueue-form-row">
            <MonthPicker
              id="startMonth"
              label="Start month"
              value={startMonth}
              onChange={handleStartMonthChange}
              max={thisMonth}
              className="enqueue-month"
            />
            <MonthPicker
              id="endMonth"
              label="End month"
              value={endMonth}
              onChange={setEndMonth}
              min={startMonth}
              max={thisMonth}
              className="enqueue-month"
            />

            <label className="enqueue-checkbox">
              <input
                type="checkbox"
                checked={excludeBullet}
                onChange={(e) => setExcludeBullet(e.target.checked)}
              />
              Exclude bullet games
            </label>
            <div className="enqueue-submit">
              <button type="submit" className="btn" disabled={mutation.isPending}>
                {mutation.isPending ? 'Enqueueing…' : 'Enqueue'}
              </button>
            </div>
          </div>
        </form>
        {message && (
          <div className={`enqueue-message ${message.type}`}>{message.text}</div>
        )}
      </div>

      <div className="panel">
        <h2>Request status</h2>
        {requests.length === 0 ? (
          <p className="empty">No recent requests. Submit a request above.</p>
        ) : (
          <div className="table-wrap">
            <table className="request-status-table">
              <thead>
                <tr>
                  <th>Player</th>
                  <th>Months</th>
                  <th>Status</th>
                  <th>Games</th>
                  <th>Data</th>
                  <th>Error</th>
                </tr>
              </thead>
              <tbody>
                {/* data-label drives the stacked card layout under 640px, where
                    six columns cannot fit across and the header row is hidden. */}
                {requests.map((row) => (
                  <tr key={row.id}>
                    <td data-label="Player">{row.player}</td>
                    <td data-label="Months" className="nowrap">
                      {formatMonthRange(row)}
                    </td>
                    <td
                      data-label="Status"
                      className={`nowrap status-${(row.status || '').toLowerCase()}`}
                    >
                      {row.status || '—'}
                    </td>
                    <td data-label="Games">{row.gamesIndexed ?? 0}</td>
                    <td data-label="Data" className="nowrap">
                      <DataBadge data={row.data} />
                    </td>
                    <td
                      data-label="Error"
                      className={row.errorMessage ? '' : 'is-empty'}
                    >
                      {row.errorMessage || '—'}
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        )}
        {requests.length > 0 && (
          <p className="panel-note">
            Indexed games are kept for 7 days, then deleted. A request stays in this list
            after that, marked <strong>Pruned</strong> — re-run it to index the games again.
          </p>
        )}
      </div>
    </>
  );
}
