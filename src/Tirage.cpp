#include "Tirage.h"

#include <algorithm>
#include <cctype>

#include "Depot.h"
#include "Langue.h"
#include "Json.h"

// Valeurs KillMethodBroad / KillMethodStrict : celles relevées dans les défis
// officiels du jeu (accident_drown, accident_push, consumed_poison…). « melee »
// et « throw » restent à confirmer : evenements.log le dira à la première
// élimination de ce genre.
const std::vector<SMethode>& Methodes() {
    static const std::vector<SMethode> s_Methodes = {
        { "accident",           "accident",     "",                          "Accident (au choix)",            "Accidents" },
        { "noyade",             "",             "accident_drown",            "Noyade",                         "Accidents" },
        { "chute",              "",             "accident_push",             "Chute (poussée)",                "Accidents" },
        { "electrocution",      "",             "accident_electric",         "Électrocution",                  "Accidents" },
        { "objet_suspendu",     "",             "accident_suspended_object", "Objet suspendu",                 "Accidents" },
        { "brulure",            "",             "accident_burn",             "Brûlure",                        "Accidents" },
        { "explosion_accident", "",             "accident_explosion",        "Explosion accidentelle",         "Accidents" },
        { "poison",             "poison",       "",                          "Poison (au choix)",              "Poisons" },
        { "poison_ingere",      "",             "consumed_poison",           "Poison ingéré",                  "Poisons" },
        { "poison_injecte",     "",             "injected_poison",           "Poison injecté",                 "Poisons" },
        { "pistolet",           "pistol",       "",                          "Pistolet",                       "Armes à feu" },
        { "pm",                 "smg",          "",                          "Pistolet-mitrailleur",           "Armes à feu" },
        { "pompe",              "shotgun",      "",                          "Fusil à pompe",                  "Armes à feu" },
        { "assaut",             "assaultrifle", "",                          "Fusil d'assaut",                 "Armes à feu" },
        { "precision",          "sniperrifle",  "",                          "Fusil de précision",             "Armes à feu" },
        { "explosif",           "explosive",    "",                          "Explosif",                       "Explosifs" },
        { "corde",              "",             "fiberwire",                 "Corde de piano",                 "Corps à corps" },
        { "mains_nues",         "unarmed",      "",                          "À mains nues",                   "Corps à corps" },
        { "melee",              "melee",        "",                          "Arme de mêlée",                  "Corps à corps" },
    };

    return s_Methodes;
}

const char* CleMode(EMode p_Mode) {
    switch (p_Mode) {
        case EMode::Puriste: return "puriste";
        case EMode::Freestyle: return "freestyle";
        default: return "assassin";
    }
}

EMode ModeDepuisCle(const std::string& p_Cle) {
    if (p_Cle == "puriste") return EMode::Puriste;
    if (p_Cle == "freestyle") return EMode::Freestyle;
    return EMode::Assassin;
}

const char* LibelleMode(EMode p_Mode) {
    switch (p_Mode) {
        case EMode::Puriste: return "Puriste";
        case EMode::Freestyle: return "Freestyle";
        default: return "Assassin";
    }
}

const std::vector<const char*>& FamillesMethodes() {
    static const std::vector<const char*> s_Familles = {
        "Accidents", "Poisons", "Armes à feu", "Explosifs", "Corps à corps", "Au choix",
    };

    return s_Familles;
}

const std::vector<SComplication>& Complications() {
    static const std::vector<SComplication> s_Complications = {
        { "sans_etre_repere",       "Ne pas être repéré",              "Personne ne doit vous voir commettre un acte suspect." },
        { "aucun_corps",            "Aucun corps découvert",           "Les corps d'accidents ne comptent pas." },
        { "aucune_victime",         "Aucune victime collatérale",      "Seules les cibles meurent." },
        { "aucune_neutralisation",  "Aucune neutralisation",           "Ne neutralisez personne d'autre que les cibles." },
        { "aucun_changement",       "Aucun changement de tenue",       "Vous gardez votre tenue de départ." },
        { "aucun_enregistrement",   "Aucun enregistrement",            "Aucune caméra ne doit vous filmer." },
        { "tirs_tete",              "Tirs à la tête uniquement",       "Toute élimination par balle se fait à la tête." },
        { "ordre_impose",           "Ordre d'élimination imposé",      "Éliminez les cibles dans l'ordre de la fiche." },
    };

    return s_Complications;
}

