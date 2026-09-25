program afconv;

{ Reads a database written by an older AntiFake, writes it again and reads it back, so the
  answer to "does it convert the old file?" is a measurement and not a promise - every field of
  every account, not only the names. After that, what the file and its journal have to keep:
  the decisions of the admins after a kill, the .bak of every write, a date that is no date,
  a range of days that does not exist, and what a player without admin is shown of the Steam
  accounts and the computers of somebody else.
  afconv.exe <antifake_dll.dll> <old antifake.dat> (the file named here is only read)

  Built from the library folder (source_dll), on Windows with FPC 3.2 for i386:
    mkdir build\test
    fpc -Mobjfpc -Sh -O2 -Fu. -FUbuild\test -FEbuild\test tests\afconv.pas
  and run in build\test next to a copy of build\win32\antifake_dll.dll. }

{$mode objfpc}{$H+}

uses
  SysUtils, Classes, dynlibs;

type
  TPathProc = procedure(Path: PChar); cdecl;
  TFlag = function: Boolean; cdecl;
  TCount = function: LongInt; cdecl;
  TNameOf = function(Index: LongInt): PChar; cdecl;
  TVisitCount = function(Index: LongInt): LongInt; cdecl;
  TStartTask = procedure(PlayerID, TaskType, Kind, SortMethod, TargetIndex, MaxResults: LongInt;
    SearchPhrase: PChar; SearchAll, ComparePartial, FilterFirst, FilterLast, FilterAll: Boolean); cdecl;
  TSlotFlag = function(Slot: LongInt): Boolean; cdecl;
  TSelect = procedure(Slot: LongInt); cdecl;
  TItemText = function(ItemIdx: LongInt): PChar; cdecl;
  TLoadIni = function(Path: PChar): Boolean; cdecl;
  TVoidProc = procedure; cdecl;
  TReport = function: PChar; cdecl;
  TSetVerdict = function(A, B, Verdict: LongInt; By: PChar): Boolean; cdecl;
  TGetVerdict = function(A, B: LongInt): PChar; cdecl;
  TRunCmd = function(Slot: LongInt; Cmd, Params: PChar; IsAdmin: Boolean; Who: PChar): LongInt; cdecl;
  TSlotCount = function(Slot: LongInt): LongInt; cdecl;
  TSlotLine = function(Slot, Index: LongInt): PChar; cdecl;
  TSetOnline = procedure(Slot, ProfileIdx: LongInt; Name, IP, Hwid: PChar; IsAdmin: Boolean); cdecl;

const
  TASK_PRIVACYID = 4;
  TASK_VISITS = 12;
  SORT_ID_UP = 0;
  SORT_LATEST_DOWN = 7;
  { where the fields of the first account lie in a file of this format (packed records): the
    header of the file is 12 bytes, a profile 200, an entry of its lists 109 }
  FILE_HEAD = 12;
  PROFILE_SIZE = 200;
  AT_REGISTER_TIME = 160;
  AT_NAMES_COUNT = 192;
  AT_LAST_USED_DATE = 101;

var
  Lib: TLibHandle;
  StartLoad, StartSave: TPathProc;
  LoadDone, LoadFailed, SaveDone: TFlag;
  ProfileCount: TCount;
  ProfileName, ProfileSteam, ProfileHwid, RegisterTime, LastPlayed, TimePlayed, TimeSpec, SteamInfo: TNameOf;
  VisitCount: TVisitCount;
  StartTask: TStartTask;
  TaskDone, SlotBusy, StartedTask: TSlotFlag;
  SelectResult, RenderRes: TSelect;
  ResultCount: TCount;
  ItemString, ItemLogins, ItemTime, ItemFirst, ItemLast: TItemText;
  LoadIni: TLoadIni;
  JournalFlush, Cleanup: TVoidProc;
  JournalStatus: TReport;
  SetVerdict: TSetVerdict;
  GetVerdict: TGetVerdict;
  RunCmd: TRunCmd;
  OutCount: TSlotCount;
  OutLine: TSlotLine;
  SetOnline: TSetOnline;
  Old, New1, IniPath, NanPath, Hwid1, Said, Before1: AnsiString;
  i, n, WasCount, Failures, Differ: LongInt;
  Snap1, Snap2: TStringList;
  L: TStringList;
  Ok: Boolean;

