{ ---------------------------------------------------------------------------------------
  AntiFake Extended - the letters and the line buffer every view is built from.

  The script used to build the tables itself. ScriptCore is an interpreter and every line
  of it runs on the game thread, so the same work here is many times cheaper, and the
  script is left with what only it can do: read the players and print the lines.
  --------------------------------------------------------------------------------------- }

const
  { every table is this wide: Soldat draws a console line as long as a script makes it, and on
    Full HD that is far more than the chat lets anybody type }
  VIEW_WIDTH = 100;
  { who /locate talks about }
  LOCATE_SELF = 0;
  LOCATE_NAME = 1;
  LOCATE_ALL = 2;

{ The colours of everything the library writes. They are settings ([Colors] in settings.ini),
  so a server can match its own look; these read them every time, which costs nothing and
  means a colour changed in the file is used the moment the settings are read again. }
function CLR_MAIN: Integer;
begin
  Result := Settings.ColMain;
end;

function CLR_INFO: Integer;
begin
  Result := Settings.ColInfo;
end;

function CLR_DATA: Integer;
begin
  Result := Settings.ColData;
end;

function CLR_WARN: Integer;
begin
  Result := Settings.ColWarn;
end;

function CLR_ERROR: Integer;
begin
  Result := Settings.ColError;
end;

function CLR_GOOD: Integer;
begin
  Result := Settings.ColGood;
end;

type
  TOutBuffer = record
    Lines: TStringList;     { one line per entry, the colour in Objects }
    Cut: Boolean;           { the answer was cut (Say): the rest of it is not wanted either }
  end;

var
  Outp: array[0..MAX_SLOTS] of TOutBuffer;
  OutCS: TRTLCriticalSection;
  { who is on the server right now; the script keeps this up to date so the library can draw
    the tables that talk about the players in the game }
  OnlineProfile: array[0..MAX_SLOTS] of Integer;
  OnlineName: array[0..MAX_SLOTS] of string;
  OnlineIP: array[0..MAX_SLOTS] of string;
  OnlineHwid: array[0..MAX_SLOTS] of string;
  OnlineHere: array[0..MAX_SLOTS] of Boolean;
  { the game thread writes the lists above at every join and leave while the workers draw from
    them: everything is copied under this lock (OnlineRow) }
  OnlineCS: TRTLCriticalSection;
  { the page a player is looking at, so /af_next and /af_prev know where they are }
  ViewPage: array[0..MAX_SLOTS] of Integer;

type
  TOnlineRow = record
    Here: Boolean;
    Profile: Integer;
    Name, IP, Hwid: string;
  end;

{ who plays on a slot, as one copy taken under the lock }
function OnlineRow(Slot: Integer): TOnlineRow;
begin
  Result.Here := False;
  Result.Profile := -1;
  Result.Name := '';
  Result.IP := '';
  Result.Hwid := '';
  if (Slot < 0) or (Slot > MAX_SLOTS) then Exit;
  EnterCriticalSection(OnlineCS);
  try
    Result.Here := OnlineHere[Slot];
    Result.Profile := OnlineProfile[Slot];
    Result.Name := OnlineName[Slot];
    Result.IP := OnlineIP[Slot];
    Result.Hwid := OnlineHwid[Slot];
  finally LeaveCriticalSection(OnlineCS); end;
end;

function OnlineProfileOf(Slot: Integer): Integer;
begin
  Result := -1;
  if (Slot < 0) or (Slot > MAX_SLOTS) then Exit;
  EnterCriticalSection(OnlineCS);
  try
    Result := OnlineProfile[Slot];
  finally LeaveCriticalSection(OnlineCS); end;
end;

{ ---------------- small helpers ---------------- }

{ every padded cell and every frame of every table comes through here: the text is made at its
  full length at once instead of growing a letter at a time }
function Repeated(const Ch: string; Count: Integer): string;
var
  k, n: Integer;
begin
  Result := '';
  n := Length(Ch);
  if (Count <= 0) or (n = 0) then Exit;
  SetLength(Result, Count * n);
  if n = 1 then
    FillChar(Result[1], Count, Ch[1])
  else
    for k := 0 to Count - 1 do Move(Ch[1], Result[k * n + 1], n);
end;

{ Align 0 = left, 1 = right, 2 = centred; longer text is cut, never wrapped }
function Pad(const Text: string; Len, Align: Integer): string;
var
  S: string;
  L: Integer;
