/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Affero General Public License as published by the
 * Free Software Foundation; either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * mod-small-raid-fixes
 *
 * Fixes specific raid encounter mechanics that bypass AutoBalance's stat-scaling hooks
 * entirely. Each fix scales linearly based on player count: maximum reduction at 1 player,
 * no intervention at PlayerThreshold (default 10) or above.
 *
 * Molten Core:
 *   Fix 1 - Majordomo Executus firepit DoT: periodic fire damage is a player-side aura
 *            tick, not a creature attack, so AutoBalance never sees it.
 *   Fix 2 - Ragnaros Wrath of Ragnaros: knockback fires on a hard-coded 25s C++ timer
 *            that AutoBalance has no hook into.
 *
 * Blackwing Lair:
 *   Fix 3 - Razorgore Phase 1 add cap: instance spawns up to 52 adds into the room while
 *            the player is immobilised on the mind-control orb. Cap scales from MaxAddsSolo
 *            at 1 player up to 52 (vanilla) at PlayerThreshold players.
 */

#include "ScriptMgr.h"
#include "Player.h"
#include "Config.h"
#include "SpellScript.h"
#include "SpellAuraEffects.h"
#include "Map.h"
#include "Creature.h"
#include <algorithm>
#include <list>
#include <unordered_map>
#include <vector>

// ============================================================
// BWL Razorgore add NPC IDs (from blackwing_lair.h / boss_razorgore.cpp)
// ============================================================

constexpr uint32 BWL_NPC_ELITE_DRACHKIN = 12422;
constexpr uint32 BWL_NPC_ELITE_WARRIOR  = 12458;
constexpr uint32 BWL_NPC_WARRIOR        = 12416;
constexpr uint32 BWL_NPC_MAGE           = 12420;
constexpr uint32 BWL_NPC_WARLOCK        = 12459;
constexpr uint32 BWL_VANILLA_MAX_ADDS   = 52; // 12 dragonkins + 40 orcs

static const std::vector<uint32> BWL_ADD_ENTRIES = {
    BWL_NPC_ELITE_DRACHKIN, BWL_NPC_ELITE_WARRIOR,
    BWL_NPC_WARRIOR, BWL_NPC_MAGE, BWL_NPC_WARLOCK
};

// ============================================================
// Configuration globals
// ============================================================

// Global
bool   g_SmallRaidFixesEnabled         = true;

// Molten Core
bool   g_MCEnabled                     = true;
uint32 g_MCPlayerThreshold             = 10;
bool   g_MajordomoFirepitFixEnabled    = true;
float  g_MajordomoFirepitDamageScalar  = 0.35f;
bool   g_RagnarosWrathFixEnabled       = true;
uint32 g_RagnarosWrathMinIntervalMs    = 50000;

// Blackwing Lair
bool   g_BWLEnabled                    = true;
bool   g_BWLRazorgoreAddFixEnabled     = true;
uint32 g_BWLMaxAddsSolo                = 3;
uint32 g_BWLPlayerThreshold            = 10;

// Per-instance timestamp tracking for Wrath of Ragnaros
std::unordered_map<uint32, uint32> g_LastWrathCastTime;

// ============================================================
// Utility
// ============================================================

static inline float lerpf(float a, float b, float t)
{
    return a + t * (b - a);
}

// Returns 0.0 at playerCount=1 (max fix), 1.0 at playerCount>=threshold (no fix).
static inline float scalingT(uint32 playerCount, uint32 threshold)
{
    if (playerCount <= 1)
        return 0.0f;
    if (threshold <= 1 || playerCount >= threshold)
        return 1.0f;
    return float(playerCount - 1) / float(threshold - 1);
}

// ============================================================
// Config loader
// ============================================================

class SmallRaidFixes_WorldScript : public WorldScript
{
public:
    SmallRaidFixes_WorldScript() : WorldScript("SmallRaidFixes_WorldScript") { }

