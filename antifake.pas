unit Antifake;

{ =========================================================================================
  AntiFake Extended System Advanced 2.1.1 - the script side.

  Everything that thinks is in antifake_dll: the profiles, the searching, the visits, the
  Steam lookups, the multi-account score, the storage and every table that is drawn.
  ScriptCore is an interpreter and every line of it runs on the game thread, so this file
  does only what nothing else can:

    - read the players (name, address, computer, Steam id, admin) and hand them over,
    - pass on a command and print the lines the library gives back,
    - tick.

  Nothing here searches, sorts, formats or waits. The library answers on its own threads
  and never holds the game thread for longer than it takes to copy a few strings.

  The settings live in settings.ini next to this file; the library reads them.
  ========================================================================================= }

interface

{ ---------------- the library ---------------- }

procedure DLL_Start_Load_Binary(FilePath: PChar);
external {$IFDEF WIN32} 'Start_Load_Binary@scripts/AntiFake-Extended/antifake_dll.dll cdecl' {$ELSE} 'Start_Load_Binary@scripts/AntiFake-Extended/antifake_dll.so cdecl' {$ENDIF};

function DLL_Is_Load_Complete(): Boolean;
external {$IFDEF WIN32} 'Is_Load_Complete@scripts/AntiFake-Extended/antifake_dll.dll cdecl' {$ELSE} 'Is_Load_Complete@scripts/AntiFake-Extended/antifake_dll.so cdecl' {$ENDIF};

function DLL_Is_Load_Error(): Boolean;
external {$IFDEF WIN32} 'Is_Load_Error@scripts/AntiFake-Extended/antifake_dll.dll cdecl' {$ELSE} 'Is_Load_Error@scripts/AntiFake-Extended/antifake_dll.so cdecl' {$ENDIF};

function DLL_Is_Load_In_Progress(): Boolean;
external {$IFDEF WIN32} 'Is_Load_In_Progress@scripts/AntiFake-Extended/antifake_dll.dll cdecl' {$ELSE} 'Is_Load_In_Progress@scripts/AntiFake-Extended/antifake_dll.so cdecl' {$ENDIF};

function DLL_Get_Profile_Count(): Integer;
external {$IFDEF WIN32} 'Get_Profile_Count@scripts/AntiFake-Extended/antifake_dll.dll cdecl' {$ELSE} 'Get_Profile_Count@scripts/AntiFake-Extended/antifake_dll.so cdecl' {$ENDIF};

function DLL_Login_Player(Name, SteamID, HWID, IP: PChar; IncLoginCount: Boolean): Integer;
external {$IFDEF WIN32} 'Login_Player@scripts/AntiFake-Extended/antifake_dll.dll cdecl' {$ELSE} 'Login_Player@scripts/AntiFake-Extended/antifake_dll.so cdecl' {$ENDIF};

function DLL_Get_Login_Result(Field: Integer): Integer;
external {$IFDEF WIN32} 'Get_Login_Result@scripts/AntiFake-Extended/antifake_dll.dll cdecl' {$ELSE} 'Get_Login_Result@scripts/AntiFake-Extended/antifake_dll.so cdecl' {$ENDIF};

procedure DLL_Rollback_Profile_Changes(ProfileIdx: Integer; Name, HWID, IP: PChar; Flags: Integer);
external {$IFDEF WIN32} 'Rollback_Profile_Changes@scripts/AntiFake-Extended/antifake_dll.dll cdecl' {$ELSE} 'Rollback_Profile_Changes@scripts/AntiFake-Extended/antifake_dll.so cdecl' {$ENDIF};

procedure DLL_Add_Profile_Time(Index, TimePlayed, TimeSpectator: Integer);
external {$IFDEF WIN32} 'Add_Profile_Time@scripts/AntiFake-Extended/antifake_dll.dll cdecl' {$ELSE} 'Add_Profile_Time@scripts/AntiFake-Extended/antifake_dll.so cdecl' {$ENDIF};

procedure DLL_Update_Other_Time_Value(Index, Kind: Integer; Value: PChar; TimePlayed, TimeSpectator: Integer);
external {$IFDEF WIN32} 'Update_Other_Time_Value@scripts/AntiFake-Extended/antifake_dll.dll cdecl' {$ELSE} 'Update_Other_Time_Value@scripts/AntiFake-Extended/antifake_dll.so cdecl' {$ENDIF};

procedure DLL_Visit_Note(Index: Integer; Name, Hwid, IP: PChar; AddSeconds: Integer);
external {$IFDEF WIN32} 'Visit_Note@scripts/AntiFake-Extended/antifake_dll.dll cdecl' {$ELSE} 'Visit_Note@scripts/AntiFake-Extended/antifake_dll.so cdecl' {$ENDIF};

procedure DLL_Check_And_Update_Geo(IP: PChar);
external {$IFDEF WIN32} 'Check_And_Update_Geo@scripts/AntiFake-Extended/antifake_dll.dll cdecl' {$ELSE} 'Check_And_Update_Geo@scripts/AntiFake-Extended/antifake_dll.so cdecl' {$ENDIF};

function DLL_Get_Geo_Join_String(IP: PChar): PChar;
external {$IFDEF WIN32} 'Get_Geo_Join_String@scripts/AntiFake-Extended/antifake_dll.dll cdecl' {$ELSE} 'Get_Geo_Join_String@scripts/AntiFake-Extended/antifake_dll.so cdecl' {$ENDIF};

procedure DLL_Start_Save_Binary(FilePath: PChar);
external {$IFDEF WIN32} 'Start_Save_Binary@scripts/AntiFake-Extended/antifake_dll.dll cdecl' {$ELSE} 'Start_Save_Binary@scripts/AntiFake-Extended/antifake_dll.so cdecl' {$ENDIF};

function DLL_Start_Save_For(FilePath: PChar; Slot: Integer): Boolean;
external {$IFDEF WIN32} 'Start_Save_For@scripts/AntiFake-Extended/antifake_dll.dll cdecl' {$ELSE} 'Start_Save_For@scripts/AntiFake-Extended/antifake_dll.so cdecl' {$ENDIF};

function DLL_Is_Save_In_Progress(): Boolean;
external {$IFDEF WIN32} 'Is_Save_In_Progress@scripts/AntiFake-Extended/antifake_dll.dll cdecl' {$ELSE} 'Is_Save_In_Progress@scripts/AntiFake-Extended/antifake_dll.so cdecl' {$ENDIF};

procedure DLL_Cleanup();
external {$IFDEF WIN32} 'Cleanup@scripts/AntiFake-Extended/antifake_dll.dll cdecl' {$ELSE} 'Cleanup@scripts/AntiFake-Extended/antifake_dll.so cdecl' {$ENDIF};

function DLL_Load_Settings(Path: PChar): Boolean;
external {$IFDEF WIN32} 'Load_Settings@scripts/AntiFake-Extended/antifake_dll.dll cdecl' {$ELSE} 'Load_Settings@scripts/AntiFake-Extended/antifake_dll.so cdecl' {$ENDIF};

function DLL_Get_Setting_Int(Name: PChar): Integer;
external {$IFDEF WIN32} 'Get_Setting_Int@scripts/AntiFake-Extended/antifake_dll.dll cdecl' {$ELSE} 'Get_Setting_Int@scripts/AntiFake-Extended/antifake_dll.so cdecl' {$ENDIF};

function DLL_Get_Setting_Str(Name: PChar): PChar;
external {$IFDEF WIN32} 'Get_Setting_Str@scripts/AntiFake-Extended/antifake_dll.dll cdecl' {$ELSE} 'Get_Setting_Str@scripts/AntiFake-Extended/antifake_dll.so cdecl' {$ENDIF};