namespace {
    int IndexFamille(const char* p_Famille) {
        const auto& s_Familles = FamillesMethodes();

        for (size_t i = 0; i < s_Familles.size(); ++i) {
            if (std::string_view(s_Familles[i]) == p_Famille) return static_cast<int>(i);
        }

        return -1;
    }

    bool EstArmeAFeu(const std::string& p_Large) {
        return p_Large == "pistol" || p_Large == "smg" || p_Large == "shotgun" || p_Large == "assaultrifle" ||
               p_Large == "sniperrifle";
    }

    int IndexMethode(const char* p_Cle) {
        const auto& s_Liste = Methodes();

        for (size_t i = 0; i < s_Liste.size(); ++i) {
            if (std::string_view(s_Liste[i].m_Cle) == p_Cle) return static_cast<int>(i);
        }

        return -1;
    }

    // Les méthodes de chaque mode (hors Freestyle, réglé dans la fiche).
    // Assassin : « avec le Silverballer, corde de piano, accident, poison (à
    // trouver dans la map) » (Julien). Puriste : les accidents précis plutôt que
    // « un accident », le poison et la corde — rien qui fasse du bruit.
    std::vector<const char*> MethodesDuMode(EMode p_Mode) {
        if (p_Mode == EMode::Puriste) {
            return { "noyade", "chute", "electrocution", "objet_suspendu", "brulure", "explosion_accident", "poison", "corde" };
        }
        return { "pistolet", "corde", "accident", "poison" };
    }

    // Les complications « réalistes » de l'Assassin : celles d'un tueur à gages
    // qui ne veut pas être vu. Le Puriste les a toutes, sauf les tirs à la tête
    // (il n'a pas d'arme) : c'est le Silent Assassin en costume.
    const std::vector<const char*>& ComplicationsAssassin() {
        static const std::vector<const char*> s_Liste = {
            "sans_etre_repere", "aucun_corps", "aucune_victime", "aucun_enregistrement", "aucun_changement", "tirs_tete",
            "ordre_impose",
        };
        return s_Liste;
    }

    const std::vector<const char*>& ComplicationsPuriste() {
        static const std::vector<const char*> s_Liste = {
            "sans_etre_repere", "aucun_corps", "aucune_victime", "aucun_enregistrement", "aucun_changement",
        };
        return s_Liste;
    }

    int IndexComplication(const char* p_Cle) {
        const auto& s_Liste = Complications();

        for (size_t i = 0; i < s_Liste.size(); ++i) {
            if (std::string_view(s_Liste[i].m_Cle) == p_Cle) return static_cast<int>(i);
        }

        return -1;
    }

    SComplicationTiree* Trouver(STirage& p_Tirage, const char* p_Cle) {
        const int s_Index = IndexComplication(p_Cle);

        for (auto& s_Complication : p_Tirage.m_Complications) {
            if (s_Complication.m_Index == s_Index) return &s_Complication;
        }

        return nullptr;
    }

    // Une complication déjà échouée le reste : rend vrai si l'état a changé.
    bool Echouer(STirage& p_Tirage, const char* p_Cle) {
        auto* s_Complication = Trouver(p_Tirage, p_Cle);

        if (!s_Complication || s_Complication->m_Etat == EEtatObjectif::Echoue) {
            return false;
        }

        s_Complication->m_Etat = EEtatObjectif::Echoue;
        return true;
    }
}

EEtatObjectif STirage::Bilan() const {
    bool s_ToutReussi = true;

    for (const auto& s_Cible : m_Cibles) {
        if (s_Cible.m_Etat == EEtatObjectif::Echoue) return EEtatObjectif::Echoue;
        if (s_Cible.m_Etat != EEtatObjectif::Reussi) s_ToutReussi = false;
    }

    for (const auto& s_Complication : m_Complications) {
        if (s_Complication.m_Etat == EEtatObjectif::Echoue) return EEtatObjectif::Echoue;
        if (s_Complication.m_Etat != EEtatObjectif::Reussi) s_ToutReussi = false;
    }

    for (const auto* s_Impose : { &m_Preparation.m_Arme, &m_Preparation.m_Objet, &m_Preparation.m_Objet2 }) {
        if (s_Impose->m_Etat == EEtatObjectif::Echoue) return EEtatObjectif::Echoue;
    }

    return s_ToutReussi && !m_Cibles.empty() ? EEtatObjectif::Reussi : EEtatObjectif::EnCours;
}

