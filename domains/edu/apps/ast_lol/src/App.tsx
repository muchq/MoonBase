import { lazy, Suspense } from 'react';
import { Navigate, Route, Routes } from 'react-router';
import Header from './components/Header';
import ChallengeView from './views/ChallengeView';
import HomeView from './views/HomeView';
import LessonView from './views/LessonView';

const LabView = lazy(() => import('./views/LabView'));

export default function App() {
  return (
    <>
      <Header />
      <main className="main">
        <Routes>
          <Route path="/" element={<HomeView />} />
          <Route path="/lesson/:id" element={<LessonView />} />
          <Route path="/challenge/:id" element={<ChallengeView />} />
          <Route path="/lab" element={<Suspense fallback={<p>Loading pipeline…</p>}><LabView /></Suspense>} />
          <Route path="*" element={<Navigate to="/" replace />} />
        </Routes>
      </main>
    </>
  );
}
