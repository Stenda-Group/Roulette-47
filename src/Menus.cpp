#include "Menus.h"

#include <algorithm>
#include <functional>
#include <cctype>
#include <set>

using nlohmann::json;

namespace {
    std::string Bas(std::string p_Texte) {
        for (auto& c : p_Texte) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return p_Texte;
    }

    std::string Haut(std::string p_Texte) {
        for (auto& c : p_Texte) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return p_Texte;
    }

    std::string Txt(const json& p_Json, const char* p_Cle) {
        if (!p_Json.is_object()) return {};
        const auto s_It = p_Json.find(p_Cle);
        return s_It != p_Json.end() && s_It->is_string() ? s_It->get<std::string>() : std::string();
    }

    bool Vrai(const json& p_Json, const char* p_Cle) {
        if (!p_Json.is_object()) return false;
        const auto s_It = p_Json.find(p_Cle);
        return s_It != p_Json.end() && s_It->is_boolean() && s_It->get<bool>();
    }

    // p_Json[a][b]… ou un objet vide (jamais d'exception sur une page inattendue).
    const json& Chemin(const json& p_Json, std::initializer_list<const char*> p_Cles) {
        static const json s_Rien;
        const json* s_Noeud = &p_Json;
        for (const auto* k : p_Cles) {
            if (!s_Noeud->is_object()) return s_Rien;
            const auto s_It = s_Noeud->find(k);
            if (s_It == s_Noeud->end()) return s_Rien;
            s_Noeud = &*s_It;
        }
        return *s_Noeud;
    }

    std::string Traduit(const Traducteur& p_Traduire, const std::string& p_Cle, const std::string& p_Repli) {
        if (!p_Cle.empty() && p_Traduire) {
            if (auto s_Texte = p_Traduire(p_Cle); !s_Texte.empty()) return s_Texte;
        }
        return p_Repli;
    }

    bool Contient(const std::string& p_Texte, std::initializer_list<const char*> p_Mots) {
        return std::ranges::any_of(p_Mots, [&](const char* m) { return p_Texte.find(m) != std::string::npos; });
    }

    template <class T>
    const T* AuHasard(const std::vector<const T*>& p_Liste, std::mt19937& p_Hasard) {
        if (p_Liste.empty()) return nullptr;
        return p_Liste[std::uniform_int_distribution<size_t>(0, p_Liste.size() - 1)(p_Hasard)];
    }

    SImpose Imposer(const SObjetInventaire& o) {
        SImpose s;
        s.m_Id = o.m_Id;
        s.m_IdJeu = o.m_IdJeu;
        s.m_Emplacement = o.m_Emplacement;
        s.m_Nom = o.m_Nom;
        s.m_Cle = o.m_CleNom;
        s.m_Cle2 = o.m_Cle2;
        s.m_Image = o.m_Image;
        s.m_Brut = o.m_Brut;
        return s;
    }

    // « LoadoutSettings » d'une entrée (absent : tout est permis).
    void LireReglesEntree(const json& p_Proprietes, SEntree& p_Entree) {
        // La tenue de départ (« Equip ») : 47 en costume s'il n'y en a pas, ou
        // si c'est une tenue « HERO ».
        if (const auto& E = Chemin(p_Proprietes, { "Equip" }); E.is_array() && !E.empty()) {
            p_Entree.m_EnCostume = false;
            for (const auto& t : E) {
                if (t.is_string()) p_Entree.m_Tenues.push_back(t.get<std::string>());
                if (t.is_string() && t.get<std::string>().find("HERO") != std::string::npos) p_Entree.m_EnCostume = true;
            }
        }

        const auto& L = Chemin(p_Proprietes, { "LoadoutSettings" });
        if (!L.is_object()) return;
        if (L.contains("ConcealedWeaponSlotEnabled") && L["ConcealedWeaponSlotEnabled"].is_boolean()) {
            p_Entree.m_ArmePermise = L["ConcealedWeaponSlotEnabled"].get<bool>();
        }
        if (L.contains("GearSlotsEnabledCount") && L["GearSlotsEnabledCount"].is_number_integer()) {
            p_Entree.m_Equipements = L["GearSlotsEnabledCount"].get<int>();
        }
    }

    // L'élément d'une liste d'emplacement ou d'un « Recommended » : son RepositoryId.
    std::string IdObjet(const json& p_Objet) {
        return Bas(Txt(Chemin(p_Objet, { "Unlockable", "Properties" }), "RepositoryId"));
    }
}

// ---------------------------------------------------------------- Lecture

bool LireDestinations(const json& p_Page, std::vector<SDestination>& p_Destinations, const Traducteur& p_Traduire) {
    const auto& s_Liste = Chemin(p_Page, { "data", "DestinationsData" });
    if (!s_Liste.is_array() || s_Liste.empty()) return false;

    std::vector<SDestination> s_Nouvelles;

    for (const auto& d : s_Liste) {
        const auto& L = Chemin(d, { "Location" });
        const auto& P = Chemin(L, { "Properties" });
        SDestination s;
        s.m_Id = Txt(L, "Id");
        if (s.m_Id.empty()) continue;

        // Le mode Freelancer (« GameMode ») n'a pas de contrat à cibles : la
        // roulette n'y a pas de sens.
        if (P.is_object() && P.contains("GameMode")) continue;

        s.m_Nom = Traduit(p_Traduire, Txt(L, "DisplayNameLocKey"), s.m_Id);
        s.m_Image = Txt(P, "Icon");
        s.m_Fond = Txt(P, "Background");
        s.m_Verrouillee = Vrai(P, "IsLocked");
        s.m_Brut = L;

        if (const auto s_It = std::ranges::find(p_Destinations, s.m_Id, &SDestination::m_Id); s_It != p_Destinations.end()) {
            s.m_Missions = s_It->m_Missions;
        }

        s_Nouvelles.push_back(std::move(s));
    }

    if (s_Nouvelles.empty()) return false;
    p_Destinations = std::move(s_Nouvelles);
    return true;
}

