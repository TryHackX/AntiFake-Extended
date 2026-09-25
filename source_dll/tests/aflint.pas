program aflint;

{ Every answer a player, an admin and the console get, produced by the library and checked by a
  program: every command the help lists (with and without its arguments, the p/id/i forms, the
  pages of a list, a name that matches nothing and a name of 24 letters), asked by a player with
  the default [PlayerCommands] reach and with every Player* setting at "all", by an admin and by
  the console, on an empty database, a small one and a big one (thousands of accounts with many
  names, computers, addresses and visits, long names, places, Steam data, accounts tied together
  and decisions of the admins). Every line is checked: its width, the columns of its table, the
  columns of a grid, numbers and texts that went wrong, and - for a player - an address, a
  computer or a full Steam id of somebody else. No network: every address has its place in the
  file already, and nothing is asked of Steam.
    aflint.exe <antifake_dll.dll>

  Built from the library folder (source_dll), on Windows with FPC 3.2 for i386:
    mkdir build\test
    fpc -Mobjfpc -Sh -O2 -Fu. -FUbuild\test -FEbuild\test tests\aflint.pas
  and run in build\test next to a copy of build\win32\antifake_dll.dll. }

{$mode objfpc}{$H+}

uses
  SysUtils, Classes, dynlibs;

type
  TPathProc = procedure(Path: PChar); cdecl;
  TFlag = function: Boolean; cdecl;
  TCount = function: LongInt; cdecl;
  TLogin = function(Name, SteamID, HWID, IP: PChar; IncLoginCount: Boolean): LongInt; cdecl;
  TLoadIni = function(Path: PChar): Boolean; cdecl;
  TVisitNote = procedure(Index: LongInt; Name, Hwid, IP: PChar; AddSeconds: LongInt); cdecl;
  TSetVerdict = function(A, B, Verdict: LongInt; By: PChar): Boolean; cdecl;
  TRunCmd = function(Slot: LongInt; Cmd, Params: PChar; IsAdmin: Boolean; Who: PChar): LongInt; cdecl;
  TSlotFlag = function(Slot: LongInt): Boolean; cdecl;
  TSlotCount = function(Slot: LongInt): LongInt; cdecl;
  TSlotLine = function(Slot, Index: LongInt): PChar; cdecl;
  TSlotDrop = procedure(Slot, Count: LongInt); cdecl;
  TSelect = procedure(Slot: LongInt); cdecl;
  TSetOnline = procedure(Slot, ProfileIdx: LongInt; Name, IP, Hwid: PChar; IsAdmin: Boolean); cdecl;
  TAddTime = procedure(Index, TimePlayed, TimeSpectator: LongInt); cdecl;

  { the records of the file, as af_types.inc has them }
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
  TSteamInfo = packed record
    SteamId: array[0..23] of Char;
    Persona: array[0..63] of Char;
    Country: array[0..3] of Char;
    Created: Double;
    Fetched: Double;
    Visible: Byte;
    VacBans: Byte;
    GameBans: Byte;
    CommunityBan: Byte;
    TradeBan: Byte;
    DaysSinceLastBan: Word;
    Filler: Word;
  end;

const
  PROFILE_SIZE = 200;
  ENTRY_SIZE = 109;
  VISIT_SIZE = 92;
  STEAM_BASE = Int64(76561197960265728);
  WIDTH = 100;
  { who asks }
  V_PLAYER = 0;      { a player, the [PlayerCommands] of a fresh settings.ini }
  V_PLAYER_ALL = 1;  { a player, every Player* setting at "all" }
  V_ADMIN = 2;
  V_CONSOLE = 3;
  ViewerName: array[0..3] of string = ('player', 'player-all', 'admin', 'console');
  ASKER_SLOT = 2;
  ADMIN_SLOT = 3;
  CONSOLE_SLOT = 33;

var
  Lib: TLibHandle;
  StartLoad, StartSave: TPathProc;
  LoadDone, LoadFailed, SaveDone: TFlag;
  ProfileCount: TCount;
  LoginPlayer: TLogin;
  LoadIni: TLoadIni;
  VisitNote: TVisitNote;
  SetVerdict: TSetVerdict;
  RunCmd: TRunCmd;
  SlotBusy, TaskDone, StartedTask: TSlotFlag;
  OutCount: TSlotCount;
  OutLine: TSlotLine;
  OutDrop: TSlotDrop;
  RenderRes, ClearOnline, ClearTask: TSelect;
  SetOnline: TSetOnline;
  AddTime: TAddTime;

  Violations, Answers, LinesSeen: LongInt;
  { every address, computer and Steam id of the data, with the account it belongs to }
  Secrets: TStringList;
  { every command a line may name }
  KnownWords: TStringList;
  DbName, IniPath, DbPath: string;
  { the players on the server: slot -> account, name, address, computer }
  OnAcc: array[1..32] of LongInt;
  OnName, OnIP, OnHw: array[1..32] of string;
  Pool: array of string;
  LongNick: string;
  Probe: TStringList;

procedure Violation(const Where, What, Line: string);
begin
  Inc(Violations);
  WriteLn('[VIOL] ', Where, ': ', What);
  if Line <> '' then WriteLn('       |', Line, '|');
end;

procedure WaitFlag(F: TFlag);
var k: LongInt;
begin
  k := 0;
  while (k < 3000) and not F() do begin Sleep(10); Inc(k); end;
end;

procedure UseIni(Viewer: LongInt; Extra: string);
var
  F: TStringList;
