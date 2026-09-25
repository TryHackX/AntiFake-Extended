{ ---------------------------------------------------------------------------------------
  AntiFake Extended - settings.ini: the defaults, the reading and the two ways to ask for a value.
  --------------------------------------------------------------------------------------- }

{ ---------------- settings ---------------- }

procedure SetDefaultSettings;
begin
  Settings.GeoEnabled := True;
  Settings.GeoMaxLookups := DEF_GEO_MAX_LOOKUPS;
  Settings.GeoRetrySeconds := DEF_GEO_RETRY_SECONDS;
  Settings.GeoTimeoutMs := DEF_GEO_TIMEOUT_MS;
  Settings.GeoRefreshDays := DEF_GEO_REFRESH_DAYS;
  Settings.GeoKeepDays := DEF_GEO_KEEP_DAYS;
  Settings.GeoTextPlain := False;
  Settings.VisitKeepCount := DEF_VISIT_KEEP_COUNT;
  Settings.VisitKeepDays := DEF_VISIT_KEEP_DAYS;
  Settings.VisitCooldownMinutes := DEF_VISIT_COOLDOWN_MINUTES;
  Settings.LastVisitsRows := 500;
  Settings.CommandCooldownSeconds := 3;
  Settings.HeavyCooldownSeconds := 10;
  Settings.MaxListsPerSecond := 3;
  Settings.PublicCommands := True;
  Settings.Trace := False;
  Settings.LinesPerTick := 4;
  Settings.MaxQueuedLines := 200;
  Settings.ActiveDays := DEF_ACTIVE_DAYS;
  Settings.MysqlEngine := ENGINE_BIN;
  Settings.MysqlHost := '';
  Settings.MysqlPort := 3306;
  Settings.MysqlDatabase := 'soldat';
  Settings.MysqlUser := '';
  Settings.MysqlPassword := '';
  Settings.MysqlServerId := 1;
  Settings.MysqlMinutes := 60;
  Settings.SyncSeconds := 3;
  Settings.MysqlCodepage := 1250;
  Settings.SharedSearch := False;
  Settings.SharedLinks := False;
  Settings.ColMain := $F0E68C;
  Settings.ColInfo := $87CEEB;
  Settings.ColData := $C0C0C0;
  Settings.ColWarn := $FFD700;
  Settings.ColError := $FF6347;
  Settings.ColGood := $66CC66;
  Settings.JournalSeconds := 10;
  Settings.BackupHours := 24;
  Settings.BackupKeep := 7;
  Settings.CmdVisits := 1;
  Settings.CmdVisitStats := 2;
  Settings.CmdVisitsOf := 0;
  Settings.CmdStatus := 1;
  Settings.CmdNames := 1;
  Settings.CmdStopSteam := 1;
  Settings.CmdLocate := 1;
  Settings.CmdProfiles := 0;
  Settings.CmdSearch := 0;
  Settings.CmdMulti := 0;
  Settings.CmdHelp := 2;
  Settings.CmdLastVisitors := 2;
  Settings.MultiNotice := True;
  Settings.MultiMinLinks := 1;
  Settings.MultiByAddress := True;
  Settings.MultiReportScore := 55;
  Settings.MultiMinSignals := 1;
  Settings.MultiMaxLinks := 25;
  Settings.MultiMaxShared := 20;
  Settings.MultiMaxSharedPC := 200;
  Settings.SteamKey := '';
  Settings.SteamRefreshDays := DEF_STEAM_REFRESH_DAYS;
  Settings.SteamBatch := DEF_STEAM_BATCH;
  Settings.CurlPath := 'curl';
  Settings.CurlTimeoutSeconds := 10;
  Settings.AddressDays := DEF_ADDRESS_DAYS;
  Settings.NameDays := DEF_NAME_DAYS;
  Settings.HwidDays := DEF_HWID_DAYS;
  Settings.MaxAddresses := DEF_MAX_ADDRESSES;
  Settings.ProfileDays := DEF_PROFILE_DAYS;
  Settings.SaveMinutes := DEF_SAVE_MINUTES;
  Settings.ResultsPerPage := DEF_RESULTS_PER_PAGE;
  Settings.TaskTimeoutSeconds := DEF_TASK_TIMEOUT_SECONDS;
  Settings.LocateTimeoutSeconds := DEF_LOCATE_TIMEOUT_SECONDS;
  Settings.AdminCommands := True;
  Settings.PlayerCommands := False;
  Settings.ChatCommands := False;
  Settings.DatabaseFile := 'nickreg/antifake.dat';
  Settings.Loaded := False;
  Settings.Message := '';
