/** Shop + card catalogue disk I/O via the vite dev API.
 *
 *  A shop is `screens/shops/<id>.json` (label/buys/items); the numeric id
 *  is the filename stem and is what an NPC's `shop` property references.
 *  Items are CARD_* symbols from game_ids.h.  Prices come from the card
 *  catalogue (src/game/cards_content.c) and are never stored in the shop.
 *  This editor and shops_compile.py read the same JSON, so they cannot
 *  drift.
 */

export interface CardInfo {
  symbol: string;
  name: string;
  price: number;
  type: string;
  power: number;
}

export interface ShopListItem {
  id: number;
  label: string;
  buys: 0 | 1;
  count: number;
  items: string[];
}

export interface ShopDraft {
  label: string;
  buys: 0 | 1;
  items: string[];
}

export const SHOP_MAX_ITEMS = 50;
export const SHOP_VISIBLE = 10;

export async function fetchCardList(): Promise<CardInfo[]> {
  const res = await fetch('/api/cards');
  if (!res.ok) throw new Error(`cards returned ${res.status}`);
  const body = await res.json();
  if (!body.success) throw new Error(body.error || 'cards failed');
  return body.cards || [];
}

export async function fetchShopList(): Promise<ShopListItem[]> {
  const res = await fetch('/api/shops');
  if (!res.ok) throw new Error(`shops list returned ${res.status}`);
  const body = await res.json();
  if (!body.success) throw new Error(body.error || 'shops list failed');
  return body.items || [];
}

export async function fetchShop(id: number): Promise<ShopDraft> {
  const res = await fetch(`/api/shop?id=${id}`);
  if (!res.ok) throw new Error(`shop ${id} returned ${res.status}`);
  const body = await res.json();
  if (!body.success) throw new Error(body.error || `shop ${id} failed`);
  return body.data;
}

export async function saveShop(id: number, data: ShopDraft): Promise<void> {
  const res = await fetch('/api/save-shop', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ id, data }),
  });
  if (!res.ok) throw new Error(`save shop returned ${res.status}`);
  const body = await res.json();
  if (!body.success) throw new Error(body.error || 'save shop failed');
}

export async function deleteShop(id: number): Promise<void> {
  const res = await fetch('/api/delete-shop', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ id }),
  });
  if (!res.ok) throw new Error(`delete shop returned ${res.status}`);
  const body = await res.json();
  if (!body.success) throw new Error(body.error || 'delete shop failed');
}