begin
  F := TStringList.Create;
  try
    F.Add('CommandCooldownSeconds = 0');
    F.Add('HeavyCooldownSeconds = 0');
    F.Add('MaxListsPerSecond = 60');
    F.Add('JournalSeconds = 0');
    F.Add('BackupHours = 0');
    F.Add('MultiAccountNotice = 0');
    F.Add('SteamApiKey = 0000000000000000000000000000LINT');
    F.Add('SteamCurlPath = aflint-no-such-curl');
    F.Add('VisitCooldownMinutes = 0');
    if Viewer = V_PLAYER_ALL then
    begin
      F.Add('PlayerVisits = all');
      F.Add('PlayerVisitStats = all');
      F.Add('PlayerVisitsOf = all');
      F.Add('PlayerStatus = all');
      F.Add('PlayerNames = all');
      F.Add('PlayerSteam = all');
      F.Add('PlayerLocate = all');
      F.Add('PlayerProfiles = all');
      F.Add('PlayerSearch = all');
      F.Add('PlayerMulti = all');
      F.Add('PlayerHelp = all');
      F.Add('PlayerLastVisitors = all');
    end;
    if Extra <> '' then F.Add(Extra);
    F.SaveToFile(IniPath);
  finally F.Free; end;
  LoadIni(PChar(IniPath));
end;

procedure SetStr(var Buf: array of Char; const S: string);
var k: LongInt;
begin
  FillChar(Buf[0], Length(Buf), 0);
  for k := 1 to Length(S) do
    if k <= High(Buf) then Buf[k - 1] := S[k];
end;

{ ---------------- the data ---------------- }

function PoolIP(k: LongInt): string;
begin
  Result := IntToStr(31 + k mod 180) + '.' + IntToStr(1 + (k * 7) mod 250) + '.' +
    IntToStr(1 + (k * 13) mod 250) + '.' + IntToStr(1 + (k * 31) mod 250);
end;

function HwOf(Acc, n: LongInt): string;
begin
  Result := 'HW' + IntToHex(Acc * 16 + n + $A1B2C3, 8);
end;

function SteamOf(Acc: LongInt): string;
begin
  Result := 'S' + IntToStr(100000000 + Acc * 37);
end;

procedure AddSecret(const S: string; Acc: LongInt);
begin
  if S = '' then Exit;
  Secrets.AddObject(S, TObject(PtrInt(Acc)));
end;

{ the file the library wrote, with a place for every address and what Steam said about some
  accounts written into it; read back by the library }
procedure AddPlacesAndSteam(const Path: string; SteamEvery: LongInt);
var
  F: TFileStream;
  Buf, Head, Tail: AnsiString;
  P, Count, i, k, VCount, n: LongInt;
  W: Word;
  G: TGeoEntry;
  St: TSteamInfo;
  Geo: TMemoryStream;
  Magic: Cardinal;
  Countries: array[0..5] of string = ('Poland', 'Germany', 'United Kingdom of Great Britain and Northern Ireland', 'Brazil', 'Sweden', 'United States');
  Codes: array[0..5] of string = ('PL', 'DE', 'GB', 'BR', 'SE', 'US');
begin
  F := TFileStream.Create(Path, fmOpenRead);
  try
    SetLength(Buf, F.Size);
    if F.Size > 0 then F.ReadBuffer(Buf[1], F.Size);
  finally F.Free; end;
  Move(Buf[9], Count, 4);
  P := 13;
  for i := 0 to Count - 1 do
  begin
    n := 0;
    for k := 0 to 3 do
    begin
      Move(Buf[P + 192 + k * 2], W, 2);
      Inc(n, W);
    end;
    Inc(P, PROFILE_SIZE + n * ENTRY_SIZE);
  end;
  Head := Copy(Buf, 1, P - 1);
  Move(Buf[P], n, 4);
  Tail := Copy(Buf, P + 4 + n * SizeOf(TGeoEntry), MaxInt);
  Geo := TMemoryStream.Create;
  try
    n := Length(Pool);
    Geo.WriteBuffer(n, 4);
    for k := 0 to High(Pool) do
    begin
      FillChar(G, SizeOf(G), 0);
      SetStr(G.IP, Pool[k]);
      SetStr(G.Continent, 'Europe');
      SetStr(G.ContinentCode, 'EU');
      SetStr(G.Country, Countries[k mod 6]);
      SetStr(G.CountryCode, Codes[k mod 6]);
      if k mod 5 = 0 then SetStr(G.Region, 'Kujawsko-Pomorskie Voivodeship Region')
      else SetStr(G.Region, 'Mazovia');
      if k mod 4 = 0 then SetStr(G.City, 'Llanfairpwllgwyngyllgogerychwyrndrobwllllantysiliogogogoch')
      else SetStr(G.City, 'Warsaw');
      if k mod 3 = 0 then SetStr(G.ISP, 'Orange Polska Spolka Akcyjna Telecommunications Provider')
      else SetStr(G.ISP, 'Netia');
      if k mod 2 = 0 then SetStr(G.ReverseDNS, 'host-' + IntToStr(k) + '.dynamic.very-long-provider-name.example.com');
      SetStr(G.Zip, '00-' + IntToStr(100 + k mod 900));
      SetStr(G.Timezone, 'Europe/Warsaw');
      G.IsMobile := Ord(k mod 7 = 0);
      G.IsProxy := Ord(k mod 11 = 0);
      G.IsHosting := Ord(k mod 13 = 0);
      G.LastUpdated := Now;
      Geo.WriteBuffer(G, SizeOf(G));
    end;
    { the visits, then Steam: an entry per account }
    P := 1;
    Move(Tail[P], Magic, 4);
    if Magic = $53545356 then
    begin
      Move(Tail[P + 4], n, 4);
      Inc(P, 8);
      for i := 0 to n - 1 do
      begin
        Move(Tail[P], VCount, 4);
        Inc(P, 4 + VCount * VISIT_SIZE);
      end;
    end;
    Move(Tail[P], Magic, 4);
    if Magic = $4D414554 then
    begin
      Move(Tail[P + 4], n, 4);
      Inc(P, 8);
      for i := 0 to n - 1 do
      begin
        if (SteamEvery > 0) and (i mod SteamEvery = 0) then
        begin
          FillChar(St, SizeOf(St), 0);
          SetStr(St.SteamId, IntToStr(STEAM_BASE + 100000000 + i * 37));
          if i mod 3 = 0 then SetStr(St.Persona, 'A Steam Persona Name That Is Quite Long Indeed ' + IntToStr(i))
          else SetStr(St.Persona, 'Pers' + IntToStr(i));
          if i mod 4 <> 1 then SetStr(St.Country, Codes[i mod 6]);
          if i mod 5 <> 2 then St.Created := Now - 4000 - i;
          St.Fetched := Now - 0.5;
          St.Visible := Ord(i mod 2 = 0);
          if i mod 6 = 0 then begin St.VacBans := 3; St.GameBans := 2; St.CommunityBan := 1; St.TradeBan := 1; St.DaysSinceLastBan := 1234; end;
          Move(St, Tail[P + i * SizeOf(TSteamInfo)], SizeOf(St));
        end;
      end;
    end;
    F := TFileStream.Create(Path, fmCreate);
    try
      F.WriteBuffer(Head[1], Length(Head));
      F.WriteBuffer(Geo.Memory^, Geo.Size);
      if Length(Tail) > 0 then F.WriteBuffer(Tail[1], Length(Tail));
    finally F.Free; end;
  finally Geo.Free; end;