end;

function SettingBool(const V: string; Def: Boolean): Boolean;
var
  L: string;
begin
  L := LowerCase(Trim(V));
  if (L = '1') or (L = 'yes') or (L = 'true') or (L = 'on') then Result := True
  else if (L = '0') or (L = 'no') or (L = 'false') or (L = 'off') then Result := False
  else Result := Def;
end;

{ the same, and a word that is neither is counted with the values out of range }
function SettingBool(const V: string; Def: Boolean; var Bad: Integer): Boolean;
var
  L: string;
begin
  Result := SettingBool(V, Def);
  L := LowerCase(Trim(V));
  if (L <> '') and (L <> '1') and (L <> 'yes') and (L <> 'true') and (L <> 'on') and
     (L <> '0') and (L <> 'no') and (L <> 'false') and (L <> 'off') then Inc(Bad);
end;

{ off / self / all, or 0 / 1 / 2 for anybody who prefers numbers }
function SettingScope(const V: string; Def: Integer; var Bad: Integer): Integer;
var
  T: string;
begin
  T := LowerCase(Trim(V));
  if (T = 'off') or (T = 'no') or (T = 'none') or (T = '0') then Result := 0
  else if (T = 'self') or (T = 'own') or (T = 'me') or (T = '1') then Result := 1
  else if (T = 'all') or (T = 'yes') or (T = 'anybody') or (T = '2') then Result := 2
  else
  begin
    Result := Def;
    if T <> '' then Inc(Bad);
  end;
end;

