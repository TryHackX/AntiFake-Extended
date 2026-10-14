program aftest;

{ The AntiFake library on its own, without a game server and without the network: the file and
  its journal, the reading of broken files, the settings, the cleanup, the searches, the places,
  the multi-account links, the notices and the command names. Local addresses must never start
  lookup threads, however often the script asks. Windows only (it counts the threads of the
  process).
    aftest.exe <antifake_dll.dll> [a copy of a real database file, only read]

  Built from the library folder (source_dll), on Windows with FPC 3.2 for i386:
    mkdir build\test
    fpc -Mobjfpc -Sh -O2 -Fu. -FUbuild\test -FEbuild\test tests\aftest.pas
  and run in build\test next to a copy of build\win32\antifake_dll.dll. }

{$mode objfpc}{$H+}

uses
  Windows, SysUtils, Classes, Math, dynlibs;

type
  TThreadEntry32 = record
    dwSize, cntUsage, th32ThreadID, th32OwnerProcessID: DWORD;
    tpBasePri, tpDeltaPri: LongInt;
    dwFlags: DWORD;
  end;
  TGeoText = function(IP: PChar): PChar; cdecl;
  TGeoCheck = procedure(IP: PChar); cdecl;
  TPathProc = procedure(Path: PChar); cdecl;
  TFlag = function: Boolean; cdecl;
  TCount = function: LongInt; cdecl;
  TLogin = function(Name, SteamID, HWID, IP: PChar; IncLoginCount: Boolean): LongInt; cdecl;
  TNameOf = function(Index: LongInt): PChar; cdecl;
  TRollback = procedure(ProfileIdx: LongInt; Name, HWID, IP: PChar; Flags: LongInt); cdecl;
  TStartTask = procedure(PlayerID, TaskType, Kind, SortMethod, TargetIndex, MaxResults: LongInt;
    SearchPhrase: PChar; SearchAll, ComparePartial, FilterFirst, FilterLast, FilterAll: Boolean); cdecl;
  TTaskFlag = function(PlayerID: LongInt): Boolean; cdecl;
  TSelect = procedure(PlayerID: LongInt); cdecl;
  TItemText = function(ItemIdx: LongInt): PChar; cdecl;
  TNameLookup = function(Key: PChar): LongInt; cdecl;
  TLoadIni = function(Path: PChar): Boolean; cdecl;
  TSettingInt = function(Name: PChar): LongInt; cdecl;
  TSettingStr = function(Name: PChar): PChar; cdecl;
  TMaint = function(Force: Boolean): Boolean; cdecl;
  TReport = function: PChar; cdecl;
  TConv = function(Text: PChar): PChar; cdecl;
  TVisitNote = procedure(Index: LongInt; Name, Hwid, IP: PChar; AddSeconds: LongInt); cdecl;
  TVisitCount = function(Index: LongInt): LongInt; cdecl;
  TItemCount = function(ItemIdx: LongInt): LongInt; cdecl;
  TVoidProc = procedure; cdecl;
  TRestore = function(Name: PChar): Boolean; cdecl;
  TMultiLinks = function(ProfileIdx, MaxNames: LongInt): PChar; cdecl;
  TSetVerdict = function(A, B, Verdict: LongInt; By: PChar): Boolean; cdecl;
  TGetVerdict = function(A, B: LongInt): PChar; cdecl;
  TRunCmd = function(Slot: LongInt; Cmd, Params: PChar; IsAdmin: Boolean; Who: PChar): LongInt; cdecl;
  TSlotFlag = function(Slot: LongInt): Boolean; cdecl;
  TSlotCount = function(Slot: LongInt): LongInt; cdecl;
  TSlotLine = function(Slot, Index: LongInt): PChar; cdecl;
  TSetOnline = procedure(Slot, ProfileIdx: LongInt; Name, IP, Hwid: PChar; IsAdmin: Boolean); cdecl;
  TAddTime = procedure(Index, TimePlayed, TimeSpectator: LongInt); cdecl;
  TMultiNotice = function(ProfileIdx: LongInt; PlayerName: PChar): PChar; cdecl;

function CreateToolhelp32Snapshot(Flags, Pid: DWORD): THandle; stdcall; external 'kernel32.dll';
function Thread32First(Snap: THandle; var Entry: TThreadEntry32): BOOL; stdcall; external 'kernel32.dll';
function Thread32Next(Snap: THandle; var Entry: TThreadEntry32): BOOL; stdcall; external 'kernel32.dll';

var
  Failures: Integer = 0;

procedure Check(const Name: string; Ok: Boolean);
begin
  if Ok then
    WriteLn('[ OK ] ', Name)
  else
  begin
    WriteLn('[FAIL] ', Name);
    Inc(Failures);
  end;
end;

function ThreadCount: LongInt;
var
  Snap: THandle;
  E: TThreadEntry32;
begin
  Result := 0;
  Snap := CreateToolhelp32Snapshot(4, 0);
  E.dwSize := SizeOf(E);
  if Thread32First(Snap, E) then
    repeat
      if E.th32OwnerProcessID = GetCurrentProcessId then
        Inc(Result);
      E.dwSize := SizeOf(E);
    until not Thread32Next(Snap, E);
  CloseHandle(Snap);
end;

var
  Lib: TLibHandle;
  ZLib: TLibHandle;
  JoinString, GeoString: TGeoText;
  CheckGeo: TGeoCheck;
  StartSave, StartLoad: TPathProc;
  SaveDone, SaveFailed, SaveBusy, LoadDone, LoadFailed, LoadBusy, SaveAfterLoad: TFlag;
  ProfileCount: TCount;
  LoginPlayer: TLogin;
  ProfileName: TNameOf;
  Rollback: TRollback;
  StartTask: TStartTask;
  TaskDone: TTaskFlag;
  SelectResult, ClearTask: TSelect;
  ResultCount: TCount;
  ItemName: TItemText;
  Rows, Empty: LongInt;
  ByHwid, BySteam: TNameLookup;
  LoadIni: TLoadIni;
  SettingInt: TSettingInt;
  SettingStr: TSettingStr;
  Maint: TMaint;
  MaintReport: TReport;
  Conv: TConv;
  VisitNote: TVisitNote;
  VisitCount: TVisitCount;
  Keeper: LongInt;
  IpCount: TItemCount;
  IniPath, SteamIni: AnsiString;
  SteamNote: TPathProc;
  Similar: TItemCount;
  MultiLinks: TMultiLinks;
  SetVerdict: TSetVerdict;
  GetVerdict: TGetVerdict;
  MultiStats: TReport;
  JournalFlush: TVoidProc;
  LoadNote, NoticeLine: TReport;
  MysqlStatus: TReport;
  MysqlIni: AnsiString;
  JournalStatus, BackupList: TReport;
  RestoreBackup: TRestore;
  JrnIni, JrnPath, NewDb, NewJrn: AnsiString;
  RunCmd: TRunCmd;
  SlotBusy: TSlotFlag;
  JournalFinal: TFlag;
  ItemIndex: TItemCount;
  MultiIni: AnsiString;
  Alone, One, Two: LongInt;
  Polish, Plain: AnsiString;
  Older: LongInt;
  SteamTick: TVoidProc;
  SteamInfo: TNameOf;
  SteamStatus: TReport;
  L: TStringList;
  T0, Build, Lookup: QWord;
  Ok: Boolean;
  Before, Peak, n, i: LongInt;
  S, DbPath, BakPath: AnsiString;
  JoinSilent, LocateLocal: Boolean;
  SizeBefore: Int64;
  OutCount: TSlotCount;
  OutLine: TSlotLine;
  SetOnline: TSetOnline;
  RenderRes: TSelect;
  HkIdx, UpIdx, AIdx, ZzTwo, ZzOne: LongInt;
  Said: AnsiString;
  { the audit of 2026-09-23 }
  AuditIni, LongName, Line: AnsiString;
  { the audit of 2026-10-06 }
  LogLine: TPathProc;
  LogMark, LogText: AnsiString;
  LogWait: Integer;
  LoginResult: TVisitCount;
  ProfileHwid, PlayedOf: TNameOf;
  AddTime: TAddTime;
  MultiNoticeFn: TMultiNotice;
  NetA, NetB, ShX, ShY, MnA, MnB: LongInt;
  Played: Int64;
  Lines: TStringList;
  SrcF, DstF: TFileStream;
  { round 3 of 2026-09-23 }
  LoadMillis: TCount;
  StartedTask: TSlotFlag;
  ClearOnline: TSelect;
  R3Db: AnsiString;
  RGhost, REcho, RDelta, RAlpha, RCharlie, RBravo: LongInt;
  RHash1, RHashKowal, RAt2, RCaret1, RTopGun, RTop, RCount, RBigTime, RBig, RHash99, RDots: LongInt;
  RSchoolA, RSchoolB: LongInt;

  { settings.ini for one test: the given lines, and nothing else }
  procedure UseIni(const Text: array of AnsiString);
  var k: LongInt; F: TStringList;
  begin
    F := TStringList.Create;
    try
      for k := 0 to High(Text) do F.Add(Text[k]);
      F.SaveToFile(AuditIni);
    finally F.Free; end;
    LoadIni(PChar(AuditIni));
  end;

  { the next notice for slot 0 (every admin and the console), '' when none came in Millis }
  function AdminNotice(Millis: LongInt): AnsiString;
  var T: LongInt; N: AnsiString;
  begin
    Result := '';
    T := 0;
    repeat
      N := StrPas(NoticeLine());
      if Copy(N, 1, 2) = '0|' then
      begin
        Result := N;
        Exit;
      end;
      if N = '' then begin Sleep(10); Inc(T, 10); end;
    until T >= Millis;
  end;

  procedure WaitSave;
  var T: LongInt;
  begin
    T := 0;
    while (T < 500) and not SaveDone() do begin Sleep(20); Inc(T); end;
  end;

  procedure WaitLoad;
  var T: LongInt;
  begin
    T := 0;
    while (T < 500) and not LoadDone() do begin Sleep(20); Inc(T); end;
  end;

  function FileLen(const Path: AnsiString): Int64;
  var F: file of Byte;
  begin
    Result := -1;
    if not FileExists(Path) then Exit;
    AssignFile(F, Path); Reset(F);
    Result := FileSize(F);
    CloseFile(F);
  end;

  { the copies of a database in the backup folder beside it }
  function CountBackups(const Path: AnsiString): LongInt;
  var SR: TSearchRec;
  begin
    Result := 0;
    if FindFirst(ExtractFilePath(Path) + 'backup' + PathDelim + 'antifake-*.dat', faAnyFile, SR) = 0 then
    begin
      repeat
        Inc(Result);
      until FindNext(SR) <> 0;
      FindClose(SR);
    end;
  end;

  procedure Truncate(const Path: AnsiString; Keep: Int64);
  var Src, Dst: TFileStream; Buf: array[0..4095] of Byte; Todo, Chunk: Int64;
  begin
    Src := TFileStream.Create(Path, fmOpenRead or fmShareDenyNone);
    Dst := TFileStream.Create(Path + '.cut', fmCreate);
    try
      Todo := Keep;
      while Todo > 0 do
      begin
        Chunk := Todo; if Chunk > SizeOf(Buf) then Chunk := SizeOf(Buf);
        Src.ReadBuffer(Buf, Chunk);
        Dst.WriteBuffer(Buf, Chunk);
        Todo := Todo - Chunk;
      end;
    finally Src.Free; Dst.Free; end;
    DeleteFile(Path);
    RenameFile(Path + '.cut', Path);
  end;

  { a command as the script hands it over, and every line of the answer }
  function Ask(Slot: LongInt; const C, P: AnsiString; Admin: Boolean): AnsiString;
  var T, k: LongInt;
  begin
    Result := '';
    RunCmd(Slot, PChar(C), PChar(P), Admin, PChar('test'));
    T := 0;
    while (T < 500) and SlotBusy(Slot) do begin Sleep(10); Inc(T); end;
    for k := 0 to OutCount(Slot) - 1 do Result := Result + StrPas(OutLine(Slot, k)) + #10;
  end;

  function Occurs(const Sub, S: AnsiString): LongInt;
  var k: LongInt;
  begin
    Result := 0;
    for k := 1 to Length(S) - Length(Sub) + 1 do
      if Copy(S, k, Length(Sub)) = Sub then Inc(Result);
  end;

  { a command that starts a search: its first lines, then the table once the search is done }
  function AskList(Slot: LongInt; const C, P: AnsiString): AnsiString;
  var T, k: LongInt;
  begin
    Result := Ask(Slot, C, P, True);
    T := 0;
    while (T < 500) and not TaskDone(Slot) do begin Sleep(10); Inc(T); end;
    RenderRes(Slot);
    T := 0;
    while (T < 500) and SlotBusy(Slot) do begin Sleep(10); Inc(T); end;
    for k := 0 to OutCount(Slot) - 1 do Result := Result + StrPas(OutLine(Slot, k)) + #10;
  end;

  { any command as the script hands it over, by a player or an admin: its lines, and the table
    of the search it started (the way the script asks: Command_Started_Task) }
  function AskAll(Slot: LongInt; const C, P: AnsiString; Admin: Boolean): AnsiString;
  var T, k: LongInt;
  begin
    Result := '';
    if RunCmd(Slot, PChar(C), PChar(P), Admin, PChar('test')) = 0 then Exit;
    T := 0;
    while (T < 500) and SlotBusy(Slot) do begin Sleep(10); Inc(T); end;
    for k := 0 to OutCount(Slot) - 1 do Result := Result + StrPas(OutLine(Slot, k)) + #10;
    if not StartedTask(Slot) then Exit;
    T := 0;
    while (T < 500) and not TaskDone(Slot) do begin Sleep(10); Inc(T); end;
    RenderRes(Slot);
    T := 0;
    while (T < 500) and SlotBusy(Slot) do begin Sleep(10); Inc(T); end;
    for k := 0 to OutCount(Slot) - 1 do Result := Result + StrPas(OutLine(Slot, k)) + #10;
  end;

  { the rows of a list name these accounts in this order ("| Name ") }
  function InOrder(const S: AnsiString; const Names: array of AnsiString): Boolean;
  var k, P, Last: LongInt;
  begin
    Result := True;
    Last := 0;
    for k := 0 to High(Names) do
    begin
      P := Pos('| ' + Names[k] + ' ', S);
      if (P = 0) or (P < Last) then
      begin
        WriteLn('       not in order: ', Names[k]);
        Result := False;
        Exit;
      end;
      Last := P;
    end;
  end;

  { the line of the answer that holds Sub }
  function LineWith(const S, Sub: AnsiString): AnsiString;
  var L: TStringList; k: LongInt;
  begin
    Result := '';
    L := TStringList.Create;
    try
      L.Text := S;
      for k := 0 to L.Count - 1 do
        if Pos(Sub, L[k]) > 0 then
        begin
          Result := L[k];
          Exit;
        end;
    finally L.Free; end;
  end;

