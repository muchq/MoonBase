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
  playedAt: string | number;
  indexedAt: string | number;
  numMoves: number;
  pgn?: string;
  occurrences?: Record<string, OccurrenceRow[]>;
}

/**
 * Whether a request's indexed games are still stored. Request rows outlive the
 * games they produced — the retention worker sweeps games and indexed periods
 * on a 7-day clock but never touches `indexing_requests` — so "COMPLETED, 325
 * games" keeps rendering long after querying it would return nothing.
 */
export interface DataAvailability {
  status: 'AVAILABLE' | 'PARTIAL' | 'EXPIRED' | 'UNKNOWN';
  monthsAvailable: number;
  monthsTotal: number;
  /** Epoch seconds; null once nothing is left to expire. */
  expiresAt: number | null;
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
