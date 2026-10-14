{ ---------------------------------------------------------------------------------------
  AntiFake Extended - the tables of a finished search, drawn into the slot that asked.
  --------------------------------------------------------------------------------------- }

{ ---------------- the tables of a finished search ---------------- }

{ the rows of one page; settings that were never read have no number there, and a division by
  it ended the drawing with an exception }
function PerPage: Integer;
begin
  Result := Settings.ResultsPerPage;
  if Result < 1 then Result := DEF_RESULTS_PER_PAGE;
end;

function PagesOf(Count: Integer): Integer;
begin
  Result := (Count + PerPage - 1) div PerPage;
  if Result < 1 then Result := 1;
end;

{ ---------------- how the last search of a slot is shown ----------------

  A command says how its table is to be drawn (NextView) before it starts its search, and the
  options go with that search (ViewOpt, stamped with its number, TPlayerContext.Gen). A result
  that is not the one they were given for is drawn with the plain options, never with those of
  another search. }
type
  TViewOpt = record
    Gen: Integer;        { the search they belong to; 0 = none }
    Simple: Boolean;     { /lastvisitors for players: who, when, how long - no address, no place }
    ListKey: Integer;    { /listprofiles: what it is sorted by (LIST_BY_*) }
    ListDesc: Boolean;
    ListDone: Boolean;   { the rows are in that order already }
  end;

var
  NextView: array[0..MAX_SLOTS] of TViewOpt;
  ViewOpt: array[0..MAX_SLOTS] of TViewOpt;

{ the options of the command that is being answered, back to the plain ones }
procedure ClearNextView(Slot: Integer);
begin
  if (Slot < 0) or (Slot > MAX_SLOTS) then Exit;
  FillChar(NextView[Slot], SizeOf(TViewOpt), 0);
end;

{ the options go with the search the slot has just started (StartTaskFor) }
procedure BindView(Slot: Integer);
begin
  if (Slot < 1) or (Slot > MAX_SLOTS) then Exit;
  EnterCriticalSection(Contexts[Slot].Lock);
  try
    ViewOpt[Slot] := NextView[Slot];
    ViewOpt[Slot].Gen := Contexts[Slot].Gen;
    ViewOpt[Slot].ListDone := False;
  finally LeaveCriticalSection(Contexts[Slot].Lock); end;
end;

{ a copy of the result a slot holds and the options it was asked with, in one hold of the lock }
function ViewSnapshot(Slot: Integer; out Opt: TViewOpt): TSearchResult;
begin
  Opt := Default(TViewOpt);
  if (Slot < 1) or (Slot > MAX_SLOTS) then
  begin
    Result := EmptyResult;
    Exit;
  end;
  EnterCriticalSection(Contexts[Slot].Lock);
  try
    Result := Contexts[Slot].Result;
    if (ViewOpt[Slot].Gen > 0) and (ViewOpt[Slot].Gen = Contexts[Slot].Gen) then Opt := ViewOpt[Slot];
  finally LeaveCriticalSection(Contexts[Slot].Lock); end;
end;

{ /listprofiles in the order that was asked for, once, before its first page is drawn. The search
  hands the accounts over by number, lowest first; the logins (the joins counted on the names of
  the account) and the first visit are read from the accounts when the order needs them, and the
  rows show them in a column of their own. Equal values go by number. The sorted rows take the
  place of the others only while the result is still the one that was sorted. }
procedure PrepareList(Slot: Integer);
const
  PER_HOLD = 4096;
var
  Snap: TSearchResult;
  Opt: TViewOpt;
  Rows: array of TSearchResultItem;
  Nums: array of Double;
  Txts: array of string;
  Perm, Tmp, Swap: array of Integer;
  n, i, j, k, Idx, Width, Lo, Mid, Hi, a, b: Integer;
  Sum: Int64;

  function Before(x, y: Integer): Boolean;
  var
    c: Integer;
  begin
    if Opt.ListKey = LIST_BY_NAME then c := CompareStr(Txts[x], Txts[y])
    else if Nums[x] < Nums[y] then c := -1
    else if Nums[x] > Nums[y] then c := 1
    else c := 0;
    if Opt.ListDesc then c := -c;
    if c = 0 then c := Snap.Results[x].ProfileIndex - Snap.Results[y].ProfileIndex;
    Result := c < 0;
  end;

