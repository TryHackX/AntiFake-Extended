{ ---------------------------------------------------------------------------------------
  AntiFake Extended - Engine = mysql: one database for every game server.

  The tables af_db_profiles, af_db_verdicts and af_db_geo hold the accounts, the decisions of
  the admins and the places of all servers together. Every server keeps the whole of it in
  memory - a join, a search and a list read memory, so nothing ever waits for the database -
  and a thread of its own exchanges the changes with the tables every SyncSeconds:
  - what changed here goes in as a three-way merge (SharedMerge3): the thread locks the row,
    reads what the other servers wrote, puts this server's changes since the last exchange on
    top and writes the result. Counters of several servers add up and nothing is overwritten,
    however many servers write at once. The writers take turns on one row of af_db_meta, which
    also numbers the changes (seq);
  - what the other servers wrote since the last exchange (a higher seq) comes back and goes into
    memory the same way.
  The script holds the numbers of the accounts while the server runs, so an account never moves
  while it runs: the database knows it under a number of its own, and every start gives each
  account the number of the database. A player new on two servers at once becomes one account
  the way a join finds one on a single server: through the Steam account, or without a Steam
  account the database knows, through the computer of an account without Steam. The file stays
  as a cache. Beside it (<file>.shared) is which account here is which in the database, and
  what has not reached the database yet: with that, a server that cannot reach the database
  works from the file and sends what it collected once the database answers; without it the
  run works from the file alone and sends nothing.
  --------------------------------------------------------------------------------------- }

