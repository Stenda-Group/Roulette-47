#pragma once

// Le code d'un contrat, à partager (Julien, 25/11/2026) : tout le contrat tiré
// tient dans le code lui-même — aucune base, rien n'est stocké nulle part.
// JSON compact, compressé (zlib, miniz), écrit en base64 « URL » derrière le
// préfixe « R47- ». Un code abîmé ou d'une autre version est refusé.

#include <string>

#include <nlohmann/json.hpp>

// Le contrat (JSON décrit dans Roulette::CodeDuContrat) en code « R47-… ».
std::string EncoderContrat(const nlohmann::json& p_Contrat);

// Le JSON du contrat, ou nullptr si le code est illisible. Les espaces, retours
// à la ligne et le préfixe sont tolérés (copier-coller).
nlohmann::json DecoderContrat(const std::string& p_Code);
