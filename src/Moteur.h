#pragma once

// Accès au moteur du jeu, sans aucune bibliothèque de mod.
//
// Le mod se charge seul (version.dll, voir src/chargeur) et retrouve lui-même ce
// dont il a besoin dans HITMAN3.exe, par des suites d'octets du jeu ; ce qu'il
// sait de la mémoire du moteur est décrit dans Glacier.h. Il touche le moins
// possible au jeu :
//   - les gestionnaires (cibles, tenues, ressources…) sont des adresses lues ;
//   - les fonctions du jeu (traduction, abonnement à chaque image) sont appelées ;
//   - UN seul détour, au début de la fonction qui envoie les événements au
//     serveur (ZAchievementManagerSimple::OnEventSent), posé à la main dans le
//     remplissage du code du jeu (voir PoserDetour dans Moteur.cpp).
// Si un autre mod a déjà détourné cette fonction, elle commence par son saut :
// on le redirige vers nous, et on rappelle sa cible.
//
// De même, quand un autre mod a remplacé le début d'une fonction par un saut
// (0xE9), son motif est recherché une seconde fois avec ses cinq premiers
// octets ignorés (ChercherFonction()).

#include <functional>
#include <string>

#include <Windows.h>

namespace Glacier { struct Objet; }

namespace Moteur {
    // Relit toutes les adresses. Rend faux et journalise ce qui manque si le
    // jeu a été mis à jour et qu'un motif ne correspond plus.
    bool Resoudre();

    // Branche la capture des événements (thread quelconque, une seule fois).
    // Le rappel tourne dans le thread du jeu, à chaque événement envoyé au serveur.
    bool CapterEvenements(void (*p_Rappel)(const Glacier::Objet& p_Evenement));

    // Relève les données de chaque page de menu (écran de préparation…), dans
    // le thread du jeu. Lecture seule : la page s'affiche telle quelle.
    bool CapterPages(void (*p_Rappel)(const Glacier::Objet& p_Donnees));

    // Reçoit le corps de chaque réponse du serveur d'IOI, tel que reçu (peut-être
    // compressé), juste avant que le jeu ne le lise. Si le rappel rend vrai, le
    // jeu lit p_Nouveau à la place (même forme : gzip si l'original l'était).
    bool CapterReponses(bool (*p_Rappel)(const uint8_t* p_Octets, size_t p_Taille, std::string& p_Nouveau));

    // Lance un contrat comme le fait le jeu quand une tuile démarre directement
    // une mission (action « start-contract ») : même fonction, appelée sans
    // passer par une page. THREAD DU JEU SEULEMENT (rappel de chaque image).
    // NON UTILISÉ (essai du 24/11/2026) : la mission se charge, mais sans la
    // préparation de l'équipement que fait la page de préparation, le jeu
    // n'envoie pas « ContractStart » et le serveur d'IOI répond 500 au premier
    // envoi d'événements (« Déconnecté »). Gardé pour une reprise éventuelle.
    bool PeutDemarrerContrat();

    // Ouvrir une page de menu comme le fait le jeu (action « link » d'une tuile),
    // par exemple la préparation d'une mission : page « planning », arguments
    // {"url":"planning","args":{"contractid":…}}. N'importe quel thread : la
    // page s'ouvre à l'image suivante, dans le thread du jeu.
    bool PeutOuvrirPage();
    void DemanderPage(const std::string& p_Page, const std::string& p_JsonArguments);
    // Le navigateur des menus est retenu au premier lien que le jeu exécute
    // (l'arrivée à l'accueil) ; il est oublié au départ en mission.
    bool NavigateurConnu();
    void OublierNavigateur();
    // Le rappel de chaque image, posé dès le premier lien (thread du jeu).
    void PreparerImage(void (*p_Rappel)());

    // Le lieu de départ retenu par le jeu, comme le choisit la page « Lieu de
    // départ » (action « enable-entrance ») : identifiant, « equip » (tableau
    // JSON des tenues) et l'objet de l'entrée (JSON). N'importe quel thread :
    // écrit à chaque image tant qu'il diffère, jusqu'à LibererEntree().
    void ImposerEntree(const std::string& p_IdEntree, const std::string& p_JsonEquip, const std::string& p_JsonEntree);
    void LibererEntree();
    bool PageEnAttente();
    void AnnulerPage();
    bool DemarrerContrat(const std::string& p_IdContrat);

    // Abonne un rappel à chaque image du jeu. À appeler DEPUIS le thread du jeu
    // (dans le rappel d'un événement, par exemple).
    bool AbonnerImage(void (*p_Rappel)());

    // Traduit une clé de texte du jeu dans la langue du joueur (thread du jeu).
    // "" si la clé est inconnue.
    std::string Traduire(const std::string& p_Cle);

    // La fenêtre principale du jeu (nullptr tant qu'elle n'existe pas).
    HWND FenetreDuJeu();

}
