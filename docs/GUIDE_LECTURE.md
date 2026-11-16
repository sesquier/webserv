# Guide de lecture de webserv

Ce guide présente le programme comme on le présenterait à quelqu'un qui le découvre :
- ce qu'il fait ;
- comment il est découpé ;
- dans quel ordre il a été construit, et donc dans quel ordre le lire ;
- quelles techniques il emploie.

Chaque fichier de code de la branche `review` contient des commentaires en français qui expliquent le **pourquoi** de chaque morceau.

---

## 1. Le programme en 2 minutes

webserv est un serveur HTTP/1.1 écrit en C++98. Il lit un fichier de configuration inspiré de NGINX, puis sert un ou plusieurs sites :
- des fichiers statiques, avec `index` et `autoindex` ;
- des uploads (POST) et des suppressions (DELETE) ;
- des scripts CGI en Python, Perl et Bash ;
- des pages d'erreur personnalisées et des redirections.

**L'idée centrale : un seul processus, une seule boucle `epoll`, et rien ne bloque jamais.**
- Tous les fds sont non bloquants : sockets d'écoute, clients, pipes des scripts.
- Ils sont tous surveillés par le même `epoll_wait`.
- À chaque tour, la boucle fait **une** petite action par fd prêt : un `accept`, un `recv`, un `send`, un `read` ou un `write`.
- Un client lent ou un script en boucle infinie ne ralentit donc personne.

```
                    ┌────────────────────────────────────────────┐
  fichier .conf ──► │ Config : Tokenizer → ConfigParser          │
                    │          → vector<ServerConfig>            │
                    └───────────────────┬────────────────────────┘
                                        │ (lecture seule)
                    ┌───────────────────▼────────────────────────┐
  navigateur  ◄───► │ Server::run  ─ boucle epoll_wait ─         │
  curl, siege       │   handleEvent  : accept / recv / send      │ server.cpp
                    │   Request      : parseHeaders / parseBody  │ request.cpp
                    │   handleRequest: GET / POST / DELETE       │ handlers.cpp
                    │   CGI          : fork + pipes dans epoll   │ cgi.cpp, cgiEvents.cpp
                    └───────────────────┬────────────────────────┘
                                        │ pipes stdin / stdout
                                 ┌──────▼──────┐
                                 │ script .py  │
                                 └─────────────┘
```

---

## 2. Carte des fichiers

| Fichier | Lignes | Rôle |
|---|---|---|
| `srcs/main.cpp` | ~50 | Arguments, `SIGPIPE` ignoré, config, lancement de la boucle, erreurs fatales |
| `includes/config/*.hpp`, `srcs/config/*.cpp` | ~900 | **Module de config** : lecture, découpage, grammaire, validation, valeurs par défaut, héritage |
| `includes/server.hpp` | ~110 | La classe `Server` : l'état des clients (maps par fd), la liste des méthodes |
| `srcs/server.cpp` | ~500 | **La boucle epoll** : sockets d'écoute, `accept`, `recv`, `send`, timeouts, fermeture |
| `includes/request.hpp`, `srcs/request.cpp` | ~450 | **Lecture d'une requête** : ligne de requête, headers, body (`Content-Length` ou chunked) |
| `srcs/handlers.cpp` | ~560 | **Quoi répondre** : routage, GET, POST, DELETE, autoindex, pages d'erreur, construction de la réponse |
| `includes/cgi.hpp`, `srcs/cgi.cpp` | ~400 | **Briques CGI** : environnement, `fork`/`pipe`/`dup2`/`chdir`/`execve`, lecture de la sortie du script |
| `srcs/cgiEvents.cpp` | ~220 | **CGI dans la boucle** : pipes surveillés par epoll, timeout 504, récupération des processus |
| `config/default.conf`, `config/demo/` | — | La config par défaut et les configs de démonstration pour la correction |
| `www/` | — | Les sites servis, dont les scripts de `www/site1/cgi-bin/` |
| `tests/` | — | Les tests automatiques ; `tests/run_all.sh` les lance tous |

---

## 3. L'ordre de construction (et donc de lecture)

Le projet s'est construit en 4 phases. Les lire dans cet ordre, c'est suivre le raisonnement qui a mené au code.

### Phase 1 : deux modules indépendants, en parallèle (fin octobre)
- **Ayrton : le serveur réseau.** Sockets, epoll, états `READING`/`WRITING`, première réponse, erreurs 400/413/431/501, timeouts, puis les premiers POST et DELETE.
- **Simon : le module de config.** Tokenizer, puis le parser, puis les directives une par une (`root`, `index`, `autoindex`, `methods`…), et enfin `findLocation` et `resolvePath`.

Les deux modules ne communiquent que par une interface : `loadConfig()` renvoie des `ServerConfig`. C'est ce qui a permis de travailler en parallèle.

