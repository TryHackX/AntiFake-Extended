{ ---------------------------------------------------------------------------------------
  AntiFake Extended - the copy of the database in MySQL or MariaDB.

  The connection is made by mysqlwire, which speaks the database protocol itself: no client
  library is needed, so the 32-bit server writes into any MySQL or MariaDB, 32- or 64-bit,
  on this machine or on another one.

  Engine = mirror: the file is the database, and every MysqlMinutes (and on /storage mirror)
  a thread writes a full copy of it into the tables, for a website. At start the newer of the
  two is read - the tables when the file is missing or older than the last copy (a new machine,
  a lost disk), otherwise the file. The file is written either way, so neither can be lost to
  the other. (Engine = mysql is one database for every server instead: af_shared.inc.)

  The game never waits for any of it. The copy runs on a thread of its own and takes the
  lock for one profile at a time, only long enough to turn it into the text of its rows; the
  sending happens outside the lock. Every table holds the whole state, so the copy is one
  transaction: a website never sees half of a pass. Several game servers can share one
  database, every row carries the MysqlServerId.
  --------------------------------------------------------------------------------------- }

const
  { the tables; a table that is already there gets the columns it lacks (MyPrepareTable) }
  AF_PROFILE_COLS: array[0..16] of AnsiString = (
    'server_id INT NOT NULL', 'profile_id INT NOT NULL', 'name VARCHAR(64) NOT NULL DEFAULT ''''',
    'steam VARCHAR(32) NULL', 'computer VARCHAR(32) NULL', 'address VARCHAR(46) NULL',
    'registered DATETIME NULL', 'last_played DATETIME NULL', 'time_played BIGINT NOT NULL DEFAULT 0',
    'time_spectator BIGINT NOT NULL DEFAULT 0', 'logins BIGINT NOT NULL DEFAULT 0',
    'names INT NOT NULL DEFAULT 0', 'steams INT NOT NULL DEFAULT 0', 'computers INT NOT NULL DEFAULT 0',
    'addresses INT NOT NULL DEFAULT 0', 'visits INT NOT NULL DEFAULT 0', 'updated_at DATETIME NULL');
  AF_PROFILE_KEYS = 'PRIMARY KEY (server_id, profile_id), KEY idx_name (server_id, name)';
  { the value is compared byte for byte, so "Bob" and "bob" stay two names }
  AF_OTHER_COLS: array[0..11] of AnsiString = (
    'server_id INT NOT NULL', 'profile_id INT NOT NULL', 'kind TINYINT NOT NULL',
    'value VARCHAR(64) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL',
    'logins BIGINT NOT NULL DEFAULT 0', 'time_played BIGINT NOT NULL DEFAULT 0',
    'time_spectator BIGINT NOT NULL DEFAULT 0', 'first_used DATETIME NULL', 'last_used DATETIME NULL',
    'is_current TINYINT NOT NULL DEFAULT 0', 'is_active TINYINT NOT NULL DEFAULT 1', 'updated_at DATETIME NULL');
  AF_OTHER_KEYS = 'PRIMARY KEY (server_id, profile_id, kind, value), KEY idx_value (server_id, kind, value)';
  AF_VISIT_COLS: array[0..10] of AnsiString = (
    'server_id INT NOT NULL', 'profile_id INT NOT NULL', 'started DATETIME NOT NULL',
    'seconds INT NOT NULL DEFAULT 0', 'name VARCHAR(64) NULL', 'address VARCHAR(46) NULL',
    'computer VARCHAR(32) NULL', 'country VARCHAR(4) NULL', 'confidence INT NOT NULL DEFAULT 0',
    'flags INT NOT NULL DEFAULT 0', 'updated_at DATETIME NULL');
  AF_VISIT_KEYS = 'PRIMARY KEY (server_id, profile_id, started), KEY idx_country (server_id, country)';
  AF_STEAM_COLS: array[0..13] of AnsiString = (
    'server_id INT NOT NULL', 'profile_id INT NOT NULL', 'steam_id VARCHAR(24) NOT NULL DEFAULT ''''',
    'persona VARCHAR(64) NULL', 'country VARCHAR(4) NULL', 'created DATETIME NULL',
    'visible TINYINT NOT NULL DEFAULT 0', 'vac_bans INT NOT NULL DEFAULT 0', 'game_bans INT NOT NULL DEFAULT 0',
    'community_ban TINYINT NOT NULL DEFAULT 0', 'trade_ban TINYINT NOT NULL DEFAULT 0',
    'days_since_ban INT NOT NULL DEFAULT 0', 'fetched DATETIME NULL', 'updated_at DATETIME NULL');
  AF_STEAM_KEYS = 'PRIMARY KEY (server_id, profile_id)';
  AF_LINK_COLS: array[0..6] of AnsiString = (
    'server_id INT NOT NULL', 'a INT NOT NULL', 'b INT NOT NULL', 'verdict TINYINT NOT NULL DEFAULT 0',
    'decided DATETIME NULL', 'decided_by VARCHAR(32) NULL', 'updated_at DATETIME NULL');
  AF_LINK_KEYS = 'PRIMARY KEY (server_id, a, b)';
  AF_GEO_COLS: array[0..16] of AnsiString = (
    'server_id INT NOT NULL', 'address VARCHAR(46) NOT NULL', 'continent VARCHAR(32) NULL',
    'continent_code VARCHAR(8) NULL', 'country VARCHAR(64) NULL', 'country_code VARCHAR(8) NULL',
    'region VARCHAR(64) NULL', 'city VARCHAR(64) NULL', 'zip VARCHAR(16) NULL', 'timezone VARCHAR(32) NULL',
    'isp VARCHAR(96) NULL', 'reverse_dns VARCHAR(128) NULL', 'mobile TINYINT NOT NULL DEFAULT 0',
    'proxy TINYINT NOT NULL DEFAULT 0', 'hosting TINYINT NOT NULL DEFAULT 0', 'looked_up DATETIME NULL',
    'updated_at DATETIME NULL');
  AF_GEO_KEYS = 'PRIMARY KEY (server_id, address)';
  { one row per server: when the last whole copy was finished, in local time for people and in
    UTC for the comparison with the file at start (see FileTimeUtc) }
  AF_META_COLS: array[0..5] of AnsiString = (
    'server_id INT NOT NULL', 'written_at DATETIME NULL', 'accounts INT NOT NULL DEFAULT 0',
    'rows_written INT NOT NULL DEFAULT 0', 'program VARCHAR(40) NULL', 'written_utc DATETIME NULL');
  AF_META_KEYS = 'PRIMARY KEY (server_id)';

  AF_PROFILE_HEAD = 'INSERT INTO af_profiles (server_id, profile_id, name, steam, computer, address, ' +
    'registered, last_played, time_played, time_spectator, logins, names, steams, computers, addresses, ' +
    'visits, updated_at) VALUES';
  AF_PROFILE_TAIL = 'ON DUPLICATE KEY UPDATE name = VALUES(name), steam = VALUES(steam), ' +
    'computer = VALUES(computer), address = VALUES(address), registered = VALUES(registered), ' +
    'last_played = VALUES(last_played), time_played = VALUES(time_played), ' +
    'time_spectator = VALUES(time_spectator), logins = VALUES(logins), names = VALUES(names), ' +
    'steams = VALUES(steams), computers = VALUES(computers), addresses = VALUES(addresses), ' +
    'visits = VALUES(visits), updated_at = VALUES(updated_at)';
  AF_OTHER_HEAD = 'INSERT INTO af_other (server_id, profile_id, kind, value, logins, time_played, ' +
    'time_spectator, first_used, last_used, is_current, is_active, updated_at) VALUES';
  AF_OTHER_TAIL = 'ON DUPLICATE KEY UPDATE logins = VALUES(logins), time_played = VALUES(time_played), ' +
    'time_spectator = VALUES(time_spectator), first_used = VALUES(first_used), ' +
    'last_used = VALUES(last_used), is_current = VALUES(is_current), is_active = VALUES(is_active), ' +
    'updated_at = VALUES(updated_at)';
  AF_VISIT_HEAD = 'INSERT INTO af_visits (server_id, profile_id, started, seconds, name, address, ' +
    'computer, country, confidence, flags, updated_at) VALUES';
  AF_VISIT_TAIL = 'ON DUPLICATE KEY UPDATE seconds = VALUES(seconds), name = VALUES(name), ' +
    'address = VALUES(address), computer = VALUES(computer), country = VALUES(country), ' +
    'confidence = VALUES(confidence), flags = VALUES(flags), updated_at = VALUES(updated_at)';
  AF_STEAM_HEAD = 'INSERT INTO af_steam (server_id, profile_id, steam_id, persona, country, created, ' +
    'visible, vac_bans, game_bans, community_ban, trade_ban, days_since_ban, fetched, updated_at) VALUES';
  AF_STEAM_TAIL = 'ON DUPLICATE KEY UPDATE steam_id = VALUES(steam_id), persona = VALUES(persona), ' +
    'country = VALUES(country), created = VALUES(created), visible = VALUES(visible), ' +
    'vac_bans = VALUES(vac_bans), game_bans = VALUES(game_bans), ' +
    'community_ban = VALUES(community_ban), trade_ban = VALUES(trade_ban), ' +
    'days_since_ban = VALUES(days_since_ban), fetched = VALUES(fetched), updated_at = VALUES(updated_at)';
  AF_LINK_HEAD = 'INSERT INTO af_links (server_id, a, b, verdict, decided, decided_by, updated_at) VALUES';
  AF_LINK_TAIL = 'ON DUPLICATE KEY UPDATE verdict = VALUES(verdict), decided = VALUES(decided), ' +
    'decided_by = VALUES(decided_by), updated_at = VALUES(updated_at)';
  AF_GEO_HEAD = 'INSERT INTO af_geo (server_id, address, continent, continent_code, country, ' +
    'country_code, region, city, zip, timezone, isp, reverse_dns, mobile, proxy, hosting, looked_up, ' +
    'updated_at) VALUES';
  AF_GEO_TAIL = 'ON DUPLICATE KEY UPDATE continent = VALUES(continent), ' +
    'continent_code = VALUES(continent_code), country = VALUES(country), ' +
    'country_code = VALUES(country_code), region = VALUES(region), city = VALUES(city), ' +
    'zip = VALUES(zip), timezone = VALUES(timezone), isp = VALUES(isp), ' +
    'reverse_dns = VALUES(reverse_dns), mobile = VALUES(mobile), proxy = VALUES(proxy), ' +
    'hosting = VALUES(hosting), looked_up = VALUES(looked_up), updated_at = VALUES(updated_at)';

  { the tables every whole copy rewrites; rows it did not write again belong to what is gone }
  AF_PRUNED: array[0..5] of AnsiString = ('af_profiles', 'af_other', 'af_visits', 'af_steam',
    'af_links', 'af_geo');

