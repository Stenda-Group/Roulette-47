#include "Glacier.h"

#include "Diagnostic.h"

#include <array>
#include <cstdio>

#include <Windows.h>

namespace Glacier {
    namespace {
        // Une méthode virtuelle d'un objet du jeu, par son rang dans la vtable.
        template <class F>
        F Methode(const void* p_Objet, size_t p_Rang) {
            return reinterpret_cast<F>((*reinterpret_cast<void* const* const*>(p_Objet))[p_Rang]);
        }

        // (IAllocator) Allocate : rang 7, Free : rang 9.
        // (IPageAllocator) GetAllocator : rang 9.
        using FnAllouer = void*(__fastcall*)(void* p_Allocateur, uint64_t p_Taille);
        using FnLiberer = void(__fastcall*)(void* p_Allocateur, void* p_Bloc);
        using FnAllocateurDe = void*(__fastcall*)(void* p_Pages, void* p_Bloc);

        // (ZMemoryManager) pages en 0x00, allocateur ordinaire en 0x10.
        void* AllocateurOrdinaire() {
            if (!Globales::Memoire || !*Globales::Memoire) return nullptr;
            return Champ<void*>(*Globales::Memoire, 0x10);
        }

        void* AllocateurDe(void* p_Bloc) {
            if (!Globales::Memoire || !*Globales::Memoire) return nullptr;
            if (void* s_Pages = Champ<void*>(*Globales::Memoire, 0x00)) {
                if (void* s_Allocateur = Methode<FnAllocateurDe>(s_Pages, 9)(s_Pages, p_Bloc)) return s_Allocateur;
            }
            return AllocateurOrdinaire();
        }

        // (ZBuffer) un pointeur vers {références (int32), taille (uint32)}, les
        // octets suivant cet en-tête de 8 octets.
        struct EnTeteTampon {
            int32_t m_nReferences;
            uint32_t m_nTaille;
        };

        constexpr std::array<uint32_t, 256> TableCrc32() {
            std::array<uint32_t, 256> s_Table {};
            for (uint32_t i = 0; i < 256; ++i) {
                uint32_t c = i;
                for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
                s_Table[i] = c;
            }
            return s_Table;
        }

        constexpr auto c_TableCrc32 = TableCrc32();
    }

