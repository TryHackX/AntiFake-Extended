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
  SysUtils, Classes, dynlibs, crc;

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
  TLogin = function(Name, SteamID, HWID, IP: PChar; IncLoginCount: Boolean): LongInt; cdecl;
  TVisitNote = procedure(Index: LongInt; Name, Hwid, IP: PChar; AddSeconds: LongInt); cdecl;
  TRestore = function(Name: PChar): Boolean; cdecl;

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
  { the rest of the layout, for the file and the journal that are put together by hand below }
  OTHER_SIZE = 109;
  VISIT_SIZE = 92;
  JOURNAL_HEAD = 32;
  VISITS_MAGIC = $53545356;

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
  LoginPlayer: TLogin;
  VisitNote: TVisitNote;
  SteamNote: TPathProc;
  SteamTick: TVoidProc;
  SteamStatus: TReport;
  JournalFinal: TFlag;
  SaveFailed: TFlag;
  RestoreBackup: TRestore;
  Mode: LongInt;
  SR: TSearchRec;
  BackDir: AnsiString;
  Old, New1, IniPath, NanPath, Hwid1, Said, Before1, VisDb, VisJrn: AnsiString;
  V1, V2, V3: LongInt;
  Took: QWord;
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

function FileLen(const Path: AnsiString): LongInt;
var
  Info: TSearchRec;
begin
  Result := -1;
  if FindFirst(Path, faAnyFile, Info) = 0 then
  begin
    Result := Info.Size;
    FindClose(Info);
  end;
end;

procedure PutFile(const Path, Data: AnsiString);
var
  F: TFileStream;
begin
  F := TFileStream.Create(Path, fmCreate);
  try
    if Data <> '' then F.WriteBuffer(Data[1], Length(Data));
  finally F.Free; end;
end;

function IntAt(const S: AnsiString; At: LongInt): LongInt;
begin
  Result := 0;
  if (At >= 1) and (At + 3 <= Length(S)) then Move(S[At], Result, 4);
end;

function IntBytes(N: LongInt): AnsiString;
begin
  SetLength(Result, 4);
  Move(N, Result[1], 4);
end;

{ Extra visits for a list of Count visits that starts at From (1-based) in S: copies of its
  first one, each a day older than the one before - so the newest are the ones it had. }
function OlderVisits(const S: AnsiString; From, Count, Extra: LongInt): AnsiString;
var
  k: LongInt;
  V: AnsiString;
  T: Double;
begin
  Result := '';
  if Count <= 0 then Exit;
  for k := 1 to Extra do
  begin
    V := Copy(S, From, VISIT_SIZE);
    Move(V[1], T, 8);
    T := T - k;
    Move(T, V[1], 8);
    Result := Result + V;
  end;
end;

{ An account of the file gets Extra more visits (VisitKeepCount = 0 in 2.0 wrote every one):
  the visit section is found behind the profiles and put together again. False when the file
  does not look as expected. }
function GrowVisitsInFile(const Path: AnsiString; Account, Extra: LongInt): Boolean;
var
  S, Outp: AnsiString;
  Count, p, i, n, Lists: LongInt;
begin
  Result := False;
  S := FileText(Path);
  if Length(S) < 12 then Exit;
  Count := IntAt(S, 9);
  p := 13;
  for i := 0 to Count - 1 do
  begin
    Lists := PWord(@S[p + 192])^ + PWord(@S[p + 194])^ + PWord(@S[p + 196])^ + PWord(@S[p + 198])^;
    Inc(p, PROFILE_SIZE + Lists * OTHER_SIZE);
  end;
  while (p + 7 <= Length(S)) and not ((IntAt(S, p) = LongInt(VISITS_MAGIC)) and (IntAt(S, p + 4) = Count)) do
    Inc(p);
  if p + 7 > Length(S) then Exit;
  Outp := Copy(S, 1, p + 7);
  Inc(p, 8);
  for i := 0 to Count - 1 do
  begin
    n := IntAt(S, p);
    if i = Account then
      Outp := Outp + IntBytes(n + Extra) + Copy(S, p + 4, n * VISIT_SIZE) + OlderVisits(S, p + 4, n, Extra)
    else
      Outp := Outp + Copy(S, p, 4 + n * VISIT_SIZE);
    Inc(p, 4 + n * VISIT_SIZE);
  end;
  Outp := Outp + Copy(S, p, MaxInt);
  PutFile(Path, Outp);
  Result := True;
