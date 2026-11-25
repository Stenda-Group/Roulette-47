#pragma once

// Les images du jeu (portraits des cibles, tenues, objets).
//
// Elles ne sont PAS livrées avec le mod : comme les menus du jeu, on les
// télécharge depuis le service de ressources d'IOI, à l'adresse et avec la
// signature que le jeu tient lui-même (ZOnlineManager), puis on les garde dans
// Retail/Roulette/cache/ sur le PC du joueur, d'où l'interface les affiche
// (https://cache.roulette.mod/…). Hors ligne, seul le cache sert ; une image
// absente laisse sa case sur un fond uni.

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

class Images {
public:
    Images();
    ~Images();

    // Thread du jeu : relève l'adresse du service et sa signature, qui changent
    // au fil de la session (le jeu renouvelle la signature).
    void RelireService();

    // N'importe quel thread : met l'image en file si elle n'est pas encore dans
    // le cache. Le téléchargement se fait en arrière-plan.
    void Demander(const std::string& p_Chemin, const std::string& p_Repli = {});

    // Appelé (depuis le thread des téléchargements) quand une image arrive.
    void QuandArrivee(std::function<void()> p_Rappel);

    [[nodiscard]] std::string DossierCache() const { return m_DossierCache; }

    // Vrai si le service d'IOI n'a pas cette image (ni sa vignette de repli) :
    // retenu de session en session (cache/introuvables.txt). Une cible sans
    // photo n'est plus tirée (Julien, 25/11/2026).
    [[nodiscard]] bool Introuvable(const std::string& p_Chemin);

private:
    enum class EEtat { Attente, Fichier, Echec };

    void Boucle();
    bool Telecharger(const std::string& p_Chemin, const std::string& p_Fichier);
    std::string FichierLocal(const std::string& p_Chemin) const;

    std::string m_DossierCache;

    std::mutex m_Verrou;
    std::condition_variable m_Signal;
    std::deque<std::string> m_File;
    std::unordered_map<std::string, EEtat> m_Images;
    std::function<void()> m_QuandArrivee;
    std::unordered_map<std::string, std::string> m_Replis; // image → autre chemin à essayer
    std::unordered_map<std::string, bool> m_Introuvables;  // chemins absents du service
    std::string m_UrlService;  // ex. https://…/resources-x-y
    std::string m_Signature;   // jeton SAS, jamais écrit dans le journal
    bool m_Arret = false;
    std::thread m_Thread;
};
