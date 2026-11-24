// Essai hors du jeu : menus, préparation et contrat lus sur les VRAIES pages
// relevées (Retail/Roulette/reponses/, partie de Berlin du 22/11/2026, 23 h 11),
// puis la roulette tirée et réécrite dans ces pages.
//
//   EssaiMenus.exe <dossier des pages> [contrat_sortie.json]
//
// Pages : 021 menu principal, 027 page de Berlin, 012 inventaire, 031
// préparation de « Prédateur perché », 039 liste d'un emplacement, 048 contrat.
// Le relevé contient l'adresse e-mail du compte : jamais copié dans le projet.
// Sans le jeu, rien n'est traduit : les noms restent les identifiants du jeu.

#include <filesystem>
#include <set>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>

#include <nlohmann/json.hpp>

#include "Contrat.h"
#include "Menus.h"
#include "Tirage.h"

namespace fs = std::filesystem;
using nlohmann::json;

namespace {
    int g_Echecs = 0;

    void Verifier(bool p_Ok, const std::string& p_Quoi) {
        std::cout << (p_Ok ? "  ok      " : "  ÉCHEC   ") << p_Quoi << "\n";
        if (!p_Ok) ++g_Echecs;
    }

    // Le fichier relevé qui commence par ce numéro.
    json Page(const fs::path& p_Dossier, const std::string& p_Numero) {
        for (const auto& f : fs::directory_iterator(p_Dossier)) {
            if (f.path().filename().string().starts_with(p_Numero + "_")) {
                std::ifstream s_Flux(f.path(), std::ios::binary);
                return json::parse(s_Flux, nullptr, false);
            }
        }
        std::cerr << "page " << p_Numero << " introuvable\n";
        return json();
    }

