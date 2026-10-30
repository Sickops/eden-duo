# Eden Duo — second écran sous Windows

Ce fork d'[Eden Duo](https://github.com/igawa6/eden-duo) ajoute au frontal Qt de Windows ce que
l'application Android faisait déjà : un **second écran réel**, dans sa propre fenêtre, pour les
mods dual-screen (`.dsmod.zip`).

Le runtime Duo (découverte des paquets, service `dsm:u`, compositeur du compagnon) est déjà
multi-plateforme ; il est réutilisé tel quel. Ce fork n'ajoute que la partie qui manquait au
frontal Qt : une fenêtre secondaire avec surface Vulkan, et un installateur de paquets.

## Ce qui a été ajouté

| Fichier | Rôle |
| --- | --- |
| `src/yuzu/duo/aux_window.{h,cpp}` | Fenêtre Qt secondaire (`AuxRenderWindow`) : surface Vulkan native, présentation de la sortie aux, tactile/souris/molette vers `DSMod::AuxRouting`. |
| `src/yuzu/duo/dsmod_installer.{h,cpp}` | Installateur `.dsmod.zip` (mêmes contrôles que `DualScreenPackageInstaller.kt` : `package.json`, arborescence `dualscreen/`, protection zip-slip, doublons, limites de taille). |
| `src/yuzu/duo_qt.cmake` | Ajoute les sources ci-dessus à la cible `yuzu` et lie QuaZip. |
| `src/yuzu/main_window.{h,cpp}` | Menu **View > Second Screen (DSMod)**, menu **File > Install Dual Screen Mod...**, attachement/détachement au cycle de vie de l'émulation. |
| `src/qt_common/config/uisettings.h` | Réglage persistant `enable_second_screen`. |
| `src/video_core/renderer_vulkan/vk_dsmod_aux.cpp` | Recréation du swapchain aux quand la fenêtre secondaire change de taille. |
| `src/core/mods/mod_module.cpp` | **Modules natifs Windows** : plateforme `windows-x86_64` (et `windows-arm64`), copie de l'image dans le cache puis `LoadLibrary`, erreurs de chargement détaillées. |
| `src/core/mods/modules/dsmod_module_sdk.h` | `DSMOD_SDK_EXPORT` portable : `__attribute__((visibility))` sur ELF/Mach-O, vide sous MSVC (les exports sont nommés sur la ligne de link). |
| `companions-windows.patch` | Patch à appliquer au dépôt des compagnons pour que leur `CMakeLists.txt` produise une DLL MSVC (`/EXPORT:` par symbole). |
| `src/common/dynamic_library.{h,cpp}` | Surcharge `Open(std::filesystem::path)` : `LoadLibraryW`, donc un chemin de cache non-ASCII (nom d'utilisateur accentué) charge quand même. |

## Prérequis

- **Vulkan** comme moteur graphique (le présentateur aux n'est implémenté qu'en Vulkan).
- Un mod dual-screen au format `.dsmod.zip` (racine : `package.json` + `dualscreen/...`).
- MSVC récent (testé avec Visual Studio 18 Insiders / MSVC 14.52), Ninja, Qt 6 et le SDK Vulkan
  fournis par la configuration CPM du projet.

## Compilation

```bat
call "C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvarsall.bat" x64
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release ^
      -DYUZU_TESTS=OFF -DENABLE_QT_TRANSLATION=OFF -DENABLE_WEB_SERVICE=OFF ^
      -DENABLE_UPDATE_CHECKER=OFF -DUSE_DISCORD_PRESENCE=OFF -DENABLE_WERROR=OFF
ninja -C build yuzu
```

L'exécutable est produit dans `build\bin\eden.exe`.

> **Note MSVC 19.50 (VS 18).** L'arbre 1.3.2.2 a été écrit pour MSVC 19.4x et promeut un jeu de
> warnings en erreurs. Le nouveau compilateur en signale davantage sur du code correct, ce qui
> bloquait la compilation. Une option CMake, `EDEN_MSVC_STRICT_WARNINGS`, désactive cette
> promotion automatiquement à partir de MSVC 19.50. Pour forcer l'ancien comportement :
> `-DEDEN_MSVC_STRICT_WARNINGS=ON`.

## Utilisation

1. **File > Install Dual Screen Mod...**, choisir le `.dsmod.zip`.
   Le paquet est installé dans
   `%APPDATA%\eden\load\<TITLEID>\<Nom>-<version>\`
   (ou `<exe>\user\load\...` en mode portable), là où le runtime Duo le découvre.
2. Lancer le jeu compatible.
3. **View > Second Screen (DSMod)** (ou l'activer avant le lancement, le réglage est mémorisé).

La fenêtre s'ouvre de préférence sur un écran autre que celui du jeu. Si un seul écran est
disponible, elle s'ouvre comme une fenêtre normale. Elle est redimensionnable : le swapchain aux
est recréé à la nouvelle taille, et le compagnon s'adapte via `dsm:u`.

Fermer la fenêtre secondaire ne coupe pas l'émulation : elle se cache et la case du menu se
décoche. La réactiver la fait réapparaître.

Un mod installé pendant qu'un jeu tourne n'est pris en compte qu'au prochain lancement du jeu
(la découverte des paquets se fait au démarrage de l'émulation).

## Vérifications effectuées

Sur une machine Windows 11 x64, MSVC 19.52, Qt 6.11.1, SDK Vulkan 1.4.335, avec un jeu commercial
lancé par `-g` :

| Scénario | Résultat |
| --- | --- |
| Démarrage du frontal sans jeu | OK |
| Lancement d'un jeu avec `enable_second_screen` actif | `DSMod second screen attached (1920x1032)` puis `DSMod aux window attached (1920x1009)` : surface et swapchain Vulkan créés |
| Maximisation / changement de taille | swapchain aux recréé à la nouvelle taille |
| Fermeture de la fenêtre secondaire en cours de jeu | `DSMod aux window detached` / `DSMod second screen detached`, l'émulation continue |
| Arrêt de l'émulation avec la fenêtre secondaire ouverte | détachement propre puis sortie normale, sans plantage |
| Persistance du réglage | écrit dans `qt-config.ini` (`[UI] enable_second_screen`), relu au démarrage |
| Installation de `0100000000010000-SuperMarioOdyssey-1.0.0.dsmod.zip` | `success=1`, arborescence complète extraite dans `%APPDATA%\eden\load\0100000000010000\SuperMarioOdysseycompanion-1.0.0` (`package.json`, `dualscreen/manifest.json`, `dualscreen/font`, `dualscreen/modules/{linux-x86_64,android-arm64-v8a}`) |
| Réinstallation du même paquet | même dossier remplacé, aucun doublon ni dossier `.installing` résiduel |
| Intégrité des modules extraits | SHA-256 identiques à ceux déclarés dans `package.json` |
| Module natif Windows (DLL de test, ABI 1) | `DSMod module: loaded Windows loader test module for title 0100F43008C44000 … (windows-x86_64)`, image copiée dans `%APPDATA%\eden\cache\dualscreen\module-*` |
| Rendu du second écran par le module | la valeur publiée par le module (`publish_text`) s'affiche dans la fenêtre secondaire, à côté du texte statique du manifeste |
| Fermeture propre pendant le jeu | détachement du second écran, `FreeLibrary` puis suppression du dossier `module-*` du cache |
| Module absent pour la plateforme | message explicite (`package has no windows-x86_64 module`) au lieu du message générique |

L'installateur a donc été exercé de bout en bout sur un vrai paquet `.dsmod.zip`.

Deux défauts corrigés pendant cette validation, utiles à connaître si le format évolue :

- `QuaZipFile::setFileName()` ne suffit pas : pour un `QuaZipFile` non « interne », `open()`
  ignore le nom et lit l'entrée sur laquelle le zip est positionné. Il faut appeler
  `QuaZip::setCurrentFile()` avant chaque ouverture (helper `OpenEntry()`), sinon on lit le
  premier fichier de l'archive à la place du fichier demandé — ce qui produisait un
  « package.json is not valid JSON » trompeur.
- Le dossier de staging (`<cible>.installing`) ne doit pas être pris pour une installation
  précédente : il contient déjà le nouveau `package.json` et se faisait donc supprimer par le
  nettoyage des anciennes versions, laissant une installation incomplète.

## Modules natifs (compagnons compilés)

Un paquet peut embarquer du code natif (`"requires_module": true`) : le runtime le charge, l'appelle
et l'affiche. Sous Windows, la plateforme est `windows-x86_64` (ou `windows-arm64` sur un build
ARM64) et l'image est une DLL exportant l'ABI Duo (`eden_dsmod_get_module`, plus les extensions
optionnelles) :

```json
"module": {
  "abi": 1,
  "build_ids": ["B1F12FD919EAE86A"],
  "libraries": {
    "windows-x86_64": {
      "path": "modules/windows-x86_64/0100F43008C44000.dll",
      "sha256": "…"
    }
  }
}
```

- Le nom de fichier doit être `<TITLEID>` suivi de `.dll` ; l'orthographe `.so` (celle des outils de
  packaging Linux/Android) est aussi acceptée pour la même DLL, le chargeur lisant l'en-tête de
  l'image et non le suffixe.
- Le module est recopié hors du paquet dans `%APPDATA%\eden\cache\dualscreen\module-<id>\` avant
  d'être chargé : une mise à jour du paquet en cours de partie ne peut donc pas remplacer l'image
  sous un module déjà chargé. Le dossier est supprimé à l'arrêt de l'émulation, et les dossiers
  laissés par un plantage sont balayés au démarrage suivant (au-delà de 10 minutes).
- Une DLL Windows se construit sans dépendance à Eden : l'ABI est en C pur
  (`src/core/mods/dsmod_module_abi.h`), et les symboles doivent être exportés explicitement
  (`__declspec(dllexport)` ou `/EXPORT:eden_dsmod_get_module`), l'en-tête ne les décorant pas.
- L'ABI n'est pas dépendante de la plateforme : un même module peut être compilé pour Windows,
  Linux et Android, chaque paquet déclarant l'entrée correspondante.

## Compiler un compagnon en DLL Windows

Exemple complet, validé sur le compagnon **Super Mario Odyssey 1.0.0** (le paquet publié ne
contenait que `linux-x86_64` et `android-arm64-v8a`).

### Prérequis

- Visual Studio avec la charge de travail C++ (MSVC x64) — testé avec VS 18 Insiders / MSVC 19.52 —
  plus CMake ≥ 3.22 et Ninja.
- Un checkout de **ce fork** Eden Duo, configuré une fois (`cmake -S . -B build ...`) pour peupler
  `.cache/cpm` : le build des compagnons y réutilise les en-têtes fmt / nlohmann-json, les sources
  zstd et `externals/bc_decoder`.
- Les sources du compagnon (`native/modules` du dépôt `eden-duo-companions`).

### Deux adaptations, une seule fois

1. **L'en-tête SDK.** Ce fork rend `DSMOD_SDK_EXPORT` portable
   (`src/core/mods/modules/dsmod_module_sdk.h`) : `__attribute__((visibility("default")))` sur
   ELF/Mach-O, macro vide sous MSVC. Sans cela `cl.exe` refuse l'en-tête, et déclarer les getters
   `__declspec(dllexport)` entrerait en conflit avec `dsmod_module_abi.h` (C2375 : redéfinition avec
   une liaison différente).
2. **Le dépôt des compagnons.** Ses sources ne connaissaient que GCC/Clang et le script de version
   GNU ld. Appliquer `companions-windows.patch` depuis la racine du dépôt des compagnons :

   ```bat
   git apply companions-windows.patch
   ```

   Le patch touche sept fichiers :

   | Fichier | Adaptation |
   | --- | --- |
   | `native/modules/CMakeLists.txt` | `project(... LANGUAGES C CXX)` (ooz/zstd/lz4 sont du C) ; `/utf-8 /bigobj` et `_USE_MATH_DEFINES`/`NOMINMAX` sur MSVC ; `/EXPORT:<symbole>` pour chaque entrée de `EXPORTS` (les symboles C x64 ne sont pas décorés, aucun script de version) ; bloc final qui retire des cibles les options GCC/Clang (`-ffp-contract`, `-fno-*`, `-ffile-prefix-map`, `-Wall/-Wextra/-Werror/-w`, `-O2`) ; cible `dsmod-belmont` (Castlevania, compilé hors CMake en amont) avec lz4 du cache CPM d'Eden |
   | `native/modules/third_party/ooz/stdafx.h` | `<intrin.h>` sur MSVC ; émulation `__builtin_clz/ctz` et macro `__forceinline` réservées aux compilateurs GNU |
   | `native/modules/01007EF00011E000.cpp` | `BOTW_DSMOD_EXPORT` au lieu d'un `__attribute__((visibility))` nu ; `GetThreadTimes` au lieu de `clock_gettime(CLOCK_THREAD_CPUTIME_ID)` pour la télémétrie |
   | `native/modules/acnh_publish_diag.cpp` | attribut `format(printf)` réservé à GCC/Clang |
   | `native/modules/p5r_recipes.h` | `_create_locale`/`_strtod_l` (UCRT) au lieu de `newlocale`/`strtod_l` |
   | `native/modules/p5r_inventory_reader.h` | `std::find` sur pointeurs, comparable à `text.data() + 16` |
   | `tools/build_dualscreen_package.py` | `windows-x86_64` accepté comme plateforme de module, charge utile `<TITLEID>.dll`, contrôle de sections ELF ignoré pour une image PE |

   Le patch s'applique sur l'archive `eden-duo-companions-super-mario-odyssey-1.0.0.zip`
   d'origine (`git apply --check` vérifié) ; il échoue si le dépôt contient déjà les modifications.

### Compilation

```bat
call "C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvarsall.bat" x64
cmake -S native/modules -B build-native-windows -G Ninja ^
      -DEDEN_SOURCE_ROOT="D:/chemin/vers/eden-duo" ^
      -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build-native-windows --target dsmod-smo --parallel 8
```

La cible porte le nom du jeu (`dsmod-smo`, `dsmod-p5r`, `dsmod-wonder`…) ; le fichier produit
s'appelle `<TITLEID>.dll`, comme le `.so` sous Linux.

Le script `companions\build-windows.bat` enchaîne ces deux commandes (adapter `EDEN_ROOT` ou le
chemin de `vcvarsall.bat` en tête de fichier) :

```bat
build-windows.bat            :: dsmod-smo
build-windows.bat dsmod-p5r  :: une autre cible
```

Contrôle des exports :

```bat
dumpbin /exports build-native-windows\0100000000010000.dll
```

doit lister `eden_dsmod_get_module`, `eden_dsmod_get_extensions`,
`eden_dsmod_get_font_extensions`, `eden_dsmod_get_save_extensions` et
`eden_dsmod_get_write_extensions`. Un seul symbole manquant suffit à ce que le module charge mais
n'expose pas la fonctionnalité correspondante (police, sauvegarde, écritures).

### Empaqueter le `.dsmod.zip`

Le module est un fichier de plus dans le paquet, et son empreinte est déclarée dans **les deux**
fichiers qui portent le bloc `module` — `package.json` **et** `dualscreen/manifest.json` (le runtime
lit celui du manifeste) :

```json
"windows-x86_64": {
  "path": "modules/windows-x86_64/0100000000010000.dll",
  "sha256": "<sha256 du .dll>"
}
```

`companions\pack-windows-module.ps1` fait l'opération sur un paquet existant : il lit le `title_id`
du `package.json`, ajoute la DLL sous `dualscreen/modules/<plateforme>/`, insère l'entrée dans les
deux JSON (formatage conservé, JSON revalidé) et réécrit l'archive :

```powershell
powershell -ExecutionPolicy Bypass -File pack-windows-module.ps1 `
    -Zip "C:\...\0100000000010000-SuperMarioOdyssey-1.0.0.dsmod.zip" `
    -Dll "build-native-windows\0100000000010000.dll" `
    -Out "C:\...\0100000000010000-SuperMarioOdyssey-1.0.0-windows.dsmod.zip"
```

Sans `-Out`, la sortie s'appelle `<nom>-windows-x86_64.dsmod.zip`. Le script refuse un paquet
purement déclaratif (aucun bloc `module.libraries`) et tout JSON invalide après modification.

Le paquet reconstruit pour Super Mario Odyssey est
`D:\Telechargement\0100000000010000-SuperMarioOdyssey-1.0.0-windows.dsmod.zip`
(DLL ajoutée sous `dualscreen/modules/windows-x86_64/`, entrée `windows-x86_64` ajoutée aux deux
JSON, `sha256` = `3db6d917ba21e793f6b7b07871da0ace186a46cf098b445eab95a54413f5f881`). Il s'installe
par **File > Install Dual Screen Mod...** comme n'importe quel paquet.

## Compiler et empaqueter tous les compagnons

Le dépôt des compagnons déclare **19 modules natifs** et **2 paquets purement déclaratifs**
(Link's Awakening, VA-11 Hall-A). Tous les modules se compilent avec MSVC sans retouche
supplémentaire une fois `companions-windows.patch` appliqué :

| Paquet (`packages/`) | Cible CMake | Title ID | Module |
| --- | --- | --- | --- |
| `SuperMarioOdyssey` | `dsmod-smo` | `0100000000010000` | oui |
| `MetroidDread` | `dsmod-dread` | `010093801237C000` | oui |
| `Persona5Royal` | `dsmod-p5r` | `01005CA01580E000` | oui |
| `MarioKart8Deluxe` | `dsmod-mk8d` | `0100152000022000` | oui |
| `SuperMarioWonder` | `dsmod-wonder` | `010015100B514000` | oui |
| `CrashTeamRacingNitroFueled` | `dsmod-ctr` | `0100F9F00C696000` | oui |
| `EchoesOfWisdom` | `dsmod-echoes` | `01008CF01BAAC000` | oui |
| `AnimalCrossingNH` | `dsmod-acnh` | `01006F8002326000` | oui |
| `FireEmblemThreeHouses` | `dsmod-fe3h` | `010055D009F78000` | oui |
| `BindingOfIsaac` | `dsmod-isaac` | `010021C000B6A000` | oui |
| `DragonQuest3HD2D` | `dsmod-dq3` | `01003E601E324000` | oui |
| `LuminescentPlatinum` | `dsmod-lp` | `0100000011D90000` | oui |
| `LuminescentPlatinumPearl` | `dsmod-lp-pearl` | `010018E011D92000` | oui |
| `YoruTomosu` | `dsmod-yoru` | `0100C2901153C000` | oui |
| `DiabloIII` | `dsmod-d3` | `01001B300B9BE000` | oui |
| `FarmingSimulatorNintendoSwitchEdition` | `dsmod-farming-simulator` | `0100D04004176000` | oui |
| `BreathOfTheWild` | `dsmod-breath-of-the-wild` | `01007EF00011E000` | oui |
| `BelmontsCurse` | `dsmod-belmont` | `0100BA4026E94000` | oui |
| `ChainedEchoes` | `dsmod-ce` | `0100C510166F0000` | oui |
| `LinksAwakening` | — | `01006BB00C6F0000` | non |
| `VA11HallA` | — | `010086500D3C8000` | non |

### Compilation

```bat
build-windows.bat all
```

ou, cible par cible, `build-windows.bat dsmod-belmont`. Le script configure
`build-native-windows\` au premier appel, puis produit un `<TITLEID>.dll` par module. Les DLL sont
autonomes (aucun runtime Eden à lier), ne dépendent que du runtime VC++ et ne contiennent ni chemin
utilisateur ni information de débogage.

### Empaquetage

```powershell
powershell -ExecutionPolicy Bypass -File build-windows-release.ps1
```

`companions\build-windows-release.ps1` reconstruit chaque paquet depuis `packages\` (pas depuis les
archives publiées) et écrit dans `companions\dist-windows\` :

```
<TITLEID>-<Nom>-<version>-windows.dsmod.zip
```

Il lit `title_id`, `min_runtime` et `module.build_ids` dans le manifeste du paquet, ajoute
`--module windows-x86_64=build-native-windows\<TITLEID>.dll`, puis compacte le manifeste
(`tools/compact_zip.py`, plafond de 4 Mio des installateurs). Trois paquets ne déclarent pas leurs
IDs de build dans leur manifeste : le script fournit les mêmes valeurs que `tools/build_release.sh`
(`ChainedEchoes`, `CrashTeamRacingNitroFueled`, `DragonQuest3HD2D`). Une cible absente est signalée
et le paquet correspondant est ignoré (`-Games smo,echoes` limite la sélection).

L'archive produite ne contient que l'entrée `windows-x86_64` : le module Linux/Android d'origine
n'est pas repris, les blocs `module` des copies mises en scène sont remplacés, jamais les sources.

### Résultat validé sur Super Mario Odyssey

Compagnon Super Mario Odyssey compilé avec MSVC 19.52, jeu lancé par `-g` sur une carte graphique
NVIDIA :

| Vérification | Résultat |
| --- | --- |
| Compilation MSVC | `dsmod-smo` → `0100000000010000.dll` (651 776 octets), aucune modification des sources du module |
| Exports | les 5 getters de l'ABI sont présents (`dumpbin /exports`) |
| Découverte du paquet | `DSMod: loaded 'SuperMarioOdysseycompanion-1.0.0' (6 pages, 0 points, …)` |
| Chargement du module | `DSMod module: loaded Super Mario Odyssey DSMod for title 0100000000010000 build B424BE15… (windows-x86_64)` |
| Rendu | fenêtre secondaire complète : carte de Cap Kingdom, marqueur « You Are Here », pièces, lunes, onglets Stats/Map/Tasks |
| Interaction | clic sur l'onglet **Stats** → page changée et données live (lunes 0/999, captures 0/52, listes) |
| Performances | `DSMod profile (CPU, 5s): sample=264 avg=595.3µs max=1251.5µs` |
| Arrêt | `DSMod aux window detached` / `DSMod second screen detached`, `FreeLibrary` puis cache `dualscreen` vidé |

Les sources extraites et le build restent dans
`D:\Projet IA\Dual screen\companions\` (`eden-duo-companions-super-mario-odyssey-1.0.0\` et
`build-native-windows\`), ce qui permet de rejouer la compilation après une mise à jour du
compagnon.

### Validation sur les autres compagnons

Les 21 paquets de `dist-windows\` ont été contrôlés un par un (structure de l'archive, entrée
`windows-x86_64` unique, `sha256` recalculé, `build_ids` présents, manifeste < 4 Mio, aucune charge
utile Linux/Android résiduelle) : aucun écart. Les 19 DLL exportent exactement les symboles déclarés
dans leur cible CMake (`dumpbin /exports`, `targets=19 problems=0`) et ne contiennent ni chemin
utilisateur, ni chemin de build, ni référence `.pdb` (scan ASCII et UTF-16).

Trois compagnons ont en plus été lancés en jeu :

| Jeu | Vérification | Résultat |
| --- | --- | --- |
| Animal Crossing: New Horizons (`01006F8002326000`, build `7FC1BAFF976AECA4`) | module, pages, police, rendu, arrêt | `DSMod module: loaded Animal Crossing: New Horizons DSMod … (windows-x86_64)` ; 10 pages ; police lue depuis la DLL (65 275 glyphes) ; page « Dodo Airlines » rendue en français ; `sample=300 avg=3.2–3.8µs` ; détachement propre et cache `dualscreen\module-*` supprimé |
| The Legend of Zelda: Breath of the Wild (`01007EF00011E000`, build `8E9978D50BDD20B4…`, v1.6.0) | module, pages, rendu, arrêt | module `windows-x86_64` chargé ; fenêtre attachée (1920×1009) ; page de titre « Choose your adventure on the main screen » rendue ; 60 fps stables avec le mod actif (`tick calls=300` par 5 s, `avg=1.2–3.3 ms`) ; détachement propre |
| The Legend of Zelda: Echoes of Wisdom (`01008CF01BAAC000`) | paquet d'un autre build | module chargé puis rejeté proprement (`dual-screen module does not support this game build`) : la fenêtre reste ouverte et affiche la couche miroir, le jeu démarre normalement |

Deux points relevés pendant ces essais :

- Un compagnon volumineux (BotW : 283 assets, manifeste de 3,4 Mo) met plusieurs dizaines de
  secondes à charger ses pages au premier affichage. Ce n'est pas un blocage : le jeu tourne à
  60 fps pendant ce temps et le log progresse (`DSMod: loaded asset …`). Le démarrage est simplement
  plus long qu'avec un compagnon léger.
- Le dossier de staging `%APPDATA%\eden\cache\dualscreen\module-*` n'est supprimé qu'au
  destructeur du module. Après un arrêt forcé du jeu il peut subsister ; il est balayé
  automatiquement au lancement suivant s'il a plus de dix minutes (comportement voulu, voir
  `SetModuleCacheDirectory`).

## Limites connues

- Vulkan uniquement (pas de présentateur aux en OpenGL).
- Un module natif doit exister pour la plateforme de l'utilisateur : un paquet qui ne fournit que
  `linux-x86_64` et `android-arm64-v8a` s'installe sous Windows mais son compagnon ne s'affiche pas
  — le log indique alors `package has no windows-x86_64 module`. Les paquets purement déclaratifs
  (sans module natif) fonctionnent sans restriction. Le paquet Super Mario Odyssey 1.0.0 publié
  pour Android est dans ce cas ; sa variante recompilée pour Windows est décrite plus haut.
- L'installateur ne vérifie pas les checksums ni la présence d'un module pour la plateforme
  courante : c'est le runtime qui le fait au lancement du jeu, avec un message explicite.
- Le second écran est attaché au chargement du jeu ; le basculer en cours de partie fonctionne,
  mais un changement de moteur graphique nécessite un redémarrage du jeu.
- Un paquet dont les `build_ids` ne couvrent pas la version du jeu installée ne s'affiche pas :
  la fenêtre reste ouverte (couche miroir) et le log indique
  `dual-screen module does not support this game build`. Les paquets de ce dépôt couvrent les
  builds publiés à leur date de sortie ; une mise à jour du jeu peut donc nécessiter une
  recompilation du compagnon.
- Deux manifestes frôlent le plafond de 4 Mio des installateurs (Persona 5 Royal 3 453 868 octets,
  Breath of the Wild 3 485 623 octets). Ils passent, mais un ajout de contenu les ferait refuser.
- La télémétrie `DSMod perf` est coûteuse à journaliser : avec `log_filter="*:Info"` le pilote SDL
  écrit deux lignes `DSMod controller GUID` à chaque rechargement d'entrées, ce qui peut faire
  grossir `eden_log.txt` de plusieurs mégaoctets par minute.

## Dépannage

- **La fenêtre s'ouvre mais reste noire / affiche une icône grisée** : aucun paquet dual-screen
  n'est installé pour ce titre. Le log (`%APPDATA%\eden\log\eden_log.txt`) le confirme avec
  `DSMod: title <TITLEID> has no dual-screen package`.
- **Rien ne s'ouvre** : vérifier que le moteur graphique est bien Vulkan et que
  `enable_second_screen` vaut `true` dans `qt-config.ini` (section `[UI]`, avec
  `enable_second_screen\default=false`). Le log doit contenir
  `DSMod second screen attached` puis `DSMod aux window attached`.
- **Le mod est ignoré** : contrôler que `package.json` porte bien le `title_id` du jeu et que le
  dossier installé contient `dualscreen/manifest.json`.
- **Second écran noir / page vide alors que le paquet est installé** : regarder les lignes
  `DSMod module:` du log. `package has no windows-x86_64 module` signifie que le paquet n'a pas de
  module Windows ; `dual-screen module does not support this game build` signifie que le
  `build_ids` du paquet ne correspond pas à la version du jeu (le log donne l'identifiant attendu
  juste avant, sur la ligne `looking for '<BUILD16>.json'`).
- **`cannot load dual-screen module for this device (Windows error 193)`** : l'image n'est pas une
  DLL valide pour cette architecture (par exemple un module compilé pour ARM64 ou un `.so` Linux
  renommé). Le message `Windows error 126` indique une dépendance introuvable (runtime VC++ du
  module, par exemple).
- **`missing dual-screen module entry point`** : la DLL a été liée sans `/EXPORT:` — le patch
  `companions-windows.patch` n'a pas été appliqué. Vérifier avec `dumpbin /exports`.
- **`cl.exe` refuse `__attribute__((visibility("default")))`** : `EDEN_SOURCE_ROOT` pointe vers un
  checkout Eden Duo qui n'a pas l'en-tête SDK portable de ce fork.
