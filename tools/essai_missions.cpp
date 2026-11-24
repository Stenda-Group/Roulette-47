// Essai hors du jeu sur TOUTES les missions : chaque contrat d'un dossier
// (contrats des missions au format du jeu, « Data » + « Metadata ») passe par
// la lecture des cibles, la roulette et le masquage des objectifs du jeu, comme
// à la réception du contrat en jeu.
//
//   EssaiMissions.exe <dossier des contrats>
//
// Contrôle, mission par mission : des cibles trouvées, 1 à 3 tirées, chacune
// avec son objectif, plus aucun objectif du jeu visible, la sortie liée aux
// cibles de la roulette.

#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <set>

#include <nlohmann/json.hpp>

#include "Contrat.h"
#include "Menus.h"
#include "Tirage.h"

namespace fs = std::filesystem;
using nlohmann::json;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "EssaiMissions <dossier des contrats>\n";
        return 2;
    }

    // Les types que la roulette joue (Menus.cpp) ; jamais fugitives ni escalades.
    const std::set<std::string> s_Types = { "mission", "flashback", "orbis", "campaign", "bonus" };

    std::mt19937 s_Hasard(2211);
    SReglages s_Reglages;
    s_Reglages.m_Mode = EMode::Freestyle; // essais écrits avant les modes
    s_Reglages.m_NbComplications = 2;

    int s_Missions = 0, s_SansCible = 0, s_Defauts = 0;

    for (const auto& f : fs::recursive_directory_iterator(argv[1])) {
        if (!f.is_regular_file() || f.path().extension() != ".json") continue;

        std::ifstream s_Flux(f.path(), std::ios::binary);
        auto c = json::parse(s_Flux, nullptr, false);
        if (c.is_discarded() || !c.is_object() || !c.contains("Data") || !c.contains("Metadata")) continue;

        const auto& M = c["Metadata"];
        const auto s_Type = M.value("Type", "");
        if (!s_Types.contains(s_Type)) continue;
        auto& D = c["Data"];
        if (!D.contains("Objectives") || !D["Objectives"].is_array()) continue;

        ++s_Missions;
        const auto s_Nom = M.value("Location", "?") + " " + M.value("Title", "?") + " [" + s_Type + "]";

        const auto s_Ids = CiblesDesObjectifs(D["Objectives"]);
        if (s_Ids.empty()) {
            ++s_SansCible;
            std::cout << "  SANS CIBLE  " << s_Nom << " (" << f.path().filename().string() << ")\n";
            continue;
        }

        STirage t;
        for (const auto& id : s_Ids) {
            SObjectifCible x;
            x.m_Id = id;
            x.m_Nom = id.substr(0, 8);
            t.m_Cibles.push_back(x);
        }
        STenue s_Costume;
        s_Costume.m_Id = "costume_47";
        s_Costume.m_Nom = "Costume de 47";
        s_Costume.m_EstCostume = true;
        t.m_Tenues.push_back(s_Costume);

        ChoisirCibles(t, s_Reglages, s_Hasard);
        Tirer(t, s_Reglages, s_Hasard);

        const auto s_AvantSortie = D.contains("EnableExits") ? D["EnableExits"].dump() : std::string("(aucune)");
        const auto s_ObjectifsDuJeu = D["Objectives"].size();

        std::vector<std::string> s_IdsCibles;
        const auto s_Ajout = ObjectifsRoulette(t, s_Hasard, true, &s_IdsCibles);
        const int s_Masques = MasquerObjectifsDuJeu(D, s_IdsCibles);
        for (const auto& o : s_Ajout) D["Objectives"].push_back(o);

        // Contrôles.
        std::string s_Defaut;
        if (t.m_Cibles.empty() || t.m_Cibles.size() > 3) s_Defaut += " nombre de cibles;";
        if (s_IdsCibles.size() != t.m_Cibles.size()) s_Defaut += " cible sans objectif;";

        int s_Visibles = 0;
        std::set<std::string> s_Categories;
        for (size_t i = 0; i < s_ObjectifsDuJeu; ++i) {
            const auto& o = D["Objectives"][i];
            const auto s_Cat = o.value("Category", std::string("(aucune)"));
            s_Categories.insert(s_Cat);
            // Ancien format sans « Category » (« Primary » : true) : il compterait encore.
            if (o.value("Primary", false) && !o.value("IsHidden", false)) ++s_Visibles;
            if (s_Cat == "primary" && !o.value("IsHidden", false)) ++s_Visibles;
        }
        if (s_Visibles) s_Defaut += " " + std::to_string(s_Visibles) + " objectif(s) du jeu encore primaires;";
        if (!D["EnableExits"].contains("$and")) s_Defaut += " sortie non liée;";

        std::string s_Cats;
        for (const auto& x : s_Categories) s_Cats += x + " ";

        std::cout << (s_Defaut.empty() ? "  ok          " : "  DÉFAUT      ") << s_Nom << " : " << s_Ids.size() << " cible(s) possible(s), "
                  << t.m_Cibles.size() << " tirée(s), " << s_Masques << "/" << s_ObjectifsDuJeu << " masqué(s) [" << s_Cats << "]"
                  << s_Defaut << "\n";
        if (!s_Defaut.empty()) ++s_Defauts;
        (void)s_AvantSortie;
    }

    std::cout << "\n" << s_Missions << " missions, " << s_SansCible << " sans cible lue, " << s_Defauts << " défaut(s)\n";
    return s_Defauts || s_SansCible ? 1 : 0;
}
