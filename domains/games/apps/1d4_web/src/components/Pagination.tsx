const DEFAULT_PAGE_SIZES = [10, 25, 50, 100];

interface Props {
  offset: number;
  limit: number;
  total: number;
  onLimitChange: (limit: number) => void;
  onPrev: () => void;
  onNext: () => void;
  pageSizes?: number[];
}

export default function Pagination({
  offset,
  limit,
  total,
  onLimitChange,
  onPrev,
  onNext,
  pageSizes = DEFAULT_PAGE_SIZES,
}: Props) {
  const start = total === 0 ? 0 : Math.min(offset + 1, total);
  const end = Math.min(offset + limit, total);

  return (
    <div className="pagination">
      <select
        value={limit}
        onChange={(e) => onLimitChange(Number(e.target.value))}
      >
        {pageSizes.map((n) => (
          <option key={n} value={n}>
            {n} per page
          </option>
        ))}
      </select>
      <button className="btn" disabled={offset === 0} onClick={onPrev}>
        Previous
      </button>
      <button
        className="btn"
        disabled={offset + limit >= total}
        onClick={onNext}
      >
        Next
      </button>
      <span style={{ color: 'var(--text-muted)' }}>
        {total === 0 ? '0 results' : `${start}–${end} of ${total}`}
      </span>
    </div>
  );
}