const
  AFS_PROFILE_COLS: array[0..11] of AnsiString = (
    'id INT NOT NULL', 'name VARCHAR(64) NOT NULL DEFAULT ''''', 'steam VARCHAR(32) NULL', 'computer VARCHAR(32) NULL',
    'address VARCHAR(46) NULL', 'last_played DATETIME NULL', 'time_played BIGINT NOT NULL DEFAULT 0',
    'data MEDIUMBLOB NULL', 'version BIGINT NOT NULL DEFAULT 0', 'seq BIGINT NOT NULL DEFAULT 0',
    'updated_by INT NOT NULL DEFAULT 0', 'updated_at DATETIME NULL');
  AFS_PROFILE_KEYS = 'PRIMARY KEY (id), KEY idx_seq (seq), KEY idx_steam (steam), KEY idx_computer (computer), ' +
    'KEY idx_name (name)';
  AFS_VERDICT_COLS: array[0..6] of AnsiString = (
    'a INT NOT NULL', 'b INT NOT NULL', 'verdict TINYINT NOT NULL DEFAULT 0', 'decided DOUBLE NOT NULL DEFAULT 0',
    'decided_by VARCHAR(32) NULL', 'seq BIGINT NOT NULL DEFAULT 0', 'updated_by INT NOT NULL DEFAULT 0');
  AFS_VERDICT_KEYS = 'PRIMARY KEY (a, b), KEY idx_seq (seq)';
  AFS_GEO_COLS: array[0..4] of AnsiString = (
    'address VARCHAR(46) NOT NULL', 'data BLOB NULL', 'looked_up DOUBLE NOT NULL DEFAULT 0',
    'seq BIGINT NOT NULL DEFAULT 0', 'updated_by INT NOT NULL DEFAULT 0');
  AFS_GEO_KEYS = 'PRIMARY KEY (address), KEY idx_seq (seq)';
  AFS_META_COLS: array[0..1] of AnsiString = ('name VARCHAR(40) NOT NULL', 'value BIGINT NOT NULL DEFAULT 0');
  AFS_META_KEYS = 'PRIMARY KEY (name)';

  AFS_ROW_HEAD = 'INSERT INTO af_db_profiles (id, name, steam, computer, address, last_played, time_played, data, version, seq, ' +
    'updated_by, updated_at) VALUES';
  AFS_ROW_TAIL = 'ON DUPLICATE KEY UPDATE name = VALUES(name), steam = VALUES(steam), computer = VALUES(computer), ' +
    'address = VALUES(address), last_played = VALUES(last_played), time_played = VALUES(time_played), data = VALUES(data), ' +
    'version = VALUES(version), seq = VALUES(seq), updated_by = VALUES(updated_by), updated_at = VALUES(updated_at)';
  { a decision and a place: the newer one stays, whoever writes last }
  AFS_VERDICT_HEAD = 'INSERT INTO af_db_verdicts (a, b, verdict, decided, decided_by, seq, updated_by) VALUES';
  AFS_VERDICT_TAIL = 'ON DUPLICATE KEY UPDATE verdict = IF(VALUES(decided) >= decided, VALUES(verdict), verdict), ' +
    'decided_by = IF(VALUES(decided) >= decided, VALUES(decided_by), decided_by), ' +
    'seq = IF(VALUES(decided) >= decided, VALUES(seq), seq), ' +
    'updated_by = IF(VALUES(decided) >= decided, VALUES(updated_by), updated_by), ' +
    'decided = GREATEST(decided, VALUES(decided))';
  AFS_GEO_HEAD = 'INSERT INTO af_db_geo (address, data, looked_up, seq, updated_by) VALUES';
  AFS_GEO_TAIL = 'ON DUPLICATE KEY UPDATE data = IF(VALUES(looked_up) >= looked_up, VALUES(data), data), ' +
    'seq = IF(VALUES(looked_up) >= looked_up, VALUES(seq), seq), ' +
    'updated_by = IF(VALUES(looked_up) >= looked_up, VALUES(updated_by), updated_by), ' +
    'looked_up = GREATEST(looked_up, VALUES(looked_up))';

  AFS_COLLECT = 64;     { profiles packed per hold of the lock }
  AFS_APPLY = 16;       { profiles put into memory per hold of the lock }
  AFS_PAGE = 500;       { rows of the other servers per question }
  SHARED_MAGIC = $48534641;   { "AFSH": the numbers beside the file }
  SHARED_FORMAT = 1;

type
  TSharedItem = record
    Local, Global: Integer;
    Snap, Base: AnsiString;
    Steam, Hwid: string;
  end;
  TSharedItems = array of TSharedItem;

  TSharedAnswer = record
    Local, Global: Integer;
    Snap, Data: AnsiString;
    Version: Int64;
  end;
  TSharedAnswers = array of TSharedAnswer;

  TSharedRow = record
    Global: Integer;
    Data: AnsiString;
    Version: Int64;
  end;
  TSharedRows = array of TSharedRow;

  TSharedGeo = array of TGeoEntry;

  TSharedThread = class(TThread)
  protected
    procedure Execute; override;
  end;

var
  SharedCS: TRTLCriticalSection;
  SharedThread: TSharedThread = nil;
  SharedWake: PRTLEvent = nil;
  SharedStopping: Boolean = False;
  SharedMissed: Integer = 0;          { changes without the state before them; ProfilesCritSec }
  { the rest belongs to SharedCS }
  SharedPullSeq: Int64 = 0;
  SharedOnline: Boolean = False;
  SharedEver: Boolean = False;
  SharedFailures: Integer = 0;
  SharedLastError: string = '';
  SharedRounds: Int64 = 0;
  SharedWritten: Int64 = 0;
  SharedRead: Int64 = 0;
  SharedLastOk: TDateTime = 0;
  SharedPath: string = '';            { the database file; '' = this run works from the file alone }
  SharedIdsReady: Boolean = False;    { the file on disk has the numbers memory has }
  SharedPendingOnDisk: Boolean = False;
  SharedFileSize: Int64 = 0;
  SharedFileTime: Double = 0;
  SharedDiskAt: QWord = 0;

{ ---------------- the numbers: here and in the database ---------------- }

{ the caller holds ProfilesCritSec }
procedure SharedLink(Local, Global: Integer);
var
  n, k: Integer;
begin
  if (Local < 0) or (Global <= 0) then Exit;
  if Local >= Length(SharedGlobal) then SharedGrow(Local);
  SharedGlobal[Local] := Global;
  if Global >= Length(SharedLocal) then
  begin
    n := Length(SharedLocal);
    SetLength(SharedLocal, Global + 1024);
    for k := n to High(SharedLocal) do SharedLocal[k] := -1;
  end;
  SharedLocal[Global] := Local;
end;

function SharedLocalOf(Global: Integer): Integer;
begin
  if (Global > 0) and (Global < Length(SharedLocal)) then Result := SharedLocal[Global] else Result := -1;
end;

function SharedGlobalOf(Local: Integer): Integer;
begin
  if (Local >= 0) and (Local < Length(SharedGlobal)) then Result := SharedGlobal[Local] else Result := 0;
end;

{ nothing known about the database: a start from nothing; the caller holds ProfilesCritSec }
procedure SharedForget;
begin
  SharedGlobal := nil;
  SharedBase := nil;
  SharedVersion := nil;
  SharedFlight := nil;
  SharedDirtyArr := nil;
  SharedLocal := nil;
  SharedVerdictCount := 0;
  SharedGrow(ProfileCount);
end;

function SharedUnpack(const Data: AnsiString; var P: TProfileData): Boolean;
var
  Idx: Integer;
begin
  FillChar(P.Profile, SizeOf(P.Profile), 0);
  P.OtherNames := nil;
  P.OtherSteams := nil;
  P.OtherHWIDs := nil;
  P.OtherIPs := nil;
  P.Visits := nil;
  FillChar(P.Steam, SizeOf(P.Steam), 0);
  Result := (Data <> '') and UnpackProfile(Data, Idx, P);
end;

{ a profile with nothing in it: what the database has of an account it has never seen }
function SharedEmptyData: AnsiString;
var
  P: TProfileData;
begin
  SharedUnpack('', P);
  Result := SharedPackData(P);
end;

function SharedBin(const S: AnsiString): string;
const
  HexDigits: array[0..15] of Char = '0123456789ABCDEF';
var
  i: Integer;
begin
  SetLength(Result, Length(S) * 2 + 3);
  Result[1] := 'X';
  Result[2] := '''';
  for i := 1 to Length(S) do
  begin
    Result[i * 2 + 1] := HexDigits[Ord(S[i]) shr 4];
    Result[i * 2 + 2] := HexDigits[Ord(S[i]) and 15];
  end;
  Result[Length(Result)] := '''';
end;

function GeoBytes(const G: TGeoEntry): AnsiString;
begin
  SetLength(Result, SizeOf(TGeoEntry));
  Move(G, Result[1], SizeOf(TGeoEntry));
end;

{ ---------------- three versions of one profile, merged ---------------- }

{ the entry of a list that holds Key. It is compared where it lies: a copy of every entry it
  passed made the merge of an account with a few hundred names cost milliseconds, and the merges
  run while the lock of the profiles is held }
function FindOther(const Arr: TBinaryOtherDataArray; const Key: string): Integer;
var
  k, n: Integer;
begin
  n := Length(Key);
  if n <= MAX_STRING_LEN then
    for k := 0 to High(Arr) do
      if (Arr[k].DataLen = n) and ((n = 0) or CompareMem(@Arr[k].DataString[0], @Key[1], n)) then
        Exit(k);
  Result := -1;
end;

function SameUse(const A, B: TBinaryOtherData): Boolean;
begin
  Result := (A.LoginCount = B.LoginCount) and (A.TimePlayed = B.TimePlayed) and
    (A.TimeOnSpectator = B.TimeOnSpectator) and (A.LastUsedDate = B.LastUsedDate);
end;

{ a list of names, Steam accounts, computers or addresses: this server's entries in its own order
  with what the other servers added to their uses, then what the other servers added. What one
  side removed stays removed, unless the other one used it since. }
function SharedMergeList(const Cur, Base, Remote: TBinaryOtherDataArray): TBinaryOtherDataArray;
var
  i, b, r, n, Newest: Integer;
  Key: string;
  E: TBinaryOtherData;
begin
  Result := nil;
  SetLength(Result, Length(Cur) + Length(Remote));
  n := 0;
  for i := 0 to High(Cur) do
  begin
    Key := GetOtherDataString(Cur[i]);
    b := FindOther(Base, Key);
    r := FindOther(Remote, Key);
    if r < 0 then
    begin
      { another server threw it away (its cleanup): gone here too, unless it was used here since }
      if (b >= 0) and SameUse(Cur[i], Base[b]) then Continue;
      E := Cur[i];
    end
    else
    begin
      E := Remote[r];
      if b >= 0 then
      begin
        Inc(E.LoginCount, Cur[i].LoginCount - Base[b].LoginCount);
        Inc(E.TimeOnSpectator, Cur[i].TimeOnSpectator - Base[b].TimeOnSpectator);
        Inc(E.TimePlayed, Cur[i].TimePlayed - Base[b].TimePlayed);
        if Cur[i].IsActive <> Base[b].IsActive then E.IsActive := Cur[i].IsActive;
        if Cur[i].LastUsed <> Base[b].LastUsed then E.LastUsed := Cur[i].LastUsed;
      end
      else
      begin
        { new here and there: the uses of both count }
        Inc(E.LoginCount, Cur[i].LoginCount);
        Inc(E.TimeOnSpectator, Cur[i].TimeOnSpectator);
        Inc(E.TimePlayed, Cur[i].TimePlayed);
        if Cur[i].IsActive <> 0 then E.IsActive := Cur[i].IsActive;
        if Cur[i].LastUsedDate >= E.LastUsedDate then E.LastUsed := Cur[i].LastUsed;
      end;
      if (Cur[i].FirstUsedDate > 0) and ((E.FirstUsedDate <= 0) or (Cur[i].FirstUsedDate < E.FirstUsedDate)) then
        E.FirstUsedDate := Cur[i].FirstUsedDate;
      if Cur[i].LastUsedDate > E.LastUsedDate then E.LastUsedDate := Cur[i].LastUsedDate;
    end;
    Result[n] := E;
    Inc(n);
  end;
  for r := 0 to High(Remote) do
  begin
    Key := GetOtherDataString(Remote[r]);
    if FindOther(Cur, Key) >= 0 then Continue;
    { thrown away here since the last exchange }
    b := FindOther(Base, Key);
    if (b >= 0) and SameUse(Remote[r], Base[b]) then Continue;
    Result[n] := Remote[r];
    Inc(n);
  end;
  SetLength(Result, n);
  { one entry is the one in use: the one used last }
  Newest := -1;
  for i := 0 to n - 1 do
    if Result[i].LastUsed <> 0 then
      if (Newest < 0) or (Result[i].LastUsedDate > Result[Newest].LastUsedDate) then Newest := i;
  for i := 0 to n - 1 do
    if (Result[i].LastUsed <> 0) and (i <> Newest) then Result[i].LastUsed := 0;
end;

function VisitAt(const Arr: TVisitArray; const V: TVisitEntry): Integer;
var
  k: Integer;
begin
  for k := 0 to High(Arr) do
    if (Arr[k].Started = V.Started) and (string(Arr[k].IP) = string(V.IP)) then Exit(k);
  Result := -1;
end;

{ the visits of every server, oldest first; the oldest go when there are too many }
function SharedMergeVisits(const Cur, Base, Remote: TVisitArray): TVisitArray;
var
  i, k, n, j, b: Integer;
  T: TVisitEntry;
begin
  Result := nil;
  SetLength(Result, Length(Cur) + Length(Remote));
  n := 0;
  for i := 0 to High(Cur) do
  begin
    T := Cur[i];
    k := VisitAt(Remote, T);
    if k >= 0 then
    begin
      { a visit both sides went on with (a player who moved to the other server within the
        cooldown): the seconds each side added count, as the play time does - the larger of
        the two lost what the other one added }
      b := VisitAt(Base, T);
      if b >= 0 then
        T.Seconds := Remote[k].Seconds + Max(0, Cur[i].Seconds - Base[b].Seconds)
      else if Remote[k].Seconds > T.Seconds then
        T.Seconds := Remote[k].Seconds;
    end
    else if VisitAt(Base, T) >= 0 then
      Continue;
    Result[n] := T;
    Inc(n);
  end;
  for i := 0 to High(Remote) do
  begin
    if VisitAt(Cur, Remote[i]) >= 0 then Continue;
    if VisitAt(Base, Remote[i]) >= 0 then Continue;
    Result[n] := Remote[i];
    Inc(n);
  end;
  for i := 1 to n - 1 do
  begin
    T := Result[i];
    j := i - 1;
    while (j >= 0) and (Result[j].Started > T.Started) do
    begin
      Result[j + 1] := Result[j];
      Dec(j);
    end;
    Result[j + 1] := T;
  end;
  if (Settings.VisitKeepCount > 0) and (n > Settings.VisitKeepCount) then
  begin
    for i := 0 to Settings.VisitKeepCount - 1 do
      Result[i] := Result[i + n - Settings.VisitKeepCount];
    n := Settings.VisitKeepCount;
  end;
  SetLength(Result, n);
end;

function SameHead(const A, B: TBinaryProfile): Boolean;
begin
  Result := (A.ActiveNameLen = B.ActiveNameLen) and CompareMem(@A.ActiveName, @B.ActiveName, SizeOf(A.ActiveName)) and
    (A.ActiveSteamLen = B.ActiveSteamLen) and CompareMem(@A.ActiveSteam, @B.ActiveSteam, SizeOf(A.ActiveSteam)) and
    (A.ActiveHWIDLen = B.ActiveHWIDLen) and CompareMem(@A.ActiveHWID, @B.ActiveHWID, SizeOf(A.ActiveHWID)) and
    (A.ActiveIPLen = B.ActiveIPLen) and CompareMem(@A.ActiveIP, @B.ActiveIP, SizeOf(A.ActiveIP));
end;

{ Cur := Remote with the changes of Cur since Base on top }
procedure SharedMerge3(var Cur: TProfileData; const Base, Remote: TProfileData);
var
  R: TProfileData;
begin
  R.Profile := Remote.Profile;
  { the name, Steam account, computer and address in use: this server's when they changed here }
  if not SameHead(Cur.Profile, Base.Profile) then
  begin
    R.Profile.ActiveName := Cur.Profile.ActiveName;
    R.Profile.ActiveNameLen := Cur.Profile.ActiveNameLen;
    R.Profile.ActiveSteam := Cur.Profile.ActiveSteam;
    R.Profile.ActiveSteamLen := Cur.Profile.ActiveSteamLen;
    R.Profile.ActiveHWID := Cur.Profile.ActiveHWID;
    R.Profile.ActiveHWIDLen := Cur.Profile.ActiveHWIDLen;
    R.Profile.ActiveIP := Cur.Profile.ActiveIP;
    R.Profile.ActiveIPLen := Cur.Profile.ActiveIPLen;
  end;
  if (Cur.Profile.RegisterTime > 0) and ((R.Profile.RegisterTime <= 0) or (Cur.Profile.RegisterTime < R.Profile.RegisterTime)) then
    R.Profile.RegisterTime := Cur.Profile.RegisterTime;
  if Cur.Profile.LastPlayedTime > R.Profile.LastPlayedTime then
    R.Profile.LastPlayedTime := Cur.Profile.LastPlayedTime;
  R.Profile.TimePlayed := Remote.Profile.TimePlayed + (Cur.Profile.TimePlayed - Base.Profile.TimePlayed);
  R.Profile.TimeOnSpectator := Remote.Profile.TimeOnSpectator + (Cur.Profile.TimeOnSpectator - Base.Profile.TimeOnSpectator);
  R.OtherNames := SharedMergeList(Cur.OtherNames, Base.OtherNames, Remote.OtherNames);
  R.OtherSteams := SharedMergeList(Cur.OtherSteams, Base.OtherSteams, Remote.OtherSteams);
  R.OtherHWIDs := SharedMergeList(Cur.OtherHWIDs, Base.OtherHWIDs, Remote.OtherHWIDs);
  R.OtherIPs := SharedMergeList(Cur.OtherIPs, Base.OtherIPs, Remote.OtherIPs);
  R.Profile.OtherNamesCount := Length(R.OtherNames);
  R.Profile.OtherSteamsCount := Length(R.OtherSteams);
  R.Profile.OtherHWIDsCount := Length(R.OtherHWIDs);
  R.Profile.OtherIPsCount := Length(R.OtherIPs);
  R.Visits := SharedMergeVisits(Cur.Visits, Base.Visits, Remote.Visits);
  { what Steam says: the newer answer, or this server's when it changed here }
  if (Cur.Steam.Fetched > Remote.Steam.Fetched) or
     ((Cur.Steam.Fetched = Remote.Steam.Fetched) and not CompareMem(@Cur.Steam, @Base.Steam, SizeOf(TSteamInfo))) then
    R.Steam := Cur.Steam
  else
    R.Steam := Remote.Steam;
  Cur := R;
  MarkProfileFirstUsed(Cur);
end;

{ the keys that find an account, for what a profile gained or lost; the caller holds
  ProfilesCritSec }
procedure SharedReindex(Local: Integer; const Old: TProfileData);

  procedure One(L: TStringList; const OldArr, NewArr: TBinaryOtherDataArray; Net: Boolean);
  var
    k: Integer;
    Key: string;
  begin
    if L = nil then Exit;
    for k := 0 to High(OldArr) do
    begin
      Key := GetOtherDataString(OldArr[k]);
      if (Key <> '') and (FindOther(NewArr, Key) < 0) then
      begin
        IndexRemove(L, Key, Local);
        if Net then IndexRemove(Net24Index, Net24Of(Key), Local);
      end;
    end;
    for k := 0 to High(NewArr) do
    begin
      Key := GetOtherDataString(NewArr[k]);
      if (Key <> '') and (FindOther(OldArr, Key) < 0) then
      begin
        IndexAdd(L, Key, Local);
        if Net then IndexAdd(Net24Index, Net24Of(Key), Local);
      end;
    end;
  end;

begin
  One(SteamIndex, Old.OtherSteams, Profiles[Local].OtherSteams, False);
  One(HwidIndex, Old.OtherHWIDs, Profiles[Local].OtherHWIDs, False);
  One(IpIndex, Old.OtherIPs, Profiles[Local].OtherIPs, True);
end;

{ ---------------- the sidecar: <file>.shared ---------------- }

function SharedDiskName: string;
begin
  Result := ChangeFileExt(SharedPath, '.shared');
end;

{ Which account here is which account of the database, and what changed here that the database
  has not got yet, with the state before it. It belongs to the file on disk - its size and time
  are in it - and is only written while memory has the numbers of that file. }
procedure SharedWriteDisk;
var
  Ids, PendIdx: array of LongInt;
  PendBase: array of AnsiString;
  n, m, k, Len: LongInt;
  FS: TFileStream;
  W: LongWord;
  Seq, Size: Int64;
  T: Double;
  Name: string;
begin
  EnterCriticalSection(SharedCS);
  try
    if (SharedPath = '') or not SharedIdsReady then Exit;
    Seq := SharedPullSeq;
    Size := SharedFileSize;
    T := SharedFileTime;
    Name := SharedDiskName;
  finally LeaveCriticalSection(SharedCS); end;
  PendIdx := nil;
  PendBase := nil;
  m := 0;
  LockProfiles;
  try
    n := ProfileCount;
    SetLength(Ids, n);
    for k := 0 to n - 1 do
    begin
      Ids[k] := SharedGlobalOf(k);
      if (k < Length(SharedBase)) and (SharedBase[k] <> '') and (SharedFlight[k] = 0) then
      begin
        if m >= Length(PendIdx) then
        begin
          SetLength(PendIdx, m * 2 + 16);
          SetLength(PendBase, m * 2 + 16);
        end;
        PendIdx[m] := k;
        PendBase[m] := SharedBase[k];
        Inc(m);
      end;
    end;
  finally UnlockProfiles; end;
  EnterCriticalSection(SharedCS);
  try
    try
      FS := TFileStream.Create(Name + '.tmp', fmCreate);
      try
        W := SHARED_MAGIC;
        FS.WriteBuffer(W, SizeOf(W));
        W := SHARED_FORMAT;
        FS.WriteBuffer(W, SizeOf(W));
        FS.WriteBuffer(Size, SizeOf(Size));
        FS.WriteBuffer(T, SizeOf(T));
        FS.WriteBuffer(Seq, SizeOf(Seq));
        FS.WriteBuffer(n, SizeOf(n));
        if n > 0 then FS.WriteBuffer(Ids[0], n * SizeOf(LongInt));
        FS.WriteBuffer(m, SizeOf(m));
        for k := 0 to m - 1 do
        begin
          FS.WriteBuffer(PendIdx[k], SizeOf(LongInt));
          Len := Length(PendBase[k]);
          FS.WriteBuffer(Len, SizeOf(Len));
          if Len > 0 then FS.WriteBuffer(PendBase[k][1], Len);
        end;
      finally FS.Free; end;
      { in one step: deleting the old one first left a moment without any, and a server killed
        then took the database as it is at its next start, without what waited here }
      if ReplaceFileAtomic(Name + '.tmp', Name) then
      begin
        SharedPendingOnDisk := m > 0;
        SharedDiskAt := GetTickCount64;
      end;
    except
    end;
  finally LeaveCriticalSection(SharedCS); end;
end;

{ the caller holds ProfilesCritSec; False when there is none, or it belongs to another file }
function SharedReadDisk(out Seq: Int64): Boolean;

  function ReadIt: Boolean;
  var
    FS: TFileStream;
    W: LongWord;
    Size, CurSize: Int64;
    T, CurT, CurLocal: Double;
    n, m, k, G, Idx, Len: LongInt;
    B: AnsiString;
  begin
    Result := False;
    if not FileExists(SharedDiskName) then Exit;
    if not SnapshotStamp(SharedPath, CurSize, CurT, CurLocal) then Exit;
    FS := TFileStream.Create(SharedDiskName, fmOpenRead or fmShareDenyWrite);
    try
      if (FS.Read(W, SizeOf(W)) <> SizeOf(W)) or (W <> SHARED_MAGIC) then Exit;
      if (FS.Read(W, SizeOf(W)) <> SizeOf(W)) or (W <> SHARED_FORMAT) then Exit;
      if FS.Read(Size, SizeOf(Size)) <> SizeOf(Size) then Exit;
      if FS.Read(T, SizeOf(T)) <> SizeOf(T) then Exit;
      { written for another copy of the database file: it says nothing about this one }
      if (Size <> CurSize) or (Abs(T - CurT) > 3.0 / 86400.0) then Exit;
      if FS.Read(Seq, SizeOf(Seq)) <> SizeOf(Seq) then Exit;
      if (FS.Read(n, SizeOf(n)) <> SizeOf(n)) or (n < 0) or (n > MAX_PROFILES_HARD) then Exit;
      for k := 0 to n - 1 do
      begin
        if FS.Read(G, SizeOf(G)) <> SizeOf(G) then Exit;
        if (k < ProfileCount) and (G > 0) then SharedLink(k, G);
      end;
      if (FS.Read(m, SizeOf(m)) <> SizeOf(m)) or (m < 0) or (m > MAX_PROFILES_HARD) then Exit;
      for k := 0 to m - 1 do
      begin
        if (FS.Read(Idx, SizeOf(Idx)) <> SizeOf(Idx)) or (FS.Read(Len, SizeOf(Len)) <> SizeOf(Len)) then Exit;
        if (Len < 0) or (Len > 16 * 1024 * 1024) then Exit;
        SetLength(B, Len);
        if (Len > 0) and (FS.Read(B[1], Len) <> Len) then Exit;
        if (Idx >= 0) and (Idx < ProfileCount) then
        begin
          if Idx >= Length(SharedGlobal) then SharedGrow(Idx);
          SharedBase[Idx] := B;
          SharedDirtyArr[Idx] := 1;
        end;
      end;
      Result := True;
    finally FS.Free; end;
  end;

begin
  Seq := 0;
  try
    Result := ReadIt;
  except
    Result := False;
  end;
  if not Result then
  begin
    Seq := 0;
    SharedForget;
  end;
end;

{ a full write ended: the numbers beside the file belong to the new one }
procedure SharedAfterSave(const FilePath: string);
var
  Size: Int64;
  T, TL: Double;
  Mine: Boolean;
begin
  EnterCriticalSection(SharedCS);
  try
    Mine := (SharedPath <> '') and (ExpandFileName(FilePath) = ExpandFileName(SharedPath));
  finally LeaveCriticalSection(SharedCS); end;
  if not Mine then Exit;
  if not SnapshotStamp(FilePath, Size, T, TL) then Exit;
  EnterCriticalSection(SharedCS);
  try
    SharedFileSize := Size;
    SharedFileTime := T;
    SharedIdsReady := True;
  finally LeaveCriticalSection(SharedCS); end;
  SharedWriteDisk;
end;

{ ---------------- the database ---------------- }

function SharedMeta(C: TMyConnection; const Name: string; Lock: Boolean): Int64;
var
  R: TMyResult;
  Q: string;
begin
  Q := 'SELECT value FROM af_db_meta WHERE name = ' + MyStr(Name);
  if Lock then Q := Q + ' FOR UPDATE';
  R := C.Query(Q);
  try
    if R.RowCount > 0 then Result := R.Int(0, 0) else Result := -1;
  finally R.Free; end;
end;

procedure SharedPrepare(C: TMyConnection);
begin
  MyPrepareTable(C, 'af_db_profiles', AFS_PROFILE_COLS, AFS_PROFILE_KEYS);
  MyPrepareTable(C, 'af_db_verdicts', AFS_VERDICT_COLS, AFS_VERDICT_KEYS);
  MyPrepareTable(C, 'af_db_geo', AFS_GEO_COLS, AFS_GEO_KEYS);
  MyPrepareTable(C, 'af_db_meta', AFS_META_COLS, AFS_META_KEYS);
  C.Exec('INSERT IGNORE INTO af_db_meta (name, value) VALUES (''seq'', 0), (''next_profile'', 1)');
end;

{ one profile row: the columns a website reads, and the whole profile }
function SharedRow(Global: Integer; const P: TProfileData; const Data: AnsiString; Version, Seq: Int64): string;
var
  Steam: string;
begin
  Steam := Copy(P.Profile.ActiveSteam, 1, P.Profile.ActiveSteamLen);
  Result := '(' + MyInt(Global) + ', ' + MyStrMax(DbText(Copy(P.Profile.ActiveName, 1, P.Profile.ActiveNameLen)), 64) + ', ';
  if Steam <> '' then Result := Result + MyStrMax(Steam, 32) else Result := Result + 'NULL';
  Result := Result + ', ' + MyStrMax(DbText(Copy(P.Profile.ActiveHWID, 1, P.Profile.ActiveHWIDLen)), 32) + ', ' +
    MyStrMax(Copy(P.Profile.ActiveIP, 1, P.Profile.ActiveIPLen), 46) + ', ' + MyDate(P.Profile.LastPlayedTime) + ', ' +
    MyInt(P.Profile.TimePlayed) + ', ' + SharedBin(Data) + ', ' + MyInt(Version) + ', ' + MyInt(Seq) + ', ' +
    MyInt(Settings.MysqlServerId) + ', ' + MyDate(Now) + ')';
end;

function SharedVerdictRow(Lo, Hi: Integer; const V: TPairVerdict; const SeqText, Sid: string): string;
begin
  Result := '(' + MyInt(Lo) + ', ' + MyInt(Hi) + ', ' + MyInt(V.Verdict) + ', ' + MyFloat(V.Decided) + ', ' +
    MyStrMax(DbGeo(string(V.By)), 32) + ', ' + SeqText + ', ' + Sid + ')';
end;

function SharedGeoRow(const G: TGeoEntry; const SeqText, Sid: string): string;
begin
  Result := '(' + MyStrMax(string(G.IP), 46) + ', ' + SharedBin(GeoBytes(G)) + ', ' + MyFloat(G.LastUpdated) + ', ' +
    SeqText + ', ' + Sid + ')';
end;

{ ---------------- into memory ---------------- }

{ A place located by another server; the newer one stays. The caller holds GeoCritSec. Many at
  once (Bulk: every address once, as the database has them) are only put in place, and the
  caller builds the index again after the last one: one sorted insertion each made reading a
  big database take seconds with the lock held. }
procedure SharedTakeGeoLocked(const G: TGeoEntry; Bulk: Boolean);
var
  k: Integer;
  IP: string;
  E: TGeoEntry;
begin
  IP := string(G.IP);
  if IP = '' then Exit;
  E := G;
  SaneDate(E.LastUpdated);
  k := FindGeoIndex(IP);
  if k < 0 then
  begin
    k := GeoCount;
    Inc(GeoCount);
    if GeoCount > Length(GeoCache) then SetLength(GeoCache, GeoCount * 2 + 50);
    if not Bulk then IndexAdd(GeoIndex, IP, k);
  end
  else if GeoCache[k].LastUpdated > E.LastUpdated then Exit;
  GeoCache[k] := E;
end;

{ a decision of an admin of another server, with the numbers of this one; the newer one stays.
  The caller holds ProfilesCritSec. }
procedure SharedTakeVerdict(const V: TPairVerdict);
var
  A, B, q, k: Integer;
  D: Double;
begin
  A := SharedLocalOf(V.A);
  B := SharedLocalOf(V.B);
  if (A < 0) or (B < 0) or (A = B) or (A >= ProfileCount) or (B >= ProfileCount) then Exit;
  D := V.Decided;
  SaneDate(D);
  q := IndexFirst(VerdictIndex, PairKey(A, B));
  if q >= 0 then
  begin
    k := Integer(PtrInt(VerdictIndex.Objects[q]));
    if (k >= 0) and (k < VerdictCount) and (Verdicts[k].Decided > D) then Exit;
  end;
  { a decision taken here that has not gone out yet is newer than what comes in: one taken back
    here has no entry above to say so, and an older one of another server came back with it }
  for k := 0 to SharedVerdictCount - 1 do
    if (SharedVerdictChanges[k].A = Min(A, B)) and (SharedVerdictChanges[k].B = Max(A, B)) and
       (SharedVerdictChanges[k].Decided >= D) then Exit;
  SharedQuiet := True;
  try
    StoreVerdict(A, B, V.Verdict, '');
  finally SharedQuiet := False; end;
  q := IndexFirst(VerdictIndex, PairKey(A, B));
  if q >= 0 then
  begin
    k := Integer(PtrInt(VerdictIndex.Objects[q]));
    if (k >= 0) and (k < VerdictCount) then
    begin
      Verdicts[k].Decided := D;
      Verdicts[k].By := V.By;
    end;
  end;
end;

{ An account of the database this server did not know: a new number here - or, when a player
  new here has the same Steam account (without Steam: the same computer) and has not reached the
  database yet, that player's account. The caller holds ProfilesCritSec. }
procedure SharedAdopt(Global: Integer; const Data: AnsiString; const PR: TProfileData; Version: Int64);
var
  Local, j, k: Integer;
  Steam, Hwid: string;
  PB, Old: TProfileData;
begin
  Local := -1;
  Steam := Copy(PR.Profile.ActiveSteam, 1, PR.Profile.ActiveSteamLen);
  if Steam <> '' then
  begin
    j := IndexFirst(SteamIndex, Steam);
    while (j >= 0) and (j < SteamIndex.Count) do
    begin
      if SteamIndex[j] <> Steam then Break;
      k := Integer(PtrInt(SteamIndex.Objects[j]));
      if (k >= 0) and (k < ProfileCount) and (SharedGlobalOf(k) = 0) and (SharedFlight[k] = 0) then
      begin
        Local := k;
        Break;
      end;
      Inc(j);
    end;
  end
  else
  begin
    { without Steam: the player of the same computer who has no Steam account either }
    Hwid := Copy(PR.Profile.ActiveHWID, 1, PR.Profile.ActiveHWIDLen);
    if Hwid <> '' then
    begin
      j := IndexFirst(HwidIndex, Hwid);
      while (j >= 0) and (j < HwidIndex.Count) do
      begin
        if HwidIndex[j] <> Hwid then Break;
        k := Integer(PtrInt(HwidIndex.Objects[j]));
        if (k >= 0) and (k < ProfileCount) and (SharedGlobalOf(k) = 0) and (SharedFlight[k] = 0) and
           (Length(Profiles[k].OtherSteams) = 0) then
        begin
          Local := k;
          Break;
        end;
        Inc(j);
      end;
    end;
  end;
  if Local >= 0 then
  begin
    SharedLink(Local, Global);
    Old := Profiles[Local];
    if not SharedUnpack(SharedBase[Local], PB) then SharedUnpack(SharedEmptyData, PB);
    SharedMerge3(Profiles[Local], PB, PR);
    SharedBase[Local] := Data;
    SharedVersion[Local] := Version;
    SharedDirtyArr[Local] := 1;
    SharedReindex(Local, Old);
    JournalMark(Local);
    Exit;
  end;
  Local := ProfileCount;
  Inc(ProfileCount);
  if ProfileCount > Length(Profiles) then SetLength(Profiles, ProfileCount + 100);
  SharedUnpack('', Old);
  Profiles[Local] := PR;
  MarkProfileFirstUsed(Profiles[Local]);
  SharedLink(Local, Global);
  SharedBase[Local] := '';
  SharedVersion[Local] := Version;
  SharedReindex(Local, Old);
  JournalMark(Local);
end;

{ what was sent is in the database: memory takes the database's state, with whatever changed here
  meanwhile on top. True when an account got its number. }
function SharedApplyAnswers(const Items: TSharedItems; const Answers: TSharedAnswers): Boolean;
var
  i, k, L: Integer;
  PS, PD, Old: TProfileData;
begin
  Result := False;
  i := 0;
  while i <= High(Answers) do
  begin
    LockProfiles;
    try
      k := 0;
      while (i <= High(Answers)) and (k < AFS_APPLY) do
      begin
        L := Answers[i].Local;
        if (L >= 0) and (L < ProfileCount) and SharedUnpack(Answers[i].Data, PD) then
        begin
          if SharedGlobalOf(L) <> Answers[i].Global then
          begin
            SharedLink(L, Answers[i].Global);
            Result := True;
          end;
          Old := Profiles[L];
          if SharedDirtyArr[L] = 0 then
          begin
            Profiles[L] := PD;
            MarkProfileFirstUsed(Profiles[L]);
            SharedBase[L] := '';
          end
          else
          begin
            if SharedUnpack(Answers[i].Snap, PS) then SharedMerge3(Profiles[L], PS, PD);
            SharedBase[L] := Answers[i].Data;
          end;
          SharedReindex(L, Old);
          SharedVersion[L] := Answers[i].Version;
          JournalMark(L);
        end;
        Inc(i);
        Inc(k);
      end;
    finally UnlockProfiles; end;
  end;
  LockProfiles;
  try
    for i := 0 to High(Items) do
      if (Items[i].Local >= 0) and (Items[i].Local < Length(SharedFlight)) then SharedFlight[Items[i].Local] := 0;
  finally UnlockProfiles; end;
end;

{ the rows the other servers wrote. True when an account came in that this server did not know. }
function SharedApplyRows(const Rows: TSharedRows): Boolean;
var
  i, k, n: Integer;
  PR, PB, Old: TProfileData;
begin
  Result := False;
  i := 0;
  while i <= High(Rows) do
  begin
    LockProfiles;
    try
      k := 0;
      while (i <= High(Rows)) and (k < AFS_APPLY) do
      begin
        if SharedUnpack(Rows[i].Data, PR) then
        begin
          n := SharedLocalOf(Rows[i].Global);
          if n < 0 then
          begin
            SharedAdopt(Rows[i].Global, Rows[i].Data, PR, Rows[i].Version);
            Result := True;
          end
          else if (n < ProfileCount) and (Rows[i].Version > SharedVersion[n]) then
          begin
            Old := Profiles[n];
            if SharedBase[n] = '' then
            begin
              Profiles[n] := PR;
              MarkProfileFirstUsed(Profiles[n]);
            end
            else
            begin
              { changed here too: those changes stay on top, and go in from the new base }
              if SharedUnpack(SharedBase[n], PB) then SharedMerge3(Profiles[n], PB, PR)
              else Profiles[n] := PR;
              SharedBase[n] := Rows[i].Data;
            end;
            SharedReindex(n, Old);
            SharedVersion[n] := Rows[i].Version;
            JournalMark(n);
          end;
        end;
        Inc(i);
        Inc(k);
      end;
    finally UnlockProfiles; end;
  end;
end;

{ ---------------- one exchange ---------------- }

{ what changed here since the last exchange, a few profiles per hold of the lock }
procedure SharedCollect(out Items: TSharedItems; out Verd: TVerdictArray; out Geo: TSharedGeo);
var
  i, n, Held, g: Integer;
  More: Boolean;
  Snap: AnsiString;
  Seen: TStringList;
begin
  Items := nil;
  Verd := nil;
  Geo := nil;
  n := 0;
  i := 0;
  repeat
    LockProfiles;
    try
      Held := 0;
      More := False;
      while i < ProfileCount do
      begin
        if (i < Length(SharedDirtyArr)) and (SharedDirtyArr[i] <> 0) then
        begin
          if Held >= AFS_COLLECT then
          begin
            More := True;
            Break;
          end;
          Inc(Held);
          SharedDirtyArr[i] := 0;
          if (SharedGlobal[i] = 0) and ProfileIsEmpty(i) then
          begin
            { never reached the database and nothing is left of it }
            SharedBase[i] := '';
            Inc(i);
            Continue;
          end;
          Snap := SharedPackData(Profiles[i]);
          if n >= Length(Items) then SetLength(Items, n * 2 + 16);
          Items[n].Local := i;
          Items[n].Global := SharedGlobal[i];
          Items[n].Snap := Snap;
          if SharedBase[i] <> '' then
            Items[n].Base := SharedBase[i]
          else
          begin
            { changed without the state before it: the change cannot be told apart from what the
              database has, and the database's state wins }
            Items[n].Base := Snap;
            Inc(SharedMissed);
          end;
          Items[n].Steam := Copy(Profiles[i].Profile.ActiveSteam, 1, Profiles[i].Profile.ActiveSteamLen);
          Items[n].Hwid := Copy(Profiles[i].Profile.ActiveHWID, 1, Profiles[i].Profile.ActiveHWIDLen);
          SharedFlight[i] := 1;
          Inc(n);
        end;
        Inc(i);
      end;
      if not More then
      begin
        Verd := Copy(SharedVerdictChanges, 0, SharedVerdictCount);
        SharedVerdictCount := 0;
      end;
    finally UnlockProfiles; end;
  until not More;
  SetLength(Items, n);
  { every address once: while the database is gone the same ones come back after every failed
    exchange, and an address located again meanwhile would be there twice }
  Seen := TStringList.Create;
  try
    Seen.Sorted := True;
    Seen.CaseSensitive := True;
    Seen.UseLocale := False;
    EnterCriticalSection(GeoCritSec);
    try
      if (SharedGeoNew <> nil) and (SharedGeoNew.Count > 0) then
      begin
        SetLength(Geo, SharedGeoNew.Count);
        n := 0;
        for i := 0 to SharedGeoNew.Count - 1 do
        begin
          if Seen.IndexOf(SharedGeoNew[i]) >= 0 then Continue;
          Seen.Add(SharedGeoNew[i]);
          g := FindGeoIndex(SharedGeoNew[i]);
          if g >= 0 then
          begin
            Geo[n] := GeoCache[g];
            Inc(n);
          end;
        end;
        SetLength(Geo, n);
        SharedGeoNew.Clear;
      end;
    finally LeaveCriticalSection(GeoCritSec); end;
  finally Seen.Free; end;
end;

{ the exchange failed: everything waits for the next one }
procedure SharedRequeue(const Items: TSharedItems; const Verd: TVerdictArray; const Geo: TSharedGeo);
var
  k, L: Integer;
begin
  LockProfiles;
  try
    for k := 0 to High(Items) do
    begin
      L := Items[k].Local;
      if (L >= 0) and (L < Length(SharedFlight)) then
      begin
        SharedFlight[L] := 0;
        SharedDirtyArr[L] := 1;
      end;
    end;
    for k := 0 to High(Verd) do
    begin
      if SharedVerdictCount >= Length(SharedVerdictChanges) then
        SetLength(SharedVerdictChanges, SharedVerdictCount * 2 + 16);
      SharedVerdictChanges[SharedVerdictCount] := Verd[k];
      Inc(SharedVerdictCount);
    end;
  finally UnlockProfiles; end;
  EnterCriticalSection(GeoCritSec);
  try
    if SharedGeoNew <> nil then
      for k := 0 to High(Geo) do SharedGeoNew.Add(string(Geo[k].IP));
  finally LeaveCriticalSection(GeoCritSec); end;
end;

procedure SharedFailed(const Why: string);
var
  Tell: Boolean;
  Last: QWord;
begin
  EnterCriticalSection(SharedCS);
  try
    Tell := SharedOnline or (SharedFailures = 0);
    SharedOnline := False;
    Inc(SharedFailures);
    SharedLastError := Why;
    Last := SharedDiskAt;
  finally LeaveCriticalSection(SharedCS); end;
  if Tell then
    Notice(MAX_SLOTS, Settings.ColWarn, 'Shared database: ' + Why + ' - the changes wait here and go in once it answers.');
  { what waits is kept beside the file every half a minute, so a server that is killed meanwhile
    still sends it after its next start }
  if GetTickCount64 - Last > 30000 then SharedWriteDisk;
end;

procedure SharedSucceeded(Written, Read: Integer);
var
  Back: Boolean;
begin
  EnterCriticalSection(SharedCS);
  try
    Back := not SharedOnline and (SharedFailures > 0);
    SharedOnline := True;
    SharedEver := True;
    SharedFailures := 0;
    SharedLastError := '';
    SharedLastOk := Now;
    Inc(SharedRounds);
    Inc(SharedWritten, Written);
    Inc(SharedRead, Read);
  finally LeaveCriticalSection(SharedCS); end;
  if Back then
    Notice(MAX_SLOTS, Settings.ColGood, 'Shared database: it answers again, the changes that waited are in.');
end;

{ One exchange: what changed here goes in, and - with Pull - what the other servers wrote since
  the last one comes back. False (with Why) when the database could not be reached; what was to
  go in waits for the next exchange then. }
function SharedRound(Pull: Boolean; out Why: string): Boolean;
var
  C: TMyConnection;
  Items: TSharedItems;
  Verd, Later, PulledV: TVerdictArray;
  Geo, PulledG: TSharedGeo;
  Answers: TSharedAnswers;
  Rows: TSharedRows;
  R: TMyResult;
  B: TMyBatch;
  PC, PB, PR: TProfileData;
  i, k, n, Target, Lo, Hi, LaterN, LastId, ReadCount: Integer;
  Seq, Version, Top, Since, LastSeq: Int64;
  Data: AnsiString;
  SeqText, Sid, Where: string;
  Pushed, Linked, Complete, First: Boolean;
begin
  Result := False;
  Why := '';
  SharedCollect(Items, Verd, Geo);
  Pushed := (Length(Items) > 0) or (Length(Verd) > 0) or (Length(Geo) > 0);
  if not Pushed and not Pull then Exit(True);
  C := nil;
  Answers := nil;
  Later := nil;
  LaterN := 0;
  Linked := False;
  ReadCount := 0;
  Sid := MyInt(Settings.MysqlServerId);
  try
    C := NewMysqlConnection;
    C.IOTimeoutMs := 30000;
    C.Connect;
    if Pushed then
    begin
      C.Exec('START TRANSACTION');
      try
        { the writers take turns here, and the number of this change is the next one }
        Seq := SharedMeta(C, 'seq', True) + 1;
        SeqText := MyInt(Seq);
        SetLength(Answers, Length(Items));
        n := 0;
        for i := 0 to High(Items) do
        begin
          if not SharedUnpack(Items[i].Snap, PC) then Continue;
          Target := Items[i].Global;
          if (Target <= 0) and (Items[i].Steam <> '') then
          begin
            { new here: the database may know the Steam account already }
            R := C.Query('SELECT id FROM af_db_profiles WHERE steam = ' + MyStrMax(Items[i].Steam, 32) +
              ' ORDER BY id LIMIT 1 FOR UPDATE');
            try
              if R.RowCount > 0 then Target := R.Int(0, 0);
            finally R.Free; end;
          end;
          if (Target <= 0) and (Items[i].Hwid <> '') then
          begin
            { or the account of the same computer that has no Steam account, as a join finds it }
            R := C.Query('SELECT id FROM af_db_profiles WHERE steam IS NULL AND computer = ' +
              MyStrMax(DbText(Items[i].Hwid), 32) + ' ORDER BY id LIMIT 1 FOR UPDATE');
            try
              if R.RowCount > 0 then Target := R.Int(0, 0);
            finally R.Free; end;
          end;
          Version := 0;
          Data := '';
          if Target > 0 then
          begin
            R := C.Query('SELECT version, data FROM af_db_profiles WHERE id = ' + MyInt(Target) + ' FOR UPDATE');
            try
              if R.RowCount > 0 then
              begin
                Version := R.Int(0, 0);
                Data := R.Str(0, 1);
              end;
            finally R.Free; end;
          end
          else
          begin
            Target := SharedMeta(C, 'next_profile', True);
            if Target < 1 then Target := 1;
            C.Exec('UPDATE af_db_meta SET value = ' + MyInt(Target + 1) + ' WHERE name = ''next_profile''');
          end;
          if SharedUnpack(Data, PR) then
          begin
            if not SharedUnpack(Items[i].Base, PB) then SharedUnpack(SharedEmptyData, PB);
            SharedMerge3(PC, PB, PR);
          end;
          Data := SharedPackData(PC);
          C.Exec(AFS_ROW_HEAD + ' ' + SharedRow(Target, PC, Data, Version + 1, Seq) + ' ' + AFS_ROW_TAIL);
          Answers[n].Local := Items[i].Local;
          Answers[n].Global := Target;
          Answers[n].Snap := Items[i].Snap;
          Answers[n].Data := Data;
          Answers[n].Version := Version + 1;
          Inc(n);
        end;
        SetLength(Answers, n);
        { the decisions of the admins, with the numbers of the database. One about an account that
          has not reached the database yet waits for it. }
        LockProfiles;
        try
          for i := 0 to High(Verd) do
          begin
            Lo := SharedGlobalOf(Verd[i].A);
            Hi := SharedGlobalOf(Verd[i].B);
            for k := 0 to High(Answers) do
            begin
              if Answers[k].Local = Verd[i].A then Lo := Answers[k].Global;
              if Answers[k].Local = Verd[i].B then Hi := Answers[k].Global;
            end;
            if (Lo <= 0) or (Hi <= 0) then
            begin
              if ((Lo > 0) or not ProfileIsEmpty(Verd[i].A)) and ((Hi > 0) or not ProfileIsEmpty(Verd[i].B)) then
              begin
                if LaterN >= Length(Later) then SetLength(Later, LaterN * 2 + 8);
                Later[LaterN] := Verd[i];
                Inc(LaterN);
              end;
              Verd[i].A := 0;
              Verd[i].B := 0;
            end
            else
            begin
              Verd[i].A := Lo;
              Verd[i].B := Hi;
            end;
          end;
        finally UnlockProfiles; end;
        B := TMyBatch.Create(C, AFS_VERDICT_HEAD, AFS_VERDICT_TAIL);
        try
          for i := 0 to High(Verd) do
          begin
            if (Verd[i].A <= 0) or (Verd[i].B <= 0) or (Verd[i].A = Verd[i].B) then Continue;
            if Verd[i].A < Verd[i].B then
            begin
              Lo := Verd[i].A;
              Hi := Verd[i].B;
            end
            else
            begin
              Lo := Verd[i].B;
              Hi := Verd[i].A;
            end;
            B.Add(SharedVerdictRow(Lo, Hi, Verd[i], SeqText, Sid));
          end;
          B.Flush;
        finally B.Free; end;
        { the places this server located }
        B := TMyBatch.Create(C, AFS_GEO_HEAD, AFS_GEO_TAIL);
        try
          for i := 0 to High(Geo) do
            if string(Geo[i].IP) <> '' then B.Add(SharedGeoRow(Geo[i], SeqText, Sid));
          B.Flush;
        finally B.Free; end;
        C.Exec('UPDATE af_db_meta SET value = ' + SeqText + ' WHERE name = ''seq''');
        C.Exec('COMMIT');
      except
        try C.Exec('ROLLBACK'); except end;
        raise;
      end;
    end;
  except
    on Ex: Exception do
    begin
      Why := Ex.Message;
      DropMysqlConnection(C);
      SharedRequeue(Items, Verd, Geo);
      SharedFailed(Why);
      Exit;
    end;
  end;

  { in the database now: memory takes its state }
  if SharedApplyAnswers(Items, Answers) then Linked := True;
  if LaterN > 0 then
  begin
    SetLength(Later, LaterN);
    SharedRequeue(nil, Later, nil);
  end;

  { what the other servers wrote since the last exchange }
  Complete := True;
  Top := 0;
  if Pull then
  begin
    PulledV := nil;
    PulledG := nil;
    try
      EnterCriticalSection(SharedCS);
      try
        Since := SharedPullSeq;
      finally LeaveCriticalSection(SharedCS); end;
      Top := SharedMeta(C, 'seq', False);
      if Top > Since then
      begin
        LastSeq := Since;
        LastId := 0;
        First := True;
        repeat
          if First then Where := 'seq > ' + MyInt(Since)
          else Where := '(seq > ' + MyInt(LastSeq) + ' OR (seq = ' + MyInt(LastSeq) + ' AND id > ' + MyInt(LastId) + '))';
          First := False;
          R := C.Query('SELECT id, version, seq, data FROM af_db_profiles WHERE ' + Where + ' AND seq <= ' + MyInt(Top) +
            ' AND updated_by <> ' + Sid + ' ORDER BY seq, id LIMIT ' + IntToStr(AFS_PAGE));
          try
            n := R.RowCount;
            SetLength(Rows, n);
            for k := 0 to n - 1 do
            begin
              Rows[k].Global := R.Int(k, 0);
              Rows[k].Version := R.Int(k, 1);
              LastSeq := R.Int(k, 2);
              LastId := Rows[k].Global;
              Rows[k].Data := R.Str(k, 3);
            end;
          finally R.Free; end;
          if SharedApplyRows(Rows) then Linked := True;
          Inc(ReadCount, n);
          Rows := nil;
          if (n = AFS_PAGE) and SharedStopping then Complete := False;
        until (n < AFS_PAGE) or not Complete;
        R := C.Query('SELECT a, b, verdict, decided, decided_by FROM af_db_verdicts WHERE seq > ' + MyInt(Since) +
          ' AND seq <= ' + MyInt(Top) + ' AND updated_by <> ' + Sid);
        try
          SetLength(PulledV, R.RowCount);
          for k := 0 to R.RowCount - 1 do
          begin
            FillChar(PulledV[k], SizeOf(TPairVerdict), 0);
            PulledV[k].A := R.Int(k, 0);
            PulledV[k].B := R.Int(k, 1);
            PulledV[k].Verdict := R.Int(k, 2);
            PulledV[k].Decided := R.Float(k, 3);
            SetGeoString(PulledV[k].By, R.Str(k, 4));
          end;
        finally R.Free; end;
        R := C.Query('SELECT data FROM af_db_geo WHERE seq > ' + MyInt(Since) + ' AND seq <= ' + MyInt(Top) +
          ' AND updated_by <> ' + Sid);
        try
          n := 0;
          SetLength(PulledG, R.RowCount);
          for k := 0 to R.RowCount - 1 do
          begin
            Data := R.Str(k, 0);
            if Length(Data) <> SizeOf(TGeoEntry) then Continue;
            Move(Data[1], PulledG[n], SizeOf(TGeoEntry));
            Inc(n);
          end;
          SetLength(PulledG, n);
        finally R.Free; end;
      end;
    except
      on Ex: Exception do
      begin
        { what went in is in; what comes back is asked for again next time }
        Why := Ex.Message;
        DropMysqlConnection(C);
        SharedFailed(Why);
        Exit;
      end;
    end;
    if Length(PulledV) > 0 then
    begin
      LockProfiles;
      try
        for k := 0 to High(PulledV) do SharedTakeVerdict(PulledV[k]);
      finally UnlockProfiles; end;
    end;
    if Length(PulledG) > 0 then
    begin
      EnterCriticalSection(GeoCritSec);
      try
        for k := 0 to High(PulledG) do SharedTakeGeoLocked(PulledG[k], Length(PulledG) > 64);
        if Length(PulledG) > 64 then RebuildGeoIndex;
      finally LeaveCriticalSection(GeoCritSec); end;
    end;
  end;
  DropMysqlConnection(C);

  EnterCriticalSection(SharedCS);
  try
    if Complete and (Top > SharedPullSeq) then SharedPullSeq := Top;
    Linked := Linked or SharedPendingOnDisk;
  finally LeaveCriticalSection(SharedCS); end;
  SharedSucceeded(Length(Answers), ReadCount);
  { a new number, or what waited beside the file is in: the neighbour of the file is told }
  if Linked then SharedWriteDisk;
  Result := True;