bool LireMissions(const json& p_Page, std::vector<SDestination>& p_Destinations, const Traducteur& p_Traduire) {
    const auto& s_Lieux = Chemin(p_Page, { "data", "MissionData", "SubLocationMissionsData" });
    if (!s_Lieux.is_array() || s_Lieux.empty()) return false;

    // Les missions qui se jouent avec des cibles et une seule fois de suite :
    // histoire, bonus, flashbacks, Sarajevo Six. Jamais les cibles fugitives
    // (une seule tentative : un échec serait définitif), ni les escalades, ni le
    // mode tireur d'élite, ni l'arcade.
    static const std::set<std::string> s_Types = { "mission", "flashback", "orbis", "campaign", "bonus" };

    std::string s_Parent;
    std::vector<SMissionConnue> s_Missions;
    std::set<std::string> s_Vues;
    const json* s_LieuParent = nullptr;

    for (const auto& s : s_Lieux) {
        if (s_Parent.empty()) {
            s_Parent = Txt(Chemin(s, { "Location" }), "Id");
            s_LieuParent = &Chemin(s, { "Location" });
        }

        for (const char* s_Categorie : { "Missions", "SarajevoSixMissions", "CampaignMissions" }) {
            const auto& s_Liste = Chemin(s, { s_Categorie });
            if (!s_Liste.is_array()) continue;

            for (const auto& m : s_Liste) {
                const auto& U = m.contains("UserCentricContract") && m["UserCentricContract"].is_object() ? m["UserCentricContract"] : m;
                const auto& M = Chemin(U, { "Contract", "Metadata" });
                const auto& D = Chemin(U, { "Data" });

                SMissionConnue c;
                c.m_Id = Txt(M, "Id");
                c.m_Type = Txt(M, "Type");
                if (c.m_Id.empty() || !s_Types.contains(c.m_Type)) continue;
                if (Vrai(D, "IsLocked") || Vrai(M, "IsLocked")) continue;
                if (!s_Vues.insert(c.m_Id).second) continue;

                c.m_CleTitre = Txt(M, "Title");
                c.m_Titre = Traduit(p_Traduire, c.m_CleTitre, c.m_CleTitre);
                c.m_Image = Txt(M, "TileImage");
                c.m_LieuMission = Txt(M, "Location");
                c.m_Cibles = CiblesDesObjectifs(Chemin(U, { "Contract", "Data", "Objectives" }));
                c.m_Brut = m;
                s_Missions.push_back(std::move(c));
            }
        }
    }

    if (s_Parent.empty()) return false;

    auto s_It = std::ranges::find(p_Destinations, s_Parent, &SDestination::m_Id);

    if (s_It == p_Destinations.end()) {
        // Lieu ouvert avant d'avoir vu l'écran des destinations : on le note quand même.
        SDestination s;
        s.m_Id = s_Parent;
        s.m_Nom = Traduit(p_Traduire, Txt(*s_LieuParent, "DisplayNameLocKey"), s_Parent);
        s.m_Image = Txt(Chemin(*s_LieuParent, { "Properties" }), "Icon");
        s.m_Fond = Txt(Chemin(*s_LieuParent, { "Properties" }), "Background");
        p_Destinations.push_back(std::move(s));
        s_It = std::prev(p_Destinations.end());
    }

    // La page du lieu fait foi ; ce que l'histoire avait appris et qu'elle ne
    // liste pas (rare) est gardé.
    for (auto& m : s_It->m_Missions) {
        const auto s_Meme = std::ranges::find(s_Missions, m.m_Id, &SMissionConnue::m_Id);
        if (s_Meme == s_Missions.end()) s_Missions.push_back(std::move(m));
        else {
            if (s_Meme->m_Entrees.empty()) s_Meme->m_Entrees = std::move(m.m_Entrees);
            if (!s_Meme->m_Brut.is_object()) s_Meme->m_Brut = std::move(m.m_Brut);
        }
    }
    s_It->m_Missions = std::move(s_Missions);
    return true;
}

bool LireHistoire(const json& p_Page, std::vector<SDestination>& p_Destinations, const Traducteur& p_Traduire) {
    const auto& s_Saisons = Chemin(p_Page, { "data", "StoryData" });
    if (!s_Saisons.is_array()) return false;

    bool s_Ajout = false;

    for (const auto& s_Saison : s_Saisons) {
        const auto& s_Liste = Chemin(s_Saison, { "StoryData" });
        if (!s_Liste.is_array()) continue;

        for (const auto& x : s_Liste) {
            if (Txt(x, "Type") != "Mission") continue;

            const auto& U = Chemin(x, { "Data", "UserCentricContract" });
            const auto& M = Chemin(U, { "Contract", "Metadata" });
            const auto& D = Chemin(U, { "Data" });

            SMissionConnue c;
            c.m_Id = Txt(M, "Id");
            c.m_Type = Txt(M, "Type");
            // Même règle que la page d'un lieu : pas de tutoriel ni de cible fugitive.
            if (c.m_Id.empty() || (c.m_Type != "mission" && c.m_Type != "flashback" && c.m_Type != "campaign")) continue;
            if (Vrai(D, "IsLocked") || Vrai(M, "IsLocked")) continue;

            const auto s_Parent = Txt(D, "ParentLocationId");
            const auto s_It = std::ranges::find(p_Destinations, s_Parent, &SDestination::m_Id);
            if (s_Parent.empty() || s_It == p_Destinations.end()) continue;
            // Déjà connue (destinations.json, page du lieu) : on la complète.
            if (const auto s_Deja = std::ranges::find(s_It->m_Missions, c.m_Id, &SMissionConnue::m_Id); s_Deja != s_It->m_Missions.end()) {
                s_Deja->m_LieuMission = Txt(M, "Location");
                s_Deja->m_Cibles = CiblesDesObjectifs(Chemin(U, { "Contract", "Data", "Objectives" }));
                s_Deja->m_Brut = Chemin(x, { "Data" });
                if (s_Deja->m_CleTitre.empty()) s_Deja->m_CleTitre = Txt(M, "Title");
                s_Ajout = true;
                continue;
            }

            c.m_CleTitre = Txt(M, "Title");
            c.m_Titre = Traduit(p_Traduire, c.m_CleTitre, c.m_CleTitre);
            c.m_Image = Txt(M, "TileImage");
            c.m_LieuMission = Txt(M, "Location");
            c.m_Cibles = CiblesDesObjectifs(Chemin(U, { "Contract", "Data", "Objectives" }));
            c.m_Brut = Chemin(x, { "Data" });
            s_It->m_Missions.push_back(std::move(c));
            s_Ajout = true;
        }
    }

    return s_Ajout;
}

