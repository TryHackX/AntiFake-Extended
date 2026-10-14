{ ---------------------------------------------------------------------------------------
  AntiFake Extended - the numbers, the records and everything the whole library shares.
  --------------------------------------------------------------------------------------- }

const
  BINARY_MAGIC = $414E5446;
  BINARY_VERSION = 6;   { 6 adds the visit section at the end of the file }
  MAX_STRING_LEN = 64;
  MAX_PLAYERS = 32;
  { the server console and the remote admin have no player, so they get a context of their own }
  CONSOLE_SLOT = 33;
  MAX_SLOTS = 33;
  { the geo numbers of the first versions; nothing reads these four any more - the lookups
    follow [Geo] in settings.ini, with the DEF_GEO_ values below when it says nothing }
  GEO_CACHE_EXPIRE = 90.0;
  GEO_MAX_LOOKUPS = 4;        { geo lookups running at the same time }
  GEO_RETRY_SECONDS = 600;    { an address that could not be located is asked again after 10 minutes }
  GEO_TIMEOUT_MS = 5000;

  KIND_NAME = 0;
  KIND_STEAM = 1;
  KIND_HWID = 2;
  KIND_IP = 3;
  KIND_GEO_MATCH = 5;
  KIND_ALL_LOC = 6;

  SORT_ID_UP = 0;
  SORT_ID_DOWN = 1;
  SORT_COUNT_UP = 2;
  SORT_COUNT_DOWN = 3;
  SORT_TIME_UP = 4;
  SORT_TIME_DOWN = 5;
  SORT_LATEST_UP = 6;
  SORT_LATEST_DOWN = 7;
  SORT_FIRST_UP = 8;
  SORT_FIRST_DOWN = 9;

  TASK_NONE = 0;
  TASK_SEARCH = 1;
  TASK_SEARCHID = 2;
  TASK_PRIVACY = 3;
  TASK_PRIVACYID = 4;
  TASK_CHECKP = 5;
  TASK_CHECKD = 6;
  TASK_STATUSA = 7;
  { how much of the ties of an account TASK_STATUSA looks for (its Kind): an admin gets every
    account that shares something, names too; a player who may ask /multi about the account
    only the Steam accounts, computers and addresses (from the indexes); anybody else none }
  STATUS_TIES_ALL = 0;
  STATUS_TIES_KEYS = 1;
  STATUS_TIES_NONE = 2;
  TASK_LIST = 8;
  TASK_ADVANCED_SEARCH = 9;
  TASK_GEOSEARCH = 10;
  TASK_LOCATE_ALL = 11;

  OPTIMIZE_COUNT = 2000;
  OPTIMIZE_COUNT_ADVANCED = 300;

  { every number below can be set in settings.ini; these are the values used without it }
  DEF_GEO_MAX_LOOKUPS = 4;
  DEF_GEO_RETRY_SECONDS = 600;
  DEF_GEO_TIMEOUT_MS = 5000;
  DEF_GEO_REFRESH_DAYS = 90;
  DEF_GEO_KEEP_DAYS = 365;
  DEF_ADDRESS_DAYS = 180;
  DEF_NAME_DAYS = 0;
  DEF_HWID_DAYS = 0;
  DEF_MAX_ADDRESSES = 50;
  DEF_PROFILE_DAYS = 0;
  DEF_SAVE_MINUTES = 15;
  DEF_RESULTS_PER_PAGE = 10;
  DEF_TASK_TIMEOUT_SECONDS = 30;
  DEF_LOCATE_TIMEOUT_SECONDS = 30;

  { flags of a visit }
  VF_PROXY = 1;        { the address belongs to a proxy or a VPN }
  VF_HOSTING = 2;      { the address belongs to a server room }
  VF_MOBILE = 4;       { mobile network: the city moves around }
  VF_NEW_IP = 8;       { the account had not used this address before }
  VF_NEW_HWID = 16;    { the account had not used this computer before }
  VF_UNKNOWN = 32;     { local address, or nothing known about it }

  VISITS_MAGIC = $53545356;   { "VSTS": the visit section at the end of the database }
  DEF_ACTIVE_DAYS = 30;
  AF_VERSION = '2.1';
  { where the data lives (Engine in settings.ini) }
  ENGINE_BIN = 0;      { local: the file alone }
  ENGINE_MIRROR = 1;   { the file, with a copy of it in the database for a website, read back at
                         start when the file is missing or older than the copy }
  ENGINE_MYSQL = 2;    { one database for every server (af_shared.inc); the file is its cache }
  STEAM_MAGIC = $4D414554;    { "TEAM": what Steam says about the accounts }
  VERDICT_MAGIC = $544C554D;  { "MULT": what an admin decided about a pair of accounts }
  { The journal: what changed since the last full write, so a server that is killed loses
    seconds instead of the quarter of an hour between two full writes. It is thrown away
    and started again with every full write, and replayed only onto the file it belongs to. }
  JOURNAL_MAGIC = $4C4A4641;  { "AFJL" }
  JOURNAL_FORMAT = 1;
  JREC_PROFILE = 1;
  JREC_VERDICTS = 2;
  MAX_JOURNAL_BYTES = 64 * 1024 * 1024;
  MAX_VERDICTS = 20000;
  { what an admin decided about two accounts that look tied together }
  VERDICT_NONE = 0;
  VERDICT_SAME = 1;           { one person }
  VERDICT_FAMILY = 2;         { two people, one house }
  VERDICT_IGNORE = 3;         { do not say it again }
  { Soldat shows an account as S + the 32-bit account number; the Steam Web API wants the
    64-bit form, which is that number plus this base. }
  STEAM_ID_BASE = Int64(76561197960265728);
  DEF_STEAM_REFRESH_DAYS = 7;
  DEF_STEAM_BATCH = 25;
  DEF_VISIT_KEEP_COUNT = 50;
  DEF_VISIT_KEEP_DAYS = 365;
  DEF_VISIT_COOLDOWN_MINUTES = 10;
  MAX_VISITS_HARD = 200;
  MAX_PROFILES_HARD = 5000000;

  TASK_VISITS = 12;
  TASK_UNIQUE_VISITS = 13;
  TASK_VISIT_STATS = 14;
  TASK_MULTI = 15;
  TASK_VISITS_OF = 16;
  TASK_MULTI_TOP = 17;

  ALGO_LEVENSHTEIN = 0;
  ALGO_JARO_WINKLER = 1;
  ALGO_TOKEN_SORT = 2;

