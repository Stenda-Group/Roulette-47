#pragma once

// Le tirage : pour chaque cible de la mission en cours, une méthode
// d'élimination et une tenue imposées, plus des complications pour toute la
// mission. Tout ce qui est tiré existe dans le niveau chargé (cibles et
// tenues sont lues dans le jeu) ; seule la liste des méthodes est à nous.
//
// Le suivi (réussi / échoué) se fait sur les événements que le jeu envoie
// lui-même au serveur (« Kill », « Disguise », « Spotted »…), captés par le
// mod avant leur départ (Moteur.cpp).

#include <random>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

enum class EEtatObjectif { EnCours, Reussi, Echoue };

// Les modes de la roulette, choisis avant le tirage (Julien, 24/11/2026) :
//  - Assassin (défaut) : costume de 47 et son équipement de toujours —
//    Silverballer silencieux, corde de piano, crochet — pistolet, corde,
//    accident ou poison trouvé sur place, complications réalistes ;
//  - Puriste : « Silent Assassin, costume seul » (SASO) — aucune arme, corde
//    de piano et pièces, accidents précis, poison ou corde, et les cinq
//    exigences du Silent Assassin en costume imposées ;
//  - Freestyle : tout le champ des possibles (les réglages de la fiche).
// Dans tous les cas, 3 cibles au plus.
enum class EMode { Assassin = 0, Puriste = 1, Freestyle = 2 };

const char* CleMode(EMode p_Mode);            // « assassin », « puriste », « freestyle »
EMode ModeDepuisCle(const std::string& p_Cle); // inconnu : Assassin
const char* LibelleMode(EMode p_Mode);         // « Assassin », « Puriste », « Freestyle »

// Une méthode d'élimination telle que le jeu la décrit dans l'événement Kill.
// Strict renseigné : la valeur stricte doit correspondre ; sinon la large.
struct SMethode {
    const char* m_Cle;      // identifiant stable (réglages)
    const char* m_Large;    // KillMethodBroad attendu ("" = indifférent)
    const char* m_Stricte;  // KillMethodStrict attendu ("" = indifférent)
    const char* m_Libelle;
    const char* m_Famille;  // regroupement des réglages
};

const std::vector<SMethode>& Methodes();

struct STenue {
    std::string m_Id;   // RepositoryId de la tenue (minuscules)
    std::string m_Nom;
    std::string m_Image;
    bool m_EstCostume = false; // costume de 47
};

struct SObjectifCible {
    std::string m_Id;     // RepositoryId de la cible (minuscules)
    std::string m_Nom;
    std::string m_Image;
    int m_Methode = -1;   // index dans Methodes(), -1 = au choix
    int m_Tenue = -1;     // index dans STirage::m_Tenues, -1 = au choix
    EEtatObjectif m_Etat = EEtatObjectif::EnCours;
    std::string m_Detail; // pourquoi c'est échoué (méthode, tenue)
    bool m_Officielle = true; // cible du contrat du jeu ; sinon PNJ tiré par la roulette
    std::string m_CleNom;     // nom réduit (CleNom) : c'est ainsi qu'on reconnaît un PNJ dans l'événement Kill
};

struct SComplication {
    const char* m_Cle;
    const char* m_Libelle;
    const char* m_Detail;
};

const std::vector<SComplication>& Complications();

struct SComplicationTiree {
    int m_Index = 0;
    EEtatObjectif m_Etat = EEtatObjectif::EnCours;
};

struct SReglages {
    EMode m_Mode = EMode::Assassin;     // choisi sur l'accueil de la fiche
    bool m_ImposerMethode = true;
    bool m_ImposerTenue = true;
    bool m_AutoriserCostume = true;   // le costume de 47 peut sortir comme tenue
    int m_NbComplications = 1;        // 0 à 3
    bool m_Familles[6] = { true, true, true, true, true, true }; // cf. FamillesMethodes()
    bool m_TirageAuto = true;         // tirer dès le début de la mission
    int m_NbPnj = 1;                  // (n'est plus utilisé : tous les PNJ connus sont candidats)
    int m_CiblesMin = 1;              // nombre de cibles de la roulette : de 1…
    int m_CiblesMax = 3;              // … à 3 (Julien, 22/11/2026 : « entre 1 et 3 cibles max »)
    bool m_RemplacerObjectifs = true; // les objectifs du jeu masqués : seule la roulette compte
    bool m_DestinationAuto = true;    // un contrat accepté : les menus mènent à sa mission
    bool m_Sons = true;               // effets sonores de la fiche
    bool m_Pilote = true;             // contrat accepté : le mod mène lui-même les menus jusqu'à la préparation
    bool m_EchecImpose = true;        // contrat rompu = mission à recommencer
    bool m_ImposerPreparation = true; // entrée, arme et objet tirés sur l'écran de préparation
};