bool LireInventaire(const json& p_Reponse, std::vector<SObjetInventaire>& p_Objets, const Traducteur& p_Traduire) {
    const auto& s_Liste = Chemin(p_Reponse, { "Inventory" });
    if (!s_Liste.is_array() || s_Liste.empty()) return false;

    std::vector<SObjetInventaire> s_Objets;
    std::set<std::string> s_Vus;

    for (const auto& i : s_Liste) {
        const auto& U = Chemin(i, { "Unlockable" });
        const auto& P = Chemin(U, { "Properties" });

        SObjetInventaire o;
        o.m_Emplacement = Txt(P, "LoadoutSlot");
        if (o.m_Emplacement != "concealedweapon" && o.m_Emplacement != "gear") continue;

        o.m_Id = Bas(Txt(P, "RepositoryId"));
        o.m_IdJeu = Txt(U, "Id");
        o.m_SousType = Txt(U, "Subtype");
        o.m_CleNom = Txt(U, "DisplayNameLocKey");
        if (o.m_Id.empty() || o.m_IdJeu.empty() || !s_Vus.insert(o.m_Id).second) continue;

        o.m_Nom = Traduit(p_Traduire, o.m_CleNom, o.m_IdJeu);
        o.m_Brut = i;
        s_Objets.push_back(std::move(o));
    }

    if (s_Objets.empty()) return false;
    p_Objets = std::move(s_Objets);
    return true;
}

std::vector<SEntree> LireEntreesPossedees(const json& p_Reponse, const Traducteur& p_Traduire) {
    std::vector<SEntree> s_Entrees;
    const auto& s_Liste = Chemin(p_Reponse, { "Inventory" });
    if (!s_Liste.is_array()) return s_Entrees;

    std::set<std::string> s_Vues;
    for (const auto& i : s_Liste) {
        const auto& U = Chemin(i, { "Unlockable" });
        if (Txt(U, "Subtype") != "startinglocation") continue;
        const auto& P = Chemin(U, { "Properties" });

        SEntree e;
        e.m_Id = Bas(Txt(P, "RepositoryId"));
        e.m_IdJeu = Txt(U, "Id");
        e.m_CleNom = Txt(U, "DisplayNameLocKey");
        e.m_Parent = Txt(P, "Location");
        LireReglesEntree(P, e);
        if (e.m_Id.empty() || e.m_IdJeu.empty() || !s_Vues.insert(e.m_Id).second) continue;
        e.m_Nom = Traduit(p_Traduire, e.m_CleNom, e.m_IdJeu);
        s_Entrees.push_back(std::move(e));
    }
    return s_Entrees;
}

std::vector<SEntree> EntreesPourMission(const std::vector<SEntree>& p_Possedees, const std::string& p_Parent,
                                        const SMissionConnue& p_Mission) {
    std::vector<SEntree> s_Choix;

    if (!p_Mission.m_Entrees.empty()) {
        for (const auto& e : p_Possedees) {
            if (std::ranges::find(p_Mission.m_Entrees, e.m_IdJeu) != p_Mission.m_Entrees.end()) s_Choix.push_back(e);
        }
        if (!s_Choix.empty()) return s_Choix;
    }

    // Le nom du lieu dans l'inventaire (relevé du 23/11/2026).
    std::string s_Parent = p_Parent;
    if (s_Parent == "LOCATION_PARENT_COASTALTOWN") s_Parent = "LOCATION_PARENT_SAPIENZA";
    if (s_Parent == "LOCATION_PARENT_MARRAKECH") s_Parent = "LOCATION_PARENT_MARRAKESH";

    std::vector<SEntree> s_DuLieu;
    for (const auto& e : p_Possedees) if (e.m_Parent == s_Parent || e.m_Parent == p_Parent) s_DuLieu.push_back(e);

    auto s_Jeton = p_Mission.m_LieuMission;
    if (s_Jeton.starts_with("LOCATION_")) s_Jeton = s_Jeton.substr(9);
    if (!s_Jeton.empty()) {
        for (const auto& e : s_DuLieu) {
            if ((e.m_IdJeu + "_").find("_" + s_Jeton + "_") != std::string::npos) s_Choix.push_back(e);
        }
    }

    return s_Choix.empty() ? s_DuLieu : s_Choix;
}

std::vector<std::string> JetonsDesTenues(const SPlanification& p_Planif, const std::vector<std::string>& p_NomsInternes) {
    std::vector<std::string> s_Jetons;
    for (const auto& e : p_Planif.m_Entrees) {
        for (const auto& j : e.m_Tenues) {
            auto s_Jeton = Haut(j);
            if (s_Jeton.rfind("TOKEN_OUTFIT_", 0) != 0) continue;
            const auto s_Nom = s_Jeton.substr(13) + "_";
            for (const auto& n : p_NomsInternes) {
                if ((Haut(n) + "_").rfind(s_Nom, 0) == 0 && std::ranges::find(s_Jetons, s_Jeton) == s_Jetons.end()) {
                    s_Jetons.push_back(s_Jeton);
                }
            }
        }
    }
    return s_Jetons;
}

