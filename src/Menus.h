#pragma once

// Les pages des menus que le serveur envoie au jeu (relevées le 22/11/2026,
// Retail/Roulette/reponses/) : on y lit ce que le joueur possède, et on y écrit
// la préparation tirée au sort.
//
//   - l'écran des destinations (« data.DestinationsData ») : les lieux, leur
//     image et « IsLocked » calculé par le serveur pour CE compte ;
//   - la page d'un lieu (« data.MissionData.SubLocationMissionsData ») : ses
//     missions, leur vignette et leur verrouillage ;
//   - l'inventaire (« Inventory », au démarrage) : tout ce que le joueur a
//     débloqué, avec son emplacement de préparation (« LoadoutSlot ») ;
//   - l'écran de préparation (« data.Entrances » + « data.LoadoutData ») : les
//     entrées du lieu, celles qui sont débloquées, l'équipement présélectionné
//     (« Recommended ») ;
//   - la liste d'un emplacement (« data.LoadoutItemsData.Items »), demandée quand
//     on clique sur l'arme ou l'équipement.
//
// Imposer, c'est réduire ces listes à l'élément tiré : le jeu n'offre plus que
// lui. Rien ici ne touche au jeu lui-même, tout est du JSON (essai hors du jeu :
// tools/essai_menus.cpp).

#include <functional>
#include <random>
#include <string>
#include <vector>

#include <set>

#include <nlohmann/json.hpp>

#include "Tirage.h"

using Traducteur = std::function<std::string(const std::string&)>;

struct SMissionConnue {
    std::string m_Id;     // identifiant du contrat
    std::string m_Titre;  // traduit
    std::string m_Image;  // vignette (images/contracts/…)
    std::string m_Type;   // mission, flashback, orbis (Sarajevo Six)…
    std::string m_CleTitre; // UI_CONTRACT_…_TITLE
    std::string m_LieuMission;          // LOCATION_WET_RAT (« Metadata.Location »)
    std::vector<std::string> m_Cibles;  // RepositoryId des cibles du contrat (ses objectifs)
    std::vector<std::string> m_Entrees; // entrées débloquées vues sur sa préparation (identifiants du jeu)
    nlohmann::json m_Brut;              // la mission telle que les menus la décrivent (gardée : sans elle, pas
                                        // de page de lieu à servir — flashback de Miami, 23/11/2026)
};

struct SDestination {
    std::string m_Id;      // LOCATION_PARENT_COASTALTOWN
    std::string m_Nom;     // traduit (« Sapienza »)
    std::string m_Image;   // vignette du lieu
    std::string m_Fond;    // grande image du lieu
    bool m_Verrouillee = false;
    std::vector<SMissionConnue> m_Missions; // histoire (menu principal) et pages des lieux ouvertes
    nlohmann::json m_Brut;                  // « Location » de l'écran des destinations (gardé)
};

struct SObjetInventaire {
    std::string m_Id;          // RepositoryId (minuscules)
    std::string m_IdJeu;       // FIREARMS_HERO_PISTOL_TACTICAL_ICA_19
    std::string m_Emplacement; // concealedweapon, gear, disguise…
    std::string m_SousType;    // pistol, poison, explosive, tool…
    std::string m_CleNom;      // UI_…_NAME
    std::string m_Cle2;        // Name_LOC du dépôt (seconde clé du nom), "" si aucune
    bool m_Silencieux = false; // arme à silencieux (dépôt)
    std::string m_Nom;         // traduit
    std::string m_Image;       // renseignée par le dépôt
    nlohmann::json m_Brut;     // l'objet de l'inventaire, tel quel
};

// Une entrée de la page de préparation.
struct SEntree {
    std::string m_Id;     // RepositoryId (minuscules)
    std::string m_IdJeu;  // STARTING_LOCATION_SAPIENZA_HARBOUR
    std::string m_CleNom;
    std::string m_Nom;
    std::string m_Image;
    std::string m_Parent; // LOCATION_PARENT_SAPIENZA (inventaire seulement)
    // « LoadoutSettings » de l'entrée : l'entrée du club de Berlin n'admet pas
    // d'arme dissimulée, deux autres aucun équipement (essai du 23/11/2026).
    bool m_ArmePermise = true;
    int m_Equipements = 2;
    // 47 y part en costume : aucune tenue imposée par l'entrée, ou une tenue
    // « HERO » (costumes de 47 : TOKEN_OUTFIT_HERO_…, …_HERO_EDGYSUIT).
    bool m_EnCostume = true;
    // La tenue de départ imposée (« Equip ») : des jetons TOKEN_OUTFIT_…, vide
    // si 47 part dans sa tenue.
    std::vector<std::string> m_Tenues;
};

