AntiFake Extended 2.0 - example settings
========================================

Ready settings for the usual kinds of server. AntiFake does not depend on the game mode, so these
go by what the players of a server may see and by how much the server keeps. Each file is the
whole settings.ini of AntiFake with the values that kind of server should have: every changed
value has a line "; EXAMPLE: ..." above it that says what was changed and why, and the rest is
settings.ini as it comes, with the Steam key and the MySQL password left empty. The server never
reads this folder; a file counts only once it is copied over settings.ini.


The files
---------

public-server.ini
    A public server anybody may join: the settings AntiFake comes with. A player sees their own
    visits, places and profile, the country table of the server and who came when (without
    addresses); everything else is for the admins.

private-clan-server.ini
    A private or clan server whose players know each other: they may also ask about each other
    (visits and places, !lastvisits, !visitsof, !statusa, the names of an account, Steam,
    !locate, the list of accounts). Addresses, Steam accounts and computers of others stay
    masked for everybody but the admins, and the searches (which find accounts by an address or
    a computer too) and the multi-account list stay with the admins.

privacy-strict.ini
    A server that shows a player only what is about them and keeps less: no geo lookups (no
    address leaves the server, no "joined from" line), no country table, !locate or
    !lastvisitors for players, addresses and visits kept 90 days, at most 20 addresses an
    account, places looked up before forgotten after 30 days, and an account nobody played on
    for two years emptied (its number stays).

several-servers-mysql.ini
    Several servers sharing one live MySQL or MariaDB database (Engine = mysql): a player is the
    same account on every server, with everything any of them saw. Otherwise public-server.ini.


What each file changes
----------------------

setting               public-server  private-clan  privacy-strict  several-servers-mysql
PlayerVisits          self           all           self            self
PlayerVisitStats      all            all           off             all
PlayerVisitsOf        off            all           off             off
PlayerStatus          self           all           self            self
PlayerNames           self           all           self            self
PlayerSteam           self           all           self            self
PlayerLocate          self           all           off             self
PlayerProfiles        off            all           off             off
PlayerSearch          off            off           off             off
PlayerMulti           off            off           off             off
PlayerHelp            all            all           all             all
PlayerLastVisitors    all            all           off             all
[Geo] Enabled         1              1             0               1
GeoKeepDays           365            365           30              365
VisitKeepDays         365            365           90              365
AddressDays           180            180           90              180
MaxAddresses          50             50            20              50
ProfileDays           0              0             730             0
Engine                local          local         local           mysql

public-server.ini has every value of settings.ini. several-servers-mysql.ini also fills in
MysqlHost = 127.0.0.1, MysqlUser = antifake and MysqlPassword = CHANGE_ME, placeholders for
yours. off = admins only, self = a player about themselves, all = a player about anybody.


How to use one
--------------

1. Keep a copy of your own scripts/AntiFake-Extended/settings.ini, for example as
   settings.ini.bak. Take your SteamApiKey and your MySQL settings over into the new file, and
   whatever else you changed before (colours, limits).
2. Copy the example over settings.ini: the file must be called settings.ini and stay next to
   antifake.pas.
3. Fill in what is yours (below).
4. /recompile, or restart the server (restart it for several-servers-mysql.ini). The console
   says "version 2.0, settings read (0 out of range, 0 unknown)"; any other number means that
   many lines were not used and keep their built-in value.


What you still fill in
----------------------

[Steam] SteamApiKey
    Every example leaves it empty, and the Steam part (the name on Steam, the age of the account,
    its bans) stays off until you put a key of your own there:
    https://steamcommunity.com/dev/apikey. Never publish a settings.ini with the key in it.

[MySQL] of several-servers-mysql.ini
    MysqlHost (the machine of the database; 127.0.0.1 is this machine), MysqlPort,
    MysqlDatabase, MysqlUser, MysqlPassword (CHANGE_ME is only a placeholder) and MysqlServerId
    (every server its own number). The database must exist; AntiFake makes the tables. README.md,
    "MySQL", says more.

Never publish a settings.ini with your passwords in it.


A kind of server on several servers
-----------------------------------

Take the example of the kind of server (private-clan-server.ini, privacy-strict.ini) and copy
the whole [MySQL] section of several-servers-mysql.ini over the one in it; give every server its
own MysqlServerId. With Engine = mysql the cleanup never empties an account (ProfileDays),
because the other servers may still use it.