// Toutes les candidates à égalité : les cibles du contrat comme n'importe quel
// PNJ du lieu (Julien, 23/11/2026 : « les cibles vanilla sont sélectionnables
// mais au même titre d'un PNJ de la map »).
void ChoisirCibles(STirage& p_Tirage, const SReglages& p_Reglages, std::mt19937& p_Hasard) {
    auto s_Toutes = std::move(p_Tirage.m_Cibles);
    p_Tirage.m_Cibles.clear();

    // Réglage « cibles du jeu uniquement » : aucun PNJ.
    if (p_Reglages.m_CiblesDuJeuSeules) {
        std::erase_if(s_Toutes, [](const SObjectifCible& c) { return !c.m_Officielle; });
    }

    const int s_Min = std::clamp(p_Reglages.m_CiblesMin, 1, 3);
    const int s_Max = std::clamp(p_Reglages.m_CiblesMax, s_Min, 3);
    const int n = std::min(std::uniform_int_distribution<int>(s_Min, s_Max)(p_Hasard), static_cast<int>(s_Toutes.size()));

    std::ranges::shuffle(s_Toutes, p_Hasard);
    for (int i = 0; i < n; ++i) p_Tirage.m_Cibles.push_back(std::move(s_Toutes[i]));

    // Les cibles du contrat d'abord, dans l'affichage seulement.
    std::ranges::stable_partition(p_Tirage.m_Cibles, [](const SObjectifCible& c) { return c.m_Officielle; });
}

