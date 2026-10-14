{ ---------------------------------------------------------------------------------------
  AntiFake Extended - the plain calls the script may make.

  Nothing may leave an export as an exception: the script would be unloaded with the server
  still running. Every export catches what it may raise, answers empty (or "no", or -1) and
  names itself in the log (ExportFailed).
  --------------------------------------------------------------------------------------- }

{ Engine = mysql with a database named: the account numbers belong to the shared database, also
  in a run that started without it (the database did not answer and antifake.shared was missing),
  so no account is emptied by the cleanup and no emptied number is given to somebody else }
function SharedEngine: Boolean;
begin
  Result := SharedOn or ((Settings.MysqlEngine = ENGINE_MYSQL) and (Trim(Settings.MysqlHost) <> ''));
end;

{ The value of a line without the comment behind it. The README writes the settings as
  "Engine = mirror   ; local | mirror | mysql", and that comment used to become part of the
  value: an Engine nobody knew, a SteamApiKey of "; empty = ...". A comment starts at a ';'
  that follows a space or a tab, or at a ';' right after the '='. A value that has to keep
  such a ';' (a password) is written in double quotes, and the closing quote ends it. }
function SettingValueOf(const Raw: string): string;
var
  p: Integer;
begin
  Result := Trim(Raw);
  if Result = '' then Exit;
  if Result[1] = '"' then
  begin
    p := Pos('"', Copy(Result, 2, Length(Result)));
    if p > 0 then Result := Copy(Result, 1, p + 1);
    Exit;
  end;
  if Result[1] = ';' then
  begin
    Result := '';
    Exit;
  end;
  for p := 2 to Length(Result) do
    if (Result[p] = ';') and ((Result[p - 1] = ' ') or (Result[p - 1] = #9)) then
    begin
      Result := TrimRight(Copy(Result, 1, p - 1));
      Exit;
    end;
end;

function Load_Settings(Path: PChar): Boolean; cdecl;
var
  L: TStringList;
  i, Eq, Bad, Unknown: Integer;
  Line, Key, Val: string;
begin
  Result := False;
  try
    { the script reads its settings first thing, on the server's own thread }
    GameThread := GetCurrentThreadId;
    SetDefaultSettings;
    Bad := 0;
    Unknown := 0;
    if (Path = nil) or (string(Path) = '') or not FileExists(string(Path)) then
    begin
      Settings.Message := 'no settings file, using the built-in values';
      Exit;
    end;
    L := TStringList.Create;
    try
      try
        L.LoadFromFile(string(Path));
      except
        Settings.Message := 'the settings file could not be read';
        Exit;
      end;
      for i := 0 to L.Count - 1 do
      begin
        Line := Trim(L[i]);
        if (Line = '') or (Line[1] = ';') or (Line[1] = '#') or (Line[1] = '[') then Continue;
        Eq := Pos('=', Line);
        if Eq < 2 then Continue;
        Key := Copy(Line, 1, Eq - 1);
        Val := SettingValueOf(Copy(Line, Eq + 1, Length(Line)));
        ApplySetting(Key, Val, Bad, Unknown);
      end;
    finally
      L.Free;
    end;
    Settings.Loaded := True;
    Settings.Message := Format('settings read (%d out of range, %d unknown)', [Bad, Unknown]);
    Result := True;
  except
    ExportFailed('Load_Settings');
    Result := False;
  end;
end;

{ the script asks for the values it needs itself; the password and the Steam key are never
  handed out }
function Get_Setting_Int(Name: PChar): Integer; cdecl;
var
  K: string;
begin
  Result := -1;
  try
    K := LowerCase(Trim(string(Name)));
    if K = 'saveintervalminutes' then Result := Settings.SaveMinutes
    else if K = 'resultsperpage' then Result := Settings.ResultsPerPage
    else if K = 'tasktimeoutseconds' then Result := Settings.TaskTimeoutSeconds
    else if K = 'locatetimeoutseconds' then Result := Settings.LocateTimeoutSeconds
    else if K = 'admincommands' then Result := Ord(Settings.AdminCommands)
    else if K = 'playercommands' then Result := Ord(Settings.PlayerCommands)
    else if K = 'chatcommands' then Result := Ord(Settings.ChatCommands)
    else if K = 'geoenabled' then Result := Ord(Settings.GeoEnabled)
    else if K = 'maxlookups' then Result := Settings.GeoMaxLookups
    else if K = 'retryseconds' then Result := Settings.GeoRetrySeconds
    else if K = 'timeoutms' then Result := Settings.GeoTimeoutMs
    else if K = 'refreshdays' then Result := Settings.GeoRefreshDays
    else if K = 'geokeepdays' then Result := Settings.GeoKeepDays
    else if K = 'geotextplain' then Result := Ord(Settings.GeoTextPlain)
    else if K = 'visitkeepcount' then Result := Settings.VisitKeepCount
    else if K = 'visitkeepdays' then Result := Settings.VisitKeepDays
    else if K = 'visitcooldownminutes' then Result := Settings.VisitCooldownMinutes
    else if K = 'lastvisitsrows' then Result := Settings.LastVisitsRows
    else if K = 'commandcooldownseconds' then Result := Settings.CommandCooldownSeconds
    else if K = 'heavycooldownseconds' then Result := Settings.HeavyCooldownSeconds
    else if K = 'maxlistspersecond' then Result := Settings.MaxListsPerSecond
    else if K = 'publiccommands' then Result := Ord(Settings.PublicCommands)
    else if K = 'trace' then Result := Ord(Settings.Trace)
    else if K = 'linespertick' then Result := Settings.LinesPerTick
    else if K = 'maxqueuedlines' then Result := Settings.MaxQueuedLines
    else if K = 'activedays' then Result := Settings.ActiveDays
    else if K = 'journalseconds' then Result := Settings.JournalSeconds
    else if K = 'engine' then Result := Settings.MysqlEngine
    else if K = 'mysqlminutes' then Result := Settings.MysqlMinutes
    { the numbers of the other keys too, so every one of them can be asked for }
    else if K = 'mysqlport' then Result := Settings.MysqlPort
    else if K = 'mysqlserverid' then Result := Settings.MysqlServerId
    else if K = 'syncseconds' then Result := Settings.SyncSeconds
    else if K = 'mysqlcodepage' then Result := Settings.MysqlCodepage
    else if K = 'sharedsearch' then Result := Ord(Settings.SharedSearch)
    else if K = 'sharedlinks' then Result := Ord(Settings.SharedLinks)
    else if K = 'colormain' then Result := Settings.ColMain
    else if K = 'colorinfo' then Result := Settings.ColInfo
    else if K = 'colordata' then Result := Settings.ColData
    else if K = 'colorwarning' then Result := Settings.ColWarn
    else if K = 'colorerror' then Result := Settings.ColError
    else if K = 'colorgood' then Result := Settings.ColGood
    else if K = 'backuphours' then Result := Settings.BackupHours
    else if K = 'backupkeep' then Result := Settings.BackupKeep
    else if K = 'playervisits' then Result := Settings.CmdVisits
    else if K = 'playervisitstats' then Result := Settings.CmdVisitStats
    else if K = 'playervisitsof' then Result := Settings.CmdVisitsOf
    else if K = 'playerstatus' then Result := Settings.CmdStatus
    else if K = 'playernames' then Result := Settings.CmdNames
    else if K = 'playersteam' then Result := Settings.CmdStopSteam
    else if K = 'playerlocate' then Result := Settings.CmdLocate
    else if K = 'playerprofiles' then Result := Settings.CmdProfiles
    else if K = 'playersearch' then Result := Settings.CmdSearch
    else if K = 'playermulti' then Result := Settings.CmdMulti
    else if K = 'playerhelp' then Result := Settings.CmdHelp
    else if K = 'playerlastvisitors' then Result := Settings.CmdLastVisitors
    else if K = 'multiaccountnotice' then Result := Ord(Settings.MultiNotice)
    else if K = 'multiaccountminlinks' then Result := Settings.MultiMinLinks
    else if K = 'multiaccountbyaddress' then Result := Ord(Settings.MultiByAddress)
    else if K = 'multiaccountmaxlinks' then Result := Settings.MultiMaxLinks
    else if K = 'multiaccountreportscore' then Result := Settings.MultiReportScore
    else if K = 'multiaccountminsignals' then Result := Settings.MultiMinSignals
    else if K = 'multiaccountmaxshared' then Result := Settings.MultiMaxShared
    else if K = 'multiaccountmaxsharedcomputer' then Result := Settings.MultiMaxSharedPC
    else if K = 'steamenabled' then Result := Ord(Settings.SteamKey <> '')
    else if K = 'steamrefreshdays' then Result := Settings.SteamRefreshDays
    else if K = 'steambatchsize' then Result := Settings.SteamBatch
    else if K = 'steamtimeoutseconds' then Result := Settings.CurlTimeoutSeconds
    else if K = 'addressdays' then Result := Settings.AddressDays
    else if K = 'namedays' then Result := Settings.NameDays
    else if K = 'hwiddays' then Result := Settings.HwidDays
    else if K = 'maxaddresses' then Result := Settings.MaxAddresses
    else if K = 'profiledays' then Result := Settings.ProfileDays
    else if K = 'loaded' then Result := Ord(Settings.Loaded)
    else Result := -1;
  except
    ExportFailed('Get_Setting_Int');
    Result := -1;
  end;
end;

{ exported for the tests, and useful for any text that comes from the web }
function Utf8_To_Console(Text: PChar): PChar; cdecl;
begin
  try
    TempStringResult := Utf8ToConsole(string(Text));
  except
    ExportFailed('Utf8_To_Console');
    TempStringResult := '';
  end;
  Result := PChar(TempStringResult);
end;

function Get_Setting_Str(Name: PChar): PChar; cdecl;
var
  K: string;
begin
  try
    K := LowerCase(Trim(string(Name)));
    if K = 'databasefile' then TempStringResult := Settings.DatabaseFile
    else if K = 'message' then TempStringResult := Settings.Message
    else TempStringResult := '';
  except
    ExportFailed('Get_Setting_Str');
    TempStringResult := '';
  end;
  Result := PChar(TempStringResult);
end;

var
  { a profile emptied by a rollback or by the cleanup may wait for the next new player; when
    the last look found none, the next new player does not read the whole list for one. The
    caller holds ProfilesCritSec, except Start_Load_Binary before its thread exists. }
  EmptyScanNeeded: Boolean = True;
  { how long the last reading of the database took, from Start_Load_Binary to the end of its
    thread, for the line the admins get after a /recompile: milliseconds, -1 while it runs. 32
    bits, so the game thread never reads half of what the load thread writes. }
  LoadStartMicro: Int64 = 0;
  LoadMillis: LongInt = -1;

type
  { the reading of af_tasks.inc, timed to its very end }
  TTimedLoadThread = class(TLoadThread)
  protected
    procedure Execute; override;
  end;

procedure TTimedLoadThread.Execute;
begin
  try
    inherited Execute;
  finally
    LoadMillis := LongInt((MicroClock - LoadStartMicro) div 1000);
  end;
end;

procedure Start_Load_Binary(FilePath: PChar); cdecl;
begin
  try
    if LoadInProgress then Exit;
    LoadInProgress := True; LoadComplete := False; LoadError := False;
    { a database read now may bring profiles that were emptied before }
    EmptyScanNeeded := True;
    { the last load has finished its work (LoadInProgress went down at its end): this waits
      for the few lines of it that are left }
    if LoadThread <> nil then begin LoadThread.WaitFor; FreeAndNil(LoadThread); end;
    LoadMillis := -1;
    LoadStartMicro := MicroClock;
    LoadThread := TTimedLoadThread.Create(string(FilePath), Settings.MysqlEngine = ENGINE_MIRROR);
    LoadThread.Start;
  except
    ExportFailed('Start_Load_Binary');
    { no thread: the script is told the database could not be read, instead of waiting for
      it for ever (a thread that was made but not started is freed) }
    if LoadThread <> nil then
      try LoadThread.Free; except end;
    LoadThread := nil;
    LoadError := True;
    LoadComplete := True;
    LoadInProgress := False;
  end;
end;

function Is_Load_Complete(): Boolean; cdecl;
begin
  try Result := LoadComplete; except Result := False; end;
end;

function Is_Load_Error(): Boolean; cdecl;
begin
  try Result := LoadError; except Result := False; end;
end;

function Is_Load_In_Progress(): Boolean; cdecl;
begin
  try Result := LoadInProgress; except Result := False; end;
end;

{ How long the last reading of the database took, in milliseconds: the script says it to the
  admins after a /recompile. Asked the moment the load says it is complete, before its thread
  has noted the end, it is the time until now - the same to a fraction of a millisecond. -1 when
  nothing was read yet. }
function Get_Load_Millis(): Integer; cdecl;
var
  T: Int64;
begin
  Result := -1;
  try
    T := LoadMillis;
    if (T < 0) and (LoadStartMicro > 0) then T := (MicroClock - LoadStartMicro) div 1000;
    if T > MaxInt then T := MaxInt;
    Result := Integer(T);
  except
    ExportFailed('Get_Load_Millis');
    Result := -1;
  end;
end;

{ the list of exports is in af_main.pas; this one came with the /recompile notice }
exports
  Get_Load_Millis;

function Get_Profile_Count(): Integer; cdecl;
begin
  Result := 0;
  try
    LockProfiles;
    try Result := ProfileCount; finally UnlockProfiles; end;
  except
    ExportFailed('Get_Profile_Count');
  end;
end;

function Get_Profile_Active_Name(Index: Integer): PChar; cdecl;
begin
  try
    LockProfiles;
    try TempStringResult := GetProfileActiveName(Index); finally UnlockProfiles; end;
  except
    ExportFailed('Get_Profile_Active_Name');
    TempStringResult := '';
  end;
  Result := PChar(TempStringResult);
end;

function Get_Profile_Active_Steam(Index: Integer): PChar; cdecl;
begin
  try
    LockProfiles;
    try TempStringResult := GetProfileActiveSteam(Index); finally UnlockProfiles; end;
  except
    ExportFailed('Get_Profile_Active_Steam');
    TempStringResult := '';
  end;
  Result := PChar(TempStringResult);
end;

function Get_Profile_Active_HWID(Index: Integer): PChar; cdecl;
begin
  try
    LockProfiles;
    try TempStringResult := GetProfileActiveHWID(Index); finally UnlockProfiles; end;
  except
    ExportFailed('Get_Profile_Active_HWID');
    TempStringResult := '';
  end;
  Result := PChar(TempStringResult);
end;

function Get_Profile_Register_Time(Index: Integer): PChar; cdecl;
begin
  try
    LockProfiles;
    try
      if (Index >= 0) and (Index < ProfileCount) then
        TempStringResult := FloatToStr(Profiles[Index].Profile.RegisterTime)
      else TempStringResult := '0';
    finally UnlockProfiles; end;
  except
    ExportFailed('Get_Profile_Register_Time');
    TempStringResult := '0';
  end;
  Result := PChar(TempStringResult);
end;

function Get_Profile_Last_Played(Index: Integer): PChar; cdecl;
begin
  try
    LockProfiles;
    try
      if (Index >= 0) and (Index < ProfileCount) then
        TempStringResult := FloatToStr(Profiles[Index].Profile.LastPlayedTime)
      else TempStringResult := '0';
    finally UnlockProfiles; end;
  except
    ExportFailed('Get_Profile_Last_Played');
    TempStringResult := '0';
  end;
  Result := PChar(TempStringResult);
end;

function Get_Profile_Time_Played(Index: Integer): PChar; cdecl;
begin
  try
    LockProfiles;
    try
      if (Index >= 0) and (Index < ProfileCount) then
        TempStringResult := IntToStr(Profiles[Index].Profile.TimePlayed)
      else TempStringResult := '0';
    finally UnlockProfiles; end;
  except
    ExportFailed('Get_Profile_Time_Played');
    TempStringResult := '0';
  end;
  Result := PChar(TempStringResult);
end;

function Get_Profile_Time_Spectator(Index: Integer): PChar; cdecl;
begin
  try
    LockProfiles;
    try
      if (Index >= 0) and (Index < ProfileCount) then
        TempStringResult := IntToStr(Profiles[Index].Profile.TimeOnSpectator)
      else TempStringResult := '0';
    finally UnlockProfiles; end;
  except
    ExportFailed('Get_Profile_Time_Spectator');
    TempStringResult := '0';
  end;
  Result := PChar(TempStringResult);
end;

{ The play time goes into the journal like a login, so a server that stops without a full write
  loses a minute of it at most. The game thread calls this every minute for every player, and
  at a change of team and at a leave: it never waits more than a moment for the lock. When a
  worker holds the profiles longer, the seconds wait in a short list (QueuePlayTime) and go in
  as soon as anybody takes the profiles - nothing is lost and the script need not ask again.
  Only when 64 accounts are waiting at once does it wait for the lock after all. }
procedure Add_Profile_Time(Index, TimePlayed, TimeSpectator: Integer); cdecl;
begin
  try
    if Index < 0 then Exit;
    if not GrabProfiles(20) then
    begin
      if QueuePlayTime(Index, TimePlayed, TimeSpectator) then Exit;
      LockProfiles;
    end;
    try
      ApplyPlayTime(Index, TimePlayed, TimeSpectator);
    finally UnlockProfiles; end;
  except
    ExportFailed('Add_Profile_Time');
  end;
end;

{ by the place of the entry in its list; the script uses Update_Other_Time_Value }
procedure Update_Other_Time(Index, Kind, SubIndex, TimePlayed, TimeSpectator: Integer); cdecl;
var
  Hit: Boolean;
begin
  try
    LockProfiles;
    try
      if (Index < 0) or (Index >= ProfileCount) then Exit;
      SharedTouch(Index);
      Hit := False;
      case Kind of
        KIND_NAME: if (SubIndex >= 0) and (SubIndex < Length(Profiles[Index].OtherNames)) then begin Inc(Profiles[Index].OtherNames[SubIndex].TimePlayed, TimePlayed); Inc(Profiles[Index].OtherNames[SubIndex].TimeOnSpectator, TimeSpectator); Profiles[Index].OtherNames[SubIndex].LastUsedDate := Now; Hit := True; end;
        KIND_STEAM: if (SubIndex >= 0) and (SubIndex < Length(Profiles[Index].OtherSteams)) then begin Inc(Profiles[Index].OtherSteams[SubIndex].TimePlayed, TimePlayed); Inc(Profiles[Index].OtherSteams[SubIndex].TimeOnSpectator, TimeSpectator); Profiles[Index].OtherSteams[SubIndex].LastUsedDate := Now; Hit := True; end;
        KIND_HWID: if (SubIndex >= 0) and (SubIndex < Length(Profiles[Index].OtherHWIDs)) then begin Inc(Profiles[Index].OtherHWIDs[SubIndex].TimePlayed, TimePlayed); Inc(Profiles[Index].OtherHWIDs[SubIndex].TimeOnSpectator, TimeSpectator); Profiles[Index].OtherHWIDs[SubIndex].LastUsedDate := Now; Hit := True; end;
        KIND_IP: if (SubIndex >= 0) and (SubIndex < Length(Profiles[Index].OtherIPs)) then begin Inc(Profiles[Index].OtherIPs[SubIndex].TimePlayed, TimePlayed); Inc(Profiles[Index].OtherIPs[SubIndex].TimeOnSpectator, TimeSpectator); Profiles[Index].OtherIPs[SubIndex].LastUsedDate := Now; Hit := True; end;
      end;
      { a place that is not in the list changed nothing, and nothing goes into the journal }
      if Hit then MarkDirty(Index);
    finally UnlockProfiles; end;
  except
    ExportFailed('Update_Other_Time');
  end;
end;

{ the same, but the entry is found by what it says instead of by its place in the list: the
  list moves when something is rolled back or thrown away by the cleanup, and the play time
  used to land on a different name, computer or address }
{ called from the game thread every minute: it may give up rather than wait }
procedure Update_Other_Time_Value(Index, Kind: Integer; Value: PChar; TimePlayed, TimeSpectator: Integer); cdecl;
var
  V: string;

  procedure Touch(var Arr: TBinaryOtherDataArray);
  var
    j: Integer;
  begin
    for j := 0 to High(Arr) do
      if GetOtherDataString(Arr[j]) = V then
      begin
        Inc(Arr[j].TimePlayed, TimePlayed);
        Inc(Arr[j].TimeOnSpectator, TimeSpectator);
        Arr[j].LastUsedDate := Now;
        Exit;
      end;
  end;

begin
  try
    { an entry holds MAX_STRING_LEN letters: a longer value is found by what was kept of it }
    V := Copy(string(Value), 1, MAX_STRING_LEN);
    if V = '' then Exit;
    if not GrabProfiles(20) then Exit;
    try
      if (Index < 0) or (Index >= ProfileCount) then Exit;
      SharedTouch(Index);
      case Kind of
        KIND_NAME: Touch(Profiles[Index].OtherNames);
        KIND_STEAM: Touch(Profiles[Index].OtherSteams);
        KIND_HWID: Touch(Profiles[Index].OtherHWIDs);
        KIND_IP: Touch(Profiles[Index].OtherIPs);
      end;
      MarkDirty(Index);
    finally UnlockProfiles; end;
  except
    ExportFailed('Update_Other_Time_Value');
  end;
end;

{ the newest account whose name holds the text. It reads every account; nothing in the script
  asks for it (the tests do), and the commands find names on their workers. }
function Find_Profile_By_Name(Name: PChar): Integer; cdecl;
var
  i: Integer;
  S: string;
begin
  Result := -1;
  try
    S := string(Name);
    if S = '' then Exit;
    LockProfiles;
    try
      for i := ProfileCount - 1 downto 0 do
        if ContainsText(GetProfileActiveName(i), S) then
        begin
          Result := i;
          Exit;
        end;
    finally UnlockProfiles; end;
  except
    ExportFailed('Find_Profile_By_Name');
    Result := -1;
  end;
end;

function Find_Profile_By_Steam(SteamID: PChar): Integer; cdecl;
var
  P: Integer;
  S: string;
begin
  Result := -1;
  try
    S := Copy(string(SteamID), 1, MAX_STRING_LEN);
    if S = '' then Exit;
    LockProfiles;
    try
      P := IndexFirst(SteamIndex, S);
      if P >= 0 then Result := Integer(PtrInt(SteamIndex.Objects[P]));
      if (Result < 0) or (Result >= ProfileCount) then Result := -1;
    finally UnlockProfiles; end;
  except
    ExportFailed('Find_Profile_By_Steam');
    Result := -1;
  end;
end;

function Find_Profile_By_HWID(HWID: PChar): Integer; cdecl;
var
  P: Integer;
  S: string;
begin
  Result := -1;
  try
    S := Copy(string(HWID), 1, MAX_STRING_LEN);
    if S = '' then Exit;
    LockProfiles;
    try
      P := IndexFirst(HwidIndex, S);
      if P >= 0 then Result := Integer(PtrInt(HwidIndex.Objects[P]));
      if (Result < 0) or (Result >= ProfileCount) then Result := -1;
    finally UnlockProfiles; end;
  except
    ExportFailed('Find_Profile_By_HWID');
    Result := -1;
  end;
end;

{ Which account a player is, on every join and every change of name, on the game thread: the
  Steam account, or else a computer that account played from - a computer of an account
  without Steam when the player has Steam, since an account with a Steam account of its own
  belongs to somebody else. Nothing known: a new account. The number returned is the place of
  the account in the list, which never moves while the server runs. }
function Login_Player(Name, SteamID, HWID, IP: PChar; IncLoginCount: Boolean): Integer; cdecl;
var
  i, j, FoundIdx: Integer;
  SName, SSteam, SHWID, SIP: string;
  ShouldInc: Boolean;
begin
  Result := -1;
  try
    { an entry keeps MAX_STRING_LEN letters: a longer value was stored cut, never matched the
      whole one again and made a new entry at every join }
    SName := Copy(string(Name), 1, MAX_STRING_LEN);
    SSteam := Copy(string(SteamID), 1, MAX_STRING_LEN);
    SHWID := Copy(string(HWID), 1, MAX_STRING_LEN);
    SIP := Copy(string(IP), 1, MAX_STRING_LEN);
    if Settings.Trace then TraceStep('Login_Player in: ' + SName + ' hwid ' + SHWID);
    if (SSteam = 'S0') or (SSteam = '0') then SSteam := '';

    LoginResult_Flags := 0; LoginResult_NameIdx := -1; LoginResult_SteamIdx := -1; LoginResult_HWIDIdx := -1; LoginResult_IPIdx := -1;
    LockProfiles;
    try
      { the Steam account and the computer are looked up in the sorted indexes: a join used to
        read every profile and every entry of it, on the game thread and under the lock }
      FoundIdx := -1;
      if SSteam <> '' then begin
        j := IndexFirst(SteamIndex, SSteam);
        if j >= 0 then begin
          i := Integer(PtrInt(SteamIndex.Objects[j]));
          if (i >= 0) and (i < ProfileCount) then FoundIdx := i;
        end;
      end;

      { an empty HWID matches every profile that has one, so it must not be searched for }
      if (FoundIdx < 0) and (SHWID <> '') then begin
        j := IndexFirst(HwidIndex, SHWID);
        while (j >= 0) and (j < HwidIndex.Count) and (HwidIndex[j] = SHWID) do begin
          i := Integer(PtrInt(HwidIndex.Objects[j]));
          if (i >= 0) and (i < ProfileCount) then begin
            if SSteam <> '' then begin
               { a profile that has a Steam account of its own belongs to somebody else }
               if Length(Profiles[i].OtherSteams) = 0 then FoundIdx := i;
            end else
               FoundIdx := i;
            if FoundIdx >= 0 then Break;
          end;
          Inc(j);
        end;
      end;

      if FoundIdx < 0 then begin
        { a profile emptied by a rollback or by the cleanup is reused before the list grows -
          not with Engine = mysql, where its number may belong to an account of the shared
          database. The list is read only while there may be one. }
        if not SharedEngine and EmptyScanNeeded then
        begin
          for i := 0 to ProfileCount - 1 do
            if ProfileIsEmpty(i) then begin FoundIdx := i; Break; end;
          if FoundIdx < 0 then EmptyScanNeeded := False;
        end;
        if FoundIdx < 0 then begin
          Inc(ProfileCount);
          { the list grows by a quarter: by 100 it was moved (megabytes of it, on the game
            thread) at every hundredth new player }
          if ProfileCount > Length(Profiles) then
            SetLength(Profiles, ProfileCount + ProfileCount div 4 + 100);
          FoundIdx := ProfileCount - 1;
        end;
        { a new account starts empty: what a slot held before (an emptied account, or an older
          database read over this one) must not come with it - its visits, its Steam data }
        FillChar(Profiles[FoundIdx].Profile, SizeOf(TBinaryProfile), 0);
        SetLength(Profiles[FoundIdx].OtherNames, 0); SetLength(Profiles[FoundIdx].OtherSteams, 0);
        SetLength(Profiles[FoundIdx].OtherHWIDs, 0); SetLength(Profiles[FoundIdx].OtherIPs, 0);
        SetLength(Profiles[FoundIdx].Visits, 0);
        FillChar(Profiles[FoundIdx].Steam, SizeOf(TSteamInfo), 0);
        Profiles[FoundIdx].Profile.RegisterTime := Now;
        LoginResult_Flags := LoginResult_Flags or 16;
      end;
      SharedTouch(FoundIdx);

      { only a profile that really has no date gets one: a broken date is left alone, so it does
        not quietly become "registered today" }
      if (Profiles[FoundIdx].Profile.RegisterTime = 0) and (Profiles[FoundIdx].Profile.LastPlayedTime = 0) then
        Profiles[FoundIdx].Profile.RegisterTime := Now;

      Profiles[FoundIdx].Profile.LastPlayedTime := Now;

      { empty values are not data: they would make every later player with an empty field
        look like the same account }
      if SName <> '' then begin
        ShouldInc := IncLoginCount or (SName <> GetProfileActiveName(FoundIdx));
        LoginResult_NameIdx := UpdateOrAddOther(FoundIdx, KIND_NAME, SName, ShouldInc);
      end;

      if SSteam <> '' then begin
        ShouldInc := IncLoginCount or (SSteam <> GetProfileActiveSteam(FoundIdx));
        LoginResult_SteamIdx := UpdateOrAddOther(FoundIdx, KIND_STEAM, SSteam, ShouldInc);
      end;

      if SHWID <> '' then begin
        ShouldInc := IncLoginCount or (SHWID <> GetProfileActiveHWID(FoundIdx));
        LoginResult_HWIDIdx := UpdateOrAddOther(FoundIdx, KIND_HWID, SHWID, ShouldInc);
      end;

      if SIP <> '' then begin
        ShouldInc := IncLoginCount or (SIP <> GetProfileActiveIP(FoundIdx));
        LoginResult_IPIdx := UpdateOrAddOther(FoundIdx, KIND_IP, SIP, ShouldInc);
      end;

      if SName <> '' then SetProfileActiveName(FoundIdx, SName);
      if SSteam <> '' then SetProfileActiveSteam(FoundIdx, SSteam);
      if SHWID <> '' then SetProfileActiveHWID(FoundIdx, SHWID);
      if SIP <> '' then SetProfileActiveIP(FoundIdx, SIP);

      Result := FoundIdx;
    finally UnlockProfiles; end;
    if Settings.Trace then TraceStep('Login_Player out');
  except
    ExportFailed('Login_Player');
    Result := -1;
  end;
end;

{ what the last Login_Player did: 0-3 the place of the name, Steam id, computer and address in
  their lists, 4 the flags (1 new name, 2 new Steam id, 4 new computer, 8 new address, 16 new
  account) }
function Get_Login_Result(Field: Integer): Integer; cdecl;
begin
  try
    case Field of
      0: Result := LoginResult_NameIdx;
      1: Result := LoginResult_SteamIdx;
      2: Result := LoginResult_HWIDIdx;
      3: Result := LoginResult_IPIdx;
      4: Result := LoginResult_Flags;
    else Result := -1;
    end;
  except
    Result := -1;
  end;
end;

procedure RemoveOtherData(var Arr: TBinaryOtherDataArray; Index: Integer);
var k: Integer;
begin
  if (Index < 0) or (Index >= Length(Arr)) then Exit;
  for k := Index to Length(Arr) - 2 do
    Arr[k] := Arr[k+1];
  SetLength(Arr, Length(Arr) - 1);
end;

{ what the admins decided about an account that is gone, or that a new player is about to
  take, means nothing any more. With Engine = mysql an emptied number is never given to
  anybody else, and the decisions belong to the shared database. The caller holds
  ProfilesCritSec. }
procedure ForgetVerdictsOf(Index: Integer);
var
  k, n: Integer;
begin
  if SharedEngine then Exit;
  n := 0;
  for k := 0 to VerdictCount - 1 do
    if (Verdicts[k].A <> Index) and (Verdicts[k].B <> Index) then
    begin
      if n <> k then Verdicts[n] := Verdicts[k];
      Inc(n);
    end;
  if n = VerdictCount then Exit;
  VerdictCount := n;
  RebuildVerdictIndex;
  VerdictsDirty := True;
end;

{ the entry of this list used last, for the active name, computer or address after the one
  that was active went; '' when the list is empty }
function NewestOtherValue(const Arr: TBinaryOtherDataArray): string;
var
  k, Best: Integer;
begin
  Result := '';
  Best := -1;
  for k := 0 to High(Arr) do
    if (Best < 0) or (Arr[k].LastUsedDate >= Arr[Best].LastUsedDate) then Best := k;
  if Best >= 0 then Result := GetOtherDataString(Arr[Best]);
end;

{ The script asks for this when a join turned out to be another account (a correction): what
  that join added to the old account goes again - with Flags as Login_Player gave them, 1 the
  name, 4 the computer, 8 the address - and what it only counted is counted back. A Steam id is
  never taken back (the script does not pass it). }
procedure Rollback_Profile_Changes(ProfileIdx: Integer; Name, HWID, IP: PChar; Flags: Integer); cdecl;
var
  i, FoundIdx: Integer;
  SName, SHWID, SIP, Key: string;
begin
  try
    SName := Copy(string(Name), 1, MAX_STRING_LEN);
    SHWID := Copy(string(HWID), 1, MAX_STRING_LEN);
    SIP := Copy(string(IP), 1, MAX_STRING_LEN);
    if Settings.Trace then TraceStep('Rollback in: profile ' + IntToStr(ProfileIdx));

    LockProfiles;
    try
      if (ProfileIdx < 0) or (ProfileIdx >= ProfileCount) then Exit;
      SharedTouch(ProfileIdx);

      FoundIdx := -1;
      if SName <> '' then
        for i := 0 to Length(Profiles[ProfileIdx].OtherNames) - 1 do
          if GetOtherDataString(Profiles[ProfileIdx].OtherNames[i]) = SName then begin FoundIdx := i; Break; end;

      if FoundIdx >= 0 then begin
         if (Flags and 1) <> 0 then begin
            RemoveOtherData(Profiles[ProfileIdx].OtherNames, FoundIdx);
            MarkFirstUsed(Profiles[ProfileIdx].OtherNames);
         end else begin
            if Profiles[ProfileIdx].OtherNames[FoundIdx].LoginCount > 0 then
               Dec(Profiles[ProfileIdx].OtherNames[FoundIdx].LoginCount);
         end;
      end;

      FoundIdx := -1;
      if SHWID <> '' then
        for i := 0 to Length(Profiles[ProfileIdx].OtherHWIDs) - 1 do
          if GetOtherDataString(Profiles[ProfileIdx].OtherHWIDs[i]) = SHWID then begin FoundIdx := i; Break; end;

      if FoundIdx >= 0 then begin
         if (Flags and 4) <> 0 then begin
            RemoveOtherData(Profiles[ProfileIdx].OtherHWIDs, FoundIdx);
            MarkFirstUsed(Profiles[ProfileIdx].OtherHWIDs);
            IndexRemove(HwidIndex, SHWID, ProfileIdx);
            { the account showed a computer it no longer has }
            if GetProfileActiveHWID(ProfileIdx) = Copy(SHWID, 1, 31) then
              SetProfileActiveHWID(ProfileIdx, NewestOtherValue(Profiles[ProfileIdx].OtherHWIDs));
         end else begin
            if Profiles[ProfileIdx].OtherHWIDs[FoundIdx].LoginCount > 0 then
               Dec(Profiles[ProfileIdx].OtherHWIDs[FoundIdx].LoginCount);
         end;
      end;

      FoundIdx := -1;
      if SIP <> '' then
        for i := 0 to Length(Profiles[ProfileIdx].OtherIPs) - 1 do
          if GetOtherDataString(Profiles[ProfileIdx].OtherIPs[i]) = SIP then begin FoundIdx := i; Break; end;

      if FoundIdx >= 0 then begin
         if (Flags and 8) <> 0 then begin
            RemoveOtherData(Profiles[ProfileIdx].OtherIPs, FoundIdx);
            MarkFirstUsed(Profiles[ProfileIdx].OtherIPs);
            IndexRemove(IpIndex, SIP, ProfileIdx);
            IndexRemove(Net24Index, Net24Of(SIP), ProfileIdx);
            { and an address it no longer has }
            if GetProfileActiveIP(ProfileIdx) = Copy(SIP, 1, 23) then
              SetProfileActiveIP(ProfileIdx, NewestOtherValue(Profiles[ProfileIdx].OtherIPs));
         end else begin
            if Profiles[ProfileIdx].OtherIPs[FoundIdx].LoginCount > 0 then
               Dec(Profiles[ProfileIdx].OtherIPs[FoundIdx].LoginCount);
         end;
      end;

      { the counts in the header follow the lists (a Dec of a count that was already 0 made
        it 65535) }
      Profiles[ProfileIdx].Profile.OtherNamesCount := Length(Profiles[ProfileIdx].OtherNames);
      Profiles[ProfileIdx].Profile.OtherHWIDsCount := Length(Profiles[ProfileIdx].OtherHWIDs);
      Profiles[ProfileIdx].Profile.OtherIPsCount := Length(Profiles[ProfileIdx].OtherIPs);

      if (SName <> '') and (GetProfileActiveName(ProfileIdx) = SName) then
      begin
        { the name used last before it; never an empty one, which is what showed up as an
          empty row in "list profiles" }
        Key := NewestOtherValue(Profiles[ProfileIdx].OtherNames);
        if Key <> '' then SetProfileActiveName(ProfileIdx, Key);
      end;
      if ProfileIsEmpty(ProfileIdx) then
      begin
        { nothing is left: the next new player takes this number, with nothing of this one }
        FillChar(Profiles[ProfileIdx].Profile, SizeOf(TBinaryProfile), 0);
        SetLength(Profiles[ProfileIdx].Visits, 0);
        FillChar(Profiles[ProfileIdx].Steam, SizeOf(TSteamInfo), 0);
        ForgetVerdictsOf(ProfileIdx);
        EmptyScanNeeded := True;
      end;
      MarkDirty(ProfileIdx);
    finally UnlockProfiles; end;
  except
    ExportFailed('Rollback_Profile_Changes');
  end;
end;

{ how many entries of a profile were thrown away in the last cleanup }
var
  MaintDroppedIPs: Integer = 0;
  MaintDroppedNames: Integer = 0;
  MaintDroppedHwids: Integer = 0;
  MaintDroppedGeo: Integer = 0;
  MaintDroppedProfiles: Integer = 0;
  MaintProfiles: Integer = 0;
  MaintLast: Double = 0;
  MaintChanged: Boolean = False;
  MaintRunning: Boolean = False;
  MaintDone: Boolean = False;
  { the report is written by the cleanup's thread and read by the game thread: a string that is
    replaced while it is copied is freed under the copy, so it is kept behind a lock }
  MaintCS: TRTLCriticalSection;
  MaintReport: string = '';

procedure SetMaintReport(const Text: string);
begin
  EnterCriticalSection(MaintCS);
  try
    MaintReport := Text;
  finally LeaveCriticalSection(MaintCS); end;
end;

{ throws away what nobody has used for Days days; 0 means keep everything. The newest entry of
  a kind always stays, so a profile never loses its last name or its last computer. Idx holds
  the entries of the list (nil: no index), Net the networks of addresses (nil for the others). }
function CleanOtherData(var Arr: TBinaryOtherDataArray; Days, Keep: Integer;
  Index: LongInt; Idx, Net: TStringList): Integer;
var
  k, Newest, Oldest: Integer;
  Limit: Double;

  procedure DropAt(At: Integer);
  var
    Key: string;
  begin
    Key := GetOtherDataString(Arr[At]);
    if Idx <> nil then IndexRemove(Idx, Key, Index);
    { the network of an address that went used to stay in its index, and /multi went on
      saying "same network" about an address the account no longer had }
    if Net <> nil then IndexRemove(Net, Net24Of(Key), Index);
    RemoveOtherData(Arr, At);
  end;

begin
  Result := 0;
  if Length(Arr) = 0 then Exit;
  Limit := Now - Days;
  { the entry used last always stays }
  Newest := 0;
  for k := 1 to High(Arr) do
    if Arr[k].LastUsedDate > Arr[Newest].LastUsedDate then
      Newest := k;
  if Days > 0 then
    for k := High(Arr) downto 0 do
      if (k <> Newest) and (Arr[k].LastUsedDate > 0) and (Arr[k].LastUsedDate < Limit) then
      begin
        DropAt(k);
        Inc(Result);
        if Newest > k then Dec(Newest);
      end;
  { still too many: the least recently used go first, ties included }
  while (Keep > 0) and (Length(Arr) > Keep) do
  begin
    Oldest := -1;
    for k := 0 to High(Arr) do
      if (k <> Newest) and ((Oldest < 0) or (Arr[k].LastUsedDate < Arr[Oldest].LastUsedDate)) then
        Oldest := k;
    if Oldest < 0 then Break;
    DropAt(Oldest);
    Inc(Result);
    if Newest > Oldest then Dec(Newest);
  end;
end;

{ ProfileDays: everything of an account nobody has played on for that long goes - its entries
  (and their places in the indexes), its visits, what Steam said about it and what the admins
  decided about it. Its number stays, since the list would move under everybody who holds one,
  and the next new player takes it (Login_Player). The caller holds ProfilesCritSec. }
procedure ForgetProfile(Index: Integer);
var
  k: Integer;
  Key: string;
begin
  for k := 0 to High(Profiles[Index].OtherSteams) do
    IndexRemove(SteamIndex, GetOtherDataString(Profiles[Index].OtherSteams[k]), Index);
  for k := 0 to High(Profiles[Index].OtherHWIDs) do
    IndexRemove(HwidIndex, GetOtherDataString(Profiles[Index].OtherHWIDs[k]), Index);
  for k := 0 to High(Profiles[Index].OtherIPs) do
  begin
    Key := GetOtherDataString(Profiles[Index].OtherIPs[k]);
    IndexRemove(IpIndex, Key, Index);
    IndexRemove(Net24Index, Net24Of(Key), Index);
  end;
  SetLength(Profiles[Index].OtherNames, 0);
  SetLength(Profiles[Index].OtherSteams, 0);
  SetLength(Profiles[Index].OtherHWIDs, 0);
  SetLength(Profiles[Index].OtherIPs, 0);
  SetLength(Profiles[Index].Visits, 0);
  FillChar(Profiles[Index].Steam, SizeOf(TSteamInfo), 0);
  FillChar(Profiles[Index].Profile, SizeOf(TBinaryProfile), 0);
  ForgetVerdictsOf(Index);
  EmptyScanNeeded := True;
end;

{ the daily cleanup: old addresses, names and computers of a profile, accounts nobody has used
  for ProfileDays, and locations nobody needs any more. One profile at a time under the lock,
  so a join never waits for all of them; what changed goes into the journal. }
procedure DoMaintenance(Background: Boolean);
var
  i, k, Alive, Dropped, Visits: Integer;
  Limit, Unused: Double;
  More: Boolean;
  Before: AnsiString;
begin
  MaintDroppedIPs := 0;
  MaintDroppedNames := 0;
  MaintDroppedHwids := 0;
  MaintDroppedGeo := 0;
  MaintDroppedProfiles := 0;
  MaintProfiles := 0;
  Unused := Now - Settings.ProfileDays;

  i := 0;
  repeat
    if Background and ShuttingDown then Break;
    LockProfiles;
    try
      More := i < ProfileCount;
      if More then
      begin
        { with Engine = mysql an account is used by the other servers as well: its last game
          here says nothing about it }
        if (Settings.ProfileDays > 0) and not SharedEngine and not ProfileIsEmpty(i) and
           (Profiles[i].Profile.LastPlayedTime > 0) and (Profiles[i].Profile.LastPlayedTime < Unused) then
        begin
          ForgetProfile(i);
          Inc(MaintDroppedProfiles);
          MarkDirty(i);
        end
        else
        begin
          Visits := Length(Profiles[i].Visits);
          Before := SharedBefore(i);
          Dropped := CleanOtherData(Profiles[i].OtherIPs, Settings.AddressDays, Settings.MaxAddresses, i, IpIndex, Net24Index);
          Inc(MaintDroppedIPs, Dropped);
          k := CleanOtherData(Profiles[i].OtherNames, Settings.NameDays, 0, i, nil, nil);
          Inc(MaintDroppedNames, k);
          Inc(Dropped, k);
          k := CleanOtherData(Profiles[i].OtherHWIDs, Settings.HwidDays, 0, i, HwidIndex, nil);
          Inc(MaintDroppedHwids, k);
          Inc(Dropped, k);
          TrimVisits(i);
          Profiles[i].Profile.OtherIPsCount := Length(Profiles[i].OtherIPs);
          Profiles[i].Profile.OtherNamesCount := Length(Profiles[i].OtherNames);
          Profiles[i].Profile.OtherHWIDsCount := Length(Profiles[i].OtherHWIDs);
          { the oldest entry that is left is the first one now, as after a restart }
          if Dropped > 0 then MarkProfileFirstUsed(Profiles[i]);
          if (Dropped > 0) or (Length(Profiles[i].Visits) <> Visits) then
          begin
            SharedKeep(i, Before);
            MarkDirty(i);
          end;
          if not ProfileIsEmpty(i) then Inc(MaintProfiles);
        end;
      end;
    finally UnlockProfiles; end;
    Inc(i);
  until not More;

  { a location goes when nobody has looked it up for GeoKeepDays (not when no profile has the
    address any more - nothing here asks that): an address a player keeps coming from is looked
    up again every RefreshDays at a join, so it stays. The index is built again only when
    something went. }
  if (Settings.GeoKeepDays > 0) and not (Background and ShuttingDown) then
  begin
    Limit := Now - Settings.GeoKeepDays;
    EnterCriticalSection(GeoCritSec);
    try
      Alive := 0;
      for k := 0 to GeoCount - 1 do
      begin
        if (GeoCache[k].LastUpdated > 0) and (GeoCache[k].LastUpdated < Limit) then
        begin
          Inc(MaintDroppedGeo);
          Continue;
        end;
        if Alive <> k then
          GeoCache[Alive] := GeoCache[k];
        Inc(Alive);
      end;
      GeoCount := Alive;
      if MaintDroppedGeo > 0 then RebuildGeoIndex;
    finally LeaveCriticalSection(GeoCritSec); end;
  end;

  SetMaintReport(Format('cleanup: %d addresses, %d names, %d computers, %d locations and %d unused ' +
    'profiles dropped, %d profiles left',
    [MaintDroppedIPs, MaintDroppedNames, MaintDroppedHwids, MaintDroppedGeo, MaintDroppedProfiles, MaintProfiles]));
  MaintChanged := (MaintDroppedIPs + MaintDroppedNames + MaintDroppedHwids + MaintDroppedGeo +
    MaintDroppedProfiles) > 0;
end;

type
  TMaintThread = class(TThread)
  protected
    procedure Execute; override;
  end;

procedure TMaintThread.Execute;
begin
  GuardWorkerThread;
  try
    DoMaintenance(True);
  except
    try SetMaintReport('cleanup: it stopped with an error'); except end;
    MaintChanged := False;
  end;
  MaintDone := True;
  MaintRunning := False;
end;

{ The script asks once a minute; the library decides whether a day has passed and then runs the
  cleanup on a worker. The answer of a finished run (True: something was dropped, the report is
  in Get_Maintenance_Report) comes with the next question. Force runs it here and now, on the
  thread that asks and over every profile: the tests do that, the script never does. }
function Run_Maintenance(Force: Boolean): Boolean; cdecl;
begin
  Result := False;
  try
    if Force then
    begin
      if MaintRunning then Exit;
      MaintLast := Now;
      DoMaintenance(False);
      Result := MaintChanged;
      Exit;
    end;
    if MaintDone then
    begin
      MaintDone := False;
      Result := MaintChanged;
      Exit;
    end;
    if MaintRunning or ShuttingDown then Exit;
    if (MaintLast > 0) and (Now - MaintLast < 1) then Exit;
    MaintLast := Now;
    MaintRunning := True;
    try
      StartBackground(TMaintThread.Create(True));
    except
      MaintRunning := False;
    end;
  except
    ExportFailed('Run_Maintenance');
    Result := False;
  end;
end;

function Get_Maintenance_Report(): PChar; cdecl;
begin
  try
    EnterCriticalSection(MaintCS);
    try
      TempStringResult := MaintReport;
    finally LeaveCriticalSection(MaintCS); end;
  except
    ExportFailed('Get_Maintenance_Report');
    TempStringResult := '';
  end;
  Result := PChar(TempStringResult);
end;

var
  { A full write was asked for while one was running. That one had passed some accounts before
    they changed, and with the journal off (or full) nothing else keeps those changes: the stop
    of the server used to lose them. Cleanup writes once more; any write that starts later
    takes them anyway. }
  SaveAgain: Boolean = False;
  SaveAgainPath: string = '';

{ A full write, on the game thread only. Nothing is written before the database has been read
  completely: a server stopped (or a script reloaded) while the reading runs would otherwise
  write the empty memory over the file, and the next start would read that. After a load that
  failed, what is in memory is not the database either. }
function StartSave(const FilePath: string; Asker, Session: Integer): Boolean;
var
  Restoring: Boolean;
begin
  Result := False;
  if SaveInProgress then
  begin
    SaveAgain := True;
    SaveAgainPath := FilePath;
    Exit;
  end;
  if LoadError then begin SaveError := True; SaveComplete := True; Exit; end;
  if not LoadComplete or LoadInProgress then Exit;
  { an admin puts a copy in place of the file right now (Restore_Backup): this write would put
    the memory over it. Taken under the lock the restore takes, so one of the two waits. }
  EnterCriticalSection(JournalCS);
  try
    Restoring := RestoreRunning;
    if not Restoring then SaveInProgress := True;
  finally LeaveCriticalSection(JournalCS); end;
  if Restoring then begin SaveError := True; SaveComplete := True; Exit; end;
  SaveAgain := False;
  SaveComplete := False; SaveError := False;
  try
    { the last write has finished its work (SaveInProgress went down at its end): this waits
      for the few lines of it that are left }
    if SaveThread <> nil then begin SaveThread.WaitFor; FreeAndNil(SaveThread); end;
    SaveThread := TSaveThread.Create(FilePath, Asker, Session);
    SaveThread.Start;
  except
    { no thread: nothing is being written, and the script may try again (a thread that was
      made but not started is freed) }
    if SaveThread <> nil then
      try SaveThread.Free; except end;
    SaveThread := nil;
    SaveInProgress := False;
    SaveError := True;
    SaveComplete := True;
    raise;
  end;
  Result := True;
end;

procedure Start_Save_Binary(FilePath: PChar); cdecl;
begin
  try
    StartSave(string(FilePath), 0, 0);
  except
    ExportFailed('Start_Save_Binary');
  end;
end;

{ /storage save: the same write, and Slot hears how it went. False when a write is running
  already, or the database is still being read or could not be read (then nothing may be
  written over it). }
function Start_Save_For(FilePath: PChar; Slot: Integer): Boolean; cdecl;
begin
  Result := False;
  try
    Result := StartSave(string(FilePath), Slot, SessionOf(Slot));
  except
    ExportFailed('Start_Save_For');
    Result := False;
  end;
end;

{ the database was read from MySQL, the journal was put on top of the file, or there was no file
  yet (Engine = local): True once, and the script writes the file from it }
function Save_After_Load(): Boolean; cdecl;
begin
  try
    Result := SaveAfterLoad and LoadComplete and not LoadInProgress and not LoadError;
    if Result then SaveAfterLoad := False;
  except
    Result := False;
  end;
end;

{ the next line of how something in the background ended, "slot|color|text", '' for none.
  Slot 0 is every admin in the game and the console (Multi_Notice), MAX_SLOTS the console
  alone. The script calls it every tick, so it also frees the background threads that have
  finished. }
function Notice_Line(): PChar; cdecl;
begin
  TempStringResult := '';
  try
    SweepBackground(4);
    if NoticeList <> nil then
    begin
      EnterCriticalSection(NoticeCS);
      try
        if NoticeList.Count > 0 then
        begin
          TempStringResult := NoticeList[0];
          NoticeList.Delete(0);
        end;
      finally LeaveCriticalSection(NoticeCS); end;
    end;
  except
    ExportFailed('Notice_Line');
    TempStringResult := '';
  end;
  Result := PChar(TempStringResult);
end;

{ where the database was read from at start (and whether it was moved there first), '' when
  there was nothing to say }
function Get_Load_Note(): PChar; cdecl;
begin
  try
    TempStringResult := MoveNote;
    if LoadNote <> '' then
    begin
      if TempStringResult <> '' then TempStringResult := TempStringResult + '; ';
      TempStringResult := TempStringResult + LoadNote;
    end;
  except
    ExportFailed('Get_Load_Note');
    TempStringResult := '';
  end;
  Result := PChar(TempStringResult);
end;

{ a line the script writes to the console, kept in logs/antifake/antifake.log as well }
procedure Log_Line(Text: PChar); cdecl;
begin
  try
    LogEvent(string(Text));
  except
    { the log itself failed: there is nowhere to say so }
  end;
end;

function Is_Save_Complete(): Boolean; cdecl;
begin
  try Result := SaveComplete; except Result := False; end;
end;

function Is_Save_Error(): Boolean; cdecl;
begin
  try Result := SaveError; except Result := False; end;
end;

function Is_Save_In_Progress(): Boolean; cdecl;
begin
  try Result := SaveInProgress; except Result := False; end;
end;

var
  { the addresses whose place had a "?" and were asked about again since the library started }
  GeoAskedAgain: TStringList = nil;

{ A letter of a place that an older version lost is a "?" in it (before the letters were kept,
  "Poland, ?wilcza"; on Linux the conversion itself wrote it). Such a place is asked about again
  once per start of the library, not only after RefreshDays: a place that really has a sign
  the console cannot show keeps its "?" and is not asked about over and over. Under GeoCritSec. }
function GeoAskAgain(Idx: Integer; const SIP: string): Boolean;
begin
  Result := False;
  if (Pos('?', string(GeoCache[Idx].City)) = 0) and (Pos('?', string(GeoCache[Idx].Region)) = 0) and
     (Pos('?', string(GeoCache[Idx].Country)) = 0) then Exit;
  if GeoAskedAgain = nil then
  begin
    GeoAskedAgain := TStringList.Create;
    GeoAskedAgain.Sorted := True;
  end;
  if GeoAskedAgain.IndexOf(SIP) >= 0 then Exit;
  GeoAskedAgain.Add(SIP);
  Result := True;
end;

procedure Check_And_Update_Geo(IP: PChar); cdecl;
var
  SIP: string;
  Idx: Integer;
  NeedsUpdate: Boolean;
begin
  try
    SIP := string(IP);
    if IsLocalAddress(SIP) then Exit;

    NeedsUpdate := False;

    { called at every join, on the game thread: it asks again at the next join rather than wait }
    if not GrabLock(GeoCritSec, 5) then
    begin
      if Settings.Trace then TraceStep('geo check of ' + SIP + ': busy, asked at the next join');
      Exit;
    end;
    try
      Idx := FindGeoIndex(SIP);
      if Idx = -1 then NeedsUpdate := True
      else if (Now - GeoCache[Idx].LastUpdated) > Settings.GeoRefreshDays then NeedsUpdate := True
      else if GeoAskAgain(Idx, SIP) then NeedsUpdate := True;
      if Settings.Trace then
        if Idx = -1 then
          TraceStep('geo check of ' + SIP + ': not known yet')
        else if NeedsUpdate then
          TraceStep('geo check of ' + SIP + ': ' + TraceBytes(string(GeoCache[Idx].City)) + ', asked again')
        else
          TraceStep('geo check of ' + SIP + ': ' + TraceBytes(string(GeoCache[Idx].City)) + ', kept');
    finally
      LeaveCriticalSection(GeoCritSec);
    end;

    if NeedsUpdate then
      StartGeoLookup(SIP);
  except
    ExportFailed('Check_And_Update_Geo');
  end;
end;

{ "Europe, Poland, Mazovia, Warsaw"; a part the lookup did not know is left out instead of
  leaving ", ," behind }
function Get_Geo_String(IP: PChar): PChar; cdecl;
var
  Idx: Integer;
  SIP: string;

  procedure Part(const S: string);
  begin
    if S = '' then Exit;
    if TempStringResult <> '' then TempStringResult := TempStringResult + ', ';
    TempStringResult := TempStringResult + S;
  end;

begin
  TempStringResult := '';
  try
    SIP := string(IP);
    if IsLocalAddress(SIP) then
      TempStringResult := 'Local network'
    else if not GrabLock(GeoCritSec, 5) then
      TempStringResult := 'Locating...'
    else
      try
        Idx := FindGeoIndex(SIP);
        if Idx <> -1 then
        begin
          Part(string(GeoCache[Idx].Continent));
          Part(string(GeoCache[Idx].Country));
          Part(string(GeoCache[Idx].Region));
          Part(string(GeoCache[Idx].City));
          if TempStringResult = '' then TempStringResult := 'Unknown';
        end
        { drawing a table never starts a lookup: the join asks for an address once, and a page
          that is redrawn (or paged through) would otherwise ask again and again }
        else if GeoPending.IndexOf(SIP) >= 0 then
          TempStringResult := 'Locating...'
        else
          TempStringResult := 'Unknown';
      finally
        LeaveCriticalSection(GeoCritSec);
      end;
  except
    ExportFailed('Get_Geo_String');
    TempStringResult := '';
  end;
  Result := PChar(TempStringResult);
end;

{ the result the script selected (Select_Task_Result). In a list of visits the similarity of a
  row holds how sure its location is, and its match type the VF_ flags of the visit. }
function ResultRowOk(ItemIdx: Integer): Boolean;
begin
  Result := (ActiveResult <> nil) and (ItemIdx >= 0) and (ItemIdx < ActiveResult^.ResultCount) and
            (ItemIdx < Length(ActiveResult^.Results));
end;

function Get_Result_Item_Location(ItemIdx: Integer): PChar; cdecl;
begin
  TempStringResult := '';
  try
    if ResultRowOk(ItemIdx) then
      TempStringResult := ActiveResult^.Results[ItemIdx].Location;
  except
    ExportFailed('Get_Result_Item_Location');
    TempStringResult := '';
  end;
  Result := PChar(TempStringResult);
end;

function Get_Result_Item_Confidence(ItemIdx: Integer): Integer; cdecl;
begin
  Result := 0;
  try
    if ResultRowOk(ItemIdx) then Result := ActiveResult^.Results[ItemIdx].Similarity;
  except
    Result := 0;
  end;
end;

function Get_Result_Item_Flags(ItemIdx: Integer): Integer; cdecl;
begin
  Result := 0;
  try
    if ResultRowOk(ItemIdx) then Result := ActiveResult^.Results[ItemIdx].MatchType;
  except
    Result := 0;
  end;
end;

function Get_Result_Item_Risk(ItemIdx: Integer): Integer; cdecl;
begin
  Result := 0;
  try
    if ResultRowOk(ItemIdx) then Result := ActiveResult^.Results[ItemIdx].RiskLevel;
  except
    Result := 0;
  end;
end;

function Get_Result_Top_Locations(): PChar; cdecl;
begin
  TempStringResult := '';
  try
    if ActiveResult <> nil then TempStringResult := ActiveResult^.TopLocations;
  except
    ExportFailed('Get_Result_Top_Locations');
    TempStringResult := '';
  end;
  Result := PChar(TempStringResult);
end;

const
  { how long the end of the server waits for each kind of thread. A thread that is not done by
    then is left running and named in the log: a server that never stops is worse. }
  SEARCH_STOP_MS = 10000;   { every search together; they look at Terminated as they go }
  LOAD_STOP_MS = 20000;     { the reading looks at Terminated, and MySQL is cut before }
  SAVE_STOP_MS = 60000;     { a full write is the data: it gets the longest }
  SAVE_ABORT_MS = 10000;    { a write told to stop keeps the old file and ends at once }

{ True when T has ended within Millis (and has been waited for, so it can be freed) }
function ThreadEnded(T: TThread; Millis: Int64): Boolean;
var
  Deadline: QWord;
begin
  Result := True;
  if T = nil then Exit;
  if Millis < 0 then Millis := 0;
  Deadline := GetTickCount64 + QWord(Millis);
  while not ThreadGone(T) and (GetTickCount64 < Deadline) do Sleep(10);
  Result := ThreadGone(T);
  if Result then T.WaitFor;
end;

const
  GET_MODULE_HANDLE_EX_FLAG_PIN = 1;
  GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS = 4;
{$IFNDEF WINDOWS}
  RTLD_NOW_FLAG = 2;
  RTLD_NODELETE_FLAG = $1000;
{$ENDIF}

var
  { the script has called Cleanup: the unload of the library does not do it again }
  CleanupCalled: Boolean = False;
  LibraryPinned: Boolean = False;

{$IFDEF WINDOWS}
function AfGetModuleHandleEx(Flags: LongWord; Addr: Pointer; out Module: THandle): LongBool; stdcall;
  external 'kernel32.dll' name 'GetModuleHandleExW';
{$ELSE}
type
  TAfDlInfo = record
    dli_fname: PAnsiChar;
    dli_fbase: Pointer;
    dli_sname: PAnsiChar;
    dli_saddr: Pointer;
  end;

function af_dladdr(Addr: Pointer; var Info: TAfDlInfo): LongInt; cdecl; external 'dl' name 'dladdr';
function af_dlopen(Name: PAnsiChar; Flags: LongInt): Pointer; cdecl; external 'dl' name 'dlopen';
{$ENDIF}

{ Keeps the library in the process for good: the server unloads it after the script has
  stopped, and a thread of it that is still running would then run on in code that is gone and
  bring the server down. BasicExtended and ZitroStats do the same from the start; AntiFake only
  when a thread is left over. }
function PinLibrary: Boolean;
{$IFDEF WINDOWS}
var
  H: THandle;
begin
  H := 0;
  if not LibraryPinned then
    LibraryPinned := AfGetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_PIN or GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
      @PinLibrary, H);
  Result := LibraryPinned;
end;
{$ELSE}
var
  Info: TAfDlInfo;
begin
  if not LibraryPinned then
  begin
    FillChar(Info, SizeOf(Info), 0);
    if (af_dladdr(@PinLibrary, Info) <> 0) and (Info.dli_fname <> nil) then
      LibraryPinned := af_dlopen(Info.dli_fname, RTLD_NOW_FLAG or RTLD_NODELETE_FLAG) <> nil;
  end;
  Result := LibraryPinned;
end;
{$ENDIF}

{ The script calls this when it stops (CleanupCalled); the unload of the library does not call
  it again. The order matters: no new search may start (ShuttingDown), the connections to MySQL
  are cut so nothing waits for a database, the searches are told to stop, then every background
  thread - the commands first among them, which may still be starting a search - is waited for,
  and the searches are collected last. Every wait has an end, and one step that fails does not
  stop the others. A thread that still runs at the end keeps the library loaded (pinned): the
  server unloads it right after, and that must not happen under a running thread. }
procedure Cleanup(); cdecl;
var
  i, Left: Integer;
  T: TThread;
  Deadline: QWord;
  Again, Running: Boolean;
  MarkedBefore: Integer;

  { the full write that runs, to its end; one that does not end in time is told to stop, and
    it leaves the old file as it was }
  procedure WaitForSave;
  begin
    if SaveThread = nil then Exit;
    if not ThreadEnded(SaveThread, SAVE_STOP_MS) then
    begin
      LogEvent('the full write did not end in time and was told to stop; the file that was there is kept');
      SaveThread.Terminate;
      ThreadEnded(SaveThread, SAVE_ABORT_MS);
    end;
    if ThreadGone(SaveThread) then
    begin
      { past its last line: waited for before it is freed, like every thread here }
      SaveThread.WaitFor;
      FreeAndNil(SaveThread);
    end
    else
      LogEvent('the full write did not stop and was left running');
  end;

begin
  CleanupCalled := True;
  { Engine = mysql: the last exchange, while the connections are still there }
  try SharedStop; except ExportFailed('Cleanup (the last exchange)'); end;
  ShuttingDown := True;
  try
    AbortMysqlConnections;
    for i := 1 to MAX_SLOTS do
    begin
      EnterCriticalSection(Contexts[i].Lock);
      try
        if Contexts[i].Thread <> nil then Contexts[i].Thread.Terminate;
      finally LeaveCriticalSection(Contexts[i].Lock); end;
    end;
    if LoadThread <> nil then LoadThread.Terminate;
    WaitBackground(40000);
  except
    ExportFailed('Cleanup (the threads in the background)');
  end;
  try
    Deadline := GetTickCount64 + SEARCH_STOP_MS;
    for i := 1 to MAX_SLOTS do begin
      { the thread is taken out under the lock and waited for without it: the finishing thread
        needs that same lock to write its result }
      EnterCriticalSection(Contexts[i].Lock);
      T := Contexts[i].Thread;
      Contexts[i].Thread := nil;
      LeaveCriticalSection(Contexts[i].Lock);
      if T <> nil then begin
        T.Terminate;
        if ThreadEnded(T, Int64(Deadline) - Int64(GetTickCount64)) then
          T.Free
        else
        begin
          LogEvent('the search of slot ' + IntToStr(i) + ' did not stop in time and was left running');
          { kept, so the end below knows it still runs (no new search can take the slot now) }
          EnterCriticalSection(Contexts[i].Lock);
          Contexts[i].Thread := TSearchThread(T);
          LeaveCriticalSection(Contexts[i].Lock);
        end;
      end;
    end;
  except
    ExportFailed('Cleanup (the searches)');
  end;
  try
    if LoadThread <> nil then
    begin
      LoadThread.Terminate;
      if ThreadEnded(LoadThread, LOAD_STOP_MS) then
        FreeAndNil(LoadThread)
      else
        LogEvent('the reading of the database did not stop in time and was left running');
    end;
  except
    ExportFailed('Cleanup (the reading)');
  end;
  { a load that ended while the server stopped may have started an exchange }
  try SharedStop; except ExportFailed('Cleanup (the last exchange)'); end;
  try
    Running := SaveThread <> nil;
    MarkedBefore := 0;
    if Running then
    begin
      LockProfiles;
      try MarkedBefore := DirtyCount; finally UnlockProfiles; end;
    end;
    WaitForSave;
    { What that write missed is written once more, or it would be lost with the server: a write
      the script asked for while it ran (its last one, with the journal off or full), and the
      accounts marked again while it ran - among them the ones Journal_Final had just put into
      the journal, which the end of the write started afresh; there is no later round of the
      journal to take them any more. }
    if (SaveThread = nil) and LoadComplete and not LoadError then
    begin
      Again := SaveAgain;
      if Running and not Again then
      begin
        LockProfiles;
        try Again := DirtyCount > MarkedBefore; finally UnlockProfiles; end;
      end;
      { an append to the journal failed since the last full write - Journal_Final's own among
        them, which nobody waited for: the journal misses changes, the file has to take them }
      if not Again then
      begin
        EnterCriticalSection(JournalCS);
        try Again := JournalWriteFailed; finally LeaveCriticalSection(JournalCS); end;
        if Again then LogEvent('the journal could not take every change; the whole database is written instead');
      end;
      SaveAgain := False;
      if SaveAgainPath = '' then SaveAgainPath := CurrentDbPath;
      if Again and (SaveAgainPath <> '') then
        if StartSave(SaveAgainPath, 0, 0) then WaitForSave;
    end;
  except
    ExportFailed('Cleanup (the full write)');
  end;
  { a search may have asked for a location on its way out }
  Left := 0;
  try Left := WaitBackground(3 * Settings.GeoTimeoutMs); except end;
  { What still runs now would run on in a library the server unloads in a moment: the unload
    waits for nothing, and the code is gone under the thread. Whatever has ended since is
    collected; for the rest the library stays in the process until the server ends. }
  try
    for i := 1 to MAX_SLOTS do
    begin
      EnterCriticalSection(Contexts[i].Lock);
      T := Contexts[i].Thread;
      LeaveCriticalSection(Contexts[i].Lock);
      if T = nil then Continue;
      if ThreadEnded(T, 0) then
      begin
        EnterCriticalSection(Contexts[i].Lock);
        Contexts[i].Thread := nil;
        LeaveCriticalSection(Contexts[i].Lock);
        T.Free;
      end
      else
        Inc(Left);
    end;
    if LoadThread <> nil then
      if ThreadEnded(LoadThread, 0) then FreeAndNil(LoadThread) else Inc(Left);
    if SaveThread <> nil then
      if ThreadEnded(SaveThread, 0) then FreeAndNil(SaveThread) else Inc(Left);
    if Left > 0 then
    begin
      if PinLibrary then
        LogEvent(IntToStr(Left) + ' thread(s) of AntiFake were still running when the script stopped: ' +
          'the library stays loaded so they cannot bring the server down, and AntiFake works again ' +
          'after a restart of the server')
      else
        LogEvent(IntToStr(Left) + ' thread(s) of AntiFake were still running when the script stopped, ' +
          'and the library could not be kept loaded: restart the server');
    end;
  except
    ExportFailed('Cleanup (the threads left running)');
  end;
  try LogFlush; except end;
end;

{ What the unload of the library may still do. It runs under the lock of the system's loader:
  a thread that ends needs that lock to end, so a wait for one there never returns. Nothing is
  waited for here; the script's Cleanup did the waiting, and a library with a thread left over
  was pinned by it and stays until the process ends (when every thread has been ended already).
  Without a Cleanup of the script (a test program that just ends) the threads are only told to
  stop. }
procedure CleanupAtUnload;
var
  i: Integer;
begin
  if CleanupCalled then Exit;
  ShuttingDown := True;
  try
    for i := 1 to MAX_SLOTS do
      if Contexts[i].Thread <> nil then Contexts[i].Thread.Terminate;
    if LoadThread <> nil then LoadThread.Terminate;
    if SaveThread <> nil then SaveThread.Terminate;
  except
  end;
  try LogFlush; except end;
end;
