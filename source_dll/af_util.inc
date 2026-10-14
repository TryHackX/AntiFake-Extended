{ ---------------------------------------------------------------------------------------
  AntiFake Extended - text, addresses and the small things everything else uses.
  --------------------------------------------------------------------------------------- }

{ The code page of the text the library keeps and draws its tables in, one byte a letter: the
  Central European one when the names of the players come in it (MysqlCodepage = 1250, every
  Polish letter has its byte there), else the Western one. }
function ConsoleCodepage: Integer;
begin
  if Settings.MysqlCodepage = 1250 then Result := 1250 else Result := 1252;
end;

{ A line on its way to the game console. Soldat 1.7.1 reads the console as UTF-8 (the client
  and the server set their code page to UTF-8), and a byte above 127 on its own was drawn as
  "?" ("Poland, ?wilcza"). A byte of ConsoleCodepage becomes its UTF-8; UTF-8 that is in the
  line already (a name as the server gave it) stays as it is. }
function ConsoleText(const S: string): string;
var
  i, n, Need, k: Integer;
  B: Byte;
  Valid, Plain: Boolean;
begin
  Plain := True;
  for i := 1 to Length(S) do
    if Byte(S[i]) >= $80 then
    begin
      Plain := False;
      Break;
    end;
  if Plain then Exit(S);
  Result := '';
  n := Length(S);
  i := 1;
  while i <= n do
  begin
    B := Byte(S[i]);
    if B < $80 then
    begin
      Result := Result + S[i];
      Inc(i);
      Continue;
    end;
    if (B and $E0) = $C0 then Need := 1
    else if (B and $F0) = $E0 then Need := 2
    else if (B and $F8) = $F0 then Need := 3
    else Need := 0;
    Valid := (Need > 0) and (i + Need <= n);
    if Valid then
      for k := 1 to Need do
        if (Byte(S[i + k]) and $C0) <> $80 then Valid := False;
    if Valid then
    begin
      Result := Result + Copy(S, i, Need + 1);
      Inc(i, Need + 1);
    end
    else
    begin
      Result := Result + CodepageToUtf8(S[i], ConsoleCodepage);
      Inc(i);
    end;
  end;
end;

{ The bytes of a text of fpjson (UTF-8) in a plain string, and of an answer in a UTF-8 string,
  as they are. An assignment between strings of two code pages converts, and a letter the code
  page of Windows (or of Linux without a locale) does not have became "?" on the way. }
function Utf8Bytes(const U: UTF8String): string;
begin
  Result := '';
  SetLength(Result, Length(U));
  if Length(U) > 0 then Move(U[1], Result[1], Length(U));
end;

function AsUtf8(const S: RawByteString): UTF8String;
begin
  Result := '';
  SetLength(Result, Length(S));
  if Length(S) > 0 then Move(S[1], Result[1], Length(S));
end;

function Utf8ToConsole(const S: string): string;
var
  i, Len, CP, Extra, Start: Integer;
  B: Byte;
  Broken: Boolean;
  Out, Ch: string;