end;

{ The same in the journal: the first record (an account with its visits) gets Extra more. A
  journal of format 2 carries a checksum behind every record, which is made again. }
function GrowVisitsInJournal(const Path: AnsiString; Extra: LongInt): Boolean;
var
  S, Data, Rec: AnsiString;
  Format, Len, p, k, n: LongInt;
  Sum: Cardinal;
begin
  Result := False;
  S := FileText(Path);
  if Length(S) < JOURNAL_HEAD + 5 then Exit;
  Format := IntAt(S, 5);
  if Ord(S[JOURNAL_HEAD + 1]) <> 1 then Exit;
  Len := IntAt(S, JOURNAL_HEAD + 2);
  Data := Copy(S, JOURNAL_HEAD + 6, Len);
  if Length(Data) <> Len then Exit;
  p := 1 + 4 + PROFILE_SIZE;
  for k := 1 to 4 do
  begin
    n := IntAt(Data, p);
    Inc(p, 4 + n * OTHER_SIZE);
  end;
  n := IntAt(Data, p);
  Data := Copy(Data, 1, p - 1) + IntBytes(n + Extra) + Copy(Data, p + 4, n * VISIT_SIZE) +
    OlderVisits(Data, p + 4, n, Extra) + Copy(Data, p + 4 + n * VISIT_SIZE, MaxInt);
  Rec := #1 + IntBytes(Length(Data)) + Data;
  if Format >= 2 then
  begin
    Sum := crc32(0, nil, 0);
    Sum := crc32(Sum, @Rec[1], Length(Rec));
    Rec := Rec + IntBytes(LongInt(Sum));
    Inc(Len, 4);
  end;
  PutFile(Path, Copy(S, 1, JOURNAL_HEAD) + Rec + Copy(S, JOURNAL_HEAD + 6 + Len, MaxInt));
  Result := True;
end;

{ The journal changed by hand. 0: written again as format 1 (AntiFake 2.0, no checksums),
  1: a byte of the first record damaged, 2: the first record names account 1048576 (with a
  checksum that fits). False when it is no journal of format 2. }
function ReworkJournal(const Path: AnsiString; Mode: LongInt): Boolean;
var
  S, Outp, Rec, Data: AnsiString;
  p, Len: LongInt;
  First: Boolean;
  Sum: Cardinal;
