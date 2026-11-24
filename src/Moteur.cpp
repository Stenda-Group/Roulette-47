#include "Moteur.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <format>
#include <vector>
#include <mutex>
#include <string_view>

extern "C" {
#include <hde64.h>
}

#include "Glacier.h"

#include "Diagnostic.h"

namespace {
    // (ZDelegate) un rappel du moteur : méthode, réserve, fonction, instance.
    struct SDelegue {
        void* m_pMethode = nullptr;
        void* m_pReserve = nullptr;
        void (*m_pFonction)(const void*) = nullptr;
        void* m_pInstance = nullptr;
    };
    static_assert(sizeof(SDelegue) == 32);

    // ZUpdateEventContainer::AddDelegate(conteneur, délégué, priorité, mode) ;
    // mode 2 : à chaque image, même en pause.
    using FnAjouterDelegue = void(__fastcall*)(void*, const SDelegue&, int, int);
    using FnTraduire = bool(__fastcall*)(void*, int32_t, Glacier::Chaine&, int32_t&);
    using FnLibererTexte = void(__fastcall*)(void*);
    using FnEvenement = void(__fastcall*)(void*, uint32_t, const Glacier::Objet&);
    using FnPage = void(__fastcall*)(void*, Glacier::Objet&, void*, void*, void*);
    using FnReponse = void(__fastcall*)(void*);

    FnAjouterDelegue g_AjouterDelegue = nullptr;
    FnTraduire g_Traduire = nullptr;
    FnLibererTexte g_LibererTexte = nullptr;
    void** g_VtableSucces = nullptr; // vtable de ZAchievementManagerSimple

    FnEvenement g_EvenementOriginal = nullptr;
    FnPage g_PageOriginale = nullptr;
    void* g_FonctionPage = nullptr; // ZDynamicPageController::Expand
    void (*g_RappelPage)(const Glacier::Objet&) = nullptr;
    FnReponse g_ReponseOriginale = nullptr;
    void* g_FonctionReponse = nullptr; // ZHttpResultDynamicObject::OnBufferReady
    bool (*g_RappelReponse)(const uint8_t*, size_t, std::string&) = nullptr;
    void (*g_RappelEvenement)(const Glacier::Objet&) = nullptr;
    void (*g_RappelImage)() = nullptr;

    // -------------------------------------------------------- Recherche

    struct SZone {
        uintptr_t m_Debut = 0;
        size_t m_Taille = 0;
    };

    // Les sections exécutables de HITMAN3.exe.
    std::vector<SZone> ZonesDeCode() {
        std::vector<SZone> s_Zones;
        const auto s_Base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        const auto* s_Dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(s_Base);
        const auto* s_Nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(s_Base + s_Dos->e_lfanew);
        const auto* s_Section = IMAGE_FIRST_SECTION(s_Nt);

        for (WORD i = 0; i < s_Nt->FileHeader.NumberOfSections; ++i, ++s_Section) {
            if (s_Section->Characteristics & IMAGE_SCN_MEM_EXECUTE) {
                s_Zones.push_back({ s_Base + s_Section->VirtualAddress, s_Section->Misc.VirtualSize });
            }
        }

        return s_Zones;
    }

    uintptr_t ChercherBrut(const uint8_t* p_Motif, const char* p_Masque) {
        const size_t s_Longueur = strlen(p_Masque);

        // Premier octet fixe du motif : on saute de candidat en candidat par memchr.
        size_t s_Ancre = 0;
        while (s_Ancre < s_Longueur && p_Masque[s_Ancre] == '?') ++s_Ancre;
        if (s_Ancre == s_Longueur) return 0;

        for (const auto& s_Zone : ZonesDeCode()) {
            if (s_Zone.m_Taille < s_Longueur) continue;

            const auto* s_Debut = reinterpret_cast<const uint8_t*>(s_Zone.m_Debut);
            const auto* s_Fin = s_Debut + s_Zone.m_Taille - s_Longueur;
            const auto* s_Curseur = s_Debut + s_Ancre;

            while (s_Curseur <= s_Fin + s_Ancre) {
                s_Curseur = static_cast<const uint8_t*>(
                    memchr(s_Curseur, p_Motif[s_Ancre], static_cast<size_t>(s_Fin + s_Ancre - s_Curseur) + 1)
                );

                if (!s_Curseur) break;

                const auto* s_Candidat = s_Curseur - s_Ancre;
                bool s_Ok = true;

                for (size_t i = 0; i < s_Longueur; ++i) {
                    if (p_Masque[i] != '?' && s_Candidat[i] != p_Motif[i]) {
                        s_Ok = false;
                        break;
                    }
                }

                if (s_Ok) return reinterpret_cast<uintptr_t>(s_Candidat);
                ++s_Curseur;
            }
        }

        return 0;
    }

    // Un motif de DÉBUT de fonction. Si un autre mod a posé son détour, les cinq
    // premiers octets sont devenus « E9 xx xx xx xx » : on recherche sans eux.
    uintptr_t ChercherFonction(const char* p_Nom, const char* p_Motif, const char* p_Masque) {
        const auto* s_Motif = reinterpret_cast<const uint8_t*>(p_Motif);

        if (const auto s_Adresse = ChercherBrut(s_Motif, p_Masque)) {
            Diag("Moteur : {} trouvée.", p_Nom);
            return s_Adresse;
        }

        std::string s_Masque = p_Masque;

        if (s_Masque.size() > 5) {
            s_Masque.replace(0, 5, "?????");

            if (const auto s_Adresse = ChercherBrut(s_Motif, s_Masque.c_str());
                s_Adresse && *reinterpret_cast<const uint8_t*>(s_Adresse) == 0xE9) {
                Diag("Moteur : {} trouvée (détournée par un autre mod).", p_Nom);
                return s_Adresse;
            }
        }

        Diag("Moteur : {} INTROUVABLE (jeu mis à jour ?).", p_Nom);
        return 0;
    }