begin
  { the game thread draws a table itself only when no worker could be started, and it never
    waits for the accounts: the list stays by number then (RenderPage leaves out the column) }
  if GetCurrentThreadId = GameThread then Exit;
  Snap := ViewSnapshot(Slot, Opt);
  if (Opt.Gen = 0) or Opt.ListDone or (Snap.TaskType <> TASK_LIST) or (Snap.PlayerID <> Slot) then Exit;
  { by number, lowest first: that is how the search gave them }
  if (Opt.ListKey = LIST_BY_ID) and not Opt.ListDesc then Exit;
  n := Snap.ResultCount;
  if n > Length(Snap.Results) then n := Length(Snap.Results);
  Nums := nil;
  Perm := nil;
  Tmp := nil;
  Rows := nil;
  SetLength(Nums, n);
  Txts := nil;
  if Opt.ListKey = LIST_BY_NAME then
  begin
    SetLength(Txts, n);
    for i := 0 to n - 1 do Txts[i] := LowerCase(PlainLetters(Snap.Results[i].ActiveName));
  end
  else if (Opt.ListKey = LIST_BY_LOGINS) or (Opt.ListKey = LIST_BY_FIRST) then
  begin
    i := 0;
    while i < n do
    begin
      LockProfiles;
      try
        k := 0;
        while (i < n) and (k < PER_HOLD) do
        begin
          Idx := Snap.Results[i].ProfileIndex;
          Nums[i] := 0;
          if (Idx >= 0) and (Idx < ProfileCount) then
            if Opt.ListKey = LIST_BY_FIRST then
              Nums[i] := Profiles[Idx].Profile.RegisterTime
            else
            begin
              Sum := 0;
              for j := 0 to High(Profiles[Idx].OtherNames) do
                Inc(Sum, Profiles[Idx].OtherNames[j].LoginCount);
              Nums[i] := Sum;
            end;
          Inc(i);
          Inc(k);
        end;
      finally UnlockProfiles; end;
    end;
  end
  else
    for i := 0 to n - 1 do
      case Opt.ListKey of
        LIST_BY_TIME: Nums[i] := Snap.Results[i].TimePlayed;
        LIST_BY_LAST: Nums[i] := Snap.Results[i].LastUsedDate;
        LIST_BY_NAMES: Nums[i] := Snap.Results[i].NamesCount;
        LIST_BY_IPS: Nums[i] := Snap.Results[i].IPsCount;
        LIST_BY_PCS: Nums[i] := Snap.Results[i].HWIDsCount;
        LIST_BY_STEAM: Nums[i] := Snap.Results[i].SteamsCount;
      else Nums[i] := Snap.Results[i].ProfileIndex;
      end;
  { a merge sort of the places: n log n whatever the values, and the rows are copied once }
  SetLength(Perm, n);
  SetLength(Tmp, n);
  for i := 0 to n - 1 do Perm[i] := i;
  Width := 1;
  while Width < n do
  begin
    Lo := 0;
    while Lo < n do
    begin
      Mid := Min(Lo + Width, n);
      Hi := Min(Lo + 2 * Width, n);
      a := Lo;
      b := Mid;
      for k := Lo to Hi - 1 do
        if (a < Mid) and ((b >= Hi) or not Before(Perm[b], Perm[a])) then
        begin
          Tmp[k] := Perm[a];
          Inc(a);
        end
        else
        begin
          Tmp[k] := Perm[b];
          Inc(b);
        end;
      Inc(Lo, 2 * Width);
    end;
    Swap := Perm;
    Perm := Tmp;
    Tmp := Swap;
    Width := Width * 2;
  end;
  SetLength(Rows, n);
  for i := 0 to n - 1 do
  begin
    Rows[i] := Snap.Results[Perm[i]];
    if Opt.ListKey = LIST_BY_LOGINS then Rows[i].LoginCount := Round(Nums[Perm[i]]);
    if Opt.ListKey = LIST_BY_FIRST then Rows[i].FirstUsedDate := Nums[Perm[i]];
  end;
  EnterCriticalSection(Contexts[Slot].Lock);
  try
    if (ViewOpt[Slot].Gen = Opt.Gen) and (Contexts[Slot].Gen = Opt.Gen) and
       (Pointer(Contexts[Slot].Result.Results) = Pointer(Snap.Results)) then
    begin
      Contexts[Slot].Result.Results := Rows;
      Contexts[Slot].Result.ResultCount := n;
      ViewOpt[Slot].ListDone := True;
    end;
  finally LeaveCriticalSection(Contexts[Slot].Lock); end;
end;

{ A column of names or values as wide as the longest one in the whole list - every page of it
  then looks the same - never narrower than its title and never wider than Cap. Field 0 = the
  name of the account, 1 = the value found (for an address: its length is also the most a
  masked one can take). }
function ColWidth(const R: TSearchResult; Field: Integer; const Title: string; Cap: Integer): Integer;
var
  i, L, n: Integer;
begin
  Result := Length(Title);
  n := R.ResultCount;
  if n > Length(R.Results) then n := Length(R.Results);
  for i := 0 to n - 1 do
  begin
    if Field = 1 then L := Length(R.Results[i].DataString) else L := Length(R.Results[i].ActiveName);
    if L > Result then Result := L;
    if Result >= Cap then
    begin
      Result := Cap;
      Exit;
    end;
  end;
end;

{ a cell cut to its column shows that it was cut: "LongPlayerNameNumber103" read as another
  name than the one it was cut from }
function CutCell(const S: string; W: Integer): string;
begin
  if (Length(S) > W) and (W > 2) then Result := Copy(S, 1, W - 2) + '..' else Result := S;
end;

{ a list ("a, b, c") for a last column with Room letters left: it ends after the last whole
  item that fits and says there is more }
function FitList(const S: string; Room: Integer): string;
var
  p: Integer;
begin
  Result := S;
  if (Room < 8) or (Length(S) <= Room) then Exit;
  p := Room - 4;
  while (p > 1) and (Copy(S, p, 2) <> ', ') do Dec(p);
  if p > 1 then Result := Copy(S, 1, p - 1) + ', ...'
  else Result := Copy(S, 1, Room - 3) + '...';
end;

