import { useEffect, useMemo, useRef, useState } from 'react';

export interface ComboItem {
  value: string;
  label: string;
  group?: string;
}

interface FilterComboProps {
  items: ComboItem[];
  value: string;
  onPick: (value: string) => void;
  placeholder?: string;
  /** Label renderer for a current value matching no item (stale/hand-typed
   * ids).  Keeps the value visible and selectable instead of blanking it,
   * so the control can never silently destroy data. */
  staleLabel?: (value: string) => string;
  className?: string;
}

/** Filterable combobox for long id lists (scene picker at 1000 levels).
 * A native <select> does not scale; this filters substring,
 * case-insensitive, over label and value, with arrow-key navigation.
 * Controlled by value; typing never mutates state until a pick. */
export const FilterCombo: React.FC<FilterComboProps> = ({
  items,
  value,
  onPick,
  placeholder,
  staleLabel,
  className,
}) => {
  const [query, setQuery] = useState<string | null>(null);
  const [open, setOpen] = useState(false);
  const [highlight, setHighlight] = useState(0);
  const boxRef = useRef<HTMLDivElement>(null);

  const current = items.find((i) => i.value === value) || null;
  const shown = query ?? current?.label ?? value;

  const filtered = useMemo(() => {
    const q = (query ?? '').trim().toLowerCase();
    const base = q
      ? items.filter(
          (i) =>
            i.label.toLowerCase().includes(q) ||
            i.value.toLowerCase().includes(q)
        )
      : items;
    // Stale current value stays selectable at the top.
    if (value && !items.some((i) => i.value === value)) {
      const label = staleLabel ? staleLabel(value) : value;
      return [{ value, label }, ...base];
    }
    return base;
  }, [items, query, value, staleLabel]);

  useEffect(() => {
    setHighlight(0);
  }, [query]);

  // Reset the typed query when the picked value changes from outside.
  useEffect(() => {
    setQuery(null);
  }, [value]);

  const pick = (v: string) => {
    setQuery(null);
    setOpen(false);
    if (v !== value) onPick(v);
  };

  return (
    <div
      ref={boxRef}
      className={className}
      style={{ position: 'relative' }}
      onBlur={(e) => {
        // Let option mousedowns land before closing.
        if (!e.currentTarget.contains(e.relatedTarget as Node)) {
          setOpen(false);
          setQuery(null);
        }
      }}
    >
      <input
        type="text"
        className="filter-combo-input"
        value={shown}
        placeholder={placeholder}
        onFocus={() => setOpen(true)}
        onChange={(e) => {
          setQuery(e.target.value);
          setOpen(true);
        }}
        onKeyDown={(e) => {
          if (e.key === 'ArrowDown') {
            e.preventDefault();
            setOpen(true);
            setHighlight((h) => Math.min(h + 1, filtered.length - 1));
          } else if (e.key === 'ArrowUp') {
            e.preventDefault();
            setHighlight((h) => Math.max(h - 1, 0));
          } else if (e.key === 'Enter') {
            e.preventDefault();
            if (open && filtered[highlight]) pick(filtered[highlight].value);
            else setOpen(true);
          } else if (e.key === 'Escape') {
            setOpen(false);
            setQuery(null);
          }
        }}
      />
      {open && (
        <div className="filter-combo-menu">
          {filtered.length === 0 ? (
            <div className="filter-combo-empty">No matches</div>
          ) : (
            filtered.map((item, idx) => (
              <div key={item.group + '/' + item.value}>
                {item.group && (idx === 0 || filtered[idx - 1].group !== item.group) ? (
                  <div className="filter-combo-group">{item.group}</div>
                ) : null}
                <div
                  className="filter-combo-item"
                  onMouseDown={(e) => {
                    e.preventDefault();
                    pick(item.value);
                  }}
                  onMouseEnter={() => setHighlight(idx)}
                  style={{
                    background: idx === highlight ? 'var(--accent, #06c)' : undefined,
                    color: idx === highlight ? '#fff' : undefined,
                  }}
                >
                  {item.label}
                </div>
              </div>
            ))
          )}
        </div>
      )}
    </div>
  );
};
