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
  hasPin: boolean;
  hasCrossPin: boolean;
  hasFork: boolean;
  hasSkewer: boolean;
  hasDiscoveredAttack: boolean;
  hasDiscoveredCheck: boolean;
  hasCheck: boolean;
  hasCheckmate: boolean;
  hasDoubleCheck: boolean;
  hasBackRankMate: boolean;
  hasSmotheredMate: boolean;
  hasPromotion: boolean;
  hasPromotionWithCheck: boolean;
  hasPromotionWithCheckmate: boolean;
  hasSacrifice: boolean;
  hasInterference: boolean;
  hasOverloadedPiece: boolean;
  hasZugzwang: boolean;
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
