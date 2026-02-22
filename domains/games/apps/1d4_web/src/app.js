/**
 * Router and app init — History API (path-based), no framework.
 */

import { renderGames } from './views/games.js';
import { renderIndex } from './views/index.js';
import { renderQuery } from './views/query.js';

const routes = {
  games: renderGames,
  index: renderIndex,
  query: renderQuery,
};

function getRoute() {
  const path = window.location.pathname.replace(/^\/+/, '') || 'games';
  return path.split('/')[0];
}

function setActiveNav(route) {
  const pathname = window.location.pathname.replace(/\/$/, '') || '/';
  document.querySelectorAll('.nav-link').forEach((a) => {
    const href = (a.getAttribute('href') || '/').replace(/\/$/, '') || '/';
    const isGamesPage = pathname === '/' || pathname === '/games';
    const active = (href === '/games' && isGamesPage) || (href !== '/' && href === pathname);
    a.classList.toggle('active', active);
  });
}

function render() {
  const route = getRoute();
  const allowedRoutes = ['games', 'index', 'query'];
  const safeRoute = allowedRoutes.includes(route) ? route : 'games';
  setActiveNav(safeRoute);
  const main = document.getElementById('app');
  main.innerHTML = '';
  const view = document.createElement('div');
  view.className = 'view active';
  view.id = `view-${safeRoute}`;
  main.appendChild(view);
  let fn = routes[safeRoute] || routes.games;
  if (typeof fn !== 'function') {
    fn = routes.games;
  }
  fn(view);
}

function navigate(path) {
  if (window.location.pathname !== path) {
    history.pushState(null, '', path);
  }
  render();
}

document.addEventListener('click', (e) => {
  const a = e.target.closest('a');
  if (!a || a.target === '_blank' || a.hasAttribute('download')) return;
  const href = a.getAttribute('href');
  if (!href || href.startsWith('#') || !href.startsWith('/')) return;
  try {
    const url = new URL(href, window.location.origin);
    if (url.origin !== window.location.origin) return;
  } catch {
    return;
  }
  e.preventDefault();
  navigate(a.getAttribute('href'));
});

window.addEventListener('popstate', render);
window.addEventListener('load', render);
