#pragma once

// La fenêtre de l'interface : une page HTML/CSS/JS (dossier ui/) affichée par
// WebView2 (le moteur d'Edge, présent sur Windows 10 et 11) dans une fenêtre
// transparente posée sur le jeu.
//
// - Fiche ouverte : la fenêtre couvre le jeu et prend souris et clavier.
// - Fiche fermée : elle se réduit à l'encart (en haut à droite), ne prend
//   jamais le focus et laisse passer les clics.
// - Le jeu n'est pas au premier plan : elle disparaît.
//
// Tout se passe dans un thread à elle (WebView2 veut un thread STA avec une
// boucle de messages) ; le reste du mod lui parle par Pousser() et Ouvrir().
//
// Transparence : hébergement « visuel » de WebView2 (CompositionController)
// sur une fenêtre WS_EX_NOREDIRECTIONBITMAP, seule façon d'avoir un fond
// réellement transparent. En contrepartie, la souris est relayée à la main.

#include <atomic>
#include <functional>
#include <string>
#include <thread>

#include <Windows.h>

class Fenetre {
public:
    struct SParametres {
        std::wstring m_DossierUi;       // pages servies en https://roulette.mod/
        std::wstring m_DossierCache;    // images servies en https://cache.roulette.mod/
        std::wstring m_DossierDonnees;  // profil WebView2
        int m_Touche = VK_F9;
        bool m_OutilsDeveloppement = false;

        // Rend la fenêtre du jeu (nullptr : couvrir l'écran principal, pour les essais).
        std::function<HWND()> m_FenetreDuJeu;
        // Rend l'état complet en JSON (appelé dans le thread de la fenêtre).
        std::function<std::string(bool p_Ouverte)> m_Etat;
        // Un message de la page (JSON), après que la fenêtre a traité les siens.
        std::function<void(const std::string& p_Json)> m_Message;
    };

    Fenetre() = default;
    ~Fenetre();

    void Demarrer(SParametres p_Parametres);

    // N'importe quel thread : l'état a changé, le renvoyer à la page.
    void Pousser();

    // N'importe quel thread.
    void Ouvrir(bool p_Ouvrir);
    [[nodiscard]] bool EstOuverte() const { return m_Ouverte; }

    // N'importe quel thread. Contrat rompu : la fiche s'ouvre et ne se ferme plus
    // (ni F9, ni Échap, ni bouton) ; elle laisse la partie gauche du jeu libre,
    // où le menu pause propose « Recommencer » et « Revenir au menu ». Le jeu se
    // met en pause, cette fois. Rompre(false) à la sortie de la mission.
    void Rompre(bool p_Rompu);

private:
    void Boucle();

    SParametres m_Parametres;
    std::thread m_Thread;
    std::atomic<HWND> m_Hwnd = nullptr;
    std::atomic<bool> m_Ouverte = false;
    std::atomic<bool> m_Rompu = false;
};
