#pragma once

// Les règles de la roulette écrites comme OBJECTIFS du contrat que le serveur
// envoie au jeu : c'est ce qui donne l'échec « vanilla » (Julien, 22/11/2026 :
// « fais échouer la mission comme en vanilla quand on ne respecte pas un
// objectif obligatoire »).
//
// Un objectif de contrat est une machine à états (voir le contrat de Sapienza
// relevé dans Retail/Roulette47/responses/ : « Silvio s'échappe » passe en
// Failure au bout de 90 s). Un objectif primaire en Failure fait échouer la
// mission, avec l'écran du jeu.
//
//   - cible : « Kill » de la cible avec la bonne méthode ET la bonne tenue →
//     Success ; « Kill » de la cible autrement → Failure. Un PNJ tiré au sort
//     est déclaré cible (Context.Targets, ObjectiveType « kill ») : le jeu le
//     marque lui-même et l'affiche dans ses objectifs.
//   - complication : réussie d'emblée (« - » → Success), puis Failure au
//     premier écart (Spotted, BodyFound, Kill d'un non-ciblé…).
//
// Le serveur garde SA copie du contrat et ne connaît pas ces objectifs.

#include <random>

#include <nlohmann/json.hpp>

#include "Tirage.h"

// Les objectifs à ajouter à Contract.Data.Objectives pour ce tirage.
// p_ToutesLesCibles : chaque cible tirée a son objectif, même sans consigne (les
// objectifs du jeu sont masqués). p_IdsCibles reçoit les Id des objectifs de cible.
nlohmann::json ObjectifsRoulette(const STirage& p_Tirage, std::mt19937& p_Hasard, bool p_ToutesLesCibles = false,
                                 std::vector<std::string>* p_IdsCibles = nullptr);

// Les objectifs du jeu (Contract.Data, AVANT d'y ajouter ceux de la roulette)
// deviennent invisibles et facultatifs : « primary » → « secondary », IsHidden,
// jamais affichés. Ils tournent toujours (les scripts du niveau s'en servent),
// mais seule la roulette décide de la réussite. La sortie (« EnableExits »)
// s'ouvre quand les cibles de la roulette sont éliminées. Rend le nombre
// d'objectifs masqués.
int MasquerObjectifsDuJeu(nlohmann::json& p_Donnees, const std::vector<std::string>& p_IdsCibles);

// Les objectifs au format du BRIEFING (« data.Objectives » de la page de
// préparation, que montrent l'écran « Objectifs » et l'écran de chargement) :
// une tuile « kill » par cible, avec méthode et tenue (« Conditions »), et une
// tuile par complication.
nlohmann::json BriefingRoulette(const STirage& p_Tirage);

// Les réglages du contrat lui-même (Contract.Data) : difficulté « Professionnel »
// seule (GameDifficulties réduit à « normal », le sélecteur de la préparation
// n'a plus que celle-là), et, en Puriste, aucune sauvegarde (EnableSaving).
void ReglerContrat(nlohmann::json& p_Donnees, EMode p_Mode);

// L'image du jeu d'une complication (celle des conditions de contrat), "" sinon.
const char* ImageComplication(const std::string& p_Cle);
