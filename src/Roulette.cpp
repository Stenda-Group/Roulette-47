#include "Roulette.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <thread>
#include <unordered_map>

#include <Windows.h>

#include "Diagnostic.h"
#include "Glacier.h"
#include "Langue.h"
#include "CodeContrat.h"
#include "Contrat.h"
#include "Http.h"
#include "Json.h"
#include "Moteur.h"

namespace {
    // L'application Discord de Roulette 47 (portail des développeurs de
    // Discord) ; « discord_app » de roulette47.ini la remplace. Vide : pas de
    // présence tant qu'aucune n'est réglée.
    constexpr const char* c_IdDiscordParDefaut = "1575174401912082493";

    // p_Json[a][b]… s'il existe, sinon nullptr (rien n'est créé dans la page).
    nlohmann::json* NoeudJson(nlohmann::json& p_Json, std::initializer_list<const char*> p_Cles) {
        nlohmann::json* s_Noeud = &p_Json;
        for (const auto* k : p_Cles) {
            if (!s_Noeud->is_object()) return nullptr;
            const auto s_It = s_Noeud->find(k);
            if (s_It == s_Noeud->end()) return nullptr;
            s_Noeud = &*s_It;
        }
        return s_Noeud;
    }

    double Maintenant() {
        using namespace std::chrono;
        return duration<double>(steady_clock::now().time_since_epoch()).count();
    }

    std::wstring Large(const std::string& p_Texte) {
        return std::filesystem::path(std::u8string(p_Texte.begin(), p_Texte.end())).wstring();
    }

    const char* EtatTexte(EEtatObjectif p_Etat) {
        switch (p_Etat) {
            case EEtatObjectif::Reussi: return "reussi";
            case EEtatObjectif::Echoue: return "echoue";
            default: return "en_cours";
        }
    }

    bool Contient(const std::string& p_Texte, std::initializer_list<const char*> p_Mots) {
        for (const auto* s_Mot : p_Mots) {
            if (p_Texte.find(s_Mot) != std::string::npos) return true;
        }
        return false;
    }

    // Le nom de travail d'une fiche du dépôt, rendu présentable quand le jeu n'a
    // pas (encore) donné le sien : « (S3) Firearms_pistol_Krugermeier Dark »
    // → « Krugermeier Dark », « (Pistol) ICA 19 Short Baller » → « ICA 19 Short Baller ».
    std::string NomNettoye(std::string p_Nom) {
        // Les parenthèses de tête : « (Pistol) », « (S3) », « (Pre-order) »…
        for (;;) {
            while (!p_Nom.empty() && p_Nom.front() == ' ') p_Nom.erase(0, 1);
            if (p_Nom.empty() || p_Nom.front() != '(') break;
            const auto s_Fin = p_Nom.find(')');
            if (s_Fin == std::string::npos) break;
            p_Nom.erase(0, s_Fin + 1);
        }
        // Les préfixes techniques.
        for (const char* s_Prefixe : { "TOKEN_", "Firearms_Hero_Pistol_", "Firearms_pistol_", "Firearms_", "Firearms ",
                                       "Melee_", "Tool_", "PROP_", "Prop_" }) {
            const std::string s_P = s_Prefixe;
            if (p_Nom.size() > s_P.size() && _strnicmp(p_Nom.c_str(), s_P.c_str(), s_P.size()) == 0) p_Nom.erase(0, s_P.size());
        }
        for (auto& c : p_Nom) if (c == '_') c = ' ';
        std::string s_Propre;
        for (const char c : p_Nom) {
            if (c == ' ' && (s_Propre.empty() || s_Propre.back() == ' ')) continue;
            s_Propre += c;
        }
        while (!s_Propre.empty() && s_Propre.back() == ' ') s_Propre.pop_back();
        return s_Propre;
    }

    // Le costume de départ (Julien, 25/11/2026 : « on ne peut pas choisir nous
    // même la tenue de 47 dans le menu de préparation ? Si c'est impossible,
    // mets-le toujours en costume noir classique »).
    //  - vrai : l'écran « Préparation » du jeu reste ouvert, réduit au seul
    //    emplacement du costume (présélectionné : le costume classique) ;
    //    arme, objets et entrée sont imposés par le contrat de la mission ;
    //  - faux : l'écran est retiré (MandatoryLoadout sur la page) et 47 part
    //    toujours dans le costume classique.
    constexpr bool c_CostumeAuMenu = true;
    constexpr const char* c_CostumeClassique = "TOKEN_OUTFIT_HITMANSUIT";

    // Le nom d'un costume de 47 quand le jeu n'a pas chargé son texte (aux
    // menus, ceux des costumes ne le sont jamais : essai du 25/11/2026), tiré
    // du nom de travail du dépôt : « CHAR_Agent47_Hero_Summer_Suit_M » →
    // « Summer Suit », « CHAR_EG04_Reward_Hero_DLC_C_M » → « EG04 DLC C ».
    std::string NomCostume(const std::string& p_Brut) {
        static const char* c_Ignores[] = { "char", "agent47", "hero", "reward", "token", "outfit", "m", "f" };
        std::string s_Nom, s_Mot;
        const auto Finir = [&] {
            if (s_Mot.empty()) return;
            std::string s_Bas = s_Mot;
            for (auto& c : s_Bas) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
            const bool s_Ignore = std::ranges::any_of(c_Ignores, [&](const char* x) { return s_Bas == x; });
            // Numéros de fabrication : PRO183034, HPA2793.
            const bool s_Fabrication = (s_Bas.rfind("pro", 0) == 0 || s_Bas.rfind("hpa", 0) == 0) && s_Bas.size() > 3 &&
                                       std::all_of(s_Bas.begin() + 3, s_Bas.end(), [](char c) { return c >= '0' && c <= '9'; });
            if (!s_Ignore && !s_Fabrication) {
                const bool s_Sigle = s_Mot.size() <= 4 && std::ranges::any_of(s_Mot, [](char c) { return c >= '0' && c <= '9'; });
                std::string s_Propre = s_Sigle || (s_Mot.size() <= 3 && std::ranges::all_of(s_Mot, [](char c) { return c >= 'A' && c <= 'Z'; }))
                                           ? s_Mot : s_Bas;
                if (!s_Sigle) s_Propre[0] = static_cast<char>(toupper(static_cast<unsigned char>(s_Propre[0])));
                s_Nom += (s_Nom.empty() ? "" : " ") + s_Propre;
            }
            s_Mot.clear();
        };
        for (const char c : p_Brut) {
            if (c == '_' || c == ' ') Finir();
            else s_Mot += c;
        }
        Finir();
        return s_Nom.empty() ? NomNettoye(p_Brut) : s_Nom;
    }

    // Dernier recours en français pour l'équipement de 47, dont le nom du jeu
    // n'est pas encore connu.
    const char* NomGenerique(const std::string& p_IdJeu) {
        if (p_IdJeu.find("FIBER_WIRE") != std::string::npos || p_IdJeu.find("FIBERWIRE") != std::string::npos) return "Corde de piano";
        if (p_IdJeu.find("LOCK_PICK") != std::string::npos) return "Crochet";
        if (p_IdJeu.find("PROP_TOOL_COIN") != std::string::npos) return "Pièces";
        return nullptr;
    }

    // Le nom d'une fiche du dépôt dans la langue du jeu : la clé traduite par le
    // jeu, sinon le nom lisible, sinon le premier repli non vide.
    std::string NomLisible(const EntreeDepot* p_Entree, std::initializer_list<const std::string*> p_Replis) {
        if (p_Entree && EstUneCle(p_Entree->m_Cle)) {
            if (auto s_Traduit = Moteur::Traduire(p_Entree->m_Cle); !s_Traduit.empty()) return s_Traduit;
        }

        if (p_Entree && !EstUneCle(p_Entree->m_Nom)) return p_Entree->m_Nom;

        for (const auto* s_Repli : p_Replis) {
            if (!s_Repli->empty() && !EstUneCle(*s_Repli)) return *s_Repli;
        }

        return p_Entree ? p_Entree->m_Nom : std::string();
    }
}

Roulette& Roulette::Instance() {
    static Roulette s_Instance;
    return s_Instance;
}

Roulette::Roulette() :
    m_Hasard(std::random_device {}()) {}

// ------------------------------------------------------------ Démarrage

// Le mod s'appelait « Roulette » (Retail\Roulette, roulette.ini) jusqu'à la
// v0.12 ; il est devenu « Roulette 47 » (Julien, 24/11/2026). Au premier
// démarrage dans Retail\Roulette47, ce que l'ancien dossier retenait est
// repris : réglages, destinations, noms appris, PNJ relevés lieu par lieu. Les
// images se retéléchargent d'elles-mêmes. L'ancien dossier n'est pas touché.
void Roulette::ReprendreAncienDossier() {
    namespace fs = std::filesystem;
    std::error_code e;
    const fs::path s_Ici = DossierDuMod();
    const fs::path s_Ancien = s_Ici.parent_path() / "Roulette";
    if (!fs::is_directory(s_Ancien, e) || fs::equivalent(s_Ancien, s_Ici, e)) return;

    int n = 0;
    const auto Copier = [&](const fs::path& p_De, const fs::path& p_Vers) {
        if (fs::exists(p_Vers, e) || !fs::exists(p_De, e)) return;
        fs::create_directories(p_Vers.parent_path(), e);
        if (fs::copy_file(p_De, p_Vers, e)) ++n;
    };
    Copier(s_Ancien / "roulette.ini", s_Ici / "roulette47.ini");
    Copier(s_Ancien / "destinations.json", s_Ici / "destinations.json");
    Copier(s_Ancien / "noms.json", s_Ici / "names.json");
    if (fs::is_directory(s_Ancien / "lieux", e)) {
        for (const auto& f : fs::directory_iterator(s_Ancien / "lieux", e)) Copier(f.path(), s_Ici / "locations" / f.path().filename());
    }
    if (n) Diag("Reprise de l'ancien dossier Retail\\Roulette : {} fichier(s).", n);
}

// Noms des fichiers en anglais (v0.20, Julien : le dossier du mod est vu par
// tous les joueurs). Ce qu'une version précédente a écrit sous l'ancien nom est
// repris : lieux/ -> locations/, noms.json -> names.json. Les fichiers livrés
// sous l'ancien nom (lieux_initiaux/, ui/textes.js, ui/maquette.js) et les
// journaux de développement (reponses/, evenements.log) sont retirés.
void Roulette::RenommerAnciensFichiers() {
    namespace fs = std::filesystem;
    std::error_code e;
    const fs::path s_Ici = DossierDuMod();
    int n = 0;

    if (fs::is_directory(s_Ici / "lieux", e)) {
        fs::create_directories(s_Ici / "locations", e);
        for (const auto& f : fs::directory_iterator(s_Ici / "lieux", e)) {
            const auto s_Vers = s_Ici / "locations" / f.path().filename();
            if (!fs::exists(s_Vers, e)) { fs::rename(f.path(), s_Vers, e); if (!e) ++n; }
        }
        fs::remove_all(s_Ici / "lieux", e);
    }
    if (fs::exists(s_Ici / "noms.json", e)) {
        if (!fs::exists(s_Ici / "names.json", e)) { fs::rename(s_Ici / "noms.json", s_Ici / "names.json", e); if (!e) ++n; }
        fs::remove(s_Ici / "noms.json", e);
    }
    for (const char* s_Ancien : { "lieux_initiaux", "reponses", "ui/textes.js", "ui/maquette.js", "evenements.log" }) {
        if (fs::exists(s_Ici / s_Ancien, e)) { fs::remove_all(s_Ici / s_Ancien, e); ++n; }
    }
    if (n) Diag("Anciens noms de fichiers repris ou retirés : {}.", n);
}

void Roulette::Demarrer() {
    Diag("Roulette 47 v0.22 (autonome) dans {}.", DossierDuMod());
    ReprendreAncienDossier();
    RenommerAnciensFichiers();

    if (!Moteur::Resoudre()) {
        Diag("Le mod reste inactif : une partie du jeu n'a pas été reconnue.");
        return;
    }

    // On attend que le moteur tourne (boucle de jeu montée) : à ce moment, un
    // éventuel autre mod a fini de poser ses détours depuis longtemps.
    for (int i = 0; i < 1200; ++i) {
        if (Glacier::ConteneurImage() && Moteur::FenetreDuJeu()) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }

    std::this_thread::sleep_for(std::chrono::seconds(2));

    ChargerReglages();
    ChargerDestinations();
    ChargerNoms();
    ChargerEntreesReperees();

    m_Images = new Images();
    m_Images->QuandArrivee([this] {
        ++m_ImagesArrivees;
        m_Fenetre.Pousser();
    });

    // Journal des événements, pour caler les valeurs du jeu (« log_events=1 »
    // du .ini : coupé chez les joueurs, et celui d'une version précédente effacé).
    // Repart à zéro au-delà de 2 Mo.
    const auto s_Chemin = std::filesystem::path(DossierDuMod()) / "events.log";
    std::error_code s_Erreur;
    if (m_JournalEvenements) {
        const bool s_Trop = std::filesystem::exists(s_Chemin) && std::filesystem::file_size(s_Chemin, s_Erreur) > 2'000'000;
        m_Journal.open(s_Chemin, s_Trop ? std::ios::trunc : std::ios::app);
    }
    else {
        std::filesystem::remove(s_Chemin, s_Erreur);
    }

    Fenetre::SParametres s_Parametres;
    s_Parametres.m_DossierUi = Large(DossierDuMod() + "\\ui");
    s_Parametres.m_DossierCache = Large(DossierDuMod() + "\\cache");
    s_Parametres.m_DossierDonnees = Large(DossierDuMod() + "\\webview");
    s_Parametres.m_Touche = m_Touche;
    s_Parametres.m_OutilsDeveloppement = m_OutilsDeveloppement;
    s_Parametres.m_FenetreDuJeu = [] { return Moteur::FenetreDuJeu(); };
    s_Parametres.m_Etat = [this](bool p_Ouverte) { return EtatJson(p_Ouverte); };
    s_Parametres.m_Message = [this](const std::string& p_Json) { MessagePage(p_Json); };
    m_Fenetre.Demarrer(std::move(s_Parametres));

    Moteur::CapterEvenements(&Roulette::SurEvenement);

    // (Le relevé des pages de menu par ZDynamicPageController::Expand est
    // retiré : il n'a jamais rien reçu, tout passe par les réponses du serveur.)

    // Réponses du serveur (pages des menus, contrat) : c'est d'elles que
    // viennent la destination tirée au sort et l'échec « vanilla ». Leur relevé
    // sur le disque n'a lieu qu'avec « dump_responses=1 » ; le dossier est vidé à chaque
    // lancement dans tous les cas (relevés d'une version précédente compris).
    std::filesystem::remove_all(std::filesystem::path(DossierDuMod()) / "responses", s_Erreur);
    Moteur::CapterReponses(&Roulette::SurReponse);

    // Ouverture directe de la préparation à l'acceptation (détour posé ici, une fois).
    Moteur::PreparerImage(&Roulette::SurImage);
    Diag("Ouverture directe des pages : {}.", Moteur::PeutOuvrirPage() ? "prête" : "impossible (pilote seul)");

    if (m_DiscordActif && !m_IdDiscord.empty()) m_Discord.Demarrer(m_IdDiscord);
    Diag("Prêt. Touche de la fiche : 0x{:02X}. Discord : {}.", m_Touche,
         !m_DiscordActif ? "coupé" : m_IdDiscord.empty() ? "aucune application (discord_app)" : "actif");
}

// ------------------------------------------------------------ Réglages

// Clés de roulette47.ini en anglais (v0.20, Julien : le fichier est lu par les
// joueurs). Une clé française d'une version précédente est recopiée sous son
// nouveau nom, puis effacée ; une clé déjà présente sous le nouveau nom gagne.
void Roulette::MigrerReglages(const std::wstring& p_Fichier) {
    static const std::pair<const wchar_t*, const wchar_t*> c_Cles[] = {
        { L"imposer_methode", L"enforce_method" },
        { L"imposer_tenue", L"enforce_outfit" },
        { L"costume", L"allow_suit" },
        { L"tirage_sans_contrat", L"draw_without_contract" },
        { L"pnj", L"npc_targets" },
        { L"echec_impose", L"enforce_failure" },
        { L"imposer_preparation", L"lock_planning" },
        { L"cibles_du_jeu", L"mission_targets_only" },
        { L"serie_puriste", L"purist_streak" },
        { L"destinations_exclues", L"excluded_destinations" },
        { L"cibles_min", L"targets_min" },
        { L"cibles_max", L"targets_max" },
        { L"remplacer_objectifs", L"replace_objectives" },
        { L"destination_auto", L"auto_destination" },
        { L"sons", L"sounds" },
        { L"pilote", L"menu_autopilot" },
        { L"encart", L"mission_overlay" },
        { L"langue", L"language" },
        { L"touche", L"key" },
        { L"outils_developpement", L"dev_tools" },
        { L"releve", L"dump_responses" },
        { L"journal_evenements", L"log_events" },
    };
    int n = 0;
    const auto Reprendre = [&](const std::wstring& p_Ancienne, const std::wstring& p_Nouvelle) {
        wchar_t s_Valeur[4096] = {};
        GetPrivateProfileStringW(L"roulette", p_Ancienne.c_str(), L"\x01", s_Valeur, 4096, p_Fichier.c_str());
        if (std::wstring(s_Valeur) == L"\x01") return; // absente
        wchar_t s_Deja[8] = {};
        GetPrivateProfileStringW(L"roulette", p_Nouvelle.c_str(), L"\x01", s_Deja, 8, p_Fichier.c_str());
        if (std::wstring(s_Deja) == L"\x01") WritePrivateProfileStringW(L"roulette", p_Nouvelle.c_str(), s_Valeur, p_Fichier.c_str());
        WritePrivateProfileStringW(L"roulette", p_Ancienne.c_str(), nullptr, p_Fichier.c_str()); // efface
        ++n;
    };
    for (const auto& [s_Ancienne, s_Nouvelle] : c_Cles) Reprendre(s_Ancienne, s_Nouvelle);
    for (int i = 0; i < 16; ++i) Reprendre(L"famille_" + std::to_wstring(i), L"method_family_" + std::to_wstring(i));
    // Clés retirées par les versions précédentes.
    for (const wchar_t* s_Retiree : { L"costume_depart", L"tirage_auto" }) {
        WritePrivateProfileStringW(L"roulette", s_Retiree, nullptr, p_Fichier.c_str());
    }
    if (n) Diag("roulette47.ini : {} clé(s) reprise(s) sous leur nom anglais.", n);
}

