{ ---------------------------------------------------------------------------------------
  AntiFake Extended - where an address is: the cache, the lookups and the thread that asks.
  --------------------------------------------------------------------------------------- }

const
  { ip-api.com answers 45 questions a minute from one address; over that it refuses them
    (HTTP 429), and an address that keeps asking is shut out for an hour. However many joins
    and searches ask, no more than this many lookups start in any 60 seconds. }
  GEO_PER_MINUTE = 40;
  { after a lookup that got no answer nothing is asked for a while: 1 minute, then 2, 4 ... }
  GEO_PAUSE_FIRST = 60;
  GEO_PAUSE_MAX = 1800;
  { The service says how long to wait when its minute is used up (X-Ttl, at most 60 seconds in
    practice); a longer wait than this is not taken from an answer that came over plain HTTP,
    where anyone on the way can write it - 99999999 stopped every lookup until the restart. }
  GEO_TTL_MAX = 300;
  { An answer is a few hundred bytes; one bigger than this, headers included, is refused before
    it is all read, so up to MaxLookups of them never fill the memory of the server. }
  GEO_ANSWER_MAX = 16 * 1024;

var
  { when the last GEO_PER_MINUTE lookups started (GetTickCount64), as a ring. GeoCritSec. }
  GeoAsked: array[0..GEO_PER_MINUTE - 1] of QWord;
  GeoAskedNext: Integer = 0;
  GeoPauseUntil: QWord = 0;     { no lookup starts before this }
  GeoPauseStep: Integer = 0;    { lookups without an answer in a row }

procedure TraceStep(const Step: string); forward;

{ a text for the trace with every byte above 127 as <XX>: the letters of a place as they came }
function TraceBytes(const S: string): string;
var
  k: Integer;
begin
  Result := '';
  for k := 1 to Length(S) do
    if Byte(S[k]) > 127 then Result := Result + '<' + IntToHex(Byte(S[k]), 2) + '>'
    else Result := Result + S[k];
end;

procedure StartGeoLookup(const IP: string);
var
  k: Integer;
  T: QWord;
begin
  { [Geo] Enabled = 0: no address leaves the server, whichever command or join asks }
  if not Settings.GeoEnabled then Exit;
  if IsLocalAddress(IP) or ShuttingDown then Exit;
  { the game thread calls this at a join: it asks again at the next one rather than wait (the
    searches that call it hold the lock already, and take it again at once) }
  if not GrabLock(GeoCritSec, 5) then Exit;
  try
    if ShuttingDown then Exit;
    if (GeoPending.IndexOf(IP) >= 0) or (GeoPending.Count >= Settings.GeoMaxLookups) then Exit;
    T := GetTickCount64;
    { the service refused or did not answer a moment ago }
    if T < GeoPauseUntil then Exit;
    { the oldest of the last GEO_PER_MINUTE lookups is less than a minute old }
    if (GeoAsked[GeoAskedNext] <> 0) and (T - GeoAsked[GeoAskedNext] < 60000) then Exit;
    k := GeoFailed.IndexOfName(IP);
    if (k >= 0) and (Int64(T div 1000) - StrToInt64Def(GeoFailed.ValueFromIndex[k], 0) < Settings.GeoRetrySeconds) then Exit;
    GeoPending.Add(IP);
    if Settings.Trace then TraceStep('geo lookup of ' + IP + ' started');
    Inc(GeoThreadsRunning);
    try
      StartBackground(TGeoThread.Create(IP));
      GeoAsked[GeoAskedNext] := T;
      GeoAskedNext := (GeoAskedNext + 1) mod GEO_PER_MINUTE;
    except
      Dec(GeoThreadsRunning);
      GeoPending.Delete(GeoPending.IndexOf(IP));
    end;
  finally
    LeaveCriticalSection(GeoCritSec);
  end;
end;

{ a lookup ended. Pause is how long no lookup may start: the seconds the service asked for, 0
  for none, -1 for the next step of the pause after a lookup that got no answer. }
procedure GeoLookupDone(const IP: string; Found: Boolean; Pause: Integer);
var
  k: Integer;
  T: QWord;
begin
  EnterCriticalSection(GeoCritSec);
  try
    if GeoThreadsRunning > 0 then
      Dec(GeoThreadsRunning);
    k := GeoPending.IndexOf(IP);
    if k >= 0 then GeoPending.Delete(k);
    k := GeoFailed.IndexOfName(IP);
    if k >= 0 then GeoFailed.Delete(k);
    T := GetTickCount64;
    if not Found then
    begin
      if GeoFailed.Count >= 1000 then GeoFailed.Delete(0);
      GeoFailed.Add(IP + '=' + IntToStr(T div 1000));
    end;
    if Pause < 0 then
    begin
      Pause := Min(GEO_PAUSE_MAX, GEO_PAUSE_FIRST shl Min(GeoPauseStep, 5));
      Inc(GeoPauseStep);
    end
    else if Found then
      GeoPauseStep := 0;
    if (Pause > 0) and (T + QWord(Pause) * 1000 > GeoPauseUntil) then
      GeoPauseUntil := T + QWord(Pause) * 1000;
  finally
    LeaveCriticalSection(GeoCritSec);
  end;
