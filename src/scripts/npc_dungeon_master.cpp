/*
 * mod-dungeon-master — npc_dungeon_master.cpp
 * Gossip NPC: menu flow for difficulty/theme/dungeon selection.
 */

#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"
#include "GossipDef.h"
#include "Player.h"
#include "Creature.h"
#include "Group.h"
#include "Log.h"
#include "Chat.h"
#include "ObjectAccessor.h"
#include "DungeonMasterMgr.h"
#include "RoguelikeMgr.h"
#include "RoguelikeTypes.h"
#include "DMConfig.h"
#include <cstdio>
#include <mutex>
#include <random>

using namespace DungeonMaster;

// Gossip action IDs (encoded so ranges never overlap)
enum DMGossipActions
{
    GOSSIP_ACTION_MAIN_START    = 1,
    GOSSIP_ACTION_MAIN_INFO     = 2,
    GOSSIP_ACTION_MAIN_STATS    = 3,   // unused legacy, kept for safety

    GOSSIP_ACTION_DIFF_BASE     = 100,   // +diffId
    GOSSIP_ACTION_THEME_BASE    = 200,   // +themeId
    GOSSIP_ACTION_DUNGEON_BASE  = 300,   // +mapId (maps go up to ~700)
    GOSSIP_ACTION_DUNGEON_RANDOM = 10000,

    GOSSIP_ACTION_CONFIRM       = 10001,
    GOSSIP_ACTION_CANCEL        = 10002,
    GOSSIP_ACTION_SCALE_PARTY   = 10003,
    GOSSIP_ACTION_SCALE_TIER    = 10004,
    GOSSIP_ACTION_LEADERBOARD   = 10005, // legacy — redirects to board menu

    // Dungeon list pagination (free slots, outside the [300,10001) dungeon dispatch range)
    GOSSIP_ACTION_DUNGEON_PREV_PAGE = 10006, // dungeon list - previous page
    GOSSIP_ACTION_DUNGEON_NEXT_PAGE = 10007, // dungeon list - next page

    // Roguelike Mode
    GOSSIP_ACTION_ROGUELIKE_START       = 10010,
    GOSSIP_ACTION_ROGUELIKE_SCALE_PARTY = 10011,
    GOSSIP_ACTION_ROGUELIKE_SCALE_TIER  = 10012,
    GOSSIP_ACTION_ROGUELIKE_THEME       = 10100, // +themeId
    GOSSIP_ACTION_ROGUELIKE_QUIT        = 10200,
    GOSSIP_ACTION_ROGUELIKE_BOARD       = 10201,

    // Statistics & Leaderboards sub-menus
    GOSSIP_ACTION_STATS_MENU          = 10300,  // Stats & Leaderboards hub
    GOSSIP_ACTION_STATS_NORMAL        = 10301,  // My Normal Run Stats
    GOSSIP_ACTION_STATS_ROGUELIKE     = 10302,  // My Roguelike Stats
    GOSSIP_ACTION_BOARD_MENU          = 10310,  // Leaderboards hub
    GOSSIP_ACTION_BOARD_NORMAL        = 10311,  // Normal — Fastest Clears
    GOSSIP_ACTION_BOARD_RL_TIER       = 10312,  // Roguelike — Highest Tier
    GOSSIP_ACTION_BOARD_RL_FLOORS     = 10313,  // Roguelike — Most Floors
};

struct PlayerDMSelection
{
    uint32 DifficultyId  = 0;
    uint32 ThemeId       = 0;
    uint32 MapId         = 0;
    bool   ScaleToParty  = true;
    bool   IsRoguelike   = false;
    uint32 DungeonPage   = 0;   // current page in the (paginated) dungeon menu
};

static std::unordered_map<ObjectGuid, PlayerDMSelection> sSelections;
static std::mutex sSelMutex;

class npc_dungeon_master : public CreatureScript
{
public:
    npc_dungeon_master() : CreatureScript("npc_dungeon_master") {}