    void OnAfterConfigLoad(bool /*reload*/) override
    {
        g_SmallRaidFixesEnabled        = sConfigMgr->GetOption<bool>  ("SmallRaidFixes.Enable",                        true);

        // Molten Core
        g_MCEnabled                    = sConfigMgr->GetOption<bool>  ("SmallRaidFixes.MC.Enable",                     true);
        g_MCPlayerThreshold            = sConfigMgr->GetOption<uint32>("SmallRaidFixes.MC.PlayerThreshold",             10);
        g_MajordomoFirepitFixEnabled   = sConfigMgr->GetOption<bool>  ("SmallRaidFixes.Majordomo.FirepitFix.Enable",   true);
        g_MajordomoFirepitDamageScalar = sConfigMgr->GetOption<float> ("SmallRaidFixes.Majordomo.FirepitDamageScalar", 0.35f);
        g_RagnarosWrathFixEnabled      = sConfigMgr->GetOption<bool>  ("SmallRaidFixes.Ragnaros.WrathFix.Enable",      true);
        g_RagnarosWrathMinIntervalMs   = sConfigMgr->GetOption<uint32>("SmallRaidFixes.Ragnaros.WrathMinIntervalMs",   50000);

        // Blackwing Lair
        g_BWLEnabled                   = sConfigMgr->GetOption<bool>  ("SmallRaidFixes.BWL.Enable",                    true);
        g_BWLRazorgoreAddFixEnabled    = sConfigMgr->GetOption<bool>  ("SmallRaidFixes.BWL.RazorgoreAddFix.Enable",    true);
        g_BWLMaxAddsSolo               = sConfigMgr->GetOption<uint32>("SmallRaidFixes.BWL.MaxAddsSolo",               3);
        g_BWLPlayerThreshold           = sConfigMgr->GetOption<uint32>("SmallRaidFixes.BWL.PlayerThreshold",           10);
    }
};

// ============================================================
// Fix 1: Majordomo firepit DoT - linear scalar 1 -> MC.PlayerThreshold
// ============================================================

class SmallRaidFixes_UnitScript : public UnitScript
{
public:
    SmallRaidFixes_UnitScript() : UnitScript("SmallRaidFixes_UnitScript") { }

    void ModifyPeriodicDamageAurasTick(Unit* target, Unit* /*attacker*/, uint32& damage, SpellInfo const* spellInfo) override
    {
        if (!g_SmallRaidFixesEnabled || !g_MCEnabled || !g_MajordomoFirepitFixEnabled)
            return;
        if (!target || !spellInfo)
            return;
        if (!target->IsPlayer())
            return;

        Map* map = target->GetMap();
        if (!map || map->GetId() != MAP_MOLTEN_CORE)
            return;

        // Only periodic fire damage (firepit/coal ground aura)
        if (!(spellInfo->SchoolMask & SPELL_SCHOOL_MASK_FIRE))
            return;

        uint32 playerCount = map->GetPlayersCountExceptGMs();
        if (playerCount >= g_MCPlayerThreshold)
            return;

        float t      = scalingT(playerCount, g_MCPlayerThreshold);
        float scalar = lerpf(g_MajordomoFirepitDamageScalar, 1.0f, t);
        damage       = uint32(damage * scalar);
    }
};

// ============================================================
// Fix 2: Ragnaros Wrath of Ragnaros - linear interval 1 -> MC.PlayerThreshold
// ============================================================

class SmallRaidFixes_WrathOfRagnaros : public SpellScript
{
    PrepareSpellScript(SmallRaidFixes_WrathOfRagnaros);

    bool Validate(SpellInfo const* /*spellInfo*/) override { return true; }

