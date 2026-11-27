#pragma once

// Mise à jour de Roulette 47 depuis ses releases GitHub (dépôt public
// Stenda-Group/Roulette-47).
//
// - Au lancement, la dernière release est lue (une seule requête à
//   api.github.com, rien d'autre n'est envoyé) ; plus récente que celle-ci, la
//   fiche l'annonce. « check_updates=0 » du .ini coupe la vérification.
// - « Installer » télécharge l'archive de la release, vérifie son SHA-256
//   (écrit dans les notes par tools/publish.py) et n'en garde que version.dll
//   et Roulette47/… :
//     * les deux DLL, chargées par le jeu, sont RENOMMÉES en « .ancien » (Windows
//       le permet pour une DLL en usage) et les nouvelles posées à leur place ;
//     * le reste (ui/, initial_locations/, licences) attend dans « update/ » :
//       l'interface ouverte ne doit pas changer sous la DLL qui la sert.
//   Au lancement suivant, la nouvelle DLL range « update/ » et efface les
//   « .ancien ». Rien ne change pour la partie en cours.

#include <functional>
#include <string>

#include <nlohmann/json.hpp>

namespace MiseAJour {
    // La version de cette DLL (« 0.24 »), lue dans VERSION à la compilation.
    const char* Version();

    // Essai seulement (tools/essai_maj.cpp) : se faire passer pour une autre version.
    void ForcerVersion(const std::string& p_Version);

    // Au tout début du démarrage, avant l'interface : range une mise à jour
    // installée lors de la partie précédente.
    void AppliquerEnAttente();

    // Lit la dernière release en arrière-plan ; p_Changement est appelé quand
    // l'état change (pour redessiner la fiche).
    void Verifier(std::function<void()> p_Changement);

    // Télécharge et installe la version annoncée, en arrière-plan.
    void Installer();

    // Ouvre la page de la release dans le navigateur.
    void OuvrirPage();

    // { etat: aucune | disponible | telechargement | installee | erreur,
    //   version, actuelle, erreur }
    nlohmann::json Etat();

    // « v0.24 » plus récente que « 0.23 » ? (exposée pour l'essai)
    bool PlusRecente(const std::string& p_Distante, const std::string& p_Locale);

    // Le SHA-256 que tools/publish.py écrit dans les notes : « `archive.zip` — SHA-256 `…` ».
    std::string SommeDesNotes(const std::string& p_Notes, const std::string& p_Archive);

    // Un chemin de l'archive (« Retail/Roulette47/ui/x.js ») qu'on accepte de
    // poser, relatif à Retail ; "" s'il sort de version.dll et de Roulette47/.
    std::string CheminAccepte(const std::string& p_Nom);
}
