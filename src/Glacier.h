#pragma once

// Ce que le mod sait de la mémoire du moteur de HITMAN World of Assassination
// (Glacier 2), écrit pour lui seul.
//
// Ce ne sont que des VUES en lecture : le mod ne construit, ne copie ni ne
// détruit aucun objet du jeu. Chaque champ est lu à son décalage, relevé dans
// la version du jeu du 24/11/2026 ; si une mise à jour du jeu déplace un champ,
// c'est ici qu'on le corrige, et nulle part ailleurs.
//
// Les noms entre parenthèses (ZString, ZActor…) sont ceux que le jeu donne à
// ses propres types : ils servent à s'y retrouver dans un désassembleur.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>

namespace Glacier {
    // Lecture d'un champ à un décalage donné d'un objet du jeu.
    template <class T>
    inline const T& Champ(const void* p_Objet, size_t p_Decalage) {
        return *reinterpret_cast<const T*>(static_cast<const uint8_t*>(p_Objet) + p_Decalage);
    }

    inline uint8_t Octet(const void* p_Objet, size_t p_Decalage) {
        return Champ<uint8_t>(p_Objet, p_Decalage);
    }

    // ------------------------------------------------------------- Chaîne
    // (ZString) 16 octets : longueur et drapeaux, puis le pointeur des
    // caractères. Les deux bits forts de la longueur disent que la chaîne ne
    // possède pas sa mémoire (littéral) ; sinon un en-tête de 16 octets précède
    // les caractères (taille allouée, compteur de références, suivant).
    struct Chaine {
        uint32_t m_nLongueur = 0x80000000u;
        uint32_t m_nRembourrage = 0;
        const char* m_pCaracteres = "";

        [[nodiscard]] size_t Taille() const { return m_nLongueur & 0x3FFFFFFFu; }
        [[nodiscard]] bool Possede() const { return (m_nLongueur & 0xC0000000u) == 0; }
        [[nodiscard]] std::string_view Vue() const {
            return m_pCaracteres ? std::string_view(m_pCaracteres, Taille()) : std::string_view();
        }
        [[nodiscard]] bool operator==(std::string_view p_Autre) const { return Vue() == p_Autre; }
    };
    static_assert(sizeof(Chaine) == 16);

    inline std::string VersTexte(const Chaine& p_Chaine) {
        return std::string(p_Chaine.Vue());
    }

    // ---------------------------------------------------------- Identifiant
    // (ZGuid, ZRepositoryID) l'identifiant d'une fiche du dépôt.
    struct Guid {
        uint32_t m_A = 0;
        uint16_t m_B = 0;
        uint16_t m_C = 0;
        uint8_t m_D[8] {};

        // « 0a1b2c3d-… » en minuscules.
        [[nodiscard]] std::string EnTexte() const;
    };
    static_assert(sizeof(Guid) == 16);

    // ---------------------------------------------------------------- Types
    // (IType) la description d'un type du moteur ; seul son nom sert ici.
    struct InfoType {
        const void* m_pFonctions;
        uint16_t m_nTaille;
        uint8_t m_nAlignement;
        uint16_t m_nDrapeaux;
        const char* m_pszNom;
    };
    static_assert(offsetof(InfoType, m_pszNom) == 0x10);

    // (STypeID) une entrée du registre des types.
    struct IdType {
        uint16_t m_nDrapeaux;
        uint16_t m_nNumero;
        const InfoType* m_pType;
        const IdType* m_pSource;

        [[nodiscard]] const InfoType* Info() const {
            if (m_nDrapeaux == 1 || (!m_pType && m_pSource)) return m_pSource ? m_pSource->m_pType : nullptr;
            return m_pType;
        }
    };

    // -------------------------------------------------------------- Tableau
    // (TArray) début, fin, fin de l'allocation. Un tableau de petits éléments
    // (16 octets au plus) peut les ranger SUR PLACE : bit 62 du troisième mot,
    // nombre d'éléments dans son octet bas, éléments à partir du premier mot.
    template <class T>
    struct Tableau {
        const T* m_pDebut;
        const T* m_pFin;
        uint64_t m_nTroisieme;