end;

procedure TSharedThread.Execute;
var
  Why: string;
  Wait, Fails: Integer;
begin
  GuardWorkerThread;
  Fails := 0;
  while not SharedStopping do
  begin
    Wait := Max(1, Settings.SyncSeconds) * 1000;
    if Fails > 0 then Wait := Min(60000, Wait shl Min(Fails, 5));
    RTLEventWaitFor(SharedWake, Wait);
    if SharedStopping then Break;
    try
      if SharedRound(True, Why) then Fails := 0 else Inc(Fails);
    except
      Inc(Fails);
    end;
  end;
  { the last exchange: what changed here goes in }
  try
    SharedRound(False, Why);
  except
  end;
end;

{ ---------------- the start ---------------- }

{ The first start of this server on the database: its accounts go in. An account the database
  has already - the same Steam account, or without one the same computer - gets this server's
  history added; every other account comes in with a number of its own - the first server keeps
  its numbers. }
function SharedJoin(C: TMyConnection; out Note: string): Boolean;
var
  R: TMyResult;
  BySteam, ByComputer: TStringList;
  i, j, k, q, Id, NextId, MaxId, Added, Joined, Count, Lo, Hi: Integer;
  Empty: Boolean;
  Map: array of LongInt;
  Snaps: array of AnsiString;
  PC, PB, PR: TProfileData;
  Seq, Version: Int64;
  SeqText, Sid, Steam, Hwid: string;
  Data: AnsiString;
  Rows: TMyBatch;
  VerdCopy: TVerdictArray;
  GeoCopy: TSharedGeo;