    bool OnGossipHello(Player* player, Creature* creature) override
    {
        if (!sDMConfig->IsEnabled())
        {
            ChatHandler(player->GetSession()).SendSysMessage(
                "|cFFFF0000[Мастер подземелий]|r Мастер подземелий сейчас недоступен.");
            player->PlayerTalkClass->SendCloseGossip();
            return true;
        }
        if (sDungeonMasterMgr->GetSessionByPlayer(player->GetGUID()))
        {
            LOG_INFO("module", "DungeonMaster: NPC blocked {} — still in active session",
                player->GetName());
            ChatHandler(player->GetSession()).SendSysMessage(
                "|cFFFF0000[Мастер подземелий]|r Вы уже участвуете в активном испытании!");
            player->PlayerTalkClass->SendCloseGossip();
            return true;
        }
        if (sRoguelikeMgr->IsPlayerInRun(player->GetGUID()))
        {
            player->PlayerTalkClass->ClearMenus();

            // Player is in an active roguelike run (auto-transitions between dungeons)
            RoguelikeRun* run = sRoguelikeMgr->GetRunByPlayer(player->GetGUID());
            if (run)
            {
                char tierBuf[256];
                snprintf(tierBuf, sizeof(tierBuf),
                    "|cFF00FFFF[Roguelike]|r Активный забег — |cFFFFD700Тир %u|r, "
                    "пройдено |cFFFFFFFF%u|r подз.",
                    run->CurrentTier, run->DungeonsCleared);
                ChatHandler(player->GetSession()).SendSysMessage(tierBuf);
            }
            else
            {
                ChatHandler(player->GetSession()).SendSysMessage(
                    "|cFF00FFFF[Roguelike]|r Вы участвуете в активном roguelike-забеге!");
            }

            AddGossipItemFor(player, GOSSIP_ICON_BATTLE,
                "|cFFFF0000Прервать roguelike-забег|r",
                GOSSIP_SENDER_MAIN, GOSSIP_ACTION_ROGUELIKE_QUIT);
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Отмена",
                GOSSIP_SENDER_MAIN, GOSSIP_ACTION_CANCEL);

            SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
            return true;
        }
        if (sDungeonMasterMgr->IsOnCooldown(player->GetGUID()))
        {
            uint32 rem  = sDungeonMasterMgr->GetRemainingCooldown(player->GetGUID());
            LOG_INFO("module", "DungeonMaster: NPC blocked {} — cooldown {}s remaining",
                player->GetName(), rem);
            char buf[256];
            snprintf(buf, sizeof(buf),
                "|cFFFFFF00[Мастер подземелий]|r Подождите |cFFFFFFFF%u|r мин |cFFFFFFFF%u|r сек до следующего испытания.",
                rem / 60, rem % 60);
            ChatHandler(player->GetSession()).SendSysMessage(buf);
            player->PlayerTalkClass->SendCloseGossip();
            return true;
        }
        ShowMainMenu(player, creature);
        return true;
    }

    bool OnGossipSelect(Player* player, Creature* creature, uint32 /*sender*/, uint32 action) override
    {
        player->PlayerTalkClass->ClearMenus();

        if (action == GOSSIP_ACTION_MAIN_START)
        {
            if (!sDungeonMasterMgr->CanCreateNewSession())
            {
                ChatHandler(player->GetSession()).SendSysMessage(
                    "|cFFFF0000[Мастер подземелий]|r Слишком много активных испытаний. Попробуйте позже.");
                player->PlayerTalkClass->SendCloseGossip();
                return true;
            }
            { std::lock_guard<std::mutex> lk(sSelMutex); sSelections[player->GetGUID()] = {}; }
            ShowDifficultyMenu(player, creature);
        }
        else if (action == GOSSIP_ACTION_MAIN_INFO)
            ShowInfoMenu(player, creature);
        else if (action == GOSSIP_ACTION_MAIN_STATS)
            ShowStatsAndBoardsMenu(player, creature);
        else if (action == GOSSIP_ACTION_LEADERBOARD)
            ShowBoardMenu(player, creature);
        else if (action >= GOSSIP_ACTION_DIFF_BASE && action < GOSSIP_ACTION_THEME_BASE)
        {
            uint32 diffId = action - GOSSIP_ACTION_DIFF_BASE;
            bool isRoguelike = false;
            { std::lock_guard<std::mutex> lk(sSelMutex);
              sSelections[player->GetGUID()].DifficultyId = diffId;
              isRoguelike = sSelections[player->GetGUID()].IsRoguelike; }
            if (isRoguelike)
                ShowRoguelikeScalingMenu(player, creature);
            else
                ShowScalingMenu(player, creature);
        }
        else if (action == GOSSIP_ACTION_SCALE_PARTY)
        {
            { std::lock_guard<std::mutex> lk(sSelMutex); sSelections[player->GetGUID()].ScaleToParty = true; }
            ShowThemeMenu(player, creature);
        }
        else if (action == GOSSIP_ACTION_SCALE_TIER)
        {
            { std::lock_guard<std::mutex> lk(sSelMutex); sSelections[player->GetGUID()].ScaleToParty = false; }
            ShowThemeMenu(player, creature);
        }
        else if (action >= GOSSIP_ACTION_THEME_BASE && action < GOSSIP_ACTION_DUNGEON_BASE)
        {
            { std::lock_guard<std::mutex> lk(sSelMutex);
              sSelections[player->GetGUID()].ThemeId     = action - GOSSIP_ACTION_THEME_BASE;
              sSelections[player->GetGUID()].DungeonPage = 0; }
            ShowDungeonMenu(player, creature);
        }
        else if (action == GOSSIP_ACTION_DUNGEON_PREV_PAGE)
        {
            { std::lock_guard<std::mutex> lk(sSelMutex);
              auto& s = sSelections[player->GetGUID()];
              if (s.DungeonPage > 0) --s.DungeonPage; }
            ShowDungeonMenu(player, creature);
        }
        else if (action == GOSSIP_ACTION_DUNGEON_NEXT_PAGE)
        {
            { std::lock_guard<std::mutex> lk(sSelMutex);
              ++sSelections[player->GetGUID()].DungeonPage; }
            ShowDungeonMenu(player, creature);
        }
        else if (action == GOSSIP_ACTION_DUNGEON_RANDOM)
        {
            { std::lock_guard<std::mutex> lk(sSelMutex); sSelections[player->GetGUID()].MapId = 0; }
            ShowConfirmMenu(player, creature);
        }
        else if (action >= GOSSIP_ACTION_DUNGEON_BASE && action < GOSSIP_ACTION_CONFIRM)
        {
            { std::lock_guard<std::mutex> lk(sSelMutex); sSelections[player->GetGUID()].MapId = action - GOSSIP_ACTION_DUNGEON_BASE; }
            ShowConfirmMenu(player, creature);
        }
        else if (action == GOSSIP_ACTION_CONFIRM)
            StartChallenge(player, creature);
        else if (action == GOSSIP_ACTION_CANCEL)
        {
            { std::lock_guard<std::mutex> lk(sSelMutex); sSelections.erase(player->GetGUID()); }
            ShowMainMenu(player, creature);
        }
        // ---- Roguelike Actions ----
        else if (action == GOSSIP_ACTION_ROGUELIKE_START)
        {
            if (sRoguelikeMgr->IsPlayerInRun(player->GetGUID()))
            {
                ChatHandler(player->GetSession()).SendSysMessage(
                    "|cFFFF0000[Roguelike]|r You are already in a roguelike run!");
                player->PlayerTalkClass->SendCloseGossip();
                return true;
            }
            { std::lock_guard<std::mutex> lk(sSelMutex);
              sSelections[player->GetGUID()] = {};
              sSelections[player->GetGUID()].IsRoguelike = true; }
            ShowRoguelikeDifficultyMenu(player, creature);
        }
        else if (action == GOSSIP_ACTION_ROGUELIKE_SCALE_PARTY)
        {
            { std::lock_guard<std::mutex> lk(sSelMutex); sSelections[player->GetGUID()].ScaleToParty = true; }
            ShowRoguelikeThemeMenu(player, creature);
        }
        else if (action == GOSSIP_ACTION_ROGUELIKE_SCALE_TIER)
        {
            { std::lock_guard<std::mutex> lk(sSelMutex); sSelections[player->GetGUID()].ScaleToParty = false; }
            ShowRoguelikeThemeMenu(player, creature);
        }
        else if (action >= GOSSIP_ACTION_ROGUELIKE_THEME && action < GOSSIP_ACTION_ROGUELIKE_QUIT)
        {
            uint32 themeId = action - GOSSIP_ACTION_ROGUELIKE_THEME;
            { std::lock_guard<std::mutex> lk(sSelMutex); sSelections[player->GetGUID()].ThemeId = themeId; }
            StartRoguelike(player, creature);
        }
        else if (action == GOSSIP_ACTION_ROGUELIKE_QUIT)
        {
            if (sRoguelikeMgr->IsPlayerInRun(player->GetGUID()))
            {
                sRoguelikeMgr->QuitRun(player->GetGUID());
                ChatHandler(player->GetSession()).SendSysMessage(
                    "|cFF00FFFF[Roguelike]|r Забег прерван.");
            }
            player->PlayerTalkClass->SendCloseGossip();
        }
        else if (action == GOSSIP_ACTION_ROGUELIKE_BOARD)
        {
            ShowRoguelikeLeaderboard(player, creature, false);
        }
        // ---- Statistics & Leaderboards ----
        else if (action == GOSSIP_ACTION_STATS_MENU)
            ShowStatsAndBoardsMenu(player, creature);
        else if (action == GOSSIP_ACTION_STATS_NORMAL)
            ShowNormalStats(player, creature);
        else if (action == GOSSIP_ACTION_STATS_ROGUELIKE)
            ShowRoguelikeStats(player, creature);
        else if (action == GOSSIP_ACTION_BOARD_MENU)
            ShowBoardMenu(player, creature);
        else if (action == GOSSIP_ACTION_BOARD_NORMAL)
            ShowNormalLeaderboard(player, creature);
        else if (action == GOSSIP_ACTION_BOARD_RL_TIER)
            ShowRoguelikeLeaderboard(player, creature, false);
        else if (action == GOSSIP_ACTION_BOARD_RL_FLOORS)
            ShowRoguelikeLeaderboard(player, creature, true);
        return true;
    }

