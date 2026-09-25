program afshared;

{ Engine = mysql, end to end: two game servers on one database, against a real server:
    afshared <antifake_dll.dll> <database file to start from> <host> <port> <db> <user> <password>
  The library is loaded twice (the second time from a copy under another name), so the two
  servers live in one process with nothing shared but the database.
  1. Server 11 joins with the accounts of the file and keeps their numbers.
  2. Server 12 starts without a file and gets every account, under the same numbers.
  3. A player new on server 11 appears on server 12; play time on both adds up.
  4. A decision of an admin on one server reaches the other, and so does taking it back.
  5. A player new on both servers at once becomes one account: the Steam account, or without
     Steam the computer.
  6. A visit on one server reaches the other.
  7. Server 11 starts again: everything comes back from the database.
  8. Server 11 starts without the database: it works from its file, and what it collected goes in
     once the database answers.
  9. Server 12 starts with a file that cannot be read: the database fills it again.
  Works in server ids 11 and 12 and drops its tables at the end: give it a database of its own.
  The database file is copied first, the one named here is only read.

  Built from the library folder (source_dll), on Windows with FPC 3.2 for i386:
    mkdir build\test
    fpc -Mobjfpc -Sh -O2 -Fu. -FUbuild\test -FEbuild\test tests\afshared.pas
  and run in build\test next to a copy of build\win32\antifake_dll.dll. }

{$mode objfpc}{$H+}

uses
  SysUtils, Classes, dynlibs, mysqlwire;

type
  TLoadIni = function(Path: PChar): Boolean; cdecl;
  TPathProc = procedure(Path: PChar); cdecl;
  TFlag = function: Boolean; cdecl;
  TCount = function: LongInt; cdecl;
  TNameOf = function(Index: LongInt): PChar; cdecl;
  TLogin = function(Name, Steam, Hwid, IP: PChar; IncLogin: Boolean): LongInt; cdecl;
  TAddTime = procedure(Index, Played, Spec: LongInt); cdecl;
  TFind = function(S: PChar): LongInt; cdecl;
  TSetVerdict = function(A, B, Verdict: LongInt; Who: PChar): Boolean; cdecl;
  TGetVerdict = function(A, B: LongInt): PChar; cdecl;
  TReport = function: PChar; cdecl;
  TVisitNote = procedure(Index: LongInt; Name, Hwid, IP: PChar; Secs: LongInt); cdecl;
  TVisitCount = function(Index: LongInt): LongInt; cdecl;
  TPlain = procedure; cdecl;
  TStartTask = procedure(PlayerID, TaskType, Kind, SortMethod, TargetIndex, MaxResults: LongInt;
    SearchPhrase: PChar; SearchAll, ComparePartial, FilterFirst, FilterLast, FilterAll: Boolean); cdecl;
  TSlotFlag = function(Slot: LongInt): Boolean; cdecl;
  TSelect = procedure(Slot: LongInt); cdecl;
  TItemText = function(ItemIdx: LongInt): PChar; cdecl;

  TServer = record
    Id: LongInt;
    Dll, Db, Ini: AnsiString;
    Lib: TLibHandle;
    LoadIni: TLoadIni;
    StartLoad, StartSave: TPathProc;
    LoadDone, LoadFailed, SaveBusy, SaveAfterLoad: TFlag;
    Count: TCount;
    NameOf, TimeOf: TNameOf;
    Login: TLogin;
    AddTime: TAddTime;
    BySteam, ByHwid: TFind;
    SetVerdict: TSetVerdict;
    GetVerdict: TGetVerdict;
    Status, LoadNote: TReport;
    VisitNote: TVisitNote;
    VisitCount: TVisitCount;
    Cleanup: TPlain;
    StartTask: TStartTask;
    TaskDone: TSlotFlag;
    SelectResult: TSelect;
    ResultCount: TCount;
    ItemTime: TItemText;
  end;

var
  Failures: LongInt = 0;
  A, B: TServer;
  C: TMyConnection;
  n0, i, Ia, Ib, Ja, Jb, k: LongInt;
  Names0: array of AnsiString;
  Same: Boolean;
  S: AnsiString;

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