begin
  Result := False;
  Note := '';
  Added := 0;
  Joined := 0;
  Sid := MyInt(Settings.MysqlServerId);
  SharedUnpack(SharedEmptyData, PB);
  BySteam := TStringList.Create;
  ByComputer := TStringList.Create;
  try
    BySteam.CaseSensitive := True;
    BySteam.Sorted := True;
    BySteam.Duplicates := dupIgnore;
    ByComputer.CaseSensitive := True;
    ByComputer.Sorted := True;
    ByComputer.Duplicates := dupIgnore;
    C.Exec('START TRANSACTION');
    try
      Seq := SharedMeta(C, 'seq', True) + 1;
      SeqText := MyInt(Seq);
      if SharedMeta(C, 'joined_' + IntToStr(Settings.MysqlServerId), False) >= 0 then
      begin
        C.Exec('ROLLBACK');
        Exit(True);
      end;
      R := C.Query('SELECT id, steam, computer FROM af_db_profiles');
      try
        Empty := R.RowCount = 0;
        for i := 0 to R.RowCount - 1 do
          if not R.IsNull(i, 1) and (R.Str(i, 1) <> '') then
            BySteam.AddObject(R.Str(i, 1), TObject(PtrInt(R.Int(i, 0))))
          else if not R.IsNull(i, 2) and (R.Str(i, 2) <> '') then
            ByComputer.AddObject(R.Str(i, 2), TObject(PtrInt(R.Int(i, 0))));
      finally R.Free; end;
      NextId := SharedMeta(C, 'next_profile', True);
      if NextId < 1 then NextId := 1;
      MaxId := 0;
      LockProfiles;
      try
        Count := ProfileCount;
      finally UnlockProfiles; end;
      SetLength(Map, Count);
      SetLength(Snaps, AFS_COLLECT);
      Rows := TMyBatch.Create(C, AFS_ROW_HEAD, AFS_ROW_TAIL);
      try
        i := 0;
        while i < Count do
        begin
          { a few profiles at a time under the lock, the database without it }
          LockProfiles;
          try
            j := i;
            while (j < Count) and (j - i < AFS_COLLECT) do
            begin
              if (j < ProfileCount) and not ProfileIsEmpty(j) then Snaps[j - i] := SharedPackData(Profiles[j])
              else Snaps[j - i] := '';
              Inc(j);
            end;
          finally UnlockProfiles; end;
          for k := i to j - 1 do
          begin
            Map[k] := 0;
            if not SharedUnpack(Snaps[k - i], PC) then Continue;
            Steam := Copy(PC.Profile.ActiveSteam, 1, PC.Profile.ActiveSteamLen);
            Hwid := DbText(Copy(PC.Profile.ActiveHWID, 1, PC.Profile.ActiveHWIDLen));
            Id := 0;
            if Steam <> '' then
            begin
              q := BySteam.IndexOf(Steam);
              if q >= 0 then Id := Integer(PtrInt(BySteam.Objects[q]));
            end;
            { or the account of the same computer that has no Steam account, as a join finds it }
            if (Id = 0) and (Hwid <> '') then
            begin
              q := ByComputer.IndexOf(Hwid);
              if q >= 0 then Id := Integer(PtrInt(ByComputer.Objects[q]));
            end;
            if Id > 0 then
            begin
              { the same player as an account already in the database: they become one }
              Rows.Flush;
              Version := 0;
              Data := '';
              R := C.Query('SELECT version, data FROM af_db_profiles WHERE id = ' + MyInt(Id) + ' FOR UPDATE');
              try
                if R.RowCount > 0 then
                begin
                  Version := R.Int(0, 0);
                  Data := R.Str(0, 1);
                end;
              finally R.Free; end;
              if SharedUnpack(Data, PR) then SharedMerge3(PC, PB, PR);
              C.Exec(AFS_ROW_HEAD + ' ' + SharedRow(Id, PC, SharedPackData(PC), Version + 1, Seq) + ' ' + AFS_ROW_TAIL);
              Inc(Joined);
            end
            else
            begin
              if Empty then Id := k + 1
              else
              begin
                Id := NextId;
                Inc(NextId);
              end;
              Rows.Add(SharedRow(Id, PC, Snaps[k - i], 1, Seq));
              if Steam <> '' then BySteam.AddObject(Steam, TObject(PtrInt(Id)))
              else if Hwid <> '' then ByComputer.AddObject(Hwid, TObject(PtrInt(Id)));
              Inc(Added);
            end;
            if Id > MaxId then MaxId := Id;
            Map[k] := Id;
          end;
          i := j;
        end;
        Rows.Flush;
      finally Rows.Free; end;
      if MaxId >= NextId then NextId := MaxId + 1;
      C.Exec('UPDATE af_db_meta SET value = ' + MyInt(NextId) + ' WHERE name = ''next_profile''');
      { the decisions of the admins and the places, the newer one staying }
      LockProfiles;
      try
        VerdCopy := Copy(Verdicts, 0, VerdictCount);
      finally UnlockProfiles; end;
      Rows := TMyBatch.Create(C, AFS_VERDICT_HEAD, AFS_VERDICT_TAIL);
      try
        for i := 0 to High(VerdCopy) do
        begin
          if (VerdCopy[i].A < 0) or (VerdCopy[i].B < 0) or (VerdCopy[i].A >= Length(Map)) or (VerdCopy[i].B >= Length(Map)) then
            Continue;
          Lo := Map[VerdCopy[i].A];
          Hi := Map[VerdCopy[i].B];
          if (Lo <= 0) or (Hi <= 0) or (Lo = Hi) then Continue;
          if Lo > Hi then
          begin
            q := Lo;
            Lo := Hi;
            Hi := q;
          end;
          Rows.Add(SharedVerdictRow(Lo, Hi, VerdCopy[i], SeqText, Sid));
        end;
        Rows.Flush;
      finally Rows.Free; end;
      EnterCriticalSection(GeoCritSec);
      try
        SetLength(GeoCopy, GeoCount);
        for i := 0 to GeoCount - 1 do GeoCopy[i] := GeoCache[i];
      finally LeaveCriticalSection(GeoCritSec); end;
      Rows := TMyBatch.Create(C, AFS_GEO_HEAD, AFS_GEO_TAIL);
      try
        for i := 0 to High(GeoCopy) do
          if string(GeoCopy[i].IP) <> '' then Rows.Add(SharedGeoRow(GeoCopy[i], SeqText, Sid));
        Rows.Flush;
      finally Rows.Free; end;
      C.Exec('INSERT INTO af_db_meta (name, value) VALUES (' + MyStr('joined_' + IntToStr(Settings.MysqlServerId)) + ', ' +
        MyInt(DateTimeToUnix(NowUtc)) + ') ON DUPLICATE KEY UPDATE value = VALUES(value)');
      C.Exec('UPDATE af_db_meta SET value = ' + SeqText + ' WHERE name = ''seq''');
      C.Exec('COMMIT');
      Note := Format('this server joined the database: %d accounts added, %d joined to an account of another ' +
        'server (the same Steam account, or without one the same computer)', [Added, Joined]);
      Result := True;
    except
      on Ex: Exception do
      begin
        try C.Exec('ROLLBACK'); except end;
        Note := 'joining failed: ' + Ex.Message;
      end;
    end;
  finally
    BySteam.Free;
    ByComputer.Free;
  end;