type
  TMirrorThread = class(TThread)
  protected
    procedure Execute; override;
  end;

var
  MirrorBusy: Boolean = False;
  MirrorCS: TRTLCriticalSection;
  MirrorRuns: Integer = 0;
  MirrorRows: Integer = 0;
  MirrorMillis: Int64 = 0;
  MirrorLastError: string = '';
  MirrorLastRun: Double = 0;
  { the last pass that was started, and how many failed in a row: after a failure the next
    try waits 1, 2, 4 ... minutes (at most MysqlMinutes) instead of coming every few seconds }
  MirrorLastTry: Double = 0;
  MirrorFailures: Integer = 0;
  MirrorServer: string = '';
  { who asked with /storage mirror, told when the copy is done (0 = nobody), and their session }
  MirrorAsker: Integer = 0;
  MirrorAskerSession: Integer = 0;
  { the connections that are open now; Cleanup cuts them, so no thread waits out the time limit
    of a database that does not answer }
  LiveConns: TList = nil;

{ ---------------- text for the tables ---------------- }

{ a name a player typed, in the code page of the players, as UTF-8 }
function DbText(const S: string): string;
begin
  Result := CodepageToUtf8(S, Settings.MysqlCodepage);
end;

function FromDbText(const S: string): string;
begin
  Result := Utf8ToCodepage(S, Settings.MysqlCodepage);
end;

{ the location texts are kept in the code page of the console (see Utf8ToConsole) }
function DbGeo(const S: string): string;
begin
  Result := CodepageToUtf8(S, ConsoleCodepage);
end;

function NewMysqlConnection: TMyConnection;
begin
  Result := TMyConnection.Create;
  { without TLS the password goes encrypted with this key, not with one the server sends }
  Result.ServerPublicKey := Settings.MysqlServerKey;
  Result.Host := Settings.MysqlHost;
  Result.Port := Settings.MysqlPort;
  Result.Database := Settings.MysqlDatabase;
  Result.User := Settings.MysqlUser;
  Result.Password := Settings.MysqlPassword;
  EnterCriticalSection(MirrorCS);
  try
    if LiveConns <> nil then LiveConns.Add(Result);
    { Cleanup cuts the connections it finds once (AbortMysqlConnections, after ShuttingDown):
      one made after that is cut at once, or it would wait out the time limit of its server }
    if ShuttingDown then Result.Abort;
  finally LeaveCriticalSection(MirrorCS); end;
end;

