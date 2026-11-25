#pragma once

// Discord Rich Presence (Julien, 25/11/2026) : le profil Discord du joueur
// montre la roulette en cours — mode, lieu, mission, cibles restantes.
//
// Aucune bibliothèque de Discord : le client Discord du PC écoute sur un tube
// nommé (\\.\pipe\discord-ipc-0 à 9) ; chaque message est un en-tête de 8
// octets (code, longueur) suivi de JSON. Poignée de main (code 0) avec
// l'identifiant de l'application Discord, puis SET_ACTIVITY (code 1).
// L'identifiant de l'application est un réglage (roulette47.ini, « discord_app ») :
// l'application se crée sur le portail des développeurs de Discord, avec
// l'image « logo » dans ses ressources de Rich Presence.
//
// Tout se passe dans un fil à part : Discord absent, fermé ou relancé, le mod
// réessaie toutes les 15 s sans jamais gêner le jeu.

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

struct SPresence {
    std::string m_Details; // première ligne (« Contrat proposé — Assassin »)
    std::string m_Etat;    // seconde ligne (« Paris · Le Showstopper »)
    long long m_Debut = 0; // secondes Unix du début (chronomètre), 0 sans
    bool operator==(const SPresence&) const = default;
};

class Discord {
public:
    ~Discord();

    // Rien ne se passe sans identifiant d'application.
    void Demarrer(const std::string& p_IdApplication);
    void Arreter();

    // La présence voulue ; envoyée à Discord quand elle change (ou après une
    // reconnexion). Vide (détails et état vides) : présence effacée.
    void Montrer(const SPresence& p_Presence);

private:
    void Boucle();

    std::string m_IdApplication;
    std::thread m_Fil;
    std::atomic<bool> m_Arret = false;
    std::mutex m_Verrou;
    SPresence m_Voulue;
    bool m_Nouvelle = false;
};
