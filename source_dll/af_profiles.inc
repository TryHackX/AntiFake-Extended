{ ---------------------------------------------------------------------------------------
  AntiFake Extended - the accounts: what is in one, how one is found and what a login writes.
  --------------------------------------------------------------------------------------- }

{ An entry is one the account still uses when it is the one it played with last, or when it
  was used inside the last ActiveDays days. The stored IsActive said 1 for everything ever
  written, so "active only" showed the whole history; this is the question it was meant to ask.
  ActiveDays = 0 brings the old behaviour back: everything counts. }
function EntryIsActive(const E: TBinaryOtherData; NowTime: Double): Boolean;
begin
  Result := (E.LastUsed = 1) or (Settings.ActiveDays <= 0) or
            (NowTime - E.LastUsedDate <= Settings.ActiveDays);
end;

{ the oldest entry of a kind is the one the account started with; every new entry used to be
  marked as the first one, so the filter passed everything. The caller holds ProfilesCritSec. }
procedure MarkFirstUsed(var Arr: TBinaryOtherDataArray);
var
  i, Best: Integer;
begin
  Best := -1;
  for i := 0 to Length(Arr) - 1 do
  begin
    Arr[i].FirstUsed := 0;
    if (Best < 0) or (Arr[i].FirstUsedDate < Arr[Best].FirstUsedDate) then Best := i;
  end;
  if Best >= 0 then Arr[Best].FirstUsed := 1;
end;

procedure MarkProfileFirstUsed(var P: TProfileData);
begin
  MarkFirstUsed(P.OtherNames);
  MarkFirstUsed(P.OtherSteams);
  MarkFirstUsed(P.OtherHWIDs);
  MarkFirstUsed(P.OtherIPs);
end;

{ what two accounts have in common }
const
  LINK_COMPUTER = 1;
  LINK_ADDRESS = 2;
  LINK_STEAM = 4;
  LINK_NETWORK = 8;

{ the key a decision is filed under: the smaller number first, so one pair has one place }
function PairKey(A, B: Integer): string;
begin
  if A <= B then Result := IntToStr(A) + ':' + IntToStr(B)
  else Result := IntToStr(B) + ':' + IntToStr(A);
end;

{ the caller holds ProfilesCritSec }
function VerdictOf(A, B: Integer): Integer;
var
  q, k: Integer;
begin
  Result := VERDICT_NONE;
  if VerdictIndex = nil then Exit;
  q := IndexFirst(VerdictIndex, PairKey(A, B));
  if q < 0 then Exit;
  k := Integer(PtrInt(VerdictIndex.Objects[q]));
  if (k >= 0) and (k < VerdictCount) then Result := Verdicts[k].Verdict;
end;

{ Every profile that shares a computer, a Steam account, an address or a /24 network with this
  one, as a list of profile numbers and what they have in common (the LINK_ bits above added
  together).

  Two limits keep this cheap and keep the answer worth reading. An address or a network that
  more than MultiMaxShared accounts have played from is a public one - a school, a cafe, a
  whole block behind one address - and says nothing about anybody, so it is skipped; a
  computer or a Steam account only when more than MultiMaxSharedPC accounts share it, which is
  there to bound the work. And no more than MultiMaxLinks accounts are collected, because a
  longer list is not read anyway. The caller holds ProfilesCritSec. }
procedure CollectLinks(ProfileIdx: Integer; Links: TStringList);
var
  j, k: Integer;
  Key, Net: string;
  Run, Nets: TStringList;

  { the accounts behind one key, or nothing at all when too many share it }
  procedure Walk(Idx: TStringList; const K: string; Bit, Cap: Integer);
  var
    q, Other, Had, Seen: Integer;
    Slot: string;
  begin
    q := IndexFirst(Idx, K);
    if q < 0 then Exit;
    Run.Clear;
    Seen := 0;
    while (q >= 0) and (q < Idx.Count) and (Idx[q] = K) do
    begin
      Inc(Seen);
      { Seen counts this account too: "more accounts than Cap" is Seen > Cap (it was Cap + 1,
        and one account more than the setting said was still taken) }
      if Seen > Cap then Exit;   { too many to mean anything, or too many to walk }
      Other := Integer(PtrInt(Idx.Objects[q]));
      if (Other >= 0) and (Other < ProfileCount) and (Other <> ProfileIdx) then
        Run.Add(IntToStr(Other));
      Inc(q);
    end;
    for q := 0 to Run.Count - 1 do
    begin
      Slot := Run[q];
      Had := Links.IndexOf(Slot);
      if Had >= 0 then
        Links.Objects[Had] := TObject(PtrInt(Integer(PtrInt(Links.Objects[Had])) or Bit))
      else
      begin
        if Links.Count >= Settings.MultiMaxLinks then Exit;
        Links.AddObject(Slot, TObject(PtrInt(Bit)));
      end;
    end;
  end;