{ closes and frees a connection made by NewMysqlConnection }
procedure DropMysqlConnection(var Conn: TMyConnection);
begin
  if Conn = nil then Exit;
  EnterCriticalSection(MirrorCS);
  try
    if LiveConns <> nil then LiveConns.Remove(Conn);
  finally LeaveCriticalSection(MirrorCS); end;
  try
    Conn.Close;
  except
  end;
  FreeAndNil(Conn);
end;

{ the server shuts down: every open connection is cut, and whatever waits on it gives up }
procedure AbortMysqlConnections;
var
  i: Integer;
begin
  EnterCriticalSection(MirrorCS);
  try
    if LiveConns <> nil then
      for i := 0 to LiveConns.Count - 1 do
        TMyConnection(LiveConns[i]).Abort;
  finally LeaveCriticalSection(MirrorCS); end;
end;

procedure PrepareMysqlTables(Conn: TMyConnection);
begin
  MyPrepareTable(Conn, 'af_profiles', AF_PROFILE_COLS, AF_PROFILE_KEYS);
  MyPrepareTable(Conn, 'af_other', AF_OTHER_COLS, AF_OTHER_KEYS);
  MyPrepareTable(Conn, 'af_visits', AF_VISIT_COLS, AF_VISIT_KEYS);
  MyPrepareTable(Conn, 'af_steam', AF_STEAM_COLS, AF_STEAM_KEYS);
  MyPrepareTable(Conn, 'af_links', AF_LINK_COLS, AF_LINK_KEYS);
  MyPrepareTable(Conn, 'af_geo', AF_GEO_COLS, AF_GEO_KEYS);
  MyPrepareTable(Conn, 'af_meta', AF_META_COLS, AF_META_KEYS);
end;

procedure MirrorNote(const Error: string; Rows: Integer; Millis: Int64);
begin
  EnterCriticalSection(MirrorCS);
  try
    MirrorLastError := Error;
    if Error = '' then
    begin
      MirrorRows := Rows;
      MirrorMillis := Millis;
      Inc(MirrorRuns);
      MirrorLastRun := Now;
      MirrorFailures := 0;
    end
    else
      Inc(MirrorFailures);
  finally LeaveCriticalSection(MirrorCS); end;
end;

{ The rows of one profile, as text. The caller holds ProfilesCritSec; this only reads the
  profile and builds strings, which takes a few microseconds. }
procedure ProfileRows(i: Integer; const Sid, Stamp: string; Prof, Other, Visit, LSteam: TStrings);
var
  Kind, j, m, Used: Integer;
  Arr: TBinaryOtherDataArray;
  Logins: Int64;
  Head, Key: string;
  Keys: array of string;
  D: TDateTime;

  function Taken(const K: string): Boolean;
  var
    t: Integer;
  begin
    Result := False;
    for t := 0 to Used - 1 do
      if Keys[t] = K then Exit(True);
  end;

begin
  Head := '(' + Sid + ', ' + IntToStr(i) + ', ';
  Logins := 0;
  if Length(Profiles[i].OtherNames) > 0 then Logins := Profiles[i].OtherNames[0].LoginCount;
  Prof.Add(Head + MyStrMax(DbText(GetProfileActiveName(i)), 64) + ', ' +
    MyStrMax(GetProfileActiveSteam(i), 32) + ', ' + MyStrMax(DbText(GetProfileActiveHWID(i)), 32) + ', ' +
    MyStrMax(GetProfileActiveIP(i), 46) + ', ' + MyDate(Profiles[i].Profile.RegisterTime) + ', ' +
    MyDate(Profiles[i].Profile.LastPlayedTime) + ', ' + MyInt(Profiles[i].Profile.TimePlayed) + ', ' +
    MyInt(Profiles[i].Profile.TimeOnSpectator) + ', ' + MyInt(Logins) + ', ' +
    MyInt(Length(Profiles[i].OtherNames)) + ', ' + MyInt(Length(Profiles[i].OtherSteams)) + ', ' +
    MyInt(Length(Profiles[i].OtherHWIDs)) + ', ' + MyInt(Length(Profiles[i].OtherIPs)) + ', ' +
    MyInt(Length(Profiles[i].Visits)) + ', ' + Stamp + ')');
  for Kind := KIND_NAME to KIND_IP do
  begin
    case Kind of
      KIND_NAME: Arr := Profiles[i].OtherNames;
      KIND_STEAM: Arr := Profiles[i].OtherSteams;
      KIND_HWID: Arr := Profiles[i].OtherHWIDs;
    else Arr := Profiles[i].OtherIPs;
    end;
    for j := 0 to High(Arr) do
    begin
      if GetOtherDataString(Arr[j]) = '' then Continue;
      Other.Add(Head + IntToStr(Kind) + ', ' + MyStrMax(DbText(GetOtherDataString(Arr[j])), 64) + ', ' +
        MyInt(Arr[j].LoginCount) + ', ' + MyInt(Arr[j].TimePlayed) + ', ' + MyInt(Arr[j].TimeOnSpectator) + ', ' +
        MyDate(Arr[j].FirstUsedDate) + ', ' + MyDate(Arr[j].LastUsedDate) + ', ' + MyInt(Arr[j].LastUsed) + ', ' +
        MyInt(Arr[j].IsActive) + ', ' + Stamp + ')');
    end;
  end;
  Used := 0;
  SetLength(Keys, Length(Profiles[i].Visits));
  for j := 0 to High(Profiles[i].Visits) do
    with Profiles[i].Visits[j] do
    begin
      { the start is the key of a visit; one without a real date cannot be stored }
      Key := MyDate(Started);
      if Key = 'NULL' then Continue;
      { The key holds whole seconds: a second visit of the account that started in the same second
        took the place of the first, and a database read back from the copy had one visit less.
        The later one goes in as many whole seconds later as it takes (the copy only; the file
        keeps the real start). }
      D := Started;
      m := 0;
      while Taken(Key) and (m < MAX_VISITS_HARD + 1) do
      begin
        Inc(m);
        D := Started + m / 86400.0;
        Key := MyDate(D);
      end;
      if (Key = 'NULL') or Taken(Key) then Continue;
      Keys[Used] := Key;
      Inc(Used);
      Visit.Add(Head + Key + ', ' + MyInt(Seconds) + ', ' + MyStrMax(DbText(string(Name)), 64) + ', ' +
        MyStrMax(string(IP), 46) + ', ' + MyStrMax(DbText(string(Hwid)), 32) + ', ' +
        MyStrMax(string(CountryCode), 4) + ', ' + MyInt(Confidence) + ', ' + MyInt(Flags) + ', ' + Stamp + ')');
    end;
  with Profiles[i].Steam do
    if Fetched > 0 then
      { the name on Steam is kept in the code page of the console, like the places }
      LSteam.Add(Head + MyStrMax(string(SteamId), 24) + ', ' + MyStrMax(DbGeo(string(Persona)), 64) + ', ' +
        MyStrMax(string(Country), 4) + ', ' + MyDate(Created) + ', ' + MyInt(Visible) + ', ' + MyInt(VacBans) + ', ' +
        MyInt(GameBans) + ', ' + MyInt(CommunityBan) + ', ' + MyInt(TradeBan) + ', ' + MyInt(DaysSinceLastBan) + ', ' +
        MyDate(Fetched) + ', ' + Stamp + ')');
