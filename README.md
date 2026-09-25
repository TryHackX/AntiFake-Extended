# AntiFake Extended System Advanced 2.0

Who is who on a **Soldat Dedicated Server 2.8.2 (Soldat 1.7.1)**: accounts, names, computers,
addresses, where people play from, what Steam says about them, and which accounts belong to the
same person.

Author: **Dominik (TryHackX)** - MIT License. Runs next to [ZitroStats](../ZitroStats-Core/README.md)
and does not get in its way.

---

## Contents

1. [How it works](#how-it-works)
2. [Installation](#installation)
3. [Commands](#commands)
4. [What a player may ask](#what-a-player-may-ask)
5. [Visits and where people play from](#visits-and-where-people-play-from)
6. [Steam](#steam)
7. [Accounts that look like one person](#accounts-that-look-like-one-person)
8. [The database](#the-database)
9. [MySQL](#mysql)
10. [Colours](#colours)
11. [Every setting](#every-setting)
12. [Building from source](#building-from-source)
13. [Troubleshooting](#troubleshooting)

---

## How it works

```
Soldat server
  |
  |  events: join, leave, tick, chat, admin command
  v
antifake.pas (ScriptCore 3, about 1100 lines)
  |  reads the players, passes commands on, prints lines
  v
antifake_dll.dll / .so (native, threads of its own)
     accounts, indexes, searching, visits, geo, Steam, multi-accounts,
     storage, journal, MySQL, and every table that is drawn
```

While the server runs, the game thread is never held for longer than it takes to copy a few
strings, and **nothing in the script ever waits**. A command goes through this path:

1. The player types it. The script remembers it and returns at once.
2. On the next tick the script hands it to the library. The library checks - in string compares
   and numbers - whether it is one of ours, whether that player may ask it and whether it is too
   soon after the last one, then starts a worker and returns.
3. The worker parses the arguments, looks accounts up, reads the directory, builds the table -
   whatever the command needs - and puts the finished lines in a buffer.
4. Every tick the script asks whether there are lines, and sends `LinesPerTick` (4) of them to
   the player. When there are none left and the worker is done, it stops asking; a worker that
   takes longer than `TaskTimeoutSeconds` (30) is given up.

No step blocks: there is no `Sleep`, no `WaitFor` and no busy loop anywhere in the script, and
everything that takes time (searching, locating an address, asking Steam, writing the file,
writing to MySQL) has a thread of its own and answers later. Only when the script stops (a
`/recompile` or the end of the server) does the library wait for its own threads - a search, a
lookup, a question to Steam, the last exchange with MySQL or a full write that is running -
each for a limited time.

---

## Installation

1. Copy the `AntiFake-Extended` folder into `scripts/`. **The folder name must not contain a
   space**: ScriptCore refuses to load a library from a path with one.
2. Add the script in `scripts/scripts.ini` the way you add any other.
3. On Linux, build the library once: `sh source_dll/build.sh` (see [Building from source](#building-from-source)).
4. Start the server. `nickreg/antifake.dat` appears by itself, and the folders `nickreg`,
   `nickreg/backup` and `logs/antifake` are made when they are missing. A database of an older
   version in the server folder (`antifake.dat`) moves into `nickreg` at the first start - see
   [The database](#the-database).

When you update, `antifake.pas` and the library come from the same release: replace both
together, and delete `antifake.psb` if there is one - that is the compiled script of the old
version, and the server may run it instead of compiling the new `antifake.pas`.

The settings are in `settings.ini` next to the script. Every value has a built-in default, so a
missing file or a missing line changes nothing. `Example-configs/` holds complete `settings.ini`
files for typical servers - a public server, a private or clan server where players may see more,
a privacy-strict server (players see only themselves, no geo lookups) and several servers on one
MySQL database - with a `README.txt` saying what each one changes; copy the one that fits over
`settings.ini` (keep a backup) and fill in what it asks for. The file is read when the script starts (with
the server, or at a `/recompile`). A `;` after a space starts a comment, also behind a value; a
password that contains ` ;` itself goes in double quotes (`MysqlPassword` and `SteamApiKey`
lose them; any other value keeps quotes as they are). `SteamApiKey` belongs only in the
server's own `settings.ini`; never publish it, or the file with it still in there.

At start, the console gets a line like `version 2.0, settings read (N out of range, N
unknown)`: how many settings.ini lines were outside their range and kept their default, and
how many were not recognised.

---

## Commands

`/antifake` prints all of them, and shows a player only what that server lets them use. The
commands ZitroStats has as well carry the prefix `af_`: `af_search`, `af_status`, `af_help` and
the pages of a list, `af_next`, `af_prev` and `af_page n`; the same names without the underscore
work too (`/afsearch`, `!afnext`).

Admins type `/`; everybody else types `!` in the chat.

**Naming an account.** `<name>` is a nick or a part of it, `#id` an account number. One letter
is enough, and the closest name wins: the whole name, then a name that begins with the text, then
one with a word that begins with it, then any other; between two of the same kind a player on
the server first, then the shorter name (`u` finds `up` before `hardkorowy koksu`), then the one
that played last. `/af_search` lists its results the same way, without looking at who is on
the server.

**Names, `#numbers` and quotes.** `#12` means account 12. When a player is also named exactly
`#12`, the account still answers, with a note of how to write the name in quotes to reach the
player instead; when there is no account 12, the player of that name is taken, with the same
kind of note. A name in double quotes is always a name - never an account number, a row, a slot
or a word a command reads on its own - for every command that takes a player (`af_status`,
`statusa`, `visits`, `uniquevisits`, `locations`, `lastvisits`, `lastvisitors`, `privacy`,
`privacyall`, `checkp`, `showlocate`, `steam`, `multi`, `locate`) and for the value in `checkd`,
`af_search`, `searchall`, `smartsearch`, `visitsof` and `storage restore`. Several words make one
name with no quotes needed (`/visits zz two` asks about "zz two"), except where a trailing word
would otherwise be read as something else: a sort word right after a name in `/privacy` and
`/privacyall` (`/privacy n Big Time` reads "Time" as the sort and asks only about "Big" - write
`/privacy n "Big Time"`), and the whole argument of `/multi` (`stats`, `top` and `strong` are
read as a name unless they stand alone). `/visitsof` reads a country of several words the same
way and looks its code up among the places already seen (`/visitsof United Kingdom` is
`/visitsof GB` once the server has seen a visit from there); a name it does not know is answered
with "no place located so far has that name". The help repeats the rule: `a name in double
quotes is always a name: "#1" is the player named #1, #1 the account`.

Left without a name, a command that names an account (`af_status`, `steam` and the like) answers
about your own account; the console has none of its own and has to name a player.

Every command that names an account also comes in three more forms, as in ZitroStats:

| Form | The account |
| :-- | :-- |
| `<command>id <slot>` | of the player in that server slot (`/online` shows the slots) |
| `<command>p <row>` | in that row of the last list you got - the first column of a list numbers its rows |
| `<command>i <position>` | of the player at that position of the ZitroStats ranking (`!top`), found here by the SteamID or else by the whole name; needs ZitroStats on the same server |

For example `/visitsid 3`, `/visitsp 2` after an `/af_search`, `/visitsi 1`, `/uniquevisitsi 5`,
`/locationsp 4`, `/statusap 2`, `/privacyid n 5`, `/multip 1`. The words after the name stay
the same (`/visitsi 1 30d masked`). An empty slot, a row the list does not have, a position
nobody holds or a server without ZitroStats is said so.

### Who is who

| Command | What it does |
| :-- | :-- |
| `/af_search <name>` | accounts whose current name contains this |
| `/searchall <name>` | the same, through every name ever used |
| `/smartsearch [lev\|jaro\|token] [min %] [all] <name>` | names that are nearly this one |
| `/statusa <name\|#id>` | one account: when, how long, from where, what it is tied to |
| `/privacy <n\|s\|h\|i> <name\|#id>` | what the account uses now |
| `/privacyall <n\|s\|h\|i> <name\|#id>` | the whole history |
| `/checkp <n\|s\|h\|i> <name\|#id>` | this account against all the others |
| `/checkd <n\|s\|h\|i> <value>` | which accounts have this name, computer or address |
| `/listprofiles [sort] [asc\|desc]` | every account, in the order below |
| `/online`, `/showid` | the players on the server and their accounts |
| `/steam [name\|#id]` | what Steam says |

`sort` is one of `id`/`number`/`num`/`nr`, `name`/`nick`/`nickname`, `time`/`played`/`playtime`/
`playedtime`, `logins`/`login`/`joins`/`join`/`count`, `last`/`seen`/`lastseen`/`latest`,
`first`/`registered`/`new`/`firstseen`, `names`, `ips`/`ip`/`addresses`/`address`,
`computers`/`computer`/`hwids`/`hwid`/`pcs`/`pc`, or `steam`/`steams`/`steamids`. Said alone, a
sort word puts the most, the newest or the end of the alphabet first; nothing said sorts by
account number, lowest first. `asc`/`ascending` or `desc`/`descending`, in either order, or the
old `+`/`-` behind the word (`count+`), says which way instead. The list opens with a line
saying how it is sorted (`All accounts, by logins, highest first...`), and sorting by logins,
last seen or first seen adds that as a last column - every other sort keeps the account's time
played there. A word that is not one of these, or two sort words at once, is answered with what
is wrong (`Not a sort: <word>.` or `One sort at a time, not <words>.`), the usage line and the
full list of words.

### Visits and places

| Command | What it does |
| :-- | :-- |
| `/visits [name] [7d\|30d\|all] [masked\|clean]` | when, how long and from where |
| `/uniquevisits [name] [7d\|30d\|all] [masked\|clean]` | one row per address |
| `/locations [name] [7d\|30d\|all]` | one row per country |
| `/visitsp`, `/visitsid`, `/visitsi` and the same for `uniquevisits` and `locations` | the same about the account in a row of your last list, in a slot, or at a position of the ZitroStats ranking (see above) |
| `/visitstats [range]` | the countries of the whole server, with shares |
| `/visitsof <country> [range]` | who comes from there |
| `/lastvisits [range]` | the newest visits of everybody: when, how long, the account, the name used, the address and the place; at most `LastVisitsRows` (500) |
| `/lastvisitors [range]` | who came to the server, one row per account, the last visit first: how often, how long in all, the address of the last visit and its place |
| `/locate [name]`, `/locateall` | where the players on the server are: an admin sees the address, the country and its code, the region, the city, the postcode, whether it is a proxy, a server room or a mobile line, the provider and the host name |
| `/showlocate <n\|s\|h\|i> <name\|#id>` | the places of an account and of every account with the same name, Steam id, computer or address |
| `/geosearch <place> [key:value]` | accounts that played from there: two letters are the code of a country or a continent (`pl`, `eu`), a longer word a part of the name of a country, a region or a city. The provider, the host name, the postcode and the time zone need their key: `isp:`, `dns:`, `zip:`, `timezone:`; `proxy:1`, `hosting:1`, `mobile:1` narrow it down |

`/lastvisitors` typed with `/` is the table above: one row per account, its last visit first,
with the address and the place. Typed with `!` - by a player, or by an admin who prefers the
players' form - it is the simple table instead: who came, when, how often and for how long, and
on which account, with no address and no place; `masked` and `clean` are ignored there for a
player too - whether a visit came through a VPN is a kind of place, the same as the address.

### Accounts that look like one person

| Command | What it does |
| :-- | :-- |
| `/multi` | every account tied to another one, closest first |
| `/multi <name\|#id>` | one account and everything it is tied to |
| `/multi strong` | only what is worth telling an admin about |
| `/multi top` | the computers the most accounts have played from |
| `/multi topaddresses` | the same for addresses |
| `/multi stats` | how many pairs an admin has judged |
| `/multiset <id> <id> same\|family\|ignore\|clear` | write down what a pair is |
| `/steamstatus` | how the asking of Steam is going |

### The database

| Command | What it does |
| :-- | :-- |
| `/storage` | the file, the journal, MySQL and the state |
| `/storage save` | write the whole database now; you hear when it is written |
| `/storage backups` | the copies that are kept |
| `/storage restore <name>` | put one back (the old file is kept, never deleted); it is read at the next start |
| `/storage mirror` | `Engine = mirror`: write the copy into MySQL now; you hear when it is done |

---

## What a player may ask

`[PlayerCommands]` has one line per group, and each takes one word:

- `off` - admins only,
- `self` - a player may ask about themselves and nobody else,
- `all` - a player may ask about anybody.

```ini
PlayerVisits = self          ; !visits, !uniquevisits, !locations; !lastvisits (all)
PlayerVisitStats = all       ; !visitstats - countries and counts, no addresses, no names
PlayerVisitsOf = off         ; !visitsof <country>
PlayerStatus = self          ; !mystatus and !af_status (self) and !statusa <player> (all)
PlayerNames = self           ; !mynames (self), !privacy and !privacyall (all)
PlayerSteam = self           ; !steam
PlayerLocate = self          ; !locate (self), !locate <player> / !locateall (all)
PlayerProfiles = off         ; !online, !listprofiles, !showid
PlayerSearch = off           ; !af_search, !searchall, !smartsearch, !checkd, !checkp, !geosearch,
                              ; !showlocate
PlayerMulti = off            ; !multi
PlayerHelp = all             ; !antifake and the pages of a list
PlayerLastVisitors = all     ; !lastvisitors - who came, when and for how long, no addresses
```

With `self`, a player who names somebody else with `!visits`, `!uniquevisits` or `!locations`
(or uses a slot, a row of a list or a ranking position with any command) is told that on this
server they may ask only about themselves; a range or a filter is still fine (`!visits 7d`).
The other `self` commands (`!steam`, `!af_status`, `!locate`) leave a name out and answer
about the player who asks. With `all` the player asks about anybody the way an admin does.
`PublicCommands = 0` turns every `!` command off at once, whatever these say. Admins always
have everything through `/`.

`!lastvisitors` has its own reach, `PlayerLastVisitors`: the list itself is about other players,
so `off` and `self` both mean admins only, and only `all` opens it to players - who then see it
without addresses or places, the same table an admin gets by typing `!lastvisitors` instead of
`/lastvisitors`. Naming a player inside `!lastvisitors` still takes the reach of `PlayerVisits`.

**Addresses stay with the admins.** Whatever a player may ask, an address is shown whole only to
an admin and to the player it belongs to; everybody else sees its first two numbers
(`83.22.*.*`). A Steam account or a computer is shown whole the same way; everybody else sees
its first 3 characters and `***`. `!locate` shows a player the country, the city and the
provider; an admin sees the address and everything known about the place.

Flood protection is the same idea as the heavy lists of ZitroStats: one command per player per
`CommandCooldownSeconds` (3), one list per `HeavyCooldownSeconds` (10), and at most
`MaxListsPerSecond` (3) lists a second on the whole server. `!mystatus` and `!statusa` count as
lists, because they read every account. Admins are not held back.

---

## Visits and where people play from

**Which account a player is.** At every join, and at every change of nick, the account is
found by the Steam account, or else by a computer it played from - for a player with Steam only
an account without a Steam account of its own, since one with its own Steam account belongs to
somebody else. The name and the address never decide. Nothing known: a new account. When
Steam answers after the join and the answer points to another account, the player is moved
there and what the join wrote into the first one is taken out again (a "correction" in the
console). Each account keeps its names, Steam accounts, computers and addresses, each with its
logins, play time and first and last use, and its visits.

Every join is written into the account: when it started, how long it lasted, the name, the
computer, the address, the country, and how sure that location is. Joining and leaving in a
loop does not make new rows: a visit from the same address is extended for
`VisitCooldownMinutes` (10). An account keeps `VisitKeepCount` (50) visits, not older than
`VisitKeepDays` (365).

**How sure the location is**, 0-100, starts at 100 and loses:

| Sign | Cost |
| :-- | :-- |
| proxy or VPN | -60 |
| server room (hosting) | -50 |
| mobile network | -15 |
| no host name known for the address | -5 |
| nothing known about the address | 0, and the visit is marked `no-geo` |

The addresses are located through [ip-api.com](https://ip-api.com) (over plain HTTP), once per
address, on a thread of its own. `Enabled = 0` under `[Geo]` stops anything from leaving the
server. No more than 40 lookups start in any one minute, whatever asks for them (`MaxLookups`
only limits how many run at once; `1` makes them strictly one at a time); a lookup that gets no
answer pauses the next ones for a minute, then two, four and so on, up to 30 minutes, and when
ip-api.com says its own limit is reached, they wait as long as it asks. An address that could
not be located is asked about again after `RetrySeconds` (600), a location older than
`RefreshDays` (90) is looked up again at the next join from it, and one that has not been
looked up for `GeoKeepDays` (365) is forgotten by the daily cleanup. Local and private
addresses (and IPv6 ones) are never looked up.

Once a join's address is located, the server tells everybody "`<name>` joined from `<place>`"
(`ColorGood`); at most one such line is sent a tick, so a wave of joins is announced one at a
time over the next ticks instead of all at once. Nothing is said for a local address, for one
that is not located within `LocateTimeoutSeconds` (30), or for the players who were on the
server already when the database had been read.

---

## Steam

The game server knows only the number. The name on Steam, the country, the age of the account
and the bans are public and come from the Steam Web API.

```ini
[Steam]
SteamApiKey =                 ; empty = nothing is asked of Steam at all
SteamRefreshDays = 7          ; an account already known is asked about again after this long
SteamBatchSize = 25           ; how many in one request (Steam allows 100)
SteamCurlPath = curl          ; the request goes through curl, which Windows 10 and Linux have
SteamTimeoutSeconds = 10
```

A free key for your own server: https://steamcommunity.com/dev/apikey

Soldat shows an account as `S1885850061` (the 32-bit number); the Steam Web API wants the
64-bit form. The library converts both ways. Without a key `/steam` answers that the Steam part
is off. The key is letters and digits only; anything else counts as a value out of range and
leaves Steam off. Questions go out in batches of `SteamBatchSize`, at most one batch a second,
and the key goes to curl on its input, never on its command line, so it never shows up in a
process list or a crash report.

`/steamstatus` says what went wrong when something does - no curl, no answer in time, Steam
refusing the key (HTTP 401 or 403), or an answer that is not data. Steam refusing the key
stops the questions with one line in the console; nothing more is asked until `SteamApiKey`
changes. Any other failure pauses the next batch for a minute, then two, four and so on, up to
an hour. At most 500 accounts wait for their turn; one already asked about within
`SteamRefreshDays` is not asked again.

---

## Accounts that look like one person

Two accounts are tied together when they were played from the same computer, the same Steam
account, the same address or the same /24 network. Each of those is a signal, and the score is
the signals added up:

| Signal | Points |
| :-- | :-- |
| the same computer | 60 |
| the same Steam account | 60 |
| the same address | 25 |
| the same network (only when the address differs) | 10 |
| almost the same name | 30 |
| a similar name | 15 |
| a name a little like it | 6 |

The admins hear about a pair when it reaches `MultiAccountReportScore` (55) points on at least
`MultiAccountMinSignals` (1) signals: said once when such a player joins, in the console and to
every admin in the game (built on a worker, so the join itself costs nothing), and in the log.
With those numbers **every shared computer is reported**, and a shared address on its own (25)
is not - a whole block behind one address is not worth a word. A pair has at most 4 signals
(computer, Steam, address or network, name); `MultiAccountMinSignals` 5 or 6 counts as 4.
`MultiAccountByAddress = 0` leaves addresses and networks out, so only a shared computer or
Steam account ties two accounts.

`/multi` always shows everything, whatever the threshold is: one row per account tied to at
least `MultiAccountMinLinks` (1) others - how many it is tied to, the score and the closest of
them, and what an admin has said about that pair - with a second line underneath naming what
ties it to all of them and why the closest one looks like the same person.

An address (or a /24 network) that more than `MultiAccountMaxShared` (20) accounts have played
from is a public one - a school, a cafe, a block - and is skipped. **A computer is not treated
that way**: twenty accounts on one machine is not a cafe, it is twenty accounts on one machine,
and `/multi top` is there to find exactly that; only more than `MultiAccountMaxSharedComputer`
(200) accounts on one computer or one Steam account are skipped, to bound the work. At most
`MultiAccountMaxLinks` (25) tied accounts are collected per account.

An admin writes down what a pair really is with `/multiset <id> <id> same|family|ignore|clear`.
A pair that has been judged is never reported again.

---

## The database

`nickreg/antifake.dat` (`DatabaseFile`) holds the accounts, the geo cache, the visits, the
Steam data and the admin decisions, next to the data of ZitroStats. On top of it:

- **While it loads.** Every command except `/antifake` (or `!antifake`) answers "The database
  is still being read; try again in a moment." until the database has been read.
- **A `/recompile` while people are playing** tells the admins in the game, the console and the
  log once the database has been read again: `AntiFake Extended 2.0: script compiled, database
  N accounts loaded in X ms` (or `..., but the database could not be read`).
- **A journal.** Every `JournalSeconds` (10) whatever changed - logins, play time, visits, what
  Steam said - is appended to `nickreg/antifake.jrn`, so a server that is killed loses seconds instead
  of the quarter of an hour between two full writes. The journal carries the size and the time
  (UTC) of the file it belongs to, so a journal from another copy of the database is ignored,
  and a change to or from summer time never throws a good one away. A journal that has grown
  past 64 MB takes nothing more until the next full write. A new database (no file yet) is
  written as soon as the script starts, so the journal works from the first minute.
- **A full write** every `SaveIntervalMinutes` (15) while something changed (players join, play
  or leave), and on `/storage save` (or `/storage now`): to a temporary file, then swapped in;
  the old file is kept as `.bak`. After that the journal starts again empty, with whatever
  changed while the write ran. Nothing is written before the database has been read
  completely, so a server stopped during the reading never writes an empty database over the
  file.
- **When the script stops** (a `/recompile` or the end of the server) only the journal is
  written, and the next start puts it on top of the file, so nobody waits for a full write.
  That start writes the file at once, on a thread of its own, and the journal is started again
  only after that write: until then it is the only copy of those changes on disk.
  Without the journal (`JournalSeconds = 0`), or with a journal that is full, the whole
  database is written then.
- **The daily cleanup** (`[Retention]`) runs a minute after the start and then once a day.
  It and the full write run on threads of their own and take the accounts one at a time: a
  player who joins meanwhile waits for one account, not for all of them. `ProfileDays` empties
  an account nobody has played on for that long: its number is kept, but its names, computers,
  addresses, visits, Steam data and admin decisions go, the number is reused by the next new
  player, and the cleanup report counts them. Skipped with `Engine = mysql`, where the other
  servers may still be using the account.
- **Copies with rotation.** One copy of the finished file every `BackupHours` (24) into
  `nickreg/backup` (as `antifake-<date>.dat`, beside the copies of ZitroStats), keeping the
  newest `BackupKeep` (7). A database kept in the server folder itself keeps its copies in
  `antifake-backups`.
- **A file that cannot be read is never overwritten.** The server says so and writes nothing
  until an admin looks at it. `/storage restore <name>` puts a copy back and keeps the file that
  was there as `.replaced-<date>`, and its journal as `.replaced-<date>.jrn` - **nothing is
  ever deleted**. The copy is read at the next start: restart the server. Until then nothing
  is written over it, so what happens on the server meanwhile is not kept. With
  `Engine = mysql` the file is only the cache and the next start reads the shared database.
  A restore is refused while a full write is running; try again in a moment.
- **From the server folder to nickreg.** Older versions kept `antifake.dat` in the server
  folder. When `DatabaseFile` names a folder that has no database yet, the first start moves the
  old file there, with its journal, its `.bak`, its copies (and any `.replaced-<date>` files,
  into `nickreg/backup`) and the trace log (to `logs/antifake/trace.log`) - moved, not
  copied, so the journal still fits its file, and nothing is deleted. The console and the log
  say what moved. If `nickreg` already has a database, the old file is left alone and the console
  says it is not used.

**The log.** `logs/antifake/antifake.log` keeps what AntiFake says in the console - the database
read and written, new players and names, corrections, accounts tied to others, script errors -
and every command an admin gave it, with the admin's name, one dated line each. The lines are
written by a thread of their own, never by the game thread.

---

## MySQL

```ini
[MySQL]
Engine = local                ; local | mirror | mysql
MysqlHost =
MysqlPort = 3306
MysqlDatabase = soldat
MysqlUser =
MysqlPassword =               ; with a space at the start or the end: in double quotes
MysqlServerId = 1             ; every server of one database needs its own number
MysqlMinutes = 60             ; mirror: how often the copy is written
SyncSeconds = 3               ; mysql: how often the changes are exchanged
MysqlCodepage = 1250          ; the code page of the player names: 1250, 1252 or 65001
SharedSearch = 0              ; mirror: 1 = the searches look into the other servers' copies too
SharedLinks = 0               ; mirror: 1 = so does what ties accounts together
```

**The best choices are `Engine = mysql` - several servers sharing one live database - or
`Engine = local` - one server, the file alone.** `mirror` only makes a copy for a website to
read, or lets servers that keep their own files search each other; it is not a shared database
of its own.

- `local` - the file alone.
- `mirror` - the file is the database, and every `MysqlMinutes` (and on `/storage mirror`) a whole
  copy goes into `af_profiles`, `af_other`, `af_visits`, `af_steam`, `af_links`, `af_geo` and
  `af_meta`, for a website. The copy is one transaction: a website never sees half of it. The
  tables are made on the first run; a table that is already there gets the columns it lacks.
  At start **the newer of the two is read**: the copy in MySQL when the file is missing or older
  than it (a new machine, a lost disk), otherwise the file. The times are compared in UTC
  (`af_meta.written_utc`), so summer time never decides. A file that cannot be read is put aside
  as `antifake.dat.unreadable-<date>` (never deleted) and the copy is read instead. After reading
  the copy the file is written from it at once, so the two agree again. The console says what
  was read and why. A copy that fails (the database is down, a wrong password) is tried again
  after 1, 2, 4 ... minutes, at most `MysqlMinutes` apart; `/storage` shows the last problem.
- `mysql` - **one database for every server**, below.

### Engine = mysql: one database for every server

All servers of the database work on the same accounts: a player who joins one of them is known
on all of them, with every name, Steam account, computer and address any of them saw, the
visits and places, what Steam says and the decisions of the admins. More servers, more to find
a player by - and, from every start on, the same account number everywhere. An account keeps
its number while a server runs, so one that came in since the start may carry another number
on each server until their next start.

- **Nothing waits for the database.** Every server keeps all of it in memory, as with the file:
  a join, a search and a list read memory. A thread of its own exchanges the changes every
  `SyncSeconds` (3); the game thread never waits for it. After an exchange that failed the next
  one waits longer, up to a minute, and the console says when the database stops answering and
  when it answers again.
- **Nothing is overwritten.** What changed on a server goes in as a three-way merge: the thread
  locks the row, reads what the others wrote, puts this server's changes since the last exchange
  on top and writes the result. Logins and play time of several servers add up, a new name,
  computer or address joins the others, visits are put together, and what the cleanup of one
  server throws away goes on the others too, unless it was used there since. The writers take
  turns on one row of `af_db_meta`, so any number of servers can write at once. Of two decisions
  of the admins about one pair, of two lookups of one address and of two answers of Steam about
  one account, the newer one stays.
- **New players.** A player new on two servers at once becomes one account the way a join finds
  one on a single server: through the Steam account, or - when the database does not know it -
  through the computer of an account without Steam (the database compares the Steam account and
  the computer an account uses now).
- **The first start of a server** brings its file along: an account whose Steam account (or,
  without one, whose computer) the database knows is joined to it, every other account gets a
  number of the database; the first server keeps its numbers. From then on every start reads the
  database.
- **Without the database** a server works from its file and sends what it collected once the
  database answers. Which account here is which in the database, and what has not gone in yet,
  is kept beside the file (`antifake.shared`), so a restart without the database loses nothing.
  A server that has not joined yet works from its file alone until a start when the database
  answers. If `antifake.shared` is missing when the database is reached again - deleted, or
  belonging to another copy of the database - a server that has joined before simply takes the
  database as it is: what changed here without it is not merged in, and is gone.
- **The file** stays a cache: every start with the database writes it again. A file that cannot
  be read is put aside (never deleted) and written again from the database.
- `SharedSearch`, `SharedLinks` and `/storage mirror` belong to `mirror`: with `mysql` every
  account of every server is here already. `/storage` shows the exchanges and the last problem.
- **Tables:** `af_db_profiles` (`id`, `name`, `steam`, `computer`, `address`, `last_played` and
  `time_played` for a website; the whole account in `data`), `af_db_verdicts`, `af_db_geo` and
  `af_db_meta`.
- `source_dll/tests/afshared.pas` runs two servers in one process against a database of your
  own, a restart without the database and a file that cannot be read included.

### Both

**No client library is needed.** AntiFake speaks the MySQL protocol itself, so the 32-bit
server talks to any MySQL or MariaDB - 32- or 64-bit, on the same machine or another one -
without `libmysql.dll` or `libmysqlclient.so`. It was tested against MySQL 8.4 and MariaDB
11.4: every table, every read of the mirror, the shared database, and the login methods servers
use - `caching_sha2_password` (the MySQL 8 default, including the first login after a server
start, when the password is sent encrypted with the server's RSA key) and
`mysql_native_password`; `sha256_password` is spoken as well.

What it does not do: an encrypted (TLS) connection. A user created with `REQUIRE SSL`, or a
server with `require_secure_transport = ON`, refuses the login, and `/storage` says so. MariaDB's
`ed25519` method is not supported either; give that user `mysql_native_password`.

The text in the tables is UTF-8. Player names arrive in the code page of the players' Windows;
`MysqlCodepage` says which (1250 for Central Europe, 1252 for Western Europe, 65001 for names
that arrive as UTF-8; any other number reads them as Latin-1). Values are sent as hex
literals, so no name a player chooses can change a statement.

**Several servers with `Engine = mirror`, more to search.** Servers that share one database
(each with its own `MysqlServerId` and `Engine = mirror`, so its copy is there) can look into
each other's copies - more servers, more names, computers and addresses to find a player by. Both
switches are off by default:

| Switch | What it adds |
| :-- | :-- |
| `SharedSearch = 1` | `/af_search`, `/searchall` and `/checkd` list the matching accounts of the other servers under the results of this one |
| `SharedLinks = 1` | `/statusa`, `/checkp` and `/multi <name\|#id>` list the accounts of the other servers that share a Steam account, a computer or an address with this one (`/checkp n` compares the names) |

An account of another server shows as `S<its MysqlServerId> #<its number there>`, with its name,
when it was last seen and what matched; its addresses stay with the admins, as here. The other
servers are asked on the worker of the search, never by the game, and what they hold is as old
as their last copy (`MysqlMinutes`). Those accounts are only listed, never taken into this
server's database, and the `p` form picks rows of this server only. `/smartsearch`, the visits
and the places stay this server's.

`ON DUPLICATE KEY UPDATE col = VALUES(col)` draws a deprecation warning on MySQL 8.0.20 and
newer; it is kept on purpose, because the alias form that replaces it does not exist in MariaDB
or MySQL 5.7.

---

## Colours

Every colour AntiFake writes is a setting, so the output can match the rest of your server.
They live in `[Colors]` in `settings.ini`, written as `RRGGBB` (a `#`, a `$` or a `0x` in
front is allowed).

| Setting | Default | Where it is used |
| :-- | :-- | :-- |
| `ColorMain` | `F0E68C` | the headings of the tables and the ordinary lines |
| `ColorInfo` | `87CEEB` | the frames and the "looking for..." lines |
| `ColorData` | `C0C0C0` | the rows of a table and the legends |
| `ColorWarning` | `FFD700` | something the admin should notice |
| `ColorError` | `FF6347` | something went wrong, or a very high multi-account score |
| `ColorGood` | `66CC66` | the "joined from ..." line, and the news that a write or a copy is done |

A value that is not a hex number up to `FFFFFF` is counted with the values out of range in the
line at start, and the built-in colour is used, so a typo never leaves the output unreadable.
Write all six digits: `FFF` is read as `000FFF`, a dark blue.

---
## Every setting

`settings.ini` has a comment above every line. The sections are:

| Section | What is in it |
| :-- | :-- |
| `[General]` | the database file and how often it is written |
| `[Storage]` | the journal, the copies and how many are kept |
| `[Commands]` | which events the script listens to, the flood limits, lines per tick |
| `[PlayerCommands]` | what a player may ask with `!` |
| `[Colors]` | the colours of everything AntiFake writes |
| `[Diagnostics]` | the trace (`logs/antifake/trace.log`) |
| `[Geo]` | locating addresses |
| `[Visits]` | the visit log, and how many rows `/lastvisits` and `/lastvisitors` show |
| `[MultiAccounts]` | the score, the thresholds and the limits |
| `[Steam]` | the Steam Web API |
| `[MySQL]` | the engine: the file alone, a copy in MySQL, or one database for every server |
| `[Retention]` | how long the data is kept |

---

## Building from source

The source is in `source_dll`. FPC 3.2.2, 32-bit, because the Soldat server is 32-bit.

```
build.bat            Windows, the normal build
build.bat debug      with range, overflow and object checks
sh build.sh          Linux, the same two
```

On Linux it takes the official `fpc-3.2.2.i386-linux.tar` (it has every package the library
uses; the i386 packages of Debian and Ubuntu do not), `binutils` and the 32-bit C library files:

```
sudo apt-get install binutils libc6-dev-i386 curl
wget https://downloads.sourceforge.net/project/freepascal/Linux/3.2.2/fpc-3.2.2.i386-linux.tar
tar xf fpc-3.2.2.i386-linux.tar && cd fpc-3.2.2.i386-linux && sudo ./install.sh
```

`build.sh` first compiles a small library with the same units. When a package is missing it
names the unit and says how to get it (`Can't find unit DateUtils` means the packages of FPC are
not installed, or not in `fpc.cfg`). `FPCUNITS=/usr/local/lib/fpc/3.2.2/units/i386-linux sh build.sh`
reads the packages from that folder whatever `fpc.cfg` says.

The scripts compile into `source_dll/build/` and copy only the finished library next to the
script. The MySQL part is always in it and needs nothing else on the server. `clean.bat` /
`clean.sh` remove `build/`.

The programs in `source_dll/tests` test the library without a game server: `aftest`,
`afstress`, `afconv`, `aflint` and `afexamples` on their own, `afmysql` (the mirror) and
`afshared` (two servers on one database) against a MySQL or MariaDB of your own. They build into
`build/test`; the top of each file says how to build and run it.

---

## Troubleshooting

**The script will not start: `Invalid External`.** The path to the library contains a space.
The folder has to be `AntiFake-Extended`, not `AntiFake Extended`.

**The server stops answering.** The script prints no trace of its own. `[Diagnostics] Trace = 1`
writes every step of a login and of a correction to `logs/antifake/trace.log`, with times, so
the last line says where it stopped. (`logs/antifake/antifake.log`, the events and the admin
commands, is always written.)

**Changes to `antifake.pas` have no effect.** The server runs `antifake.psb`, the compiled script
of the old version: delete it and restart the server.

**`/steam` says nothing comes back.** `/steamstatus` says why: no curl, no answer in time, Steam
refusing the key, or an answer that is not data.

**The database could not be read.** Nothing is written over it. Put a copy back with
`/storage restore <name>` (the names are in `/storage backups`), or move the unreadable file
aside and rename `nickreg/antifake.dat.bak` to `nickreg/antifake.dat`; then restart. With
`Engine = mirror` or `mysql` the start reads the database instead when it can, and puts the
unreadable file aside by itself.

**MySQL: `Access denied`.** The user, the password or the host part of the user (`'user'@'%'`
or `'user'@'localhost'`) do not match; the same login works in `mysql -h <host> -u <user> -p`.

**MySQL: "wants the password method ...".** The user was made with a method AntiFake does not
speak (MariaDB's `ed25519`). `ALTER USER ... IDENTIFIED WITH mysql_native_password BY '...'`
(MariaDB: `IDENTIFIED VIA mysql_native_password USING PASSWORD('...')`) fixes it.

**MySQL: the login is refused although the password is right.** The user or the server insists
on TLS (`REQUIRE SSL`, `require_secure_transport`); AntiFake connects without it.

**A table is wider than the console.** The game console shows far more than a player can type.
Every table AntiFake draws, `/multi` included, is 100 characters wide: a column of names is as
wide as the longest name on the list, up to 24 characters; a list of names (`/multi top`) or the
closest match (`/multi`) that would not fit is shortened with `...` instead. A line that would
still be wider - a long place, provider or host name - is cut at 98 characters and ends in
`..`. One answer is `MaxQueuedLines` (200) lines at most; a longer one ends with a line that
says it was cut there. Lower `ResultsPerPage` if the pages are too long.
