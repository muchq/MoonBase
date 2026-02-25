import { Routes, Route, Navigate } from 'react-router-dom';
import Header from './components/Header';
import ServerInfoBanner from './components/ServerInfoBanner';
import GamesView from './views/GamesView';
import IndexView from './views/IndexView';
import QueryView from './views/QueryView';

export default function App() {
  return (
    <>
      <Header />
      <main className="main">
        <Routes>
          <Route path="/" element={<Navigate to="/games" replace />} />
          <Route path="/games" element={<GamesView />} />
          <Route path="/index" element={<IndexView />} />
          <Route path="/query" element={<QueryView />} />
          <Route path="*" element={<Navigate to="/games" replace />} />
        </Routes>
      </main>
      <ServerInfoBanner />
    </>
  );
}
