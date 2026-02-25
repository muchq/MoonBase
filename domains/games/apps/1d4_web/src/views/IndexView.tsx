import { useState } from 'react';
import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query';
import { listIndexRequests, createIndex } from '../api';
import type { IndexRequest } from '../types';

function normalizeMonth(value: string): string {
  const m = value.match(/^(\d{4})-(\d{1,2})$/);
  if (!m) return value;
  const month = m[2].padStart(2, '0');
  if (parseInt(month, 10) > 12) return value;
  return `${m[1]}-${month}`;
}

export default function IndexView() {
  const queryClient = useQueryClient();
  const [message, setMessage] = useState<{
    text: string;
    type: 'error' | 'success';
  } | null>(null);
  const [player, setPlayer] = useState('');
  const [platform, setPlatform] = useState('CHESS_COM');
  const [startMonth, setStartMonth] = useState('');
  const [endMonth, setEndMonth] = useState('');

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
    const sm = normalizeMonth(startMonth.trim());
    const em = normalizeMonth(endMonth.trim());
    if (!p || !sm || !em) {
      setMessage({
        text: 'Please fill in username and both months (YYYY-MM).',
        type: 'error',
      });
      return;
    }
    setMessage(null);
    mutation.mutate({ player: p, platform, startMonth: sm, endMonth: em });
  }

  return (
    <>
      {message && (
        <div className={`message ${message.type}`}>{message.text}</div>
      )}

      <div className="panel">
        <h2>Enqueue index request</h2>
        <form onSubmit={handleSubmit}>
          <div className="form-group">
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
          <div className="form-group">
            <label htmlFor="platform">Platform</label>
            <select
              id="platform"
              value={platform}
              onChange={(e) => setPlatform(e.target.value)}
            >
              <option value="CHESS_COM">chess.com</option>
            </select>
          </div>
          <div className="form-group">
            <label htmlFor="startMonth">Start month (YYYY-MM)</label>
            <input
              id="startMonth"
              type="text"
              placeholder="2024-03"
              value={startMonth}
              onChange={(e) => setStartMonth(e.target.value)}
              required
            />
          </div>
          <div className="form-group">
            <label htmlFor="endMonth">End month (YYYY-MM)</label>
            <input
              id="endMonth"
              type="text"
              placeholder="2024-03"
              value={endMonth}
              onChange={(e) => setEndMonth(e.target.value)}
              required
            />
          </div>
          <button type="submit" className="btn" disabled={mutation.isPending}>
            Submit
          </button>
        </form>
      </div>

      <div className="panel">
        <h2>Request status</h2>
        {requests.length === 0 ? (
          <p className="empty">No recent requests. Submit a request above.</p>
        ) : (
          <table>
            <thead>
              <tr>
                <th>Player</th>
                <th>Months</th>
                <th>Status</th>
                <th>Games</th>
                <th>Error</th>
              </tr>
            </thead>
            <tbody>
              {requests.map((row) => (
                <tr key={row.id}>
                  <td>{row.player}</td>
                  <td>
                    {row.startMonth} – {row.endMonth}
                  </td>
                  <td
                    className={`status-${(row.status || '').toLowerCase()}`}
                  >
                    {row.status || '—'}
                  </td>
                  <td>{row.gamesIndexed ?? 0}</td>
                  <td>{row.errorMessage || '—'}</td>
                </tr>
              ))}
            </tbody>
          </table>
        )}
      </div>
    </>
  );
}