private:
    // ---- Menu builders ----

    void ShowMainMenu(Player* player, Creature* creature)
    {
        player->PlayerTalkClass->ClearMenus();
        AddGossipItemFor(player, GOSSIP_ICON_BATTLE, "Начать испытание",
            GOSSIP_SENDER_MAIN, GOSSIP_ACTION_MAIN_START);
        if (sDMConfig->IsRoguelikeEnabled())
            AddGossipItemFor(player, GOSSIP_ICON_BATTLE, "|cFF00FFFFRoguelike режим|r",
                GOSSIP_SENDER_MAIN, GOSSIP_ACTION_ROGUELIKE_START);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Как это работает?",
            GOSSIP_SENDER_MAIN, GOSSIP_ACTION_MAIN_INFO);
        AddGossipItemFor(player, GOSSIP_ICON_TABARD, "|cFFFFD700Статистика и рейтинги|r",
            GOSSIP_SENDER_MAIN, GOSSIP_ACTION_STATS_MENU);
        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
    }

    void ShowDifficultyMenu(Player* player, Creature* creature)
    {
        player->PlayerTalkClass->ClearMenus();
        uint8 lvl = player->GetLevel();

        for (const auto& d : sDMConfig->GetDifficulties())
        {
            char buf[256];
            if (!d.IsValidForLevel(lvl))
                snprintf(buf, sizeof(buf), "|cFF808080%s|r (Ур %u-%u) - |cFFFF0000Требуется %u+|r",
                    d.Name.c_str(), d.MinLevel, d.MaxLevel, d.MinLevel);
            else if (!d.IsOnLevelFor(lvl))
                snprintf(buf, sizeof(buf), "%s |cFF808080(Ур %u-%u — Легко)|r",
                    d.Name.c_str(), d.MinLevel, d.MaxLevel);
            else
                snprintf(buf, sizeof(buf), "|cFF00FF00%s|r (Ур %u-%u)",
                    d.Name.c_str(), d.MinLevel, d.MaxLevel);

            AddGossipItemFor(player,
                d.IsValidForLevel(lvl) ? GOSSIP_ICON_BATTLE : GOSSIP_ICON_CHAT,
                buf, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_DIFF_BASE + d.Id);
        }
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "|cFFFF0000<< Назад|r", GOSSIP_SENDER_MAIN, GOSSIP_ACTION_CANCEL);
        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
    }

    void ShowScalingMenu(Player* player, Creature* creature)
    {
        player->PlayerTalkClass->ClearMenus();

        PlayerDMSelection sel;
        { std::lock_guard<std::mutex> lk(sSelMutex);
          auto it = sSelections.find(player->GetGUID());
          if (it == sSelections.end()) { player->PlayerTalkClass->SendCloseGossip(); return; }
          sel = it->second; }

        const DifficultyTier* diff = sDMConfig->GetDifficulty(sel.DifficultyId);
        if (!diff) { player->PlayerTalkClass->SendCloseGossip(); return; }

        uint8 partyLevel = sDungeonMasterMgr->ComputeEffectiveLevel(player);

        char buf1[256], buf2[256];
        snprintf(buf1, sizeof(buf1),
            "|cFF00FF00Масштаб по уровню группы|r (Ур %u) — Полное испытание на вашем уровне",
            partyLevel);
        snprintf(buf2, sizeof(buf2),
            "|cFFFFD700Сложность подземелья|r (Ур %u-%u) — Оригинальный диапазон уровней",
            diff->MinLevel, diff->MaxLevel);

        AddGossipItemFor(player, GOSSIP_ICON_BATTLE, buf1,
            GOSSIP_SENDER_MAIN, GOSSIP_ACTION_SCALE_PARTY);
        AddGossipItemFor(player, GOSSIP_ICON_INTERACT_1, buf2,
            GOSSIP_SENDER_MAIN, GOSSIP_ACTION_SCALE_TIER);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "|cFFFF0000<< Назад|r",
            GOSSIP_SENDER_MAIN, GOSSIP_ACTION_CANCEL);
        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
    }

    void ShowThemeMenu(Player* player, Creature* creature)
    {
        player->PlayerTalkClass->ClearMenus();
        for (const auto& t : sDMConfig->GetThemes())
            AddGossipItemFor(player, GOSSIP_ICON_BATTLE, t.Name, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_THEME_BASE + t.Id);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "|cFFFF0000<< Назад|r", GOSSIP_SENDER_MAIN, GOSSIP_ACTION_CANCEL);
        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
    }

    void ShowDungeonMenu(Player* player, Creature* creature)
    {
        player->PlayerTalkClass->ClearMenus();

        // Dungeons per page. Worst-case items rendered per page:
        //   page 0:   Random(1) + DUNGEONS_PER_PAGE + Next(<=1) + Back(1)
        //   page >0:  Prev(1)   + DUNGEONS_PER_PAGE + Next(<=1) + Back(1)
        // With DUNGEONS_PER_PAGE = 24 the max is 27 <= GOSSIP_MAX_MENU_ITEMS(32).
        static const uint32 DUNGEONS_PER_PAGE = 24;

        uint32 diffId;
        uint32 page;
        { std::lock_guard<std::mutex> lk(sSelMutex);
          auto it = sSelections.find(player->GetGUID());
          if (it == sSelections.end()) { player->PlayerTalkClass->SendCloseGossip(); return; }
          diffId = it->second.DifficultyId;
          page   = it->second.DungeonPage; }

        const DifficultyTier* diff = sDMConfig->GetDifficulty(diffId);
        if (!diff) { player->PlayerTalkClass->SendCloseGossip(); return; }

        auto dungeons = sDMConfig->GetDungeonsForLevel(diff->MinLevel, diff->MaxLevel);

        // Clamp page in case the dungeon list shrank or page ran past the end.
        uint32 total      = static_cast<uint32>(dungeons.size());
        uint32 totalPages = total ? ((total + DUNGEONS_PER_PAGE - 1) / DUNGEONS_PER_PAGE) : 1;
        if (page >= totalPages)
        {
            page = totalPages - 1;
            std::lock_guard<std::mutex> lk(sSelMutex);
            auto it = sSelections.find(player->GetGUID());
            if (it != sSelections.end())
                it->second.DungeonPage = page;
        }

        // "Random Dungeon" stays reachable: shown only on the first page.
        if (page == 0)
            AddGossipItemFor(player, GOSSIP_ICON_BATTLE, "|cFFFFD700Случайное подземелье|r",
                GOSSIP_SENDER_MAIN, GOSSIP_ACTION_DUNGEON_RANDOM);

        // Render this page's slice of dungeons.
        uint32 start = page * DUNGEONS_PER_PAGE;
        uint32 end   = start + DUNGEONS_PER_PAGE;
        if (end > total)
            end = total;
        for (uint32 i = start; i < end; ++i)
        {
            const DungeonInfo* dg = dungeons[i];
            char buf[128];
            snprintf(buf, sizeof(buf), "%s (Ур %u-%u)", dg->Name.c_str(), dg->MinLevel, dg->MaxLevel);
            AddGossipItemFor(player, GOSSIP_ICON_INTERACT_1, buf,
                GOSSIP_SENDER_MAIN, GOSSIP_ACTION_DUNGEON_BASE + dg->MapId);
        }

        if (dungeons.empty())
            AddGossipItemFor(player, GOSSIP_ICON_CHAT,
                "|cFF808080Нет доступных подземелий|r", GOSSIP_SENDER_MAIN, GOSSIP_ACTION_CANCEL);

        // Page navigation, rendered conditionally.
        if (page > 0)
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "|cFF00FF00<< Предыдущая страница|r",
                GOSSIP_SENDER_MAIN, GOSSIP_ACTION_DUNGEON_PREV_PAGE);
        if (end < total)
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "|cFF00FF00Следующая страница >>|r",
                GOSSIP_SENDER_MAIN, GOSSIP_ACTION_DUNGEON_NEXT_PAGE);

        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "|cFFFF0000<< Назад|r", GOSSIP_SENDER_MAIN, GOSSIP_ACTION_CANCEL);
        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
    }

    void ShowConfirmMenu(Player* player, Creature* creature)
    {
        player->PlayerTalkClass->ClearMenus();

        PlayerDMSelection sel;
        { std::lock_guard<std::mutex> lk(sSelMutex);
          auto it = sSelections.find(player->GetGUID());
          if (it == sSelections.end()) { player->PlayerTalkClass->SendCloseGossip(); return; }
          sel = it->second; }

        const DifficultyTier* diff = sDMConfig->GetDifficulty(sel.DifficultyId);
        const Theme*          theme = sDMConfig->GetTheme(sel.ThemeId);

        std::string dgName = "Случайное подземелье";
        if (sel.MapId > 0)
            if (const DungeonInfo* dg = sDMConfig->GetDungeon(sel.MapId))
                dgName = dg->Name;

        Group* g = player->GetGroup();
        uint32 ps = g ? g->GetMembersCount() : 1;

        char buf[256];
        ChatHandler(player->GetSession()).SendSysMessage("|cFFFFD700========== Сводка испытания ==========|r");
        snprintf(buf, sizeof(buf), "  Сложность: |cFF00FF00%s|r", diff ? diff->Name.c_str() : "?");
        ChatHandler(player->GetSession()).SendSysMessage(buf);
        snprintf(buf, sizeof(buf), "  Масштаб:   |cFF00FF00%s|r",
            sel.ScaleToParty ? "Уровень группы" : "Сложность подземелья");
        ChatHandler(player->GetSession()).SendSysMessage(buf);
        snprintf(buf, sizeof(buf), "  Тема:      |cFF00FF00%s|r", theme ? theme->Name.c_str() : "?");
        ChatHandler(player->GetSession()).SendSysMessage(buf);
        snprintf(buf, sizeof(buf), "  Подземелье:|cFF00FF00%s|r", dgName.c_str());
        ChatHandler(player->GetSession()).SendSysMessage(buf);
        snprintf(buf, sizeof(buf), "  Игроков:   |cFFFFFFFF%u|r", ps);
        ChatHandler(player->GetSession()).SendSysMessage(buf);
        if (ps > 1)
            ChatHandler(player->GetSession()).SendSysMessage("|cFFFFFF00  Все члены группы будут телепортированы!|r");
        ChatHandler(player->GetSession()).SendSysMessage("|cFFFFD700========================================|r");

        AddGossipItemFor(player, GOSSIP_ICON_BATTLE, "|cFF00FF00>> НАЧАТЬ ИСПЫТАНИЕ <<|r",
            GOSSIP_SENDER_MAIN, GOSSIP_ACTION_CONFIRM);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "|cFFFF0000<< Отмена|r",
            GOSSIP_SENDER_MAIN, GOSSIP_ACTION_CANCEL);
        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
    }

    void ShowInfoMenu(Player* player, Creature* creature)
    {
        player->PlayerTalkClass->ClearMenus();
        ChatHandler(player->GetSession()).SendSysMessage("|cFFFFD700========= Испытание Мастера подземелий =========|r");
        ChatHandler(player->GetSession()).SendSysMessage("|cFFFFFFFF1.|r Выберите уровень сложности");
        ChatHandler(player->GetSession()).SendSysMessage("|cFFFFFFFF2.|r Выберите масштаб: уровень группы или сложность подземелья");
        ChatHandler(player->GetSession()).SendSysMessage("|cFFFFFFFF3.|r Выберите тему существ");
        ChatHandler(player->GetSession()).SendSysMessage("|cFFFFFFFF4.|r Выберите подземелье или случайное");
        ChatHandler(player->GetSession()).SendSysMessage("|cFFFFFFFF5.|r Вас телепортируют в очищенный инстанс");
        ChatHandler(player->GetSession()).SendSysMessage("|cFFFFFFFF6.|r Победите босса для завершения испытания");
        ChatHandler(player->GetSession()).SendSysMessage("|cFFFFFFFF7.|r Соберите золото и снаряжение в награду!");
        ChatHandler(player->GetSession()).SendSysMessage("|cFFFFD700==========================================|r");
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "<< Назад", GOSSIP_SENDER_MAIN, GOSSIP_ACTION_CANCEL);
        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
    }

    // ---- Statistics & Leaderboards Hub ----

    void ShowStatsAndBoardsMenu(Player* player, Creature* creature)
    {
        player->PlayerTalkClass->ClearMenus();
        AddGossipItemFor(player, GOSSIP_ICON_TABARD, "Моя статистика",
            GOSSIP_SENDER_MAIN, GOSSIP_ACTION_STATS_NORMAL);
        if (sDMConfig->IsRoguelikeEnabled())
            AddGossipItemFor(player, GOSSIP_ICON_TABARD, "|cFF00FFFFМоя roguelike статистика|r",
                GOSSIP_SENDER_MAIN, GOSSIP_ACTION_STATS_ROGUELIKE);
        AddGossipItemFor(player, GOSSIP_ICON_TABARD, "|cFFFFD700Рейтинги|r",
            GOSSIP_SENDER_MAIN, GOSSIP_ACTION_BOARD_MENU);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "|cFFFF0000<< Назад|r",
            GOSSIP_SENDER_MAIN, GOSSIP_ACTION_CANCEL);
        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
    }

    static void FormatTime(uint32 seconds, char* buf, size_t len)
    {
        if (seconds >= 3600)
            snprintf(buf, len, "%uh %02um %02us", seconds / 3600, (seconds % 3600) / 60, seconds % 60);
        else
            snprintf(buf, len, "%um %02us", seconds / 60, seconds % 60);
    }

    void ShowNormalStats(Player* player, Creature* creature)
    {
        player->PlayerTalkClass->ClearMenus();
        PlayerStats st = sDungeonMasterMgr->GetPlayerStats(player->GetGUID());
        auto chat = ChatHandler(player->GetSession());
        char buf[256];

        chat.SendSysMessage("|cFFFFD700═══════════ Статистика забегов ═══════════|r");

        snprintf(buf, sizeof(buf), "  Забегов: |cFFFFFFFF%u|r  —  Завершено: |cFF00FF00%u|r  —  Провалено: |cFFFF0000%u|r",
            st.TotalRuns, st.CompletedRuns, st.FailedRuns);
        chat.SendSysMessage(buf);

        if (st.TotalRuns > 0)
        {
            float winRate = st.CompletedRuns * 100.0f / st.TotalRuns;
            snprintf(buf, sizeof(buf), "  Процент побед: |cFFFFD700%.1f%%|r", winRate);
            chat.SendSysMessage(buf);
        }

        chat.SendSysMessage(" ");

        snprintf(buf, sizeof(buf), "  Убито мобов: |cFFFFFFFF%u|r  —  Убито боссов: |cFFFFFFFF%u|r",
            st.TotalMobsKilled, st.TotalBossesKilled);
        chat.SendSysMessage(buf);

        snprintf(buf, sizeof(buf), "  Смертей: |cFFFF0000%u|r", st.TotalDeaths);
        chat.SendSysMessage(buf);

        if (st.TotalDeaths > 0 && st.TotalMobsKilled > 0)
        {
            float kd = static_cast<float>(st.TotalMobsKilled + st.TotalBossesKilled) / st.TotalDeaths;
            snprintf(buf, sizeof(buf), "  K/D: |cFFFFD700%.1f|r", kd);
            chat.SendSysMessage(buf);
        }

        if (st.FastestClear > 0)
        {
            char timeBuf[64];
            FormatTime(st.FastestClear, timeBuf, sizeof(timeBuf));
            snprintf(buf, sizeof(buf), "  Лучшее время: |cFF00FFFF%s|r", timeBuf);
            chat.SendSysMessage(buf);
        }

        chat.SendSysMessage("|cFFFFD700══════════════════════════════════════════|r");

        AddGossipItemFor(player, GOSSIP_ICON_TABARD, "|cFFFFD700Рейтинги|r",
            GOSSIP_SENDER_MAIN, GOSSIP_ACTION_BOARD_MENU);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "|cFFFF0000<< Назад|r",
            GOSSIP_SENDER_MAIN, GOSSIP_ACTION_STATS_MENU);
        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
    }

    void ShowRoguelikeStats(Player* player, Creature* creature)
    {
        player->PlayerTalkClass->ClearMenus();
        RoguelikePlayerStats st = sRoguelikeMgr->GetRoguelikePlayerStats(player->GetGUID());
        auto chat = ChatHandler(player->GetSession());
        char buf[256];

        chat.SendSysMessage("|cFF00FFFF═══════════ Roguelike статистика ═══════════|r");

        snprintf(buf, sizeof(buf), "  Всего забегов: |cFFFFFFFF%u|r", st.TotalRuns);
        chat.SendSysMessage(buf);

        snprintf(buf, sizeof(buf), "  Макс. тир: |cFFFFD700%u|r  —  Макс. этажей: |cFFFFD700%u|r",
            st.HighestTier, st.MostFloorsCleared);
        chat.SendSysMessage(buf);

        snprintf(buf, sizeof(buf), "  Всего этажей пройдено: |cFFFFFFFF%u|r", st.TotalFloorsCleared);
        chat.SendSysMessage(buf);

        chat.SendSysMessage(" ");

        snprintf(buf, sizeof(buf), "  Убито мобов: |cFFFFFFFF%u|r  —  Убито боссов: |cFFFFFFFF%u|r",
            st.TotalMobsKilled, st.TotalBossesKilled);
        chat.SendSysMessage(buf);

        snprintf(buf, sizeof(buf), "  Смертей: |cFFFF0000%u|r", st.TotalDeaths);
        chat.SendSysMessage(buf);

        if (st.TotalDeaths > 0 && (st.TotalMobsKilled + st.TotalBossesKilled) > 0)
        {
            float kd = static_cast<float>(st.TotalMobsKilled + st.TotalBossesKilled) / st.TotalDeaths;
            snprintf(buf, sizeof(buf), "  K/D: |cFFFFD700%.1f|r", kd);
            chat.SendSysMessage(buf);
        }

        if (st.TotalRuns > 0)
        {
            float avgFloors = static_cast<float>(st.TotalFloorsCleared) / st.TotalRuns;
            snprintf(buf, sizeof(buf), "  Среднее этажей/забег: |cFF00FFFF%.1f|r", avgFloors);
            chat.SendSysMessage(buf);
        }

        if (st.LongestRunTime > 0)
        {
            char timeBuf[64];
            FormatTime(st.LongestRunTime, timeBuf, sizeof(timeBuf));
            snprintf(buf, sizeof(buf), "  Самый долгий забег: |cFF00FFFF%s|r", timeBuf);
            chat.SendSysMessage(buf);
        }

        chat.SendSysMessage("|cFF00FFFF══════════════════════════════════════════|r");

        AddGossipItemFor(player, GOSSIP_ICON_TABARD, "|cFFFFD700Рейтинги|r",
            GOSSIP_SENDER_MAIN, GOSSIP_ACTION_BOARD_MENU);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "|cFFFF0000<< Назад|r",
            GOSSIP_SENDER_MAIN, GOSSIP_ACTION_STATS_MENU);
        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
    }

    // ---- Leaderboard Hub ----

    void ShowBoardMenu(Player* player, Creature* creature)
    {
        player->PlayerTalkClass->ClearMenus();
        AddGossipItemFor(player, GOSSIP_ICON_TABARD,
            "|cFFFFD700Обычные забеги — Рекорды скорости|r",
            GOSSIP_SENDER_MAIN, GOSSIP_ACTION_BOARD_NORMAL);
        if (sDMConfig->IsRoguelikeEnabled())
        {
            AddGossipItemFor(player, GOSSIP_ICON_TABARD,
                "|cFF00FFFFRoguelike — Наивысший тир|r",
                GOSSIP_SENDER_MAIN, GOSSIP_ACTION_BOARD_RL_TIER);
            AddGossipItemFor(player, GOSSIP_ICON_TABARD,
                "|cFF00FFFFRoguelike — Больше всего этажей|r",
                GOSSIP_SENDER_MAIN, GOSSIP_ACTION_BOARD_RL_FLOORS);
        }
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "|cFFFF0000<< Назад|r",
            GOSSIP_SENDER_MAIN, GOSSIP_ACTION_STATS_MENU);
        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
    }

    void ShowNormalLeaderboard(Player* player, Creature* creature)
    {
        player->PlayerTalkClass->ClearMenus();

        auto entries = sDungeonMasterMgr->GetOverallLeaderboard(10);
        auto chat = ChatHandler(player->GetSession());

        chat.SendSysMessage("|cFFFFD700═══════ Обычные забеги — Рекорды скорости ═══════|r");

        if (entries.empty())
        {
            chat.SendSysMessage("  |cFF808080Ещё нет записанных забегов.|r");
        }
        else
        {
            uint32 rank = 0;
            uint32 myGuid = player->GetGUID().GetCounter();
            for (const auto& e : entries)
            {
                ++rank;
                char timeBuf[64];
                FormatTime(e.ClearTime, timeBuf, sizeof(timeBuf));

                const DifficultyTier* diff = sDMConfig->GetDifficulty(e.DifficultyId);
                const DungeonInfo* dg = sDMConfig->GetDungeon(e.MapId);

                bool isMe = (e.Guid == myGuid);
                char buf[384];
                snprintf(buf, sizeof(buf),
                    "  %s#%u%s |cFFFFFFFF%s|r — |cFF00FFFF%s|r — %s (%s) %uP%s%s",
                    isMe ? "|cFF00FF00" : "|cFFFFD700",
                    rank,
                    isMe ? "|r" : "|r",
                    e.CharName.c_str(),
                    timeBuf,
                    dg ? dg->Name.c_str() : "?",
                    diff ? diff->Name.c_str() : "?",
                    e.PartySize,
                    e.Scaled ? " |cFF00FF00[Масштаб]|r" : "",
                    isMe ? " |cFF00FF00<< ВЫ|r" : "");
                chat.SendSysMessage(buf);
            }
        }

        chat.SendSysMessage("|cFFFFD700════════════════════════════════════════════════|r");
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "|cFFFF0000<< Назад|r",
            GOSSIP_SENDER_MAIN, GOSSIP_ACTION_BOARD_MENU);
        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
    }

    void ShowRoguelikeLeaderboard(Player* player, Creature* creature, bool sortByFloors)
    {
        player->PlayerTalkClass->ClearMenus();

        auto entries = sRoguelikeMgr->GetRoguelikeLeaderboard(10, sortByFloors);
        auto chat = ChatHandler(player->GetSession());

        if (sortByFloors)
            chat.SendSysMessage("|cFF00FFFF═══════ Roguelike — Больше всего этажей ═══════|r");
        else
            chat.SendSysMessage("|cFF00FFFF═══════ Roguelike — Наивысший тир ═══════|r");

        if (entries.empty())
            chat.SendSysMessage("  |cFF808080Ещё нет roguelike забегов.|r");
        else
        {
            uint32 rank = 0;
            uint32 myGuid = player->GetGUID().GetCounter();
            for (const auto& e : entries)
            {
                ++rank;
                char timeBuf[64];
                FormatTime(e.RunDuration, timeBuf, sizeof(timeBuf));

                bool isMe = (e.Guid == myGuid);
                char buf[384];
                snprintf(buf, sizeof(buf),
                    "  %s#%u%s |cFFFFFFFF%s|r — Тир |cFFFFD700%u|r — |cFF00FF00%u|r подз. — |cFF00FFFF%s|r — %u убийств — %uP%s",
                    isMe ? "|cFF00FF00" : "|cFFFFD700",
                    rank,
                    isMe ? "|r" : "|r",
                    e.CharName.c_str(),
                    e.TierReached,
                    e.DungeonsCleared,
                    timeBuf,
                    e.TotalKills,
                    e.PartySize,
                    isMe ? " |cFF00FF00<< ВЫ|r" : "");
                chat.SendSysMessage(buf);
            }
        }

        chat.SendSysMessage("|cFF00FFFF══════════════════════════════════════════════|r");
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "|cFFFF0000<< Назад|r",
            GOSSIP_SENDER_MAIN, GOSSIP_ACTION_BOARD_MENU);
        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
    }

    // ---- Roguelike Menus ----

    void ShowRoguelikeDifficultyMenu(Player* player, Creature* creature)
    {
        player->PlayerTalkClass->ClearMenus();
        uint8 lvl = player->GetLevel();

        ChatHandler(player->GetSession()).SendSysMessage(
            "|cFF00FFFF========== Roguelike режим ==========|r");
        ChatHandler(player->GetSession()).SendSysMessage(
            "|cFFFFFFFFПроходите подземелья одно за другим. Каждое прохождение повышает тир.|r");
        ChatHandler(player->GetSession()).SendSysMessage(
            "|cFFFFFFFFВраги становятся сильнее, но вы получаете мощные усиления.|r");
        ChatHandler(player->GetSession()).SendSysMessage(
            "|cFFFF0000Один вайп — и забег завершён!|r");
        ChatHandler(player->GetSession()).SendSysMessage(
            "|cFF00FFFF========================================|r");

        for (const auto& d : sDMConfig->GetDifficulties())
        {
            char buf[256];
            if (!d.IsValidForLevel(lvl))
                snprintf(buf, sizeof(buf), "|cFF808080%s|r (Ур %u-%u) - |cFFFF0000Требуется %u+|r",
                    d.Name.c_str(), d.MinLevel, d.MaxLevel, d.MinLevel);
            else
                snprintf(buf, sizeof(buf), "|cFF00FFFF%s|r (Ур %u-%u)",
                    d.Name.c_str(), d.MinLevel, d.MaxLevel);

            AddGossipItemFor(player,
                d.IsValidForLevel(lvl) ? GOSSIP_ICON_BATTLE : GOSSIP_ICON_CHAT,
                buf, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_DIFF_BASE + d.Id);
        }
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "|cFFFF0000<< Назад|r",
            GOSSIP_SENDER_MAIN, GOSSIP_ACTION_CANCEL);
        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
    }

    void ShowRoguelikeScalingMenu(Player* player, Creature* creature)
    {
        player->PlayerTalkClass->ClearMenus();

        uint8 partyLevel = sDungeonMasterMgr->ComputeEffectiveLevel(player);

        char buf1[256], buf2[256];
        snprintf(buf1, sizeof(buf1),
            "|cFF00FF00Масштаб по уровню группы|r (Ур %u)", partyLevel);
        snprintf(buf2, sizeof(buf2),
            "|cFFFFD700Сложность подземелья|r — Оригинальные диапазоны уровней");

        AddGossipItemFor(player, GOSSIP_ICON_BATTLE, buf1,
            GOSSIP_SENDER_MAIN, GOSSIP_ACTION_ROGUELIKE_SCALE_PARTY);
        AddGossipItemFor(player, GOSSIP_ICON_INTERACT_1, buf2,
            GOSSIP_SENDER_MAIN, GOSSIP_ACTION_ROGUELIKE_SCALE_TIER);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "|cFFFF0000<< Назад|r",
            GOSSIP_SENDER_MAIN, GOSSIP_ACTION_CANCEL);
        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
    }

    void ShowRoguelikeThemeMenu(Player* player, Creature* creature)
    {
        player->PlayerTalkClass->ClearMenus();
        for (const auto& t : sDMConfig->GetThemes())
            AddGossipItemFor(player, GOSSIP_ICON_BATTLE, t.Name,
                GOSSIP_SENDER_MAIN, GOSSIP_ACTION_ROGUELIKE_THEME + t.Id);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "|cFFFF0000<< Назад|r",
            GOSSIP_SENDER_MAIN, GOSSIP_ACTION_CANCEL);
        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
    }

    void StartRoguelike(Player* player, Creature* /*creature*/)
    {
        player->PlayerTalkClass->SendCloseGossip();

        PlayerDMSelection sel;
        { std::lock_guard<std::mutex> lk(sSelMutex);
          auto it = sSelections.find(player->GetGUID());
          if (it == sSelections.end()) {
              ChatHandler(player->GetSession()).SendSysMessage(
                  "|cFFFF0000[Roguelike]|r Выбор истёк. Попробуйте снова.");
              return; }
          sel = it->second;
          sSelections.erase(it); }

        const DifficultyTier* diff = sDMConfig->GetDifficulty(sel.DifficultyId);
        if (!diff || !diff->IsValidForLevel(player->GetLevel()))
        {
            ChatHandler(player->GetSession()).SendSysMessage(
                "|cFFFF0000[Roguelike]|r Требования по уровню не выполнены!");
            return;
        }

        uint32 runId = 0; // unused, StartRun returns bool
        if (!sRoguelikeMgr->StartRun(player, sel.DifficultyId,
            sel.ThemeId, sel.ScaleToParty))
        {
            ChatHandler(player->GetSession()).SendSysMessage(
                "|cFFFF0000[Roguelike]|r Не удалось запустить roguelike забег!");
            return;
        }

        ChatHandler(player->GetSession()).SendSysMessage(
            "|cFF00FFFF[Roguelike]|r Забег начат! Проходите подземелья для прогресса. Удачи!");
    }

    // ---- Launch ----
    void StartChallenge(Player* player, Creature* /*creature*/)
    {
        player->PlayerTalkClass->SendCloseGossip();

        PlayerDMSelection sel;
        { std::lock_guard<std::mutex> lk(sSelMutex);
          auto it = sSelections.find(player->GetGUID());
          if (it == sSelections.end()) {
              ChatHandler(player->GetSession()).SendSysMessage("|cFFFF0000[Мастер подземелий]|r Выбор истёк. Попробуйте снова.");
              return; }
          sel = it->second;
          sSelections.erase(it); }

        const DifficultyTier* diff = sDMConfig->GetDifficulty(sel.DifficultyId);
        if (!diff || !diff->IsValidForLevel(player->GetLevel()))
        {
            ChatHandler(player->GetSession()).SendSysMessage("|cFFFF0000[Мастер подземелий]|r Требования по уровню не выполнены!");
            return;
        }

        // Resolve random dungeon
        uint32 mapId = sel.MapId;
        if (mapId == 0)
        {
            auto dgs = sDMConfig->GetDungeonsForLevel(diff->MinLevel, diff->MaxLevel);
            if (dgs.empty()) {
                ChatHandler(player->GetSession()).SendSysMessage("|cFFFF0000[Мастер подземелий]|r Нет доступных подземелий!");
                return; }
            static thread_local std::mt19937 rng{ std::random_device{}() };
            mapId = dgs[std::uniform_int_distribution<size_t>(0, dgs.size()-1)(rng)]->MapId;
        }

        Session* s = sDungeonMasterMgr->CreateSession(player, sel.DifficultyId, sel.ThemeId, mapId, sel.ScaleToParty);
        if (!s) {
            ChatHandler(player->GetSession()).SendSysMessage("|cFFFF0000[Мастер подземелий]|r Не удалось создать сессию!");
            return; }

        if (!sDungeonMasterMgr->StartDungeon(s)) {
            ChatHandler(player->GetSession()).SendSysMessage("|cFFFF0000[Мастер подземелий]|r Не удалось инициализировать подземелье!");
            sDungeonMasterMgr->AbandonSession(s->SessionId); return; }

        if (!sDungeonMasterMgr->TeleportPartyIn(s)) {
            ChatHandler(player->GetSession()).SendSysMessage("|cFFFF0000[Мастер подземелий]|r Телепортация не удалась!");
            sDungeonMasterMgr->AbandonSession(s->SessionId); return; }

        if (sDMConfig->ShouldAnnounceCompletion())
        {
            const Theme* theme = sDMConfig->GetTheme(sel.ThemeId);
            const DungeonInfo* dg = sDMConfig->GetDungeon(mapId);
            char buf[256];
            snprintf(buf, sizeof(buf),
                "|cFF00FF00[Мастер подземелий]|r |cFFFFFFFF%s|r начал испытание |cFFFFD700%s|r |cFF00FFFF%s|r!",
                player->GetName().c_str(), diff->Name.c_str(),
                theme ? theme->Name.c_str() : "Случайная");

            char detail[256];
            snprintf(detail, sizeof(detail),
                "|cFFFFD700[Мастер подземелий]|r Сложность: |cFF00FF00%s|r  Тема: |cFF00FF00%s|r  Подземелье: |cFF00FF00%s|r  Масштаб: |cFF00FF00%s|r",
                diff->Name.c_str(),
                theme ? theme->Name.c_str() : "Случайная",
                dg ? dg->Name.c_str() : "Случайное",
                sel.ScaleToParty ? "Уровень группы" : "Сложность подземелья");

            // Broadcast to ALL party members
            for (const auto& pd : s->Players)
                if (Player* p = ObjectAccessor::FindPlayer(pd.PlayerGuid))
                {
                    ChatHandler(p->GetSession()).SendSysMessage(buf);
                    ChatHandler(p->GetSession()).SendSysMessage(detail);
                }
        }
    }
};

void AddSC_npc_dungeon_master()
{
    new npc_dungeon_master();
}
