/**
 * Motif badge rendering — pin, fork, skewer, etc.
 */

const MOTIF_KEYS = [
  ['hasPin', 'pin'],
  ['hasCrossPin', 'cross_pin'],
  ['hasFork', 'fork'],
  ['hasSkewer', 'skewer'],
  ['hasDiscoveredAttack', 'discovered_attack'],
];

/**
 * @param {import('./table.js').GameRow} game
 * @returns {string[]} — list of motif labels present in the game
 */
export function getMotifs(game) {
  if (!game) return [];
  return MOTIF_KEYS.filter(([key]) => game[key]).map(([, label]) => label);
}

/**
 * @param {import('./table.js').GameRow} game
 * @returns {HTMLSpanElement}
 */
export function renderMotifs(game) {
  const span = document.createElement('span');
  span.className = 'motifs';
  for (const label of getMotifs(game)) {
    const badge = document.createElement('span');
    badge.className = 'motif-badge';
    badge.textContent = label.replace(/_/g, ' ');
    span.appendChild(badge);
  }
  return span;
}
