# Multiplayer buying

Buying replaces a match's weapon, armour and ammunition pickups with purchases
paid in credits. Quake 4 1.3 added it (Ritual's "mode-agnostic buy menus") and
The Awakening extended it in an unfinished form. openQ4 offers both as one
server option and covers the stock buy menu with a retained card. The player
guide is [Buying in multiplayer](../user/buying.md).

## The option

| Choice | `si_isBuyingEnabled` | `si_buyingRules` |
|---|---|---|
| Off | 0 | kept |
| Quake 4 | 1 | 0 |
| The Awakening | 1 | 1 |

`buyingMode_t` in `src/mpgame/mp/Buying.h` is this three-way choice. Create
Server's **Buying** row (`createBuying`, 0 to 2, `Session_RetainedCreateValue`),
the retained Vote and Admin pages (`RVF_BUYING`, a choice of
`mp.vote.buying<n>` / `mp.admin.buying<n>`), the packed vote's `m_buying` byte,
`callvote buying 0-2` and the admin apply all carry the mode;
`MP_SetBuyingModeCVars` writes both settings, and turning buying off keeps the
rules for the next time it is on. The stock menus' **Allow Buying** check box
maps through `MP_BuyingModeFromOnOff`, so it keeps the rules a server sells by.
A change restarts the map, as the stock buying vote did.

Buying is a game type property (`GTF_BUYING`: DM, Team DM, the four CTF modes
and DeadZone). The match rules allow `buying_enabled` in the same modes
(`MP_RULE_MODES_BUYING`). Before this they allowed it only in DM, Team DM and
DeadZone, so a casual CTF server with buying on had its imported rules rejected
("competitive rules rejected field 28") and its match never left warmup.

## The catalog and the rules

`buyCatalog` in `src/mpgame/mp/Buying.cpp` lists every item either set of
rules sells, in the buy card's order, with its buy impulse, page, whether retail
sells it, the Awakening's price and its name and detail strings. It is the one
source for prices (`riBuyingManager::GetItemPrice`), availability
(`riBuyingManager::IsItemSold`, which `idPlayer::ItemBuyStatus` asks first),
impulses (`GetItemBuyImpulse`, `GetBuyItemForImpulse`) and the card's rows.
`tools/ui/retained_mp_buy.py` reads it, so the generator and the game agree.

- **Quake 4**: retail's items at the content's `ItemCostConstants` prices,
  with the content's `BuyingGameBalanceConstants` cash awards.
- **The Awakening**: the expansion's prices, which it kept in code (from its
  `guis/buymenu.gui` labels), and its cash awards (its
  `BuyingGameBalanceConstants`), applied over the content's: a key it does not
  set (the suicide penalty, medal bonuses, DeadZone control) pays as under
  Quake 4. It sells the Dark Matter Gun but not the Napalm Gun, FC Armor Regen
  as a fourth team special, and four weapon mods.

An item is sold only where the content defines what it gives: a weapon's def
and class, a mod's def on its weapon's `def_modN`. The expansion's core
cannon, fire cannon, freeze gun, their mods and the concussion grenades need
its content, which multiplayer never mounts (consolidation decision), so they
are not in the catalog. Two of its mods are left out on merit: the shotgun's
10-round clip (the multiplayer shotgun has none) and the railgun's penetration
(`hitscan_railgun_mp` already penetrates at 1.0; the mod's 0.85 is a
downgrade). Its menu's `wpmod_nailgun_rof` names a def no content has; its own
player def and retail call that mod `wpmod_nailgun_power`, which the catalog
sells as retail's Firepower Mod.

### Weapon mods in multiplayer

- `rvWeapon::Init` copies the mod defs into the weapon's spawn arguments, as in
  single player. A multiplayer weapon that fires from the ammo pool stays
  clipless: a mod's `clipSize` (the nailgun mod's 100) never adds a reload.
