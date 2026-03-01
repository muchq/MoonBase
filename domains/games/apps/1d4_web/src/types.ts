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

export interface IndexRequest {
  id: string;
  player: string;
  platform: string;
  startMonth: string;
  endMonth: string;
  status: string;
  gamesIndexed: number;
  errorMessage: string | null;
}

export interface QueryResponse {
  games: GameRow[];
  count: number;
}
