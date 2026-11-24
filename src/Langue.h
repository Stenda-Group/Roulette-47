#pragma once

// La langue des textes du mod : la fiche, les objectifs écrits dans le contrat,
// les raisons d'un échec. Cinq langues (Julien, 24/11/2026) : français, anglais,
// italien, espagnol, russe ; toute autre donne l'anglais.
//
// Choix, du plus sûr au moins sûr :
//   1. « langue » dans roulette47.ini (fr, en, it, es, ru) : imposée ;
//   2. la langue du jeu, lue dans ses propres textes dès qu'ils sont chargés
//      (« États-Unis », « United States », « Stati Uniti », « Estados Unidos »,
//      « США ») ;
//   3. la langue réglée dans Steam pour le jeu (appmanifest_1659040.acf) ;
//   4. celle de Windows.
//
// Les noms venus du jeu (cibles, objets, lieux, missions) sont déjà dans la
// langue du jeu : seuls les textes écrits par le mod passent par ici.

#include <string>

enum class ELangue { Fr = 0, En, It, Es, Ru };

ELangue Langue();
void DefinirLangue(ELangue p_Langue);
const char* CodeLangue(ELangue p_Langue);         // « fr », « en »…
bool LangueDepuisCode(const std::string& p_Code, ELangue& p_Langue);

ELangue LangueDeWindows();
bool LangueDeSteam(const std::string& p_DossierDuMod, ELangue& p_Langue);
// Le nom des États-Unis tel que le jeu l'écrit (UI_LOCATION_COLORADO_COUNTRY).
bool LangueDepuisTexteDuJeu(const std::string& p_EtatsUnis, ELangue& p_Langue);

// Un texte du mod dans la langue courante (repli : le français, puis la clé).
std::string Tx(const std::string& p_Cle);
// Le même, « %s » remplacé par p_Valeur.
std::string TxF(const std::string& p_Cle, const std::string& p_Valeur);