end;

procedure TMirrorThread.Execute;
var
  Conn: TMyConnection;
  Prof, Other, Visit, Steam, Link, Geo: TMyBatch;
  LProf, LOther, LVisit, LSteam: TStringList;
  i, j, Count, Rows, Asker, Session, VerdN, GeoN: Integer;
  Sid, Stamp, StampUtc, Err, Server: string;
  StampTime: TDateTime;
  Started: QWord;
  Aborted: Boolean;
  VerdCopy: TVerdictArray;
  GeoCopy: array of TGeoEntry;

  procedure Pour(L: TStringList; B: TMyBatch);
  var
    k: Integer;
  begin
    for k := 0 to L.Count - 1 do B.Add(L[k]);
    Inc(Rows, L.Count);
    L.Clear;
  end;

begin
  GuardWorkerThread;
  Conn := nil;
  Prof := nil; Other := nil; Visit := nil; Steam := nil; Link := nil; Geo := nil;
  LProf := TStringList.Create;
  LOther := TStringList.Create;
  LVisit := TStringList.Create;
  LSteam := TStringList.Create;
  Rows := 0;
  Count := 0;
  Err := '';
  Aborted := False;
  Started := GetTickCount64;
  Sid := IntToStr(Settings.MysqlServerId);
  StampTime := Now;
  Stamp := MyDate(StampTime);
  StampUtc := MyDate(LocalTimeToUniversal(StampTime));
  VerdCopy := nil;
  GeoCopy := nil;
  try
    try
      Conn := NewMysqlConnection;
      Conn.Connect;
      Server := Conn.ServerVersion;
      EnterCriticalSection(MirrorCS);
      try
        MirrorServer := Server;
      finally LeaveCriticalSection(MirrorCS); end;
      PrepareMysqlTables(Conn);
      Conn.Exec('START TRANSACTION');
      Prof := TMyBatch.Create(Conn, AF_PROFILE_HEAD, AF_PROFILE_TAIL);
      Other := TMyBatch.Create(Conn, AF_OTHER_HEAD, AF_OTHER_TAIL);
      Visit := TMyBatch.Create(Conn, AF_VISIT_HEAD, AF_VISIT_TAIL);
      Steam := TMyBatch.Create(Conn, AF_STEAM_HEAD, AF_STEAM_TAIL);
      Link := TMyBatch.Create(Conn, AF_LINK_HEAD, AF_LINK_TAIL);
      Geo := TMyBatch.Create(Conn, AF_GEO_HEAD, AF_GEO_TAIL);

      LockProfiles;
      try
        Count := ProfileCount;
      finally UnlockProfiles; end;
      for i := 0 to Count - 1 do
      begin
        if Terminated or ShuttingDown then begin Aborted := True; Break; end;
        LockProfiles;
        try
          if i >= ProfileCount then Break;
          if not ProfileIsEmpty(i) then ProfileRows(i, Sid, Stamp, LProf, LOther, LVisit, LSteam);
        finally UnlockProfiles; end;
        Pour(LProf, Prof);
        Pour(LOther, Other);
        Pour(LVisit, Visit);
        Pour(LSteam, Steam);
      end;

      { what the admins decided about pairs of accounts: copied under the lock, turned into text
        outside it }
      if not Aborted then
      begin
        LockProfiles;
        try
          VerdN := VerdictCount;
          SetLength(VerdCopy, VerdN);
          for i := 0 to VerdN - 1 do VerdCopy[i] := Verdicts[i];
        finally UnlockProfiles; end;
        for i := 0 to VerdN - 1 do
          LProf.Add('(' + Sid + ', ' + MyInt(VerdCopy[i].A) + ', ' + MyInt(VerdCopy[i].B) + ', ' +
            MyInt(VerdCopy[i].Verdict) + ', ' + MyDate(VerdCopy[i].Decided) + ', ' +
            MyStrMax(DbText(string(VerdCopy[i].By)), 32) + ', ' + Stamp + ')');
        VerdCopy := nil;
        Pour(LProf, Link);
      end;

      { and where the addresses are: the same, so a join never waits for the text of a big cache }
      if not Aborted then
      begin
        EnterCriticalSection(GeoCritSec);
        try
          GeoN := GeoCount;
          SetLength(GeoCopy, GeoN);
          for i := 0 to GeoN - 1 do GeoCopy[i] := GeoCache[i];
        finally LeaveCriticalSection(GeoCritSec); end;
        for i := 0 to GeoN - 1 do
          with GeoCopy[i] do
          begin
            if string(IP) = '' then Continue;
            LProf.Add('(' + Sid + ', ' + MyStrMax(string(IP), 46) + ', ' +
              MyStrMax(DbGeo(string(Continent)), 32) + ', ' + MyStrMax(DbGeo(string(ContinentCode)), 8) + ', ' +
              MyStrMax(DbGeo(string(Country)), 64) + ', ' + MyStrMax(DbGeo(string(CountryCode)), 8) + ', ' +
              MyStrMax(DbGeo(string(Region)), 64) + ', ' + MyStrMax(DbGeo(string(City)), 64) + ', ' +
              MyStrMax(DbGeo(string(Zip)), 16) + ', ' + MyStrMax(DbGeo(string(Timezone)), 32) + ', ' +
              MyStrMax(DbGeo(string(ISP)), 96) + ', ' + MyStrMax(DbGeo(string(ReverseDNS)), 128) + ', ' +
              MyInt(IsMobile) + ', ' + MyInt(IsProxy) + ', ' + MyInt(IsHosting) + ', ' +
              MyDate(LastUpdated) + ', ' + Stamp + ')');
          end;
        GeoCopy := nil;
        Pour(LProf, Geo);
      end;

      Prof.Flush; Other.Flush; Visit.Flush; Steam.Flush; Link.Flush; Geo.Flush;

      if Aborted then
        Conn.Exec('ROLLBACK')
      else
      begin
        { rows this pass did not write again belong to accounts, entries and visits that are gone }
        for j := 0 to High(AF_PRUNED) do
          Conn.Exec('DELETE FROM ' + AF_PRUNED[j] + ' WHERE server_id = ' + Sid + ' AND (updated_at < ' +
            Stamp + ' OR updated_at IS NULL)');
        Conn.Exec('INSERT INTO af_meta (server_id, written_at, accounts, rows_written, program, written_utc) ' +
          'VALUES (' + Sid + ', ' + Stamp + ', ' + MyInt(Count) + ', ' + MyInt(Rows) + ', ' +
          MyStr('AntiFake ' + AF_VERSION) + ', ' + StampUtc + ') ON DUPLICATE KEY UPDATE ' +
          'written_at = VALUES(written_at), accounts = VALUES(accounts), rows_written = VALUES(rows_written), ' +
          'program = VALUES(program), written_utc = VALUES(written_utc)');
        Conn.Exec('COMMIT');
      end;
    except
      on E: Exception do
      begin
        Err := E.Message;
        try
          if (Conn <> nil) and Conn.Connected then Conn.Exec('ROLLBACK');
        except
        end;
      end;
    end;
  finally
    Prof.Free; Other.Free; Visit.Free; Steam.Free; Link.Free; Geo.Free;
    LProf.Free; LOther.Free; LVisit.Free; LSteam.Free;
    DropMysqlConnection(Conn);
    if ShuttingDown and (Err <> '') then Aborted := True;
    if not Aborted then MirrorNote(Err, Rows, GetTickCount64 - Started);
    EnterCriticalSection(MirrorCS);
    try
      Asker := MirrorAsker;
      Session := MirrorAskerSession;
      MirrorAsker := 0;
    finally LeaveCriticalSection(MirrorCS); end;
    if Asker > 0 then
    begin
      if Aborted then
        NoticeFor(Asker, Session, Settings.ColWarn, 'The copy into MySQL was stopped: the server is shutting down.')
      else if Err = '' then
        NoticeFor(Asker, Session, Settings.ColGood, Format('The copy in MySQL is written: %d accounts, %d rows in %.1f s.',
          [Count, Rows, (GetTickCount64 - Started) / 1000]))
      else
        NoticeFor(Asker, Session, Settings.ColError, 'The copy into MySQL failed: ' + Err);
    end;
    { the very last thing: from here on another pass may start }
    EnterCriticalSection(MirrorCS);
    try
      MirrorBusy := False;
    finally LeaveCriticalSection(MirrorCS); end;
  end;
