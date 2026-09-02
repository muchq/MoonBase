export interface OccurrenceRow {
  gameUrl: string;
  motif: string;
  moveNumber: number;
  side: 'white' | 'black';
  description: string;
  movedPiece?: string | null;
  attacker?: string | null;
  target?: string | null;
  isDiscovered?: boolean | null;
  isMate?: boolean | null;
  pinType?: string | null;
}

export interface GameRow {
  gameUrl: string;
  platform: string;
  whiteUsername: string;
  blackUsername: string;
  whiteElo: number | null;
  blackElo: number | null;
  timeClass: string;
  eco: string;
  result: string;
  // Nullable: a game whose archive entry carried no end time is stored with no played_at, and a
  // negated date filter returns it (#1302). The API omits the key entirely in that case.
  playedAt: string | number | null;
  indexedAt: string | number;
  numMoves: number;
  pgn?: string;
  occurrences?: Record<string, OccurrenceRow[]>;
}

/**
 * Whether a request's indexed games are still stored. Request rows outlive the
 * games they produced — the retention worker sweeps games and indexed periods
 * on a 7-day clock and the request rows themselves on a 30-day one — so
 * "COMPLETED, 325 games" keeps rendering long after querying it would return
 * nothing. The gap between the two windows is where this field earns its keep:
 * the request is still there to say EXPIRED rather than having vanished.
 */
export interface DataAvailability {
  status: 'AVAILABLE' | 'PARTIAL' | 'EXPIRED' | 'UNKNOWN';
  monthsAvailable: number;
  monthsTotal: number;
  /** Epoch seconds. Absent (not null) once nothing is left to expire — the API omits nulls. */
  expiresAt?: number;
}

export interface IndexRequest {
  id: string;
  player: string;
  platform: string;
  startMonth: string;
  endMonth: string;
  status: string;
  gamesIndexed: number;
  errorMessage: string | null;
  excludeBullet: boolean;
  /** Absent until the request COMPLETEs. */
  data?: DataAvailability | null;
}

export interface QueryResponse {
  games: GameRow[];
  count: number;
}

// Query stats (MoonBase#1465), from the stats service's one_d4 endpoints,
// routed through api.1d4.net. One row per day, entry point, source, outcome,
// and cache disposition; counts only.
export interface QueryStatRow {
  date: string;
  entry: string;
  source: string;
  outcome: string;
  cache: string;
  requests: number;
}

export interface QueryStats {
  days: number;
  rows: QueryStatRow[];
}

// Which fields, motifs, order-by motifs, and group-by columns queries used
// over the window, busiest first.
export interface QueryTermRow {
  entry: string;
  kind: string;
  term: string;
  requests: number;
}

export interface QueryTerms {
  days: number;
  rows: QueryTermRow[];
}
