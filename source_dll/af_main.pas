library antifake_dll;

{$mode objfpc}{$H+}

uses
  {$IFDEF UNIX}cthreads, BaseUnix, Unix,{$ENDIF}
  SysUtils, Classes, Math, DateUtils, Process, dynlibs, fphttpclient, ssockets, fpjson, jsonparser, crc, mysqlwire;

{ ---------------- the library, module by module ----------------

  Everything below is in its own file, in the order the parts need each other. The
  views and the commands come last, after everything they draw from exists. }
{$I af_types.inc}
{$I af_settings.inc}
{$I af_util.inc}
{$I af_geo.inc}
{$I af_profiles.inc}
{$I af_match.inc}
{$I af_steam.inc}
{$I af_store.inc}
{$I af_tasks.inc}
{$I af_exports.inc}

{ ---------------- everything the players see ----------------

  Five files, in the order they need each other: the letters and the buffer, the small
  lookups and the rules of who may ask what, the tables of a finished search, the views
  that answer at once, and the door the script knocks on. }
{$I af_text.inc}
{$I af_commands.inc}
{$I af_views.inc}
{$I af_dispatch.inc}
{$I af_run.inc}

exports
  Start_Search_Task, Is_Task_Complete, Is_Task_In_Progress, Select_Task_Result,
  Get_Result_Player_ID, Get_Result_Task_Type, Get_Result_Kind, Get_Result_Sort_Method,
  Get_Result_Count, Get_Result_Execution_Time, Get_Result_Item_Index, Get_Result_Item_SubIndex,
  Get_Result_Item_String, Get_Result_Item_LoginCount, Get_Result_Item_TimePlayed,
  Get_Result_Item_FirstUsedDate, Get_Result_Item_LastUsedDate, Get_Result_Item_NamesCount,
  Get_Result_Item_SteamsCount, Get_Result_Item_HWIDsCount, Get_Result_Item_IPsCount,
  Get_Result_Active_Name, Get_Result_Item_ActiveName, Get_Result_Profile_Index, Get_Result_Register_Time,
  Get_Result_Last_Played_Time, Get_Result_Time_Played, Get_Result_Time_On_Spectator,
  Get_Result_Multi_Accounts_Count, Get_Result_Multi_Accounts_ByName_Count, Get_Result_Item_MatchType, Clear_Task_Result,
  Start_Load_Binary, Is_Load_Complete, Is_Load_Error, Is_Load_In_Progress,
  Load_Settings, Get_Setting_Int, Get_Setting_Str, Run_Maintenance, Get_Maintenance_Report, Utf8_To_Console,
  Visit_Note, Get_Visit_Count, Get_Result_Item_Location, Get_Result_Item_Confidence, Get_Result_Item_Flags,
  Steam_Note, Steam_Tick, Get_Steam_Info, Get_Steam_Status,
  Journal_Flush, Journal_Final, Get_Journal_Status, Get_Backup_List, Restore_Backup,
  Mysql_Tick, Get_Mysql_Status, Get_Load_Note, Notice_Line, Start_Save_For, Save_After_Load,
  { the door the script knocks on, and the lines it reads back }
  Run_Command, Command_Busy, Command_Started_Task, Out_Count, Out_Line, Out_Color, Out_Clear, Out_Drop,
  Set_Online, Clear_Online, Render_Result, Multi_Notice, Log_Line,
  Get_Multi_Links, Set_Multi_Verdict, Get_Multi_Verdict, Get_Multi_Stats,
  Get_Profile_Count, Get_Profile_Active_Name, Get_Profile_Active_Steam, Get_Profile_Active_HWID,
  Get_Profile_Register_Time, Get_Profile_Last_Played, Get_Profile_Time_Played, Get_Profile_Time_Spectator,
  Add_Profile_Time, Update_Other_Time, Update_Other_Time_Value, Find_Profile_By_Name, Find_Profile_By_Steam, Find_Profile_By_HWID,
  Login_Player, Get_Login_Result, Rollback_Profile_Changes,
  Start_Save_Binary, Is_Save_Complete, Is_Save_Error, Is_Save_In_Progress, Cleanup,
  Get_Result_Item_Similarity, Check_And_Update_Geo, Get_Geo_String, Get_Geo_Join_String, Get_Result_Item_Risk, Get_Result_Top_Locations;

var i: Integer;