procedure CopyFileTo(const Src, Dst: AnsiString);
var
  F1, F2: TFileStream;
begin
  F1 := TFileStream.Create(Src, fmOpenRead or fmShareDenyNone);
  try
    F2 := TFileStream.Create(Dst, fmCreate);
    try
      F2.CopyFrom(F1, 0);
    finally F2.Free; end;
  finally F1.Free; end;
end;

procedure DeleteMatching(const Mask: AnsiString);
var
  SR: TSearchRec;
  Dir: AnsiString;
begin
  Dir := ExtractFilePath(Mask);
  if FindFirst(Mask, faAnyFile, SR) = 0 then
  begin
    repeat
      DeleteFile(Dir + SR.Name);
    until FindNext(SR) <> 0;
    FindClose(SR);
  end;
end;

function CountMatching(const Mask: AnsiString): LongInt;
var
  SR: TSearchRec;
begin
  Result := 0;
  if FindFirst(Mask, faAnyFile, SR) = 0 then
  begin
    repeat
      Inc(Result);
    until FindNext(SR) <> 0;
    FindClose(SR);
  end;
end;

procedure WriteIni(var Srv: TServer; const Engine, Port: AnsiString);
var
  L: TStringList;
begin
  L := TStringList.Create;
  try
    L.Add('[MySQL]');
    L.Add('Engine = ' + Engine);
    L.Add('MysqlHost = ' + ParamStr(3));
    L.Add('MysqlPort = ' + Port);
    L.Add('MysqlDatabase = ' + ParamStr(5));
    L.Add('MysqlUser = ' + ParamStr(6));
    L.Add('MysqlPassword = ' + ParamStr(7));
    L.Add('MysqlServerId = ' + IntToStr(Srv.Id));
    L.Add('MysqlCodepage = 1250');
    L.Add('SyncSeconds = 1');
    L.SaveToFile(Srv.Ini);
  finally L.Free; end;
  Srv.LoadIni(PChar(Srv.Ini));
end;

procedure Open(var Srv: TServer);
begin
  Srv.Lib := LoadLibrary(Srv.Dll);
  if Srv.Lib = NilHandle then begin WriteLn('no library ', Srv.Dll); Halt(1); end;
  Srv.LoadIni := TLoadIni(GetProcedureAddress(Srv.Lib, 'Load_Settings'));
  Srv.StartLoad := TPathProc(GetProcedureAddress(Srv.Lib, 'Start_Load_Binary'));
  Srv.StartSave := TPathProc(GetProcedureAddress(Srv.Lib, 'Start_Save_Binary'));
  Srv.LoadDone := TFlag(GetProcedureAddress(Srv.Lib, 'Is_Load_Complete'));
  Srv.LoadFailed := TFlag(GetProcedureAddress(Srv.Lib, 'Is_Load_Error'));
  Srv.SaveBusy := TFlag(GetProcedureAddress(Srv.Lib, 'Is_Save_In_Progress'));
  Srv.SaveAfterLoad := TFlag(GetProcedureAddress(Srv.Lib, 'Save_After_Load'));
  Srv.Count := TCount(GetProcedureAddress(Srv.Lib, 'Get_Profile_Count'));
  Srv.NameOf := TNameOf(GetProcedureAddress(Srv.Lib, 'Get_Profile_Active_Name'));
  Srv.TimeOf := TNameOf(GetProcedureAddress(Srv.Lib, 'Get_Profile_Time_Played'));
  Srv.Login := TLogin(GetProcedureAddress(Srv.Lib, 'Login_Player'));
  Srv.AddTime := TAddTime(GetProcedureAddress(Srv.Lib, 'Add_Profile_Time'));
  Srv.BySteam := TFind(GetProcedureAddress(Srv.Lib, 'Find_Profile_By_Steam'));
  Srv.ByHwid := TFind(GetProcedureAddress(Srv.Lib, 'Find_Profile_By_HWID'));
  Srv.SetVerdict := TSetVerdict(GetProcedureAddress(Srv.Lib, 'Set_Multi_Verdict'));
  Srv.GetVerdict := TGetVerdict(GetProcedureAddress(Srv.Lib, 'Get_Multi_Verdict'));
  Srv.Status := TReport(GetProcedureAddress(Srv.Lib, 'Get_Mysql_Status'));
  Srv.LoadNote := TReport(GetProcedureAddress(Srv.Lib, 'Get_Load_Note'));
  Srv.VisitNote := TVisitNote(GetProcedureAddress(Srv.Lib, 'Visit_Note'));
  Srv.VisitCount := TVisitCount(GetProcedureAddress(Srv.Lib, 'Get_Visit_Count'));
  Srv.Cleanup := TPlain(GetProcedureAddress(Srv.Lib, 'Cleanup'));
  Srv.StartTask := TStartTask(GetProcedureAddress(Srv.Lib, 'Start_Search_Task'));
  Srv.TaskDone := TSlotFlag(GetProcedureAddress(Srv.Lib, 'Is_Task_Complete'));
  Srv.SelectResult := TSelect(GetProcedureAddress(Srv.Lib, 'Select_Task_Result'));
  Srv.ResultCount := TCount(GetProcedureAddress(Srv.Lib, 'Get_Result_Count'));
  Srv.ItemTime := TItemText(GetProcedureAddress(Srv.Lib, 'Get_Result_Item_TimePlayed'));
