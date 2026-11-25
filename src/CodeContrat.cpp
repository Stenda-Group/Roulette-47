#include "CodeContrat.h"

#include <cctype>
#include <vector>

#include <miniz.h>

namespace {
    constexpr char c_Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    constexpr const char* c_Prefixe = "R47-";

    std::string Base64(const std::vector<uint8_t>& p_Octets) {
        std::string s;
        size_t i = 0;
        for (; i + 2 < p_Octets.size(); i += 3) {
            const uint32_t v = (p_Octets[i] << 16) | (p_Octets[i + 1] << 8) | p_Octets[i + 2];
            for (int k = 18; k >= 0; k -= 6) s += c_Alphabet[(v >> k) & 63];
        }
        if (const size_t r = p_Octets.size() - i; r > 0) {
            uint32_t v = p_Octets[i] << 16;
            if (r == 2) v |= p_Octets[i + 1] << 8;
            s += c_Alphabet[(v >> 18) & 63];
            s += c_Alphabet[(v >> 12) & 63];
            if (r == 2) s += c_Alphabet[(v >> 6) & 63];
        }
        return s;
    }

    bool DepuisBase64(const std::string& p_Texte, std::vector<uint8_t>& p_Octets) {
        uint32_t v = 0;
        int n = 0;
        for (const char c : p_Texte) {
            const char* s_Pos = strchr(c_Alphabet, c);
            if (!s_Pos || !c) return false;
            v = (v << 6) | static_cast<uint32_t>(s_Pos - c_Alphabet);
            if (++n == 4) {
                p_Octets.push_back(static_cast<uint8_t>(v >> 16));
                p_Octets.push_back(static_cast<uint8_t>(v >> 8));
                p_Octets.push_back(static_cast<uint8_t>(v));
                v = 0;
                n = 0;
            }
        }
        if (n == 1) return false;
        if (n == 2) p_Octets.push_back(static_cast<uint8_t>(v >> 4));
        if (n == 3) {
            p_Octets.push_back(static_cast<uint8_t>(v >> 10));
            p_Octets.push_back(static_cast<uint8_t>(v >> 2));
        }
        return true;
    }
}

std::string EncoderContrat(const nlohmann::json& p_Contrat) {
    const std::string s_Json = p_Contrat.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    mz_ulong s_Taille = mz_compressBound(static_cast<mz_ulong>(s_Json.size()));
    std::vector<uint8_t> s_Compresse(s_Taille);
    if (mz_compress2(s_Compresse.data(), &s_Taille, reinterpret_cast<const uint8_t*>(s_Json.data()),
                     static_cast<mz_ulong>(s_Json.size()), MZ_BEST_COMPRESSION) != MZ_OK) {
        return {};
    }
    s_Compresse.resize(s_Taille);
    return c_Prefixe + Base64(s_Compresse);
}

nlohmann::json DecoderContrat(const std::string& p_Code) {
    std::string s_Texte;
    for (const char c : p_Code) {
        if (!std::isspace(static_cast<unsigned char>(c))) s_Texte += c;
    }
    if (s_Texte.rfind(c_Prefixe, 0) == 0) s_Texte.erase(0, strlen(c_Prefixe));
    if (s_Texte.empty() || s_Texte.size() > 8192) return nullptr;

    std::vector<uint8_t> s_Compresse;
    if (!DepuisBase64(s_Texte, s_Compresse)) return nullptr;

    // Un contrat ne dépasse pas quelques kilo-octets : 64 Ko au plus.
    std::vector<uint8_t> s_Json(65536);
    mz_ulong s_Taille = static_cast<mz_ulong>(s_Json.size());
    if (mz_uncompress(s_Json.data(), &s_Taille, s_Compresse.data(), static_cast<mz_ulong>(s_Compresse.size())) != MZ_OK) {
        return nullptr;
    }

    auto s_Contrat = nlohmann::json::parse(s_Json.begin(), s_Json.begin() + s_Taille, nullptr, false);
    if (s_Contrat.is_discarded() || !s_Contrat.is_object() || s_Contrat.value("v", 0) != 1) return nullptr;
    return s_Contrat;
}