procedure Check(const Name: string; Good: Boolean);
begin
  if Good then
    WriteLn('[ OK ] ', Name)
  else
  begin
    WriteLn('[FAIL] ', Name);
    Inc(Failures);
  end;
end;

procedure WaitLoad;
var k: LongInt;
begin
  k := 0;
  while (k < 600) and not LoadDone() do begin Sleep(50); Inc(k); end;
end;

procedure WaitSave;
var k: LongInt;
begin
  k := 0;
  while (k < 600) and not SaveDone() do begin Sleep(50); Inc(k); end;
end;

function WaitTask(Slot, Millis: LongInt): Boolean;
var k: LongInt;
begin
  k := 0;
  while (k < Millis div 10) and not TaskDone(Slot) do begin Sleep(10); Inc(k); end;
  Result := TaskDone(Slot);
end;

function FileText(const Path: AnsiString): AnsiString;
var
  F: TFileStream;
begin
  Result := '';
  if not FileExists(Path) then Exit;
  F := TFileStream.Create(Path, fmOpenRead or fmShareDenyNone);
  try
    SetLength(Result, F.Size);
    if F.Size > 0 then F.ReadBuffer(Result[1], F.Size);
  finally F.Free; end;
end;

procedure PatchNaN(const Path: AnsiString; At: Int64);
const
  NAN_BITS: QWord = QWord($7FF8000000000000);
var
  F: TFileStream;
begin
  F := TFileStream.Create(Path, fmOpenReadWrite);
  try
    F.Position := At;
    F.WriteBuffer(NAN_BITS, SizeOf(NAN_BITS));
  finally F.Free; end;
end;

function WordAt(const Path: AnsiString; At: Int64): LongInt;
var
  F: TFileStream;
  W: Word;
begin
  W := 0;
  F := TFileStream.Create(Path, fmOpenRead or fmShareDenyNone);
  try
    F.Position := At;
    F.ReadBuffer(W, SizeOf(W));
  finally F.Free; end;
  Result := W;
end;

{ every field of every account as text: the head of the profile, what Steam said, and every
  entry of the four lists with its logins, its time and its dates }
procedure Snapshot(Into: TStringList);
var
  k, Kind, j: LongInt;
  S: AnsiString;
begin
  Into.Clear;
  for k := 0 to ProfileCount() - 1 do
  begin
    S := StrPas(ProfileName(k)) + '|' + StrPas(ProfileSteam(k)) + '|' + StrPas(ProfileHwid(k)) + '|' +
      StrPas(RegisterTime(k)) + '|' + StrPas(LastPlayed(k)) + '|' + StrPas(TimePlayed(k)) + '|' +
      StrPas(TimeSpec(k)) + '|' + IntToStr(VisitCount(k)) + '|' + StrPas(SteamInfo(k));
    for Kind := 0 to 3 do
    begin
      StartTask(1, TASK_PRIVACYID, Kind, SORT_ID_UP, k, 0, PChar(''), False, False, False, False, False);
      if not WaitTask(1, 5000) then S := S + ' [no answer]';
      SelectResult(1);
      S := S + ' {' + IntToStr(Kind) + ':';
      for j := 0 to ResultCount() - 1 do
        S := S + ' ' + StrPas(ItemString(j)) + '/' + StrPas(ItemLogins(j)) + '/' + StrPas(ItemTime(j)) + '/' +
          StrPas(ItemFirst(j)) + '/' + StrPas(ItemLast(j));
      S := S + '}';
    end;
    Into.Add(S);
  end;
end;

{ a command as the script hands it over, and every line of the answer - with the table of a
  search once it is done }