begin
  Links.Clear;
  if (ProfileIdx < 0) or (ProfileIdx >= ProfileCount) then Exit;
  Run := TStringList.Create;
  Nets := TStringList.Create;
  try
    for j := 0 to Length(Profiles[ProfileIdx].OtherHWIDs) - 1 do
    begin
      Key := GetOtherDataString(Profiles[ProfileIdx].OtherHWIDs[j]);
      if Key <> '' then Walk(HwidIndex, Key, LINK_COMPUTER, Settings.MultiMaxSharedPC);
    end;
    { one Steam account on two profiles can only happen after a correction, but then it is
      the same person beyond doubt }
    for j := 0 to Length(Profiles[ProfileIdx].OtherSteams) - 1 do
    begin
      Key := GetOtherDataString(Profiles[ProfileIdx].OtherSteams[j]);
      if Key <> '' then Walk(SteamIndex, Key, LINK_STEAM, Settings.MultiMaxSharedPC);
    end;
    if not Settings.MultiByAddress then Exit;
    for k := 0 to Length(Profiles[ProfileIdx].OtherIPs) - 1 do
    begin
      Key := GetOtherDataString(Profiles[ProfileIdx].OtherIPs[k]);
      if Key = '' then Continue;
      Walk(IpIndex, Key, LINK_ADDRESS, Settings.MultiMaxShared);
      { fifty addresses of one network walked that network fifty times }
      Net := Net24Of(Key);
      if Nets.IndexOf(Net) >= 0 then Continue;
      Nets.Add(Net);
      Walk(Net24Index, Net, LINK_NETWORK, Settings.MultiMaxShared);
    end;
    { the same address already says everything the network would }
    for j := 0 to Links.Count - 1 do
    begin
      k := Integer(PtrInt(Links.Objects[j]));
      if (k and LINK_ADDRESS) <> 0 then
        Links.Objects[j] := TObject(PtrInt(k and not LINK_NETWORK));
    end;
  finally
    Nets.Free;
    Run.Free;
  end;
end;

const
  { One list of one account never grows past this. Its length is a 16-bit number in the file,
    and a list that ran past 65535 (a player changing names in a loop for weeks, with
    NameDays = 0) made every account behind it unreadable. A full list gives up the entry
    used least recently instead. }
  MAX_OTHER_ENTRIES = 10000;

{ the entry of a full list that was used least recently goes, with its places in the indexes;
  the caller holds ProfilesCritSec }
procedure DropLeastUsed(var Arr: TBinaryOtherDataArray; Idx, Net: TStringList; ProfileIdx: Integer);
var
  k, Oldest: Integer;
  Key: string;
begin
  if Length(Arr) = 0 then Exit;
  Oldest := 0;
  for k := 1 to High(Arr) do
    if Arr[k].LastUsedDate < Arr[Oldest].LastUsedDate then Oldest := k;
  Key := GetOtherDataString(Arr[Oldest]);
  if Idx <> nil then IndexRemove(Idx, Key, ProfileIdx);
  if Net <> nil then IndexRemove(Net, Net24Of(Key), ProfileIdx);
  for k := Oldest to High(Arr) - 1 do Arr[k] := Arr[k + 1];
  SetLength(Arr, Length(Arr) - 1);
end;