end;

{ the seconds of the newest visit of an account, -1 when it has none }
function NewestVisitSeconds(var Srv: TServer; Index: LongInt): Int64;
var
  T: LongInt;
begin
  Result := -1;
  { the visits of one account, the newest first }
  Srv.StartTask(1, 12, 0, 7, Index, 0, PChar(''), False, False, False, False, False);
  T := 0;
  while (T < 300) and not Srv.TaskDone(1) do begin Sleep(10); Inc(T); end;
  Srv.SelectResult(1);
  if Srv.ResultCount() > 0 then Result := StrToInt64Def(StrPas(Srv.ItemTime(0)), -1);
end;

{ waits until the newest visit of the account has these seconds, at most 12 seconds }
function WaitVisitSeconds(var Srv: TServer; Index: LongInt; Seconds: Int64): Boolean;
var
  T: LongInt;
  Got: Int64;
begin
  T := 0;
  repeat
    Got := NewestVisitSeconds(Srv, Index);
    Result := Got = Seconds;
    if Result then Exit;
    Sleep(100);
    Inc(T);
  until T > 120;
  WriteLn('       S', Srv.Id, ': the visit has ', Got, ' s, not ', Seconds);
end;

{ the script's way of stopping: a full write, then Cleanup (the last exchange), then the library
  goes. Cleanup has to come first: threads cannot end while Windows unloads a library. }
procedure Close(var Srv: TServer);
var
  T: LongInt;
begin
  Srv.StartSave(PChar(Srv.Db));
  Sleep(50);
  T := 0;
  while (T < 600) and Srv.SaveBusy() do begin Sleep(50); Inc(T); end;
  Srv.Cleanup();
  FreeLibrary(Srv.Lib);
  Srv.Lib := NilHandle;
end;

procedure Load(var Srv: TServer);
var
  T: LongInt;
begin
  Srv.StartLoad(PChar(Srv.Db));
  T := 0;
  while (T < 1200) and not Srv.LoadDone() do begin Sleep(50); Inc(T); end;
  WriteLn('       S', Srv.Id, ': ', StrPas(Srv.LoadNote()));
  { the script writes the file as soon as it sees the load, when the library asks for it }
  if Srv.SaveAfterLoad() then
  begin
    Srv.StartSave(PChar(Srv.Db));
    Sleep(50);
    T := 0;
    while (T < 600) and Srv.SaveBusy() do begin Sleep(50); Inc(T); end;
  end;
end;

function TimeOf(var Srv: TServer; Index: LongInt): Int64;
begin
  Result := StrToInt64Def(StrPas(Srv.TimeOf(Index)), -1);
end;

{ waits until the other server has it, at most 12 seconds }
function WaitFor(var Srv: TServer; const Steam: AnsiString; Played: Int64): Boolean;
var
  T, Idx: LongInt;
