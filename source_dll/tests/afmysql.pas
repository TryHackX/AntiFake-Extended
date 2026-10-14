program afmysql;

{ The copy of the AntiFake database in MySQL, end to end, against a real server:
    afmysql <antifake_dll.dll> <database file to start from> <host> <port> <db> <user> <password>
  1. Engine = mirror: the file is read, the copy is written, every table is read back.
  2. The same with the file gone: the copy is read and is the same as the file was.
  3. Engine = mirror with the file newer: the file is read.
  4. Engine = mirror with a file that cannot be read: the copy is read, the file is put aside.
  5. Nothing is written while the reading runs; a database that cannot be reached leaves the file.
  6. SharedSearch and SharedLinks: seen as another server of the database, the copy of step 1
     is listed under the results of /af_search and /statusa.
  Works in server id 4242 and drops its tables at the end: give it a database of its own. The
  database file is copied first, the one named here is only read.

  Built from the library folder (source_dll), on Windows with FPC 3.2 for i386:
    mkdir build\test
    fpc -Mobjfpc -Sh -O2 -Fu. -FUbuild\test -FEbuild\test tests\afmysql.pas
  and run in build\test next to a copy of build\win32\antifake_dll.dll. }

{$mode objfpc}{$H+}

uses
  SysUtils, Classes, dynlibs, base64, mysqlwire;

type
  TPathProc = procedure(Path: PChar); cdecl;
  TFlag = function: Boolean; cdecl;
  TCount = function: LongInt; cdecl;
  TNameOf = function(Index: LongInt): PChar; cdecl;
  TVisitCount = function(Index: LongInt): LongInt; cdecl;
  TReport = function: PChar; cdecl;
  TLoadIni = function(Path: PChar): Boolean; cdecl;
  TTick = procedure(Force: Boolean); cdecl;
  TSetVerdict = function(A, B, Verdict: LongInt; Who: PChar): Boolean; cdecl;
  TRunCmd = function(Slot: LongInt; Cmd, Params: PChar; IsAdmin: Boolean; Who: PChar): LongInt; cdecl;
  TSlotFlag = function(Slot: LongInt): Boolean; cdecl;
  TSlotCount = function(Slot: LongInt): LongInt; cdecl;
  TSlotLine = function(Slot, Index: LongInt): PChar; cdecl;
  TSelect = procedure(PlayerID: LongInt); cdecl;

const
  SOLDAT_NAME_MAX = 24;   { the longest name a player can have in Soldat }

var
  Failures: LongInt = 0;
  Lib: TLibHandle;
  StartLoad, StartSave: TPathProc;
  LoadDone, LoadFailed, SaveBusy, LoadBusy, SaveAfterLoad: TFlag;
  ProfileCount: TCount;
  ProfileName, ProfileSteam, SteamInfo: TNameOf;
  VisitCount: TVisitCount;
  MysqlStatus, LoadNote, NoticeLine: TReport;
  LoadIni: TLoadIni;
  MysqlTick: TTick;
  SetVerdict: TSetVerdict;
  RunCmd: TRunCmd;
  SlotBusy, TaskDone: TSlotFlag;
  OutCount: TSlotCount;
  OutLine: TSlotLine;
  RenderRes: TSelect;
  Said: AnsiString;
  DbPath, IniPath, Aside: AnsiString;
  Names1, Steams1, Steam1: array of AnsiString;
  Visits1: array of LongInt;
  n, i, Visits, WithSteam, Kept: LongInt;
  T0: QWord;
  C: TMyConnection;
  Same: Boolean;

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
  A, B: TFileStream;
begin
  A := TFileStream.Create(Src, fmOpenRead or fmShareDenyNone);
  try
    B := TFileStream.Create(Dst, fmCreate);
    try
      B.CopyFrom(A, 0);
    finally
      B.Free;
    end;
  finally
    A.Free;
  end;
end;

procedure WaitLoad;
var
  k: LongInt;
begin
  k := 0;
  while (k < 600) and not LoadDone() do begin Sleep(50); Inc(k); end;
end;

procedure WaitSave;
var
  k: LongInt;
begin
  k := 0;
  Sleep(50);
  while (k < 600) and SaveBusy() do begin Sleep(50); Inc(k); end;
end;