// La page de préparation, lue.
struct SPlanification {
    std::string m_IdContrat;
    std::string m_Lieu;          // LOCATION_COASTALTOWN
    std::string m_CleTitre;      // UI_CONTRACT_OCTOPUS_TITLE
    std::string m_Type;          // mission, elusive…
    std::vector<std::string> m_Cibles;  // RepositoryId des cibles du contrat
    std::vector<SEntree> m_Entrees;     // les entrées DÉBLOQUÉES seulement
};

// ---------------------------------------------------------------- Lecture

// Écran des destinations : rend vrai et remplace p_Destinations (en gardant les
// missions déjà connues) si la page en est une.
bool LireDestinations(const nlohmann::json& p_Page, std::vector<SDestination>& p_Destinations, const Traducteur& p_Traduire);

// Menu principal (« data.StoryData ») : les missions de l'histoire de chaque
// saison, avec leur contrat. On connaît ainsi les missions de tous les lieux
// sans avoir ouvert leur page. Les ajoute à leur destination.
bool LireHistoire(const nlohmann::json& p_Page, std::vector<SDestination>& p_Destinations, const Traducteur& p_Traduire);

// Page d'un lieu : ses missions, rangées sous sa destination.
bool LireMissions(const nlohmann::json& p_Page, std::vector<SDestination>& p_Destinations, const Traducteur& p_Traduire);

// Réponse d'inventaire (« Inventory » en tête) : armes dissimulées et équipement.
bool LireInventaire(const nlohmann::json& p_Reponse, std::vector<SObjetInventaire>& p_Objets, const Traducteur& p_Traduire);

// La même réponse : les costumes de 47 possédés (emplacement « disguise »).
std::vector<SObjetInventaire> LireCostumes(const nlohmann::json& p_Reponse, const Traducteur& p_Traduire);

// La même réponse : les entrées possédées (« startinglocation »), avec leur lieu.
std::vector<SEntree> LireEntreesPossedees(const nlohmann::json& p_Reponse, const Traducteur& p_Traduire);

// Les entrées possédées qui vont avec une mission. Celles vues sur sa
// préparation si on les connaît ; sinon celles de son lieu (deux lieux ont un
// autre nom dans l'inventaire : Sapienza, Marrakech) dont l'identifiant porte
// celui de la mission (STARTING_LOCATION_WET_RAT_… pour LOCATION_WET_RAT),
// ou toutes celles du lieu si aucune ne le porte. Une entrée qui ne va pas est
// rattrapée sur la page de préparation.
std::vector<SEntree> EntreesPourMission(const std::vector<SEntree>& p_Possedees, const std::string& p_Parent,
                                        const SMissionConnue& p_Mission);

// Écran de préparation.
bool LirePlanification(const nlohmann::json& p_Page, SPlanification& p_Planif, const Traducteur& p_Traduire);

// Les cibles d'une liste d'objectifs, quel que soit le format : contrat
// (« SuccessEvent » Kill, ou machine à états « kill ») ou briefing de la
// préparation (« Type: kill, Properties.Id »). Un objectif de groupe (Berlin :
// « éliminez 5 agents » parmi 11, « Context.Targets ») donne tout le groupe.
std::vector<std::string> CiblesDesObjectifs(const nlohmann::json& p_Objectifs);

// Destinations ↔ fichier (Retail/Roulette/destinations.json).
nlohmann::json DestinationsEnJson(const std::vector<SDestination>& p_Destinations);
std::vector<SDestination> DestinationsDepuisJson(const nlohmann::json& p_Json);

// ---------------------------------------------------------------- Tirages

// Une destination possédée et une de ses missions débloquées dont on connaît
// les cibles. Rend faux s'il n'y a rien à tirer.
// p_Exclues : les lieux (identifiants) que le joueur a décochés dans les
// réglages ; s'ils le sont tous, aucun n'est écarté.
bool TirerDestination(const std::vector<SDestination>& p_Destinations, std::mt19937& p_Hasard, int& p_Lieu, int& p_Mission,
                      const std::set<std::string>& p_Exclues = {});

