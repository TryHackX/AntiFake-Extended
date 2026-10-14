{ ---------------------------------------------------------------------------------------
  AntiFake Extended - the one door the script knocks on.

  Run_Command takes the slot, the word the player typed and its arguments, and either fills
  the slot's line buffer with the answer or starts a search whose answer comes later. The
  script never decides anything: it only prints what it finds in the buffer.
  --------------------------------------------------------------------------------------- }

{ the heavy commands: the ones that read every profile }
function IsHeavyCommand(const Cmd: string): Boolean;
const
  List = ' search searchall smartsearch advancedsearch checkp checkd listprofiles geosearch' +
         ' visitstats visitsof whofrom multi multiaccounts locateall showlocate' +
         ' status statusa mystatus lastvisits lastvisitors ';
begin
  Result := Pos(' ' + Cmd + ' ', List) > 0;
end;

{ start a search and tell the script the answer comes later; a search of this slot that is
  still running keeps its turn, and the player hears so instead of getting its old answer }
function StartTaskFor(Slot, TaskType, Kind, SortMethod, Target, MaxResults: Integer;
  const Phrase: string; SearchAll, Partial, FFirst, FLast, FAll: Boolean): Integer;
begin
  ViewPage[Slot] := 1;
  if not StartSearch(Slot, TaskType, Kind, SortMethod, Target, MaxResults, Phrase,
    SearchAll, Partial, FFirst, FLast, FAll) then
  begin
    if ShuttingDown then
      Say(Slot, CLR_WARN, 'The server is shutting down.')
    else
      Say(Slot, CLR_WARN, 'Your last search is still running; ask again when it has answered.');
    Result := CMD_ANSWERED;
    Exit;
  end;
  { how its table is to be drawn goes with this search }
  BindView(Slot);
  CmdStartedTask[Slot] := True;
  Result := CMD_TASK;
end;

{ ServerWide: /lastvisits and /lastvisitors - the newest visits of everybody, or one row per
  account, at most LastVisitsRows rows. A player named after them gets that player's visits.
  Simple: the list of visitors without addresses and places (!lastvisitors); a player gets no
  filter of it either - which of them came through a VPN is a place too. }
function RunVisits(Slot: Integer; const Params: string; IsAdmin: Boolean;
  Unique, StatsOnly, ServerWide, Simple: Boolean): Integer;
var
  W: TStringList;
  i, Idx, Kind, Sort, TaskType, Rows: Integer;
  Target, Range, Word1, Why: string;
  Masked, Clean, ByCountry: Boolean;
begin
  Result := CMD_ANSWERED;
  Why := '';
  W := TStringList.Create;
  try
    SplitWords(Trim(Params), W);
    Target := '';
    Range := '';
    Masked := False;
    Clean := False;
    ByCountry := False;
    Sort := SORT_LATEST_DOWN;
    for i := 0 to W.Count - 1 do
    begin
      Word1 := LowerCase(W[i]);
      if Word1 = 'masked' then Masked := True
      else if Word1 = 'clean' then Clean := True
      else if Word1 = 'bycountry' then ByCountry := True
      else if Word1 = 'byip' then ByCountry := False
      { nothing lists the visits by name; the word used to take the place of the range that
        came before it ("7d byname" was every visit) }
      else if Word1 = 'byname' then Continue
      else if Pos('sort:', Word1) = 1 then
      begin
        Kind := ParseSortWord(Copy(Word1, 6, Length(Word1)));
        if Kind >= 0 then Sort := Kind;
      end
      else if IsVisitKeyword(Word1) then Range := Word1
      else
      begin
        { every other word is a part of the name ("zz two" was "zz"), and a name in double
          quotes is one word ("all" in quotes is a player) }
        if Target <> '' then Target := Target + ' ';
        Target := Target + W[i];
      end;
    end;

    Rows := 0;
    if StatsOnly then
    begin
      Idx := -1;
      TaskType := TASK_VISIT_STATS;
      Kind := 0;
    end
    else if ServerWide and (Target = '') then
    begin
      Idx := -1;
      Rows := Settings.LastVisitsRows;
      if Unique then
      begin
        TaskType := TASK_UNIQUE_VISITS;
        Kind := 2;
        if Simple then
        begin
          NextView[Slot].Simple := True;
          if not IsAdmin then
          begin
            Masked := False;
            Clean := False;
          end;
        end;
        Say(Slot, CLR_INFO, 'Who came to the server, newest first (at most ' + IntToStr(Rows) + ')...');
      end
      else
      begin
        TaskType := TASK_VISITS;
        Kind := 0;
        Say(Slot, CLR_INFO, 'The newest visits of the server (at most ' + IntToStr(Rows) + ')...');
      end;
    end
    else
    begin
      if not TargetOf(Slot, Target, IsAdmin, Idx, Why) then
      begin
        Say(Slot, CLR_WARN, Why);
        Exit;
      end;
      if Unique then TaskType := TASK_UNIQUE_VISITS else TaskType := TASK_VISITS;
      if ByCountry then Kind := 1 else Kind := 0;
    end;
    Result := StartTaskFor(Slot, TaskType, Kind, Sort, Idx, Rows, Range,
      False, False, Masked, Clean, False);
  finally W.Free; end;
end;

function RunMulti(Slot: Integer; const Params: string): Integer;
var
  Word1, Rest: string;
  Idx, Sort: Integer;
