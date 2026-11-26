#include "Contrat.h"
#include "Langue.h"

#include <format>
#include <string>
#include <vector>

using nlohmann::json;

namespace {
    std::string NouveauGuid(std::mt19937& p_Hasard) {
        std::uniform_int_distribution<uint32_t> s_Octet(0, 255);
        uint8_t b[16];
        for (auto& o : b) o = static_cast<uint8_t>(s_Octet(p_Hasard));
        b[6] = (b[6] & 0x0F) | 0x40; // version 4
        b[8] = (b[8] & 0x3F) | 0x80;
        return std::format(
            "{:02x}{:02x}{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}",
            b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]
        );
    }

    json Egal(const char* p_Champ, const json& p_Valeur) {
        return { { "$eq", json::array({ std::string("$Value.") + p_Champ, p_Valeur }) } };
    }

    // Un objectif primaire, compté hors score (le score reste celui du jeu).
    json Objectif(std::mt19937& p_Hasard, const std::string& p_Texte, json p_Definition) {
        return {
            { "Id", NouveauGuid(p_Hasard) },
            { "Category", "primary" },
            { "ExcludeFromScoring", true },
            { "ForceShowOnLoadingScreen", true },
            { "BriefingName", p_Texte },
            { "BriefingText", p_Texte },
            { "LongBriefingText", p_Texte },
            { "HUDTemplate", { { "display", p_Texte } } },
            { "Type", "statemachine" },
            { "Definition", std::move(p_Definition) },
        };
    }

    // Réussie d'emblée, échouée au premier des événements donnés.
    json Interdiction(std::mt19937& p_Hasard, const std::string& p_Texte, json p_Echecs) {
        json s_Etats = {
            { "Start", { { "-", { { "Transition", "Success" } } } } },
            { "Success", std::move(p_Echecs) },
        };

        return Objectif(p_Hasard, p_Texte, { { "Context", json::object() }, { "States", std::move(s_Etats) } });
    }

    const json c_Echec = json::array({ { { "Transition", "Failure" } } });

    const char* const c_Nul = "00000000-0000-0000-0000-000000000000";

    // La valeur « KillMethod » des conditions de cible, telle que les escalades
    // du jeu l'écrivent (relevé de leurs contrats) ; "" = pas d'icône native
    // (le texte de l'objectif dit quand même la méthode).
    std::string MethodeDuJeu(const SMethode& m) {
        const std::string s_Stricte = m.m_Stricte, s_Large = m.m_Large;
        if (!s_Stricte.empty()) return s_Stricte == "injected_poison" ? std::string() : s_Stricte;
        if (s_Large == "melee") return "melee_lethal";
        return s_Large;
    }

    // Méthode et tenue d'une cible, au format des escalades (« TargetConditions »).
    json ConditionsDeCible(const STirage& p_Tirage, const SObjectifCible& c, bool p_Briefing) {
        json s_Conditions = json::array();
        const auto Condition = [&](const char* p_Type, const std::string& p_Repo, const std::string& p_Methode) {
            json x = { { "Type", p_Type }, { "HardCondition", true } };
            if (p_Briefing || !p_Repo.empty()) x["RepositoryId"] = p_Repo.empty() ? c_Nul : p_Repo;
            if (p_Briefing || !p_Methode.empty()) x["KillMethod"] = p_Methode;
            if (p_Briefing) x["ObjectiveId"] = c_Nul;
            s_Conditions.push_back(std::move(x));
        };

        if (c.m_Methode >= 0) {
            if (const auto s_Methode = MethodeDuJeu(Methodes()[c.m_Methode]); !s_Methode.empty()) Condition("killmethod", "", s_Methode);
        }
        if (c.m_Tenue >= 0 && c.m_Tenue < static_cast<int>(p_Tirage.m_Tenues.size())) {
            const auto& t = p_Tirage.m_Tenues[c.m_Tenue];
            if (t.m_EstCostume) Condition("hitmansuit", "", "");
            else Condition("disguise", t.m_Id, "");
        }
        return s_Conditions;
    }


    json EchecSi(json p_Condition) {
        return json::array({ { { "Condition", std::move(p_Condition) }, { "Transition", "Failure" } } });
    }
}