procedure WriteIniPort(const Engine, Port: AnsiString);
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
    L.Add('MysqlServerId = 4242');
    L.Add('MysqlCodepage = 1250');
    L.SaveToFile(IniPath);
  finally
    L.Free;
  end;
  LoadIni(PChar(IniPath));
end;

procedure WriteIni(const Engine: AnsiString);
begin
  WriteIniPort(Engine, ParamStr(4));
end;

{ this library as another server of the same database, which looks into the copies of the others;
  its players may ask about anybody (to see what a player is shown of the other servers) }
procedure WriteIniShared;
var
  L: TStringList;
begin
  L := TStringList.Create;
  try
    L.Add('[MySQL]');
    L.Add('Engine = bin');
    L.Add('MysqlHost = ' + ParamStr(3));
    L.Add('MysqlPort = ' + ParamStr(4));
    L.Add('MysqlDatabase = ' + ParamStr(5));
    L.Add('MysqlUser = ' + ParamStr(6));
    L.Add('MysqlPassword = ' + ParamStr(7));
    L.Add('MysqlServerId = 4343');
    L.Add('MysqlCodepage = 1250');
    L.Add('SharedSearch = 1');
    L.Add('SharedLinks = 1');
    L.Add('[PlayerCommands]');
    L.Add('PlayerStatus = all');
    { the ties of an account are in a player's status only when they may ask /multi about it }
    L.Add('PlayerMulti = all');
    L.SaveToFile(IniPath);
  finally
    L.Free;
  end;
  LoadIni(PChar(IniPath));
end;

{ Engine = mirror without MysqlHost }
procedure WriteIniNoHost;
var
  L: TStringList;
begin
  L := TStringList.Create;
  try
    L.Add('[MySQL]');
    L.Add('Engine = mirror');
    L.Add('MysqlServerId = 4242');
    L.SaveToFile(IniPath);
  finally
    L.Free;
  end;
  LoadIni(PChar(IniPath));
end;

{ the line of the table that holds Part, '' when there is none }
function LineWith(const Said, Part: AnsiString): AnsiString;
var
  L: TStringList;
  k: LongInt;
begin
  Result := '';
  L := TStringList.Create;
  try
    L.Text := Said;
    for k := 0 to L.Count - 1 do
      if Pos(Part, L[k]) > 0 then
      begin
        Result := L[k];
        Exit;
      end;
  finally
    L.Free;
  end;
end;

{ a command that starts a search, and the table once it is done }
function AskList(Slot: LongInt; const Cmd, Params: AnsiString; Admin: Boolean = True): AnsiString;
var
  T, k: LongInt;
begin
  Result := '';
  RunCmd(Slot, PChar(Cmd), PChar(Params), Admin, PChar('test'));
  T := 0;
  while (T < 1000) and not TaskDone(Slot) do begin Sleep(10); Inc(T); end;
  RenderRes(Slot);
  T := 0;
  while (T < 500) and SlotBusy(Slot) do begin Sleep(10); Inc(T); end;
  for k := 0 to OutCount(Slot) - 1 do Result := Result + StrPas(OutLine(Slot, k)) + #10;
end;

function AsideCount(const Mask: AnsiString): LongInt;
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

procedure Truncate(const Path: AnsiString; Size: Int64);
var
  F: TFileStream;
begin
  F := TFileStream.Create(Path, fmOpenReadWrite);
  try
    F.Size := Size;
  finally
    F.Free;
  end;
end;

function Conn: TMyConnection;
begin
  Result := TMyConnection.Create;
  Result.Host := ParamStr(3);
  Result.Port := StrToIntDef(ParamStr(4), 3306);
  Result.Database := ParamStr(5);
  Result.User := ParamStr(6);
  Result.Password := ParamStr(7);
end;

function Scalar(const SQL: AnsiString): AnsiString;
var
  R: TMyResult;
begin
  R := C.Query(SQL);
  try
    Result := R.Str(0, 0);
  finally
    R.Free;
  end;
end;

{ ---- the key the password is encrypted with (no TLS) ---- }

const
  { a key of nobody: given instead of the server's own, the server cannot read the password }
  OTHER_KEY = '-----BEGIN PUBLIC KEY-----'#10 +
    'MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAsWrNWnVkVQorr42yRsuJ'#10 +
    'c1pLez3+Nspt4Qjat5L5W68o2zZNjiEZ4+5GigbfTZgJB8wSUe/7Oc9htHMnwKjT'#10 +
    'lrD0kwRvRkYxi2tnusZW7UxP2vfjMBRZo1YEgBby323OXQZBO6/2k7qcpta9v4/N'#10 +
    'JEHT9lFqfQx/xCe/LEbZRNuk1WgDB9qp4pL5cPHRH3I16PgbNXuIbDE3ruM8gPgE'#10 +
    'EOb1u80aoOdkeMIgpDodEJ3nqvfrf/r4pt0F1Ap/lvz3DtX2gSLF/zCWFmu1Dd1p'#10 +
    'Yu5rOlDUtLnm3g1zvp1EtlOPvdrATmjGNyHai8AI1MEPnBy27caWy9zhsQphJ673'#10 +
    'BwIDAQAB'#10 +
    '-----END PUBLIC KEY-----'#10;

{ a PEM "RSA PUBLIC KEY" of these two numbers (big-endian) }
function RsaPem(const N, E: AnsiString): AnsiString;

  function DerLen(L: LongInt): AnsiString;
  begin
    if L < $80 then
      Result := AnsiChar(L)
    else if L < $100 then
      Result := #$81 + AnsiChar(L)
    else
      Result := #$82 + AnsiChar(L shr 8) + AnsiChar(L and $FF);
  end;

  function DerInt(const V: AnsiString): AnsiString;
  begin
    Result := #$02 + DerLen(Length(V) + 1) + #0 + V;
  end;

var
  Body: AnsiString;
begin
  Body := DerInt(N) + DerInt(E);
  Result := '-----BEGIN RSA PUBLIC KEY-----'#10 + EncodeStringBase64(#$30 + DerLen(Length(Body)) + Body) +
    #10'-----END RSA PUBLIC KEY-----'#10;
end;

{ '' when the password could be encrypted with the key, else what went wrong }
function RsaError(const Pem: AnsiString; Aborted: PBoolean): AnsiString;
begin
  Result := '';
  try
    MyRsaEncrypt(Pem, 'password', Aborted);
  except
    on E: Exception do Result := E.Message;
  end;
end;

{ a login of the user of the test after FLUSH PRIVILEGES, which empties the cache of
  caching_sha2_password: the login takes the full path, with the password encrypted by RSA.
  '' when it worked, else what went wrong }
function FullLogin(const Key: AnsiString): AnsiString;
var
  T: TMyConnection;
begin
  C.Exec('FLUSH PRIVILEGES');
  T := Conn;
  try
    T.ServerPublicKey := Key;
    try
      T.Connect;
      Result := '';
    except
      on E: Exception do Result := E.Message;
    end;
  finally
    T.Free;
  end;
end;

procedure KeyChecks;
var
  Pem, Plugin, Path: AnsiString;
  R: TMyResult;
  Flag: Boolean;
begin
  Check('a key of the server of more than 8192 bits is refused',
    Pos('too big', RsaError(RsaPem(#$C1 + StringOfChar(#$5B, 1024), #1#0#1), nil)) > 0);
  Check('an exponent of more than 4 bytes is refused',
    Pos('too big', RsaError(RsaPem(#$C1 + StringOfChar(#$5B, 255), #1#0#0#0#1), nil)) > 0);
  Check('a key of 8192 bits is still taken', RsaError(RsaPem(#$C1 + StringOfChar(#$5B, 1023), #1#0#1), nil) = '');
  Flag := True;
  Check('Abort ends the computing with the key', Pos('cut', RsaError(OTHER_KEY, @Flag)) > 0);
  Check('the old call without Abort still encrypts', Length(MyRsaEncrypt(OTHER_KEY, 'password')) = 256);

  Plugin := '';
  try
    Plugin := Scalar('SELECT plugin FROM mysql.user WHERE CONCAT(user, ''@'', host) = CURRENT_USER()');
  except
  end;
  Pem := '';
  try
    R := C.Query('SHOW STATUS LIKE ''Caching_sha2_password_rsa_public_key''');
    try
      if R.RowCount > 0 then Pem := R.Str(0, 1);
    finally R.Free; end;
  except
  end;
  if (Plugin <> 'caching_sha2_password') or (Pos('-----BEGIN', Pem) = 0) then
  begin
    WriteLn('       (the user logs in with ', Plugin, ': no RSA on this server, the key checks are skipped)');
    Exit;
  end;
  Path := ExpandFileName('afmysql-server-key.pem');
  with TStringList.Create do
  try
    Text := Pem;
    SaveToFile(Path);
  finally Free; end;
  Check('the full path without a key given: the server is asked for its key, as before', FullLogin('') = '');
  Check('with the key of the server given as text the full path logs in', FullLogin(Pem) = '');
  Check('with the path of its file too', FullLogin(Path) = '');
  Check('with another key given the login fails: the given key is used, never one the server sends',
    FullLogin(OTHER_KEY) <> '');
  Check('a key file that is not there says so', Pos('cannot be read from', FullLogin(Path + '.missing')) > 0);
  DeleteFile(Path);
end;

function WaitMirror: Boolean;
var
  S: AnsiString;
begin
  T0 := GetTickCount64;
  repeat
    Sleep(200);
    S := StrPas(MysqlStatus());
  until ((Pos('copies', S) > 0) and (Pos('writing one now', S) = 0)) or (Pos('last problem', S) > 0) or
        (GetTickCount64 - T0 > 30000);
  WriteLn('       ', S);
  Result := (Pos('copies', S) > 0) and (Pos('last problem', S) = 0);
end;

procedure DropTables;
begin
  C.Exec('DROP TABLE IF EXISTS af_profiles');
  C.Exec('DROP TABLE IF EXISTS af_other');
  C.Exec('DROP TABLE IF EXISTS af_visits');
  C.Exec('DROP TABLE IF EXISTS af_steam');
  C.Exec('DROP TABLE IF EXISTS af_links');
  C.Exec('DROP TABLE IF EXISTS af_geo');
  C.Exec('DROP TABLE IF EXISTS af_meta');
end;

begin
  if ParamCount < 6 then
  begin
    WriteLn('Usage: afmysql <antifake_dll.dll> <database file> <host> <port> <db> <user> [password]');
    Halt(2);
  end;
  Lib := LoadLibrary(ParamStr(1));
  if Lib = NilHandle then begin WriteLn('no library'); Halt(1); end;
  StartLoad := TPathProc(GetProcedureAddress(Lib, 'Start_Load_Binary'));
  StartSave := TPathProc(GetProcedureAddress(Lib, 'Start_Save_Binary'));
  LoadDone := TFlag(GetProcedureAddress(Lib, 'Is_Load_Complete'));
  LoadFailed := TFlag(GetProcedureAddress(Lib, 'Is_Load_Error'));
  SaveBusy := TFlag(GetProcedureAddress(Lib, 'Is_Save_In_Progress'));
  LoadBusy := TFlag(GetProcedureAddress(Lib, 'Is_Load_In_Progress'));
  SaveAfterLoad := TFlag(GetProcedureAddress(Lib, 'Save_After_Load'));
  ProfileCount := TCount(GetProcedureAddress(Lib, 'Get_Profile_Count'));
  ProfileName := TNameOf(GetProcedureAddress(Lib, 'Get_Profile_Active_Name'));
  ProfileSteam := TNameOf(GetProcedureAddress(Lib, 'Get_Profile_Active_Steam'));
  SteamInfo := TNameOf(GetProcedureAddress(Lib, 'Get_Steam_Info'));
  VisitCount := TVisitCount(GetProcedureAddress(Lib, 'Get_Visit_Count'));
  MysqlStatus := TReport(GetProcedureAddress(Lib, 'Get_Mysql_Status'));
  LoadNote := TReport(GetProcedureAddress(Lib, 'Get_Load_Note'));
  NoticeLine := TReport(GetProcedureAddress(Lib, 'Notice_Line'));
  LoadIni := TLoadIni(GetProcedureAddress(Lib, 'Load_Settings'));
  MysqlTick := TTick(GetProcedureAddress(Lib, 'Mysql_Tick'));
  SetVerdict := TSetVerdict(GetProcedureAddress(Lib, 'Set_Multi_Verdict'));
  RunCmd := TRunCmd(GetProcedureAddress(Lib, 'Run_Command'));
  SlotBusy := TSlotFlag(GetProcedureAddress(Lib, 'Command_Busy'));
  TaskDone := TSlotFlag(GetProcedureAddress(Lib, 'Is_Task_Complete'));
  OutCount := TSlotCount(GetProcedureAddress(Lib, 'Out_Count'));
  OutLine := TSlotLine(GetProcedureAddress(Lib, 'Out_Line'));
  RenderRes := TSelect(GetProcedureAddress(Lib, 'Render_Result'));

  DbPath := ExpandFileName('afmysql-work.dat');
  IniPath := ExpandFileName('afmysql-work.ini');
  if FileExists(DbPath) then DeleteFile(DbPath);
  if FileExists(DbPath + '.bak') then DeleteFile(DbPath + '.bak');
  if FileExists(ChangeFileExt(DbPath, '.jrn')) then DeleteFile(ChangeFileExt(DbPath, '.jrn'));
  if FileExists(ChangeFileExt(DbPath, '.jrn') + '.aside') then DeleteFile(ChangeFileExt(DbPath, '.jrn') + '.aside');
  if FileExists(DbPath + '.aside') then DeleteFile(DbPath + '.aside');
  CopyFileTo(ParamStr(2), DbPath);

  C := Conn;
  C.Connect;
  DropTables;
  { a table of another layout: no computer and no address yet, and an account that is gone }
  C.Exec('CREATE TABLE af_profiles (server_id INT NOT NULL, profile_id INT NOT NULL, name VARCHAR(64) NOT NULL, ' +
    'updated_at DATETIME NOT NULL, PRIMARY KEY (server_id, profile_id))');
  C.Exec('INSERT INTO af_profiles VALUES (4242, 99999, ''Gone'', ''2020-01-01 00:00:00'')');

  { ---- 1. the copy ---- }
  WriteIni('mirror');
  StartLoad(PChar(DbPath));
  WaitLoad;
  n := ProfileCount();
  Check('the file is read', (not LoadFailed()) and (n > 0));
  SetVerdict(0, 1, 2, PChar('tester'));
  SetLength(Names1, n);
  SetLength(Steams1, n);
  SetLength(Steam1, n);
  SetLength(Visits1, n);
  Visits := 0;
  WithSteam := 0;
  for i := 0 to n - 1 do
  begin
    Names1[i] := StrPas(ProfileName(i));
    Steams1[i] := StrPas(ProfileSteam(i));
    Steam1[i] := StrPas(SteamInfo(i));
    Visits1[i] := VisitCount(i);
    Inc(Visits, Visits1[i]);
    if Steam1[i] <> '' then Inc(WithSteam);
  end;
  WriteLn(Format('       %d accounts, %d visits, %d with Steam data', [n, Visits, WithSteam]));
  MysqlTick(True);
  Check('the copy is written', WaitMirror);
  Check('every account is in af_profiles', StrToIntDef(Scalar('SELECT COUNT(*) FROM af_profiles WHERE server_id = 4242'), -1) = n);
  Check('the account that is gone was removed', Scalar('SELECT COUNT(*) FROM af_profiles WHERE server_id = 4242 AND profile_id = 99999') = '0');
  Check('the columns the old table lacked were added',
    Scalar('SELECT COUNT(*) FROM information_schema.columns WHERE table_schema = DATABASE() AND table_name = ''af_profiles'' ' +
      'AND column_name IN (''computer'', ''address'', ''visits'')') = '3');
  Check('every visit is in af_visits', StrToIntDef(Scalar('SELECT COUNT(*) FROM af_visits WHERE server_id = 4242'), -1) = Visits);
  Check('names, computers and addresses in af_other', StrToIntDef(Scalar('SELECT COUNT(*) FROM af_other WHERE server_id = 4242'), 0) >= n);
  Check('what Steam says in af_steam', StrToIntDef(Scalar('SELECT COUNT(*) FROM af_steam WHERE server_id = 4242'), -1) = WithSteam);
  Check('the decision of an admin in af_links', Scalar('SELECT verdict FROM af_links WHERE server_id = 4242 AND a = 0 AND b = 1') = '2');
  Check('the places in af_geo', StrToIntDef(Scalar('SELECT COUNT(*) FROM af_geo WHERE server_id = 4242'), -1) >= 0);
  Check('when the copy was finished, in af_meta', Scalar('SELECT accounts FROM af_meta WHERE server_id = 4242') = IntToStr(n));
  Check('the text is UTF-8', Scalar('SELECT COUNT(*) FROM af_profiles WHERE server_id = 4242 AND name <> CONVERT(name USING utf8mb4)') = '0');
  Check('the Steam names are UTF-8', Scalar('SELECT COUNT(*) FROM af_steam WHERE server_id = 4242 AND persona LIKE ''%?%''') = '0');
  { the moment of the copy in UTC as well: the comparison with the file must not move with the summer time }
  Check('when the copy was finished, in UTC too', Abs(StrToIntDef(Scalar('SELECT TIMESTAMPDIFF(SECOND, written_utc, UTC_TIMESTAMP()) ' +
    'FROM af_meta WHERE server_id = 4242'), 99999)) < 600);

  { ---- 6. another server of the same database looks into this copy ---- }
  WriteIniShared;
  Said := AskList(5, 'af_search', Copy(Names1[0], 1, 3));
  Check('SharedSearch: the accounts of the other server are listed under the results',
    (Pos('On the other servers of the database', Said) > 0) and (Pos('S4242 #', Said) > 0));
  if Pos('S4242 #', Said) = 0 then WriteLn(Said);
  Said := AskList(6, 'statusa', '#0');
  Check('SharedLinks: /statusa lists the account of the other server with the same computer or address',
    Pos('S4242 #0 ', Said) > 0);
  if Pos('S4242 #0 ', Said) = 0 then WriteLn(Said);
  { a player sees the address, the Steam account or the computer of the other server only in
    part - the value after the kinds was shown whole to anybody who could ask }
  Aside := LineWith(Said, 'S4242 #0 ');
  if Aside <> '' then
  begin
    Said := AskList(7, 'statusa', '#0', False);
    Check('SharedLinks: a player sees what ties the account there only in part',
      (LineWith(Said, 'S4242 #0 ') <> '') and (LineWith(Said, 'S4242 #0 ') <> Aside) and
      ((Pos('***', LineWith(Said, 'S4242 #0 ')) > 0) or (Pos('*.*', LineWith(Said, 'S4242 #0 ')) > 0)));
  end;
  WriteIni('mirror');

  { ---- 2. Engine = mirror, the file is gone: the copy is read ---- }
  Aside := DbPath + '.aside';
  if FileExists(Aside) then DeleteFile(Aside);
  RenameFile(DbPath, Aside);
  if FileExists(ChangeFileExt(DbPath, '.jrn')) then RenameFile(ChangeFileExt(DbPath, '.jrn'), ChangeFileExt(DbPath, '.jrn') + '.aside');
  WriteIni('mirror');
  StartLoad(PChar(DbPath));
  WaitLoad;
  WriteLn('       ', StrPas(LoadNote()));
  Check('without the file the copy in MySQL is read', (not LoadFailed()) and (Pos('read from MySQL', StrPas(LoadNote())) = 1));
  Same := ProfileCount() = n;
  Kept := 0;
  if Same then
    for i := 0 to n - 1 do
    begin
      { A name of a player has at most 24 characters in Soldat, one byte each in the code page of
        the console, so it never comes near the 64 characters of the columns. The test file has
        a longer one on purpose (the storage cuts it): it is compared as far as a real name goes,
        and not counted as lost when the copy cuts it too. }
      if (Length(Names1[i]) > SOLDAT_NAME_MAX) and (Copy(StrPas(ProfileName(i)), 1, SOLDAT_NAME_MAX) =
        Copy(Names1[i], 1, SOLDAT_NAME_MAX)) and (StrPas(ProfileName(i)) <> Names1[i]) then
      begin
        WriteLn('       account ', i, ': a name longer than any Soldat name (', Length(Names1[i]), ' characters) came back as ',
          Length(StrPas(ProfileName(i))), ' - not counted as lost');
        Names1[i] := StrPas(ProfileName(i));
      end;
      if (StrPas(ProfileName(i)) <> Names1[i]) or (StrPas(ProfileSteam(i)) <> Steams1[i]) or (VisitCount(i) <> Visits1[i]) or
         (StrPas(SteamInfo(i)) <> Steam1[i]) then
      begin
        WriteLn('       account ', i, ': "', Names1[i], '" -> "', StrPas(ProfileName(i)), '", visits ', Visits1[i], ' -> ',
          VisitCount(i));
        Same := False;
      end
      else
        Inc(Kept);
    end;
  Check(Format('every account, name, Steam id, visit and Steam answer came back (%d of %d)', [Kept, n]), Same);
  { the script writes the file from the copy as soon as it sees the load: the library asks once }
  Check('the library asks for the file to be written from the copy', SaveAfterLoad() and not SaveAfterLoad());
  StartSave(PChar(DbPath));
  WaitSave;
  Check('the file is written again from the copy', FileExists(DbPath));

  { ---- 3. Engine = mirror, the file is newer: the file is read ---- }
  Sleep(1100);
  StartSave(PChar(DbPath));
  WaitSave;
  StartLoad(PChar(DbPath));
  WaitLoad;
  WriteLn('       ', StrPas(LoadNote()));
  Check('a file newer than the copy is read', (not LoadFailed()) and (Pos('the file is newer', StrPas(LoadNote())) = 1) and
    (ProfileCount() = n));
  Check('a file that was read is not written again at once', not SaveAfterLoad());

  { ---- 4. Engine = mirror, the file cannot be read: the copy saves the day, the file is put aside ---- }
  MysqlTick(True);
  Check('another copy is written', WaitMirror);
  Aside := DbPath + '.good';
  if FileExists(Aside) then DeleteFile(Aside);
  CopyFileTo(DbPath, Aside);
  Truncate(DbPath, 100);
  StartLoad(PChar(DbPath));
  WaitLoad;
  WriteLn('       ', StrPas(LoadNote()));
  Check('an unreadable file: the copy in MySQL is read', (not LoadFailed()) and (ProfileCount() = n) and
    (Pos('read from MySQL', StrPas(LoadNote())) = 1) and (Pos('put aside as', StrPas(LoadNote())) > 0));
  Check('the unreadable file is kept under a name of its own', not FileExists(DbPath) and (AsideCount(DbPath + '.unreadable-*') - AsideCount(DbPath + '.unreadable-*.jrn') = 1));
  Check('and the file is written anew from the copy', SaveAfterLoad());
  StartSave(PChar(DbPath));
  WaitSave;
  Check('the new file is there', FileExists(DbPath));
  DeleteMatching(DbPath + '.unreadable-*');
  DeleteFile(DbPath + '.good');

  { ---- 5. nothing is written while the reading runs ---- }
  WriteIniPort('mirror', '1');
  StartLoad(PChar(DbPath));
  if LoadBusy() then
  begin
    StartSave(PChar(DbPath + '.early'));
    Sleep(300);
    Check('a write while the database is still being read is refused', not FileExists(DbPath + '.early'));
  end
  else
    WriteLn('       (the reading was over too soon to try a write during it)');
  WaitLoad;
  WriteLn('       ', StrPas(LoadNote()));
  Check('a database that cannot be reached: the file is read', (not LoadFailed()) and (ProfileCount() = n) and
    (Pos('MySQL could not be read', StrPas(LoadNote())) = 1));
  if FileExists(DbPath + '.early') then DeleteFile(DbPath + '.early');
  { the note of a load says only what this load did, and names the engine that is set }
  WriteIniNoHost;
  StartLoad(PChar(DbPath));
  WaitLoad;
  WriteLn('       ', StrPas(LoadNote()));
  Check('Engine = mirror without MysqlHost: the file is read, and the note says so',
    (not LoadFailed()) and (ProfileCount() = n) and
    (Pos('Engine = mirror, but no MysqlHost', StrPas(LoadNote())) = 1));

  KeyChecks;

  DropTables;
  C.Free;
  WriteIni('bin');
  for i := 0 to 5 do
  begin
    case i of
      0: Aside := DbPath;
      1: Aside := DbPath + '.aside';
      2: Aside := DbPath + '.bak';
      3: Aside := ChangeFileExt(DbPath, '.jrn');
      4: Aside := ChangeFileExt(DbPath, '.jrn') + '.aside';
    else Aside := IniPath;
    end;
    if FileExists(Aside) then DeleteFile(Aside);
  end;
  if Failures = 0 then
    WriteLn('ALL TESTS PASSED')
  else
    WriteLn(Failures, ' TEST(S) FAILED');
  Halt(Failures);
end.
