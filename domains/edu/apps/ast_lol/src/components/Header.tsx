import { Link } from 'react-router';
import { challenges } from '../curriculum/registry';
import { useProgress } from '../state/progress';

export default function Header() {
  const progress = useProgress();
  const done = challenges.filter((c) => progress.completed[c.id] !== undefined).length;
  return (
    <header className="header">
      <h1>
        <Link to="/">ast.lol</Link>
      </h1>
      <p className="tagline">parse. transform. optimize.</p>
      <span className="spacer" />
      <span className="progress-chip" title="challenges solved">
        {done}/{challenges.length} solved
      </span>
    </header>
  );
}
