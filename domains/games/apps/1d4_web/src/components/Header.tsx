import { NavLink } from 'react-router-dom';

export default function Header() {
  return (
    <header className="header">
      <h1>
        <a href="/">1d4</a>
      </h1>
      <p className="tagline">Chess game indexer</p>
      <nav className="nav">
        <NavLink
          to="/games"
          className={({ isActive }) => `nav-link${isActive ? ' active' : ''}`}
        >
          Games
        </NavLink>
        <NavLink
          to="/index"
          className={({ isActive }) => `nav-link${isActive ? ' active' : ''}`}
        >
          Index
        </NavLink>
        <NavLink
          to="/query"
          className={({ isActive }) => `nav-link${isActive ? ' active' : ''}`}
        >
          Query
        </NavLink>
      </nav>
    </header>
  );
}