void Tirer(STirage& p_Tirage, const SReglages& p_Reglages, std::mt19937& p_Hasard) {
    const EMode s_Mode = p_Reglages.m_Mode;
    const bool s_Libre = s_Mode == EMode::Freestyle;
    p_Tirage.m_Mode = s_Mode;

    // Méthodes permises : celles du mode ; en Freestyle, celles des réglages.
    std::vector<int> s_Methodes;
    const auto& s_Toutes = Methodes();

    if (s_Libre) {
        for (size_t i = 0; i < s_Toutes.size(); ++i) {
            const int s_Famille = IndexFamille(s_Toutes[i].m_Famille);

            if (s_Famille >= 0 && p_Reglages.m_Familles[s_Famille]) {
                s_Methodes.push_back(static_cast<int>(i));
            }
        }

        // « Au choix » est une issue du tirage comme une autre, si elle est cochée.
        const bool s_AuChoixPermis = p_Reglages.m_Familles[IndexFamille("Au choix")];

        if (s_AuChoixPermis || s_Methodes.empty()) {
            s_Methodes.push_back(-1);
        }
    }
    else {
        for (const char* s_Cle : MethodesDuMode(s_Mode)) {
            if (const int i = IndexMethode(s_Cle); i >= 0) s_Methodes.push_back(i);
        }
        if (s_Methodes.empty()) s_Methodes.push_back(-1);
    }

    // Tenues permises : le costume de 47 seulement hors Freestyle (« toujours
    // en costume de 47 »).
    std::vector<int> s_Tenues;

    for (size_t i = 0; i < p_Tirage.m_Tenues.size(); ++i) {
        const bool s_Costume = p_Tirage.m_Tenues[i].m_EstCostume;
        if (s_Libre ? (!s_Costume || p_Reglages.m_AutoriserCostume) : s_Costume) {
            s_Tenues.push_back(static_cast<int>(i));
        }
    }

    const bool s_ImposerMethode = !s_Libre || p_Reglages.m_ImposerMethode;
    const bool s_ImposerTenue = !s_Libre || p_Reglages.m_ImposerTenue;

    for (auto& s_Cible : p_Tirage.m_Cibles) {
        s_Cible.m_Etat = EEtatObjectif::EnCours;
        s_Cible.m_Detail.clear();
        s_Cible.m_Methode = -1;
        s_Cible.m_Tenue = -1;

        if (s_ImposerMethode) {
            s_Cible.m_Methode = s_Methodes[std::uniform_int_distribution<size_t>(0, s_Methodes.size() - 1)(p_Hasard)];
        }

        if (s_ImposerTenue && !s_Tenues.empty()) {
            s_Cible.m_Tenue = s_Tenues[std::uniform_int_distribution<size_t>(0, s_Tenues.size() - 1)(p_Hasard)];
        }
    }

    // Complications, sans doublon et sans contradiction : « aucun changement de
    // tenue » n'a pas de sens si une tenue autre que le costume est imposée ;
    // « tirs à la tête » n'en a pas sans arme à feu (Assassin).
    bool s_TenueImposee = false;
    bool s_ArmeAFeu = false;

    for (const auto& s_Cible : p_Tirage.m_Cibles) {
        if (s_Cible.m_Tenue >= 0 && !p_Tirage.m_Tenues[s_Cible.m_Tenue].m_EstCostume) s_TenueImposee = true;
        if (s_Cible.m_Methode < 0 || EstArmeAFeu(s_Toutes[s_Cible.m_Methode].m_Large)) s_ArmeAFeu = true;
    }

    p_Tirage.m_Complications.clear();

    if (s_Mode == EMode::Puriste) {
        // Toujours les mêmes : c'est la règle du jeu, pas un tirage.
        for (const char* s_Cle : ComplicationsPuriste()) {
            if (const int i = IndexComplication(s_Cle); i >= 0) p_Tirage.m_Complications.push_back({ i, EEtatObjectif::EnCours });
        }
    }
    else {
        std::vector<int> s_Candidates;

        for (size_t i = 0; i < Complications().size(); ++i) {
            const std::string_view s_Cle = Complications()[i].m_Cle;
            if (s_TenueImposee && s_Cle == "aucun_changement") continue;
            // L'ordre : au moins deux cibles, toutes connues par leur identifiant.
            if (s_Cle == "ordre_impose" &&
                (p_Tirage.m_Cibles.size() < 2 ||
                 std::ranges::any_of(p_Tirage.m_Cibles, [](const SObjectifCible& c) { return c.m_Id.empty() || c.m_Id.starts_with("pnj:"); }))) continue;
            if (s_Mode == EMode::Assassin) {
                if (std::ranges::none_of(ComplicationsAssassin(), [&](const char* c) { return s_Cle == c; })) continue;
                if (s_Cle == "tirs_tete" && !s_ArmeAFeu) continue;
            }
            s_Candidates.push_back(static_cast<int>(i));
        }

        std::ranges::shuffle(s_Candidates, p_Hasard);

        for (int i = 0; i < std::clamp(p_Reglages.m_NbComplications, 0, 3) && i < static_cast<int>(s_Candidates.size()); ++i) {
            p_Tirage.m_Complications.push_back({ s_Candidates[i], EEtatObjectif::EnCours });
        }
    }

    p_Tirage.m_UneEliminationFaite = false;
    p_Tirage.m_Terminee = false;
    p_Tirage.m_Valide = true;
}

bool EcarterComplication(STirage& p_Tirage, const char* p_Cle) {
    const int s_Index = IndexComplication(p_Cle);
    return std::erase_if(p_Tirage.m_Complications, [&](const SComplicationTiree& c) { return c.m_Index == s_Index; }) > 0;
}