begin
  S := Text;
  if Length(S) > Len then S := Copy(S, 1, Len);
  L := Len - Length(S);
  case Align of
    1: Result := Repeated(' ', L) + S;
    2: Result := Repeated(' ', L div 2) + S + Repeated(' ', L - (L div 2));
  else Result := S + Repeated(' ', L);
  end;
end;

function Frame(const Title, Ch: string; Width: Integer): string;
var
  Inner: string;
  L: Integer;
begin
  if Title = '' then
  begin
    Result := Repeated(Ch, Width);
    Exit;
  end;
  Inner := ' ' + Title + ' ';
  if Length(Inner) >= Width then
  begin
    Result := Inner;
    Exit;
  end;
  L := (Width - Length(Inner)) div 2;
  Result := Repeated(Ch, L) + Inner + Repeated(Ch, Width - Length(Inner) - L);
end;

function SecondsText(Sec: Int64): string;
var
  d, h, m: Int64;
begin
  if Sec < 0 then Sec := 0;
  d := Sec div 86400;
  h := (Sec mod 86400) div 3600;
  m := (Sec mod 3600) div 60;
  if d > 0 then Result := IntToStr(d) + 'd ' + IntToStr(h) + 'h'
  else if h > 0 then Result := IntToStr(h) + 'h ' + IntToStr(m) + 'm'
  else if m > 0 then Result := IntToStr(m) + 'm ' + IntToStr(Sec mod 60) + 's'
  else Result := IntToStr(Sec) + 's';
end;

{ A date read from a damaged file can be any number, and one past the year 9999 made
  FormatDateTime raise: the whole table that showed it was lost. It is shown as '?'. }
{ a counter as ZitroStats shows its own: in full below 10 000, then 12.3k and 1.2M }
function CountText(N: Int64): string;
begin
  if N < 10000 then Result := IntToStr(N)
  else if N < 1000000 then Result := FormatFloat('0.0', N / 1000.0) + 'k'
  else Result := FormatFloat('0.0', N / 1000000.0) + 'M';
end;

function DateText(DT: Double): string;
begin
  if IsNan(DT) or (DT < 1) then Result := 'never'
  else if DT >= 2958466 then Result := '?'
  else Result := FormatDateTime('yyyy-mm-dd', DT);
end;

function DateTimeText(DT: Double): string;
begin
  if IsNan(DT) or (DT < 1) then Result := 'never'
  else if DT >= 2958466 then Result := '?'
  else Result := FormatDateTime('yyyy-mm-dd hh:nn', DT);
end;

function KindName(Kind: Integer): string;
begin
  case Kind of
    KIND_NAME: Result := 'NAME';
    KIND_STEAM: Result := 'STEAM';
    KIND_HWID: Result := 'HWID';
    KIND_IP: Result := 'IP';
  else Result := 'DATA';
  end;
end;

{ a bar of the given width, filled to the share Value is of Total }
function Bar(Value, Total, Width: Integer): string;
var
  n: Integer;
begin
  if Total < 1 then Total := 1;
  if Width < 0 then Width := 0;
  { in 64 bits: a big count times the width ran over the 32-bit range and drew nonsense }
  n := Round(Int64(Value) * Width / Total);
  if n > Width then n := Width;
  if n < 0 then n := 0;
  Result := Pad(Repeated('#', n), Width, 0);
end;

function FlagsText(Flags: Integer): string;
begin
  Result := '';
  if (Flags and VF_PROXY) <> 0 then Result := Result + 'vpn ';
  if (Flags and VF_HOSTING) <> 0 then Result := Result + 'host ';
  if (Flags and VF_MOBILE) <> 0 then Result := Result + 'mob ';
  if (Flags and VF_NEW_IP) <> 0 then Result := Result + 'new-ip ';
  if (Flags and VF_NEW_HWID) <> 0 then Result := Result + 'new-pc ';
  if (Flags and VF_UNKNOWN) <> 0 then Result := Result + 'no-geo ';
  Result := Trim(Result);
  if Result = '' then Result := '-';
end;

function VerdictText(V: Integer): string;
begin
  case V of
    VERDICT_SAME: Result := 'one person';
    VERDICT_FAMILY: Result := 'one family';
    VERDICT_IGNORE: Result := 'ignored';
  else Result := '-';
  end;
end;