end;

{ a database of Accounts accounts (0 = empty), written, given its places and read back; the
  players on the server are the first accounts }
procedure MakeDatabase(Accounts: LongInt);
var
  a, k, Idx, n: LongInt;
  Name, IP: string;
begin
  Secrets.Clear;
  Secrets.Sorted := False;
  if FileExists(DbPath) then DeleteFile(DbPath);
  StartLoad(PChar(DbPath));
  WaitFlag(LoadDone);
  SetLength(Pool, 0);
  if Accounts > 0 then
  begin
    n := Accounts div 2 + 20;
    SetLength(Pool, n);
    for k := 0 to n - 1 do Pool[k] := PoolIP(k);
  end;
  for a := 0 to Accounts - 1 do
  begin
    if a = 5 then Name := LongNick
    else if a mod 97 = 3 then Name := 'Player With A Long Nick' + IntToStr(a mod 10)
    else Name := 'Nick' + IntToStr(a);
    for k := 0 to 2 + a mod 4 do
    begin
      IP := Pool[(a * 3 + k * 5) mod Length(Pool)];
      if (a = 7) and (k = 1) then IP := '192.168.1.' + IntToStr(10 + a mod 200);
      if k = 1 then Idx := LoginPlayer(PChar(Name + '_alt'), PChar(SteamOf(a)), PChar(HwOf(a, k mod 2)), PChar(IP), True)
      else Idx := LoginPlayer(PChar(Name), PChar(SteamOf(a)), PChar(HwOf(a, k mod 2)), PChar(IP), True);
      AddSecret(IP, Idx);
      AddSecret(HwOf(a, k mod 2), Idx);
      AddSecret(SteamOf(a), Idx);
      AddSecret(IntToStr(STEAM_BASE + 100000000 + a * 37), Idx);
    end;
    { accounts that share a computer with the one before them }
    if (a > 0) and (a mod 10 = 0) then
    begin
      Idx := LoginPlayer(PChar(Name), PChar(SteamOf(a)), PChar(HwOf(a - 1, 0)), PChar(Pool[a mod Length(Pool)]), True);
      AddSecret(HwOf(a - 1, 0), Idx);
    end;
  end;
  if Accounts > 0 then
  begin
    { a player who has been here for years }
    for k := 1 to 12000 do LoginPlayer(PChar('Nick1'), PChar(SteamOf(1)), PChar(HwOf(1, 0)), PChar(Pool[(3 + 0) mod Length(Pool)]), True);
    AddTime(1, 90000000, 5000000);
    AddTime(0, 5, 0);
    { a computer shared by a dozen accounts }
    for a := 20 to 32 do
      if a < Accounts then
        LoginPlayer(PChar('Nick' + IntToStr(a)), PChar(SteamOf(a)), PChar('HWSHAREDPC01'), PChar(Pool[a mod Length(Pool)]), True);
    AddSecret('HWSHAREDPC01', 20);
  end;
  StartSave(PChar(DbPath));
  WaitFlag(SaveDone);
  if Accounts > 0 then
  begin
    AddPlacesAndSteam(DbPath, 3);
    StartLoad(PChar(DbPath));
    WaitFlag(LoadDone);
    if LoadFailed() then WriteLn('       the database could not be read back');
    { visits: several per account, from the places of its addresses }
    for a := 0 to Accounts - 1 do
      for k := 0 to 1 + a mod 4 do
      begin
        IP := Pool[(a * 3 + k * 5) mod Length(Pool)];
        VisitNote(a, PChar('Nick' + IntToStr(a)), PChar(HwOf(a, 0)), PChar(IP), 60 + a * 13 + k);
      end;
    { decisions of the admins }
    for a := 1 to 40 do
      if a * 10 < Accounts then SetVerdict(a * 10, a * 10 - 1, 1 + a mod 3, PChar('Dominik the Admin'));
  end;
  Secrets.Sorted := True;
  WriteLn('       database ', DbName, ': ', ProfileCount(), ' accounts');
