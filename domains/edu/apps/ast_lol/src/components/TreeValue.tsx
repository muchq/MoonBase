import { show } from '../grader/deepEqual';

/** A keyboard-accessible structural view; raw JSON remains available beside it. */
export default function TreeValue({ value, name = 'root', depth = 0 }: { value: unknown; name?: string; depth?: number }) {
  if (typeof value !== 'object' || value === null) return <div className="tree-leaf"><span>{name}</span> <code>{show(value)}</code></div>;
  if (depth > 16) return <pre>{JSON.stringify(value, null, 2)}</pre>;
  const entries = Object.entries(value);
  const type = !Array.isArray(value) && 'type' in value ? String(value.type) : Array.isArray(value) ? `${entries.length} items` : `${entries.length} fields`;
  return <details className="tree-node" open={depth < 3}>
    <summary><span>{name}</span> <strong>{type}</strong></summary>
    <div>{entries.map(([key, child]) => <TreeValue key={key} name={key} value={child} depth={depth + 1} />)}</div>
  </details>;
}
