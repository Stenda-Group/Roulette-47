#pragma once

// Roulette : des défis tirés au sort dans la mission en cours (méthode et
// tenue imposées par cible, complications), suivis en direct.
//
// Mod autonome : chargé par version.dll (src/chargeur), il n'a besoin d'aucun
// autre mod ni d'aucune bibliothèque de mod. Voir Moteur.h.
//
// Trois threads :
//   - le thread du jeu : événements, lecture du niveau, tirage ;
//   - le thread de l'interface (Fenetre) : la page, la touche F9 ;
//   - le thread des images : téléchargements.
// m_Verrou protège tout ce qu'ils partagent.

#include <atomic>
#include <fstream>
#include <mutex>
#include <random>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "Depot.h"
#include "Discord.h"
#include "Fenetre.h"
#include "Images.h"
#include "Menus.h"
#include "Tirage.h"

namespace Glacier { struct Objet; struct Acteur; }

class Roulette {
public:
    static Roulette& Instance();

    // Thread d'initialisation (lancé par DllMain).
    void Demarrer();

private:
    Roulette();

    // Thread du jeu.
    static void SurEvenement(const Glacier::Objet& p_Evenement);
    static void SurImage();
    static bool SurReponse(const uint8_t* p_Octets, size_t p_Taille, std::string& p_Nouveau);
    bool ReleverReponse(const uint8_t* p_Octets, size_t p_Taille, std::string& p_Nouveau);
    bool InjecterContrat(nlohmann::json& p_Reponse); // tirage + objectifs ajoutés au contrat
    bool TraiterMenus(nlohmann::json& p_Reponse);    // destinations, inventaire, préparation (vrai = réécrite)
    bool PreparerAvantMission(nlohmann::json& p_Page); // écran de préparation : tirage + page réécrite
    void TirerAvantMission();                        // sous verrou, sur les données déjà lues
    bool TirerContrat();                             // sous verrou : destination, mission, cibles, préparation
    void Piloter();                                  // thread à part : mène les menus jusqu'à la préparation
    void OublierContrat();                           // sous verrou : contrat refusé ou abandonné
    bool ChargerMemoireLieu(const std::string& p_Lieu, STirage& p_Tirage, const std::set<std::string>& p_Officielles);
    void ChargerDestinations();
    bool TirerDestinationMaintenant(); // sous verrou
    void RetrouverDestination();       // index de la destination tirée, après une relecture
    void EnregistrerDestinations();                  // sous verrou
    void TraiterEvenement(const Glacier::Objet& p_Evenement);
    void ChaqueImage();
    bool PreparerNiveau(); // relit cibles, tenues et mission dans m_Tirage
    void ChoisirImagesObjets(); // une image du jeu pour chaque méthode et pour le costume
    void ComposerCibles();      // cibles du contrat + PNJ tirés au sort
    void Marquer(bool p_Marquer); // halo rouge et icône de carte sur les PNJ cibles
    void RetenirPnjDuLieu();    // garde PNJ et tenues du lieu (tirage d'avant-mission)

    // Thread de l'interface.
    std::string EtatJson(bool p_Ouverte);
    void MessagePage(const std::string& p_Json);

    void ChargerReglages();
    void ReprendreAncienDossier(); // Retail\Roulette (v0.12 et avant) → Retail\Roulette47
    void DetecterLangueDuJeu(); // dès que les textes du jeu sont chargés
    void EnregistrerReglages();

    // Les noms des objets dans la langue du jeu (voir ApprendreNoms).
    std::string TraduireNom(const std::string& p_Cle, const std::string& p_Cle2);
    void ApprendreNoms();   // sous verrou : traduit tout ce qui peut l'être, retient, renomme
    void ChargerNoms();
    void EnregistrerNoms();

    // Code de contrat à partager (CodeContrat.h). Sous verrou.
    std::string CodeDuContrat() const;          // "" sans contrat proposé ou accepté
    bool ImporterContrat(const std::string& p_Code); // le contrat du code devient le contrat proposé
    std::string m_ErreurCode;                   // pourquoi le dernier code a été refusé ("" sinon)
    std::string m_AvertissementCode;            // ce qui manquait au joueur (entrée, objet non débloqués)
    void PreparerPlanification();               // m_Planif et m_CiblesPlanif de la destination tirée
    void CompleterImposes(SPreparation& p_Preparation); // images et noms lisibles de l'entrée et de l'équipement

    // Puriste « sans filet » : contrats réussis d'affilée (roulette47.ini,
    // « serie_puriste »). Un échec ou un abandon la remet à zéro.
    int m_SeriePuriste = 0;

    // Discord Rich Presence (Discord.h) : réglage « discord » (défaut : oui) et
    // identifiant de l'application Discord, « discord_app » (texte).
    Discord m_Discord;
    bool m_DiscordActif = true;
    std::string m_IdDiscord;
    std::string m_PhasePresence;     // ce qui fait repartir le chronomètre
    long long m_DebutPresence = 0;
    double m_DernierePresence = 0.0;
    SPresence PresenceActuelle();    // sous verrou
    // Les destinations décochées dans les réglages (roulette47.ini,
    // « destinations_exclues », identifiants séparés par des virgules).
    std::set<std::string> m_DestinationsExclues;
    // Clôt le tirage en cours (fin de contrat ou départ du niveau) et compte la série.
    void TerminerTirage();