int MasquerObjectifsDuJeu(json& p_Donnees, const std::vector<std::string>& p_IdsCibles) {
    if (!p_Donnees.is_object() || !p_Donnees.contains("Objectives") || !p_Donnees["Objectives"].is_array()) return 0;

    const json s_Invisible = {
        { "IfInProgress", { { "Visible", false } } },
        { "IfCompleted", { { "Visible", false } } },
        { "IfFailed", { { "Visible", false } } },
    };

    int s_Masques = 0;

    for (auto& o : p_Donnees["Objectives"]) {
        if (!o.is_object()) continue;

        // Deux formats dans les contrats du jeu (vérifiés sur les 50 missions,
        // tools/essai_missions.cpp) : « Category » (primary, secondary,
        // condition) et l'ancien « Primary » : true, sans catégorie (Paris,
        // Sapienza, Marrakech, Bangkok…). Les « condition » sont des
        // déclencheurs muets des scripts : on n'y touche pas.
        if (o.contains("Category")) {
            const auto s_Categorie = o.value("Category", std::string());
            if (s_Categorie != "primary" && s_Categorie != "secondary") continue;
            o["Category"] = "secondary";
        }
        else {
            o["Primary"] = false;
        }

        o["IsHidden"] = true;
        o["OnActive"] = s_Invisible;
        if (o.contains("OnInactive")) o["OnInactive"] = s_Invisible;
        if (o.contains("ForceShowOnLoadingScreen")) o["ForceShowOnLoadingScreen"] = false;
        ++s_Masques;
    }

    // La sortie : ouverte une fois toutes les cibles de la roulette éliminées
    // (le jeu l'ouvrait sur SES objectifs, désormais facultatifs).
    if (!p_IdsCibles.empty()) {
        json s_Toutes = json::array();
        for (const auto& id : p_IdsCibles) s_Toutes.push_back({ { "$eq", json::array({ "$" + id, "Completed" }) } });
        p_Donnees["EnableExits"] = { { "$and", std::move(s_Toutes) } };
    }

    return s_Masques;
}

// L'image du jeu de chaque complication (celle des conditions de contrat).
void ReglerContrat(json& p_Donnees, EMode p_Mode) {
    if (!p_Donnees.is_object()) return;

    // Professionnel pour tous les contrats (Julien, 25/11/2026).
    if (auto s_It = p_Donnees.find("GameDifficulties"); s_It != p_Donnees.end() && s_It->is_array()) {
        json s_Gardees = json::array();
        for (const auto& d : *s_It) {
            if (d.is_object() && d.value("Difficulty", "") == "normal") s_Gardees.push_back(d);
        }
        if (s_Gardees.empty()) s_Gardees.push_back({ { "Difficulty", "normal" }, { "Bricks", json::array() } });
        *s_It = std::move(s_Gardees);
    }
    else {
        p_Donnees["GameDifficulties"] = json::array({ { { "Difficulty", "normal" }, { "Bricks", json::array() } } });
    }

    // Sans filet, dans tous les modes (Julien, 25/11/2026) : ni sauvegarde ni
    // chargement, comme une cible fugitive.
    (void) p_Mode;
    p_Donnees["EnableSaving"] = false;
}

const char* ImageComplication(const std::string& p_Cle) {
    if (p_Cle == "sans_etre_repere") return "images/contractconditions/condition_contrac_do_not_be_spotted.jpg";
    if (p_Cle == "aucun_corps") return "images/contractconditions/condition_contrac_no_bodies_found.jpg";
    if (p_Cle == "aucune_victime") return "images/contractconditions/condition_contrac_targets_only.jpg";
    if (p_Cle == "aucune_neutralisation") return "images/contracts/gamechangers/Gamechanger_Global_NoPacifications.jpg";
    if (p_Cle == "aucun_changement") return "images/contractconditions/condition_contract_no_disguise_changes.jpg";
    if (p_Cle == "aucun_enregistrement") return "images/contracts/gamechangers/gamechanger_global_bigbrother.jpg";
    if (p_Cle == "tirs_tete") return "images/contractconditions/condition_contrac_headshots_only.jpg";
    // Une rangée de balles, une tombée : l'image d'un palier d'escalade du jeu.
    if (p_Cle == "ordre_impose") return "images/challenges/profile_challenges/escalation_s2_tier_5.jpg";
    return "";
}

