// Maquette : fait tourner la page hors du jeu, avec des données RÉELLES —
// destinations du joueur (destinations.json), et les deux contrats tirés en jeu
// le 22/11/2026 (Whittleton, Chongqing : diagnostic.log et captures du jeu).
// Aucune valeur inventée. En jeu, ce fichier n'est pas chargé.
'use strict';

(() => {
  if (window.ROULETTE_EN_JEU) return;

  // Les images viennent du cache que le mod remplit pendant la partie.
  // Servie par tools/apercu_ui.py, la page les trouve sous /cache/.
  const BASE = location.protocol === 'file:'
    ? 'file:///C:/Program%20Files%20(x86)/Steam/steamapps/common/HITMAN%203/Retail/Roulette/cache/'
    : '/cache/';

  // Tirage.cpp, à l'identique (clés, libellés, familles).
  const CLES_METHODES = ['accident', 'noyade', 'chute', 'electrocution', 'objet_suspendu', 'brulure',
    'explosion_accident', 'poison', 'poison_ingere', 'poison_injecte', 'pistolet', 'pm', 'pompe', 'assaut',
    'precision', 'explosif', 'corde', 'mains_nues', 'melee'];
  const METHODES = [
    ['Accident (au choix)', 'Accidents'], ['Noyade', 'Accidents'], ['Chute (poussée)', 'Accidents'],
    ['Électrocution', 'Accidents'], ['Objet suspendu', 'Accidents'], ['Brûlure', 'Accidents'],
    ['Explosion accidentelle', 'Accidents'], ['Poison (au choix)', 'Poisons'], ['Poison ingéré', 'Poisons'],
    ['Poison injecté', 'Poisons'], ['Pistolet', 'Armes à feu'], ['Pistolet-mitrailleur', 'Armes à feu'],
    ['Fusil à pompe', 'Armes à feu'], ['Fusil d\'assaut', 'Armes à feu'], ['Fusil de précision', 'Armes à feu'],
    ['Explosif', 'Explosifs'], ['Corde de piano', 'Corps à corps'], ['À mains nues', 'Corps à corps'],
    ['Arme de mêlée', 'Corps à corps'],
  ].map(([libelle, famille], i) => ({ libelle, famille, cle: CLES_METHODES[i] }));
  const methode = (cle) => METHODES.find((m) => m.cle === cle);

  const FAMILLES = ['Accidents', 'Poisons', 'Armes à feu', 'Explosifs', 'Corps à corps', 'Au choix'];

  // Contrat.cpp (ImageComplication) : images des conditions de contrat du jeu.
  const COMPLICATIONS = {
    aucun_corps: { cle: 'aucun_corps', titre: 'Aucun corps découvert', detail: 'Les corps d\'accidents ne comptent pas.',
      image: 'images/contractconditions/condition_contrac_no_bodies_found.jpg' },
  };

  const COSTUME = { nom: 'Costume de 47', costume: true };

  // Destinations possédées (destinations.json du 23/11/2026).
  const DESTINATIONS = [
    ['Paris, France', 'LOCATION_PARIS/tile.jpg'], ['Sapienza, Italie', 'LOCATION_COASTALTOWN/tile.jpg'],
    ['Marrakech, Maroc', 'LOCATION_MARRAKECH/tile.jpg'], ['Bangkok, Thaïlande', 'LOCATION_BANGKOK/tile.jpg'],
    ['Colorado, États-Unis', 'LOCATION_COLORADO/tile.jpg'], ['Hokkaido, Japon', 'LOCATION_HOKKAIDO/tile.jpg'],
    ['Hawke\'s Bay, Nouvelle-Zélande', 'LOCATION_NEWZEALAND/tile.jpg'], ['Miami, États-Unis', 'LOCATION_MIAMI/tile.jpg'],
    ['Santa Fortuna, Colombie', 'LOCATION_COLOMBIA/tile.jpg'], ['Bombay, Inde', 'LOCATION_MUMBAI/tile.jpg'],
    ['Crique de Whittleton, États-Unis', 'LOCATION_NORTHAMERICA/tile.jpg'], ['Île Ambrose, mer d\'Andaman', 'LOCATION_ROCKY/rocky_tile.jpg'],
    ['Île de Sgàil, Atlantique Nord', 'LOCATION_NORTHSEA/tile.jpg'], ['New York, États-Unis', 'LOCATION_GREEDY_RACCOON/tile.jpg'],
    ['Île Haven, Maldives', 'LOCATION_OPULENT_STINGRAY/tile.jpg'], ['Dubaï, Émirats arabes unis', 'LOCATION_GOLDEN/golden_tile.jpg'],
    ['Dartmoor, Angleterre', 'LOCATION_ANCESTRAL/ancestral_tile.jpg'], ['Berlin, Allemagne', 'LOCATION_EDGY/edgy_tile.jpg'],
    ['Chongqing, Chine', 'LOCATION_WET/wet_tile.jpg'], ['Mendoza, Argentine', 'LOCATION_ELEGANT/elegant_tile.jpg'],
    ['Carpates, Roumanie', 'LOCATION_TRAPPED/trapped_tile.jpg'],
  ].map(([nom, image]) => ({ nom, image: 'images/locations/' + image }));

  // Les deux contrats tirés en jeu (essais du 22/11/2026).
  const CONTRATS = [
    {
      destination: { nom: 'Crique de Whittleton, États-Unis', image: 'images/locations/LOCATION_NORTHAMERICA/tile.jpg',
        fond: 'images/locations/LOCATION_NORTHAMERICA/background.jpg',
        mission: { titre: 'Une autre vie', image: 'images/contracts/skunk/tile.jpg' } },
      cibles: [
        { nom: 'Nolan Cassidy', image: 'images/actors/Skunk_Nolan_Cassidy.jpg', methode: methode('melee'), tenue: COSTUME },
        { nom: 'Janus', image: 'images/actors/Skunk_Janus.jpg', methode: methode('electrocution'), tenue: COSTUME },
      ],
      complications: [COMPLICATIONS.aucun_corps],
      // EvaluerDifficulte (Tirage.cpp) appliqué à ce contrat : 6,8 points.
      difficulte: { note: 5, points: 6.8, facteurs: [
        { texte: 'Électrocution', cible: 'Janus', points: 2 }, { texte: 'Aucun corps découvert', cible: '', points: 1.2 },
        { texte: 'Costume de 47', cible: 'Nolan Cassidy', points: 0.6 }] },
      preparation: {
        entree: { nom: 'Panneau de banlieue', image: 'images/entrances/skunk_entrance_granny.jpg' },
        arme: { nom: 'HWK 21 Camouflage' },
        objet: { nom: 'Griffe de chat' },
      },
    },
    {
      destination: { nom: 'Chongqing, Chine', image: 'images/locations/LOCATION_WET/wet_tile.jpg',
        fond: 'images/locations/LOCATION_WET/wet_background.jpg',
        mission: { titre: 'Fin d\'une ère', image: 'images/contracts/rat/rat_tile.jpg' } },
      cibles: [{ nom: 'Hush', image: 'images/actors/Wet_Hush.jpg', methode: methode('noyade'), tenue: COSTUME }],
      complications: [COMPLICATIONS.aucun_corps],
      difficulte: { note: 4, points: 4.4, facteurs: [
        { texte: 'Noyade', cible: 'Hush', points: 1.8 }, { texte: 'Aucun corps découvert', cible: '', points: 1 },
        { texte: 'Costume de 47', cible: 'Hush', points: 0.6 }] },
      preparation: {
        entree: { nom: 'Le bloc', image: 'images/entrances/Wet_Entrance_Block_Security.jpg' },
        arme: { nom: 'ICA19 Shortballer' },
        objet: { nom: 'Charge IEM télécommandée' },
      },
    },
  ];

  const copie = (x) => JSON.parse(JSON.stringify(x));
  let numero = 0;

  const etat = {
    ouverte: true,
    base: BASE,
    images: 0,
    tirage: 0,
    // Aperçu dans une autre langue : ?langue=en (fr, en, it, es, ru).
    langue: new URLSearchParams(location.search).get('langue') || 'fr',
    etat: 'hors_mission',
    enMission: false,
    tirable: true,
    mission: { lieu: '', titre: '' },
    relancePossible: false,
    dansLeContrat: false,
    rompu: false,
    raison: '',
    cibles: [],
    complications: [],
    preparation: null,
    destination: null,
    destinations: DESTINATIONS,
    familles: FAMILLES,
    reglages: {
      mode: 'assassin', imposerMethode: true, imposerTenue: true, costume: true, tirageAuto: false, sons: true, pilote: true,
      complications: 1, pnj: 1, echecImpose: true, familles: [true, true, true, true, true, true],
      ciblesMin: 1, ciblesMax: 3, remplacerObjectifs: true, destinationAuto: true, imposerPreparation: true,
    },
    catalogue: { methodes: METHODES, tenues: [COSTUME] },
  };

  function poserContrat() {
    const c = copie(CONTRATS[numero % CONTRATS.length]);
    numero++;
    Object.assign(etat, {
      destination: c.destination,
      mission: { lieu: c.destination.nom, titre: c.destination.mission.titre },
      cibles: c.cibles.map((x) => ({ ...x, etat: 'en_cours', officielle: true })),
      complications: c.complications.map((x) => ({ ...x, etat: 'en_cours' })),
      preparation: c.preparation,
      difficulte: c.difficulte,
      costumeDepart: { id: '', nom: 'Costume de la mission', image: '' },
      lancement: null,
      etat: 'propose', enMission: false, relancePossible: true, dansLeContrat: false, rompu: false,
    });
    etat.tirage++;
  }

  function oublier() {
    Object.assign(etat, { etat: 'hors_mission', enMission: false, cibles: [], complications: [], preparation: null,
      destination: null, relancePossible: false, dansLeContrat: false, rompu: false, mission: { lieu: '', titre: '' } });
    etat.tirage++;
  }

  function pousser() {
    roulette.recevoir(copie(etat));
  }

  roulette.local = (m) => {
    if (m.action === 'prete') { pousser(); return; }
    if (m.action === 'tirer' || (m.action === 'relancer' && etat.relancePossible)) poserContrat();
    if (m.action === 'accepter' && etat.etat === 'propose') {
      // Comme le mod : écran de chargement, puis préparation ouverte 3 s plus tard.
      Object.assign(etat, { etat: 'accepte', lancement: 'attente' });
      setTimeout(() => { etat.lancement = 'prete'; pousser(); }, 3000);
    }
    if (m.action === 'refuser') oublier();
    if (m.action === 'fermer') etat.ouverte = false;
    if (m.action === 'reglage') {
      if (m.cle === 'famille') etat.reglages.familles[m.index] = m.valeur;
      else etat.reglages[m.cle] = m.valeur;
    }
    pousser();
  };

  // ------------------------------------------------ Barre de la maquette

  const barre = document.createElement('div');
  barre.style.cssText = 'position:fixed;left:12px;bottom:12px;z-index:99;display:flex;gap:6px;flex-wrap:wrap;' +
    'font:13px Segoe UI,sans-serif;max-width:calc(100vw - 24px)';
  document.body.append(barre);

  const actions = [
    ['F9', () => { etat.ouverte = !etat.ouverte; }],
    ['Accueil', oublier],
    ['Lancer la mission', () => {
      if (!etat.cibles.length) poserContrat();
      Object.assign(etat, { etat: 'en_cours', enMission: true, dansLeContrat: true, relancePossible: false,
        costumeDepart: null, lancement: null });
      etat.preparation.arme.etat = 'reussi';
      etat.preparation.objet.etat = 'reussi';
    }],
    ['Cible éliminée', () => { if (etat.cibles[0]) etat.cibles[0].etat = 'reussi'; }],
    ['Complication rompue', () => { if (etat.complications[0]) etat.complications[0].etat = 'echoue'; }],
    ['Contrat rompu', () => {
      if (!etat.cibles.length) return;
      const c = etat.cibles[etat.cibles.length - 1];
      c.etat = 'echoue';
      c.detail = 'Méthode : pistol';
      Object.assign(etat, { rompu: true, raison: c.nom + ' — ' + c.detail, etat: 'echoue' });
    }],
  ];

  barre.replaceChildren(...actions.map(([libelle, faire]) => {
    const b = document.createElement('button');
    b.textContent = libelle;
    b.style.cssText = 'padding:6px 10px;background:#2a2a30;color:#ddd;border:1px dashed #555;cursor:pointer';
    b.onclick = () => { faire(); pousser(); };
    return b;
  }));

  document.documentElement.style.background = 'linear-gradient(160deg,#2b3340,#11151b 60%,#070809)';
})();