function UpdateOrAddOther(ProfileIdx, Kind: Integer; const Value: string; IncLoginCount: Boolean): Integer;
var i, Len: Integer; Found: Boolean; NowTime: Double;
begin
  Result := -1; Found := False; NowTime := Now;
  MarkDirty(ProfileIdx);
  case Kind of
    KIND_NAME: Len := Length(Profiles[ProfileIdx].OtherNames);
    KIND_STEAM: Len := Length(Profiles[ProfileIdx].OtherSteams);
    KIND_HWID: Len := Length(Profiles[ProfileIdx].OtherHWIDs);
    KIND_IP: Len := Length(Profiles[ProfileIdx].OtherIPs);
  else Exit;
  end;
  for i := 0 to Len - 1 do
    case Kind of
      KIND_NAME:
        if GetOtherDataString(Profiles[ProfileIdx].OtherNames[i]) = Value then
        begin Found := True; Result := i;
          Profiles[ProfileIdx].OtherNames[i].IsActive := 1;
          Profiles[ProfileIdx].OtherNames[i].LastUsed := 1;
          if IncLoginCount then Inc(Profiles[ProfileIdx].OtherNames[i].LoginCount);
          Profiles[ProfileIdx].OtherNames[i].LastUsedDate := NowTime;
        end else Profiles[ProfileIdx].OtherNames[i].LastUsed := 0;
      KIND_STEAM:
        if GetOtherDataString(Profiles[ProfileIdx].OtherSteams[i]) = Value then
        begin Found := True; Result := i;
          Profiles[ProfileIdx].OtherSteams[i].IsActive := 1;
          Profiles[ProfileIdx].OtherSteams[i].LastUsed := 1;
          if IncLoginCount then Inc(Profiles[ProfileIdx].OtherSteams[i].LoginCount);
          Profiles[ProfileIdx].OtherSteams[i].LastUsedDate := NowTime;
        end else Profiles[ProfileIdx].OtherSteams[i].LastUsed := 0;
      KIND_HWID:
        if GetOtherDataString(Profiles[ProfileIdx].OtherHWIDs[i]) = Value then
        begin Found := True; Result := i;
          Profiles[ProfileIdx].OtherHWIDs[i].IsActive := 1;
          Profiles[ProfileIdx].OtherHWIDs[i].LastUsed := 1;
          if IncLoginCount then Inc(Profiles[ProfileIdx].OtherHWIDs[i].LoginCount);
          Profiles[ProfileIdx].OtherHWIDs[i].LastUsedDate := NowTime;
        end else Profiles[ProfileIdx].OtherHWIDs[i].LastUsed := 0;
      KIND_IP:
        if GetOtherDataString(Profiles[ProfileIdx].OtherIPs[i]) = Value then
        begin Found := True; Result := i;
          Profiles[ProfileIdx].OtherIPs[i].IsActive := 1;
          Profiles[ProfileIdx].OtherIPs[i].LastUsed := 1;
          if IncLoginCount then Inc(Profiles[ProfileIdx].OtherIPs[i].LoginCount);
          Profiles[ProfileIdx].OtherIPs[i].LastUsedDate := NowTime;
        end else Profiles[ProfileIdx].OtherIPs[i].LastUsed := 0;
    end;
  if not Found then
  begin
    if Len >= MAX_OTHER_ENTRIES then
    begin
      case Kind of
        KIND_NAME: DropLeastUsed(Profiles[ProfileIdx].OtherNames, nil, nil, ProfileIdx);
        KIND_STEAM: DropLeastUsed(Profiles[ProfileIdx].OtherSteams, SteamIndex, nil, ProfileIdx);
        KIND_HWID: DropLeastUsed(Profiles[ProfileIdx].OtherHWIDs, HwidIndex, nil, ProfileIdx);
        KIND_IP: DropLeastUsed(Profiles[ProfileIdx].OtherIPs, IpIndex, Net24Index, ProfileIdx);
      end;
      Dec(Len);
    end;
    Result := Len;
    case Kind of
      KIND_NAME: begin
        SetLength(Profiles[ProfileIdx].OtherNames, Len + 1);
        Profiles[ProfileIdx].Profile.OtherNamesCount := Len + 1;
        FillChar(Profiles[ProfileIdx].OtherNames[Len], SizeOf(TBinaryOtherData), 0);
        SetOtherDataString(Profiles[ProfileIdx].OtherNames[Len], Value);
        with Profiles[ProfileIdx].OtherNames[Len] do begin IsActive := 1; LastUsed := 1; LoginCount := 1; FirstUsedDate := NowTime; LastUsedDate := NowTime; end;
        MarkFirstUsed(Profiles[ProfileIdx].OtherNames);
        LoginResult_Flags := LoginResult_Flags or 1;
      end;
      KIND_STEAM: begin
        SetLength(Profiles[ProfileIdx].OtherSteams, Len + 1);
        Profiles[ProfileIdx].Profile.OtherSteamsCount := Len + 1;
        IndexAdd(SteamIndex, Value, ProfileIdx);
        FillChar(Profiles[ProfileIdx].OtherSteams[Len], SizeOf(TBinaryOtherData), 0);
        SetOtherDataString(Profiles[ProfileIdx].OtherSteams[Len], Value);
        with Profiles[ProfileIdx].OtherSteams[Len] do begin IsActive := 1; LastUsed := 1; LoginCount := 1; FirstUsedDate := NowTime; LastUsedDate := NowTime; end;
        MarkFirstUsed(Profiles[ProfileIdx].OtherSteams);
        LoginResult_Flags := LoginResult_Flags or 2;
      end;
      KIND_HWID: begin
        SetLength(Profiles[ProfileIdx].OtherHWIDs, Len + 1);
        Profiles[ProfileIdx].Profile.OtherHWIDsCount := Len + 1;
        IndexAdd(HwidIndex, Value, ProfileIdx);
        FillChar(Profiles[ProfileIdx].OtherHWIDs[Len], SizeOf(TBinaryOtherData), 0);
        SetOtherDataString(Profiles[ProfileIdx].OtherHWIDs[Len], Value);
        with Profiles[ProfileIdx].OtherHWIDs[Len] do begin IsActive := 1; LastUsed := 1; LoginCount := 1; FirstUsedDate := NowTime; LastUsedDate := NowTime; end;
        MarkFirstUsed(Profiles[ProfileIdx].OtherHWIDs);
        LoginResult_Flags := LoginResult_Flags or 4;
      end;
      KIND_IP: begin
        SetLength(Profiles[ProfileIdx].OtherIPs, Len + 1);
        Profiles[ProfileIdx].Profile.OtherIPsCount := Len + 1;
        IndexAdd(IpIndex, Value, ProfileIdx);
        IndexAdd(Net24Index, Net24Of(Value), ProfileIdx);
        FillChar(Profiles[ProfileIdx].OtherIPs[Len], SizeOf(TBinaryOtherData), 0);
        SetOtherDataString(Profiles[ProfileIdx].OtherIPs[Len], Value);
        with Profiles[ProfileIdx].OtherIPs[Len] do begin IsActive := 1; LastUsed := 1; LoginCount := 1; FirstUsedDate := NowTime; LastUsedDate := NowTime; end;
        MarkFirstUsed(Profiles[ProfileIdx].OtherIPs);
        LoginResult_Flags := LoginResult_Flags or 8;
      end;
    end;
  end;
