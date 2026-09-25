program afstress;

{ Does what the game does: the main thread writes visits while search threads read them.
  afstress.exe <antifake_dll.dll>  - prints where it stops if it ever stops.

  Built from the library folder (source_dll), on Windows with FPC 3.2 for i386:
    mkdir build\test
    fpc -Mobjfpc -Sh -O2 -Fu. -FUbuild\test -FEbuild\test tests\afstress.pas
  and run in build\test next to a copy of build\win32\antifake_dll.dll. }

{$mode objfpc}{$H+}

uses
  SysUtils, Math, dynlibs;

type
  TStartTask = procedure(PlayerID, TaskType, Kind, SortMethod, TargetIndex, MaxResults: LongInt;
    SearchPhrase: PChar; SearchAll, ComparePartial, FilterFirst, FilterLast, FilterAll: Boolean); cdecl;
  TTaskFlag = function(PlayerID: LongInt): Boolean; cdecl;
  TSelect = procedure(PlayerID: LongInt); cdecl;
  TCount = function: LongInt; cdecl;
  TLogin = function(Name, SteamID, HWID, IP: PChar; IncLoginCount: Boolean): LongInt; cdecl;
  TVisitNote = procedure(Index: LongInt; Name, Hwid, IP: PChar; AddSeconds: LongInt); cdecl;
  TGeoCheck = procedure(IP: PChar); cdecl;
  TGeoText = function(IP: PChar): PChar; cdecl;

var
  Lib: TLibHandle;
  StartTask: TStartTask;
  TaskDone, TaskBusy: TTaskFlag;
  SelectResult: TSelect;
  ResultCount, ProfileCount: TCount;
  LoginPlayer: TLogin;
  VisitNote: TVisitNote;
  CheckGeo: TGeoCheck;
  GeoString: TGeoText;
  Idx, Round_, Waited, Kind: LongInt;
  Stuck: Boolean;

begin
  SetExceptionMask([exInvalidOp, exDenormalized, exZeroDivide, exOverflow, exUnderflow, exPrecision]);
  Lib := LoadLibrary(ParamStr(1));
  if Lib = NilHandle then begin WriteLn('library did not load'); Halt(1); end;
  StartTask := TStartTask(GetProcedureAddress(Lib, 'Start_Search_Task'));
  TaskDone := TTaskFlag(GetProcedureAddress(Lib, 'Is_Task_Complete'));
  TaskBusy := TTaskFlag(GetProcedureAddress(Lib, 'Is_Task_In_Progress'));
  SelectResult := TSelect(GetProcedureAddress(Lib, 'Select_Task_Result'));
  ResultCount := TCount(GetProcedureAddress(Lib, 'Get_Result_Count'));
  ProfileCount := TCount(GetProcedureAddress(Lib, 'Get_Profile_Count'));
  LoginPlayer := TLogin(GetProcedureAddress(Lib, 'Login_Player'));
  VisitNote := TVisitNote(GetProcedureAddress(Lib, 'Visit_Note'));
  CheckGeo := TGeoCheck(GetProcedureAddress(Lib, 'Check_And_Update_Geo'));
  GeoString := TGeoText(GetProcedureAddress(Lib, 'Get_Geo_String'));

  Idx := LoginPlayer(PChar('Stress'), PChar(''), PChar('HW-STRESS'), PChar('5.6.7.8'), True);
  WriteLn('profile #', Idx, ' of ', ProfileCount());

  Stuck := False;
  for Round_ := 1 to 60 do
  begin
    Kind := Round_ mod 3;
    { the game thread keeps writing while the search runs }
    VisitNote(Idx, PChar('Stress'), PChar('HW-STRESS'), PChar('5.6.7.' + IntToStr(Round_ mod 7)), 30);
    CheckGeo(PChar('5.6.7.' + IntToStr(Round_ mod 7)));
    GeoString(PChar('5.6.7.' + IntToStr(Round_ mod 7)));
    case Kind of
      0: StartTask(1, 12, 0, 7, Idx, 0, PChar(''), False, False, False, False, False);
      1: StartTask(1, 13, 0, 7, -1, 0, PChar(''), False, False, False, False, False);
      2: StartTask(1, 14, 0, 3, -1, 0, PChar(''), False, False, False, False, False);
    end;
    Waited := 0;
    while (Waited < 300) and not TaskDone(1) do
    begin
      VisitNote(Idx, PChar('Stress'), PChar('HW-STRESS'), PChar('5.6.7.' + IntToStr(Round_ mod 7)), 5);
      Sleep(10);
      Inc(Waited);
    end;
    if not TaskDone(1) then
    begin
      WriteLn('STUCK in round ', Round_, ' (task type ', 12 + Kind, ')');
      Stuck := True;
      Break;
    end;
    SelectResult(1);
    if Round_ mod 20 = 0 then
      WriteLn('round ', Round_, ': ', ResultCount(), ' rows');
  end;

  if Stuck then
  begin
    WriteLn('FAILED: a task never finished');
    Halt(1);
  end;
  WriteLn('ALL ROUNDS PASSED');
end.