begin
  Result := CMD_ANSWERED;
  Rest := Trim(Params);
  { a word of its own (stats, top, strong, a sort) says what to list; a longer text is a name -
    "Top Gun" used to list the computers - and a name in double quotes is always one ("top") }
  if Pos(' ', Rest) > 0 then Word1 := '' else Word1 := LowerCase(Rest);

  if (Word1 = 'stats') or (Word1 = 'summary') then
  begin
    Say(Slot, CLR_INFO, 'Multi accounts: ' + MultiStatsText);
    Exit;
  end;
  if (Word1 = 'top') or (Word1 = 'topcomputers') or (Word1 = 'toppc') then
  begin
    Say(Slot, CLR_INFO, 'The computers that the most accounts have played from...');
    Result := StartTaskFor(Slot, TASK_MULTI_TOP, 0, SORT_COUNT_DOWN, -1, 0, '',
      False, False, False, False, False);
    Exit;
  end;
  if (Word1 = 'topaddresses') or (Word1 = 'topip') or (Word1 = 'topaddr') then
  begin
    Say(Slot, CLR_INFO, 'The addresses that the most accounts have played from...');
    Result := StartTaskFor(Slot, TASK_MULTI_TOP, 1, SORT_COUNT_DOWN, -1, 0, '',
      False, False, False, False, False);
    Exit;
  end;
  if (Word1 = 'strong') or (Word1 = 'report') then
  begin
    Say(Slot, CLR_INFO, 'Accounts worth a look (' + IntToStr(Settings.MultiReportScore) +
        ' points or more)...');
    Result := StartTaskFor(Slot, TASK_MULTI, 1, SORT_ID_UP, -1, 0, '',
      False, False, False, False, False);
    Exit;
  end;
  if Rest <> '' then
  begin
    Sort := ParseSortWord(Word1);
    if Sort >= 0 then
    begin
      Say(Slot, CLR_INFO, 'Looking for accounts that share a computer or an address...');
      Result := StartTaskFor(Slot, TASK_MULTI, 0, Sort, -1, 0, '',
        False, False, False, False, False);
      Exit;
    end;
    Idx := ResolveProfileFor(Slot, Rest);
    if Idx < 0 then
    begin
      Say(Slot, CLR_ERROR, 'No account found for: ' + ShownText(Rest));
      Exit;
    end;
    ViewMultiOf(Slot, Idx);
    Exit;
  end;
  Say(Slot, CLR_INFO, 'Looking for accounts that share a computer or an address...');
  Result := StartTaskFor(Slot, TASK_MULTI, 0, SORT_ID_UP, -1, 0, '',
    False, False, False, False, False);
end;

function RunMultiSet(Slot: Integer; const Params, Who: string): Integer;
var
  W: TStringList;
  A, B, V: Integer;
  Word3: string;
begin
  Result := CMD_ANSWERED;
  W := TStringList.Create;
  try
    SplitWords(Trim(Params), W);
    if W.Count < 3 then
    begin
      Say(Slot, CLR_WARN, 'Usage: ' + CMD_PREFIX + 'multiset <id> <id> same|family|ignore|clear');
      Exit;
    end;
    A := AccountNumberOf(W[0]);
    B := AccountNumberOf(W[1]);
    Word3 := LowerCase(W[2]);
    if Word3 = 'same' then V := VERDICT_SAME
    else if (Word3 = 'family') or (Word3 = 'house') then V := VERDICT_FAMILY
    else if Word3 = 'ignore' then V := VERDICT_IGNORE
    else if (Word3 = 'clear') or (Word3 = 'none') then V := VERDICT_NONE
    else
    begin
      Say(Slot, CLR_WARN, 'Say one of: same, family, ignore, clear.');
      Exit;
    end;
    if (A < 0) or (B < 0) or (A = B) then
    begin
      Say(Slot, CLR_WARN, 'Two different account numbers, please.');
      Exit;
    end;
    if Set_Multi_Verdict(A, B, V, PChar(Who)) then
    begin
      if V = VERDICT_NONE then
        Say(Slot, CLR_INFO, 'Cleared what was said about #' + IntToStr(A) + ' and #' + IntToStr(B) + '.')
      else
        Say(Slot, CLR_INFO, '#' + IntToStr(A) + ' and #' + IntToStr(B) + ': ' + VerdictText(V) + '.');
    end
    else
      Say(Slot, CLR_ERROR, 'That pair could not be written down.');
  finally W.Free; end;
end;

function RunSmartSearch(Slot: Integer; const Params: string): Integer;
var
  W: TStringList;
  i, Algo, MinLevel: Integer;
  Phrase, Word1: string;
  SearchAll, GotLevel: Boolean;
begin
  Result := CMD_ANSWERED;
  W := TStringList.Create;
  try
    SplitWords(Trim(Params), W);
    if W.Count = 0 then
    begin
      Say(Slot, CLR_INFO, '=== Fuzzy name search ===');
      Say(Slot, CLR_MAIN, 'Usage: ' + CMD_PREFIX + 'smartsearch [lev|jaro|token] [min %] [all|actual] <name>');
      Say(Slot, CLR_DATA, '  lev   - typos and changed letters (the default)');
      Say(Slot, CLR_DATA, '  jaro  - short names and common beginnings');
      Say(Slot, CLR_DATA, '  token - the same words in another order');
      Exit;
    end;
    Algo := 0;
    MinLevel := 80;
    SearchAll := False;
    GotLevel := False;
    Phrase := '';
    i := 0;
    if i < W.Count then
    begin
      Word1 := LowerCase(W[i]);
      if Word1 = 'jaro' then begin Algo := 1; Inc(i); end
      else if (Word1 = 'token') or (Word1 = 'smart') then begin Algo := 2; Inc(i); end
      else if Word1 = 'lev' then begin Algo := 0; Inc(i); end;
    end;
    if i < W.Count then
      if (StrToIntDef(W[i], -1) >= 0) and (StrToIntDef(W[i], -1) <= 100) then
      begin
        MinLevel := StrToInt(W[i]);
        GotLevel := True;
        Inc(i);
      end;
    if i < W.Count then
    begin
      Word1 := LowerCase(W[i]);
      if Word1 = 'all' then begin SearchAll := True; Inc(i); end
      else if Word1 = 'actual' then begin SearchAll := False; Inc(i); end;
    end;
    while i < W.Count do
    begin
      if Phrase <> '' then Phrase := Phrase + ' ';
      Phrase := Phrase + W[i];
      Inc(i);
    end;
    { a name in double quotes is the name, even one that looks like a method or a number }
    Phrase := UnquotedText(Phrase);
    if Phrase = '' then
    begin
      Say(Slot, CLR_WARN, 'Type a name or a part of it.');
      Exit;
    end;
    if not GotLevel then MinLevel := 80;
    { the method by the name the usage gives it, not by a number nobody was told }
    case Algo of
      1: Word1 := 'jaro';
      2: Word1 := 'token';
    else Word1 := 'lev';
    end;
    Say(Slot, CLR_INFO, 'Fuzzy search: "' + Phrase + '" (' + Word1 + ', at least ' +
        IntToStr(MinLevel) + '%)...');
    Result := StartTaskFor(Slot, TASK_ADVANCED_SEARCH, Algo, MinLevel, -1, 50, Phrase,
      SearchAll, False, False, False, False);
  finally W.Free; end;