void Roulette::ChargerReglages() {
    const auto s_Fichier = Large(DossierDuMod() + "\\roulette47.ini");
    MigrerReglages(s_Fichier);
    auto Lire = [&](const wchar_t* p_Cle, int p_Defaut) {
        return static_cast<int>(GetPrivateProfileIntW(L"roulette", p_Cle, p_Defaut, s_Fichier.c_str()));
    };

    std::lock_guard s_Verrou(m_Verrou);

    m_Reglages.m_Mode = static_cast<EMode>(std::clamp(Lire(L"mode", 0), 0, 2));
    m_Reglages.m_ImposerMethode = Lire(L"enforce_method", 1) != 0;
    m_Reglages.m_ImposerTenue = Lire(L"enforce_outfit", 1) != 0;
    m_Reglages.m_AutoriserCostume = Lire(L"allow_suit", 1) != 0;
    m_Reglages.m_NbComplications = std::clamp(Lire(L"complications", 3), 0, 3);
    // Plus aucun tirage tout seul (Julien, 23/11/2026 : « c'est à moi de
    // décider si je veux jouer avec la roulette »). Nouvelle clé : l'ancienne
    // « tirage_auto » valait 1 chez lui.
    m_Reglages.m_TirageAuto = Lire(L"draw_without_contract", 0) != 0;
    m_Reglages.m_NbPnj = std::clamp(Lire(L"npc_targets", 1), 0, 3);
    m_Reglages.m_EchecImpose = Lire(L"enforce_failure", 1) != 0;
    m_Reglages.m_ImposerPreparation = Lire(L"lock_planning", 1) != 0;
    m_Reglages.m_CiblesDuJeuSeules = Lire(L"mission_targets_only", 0) != 0;
    m_SeriePuriste = std::max(0, Lire(L"purist_streak", 0));
    m_DiscordActif = Lire(L"discord", 1) != 0;
    {
        wchar_t s_Liste[4096] = {};
        GetPrivateProfileStringW(L"roulette", L"excluded_destinations", L"", s_Liste, 4096, s_Fichier.c_str());
        std::string s_Texte;
        for (const wchar_t* c = s_Liste; *c; ++c) s_Texte += static_cast<char>(*c < 128 ? *c : '?');
        m_DestinationsExclues.clear();
        size_t s_Debut = 0;
        while (s_Debut <= s_Texte.size()) {
            const size_t s_Fin = std::min(s_Texte.find(',', s_Debut), s_Texte.size());
            if (s_Fin > s_Debut) m_DestinationsExclues.insert(s_Texte.substr(s_Debut, s_Fin - s_Debut));
            s_Debut = s_Fin + 1;
        }
    }
    m_Reglages.m_CiblesMin = std::clamp(Lire(L"targets_min", 1), 1, 3);
    m_Reglages.m_CiblesMax = std::clamp(Lire(L"targets_max", 3), m_Reglages.m_CiblesMin, 3);
    m_Reglages.m_RemplacerObjectifs = Lire(L"replace_objectives", 1) != 0;
    m_Reglages.m_DestinationAuto = Lire(L"auto_destination", 1) != 0;
    m_Reglages.m_Sons = Lire(L"sounds", 1) != 0;
    m_Reglages.m_Pilote = Lire(L"menu_autopilot", 1) != 0;

    for (size_t i = 0; i < FamillesMethodes().size(); ++i) {
        m_Reglages.m_Familles[i] = Lire((L"method_family_" + std::to_wstring(i)).c_str(), 1) != 0;
    }

    m_Encart = Lire(L"mission_overlay", 1) != 0;

    // La langue : imposée par « language= » (fr, en, it, es, ru), sinon celle de
    // Steam pour le jeu, sinon celle de Windows ; le jeu la confirme ensuite
    // dès que ses textes sont chargés (DetecterLangueDuJeu).
    {
        wchar_t s_Code[16] = {};
        GetPrivateProfileStringW(L"roulette", L"language", L"", s_Code, 16, s_Fichier.c_str());
        std::string s_Texte;
        for (const wchar_t* c = s_Code; *c; ++c) s_Texte += static_cast<char>(*c);
        ELangue s_Langue = ELangue::Fr;
        if (LangueDepuisCode(s_Texte, s_Langue)) {
            m_LangueImposee = true;
            m_LangueChoisie = CodeLangue(s_Langue);
            Diag("Langue imposée par roulette47.ini : {}.", CodeLangue(s_Langue));
        }
        else if (LangueDeSteam(DossierDuMod(), s_Langue)) {
            Diag("Langue de Steam pour le jeu : {}.", CodeLangue(s_Langue));
        }
        else {
            s_Langue = LangueDeWindows();
            Diag("Langue de Windows : {}.", CodeLangue(s_Langue));
        }
        DefinirLangue(s_Langue);
    }
    m_Touche = Lire(L"key", VK_F9);
    m_OutilsDeveloppement = Lire(L"dev_tools", 0) != 0;
    m_ReleverReponses = Lire(L"dump_responses", 0) != 0;
    m_JournalEvenements = Lire(L"log_events", 0) != 0;
}

void Roulette::EnregistrerReglages() {
    const auto s_Fichier = Large(DossierDuMod() + "\\roulette47.ini");
    auto Ecrire = [&](const std::wstring& p_Cle, int p_Valeur) {
        WritePrivateProfileStringW(L"roulette", p_Cle.c_str(), std::to_wstring(p_Valeur).c_str(), s_Fichier.c_str());
    };

    SReglages s_R;
    bool s_Encart;

    {
        std::lock_guard s_Verrou(m_Verrou);
        s_R = m_Reglages;
        s_Encart = m_Encart;
    }

    Ecrire(L"mode", static_cast<int>(s_R.m_Mode));
    Ecrire(L"enforce_method", s_R.m_ImposerMethode);
    Ecrire(L"enforce_outfit", s_R.m_ImposerTenue);
    Ecrire(L"allow_suit", s_R.m_AutoriserCostume);
    Ecrire(L"complications", s_R.m_NbComplications);
    Ecrire(L"draw_without_contract", s_R.m_TirageAuto);
    Ecrire(L"npc_targets", s_R.m_NbPnj);
    Ecrire(L"enforce_failure", s_R.m_EchecImpose);
    Ecrire(L"lock_planning", s_R.m_ImposerPreparation);
    Ecrire(L"mission_targets_only", s_R.m_CiblesDuJeuSeules);
    Ecrire(L"discord", m_DiscordActif);
    {
        std::wstring s_Liste;
        {
            std::lock_guard s_Verrou(m_Verrou);
            for (const auto& s_Id : m_DestinationsExclues) s_Liste += (s_Liste.empty() ? L"" : L",") + Large(s_Id);
        }
        WritePrivateProfileStringW(L"roulette", L"excluded_destinations", s_Liste.c_str(), s_Fichier.c_str());
    }
    Ecrire(L"targets_min", s_R.m_CiblesMin);
    Ecrire(L"targets_max", s_R.m_CiblesMax);
    Ecrire(L"replace_objectives", s_R.m_RemplacerObjectifs);
    Ecrire(L"auto_destination", s_R.m_DestinationAuto);
    Ecrire(L"sounds", s_R.m_Sons);
    Ecrire(L"menu_autopilot", s_R.m_Pilote);

    for (size_t i = 0; i < FamillesMethodes().size(); ++i) {
        Ecrire(L"method_family_" + std::to_wstring(i), s_R.m_Familles[i]);
    }

    Ecrire(L"mission_overlay", s_Encart);
    Ecrire(L"key", m_Touche);
    {
        // Langue de la fiche : vide = celle du jeu.
        std::string s_Langue;
        {
            std::lock_guard s_Verrou(m_Verrou);
            s_Langue = m_LangueChoisie;
        }
        WritePrivateProfileStringW(L"roulette", L"language", Large(s_Langue).c_str(), s_Fichier.c_str());
    }
}

// ------------------------------------------------------------ Thread du jeu

void Roulette::SurEvenement(const Glacier::Objet& p_Evenement) {
    Instance().TraiterEvenement(p_Evenement);
}

void Roulette::SurImage() {
    Instance().ChaqueImage();
}

bool Roulette::SurReponse(const uint8_t* p_Octets, size_t p_Taille, std::string& p_Nouveau) {
    return Instance().ReleverReponse(p_Octets, p_Taille, p_Nouveau);
}

// Écrit chaque réponse JSON du serveur dans Retail/Roulette47/responses/ (150 au
// plus par session). Une réponse qui contient un jeton d'authentification n'est
// JAMAIS écrite : le relevé ne doit pas laisser d'identifiants sur le disque.
bool Roulette::ReleverReponse(const uint8_t* p_Octets, size_t p_Taille, std::string& p_Nouveau) {
    static int s_Numero = 0;
    static std::set<size_t> s_Vues;

    bool s_Modifiee = false;

    try {
        auto s_Texte = CorpsEnTexte(p_Octets, p_Taille);
        ++s_Numero;

        if (s_Texte.empty()) {
            Diag("Réponse n° {} : {} octets, illisible.", s_Numero, p_Taille);
            return false;
        }

        // Le contrat d'une mission qui démarre (on y écrit la roulette) et les
        // pages des menus (destinations, inventaire, préparation : Menus.h).
        const bool s_EstContrat = s_Texte.find("\"ContractSessionId\"") != std::string::npos &&
                                  s_Texte.find("\"Objectives\"") != std::string::npos;
        const bool s_EstMenu = Contient(s_Texte, { "\"DestinationsData\"", "\"SubLocationMissionsData\"", "\"Inventory\"",
                                                   "\"Entrances\"", "\"LoadoutItemsData\"", "\"OrderedUnlocks\"" });

        if (s_EstContrat || s_EstMenu) {
            auto s_Json = nlohmann::json::parse(s_Texte, nullptr, false);

            if (!s_Json.is_discarded() && s_Json.is_object()) {
                // Une page des menus ou un contrat : le jeu est en ligne. Une
                // réponse quelconque ne suffit pas (au démarrage, ou pendant une
                // maintenance, le serveur répond sans servir de page).
                if (m_ReponsesServeur.fetch_add(1) == 0) {
                    Diag("Serveurs d'IOI : première page reçue, jeu en ligne.");
                    m_Fenetre.Pousser();
                }

                const bool s_Change = s_EstContrat && s_Json.contains("Contract") ? InjecterContrat(s_Json) : TraiterMenus(s_Json);

                if (s_Change) {
                    s_Texte = s_Json.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
                    p_Nouveau = EstGzip(p_Octets, p_Taille) ? EnGzip(s_Texte) : s_Texte;
                    s_Modifiee = !p_Nouveau.empty();
                }
            }
        }

        // Relevé sur le disque : outil de développement (« dump_responses=1 »).
        if (!m_ReleverReponses || s_Numero > 150) return s_Modifiee;

        // Jamais de jeton d'authentification ni d'identité sur le disque. On
        // cherche les CLÉS (« "access_token" », « "token": ») : les objets du jeu
        // ont des champs « TokenID » ou « OutfitToken » sans rien de secret (la
        // v0.4.1 écartait tout texte contenant « token », donc les pages des
        // menus). La réponse de profil porte SteamId, gamertag et adresse.
        const auto s_Bas = EnMinuscules(s_Texte);
        for (const char* s_Cle : { "_token\"", "\"token\"", "authorization", "password", "\"ticket\"",
                                   "\"steamid\"", "\"epicid\"", "\"gamertag\"", "\"email\"", "\"amazonid\"" }) {
            if (s_Bas.find(s_Cle) != std::string::npos) {
                Diag("Réponse n° {} : {} octets, non écrite (jeton).", s_Numero, s_Texte.size());
                return s_Modifiee;
            }
        }

        if (!s_Vues.insert(std::hash<std::string> {}(s_Texte)).second) return s_Modifiee;

        // Indice du contenu : les premières clés du JSON.
        std::string s_Indice;
        try {
            const auto s_Json = nlohmann::json::parse(s_Texte);
            if (s_Json.is_object()) {
                for (auto it = s_Json.begin(); it != s_Json.end() && s_Indice.size() < 50; ++it) s_Indice += "_" + it.key();
            }
            else if (s_Json.is_array()) s_Indice = "_tableau";
        }
        catch (...) {
            s_Indice = "_texte";
        }
        for (auto& c : s_Indice) if (!isalnum(static_cast<unsigned char>(c)) && c != '_') c = '-';

        const auto s_Dossier = std::filesystem::path(DossierDuMod()) / "responses";
        std::error_code s_Erreur;
        std::filesystem::create_directories(s_Dossier, s_Erreur);

        std::ofstream(s_Dossier / std::format("{:03}{}.json", s_Numero, s_Indice), std::ios::binary) << s_Texte;
        Diag("Réponse n° {} relevée : {} ({} Ko{}{}).", s_Numero, s_Indice, s_Texte.size() / 1024,
             EstGzip(p_Octets, p_Taille) ? ", gzip" : "", s_Modifiee ? ", RÉÉCRITE PAR LA ROULETTE" : "");
    }
    catch (const std::exception& e) {
        Diag("Réponse illisible : {}", e.what());
    }

    return s_Modifiee;
}

// Le contrat arrive du serveur AVANT le chargement du niveau : c'est ici que se
// fait le tirage, puis ses règles sont écrites comme objectifs du contrat (voir
// Contrat.h). PNJ et tenues du lieu viennent de Retail/Roulette47/locations/, gardés
// lors d'une visite précédente ; à la première visite d'un lieu, seules les
// cibles du contrat sont tirées, sans tenue du niveau.
// Le contrat arrive du serveur AVANT le chargement du niveau : la roulette y est
// écrite comme objectifs du contrat (voir Contrat.h). Le tirage est celui fait
// sur l'écran de préparation (TirerAvantMission) quand la mission y est passée ;
// sinon il se fait ici. PNJ et tenues du lieu viennent de Retail/Roulette47/locations/,
// gardés lors d'une visite précédente ; à la première visite d'un lieu, seules
// les cibles du contrat sont tirées, sans tenue du niveau.
bool Roulette::InjecterContrat(nlohmann::json& p_Reponse) {
    using nlohmann::json;

    auto& s_Contrat = p_Reponse["Contract"];
    if (!s_Contrat.contains("Data") || !s_Contrat["Data"].contains("Objectives") || !s_Contrat["Data"]["Objectives"].is_array()) {
        return false;
    }

    std::lock_guard s_Verrou(m_Verrou);

    if (!m_Reglages.m_EchecImpose) {
        Diag("Contrat reçu : roulette non ajoutée (réglages).");
        return false;
    }

    const auto& s_Meta = s_Contrat.contains("Metadata") ? s_Contrat["Metadata"] : json::object();
    const auto s_Lieu = ChampTexte(s_Meta, "Location");
    const auto s_Type = ChampTexte(s_Meta, "Type");
    const auto s_IdContrat = ChampTexte(s_Meta, "Id");

    // Une cible fugitive ne se joue qu'une fois : un échec imposé par la
    // roulette serait définitif. Jamais de roulette dessus.
    if (s_Type == "elusive") {
        Diag("Contrat reçu ({}) : cible fugitive, roulette non ajoutée.", s_Lieu);
        m_Prepare = false;
        return false;
    }

    m_Depot.Charger();

    STirage s_Tirage;
    bool s_Memoire = true;
    const bool s_DeLaPreparation = m_Prepare && m_IdContratPrepare == s_IdContrat;

    // Sans contrat de la roulette accepté pour cette mission, partie normale.
    if (!s_DeLaPreparation && !m_Reglages.m_TirageAuto) {
        Diag("Contrat reçu ({}, {}) : aucun contrat de la roulette accepté pour cette mission, partie normale.", s_Lieu, s_Type);
        return false;
    }

    if (s_DeLaPreparation) {
        s_Tirage = m_TiragePrepare;
    }
    else {
        // ---- Cibles du contrat.
        std::set<std::string> s_Officielles;

        for (const auto& s_Id : CiblesDesObjectifs(s_Contrat["Data"]["Objectives"])) {
            SObjectifCible c;
            c.m_Id = s_Id;
            if (const auto* e = m_Depot.Trouver(s_Id)) {
                c.m_Nom = NomLisible(e, {});
                c.m_Image = e->m_Image;
            }
            if (c.m_Nom.empty()) c.m_Nom = Tx("cible");
            s_Officielles.insert(s_Id);
            s_Tirage.m_Cibles.push_back(std::move(c));
        }

        if (s_Tirage.m_Cibles.empty()) {
            Diag("Contrat reçu ({}, {}) : aucune cible à éliminer, roulette non ajoutée.", s_Lieu, s_Type);
            return false;
        }

        s_Memoire = ChargerMemoireLieu(s_Lieu, s_Tirage, s_Officielles);
        ChoisirCibles(s_Tirage, m_Reglages, m_Hasard);
        Tirer(s_Tirage, m_Reglages, m_Hasard);
    }

    // Le contrat accepté est joué : il est consommé.
    m_Prepare = false;
    m_Propose = false;
    m_DestLieu = m_DestMission = -1;
    m_DestIdLieu.clear();
    m_DestContrat.clear();
    m_DernierContrat = s_IdContrat;

    // ---- Les objectifs de la roulette, ajoutés à ceux du contrat — qui sont
    // d'abord masqués (Julien : la mission affichait encore ses objectifs).
    std::vector<std::string> s_IdsCibles;
    const auto s_Objectifs = ObjectifsRoulette(s_Tirage, m_Hasard, m_Reglages.m_RemplacerObjectifs, &s_IdsCibles);
    const int s_Masques = m_Reglages.m_RemplacerObjectifs ? MasquerObjectifsDuJeu(s_Contrat["Data"], s_IdsCibles) : 0;
    for (const auto& o : s_Objectifs) s_Contrat["Data"]["Objectives"].push_back(o);

    // ---- Difficulté Professionnel ; Puriste : sans sauvegarde.
    ReglerContrat(s_Contrat["Data"], s_Tirage.m_Mode);

    // ---- L'équipement tiré à la préparation devient l'équipement obligatoire.
    // Costume au menu : pas d'équipement obligatoire (il remplacerait le
    // costume choisi) ; l'entrée reste imposée.
    const bool s_AuMenu = c_CostumeAuMenu && EntreeEnCostume(s_Tirage.m_Preparation);
    bool s_Obligatoire = false;
    if (s_DeLaPreparation && m_Reglages.m_ImposerPreparation) {
        if (!s_AuMenu) s_Obligatoire = AjouterEquipementObligatoire(s_Contrat, s_Tirage.m_Preparation);
        else if (!s_Tirage.m_Preparation.m_Entree.Vide() && s_Contrat.contains("Data") && s_Contrat["Data"].is_object()) {
            s_Contrat["Data"]["Entrances"] = nlohmann::json::array({ s_Tirage.m_Preparation.m_Entree.m_Id });
        }
    }

    const auto s_Pnj = std::ranges::count_if(s_Tirage.m_Cibles, [](const auto& c) { return !c.m_Officielle; });

    m_Tirage = std::move(s_Tirage);
    m_TirageInjecte = true;
    m_DemandeTirage = false;
    m_Rompu = false;
    ++m_NumeroTirage;

    if (m_Images) {
        for (const auto& c : m_Tirage.m_Cibles) m_Images->Demander(c.m_Image);
        for (const auto& t : m_Tirage.m_Tenues) m_Images->Demander(t.m_Image);
    }

    Diag("Contrat reçu ({}, {}) : roulette ajoutée{} — {} cible(s) dont {} PNJ, {} tenue(s), {} objectif(s) ajouté(s), {} du jeu masqué(s){}{}.",
         s_Lieu, s_Type, s_DeLaPreparation ? " (tirage de la préparation)" : "", m_Tirage.m_Cibles.size(), s_Pnj,
         m_Tirage.m_Tenues.size(), s_Objectifs.size(), s_Masques, s_Obligatoire ? ", équipement obligatoire écrit" : "",
         s_Memoire ? "" : " ; lieu jamais visité : ni PNJ ni tenue du niveau");

    for (const auto& o : s_Objectifs) Diag("  objectif : {}", ChampTexte(o, "BriefingText"));

    m_Fenetre.Pousser();
    return true;
}

