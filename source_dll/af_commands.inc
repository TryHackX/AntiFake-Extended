{ ---------------------------------------------------------------------------------------
  AntiFake Extended - every command, from the word the player typed to the answer.

  The script used to parse the words, check who may ask what, hold the flood limits and
  build the answer. All of it is here now: the script hands over the slot, the command and
  its arguments, and takes back finished lines.
  --------------------------------------------------------------------------------------- }

const
  CMD_PREFIX = '/';
  { what a command comes to. Run_Command tells the script CMD_UNKNOWN, CMD_TASK or CMD_REFUSED:
    a command it takes runs on a worker, and its lines (CMD_ANSWERED there) reach the buffer
    the way the answer of a search does }
  CMD_UNKNOWN = 0;      { not one of ours }
  CMD_ANSWERED = 1;     { the lines are waiting in the buffer (DispatchCommand only) }
  CMD_TASK = 2;         { taken: the answer comes on a later tick }
  CMD_REFUSED = 3;      { not allowed, or too soon after the last one - the buffer says why }

var
  { flood limits, the same idea as the heavy lists of ZitroStats }
  LastCmdAt: array[0..MAX_SLOTS] of Double;
  LastHeavyAt: array[0..MAX_SLOTS] of Double;
  HeavySecond: Double = 0;
  HeavyInSecond: Integer = 0;

{ ---------------- the small things the views ask for ---------------- }