end;

{ /visitsof: the search reads its first word as the country, so a country of several words
  ("United Kingdom", or one in double quotes) is looked up here and handed over as its code; the
  words of a range stay behind it. Shown is the country as the asker reads it; Known is False
  for a name of several words that no place located so far has. }
function CountryPhrase(const Params: string; out Shown: string; out Known: Boolean): string;
var
  W: TStringList;
  k, g: Integer;
  Country, Ranges, Code: string;
  Two: Boolean;
begin
  Country := '';
  Ranges := '';
  Known := True;
  W := TStringList.Create;
  try
    SplitWords(Trim(Params), W);
    for k := 0 to W.Count - 1 do
      if IsVisitKeyword(LowerCase(W[k])) then Ranges := Ranges + ' ' + W[k]
      else
      begin
        if Country <> '' then Country := Country + ' ';
        Country := Country + UnquotedText(W[k]);
      end;
  finally W.Free; end;
  Shown := Country;
  Result := Country + Ranges;
  if Pos(' ', Country) = 0 then Exit;
  { the code of the country of that name: the whole name first, else the one country that
    holds it }
  Code := '';
  Two := False;
  if GrabLock(GeoCritSec, DrawWait(1000)) then
  try
    for g := 0 to GeoCount - 1 do
      if SameText(string(GeoCache[g].Country), Country) then
      begin
        Code := string(GeoCache[g].CountryCode);
        Break;
      end;
    if Code = '' then
      for g := 0 to GeoCount - 1 do
        if ContainsText(string(GeoCache[g].Country), Country) then
        begin
          if (Code <> '') and not SameText(Code, string(GeoCache[g].CountryCode)) then Two := True;
          Code := string(GeoCache[g].CountryCode);
        end;
  finally LeaveCriticalSection(GeoCritSec); end;
  if (Length(Code) <> 2) or Two then
  begin
    Known := False;
    Exit;
  end;
  Result := UpperCase(Code) + Ranges;
  Shown := Country + ' (' + UpperCase(Code) + ')';
end;

{ ---------------- the door ---------------- }

{ Everything below runs on a worker thread. Nothing in here may be called from the game
  thread: a name that is looked up by hand, a directory that is read, a file that is copied -
  any of them would sit in the middle of a server tick. Chat: the command was typed in the chat
  with the ! of the players (the script says so). }
function DispatchCommand(Slot: Integer; const Cmd, Params, Who: string;
  IsAdmin, Chat: Boolean): Integer;
var
  C, P, Why, Word1, Rest: string;
  Idx, Kind, Sort, q, k, Ties: Integer;
  W: TStringList;
  Desc, Known: Boolean;
