import { useState } from 'react';
import { useQuery } from '@tanstack/react-query';
import { getServerInfo } from '../api';

const POLL_INTERVAL_MS = 5 * 60 * 1000; // 5 minutes

export default function ServerInfoBanner() {
  const [dismissed, setDismissed] = useState(false);

  const { data } = useQuery({
    queryKey: ['server-info'],
    queryFn: getServerInfo,
    refetchInterval: POLL_INTERVAL_MS,
  });

  const windows = data?.maintenanceWindows ?? [];

  if (dismissed || windows.length === 0) {
    return null;
  }

  return (
    <div className="server-info-banner" role="status" aria-live="polite">
      <div className="server-info-banner__content">
        <span className="server-info-banner__icon">⚠</span>
        <div className="server-info-banner__messages">
          {windows.map((w, i) => (
            <p key={i} className="server-info-banner__message">
              {w.message}
            </p>
          ))}
        </div>
      </div>
      <button
        type="button"
        className="server-info-banner__dismiss"
        aria-label="Dismiss notification"
        onClick={() => setDismissed(true)}
      >
        ✕
      </button>
    </div>
  );
}
