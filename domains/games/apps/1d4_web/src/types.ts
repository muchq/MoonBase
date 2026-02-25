export interface OccurrenceRow {
  moveNumber: number;
  side: 'white' | 'black';
  description: string;
  ply?: number; // populated in Phase 2 after backend adds it
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
  hasPin: boolean;
  hasCrossPin: boolean;
  hasFork: boolean;
  hasSkewer: boolean;
  hasDiscoveredAttack: boolean;
  hasDiscoveredCheck: boolean;
  hasCheck: boolean;
  hasCheckmate: boolean;
  hasPromotion: boolean;
  hasPromotionWithCheck: boolean;
  hasPromotionWithCheckmate: boolean;
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