    // Entrées où 47 est repéré dès le départ (entrees_reperees.json) : la
    // complication « Ne pas être repéré » n'y est plus jamais tirée.
    std::set<std::string> m_EntreesReperees;
    void ChargerEntreesReperees();
    void RetenirEntreeReperee(const std::string& p_IdEntree);
    void Journaliser(const std::string& p_Nom, const std::string& p_Json);

private:
    std::mutex m_Verrou;
    STirage m_Tirage;
    SReglages m_Reglages;
    int m_NumeroTirage = 0;          // change à chaque tirage : l'interface anime
    bool m_EnMission = false;
    bool m_DemandeTirage = false;    // demandé par l'interface, fait par le thread du jeu
    double m_DernierEssaiNiveau = -1000.0; // PreparerNiveau : un essai toutes les 5 s au plus
    std::atomic<bool> m_ContratLance = false; // ContractStart reçu, niveau pas encore quitté
    bool m_DemandeReleve = false;    // mission en cours : relever ses PNJ (même sans roulette)
    std::string m_Lieu;              // « Paris, France »
    std::string m_TitreMission;      // titre du contrat, dans la langue du jeu
    std::vector<std::string> m_ImagesMethodes; // par index de Methodes() ; "" = icône
    std::string m_IdLieu;            // LOCATION_PARIS

    // PNJ du niveau qui peuvent devenir cibles (thread du jeu ; pointeurs valables
    // tant que le niveau est chargé, oubliés — jamais relus — à sa sortie).
    struct SPnj {
        std::string m_Nom;
        std::string m_Image;      // portrait du dépôt s'il existe, sinon image de la tenue
        bool m_Portrait = false;  // vrai portrait (fiche du dépôt à son nom)
        std::string m_IdDepot;    // identifiant du dépôt (celui des événements Kill), "" sans portrait
        Glacier::Acteur* m_Acteur = nullptr;
    };
    std::vector<SPnj> m_Pnj;
    std::vector<SObjectifCible> m_CiblesContrat;
    std::vector<Glacier::Acteur*> m_Marques;  // PNJ marqués comme cibles en ce moment

    // Avant la mission (pages des menus, voir Menus.h).
    std::vector<SDestination> m_Destinations;    // gardées dans destinations.json
    int m_DestLieu = -1;                         // destination tirée (index), -1 = aucune
    int m_DestMission = -1;                      // sa mission (index), -1 = au choix
    int m_NumeroDestination = 0;                 // change à chaque tirage de destination
    std::string m_DestIdLieu;                    // la destination tirée, par identifiant (les index changent)
    std::string m_DestContrat;                   // le contrat de sa mission, "" = au choix
    std::string m_TuileContrat;                  // le contrat posé sur la tuile du menu principal
    std::string m_DernierContrat;                // le dernier contrat lancé
    std::vector<SObjetInventaire> m_Inventaire;  // armes dissimulées et équipement possédés
    SPlanification m_Planif;                     // le dernier écran de préparation lu
    std::vector<SObjectifCible> m_CiblesPlanif;  // ses cibles, nommées
    std::vector<SEntree> m_EntreesPossedees;     // entrées débloquées (inventaire)
    STirage m_TiragePrepare;                     // le contrat tiré par F9 (tout d'un coup)
    std::string m_IdContratPrepare;              // sa mission
    bool m_Propose = false;                      // tiré, ni accepté ni refusé
    std::atomic<bool> m_PiloteEnCours = false;   // le mod mène les menus (touches simulées)
    std::atomic<int> m_PagesLieu = 0;            // pages reçues, comptées pour le pilote
    std::atomic<int> m_PagesPrep = 0;
    std::atomic<int> m_PagesEntrees = 0;
    std::string m_EtatPilote;                    // ce que le pilote dit à la fiche
    bool m_Prepare = false;                      // accepté : il attend sa mission
    // Écran de chargement de la fiche après « Accepter » (Julien, 25/11/2026 :
    // la préparation ne s'ouvre pas tout de suite) : le compte des pages de
    // préparation à l'acceptation ; -1 = aucun lancement en cours.
    std::atomic<int> m_PagesPrepAuLancement = -1;
    // Les costumes de 47 possédés (emplacement « disguise » de l'inventaire) :
    // le costume classique y est pris pour la préparation.
    std::vector<SObjetInventaire> m_Costumes;
    void AppliquerCostume(SPreparation& p_Preparation); // sous verrou : le costume retenu, si l'entrée le permet
    [[nodiscard]] bool EntreeEnCostume(const SPreparation& p_Preparation) const; // sous verrou

    bool m_TirageInjecte = false;    // tirage écrit dans le contrat du jeu : figé jusqu'à la fin
    bool m_Rompu = false;            // contrat rompu : la mission est à recommencer
    std::string m_RaisonRupture;
    std::string m_ImageCostume;      // le costume de 47 porté au départ
    bool m_Encart = true;
    int m_Touche = VK_F9;
    bool m_OutilsDeveloppement = false;

    std::atomic<int> m_ImagesArrivees = 0;
    double m_DernierService = -100.0;
    std::atomic<bool> m_AbonneImage = false;

    Depot m_Depot;
    Images* m_Images = nullptr;
    Fenetre m_Fenetre;
    std::mt19937 m_Hasard;

    // Les noms appris : clé de texte (en capitales) → nom dans la langue du jeu.
    // Aux menus, le jeu n'a chargé qu'une partie de ses textes (78 objets sur
    // 265 le 24/11/2026) ; la préparation et la mission ont les autres. Gardés
    // dans noms.json pour les tirages suivants.
    std::mutex m_VerrouNoms;
    std::unordered_map<std::string, std::string> m_NomsAppris;
    bool m_NomsModifies = false;

    std::atomic<bool> m_LangueImposee = false; // « langue= » dans roulette.ini
    std::atomic<bool> m_LangueDuJeuLue = false;

    std::mutex m_VerrouJournal;
    std::ofstream m_Journal;
};
