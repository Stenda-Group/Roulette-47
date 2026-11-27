// Essai hors du jeu : une mission d'un DLC que le joueur n'a pas n'est jamais
// tirée (Droits.cpp, MissionPossedee).
//
//   EssaiDroits.exe [destinations.json]
//
// Les droits viennent des menus relevés le 26/11/2026 (destinations.json du
// mod) : « Host Assignment ft. Jacksepticeye » porte H3_ET_SHAMROCK dans
// SubLocation.Properties.Entitlements et rien dans son contrat ; « Holiday
// Hoarders » nomme six lieux, au choix. Avec le fichier en argument, chaque
// mission relevée est aussi passée au crible, The Host Pack faisant défaut.

#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>

#include "Droits.h"

using json = nlohmann::json;

int main(int argc, char** argv) {
    int s_Erreurs = 0, s_Controles = 0;
    const auto Verifier = [&](const char* p_Nom, bool p_Obtenu, bool p_Attendu) {
        ++s_Controles;
        if (p_Obtenu != p_Attendu) {
            ++s_Erreurs;
            std::cout << "ERREUR  " << p_Nom << " : " << p_Obtenu << " au lieu de " << p_Attendu << "\n";
        }
    };

    // Le joueur de l'essai a tout, sauf The Host Pack.
    const auto SansShamrock = [](const std::string& d) { return d != "H3_ET_SHAMROCK"; };
    const auto Tout = [](const std::string&) { return true; };
    const auto RienDeH3 = [](const std::string& d) { return !d.starts_with("LOCATION_"); };

    const json s_Hote = json::parse(R"({
        "Location": { "Properties": { "Entitlements": ["LOCATION_NEWZEALAND"] } },
        "SubLocation": { "Properties": { "Entitlements": ["H3_ET_SHAMROCK"] } },
        "UserCentricContract": { "Contract": { "Metadata": { "Entitlements": [] } } } })");
    const json s_Vacances = json::parse(R"({
        "SubLocation": { "Properties": { "Entitlements": ["LOCATION_PARIS"] } },
        "UserCentricContract": { "Contract": { "Metadata": { "Entitlements":
            ["LOCATION_GOLDEN", "LOCATION_ANCESTRAL", "LOCATION_EDGY", "LOCATION_WET", "LOCATION_ELEGANT", "LOCATION_TRAPPED"] } } } })");
    const json s_Revenant = json::parse(R"({
        "SubLocation": { "Properties": { "Entitlements": ["H3_ET_SAMBUCA"] } },
        "UserCentricContract": { "Contract": { "Metadata": { "Entitlements": ["LOCATION_MIAMI"] } } } })");
    const json s_Histoire = json::parse(R"({ "Data": {
        "SubLocation": { "Properties": { "Entitlements": ["H3_ET_SHAMROCK"] } } } })");

    Verifier("hôte, sans The Host Pack", MissionPossedee(s_Hote, SansShamrock), false);
    Verifier("hôte, avec", MissionPossedee(s_Hote, Tout), true);
    Verifier("revenant (autre pack possédé)", MissionPossedee(s_Revenant, SansShamrock), true);
    Verifier("Holiday Hoarders, un lieu suffit", MissionPossedee(s_Vacances, [](const std::string& d) {
        return d == "LOCATION_PARIS" || d == "LOCATION_WET"; }), true);
    Verifier("Holiday Hoarders, aucun lieu de HITMAN 3", MissionPossedee(s_Vacances, RienDeH3), false);
    Verifier("mission de l'histoire (sous Data)", MissionPossedee(s_Histoire, SansShamrock), false);
    Verifier("mission sans droit", MissionPossedee(json::object(), SansShamrock), true);
    Verifier("pas d'objet", MissionPossedee(json(), SansShamrock), true);

    // Le vrai relevé : seule la mission de l'hôte doit tomber.
    if (argc > 1) {
        std::ifstream s_Entree(argv[1], std::ios::binary);
        std::stringstream s_Tampon;
        s_Tampon << s_Entree.rdbuf();
        const auto s_Destinations = json::parse(s_Tampon.str());
        std::set<std::string> s_Ecartees;
        int s_Missions = 0;
        for (const auto& d : s_Destinations) {
            for (const auto& m : d.value("missions", json::array())) {
                ++s_Missions;
                if (!MissionPossedee(m.value("brut", json()), SansShamrock)) s_Ecartees.insert(m.value("titre", m.value("id", "?")));
            }
        }
        std::cout << s_Missions << " missions relevées, " << s_Ecartees.size() << " écartée(s) :";
        for (const auto& t : s_Ecartees) std::cout << " « " << t << " »";
        std::cout << "\n";
        Verifier("relevé : une seule mission écartée", s_Ecartees.size() == 1, true);
    }

    std::cout << s_Controles << " contrôles, " << s_Erreurs << " erreur(s).\n";
    std::cout << (s_Erreurs == 0 ? "Tout est bon.\n" : "À REPRENDRE.\n");
    return s_Erreurs == 0 ? 0 : 1;
}