const
  Locals: array[0..7] of AnsiString = ('127.0.0.1', '10.1.2.3', '192.168.0.10', '172.20.5.1', '169.254.1.1',
    '100.64.0.1', 'localhost', '');
  CmdPlain: array[0..5] of AnsiString = ('search', 'status', 'help', 'next', 'prev', 'page');
  Named: array[0..11] of AnsiString = ('af_search', 'afsearch', 'af_status', 'afstatus', 'af_help', 'afhelp',
    'af_next', 'afnext', 'af_prev', 'afprev', 'af_page', 'afpage');
begin
  Lib := LoadLibrary(ParamStr(1));
  Check('library loads', Lib <> NilHandle);
  if Lib = NilHandle then
    Halt(1);
  JoinString := TGeoText(GetProcedureAddress(Lib, 'Get_Geo_Join_String'));
  GeoString := TGeoText(GetProcedureAddress(Lib, 'Get_Geo_String'));
  CheckGeo := TGeoCheck(GetProcedureAddress(Lib, 'Check_And_Update_Geo'));
  Check('geo exports found', Assigned(JoinString) and Assigned(GeoString) and Assigned(CheckGeo));
  { the time a load takes, for the line the admins get after a /recompile }
  LoadMillis := TCount(GetProcedureAddress(Lib, 'Get_Load_Millis'));
  Check('Get_Load_Millis is exported and says -1 before anything was read',
    Assigned(LoadMillis) and (LoadMillis() = -1));

  Sleep(200);
  Before := ThreadCount;
  Peak := Before;
  JoinSilent := True;
  LocateLocal := True;
  { the script asks every tick for 30 seconds: 1800 calls per join }
  for n := 1 to 1800 do
  begin
    for i := 0 to High(Locals) do
    begin
      if StrPas(JoinString(PChar(Locals[i]))) <> '' then
        JoinSilent := False;
      CheckGeo(PChar(Locals[i]));
    end;
    if n mod 100 = 0 then
    begin
      S := StrPas(GeoString(PChar(Locals[n div 100 mod 7])));
      if S <> 'Local network' then
        LocateLocal := False;
      Peak := Max(Peak, ThreadCount);
    end;
  end;
  Check(Format('no lookup threads for local addresses (threads %d before, %d at most)', [Before, Peak]), Peak <= Before);
  Check('join location stays empty for local players', JoinSilent);
  Check('!locate shows "Local network" for local addresses', LocateLocal);

  { ---- storage: a save never loses the old file, a damaged file never replaces the data ---- }
  StartSave := TPathProc(GetProcedureAddress(Lib, 'Start_Save_Binary'));
  StartLoad := TPathProc(GetProcedureAddress(Lib, 'Start_Load_Binary'));
  SaveDone := TFlag(GetProcedureAddress(Lib, 'Is_Save_Complete'));
  SaveFailed := TFlag(GetProcedureAddress(Lib, 'Is_Save_Error'));
  SaveBusy := TFlag(GetProcedureAddress(Lib, 'Is_Save_In_Progress'));
  LoadDone := TFlag(GetProcedureAddress(Lib, 'Is_Load_Complete'));
  LoadFailed := TFlag(GetProcedureAddress(Lib, 'Is_Load_Error'));
  SaveAfterLoad := TFlag(GetProcedureAddress(Lib, 'Save_After_Load'));
  LoadBusy := TFlag(GetProcedureAddress(Lib, 'Is_Load_In_Progress'));
  ProfileCount := TCount(GetProcedureAddress(Lib, 'Get_Profile_Count'));
  LoginPlayer := TLogin(GetProcedureAddress(Lib, 'Login_Player'));
  ProfileName := TNameOf(GetProcedureAddress(Lib, 'Get_Profile_Active_Name'));
  Rollback := TRollback(GetProcedureAddress(Lib, 'Rollback_Profile_Changes'));
  Check('storage exports found', Assigned(StartSave) and Assigned(StartLoad) and Assigned(SaveDone) and
    Assigned(LoadDone) and Assigned(ProfileCount) and Assigned(LoginPlayer) and Assigned(Rollback));

  { aftest.exe <dll> [database] also reads a real file (a copy: nothing is written to it) }
  if ParamStr(2) <> '' then
  begin
    StartLoad(PChar(ParamStr(2))); WaitLoad;
    Check(Format('the server database reads back (%d profiles)', [ProfileCount()]),
      (not LoadFailed()) and (ProfileCount() > 0));
  end;

  DbPath := ExpandFileName('aftest.dat');
  BakPath := DbPath + '.bak';
  if FileExists(DbPath) then DeleteFile(DbPath);
  if FileExists(BakPath) then DeleteFile(BakPath);
  if FileExists(ChangeFileExt(DbPath, '.jrn')) then DeleteFile(ChangeFileExt(DbPath, '.jrn'));

  { nothing is written before a database has been read: a server stopped in the middle of the
    reading would write its empty memory over the file }
  if ParamStr(2) = '' then
  begin
    StartSave(PChar(DbPath));
    Sleep(300);
    Check('no database is written before one has been read', not FileExists(DbPath));
  end;
  StartLoad(PChar(DbPath)); WaitLoad;
  Check('a missing file reads as an empty database', (not LoadFailed()) and (ProfileCount() = 0));
  { a new database is written at once (the script asks Save_After_Load after every load; the
    journal and what comes of it are tested with the journal below) }
  Check('a new database asks to be written at once, once', Assigned(SaveAfterLoad) and SaveAfterLoad() and
    not SaveAfterLoad());

  n := ProfileCount();
  LoginPlayer('Alice', '76561198000000001', 'HWALICE', '8.8.8.8', True);
  LoginPlayer('Bob', '76561198000000002', 'HWBOB', '8.8.4.4', True);
  LoginPlayer('Carol', '', 'HWCAROL', '9.9.9.9', True);
  Check('three players make three profiles', ProfileCount() = n + 3);

  { an empty name, HWID and IP must not join a player to somebody else }
  n := ProfileCount();
  LoginPlayer('Dave', '', '', '', True);
  Check('a player without HWID gets a profile of their own', ProfileCount() = n + 1);
  LoginPlayer('Erin', '', '', '', True);
  Check('a second player without HWID is not the same account', ProfileCount() = n + 2);

  StartSave(PChar(DbPath)); WaitSave;
  Check('the database is written', FileExists(DbPath) and not SaveFailed());
  SizeBefore := FileLen(DbPath);
  StartSave(PChar(DbPath)); WaitSave;
  Check('the previous file is kept as .bak', FileExists(BakPath) and (FileLen(BakPath) = SizeBefore));

  StartLoad(PChar(DbPath)); WaitLoad;
  Check('the database reads back', (not LoadFailed()) and (ProfileCount() = n + 2));

  { half a file: the load has to refuse it and leave the profiles alone }
  Truncate(DbPath, SizeBefore div 2);
  StartLoad(PChar(DbPath)); WaitLoad;
  Check('a truncated file is refused', LoadFailed());
  Check('the profiles in memory survive a refused load', ProfileCount() = n + 2);
  SizeBefore := FileLen(DbPath);
  StartSave(PChar(DbPath)); WaitSave;
  Check('nothing is written over a database that could not be read',
    SaveFailed() and (FileLen(DbPath) = SizeBefore));

  { the rollback of the only name leaves no empty row behind }
  StartLoad(PChar(BakPath)); WaitLoad;
  Check('the backup reads back', (not LoadFailed()) and (ProfileCount() = n + 2));
  i := ProfileCount();
  LoginPlayer('Zed', '', 'HWZED', '7.7.7.7', True);
  Check('the new player is added', ProfileCount() = i + 1);
  Rollback(i, 'Zed', 'HWZED', '7.7.7.7', 1 or 4 or 8);
  Check('the rolled back profile leaves no row with data', ProfileCount() = i + 1);
  LoginPlayer('Yuri', '', 'HWYURI', '6.6.6.6', True);
  Check('the emptied slot is used again instead of a new one', ProfileCount() = i + 1);
  Check('the slot carries the new player', StrPas(ProfileName(i)) = 'Yuri');

  { the list of profiles: an emptied one is not a row, and no row has an empty name }
  StartTask := TStartTask(GetProcedureAddress(Lib, 'Start_Search_Task'));
  TaskDone := TTaskFlag(GetProcedureAddress(Lib, 'Is_Task_Complete'));
  SelectResult := TSelect(GetProcedureAddress(Lib, 'Select_Task_Result'));
  ResultCount := TCount(GetProcedureAddress(Lib, 'Get_Result_Count'));
  ItemName := TItemText(GetProcedureAddress(Lib, 'Get_Result_Item_ActiveName'));
  ByHwid := TNameLookup(GetProcedureAddress(Lib, 'Find_Profile_By_HWID'));
  BySteam := TNameLookup(GetProcedureAddress(Lib, 'Find_Profile_By_Steam'));
  Check('lookup exports found', Assigned(ByHwid) and Assigned(BySteam));
  Check('list exports found', Assigned(StartTask) and Assigned(TaskDone) and Assigned(SelectResult) and
    Assigned(ResultCount) and Assigned(ItemName));
  i := ProfileCount();
  LoginPlayer('Ghost', '', 'HWGHOST', '5.5.5.5', True);
  Rollback(i, 'Ghost', 'HWGHOST', '5.5.5.5', 1 or 4 or 8);
  StartTask(1, 8 {TASK_LIST}, 0, 0 {SORT_ID_UP}, -1, 0, '', False, False, False, False, False);
  n := 0;
  while (n < 500) and not TaskDone(1) do begin Sleep(20); Inc(n); end;
  SelectResult(1);
  Rows := ResultCount();
  Empty := 0;
  for n := 0 to Rows - 1 do
    if StrPas(ItemName(n)) = '' then Inc(Empty);
  Check('no row of the profile list is empty', Empty = 0);
  Check('the emptied profile is left out of the list', Rows = ProfileCount() - 1);

  { the thread of a finished task is freed when the next one starts: this used to happen while
    its own lock was held, which could hang the server }
  ClearTask := TSelect(GetProcedureAddress(Lib, 'Clear_Task_Result'));
  Check('clear export found', Assigned(ClearTask));
  ClearTask(1);
  StartTask(1, 8 {TASK_LIST}, 0, 0, -1, 0, '', False, False, False, False, False);
  n := 0;
  while (n < 500) and not TaskDone(1) do begin Sleep(20); Inc(n); end;
  Check('a second task on the same slot finishes too', TaskDone(1));
  SelectResult(1);
  Check('the second task has the same rows', ResultCount() = Rows);

  { how long a join takes with a database that is not tiny: it used to read every profile
    and every entry of it, on the game thread and under the lock }
  T0 := GetTickCount64;
  for n := 1 to 20000 do
    LoginPlayer(PChar('P' + IntToStr(n)), PChar('S9' + IntToStr(1000000 + n)),
      PChar('HW' + IntToStr(n)), PChar('10.0.' + IntToStr(n div 250) + '.' + IntToStr(n mod 250)), True);
  Build := GetTickCount64 - T0;
  Check(Format('20000 accounts created (%d ms)', [Build]), ProfileCount() >= 20000);
  Ok := True;
  T0 := GetTickCount64;
  for n := 1 to 2000 do
  begin
    i := 1 + (n * 7) mod 20000;
    if LoginPlayer(PChar('P' + IntToStr(i)), PChar('S9' + IntToStr(1000000 + i)),
         PChar('HW' + IntToStr(i)), PChar('10.0.0.1'), False) < 0 then
      Ok := False;
  end;
  Lookup := GetTickCount64 - T0;
  Check(Format('2000 joins of known players found their account (%d ms)', [Lookup]), Ok);
  Check(Format('a join stays quick with 20000 accounts (%d ms for 2000)', [Lookup]), Lookup < 2000);
  Ok := True;
  for n := 1 to 50 do
  begin
    i := 1 + (n * 211) mod 20000;
    if ByHwid(PChar('HW' + IntToStr(i))) <> BySteam(PChar('S9' + IntToStr(1000000 + i))) then
      Ok := False;
  end;
  Check('the computer index and the Steam index point at the same account', Ok);
  Check('an unknown computer is not found', ByHwid(PChar('HW-NOBODY')) < 0);

  { the settings file: what is in it wins, what is missing keeps the built-in value, and a
    value outside the range is ignored }
  LoadIni := TLoadIni(GetProcedureAddress(Lib, 'Load_Settings'));
  SettingInt := TSettingInt(GetProcedureAddress(Lib, 'Get_Setting_Int'));
  SettingStr := TSettingStr(GetProcedureAddress(Lib, 'Get_Setting_Str'));
  Maint := TMaint(GetProcedureAddress(Lib, 'Run_Maintenance'));
  MaintReport := TReport(GetProcedureAddress(Lib, 'Get_Maintenance_Report'));
  IpCount := TItemCount(GetProcedureAddress(Lib, 'Get_Result_Item_IPsCount'));
  Check('settings exports found', Assigned(LoadIni) and Assigned(SettingInt) and
    Assigned(SettingStr) and Assigned(Maint) and Assigned(MaintReport) and Assigned(IpCount));

  LoadIni(PChar('does-not-exist.ini'));
  Check('without a file the built-in values are used',
    (SettingInt(PChar('saveintervalminutes')) = 15) and (SettingInt(PChar('addressdays')) = 180) and
    (SettingInt(PChar('loaded')) = 0) and (StrPas(SettingStr(PChar('databasefile'))) = 'nickreg/antifake.dat'));

  IniPath := ExpandFileName('aftest.ini');
  L := TStringList.Create;
  try
    L.Add('; a comment');
    L.Add('[General]');
    L.Add('DatabaseFile = other.dat');
    L.Add('SaveIntervalMinutes = 7');
    L.Add('[Commands]');
    L.Add('PlayerCommands = yes');
    L.Add('ChatLocate = 0');              { a setting that no longer exists is ignored }
    L.Add('ResultsPerPage = 999');        { out of range: stays 10 }
    L.Add('[Retention]');
    L.Add('AddressDays = 30');
    L.Add('MaxAddresses = 3');
    L.Add('NoSuchKey = 5');
    L.SaveToFile(IniPath);
  finally
    L.Free;
  end;
  Check('the settings file is read', LoadIni(PChar(IniPath)));
  Check('values from the file win',
    (StrPas(SettingStr(PChar('databasefile'))) = 'other.dat') and
    (SettingInt(PChar('saveintervalminutes')) = 7) and
    (SettingInt(PChar('playercommands')) = 1) and
    (SettingInt(PChar('addressdays')) = 30) and (SettingInt(PChar('maxaddresses')) = 3));
  Check('a value outside the range keeps the built-in one', SettingInt(PChar('resultsperpage')) = 10);
  Check('what the file does not say stays as it was', SettingInt(PChar('namedays')) = 0);

  { retention: the profile keeps its newest address and at most MaxAddresses of them }
  i := ProfileCount();
  LoginPlayer('Keeper', '', 'HW-KEEP', '1.1.1.1', True);
  LoginPlayer('Keeper', '', 'HW-KEEP', '1.1.1.2', True);
  LoginPlayer('Keeper', '', 'HW-KEEP', '1.1.1.3', True);
  LoginPlayer('Keeper', '', 'HW-KEEP', '1.1.1.4', True);
  LoginPlayer('Keeper', '', 'HW-KEEP', '1.1.1.5', True);
  Check('the addresses are collected', ProfileCount() = i + 1);
  Maint(True);
  StartTask(1, 8 {TASK_LIST}, 0, 0, -1, 0, '', False, False, False, False, False);
  n := 0;
  while (n < 500) and not TaskDone(1) do begin Sleep(20); Inc(n); end;
  SelectResult(1);
  Ok := False;
  for n := 0 to ResultCount() - 1 do
    if StrPas(ItemName(n)) = 'Keeper' then
      Ok := IpCount(n) = 3;
  Check('the cleanup leaves at most MaxAddresses addresses', Ok);
  Check('the cleanup says what it did', Pos('addresses', StrPas(MaintReport())) > 0);
  if FileExists(IniPath) then DeleteFile(IniPath);
  LoadIni(PChar('does-not-exist.ini'));

  { place names arrive in UTF-8 and the console draws one byte per character }
  Conv := TConv(GetProcedureAddress(Lib, 'Utf8_To_Console'));
  Check('text conversion export found', Assigned(Conv));
  if Assigned(Conv) then
  begin
    Check('a Swedish name keeps its letters',
      StrPas(Conv(PChar(#$56#$C3#$A4#$73#$74#$65#$72))) = 'V' + Chr($E4) + 'ster');
    { MysqlCodepage = 1250 (the default): the text is kept in that code page, every Polish
      letter has its byte }
    Check('a Polish name keeps its letters in code page 1250',
      StrPas(Conv(PChar(#$C5#$81#$C3#$B3#$64#$C5#$BA))) = Chr($A3) + Chr($F3) + 'd' + Chr($9F));
    Lines := TStringList.Create;
    try
      Lines.Add('[MySQL]');
      Lines.Add('MysqlCodepage = 1252');
      Lines.SaveToFile(IniPath);
    finally Lines.Free; end;
    LoadIni(PChar(IniPath));
    DeleteFile(IniPath);
    Check('with the Western code page a Polish name gets plain letters where it has none',
      StrPas(Conv(PChar(#$C5#$81#$C3#$B3#$64#$C5#$BA))) = 'L' + Chr($F3) + 'dz');
    LoadIni(PChar('does-not-exist.ini'));
    Check('plain ascii is left alone', StrPas(Conv(PChar('Vannas 123'))) = 'Vannas 123');
    Check('a sign the console has no letter for becomes a question mark',
      StrPas(Conv(PChar(#$E4#$B8#$AD))) = '?');
    { text that was already converted must survive a second pass unchanged }
    Check('converted text is not mangled again',
      StrPas(Conv(PChar('V' + Chr($E4) + 'ster'))) = 'V' + Chr($E4) + 'ster');
    Check('the old mojibake in the database is repaired',
      StrPas(Conv(PChar('V' + Chr($C3) + Chr($A4) + 'ster'))) = 'V' + Chr($E4) + 'ster');
  end;

  { the visit log: one visit per account and address, extended while the player keeps playing }
  VisitNote := TVisitNote(GetProcedureAddress(Lib, 'Visit_Note'));
  VisitCount := TVisitCount(GetProcedureAddress(Lib, 'Get_Visit_Count'));
  Check('visit exports found', Assigned(VisitNote) and Assigned(VisitCount));
  Keeper := ProfileCount();
  LoginPlayer('Visitor', '', 'HW-VIS', '8.8.8.8', True);
  Check('the visitor has an account', ProfileCount() = Keeper + 1);
  VisitNote(Keeper, PChar('Visitor'), PChar('HW-VIS'), PChar('8.8.8.8'), 60);
  Check('the first visit is written', VisitCount(Keeper) = 1);
  VisitNote(Keeper, PChar('Visitor'), PChar('HW-VIS'), PChar('8.8.8.8'), 60);
  VisitNote(Keeper, PChar('Visitor'), PChar('HW-VIS'), PChar('8.8.8.8'), 60);
  Check('playing on does not add visits', VisitCount(Keeper) = 1);
  VisitNote(Keeper, PChar('Visitor'), PChar('HW-VIS'), PChar('9.9.9.9'), 30);
  Check('another address is another visit', VisitCount(Keeper) = 2);
  StartTask(1, 12 {TASK_VISITS}, 0, 0, Keeper, 0, '', False, False, False, False, False);
  n := 0;
  while (n < 500) and not TaskDone(1) do begin Sleep(20); Inc(n); end;
  SelectResult(1);
  Check('both visits are listed', ResultCount() = 2);
  StartTask(1, 13 {TASK_UNIQUE_VISITS}, 0, 0, Keeper, 0, '', False, False, False, False, False);
  n := 0;
  while (n < 500) and not TaskDone(1) do begin Sleep(20); Inc(n); end;
  SelectResult(1);
  Check('the unique list has one row per address', ResultCount() = 2);

  { the visits survive a save and a load }
  StartSave(PChar(DbPath)); WaitSave;
  StartLoad(PChar(DbPath)); WaitLoad;
  Check('the visits come back from the database',
    (not LoadFailed()) and (VisitCount(Keeper) = 2));

  { a Polish name reaches the server one byte per character. Those bytes used to be thrown
    away letter by letter, so the same nick with and without its accents looked different. }
  Similar := TItemCount(GetProcedureAddress(Lib, 'Get_Result_Item_Similarity'));
  Polish := 'Ku' + Chr($A3) + 'a' + Chr($9C) + 'ek';   { Ku-L-a-S-ek on the Polish page }
  Plain := 'KuLaSek';
  LoginPlayer(PChar(Polish), '', 'HW-PL', '5.5.5.5', True);
  StartTask(1, 9 {TASK_ADVANCED_SEARCH}, 0 {levenshtein}, 50, -1, 50, PChar(Plain),
    True, False, False, False, False);
  n := 0;
  while (n < 500) and not TaskDone(1) do begin Sleep(20); Inc(n); end;
  SelectResult(1);
  Ok := False;
  for n := 0 to ResultCount() - 1 do
    if StrPas(ItemName(n)) = Polish then Ok := Similar(n) = 100;
  Check('a name with Polish letters matches the same name without them', Ok);

  { "the first one it used" is the oldest entry, not every entry ever written }
  LoginPlayer(PChar('Older'), '', 'HW-OLD', '6.6.6.1', True);
  LoginPlayer(PChar('Newer'), '', 'HW-OLD', '6.6.6.2', True);
  Older := ByHwid(PChar('HW-OLD'));
  Check('the two names share one account', Older >= 0);
  StartTask(1, 4 {TASK_PRIVACYID}, 0 {KIND_NAME}, 0, Older, 100, '',
    False, False, False, False, False);
  n := 0;
  while (n < 500) and not TaskDone(1) do begin Sleep(20); Inc(n); end;
  SelectResult(1);
  Check('the whole history has both names', ResultCount() = 2);
  StartTask(1, 3 {TASK_PRIVACY}, 0 {KIND_NAME}, 0, Older, 100, '',
    False, False, False, False, False);
  n := 0;
  while (n < 500) and not TaskDone(1) do begin Sleep(20); Inc(n); end;
  SelectResult(1);
  Check('both names were used today, so both are still current', ResultCount() = 2);
  StartTask(1, 6 {TASK_CHECKD}, 0 {KIND_NAME}, 0, -1, 0, PChar('Older'),
    False, False, True {first only}, False, False);
  n := 0;
  while (n < 500) and not TaskDone(1) do begin Sleep(20); Inc(n); end;
  SelectResult(1);
  Rows := ResultCount();
  StartTask(1, 6 {TASK_CHECKD}, 0 {KIND_NAME}, 0, -1, 0, PChar('Newer'),
    False, False, True {first only}, False, False);
  n := 0;
  while (n < 500) and not TaskDone(1) do begin Sleep(20); Inc(n); end;
  SelectResult(1);
  Check('only the oldest name counts as the first one',
    (Rows = 1) and (ResultCount() = 0));
  StartTask(1, 6 {TASK_CHECKD}, 0 {KIND_NAME}, 0, -1, 0, PChar('Newer'),
    False, False, False, True {last only}, False);
  n := 0;
  while (n < 500) and not TaskDone(1) do begin Sleep(20); Inc(n); end;
  SelectResult(1);
  Check('the name it played with last is the one it uses now', ResultCount() = 1);

  { two accounts are tied together when they were played from the same computer or the same
    address. The game thread asks this on every join, so it goes through the indexes. }
  MultiLinks := TMultiLinks(GetProcedureAddress(Lib, 'Get_Multi_Links'));
  ItemIndex := TItemCount(GetProcedureAddress(Lib, 'Get_Result_Item_Index'));
  Check('multi account export found', Assigned(MultiLinks) and Assigned(ItemIndex));
  One := LoginPlayer('Twin-A', 'S-TWIN-A', 'HW-TA', '7.7.1.1', True);
  Two := LoginPlayer('Twin-B', 'S-TWIN-B', 'HW-TB', '7.7.2.2', True);
  Alone := LoginPlayer('Hermit', 'S-ALONE', 'HW-ALONE', '7.7.9.9', True);
  Check('three accounts of their own', (One <> Two) and (Two <> Alone));
  Check('an account nobody shares anything with says nothing',
    StrPas(MultiLinks(Alone, 4)) = '');
  { the first one now plays from the computer of the second }
  LoginPlayer('Twin-A', 'S-TWIN-A', 'HW-TB', '7.7.1.1', True);
  S := StrPas(MultiLinks(One, 4));
  S := StrPas(MultiLinks(One, 4));
  Check('a shared computer ties the two accounts together',
    (Pos('1|', S) = 1) and (Pos(IntToStr(Two) + ':1:', S) > 0) and (Pos(':0:Twin-B', S) > 0));
  Check('and it is the same seen from the other side',
    Pos(IntToStr(One) + ':1:', StrPas(MultiLinks(Two, 4))) > 0);
  Check('a shared computer alone is enough to tell an admin about',
    (60 >= SettingInt(PChar('multiaccountreportscore'))) and
    (SettingInt(PChar('multiaccountminsignals')) <= 1));
  Check('a shared address alone is not', 25 < SettingInt(PChar('multiaccountreportscore')));
  { and the third one turns up on the address of the second }
  LoginPlayer('Hermit', 'S-ALONE', 'HW-ALONE', '7.7.2.2', True);
  S := StrPas(MultiLinks(Alone, 4));
  Check('a shared address ties them too', Pos(IntToStr(Two) + ':2:', S) > 0);
  { the second one now shares a computer with the first and an address with the third }
  S := StrPas(MultiLinks(Two, 4));
  Check('one account can be tied to two others for two different reasons',
    (Pos('2|', S) = 1) and (Pos(IntToStr(One) + ':1:', S) > 0) and
    (Pos(IntToStr(Alone) + ':2:', S) > 0));

  { an admin says what a pair is, and it is remembered }
  SetVerdict := TSetVerdict(GetProcedureAddress(Lib, 'Set_Multi_Verdict'));
  GetVerdict := TGetVerdict(GetProcedureAddress(Lib, 'Get_Multi_Verdict'));
  MultiStats := TReport(GetProcedureAddress(Lib, 'Get_Multi_Stats'));
  Check('verdict exports found',
    Assigned(SetVerdict) and Assigned(GetVerdict) and Assigned(MultiStats));
  Check('nobody has judged the pair yet', StrPas(GetVerdict(One, Two)) = '');
  Check('an admin can say it is one family', SetVerdict(One, Two, 2, PChar('Dominik')));
  Check('and it is written down the same way round',
    (Pos('2|Dominik|', StrPas(GetVerdict(Two, One))) = 1) and
    (Pos('2|Dominik|', StrPas(GetVerdict(One, Two))) = 1));
  Check('the links carry what was decided',
    Pos(':2:Twin-B', StrPas(MultiLinks(One, 4))) > 0);
  Check('the summary counts it', Pos('1 one family', StrPas(MultiStats())) > 0);
  Check('an account that is not there cannot be judged',
    not SetVerdict(One, 999999, 1, PChar('Dominik')));
  Check('and a pair cannot be judged against itself', not SetVerdict(One, One, 1, PChar('x')));

  StartTask(1, 15 {TASK_MULTI}, 0, 0, -1, 0, '', False, False, False, False, False);
  n := 0;
  while (n < 500) and not TaskDone(1) do begin Sleep(20); Inc(n); end;
  SelectResult(1);
  Rows := 0;
  for n := 0 to ResultCount() - 1 do
    if (ItemIndex(n) = One) or (ItemIndex(n) = Two) or (ItemIndex(n) = Alone) then Inc(Rows);
  Check('the list holds every account that is tied to another', Rows = 3);

  MultiIni := ExpandFileName('afmulti.ini');
  L := TStringList.Create;
  try
    L.Add('[MultiAccounts]');
    L.Add('MultiAccountByAddress = 0');
    L.SaveToFile(MultiIni);
  finally
    L.Free;
  end;
  LoadIni(PChar(MultiIni));
  { the decision survives a save and a load }
  StartSave(PChar(DbPath)); WaitSave;
  StartLoad(PChar(DbPath)); WaitLoad;
  Check('what the admin decided comes back from the database',
    (not LoadFailed()) and (Pos('2|Dominik|', StrPas(GetVerdict(One, Two))) = 1));
  Check('and it can be taken back', SetVerdict(One, Two, 0, PChar('Dominik')) and
    (StrPas(GetVerdict(One, Two)) = ''));

  Check('a shared address can be turned off',
    (SettingInt(PChar('multiaccountbyaddress')) = 0) and (StrPas(MultiLinks(Alone, 4)) = '') and
    (Pos(IntToStr(Two) + ':1:', StrPas(MultiLinks(One, 4))) > 0));
  if FileExists(MultiIni) then DeleteFile(MultiIni);
  LoadIni(PChar('does-not-exist.ini'));

  { what Steam says about an account. Nothing may leave the machine without an API key, and
    with one the question goes through curl - here a name that does not exist, so the worker
    runs the whole way through and fails without touching the network. }
  SteamNote := TPathProc(GetProcedureAddress(Lib, 'Steam_Note'));
  SteamTick := TVoidProc(GetProcedureAddress(Lib, 'Steam_Tick'));
  SteamInfo := TNameOf(GetProcedureAddress(Lib, 'Get_Steam_Info'));
  SteamStatus := TReport(GetProcedureAddress(Lib, 'Get_Steam_Status'));
  Check('steam exports found', Assigned(SteamNote) and Assigned(SteamTick) and
    Assigned(SteamInfo) and Assigned(SteamStatus));
  Check('without an api key the steam part is off',
    (SettingInt(PChar('steamenabled')) = 0) and (Pos('off', StrPas(SteamStatus())) = 1));
  Before := ThreadCount;
  SteamNote(PChar('S1885850061'));
  SteamTick();
  Sleep(200);
  Check('without a key nothing is asked and no thread starts',
    (ThreadCount <= Before) and (Pos('off', StrPas(SteamStatus())) = 1));
  Check('an account nobody asked about yet has no steam line',
    StrPas(SteamInfo(Keeper)) = '');
  Check('an index outside the list is safe',
    (StrPas(SteamInfo(-1)) = '') and (StrPas(SteamInfo(999999)) = ''));

  SteamIni := ExpandFileName('afsteam.ini');
  L := TStringList.Create;
  try
    L.Add('[Steam]');
    L.Add('SteamApiKey = 0000000000000000000000000000TEST');
    L.Add('SteamBatchSize = 5');
    L.Add('SteamCurlPath = aftest-no-such-curl');
    L.Add('SteamTimeoutSeconds = 2');
    L.SaveToFile(SteamIni);
  finally
    L.Free;
  end;
  Check('the steam settings are read',
    LoadIni(PChar(SteamIni)) and (SettingInt(PChar('steamenabled')) = 1) and
    (SettingInt(PChar('steamrefreshdays')) = 7));
  SteamNote(PChar('S1885850061'));
  SteamNote(PChar('S1885850061'));
  SteamNote(PChar('S0'));
  SteamNote(PChar(''));
  Check('the same account is queued once and rubbish is dropped',
    Pos('waiting 1', StrPas(SteamStatus())) > 0);
  SteamTick();
  n := 0;
  while (n < 250) and (Pos('waiting 0', StrPas(SteamStatus())) = 0) do begin Sleep(20); Inc(n); end;
  Check('the batch leaves the queue', Pos('waiting 0', StrPas(SteamStatus())) > 0);
  n := 0;
  while (n < 250) and (Pos('answered 0', StrPas(SteamStatus())) = 0) do begin Sleep(20); Inc(n); end;
  Check('a curl that is not there costs nothing but the answer',
    (Pos('asked 1', StrPas(SteamStatus())) > 0) and
    (Pos('answered 0', StrPas(SteamStatus())) > 0) and
    (StrPas(SteamInfo(Keeper)) = ''));
  Sleep(300);
  StartSave(PChar(DbPath)); WaitSave;
  StartLoad(PChar(DbPath)); WaitLoad;
  Check('the file keeps its shape with the steam section',
    (not LoadFailed()) and (VisitCount(Keeper) = 2) and (StrPas(SteamInfo(Keeper)) = ''));
  if FileExists(SteamIni) then DeleteFile(SteamIni);

  { The journal: what happens between two full writes is not lost when the server is killed.
    The library is unloaded and loaded again here, which is as close to a kill as a test gets. }
  JournalFlush := TVoidProc(GetProcedureAddress(Lib, 'Journal_Flush'));
  JournalStatus := TReport(GetProcedureAddress(Lib, 'Get_Journal_Status'));
  BackupList := TReport(GetProcedureAddress(Lib, 'Get_Backup_List'));
  RestoreBackup := TRestore(GetProcedureAddress(Lib, 'Restore_Backup'));
  Check('journal exports found', Assigned(JournalFlush) and Assigned(JournalStatus) and
    Assigned(BackupList) and Assigned(RestoreBackup));
  JrnIni := ExpandFileName('afjrn.ini');
  L := TStringList.Create;
  try
    L.Add('[Storage]');
    L.Add('JournalSeconds = 5');
    L.Add('BackupHours = 0');
    L.SaveToFile(JrnIni);
  finally L.Free; end;
  LoadIni(PChar(JrnIni));
  StartSave(PChar(DbPath)); WaitSave;
  JrnPath := ChangeFileExt(DbPath, '') + '.jrn';
  Check('a full write starts a journal of its own', FileExists(JrnPath));
  { a player joins after the full write - exactly what a kill would throw away }
  Keeper := LoginPlayer('AfterSave', 'S-AFTER', 'HW-AFTER', '4.4.4.4', True);
  JournalFlush();
  n := 0;
  while (n < 250) and (Pos('1 writes', StrPas(JournalStatus())) = 0) do begin Sleep(20); Inc(n); end;
  Check('the change is in the journal', Pos('1 writes', StrPas(JournalStatus())) > 0);
  Empty := ProfileCount();
  StartLoad(PChar(DbPath)); WaitLoad;
  Check('after a kill the database comes back with that change',
    (not LoadFailed()) and (ProfileCount() = Empty) and
    (StrPas(ProfileName(Keeper)) = 'AfterSave'));
  Check('and the library says how many records it replayed',
    Pos('replayed', StrPas(JournalStatus())) > 0);
  { a journal that belongs to another file must not be read onto this one }
  StartSave(PChar(DbPath)); WaitSave;
  LoginPlayer('OnlyInJournal', 'S-ONLY', 'HW-ONLY', '4.4.4.5', True);
  JournalFlush();
  Sleep(1000);
  StartSave(PChar(DbPath)); WaitSave;        { the journal starts again, empty }
  StartLoad(PChar(DbPath)); WaitLoad;
  Check('a fresh journal after a full write replays nothing',
    (not LoadFailed()) and (ByHwid(PChar('HW-ONLY')) >= 0));
  { the script stops: the journal takes what changed since the full write, nothing else is
    written, and the next start has it all the same }
  JournalFinal := TFlag(GetProcedureAddress(Lib, 'Journal_Final'));
  Check('Journal_Final is exported', Assigned(JournalFinal));
  Empty := FileLen(DbPath);
  LoginPlayer('AtTheEnd', 'S-END', 'HW-END', '4.4.4.6', True);
  Check('at the end the journal takes everything', JournalFinal());
  Sleep(1000);
  Check('and the database file is not written again', FileLen(DbPath) = Empty);
  StartLoad(PChar(DbPath)); WaitLoad;
  Check('the next start has the last change from the journal',
    (not LoadFailed()) and (ByHwid(PChar('HW-END')) >= 0));
  { The script stops again before the next full write (a restart of an empty server, a
    /recompile): what the journal gave is still there at the start after that. The journal used
    to start again empty after the load, and the file did not have it yet. }
  Check('a journal put on top of the file asks for the file to be written at once', SaveAfterLoad());
  JournalFinal();
  Sleep(300);
  StartLoad(PChar(DbPath)); WaitLoad;
  Check('a second stop before the next full write keeps what the journal gave',
    (not LoadFailed()) and (ByHwid(PChar('HW-END')) >= 0));
  { A new database: the journal belongs to a file on disk, so the load asks for the file to be
    written at once (the script does so) - a server killed before its first full write keeps the
    players of its first minutes. The empty database gets no copy in the backup folder. }
  NewDb := ExpandFileName('aftest-new.dat');
  NewJrn := ChangeFileExt(NewDb, '') + '.jrn';
  if FileExists(NewDb) then DeleteFile(NewDb);
  if FileExists(NewDb + '.bak') then DeleteFile(NewDb + '.bak');
  if FileExists(NewJrn) then DeleteFile(NewJrn);
  StartLoad(PChar(NewDb)); WaitLoad;
  Check('a new database asks to be written at once', (not LoadFailed()) and (ProfileCount() = 0) and SaveAfterLoad());
  i := CountBackups(NewDb);
  StartSave(PChar(NewDb)); WaitSave;
  Check('... and is on disk with its journal from the start', (not SaveFailed()) and FileExists(NewDb) and
    FileExists(NewJrn));
  Check('an empty database gets no copy in the backup folder', CountBackups(NewDb) = i);
  LoginPlayer('FirstOne', 'S-FIRST', 'HW-FIRST', '4.4.4.7', True);
  JournalFlush();
  n := 0;
  while (n < 250) and (Pos('1 writes', StrPas(JournalStatus())) = 0) do begin Sleep(20); Inc(n); end;
  StartLoad(PChar(NewDb)); WaitLoad;
  Check('a server killed before the first full write keeps its first player, and writes it into the file',
    (not LoadFailed()) and (ByHwid(PChar('HW-FIRST')) >= 0) and SaveAfterLoad());
  StartSave(PChar(NewDb)); WaitSave;
  StartLoad(PChar(NewDb)); WaitLoad;
  Check('after that write the file has it and the journal starts again empty',
    (not LoadFailed()) and (ByHwid(PChar('HW-FIRST')) >= 0) and not SaveAfterLoad());
  if FileExists(NewDb) then DeleteFile(NewDb);
  if FileExists(NewDb + '.bak') then DeleteFile(NewDb + '.bak');
  if FileExists(NewJrn) then DeleteFile(NewJrn);
  { back to the database of the tests (the file and its journal, as the last start had them) }
  StartLoad(PChar(DbPath)); WaitLoad;
  Check('the database of the tests reads again', (not LoadFailed()) and (ByHwid(PChar('HW-END')) >= 0));
  if FileExists(JrnIni) then DeleteFile(JrnIni);
  LoadIni(PChar('does-not-exist.ini'));
  if FileExists(JrnPath) then DeleteFile(JrnPath);

  { The database is always part of the library. With Engine = local it is off and says so; with
    Engine = mirror or mysql and a database that cannot be reached, the start reads the file and
    the load note says why. }
  MysqlStatus := TReport(GetProcedureAddress(Lib, 'Get_Mysql_Status'));
  LoadNote := TReport(GetProcedureAddress(Lib, 'Get_Load_Note'));
  NoticeLine := TReport(GetProcedureAddress(Lib, 'Notice_Line'));
  Check('mysql exports found', Assigned(MysqlStatus) and Assigned(LoadNote) and Assigned(NoticeLine) and
    (GetProcedureAddress(Lib, 'Mysql_Load') = nil));
  Check('with Engine = local the database is off', Pos('off (Engine = local)', StrPas(MysqlStatus())) = 1);
  MysqlIni := ExpandFileName('afmysql.ini');
  L := TStringList.Create;
  try
    L.Add('[MySQL]');
    L.Add('Engine = mirror');
    L.Add('MysqlHost = 127.0.0.1');
    L.Add('MysqlPort = 1');
    L.SaveToFile(MysqlIni);
  finally L.Free; end;
  LoadIni(PChar(MysqlIni));
  Check('the three engines are read', SettingInt(PChar('engine')) = 1);
  StartSave(PChar(DbPath)); WaitSave;
  Empty := ProfileCount();
  StartLoad(PChar(DbPath)); WaitLoad;
  Check('a database that cannot be reached: the file is read, and the note says why',
    (not LoadFailed()) and (ProfileCount() = Empty) and (Pos('MySQL could not be read', StrPas(LoadNote())) = 1) and
    (Pos('the file is read', StrPas(LoadNote())) > 0));
  { Engine = mysql: the first start without the database, nothing beside the file yet }
  L := TStringList.Create;
  try
    L.Add('[MySQL]');
    L.Add('Engine = mysql');
    L.Add('MysqlHost = 127.0.0.1');
    L.Add('MysqlPort = 1');
    L.SaveToFile(MysqlIni);
  finally L.Free; end;
  LoadIni(PChar(MysqlIni));
  Check('Engine = mysql is read', SettingInt(PChar('engine')) = 2);
  StartLoad(PChar(DbPath)); WaitLoad;
  Check('Engine = mysql without the database: the file is read, and this run works from it',
    (not LoadFailed()) and (ProfileCount() = Empty) and (Pos('works from the file alone', StrPas(LoadNote())) > 0));
  if Pos('works from the file alone', StrPas(LoadNote())) = 0 then WriteLn('       ', StrPas(LoadNote()));
  if FileExists(MysqlIni) then DeleteFile(MysqlIni);
  LoadIni(PChar('does-not-exist.ini'));
  Check('and then the file is the source again', SettingInt(PChar('engine')) = 0);

  if FileExists(DbPath) then DeleteFile(DbPath);
  if FileExists(BakPath) then DeleteFile(BakPath);

  { ---- command names: search, status, help and the pages belong to ZitroStats as well, so
    AntiFake answers them only as af_search ... af_page, or the same without the underscore ---- }
  RunCmd := TRunCmd(GetProcedureAddress(Lib, 'Run_Command'));
  SlotBusy := TSlotFlag(GetProcedureAddress(Lib, 'Command_Busy'));
  Check('command exports found', Assigned(RunCmd) and Assigned(SlotBusy));
  Ok := True;
  for i := 0 to High(CmdPlain) do
    if RunCmd(1, PChar(CmdPlain[i]), PChar(''), True, PChar('test')) <> 0 then
      Ok := False;
  Check('search, status, help, next, prev and page are left to ZitroStats', Ok);
  Ok := True;
  for i := 0 to High(Named) do
  begin
    n := RunCmd(2 + i, PChar(Named[i]), PChar('ab'), True, PChar('test'));
    if n = 0 then
    begin
      WriteLn('       not an AntiFake command: ', Named[i]);
      Ok := False;
    end;
  end;
  Check('af_search, afsearch ... af_page, afpage are AntiFake commands', Ok);
  for i := 0 to High(Named) do
  begin
    n := 0;
    while (n < 500) and SlotBusy(2 + i) do begin Sleep(10); Inc(n); end;
  end;

  { ---- which account a command means: the closest name, a server slot, a row of the last list ---- }
  OutCount := TSlotCount(GetProcedureAddress(Lib, 'Out_Count'));
  OutLine := TSlotLine(GetProcedureAddress(Lib, 'Out_Line'));
  SetOnline := TSetOnline(GetProcedureAddress(Lib, 'Set_Online'));
  RenderRes := TSelect(GetProcedureAddress(Lib, 'Render_Result'));
  Check('answer exports found', Assigned(OutCount) and Assigned(OutLine) and Assigned(SetOnline) and
    Assigned(RenderRes));
  HkIdx := ProfileCount();
  LoginPlayer('hardkorowy koksu', '', 'HW-HK', '31.1.1.1', True);
  UpIdx := ProfileCount();
  LoginPlayer('up', '', 'HW-UP', '32.2.2.2', True);
  AIdx := ProfileCount();
  LoginPlayer('a', '', 'HW-A', '33.3.3.3', True);
  ZzTwo := ProfileCount();
  LoginPlayer('zz two', '', 'HW-ZZ2', '34.4.4.4', True);
  ZzOne := ProfileCount();
  LoginPlayer('zz one', '', 'HW-ZZ1', '35.5.5.5', True);
  Check('five more accounts', ProfileCount() = ZzOne + 1);
  Said := Ask(5, 'multi', 'u', True);
  Check('"u" means "up", not the longer "hardkorowy koksu"', Pos('Account #' + IntToStr(UpIdx) + ' ', Said) > 0);
  Said := Ask(5, 'multi', 'a', True);
  Check('a nick of one letter is found by that letter', Pos('Account #' + IntToStr(AIdx) + ' ', Said) > 0);
  Said := Ask(5, 'multi', 'koks', True);
  Check('the beginning of a word in a name is found', Pos('Account #' + IntToStr(HkIdx) + ' ', Said) > 0);
  SetOnline(7, ZzTwo, PChar('zz two'), PChar('34.4.4.4'), PChar('HW-ZZ2'), False);
  Said := Ask(5, 'multi', 'zz', True);
  Check('between two names as close, the player on the server wins', Pos('Account #' + IntToStr(ZzTwo) + ' ', Said) > 0);
  Said := Ask(5, 'multiid', '7', True);
  Check('<command>id <slot> asks about the player on that slot', Pos('Account #' + IntToStr(ZzTwo) + ' ', Said) > 0);
  Said := Ask(5, 'multiid', '30', True);
  Check('an empty slot is said to be empty', Pos('Nobody plays on slot 30', Said) > 0);
  Said := AskList(6, 'af_search', 'u');
  Check('the search puts the closest name on top',
    (Pos('| up ', Said) > 0) and (Pos('| up ', Said) < Pos('| hardkorowy koksu', Said)));
  Said := Ask(6, 'multip', '1', True);
  Check('<command>p <row> asks about the account in that row', Pos('Account #' + IntToStr(UpIdx) + ' ', Said) > 0);
  Said := Ask(6, 'multip', '999', True);
  Check('a row the list does not have is refused', Pos('not 999', Said) > 0);
  { <command>i <position>: the ranking of ZitroStats, asked only while its library is loaded }
  Said := Ask(6, 'visitsi', '1', True);
  Check('<command>i without ZitroStats on the server says so', Pos('ZitroStats does not run', Said) > 0);
  Said := Ask(6, 'statusai', 'x', True);
  Check('<command>i without a position shows its usage', Pos('position in the ZitroStats ranking', Said) > 0);
  if FileExists(GetEnvironmentVariable('AFTEST_ZITRO')) then
  begin
    ZLib := LoadLibrary(GetEnvironmentVariable('AFTEST_ZITRO'));
    Said := Ask(6, 'multii', '1', True);
    Check('with ZitroStats loaded its ranking is asked (empty here)', Pos('Nobody is at position 1 ', Said) > 0);
    if ZLib <> NilHandle then UnloadLibrary(ZLib);
  end;

  { the visits of the whole server, and one row per visitor (the reads above numbered the
    accounts again, so the visitor is found by its computer) }
  Keeper := ByHwid(PChar('HW-VIS'));
  VisitNote(UpIdx, PChar('up'), PChar('HW-UP'), PChar('32.2.2.2'), 90);
  VisitNote(AIdx, PChar('a'), PChar('HW-A'), PChar('33.3.3.3'), 30);
  Said := AskList(8, 'lastvisits', '');
  Check('/lastvisits lists the visits of every account',
    (Pos('#' + IntToStr(UpIdx) + ' ', Said) > 0) and (Pos('#' + IntToStr(AIdx) + ' ', Said) > 0) and
    (Occurs('#' + IntToStr(Keeper) + ' ', Said) = 2));
  Said := AskList(9, 'lastvisitors', '');
  Check('/lastvisitors has one row per account', (Pos('Last visit', Said) > 0) and
    (Occurs('#' + IntToStr(Keeper) + ' ', Said) = 1) and (Occurs('#' + IntToStr(UpIdx) + ' ', Said) = 1));
  Said := Ask(10, 'lastvisits', '', False);
  Check('a player without the reach does not get the list of the server', Pos('for admins', Said) > 0);
  Said := Ask(11, 'visits', 'up', False);
  Check('a player who may ask only about themselves is told so, not shown their own visits',
    Pos('only about yourself', Said) > 0);
  Said := Ask(12, 'visitsid', '7', False);
  Check('the slot form is refused the same way', Pos('only about yourself', Said) > 0);

  { ---- older versions kept the database in the server folder: the first start moves it, its
    journal and its copies into the folder the settings name, and leaves it alone after that ---- }
  if FileExists('mvdir\backup\antifake-20000101-000000.dat') then DeleteFile('mvdir\backup\antifake-20000101-000000.dat');
  if FileExists('mvdir\mv.dat') then DeleteFile('mvdir\mv.dat');
  if FileExists('mvdir\mv.jrn') then DeleteFile('mvdir\mv.jrn');
  StartSave(PChar('mv.dat')); WaitSave;
  ForceDirectories('antifake-backups');
  L := TStringList.Create;
  try
    L.Add('a copy');
    L.SaveToFile('antifake-backups\antifake-20000101-000000.dat');
  finally L.Free; end;
  Rows := ProfileCount();
  Check('the old database and its journal are in the server folder', FileExists('mv.dat') and FileExists('mv.jrn'));
  StartLoad(PChar('mvdir\mv.dat')); WaitLoad;
  Check('the database moves into the folder the settings name',
    FileExists('mvdir\mv.dat') and not FileExists('mv.dat'));
  Check('its journal goes with it', FileExists('mvdir\mv.jrn') and not FileExists('mv.jrn'));
  Check('the copies go to the backup folder beside it',
    FileExists('mvdir\backup\antifake-20000101-000000.dat') and
    not FileExists('antifake-backups\antifake-20000101-000000.dat'));
  Check('it reads back whole, and the load says it was moved',
    (not LoadFailed()) and (ProfileCount() = Rows) and (Pos('moved', StrPas(LoadNote())) > 0));
  StartSave(PChar('mv.dat')); WaitSave;
  StartLoad(PChar('mvdir\mv.dat')); WaitLoad;
  Check('a database that is already there is not replaced by an old one',
    FileExists('mv.dat') and (Pos('not used', StrPas(LoadNote())) > 0));
  if FileExists('mv.dat') then DeleteFile('mv.dat');
  if FileExists('mv.jrn') then DeleteFile('mv.jrn');

  { ---- the audit of 2026-09-23: what it fixed stays fixed ---- }
  AuditIni := ExpandFileName('afaudit.ini');
  LoginResult := TVisitCount(GetProcedureAddress(Lib, 'Get_Login_Result'));
  ProfileHwid := TNameOf(GetProcedureAddress(Lib, 'Get_Profile_Active_HWID'));
  PlayedOf := TNameOf(GetProcedureAddress(Lib, 'Get_Profile_Time_Played'));
  AddTime := TAddTime(GetProcedureAddress(Lib, 'Add_Profile_Time'));
  MultiNoticeFn := TMultiNotice(GetProcedureAddress(Lib, 'Multi_Notice'));
  Check('the exports of the audit tests are found', Assigned(LoginResult) and Assigned(ProfileHwid) and
    Assigned(PlayedOf) and Assigned(AddTime) and Assigned(MultiNoticeFn));

  { settings: the README writes a comment behind a value, an editor may put the UTF-8 mark in
    front of the first line (the reading of the file takes it away), and text that is no number
    used to become the default minus one }
  UseIni([#$EF#$BB#$BF + 'LinesPerTick = 9',
    'SyncSeconds = 7          ; mysql: seconds between two exchanges',
    'Engine = mirror          ; local | mirror | mysql',
    'SteamApiKey =            ; empty = nothing is asked of Steam at all',
    'MaxLookups = lots',
    'SaveIntervalMinutes =',
    'MultiAccountMinSignals = 6',
    'SharedSearch = on']);
  Check('a comment behind a value is not part of it',
    (SettingInt(PChar('syncseconds')) = 7) and (SettingInt(PChar('engine')) = 1) and
    (SettingInt(PChar('steamenabled')) = 0));
  Check('text that is no number, or nothing, keeps the built-in value',
    (SettingInt(PChar('maxlookups')) = 4) and (SettingInt(PChar('saveintervalminutes')) = 15));
  Check('and it is counted with the values out of range',
    Pos('(1 out of range, 0 unknown)', StrPas(SettingStr(PChar('message')))) > 0);
  Check('the first key of a file with the UTF-8 mark is read', SettingInt(PChar('linespertick')) = 9);
  Check('more signals than a pair can have means all four', SettingInt(PChar('multiaccountminsignals')) = 4);
  Check('on and off work for the switches of the shared search too', SettingInt(PChar('sharedsearch')) = 1);
  { a Steam key in double quotes, like a password, and one with signs no key has }
  UseIni(['SteamApiKey = "0000000000000000000000000000TEST"']);
  Check('a Steam key in double quotes is read without them', (SettingInt(PChar('steamenabled')) = 1) and
    (Pos('(0 out of range', StrPas(SettingStr(PChar('message')))) > 0));
  UseIni(['SteamApiKey = 0000TEST&steamids=1']);
  Check('a Steam key with signs no key has is counted and left out, and never shown',
    (SettingInt(PChar('steamenabled')) = 0) and
    (Pos('(1 out of range', StrPas(SettingStr(PChar('message')))) > 0) and
    (Pos('TEST', StrPas(SettingStr(PChar('message')))) = 0));
  LoadIni(PChar('does-not-exist.ini'));

  { the help: every line fits the width, the pages are said once and with the prefix of the one
    who asks, and it ends with the version and its author }
  Said := Ask(20, 'antifake', '', False);
  Lines := TStringList.Create;
  try
    Lines.Text := Said;
    Ok := Lines.Count > 10;
    for n := 0 to Lines.Count - 1 do
      if Length(Lines[n]) > 100 then
      begin
        WriteLn('       too wide: ', Lines[n]);
        Ok := False;
      end;
    Check('every line of the help of a player fits 100 letters', Ok);
    Check('the help ends with the version and its author',
      (Lines.Count > 0) and (Pos('AntiFake Extended 2.1 by TryHackX', Lines[Lines.Count - 1]) > 0));
  finally Lines.Free; end;
  Check('a player is shown the pages once, with !',
    (Occurs('!af_next', Said) = 1) and (Pos('/af_next', Said) = 0));
  Check('a player is not shown the forms of naming somebody else they cannot use',
    Pos('instead of p:', Said) = 0);
  Said := Ask(21, 'antifake', '', True);
  Lines := TStringList.Create;
  try
    Lines.Text := Said;
    Ok := True;
    for n := 0 to Lines.Count - 1 do
      if Length(Lines[n]) > 100 then
      begin
        WriteLn('       too wide: ', Lines[n]);
        Ok := False;
      end;
    Check('every line of the help of an admin fits 100 letters', Ok);
  finally Lines.Free; end;
  Check('an admin is shown the pages once, with /',
    (Occurs('/af_next', Said) = 1) and (Pos('!af_next', Said) = 0));
  Check('/storage mirror is offered only with Engine = mirror', Pos('/storage mirror', Said) = 0);
  Check('/online is not offered a sort it does not take', Pos('/online [sort]', Said) = 0);

  { one answer is MaxQueuedLines long at most, and says where it was cut }
  UseIni(['MaxQueuedLines = 20']);
  Ask(22, 'antifake', '', True);
  Check('an answer is cut at MaxQueuedLines and says so',
    (OutCount(22) = 20) and (Pos('cut here', StrPas(OutLine(22, 19))) > 0));
  LoadIni(PChar('does-not-exist.ini'));

  { what Login_Player says about a join: 1 a new name, 2 a new Steam id, 4 a new computer,
    8 a new address, 16 a new account }
  i := LoginPlayer('Flag One', 'S-FLAG-1', 'HW-FLAG-1', '46.6.6.1', True);
  Check('a new account with everything says 1 + 2 + 4 + 8 + 16', LoginResult(4) = 31);
  LoginPlayer('Flag Two', 'S-FLAG-1', 'HW-FLAG-1', '46.6.6.1', False);
  Check('a new name says 1', LoginResult(4) = 1);
  LoginPlayer('Flag Two', 'S-FLAG-1', 'HW-FLAG-2', '46.6.6.2', False);
  Check('a new computer and a new address say 4 and 8', LoginResult(4) = 12);
  n := LoginPlayer('Flag Three', '', 'HW-FLAG-3', '46.6.6.3', True);
  LoginPlayer('Flag Three', 'S-FLAG-3', 'HW-FLAG-3', '46.6.6.3', False);
  Check('a Steam id on an account without one says 2', (LoginResult(4) = 2) and (LoginResult(1) = 0));

  { a value longer than an entry holds was stored cut and never matched again: one more name
    at every join }
  LongName := 'L' + StringOfChar('o', 70) + 'ng';
  i := LoginPlayer(PChar(LongName), '', 'HW-LONG', '44.4.4.4', True);
  LoginPlayer(PChar(LongName), '', 'HW-LONG', '44.4.4.4', True);
  LoginPlayer(PChar(LongName), '', 'HW-LONG', '44.4.4.4', True);
  StartTask(1, 4 {TASK_PRIVACYID}, 0 {KIND_NAME}, 0, i, 100, '', False, False, False, False, False);
  n := 0;
  while (n < 500) and not TaskDone(1) do begin Sleep(20); Inc(n); end;
  SelectResult(1);
  Check('a name longer than an entry holds stays one name', ResultCount() = 1);

  { a computer a correction takes back is not the one the account shows any more }
  i := LoginPlayer('Rollie', 'S-ROLL-1', 'HW-ROLL-1', '45.5.5.5', True);
  n := LoginPlayer('Rollie', 'S-ROLL-1', 'HW-ROLL-2', '45.5.5.5', False);
  Check('the Steam account finds its account on a new computer', (n = i) and ((LoginResult(4) and 4) <> 0));
  Rollback(i, 'Rollie', 'HW-ROLL-2', '45.5.5.5', 4);
  Check('the computer taken back is gone, and the account shows the one it has',
    (StrPas(ProfileHwid(i)) = 'HW-ROLL-1') and (ByHwid(PChar('HW-ROLL-2')) < 0));

  { the play time of a minute goes in }
  Played := StrToInt64Def(StrPas(PlayedOf(i)), -1);
  AddTime(i, 60, 5);
  Check('the play time of a minute is added', StrToInt64Def(StrPas(PlayedOf(i)), -1) = Played + 60);

  { the notice about a join is made on a worker and comes through Notice_Line, for slot 0 }
  while StrPas(NoticeLine()) <> '' do ;
  MnA := LoginPlayer('Mn Alpha', 'S-MN-A', 'HW-MN-A', '71.1.1.1', True);
  MnB := LoginPlayer('Mn Beta', 'S-MN-B', 'HW-MN-B', '72.2.2.2', True);
  LoginPlayer('Mn Alpha', 'S-MN-A', 'HW-MN-B', '71.1.1.1', False);
  Check('the join is not held up: Multi_Notice answers at once and empty',
    StrPas(MultiNoticeFn(MnA, PChar('Mn Alpha'))) = '');
  Line := AdminNotice(5000);
  Check('the notice comes a moment later for every admin and the console',
    (Pos('Mn Alpha plays on account #' + IntToStr(MnA), Line) > 0) and
    (Pos('#' + IntToStr(MnB) + ' Mn Beta', Line) > 0));
  Line := AdminNotice(2000);
  Check('with the command to look at it', Pos('multi ' + IntToStr(MnA) + ' to look at it', Line) > 0);
  UseIni(['MultiAccountMinLinks = 2']);
  MultiNoticeFn(MnA, PChar('Mn Alpha'));
  Check('MultiAccountMinLinks is heard: one tied account says nothing when it takes two',
    AdminNotice(1500) = '');
  LoadIni(PChar('does-not-exist.ini'));

  { an address more accounts than MultiAccountMaxShared have played from is a public one }
  UseIni(['MultiAccountMaxShared = 2']);
  ShX := LoginPlayer('Share X', '', 'HW-SH-X', '61.62.63.64', True);
  ShY := LoginPlayer('Share Y', '', 'HW-SH-Y', '61.62.63.64', True);
  Check('two accounts on one address are tied', Pos('|' + IntToStr(ShY) + ':2:', StrPas(MultiLinks(ShX, 50))) > 0);
  LoginPlayer('Share Z', '', 'HW-SH-Z', '61.62.63.64', True);
  Check('three are more than 2: the address says nothing any more', StrPas(MultiLinks(ShX, 50)) = '');
  LoadIni(PChar('does-not-exist.ini'));

  { an address the cleanup took is not a shared network any more }
  NetA := LoginPlayer('Net A', '', 'HW-NET-A', '50.60.70.1', True);
  Sleep(30);
  LoginPlayer('Net A', '', 'HW-NET-A', '90.1.1.1', True);
  NetB := LoginPlayer('Net B', '', 'HW-NET-B', '50.60.70.2', True);
  Check('two accounts of one network are tied', Pos('|' + IntToStr(NetA) + ':8:', StrPas(MultiLinks(NetB, 50))) > 0);
  UseIni(['MaxAddresses = 1']);
  Maint(True);
  Check('the network of an address the cleanup took ties nothing any more',
    Pos('|' + IntToStr(NetA) + ':', StrPas(MultiLinks(NetB, 50))) = 0);
  LoadIni(PChar('does-not-exist.ini'));

  { while the database is being read a command is refused: its answer would come from half of it }
  StartSave(PChar(DbPath)); WaitSave;
  StartLoad(PChar(DbPath));
  n := RunCmd(23, PChar('multi'), PChar('1'), True, PChar('test'));
  Line := StrPas(OutLine(23, 0));
  if LoadBusy() then
    Check('a command is refused while the database is read', (n = 3) and (Pos('still being read', Line) > 0))
  else
    WriteLn('       (the database was read before the command came; the refusal was not seen)');
  WaitLoad;

  { ProfileDays: an account nobody played on for that long is emptied; its number stays and the
    next new player takes it. afold.dat (the file of afconv) holds seven accounts last played
    days before these tests were written; it is read from a copy. }
  if FileExists('afold.dat') then
  begin
    SrcF := TFileStream.Create('afold.dat', fmOpenRead or fmShareDenyNone);
    try
      DstF := TFileStream.Create('afaudit-old.dat', fmCreate);
      try
        DstF.CopyFrom(SrcF, 0);
      finally DstF.Free; end;
    finally SrcF.Free; end;
    UseIni(['ProfileDays = 1', 'JournalSeconds = 0']);
    StartLoad(PChar(ExpandFileName('afaudit-old.dat'))); WaitLoad;
    n := ProfileCount();
    Maint(True);
    Check('accounts nobody played on for ProfileDays are emptied: ' + StrPas(MaintReport()),
      (not LoadFailed()) and (n = 7) and (Pos('and 7 unused profiles dropped', StrPas(MaintReport())) > 0) and
      (ProfileCount() = n) and (BySteam(PChar('S1885850061')) < 0));
    i := LoginPlayer('After', '', 'HW-AFTER', '47.7.7.7', True);
    Check('their numbers stay, and the next new player takes the first of them',
      (i = 0) and (ProfileCount() = n));
    StartTask(1, 8 {TASK_LIST}, 0, 0, -1, 0, '', False, False, False, False, False);
    n := 0;
    while (n < 500) and not TaskDone(1) do begin Sleep(20); Inc(n); end;
    SelectResult(1);
    Check('the list of accounts leaves the emptied ones out', ResultCount() = 1);
    LoadIni(PChar('does-not-exist.ini'));
    if FileExists('afaudit-old.dat') then DeleteFile('afaudit-old.dat');
  end
  else
    WriteLn('       (afold.dat is not here: ProfileDays was not tried)');

  { ---- round 3 of 2026-09-23: /listprofiles in any order, !lastvisitors for the players, names
    that look like the forms, and how long a load takes ---- }
  StartedTask := TSlotFlag(GetProcedureAddress(Lib, 'Command_Started_Task'));
  ClearOnline := TSelect(GetProcedureAddress(Lib, 'Clear_Online'));
  Check('the exports of round 3 are found', Assigned(StartedTask) and Assigned(ClearOnline));
  ClearOnline(7);
  UseIni(['CommandCooldownSeconds = 0', 'HeavyCooldownSeconds = 0', 'MaxListsPerSecond = 60',
    'JournalSeconds = 0', 'PlayerProfiles = all']);
  R3Db := ExpandFileName('afround3.dat');
  if FileExists(R3Db) then DeleteFile(R3Db);
  StartLoad(PChar(R3Db)); WaitLoad;
  Check('round 3 starts on an empty database', (not LoadFailed()) and (ProfileCount() = 0));
  if Assigned(LoadMillis) then
    Check(Format('the library says how long the load took (%d ms)', [LoadMillis()]),
      (LoadMillis() >= 0) and (LoadMillis() < 60000))
  else
    Check('the library says how long the load took', False);
  { five accounts, each different in everything: the first one is emptied and taken again last,
    so its number is the lowest and its first visit the newest }
  RGhost := LoginPlayer('Ghost', '', 'HW-R3-G', '51.0.0.9', True);
  Sleep(40);
  RDelta := LoginPlayer('Delta', '', 'HW-R3-D', '51.0.0.1', True);
  Sleep(40);
  for n := 1 to 4 do RAlpha := LoginPlayer('alpha', 'S-R3-A', 'HW-R3-A', '51.0.0.2', True);
  Sleep(40);
  RCharlie := LoginPlayer('Charlie2', 'S-R3-C', 'HW-R3-C', '51.0.0.3', True);
  LoginPlayer('Charlie3', 'S-R3-C', 'HW-R3-C2', '51.0.0.4', True);
  LoginPlayer('Charlie', 'S-R3-C', 'HW-R3-C', '51.0.0.5', True);
  Sleep(40);
  RBravo := LoginPlayer('bravo', '', 'HW-R3-B', '51.0.0.6', True);
  LoginPlayer('bravo', '', 'HW-R3-B', '51.0.0.6', True);
  Rollback(RGhost, 'Ghost', 'HW-R3-G', '51.0.0.9', 1 or 4 or 8);
  Sleep(40);
  REcho := LoginPlayer('Echo', '', 'HW-R3-E', '51.0.0.7', True);
  Check('five accounts, the emptied number taken again by the last one',
    (REcho = 0) and (RDelta = 1) and (RAlpha = 2) and (RCharlie = 3) and (RBravo = 4) and (ProfileCount() = 5));
  AddTime(RCharlie, 900, 0); Sleep(40);
  AddTime(REcho, 600, 0); Sleep(40);
  AddTime(RBravo, 50, 0); Sleep(40);
  AddTime(RDelta, 300, 0); Sleep(40);
  AddTime(RAlpha, 100, 0);

  Said := AskAll(24, 'listprofiles', '', True);
  Check('/listprofiles alone: by number, lowest first, as it always was',
    InOrder(Said, ['Echo', 'Delta', 'alpha', 'Charlie', 'bravo']) and
    (Pos('by account number, lowest first', Said) > 0) and (Pos('Logins', Said) = 0));
  Said := AskAll(24, 'listprofiles', 'time', True);
  Check('a sort alone goes down: the most time first',
    InOrder(Said, ['Charlie', 'Echo', 'Delta', 'alpha', 'bravo']) and
    (Pos('by time played, highest first', Said) > 0));
  Said := AskAll(24, 'listprofiles', 'played asc', True);
  Check('asc turns it round (played is the time)', InOrder(Said, ['bravo', 'alpha', 'Delta', 'Echo', 'Charlie']));
  Said := AskAll(24, 'listprofiles', 'asc', True);
  Check('asc alone keeps the number', InOrder(Said, ['Echo', 'Delta', 'alpha', 'Charlie', 'bravo']));
  Said := AskAll(24, 'listprofiles', 'desc', True);
  Check('desc alone: by number, highest first', InOrder(Said, ['bravo', 'Charlie', 'alpha', 'Delta', 'Echo']));
  Said := AskAll(24, 'listprofiles', 'name', True);
  Check('by name: the end of the alphabet first, big and small letters alike',
    InOrder(Said, ['Echo', 'Delta', 'Charlie', 'bravo', 'alpha']));
  Said := AskAll(24, 'listprofiles', 'nick asc', True);
  Check('nick asc: A to Z', InOrder(Said, ['alpha', 'bravo', 'Charlie', 'Delta', 'Echo']));
  Said := AskAll(24, 'listprofiles', 'logins', True);
  Line := TrimRight(LineWith(Said, '| alpha '));
  Check('by logins, the most first, one number as many by account number, the logins shown',
    InOrder(Said, ['alpha', 'Charlie', 'bravo', 'Echo', 'Delta']) and (Pos('Logins', Said) > 0) and
    (Copy(Line, Length(Line) - 1, 2) = ' 4'));
  Said := AskAll(24, 'listprofiles', 'joins asc', True);
  Check('joins asc', InOrder(Said, ['Echo', 'Delta', 'bravo', 'Charlie', 'alpha']));
  Said := AskAll(24, 'listprofiles', 'first', True);
  Check('by the first visit, the newest first (a number taken again is new), the date shown',
    InOrder(Said, ['Echo', 'bravo', 'Charlie', 'alpha', 'Delta']) and (Pos('First seen', Said) > 0) and
    (Pos(FormatDateTime('yyyy-mm-dd', Now), Said) > 0));
  Said := AskAll(24, 'listprofiles', 'registered asc', True);
  Check('registered asc: the oldest first', InOrder(Said, ['Delta', 'alpha', 'Charlie', 'bravo', 'Echo']));
  Said := AskAll(24, 'listprofiles', 'new', True);
  Check('new is the first visit as well', InOrder(Said, ['Echo', 'bravo', 'Charlie', 'alpha', 'Delta']));
  Said := AskAll(24, 'listprofiles', 'last', True);
  Check('by the last visit, the newest first, the date shown',
    InOrder(Said, ['alpha', 'Delta', 'bravo', 'Echo', 'Charlie']) and (Pos('Last seen', Said) > 0));
  Said := AskAll(24, 'listprofiles', 'seen asc', True);
  Check('seen asc', InOrder(Said, ['Charlie', 'Echo', 'bravo', 'Delta', 'alpha']));
  Said := AskAll(24, 'listprofiles', 'names', True);
  Check('by the number of names', InOrder(Said, ['Charlie', 'Echo', 'Delta', 'alpha', 'bravo']));
  Said := AskAll(24, 'listprofiles', 'addresses', True);
  Check('by the number of addresses', InOrder(Said, ['Charlie', 'Echo', 'Delta', 'alpha', 'bravo']));
  Said := AskAll(24, 'listprofiles', 'ips asc', True);
  Check('ips asc', InOrder(Said, ['Echo', 'Delta', 'alpha', 'bravo', 'Charlie']));
  Said := AskAll(24, 'listprofiles', 'computers', True);
  Check('by the number of computers', InOrder(Said, ['Charlie', 'Echo', 'Delta', 'alpha', 'bravo']));
  Said := AskAll(24, 'listprofiles', 'hwids asc', True);
  Check('hwids asc', InOrder(Said, ['Echo', 'Delta', 'alpha', 'bravo', 'Charlie']));
  Said := AskAll(24, 'listprofiles', 'steam', True);
  Check('by the number of Steam ids', InOrder(Said, ['alpha', 'Charlie', 'Echo', 'Delta', 'bravo']));
  Said := AskAll(24, 'listprofiles', 'time+', True);
  Check('the old time+ still sorts up', InOrder(Said, ['bravo', 'alpha', 'Delta', 'Echo', 'Charlie']));
  Said := AskAll(24, 'listprofiles', 'id-', True);
  Check('the old id- still sorts down', InOrder(Said, ['bravo', 'Charlie', 'alpha', 'Delta', 'Echo']));
  Said := AskAll(24, 'listprofiles', 'count', True);
  Check('the old count is the logins (it sorted by nothing the list held)',
    InOrder(Said, ['alpha', 'Charlie', 'bravo', 'Echo', 'Delta']));
  Said := AskAll(24, 'listprofiles', 'latest', True);
  Check('the old latest is the last visit', InOrder(Said, ['alpha', 'Delta', 'bravo', 'Echo', 'Charlie']));
  Said := AskAll(24, 'listprofiles', 'foo', True);
  Check('a word that is no sort is said, with the sorts there are',
    (Pos('Not a sort: foo.', Said) > 0) and (Pos('sort = id, name, time', Said) > 0) and (Pos('| alpha ', Said) = 0));
  Said := AskAll(24, 'listprofiles', 'time name', True);
  Check('two sorts at once are refused', Pos('One sort at a time', Said) > 0);
  AskAll(24, 'listprofiles', 'time', True);
  Said := AskAll(24, 'statusap', '1', True);
  Check('<command>p takes the row the sorted list shows',
    Pos('Account #' + IntToStr(RCharlie) + ': Charlie', Said) > 0);
  Said := AskAll(25, '!listprofiles', 'time asc', False);
  Check('a player asks the same way in the chat (PlayerProfiles = all)',
    InOrder(Said, ['bravo', 'alpha', 'Delta', 'Echo', 'Charlie']));
  Said := AskAll(25, 'af_next', '', False);
  Check('and the page after it is drawn in the same order', InOrder(Said, ['bravo', 'alpha', 'Delta', 'Echo', 'Charlie']));

  { the visitors of the server: who, when and for how long for the players; the whole table for
    an admin who asks with / }
  VisitNote(RDelta, PChar('Delta'), PChar('HW-R3-D'), PChar('51.0.0.1'), 120);
  VisitNote(RAlpha, PChar('alpha'), PChar('HW-R3-A'), PChar('51.0.0.2'), 60);
  VisitNote(RCharlie, PChar('Charlie'), PChar('HW-R3-C'), PChar('51.0.0.5'), 300);
  UseIni(['CommandCooldownSeconds = 0', 'HeavyCooldownSeconds = 0', 'MaxListsPerSecond = 60']);
  Check('PlayerLastVisitors is "all" when settings.ini says nothing', SettingInt(PChar('playerlastvisitors')) = 2);
  Said := AskAll(26, '!lastvisitors', '', False);
  Check('a player gets who came, when and for how long - no address, no place',
    (Pos('Last visit', Said) > 0) and (Pos('| Charlie', Said) > 0) and (Pos('Address', Said) = 0) and
    (Pos('51.0.', Said) = 0) and (Pos('Unknown', Said) = 0));
  Said := AskAll(26, 'lastvisitors', '', False);
  Check('the same with / (PlayerCommands)', (Pos('Last visit', Said) > 0) and (Pos('51.0.', Said) = 0));
  Said := AskAll(27, '!lastvisitors', '', True);
  Check('an admin who asks in the chat gets the same simple list',
    (Pos('| Charlie', Said) > 0) and (Pos('Address', Said) = 0) and (Pos('51.0.', Said) = 0));
  Said := AskAll(27, 'lastvisitors', '', True);
  Check('/lastvisitors of an admin stays the whole table', (Pos('Address', Said) > 0) and (Pos('51.0.0.5', Said) > 0));
  Said := AskAll(33, 'lastvisitors', '', True);
  Check('and so does it for the console', Pos('51.0.0.5', Said) > 0);
  Said := AskAll(27, '!lastvisitors', 'masked', True);
  Check('an admin keeps the filters in the chat (no visit here came through a VPN)', Pos('No results', Said) > 0);
  Said := AskAll(26, '!lastvisitors', 'masked', False);
  Check('a player does not: which visitors used a VPN is a place too', Pos('| Charlie', Said) > 0);
  Said := AskAll(26, '!lastvisitors', 'alpha', False);
  Check('a player who may ask only about themselves cannot name somebody in it',
    Pos('only about yourself', Said) > 0);
  Said := AskAll(26, 'antifake', '', False);
  Check('the help of a player offers !lastvisitors', Pos('!lastvisitors [range]', Said) > 0);
  UseIni(['CommandCooldownSeconds = 0', 'HeavyCooldownSeconds = 0', 'PlayerLastVisitors = off']);
  Said := AskAll(26, '!lastvisitors', '', False);
  Check('PlayerLastVisitors = off keeps it for the admins', (Pos('for admins', Said) > 0) and (Pos('| Charlie', Said) = 0));
  Said := AskAll(26, 'antifake', '', False);
  Check('and the help does not offer it then', Pos('!lastvisitors', Said) = 0);
  UseIni(['CommandCooldownSeconds = 0', 'HeavyCooldownSeconds = 0', 'PlayerLastVisitors = self']);
  Said := AskAll(26, '!lastvisitors', '', False);
  Check('self means admins only: the list is about the others',
    (SettingInt(PChar('playerlastvisitors')) = 1) and (Pos('for admins', Said) > 0));
  UseIni(['PlayerLastVisitors = bogus']);
  Check('a word it does not know keeps "all" and is counted',
    (SettingInt(PChar('playerlastvisitors')) = 2) and (Pos('(1 out of range', StrPas(SettingStr(PChar('message')))) > 0));

  { names that look like the forms: "#1", "#Kowal", "@2", "^1", "top", "count", two words }
  RHash1 := LoginPlayer('#1', '', 'HW-R3-H1', '52.0.0.1', True);
  RHashKowal := LoginPlayer('#Kowal', '', 'HW-R3-HK', '52.0.0.2', True);
  RAt2 := LoginPlayer('@2', '', 'HW-R3-AT', '52.0.0.3', True);
  RCaret1 := LoginPlayer('^1', '', 'HW-R3-CA', '52.0.0.4', True);
  RTopGun := LoginPlayer('Top Gun', '', 'HW-R3-TG', '52.0.0.5', True);
  RTop := LoginPlayer('top', '', 'HW-R3-TP', '52.0.0.6', True);
  RCount := LoginPlayer('count', '', 'HW-R3-CN', '52.0.0.7', True);
  RBigTime := LoginPlayer('Big Time', '', 'HW-R3-BT', '52.0.0.8', True);
  RBig := LoginPlayer('Big', '', 'HW-R3-BG', '52.0.0.9', True);
  RHash99 := LoginPlayer('#99999', '', 'HW-R3-H9', '52.0.0.10', True);
  Check('ten accounts named like the forms', (RHash99 = RHash1 + 9) and (RCount > RTop));
  UseIni(['CommandCooldownSeconds = 0', 'HeavyCooldownSeconds = 0', 'MaxListsPerSecond = 60']);
  Said := AskAll(28, 'multi', '#1', True);
  Check('#1 is still account 1, and the player named #1 is pointed out',
    (Pos('#1 is account #1 (Delta).', Said) > 0) and
    (Pos('The player named #1 is account #' + IntToStr(RHash1) + ': write "#1" to mean them.', Said) > 0) and
    (Pos('Account #1 ', Said) > 0));
  Said := AskAll(28, 'multi', '"#1"', True);
  Check('"#1" in double quotes is the player named #1, with no word about account 1',
    (Pos('Account #' + IntToStr(RHash1) + ' ', Said) > 0) and (Pos('is account #1', Said) = 0));
  Said := AskAll(28, 'statusa', '"#1"', True);
  Check('the same for /statusa', Pos('Account #' + IntToStr(RHash1) + ': #1', Said) > 0);

  { the ties of an account are what /multi tells: PlayerMulti decides who sees them in a status,
    not PlayerStatus (a player saw the accounts of strangers from one school address) }
  RSchoolA := LoginPlayer('SchoolA', '', 'HW-R3-SA', '53.0.0.1', True);
  RSchoolB := LoginPlayer('SchoolB', '', 'HW-R3-SB', '53.0.0.1', True);
  SetOnline(20, RSchoolA, PChar('SchoolA'), PChar('53.0.0.1'), PChar('HW-R3-SA'), False);
  UseIni(['CommandCooldownSeconds = 0', 'HeavyCooldownSeconds = 0', 'MaxListsPerSecond = 60',
    'PlayerStatus = self', 'PlayerMulti = off']);
  Said := AskAll(20, '!mystatus', '', False);
  Check('!mystatus of a player without /multi says nothing of the accounts tied to theirs',
    (Pos('Account #' + IntToStr(RSchoolA) + ': SchoolA', Said) > 0) and (Pos('SchoolB', Said) = 0) and
    (Pos('Looks like one person', Said) = 0) and (Pos('share something', Said) = 0));
  Said := AskAll(28, 'statusa', '#' + IntToStr(RSchoolA), True);
  Check('an admin sees them', Pos('SchoolB', Said) > 0);
  UseIni(['CommandCooldownSeconds = 0', 'HeavyCooldownSeconds = 0', 'MaxListsPerSecond = 60',
    'PlayerStatus = self', 'PlayerMulti = self']);
  Said := AskAll(20, '!mystatus', '', False);
  Check('with PlayerMulti = self the player sees the account behind the same address (from the index)',
    Pos('SchoolB', Said) > 0);
  ClearOnline(20);

  { the game console draws one byte per letter in the code page of the player's Windows: a Polish
    letter goes out as its byte of 1250 (UTF-8 was drawn as two letters, "Poland, ?wilcza") }
  n := LoginPlayer('SwTest', '', 'HW-SWT', '10.9.8.7', True);
  VisitNote(n, PChar(#$C5#$9A'wilcza'), PChar('HW-SWT'), PChar('10.9.8.7'), 60);
  Said := AskAll(29, 'visits', '#' + IntToStr(n), True);
  Check('a Polish letter reaches the console as its byte of code page 1250',
    (Pos(Chr($8C) + 'wilcza', Said) > 0) and (Pos(#$C5#$9A'wilcza', Said) = 0));
  Said := AskAll(28, 'multi', '#99999', True);
  Check('a #number no account has takes the player of that name, and says so',
    (Pos('There is no account #99999: the player named #99999 (account #' + IntToStr(RHash99) + ') was taken.', Said) > 0) and
    (Pos('Account #' + IntToStr(RHash99) + ' ', Said) > 0));
  Said := AskAll(28, 'multi', '#Kowal', True);
  Check('a name that begins with # is a name (it could not be named at all)',
    Pos('Account #' + IntToStr(RHashKowal) + ' ', Said) > 0);
  Said := AskAll(28, 'multi', '@2', True);
  Check('@2 typed is a name, not row 2 of the last list', Pos('Account #' + IntToStr(RAt2) + ' ', Said) > 0);
  Said := AskAll(28, 'multi', '^1', True);
  Check('^1 typed is a name, not position 1 of ZitroStats', Pos('Account #' + IntToStr(RCaret1) + ' ', Said) > 0);
  Said := AskAll(28, 'multi', 'Top Gun', True);
  Check('/multi Top Gun is the player, not the computers of the top',
    Pos('Account #' + IntToStr(RTopGun) + ' ', Said) > 0);
  Said := AskAll(28, 'multi', '"top"', True);
  Check('/multi "top" is the player named top', Pos('Account #' + IntToStr(RTop) + ' ', Said) > 0);
  Said := AskAll(28, 'multi', 'top', True);
  Check('/multi top is still the computers the most accounts played from',
    Pos('The computers that the most accounts', Said) > 0);
  Said := AskAll(33, 'privacy', 'n count', True);
  Check('/privacy n count: a word alone is the name, also one that looks like a sort',
    (Pos('no account of its own', Said) = 0) and (Pos('| count ', Said) > 0));
  Said := AskAll(33, 'privacy', 'n count count', True);
  Check('and a sort behind the name still sorts', Pos('| count ', Said) > 0);
  Said := AskAll(33, 'privacyall', 'n "count"', True);
  Check('a name in double quotes for /privacyall', Pos('| count ', Said) > 0);
  VisitNote(RBigTime, PChar('Big Time'), PChar('HW-R3-BT'), PChar('52.0.0.8'), 60);
  VisitNote(RBig, PChar('Big'), PChar('HW-R3-BG'), PChar('52.0.0.9'), 60);
  Said := AskAll(29, 'visits', 'Big Time', True);
  Check('/visits takes a name of two words whole ("Big Time" was "Big")', Pos('Big Time', Said) > 0);
  Said := AskAll(29, 'visits', '"Big Time" 7d', True);
  Check('and in double quotes with a range behind it', Pos('Big Time', Said) > 0);
  RDots := LoginPlayer('a..b', '', 'HW-R3-DT', '52.0.0.11', True);
  VisitNote(RDots, PChar('a..b'), PChar('HW-R3-DT'), PChar('52.0.0.11'), 60);
  Said := AskAll(29, 'visits', '"a..b"', True);
  Check('a name in double quotes with ".." in it is a name, not a range of days', Pos('| a..b ', Said) > 0);
  Said := AskAll(30, 'visits', '7d Big', False);
  Check('a range in front of a name no longer lets a "self" player ask about somebody',
    Pos('only about yourself', Said) > 0);
  Said := AskAll(29, 'checkd', 'n "Big Time"', True);
  Check('/checkd looks for a value in double quotes without them', Pos('| Big Time', Said) > 0);
  Said := AskAll(29, 'af_search', '"#1"', True);
  Check('/af_search looks for a text in double quotes without them',
    Pos('#' + IntToStr(RHash1) + ' | #1 ', Said) > 0);
  SetOnline(31, RHash1, PChar('#1'), PChar('52.0.0.1'), PChar('HW-R3-H1'), False);
  Said := AskAll(29, 'locate', '"#1"', True);
  Check('/locate "#1" is the player named #1', Pos('  31 | #1', Said) > 0);
  Said := AskAll(29, 'locate', '#1', True);
  Check('/locate #1 is account 1, and says who is named #1',
    (Pos('Account #1 is not on the server.', Said) > 0) and (Pos('The player named #1 is account #', Said) > 0));
  Said := AskAll(28, 'multiid', '31', True);
  Check('a slot form means its account and says nothing about names',
    (Pos('Account #' + IntToStr(RHash1) + ' ', Said) > 0) and (Pos('The player named', Said) = 0));
  ClearOnline(31);
  Said := AskAll(29, 'visitsof', 'United Kingdom', True);
  Check('/visitsof takes a country of two words whole', Pos('Nobody here came from United Kingdom', Said) > 0);
  Said := AskAll(29, 'visitsof', '"United Kingdom" 30d', True);
  Check('also in double quotes with a range behind it', Pos('Nobody here came from United Kingdom', Said) > 0);
  Check('a command from the chat comes with its !, and ! alone is nothing',
    (RunCmd(32, PChar('!nosuchcommand'), PChar(''), True, PChar('test')) = 0) and
    (RunCmd(32, PChar('!'), PChar(''), True, PChar('test')) = 0));
  Said := AskAll(28, 'antifake', '', True);
  Check('the help says how to name a player whose name looks like #1, and the sorts of the list',
    (Pos('a name in double quotes is always a name', Said) > 0) and
    (Pos('/listprofiles [sort] [asc|desc]', Said) > 0) and (Pos('listprofiles sort = id, name', Said) > 0));
  { AFW-11: a control character (an escape sequence of a terminal) in a log line is written as ?,
    so whoever reads the log with cat or tail sees no lines painted over }
  LogLine := TPathProc(GetProcedureAddress(Lib, 'Log_Line'));
  LogMark := 'afw11-' + IntToStr(GetTickCount64);
  LogLine(PChar(LogMark + ' x' + #27 + '[2K' + #7 + 'y'));
  LogText := '';
  for LogWait := 1 to 60 do
  begin
    Sleep(100);
    try
      with TStringList.Create do
      try
        LoadFromFile('logs' + DirectorySeparator + 'antifake' + DirectorySeparator + 'antifake.log');
        LogText := Text;
      finally Free; end;
    except
      LogText := '';
    end;
    if Pos(LogMark, LogText) > 0 then Break;
  end;
  Check('a control character of a log line is written as ? (no escape sequence in antifake.log)',
    (Pos(LogMark + ' x?[2K?y', LogText) > 0) and (Pos(#27, LogText) = 0));

  LoadIni(PChar('does-not-exist.ini'));
  if FileExists(R3Db) then DeleteFile(R3Db);

  if FileExists(AuditIni) then DeleteFile(AuditIni);

  if Failures = 0 then
    WriteLn('ALL TESTS PASSED')
  else
    WriteLn(Failures, ' TEST(S) FAILED');
  Halt(Failures);
end.