begin
  T := 0;
  repeat
    Idx := Srv.BySteam(PChar(Steam));
    Result := (Idx >= 0) and ((Played < 0) or (TimeOf(Srv, Idx) = Played));
    if Result then Exit;
    Sleep(100);
    Inc(T);
  until T > 120;
  if Idx >= 0 then WriteLn('       S', Srv.Id, ' has ', Steam, ' with ', TimeOf(Srv, Idx), ' s, not ', Played)
  else WriteLn('       S', Srv.Id, ' does not have ', Steam);
end;

{ the same for a player without Steam, found by the computer }
function WaitForHwid(var Srv: TServer; const Hwid: AnsiString; Played: Int64): Boolean;
var
  T, Idx: LongInt;
begin
  T := 0;
  repeat
    Idx := Srv.ByHwid(PChar(Hwid));
    Result := (Idx >= 0) and (TimeOf(Srv, Idx) = Played);
    if Result then Exit;
    Sleep(100);
    Inc(T);
  until T > 120;
  if Idx >= 0 then WriteLn('       S', Srv.Id, ' has ', Hwid, ' with ', TimeOf(Srv, Idx), ' s, not ', Played)
  else WriteLn('       S', Srv.Id, ' does not have ', Hwid);
end;

function WaitVerdict(var Srv: TServer; X, Y: LongInt; const Starts: AnsiString): Boolean;
var
  T: LongInt;
begin
  T := 0;
  repeat
    S := StrPas(Srv.GetVerdict(X, Y));
    if Starts = '' then Result := S = '' else Result := Copy(S, 1, Length(Starts)) = Starts;
    if Result then Exit;
    Sleep(100);
    Inc(T);
  until T > 120;
  WriteLn('       S', Srv.Id, ' says "', S, '"');
end;

function Scalar(const SQL: AnsiString): AnsiString;
var
  R: TMyResult;
begin
  R := C.Query(SQL);
  try
    if R.RowCount > 0 then Result := R.Str(0, 0) else Result := '';
  finally R.Free; end;
end;

procedure DropTables;
begin
  C.Exec('DROP TABLE IF EXISTS af_db_profiles');
  C.Exec('DROP TABLE IF EXISTS af_db_verdicts');
  C.Exec('DROP TABLE IF EXISTS af_db_geo');
  C.Exec('DROP TABLE IF EXISTS af_db_meta');
end;

procedure Tidy;
begin
  DeleteMatching(ExpandFileName('afshared-a.*'));
  DeleteMatching(ExpandFileName('afshared-b.*'));
end;

