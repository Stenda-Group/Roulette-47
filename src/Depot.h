#pragma once

// Le dépôt du jeu (pro.repo) : la fiche de chaque personnage, tenue et objet,
// avec son nom et le chemin de son image (« images/actors/… »). C'est là que le
// mod prend les noms et les visuels : rien n'est recopié dans le projet, tout
// est lu dans le jeu installé.
//
// Les noms des tenues y sont des CLÉS de traduction
// (« char_…_name_<guid> »), pas du texte : NomLisible() les reconnaît.

#include <string>
#include <unordered_map>

struct EntreeDepot {
    std::string m_Id;    // GUID en minuscules
    std::string m_Nom;   // Name, sinon CommonName, sinon Title
    std::string m_Cle;   // le champ Name brut : souvent une clé de traduction que le jeu sait traduire
    std::string m_Image; // chemin relatif du jeu, ex. « images/actors/… .jpg » (vide si aucun)
    std::string m_Tile;  // vignette « Tile » : repli quand Image n'est pas sur le service
    bool m_EstCostume = false; // IsHitmanSuit : un costume de 47
    std::string m_NomCommun;   // CommonName (anglais, stable) : « Fiber Wire »…
    std::string m_Categorie;   // InventoryCategoryIcon des objets : « sniperrifle », « melee »…
    std::string m_CleLoc;      // Name_LOC des armes : seconde clé de traduction du nom
    bool m_Silencieux = false; // Perks contient « suppressor » (relevé du 24/11/2026)
};

class Depot {
public:
    // À appeler en mission (le dépôt n'est chargé qu'à ce moment). Rend vrai une
    // fois le dépôt lu ; les appels suivants ne refont rien.
    bool Charger();

    [[nodiscard]] bool EstCharge() const { return !m_Entrees.empty(); }

    // nullptr si l'identifiant n'est pas dans le dépôt.
    [[nodiscard]] const EntreeDepot* Trouver(const std::string& p_Id) const;

    // Toutes les fiches (pour chercher un objet par nom ou par catégorie).
    [[nodiscard]] const std::unordered_map<std::string, EntreeDepot>& Entrees() const { return m_Entrees; }

    // La fiche complète en JSON, pour le diagnostic ("" si absente).
    [[nodiscard]] std::string Brut(const std::string& p_Id) const;

private:
    std::unordered_map<std::string, EntreeDepot> m_Entrees;
    const void* m_Donnees = nullptr; // THashMap du jeu, vivant tant que la ressource l'est
};

std::string EnMinuscules(std::string p_Texte);

// Vrai si le texte est une clé de traduction plutôt qu'un nom.
bool EstUneCle(const std::string& p_Texte);