    void HandleBeforeCast()
    {
        if (!g_SmallRaidFixesEnabled || !g_MCEnabled || !g_RagnarosWrathFixEnabled)
            return;

        Unit* caster = GetCaster();
        if (!caster)
            return;

        Map* map = caster->GetMap();
        if (!map || map->GetId() != MAP_MOLTEN_CORE)
            return;

        uint32 playerCount = map->GetPlayersCountExceptGMs();
        if (playerCount >= g_MCPlayerThreshold)
            return;

        float  t           = scalingT(playerCount, g_MCPlayerThreshold);
        uint32 minInterval = uint32(lerpf(float(g_RagnarosWrathMinIntervalMs), 25000.0f, t));

        uint32 mapInstanceId = map->GetInstanceId();
        uint32 currentTime   = getMSTime();

        auto it = g_LastWrathCastTime.find(mapInstanceId);
        if (it != g_LastWrathCastTime.end())
        {
            uint32 elapsed = getMSTimeDiff(it->second, currentTime);
            if (elapsed < minInterval)
            {
                PreventHitDefaultEffect(EFFECT_0);
                PreventHitDefaultEffect(EFFECT_1);
                PreventHitDefaultEffect(EFFECT_2);
                return;
            }
        }

        g_LastWrathCastTime[mapInstanceId] = currentTime;
    }

    void Register() override
    {
        BeforeCast += SpellCastFn(SmallRaidFixes_WrathOfRagnaros::HandleBeforeCast);
    }
};

// ============================================================
// Fix 3: BWL Razorgore Phase 1 add cap - linear 1 -> BWL.PlayerThreshold
// ============================================================

class SmallRaidFixes_AllCreatureScript : public AllCreatureScript
{
public:
    SmallRaidFixes_AllCreatureScript() : AllCreatureScript("SmallRaidFixes_AllCreatureScript") { }

    void OnCreatureAddWorld(Creature* creature) override
    {
        if (!g_SmallRaidFixesEnabled || !g_BWLEnabled || !g_BWLRazorgoreAddFixEnabled)
            return;

        uint32 entry = creature->GetEntry();
        bool isBWLAdd = (entry == BWL_NPC_ELITE_DRACHKIN || entry == BWL_NPC_ELITE_WARRIOR ||
                         entry == BWL_NPC_WARRIOR        || entry == BWL_NPC_MAGE          ||
                         entry == BWL_NPC_WARLOCK);
        if (!isBWLAdd)
            return;

        Map* map = creature->GetMap();
        if (!map || map->GetId() != MAP_BLACKWING_LAIR)
            return;

        uint32 playerCount = map->GetPlayersCountExceptGMs();
        if (playerCount >= g_BWLPlayerThreshold)
            return;

        float  t      = scalingT(playerCount, g_BWLPlayerThreshold);
        uint32 addCap = uint32(lerpf(float(g_BWLMaxAddsSolo), float(BWL_VANILLA_MAX_ADDS), t));

        // Count all currently alive adds in the room.
        // The new creature is already in the world when this hook fires, so it is
        // included in the search results. liveCount therefore reflects the total
        // after this spawn. If that total exceeds the cap, despawn this new add.
        std::list<Creature*> addList;
        creature->GetCreatureListWithEntryInGrid(addList, BWL_ADD_ENTRIES, 300.0f);

        uint32 liveCount = 0;
        for (Creature* add : addList)
        {
            if (add->IsAlive())
                ++liveCount;
        }

        if (liveCount > addCap)
            creature->DespawnOrUnsummon(0ms);
    }
};

// ============================================================
// Instance cleanup
// ============================================================

class SmallRaidFixes_AllMapScript : public AllMapScript
{
public:
    SmallRaidFixes_AllMapScript() : AllMapScript("SmallRaidFixes_AllMapScript") { }

    void OnDestroyInstance(MapInstanced* /*mapInstanced*/, Map* map) override
    {
        if (!map)
            return;

        if (map->GetId() == MAP_MOLTEN_CORE)
            g_LastWrathCastTime.erase(map->GetInstanceId());
    }
};

// ============================================================
// Registration
// ============================================================

void AddSC_mod_small_raid_fixes()
{
    new SmallRaidFixes_WorldScript();
    new SmallRaidFixes_UnitScript();
    new SmallRaidFixes_AllCreatureScript();
    new SmallRaidFixes_AllMapScript();
    RegisterSpellScript(SmallRaidFixes_WrathOfRagnaros);
}