bool AppliquerEvenement(STirage& p_Tirage, const std::string& p_Nom, const nlohmann::json& p_Valeur, double p_Temps) {
    if (!p_Tirage.m_Valide || p_Tirage.m_Terminee) {
        return false;
    }

    bool s_Change = false;

    if (p_Nom == "Kill" && p_Valeur.is_object()) {
        const auto s_Id = EnMinuscules(ChampTexte(p_Valeur, "RepositoryId"));
        const auto s_Large = ChampTexte(p_Valeur, "KillMethodBroad");
        const auto s_Stricte = ChampTexte(p_Valeur, "KillMethodStrict");
        const auto s_Victime = CleNom(ChampTexte(p_Valeur, "ActorName"));

        // La victime est-elle une cible de la roulette ? Cible du contrat : par
        // son identifiant ; PNJ tiré au sort : par son nom (le jeu ne le compte
        // pas comme cible, IsTarget est faux).
        const auto Correspond = [&](const SObjectifCible& c) {
            if (!s_Id.empty() && c.m_Id == s_Id) return true;
            return !c.m_Officielle && !s_Victime.empty() && c.m_CleNom == s_Victime;
        };

        const bool s_EstCible = ChampBooleen(p_Valeur, "IsTarget") ||
                                std::ranges::any_of(p_Tirage.m_Cibles, Correspond);

        if (EstArmeAFeu(s_Large) && !ChampBooleen(p_Valeur, "IsHeadshot")) {
            s_Change |= Echouer(p_Tirage, "tirs_tete");
        }

        if (!s_EstCible) {
            s_Change |= Echouer(p_Tirage, "aucune_victime");
        }

        for (auto& s_Cible : p_Tirage.m_Cibles) {
            if (!Correspond(s_Cible) || s_Cible.m_Etat != EEtatObjectif::EnCours) {
                continue;
            }

            p_Tirage.m_UneEliminationFaite = true;

            std::string s_Detail;

            if (s_Cible.m_Methode >= 0) {
                const auto& s_Methode = Methodes()[s_Cible.m_Methode];
                const bool s_Ok = *s_Methode.m_Stricte ? s_Stricte == s_Methode.m_Stricte : s_Large == s_Methode.m_Large;

                if (!s_Ok) {
                    s_Detail = TxF("detail.methode", s_Stricte.empty() ? s_Large : s_Stricte);
                }
            }

            if (s_Cible.m_Tenue >= 0) {
                const auto& s_Tenue = p_Tirage.m_Tenues[s_Cible.m_Tenue];
                const auto s_TenuePortee = EnMinuscules(ChampTexte(p_Valeur, "OutfitRepositoryId"));
                const bool s_Ok = s_TenuePortee == s_Tenue.m_Id ||
                                  (s_Tenue.m_EstCostume && ChampBooleen(p_Valeur, "OutfitIsHitmanSuit"));

                if (!s_Ok) {
                    if (!s_Detail.empty()) s_Detail += " · ";
                    s_Detail += Tx("detail.tenue");
                }
            }

            // Ordre imposé : une cible plus haut sur la fiche encore en vie.
            if (const auto s_Rang = &s_Cible - p_Tirage.m_Cibles.data(); s_Rang > 0) {
                for (ptrdiff_t j = 0; j < s_Rang; ++j) {
                    if (p_Tirage.m_Cibles[j].m_Etat == EEtatObjectif::EnCours) {
                        s_Change |= Echouer(p_Tirage, "ordre_impose");
                        break;
                    }
                }
            }

            s_Cible.m_Etat = s_Detail.empty() ? EEtatObjectif::Reussi : EEtatObjectif::Echoue;
            s_Cible.m_Detail = std::move(s_Detail);
            s_Change = true;
        }
    }
    else if (p_Nom == "CrowdNPC_Died") {
        s_Change |= Echouer(p_Tirage, "aucune_victime");
    }
    else if (p_Nom == "Pacify" && p_Valeur.is_object() && !ChampBooleen(p_Valeur, "IsTarget")) {
        s_Change |= Echouer(p_Tirage, "aucune_neutralisation");
    }
    else if (p_Nom == "Disguise") {
        s_Change |= Echouer(p_Tirage, "aucun_changement");
    }
    else if (p_Nom == "Spotted" && p_Valeur.is_array() && !p_Valeur.empty()) {
        if (p_Temps >= 0.0 && p_Temps < c_GraceDepart) s_Change |= EcarterComplication(p_Tirage, "sans_etre_repere");
        else s_Change |= Echouer(p_Tirage, "sans_etre_repere");
    }
    else if (p_Nom == "BodyFound" || p_Nom == "DeadBodySeen" || p_Nom == "MurderedBodySeen") {
        s_Change |= Echouer(p_Tirage, "aucun_corps");
    }
    else if (p_Nom == "SecuritySystemRecorder" && p_Valeur.is_object() && ChampTexte(p_Valeur, "event") == "spotted") {
        // Comme l'objectif écrit dans le contrat (Contrat.cpp) : filmé = échoué,
        // sans rattrapage en effaçant l'enregistrement.
        s_Change |= Echouer(p_Tirage, "aucun_enregistrement");
    }

    return s_Change;
}