end;

{ the players on the server: slot 1..6 are the first accounts, the asker is slot 2, the admin 3 }
procedure PutPlayersOn(Accounts: LongInt);
var
  s: LongInt;
begin
  for s := 1 to 32 do
  begin
    ClearOnline(s);
    OnAcc[s] := -1;
    OnName[s] := '';
    OnIP[s] := '';
    OnHw[s] := '';
  end;
  if Accounts = 0 then
  begin
    { nobody has an account yet: the asker is on the server without one }
    OnName[ASKER_SLOT] := 'Newcomer';
    OnIP[ASKER_SLOT] := '83.1.2.3';
    SetOnline(ASKER_SLOT, -1, PChar(OnName[ASKER_SLOT]), PChar(OnIP[ASKER_SLOT]), PChar('HWNEW'), False);
    Exit;
  end;
  for s := 1 to 6 do
  begin
    OnAcc[s] := s - 1;
    if s - 1 = 5 then OnName[s] := LongNick else OnName[s] := 'Nick' + IntToStr(s - 1);
    OnIP[s] := Pool[((s - 1) * 3) mod Length(Pool)];
    OnHw[s] := HwOf(s - 1, 0);
    SetOnline(s, OnAcc[s], PChar(OnName[s]), PChar(OnIP[s]), PChar(OnHw[s]), s = ADMIN_SLOT);
  end;
end;

{ ---------------- asking ---------------- }

function Collect(Slot: LongInt; Into: TStringList): LongInt;
var k, n: LongInt;
begin
  n := OutCount(Slot);
  for k := 0 to n - 1 do Into.Add(StrPas(OutLine(Slot, k)));
  OutDrop(Slot, n);
  Result := n;
end;

procedure WaitIdle(Slot: LongInt);
var k: LongInt;
begin
  k := 0;
  while (k < 3000) and SlotBusy(Slot) do begin Sleep(2); Inc(k); end;
end;

{ a command the way the script hands it over, and every line of the answer, the table of a
  search included }
function Ask(Viewer: LongInt; const C, P: string; Into: TStringList): LongInt;
var
  Slot, k: LongInt;
  Admin: Boolean;
begin
  Into.Clear;
  case Viewer of
    V_ADMIN: Slot := ADMIN_SLOT;
    V_CONSOLE: Slot := CONSOLE_SLOT;
  else Slot := ASKER_SLOT;
  end;
  Admin := Viewer >= V_ADMIN;
  Result := RunCmd(Slot, PChar(C), PChar(P), Admin, PChar('aflint'));
  if Result = 0 then Exit;
  WaitIdle(Slot);
  Collect(Slot, Into);
  if StartedTask(Slot) then
  begin
    k := 0;
    while (k < 3000) and not TaskDone(Slot) do begin Sleep(2); Inc(k); end;
    RenderRes(Slot);
    WaitIdle(Slot);
    Collect(Slot, Into);
  end;
end;

{ ---------------- the checks ---------------- }

function IsDigits(const S: string): Boolean;
var k: LongInt;
begin
  Result := S <> '';
  for k := 1 to Length(S) do
    if not (S[k] in ['0'..'9']) then begin Result := False; Exit; end;
end;

{ the words of a line that can be a secret: split at spaces and the signs around values }
procedure Tokens(const Line: string; Into: TStringList);
var
  k: LongInt;
  Cur: string;
begin
  Into.Clear;
  Cur := '';
  for k := 1 to Length(Line) do
    if Line[k] in [' ', '|', ',', '(', ')', '[', ']', ';', '"', '='] then
    begin
      if Cur <> '' then Into.Add(Cur);
      Cur := '';
    end
    else Cur := Cur + Line[k];
  if Cur <> '' then Into.Add(Cur);
end;

function TrimEnd(const S: string): string;
var n: LongInt;
begin
  n := Length(S);
  while (n > 0) and (S[n] in [' ', '.', ',', ')', ':', ';']) do Dec(n);
  Result := Copy(S, 1, n);
end;

{ the places of the " | " of a table row, as text }
function BarPositions(const Line: string; Most: LongInt): string;
var k, n: LongInt;
begin
  Result := '';
  n := 0;
  k := 1;
  while k <= Length(Line) - 2 do
  begin
    if (Line[k] = ' ') and (Line[k + 1] = '|') and (Line[k + 2] = ' ') then
    begin
      Result := Result + IntToStr(k) + ',';
      Inc(n);
      if n >= Most then Exit;
      Inc(k, 3);
    end
    else Inc(k);
  end;
end;

function BarCount(const Line: string): LongInt;
var k: LongInt;
begin
  Result := 0;
  k := 1;
  while k <= Length(Line) - 2 do
  begin
    if (Line[k] = ' ') and (Line[k + 1] = '|') and (Line[k + 2] = ' ') then
    begin
      Inc(Result);
      Inc(k, 3);
    end
    else Inc(k);
  end;
end;

{ the cells of a row }
procedure Cells(const Line: string; Into: TStringList);
var k, Start: LongInt;
begin
  Into.Clear;
  Start := 1;
  k := 1;
  while k <= Length(Line) - 2 do
  begin
    if (Line[k] = ' ') and (Line[k + 1] = '|') and (Line[k + 2] = ' ') then
    begin
      Into.Add(Trim(Copy(Line, Start, k - Start)));
      Inc(k, 3);
      Start := k;
    end
    else Inc(k);
  end;
  Into.Add(Trim(Copy(Line, Start, MaxInt)));
end;

