import React, { useEffect, useMemo, useState } from 'react';
import { RampGuide, fetchRampGuide } from './io/romRecolor';

export type RampSetFilter = 'obj' | 'base' | 'all';

/** First ramp (lowest slot) holding a slot in the given set, or null.
 *  Used for valid-by-construction defaults (e.g. new combat-art sets)
 *  instead of a hardcoded ramp name that a rename could silently break. */
export function firstSlottedRamp(
  guide: RampGuide | null,
  setName: 'obj' | 'base' | 'title',
): string | null {
  if (!guide) return null;
  let best: { slot: number; name: string } | null = null;
  for (const [name, e] of Object.entries(guide.ramps)) {
    for (const s of e.slots || []) {
      const [set, slot] = s.split(':');
      if (set !== setName) continue;
      const n = Number(slot);
      if (!best || n < best.slot) best = { slot: n, name };
    }
  }
  return best ? best.name : null;
}

/** Resolve a stored palette value (ramp name, or a legacy OBJ slot int)
 *  to a ramp name. Returns null when it names nothing known. */
export function resolveRampName(
  value: string | number | undefined | null,
  guide: RampGuide | null,
): string | null {
  if (typeof value === 'string') return guide?.ramps[value] ? value : null;
  if (!guide || typeof value !== 'number' || !Number.isInteger(value)) return null;
  const hit = Object.entries(guide.slots.obj || {})
    .find(([slot]) => Number(slot) === value);
  return hit ? hit[1] : null;
}

interface RampSelectProps {
  /** Stored value: ramp name, or a legacy numeric slot. Picking always
   *  yields a ramp name (files normalize to names on edit). */
  value: string | number;
  /** Which hardware set the ramp must belong to. 'obj' = overworld
   *  sprites, 'base' = BG-stamp combat art, 'all' = OAM combat art
   *  (slotless by design). */
  filter: RampSetFilter;
  onPick: (rampName: string) => void;
  /** Optional field: renders a `(none)` option that calls onClear. */
  optional?: boolean;
  onClear?: () => void;
  /** Jump into the Palettes view with the ramp editor pre-opened. */
  onEditRamp?: (rampName: string) => void;
  disabled?: boolean;
  title?: string;
}

/** Ramp picker wired to the palette system (replaces the legacy 0-7
 *  number inputs, which could neither display named ramps nor prevent
 *  manifest-breaking values). Options come from the static export, so
 *  selection works offline with no manifest built. */
export const RampSelect: React.FC<RampSelectProps> = ({
  value, filter, onPick, optional, onClear, onEditRamp, disabled, title,
}) => {
  const [guide, setGuide] = useState<RampGuide | null>(null);
  const [failed, setFailed] = useState(false);
  useEffect(() => {
    fetchRampGuide().then(setGuide).catch(() => setFailed(true));
  }, []);

  const options = useMemo(() => {
    if (!guide) return [];
    return Object.entries(guide.ramps)
      .filter(([, e]) => filter === 'all'
        || (e.slots || []).some((s) => s.startsWith(`${filter}:`)))
      .map(([name]) => name)
      .sort();
  }, [guide, filter]);

  const current = resolveRampName(value, guide);
  const currentColors = (current && guide?.ramps[current]?.colors) || null;
  const emptyOk = optional && (value === '' || value === undefined);
  const unknown = !!guide && !current && !emptyOk;

  if (failed || (!guide && !failed)) {
    return (
      <span title={title}>
        <select disabled value="">
          <option value="">{failed ? 'ramps unavailable (export missing)' : 'loading ramps…'}</option>
        </select>
      </span>
    );
  }

  return (
    <span style={{ display: 'inline-flex', gap: 4, alignItems: 'center' }} title={title}>
      {currentColors && (
        <span title={`${current} shades`} style={{ display: 'inline-flex', gap: 1 }}>
          {currentColors.map((c, i) => (
            <span key={i} style={{ width: 12, height: 12, background: c, border: '1px solid #444' }} />
          ))}
        </span>
      )}
      <select
        value={current || ''}
        disabled={disabled}
        onChange={(e) => {
          if (e.target.value) onPick(e.target.value);
          else if (onClear) onClear();
        }}
        style={unknown ? { border: '2px solid #a00' } : undefined}
        title={unknown ? `Unknown ramp value '${value}' — pick a ramp to fix` : title}
      >
        {unknown && <option value="">{`slot ${value} (unknown — pick a ramp)`}</option>}
        {optional && <option value="">(none)</option>}
        {options.map((n) => {
          const slots = (guide?.ramps[n]?.slots || []).join(',');
          return <option key={n} value={n}>{n}{slots ? ` (${slots})` : ''}</option>;
        })}
      </select>
      {onEditRamp && current && (
        <button
          className="btn btn-sm"
          title={`Edit ${current} colors in the Palettes view`}
          onClick={() => onEditRamp(current)}
          style={{ cursor: 'pointer' }}
        >✎</button>
      )}
    </span>
  );
};
