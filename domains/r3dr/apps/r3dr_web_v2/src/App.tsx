import { useState } from 'react';
import ShortenCard from './components/ShortenCard';
import RecentLinks from './components/RecentLinks';
import { addRecent, clearRecent, loadRecent, type RecentLink } from './recent';

export default function App() {
  const [recent, setRecent] = useState<RecentLink[]>(() => loadRecent(Date.now()));

  return (
    <div className="shell">
      <header className="hero">
        <h1 className="wordmark">
          r<span className="wordmark-blossom">3</span>dr
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
        Links live at <code>r3dr.net/r/&#123;slug&#125;</code> and always expire — 30 days max.
      </footer>
    </div>
  );
}