procedure CheckAnswer(Viewer: LongInt; const C, P: string; Lines: TStringList);
var
  i, j, k, Own, Owner, HeadBars, Col: LongInt;
  Where, L, T, Head, W: string;
  Toks, HeadCells, RowCells: TStringList;
  InGrid, IsHelp: Boolean;
  GridW: LongInt;
begin
  Inc(Answers);
  Where := DbName + ' / ' + ViewerName[Viewer] + ' / ' + C;
  if P <> '' then Where := Where + ' ' + P;
  if Lines.Count = 0 then
  begin
    Violation(Where, 'the answer is empty', '');
    Exit;
  end;
  Own := -1;
  if Viewer <= V_PLAYER_ALL then Own := OnAcc[ASKER_SLOT];
  IsHelp := (C = 'antifake') or (C = 'af_help') or (C = 'afhelp');
  Toks := TStringList.Create;
  HeadCells := TStringList.Create;
  RowCells := TStringList.Create;
  try
    Head := '';
    HeadBars := 0;
    InGrid := False;
    GridW := 33;
    for i := 0 to Lines.Count - 1 do
    begin
      L := Lines[i];
      Inc(LinesSeen);
      if Length(L) > WIDTH then Violation(Where, 'longer than ' + IntToStr(WIDTH) + ' (' + IntToStr(Length(L)) + ')', L);
      if Trim(L) = '' then Violation(Where, 'a line with nothing on it', L);
      { an exception the worker caught while it answered }
      if Pos('Something went wrong', L) > 0 then Violation(Where, 'an error while answering', L);
      T := LowerCase(L);
      if (Pos(' nan', T) > 0) or (Pos(' inf', T) > 0) or (Pos('%%', L) > 0) or (Pos('%d', L) > 0) or
         (Pos('%s', L) > 0) or (Pos(' -0 ', L) > 0) or (Pos('-0.0', L) > 0) then
        Violation(Where, 'a number or a format that went wrong', L);
      T := TrimRight(L);
      if (Copy(T, Length(T) - 1, 2) = '()') or ((T <> '') and (T[Length(T)] = ':')) then
        Violation(Where, 'a value is missing at the end', L);
      { a sentence, not a table, a grid, a legend, a key before its value or the line of a
        place under its row (those space their parts on purpose) }
      if Pos('  ', Trim(L)) > 0 then
        if (Pos(' | ', L) = 0) and (L[1] <> '!') and (L[1] <> '/') and (L[1] <> '#') and
           (L[1] <> ' ') and not IsHelp and (Pos('  :', L) = 0) and (Pos(' : ', L) = 0) and
           (Pos('  [', L) = 0) and
           (Pos('---', L) = 0) and (Pos('===', L) = 0) and (Pos('   ', Trim(L)) = 0) then
          Violation(Where, 'two spaces inside a sentence', L);
      { a secret of somebody else, for a player }
      Tokens(L, Toks);
      for j := 0 to Toks.Count - 1 do
      begin
        W := Toks[j];
        if (Viewer <= V_PLAYER_ALL) then
        begin
          k := Secrets.IndexOf(W);
          if k >= 0 then
          begin
            Owner := LongInt(PtrInt(Secrets.Objects[k]));
            { the same value may belong to several accounts: it is the viewer's own when one
              of them is the viewer's }
            while (k > 0) and (Secrets[k - 1] = W) do Dec(k);
            while (k < Secrets.Count) and (Secrets[k] = W) do
            begin
              if LongInt(PtrInt(Secrets.Objects[k])) = Own then Owner := Own;
              Inc(k);
            end;
            if Owner <> Own then Violation(Where, 'a player is shown ' + W + ' of account #' + IntToStr(Owner), L);
          end;
        end;
        { a command that does not exist }
        if (Length(W) > 1) and ((W[1] = '/') or (W[1] = '!')) and (W[2] in ['a'..'z']) then
        begin
          T := TrimEnd(Copy(W, 2, MaxInt));
          if KnownWords.IndexOf(T) < 0 then
            Violation(Where, 'names a command that does not exist: ' + W, L);
        end;
      end;
      { tables: every row of a table has its " | " where the head has them }
      if Pos(' | ', L) > 0 then
      begin
        if (Head = '') or (BarCount(L) < HeadBars) then
        begin
          if (Head <> '') and (BarCount(L) < HeadBars) and (i > 0) and (Pos(' | ', Lines[i - 1]) > 0) then
            Violation(Where, 'a row has fewer columns than its table', L);
          Head := L;
          HeadBars := BarCount(L);
          Cells(L, HeadCells);
        end
        else if BarPositions(L, HeadBars) <> BarPositions(Head, HeadBars) then
          Violation(Where, 'the columns do not stand under the head of the table (' +
            BarPositions(Head, HeadBars) + ' / ' + BarPositions(L, HeadBars) + ')', L)
        else
        begin
          { a counter of 10 000 or more in full }
          Cells(L, RowCells);
          for Col := 0 to HeadCells.Count - 1 do
            if Col < RowCells.Count then
              if (HeadCells[Col] = 'Logins') or (HeadCells[Col] = 'Visits') or
                 (HeadCells[Col] = 'Accounts') or (HeadCells[Col] = 'Tied') or (HeadCells[Col] = 'Names') then
                if IsDigits(RowCells[Col]) and (Length(RowCells[Col]) >= 5) then
                  Violation(Where, 'a counter printed in full: ' + HeadCells[Col] + ' ' + RowCells[Col], L);
        end;
      end
      else if (Trim(L) <> '') and (L[1] <> ' ') then
      begin
        Head := '';
        HeadBars := 0;
      end;
      { grids: the help (columns of 33) and the tied accounts of a status (columns of 25) }
      if (Pos('----', L) > 0) or (Pos('====', L) > 0) then
      begin
        InGrid := IsHelp or (Pos('share something', L) > 0);
        if Pos('share something', L) > 0 then GridW := 25 else GridW := 33;
        if (Pos('====', L) > 0) and (i > 0) then InGrid := False;
        if IsHelp and (Pos('Pages', L) = 0) and (Pos('Admin', L) = 0) and (Pos('Your', L) = 0) and
           (Pos('server', L) = 0) and (Pos('other players', L) = 0) then InGrid := False;
        Continue;
      end;
      if InGrid and (L <> '') and ((L[1] = '!') or (L[1] = '/') or (L[1] = '#') or (L[1] = '(')) then
      begin
        { every item starts at a column, and two items leave two spaces at least }
        k := 1;
        while k <= Length(L) do
        begin
          if (L[k] <> ' ') and ((k = 1) or (L[k - 1] = ' ')) and ((k = 1) or ((k >= 3) and (L[k - 2] = ' '))) then
            if ((k - 1) mod GridW) <> 0 then
              Violation(Where, 'a grid item does not start at a column (' + IntToStr(k - 1) + ')', L);
          if (L[k] <> ' ') and (k > 2) and (L[k - 1] = ' ') and (L[k - 2] <> ' ') and (((k - 1) mod GridW) = 0) then
            Violation(Where, 'two grid items touch (one space between them)', L);
          Inc(k);
        end;
      end
      else if InGrid and (L <> '') and not IsHelp then InGrid := False;
    end;
  finally
    Toks.Free;
    HeadCells.Free;
    RowCells.Free;
  end;
end;

{ ---------------- the commands ---------------- }

const
  { every command of the help, with what it takes; in the arguments o = another player's name,
    i = #id of another account, l = the long name, n = a name that matches nothing, s = the
    slot of another player, m = the asker's own name (each in curly brackets) }
  Commands: array[0..131] of string = (
    'antifake|', 'af_help|', 'afhelp|',
    'mystatus|', 'mynames|', 'mynames|count', 'mynames|latest',
    'visits|', 'visits|7d', 'visits|all masked', 'visits|clean bycountry', 'visits|2026-01-01..2026-12-31',
    'visits|{o}', 'visits|{o} 30d', 'visits|{m}', 'visits|{n}', 'visits|{l}', 'visits|{i}', 'visitsp|1', 'visitsid|{s}', 'visitsi|1',
    'uniquevisits|', 'uniquevisits|{o}', 'uniquevisits|all', 'uniquevisitsp|1', 'uniquevisitsid|{s}',
    'locations|', 'locations|{o}', 'locations|30d', 'locationsp|1', 'locationsid|{s}',
    'steam|', 'steam|{o}', 'steam|{i}', 'steam|{n}', 'steamp|1', 'steamid|{s}', 'mysteam|',
    'locate|', 'locate|{o}', 'locate|{n}', 'locateid|{s}', 'locateall|',
    'visitstats|', 'visitstats|7d', 'visitmetrics|all',
    'visitsof|', 'visitsof|PL', 'visitsof|Poland 30d', 'visitsof|XX',
    'lastvisits|', 'lastvisits|7d', 'lastvisits|{o}', 'lastvisitors|', 'lastvisitors|all',
    'online|', 'showid|', 'listprofiles|', 'listprofiles|time', 'listprofiles|latest',
    'statusa|', 'statusa|{o}', 'statusa|{i}', 'statusa|{n}', 'statusa|{l}', 'statusap|1', 'statusaid|{s}', 'af_status|{o}', 'afstatus|',
    'privacy|', 'privacy|n {o}', 'privacy|s {o}', 'privacy|h {o}', 'privacy|i {o}', 'privacy|x {o}', 'privacyp|i 1', 'privacyid|h {s}',
    'privacyall|n {i}', 'privacyall|i {o} count',
    'af_search|', 'af_search|Nick', 'af_search|{n}', 'afsearch|{l}', 'searchall|alt', 'searchall|',
    'smartsearch|', 'smartsearch|Nik1', 'smartsearch|jaro 60 all Nick', 'smartsearch|token {n}',
    'checkp|', 'checkp|i {o}', 'checkp|h {o}', 'checkp|n {i}', 'checkd|', 'checkd|n Nick1', 'checkd|i 3', 'checkd|s S1',
    'geosearch|', 'geosearch|pl', 'geosearch|warsaw proxy:1', 'showlocate|', 'showlocate|i {o}', 'showlocate|h {i}',
    'multi|',
    { round 3: the sorts of the list, the list of visitors in the chat, names in double quotes }
    'listprofiles|name', 'listprofiles|logins asc', 'listprofiles|first', 'listprofiles|last',
    'listprofiles|pcs desc', 'listprofiles|desc', 'listprofiles|steam', 'listprofiles|foo', 'listprofiles|time name',
    '!lastvisitors|', '!lastvisitors|7d', 'lastvisitors|masked', 'lastvisitors|{o}',
    'statusa|"{o}"', 'statusa|#1', 'visits|"{o}" 30d', 'locate|"{o}"', 'steam|"{o}"', 'privacy|n "{o}" count',
    'checkp|n "{o}"', 'showlocate|i "{o}"',
    'checkd|n "Nick1"', 'af_search|"Nick"', 'searchall|"alt"', 'smartsearch|"Nik1"',
    'visitsof|United States', 'visitsof|"United Kingdom of Great Britain and Northern Ireland" 30d',
    'visitsof|Middle Earth');
  AdminOnly: array[0..20] of string = (
    'multi|{o}', 'multi|{i}', 'multi|{n}', 'multi|strong', 'multi|top', 'multi|topaddresses', 'multi|stats',
    'multi|count', 'multip|1', 'multiid|{s}',
    'multiset|', 'multiset|1 2 same', 'multiset|1 2 clear', 'multiset|1 1 same', 'multiset|1 2 maybe',
    'steamstatus|', 'storage|', 'storage|backups',
    'multi|"{o}"', 'multi|Top Gun', 'multi|#99999');
  AdminMore: array[0..2] of string = ('storage|restore', 'storage|restore nothing-here.dat', 'storage|mirror');
  Pages: array[0..4] of string = ('af_next|', 'af_prev|', 'af_page|999', 'af_page|1', 'afnext|');

function Expand(const S: string): string;
var
  k: LongInt;
begin
  Result := S;
  k := Pos('{o}', Result);
  if k > 0 then Result := Copy(Result, 1, k - 1) + OnName[4] + Copy(Result, k + 3, MaxInt);
  k := Pos('{m}', Result);
  if k > 0 then Result := Copy(Result, 1, k - 1) + OnName[ASKER_SLOT] + Copy(Result, k + 3, MaxInt);
  k := Pos('{i}', Result);
  if k > 0 then Result := Copy(Result, 1, k - 1) + '#4' + Copy(Result, k + 3, MaxInt);
  k := Pos('{l}', Result);
  if k > 0 then Result := Copy(Result, 1, k - 1) + LongNick + Copy(Result, k + 3, MaxInt);
  k := Pos('{n}', Result);
  if k > 0 then Result := Copy(Result, 1, k - 1) + 'zzqqxxnothing' + Copy(Result, k + 3, MaxInt);
  k := Pos('{s}', Result);
  if k > 0 then Result := Copy(Result, 1, k - 1) + '5' + Copy(Result, k + 3, MaxInt);
end;

procedure RunOne(Viewer: LongInt; const Spec: string; Lines: TStringList; AfterList: Boolean);
var
  C, P: string;
  q, k, Answer: LongInt;
begin
  q := Pos('|', Spec);
  C := Copy(Spec, 1, q - 1);
  P := Expand(Copy(Spec, q + 1, MaxInt));
  Answer := Ask(Viewer, C, P, Lines);
  if Answer = 0 then
  begin
    Violation(DbName + ' / ' + ViewerName[Viewer] + ' / ' + C, 'not answered at all (not a command)', '');
    Exit;
  end;
  CheckAnswer(Viewer, C, P, Lines);
  { the pages of the list it made }
  if AfterList then
    for k := 0 to High(Pages) do
    begin
      q := Pos('|', Pages[k]);
      Ask(Viewer, Copy(Pages[k], 1, q - 1), Copy(Pages[k], q + 1, MaxInt), Lines);
      CheckAnswer(Viewer, C + ' then ' + Copy(Pages[k], 1, q - 1), Copy(Pages[k], q + 1, MaxInt), Lines);
    end;
end;

procedure RunAll(Accounts: LongInt);
var
  Viewer, k: LongInt;
  Lines: TStringList;
  Extra: string;
begin
  MakeDatabase(Accounts);
  PutPlayersOn(Accounts);
  Lines := TStringList.Create;
  try
    for Viewer := V_PLAYER to V_CONSOLE do
    begin
      Extra := '';
      UseIni(Viewer, Extra);
      ClearTask(ASKER_SLOT);
      ClearTask(ADMIN_SLOT);
      ClearTask(CONSOLE_SLOT);
      for k := 0 to High(Commands) do
        RunOne(Viewer, Commands[k], Lines, (Pos('search', Commands[k]) > 0) or
          (Pos('listprofiles', Commands[k]) = 1) or (Pos('lastvisit', Commands[k]) > 0));
      if Viewer >= V_ADMIN then
      begin
        for k := 0 to High(AdminOnly) do
          RunOne(Viewer, AdminOnly[k], Lines, (AdminOnly[k] = 'multi|') or (AdminOnly[k] = 'multi|top'));
        for k := 0 to High(AdminMore) do RunOne(Viewer, AdminMore[k], Lines, False);
        UseIni(Viewer, 'Engine = mirror');
        RunOne(Viewer, 'antifake|', Lines, False);
        RunOne(Viewer, 'storage|', Lines, False);
        UseIni(Viewer, 'Enabled = 0');
        RunOne(Viewer, 'antifake|', Lines, False);
      end
      else
      begin
        { the admin commands, refused }
        for k := 0 to 3 do RunOne(Viewer, AdminOnly[10 + k], Lines, False);
        RunOne(Viewer, 'steamstatus|', Lines, False);
        RunOne(Viewer, 'storage|', Lines, False);
      end;
    end;
  finally Lines.Free; end;
end;

procedure AddKnown;
const
  Words: array[0..62] of string = ('af_search', 'afsearch', 'searchall', 'smartsearch', 'advancedsearch',
    'privacy', 'privacyall', 'checkp', 'checkd', 'statusa', 'af_status', 'afstatus', 'mystatus', 'mynames',
    'listprofiles', 'listprofilesonline', 'listonline', 'online', 'showid', 'showprofileid', 'visits',
    'uniquevisits', 'uvisits', 'locations', 'visitstats', 'visitmetrics', 'visitsof', 'whofrom', 'locate',
    'locateall', 'showlocate', 'geosearch', 'steam', 'mysteam', 'steamstatus', 'multi', 'multiaccounts',
    'multiset', 'setmulti', 'storage', 'afstorage', 'antifake', 'af_help', 'afhelp', 'af_next', 'afnext',
    'af_prev', 'afprev', 'af_page', 'afpage', 'lastvisits', 'lastvisitors',
    'visitsp', 'visitsid', 'visitsi', 'uniquevisitsp', 'uniquevisitsid', 'uniquevisitsi', 'locationsp',
    'locationsid', 'locationsi', 'statusap', 'statusaid');
  More: array[0..17] of string = ('statusai', 'steamp', 'steamid', 'steami', 'multip', 'multiid', 'multii',
    'locatep', 'locateid', 'locatei', 'privacyp', 'privacyid', 'privacyi', 'privacyallp', 'privacyallid',
    'privacyalli', 'checkpp', 'showlocatep');
var k: LongInt;
begin
  for k := 0 to High(Words) do KnownWords.Add(Words[k]);
  for k := 0 to High(More) do KnownWords.Add(More[k]);
  KnownWords.Add('checkpid');
  KnownWords.Add('checkpi');
  KnownWords.Add('showlocateid');
  KnownWords.Add('showlocatei');
  { the forms a usage text writes with <...> after them }
  KnownWords.Add('command');
  { the ranking of ZitroStats, which the <command>i forms read }
  KnownWords.Add('top');
end;

begin
  Lib := LoadLibrary(ParamStr(1));
  if Lib = NilHandle then begin WriteLn('[FAIL] the library does not load'); Halt(1); end;
  StartLoad := TPathProc(GetProcedureAddress(Lib, 'Start_Load_Binary'));
  StartSave := TPathProc(GetProcedureAddress(Lib, 'Start_Save_Binary'));
  LoadDone := TFlag(GetProcedureAddress(Lib, 'Is_Load_Complete'));
  LoadFailed := TFlag(GetProcedureAddress(Lib, 'Is_Load_Error'));
  SaveDone := TFlag(GetProcedureAddress(Lib, 'Is_Save_Complete'));
  ProfileCount := TCount(GetProcedureAddress(Lib, 'Get_Profile_Count'));
  LoginPlayer := TLogin(GetProcedureAddress(Lib, 'Login_Player'));
  LoadIni := TLoadIni(GetProcedureAddress(Lib, 'Load_Settings'));
  VisitNote := TVisitNote(GetProcedureAddress(Lib, 'Visit_Note'));
  SetVerdict := TSetVerdict(GetProcedureAddress(Lib, 'Set_Multi_Verdict'));
  RunCmd := TRunCmd(GetProcedureAddress(Lib, 'Run_Command'));
  SlotBusy := TSlotFlag(GetProcedureAddress(Lib, 'Command_Busy'));
  TaskDone := TSlotFlag(GetProcedureAddress(Lib, 'Is_Task_Complete'));
  StartedTask := TSlotFlag(GetProcedureAddress(Lib, 'Command_Started_Task'));
  OutCount := TSlotCount(GetProcedureAddress(Lib, 'Out_Count'));
  OutLine := TSlotLine(GetProcedureAddress(Lib, 'Out_Line'));
  OutDrop := TSlotDrop(GetProcedureAddress(Lib, 'Out_Drop'));
  RenderRes := TSelect(GetProcedureAddress(Lib, 'Render_Result'));
  ClearOnline := TSelect(GetProcedureAddress(Lib, 'Clear_Online'));
  ClearTask := TSelect(GetProcedureAddress(Lib, 'Clear_Task_Result'));
  SetOnline := TSetOnline(GetProcedureAddress(Lib, 'Set_Online'));
  AddTime := TAddTime(GetProcedureAddress(Lib, 'Add_Profile_Time'));
  if not (Assigned(StartLoad) and Assigned(RunCmd) and Assigned(OutDrop) and Assigned(SetOnline) and
    Assigned(AddTime) and Assigned(ClearTask)) then
  begin
    WriteLn('[FAIL] an export is missing');
    Halt(1);
  end;
  Secrets := TStringList.Create;
  Secrets.CaseSensitive := True;
  Secrets.Duplicates := dupAccept;
  KnownWords := TStringList.Create;
  KnownWords.Sorted := True;
  AddKnown;
  IniPath := ExpandFileName('aflint.ini');
  { relative, as the script names it (nickreg/antifake.dat): /storage shows the path }
  DbPath := 'aflint.dat';
  LongNick := 'Abcdefghijklmnopqrstuvwx';   { 24 letters, the longest nick Soldat takes }
  Violations := 0;
  Answers := 0;
  LinesSeen := 0;

  DbName := 'empty';
  RunAll(0);
  DbName := 'small';
  RunAll(12);
  { a country of several words is taken whole: by its code in the places located so far }
  UseIni(V_ADMIN, '');
  Probe := TStringList.Create;
  try
    Ask(V_ADMIN, 'visitsof', '"United States" 30d', Probe);
    if Pos('come from United States (US)', Probe.Text) = 0 then
      Violation('small / admin / visitsof "United States" 30d', 'the country of two words was not taken whole', '');
    Ask(V_ADMIN, 'visitsof', 'United Kingdom of Great Britain and Northern Ireland', Probe);
    if Pos('come from United Kingdom of Great Britain and Northern Ireland (GB)', Probe.Text) = 0 then
      Violation('small / admin / visitsof United Kingdom ...', 'the country of seven words was not taken whole', '');
  finally Probe.Free; end;
  DbName := 'big';
  RunAll(3000);

  WriteLn(Answers, ' answers, ', LinesSeen, ' lines checked');
  if FileExists(IniPath) then DeleteFile(IniPath);
  if FileExists(DbPath) then DeleteFile(DbPath);
  if FileExists(DbPath + '.bak') then DeleteFile(DbPath + '.bak');
  if Violations = 0 then
    WriteLn('ALL TESTS PASSED')
  else
    WriteLn(Violations, ' VIOLATION(S)');
  Halt(Violations);
end.