{ the address of a unique visit and how many visits came from it ("83.22.1.9 x4") }
function VisitPlace(Slot: Integer; const Item: TSearchResultItem; ByCountry: Boolean): string;
begin
  if ByCountry then Result := Item.DataString
  else Result := AddressFor(Slot, Item.ProfileIndex, Item.DataString);
  if Item.LoginCount > 1 then Result := Result + ' x' + CountText(Item.LoginCount);
end;

{ the kinds that matched, in words }
function KindsText(Kinds: Integer): string;
var
  K: Integer;
begin
  Result := '';
  for K := KIND_NAME to KIND_IP do
    if (Kinds and (1 shl K)) <> 0 then
    begin
      if Result <> '' then Result := Result + ', ';
      Result := Result + LowerCase(KindName(K));
    end;
end;

{ the accounts of the other servers of the database under the results of this one }
procedure SayForeign(Slot: Integer; const Rows: TForeignRows; const Note: string);
const
  SHOWN = 30;
var
  i, NW, WW: Integer;
  V, Where: string;
begin
  if Note <> '' then
    Say(Slot, CLR_WARN, 'Other servers: ' + Note);
  if Length(Rows) = 0 then Exit;
  SaySection(Slot, 'On the other servers of the database (' + IntToStr(Length(Rows)) + ')');
  NW := 4;
  WW := 7;
  for i := 0 to Min(High(Rows), SHOWN - 1) do
  begin
    NW := Max(NW, Length(Rows[i].Name));
    WW := Max(WW, Length('S' + IntToStr(Rows[i].Server) + ' #' + IntToStr(Rows[i].Account)));
  end;
  if NW > 24 then NW := 24;
  Say(Slot, CLR_MAIN, Pad('Account', WW, 1) + ' | ' + Pad('Name', NW, 0) + ' | ' + Pad('Last seen', 10, 0) +
      ' | What matches');
  for i := 0 to Min(High(Rows), SHOWN - 1) do
  begin
    { an address, a Steam account or a computer stays with the admins, as on this server }
    V := SecretFor(Slot, -1, Rows[i].Kind, Rows[i].Value);
    Where := 'S' + IntToStr(Rows[i].Server) + ' #' + IntToStr(Rows[i].Account);
    Say(Slot, CLR_DATA, Pad(Where, WW, 1) + ' | ' + Pad(Rows[i].Name, NW, 0) + ' | ' +
        Pad(DateText(Rows[i].LastSeen), 10, 0) + ' | ' + KindsText(Rows[i].Kinds) + ': ' + V);
  end;
  if Length(Rows) > SHOWN then
    Say(Slot, CLR_DATA, '... and ' + IntToStr(Length(Rows) - SHOWN) + ' more');
  Say(Slot, CLR_DATA, 'S = the MysqlServerId of the server, # = the account there');
end;

{ One page of the result a slot is holding, drawn from a copy taken under the slot's lock (a new
  search or a player who leaves replaces the result meanwhile). In the tables of accounts the
  first column numbers the rows: a <command>p <row> asks about the account in that row. }
procedure RenderPage(Slot, Page: Integer);
var
  R: PSearchResult;
  Snap: TSearchResult;
  Opt: TViewOpt;
  i, First, Last, Total, Pages, Risk, Sim, Color, Share, RowW, NW, VW, AW: Integer;
  A, B, C, Lead: string;
  ServerWide, ByCountry: Boolean;