end;

{ everything of the database, read in one consistent view: the account with number k in the
  database is account k - 1 here }
function SharedReadAll(C: TMyConnection; out Note: string): Boolean;
var
  R: TMyResult;
  Cur: TMyCursor;
  Fresh, NewList: TProfileArray;
  Versions: array of Int64;
  Ids: array of LongInt;
  n, i, Id, MaxId, VN, GN, Lo, Hi: Integer;
  Seq: Int64;
  V: TVerdictArray;
  GeoList: TSharedGeo;
  Data: AnsiString;
begin
  Result := False;
  Note := '';
  n := 0;
  MaxId := 0;
  VN := 0;
  GN := 0;
  Fresh := nil;
  NewList := nil;
  Versions := nil;
  Ids := nil;
  V := nil;
  GeoList := nil;
  C.Exec('START TRANSACTION WITH CONSISTENT SNAPSHOT');
  try
    Seq := SharedMeta(C, 'seq', False);
    Cur := C.Open('SELECT id, version, data FROM af_db_profiles WHERE data IS NOT NULL');
    try
      while Cur.Next do
      begin
        Id := Cur.Int(0);
        if (Id <= 0) or (Id > MAX_PROFILES_HARD) then Continue;
        if n >= Length(Fresh) then
        begin
          SetLength(Fresh, n * 2 + 1024);
          SetLength(Versions, n * 2 + 1024);
          SetLength(Ids, n * 2 + 1024);
        end;
        Data := Cur.Str(2);
        if not SharedUnpack(Data, Fresh[n]) then Continue;
        Ids[n] := Id;
        Versions[n] := Cur.Int(1);
        if Id > MaxId then MaxId := Id;
        Inc(n);
      end;
    finally Cur.Free; end;
    R := C.Query('SELECT a, b, verdict, decided, decided_by FROM af_db_verdicts WHERE verdict > 0');
    try
      SetLength(V, R.RowCount);
      for i := 0 to R.RowCount - 1 do
      begin
        FillChar(V[VN], SizeOf(TPairVerdict), 0);
        V[VN].A := R.Int(i, 0);
        V[VN].B := R.Int(i, 1);
        V[VN].Verdict := R.Int(i, 2);
        V[VN].Decided := R.Float(i, 3);
        SaneDate(V[VN].Decided);
        SetGeoString(V[VN].By, R.Str(i, 4));
        Inc(VN);
      end;
    finally R.Free; end;
    Cur := C.Open('SELECT data FROM af_db_geo');
    try
      while Cur.Next do
      begin
        Data := Cur.Str(0);
        if Length(Data) <> SizeOf(TGeoEntry) then Continue;
        if GN >= Length(GeoList) then SetLength(GeoList, GN * 2 + 256);
        Move(Data[1], GeoList[GN], SizeOf(TGeoEntry));
        Inc(GN);
      end;
    finally Cur.Free; end;
    C.Exec('COMMIT');
  except
    on Ex: Exception do
    begin
      try C.Exec('ROLLBACK'); except end;
      Note := 'reading failed: ' + Ex.Message;
      Exit;
    end;
  end;
  { The accounts are put in their places before the lock is taken, and memory gets the new list
    in one step. The old list used to be thrown away first: a new one that could not be had (the
    memory of a 32-bit server) left the server without any. A number that no row has is an
    empty account here. }
  try
    SetLength(NewList, MaxId + 100);
  except
    on Ex: Exception do
    begin
      Note := Format('reading failed: no room for %d accounts (%s)', [MaxId, Ex.Message]);
      Exit;
    end;
  end;
  for i := 0 to n - 1 do
  begin
    NewList[Ids[i] - 1] := Fresh[i];
    MarkProfileFirstUsed(NewList[Ids[i] - 1]);
  end;
  Fresh := nil;
  LockProfiles;
  try
    Profiles := NewList;
    NewList := nil;
    ProfileCount := MaxId;
    SharedForget;
    for i := 0 to n - 1 do
    begin
      SharedLink(Ids[i] - 1, Ids[i]);
      SharedVersion[Ids[i] - 1] := Versions[i];
    end;
    Dirty := nil;
    SetLength(Dirty, Length(Profiles));
    DirtyCount := 0;
    SetLength(Verdicts, VN + 100);
    VerdictCount := 0;
    for i := 0 to VN - 1 do
    begin
      Lo := V[i].A - 1;
      Hi := V[i].B - 1;
      if (Lo < 0) or (Hi < 0) or (Lo >= ProfileCount) or (Hi >= ProfileCount) or (Lo = Hi) then Continue;
      if VerdictCount >= MAX_VERDICTS then Break;
      Verdicts[VerdictCount] := V[i];
      Verdicts[VerdictCount].A := Min(Lo, Hi);
      Verdicts[VerdictCount].B := Max(Lo, Hi);
      Inc(VerdictCount);
    end;
    RebuildVerdictIndex;
    RebuildProfileIndexes;
  finally UnlockProfiles; end;
  EnterCriticalSection(GeoCritSec);
  try
    for i := 0 to GN - 1 do SharedTakeGeoLocked(GeoList[i], True);
    RebuildGeoIndex;
  finally LeaveCriticalSection(GeoCritSec); end;
  EnterCriticalSection(SharedCS);
  try
    SharedPullSeq := Seq;
    { the file on disk has the numbers it had until it is written again }
    SharedIdsReady := False;
  finally LeaveCriticalSection(SharedCS); end;
  Note := Format('%d accounts and %d decisions of the admins read', [n, VerdictCount]);
  Result := True;
