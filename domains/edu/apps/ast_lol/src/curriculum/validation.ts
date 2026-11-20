import { demoDb, type Database } from '../lang/sql';

/** Small, reproducible semantic fixtures. Cost budgets still use benchDb. */
export function seededDatabase(seed: number): Database {
  let state = seed >>> 0;
  const random = () => {
    state = (Math.imul(state, 1664525) + 1013904223) >>> 0;
    return state / 4294967296;
  };
  const pick = <T>(values: T[]): T => values[Math.floor(random() * values.length)];
  const db: Database = { users: [], orders: [], products: [] };
  for (let i = 0; i < 7; i++) {
    db.users.push({
      id: i + 1,
      name: `user${i}`,
      city: pick([null, 'seattle', 'london', 'berlin']),
      signup_year: pick([2019, 2021, 2024, null]),
    });
    db.products.push({
      id: i + 1,
      name: `product${i}`,
      category: pick(['hardware', 'software', null]),
      price: pick([0, 50, 300, null]),
    });
  }
  for (let i = 0; i < 18; i++)
    db.orders.push({
      id: i + 1,
      user_id: pick([1, 1, 2, 3, 7, null]),
      product_id: pick([1, 2, 3, 7, null]),
      quantity: pick([0, 1, 2, null]),
      total: pick([0, 20, 200, null]),
      year: pick([2023, 2024, 2025, null]),
    });
  // SQL bags preserve duplicate rows; no uniqueness constraints are assumed.
  db.users.push({ ...db.users[0] });
  db.orders.push({ ...db.orders[0] });
  return db;
}
export const semanticDatabases: { label: string; db: Database }[] = [
  { label: 'empty tables', db: { users: [], orders: [], products: [] } },
  { label: 'empty join side', db: { ...demoDb, orders: [] } },
  ...[7, 19, 41].map((seed) => ({
    label: `seed ${seed} (nulls, duplicates, skew)`,
    db: seededDatabase(seed),
  })),
];