begin
  PrepareList(Slot);
  Snap := ViewSnapshot(Slot, Opt);
  R := @Snap;
  if R^.PlayerID <> Slot then
  begin
    Say(Slot, CLR_WARN, 'That result is gone, please ask again.');
    Exit;
  end;
  Total := R^.ResultCount;
  if Total > Length(R^.Results) then Total := Length(R^.Results);
  if Total = 0 then
  begin
    if (Length(R^.Foreign) > 0) or (R^.ForeignNote <> '') then
    begin
      Say(Slot, CLR_WARN, 'Nothing found on this server.');
      SayForeign(Slot, R^.Foreign, R^.ForeignNote);
    end
    else
      Say(Slot, CLR_WARN, 'No results found.');
    Exit;
  end;
  Pages := PagesOf(Total);
  if Page < 1 then Page := 1;
  if Page > Pages then Page := Pages;
  ViewPage[Slot] := Page;
  First := (Page - 1) * PerPage;
  Last := First + PerPage - 1;
  if Last >= Total then Last := Total - 1;
  RowW := Length(IntToStr(Total));
  NW := ColWidth(Snap, 0, 'Name', 24);

  Say(Slot, CLR_INFO, Pad('---[ AntiFake results (page ' + IntToStr(Page) + '/' +
      IntToStr(Pages) + ') ]---', VIEW_WIDTH, 2));

  case R^.TaskType of
    TASK_SEARCH, TASK_SEARCHID, TASK_LIST:
      begin
        { /listprofiles sorted by something the table does not show gets it as a last column;
          the logins and the first visit are in the rows once PrepareList has put them there }
        if (R^.TaskType = TASK_LIST) and not Opt.ListDone and (Opt.ListKey <> LIST_BY_LAST) then
          Opt.ListKey := LIST_BY_ID;
        if R^.TaskType = TASK_LIST then
        begin
          A := Pad('Time', 9, 1);
          case Opt.ListKey of
            LIST_BY_LOGINS: A := A + ' | ' + Pad('Logins', 6, 1);
            LIST_BY_LAST: A := A + ' | Last seen';
            LIST_BY_FIRST: A := A + ' | First seen';
          end;
        end
        else A := 'Last seen';
        Say(Slot, CLR_MAIN, Pad('#', RowW, 1) + ' | ' + Pad('Account', 7, 1) + ' | ' +
            Pad('Name', NW, 0) + ' | ' + Pad('Names', 5, 1) + ' | ' + Pad('Steam', 5, 1) + ' | ' +
            Pad('PC', 3, 1) + ' | ' + Pad('IP', 3, 1) + ' | ' + A);
        for i := First to Last do
        begin
          if R^.TaskType = TASK_LIST then
          begin
            B := Pad(SecondsText(R^.Results[i].TimePlayed), 9, 1);
            case Opt.ListKey of
              LIST_BY_LOGINS: B := B + ' | ' + Pad(CountText(R^.Results[i].LoginCount), 6, 1);
              LIST_BY_LAST: B := B + ' | ' + DateText(R^.Results[i].LastUsedDate);
              LIST_BY_FIRST: B := B + ' | ' + DateText(R^.Results[i].FirstUsedDate);
            end;
          end
          else B := DateText(R^.Results[i].LastUsedDate);
          Say(Slot, CLR_DATA, Pad(IntToStr(i + 1), RowW, 1) + ' | ' +
              Pad('#' + IntToStr(R^.Results[i].ProfileIndex), 7, 1) + ' | ' +
              Pad(R^.Results[i].ActiveName, NW, 0) + ' | ' +
              Pad(IntToStr(R^.Results[i].NamesCount), 5, 1) + ' | ' +
              Pad(IntToStr(R^.Results[i].SteamsCount), 5, 1) + ' | ' +
              Pad(IntToStr(R^.Results[i].HWIDsCount), 3, 1) + ' | ' +
              Pad(IntToStr(R^.Results[i].IPsCount), 3, 1) + ' | ' + B);
        end;
      end;

    TASK_PRIVACY, TASK_PRIVACYID:
      begin
        if (R^.Kind = KIND_IP) and Settings.GeoEnabled then
        begin
          AW := ColWidth(Snap, 1, 'Address', 39);
          Say(Slot, CLR_MAIN, Pad('#', 3, 1) + ' | ' + Pad('Address', AW, 0) + ' | ' +
              Pad('Risk', 8, 0) + ' | ' + Pad('Time', 9, 1) + ' | ' + Pad('Last', 10, 0) + ' | Where');
          for i := First to Last do
          begin
            Risk := R^.Results[i].RiskLevel;
            case Risk of
              1: A := 'mobile';
              2: A := 'proxy';
              3: A := 'hosting';
            else A := 'ordinary';
            end;
            if Risk > 0 then Color := CLR_WARN else Color := CLR_DATA;
            Say(Slot, Color, Pad(IntToStr(R^.Results[i].SubIndex), 3, 1) + ' | ' +
                Pad(AddressFor(Slot, R^.Results[i].ProfileIndex, R^.Results[i].DataString), AW, 0) + ' | ' +
                Pad(A, 8, 0) + ' | ' + Pad(SecondsText(R^.Results[i].TimePlayed), 9, 1) + ' | ' +
                Pad(DateText(R^.Results[i].LastUsedDate), 10, 0) + ' | ' +
                WhereFor(Slot, R^.Results[i].DataString));
          end;
        end
        else
        begin
          VW := ColWidth(Snap, 1, KindName(R^.Kind), 40);
          Say(Slot, CLR_MAIN, Pad('#', 3, 1) + ' | ' + Pad(KindName(R^.Kind), VW, 0) + ' | ' +
              Pad('Logins', 6, 1) + ' | ' + Pad('Time', 9, 1) + ' | ' +
              Pad('First', 10, 0) + ' | ' + Pad('Last', 10, 0));
          { an address, a Steam account or a computer stays with the admins and its owner here
            too, with or without places; a player with the whole reach used to see those of
            everybody else in full }
          for i := First to Last do
          begin
            A := SecretFor(Slot, R^.Results[i].ProfileIndex, R^.Kind, R^.Results[i].DataString);
            Say(Slot, CLR_DATA, Pad(IntToStr(R^.Results[i].SubIndex), 3, 1) + ' | ' +
                Pad(A, VW, 0) + ' | ' +
                Pad(CountText(R^.Results[i].LoginCount), 6, 1) + ' | ' +
                Pad(SecondsText(R^.Results[i].TimePlayed), 9, 1) + ' | ' +
                Pad(DateText(R^.Results[i].FirstUsedDate), 10, 0) + ' | ' +
                Pad(DateText(R^.Results[i].LastUsedDate), 10, 0));
          end;
        end;
      end;

    TASK_CHECKP, TASK_CHECKD:
      begin
        Say(Slot, CLR_MAIN, Pad('#', RowW, 1) + ' | ' + Pad('Account', 7, 1) + ' | ' +
            Pad('Name', NW, 0) + ' | ' + Pad('What matches', 12, 0) + ' | Value');
        { what matched and the value that did (a Steam account, a computer or an address in full
          for an admin and its owner only); a shared name is the weakest tie }
        for i := First to Last do
        begin
          if R^.Results[i].MatchType > 0 then Color := CLR_WARN else Color := CLR_DATA;
          if R^.Results[i].MatchType = KIND_GEO_MATCH then A := 'PLACE'
          else A := KindName(R^.Results[i].MatchType);
          Say(Slot, Color, Pad(IntToStr(i + 1), RowW, 1) + ' | ' +
              Pad('#' + IntToStr(R^.Results[i].ProfileIndex), 7, 1) + ' | ' +
              Pad(R^.Results[i].ActiveName, NW, 0) + ' | ' + Pad(A, 12, 0) + ' | ' +
              SecretFor(Slot, R^.Results[i].ProfileIndex, R^.Results[i].MatchType, R^.Results[i].DataString));
        end;
      end;

    TASK_ADVANCED_SEARCH:
      begin
        VW := ColWidth(Snap, 1, 'Name found', 24);
        NW := ColWidth(Snap, 0, 'Account now called', 24);
        Say(Slot, CLR_MAIN, Pad('#', RowW, 1) + ' | ' + Pad('Match', 5, 1) + ' | ' +
            Pad('Name found', VW, 0) + ' | ' + Pad('Account now called', NW, 0) + ' | Account');
        for i := First to Last do
        begin
          Sim := R^.Results[i].Similarity;
          if Sim >= 95 then Color := CLR_ERROR
          else if Sim >= 80 then Color := CLR_WARN
          else Color := CLR_DATA;
          Say(Slot, Color, Pad(IntToStr(i + 1), RowW, 1) + ' | ' + Pad(IntToStr(Sim) + '%', 5, 1) + ' | ' +
              Pad(R^.Results[i].DataString, VW, 0) + ' | ' +
              Pad(R^.Results[i].ActiveName, NW, 0) + ' | ' +
              '#' + IntToStr(R^.Results[i].ProfileIndex));
        end;
      end;

    TASK_GEOSEARCH, TASK_LOCATE_ALL:
      begin
        AW := ColWidth(Snap, 1, 'Address', 39);
        Say(Slot, CLR_MAIN, Pad('#', RowW, 1) + ' | ' + Pad('Account', 7, 1) + ' | ' +
            Pad('Name', NW, 0) + ' | ' + Pad('Address', AW, 0) + ' | Where');
        for i := First to Last do
          Say(Slot, CLR_DATA, Pad(IntToStr(i + 1), RowW, 1) + ' | ' +
              Pad('#' + IntToStr(R^.Results[i].ProfileIndex), 7, 1) + ' | ' +
              Pad(R^.Results[i].ActiveName, NW, 0) + ' | ' +
              Pad(AddressFor(Slot, R^.Results[i].ProfileIndex, R^.Results[i].DataString), AW, 0) + ' | ' +
              WhereFor(Slot, R^.Results[i].DataString));
      end;

    TASK_VISITS, TASK_UNIQUE_VISITS:
      begin
        ServerWide := R^.ProfileIndex < 0;
        if (R^.TaskType = TASK_UNIQUE_VISITS) and (R^.Kind = 2) and
           (Opt.Simple or not ViewerAdmin[Slot]) then
        begin
          { !lastvisitors: who came, when, how often and for how long, and on which account -
            no address and no place. The players get this one, and so does an admin who asks
            with the ! of the players; /lastvisitors of an admin is the table below. }
          Say(Slot, CLR_MAIN, Pad('#', RowW, 1) + ' | ' + Pad('Last visit', 16, 0) + ' | ' +
              Pad('Visits', 6, 1) + ' | ' + Pad('Time', 8, 1) + ' | ' + Pad('Account', 7, 1) + ' | Name');
          for i := First to Last do
            Say(Slot, CLR_DATA, Pad(IntToStr(i + 1), RowW, 1) + ' | ' +
                Pad(DateTimeText(R^.Results[i].LastUsedDate), 16, 0) + ' | ' +
                Pad(CountText(R^.Results[i].LoginCount), 6, 1) + ' | ' +
                Pad(SecondsText(R^.Results[i].TimePlayed), 8, 1) + ' | ' +
                Pad('#' + IntToStr(R^.Results[i].ProfileIndex), 7, 1) + ' | ' +
                R^.Results[i].ActiveName);
        end
        else if (R^.TaskType = TASK_UNIQUE_VISITS) and (R^.Kind = 2) then
        begin
          { /lastvisitors: one row per account, its last visit first. The columns alone are
            100 letters wide, so what the visits were (the flags) goes on the line of the place
            below each row: as a last column it ran past the width of every other table. }
          AW := ColWidth(Snap, 1, 'Address', 39);
          Say(Slot, CLR_MAIN, Pad('#', RowW, 1) + ' | ' + Pad('Last visit', 16, 0) + ' | ' +
              Pad('Visits', 6, 1) + ' | ' + Pad('Time', 8, 1) + ' | ' + Pad('Account', 7, 1) + ' | ' +
              Pad('Name', NW, 0) + ' | ' + Pad('Address', AW, 0));
          for i := First to Last do
          begin
            Risk := R^.Results[i].Similarity;
            if Risk >= 85 then Color := CLR_DATA
            else if Risk >= 30 then Color := CLR_WARN
            else Color := CLR_ERROR;
            Say(Slot, Color, Pad(IntToStr(i + 1), RowW, 1) + ' | ' +
                Pad(DateTimeText(R^.Results[i].LastUsedDate), 16, 0) + ' | ' +
                Pad(CountText(R^.Results[i].LoginCount), 6, 1) + ' | ' +
                Pad(SecondsText(R^.Results[i].TimePlayed), 8, 1) + ' | ' +
                Pad('#' + IntToStr(R^.Results[i].ProfileIndex), 7, 1) + ' | ' +
                Pad(R^.Results[i].ActiveName, NW, 0) + ' | ' +
                AddressFor(Slot, R^.Results[i].ProfileIndex, R^.Results[i].DataString));
            B := FlagsText(R^.Results[i].MatchType);
            if B = '-' then B := '' else B := '  [' + B + ']';
            if (R^.Results[i].Location <> '') or (B <> '') then
              Say(Slot, CLR_DATA, Pad('', RowW + 3 + 16, 0) + '   ' + R^.Results[i].Location + B);
          end;
        end
        else
        begin
          { One row per visit (/visits, /lastvisits) or per address (/uniquevisits), the place
            and the flags on the line below it: as a last column the flags ran past the width of
            the view. One row per country (/locations) has no line below, and keeps them.
            "bycountry" means something only there: /visits bycountry used to put the address
            of every visit, whole, under the title "Country". }
          ByCountry := (R^.TaskType = TASK_UNIQUE_VISITS) and (R^.Kind = 1);
          if ByCountry then A := 'Country' else A := 'Address';
          AW := Length(A);
          for i := 0 to Total - 1 do
            AW := Max(AW, Length(VisitPlace(-1, R^.Results[i], ByCountry)));
          if AW > 44 then AW := 44;
          { the visits of the whole server (/lastvisits) say whose they are }
          if ServerWide then Lead := Pad('#', RowW, 1) + ' | ' else Lead := '';
          B := Pad('When', 16, 0) + ' | ' + Pad('For', 8, 1) + ' | ';
          if ServerWide then B := B + Pad('Account', 7, 1) + ' | ';
          if ByCountry then C := ' | Flags' else C := '';
          Say(Slot, CLR_MAIN, Lead + B + Pad('Name', NW, 0) + ' | ' + Pad(A, AW, 0) + ' | ' +
              Pad('Sure', 4, 1) + C);
          for i := First to Last do
          begin
            Risk := R^.Results[i].Similarity;
            if Risk >= 85 then Color := CLR_DATA
            else if Risk >= 30 then Color := CLR_WARN
            else Color := CLR_ERROR;
            if ServerWide then Lead := Pad(IntToStr(i + 1), RowW, 1) + ' | ' else Lead := '';
            B := Pad(DateTimeText(R^.Results[i].FirstUsedDate), 16, 0) + ' | ' +
                 Pad(SecondsText(R^.Results[i].TimePlayed), 8, 1) + ' | ';
            if ServerWide then B := B + Pad('#' + IntToStr(R^.Results[i].ProfileIndex), 7, 1) + ' | ';
            if ByCountry then C := ' | ' + FlagsText(R^.Results[i].MatchType) else C := '';
            Say(Slot, Color, Lead + B + Pad(R^.Results[i].ActiveName, NW, 0) + ' | ' +
                Pad(VisitPlace(Slot, R^.Results[i], ByCountry), AW, 0) + ' | ' +
                Pad(IntToStr(Risk) + '%', 4, 1) + C);
            if ByCountry then Continue;
            C := FlagsText(R^.Results[i].MatchType);
            if C = '-' then C := '' else C := '  [' + C + ']';
            if (R^.Results[i].Location <> '') or (C <> '') then
              Say(Slot, CLR_DATA, Pad('', Length(Lead) + 16, 0) + '   ' + R^.Results[i].Location + C);
          end;
        end;
      end;

    TASK_VISIT_STATS:
      begin
        Share := 0;
        for i := 0 to Total - 1 do Inc(Share, R^.Results[i].LoginCount);
        if Share < 1 then Share := 1;
        VW := 7;
        for i := 0 to Total - 1 do
        begin
          A := R^.Results[i].Location;
          if A = '' then
            if R^.Results[i].DataString = '??' then A := 'unknown'
            else A := R^.Results[i].DataString;
          VW := Max(VW, Length(A));
        end;
        { A country's name of at most 18 letters (the ISO code beside a name that is cut says
          which country it is) and a bar of 11 leave room for the flags. The flags of a country
          are the kinds of line its visits came through: that some account came from a new
          address or a new computer is true of nearly every country, said nothing, and pushed
          the row past the width of the view. }
        if VW > 18 then VW := 18;
        Say(Slot, CLR_MAIN, Pad('#', RowW, 1) + ' | ' + Pad('Country', VW, 0) + ' | ' +
            Pad('ISO', 3, 0) + ' | ' + Pad('Visits', 6, 1) + ' | ' + Pad('Share', 18, 0) + ' | ' +
            Pad('Time', 9, 1) + ' | ' + Pad('Last', 10, 0) + ' | Flags');
        for i := First to Last do
        begin
          A := R^.Results[i].Location;
          if A = '' then
            if R^.Results[i].DataString = '??' then A := 'unknown'
            else A := R^.Results[i].DataString;
          Risk := Round(R^.Results[i].LoginCount * 100.0 / Share);
          Say(Slot, CLR_DATA, Pad(IntToStr(i + 1), RowW, 1) + ' | ' + Pad(A, VW, 0) + ' | ' +
              Pad(R^.Results[i].DataString, 3, 0) + ' | ' +
              Pad(CountText(R^.Results[i].LoginCount), 6, 1) + ' | ' +
              Pad('[' + Bar(R^.Results[i].LoginCount, Share, 11) + '] ' +
                  Pad(IntToStr(Risk) + '%', 4, 1), 18, 0) + ' | ' +
              Pad(SecondsText(R^.Results[i].TimePlayed), 9, 1) + ' | ' +
              Pad(DateText(R^.Results[i].LastUsedDate), 10, 0) + ' | ' +
              FlagsText(R^.Results[i].MatchType and (VF_PROXY or VF_HOSTING or VF_MOBILE or VF_UNKNOWN)));
        end;
      end;

    TASK_VISITS_OF:
      begin
        Say(Slot, CLR_MAIN, Pad('#', RowW, 1) + ' | ' + Pad('Account', 7, 1) + ' | ' +
            Pad('Name', NW, 0) + ' | ' + Pad('Visits', 6, 1) + ' | ' +
            Pad('Time there', 10, 1) + ' | ' + Pad('Last', 10, 0));
        for i := First to Last do
          Say(Slot, CLR_DATA, Pad(IntToStr(i + 1), RowW, 1) + ' | ' +
              Pad('#' + IntToStr(R^.Results[i].ProfileIndex), 7, 1) + ' | ' +
              Pad(R^.Results[i].ActiveName, NW, 0) + ' | ' +
              Pad(CountText(R^.Results[i].LoginCount), 6, 1) + ' | ' +
              Pad(SecondsText(R^.Results[i].TimePlayed), 10, 1) + ' | ' +
              Pad(DateText(R^.Results[i].LastUsedDate), 10, 0));
      end;

    TASK_MULTI:
      begin
        { One row per account: how many it is tied to, the score and the closest of them, and
          what an admin said about that pair. The line below says what ties the account to all
          of them and why the closest one looks like the same person: as columns of their own
          the row was 124 letters wide, and "They share" - every tie of the account, next to
          the closest - was cut to 16 letters ("computer+steam+a"). }
        VW := 7;
        for i := 0 to Total - 1 do
          VW := Max(VW, Length('#' + IntToStr(R^.Results[i].SubIndex) + ' ' + R^.Results[i].Location));
        VW := Min(VW, Min(31, VIEW_WIDTH - (RowW + NW + 46)));
        Say(Slot, CLR_MAIN, Pad('#', RowW, 1) + ' | ' + Pad('Account', 7, 1) + ' | ' +
            Pad('Name', NW, 0) + ' | ' + Pad('Tied', 4, 1) + ' | ' + Pad('Score', 7, 1) + ' | ' +
            Pad('Closest', VW, 0) + ' | Admin said');
        for i := First to Last do
        begin
          Sim := R^.Results[i].Similarity;
          if Sim >= Settings.MultiReportScore then Color := CLR_ERROR
          else if Sim >= 40 then Color := CLR_WARN
          else Color := CLR_DATA;
          Say(Slot, Color, Pad(IntToStr(i + 1), RowW, 1) + ' | ' +
              Pad('#' + IntToStr(R^.Results[i].ProfileIndex), 7, 1) + ' | ' +
              Pad(R^.Results[i].ActiveName, NW, 0) + ' | ' +
              Pad(CountText(R^.Results[i].LoginCount), 4, 1) + ' | ' +
              Pad(IntToStr(Sim) + ' (' + IntToStr(R^.Results[i].RiskLevel) + ')', 7, 1) + ' | ' +
              Pad(CutCell('#' + IntToStr(R^.Results[i].SubIndex) + ' ' + R^.Results[i].Location, VW), VW, 0) + ' | ' +
              VerdictText(VerdictSeen(R^.Results[i].ProfileIndex, R^.Results[i].SubIndex)));
          Say(Slot, CLR_DATA, Pad('', RowW, 0) + '   tied by ' + LinkText(R^.Results[i].MatchType) +
              '; the closest: ' + R^.Results[i].DataString);
        end;
      end;

    TASK_MULTI_TOP:
      begin
        if R^.Kind = 1 then A := 'Address' else A := 'Computer';
        AW := ColWidth(Snap, 1, A, 39);
        Say(Slot, CLR_MAIN, Pad('#', RowW, 1) + ' | ' + Pad(A, AW, 0) + ' | ' +
            Pad('Accounts', 8, 1) + ' | Who');
        for i := First to Last do
        begin
          Risk := R^.Results[i].LoginCount;
          if Risk >= 5 then Color := CLR_ERROR
          else if Risk >= 3 then Color := CLR_WARN
          else Color := CLR_DATA;
          { the addresses and the computers shared by several accounts: whole for an admin only
            (a computer was shown in full to anybody the settings let ask) }
          if R^.Kind = 1 then B := SecretFor(Slot, -1, KIND_IP, R^.Results[i].DataString)
          else B := SecretFor(Slot, -1, KIND_HWID, R^.Results[i].DataString);
          { the names, as many as fit (four long ones made the row 140 letters wide) }
          Say(Slot, Color, Pad(IntToStr(i + 1), RowW, 1) + ' | ' +
              Pad(B, AW, 0) + ' | ' + Pad(CountText(Risk), 8, 1) + ' | ' +
              FitList(R^.Results[i].Location, VIEW_WIDTH - (RowW + AW + 17)));
        end;
      end;

    TASK_STATUSA:
      begin
        { the accounts tied to one account (/af_next after /statusa): the row used to say only
          the name, without the number of the account or what ties it }
        Say(Slot, CLR_MAIN, Pad('#', RowW, 1) + ' | ' + Pad('Account', 7, 1) + ' | ' +
            Pad('Name', NW, 0) + ' | Tied by');
        for i := First to Last do
        begin
          if R^.Results[i].MatchType = 1 then
          begin
            A := 'a Steam account, a computer or an address';
            Color := CLR_WARN;
          end
          else
          begin
            A := 'a name only';
            Color := CLR_DATA;
          end;
          Say(Slot, Color, Pad(IntToStr(i + 1), RowW, 1) + ' | ' +
              Pad('#' + IntToStr(R^.Results[i].ProfileIndex), 7, 1) + ' | ' +
              Pad(R^.Results[i].ActiveName, NW, 0) + ' | ' + A);
        end;
      end;
  else
    for i := First to Last do
      Say(Slot, CLR_DATA, Pad(IntToStr(i + 1), RowW, 1) + ' | ' + R^.Results[i].DataString);
  end;

  { the other servers under the first page }
  if Page = 1 then
    SayForeign(Slot, R^.Foreign, R^.ForeignNote);
  C := IntToStr(Total) + ' rows in ' + FormatFloat('0.000', R^.ExecutionTime) + ' s';
  if Pages > 1 then C := C + '   ' + CMD_PREFIX + 'af_next / ' + CMD_PREFIX + 'af_prev / ' +
                            CMD_PREFIX + 'af_page n';
  Say(Slot, CLR_INFO, Pad('page ' + IntToStr(Page) + '/' + IntToStr(Pages) + '   ' + C,
      VIEW_WIDTH, 2));