begin
  C := Cmd;
  P := Params;
  Why := '';
  Idx := -1;
  OutClear(Slot);
  Result := CMD_ANSWERED;
  if (Slot >= 0) and (Slot <= MAX_SLOTS) then ViewerAdmin[Slot] := IsAdmin;
  ClearNextView(Slot);

  { ---- the pages of the last list ---- }
  if C = 'next' then
  begin
    RenderPage(Slot, ViewPage[Slot] + 1);
    Exit;
  end;
  if C = 'prev' then
  begin
    RenderPage(Slot, ViewPage[Slot] - 1);
    Exit;
  end;
  if C = 'page' then
  begin
    RenderPage(Slot, StrToIntDef(P, 1));
    Exit;
  end;

  if (C = 'antifake') or (C = 'help') then
  begin
    ViewHelp(Slot, IsAdmin);
    Exit;
  end;

  { <command>p <row> and <command>i <position>: the account in that row of the last list, or
    at that position of the ZitroStats ranking (ExpandTargetForm) }
  if Pos(' ' + C + ' ', ' visits uniquevisits uvisits locations statusa status steam multi' +
     ' multiaccounts locate privacy privacyall checkp showlocate ') > 0 then
  begin
    if not RowTargets(Slot, P, Why) then
    begin
      Say(Slot, CLR_WARN, Why);
      Exit;
    end;
    if not RankTargets(P, Why) then
    begin
      Say(Slot, CLR_WARN, Why);
      Exit;
    end;
    { taken by the name only }
    if Why <> '' then Say(Slot, CLR_WARN, Why);
  end;

  { ---- what somebody is ---- }
  { a text in double quotes is looked for as it stands, quotes left out }
  if C = 'search' then
  begin
    Rest := UnquotedText(P);
    if Rest = '' then begin Say(Slot, CLR_WARN, 'Usage: ' + CMD_PREFIX + 'af_search <name>'); Exit; end;
    Say(Slot, CLR_INFO, 'Looking for ' + Rest + '...');
    Result := StartTaskFor(Slot, TASK_SEARCH, 0, SORT_ID_UP, -1, 0, Rest, False, True,
      False, False, False);
    Exit;
  end;
  if C = 'searchall' then
  begin
    Rest := UnquotedText(P);
    if Rest = '' then begin Say(Slot, CLR_WARN, 'Usage: ' + CMD_PREFIX + 'searchall <name>'); Exit; end;
    Say(Slot, CLR_INFO, 'Looking for ' + Rest + ' in every name ever used...');
    Result := StartTaskFor(Slot, TASK_SEARCHID, 0, SORT_ID_UP, -1, 0, Rest, True, True,
      False, False, False);
    Exit;
  end;
  if (C = 'smartsearch') or (C = 'advancedsearch') then
  begin
    Result := RunSmartSearch(Slot, P);
    Exit;
  end;
  if (C = 'statusa') or (C = 'status') or (C = 'mystatus') then
  begin
    if C = 'mystatus' then P := '';
    if not TargetOf(Slot, P, IsAdmin, Idx, Why) then
    begin
      Say(Slot, CLR_WARN, Why);
      Exit;
    end;
    { the ties of an account are what /multi says: PlayerMulti decides who sees them, not
      PlayerStatus (a player saw the accounts of strangers from the same school address) }
    if IsAdmin then Ties := STATUS_TIES_ALL
    else if (Settings.CmdMulti = 2) or ((Settings.CmdMulti = 1) and (Idx = OnlineProfileOf(Slot))) then
      Ties := STATUS_TIES_KEYS
    else
      Ties := STATUS_TIES_NONE;
    Result := StartTaskFor(Slot, TASK_STATUSA, Ties, 0, Idx, 0, '', False, False,
      False, False, False);
    Exit;
  end;
  if (C = 'privacy') or (C = 'privacyall') or (C = 'mynames') then
  begin
    if C = 'mynames' then
    begin
      Kind := KIND_NAME;
      Rest := '';
      Sort := ParseSortWord(P);
      if Sort < 0 then Sort := SORT_ID_UP;
      Idx := OnlineProfileOf(Slot);
      if Idx < 0 then
      begin
        Say(Slot, CLR_WARN, 'Your account is not ready yet.');
        Exit;
      end;
    end
    else
    begin
      W := TStringList.Create;
      try
        SplitWords(P, W);
        if W.Count < 1 then
        begin
          Say(Slot, CLR_WARN, 'Usage: ' + CMD_PREFIX + C + ' <n|s|h|i> <name/#id> [sort]');
          Exit;
        end;
        Kind := ParseKindLetter(W[0]);
        if Kind < 0 then
        begin
          Say(Slot, CLR_WARN, 'The kind is one of n (name), s (steam), h (hwid), i (ip).');
          Exit;
        end;
        Rest := '';
        Sort := SORT_ID_UP;
        { the sort comes last (/privacy n Kowal count), behind a name; a word alone is the name,
          also one that looks like a sort ("count"), and a name in double quotes is always one.
          Every word that looked like a sort was taken for one, wherever it stood: "Mr Count"
          was "Mr". }
        q := W.Count - 1;
        if (q >= 2) and (ParseSortWord(W[q]) >= 0) then
        begin
          Sort := ParseSortWord(W[q]);
          Dec(q);
        end;
        for k := 1 to q do
        begin
          if Rest <> '' then Rest := Rest + ' ';
          Rest := Rest + W[k];
        end;
      finally W.Free; end;
      if not TargetOf(Slot, Rest, IsAdmin, Idx, Why) then
      begin
        Say(Slot, CLR_WARN, Why);
        Exit;
      end;
    end;
    if C = 'privacyall' then q := TASK_PRIVACYID else q := TASK_PRIVACY;
    Result := StartTaskFor(Slot, q, Kind, Sort, Idx, 200, '', False, False,
      False, False, False);
    Exit;
  end;
  if C = 'checkd' then
  begin
    W := TStringList.Create;
    try
      SplitWords(P, W);
      if W.Count < 2 then
      begin
        Say(Slot, CLR_WARN, 'Usage: ' + CMD_PREFIX + 'checkd <n|s|h|i> <value>');
        Exit;
      end;
      Kind := ParseKindLetter(W[0]);
      if Kind < 0 then
      begin
        Say(Slot, CLR_WARN, 'The kind is one of n, s, h, i.');
        Exit;
      end;
      { a value in double quotes is looked for without them }
      Rest := UnquotedText(Copy(P, Pos(' ', P) + 1, Length(P)));
    finally W.Free; end;
    if Rest = '' then
    begin
      Say(Slot, CLR_WARN, 'Usage: ' + CMD_PREFIX + 'checkd <n|s|h|i> <value>');
      Exit;
    end;
    Say(Slot, CLR_INFO, 'Looking for that ' + LowerCase(KindName(Kind)) + '...');
    Result := StartTaskFor(Slot, TASK_CHECKD, Kind, SORT_ID_UP, -1, 0, Rest,
      False, True, False, False, False);
    Exit;
  end;
  if C = 'checkp' then
  begin
    W := TStringList.Create;
    try
      SplitWords(P, W);
      if W.Count < 2 then
      begin
        Say(Slot, CLR_WARN, 'Usage: ' + CMD_PREFIX + 'checkp <n|s|h|i> <name/#id>');
        Exit;
      end;
      Kind := ParseKindLetter(W[0]);
      { a kind that is none of them was taken for a name ("/checkp computer Kowal" compared
        names), where /checkd says so }
      if Kind < 0 then
      begin
        Say(Slot, CLR_WARN, 'The kind is one of n, s, h, i.');
        Exit;
      end;
      Rest := Trim(Copy(P, Pos(' ', P) + 1, Length(P)));
    finally W.Free; end;
    Idx := ResolveProfileFor(Slot, Rest);
    if Idx < 0 then
    begin
      Say(Slot, CLR_ERROR, 'No account found for: ' + ShownText(Rest));
      Exit;
    end;
    Say(Slot, CLR_INFO, 'Comparing account #' + IntToStr(Idx) + ' with the others...');
    Result := StartTaskFor(Slot, TASK_CHECKP, Kind, SORT_ID_UP, Idx, 0, '',
      False, False, False, False, False);
    Exit;
  end;
  if C = 'listprofiles' then
  begin
    { the search gives the accounts by number; the order asked for is made before the first
      page is drawn (PrepareList). "count" and "first" of before sorted by what the rows of
      this list never held, and came back in no order at all. }
    if not ParseListSort(P, Kind, Desc, Why) then
    begin
      Say(Slot, CLR_WARN, Why);
      Say(Slot, CLR_DATA, 'Usage: ' + CMD_PREFIX + 'listprofiles [sort] [asc|desc] - a sort alone puts the ' +
          'most or the newest first');
      Say(Slot, CLR_DATA, 'sort = ' + LIST_SORT_WORDS);
      Exit;
    end;
    NextView[Slot].ListKey := Kind;
    NextView[Slot].ListDesc := Desc;
    Say(Slot, CLR_INFO, 'All accounts, ' + ListOrderText(Kind, Desc) + '...');
    Result := StartTaskFor(Slot, TASK_LIST, 0, SORT_ID_UP, -1, 0, '', False, False,
      False, False, False);
    Exit;
  end;
  if (C = 'online') or (C = 'listprofilesonline') or (C = 'listonline') or
     (C = 'showid') or (C = 'showprofileid') then
  begin
    ViewOnline(Slot, IsAdmin);
    Exit;
  end;

  { ---- visits and places ---- }
  if (C = 'visits') then begin Result := RunVisits(Slot, P, IsAdmin, False, False, False, False); Exit; end;
  if (C = 'uniquevisits') or (C = 'uvisits') then
  begin
    Result := RunVisits(Slot, P, IsAdmin, True, False, False, False);
    Exit;
  end;
  if C = 'locations' then
  begin
    { in front: a name in quotes that is never closed takes every word behind it }
    Result := RunVisits(Slot, 'bycountry ' + P, IsAdmin, True, False, False, False);
    Exit;
  end;
  if C = 'lastvisits' then
  begin
    Result := RunVisits(Slot, P, IsAdmin, False, False, True, False);
    Exit;
  end;
  if C = 'lastvisitors' then
  begin
    { the players get it without addresses and places, and so does an admin who asks in the
      chat; /lastvisitors of an admin is the whole table }
    Result := RunVisits(Slot, P, IsAdmin, True, False, True, Chat or not IsAdmin);
    Exit;
  end;
  if (C = 'visitstats') or (C = 'visitmetrics') then
  begin
    Result := RunVisits(Slot, P, IsAdmin, False, True, False, False);
    Exit;
  end;
  if (C = 'visitsof') or (C = 'whofrom') then
  begin
    Rest := CountryPhrase(P, Word1, Known);
    if Word1 = '' then
    begin
      Say(Slot, CLR_WARN, 'Usage: ' + CMD_PREFIX + 'visitsof <country or ISO code> [7d|30d|all]');
      Say(Slot, CLR_DATA, 'For example: ' + CMD_PREFIX + 'visitsof PL 30d, or ' +
          CMD_PREFIX + 'visitsof Poland');
      Exit;
    end;
    if not Known then
    begin
      Say(Slot, CLR_WARN, 'Nobody here came from ' + Word1 + ' (no place located so far has that name).');
      Exit;
    end;
    Say(Slot, CLR_INFO, 'Looking for the players who come from ' + Word1 + '...');
    Result := StartTaskFor(Slot, TASK_VISITS_OF, 0, SORT_COUNT_DOWN, -1, 0, Rest,
      False, False, False, False, False);
    Exit;
  end;
  if C = 'locate' then
  begin
    { an admin without a name sees everybody; a player without a name sees themselves (and
      with PlayerLocate = self the name was taken away before this point) }
    if P <> '' then ViewLocate(Slot, P, LOCATE_NAME, IsAdmin)
    else if IsAdmin then ViewLocate(Slot, '', LOCATE_ALL, IsAdmin)
    else ViewLocate(Slot, '', LOCATE_SELF, IsAdmin);
    Exit;
  end;
  if C = 'locateall' then
  begin
    ViewLocate(Slot, '', LOCATE_ALL, IsAdmin);
    Exit;
  end;
  if C = 'showlocate' then
  begin
    W := TStringList.Create;
    try
      SplitWords(P, W);
      if W.Count < 2 then
      begin
        Say(Slot, CLR_WARN, 'Usage: ' + CMD_PREFIX + 'showlocate <n|s|h|i> <name/#id>');
        Exit;
      end;
      Kind := ParseKindLetter(W[0]);
      { the same for a kind that is none of them, which was taken for an address }
      if Kind < 0 then
      begin
        Say(Slot, CLR_WARN, 'The kind is one of n, s, h, i.');
        Exit;
      end;
      Rest := Trim(Copy(P, Pos(' ', P) + 1, Length(P)));
    finally W.Free; end;
    Idx := ResolveProfileFor(Slot, Rest);
    if Idx < 0 then
    begin
      Say(Slot, CLR_ERROR, 'No account found for: ' + ShownText(Rest));
      Exit;
    end;
    case Kind of
      KIND_NAME: Rest := GetProfileActiveNameSafe(Idx);
      KIND_STEAM: Rest := GetProfileActiveSteamSafe(Idx);
      KIND_HWID: Rest := GetProfileActiveHWIDSafe(Idx);
    else Rest := GetProfileActiveIPSafe(Idx);
    end;
    Say(Slot, CLR_INFO, 'The places of account #' + IntToStr(Idx) + ' and of every account with the same ' +
        LowerCase(KindName(Kind)) + '...');
    Result := StartTaskFor(Slot, TASK_LOCATE_ALL, Kind, SORT_ID_UP, Idx, 0, Rest,
      False, False, False, False, False);
    Exit;
  end;
  if C = 'geosearch' then
  begin
    if P = '' then
    begin
      Say(Slot, CLR_WARN, 'Usage: ' + CMD_PREFIX + 'geosearch <city, region, country or its code>');
      Say(Slot, CLR_DATA, 'Also: isp:<text> dns:<text> zip:<text> proxy:1 hosting:1 mobile:1, ' +
          'e.g. ' + CMD_PREFIX + 'geosearch pl proxy:1');
      Exit;
    end;
    Say(Slot, CLR_INFO, 'Looking for players in ' + P + '...');
    Result := StartTaskFor(Slot, TASK_GEOSEARCH, 0, SORT_ID_UP, -1, 0, P,
      False, True, False, False, False);
    Exit;
  end;

  { ---- Steam ---- }
  if (C = 'steam') or (C = 'mysteam') then
  begin
    if C = 'mysteam' then P := '';
    if not TargetOf(Slot, P, IsAdmin, Idx, Why) then
    begin
      Say(Slot, CLR_WARN, Why);
      Exit;
    end;
    { asking here as well means a key put in after the player joined still works }
    Rest := GetProfileActiveSteamSafe(Idx);
    if Rest <> '' then Steam_Note(PChar(Rest));
    ViewSteam(Slot, Idx);
    Exit;
  end;
  if C = 'steamstatus' then
  begin
    if not IsAdmin then
    begin
      Say(Slot, CLR_WARN, 'Only admins can see this.');
      Result := CMD_REFUSED;
      Exit;
    end;
    Say(Slot, CLR_INFO, 'Steam: ' + SteamStatusText);
    Exit;
  end;

  { ---- accounts that look like one person ---- }
  if (C = 'multi') or (C = 'multiaccounts') then
  begin
    Result := RunMulti(Slot, P);
    Exit;
  end;
  if (C = 'multiset') or (C = 'setmulti') then
  begin
    if not IsAdmin then
    begin
      Say(Slot, CLR_WARN, 'Only admins can do this.');
      Result := CMD_REFUSED;
      Exit;
    end;
    Result := RunMultiSet(Slot, P, Who);
    Exit;
  end;

  { ---- the database ---- }
  if (C = 'storage') or (C = 'afstorage') then
  begin
    if not IsAdmin then
    begin
      Say(Slot, CLR_WARN, 'Only admins can see this.');
      Result := CMD_REFUSED;
      Exit;
    end;
    Word1 := LowerCase(Trim(P));
    if (Word1 = 'save') or (Word1 = 'now') then
    begin
      Result := CMD_TASK;      { the script writes; it knows the path and the times }
      Exit;
    end;
    ViewStorage(Slot, Word1);
    Exit;
  end;

  Result := CMD_UNKNOWN;