// Retail/Roulette47/locations/<LIEU>.json : tenues du niveau (plus le costume de 47)
// et PNJ tirés au sort parmi ceux qu'on y a vus. Rend faux si le lieu n'a jamais
// été visité. Sous verrou.
bool Roulette::ChargerMemoireLieu(const std::string& p_Lieu, STirage& p_Tirage, const std::set<std::string>& p_Officielles) {
    using nlohmann::json;

    // Le relevé du joueur d'abord (locations/), sinon celui livré avec le mod
    // (initial_locations/, les missions relevées le 24/11/2026) : un tout premier
    // tirage sur une mission jamais jouée propose déjà des PNJ.
    json s_Memoire;
    {
        std::string s_Fichier = p_Lieu;
        for (auto& c : s_Fichier) if (!isalnum(static_cast<unsigned char>(c)) && c != '_') c = '_';
        for (const char* s_Dossier : { "locations", "initial_locations" }) {
            std::ifstream s_Flux(std::filesystem::path(DossierDuMod()) / s_Dossier / (s_Fichier + ".json"), std::ios::binary);
            if (!s_Flux) continue;
            s_Memoire = json::parse(s_Flux, nullptr, false);
            if (!s_Memoire.is_discarded() && s_Memoire.is_object()) break;
        }
        if (s_Memoire.is_discarded() || !s_Memoire.is_object()) s_Memoire = json::object();
    }

    p_Tirage.m_Tenues.clear();

    for (const auto& t : s_Memoire.value("tenues", json::array())) {
        STenue s_Tenue;
        s_Tenue.m_Id = ChampTexte(t, "id");
        s_Tenue.m_Nom = ChampTexte(t, "nom");
        s_Tenue.m_Image = ChampTexte(t, "image");
        // Le nom relevé est dans la langue de qui a relevé : celle du joueur
        // d'abord, si le jeu sait traduire la tenue.
        if (const auto* e = m_Depot.Trouver(s_Tenue.m_Id); e && EstUneCle(e->m_Cle)) {
            if (auto s_Traduit = Moteur::Traduire(e->m_Cle); !s_Traduit.empty()) s_Tenue.m_Nom = std::move(s_Traduit);
        }
        if (!s_Tenue.m_Id.empty()) p_Tirage.m_Tenues.push_back(std::move(s_Tenue));
    }

    STenue s_Costume;
    s_Costume.m_Id = "costume_47";
    s_Costume.m_Nom = Tx("costume47");
    s_Costume.m_Image = m_ImageCostume;
    s_Costume.m_EstCostume = true;
    p_Tirage.m_Tenues.push_back(std::move(s_Costume));

    std::vector<json> s_Pnj;
    for (const auto& p : s_Memoire.value("pnj", json::array())) {
        if (ChampTexte(p, "id").empty() || p_Officielles.contains(EnMinuscules(ChampTexte(p, "id")))) continue;
        // Sans photo sur le service d'IOI : jamais tiré.
        if (m_Images && m_Images->Introuvable(ChampTexte(p, "image"))) continue;
        s_Pnj.push_back(p);
    }

    std::ranges::shuffle(s_Pnj, m_Hasard);

    for (size_t k = 0; k < s_Pnj.size(); ++k) {
        SObjectifCible c;
        c.m_Officielle = false;
        c.m_Id = EnMinuscules(ChampTexte(s_Pnj[k], "id"));
        c.m_Nom = ChampTexte(s_Pnj[k], "nom");
        // Relevés avant le 24/11/2026 : nom interne, sans ses accents.
        if (const auto* e = m_Depot.Trouver(c.m_Id); e && !EstUneCle(e->m_Nom) && CleNom(e->m_Nom) == CleNom(c.m_Nom)) c.m_Nom = e->m_Nom;
        c.m_CleNom = CleNom(c.m_Nom);
        c.m_Image = ChampTexte(s_Pnj[k], "image");
        p_Tirage.m_Cibles.push_back(std::move(c));
    }

    return !s_Memoire.empty();
}

// ------------------------------------------------------------ Pages des menus

// La langue du jeu, lue dans ses propres textes : le nom des États-Unis. Relue
// à chaque page des menus : le joueur peut changer la langue du jeu en cours de
// partie (Julien, 26/11/2026 : jeu repassé en français, fiche restée en anglais).
void Roulette::DetecterLangueDuJeu() {
    if (m_LangueImposee) return;

    ELangue s_Langue = ELangue::Fr;
    if (!LangueDepuisTexteDuJeu(Moteur::Traduire("UI_LOCATION_COLORADO_COUNTRY"), s_Langue)) return;

    const bool s_Premiere = !m_LangueDuJeuLue.exchange(true);
    if (s_Langue != Langue()) {
        Diag("Langue du jeu : {} (au lieu de {}).", CodeLangue(s_Langue), CodeLangue(Langue()));
        DefinirLangue(s_Langue);
        m_Fenetre.Pousser();
    }
    else if (s_Premiere) {
        Diag("Langue du jeu : {}.", CodeLangue(s_Langue));
    }
}

bool Roulette::TraiterMenus(nlohmann::json& p_Reponse) {
    const Traducteur s_Traduire = [](const std::string& p_Cle) { return Moteur::Traduire(p_Cle); };
    DetecterLangueDuJeu();

    // ---- Inventaire (au démarrage) : ce que le joueur peut emporter.
    if (p_Reponse.contains("Inventory")) {
        std::vector<SObjetInventaire> s_Objets;
        auto s_Entrees = LireEntreesPossedees(p_Reponse, s_Traduire);
        auto s_Costumes = LireCostumes(p_Reponse, s_Traduire);
        if (LireInventaire(p_Reponse, s_Objets, s_Traduire)) {
            std::lock_guard s_Verrou(m_Verrou);
            m_Inventaire = std::move(s_Objets);
            m_EntreesPossedees = std::move(s_Entrees);
            if (!s_Costumes.empty()) {
                m_Costumes = std::move(s_Costumes);
                Diag("Inventaire : {} costume(s) de 47 possédé(s).", m_Costumes.size());
            }
            Diag("Inventaire lu : {} arme(s) dissimulée(s) et équipement(s), {} entrée(s).", m_Inventaire.size(), m_EntreesPossedees.size());
        }
        return false;
    }

    const auto s_Donnees = p_Reponse.find("data");
    if (s_Donnees == p_Reponse.end() || !s_Donnees->is_object()) return false;

    // ---- Écran des destinations.
    if (s_Donnees->contains("DestinationsData")) {
        std::vector<SDestination> s_Liste;
        {
            std::lock_guard s_Verrou(m_Verrou);
            s_Liste = m_Destinations;
        }

        bool s_Change = false;

        if (LireDestinations(p_Reponse, s_Liste, s_Traduire)) {
            LireHistoire(p_Reponse, s_Liste, s_Traduire);

            // Titres des missions dans la langue du jeu du moment :
            // destinations.json garde ceux de la langue où ils ont été lus, et
            // la page d'un lieu (qui les relit) peut ne jamais revenir (Julien a
            // passé son jeu en anglais le 26/11/2026 : titres restés en français).
            for (auto& d : s_Liste) {
                for (auto& m : d.m_Missions) {
                    if (auto s_Titre = Moteur::Traduire(m.m_CleTitre); !s_Titre.empty()) m.m_Titre = std::move(s_Titre);
                }
            }

            std::lock_guard s_Verrou(m_Verrou);
            m_Destinations = std::move(s_Liste);
            RetrouverDestination(); // les index ont pu changer
            EnregistrerDestinations();

            const auto s_Possedees = std::ranges::count_if(m_Destinations, [](const auto& d) { return !d.m_Verrouillee; });
            const auto s_Missions = std::ranges::count_if(m_Destinations, [](const auto& d) { return !d.m_Missions.empty(); });
            Diag("Destinations : {} lieu(x), dont {} possédé(s), {} aux missions connues.", m_Destinations.size(), s_Possedees, s_Missions);

            if (m_Images) {
                m_Images->RelireService();
                for (const auto& d : m_Destinations) if (!d.m_Verrouillee) m_Images->Demander(d.m_Image);
            }

            // Rien n'est tiré ici (Julien : le tirage se demande, par F9). Un
            // contrat déjà accepté : l'onglet Destinations ne propose que son lieu.
            if (m_Prepare && m_Reglages.m_DestinationAuto && m_DestLieu >= 0) {
                s_Change = ImposerDestination(p_Reponse, m_DestIdLieu);
                Diag("Menu principal : {} {}.", s_Change ? "seule destination proposée :" : "destination du contrat ABSENTE de la liste :",
                     m_Destinations[m_DestLieu].m_Nom);
            }

            m_Fenetre.Pousser();
        }
        return s_Change;
    }

    // ---- Page d'un lieu : ses missions.
    if (s_Donnees->contains("MissionData")) {
        std::vector<SDestination> s_Liste;
        {
            std::lock_guard s_Verrou(m_Verrou);
            s_Liste = m_Destinations;
        }

        if (LireMissions(p_Reponse, s_Liste, s_Traduire)) {
            std::lock_guard s_Verrou(m_Verrou);
            const auto s_Avant = m_Destinations.size();
            m_Destinations = std::move(s_Liste);
            (void)s_Avant;
            RetrouverDestination(); // les missions du lieu ont pu changer d'ordre
            EnregistrerDestinations();

            for (const auto& d : m_Destinations) {
                if (d.m_Missions.empty()) continue;
                if (m_Images) for (const auto& m : d.m_Missions) m_Images->Demander(m.m_Image);
            }
            Diag("Missions d'un lieu relevées ({} lieu(x) aux missions connues).",
                 std::ranges::count_if(m_Destinations, [](const auto& d) { return !d.m_Missions.empty(); }));
            m_Fenetre.Pousser();
        }

        // Contrat accepté : la page ouverte, quel que soit son lieu, devient
        // celle de la mission tirée (le menu principal n'est relu qu'au
        // lancement et après une mission ; les pages des lieux, à chaque fois).
        std::lock_guard s_Verrou(m_Verrou);
        if (!m_Prepare || !m_Reglages.m_DestinationAuto || m_DestLieu < 0 || m_DestMission < 0) return false;

        const auto& d = m_Destinations[m_DestLieu];
        const auto& m = d.m_Missions[m_DestMission];
        if (ImposerPageLieu(p_Reponse, d, m)) {
            Diag("Page d'un lieu : remplacée par la mission du contrat, « {} » ({}).", m.m_Titre, d.m_Nom);
            ++m_PagesLieu;
            return true;
        }
        if (ImposerMission(p_Reponse, m_DestContrat)) {
            Diag("Page du lieu : seule la mission du contrat est proposée.");
            ++m_PagesLieu;
            return true;
        }
        Diag("Page d'un lieu : mission du contrat introuvable (forme inconnue), page laissée telle quelle.");
        return false;
    }

    // ---- Page « Lieu de départ » : seule l'entrée tirée.
    if (s_Donnees->contains("OrderedUnlocks")) {
        std::lock_guard s_Verrou(m_Verrou);
        if (!m_Prepare || !m_Reglages.m_ImposerPreparation || ContratDeLaPage(p_Reponse) != m_IdContratPrepare) return false;

        const bool s_Change = ImposerListeEntrees(p_Reponse, m_TiragePrepare.m_Preparation, m_Planif.m_Entrees);
        if (s_Change) {
            Diag("Lieu de départ : seule l'entrée tirée est proposée ({}).", m_TiragePrepare.m_Preparation.m_Entree.m_Nom);
            ++m_PagesEntrees;
        }
        return s_Change;
    }

    // ---- Écran de préparation.
    if (s_Donnees->contains("Entrances") && s_Donnees->contains("LoadoutData")) {
        return PreparerAvantMission(p_Reponse);
    }

    // ---- Liste d'un emplacement (arme, équipement), demandée au clic.
    if (s_Donnees->contains("LoadoutItemsData")) {
        std::lock_guard s_Verrou(m_Verrou);

        if (!m_Prepare || !m_Reglages.m_ImposerPreparation || ContratDeLaPage(p_Reponse) != m_IdContratPrepare) return false;

        bool s_Absent = false;
        const bool s_Change = ImposerEmplacement(p_Reponse, m_TiragePrepare.m_Preparation, s_Absent,
                                                 c_CostumeAuMenu && EntreeEnCostume(m_TiragePrepare.m_Preparation));
        if (s_Change) Diag("Préparation : liste d'emplacement réduite à l'objet tiré (ou vidée).");
        if (s_Absent) Diag("Préparation : l'objet tiré n'est pas permis dans ce lieu, l'emplacement reste libre.");
        return s_Change;
    }

    return false;
}

// L'écran de préparation arrive avant le départ : c'est là que la roulette est
// tirée, sous les yeux du joueur (Julien : « que la roulette soit faite avant
// de démarrer la mission »). La page est réécrite : seule l'entrée tirée reste
// proposée, l'arme et l'objet tirés sont présélectionnés. Le même tirage est
// ensuite écrit dans le contrat.
bool Roulette::PreparerAvantMission(nlohmann::json& p_Page) {
    const Traducteur s_Traduire = [](const std::string& p_Cle) { return Moteur::Traduire(p_Cle); };

    SPlanification s_Planif;
    if (!LirePlanification(p_Page, s_Planif, s_Traduire)) return false;

    if (s_Planif.m_Type == "elusive") {
        Diag("Préparation : cible fugitive, pas de roulette.");
        return false;
    }

    std::lock_guard s_Verrou(m_Verrou);

    // Seulement la mission d'un contrat accepté : sinon, préparation du jeu.
    if (m_EnMission || !m_Prepare || s_Planif.m_IdContrat != m_IdContratPrepare) return false;

    if (const auto* s_Toutes = NoeudJson(p_Page, { "data", "Entrances" }); s_Toutes && s_Toutes->is_array()) {
        std::string s_Ordre;
        for (const auto& e : *s_Toutes) s_Ordre += " " + ChampTexte(e, "Id");
        Diag("Préparation : entrées dans l'ordre de la page :{}", s_Ordre);
    }

    m_Depot.Charger();
    if (m_Images) m_Images->RelireService();

    // Les entrées que la page dit débloquées POUR CETTE MISSION : retenues pour
    // les prochains tirages, et l'entrée tirée y est vérifiée.
    for (auto& e : s_Planif.m_Entrees) {
        if (const auto* x = m_Depot.Trouver(e.m_Id)) {
            e.m_Image = x->m_Image;
            if (e.m_Nom == e.m_IdJeu) e.m_Nom = NomLisible(x, { &e.m_Nom });
        }
    }
    m_Planif.m_Entrees = s_Planif.m_Entrees;

    if (m_DestLieu >= 0 && m_DestMission >= 0 && !s_Planif.m_Entrees.empty()) {
        auto& s_Connues = m_Destinations[m_DestLieu].m_Missions[m_DestMission].m_Entrees;
        s_Connues.clear();
        for (const auto& e : s_Planif.m_Entrees) s_Connues.push_back(e.m_IdJeu);
        EnregistrerDestinations();
    }

    auto& s_Entree = m_TiragePrepare.m_Preparation.m_Entree;

    // Les entrées de la page qui conviennent au mode : en costume (Assassin et
    // Puriste), arme permise (Assassin), deux équipements. La page liste aussi
    // les entrées débloquées d'office, absentes de l'inventaire.
    const EMode s_Mode = m_TiragePrepare.m_Mode;
    std::vector<const SEntree*> s_Convenables;
    for (const auto& e : s_Planif.m_Entrees) s_Convenables.push_back(&e);
    if (s_Mode != EMode::Freestyle) {
        const auto Resserrer = [&](auto p_Garder) {
            std::vector<const SEntree*> s_Reste;
            for (const auto* e : s_Convenables) if (p_Garder(*e)) s_Reste.push_back(e);
            if (!s_Reste.empty()) s_Convenables = std::move(s_Reste);
        };
        Resserrer([](const SEntree& e) { return e.m_EnCostume; });
        if (s_Mode == EMode::Assassin) Resserrer([](const SEntree& e) { return e.m_ArmePermise; });
        Resserrer([](const SEntree& e) { return e.m_Equipements >= 2; });
    }

    const auto s_Tiree = std::ranges::find(s_Planif.m_Entrees, s_Entree.m_Id, &SEntree::m_Id);
    const bool s_Absente = s_Tiree == s_Planif.m_Entrees.end();
    const bool s_Deguisee = !s_Absente && s_Mode != EMode::Freestyle && !s_Tiree->m_EnCostume &&
                            std::ranges::any_of(s_Convenables, [](const SEntree* e) { return e->m_EnCostume; });

    if (!s_Convenables.empty() && (s_Absente || s_Deguisee)) {
        const auto& e = *s_Convenables[std::uniform_int_distribution<size_t>(0, s_Convenables.size() - 1)(m_Hasard)];
        if (s_Entree.Vide()) Diag("Préparation : lieu de départ choisi sur la page (mode {}) : « {} ».", LibelleMode(s_Mode), e.m_Nom);
        else Diag("Préparation : l'entrée tirée « {} » ne va pas ({}), remplacée par « {} ».", s_Entree.m_Nom,
                  s_Absente ? "absente de cette mission" : "47 n'y part pas en costume", e.m_Nom);
        s_Entree.m_Id = e.m_Id;
        s_Entree.m_IdJeu = e.m_IdJeu;
        s_Entree.m_Nom = e.m_Nom;
        s_Entree.m_Cle = e.m_CleNom;
        s_Entree.m_Image = e.m_Image;
        if (m_Images) m_Images->Demander(e.m_Image);
        ++m_NumeroTirage;
    }

    // Ce que l'entrée (tirée ou remplacée) refuse ne s'impose pas : l'arme
    // serait marquée « non emportée » alors que le jeu interdit de la prendre.
    if (const auto s_It = std::ranges::find(s_Planif.m_Entrees, s_Entree.m_Id, &SEntree::m_Id); s_It != s_Planif.m_Entrees.end()) {
        auto& p = m_TiragePrepare.m_Preparation;
        if (!s_It->m_ArmePermise && !p.m_Arme.Vide()) {
            Diag("Préparation : « {} » n'admet pas d'arme dissimulée, arme imposée retirée.", s_Entree.m_Nom);
            p.m_Arme = {};
        }
        if (s_It->m_Equipements <= 0 && !p.m_Objet.Vide()) {
            Diag("Préparation : « {} » n'admet aucun équipement, objet imposé retiré.", s_Entree.m_Nom);
            p.m_Objet = {};
        }
        if (s_It->m_Equipements <= 1 && !p.m_Objet2.Vide()) {
            Diag("Préparation : « {} » n'admet qu'un équipement, second objet retiré.", s_Entree.m_Nom);
            p.m_Objet2 = {};
        }
    }

    // La page de préparation est ouverte : le jeu a chargé les textes de
    // l'équipement, les noms se traduisent (et se retiennent).
    ApprendreNoms();

    const bool s_Nouvelle = false;
    m_Tirage = m_TiragePrepare;

    bool s_Change = m_Reglages.m_ImposerPreparation && ImposerPlanification(p_Page, m_TiragePrepare.m_Preparation, c_CostumeAuMenu);

    // Préparation verrouillée (Julien, 24/11/2026 : on arrive sur la
    // préparation et on n'a plus qu'à lancer, sans rien pouvoir changer).
    // Costume au menu : l'écran « Préparation » reste, mais ne propose plus
    // que l'emplacement du costume ; l'équipement est imposé par le contrat
    // de la mission (InjecterContrat).
    // Les listes de chaque emplacement ne proposent alors que l'objet tiré
    // (ImposerEmplacement, strict) : un équipement obligatoire remplacerait
    // tout, costume compris (essai du 25/11/2026 : 47 partait dans la tenue
    // de l'entrée).
    const bool s_CostumeAuMenu = c_CostumeAuMenu && m_Reglages.m_ImposerPreparation && EntreeEnCostume(m_TiragePrepare.m_Preparation);
    if (s_CostumeAuMenu) {
        Diag("Préparation : costume au choix dans le menu, équipement réduit aux objets tirés.");
    }
    else if (m_Reglages.m_ImposerPreparation) {
        for (auto* s_Contrat : { NoeudJson(p_Page, { "data", "Contract" }), NoeudJson(p_Page, { "data", "UserCentric", "Contract" }) }) {
            if (s_Contrat && AjouterEquipementObligatoire(*s_Contrat, m_TiragePrepare.m_Preparation)) s_Change = true;
        }
    }

    // Le lieu de départ tiré devient celui que le jeu retient, comme si le
    // joueur l'avait choisi sur la page « Lieu de départ » (que le verrou
    // retire) : sans cela, le jeu partait de la dernière entrée choisie.
    if (m_Reglages.m_ImposerPreparation && !m_TiragePrepare.m_Preparation.m_Entree.Vide()) {
        bool s_Trouvee = false;
        if (const auto* s_Entrees = NoeudJson(p_Page, { "data", "Entrances" }); s_Entrees && s_Entrees->is_array()) {
            for (const auto& e : *s_Entrees) {
                const auto& P = e.contains("Properties") && e["Properties"].is_object() ? e["Properties"] : nlohmann::json::object();
                if (EnMinuscules(ChampTexte(P, "RepositoryId")) != m_TiragePrepare.m_Preparation.m_Entree.m_Id) continue;
                auto s_Equip = P.contains("Equip") && P["Equip"].is_array() ? P["Equip"] : nlohmann::json::array();
                auto s_Entree = e;
                // Le costume choisi par le joueur remplace celui de l'entrée
                // (une entrée « en costume » : aucune tenue, ou une tenue HERO).
                if (const auto& c = m_TiragePrepare.m_Preparation.m_Costume; !s_CostumeAuMenu && !c.Vide() && !c.m_IdJeu.empty()) {
                    s_Equip = nlohmann::json::array({ c.m_IdJeu });
                    if (s_Entree.contains("Properties") && s_Entree["Properties"].is_object()) s_Entree["Properties"]["Equip"] = s_Equip;
                    Diag("Préparation : costume de départ « {} » ({}).", c.m_Nom, c.m_IdJeu);
                }
                Moteur::ImposerEntree(m_TiragePrepare.m_Preparation.m_Entree.m_Id, s_Equip.dump(), s_Entree.dump());
                s_Trouvee = true;
                break;
            }
        }
        Diag("Préparation : lieu de départ « {} » {}.", m_TiragePrepare.m_Preparation.m_Entree.m_Nom,
             s_Trouvee ? "imposé au jeu" : "absent de la page, NON imposé");
    }

    // Difficulté Professionnel seule dans le sélecteur de la préparation ;
    // Puriste : sans sauvegarde (mêmes réglages que le contrat de la mission).
    for (auto* s_Donnees : { NoeudJson(p_Page, { "data", "Contract", "Data" }),
                             NoeudJson(p_Page, { "data", "UserCentric", "Contract", "Data" }) }) {
        if (s_Donnees) ReglerContrat(*s_Donnees, m_TiragePrepare.m_Mode);
    }

    // Le contrat que la page porte (briefing, « Objectifs ») : mêmes objectifs
    // que ceux qui seront écrits dans le contrat de la mission.
    bool s_Objectifs = false;
    if (m_Reglages.m_RemplacerObjectifs) {
        for (auto* s_Donnees : { NoeudJson(p_Page, { "data", "Contract", "Data" }),
                                 NoeudJson(p_Page, { "data", "UserCentric", "Contract", "Data" }) }) {
            if (!s_Donnees || !s_Donnees->contains("Objectives") || !(*s_Donnees)["Objectives"].is_array()) continue;
            std::vector<std::string> s_Ids;
            const auto s_Ajout = ObjectifsRoulette(m_TiragePrepare, m_Hasard, true, &s_Ids);
            MasquerObjectifsDuJeu(*s_Donnees, s_Ids);
            for (const auto& o : s_Ajout) (*s_Donnees)["Objectives"].push_back(o);
            s_Objectifs = true;
        }

        // Ce que montrent l'écran « Objectifs » et l'écran de chargement (essai
        // en jeu du 22/11/2026 : ils montraient encore ceux du jeu).
        if (auto* s_Briefing = NoeudJson(p_Page, { "data", "Objectives" }); s_Briefing && s_Briefing->is_array()) {
            *s_Briefing = BriefingRoulette(m_TiragePrepare);
            s_Objectifs = true;
        }
    }

    Diag("Préparation de « {} » ({}) : {}{}{}.", m_TitreMission, m_Planif.m_Lieu, s_Nouvelle ? "roulette tirée" : "contrat accepté appliqué",
         s_Change ? ", entrée et équipement imposés dans le menu" : "", s_Objectifs ? ", objectifs de la roulette au briefing" : "");

    ++m_PagesPrep;
    m_Fenetre.Pousser();
    return s_Change || s_Objectifs;
}