    // Une adresse donnée par une instruction relative à RIP (lea/mov [rip+x]).
    template <class T>
    bool ChercherGlobale(T& p_Sortie, const char* p_Nom, const char* p_Motif, const char* p_Masque, ptrdiff_t p_Decalage) {
        const auto s_Cible = ChercherBrut(reinterpret_cast<const uint8_t*>(p_Motif), p_Masque);

        if (!s_Cible) {
            Diag("Moteur : {} INTROUVABLE (jeu mis à jour ?).", p_Nom);
            return false;
        }

        const uintptr_t s_Relatif = s_Cible + p_Decalage;
        const uintptr_t s_Adresse = s_Relatif + *reinterpret_cast<const int32_t*>(s_Relatif) + sizeof(int32_t);
        p_Sortie = reinterpret_cast<T>(s_Adresse);
        return true;
    }

    // -------------------------------------------------------- Détours

    void __fastcall SurEvenement(void* th, uint32_t p_Index, const Glacier::Objet& p_Evenement) {
        if (g_RappelEvenement) {
            // Rien ne doit remonter jusqu'au jeu : l'événement part au serveur
            // quoi qu'il arrive à notre lecture.
            __try {
                g_RappelEvenement(p_Evenement);
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
        }

        g_EvenementOriginal(th, p_Index, p_Evenement);
    }

    // Une page de menu (préparation, choix de mission…) reçoit ses données.
    void __fastcall SurPage(void* th, Glacier::Objet& p_Donnees, void* a3, void* a4, void* a5) {
        if (g_RappelPage) {
            __try {
                g_RappelPage(p_Donnees);
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
        }

        g_PageOriginale(th, p_Donnees, a3, a4, a5);
    }

    // Hors du __try (des objets C++ à détruire) : lit la réponse et, si le mod
    // la réécrit, remplace le tampon que le jeu va lire.
    void TraiterReponse(void* th) {
        const auto s_Tampon = Glacier::TamponReponse(th);
        if (s_Tampon.empty()) return;

        std::string s_Nouveau;

        if (g_RappelReponse(s_Tampon.data(), s_Tampon.size(), s_Nouveau) && !s_Nouveau.empty()) {
            Glacier::RemplacerTamponReponse(th, s_Nouveau);
        }
    }

    // Une réponse du serveur est arrivée ; le jeu va la lire.
    void __fastcall SurReponse(void* th) {
        if (g_RappelReponse && th) {
            __try {
                TraiterReponse(th);
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
        }

        g_ReponseOriginale(th);
    }

    void OuvrirPageEnAttente(); // plus bas
    void MaintenirEntree();     // plus bas

    void SurImage(const void*) {
        OuvrirPageEnAttente();
        MaintenirEntree();

        if (g_RappelImage) {
            __try {
                g_RappelImage();
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
    }

    // ------------------------------------------------- Démarrage d'un contrat
    //
    // Relevé du 23/11/2026 dans HITMAN3.exe : l'action « start-contract » des
    // pages appelle Demarrer(page + 0xF8, &identifiant, &rappel, &rappel). La
    // fonction lit les réglages d'équipement du contrat puis demande son
    // chargement au gestionnaire des contrats (un singleton). De la page, elle
    // ne lit que deux choses :
    //   - +0x08 : un pointeur vers un bloc dont l'octet +0x08 passe à 1 quand le
    //     chargement a réussi (un drapeau « contrat lancé » de la page) ;
    //   - +0x18 : une liste de déguisements proposés, parcourue seulement si
    //     aucun déguisement n'est retenu.
    // Un contexte factice qui fournit ces deux champs (bloc à nous, liste vide)
    // suffit : la mission se lance sans aucune page de menu.

    using FnDemarrer = void(__fastcall*)(void*, void*, void*, void*);
    FnDemarrer g_Demarrer = nullptr;

    struct SListeVide {
        void* m_pDebut = nullptr;
        void* m_pFin = nullptr;
        uint64_t m_nDrapeaux = 0; // bit 62 nul : stockage externe, début == fin
        uint64_t m_Reserve[5] {};
    };

    struct SContexteFactice {
        void* m_p00 = nullptr;
        uint8_t* m_pDrapeaux = nullptr;
        void* m_p10 = nullptr;
        SListeVide* m_pListe = nullptr;
        uint8_t m_Reserve[0x80] {};
    };

    alignas(16) uint8_t g_BlocDrapeaux[64] {};
    SListeVide g_ListeVide;
    SContexteFactice g_Contexte { nullptr, g_BlocDrapeaux, nullptr, &g_ListeVide };

    // Un ZString du jeu qui ne possède pas sa chaîne (drapeau 0x80000000), comme
    // les littéraux du jeu ; aucun destructeur à appeler.
    struct SZStringPrete {
        uint32_t m_nLongueur;
        uint32_t m_nRembourrage;
        const char* m_pTexte;
    };

    bool AppelerDemarrer(const char* p_Id, uint32_t p_Taille) {
        SZStringPrete s_Id { p_Taille | 0x80000000u, 0, p_Id };
        // Deux std::function vides (64 octets, pointeur d'implémentation nul en
        // +0x38) : le jeu vérifie ce pointeur avant tout appel.
        alignas(16) uint8_t s_Ok[64] = {};
        alignas(16) uint8_t s_Erreur[64] = {};
        __try {
            g_Demarrer(&g_Contexte, &s_Id, s_Ok, s_Erreur);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    // ---------------------------------------------------- Ouverture d'une page
    //
    // Relevé du 24/11/2026. Aux menus, chaque action d'un modèle de page est un
    // objet (« ZLink@JSONTemplate », « ZStartContract@JSONTemplate »…). Pour un
    // lien, ZLink::Execute :
    //   1. obtient le navigateur des menus par la liaison rangée en +0x28 du
    //      lien (méthode n° 2 de cette liaison, qui rend {entité, interface}) ;
    //   2. remplit une requête de page (0x90 octets, construite par la fonction
    //      que retrouve c_MotifRequete) : page (ZString) en +0x00, arguments
    //      (ZDynamicObject) en +0x10, options en +0x40… (« addtohistory » en +0x42) ;
    //   3. appelle la méthode n° 5 de l'interface en +0x60 du navigateur.
    // Le mod fait de même : il retient le navigateur au passage du premier lien
    // que le jeu exécute (l'arrivée à l'accueil), et ouvre la page demandée à
    // l'image suivante, dans le thread du jeu. Les arguments sont fabriqués par
    // le lecteur JSON du jeu (celui de ZHttpResultDynamicObject::OnBufferReady).
    // Aucune touche, aucun parcours des menus.
    //
    // (ZDynamicPageController::HandleAction, essayé d'abord, ne reçoit rien à
    // l'accueil : les tuiles passent par ces objets d'action.)

    using FnLien = uint64_t(__fastcall*)(void*, void*, void*, void*);
    using FnDeserialiser = void*(__fastcall*)(void* p_Resultat, const uint8_t* p_Octets, uint64_t p_Taille, bool p_Option, void* p_Erreur);
    using FnConstruireRequete = void*(__fastcall*)(void* p_Requete);
    using FnLiaison = void*(__fastcall*)(void* p_Liaison, void* p_Sortie);
    using FnNaviguer = void(__fastcall*)(void* p_Interface, void* p_Requete);

    void* g_FonctionLien = nullptr;
    FnLien g_LienSuite = nullptr;
    FnDeserialiser g_Deserialiser = nullptr;
    FnConstruireRequete g_ConstruireRequete = nullptr;

    std::atomic<void*> g_Navigateur = nullptr;
    void (*g_RappelImageVoulu)() = nullptr;

    std::mutex g_VerrouLien;
    std::string g_Page;
    std::string g_Arguments;
    std::atomic<bool> g_LienEnAttente = false;

    struct SZStringLitterale {
        uint32_t m_nLongueur;
        uint32_t m_nRembourrage;
        const char* m_pTexte;
    };
    static_assert(sizeof(SZStringLitterale) == 16);

    // Le navigateur derrière un lien (nullptr si illisible).
    void* NavigateurDuLien(void* p_Lien) {
        __try {
            void* s_Liaison = *reinterpret_cast<void**>(static_cast<uint8_t*>(p_Lien) + 0x28);
            if (!s_Liaison) return nullptr;
            alignas(16) void* s_Sortie[4] = {};
            const auto s_Lire = (*reinterpret_cast<FnLiaison**>(s_Liaison))[2];
            s_Lire(s_Liaison, s_Sortie);
            return s_Sortie[1];
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return nullptr;
        }
    }

    // Sans objet C++ à détruire (exigence de __try).
    bool Naviguer(void* p_Navigateur, const char* p_Page, size_t p_TaillePage, const char* p_Json, size_t p_TailleJson) {
        // La requête et la chaîne de la page restent en vie après l'appel (le
        // navigateur peut les garder) : une par contrat accepté.
        auto* s_Requete = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        if (!s_Requete) return false;
        auto* s_Texte = reinterpret_cast<char*>(s_Requete + 0x800);
        memcpy(s_Texte, p_Page, std::min<size_t>(p_TaillePage, 0x7ff));

        SZStringLitterale s_Erreur { 0x80000000u, 0, "" };

        __try {
            g_ConstruireRequete(s_Requete);

            alignas(16) uint8_t s_Arguments[32] = {};
            g_Deserialiser(s_Arguments, reinterpret_cast<const uint8_t*>(p_Json), p_TailleJson, false, &s_Erreur);
            if (*reinterpret_cast<void**>(s_Arguments) == nullptr) return false; // JSON refusé

            const SZStringLitterale s_Page { static_cast<uint32_t>(p_TaillePage) | 0x80000000u, 0, s_Texte };
            memcpy(s_Requete + 0x00, &s_Page, 16);
            memcpy(s_Requete + 0x10, s_Arguments, 16);
            // Options comme dans la requête du jeu (relevé du 24/11/2026) :
            // addtohistory (« Retour » ramène à l'accueil), clearcurrentpagecontext.
            s_Requete[0x42] = 1;
            s_Requete[0x45] = 1;

            auto* s_Interface = static_cast<uint8_t*>(p_Navigateur) + 0x60;
            const auto s_Aller = (*reinterpret_cast<FnNaviguer**>(s_Interface))[5];
            s_Aller(s_Interface, s_Requete);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    void OuvrirPageEnAttente() {
        if (!g_LienEnAttente) return;
        void* s_Navigateur = g_Navigateur;
        if (!s_Navigateur || !g_ConstruireRequete || !g_Deserialiser) return; // le pilote prendra le relais
        if (!g_LienEnAttente.exchange(false)) return;

        std::string s_Page, s_Args;
        {
            std::lock_guard s_Verrou(g_VerrouLien);
            s_Page = g_Page;
            s_Args = g_Arguments;
        }

        const bool s_Ok = Naviguer(s_Navigateur, s_Page.c_str(), s_Page.size(), s_Args.c_str(), s_Args.size());
        Diag("Moteur : ouverture de la page « {} » : {}.", s_Page, s_Ok ? "demandée au jeu" : "ÉCHEC (arguments illisibles ou exception)");
    }

    // ------------------------------------------------------- Lieu de départ
    //
    // Relevé du 24/11/2026 : ZEnableEntrance::Execute (le choix d'une entrée
    // sur la page « Lieu de départ ») ne fait qu'écrire trois globales du jeu —
    // l'identifiant de l'entrée (ZRepositoryID), son « equip » et l'objet de
    // l'entrée (ZDynamicObject) — que le lancement du contrat relit ensuite.
    // Sans passer par cette page, le jeu garde la dernière entrée choisie pour
    // le lieu (essai : Santa Fortuna, départ au village au lieu des champs de
    // coca). Le mod les écrit donc lui-même, à chaque image, tant qu'elles
    // diffèrent (la page de préparation peut les réécrire en s'ouvrant).

    using FnAffecter = void*(__fastcall*)(void* p_Destination, const void* p_Source);

    uint8_t* g_GlobaleEntree = nullptr;      // ZRepositoryID, 16 octets
    uint8_t* g_GlobaleEquip = nullptr;       // ZDynamicObject
    uint8_t* g_GlobaleObjetEntree = nullptr; // ZDynamicObject
    FnAffecter g_Affecter = nullptr;

    std::mutex g_VerrouEntree;
    uint8_t g_EntreeVoulue[16] = {};
    std::string g_JsonEquip;
    std::string g_JsonEntree;
    std::atomic<bool> g_EntreeActive = false;
    std::atomic<bool> g_EntreeNouvelle = false;

    alignas(16) uint8_t g_ObjetEquip[32] = {};
    alignas(16) uint8_t g_ObjetEntree[32] = {};

    bool LireJson(uint8_t* p_Objet, const char* p_Json, size_t p_Taille) {
        SZStringLitterale s_Erreur { 0x80000000u, 0, "" };
        __try {
            memset(p_Objet, 0, 32);
            g_Deserialiser(p_Objet, reinterpret_cast<const uint8_t*>(p_Json), p_Taille, false, &s_Erreur);
            return *reinterpret_cast<void**>(p_Objet) != nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    bool EcrireEntree(const uint8_t* p_Id) {
        __try {
            memcpy(g_GlobaleEntree, p_Id, 16);
            g_Affecter(g_GlobaleEquip, g_ObjetEquip);
            g_Affecter(g_GlobaleObjetEntree, g_ObjetEntree);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    void MaintenirEntree() {
        if (!g_EntreeActive || !g_GlobaleEntree || !g_Affecter || !g_Deserialiser) return;

        static bool s_Pret = false;
        uint8_t s_Id[16];
        {
            std::lock_guard s_Verrou(g_VerrouEntree);
            memcpy(s_Id, g_EntreeVoulue, 16);
            if (g_EntreeNouvelle.exchange(false)) {
                // Les objets lus ne sont jamais libérés : le jeu en garde une
                // référence dans ses globales (une paire par contrat accepté).
                s_Pret = LireJson(g_ObjetEquip, g_JsonEquip.c_str(), g_JsonEquip.size()) &&
                         LireJson(g_ObjetEntree, g_JsonEntree.c_str(), g_JsonEntree.size());
                if (!s_Pret) Diag("Moteur : lieu de départ illisible, non imposé.");
            }
        }
        if (!s_Pret || memcmp(g_GlobaleEntree, s_Id, 16) == 0) return;

        static int s_Ecritures = 0;
        const bool s_Ok = EcrireEntree(s_Id);
        if (++s_Ecritures <= 20) Diag("Moteur : lieu de départ imposé au jeu : {}.", s_Ok ? "fait" : "ÉCHEC (exception)");
        if (!s_Ok) g_EntreeActive = false;
    }

    // Chaque lien exécuté par le jeu (thread du jeu) : on retient son
    // navigateur, et c'est l'occasion de s'abonner à chaque image (aux menus,
    // aucun événement du jeu n'arrive pour le faire).
    uint64_t __fastcall SurLien(void* p_Lien, void* a, void* b, void* c) {
        if (void* s_Navigateur = NavigateurDuLien(p_Lien)) {
            if (g_Navigateur.exchange(s_Navigateur) != s_Navigateur) Diag("Moteur : navigateur des menus retenu ({}).", s_Navigateur);
        }
        if (g_RappelImageVoulu) Moteur::AbonnerImage(g_RappelImageVoulu);

        return g_LienSuite(p_Lien, a, b, c);
    }

    bool g_Resolu = false;

    // -------------------------------------------------------- Détour
    //
    // Un seul détour, au début de ZAchievementManagerSimple::OnEventSent.
    //
    // - Remplacer l'entrée de vtable ne suffit pas (v0.3 : aucun événement reçu,
    //   l'objet appelle par une autre table).
    // - MinHook échoue (MH_ERROR_MEMORY_ALLOC, v0.3.1) : il veut une page libre à
    //   moins de 1 Go de la fonction, et le jeu occupe tout l'espace autour.
    //
    // On n'alloue donc rien : le relais (saut absolu vers notre code) et, si la
    // fonction est intacte, le tremplin (instructions d'origine + retour) sont écrits dans le
    // REMPLISSAGE de la section de code du jeu — les suites d'octets 0xCC que le
    // compilateur laisse entre deux fonctions, jamais exécutées, et toujours à
    // portée d'un saut relatif de 32 bits.
    //
    // Si la fonction commence déjà par un saut (E9 : un autre mod), on le redirige
    // vers nous et « la suite » est sa cible : jeu → Roulette → autre mod → original.

    uint8_t* TrouverRemplissage(size_t p_Taille) {
        static std::vector<uint8_t*> s_Pris;

        for (const auto& s_Zone : ZonesDeCode()) {
            auto* s_Octet = reinterpret_cast<uint8_t*>(s_Zone.m_Debut);
            auto* const s_Fin = s_Octet + s_Zone.m_Taille;

            while (s_Octet < s_Fin) {
                if (*s_Octet != 0xCC) {
                    ++s_Octet;
                    continue;
                }

                auto* s_Debut = s_Octet;
                while (s_Octet < s_Fin && *s_Octet == 0xCC) ++s_Octet;

                // Deux octets de marge de chaque côté : on ne colle jamais une fonction.
                if (static_cast<size_t>(s_Octet - s_Debut) >= p_Taille + 4) {
                    auto* s_Place = s_Debut + 2;

                    if (std::ranges::find(s_Pris, s_Place) == s_Pris.end()) {
                        s_Pris.push_back(s_Place);
                        return s_Place;
                    }
                }
            }
        }

        return nullptr;
    }

    bool Ecrire(void* p_Adresse, const void* p_Octets, size_t p_Taille) {
        DWORD s_Ancienne = 0;
        if (!VirtualProtect(p_Adresse, p_Taille, PAGE_EXECUTE_READWRITE, &s_Ancienne)) return false;
        memcpy(p_Adresse, p_Octets, p_Taille);
        VirtualProtect(p_Adresse, p_Taille, s_Ancienne, &s_Ancienne);
        FlushInstructionCache(GetCurrentProcess(), p_Adresse, p_Taille);
        return true;
    }

    // FF 25 00 00 00 00 <adresse sur 8 octets> : saut absolu, 14 octets.
    bool SautAbsolu(uint8_t* p_Ou, const void* p_Vers) {
        uint8_t s_Code[14] = { 0xFF, 0x25, 0, 0, 0, 0 };
        const auto s_Adresse = reinterpret_cast<uint64_t>(p_Vers);
        memcpy(s_Code + 6, &s_Adresse, 8);
        return Ecrire(p_Ou, s_Code, sizeof(s_Code));
    }

    std::string Octets(const uint8_t* p_Code, size_t p_Taille) {
        std::string s;
        for (size_t i = 0; i < p_Taille; ++i) s += std::format("{:02X} ", p_Code[i]);
        return s;
    }

    // Rend l'adresse à appeler pour continuer vers l'original, ou nullptr.
    void* PoserDetour(void* p_Fonction, void* p_Detour) {
        auto* s_Code = static_cast<uint8_t*>(p_Fonction);
        Diag("Moteur : détour en {} : {}", p_Fonction, Octets(s_Code, 16));

        void* s_Suite = nullptr;

        if (s_Code[0] == 0xE9) {
            // Déjà détournée (un autre mod) : la suite est la cible de son saut.
            s_Suite = s_Code + 5 + *reinterpret_cast<int32_t*>(s_Code + 1);
        }
        else {
            // Recopier assez d'instructions entières pour libérer 5 octets.
            size_t s_Longueur = 0;

            while (s_Longueur < 5) {
                hde64s s_Instr {};
                const unsigned s_Taille = hde64_disasm(s_Code + s_Longueur, &s_Instr);
                const bool s_RipRelatif = (s_Instr.flags & F_MODRM) && s_Instr.modrm_mod == 0 && s_Instr.modrm_rm == 5;

                if ((s_Instr.flags & F_ERROR) || (s_Instr.flags & F_RELATIVE) || s_RipRelatif || s_Taille == 0) {
                    Diag("Moteur : début de fonction non recopiable, détour abandonné.");
                    return nullptr;
                }

                s_Longueur += s_Taille;
            }

            auto* s_Tremplin = TrouverRemplissage(s_Longueur + 14);
            if (!s_Tremplin) {
                // Plus de remplissage assez long dans le code du jeu (23/11/2026) :
                // le tremplin finit par un saut absolu et ne recopie aucune
                // instruction relative, il peut donc vivre n'importe où.
                s_Tremplin = static_cast<uint8_t*>(VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
            }

            if (!s_Tremplin || !Ecrire(s_Tremplin, s_Code, s_Longueur) ||
                !SautAbsolu(s_Tremplin + s_Longueur, s_Code + s_Longueur)) {
                Diag("Moteur : pas de place pour le tremplin.");
                return nullptr;
            }

            s_Suite = s_Tremplin;
        }

        auto* s_Relais = TrouverRemplissage(14);

        if (!s_Relais || !SautAbsolu(s_Relais, p_Detour)) {
            Diag("Moteur : pas de place pour le relais.");
            return nullptr;
        }

        const auto s_Ecart = reinterpret_cast<intptr_t>(s_Relais) - reinterpret_cast<intptr_t>(s_Code + 5);

        if (s_Ecart > INT32_MAX || s_Ecart < INT32_MIN) {
            Diag("Moteur : relais hors de portée.");
            return nullptr;
        }

        // Le saut vers le relais remplace les 5 premiers octets, écrits d'un coup
        // avec les 3 suivants (inchangés) : le jeu ne voit jamais un saut à moitié écrit.
        uint8_t s_Nouveau[8];
        memcpy(s_Nouveau, s_Code, 8);
        s_Nouveau[0] = 0xE9;
        const auto s_Rel = static_cast<int32_t>(s_Ecart);
        memcpy(s_Nouveau + 1, &s_Rel, 4);

        DWORD s_Ancienne = 0;

        if (!VirtualProtect(s_Code, 8, PAGE_EXECUTE_READWRITE, &s_Ancienne)) {
            Diag("Moteur : code du jeu non modifiable ({}).", GetLastError());
            return nullptr;
        }

        if ((reinterpret_cast<uintptr_t>(s_Code) & 7) == 0) {
            LONG64 s_Valeur = 0;
            memcpy(&s_Valeur, s_Nouveau, 8);
            InterlockedExchange64(reinterpret_cast<volatile LONG64*>(s_Code), s_Valeur);
        }
        else {
            memcpy(s_Code, s_Nouveau, 8);
        }

        VirtualProtect(s_Code, 8, s_Ancienne, &s_Ancienne);
        FlushInstructionCache(GetCurrentProcess(), s_Code, 8);

        Diag("Moteur : détour posé.");
        Diag("Moteur : après détour : {}", Octets(s_Code, 16));
        return s_Suite;
    }
}

// ------------------------------------------------------------------------

namespace Moteur {
    bool Resoudre() {
        if (g_Resolu) {
            return true;
        }

        // Des suites d'octets de HITMAN3.exe autour d'une instruction qui
        // désigne la globale ou du début de la fonction cherchée.
        using namespace Glacier;
        bool s_Ok = true;

        s_Ok &= ChercherGlobale(Globales::BoucleDeJeu, "GameLoopManager",
            "\x48\x8D\x0D\x00\x00\x00\x00\x4C\x89\x74\x24\x48\xE8", "xxx????xxxxxx", 3);
        s_Ok &= ChercherGlobale(Globales::Personnages, "ActorManager",
            "\x4C\x8D\x15\x00\x00\x00\x00\x48\x89\xB4\x24\x40\x06\x00\x00", "xxx????xxxxxxxx", 3);
        s_Ok &= ChercherGlobale(Globales::Ressources, "ResourceManager",
            "\x48\x8D\x0D\x00\x00\x00\x00\xFF\x90\x98\x01\x00\x00", "xxx????xxxxxx", 3);
        s_Ok &= ChercherGlobale(Globales::Contenus, "ContentKitManager",
            "\x48\x89\x05\x00\x00\x00\x00\x48\x8D\x55\xA0", "xxx????xxxx", 3);
        s_Ok &= ChercherGlobale(Globales::TextesInterface, "UIText",
            "\x48\x8D\x0D\x00\x00\x00\x00\xE8\x00\x00\x00\x00\x84\xC0\x75\x00\x8B\x7C\x24\x40\x48\x8D\x55\xE0",
            "xxx????x????xxx?xxxxxxxx", 3);
        s_Ok &= ChercherGlobale(Globales::Cibles, "TargetManager",
            "\x48\x89\x05\x00\x00\x00\x00\xF3\x0F\x10\x0D", "xxx????xxxx", 3);
        s_Ok &= ChercherGlobale(Globales::Contrats, "ContractsManager",
            "\x48\x8D\x3D\x00\x00\x00\x00\x48\x8B\xCF\xE8\x00\x00\x00\x00\x48\x8B\x4B\x60\x48\x8D\x55\x20\x48\x89\x7D"
            "\x20\xE8\x00\x00\x00\x00\x66\x0F\x6F\x05\x00\x00\x00\x00\x48\x8D\x05\x00\x00\x00\x00\x45\x33\xF6",
            "xxx????xxxx????xxxxxxxxxxxxx????xxxx????xxx????xxx", 3);
        s_Ok &= ChercherGlobale(Globales::EnLigne, "OnlineManager",
            "\x48\x8D\x0D\x00\x00\x00\x00\xFF\x90\x88\x00\x00\x00\xF7\x46\x48", "xxx????xxxxxxxxx", 3);
        s_Ok &= ChercherGlobale(Globales::Memoire, "MemoryManager",
            "\x48\x89\x1D\x00\x00\x00\x00\x48\x83\xC4\x00\x5B\xC3\x48\x8D\x0D", "xxx????xxx?xxxxx", 3);
        s_Ok &= ChercherGlobale(Globales::ConteneurRessources, "ResourceContainer",
            "\x48\x8B\x05\x00\x00\x00\x00\x41\x8B\xC8\x48\xC1\xE1", "xxx????xxxxxx", 3);

        g_AjouterDelegue = reinterpret_cast<FnAjouterDelegue>(ChercherFonction("ZUpdateEventContainer::AddDelegate",
            "\x48\x89\x5C\x24\x10\x48\x89\x6C\x24\x18\x56\x57\x41\x56\x48\x83\xEC\x00\x41\x0F\xB6\xE9",
            "xxxxxxxxxxxxxxxxx?xxxx"));
        g_Traduire = reinterpret_cast<FnTraduire>(ChercherFonction("ZUIText::TryGetTextFromNameHash",
            "\x48\x89\x5C\x24\x08\x48\x89\x6C\x24\x10\x48\x89\x74\x24\x18\x48\x89\x7C\x24\x20\x41\x54\x41\x56\x41\x57"
            "\x48\x83\xEC\x00\x48\x8D\x59\x28",
            "xxxxxxxxxxxxxxxxxxxxxxxxxxxxx?xxxx"));
        g_LibererTexte = reinterpret_cast<FnLibererTexte>(ChercherFonction("ZString::ZImpl::Free",
            "\x48\x89\x5C\x24\x18\x48\x89\x6C\x24\x20\x56\x48\x83\xEC\x00\x48\x8B\xF1\xE8",
            "xxxxxxxxxxxxxx?xxxx"));

        // La vtable de ZAchievementManagerSimple (OnEventSent est sa 8e entrée).
        {
            const auto s_Cible = ChercherBrut(
                reinterpret_cast<const uint8_t*>("\x48\x8D\x05\x00\x00\x00\x00\x48\x89\x07\x48\x8D\x55\x97"),
                "xxx????xxxxxxx"
            );

            if (s_Cible) {
                g_VtableSucces = reinterpret_cast<void**>(s_Cible + 7 + *reinterpret_cast<const int32_t*>(s_Cible + 3));
                Diag("Moteur : vtable des succès trouvée.");
            }
            else {
                Diag("Moteur : vtable des succès INTROUVABLE (jeu mis à jour ?).");
            }
        }

        g_FonctionPage = reinterpret_cast<void*>(ChercherFonction("ZDynamicPageController::Expand",
            "\x40\x53\x56\x57\x41\x54\x41\x57\x48\x81\xEC\x00\x00\x00\x00\x48\x8D\x05", "xxxxxxxxxxx????xxx"));

        g_FonctionReponse = reinterpret_cast<void*>(ChercherFonction("ZHttpResultDynamicObject::OnBufferReady",
            "\x48\x89\x5C\x24\x08\x48\x89\x74\x24\x10\x57\x48\x83\xEC\x00\x80\x79\x50\x00\x48\x8B\xD9",
            "xxxxxxxxxxxxxx?xxx?xxx"));

        // ZLink@JSONTemplate::Execute : motif de 28 octets (unique dans le jeu du 24/11/2026).
        g_FonctionLien = reinterpret_cast<void*>(ChercherFonction("ZLink::Execute",
            "\x48\x89\x5C\x24\x18\x55\x48\x8D\x6C\x24\xA9\x48\x81\xEC\xE0\x00\x00\x00\x48\x8B\xD9\x48\x8B\x49\x28"
            "\x48\x85\xC9",
            "xxxxxxxxxxxxxxxxxxxxxxxxxxxx"));

        // Les globales du lieu de départ, lues dans ZEnableEntrance::Execute
        // (motif unique dans le jeu du 24/11/2026).
        {
            const auto s_Code = ChercherBrut(
                reinterpret_cast<const uint8_t*>(
                    "\x48\x8B\x08\x48\x89\x0D\x00\x00\x00\x00\x48\x8B\xCB\x48\x8B\x40\x08\x48\x89\x05\x00\x00\x00\x00"
                    "\x48\x8B\x03\xFF\x50\x58\x48\x8B\xD0\x48\x8D\x0D\x00\x00\x00\x00\xE8\x00\x00\x00\x00"
                    "\x48\x8B\x03\x48\x8B\xCB\xFF\x50\x70\x48\x8B\xD0\x48\x8D\x0D"),
                "xxxxxx????xxxxxxxxxx????xxxxxxxxxxxx????x????xxxxxxxxxxxxxxx");
            if (s_Code) {
                const auto Cible = [&](size_t p_Deplacement, size_t p_Suivante) {
                    return reinterpret_cast<uint8_t*>(s_Code + p_Suivante + *reinterpret_cast<const int32_t*>(s_Code + p_Deplacement));
                };
                g_GlobaleEntree = Cible(6, 10);
                g_GlobaleEquip = Cible(36, 40);
                g_Affecter = reinterpret_cast<FnAffecter>(Cible(41, 45));
                g_GlobaleObjetEntree = Cible(60, 64);
                Diag("Moteur : globales du lieu de départ trouvées ({}, {}, {}).",
                     static_cast<void*>(g_GlobaleEntree), static_cast<void*>(g_GlobaleEquip), static_cast<void*>(g_GlobaleObjetEntree));
            }
            else {
                Diag("Moteur : globales du lieu de départ INTROUVABLES (jeu mis à jour ?).");
            }
        }

        // Constructeur de la requête de page (motif unique dans le jeu du 24/11/2026).
        g_ConstruireRequete = reinterpret_cast<FnConstruireRequete>(ChercherFonction("SPageRequest::SPageRequest",
            "\x48\x83\xEC\x18\x33\xD2\xC7\x01\x00\x00\x00\x80\x48\x89\x14\x24\x48\x8D\x05",
            "xxxxxxxxxxxxxxxxxxx"));

        // Le lecteur JSON du jeu : la cible de l'appel que fait OnBufferReady.
        {
            const auto s_Appel = ChercherBrut(
                reinterpret_cast<const uint8_t*>("\x44\x8B\xC1\x48\x8D\x50\x08\x48\x8D\x44\x24\x30\x48\x0F\x44\xD6"
                                                 "\x48\x8D\x4C\x24\x40\x48\x89\x44\x24\x20\xE8"),
                "xxxxxxxxxxxxxxxxxxxxxxxxxxx");
            if (s_Appel) {
                const auto s_E8 = s_Appel + 26;
                g_Deserialiser = reinterpret_cast<FnDeserialiser>(s_E8 + 5 + *reinterpret_cast<const int32_t*>(s_E8 + 1));
                Diag("Moteur : lecteur JSON du jeu trouvé ({}).", reinterpret_cast<void*>(g_Deserialiser));
            }
            else {
                Diag("Moteur : lecteur JSON du jeu INTROUVABLE (jeu mis à jour ?).");
            }
        }

        // Le démarrage d'un contrat : la cible de l'appel que fait l'action
        // « start-contract » (son début de fonction est trop banal pour un motif).
        {
            const auto s_Appel = ChercherBrut(
                reinterpret_cast<const uint8_t*>("\x48\x8D\x8E\xF8\x00\x00\x00\x48\x89\x9D\xD8\x01\x00\x00\x4C\x8D\x8D\xA0\x01\x00\x00"
                                                 "\x48\x89\x5D\x18\x4C\x8D\x45\xE0\x48\x8D\x55\x90\xE8"),
                "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx");
            if (s_Appel) {
                const auto s_E8 = s_Appel + 33;
                g_Demarrer = reinterpret_cast<FnDemarrer>(s_E8 + 5 + *reinterpret_cast<const int32_t*>(s_E8 + 1));
                Diag("Moteur : démarrage de contrat trouvé ({}).", reinterpret_cast<void*>(g_Demarrer));
            }
            else {
                Diag("Moteur : démarrage de contrat INTROUVABLE (jeu mis à jour ?).");
            }
        }

        s_Ok &= g_AjouterDelegue && g_Traduire && g_VtableSucces;
        g_Resolu = s_Ok;

        Diag("Moteur : {}.", s_Ok ? "prêt" : "INCOMPLET");
        return s_Ok;
    }

    bool CapterEvenements(void (*p_Rappel)(const Glacier::Objet&)) {
        if (!g_VtableSucces || g_EvenementOriginal) {
            return g_EvenementOriginal != nullptr;
        }

        g_RappelEvenement = p_Rappel;

        void* s_Fonction = g_VtableSucces[7];
        void* s_Suite = PoserDetour(s_Fonction, reinterpret_cast<void*>(&SurEvenement));

        if (!s_Suite) {
            return false;
        }

        g_EvenementOriginal = reinterpret_cast<FnEvenement>(s_Suite);
        return true;
    }

    bool CapterPages(void (*p_Rappel)(const Glacier::Objet&)) {
        if (!g_FonctionPage || g_PageOriginale) {
            return g_PageOriginale != nullptr;
        }

        g_RappelPage = p_Rappel;
        void* s_Suite = PoserDetour(g_FonctionPage, reinterpret_cast<void*>(&SurPage));

        if (!s_Suite) {
            return false;
        }

        g_PageOriginale = reinterpret_cast<FnPage>(s_Suite);
        return true;
    }

    bool PeutOuvrirPage() {
        if (!g_FonctionLien || !g_Deserialiser || !g_ConstruireRequete) return false;
        if (g_LienSuite) return true;

        void* s_Suite = PoserDetour(g_FonctionLien, reinterpret_cast<void*>(&SurLien));
        if (!s_Suite) {
            g_FonctionLien = nullptr; // ne pas réessayer
            return false;
        }
        g_LienSuite = reinterpret_cast<FnLien>(s_Suite);
        return true;
    }

    void ImposerEntree(const std::string& p_IdEntree, const std::string& p_JsonEquip, const std::string& p_JsonEntree) {
        // ZRepositoryID : disposition d'un GUID (data1, data2, data3, data4[8]).
        uint32_t d1 = 0;
        unsigned d2 = 0, d3 = 0, d4[8] = {};
        if (sscanf_s(p_IdEntree.c_str(), "%8x-%4x-%4x-%2x%2x-%2x%2x%2x%2x%2x%2x",
                     &d1, &d2, &d3, &d4[0], &d4[1], &d4[2], &d4[3], &d4[4], &d4[5], &d4[6], &d4[7]) != 11) {
            Diag("Moteur : identifiant d'entrée illisible ({}).", p_IdEntree);
            return;
        }

        std::lock_guard s_Verrou(g_VerrouEntree);
        memcpy(g_EntreeVoulue, &d1, 4);
        const uint16_t s_D2 = static_cast<uint16_t>(d2), s_D3 = static_cast<uint16_t>(d3);
        memcpy(g_EntreeVoulue + 4, &s_D2, 2);
        memcpy(g_EntreeVoulue + 6, &s_D3, 2);
        for (int i = 0; i < 8; ++i) g_EntreeVoulue[8 + i] = static_cast<uint8_t>(d4[i]);
        g_JsonEquip = p_JsonEquip;
        g_JsonEntree = p_JsonEntree;
        g_EntreeNouvelle = true;
        g_EntreeActive = true;
    }

    void LibererEntree() {
        g_EntreeActive = false;
    }

    bool NavigateurConnu() {
        return g_Navigateur != nullptr;
    }

    void OublierNavigateur() {
        g_Navigateur = nullptr;
    }

    void PreparerImage(void (*p_Rappel)()) {
        g_RappelImageVoulu = p_Rappel;
    }

    void DemanderPage(const std::string& p_Page, const std::string& p_JsonArguments) {
        {
            std::lock_guard s_Verrou(g_VerrouLien);
            g_Page = p_Page;
            g_Arguments = p_JsonArguments;
        }
        g_LienEnAttente = true;
    }

    bool PageEnAttente() {
        return g_LienEnAttente;
    }

    void AnnulerPage() {
        g_LienEnAttente = false;
    }

    bool PeutDemarrerContrat() {
        return g_Demarrer != nullptr;
    }

    bool DemarrerContrat(const std::string& p_IdContrat) {
        if (!g_Demarrer || p_IdContrat.empty()) return false;
        g_BlocDrapeaux[8] = 0;
        return AppelerDemarrer(p_IdContrat.c_str(), static_cast<uint32_t>(p_IdContrat.size()));
    }

    bool CapterReponses(bool (*p_Rappel)(const uint8_t*, size_t, std::string&)) {
        if (!g_FonctionReponse || g_ReponseOriginale) {
            return g_ReponseOriginale != nullptr;
        }

        g_RappelReponse = p_Rappel;
        void* s_Suite = PoserDetour(g_FonctionReponse, reinterpret_cast<void*>(&SurReponse));

        if (!s_Suite) {
            return false;
        }

        g_ReponseOriginale = reinterpret_cast<FnReponse>(s_Suite);
        return true;
    }

    bool AbonnerImage(void (*p_Rappel)()) {
        if (g_RappelImage) {
            return true;
        }

        void* s_Conteneur = Glacier::ConteneurImage();

        if (!g_AjouterDelegue || !s_Conteneur) {
            return false;
        }

        g_RappelImage = p_Rappel;

        static const SDelegue s_Delegue { nullptr, nullptr, &SurImage, nullptr };
        g_AjouterDelegue(s_Conteneur, s_Delegue, 1, 2);

        Diag("Moteur : abonné à chaque image.");
        return true;
    }

    std::string Traduire(const std::string& p_Cle) {
        if (!g_Traduire || !Glacier::Globales::TextesInterface || p_Cle.empty()) {
            return {};
        }

        // Le jeu range ses textes sous le CRC32 de la clé en capitales.
        std::string s_Majuscules = p_Cle;

        for (auto& c : s_Majuscules) {
            c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
        }

        for (const std::string* s_Essai : { static_cast<const std::string*>(&s_Majuscules), &p_Cle }) {
            Glacier::Chaine s_Resultat;
            int32_t s_Balisage = 0;
            const auto s_Hachage = static_cast<int32_t>(Glacier::Crc32(*s_Essai));

            if (!g_Traduire(Glacier::Globales::TextesInterface, s_Hachage, s_Resultat, s_Balisage)) continue;

            std::string s_Texte = Glacier::VersTexte(s_Resultat);

            // Le jeu a pu allouer la chaîne rendue : on la lui rend (son en-tête
            // précède les caractères de 16 octets).
            if (s_Resultat.Possede() && g_LibererTexte) {
                g_LibererTexte(const_cast<char*>(s_Resultat.m_pCaracteres) - 16);
            }

            if (!s_Texte.empty()) return s_Texte;
        }

        return {};
    }

    HWND FenetreDuJeu() {
        struct SRecherche {
            DWORD m_Processus;
            HWND m_Fenetre = nullptr;
            LONG m_Aire = 0;
        } s_Recherche { GetCurrentProcessId() };

        EnumWindows([](HWND p_Fenetre, LPARAM p_Contexte) -> BOOL {
            auto* s_R = reinterpret_cast<SRecherche*>(p_Contexte);
            DWORD s_Processus = 0;
            GetWindowThreadProcessId(p_Fenetre, &s_Processus);

            if (s_Processus != s_R->m_Processus || !IsWindowVisible(p_Fenetre)) {
                return TRUE;
            }

            // Pas nos propres fenêtres.
            wchar_t s_Classe[64] = {};
            GetClassNameW(p_Fenetre, s_Classe, 64);
            if (wcsncmp(s_Classe, L"Roulette", 8) == 0) return TRUE;

            RECT s_Rect {};
            GetClientRect(p_Fenetre, &s_Rect);
            const LONG s_Aire = (s_Rect.right - s_Rect.left) * (s_Rect.bottom - s_Rect.top);

            if (s_Aire > s_R->m_Aire) {
                s_R->m_Aire = s_Aire;
                s_R->m_Fenetre = p_Fenetre;
            }

            return TRUE;
        }, reinterpret_cast<LPARAM>(&s_Recherche));

        return s_Recherche.m_Fenetre;
    }
}