std::vector<std::string> CiblesDesObjectifs(const json& p_Objectifs) {
    std::vector<std::string> s_Cibles;
    if (!p_Objectifs.is_array()) return s_Cibles;

    for (const auto& o : p_Objectifs) {
        if (!o.is_object()) continue;
        std::string s_Id;

        if (Txt(Chemin(o, { "SuccessEvent" }), "EventName") == "Kill") {
            s_Id = Txt(Chemin(o, { "SuccessEvent", "EventValues" }), "RepositoryId");
        }
        else if (Txt(o, "Type") == "kill") {
            s_Id = Txt(Chemin(o, { "Properties" }), "Id");
        }
        else if (Txt(o, "Category") == "primary" || Txt(o, "ObjectiveType") == "kill") {
            // Machine à états : ses cibles sont dans « Context.Targets » — une
            // seule (« kill »), ou un groupe (Berlin : 5 agents parmi 11).
            const auto& t = Chemin(o, { "Definition", "Context", "Targets" });
            if (t.is_array()) {
                for (const auto& x : t) {
                    if (!x.is_string()) continue;
                    const auto s_Cible = Bas(x.get<std::string>());
                    if (!s_Cible.empty() && std::ranges::find(s_Cibles, s_Cible) == s_Cibles.end()) s_Cibles.push_back(s_Cible);
                }
            }
        }

        s_Id = Bas(s_Id);
        if (!s_Id.empty() && std::ranges::find(s_Cibles, s_Id) == s_Cibles.end()) s_Cibles.push_back(s_Id);
    }

    return s_Cibles;
}

bool LirePlanification(const json& p_Page, SPlanification& p_Planif, const Traducteur& p_Traduire) {
    const auto& d = Chemin(p_Page, { "data" });
    const auto& s_Entrees = Chemin(d, { "Entrances" });
    const auto& M = Chemin(d, { "Contract", "Metadata" });

    if (!s_Entrees.is_array() || !Chemin(d, { "LoadoutData" }).is_array() || !M.is_object()) return false;

    SPlanification s;
    s.m_IdContrat = Txt(M, "Id");
    s.m_Lieu = Txt(M, "Location");
    s.m_CleTitre = Txt(M, "Title");
    s.m_Type = Txt(M, "Type");
    if (s.m_IdContrat.empty()) return false;

    for (const auto* s_Liste : { &Chemin(d, { "Objectives" }), &Chemin(d, { "Contract", "Data", "Objectives" }) }) {
        for (auto& c : CiblesDesObjectifs(*s_Liste)) {
            if (std::ranges::find(s.m_Cibles, c) == s.m_Cibles.end()) s.m_Cibles.push_back(std::move(c));
        }
    }

    std::set<std::string> s_Debloquees;
    if (const auto& u = Chemin(d, { "UnlockedEntrances" }); u.is_array()) {
        for (const auto& x : u) if (x.is_string()) s_Debloquees.insert(Bas(x.get<std::string>()));
    }

    for (const auto& e : s_Entrees) {
        SEntree s_Entree;
        s_Entree.m_Id = Bas(Txt(Chemin(e, { "Properties" }), "RepositoryId"));
        s_Entree.m_IdJeu = Txt(e, "Id");
        s_Entree.m_CleNom = Txt(e, "DisplayNameLocKey");
        LireReglesEntree(Chemin(e, { "Properties" }), s_Entree);
        if (s_Entree.m_Id.empty() || !s_Debloquees.contains(s_Entree.m_Id)) continue;
        s_Entree.m_Nom = Traduit(p_Traduire, s_Entree.m_CleNom, s_Entree.m_IdJeu);
        s.m_Entrees.push_back(std::move(s_Entree));
    }

    p_Planif = std::move(s);
    return true;
}

json DestinationsEnJson(const std::vector<SDestination>& p_Destinations) {
    json s_Liste = json::array();

    for (const auto& d : p_Destinations) {
        json s_Missions = json::array();
        for (const auto& m : d.m_Missions) {
            s_Missions.push_back({ { "id", m.m_Id }, { "titre", m.m_Titre }, { "image", m.m_Image }, { "type", m.m_Type },
                                   { "cle", m.m_CleTitre }, { "lieu", m.m_LieuMission }, { "cibles", m.m_Cibles },
                                   { "entrees", m.m_Entrees }, { "brut", m.m_Brut } });
        }

        s_Liste.push_back({ { "id", d.m_Id }, { "nom", d.m_Nom }, { "image", d.m_Image }, { "fond", d.m_Fond },
                            { "verrouillee", d.m_Verrouillee }, { "missions", s_Missions }, { "brut", d.m_Brut } });
    }

    return s_Liste;
}

std::vector<SDestination> DestinationsDepuisJson(const json& p_Json) {
    std::vector<SDestination> s_Liste;
    if (!p_Json.is_array()) return s_Liste;

    for (const auto& x : p_Json) {
        SDestination d;
        d.m_Id = Txt(x, "id");
        if (d.m_Id.empty()) continue;
        d.m_Nom = Txt(x, "nom");
        d.m_Image = Txt(x, "image");
        d.m_Fond = Txt(x, "fond");
        d.m_Verrouillee = Vrai(x, "verrouillee");
        if (const auto& b = Chemin(x, { "brut" }); b.is_object()) d.m_Brut = b;

        if (const auto& m = Chemin(x, { "missions" }); m.is_array()) {
            for (const auto& y : m) {
                SMissionConnue c { Txt(y, "id"), Txt(y, "titre"), Txt(y, "image"), Txt(y, "type"), Txt(y, "cle"), Txt(y, "lieu") };
                for (const char* k : { "cibles", "entrees" }) {
                    if (const auto& l = Chemin(y, { k }); l.is_array()) {
                        for (const auto& v : l) {
                            if (v.is_string()) (std::string(k) == "cibles" ? c.m_Cibles : c.m_Entrees).push_back(v.get<std::string>());
                        }
                    }
                }
                if (const auto& b = Chemin(y, { "brut" }); b.is_object()) c.m_Brut = b;
                d.m_Missions.push_back(std::move(c));
            }
        }

        s_Liste.push_back(std::move(d));
    }

    return s_Liste;
}

// ---------------------------------------------------------------- Tirages