begin
  if ParamCount < 6 then
  begin
    WriteLn('Usage: afshared <antifake_dll.dll> <database file> <host> <port> <db> <user> [password]');
    Halt(2);
  end;
  Tidy;
  A.Id := 11;
  A.Dll := ExpandFileName(ParamStr(1));
  A.Db := ExpandFileName('afshared-a.dat');
  A.Ini := ExpandFileName('afshared-a.ini');
  B.Id := 12;
  B.Dll := ExpandFileName('afshared-b.dll');
  B.Db := ExpandFileName('afshared-b.dat');
  B.Ini := ExpandFileName('afshared-b.ini');
  CopyFileTo(A.Dll, B.Dll);
  CopyFileTo(ParamStr(2), A.Db);

  C := TMyConnection.Create;
  C.Host := ParamStr(3);
  C.Port := StrToIntDef(ParamStr(4), 3306);
  C.Database := ParamStr(5);
  C.User := ParamStr(6);
  C.Password := ParamStr(7);
  C.Connect;
  DropTables;

  { what the file holds, read the ordinary way }
  Open(A);
  WriteIni(A, 'local', ParamStr(4));
  Load(A);
  n0 := A.Count();
  SetLength(Names0, n0);
  for i := 0 to n0 - 1 do Names0[i] := StrPas(A.NameOf(i));
  WriteLn(Format('       the file: %d accounts', [n0]));

  { ---- 1. the first server joins ---- }
  WriteIni(A, 'mysql', ParamStr(4));
  Load(A);
  Check('server 11 joins the database', (not A.LoadFailed()) and (Pos('joined the database', StrPas(A.LoadNote())) > 0));
  Same := A.Count() <= n0;
  for i := 0 to A.Count() - 1 do
    if (StrPas(A.NameOf(i)) <> Names0[i]) and (Names0[i] <> '') then Same := False;
  Check('every account keeps its number', Same and (A.Count() > 0));
  Check('the numbers are kept beside the file', FileExists(ChangeFileExt(A.Db, '.shared')));
  Check('the database has the accounts', StrToIntDef(Scalar('SELECT COUNT(*) FROM af_db_profiles'), 0) > 0);

  { ---- 2. the second server, without a file ---- }
  Open(B);
  WriteIni(B, 'mysql', ParamStr(4));
  Load(B);
  Same := (not B.LoadFailed()) and (B.Count() = A.Count());
  for i := 0 to B.Count() - 1 do
    if StrPas(B.NameOf(i)) <> StrPas(A.NameOf(i)) then Same := False;
  Check(Format('server 12 gets every account under the same number (%d / %d)', [B.Count(), A.Count()]), Same);
  Check('both servers are in the database', Scalar('SELECT COUNT(*) FROM af_db_meta WHERE name IN (''joined_11'', ''joined_12'')') = '2');

  { ---- 3. a new player, and play time on both servers ---- }
  Ia := A.Login(PChar('SharedAlice'), PChar('S777001'), PChar('HWIDALICE'), PChar('10.1.1.1'), True);
  A.AddTime(Ia, 60, 5);
  Check('a player new on server 11 reaches server 12', WaitFor(B, 'S777001', 60));
  Ib := B.BySteam(PChar('S777001'));
  if Ib >= 0 then
  begin
    A.AddTime(Ia, 30, 0);
    B.AddTime(Ib, 45, 0);
    Check('play time on both servers adds up (server 11)', WaitFor(A, 'S777001', 135));
    Check('play time on both servers adds up (server 12)', WaitFor(B, 'S777001', 135));
  end;

  { ---- 4. the decisions of the admins ---- }
  A.SetVerdict(0, 1, 2, PChar('tester'));
  Check('a decision on server 11 reaches server 12', WaitVerdict(B, 0, 1, '2|tester|'));
  Sleep(20);
  B.SetVerdict(0, 1, 0, PChar('tester'));
  Check('taking it back on server 12 reaches server 11', WaitVerdict(A, 0, 1, ''));
  { taking back a decision that has not reached this server yet: it used to be refused ("could
    not be written down"), and the older decision coming in afterwards took the place of it }
  if A.Count() >= 4 then
  begin
    A.SetVerdict(2, 3, 1, PChar('tester'));
    Sleep(20);
    Check('taking back a decision that has not arrived yet is accepted', B.SetVerdict(2, 3, 0, PChar('tester')));
    Check('and it reaches the server that took it (server 11)', WaitVerdict(A, 2, 3, ''));
    Sleep(2500);
    Check('and the older decision does not come back (server 12)', WaitVerdict(B, 2, 3, ''));
  end;

  { ---- 5. new on both servers at once ---- }
  Ja := A.Login(PChar('BobOnA'), PChar('S888002'), PChar('HWIDBOB1'), PChar('10.2.2.2'), True);
  Jb := B.Login(PChar('BobOnB'), PChar('S888002'), PChar('HWIDBOB2'), PChar('10.2.2.3'), True);
  A.AddTime(Ja, 10, 0);
  B.AddTime(Jb, 20, 0);
  Check('one Steam account new on both servers: one account (server 11)', WaitFor(A, 'S888002', 30));
  Check('one Steam account new on both servers: one account (server 12)', WaitFor(B, 'S888002', 30));
  Check('one row in the database', Scalar('SELECT COUNT(*) FROM af_db_profiles WHERE steam = ''S888002''') = '1');
  { without Steam the computer decides, as at a join on one server }
  Ja := A.Login(PChar('CompOnA'), PChar(''), PChar('HWIDSAME01'), PChar('10.4.4.4'), True);
  Jb := B.Login(PChar('CompOnB'), PChar(''), PChar('HWIDSAME01'), PChar('10.4.4.5'), True);
  A.AddTime(Ja, 7, 0);
  B.AddTime(Jb, 8, 0);
  Check('one computer without Steam new on both servers at once: one account (server 11)', WaitForHwid(A, 'HWIDSAME01', 15));
  Check('one computer without Steam new on both servers at once: one account (server 12)', WaitForHwid(B, 'HWIDSAME01', 15));
  Check('one row in the database for that computer',
    Scalar('SELECT COUNT(*) FROM af_db_profiles WHERE computer = ''HWIDSAME01''') = '1');

  { ---- 6. a visit ---- }
  if (Ia >= 0) and (Ib >= 0) then
  begin
    A.VisitNote(Ia, PChar('SharedAlice'), PChar('HWIDALICE'), PChar('10.1.1.1'), 100);
    k := 0;
    while (k < 120) and (B.VisitCount(Ib) < 1) do begin Sleep(100); Inc(k); end;
    Check('a visit on server 11 reaches server 12', B.VisitCount(Ib) = 1);
    { the player goes on on both servers within the cooldown: the seconds each server added
      count (the larger of the two sides used to win, 130 here) }
    if B.VisitCount(Ib) = 1 then
    begin
      A.VisitNote(Ia, PChar('SharedAlice'), PChar('HWIDALICE'), PChar('10.1.1.1'), 20);
      B.VisitNote(Ib, PChar('SharedAlice'), PChar('HWIDALICE'), PChar('10.1.1.1'), 30);
      Check('a visit both servers went on with adds up (server 11)', WaitVisitSeconds(A, Ia, 150));
      Check('a visit both servers went on with adds up (server 12)', WaitVisitSeconds(B, Ib, 150));
    end;
  end;

  { ---- 7. server 11 starts again ---- }
  Close(A);
  Open(A);
  WriteIni(A, 'mysql', ParamStr(4));
  Load(A);
  Ia := A.BySteam(PChar('S777001'));
  Check('after a new start everything comes back from the database', (not A.LoadFailed()) and (A.Count() = B.Count()) and
    (Ia >= 0) and (TimeOf(A, Ia) = 135));

  { ---- 8. server 11 starts without the database ---- }
  Close(A);
  Open(A);
  WriteIni(A, 'mysql', '1');
  Load(A);
  S := StrPas(A.LoadNote());
  Check('without the database the file is read, and the server works from it',
    (not A.LoadFailed()) and (A.Count() = B.Count()) and (Pos('works from the file', S) > 0));
  Ia := A.BySteam(PChar('S777001'));
  if Ia >= 0 then A.AddTime(Ia, 10, 0);
  Ja := A.Login(PChar('Carol'), PChar('S999003'), PChar('HWIDCAROL'), PChar('10.3.3.3'), True);
  A.AddTime(Ja, 15, 0);
  Sleep(1500);
  WriteLn('       S11: ', StrPas(A.Status()));
  WriteIni(A, 'mysql', ParamStr(4));
  Check('what server 11 collected without the database goes in once it answers (a new player)', WaitFor(B, 'S999003', 15));
  Check('what server 11 collected without the database goes in once it answers (play time)', WaitFor(B, 'S777001', 145));
  WriteLn('       S11: ', StrPas(A.Status()));

  { ---- 9. server 12 with a file that cannot be read ---- }
  Close(B);
  with TFileStream.Create(B.Db, fmOpenReadWrite) do
  try
    Size := 100;
  finally Free; end;
  Open(B);
  WriteIni(B, 'mysql', ParamStr(4));
  Load(B);
  Check('an unreadable file: the database fills the server again', (not B.LoadFailed()) and (B.Count() = A.Count()) and
    (B.BySteam(PChar('S999003')) >= 0));
  Check('the unreadable file is kept under a name of its own', (CountMatching(B.Db + '.unreadable-*') - CountMatching(B.Db + '.unreadable-*.jrn') = 1));

  Close(A);
  Close(B);
  DropTables;
  C.Free;
  Tidy;
  DeleteFile(B.Dll);
  if Failures = 0 then
    WriteLn('ALL TESTS PASSED')
  else
    WriteLn(Failures, ' TEST(S) FAILED');
  Halt(Failures);
end.