end;

{ ---------------- the door ---------------- }

type
  TCommandThread = class(TThread)
  private
    FSlot: Integer;
    FCmd, FParams, FWho: string;
    FAdmin, FChat: Boolean;
  protected
    procedure Execute; override;
  public
    constructor Create(ASlot: Integer; const ACmd, AParams, AWho: string; AAdmin, AChat: Boolean);
  end;

constructor TCommandThread.Create(ASlot: Integer; const ACmd, AParams, AWho: string;
  AAdmin, AChat: Boolean);
begin
  inherited Create(True);
  FSlot := ASlot;
  FCmd := ACmd;
  FParams := AParams;
  FWho := AWho;
  FAdmin := AAdmin;
  FChat := AChat;
end;

procedure TCommandThread.Execute;
begin
  GuardWorkerThread;
  { the slot is free again whatever happens here: a slot left busy took no command any more
    until its player left }
  try
    try
      { "#render": the table of a finished search (Render_Result) }
      if FCmd = '#render' then
        RenderResult(FSlot)
      else
        DispatchCommand(FSlot, FCmd, FParams, FWho, FAdmin, FChat);
    except
      on E: Exception do
      begin
        OutClear(FSlot);
        Say(FSlot, CLR_ERROR, 'Something went wrong answering that: ' + E.Message);
      end;
    end;
  finally
    EnterCriticalSection(CmdCS);
    try
      CmdBusy[FSlot] := False;
    finally LeaveCriticalSection(CmdCS); end;
  end;
