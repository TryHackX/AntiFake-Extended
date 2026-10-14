{ ---------------------------------------------------------------------------------------
  AntiFake Extended - what the Steam Web API says about an account.
  --------------------------------------------------------------------------------------- }

const
  STEAM_MAX_ANSWER = 1048576;   { an answer longer than this is no answer of the Web API }
  { after a question that got no usable answer nothing is asked for a while: 1 minute, then 2,
    4 ... at most an hour }
  STEAM_PAUSE_FIRST = 60;
  STEAM_PAUSE_MAX = 3600;

var
  { the key Steam refused (or one that is no key at all): nothing is asked with it again until
    the settings name another one. SteamCS, like the rest below. }
  SteamRefusedKey: string = '';
  SteamPauseUntil: QWord = 0;
  SteamPauseStep: Integer = 0;

{ a Steam Web API key is 32 letters and digits; anything else would change the address it goes
  in, and is refused before anything is asked }
function SteamKeyUsable(const Key: string): Boolean;
var
  i: Integer;
begin
  Result := (Length(Key) >= 1) and (Length(Key) <= 64);
  if Result then
    for i := 1 to Length(Key) do
      if not (Key[i] in ['0'..'9', 'A'..'Z', 'a'..'z']) then Exit(False);
end;

{ Asks curl for Url. Answer is what came back, Code the HTTP status (0 when there was none).
  The address goes to curl on its input, not on its command line: every user of the machine
  can read the command line of a process, and this address carries the API key. }
function RunCurl(const Url: string; var Answer: string; var Code: Integer; var Problem: string): Boolean;
var
  Proc: TProcess;
  Buf: array[0..4095] of Byte;
  n, Waited, Old, p: Integer;
  Config: string;
  TooLong: Boolean;

  procedure Take;
  begin
    while Proc.Output.NumBytesAvailable > 0 do
    begin
      n := Proc.Output.Read(Buf, SizeOf(Buf));
      if n <= 0 then Break;
      { read to the end all the same, or curl waits for room in the pipe }
      if Length(Answer) + n > STEAM_MAX_ANSWER then TooLong := True;
      if TooLong then Continue;
      Old := Length(Answer);
      SetLength(Answer, Old + n);
      Move(Buf, Answer[Old + 1], n);
    end;
  end;