end;

{ the summary of one account, which is not a table }
procedure RenderStatus(Slot: Integer);
var
  R: PSearchResult;
  Snap: TSearchResult;
  i, n, Idx: Integer;
  Line, Steam: string;
  Items: TStringList;
begin
  Snap := ResultSnapshot(Slot);
  R := @Snap;
  if R^.PlayerID <> Slot then
  begin
    Say(Slot, CLR_WARN, 'That result is gone, please ask again.');
    Exit;
  end;
  Idx := R^.ProfileIndex;
  if Idx < 0 then
  begin
    Say(Slot, CLR_ERROR, 'No such account.');
    Exit;
  end;
  SayHeader(Slot, 'Account #' + IntToStr(Idx) + ': ' + R^.ActiveName);
  Say(Slot, CLR_MAIN, Pad('First seen', 14, 0) + ': ' + DateTimeText(R^.RegisterTime) +
      '    ' + Pad('Last seen', 12, 0) + ': ' + DateTimeText(R^.LastPlayedTime));
  Say(Slot, CLR_MAIN, Pad('Time played', 14, 0) + ': ' + SecondsText(R^.TimePlayed) +
      '    ' + Pad('As spectator', 12, 0) + ': ' + SecondsText(R^.TimeOnSpectator));
  if R^.TopLocations <> '' then
    Say(Slot, CLR_MAIN, Pad('Comes from', 14, 0) + ': ' + R^.TopLocations);

  Steam := SteamLineOf(Idx);
  if Steam <> '' then Say(Slot, CLR_MAIN, Pad('Steam', 14, 0) + ': ' + Steam);

  { the ties are for a viewer who may ask /multi about this account (the search left them out
    for anybody else, and the summary below would tell them all the same) }
  Line := '';
  if R^.Kind <> STATUS_TIES_NONE then Line := MultiSummaryOf(Idx);
  if Line <> '' then
  begin
    SaySection(Slot, 'Looks like one person with');
    Say(Slot, CLR_WARN, Line);
    Say(Slot, CLR_DATA, CMD_PREFIX + 'multi ' + IntToStr(Idx) + ' for the whole list');
  end;

  n := Min(R^.ResultCount, Length(R^.Results));
  if n > 0 then
  begin
    SaySection(Slot, 'Accounts that share something with this one');
    Items := TStringList.Create;
    try
      { three columns of at most 33 letters: a longer name (from a file or another server) used
        to widen every column past the width of the view. The rest is a table of its own. }
      for i := 0 to n - 1 do
      begin
        if i >= 12 then
        begin
          Items.Add(IntToStr(n - 12) + ' more: ' + CMD_PREFIX + 'af_page 1');
          Break;
        end;
        Items.Add(Copy('#' + IntToStr(R^.Results[i].ProfileIndex) + ' ' + R^.Results[i].ActiveName, 1, 32));
      end;
      SayGrid(Slot, Items, 3, 25, CLR_DATA);
    finally Items.Free; end;
  end;
  SayForeign(Slot, R^.Foreign, R^.ForeignNote);
  Say(Slot, CLR_INFO, Frame('', '=', VIEW_WIDTH));
end;

{ what the script asks for once a task is finished }
procedure RenderResult(Slot: Integer);
var
  Snap: TSearchResult;
begin
  Snap := ResultSnapshot(Slot);
  if Snap.TaskType = TASK_STATUSA then RenderStatus(Slot) else RenderPage(Slot, 1);
end;