// Sous verrou. N'utilise que des données déjà lues (cibles nommées, mémoire du
// lieu, inventaire) : « Relancer » l'appelle depuis le thread de l'interface.
void Roulette::TirerAvantMission() {
    STirage t;
    t.m_Cibles = m_CiblesPlanif;

    std::set<std::string> s_Officielles;
    for (const auto& c : t.m_Cibles) s_Officielles.insert(c.m_Id);

    const bool s_Memoire = ChargerMemoireLieu(m_Planif.m_Lieu, t, s_Officielles);
    ChoisirCibles(t, m_Reglages, m_Hasard);
    Tirer(t, m_Reglages, m_Hasard);

    if (m_Reglages.m_ImposerPreparation) {
        // Freestyle : les tenues imposées aux cibles, par leur nom interne.
        std::vector<std::string> s_Jetons;
        if (m_Reglages.m_Mode == EMode::Freestyle) {
            std::vector<std::string> s_Noms;
            for (const auto& c : t.m_Cibles) {
                if (c.m_Tenue < 0 || c.m_Tenue >= static_cast<int>(t.m_Tenues.size())) continue;
                const auto& s_Tenue = t.m_Tenues[c.m_Tenue];
                if (s_Tenue.m_EstCostume) continue;
                if (const auto* e = m_Depot.Trouver(s_Tenue.m_Id); e && !e->m_NomCommun.empty()) s_Noms.push_back(e->m_NomCommun);
            }
            s_Jetons = JetonsDesTenues(m_Planif, s_Noms);
        }

        t.m_Preparation = TirerPreparation(m_Planif, m_Inventaire, t, m_Reglages.m_Mode, m_Hasard, s_Jetons);
        CompleterImposes(t.m_Preparation);
        AppliquerCostume(t.m_Preparation);
        if (!s_Jetons.empty()) Diag("Freestyle : entrée dans la tenue d'une cible possible ({} jeton(s)), entrée « {} ».",
                                    s_Jetons.size(), t.m_Preparation.m_Entree.m_Nom);

        if (m_EntreesReperees.contains(t.m_Preparation.m_Entree.m_Id) && EcarterComplication(t, "sans_etre_repere")) {
            Diag("Entrée « {} » : 47 y est repéré dès le départ, « Ne pas être repéré » écartée.", t.m_Preparation.m_Entree.m_Nom);
        }

    }

    m_TiragePrepare = std::move(t);
    m_IdContratPrepare = m_Planif.m_IdContrat;
    ++m_NumeroTirage;

    if (m_Images) {
        for (const auto& c : m_TiragePrepare.m_Cibles) m_Images->Demander(c.m_Image);
        for (const auto& s : m_TiragePrepare.m_Tenues) m_Images->Demander(s.m_Image);
        for (const auto& i : m_ImagesMethodes) m_Images->Demander(i);
        for (const auto& c : m_TiragePrepare.m_Complications) m_Images->Demander(ImageComplication(Complications()[c.m_Index].m_Cle));
        const auto& p = m_TiragePrepare.m_Preparation;
        for (const auto* s_Impose : { &p.m_Entree, &p.m_Arme, &p.m_Objet, &p.m_Objet2 }) m_Images->Demander(s_Impose->m_Image);
    }

    std::string s_Compl;
    for (const auto& c : m_TiragePrepare.m_Complications) s_Compl += std::string(s_Compl.empty() ? "" : ", ") + Complications()[c.m_Index].m_Cle;
    Diag("Complications tirées : {}.", s_Compl.empty() ? "aucune" : s_Compl);
    const auto& p = m_TiragePrepare.m_Preparation;
    Diag("Tirage d'avant-mission (mode {}) : {} cible(s), {} tenue(s){} ; entrée « {} », arme « {} », objets « {} » et « {} » ({} objet(s) en inventaire).",
         LibelleMode(m_Reglages.m_Mode), m_TiragePrepare.m_Cibles.size(), m_TiragePrepare.m_Tenues.size(), s_Memoire ? "" : " (lieu jamais visité)",
         p.m_Entree.m_Nom, p.m_Arme.m_Nom, p.m_Objet.m_Nom, p.m_Objet2.m_Nom, m_Inventaire.size());
}

// Le contrat, tout d'un coup (Julien, 23/11/2026 : « quand on fait le tirage,
// ça génère la destination et tout le reste en même temps ») : tout vient de
// ce que le menu principal et l'inventaire ont déjà dit — missions et leurs
// cibles (StoryData), entrées et équipement possédés, PNJ et tenues des lieux
// déjà visités. Sous verrou.
bool Roulette::TirerContrat() {
    m_Depot.Charger();

    // Ce que le dépôt sait de chaque objet : sa seconde clé de nom et son
    // silencieux (le mode Assassin le veut).
    for (auto& o : m_Inventaire) {
        if (const auto* e = m_Depot.Trouver(o.m_Id)) {
            o.m_Cle2 = e->m_CleLoc;
            o.m_Silencieux = e->m_Silencieux;
        }
    }

    // L'inventaire arrive avant que les textes du jeu soient chargés : ses noms
    // sont traduits ici, ou repris de ceux déjà appris.
    ApprendreNoms();
    if (m_Images) m_Images->RelireService();
    if (m_ImagesMethodes.empty() || m_ImageCostume.empty()) ChoisirImagesObjets();

    if (!TirerDestinationMaintenant() || m_DestMission < 0) {
        Diag("Tirage : aucune mission aux cibles connues (le menu principal n'a pas encore été lu ?).");
        return false;
    }

    m_ErreurCode.clear();
    m_AvertissementCode.clear();
    PreparerPlanification();
    TirerAvantMission();
    m_Propose = true;
    m_Prepare = false;
    m_Tirage = m_TiragePrepare;
    return true;
}

// Images et noms lisibles de l'entrée et de l'équipement imposés. Sous verrou.
void Roulette::CompleterImposes(SPreparation& p_Preparation) {
    for (auto* s_Impose : { &p_Preparation.m_Arme, &p_Preparation.m_Objet, &p_Preparation.m_Objet2 }) {
        if (s_Impose->Vide()) continue;
        const auto* e = m_Depot.Trouver(s_Impose->m_Id);
        if (e && s_Impose->m_Image.empty()) s_Impose->m_Image = e->m_Image;
        // Pas encore de texte du jeu pour cet objet : un nom générique en
        // français pour l'équipement de 47, sinon le nom de sa fiche, nettoyé.
        if (s_Impose->m_Nom == s_Impose->m_IdJeu) {
            if (const char* g = NomGenerique(s_Impose->m_IdJeu)) s_Impose->m_Nom = g;
            else s_Impose->m_Nom = NomNettoye(e ? (e->m_NomCommun.empty() ? e->m_Nom : e->m_NomCommun) : s_Impose->m_IdJeu);
        }
    }
}

// La planification de la mission tirée (m_DestLieu, m_DestMission) : entrées
// possédées, cibles du contrat, nom du lieu et de la mission. Sous verrou.
void Roulette::PreparerPlanification() {
    const auto& d = m_Destinations[m_DestLieu];
    const auto& m = d.m_Missions[m_DestMission];

    SPlanification p;
    p.m_IdContrat = m.m_Id;
    p.m_Lieu = m.m_LieuMission;
    p.m_CleTitre = m.m_CleTitre;
    p.m_Type = m.m_Type;
    p.m_Cibles = m.m_Cibles;
    p.m_Entrees = EntreesPourMission(m_EntreesPossedees, d.m_Id, m);
    for (auto& e : p.m_Entrees) {
        if (const auto* x = m_Depot.Trouver(e.m_Id)) {
            e.m_Image = x->m_Image;
            if (e.m_Nom == e.m_IdJeu) e.m_Nom = NomLisible(x, { &e.m_Nom });
        }
    }
    m_Planif = std::move(p);

    m_CiblesPlanif.clear();
    for (const auto& s_Id : m_Planif.m_Cibles) {
        SObjectifCible c;
        c.m_Id = s_Id;
        if (const auto* e = m_Depot.Trouver(s_Id)) {
            c.m_Nom = NomLisible(e, {});
            c.m_Image = e->m_Image;
        }
        if (c.m_Nom.empty()) c.m_Nom = Tx("cible");
        m_CiblesPlanif.push_back(std::move(c));
    }

    m_Lieu = d.m_Nom;
    m_TitreMission = m.m_Titre;
}

// ------------------------------------------------------------ Code de contrat

// Le contrat proposé ou accepté, en JSON compact : mission, cibles (nom et
// photo compris : celui qui reçoit le code n'a peut-être jamais relevé ce lieu),
// méthodes et complications par leur clé, tenues, entrée et équipement par leur
// identifiant. Sous verrou.
std::string Roulette::CodeDuContrat() const {
    using nlohmann::json;
    if (!(m_Propose || m_Prepare) || m_IdContratPrepare.empty() || !m_TiragePrepare.m_Valide) return {};
    const auto& t = m_TiragePrepare;

    // Seulement les tenues imposées aux cibles (le code reste court).
    json s_Tenues = json::array();
    std::map<int, int> s_Renumero;
    for (const auto& c : t.m_Cibles) {
        if (c.m_Tenue < 0 || c.m_Tenue >= static_cast<int>(t.m_Tenues.size()) || s_Renumero.contains(c.m_Tenue)) continue;
        const auto& s = t.m_Tenues[c.m_Tenue];
        s_Renumero[c.m_Tenue] = static_cast<int>(s_Tenues.size());
        s_Tenues.push_back(s.m_EstCostume ? json { { "s", true } } : json { { "i", s.m_Id }, { "n", s.m_Nom }, { "im", s.m_Image } });
    }

    json s_Cibles = json::array();
    for (const auto& c : t.m_Cibles) {
        json s_Cible = { { "i", c.m_Id }, { "n", c.m_Nom }, { "im", c.m_Image } };
        if (!c.m_Officielle) s_Cible["o"] = false;
        if (c.m_Methode >= 0) s_Cible["me"] = Methodes()[c.m_Methode].m_Cle;
        if (const auto s_It = s_Renumero.find(c.m_Tenue); s_It != s_Renumero.end()) s_Cible["te"] = s_It->second;
        s_Cibles.push_back(std::move(s_Cible));
    }

    json s_Complications = json::array();
    for (const auto& c : t.m_Complications) s_Complications.push_back(Complications()[c.m_Index].m_Cle);

    const auto& p = t.m_Preparation;
    json s_Prep = json::object();
    if (!p.m_Entree.m_Id.empty()) s_Prep["e"] = p.m_Entree.m_Id;
    if (!p.m_Arme.m_Id.empty()) s_Prep["a"] = p.m_Arme.m_Id;
    if (!p.m_Objet.m_Id.empty()) s_Prep["o"] = p.m_Objet.m_Id;
    if (!p.m_Objet2.m_Id.empty()) s_Prep["o2"] = p.m_Objet2.m_Id;
    return EncoderContrat({
        { "v", 1 }, { "mo", CleMode(t.m_Mode) }, { "c", m_IdContratPrepare },
        { "cib", s_Cibles }, { "ten", s_Tenues }, { "k", s_Complications }, { "p", s_Prep },
    });
}

// Sous verrou.
bool Roulette::ImporterContrat(const std::string& p_Code) {
    using nlohmann::json;
    m_ErreurCode.clear();
    m_AvertissementCode.clear();

    const json s_Code = DecoderContrat(p_Code);
    if (s_Code.is_null()) {
        m_ErreurCode = Tx("code.illisible");
        Diag("Code de contrat illisible.");
        return false;
    }

    // La mission, parmi celles que ce joueur possède.
    const auto s_IdContrat = ChampTexte(s_Code, "c");
    int l = -1, m = -1;
    for (int i = 0; i < static_cast<int>(m_Destinations.size()) && l < 0; ++i) {
        for (int j = 0; j < static_cast<int>(m_Destinations[i].m_Missions.size()); ++j) {
            if (m_Destinations[i].m_Missions[j].m_Id == s_IdContrat) {
                l = i;
                m = j;
                break;
            }
        }
    }
    if (l < 0) {
        m_ErreurCode = Tx("code.mission");
        Diag("Code de contrat : mission {} absente des destinations de ce joueur.", s_IdContrat);
        return false;
    }

    m_Depot.Charger();
    ApprendreNoms();
    if (m_Images) m_Images->RelireService();
    if (m_ImagesMethodes.empty() || m_ImageCostume.empty()) ChoisirImagesObjets();

    m_DestLieu = l;
    m_DestMission = m;
    m_DestIdLieu = m_Destinations[l].m_Id;
    m_DestContrat = s_IdContrat;
    ++m_NumeroDestination;
    PreparerPlanification();

    STirage t;
    t.m_Mode = ModeDepuisCle(ChampTexte(s_Code, "mo"));

    for (const auto& s : s_Code.value("ten", json::array())) {
        STenue s_Tenue;
        s_Tenue.m_Id = ChampTexte(s, "i");
        s_Tenue.m_Nom = ChampTexte(s, "n");
        s_Tenue.m_Image = ChampTexte(s, "im");
        s_Tenue.m_EstCostume = ChampBooleen(s, "s");
        if (s_Tenue.m_EstCostume) {
            s_Tenue.m_Nom = Tx("costume47");
            s_Tenue.m_Image = m_ImageCostume;
        }
        t.m_Tenues.push_back(std::move(s_Tenue));
    }

    for (const auto& c : s_Code.value("cib", json::array())) {
        SObjectifCible s_Cible;
        s_Cible.m_Id = ChampTexte(c, "i");
        s_Cible.m_Nom = ChampTexte(c, "n");
        s_Cible.m_Image = ChampTexte(c, "im");
        s_Cible.m_Officielle = c.value("o", true);
        s_Cible.m_CleNom = CleNom(s_Cible.m_Nom);
        const auto s_Methode = ChampTexte(c, "me");
        for (size_t i = 0; i < Methodes().size(); ++i) {
            if (s_Methode == Methodes()[i].m_Cle) s_Cible.m_Methode = static_cast<int>(i);
        }
        const int s_Tenue = c.value("te", -1);
        s_Cible.m_Tenue = s_Tenue >= 0 && s_Tenue < static_cast<int>(t.m_Tenues.size()) ? s_Tenue : -1;
        if (!s_Cible.m_Id.empty()) t.m_Cibles.push_back(std::move(s_Cible));
    }

    for (const auto& k : s_Code.value("k", json::array())) {
        if (!k.is_string()) continue;
        for (size_t i = 0; i < Complications().size(); ++i) {
            if (k.get<std::string>() == Complications()[i].m_Cle) t.m_Complications.push_back({ static_cast<int>(i), EEtatObjectif::EnCours });
        }
    }

    if (t.m_Cibles.empty()) {
        m_ErreurCode = Tx("code.illisible");
        return false;
    }

    // Entrée et équipement : ceux de CE joueur, s'il les a débloqués.
    const auto& P = s_Code.contains("p") && s_Code["p"].is_object() ? s_Code["p"] : json::object();
    std::vector<std::string> s_Manquants;
    t.m_Preparation = PreparationDepuisIds(m_Planif, m_Inventaire, ChampTexte(P, "e"), ChampTexte(P, "a"),
                                           ChampTexte(P, "o"), ChampTexte(P, "o2"), s_Manquants);
    CompleterImposes(t.m_Preparation);
    if (!s_Manquants.empty()) {
        m_AvertissementCode = Tx("code.manquants");
        Diag("Code de contrat : {} élément(s) de la préparation non débloqué(s) chez ce joueur.", s_Manquants.size());
    }

    t.m_Valide = true;
    AppliquerCostume(t.m_Preparation);
    m_TiragePrepare = std::move(t);
    m_IdContratPrepare = m_Planif.m_IdContrat;
    ++m_NumeroTirage;

    if (m_Images) {
        for (const auto& c : m_TiragePrepare.m_Cibles) m_Images->Demander(c.m_Image);
        for (const auto& s : m_TiragePrepare.m_Tenues) m_Images->Demander(s.m_Image);
        for (const auto& c : m_TiragePrepare.m_Complications) m_Images->Demander(ImageComplication(Complications()[c.m_Index].m_Cle));
        const auto& p = m_TiragePrepare.m_Preparation;
        for (const auto* s_Impose : { &p.m_Entree, &p.m_Arme, &p.m_Objet, &p.m_Objet2 }) m_Images->Demander(s_Impose->m_Image);
    }

    m_Propose = true;
    m_Prepare = false;
    m_Tirage = m_TiragePrepare;
    Diag("Contrat importé par code : {} — {} ; {} cible(s), mode {}.", m_Lieu, m_TitreMission, m_Tirage.m_Cibles.size(),
         LibelleMode(m_Tirage.m_Mode));
    return true;
}