{ a colour written as RRGGBB, with or without a # or a 0x in front }
function SettingColor(const V: string; Def: Integer; var Bad: Integer): Integer;
var
  T: string;
  n: Int64;
begin
  Result := Def;
  T := Trim(V);
  if T = '' then Exit;
  if Copy(T, 1, 1) = '#' then Delete(T, 1, 1)
  else if (Copy(LowerCase(T), 1, 2) = '0x') then Delete(T, 1, 2)
  else if Copy(T, 1, 1) = '$' then Delete(T, 1, 1);
  if not TryStrToInt64('$' + T, n) then
  begin
    Inc(Bad);
    Exit;
  end;
  if (n < 0) or (n > $FFFFFF) then
  begin
    Inc(Bad);
    Exit;
  end;
  Result := Integer(n);
end;

{ An empty value says nothing and keeps the default. Text that is no number used to become
  Def - 1, which was taken without a word whenever it was inside the range ("SyncSeconds = x"
  gave 2); now it is counted and the default is kept, like a number out of range. }
function SettingInt(const V: string; Def, Lo, Hi: Integer; var Bad: Integer): Integer;
var
  T: string;
begin
  Result := Def;
  T := Trim(V);
  if T = '' then Exit;
  if not TryStrToInt(T, Result) or (Result < Lo) or (Result > Hi) then
  begin
    Inc(Bad);
    Result := Def;
  end;
end;

{ a password or a key: the spaces around it are not part of it, and one that has to begin or
  end with a space is written in double quotes }
function SettingSecret(const V: string): string;
begin
  Result := Trim(V);
  if (Length(Result) >= 2) and (Result[1] = '"') and (Result[Length(Result)] = '"') then
    Result := Copy(Result, 2, Length(Result) - 2);
end;

{ a Steam Web API key is letters and digits (Steam hands out 32 of them) }
function SettingKeyOk(const S: string): Boolean;
var
  k: Integer;
begin
  Result := True;
  for k := 1 to Length(S) do
    if not (S[k] in ['0'..'9', 'A'..'Z', 'a'..'z']) then
    begin
      Result := False;
      Exit;
    end;
end;

{ settings.ini: sections are only a comment here, the key alone decides }
procedure ApplySetting(const Key, Value: string; var Bad, Unknown: Integer);
var
  K: string;
begin
  K := LowerCase(Trim(Key));
  if K = 'databasefile' then
    Settings.DatabaseFile := {$IFDEF UNIX}StringReplace(Trim(Value), '\', '/', [rfReplaceAll]){$ELSE}Trim(Value){$ENDIF}
  else if K = 'saveintervalminutes' then Settings.SaveMinutes := SettingInt(Value, DEF_SAVE_MINUTES, 1, 1440, Bad)
  else if K = 'resultsperpage' then Settings.ResultsPerPage := SettingInt(Value, DEF_RESULTS_PER_PAGE, 3, 30, Bad)
  else if K = 'tasktimeoutseconds' then Settings.TaskTimeoutSeconds := SettingInt(Value, DEF_TASK_TIMEOUT_SECONDS, 5, 600, Bad)
  else if K = 'locatetimeoutseconds' then Settings.LocateTimeoutSeconds := SettingInt(Value, DEF_LOCATE_TIMEOUT_SECONDS, 5, 600, Bad)
  else if K = 'admincommands' then Settings.AdminCommands := SettingBool(Value, True, Bad)
  else if K = 'playercommands' then Settings.PlayerCommands := SettingBool(Value, False, Bad)
  else if K = 'chatcommands' then Settings.ChatCommands := SettingBool(Value, False, Bad)
  else if K = 'enabled' then Settings.GeoEnabled := SettingBool(Value, True, Bad)
  else if K = 'maxlookups' then Settings.GeoMaxLookups := SettingInt(Value, DEF_GEO_MAX_LOOKUPS, 1, 32, Bad)
  else if K = 'retryseconds' then Settings.GeoRetrySeconds := SettingInt(Value, DEF_GEO_RETRY_SECONDS, 10, 86400, Bad)
  else if K = 'timeoutms' then Settings.GeoTimeoutMs := SettingInt(Value, DEF_GEO_TIMEOUT_MS, 500, 60000, Bad)
  else if K = 'refreshdays' then Settings.GeoRefreshDays := SettingInt(Value, DEF_GEO_REFRESH_DAYS, 1, 3650, Bad)
  else if K = 'geokeepdays' then Settings.GeoKeepDays := SettingInt(Value, DEF_GEO_KEEP_DAYS, 0, 3650, Bad)
  else if K = 'geotextplain' then Settings.GeoTextPlain := SettingBool(Value, False, Bad)
  else if K = 'visitkeepcount' then Settings.VisitKeepCount := SettingInt(Value, DEF_VISIT_KEEP_COUNT, 0, MAX_VISITS_HARD, Bad)
  else if K = 'visitkeepdays' then Settings.VisitKeepDays := SettingInt(Value, DEF_VISIT_KEEP_DAYS, 0, 3650, Bad)
  else if K = 'visitcooldownminutes' then Settings.VisitCooldownMinutes := SettingInt(Value, DEF_VISIT_COOLDOWN_MINUTES, 0, 1440, Bad)
  else if K = 'lastvisitsrows' then Settings.LastVisitsRows := SettingInt(Value, 500, 10, 5000, Bad)
  else if K = 'commandcooldownseconds' then Settings.CommandCooldownSeconds := SettingInt(Value, 3, 0, 120, Bad)
  else if K = 'heavycooldownseconds' then Settings.HeavyCooldownSeconds := SettingInt(Value, 10, 0, 600, Bad)
  else if K = 'maxlistspersecond' then Settings.MaxListsPerSecond := SettingInt(Value, 3, 1, 60, Bad)
  else if K = 'publiccommands' then Settings.PublicCommands := SettingBool(Value, True, Bad)
  else if K = 'trace' then Settings.Trace := SettingBool(Value, False, Bad)
  else if K = 'linespertick' then Settings.LinesPerTick := SettingInt(Value, 4, 1, 30, Bad)
  else if K = 'maxqueuedlines' then Settings.MaxQueuedLines := SettingInt(Value, 200, 20, 2000, Bad)
  else if K = 'activedays' then Settings.ActiveDays := SettingInt(Value, DEF_ACTIVE_DAYS, 0, 3650, Bad)
  else if K = 'journalseconds' then Settings.JournalSeconds := SettingInt(Value, 10, 0, 3600, Bad)
  else if K = 'engine' then
  begin
    if LowerCase(Trim(Value)) = 'mirror' then Settings.MysqlEngine := ENGINE_MIRROR
    else if (LowerCase(Trim(Value)) = 'mysql') or (LowerCase(Trim(Value)) = 'shared') then Settings.MysqlEngine := ENGINE_MYSQL
    else if (LowerCase(Trim(Value)) = 'local') or (LowerCase(Trim(Value)) = 'bin') or (LowerCase(Trim(Value)) = 'file') then
      Settings.MysqlEngine := ENGINE_BIN
    else begin Settings.MysqlEngine := ENGINE_BIN; if Trim(Value) <> '' then Inc(Bad); end;
  end
  else if K = 'mysqlhost' then Settings.MysqlHost := Trim(Value)
  else if K = 'mysqlport' then Settings.MysqlPort := SettingInt(Value, 3306, 1, 65535, Bad)
  else if K = 'mysqldatabase' then Settings.MysqlDatabase := Trim(Value)
  else if K = 'mysqluser' then Settings.MysqlUser := Trim(Value)
  else if K = 'mysqlpassword' then Settings.MysqlPassword := SettingSecret(Value)
  else if K = 'mysqlserverid' then Settings.MysqlServerId := SettingInt(Value, 1, 1, 100000, Bad)
  else if K = 'mysqlminutes' then Settings.MysqlMinutes := SettingInt(Value, 60, 1, 10080, Bad)
  else if K = 'syncseconds' then Settings.SyncSeconds := SettingInt(Value, 3, 1, 60, Bad)
  else if K = 'mysqlcodepage' then Settings.MysqlCodepage := SettingInt(Value, 1250, 0, 65535, Bad)
  else if K = 'sharedsearch' then Settings.SharedSearch := SettingBool(Value, False, Bad)
  else if K = 'sharedlinks' then Settings.SharedLinks := SettingBool(Value, False, Bad)
  else if K = 'colormain' then Settings.ColMain := SettingColor(Value, $F0E68C, Bad)
  else if K = 'colorinfo' then Settings.ColInfo := SettingColor(Value, $87CEEB, Bad)
  else if K = 'colordata' then Settings.ColData := SettingColor(Value, $C0C0C0, Bad)
  else if K = 'colorwarning' then Settings.ColWarn := SettingColor(Value, $FFD700, Bad)
  else if K = 'colorerror' then Settings.ColError := SettingColor(Value, $FF6347, Bad)
  else if K = 'colorgood' then Settings.ColGood := SettingColor(Value, $66CC66, Bad)
  else if K = 'backuphours' then Settings.BackupHours := SettingInt(Value, 24, 0, 8760, Bad)
  else if K = 'backupkeep' then Settings.BackupKeep := SettingInt(Value, 7, 1, 100, Bad)
  else if K = 'playervisits' then Settings.CmdVisits := SettingScope(Value, 1, Bad)
  else if K = 'playervisitstats' then Settings.CmdVisitStats := SettingScope(Value, 2, Bad)
  else if K = 'playervisitsof' then Settings.CmdVisitsOf := SettingScope(Value, 0, Bad)
  else if K = 'playerstatus' then Settings.CmdStatus := SettingScope(Value, 1, Bad)
  else if K = 'playernames' then Settings.CmdNames := SettingScope(Value, 1, Bad)
  else if K = 'playersteam' then Settings.CmdStopSteam := SettingScope(Value, 1, Bad)
  else if K = 'playerlocate' then Settings.CmdLocate := SettingScope(Value, 1, Bad)
  else if K = 'playerprofiles' then Settings.CmdProfiles := SettingScope(Value, 0, Bad)
  else if K = 'playersearch' then Settings.CmdSearch := SettingScope(Value, 0, Bad)
  else if K = 'playermulti' then Settings.CmdMulti := SettingScope(Value, 0, Bad)
  else if K = 'playerhelp' then Settings.CmdHelp := SettingScope(Value, 2, Bad)
  else if K = 'playerlastvisitors' then Settings.CmdLastVisitors := SettingScope(Value, 2, Bad)
  else if K = 'multiaccountnotice' then Settings.MultiNotice := SettingBool(Value, True, Bad)
  else if K = 'multiaccountminlinks' then Settings.MultiMinLinks := SettingInt(Value, 1, 1, 100, Bad)
  else if K = 'multiaccountbyaddress' then Settings.MultiByAddress := SettingBool(Value, True, Bad)
  else if K = 'multiaccountmaxlinks' then Settings.MultiMaxLinks := SettingInt(Value, 25, 1, 500, Bad)
  else if K = 'multiaccountreportscore' then Settings.MultiReportScore := SettingInt(Value, 55, 1, 100, Bad)
  { a pair has four signals at most (computer, Steam, address or network, name): 5 and 6 asked
    for more than there is, and nobody was ever reported; they mean all four }
  else if K = 'multiaccountminsignals' then Settings.MultiMinSignals := Min(4, SettingInt(Value, 1, 1, 6, Bad))
  else if K = 'multiaccountmaxshared' then Settings.MultiMaxShared := SettingInt(Value, 20, 2, 1000, Bad)
  else if K = 'multiaccountmaxsharedcomputer' then Settings.MultiMaxSharedPC := SettingInt(Value, 200, 2, 5000, Bad)
  { the names of the keys are read without their section, so the Steam ones carry it }
  { a key in double quotes (as a password may be written) was taken with its quotes. One with
    anything but letters and digits in it would change the address curl asks: it is counted
    with the values out of range and left out (the Steam part stays off) - and never shown. }
  else if K = 'steamapikey' then
  begin
    Settings.SteamKey := SettingSecret(Value);
    if not SettingKeyOk(Settings.SteamKey) then
    begin
      Inc(Bad);
      Settings.SteamKey := '';
    end;
  end
  else if K = 'steamrefreshdays' then Settings.SteamRefreshDays := SettingInt(Value, DEF_STEAM_REFRESH_DAYS, 1, 365, Bad)
  else if K = 'steambatchsize' then Settings.SteamBatch := SettingInt(Value, DEF_STEAM_BATCH, 1, 100, Bad)
  else if K = 'steamcurlpath' then Settings.CurlPath := Trim(Value)
  else if K = 'steamtimeoutseconds' then Settings.CurlTimeoutSeconds := SettingInt(Value, 10, 2, 120, Bad)
  else if K = 'addressdays' then Settings.AddressDays := SettingInt(Value, DEF_ADDRESS_DAYS, 0, 3650, Bad)
  else if K = 'namedays' then Settings.NameDays := SettingInt(Value, DEF_NAME_DAYS, 0, 3650, Bad)
  else if K = 'hwiddays' then Settings.HwidDays := SettingInt(Value, DEF_HWID_DAYS, 0, 3650, Bad)
  else if K = 'maxaddresses' then Settings.MaxAddresses := SettingInt(Value, DEF_MAX_ADDRESSES, 0, 1000, Bad)
  else if K = 'profiledays' then Settings.ProfileDays := SettingInt(Value, DEF_PROFILE_DAYS, 0, 3650, Bad)
  else
    Inc(Unknown);
end;

{ "83.24.17.9" -> "83.24.17.": two addresses of one network start with the same three numbers.
  An address that is not four numbers (or is IPv6) has no network here and is left whole. }
function Net24Of(const IP: string): string;
var
  i, Dots: Integer;
begin
  Result := IP;
  Dots := 0;
  for i := 1 to Length(IP) do
    if IP[i] = '.' then
    begin
      Inc(Dots);
      if Dots = 3 then
      begin
        Result := Copy(IP, 1, i);
        Exit;
      end;
    end;
end;

{ the first entry with this key, or -1: with duplicates the binary search can land in the
  middle of them, so it walks back to the first one }
function IndexFirst(L: TStringList; const Key: string): Integer;
var
  P: Integer;
begin
  Result := -1;
  if (L = nil) or (Key = '') then Exit;
  if not L.Find(Key, P) then Exit;
  while (P > 0) and (L[P - 1] = Key) do Dec(P);
  Result := P;
end;

procedure IndexAdd(L: TStringList; const Key: string; Value: Integer);
begin
  if (L = nil) or (Key = '') then Exit;
  L.AddObject(Key, TObject(PtrInt(Value)));
end;

procedure IndexRemove(L: TStringList; const Key: string; Value: Integer);
var
  P: Integer;
begin
  P := IndexFirst(L, Key);
  while (P >= 0) and (P < L.Count) and (L[P] = Key) do
  begin
    if Integer(PtrInt(L.Objects[P])) = Value then
    begin
      L.Delete(P);
      Exit;
    end;
    Inc(P);
  end;
end;

procedure InitGeoEntry(var Entry: TGeoEntry; IP: string);
begin
  FillChar(Entry, SizeOf(TGeoEntry), 0);
  if Length(IP) > 23 then SetLength(IP, 23);
  Move(IP[1], Entry.IP, Length(IP));
end;

function FindGeoIndex(const IP: string): Integer;
var
  P: Integer;
begin
  Result := -1;
  P := IndexFirst(GeoIndex, IP);
  if P < 0 then Exit;
  Result := Integer(PtrInt(GeoIndex.Objects[P]));
  { a stale entry is no answer: the cache is rebuilt after every load }
  if (Result < 0) or (Result >= GeoCount) or (string(GeoCache[Result].IP) <> IP) then
    Result := -1;
end;

{ the caller holds GeoCritSec }
procedure RebuildGeoIndex;
var
  i: Integer;
begin
  if GeoIndex = nil then Exit;
  GeoIndex.Sorted := False;
  GeoIndex.Clear;
  for i := 0 to GeoCount - 1 do
    if GeoCache[i].IP[0] <> #0 then
      GeoIndex.AddObject(string(GeoCache[i].IP), TObject(PtrInt(i)));
  GeoIndex.Sorted := True;
end;

{ the letters of Latin Extended-A written with the closest plain letter, in codepoint order
  from U+0100; the few that CP1252 really has are handled separately below }
const
  LATIN_A_PLAIN =
    'AaAaAaCcCcCcCcDdDdEeEeEeEeEeGgGgGgGgHhHhIiIiIiIiIiIiJjKkkLlLlLlL' +
    'lLlNnNnNnnNnOoOoOoOoRrRrRrSsSsSsSsTtTtTtUuUuUuUuUuUuWwYyYZzZzZzs';

{ ip-api.com answers in UTF-8 and the game console draws one byte per character, so "Vasterbotten
  County" arrived as mojibake. Text is converted once, when it is stored: the Latin-1 range keeps
  its byte, the handful of characters CP1252 adds get theirs, everything else is written with the
  closest plain letter (and an unknown sign becomes '?'). }
