import { useEffect, useState } from 'react';
import ShortenCard from './components/ShortenCard';
import RecentLinks from './components/RecentLinks';
import { addRecent, clearRecent, loadRecent, type RecentLink } from './recent';

export default function App() {
  const [recent, setRecent] = useState<RecentLink[]>(() => loadRecent(Date.now()));

  // Re-render each minute so "expires in …" stays true in a tab left open,
  // and dead links drop out. In-memory prune, not a storage reload — the
  // list must survive when storage doesn't.
  useEffect(() => {
    const id = window.setInterval(
      () => setRecent((prev) => prev.filter((link) => link.expiresAt > Date.now())),
      60_000
    );
    return () => window.clearInterval(id);
  }, []);

  return (
    <div className="shell">
      <header className="hero">
        <h1 className="wordmark">
          ii<span className="wordmark-blossom">l</span>i
        </h1>
        <p className="tagline">Shorten a link. Share it anywhere. It expires on your schedule.</p>
      </header>
      <main className="content">
        <ShortenCard onMinted={(link) => setRecent((prev) => addRecent(prev, link))} />
        <RecentLinks
          links={recent}
          onClear={() => {
            clearRecent();
            setRecent([]);
          }}
        />
      </main>
      <footer className="footer">
        Links live at <code>i.iili.uk/r/&#123;slug&#125;</code> and always expire — 30 days max.
      </footer>
    </div>
  );
}