bool TirerDestination(const std::vector<SDestination>& p_Destinations, std::mt19937& p_Hasard, int& p_Lieu, int& p_Mission) {
    const auto Jouables = [](const SDestination& d) {
        std::vector<int> s_Index;
        for (size_t j = 0; j < d.m_Missions.size(); ++j) {
            if (!d.m_Missions[j].m_Cibles.empty() && d.m_Missions[j].m_Brut.is_object()) s_Index.push_back(static_cast<int>(j));
        }
        return s_Index;
    };

    std::vector<int> s_Lieux;
    for (size_t i = 0; i < p_Destinations.size(); ++i) {
        if (!p_Destinations[i].m_Verrouillee && !Jouables(p_Destinations[i]).empty()) s_Lieux.push_back(static_cast<int>(i));
    }
    if (s_Lieux.empty()) return false;

    p_Lieu = s_Lieux[std::uniform_int_distribution<size_t>(0, s_Lieux.size() - 1)(p_Hasard)];
    const auto s_Missions = Jouables(p_Destinations[p_Lieu]);
    p_Mission = s_Missions[std::uniform_int_distribution<size_t>(0, s_Missions.size() - 1)(p_Hasard)];
    return true;
}

SPreparation TirerPreparation(const SPlanification& p_Planif, const std::vector<SObjetInventaire>& p_Inventaire,
                              const STirage& p_Tirage, EMode p_Mode, std::mt19937& p_Hasard,
                              const std::vector<std::string>& p_JetonsVoulus) {
    SPreparation p;
    p.m_IdContrat = p_Planif.m_IdContrat;

    // ---- Entrée : une de celles que le joueur a débloquées.
    bool s_ArmePermise = true;
    int s_Equipements = 2;
    // Les entrées d'une autre mission du même lieu sont écartées : à Dartmoor,
    // « STARTING_LOCATION_ANCESTRAL_FILUR_OUTBREAK » (nom « Unknown ») est
    // débloquée mais n'existe pas dans la scène de la mission, et le jeu part
    // alors de l'entrée par défaut (essai du 24/11/2026). Quand des entrées
    // portent le nom de code de la mission (« …_ANCESTRAL_BULLDOG_… »), seules
    // celles-là restent ; sinon (Sapienza : « SAPIENZA » pour LOCATION_COASTALTOWN)
    // toutes. Un nom « Unknown » n'est jamais tiré quand il y a mieux.
    std::vector<const SEntree*> s_Candidates;
    {
        const std::string s_Code = p_Planif.m_Lieu.rfind("LOCATION_", 0) == 0 ? p_Planif.m_Lieu.substr(9) : std::string();
        const std::string s_Prefixe = "STARTING_LOCATION_" + s_Code + "_";
        for (const auto& e : p_Planif.m_Entrees) {
            if (!s_Code.empty() && Haut(e.m_IdJeu).rfind(s_Prefixe, 0) == 0) s_Candidates.push_back(&e);
        }
        if (s_Candidates.empty()) {
            for (const auto& e : p_Planif.m_Entrees) s_Candidates.push_back(&e);
        }
        std::vector<const SEntree*> s_Nommees;
        for (const auto* e : s_Candidates) {
            if (!e->m_Nom.empty() && e->m_Nom != "Unknown") s_Nommees.push_back(e);
        }
        if (!s_Nommees.empty()) s_Candidates = std::move(s_Nommees);
    }

    // Le mode resserre encore : Assassin et Puriste partent en costume
    // (« toujours en costume de 47 »), l'Assassin avec son arme, tous deux avec
    // leurs deux équipements. Une exigence que nulle entrée ne remplit est
    // abandonnée plutôt que de laisser la mission sans entrée.
    const auto Resserrer = [&](auto p_Garder) {
        std::vector<const SEntree*> s_Reste;
        for (const auto* e : s_Candidates) if (p_Garder(*e)) s_Reste.push_back(e);
        if (!s_Reste.empty()) s_Candidates = std::move(s_Reste);
    };
    if (p_Mode != EMode::Freestyle) {
        // Le costume, lui, n'est jamais abandonné. Les entrées où 47 part en
        // costume sont souvent « débloquées d'office » et absentes de
        // l'inventaire (New York, 24/11/2026 : seules les cinq déguisées y
        // sont) : sans aucune, l'entrée attend la page de préparation, qui
        // les liste toutes (Roulette::PreparerAvantMission la choisit là).
        std::erase_if(s_Candidates, [](const SEntree* e) { return !e->m_EnCostume; });
        if (p_Mode == EMode::Assassin) Resserrer([](const SEntree& e) { return e.m_ArmePermise; });
        Resserrer([](const SEntree& e) { return e.m_Equipements >= 2; });
    }

    // Freestyle : une cible à tuer dans une tenue donnée, et une entrée qui fait
    // partir 47 dans cette tenue — on part de là (Julien, 24/11/2026).
    if (p_Mode == EMode::Freestyle && !p_JetonsVoulus.empty()) {
        Resserrer([&](const SEntree& e) {
            for (const auto& j : e.m_Tenues) {
                if (std::ranges::find(p_JetonsVoulus, Haut(j)) != p_JetonsVoulus.end()) return true;
            }
            return false;
        });
    }

    if (!s_Candidates.empty()) {
        const auto& e = *s_Candidates[std::uniform_int_distribution<size_t>(0, s_Candidates.size() - 1)(p_Hasard)];
        p.m_Entree.m_Id = e.m_Id;
        p.m_Entree.m_IdJeu = e.m_IdJeu;
        p.m_Entree.m_Nom = e.m_Nom;
        p.m_Entree.m_Cle = e.m_CleNom;
        p.m_Entree.m_Image = e.m_Image;
        s_ArmePermise = e.m_ArmePermise;
        s_Equipements = e.m_Equipements;
    }

    // ---- Assassin et Puriste : l'équipement de 47, toujours le même.
    const auto Parmi = [&](auto p_Convient) {
        std::vector<const SObjetInventaire*> s_Liste;
        for (const auto& o : p_Inventaire) if (p_Convient(o)) s_Liste.push_back(&o);
        return s_Liste;
    };
    // Le premier possédé dans l'ordre de préférence, sinon un au hasard parmi la famille.
    const auto Prefere = [&](std::initializer_list<const char*> p_Ordre, auto p_Famille) -> const SObjetInventaire* {
        for (const char* s_Id : p_Ordre) {
            for (const auto& o : p_Inventaire) if (Haut(o.m_IdJeu) == s_Id) return &o;
        }
        return AuHasard(Parmi(p_Famille), p_Hasard);
    };
    const auto Corde = [&] {
        return Prefere({ "PROP_MELEE_CLASSIC_FIBER_WIRE", "TOKEN_FIBERWIRE" }, [](const SObjetInventaire& o) {
            return o.m_Emplacement == "gear" && Contient(Haut(o.m_IdJeu), { "FIBER_WIRE", "FIBERWIRE" });
        });
    };
    // « Crochet MK (maximum débloqué) » : MK III (S3), puis MK II (S2), puis le premier.
    const auto Crochet = [&] {
        return Prefere({ "PROP_TOOL_LOCK_PICK_S3", "PROP_TOOL_LOCK_PICK_S2", "PROP_TOOL_LOCK_PICK" }, [](const SObjetInventaire& o) {
            return o.m_Emplacement == "gear" && Contient(Haut(o.m_IdJeu), { "LOCK_PICK" });
        });
    };
    const auto Pieces = [&] {
        return Prefere({ "PROP_TOOL_COIN", "PROP_TOOL_COIN_CLASSIC" }, [](const SObjetInventaire& o) {
            return o.m_Emplacement == "gear" && o.m_SousType == "distraction" && Contient(Haut(o.m_IdJeu), { "COIN" });
        });
    };
    // Deux équipements au plus, autant que l'entrée en admet.
    const auto Equiper = [&](const SObjetInventaire* p_Premier, const SObjetInventaire* p_Second) {
        if (p_Premier && s_Equipements >= 1) p.m_Objet = Imposer(*p_Premier);
        if (p_Second && s_Equipements >= (p_Premier ? 2 : 1)) (p_Premier ? p.m_Objet2 : p.m_Objet) = Imposer(*p_Second);
    };

    if (p_Mode == EMode::Assassin) {
        // « Silverballer avec silencieux (camo qui varie) » : les ICA 19 « Baller »
        // dont le dépôt dit le silencieux (SilverBaller, S2, GoldBaller,
        // IceBaller, FlowerBaller, Short Baller…). Le Classic Baller n'en a pas.
        auto s_Ballers = Parmi([](const SObjetInventaire& o) {
            return o.m_Emplacement == "concealedweapon" && o.m_SousType == "pistol" && o.m_Silencieux && Contient(Haut(o.m_IdJeu), { "BALLER" });
        });
        if (s_Ballers.empty()) {
            s_Ballers = Parmi([](const SObjetInventaire& o) {
                return o.m_Emplacement == "concealedweapon" && Contient(Haut(o.m_IdJeu), { "SILVERBALLER" });
            });
        }
        if (const auto* o = AuHasard(s_Ballers, p_Hasard); o && s_ArmePermise) p.m_Arme = Imposer(*o);

        // Corde de piano et crochet ; des pièces si aucun crochet. (Deux
        // emplacements d'équipement seulement : le jeu n'en a pas plus.)
        const auto* s_Crochet = Crochet();
        Equiper(Corde(), s_Crochet ? s_Crochet : Pieces());
        return p;
    }

    if (p_Mode == EMode::Puriste) {
        // Aucune arme : la corde de piano et des pièces, l'équipement du SASO.
        Equiper(Corde(), Pieces());
        return p;
    }

    // ---- Freestyle. Arme dissimulée : un pistolet possédé.
    std::vector<const SObjetInventaire*> s_Pistolets;
    for (const auto& o : p_Inventaire) {
        if (o.m_Emplacement == "concealedweapon" && o.m_SousType == "pistol") s_Pistolets.push_back(&o);
    }
    if (const auto* o = AuHasard(s_Pistolets, p_Hasard); o && s_ArmePermise) p.m_Arme = Imposer(*o);

    // ---- Équipement : ce qu'exige la méthode d'une cible, si le joueur l'a.
    // Les identifiants du jeu disent ce qu'est l'objet (relevé de l'inventaire
    // du 22/11/2026) : PROP_MELEE_SYRINGE_LETHAL, PROP_POISON_PILLS_LETHAL,
    // PROP_DEVICE_ICA_MODULAR_REMOTE_EXPLOSIVE, PROP_MELEE_CLASSIC_FIBER_WIRE…
    const auto Convient = [](const std::string& p_Methode, const SObjetInventaire& o) {
        const auto s_Id = Haut(o.m_IdJeu);
        if (o.m_Emplacement != "gear") return false;

        if (p_Methode == "poison_injecte") return o.m_SousType == "poison" && Contient(s_Id, { "SYRINGE" }) && Contient(s_Id, { "LETHAL" });
        if (p_Methode == "poison" || p_Methode == "poison_ingere") {
            return o.m_SousType == "poison" && Contient(s_Id, { "LETHAL" }) && !Contient(s_Id, { "SYRINGE" });
        }
        if (p_Methode == "explosif") {
            return o.m_SousType == "explosive" && Contient(s_Id, { "EXPLOSIVE" }) && !Contient(s_Id, { "BREACH", "SEDATIVE", "EMETIC" });
        }
        if (p_Methode == "corde") return Contient(s_Id, { "FIBER_WIRE", "FIBERWIRE", "PHONE_CORD" });
        if (p_Methode == "melee") {
            return o.m_SousType == "melee" &&
                   Contient(s_Id, { "KNIFE", "DAGGER", "KUKRI", "ICE_PICK", "HATCHET", "MACHETE", "KATANA", "BLADE", "SCALPEL", "SWORD", "AXE" });
        }
        return false;
    };

    std::vector<const SObjetInventaire*> s_Choix;

    for (const auto& c : p_Tirage.m_Cibles) {
        if (c.m_Methode < 0) continue;
        const std::string s_Methode = Methodes()[c.m_Methode].m_Cle;
        for (const auto& o : p_Inventaire) if (Convient(s_Methode, o)) s_Choix.push_back(&o);
        if (!s_Choix.empty()) break;
    }

    // Sinon un outil ou une diversion (crochet, pièce, brouilleur…).
    if (s_Choix.empty()) {
        for (const auto& o : p_Inventaire) {
            if (o.m_Emplacement == "gear" && (o.m_SousType == "tool" || o.m_SousType == "distraction")) s_Choix.push_back(&o);
        }
    }

    if (const auto* o = AuHasard(s_Choix, p_Hasard); o && s_Equipements > 0) p.m_Objet = Imposer(*o);

    return p;
}