end;

{ at a start from the file: what this server has that the database may lack - accounts it never
  sent, and the decisions of the admins (the newer decision stays, so one sent twice changes
  nothing) }
procedure SharedQueueLocal;
var
  i: Integer;
  Empty: AnsiString;
begin
  Empty := SharedEmptyData;
  LockProfiles;
  try
    SharedGrow(ProfileCount);
    for i := 0 to ProfileCount - 1 do
      if (SharedGlobal[i] = 0) and not ProfileIsEmpty(i) then
      begin
        if SharedBase[i] = '' then SharedBase[i] := Empty;
        SharedDirtyArr[i] := 1;
      end;
    SharedVerdictCount := 0;
    if Length(SharedVerdictChanges) < VerdictCount then SetLength(SharedVerdictChanges, VerdictCount + 16);
    for i := 0 to VerdictCount - 1 do
    begin
      SharedVerdictChanges[SharedVerdictCount] := Verdicts[i];
      Inc(SharedVerdictCount);
    end;
  finally UnlockProfiles; end;
end;

{ Engine = mysql, on the thread that reads the database file, after the file (and its journal)
  is in memory. True when the accounts come from the shared database now - they carry its
  numbers, and the file is to be written from them. False when the database could not be
  reached: with the numbers beside the file the server works from the file and the exchange
  starts once the database answers (AllowOffline); without them this run works from the file
  alone, and the server joins the database at a start when it answers. }
