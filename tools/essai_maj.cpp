// Essai de la mise à jour (MiseAJour.cpp).
//
//   EssaiMaj.exe            contrôles hors ligne (versions, somme des notes, chemins)
//   EssaiMaj.exe --reseau   en plus, l'installation réelle de la dernière release,
//                           à lancer depuis <dossier>\Retail\Roulette47\ (copié là
//                           par l'essai : jamais dans le vrai dossier du jeu) avec
//                           de fausses version.dll et Roulette47.dll à côté.
//
// L'essai réseau se fait passer pour la v0.0.1 : toute release est plus récente.
// Il charge la fausse Roulette47.dll avant d'installer, comme le jeu charge la
// vraie : c'est le cas qui compte (une DLL chargée ne s'écrase pas, elle se renomme).

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

#include <Windows.h>

#include "MiseAJour.h"

namespace fs = std::filesystem;

int main(int argc, char** argv) {
    int s_Erreurs = 0, s_Controles = 0;
    const auto Verifier = [&](const std::string& p_Nom, bool p_Ok) {
        ++s_Controles;
        if (!p_Ok) { ++s_Erreurs; std::cout << "ERREUR  " << p_Nom << "\n"; }
        else std::cout << "ok      " << p_Nom << "\n";
    };

    using namespace MiseAJour;
    Verifier("v0.24 > 0.23", PlusRecente("v0.24", "0.23"));
    Verifier("v0.23 = 0.23", !PlusRecente("v0.23", "0.23"));
    Verifier("v0.22 < 0.23", !PlusRecente("v0.22", "0.23"));
    Verifier("v0.100 > 0.99", PlusRecente("v0.100", "0.99"));
    Verifier("v1.0 > 0.99", PlusRecente("v1.0", "0.99"));
    Verifier("v0.23.1 > 0.23", PlusRecente("v0.23.1", "0.23"));
    Verifier("étiquette vide", !PlusRecente("", "0.23"));

    const std::string s_Notes = "## Fixed\n\n- x\n\n## Checksum\n\n`roulette47-v0.23.zip` — SHA-256 "
                                "`c1b51c15cb5c587886e881be2877d44910bcef29edd0dec738c9396155584c39`\n";
    Verifier("somme lue dans les notes", SommeDesNotes(s_Notes, "roulette47-v0.23.zip") == "c1b51c15cb5c587886e881be2877d44910bcef29edd0dec738c9396155584c39");
    Verifier("somme d'une autre archive", SommeDesNotes(s_Notes, "roulette47-v0.24.zip").empty());

    Verifier("chemin : DLL", CheminAccepte("Retail/Roulette47/Roulette47.dll") == "Roulette47/Roulette47.dll");
    Verifier("chemin : chargeur", CheminAccepte("Retail/version.dll") == "version.dll");
    Verifier("chemin : interface", CheminAccepte("Retail/Roulette47/ui/roulette.js") == "Roulette47/ui/roulette.js");
    Verifier("chemin : remontée refusée", CheminAccepte("Retail/Roulette47/../HITMAN3.exe").empty());
    Verifier("chemin : hors du mod refusé", CheminAccepte("Retail/HITMAN3.exe").empty());
    Verifier("chemin : absolu refusé", CheminAccepte("Retail/C:/Windows/x.dll").empty());
    Verifier("chemin : hors de Retail refusé", CheminAccepte("Runtime/chunk0.rpkg").empty());

    if (argc > 1 && std::string(argv[1]) == "--reseau") {
        wchar_t s_Exe[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, s_Exe, MAX_PATH);
        const fs::path s_Mod = fs::path(s_Exe).parent_path();
        const fs::path s_Retail = s_Mod.parent_path();
        if (s_Mod.filename() != L"Roulette47" || s_Retail.filename() != L"Retail" ||
            fs::exists(s_Retail / L"HITMAN3.exe")) {
            std::cout << "À lancer depuis une copie <dossier>\\Retail\\Roulette47\\, jamais dans le jeu.\n";
            return 2;
        }

        const auto s_TailleAvant = fs::file_size(s_Mod / L"Roulette47.dll");
        const HMODULE s_Chargee = LoadLibraryW((s_Mod / L"Roulette47.dll").c_str());
        Verifier("fausse Roulette47.dll chargée", s_Chargee != nullptr);

        ForcerVersion("0.0.1");
        Verifier("(" + std::string(Version()) + ")", true);
        MiseAJour::Verifier([] {});
        const auto Attendre = [](std::initializer_list<const char*> p_Fins, int p_Secondes) {
            for (int i = 0; i < p_Secondes * 10; ++i) {
                const auto e = Etat()["etat"].get<std::string>();
                for (const char* f : p_Fins) if (e == f) return e;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            return Etat()["etat"].get<std::string>();
        };

        const auto s_Annonce = Attendre({ "disponible", "erreur" }, 40);
        const auto s_Version = Etat()["version"].get<std::string>();
        Verifier("release annoncée (" + s_Version + ")", s_Annonce == "disponible" && !s_Version.empty());

        Installer();
        const auto s_Fin = Attendre({ "installee", "erreur" }, 120);
        Verifier("installation : " + s_Fin + " " + Etat()["erreur"].get<std::string>(), s_Fin == "installee");

        Verifier("nouvelle Roulette47.dll posée", fs::file_size(s_Mod / L"Roulette47.dll") != s_TailleAvant);
        Verifier("l'ancienne, chargée, renommée", fs::exists(s_Mod / L"Roulette47.dll.ancien"));
        Verifier("version.dll remplacée", fs::exists(s_Retail / L"version.dll.ancien") && fs::file_size(s_Retail / L"version.dll") > 50'000);
        Verifier("interface en attente dans update/", fs::exists(s_Mod / L"update" / L"ui" / L"index.html"));
        Verifier("rien d'autre écrit dans Retail", !fs::exists(s_Retail / L"Roulette47" / L"Roulette47.dll.nouveau"));
        {
            std::ifstream f(s_Mod / L"update" / L"VERSION");
            std::string v;
            std::getline(f, v);
            Verifier("update/VERSION = " + v, v == s_Version);
        }

        // Lancement suivant : la nouvelle DLL range update/ et efface les .ancien.
        FreeLibrary(s_Chargee);
        ForcerVersion(s_Version);
        AppliquerEnAttente();
        Verifier("interface rangée", fs::exists(s_Mod / L"ui" / L"index.html") && fs::exists(s_Mod / L"initial_locations"));
        Verifier("update/ retiré", !fs::exists(s_Mod / L"update"));
        Verifier(".ancien effacés", !fs::exists(s_Mod / L"Roulette47.dll.ancien") && !fs::exists(s_Retail / L"version.dll.ancien"));
    }

    std::cout << s_Controles << " contrôles, " << s_Erreurs << " erreur(s).\n";
    std::cout << (s_Erreurs == 0 ? "Tout est bon.\n" : "À REPRENDRE.\n");
    return s_Erreurs == 0 ? 0 : 1;
}
