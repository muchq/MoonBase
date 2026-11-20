import type { OccurrenceRow } from '../types';

interface Props {
  label: string;
  occurrences?: OccurrenceRow[];
}

export default function MotifBadge({ label, occurrences }: Props) {
  const name = label.replace(/_/g, ' ');
  // Led by the name: in the table a long badge is ellipsized.
  const title = [
    name,
    ...(occurrences ?? []).map(
      (o) => `Move ${o.moveNumber} (${o.side}): ${o.description}`
    ),
  ].join('\n');

  return (
    <span className="motif-badge" title={title}>
      {name}
    </span>
  );
}
