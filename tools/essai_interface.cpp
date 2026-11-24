// Essai de l'interface sans le jeu : la fenêtre transparente et la page, sur le
// bureau, nourries d'un état fixe (tools/etat_essai.json, données réelles du
// premier essai à Paris). Sert à vérifier transparence, souris, F9 et encart.
//
//   EssaiInterface.exe [--ferme]
//
// Les images viennent du cache du jeu (Retail\Roulette\cache), s'il existe.

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <Windows.h>

#include "Fenetre.h"

namespace {
    std::filesystem::path Racine() {
        wchar_t s_Exe[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, s_Exe, MAX_PATH);
        // build/EssaiInterface.exe -> racine du projet
        return std::filesystem::path(s_Exe).parent_path().parent_path();
    }

    std::string Lire(const std::filesystem::path& p_Fichier) {
        std::ifstream s_Flux(p_Fichier, std::ios::binary);
        std::stringstream s_Contenu;
        s_Contenu << s_Flux.rdbuf();
        return s_Contenu.str();
    }
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR p_Ligne, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    const auto s_Racine = Racine();
    const std::string s_Etat = Lire(s_Racine / "tools" / "etat_essai.json");
    const bool s_Ferme = wcsstr(p_Ligne, L"--ferme") != nullptr;

    Fenetre::SParametres s_P;
    s_P.m_DossierUi = (s_Racine / "ui").wstring();
    const std::filesystem::path s_Retail = L"C:\\Program Files (x86)\\Steam\\steamapps\\common\\HITMAN 3\\Retail";
    const auto s_Cache = s_Retail / "Roulette" / "cache";
    s_P.m_DossierCache = (std::filesystem::exists(s_Cache) ? s_Cache : s_Retail / "mods" / "Roulette" / "cache").wstring();
    s_P.m_DossierDonnees = (std::filesystem::temp_directory_path() / "RouletteEssaiWebView").wstring();
    s_P.m_OutilsDeveloppement = true;
    s_P.m_Etat = [s_Etat](bool p_Ouverte) {
        // L'état du fichier, précédé de « ouverte ».
        const auto s_Accolade = s_Etat.find('{');
        if (s_Accolade == std::string::npos) return std::string("{}");
        return std::string("{\"ouverte\":") + (p_Ouverte ? "true," : "false,") + s_Etat.substr(s_Accolade + 1);
    };

    static Fenetre s_Fenetre;
    s_Fenetre.Demarrer(std::move(s_P));

    Sleep(1500);
    if (!s_Ferme) s_Fenetre.Ouvrir(true);

    // Le processus vit tant que la fenêtre vit (l'essai se coupe en tuant le processus).
    MSG s_Message;
    while (GetMessageW(&s_Message, nullptr, 0, 0) > 0) {
        DispatchMessageW(&s_Message);
    }

    return 0;
}