end;

{ a header of the answer, '' when it has none }
function GeoHeader(Headers: TStrings; const Name: string): string;
var
  k, p: Integer;
begin
  Result := '';
  if Headers = nil then Exit;
  for k := 0 to Headers.Count - 1 do
  begin
    p := Pos(':', Headers[k]);
    if (p > 1) and SameText(Trim(Copy(Headers[k], 1, p - 1)), Name) then
    begin
      Result := Trim(Copy(Headers[k], p + 1, MaxInt));
      Exit;
    end;
  end;
end;

{ The limits of one lookup, checked at every read of the connection. TimeoutMs of the client
  holds for each read alone, so an answer that came a byte at a time kept the thread for hours,
  longer than the end of the library waits for it; the whole lookup now ends after twice
  TimeoutMs (and the read running then), whatever the other side sends. }
type
  EGeoLimit = class(Exception);

  TGeoSocket = class(TSocketHandler)
  public
    Deadline: QWord;
    Got: Integer;
    function Recv(const Buffer; Count: Integer): Integer; override;
  end;

  TGeoClient = class(TFPHTTPClient)
  public
    Deadline: QWord;
    procedure GiveSocket(Sender: TObject; const UseSSL: Boolean; out AHandler: TSocketHandler);
  end;

function TGeoSocket.Recv(const Buffer; Count: Integer): Integer;
begin
  if GetTickCount64 > Deadline then raise EGeoLimit.Create('the answer took too long');
  Result := inherited Recv(Buffer, Count);
  if Result > 0 then
  begin
    Inc(Got, Result);
    if Got > GEO_ANSWER_MAX then raise EGeoLimit.Create('the answer is too big');
  end;
end;

procedure TGeoClient.GiveSocket(Sender: TObject; const UseSSL: Boolean; out AHandler: TSocketHandler);
begin
  { the plain one: the lookup never goes over TLS }
  AHandler := TGeoSocket.Create;
  TGeoSocket(AHandler).Deadline := Deadline;
end;

{ a pause the service asked for, kept within GEO_TTL_MAX }
function GeoTtl(Headers: TStrings): Integer;
begin
  Result := Min(GEO_TTL_MAX, Max(1, StrToIntDef(GeoHeader(Headers, 'X-Ttl'), GEO_PAUSE_FIRST)));
end;

constructor TGeoThread.Create(const TargetIP: string);
begin
  inherited Create(True);
  FIP := TargetIP;
end;

{ The text of one string field of the flat object ip-api.com answers with, as UTF-8 bytes, read
  byte by byte out of the body. No conversion of the run time library touches it: on a Linux
  server without a locale the letters outside ASCII came out as "?" ("Poland, ?wilcza"). Def when
  the field is not there or is not a string. }
function JsonField(const Body, Name, Def: string): string;
var
  p, n: Integer;
  u: LongWord;