namespace {
    // Une touche, comme le clavier : code de balayage (le jeu lit les touches
    // ainsi), « étendue » pour les flèches et Page préc./suiv.
    void Touche(WORD p_Vk, int p_Apres = 450) {
        const bool s_Etendue = p_Vk == VK_LEFT || p_Vk == VK_RIGHT || p_Vk == VK_UP || p_Vk == VK_DOWN ||
                               p_Vk == VK_PRIOR || p_Vk == VK_NEXT;
        INPUT s_Entrees[2] {};
        for (int i = 0; i < 2; ++i) {
            s_Entrees[i].type = INPUT_KEYBOARD;
            s_Entrees[i].ki.wScan = static_cast<WORD>(MapVirtualKeyW(p_Vk, MAPVK_VK_TO_VSC));
            s_Entrees[i].ki.dwFlags = KEYEVENTF_SCANCODE | (s_Etendue ? KEYEVENTF_EXTENDEDKEY : 0) | (i ? KEYEVENTF_KEYUP : 0);
        }
        SendInput(1, &s_Entrees[0], sizeof(INPUT));
        Sleep(80);
        SendInput(1, &s_Entrees[1], sizeof(INPUT));
        Sleep(p_Apres);
    }
}

// Le chemin des menus, au clavier, vérifié en jeu le 23/11/2026 :
//   Échap ×3 ramène au menu principal (Échap n'y fait rien) ; Page préc. ×6
//   s'arrête sur « À la une » ; Page suiv. ×2 = « Destinations » ; Entrée ouvre
//   le lieu sélectionné, que le mod remplace par celui du contrat ; Entrée ouvre
//   sa mission (la seule) ; sur le briefing, Bas, Entrée, Droite, Entrée ouvre
//   « Lieu de départ », dont le curseur est posé sur l'entrée tirée ; Entrée la
//   choisit et revient à la préparation.
// Chaque page attendue est confirmée par sa réponse du serveur : sans elle, le
// pilote s'arrête là plutôt que d'appuyer à l'aveugle.
void Roulette::Piloter() {
    const auto Dire = [this](const std::string& p_Texte, bool p_Echec) {
        {
            std::lock_guard s_Verrou(m_Verrou);
            m_EtatPilote = p_Texte;
        }
        Diag("Pilote : {}", p_Texte);
        m_Fenetre.Pousser();
        (void)p_Echec;
    };

    // Le contrat tient toujours, et le jeu est devant (sinon on n'appuie plus).
    const auto Encore = [this] {
        {
            std::lock_guard s_Verrou(m_Verrou);
            if (!m_Prepare || m_EnMission) return false;
        }
        const HWND s_Jeu = Moteur::FenetreDuJeu();
        return s_Jeu && GetForegroundWindow() == s_Jeu;
    };

    const auto Attendre = [](std::atomic<int>& p_Compteur, int p_Avant, int p_Ms) {
        for (int t = 0; t < p_Ms; t += 100) {
            if (p_Compteur.load() > p_Avant) return true;
            Sleep(100);
        }
        return false;
    };

    Sleep(1300); // la fiche se ferme

    if (const HWND s_Jeu = Moteur::FenetreDuJeu()) SetForegroundWindow(s_Jeu);
    Sleep(300);
    if (!Encore()) return Dire(Tx("pilote.premier_plan"), true);

    Dire(Tx("pilote.en_route"), false);

    for (int i = 0; i < 3; ++i) Touche(VK_ESCAPE, 500);
    for (int i = 0; i < 6; ++i) Touche(VK_PRIOR, 300);
    for (int i = 0; i < 2; ++i) Touche(VK_NEXT, 650);
    Sleep(500);
    if (!Encore()) return Dire(Tx("pilote.interrompu"), true);

    int n = m_PagesLieu;
    Touche(VK_RETURN, 0);
    if (!Attendre(m_PagesLieu, n, 8000)) return Dire(Tx("pilote.lieu"), true);

    Sleep(1500);
    if (!Encore()) return Dire(Tx("pilote.interrompu"), true);
    n = m_PagesPrep;
    Touche(VK_RETURN, 0);
    if (!Attendre(m_PagesPrep, n, 10000)) return Dire(Tx("pilote.preparation"), true);

    // Plus de passage par « Lieu de départ » (24/11/2026) : l'entrée tirée est
    // écrite directement dans le jeu (Moteur::ImposerEntree), et l'écran
    // « Préparation » est retiré par le verrou du contrat (MandatoryLoadout).
    Dire(Tx("pilote.atteinte"), false);
}

bool Roulette::EntreeEnCostume(const SPreparation& p_Preparation) const {
    if (p_Preparation.m_Entree.Vide()) return false;
    for (const auto& e : m_Planif.m_Entrees) {
        if (e.m_Id == p_Preparation.m_Entree.m_Id) return e.m_EnCostume;
    }
    return false;
}

void Roulette::AppliquerCostume(SPreparation& p_Preparation) {
    p_Preparation.m_Costume = {};
    if (!EntreeEnCostume(p_Preparation)) return;

    // Le costume noir classique : présélectionné dans le menu, ou imposé.
    const auto s_It = std::ranges::find_if(m_Costumes, [](const auto& o) { return o.m_IdJeu == c_CostumeClassique; });
    if (s_It == m_Costumes.end()) return;

    SImpose& c = p_Preparation.m_Costume;
    c.m_Id = s_It->m_Id;
    c.m_IdJeu = s_It->m_IdJeu;
    c.m_Emplacement = "disguise";
    c.m_Nom = s_It->m_Nom;
    c.m_Cle = s_It->m_CleNom;
    c.m_Brut = s_It->m_Brut;
    if (const auto* e = m_Depot.Trouver(c.m_Id)) {
        c.m_Image = e->m_Image;
        if (c.m_Nom == c.m_IdJeu) c.m_Nom = NomCostume(e->m_NomCommun.empty() ? e->m_Nom : e->m_NomCommun);
    }
    if (m_Images) m_Images->Demander(c.m_Image);
}

void Roulette::OublierContrat() {
    Moteur::LibererEntree();
    m_PagesPrepAuLancement = -1;
    m_Propose = false;
    m_Prepare = false;
    m_Tirage = {};
    m_TiragePrepare = {};
    m_IdContratPrepare.clear();
    m_DestLieu = m_DestMission = -1;
    m_DestIdLieu.clear();
    m_DestContrat.clear();
    m_Lieu.clear();
    m_TitreMission.clear();
    ++m_NumeroTirage;
}

bool Roulette::TirerDestinationMaintenant() {
    int l = -1, m = -1;
    if (!TirerDestination(m_Destinations, m_Hasard, l, m, m_DestinationsExclues)) return false;

    m_DestLieu = l;
    m_DestMission = m;
    m_DestIdLieu = m_Destinations[l].m_Id;
    m_DestContrat = m >= 0 ? m_Destinations[l].m_Missions[m].m_Id : std::string();
    ++m_NumeroDestination;

    const auto& d = m_Destinations[l];
    if (m_Images) {
        m_Images->Demander(d.m_Fond);
        m_Images->Demander(d.m_Image);
        if (m >= 0) m_Images->Demander(d.m_Missions[m].m_Image);
    }

    Diag("Destination tirée : {}{}.", d.m_Nom, m >= 0 ? " — " + d.m_Missions[m].m_Titre : std::string(" (missions pas encore connues)"));
    return true;
}

void Roulette::RetrouverDestination() {
    m_DestLieu = m_DestMission = -1;
    for (size_t i = 0; i < m_Destinations.size(); ++i) {
        if (m_Destinations[i].m_Id != m_DestIdLieu) continue;
        m_DestLieu = static_cast<int>(i);
        const auto& s_Missions = m_Destinations[i].m_Missions;
        for (size_t j = 0; j < s_Missions.size(); ++j) {
            if (!m_DestContrat.empty() && s_Missions[j].m_Id == m_DestContrat) m_DestMission = static_cast<int>(j);
        }
    }
}

// ---------------------------------------------------------------- Noms

// Le nom dans la langue du jeu : traduit par le jeu s'il a le texte (et alors
// retenu), sinon celui déjà appris, sinon "".
std::string Roulette::TraduireNom(const std::string& p_Cle, const std::string& p_Cle2) {
    for (const auto* s_Cle : { &p_Cle, &p_Cle2 }) {
        if (s_Cle->empty()) continue;
        if (auto s_Texte = Moteur::Traduire(*s_Cle); !s_Texte.empty()) {
            auto s_Majuscules = *s_Cle;
            for (auto& c : s_Majuscules) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
            std::lock_guard s_Verrou(m_VerrouNoms);
            auto& s_Connu = m_NomsAppris[s_Majuscules];
            if (s_Connu != s_Texte) {
                s_Connu = s_Texte;
                m_NomsModifies = true;
            }
            return s_Texte;
        }
    }

    std::lock_guard s_Verrou(m_VerrouNoms);
    for (const auto* s_Cle : { &p_Cle, &p_Cle2 }) {
        if (s_Cle->empty()) continue;
        auto s_Majuscules = *s_Cle;
        for (auto& c : s_Majuscules) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
        if (const auto s_It = m_NomsAppris.find(s_Majuscules); s_It != m_NomsAppris.end()) return s_It->second;
    }
    return {};
}

// Julien (24/11/2026) : des objets s'affichaient « TOOL_GREENTINI_COOKIE_A »
// ou en anglais (« (Pistol) ICA 19 Short Baller »). Aux menus, le jeu n'a
// chargé qu'une partie de ses textes ; la page de préparation et la mission
// ont les autres. On traduit donc à chaque occasion — tirage, préparation,
// départ en mission —, on retient ce qui l'a été (names.json) et on renomme
// ce qui est déjà tiré.
void Roulette::ApprendreNoms() {
    int s_Traduits = 0, s_Restants = 0;

    for (auto& o : m_Inventaire) {
        if (auto t = TraduireNom(o.m_CleNom, o.m_Cle2); !t.empty()) {
            o.m_Nom = std::move(t);
            ++s_Traduits;
        }
        else {
            ++s_Restants;
        }
    }
    for (auto& e : m_EntreesPossedees) {
        if (auto t = TraduireNom(e.m_CleNom, {}); !t.empty()) e.m_Nom = std::move(t);
    }
    for (auto& o : m_Costumes) {
        if (auto t = TraduireNom(o.m_CleNom, o.m_Cle2); !t.empty()) o.m_Nom = std::move(t);
    }
    for (auto& e : m_Planif.m_Entrees) {
        if (auto t = TraduireNom(e.m_CleNom, {}); !t.empty()) e.m_Nom = std::move(t);
    }
    for (auto* s_Tirage : { &m_TiragePrepare, &m_Tirage }) {
        auto& p = s_Tirage->m_Preparation;
        for (auto* s_Impose : { &p.m_Entree, &p.m_Arme, &p.m_Objet, &p.m_Objet2 }) {
            if (s_Impose->Vide()) continue;
            if (auto t = TraduireNom(s_Impose->m_Cle, s_Impose->m_Cle2); !t.empty()) s_Impose->m_Nom = std::move(t);
        }
    }

    bool s_Enregistrer = false;
    size_t s_Appris = 0;
    {
        std::lock_guard s_Verrou(m_VerrouNoms);
        s_Enregistrer = m_NomsModifies;
        s_Appris = m_NomsAppris.size();
    }
    if (s_Enregistrer) EnregistrerNoms();

    static int s_Dernier = -1;
    if (s_Traduits != s_Dernier) {
        s_Dernier = s_Traduits;
        Diag("Noms : {} objet(s) dans la langue du jeu, {} pas encore ({} nom(s) appris en tout).", s_Traduits, s_Restants, s_Appris);
    }
}

// Sous verrou.
void Roulette::TerminerTirage() {
    const bool s_ACompter = m_TirageInjecte && m_Tirage.m_Valide && !m_Tirage.m_Terminee && m_Tirage.m_Mode == EMode::Puriste;
    Conclure(m_Tirage);
    if (!s_ACompter) return;

    m_SeriePuriste = m_Tirage.Bilan() == EEtatObjectif::Reussi ? m_SeriePuriste + 1 : 0;
    Diag("Puriste sans filet : contrat {} ; série : {}.", m_SeriePuriste > 0 ? "réussi" : "perdu", m_SeriePuriste);
    // Écrite ici, sans repasser par EnregistrerReglages (qui prend le verrou).
    const auto s_Fichier = Large(DossierDuMod() + "\\roulette47.ini");
    WritePrivateProfileStringW(L"roulette", L"purist_streak", std::to_wstring(m_SeriePuriste).c_str(), s_Fichier.c_str());
}

// Ce que Discord montre : où en est la roulette. Le chronomètre repart à
// chaque changement de phase (menus, contrat proposé, accepté, mission). Sous verrou.
SPresence Roulette::PresenceActuelle() {
    // Seulement en mission, et seulement avec un contrat Roulette 47 (Julien,
    // 25/11/2026) : partout ailleurs, la présence est effacée.
    SPresence p;
    if (!(m_EnMission && m_TirageInjecte && m_Tirage.m_Valide)) {
        m_PhasePresence.clear();
        return p;
    }

    // Le mod « DiscordRichPresence » de ZHMModSDK écrit la présence du même
    // processus : Roulette 47 renvoie la sienne toutes les 5 s pour garder la main.
    static bool s_AutrePresenceSignalee = false;
    if (!s_AutrePresenceSignalee && GetModuleHandleW(L"DiscordRichPresence.dll")) {
        s_AutrePresenceSignalee = true;
        Diag("Discord : le mod DiscordRichPresence de ZHMModSDK est chargé ; la présence de Roulette 47 est renvoyée toutes les 5 s pour passer devant.");
    }

    p.m_Details = TxF("discord.mission", Tx(std::string("discord.mode.") + CleMode(m_Tirage.m_Mode)));
    if (m_Tirage.m_Terminee) {
        p.m_Etat = Tx(m_Tirage.Bilan() == EEtatObjectif::Reussi ? "discord.reussi" : "discord.echoue");
    }
    else {
        const auto s_Restantes = std::ranges::count_if(m_Tirage.m_Cibles, [](const auto& c) { return c.m_Etat == EEtatObjectif::EnCours; });
        p.m_Etat = m_Lieu + (m_Lieu.empty() ? "" : " · ") + TxF("discord.cibles", std::to_string(s_Restantes));
    }

    // Le chronomètre part à l'entrée en mission.
    const std::string s_Phase = "mission" + m_IdLieu;
    if (s_Phase != m_PhasePresence) {
        m_PhasePresence = s_Phase;
        m_DebutPresence = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    }
    p.m_Debut = m_DebutPresence;
    return p;
}

void Roulette::ChargerEntreesReperees() {
    std::ifstream s_Flux(std::filesystem::path(DossierDuMod()) / "entrees_reperees.json", std::ios::binary);
    if (!s_Flux) return;
    const auto s_Json = nlohmann::json::parse(s_Flux, nullptr, false);
    if (!s_Json.is_array()) return;
    for (const auto& e : s_Json) if (e.is_string()) m_EntreesReperees.insert(e.get<std::string>());
}

// Sous verrou.
void Roulette::RetenirEntreeReperee(const std::string& p_IdEntree) {
    if (p_IdEntree.empty() || !m_EntreesReperees.insert(p_IdEntree).second) return;
    std::ofstream(std::filesystem::path(DossierDuMod()) / "entrees_reperees.json", std::ios::binary | std::ios::trunc)
        << nlohmann::json(m_EntreesReperees).dump(1);
}

void Roulette::ChargerNoms() {
    std::ifstream s_Flux(std::filesystem::path(DossierDuMod()) / "names.json", std::ios::binary);
    if (!s_Flux) return;

    const auto s_Json = nlohmann::json::parse(s_Flux, nullptr, false);
    if (!s_Json.is_object()) return;

    std::lock_guard s_Verrou(m_VerrouNoms);
    for (const auto& [k, v] : s_Json.items()) {
        if (v.is_string()) m_NomsAppris[k] = v.get<std::string>();
    }
    Diag("Noms appris gardés : {}.", m_NomsAppris.size());
}

void Roulette::EnregistrerNoms() {
    nlohmann::json s_Json = nlohmann::json::object();
    {
        std::lock_guard s_Verrou(m_VerrouNoms);
        for (const auto& [k, v] : m_NomsAppris) s_Json[k] = v;
        m_NomsModifies = false;
    }

    std::ofstream s_Flux(std::filesystem::path(DossierDuMod()) / "names.json", std::ios::binary | std::ios::trunc);
    s_Flux << s_Json.dump(1);
}

void Roulette::ChargerDestinations() {
    std::ifstream s_Flux(std::filesystem::path(DossierDuMod()) / "destinations.json", std::ios::binary);
    if (!s_Flux) return;

    const auto s_Json = nlohmann::json::parse(s_Flux, nullptr, false);
    if (s_Json.is_discarded()) return;

    std::lock_guard s_Verrou(m_Verrou);
    m_Destinations = DestinationsDepuisJson(s_Json);
    Diag("Destinations gardées : {} lieu(x).", m_Destinations.size());
}

void Roulette::EnregistrerDestinations() {
    std::ofstream(std::filesystem::path(DossierDuMod()) / "destinations.json", std::ios::binary)
        << DestinationsEnJson(m_Destinations).dump(1, ' ', false, nlohmann::json::error_handler_t::replace);
}

void Roulette::Journaliser(const std::string& p_Nom, const std::string& p_Json) {
    static const std::set<std::string> s_Suivis = {
        "Kill", "Pacify", "Disguise", "Spotted", "Witnesses", "BodyFound", "DeadBodySeen", "MurderedBodySeen",
        "AccidentBodyFound", "SecuritySystemRecorder", "CrowdNPC_Died", "ContractStart", "ContractEnd", "exit_gate",
    };

    if (!s_Suivis.contains(p_Nom)) {
        return;
    }

    std::lock_guard s_Verrou(m_VerrouJournal);

    if (m_Journal) {
        m_Journal << p_Json << '\n';
        m_Journal.flush();
    }
}

