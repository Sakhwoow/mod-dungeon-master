/*
 * mod-dungeon-master — DMConfig.cpp
 * Configuration loader.
 */

#include "DMConfig.h"
#include "Config.h"
#include "Log.h"
#include <sstream>
#include <algorithm>

namespace DungeonMaster
{


static std::vector<std::string> SplitString(const std::string& str, char delim)
{
    std::vector<std::string> tokens;
    std::stringstream ss(str);
    std::string token;
    while (std::getline(ss, token, delim))
    {
        size_t start = token.find_first_not_of(" \t");
        size_t end   = token.find_last_not_of(" \t");
        if (start != std::string::npos && end != std::string::npos)
            tokens.push_back(token.substr(start, end - start + 1));
        else if (!token.empty())
            tokens.push_back(token);
    }
    return tokens;
}


static std::string StripQuotes(const std::string& s)
{
    std::string out = s;
    if (!out.empty() && out.front() == '"') out.erase(out.begin());
    if (!out.empty() && out.back()  == '"') out.pop_back();
    return out;
}

// Singleton
DMConfig* DMConfig::Instance()
{
    static DMConfig instance;
    return &instance;
}

float DMConfig::GetVisualScaleForCreatureType(uint32 type) const
{
    switch (type)
    {
        case 1: return _visualScaleBeast;
        case 2: return _visualScaleDragonkin;
        case 3: return _visualScaleDemon;
        case 4: return _visualScaleElemental;
        case 5: return _visualScaleGiant;
        case 6: return _visualScaleUndead;
        case 7: return _visualScaleHumanoid;
        case 9: return _visualScaleMechanical;
        default: return 1.0f;
    }
}

// Load all config values
void DMConfig::LoadConfig(bool reload)
{
    if (reload)
        LOG_INFO("module", "DungeonMaster: Reloading configuration...");

    _enabled  = sConfigMgr->GetOption<bool>  ("DungeonMaster.Enable", true);
    _debug    = sConfigMgr->GetOption<bool>  ("DungeonMaster.Debug",  false);
    _npcEntry = sConfigMgr->GetOption<uint32>("DungeonMaster.NpcEntry", 500000);

    // Scaling
    _levelBand       = sConfigMgr->GetOption<uint8> ("DungeonMaster.Scaling.LevelBand",        3);
    _perPlayerHealth = sConfigMgr->GetOption<float> ("DungeonMaster.Scaling.PerPlayerHealth",   0.25f);
    _perPlayerDamage = sConfigMgr->GetOption<float> ("DungeonMaster.Scaling.PerPlayerDamage",   0.10f);
    _soloMultiplier  = sConfigMgr->GetOption<float> ("DungeonMaster.Scaling.SoloMultiplier",    0.50f);
    _eliteHealthMult = sConfigMgr->GetOption<float> ("DungeonMaster.Scaling.EliteHealthMult",   2.0f);
    _eliteDamageMult = sConfigMgr->GetOption<float> ("DungeonMaster.Scaling.EliteDamageMult",   1.5f);
    _bossHealthMult  = sConfigMgr->GetOption<float> ("DungeonMaster.Scaling.BossHealthMult",    8.0f);
    _bossDamageMult  = sConfigMgr->GetOption<float> ("DungeonMaster.Scaling.BossDamageMult",    1.5f);

    // Rewards
    _baseGold     = sConfigMgr->GetOption<uint32>("DungeonMaster.Rewards.BaseGold",    50000);
    _goldPerMob   = sConfigMgr->GetOption<uint32>("DungeonMaster.Rewards.GoldPerMob",  50);
    _goldPerBoss  = sConfigMgr->GetOption<uint32>("DungeonMaster.Rewards.GoldPerBoss", 10000);
    _xpMultiplier = sConfigMgr->GetOption<float> ("DungeonMaster.Rewards.XPMultiplier", 1.0f);
    _itemChance   = sConfigMgr->GetOption<uint32>("DungeonMaster.Rewards.ItemChance",  80);
    _rareChance   = sConfigMgr->GetOption<uint32>("DungeonMaster.Rewards.RareChance",  40);
    _epicChance   = sConfigMgr->GetOption<uint32>("DungeonMaster.Rewards.EpicChance",  15);
    _bonusTokenEntry       = sConfigMgr->GetOption<uint32>("DungeonMaster.Rewards.BonusTokenEntry",       0);
    _bonusTokensPerDungeon = sConfigMgr->GetOption<uint32>("DungeonMaster.Rewards.BonusTokensPerDungeon", 50);
    _bonusTokensPerBoss    = sConfigMgr->GetOption<uint32>("DungeonMaster.Rewards.BonusTokensPerBoss",    1);

    // Dungeon settings
    _bossCount       = sConfigMgr->GetOption<uint32>("DungeonMaster.Dungeon.BossCount",      1);
    _eliteChance     = sConfigMgr->GetOption<uint32>("DungeonMaster.Dungeon.EliteChance",    20);
    _aggroRadius     = sConfigMgr->GetOption<float> ("DungeonMaster.Dungeon.AggroRadius",    15.0f);
    _rareSpawnChance = sConfigMgr->GetOption<uint32>("DungeonMaster.Dungeon.RareSpawnChance", 5);
    _rareHealthMult  = sConfigMgr->GetOption<float> ("DungeonMaster.Scaling.RareHealthMult",  4.0f);
    _rareDamageMult  = sConfigMgr->GetOption<float> ("DungeonMaster.Scaling.RareDamageMult",  2.0f);
    _recentCreatureEntries = sConfigMgr->GetOption<uint32>("DungeonMaster.Variety.RecentEntries", 12);
    _visualScaleBeast = std::clamp(sConfigMgr->GetOption<float>("DungeonMaster.VisualScale.Beast", 1.0f), 0.55f, 1.0f);
    _visualScaleDragonkin = std::clamp(sConfigMgr->GetOption<float>("DungeonMaster.VisualScale.Dragonkin", 0.85f), 0.55f, 1.0f);
    _visualScaleDemon = std::clamp(sConfigMgr->GetOption<float>("DungeonMaster.VisualScale.Demon", 0.95f), 0.55f, 1.0f);
    _visualScaleElemental = std::clamp(sConfigMgr->GetOption<float>("DungeonMaster.VisualScale.Elemental", 0.90f), 0.55f, 1.0f);
    _visualScaleGiant = std::clamp(sConfigMgr->GetOption<float>("DungeonMaster.VisualScale.Giant", 0.70f), 0.55f, 1.0f);
    _visualScaleUndead = std::clamp(sConfigMgr->GetOption<float>("DungeonMaster.VisualScale.Undead", 1.0f), 0.55f, 1.0f);
    _visualScaleHumanoid = std::clamp(sConfigMgr->GetOption<float>("DungeonMaster.VisualScale.Humanoid", 1.0f), 0.55f, 1.0f);
    _visualScaleMechanical = std::clamp(sConfigMgr->GetOption<float>("DungeonMaster.VisualScale.Mechanical", 0.90f), 0.55f, 1.0f);

    // Timers
    _cooldownMinutes   = sConfigMgr->GetOption<uint32>("DungeonMaster.Cooldown.Minutes",     5);
    _timeLimitEnabled  = sConfigMgr->GetOption<bool>  ("DungeonMaster.TimeLimit.Enable",     false);
    _timeLimitMinutes  = sConfigMgr->GetOption<uint32>("DungeonMaster.TimeLimit.Minutes",    30);
    _maxConcurrentRuns = sConfigMgr->GetOption<uint32>("DungeonMaster.MaxConcurrentRuns",    20);

    // Death
    _respawnAtStart = sConfigMgr->GetOption<bool>  ("DungeonMaster.Death.RespawnAtStart",   true);
    _maxWipes       = sConfigMgr->GetOption<uint32>("DungeonMaster.Death.MaxWipes",         0);

    // Completion
    _completionTeleportDelay = sConfigMgr->GetOption<uint32>("DungeonMaster.Completion.TeleportDelay", 30);
    _announceCompletion      = sConfigMgr->GetOption<bool>  ("DungeonMaster.Completion.Announcement",  true);

    // Roguelike
    _roguelikeEnabled         = sConfigMgr->GetOption<bool>  ("DungeonMaster.Roguelike.Enable",            true);
    _roguelikeTransitionDelay = sConfigMgr->GetOption<uint32>("DungeonMaster.Roguelike.TransitionDelay",   30);
    _roguelikeHpScaling       = sConfigMgr->GetOption<float> ("DungeonMaster.Roguelike.HpScalingPerTier",  0.10f);
    _roguelikeDmgScaling      = sConfigMgr->GetOption<float> ("DungeonMaster.Roguelike.DmgScalingPerTier", 0.08f);
    _roguelikeArmorScaling    = sConfigMgr->GetOption<float> ("DungeonMaster.Roguelike.ArmorScalingPerTier", 0.05f);
    _roguelikeExpThreshold    = sConfigMgr->GetOption<uint32>("DungeonMaster.Roguelike.ExponentialThreshold", 5);
    _roguelikeExpFactor       = sConfigMgr->GetOption<float> ("DungeonMaster.Roguelike.ExponentialFactor",   1.15f);
    _roguelikeAffixStartTier  = sConfigMgr->GetOption<uint32>("DungeonMaster.Roguelike.AffixStartTier",     3);
    _roguelikeSecondAffixTier = sConfigMgr->GetOption<uint32>("DungeonMaster.Roguelike.SecondAffixTier",     7);
    _roguelikeThirdAffixTier  = sConfigMgr->GetOption<uint32>("DungeonMaster.Roguelike.ThirdAffixTier",     10);
    _roguelikeMaxBuffs        = sConfigMgr->GetOption<uint32>("DungeonMaster.Roguelike.MaxBuffs",            20);
    _roguelikeVendorEnabled = sConfigMgr->GetOption<bool>("DungeonMaster.Roguelike.VendorEnable", true);

    // White / black lists
    _dungeonWhitelist.clear();
    _dungeonBlacklist.clear();
    ParseStringList(sConfigMgr->GetOption<std::string>("DungeonMaster.Dungeon.Whitelist", ""), _dungeonWhitelist);
    ParseStringList(sConfigMgr->GetOption<std::string>("DungeonMaster.Dungeon.Blacklist", ""), _dungeonBlacklist);

    // Populate sub-tables
    LoadDifficulties();
    LoadThemes();
    LoadDungeons();
    LoadRoguelikeBuffPool();

    LOG_INFO("module", "DungeonMaster: Config loaded — {} difficulties, {} themes, {} dungeons, {} roguelike buffs.",
        _difficulties.size(), _themes.size(), _dungeons.size(), _roguelikeBuffPool.size());
}

// Load difficulty tiers from config
void DMConfig::LoadDifficulties()
{
    _difficulties.clear();

    for (uint32 i = 1; i <= MAX_DIFFICULTIES; ++i)
    {
        std::string val = sConfigMgr->GetOption<std::string>(
            "DungeonMaster.Difficulty." + std::to_string(i), "");
        if (val.empty())
            break;

        auto parts = SplitString(StripQuotes(val), ',');
        if (parts.size() < 7) continue;

        DifficultyTier t;
        t.Id = i;
        t.Name = parts[0];
        try {
            t.MinLevel         = static_cast<uint8>(std::stoi(parts[1]));
            t.MaxLevel         = static_cast<uint8>(std::stoi(parts[2]));
            t.HealthMultiplier = std::stof(parts[3]);
            t.DamageMultiplier = std::stof(parts[4]);
            t.RewardMultiplier = std::stof(parts[5]);
            t.MobCountMultiplier = std::stof(parts[6]);
        } catch (...) {
            LOG_ERROR("module", "DungeonMaster: Bad difficulty entry #{}", i);
            continue;
        }
        _difficulties.push_back(t);
    }

    if (_difficulties.empty())
    {
        DifficultyTier def;
        def.Id = 1; def.Name = "Normal";
        _difficulties.push_back(def);
        LOG_WARN("module", "DungeonMaster: No difficulties configured, using default.");
    }
}

// Load themes from config
void DMConfig::LoadThemes()
{
    _themes.clear();

    for (uint32 i = 1; i <= MAX_THEMES; ++i)
    {
        std::string val = sConfigMgr->GetOption<std::string>(
            "DungeonMaster.Theme." + std::to_string(i), "");
        if (val.empty())
            break;

        auto parts = SplitString(StripQuotes(val), ',');
        if (parts.size() < 2) continue;

        Theme theme;
        theme.Id   = i;
        theme.Name = parts[0];

        for (size_t j = 1; j < parts.size(); ++j)
        {
            try {
                theme.CreatureTypes.push_back(
                    static_cast<uint32>(std::stoi(parts[j])));
            } catch (...) { /* skip bad token */ }
        }
        _themes.push_back(theme);
    }

    if (_themes.empty())
    {
        Theme def;
        def.Id = 1; def.Name = "Random"; def.CreatureTypes.push_back(uint32(-1));
        _themes.push_back(def);
        LOG_WARN("module", "DungeonMaster: No themes configured, using Random.");
    }
}

