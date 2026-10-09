import React, { useEffect, useState } from 'react';
import {
  CardInfo, ShopListItem, ShopDraft,
  fetchCardList, fetchShopList, fetchShop, saveShop, deleteShop,
  SHOP_MAX_ITEMS, SHOP_VISIBLE,
} from './io/shops';

/** Shop editor: each `screens/shops/<id>.json` is a stock list referenced
 *  by an NPC's `shop` property.  Items are CARD_* symbols; prices come
 *  from the card catalogue and are never stored here.  `buys=1` marks a
 *  card merchant (the quick screen's CARDS detail then offers SELL).
 *
 *  The numeric shop id is the filename stem (append-only, like scene ids);
 *  deleting a shop removes the file and any NPC still pointing at it will
 *  stock nothing until repointed. */

export const ShopManager: React.FC = () => {
  const [cards, setCards] = useState<CardInfo[]>([]);
  const [items, setItems] = useState<ShopListItem[]>([]);
  const [activeId, setActiveId] = useState<number | null>(null);
  const [draft, setDraft] = useState<ShopDraft | null>(null);
  const [dirty, setDirty] = useState(false);
  const [status, setStatus] = useState('');

  const refreshList = async (select?: number) => {
    const list = await fetchShopList();
    setItems(list);
    setActiveId((prev) => {
      const want = select ?? prev;
      if (want != null && list.some((s) => s.id === want)) return want;
      return list[0]?.id ?? null;
    });
  };

  useEffect(() => {
    fetchCardList().then(setCards).catch((e) => setStatus(`cards load failed: ${e.message}`));
    refreshList().catch((e) => setStatus(`load failed: ${e.message}`));
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  useEffect(() => {
    if (activeId == null) { setDraft(null); return; }
    fetchShop(activeId).then((data) => {
      setDraft({ label: data.label || '', buys: data.buys ? 1 : 0, items: (data.items || []).slice() });
      setDirty(false);
      setStatus('');
    }).catch((e) => setStatus(`load failed: ${e.message}`));
  }, [activeId]);

  const cardBySymbol = (sym: string) => cards.find((c) => c.symbol === sym);

  const patch = (p: Partial<ShopDraft>) => {
    setDraft((prev) => (prev ? { ...prev, ...p } : prev));
    setDirty(true);
  };

  const save = async () => {
    if (!draft || activeId == null) return;
    try {
      await saveShop(activeId, draft);
      setDirty(false);
      setStatus('saved — recompile to apply');
      await refreshList(activeId);
    } catch (e: any) {
      setStatus(`save failed: ${e.message}`);
    }
  };

  const addNew = async () => {
    const next = items.reduce((m, s) => Math.max(m, s.id), 0) + 1;
    if (next > 255) { setStatus('no free shop id (max 255)'); return; }
    const first = cards[0]?.symbol || '';
    try {
      await saveShop(next, { label: `Shop ${next}`, buys: 0, items: first ? [first] : [] });
      await refreshList(next);
    } catch (e: any) {
      setStatus(`create failed: ${e.message}`);
    }
  };

  const remove = async () => {
    if (activeId == null) return;
    if (!window.confirm(`Delete shop ${activeId}? NPCs pointing at it will stock nothing.`)) return;
    try {
      await deleteShop(activeId);
      setDraft(null);
      await refreshList();
    } catch (e: any) {
      setStatus(`delete failed: ${e.message}`);
    }
  };

  return (
    <div style={{ display: 'flex', gap: 16, padding: 16, height: '100%', overflow: 'auto', background: '#f4efe4', color: '#222' }}>
      <div style={{ minWidth: 220 }}>
        <div style={{ fontWeight: 'bold', marginBottom: 4 }}>Shops</div>
        {items.map((it) => (
          <button
            key={it.id}
            onClick={() => setActiveId(it.id)}
            style={{
              display: 'block', width: '100%', textAlign: 'left', background: 'none',
              border: 'none', color: 'inherit', cursor: 'pointer', padding: 3,
              fontWeight: it.id === activeId ? 'bold' : 'normal',
            }}
          >
            {it.label || `Shop ${it.id}`} {it.buys ? '· merchant' : ''}
            <div style={{ fontSize: 11, color: '#777' }}>id {it.id} · {it.count} item(s)</div>
          </button>
        ))}
        <div style={{ marginTop: 8 }}>
          <button onClick={addNew}>+ New Shop</button>
        </div>
        <div style={{ fontSize: 11, color: '#555', marginTop: 8, lineHeight: 1.4 }}>
          Point an NPC at a shop with the Inspector's Shop field. The ROM
          shows {SHOP_VISIBLE} items at a time and scrolls.
        </div>
      </div>

      {draft && (
        <div style={{ flex: 1, minWidth: 340 }}>
          <h3 style={{ margin: '0 0 8px' }}>Shop {activeId}</h3>

          <div className="form-group">
            <label>Label (editor only)</label>
            <input
              type="text"
              value={draft.label}
              placeholder={`Shop ${activeId}`}
              onChange={(e) => patch({ label: e.target.value })}
            />
          </div>

          <div className="form-group" style={{ marginTop: 8 }}>
            <label>
              <input
                type="checkbox"
                checked={draft.buys === 1}
                onChange={(e) => patch({ buys: e.target.checked ? 1 : 0 })}
              />{' '}
              Card merchant (enables SELL on the CARDS detail page)
            </label>
          </div>

          <div style={{ marginTop: 10, fontWeight: 600, fontSize: 13 }}>
            Stock ({draft.items.length}/{SHOP_MAX_ITEMS})
          </div>
          {draft.items.map((sym, i) => {
            const card = cardBySymbol(sym);
            return (
              <div key={i} className="form-row" style={{ alignItems: 'center', marginTop: 4 }}>
                <span style={{ width: 20, textAlign: 'right' }}>{i + 1}</span>
                <select
                  style={{ gridColumn: 'span 2', minWidth: 220 }}
                  value={sym}
                  onChange={(e) => {
                    const list = draft.items.slice();
                    list[i] = e.target.value;
                    patch({ items: list });
                  }}
                >
                  {!card && sym && <option value={sym}>{sym} (unknown)</option>}
                  {cards.map((c) => (
                    <option key={c.symbol} value={c.symbol}>
                      {c.name} — {c.symbol} {c.price > 0 ? `(${c.price}g)` : '(not sold)'}
                    </option>
                  ))}
                </select>
                <button
                  onClick={() => patch({ items: draft.items.filter((_, j) => j !== i) })}
                  title="Remove"
                >
                  ✕
                </button>
              </div>
            );
          })}

          <div style={{ marginTop: 6 }}>
            <button
              disabled={draft.items.length >= SHOP_MAX_ITEMS || cards.length === 0}
              onClick={() => patch({ items: [...draft.items, cards[0].symbol] })}
            >
              + Add item
            </button>{' '}
            <button
              disabled={!draft.items.length}
              onClick={() => patch({ items: draft.items.slice(0, -1) })}
            >
              - Remove last
            </button>
          </div>

          <div style={{ marginTop: 14, display: 'flex', gap: 8 }}>
            <button onClick={save} disabled={!dirty}>Save</button>
            <button onClick={remove} style={{ color: '#a00' }}>Delete shop</button>
          </div>
          {status && <div style={{ marginTop: 8, fontSize: 12, color: '#555' }}>{status}</div>}
        </div>
      )}
    </div>
  );
};