{ what two accounts have in common, in words }
function LinkText(Bits: Integer): string;
begin
  Result := '';
  if (Bits and LINK_COMPUTER) <> 0 then Result := 'computer';
  if (Bits and LINK_STEAM) <> 0 then
  begin
    if Result <> '' then Result := Result + '+';
    Result := Result + 'steam';
  end;
  if (Bits and LINK_ADDRESS) <> 0 then
  begin
    if Result <> '' then Result := Result + '+';
    Result := Result + 'address';
  end
  else if (Bits and LINK_NETWORK) <> 0 then
  begin
    if Result <> '' then Result := Result + '+';
    Result := Result + 'network';
  end;
  if Result = '' then Result := '?';
end;

{ ---------------- the buffer the script reads ---------------- }

procedure OutClear(Slot: Integer);
begin
  if (Slot < 0) or (Slot > MAX_SLOTS) then Exit;
  EnterCriticalSection(OutCS);
  try
    if Outp[Slot].Lines <> nil then Outp[Slot].Lines.Clear;
    Outp[Slot].Cut := False;
  finally LeaveCriticalSection(OutCS); end;
end;

{ One answer is MaxQueuedLines long at most: that is all the script keeps waiting for a player,
  and the lines of a longer one could be dropped there without a word. The last line that fits
  says that the rest is cut, and nothing more of that answer comes after it (every answer
  starts with OutClear). }
procedure Say(Slot, Color: Integer; const Text: string);
var
  Cap: Integer;
begin
  if (Slot < 0) or (Slot > MAX_SLOTS) then Exit;
  Cap := Settings.MaxQueuedLines;
  if Cap < 20 then Cap := 20;
  EnterCriticalSection(OutCS);
  try
    if (Outp[Slot].Lines = nil) or Outp[Slot].Cut then Exit;
    if Outp[Slot].Lines.Count >= Cap - 1 then
    begin
      Outp[Slot].Lines.AddObject('... cut here: one answer is ' + IntToStr(Cap) +
        ' lines at most (MaxQueuedLines)', TObject(PtrInt(Settings.ColWarn)));
      Outp[Slot].Cut := True;
    end
    else if Length(TrimRight(Text)) > VIEW_WIDTH then
      { a place, a provider or a host name has no length of its own: the last column of a table
        ran past the width of the view with a long one; it is cut and shows that it was }
      Outp[Slot].Lines.AddObject(Copy(Text, 1, VIEW_WIDTH - 2) + '..', TObject(PtrInt(Color)))
    else
      Outp[Slot].Lines.AddObject(TrimRight(Text), TObject(PtrInt(Color)));
  finally LeaveCriticalSection(OutCS); end;
end;

procedure SayHeader(Slot: Integer; const Title: string);
begin
  Say(Slot, CLR_INFO, Frame(Title, '=', VIEW_WIDTH));
end;

procedure SaySection(Slot: Integer; const Title: string);
begin
  Say(Slot, CLR_WARN, Frame(Title, '-', VIEW_WIDTH));
end;

procedure SayRule(Slot: Integer);
begin
  Say(Slot, CLR_WARN, Repeated('-', VIEW_WIDTH));
end;

{ Items laid out in columns of ColW letters, Cols to a row (never wider than VIEW_WIDTH), the
  way ZitroStats lays out its help: every item starts at a column; one that would leave fewer
  than two spaces before the next column takes two columns (or more), and a row ends where the
  next item no longer fits. No two items touch, and the sections of a view keep the same
  columns. The list is emptied. }
procedure SayGrid(Slot: Integer; Items: TStringList; Cols, ColW, Color: Integer);
var
  k, Used, Span: Integer;
  Line: string;
begin
  if Cols < 1 then Cols := 1;
  if ColW < 1 then ColW := 1;
  while (Cols > 1) and (Cols * ColW > VIEW_WIDTH) do Dec(Cols);
  Line := '';
  Used := 0;
  for k := 0 to Items.Count - 1 do
  begin
    Span := (Length(Items[k]) + 2 + ColW - 1) div ColW;
    if Span < 1 then Span := 1;
    if (Used > 0) and (Used + Span > Cols) then
    begin
      Say(Slot, Color, TrimRight(Line));
      Line := '';
      Used := 0;
    end;
    Line := Line + Pad(Items[k], Span * ColW, 0);
    Inc(Used, Span);
  end;
  if Line <> '' then Say(Slot, Color, TrimRight(Line));
  Items.Clear;
end;