end;

procedure QuickSortBySimilarity(var Arr: array of TSearchResultItem; Left, Right: Integer);
var
  I, J: Integer;
  Pivot, Temp: TSearchResultItem;
begin
  if Left >= Right then Exit;
  I := Left;
  J := Right;
  Pivot := Arr[(Left + Right) div 2];

  repeat
    while Arr[I].Similarity > Pivot.Similarity do Inc(I);
    while Arr[J].Similarity < Pivot.Similarity do Dec(J);
    if I <= J then begin
      Temp := Arr[I];
      Arr[I] := Arr[J];
      Arr[J] := Temp;
      Inc(I);
      Dec(J);
    end;
  until I > J;

  if Left < J then QuickSortBySimilarity(Arr, Left, J);
  if I < Right then QuickSortBySimilarity(Arr, I, Right);
end;

{ A name reaches us one byte per character - the page the game console draws, or the Polish
  page a Polish client sends - and now and then as UTF-8. Comparing two nicks needs plain
  letters either way. The old version replaced UTF-8 letters only, so a single-byte name
  written with L-slash lost that letter completely and every comparison with it was too low.
  Where the two pages disagree the Polish letter wins: this is a Polish server. }
{ The fuzzy search runs this and SanitizeString over every account there is, so the text is
  written into a string made at its full length once (no letter comes out as two), instead of
  growing one letter at a time. }
function PlainLetters(const S: string): string;
var
  i, Len, CP, Extra, k, n: Integer;
  B: Byte;

  procedure Put(C: Char);
  begin
    Inc(n);
    Result[n] := C;
  end;