{ every word the library answers to; anything else is somebody else's command }
{ The names a player types. search, status, help and the pages are ZitroStats commands as well,
  so AntiFake answers them as af_search, af_status, af_help, af_next, af_prev and af_page, or the
  same without the underscore; everything inside the library keeps the short name (InternalName). }
function IsKnownCommand(const Cmd: string): Boolean;
const
  List = ' af_search afsearch searchall smartsearch advancedsearch privacy privacyall checkp checkd' +
         ' statusa af_status afstatus mystatus mynames listprofiles listprofilesonline listonline online' +
         ' showid showprofileid visits uniquevisits uvisits locations visitstats visitmetrics' +
         ' visitsof whofrom locate locateall showlocate geosearch steam mysteam steamstatus' +
         ' multi multiaccounts multiset setmulti storage afstorage antifake af_help afhelp' +
         ' af_next afnext af_prev afprev af_page afpage lastvisits lastvisitors ';
begin
  Result := Pos(' ' + Cmd + ' ', List) > 0;
end;

function InternalName(const Cmd: string): string;
begin
  if (Cmd = 'af_search') or (Cmd = 'afsearch') then Result := 'search'
  else if (Cmd = 'af_status') or (Cmd = 'afstatus') then Result := 'status'
  else if (Cmd = 'af_help') or (Cmd = 'afhelp') then Result := 'help'
  else if (Cmd = 'af_next') or (Cmd = 'afnext') then Result := 'next'
  else if (Cmd = 'af_prev') or (Cmd = 'afprev') then Result := 'prev'
  else if (Cmd = 'af_page') or (Cmd = 'afpage') then Result := 'page'
  else Result := Cmd;
end;

{ "#12" and "12" both mean account number 12 }
function AccountNumberOf(const S: string): Integer;
var
  T: string;
begin
  T := Trim(S);
  if Copy(T, 1, 1) = '#' then Delete(T, 1, 1);
  Result := StrToIntDef(T, -1);
end;

{ These run on the command workers, never on the game thread, so they wait for the lock: giving
  up after a moment used to answer "" or "no account" while a full write held it. }
function GetProfileActiveNameSafe(Index: Integer): string;
begin
  LockProfiles;
  try
    Result := GetProfileActiveName(Index);
  finally UnlockProfiles; end;
end;

function GetProfileActiveSteamSafe(Index: Integer): string;
begin
  LockProfiles;
  try
    Result := GetProfileActiveSteam(Index);
  finally UnlockProfiles; end;
end;

function GetProfileActiveHWIDSafe(Index: Integer): string;
begin
  LockProfiles;
  try
    Result := GetProfileActiveHWID(Index);
  finally UnlockProfiles; end;
end;

function GetProfileActiveIPSafe(Index: Integer): string;
begin
  LockProfiles;
  try
    Result := GetProfileActiveIP(Index);
  finally UnlockProfiles; end;
end;

{ the status lines, as plain strings for the views }
function JournalStatusText: string;
begin
  Result := string(Get_Journal_Status());
end;

function MysqlStatusText: string;
begin
  Result := string(Get_Mysql_Status());
end;

function SteamStatusText: string;
begin
  Result := string(Get_Steam_Status());
end;

function MultiStatsText: string;
begin
  Result := string(Get_Multi_Stats());
end;

function RestoreBackupFile(const Name: string): Boolean;
begin
  Result := Restore_Backup(PChar(Name));
end;

{ A copy of the result a slot holds, taken under the slot's lock. Several workers draw at the
  same time, and a player who leaves (or a new search) replaces the result while one draws. }
function ResultSnapshot(Slot: Integer): TSearchResult;
begin
  if (Slot < 1) or (Slot > MAX_SLOTS) then
  begin
    Result := EmptyResult;
    Exit;
  end;
  EnterCriticalSection(Contexts[Slot].Lock);
  try
    Result := Contexts[Slot].Result;
  finally LeaveCriticalSection(Contexts[Slot].Lock); end;
end;

{ How long drawing a table may wait for a lock. The tables are drawn on the workers, which
  wait: a full write or an exchange holds a lock for moments, and giving up after 5 or 20 ms
  printed "locating...", "-" or nothing in rows that had an answer. The game thread draws a table
  itself only when no worker could be started, and never waits. }
function DrawWait(Millis: Integer): Integer;
begin
  if GetCurrentThreadId = GameThread then Result := 5 else Result := Millis;
end;

{ where an address is, read from the cache only - drawing never starts a lookup }
function GeoTextOf(const IP: string): string;
var
  Idx: Integer;
begin
  Result := '';
  if IP = '' then Exit;
  if IsLocalAddress(IP) then
  begin
    Result := 'local network';
    Exit;
  end;
  if not GrabLock(GeoCritSec, DrawWait(1000)) then
  begin
    Result := '?';
    Exit;
  end;
  try
    Idx := FindGeoIndex(IP);
    if Idx >= 0 then
    begin
      Result := string(GeoCache[Idx].Country);
      if string(GeoCache[Idx].City) <> '' then
        Result := Result + ', ' + string(GeoCache[Idx].City);
      if string(GeoCache[Idx].ISP) <> '' then
        Result := Result + ' (' + string(GeoCache[Idx].ISP) + ')';
    end
    else if GeoPending.IndexOf(IP) >= 0 then Result := 'locating...'
    else Result := 'unknown';
  finally LeaveCriticalSection(GeoCritSec); end;
end;

{ everything known about where an address is, for an admin: the country and its code, the
  region, the city and the postcode, the provider, the host name and what kind of line it is }
function GeoFullTextOf(const IP: string): string;
var
  Idx: Integer;
  Region, City: string;
begin
  Result := '';
  if IP = '' then Exit;
  if IsLocalAddress(IP) then
  begin
    Result := 'local network';
    Exit;
  end;
  if not GrabLock(GeoCritSec, DrawWait(1000)) then
  begin
    Result := '?';
    Exit;
  end;
  try
    Idx := FindGeoIndex(IP);
    if Idx < 0 then
    begin
      if GeoPending.IndexOf(IP) >= 0 then Result := 'locating...' else Result := 'unknown';
      Exit;
    end;
    Result := string(GeoCache[Idx].Country);
    if string(GeoCache[Idx].CountryCode) <> '' then
      Result := Result + ' (' + string(GeoCache[Idx].CountryCode) + ')';
    Region := string(GeoCache[Idx].Region);
    City := string(GeoCache[Idx].City);
    if (Region <> '') and not SameText(Region, City) then Result := Result + ', ' + Region;
    if City <> '' then Result := Result + ', ' + City;
    if string(GeoCache[Idx].Zip) <> '' then Result := Result + ' ' + string(GeoCache[Idx].Zip);
    { the kind of line before the provider and the host name: a long host name cut at the width
      of the view took "proxy/VPN" with it }
    if GeoCache[Idx].IsProxy = 1 then Result := Result + ' | proxy/VPN';
    if GeoCache[Idx].IsHosting = 1 then Result := Result + ' | server room';
    if GeoCache[Idx].IsMobile = 1 then Result := Result + ' | mobile';
    if string(GeoCache[Idx].ISP) <> '' then Result := Result + ' | ' + string(GeoCache[Idx].ISP);
    if string(GeoCache[Idx].ReverseDNS) <> '' then Result := Result + ' | ' + string(GeoCache[Idx].ReverseDNS);
  finally LeaveCriticalSection(GeoCritSec); end;
end;

{ where an address is, as much as this viewer is shown: all of it for an admin }
function WhereFor(Slot: Integer; const IP: string): string;
begin
  if (Slot >= 0) and (Slot <= MAX_SLOTS) and ViewerAdmin[Slot] then Result := GeoFullTextOf(IP)
  else Result := GeoTextOf(IP);
end;

{ an address as a viewer may see it: whole for an admin and for the viewer's own account,
  otherwise only its first two numbers }
function AddressFor(Slot, Owner: Integer; const IP: string): string;
var
  i, Dots: Integer;
begin
  Result := IP;
  if (Slot < 0) or (Slot > MAX_SLOTS) then Exit;
  if ViewerAdmin[Slot] then Exit;
  if (Owner >= 0) and (Owner = OnlineProfileOf(Slot)) then Exit;
  Dots := 0;
  for i := 1 to Length(IP) do
    if IP[i] = '.' then
    begin
      Inc(Dots);
      if Dots = 2 then
      begin
        Result := Copy(IP, 1, i) + '*.*';
        Exit;
      end;
    end;
  Result := '*';
end;

{ A Steam account or a computer as a viewer may see it: whole for an admin and for the viewer's
  own account, otherwise only its first letters - like an address, only the admins see those of
  somebody else. An address goes through AddressFor, anything else (a name) is shown. }
function SecretFor(Slot, Owner, Kind: Integer; const Value: string): string;
begin
  if Kind = KIND_IP then
  begin
    Result := AddressFor(Slot, Owner, Value);
    Exit;
  end;
  Result := Value;
  if (Kind <> KIND_STEAM) and (Kind <> KIND_HWID) then Exit;
  if (Slot < 0) or (Slot > MAX_SLOTS) then Exit;
  if ViewerAdmin[Slot] then Exit;
  if (Owner >= 0) and (Owner = OnlineProfileOf(Slot)) then Exit;
  Result := Copy(Value, 1, 3) + '***';
end;

function VerdictSeen(A, B: Integer): Integer;
begin
  Result := VERDICT_NONE;
  if not GrabProfiles(DrawWait(2000)) then Exit;
  try
    Result := VerdictOf(A, B);
  finally UnlockProfiles; end;
end;

{ "Cabal (PL), account from 2015-06-11, no bans" - or nothing when Steam was never asked }
function SteamLineOf(Index: Integer): string;
var
  Bans: string;
begin
  Result := '';
  if not GrabProfiles(DrawWait(2000)) then Exit;
  try
    if (Index < 0) or (Index >= ProfileCount) then Exit;
    if Profiles[Index].Steam.Fetched <= 0 then Exit;
    Result := KeptToConsole(string(Profiles[Index].Steam.Persona));
    if string(Profiles[Index].Steam.Country) <> '' then
      Result := Result + ' (' + string(Profiles[Index].Steam.Country) + ')';
    if Profiles[Index].Steam.Created > 1 then
      Result := Result + ', account from ' + DateText(Profiles[Index].Steam.Created);
    if Profiles[Index].Steam.Visible = 0 then Result := Result + ', profile hidden';
    Bans := '';
    if Profiles[Index].Steam.VacBans > 0 then
      Bans := 'VAC ' + IntToStr(Profiles[Index].Steam.VacBans);
    if Profiles[Index].Steam.GameBans > 0 then
    begin
      if Bans <> '' then Bans := Bans + ', ';
      Bans := Bans + 'game ' + IntToStr(Profiles[Index].Steam.GameBans);
    end;
    if Profiles[Index].Steam.CommunityBan = 1 then
    begin
      if Bans <> '' then Bans := Bans + ', ';
      Bans := Bans + 'community';
    end;
    if Bans = '' then Result := Result + ', no bans'
    else Result := Result + ', BANS: ' + Bans;
  finally UnlockProfiles; end;
end;

{ "#17 Kowal (92, computer+address)" for the closest account, or nothing }
function MultiSummaryOf(Index: Integer): string;
var
  Links: TStringList;
  i, Other, Bits, Score, Sig, Best, BestIdx, BestBits: Integer;
  Reasons: string;
begin
  Result := '';
  Links := TStringList.Create;
  try
    if not GrabProfiles(DrawWait(2000)) then Exit;
    try
      CollectLinks(Index, Links);
      Best := -1;
      BestIdx := -1;
      BestBits := 0;
      for i := 0 to Links.Count - 1 do
      begin
        Other := StrToIntDef(Links[i], -1);
        Bits := Integer(PtrInt(Links.Objects[i]));
        Score := LinkScore(Index, Other, Bits, Reasons, Sig);
        if Score > Best then
        begin
          Best := Score;
          BestIdx := Other;
          BestBits := Bits;
        end;
      end;
      if BestIdx < 0 then Exit;
      Result := IntToStr(Links.Count) + ' other account';
      if Links.Count > 1 then Result := Result + 's';
      Result := Result + ', closest #' + IntToStr(BestIdx) + ' ' +
                GetProfileActiveName(BestIdx) + ' (' + IntToStr(Best) + ', ' +
                LinkText(BestBits) + ')';
    finally UnlockProfiles; end;
  finally Links.Free; end;
end;

{ ---------------- who may ask what ---------------- }

function ScopeOfCommand(const Cmd: string): Integer;
begin
  if (Cmd = 'visits') or (Cmd = 'uniquevisits') or (Cmd = 'uvisits') or (Cmd = 'locations') or
     (Cmd = 'lastvisits') then
    Result := Settings.CmdVisits
  { the players get it without addresses and places (the simple list of RenderPage) }
  else if Cmd = 'lastvisitors' then Result := Settings.CmdLastVisitors
  else if (Cmd = 'visitstats') or (Cmd = 'visitmetrics') then Result := Settings.CmdVisitStats
  else if (Cmd = 'visitsof') or (Cmd = 'whofrom') then Result := Settings.CmdVisitsOf
  else if (Cmd = 'mystatus') or (Cmd = 'statusa') or (Cmd = 'status') then Result := Settings.CmdStatus
  else if (Cmd = 'mynames') or (Cmd = 'privacy') or (Cmd = 'privacyall') then Result := Settings.CmdNames
  else if (Cmd = 'steam') or (Cmd = 'mysteam') then Result := Settings.CmdStopSteam
  else if (Cmd = 'locate') or (Cmd = 'locateall') then Result := Settings.CmdLocate
  else if (Cmd = 'listprofiles') or (Cmd = 'listprofilesonline') or (Cmd = 'listonline') or
          (Cmd = 'online') or (Cmd = 'showid') or (Cmd = 'showprofileid') then
    Result := Settings.CmdProfiles
  else if (Cmd = 'search') or (Cmd = 'searchall') or (Cmd = 'smartsearch') or
          (Cmd = 'advancedsearch') or (Cmd = 'checkd') or (Cmd = 'checkp') or
          (Cmd = 'geosearch') or (Cmd = 'showlocate') then Result := Settings.CmdSearch
  else if (Cmd = 'multi') or (Cmd = 'multiaccounts') then Result := Settings.CmdMulti
  else if (Cmd = 'antifake') or (Cmd = 'help') or (Cmd = 'next') or (Cmd = 'prev') or
          (Cmd = 'page') then Result := Settings.CmdHelp
  else Result := 0;
end;

{ these name somebody else by their nature, so a player needs the whole reach for them }
function NeedsFullScope(const Cmd: string): Boolean;
const
  List = ' statusa privacy privacyall locateall listprofiles listprofilesonline' +
         ' listonline online showid showprofileid search searchall smartsearch advancedsearch' +
         ' checkd checkp geosearch showlocate multi multiaccounts visitsof whofrom' +
         ' lastvisits lastvisitors ';
begin
  Result := Pos(' ' + Cmd + ' ', List) > 0;
end;

{ the words of /visits that name a range or a filter, not a player; a word in double quotes is
  a name, also one with ".." in it }
function IsVisitKeyword(const S: string): Boolean;
begin
  Result := False;
  if (S <> '') and (S[1] = '"') then Exit;
  Result := (S = 'all') or (S = 'masked') or (S = 'clean') or (S = 'bycountry') or
            (S = 'byip') or (S = 'byname') or (Pos('sort:', S) = 1) or (Pos('..', S) > 0);
  if not Result then
    if Length(S) >= 2 then
      if S[Length(S)] = 'd' then Result := StrToIntDef(Copy(S, 1, Length(S) - 1), -1) > 0;
end;

{ one command per player per CommandCooldownSeconds, one list per HeavyCooldownSeconds and
  at most MaxListsPerSecond lists a second on the whole server. Admins are not held back. }
function CommandAllowed(Slot: Integer; Heavy, IsAdmin: Boolean; var Why: string): Boolean;
var
  NowT, Second: Double;
  Wait: Integer;
begin
  Result := True;
  Why := '';
  if IsAdmin or (Slot = CONSOLE_SLOT) then Exit;
  if (Slot < 0) or (Slot > MAX_SLOTS) then Exit;
  NowT := Now;
  { the clock went back (the end of summer time): what was noted in the "future" waits no more }
  if LastCmdAt[Slot] > NowT then LastCmdAt[Slot] := 0;
  if LastHeavyAt[Slot] > NowT then LastHeavyAt[Slot] := 0;
  if Settings.CommandCooldownSeconds > 0 then
    if NowT - LastCmdAt[Slot] < Settings.CommandCooldownSeconds / 86400.0 then
    begin
      Wait := Round((Settings.CommandCooldownSeconds / 86400.0 - (NowT - LastCmdAt[Slot])) * 86400) + 1;
      Why := 'Wait ' + IntToStr(Wait) + ' s before the next command.';
      Result := False;
      Exit;
    end;
  if Heavy then
  begin
    if Settings.HeavyCooldownSeconds > 0 then
      if NowT - LastHeavyAt[Slot] < Settings.HeavyCooldownSeconds / 86400.0 then
      begin
        Wait := Round((Settings.HeavyCooldownSeconds / 86400.0 - (NowT - LastHeavyAt[Slot])) * 86400) + 1;
        Why := 'Wait ' + IntToStr(Wait) + ' s before the next list.';
        Result := False;
        Exit;
      end;
    Second := Int(NowT * 86400);
    if Second <> HeavySecond then
    begin
      HeavySecond := Second;
      HeavyInSecond := 0;
    end;
    if HeavyInSecond >= Settings.MaxListsPerSecond then
    begin
      Why := 'The server is busy with other lists, try again in a moment.';
      Result := False;
      Exit;
    end;
    Inc(HeavyInSecond);
    LastHeavyAt[Slot] := NowT;
  end;
  LastCmdAt[Slot] := NowT;
end;

{ ---------------- reading the arguments ---------------- }

function ParseKindLetter(const S: string): Integer;
var
  T: string;
begin
  T := LowerCase(Trim(S));
  if (T = 'n') or (T = 'name') then Result := KIND_NAME
  else if (T = 's') or (T = 'steam') then Result := KIND_STEAM
  else if (T = 'h') or (T = 'hwid') or (T = 'pc') then Result := KIND_HWID
  else if (T = 'i') or (T = 'ip') or (T = 'address') then Result := KIND_IP
  else Result := -1;
end;

function ParseSortWord(const S: string): Integer;
var
  T: string;
begin
  T := LowerCase(Trim(S));
  if T = 'id' then Result := SORT_ID_UP
  else if T = 'id-' then Result := SORT_ID_DOWN
  else if T = 'count' then Result := SORT_COUNT_DOWN
  else if T = 'count+' then Result := SORT_COUNT_UP
  else if T = 'time' then Result := SORT_TIME_DOWN
  else if T = 'time+' then Result := SORT_TIME_UP
  else if T = 'latest' then Result := SORT_LATEST_DOWN
  else if T = 'latest+' then Result := SORT_LATEST_UP
  else if T = 'first' then Result := SORT_FIRST_UP
  else if T = 'first-' then Result := SORT_FIRST_DOWN
  else Result := -1;
end;

{ the words of a command, split on spaces. A text in double quotes is one word, spaces and all,
  and keeps its quotes, so whoever reads the words knows it is a name: "Big Time", "#1" and
  "all" are names, never a range, a sort or an account number. }
procedure SplitWords(const S: string; List: TStringList);
var
  i: Integer;
  Cur: string;
  Quoted: Boolean;
begin
  List.Clear;
  Cur := '';
  Quoted := False;
  for i := 1 to Length(S) do
    if Quoted then
    begin
      Cur := Cur + S[i];
      if S[i] = '"' then Quoted := False;
    end
    else if S[i] = ' ' then
    begin
      if Cur <> '' then List.Add(Cur);
      Cur := '';
    end
    else
    begin
      if (S[i] = '"') and (Cur = '') then Quoted := True;
      Cur := Cur + S[i];
    end;
  if Cur <> '' then List.Add(Cur);
end;

{ the text without its double quotes: what stands between the first two of them (a closing one
  that is missing is forgiven); a text without them is only trimmed }
function UnquotedText(const S: string): string;
var
  q: Integer;
begin
  Result := Trim(S);
  if (Result = '') or (Result[1] <> '"') then Exit;
  Delete(Result, 1, 1);
  q := Pos('"', Result);
  if q > 0 then Result := Copy(Result, 1, q - 1);
end;

{ Does the text name a player: any word of it that is not a range or a filter of the visits.
  Only the first word used to be looked at, so "!visits 7d Kowal" showed a player whose reach
  is "self" the visits of Kowal. }
function NamesTarget(const Params: string): Boolean;
var
  W: TStringList;
  k: Integer;
begin
  Result := False;
  W := TStringList.Create;
  try
    SplitWords(Trim(Params), W);
    for k := 0 to W.Count - 1 do
      if not IsVisitKeyword(LowerCase(W[k])) then
      begin
        Result := True;
        Exit;
      end;
  finally W.Free; end;
end;

{ ---------------- /listprofiles [sort] [asc|desc] ---------------- }

const
  LIST_BY_ID = 0;
  LIST_BY_NAME = 1;
  LIST_BY_TIME = 2;
  LIST_BY_LOGINS = 3;
  LIST_BY_LAST = 4;
  LIST_BY_FIRST = 5;
  LIST_BY_NAMES = 6;
  LIST_BY_IPS = 7;
  LIST_BY_PCS = 8;
  LIST_BY_STEAM = 9;
  LIST_SORT_WORDS = 'id, name, time, logins, last, first, names, ips, pcs, steam';

{ one word of /listprofiles: what it sorts by (LIST_BY_*), -1 for none. The words of before
  stay (count was the logins, latest the last visit), and so does their sign: a + behind a word
  sorts up and a - down (Dir 1 and 2; 0 = not said). }
function ParseListKey(const S: string; out Dir: Integer): Integer;
var
  T: string;
begin
  T := LowerCase(Trim(S));
  Dir := 0;
  if (T <> '') and (T[Length(T)] = '+') then
  begin
    Dir := 1;
    Delete(T, Length(T), 1);
  end
  else if (T <> '') and (T[Length(T)] = '-') then
  begin
    Dir := 2;
    Delete(T, Length(T), 1);
  end;
  if (T = 'id') or (T = 'number') or (T = 'num') or (T = 'nr') then Result := LIST_BY_ID
  else if (T = 'name') or (T = 'nick') or (T = 'nickname') then Result := LIST_BY_NAME
  else if (T = 'time') or (T = 'played') or (T = 'playtime') or (T = 'playedtime') then Result := LIST_BY_TIME
  else if (T = 'logins') or (T = 'login') or (T = 'joins') or (T = 'join') or (T = 'count') then
    Result := LIST_BY_LOGINS
  else if (T = 'last') or (T = 'seen') or (T = 'lastseen') or (T = 'latest') then Result := LIST_BY_LAST
  else if (T = 'first') or (T = 'registered') or (T = 'new') or (T = 'firstseen') then Result := LIST_BY_FIRST
  else if T = 'names' then Result := LIST_BY_NAMES
  else if (T = 'ips') or (T = 'addresses') or (T = 'ip') or (T = 'address') then Result := LIST_BY_IPS
  else if (T = 'computers') or (T = 'hwids') or (T = 'pcs') or (T = 'computer') or (T = 'hwid') or
          (T = 'pc') then Result := LIST_BY_PCS
  else if (T = 'steam') or (T = 'steams') or (T = 'steamids') then Result := LIST_BY_STEAM
  else Result := -1;
end;

{ The words of /listprofiles (and !listprofiles): what the list is sorted by and which way.
  Nothing is by number, lowest first, as it always was; a sort without a direction goes down -
  the biggest, the newest, the end of the alphabet first; asc or desc alone keep the number. A
  word it does not know, or two sorts, is False and Why says so. }
function ParseListSort(const Params: string; out Key: Integer; out Desc: Boolean;
  out Why: string): Boolean;
var
  W: TStringList;
  k, Found, Dir, D1: Integer;
  T: string;
begin
  Result := False;
  Key := LIST_BY_ID;
  Desc := False;
  Why := '';
  Found := -1;
  Dir := 0;
  D1 := 0;
  W := TStringList.Create;
  try
    SplitWords(Trim(Params), W);
    for k := 0 to W.Count - 1 do
    begin
      T := LowerCase(W[k]);
      if (T = 'asc') or (T = 'ascending') then Dir := 1
      else if (T = 'desc') or (T = 'descending') then Dir := 2
      else if ParseListKey(T, D1) < 0 then
      begin
        Why := 'Not a sort: ' + W[k] + '.';
        Exit;
      end
      else if Found >= 0 then
      begin
        Why := 'One sort at a time, not ' + Trim(Params) + '.';
        Exit;
      end
      else
      begin
        Found := ParseListKey(T, D1);
        if D1 > 0 then Dir := D1;
      end;
    end;
  finally W.Free; end;
  if Found >= 0 then Key := Found;
  if Dir = 0 then Desc := Found >= 0 else Desc := Dir = 2;
  Result := True;
end;

{ "by time played, highest first" }
function ListOrderText(Key: Integer; Desc: Boolean): string;
begin
  case Key of
    LIST_BY_NAME: Result := 'by name';
    LIST_BY_TIME: Result := 'by time played';
    LIST_BY_LOGINS: Result := 'by logins';
    LIST_BY_LAST: Result := 'by last seen';
    LIST_BY_FIRST: Result := 'by first seen';
    LIST_BY_NAMES: Result := 'by the number of names';
    LIST_BY_IPS: Result := 'by the number of addresses';
    LIST_BY_PCS: Result := 'by the number of computers';
    LIST_BY_STEAM: Result := 'by the number of Steam ids';
  else Result := 'by account number';
  end;
  if Key = LIST_BY_NAME then
  begin
    if Desc then Result := Result + ', Z to A' else Result := Result + ', A to Z';
  end
  else if (Key = LIST_BY_LAST) or (Key = LIST_BY_FIRST) then
  begin
    if Desc then Result := Result + ', newest first' else Result := Result + ', oldest first';
  end
  else if Desc then Result := Result + ', highest first'
  else Result := Result + ', lowest first';
end;

{ the account whose current name is exactly this, or the best substring match. The old
  version walked the list backwards and took the first thing it found, so two similar
  nicknames gave an answer that depended on the order in the file. }
{ The account a name means: the closest name (NameRank); between two of the same kind a player
  on the server first, then the shorter name, then the one that played last. On a worker: it
  waits for the lock, and takes it for a few thousand accounts at a time - the whole list under
  one hold kept every join waiting, and a hold it could not get within 50 ms answered "no account
  found" for a name that was there. }
function FindProfileByNameExact(const Name: string): Integer;
const
  PER_HOLD = 4096;
var
  i, n, s, R, BestR, OnCount: Integer;
  BestTime: Double;
  Low: string;
  Online: array[0..MAX_SLOTS] of Integer;
  IsOn, BestOn, Better, More: Boolean;
begin
  Result := -1;
  Low := LowerCase(Trim(Name));
  if Low = '' then Exit;
  OnCount := 0;
  EnterCriticalSection(OnlineCS);
  try
    for s := 1 to MAX_SLOTS do
      if OnlineHere[s] and (OnlineProfile[s] >= 0) then
      begin
        Online[OnCount] := OnlineProfile[s];
        Inc(OnCount);
      end;
  finally LeaveCriticalSection(OnlineCS); end;
  BestR := MaxInt;
  BestTime := -1;
  BestOn := False;
  i := 0;
  repeat
    LockProfiles;
    try
      n := 0;
      while (i < ProfileCount) and (n < PER_HOLD) do
      begin
        Inc(n);
        R := NameRank(GetProfileActiveName(i), Low);
        if R >= 0 then
        begin
          IsOn := False;
          for s := 0 to OnCount - 1 do
            if Online[s] = i then
            begin
              IsOn := True;
              Break;
            end;
          if Result < 0 then Better := True
          else if R div 1000 <> BestR div 1000 then Better := R div 1000 < BestR div 1000
          else if IsOn <> BestOn then Better := IsOn
          else if R mod 1000 <> BestR mod 1000 then Better := R mod 1000 < BestR mod 1000
          else Better := Profiles[i].Profile.LastPlayedTime > BestTime;
          if Better then
          begin
            Result := i;
            BestR := R;
            BestOn := IsOn;
            BestTime := Profiles[i].Profile.LastPlayedTime;
          end;
        end;
        Inc(i);
      end;
      More := i < ProfileCount;
    finally UnlockProfiles; end;
  until not More;
end;

const
  { What the forms put where the player's words were (ExpandTargetForm): the account of a slot
    (#), a row of the last list (@) or a position of the ZitroStats ranking (^). The sign in
    front of them cannot be typed, so a name that looks like one of them ("#1", "@2", "^3") is
    never taken for one - "@2" typed by a player used to be row 2 of the last list. }
  FORM_MARK = #1;

{ what the asker typed, for a message: the sign of the forms left out }
function ShownText(const S: string): string;
var
  k: Integer;
begin
  Result := S;
  for k := Length(Result) downto 1 do
    if Result[k] = FORM_MARK then Delete(Result, k, 1);
end;

{ "#12" as it was typed, or as a form put it in (FORM_MARK): True, with the number in Acc and
  Typed saying which of the two. A # with anything but digits behind it is a name ("#Kowal"):
  it used to be read as a number, and such a player could not be named at all. }
function IsAccountForm(const S: string; out Acc: Integer; out Typed: Boolean): Boolean;
var
  T: string;
  k: Integer;
begin
  Result := False;
  Acc := -1;
  Typed := True;
  T := Trim(S);
  if Copy(T, 1, 2) = FORM_MARK + '#' then
  begin
    Typed := False;
    Delete(T, 1, 1);
  end;
  if (Length(T) < 2) or (T[1] <> '#') then Exit;
  for k := 2 to Length(T) do
    if not (T[k] in ['0'..'9']) then Exit;
  { more digits than any account number has: no account }
  if Length(T) > 10 then Acc := MaxInt
  else Acc := StrToIntDef(Copy(T, 2, Length(T)), MaxInt);
  Result := True;
end;

{ the account whose name is exactly this (the one FindProfileByNameExact takes among several),
  -1 for none }
function AccountNamed(const Name: string): Integer;
begin
  Result := FindProfileByNameExact(Name);
  if Result >= 0 then
    if not SameText(GetProfileActiveNameSafe(Result), Trim(Name)) then Result := -1;
end;

{ The account a text names, for the one on Slot who asked (-1: nobody hears anything). "#12" is
  account 12, as it always was; a name in double quotes is always a name; anything else is the
  closest name (FindProfileByNameExact). When an account is named "#12" as well, account 12 is
  still taken, and the one who asked hears which one it was and how to get the other; when
  there is no account 12, the account of that name is taken. A form (a slot, a row, a position)
  says exactly which account it means. -1 when nothing matches. On a worker. }
function ResolveProfileFor(Slot: Integer; const S: string): Integer;
var
  T, Name: string;
  Acc, Other: Integer;
  Typed, Valid: Boolean;
begin
  Result := -1;
  T := Trim(S);
  if T = '' then Exit;
  if not IsAccountForm(T, Acc, Typed) then
  begin
    Result := FindProfileByNameExact(UnquotedText(T));
    Exit;
  end;
  { a worker: it waits for the lock rather than hand back a number nobody checked }
  LockProfiles;
  try
    Valid := (Acc >= 0) and (Acc < ProfileCount);
  finally UnlockProfiles; end;
  if Valid then Result := Acc;
  if not Typed then Exit;
  Other := AccountNamed(T);
  if (Other < 0) or (Other = Result) then Exit;
  if Valid then
  begin
    Name := GetProfileActiveNameSafe(Acc);
    if Name <> '' then Name := ' (' + Name + ')';
    Say(Slot, CLR_WARN, T + ' is account #' + IntToStr(Acc) + Name + '.');
    Say(Slot, CLR_WARN, 'The player named ' + T + ' is account #' + IntToStr(Other) + ': write "' + T +
        '" to mean them.');
  end
  else
  begin
    Result := Other;
    Say(Slot, CLR_WARN, 'There is no account ' + T + ': the player named ' + T + ' (account #' +
        IntToStr(Other) + ') was taken.');
  end;
end;

{ The account of the player on a slot, or of whoever the words name. Who may name somebody else
  was settled in Run_Command (the [PlayerCommands] reach), so a player whose reach is "all" gets
  the answer here like an admin. }
function TargetOf(Slot: Integer; const Params: string; IsAdmin: Boolean;
  var Idx: Integer; var Why: string): Boolean;
begin
  Result := False;
  Why := '';
  Idx := -1;
  if Trim(Params) = '' then
  begin
    Idx := OnlineProfileOf(Slot);
    if Idx >= 0 then
    begin
      Result := True;
      Exit;
    end;
    if Slot = CONSOLE_SLOT then
      Why := 'The console has no account of its own: name the player (a nick, part of it or #number).'
    else
      Why := 'Your account is not ready yet.';
    Exit;
  end;
  Idx := ResolveProfileFor(Slot, Params);
  if Idx < 0 then
  begin
    Why := 'No account found for: ' + ShownText(Trim(Params));
    Exit;
  end;
  Result := True;
end;

{ ---------------- <command>id <slot> and <command>p <row>, as in ZitroStats ---------------- }

const
  { the commands that name one account; those in KIND_FIRST take a kind letter before it }
  TARGET_STEMS = ' visits uniquevisits uvisits locations statusa af_status afstatus steam multi' +
                 ' multiaccounts locate privacy privacyall checkp showlocate ';
  KIND_FIRST = ' privacy privacyall checkp showlocate ';

{ /visitsid 3 asks about the player on server slot 3, /visitsp 2 about the account in row 2 of
  the last list the asker got. The command gets its plain name back and the number becomes
  #<account> (a slot is read here: one number under a short lock), @<row> (read by the worker,
  which holds the list - RowTargets) or ^<position> (RankTargets), each behind FORM_MARK. Used =
  it was one of these forms; Why is not empty when its number is wrong. }
procedure ExpandTargetForm(var C, P: string; var Used: Boolean; var Why: string);
const
  FORM_SLOT = 0;
  FORM_ROW = 1;
  FORM_RANK = 2;
var
  Stem, Head, Num, Tail, Form: string;
  Kind: Integer;
  N, Acc, q: Integer;
begin
  Used := False;
  Why := '';
  if IsKnownCommand(C) then Exit;     { checkp stays checkp }
  if (Length(C) > 2) and (Copy(C, Length(C) - 1, 2) = 'id') and
     (Pos(' ' + Copy(C, 1, Length(C) - 2) + ' ', TARGET_STEMS) > 0) then
  begin
    Stem := Copy(C, 1, Length(C) - 2);
    Kind := FORM_SLOT;
  end
  else if (Length(C) > 1) and (C[Length(C)] = 'p') and
          (Pos(' ' + Copy(C, 1, Length(C) - 1) + ' ', TARGET_STEMS) > 0) then
  begin
    Stem := Copy(C, 1, Length(C) - 1);
    Kind := FORM_ROW;
  end
  else if (Length(C) > 1) and (C[Length(C)] = 'i') and
          (Pos(' ' + Copy(C, 1, Length(C) - 1) + ' ', TARGET_STEMS) > 0) then
  begin
    Stem := Copy(C, 1, Length(C) - 1);
    Kind := FORM_RANK;
  end
  else Exit;
  Used := True;
  C := Stem;
  Head := '';
  Tail := Trim(P);
  Form := CMD_PREFIX + Stem;
  case Kind of
    FORM_SLOT: Form := Form + 'id';
    FORM_ROW: Form := Form + 'p';
    else Form := Form + 'i';
  end;
  if Pos(' ' + Stem + ' ', KIND_FIRST) > 0 then Form := Form + ' <kind>';
  case Kind of
    FORM_SLOT: Form := Form + ' <slot>';
    FORM_ROW: Form := Form + ' <row of your last list>';
    else Form := Form + ' <position in the ZitroStats ranking>';
  end;
  if Pos(' ' + Stem + ' ', KIND_FIRST) > 0 then
  begin
    { the kind letter stays in front }
    q := Pos(' ', Tail);
    if q = 0 then
    begin
      Why := 'Usage: ' + Form;
      Exit;
    end;
    Head := Copy(Tail, 1, q - 1) + ' ';
    Tail := Trim(Copy(Tail, q + 1, Length(Tail)));
  end;
  q := Pos(' ', Tail);
  if q > 0 then
  begin
    Num := Copy(Tail, 1, q - 1);
    Tail := Copy(Tail, q, Length(Tail));
  end
  else
  begin
    Num := Tail;
    Tail := '';
  end;
  N := StrToIntDef(Num, -1);
  if Kind = FORM_SLOT then
  begin
    if (N < 1) or (N > 32) then
    begin
      Why := 'Usage: ' + Form;
      Exit;
    end;
    EnterCriticalSection(OnlineCS);
    try
      if OnlineHere[N] then Acc := OnlineProfile[N] else Acc := -2;
    finally LeaveCriticalSection(OnlineCS); end;
    if Acc = -2 then
      Why := 'Nobody plays on slot ' + IntToStr(N) + ' (' + CMD_PREFIX + 'online lists the slots).'
    else if Acc < 0 then
      Why := 'The account of the player on slot ' + IntToStr(N) + ' is not ready yet.'
    else
      P := Head + FORM_MARK + '#' + IntToStr(Acc) + Tail;
  end
  else if N < 1 then
    Why := 'Usage: ' + Form
  else if Kind = FORM_RANK then
    P := Head + FORM_MARK + '^' + IntToStr(N) + Tail
  else
    P := Head + FORM_MARK + '@' + IntToStr(N) + Tail;
end;

{ ---------------- positions of the ZitroStats ranking ----------------

  ZitroStats is asked only while the server has its library loaded, and only a reference for
  the moment of the question is held, so ZitroStats can still be reloaded. }

{$IFDEF WINDOWS}
function AfModuleHandle(Name: PChar): PtrUInt; stdcall; external 'kernel32.dll' name 'GetModuleHandleA';
function AfModuleFileName(M: PtrUInt; Buf: PChar; Size: LongWord): LongWord; stdcall;
  external 'kernel32.dll' name 'GetModuleFileNameA';
{$ENDIF}

{ the file of the ZitroStats library when the server has it loaded, '' when ZitroStats does not run }
function ZitroLibraryPath: string;
{$IFDEF WINDOWS}
var
  M: PtrUInt;
  Buf: array[0..1023] of Char;
  n: LongWord;
begin
  Result := '';
  M := AfModuleHandle('zitrostats_dll.dll');
  if M = 0 then Exit;
  n := AfModuleFileName(M, @Buf[0], SizeOf(Buf));
  if (n > 0) and (n < SizeOf(Buf)) then SetString(Result, PChar(@Buf[0]), n);
end;
{$ELSE}
var
  H: THandle;
  Buf: array[0..4095] of Char;
  n, i, p: Integer;
  S, Part: string;
  L: TStringList;
begin
  Result := '';
  { the libraries of the process, from the kernel (read in pieces: the file has no size) }
  H := FileOpen('/proc/self/maps', fmOpenRead or fmShareDenyNone);
  if H = feInvalidHandle then Exit;
  S := '';
  try
    repeat
      n := FileRead(H, Buf, SizeOf(Buf));
      if n > 0 then
      begin
        SetString(Part, PChar(@Buf[0]), n);
        S := S + Part;
      end;
    until n <= 0;
  finally
    FileClose(H);
  end;
  L := TStringList.Create;
  try
    L.Text := S;
    for i := 0 to L.Count - 1 do
      if Pos('/zitrostats_dll.so', L[i]) > 0 then
      begin
        p := Pos('/', L[i]);
        Result := Trim(Copy(L[i], p, Length(L[i])));
        Break;
      end;
  finally
    L.Free;
  end;
end;
{$ENDIF}

{ the account at a position of the ZitroStats ranking (its !top): name, SteamID and, from
  ZitroStats 5.7 on (API 17), the computer it knows the account by }
function ZitroRankAccount(Rank: Integer; out Name, Steam, Hwid, Why: string): Boolean;
type
  TTopCopy = function(Rank: LongInt; Buf: PChar; Size: LongInt): LongInt; cdecl;
var
  Path, S: string;
  H: TLibHandle;
  F: TTopCopy;
  Buf: array[0..511] of Char;
  n, q: Integer;
begin
  Result := False;
  Name := '';
  Steam := '';
  Hwid := '';
  Why := '';
  n := 0;
  Path := ZitroLibraryPath;
  if Path = '' then
  begin
    Why := 'The positions are those of the ZitroStats ranking, and ZitroStats does not run on this server.';
    Exit;
  end;
  H := LoadLibrary(Path);
  if H = NilHandle then
  begin
    Why := 'ZitroStats could not be asked for its ranking.';
    Exit;
  end;
  try
    { Name, tab, SteamID, tab, computer; an older ZitroStats has only the first two fields }
    F := TTopCopy(GetProcedureAddress(H, 'ZS_API_Top_Player_Copy2'));
    if not Assigned(F) then
      F := TTopCopy(GetProcedureAddress(H, 'ZS_API_Top_Player_Copy'));
    if not Assigned(F) then
    begin
      Why := 'This ZitroStats is too old to be asked for its ranking.';
      Exit;
    end;
    n := F(Rank, @Buf[0], SizeOf(Buf));
  finally
    UnloadLibrary(H);
  end;
  if n <= 0 then
  begin
    Why := 'Nobody is at position ' + IntToStr(Rank) + ' of the ZitroStats ranking.';
    Exit;
  end;
  SetString(S, PChar(@Buf[0]), n);
  q := Pos(#9, S);
  if q = 0 then q := Length(S) + 1;
  Name := Copy(S, 1, q - 1);
  S := Copy(S, q + 1, Length(S));
  { the computer goes up to the next tab: a later API may add fields behind it }
  q := Pos(#9, S);
  if q > 0 then
  begin
    Hwid := Copy(S, q + 1, Length(S));
    S := Copy(S, 1, q - 1);
    q := Pos(#9, Hwid);
    if q > 0 then Hwid := Copy(Hwid, 1, q - 1);
    Hwid := Trim(Hwid);
  end;
  Steam := Trim(S);
  if Steam = '0' then Steam := '';
  Result := Name <> '';
  if not Result then
    Why := 'Nobody is at position ' + IntToStr(Rank) + ' of the ZitroStats ranking.';
end;

{ "^3" among the words (from <command>i 3): the account here of the player at position 3 of
  the ZitroStats ranking, by the SteamID, else by the computer (an account without Steam), else
  by the whole name - then Why says so while the result is True. On a worker. }
function RankTargets(var P: string; var Why: string): Boolean;
var
  W: TStringList;
  i, j, Rank, Acc, First: Integer;
  Name, Steam, Hwid: string;
begin
  Result := True;
  Why := '';
  if Pos(FORM_MARK + '^', P) = 0 then Exit;
  W := TStringList.Create;
  try
    SplitWords(P, W);
    for i := 0 to W.Count - 1 do
    begin
      if (Length(W[i]) < 3) or (Copy(W[i], 1, 2) <> FORM_MARK + '^') then Continue;
      Rank := StrToIntDef(Copy(W[i], 3, Length(W[i])), -1);
      if Rank < 1 then Continue;
      if not ZitroRankAccount(Rank, Name, Steam, Hwid, Why) then
      begin
        Result := False;
        Exit;
      end;
      Acc := -1;
      if Steam <> '' then
      begin
        { the id as ZitroStats keeps it, or in any of the other ways it can be written }
        LockProfiles;
        try
          Acc := ProfileOfSteam(Steam);
          if (Acc < 0) and (SteamToId64(Steam) <> '') then Acc := ProfileOfId64(SteamToId64(Steam));
        finally UnlockProfiles; end;
      end;
      if (Acc < 0) and (Steam = '') and (Hwid <> '') then
      begin
        { the accounts of that computer here: the one with this name, else the first of them
          (a name of its own is no proof, the computer is) }
        LockProfiles;
        try
          j := IndexFirst(HwidIndex, Hwid);
          First := -1;
          while (j >= 0) and (j < HwidIndex.Count) and (HwidIndex[j] = Hwid) do
          begin
            Acc := Integer(PtrInt(HwidIndex.Objects[j]));
            if (Acc >= 0) and (Acc < ProfileCount) then
            begin
              if First < 0 then First := Acc;
              if SameText(GetProfileActiveName(Acc), Trim(Name)) then Break;
            end;
            Acc := -1;
            Inc(j);
          end;
          if Acc < 0 then Acc := First;
        finally UnlockProfiles; end;
      end;
      if Acc < 0 then
      begin
        Acc := AccountNamed(Name);
        if Acc >= 0 then
          Why := Name + ' (position ' + IntToStr(Rank) + ' of the ZitroStats ranking) was found by the name only: ' +
            'the account may be somebody else''s.';
      end;
      if Acc < 0 then
      begin
        Why := Name + ' (position ' + IntToStr(Rank) + ' of the ZitroStats ranking) has no account here.';
        Result := False;
        Exit;
      end;
      W[i] := FORM_MARK + '#' + IntToStr(Acc);
    end;
    P := '';
    for i := 0 to W.Count - 1 do
    begin
      if P <> '' then P := P + ' ';
      P := P + W[i];
    end;
  finally W.Free; end;
end;

{ "@3" among the words: the account in row 3 of the last list this slot got. On a worker - the
  list is copied under the slot's lock. }
function RowTargets(Slot: Integer; var P: string; var Why: string): Boolean;
var
  W: TStringList;
  i, Row: Integer;
  Snap: TSearchResult;
  Taken: Boolean;
begin
  Result := True;
  Why := '';
  if Pos(FORM_MARK + '@', P) = 0 then Exit;
  W := TStringList.Create;
  try
    SplitWords(P, W);
    Taken := False;
    for i := 0 to W.Count - 1 do
    begin
      if (Length(W[i]) < 3) or (Copy(W[i], 1, 2) <> FORM_MARK + '@') then Continue;
      Row := StrToIntDef(Copy(W[i], 3, Length(W[i])), -1);
      if Row < 1 then Continue;
      if not Taken then
      begin
        Snap := ResultSnapshot(Slot);
        Taken := True;
      end;
      if (Snap.PlayerID <> Slot) or (Row > Snap.ResultCount) or (Row > Length(Snap.Results)) then
      begin
        if (Snap.PlayerID <> Slot) or (Snap.ResultCount = 0) then
          Why := 'You have no list to pick a row from: ask for one first (' + CMD_PREFIX +
                 'af_search, ' + CMD_PREFIX + 'lastvisits ...).'
        else
          Why := 'Your last list has ' + IntToStr(Snap.ResultCount) + ' rows, not ' + IntToStr(Row) + '.';
        Result := False;
        Exit;
      end;
      if (Snap.TaskType = TASK_VISIT_STATS) or (Snap.TaskType = TASK_MULTI_TOP) or
         (Snap.Results[Row - 1].ProfileIndex < 0) then
      begin
        Why := 'The rows of your last list are not accounts.';
        Result := False;
        Exit;
      end;
      W[i] := FORM_MARK + '#' + IntToStr(Snap.Results[Row - 1].ProfileIndex);
    end;
    P := '';
    for i := 0 to W.Count - 1 do
    begin
      if P <> '' then P := P + ' ';
      P := P + W[i];
    end;
  finally W.Free; end;
end;
