#pragma once

// Journal du mod : Retail/Roulette/diagnostic.log, remis à zéro à chaque
// lancement du jeu. C'est lui qu'il faut lire quand quelque chose ne marche pas
// (motifs introuvables après une mise à jour du jeu, WebView2 absent…).

#include <format>
#include <string>

std::string DossierDuMod(); // …/Retail/Roulette (le dossier de Roulette.dll)

void Diagnostiquer(const std::string& p_Ligne);

template <class... Args>
void Diag(std::format_string<Args...> p_Format, Args&&... p_Args) {
    Diagnostiquer(std::format(p_Format, std::forward<Args>(p_Args)...));
}