end;

{ ---------------- the database as the source ----------------

  Engine = mysql, at start, on the load thread: the newer of the copy in the tables and the
  file is read. True when the tables were read and are in memory now; False leaves the work
  to the file, and LoadNote says why. Nothing in memory is touched until everything has been
  read. }

procedure SetFreshHwid(var P: TProfileData; const V: string);
var
  Len: Integer;
begin
  FillChar(P.Profile.ActiveHWID, SizeOf(P.Profile.ActiveHWID), 0);
  Len := Length(V);
  if Len >= SizeOf(P.Profile.ActiveHWID) then Len := SizeOf(P.Profile.ActiveHWID) - 1;
  if Len > 0 then Move(V[1], P.Profile.ActiveHWID, Len);
  P.Profile.ActiveHWIDLen := Len;
end;

procedure SetFreshIP(var P: TProfileData; const V: string);
var
  Len: Integer;
begin
  FillChar(P.Profile.ActiveIP, SizeOf(P.Profile.ActiveIP), 0);
  Len := Length(V);
  if Len >= SizeOf(P.Profile.ActiveIP) then Len := SizeOf(P.Profile.ActiveIP) - 1;
  if Len > 0 then Move(V[1], P.Profile.ActiveIP, Len);
  P.Profile.ActiveIPLen := Len;
end;

{ how new the file is, with its journal, in UTC; 0 when there is no file (a journal alone
  cannot be read, so it does not count) }
function NewestFileTime(const Path: string): TDateTime;
var
  T: TDateTime;
begin
  Result := 0;
  if not FileTimeUtc(Path, T) then Exit;
  Result := T;
  if FileTimeUtc(JournalNameFor(Path), T) then
    if T > Result then Result := T;
end;

{ Force: the file cannot be read, so the copy is taken whatever its age }
function TryMysqlLoad(const FilePath: string; Force: Boolean): Boolean;
var
  Conn: TMyConnection;
  Cur: TMyCursor;
  Fresh: TProfileArray;
  FreshVerdicts, Kept: TVerdictArray;
  FreshGeo: array of TGeoEntry;
  Count, Idx, i, VerdN, GeoN, KeptN: Integer;
  Written, WrittenUtc, FileTime: TDateTime;
  Sid: string;