    // Objectifs encore visibles comme « primary » (ni masqués ni de la roulette).
    int PrimairesVisibles(const json& p_Objectifs) {
        int n = 0;
        for (const auto& o : p_Objectifs) {
            if (o.value("Category", "") == "primary" && !o.value("IsHidden", false)) ++n;
        }
        return n;
    }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "EssaiMenus <dossier des pages> [contrat_sortie.json]\n";
        return 2;
    }

    const fs::path s_Dossier = argv[1];
    const Traducteur s_SansJeu = [](const std::string&) { return std::string(); };
    std::mt19937 s_Hasard(2211);

    // ---------------------------------------------------------------- Menu principal
    std::cout << "Menu principal (021) :\n";
    auto s_Menu = Page(s_Dossier, "021");
    std::vector<SDestination> s_Destinations;
    Verifier(LireDestinations(s_Menu, s_Destinations, s_SansJeu), "destinations reconnues");
    Verifier(s_Destinations.size() == 25, "25 lieux (sans le mode Freelancer) : " + std::to_string(s_Destinations.size()));
    Verifier(LireHistoire(s_Menu, s_Destinations, s_SansJeu), "missions de l'histoire lues");

    const auto s_AvecMissions = std::ranges::count_if(s_Destinations, [](const auto& d) { return !d.m_Missions.empty(); });
    std::cout << "    " << s_AvecMissions << " lieux aux missions connues sans avoir ouvert leur page\n";
    Verifier(s_AvecMissions >= 20, "presque tous les lieux ont une mission");

    const auto s_Berlin = std::ranges::find(s_Destinations, std::string("LOCATION_PARENT_EDGY"), &SDestination::m_Id);
    Verifier(s_Berlin != s_Destinations.end() && !s_Berlin->m_Missions.empty() &&
                 s_Berlin->m_Missions[0].m_Id == "ebcd14b2-0786-4ceb-a2a4-e771f60d0125" &&
                 s_Berlin->m_Missions[0].m_CleTitre == "UI_CONTRACT_FOX_MAIN_TITLE",
             "Berlin : « Prédateur perché » et son contrat");

    std::cout << "\nPage de Berlin (027) :\n";
    Verifier(LireMissions(Page(s_Dossier, "027"), s_Destinations, s_SansJeu), "page d'un lieu reconnue");
    for (const auto& m : s_Berlin->m_Missions) std::cout << "    [" << m.m_Type << "] " << m.m_CleTitre << " " << m.m_Id << "\n";

    const auto s_Relu = DestinationsDepuisJson(json::parse(DestinationsEnJson(s_Destinations).dump()));
    Verifier(DestinationsEnJson(s_Relu) == DestinationsEnJson(s_Destinations), "fichier destinations relu à l'identique");

    std::cout << "\nTirages de destination et tuile du menu :\n";
    int l = -1, m = -1;
    for (int i = 0; i < 5; ++i) {
        TirerDestination(s_Destinations, s_Hasard, l, m);
        std::cout << "    " << s_Destinations[l].m_Id << (m >= 0 ? " — " + s_Destinations[l].m_Missions[m].m_CleTitre : " — mission au choix") << "\n";
    }
    Verifier(m >= 0, "une mission tirée avec la destination");
    Verifier(ImposerDestination(s_Menu, "LOCATION_PARENT_NORTHAMERICA"), "onglet Destinations réécrit");
    Verifier(s_Menu["data"]["DestinationsData"].size() == 1 &&
                 s_Menu["data"]["DestinationsData"][0]["Location"]["Id"] == "LOCATION_PARENT_NORTHAMERICA",
             "seule la destination tirée (Whittleton) reste");

    std::cout << "\nPage de Whittleton (064) :\n";
    {
        auto s_Lieu = Page(s_Dossier, "064");
        Verifier(!ImposerMission(s_Lieu, "ebcd14b2-0786-4ceb-a2a4-e771f60d0125"), "mission d'un autre lieu : page laissée telle quelle");
        Verifier(ImposerMission(s_Lieu, "82f55837-e26c-41bf-bc6e-fa97b7981fbc"), "page réécrite pour « Une autre vie »");
        size_t n = 0;
        for (const auto& s : s_Lieu["data"]["MissionData"]["SubLocationMissionsData"]) {
            for (const auto& [k, v] : s.items()) if (v.is_array() && !v.empty() && v[0].is_object() && v[0].contains("UserCentricContract")) n += v.size();
        }
        Verifier(n == 1, "une seule mission reste (escalades, arcade, affectation spéciale retirées) : " + std::to_string(n));
    }

    // ---------------------------------------------------------------- v0.9 : contrat tiré au menu
    std::cout << "\nContrat tiré au menu (v0.9) :\n";
    {
        // Les cibles de chaque mission, lues dans le menu principal.
        size_t s_Jouables = 0;
        for (const auto& d : s_Destinations) for (const auto& m : d.m_Missions) if (!m.m_Cibles.empty()) ++s_Jouables;
        Verifier(s_Jouables >= 20, std::to_string(s_Jouables) + " missions aux cibles connues dès le menu principal");

        const auto s_Paris = std::ranges::find(s_Destinations, std::string("LOCATION_PARENT_PARIS"), &SDestination::m_Id);
        const auto& s_Spectacle = s_Paris->m_Missions[0];
        Verifier(s_Spectacle.m_Cibles.size() == 2 && s_Spectacle.m_LieuMission == "LOCATION_PARIS" && s_Spectacle.m_Brut.is_object(),
                 "Paris : 2 cibles, lieu de mission, forme brute gardée");

        // Les entrées possédées, rangées par mission.
        const auto s_Possedees = LireEntreesPossedees(Page(s_Dossier, "012"), s_SansJeu);
        Verifier(s_Possedees.size() == 144, std::to_string(s_Possedees.size()) + " entrées possédées");
        const auto s_SansArme = std::ranges::count_if(s_Possedees, [](const SEntree& e) { return !e.m_ArmePermise; });
        const auto s_SansObjet = std::ranges::count_if(s_Possedees, [](const SEntree& e) { return e.m_Equipements == 0; });
        Verifier(s_SansArme == 3 && s_SansObjet == 2, "règles des entrées lues (3 sans arme dissimulée, 2 sans équipement)");
        const auto s_DeParis = EntreesPourMission(s_Possedees, "LOCATION_PARENT_PARIS", s_Spectacle);
        Verifier(!s_DeParis.empty() && std::ranges::all_of(s_DeParis, [](const SEntree& e) { return e.m_IdJeu.find("PARIS") != std::string::npos; }),
                 std::to_string(s_DeParis.size()) + " entrées pour Paris, toutes de Paris");
        const auto s_Sapienza = std::ranges::find(s_Destinations, std::string("LOCATION_PARENT_COASTALTOWN"), &SDestination::m_Id);
        Verifier(!EntreesPourMission(s_Possedees, s_Sapienza->m_Id, s_Sapienza->m_Missions[0]).empty(),
                 "Sapienza : entrées trouvées malgré le nom LOCATION_PARENT_SAPIENZA de l'inventaire");

        // N'importe quelle page de lieu (Whittleton) devient celle du contrat (Paris).
        auto s_Lieu = Page(s_Dossier, "064");
        Verifier(ImposerPageLieu(s_Lieu, *s_Paris, s_Spectacle), "page de Whittleton remplacée par Paris");
        const auto& S = s_Lieu["data"]["MissionData"]["SubLocationMissionsData"];
        Verifier(S.size() == 1 && S[0]["Missions"].size() == 1 &&
                     S[0]["Missions"][0]["UserCentricContract"]["Contract"]["Metadata"]["Id"] == s_Spectacle.m_Id &&
                     s_Lieu["data"]["Location"]["Id"] == "LOCATION_PARENT_PARIS" && S[0]["EscalationMissions"].empty(),
                 "une seule mission, celle du contrat, sous le lieu de Paris");

        // Destinations relues : cibles et entrées gardées sur disque.
        auto s_Copie = s_Destinations;
        s_Copie[0].m_Missions.push_back(s_Spectacle);
        s_Copie[0].m_Missions.back().m_Entrees = { "STARTING_LOCATION_PARIS_PEACOCK_ATTIC" };
        const auto s_Relues = DestinationsDepuisJson(json::parse(DestinationsEnJson(s_Copie).dump()));
        Verifier(s_Relues[0].m_Missions.back().m_Cibles == s_Spectacle.m_Cibles && s_Relues[0].m_Missions.back().m_Entrees.size() == 1,
                 "cibles et entrées connues relues depuis destinations.json");
        Verifier(s_Relues[0].m_Missions.back().m_Brut == s_Spectacle.m_Brut, "forme brute de la mission relue (page de lieu servie après un redémarrage)");
    }

    // ---------------------------------------------------------------- Inventaire
    std::cout << "\nInventaire (012) :\n";
    std::vector<SObjetInventaire> s_Inventaire;
    Verifier(LireInventaire(Page(s_Dossier, "012"), s_Inventaire, s_SansJeu), "inventaire reconnu");

    // ---------------------------------------------------------------- Préparation
    std::cout << "\nPréparation de Berlin (031) :\n";
    auto s_PagePlanif = Page(s_Dossier, "031");
    SPlanification s_Planif;
    Verifier(LirePlanification(s_PagePlanif, s_Planif, s_SansJeu), "écran de préparation reconnu");
    Verifier(s_Planif.m_Cibles.size() == 11, "les 11 agents de Berlin sont des cibles possibles : " + std::to_string(s_Planif.m_Cibles.size()));
    std::cout << "    " << s_Planif.m_Entrees.size() << " entrée(s) débloquée(s)\n";

    STirage s_Base;
    for (const auto& id : s_Planif.m_Cibles) {
        SObjectifCible c;
        c.m_Id = id;
        c.m_Nom = id.substr(0, 8);
        s_Base.m_Cibles.push_back(c);
    }
    for (int i = 0; i < 3; ++i) {
        SObjectifCible p;
        p.m_Officielle = false;
        p.m_Id = "pnj-" + std::to_string(i);
        p.m_Nom = p.m_Id;
        s_Base.m_Cibles.push_back(p);
    }
    STenue s_Costume;
    s_Costume.m_Id = "costume_47";
    s_Costume.m_Nom = "Costume de 47";
    s_Costume.m_EstCostume = true;
    s_Base.m_Tenues.push_back(s_Costume);

    // Nombre de cibles : 1 à 3, au moins une du contrat, PNJ ≤ réglage.
    SReglages s_Reglages;
    s_Reglages.m_Mode = EMode::Freestyle; // essais écrits avant les modes (voir plus bas)
    s_Reglages.m_NbComplications = 2;
    int s_Repartition[4] = {}, s_HorsBornes = 0, s_SansContrat = 0, s_TropDePnj = 0;
    for (int i = 0; i < 600; ++i) {
        auto t = s_Base;
        ChoisirCibles(t, s_Reglages, s_Hasard);
        const auto n = static_cast<int>(t.m_Cibles.size());
        const auto k = std::ranges::count_if(t.m_Cibles, [](const auto& c) { return !c.m_Officielle; });
        if (n < 1 || n > 3) ++s_HorsBornes; else ++s_Repartition[n];
        if (k == n) ++s_SansContrat;
        if (k > s_Reglages.m_NbPnj) ++s_TropDePnj;
    }
    std::cout << "    600 tirages : 1 cible " << s_Repartition[1] << ", 2 cibles " << s_Repartition[2] << ", 3 cibles " << s_Repartition[3] << "\n";
    Verifier(s_HorsBornes == 0 && s_Repartition[1] && s_Repartition[2] && s_Repartition[3], "entre 1 et 3 cibles, les trois nombres sortent");
    std::cout << "    tirages sans aucune cible du contrat : " << s_SansContrat << "\n";
    Verifier(s_SansContrat > 0, "les cibles du contrat ne sont pas imposées (PNJ à égalité)");
    (void)s_TropDePnj;

    s_Reglages.m_CiblesMin = s_Reglages.m_CiblesMax = 2;
    {
        auto t = s_Base;
        ChoisirCibles(t, s_Reglages, s_Hasard);
        Verifier(t.m_Cibles.size() == 2, "réglé à 2 : exactement 2 cibles");
    }
    s_Reglages.m_CiblesMin = 1;
    s_Reglages.m_CiblesMax = 3;

    STirage s_Tirage = s_Base;
    ChoisirCibles(s_Tirage, s_Reglages, s_Hasard);
    Tirer(s_Tirage, s_Reglages, s_Hasard);
    s_Tirage.m_Preparation = TirerPreparation(s_Planif, s_Inventaire, s_Tirage, EMode::Freestyle, s_Hasard);
    const auto& p = s_Tirage.m_Preparation;
    std::cout << "    tirage : " << s_Tirage.m_Cibles.size() << " cible(s) ; entrée " << p.m_Entree.m_IdJeu << ", arme " << p.m_Arme.m_IdJeu
              << ", objet " << p.m_Objet.m_IdJeu << "\n";
    Verifier(ImposerPlanification(s_PagePlanif, p), "écran de préparation réécrit");
    Verifier(s_PagePlanif["data"]["Entrances"].size() == 1, "une seule entrée : la tirée");

    // Le briefing de la page : objectifs du jeu masqués, ceux de la roulette ajoutés.
    for (const std::string s_Chemin : { "Contract", "UserCentric" }) {
        auto& D = s_Chemin == "Contract" ? s_PagePlanif["data"]["Contract"]["Data"]
                                         : s_PagePlanif["data"]["UserCentric"]["Contract"]["Data"];
        std::vector<std::string> s_Ids;
        const auto s_Ajout = ObjectifsRoulette(s_Tirage, s_Hasard, true, &s_Ids);
        const int s_Masques = MasquerObjectifsDuJeu(D, s_Ids);
        for (const auto& o : s_Ajout) D["Objectives"].push_back(o);
        Verifier(s_Masques > 0 && PrimairesVisibles(D["Objectives"]) == static_cast<int>(s_Ajout.size()),
                 "briefing (" + s_Chemin + ") : " + std::to_string(s_Masques) + " objectifs du jeu masqués, seuls ceux de la roulette restent");
    }

    // Écran « Objectifs » et chargement : au format du briefing.
    {
        const auto b = BriefingRoulette(s_Tirage);
        Verifier(b.size() == s_Tirage.m_Cibles.size() + s_Tirage.m_Complications.size() && b[0]["Type"] == "kill" &&
                     b[0]["Properties"]["Id"] == s_Tirage.m_Cibles[0].m_Id,
                 "briefing : une tuile par cible et par complication");
        std::cout << "    " << b[0].dump() << "\n";
    }

    // ---- Les modes (24/11/2026). Hors du jeu, le dépôt manque : les silencieux
    // sont marqués comme le jeu les dit (relevé du dépôt : « suppressor »).
    std::cout << "\nModes :\n";
    {
        auto s_Inv = s_Inventaire;
        for (auto& o : s_Inv) {
            o.m_Silencieux = o.m_IdJeu.find("BALLER") != std::string::npos && o.m_IdJeu != "FIREARMS_PISTOL_CLASSIC_SILVERBALLER";
        }
        const auto Methode = [](const SObjectifCible& c) { return c.m_Methode < 0 ? std::string("?") : std::string(Methodes()[c.m_Methode].m_Cle); };

        for (const EMode s_Mode : { EMode::Assassin, EMode::Puriste }) {
            SReglages r;
            r.m_Mode = s_Mode;
            r.m_NbComplications = 3;
            r.m_ImposerMethode = false; // sans effet hors Freestyle
            r.m_ImposerTenue = false;
            bool s_MethodesOk = true, s_CostumeOk = true, s_ComplOk = true, s_PrepOk = true, s_TroisAuPlus = true;
            std::set<std::string> s_Armes, s_Methodes;
            for (int i = 0; i < 300; ++i) {
                auto t = s_Base;
                ChoisirCibles(t, r, s_Hasard);
                Tirer(t, r, s_Hasard);
                if (t.m_Cibles.size() > 3) s_TroisAuPlus = false;
                for (const auto& c : t.m_Cibles) {
                    const auto k = Methode(c);
                    s_Methodes.insert(k);
                    const bool ok = s_Mode == EMode::Assassin
                        ? (k == "pistolet" || k == "corde" || k == "accident" || k == "poison")
                        : (k == "noyade" || k == "chute" || k == "electrocution" || k == "objet_suspendu" || k == "brulure" ||
                           k == "explosion_accident" || k == "poison" || k == "corde");
                    if (!ok) s_MethodesOk = false;
                    if (c.m_Tenue < 0 || !t.m_Tenues[c.m_Tenue].m_EstCostume) s_CostumeOk = false;
                }
                for (const auto& x : t.m_Complications) {
                    const std::string k = Complications()[x.m_Index].m_Cle;
                    if (k == "aucune_neutralisation") s_ComplOk = false;
                    if (s_Mode == EMode::Puriste && k == "tirs_tete") s_ComplOk = false;
                }
                if (s_Mode == EMode::Puriste && t.m_Complications.size() != 5) s_ComplOk = false;

                const auto q = TirerPreparation(s_Planif, s_Inv, t, s_Mode, s_Hasard);
                if (!q.m_Arme.Vide()) s_Armes.insert(q.m_Arme.m_IdJeu);
                const auto Est = [](const SImpose& x, const char* mot) { return x.m_IdJeu.find(mot) != std::string::npos; };
                if (s_Mode == EMode::Assassin) {
                    if (q.m_Arme.Vide() || !Est(q.m_Arme, "BALLER") || q.m_Arme.m_IdJeu == "FIREARMS_PISTOL_CLASSIC_SILVERBALLER") s_PrepOk = false;
                    if (!Est(q.m_Objet, "FIBER") || !Est(q.m_Objet2, "LOCK_PICK_S3")) s_PrepOk = false;
                }
                else {
                    if (!q.m_Arme.Vide() || !Est(q.m_Objet, "FIBER") || !Est(q.m_Objet2, "COIN")) s_PrepOk = false;
                }
            }
            const std::string n = LibelleMode(s_Mode);
            std::cout << "    " << n << " : méthodes";
            for (const auto& k : s_Methodes) std::cout << " " << k;
            std::cout << " ; armes";
            for (const auto& a : s_Armes) std::cout << " " << a;
            std::cout << "\n";
            Verifier(s_TroisAuPlus, n + " : 3 cibles au plus");
            Verifier(s_MethodesOk, n + " : seulement les méthodes du mode");
            Verifier(s_CostumeOk, n + " : costume de 47 pour chaque cible");
            Verifier(s_ComplOk, n + (s_Mode == EMode::Puriste ? " : les 5 exigences du SASO, rien d'autre" : " : complications réalistes seulement"));
            Verifier(s_PrepOk, n + (s_Mode == EMode::Assassin ? " : Silverballer silencieux, corde de piano, crochet MK III"
                                                              : " : aucune arme, corde de piano et pièces"));
            if (s_Mode == EMode::Assassin) Verifier(s_Armes.size() > 1, "Assassin : la variante du Silverballer change d'un tirage à l'autre");

            // Le verrou : l'équipement obligatoire porte les trois objets.
            auto t = s_Base;
            ChoisirCibles(t, r, s_Hasard);
            Tirer(t, r, s_Hasard);
            const auto q = TirerPreparation(s_Planif, s_Inv, t, s_Mode, s_Hasard);
            nlohmann::json c = { { "Data", nlohmann::json::object() } };
            AjouterEquipementObligatoire(c, q);
            Verifier(c["Data"]["MandatoryLoadout"].size() == (s_Mode == EMode::Assassin ? 3u : 2u),
                     n + " : équipement obligatoire = " + std::to_string(c["Data"]["MandatoryLoadout"].size()) + " objet(s)");
        }
    }

    std::cout << "\nLieu de départ (079, Whittleton) :\n";
    {
        auto s_Page = Page(s_Dossier, "079");
        // Les entrées débloquées du lieu, telles que la page les liste (la page
        // de préparation relevée est déjà réécrite : une seule entrée).
        SPlanification w;
        for (const auto& e : s_Page["data"]["OrderedUnlocks"]) {
            SEntree x;
            x.m_Id = e["Properties"].value("RepositoryId", "");
            x.m_IdJeu = e.value("Id", "");
            w.m_Entrees.push_back(x);
        }
        Verifier(w.m_Entrees.size() == 7, "7 entrées débloquées à Whittleton");
        SPreparation q;
        q.m_Entree.m_Id = "896cf183-d974-4d45-a819-2f04466d95e2";
        q.m_Entree.m_IdJeu = "STARTING_LOCATION_NORTHAMERICA_SKUNK_GARDENER";
        const auto s_Avant = s_Page["data"]["OrderedUnlocks"].size();
        const auto s_AvantDebloquees = s_Page["data"]["Unlocked"].size();
        Verifier(ImposerListeEntrees(s_Page, q, w.m_Entrees) && s_Page["data"]["OrderedUnlocks"].size() == 1 &&
                     s_Page["data"]["OrderedUnlocks"][0]["Id"] == q.m_Entree.m_IdJeu,
                 std::to_string(s_Avant) + " entrées → la seule tirée (jardinier)");
        int s_Restantes = 0;
        bool s_TireeGardee = false;
        for (const auto& x : s_Page["data"]["Unlocked"]) {
            for (const auto& e : w.m_Entrees) if (x == e.m_Id) ++s_Restantes;
            if (x == q.m_Entree.m_Id) s_TireeGardee = true;
        }
        Verifier(s_Restantes == 1 && s_TireeGardee, "débloquées : " + std::to_string(s_AvantDebloquees) + " → " +
                     std::to_string(s_Page["data"]["Unlocked"].size()) + ", seule l'entrée tirée du lieu reste");
    }

    std::cout << "\nListe d'un emplacement (039) :\n";
    {
        auto s_Liste = Page(s_Dossier, "039");
        const auto s_Avant = s_Liste["data"]["LoadoutItemsData"]["Items"].size();
        bool s_Absent = false;
        const bool s_Change = ImposerEmplacement(s_Liste, p, s_Absent);
        std::cout << "    emplacement " << s_Liste["data"].value("SlotId", "?") << " : " << s_Avant << " → "
                  << s_Liste["data"]["LoadoutItemsData"]["Items"].size() << "\n";
        Verifier(!s_Change, "2e équipement (emplacement 5) laissé libre");
    }

    // ---------------------------------------------------------------- Contrat
    std::cout << "\nContrat de Berlin (048) :\n";
    auto s_Reponse = Page(s_Dossier, "048");
    auto& s_Donnees = s_Reponse["Contract"]["Data"];
    const auto s_ObjectifsDuJeu = s_Donnees["Objectives"].size();

    std::vector<std::string> s_IdsCibles;
    const auto s_Objectifs = ObjectifsRoulette(s_Tirage, s_Hasard, true, &s_IdsCibles);
    const int s_Masques = MasquerObjectifsDuJeu(s_Donnees, s_IdsCibles);
    for (const auto& o : s_Objectifs) s_Donnees["Objectives"].push_back(o);

    Verifier(s_IdsCibles.size() == s_Tirage.m_Cibles.size(), "un objectif par cible tirée, même sans consigne");
    Verifier(s_Objectifs.size() == s_Tirage.m_Cibles.size() + s_Tirage.m_Complications.size(), "plus un par complication");
    Verifier(PrimairesVisibles(s_Donnees["Objectives"]) == static_cast<int>(s_Objectifs.size()),
             "les " + std::to_string(s_Masques) + " objectifs du jeu (sur " + std::to_string(s_ObjectifsDuJeu) + ") ne s'affichent plus");

    bool s_Conditions = true;
    for (size_t i = 0; i < s_ObjectifsDuJeu; ++i) {
        const auto& o = s_Donnees["Objectives"][i];
        if (o.value("Category", "") == "condition" && o.value("IsHidden", false)) s_Conditions = false;
    }
    Verifier(s_Conditions, "les déclencheurs « condition » des scripts sont intacts");

    const auto& s_Sortie = s_Donnees["EnableExits"];
    Verifier(s_Sortie.contains("$and") && s_Sortie["$and"].size() == s_IdsCibles.size() &&
                 s_Sortie["$and"][0]["$eq"][0] == "$" + s_IdsCibles[0],
             "sortie ouverte quand les cibles de la roulette sont éliminées : " + s_Sortie.dump());

    Verifier(AjouterEquipementObligatoire(s_Reponse["Contract"], p), "équipement obligatoire ajouté");

    if (argc > 2) std::ofstream(argv[2], std::ios::binary) << s_Donnees.dump(1);

    std::cout << "\n" << (g_Echecs ? std::to_string(g_Echecs) + " ÉCHEC(S)" : "Tout est bon") << "\n";
    return g_Echecs ? 1 : 0;
}