void Roulette::TraiterEvenement(const Glacier::Objet& p_Evenement) {
    // Le premier événement arrive dans le thread du jeu : c'est là qu'on
    // s'abonne à chaque image (l'abonnement doit se faire depuis ce thread).
    if (!m_AbonneImage) {
        m_AbonneImage = Moteur::AbonnerImage(&Roulette::SurImage);
    }

    try {
        const auto s_Evenement = VersJson(p_Evenement);
        const auto s_Nom = ChampTexte(s_Evenement, "Name");
        const auto s_Valeur = s_Evenement.contains("Value") ? s_Evenement["Value"] : nlohmann::json();
        const double s_Temps = s_Evenement.contains("Timestamp") && s_Evenement["Timestamp"].is_number()
                                   ? s_Evenement["Timestamp"].get<double>() : -1.0;

        Journaliser(s_Nom, s_Evenement.dump());

        static bool s_Premier = true;
        if (s_Premier) {
            s_Premier = false;
            Diag("Premier événement du jeu reçu : {}.", s_Nom);
        }
        if (s_Nom == "ContractStart" || s_Nom == "ContractEnd") Diag("Événement {}.", s_Nom);

        bool s_Change = false;

        {
            std::lock_guard s_Verrou(m_Verrou);

            if (s_Nom == "ContractStart") {
                // Tirage déjà écrit dans le contrat : on le garde (le niveau ne
                // sert plus qu'à relire PNJ et tenues pour la prochaine fois).
                if (!m_TirageInjecte) m_Tirage = {};
                m_DemandeTirage = m_TirageInjecte || m_Reglages.m_TirageAuto;
                // Julien (24/11/2026) : dans un lieu jamais joué, la roulette ne
                // tirait que les cibles du jeu. Les PNJ d'un lieu ne se lisent que
                // dans son niveau : toute mission lancée les relève désormais,
                // avec ou sans roulette.
                m_DemandeReleve = true;
                m_ContratLance = true;
                m_DernierEssaiNiveau = -1000.0;
                m_Rompu = false;
                s_Change = true;

                // L'arme et l'objet tirés à la préparation sont-ils emportés ?
                if (m_TirageInjecte && VerifierPreparation(m_Tirage, s_Valeur)) {
                    const auto& p = m_Tirage.m_Preparation;
                    Diag("Départ : arme imposée {}, objets imposés {} et {}.",
                         p.m_Arme.Vide() ? "aucune" : p.m_Arme.m_Etat == EEtatObjectif::Reussi ? "emportée" : "ABSENTE",
                         p.m_Objet.Vide() ? "aucun" : p.m_Objet.m_Etat == EEtatObjectif::Reussi ? "emporté" : "ABSENT",
                         p.m_Objet2.Vide() ? "aucun" : p.m_Objet2.m_Etat == EEtatObjectif::Reussi ? "emporté" : "ABSENT");
                }
            }
            else if (s_Nom == "ContractEnd") {
                TerminerTirage();
                s_Change = true;
            }
            else {
                s_Change = AppliquerEvenement(m_Tirage, s_Nom, s_Valeur, s_Temps);

                // Repéré dès le départ : la complication vient d'être écartée ;
                // l'entrée tirée ne la reverra plus.
                if (s_Nom == "Spotted" && s_Change && s_Temps >= 0.0 && s_Temps < c_GraceDepart) {
                    const auto& s_Entree = m_Tirage.m_Preparation.m_Entree;
                    Diag("Repéré {:.1f} s après le départ (entrée « {} ») : « Ne pas être repéré » écartée.", s_Temps, s_Entree.m_Nom);
                    RetenirEntreeReperee(s_Entree.m_Id);
                }
            }
        }

        // Une condition rompue pour de bon : la roulette acceptée, la mission est
        // perdue — la fiche reste ouverte jusqu'à ce qu'on la recommence ou la quitte.
        bool s_Rupture = false;

        if (s_Change) {
            std::lock_guard s_Verrou(m_Verrou);
            std::string s_Raison;

            if (m_Reglages.m_EchecImpose && m_EnMission && !m_Rompu && !m_Tirage.m_Terminee && Rompu(m_Tirage, s_Raison)) {
                m_Rompu = true;
                m_RaisonRupture = s_Raison;
                s_Rupture = true;
                Diag("Contrat rompu : {}", s_Raison);
            }
        }

        // (La fiche n'est plus imposée : Julien veut l'échec du jeu lui-même,
        // « Mission échouée », pas un écran du mod. Voir README, échec vanilla.)
        (void)s_Rupture;

        if (s_Change) {
            m_Fenetre.Pousser();
        }
    }
    catch (const std::exception& s_Exception) {
        Diag("Événement illisible : {}", s_Exception.what());
    }
}

void Roulette::ChaqueImage() {
    const double s_Maintenant = Maintenant();


    // Discord : la présence voulue, une fois par seconde.
    if (m_DiscordActif && s_Maintenant - m_DernierePresence > 1.0) {
        m_DernierePresence = s_Maintenant;
        SPresence s_Presence;
        {
            std::lock_guard s_Verrou(m_Verrou);
            s_Presence = PresenceActuelle();
        }
        m_Discord.Montrer(s_Presence);
    }

    // L'adresse du service d'images et sa signature se renouvellent.
    if (m_Images && s_Maintenant - m_DernierService > 5.0) {
        m_DernierService = s_Maintenant;
        m_Images->RelireService();
    }

    // En mission tant que le jeu a des cibles, ou, une fois le contrat lancé,
    // tant que le niveau a des personnages : certaines missions (Hawke's Bay)
    // ne déclarent aucune cible au gestionnaire du jeu en début de partie.
    size_t s_Personnages = 0;
    for (const auto& s_Ref : Glacier::Acteurs()) if (s_Ref.m_pInterface) ++s_Personnages;
    const bool s_AvecCibles = !Glacier::CiblesDuContrat().empty();
    if (s_Personnages == 0 && !s_AvecCibles) m_ContratLance = false;
    const bool s_EnMission = s_AvecCibles || (m_ContratLance && s_Personnages > 0);
    bool s_Change = false;

    {
        std::lock_guard s_Verrou(m_Verrou);

        if (s_EnMission != m_EnMission) {
            m_EnMission = s_EnMission;
            s_Change = true;
            Diag("{} ({} personnage(s), cibles du jeu : {}).", s_EnMission ? "Entrée en mission" : "Sortie de mission",
                 s_Personnages, s_AvecCibles ? "oui" : "non");

            // Les menus ne survivent peut-être pas au niveau : le navigateur sera
            // retenu de nouveau au premier lien du retour aux menus.
            if (s_EnMission) {
                Moteur::OublierNavigateur();
                Moteur::LibererEntree();
                ApprendreNoms();
            }

            if (!s_EnMission) {
                // Le niveau s'en va : on oublie ses PNJ sans plus les toucher.
                TerminerTirage();
                m_DemandeTirage = false;
                m_DemandeReleve = false;
                m_Pnj.clear();
                m_Marques.clear();
                m_Rompu = false;
                m_TirageInjecte = false;

                // Retour aux menus : le tirage de la mission finie est oublié
                // (Julien : l'encart des cibles restait affiché au menu).
                m_Tirage = {};
                m_Lieu.clear();
                m_TitreMission.clear();
                if (m_Prepare || m_Propose) {
                    m_Tirage = m_TiragePrepare;
                    if (m_DestLieu >= 0) {
                        m_Lieu = m_Destinations[m_DestLieu].m_Nom;
                        if (m_DestMission >= 0) m_TitreMission = m_Destinations[m_DestLieu].m_Missions[m_DestMission].m_Titre;
                    }
                }
            }
        }

        // Mission sans roulette : le niveau est seulement relevé (PNJ et tenues
        // du lieu, pour les tirages suivants) ; le reste de l'état est rendu tel quel.
        if (m_EnMission && m_DemandeReleve && !m_DemandeTirage) {
            const auto s_Avant = m_Tirage;
            const auto s_CiblesContrat = m_CiblesContrat;
            const bool s_Fait = PreparerNiveau();
            // Rendu même après une lecture ratée : PreparerNiveau vide le tirage,
            // et un niveau pas encore prêt est relu toutes les 5 s.
            m_Tirage = s_Avant;
            m_CiblesContrat = s_CiblesContrat;
            if (s_Fait) {
                m_DemandeReleve = false;
                Diag("Relevé du lieu {} : {} PNJ retenus pour les prochains tirages.", m_IdLieu, m_Pnj.size());
            }
        }

        if (m_EnMission && m_DemandeTirage) {
            Marquer(false); // un nouveau tirage remplace les PNJ marqués

            if (m_TirageInjecte) {
                // Le contrat porte déjà la roulette : relire le niveau pour la
                // mémoire du lieu, puis remettre le tirage tel qu'il a été écrit.
                const auto s_Injecte = m_Tirage;
                const bool s_Fait = PreparerNiveau();
                m_Tirage = s_Injecte; // même après une lecture ratée (voir plus haut)
                if (s_Fait) {
                    m_DemandeReleve = false;
                    for (auto& t : m_Tirage.m_Tenues) if (t.m_EstCostume) t.m_Image = m_ImageCostume;
                    if (m_Images) for (const auto& t : m_Tirage.m_Tenues) m_Images->Demander(t.m_Image);
                    m_DemandeTirage = false;
                    s_Change = true;
                }
            }
            else if (PreparerNiveau()) {
                m_DemandeReleve = false;
                ComposerCibles();
                Tirer(m_Tirage, m_Reglages, m_Hasard);
                Marquer(true);
                m_DemandeTirage = false;
                ++m_NumeroTirage;
                s_Change = true;

                Diag("Tirage : {} cible(s) dont {} PNJ, {} tenue(s) possibles.", m_Tirage.m_Cibles.size(),
                     m_Marques.size(), m_Tirage.m_Tenues.size());
            }
        }
    }

    if (s_Change) {
        if (!s_EnMission) {
            m_Fenetre.Rompre(false);
            m_Fenetre.Ouvrir(false);
        }
        m_Fenetre.Pousser();
    }
}

bool Roulette::PreparerNiveau() {
    if (Glacier::CiblesDuContrat().empty() && (!m_ContratLance || Glacier::Acteurs().empty())) {
        return false;
    }

    // Un niveau pas encore prêt (personnages en cours de chargement) est
    // relu toutes les 5 s, pas à chaque image : à Hawke's Bay, sans cible
    // déclarée, l'essai se répétait 300 fois par seconde.
    if (Maintenant() - m_DernierEssaiNiveau < 5.0) return false;

    // Un nouvel essai après un échec ne réécrit pas tout le relevé au journal.
    struct SMuet {
        explicit SMuet(bool p_Muet) { g_DiagMuet = p_Muet; }
        ~SMuet() { g_DiagMuet = false; }
    } s_Muet(m_DernierEssaiNiveau > 0.0);

    m_Depot.Charger();
    ChoisirImagesObjets();

    m_Tirage.m_Cibles.clear();
    m_Tirage.m_Tenues.clear();
    m_CiblesContrat.clear();
    m_Pnj.clear();

    // ------------------------------------------------ Mission
    if (const auto* s_Contexte = Glacier::Contexte()) {
        const auto s_IdLieu = VersTexte(s_Contexte->IdLieu());
        m_IdLieu = s_IdLieu;
        const auto s_Ville = Moteur::Traduire("UI_" + s_IdLieu + "_CITY");
        const auto s_Pays = Moteur::Traduire("UI_" + s_IdLieu + "_COUNTRY");
        m_Lieu = s_Ville.empty() ? s_IdLieu : (s_Pays.empty() ? s_Ville : s_Ville + ", " + s_Pays);

        const auto s_Contrat = VersJson(s_Contexte->Donnees());
        const auto s_Meta = s_Contrat.contains("Metadata") ? s_Contrat["Metadata"] : nlohmann::json::object();
        const auto s_CleTitre = ChampTexte(s_Meta, "Title");
        m_TitreMission = EstUneCle(s_CleTitre) ? Moteur::Traduire(s_CleTitre) : s_CleTitre;

        Diag("Mission : lieu « {} » ({}), titre « {} » ({}).", m_Lieu, s_IdLieu, m_TitreMission, s_CleTitre);
    }

    // ------------------------------------------------ Cibles
    std::set<std::string> s_Vues;

    for (const auto& s_Info : Glacier::CiblesDuContrat()) {
        SObjectifCible s_Cible;
        s_Cible.m_Id = s_Info.m_IdDepot.EnTexte();

        if (!s_Vues.insert(s_Cible.m_Id).second) {
            continue;
        }

        const auto* s_Entree = m_Depot.Trouver(s_Cible.m_Id);
        std::string s_NomActeur;

        if (s_Info.m_Acteur.m_pInterface) {
            s_NomActeur = VersTexte(s_Info.m_Acteur.m_pInterface->Nom());
        }

        s_Cible.m_Nom = NomLisible(s_Entree, { &s_NomActeur });
        if (s_Entree) s_Cible.m_Image = s_Entree->m_Image;

        Diag("Cible {} « {} », image « {} »", s_Cible.m_Id, s_Cible.m_Nom, s_Cible.m_Image);
        m_CiblesContrat.push_back(std::move(s_Cible));
    }

    // ------------------------------------------------ Tenues
    //
    // Le gestionnaire charge TOUTES les tenues que 47 peut porter : celles du
    // niveau, mais aussi ses costumes débloqués (v0.1 : « blacksuit » tiré comme
    // tenue) et quelques tenues spéciales (« Agent 47 » torse nu, « Agent
    // Smith », « NoName » — v0.2). Règle :
    //   - un costume de 47 n'est jamais une tenue : il devient l'entrée unique
    //     « Costume de 47 » ;
    //   - une tenue PORTÉE par un personnage du niveau est retenue ;
    //   - une tenue que personne ne porte (trouvée dans un placard, comme le
    //     Magicien vampire à Paris) est retenue si son nom interne a le même
    //     préfixe que celles du niveau (« Paris_… »). Les tenues d'événements
    //     (« FrenchMartini_… » : Agent Smith, Agent 47 torse nu, croupier,
    //     « Sécurité de Murillo ») ont un autre préfixe et restent dehors.
    //     (v0.3.2 écartait tout nom contenant « Unique » : le Magicien vampire,
    //     tenue de Paris, sortait avec.)
    std::set<std::string> s_Portees;
    std::set<std::string> s_Prefixes; // « paris » : préfixe des tenues portées dans le niveau

    const auto Prefixe = [](const std::string& p_NomCommun) {
        return EnMinuscules(p_NomCommun.substr(0, p_NomCommun.find('_')));
    };

    for (const auto& s_Ref : Glacier::Acteurs()) {
        const auto* s_Acteur = s_Ref.m_pInterface;
        if (!s_Acteur) continue;

        if (const auto* s_Kit = s_Acteur->TenuePortee()) {
            s_Portees.insert(s_Kit->Id().EnTexte());
            if (!s_Kit->CostumeDe47()) s_Prefixes.insert(Prefixe(VersTexte(s_Kit->NomCommun())));
        }
    }

    size_t s_Costumes = 0, s_Ecartees = 0;

    if (Glacier::Globales::Contenus) {
        s_Vues.clear();

        Glacier::PourChaqueTenue([&](const Glacier::Guid& s_IdKit, const Glacier::Tenue* s_Kit) {
            if (!s_Kit || !s_Kit->PortablePar47()) {
                return;
            }

            STenue s_Tenue;
            s_Tenue.m_Id = s_IdKit.EnTexte();

            if (!s_Vues.insert(s_Tenue.m_Id).second) {
                return;
            }

            const auto* s_Entree = m_Depot.Trouver(s_Tenue.m_Id);
            const auto s_NomCommun = VersTexte(s_Kit->NomCommun());
            const auto s_Titre = VersTexte(s_Kit->Titre());
            const auto s_Indices = EnMinuscules(s_NomCommun + " " + s_Titre + " " + (s_Entree ? s_Entree->m_Cle : ""));

            const bool s_EstCostume = s_Kit->CostumeDe47() || s_Kit->Type() == Glacier::Tenue::c_TypeCostume ||
                                      (s_Entree && s_Entree->m_EstCostume) || Contient(s_Indices, { "hero" });

            if (s_EstCostume) {
                ++s_Costumes;
                return;
            }

            const bool s_Portee = s_Portees.contains(s_Tenue.m_Id);
            const bool s_Suspecte = !s_Prefixes.contains(Prefixe(s_NomCommun)) ||
                                    Contient(s_Indices, { "reward", "noname", "naked", "agent47", "agent 47", "_47_" }) ||
                                    (s_Entree && s_Entree->m_Image.find("unlockables_override") != std::string::npos);

            s_Tenue.m_Nom = NomLisible(s_Entree, { &s_Titre, &s_NomCommun });
            if (s_Entree) s_Tenue.m_Image = s_Entree->m_Image;

            Diag("  tenue {} « {} » ({} / {}) : {}{}", s_Tenue.m_Id, s_Tenue.m_Nom, s_NomCommun, s_Titre,
                 s_Portee ? "portée" : "non portée", (!s_Portee && s_Suspecte) ? ", ÉCARTÉE" : "");

            if (!s_Portee && s_Suspecte) {
                ++s_Ecartees;
                return;
            }

            m_Tirage.m_Tenues.push_back(std::move(s_Tenue));
        });

        std::ranges::sort(m_Tirage.m_Tenues, {}, &STenue::m_Nom);

        STenue s_Costume;
        s_Costume.m_Id = "costume_47";
        s_Costume.m_Nom = Tx("costume47");
        s_Costume.m_Image = m_ImageCostume;
        s_Costume.m_EstCostume = true;
        m_Tirage.m_Tenues.push_back(std::move(s_Costume));

        std::string s_ListePrefixes;
        for (const auto& p : s_Prefixes) s_ListePrefixes += p + " ";
        Diag("{} tenue(s) retenue(s), {} costume(s) de 47 et {} tenue(s) spéciale(s) écartés ; {} portée(s) dans le niveau (préfixes : {}).",
             m_Tirage.m_Tenues.size() - 1, s_Costumes, s_Ecartees, s_Portees.size(), s_ListePrefixes);
    }

    // Les images partent tout de suite en téléchargement : la fiche les trouvera prêtes.
    if (m_Images) {
        const auto Repli = [&](const std::string& p_Id) {
            const auto* e = m_Depot.Trouver(p_Id);
            return e ? e->m_Tile : std::string();
        };

        for (const auto& c : m_CiblesContrat) m_Images->Demander(c.m_Image, Repli(c.m_Id));
        for (const auto& t : m_Tirage.m_Tenues) m_Images->Demander(t.m_Image, Repli(t.m_Id));
        for (const auto& i : m_ImagesMethodes) m_Images->Demander(i);
    }

    // ------------------------------------------------ PNJ candidats
    //
    // Tout personnage vivant du niveau qui a un nom et n'est ni une cible du
    // contrat ni un figurant de la foule. Photo : la fiche du dépôt à son nom
    // si elle existe (vrai portrait), sinon l'image de sa tenue.
    if (Glacier::Globales::Personnages) {
        // Les fiches du dépôt qui ont un portrait, par nom réduit.
        std::unordered_map<std::string, const EntreeDepot*> s_Portraits;

        for (const auto& [s_Id, e] : m_Depot.Entrees()) {
            if (e.m_Image.find("images/actors/") != std::string::npos && !EstUneCle(e.m_Nom)) {
                s_Portraits.emplace(CleNom(e.m_Nom), &e);
            }
        }

        std::set<std::string> s_NomsVus;
        size_t s_AvecPortrait = 0;

        size_t s_Presents = 0, s_Morts = 0, s_Figurants = 0, s_Cibles = 0;
        std::string s_Exemples;

        for (const auto& s_Ref : Glacier::Acteurs()) {
            auto* s_Acteur = s_Ref.m_pInterface;
            if (!s_Acteur) continue;
            ++s_Presents;
            if (s_Exemples.size() < 300) s_Exemples += " | " + VersTexte(s_Acteur->Nom());

            if (!s_Acteur->Vivant()) { ++s_Morts; continue; }
            if (s_Acteur->Figurant()) { ++s_Figurants; continue; }
            if (s_Acteur->CibleDuContrat()) { ++s_Cibles; continue; }

            SPnj s_Pnj;
            s_Pnj.m_Nom = VersTexte(s_Acteur->Nom());
            s_Pnj.m_Acteur = s_Acteur;

            const auto s_Cle = CleNom(s_Pnj.m_Nom);
            if (s_Cle.size() < 3 || !s_NomsVus.insert(s_Cle).second) continue;

            if (const auto s_It = s_Portraits.find(s_Cle); s_It != s_Portraits.end()) {
                s_Pnj.m_Image = s_It->second->m_Image;
                s_Pnj.m_Portrait = true;
                s_Pnj.m_IdDepot = s_It->second->m_Id;
                // Le nom interne du personnage perd ses accents (« Emiliano
                // K?mpfer ») ; la fiche du dépôt les a.
                if (!EstUneCle(s_It->second->m_Nom)) s_Pnj.m_Nom = s_It->second->m_Nom;
                ++s_AvecPortrait;
            }
            else if (const auto* s_Kit = s_Acteur->TenuePortee()) {
                if (const auto* e = m_Depot.Trouver(s_Kit->Id().EnTexte())) s_Pnj.m_Image = e->m_Image;
            }

            m_Pnj.push_back(std::move(s_Pnj));
        }

        if (m_Pnj.empty()) {
            Diag("Aucun PNJ : {} personnage(s), dont {} non vivant(s), {} figurant(s), {} cible(s) ; noms :{}",
                 s_Presents, s_Morts, s_Figurants, s_Cibles, s_Exemples);
        }
        Diag("{} PNJ peuvent devenir cibles, dont {} avec un portrait du dépôt.", m_Pnj.size(), s_AvecPortrait);
        RetenirPnjDuLieu();
    }

    // Réussi s'il y a des cibles, ou au moins des PNJ (mission sans cible déclarée).
    const bool s_Pret = !m_CiblesContrat.empty() || !m_Pnj.empty();
    m_DernierEssaiNiveau = s_Pret ? -1000.0 : Maintenant();
    return s_Pret;
}