function Ask(Slot: LongInt; const C, P: AnsiString; Admin: Boolean): AnsiString;
var T, k: LongInt;
begin
  Result := '';
  RunCmd(Slot, PChar(C), PChar(P), Admin, PChar('afconv'));
  T := 0;
  while (T < 500) and SlotBusy(Slot) do begin Sleep(10); Inc(T); end;
  if StartedTask(Slot) then
  begin
    WaitTask(Slot, 5000);
    RenderRes(Slot);
    T := 0;
    while (T < 500) and SlotBusy(Slot) do begin Sleep(10); Inc(T); end;
  end;
  for k := 0 to OutCount(Slot) - 1 do Result := Result + StrPas(OutLine(Slot, k)) + #10;
end;

begin
  Failures := 0;
  Lib := LoadLibrary(ParamStr(1));
  if Lib = NilHandle then begin WriteLn('no library'); Halt(1); end;
  StartLoad := TPathProc(GetProcedureAddress(Lib, 'Start_Load_Binary'));
  StartSave := TPathProc(GetProcedureAddress(Lib, 'Start_Save_Binary'));
  LoadDone := TFlag(GetProcedureAddress(Lib, 'Is_Load_Complete'));
  LoadFailed := TFlag(GetProcedureAddress(Lib, 'Is_Load_Error'));
  SaveDone := TFlag(GetProcedureAddress(Lib, 'Is_Save_Complete'));
  ProfileCount := TCount(GetProcedureAddress(Lib, 'Get_Profile_Count'));
  ProfileName := TNameOf(GetProcedureAddress(Lib, 'Get_Profile_Active_Name'));
  ProfileSteam := TNameOf(GetProcedureAddress(Lib, 'Get_Profile_Active_Steam'));
  ProfileHwid := TNameOf(GetProcedureAddress(Lib, 'Get_Profile_Active_HWID'));
  RegisterTime := TNameOf(GetProcedureAddress(Lib, 'Get_Profile_Register_Time'));
  LastPlayed := TNameOf(GetProcedureAddress(Lib, 'Get_Profile_Last_Played'));
  TimePlayed := TNameOf(GetProcedureAddress(Lib, 'Get_Profile_Time_Played'));
  TimeSpec := TNameOf(GetProcedureAddress(Lib, 'Get_Profile_Time_Spectator'));
  SteamInfo := TNameOf(GetProcedureAddress(Lib, 'Get_Steam_Info'));
  VisitCount := TVisitCount(GetProcedureAddress(Lib, 'Get_Visit_Count'));
  StartTask := TStartTask(GetProcedureAddress(Lib, 'Start_Search_Task'));
  TaskDone := TSlotFlag(GetProcedureAddress(Lib, 'Is_Task_Complete'));
  SelectResult := TSelect(GetProcedureAddress(Lib, 'Select_Task_Result'));
  ResultCount := TCount(GetProcedureAddress(Lib, 'Get_Result_Count'));
  ItemString := TItemText(GetProcedureAddress(Lib, 'Get_Result_Item_String'));
  ItemLogins := TItemText(GetProcedureAddress(Lib, 'Get_Result_Item_LoginCount'));
  ItemTime := TItemText(GetProcedureAddress(Lib, 'Get_Result_Item_TimePlayed'));
  ItemFirst := TItemText(GetProcedureAddress(Lib, 'Get_Result_Item_FirstUsedDate'));
  ItemLast := TItemText(GetProcedureAddress(Lib, 'Get_Result_Item_LastUsedDate'));
  LoadIni := TLoadIni(GetProcedureAddress(Lib, 'Load_Settings'));
  JournalFlush := TVoidProc(GetProcedureAddress(Lib, 'Journal_Flush'));
  JournalStatus := TReport(GetProcedureAddress(Lib, 'Get_Journal_Status'));
  Cleanup := TVoidProc(GetProcedureAddress(Lib, 'Cleanup'));
  SetVerdict := TSetVerdict(GetProcedureAddress(Lib, 'Set_Multi_Verdict'));
  GetVerdict := TGetVerdict(GetProcedureAddress(Lib, 'Get_Multi_Verdict'));
  RunCmd := TRunCmd(GetProcedureAddress(Lib, 'Run_Command'));
  SlotBusy := TSlotFlag(GetProcedureAddress(Lib, 'Command_Busy'));
  StartedTask := TSlotFlag(GetProcedureAddress(Lib, 'Command_Started_Task'));
  OutCount := TSlotCount(GetProcedureAddress(Lib, 'Out_Count'));
  OutLine := TSlotLine(GetProcedureAddress(Lib, 'Out_Line'));
  SetOnline := TSetOnline(GetProcedureAddress(Lib, 'Set_Online'));
  RenderRes := TSelect(GetProcedureAddress(Lib, 'Render_Result'));

  Old := ParamStr(2);
  New1 := ChangeFileExt(Old, '') + '-converted.dat';
  Snap1 := TStringList.Create;
  Snap2 := TStringList.Create;

  { ---- the old file, written again and read back: every field of every account ---- }
  StartLoad(PChar(Old));
  WaitLoad;
  if LoadFailed() then begin WriteLn('the old file could not be read'); Halt(1); end;
  WasCount := ProfileCount();
  Snapshot(Snap1);
  n := 0;
  for i := 0 to WasCount - 1 do Inc(n, VisitCount(i));
  WriteLn('old file : ', WasCount, ' accounts, ', n, ' visits');

  StartSave(PChar(New1));
  WaitSave;
  WriteLn('written  : ', New1);

  StartLoad(PChar(New1));
  WaitLoad;
  if LoadFailed() then begin WriteLn('the new file could not be read'); Halt(1); end;
  Snapshot(Snap2);
  n := 0;
  for i := 0 to ProfileCount() - 1 do Inc(n, VisitCount(i));
  WriteLn('new file : ', ProfileCount(), ' accounts, ', n, ' visits');

  Ok := Snap2.Count = Snap1.Count;
  Differ := 0;
  if Ok then
    for i := 0 to Snap1.Count - 1 do
      if Snap1[i] <> Snap2[i] then
      begin
        Inc(Differ);
        if Differ <= 3 then
          WriteLn('account ', i, ' changed:', LineEnding, '  ', Snap1[i], LineEnding, '  ', Snap2[i]);
      end;
  Ok := Ok and (Differ = 0);
  if Ok then WriteLn('SAME: every account and every field of it came through')
  else WriteLn('DIFFERENT - something did not survive');
  Check('the old file converts with every field of every account', Ok);

  { ---- what the file and its journal keep; the settings a server has ---- }
  IniPath := ExpandFileName('afconv.ini');
  L := TStringList.Create;
  try
    L.Add('[Storage]');
    L.Add('JournalSeconds = 5');
    L.Add('BackupHours = 0');
    L.Add('[PlayerCommands]');
    L.Add('PlayerNames = all');
    L.SaveToFile(IniPath);
  finally L.Free; end;
  LoadIni(PChar(IniPath));

  if WasCount >= 2 then
  begin
    { a decision of an admin after the last full write, and the server is killed: the journal
      has it, and the file (without it) used to be put over it at the next start }
    StartLoad(PChar(New1));
    WaitLoad;
    Check('an admin decides about a pair', SetVerdict(0, 1, 2, PChar('afconv')));
    JournalFlush();
    n := 0;
    while (n < 250) and (Pos(' 0 writes', StrPas(JournalStatus())) > 0) do begin Sleep(20); Inc(n); end;
    Check('the decision is in the journal', Pos(' 0 writes', StrPas(JournalStatus())) = 0);
    StartLoad(PChar(New1));
    WaitLoad;
    Check('after a kill the decision comes back from the journal',
      (not LoadFailed()) and (Pos('2|afconv|', StrPas(GetVerdict(0, 1))) = 1));
    if Pos('2|afconv|', StrPas(GetVerdict(0, 1))) <> 1 then
      WriteLn('       "', StrPas(GetVerdict(0, 1)), '" / ', StrPas(JournalStatus()));
  end;

  { every full write keeps the file before it as .bak and leaves no .tmp behind }
  StartSave(PChar(New1));
  WaitSave;
  Before1 := FileText(New1);
  if WasCount >= 2 then SetVerdict(0, 1, 1, PChar('afconv'));
  StartSave(PChar(New1));
  WaitSave;
  Check('the file before the write stays as .bak', (Before1 <> '') and (FileText(New1 + '.bak') = Before1));
  Check('the new file is in place, and no .tmp is left', FileExists(New1) and not FileExists(New1 + '.tmp'));

  { a date that is no date (a damaged file): read as "never", and every table still answers.
    Comparing one raised an exception - on the game thread, at a join, that unloads the script. }
  NanPath := ExpandFileName('afconv-nan.dat');
  Before1 := FileText(New1);
  with TFileStream.Create(NanPath, fmCreate) do
  try
    if Before1 <> '' then WriteBuffer(Before1[1], Length(Before1));
  finally Free; end;
  if WasCount >= 1 then
  begin
    PatchNaN(NanPath, FILE_HEAD + AT_REGISTER_TIME);
    n := WordAt(NanPath, FILE_HEAD + AT_NAMES_COUNT);
    if n > 0 then PatchNaN(NanPath, FILE_HEAD + PROFILE_SIZE + AT_LAST_USED_DATE);
    StartLoad(PChar(NanPath));
    WaitLoad;
    Check('a file with a date that is no date is read', not LoadFailed());
    Check('that date reads as never', StrPas(RegisterTime(0)) = '0');
    if n > 0 then
    begin
      Said := Ask(2, 'privacyall', 'n #0', True);
      Check('the table of that account is drawn', (Pos('went wrong', Said) = 0) and (Pos('never', Said) > 0));
      if (Pos('went wrong', Said) > 0) or (Pos('never', Said) = 0) then WriteLn(Said);
    end;
  end;

  { a range of days that does not exist (31 February): the search answers, it used to leave the
    slot busy for good }
  StartTask(1, TASK_VISITS, 0, SORT_LATEST_DOWN, -1, 0, PChar('2026-02-31..2026-03-01'), False, False,
    False, False, False);
  Check('a range with a day that does not exist still gets an answer', WaitTask(1, 5000));

  { what a player is shown of another account: the computers only in part (the whole reach is
    on in the settings above), the admin sees them whole }
  StartLoad(PChar(New1));
  WaitLoad;
  if WasCount >= 2 then
  begin
    Hwid1 := StrPas(ProfileHwid(1));
    if Length(Hwid1) >= 6 then
    begin
      { one text of the library at a time: its answers share one buffer }
      Said := StrPas(ProfileName(0));
      Before1 := StrPas(ProfileHwid(0));
      SetOnline(3, 0, PChar(Said), PChar('1.2.3.4'), PChar(Before1), False);
      Said := Ask(3, 'privacyall', 'h #1', False);
      Check('a player sees the computer of another account only in part',
        (Pos(Copy(Hwid1, 1, 3) + '***', Said) > 0) and (Pos(Hwid1, Said) = 0));
      if (Pos(Copy(Hwid1, 1, 3) + '***', Said) = 0) or (Pos(Hwid1, Said) > 0) then WriteLn(Said);
      Said := Ask(4, 'privacyall', 'h #1', True);
      Check('an admin sees it whole', Pos(Hwid1, Said) > 0);
    end
    else
      WriteLn('       (account 1 has no computer to hide)');
  end;

  Cleanup();
  UnloadLibrary(Lib);
  for i := 0 to 3 do
  begin
    case i of
      0: Said := NanPath;
      1: Said := ChangeFileExt(NanPath, '.jrn');
      2: Said := IniPath;
    else Said := ChangeFileExt(New1, '.jrn');
    end;
    if FileExists(Said) then DeleteFile(Said);
  end;
  Snap1.Free;
  Snap2.Free;
  if Failures = 0 then
    WriteLn('ALL CHECKS PASSED')
  else
    WriteLn(Failures, ' CHECK(S) FAILED');
  if Failures > 0 then Halt(1);
end.