void Conclure(STirage& p_Tirage) {
    if (!p_Tirage.m_Valide || p_Tirage.m_Terminee) {
        return;
    }

    for (auto& s_Cible : p_Tirage.m_Cibles) {
        if (s_Cible.m_Etat == EEtatObjectif::EnCours) {
            s_Cible.m_Etat = EEtatObjectif::Echoue;
            s_Cible.m_Detail = Tx("detail.non_eliminee");
        }
    }

    for (auto& s_Complication : p_Tirage.m_Complications) {
        if (s_Complication.m_Etat == EEtatObjectif::EnCours) {
            s_Complication.m_Etat = EEtatObjectif::Reussi;
        }
    }

    for (auto* s_Impose : { &p_Tirage.m_Preparation.m_Entree, &p_Tirage.m_Preparation.m_Arme, &p_Tirage.m_Preparation.m_Objet, &p_Tirage.m_Preparation.m_Objet2 }) {
        if (!s_Impose->Vide() && s_Impose->m_Etat == EEtatObjectif::EnCours) s_Impose->m_Etat = EEtatObjectif::Reussi;
    }

    p_Tirage.m_Terminee = true;
}

bool VerifierPreparation(STirage& p_Tirage, const nlohmann::json& p_Depart) {
    auto& p = p_Tirage.m_Preparation;
    if (p.Vide() || !p_Depart.is_object()) return false;

    // Les RepositoryId de l'équipement emporté.
    std::vector<std::string> s_Emporte;
    if (const auto s_It = p_Depart.find("Loadout"); s_It != p_Depart.end() && s_It->is_array()) {
        for (const auto& o : *s_It) {
            if (o.is_object()) s_Emporte.push_back(EnMinuscules(ChampTexte(o, "RepositoryId")));
        }
    }

    bool s_Change = false;

    for (auto* s_Impose : { &p.m_Arme, &p.m_Objet, &p.m_Objet2 }) {
        if (s_Impose->Vide()) continue;
        const bool s_Ok = std::ranges::find(s_Emporte, s_Impose->m_Id) != s_Emporte.end();
        s_Impose->m_Etat = s_Ok ? EEtatObjectif::Reussi : EEtatObjectif::Echoue;
        s_Change = true;
    }

    return s_Change;
}

bool Rompu(const STirage& p_Tirage, std::string& p_Raison) {
    if (!p_Tirage.m_Valide) {
        return false;
    }

    for (const auto& s_Cible : p_Tirage.m_Cibles) {
        if (s_Cible.m_Etat == EEtatObjectif::Echoue) {
            p_Raison = s_Cible.m_Nom + (s_Cible.m_Detail.empty() ? "" : " — " + s_Cible.m_Detail);
            return true;
        }
    }

    for (const auto& s_Complication : p_Tirage.m_Complications) {
        const auto& s_Definition = Complications()[s_Complication.m_Index];

        if (s_Complication.m_Etat == EEtatObjectif::Echoue) {
            p_Raison = Tx(std::string("compl.") + s_Definition.m_Cle + ".titre");
            return true;
        }
    }

    for (const auto* s_Impose : { &p_Tirage.m_Preparation.m_Arme, &p_Tirage.m_Preparation.m_Objet, &p_Tirage.m_Preparation.m_Objet2 }) {
        if (s_Impose->m_Etat == EEtatObjectif::Echoue) {
            p_Raison = TxF("raison.preparation", s_Impose->m_Nom);
            return true;
        }
    }

    return false;
}

std::string CleNom(const std::string& p_Nom) {
    std::string s_Cle;

    for (const unsigned char c : p_Nom) {
        if (c < 0x80 && std::isalnum(c)) s_Cle += static_cast<char>(std::tolower(c));
    }

    return s_Cle;
}
