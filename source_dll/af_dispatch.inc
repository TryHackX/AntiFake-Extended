{ ---------------------------------------------------------------------------------------
  AntiFake Extended - the views that answer at once: the help, Steam, the ties of one account,
  the players on the server, where they come from and the storage. The dispatcher that picks
  one is in af_run.inc.
  --------------------------------------------------------------------------------------- }

{ ---------------- the help ---------------- }

{ Every command the one who asks may use, in the form they type it: a player sees what the
  [PlayerCommands] reach of the server gives them, with !, an admin sees that and the admin
  commands, with /. The pages of a list are said once, under their own heading. }
procedure ViewHelp(Slot: Integer; IsAdmin: Boolean);
const
  { three columns of 33 in every section, so they stand under each other all the way down }
  HELP_COLS = 3;
  HELP_COLW = 33;
var
  L: TStringList;
  P: string;
  CanName: Boolean;
begin
  if IsAdmin then P := CMD_PREFIX else P := '!';
  { a player who may ask about somebody else, and so needs the forms of naming one }
  CanName := IsAdmin or (Settings.CmdVisits > 1) or (Settings.CmdStatus > 1) or
    (Settings.CmdNames > 1) or (Settings.CmdStopSteam > 1) or (Settings.CmdSearch > 1) or
    (Settings.CmdMulti > 1) or ((Settings.CmdLocate > 1) and Settings.GeoEnabled);
  L := TStringList.Create;
  try
    SayHeader(Slot, 'AntiFake Extended ' + AF_VERSION + ' - commands');

    SaySection(Slot, 'Your account');
    if Settings.CmdStatus > 0 then L.Add('!mystatus');
    if Settings.CmdNames > 0 then L.Add('!mynames [sort]');
    if Settings.CmdVisits > 0 then L.Add('!visits [range]');
    if Settings.CmdVisits > 0 then L.Add('!uniquevisits [range]');
    if Settings.CmdVisits > 0 then L.Add('!locations [range]');
    if Settings.CmdStopSteam > 0 then L.Add('!steam');
    { the places come from the geo lookups, as for the admins below }
    if (Settings.CmdLocate > 0) and Settings.GeoEnabled then L.Add('!locate');
    if L.Count = 0 then L.Add('(nothing - the admin turned the ! commands off)');
    SayGrid(Slot, L, HELP_COLS, HELP_COLW, CLR_MAIN);

    { the pages used to be listed here with ! and again below with / }
    if Settings.CmdVisitStats > 0 then L.Add('!visitstats [range]');
    if Settings.CmdVisitsOf > 1 then L.Add('!visitsof c* [range]');
    if Settings.CmdVisits > 1 then L.Add('!lastvisits [range]');
    { who came, when and for how long, without addresses: PlayerLastVisitors }
    if Settings.CmdLastVisitors > 1 then L.Add('!lastvisitors [range]');
    if Settings.CmdProfiles > 1 then L.Add('!online');
    if Settings.CmdProfiles > 1 then L.Add('!listprofiles [sort] [asc|desc]');
    if Settings.CmdHelp > 0 then L.Add('!antifake');
    if L.Count > 0 then
    begin
      SaySection(Slot, 'The server');
      SayGrid(Slot, L, HELP_COLS, HELP_COLW, CLR_MAIN);
    end;

    if Settings.CmdVisits > 1 then L.Add('!visits p*');
    if Settings.CmdVisits > 1 then L.Add('!visitsp r*');
    if Settings.CmdVisits > 1 then L.Add('!visitsid s*');
    if Settings.CmdVisits > 1 then L.Add('!visitsi n*');
    if Settings.CmdVisits > 1 then L.Add('!uniquevisits p*');
    if Settings.CmdVisits > 1 then L.Add('!locations p*');
    if Settings.CmdStatus > 1 then L.Add('!statusa p*');
    if Settings.CmdNames > 1 then L.Add('!privacy k* p*');
    if Settings.CmdNames > 1 then L.Add('!privacyall k* p*');
    if Settings.CmdStopSteam > 1 then L.Add('!steam p*');
    if (Settings.CmdLocate > 1) and Settings.GeoEnabled then L.Add('!locate p*');
    if (Settings.CmdLocate > 1) and Settings.GeoEnabled then L.Add('!locateall');
    { everything PlayerSearch opens, not only two of them }
    if Settings.CmdSearch > 1 then L.Add('!af_search t*');
    if Settings.CmdSearch > 1 then L.Add('!searchall t*');
    if Settings.CmdSearch > 1 then L.Add('!smartsearch t*');
    if Settings.CmdSearch > 1 then L.Add('!checkp k* p*');
    if Settings.CmdSearch > 1 then L.Add('!checkd k* v*');
    if (Settings.CmdSearch > 1) and Settings.GeoEnabled then L.Add('!geosearch t*');
    if (Settings.CmdSearch > 1) and Settings.GeoEnabled then L.Add('!showlocate k* p*');
    if Settings.CmdMulti > 1 then L.Add('!multi p*');
    if L.Count > 0 then
    begin
      SaySection(Slot, 'About other players');
      SayGrid(Slot, L, HELP_COLS, HELP_COLW, CLR_MAIN);
    end;
    L.Clear;

    if IsAdmin then
    begin
      SaySection(Slot, 'Admin: who is who');
      L.Add('/af_search t*');
      L.Add('/searchall t*');
      L.Add('/smartsearch t*');
      L.Add('/statusa p*');
      L.Add('/privacy k* p*');
      L.Add('/privacyall k* p*');
      L.Add('/checkp k* p*');
      L.Add('/checkd k* v*');
      L.Add('/listprofiles [sort] [asc|desc]');
      { /online and /showid take nothing: the sort and the id they offered did nothing }
      L.Add('/online');
      L.Add('/showid');
      L.Add('/steam [p]');
      SayGrid(Slot, L, HELP_COLS, HELP_COLW, CLR_DATA);

      SaySection(Slot, 'Admin: visits and places');
      { a column for each command, a row for each way of naming the player }
      L.Add('/visits [p] [range]');
      L.Add('/uniquevisits [p] [range]');
      L.Add('/locations [p] [range]');
      L.Add('/visitsp r*');
      L.Add('/uniquevisitsp r*');
      L.Add('/locationsp r*');
      L.Add('/visitsid s*');
      L.Add('/uniquevisitsid s*');
      L.Add('/locationsid s*');
      L.Add('/visitsi n*');
      L.Add('/uniquevisitsi n*');
      L.Add('/locationsi n*');
      SayGrid(Slot, L, HELP_COLS, HELP_COLW, CLR_DATA);
      L.Add('/visitstats [range]');
      L.Add('/visitsof c* [range]');
      L.Add('/lastvisits [range]');
      L.Add('/lastvisitors [range]');
      if Settings.GeoEnabled then L.Add('/showlocate k* p*');
      if Settings.GeoEnabled then L.Add('/geosearch t*');
      if Settings.GeoEnabled then L.Add('/locate [p]');
      if Settings.GeoEnabled then L.Add('/locateall');
      SayGrid(Slot, L, HELP_COLS, HELP_COLW, CLR_DATA);

      SaySection(Slot, 'Admin: accounts that look like one person');
      L.Add('/multi [p|sort]');
      L.Add('/multi strong');
      L.Add('/multi top');
      L.Add('/multi topaddresses');
      L.Add('/multi stats');
      L.Add('/multiset a* b* w*');
      L.Add('/steamstatus');
      SayGrid(Slot, L, HELP_COLS, HELP_COLW, CLR_DATA);

      SaySection(Slot, 'Admin: the database');
      L.Add('/storage');
      L.Add('/storage save');
      L.Add('/storage backups');
      L.Add('/storage restore f*');
      { the copy for a website belongs to Engine = mirror, as in /storage }
      if Settings.MysqlEngine = ENGINE_MIRROR then L.Add('/storage mirror');
      SayGrid(Slot, L, HELP_COLS, HELP_COLW, CLR_DATA);
    end;

    { with the ! of a player, who may not have the / commands at all }
    SaySection(Slot, 'Pages of the last list');
    L.Add(P + 'af_next');
    L.Add(P + 'af_prev');
    L.Add(P + 'af_page n*');
    SayGrid(Slot, L, HELP_COLS, HELP_COLW, CLR_MAIN);

    SayRule(Slot);
    Say(Slot, CLR_DATA, '* required  [ ] optional  a|b = one of them  p = nick, part of it or #id');
    if CanName then
    begin
      { #1 is an account number, so a player named "#1" (or "all", "top") is written in quotes }
      Say(Slot, CLR_DATA, 'a name in double quotes is always a name: "#1" is the player named #1, #1 the account');
      Say(Slot, CLR_DATA, 'instead of p: <command>p r*  <command>id s*  <command>i n*   e.g. ' + P + 'visitsi 1');
      Say(Slot, CLR_DATA, 'r = row of your last list  s = slot (' + P + 'online)  n = position in ZitroStats !top');
    end;
    if IsAdmin or (Settings.CmdNames > 1) or (Settings.CmdSearch > 1) or (Settings.CmdVisitsOf > 1) then
      Say(Slot, CLR_DATA, 'k = kind: n (name), s (steam), h (hwid), i (ip)   c = country or ISO code   v = a value   t = text');
    { the example range is the last three weeks, so it never looks out of date }
    Say(Slot, CLR_DATA, 'range = 7d, 30d, all or ' + FormatDateTime('yyyy-mm-dd', Date - 20) + '..' +
      FormatDateTime('yyyy-mm-dd', Date) + '   sort = id, count, time, latest, first');
    if IsAdmin or (Settings.CmdProfiles > 1) then
      Say(Slot, CLR_DATA, 'listprofiles sort = ' + LIST_SORT_WORDS + ' (desc if alone)');
    { byname was offered here, and nothing lists the visits by name }
    if IsAdmin or (Settings.CmdVisits > 0) then
      Say(Slot, CLR_DATA, 'visits also take masked (VPN or hosting only), clean (without), bycountry, byip, sort:<what>');
    if IsAdmin then
    begin
      Say(Slot, CLR_DATA, 'a, b = account numbers   w = same, family, ignore or clear');
      Say(Slot, CLR_DATA, 'f = a copy from /storage backups');
    end;
    Say(Slot, CLR_INFO, Frame('AntiFake Extended ' + AF_VERSION + ' by TryHackX', '=', VIEW_WIDTH));
  finally
    L.Free;
  end;
end;

{ ---------------- what Steam says about an account ---------------- }

procedure ViewSteam(Slot, Idx: Integer);
var
  S: string;
  Age, Vac, GameB, Comm, Trade, LastBan, Vis: Integer;
  Created, Fetched, Persona, Country, Id: string;
begin
  if Settings.SteamKey = '' then
  begin
    Say(Slot, CLR_WARN, 'The Steam part is off: no SteamApiKey in settings.ini.');
    Exit;
  end;
  { a worker waits for the lock: giving up after 20 ms answered nothing at all while a full
    write held it }
  LockProfiles;
  try
    if (Idx < 0) or (Idx >= ProfileCount) then
    begin
      Say(Slot, CLR_ERROR, 'No such account.');
      Exit;
    end;
    if Profiles[Idx].Steam.Fetched <= 0 then
    begin
      S := GetProfileActiveSteam(Idx);
      Say(Slot, CLR_WARN, 'Nothing from Steam for account #' + IntToStr(Idx) + ' yet.');
      if S = '' then
        Say(Slot, CLR_DATA, 'That account has no Steam id.')
      { /steamstatus is for admins: a player is not sent to it }
      else if ViewerAdmin[Slot] then
        Say(Slot, CLR_DATA, 'Asking now - try again in a few seconds, ' + CMD_PREFIX +
            'steamstatus says how it went.')
      else
        Say(Slot, CLR_DATA, 'Asking now - try again in a few seconds.');
      Exit;
    end;
    Id := string(Profiles[Idx].Steam.SteamId);
    Persona := Utf8ToConsole(string(Profiles[Idx].Steam.Persona));
    Country := string(Profiles[Idx].Steam.Country);
    if Persona = '' then Persona := '(no name)';
    if Country = '' then Country := '??';
    if Profiles[Idx].Steam.Created > 1 then
    begin
      Created := DateText(Profiles[Idx].Steam.Created);
      Age := Trunc(Now - Profiles[Idx].Steam.Created);
    end
    else
    begin
      Created := 'unknown';
      Age := -1;
    end;
    Vis := Profiles[Idx].Steam.Visible;
    Vac := Profiles[Idx].Steam.VacBans;
    GameB := Profiles[Idx].Steam.GameBans;
    Comm := Profiles[Idx].Steam.CommunityBan;
    Trade := Profiles[Idx].Steam.TradeBan;
    LastBan := Profiles[Idx].Steam.DaysSinceLastBan;
    Fetched := DateTimeText(Profiles[Idx].Steam.Fetched);
  finally UnlockProfiles; end;

  SayHeader(Slot, 'Steam of account #' + IntToStr(Idx) + ': ' + Persona + ' (' + Country + ')');
  if Vis = 1 then S := 'public' else S := 'private';
  Say(Slot, CLR_MAIN, Pad('Account', 9, 0) + ': ' + SecretFor(Slot, Idx, KIND_STEAM, Id) + '   Profile: ' + S);
  if Age >= 0 then
    Say(Slot, CLR_MAIN, Pad('Created', 9, 0) + ': ' + Created + ' (' +
        SecondsText(Int64(Age) * 86400) + ' ago)')
  else
    Say(Slot, CLR_MAIN, Pad('Created', 9, 0) + ': unknown');
  if (Vac = 0) and (GameB = 0) and (Comm = 0) and (Trade = 0) then
    Say(Slot, CLR_DATA, Pad('Bans', 9, 0) + ': none')
  else
  begin
    S := 'VAC ' + IntToStr(Vac) + ' | game ' + IntToStr(GameB);
    if Comm = 1 then S := S + ' | community' ;
    if Trade = 1 then S := S + ' | trade';
    if LastBan > 0 then S := S + ' | last ' + IntToStr(LastBan) + ' d ago';
    Say(Slot, CLR_ERROR, Pad('Bans', 9, 0) + ': ' + S);
  end;
  Say(Slot, CLR_DATA, Pad('Asked', 9, 0) + ': ' + Fetched);
end;

{ ---------------- one account and what it is tied to ---------------- }

procedure ViewMultiOf(Slot, Idx: Integer);
var
  Links: TStringList;
  i, Other, Bits, Score, Sig, V, Color, NW: Integer;
  Reasons: string;
  Names: array of string;
  ForeignRows: TForeignRows;
  ForeignWhy: string;
begin
  { the accounts of the other servers that share a Steam account, a computer or an address with
    this one (SharedLinks); asked first, the lock of the profiles is taken for each kind }
  ForeignRows := nil;
  ForeignWhy := '';
  Names := nil;
  Sig := 0;
  Reasons := '';
  if SharedReady and Settings.SharedLinks then
    ForeignLinks(Idx, FOREIGN_TIES, ForeignRows, ForeignWhy);
  Links := TStringList.Create;
  try
    { on a worker: it waits for the lock, where giving up answered nothing at all, or half a
      table, while a full write held it }
    LockProfiles;
    try
      CollectLinks(Idx, Links);
      SetLength(Names, Links.Count);
      NW := 4;
      for i := 0 to Links.Count - 1 do
      begin
        Names[i] := GetProfileActiveName(StrToIntDef(Links[i], -1));
        NW := Max(NW, Length(Names[i]));
      end;
      if NW > 24 then NW := 24;
    finally UnlockProfiles; end;
    if Links.Count = 0 then
    begin
      Say(Slot, CLR_INFO, 'Account #' + IntToStr(Idx) + ' is not tied to any other one here.');
      SayForeign(Slot, ForeignRows, ForeignWhy);
      Exit;
    end;
    SayHeader(Slot, 'Account #' + IntToStr(Idx) + ' is tied to ' + IntToStr(Links.Count) +
        ' other(s)');
    { "computer+steam+address" is 22 letters: the old 16 cut it to "computer+steam+a" }
    Say(Slot, CLR_MAIN, Pad('Account', 7, 1) + ' | ' + Pad('Name', NW, 0) + ' | ' +
        Pad('Score', 8, 1) + ' | ' + Pad('What they share', 22, 0) + ' | Admin said');
    for i := 0 to Links.Count - 1 do
    begin
      Other := StrToIntDef(Links[i], -1);
      Bits := Integer(PtrInt(Links.Objects[i]));
      LockProfiles;
      try
        Score := LinkScore(Idx, Other, Bits, Reasons, Sig);
        V := VerdictOf(Idx, Other);
      finally UnlockProfiles; end;
      if Score >= Settings.MultiReportScore then Color := CLR_ERROR
      else if Score >= 40 then Color := CLR_WARN
      else Color := CLR_DATA;
      Say(Slot, Color, Pad('#' + IntToStr(Other), 7, 1) + ' | ' + Pad(Names[i], NW, 0) + ' | ' +
          Pad(IntToStr(Score) + ' (' + IntToStr(Sig) + ')', 8, 1) + ' | ' +
          Pad(LinkText(Bits), 22, 0) + ' | ' + VerdictText(V));
    end;
    { /multiset is for admins: a player with PlayerMulti = all is not sent to it }
    if ViewerAdmin[Slot] then
      Say(Slot, CLR_DATA, 'Say what a pair is: ' + CMD_PREFIX + 'multiset ' + IntToStr(Idx) +
          ' <id> same|family|ignore|clear');
    SayForeign(Slot, ForeignRows, ForeignWhy);
  finally Links.Free; end;
end;

{ ---------------- the players who are here now ---------------- }

procedure ViewOnline(Slot: Integer; IsAdmin: Boolean);
var
  i, n, NW, KW: Integer;
  Acc: string;
  Rows: array[1..32] of TOnlineRow;
  Known: array[1..32] of string;
begin
  SayHeader(Slot, 'Players on the server');
  { the names first: the columns are as wide as the longest of them }
  NW := 4;
  KW := 8;
  n := 0;
  for i := 1 to 32 do
  begin
    Rows[i] := OnlineRow(i);
    Known[i] := '';
    if not Rows[i].Here then Continue;
    Inc(n);
    NW := Max(NW, Length(Rows[i].Name));
    if IsAdmin and (Rows[i].Profile >= 0) then
    begin
      Known[i] := GetProfileActiveNameSafe(Rows[i].Profile);
      KW := Max(KW, Length(Known[i]));
    end;
  end;
  if n = 0 then
  begin
    Say(Slot, CLR_WARN, 'Nobody is playing right now.');
    Exit;
  end;
  if NW > 24 then NW := 24;
  if KW > 24 then KW := 24;
  if IsAdmin then
    Say(Slot, CLR_MAIN, Pad('Slot', 4, 1) + ' | ' + Pad('Name', NW, 0) + ' | ' +
        Pad('Account', 7, 1) + ' | ' + Pad('Known as', KW, 0) + ' | From')
  else
    Say(Slot, CLR_MAIN, Pad('Slot', 4, 1) + ' | ' + Pad('Name', NW, 0) + ' | ' +
        Pad('Account', 7, 1));
  for i := 1 to 32 do
  begin
    if not Rows[i].Here then Continue;
    if Rows[i].Profile < 0 then Acc := '...' else Acc := '#' + IntToStr(Rows[i].Profile);
    if IsAdmin then
      Say(Slot, CLR_DATA, Pad(IntToStr(i), 4, 1) + ' | ' + Pad(Rows[i].Name, NW, 0) + ' | ' +
          Pad(Acc, 7, 1) + ' | ' + Pad(Known[i], KW, 0) + ' | ' + GeoTextOf(Rows[i].IP))
    else
      Say(Slot, CLR_DATA, Pad(IntToStr(i), 4, 1) + ' | ' + Pad(Rows[i].Name, NW, 0) + ' | ' +
          Pad(Acc, 7, 1));
  end;
end;

{ Where the players on the server come from. Mode: LOCATE_SELF = the one who asks,
  LOCATE_NAME = the players whose name holds Who, LOCATE_ALL = everybody. An admin sees the
  address and everything known about the place; a player sees the country, the city and the
  provider, and their own address only. }
procedure ViewLocate(Slot: Integer; const Who: string; Mode: Integer; IsAdmin: Boolean);
var
  i, n, Acc, NW, AW: Integer;
  Low: string;
  Rows: array[1..32] of TOnlineRow;
  Shown: array[1..32] of Boolean;
  Typed: Boolean;
begin
  Low := '';
  { "#12" (typed, or what /locateid and /locatep become): the player on account 12, with the
    note of a "#12" that is somebody's name as well (ResolveProfileFor); a name in double quotes
    is always a name. A # with anything else behind it is a name too. }
  Acc := -1;
  if Mode = LOCATE_NAME then
    if IsAccountForm(Who, Acc, Typed) then
    begin
      Acc := ResolveProfileFor(Slot, Who);
      if Acc < 0 then
      begin
        Say(Slot, CLR_WARN, 'No account found for: ' + ShownText(Trim(Who)));
        Exit;
      end;
    end
    else Low := LowerCase(UnquotedText(Who));
  NW := 4;
  AW := 7;
  n := 0;
  for i := 1 to 32 do
  begin
    Rows[i] := OnlineRow(i);
    Shown[i] := False;
    if not Rows[i].Here then Continue;
    if (Mode = LOCATE_SELF) and (i <> Slot) then Continue;
    if Mode = LOCATE_NAME then
      if Acc >= 0 then
      begin
        if Rows[i].Profile <> Acc then Continue;
      end
      else if (Low <> '') and (Pos(Low, LowerCase(Rows[i].Name)) = 0) then Continue;
    Shown[i] := True;
    Inc(n);
    NW := Max(NW, Length(Rows[i].Name));
    AW := Max(AW, Length(Rows[i].IP));
  end;
  if NW > 24 then NW := 24;
  if AW > 39 then AW := 39;
  SayHeader(Slot, 'Where the players come from');
  if n > 0 then
    Say(Slot, CLR_MAIN, Pad('Slot', 4, 1) + ' | ' + Pad('Name', NW, 0) + ' | ' +
        Pad('Address', AW, 0) + ' | Where');
  for i := 1 to 32 do
    if Shown[i] then
      Say(Slot, CLR_DATA, Pad(IntToStr(i), 4, 1) + ' | ' + Pad(Rows[i].Name, NW, 0) + ' | ' +
          Pad(AddressFor(Slot, Rows[i].Profile, Rows[i].IP), AW, 0) + ' | ' + WhereFor(Slot, Rows[i].IP));
  if n = 0 then
  begin
    if Mode = LOCATE_SELF then Say(Slot, CLR_WARN, 'Your place is not known yet, try again in a moment.')
    else if Mode = LOCATE_ALL then Say(Slot, CLR_WARN, 'Nobody is playing right now.')
    else if Acc >= 0 then Say(Slot, CLR_WARN, 'Account #' + IntToStr(Acc) + ' is not on the server.')
    else Say(Slot, CLR_WARN, 'Nobody on the server has that in the name.');
  end;
end;

{ ---------------- the database ---------------- }

procedure ViewStorage(Slot: Integer; const What: string);
var
  Files: TStringList;
  i: Integer;
  Name: string;
begin
  if (What = 'backups') or (What = 'copies') then
  begin
    Files := ListBackupFiles(LoadedPath);
    try
      if Files.Count = 0 then
      begin
        Say(Slot, CLR_WARN, 'No copies yet (one is made every BackupHours after a full write).');
        Exit;
      end;
      SayHeader(Slot, 'Copies kept, oldest first');
      for i := 0 to Files.Count - 1 do
        Say(Slot, CLR_DATA, Pad(IntToStr(i + 1), 4, 1) + ' | ' + Files[i]);
      Say(Slot, CLR_DATA, 'Put one back: ' + CMD_PREFIX +
          'storage restore <name>, then restart the server.');
    finally Files.Free; end;
    Exit;
  end;
  if Copy(What, 1, 8) = 'restore ' then
  begin
    { a name in double quotes is the name without them }
    Name := UnquotedText(Copy(What, 9, Length(What)));
    if Name = '' then
    begin
      Say(Slot, CLR_WARN, 'Usage: ' + CMD_PREFIX + 'storage restore <name from ' +
          CMD_PREFIX + 'storage backups>');
      Exit;
    end;
    if RestoreBackupFile(Name) then
    begin
      Say(Slot, CLR_INFO, Name + ' is now the database file.');
      Say(Slot, CLR_INFO, 'The one that was there was kept as .replaced-<date> - nothing was deleted.');
      Say(Slot, CLR_WARN, 'Restart the server to read it. Until then nothing is written over it.');
      { with one database for every server the file is only its cache }
      if Settings.MysqlEngine = ENGINE_MYSQL then
        Say(Slot, CLR_WARN, 'Engine = mysql: this file is only the cache - the next start reads the shared database.');
      LoadError := True;
    end
    else
    begin
      { a restore is also refused while a full write runs: the name alone was not the answer }
      Say(Slot, CLR_ERROR, 'That copy could not be put in place now: either the name is wrong (' +
          CMD_PREFIX + 'storage backups lists them)');
      Say(Slot, CLR_ERROR, 'or a full write is running - try again in a moment.');
    end;
    Exit;
  end;
  if (What = 'mirror') or (What = 'mysql') then
  begin
    if StartMirrorFor(Slot, Name) then
      Say(Slot, CLR_INFO, 'Writing the copy into MySQL now; you hear when it is done.')
    else
      Say(Slot, CLR_WARN, Name);
    Exit;
  end;
  SayHeader(Slot, 'Storage');
  Say(Slot, CLR_MAIN, Pad('File', 10, 0) + ': ' + LoadedPath);
  Say(Slot, CLR_MAIN, Pad('Journal', 10, 0) + ': ' + JournalStatusText);
  Say(Slot, CLR_MAIN, Pad('MySQL', 10, 0) + ': ' + MysqlStatusText);
  { BackupHours = 0 means no copies at all, not "every 0 h" }
  if Settings.BackupHours > 0 then
    Say(Slot, CLR_MAIN, Pad('Full write', 10, 0) + ': every ' + IntToStr(Settings.SaveMinutes) +
        ' minutes, copies every ' + IntToStr(Settings.BackupHours) + ' h (keeping ' +
        IntToStr(Settings.BackupKeep) + ')')
  else
    Say(Slot, CLR_MAIN, Pad('Full write', 10, 0) + ': every ' + IntToStr(Settings.SaveMinutes) +
        ' minutes, no copies (BackupHours = 0)');
  { a copy put back with /storage restore stops the writes the same way, and was reported as a
    database that could not be read }
  if LoadError then
    Say(Slot, CLR_ERROR, Pad('State', 10, 0) + ': nothing is written until the next start - the ' +
        'database could not be read, or a copy was put back')
  else
    Say(Slot, CLR_DATA, Pad('State', 10, 0) + ': fine');
  { the copy for a website belongs to Engine = mirror }
  if Settings.MysqlEngine = ENGINE_MIRROR then
    Say(Slot, CLR_DATA, 'More: ' + CMD_PREFIX + 'storage save, backups, restore <name>, mirror')
  else
    Say(Slot, CLR_DATA, 'More: ' + CMD_PREFIX + 'storage save, backups, restore <name>');
end;
