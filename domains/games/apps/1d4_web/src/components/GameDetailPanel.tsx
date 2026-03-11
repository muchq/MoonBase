import { useState, useMemo, useRef, useEffect, type CSSProperties } from 'react';
import { useQuery } from '@tanstack/react-query';
import { Chess, type Move } from 'chess.js';
import { Chessboard } from 'react-chessboard';
import { getGameDetail } from '../api';
import type { GameRow, OccurrenceRow } from '../types';

const MOTIF_COLORS: Record<string, string> = {
  attack: '#f6c90e',
  fork: '#f6c90e',
  pin: '#e84393',
  skewer: '#e84393',
  cross_pin: '#e84393',
  discovered_attack: '#66b2ff',
  discovered_check: '#66b2ff',
  check: '#ff4444',
  checkmate: '#ff4444',
  double_check: '#ff4444',
  back_rank_mate: '#ff4444',
  smothered_mate: '#ff4444',
  promotion: '#44cc44',
  promotion_with_check: '#44cc44',
  promotion_with_checkmate: '#44cc44',

};

const START_FEN = 'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1';

function parsePgn(pgn: string): { fens: string[]; moves: Move[] } {
  const chess = new Chess();
  chess.loadPgn(pgn);
  const sans = chess.history();
  chess.reset();
  const fens: string[] = [chess.fen()];
  const moves: Move[] = [];
  for (const san of sans) {
    const move = chess.move(san);
    moves.push(move);
    fens.push(chess.fen());
  }
  return { fens, moves };
}

// 0-indexed half-move index of the move that produced the motif.
// fens[ply + 1] is the resulting board position to display.
function occurrencePly(occ: OccurrenceRow): number {
  return (occ.moveNumber - 1) * 2 + (occ.side === 'black' ? 1 : 0);
}

function formatMoveLabel(occ: OccurrenceRow): string {
  return occ.side === 'white' ? `${occ.moveNumber}.` : `${occ.moveNumber}...`;
}

function formatDate(val: string | number | null | undefined): string {
  if (val == null) return '';
  const date = typeof val === 'number' ? new Date(val * 1000) : new Date(val);
  return isNaN(date.getTime()) ? '' : date.toISOString().slice(0, 10);
}

interface Props {
  game: GameRow;
  onClose: () => void;
}

const needsDetail = (g: GameRow): boolean =>
  g.gameUrl != null && (g.pgn === undefined || g.occurrences === undefined);