function SharedStart(const FilePath: string; AllowOffline: Boolean; out Note: string): Boolean;
var
  C: TMyConnection;
  HasDisk, Queued: Boolean;
  Seq, Size: Int64;
  T, TL: Double;
  Part, Why: string;
begin
  Result := False;
  Note := '';
  Queued := False;
  SharedStopping := False;
  EnterCriticalSection(SharedCS);
  try
    SharedPath := FilePath;
    SharedIdsReady := True;
    SharedPendingOnDisk := False;
    SharedOnline := False;
    SharedEver := False;
    SharedFailures := 0;
    SharedLastError := '';
    SharedRounds := 0;
    SharedWritten := 0;
    SharedRead := 0;
    SharedDiskAt := GetTickCount64;
    if SnapshotStamp(FilePath, Size, T, TL) then
    begin
      SharedFileSize := Size;
      SharedFileTime := T;
    end
    else
    begin
      SharedFileSize := 0;
      SharedFileTime := 0;
    end;
  finally LeaveCriticalSection(SharedCS); end;
  LockProfiles;
  try
    SharedForget;
    HasDisk := SharedReadDisk(Seq);
    SharedOn := True;
  finally UnlockProfiles; end;
  EnterCriticalSection(SharedCS);
  try
    SharedPullSeq := Seq;
    SharedPendingOnDisk := HasDisk;
  finally LeaveCriticalSection(SharedCS); end;
  C := nil;
  try
    try
      C := NewMysqlConnection;
      { A first server that brings many accounts holds the others' turn for a while, and the
        one that waits sees nothing from the database meanwhile: without the longer time limit
        it gave up after 30 seconds, whatever the database said it would wait. This is the
        thread that reads the database, never the game; the end of the server cuts it. }
      C.IOTimeoutMs := 610000;
      C.Connect;
      C.Exec('SET SESSION innodb_lock_wait_timeout = 600');
      SharedPrepare(C);
      if SharedMeta(C, 'joined_' + IntToStr(Settings.MysqlServerId), False) < 0 then
      begin
        { the first start of this server on this database; what lay beside the file belonged to
          another one }
        HasDisk := False;
        LockProfiles;
        try
          SharedForget;
        finally UnlockProfiles; end;
        if not SharedJoin(C, Part) then raise Exception.Create(Part);
        Note := Part;
      end
      else if HasDisk then
      begin
        { what changed here while the database could not be reached goes in before it is read }
        SharedQueueLocal;
        Queued := True;
        DropMysqlConnection(C);
        if not SharedRound(False, Why) then raise Exception.Create(Why);
        C := NewMysqlConnection;
        C.Connect;
      end
      else
        Note := 'nothing beside the file says which account here is which in the database, so the ' +
          'database is taken as it is';
      if not SharedReadAll(C, Part) then raise Exception.Create(Part);
      if Note <> '' then Note := Note + '; ';
      Note := Note + Part;
      EnterCriticalSection(SharedCS);
      try
        SharedOnline := True;
        SharedEver := True;
        SharedLastOk := Now;
      finally LeaveCriticalSection(SharedCS); end;
      Result := True;
    except
      on Ex: Exception do
      begin
        EnterCriticalSection(SharedCS);
        try
          SharedLastError := Ex.Message;
          SharedFailures := 1;
        finally LeaveCriticalSection(SharedCS); end;
        if HasDisk and AllowOffline then
        begin
          if not Queued then SharedQueueLocal;
          Note := Ex.Message + ' - the server works from the file, and its changes go in once the database answers';
        end
        else
        begin
          LockProfiles;
          try
            SharedOn := False;
            SharedForget;
          finally UnlockProfiles; end;
          EnterCriticalSection(SharedCS);
          try
            SharedPath := '';
          finally LeaveCriticalSection(SharedCS); end;
          { nothing says which account here is which in the database, so what this run changes
            cannot go in later: a server that joined before takes the database as it is at its
            next start (only one that never joined brings its file then) }
          Note := Ex.Message + ' - this run works from the file alone, and what it changes does not go to the ' +
            'database';
        end;
      end;
    end;
  finally
    DropMysqlConnection(C);
  end;
  if SharedOn and not ShuttingDown then
  begin
    if SharedWake = nil then SharedWake := RTLEventCreate;
    SharedThread := TSharedThread.Create(False);
  end;
