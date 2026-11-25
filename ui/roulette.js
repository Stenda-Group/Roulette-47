// Roulette — la page de la fiche.
//
// Le mod pousse l'état complet par roulette.recevoir(etat) ; la page ne garde
// que ce qui sert à l'animation. Elle renvoie des intentions (tirer, accepter,
// refuser, relancer, fermer, régler) et le mod décide : c'est lui qui tire, qui
// réécrit les menus du jeu et qui suit les événements.
//
// États (etat.etat) :
//   hors_mission  aucun contrat : l'accueil, « Tirer un contrat » ;
//   propose       tiré, à accepter ou refuser ;
//   accepte       accepté : sa préparation s'ouvre, verrouillée ;
//   attente, en_cours, reussi, echoue : en mission.
'use strict';

const roulette = (() => {
  const $ = (id) => document.getElementById(id);

  const ICONES_FAMILLE = {
    'Accidents': 'i-Accidents', 'Poisons': 'i-Poisons', 'Armes à feu': 'i-Armes',
    'Explosifs': 'i-Explosifs', 'Corps à corps': 'i-Corps',
  };

  const ICONES_METHODE = {
    accident: 'i-Accidents', noyade: 'i-noyade', chute: 'i-chute', electrocution: 'i-eclair',
    objet_suspendu: 'i-suspendu', brulure: 'i-flamme', explosion_accident: 'i-Explosifs', mains_nues: 'i-main',
  };

  const ICONES_COMPLICATION = {
    sans_etre_repere: 'i-oeil', aucun_corps: 'i-corps', aucune_victime: 'i-victime',
    aucune_neutralisation: 'i-main', aucun_changement: 'i-cintre', aucun_enregistrement: 'i-camera', tirs_tete: 'i-tete',
  };


  // ---------------------------------------------------------------- Langue

  let langue = 'fr';
  const t = (cle) => (TEXTES[langue] && TEXTES[langue][cle]) || TEXTES.fr[cle] || cle;
  const tr = t;

  function appliquerLangue(code) {
    langue = TEXTES[code] ? code : 'fr';
    document.documentElement.lang = langue;
    document.querySelectorAll('[data-t]').forEach((n) => { n.textContent = t(n.dataset.t); });
    document.querySelectorAll('[data-t-html]').forEach((n) => { n.innerHTML = t(n.dataset.tHtml); });
    document.querySelectorAll('[data-t-title]').forEach((n) => { n.title = t(n.dataset.tTitle); });
    document.querySelectorAll('[data-t-aria]').forEach((n) => { n.setAttribute('aria-label', t(n.dataset.tAria)); });
    document.querySelectorAll('[data-t-placeholder]').forEach((n) => { n.placeholder = t(n.dataset.tPlaceholder); });
  }

  let etat = null;
  let gestionLocale = null; // maquette hors du jeu
  let ouverteAvant = false;
  let dernierTirage = null;
  let dernierEtats = '';
  let dernieresImages = 0;
  let minuteries = [];
  let mosaiquePosee = '';
  let langueAppliquee = false;

  // ---------------------------------------------------------------- Pont

  const enJeu = !!window.ROULETTE_EN_JEU;
  if (enJeu) document.documentElement.classList.add('en-jeu');

  // La fiche à la taille de l'écran : 1 sous 1500 × 980, jusqu'à 1,7.
  function ajusterTaille() {
    const z = Math.max(1, Math.min(1.7, window.innerWidth / 1500, window.innerHeight / 980));
    document.documentElement.style.setProperty('--zoom', z.toFixed(3));
  }
  ajusterTaille();
  window.addEventListener('resize', ajusterTaille);

  // ---------------------------------------------------------------- Sons
  //
  // Fabriqués à la volée (Web Audio) : aucun fichier, rien de repris ailleurs.
  // Ils passent par le navigateur de la fiche, sans toucher au son du jeu.
  const son = (() => {
    let ctx = null;
    let bruit = null;

    function contexte() {
      if (!etat || !etat.reglages || etat.reglages.sons === false) return null;
      if (!ctx) {
        try { ctx = new AudioContext(); } catch (e) { return null; }
        bruit = ctx.createBuffer(1, ctx.sampleRate, ctx.sampleRate);
        const d = bruit.getChannelData(0);
        for (let i = 0; i < d.length; i++) d[i] = Math.random() * 2 - 1;
      }
      if (ctx.state === 'suspended') ctx.resume();
      return ctx;
    }

    function enveloppe(c, gain, attaque, duree) {
      const g = c.createGain();
      const t = c.currentTime;
      g.gain.setValueAtTime(0.0001, t);
      g.gain.exponentialRampToValueAtTime(gain, t + attaque);
      g.gain.exponentialRampToValueAtTime(0.0001, t + duree);
      g.connect(c.destination);
      return g;
    }

    function ton(frequence, duree, gain, type, glisse) {
      const c = contexte();
      if (!c) return;
      const o = c.createOscillator();
      o.type = type || 'sine';
      o.frequency.setValueAtTime(frequence, c.currentTime);
      if (glisse) o.frequency.exponentialRampToValueAtTime(glisse, c.currentTime + duree);
      o.connect(enveloppe(c, gain, 0.004, duree));
      o.start();
      o.stop(c.currentTime + duree + 0.02);
    }

    function souffle(duree, gain, de, a) {
      const c = contexte();
      if (!c) return;
      const s = c.createBufferSource();
      s.buffer = bruit;
      const f = c.createBiquadFilter();
      f.type = 'bandpass';
      f.Q.value = 1.2;
      f.frequency.setValueAtTime(de, c.currentTime);
      f.frequency.exponentialRampToValueAtTime(a, c.currentTime + duree);
      s.connect(f);
      f.connect(enveloppe(c, gain, duree * 0.3, duree));
      s.start();
      s.stop(c.currentTime + duree + 0.02);
    }

    return {
      // Le cliquetis de la roulette.
      tic: () => ton(2400 + Math.random() * 600, 0.03, 0.035, 'triangle'),
      // Une valeur qui tombe.
      tombe: () => { ton(180, 0.18, 0.12, 'sine', 70); souffle(0.08, 0.05, 3000, 1200); },
      // Une carte qui arrive.
      passe: () => souffle(0.32, 0.06, 600, 4000),
      // La destination tirée : un coup sourd.
      impact: () => { ton(110, 0.5, 0.22, 'sine', 45); souffle(0.25, 0.07, 1800, 300); },
      // Contrat accepté : le tampon.
      tampon: () => { ton(90, 0.35, 0.25, 'sine', 40); souffle(0.12, 0.1, 2500, 600); setTimeout(() => ton(660, 0.4, 0.05, 'triangle'), 90); },
      refus: () => ton(220, 0.3, 0.07, 'triangle', 110),
      clic: () => ton(1500, 0.04, 0.03, 'square'),
      ouverture: () => souffle(0.4, 0.05, 300, 2500),
    };
  })();

  function envoyer(message) {
    if (enJeu) window.chrome.webview.postMessage(message);
    else if (gestionLocale) gestionLocale(message);
  }

  if (enJeu) window.chrome.webview.addEventListener('message', (e) => recevoir(e.data));

  // ---------------------------------------------------------------- Outils

  function el(balise, classe, texte) {
    const n = document.createElement(balise);
    if (classe) n.className = classe;
    if (texte !== undefined) n.textContent = texte;
    return n;
  }

  function icone(id) {
    const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
    svg.setAttribute('viewBox', '0 0 24 24');
    const use = document.createElementNS('http://www.w3.org/2000/svg', 'use');
    use.setAttribute('href', '#' + id);
    svg.append(use);
    return svg;
  }

  function plus_tard(fonction, delai) {
    minuteries.push(setTimeout(fonction, delai));
  }

  function annulerAnimations() {
    minuteries.forEach(clearTimeout);
    minuteries.forEach(clearInterval);
    minuteries = [];
  }

  function urlImage(chemin, essai) {
    if (!chemin || !etat) return '';
    return etat.base + chemin.split('/').map(encodeURIComponent).join('/') + (essai ? '?n=' + essai : '');
  }

  // Une image du jeu en fond, si elle charge. Une image pas encore téléchargée
  // par le mod est redemandée (elle seule) quand il en annonce de nouvelles.
  let manquantes = [];

  // Une image arrivée en retard ne s'applique que si le nœud l'attend encore :
  // pendant que la roulette défile, l'image d'une valeur intermédiaire (un fusil
  // de précision) se posait parfois après la valeur tirée (« Pistolet »).
  function poserImage(noeud, chemin, quandChargee, essai) {
    const url = urlImage(chemin, essai);
    noeud.dataset.attendu = chemin || '';
    if (!url) return;
    const img = new Image();
    img.onload = () => {
      if (noeud.dataset.attendu !== chemin) return;
      noeud.style.backgroundImage = `url("${url}")`;
      if (quandChargee) quandChargee();
    };
    img.onerror = () => manquantes.push([noeud, chemin, quandChargee]);
    img.src = url;
  }

  function reessayerImages() {
    const liste = manquantes;
    manquantes = [];
    liste.forEach(([noeud, chemin, quandChargee]) => {
      if (noeud.isConnected) poserImage(noeud, chemin, quandChargee, etat.images);
    });
  }

  const auHasard = (liste) => liste[Math.floor(Math.random() * liste.length)];

  // ---------------------------------------------------------------- Accueil

  // Les modes (Julien, 24/11/2026) : ce que le tirage imposera, une ligne
  // par sujet (tenue, équipement, méthodes, exigences).
  const MODES = [
    { cle: 'assassin', defaut: true, lignes: [['i-costume', 1], ['i-Armes', 2], ['i-Accidents', 3], ['i-oeil', 4]] },
    { cle: 'puriste', lignes: [['i-costume', 1], ['i-sac', 2], ['i-Poisons', 3], ['i-oeil', 4], ['i-ko', 5]] },
    { cle: 'freestyle', lignes: [['i-de', 1], ['i-porte', 2], ['i-reglages', 3]] },
  ];
  const nomMode = (cle) => t('mode_' + ((MODES.find((m) => m.cle === cle) || MODES[0]).cle));

  function rendreModes() {
    const zone = $('modes');
    const actif = (etat.reglages && etat.reglages.mode) || 'assassin';
    zone.replaceChildren();
    MODES.forEach((m) => {
      const b = el('button', 'mode' + (m.cle === actif ? ' actif' : ''));
      b.setAttribute('role', 'radio');
      b.setAttribute('aria-checked', m.cle === actif ? 'true' : 'false');
      const tete = el('div', 'mode__tete');
      tete.append(el('span', 'mode__nom', t('mode_' + m.cle)));
      if (m.defaut) tete.append(el('span', 'mode__defaut', t('par_defaut')));
      const lignes = el('ul', 'mode__lignes');
      m.lignes.forEach(([idIcone, n]) => {
        const li = el('li', 'mode__ligne');
        li.append(icone(idIcone), el('span', '', t('mode_' + m.cle + '_' + n)));
        lignes.append(li);
      });
      b.append(tete, el('span', 'mode__accroche', t('mode_' + m.cle + '_accroche')), lignes);
      // Puriste « sans filet » : la série de contrats réussis d'affilée.
      if (m.cle === 'puriste' && etat.reglages && etat.reglages.seriePuriste != null) {
        tete.append(el('span', 'mode__serie', t('serie_puriste') + etat.reglages.seriePuriste));
      }
      b.onclick = () => {
        if (m.cle === actif) return;
        son.clic();
        envoyer({ action: 'reglage', cle: 'mode', valeur: m.cle });
      };
      zone.append(b);
    });
  }

  function rendreAccueil() {
    const enMission = etat.enMission;
    const texte = $('accueil-texte');
    const b = $('btn-tirer');

    if (enMission) {
      texte.textContent = t('accueil_en_mission');
      b.hidden = true;
      $('modes').hidden = true;
    } else {
      b.hidden = false;
      b.disabled = !etat.tirable;
      $('modes').hidden = false;
      rendreModes();
      texte.textContent = etat.tirable ? t('accueil_tirable') : t('accueil_menu');
    }

    // La mosaïque des destinations possédées, une seule fois.
    const cle = etat.destinations.map((d) => d.image).join('|');
    if (cle === mosaiquePosee) return;
    mosaiquePosee = cle;
    const m = $('mosaique');
    m.replaceChildren();
    const images = etat.destinations.map((d) => d.image).filter(Boolean);
    for (let i = 0; i < 36 && images.length; i++) {
      const c = el('div');
      m.append(c);
      poserImage(c, images[(i * 7) % images.length], () => c.classList.add('charge'));
    }
  }

  // ---------------------------------------------------------------- Tuiles

  function tuile(titre) {
    const t = el('div', 'tuile');
    const texte = el('div', 'tuile__texte');
    texte.append(el('span', 'tuile__titre', titre), el('span', 'tuile__valeur'), el('span', 'tuile__famille'));
    t.append(el('div', 'tuile__visuel'), texte);
    return t;
  }

  function remplirMethode(t, methode) {
    const visuel = t.querySelector('.tuile__visuel');
    t.classList.remove('tuile--image');
    visuel.style.backgroundImage = '';
    visuel.dataset.attendu = '';
    const idIcone = !methode ? 'i-choix' : ICONES_METHODE[methode.cle] || ICONES_FAMILLE[methode.familleCle || methode.famille] || 'i-choix';
    visuel.replaceChildren(icone(idIcone));
    t.querySelector('.tuile__valeur').textContent = methode ? methode.libelle : tr('au_choix');
    t.querySelector('.tuile__famille').textContent = methode ? methode.famille : '';
    if (methode && methode.image) {
      poserImage(visuel, methode.image, () => { visuel.replaceChildren(); t.classList.add('tuile--image'); });
    }
  }

  function remplirTenue(t, tenue) {
    const visuel = t.querySelector('.tuile__visuel');
    t.classList.remove('tuile--image');
    visuel.style.backgroundImage = '';
    visuel.dataset.attendu = '';
    visuel.replaceChildren(icone(!tenue ? 'i-choix' : tenue.costume ? 'i-costume' : 'i-silhouette'));
    t.querySelector('.tuile__valeur').textContent = tenue ? tenue.nom : tr('au_choix');
    t.querySelector('.tuile__famille').textContent = tenue && tenue.costume ? tr('nimporte_costume') : '';
    if (tenue && tenue.image) {
      poserImage(visuel, tenue.image, () => { visuel.replaceChildren(); t.classList.add('tuile--image'); });
    }
  }

  // Les valeurs défilent comme une roulette, puis la vraie tombe.
  function faireRouler(t, remplir, catalogue, finale, depart, duree) {
    if (!catalogue || catalogue.length < 2) {
      plus_tard(() => remplir(t, finale), depart);
      return;
    }
    plus_tard(() => {
      t.classList.add('tuile--roule');
      const tour = setInterval(() => { remplir(t, auHasard(catalogue)); son.tic(); }, 80);
      minuteries.push(tour);
      plus_tard(() => {
        clearInterval(tour);
        t.classList.remove('tuile--roule');
        remplir(t, finale);
        son.tombe();
        t.classList.add('tuile--tombe');
        t.addEventListener('animationend', () => t.classList.remove('tuile--tombe'), { once: true });
      }, duree);
    }, depart);
  }

  // ---------------------------------------------------------------- Contrat

  function remplirBandeau(d, mission) {
    const m = $('bandeau-mode');
    m.hidden = !etat.modeContrat;
    m.textContent = etat.modeContrat ? t('mode_prefixe') + ' ' + nomMode(etat.modeContrat) : '';
    const fond = $('bandeau-fond');
    $('bandeau-lieu').textContent = d ? d.nom : (mission.lieu || '');
    $('bandeau-mission').textContent = d && d.mission ? d.mission.titre : (mission.titre || '');
    fond.classList.remove('charge');
    fond.style.backgroundImage = '';
    if (d) poserImage(fond, d.fond || d.image, () => fond.classList.add('charge'));
    const v = $('bandeau-vignette');
    v.style.backgroundImage = '';
    v.hidden = !(d && d.mission && d.mission.image);
    if (!v.hidden) poserImage(v, d.mission.image);
  }

  function rendreBandeau(animer) {
    const b = $('bandeau');
    const d = etat.destination;
    if (!animer || etat.destinations.length < 2) {
      remplirBandeau(d, etat.mission || {});
      return;
    }
    // Les destinations possédées défilent, image comprise, puis la tirée tombe.
    b.classList.add('bandeau--roule');
    const fond = $('bandeau-fond');
    const tour = setInterval(() => {
      const x = auHasard(etat.destinations);
      $('bandeau-lieu').textContent = x.nom;
      $('bandeau-mission').textContent = '';
      fond.style.backgroundImage = `url("${urlImage(x.image)}")`;
      fond.classList.add('charge');
      son.tic();
    }, 90);
    minuteries.push(tour);
    plus_tard(() => {
      clearInterval(tour);
      b.classList.remove('bandeau--roule');
      remplirBandeau(d, etat.mission || {});
      son.impact();
      b.classList.add('bandeau--tombe');
      b.addEventListener('animationend', () => b.classList.remove('bandeau--tombe'), { once: true });
    }, 1000);
  }

  function carteCible(c) {
    const carte = el('article', 'cible');
    const portrait = el('div', 'portrait');
    const repli = el('div', 'portrait__sans-image');
    repli.append(icone('i-silhouette'));
    portrait.append(repli);
    poserImage(portrait, c.image, () => repli.remove());

    const nom = el('div', 'portrait__nom');
    nom.append(el('span', 'sur', c.officielle === false ? t('cible_roulette') : t('cible')),
      el('span', 'portrait__libelle', c.nom || t('cible')));
    if (c.officielle === false) carte.classList.add('cible--roulette');
    portrait.append(nom);

    if (c.etat === 'reussi' || c.etat === 'echoue') {
      const tampon = el('div', 'portrait__tampon portrait__tampon--' + c.etat);
      const s = el('span');
      s.append(icone(c.etat === 'reussi' ? 'i-ok' : 'i-ko'), c.etat === 'reussi' ? t('elimine') : t('echoue'));
      tampon.append(s);
      portrait.append(tampon);
    }

    const tuiles = el('div', 'tuiles');
    const tMethode = tuile(t('methode'));
    const tTenue = tuile(t('tenue'));
    tuiles.append(tMethode, tTenue);
    carte.append(portrait, tuiles);
    if (c.detail) carte.append(el('div', 'cible__detail', c.detail));
    return { carte, tMethode, tTenue };
  }

  function ligneComplication(c) {
    const n = el('div', 'complication complication--' + (c.etat || 'en_cours'));
    const image = el('div', 'complication__image');
    image.append(icone(ICONES_COMPLICATION[c.cle] || 'i-choix'));
    if (c.image) poserImage(image, c.image, () => image.replaceChildren());
    const textes = el('div');
    textes.append(el('div', 'complication__titre', c.titre), el('div', 'complication__detail', c.detail));
    const marque = el('div', 'complication__etat');
    if (c.etat === 'reussi') marque.append(icone('i-ok'));
    if (c.etat === 'echoue') marque.append(icone('i-ko'));
    n.append(image, textes, marque);
    return n;
  }

  // Le costume de départ : la seule pièce de la préparation que le joueur
  // choisit (Julien, 25/11/2026 : sans effet sur le contrat).
  function ligneCostume() {
    const c = etat.costumeDepart;
    if (!c) return null;
    const n = el('button', 'prep prep--choix');
    const image = el('div', 'prep__image');
    image.append(icone('i-costume'));
    if (c.image) poserImage(image, c.image, () => image.replaceChildren());
    const textes = el('div');
    textes.append(el('div', 'prep__titre', t('costume_depart')), el('div', 'prep__valeur', c.nom));
    n.append(image, textes, el('span', 'prep__changer', t('costume_changer')));
    n.onclick = ouvrirVestiaire;
    return n;
  }

  let filtreVestiaire = '';

  function ouvrirVestiaire() {
    son.clic();
    filtreVestiaire = '';
    $('vestiaire-recherche').value = '';
    $('vestiaire').hidden = false;
    envoyer({ action: 'costumes' });
    rendreVestiaire();
    $('vestiaire-recherche').focus();
  }

  function fermerVestiaire() { $('vestiaire').hidden = true; }

  function rendreVestiaire() {
    const grille = $('vestiaire-grille');
    const defile = grille.scrollTop;
    grille.replaceChildren();
    const choisi = etat.costumeDepart ? etat.costumeDepart.id : '';
    const f = filtreVestiaire.trim().toLocaleLowerCase(langue);
    const choisir = (id) => { son.clic(); envoyer({ action: 'costume', id }); fermerVestiaire(); };

    if (!f) {
      const defaut = el('button', 'costume costume--defaut' + (choisi ? '' : ' costume--choisi'));
      defaut.append(icone('i-costume'), el('span', 'costume__nom', t('costume_du_jeu')));
      defaut.onclick = () => choisir('');
      grille.append(defaut);
    }
    (etat.costumes || [])
      .filter((c) => !f || c.nom.toLocaleLowerCase(langue).includes(f))
      .sort((a, b) => a.nom.localeCompare(b.nom, langue))
      .forEach((c) => {
        const b = el('button', 'costume' + (c.id === choisi ? ' costume--choisi' : ''));
        b.title = c.nom;
        b.append(el('span', 'costume__nom', c.nom));
        if (c.image) poserImage(b, c.image);
        b.onclick = () => choisir(c.id);
        grille.append(b);
      });
    grille.scrollTop = defile;
  }

  $('vestiaire-fermer').onclick = fermerVestiaire;
  $('vestiaire-recherche').oninput = (e) => { filtreVestiaire = e.target.value; rendreVestiaire(); };

  // ---------------------------------------------------------------- Difficulté

  function niveauDifficulte(note) { return note <= 2 ? 1 : note <= 4 ? 2 : note <= 6 ? 3 : note <= 8 ? 4 : 5; }

  // Animé : la note tombe une fois tout le contrat posé (Julien, 25/11/2026 :
  // elle apparaissait avant les cibles), à « fin » millisecondes.
  function rendreDifficulte(animer, fin) {
    const d = etat.difficulte;
    const zone = $('difficulte');
    zone.hidden = !d;
    if (!d) return;
    const niveau = niveauDifficulte(d.note);
    zone.className = 'difficulte difficulte--' + niveau;
    const facteurs = (d.facteurs || []).slice(0, 3);
    zone.title = t('diff_aide') + (facteurs.length ? '\n\n' + t('diff_poids') + ' :\n' +
      facteurs.map((f) => '• ' + f.texte + (f.cible ? ' — ' + f.cible : '')).join('\n') : '');
    zone.replaceChildren();

    const tete = el('div', 'difficulte__tete');
    tete.append(el('span', 'difficulte__note', String(d.note)), el('span', 'difficulte__sur', '/ 10'),
      el('span', 'difficulte__libelle', t('diff_' + niveau)));
    const jauge = el('div', 'difficulte__jauge');
    for (let i = 1; i <= 10; i++) {
      const c = el('i', i <= d.note ? 'plein' : '');
      c.style.animationDelay = (150 + i * 45) + 'ms';
      jauge.append(c);
    }
    zone.append(el('div', 'difficulte__titre', t('difficulte')), tete, jauge);
    if (facteurs.length) zone.append(el('div', 'difficulte__poids', facteurs.map((f) => f.texte).join(' · ')));

    if (animer) {
      zone.style.visibility = 'hidden';
      plus_tard(() => {
        zone.style.visibility = '';
        zone.classList.add('difficulte--entree');
        son.impact();
      }, fin);
    }
  }

  function lignesPreparation() {
    const p = etat.preparation;
    const costume = ligneCostume();
    if (!p) return costume ? [costume] : [];
    const lignes = [[t('lieu_depart'), p.entree, 'i-porte'], [t('arme_dissimulee'), p.arme, 'i-Armes'], [t('equipement'), p.objet, 'i-sac'],
      [t('equipement'), p.objet2, 'i-sac']]
      .filter(([, objet]) => objet)
      .map(([titre, objet, idIcone]) => {
        const n = el('div', 'prep');
        if (objet.etat === 'reussi' || objet.etat === 'echoue') n.classList.add('prep--' + objet.etat);
        const image = el('div', 'prep__image');
        image.append(icone(idIcone));
        if (objet.image) poserImage(image, objet.image, () => image.replaceChildren());
        const textes = el('div');
        const titreN = el('div', 'prep__titre', titre + (objet.etat === 'echoue' ? t('non_emporte') : ''));
        textes.append(titreN, el('div', 'prep__valeur', objet.nom));
        n.append(image, textes);
        return n;
      });
    if (costume) lignes.splice(p.entree ? 1 : 0, 0, costume);
    return lignes;
  }

  // Tout le contrat. Animé : la destination roule, puis les cibles arrivent
  // une à une (balayage rouge), méthodes et tenues défilent, puis les
  // complications et la préparation glissent en place.
  function rendreContrat(animer) {
    annulerAnimations();
    manquantes = [];
    rendreBandeau(animer);

    const zone = $('cibles');
    zone.replaceChildren();
    const base = animer ? 1000 : 0;
    const cat = etat.catalogue || {};

    etat.cibles.forEach((c, i) => {
      const { carte, tMethode, tTenue } = carteCible(c);
      if (animer) {
        carte.style.visibility = 'hidden';
        plus_tard(() => { carte.style.visibility = ''; carte.classList.add('cible--entree'); son.passe(); }, base + i * 260);
        faireRouler(tMethode, remplirMethode, cat.methodes, c.methode, base + i * 260 + 250, 800);
        faireRouler(tTenue, remplirTenue, cat.tenues, c.tenue, base + i * 260 + 350, 950);
      } else {
        remplirMethode(tMethode, c.methode);
        remplirTenue(tTenue, c.tenue);
      }
      zone.append(carte);
    });

    const apres = base + etat.cibles.length * 260 + (animer ? 400 : 0);

    const compl = $('complications');
    compl.replaceChildren();
    etat.complications.forEach((c, j) => {
      const n = ligneComplication(c);
      if (animer) { n.classList.add('complication--entree'); n.style.animationDelay = (apres + j * 120) + 'ms'; }
      compl.append(n);
    });

    const prep = $('preparation');
    prep.replaceChildren();
    const lignes = lignesPreparation();
    $('bloc-preparation').hidden = !lignes.length;
    lignes.forEach((n, k) => {
      if (animer) { n.classList.add('prep--entree'); n.style.animationDelay = (apres + 300 + k * 120) + 'ms'; }
      prep.append(n);
    });

    // La note, après la dernière tenue arrêtée et la dernière ligne posée.
    const finCibles = base + Math.max(0, etat.cibles.length - 1) * 260 + 350 + 950;
    const finPreparation = apres + 300 + Math.max(0, lignes.length - 1) * 120 + 450;
    rendreDifficulte(animer, Math.max(finCibles, finPreparation) + 200);
  }

  // ---------------------------------------------------------------- En-tête et boutons

  function rendreEntete() {
    const e = etat.etat;
    const d = etat.destination;
    const avecContrat = e !== 'hors_mission' && !(e === 'attente' && !etat.cibles.length);

    $('entete-sur').textContent = avecContrat ? (d ? d.nom : (etat.mission.lieu || '')) : (etat.enMission ? t('en_mission') : t('menus_jeu'));
    $('entete-titre').textContent = avecContrat ? (d && d.mission ? d.mission.titre : (etat.mission.titre || '')) : t('roulette_contrats');

    const chip = $('etat-contrat');
    chip.className = 'etat etat--' + (etat.rompu ? 'echoue' : e);
    chip.textContent = etat.rompu ? t('etat_echoue') : (e ? t('etat_' + e) : '');

    const horsMission = !etat.enMission;
    $('btn-accepter').hidden = e !== 'propose';
    $('btn-refuser').hidden = !(horsMission && (e === 'propose' || e === 'accepte'));
    $('btn-refuser-texte').textContent = e === 'accepte' ? t('abandonner') : t('refuser');
    $('btn-relancer').hidden = !avecContrat || !etat.relancePossible;
    $('btn-code').hidden = !(horsMission && etat.codeContrat);
    // Code reçu : refusé, ou importé avec des éléments non débloqués.
    const message = etat.erreurCode || etat.avertissementCode || '';
    $('code-message').hidden = !message;
    $('code-message').textContent = message;
    $('code-message').classList.toggle('code__message--erreur', !!etat.erreurCode);

    $('aide').textContent =
      e === 'accepte' ? (etat.pilote ? t('aide_pilote') + etat.pilote
        : etat.reglages.pilote ? t('aide_ouvre')
        : t('aide_manuel'))
      : etat.dansLeContrat ? t('aide_contrat')
      : etat.enMission && etat.cibles.length && !etat.relancePossible ? t('aide_fige')
      : '';

    const rompu = !!etat.rompu;
    $('rupture').hidden = !rompu;
    $('rupture-raison').textContent = etat.raison || '';
    $('btn-fermer').hidden = rompu;
  }

  function rendreReglages() {
    const r = etat.reglages;
    document.querySelectorAll('[data-reglage]').forEach((c) => { c.checked = !!r[c.dataset.reglage]; });

    // Méthodes, tenues et familles ne se règlent qu'en Freestyle : les autres
    // modes les fixent eux-mêmes.
    const libre = r.mode === 'freestyle';
    document.querySelectorAll('[data-freestyle]').forEach((n) => { n.hidden = !libre; });
    $('note-freestyle').hidden = libre;

    const segments = (id, cle, de, a) => {
      const seg = $(id);
      seg.replaceChildren();
      for (let n = de; n <= a; n++) {
        const b = el('button', n === r[cle] ? 'actif' : '', String(n));
        b.onclick = () => envoyer({ action: 'reglage', cle, valeur: n });
        seg.append(b);
      }
    };
    segments('nb-complications', 'complications', 0, 3);
    segments('nb-cibles-min', 'ciblesMin', 1, 3);
    segments('nb-cibles-max', 'ciblesMax', 1, 3);

    // Les destinations que le tirage peut proposer (toutes cochées par défaut).
    const lieux = $('destinations-reglage');
    lieux.replaceChildren();
    (etat.destinations || []).forEach((d) => {
      const b = el('button', d.active ? 'actif' : '', d.nom);
      b.onclick = () => envoyer({ action: 'reglage', cle: 'destination', id: d.id, valeur: !d.active });
      lieux.append(b);
    });

    const puces = $('familles');
    puces.replaceChildren();
    etat.familles.forEach((f, i) => {
      const b = el('button', r.familles[i] ? 'actif' : '');
      b.append(icone(ICONES_FAMILLE[(etat.famillesCles || [])[i] || f] || 'i-choix'), f);
      b.onclick = () => envoyer({ action: 'reglage', cle: 'famille', index: i, valeur: !r.familles[i] });
      puces.append(b);
    });
  }

  // ---------------------------------------------------------------- Réception

  function recevoir(nouveau) {
    etat = nouveau;
    if ((etat.langue || 'fr') !== langue || !langueAppliquee) {
      appliquerLangue(etat.langue || 'fr');
      langueAppliquee = true;
      dernierTirage = dernierTirage === null ? null : '';  // tout redessiner dans la nouvelle langue
    }
    document.body.classList.toggle('fiche-ouverte', etat.ouverte);
    $('fiche').hidden = !etat.ouverte;

    const signature = JSON.stringify(etat.cibles.map((c) => [c.nom, c.methode, c.tenue])) + '|' + etat.tirage;
    const etats = JSON.stringify([etat.cibles.map((c) => [c.etat, c.detail]), etat.complications.map((c) => c.etat),
      etat.etat, etat.preparation, etat.rompu, etat.destination && etat.destination.nom, etat.costumeDepart,
      etat.difficulte && etat.difficulte.note]);
    const nouveauTirage = signature !== dernierTirage;

    // On anime un contrat qui vient d'être tiré SOUS LES YEUX (fiche ouverte).
    const animer = etat.ouverte && ouverteAvant && nouveauTirage && dernierTirage !== null && etat.etat === 'propose';
    if (etat.ouverte && !ouverteAvant) son.ouverture();

    if (etat.ouverte) {
      const e = etat.etat;
      const accueil = e === 'hors_mission' || (e === 'attente' && !etat.cibles.length);
      $('vue-accueil').hidden = !accueil;
      $('vue-contrat').hidden = accueil;

      rendreEntete();
      if (accueil) {
        rendreAccueil();
      } else if (animer || !ouverteAvant || nouveauTirage || etats !== dernierEtats) {
        rendreContrat(animer);
      } else if (etat.images !== dernieresImages) {
        reessayerImages();
      }
      rendreReglages();
    } else {
      annulerAnimations();
    }

    // Le sélecteur de costume suit l'état (images arrivées, contrat parti).
    if (!$('vestiaire').hidden) {
      if (!etat.ouverte || !etat.costumeDepart) fermerVestiaire();
      else if (etat.images !== dernieresImages) rendreVestiaire();
    }
    suivreLancement();

    dernierTirage = signature;
    dernierEtats = etats;
    dernieresImages = etat.images;
    ouverteAvant = etat.ouverte;

  }

  // ---------------------------------------------------------------- Lancement
  //
  // Contrat accepté : le mod ouvre la préparation, qui met quelques secondes à
  // venir (Julien, 25/11/2026). La fiche montre la roue jusqu'à ce que le mod
  // dise « prete », puis se ferme. Si rien ne vient en 30 s, elle se ferme
  // quand même : le jeu est peut-être déjà sur la page.
  let lancementVu = false;
  let fermetureLancement = null;
  let limiteLancement = null;

  function suivreLancement() {
    const l = etat.lancement;
    if (!etat.ouverte || !l) lancementVu = false;
    else if (l === 'attente') lancementVu = true;

    const montrer = lancementVu && (l === 'attente' || l === 'prete');
    $('chargement').hidden = !montrer;
    if (montrer) {
      const d = etat.destination;
      $('chargement-detail').textContent = [d ? d.nom : etat.mission.lieu, d && d.mission ? d.mission.titre : etat.mission.titre]
        .filter(Boolean).join(' — ');
    }

    if (l === 'attente' && lancementVu && !limiteLancement) {
      limiteLancement = setTimeout(() => {
        limiteLancement = null;
        if (etat.lancement === 'attente' && lancementVu) { lancementVu = false; $('chargement').hidden = true; envoyer({ action: 'fermer' }); }
      }, 30000);
    }
    if (l !== 'attente' && limiteLancement) { clearTimeout(limiteLancement); limiteLancement = null; }

    if (l === 'prete' && lancementVu && !fermetureLancement) {
      fermetureLancement = setTimeout(() => {
        fermetureLancement = null;
        lancementVu = false;
        $('chargement').hidden = true;
        envoyer({ action: 'fermer' });
      }, 600);
    }
  }

  // ---------------------------------------------------------------- Boutons

  $('btn-tirer').onclick = () => { son.clic(); envoyer({ action: 'tirer' }); };
  $('btn-accepter').onclick = () => {
    son.tampon();
    fermerVestiaire();
    envoyer({ action: 'accepter' });
    // Le mod mène ensuite les menus : la fiche montre l'écran de chargement
    // (suivreLancement) et se ferme quand la préparation est ouverte.
  };
  $('btn-refuser').onclick = () => { son.refus(); envoyer({ action: 'refuser' }); };
  $('btn-relancer').onclick = () => { son.clic(); envoyer({ action: 'relancer' }); };

  // Code de contrat : copié dans le presse-papiers, ou collé puis chargé.
  $('btn-code').onclick = async () => {
    if (!etat || !etat.codeContrat) return;
    son.clic();
    let ok = false;
    try { await navigator.clipboard.writeText(etat.codeContrat); ok = true; } catch (e) { /* repli plus bas */ }
    if (!ok) {
      const zone = el('textarea');
      zone.value = etat.codeContrat;
      document.body.append(zone);
      zone.select();
      try { ok = document.execCommand('copy'); } catch (e) { ok = false; }
      zone.remove();
    }
    const texte = $('btn-code-texte');
    texte.textContent = ok ? t('code_copie') : t('code_echec_copie');
    setTimeout(() => { texte.textContent = t('code_copier'); }, 1800);
  };
  const chargerCode = () => {
    const code = $('champ-code').value.trim();
    if (!code) return;
    son.clic();
    envoyer({ action: 'importer', code });
    $('champ-code').value = '';
  };
  $('form-code').onsubmit = (e) => { e.preventDefault(); chargerCode(); };
  $('btn-charger').onclick = chargerCode;
  $('btn-fermer').onclick = () => envoyer({ action: 'fermer' });
  $('btn-reglages').onclick = () => { $('reglages').hidden = false; };
  $('btn-retour').onclick = () => { $('reglages').hidden = true; };

  document.querySelectorAll('[data-reglage]').forEach((c) => {
    c.onchange = () => envoyer({ action: 'reglage', cle: c.dataset.reglage, valeur: c.checked });
  });

  document.addEventListener('keydown', (e) => {
    if (e.key === 'Escape' && !$('vestiaire').hidden) { fermerVestiaire(); return; }
    if (e.key === 'Escape' && !(etat && etat.rompu)) envoyer({ action: 'fermer' });
  });

  document.addEventListener('DOMContentLoaded', () => envoyer({ action: 'prete' }));

  return {
    recevoir,
    set local(f) { gestionLocale = f; },
  };
})();
