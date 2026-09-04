/* Item-spawn enable flag.
 *
 * itspawn.c checks db_AreItemSpawnsEnabled() every frame before running the
 * random-item countdown, and gmmain.c calls db_EnableItemSpawns() once at
 * boot (its guard flag is assigned false on the line above the branch, so the
 * enable is unconditional). The PC port excludes gmmain.c -- it is the console
 * entry point -- and does not compile src/melee/db at all, because dbitem.c
 * drags in the whole DevText debug-menu module. So both halves fell through to
 * the weak stubs: db_AreItemSpawnsEnabled() answered 0 forever and no VS item
 * ever spawned. Against Dolphin that showed up as the console drawing RNG
 * continuously from match frame 604 (items dropping, bouncing, throwing
 * collision sparks) while the port drew none at all.
 *
 * These are strong definitions and so override the weak stubs in
 * pc_stub/weak_stubs.c and pc_stub/undef_stubs.c. Only the item-spawn trio
 * lives here; the rest of the debug menu stays stubbed.
 */

static int pc_item_spawns_enabled;

void db_EnableItemSpawns(void)
{
    pc_item_spawns_enabled = 1;
}

void db_DisableItemSpawns(void)
{
    pc_item_spawns_enabled = 0;
}

int db_AreItemSpawnsEnabled(void)
{
    return pc_item_spawns_enabled;
}