// ---------------------------------------------------------------- Écriture

bool ImposerPlanification(json& p_Page, const SPreparation& p_Preparation) {
    if (!p_Page.is_object() || !p_Page.contains("data") || !p_Page["data"].is_object()) return false;
    auto& d = p_Page["data"];
    bool s_Change = false;

    // Seule l'entrée tirée reste proposée (si elle est bien dans la liste).
    if (!p_Preparation.m_Entree.Vide() && d.contains("Entrances") && d["Entrances"].is_array()) {
        json s_Gardees = json::array();
        for (const auto& e : d["Entrances"]) {
            if (Bas(Txt(Chemin(e, { "Properties" }), "RepositoryId")) == p_Preparation.m_Entree.m_Id) s_Gardees.push_back(e);
        }
        if (!s_Gardees.empty()) {
            s_Change |= s_Gardees.size() != d["Entrances"].size();
            d["Entrances"] = std::move(s_Gardees);
        }
    }

    // L'arme et les objets tirés, présélectionnés (« Recommended », comme le jeu
    // présélectionne le dernier équipement utilisé). Le premier emplacement
    // « gear » reçoit l'objet, le second le second objet (ou reste libre).
    if (d.contains("LoadoutData") && d["LoadoutData"].is_array()) {
        int s_Gears = 0;

        for (auto& s_Emplacement : d["LoadoutData"]) {
            const auto s_Nom = Txt(s_Emplacement, "SlotName");
            const SImpose* s_Impose = nullptr;

            if (s_Nom == "concealedweapon") s_Impose = &p_Preparation.m_Arme;
            else if (s_Nom == "gear" && s_Gears < 2) {
                s_Impose = s_Gears == 0 ? &p_Preparation.m_Objet : &p_Preparation.m_Objet2;
                ++s_Gears;
            }

            if (!s_Impose || s_Impose->Vide() || !s_Impose->m_Brut.is_object()) continue;

            json s_Objet = s_Impose->m_Brut;
            if (!s_Objet.contains("Properties")) s_Objet["Properties"] = json::object();
            s_Emplacement["Recommended"] = { { "item", s_Objet }, { "type", s_Nom }, { "owned", true } };
            s_Change = true;
        }
    }

    return s_Change;
}

