#pragma once

// Les DLC que le joueur possède : la roulette ne tire que les missions qu'il
// peut lancer.
//
// Le serveur d'IOI dit « IsLocked: false » pour une mission d'un pack qu'on ne
// possède pas (« Host Assignment ft. Jacksepticeye », The Host Pack) : c'est le
// jeu qui refuse ensuite de la lancer (« Error starting contract »). Le droit
// qu'elle exige est écrit dans les menus (« SubLocation.Properties.Entitlements »,
// H3_ET_SHAMROCK) ; le jeu le traduit en numéro de DLC Steam par une table de
// HITMAN3.exe (relevée par tools/droits_steam.py), et c'est à Steam qu'on
// demande si ce DLC est là.

#include <functional>
#include <string>

#include <nlohmann/json.hpp>

// Le joueur a-t-il ce droit ? Vrai quand on ne sait pas : droit absent de la
// table (lieux, éditions anciennes : le serveur les verrouille lui-même), Steam
// absent ou pas encore prêt. Mieux vaut tirer une mission de trop que retirer
// au joueur une mission qu'il possède.
bool DroitPossede(const std::string& p_Droit);

// Une mission telle que les menus la décrivent (« brut ») : chaque liste de
// droits de sa scène et de son contrat doit en contenir un que le joueur a
// (une liste en nomme parfois plusieurs, au choix : « Holiday Hoarders » se
// joue avec n'importe quel lieu de HITMAN 3).
bool MissionPossedee(const nlohmann::json& p_Brut, const std::function<bool(const std::string&)>& p_Possede);