begin
  Result := False;
  Answer := '';
  Code := 0;
  Problem := '';
  TooLong := False;
  Proc := TProcess.Create(nil);
  try
    try
      Proc.Executable := Settings.CurlPath;
      Proc.Parameters.Add('-s');
      Proc.Parameters.Add('--max-time');
      Proc.Parameters.Add(IntToStr(Settings.CurlTimeoutSeconds));
      { the HTTP status on a line of its own after the answer }
      Proc.Parameters.Add('-w');
      Proc.Parameters.Add('\n%{http_code}');
      Proc.Parameters.Add('-K');
      Proc.Parameters.Add('-');
      Proc.Options := [poUsePipes, poNoConsole];
      Proc.ShowWindow := swoHide;
      try
        Proc.Execute;
      except
        Problem := 'curl could not be started (SteamCurlPath = ' + Settings.CurlPath + ')';
        Exit;
      end;
      Config := 'url = "' + Url + '"' + LineEnding;
      Proc.Input.Write(Config[1], Length(Config));
      Proc.CloseInput;
      Waited := 0;
      { the end of the server does not wait for Steam: Cleanup gives the threads in the
        background only so long, and a thread still in here after that kept the library from
        being unloaded safely }
      while Proc.Running and (Waited < Settings.CurlTimeoutSeconds * 1000 + 2000) and
            not ShuttingDown do
      begin
        Take;
        Sleep(20);
        Inc(Waited, 20);
      end;
      if Proc.Running then
      begin
        Proc.Terminate(255);
        if ShuttingDown then
          Problem := 'the server is shutting down'
        else
          Problem := 'curl did not answer in ' + IntToStr(Settings.CurlTimeoutSeconds) + ' s';
        Exit;
      end;
      Take;
      if TooLong then
      begin
        Answer := '';
        Problem := 'Steam sent back more than ' + IntToStr(STEAM_MAX_ANSWER div 1024) + ' KB';
        Exit;
      end;
      p := Length(Answer);
      while (p > 0) and (Answer[p] <> #10) do Dec(p);
      if p > 0 then
      begin
        Code := StrToIntDef(Trim(Copy(Answer, p + 1, MaxInt)), 0);
        SetLength(Answer, p - 1);
      end;
      Result := (Proc.ExitStatus = 0) and (Answer <> '');
      if not Result then
        if Proc.ExitStatus <> 0 then
          Problem := 'curl ended with code ' + IntToStr(Proc.ExitStatus)
        else
          Problem := 'Steam sent nothing back';
    except
      Result := False;
      if Problem = '' then Problem := 'curl could not be run at all';
    end;
  finally
    Proc.Free;
  end;
end;

{ the caller holds ProfilesCritSec }
function ProfileOfSteam(const Key: string): Integer;
var
  P: Integer;
begin
  Result := -1;
  P := IndexFirst(SteamIndex, Key);
  if P < 0 then Exit;
  Result := Integer(PtrInt(SteamIndex.Objects[P]));
  if (Result < 0) or (Result >= ProfileCount) then Result := -1;
end;

{ the profile that plays under this Steam account, whichever way the id is written }
function ProfileOfId64(const Id64: string): Integer;
begin
  Result := ProfileOfSteam(Id64ToSteam(Id64));
  if Result < 0 then Result := ProfileOfSteam('S' + Id64);
  if Result < 0 then Result := ProfileOfSteam(Id64);
end;

constructor TSteamThread.Create(const AIds: string);
begin
  inherited Create(True);
  FIds := AIds;
  FKey := Settings.SteamKey;
  UniqueString(FKey);
end;

{ what went wrong, for /steamstatus: written by the Steam thread, read by the workers }
procedure SetSteamError(const Text: string; OnlyIfNone: Boolean);
begin
  EnterCriticalSection(SteamCS);
  try
    if not OnlyIfNone or (SteamLastError = '') then SteamLastError := Text;
  finally LeaveCriticalSection(SteamCS); end;
end;

{ Steam will not take this key, or it is no key at all: the console hears it once, and nothing
  more is asked with it. A wrong key used to be tried again with every batch, for good. }
procedure SteamRefuse(const Key, Why: string);
var
  First: Boolean;
begin
  EnterCriticalSection(SteamCS);
  try
    First := SteamRefusedKey <> Key;
    SteamRefusedKey := Key;
    SteamLastError := Why;
    if SteamQueue <> nil then SteamQueue.Clear;
  finally LeaveCriticalSection(SteamCS); end;
  if First then
    Notice(MAX_SLOTS, Settings.ColError, 'Steam: ' + Why + ' - nothing more is asked until SteamApiKey in ' +
      'settings.ini is changed.');
end;

procedure TSteamThread.Execute;
var
  Answer, Id, Problem, Key, Raw: string;
  Got, Code: Integer;
  JData, Node, Item: TJSONData;
  Arr: TJSONArray;
  Obj: TJSONObject;
  i, Idx, Created: Integer;
  Usable, Refused: Boolean;
begin
  GuardWorkerThread;
  Got := 0;
  Code := 0;
  Answer := '';
  Problem := '';
  Usable := False;
  Refused := False;
  Key := FKey;
  try
    try
      { who they are }
      if not RunCurl('https://api.steampowered.com/ISteamUser/GetPlayerSummaries/v2/?key=' +
                     Key + '&steamids=' + FIds, Answer, Code, Problem) and (Code = 0) then
        SetSteamError(Problem, False)
      else if (Code = 401) or (Code = 403) then
      begin
        { Steam answers a wrong or missing key with 403 and a web page, not with data }
        Refused := True;
        SteamRefuse(Key, 'Steam refused the API key (HTTP ' + IntToStr(Code) + ')');
      end
      else if (Code <> 200) or (Copy(TrimLeft(Answer), 1, 1) <> '{') then
        SetSteamError('Steam did not answer the question (HTTP ' + IntToStr(Code) + ')', False)
      else
      begin
        Usable := True;
        JData := GetJSON(AsUtf8(Answer));
        try
          Node := JData.FindPath('response.players');
          if (Node <> nil) and (Node.JSONType = jtArray) then
          begin
            Arr := TJSONArray(Node);
            for i := 0 to Arr.Count - 1 do
            begin
              Item := Arr.Items[i];
              if Item.JSONType <> jtObject then Continue;
              Obj := TJSONObject(Item);
              Id := Obj.Get('steamid', '');
              if Id = '' then Continue;
              LockProfiles;
              try
                Idx := ProfileOfId64(Id);
                if Idx >= 0 then
                begin
                  SharedTouch(Idx);
                  SetGeoString(Profiles[Idx].Steam.SteamId, Id);
                  { the name as the raw answer has it: fpjson on Linux without a locale made "?" of
                    every letter outside ASCII }
                  Raw := JsonObjectWith(Answer, 'steamid', Id);
                  SetGeoString(Profiles[Idx].Steam.Persona, JsonField(Raw, 'personaname', Utf8Bytes(Obj.Get('personaname', ''))));
                  SetGeoString(Profiles[Idx].Steam.Country, JsonField(Raw, 'loccountrycode', ''));
                  Created := Obj.Get('timecreated', 0);
                  if Created > 0 then Profiles[Idx].Steam.Created := UnixToDateTime(Created);
                  if Obj.Get('communityvisibilitystate', 1) = 3 then
                    Profiles[Idx].Steam.Visible := 1
                  else
                    Profiles[Idx].Steam.Visible := 0;
                  Profiles[Idx].Steam.Fetched := Now;
                  MarkDirty(Idx);
                  Inc(SteamAnswered);
                  Inc(Got);
                end;
              finally UnlockProfiles; end;
            end;
          end
          else
            SetSteamError('Steam said no players - is the API key right?', False);
        finally
          JData.Free;
        end;
        if Got = 0 then
          SetSteamError('Steam knows none of the accounts we asked about', True);
      end;

      { the second question only when the first one got an answer }
      if ShuttingDown or not Usable then Exit;

      { and what they have been banned for }
      if RunCurl('https://api.steampowered.com/ISteamUser/GetPlayerBans/v1/?key=' +
                 Key + '&steamids=' + FIds, Answer, Code, Problem) and (Code = 200) and
         (Copy(TrimLeft(Answer), 1, 1) = '{') then
      begin
        JData := GetJSON(AsUtf8(Answer));
        try
          Node := JData.FindPath('players');
          if (Node <> nil) and (Node.JSONType = jtArray) then
          begin
            Arr := TJSONArray(Node);
            for i := 0 to Arr.Count - 1 do
            begin
              Item := Arr.Items[i];
              if Item.JSONType <> jtObject then Continue;
              Obj := TJSONObject(Item);
              Id := Obj.Get('SteamId', '');
              if Id = '' then Id := Obj.Get('steamid', '');
              if Id = '' then Continue;
              LockProfiles;
              try
                Idx := ProfileOfId64(Id);
                if Idx >= 0 then
                begin
                  SharedTouch(Idx);
                  Profiles[Idx].Steam.VacBans := Max(0, Min(255, Obj.Get('NumberOfVACBans', 0)));
                  Profiles[Idx].Steam.GameBans := Max(0, Min(255, Obj.Get('NumberOfGameBans', 0)));
                  Profiles[Idx].Steam.DaysSinceLastBan := Max(0, Min(65535, Obj.Get('DaysSinceLastBan', 0)));
                  if Obj.Get('CommunityBanned', False) then
                    Profiles[Idx].Steam.CommunityBan := 1
                  else
                    Profiles[Idx].Steam.CommunityBan := 0;
                  if Obj.Get('EconomyBan', 'none') <> 'none' then
                    Profiles[Idx].Steam.TradeBan := 1
                  else
                    Profiles[Idx].Steam.TradeBan := 0;
                  if Profiles[Idx].Steam.Fetched = 0 then Profiles[Idx].Steam.Fetched := Now;
                  if string(Profiles[Idx].Steam.SteamId) = '' then
                    SetGeoString(Profiles[Idx].Steam.SteamId, Id);
                  MarkDirty(Idx);
                end;
              finally UnlockProfiles; end;
            end;
          end;
        finally
          JData.Free;
        end;
      end;
    except
      on E: Exception do SetSteamError('the answer could not be read (' + E.Message + ')', False);
    end;
  finally
    EnterCriticalSection(SteamCS);
    try
      SteamBusy := False;
      if Got > 0 then SteamLastError := '' else Inc(SteamFailed);
      { no answer at all (no curl, no network, Steam down or asked too often): the next batch
        waits 1, 2, 4 ... minutes instead of following at once }
      if Usable then
        SteamPauseStep := 0
      else if not Refused then
      begin
        SteamPauseUntil := GetTickCount64 +
          QWord(Min(STEAM_PAUSE_MAX, STEAM_PAUSE_FIRST shl Min(SteamPauseStep, 6))) * 1000;
        Inc(SteamPauseStep);
      end;
    finally LeaveCriticalSection(SteamCS); end;
  end;
end;

{ everything in one profile as bytes: the record, the four lists, the visits and Steam }
function PackProfile(Index: Integer): AnsiString;
var
  Buf: TMemoryStream;
  n, k: Integer;

  procedure PutList(const Arr: TBinaryOtherDataArray);
  var
    q: Integer;
  begin
    q := Length(Arr);
    Buf.Write(q, SizeOf(q));
    for q := 0 to Length(Arr) - 1 do Buf.Write(Arr[q], SizeOf(TBinaryOtherData));
  end;

begin
  Result := '';
  if (Index < 0) or (Index >= ProfileCount) then Exit;
  Buf := TMemoryStream.Create;
  try
    n := Index;
    Buf.Write(n, SizeOf(n));
    Profiles[Index].Profile.OtherNamesCount := Length(Profiles[Index].OtherNames);
    Profiles[Index].Profile.OtherSteamsCount := Length(Profiles[Index].OtherSteams);
    Profiles[Index].Profile.OtherHWIDsCount := Length(Profiles[Index].OtherHWIDs);
    Profiles[Index].Profile.OtherIPsCount := Length(Profiles[Index].OtherIPs);
    Buf.Write(Profiles[Index].Profile, SizeOf(TBinaryProfile));
    PutList(Profiles[Index].OtherNames);
    PutList(Profiles[Index].OtherSteams);
    PutList(Profiles[Index].OtherHWIDs);
    PutList(Profiles[Index].OtherIPs);
    n := Length(Profiles[Index].Visits);
    Buf.Write(n, SizeOf(n));
    for k := 0 to n - 1 do Buf.Write(Profiles[Index].Visits[k], SizeOf(TVisitEntry));
    Buf.Write(Profiles[Index].Steam, SizeOf(TSteamInfo));
    SetLength(Result, Buf.Size);
    if Buf.Size > 0 then Move(Buf.Memory^, Result[1], Buf.Size);
  finally
    Buf.Free;
  end;
end;

{ and back again; False when the bytes do not hold a whole profile }
function UnpackProfile(const Data: AnsiString; var Index: Integer; var P: TProfileData): Boolean;
var
  Pos: Integer;

  function Take(var Dest; Size: Integer): Boolean;
  begin
    Result := (Size >= 0) and (Pos + Size <= Length(Data) + 1);
    if not Result then Exit;
    if Size > 0 then Move(Data[Pos], Dest, Size);
    Inc(Pos, Size);
  end;

  function TakeList(var Arr: TBinaryOtherDataArray): Boolean;
  var
    n, q: Integer;
  begin
    Result := False;
    if not Take(n, SizeOf(n)) then Exit;
    if (n < 0) or (n > 100000) then Exit;
    SetLength(Arr, n);
    for q := 0 to n - 1 do
      if not Take(Arr[q], SizeOf(TBinaryOtherData)) then Exit;
    Result := True;
  end;

var
  n, q: Integer;
begin
  Result := False;
  Pos := 1;
  if not Take(Index, SizeOf(Index)) then Exit;
  if (Index < 0) or (Index > MAX_PROFILES_HARD) then Exit;
  if not Take(P.Profile, SizeOf(TBinaryProfile)) then Exit;
  if not TakeList(P.OtherNames) then Exit;
  if not TakeList(P.OtherSteams) then Exit;
  if not TakeList(P.OtherHWIDs) then Exit;
  if not TakeList(P.OtherIPs) then Exit;
  if not Take(n, SizeOf(n)) then Exit;
  { more visits than an account keeps (written with VisitKeepCount = 0 by 2.0) are read, and
    the newest are kept: refusing the record lost every record after it in the journal }
  if (n < 0) or (n > MAX_VISITS_READ) or
     (Int64(n) * SizeOf(TVisitEntry) > Int64(Length(Data)) - Pos + 1) then Exit;
  SetLength(P.Visits, n);
  for q := 0 to n - 1 do
    if not Take(P.Visits[q], SizeOf(TVisitEntry)) then Exit;
  KeepNewestVisits(P.Visits, MAX_VISITS_HARD);
  if not Take(P.Steam, SizeOf(TSteamInfo)) then Exit;
  { from the journal or from another server: a date that is no date is "never" }
  SaneProfile(P);
  Result := True;
end;

procedure SetProfileName(var P: TProfileData; const Value: string);
var
  Len: Integer;
begin
  FillChar(P.Profile.ActiveName, SizeOf(P.Profile.ActiveName), 0);
  Len := Length(Value);
  if Len >= SizeOf(P.Profile.ActiveName) then Len := SizeOf(P.Profile.ActiveName) - 1;
  if Len > 0 then Move(Value[1], P.Profile.ActiveName, Len);
  P.Profile.ActiveNameLen := Len;
end;

procedure SetProfileSteam(var P: TProfileData; const Value: string);
var
  Len: Integer;
begin
  FillChar(P.Profile.ActiveSteam, SizeOf(P.Profile.ActiveSteam), 0);
  Len := Length(Value);
  if Len >= SizeOf(P.Profile.ActiveSteam) then Len := SizeOf(P.Profile.ActiveSteam) - 1;
  if Len > 0 then Move(Value[1], P.Profile.ActiveSteam, Len);
  P.Profile.ActiveSteamLen := Len;
end;

{ one entry of a profile, as the database gives it back }
procedure AddOtherFromDb(var P: TProfileData; Kind: Integer; const Value: string;
  Logins, TimePlayed, TimeSpec: Int64; First, Last: Double; Current, Active: Integer);
var
  Arr: TBinaryOtherDataArray;
  n: Integer;
begin
  if Value = '' then Exit;
  case Kind of
    KIND_NAME: Arr := P.OtherNames;
    KIND_STEAM: Arr := P.OtherSteams;
    KIND_HWID: Arr := P.OtherHWIDs;
    KIND_IP: Arr := P.OtherIPs;
  else Exit;
  end;
  n := Length(Arr);
  SetLength(Arr, n + 1);
  FillChar(Arr[n], SizeOf(TBinaryOtherData), 0);
  SetOtherDataString(Arr[n], Value);
  Arr[n].LoginCount := Logins;
  Arr[n].TimePlayed := TimePlayed;
  Arr[n].TimeOnSpectator := TimeSpec;
  Arr[n].FirstUsedDate := First;
  Arr[n].LastUsedDate := Last;
  Arr[n].LastUsed := Current;
  Arr[n].IsActive := Active;
  case Kind of
    KIND_NAME: begin P.OtherNames := Arr; P.Profile.OtherNamesCount := n + 1; end;
    KIND_STEAM: begin P.OtherSteams := Arr; P.Profile.OtherSteamsCount := n + 1; end;
    KIND_HWID: begin P.OtherHWIDs := Arr; P.Profile.OtherHWIDsCount := n + 1; end;
    KIND_IP: begin P.OtherIPs := Arr; P.Profile.OtherIPsCount := n + 1; end;
  end;
end;

{ one visit, as the database gives it back }
procedure AddVisitFromDb(var P: TProfileData; Started: Double; Seconds: Integer;
  const Name, Address, Computer, Country: string; Confidence, Flags: Integer);
var
  n: Integer;
begin
  n := Length(P.Visits);
  if n >= MAX_VISITS_HARD then Exit;
  SetLength(P.Visits, n + 1);
  FillChar(P.Visits[n], SizeOf(TVisitEntry), 0);
  P.Visits[n].Started := Started;
  P.Visits[n].Seconds := Seconds;
  SetGeoString(P.Visits[n].Name, Name);
  SetGeoString(P.Visits[n].IP, Address);
  SetGeoString(P.Visits[n].Hwid, Copy(Computer, 1, 15));
  SetGeoString(P.Visits[n].CountryCode, Country);
  P.Visits[n].Confidence := Confidence;
  P.Visits[n].Flags := Flags;
end;

{ the caller holds ProfilesCritSec }
procedure RebuildVerdictIndex;
var
  i: Integer;
begin
  if VerdictIndex = nil then Exit;
  VerdictIndex.Sorted := False;
  VerdictIndex.Clear;
  for i := 0 to VerdictCount - 1 do
    VerdictIndex.AddObject(PairKey(Verdicts[i].A, Verdicts[i].B), TObject(PtrInt(i)));
  VerdictIndex.Sorted := True;
end;

{ An admin looked at two accounts and said what they are. VERDICT_NONE takes the decision
  back. The caller holds ProfilesCritSec. }
function StoreVerdict(A, B, Verdict: Integer; const By: string): Boolean;
var
  Key: string;
  q, k, Lo, Hi: Integer;
begin
  Result := False;
  if (A < 0) or (B < 0) or (A = B) or (A >= ProfileCount) or (B >= ProfileCount) then Exit;
  if (Verdict < VERDICT_NONE) or (Verdict > VERDICT_IGNORE) then Exit;
  if A <= B then begin Lo := A; Hi := B; end else begin Lo := B; Hi := A; end;
  Key := PairKey(Lo, Hi);
  q := IndexFirst(VerdictIndex, Key);
  if q >= 0 then
  begin
    k := Integer(PtrInt(VerdictIndex.Objects[q]));
    if (k < 0) or (k >= VerdictCount) then Exit;
    if Verdict = VERDICT_NONE then
    begin
      { the last one takes the place of the one that goes, so nothing has to be moved }
      Verdicts[k] := Verdicts[VerdictCount - 1];
      Dec(VerdictCount);
      RebuildVerdictIndex;
      VerdictsDirty := True;
      SharedNoteVerdict(Lo, Hi, VERDICT_NONE, Now, By);
      Result := True;
      Exit;
    end;
    Verdicts[k].Verdict := Verdict;
    Verdicts[k].Decided := Now;
    SetGeoString(Verdicts[k].By, By);
    VerdictsDirty := True;
    SharedNoteVerdict(Lo, Hi, Verdict, Verdicts[k].Decided, By);
    Result := True;
    Exit;
  end;
  { nothing was said about the pair here: taking it back is done already ("could not be written
    down" was the answer), and with Engine = mysql it goes out all the same, for a decision this
    server has not received yet }
  if Verdict = VERDICT_NONE then
  begin
    SharedNoteVerdict(Lo, Hi, VERDICT_NONE, Now, By);
    Result := True;
    Exit;
  end;
  if VerdictCount >= MAX_VERDICTS then Exit;
  if VerdictCount >= Length(Verdicts) then SetLength(Verdicts, VerdictCount + 100);
  FillChar(Verdicts[VerdictCount], SizeOf(TPairVerdict), 0);
  Verdicts[VerdictCount].A := Lo;
  Verdicts[VerdictCount].B := Hi;
  Verdicts[VerdictCount].Verdict := Verdict;
  Verdicts[VerdictCount].Decided := Now;
  SetGeoString(Verdicts[VerdictCount].By, By);
  IndexAdd(VerdictIndex, Key, VerdictCount);
  SharedNoteVerdict(Lo, Hi, Verdict, Verdicts[VerdictCount].Decided, By);
  Inc(VerdictCount);
  VerdictsDirty := True;
  Result := True;
end;

{ How much two accounts look like one person, 0-100, and why in plain words.

  A shared computer is the strongest thing we have: two accounts on one machine is one person
  or one household, and either way the admin wants to know. It alone is worth more than the
  threshold, so it is always said. A shared address is much weaker - a house, a dormitory, a
  cafe, a whole block behind one address - and alone it is not worth a word; together with a
  name that is nearly the same it is. A network is weaker still.
  Signals counts how many of them agree, for admins who want more than one before hearing.
  The caller holds ProfilesCritSec. }
{ the score of a link from what ties the two accounts and their names (Named: both names are
  known); the caller may hold no lock }
function LinkScoreOf(const NameA, NameB: string; Named: Boolean; Bits: Integer;
  var Reasons: string; var Signals: Integer): Integer;
var
  Sim: Integer;

  procedure Add(Points: Integer; const Why: string);
  begin
    Inc(Result, Points);
    Inc(Signals);
    if Reasons <> '' then Reasons := Reasons + ', ';
    Reasons := Reasons + Why;
  end;

begin
  Result := 0;
  Signals := 0;
  Reasons := '';
  if (Bits and LINK_COMPUTER) <> 0 then Add(60, 'same computer');
  if (Bits and LINK_STEAM) <> 0 then Add(60, 'same Steam account');
  if (Bits and LINK_ADDRESS) <> 0 then Add(25, 'same address');
  if ((Bits and LINK_NETWORK) <> 0) and ((Bits and LINK_ADDRESS) = 0) then
    Add(10, 'same network');
  if Named then
  begin
    Sim := CalculateSimilarity(NameA, NameB);
    if Sim >= 90 then Add(30, 'almost the same name')
    else if Sim >= 75 then Add(15, 'a similar name')
    else if Sim >= 60 then Add(6, 'a name a little like it');
  end;
  if Result > 100 then Result := 100;
end;

{ the same for two accounts; the caller holds ProfilesCritSec }
function LinkScore(A, B, Bits: Integer; var Reasons: string; var Signals: Integer): Integer;
begin
  if (A >= 0) and (A < ProfileCount) and (B >= 0) and (B < ProfileCount) then
    Result := LinkScoreOf(GetProfileActiveName(A), GetProfileActiveName(B), True, Bits, Reasons, Signals)
  else
    Result := LinkScoreOf('', '', False, Bits, Reasons, Signals);
end;

{ "3|17:3:80:2:0:Kowal|22:2:25:1:2:Rambo" - the account is tied to three others; of the first
  ones written out, #17 shares a computer and an address, scores 80 on two signals and nobody
  has judged the pair yet, and #22 shares an address, scores 25 on one signal and an admin
  marked it as one family. Empty when the account plays alone, which is the usual answer. }
{ a text a player chose (a nick, a Steam name) as one field of a line split at Seps: each of
  them becomes '/', so the fields after it stay where they are }
function FieldText(const S, Seps: string): string;
var
  i: Integer;
begin
  Result := S;
  for i := 1 to Length(Result) do
    if Pos(Result[i], Seps) > 0 then Result[i] := '/';
end;

{ Get_Multi_Links and Get_Steam_Info are not used by the script any more (the views draw from
  the profiles themselves); they are kept for the test programs and for tools. }
function Get_Multi_Links(ProfileIdx, MaxNames: Integer): PChar; cdecl;
var
  Links: TStringList;
  R, Reasons: string;
  i, Why, Other, Score, Signals: Integer;
begin
  R := '';
  try
    if MaxNames < 1 then MaxNames := 8;
    Links := TStringList.Create;
    try
      if GrabProfiles(20) then
      try
        CollectLinks(ProfileIdx, Links);
        if Links.Count >= Settings.MultiMinLinks then
        begin
          R := IntToStr(Links.Count);
          for i := 0 to Links.Count - 1 do
          begin
            if i >= MaxNames then Break;
            Other := StrToIntDef(Links[i], -1);
            Why := Integer(PtrInt(Links.Objects[i]));
            if (Other < 0) or (Other >= ProfileCount) then Continue;
            Score := LinkScore(ProfileIdx, Other, Why, Reasons, Signals);
            R := R + '|' + Links[i] + ':' + IntToStr(Why) + ':' + IntToStr(Score) + ':' +
                 IntToStr(Signals) + ':' + IntToStr(VerdictOf(ProfileIdx, Other)) + ':' +
                 FieldText(GetProfileActiveName(Other), '|:');
          end;
        end;
      finally UnlockProfiles; end;
    finally
      Links.Free;
    end;
  except
    ExportFailed('Get_Multi_Links');
    R := '';
  end;
  TempStringResult := R;
  Result := PChar(TempStringResult);
end;

{ the script says who joined; the account is asked about only if what we have is stale }
procedure Steam_Note(SteamId: PChar); cdecl;
var
  Id64: string;
  Idx: Integer;
  Fresh: Boolean;
begin
  try
    if (Settings.SteamKey = '') or ShuttingDown then Exit;
    Id64 := SteamToId64(string(SteamId));
    if Id64 = '' then Exit;
    Fresh := False;
    if GrabProfiles(20) then
    try
      Idx := ProfileOfId64(Id64);
      if Idx >= 0 then
        Fresh := (Profiles[Idx].Steam.Fetched > 0) and
                 (Now - Profiles[Idx].Steam.Fetched < Settings.SteamRefreshDays);
    finally UnlockProfiles; end;
    if Fresh then Exit;
    if not GrabLock(SteamCS, 20) then Exit;
    try
      if SteamQueue = nil then Exit;
      { a key Steam refused asks nothing, so nothing waits for it }
      if SteamRefusedKey = Settings.SteamKey then Exit;
      if SteamQueue.IndexOf(Id64) >= 0 then Exit;
      if SteamQueue.Count >= 500 then Exit;
      SteamQueue.Add(Id64);
    finally LeaveCriticalSection(SteamCS); end;
  except
    ExportFailed('Steam_Note');
  end;
end;

{ called once a second by the script: one batch at a time, and never on the game thread }
procedure Steam_Tick(); cdecl;
var
  Ids: string;
  k: Integer;
begin
  try
    if (Settings.SteamKey = '') or ShuttingDown then Exit;
    if not GrabLock(SteamCS, 5) then Exit;
    try
      { a key that is no key goes into no address: said once, and nothing is asked }
      if (SteamRefusedKey <> Settings.SteamKey) and not SteamKeyUsable(Settings.SteamKey) then
      begin
        SteamRefusedKey := Settings.SteamKey;
        SteamLastError := 'SteamApiKey in settings.ini is not a Steam Web API key (only letters and digits)';
        if SteamQueue <> nil then SteamQueue.Clear;
        Notice(MAX_SLOTS, Settings.ColError, 'Steam: ' + SteamLastError + ' - nothing is asked until it is changed.');
        Exit;
      end;
      if SteamRefusedKey = Settings.SteamKey then Exit;
      if GetTickCount64 < SteamPauseUntil then Exit;
      if SteamBusy or (SteamQueue = nil) or (SteamQueue.Count = 0) then Exit;
      Ids := '';
      k := 0;
      while (SteamQueue.Count > 0) and (k < Settings.SteamBatch) do
      begin
        if Ids <> '' then Ids := Ids + ',';
        Ids := Ids + SteamQueue[0];
        SteamQueue.Delete(0);
        Inc(k);
      end;
      if Ids = '' then Exit;
      SteamBusy := True;
      Inc(SteamAsked, k);
      try
        StartBackground(TSteamThread.Create(Ids));
      except
        SteamBusy := False;
      end;
    finally LeaveCriticalSection(SteamCS); end;
  except
    ExportFailed('Steam_Tick');
  end;
end;

{ what Steam knows about one profile, as one line for the script to split }
function Get_Steam_Info(Index: Integer): PChar; cdecl;
var
  R, Created, Age: string;
begin
  R := '';
  try
    if GrabProfiles(20) then
    try
      if (Index >= 0) and (Index < ProfileCount) and (Profiles[Index].Steam.Fetched > 0) then
      begin
        if Profiles[Index].Steam.Created > 0 then
        begin
          Created := FormatDateTime('yyyy-mm-dd', Profiles[Index].Steam.Created);
          Age := IntToStr(Trunc(Now - Profiles[Index].Steam.Created));
        end
        else
        begin
          Created := '?';
          Age := '-1';
        end;
        { the dates are written out here: the script has no float to read them with }
        R := string(Profiles[Index].Steam.SteamId) + '|' +
             FieldText(Utf8ToConsole(string(Profiles[Index].Steam.Persona)), '|') + '|' +
             string(Profiles[Index].Steam.Country) + '|' +
             Created + '|' + Age + '|' +
             IntToStr(Profiles[Index].Steam.Visible) + '|' +
             IntToStr(Profiles[Index].Steam.VacBans) + '|' +
             IntToStr(Profiles[Index].Steam.GameBans) + '|' +
             IntToStr(Profiles[Index].Steam.CommunityBan) + '|' +
             IntToStr(Profiles[Index].Steam.TradeBan) + '|' +
             IntToStr(Profiles[Index].Steam.DaysSinceLastBan) + '|' +
             FormatDateTime('yyyy-mm-dd hh:nn', Profiles[Index].Steam.Fetched);
      end;
    finally UnlockProfiles; end;
  except
    ExportFailed('Get_Steam_Info');
    R := '';
  end;
  TempStringResult := R;
  Result := PChar(TempStringResult);
end;

{ an admin says what a pair is; 0 takes the decision back }
function Set_Multi_Verdict(A, B, Verdict: Integer; By: PChar): Boolean; cdecl;
begin
  Result := False;
  if not GrabProfiles(50) then Exit;
  try
    Result := StoreVerdict(A, B, Verdict, string(By));
  finally UnlockProfiles; end;
end;

{ "2|Kowal|2026-09-21 12:30" - what was decided, by whom and when; empty when nobody has }
function Get_Multi_Verdict(A, B: Integer): PChar; cdecl;
var
  R, Key: string;
  q, k: Integer;
begin
  R := '';
  if GrabProfiles(20) then
  try
    Key := PairKey(A, B);
    q := IndexFirst(VerdictIndex, Key);
    if q >= 0 then
    begin
      k := Integer(PtrInt(VerdictIndex.Objects[q]));
      if (k >= 0) and (k < VerdictCount) then
        R := IntToStr(Verdicts[k].Verdict) + '|' + string(Verdicts[k].By) + '|' +
             FormatDateTime('yyyy-mm-dd hh:nn', Verdicts[k].Decided);
    end;
  finally UnlockProfiles; end;
  TempStringResult := R;
  Result := PChar(TempStringResult);
end;

{ how many pairs an admin has judged, and how they fell }
function Get_Multi_Stats(): PChar; cdecl;
var
  i, Same, Family, Ignored: Integer;
begin
  Same := 0; Family := 0; Ignored := 0;
  if GrabProfiles(50) then
  try
    for i := 0 to VerdictCount - 1 do
      case Verdicts[i].Verdict of
        VERDICT_SAME: Inc(Same);
        VERDICT_FAMILY: Inc(Family);
        VERDICT_IGNORE: Inc(Ignored);
      end;
  finally UnlockProfiles; end;
  TempStringResult := Format('%d judged: %d one person, %d one family, %d ignored',
    [Same + Family + Ignored, Same, Family, Ignored]);
  Result := PChar(TempStringResult);
end;

function Get_Steam_Status(): PChar; cdecl;
var
  Waiting: Integer;
  Err: string;
  Refused: Boolean;
  Pause: QWord;
begin
  if Settings.SteamKey = '' then
    TempStringResult := 'off (no API key in settings.ini)'
  else
  begin
    Waiting := 0;
    Err := '';
    Refused := False;
    Pause := 0;
    if GrabLock(SteamCS, 20) then
    try
      if SteamQueue <> nil then Waiting := SteamQueue.Count;
      Err := SteamLastError;
      Refused := SteamRefusedKey = Settings.SteamKey;
      if SteamPauseUntil > GetTickCount64 then Pause := (SteamPauseUntil - GetTickCount64) div 1000 + 1;
    finally LeaveCriticalSection(SteamCS); end;
    TempStringResult := Format('on, asked %d, answered %d, failed %d, waiting %d, refresh every %d d',
      [SteamAsked, SteamAnswered, SteamFailed, Waiting, Settings.SteamRefreshDays]);
    if Refused then
      TempStringResult := TempStringResult + ' | stopped: nothing is asked until SteamApiKey is changed'
    else if Pause > 0 then
      TempStringResult := TempStringResult + Format(' | the next question in %d s', [Pause]);
    if Err <> '' then
      TempStringResult := TempStringResult + ' | last problem: ' + Err;
  end;
  Result := PChar(TempStringResult);
end;