json ObjectifsRoulette(const STirage& p_Tirage, std::mt19937& p_Hasard, bool p_ToutesLesCibles, std::vector<std::string>* p_IdsCibles) {
    json s_Objectifs = json::array();

    // Les PNJ ajoutés comme cibles : ils ne comptent pas comme « victimes ».
    json s_IdsPnj = json::array();

    for (const auto& c : p_Tirage.m_Cibles) {
        if (!c.m_Officielle && !c.m_Id.empty()) s_IdsPnj.push_back(c.m_Id);
    }

    // ------------------------------------------------ Cibles
    for (const auto& c : p_Tirage.m_Cibles) {
        if (c.m_Id.empty() || c.m_Id.starts_with("pnj:")) continue;

        json s_Conditions = json::array({ Egal("RepositoryId", c.m_Id) });
        std::string s_Consigne;

        if (c.m_Methode >= 0) {
            const auto& m = Methodes()[c.m_Methode];
            // Méthode « stricte » : dans l'un ou l'autre champ (corde de piano
            // en 3.300 : KillMethodStrict vide, KillMethodBroad « fiberwire »).
            // Même règle que AppliquerEvenement (Tirage.cpp).
            s_Conditions.push_back(*m.m_Stricte
                ? json { { "$or", json::array({ Egal("KillMethodStrict", m.m_Stricte), Egal("KillMethodBroad", m.m_Stricte) }) } }
                : Egal("KillMethodBroad", m.m_Large));
            s_Consigne = Tx(std::string("methode.") + m.m_Cle);
        }

        if (c.m_Tenue >= 0 && c.m_Tenue < static_cast<int>(p_Tirage.m_Tenues.size())) {
            const auto& t = p_Tirage.m_Tenues[c.m_Tenue];
            s_Conditions.push_back(t.m_EstCostume ? Egal("OutfitIsHitmanSuit", true) : Egal("OutfitRepositoryId", t.m_Id));
            s_Consigne += (s_Consigne.empty() ? "" : ", ") + TxF("obj.tenue", t.m_Nom);
        }

        // Cible du contrat sans contrainte : son objectif à lui suffit — sauf
        // quand les objectifs du jeu sont masqués.
        if (c.m_Officielle && s_Conditions.size() == 1 && !p_ToutesLesCibles) continue;

        const auto s_Texte = TxF("obj.eliminer", c.m_Nom) + (s_Consigne.empty() ? "" : " — " + s_Consigne);

        json s_Tuer = json::array({
            { { "Condition", { { "$and", s_Conditions } } }, { "Transition", "Success" } },
            { { "Condition", Egal("RepositoryId", c.m_Id) }, { "Transition", "Failure" } },
        });

        // Un PNJ est déclaré cible ; une cible du contrat l'est déjà, mais quand
        // les objectifs du jeu sont masqués, c'est l'objectif de la roulette qui
        // la montre (portrait au chargement, liste des cibles).
        const bool s_Declaree = !c.m_Officielle || p_ToutesLesCibles;

        json s_Definition = {
            { "Context", { { "Targets", s_Declaree ? json::array({ c.m_Id }) : json::array() } } },
            { "States", { { "Start", { { "Kill", std::move(s_Tuer) } } } } },
        };

        auto s_Objectif = Objectif(p_Hasard, s_Texte, std::move(s_Definition));

        if (s_Declaree) {
            s_Objectif["ObjectiveType"] = "kill";
            if (!c.m_Image.empty()) s_Objectif["Image"] = c.m_Image;
        }

        // Méthode et tenue montrées par le jeu lui-même, comme en escalade.
        if (auto s_Conditions = ConditionsDeCible(p_Tirage, c, false); !s_Conditions.empty()) {
            s_Objectif["TargetConditions"] = std::move(s_Conditions);
        }

        if (p_IdsCibles) p_IdsCibles->push_back(s_Objectif["Id"].get<std::string>());
        s_Objectifs.push_back(std::move(s_Objectif));
    }

    // ------------------------------------------------ Complications
    for (const auto& s_Tiree : p_Tirage.m_Complications) {
        const auto& d = Complications()[s_Tiree.m_Index];
        const std::string s_Cle = d.m_Cle;
        const auto s_Texte = Tx("obj.prefixe") + Tx(std::string("compl.") + d.m_Cle + ".titre");

        json s_Echecs;

        if (s_Cle == "ordre_impose") {
            // Les cibles dans l'ordre de la fiche : chaque étape attend la
            // suivante, en tuer une autre avant fait échouer. Seulement des
            // cibles connues par leur identifiant (le tirage écarte sinon).
            std::vector<std::string> s_Ordre;
            for (const auto& c : p_Tirage.m_Cibles) {
                if (!c.m_Id.empty() && !c.m_Id.starts_with("pnj:")) s_Ordre.push_back(c.m_Id);
            }
            if (s_Ordre.size() < 2) continue;

            json s_Etats = json::object();
            for (size_t k = 0; k < s_Ordre.size(); ++k) {
                const std::string s_Etat = k == 0 ? "Start" : "Etape" + std::to_string(k + 1);
                const std::string s_Suite = k + 1 == s_Ordre.size() ? "Success" : "Etape" + std::to_string(k + 2);
                json s_Trop = json::array();
                for (size_t j = k + 1; j < s_Ordre.size(); ++j) s_Trop.push_back(Egal("RepositoryId", s_Ordre[j]));
                json s_Kill = json::array({ { { "Condition", Egal("RepositoryId", s_Ordre[k]) }, { "Transition", s_Suite } } });
                if (!s_Trop.empty()) s_Kill.push_back({ { "Condition", { { "$or", s_Trop } } }, { "Transition", "Failure" } });
                s_Etats[s_Etat] = { { "Kill", std::move(s_Kill) } };
            }

            auto s_Objectif = Objectif(p_Hasard, s_Texte, { { "Context", json::object() }, { "States", std::move(s_Etats) } });
            s_Objectif["Image"] = ImageComplication(s_Cle);
            s_Objectifs.push_back(std::move(s_Objectif));
            continue;
        }

        if (s_Cle == "sans_etre_repere") {
            // Délai de grâce : repéré dans les premières secondes (l'entrée
            // tirée met 47 en vue), la complication est écartée — l'objectif
            // passe à « Success » et n'écoute plus rien. Passé le délai, être
            // repéré fait échouer.
            const json s_Ecartee = { { "Ecartee", false } };
            json s_Etats = {
                { "Start", {
                    { "$timer", json::array({ { { "Condition", { { "$after", c_GraceDepart } } }, { "Transition", "Success" } } }) },
                    { "Spotted", json::array({ { { "Actions", { { "$set", json::array({ "Ecartee", true }) } } }, { "Transition", "Success" } } }) },
                } },
                { "Success", {
                    { "Spotted", json::array({ { { "Condition", { { "$eq", json::array({ "$.Ecartee", false }) } } }, { "Transition", "Failure" } } }) },
                } },
            };
            auto s_Objectif = Objectif(p_Hasard, s_Texte, { { "Context", s_Ecartee }, { "States", std::move(s_Etats) } });
            s_Objectif["Image"] = ImageComplication(s_Cle);
            s_Objectifs.push_back(std::move(s_Objectif));
            continue;
        }
        else if (s_Cle == "aucun_corps") {
            s_Echecs = { { "BodyFound", c_Echec } };
        }
        else if (s_Cle == "aucune_victime") {
            json s_NonCible = json::array({ Egal("IsTarget", false) });
            if (!s_IdsPnj.empty()) {
                json s_EstPnj = json::array();
                for (const auto& id : s_IdsPnj) s_EstPnj.push_back(Egal("RepositoryId", id));
                s_NonCible.push_back({ { "$not", { { "$or", s_EstPnj } } } });
            }
            s_Echecs = { { "Kill", EchecSi({ { "$and", s_NonCible } }) }, { "CrowdNPC_Died", c_Echec } };
        }
        else if (s_Cle == "aucune_neutralisation") {
            s_Echecs = { { "Pacify", EchecSi(Egal("IsTarget", false)) } };
        }
        else if (s_Cle == "aucun_changement") {
            s_Echecs = { { "Disguise", c_Echec } };
        }
        else if (s_Cle == "aucun_enregistrement") {
            s_Echecs = { { "SecuritySystemRecorder", EchecSi(Egal("event", "spotted")) } };
        }
        else if (s_Cle == "tirs_tete") {
            s_Echecs = { { "Kill", EchecSi({ { "$and", json::array({ Egal("KillClass", "ballistic"), Egal("IsHeadshot", false) }) } }) } };
        }
        else {
            continue;
        }

        auto s_Objectif = Interdiction(p_Hasard, s_Texte, std::move(s_Echecs));
        s_Objectif["Image"] = ImageComplication(s_Cle);
        s_Objectifs.push_back(std::move(s_Objectif));
    }

    return s_Objectifs;
}