begin
  Result := False;
  S := FileText(Path);
  if (Length(S) < JOURNAL_HEAD + 9) or (IntAt(S, 5) <> 2) then Exit;
  Outp := Copy(S, 1, JOURNAL_HEAD);
  if Mode = 0 then
  begin
    Len := 1;
    Move(Len, Outp[5], 4);
  end;
  p := JOURNAL_HEAD + 1;
  First := True;
  while p + 4 <= Length(S) do
  begin
    Len := IntAt(S, p + 1);
    Data := Copy(S, p + 5, Len);
    Rec := Copy(S, p, 5 + Len + 4);
    if First and (Mode = 1) and (Len > 0) then
      Rec[5 + Len] := AnsiChar(Ord(Rec[5 + Len]) xor $FF);
    if First and (Mode = 2) then
    begin
      Rec := S[p] + IntBytes(Len) + IntBytes(1048576) + Copy(Data, 5, MaxInt);
      Sum := crc32(0, nil, 0);
      Sum := crc32(Sum, @Rec[1], Length(Rec));
      Rec := Rec + IntBytes(LongInt(Sum));
    end;
    if Mode = 0 then Rec := Copy(Rec, 1, 5 + Len);
    Outp := Outp + Rec;
    Inc(p, 5 + Len + 4);
    First := False;
  end;
  PutFile(Path, Outp);
  Result := True;
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
  { started as the "curl" of the Steam check below: a curl that never answers }
  if ParamStr(1) = '-s' then
  begin
    Sleep(60000);
    Halt(0);
  end;
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
  LoginPlayer := TLogin(GetProcedureAddress(Lib, 'Login_Player'));
  VisitNote := TVisitNote(GetProcedureAddress(Lib, 'Visit_Note'));
  SteamNote := TPathProc(GetProcedureAddress(Lib, 'Steam_Note'));
  SteamTick := TVoidProc(GetProcedureAddress(Lib, 'Steam_Tick'));
  SteamStatus := TReport(GetProcedureAddress(Lib, 'Get_Steam_Status'));
  JournalFinal := TFlag(GetProcedureAddress(Lib, 'Journal_Final'));
  SaveFailed := TFlag(GetProcedureAddress(Lib, 'Is_Save_Error'));
  RestoreBackup := TRestore(GetProcedureAddress(Lib, 'Restore_Backup'));

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

  { VisitKeepCount = 0 ("no limit"): 2.0 wrote every visit, and an account with more than 200
    ended the reading - of the visits of every account after it, the Steam section and the
    decisions in the file, of every record after it in the journal. Now the newest 200 are
    kept and the rest is read. The account with too many sits in the middle. }
  L := TStringList.Create;
  try
    L.Add('[Storage]');
    L.Add('JournalSeconds = 5');
    L.Add('BackupHours = 0');
    L.Add('[Visits]');
    L.Add('VisitKeepCount = 0');
    L.SaveToFile(IniPath);
  finally L.Free; end;
  LoadIni(PChar(IniPath));
  VisDb := ExpandFileName('afconv-visits.dat');
  VisJrn := ChangeFileExt(VisDb, '.jrn');
  for i := 0 to 2 do
  begin
    case i of
      0: Said := VisDb;
      1: Said := VisJrn;
    else Said := VisDb + '.bak';
    end;
    if FileExists(Said) then DeleteFile(Said);
  end;
  StartLoad(PChar(VisDb));
  WaitLoad;
  for i := 0 to 3 do
  begin
    n := LoginPlayer(PChar('Visitor' + IntToStr(i)), PChar(''), PChar('HW-VIS-' + IntToStr(i)),
      PChar('10.9.' + IntToStr(i) + '.1'), True);
    VisitNote(n, PChar('Visitor' + IntToStr(i)), PChar('HW-VIS-' + IntToStr(i)),
      PChar('10.9.' + IntToStr(i) + '.1'), 60);
    if i >= 2 then
      VisitNote(n, PChar('Visitor' + IntToStr(i)), PChar('HW-VIS-' + IntToStr(i)),
        PChar('10.8.' + IntToStr(i) + '.1'), 60);
  end;
  SetVerdict(0, 2, 2, PChar('afconv'));
  V2 := VisitCount(2);
  V3 := VisitCount(3);
  StartSave(PChar(VisDb));
  WaitSave;
  Check('the file of the visit check is written', (ProfileCount() = 4) and FileExists(VisDb) and
    GrowVisitsInFile(VisDb, 1, 210));
  StartLoad(PChar(VisDb));
  WaitLoad;
  Check('an account with more than 200 visits in the file keeps the newest 200',
    (not LoadFailed()) and (VisitCount(1) = 200));
  Check('and the accounts after it keep their visits, and the decisions are read',
    (VisitCount(2) = V2) and (VisitCount(3) = V3) and (Pos('2|afconv|', StrPas(GetVerdict(0, 2))) = 1));
  if not ((VisitCount(2) = V2) and (VisitCount(3) = V3)) then
    WriteLn('       visits ', VisitCount(0), ' ', VisitCount(1), ' ', VisitCount(2), ' ', VisitCount(3),
      ' verdict "', StrPas(GetVerdict(0, 2)), '"');
  { the journal: account 1 changes first, account 3 and a decision after it }
  n := FileLen(VisJrn);
  VisitNote(1, PChar('Visitor1'), PChar('HW-VIS-1'), PChar('10.7.1.1'), 60);
  JournalFlush();
  i := 0;
  while (i < 250) and (FileLen(VisJrn) = n) do begin Sleep(20); Inc(i); end;
  n := FileLen(VisJrn);
  VisitNote(3, PChar('Visitor3'), PChar('HW-VIS-3'), PChar('10.7.3.1'), 60);
  SetVerdict(1, 3, 1, PChar('afconv'));
  JournalFlush();
  i := 0;
  while (i < 250) and (FileLen(VisJrn) = n) do begin Sleep(20); Inc(i); end;
  Sleep(200);
  Check('the journal of the visit check is written', (FileLen(VisJrn) > n) and GrowVisitsInJournal(VisJrn, 7));
  StartLoad(PChar(VisDb));
  WaitLoad;
  Check('a journal record with more than 200 visits keeps the newest 200',
    (not LoadFailed()) and (VisitCount(1) = 200));
  Check('and the records after it are replayed',
    (VisitCount(3) = V3 + 1) and (Pos('1|afconv|', StrPas(GetVerdict(1, 3))) = 1));
  if VisitCount(3) <> V3 + 1 then
    WriteLn('       visits of #3: ', VisitCount(3), ' / ', StrPas(JournalStatus()));

  { The journal has a checksum behind every record now (format 2): a journal of 2.0 (format 1)
    is still replayed, a damaged record ends the replay, and so does a record that names an
    account far beyond the ones there are (it used to make the list that long). Every round: a
    fresh journal (the load starts one), a change of account 0 in it, the journal reworked, and
    the start after a kill. }
  for Mode := 0 to 2 do
  begin
    StartSave(PChar(VisDb));
    WaitSave;
    StartLoad(PChar(VisDb));
    WaitLoad;
    V1 := VisitCount(0);
    n := FileLen(VisJrn);
    VisitNote(0, PChar('Visitor0'), PChar('HW-VIS-0'), PChar('10.6.' + IntToStr(Mode) + '.1'), 60);
    JournalFlush();
    i := 0;
    while (i < 250) and (FileLen(VisJrn) = n) do begin Sleep(20); Inc(i); end;
    Sleep(200);
    Ok := ReworkJournal(VisJrn, Mode);
    StartLoad(PChar(VisDb));
    WaitLoad;
    Said := StrPas(JournalStatus());
    case Mode of
      0: Check('a journal of format 1 (AntiFake 2.0) is still replayed',
           Ok and (not LoadFailed()) and (VisitCount(0) = V1 + 1));
      1: Check('a record with a wrong checksum ends the replay and says so',
           Ok and (not LoadFailed()) and (VisitCount(0) = V1) and (Pos('checksum', Said) > 0));
    else
      Check('a record that names account 1048576 ends the replay and says so',
        Ok and (not LoadFailed()) and (ProfileCount() = 4) and (Pos('far beyond', Said) > 0));
    end;
    if not Ok or (Mode = 0) and (VisitCount(0) <> V1 + 1) then
      WriteLn('       round ', Mode, ': visits ', V1, ' -> ', VisitCount(0), ', ', ProfileCount(), ' accounts / ', Said);
  end;

  { An append to the journal that fails (here: its file is gone) used to count as written, and
    Journal_Final still said True - the script skipped the full write and the change was lost.
    Now Journal_Final says False until a full write starts a new journal. }
  StartSave(PChar(VisDb));
  WaitSave;
  StartLoad(PChar(VisDb));
  WaitLoad;
  if FileExists(VisJrn) then DeleteFile(VisJrn);
  VisitNote(0, PChar('Visitor0'), PChar('HW-VIS-0'), PChar('10.5.0.1'), 60);
  JournalFlush();
  i := 0;
  while (i < 250) and (Pos('is gone', StrPas(JournalStatus())) = 0) do begin Sleep(20); Inc(i); end;
  Sleep(100);
  Check('an append to a journal that is gone is reported', Pos('is gone', StrPas(JournalStatus())) > 0);
  VisitNote(0, PChar('Visitor0'), PChar('HW-VIS-0'), PChar('10.5.1.1'), 60);
  Check('and the end of the script then asks for a full write', not JournalFinal());
  StartSave(PChar(VisDb));
  WaitSave;
  VisitNote(0, PChar('Visitor0'), PChar('HW-VIS-0'), PChar('10.5.2.1'), 60);
  Check('a full write starts a journal that takes everything again', FileExists(VisJrn) and JournalFinal());
  Sleep(300);

  { /storage restore: only a copy AntiFake made (the backup folder is shared with ZitroStats),
    and nothing is written over the copy afterwards }
  BackDir := ExtractFilePath(VisDb) + 'backup';
  ForceDirectories(BackDir);
  StartLoad(PChar(VisDb));
  WaitLoad;
  Before1 := FileText(VisDb);
  PutFile(BackDir + PathDelim + 'zitrostats-20260101-000000.dat', 'not a database of AntiFake');
  PutFile(BackDir + PathDelim + 'antifake-20260101-000000.dat', Before1);
  Check('a file of the backup folder that is no copy of AntiFake is not put back',
    not RestoreBackup(PChar('zitrostats-20260101-000000.dat')) and (FileText(VisDb) = Before1));
  Check('a copy of the list is put back', RestoreBackup(PChar('antifake-20260101-000000.dat')) and
    (FileText(VisDb) = Before1));
  VisitNote(0, PChar('Visitor0'), PChar('HW-VIS-0'), PChar('10.4.0.1'), 60);
  StartSave(PChar(VisDb));
  WaitSave;
  Check('and no full write goes over it afterwards', SaveFailed() and (FileText(VisDb) = Before1));
  DeleteFile(BackDir + PathDelim + 'zitrostats-20260101-000000.dat');
  DeleteFile(BackDir + PathDelim + 'antifake-20260101-000000.dat');
  RemoveDir(BackDir);

  { /recompile or the end of the server while a question to Steam hangs: Cleanup stops it at
    once. It used to wait SteamTimeoutSeconds for curl (up to 120), and a thread still running
    after Cleanup ran on in a library that was unloaded under it. The "curl" is this program. }
  L := TStringList.Create;
  try
    L.Add('[Steam]');
    L.Add('SteamApiKey = 0000000000000000000000000000TEST');
    L.Add('SteamCurlPath = ' + ParamStr(0));
    L.Add('SteamTimeoutSeconds = 30');
    L.SaveToFile(IniPath);
  finally L.Free; end;
  LoadIni(PChar(IniPath));
  SteamNote(PChar('S1885850061'));
  SteamTick();
  i := 0;
  while (i < 250) and (Pos('waiting 0', StrPas(SteamStatus())) = 0) do begin Sleep(20); Inc(i); end;
  Sleep(1000);
  Took := GetTickCount64;
  Cleanup();
  Took := GetTickCount64 - Took;
  Check('the end does not wait for a question to Steam that hangs (' + IntToStr(Took) + ' ms)',
    Took < 5000);
  UnloadLibrary(Lib);
  { what the restore check moved aside }
  L := TStringList.Create;
  try
    if FindFirst(VisDb + '.replaced-*', faAnyFile, SR) = 0 then
    begin
      repeat
        L.Add(ExtractFilePath(VisDb) + SR.Name);
      until FindNext(SR) <> 0;
      FindClose(SR);
    end;
    for i := 0 to L.Count - 1 do DeleteFile(L[i]);
  finally L.Free; end;
  for i := 0 to 7 do
  begin
    case i of
      0: Said := NanPath;
      1: Said := ChangeFileExt(NanPath, '.jrn');
      2: Said := IniPath;
      3: Said := VisDb;
      4: Said := VisJrn;
      5: Said := VisDb + '.bak';
      6: Said := VisDb + '.tmp';
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
