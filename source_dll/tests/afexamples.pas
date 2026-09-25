program afexamples;

{ Every file of Example-configs is a complete settings.ini: the library reads each one with
  "(0 out of range, 0 unknown)", and each has the values its kind of server needs. Nothing here
  asks for the Steam key (the library never hands it out): only whether one is set.
    afexamples.exe <antifake_dll.dll> <the Example-configs folder>

  Built from the library folder (source_dll), on Windows with FPC 3.2 for i386:
    mkdir build\test
    fpc -Mobjfpc -Sh -O2 -Fu. -FUbuild\test -FEbuild\test tests\afexamples.pas
  and run in build\test next to a copy of build\win32\antifake_dll.dll. }

{$mode objfpc}{$H+}

uses
  SysUtils, Classes, dynlibs;

type
  TLoadIni = function(Path: PChar): Boolean; cdecl;
  TSettingInt = function(Name: PChar): LongInt; cdecl;
  TSettingStr = function(Name: PChar): PChar; cdecl;

const
  { [PlayerCommands] as the library answers them }
  S_OFF = 0;
  S_SELF = 1;
  S_ALL = 2;
  NAMES: array[0..3] of string = ('public-server.ini', 'private-clan-server.ini', 'privacy-strict.ini',
    'several-servers-mysql.ini');

var
  Failures: Integer = 0;
  Lib: TLibHandle;
  LoadIni: TLoadIni;
  SettingInt: TSettingInt;
  SettingStr: TSettingStr;
  Dir: string;
  SR: TSearchRec;
  n, i: Integer;

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

function Int(const Name: string): LongInt;
begin
  Result := SettingInt(PChar(Name));
end;

{ one file read by the library; True when it counted nothing out of range and nothing unknown }
function LoadClean(const Path: string): Boolean;
var
  M: string;
begin
  Result := LoadIni(PChar(Path));
  M := StrPas(SettingStr(PChar('message')));
  WriteLn('       ', ExtractFileName(Path), ': ', M);
  Result := Result and (Int('loaded') = 1) and (Pos('(0 out of range, 0 unknown)', M) > 0);
end;

{ the reach of the twelve ! groups, in the order of [PlayerCommands] }
function Reach(Visits, VisitStats, VisitsOf, Status, Names, Steam, Locate, Profiles, Search, Multi, Help,
  LastVisitors: Integer): Boolean;
begin
  Result := (Int('playervisits') = Visits) and (Int('playervisitstats') = VisitStats) and
    (Int('playervisitsof') = VisitsOf) and (Int('playerstatus') = Status) and (Int('playernames') = Names) and
    (Int('playersteam') = Steam) and (Int('playerlocate') = Locate) and (Int('playerprofiles') = Profiles) and
    (Int('playersearch') = Search) and (Int('playermulti') = Multi) and (Int('playerhelp') = Help) and
    (Int('playerlastvisitors') = LastVisitors);
end;

{ what every example keeps as settings.ini has it }
function Common: Boolean;
begin
  Result := (Int('steamenabled') = 0) and (Int('admincommands') = 1) and (Int('playercommands') = 0) and
    (Int('chatcommands') = 0) and (Int('publiccommands') = 1) and (Int('saveintervalminutes') = 15) and
    (Int('journalseconds') = 10) and (Int('backuphours') = 24) and (Int('multiaccountnotice') = 1) and
    (Int('multiaccountreportscore') = 55) and (Int('sharedsearch') = 0) and (Int('sharedlinks') = 0) and
    (Int('trace') = 0) and (StrPas(SettingStr(PChar('databasefile'))) = 'nickreg/antifake.dat');
end;