        [[nodiscard]] bool SurPlace() const { return sizeof(T) <= 16 && ((m_nTroisieme >> 62) & 1); }
        [[nodiscard]] const T* begin() const { return SurPlace() ? reinterpret_cast<const T*>(&m_pDebut) : m_pDebut; }
        [[nodiscard]] const T* end() const {
            return SurPlace() ? begin() + static_cast<uint8_t>(m_nTroisieme) : m_pFin;
        }
        [[nodiscard]] size_t size() const { return static_cast<size_t>(end() - begin()); }
    };
    static_assert(sizeof(Tableau<int>) == 24);

    // ---------------------------------------------------------------- Objet
    // (ZDynamicObject) une valeur de type quelconque : son type, ses données.
    struct Objet {
        const IdType* m_pType;
        const void* m_pDonnees;

        [[nodiscard]] const char* NomDuType() const {
            const auto* s_Info = m_pType ? m_pType->Info() : nullptr;
            return s_Info ? s_Info->m_pszNom : nullptr;
        }

        // Les données si l'objet est du type nommé (nom du moteur :
        // « ZString », « bool », « TArray<ZDynamicObject> »…), sinon nullptr.
        template <class T>
        [[nodiscard]] const T* Comme(const char* p_NomDuType) const {
            const char* s_Nom = NomDuType();
            if (!s_Nom || !m_pDonnees || strcmp(s_Nom, p_NomDuType) != 0) return nullptr;
            return static_cast<const T*>(m_pDonnees);
        }
    };
    static_assert(sizeof(Objet) == 16);

    // (SDynamicObjectKeyValuePair) un champ d'objet JSON du moteur.
    struct Paire {
        Chaine m_Cle;
        Objet m_Valeur;
    };
    static_assert(sizeof(Paire) == 32);

    using ObjetJson = Tableau<Paire>;    // « TArray<SDynamicObjectKeyValuePair> »
    using TableauJson = Tableau<Objet>;  // « TArray<ZDynamicObject> »

    // (TEntityRef<T>) l'entité, puis l'interface qui nous intéresse.
    template <class T>
    struct RefEntite {
        void* m_pEntite;
        T* m_pInterface;
    };
    static_assert(sizeof(RefEntite<int>) == 16);

    // ---------------------------------------------------------------- Tenue
    // (ZGlobalOutfitKit) une tenue du jeu.
    struct Tenue {
        static constexpr int32_t c_TypeCostume = 1; // EOutfitType::eOT_Suit

        [[nodiscard]] const Chaine& NomCommun() const { return Champ<Chaine>(this, 0x50); }
        [[nodiscard]] const Chaine& Titre() const { return Champ<Chaine>(this, 0x60); }
        [[nodiscard]] bool PortablePar47() const { return Octet(this, 0xA9) != 0; } // m_bHeroDisguiseAvailable
        [[nodiscard]] int32_t Type() const { return Champ<int32_t>(this, 0x194); }
        [[nodiscard]] bool CostumeDe47() const { return Octet(this, 0x19E) != 0; } // m_bIsHitmanSuit
        [[nodiscard]] const Guid& Id() const { return Champ<Guid>(this, 0x200); }
    };

    // --------------------------------------------------------------- Acteur
    // (ZActor) un personnage du niveau. Ses drapeaux sont des bits :
    //   0x1178 bit 2 : vivant
    //   0x117A bit 0 : cible du contrat (lueur rouge en Instinct)
    //   0x117A bit 1 : cible « vivante » (icône sur la carte)
    //   0x117B bit 0 : figurant de la foule
    struct Acteur {
        [[nodiscard]] const Chaine& Nom() const { return Champ<Chaine>(this, 0x498); }
        [[nodiscard]] const Tenue* TenuePortee() const { return Champ<RefEntite<Tenue>>(this, 0xFF8).m_pInterface; }
        [[nodiscard]] bool Vivant() const { return Octet(this, 0x1178) & 0x04; }
        [[nodiscard]] bool CibleDuContrat() const { return Octet(this, 0x117A) & 0x01; }
        [[nodiscard]] bool Figurant() const { return Octet(this, 0x117B) & 0x01; }