json BriefingRoulette(const STirage& p_Tirage) {
    json s_Liste = json::array();

    for (const auto& c : p_Tirage.m_Cibles) {
        if (c.m_Id.empty() || c.m_Id.starts_with("pnj:")) continue;
        s_Liste.push_back({ { "Type", "kill" },
                            { "Properties", { { "Id", c.m_Id }, { "Conditions", ConditionsDeCible(p_Tirage, c, true) } } } });
    }

    for (const auto& s_Tiree : p_Tirage.m_Complications) {
        const auto& d = Complications()[s_Tiree.m_Index];
        const auto s_Texte = Tx("obj.prefixe") + Tx(std::string("compl.") + d.m_Cle + ".titre");
        s_Liste.push_back({ { "Type", "custom" },
                            { "Properties", {
                                  { "BriefingName", s_Texte },
                                  { "BriefingText", Tx(std::string("compl.") + d.m_Cle + ".detail") },
                                  { "LongBriefingText", Tx(std::string("compl.") + d.m_Cle + ".detail") },
                                  { "Image", ImageComplication(d.m_Cle) },
                                  { "DisplayAsKill", false },
                                  { "ObjectivesCategory", "primary" },
                                  { "ForceShowOnLoadingScreen", true },
                              } } });
    }

    return s_Liste;
}
