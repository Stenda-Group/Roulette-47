#pragma once

// Les réponses du serveur d'IOI, telles que le jeu les reçoit.
//
// C'est là que passent les pages des menus (destinations, préparation d'une
// mission : entrées, équipement possédé…) et le contrat de chaque mission. Le
// relevé de ZDynamicPageController::Expand n'a rien reçu (essai du 22/11/2026) :
// ces écrans ne passent pas par lui.

#include <cstdint>
#include <string>

// Le corps en texte : décompressé s'il arrive en gzip, tel quel sinon. Vide si
// la décompression échoue.
std::string CorpsEnTexte(const uint8_t* p_Octets, size_t p_Taille);

// Recompresse en gzip (pour rendre au jeu une réponse modifiée sous la même
// forme que celle reçue).
std::string EnGzip(const std::string& p_Texte);

// Vrai si le corps reçu est compressé en gzip.
bool EstGzip(const uint8_t* p_Octets, size_t p_Taille);