begin
  Out := '';
  i := 1;
  Len := Length(S);
  while i <= Len do
  begin
    Start := i;
    B := Byte(S[i]);
    CP := -1;
    Extra := 0;
    if B < $80 then
      CP := B
    else if (B and $E0) = $C0 then begin CP := B and $1F; Extra := 1; end
    else if (B and $F0) = $E0 then begin CP := B and $0F; Extra := 2; end
    else if (B and $F8) = $F0 then begin CP := B and $07; Extra := 3; end;
    if (CP < 0) or (i + Extra > Len) then
    begin
      { not UTF-8 at all: the byte is already what the console wants }
      Out := Out + S[i];
      Inc(i);
      Continue;
    end;
    Broken := False;
    while Extra > 0 do
    begin
      Inc(i);
      B := Byte(S[i]);
      if (B and $C0) <> $80 then
      begin
        Broken := True;
        Break;
      end;
      CP := (CP shl 6) or (B and $3F);
      Dec(Extra);
    end;
    if Broken then
    begin
      { it only looked like UTF-8: the first byte is already what the console wants }
      Out := Out + S[Start];
      i := Start + 1;
      Continue;
    end;
    Inc(i);
    { a letter the code page of the console has keeps its own byte (with 1250 the Polish ones;
      ConsoleText makes it UTF-8 again on the way out) }
    Ch := '';
    if (CP >= $80) and (CP <> $A0) and not Settings.GeoTextPlain then
    begin
      Ch := Utf8ToCodepage(Copy(S, Start, i - Start), ConsoleCodepage);
      if (Length(Ch) <> 1) or (Ch = '?') then Ch := '';
    end;
    if CP = $A0 then
      { a no-break space is a space, in both kinds of text }
      Out := Out + ' '
    else if Ch <> '' then
      Out := Out + Ch
    else if (Settings.GeoTextPlain or (ConsoleCodepage <> 1252)) and (CP >= $80) and (CP <= $17F) then
    begin
      { only plain letters wanted: accents are dropped. The signs above U+017F take the
        common way below, which already writes them in plain text. }
      case CP of
        $C0..$C5: Out := Out + 'A';
        $C6: Out := Out + 'AE';
        $C7: Out := Out + 'C';
        $C8..$CB: Out := Out + 'E';
        $CC..$CF: Out := Out + 'I';
        $D0: Out := Out + 'D';
        $D1: Out := Out + 'N';
        $D2..$D6, $D8: Out := Out + 'O';
        $D9..$DC: Out := Out + 'U';
        $DD: Out := Out + 'Y';
        $DE: Out := Out + 'Th';
        $DF: Out := Out + 'ss';
        $E0..$E5: Out := Out + 'a';
        $E6: Out := Out + 'ae';
        $E7: Out := Out + 'c';
        $E8..$EB: Out := Out + 'e';
        $EC..$EF: Out := Out + 'i';
        $F0: Out := Out + 'd';
        $F1: Out := Out + 'n';
        $F2..$F6, $F8: Out := Out + 'o';
        $F9..$FC: Out := Out + 'u';
        $FD, $FF: Out := Out + 'y';
        $FE: Out := Out + 'th';
        $100..$17F: Out := Out + LATIN_A_PLAIN[CP - $100 + 1];
      else
        Out := Out + '?';
      end;
    end
    else if CP < $100 then
      Out := Out + Chr(CP)
    else if CP <= $17F then
    begin
      case CP of
        $152: Out := Out + Chr($8C);   { OE }
        $153: Out := Out + Chr($9C);   { oe }
        $160: Out := Out + Chr($8A);   { S caron }
        $161: Out := Out + Chr($9A);
        $178: Out := Out + Chr($9F);   { Y diaeresis }
        $17D: Out := Out + Chr($8E);   { Z caron }
        $17E: Out := Out + Chr($9E);
      else
        Out := Out + LATIN_A_PLAIN[CP - $100 + 1];
      end;
    end
    else
      case CP of
        $2018, $2019: Out := Out + '''';
        $201C, $201D: Out := Out + '"';
        $2013, $2014: Out := Out + '-';
        $2022: Out := Out + '*';
        $2026: Out := Out + '...';
        $20AC:
          if Settings.GeoTextPlain then Out := Out + 'EUR' else Out := Out + Chr($80);
      else
        Out := Out + '?';
      end;
  end;
  Result := Out;
end;

{ The answer of the lookup comes over plain HTTP, and the country goes out to every player in
  "joined from": a line break or another control character in it (JSON lets \n and \r through as such) is a space here, in the tables of the admins and in the database. }
procedure SetGeoString(var Buffer: array of Char; const Value: string);
var
  Len, k: Integer;
  V: string;
begin
  FillChar(Buffer, Length(Buffer), 0);
  V := Utf8ToConsole(Value);
  for k := 1 to Length(V) do
    if Ord(V[k]) < 32 then V[k] := ' ';
  Len := Length(V);
  if Len >= Length(Buffer) then Len := Length(Buffer) - 1;
  if Len > 0 then Move(V[1], Buffer, Len);
end;

{ Addresses ip-api.com cannot locate: loopback, private, link-local, shared and multicast ranges }
function IsLocalAddress(const IP: string): Boolean;
var
  S: string;
  p, A, B: Integer;
begin
  Result := True;
  if (IP = '') or SameText(IP, 'localhost') or (Pos(':', IP) > 0) then Exit;
  S := IP;
  p := Pos('.', S);
  if p = 0 then Exit;
  A := StrToIntDef(Copy(S, 1, p - 1), -1);
  Delete(S, 1, p);
  p := Pos('.', S);
  if p = 0 then Exit;
  B := StrToIntDef(Copy(S, 1, p - 1), -1);
  Result := (A < 1) or (A = 10) or (A = 127) or (A >= 224) or ((A = 192) and (B = 168)) or
    ((A = 172) and (B >= 16) and (B <= 31)) or ((A = 169) and (B = 254)) or ((A = 100) and (B >= 64) and (B <= 127));
end;

{ StartGeoLookup (af_geo.inc) starts a lookup unless the address is local, a lookup for it is
  running, it failed a short while ago or enough lookups are running already. Before, every call
  started a new thread, and the script asks 60 times a second until the address is located: for
  addresses that cannot be located that was up to 1800 threads and web requests per join, which
  could exhaust the memory of the 32-bit server and crash it. }

{ one line for a slot that is not waiting for a command: how something that ran in the
  background ended. Slot 0 is every admin in the game and the console. Any thread may call it;
  the script prints it on its next tick. }
procedure Notice(Slot, Color: Integer; const Text: string);
begin
  if NoticeList = nil then Exit;
  EnterCriticalSection(NoticeCS);
  try
    if NoticeList.Count < 200 then
      NoticeList.Add(IntToStr(Slot) + '|' + IntToStr(Color) + '|' + Text);
  finally LeaveCriticalSection(NoticeCS); end;
end;

{ ---------------- Engine = mysql: what goes to the shared database ----------------

  The shared database gets what changed here since the last exchange (af_shared.inc), so the
  state of a profile before its first change is kept until the change is sent. A profile nobody
  changed here keeps nothing. }

{ room in the lists of the shared database for profile Index; the caller holds ProfilesCritSec }
procedure SharedGrow(Index: Integer);
var
  n, k: Integer;
begin
  n := Length(SharedGlobal);
  if Index < n then Exit;
  SetLength(SharedGlobal, Index + 256);
  SetLength(SharedBase, Index + 256);
  SetLength(SharedVersion, Index + 256);
  SetLength(SharedFlight, Index + 256);
  SetLength(SharedDirtyArr, Index + 256);
  for k := n to High(SharedGlobal) do
  begin
    SharedGlobal[k] := 0;
    SharedVersion[k] := 0;
    SharedFlight[k] := 0;
    SharedDirtyArr[k] := 0;
  end;
end;

{ one profile as bytes, in the layout of PackProfile (the number in front is 0) }
function SharedPackData(const P: TProfileData): AnsiString;
var
  Buf: TMemoryStream;
  n, k: Integer;
  Head: TBinaryProfile;

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
  Buf := TMemoryStream.Create;
  try
    n := 0;
    Buf.Write(n, SizeOf(n));
    Head := P.Profile;
    Head.OtherNamesCount := Length(P.OtherNames);
    Head.OtherSteamsCount := Length(P.OtherSteams);
    Head.OtherHWIDsCount := Length(P.OtherHWIDs);
    Head.OtherIPsCount := Length(P.OtherIPs);
    Buf.Write(Head, SizeOf(TBinaryProfile));
    PutList(P.OtherNames);
    PutList(P.OtherSteams);
    PutList(P.OtherHWIDs);
    PutList(P.OtherIPs);
    n := Length(P.Visits);
    Buf.Write(n, SizeOf(n));
    for k := 0 to n - 1 do Buf.Write(P.Visits[k], SizeOf(TVisitEntry));
    Buf.Write(P.Steam, SizeOf(TSteamInfo));
    SetLength(Result, Buf.Size);
    if Buf.Size > 0 then Move(Buf.Memory^, Result[1], Buf.Size);
  finally
    Buf.Free;
  end;
end;

{ profile Index is about to change; the caller holds ProfilesCritSec }
procedure SharedTouch(Index: Integer);
begin
  if not SharedOn then Exit;
  if (Index < 0) or (Index >= ProfileCount) then Exit;
  if Index >= Length(SharedGlobal) then SharedGrow(Index);
  if SharedBase[Index] = '' then SharedBase[Index] := SharedPackData(Profiles[Index]);
end;

{ the same in two steps, for a pass over every profile that changes only a few of them: the
  state before, and - when something changed - kept }
function SharedBefore(Index: Integer): AnsiString;
begin
  Result := '';
  if not SharedOn then Exit;
  if (Index < 0) or (Index >= ProfileCount) then Exit;
  if (Index < Length(SharedBase)) and (SharedBase[Index] <> '') then Exit;
  Result := SharedPackData(Profiles[Index]);
end;

procedure SharedKeep(Index: Integer; const Before: AnsiString);
begin
  if (Before = '') or not SharedOn then Exit;
  if (Index < 0) or (Index >= ProfileCount) then Exit;
  if Index >= Length(SharedGlobal) then SharedGrow(Index);
  if SharedBase[Index] = '' then SharedBase[Index] := Before;
end;

{ a decision of an admin, for the next exchange; the caller holds ProfilesCritSec }
procedure SharedNoteVerdict(Lo, Hi, Verdict: Integer; Decided: Double; const By: string);
begin
  if not SharedOn or SharedQuiet then Exit;
  if SharedVerdictCount >= MAX_VERDICTS * 2 then Exit;
  if SharedVerdictCount >= Length(SharedVerdictChanges) then
    SetLength(SharedVerdictChanges, SharedVerdictCount * 2 + 16);
  FillChar(SharedVerdictChanges[SharedVerdictCount], SizeOf(TPairVerdict), 0);
  SharedVerdictChanges[SharedVerdictCount].A := Lo;
  SharedVerdictChanges[SharedVerdictCount].B := Hi;
  SharedVerdictChanges[SharedVerdictCount].Verdict := Verdict;
  SharedVerdictChanges[SharedVerdictCount].Decided := Decided;
  SetGeoString(SharedVerdictChanges[SharedVerdictCount].By, By);
  Inc(SharedVerdictCount);
end;

{ a profile changed: the journal writes it at its next round. The caller holds ProfilesCritSec. }
procedure JournalMark(Index: Integer);
begin
  if (Index < 0) or (Index >= ProfileCount) then Exit;
  if Index >= Length(Dirty) then SetLength(Dirty, Index + 100);
  if Dirty[Index] = 0 then
  begin
    Dirty[Index] := 1;
    Inc(DirtyCount);
  end;
end;

{ the same, and with Engine = mysql it goes to the shared database at the next exchange }
procedure MarkDirty(Index: Integer);
begin
  if (Index < 0) or (Index >= ProfileCount) then Exit;
  JournalMark(Index);
  if SharedOn then
  begin
    if Index >= Length(SharedGlobal) then SharedGrow(Index);
    SharedDirtyArr[Index] := 1;
  end;
end;

{ the player of a slot right now, as a number that changes when they leave }
function SessionOf(Slot: Integer): Integer;
begin
  Result := 0;
  if (Slot >= 0) and (Slot <= MAX_SLOTS) then Result := SlotSession[Slot];
end;

{ a notice for the player who asked; dropped when that player has left the slot since. The
  console (the last slot) has no players and always hears it. }
procedure NoticeFor(Slot, Session, Color: Integer; const Text: string);
begin
  if (Slot < 1) or (Slot > MAX_SLOTS) then Exit;
  if (Slot < MAX_SLOTS) and (SessionOf(Slot) <> Session) then Exit;
  Notice(Slot, Color, Text);
end;

{ a player left: what was waiting for them is not for the next one }
procedure DropNotices(Slot: Integer);
var
  k: Integer;
  Prefix: string;
begin
  if NoticeList = nil then Exit;
  Prefix := IntToStr(Slot) + '|';
  EnterCriticalSection(NoticeCS);
  try
    for k := NoticeList.Count - 1 downto 0 do
      if Copy(NoticeList[k], 1, Length(Prefix)) = Prefix then NoticeList.Delete(k);
  finally LeaveCriticalSection(NoticeCS); end;
end;

{ ---------------- data on the disk, not only in the system's cache ---------------- }

{$IFDEF WINDOWS}
function AfFlushFileBuffers(Handle: THandle): LongBool; stdcall; external 'kernel32.dll' name 'FlushFileBuffers';
{$ENDIF}

{ puts what was written through H on the disk before it returns. A file that takes another's
  place by a rename must be whole on the disk first: the rename itself is written at once, and a
  power cut right after it would leave the new name on blocks that were never written. }
function SyncFileHandle(H: THandle): Boolean;
begin
  {$IFDEF WINDOWS}
  Result := AfFlushFileBuffers(H);
  {$ELSE}
  Result := fpfsync(H) = 0;
  {$ENDIF}
end;

{ ---------------- a clock for short waits ---------------- }

{$IFDEF WINDOWS}
function QueryPerformanceCounter(out Count: Int64): LongBool; stdcall; external 'kernel32.dll';
function QueryPerformanceFrequency(out Freq: Int64): LongBool; stdcall; external 'kernel32.dll';

var
  ClockFreq: Int64 = 0;
{$ENDIF}

{ microseconds from an arbitrary start, never jumping. On Windows GetTickCount64 and Sleep(1)
  move in steps of 15.6 ms, too coarse for waits of a few milliseconds. }
function MicroClock: Int64;
{$IFDEF WINDOWS}
var
  C, F: Int64;
begin
  F := ClockFreq;
  if F <= 0 then
  begin
    if not QueryPerformanceFrequency(F) or (F <= 0) then F := 1000;
    ClockFreq := F;
  end;
  if not QueryPerformanceCounter(C) then
    C := Int64(GetTickCount64) * (F div 1000);
  Result := (C div F) * 1000000 + ((C mod F) * 1000000) div F;
end;
{$ELSE}
begin
  Result := Int64(GetTickCount64) * 1000;
end;
{$ENDIF}

{ The game thread must never stop. Anything it calls takes the lock only if it is free right
  now, tries again for a few milliseconds, and gives up: a visit is worth less than a frozen
  server (the play time of an account is not given up, it waits - QueuePlayTime). The clock
  decides how long: on Windows Sleep(1) lasts a whole timer step of 15.6 ms, so counting them
  made "5 ms" 78 ms. Short waits only yield; longer ones (on the workers) rest a little between
  the tries. }
function GrabLock(var CS: TRTLCriticalSection; Millis: Integer): Boolean;
var
  Start, Deadline: Int64;
begin
  Result := TryEnterCriticalSection(CS) <> 0;
  if Result or (Millis <= 0) then Exit;
  Start := MicroClock;
  Deadline := Start + Int64(Millis) * 1000;
  repeat
    if (Millis > 20) and (MicroClock - Start > 5000) then
      Sleep(1)
    else
      Sleep(0);
    Result := TryEnterCriticalSection(CS) <> 0;
  until Result or (MicroClock >= Deadline);
end;

{ ---------------- play time that could not wait ----------------

  Add_Profile_Time runs on the game thread every minute for every player, and a worker may hold
  the profiles longer than the game thread may wait (a full write, a long search). The seconds
  are not dropped then: they wait here, one line per account, and whoever takes the profiles
  next - the game thread or any worker, the journal and the full write among them - adds them
  first. }
type
  TPendingTime = record
    Index: Integer;
    Played, Spectator: Int64;
  end;

const
  PENDING_TIME_MAX = 64;

var
  PendingTimeCS: TRTLCriticalSection;
  PendingTime: array[0..PENDING_TIME_MAX - 1] of TPendingTime;
  PendingTimeCount: LongInt = 0;

{ the seconds of one account, into the account; the caller holds ProfilesCritSec }
procedure ApplyPlayTime(Index: Integer; Played, Spectator: Int64);
begin
  if (Index < 0) or (Index >= ProfileCount) then Exit;
  SharedTouch(Index);
  Inc(Profiles[Index].Profile.TimePlayed, Played);
  Inc(Profiles[Index].Profile.TimeOnSpectator, Spectator);
  Profiles[Index].Profile.LastPlayedTime := Now;
  MarkDirty(Index);
end;

{ keeps the seconds of one account for the next one who takes the profiles; False only when
  64 accounts are waiting already }
function QueuePlayTime(Index: Integer; Played, Spectator: Int64): Boolean;
var
  k: Integer;
begin
  Result := False;
  EnterCriticalSection(PendingTimeCS);
  try
    for k := 0 to PendingTimeCount - 1 do
      if PendingTime[k].Index = Index then
      begin
        Inc(PendingTime[k].Played, Played);
        Inc(PendingTime[k].Spectator, Spectator);
        Result := True;
        Exit;
      end;
    if PendingTimeCount >= PENDING_TIME_MAX then Exit;
    PendingTime[PendingTimeCount].Index := Index;
    PendingTime[PendingTimeCount].Played := Played;
    PendingTime[PendingTimeCount].Spectator := Spectator;
    Inc(PendingTimeCount);
    Result := True;
  finally LeaveCriticalSection(PendingTimeCS); end;
end;

{ what waited, into the accounts; the caller has just taken ProfilesCritSec. The count is read
  without the lock: a line that comes a moment later goes with the next taker. }
procedure DrainPlayTime;
var
  k, n: Integer;
  Taken: array[0..PENDING_TIME_MAX - 1] of TPendingTime;
begin
  if PendingTimeCount <= 0 then Exit;
  EnterCriticalSection(PendingTimeCS);
  try
    n := PendingTimeCount;
    for k := 0 to n - 1 do Taken[k] := PendingTime[k];
    PendingTimeCount := 0;
  finally LeaveCriticalSection(PendingTimeCS); end;
  for k := 0 to n - 1 do ApplyPlayTime(Taken[k].Index, Taken[k].Played, Taken[k].Spectator);
end;

{ ---------------- the profiles: the game thread goes first ----------------

  A critical section is not fair. A worker that walks the profiles gives the lock back after
  every profile and takes it again at once; the game thread, woken up for its turn, finds it
  taken again and goes back to sleep, and a join waited hundreds of milliseconds for a /multi or
  a full write. So while the game thread waits (GameWants), a worker that is about to take the
  lock lets it go first - at most 20 ms, so nothing can hang on it. A worker that holds the lock
  already (ProfilesDepth) never waits: the game thread could be waiting for exactly that. }

var
  GameThread: TThreadID = TThreadID(0);   { the server's thread: the script calls from it }
  GameWants: LongInt = 0;

threadvar
  ProfilesDepth: LongInt;

procedure WaitForGameTurn(Millis: Integer);
var
  Deadline: Int64;
begin
  if (GameWants <= 0) or (ProfilesDepth > 0) then Exit;
  Deadline := MicroClock + Int64(Millis) * 1000;
  while (GameWants > 0) and (MicroClock < Deadline) do ThreadSwitch;
end;

{ the play time that waited goes in as the lock is taken; nothing in it may leave the lock
  taken behind an exception, since the caller's "finally" has not begun yet }
procedure TakenProfiles;
begin
  Inc(ProfilesDepth);
  if PendingTimeCount > 0 then
    try
      DrainPlayTime;
    except
    end;
end;

procedure LockProfiles;
begin
  if GetCurrentThreadId = GameThread then
  begin
    InterLockedIncrement(GameWants);
    EnterCriticalSection(ProfilesCritSec);
    InterLockedDecrement(GameWants);
  end
  else
  begin
    WaitForGameTurn(20);
    EnterCriticalSection(ProfilesCritSec);
  end;
  TakenProfiles;
end;

procedure UnlockProfiles;
begin
  Dec(ProfilesDepth);
  LeaveCriticalSection(ProfilesCritSec);
end;

function GrabProfiles(Millis: Integer): Boolean;
begin
  if GetCurrentThreadId = GameThread then
  begin
    InterLockedIncrement(GameWants);
    Result := GrabLock(ProfilesCritSec, Millis);
    InterLockedDecrement(GameWants);
  end
  else
  begin
    WaitForGameTurn(Millis);
    Result := GrabLock(ProfilesCritSec, Millis);
  end;
  if Result then TakenProfiles;
end;

{ ---------------- when a file was written ---------------- }

{ The time of the last write in UTC. The local time of a file depends on the summer time of
  today, not of the day it was written, so after the clocks change an older file looks newer
  by an hour (or the other way round); UTC never moves. }
function FileTimeUtc(const Path: string; out T: TDateTime): Boolean;
{$IFDEF WINDOWS}
var
  SR: TSearchRec;
  Q: QWord;
begin
  Result := False;
  T := 0;
  if FindFirst(Path, faAnyFile, SR) <> 0 then Exit;
  try
    Q := (QWord(SR.FindData.ftLastWriteTime.dwHighDateTime) shl 32) or
         QWord(SR.FindData.ftLastWriteTime.dwLowDateTime);
    T := EncodeDate(1601, 1, 1) + Q / 10000000.0 / 86400.0;
    Result := True;
  finally
    FindClose(SR);
  end;
end;
{$ELSE}
var
  St: BaseUnix.Stat;
begin
  T := 0;
  Result := BaseUnix.FpStat(PChar(Path), St) = 0;
  if Result then T := UnixToDateTime(St.st_mtime);
end;
{$ENDIF}

function NowUtc: TDateTime;
begin
  Result := LocalTimeToUniversal(Now);
end;

{ ---------------- threads in the background ----------------

  A thread that frees itself (FreeOnTerminate) still runs its last lines after it has said it
  is done, and the library may not be unloaded under them. So every background thread is
  kept here: the game thread frees the finished ones as it goes (SweepBackground), and Cleanup
  waits for the rest (WaitBackground). }

procedure StartBackground(T: TThread);
begin
  T.FreeOnTerminate := False;
  EnterCriticalSection(BgCS);
  try
    BgThreads.Add(T);
  finally LeaveCriticalSection(BgCS); end;
  try
    T.Start;
  except
    EnterCriticalSection(BgCS);
    try
      BgThreads.Remove(T);
    finally LeaveCriticalSection(BgCS); end;
    T.Free;
    raise;
  end;
end;

{$IFDEF WINDOWS}
function AfWaitForObject(Handle: THandle; Millis: LongWord): LongWord; stdcall;
  external 'kernel32.dll' name 'WaitForSingleObject';
{$ENDIF}

{ True when the thread has finished, or is not there any more. When a process ends, Windows
  ends every other thread before it unloads the library, and such a thread never says it has
  finished: every wait of the unload for it ran to its full length (40 seconds for the
  background alone). Its handle says it has ended, and waiting for it then returns at once. }
function ThreadGone(T: TThread): Boolean;
begin
  Result := T.Finished;
  {$IFDEF WINDOWS}
  if not Result then Result := AfWaitForObject(T.Handle, 0) = 0;
  {$ENDIF}
end;

{ frees the threads that have finished, at most MaxFree of them (the game thread frees a few a
  tick); they are past their work, so waiting for the rest of them takes microseconds }
procedure SweepBackground(MaxFree: Integer);
var
  i: Integer;
  Done: TList;
begin
  if BgThreads = nil then Exit;
  Done := nil;
  if TryEnterCriticalSection(BgCS) = 0 then Exit;
  try
    for i := BgThreads.Count - 1 downto 0 do
      if ThreadGone(TThread(BgThreads[i])) then
      begin
        if (Done <> nil) and (Done.Count >= MaxFree) then Break;
        if Done = nil then Done := TList.Create;
        Done.Add(BgThreads[i]);
        BgThreads.Delete(i);
      end;
  finally LeaveCriticalSection(BgCS); end;
  if Done = nil then Exit;
  try
    for i := 0 to Done.Count - 1 do
    begin
      TThread(Done[i]).WaitFor;
      TThread(Done[i]).Free;
    end;
  finally Done.Free; end;
end;

{ The first line of every worker thread. On Linux the server takes its own signal handlers back
  once the library is loaded, so a floating point error on a worker would end the whole server
  instead of raising an exception the worker catches; there the worker gets NaN and infinity
  instead. Windows keeps its exceptions. }
procedure GuardWorkerThread;
begin
  {$IFDEF UNIX}
  SetExceptionMask([exInvalidOp, exDenormalized, exZeroDivide, exOverflow, exUnderflow, exPrecision]);
  {$ENDIF}
end;

{$IFDEF UNIX}
{ A write to a connection the other side has closed (MySQL, a web request, the mail pipe) raises
  SIGPIPE on Linux, and a SIGPIPE nobody catches ends the whole server. It is ignored instead,
  unless the server installed a handler of its own; the write then simply fails. }
procedure IgnoreBrokenPipes;
var
  Cur, Act: SigActionRec;
begin
  FillChar(Cur, SizeOf(Cur), 0);
  if FPSigaction(SIGPIPE, nil, @Cur) <> 0 then Exit;
  if PPointer(@Cur.sa_handler)^ <> nil then Exit;
  FillChar(Act, SizeOf(Act), 0);
  PPointer(@Act.sa_handler)^ := Pointer(1);   { SIG_IGN }
  FPSigaction(SIGPIPE, @Act, nil);
end;
{$ENDIF}

{ waits up to Millis for every background thread and frees it; the number still running }
function WaitBackground(Millis: Integer): Integer;
var
  Deadline: QWord;
begin
  Result := 0;
  if BgThreads = nil then Exit;
  Deadline := GetTickCount64 + QWord(Millis);
  repeat
    SweepBackground(MaxInt);
    EnterCriticalSection(BgCS);
    try
      Result := BgThreads.Count;
    finally LeaveCriticalSection(BgCS); end;
    if Result = 0 then Exit;
    Sleep(10);
  until GetTickCount64 >= Deadline;
end;

{ How well a name answers what was typed; lower is closer, -1 = not at all. The kind of match
  comes first - the whole name (0), its beginning (1), the beginning of a word in it (2), any
  part of it (3) - then the length: "u" finds "up" before "hardkorowy koksu". A nick of one
  letter is found by that letter. The same rule as ZitroStats. Low is already lower case. }
function NameRank(const Name, Low: string): Integer;
var
  i, m, Kind: Integer;
  L: string;
begin
  Result := -1;
  m := Length(Low);
  if m = 0 then Exit;
  L := LowerCase(Name);
  if Pos(Low, L) = 0 then Exit;
  if L = Low then Kind := 0
  else if Copy(L, 1, m) = Low then Kind := 1
  else
  begin
    Kind := 3;
    for i := 2 to Length(L) - m + 1 do
      if not (L[i - 1] in ['a'..'z', '0'..'9']) then
        if Copy(L, i, m) = Low then
        begin
          Kind := 2;
          Break;
        end;
  end;
  Result := Kind * 1000 + Min(999, Length(L) - m);
end;

{ ---------------- logs/antifake ----------------
  What AntiFake did, one dated line each, next to the logs of ZitroStats: antifake.log holds the
  events (the database read and written, copies, corrections, new players, admin commands) and
  trace.log the steps of Trace = 1. The lines wait in a queue and a short-lived thread appends
  them, so the game thread never waits for the disk. }

const
  LOG_DIR = 'logs' + DirectorySeparator + 'antifake';

var
  LogCS: TRTLCriticalSection;
  LogQueue: TStringList = nil;     { "file<TAB>line" }
  LogWriting: Boolean = False;     { a thread is writing, or about to }

type
  TLogThread = class(TThread)
  protected
    procedure Execute; override;
  end;

procedure WriteLogLines(Lines: TStringList);
var
  i, k, t: Integer;
  Name, Path, Text: string;
  Done: TStringList;
  F: TFileStream;
begin
  Done := TStringList.Create;
  try
    for i := 0 to Lines.Count - 1 do
    begin
      t := Pos(#9, Lines[i]);
      Name := Copy(Lines[i], 1, t - 1);
      if Done.IndexOf(Name) >= 0 then Continue;
      Done.Add(Name);
      { every line of this file, in the order they came }
      Text := '';
      for k := i to Lines.Count - 1 do
        if Copy(Lines[k], 1, t) = Name + #9 then
          Text := Text + Copy(Lines[k], t + 1, MaxInt) + LineEnding;
      try
        ForceDirectories(LOG_DIR);
        Path := LOG_DIR + DirectorySeparator + Name;
        if FileExists(Path) then
        begin
          F := TFileStream.Create(Path, fmOpenWrite or fmShareDenyWrite);
          F.Seek(0, soEnd);
        end
        else F := TFileStream.Create(Path, fmCreate);
        try
          F.WriteBuffer(Text[1], Length(Text));
        finally F.Free; end;
      except
        { a log that cannot be written is not worth stopping anything for }
      end;
    end;
  finally Done.Free; end;
end;

procedure TLogThread.Execute;
var
  Batch: TStringList;
begin
  GuardWorkerThread;
  Batch := TStringList.Create;
  try
    repeat
      Sleep(200);     { what comes close together goes out in one write }
      EnterCriticalSection(LogCS);
      try
        if (LogQueue = nil) or (LogQueue.Count = 0) then
        begin
          LogWriting := False;
          Break;
        end;
        Batch.Assign(LogQueue);
        LogQueue.Clear;
      finally LeaveCriticalSection(LogCS); end;
      WriteLogLines(Batch);
      Batch.Clear;
    until False;
  finally Batch.Free; end;
end;

{ A log line holds no control characters: names and addresses of players come into it, and an
  escape sequence of a terminal (ESC [ ...) in one of them moved the cursor and painted over the
  lines before it for whoever read the log with cat or tail. The line end is added by the writer. }
function LogSafe(const Line: string): string;
var
  k: Integer;
begin
  Result := Line;
  for k := 1 to Length(Result) do
    if Ord(Result[k]) < 32 then Result[k] := '?';
end;

procedure LogTo(const FileName, Line: string);
var
  Start: Boolean;
begin
  Start := False;
  EnterCriticalSection(LogCS);
  try
    if LogQueue = nil then LogQueue := TStringList.Create;
    { a runaway loop fills a little memory, never the disk }
    if LogQueue.Count < 10000 then
      LogQueue.Add(FileName + #9 + FormatDateTime('yyyy-mm-dd hh:nn:ss', Now) + '  ' + LogSafe(Line));
    if not LogWriting and not ShuttingDown then
    begin
      LogWriting := True;
      Start := True;
    end;
  finally LeaveCriticalSection(LogCS); end;
  if Start then
    try
      StartBackground(TLogThread.Create(True));
    except
      EnterCriticalSection(LogCS);
      try
        LogWriting := False;
      finally LeaveCriticalSection(LogCS); end;
    end;
end;

procedure LogEvent(const Line: string);
begin
  LogTo('antifake.log', Line);
end;

var
  ExportErrors: LongInt = 0;

{ An export that failed. Nothing may leave the library towards the script as an exception: the
  script gets an empty answer, and the log says where - the first twenty times, then every
  thousandth, so a call that fails on every tick cannot fill the disk. Called inside an except. }
procedure ExportFailed(const Where: string);
var
  n: LongInt;
begin
  n := InterLockedIncrement(ExportErrors);
  if (n > 20) and (n mod 1000 <> 0) then Exit;
  try
    if ExceptObject is Exception then
      LogEvent('error ' + IntToStr(n) + ' in ' + Where + ': ' + Exception(ExceptObject).Message)
    else
      LogEvent('error ' + IntToStr(n) + ' in ' + Where);
  except
    { not even the log: the answer stays empty all the same }
  end;
end;

{ what is still queued, written by the caller: at the very end, when no thread starts any more }
procedure LogFlush;
var
  Batch: TStringList;
begin
  Batch := TStringList.Create;
  try
    EnterCriticalSection(LogCS);
    try
      if LogQueue <> nil then
      begin
        Batch.Assign(LogQueue);
        LogQueue.Clear;
      end;
    finally LeaveCriticalSection(LogCS); end;
    if Batch.Count > 0 then WriteLogLines(Batch);
  finally Batch.Free; end;
end;