// Entrée, arme dissimulée et équipement, cohérents avec les méthodes tirées
// (une seringue létale pour « poison injecté »…).
SPreparation TirerPreparation(const SPlanification& p_Planif, const std::vector<SObjetInventaire>& p_Inventaire,
                              const STirage& p_Tirage, EMode p_Mode, std::mt19937& p_Hasard,
                              const std::vector<std::string>& p_JetonsVoulus = {});

// La préparation d'un contrat reçu par code : entrée, arme et objets désignés
// par leur identifiant, pris dans les déblocages de CE joueur. Ce qu'il ne
// possède pas reste vide (p_Manquants reçoit leurs identifiants).
SPreparation PreparationDepuisIds(const SPlanification& p_Planif, const std::vector<SObjetInventaire>& p_Inventaire,
                                  const std::string& p_Entree, const std::string& p_Arme, const std::string& p_Objet,
                                  const std::string& p_Objet2, std::vector<std::string>& p_Manquants);

// Freestyle : les jetons de tenue (TOKEN_OUTFIT_…) des entrées de la mission qui
// font partir 47 dans l'une des tenues données, par leur nom interne du dépôt
// (« Fox_Worker_Bartender_M_… » ↔ TOKEN_OUTFIT_FOX_WORKER_BARTENDER).
std::vector<std::string> JetonsDesTenues(const SPlanification& p_Planif, const std::vector<std::string>& p_NomsInternes);

// ---------------------------------------------------------------- Écriture

// Écran de préparation : seule l'entrée tirée reste proposée, l'arme et
// l'équipement tirés sont présélectionnés. Rend vrai si la page a changé.
bool ImposerPlanification(nlohmann::json& p_Page, const SPreparation& p_Preparation);

// Liste d'un emplacement : réduite à l'objet tiré, s'il y figure. Rend vrai si
// la page a changé ; p_Absent dit si l'objet manquait (interdit dans ce lieu).
bool ImposerEmplacement(nlohmann::json& p_Page, const SPreparation& p_Preparation, bool& p_Absent);

// Le contrat : l'équipement tiré devient l'équipement obligatoire
// (« MandatoryLoadout », le champ du tutoriel de la Facility).
bool AjouterEquipementObligatoire(nlohmann::json& p_Contrat, const SPreparation& p_Preparation);

// Menu principal (« data.DestinationsData ») : l'onglet Destinations ne propose
// plus que le lieu tiré. (Essai en jeu du 22/11/2026 : la tuile « Jouer
// l'histoire » que la v0.7 réécrivait n'est affichée nulle part.)
bool ImposerDestination(nlohmann::json& p_Page, const std::string& p_IdLieu);

// Page d'un lieu : seule la mission tirée y reste (les autres sections, —
// escalades, arcade… — sont vidées). Rien ne change si elle n'y est pas.
bool ImposerMission(nlohmann::json& p_Page, const std::string& p_IdContrat);

// Page d'un lieu, QUEL QU'IL SOIT : elle devient celle du lieu tiré, avec sa
// seule mission tirée. Le menu principal n'est relu qu'au lancement du jeu et
// après une mission : un contrat accepté entre-temps n'y paraît pas, mais
// chaque page de lieu est redemandée à l'ouverture. Ouvrir n'importe quelle
// destination mène donc à la mission tirée (Julien : « quelques clics, pas à
// nous de sélectionner la destination »).
bool ImposerPageLieu(nlohmann::json& p_Page, const SDestination& p_Lieu, const SMissionConnue& p_Mission);

// Page « Lieu de départ » (« data.OrderedUnlocks » + « data.Unlocked ») : le
// jeu dessine TOUTES les entrées du lieu d'après ses propres données et ne lit
// dans la page que celles qui sont débloquées (« Unlocked », essai en jeu du
// 22/11/2026). Les autres entrées débloquées (p_Entrees) en sont retirées :
// elles apparaissent verrouillées, seule la tirée se choisit (avec l'entrée par
// défaut, que le jeu débloque toujours). Le modèle envoyé avec la page n'est
// pas suivi par le jeu : le pilote choisit l'entrée au clavier (Roulette.cpp).
bool ImposerListeEntrees(nlohmann::json& p_Page, const SPreparation& p_Preparation, const std::vector<SEntree>& p_Entrees);

// L'identifiant du contrat d'une page de liste d'emplacement ("" sinon).
std::string ContratDeLaPage(const nlohmann::json& p_Page);