function DLL_Run_Maintenance(Force: Boolean): Boolean;
external {$IFDEF WIN32} 'Run_Maintenance@scripts/AntiFake-Extended/antifake_dll.dll cdecl' {$ELSE} 'Run_Maintenance@scripts/AntiFake-Extended/antifake_dll.so cdecl' {$ENDIF};

function DLL_Get_Maintenance_Report(): PChar;
external {$IFDEF WIN32} 'Get_Maintenance_Report@scripts/AntiFake-Extended/antifake_dll.dll cdecl' {$ELSE} 'Get_Maintenance_Report@scripts/AntiFake-Extended/antifake_dll.so cdecl' {$ENDIF};

procedure DLL_Steam_Note(SteamId: PChar);
external {$IFDEF WIN32} 'Steam_Note@scripts/AntiFake-Extended/antifake_dll.dll cdecl' {$ELSE} 'Steam_Note@scripts/AntiFake-Extended/antifake_dll.so cdecl' {$ENDIF};

procedure DLL_Steam_Tick();
external {$IFDEF WIN32} 'Steam_Tick@scripts/AntiFake-Extended/antifake_dll.dll cdecl' {$ELSE} 'Steam_Tick@scripts/AntiFake-Extended/antifake_dll.so cdecl' {$ENDIF};

procedure DLL_Journal_Flush();
external {$IFDEF WIN32} 'Journal_Flush@scripts/AntiFake-Extended/antifake_dll.dll cdecl' {$ELSE} 'Journal_Flush@scripts/AntiFake-Extended/antifake_dll.so cdecl' {$ENDIF};

function DLL_Get_Load_Note(): PChar;
external {$IFDEF WIN32} 'Get_Load_Note@scripts/AntiFake-Extended/antifake_dll.dll cdecl' {$ELSE} 'Get_Load_Note@scripts/AntiFake-Extended/antifake_dll.so cdecl' {$ENDIF};

function DLL_Get_Load_Millis(): Integer;
external {$IFDEF WIN32} 'Get_Load_Millis@scripts/AntiFake-Extended/antifake_dll.dll cdecl' {$ELSE} 'Get_Load_Millis@scripts/AntiFake-Extended/antifake_dll.so cdecl' {$ENDIF};

function DLL_Notice_Line(): PChar;
external {$IFDEF WIN32} 'Notice_Line@scripts/AntiFake-Extended/antifake_dll.dll cdecl' {$ELSE} 'Notice_Line@scripts/AntiFake-Extended/antifake_dll.so cdecl' {$ENDIF};

procedure DLL_Mysql_Tick(Force: Boolean);
external {$IFDEF WIN32} 'Mysql_Tick@scripts/AntiFake-Extended/antifake_dll.dll cdecl' {$ELSE} 'Mysql_Tick@scripts/AntiFake-Extended/antifake_dll.so cdecl' {$ENDIF};

function DLL_Save_After_Load(): Boolean;
external {$IFDEF WIN32} 'Save_After_Load@scripts/AntiFake-Extended/antifake_dll.dll cdecl' {$ELSE} 'Save_After_Load@scripts/AntiFake-Extended/antifake_dll.so cdecl' {$ENDIF};

function DLL_Is_Task_Complete(PlayerID: Integer): Boolean;
external {$IFDEF WIN32} 'Is_Task_Complete@scripts/AntiFake-Extended/antifake_dll.dll cdecl' {$ELSE} 'Is_Task_Complete@scripts/AntiFake-Extended/antifake_dll.so cdecl' {$ENDIF};

procedure DLL_Clear_Task_Result(PlayerID: Integer);
external {$IFDEF WIN32} 'Clear_Task_Result@scripts/AntiFake-Extended/antifake_dll.dll cdecl' {$ELSE} 'Clear_Task_Result@scripts/AntiFake-Extended/antifake_dll.so cdecl' {$ENDIF};

function DLL_Run_Command(Slot: Integer; Cmd, Params: PChar; IsAdmin: Boolean; Who: PChar): Integer;
external {$IFDEF WIN32} 'Run_Command@scripts/AntiFake-Extended/antifake_dll.dll cdecl' {$ELSE} 'Run_Command@scripts/AntiFake-Extended/antifake_dll.so cdecl' {$ENDIF};

function DLL_Command_Busy(Slot: Integer): Boolean;
external {$IFDEF WIN32} 'Command_Busy@scripts/AntiFake-Extended/antifake_dll.dll cdecl' {$ELSE} 'Command_Busy@scripts/AntiFake-Extended/antifake_dll.so cdecl' {$ENDIF};

function DLL_Command_Started_Task(Slot: Integer): Boolean;
external {$IFDEF WIN32} 'Command_Started_Task@scripts/AntiFake-Extended/antifake_dll.dll cdecl' {$ELSE} 'Command_Started_Task@scripts/AntiFake-Extended/antifake_dll.so cdecl' {$ENDIF};

function DLL_Out_Count(Slot: Integer): Integer;
external {$IFDEF WIN32} 'Out_Count@scripts/AntiFake-Extended/antifake_dll.dll cdecl' {$ELSE} 'Out_Count@scripts/AntiFake-Extended/antifake_dll.so cdecl' {$ENDIF};

function DLL_Out_Line(Slot, Index: Integer): PChar;
external {$IFDEF WIN32} 'Out_Line@scripts/AntiFake-Extended/antifake_dll.dll cdecl' {$ELSE} 'Out_Line@scripts/AntiFake-Extended/antifake_dll.so cdecl' {$ENDIF};

function DLL_Out_Color(Slot, Index: Integer): Integer;
external {$IFDEF WIN32} 'Out_Color@scripts/AntiFake-Extended/antifake_dll.dll cdecl' {$ELSE} 'Out_Color@scripts/AntiFake-Extended/antifake_dll.so cdecl' {$ENDIF};

procedure DLL_Out_Drop(Slot, Count: Integer);
external {$IFDEF WIN32} 'Out_Drop@scripts/AntiFake-Extended/antifake_dll.dll cdecl' {$ELSE} 'Out_Drop@scripts/AntiFake-Extended/antifake_dll.so cdecl' {$ENDIF};

procedure DLL_Set_Online(Slot, ProfileIdx: Integer; Name, IP, Hwid: PChar; IsAdmin: Boolean);
external {$IFDEF WIN32} 'Set_Online@scripts/AntiFake-Extended/antifake_dll.dll cdecl' {$ELSE} 'Set_Online@scripts/AntiFake-Extended/antifake_dll.so cdecl' {$ENDIF};

procedure DLL_Clear_Online(Slot: Integer);
external {$IFDEF WIN32} 'Clear_Online@scripts/AntiFake-Extended/antifake_dll.dll cdecl' {$ELSE} 'Clear_Online@scripts/AntiFake-Extended/antifake_dll.so cdecl' {$ENDIF};

procedure DLL_Render_Result(Slot: Integer);
external {$IFDEF WIN32} 'Render_Result@scripts/AntiFake-Extended/antifake_dll.dll cdecl' {$ELSE} 'Render_Result@scripts/AntiFake-Extended/antifake_dll.so cdecl' {$ENDIF};

function DLL_Journal_Final(): Boolean;
external {$IFDEF WIN32} 'Journal_Final@scripts/AntiFake-Extended/antifake_dll.dll cdecl' {$ELSE} 'Journal_Final@scripts/AntiFake-Extended/antifake_dll.so cdecl' {$ENDIF};

function DLL_Multi_Notice(ProfileIdx: Integer; PlayerName: PChar): PChar;
external {$IFDEF WIN32} 'Multi_Notice@scripts/AntiFake-Extended/antifake_dll.dll cdecl' {$ELSE} 'Multi_Notice@scripts/AntiFake-Extended/antifake_dll.so cdecl' {$ENDIF};

