#include "Depot.h"

#include <algorithm>

#include "Diagnostic.h"
#include "Glacier.h"
#include "Json.h"

namespace {
    // « [assembly:/repository/pro.repo].pc_repo » : MD5 17204d1afd76ab13…,
    // octets 1 à 7 du condensé.
    constexpr uint64_t c_IdDepot = 0x00204D1AFD76AB13ull;
}

std::string EnMinuscules(std::string p_Texte) {
    std::ranges::transform(p_Texte, p_Texte.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return p_Texte;
}

bool EstUneCle(const std::string& p_Texte) {
    // « char_exclusive_hero_blacksuit_m_pro115373_name_9480eaaf-… » : pas
    // d'espace, des soulignés, souvent un suffixe _name_ / _desc_.
    if (p_Texte.empty()) return true;
    if (p_Texte.find(' ') != std::string::npos) return false;
    return p_Texte.find('_') != std::string::npos;
}

bool Depot::Charger() {
    if (EstCharge()) {
        return true;
    }

    static Glacier::RefRessource s_Ressource;

    if (s_Ressource.m_nIndex < 0) {
        Glacier::DemanderRessource(s_Ressource, c_IdDepot);
    }

    const void* s_Donnees = Glacier::DonneesRessource(s_Ressource);

    if (!s_Donnees) {
        static int s_DernierEtat = -2;
        const int s_Etat = Glacier::EtatRessource(s_Ressource);
        if (s_Etat != s_DernierEtat) {
            s_DernierEtat = s_Etat;
            Diag("Dépôt pas encore lisible (ressource n° {}, état {}).", s_Ressource.m_nIndex, s_Etat);
        }
        return false;
    }

    m_Donnees = s_Donnees;
    size_t s_AvecImage = 0, s_SansPaires = 0;

    Glacier::PourChaqueFiche(s_Donnees, [&](const Glacier::Guid& s_IdDepot, const Glacier::Objet& s_Objet) {
        const auto s_Paires = s_Objet.Comme<Glacier::ObjetJson>("TArray<SDynamicObjectKeyValuePair>");

        if (!s_Paires) {
            ++s_SansPaires;
            return;
        }

        EntreeDepot s_Entree;
        s_Entree.m_Id = s_IdDepot.EnTexte();

        std::string s_Name, s_CommonName, s_Title;

        for (const auto& s_Paire : *s_Paires) {
            const auto& s_Cle = s_Paire.m_Cle;

            if (s_Cle == "IsHitmanSuit") {
                if (const auto s_Booleen = s_Paire.m_Valeur.Comme<bool>("bool")) s_Entree.m_EstCostume = *s_Booleen;
                continue;
            }

            // Le silencieux d'une arme est un « perk » : « suppressor ».
            if (s_Cle == "Perks") {
                const auto s_Perks = VersJson(s_Paire.m_Valeur);
                if (s_Perks.is_array()) {
                    for (const auto& p : s_Perks) {
                        if (p.is_string() && p.get<std::string>() == "suppressor") s_Entree.m_Silencieux = true;
                    }
                }
                continue;
            }

            const auto s_Valeur = s_Paire.m_Valeur.Comme<Glacier::Chaine>("ZString");

            if (!s_Valeur) {
                continue;
            }

            if (s_Cle == "Name") s_Name = VersTexte(*s_Valeur);
            else if (s_Cle == "CommonName") s_CommonName = VersTexte(*s_Valeur);
            else if (s_Cle == "Title") s_Title = VersTexte(*s_Valeur);
            else if (s_Cle == "Image") s_Entree.m_Image = VersTexte(*s_Valeur);
            else if (s_Cle == "Tile") s_Entree.m_Tile = VersTexte(*s_Valeur);
            else if (s_Cle == "InventoryCategoryIcon") s_Entree.m_Categorie = EnMinuscules(VersTexte(*s_Valeur));
            else if (s_Cle == "Name_LOC") s_Entree.m_CleLoc = VersTexte(*s_Valeur);
        }

        // Un vrai nom d'abord ; une clé de traduction en dernier recours.
        for (const auto* s_Candidat : { &s_Name, &s_CommonName, &s_Title }) {
            if (!s_Candidat->empty() && !EstUneCle(*s_Candidat)) {
                s_Entree.m_Nom = *s_Candidat;
                break;
            }
        }

        s_Entree.m_Cle = s_Name;
        s_Entree.m_NomCommun = s_CommonName;

        if (s_Entree.m_Nom.empty()) {
            s_Entree.m_Nom = !s_Name.empty() ? s_Name : !s_CommonName.empty() ? s_CommonName : s_Title;
        }

        if (!s_Entree.m_Image.empty()) {
            ++s_AvecImage;
        }

        m_Entrees.emplace(s_Entree.m_Id, std::move(s_Entree));
    });

    Diag("Dépôt lu : {} fiches, dont {} avec une image ({} illisibles).", m_Entrees.size(), s_AvecImage, s_SansPaires);

    return EstCharge();
}

const EntreeDepot* Depot::Trouver(const std::string& p_Id) const {
    const auto s_It = m_Entrees.find(EnMinuscules(p_Id));
    return s_It == m_Entrees.end() ? nullptr : &s_It->second;
}

std::string Depot::Brut(const std::string& p_Id) const {
    if (!m_Donnees) {
        return {};
    }

    const auto s_Cherche = EnMinuscules(p_Id);
    std::string s_Json;

    Glacier::PourChaqueFiche(m_Donnees, [&](const Glacier::Guid& s_IdDepot, const Glacier::Objet& s_Objet) {
        if (s_Json.empty() && s_IdDepot.EnTexte() == s_Cherche) {
            s_Json = VersJson(s_Objet).dump();
        }
    });

    return s_Json;
}
