# Journeys — travel-demo road content

Data for the travel-demo ROM's journey system (`kaartenheld-travel-demo.gb`).
Compiled by `tools/screen_compiler/journey_compile.py` into
`src/game/travel_content.c` (ROM bank 14); `make journeys` rebuilds it,
`make journeys-check` fails loudly if the committed C is stale.
Never hand-edit the generated C — edit the JSON here instead.

## Routes (`routes/*.json`)

The demo loop is field → town → desolate field → field, all act 2:
`field_to_village` (10 steps), `town_to_desolate` (16 steps),
`desolate_to_field` (10 steps), plus the act-1 variant
`field_to_village_act1` (8 steps, walked while chapter 1).
Town and desolate signposts start their legs; each map also has a
supply merchant (one ANY-map event row covers all three, since every
route prices supplies identically).

A route is one road the signposts can start: `id`, display `name`,
`start` and `destination` scenes (keys of `levels/registry.json`),
`dest_x`/`dest_y` arrival tile, and `act` (1–9, the act the road
belongs to).
Each route carries its own economy and tuning, so the same road can
behave differently per act, and different roads draw different events:

- `supply`: `provisions_price` (merchant gold cost) plus
  `provisions_per_day` (provisions burned per travel day —
  a desert route just eats more).
- `camp`: the daily camp menu tuning — `hunt` (provisions yield
  range plus detour range, plus a rare `jackpot` haul that can
  rescue a starving party; green roads roll it at 30%, desolate
  roads at 20%), `rest` (heal plus schedule delay),
  `starve` (HP lost and every-N-days cadence without provisions),
  `study` (steps-saved / getting-lost ranges plus extra provisions).
- `methods`: carriage (instant, paid) and/or on-foot (fixed `steps`,
  per-step event `frequency`, and a `pool` of `{event, weight}`
  entries; weights sum to at most 255 for the fixed-bank sampler).
- `weather` (optional): the road's ambient climate — one entry per
  weather (`clear`, `rain`, `storm`, `heat`, `cold`, `wind`, `snow`,
  `fog`) with a roll `weight` (0 = never; all weights sum to at most
  255), a `days_min`/`days_max` duration, extra `provisions`
  burned per day while it holds, and a `hunt` delta added to the
  hunting jackpot chance (clear skies hunt well at +10, storms
  and snow barely at all). Each road tunes its own climate, so
  the same trip runs hotter, colder, or stormier depending on the act:
  act-1 roads stay gentle (light storms, no snow) while act-2 roads
  roll real heat, cold snaps, and snow. Snow also slows the march:
  every travelled step burns two days while it falls.

Weather is a journey condition, not just another encounter: each
travel day rolls (or keeps) one condition on the travel stream, the
route line shows it (`FIELD ROAD STORM`), and it changes what the
road costs. Rain eases hunger (`provisions` −1/day); heat and drought
eat more; cold snaps and wind spoil extra; storms and snow
tax most. Five crossings keep a weather eye open (see `wx` below),
so rain on the river or fog on the shortcut genuinely hurts.

## Events (`events/*.json`)

Three kinds:

- `battle`: an ambush with fixed stats (`battle` id, `enemy_name`,
  `enemy_hp`, `victory_gold`, `victory_days`, buffed `late_enemy_hp`
  once the Convergence clock hits zero).
- `choice`: a prompt with two options (`opt1`/`opt2`, at most 8 chars
  each). Each option either stages a fight (`battle`: a battle-event
  id) or applies numeric effects (`gold`, `food`, `water`, `heal`,
  `hurt`, `delay` added steps, `save` shaved steps, `days` extra
  burned days). Resources floor at zero, heals cap at max, and any
  death is a cold hard game over.
- `ruins`: the roadside-ruins detour (explore loads the ruins map,
  ignore keeps the schedule).

Every scenario gives success and failure the same odds: each option
carries `risk: 50`, and the compiler rejects anything else.
Each option also carries a `fail` branch (own result line plus
override effects, or its own `battle`) that applies when the roll
fails, so every choice can go either way.
`acts` lists the acts an event may appear on (default: all); the
compiler fails loudly if a route pools an event that does not admit
the road's act, or stages a battle that does not admit it either.
An option may also carry `wx`: a per-weather delta table keyed by
weather name (`rain`, `storm`, `fog`, ...) with `hurt`/`delay`/
`save`/`days`/`provisions` overrides (−10..10) that apply while
that weather holds — rain and storm sharpen a ford or a climb
(+hurt), fog eats a shortcut's saving (−save) or stretches a wrong
turn (+delay). Battle options only sharpen `hurt`. The deltas bake
into the staged event (both branches), so the outcome lines already
show the weather-sharpened numbers.

