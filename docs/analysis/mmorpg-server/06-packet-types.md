# 6. Packet Types

**File:** `src/enums/packets-type.enum.ts`

## Server → Client: 72 types

Major categories: Auth (Login, Session, CharacterList), Entity CRUD, Combat (Action, AutoAttack, TakeDamage, Die), UI (OpenWindow, Tooltip, Vendor), Inventory (AddItem, RemoveItem, MoveItem), Social (Party, Guild, Trade, Chat), World (LoadLevel, UpdateTick, MoveTo).

## Client → Server: 63 types

Major categories: Auth (Login, CreateCharacter), Movement (UpdateEntity), Combat (Action, CheckHit, AutoAttack), Inventory (DestroyItem, MoveItem, Equip), Social (RequestParty, RequestTrade, ChatMessage), Interaction (Interact, Collect, Skinning), Queue (batched packets).

## Observation

The packet type list is the most comprehensive of all three implementations (72+63 = 135 total vs Server1's 130+40 = 170 total). This is a **full MMORPG** with crafting, guilds, quests, trading, pets, mounts, events, etc.

All game-specific — none of this enters netudp.