// Les cibles du contrat, puis les PNJ tirés au sort (thread du jeu, sous verrou).
void Roulette::ComposerCibles() {
    m_Tirage.m_Cibles = m_CiblesContrat;

    std::vector<size_t> s_Indices;
    for (size_t i = 0; i < m_Pnj.size(); ++i) {
        // Sans photo sur le service d'IOI : jamais tiré.
        if (m_Images && m_Images->Introuvable(m_Pnj[i].m_Image)) continue;
        s_Indices.push_back(i);
    }
    std::ranges::shuffle(s_Indices, m_Hasard);

    const auto s_Nombre = s_Indices.size(); // tous les PNJ du niveau sont candidats

    for (size_t k = 0; k < s_Nombre; ++k) {
        const auto& s_Pnj = m_Pnj[s_Indices[k]];

        SObjectifCible s_Cible;
        s_Cible.m_Officielle = false;
        s_Cible.m_Nom = s_Pnj.m_Nom;
        s_Cible.m_CleNom = CleNom(s_Pnj.m_Nom);
        s_Cible.m_Image = s_Pnj.m_Image;
        s_Cible.m_Id = "pnj:" + s_Cible.m_CleNom;
        m_Tirage.m_Cibles.push_back(std::move(s_Cible));
    }

    // Entre 1 et 3 cibles en tout (réglages) : seuls les PNJ retenus sont marqués.
    ChoisirCibles(m_Tirage, m_Reglages, m_Hasard);
    m_Marques.clear();

    for (const auto& c : m_Tirage.m_Cibles) {
        if (c.m_Officielle) continue;
        for (size_t k = 0; k < s_Nombre; ++k) {
            const auto& s_Pnj = m_Pnj[s_Indices[k]];
            if ("pnj:" + CleNom(s_Pnj.m_Nom) != c.m_Id) continue;
            m_Marques.push_back(s_Pnj.m_Acteur);
            if (m_Images) m_Images->Demander(s_Pnj.m_Image);
            Diag("PNJ tiré comme cible : « {} » ({})", s_Pnj.m_Nom, s_Pnj.m_Portrait ? "portrait" : "image de sa tenue");
            break;
        }
    }
}

// Les PNJ tirés deviennent des cibles aux yeux du joueur : halo rouge en mode
// Instinct (m_bContractTarget) et icône de cible sur la carte
// (m_bContractTargetLive). Deux drapeaux du personnage, côté client : le contrat
// du serveur n'est pas touché. Le halo se met à jour au prochain rafraîchissement
// du personnage (le jeu le recalcule de lui-même).
void Roulette::Marquer(bool p_Marquer) {
    for (auto* s_Acteur : m_Marques) {
        if (!s_Acteur) continue;
        s_Acteur->MarquerCible(p_Marquer);
    }

    if (!p_Marquer) {
        m_Marques.clear();
    }
}

// Retail/Roulette47/locations/<LOCATION>.json : les PNJ vus dans ce lieu, pour tirer
// des cibles AVANT d'y entrer la fois suivante.
// Écrit aussi un lieu sans PNJ nommé, pourvu qu'il ait des tenues : à Nightcall
// (Hawke's Bay), les 35 personnages de la liste du niveau sont l'équipe de la
// mission de Jacksepticeye, désactivée ; seuls la cible et ses gardes sans nom
// sont là. Le fichier garde alors les tenues, que le tirage d'avant-mission
// peut proposer (26/11/2026).
void Roulette::RetenirPnjDuLieu() {
    const bool s_Tenues = std::ranges::any_of(m_Tirage.m_Tenues, [](const STenue& t) { return !t.m_EstCostume; });
    if (m_IdLieu.empty() || (m_Pnj.empty() && !s_Tenues)) return;

    nlohmann::json s_Liste = { { "pnj", nlohmann::json::array() }, { "tenues", nlohmann::json::array() } };

    for (const auto& p : m_Pnj) {
        s_Liste["pnj"].push_back({ { "nom", p.m_Nom }, { "image", p.m_Image }, { "id", p.m_IdDepot } });
    }

    for (const auto& t : m_Tirage.m_Tenues) {
        if (!t.m_EstCostume) s_Liste["tenues"].push_back({ { "id", t.m_Id }, { "nom", t.m_Nom }, { "image", t.m_Image } });
    }

    const auto s_Dossier = std::filesystem::path(DossierDuMod()) / "locations";
    std::error_code s_Erreur;
    std::filesystem::create_directories(s_Dossier, s_Erreur);

    std::string s_Fichier = m_IdLieu;
    for (auto& c : s_Fichier) if (!isalnum(static_cast<unsigned char>(c)) && c != '_') c = '_';

    std::ofstream(s_Dossier / (s_Fichier + ".json"), std::ios::binary) << s_Liste.dump(1);
}

// Une image du jeu pour chaque méthode : celle d'un objet du dépôt qui la
// représente (la corde de piano, le Jaeger 7, l'ICA 19…).
//
// Le dépôt contient des dizaines de variantes par arme (dorée, de PNJ, d'essai,
// de récompense…) : prendre le premier objet d'une catégorie donnait un ICA 19
// « GoldBaller », une boîte en polystyrène empoisonnable pour « Poison », une
// canette pour la mêlée (essai du 22/11/2026). Chaque méthode a donc des
// recherches par ordre de préférence (tous les morceaux doivent figurer dans le
// nom commun, normalisé) ; les variantes sont écartées ; parmi ce qui reste, le
// nom le plus court gagne (« ICA 19 » plutôt que « ICA 19 Chrome Covert »).
// Les accidents et les mains nues n'ont pas d'objet : ils gardent leur icône.
// Tout est journalisé, avec les autres candidats, pour corriger un choix.
void Roulette::ChoisirImagesObjets() {
    struct SChoix {
        const char* m_Cle;
        std::vector<std::vector<const char*>> m_Recherches;
    };

    static const std::vector<SChoix> s_Choix = {
        { "poison", { { "lethal", "poison", "vial" }, { "lethal", "pills" }, { "lethal", "syringe" } } },
        { "poison_ingere", { { "lethal", "poison", "vial" }, { "lethal", "pills" }, { "lethal", "pill" } } },
        { "poison_injecte", { { "lethal", "syringe" } } },
        { "pistolet", { { "silverballer" }, { "ica 19" } } },
        { "pm", { { "hx 10" }, { "tac smg" }, { "smg" } } },
        { "pompe", { { "bartoli 12g" }, { "bartoli" }, { "enram" } } },
        { "assaut", { { "tac 4" }, { "shashka" } } },
        { "precision", { { "jaeger 7" }, { "jaeger" }, { "sieger" } } },
        { "explosif", { { "ica remote explosive" }, { "remote explosive" } } },
        { "explosion_accident", { { "propane flask" }, { "propane" } } },
        // La corde de piano classique d'abord (« Melee_FiberWireClassic ») :
        // le nom le plus court donnait la variante « (Fiberwire) Rope ».
        { "corde", { { "fiberwireclassic" }, { "melee", "fiberwire" }, { "fiber wire" }, { "fiberwire" }, { "garrote" } } },
        { "melee", { { "kitchen knife" }, { "katana" }, { "hatchet" } } },
        { "electrocution", { { "electrocution" } } },
    };

    // Variantes à ne jamais montrer.
    static const std::vector<const char*> s_Exclus = {
        "gold", "test", "npc", "poisonable", "easter", "evergreen", "antique", "emetic", "sedative",
        "reward", "detonator", "ammo", "magazine", "briefcase", "container", "dlc", "jungle",
    };

    // Nom commun normalisé : minuscules, « _ » « - » « ( ) » remplacés par des espaces.
    const auto Normaliser = [](const std::string& p_Nom) {
        std::string s_Nom = " " + EnMinuscules(p_Nom) + " ";
        for (auto& c : s_Nom) if (c == '_' || c == '-' || c == '(' || c == ')' || c == '/') c = ' ';
        std::string s_Propre;
        for (char c : s_Nom) if (!(c == ' ' && !s_Propre.empty() && s_Propre.back() == ' ')) s_Propre += c;
        return s_Propre;
    };

    if (m_ImagesMethodes.empty()) {
        // Les fiches qui ont une image et un nom, triées pour un choix stable.
        std::vector<std::pair<std::string, const EntreeDepot*>> s_Objets;

        for (const auto& [s_Id, s_Entree] : m_Depot.Entrees()) {
            if (s_Entree.m_Image.empty() || s_Entree.m_NomCommun.empty() || s_Entree.m_EstCostume) continue;

            auto s_Nom = Normaliser(s_Entree.m_NomCommun);
            const bool s_Exclu = std::ranges::any_of(s_Exclus, [&](const char* x) { return s_Nom.find(x) != std::string::npos; });
            if (!s_Exclu) s_Objets.emplace_back(std::move(s_Nom), &s_Entree);
        }

        std::ranges::sort(s_Objets, [](const auto& a, const auto& b) {
            return a.first.size() != b.first.size() ? a.first.size() < b.first.size() : a.second->m_Id < b.second->m_Id;
        });

        m_ImagesMethodes.assign(Methodes().size(), {});

        for (size_t i = 0; i < Methodes().size(); ++i) {
            const auto s_It = std::ranges::find_if(s_Choix, [&](const SChoix& c) { return std::string_view(c.m_Cle) == Methodes()[i].m_Cle; });
            if (s_It == s_Choix.end()) continue;

            std::vector<const EntreeDepot*> s_Candidats;

            for (const auto& s_Recherche : s_It->m_Recherches) {
                for (const auto& [s_Nom, e] : s_Objets) {
                    const bool s_Tout = std::ranges::all_of(s_Recherche, [&](const char* m) {
                        return s_Nom.find(std::string(" ") + m) != std::string::npos;
                    });
                    if (s_Tout) s_Candidats.push_back(e);
                }
                if (!s_Candidats.empty()) break;
            }

            if (!s_Candidats.empty()) m_ImagesMethodes[i] = s_Candidats.front()->m_Image;

            std::string s_Autres;
            for (size_t k = 1; k < s_Candidats.size() && k < 5; ++k) s_Autres += " | " + s_Candidats[k]->m_NomCommun;

            Diag("Image de « {} » : {} ({}){}{}", Methodes()[i].m_Libelle,
                 s_Candidats.empty() ? "aucune, icône" : s_Candidats.front()->m_NomCommun,
                 s_Candidats.empty() ? "" : s_Candidats.front()->m_Image, s_Autres.empty() ? "" : " ; autres :", s_Autres);
        }
    }

    // Pas d'objet du dépôt pour la méthode (« Accident », « Noyade »…) : l'image
    // des défis génériques du jeu (Julien, 25/11/2026 : plus de case vide).
    {
        const auto Defi = [](const char* p_Nom) { return std::string("images/challenges/profile_challenges/generic_location_47_") + p_Nom + ".jpg"; };
        const std::pair<const char*, std::string> s_Replis[] = {
            { "accident", Defi("accident") }, { "electrocution", Defi("accident") }, { "chute", Defi("accident") },
            { "objet_suspendu", Defi("accident") }, { "brulure", Defi("accident") }, { "explosion_accident", Defi("accident") },
            { "noyade", Defi("drown") }, { "poison", Defi("poison") }, { "poison_ingere", Defi("poison") },
            { "poison_injecte", Defi("poison") }, { "corde", Defi("fiberwire") },
        };
        for (size_t i = 0; i < Methodes().size() && i < m_ImagesMethodes.size(); ++i) {
            if (!m_ImagesMethodes[i].empty()) continue;
            for (const auto& [s_Cle, s_Image] : s_Replis) {
                if (std::string_view(Methodes()[i].m_Cle) == s_Cle) m_ImagesMethodes[i] = s_Image;
            }
        }
    }

    // Le costume de 47 : toujours son costume d'origine (Julien, 24/11/2026),
    // quel que soit celui avec lequel la dernière mission a commencé. Repli :
    // parmi les fiches « signature », le nom le plus court (les variantes,
    // « with Gloves »…, l'allongent).
    m_ImageCostume.clear();
    {
        const EntreeDepot* s_Meilleur = nullptr;
        std::string s_Candidats;
        for (const auto& [s_Id, e] : m_Depot.Entrees()) {
            if (!e.m_EstCostume || e.m_Image.empty()) continue;
            const auto s_Nom = EnMinuscules(e.m_NomCommun);
            if (s_Nom.find("signature") == std::string::npos) continue;
            s_Candidats += " | " + e.m_NomCommun + " (" + e.m_Id + ")";
            if (!s_Meilleur || e.m_NomCommun.size() < s_Meilleur->m_NomCommun.size() ||
                (e.m_NomCommun.size() == s_Meilleur->m_NomCommun.size() && e.m_Id < s_Meilleur->m_Id)) {
                s_Meilleur = &e;
            }
        }
        // « HitmanSuit » : le costume noir, chemise blanche, cravate rouge,
        // celui de l'image « 47_outfits_signaturesuit ». Les fiches nommées
        // « Signature Suit » montrent parfois un col roulé.
        if (const auto* e = m_Depot.Trouver("4fc9396e-2619-4e66-a51e-2bd366230da7"); e && !e->m_Image.empty()) s_Meilleur = e;
        if (s_Meilleur) m_ImageCostume = s_Meilleur->m_Image;
        Diag("Costume de 47 : « {} » parmi{}", s_Meilleur ? s_Meilleur->m_NomCommun : std::string("aucun"), s_Candidats);
    }

    Diag("Image du costume de 47 : « {} »", m_ImageCostume);
}

// ------------------------------------------------------------ Thread de l'interface