begin
  Result := Def;
  p := Pos('"' + Name + '":', Body);
  if p = 0 then Exit;
  p := p + Length(Name) + 3;
  n := Length(Body);
  while (p <= n) and (Body[p] = ' ') do Inc(p);
  if (p > n) or (Body[p] <> '"') then Exit;
  Inc(p);
  Result := '';
  while p <= n do
  begin
    if Body[p] = '"' then Exit;
    if (Body[p] = '\') and (p < n) then
    begin
      Inc(p);
      case Body[p] of
        'u':
          if p + 4 <= n then
          begin
            u := StrToIntDef('$' + Copy(Body, p + 1, 4), 63);
            Inc(p, 4);
            { half of a pair beyond U+FFFF: no letter of a place }
            if (u >= $D800) and (u <= $DFFF) then u := 63;
            if u < $80 then Result := Result + Chr(u)
            else if u < $800 then Result := Result + Chr($C0 or (u shr 6)) + Chr($80 or (u and $3F))
            else Result := Result + Chr($E0 or (u shr 12)) + Chr($80 or ((u shr 6) and $3F)) + Chr($80 or (u and $3F));
          end;
        'n', 'r', 't', 'b', 'f': Result := Result + ' ';
      else
        Result := Result + Body[p];
      end;
    end
    else
      Result := Result + Body[p];
    Inc(p);
  end;
  { no closing quote: a broken answer }
  Result := Def;
end;

{ The object of a JSON answer that holds "Key":"Value" and no other object, as raw bytes: the
  entry of one player in the answer of Steam. Strings are skipped whole, so a brace in a name
  does not count. '' when there is none. }
function JsonObjectWith(const Body, Key, Value: string): string;
var
  p, n, Depth: Integer;
  InStr: Boolean;
  Starts: array of Integer;
  Part: string;
begin
  Result := '';
  n := Length(Body);
  Depth := 0;
  InStr := False;
  SetLength(Starts, 0);
  p := 1;
  while p <= n do
  begin
    if InStr then
    begin
      if Body[p] = '\' then Inc(p)
      else if Body[p] = '"' then InStr := False;
    end
    else if Body[p] = '"' then InStr := True
    else if Body[p] = '{' then
    begin
      Inc(Depth);
      SetLength(Starts, Depth);
      Starts[Depth - 1] := p;
    end
    else if (Body[p] = '}') and (Depth > 0) then
    begin
      Part := Copy(Body, Starts[Depth - 1], p - Starts[Depth - 1] + 1);
      Dec(Depth);
      if Pos('"' + Key + '":"' + Value + '"', Part) > 0 then
      begin
        Result := Part;
        Exit;
      end;
    end;
    Inc(p);
  end;
end;

procedure TGeoThread.Execute;
var
  Client: TGeoClient;
  Response: string;
  Body: TMemoryStream;
  JData: TJSONData;
  JObj: TJSONObject;
  Idx, Pause: Integer;
  Found: Boolean;
begin
  GuardWorkerThread;
  Found := False;
  Pause := 0;
  { whatever happens, the thread has to report that it is gone: Cleanup counts on it }
  try
  Client := TGeoClient.Create(nil);
  Client.ConnectTimeout := Settings.GeoTimeoutMs;
  Client.IOTimeout := Settings.GeoTimeoutMs;
  Client.Deadline := GetTickCount64 + 2 * QWord(Settings.GeoTimeoutMs);
  Client.OnGetSocketHandler := @Client.GiveSocket;
  try
    try
      { the bytes of the body as they came over the wire }
      Body := TMemoryStream.Create;
      try
        Client.Get('http://ip-api.com/json/' + FIP + '?fields=20181819', Body);
        Response := '';
        SetLength(Response, Body.Size);
        if Body.Size > 0 then Move(Body.Memory^, Response[1], Body.Size);
      finally
        Body.Free;
      end;
      { the last question of the service's minute: the next lookup waits for the next one }
      if GeoHeader(Client.ResponseHeaders, 'X-Rl') = '0' then
        Pause := GeoTtl(Client.ResponseHeaders);

      JData := GetJSON(AsUtf8(Response));
      try
        { an answer that is not an object, or has no status, is no place (it used to end in an
          access violation that only the except below caught) }
        if (JData is TJSONObject) and (TJSONObject(JData).Get('status', '') = 'success') then
        begin
          JObj := TJSONObject(JData);

          EnterCriticalSection(GeoCritSec);
          try
            Idx := FindGeoIndex(FIP);
            if Idx = -1 then begin
              Idx := GeoCount;
              Inc(GeoCount);
              if GeoCount > Length(GeoCache) then
                SetLength(GeoCache, GeoCount * 2 + 50);
              InitGeoEntry(GeoCache[Idx], FIP);
              IndexAdd(GeoIndex, FIP, Idx);
            end;

            with GeoCache[Idx] do begin
              SetGeoString(Continent, JsonField(Response, 'continent', 'UNK'));
              SetGeoString(ContinentCode, JsonField(Response, 'continentCode', 'UNK'));

              SetGeoString(Country, JsonField(Response, 'country', 'Unknown'));
              SetGeoString(CountryCode, JsonField(Response, 'countryCode', '??'));
              SetGeoString(Region, JsonField(Response, 'regionName', 'Unknown'));
              SetGeoString(City, JsonField(Response, 'city', 'Unknown'));
              if Settings.Trace then
                TraceStep('geo answer for ' + FIP + ': city ' + TraceBytes(JsonField(Response, 'city', '')) +
                  ' (fpjson: ' + TraceBytes(Utf8Bytes(JObj.Get('city', ''))) + '), kept as ' + TraceBytes(string(City)));
              SetGeoString(Zip, JsonField(Response, 'zip', ''));

              SetGeoString(ISP, JsonField(Response, 'isp', 'Unknown'));
              SetGeoString(ReverseDNS, JsonField(Response, 'reverse', ''));
              SetGeoString(Timezone, JsonField(Response, 'timezone', ''));

              if JObj.Get('mobile', False) then IsMobile := 1 else IsMobile := 0;
              if JObj.Get('proxy', False) then IsProxy := 1 else IsProxy := 0;
              if JObj.Get('hosting', False) then IsHosting := 1 else IsHosting := 0;

              LastUpdated := Now;
              Found := True;
            end;
            { Engine = mysql: the other servers get the place at the next exchange (once, however
              often it was located meanwhile) }
            if SharedOn and (SharedGeoNew <> nil) and (SharedGeoNew.IndexOf(FIP) < 0) then
              SharedGeoNew.Add(FIP);
          finally
            LeaveCriticalSection(GeoCritSec);
          end;
        end;
      finally
        JData.Free;
      end;
    except
      { refused for asking too often (429: as long as it says), or no answer at all: every
        lookup waits, longer after every failure in a row }
      if Settings.Trace then
        if ExceptObject is Exception then
          TraceStep('geo lookup of ' + FIP + ' failed (HTTP ' + IntToStr(Client.ResponseStatusCode) + '): ' +
            Exception(ExceptObject).Message);
      if Client.ResponseStatusCode = 429 then
        Pause := GeoTtl(Client.ResponseHeaders)
      else
        Pause := -1;
    end;
  finally
    Client.Free;
  end;
  finally
    GeoLookupDone(FIP, Found, Pause);
  end;
end;

{ A date read from a file, a journal or another server that is no date at all - not a number,
  infinite, before 1899 or after the year 9999 - is taken as "never". The bits are looked at
  first: the FPU raises an exception for comparing a NaN, and one at a join (on the game
  thread) unloaded the script. }
procedure SaneDate(var D: Double);
begin
  if ((PQWord(@D)^ shr 52) and $7FF) = $7FF then D := 0
  else if (D < 0) or (D > 2958465) then D := 0;
end;

{ every date of a profile; the caller owns the record }
procedure SaneProfile(var P: TProfileData);

  procedure List(var Arr: TBinaryOtherDataArray);
  var
    k: Integer;
  begin
    for k := 0 to High(Arr) do
    begin
      SaneDate(Arr[k].FirstUsedDate);
      SaneDate(Arr[k].LastUsedDate);
    end;
  end;

var
  k: Integer;
begin
  SaneDate(P.Profile.RegisterTime);
  SaneDate(P.Profile.LastPlayedTime);
  List(P.OtherNames);
  List(P.OtherSteams);
  List(P.OtherHWIDs);
  List(P.OtherIPs);
  for k := 0 to High(P.Visits) do
  begin
    SaneDate(P.Visits[k].Started);
    if P.Visits[k].Seconds < 0 then P.Visits[k].Seconds := 0;
  end;
  SaneDate(P.Steam.Created);
  SaneDate(P.Steam.Fetched);
end;

function GetOtherDataString(const Data: TBinaryOtherData): string;
begin
  Result := Copy(Data.DataString, 1, Data.DataLen);
end;

procedure SetOtherDataString(var Data: TBinaryOtherData; const Value: string);
var Len: Integer;
begin
  Len := Length(Value);
  if Len > MAX_STRING_LEN then Len := MAX_STRING_LEN;
  FillChar(Data.DataString, MAX_STRING_LEN, 0);
  if Len > 0 then Move(Value[1], Data.DataString, Len);
  Data.DataLen := Len;
end;

{ With Trace = 1 every step of the library is appended to logs/antifake/trace.log, one line at
  a time and closed again, so the file is complete even if the server has to be killed. }
var
  TraceCS: TRTLCriticalSection;
  TraceReady: Boolean = False;

procedure TraceStep(const Step: string);
var
  F: TextFile;
begin
  if not Settings.Trace then Exit;
  if not TraceReady then Exit;
  EnterCriticalSection(TraceCS);
  try
    AssignFile(F, LOG_DIR + DirectorySeparator + 'trace.log');
    try
      ForceDirectories(LOG_DIR);
      if FileExists(LOG_DIR + DirectorySeparator + 'trace.log') then Append(F) else Rewrite(F);
      { the HWID of a player comes from the client unfiltered: no control characters (LogSafe) }
      WriteLn(F, FormatDateTime('hh:nn:ss.zzz', Now), '  ', LogSafe(Step));
      CloseFile(F);
    except
    end;
  finally
    LeaveCriticalSection(TraceCS);
  end;
end;

{ how sure we are that the player really sits where the address says; the caller holds
  GeoCritSec }
function VisitConfidence(GeoIdx: Integer; out Flags: Byte; out Country, Where: string): Integer;
begin
  Flags := 0;
  Country := '';
  Where := '';
  if GeoIdx < 0 then
  begin
    Flags := VF_UNKNOWN;
    Result := 0;
    Exit;
  end;
  Country := string(GeoCache[GeoIdx].CountryCode);
  Where := string(GeoCache[GeoIdx].Country);
  if string(GeoCache[GeoIdx].City) <> '' then
    Where := Where + ', ' + string(GeoCache[GeoIdx].City);
  Result := 100;
  if GeoCache[GeoIdx].IsProxy = 1 then
  begin
    Flags := Flags or VF_PROXY;
    Dec(Result, 60);
  end;
  if GeoCache[GeoIdx].IsHosting = 1 then
  begin
    Flags := Flags or VF_HOSTING;
    Dec(Result, 50);
  end;
  if GeoCache[GeoIdx].IsMobile = 1 then
  begin
    Flags := Flags or VF_MOBILE;
    Dec(Result, 15);
  end;
  if string(GeoCache[GeoIdx].ReverseDNS) = '' then
    Dec(Result, 5);
  if Result < 0 then Result := 0;
  if Result > 100 then Result := 100;
end;

{ does this visit belong in the answer: inside the time range, and masked or clean when asked }
function VisitWanted(const V: TVisitEntry; FromT, ToT: Double; OnlyMasked, OnlyClean: Boolean): Boolean;
begin
  Result := False;
  if (FromT > 0) and (V.Started < FromT) then Exit;
  if (ToT > 0) and (V.Started > ToT) then Exit;
  if OnlyMasked and ((V.Flags and (VF_PROXY or VF_HOSTING)) = 0) then Exit;
  if OnlyClean and ((V.Flags and (VF_PROXY or VF_HOSTING)) <> 0) then Exit;
  Result := True;
end;

{ how far back a visit list reaches: "7d", "30d", "all" or "2026-09-01..2026-09-20".
  Returns False when the text says nothing about time. }
function ParseVisitRange(const Phrase: string; out FromT, ToT: Double): Boolean;
var
  P, Days: Integer;
  A, B: string;
  Day: TDateTime;

  { "2026-09-01" as a day; a day that does not exist (2026-02-31) is none: EncodeDate raised an
    exception there, before the search had even started, and the slot stayed busy for good }
  function DayOf(const S: string; out Value: TDateTime): Boolean;
  var
    Y, M, D: Integer;
  begin
    Value := 0;
    Y := StrToIntDef(Copy(S, 1, 4), 0);
    M := StrToIntDef(Copy(S, 6, 2), 0);
    D := StrToIntDef(Copy(S, 9, 2), 0);
    Result := (Y > 1990) and (Y < 10000) and (M >= 1) and (M <= 12) and (D >= 1) and (D <= 31) and
      TryEncodeDate(Y, M, D, Value);
  end;

begin
  Result := False;
  FromT := 0;
  ToT := 0;
  A := LowerCase(Trim(Phrase));
  if (A = '') or (A = 'all') then Exit;
  P := Pos('..', A);
  if P > 1 then
  begin
    B := Copy(A, P + 2, Length(A));
    A := Copy(A, 1, P - 1);
    if (Length(A) = 10) and (Length(B) = 10) then
    begin
      if DayOf(A, Day) then FromT := Day;
      if DayOf(B, Day) then ToT := Day + 1;
      Result := (FromT > 0) or (ToT > 0);
    end;
    Exit;
  end;
  if (Length(A) >= 2) and (A[Length(A)] = 'd') then
  begin
    Days := StrToIntDef(Copy(A, 1, Length(A) - 1), 0);
    if Days > 0 then
    begin
      FromT := Now - Days;
      Result := True;
    end;
  end;
end;

{ the newest visit of a profile, or -1 }
function NewestVisit(Index: Integer): Integer;
var
  k: Integer;
begin
  Result := -1;
  for k := 0 to High(Profiles[Index].Visits) do
    if (Result < 0) or (Profiles[Index].Visits[k].Started > Profiles[Index].Visits[Result].Started) then
      Result := k;
end;

const
  { the most visits of one account that a file, a journal or an exchange is believed to hold;
    more is a damaged count, not a list }
  MAX_VISITS_READ = 100000;

{ How many visits an account keeps. VisitKeepCount = 0 ("no limit") is the most any account
  can hold: the file, the journal and the exchange of Engine = mysql carry no more than that,
  and an account with more used to end the reading of everything after it. }
function VisitLimit: Integer;
begin
  Result := Settings.VisitKeepCount;
  if (Result <= 0) or (Result > MAX_VISITS_HARD) then Result := MAX_VISITS_HARD;
end;

{ keeps the Keep newest visits of V (by their start), in the order they had; for a list read
  from a file or a journal that holds more than an account may keep }
procedure KeepNewestVisits(var V: TVisitArray; Keep: Integer);
var
  Top: array of Double;     { the latest starts, latest first }
  Filled, i, j, Above, TieRoom: Integer;
  Cut: Double;
  R: TVisitArray;
begin
  if (Keep <= 0) or (Length(V) <= Keep) then Exit;
  SetLength(Top, Keep);
  Filled := 0;
  for i := 0 to High(V) do
  begin
    if (Filled = Keep) and (V[i].Started <= Top[Keep - 1]) then Continue;
    if Filled < Keep then Inc(Filled);
    j := Filled - 1;
    while (j > 0) and (Top[j - 1] < V[i].Started) do
    begin
      Top[j] := Top[j - 1];
      Dec(j);
    end;
    Top[j] := V[i].Started;
  end;
  Cut := Top[Keep - 1];
  Above := 0;
  for i := 0 to High(V) do
    if V[i].Started > Cut then Inc(Above);
  { of the visits that started at the cut, the last ones in the list are kept }
  TieRoom := Keep - Above;
  SetLength(R, Keep);
  j := Keep;
  for i := High(V) downto 0 do
    if (V[i].Started > Cut) or ((V[i].Started = Cut) and (TieRoom > 0)) then
    begin
      if V[i].Started = Cut then Dec(TieRoom);
      Dec(j);
      R[j] := V[i];
    end;
  V := R;
end;

{ the caller holds ProfilesCritSec }
procedure TrimVisits(Index: Integer);
var
  k, Oldest, Keep: Integer;
  Limit: Double;
begin
  if Settings.VisitKeepDays > 0 then
  begin
    Limit := Now - Settings.VisitKeepDays;
    for k := High(Profiles[Index].Visits) downto 0 do
      if Profiles[Index].Visits[k].Started < Limit then
      begin
        for Oldest := k to High(Profiles[Index].Visits) - 1 do
          Profiles[Index].Visits[Oldest] := Profiles[Index].Visits[Oldest + 1];
        SetLength(Profiles[Index].Visits, Length(Profiles[Index].Visits) - 1);
      end;
  end;
  Keep := VisitLimit;
  while Length(Profiles[Index].Visits) > Keep do
  begin
    Oldest := 0;
    for k := 1 to High(Profiles[Index].Visits) do
      if Profiles[Index].Visits[k].Started < Profiles[Index].Visits[Oldest].Started then
        Oldest := k;
    for k := Oldest to High(Profiles[Index].Visits) - 1 do
      Profiles[Index].Visits[k] := Profiles[Index].Visits[k + 1];
    SetLength(Profiles[Index].Visits, Length(Profiles[Index].Visits) - 1);
  end;
end;

{ One call does everything: the script says "this account played another N seconds under this
  name, from this computer and this address". A visit that is still running (same address, last
  touched less than VisitCooldownMinutes ago) is extended instead of a new one being written, so
  a player who joins and leaves in a loop cannot pump the list. }
procedure Visit_Note(Index: Integer; Name, Hwid, IP: PChar; AddSeconds: Integer); cdecl;
var
  k, GeoIdx, Conf: Integer;
  SIP, SName, SHwid, Country, Where: string;
  Flags: Byte;
  NewIP, NewHwid: Boolean;
  T: Double;
begin
  try
    SIP := string(IP);
    SName := string(Name);
    SHwid := string(Hwid);
    if (SIP = '') and (SName = '') then Exit;
    Conf := 0;
    Flags := 0;
    Country := '';
    Where := '';
    if GrabLock(GeoCritSec, 20) then
    try
      GeoIdx := FindGeoIndex(SIP);
      Conf := VisitConfidence(GeoIdx, Flags, Country, Where);
    finally LeaveCriticalSection(GeoCritSec); end;

    if not GrabProfiles(20) then Exit;
    try
      if (Index < 0) or (Index >= ProfileCount) then Exit;
      SharedTouch(Index);
      T := Now;
      k := NewestVisit(Index);
      if (k >= 0) and (string(Profiles[Index].Visits[k].IP) = SIP) and
         ((Settings.VisitCooldownMinutes <= 0) or
          (T - (Profiles[Index].Visits[k].Started + Profiles[Index].Visits[k].Seconds / 86400.0) <
           Settings.VisitCooldownMinutes / 1440.0)) then
      begin
        { the same visit goes on }
        Inc(Profiles[Index].Visits[k].Seconds, AddSeconds);
        if Conf > 0 then
        begin
          Profiles[Index].Visits[k].Confidence := Conf;
          Profiles[Index].Visits[k].Flags := (Profiles[Index].Visits[k].Flags and (VF_NEW_IP or VF_NEW_HWID)) or Flags;
          SetGeoString(Profiles[Index].Visits[k].CountryCode, Country);
        end;
        MarkDirty(Index);
        Exit;
      end;
      { a new visit: is the address or the computer new for this account? }
      NewIP := True;
      for k := 0 to High(Profiles[Index].OtherIPs) do
        if GetOtherDataString(Profiles[Index].OtherIPs[k]) = SIP then
        begin
          NewIP := Profiles[Index].OtherIPs[k].LoginCount <= 1;
          Break;
        end;
      NewHwid := True;
      for k := 0 to High(Profiles[Index].OtherHWIDs) do
        if GetOtherDataString(Profiles[Index].OtherHWIDs[k]) = SHwid then
        begin
          NewHwid := Profiles[Index].OtherHWIDs[k].LoginCount <= 1;
          Break;
        end;
      k := Length(Profiles[Index].Visits);
      SetLength(Profiles[Index].Visits, k + 1);
      FillChar(Profiles[Index].Visits[k], SizeOf(TVisitEntry), 0);
      Profiles[Index].Visits[k].Started := T;
      Profiles[Index].Visits[k].Seconds := AddSeconds;
      Profiles[Index].Visits[k].Confidence := Conf;
      if NewIP then Flags := Flags or VF_NEW_IP;
      if NewHwid and (SHwid <> '') then Flags := Flags or VF_NEW_HWID;
      Profiles[Index].Visits[k].Flags := Flags;
      SetGeoString(Profiles[Index].Visits[k].IP, SIP);
      SetGeoString(Profiles[Index].Visits[k].Name, SName);
      SetGeoString(Profiles[Index].Visits[k].Hwid, Copy(SHwid, 1, 15));
      SetGeoString(Profiles[Index].Visits[k].CountryCode, Country);
      TrimVisits(Index);
      MarkDirty(Index);
    finally UnlockProfiles; end;
  except
    ExportFailed('Visit_Note');
  end;
end;

function Get_Visit_Count(Index: Integer): Integer; cdecl;
begin
  Result := 0;
  LockProfiles;
  try
    if (Index >= 0) and (Index < ProfileCount) then
      Result := Length(Profiles[Index].Visits);
  finally UnlockProfiles; end;
end;

{ the caller holds ProfilesCritSec }
procedure RebuildProfileIndexes;
var
  i, j: Integer;
  Key: string;
begin
  if (SteamIndex = nil) or (HwidIndex = nil) or (IpIndex = nil) or (Net24Index = nil) then Exit;
  SteamIndex.Sorted := False;
  HwidIndex.Sorted := False;
  IpIndex.Sorted := False;
  Net24Index.Sorted := False;
  SteamIndex.Clear;
  HwidIndex.Clear;
  IpIndex.Clear;
  Net24Index.Clear;
  for i := 0 to ProfileCount - 1 do
  begin
    for j := 0 to Length(Profiles[i].OtherSteams) - 1 do
    begin
      Key := GetOtherDataString(Profiles[i].OtherSteams[j]);
      if Key <> '' then SteamIndex.AddObject(Key, TObject(PtrInt(i)));
    end;
    for j := 0 to Length(Profiles[i].OtherHWIDs) - 1 do
    begin
      Key := GetOtherDataString(Profiles[i].OtherHWIDs[j]);
      if Key <> '' then HwidIndex.AddObject(Key, TObject(PtrInt(i)));
    end;
    for j := 0 to Length(Profiles[i].OtherIPs) - 1 do
    begin
      Key := GetOtherDataString(Profiles[i].OtherIPs[j]);
      if Key <> '' then
      begin
        IpIndex.AddObject(Key, TObject(PtrInt(i)));
        Net24Index.AddObject(Net24Of(Key), TObject(PtrInt(i)));
      end;
    end;
  end;
  SteamIndex.Sorted := True;
  HwidIndex.Sorted := True;
  IpIndex.Sorted := True;
  Net24Index.Sorted := True;
end;

{ nothing is left of this profile: the list skips it and the next new player takes the slot.
  the caller holds ProfilesCritSec }
function ProfileIsEmpty(Index: Integer): Boolean;
begin
  Result := (Index >= 0) and (Index < ProfileCount) and
            (Length(Profiles[Index].OtherNames) = 0) and (Length(Profiles[Index].OtherSteams) = 0) and
            (Length(Profiles[Index].OtherHWIDs) = 0) and (Length(Profiles[Index].OtherIPs) = 0);
end;

function GetProfileActiveName(Index: Integer): string;
begin
  if (Index >= 0) and (Index < ProfileCount) then
    Result := Copy(Profiles[Index].Profile.ActiveName, 1, Profiles[Index].Profile.ActiveNameLen)
  else Result := '';
end;

procedure SetProfileActiveName(Index: Integer; const Value: string);
var Len: Integer;
begin
  if (Index >= 0) and (Index < ProfileCount) then
  begin
    Len := Length(Value);
    if Len > MAX_STRING_LEN then Len := MAX_STRING_LEN;
    FillChar(Profiles[Index].Profile.ActiveName, MAX_STRING_LEN, 0);
    if Len > 0 then Move(Value[1], Profiles[Index].Profile.ActiveName, Len);
    Profiles[Index].Profile.ActiveNameLen := Len;
  end;
end;

function GetProfileActiveSteam(Index: Integer): string;
begin
  if (Index >= 0) and (Index < ProfileCount) then
    Result := Copy(Profiles[Index].Profile.ActiveSteam, 1, Profiles[Index].Profile.ActiveSteamLen)
  else Result := '';
end;

procedure SetProfileActiveSteam(Index: Integer; const Value: string);
var Len: Integer;
begin
  if (Index >= 0) and (Index < ProfileCount) then
  begin
    Len := Length(Value);
    if Len > 31 then Len := 31;
    FillChar(Profiles[Index].Profile.ActiveSteam, 32, 0);
    if Len > 0 then Move(Value[1], Profiles[Index].Profile.ActiveSteam, Len);
    Profiles[Index].Profile.ActiveSteamLen := Len;
  end;
end;

function GetProfileActiveHWID(Index: Integer): string;
begin
  if (Index >= 0) and (Index < ProfileCount) then
    Result := Copy(Profiles[Index].Profile.ActiveHWID, 1, Profiles[Index].Profile.ActiveHWIDLen)
  else Result := '';
end;

procedure SetProfileActiveHWID(Index: Integer; const Value: string);
var Len: Integer;
begin
  if (Index >= 0) and (Index < ProfileCount) then
  begin
    Len := Length(Value);
    if Len > 31 then Len := 31;
    FillChar(Profiles[Index].Profile.ActiveHWID, 32, 0);
    if Len > 0 then Move(Value[1], Profiles[Index].Profile.ActiveHWID, Len);
    Profiles[Index].Profile.ActiveHWIDLen := Len;
  end;
end;

function GetProfileActiveIP(Index: Integer): string;
begin
  if (Index >= 0) and (Index < ProfileCount) then
    Result := Copy(Profiles[Index].Profile.ActiveIP, 1, Profiles[Index].Profile.ActiveIPLen)
  else Result := '';
end;

procedure SetProfileActiveIP(Index: Integer; const Value: string);
var Len: Integer;
begin
  if (Index >= 0) and (Index < ProfileCount) then
  begin
    Len := Length(Value);
    if Len > 23 then Len := 23;
    FillChar(Profiles[Index].Profile.ActiveIP, 24, 0);
    if Len > 0 then Move(Value[1], Profiles[Index].Profile.ActiveIP, Len);
    Profiles[Index].Profile.ActiveIPLen := Len;
  end;
end;

function ContainsText(const AText, ASubText: string): Boolean;
begin
  Result := Pos(LowerCase(ASubText), LowerCase(AText)) > 0;
end;

procedure QuickSortResults(var Arr: array of TSearchResultItem; Left, Right, SortMethod: Integer);
var I, J: Integer; Pivot, Temp: TSearchResultItem;
  function Compare(const A, B: TSearchResultItem): Integer;
  begin
    case SortMethod of
      SORT_ID_UP: Result := A.SubIndex - B.SubIndex;
      SORT_ID_DOWN: Result := B.SubIndex - A.SubIndex;
      SORT_COUNT_UP: Result := Sign(A.LoginCount - B.LoginCount);
      SORT_COUNT_DOWN: Result := Sign(B.LoginCount - A.LoginCount);
      SORT_TIME_UP: Result := Sign(A.TimePlayed - B.TimePlayed);
      SORT_TIME_DOWN: Result := Sign(B.TimePlayed - A.TimePlayed);
      SORT_LATEST_UP: Result := Sign(A.LastUsedDate - B.LastUsedDate);
      SORT_LATEST_DOWN: Result := Sign(B.LastUsedDate - A.LastUsedDate);
      SORT_FIRST_UP: Result := Sign(A.FirstUsedDate - B.FirstUsedDate);
      SORT_FIRST_DOWN: Result := Sign(B.FirstUsedDate - A.FirstUsedDate);
    else Result := 0;
    end;
  end;
begin
  if Left >= Right then Exit;
  I := Left; J := Right; Pivot := Arr[(Left + Right) div 2];
  repeat
    while Compare(Arr[I], Pivot) < 0 do Inc(I);
    while Compare(Arr[J], Pivot) > 0 do Dec(J);
    if I <= J then begin Temp := Arr[I]; Arr[I] := Arr[J]; Arr[J] := Temp; Inc(I); Dec(J); end;
  until I > J;
  if Left < J then QuickSortResults(Arr, Left, J, SortMethod);
  if I < Right then QuickSortResults(Arr, I, Right, SortMethod);
end;

{ the closest names first (NameRank, kept in Similarity), then the one that played last }
procedure QuickSortByRank(var Arr: array of TSearchResultItem; Left, Right: Integer);
var I, J: Integer; Pivot, Temp: TSearchResultItem;
  function Before(const A, B: TSearchResultItem): Boolean;
  begin
    if A.Similarity <> B.Similarity then Result := A.Similarity < B.Similarity
    else Result := A.LastUsedDate > B.LastUsedDate;
  end;
begin
  if Left >= Right then Exit;
  I := Left; J := Right; Pivot := Arr[(Left + Right) div 2];
  repeat
    while Before(Arr[I], Pivot) do Inc(I);
    while Before(Pivot, Arr[J]) do Dec(J);
    if I <= J then begin Temp := Arr[I]; Arr[I] := Arr[J]; Arr[J] := Temp; Inc(I); Dec(J); end;
  until I > J;
  if Left < J then QuickSortByRank(Arr, Left, J);
  if I < Right then QuickSortByRank(Arr, I, Right);
end;

procedure QuickSortByNameLen(var Arr: array of TSearchResultItem; Left, Right: Integer);
var I, J: Integer; Pivot, Temp: TSearchResultItem;
begin
  if Left >= Right then Exit;
  I := Left; J := Right; Pivot := Arr[(Left + Right) div 2];
  repeat
    while Length(Arr[I].DataString) < Length(Pivot.DataString) do Inc(I);
    while Length(Arr[J].DataString) > Length(Pivot.DataString) do Dec(J);
    if I <= J then begin Temp := Arr[I]; Arr[I] := Arr[J]; Arr[J] := Temp; Inc(I); Dec(J); end;
  until I > J;
  if Left < J then QuickSortByNameLen(Arr, Left, J);
  if I < Right then QuickSortByNameLen(Arr, I, Right);
end;