## Event list

### People & encounters

- `stranded_merchant`: Share provisions with a stranded merchant for gold, or leave them hungry.
- `route_info`: Listen to a traveller's news for a shortcut, or walk on past.
- `wounded_adventurer`: Aid a hurt fighter for a share of their loot, or leave them behind.
- `lost_child`: Guide a lost child home for a detour, or leave the child behind.
- `pilgrims`: Wait a day for a pilgrim procession, or shove rudely through it.
- `supply_merchant`: Buy provisions from a travelling merchant, or decline the goods.
- `former_enemy`: Greet an old foe and collect an old debt, or avoid the meeting.
- `join_journey`: Accept a fellow walker's shared meal and slower pace, or decline the company.
- `traveler_help`: Share supplies with a traveller for gold and a detour, or walk on past.

### Road & environment

- `landslide`: Climb a fresh landslide and get hurt, or detour around it.
- `abandoned_camp`: Search a deserted camp for leftover food and gold, or leave it alone.
- `dangerous_shortcut`: Take a thorny shortcut for a big saving at a bloody cost, or stay safe.
- `river_crossing`: Ford a fast river and get hurt, or pay a bridge toll instead.
- `storm_shelter`: Shelter from a storm for a day, or push on and get battered.
- `wrong_sign`: Follow a sign that looks wrong and lose time, or ignore it and walk on.
- `strange_fog`: Wait out a thick fog, or push through and get lost and hurt.
- `old_shrine`: Rest at an old shrine for a blessed heal, or pass by untouched.

### Supplies & resources

- `spoiled_food`: Throw out spoiled provisions, or risk eating them and fall ill.
- `water_source`: Take found provisions (plus healing), or haul a bigger load instead.
- `depot_parts`: Strip an abandoned depot for sellable scrap, or leave it alone.
- `pack_breaks`: Spend a day mending a torn pack, or dump supplies to lighten the load.
- `trade_food_parts`: Trade provisions for scrap with a tinker, or decline the deal.
- `wild_berries`: Pick wild berries for the road, or leave the bushes alone.
- `cook_meal`: Buy a hot meal that heals, or decline and keep your gold.
- `supply_cache`: Dig up a buried supply cache for provisions, or leave it buried.

### Combat & danger

- `ambush_slimes`: Slimes burst from the roadside and must be fought off.
- `slime_trio`: Staged battle row only — a slime trio ambush with victory gold.
- `passage_guard`: Fight the beast blocking the pass, or detour a long way around it.
- `night_stalker`: Stand watch through a fearful night, or sleep and wake up bitten.
- `occupied_camp`: Fight the monsters holding a campsite, or sneak quietly past them.
- `wounded_monster`: Finish a hurt beast for its bounty, or give it a wide berth.
- `battle_signs`: Search a recent battlefield for loot, or hurry on and save a step.
- `powerful_enemy`: Fight a slime pack for a rich reward, or flee and lose time instead.
- `skirmish_bats`: Staged battle row only — a bat ambush with victory gold.
- `skirmish_spiders`: Staged battle row only — a spider ambush with victory gold.
- `weak_beast`: Staged battle row only — a wounded beast that pays a small bounty.

### Mysteries & worldbuilding

- `slime_tree`: Scrape valuable goo off a slime-covered tree, or leave it alone.
- `waystone`: Touch a flickering Waystone for warmth, or leave its magic alone.
- `abandoned_village`: Search evacuated houses for supplies at the cost of a detour, or pass by.
- `slime_letter`: Read a dropped letter mentioning the Lord of Slimes, or leave it lying.
- `convergence_talk`: Hear a traveller's warning about the Convergence, or walk on past.
- `watched`: Call out to eyes watching from a ridge, or hurry on and save a step.
- `ruined_monument`: Read an ancient monument's account, or leave its story unread.
- `strange_animals`: Follow strangely behaving animals to a berry trail, or avoid them.
- `strange_light`: Investigate a cold fire on the horizon for its cache, or make an early camp.

### Choices with consequences

- `repair_cart`: Spend two days repairing a stranger's cart for later-paid gold, or leave it.
- `strange_noises`: Check ominous noises in the dark and fight what you find, or ignore them.
- `share_food`: Share provisions with a hungry family for their gratitude, or refuse them.
- `help_settlement`: Spend two days helping a village for provisions and gold, or pass by.
- `distress_signal`: Answer a signal fire for a delayed reward, or ignore it and keep the pace.
- `protect_caravan`: Guard a caravan against slimes, or decline the work.
- `ruins_sight`: Spot old ruins off the road and choose to explore or ignore them.