end;

{ the sign a command was typed with: "!" in the chat, "/" otherwise }
function PrefixOf(Chat: Boolean): string;
begin
  if Chat then Result := '!' else Result := CMD_PREFIX;
end;

{ What the script calls, on the game thread. Everything here is a string compare or a number:
  is it one of ours, may this player ask it, and is it too soon after the last one. The work
  itself goes to a thread, and the script picks the lines up on a later tick. }
function RunCommandOn(Slot: Integer; Cmd, Params: PChar; IsAdmin: Boolean;
  Who: PChar): Integer; forward;

function Run_Command(Slot: Integer; Cmd, Params: PChar; IsAdmin: Boolean;
  Who: PChar): Integer; cdecl;
begin
  try
    Result := RunCommandOn(Slot, Cmd, Params, IsAdmin, Who);
  except
    ExportFailed('Run_Command');
    { what went wrong is not the script's command: it is left alone, nothing waits for it }
    Result := CMD_UNKNOWN;
  end;
end;

function RunCommandOn(Slot: Integer; Cmd, Params: PChar; IsAdmin: Boolean;
  Who: PChar): Integer;
var
  C, P, Why, FormWhy, Typed, Prefix: string;
  Scope: Integer;
  FormUsed, Chat: Boolean;
begin
  Result := CMD_UNKNOWN;
  FormUsed := False;
  FormWhy := '';
  Why := '';
  if (Slot < 1) or (Slot > MAX_SLOTS) then Exit;
  C := LowerCase(Trim(string(Cmd)));
  P := Trim(string(Params));
  { a command typed in the chat comes with its "!", the form of the players }
  Chat := Copy(C, 1, 1) = '!';
  if Chat then Delete(C, 1, 1);
  if C = '' then Exit;
  Typed := C;
  ExpandTargetForm(C, P, FormUsed, FormWhy);
  if not IsKnownCommand(C) then Exit;
  C := InternalName(C);

  if ScopeOfCommand(C) = 0 then
    if not IsAdmin then
    begin
      OutClear(Slot);
      Say(Slot, CLR_WARN, 'That command is for admins on this server.');
      Result := CMD_REFUSED;
      Exit;
    end;

  { a player without admin: the reach their settings allow }
  if not IsAdmin then
  begin
    Scope := ScopeOfCommand(C);
    if Scope < 2 then
    begin
      if NeedsFullScope(C) then
      begin
        OutClear(Slot);
        Say(Slot, CLR_WARN, 'On this server that one is for admins.');
        Result := CMD_REFUSED;
        Exit;
      end;
      { about themselves only: a player named here is refused, not quietly swapped for the
        one who asks }
      if FormUsed or
         (((C = 'visits') or (C = 'uniquevisits') or (C = 'uvisits') or (C = 'locations')) and
          NamesTarget(P)) then
      begin
        OutClear(Slot);
        Say(Slot, CLR_WARN, 'On this server you can ask only about yourself: ' + PrefixOf(Chat) + C +
            ' without a name.');
        Result := CMD_REFUSED;
        Exit;
      end;
      if (C <> 'antifake') and (C <> 'help') and (C <> 'next') and (C <> 'prev') and
         (C <> 'page') and (C <> 'visitstats') and (C <> 'visitmetrics') and (C <> 'mynames') and
         (C <> 'visits') and (C <> 'uniquevisits') and (C <> 'uvisits') and (C <> 'locations') then
        P := '';
    end;
    { !lastvisitors lists the server by the reach of PlayerLastVisitors; a player named in it
      means that player's visits, which is the reach of PlayerVisits }
    if (C = 'lastvisitors') and (Settings.CmdVisits < 2) and NamesTarget(P) then
    begin
      OutClear(Slot);
      Say(Slot, CLR_WARN, 'On this server you can ask only about yourself: ' + PrefixOf(Chat) + C +
          ' without a name.');
      Result := CMD_REFUSED;
      Exit;
    end;
  end;

  { while the database is being read every answer would come from half of it: nothing but the
    help until it is done }
  if LoadInProgress and (C <> 'antifake') and (C <> 'help') then
  begin
    OutClear(Slot);
    Say(Slot, CLR_WARN, 'The database is still being read; try again in a moment.');
    Result := CMD_REFUSED;
    Exit;
  end;

  { a slot or a row that does not fit, told once the command itself is allowed }
  if FormWhy <> '' then
  begin
    OutClear(Slot);
    Say(Slot, CLR_WARN, FormWhy);
    Result := CMD_REFUSED;
    Exit;
  end;

  if not CommandAllowed(Slot, IsHeavyCommand(C), IsAdmin, Why) then
  begin
    OutClear(Slot);
    Say(Slot, CLR_WARN, Why);
    Result := CMD_REFUSED;
    Exit;
  end;

  { one command at a time per slot; the one that is running keeps its turn }
  if not GrabLock(CmdCS, 5) then
  begin
    OutClear(Slot);
    Say(Slot, CLR_WARN, 'Busy, try again in a moment.');
    Result := CMD_REFUSED;
    Exit;
  end;
  try
    if CmdBusy[Slot] then
    begin
      OutClear(Slot);
      Say(Slot, CLR_WARN, 'The last command is still being answered.');
      Result := CMD_REFUSED;
      Exit;
    end;
    CmdBusy[Slot] := True;
    CmdStartedTask[Slot] := False;
  finally LeaveCriticalSection(CmdCS); end;

  { the slot is busy from here on: whatever fails before the worker runs frees it again }
  try
    OutClear(Slot);
    if IsAdmin then
    begin
      if Chat then Prefix := '!' else Prefix := CMD_PREFIX;
      LogEvent(TrimRight(string(Who) + ': ' + Prefix + Typed + ' ' + Trim(string(Params))));
    end;
    StartBackground(TCommandThread.Create(Slot, C, P, string(Who), IsAdmin, Chat));
    Result := CMD_TASK;
  except
    EnterCriticalSection(CmdCS);
    try
      CmdBusy[Slot] := False;
    finally LeaveCriticalSection(CmdCS); end;
    Say(Slot, CLR_ERROR, 'That could not be started.');
    Result := CMD_REFUSED;
  end;
end;

{ did the command that just ran start a search whose result is still coming }
function Command_Started_Task(Slot: Integer): Boolean; cdecl;
begin
  Result := False;
  try
    if (Slot < 0) or (Slot > MAX_SLOTS) then Exit;
    EnterCriticalSection(CmdCS);
    try
      Result := CmdStartedTask[Slot];
    finally LeaveCriticalSection(CmdCS); end;
  except
    ExportFailed('Command_Started_Task');
    Result := False;
  end;
end;

{ has the worker finished with this slot }
function Command_Busy(Slot: Integer): Boolean; cdecl;
begin
  Result := False;
  try
    if (Slot < 0) or (Slot > MAX_SLOTS) then Exit;
    EnterCriticalSection(CmdCS);
    try
      Result := CmdBusy[Slot];
    finally LeaveCriticalSection(CmdCS); end;
  except
    ExportFailed('Command_Busy');
    Result := False;
  end;
end;

{ ---------------- what the script reads back ---------------- }

function Out_Count(Slot: Integer): Integer; cdecl;
begin
  Result := 0;
  try
    if (Slot < 0) or (Slot > MAX_SLOTS) then Exit;
    EnterCriticalSection(OutCS);
    try
      if Outp[Slot].Lines <> nil then Result := Outp[Slot].Lines.Count;
    finally LeaveCriticalSection(OutCS); end;
  except
    ExportFailed('Out_Count');
    Result := 0;
  end;
end;

function Out_Line(Slot, Index: Integer): PChar; cdecl;
begin
  TempStringResult := '';
  try
    if (Slot >= 0) and (Slot <= MAX_SLOTS) then
    begin
      EnterCriticalSection(OutCS);
      try
        if Outp[Slot].Lines <> nil then
          if (Index >= 0) and (Index < Outp[Slot].Lines.Count) then
            TempStringResult := Outp[Slot].Lines[Index];
      finally LeaveCriticalSection(OutCS); end;
    end;
  except
    ExportFailed('Out_Line');
    TempStringResult := '';
  end;
  Result := PChar(TempStringResult);
end;

function Out_Color(Slot, Index: Integer): Integer; cdecl;
begin
  Result := CLR_MAIN;
  try
    if (Slot < 0) or (Slot > MAX_SLOTS) then Exit;
    EnterCriticalSection(OutCS);
    try
      if Outp[Slot].Lines <> nil then
        if (Index >= 0) and (Index < Outp[Slot].Lines.Count) then
          Result := Integer(PtrInt(Outp[Slot].Lines.Objects[Index]));
    finally LeaveCriticalSection(OutCS); end;
  except
    ExportFailed('Out_Color');
    Result := CLR_MAIN;
  end;
end;

procedure Out_Clear(Slot: Integer); cdecl;
begin
  try
    OutClear(Slot);
  except
    ExportFailed('Out_Clear');
  end;
end;

{ the first Count lines have been printed: they go, and whatever a worker wrote after them
  stays for the next tick (clearing the whole buffer lost those) }
procedure Out_Drop(Slot, Count: Integer); cdecl;
var
  k: Integer;
begin
  try
    if (Slot < 0) or (Slot > MAX_SLOTS) then Exit;
    EnterCriticalSection(OutCS);
    try
      if Outp[Slot].Lines <> nil then
        for k := 1 to Count do
        begin
          if Outp[Slot].Lines.Count = 0 then Break;
          Outp[Slot].Lines.Delete(0);
        end;
    finally LeaveCriticalSection(OutCS); end;
  except
    ExportFailed('Out_Drop');
  end;
end;

{ the script tells the library who is playing, so the library can draw the tables that
  talk about them }
procedure Set_Online(Slot, ProfileIdx: Integer; Name, IP, Hwid: PChar; IsAdmin: Boolean); cdecl;
begin
  try
    if (Slot < 1) or (Slot > MAX_SLOTS) then Exit;
    EnterCriticalSection(OnlineCS);
    try
      OnlineHere[Slot] := True;
      OnlineProfile[Slot] := ProfileIdx;
      OnlineName[Slot] := string(Name);
      OnlineIP[Slot] := string(IP);
      OnlineHwid[Slot] := string(Hwid);
    finally LeaveCriticalSection(OnlineCS); end;
  except
    ExportFailed('Set_Online');
  end;
end;

{ a player left: the next one on this slot hears nothing that was meant for this one }
procedure Clear_Online(Slot: Integer); cdecl;
begin
  try
    if (Slot < 1) or (Slot > MAX_SLOTS) then Exit;
    EnterCriticalSection(OnlineCS);
    try
      OnlineHere[Slot] := False;
      OnlineProfile[Slot] := -1;
      OnlineName[Slot] := '';
      OnlineIP[Slot] := '';
      OnlineHwid[Slot] := '';
    finally LeaveCriticalSection(OnlineCS); end;
    Inc(SlotSession[Slot]);
    DropNotices(Slot);
    EnterCriticalSection(MirrorCS);
    try
      if MirrorAsker = Slot then MirrorAsker := 0;
    finally LeaveCriticalSection(MirrorCS); end;
    OutClear(Slot);
    ViewPage[Slot] := 1;
  except
    ExportFailed('Clear_Online');
  end;
end;

{ A finished search, drawn into the slot's buffer. The drawing waits for locks that a full write
  or the copy into MySQL may hold, so it runs on a worker like every answer, and the script
  picks the lines up the same way (Command_Busy, Out_Count). When no worker can take it now,
  nothing happens and the script asks again on the next tick. }
procedure Render_Result(Slot: Integer); cdecl;
var
  Started: Boolean;
begin
  try
    if (Slot < 1) or (Slot > MAX_SLOTS) then Exit;
    Started := False;
    if not GrabLock(CmdCS, 5) then Exit;
    try
      if not CmdBusy[Slot] then
      begin
        CmdBusy[Slot] := True;
        CmdStartedTask[Slot] := False;
        Started := True;
      end;
    finally LeaveCriticalSection(CmdCS); end;
    if not Started then Exit;
    try
      OutClear(Slot);
      StartBackground(TCommandThread.Create(Slot, '#render', '', '', ViewerAdmin[Slot], False));
    except
      { no thread: drawn here, as it always was, and the slot is free again either way }
      try
        RenderResult(Slot);
      finally
        EnterCriticalSection(CmdCS);
        try
          CmdBusy[Slot] := False;
        finally LeaveCriticalSection(CmdCS); end;
      end;
    end;
  except
    ExportFailed('Render_Result');
  end;
end;

{ The line the admins get when a player joins on an account tied to others, '' for nothing.
  An account is named when its score and its signals reach MultiAccountReportScore and
  MultiAccountMinSignals and no admin has judged the pair; nothing is said before
  MultiAccountMinLinks accounts are named (the setting used to be read and never asked). The
  line names three of them at most. On a worker: it waits for the lock like one. }
function MultiNoticeText(ProfileIdx: Integer; const PlayerName: string): string;
var
  Links: TStringList;
  i, Other, Bits, Score, Sig, Said, Worth, Total: Integer;
  Names, Reasons: string;
begin
  Result := '';
  Sig := 0;
  Reasons := '';
  Links := TStringList.Create;
  try
    LockProfiles;
    try
      CollectLinks(ProfileIdx, Links);
      Total := Links.Count;
      Names := '';
      Said := 0;
      Worth := 0;
      for i := 0 to Links.Count - 1 do
      begin
        { three are named; past them the rest is only counted until there are enough }
        if (Said >= 3) and (Worth >= Settings.MultiMinLinks) then Break;
        Other := StrToIntDef(Links[i], -1);
        Bits := Integer(PtrInt(Links.Objects[i]));
        Score := LinkScore(ProfileIdx, Other, Bits, Reasons, Sig);
        if Score < Settings.MultiReportScore then Continue;
        if Sig < Settings.MultiMinSignals then Continue;
        if VerdictOf(ProfileIdx, Other) <> VERDICT_NONE then Continue;
        Inc(Worth);
        if Said >= 3 then Continue;
        if Names <> '' then Names := Names + ', ';
        Names := Names + '#' + IntToStr(Other) + ' ' + GetProfileActiveName(Other) +
                 ' (' + IntToStr(Score) + ', ' + LinkText(Bits) + ')';
        Inc(Said);
      end;
    finally UnlockProfiles; end;
    if (Names = '') or (Worth < Settings.MultiMinLinks) then Exit;
    Result := PlayerName + ' plays on account #' + IntToStr(ProfileIdx) + ', which looks like ' + Names;
    if Total > Said then Result := Result + ' (of ' + IntToStr(Total) + ' tied)';
  finally Links.Free; end;
end;

type
  TMultiNoticeThread = class(TThread)
  private
    FProfile: Integer;
    FName: string;
  protected
    procedure Execute; override;
  public
    constructor Create(AProfile: Integer; const AName: string);
  end;

constructor TMultiNoticeThread.Create(AProfile: Integer; const AName: string);
begin
  inherited Create(True);
  FProfile := AProfile;
  FName := AName;
end;

{ the line goes to the admins as two notices of slot 0 (Notice_Line), and into the log here }
procedure TMultiNoticeThread.Execute;
var
  Line: string;
begin
  GuardWorkerThread;
  try
    Line := MultiNoticeText(FProfile, FName);
    if Line = '' then Exit;
    LogEvent(Line);
    Notice(0, Settings.ColWarn, Line);
    Notice(0, Settings.ColData, CMD_PREFIX + 'multi ' + IntToStr(FProfile) + ' to look at it.');
  except
    { a notice that could not be made is not worth more than that }
  end;
end;

{ Asked at every join, on the game thread. Collecting the ties and scoring them read the
  indexes and the profiles under their lock right there; now a worker does it and the answer
  comes a moment later through Notice_Line, as two lines for slot 0, which means "every admin
  in the game, and the console" (the line is in logs/antifake/antifake.log already). What this
  returns is always '' now. }
function Multi_Notice(ProfileIdx: Integer; PlayerName: PChar): PChar; cdecl;
begin
  TempStringResult := '';
  try
    if Settings.MultiNotice and not ShuttingDown and (ProfileIdx >= 0) then
      StartBackground(TMultiNoticeThread.Create(ProfileIdx, string(PlayerName)));
  except
    ExportFailed('Multi_Notice');
  end;
  Result := PChar(TempStringResult);
end;