type
  TBinaryOtherData = packed record
    DataString: array[0..MAX_STRING_LEN-1] of Char;
    DataLen: Word;
    IsActive: Byte;
    FirstUsed: Byte;
    LastUsed: Byte;
    LoginCount: Int64;
    TimeOnSpectator: Int64;
    TimePlayed: Int64;
    FirstUsedDate: Double;
    LastUsedDate: Double;
  end;

  TBinaryOtherDataArray = array of TBinaryOtherData;

  TBinaryProfile = packed record
    ActiveName: array[0..MAX_STRING_LEN-1] of Char;
    ActiveNameLen: Word;
    ActiveSteam: array[0..31] of Char;
    ActiveSteamLen: Word;
    ActiveHWID: array[0..31] of Char;
    ActiveHWIDLen: Word;
    ActiveIP: array[0..23] of Char;
    ActiveIPLen: Word;
    RegisterTime: Double;
    LastPlayedTime: Double;
    TimePlayed: Int64;
    TimeOnSpectator: Int64;
    OtherNamesCount: Word;
    OtherSteamsCount: Word;
    OtherHWIDsCount: Word;
    OtherIPsCount: Word;
  end;

  { the journal says which file it belongs to: a journal left over from an older database
    means nothing and is ignored }
  TJournalHeader = packed record
    Magic: LongWord;
    Format: LongWord;
    SnapSize: Int64;
    SnapTime: Double;
    Written: Double;
  end;

  { an admin looked at two accounts and said what they are }
  TPairVerdict = packed record
    A, B: LongInt;            { always A < B }
    Verdict: Byte;
    Filler: array[0..2] of Byte;
    Decided: Double;
    By: array[0..31] of Char;
  end;
  TVerdictArray = array of TPairVerdict;

  { what the Steam Web API says about an account; one entry per profile }
  TSteamInfo = packed record
    SteamId: array[0..23] of Char;
    Persona: array[0..63] of Char;    { the name on Steam }
    Country: array[0..3] of Char;
    Created: Double;                  { when the Steam account was made }
    Fetched: Double;                  { when we last asked }
    Visible: Byte;                    { 1 = public profile }
    VacBans: Byte;
    GameBans: Byte;
    CommunityBan: Byte;
    TradeBan: Byte;
    DaysSinceLastBan: Word;
    Filler: Word;
  end;

  { one visit of one account: when it started, how long it lasted, and from where }
  TVisitEntry = packed record
    Started: Double;
    Seconds: LongInt;
    IP: array[0..23] of Char;
    Name: array[0..31] of Char;
    Hwid: array[0..15] of Char;
    CountryCode: array[0..3] of Char;
    Confidence: Byte;      { 0-100: how sure the location is }
    Flags: Byte;           { see VF_ below }
    Filler: Word;
  end;
  TVisitArray = array of TVisitEntry;

  TProfileData = record
    Profile: TBinaryProfile;
    OtherNames: TBinaryOtherDataArray;
    OtherSteams: TBinaryOtherDataArray;
    OtherHWIDs: TBinaryOtherDataArray;
    OtherIPs: TBinaryOtherDataArray;
    Visits: TVisitArray;      { written in its own section at the end of the file }
    Steam: TSteamInfo;        { the same, in a section of its own }
  end;
  TProfileArray = array of TProfileData;

  TGeoEntry = packed record
    IP: array[0..23] of Char;
    Continent: array[0..31] of Char;
    ContinentCode: array[0..7] of Char;
    Country: array[0..63] of Char;
    CountryCode: array[0..7] of Char;
    Region: array[0..63] of Char;
    City: array[0..63] of Char;
    ISP: array[0..63] of Char;
    ReverseDNS: array[0..127] of Char;
    Zip: array[0..15] of Char;
    Timezone: array[0..31] of Char;
    IsMobile: Byte;
    IsProxy: Byte;
    IsHosting: Byte;
    LastUpdated: Double;
  end;

  { what settings.ini can change; Load_Settings fills it, the script asks for the values it needs }
  TAntiFakeSettings = record
    GeoEnabled: Boolean;
    GeoMaxLookups: Integer;
    GeoRetrySeconds: Integer;
    GeoTimeoutMs: Integer;
    GeoRefreshDays: Integer;
    GeoKeepDays: Integer;
    GeoTextPlain: Boolean;   { write place names (and every other text from the web, the Steam
                               names too) with plain letters only }
    VisitKeepCount: Integer;
    VisitKeepDays: Integer;
    VisitCooldownMinutes: Integer;
    LastVisitsRows: Integer;      { the rows of /lastvisits and /lastvisitors }
    CommandCooldownSeconds: Integer;
    HeavyCooldownSeconds: Integer;
    MaxListsPerSecond: Integer;
    PublicCommands: Boolean;
    Trace: Boolean;
    LinesPerTick: Integer;
    MaxQueuedLines: Integer;
    ActiveDays: Integer;          { how long an entry counts as one the account still uses }
    { where the data lives: ENGINE_BIN (the file), ENGINE_MIRROR (the file, and a copy in MySQL
      for a website) or ENGINE_MYSQL (one database for every server, the file its cache) }
    MysqlEngine: Integer;
    MysqlHost: string;
    MysqlPort: Integer;
    MysqlDatabase: string;
    MysqlUser: string;
    MysqlPassword: string;
    { the RSA public key of the database server (a PEM file); '' = the server is asked for it }
    MysqlServerKey: string;
    MysqlServerId: Integer;
    MysqlMinutes: Integer;
    SyncSeconds: Integer;         { Engine = mysql: how often the changes are exchanged }
    { several servers of one database look into each other's copies }
    SharedSearch: Boolean;        { /af_search, /searchall, /checkd }
    SharedLinks: Boolean;         { /statusa, /checkp, /multi }
    { the code page the player names are in, for the UTF-8 of the database }
    MysqlCodepage: Integer;
    { the colours of the messages, as 0xRRGGBB }
    ColMain: Integer;
    ColInfo: Integer;
    ColData: Integer;
    ColWarn: Integer;
    ColError: Integer;
    ColGood: Integer;
    JournalSeconds: Integer;      { 0 = no journal, only the full write }
    BackupHours: Integer;         { a copy of the finished file this often; 0 = never }
    BackupKeep: Integer;          { how many of them are kept }
    { What a player without admin may ask with ! : 0 = nothing (admins only), 1 = about
      themselves, 2 = about anybody. An admin always has everything through / . }
    CmdVisits: Integer;           { !visits, !uniquevisits, !locations }
    CmdVisitStats: Integer;       { !visitstats - the countries of the whole server }
    CmdVisitsOf: Integer;         { !visitsof <country> - who comes from there }
    CmdStatus: Integer;           { !mystatus, !statusa, !af_status }
    CmdNames: Integer;            { !mynames, !privacy }
    CmdStopSteam: Integer;        { !steam }
    CmdLocate: Integer;           { !locate, !locateall }
    CmdProfiles: Integer;         { !listprofiles, !online, !showid }
    CmdSearch: Integer;           { !af_search, !searchall, !smartsearch, !checkd, !checkp,
                                    !geosearch, !showlocate }
    CmdMulti: Integer;            { !multi }
    CmdHelp: Integer;             { !antifake and the pages of a list }
    CmdLastVisitors: Integer;     { !lastvisitors: who came, when, for how long and on which
                                    account - no address, no place (off or self = admins only) }
    MultiNotice: Boolean;         { say in the console and to the admins when a linked account joins }
    MultiMinLinks: Integer;       { how many other accounts it takes before it is said }
    MultiByAddress: Boolean;      { a shared address counts, not only a shared computer }
    MultiMaxLinks: Integer;       { at most this many other accounts are collected }
    MultiReportScore: Integer;    { from this score on it is worth telling an admin }
    MultiMinSignals: Integer;     { and only with this many signals that agree (four at most:
                                    computer, Steam, address or network, name) }
    MultiMaxShared: Integer;      { an address with more accounts than this is a public one:
                                    a school, a cafe, a whole block behind one address, and it
                                    says nothing about anybody }
    MultiMaxSharedPC: Integer;    { a computer is another matter - twenty accounts on one
                                    machine is not a cafe, it is twenty accounts on one
                                    machine - so this limit is only there to bound the work }
    SteamKey: string;             { empty = the whole Steam part is off }
    SteamRefreshDays: Integer;
    SteamBatch: Integer;
    CurlPath: string;
    CurlTimeoutSeconds: Integer;
    AddressDays: Integer;
    NameDays: Integer;
    HwidDays: Integer;
    MaxAddresses: Integer;
    ProfileDays: Integer;         { an account nobody played on for this long is emptied by the
                                    daily cleanup (not with Engine = mysql); 0 = never }
    SaveMinutes: Integer;
    ResultsPerPage: Integer;
    TaskTimeoutSeconds: Integer;
    LocateTimeoutSeconds: Integer;
    AdminCommands: Boolean;
    PlayerCommands: Boolean;
    ChatCommands: Boolean;
    DatabaseFile: string;
    Loaded: Boolean;
    Message: string;
  end;

  TSearchResultItem = record
    ProfileIndex: Integer;
    SubIndex: Integer;
    DataString: string;
    ActiveName: string;
    LoginCount: Int64;
    TimePlayed: Int64;
    FirstUsedDate: Double;
    LastUsedDate: Double;
    NamesCount: Integer;
    SteamsCount: Integer;
    HWIDsCount: Integer;
    IPsCount: Integer;
    MatchType: Integer;    { in a list of visits: the VF_ flags of the visit }
    Similarity: Integer;   { in a list of visits: how sure its location is (0-100) }
    RiskLevel: Integer;
    Location: string;      { country, city and provider of a visit }
  end;

  TSearchTask = record
    TaskType, PlayerID, Kind, SortMethod, TargetIndex, MaxResults: Integer;
    SearchPhrase: string;
    SearchAll, ComparePartial, FilterFirst, FilterLast, FilterAll: Boolean;
    Gen: Integer;              { the slot's number of the search; see TPlayerContext.Gen }
  end;

  { an account of another server of the database that matches (SharedSearch, SharedLinks) }
  TForeignRow = record
    Server, Account: Integer;
    Name: string;           { its name now }
    Value: string;          { what matched: a name, a Steam id, a computer, an address }
    Kind: Integer;          { the kind of Value }
    Kinds: Integer;         { every kind that matched, 1 shl KIND_* }
    LastSeen: Double;
  end;
  TForeignRows = array of TForeignRow;

  TSearchResult = record
    PlayerID, TaskType, Kind, SortMethod, ResultCount, ProfileIndex, MultiAccountsCount: Integer;
    MultiAccountsByNameCount: Integer;
    Success: Boolean;
    Results: array of TSearchResultItem;
    ExecutionTime, RegisterTime, LastPlayedTime: Double;
    ActiveName: string;
    TopLocations: string;
    TimePlayed, TimeOnSpectator: Int64;
    Foreign: TForeignRows;  { the other servers of the database }
    ForeignNote: string;    { why they could not be read, '' when they could }
  end;
  PSearchResult = ^TSearchResult;

  TSearchThread = class(TThread)
  private
    FTask: TSearchTask;
  protected
    procedure Execute; override;
  public
    constructor Create(const ATask: TSearchTask);
  end;

  TPlayerContext = record
    Thread: TSearchThread;
    Result: TSearchResult;
    IsBusy: Boolean;
    IsComplete: Boolean;
    Lock: TRTLCriticalSection;
    { counts every search and every player who leaves: a search that finishes for a number that
      is not the current one any more keeps its result to itself, so the next player on the
      slot never sees what the last one asked for }
    Gen: Integer;
  end;

  TLoadThread = class(TThread)
  private
    FFilePath: string;
    FTryMysql: Boolean;
  protected
    procedure Execute; override;
  public
    constructor Create(const APath: string; TryMysql: Boolean);
  end;

  TSaveThread = class(TThread)
  private
    FFilePath: string;
    FAsker, FSession: Integer;   { who asked (/storage save) and their session, 0 = nobody }
    FStarted: QWord;
  protected
    procedure Execute; override;
  public
    constructor Create(const APath: string; AAsker, ASession: Integer);
  end;

  TGeoThread = class(TThread)
  private
    FIP: string;
  protected
    procedure Execute; override;
  public
    constructor Create(const TargetIP: string);
  end;

var
  Settings: TAntiFakeSettings;
  Profiles: TProfileArray;
  ProfileCount: Integer = 0;
  ProfilesCritSec: TRTLCriticalSection;

  GeoCache: array of TGeoEntry;
  GeoCount: Integer = 0;
  GeoCritSec: TRTLCriticalSection;
  GeoPending: TStringList = nil;  { addresses with a lookup running }
  GeoFailed: TStringList = nil;   { address=second of the last failed lookup; not saved }

  { sorted keys of what a player brings with them, so a join does not read the whole database.
    SteamIndex and HwidIndex belong to ProfilesCritSec, GeoIndex to GeoCritSec. }
  SteamIndex: TStringList = nil;  { Steam account -> profile }
  HwidIndex: TStringList = nil;   { computer -> profile (several profiles may share one) }
  IpIndex: TStringList = nil;     { address -> profile (the same) }
  Net24Index: TStringList = nil;  { the first three numbers of an address -> profile }
  { the decisions belong to ProfilesCritSec, like the profiles they talk about }
  Verdicts: TVerdictArray = nil;
  VerdictCount: Integer = 0;
  { one byte per profile: 1 = changed since the last time the journal was written }
  Dirty: array of Byte;
  DirtyCount: Integer = 0;
  VerdictsDirty: Boolean = False;
  JournalCS: TRTLCriticalSection;
  JournalBusy: Boolean = False;
  JournalPath: string = '';
  LoadedPath: string = '';        { the database this server is running on }
  JournalBytes: Int64 = 0;
  JournalWrites: Integer = 0;
  JournalReplayed: Integer = 0;
  JournalLastError: string = '';
  VerdictIndex: TStringList = nil;  { "a:b" -> place in Verdicts }
  { Engine = mysql (af_shared.inc): what this server changed since the last exchange with the
    shared database. Everything but SharedGeoNew belongs to ProfilesCritSec, SharedGeoNew to
    GeoCritSec; the per-profile lists always have the same length (SharedGrow). }
  SharedOn: Boolean = False;
  SharedQuiet: Boolean = False;       { the exchange itself stores a decision: nothing to send back }
  SharedDirtyArr: array of Byte;      { 1 = changed here since it was last sent }
  SharedFlight: array of Byte;        { 1 = on its way to the database right now }
  SharedBase: array of AnsiString;    { the profile before its first change since the last
                                        exchange; '' = nothing changed here }
  SharedGlobal: array of LongInt;     { its number in the shared database, 0 = not there yet }
  SharedVersion: array of Int64;
  SharedLocal: array of LongInt;      { number in the database -> number here, -1 = none }
  SharedVerdictChanges: TVerdictArray = nil;
  SharedVerdictCount: Integer = 0;
  SharedGeoNew: TStringList = nil;    { addresses located here since the last exchange }
  GeoIndex: TStringList = nil;    { address -> entry in the geo cache }

  Contexts: array[1..MAX_SLOTS] of TPlayerContext;
  ActiveResult: PSearchResult;
  EmptyResult: TSearchResult;

  LoadThread: TLoadThread = nil;
  SaveThread: TSaveThread = nil;

  { the geo lookups running now. They are background threads like the others (StartBackground)
    and Cleanup waits for them with the rest (WaitBackground); the count is only kept. }
  GeoThreadsRunning: Integer = 0;
  ShuttingDown: Boolean = False;

  LoadInProgress: Boolean = False;
  LoadComplete: Boolean = False;
  LoadError: Boolean = False;
  SaveInProgress: Boolean = False;
  SaveComplete: Boolean = False;
  SaveError: Boolean = False;

  { what the last Login_Player did (Get_Login_Result); the game thread alone writes and reads
    them. The flags: 1 a new name, 2 a new Steam id, 4 a new computer, 8 a new address,
    16 a new account. }
  LoginResult_NameIdx: Integer = -1;
  LoginResult_SteamIdx: Integer = -1;
  LoginResult_HWIDIdx: Integer = -1;
  LoginResult_IPIdx: Integer = -1;
  LoginResult_Flags: Integer = 0;

  { the end of something that ran in the background, for one slot, as "slot|color|text";
    the script picks these up every tick }
  NoticeList: TStringList = nil;
  NoticeCS: TRTLCriticalSection;
  { where the database was read from at start, when there was a choice }
  LoadNote: string = '';
  { the database was read from MySQL: the script writes the file from it once it has noticed
    the load (Save_After_Load), so the file and the copy agree again }
  SaveAfterLoad: Boolean = False;
  { profiles written into the journal while a full write ran: the journal starts again empty
    at the end of the write, so they go into the new one. Belongs to ProfilesCritSec. }
  SaveFlushed: array of Integer = nil;
  SaveFlushedCount: Integer = 0;
  { counts the players of every slot: a notice for a player who has left is dropped }
  SlotSession: array[0..MAX_SLOTS] of Integer;
  { every thread that runs in the background, kept until it has finished and been freed, so
    the library is never unloaded under one (see StartBackground) }
  BgThreads: TList = nil;
  BgCS: TRTLCriticalSection;

  { one command at a time per slot, answered on a worker thread (af_run) }
  CmdBusy: array[0..MAX_SLOTS] of Boolean;
  { did this command start a search, or did it answer by itself? The script needs to know
    whether to wait for a result or to stop waiting once the worker is done. }
  CmdStartedTask: array[0..MAX_SLOTS] of Boolean;
  CmdCS: TRTLCriticalSection;
  { whether the last command of a slot came from an admin: a table shows an admin every
    address in full, a player only their own }
  ViewerAdmin: array[0..MAX_SLOTS] of Boolean;

{ What an export hands back as a PChar lives here until the same thread calls again. It is
  per thread: a command answered on a worker and the game thread reading its lines must never
  write the same string at the same time. }
threadvar
  TempStringResult: string;

{ ---------------- Steam ---------------- }

type
  TSteamThread = class(TThread)
  private
    FIds: string;        { the SteamIDs of this batch, separated by commas }
    FKey: string;        { the key, copied when the batch starts: a settings reload may replace it }
  protected
    procedure Execute; override;
  public
    constructor Create(const AIds: string);
  end;

var
  SteamQueue: TStringList = nil;   { SteamIDs waiting to be asked about }
  SteamCS: TRTLCriticalSection;
  SteamBusy: Boolean = False;
  SteamAsked: Integer = 0;
  SteamAnswered: Integer = 0;
  SteamFailed: Integer = 0;
  SteamLastError: string = '';   { what went wrong last time, in words for the admin }