std::string Roulette::EtatJson(bool p_Ouverte) {
    using nlohmann::json;
    std::lock_guard s_Verrou(m_Verrou);

    // L'arme à feu tirée à la préparation : sous une cible à tuer avec ce type
    // d'arme, c'est sa photo qui s'affiche (Julien, 24/11/2026), pas celle
    // d'un modèle générique.
    std::string s_ImageArme, s_TypeArme;
    if (const auto& a = m_Tirage.m_Preparation.m_Arme; !a.Vide() && !a.m_Image.empty()) {
        if (const auto* e = m_Depot.Trouver(a.m_Id)) {
            s_ImageArme = a.m_Image;
            s_TypeArme = e->m_Categorie;
        }
    }

    const auto MethodeJson = [&, this](int i, bool p_ArmeTiree = false) -> json {
        if (i < 0) return nullptr;
        const auto& m = Methodes()[i];
        auto s_Image = i < static_cast<int>(m_ImagesMethodes.size()) ? m_ImagesMethodes[i] : std::string();
        if (p_ArmeTiree && !s_ImageArme.empty() && *m.m_Large && s_TypeArme == m.m_Large) s_Image = s_ImageArme;
        return { { "cle", m.m_Cle }, { "libelle", Tx(std::string("methode.") + m.m_Cle) },
                 { "famille", Tx(std::string("famille.") + m.m_Famille) }, { "familleCle", m.m_Famille }, { "image", s_Image } };
    };

    const auto TenueJson = [](const STenue& t) -> json {
        return { { "nom", t.m_Nom }, { "image", t.m_Image }, { "costume", t.m_EstCostume } };
    };

    json s_Etat;
    s_Etat["ouverte"] = p_Ouverte;
    s_Etat["base"] = "https://cache.roulette.mod/";
    s_Etat["images"] = m_ImagesArrivees.load();
    s_Etat["tirage"] = m_NumeroTirage;
    s_Etat["mission"] = { { "lieu", m_Lieu }, { "titre", m_TitreMission } };
    s_Etat["enLigne"] = m_ReponsesServeur.load() > 0;

    if (!m_EnMission && m_Propose) s_Etat["etat"] = "propose";
    else if (!m_EnMission && m_Prepare) s_Etat["etat"] = "accepte";
    else if (!m_EnMission) s_Etat["etat"] = "hors_mission";
    else if (!m_Tirage.m_Valide) s_Etat["etat"] = "attente";
    else if (!m_Tirage.m_Terminee) s_Etat["etat"] = m_Tirage.Bilan() == EEtatObjectif::Echoue ? "echoue" : "en_cours";
    else s_Etat["etat"] = EtatTexte(m_Tirage.Bilan());

    s_Etat["enMission"] = m_EnMission;
    s_Etat["codeContrat"] = CodeDuContrat();
    s_Etat["erreurCode"] = m_ErreurCode;
    s_Etat["avertissementCode"] = m_AvertissementCode;
    s_Etat["pilote"] = m_EtatPilote;
    // « attente » : contrat accepté, préparation pas encore ouverte (écran de
    // chargement) ; « prete » : elle vient de s'ouvrir, la fiche se ferme.
    if (const int n = m_PagesPrepAuLancement.load(); n >= 0 && m_Prepare && !m_EnMission) {
        s_Etat["lancement"] = m_PagesPrep.load() > n ? "prete" : "attente";
    }
    else {
        s_Etat["lancement"] = nullptr;
    }
    s_Etat["piloteEnCours"] = m_PiloteEnCours.load();
    s_Etat["rompu"] = m_Rompu;
    s_Etat["dansLeContrat"] = m_TirageInjecte;
    s_Etat["raison"] = m_RaisonRupture;
    s_Etat["relancePossible"] = (!m_EnMission && (m_Prepare || m_Propose)) ||
                                (!m_Rompu && !m_TirageInjecte && m_EnMission && !m_Tirage.m_UneEliminationFaite && !m_Tirage.m_Terminee);

    json s_Cibles = json::array();

    for (const auto& c : m_Tirage.m_Cibles) {
        s_Cibles.push_back({
            { "nom", c.m_Nom },
            { "image", c.m_Image },
            { "etat", EtatTexte(c.m_Etat) },
            { "methode", MethodeJson(c.m_Methode, true) },
            { "tenue", c.m_Tenue >= 0 && c.m_Tenue < static_cast<int>(m_Tirage.m_Tenues.size())
                           ? TenueJson(m_Tirage.m_Tenues[c.m_Tenue]) : json(nullptr) },
            { "detail", c.m_Detail },
            { "officielle", c.m_Officielle },
        });
    }

    s_Etat["cibles"] = s_Cibles;

    // ---- Préparation tirée avant la mission.
    const auto ImposeJson = [](const SImpose& i) -> json {
        if (i.Vide()) return nullptr;
        return { { "nom", i.m_Nom }, { "image", i.m_Image }, { "etat", EtatTexte(i.m_Etat) } };
    };

    const auto& s_Prep = m_Tirage.m_Preparation;
    s_Etat["preparation"] = s_Prep.Vide() ? json(nullptr) : json {
        { "entree", ImposeJson(s_Prep.m_Entree) }, { "arme", ImposeJson(s_Prep.m_Arme) }, { "objet", ImposeJson(s_Prep.m_Objet) },
        { "objet2", ImposeJson(s_Prep.m_Objet2) } };
    s_Etat["modeContrat"] = m_Tirage.m_Valide ? json(CleMode(m_Tirage.m_Mode)) : json(nullptr);

    // ---- Difficulté estimée du contrat (Tirage.h, EvaluerDifficulte).
    s_Etat["difficulte"] = nullptr;
    if (m_Tirage.m_Valide && !m_Tirage.m_Cibles.empty()) {
        // Ce que l'équipement imposé apporte, en catégories de méthode.
        std::set<std::string> s_Fournis;
        for (const auto* s : { &s_Prep.m_Arme, &s_Prep.m_Objet, &s_Prep.m_Objet2 }) {
            if (s->Vide()) continue;
            const auto s_IdJeu = EnMinuscules(s->m_IdJeu);
            // La corde (PROP_MELEE_CLASSIC_FIBER_WIRE, TOKEN_FIBERWIRE) est rangée
            // en « melee » : elle ne fournit pas pour autant une arme de mêlée.
            if (s_IdJeu.find("fiberwire") != std::string::npos || s_IdJeu.find("fiber_wire") != std::string::npos) {
                s_Fournis.insert("fiberwire");
                continue;
            }
            if (const auto* e = m_Depot.Trouver(s->m_Id); e && !e->m_Categorie.empty()) s_Fournis.insert(e->m_Categorie);
            if (s_IdJeu.find("syringe") != std::string::npos && s_IdJeu.find("lethal") != std::string::npos) s_Fournis.insert("injected_poison");
            else if (s_IdJeu.find("lethal") != std::string::npos && s_IdJeu.find("poison") != std::string::npos) s_Fournis.insert("consumed_poison");
        }
        // Sans préparation imposée, le joueur a son propre équipement.
        const bool s_ArmeDispo = s_Prep.Vide() || !s_Prep.m_Arme.Vide();
        const auto d = EvaluerDifficulte(m_Tirage, s_Fournis, s_ArmeDispo);

        json s_Facteurs = json::array();
        for (const auto& f : d.m_Facteurs) {
            using EType = SFacteurDifficulte::EType;
            std::string s_Texte;
            switch (f.m_Type) {
                case EType::Methode:
                    s_Texte = Tx(std::string("methode.") + Methodes()[f.m_Index].m_Cle);
                    break;
                case EType::Tenue:
                    s_Texte = m_Tirage.m_Tenues[f.m_Index].m_EstCostume ? Tx("costume47") : m_Tirage.m_Tenues[f.m_Index].m_Nom;
                    break;
                case EType::Complication:
                    s_Texte = Tx(std::string("compl.") + Complications()[f.m_Index].m_Cle + ".titre");
                    break;
                case EType::Pnj:
                    s_Texte = Tx("diff.pnj");
                    break;
                case EType::ArmeATrouver:
                    s_Texte = Tx("diff.arme");
                    break;
            }
            s_Facteurs.push_back({ { "texte", s_Texte }, { "cible", f.m_Cible }, { "points", std::round(f.m_Points * 10.0) / 10.0 } });
            if (s_Facteurs.size() >= 4) break;
        }
        s_Etat["difficulte"] = { { "note", d.m_Note }, { "points", std::round(d.m_Points * 10.0) / 10.0 }, { "facteurs", s_Facteurs } };
    }

    // ---- Destination tirée parmi celles que le joueur possède.
    json s_Possedees = json::array();
    for (const auto& d : m_Destinations) {
        if (!d.m_Verrouillee) {
            s_Possedees.push_back({ { "id", d.m_Id }, { "nom", d.m_Nom }, { "image", d.m_Image },
                                    { "active", !m_DestinationsExclues.contains(d.m_Id) } });
        }
    }

    json s_Destination = nullptr;
    if (m_DestLieu >= 0 && m_DestLieu < static_cast<int>(m_Destinations.size())) {
        const auto& d = m_Destinations[m_DestLieu];
        json s_Mission = nullptr;
        if (m_DestMission >= 0 && m_DestMission < static_cast<int>(d.m_Missions.size())) {
            const auto& m = d.m_Missions[m_DestMission];
            s_Mission = { { "titre", m.m_Titre }, { "image", m.m_Image } };
        }
        s_Destination = { { "nom", d.m_Nom }, { "image", d.m_Image }, { "fond", d.m_Fond }, { "mission", s_Mission },
                          { "missionsConnues", !d.m_Missions.empty() } };
    }

    s_Etat["destination"] = s_Destination;
    {
        int l = -1, m = -1;
        std::mt19937 s_Essai(1);
        s_Etat["tirable"] = TirerDestination(m_Destinations, s_Essai, l, m, m_DestinationsExclues);
    }
    s_Etat["numeroDestination"] = m_NumeroDestination;
    s_Etat["destinations"] = s_Possedees;

    json s_Complications = json::array();

    for (const auto& c : m_Tirage.m_Complications) {
        const auto& d = Complications()[c.m_Index];
        s_Complications.push_back({ { "cle", d.m_Cle }, { "titre", Tx(std::string("compl.") + d.m_Cle + ".titre") },
                                    { "detail", Tx(std::string("compl.") + d.m_Cle + ".detail") }, { "etat", EtatTexte(c.m_Etat) },
                                    { "image", ImageComplication(d.m_Cle) } });
    }

    s_Etat["complications"] = s_Complications;
    {
        json s_Noms = json::array(), s_Cles = json::array();
        for (const char* f : FamillesMethodes()) {
            s_Noms.push_back(Tx(std::string("famille.") + f));
            s_Cles.push_back(f);
        }
        s_Etat["familles"] = s_Noms;
        s_Etat["famillesCles"] = s_Cles;
    }
    s_Etat["langue"] = CodeLangue(Langue());

    json s_Familles = json::array();
    for (size_t i = 0; i < FamillesMethodes().size(); ++i) s_Familles.push_back(m_Reglages.m_Familles[i]);

    s_Etat["reglages"] = {
        { "mode", CleMode(m_Reglages.m_Mode) },
        { "imposerMethode", m_Reglages.m_ImposerMethode },
        { "imposerTenue", m_Reglages.m_ImposerTenue },
        { "costume", m_Reglages.m_AutoriserCostume },
        { "tirageAuto", m_Reglages.m_TirageAuto },
        { "encart", m_Encart },
        { "complications", m_Reglages.m_NbComplications },
        { "pnj", m_Reglages.m_NbPnj },
        { "echecImpose", m_Reglages.m_EchecImpose },
        { "imposerPreparation", m_Reglages.m_ImposerPreparation },
        { "ciblesDuJeu", m_Reglages.m_CiblesDuJeuSeules },
        { "discord", m_DiscordActif },
        { "seriePuriste", m_SeriePuriste },
        { "ciblesMin", m_Reglages.m_CiblesMin },
        { "ciblesMax", m_Reglages.m_CiblesMax },
        { "remplacerObjectifs", m_Reglages.m_RemplacerObjectifs },
        { "destinationAuto", m_Reglages.m_DestinationAuto },
        { "sons", m_Reglages.m_Sons },
        { "pilote", m_Reglages.m_Pilote },
        { "familles", s_Familles },
        { "langue", m_LangueChoisie.empty() ? std::string("jeu") : m_LangueChoisie },
    };

    // Ce qui défile pendant l'animation du tirage.
    json s_Methodes = json::array();
    for (size_t i = 0; i < Methodes().size(); ++i) s_Methodes.push_back(MethodeJson(static_cast<int>(i)));

    json s_Tenues = json::array();
    for (const auto& t : m_Tirage.m_Tenues) s_Tenues.push_back(TenueJson(t));

    s_Etat["catalogue"] = { { "methodes", s_Methodes }, { "tenues", s_Tenues } };

    return s_Etat.dump();
}

void Roulette::MessagePage(const std::string& p_Json) {
    nlohmann::json s_Message;

    try {
        s_Message = nlohmann::json::parse(p_Json);
    }
    catch (...) {
        return;
    }

    const auto s_Action = ChampTexte(s_Message, "action");

    if (s_Action == "relancer") {
        std::lock_guard s_Verrou(m_Verrou);

        if (!m_EnMission && (m_Prepare || m_Propose)) {
            // Un nouveau contrat entier, à accepter de nouveau.
            if (TirerContrat()) Diag("Contrat relancé.");
            m_Fenetre.Pousser();
        }
        // Relancer après une première élimination, ce serait choisir son défi.
        else if (m_EnMission && !m_TirageInjecte && !m_Tirage.m_UneEliminationFaite && !m_Tirage.m_Terminee) {
            m_DemandeTirage = true;
        }
    }
    else if (s_Action == "tirer") {
        std::lock_guard s_Verrou(m_Verrou);
        if (m_ReponsesServeur.load() == 0) {
            Diag("Tirage refusé : aucune réponse des serveurs d'IOI (jeu hors ligne).");
        }
        else if (!m_EnMission && TirerContrat()) {
            const auto& p = m_TiragePrepare.m_Preparation;
            Diag("Contrat proposé : {} — {} ; {} cible(s), entrée « {} ».", m_Lieu, m_TitreMission, m_TiragePrepare.m_Cibles.size(), p.m_Entree.m_Nom);
        }
        m_Fenetre.Pousser();
    }
    else if (s_Action == "accepter") {
        std::lock_guard s_Verrou(m_Verrou);
        if (m_Propose && !m_EnMission) {
            m_Propose = false;
            m_Prepare = true;
            m_EtatPilote.clear();
            Diag("Contrat accepté : {} — {}.", m_Lieu, m_TitreMission);
            // La fiche montre un écran de chargement jusqu'à la préparation.
            if (m_Reglages.m_DestinationAuto && m_Reglages.m_Pilote) m_PagesPrepAuLancement = m_PagesPrep.load();

            // Julien (24/11/2026) : « accéder à l'écran de préparation
            // directement ». Le mod demande au jeu d'ouvrir la page de
            // préparation du contrat, comme le ferait une tuile (action
            // « link »), sans parcourir les menus ni simuler de touche. Si le
            // jeu ne l'a pas ouverte en trois secondes (navigateur des menus
            // pas encore connu), le pilote au clavier prend le relais.
            // Réglage « Aller seul à la mission acceptée » (m_Pilote) : coupé, rien ne se fait.
            const bool s_Direct = m_Reglages.m_Pilote && m_Reglages.m_DestinationAuto &&
                                  Moteur::PeutOuvrirPage() && Moteur::NavigateurConnu();
            if (s_Direct) {
                // Même forme que la requête du jeu relevée le 24/11/2026 (tuile
                // d'une mission → vidéo → préparation).
                const nlohmann::json s_Arguments = {
                    { "url", "planning" },
                    { "contractid", m_IdContratPrepare },
                    { "args", { { "contractid", m_IdContratPrepare }, { "resetescalation", false } } },
                };
                Moteur::DemanderPage("planning", s_Arguments.dump());
                Diag("Ouverture directe de la préparation demandée ({}).", m_IdContratPrepare);
            }
            if (m_Reglages.m_DestinationAuto && !m_PiloteEnCours.exchange(true)) {
                std::thread([this, s_Direct] {
                    if (s_Direct) {
                        for (int i = 0; i < 30 && Moteur::PageEnAttente(); ++i) Sleep(100);
                        if (!Moteur::PageEnAttente()) {
                            m_PiloteEnCours = false;
                            return;
                        }
                        Moteur::AnnulerPage();
                        Diag("Ouverture directe : le jeu n'a pas pris la demande, le pilote prend le relais.");
                    }
                    if (!m_Reglages.m_Pilote) {
                        m_PagesPrepAuLancement = -1;
                        m_PiloteEnCours = false;
                        return;
                    }
                    // Le pilote appuie sur les touches du jeu : la fiche (et son
                    // écran de chargement) doit lui laisser la place.
                    m_Fenetre.Ouvrir(false);
                    Piloter();
                    if (m_PagesPrep.load() <= m_PagesPrepAuLancement.load()) m_PagesPrepAuLancement = -1;
                    m_PiloteEnCours = false;
                }).detach();
            }
        }
        m_Fenetre.Pousser();
    }
    else if (s_Action == "importer") {
        std::lock_guard s_Verrou(m_Verrou);
        if (m_ReponsesServeur.load() == 0) m_ErreurCode = Tx("code.hors_ligne");
        else if (!m_EnMission) ImporterContrat(ChampTexte(s_Message, "code"));
        else m_ErreurCode = Tx("code.en_mission");
        m_Fenetre.Pousser();
    }
    else if (s_Action == "refuser") {
        std::lock_guard s_Verrou(m_Verrou);
        if (!m_EnMission && (m_Propose || m_Prepare)) {
            Diag("Contrat {} : {} — {}.", m_Prepare ? "abandonné" : "refusé", m_Lieu, m_TitreMission);
            OublierContrat();
        }
        m_Fenetre.Pousser();
    }
    else if (s_Action == "reglage") {
        const auto s_Cle = ChampTexte(s_Message, "cle");
        const auto& s_Valeur = s_Message["valeur"];
        bool s_RelireLangue = false;

        {
            std::lock_guard s_Verrou(m_Verrou);

            // Langue de la fiche : « jeu » (défaut) suit la langue du jeu ;
            // fr, en, it, es, ru l'imposent (Julien, 26/11/2026).
            if (s_Cle == "langue" && s_Valeur.is_string()) {
                ELangue s_Langue = ELangue::Fr;
                if (LangueDepuisCode(s_Valeur.get<std::string>(), s_Langue)) {
                    m_LangueChoisie = CodeLangue(s_Langue);
                    m_LangueImposee = true;
                    DefinirLangue(s_Langue);
                }
                else {
                    m_LangueChoisie.clear();
                    m_LangueImposee = false;
                    s_RelireLangue = true;
                }
                Diag("Langue de la fiche : {}.", m_LangueChoisie.empty() ? std::string("celle du jeu") : m_LangueChoisie);
            }
            else if (s_Cle == "mode" && s_Valeur.is_string()) m_Reglages.m_Mode = ModeDepuisCle(s_Valeur.get<std::string>());
            else if (s_Cle == "imposerMethode") m_Reglages.m_ImposerMethode = s_Valeur.is_boolean() && s_Valeur.get<bool>();
            else if (s_Cle == "imposerTenue") m_Reglages.m_ImposerTenue = s_Valeur.is_boolean() && s_Valeur.get<bool>();
            else if (s_Cle == "costume") m_Reglages.m_AutoriserCostume = s_Valeur.is_boolean() && s_Valeur.get<bool>();
            else if (s_Cle == "tirageAuto") m_Reglages.m_TirageAuto = s_Valeur.is_boolean() && s_Valeur.get<bool>();
            else if (s_Cle == "encart") m_Encart = s_Valeur.is_boolean() && s_Valeur.get<bool>();
            else if (s_Cle == "echecImpose") m_Reglages.m_EchecImpose = s_Valeur.is_boolean() && s_Valeur.get<bool>();
            else if (s_Cle == "imposerPreparation") m_Reglages.m_ImposerPreparation = s_Valeur.is_boolean() && s_Valeur.get<bool>();
            else if (s_Cle == "ciblesDuJeu") m_Reglages.m_CiblesDuJeuSeules = s_Valeur.is_boolean() && s_Valeur.get<bool>();
            else if (s_Cle == "discord") {
                m_DiscordActif = s_Valeur.is_boolean() && s_Valeur.get<bool>();
                if (m_DiscordActif) m_Discord.Demarrer(m_IdDiscord);
                else m_Discord.Arreter(); // le tube fermé, Discord efface la présence
            }
            else if (s_Cle == "destination") {
                const auto s_Id = ChampTexte(s_Message, "id");
                if (!s_Id.empty()) {
                    if (s_Valeur.is_boolean() && s_Valeur.get<bool>()) m_DestinationsExclues.erase(s_Id);
                    else m_DestinationsExclues.insert(s_Id);
                }
            }
            else if (s_Cle == "pnj" && s_Valeur.is_number_integer()) m_Reglages.m_NbPnj = std::clamp(s_Valeur.get<int>(), 0, 3);
            else if (s_Cle == "remplacerObjectifs") m_Reglages.m_RemplacerObjectifs = s_Valeur.is_boolean() && s_Valeur.get<bool>();
            else if (s_Cle == "destinationAuto") m_Reglages.m_DestinationAuto = s_Valeur.is_boolean() && s_Valeur.get<bool>();
            else if (s_Cle == "sons") m_Reglages.m_Sons = s_Valeur.is_boolean() && s_Valeur.get<bool>();
            else if (s_Cle == "pilote") m_Reglages.m_Pilote = s_Valeur.is_boolean() && s_Valeur.get<bool>();
            else if (s_Cle == "ciblesMin" && s_Valeur.is_number_integer()) {
                m_Reglages.m_CiblesMin = std::clamp(s_Valeur.get<int>(), 1, 3);
                m_Reglages.m_CiblesMax = std::max(m_Reglages.m_CiblesMax, m_Reglages.m_CiblesMin);
            }
            else if (s_Cle == "ciblesMax" && s_Valeur.is_number_integer()) {
                m_Reglages.m_CiblesMax = std::clamp(s_Valeur.get<int>(), 1, 3);
                m_Reglages.m_CiblesMin = std::min(m_Reglages.m_CiblesMin, m_Reglages.m_CiblesMax);
            }
            else if (s_Cle == "complications" && s_Valeur.is_number_integer()) {
                m_Reglages.m_NbComplications = std::clamp(s_Valeur.get<int>(), 0, 3);
            }
            else if (s_Cle == "famille" && s_Message["index"].is_number_integer()) {
                const int i = s_Message["index"].get<int>();
                if (i >= 0 && i < static_cast<int>(FamillesMethodes().size())) {
                    m_Reglages.m_Familles[i] = s_Valeur.is_boolean() && s_Valeur.get<bool>();
                }
            }
        }

        if (s_RelireLangue) DetecterLangueDuJeu();
        EnregistrerReglages();
        m_Fenetre.Pousser();
    }
}
