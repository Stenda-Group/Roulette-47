#include "Droits.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <mutex>

#include <Windows.h>

#include "Diagnostic.h"

namespace {
    using json = nlohmann::json;

    // Droit -> numéro de DLC Steam, tel que HITMAN3.exe l'écrit (build 3.300,
    // tools/droits_steam.py). Deux séries : le jeu prend la première quand il
    // tourne sous l'application 1659040 (HITMAN 3) ou 1847520, la seconde sous
    // 1172010 ou 1850750. Les lieux (LOCATION_…) n'y sont pas : le serveur les
    // verrouille déjà pour chaque compte.
    struct SDroitSteam {
        const char* m_Droit;
        uint32_t m_Dlc;        // série de HITMAN 3 (1659040, 1847520)
        uint32_t m_DlcAutre;   // série de 1172010, 1850750
    };

    constexpr SDroitSteam c_Table[] = {
        { "H3_EXPANSION",             1829591, 1659020 },
        { "H3_DEADLYSINS_GREED",      1829580, 1659023 },
        { "H3_DEADLYSINS_PRIDE",      1829581, 1659024 },
        { "H3_DEADLYSINS_SLOTH",      1829582, 1659025 },
        { "H3_DEADLYSINS_LUST",       1829583, 1659026 },
        { "H3_DEADLYSINS_GLUTTONY",   1829584, 1659027 },
        { "H3_DEADLYSINS_WRATH",      1829586, 1659028 },
        { "H3_DEADLYSINS_ENVY",       1829585, 1659029 },
        { "H3_VANITY_CONCRETEART",    2184790, 2184890 },
        { "H3_VANITY_MAKESHIFTSCRAP", 2184791, 2184891 },
        { "H3_SARAJEVO_SIX",          2475260, 2476970 },
        { "H3_ET_SAMBUCA",            2828470, 2828460 },
        { "H3_ET_PENICILLIN",         2973650, 2973530 },
        { "H3_ET_TOMORROWLAND",       3110360, 3110380 },
        { "H3_ET_LAMBIC",             3254350, 3254340 },
        { "H3_ET_FRENCHMARTINI",      3711140, 3711120 },
        { "H3_ET_BAIJU",              3957470, 3957400 },
        { "H3_ET_BELLINI",            4097630, 4097610 },
        { "H3_ET_FILUR",              4328240, 4232040 },
        { "H3_VANITY_SPORT",          4542910, 4542920 },
        { "H3_ET_POMADA",             4621250, 4621240 },
        { "H3_ET_GREENTINI",          4911210, 4911230 },
        { "H3_VANITY_EG04",           4944070, 4944060 },
        { "H3_ET_SHAMROCK",           5235590, 5235640 },
        { "H3_VANITY_EG05",           5235600, 5235650 },
        { "H3_VANITY_EG06",           5494910, 5494890 },
    };

    std::mutex g_Verrou;
    bool g_Lu = false;                      // Steam interrogé une fois, avec succès
    std::map<std::string, bool> g_Possedes; // droit de la table -> possédé

    // Les fonctions « à plat » de steam_api64.dll, que le jeu a déjà chargée et
    // initialisée. Plusieurs versions d'interface : la première trouvée.
    template <typename T>
    T Fonction(HMODULE p_Steam, std::initializer_list<const char*> p_Noms) {
        for (const char* n : p_Noms) {
            if (const auto f = GetProcAddress(p_Steam, n)) return reinterpret_cast<T>(f);
        }
        return nullptr;
    }

    // Interroge Steam pour toute la table. Faux si Steam n'est pas (encore) là.
    bool LireSteam() {
        const HMODULE s_Steam = GetModuleHandleW(L"steam_api64.dll");
        if (!s_Steam) return false;

        using FnInterface = void* (*)();
        using FnAppId = uint32_t (*)(void*);
        using FnDlc = bool (*)(void*, uint32_t);

        const auto s_Apps = Fonction<FnInterface>(s_Steam, { "SteamAPI_SteamApps_v009", "SteamAPI_SteamApps_v008", "SteamAPI_SteamApps_v010" });
        const auto s_Utils = Fonction<FnInterface>(s_Steam, { "SteamAPI_SteamUtils_v011", "SteamAPI_SteamUtils_v010", "SteamAPI_SteamUtils_v012" });
        const auto s_GetAppId = Fonction<FnAppId>(s_Steam, { "SteamAPI_ISteamUtils_GetAppID" });
        const auto s_DlcInstalle = Fonction<FnDlc>(s_Steam, { "SteamAPI_ISteamApps_BIsDlcInstalled" });
        if (!s_Apps || !s_Utils || !s_GetAppId || !s_DlcInstalle) return false;

        void* const s_IApps = s_Apps();
        void* const s_IUtils = s_Utils();
        if (!s_IApps || !s_IUtils) return false;

        const uint32_t s_Jeu = s_GetAppId(s_IUtils);
        bool s_Serie1;
        if (s_Jeu == 1659040 || s_Jeu == 1847520) s_Serie1 = true;
        else if (s_Jeu == 1172010 || s_Jeu == 1850750) s_Serie1 = false;
        else {
            Diag("DLC : application Steam {} inconnue, aucune mission écartée.", s_Jeu);
            g_Lu = true;
            return true;
        }

        std::string s_Absents;
        for (const auto& d : c_Table) {
            const uint32_t s_Dlc = s_Serie1 ? d.m_Dlc : d.m_DlcAutre;
            const bool s_A = s_DlcInstalle(s_IApps, s_Dlc);
            g_Possedes[d.m_Droit] = s_A;
            if (!s_A) s_Absents += std::string(s_Absents.empty() ? "" : ", ") + d.m_Droit + " (" + std::to_string(s_Dlc) + ")";
        }

        g_Lu = true;
        Diag("DLC (Steam, application {}) : {} sur {} possédés ; absents : {}.", s_Jeu,
             std::ranges::count_if(g_Possedes, [](const auto& p) { return p.second; }), g_Possedes.size(),
             s_Absents.empty() ? std::string("aucun") : s_Absents);
        return true;
    }

    // Les listes de droits d'une mission : sa scène et son contrat.
    const json* Liste(const json& p_Racine, std::initializer_list<const char*> p_Chemin) {
        const json* x = &p_Racine;
        for (const char* c : p_Chemin) {
            if (!x->is_object() || !x->contains(c)) return nullptr;
            x = &(*x)[c];
        }
        return x->is_array() ? x : nullptr;
    }
}

bool DroitPossede(const std::string& p_Droit) {
    std::lock_guard s_Verrou(g_Verrou);
    if (!g_Lu && !LireSteam()) return true;
    const auto s_It = g_Possedes.find(p_Droit);
    return s_It == g_Possedes.end() || s_It->second;
}

bool MissionPossedee(const json& p_Brut, const std::function<bool(const std::string&)>& p_Possede) {
    if (!p_Brut.is_object()) return true;

    // Mission de la page d'un lieu : { SubLocation, UserCentricContract } ; de
    // l'histoire : le même objet sous « Data ».
    const json& B = p_Brut.contains("UserCentricContract") || !p_Brut.contains("Data") ? p_Brut : p_Brut["Data"];

    for (const json* L : { Liste(B, { "SubLocation", "Properties", "Entitlements" }),
                           Liste(B, { "UserCentricContract", "Contract", "Metadata", "Entitlements" }) }) {
        if (!L || L->empty()) continue;
        bool s_Un = false;
        for (const auto& d : *L) {
            if (d.is_string() && p_Possede(d.get<std::string>())) { s_Un = true; break; }
        }
        if (!s_Un) return false;
    }
    return true;
}