begin
  Result := '';
  Len := Length(S);
  SetLength(Result, Len);
  n := 0;
  i := 1;
  while i <= Len do
  begin
    B := Byte(S[i]);
    CP := B;
    Extra := 0;
    { a UTF-8 letter is read as one character, anything else stays the byte it is }
    if (B and $E0) = $C0 then Extra := 1
    else if (B and $F0) = $E0 then Extra := 2
    else if (B and $F8) = $F0 then Extra := 3;
    if (Extra > 0) and (i + Extra <= Len) then
    begin
      case Extra of
        1: CP := B and $1F;
        2: CP := B and $0F;
      else CP := B and $07;
      end;
      for k := 1 to Extra do
      begin
        if (Byte(S[i + k]) and $C0) <> $80 then begin CP := B; Extra := 0; Break; end;
        CP := (CP shl 6) or (Byte(S[i + k]) and $3F);
      end;
    end
    else
      Extra := 0;
    Inc(i, Extra + 1);
    if CP < $80 then
    begin
      Put(Chr(CP));
      Continue;
    end;
    if (CP >= $100) and (CP <= $17F) then
    begin
      Put(LATIN_A_PLAIN[CP - $100 + 1]);
      Continue;
    end;
    case CP of
      $83: Put('f');
      $8A, $9A: Put('s');                          { S caron }
      $8C, $9C: Put('s');                          { S acute on the Polish page }
      $8E, $9E, $8F, $9F: Put('z');                { Z caron, Z acute }
      $A3, $B3: Put('l');                          { L slash }
      $A5, $B9: Put('a');                          { A ogonek }
      $AF, $BF: Put('z');                          { Z dot }
      $C0..$C5, $E0..$E5: Put('a');
      $C6, $E6: Put('c');                          { C acute on the Polish page }
      $C7, $E7: Put('c');
      $C8..$CB, $E8..$EB: Put('e');
      $CC..$CF, $EC..$EF: Put('i');
      $D0, $F0: Put('d');
      $D1, $F1: Put('n');
      $D2..$D6, $D8, $F2..$F6, $F8: Put('o');
      $D9..$DC, $F9..$FC: Put('u');
      $DD, $FD, $FF: Put('y');
      $DE, $FE: Put('t');
      $DF: Put('s');
    end;
  end;
  SetLength(Result, n);
end;

function SanitizeString(S: string): string;
var
  i, n: Integer;
  C: Char;
  Len: Integer;
begin
  S := LowerCase(PlainLetters(S));
  Len := Length(S);
  { never longer than what goes in: made once at that length (see PlainLetters) }
  Result := '';
  SetLength(Result, Len);
  n := 0;
  i := 1;

  while i <= Len do begin
    C := S[i];

    if (C = '^') and (i < Len) then begin
      Inc(i, 2);
      Continue;
    end;

    case C of
      '0': C := 'o';
      '1', '!', '|': C := 'i';
      '3': C := 'e';
      '4', '@': C := 'a';
      '5', '$': C := 's';
      '7': C := 't';
      '8': C := 'b';
      '9': C := 'g';
      '(', ')', '[', ']', '{', '}', '<', '>', '_', '-', '.', '"', '''', '`', '~', ',', ';', ':', '/', '\', '+', '=', '*', '&', '%', '#':
        begin
          Inc(i);
          Continue;
        end;
    end;

    if ((C >= 'a') and (C <= 'z')) or ((C >= '0') and (C <= '9')) or (C = ' ') then
    begin
      Inc(n);
      Result[n] := C;
    end;

    Inc(i);
  end;
  SetLength(Result, n);
end;

{ Two rows instead of the whole matrix: the distance only ever needs the row above and the
  one being filled, so a pair of 20-letter nicks wants 2 x 21 numbers, not 441 - and the
  fuzzy search compares one name against every profile there is. }
function LevenshteinDistance(S1, S2: string): Integer;
var
  Len1, Len2, i, j, Cost, Prev, Temp: Integer;
  Row: array of Integer;
begin
  Len1 := Length(S1);
  Len2 := Length(S2);
  if Len1 = 0 then Exit(Len2);
  if Len2 = 0 then Exit(Len1);
  Row := nil;
  SetLength(Row, Len2 + 1);
  for j := 0 to Len2 do Row[j] := j;
  for i := 1 to Len1 do
  begin
    Prev := Row[0];          { the value above and to the left }
    Row[0] := i;
    for j := 1 to Len2 do
    begin
      Temp := Row[j];
      if S1[i] = S2[j] then Cost := 0 else Cost := 1;
      Row[j] := Min(Min(Row[j] + 1, Row[j - 1] + 1), Prev + Cost);
      Prev := Temp;
    end;
  end;
  Result := Row[Len2];
end;