bool ImposerDestination(json& p_Page, const std::string& p_IdLieu) {
    if (p_IdLieu.empty() || !p_Page.contains("data") || !p_Page["data"].is_object()) return false;
    auto& d = p_Page["data"];
    if (!d.contains("DestinationsData") || !d["DestinationsData"].is_array()) return false;

    json s_Gardees = json::array();
    for (const auto& x : d["DestinationsData"]) {
        if (Txt(Chemin(x, { "Location" }), "Id") == p_IdLieu) s_Gardees.push_back(x);
    }

    if (s_Gardees.empty()) return false;
    d["DestinationsData"] = std::move(s_Gardees);
    return true;
}

bool ImposerMission(json& p_Page, const std::string& p_IdContrat) {
    if (p_IdContrat.empty()) return false;
    const auto& s_Lieux = Chemin(p_Page, { "data", "MissionData", "SubLocationMissionsData" });
    if (!s_Lieux.is_array()) return false;

    const auto IdDe = [](const json& m) {
        const auto& U = m.contains("UserCentricContract") && m["UserCentricContract"].is_object() ? m["UserCentricContract"] : m;
        return Txt(Chemin(U, { "Contract", "Metadata" }), "Id");
    };
    // Une liste de missions : un tableau d'objets portant un contrat.
    const auto EstListe = [&](const json& v) {
        return v.is_array() && !v.empty() && v[0].is_object() &&
               (v[0].contains("UserCentricContract") || v[0].contains("Contract"));
    };

    // La mission tirée est-elle sur cette page ?
    bool s_Trouvee = false;
    for (const auto& s : s_Lieux) {
        if (!s.is_object()) continue;
        for (const auto& [k, v] : s.items()) {
            if (!EstListe(v)) continue;
            for (const auto& m : v) if (IdDe(m) == p_IdContrat) s_Trouvee = true;
        }
    }
    if (!s_Trouvee) return false;

    json s_Gardes = json::array();
    for (auto s : s_Lieux) {
        if (!s.is_object()) continue;
        bool s_Contient = false;
        for (auto& [k, v] : s.items()) {
            if (!EstListe(v)) continue;
            json s_Reste = json::array();
            for (const auto& m : v) {
                if (IdDe(m) == p_IdContrat) {
                    s_Reste.push_back(m);
                    s_Contient = true;
                }
            }
            v = std::move(s_Reste);
        }
        if (s_Contient) s_Gardes.push_back(std::move(s));
    }

    p_Page["data"]["MissionData"]["SubLocationMissionsData"] = std::move(s_Gardes);
    return true;
}

bool ImposerPageLieu(json& p_Page, const SDestination& p_Lieu, const SMissionConnue& p_Mission) {
    if (!p_Mission.m_Brut.is_object()) return false;
    if (!p_Page.contains("data") || !p_Page["data"].is_object()) return false;
    auto& d = p_Page["data"];
    if (!d.contains("MissionData") || !d["MissionData"].is_object()) return false;
    auto& M = d["MissionData"];
    if (!M.contains("SubLocationMissionsData") || !M["SubLocationMissionsData"].is_array() || M["SubLocationMissionsData"].empty()) return false;

    // Le lieu affiché : celui de la destination tirée.
    if (p_Lieu.m_Brut.is_object()) {
        if (d.contains("Location")) d["Location"] = p_Lieu.m_Brut;
        if (M.contains("Location")) M["Location"] = p_Lieu.m_Brut;
    }

    // Un seul sous-lieu, celui de la mission, avec elle seule. Les autres
    // sections (escalades, arcade…) sont vidées.
    json s_Sous = M["SubLocationMissionsData"][0];
    for (auto& [k, v] : s_Sous.items()) {
        if (v.is_array()) v = json::array();
    }
    if (p_Mission.m_Brut.contains("Location")) s_Sous["Location"] = p_Mission.m_Brut["Location"];
    if (p_Mission.m_Brut.contains("SubLocation")) s_Sous["SubLocation"] = p_Mission.m_Brut["SubLocation"];
    s_Sous["Missions"] = json::array({ p_Mission.m_Brut });

    M["SubLocationMissionsData"] = json::array({ std::move(s_Sous) });
    return true;
}