    // hard-coded list of WotLK 5-man instances with level ranges
void DMConfig::LoadDungeons()
{
    _dungeons.clear();

    struct Def { uint32 map; const char* name; uint8 lo; uint8 hi; };

    static const Def kDungeons[] =
    {
        // Classic
        { 389, "Огненная пропасть",        13, 20 },
        {  36, "Мёртвые копи",             15, 25 },
        {  33, "Замок Темного Клыка",       18, 28 },
        {  34, "Темница",                  20, 30 },
        {  43, "Пещеры Стенаний",          15, 28 },
        {  48, "Глубины Черной Пучины",    20, 32 },
        {  47, "Загон острых колючек",     25, 35 },
        {  90, "Гномреган",                25, 38 },
        { 129, "Курганы острых колючек",   35, 45 },
        { 189, "Алый монастырь",           30, 45 },
        {  70, "Улдаман",                  38, 50 },
        { 209, "Зул'Фаррак",              42, 52 },
        { 349, "Мародон",                  40, 52 },
        { 109, "Затонувший храм",          45, 55 },
        { 230, "Глубины Черной горы",      48, 60 },
        { 229, "Пик Черной горы",          52, 60 },
        { 289, "Некромантская школа",      55, 60 },
        { 329, "Стратхольм",               55, 60 },
        // TBC
        { 543, "Укрепления Адского огня",  58, 70 },
        { 542, "Кровавый горн",            59, 70 },
        { 547, "Логово рабов",             60, 70 },
        { 546, "Подземные топи",           61, 70 },
        { 557, "Гробницы Маны",            62, 70 },
        { 558, "Аукенайские склепы",       63, 70 },
        { 556, "Залы Сетекк",              65, 70 },
        { 555, "Тёмный лабиринт",          68, 70 },
        { 540, "Разрушенные чертоги",      68, 70 },
        { 553, "Ботаника",                 68, 70 },
        { 554, "Механар",                  68, 70 },
        { 552, "Аркатрац",                 68, 70 },
        // WotLK
        { 574, "Крепость Утгард",          68, 80 },
        { 576, "Нексус",                   69, 80 },
        { 601, "Азжол-Неруб",              70, 80 },
        { 619, "Ан'кахет",                71, 80 },
        { 600, "Крепость Драк'Тарон",     72, 80 },
        { 608, "Аметистовая крепость",     73, 80 },
        { 604, "Гундрак",                  74, 80 },
        { 599, "Чертоги Камня",            75, 80 },
        { 602, "Чертоги Молний",           77, 80 },
        { 578, "Окулус",                   77, 80 },
        { 575, "Шпиль Утгард",             78, 80 },
        { 595, "Очищение Стратхольма",     78, 80 },
        { 632, "Кузница Душ",              79, 80 },
        { 658, "Яма Сарона",               79, 80 },
        { 668, "Чертоги Отражений",        79, 80 },
    };

    for (const auto& d : kDungeons)
    {
        if (!IsDungeonAllowed(d.map))
            continue;

        DungeonInfo info;
        info.MapId       = d.map;
        info.Name        = d.name;
        info.MinLevel    = d.lo;
        info.MaxLevel    = d.hi;
        info.IsAvailable = true;
        _dungeons.push_back(info);
    }
}

// Utility
void DMConfig::ParseStringList(const std::string& str, std::unordered_set<uint32>& outSet)
{
    if (str.empty()) return;
    for (const auto& tok : SplitString(str, ','))
    {
        try { outSet.insert(static_cast<uint32>(std::stoul(tok))); }
        catch (...) { /* skip */ }
    }
}

const DifficultyTier* DMConfig::GetDifficulty(uint32 id) const
{
    for (const auto& d : _difficulties)
        if (d.Id == id) return &d;
    return nullptr;
}

std::vector<const DifficultyTier*> DMConfig::GetDifficultiesForLevel(uint8 level) const
{
    std::vector<const DifficultyTier*> out;
    for (const auto& d : _difficulties)
        if (d.IsValidForLevel(level))
            out.push_back(&d);
    return out;
}

const Theme* DMConfig::GetTheme(uint32 id) const
{
    for (const auto& t : _themes)
        if (t.Id == id) return &t;
    return nullptr;
}

const DungeonInfo* DMConfig::GetDungeon(uint32 mapId) const
{
    for (const auto& d : _dungeons)
        if (d.MapId == mapId) return &d;
    return nullptr;
}

std::vector<const DungeonInfo*> DMConfig::GetDungeonsForLevel(uint8 minLevel, uint8 maxLevel) const
{
    std::vector<const DungeonInfo*> out;
    for (const auto& d : _dungeons)
        if (d.MaxLevel >= minLevel && d.MinLevel <= maxLevel && d.IsAvailable)
            out.push_back(&d);
    return out;
}

bool DMConfig::IsDungeonAllowed(uint32 mapId) const
{
    if (_dungeonBlacklist.count(mapId)) return false;
    if (!_dungeonWhitelist.empty() && !_dungeonWhitelist.count(mapId)) return false;
    return true;
}

