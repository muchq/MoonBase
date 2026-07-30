import { useEffect, useRef, useState } from 'react';

/**
 * chess.com's game archives don't reach back further than this, so there is
 * nothing to index before it and the year stepper stops here.
 */
const EARLIEST_MONTH = '2007-01';

const MONTH_LABELS = [
  'Jan',
  'Feb',
  'Mar',
  'Apr',
  'May',
  'Jun',
  'Jul',
  'Aug',
  'Sep',
  'Oct',
  'Nov',
  'Dec',
];

export function monthValue(year: number, monthIndex: number): string {
  return `${year}-${String(monthIndex + 1).padStart(2, '0')}`;
}

/**
 * The current month in the viewer's own time zone. Deriving it from
 * `toISOString()` would roll into the next month for anyone east of UTC on the
 * last day of a month.
 */
export function currentMonth(): string {
  const now = new Date();
  return monthValue(now.getFullYear(), now.getMonth());
}

function parseMonth(value: string): { year: number; monthIndex: number } | null {
  const m = /^(\d{4})-(\d{2})$/.exec(value);
  if (!m) return null;
  const monthIndex = Number(m[2]) - 1;
  if (monthIndex < 0 || monthIndex > 11) return null;
  return { year: Number(m[1]), monthIndex };
}

/** `2024-03` → `Mar 2024`. Unparseable values are shown as-is. */
export function formatMonth(value: string): string {
  const parsed = parseMonth(value);
  return parsed ? `${MONTH_LABELS[parsed.monthIndex]} ${parsed.year}` : value;
}

interface MonthPickerProps {
  id: string;
  label: string;
  /** Canonical `YYYY-MM`. */
  value: string;
  onChange: (value: string) => void;
  /** Earliest selectable month, inclusive. */
  min?: string;
  /** Latest selectable month, inclusive. */
  max?: string;
  /** Extra class on the wrapper, for layout the surrounding form owns. */
  className?: string;
}

/**
 * A month-granularity date picker: a trigger showing the current selection, and
 * a popover with a year stepper over a grid of twelve months. Values are
 * canonical `YYYY-MM` throughout, so there is no free-text format to get wrong.
 *
 * Zero-padded `YYYY-MM` sorts lexicographically in calendar order, which is why
 * `min`/`max` bounds are plain string comparisons.
 */
export default function MonthPicker({
  id,
  label,
  value,
  onChange,
  min = EARLIEST_MONTH,
  max = currentMonth(),
  className = '',
}: MonthPickerProps) {
  const [open, setOpen] = useState(false);
  const [year, setYear] = useState(() => yearToShow(value, max));
  const rootRef = useRef<HTMLDivElement>(null);
  const triggerRef = useRef<HTMLButtonElement>(null);
  const labelId = `${id}-label`;
  const valueId = `${id}-value`;

  useEffect(() => {
    if (!open) return;
    function onPointerDown(event: MouseEvent) {
      if (!rootRef.current?.contains(event.target as Node)) setOpen(false);
    }
    document.addEventListener('mousedown', onPointerDown);
    return () => document.removeEventListener('mousedown', onPointerDown);
  }, [open]);

  function close() {
    setOpen(false);
    triggerRef.current?.focus();
  }

  function toggle() {
    // Reopening lands on the selected month's year rather than wherever the
    // stepper was left last time.
    if (!open) setYear(yearToShow(value, max));
    setOpen(!open);
  }

  function handleKeyDown(event: React.KeyboardEvent) {
    if (open && event.key === 'Escape') {
      event.stopPropagation();
      close();
    }
  }

  function select(monthIndex: number) {
    onChange(monthValue(year, monthIndex));
    close();
  }

  const minYear = parseMonth(min)?.year ?? year;
  const maxYear = parseMonth(max)?.year ?? year;

  return (
    <div
      className={`form-group month-picker ${className}`.trim()}
      ref={rootRef}
      onKeyDown={handleKeyDown}
    >
      <label htmlFor={id} id={labelId}>
        {label}
      </label>
      <button
        type="button"
        id={id}
        ref={triggerRef}
        className="month-picker-trigger"
        // Name the trigger "<label> <value>", the way a native select reads.
        // The value needs its own referenced node: accessible-name computation
        // skips nodes already visited, so a self-reference contributes nothing.
        aria-labelledby={`${labelId} ${valueId}`}
        aria-haspopup="dialog"
        aria-expanded={open}
        onClick={toggle}
      >
        <span id={valueId}>{formatMonth(value)}</span>
      </button>
      {open && (
        <div className="month-picker-popover" role="dialog" aria-labelledby={labelId}>
          <div className="month-picker-years">
            <button
              type="button"
              className="month-picker-step"
              aria-label="Previous year"
              disabled={year <= minYear}
              onClick={() => setYear(year - 1)}
            >
              ‹
            </button>
            <span className="month-picker-year">{year}</span>
            <button
              type="button"
              className="month-picker-step"
              aria-label="Next year"
              disabled={year >= maxYear}
              onClick={() => setYear(year + 1)}
            >
              ›
            </button>
          </div>
          <div className="month-picker-grid">
            {MONTH_LABELS.map((name, monthIndex) => {
              const candidate = monthValue(year, monthIndex);
              return (
                <button
                  key={name}
                  type="button"
                  className="month-picker-month"
                  aria-pressed={candidate === value}
                  disabled={candidate < min || candidate > max}
                  onClick={() => select(monthIndex)}
                >
                  {name}
                </button>
              );
            })}
          </div>
        </div>
      )}
    </div>
  );
}

function yearToShow(value: string, max: string): number {
  return parseMonth(value)?.year ?? parseMonth(max)?.year ?? new Date().getFullYear();
}