const std::vector<const char*>& FamillesMethodes();

// Un élément de la préparation imposé avant la mission (entrée, arme, objet).
struct SImpose {
    std::string m_Id;     // RepositoryId (minuscules) ; "" = rien d'imposé
    std::string m_IdJeu;  // identifiant du jeu (STARTING_LOCATION_…, FIREARMS_…)
    std::string m_Emplacement; // concealedweapon, gear (objets)
    std::string m_Nom;
    std::string m_Cle;    // clé du nom dans les textes du jeu (UI_…_NAME)
    std::string m_Cle2;   // seconde clé (Name_LOC du dépôt), "" si aucune
    std::string m_Image;
    EEtatObjectif m_Etat = EEtatObjectif::EnCours; // vérifié au départ de la mission
    nlohmann::json m_Brut;  // l'objet tel que l'inventaire le décrit (pour réécrire les menus)

    [[nodiscard]] bool Vide() const { return m_Id.empty(); }
};

// La préparation tirée sur l'écran de préparation, avant la mission.
struct SPreparation {
    std::string m_IdContrat;  // la mission pour laquelle elle a été tirée
    SImpose m_Entree;
    SImpose m_Arme;           // arme dissimulée
    SImpose m_Objet;          // équipement (premier emplacement)
    SImpose m_Objet2;         // second équipement (modes Assassin et Puriste)

    [[nodiscard]] bool Vide() const { return m_Entree.Vide() && m_Arme.Vide() && m_Objet.Vide() && m_Objet2.Vide(); }
};

struct STirage {
    bool m_Valide = false;
    EMode m_Mode = EMode::Freestyle;     // le mode du tirage
    SPreparation m_Preparation;          // vide si la mission n'est pas passée par la préparation
    std::string m_Scene;                 // scène pour laquelle on a tiré
    std::vector<STenue> m_Tenues;        // tenues du niveau
    std::vector<SObjectifCible> m_Cibles;
    std::vector<SComplicationTiree> m_Complications;
    bool m_UneEliminationFaite = false;  // passé ce point, relancer serait tricher
    bool m_Terminee = false;

    [[nodiscard]] EEtatObjectif Bilan() const;
};

// Réduit les cibles candidates (celles du contrat et tous les PNJ connus du
// lieu, à égalité) au nombre réglé : entre m_CiblesMin et m_CiblesMax.
void ChoisirCibles(STirage& p_Tirage, const SReglages& p_Reglages, std::mt19937& p_Hasard);

// Tire de nouveau méthodes, tenues et complications (les cibles et tenues du
// niveau doivent déjà être dans p_Tirage).
void Tirer(STirage& p_Tirage, const SReglages& p_Reglages, std::mt19937& p_Hasard);

// Applique un événement du jeu au tirage. Rend vrai si un état a changé.
// p_Temps : l'horodatage de l'événement (secondes depuis le début de la mission).
bool AppliquerEvenement(STirage& p_Tirage, const std::string& p_Nom, const nlohmann::json& p_Valeur, double p_Temps = -1.0);

// Repéré au tout début de la mission (l'entrée tirée met 47 en vue) : ce n'est
// pas une faute du joueur. « Ne pas être repéré » est alors écartée, dans le
// contrat du jeu comme sur la fiche (Julien, 24/11/2026).
inline constexpr double c_GraceDepart = 5.0;

// Retire une complication du tirage ; vrai si elle y était.
bool EcarterComplication(STirage& p_Tirage, const char* p_Cle);

// Départ de la mission (valeur de « ContractStart ») : l'arme et l'objet imposés
// sont-ils dans l'équipement emporté ? Rend vrai si un état a changé.
bool VerifierPreparation(STirage& p_Tirage, const nlohmann::json& p_Depart);

// Fin de mission : ce qui est encore « en cours » se conclut.
void Conclure(STirage& p_Tirage);

// Vrai si le contrat est rompu pour de bon (une cible manquée, une complication
// irrattrapable) ; p_Raison dit laquelle.
bool Rompu(const STirage& p_Tirage, std::string& p_Raison);

// Un nom réduit à ses lettres et chiffres ASCII, en minuscules : le jeu écrit
// « Andr? Beaul? » dans ses événements pour « André Beaulé ».
std::string CleNom(const std::string& p_Nom);
