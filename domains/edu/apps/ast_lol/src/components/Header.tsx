import { useEffect } from 'react';
import { Link } from 'react-router';
import { challenges } from '../curriculum/registry';
import { setTheme, useProgress } from '../state/progress';

export default function Header() {
  const progress = useProgress();
  const done = challenges.filter(c => progress.completed[c.id] !== undefined).length;
  useEffect(() => { document.documentElement.dataset.theme = progress.theme; }, [progress.theme]);
  return <header className="header">
    <h1><Link to="/">ast.lol</Link></h1>
    <p className="tagline">parse. transform. optimize.</p>
    <Link className="lab-link" to="/lab">Explore the pipeline</Link>
    <span className="spacer" />
    <span className="progress-chip" title="challenges with a passing run">{done}/{challenges.length} solved</span>
    <label className="theme-control">Theme
      <select aria-label="Theme" value={progress.theme} onChange={e => setTheme(e.target.value as 'system' | 'light' | 'dark')}>
        <option value="system">System</option><option value="light">Light</option><option value="dark">Dark</option>
      </select>
    </label>
  </header>;
}