### Phase 2 : le branchement (début novembre)
- La fusion des deux branches : le serveur utilise la config.
- La réponse sort de la boucle : `handlers.cpp` est créé.
- Les dossiers (`/` final, `index`, `autoindex`, 403), le décodage des URL, les pages d'erreur HTML.

### Phase 3 : les CGI (10-12 novembre)
Un petit pas à la fois :
1. détecter un script par son extension ;
2. construire son environnement ;
3. lire sa sortie ;
4. le lancer (`fork`/`pipe`/`execve`) ;
5. le brancher sur la boucle epoll ;
6. lui transmettre les headers de la requête ;
7. le bonus Perl et Bash.

### Phase 4 : la solidité
Les tests automatiques, `siege`, valgrind, les configs de démo et le README.

### Ordre de lecture conseillé

| # | Lire | Ce qu'il faut en retenir |
|---|---|---|
| 1 | `main.cpp` | Les 3 étapes du programme. Pourquoi `SIGPIPE` est ignoré. Pourquoi les erreurs sont des exceptions. |
| 2 | `config/Token.hpp`, `Tokenizer.cpp` | Le texte devient une liste de mots, `{`, `}`, `;`, avec leur numéro de ligne. |
| 3 | `config/ConfigParser.hpp` puis `.cpp` | La grammaire en fonctions récursives, la table de fonctions par directive, les validations, puis `finalizeServer` (valeurs par défaut et héritage). |
| 4 | `config/LocationConfig.*`, `ServerConfig.*` | Les structures produites ; `findLocation` (le préfixe le plus long) ; `resolvePath` (`root`, `..` refusé). |
| 5 | `server.hpp` | L'état d'un client : ses maps et sa machine à états. |
| 6 | `server.cpp` : `addListener`, `run` | Les sockets d'écoute et **la** boucle epoll : les timeouts et le traitement des événements. |
| 7 | `server.cpp` : `handleEvent` | L'aiguillage : accept → recv → parsing → réponse → send. **Le passage le plus important pour la correction.** |
| 8 | `request.cpp` | `parseHeaders`, `parseBody`, le format chunked. Pourquoi deux temps. |
| 9 | `handlers.cpp` | `handleRequest` (l'ordre des vérifications), GET, POST, DELETE, `buildError`, `buildResponse`. |
| 10 | `cgi.hpp`, `cgi.cpp` | Le contrat CGI, `buildCgiEnv`, `startCgi`/`runScript`, `parseCgiOutput`. |
| 11 | `cgiEvents.cpp` | Le script vu depuis la boucle : pipes dans epoll, `waitpid(WNOHANG)`, 502 et 504. |

---

## 4. Suivre deux requêtes de bout en bout

### `GET /files/notes.txt`
1. `run` : `epoll_wait` signale le socket d'écoute 8080 → `handleEvent` → `accept`, `O_NONBLOCK`, `epoll_ctl(ADD, EPOLLIN | EPOLLRDHUP)`, état `READING`.
2. Tour suivant : le client est prêt en lecture → **un** `recv` → les octets sont ajoutés à `_clients[fd]`.
3. `parseHeaders` trouve la ligne vide → `COMPLETE`. La location choisie par `findLocation("/files/notes.txt")` est `/files`.
4. `parseBody` : pas de body → `COMPLETE`.
5. `handleRequest` : GET est connu, pas de redirection, GET est autorisé, pas de CGI → `handleGet`.
6. `resolvePath` : la location a son propre `root`, qui remplace le préfixe → `./www/site1/files/notes.txt`.
7. `serveFile` : `stat` (fichier normal) → `access` → lecture → `buildResponse(200, …, "text/plain")`.
8. `queueResponse` : la réponse est rangée, état `WRITING`, `epoll_ctl(MOD, EPOLLOUT)`.
9. Tour suivant : le client est prêt en écriture → **un** `send`. Tout est parti → `closeClient`.

### `POST /cgi-bin/hello.py` avec `name=Alice`
1. Étapes 1 à 4 comme ci-dessus. `parseBody` attend que les 10 octets annoncés par `Content-Length` soient arrivés.
2. `handleRequest` : `.py` a un interpréteur dans cette location → `handleCgi` : `stat`, `access`, `buildCgiEnv`, `cgi.pending = true`.
3. `startCgiJob` → `startCgi` : 2 `pipe`, `fork`. Dans l'enfant : `dup2`, fermeture des fds 3 à 1023, `chdir`, `execve("/usr/bin/python3", ["python3", "hello.py"], env)`.
4. Le client passe en `CGI_RUNNING` (seul `EPOLLRDHUP` est surveillé). Le pipe de sortie est surveillé en `EPOLLIN`, le pipe d'entrée en `EPOLLOUT`.
5. `EPOLLOUT` sur le pipe d'entrée → `writeCgiBody` écrit `name=Alice`, puis ferme ce pipe : le script lit EOF.
6. `EPOLLIN` sur le pipe de sortie → `readCgiOutput`, autant de fois que nécessaire. `read` renvoie 0 → la sortie est complète.
7. `reapCgiJob` → `waitpid(WNOHANG)` : le script s'est terminé avec le code 0 → `cgiResponse` → `parseCgiOutput` → `buildResponse(200, …)`.
8. `queueResponse`, puis `send` et fermeture, comme pour le GET.

---

## 5. Les techniques employées

| Technique | Où | Pourquoi |
|---|---|---|
| **Multiplexage avec epoll** (level-triggered) | `server.cpp`, `run` | Un seul processus surveille des centaines de fds sans bloquer. En mode level-triggered, epoll signale un fd tant qu'il reste des données : on peut se contenter d'un `recv` par événement. |
| **I/O non bloquantes** (`O_NONBLOCK`) | sockets, pipes | Un appel ne peut jamais figer la boucle, même si l'état d'un fd a changé depuis l'annonce d'epoll. |
| **Machine à états** par client | `ClientState` | `READING` → `CGI_RUNNING` → `WRITING`. L'état décide de ce qu'epoll surveille et de l'action à faire. |
| **Parsing incrémental** | `request.cpp` | TCP livre la requête par morceaux : chaque analyse peut répondre `INCOMPLETE`, et on recommence au prochain `recv`. |
| **Timeouts sans thread ni signal** | `run`, `checkCgiJobs` | `epoll_wait` se réveille au moins toutes les secondes ; on compare `time()` à la dernière activité. 30 s → 408, 10 s de silence d'un script → 504. |
| **Parser à descente récursive** | `ConfigParser.cpp` | Une fonction par règle de la grammaire : le code a la forme du fichier de config. |
| **Table de pointeurs sur fonctions membres** | `initHandlers` | Une fonction par directive, trouvée par son nom : pas de longue chaîne de `if`. |
| **Héritage de configuration** | `finalizeServer` | Le serveur HTTP ne consulte qu'une `LocationConfig` déjà complète. |
| **Exceptions** | `ConfigError`, `runtime_error` | Une erreur profonde remonte jusqu'au main (config) ou jusqu'à la boucle (un client) sans faire planter le serveur. |
| **Pas de `new`** | partout | `std::string`, `std::vector` et `std::map` gèrent la mémoire : rien à libérer à la main, pas de fuite. Les fds et les processus sont libérés par `closeClient` et `abortCgiJob`. |
| **`fork` + `pipe` + `dup2` + `execve`** | `cgi.cpp` | Lancer un autre programme sans remplacer webserv, et brancher son stdin et son stdout sur des pipes. |
| **`waitpid(WNOHANG)`** | `cgiEvents.cpp` | Récupérer un script terminé sans attendre ceux qui tournent : pas de zombie, pas de blocage. |
| **Défense en profondeur** | `handlers.cpp`, `LocationConfig.cpp` | Décodage des URL **avant** de vérifier les `..`, noms d'upload filtrés, `O_EXCL`, `stat` avant `open` (contre les FIFO), échappement HTML, `HTTP_PROXY` ignoré. |

---

## 6. Les règles du sujet, et où elles sont respectées

| Règle | Où le montrer |
|---|---|
| Un seul `poll` (ou équivalent) pour toutes les I/O, en lecture et en écriture en même temps | `server.cpp`, `run` : un seul `epoll_wait` ; `enableWriteEvent` passe un client en `EPOLLOUT` |
| Aucune lecture ni écriture sur un socket ou un pipe sans passer par epoll | `handleEvent` → `recv` et `handleClientWrite` → `send` ; `handleCgiEvent` → `read` et `write` |
| Pas d'`errno` après une lecture ou une écriture | Le seul `errno` suit `epoll_wait` (`EINTR`). Pour un upload, on fait un `stat` au lieu de tester `EEXIST`. |
| `fork` uniquement pour le CGI | `cgi.cpp`, `startCgi` : c'est le seul `fork` |
| Le CGI s'exécute dans le bon dossier | `runScript` : `chdir` |
| Le serveur ne bloque jamais, et un client peut être déconnecté proprement | Timeouts dans `run`, `closeClient`, `EPOLLRDHUP` |
| Pages d'erreur par défaut | `defaultErrorPage` + `buildError` |

---

## 7. Comment vérifier soi-même

```sh
make && tests/run_all.sh               # toutes les suites de tests
tests/run_all.sh --valgrind            # les mêmes, sous valgrind
./webserv config/demo/routes.conf      # une config de démonstration
```

**Pour réviser :** prenez une ligne du tableau de la section 3. Fermez le fichier, et racontez à l'autre ce qu'il fait. Puis rouvrez-le et comparez avec les commentaires.
