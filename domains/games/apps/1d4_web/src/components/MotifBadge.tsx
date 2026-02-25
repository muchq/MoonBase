import type { OccurrenceRow } from '../types';

interface Props {
  label: string;
  occurrences?: OccurrenceRow[];
}

export default function MotifBadge({ label, occurrences }: Props) {
  const title =
    occurrences && occurrences.length > 0
      ? occurrences
          .map((o) => `Move ${o.moveNumber} (${o.side}): ${o.description}`)
          .join('\n')
      : undefined;

  return (
    <span className="motif-badge" title={title}>
      {label.replace(/_/g, ' ')}
    </span>
  );
}
