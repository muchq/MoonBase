import { Routes, Route, Navigate } from 'react-router';
import Header from './components/Header';
import GamesView from './views/GamesView';
import IndexView from './views/IndexView';
import QueryView from './views/QueryView';
import McpView from './views/McpView';

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
          <Route path="/mcp" element={<McpView />} />
          <Route path="*" element={<Navigate to="/games" replace />} />
        </Routes>
      </main>
    </>
  );
}
