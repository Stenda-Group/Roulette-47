#include "Http.h"

#include <miniz.h>

std::string CorpsEnTexte(const uint8_t* p_Octets, size_t p_Taille) {
    // gzip : 1F 8B, méthode 8 (deflate), puis des champs facultatifs selon les drapeaux.
    if (p_Taille < 18 || p_Octets[0] != 0x1F || p_Octets[1] != 0x8B || p_Octets[2] != 8) {
        return std::string(reinterpret_cast<const char*>(p_Octets), p_Taille);
    }

    const uint8_t s_Drapeaux = p_Octets[3];
    size_t s_Pos = 10;

    if (s_Drapeaux & 0x04) { // FEXTRA
        if (s_Pos + 2 > p_Taille) return {};
        s_Pos += 2 + (p_Octets[s_Pos] | (p_Octets[s_Pos + 1] << 8));
    }

    if (s_Drapeaux & 0x08) { // FNAME
        while (s_Pos < p_Taille && p_Octets[s_Pos]) ++s_Pos;
        ++s_Pos;
    }

    if (s_Drapeaux & 0x10) { // FCOMMENT
        while (s_Pos < p_Taille && p_Octets[s_Pos]) ++s_Pos;
        ++s_Pos;
    }

    if (s_Drapeaux & 0x02) s_Pos += 2; // FHCRC

    if (s_Pos + 8 >= p_Taille) return {};

    size_t s_Sortie = 0;
    void* s_Texte = tinfl_decompress_mem_to_heap(p_Octets + s_Pos, p_Taille - s_Pos - 8, &s_Sortie, 0);

    if (!s_Texte) return {};

    std::string s_Resultat(static_cast<const char*>(s_Texte), s_Sortie);
    mz_free(s_Texte);
    return s_Resultat;
}

bool EstGzip(const uint8_t* p_Octets, size_t p_Taille) {
    return p_Taille >= 18 && p_Octets[0] == 0x1F && p_Octets[1] == 0x8B && p_Octets[2] == 8;
}

std::string EnGzip(const std::string& p_Texte) {
    size_t s_Taille = 0;
    void* s_Deflate = tdefl_compress_mem_to_heap(p_Texte.data(), p_Texte.size(), &s_Taille, TDEFL_DEFAULT_MAX_PROBES);

    if (!s_Deflate) return {};

    std::string s_Gzip = std::string("\x1F\x8B\x08\x00\x00\x00\x00\x00\x00\xFF", 10);
    s_Gzip.append(static_cast<const char*>(s_Deflate), s_Taille);
    mz_free(s_Deflate);

    const auto s_Crc = static_cast<uint32_t>(mz_crc32(MZ_CRC32_INIT, reinterpret_cast<const unsigned char*>(p_Texte.data()), p_Texte.size()));
    const auto s_Longueur = static_cast<uint32_t>(p_Texte.size());

    for (int i = 0; i < 4; ++i) s_Gzip += static_cast<char>((s_Crc >> (8 * i)) & 0xFF);
    for (int i = 0; i < 4; ++i) s_Gzip += static_cast<char>((s_Longueur >> (8 * i)) & 0xFF);

    return s_Gzip;
}