begin
  Result := False;
  Fresh := nil;
  FreshVerdicts := nil;
  Kept := nil;
  FreshGeo := nil;
  if Settings.MysqlHost = '' then
  begin
    LoadNote := 'Engine = mirror, but no MysqlHost is set: the file is read';
    Exit;
  end;
  Sid := IntToStr(Settings.MysqlServerId);
  Conn := NewMysqlConnection;
  try
    try
      Conn.Connect;
      PrepareMysqlTables(Conn);
      { the six tables are read in one snapshot, so a copy written meanwhile is not half in it }
      Conn.Exec('START TRANSACTION WITH CONSISTENT SNAPSHOT');
      Written := 0;
      WrittenUtc := 0;
      Count := 0;
      Cur := Conn.Open('SELECT written_at, accounts, written_utc FROM af_meta WHERE server_id = ' + Sid);
      try
        if Cur.Next then
        begin
          Written := Cur.Date(0);
          Count := Cur.Int(1);
          WrittenUtc := Cur.Date(2);
        end;
      finally Cur.Free; end;
      if Written <= 0 then
      begin
        LoadNote := 'MySQL has no copy for server ' + Sid + ' yet: the file is read';
        Exit;
      end;
      if not Force then
      begin
        { UTC on both sides: the local time of a file moves with the summer time }
        if WrittenUtc <= 0 then
        begin
          LoadNote := 'the copy in MySQL does not say when it was written (UTC): the file is read';
          Exit;
        end;
        FileTime := NewestFileTime(FilePath);
        if FileTime >= WrittenUtc then
        begin
          LoadNote := 'the file is newer than the copy in MySQL (' + FormatDateTime('yyyy-mm-dd hh:nn', Written) +
            '): the file is read';
          Exit;
        end;
      end;

      { the highest account number decides the size; accounts are numbered by their place }
      Cur := Conn.Open('SELECT COALESCE(MAX(profile_id), -1) FROM af_profiles WHERE server_id = ' + Sid);
      try
        if Cur.Next then Count := Cur.Int(0) + 1 else Count := 0;
      finally Cur.Free; end;
      if (Count <= 0) or (Count > MAX_PROFILES_HARD) then
      begin
        LoadNote := 'the copy in MySQL holds no usable accounts: the file is read';
        Exit;
      end;
      SetLength(Fresh, Count);
      for i := 0 to Count - 1 do
        FillChar(Fresh[i].Profile, SizeOf(TBinaryProfile), 0);

      Cur := Conn.Open('SELECT profile_id, name, steam, computer, address, registered, last_played, ' +
        'time_played, time_spectator FROM af_profiles WHERE server_id = ' + Sid);
      try
        while Cur.Next do
        begin
          Idx := Cur.Int(0);
          if (Idx < 0) or (Idx >= Count) then Continue;
          SetProfileName(Fresh[Idx], FromDbText(Cur.Str(1)));
          SetProfileSteam(Fresh[Idx], Cur.Str(2));
          SetFreshHwid(Fresh[Idx], FromDbText(Cur.Str(3)));
          SetFreshIP(Fresh[Idx], Cur.Str(4));
          Fresh[Idx].Profile.RegisterTime := Cur.Date(5);
          Fresh[Idx].Profile.LastPlayedTime := Cur.Date(6);
          Fresh[Idx].Profile.TimePlayed := Cur.Int(7);
          Fresh[Idx].Profile.TimeOnSpectator := Cur.Int(8);
        end;
      finally Cur.Free; end;

      Cur := Conn.Open('SELECT profile_id, kind, value, logins, time_played, time_spectator, first_used, ' +
        'last_used, is_current, is_active FROM af_other WHERE server_id = ' + Sid +
        ' ORDER BY profile_id, kind, first_used');
      try
        while Cur.Next do
        begin
          Idx := Cur.Int(0);
          if (Idx < 0) or (Idx >= Count) then Continue;
          AddOtherFromDb(Fresh[Idx], Cur.Int(1), FromDbText(Cur.Str(2)), Cur.Int(3), Cur.Int(4), Cur.Int(5),
            Cur.Date(6), Cur.Date(7), Cur.Int(8), Cur.Int(9));
        end;
      finally Cur.Free; end;

      Cur := Conn.Open('SELECT profile_id, started, seconds, name, address, computer, country, confidence, ' +
        'flags FROM af_visits WHERE server_id = ' + Sid + ' ORDER BY profile_id, started');
      try
        while Cur.Next do
        begin
          Idx := Cur.Int(0);
          if (Idx < 0) or (Idx >= Count) then Continue;
          AddVisitFromDb(Fresh[Idx], Cur.Date(1), Cur.Int(2), FromDbText(Cur.Str(3)), Cur.Str(4),
            FromDbText(Cur.Str(5)), Cur.Str(6), Cur.Int(7), Cur.Int(8));
        end;
      finally Cur.Free; end;

      Cur := Conn.Open('SELECT profile_id, steam_id, persona, country, created, visible, vac_bans, game_bans, ' +
        'community_ban, trade_ban, days_since_ban, fetched FROM af_steam WHERE server_id = ' + Sid);
      try
        while Cur.Next do
        begin
          Idx := Cur.Int(0);
          if (Idx < 0) or (Idx >= Count) then Continue;
          with Fresh[Idx].Steam do
          begin
            FillChar(Fresh[Idx].Steam, SizeOf(TSteamInfo), 0);
            StrPLCopy(@SteamId[0], Cur.Str(1), SizeOf(SteamId) - 1);
            { UTF-8 in the table, the code page of the console in memory }
            SetGeoString(Persona, Cur.Str(2));
            StrPLCopy(@Country[0], Cur.Str(3), SizeOf(Country) - 1);
            Created := Cur.Date(4);
            Visible := Cur.Int(5);
            VacBans := Cur.Int(6);
            GameBans := Cur.Int(7);
            CommunityBan := Cur.Int(8);
            TradeBan := Cur.Int(9);
            DaysSinceLastBan := Cur.Int(10);
            Fetched := Cur.Date(11);
          end;
        end;
      finally Cur.Free; end;

      VerdN := 0;
      Cur := Conn.Open('SELECT a, b, verdict, decided, decided_by FROM af_links WHERE server_id = ' + Sid);
      try
        while Cur.Next do
        begin
          if VerdN >= MAX_VERDICTS then Break;
          if VerdN >= Length(FreshVerdicts) then SetLength(FreshVerdicts, VerdN * 2 + 16);
          FillChar(FreshVerdicts[VerdN], SizeOf(TPairVerdict), 0);
          FreshVerdicts[VerdN].A := Cur.Int(0);
          FreshVerdicts[VerdN].B := Cur.Int(1);
          FreshVerdicts[VerdN].Verdict := Cur.Int(2);
          FreshVerdicts[VerdN].Decided := Cur.Date(3);
          StrPLCopy(@FreshVerdicts[VerdN].By[0], FromDbText(Cur.Str(4)), SizeOf(FreshVerdicts[VerdN].By) - 1);
          Inc(VerdN);
        end;
      finally Cur.Free; end;

      GeoN := 0;
      Cur := Conn.Open('SELECT address, continent, continent_code, country, country_code, region, city, zip, ' +
        'timezone, isp, reverse_dns, mobile, proxy, hosting, looked_up FROM af_geo WHERE server_id = ' + Sid);
      try
        while Cur.Next do
        begin
          if GeoN >= Length(FreshGeo) then SetLength(FreshGeo, GeoN * 2 + 16);
          FillChar(FreshGeo[GeoN], SizeOf(TGeoEntry), 0);
          with FreshGeo[GeoN] do
          begin
            StrPLCopy(@IP[0], Cur.Str(0), SizeOf(IP) - 1);
            { the texts come as UTF-8 and are kept in the console's code page }
            SetGeoString(Continent, Cur.Str(1));
            SetGeoString(ContinentCode, Cur.Str(2));
            SetGeoString(Country, Cur.Str(3));
            SetGeoString(CountryCode, Cur.Str(4));
            SetGeoString(Region, Cur.Str(5));
            SetGeoString(City, Cur.Str(6));
            SetGeoString(Zip, Cur.Str(7));
            SetGeoString(Timezone, Cur.Str(8));
            SetGeoString(ISP, Cur.Str(9));
            SetGeoString(ReverseDNS, Cur.Str(10));
            IsMobile := Cur.Int(11);
            IsProxy := Cur.Int(12);
            IsHosting := Cur.Int(13);
            LastUpdated := Cur.Date(14);
          end;
          if string(FreshGeo[GeoN].IP) <> '' then Inc(GeoN);
        end;
      finally Cur.Free; end;

      { Everything is read: now it replaces what is in memory. It is put right first and the
        lists change hands in one step under the locks: they used to be copied there entry by
        entry, into lists made after the old ones were thrown away - lists that could not be
        had any more (the memory of a 32-bit server) left the server without any. }
      for i := 0 to Count - 1 do MarkProfileFirstUsed(Fresh[i]);
      SetLength(Kept, VerdN + 100);
      KeptN := 0;
      for i := 0 to VerdN - 1 do
        if (FreshVerdicts[i].A >= 0) and (FreshVerdicts[i].A < Count) and
           (FreshVerdicts[i].B >= 0) and (FreshVerdicts[i].B < Count) and
           (FreshVerdicts[i].A <> FreshVerdicts[i].B) then
        begin
          Kept[KeptN] := FreshVerdicts[i];
          Inc(KeptN);
        end;
      SetLength(Fresh, Count + 100);
      SetLength(FreshGeo, GeoN + 100);
      LockProfiles;
      try
        Profiles := Fresh;
        ProfileCount := Count;
        Dirty := nil;
        SetLength(Dirty, Length(Profiles));
        DirtyCount := 0;
        Verdicts := Kept;
        VerdictCount := KeptN;
        RebuildVerdictIndex;
        RebuildProfileIndexes;
      finally UnlockProfiles; end;
      Fresh := nil;
      Kept := nil;
      EnterCriticalSection(GeoCritSec);
      try
        GeoCache := FreshGeo;
        GeoCount := GeoN;
        RebuildGeoIndex;
      finally LeaveCriticalSection(GeoCritSec); end;
      FreshGeo := nil;
      if Force then
        LoadNote := Format('read from MySQL: %d accounts, copy of %s', [Count, FormatDateTime('yyyy-mm-dd hh:nn', Written)])
      else
        LoadNote := Format('read from MySQL: %d accounts, copy of %s (newer than the file)',
          [Count, FormatDateTime('yyyy-mm-dd hh:nn', Written)]);
      Result := True;
    except
      on E: Exception do
        if Force then
          LoadNote := 'the file could not be read, and MySQL could not be read either (' + E.Message + ')'
        else
          LoadNote := 'MySQL could not be read (' + E.Message + '): the file is read';
    end;
  finally
    try
      if Conn.Connected then Conn.Exec('COMMIT');
    except
    end;
    DropMysqlConnection(Conn);
  end;
end;

{ ---------------- what the script and the admins call ---------------- }

{ a pass now, when it is time (or Force); True when one was started }
function StartMirror(Force: Boolean): Boolean;
var
  Wait: Double;
begin
  Result := False;
  if ShuttingDown or not LoadComplete or LoadInProgress or LoadError then Exit;
  if Settings.MysqlEngine <> ENGINE_MIRROR then Exit;
  if Settings.MysqlHost = '' then Exit;
  if not GrabLock(MirrorCS, 5) then Exit;
  try
    if MirrorBusy then Exit;
    if not Force then
    begin
      if MirrorLastRun > 0 then
        if Now - MirrorLastRun < Settings.MysqlMinutes / 1440.0 then Exit;
      if MirrorFailures > 0 then
      begin
        Wait := 1 shl Min(MirrorFailures - 1, 12);
        if Wait > Settings.MysqlMinutes then Wait := Settings.MysqlMinutes;
        if Now - MirrorLastTry < Wait / 1440.0 then Exit;
      end;
    end;
    MirrorBusy := True;
    MirrorLastTry := Now;
  finally LeaveCriticalSection(MirrorCS); end;
  try
    StartBackground(TMirrorThread.Create(True));
    Result := True;
  except
    EnterCriticalSection(MirrorCS);
    try
      MirrorBusy := False;
    finally LeaveCriticalSection(MirrorCS); end;
  end;
end;

{ the script calls this once a minute; the copy decides itself whether it is time }
procedure Mysql_Tick(Force: Boolean); cdecl;
begin
  try
    StartMirror(Force);
  except
    ExportFailed('Mysql_Tick');
  end;
end;

{ /storage mirror: a copy now, and the admin who asked hears how it went }
function StartMirrorFor(Slot: Integer; out Why: string): Boolean;
begin
  Result := False;
  Why := '';
  if Settings.MysqlEngine = ENGINE_MYSQL then
  begin
    Why := 'Engine = mysql: every server works on one database, there is no copy to write.';
    Exit;
  end;
  if Settings.MysqlEngine <> ENGINE_MIRROR then
  begin
    Why := 'The copy into MySQL is off (Engine = local in settings.ini).';
    Exit;
  end;
  if Settings.MysqlHost = '' then
  begin
    Why := 'No MysqlHost in settings.ini.';
    Exit;
  end;
  if LoadError then
  begin
    Why := 'The database could not be read, so nothing is copied from it.';
    Exit;
  end;
  if not LoadComplete or LoadInProgress then
  begin
    Why := 'The database is still being read; try again in a moment.';
    Exit;
  end;
  EnterCriticalSection(MirrorCS);
  try
    if MirrorBusy then
    begin
      Why := 'A copy is being written right now; you hear when it is done.';
      MirrorAsker := Slot;
      MirrorAskerSession := SessionOf(Slot);
      Exit;
    end;
    MirrorAsker := Slot;
    MirrorAskerSession := SessionOf(Slot);
  finally LeaveCriticalSection(MirrorCS); end;
  if not StartMirror(True) then
  begin
    EnterCriticalSection(MirrorCS);
    try
      if MirrorAsker = Slot then MirrorAsker := 0;
    finally LeaveCriticalSection(MirrorCS); end;
    Why := 'The copy could not be started just now; try again in a moment.';
    Exit;
  end;
  Result := True;
end;

{ ---------------- the other servers of the database ----------------

  With SharedSearch or SharedLinks the searches also look into the copies the other servers of
  the database wrote (Engine = mirror there, every MysqlMinutes). The question runs on
  the worker of the search, with a connection of its own for the moment of the question; the
  game never waits for it. The accounts of the other servers are not taken into this one: they
  are listed under the results of this server, with the number of their server. }

const
  FOREIGN_MAX = 100;
  FOREIGN_SELECT = 'SELECT o.server_id, o.profile_id, p.name, o.kind, o.value, o.last_used FROM af_other o ' +
    'JOIN af_profiles p ON p.server_id = o.server_id AND p.profile_id = o.profile_id';
  { what ties two accounts: a Steam account, a computer or an address (a name alone does not) }
  FOREIGN_TIES = (1 shl KIND_STEAM) or (1 shl KIND_HWID) or (1 shl KIND_IP);

function SharedReady: Boolean;
begin
  { with Engine = mysql every account of every server is here already }
  Result := (Settings.SharedSearch or Settings.SharedLinks) and (Trim(Settings.MysqlHost) <> '') and
    (Settings.MysqlEngine <> ENGINE_MYSQL);
end;

{ a text for LIKE: its own % _ and \ are taken as they are }
function LikeText(const S: string): string;
var
  i: Integer;
begin
  Result := '';
  for i := 1 to Length(S) do
  begin
    if S[i] in ['%', '_', '\'] then
      Result := Result + '\';
    Result := Result + S[i];
  end;
end;

{ one account of another server once, with every kind that matched }
procedure CollapseForeign(var Rows: TForeignRows);
var
  i, j, n: Integer;
  Found: Boolean;
begin
  n := 0;
  for i := 0 to High(Rows) do
  begin
    Found := False;
    for j := 0 to n - 1 do
      if (Rows[j].Server = Rows[i].Server) and (Rows[j].Account = Rows[i].Account) then
      begin
        Rows[j].Kinds := Rows[j].Kinds or Rows[i].Kinds;
        if Rows[i].LastSeen > Rows[j].LastSeen then
          Rows[j].LastSeen := Rows[i].LastSeen;
        Found := True;
        Break;
      end;
    if not Found then
    begin
      Rows[n] := Rows[i];
      Inc(n);
    end;
  end;
  SetLength(Rows, n);
end;

function ForeignQuery(const SQL: string; out Rows: TForeignRows; out Why: string): Boolean;
var
  Conn: TMyConnection;
  Cur: TMyCursor;
  n, K: Integer;
begin
  Rows := nil;
  Why := '';
  Result := False;
  Conn := nil;
  n := 0;
  try
    try
      Conn := NewMysqlConnection;
      Conn.IOTimeoutMs := 10000;
      Conn.Connect;
      Cur := Conn.Open(SQL);
      try
        while Cur.Next do
        begin
          SetLength(Rows, n + 1);
          Rows[n].Server := Cur.Int(0);
          Rows[n].Account := Cur.Int(1);
          Rows[n].Name := FromDbText(Cur.Str(2));
          K := Cur.Int(3);
          Rows[n].Kind := K;
          Rows[n].Kinds := 1 shl K;
          if K = KIND_NAME then
            Rows[n].Value := FromDbText(Cur.Str(4))
          else
            Rows[n].Value := Cur.Str(4);
          Rows[n].LastSeen := Cur.Date(5);
          Inc(n);
        end;
      finally
        Cur.Free;
      end;
      CollapseForeign(Rows);
      Result := True;
    except
      on Ex: Exception do
        Why := 'they could not be read (' + Ex.Message + ')';
    end;
  finally
    DropMysqlConnection(Conn);
  end;
end;

function ForeignOthers: string;
begin
  Result := ' WHERE o.server_id <> ' + MyInt(Settings.MysqlServerId);
end;

{ the accounts of the other servers whose name now (or any name, AllNames) contains Text }
function ForeignNames(const Text: string; AllNames: Boolean; out Rows: TForeignRows; out Why: string): Boolean;
var
  Pat: string;
begin
  Pat := MyStr('%' + LikeText(DbText(Trim(Text))) + '%');
  if AllNames then
    Result := ForeignQuery(FOREIGN_SELECT + ForeignOthers + ' AND o.kind = ' + IntToStr(KIND_NAME) +
      ' AND LOWER(o.value) LIKE LOWER(' + Pat + ') ORDER BY o.last_used DESC LIMIT ' + IntToStr(FOREIGN_MAX), Rows, Why)
  else
    Result := ForeignQuery('SELECT p.server_id, p.profile_id, p.name, ' + IntToStr(KIND_NAME) + ', p.name, p.last_played ' +
      'FROM af_profiles p WHERE p.server_id <> ' + MyInt(Settings.MysqlServerId) + ' AND LOWER(p.name) LIKE LOWER(' + Pat +
      ') ORDER BY p.last_played DESC LIMIT ' + IntToStr(FOREIGN_MAX), Rows, Why);
end;

{ the accounts of the other servers that used exactly this value }
function ForeignExact(Kind: Integer; const Value: string; out Rows: TForeignRows; out Why: string): Boolean;
var
  Cond: string;
begin
  if Kind = KIND_NAME then
    Cond := 'LOWER(o.value) = LOWER(' + MyStr(DbText(Trim(Value))) + ')'
  else
    Cond := 'o.value = ' + MyStr(Trim(Value));
  Result := ForeignQuery(FOREIGN_SELECT + ForeignOthers + ' AND o.kind = ' + IntToStr(Kind) + ' AND ' + Cond +
    ' ORDER BY o.last_used DESC LIMIT ' + IntToStr(FOREIGN_MAX), Rows, Why);
end;

{ the accounts of the other servers that used a value of these kinds (1 shl KIND_*) that the
  account Idx of this server used as well }
function ForeignLinks(Idx, KindMask: Integer; out Rows: TForeignRows; out Why: string): Boolean;
var
  K, j: Integer;
  Cond, List, V: string;
begin
  Rows := nil;
  Why := '';
  Cond := '';
  for K := KIND_NAME to KIND_IP do
  begin
    if (KindMask and (1 shl K)) = 0 then
      Continue;
    List := '';
    LockProfiles;
    try
      if (Idx < 0) or (Idx >= ProfileCount) then
        Exit(True);
      case K of
        KIND_NAME:
          for j := 0 to Min(High(Profiles[Idx].OtherNames), 199) do
          begin
            V := MyStr(DbText(GetOtherDataString(Profiles[Idx].OtherNames[j])));
            if List <> '' then List := List + ', ';
            List := List + V;
          end;
        KIND_STEAM:
          for j := 0 to Min(High(Profiles[Idx].OtherSteams), 199) do
          begin
            V := MyStr(GetOtherDataString(Profiles[Idx].OtherSteams[j]));
            if List <> '' then List := List + ', ';
            List := List + V;
          end;
        KIND_HWID:
          for j := 0 to Min(High(Profiles[Idx].OtherHWIDs), 199) do
          begin
            V := MyStr(GetOtherDataString(Profiles[Idx].OtherHWIDs[j]));
            if List <> '' then List := List + ', ';
            List := List + V;
          end;
        KIND_IP:
          for j := 0 to Min(High(Profiles[Idx].OtherIPs), 199) do
          begin
            V := MyStr(GetOtherDataString(Profiles[Idx].OtherIPs[j]));
            if List <> '' then List := List + ', ';
            List := List + V;
          end;
      end;
    finally
      UnlockProfiles;
    end;
    if List = '' then
      Continue;
    if Cond <> '' then
      Cond := Cond + ' OR ';
    Cond := Cond + '(o.kind = ' + IntToStr(K) + ' AND o.value IN (' + List + '))';
  end;
  if Cond = '' then
    Exit(True);
  Result := ForeignQuery(FOREIGN_SELECT + ForeignOthers + ' AND (' + Cond + ') ORDER BY o.last_used DESC LIMIT ' +
    IntToStr(FOREIGN_MAX * 3), Rows, Why);
end;