begin
  if ParamCount < 2 then
  begin
    WriteLn('afexamples.exe <antifake_dll.dll> <Example-configs folder>');
    Halt(2);
  end;
  Lib := LoadLibrary(ParamStr(1));
  if Lib = NilHandle then
  begin
    WriteLn('[FAIL] the library ', ParamStr(1), ' does not load');
    Halt(1);
  end;
  LoadIni := TLoadIni(GetProcedureAddress(Lib, 'Load_Settings'));
  SettingInt := TSettingInt(GetProcedureAddress(Lib, 'Get_Setting_Int'));
  SettingStr := TSettingStr(GetProcedureAddress(Lib, 'Get_Setting_Str'));
  Check('the library has Load_Settings, Get_Setting_Int and Get_Setting_Str',
    Assigned(LoadIni) and Assigned(SettingInt) and Assigned(SettingStr));
  if Failures > 0 then
    Halt(1);
  Dir := IncludeTrailingPathDelimiter(ExpandFileName(ParamStr(2)));

  { every file of the folder, also one this program does not know }
  n := 0;
  if FindFirst(Dir + '*.ini', faAnyFile, SR) = 0 then
  try
    repeat
      Inc(n);
      Check(SR.Name + ' reads with 0 out of range, 0 unknown', LoadClean(Dir + SR.Name));
    until FindNext(SR) <> 0;
  finally
    FindClose(SR);
  end;
  Check(Format('Example-configs holds the %d examples (%d .ini files)', [Length(NAMES), n]), n >= Length(NAMES));
  for i := 0 to High(NAMES) do
    Check(NAMES[i] + ' is there', FileExists(Dir + NAMES[i]));

  { what each one is for }
  LoadClean(Dir + 'public-server.ini');
  Check('public-server.ini: the ! commands as AntiFake comes, geo on, the file alone', Common and
    Reach(S_SELF, S_ALL, S_OFF, S_SELF, S_SELF, S_SELF, S_SELF, S_OFF, S_OFF, S_OFF, S_ALL, S_ALL) and
    (Int('geoenabled') = 1) and (Int('engine') = 0) and (Int('addressdays') = 180) and (Int('profiledays') = 0) and
    (Int('visitkeepdays') = 365) and (Int('geokeepdays') = 365));

  LoadClean(Dir + 'private-clan-server.ini');
  Check('private-clan-server.ini: players ask about anybody, searches and multi-accounts stay with the admins',
    Common and Reach(S_ALL, S_ALL, S_ALL, S_ALL, S_ALL, S_ALL, S_ALL, S_ALL, S_OFF, S_OFF, S_ALL, S_ALL) and
    (Int('geoenabled') = 1) and (Int('engine') = 0) and (Int('addressdays') = 180) and (Int('profiledays') = 0));

  LoadClean(Dir + 'privacy-strict.ini');
  Check('privacy-strict.ini: players only about themselves, no geo, shorter keeping', Common and
    Reach(S_SELF, S_OFF, S_OFF, S_SELF, S_SELF, S_SELF, S_OFF, S_OFF, S_OFF, S_OFF, S_ALL, S_OFF) and
    (Int('geoenabled') = 0) and (Int('geokeepdays') = 30) and (Int('visitkeepdays') = 90) and
    (Int('addressdays') = 90) and (Int('maxaddresses') = 20) and (Int('profiledays') = 730) and
    (Int('engine') = 0));

  LoadClean(Dir + 'several-servers-mysql.ini');
  Check('several-servers-mysql.ini: Engine = mysql, server 1, otherwise the public server', Common and
    Reach(S_SELF, S_ALL, S_OFF, S_SELF, S_SELF, S_SELF, S_SELF, S_OFF, S_OFF, S_OFF, S_ALL, S_ALL) and
    (Int('engine') = 2) and (Int('mysqlport') = 3306) and (Int('mysqlserverid') = 1) and
    (Int('syncseconds') = 3) and (Int('mysqlcodepage') = 1250) and (Int('geoenabled') = 1));

  if Failures = 0 then
    WriteLn('ALL TESTS PASSED')
  else
    WriteLn(Failures, ' TEST(S) FAILED');
  Halt(Failures);
end.
