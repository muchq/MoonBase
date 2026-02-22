/**
 * Router and app init — hash-based routing, no framework.
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
  const hash = window.location.hash.slice(1) || 'games';
  return hash.split('/')[0];
}

function setActiveNav(route) {
  document.querySelectorAll('.nav-link').forEach((a) => {
    a.classList.toggle('active', a.getAttribute('href') === `#${route}`);
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

window.addEventListener('hashchange', render);
window.addEventListener('load', render);