    // sequential entries from DungeonMaster
void DMConfig::LoadRoguelikeBuffPool()
{
    _roguelikeBuffPool.clear();

    for (uint32 i = 1; i <= MAX_ROGUELIKE_BUFFS; ++i)
    {
        std::string val = sConfigMgr->GetOption<std::string>(
            "DungeonMaster.Roguelike.Buff." + std::to_string(i), "");
        if (val.empty())
            break;

        // Strip surrounding quotes
        std::string stripped = val;
        if (!stripped.empty() && stripped.front() == '"') stripped.erase(stripped.begin());
        if (!stripped.empty() && stripped.back()  == '"') stripped.pop_back();

        // Split by comma
        std::stringstream ss(stripped);
        std::string token;
        std::vector<std::string> parts;
        while (std::getline(ss, token, ','))
        {
            size_t s = token.find_first_not_of(" \t");
            size_t e = token.find_last_not_of(" \t");
            if (s != std::string::npos && e != std::string::npos)
                parts.push_back(token.substr(s, e - s + 1));
        }

        if (parts.size() < 3) continue;

        RoguelikeBuff b;
        b.Id = i;
        try {
            b.SpellId = static_cast<uint32>(std::stoul(parts[0]));
            b.Name    = parts[1];
            b.Weight  = static_cast<uint32>(std::stoul(parts[2]));
        } catch (...) {
            LOG_ERROR("module", "DungeonMaster: Bad roguelike buff entry #{}", i);
            continue;
        }
        _roguelikeBuffPool.push_back(b);
    }

    // Default pool if none configured
    if (_roguelikeBuffPool.empty())
    {
        LOG_INFO("module", "DungeonMaster: No roguelike buffs configured — using defaults.");

        // World buffs (Classic — stack with everything, universally applicable)
        _roguelikeBuffPool.push_back({1, 15366, "Songflower Serenade",   100});  // +15 stats, +5% crit
        _roguelikeBuffPool.push_back({2, 22888, "Rallying Cry",          80});   // +140 AP, +10% spell crit
        _roguelikeBuffPool.push_back({3, 24425, "Spirit of Zandalar",    80});   // +15% move speed, +10% stats
        _roguelikeBuffPool.push_back({4, 16609, "Warchief's Blessing",   80});   // +300 HP, haste, MP5
        _roguelikeBuffPool.push_back({5, 23768, "Fortune of Damage",     60});   // +10% damage

        // Class buffs (work on any target via AddAura)
        _roguelikeBuffPool.push_back({6, 20217, "Blessing of Kings",     90});   // +10% all stats
        _roguelikeBuffPool.push_back({7, 48161, "Power Word: Fortitude", 100});  // +stamina
        _roguelikeBuffPool.push_back({8, 48469, "Gift of the Wild",      100});  // +stats/armor/resists
        _roguelikeBuffPool.push_back({9, 19506, "Trueshot Aura",         70});   // +10% AP
        _roguelikeBuffPool.push_back({10, 24932, "Leader of the Pack",   70});   // +5% crit
    }
}

} // namespace DungeonMaster