        // Les deux drapeaux de cible, côté client seulement.
        void MarquerCible(bool p_Oui) {
            auto& s_Octet = *(reinterpret_cast<uint8_t*>(this) + 0x117A);
            s_Octet = p_Oui ? (s_Octet | 0x03) : (s_Octet & ~0x03);
        }
    };

    // (ZTargetManager::STargetInfo) une cible du contrat en cours.
    struct InfoCible {
        uint32_t m_nJeton;
        uint32_t m_nRembourrage;
        RefEntite<Acteur> m_Acteur;   // 0x08
        uint8_t m_Reserve[0x28];      // 0x18 : visibilité, distance, lancers de rayons
        Guid m_IdDepot;               // 0x40
        uint8_t m_Fin[0x08];          // 0x50
    };
    static_assert(sizeof(InfoCible) == 0x58);
    static_assert(offsetof(InfoCible, m_IdDepot) == 0x40);

    // --------------------------------------------------------- Gestionnaires
    // Les adresses des gestionnaires du jeu, retrouvées par Moteur::Resoudre().
    namespace Globales {
        inline uint8_t* BoucleDeJeu = nullptr;         // ZGameLoopManager
        inline uint8_t* Personnages = nullptr;         // ZActorManager
        inline uint8_t* Ressources = nullptr;          // ZResourceManager
        inline uint8_t* Contenus = nullptr;            // ZContentKitManager
        inline uint8_t* TextesInterface = nullptr;     // ZUIText
        inline uint8_t* Cibles = nullptr;              // ZTargetManager
        inline uint8_t* Contrats = nullptr;            // ZContractsManager
        inline uint8_t* EnLigne = nullptr;             // ZOnlineManager
        inline uint8_t** Memoire = nullptr;            // ZMemoryManager*
        inline uint8_t** ConteneurRessources = nullptr; // ZResourceContainer*
    }

    // (ZGameLoopManager::m_pUpdateEventContainer) où s'abonner à chaque image.
    inline void* ConteneurImage() {
        return Globales::BoucleDeJeu ? Champ<void*>(Globales::BoucleDeJeu, 0x58) : nullptr;
    }

    // (ZActorManager::m_aActors) les 500 places de personnages.
    inline std::span<const RefEntite<Acteur>> Acteurs() {
        if (!Globales::Personnages) return {};
        return { &Champ<RefEntite<Acteur>>(Globales::Personnages, 0x8), 500 };
    }

    // (ZTargetManager::m_aTargets) les cibles du contrat en cours.
    inline std::span<const InfoCible> CiblesDuContrat() {
        if (!Globales::Cibles) return {};
        const auto& s_Tableau = Champ<Tableau<InfoCible>>(Globales::Cibles, 0x8);
        if (!s_Tableau.m_pDebut || s_Tableau.m_pFin < s_Tableau.m_pDebut) return {};
        return { s_Tableau.m_pDebut, s_Tableau.m_pFin };
    }

    // (ZContractsManager::m_contractContext, en 0x8A0) le contrat en cours.
    struct ContexteContrat {
        [[nodiscard]] const Chaine& IdLieu() const { return Champ<Chaine>(this, 0x10); }
        [[nodiscard]] const Guid& DeguisementDeDepart() const { return Champ<Guid>(this, 0x40); }
        [[nodiscard]] const Objet& Donnees() const { return Champ<Objet>(this, 0xA8); }
    };

    inline const ContexteContrat* Contexte() {
        return Globales::Contrats ? &Champ<ContexteContrat>(Globales::Contrats, 0x8A0) : nullptr;
    }

