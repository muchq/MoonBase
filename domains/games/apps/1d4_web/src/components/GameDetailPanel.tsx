import { useState, useMemo } from 'react';
import { Chess, type Move } from 'chess.js';
import { Chessboard } from 'react-chessboard';
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
  sacrifice: '#ff9900',
  interference: '#cc66ff',
  overloaded_piece: '#cc66ff',
  zugzwang: '#aaaaaa',
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

export default function GameDetailPanel({ game, onClose }: Props) {
  const [currentPly, setCurrentPly] = useState(0);
  const [orientation, setOrientation] = useState<'white' | 'black'>('white');
  const [activeOccurrence, setActiveOccurrence] = useState<OccurrenceRow | null>(null);

  const { fens, moves } = useMemo(
    () => (game.pgn ? parsePgn(game.pgn) : { fens: [START_FEN], moves: [] }),
    [game.pgn]
  );

  const totalPlies = moves.length;
  const fen = fens[currentPly] ?? fens[fens.length - 1];
  const lastMove = currentPly > 0 ? moves[currentPly - 1] : null;

  const activeMotifKey = activeOccurrence
    ? Object.entries(game.occurrences ?? {}).find(([, occs]) =>
        occs.includes(activeOccurrence)
      )?.[0]
    : null;
  const motifColor = activeMotifKey != null ? (MOTIF_COLORS[activeMotifKey] ?? null) : null;

  const squareStyles: Record<string, React.CSSProperties> = {};
  if (lastMove) {
    squareStyles[lastMove.from] = {
      backgroundColor: motifColor ? motifColor + '66' : 'rgba(255,255,0,0.3)',
    };
    squareStyles[lastMove.to] = {
      backgroundColor: motifColor ? motifColor + 'aa' : 'rgba(255,255,0,0.55)',
    };
  }

  function seekTo(ply: number) {
    setCurrentPly(Math.max(0, Math.min(ply, totalPlies)));
  }

  function handleOccurrenceClick(occ: OccurrenceRow) {
    setActiveOccurrence(occ);
    seekTo(occurrencePly(occ) + 1);
  }

  const moveLabel =
    currentPly === 0
      ? 'Start'
      : currentPly === totalPlies
        ? 'End'
        : `Move ${Math.ceil(currentPly / 2)}${currentPly % 2 === 1 ? ' (W)' : ' (B)'}`;

  const occurrenceEntries = Object.entries(game.occurrences ?? {});

  return (
    <div className="panel" style={{ marginTop: '1rem' }}>
      <div
        style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'flex-start' }}
      >
        <div>
          <strong>
            {game.whiteUsername} vs {game.blackUsername}
          </strong>
          {game.playedAt && (
            <span className="text-muted" style={{ marginLeft: '0.75rem', fontSize: '0.875rem' }}>
              {formatDate(game.playedAt)}
            </span>
          )}
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

      {!game.pgn && (
        <p className="text-muted" style={{ marginTop: '0.75rem' }}>
          PGN not available for this game.
        </p>
      )}

      {game.pgn && (
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
                position={fen}
                boardOrientation={orientation}
                customSquareStyles={squareStyles}
                arePiecesDraggable={false}
              />
            </div>
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
          </div>

          {/* Occurrence list */}
          {occurrenceEntries.length > 0 && (
            <div style={{ flex: '1 1 200px' }}>
              <h3 style={{ margin: '0 0 0.5rem', fontSize: '1rem' }}>Motifs</h3>
              <ul style={{ listStyle: 'none', padding: 0, margin: 0 }}>
                {occurrenceEntries.flatMap(([motif, occs]) =>
                  occs.map((occ, i) => {
                    const color = MOTIF_COLORS[motif] ?? '#aaa';
                    const isActive = activeOccurrence === occ;
                    return (
                      <li
                        key={`${motif}-${i}`}
                        onClick={() => handleOccurrenceClick(occ)}
                        style={{
                          cursor: 'pointer',
                          padding: '0.35rem 0.5rem',
                          borderRadius: '4px',
                          marginBottom: '0.25rem',
                          borderLeft: `3px solid ${color}`,
                          backgroundColor: isActive ? 'var(--bg-panel)' : 'transparent',
                          display: 'flex',
                          gap: '0.5rem',
                          alignItems: 'baseline',
                        }}
                      >
                        <span
                          className="badge"
                          style={{
                            backgroundColor: color,
                            color: '#000',
                            fontSize: '0.7rem',
                          }}
                        >
                          {motif.replace(/_/g, ' ')}
                        </span>
                        <span style={{ fontSize: '0.875rem' }}>{formatMoveLabel(occ)}</span>
                        {occ.description && (
                          <span className="text-muted" style={{ fontSize: '0.8rem' }}>
                            {occ.description}
                          </span>
                        )}
                      </li>
                    );
                  })
                )}
              </ul>
            </div>
          )}
        </div>
      )}
    </div>
  );
}