initialization
  DefaultFormatSettings.DecimalSeparator := '.';
  { every "hh:nn" of a table and of the logs, whatever the locale of the machine says }
  DefaultFormatSettings.TimeSeparator := ':';
  { the library is loaded by the script, on the server's own thread }
  GameThread := GetCurrentThreadId;
  {$IFDEF UNIX}IgnoreBrokenPipes;{$ENDIF}
  InitCriticalSection(ProfilesCritSec);
  InitCriticalSection(PendingTimeCS);
  InitCriticalSection(MaintCS);
  InitCriticalSection(GeoCritSec);
  GeoPending := TStringList.Create;
  InitCriticalSection(TraceCS);
  TraceReady := True;
  InitCriticalSection(LogCS);
  InitCriticalSection(SteamCS);
  InitCriticalSection(JournalCS);
  InitCriticalSection(OutCS);
  InitCriticalSection(CmdCS);
  for i := 0 to MAX_SLOTS do
  begin
    Outp[i].Lines := TStringList.Create;
    OnlineProfile[i] := -1;
    OnlineHere[i] := False;
    ViewPage[i] := 1;
    LastCmdAt[i] := 0;
    LastHeavyAt[i] := 0;
  end;
  InitCriticalSection(MirrorCS);
  InitCriticalSection(SharedCS);
  InitCriticalSection(SharedDiskCS);
  SharedGeoNew := TStringList.Create;
  InitCriticalSection(NoticeCS);
  NoticeList := TStringList.Create;
  InitCriticalSection(BgCS);
  BgThreads := TList.Create;
  InitCriticalSection(OnlineCS);
  LiveConns := TList.Create;
  SteamQueue := TStringList.Create;
  GeoFailed := TStringList.Create;
  { the keys are plain text (numbers, addresses): compared byte by byte, without the locale,
    which makes every sort and every lookup several times faster }
  SteamIndex := TStringList.Create;
  SteamIndex.UseLocale := False;
  SteamIndex.CaseSensitive := True;
  SteamIndex.Duplicates := dupAccept;
  SteamIndex.Sorted := True;
  HwidIndex := TStringList.Create;
  HwidIndex.UseLocale := False;
  HwidIndex.CaseSensitive := True;
  HwidIndex.Duplicates := dupAccept;
  HwidIndex.Sorted := True;
  IpIndex := TStringList.Create;
  IpIndex.UseLocale := False;
  IpIndex.CaseSensitive := True;
  IpIndex.Duplicates := dupAccept;
  IpIndex.Sorted := True;
  Net24Index := TStringList.Create;
  Net24Index.UseLocale := False;
  Net24Index.CaseSensitive := True;
  Net24Index.Duplicates := dupAccept;
  Net24Index.Sorted := True;
  VerdictIndex := TStringList.Create;
  VerdictIndex.UseLocale := False;
  VerdictIndex.CaseSensitive := True;
  VerdictIndex.Duplicates := dupIgnore;
  VerdictIndex.Sorted := True;
  GeoIndex := TStringList.Create;
  GeoIndex.UseLocale := False;
  GeoIndex.CaseSensitive := True;
  GeoIndex.Duplicates := dupAccept;
  GeoIndex.Sorted := True;
  SetLength(Profiles, 100);
  SetLength(GeoCache, 100);

  ActiveResult := @EmptyResult;
  FillChar(EmptyResult, SizeOf(EmptyResult), 0);

  for i := 1 to MAX_SLOTS do begin
    Contexts[i].IsBusy := False;
    Contexts[i].IsComplete := False;
    Contexts[i].Thread := nil;
    InitCriticalSection(Contexts[i].Lock);
  end;

finalization
  { never Cleanup itself: this runs under the lock of the loader, where no thread can be
    waited for (see CleanupAtUnload) }
  CleanupAtUnload;
  FreeAndNil(LogQueue);
  DoneCriticalSection(LogCS);
  FreeAndNil(BgThreads);
  DoneCriticalSection(BgCS);
  FreeAndNil(LiveConns);
  DoneCriticalSection(OnlineCS);
  DoneCriticalSection(ProfilesCritSec);
  DoneCriticalSection(GeoCritSec);
  for i := 1 to MAX_SLOTS do DoneCriticalSection(Contexts[i].Lock);
  SteamIndex.Free;
  HwidIndex.Free;
  IpIndex.Free;
  Net24Index.Free;
  VerdictIndex.Free;
  GeoIndex.Free;
  GeoPending.Free;
  GeoFailed.Free;
  FreeAndNil(SharedGeoNew);
  DoneCriticalSection(SharedCS);
  DoneCriticalSection(SharedDiskCS);
  { the rest of what the start made: a script that is loaded again (/recompile) gets a fresh
    library, and these were left behind by every old one }
  for i := 0 to MAX_SLOTS do FreeAndNil(Outp[i].Lines);
  FreeAndNil(NoticeList);
  FreeAndNil(SteamQueue);
  TraceReady := False;
  DoneCriticalSection(TraceCS);
  DoneCriticalSection(SteamCS);
  DoneCriticalSection(JournalCS);
  DoneCriticalSection(OutCS);
  DoneCriticalSection(CmdCS);
  DoneCriticalSection(MirrorCS);
  DoneCriticalSection(NoticeCS);
  DoneCriticalSection(MaintCS);
  DoneCriticalSection(PendingTimeCS);

end.
