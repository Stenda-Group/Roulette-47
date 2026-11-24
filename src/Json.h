#pragma once

// Conversion d'un objet du jeu (valeur d'un événement, fiche du dépôt) en
// JSON. On parcourt l'objet nous-mêmes, type par type (noms des types du
// moteur, voir Glacier.h). Un type inconnu devient null plutôt que de faire
// échouer toute la lecture.

#include <string>

#include <nlohmann/json.hpp>

#include "Glacier.h"

using Glacier::VersTexte;

inline nlohmann::json VersJson(const Glacier::Objet& p_Objet) {
    if (const auto s_Objet = p_Objet.Comme<Glacier::ObjetJson>("TArray<SDynamicObjectKeyValuePair>")) {
        auto s_Json = nlohmann::json::object();

        for (const auto& s_Paire : *s_Objet) {
            s_Json[VersTexte(s_Paire.m_Cle)] = VersJson(s_Paire.m_Valeur);
        }

        return s_Json;
    }

    if (const auto s_Tableau = p_Objet.Comme<Glacier::TableauJson>("TArray<ZDynamicObject>")) {
        auto s_Json = nlohmann::json::array();

        for (const auto& s_Element : *s_Tableau) {
            s_Json.push_back(VersJson(s_Element));
        }

        return s_Json;
    }

    if (const auto v = p_Objet.Comme<Glacier::Chaine>("ZString")) return VersTexte(*v);
    if (const auto v = p_Objet.Comme<bool>("bool")) return *v;
    if (const auto v = p_Objet.Comme<double>("float64")) return *v;
    if (const auto v = p_Objet.Comme<float>("float32")) return *v;
    if (const auto v = p_Objet.Comme<int32_t>("int32")) return *v;
    if (const auto v = p_Objet.Comme<uint32_t>("uint32")) return *v;
    if (const auto v = p_Objet.Comme<int64_t>("int64")) return *v;
    if (const auto v = p_Objet.Comme<uint64_t>("uint64")) return *v;

    return nullptr;
}

// Lecture tolérante d'un champ texte : "" si absent ou d'un autre type.
inline std::string ChampTexte(const nlohmann::json& p_Json, const char* p_Cle) {
    const auto s_It = p_Json.find(p_Cle);

    if (s_It == p_Json.end() || !s_It->is_string()) {
        return {};
    }

    return s_It->get<std::string>();
}

inline bool ChampBooleen(const nlohmann::json& p_Json, const char* p_Cle) {
    const auto s_It = p_Json.find(p_Cle);
    return s_It != p_Json.end() && s_It->is_boolean() && s_It->get<bool>();
}
