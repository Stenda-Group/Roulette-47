// Essai hors du jeu : pour chaque méthode de la roulette, la fiche
// (MethodeRespectee, Tirage.cpp) et l'objectif écrit dans le contrat
// (ConditionMethode, Contrat.cpp) doivent rendre le même verdict sur une
// élimination donnée. Les deux règles sont écrites deux fois ; cet essai les
// garde d'accord.
//
//   EssaiMethodes.exe
//
// Les éliminations essayées reprennent les valeurs que le jeu envoie dans
// l'événement « Kill » (vocabulaire UI_KILL_METHOD_… du jeu, et la corde de
// piano relevée en 3.300 : KillMethodStrict vide, KillMethodBroad « fiberwire »).

#include <iostream>
#include <random>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "Contrat.h"
#include "Tirage.h"

using json = nlohmann::json;

namespace {
    // Évalue les opérateurs qu'écrit Contrat.cpp ($eq, $and, $or, $not) sur
    // les valeurs d'un événement ($Value.<champ>).
    bool Evaluer(const json& p_Condition, const json& p_Valeur) {
        if (p_Condition.contains("$eq")) {
            const auto& a = p_Condition["$eq"];
            const std::string s_Champ = a[0].get<std::string>().substr(std::string("$Value.").size());
            return p_Valeur.contains(s_Champ) && p_Valeur[s_Champ] == a[1];
        }
        if (p_Condition.contains("$and")) {
            for (const auto& c : p_Condition["$and"]) if (!Evaluer(c, p_Valeur)) return false;
            return true;
        }
        if (p_Condition.contains("$or")) {
            for (const auto& c : p_Condition["$or"]) if (Evaluer(c, p_Valeur)) return true;
            return false;
        }
        if (p_Condition.contains("$not")) return !Evaluer(p_Condition["$not"], p_Valeur);
        throw std::runtime_error("opérateur inconnu : " + p_Condition.dump());
    }

    struct SElimination {
        const char* m_Nom;
        const char* m_Stricte;
        const char* m_Large;
        const char* m_Categorie;
    };
}

int main() {
    const std::vector<SElimination> s_Eliminations = {
        { "corde de piano (3.300)",        "",                          "fiberwire",                       "" },
        { "corde de piano (avant)",        "fiberwire",                 "",                                "" },
        { "couteau, katana",               "",                          "melee_lethal",                    "melee" },
        { "objet lancé",                   "",                          "throw",                           "melee" },
        { "à mains nues",                  "",                          "unarmed",                         "" },
        { "pistolet, tir",                 "",                          "pistol",                          "pistol" },
        { "pistolet, corps à corps",       "",                          "close_combat_pistol_elimination", "pistol" },
        { "PM, tir",                       "",                          "smg",                             "smg" },
        { "PM, coup porté",                "",                          "melee_lethal",                    "smg" },
        { "fusil à pompe",                 "",                          "shotgun",                         "shotgun" },
        { "fusil de précision",            "",                          "sniperrifle",                     "sniperrifle" },
        { "explosif",                      "",                          "explosive",                       "explosive" },
        { "noyade",                        "accident_drown",            "accident",                        "" },
        { "chute",                         "accident_push",             "accident",                        "" },
        { "objet suspendu",                "accident_suspended_object", "accident",                        "" },
        { "poison ingéré",                 "consumed_poison",           "poison",                          "poison" },
        { "poison injecté",                "injected_poison",           "poison",                          "poison" },
    };

    // Ce qu'on attend de la correction du 26/11/2026 : la règle elle-même.
    const std::vector<std::tuple<const char*, const char*, bool>> s_Attendus = {
        { "corde",     "corde de piano (3.300)",  true },
        { "corde",     "corde de piano (avant)",  true },
        { "melee",     "couteau, katana",         true },
        { "melee",     "PM, coup porté",          false },
        { "melee",     "objet lancé",             false },
        { "pistolet",  "pistolet, tir",           true },
        { "pistolet",  "pistolet, corps à corps", true },
        { "pm",        "PM, tir",                 true },
        { "pm",        "PM, coup porté",          true },
        { "pm",        "couteau, katana",         false },
        { "noyade",    "noyade",                  true },
        { "noyade",    "chute",                   false },
        { "accident",  "objet suspendu",          true },
        { "poison",    "poison injecté",          true },
        { "mains_nues","à mains nues",            true },
    };

    // Une cible par méthode, toutes déclarées : l'objectif de chacune porte
    // la condition de sa méthode.
    STirage s_Tirage;
    const auto& s_Methodes = Methodes();
    for (int i = 0; i < static_cast<int>(s_Methodes.size()); ++i) {
        SObjectifCible c;
        c.m_Id = "cible-" + std::to_string(i);
        c.m_Nom = s_Methodes[i].m_Libelle;
        c.m_Methode = i;
        s_Tirage.m_Cibles.push_back(c);
    }

    std::mt19937 s_Hasard(1);
    const auto s_Objectifs = ObjectifsRoulette(s_Tirage, s_Hasard, true, nullptr);

    int s_Erreurs = 0, s_Controles = 0;

    for (int i = 0; i < static_cast<int>(s_Methodes.size()); ++i) {
        const auto& s_Condition = s_Objectifs[i]["Definition"]["States"]["Start"]["Kill"][0]["Condition"];

        for (const auto& e : s_Eliminations) {
            json s_Valeur = { { "RepositoryId", "cible-" + std::to_string(i) }, { "KillMethodStrict", e.m_Stricte },
                              { "KillMethodBroad", e.m_Large }, { "KillItemCategory", e.m_Categorie } };
            const bool s_Fiche = MethodeRespectee(s_Methodes[i], e.m_Stricte, e.m_Large, e.m_Categorie);
            const bool s_Contrat = Evaluer(s_Condition, s_Valeur);
            ++s_Controles;

            if (s_Fiche != s_Contrat) {
                ++s_Erreurs;
                std::cout << "DÉSACCORD  " << s_Methodes[i].m_Cle << " / " << e.m_Nom << " : fiche " << s_Fiche
                          << ", contrat " << s_Contrat << "\n";
            }

            for (const auto& [m, n, v] : s_Attendus) {
                if (std::string(m) != s_Methodes[i].m_Cle || std::string(n) != e.m_Nom) continue;
                ++s_Controles;
                if (s_Fiche != v) {
                    ++s_Erreurs;
                    std::cout << "ATTENDU    " << m << " / " << n << " : " << v << ", obtenu " << s_Fiche << "\n";
                }
            }
        }
    }

    std::cout << s_Controles << " contrôles, " << s_Erreurs << " erreur(s).\n";
    std::cout << (s_Erreurs == 0 ? "Tout est bon.\n" : "À REPRENDRE.\n");
    return s_Erreurs == 0 ? 0 : 1;
}