procedure DLL_Log_Line(Text: PChar);
external {$IFDEF WIN32} 'Log_Line@scripts/AntiFake-Extended/antifake_dll.dll cdecl' {$ELSE} 'Log_Line@scripts/AntiFake-Extended/antifake_dll.so cdecl' {$ENDIF};

implementation

const
  AF_VERSION = '2.1.1';
  INI_FILE = 'scripts/AntiFake-Extended/settings.ini';
  DB_FILE_CONST = 'nickreg/antifake.dat';
  CMD_PREFIX = '/';
  CONSOLE_SLOT = 33;

  { what DLL_Run_Command answers: 0 not a command of AntiFake, 2 a task that answers on a later
    tick; anything else (3, refused or answered at once) has its lines in the buffer already }
  CMD_UNKNOWN = 0;
  CMD_TASK = 2;

  KIND_NAME = 0;
  KIND_STEAM = 1;
  KIND_HWID = 2;
  KIND_IP = 3;

  { what Login_Player added, as Get_Login_Result(4) says it: 1 a name, 2 a Steam id, 4 a
    computer, 8 an address, 16 a new account; Rollback_Profile_Changes reads the same bits }
  LOGIN_FLAG_NEW_NAME = 1;
  LOGIN_FLAG_NEW_STEAM = 2;
  LOGIN_FLAG_NEW_PROFILE = 16;

  { what one tick does at most: the rest waits for the next ticks }
  PLAYERS_PER_TICK = 4;       { the minute's time, and the logins after the database was read }
  LIBRARY_LINES_PER_TICK = 16; { lines taken from the library for a player (4 of them are sent) }
  CONSOLE_LINES_PER_TICK = 8;  { lines written to the server console }
  NOTICES_PER_TICK = 8;

var
  { one row per player: which account they are on, and what the login wrote down, so a
    correction can be taken back }
  PlayerProfile: array[1..32] of Integer;
  PlayerLoginFlags: array[1..32] of Integer;
  PlayerJoinTime: array[1..32] of Double;
  PlayerLastTimeUpdate: array[1..32] of Double;
  { the team the time since the last look belongs to: a team change counts that time first }
  PlayerTeam: array[1..32] of Integer;
  PlayerPendingGeo: array[1..32] of Boolean;
  { Soldat lets a player change their nick while they are playing, and that name belongs in
    the account too; this is what the last look saw }
  PlayerLastName: array[1..32] of string;
  PlayerGeoTimer: array[1..32] of Integer;
  PlayerTaskPending: array[1..CONSOLE_SLOT] of Boolean;
  PlayerTaskTimer: array[1..CONSOLE_SLOT] of Integer;
  { the Steam id the last login used, and the account the admins last heard about: OnJoin and
    OnSteamAuth both come for one join }
  PlayerSteam: array[1..32] of string;
  PlayerNoticeFor: array[1..32] of Integer;
  { the next slot of the minute's time update, and of the logins after the load (0 = none) }
  MinuteNext, LoginNext: Integer;
  { the logins after the load: those players are not announced as if they had just joined }
  QuietLogin: Boolean;
  ScriptErrors: Integer;
  { the last tick that sent lines, by the clock }
  LastRealTick: Double;
  { humans were in the game when the script started (a /recompile): the admins hear when the
    database is loaded again }
  AnnounceLoad: Boolean;

  { a command is answered one tick after it was typed: writing to a player from inside the
    chat event is what used to freeze the server. A second command typed in the same tick (a
    bind, a macro) waits in the second place and runs a tick later. }
  PendingCmd: array[1..32] of string;
  PendingParams: array[1..32] of string;
  PendingCmd2: array[1..32] of string;
  PendingParams2: array[1..32] of string;

  { Soldat cannot take a whole table in one tick either, so the lines go out a few at a time }
  OutQueue: array[1..32] of TStringList;
  OUT_PER_TICK: Integer;
  OUT_MAX_LINES: Integer;
  { the lines that did not fit into a full queue; the player hears how many after the rest }
  OutDropped: array[1..32] of Integer;

  DB_FILE: string;
  DatabaseLoaded, DatabaseError, PendingSave: Boolean;
  { a full write is due and waits for the time of every player to go in first }
  SaveWaitsForTime: Boolean;
  LastSaveTime: Double;
  SAVE_INTERVAL, TASK_TIMEOUT_TICKS, LOCATE_TIMEOUT, JOURNAL_SECONDS: Integer;
  LastMaintTick, LastJournalTick, TimeUpdateSeconds: Integer;
  USE_ON_SPEAK_CMD, USE_ON_COMMAND, USE_ON_ADMIN_COMMAND: Boolean;
  USE_GEODATA, PUBLIC_ENABLED, STEAM_ENABLED, MYSQL_ON, MULTI_NOTICE: Boolean;
  MYSQL_ENGINE: Integer;   { 0 = local: the file only, 1 = mirror: the file with a copy in MySQL, 2 = mysql: one database for every server }
  { the colours of [Colors] in settings.ini, the ones the library writes its own lines in }
  COLOR_INFO, COLOR_WARN, COLOR_GOOD: Integer;

{ ---------------- talking to a player ---------------- }

function SlotOf(Player: TActivePlayer): Integer;
begin
  if Player = nil then Result := CONSOLE_SLOT else Result := Player.ID;
end;

function SlotIsAdmin(Slot: Integer): Boolean;
begin
  Result := False;
  if Slot = CONSOLE_SLOT then Result := True
  else if (Slot >= 1) and (Slot <= 32) then
    if Players[Slot].Active then Result := Players[Slot].IsAdmin;
end;

{ a line for the server console, kept in logs/antifake/antifake.log as well }
procedure LogNote(Text: string);
begin
  WriteLn('[AntiFake] ' + Text);
  DLL_Log_Line(PChar(Text));
end;

{ One line to one slot; the console gets it at once, a player through the queue. A player never
  gets it at once: this runs inside the events too, and a write from there (or many in one
  tick) stops the server. A full queue keeps its order and counts what did not fit. }
procedure SaySlot(Slot: Integer; Text: string; Color: Integer);
begin
  if (Slot = CONSOLE_SLOT) or (Slot < 1) or (Slot > 32) then
  begin
    WriteLn('[AntiFake] ' + Text);
    Exit;
  end;
  if not Players[Slot].Active then Exit;
  if OutQueue[Slot] = nil then Exit;
  if OutQueue[Slot].Count >= OUT_MAX_LINES then
  begin
    OutDropped[Slot] := OutDropped[Slot] + 1;
    Exit;
  end;
  OutQueue[Slot].Add(IntToStr(Color) + '|' + Text);
end;

{ What the library has written for this slot, a few lines a tick and for a player no more than
  the queue can still hold; the rest stays in the library, in order, for the next tick, and so
  do the lines a worker adds meanwhile (only the lines taken here are taken away). The number
  of lines still waiting. }
function FlushLibraryLines(Slot: Integer): Integer;
var
  i, n, Total: Integer;
begin
  Total := DLL_Out_Count(Slot);
  n := Total;
  if Slot = CONSOLE_SLOT then
  begin
    if n > CONSOLE_LINES_PER_TICK then n := CONSOLE_LINES_PER_TICK;
  end
  else
  begin
    if n > LIBRARY_LINES_PER_TICK then n := LIBRARY_LINES_PER_TICK;
    if OutQueue[Slot] <> nil then
      if n > OUT_MAX_LINES - OutQueue[Slot].Count then n := OUT_MAX_LINES - OutQueue[Slot].Count;
    if n < 0 then n := 0;
  end;
  for i := 0 to n - 1 do
    SaySlot(Slot, DLL_Out_Line(Slot, i), DLL_Out_Color(Slot, i));
  DLL_Out_Drop(Slot, n);
  Result := Total - n;
end;

{ /storage save, /storage now, /afstorage save (and the same typed in the chat with !) }
function IsStorageSave(Cmd, Params: string): Boolean;
var
  C, P: string;
begin
  Result := False;
  C := Cmd;
  if Copy(C, 1, 1) = '!' then C := Copy(C, 2, Length(C));
  if (C <> 'storage') and (C <> 'afstorage') then Exit;
  P := LowerCase(Trim(Params));
  Result := (P = 'save') or (P = 'now');
end;

{ ---------------- the time a player spends here ---------------- }

procedure UpdatePlayerTime(ID: Integer);
var
  Elapsed, PlayTime, SpecTime: Integer;
  NameStr, SteamStr: string;
  IsSpec: Boolean;
begin
  if PlayerProfile[ID] < 0 then Exit;
  if PlayerLastTimeUpdate[ID] = 0 then
  begin
    PlayerLastTimeUpdate[ID] := Now;
    PlayerTeam[ID] := Players[ID].Team;
    Exit;
  end;
  Elapsed := Round((Now - PlayerLastTimeUpdate[ID]) * 86400);
  PlayerLastTimeUpdate[ID] := Now;
  { the time since the last look was spent on the team of then (TeamJoined comes here before a
    team change is counted), the time from now on goes to the team of now }
  IsSpec := PlayerTeam[ID] = 5;
  PlayerTeam[ID] := Players[ID].Team;
  { the time is written at least once a minute: a longer gap than ten minutes is the clock
    that moved (summer time), not an hour of play, and a gap below nothing the clock that went
    back; the look of now is the start of the next one either way }
  if (Elapsed < 1) or (Elapsed > 600) then Exit;
  { the account changes, so the full write is due at SaveIntervalMinutes as settings.ini says,
    and not only after a join or a leave (without the journal that was all that was kept) }
  PendingSave := True;
  if IsSpec then begin PlayTime := 0; SpecTime := Elapsed; end
  else begin PlayTime := Elapsed; SpecTime := 0; end;
  DLL_Add_Profile_Time(PlayerProfile[ID], PlayTime, SpecTime);
  { by value, not by place in the list: a rollback or the cleanup moves the entries. The name
    and the Steam id are the ones the last login wrote into the account; a nick changed since
    then is not in it yet (the minute pass adds it right after this) }
  NameStr := PlayerLastName[ID];
  if NameStr = '' then NameStr := Players[ID].Name;
  SteamStr := PlayerSteam[ID];
  DLL_Update_Other_Time_Value(PlayerProfile[ID], KIND_NAME, PChar(NameStr), PlayTime, SpecTime);
  if SteamStr <> '' then
    DLL_Update_Other_Time_Value(PlayerProfile[ID], KIND_STEAM, PChar(SteamStr), PlayTime, SpecTime);
  DLL_Update_Other_Time_Value(PlayerProfile[ID], KIND_HWID, PChar(Players[ID].HWID), PlayTime, SpecTime);
  DLL_Update_Other_Time_Value(PlayerProfile[ID], KIND_IP, PChar(Players[ID].IP), PlayTime, SpecTime);
  DLL_Visit_Note(PlayerProfile[ID], PChar(Players[ID].Name), PChar(Players[ID].HWID),
                 PChar(Players[ID].IP), Elapsed);
end;

{ ---------------- a player arrives ---------------- }

{ the admins hear it when somebody joins on an account that looks like another one: the library
  looks at the links on a worker and hands the lines over as notices for slot 0 (every admin in
  the game and the console), so the join itself costs nothing }
procedure NoteMultiAccount(ID: Integer);
var
  Line: string;
begin
  if not MULTI_NOTICE then Exit;
  if PlayerProfile[ID] < 0 then Exit;
  { OnJoin and OnSteamAuth, or a nick change: the admins hear about an account once }
  if PlayerNoticeFor[ID] = PlayerProfile[ID] then Exit;
  PlayerNoticeFor[ID] := PlayerProfile[ID];
  Line := DLL_Multi_Notice(PlayerProfile[ID], PChar(Players[ID].Name));
end;

procedure ProcessLogin(ID: Byte);
var
  SteamStr, OldName: string;
  OldProfileIdx, NewProfileIdx, TempFlags: Integer;
begin
  if not DatabaseLoaded then Exit;
  if DatabaseError then Exit;
  { bots have no computer and no address of their own, and Soldat gives them the Steam id
    of the server, which changes with every start }
  if not Players[ID].Human then Exit;

  if USE_GEODATA and (PlayerJoinTime[ID] = 0) then
  begin
    DLL_Check_And_Update_Geo(PChar(Players[ID].IP));
    { the players who were here before the database was read are not announced again }
    if not QuietLogin then
    begin
      PlayerPendingGeo[ID] := True;
      PlayerGeoTimer[ID] := 0;
      if OutQueue[ID] <> nil then OutQueue[ID].Clear;
    end;
  end;

  SteamStr := Players[ID].SteamIDString;
  if (SteamStr = 'S0') or (SteamStr = '0') then SteamStr := '';
  PlayerSteam[ID] := SteamStr;
  OldProfileIdx := PlayerProfile[ID];

  if OldProfileIdx < 0 then
  begin
    NewProfileIdx := DLL_Login_Player(PChar(Players[ID].Name), PChar(SteamStr),
                                      PChar(Players[ID].HWID), PChar(Players[ID].IP), True);
    PlayerProfile[ID] := NewProfileIdx;
    PlayerLoginFlags[ID] := DLL_Get_Login_Result(4);
    if (PlayerLoginFlags[ID] and LOGIN_FLAG_NEW_PROFILE) <> 0 then
      LogNote('New player: ' + Players[ID].Name + ' (account #' + IntToStr(NewProfileIdx) + ')')
    else if (PlayerLoginFlags[ID] and LOGIN_FLAG_NEW_NAME) <> 0 then
      LogNote(Players[ID].Name + ' under a new name (account #' + IntToStr(NewProfileIdx) + ')');
    if PlayerJoinTime[ID] = 0 then PlayerJoinTime[ID] := Now;
    PlayerLastTimeUpdate[ID] := Now;
    PlayerTeam[ID] := Players[ID].Team;
    PendingSave := True;
  end
  else
  begin
    NewProfileIdx := DLL_Login_Player(PChar(Players[ID].Name), PChar(SteamStr),
                                      PChar(Players[ID].HWID), PChar(Players[ID].IP), False);
    TempFlags := DLL_Get_Login_Result(4);
    if NewProfileIdx <> OldProfileIdx then
    begin
      LogNote('Correction: moving ' + Players[ID].Name + ' from account #' +
              IntToStr(OldProfileIdx) + ' to #' + IntToStr(NewProfileIdx));
      { what the last login wrote into the old account goes out again, under the name it was
        written with: after a nick change the name of now was never in there }
      OldName := PlayerLastName[ID];
      if OldName = '' then OldName := Players[ID].Name;
      DLL_Rollback_Profile_Changes(OldProfileIdx, PChar(OldName),
                                   PChar(Players[ID].HWID), PChar(Players[ID].IP), PlayerLoginFlags[ID]);
      { a new account has counted the login already, an old one counts it now. That second
        login finds everything in place and adds nothing, so what a later correction takes
        back is what the first one wrote }
      if (TempFlags and LOGIN_FLAG_NEW_PROFILE) = 0 then
        NewProfileIdx := DLL_Login_Player(PChar(Players[ID].Name), PChar(SteamStr),
                                          PChar(Players[ID].HWID), PChar(Players[ID].IP), True);
      PlayerLoginFlags[ID] := TempFlags;
      PlayerProfile[ID] := NewProfileIdx;
      PlayerLastTimeUpdate[ID] := Now;
      PlayerTeam[ID] := Players[ID].Team;
      PendingSave := True;
    end
    else
    begin
      { the same account: a nick changed in the game, or Steam answered after the join. What
        this wrote goes out with the rest if the account turns out to be the wrong one }
      PlayerLoginFlags[ID] := PlayerLoginFlags[ID] or TempFlags;
      if (TempFlags and LOGIN_FLAG_NEW_NAME) <> 0 then
      begin
        LogNote(Players[ID].Name + ' under a new name (account #' + IntToStr(NewProfileIdx) + ')');
        PendingSave := True;
      end;
      if (TempFlags and LOGIN_FLAG_NEW_STEAM) <> 0 then
      begin
        LogNote(Players[ID].Name + ' linked a new Steam id to account #' +
                IntToStr(NewProfileIdx));
        PendingSave := True;
      end;
    end;
  end;

  PlayerLastName[ID] := Players[ID].Name;

  { the library draws the tables that talk about the players, so it has to know them }
  DLL_Set_Online(ID, PlayerProfile[ID], PChar(Players[ID].Name), PChar(Players[ID].IP),
                 PChar(Players[ID].HWID), Players[ID].IsAdmin);

  if STEAM_ENABLED then
    if SteamStr <> '' then DLL_Steam_Note(PChar(SteamStr));

  NoteMultiAccount(ID);
end;

{ ---------------- commands ---------------- }

{ the command is remembered and run on the next tick }
procedure QueueCommand(Player: TActivePlayer; Cmd, Params: string);
var
  Slot, Answer: Integer;
  Who: string;
begin
  Who := 'console';
  Slot := SlotOf(Player);
  if (Slot < 1) or (Slot > 32) then
  begin
    { the console answers through WriteLn and can run at once }
    Answer := DLL_Run_Command(CONSOLE_SLOT, PChar(Cmd), PChar(Params), True, PChar(Who));
    if Answer = CMD_UNKNOWN then Exit;
    if FlushLibraryLines(CONSOLE_SLOT) > 0 then
    begin
      PlayerTaskPending[CONSOLE_SLOT] := True;
      PlayerTaskTimer[CONSOLE_SLOT] := 0;
    end;
    if Answer = CMD_TASK then
    begin
      if IsStorageSave(Cmd, Params) then
        begin
          if DatabaseError then
            LogNote('The database could not be read, so nothing is written over it.')
          else if not DatabaseLoaded then
            LogNote('The database is still being read; try again in a moment.')
          else if DLL_Start_Save_For(PChar(DB_FILE), CONSOLE_SLOT) then
          begin
            LastSaveTime := Now;
            PendingSave := False;
            LogNote('Writing the whole database now.');
          end
          else
            LogNote('A full write is already running.');
        end;
      { the answer comes on a later tick, through the same loop as everything else }
      PlayerTaskPending[CONSOLE_SLOT] := True;
      PlayerTaskTimer[CONSOLE_SLOT] := 0;
    end;
    Exit;
  end;
  { One command a tick. The /command of an admin comes through OnCommand and OnAdminCommand
    both: the server calls the one and then the other with the same text, in one go and before
    any tick, so the second copy of what is waiting is dropped. Another command of the same
    tick (a ! command of ZitroStats and one of AntiFake from one bind) takes the second place;
    a third one is dropped. The console comes through OnAdminCommand alone (above), and a !
    command through OnSpeak alone. }
  if PendingCmd[Slot] = '' then
  begin
    PendingCmd[Slot] := Cmd;
    PendingParams[Slot] := Params;
    Exit;
  end;
  if (PendingCmd[Slot] = Cmd) and (PendingParams[Slot] = Params) then Exit;
  if PendingCmd2[Slot] <> '' then Exit;
  PendingCmd2[Slot] := Cmd;
  PendingParams2[Slot] := Params;
end;

{ one command, handed to the library and answered from its buffer }
procedure RunPending(Slot: Integer);
var
  Answer: Integer;
  Cmd, Params, Who: string;
begin
  Cmd := PendingCmd[Slot];
  Params := PendingParams[Slot];
  PendingCmd[Slot] := PendingCmd2[Slot];
  PendingParams[Slot] := PendingParams2[Slot];
  PendingCmd2[Slot] := '';
  PendingParams2[Slot] := '';
  if Cmd = '' then Exit;
  if not Players[Slot].Active then Exit;
  Who := Players[Slot].Name;
  Answer := DLL_Run_Command(Slot, PChar(Cmd), PChar(Params), Players[Slot].IsAdmin, PChar(Who));
  if Answer = CMD_UNKNOWN then Exit;
  if FlushLibraryLines(Slot) > 0 then
  begin
    PlayerTaskPending[Slot] := True;
    PlayerTaskTimer[Slot] := 0;
  end;
  if Answer = CMD_TASK then
  begin
    if IsStorageSave(Cmd, Params) then
      begin
        if DatabaseError then SaySlot(Slot, 'The database could not be read, so nothing is written over it.', COLOR_WARN)
        else if not DatabaseLoaded then SaySlot(Slot, 'The database is still being read; try again in a moment.', COLOR_WARN)
        else if DLL_Is_Save_In_Progress() then SaySlot(Slot, 'A full write is already running.', COLOR_WARN)
        else
        begin
          UpdatePlayerTime(Slot);
          if DLL_Start_Save_For(PChar(DB_FILE), Slot) then
          begin
            LastSaveTime := Now;
            PendingSave := False;
            SaySlot(Slot, 'Writing the whole database now; you hear when it is done.', COLOR_INFO);
          end
          else
            SaySlot(Slot, 'A full write is already running.', COLOR_WARN);
        end;
      end;
    { the answer comes on a later tick, through the same loop as everything else }
    PlayerTaskPending[Slot] := True;
    PlayerTaskTimer[Slot] := 0;
  end;
end;

{ splits "word rest of it" }
procedure SplitCommand(Text: string; var Cmd, Params: string);
var
  SpacePos: Integer;
begin
  SpacePos := Pos(' ', Text);
  if SpacePos > 0 then
  begin
    Cmd := LowerCase(Copy(Text, 1, SpacePos - 1));
    Params := Trim(Copy(Text, SpacePos + 1, Length(Text)));
  end
  else
  begin
    Cmd := LowerCase(Text);
    Params := '';
  end;
end;

procedure Event_OnSpeak(Player: TActivePlayer; Text: string);
var
  Cmd, Params, S: string;
begin
  { the server gives the team chat with a ^ in front: a ! command typed there counts the same }
  S := Text;
  if Length(S) > 1 then
    if S[1] = '^' then Delete(S, 1, 1);
  if Length(S) < 2 then Exit;
  if (S[1] = '!') and PUBLIC_ENABLED then
  begin
    SplitCommand(Copy(S, 2, Length(S)), Cmd, Params);
    { the library hears that it was typed with the ! of the players: an admin who asks for
      !lastvisitors that way gets the list the players get }
    QueueCommand(Player, '!' + Cmd, Params);
    Exit;
  end;
  if (S[1] = CMD_PREFIX) and USE_ON_SPEAK_CMD then
  begin
    SplitCommand(Copy(S, 2, Length(S)), Cmd, Params);
    QueueCommand(Player, Cmd, Params);
  end;
end;

function Event_OnCommand(Player: TActivePlayer; Command: string): Boolean;
var
  Cmd, Params: string;
begin
  Result := False;
  if Length(Command) < 2 then Exit;
  if Copy(Command, 1, 1) = CMD_PREFIX then Command := Copy(Command, 2, Length(Command));
  SplitCommand(Command, Cmd, Params);
  QueueCommand(Player, Cmd, Params);
end;

function Event_OnAdminCommand(Player: TActivePlayer; Command: string): Boolean;
var
  Cmd, Params: string;
begin
  Result := False;
  if Length(Command) < 2 then Exit;
  { the server console and the remote admin come only this way (without a player), so they are
    heard whatever AdminCommands says; AdminCommands = 0 leaves out the admins in the game }
  if Player <> nil then
    if not USE_ON_ADMIN_COMMAND then Exit;
  if Copy(Command, 1, 1) = CMD_PREFIX then Command := Copy(Command, 2, Length(Command));
  SplitCommand(Command, Cmd, Params);
  QueueCommand(Player, Cmd, Params);
end;

{ ---------------- the events of the game ---------------- }

function WhenSteamLoginAuth(ID: Byte; AuthState: Byte): Byte;
var
  SteamStr: string;
begin
  Result := AuthState;
  if (ID < 1) or (ID > 32) then Exit;
  if AuthState <> 0 then Exit;
  { an answer for a slot that is not in the game yet (OnJoin logs that player in) or any more:
    its name, address and computer would be somebody else's }
  if not Players[ID].Active then Exit;
  if not Players[ID].Human then Exit;
  { OnJoin may have logged the player in with this Steam id already }
  SteamStr := Players[ID].SteamIDString;
  if (SteamStr = 'S0') or (SteamStr = '0') then SteamStr := '';
  if PlayerProfile[ID] >= 0 then
    if SteamStr = PlayerSteam[ID] then Exit;
  ProcessLogin(ID);
end;

procedure JoinGame(Player: TActivePlayer; Team: TTeam);
begin
  ProcessLogin(Player.ID);
end;

{ A team change: the time until now belongs to the team the player leaves (spectator or not).
  A move to the same team, which is how a script puts a player back on their feet, changes
  nothing and costs nothing. }
procedure TeamJoined(Player: TActivePlayer; Team: TTeam);
var
  ID: Integer;
begin
  ID := Player.ID;
  if PlayerProfile[ID] < 0 then Exit;
  if Player.Team = PlayerTeam[ID] then Exit;
  UpdatePlayerTime(ID);
end;

procedure LeaveGame(Player: TActivePlayer; Kicked: Boolean);
var
  ID: Integer;
begin
  ID := Player.ID;
  { only an account changes here: a bot or a player the database never saw leaves nothing to
    write, and asking for a full write for them wrote the whole database for nothing }
  if PlayerProfile[ID] >= 0 then
  begin
    UpdatePlayerTime(ID);
    PendingSave := True;
  end;
  PlayerProfile[ID] := -1;
  PlayerLoginFlags[ID] := 0;
  PlayerJoinTime[ID] := 0;
  PlayerLastTimeUpdate[ID] := 0;
  PlayerTeam[ID] := 0;
  PlayerPendingGeo[ID] := False;
  PlayerGeoTimer[ID] := 0;
  PlayerLastName[ID] := '';
  PlayerTaskPending[ID] := False;
  PlayerTaskTimer[ID] := 0;
  PlayerSteam[ID] := '';
  PlayerNoticeFor[ID] := -1;
  PendingCmd[ID] := '';
  PendingParams[ID] := '';
  PendingCmd2[ID] := '';
  PendingParams2[ID] := '';
  OutDropped[ID] := 0;
  if OutQueue[ID] <> nil then OutQueue[ID].Clear;
  DLL_Clear_Task_Result(ID);
  DLL_Clear_Online(ID);
end;

procedure Timers(Ticks: Integer);
var
  i, j, k, n, Left, NoteColor: Integer;
  ElapsedSave: Double;
  GeoStr, OutLine, Note: string;
  Busy, RealTick: Boolean;
begin
  { After a stall Soldat runs the ticks it missed one right after the other. The lines for the
    players and the console wait for real time: a tick at least 8 ms after the last one. A clock
    that went back (Now is local time: the end of summer time) is time that passed as well, or
    the lines would wait up to an hour for the clock to catch up. }
  RealTick := (Now < LastRealTick) or ((Now - LastRealTick) * 86400000 >= 8);
  if RealTick then LastRealTick := Now;

  { how something that ran in the background ended: "slot|color|text", one call per tick
    when there is nothing; slot 0 is every admin in the game and the console (the library has
    logged such a line already) }
  n := 0;
  Note := DLL_Notice_Line();
  while Note <> '' do
  begin
    k := Pos('|', Note);
    j := StrToIntDef(Copy(Note, 1, k - 1), CONSOLE_SLOT);
    Note := Copy(Note, k + 1, Length(Note));
    k := Pos('|', Note);
    NoteColor := StrToIntDef(Copy(Note, 1, k - 1), COLOR_INFO);
    Note := Copy(Note, k + 1, Length(Note));
    if j = 0 then
    begin
      SaySlot(CONSOLE_SLOT, Note, NoteColor);
      for i := 1 to 32 do
        if SlotIsAdmin(i) then SaySlot(i, Note, NoteColor);
    end
    else
      SaySlot(j, Note, NoteColor);
    n := n + 1;
    if n >= NOTICES_PER_TICK then Break;
    Note := DLL_Notice_Line();
  end;

  { the lines waiting for the players, a few per tick each }
  if RealTick then
    for i := 1 to 32 do
      if OutQueue[i] <> nil then
        if OutQueue[i].Count > 0 then
        begin
          if not Players[i].Active then OutQueue[i].Clear
          else
          begin
            for j := 1 to OUT_PER_TICK do
            begin
              if OutQueue[i].Count = 0 then Break;
              OutLine := OutQueue[i][0];
              OutQueue[i].Delete(0);
              k := Pos('|', OutLine);
              if k > 0 then
                Players[i].WriteConsole(Copy(OutLine, k + 1, Length(OutLine)),
                  StrToIntDef(Copy(OutLine, 1, k - 1), COLOR_INFO));
            end;
            { what did not fit into the full queue is told after the rest }
            if OutQueue[i].Count = 0 then
              if OutDropped[i] > 0 then
              begin
                OutQueue[i].Add(IntToStr(COLOR_WARN) + '|' + IntToStr(OutDropped[i]) +
                  ' more line(s) did not fit and were left out.');
                OutDropped[i] := 0;
              end;
          end;
        end;

  { the commands the players typed, one tick after they typed them }
  for i := 1 to 32 do
    if PendingCmd[i] <> '' then RunPending(i);

  if DLL_Is_Load_In_Progress() then Exit;

  if not DatabaseLoaded then
  begin
    if DLL_Is_Load_Complete() then
    begin
      if DLL_Is_Load_Error() then
      begin
        DatabaseError := True;
        LogNote('The database could not be read. Nothing will be saved until this is fixed.');
        LogNote('Take the last good copy (' + DB_FILE + '.bak) and restart the server.');
      end
      else LogNote('Database loaded: ' + IntToStr(DLL_Get_Profile_Count()) + ' accounts.');
      Note := DLL_Get_Load_Note();
      if Note <> '' then LogNote(Note);
      DatabaseLoaded := True;
      { a /recompile while people play: the admins in the game and the console hear that the
        script is back, and how long the library took to read the database }
      if AnnounceLoad then
      begin
        AnnounceLoad := False;
        if DatabaseError then
        begin
          Note := 'AntiFake Extended ' + AF_VERSION + ': script compiled, but the database could not be read';
          NoteColor := COLOR_WARN;
        end
        else
        begin
          Note := 'AntiFake Extended ' + AF_VERSION + ': script compiled, database ' +
            IntToStr(DLL_Get_Profile_Count()) + ' accounts loaded in ' + IntToStr(DLL_Get_Load_Millis()) + ' ms';
          NoteColor := COLOR_GOOD;
        end;
        WriteLn(Note);
        DLL_Log_Line(PChar(Note));
        for i := 1 to 32 do
          if SlotIsAdmin(i) then SaySlot(i, Note, NoteColor);
      end;
      { read from the database: the file is written from it at once, so the two agree again; a
        new database (no file yet) is written at once as well, so its journal works from the start }
      if DLL_Save_After_Load() then
      begin
        DLL_Start_Save_Binary(PChar(DB_FILE));
        LastSaveTime := Now;
      end;
      { the players who are here already log in from the next tick on, a few a tick }
      LoginNext := 1;
    end;
    Exit;
  end;

  if LoginNext > 0 then
  begin
    n := 0;
    while (LoginNext <= 32) and (n < PLAYERS_PER_TICK) do
    begin
      i := LoginNext;
      LoginNext := LoginNext + 1;
      if PlayerProfile[i] < 0 then
        if Players[i].Active then
          if Players[i].Human then
          begin
            n := n + 1;
            QuietLogin := True;
            ProcessLogin(i);
            QuietLogin := False;
          end;
    end;
    if LoginNext > 32 then LoginNext := 0;
  end;

  if STEAM_ENABLED then
    if Ticks mod 60 = 0 then DLL_Steam_Tick();

  { what changed goes into the journal }
  if JOURNAL_SECONDS > 0 then
    if Ticks - LastJournalTick >= JOURNAL_SECONDS * 60 then
    begin
      LastJournalTick := Ticks;
      DLL_Journal_Flush();
    end;

  { Engine = mirror: the copy for a website decides itself whether it is time; asked once a
    minute (with Engine = mysql the library exchanges on a thread of its own and this does
    nothing) }
  if MYSQL_ON then
    if Ticks mod 3600 = 0 then DLL_Mysql_Tick(False);

  { "somebody joined from ..." once the address has been located. The line goes to every
    player, so one a tick at most; the others come on the next ticks. }
  if USE_GEODATA then
  begin
    n := 0;
    for i := 1 to 32 do
      if PlayerPendingGeo[i] then
        if Players[i].Active then
          if n = 0 then
          begin
            Inc(PlayerGeoTimer[i], 1);
            GeoStr := DLL_Get_Geo_Join_String(PChar(Players[i].IP));
            if GeoStr <> 'Locating...' then
            begin
              { '' is a local address and Unknown one nobody asked for (too many lookups at
                once): nothing to say, and nothing more will come, so it is not asked again }
              if GeoStr <> '' then
                if GeoStr <> 'Unknown' then
                begin
                  Players.WriteConsole(Players[i].Name + ' joined from ' + GeoStr, COLOR_GOOD);
                  n := 1;
                end;
              PlayerPendingGeo[i] := False;
            end
            else if PlayerGeoTimer[i] > (LOCATE_TIMEOUT * 60) then PlayerPendingGeo[i] := False;
          end;
  end;

  { An answer that is ready is picked up here. Nothing in this loop waits for anything: it
    asks whether the worker has finished and whether there are lines to print, and if not it
    comes back on the next tick. }
  for i := 1 to CONSOLE_SLOT do
    if PlayerTaskPending[i] then
    begin
      { whatever the worker has already written. Whether it is done is asked first: a worker
        that finishes in between has then written everything before the lines are taken. }
      Busy := DLL_Command_Busy(i);
      Left := 0;
      if DLL_Out_Count(i) > 0 then
      begin
        if RealTick then Left := FlushLibraryLines(i) else Left := 1;
      end;
      if Busy then
      begin
        Inc(PlayerTaskTimer[i], 1);
        if PlayerTaskTimer[i] > TASK_TIMEOUT_TICKS then
        begin
          PlayerTaskPending[i] := False;
          PlayerTaskTimer[i] := 0;
          SaySlot(i, 'That took too long and was given up.', COLOR_WARN);
        end;
      end
      else if Left > 0 then
        { the rest of the answer goes out on the next ticks }
        PlayerTaskTimer[i] := 0
      else if not DLL_Command_Started_Task(i) then
      begin
        { the command answered by itself; the lines are already out }
        PlayerTaskPending[i] := False;
        PlayerTaskTimer[i] := 0;
      end
      else if DLL_Is_Task_Complete(i) then
      begin
        { the table is drawn by a worker; its lines come on the next ticks like any answer }
        PlayerTaskTimer[i] := 0;
        DLL_Render_Result(i);
      end
      else
      begin
        Inc(PlayerTaskTimer[i], 1);
        if PlayerTaskTimer[i] > TASK_TIMEOUT_TICKS then
        begin
          PlayerTaskPending[i] := False;
          PlayerTaskTimer[i] := 0;
          SaySlot(i, 'That took too long and was given up.', COLOR_WARN);
          DLL_Clear_Task_Result(i);
        end;
      end;
    end;

  { nothing below runs while the file is being written; how the write ended comes as a notice }
  if DLL_Is_Save_In_Progress() then Exit;

  { once a minute the time of every player is written down, a few players a tick }
  if Ticks mod (TimeUpdateSeconds * 60) = 0 then MinuteNext := 1;
  if MinuteNext > 0 then
  begin
    n := 0;
    while (MinuteNext <= 32) and (n < PLAYERS_PER_TICK) do
    begin
      i := MinuteNext;
      MinuteNext := MinuteNext + 1;
      if PlayerProfile[i] >= 0 then
        if Players[i].Active then
        begin
          n := n + 1;
          UpdatePlayerTime(i);
          { a nick changed in the middle of a game belongs to the account as well }
          if Players[i].Name <> PlayerLastName[i] then ProcessLogin(i);
        end;
    end;
    if MinuteNext > 32 then MinuteNext := 0;
  end;

  { The cleanup decides itself whether a day has passed and runs on a worker; asked once a
    minute, it answers True when a finished run dropped something (the report is ready). }
  if Ticks - LastMaintTick >= 60 * 60 then
  begin
    LastMaintTick := Ticks;
    if DLL_Run_Maintenance(False) then
    begin
      LogNote(DLL_Get_Maintenance_Report());
      PendingSave := True;
    end;
  end;

  { The time of every player goes in before a full write: the minute pass is started (or the
    one that runs is let finish), a few players a tick, and the write starts once it is
    through. All of them in one tick were up to 32 players' worth of calls into the library,
    each of which may wait for the lock of the profiles. }
  ElapsedSave := (Now - LastSaveTime) * 86400;
  { a clock that went back (summer time): the write is due, not an hour late }
  if ElapsedSave < 0 then ElapsedSave := SAVE_INTERVAL;
  if PendingSave and (not DatabaseError) and (ElapsedSave >= SAVE_INTERVAL) then
  begin
    if not SaveWaitsForTime then
    begin
      SaveWaitsForTime := True;
      if MinuteNext = 0 then MinuteNext := 1;
    end
    else if MinuteNext = 0 then
    begin
      SaveWaitsForTime := False;
      DLL_Start_Save_Binary(PChar(DB_FILE));
      LastSaveTime := Now;
      PendingSave := False;
    end;
  end
  else SaveWaitsForTime := False;
end;

{ An error in a handler would otherwise unload the script until the server restarts, and the
  unload itself stops the game while everything is written. It is reported (the first few, then
  every thousandth) and the script keeps running. }
function HandleScriptError(ErrorCode: TErrorType; Message, UnitName, FunctionName: string; Row, Col: Cardinal): Boolean;
begin
  Result := True;
  ScriptErrors := ScriptErrors + 1;
  if (ScriptErrors <= 20) or (ScriptErrors mod 1000 = 0) then
    LogNote('script error ' + IntToStr(ScriptErrors) + ' in ' + FunctionName + ' (' + UnitName + ' ' +
      IntToStr(Row) + ':' + IntToStr(Col) + '): ' + Message);
end;

{ ---------------- start and stop ---------------- }

{ a colour of settings.ini; the built-in one when the library does not know the name }
function ColorSetting(Key: string; Def: Integer): Integer;
begin
  Result := DLL_Get_Setting_Int(PChar(Key));
  if Result < 0 then Result := Def;
end;

procedure LoadSettings();
var
  Path, Note: string;
  Ok: Boolean;
begin
  Path := INI_FILE;
  Ok := DLL_Load_Settings(PChar(Path));
  Path := 'chatcommands';           USE_ON_SPEAK_CMD := DLL_Get_Setting_Int(PChar(Path)) = 1;
  Path := 'playercommands';         USE_ON_COMMAND := DLL_Get_Setting_Int(PChar(Path)) = 1;
  Path := 'admincommands';          USE_ON_ADMIN_COMMAND := DLL_Get_Setting_Int(PChar(Path)) = 1;
  Path := 'geoenabled';             USE_GEODATA := DLL_Get_Setting_Int(PChar(Path)) = 1;
  Path := 'publiccommands';         PUBLIC_ENABLED := DLL_Get_Setting_Int(PChar(Path)) = 1;
  Path := 'steamenabled';           STEAM_ENABLED := DLL_Get_Setting_Int(PChar(Path)) = 1;
  Path := 'engine';                 MYSQL_ENGINE := DLL_Get_Setting_Int(PChar(Path));
  MYSQL_ON := MYSQL_ENGINE > 0;
  Path := 'multiaccountnotice';     MULTI_NOTICE := DLL_Get_Setting_Int(PChar(Path)) = 1;
  Path := 'saveintervalminutes';    SAVE_INTERVAL := DLL_Get_Setting_Int(PChar(Path)) * 60;
  Path := 'tasktimeoutseconds';     TASK_TIMEOUT_TICKS := DLL_Get_Setting_Int(PChar(Path)) * 60;
  Path := 'locatetimeoutseconds';   LOCATE_TIMEOUT := DLL_Get_Setting_Int(PChar(Path));
  Path := 'journalseconds';         JOURNAL_SECONDS := DLL_Get_Setting_Int(PChar(Path));
  Path := 'linespertick';           OUT_PER_TICK := DLL_Get_Setting_Int(PChar(Path));
  Path := 'maxqueuedlines';         OUT_MAX_LINES := DLL_Get_Setting_Int(PChar(Path));
  Path := 'databasefile';
  DB_FILE := DLL_Get_Setting_Str(PChar(Path));
  if DB_FILE = '' then DB_FILE := DB_FILE_CONST;
  { the lines the script writes itself, in the colours the library writes its own }
  COLOR_INFO := ColorSetting('colorinfo', $87CEEB);
  COLOR_WARN := ColorSetting('colorwarning', $FFD700);
  COLOR_GOOD := ColorSetting('colorgood', $66CC66);
  TimeUpdateSeconds := 60;
  if OUT_PER_TICK < 1 then OUT_PER_TICK := 4;
  if OUT_MAX_LINES < 20 then OUT_MAX_LINES := 200;
  { the library says how the reading went and how many lines it refused (out of range or
    unknown, those keep the built-in value): the note in the console settings.ini promises }
  Path := 'message';
  Note := DLL_Get_Setting_Str(PChar(Path));
  if Note = '' then
    if Ok then Note := 'settings read from settings.ini'
    else Note := 'no settings.ini, using the built-in values';
  LogNote('version ' + AF_VERSION + ', ' + Note);
end;

procedure ScriptDecl();
var
  i: Byte;
begin
  Script.OnUnhandledException := @HandleScriptError;
  Game.TickThreshold := 1;
  Game.OnClockTick := @Timers;
  Game.OnJoin := @JoinGame;
  Game.OnLeave := @LeaveGame;
  Game.OnSteamAuth := @WhenSteamLoginAuth;
  for i := 0 to 5 do
    Game.Teams[i].OnJoin := @TeamJoined;
  Game.OnAdminCommand := @Event_OnAdminCommand;
  for i := 1 to 32 do
  begin
    if USE_ON_SPEAK_CMD or PUBLIC_ENABLED then Players[i].OnSpeak := @Event_OnSpeak;
    if USE_ON_COMMAND then Players[i].OnCommand := @Event_OnCommand;
  end;
end;

procedure ScriptInit();
var
  i: Integer;
begin
  AnnounceLoad := False;
  for i := 1 to 32 do
  begin
    { humans in the game while the script starts: a /recompile, not the start of the server }
    if Players[i].Active then
      if Players[i].Human then AnnounceLoad := True;
    PlayerProfile[i] := -1;
    PlayerLoginFlags[i] := 0;
    PlayerJoinTime[i] := 0;
    PlayerLastTimeUpdate[i] := 0;
    PlayerTeam[i] := 0;
    PlayerPendingGeo[i] := False;
    PlayerGeoTimer[i] := 0;
    PlayerLastName[i] := '';
    PlayerTaskPending[i] := False;
    PlayerTaskTimer[i] := 0;
    PlayerSteam[i] := '';
    PlayerNoticeFor[i] := -1;
    PendingCmd[i] := '';
    PendingParams[i] := '';
    PendingCmd2[i] := '';
    PendingParams2[i] := '';
    OutDropped[i] := 0;
    OutQueue[i] := File.CreateStringList();
  end;
  MinuteNext := 0;
  LoginNext := 0;
  QuietLogin := False;
  ScriptErrors := 0;
  LastRealTick := 0;
  PlayerTaskPending[CONSOLE_SLOT] := False;
  PlayerTaskTimer[CONSOLE_SLOT] := 0;
  DatabaseLoaded := False;
  DatabaseError := False;
  PendingSave := False;
  SaveWaitsForTime := False;
  LastSaveTime := Now;
  LastMaintTick := 0;
  LastJournalTick := 0;
end;

procedure ScriptShutdown();
var
  i: Integer;
begin
  for i := 1 to 32 do
    if Players[i].Active then
      if PlayerProfile[i] >= 0 then UpdatePlayerTime(i);
  { Nothing is written over the file before it has been read completely: the memory would
    still be empty. With the journal on, what changed since the last full write goes into it
    and the next start puts it on top of the file, so a /recompile does not stop the game for
    the seconds a full write takes; without the journal (or when it could not take all of it)
    the whole database is written. }
  if DatabaseLoaded and not DatabaseError then
    if not DLL_Journal_Final() then
      DLL_Start_Save_Binary(PChar(DB_FILE));
  DLL_Cleanup();
  { the lists are not the server's: a /recompile would leave them behind }
  for i := 1 to 32 do
    if OutQueue[i] <> nil then
    begin
      OutQueue[i].Free;
      OutQueue[i] := nil;
    end;
end;

initialization
begin
  { a failure in one of these two must not keep the events and the database away: the script
    would stay loaded and silent }
  try
    LoadSettings();
  except
    LogNote('the settings could not be read: ' + ExceptionToString(ExceptionType, ExceptionParam));
  end;
  try
    ScriptInit();
  except
    LogNote('the start failed: ' + ExceptionToString(ExceptionType, ExceptionParam));
  end;
  ScriptDecl();
  { the library reads the database on a thread of its own; with Engine = mirror it reads the
    newer of the copy in MySQL and the file, with Engine = mysql the database of every server.
    The timer picks the result up. }
  DLL_Start_Load_Binary(PChar(DB_FILE));
end;

finalization
begin
  ScriptShutdown();
end;

end.
