import type { Catalog, Database } from './types';

/**
 * The course's sample data. `demoDb` is small enough to reason about by hand
 * and appears in lessons and visible test cases; `benchDb` is a larger,
 * deterministically generated set the optimizer challenges use for cost
 * grading. Both share one catalog.
 */

export const shopCatalog: Catalog = {
  users: ['id', 'name', 'city', 'signup_year'],
  orders: ['id', 'user_id', 'product_id', 'quantity', 'total', 'year'],
  products: ['id', 'name', 'category', 'price'],
};

export const demoDb: Database = {
  users: [
    { id: 1, name: 'ada', city: 'london', signup_year: 2019 },
    { id: 2, name: 'grace', city: 'seattle', signup_year: 2021 },
    { id: 3, name: 'alan', city: 'london', signup_year: 2022 },
    { id: 4, name: 'edsger', city: null, signup_year: 2020 },
    { id: 5, name: 'barbara', city: 'boston', signup_year: 2023 },
    { id: 6, name: 'donald', city: 'palo alto', signup_year: 2018 },
  ],
  products: [
    { id: 1, name: 'keyboard', category: 'hardware', price: 80 },
    { id: 2, name: 'monitor', category: 'hardware', price: 240 },
    { id: 3, name: 'compiler license', category: 'software', price: 500 },
    { id: 4, name: 'coffee', category: 'consumable', price: 12 },
    { id: 5, name: 'desk', category: 'furniture', price: 320 },
  ],
  orders: [
    { id: 1, user_id: 1, product_id: 2, quantity: 1, total: 240, year: 2023 },
    { id: 2, user_id: 2, product_id: 4, quantity: 10, total: 120, year: 2024 },
    { id: 3, user_id: 2, product_id: 3, quantity: 1, total: 500, year: 2024 },
    { id: 4, user_id: 3, product_id: 1, quantity: 2, total: 160, year: 2023 },
    { id: 5, user_id: 1, product_id: 4, quantity: 5, total: 60, year: 2025 },
    { id: 6, user_id: 5, product_id: 5, quantity: 1, total: 320, year: 2024 },
    { id: 7, user_id: 4, product_id: 2, quantity: 2, total: null, year: 2025 },
    { id: 8, user_id: 6, product_id: 3, quantity: 1, total: 500, year: 2022 },
    { id: 9, user_id: 2, product_id: 1, quantity: 1, total: 80, year: 2025 },
    { id: 10, user_id: 5, product_id: 4, quantity: 20, total: 240, year: 2025 },
  ],
};

/**
 * Deterministic 32-bit generator (mulberry32) so benchDb is identical on
 * every load, in every engine — cost budgets in challenge test names
 * depend on that. Mulberry32 rather than a raw LCG: an LCG's low bit
 * strictly alternates, and every record here draws a fixed number of
 * values, so `% n` with even n would silently confine whole columns to
 * half their domain.
 */
function makeRng(seed: number): () => number {
  let state = seed >>> 0;
  return () => {
    state = (state + 0x6d2b79f5) >>> 0;
    let t = Math.imul(state ^ (state >>> 15), state | 1);
    t = (t + Math.imul(t ^ (t >>> 7), t | 61)) ^ t;
    return (t ^ (t >>> 14)) >>> 0;
  };
}

function buildBenchDb(): Database {
  const rng = makeRng(0xa57_101);
  const pick = <T>(xs: readonly T[]): T => xs[rng() % xs.length];
  const int = (lo: number, hi: number): number => lo + (rng() % (hi - lo + 1));

  const cities = ['london', 'seattle', 'boston', 'tokyo', 'berlin', 'portland', null] as const;
  const firstNames = [
    'ada', 'grace', 'alan', 'edsger', 'barbara', 'donald', 'john', 'leslie',
    'tony', 'niklaus', 'frances', 'margaret',
  ] as const;
  const categories = ['hardware', 'software', 'consumable', 'furniture'] as const;

  const users = Array.from({ length: 120 }, (_, i) => ({
    id: i + 1,
    name: `${pick(firstNames)}_${i + 1}`,
    city: pick(cities),
    signup_year: int(2015, 2025),
  }));
  const products = Array.from({ length: 30 }, (_, i) => ({
    id: i + 1,
    name: `product_${i + 1}`,
    category: pick(categories),
    price: int(5, 600),
  }));
  const orders = Array.from({ length: 600 }, (_, i) => {
    const quantity = int(1, 20);
    const product_id = int(1, products.length);
    return {
      id: i + 1,
      user_id: int(1, users.length),
      product_id,
      quantity,
      // A few totals are null, so 3VL shows up in bench results too.
      total: rng() % 19 === 0 ? null : quantity * products[product_id - 1].price,
      year: int(2020, 2025),
    };
  });
  return { users, products, orders };
}

export const benchDb: Database = buildBenchDb();