    std::string Guid::EnTexte() const {
        char s_Texte[40];
        snprintf(s_Texte, sizeof(s_Texte), "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                 m_A, m_B, m_C, m_D[0], m_D[1], m_D[2], m_D[3], m_D[4], m_D[5], m_D[6], m_D[7]);
        return s_Texte;
    }

    uint32_t Crc32(std::string_view p_Texte) {
        uint32_t s_Crc = 0xFFFFFFFFu;
        for (const char c : p_Texte) s_Crc = c_TableCrc32[(s_Crc ^ static_cast<uint8_t>(c)) & 0xFF] ^ (s_Crc >> 8);
        return s_Crc ^ 0xFFFFFFFFu;
    }

    std::string AdresseImages() {
        if (!Globales::EnLigne) return {};
        return VersTexte(Champ<Chaine>(Globales::EnLigne, 0x430));
    }

    // La signature est une chaîne « protégée » (TCheatProtect<ZString>) : une
    // chaîne ordinaire dont seuls les CARACTÈRES sont brouillés, avec pour clé
    // l'octet bas de son champ longueur (drapeaux compris). Chaque caractère
    // i < n-1 est xoré avec (caractère brouillé i+1 + clé), le dernier avec la
    // clé seule.
    namespace {
        // Sans objet C++ (exigence de __try) : une adresse fausse ne doit pas
        // fermer le jeu. Rend le nombre de caractères copiés, 0 si illisible.
        uint32_t CopierSignature(const Chaine* p_Brouillee, char* p_Sortie, uint32_t p_Max, uint32_t& p_Drapeaux) {
            __try {
                p_Drapeaux = p_Brouillee->m_nLongueur;
                const uint32_t s_Taille = static_cast<uint32_t>(p_Brouillee->Taille());
                if (!p_Brouillee->m_pCaracteres || s_Taille == 0 || s_Taille > p_Max) return 0;
                memcpy(p_Sortie, p_Brouillee->m_pCaracteres, s_Taille);
                return s_Taille;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                return 0;
            }
        }
    }

    std::string SignatureImages() {
        if (!Globales::EnLigne) return {};

        char s_Tampon[4096];
        uint32_t s_Drapeaux = 0;
        const uint32_t n = CopierSignature(&Champ<Chaine>(Globales::EnLigne, 0x440), s_Tampon, sizeof(s_Tampon), s_Drapeaux);
        if (n == 0) return {};

        const auto s_Cle = static_cast<uint8_t>(s_Drapeaux);
        std::string s_Clair(s_Tampon, n);
        for (uint32_t i = 0; i + 1 < n; ++i) {
            s_Clair[i] = static_cast<char>(static_cast<uint8_t>(s_Tampon[i]) ^ static_cast<uint8_t>(static_cast<uint8_t>(s_Tampon[i + 1]) + s_Cle));
        }
        s_Clair[n - 1] = static_cast<char>(static_cast<uint8_t>(s_Tampon[n - 1]) ^ s_Cle);
        return s_Clair;
    }

    bool DemanderRessource(RefRessource& p_Ref, uint64_t p_IdExecution) {
        if (!Globales::Ressources) return false;
        // (ZResourceManager) GetResourcePtr : rang 8 ; l'identifiant d'exécution
        // est rangé partie haute d'abord.
        const uint32_t s_Id[2] = { static_cast<uint32_t>(p_IdExecution >> 32), static_cast<uint32_t>(p_IdExecution) };
        using FnDemander = void*(__fastcall*)(void* p_Gestionnaire, RefRessource* p_Sortie, const uint32_t* p_Id, int p_Priorite);
        Methode<FnDemander>(Globales::Ressources, 8)(Globales::Ressources, &p_Ref, s_Id, 0);
        return p_Ref.m_nIndex >= 0;
    }

    const void* DonneesRessource(const RefRessource& p_Ref) {
        if (p_Ref.m_nIndex < 0 || !Globales::ConteneurRessources || !*Globales::ConteneurRessources) return nullptr;
        // (ZResourceContainer) {nombre, capacité, fiches} ; fiche de 64 octets :
        // données en 0x08, état en 0x20 (4 : valide).
        const auto* s_Fiches = Champ<const uint8_t*>(*Globales::ConteneurRessources, 0x8);
        if (!s_Fiches) return nullptr;
        const auto* s_Fiche = s_Fiches + static_cast<size_t>(p_Ref.m_nIndex) * 64;
        if (Champ<int32_t>(s_Fiche, 0x20) != 4) return nullptr;
        return Champ<const void*>(s_Fiche, 0x8);
    }

    int EtatRessource(const RefRessource& p_Ref) {
        if (p_Ref.m_nIndex < 0 || !Globales::ConteneurRessources || !*Globales::ConteneurRessources) return -1;
        const auto* s_Fiches = Champ<const uint8_t*>(*Globales::ConteneurRessources, 0x8);
        if (!s_Fiches) return -1;
        return Champ<int32_t>(s_Fiches + static_cast<size_t>(p_Ref.m_nIndex) * 64, 0x20);
    }

    std::span<const uint8_t> TamponReponse(const void* p_Reponse) {
        const auto* s_EnTete = Champ<const EnTeteTampon*>(p_Reponse, 0x40);
        if (!s_EnTete || s_EnTete->m_nTaille == 0) return {};
        return { reinterpret_cast<const uint8_t*>(s_EnTete + 1), s_EnTete->m_nTaille };
    }

    bool RemplacerTamponReponse(void* p_Reponse, std::string_view p_Octets) {
        void* s_Allocateur = AllocateurOrdinaire();
        if (!s_Allocateur) return false;

        auto* s_Nouveau = static_cast<EnTeteTampon*>(
            Methode<FnAllouer>(s_Allocateur, 7)(s_Allocateur, sizeof(EnTeteTampon) + p_Octets.size()));
        if (!s_Nouveau) return false;

        s_Nouveau->m_nReferences = 1;
        s_Nouveau->m_nTaille = static_cast<uint32_t>(p_Octets.size());
        memcpy(s_Nouveau + 1, p_Octets.data(), p_Octets.size());

        auto*& s_Champ = *reinterpret_cast<EnTeteTampon**>(static_cast<uint8_t*>(p_Reponse) + 0x40);
        EnTeteTampon* s_Ancien = s_Champ;
        s_Champ = s_Nouveau;

        // L'ancien : une référence de moins, ou rendu à son allocateur.
        if (s_Ancien) {
            if (s_Ancien->m_nReferences > 1) {
                --s_Ancien->m_nReferences;
            }
            else if (void* s_Proprietaire = AllocateurDe(s_Ancien)) {
                Methode<FnLiberer>(s_Proprietaire, 9)(s_Proprietaire, s_Ancien);
            }
        }

        return true;
    }
}
