// Essai hors du jeu : applique la roulette au VRAI contrat relevé (Sapienza,
// « Le monde de demain », Retail/Roulette/reponses/039_…json) et écrit le
// résultat, pour relire les objectifs ajoutés avant de les envoyer au jeu.
//
//   EssaiContrat.exe <contrat.json> <sortie.json>
//
// Données réelles seulement : la tenue « Chef du manoir » et le PNJ « Herb
// McGreenwood » (identifiant relevé dans evenements.log) viennent de la partie
// du 22/11/2026.

#include <fstream>
#include <iostream>
#include <random>
#include <sstream>

#include <nlohmann/json.hpp>

#include "Contrat.h"
#include "Http.h"
#include "Tirage.h"

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "EssaiContrat <contrat.json> <sortie.json>\n";
        return 2;
    }

    std::ifstream s_Entree(argv[1], std::ios::binary);
    std::stringstream s_Tampon;
    s_Tampon << s_Entree.rdbuf();
    auto s_Reponse = nlohmann::json::parse(s_Tampon.str());

    STirage s_Tirage;

    for (const auto& o : s_Reponse["Contract"]["Data"]["Objectives"]) {
        if (o.contains("SuccessEvent") && o["SuccessEvent"].value("EventName", "") == "Kill") {
            SObjectifCible c;
            c.m_Id = o["SuccessEvent"]["EventValues"].value("RepositoryId", "");
            c.m_Nom = c.m_Id == "0dfaea51-3c36-4722-9eff-f1e7ef139878" ? "Silvio Caruso" : "Francesca De Santis";
            s_Tirage.m_Cibles.push_back(c);
        }
    }

    SObjectifCible s_Pnj;
    s_Pnj.m_Officielle = false;
    s_Pnj.m_Id = "e246cb7f-783d-484a-8865-1aba8920ccd9";
    s_Pnj.m_Nom = "Herb McGreenwood";
    s_Pnj.m_CleNom = CleNom(s_Pnj.m_Nom);
    s_Tirage.m_Cibles.push_back(s_Pnj);

    STenue s_Chef;
    s_Chef.m_Id = "4c6816d8-4ae7-4161-a971-970055e64b34";
    s_Chef.m_Nom = "Chef du manoir";
    s_Tirage.m_Tenues.push_back(s_Chef);

    STenue s_Costume;
    s_Costume.m_Id = "costume_47";
    s_Costume.m_Nom = "Costume de 47";
    s_Costume.m_EstCostume = true;
    s_Tirage.m_Tenues.push_back(s_Costume);

    SReglages s_Reglages;
    s_Reglages.m_Mode = EMode::Freestyle; // essais écrits avant les modes
    s_Reglages.m_NbComplications = 3;

    std::mt19937 s_Hasard(1234);
    Tirer(s_Tirage, s_Reglages, s_Hasard);

    const auto s_Objectifs = ObjectifsRoulette(s_Tirage, s_Hasard);
    for (const auto& o : s_Objectifs) s_Reponse["Contract"]["Data"]["Objectives"].push_back(o);

    // Aller-retour gzip : ce que le mod rendra au jeu doit se relire à l'identique.
    const auto s_Texte = s_Reponse.dump();
    const auto s_Gzip = EnGzip(s_Texte);
    const auto s_Relu = CorpsEnTexte(reinterpret_cast<const uint8_t*>(s_Gzip.data()), s_Gzip.size());

    std::cout << "objectifs ajoutés : " << s_Objectifs.size() << "\n";
    std::cout << "gzip : " << s_Texte.size() << " -> " << s_Gzip.size() << " octets, relu "
              << (s_Relu == s_Texte ? "IDENTIQUE" : "DIFFERENT") << "\n";

    std::ofstream(argv[2], std::ios::binary) << s_Objectifs.dump(1);
    return s_Relu == s_Texte ? 0 : 1;
}