bool ImposerListeEntrees(json& p_Page, const SPreparation& p_Preparation, const std::vector<SEntree>& p_Entrees) {
    if (p_Preparation.m_Entree.Vide() || !p_Page.contains("data") || !p_Page["data"].is_object()) return false;
    auto& d = p_Page["data"];
    if (!d.contains("OrderedUnlocks") || !d["OrderedUnlocks"].is_array()) return false;

    json s_Gardees = json::array();
    for (const auto& e : d["OrderedUnlocks"]) {
        if (Txt(e, "Subtype") != "startinglocation") return false; // pas une liste d'entrées
        if (Txt(e, "Id") == p_Preparation.m_Entree.m_IdJeu ||
            Bas(Txt(Chemin(e, { "Properties" }), "RepositoryId")) == p_Preparation.m_Entree.m_Id) {
            s_Gardees.push_back(e);
        }
    }

    if (s_Gardees.empty()) return false;
    bool s_Change = s_Gardees.size() != d["OrderedUnlocks"].size();
    d["OrderedUnlocks"] = std::move(s_Gardees);

    // Les autres entrées du lieu ne sont plus « débloquées ».
    if (d.contains("Unlocked") && d["Unlocked"].is_array()) {
        std::set<std::string> s_Autres;
        for (const auto& e : p_Entrees) if (e.m_Id != p_Preparation.m_Entree.m_Id) s_Autres.insert(e.m_Id);

        json s_Reste = json::array();
        for (const auto& x : d["Unlocked"]) {
            if (x.is_string() && s_Autres.contains(Bas(x.get<std::string>()))) continue;
            s_Reste.push_back(x);
        }
        if (s_Reste.size() != d["Unlocked"].size()) s_Change = true;
        d["Unlocked"] = std::move(s_Reste);
    }

    return s_Change;
}

std::string ContratDeLaPage(const json& p_Page) {
    return Txt(Chemin(p_Page, { "data", "UserCentric", "Contract", "Metadata" }), "Id");
}

bool ImposerEmplacement(json& p_Page, const SPreparation& p_Preparation, bool& p_Absent) {
    p_Absent = false;
    if (!p_Page.is_object() || !p_Page.contains("data") || !p_Page["data"].is_object()) return false;

    auto& d = p_Page["data"];
    if (!d.contains("LoadoutItemsData") || !d["LoadoutItemsData"].is_object()) return false;
    auto& s_Donnees = d["LoadoutItemsData"];
    if (!s_Donnees.contains("Items") || !s_Donnees["Items"].is_array()) return false;

    // Quel emplacement ? « 2 » arme dissimulée, « 4 » premier équipement (le
    // « 5 » reste libre), d'après l'écran de préparation relevé.
    const auto s_Slot = Txt(d, "SlotId").empty() ? Txt(s_Donnees, "SlotId") : Txt(d, "SlotId");
    const SImpose* s_Impose = s_Slot == "2" ? &p_Preparation.m_Arme : s_Slot == "4" ? &p_Preparation.m_Objet
                            : s_Slot == "5" ? &p_Preparation.m_Objet2 : nullptr;
    if (!s_Impose || s_Impose->Vide()) return false;

    json s_Gardes = json::array();
    for (const auto& i : s_Donnees["Items"]) {
        if (IdObjet(Chemin(i, { "Item" })) == s_Impose->m_Id) s_Gardes.push_back(i);
    }

    if (s_Gardes.empty()) {
        p_Absent = true; // interdit dans ce lieu : on laisse le choix plutôt qu'une liste vide
        return false;
    }

    s_Donnees["Items"] = std::move(s_Gardes);
    s_Donnees["HasMore"] = false;
    s_Donnees["HasMoreLeft"] = false;
    s_Donnees["HasMoreRight"] = false;
    return true;
}

// Verrouille la préparation par les moyens du jeu lui-même :
//  - « MandatoryLoadout » : l'équipement imposé (celui des missions spéciales).
//    Le modèle de la page de préparation retire tout l'écran « Préparation »
//    dès que ce champ existe, même vide : plus rien à changer, il ne reste que
//    « Jouer ». Écrit aussi quand rien n'est imposé (liste vide) : Julien
//    (24/11/2026) ne veut ni équipement, ni costume, ni entrée modifiables.
//  - « Entrances » : les entrées permises (une seule, la tirée), comme dans
//    les contrats spéciaux du jeu (Colorado).
bool AjouterEquipementObligatoire(json& p_Contrat, const SPreparation& p_Preparation) {
    if (!p_Contrat.is_object() || !p_Contrat.contains("Data") || !p_Contrat["Data"].is_object()) return false;

    json s_Liste = json::array();
    for (const auto* s : { &p_Preparation.m_Arme, &p_Preparation.m_Objet, &p_Preparation.m_Objet2 }) {
        if (s->Vide() || s->m_IdJeu.empty()) continue;
        s_Liste.push_back({ { "Id", s->m_IdJeu }, { "Properties", { { "LoadoutSlot", s->m_Emplacement }, { "RepositoryId", s->m_Id } } } });
    }

    p_Contrat["Data"]["MandatoryLoadout"] = std::move(s_Liste);
    if (!p_Preparation.m_Entree.Vide()) p_Contrat["Data"]["Entrances"] = json::array({ p_Preparation.m_Entree.m_Id });
    return true;
}