    // (ZContentKitManager::m_repositoryGlobalOutfitKits) toutes les tenues
    // chargées, rangées dans un arbre binaire en 0x08 : {réservé, nul, racine
    // droite, racine gauche (la vraie racine)} puis le nombre d'éléments.
    // Nœud : {équilibre, parent, gauche, droite, donnée en 0x20}, donnée =
    // {identifiant (16 octets), RefEntite<Tenue>}.
    template <class F>
    void PourChaqueTenue(F&& p_Rappel) {
        if (!Globales::Contenus) return;
        const auto* s_Arbre = Globales::Contenus + 0x8;
        const auto* s_Racine = Champ<const uint8_t*>(s_Arbre, 0x18);
        const auto s_Nombre = Champ<int32_t>(s_Arbre, 0x20);
        if (!s_Racine || s_Nombre <= 0) return;

        // Parcours dans l'ordre, sans récursion, borné par le nombre annoncé.
        const uint8_t* s_Pile[96];
        size_t s_Hauteur = 0;
        const uint8_t* s_Noeud = s_Racine;
        int32_t s_Vus = 0;

        while ((s_Noeud || s_Hauteur > 0) && s_Vus < s_Nombre) {
            while (s_Noeud) {
                if (s_Hauteur == std::size(s_Pile)) return; // arbre illisible
                s_Pile[s_Hauteur++] = s_Noeud;
                s_Noeud = Champ<const uint8_t*>(s_Noeud, 0x10);
            }
            s_Noeud = s_Pile[--s_Hauteur];
            p_Rappel(Champ<Guid>(s_Noeud, 0x20), Champ<RefEntite<Tenue>>(s_Noeud, 0x30).m_pInterface);
            ++s_Vus;
            s_Noeud = Champ<const uint8_t*>(s_Noeud, 0x18);
        }
    }

    // (ZOnlineManager) l'adresse du service d'images et sa signature d'accès.
    std::string AdresseImages();
    std::string SignatureImages();

    // ------------------------------------------------------------ Ressources
    // (ZResourcePtr) un index dans le conteneur des ressources, -1 si aucune.
    struct RefRessource {
        int32_t m_nIndex = -1;
        uint32_t m_nRembourrage = 0;
    };

    // Demande une ressource au gestionnaire par son identifiant d'exécution
    // (64 bits : MD5 du chemin, octets 1 à 7). La référence est gardée à vie.
    bool DemanderRessource(RefRessource& p_Ref, uint64_t p_IdExecution);

    // Les données de la ressource si elle est chargée et valide, sinon nullptr.
    const void* DonneesRessource(const RefRessource& p_Ref);

    // L'état de la ressource (4 : valide), -1 si illisible ; pour le diagnostic.
    int EtatRessource(const RefRessource& p_Ref);

    // (THashMap<ZRepositoryID, ZDynamicObject>) la table du dépôt, en tête des
    // données de « pro.repo » : {nombre, libre, nombre de seaux, seaux, nœuds}.
    // Nœud de 40 octets : {suivant (int32), identifiant, objet}.
    template <class F>
    void PourChaqueFiche(const void* p_Table, F&& p_Rappel) {
        const auto s_Seaux = Champ<uint32_t>(p_Table, 0x8);
        const auto* s_Tetes = Champ<const uint32_t*>(p_Table, 0x10);
        const auto* s_Noeuds = Champ<const uint8_t*>(p_Table, 0x18);
        if (!s_Tetes || !s_Noeuds) return;

        for (uint32_t s_Seau = 0; s_Seau < s_Seaux; ++s_Seau) {
            for (uint32_t i = s_Tetes[s_Seau]; i != UINT32_MAX;) {
                const auto* s_Noeud = s_Noeuds + static_cast<size_t>(i) * 40;
                p_Rappel(Champ<Guid>(s_Noeud, 0x8), Champ<Objet>(s_Noeud, 0x18));
                i = Champ<uint32_t>(s_Noeud, 0x0);
            }
        }
    }

    // ------------------------------------------------------------- Mémoire
    // Remplace le tampon d'une réponse HTTP (ZHttpResultDynamicObject, tampon
    // en 0x40) par une copie de p_Octets, allouée par l'allocateur du jeu : le
    // jeu la libérera lui-même. L'ancien tampon est rendu comme le ferait le jeu.
    bool RemplacerTamponReponse(void* p_Reponse, std::string_view p_Octets);

    // Le tampon d'une réponse HTTP : données et taille (nullptr, 0 si vide).
    std::span<const uint8_t> TamponReponse(const void* p_Reponse);

    // ---------------------------------------------------------------- Divers
    // CRC32 (IEEE) : le jeu range ses textes sous celui de leur clé.
    uint32_t Crc32(std::string_view p_Texte);
}