export default function GameDetailPanel({ game, onClose }: Props) {
  const [currentPly, setCurrentPly] = useState(0);
  const [orientation, setOrientation] = useState<'white' | 'black'>('white');
  const [activeOccurrence, setActiveOccurrence] = useState<OccurrenceRow | null>(null);
  const motifListRef = useRef<HTMLUListElement>(null);

  const { data: detailGame, isLoading: detailLoading } = useQuery({
    queryKey: ['gameDetail', game.gameUrl],
    queryFn: () => getGameDetail(game.gameUrl!),
    enabled: needsDetail(game),
  });

  const displayGame = detailGame ?? game;

  const { fens, moves } = useMemo(
    () =>
      displayGame.pgn
        ? parsePgn(displayGame.pgn)
        : { fens: [START_FEN], moves: [] },
    [displayGame.pgn]
  );

  const totalPlies = moves.length;
  const fen = fens[currentPly] ?? fens[fens.length - 1];
  const lastMove = currentPly > 0 ? moves[currentPly - 1] : null;

  // Flatten and sort all occurrences by ply for ordered navigation and display
  const sortedOccurrences = useMemo(() => {
    return Object.entries(displayGame.occurrences ?? {}).flatMap(
      ([motif, occs]: [string, OccurrenceRow[]]) =>
        occs.map((occ: OccurrenceRow) => ({ motif, occ }))
    ).sort((a, b) => occurrencePly(a.occ) - occurrencePly(b.occ));
  }, [displayGame.occurrences]);

  // Group occurrences by ply so the list shows one row per move
  const groupedOccurrences = useMemo(() => {
    const groups: {
      ply: number;
      moveLabel: string;
      items: { motif: string; occ: OccurrenceRow }[];
    }[] = [];
    for (const item of sortedOccurrences) {
      const ply = occurrencePly(item.occ);
      const last = groups[groups.length - 1];
      if (last && last.ply === ply) {
        last.items.push(item);
      } else {
        groups.push({ ply, moveLabel: formatMoveLabel(item.occ), items: [item] });
      }
    }
    return groups;
  }, [sortedOccurrences]);

  const activeIndex = activeOccurrence
    ? sortedOccurrences.findIndex(({ occ }) => occ === activeOccurrence)
    : -1;

  const activeMotifKey = activeIndex >= 0 ? sortedOccurrences[activeIndex].motif : null;
  const motifColor = activeMotifKey != null ? (MOTIF_COLORS[activeMotifKey] ?? null) : null;

  const squareStyles: Record<string, CSSProperties> = {};
  if (lastMove) {
    squareStyles[lastMove.from] = {
      backgroundColor: motifColor ? motifColor + '66' : 'rgba(255,255,0,0.3)',
    };
    squareStyles[lastMove.to] = {
      backgroundColor: motifColor ? motifColor + 'aa' : 'rgba(255,255,0,0.55)',
    };
  }

  // Auto-scroll the active motif into view in the list
  useEffect(() => {
    if (!activeOccurrence || !motifListRef.current) return;
    const active = motifListRef.current.querySelector('[data-active="true"]') as HTMLElement;
    active?.scrollIntoView?.({ block: 'nearest', behavior: 'smooth' });
  }, [activeOccurrence]);

  function seekTo(ply: number) {
    setCurrentPly(Math.max(0, Math.min(ply, totalPlies)));
  }

  function handleOccurrenceClick(occ: OccurrenceRow) {
    setActiveOccurrence(occ);
    seekTo(occurrencePly(occ) + 1);
  }

  function handlePrevMotif() {
    if (sortedOccurrences.length === 0) return;
    const idx = activeIndex <= 0 ? sortedOccurrences.length - 1 : activeIndex - 1;
    handleOccurrenceClick(sortedOccurrences[idx].occ);
  }

  function handleNextMotif() {
    if (sortedOccurrences.length === 0) return;
    const idx =
      activeIndex < 0 || activeIndex === sortedOccurrences.length - 1 ? 0 : activeIndex + 1;
    handleOccurrenceClick(sortedOccurrences[idx].occ);
  }

  const moveLabel =
    currentPly === 0
      ? 'Start'
      : currentPly === totalPlies
        ? 'End'
        : `Move ${Math.ceil(currentPly / 2)}${currentPly % 2 === 1 ? ' (W)' : ' (B)'}`;

  const motifNavLabel =
    activeIndex >= 0
      ? `${activeIndex + 1} / ${sortedOccurrences.length}`
      : `${sortedOccurrences.length} motif${sortedOccurrences.length !== 1 ? 's' : ''}`;

  // Board is min(400px, 90vw) square; motif list matches that height + controls
  const motifListMaxHeight = 'min(440px, 65vh)';

  if (needsDetail(game) && detailLoading) {
    return (
      <div className="panel" style={{ marginTop: '1rem' }}>
        <div
          style={{
            display: 'flex',
            justifyContent: 'space-between',
            alignItems: 'flex-start',
          }}
        >
          <div>
            <strong>
              {game.whiteUsername} vs {game.blackUsername}
            </strong>
            <span className="text-muted" style={{ marginLeft: '0.75rem', fontSize: '0.875rem' }}>
              {game.result} · {game.timeClass} · {game.eco}
            </span>
          </div>
          <button
            type="button"
            className="btn"
            onClick={onClose}
            aria-label="Close panel"
            style={{ padding: '0 0.5rem' }}
          >
            ×
          </button>
        </div>
        <div className="loading" style={{ marginTop: '0.75rem' }}>
          Loading game…
        </div>
      </div>
    );
  }

  return (
    <div className="panel" style={{ marginTop: '1rem' }}>
      <div
        style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'flex-start' }}
      >
        <div>
          <strong>
            {displayGame.whiteUsername} vs {displayGame.blackUsername}
          </strong>
          {displayGame.playedAt && (
            <span className="text-muted" style={{ marginLeft: '0.75rem', fontSize: '0.875rem' }}>
              {formatDate(displayGame.playedAt)}
            </span>
          )}
          <span className="text-muted" style={{ marginLeft: '0.75rem', fontSize: '0.875rem' }}>
            {displayGame.result} · {displayGame.timeClass} · {displayGame.eco}
          </span>
        </div>
        <button
          type="button"
          className="btn"
          onClick={onClose}
          aria-label="Close panel"
          style={{ padding: '0 0.5rem' }}
        >
          ×
        </button>
      </div>

      {!displayGame.pgn && (
        <p className="text-muted" style={{ marginTop: '0.75rem' }}>
          PGN not available for this game.
        </p>
      )}

      {displayGame.pgn && (
        <div
          style={{
            display: 'flex',
            gap: '1.5rem',
            marginTop: '1rem',
            flexWrap: 'wrap',
            alignItems: 'flex-start',
          }}
        >
          {/* Board */}
          <div style={{ flex: '0 0 auto' }}>
            <div style={{ width: 'min(400px, 90vw)' }}>
              <Chessboard
                options={{
                  position: fen,
                  boardOrientation: orientation,
                  squareStyles: squareStyles,
                  allowDragging: false,
                }}
              />
            </div>
            {/* Move navigation */}
            <div
              style={{
                display: 'flex',
                gap: '0.4rem',
                marginTop: '0.5rem',
                alignItems: 'center',
              }}
            >
              <button
                type="button"
                className="btn"
                onClick={() => {
                  seekTo(0);
                  setActiveOccurrence(null);
                }}
                aria-label="Go to start"
              >
                ««
              </button>
              <button
                type="button"
                className="btn"
                onClick={() => seekTo(currentPly - 1)}
                disabled={currentPly === 0}
                aria-label="Previous move"
              >
                ‹
              </button>
              <span
                style={{ fontSize: '0.875rem', minWidth: '6.5rem', textAlign: 'center' }}
              >
                {moveLabel}
              </span>
              <button
                type="button"
                className="btn"
                onClick={() => seekTo(currentPly + 1)}
                disabled={currentPly === totalPlies}
                aria-label="Next move"
              >
                ›
              </button>
              <button
                type="button"
                className="btn"
                onClick={() => {
                  seekTo(totalPlies);
                  setActiveOccurrence(null);
                }}
                aria-label="Go to end"
              >
                »»
              </button>
              <button
                type="button"
                className="btn"
                onClick={() => setOrientation((o) => (o === 'white' ? 'black' : 'white'))}
                aria-label="Flip board"
                title="Flip board"
              >
                ⇅
              </button>
            </div>
            {/* Motif navigation */}
            {groupedOccurrences.length > 0 && (
              <div
                style={{
                  display: 'flex',
                  gap: '0.4rem',
                  marginTop: '0.4rem',
                  alignItems: 'center',
                }}
              >
                <button
                  type="button"
                  className="btn"
                  onClick={handlePrevMotif}
                  aria-label="Previous motif"
                  title="Previous motif"
                >
                  ‹ motif
                </button>
                <span
                  style={{ fontSize: '0.875rem', minWidth: '6.5rem', textAlign: 'center' }}
                >
                  {motifNavLabel}
                </span>
                <button
                  type="button"
                  className="btn"
                  onClick={handleNextMotif}
                  aria-label="Next motif"
                  title="Next motif"
                >
                  motif ›
                </button>
              </div>
            )}
          </div>

          {/* Occurrence list — one row per move, all motifs for that move as badges */}
          {groupedOccurrences.length > 0 && (
            <div style={{ flex: '1 1 200px', minWidth: 0 }}>
              <h3 style={{ margin: '0 0 0.5rem', fontSize: '1rem' }}>Motifs</h3>
              <ul
                ref={motifListRef}
                style={{
                  listStyle: 'none',
                  padding: 0,
                  margin: 0,
                  maxHeight: motifListMaxHeight,
                  overflowY: 'auto',
                }}
              >
                {groupedOccurrences.map(({ ply, moveLabel, items }) => {
                  const isGroupActive = items.some(({ occ }) => occ === activeOccurrence);
                  // Accent color: use the active badge's color, or the first badge's color
                  const accentMotif =
                    (items.find(({ occ }) => occ === activeOccurrence) ?? items[0]).motif;
                  const accentColor = MOTIF_COLORS[accentMotif] ?? '#aaa';
                  return (
                    <li
                      key={ply}
                      data-active={isGroupActive ? 'true' : 'false'}
                      onClick={() => handleOccurrenceClick(items[0].occ)}
                      style={{
                        cursor: 'pointer',
                        padding: '0.35rem 0.5rem',
                        borderRadius: '4px',
                        marginBottom: '0.25rem',
                        borderLeft: `3px solid ${accentColor}`,
                        backgroundColor: isGroupActive ? 'var(--bg-panel)' : 'transparent',
                        display: 'flex',
                        gap: '0.5rem',
                        alignItems: 'center',
                      }}
                    >
                      <span
                        style={{
                          fontSize: '0.8rem',
                          minWidth: '3rem',
                          color: 'var(--text-muted)',
                          flexShrink: 0,
                        }}
                      >
                        {moveLabel}
                      </span>
                      <div style={{ display: 'flex', flexWrap: 'wrap', gap: '0.3rem' }}>
                        {items.map(({ motif, occ }, j) => {
                          const color = MOTIF_COLORS[motif] ?? '#aaa';
                          const isBadgeActive = occ === activeOccurrence;
                          return (
                            <span
                              key={j}
                              className="badge"
                              onClick={(e) => {
                                e.stopPropagation();
                                handleOccurrenceClick(occ);
                              }}
                              style={{
                                backgroundColor: color,
                                color: '#000',
                                fontSize: '0.7rem',
                                cursor: 'pointer',
                                outline: isBadgeActive ? '2px solid var(--text)' : 'none',
                                outlineOffset: '1px',
                              }}
                            >
                              {motif.replace(/_/g, ' ')}
                            </span>
                          );
                        })}
                      </div>
                    </li>
                  );
                })}
              </ul>
            </div>
          )}
        </div>
      )}
    </div>
  );
}