end;

{ the end: the last exchange (a few seconds at most), and the numbers beside the file }
procedure SharedStop;
var
  Deadline: QWord;
begin
  if SharedThread <> nil then
  begin
    SharedStopping := True;
    if SharedWake <> nil then RTLEventSetEvent(SharedWake);
    { ThreadGone: a thread Windows already ended at the exit of the process never says it has
      finished, and waiting for it would take the whole time for nothing }
    Deadline := GetTickCount64 + 5000;
    while not ThreadGone(SharedThread) and (GetTickCount64 < Deadline) do Sleep(10);
    { The database does not answer in time: its connections are cut, and cut again while the
      thread ends - one cut once left the last exchange free to open a new connection and wait
      out its whole time limit (30 seconds) with the server stopping. }
    Deadline := GetTickCount64 + 5000;
    while not ThreadGone(SharedThread) and (GetTickCount64 < Deadline) do
    begin
      AbortMysqlConnections;
      Sleep(20);
    end;
    SharedThread.WaitFor;
    FreeAndNil(SharedThread);
  end;
  if SharedOn then
  begin
    SharedWriteDisk;
    LockProfiles;
    try
      SharedOn := False;
    finally UnlockProfiles; end;
  end;
  if SharedWake <> nil then
  begin
    RTLEventDestroy(SharedWake);
    SharedWake := nil;
  end;
end;

function SharedStatus: string;
begin
  EnterCriticalSection(SharedCS);
  try
    if SharedPath = '' then
    begin
      if SharedLastError <> '' then
        Result := 'not joined (' + SharedLastError + '): this run works from the file alone, and what it changes ' +
          'does not go to the database'
      else
        Result := 'starting';
    end
    else if SharedOnline then
      Result := Format('exchanging every %d s: %d exchanges, %d accounts sent, %d received, the last one at %s',
        [Settings.SyncSeconds, SharedRounds, SharedWritten, SharedRead, FormatDateTime('hh:nn:ss', SharedLastOk)])
    else if SharedEver then
      Result := 'not answering (' + SharedLastError + '), the changes wait here and go in once it answers'
    else
      Result := 'not reached yet (' + SharedLastError + '), working from the file; the changes go in once it answers';
  finally LeaveCriticalSection(SharedCS); end;
  if SharedMissed > 0 then Result := Result + Format(' | %d changes without their state before', [SharedMissed]);
end;

function Get_Mysql_Status(): PChar; cdecl;
var
  Mode, Err: string;
begin
  if Settings.MysqlEngine = ENGINE_BIN then
    TempStringResult := 'off (Engine = local)'
  else if Settings.MysqlHost = '' then
    TempStringResult := 'off (no MysqlHost in settings.ini)'
  else if Settings.MysqlEngine = ENGINE_MYSQL then
    TempStringResult := Format('one database for every server, %s:%d/%s, this is server %d: ', [Settings.MysqlHost,
      Settings.MysqlPort, Settings.MysqlDatabase, Settings.MysqlServerId]) + SharedStatus
  else
  begin
    Mode := 'a copy for websites, read back at start when the file is missing or older';
    EnterCriticalSection(MirrorCS);
    try
      TempStringResult := Format('%s, %s:%d/%s', [Mode, Settings.MysqlHost, Settings.MysqlPort,
        Settings.MysqlDatabase]);
      if MirrorServer <> '' then TempStringResult := TempStringResult + ' (' + MirrorServer + ')';
      if MirrorRuns > 0 then
        TempStringResult := TempStringResult + Format(', %d copies, the last one %s: %d rows in %.1f s',
          [MirrorRuns, FormatDateTime('yyyy-mm-dd hh:nn', MirrorLastRun), MirrorRows, MirrorMillis / 1000])
      else
        TempStringResult := TempStringResult + ', no copy written yet';
      if MirrorBusy then TempStringResult := TempStringResult + ', writing one now';
      Err := MirrorLastError;
    finally LeaveCriticalSection(MirrorCS); end;
    if Err <> '' then TempStringResult := TempStringResult + ' | last problem: ' + Err;
  end;
  Result := PChar(TempStringResult);
end;