- `idPlayer::WriteWeaponModsToSnapshot` sends a 16-bit mask of weapons with
  mods and each one's four mod bits; every client rebuilds a weapon whose mods
  change (`currentWeapon = -1`, as `GiveWeaponMod` does on the server), and a
  respawn read keeps the mods the snapshot carried across `Init()`.
- `SpawnToPoint` restores the mods of every weapon a buying mode carries over a
  death.

## The buy card

`guis/menu/mp_buy.rui` (`tools/ui/retained_mp_buy.py`) covers the game's
`guis/buymenu.gui` (`Session_IsBuyMenu`) as the Escape card covers its menu:
the same card, header, tab strip, prompt bar, softened view and motion, at the
Escape card's size. The session loads it with the level where the server sells,
otherwise on its first opening.

- **Tabs**: WEAPONS, MODS, ARMOR & AMMO, TEAM. Each page lists its catalog
  items; a row the rules do not sell collapses and the rows after it close up
  (`top` bound to the shown rows before it). MODS says why it is empty under
  Quake 4's rules.
- **Plates**: the in-game light plate at a 36 dp pitch with the item's HUD
  atlas icon, its name, a lock where it cannot be bought now, a check where it
  is owned, and its price in the value colour, red where it is out of reach.
  An unavailable plate dims to 0.6 and shakes when chosen.
- **Detail**: the hot plate shows, in the column beside the list, the weapon
  wheel's drawing of the weapon (the weapon a mod fits) or the item's symbol,
  the name, the price, why it cannot be bought now or that it is owned, and
  what it does. Section 14.18 puts an unavailable action's reason on its plate;
  the buy card's pitch holds no second line, so the reason sits in the detail
  column, the only adaptation of the in-game card construction.
- **Data**: `idMultiplayerGame::PublishRetainedBuy` publishes
  `mp.buy.title|rules|credits`, the pages' counts and `mp.buy<i>.shown|available|owned|dear|label|price|detail|reason`
  in catalog order; the reasons come from `RetainedBuyReason`.
- **Verbs**: each plate runs `mpBuy` with its catalog index as a typed operand
  (`SessionMenuBuyRequest`, frozen when the plate is chosen); the session
  sends `retained buy <index>`, and the game checks the purchase again and
  buys through the buy impulse, keeping the menu open. Back runs `mpClose`.

The classic buy screen (`guis/buymenu.gui`) lists Quake 4's items. It shows the
current rules' prices; the Awakening's extras are bought from the card or with
`buy <item>`.

## Evidence

- `tools/tests/mp_buying_rules_contract.py`: the catalog, both price lists,
  the Awakening's awards, the match-rule mask, the vote and admin transport, the
  snapshot and respawn handling, the clipless rule, the session and adapter
  hooks and the generated documents.
- `openq4-ui-retained-buy` (`tools/tests/native/UiRetainedBuyChecks.inl`): the
  card validates and lays out in a runtime; plates, locks, checks, red prices,
  the collapsing rows, the detail beside the focused plate, purchases and the
  empty MODS page.
- `tools/tests/ui_retained_gate.py`: the session covers the buy menu with the
  card, `mpBuy <index>` reaches the game as `retained buy <index>`, malformed
  or out-of-range requests ask nothing, and Back closes.
- `tools/ui/capture_buy_menu.py --assets <Quake 4> --output .tmp/<run> --renderer gl|vulkan`
  plays a CTF listen server by the Awakening's rules: the card buys the
  hyperblaster (20000 to 19300 credits), its Bounce Shot Mod (11300, mod bits
  `3:1`), Team Ammo Regen (1300), and after a suicide (1000) the hyperblaster
  comes back with its mod. A Quake 4 run shows the empty MODS page. A remote
  client on a dedicated server buys the same and reports the server's charge,
  the mod bits from the snapshot and its own hyperblaster built with
  `projectile_hyperblaster_bounce1`.
