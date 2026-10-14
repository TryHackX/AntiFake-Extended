{ ---------------------------------------------------------------------------------------
  AntiFake Extended - the file, the journal, the copies that are kept and putting one back.
  --------------------------------------------------------------------------------------- }

{ ---------------- copies that are kept ---------------- }

{ A plain copy, in pieces, so a big database does not want a big block of memory. A write that
  fails (a full disk) raises instead of returning 0: the copy is then False and the piece of it
  is deleted - a short copy used to count as made, and the rotation threw an older good one
  away for it. The copy is on the disk when this returns. }
function CopyFileTo(const Source, Dest: string): Boolean;
var
  Src, Dst: TFileStream;
  Buf: array[0..65535] of Byte;
  n: Integer;
  Made: Boolean;
begin
  Result := False;
  Made := False;
  try
    Src := TFileStream.Create(Source, fmOpenRead or fmShareDenyNone);
    try
      Dst := TFileStream.Create(Dest, fmCreate);
      Made := True;
      try
        repeat
          n := Src.Read(Buf, SizeOf(Buf));
          if n > 0 then Dst.WriteBuffer(Buf, n);
        until n <= 0;
        SyncFileHandle(Dst.Handle);
      finally Dst.Free; end;
    finally Src.Free; end;
    Result := True;
  except
    Result := False;
  end;
  if Made and not Result then
    try DeleteFile(Dest); except end;
end;

{ ---------------- one file in place of another ---------------- }

{$IFDEF WINDOWS}
function AfMoveFileEx(Src, Dst: PWideChar; Flags: LongWord): LongBool; stdcall;
  external 'kernel32.dll' name 'MoveFileExW';
function AfCreateHardLink(NewName, Existing: PWideChar; Security: Pointer): LongBool; stdcall;
  external 'kernel32.dll' name 'CreateHardLinkW';
{$ENDIF}

{$IFDEF UNIX}
{ a rename is an entry of the folder: on Linux it is on the disk once the folder is }
procedure SyncFolderOf(const Path: string);
var
  Dir: string;
  Fd: LongInt;
begin
  Dir := ExtractFileDir(Path);
  if Dir = '' then Dir := '.';
  Fd := FpOpen(PChar(Dir), O_RDONLY);
  if Fd >= 0 then
  begin
    fpfsync(Fd);
    FpClose(Fd);
  end;
end;
{$ENDIF}

{ Src takes the place of Dst in one step, whether Dst is there or not: there is no moment
  without either of them. A file that somebody holds open for a moment (a virus scanner, a copy
  being made) gets a few more tries. Src has to be on the disk already (SyncFileHandle). }
function ReplaceFileAtomic(const Src, Dst: string): Boolean;
var
  Round: Integer;
begin
  Result := False;
  for Round := 1 to 5 do
  begin
    {$IFDEF WINDOWS}
    { MOVEFILE_REPLACE_EXISTING or MOVEFILE_WRITE_THROUGH }
    Result := AfMoveFileEx(PWideChar(UnicodeString(Src)), PWideChar(UnicodeString(Dst)), 1 or 8);
    {$ELSE}
    Result := FpRename(PChar(Src), PChar(Dst)) = 0;
    if Result then SyncFolderOf(Dst);
    {$ENDIF}
    if Result or (Round = 5) then Exit;
    Sleep(100);
  end;
end;

{ the file that is about to be replaced stays as Bak: a second name for the same data where
  the file system has them, a copy where it does not }
function KeepAsBak(const FilePath, BakPath: string): Boolean;
begin
  if FileExists(BakPath) then DeleteFile(BakPath);
  {$IFDEF WINDOWS}
  Result := AfCreateHardLink(PWideChar(UnicodeString(BakPath)), PWideChar(UnicodeString(FilePath)), nil);
  {$ELSE}
  Result := FpLink(PChar(FilePath), PChar(BakPath)) = 0;
  {$ENDIF}
  if not Result then Result := CopyFileTo(FilePath, BakPath);
end;

{ the copies go to the backup folder next to the database (nickreg/backup, where ZitroStats keeps
  its own); a database kept in the server folder itself keeps them in antifake-backups }
function BackupDirFor(const DbPath: string): string;
begin
  if ExtractFilePath(DbPath) = '' then Result := 'antifake-backups'
  else Result := ExtractFilePath(DbPath) + 'backup';
end;

{ every copy that is there, oldest first }
function ListBackupFiles(const DbPath: string): TStringList;
var
  Info: TSearchRec;
  Dir: string;
begin
  Result := TStringList.Create;
  Result.Sorted := True;      { the names carry the date, so sorted is oldest first }
  Dir := BackupDirFor(DbPath);
  if FindFirst(Dir + PathDelim + 'antifake-*.dat', faAnyFile, Info) = 0 then
  begin
    repeat
      if (Info.Attr and faDirectory) = 0 then Result.Add(Info.Name);
    until FindNext(Info) <> 0;
    FindClose(Info);
  end;
end;

{ a copy of the file that was just finished, and the oldest ones over the limit go away.
  Runs on the save thread, never on the game thread. }
procedure KeepBackup(const DbPath: string);
var
  Dir, Name: string;
  Files: TStringList;
  Newest: TDateTime;
  Info: TSearchRec;
  i: Integer;
begin
  if Settings.BackupHours <= 0 then Exit;
  Dir := BackupDirFor(DbPath);
  if not DirectoryExists(Dir) then
    if not ForceDirectories(Dir) then Exit;
  Files := ListBackupFiles(DbPath);
  try
    { not again if the newest one is younger than the interval }
    if Files.Count > 0 then
    begin
      Newest := 0;
      if FindFirst(Dir + PathDelim + Files[Files.Count - 1], faAnyFile, Info) = 0 then
      begin
        Newest := FileDateToDateTime(Info.Time);
        FindClose(Info);
      end;
      if (Newest > 0) and (Now - Newest < Settings.BackupHours / 24.0) then Exit;
    end;
    Name := Dir + PathDelim + 'antifake-' + FormatDateTime('yyyymmdd-hhnnss', Now) + '.dat';
    if not CopyFileTo(DbPath, Name) then Exit;
    LogEvent('copy of the database made: ' + Name);
    Files.Free;
    Files := ListBackupFiles(DbPath);
    for i := 0 to Files.Count - Settings.BackupKeep - 1 do
      DeleteFile(Dir + PathDelim + Files[i]);
  finally
    Files.Free;
  end;
end;

{ ---------------- the journal ---------------- }

{ the journal sits next to the database and carries its name }
function JournalNameFor(const DbPath: string): string;
begin
  Result := ChangeFileExt(DbPath, '') + '.jrn';
end;

var
  MoveNote: string = '';    { what MoveOldDatabase did, for the note after the load }

{ The database used to sit in the server folder (antifake.dat, its .bak and .jrn, the copies in
  antifake-backups and the trace log). When the settings name a folder (nickreg/antifake.dat)
  and it has no database yet, all of it moves there before the first read: moved, not copied,
  so the journal still fits its file, and nothing is left behind or deleted. Runs on the load
  thread. The note says what happened; '' = nothing to do. }
function MoveOldDatabase(const DbPath: string): string;
var
  Old, OldDir, NewDir, Name: string;
  Info: TSearchRec;
  Copies, Failed: Integer;

  procedure MoveOne(const Src, Dst: string);
  begin
    if not FileExists(Src) or FileExists(Dst) then Exit;
    if not RenameFile(Src, Dst) then Inc(Failed);
  end;

begin
  Result := '';
  if ExtractFilePath(DbPath) = '' then Exit;
  ForceDirectories(ExtractFilePath(DbPath));
  Old := ExtractFileName(DbPath);
  if SameText(ExpandFileName(Old), ExpandFileName(DbPath)) then Exit;
  if not FileExists(Old) then Exit;
  if FileExists(DbPath) or FileExists(JournalNameFor(DbPath)) then
  begin
    Result := Old + ' in the server folder is not used: the database is ' + DbPath +
              '. Move the old one away once you no longer need it.';
    Exit;
  end;
  Failed := 0;
  Copies := 0;
  if not RenameFile(Old, DbPath) then
  begin
    Result := Old + ' could not be moved to ' + DbPath + ': ' + DbPath + ' starts empty, ' +
              Old + ' stays where it is.';
    Exit;
  end;
  MoveOne(JournalNameFor(Old), JournalNameFor(DbPath));
  MoveOne(Old + '.bak', DbPath + '.bak');
  NewDir := BackupDirFor(DbPath);
  ForceDirectories(NewDir);
  OldDir := BackupDirFor(Old);
  if FindFirst(OldDir + PathDelim + 'antifake-*.dat', faAnyFile, Info) = 0 then
  begin
    repeat
      if (Info.Attr and faDirectory) = 0 then
      begin
        MoveOne(OldDir + PathDelim + Info.Name, NewDir + PathDelim + Info.Name);
        Inc(Copies);
      end;
    until FindNext(Info) <> 0;
    FindClose(Info);
  end;
  RemoveDir(OldDir);      { only when it is empty now }
  if FindFirst(Old + '.replaced-*', faAnyFile, Info) = 0 then
  begin
    repeat
      Name := Info.Name;
      MoveOne(Name, NewDir + PathDelim + Name);
    until FindNext(Info) <> 0;
    FindClose(Info);
  end;
  ForceDirectories(LOG_DIR);
  MoveOne('antifake_trace.log', LOG_DIR + DirectorySeparator + 'trace.log');
  Result := 'the database moved from the server folder to ' + DbPath;
  if Copies > 0 then Result := Result + ', its ' + IntToStr(Copies) + ' copies to ' + NewDir;
  if Failed > 0 then Result := Result + ' (' + IntToStr(Failed) + ' file(s) could not be moved and stay in the server folder)';
end;

procedure SetJournalError(const Text: string);
begin
  EnterCriticalSection(JournalCS);
  try
    JournalLastError := Text;
  finally LeaveCriticalSection(JournalCS); end;
end;

const
  { Format 2 of the journal: every record carries a checksum behind it (JournalRecordSum), so a
    damaged record ends the replay instead of being put into the accounts. A journal of format 1
    (JOURNAL_FORMAT, AntiFake 2.0) has none and is replayed as before. }
  JOURNAL_FORMAT_SUM = 2;
  { a new account of the journal comes right after the ones before it; one this far beyond
    them is a damaged record (it used to make the list that long, up to 5 million accounts) }
  JOURNAL_MAX_JUMP = 1000;

var
  { An append to the journal failed since the last full write (a full disk, a file that is
    gone): the journal misses changes. Journal_Final then says False and Cleanup writes the
    whole database. A new journal after a full write starts without it. Belongs to JournalCS. }
  JournalWriteFailed: Boolean = False;
  { an admin puts a copy in place of the database right now (Restore_Backup): no full write
    may start (StartSave). Belongs to JournalCS. }
  RestoreRunning: Boolean = False;

{ LoadedPath for a thread that did not write it: the load and the save thread replace it, and
  a string read while it is replaced may already be freed }
function CurrentDbPath: string;
begin
  EnterCriticalSection(JournalCS);
  try
    Result := LoadedPath;
  finally LeaveCriticalSection(JournalCS); end;
end;

procedure SetLoadedPath(const DbPath: string);
begin
  EnterCriticalSection(JournalCS);
  try
    LoadedPath := DbPath;
  finally LeaveCriticalSection(JournalCS); end;
end;

function JournalRecordSum(Kind: Byte; Len: LongInt; const Data: AnsiString): Cardinal;
begin
  Result := crc32(0, nil, 0);
  Result := crc32(Result, @Kind, 1);
  Result := crc32(Result, @Len, SizeOf(Len));
  if Len > 0 then Result := crc32(Result, @Data[1], Len);
end;

{ one record of the journal as it is written: kind, length, the bytes, the checksum }
function JournalRecord(Kind: Byte; const Data: AnsiString): AnsiString;
var
  Len: LongInt;
  Sum: Cardinal;
begin
  Len := Length(Data);
  Sum := JournalRecordSum(Kind, Len, Data);
  SetLength(Result, 1 + SizeOf(Len) + Len + SizeOf(Sum));
  Result[1] := AnsiChar(Kind);
  Move(Len, Result[2], SizeOf(Len));
  if Len > 0 then Move(Data[1], Result[2 + SizeOf(Len)], Len);
  Move(Sum, Result[2 + SizeOf(Len) + Len], SizeOf(Sum));
end;

{ which database this journal belongs to: its size and the moment it was written, in UTC
  (see FileTimeUtc), and the local time a journal written by an older version holds }
function SnapshotStamp(const DbPath: string; var Size: Int64; var TimeUtc, TimeLocal: Double): Boolean;
var
  Info: TSearchRec;
  T: TDateTime;
begin
  Result := False;
  Size := 0;
  TimeUtc := 0;
  TimeLocal := 0;
  if FindFirst(DbPath, faAnyFile, Info) = 0 then
  begin
    Size := Info.Size;
    TimeLocal := FileDateToDateTime(Info.Time);
    FindClose(Info);
    if FileTimeUtc(DbPath, T) then TimeUtc := T else TimeUtc := TimeLocal;
    Result := True;
  end;
end;

{ A journal that belongs to the database that is on disk now. Runs on the load and the save
  thread; a write of the journal thread may be under way, and the new journal waits for it:
  starting the file again in the middle of that write used to fail, and a journal that did
  not start is ignored at the next start. }
procedure JournalStart(const DbPath: string);
var
  F: TFileStream;
  H: TJournalHeader;
  Size: Int64;
  TimeUtc, TimeLocal: Double;
  Mine: Boolean;
  Deadline: QWord;
begin
  Mine := False;
  Deadline := GetTickCount64 + 10000;
  repeat
    EnterCriticalSection(JournalCS);
    try
      if not JournalBusy then
      begin
        JournalBusy := True;
        Mine := True;
      end;
    finally LeaveCriticalSection(JournalCS); end;
    if Mine then Break;
    Sleep(5);
  until GetTickCount64 >= Deadline;
  try
    { the paths are read by other threads (CurrentDbPath, Journal_Flush): replaced under the lock }
    SetLoadedPath(DbPath);
    if not Mine then
    begin
      { a write of the journal that holds it this long would collide with a new file; the
        journal stays off until the next full write starts it again, and the end of the server
        writes the whole database (Journal_Final finds no journal) }
      EnterCriticalSection(JournalCS);
      try
        JournalPath := '';
      finally LeaveCriticalSection(JournalCS); end;
      SetJournalError('the journal was busy for 10 s after a full write and was not started again; ' +
        'it starts with the next full write');
      Exit;
    end;
    EnterCriticalSection(JournalCS);
    try
      JournalPath := JournalNameFor(DbPath);
    finally LeaveCriticalSection(JournalCS); end;
    JournalBytes := 0;
    if not SnapshotStamp(DbPath, Size, TimeUtc, TimeLocal) then Exit;
    try
      F := TFileStream.Create(JournalNameFor(DbPath), fmCreate);
      try
        FillChar(H, SizeOf(H), 0);
        H.Magic := JOURNAL_MAGIC;
        H.Format := JOURNAL_FORMAT_SUM;
        H.SnapSize := Size;
        H.SnapTime := TimeUtc;
        H.Written := NowUtc;
        F.WriteBuffer(H, SizeOf(H));
        JournalBytes := SizeOf(H);
      finally F.Free; end;
      { everything up to here is in the file the journal belongs to }
      EnterCriticalSection(JournalCS);
      try
        JournalWriteFailed := False;
      finally LeaveCriticalSection(JournalCS); end;
    except
      on E: Exception do SetJournalError(E.Message);
    end;
  finally
    if Mine then
    begin
      EnterCriticalSection(JournalCS);
      try
        JournalBusy := False;
      finally LeaveCriticalSection(JournalCS); end;
    end;
  end;
end;

var
  { the decisions went into the journal while a full write ran: like the profiles in
    SaveFlushed they go into the new journal as well. Belongs to ProfilesCritSec. }
  SaveVerdictsFlushed: Boolean = False;

{ What changed since the last time: every marked profile, and the verdicts. A few dozen
  profiles per hold of the lock, so a join waits for a few dozen at most. Ok is False when the
  lock could not be had within GrabMs; what was packed until then is returned all the same (its
  marks are gone, so it has to be written). }
function PackJournal(GrabMs: Integer; out Ok: Boolean): AnsiString;
const
  PER_HOLD = 64;
var
  Rec: AnsiString;
  i, n, Len: Integer;
  Out: TMemoryStream;
  More: Boolean;
begin
  Result := '';
  Ok := False;
  Out := TMemoryStream.Create;
  try
    i := 0;
    repeat
      if not GrabProfiles(GrabMs) then Exit;
      try
        n := 0;
        More := False;
        while i < ProfileCount do
        begin
          if i >= Length(Dirty) then
          begin
            i := ProfileCount;
            Break;
          end;
          if Dirty[i] <> 0 then
          begin
            if n >= PER_HOLD then
            begin
              More := True;
              Break;
            end;
            Inc(n);
            Dirty[i] := 0;
            Dec(DirtyCount);
            { a full write that runs now starts the journal again when it ends, and it may have
              passed this profile already: it is written into the new journal as well }
            if SaveInProgress then
            begin
              if SaveFlushedCount >= Length(SaveFlushed) then SetLength(SaveFlushed, SaveFlushedCount * 2 + 64);
              SaveFlushed[SaveFlushedCount] := i;
              Inc(SaveFlushedCount);
            end;
            Rec := PackProfile(i);
            if Rec <> '' then
            begin
              Rec := JournalRecord(JREC_PROFILE, Rec);
              Out.Write(Rec[1], Length(Rec));
            end;
          end;
          Inc(i);
        end;
        if not More then
        begin
          if DirtyCount < 0 then DirtyCount := 0;
          if VerdictsDirty then
          begin
            VerdictsDirty := False;
            if SaveInProgress then SaveVerdictsFlushed := True;
            Len := VerdictCount * SizeOf(TPairVerdict);
            SetLength(Rec, Len);
            for i := 0 to VerdictCount - 1 do
              Move(Verdicts[i], Rec[i * SizeOf(TPairVerdict) + 1], SizeOf(TPairVerdict));
            Rec := JournalRecord(JREC_VERDICTS, Rec);
            Out.Write(Rec[1], Length(Rec));
          end;
        end;
      finally UnlockProfiles; end;
    until not More;
    Ok := True;
  finally
    SetLength(Result, Out.Size);
    if Out.Size > 0 then Move(Out.Memory^, Result[1], Out.Size);
    Out.Free;
  end;
end;

{ everything that changed since the last time, appended in one go. Runs on its own thread, and
  so does the packing (Pack): the game thread only marks profiles as changed, which is one byte,
  and starts the thread. }
type
  TJournalThread = class(TThread)
  private
    FData: AnsiString;
    FPath: string;
    FPack: Boolean;
  protected
    procedure Execute; override;
  public
    constructor Create(const AData: AnsiString; const APath: string; APack: Boolean);
  end;

constructor TJournalThread.Create(const AData: AnsiString; const APath: string; APack: Boolean);
begin
  inherited Create(True);
  FData := AData;
  FPath := APath;
  FPack := APack;
end;

procedure TJournalThread.Execute;
var
  F: TFileStream;
  Ok, Failed: Boolean;
begin
  GuardWorkerThread;
  Failed := False;
  try
    if FPack then
    begin
      FData := PackJournal(2000, Ok);
      Inc(JournalBytes, Length(FData));
    end;
    if FData <> '' then
    begin
      { what was packed has lost its marks: if it cannot be written, only a full write keeps it }
      if not FileExists(FPath) then
      begin
        Failed := True;
        SetJournalError('the journal file ' + FPath + ' is gone; the next full write starts a new one');
      end
      else
      begin
        F := TFileStream.Create(FPath, fmOpenWrite or fmShareDenyWrite);
        try
          F.Seek(0, soEnd);
          { raises when the disk takes less than all of it, where Write said nothing }
          F.WriteBuffer(FData[1], Length(FData));
        finally F.Free; end;
        Inc(JournalWrites);
      end;
    end;
  except
    on E: Exception do
    begin
      { what was packed (or half packed) lost its marks either way }
      Failed := True;
      SetJournalError(E.Message);
    end;
  end;
  EnterCriticalSection(JournalCS);
  try
    if Failed then JournalWriteFailed := True;
    JournalBusy := False;
  finally LeaveCriticalSection(JournalCS); end;
end;

{ The script calls this every few seconds. The game thread only starts the thread; the thread
  packs what changed and appends it. }
procedure Journal_Flush(); cdecl;
var
  Path: string;
begin
  try
    if ShuttingDown or LoadError then Exit;
    if JournalBytes > MAX_JOURNAL_BYTES then Exit;
    if not GrabLock(JournalCS, 5) then Exit;
    try
      { the path is taken under the lock: the load and the save thread replace it }
      Path := JournalPath;
      if (Path = '') or JournalBusy then Exit;
      { read without the lock of the profiles: a mark set a moment later goes with the next call }
      if (DirtyCount = 0) and not VerdictsDirty then Exit;
      JournalBusy := True;
    finally LeaveCriticalSection(JournalCS); end;
    try
      StartBackground(TJournalThread.Create('', Path, True));
    except
      EnterCriticalSection(JournalCS);
      try
        JournalBusy := False;
      finally LeaveCriticalSection(JournalCS); end;
    end;
  except
    ExportFailed('Journal_Flush');
  end;
end;

function JournalIsBusy: Boolean;
begin
  EnterCriticalSection(JournalCS);
  try
    Result := JournalBusy;
  finally LeaveCriticalSection(JournalCS); end;
end;

{ The script stops (a /recompile or the end of the server). True when everything that changed
  since the last full write is on its way to the journal - Cleanup waits for the journal
  thread, and the next start puts the journal on top of the file - so no full write is needed.
  False when there is no journal, it is full, an earlier append failed, or not everything could
  be taken: then the script writes the whole database. Should this last append fail, Cleanup
  sees it (JournalWriteFailed) and writes the whole database itself. }
function Journal_Final(): Boolean; cdecl;
var
  T0: QWord;
  Buf: AnsiString;
  Path: string;
  Ok, Mine, Started, Failed: Boolean;
begin
  Result := False;
  Mine := False;
  Started := False;
  try
    try
      if ShuttingDown or LoadError or (Settings.JournalSeconds <= 0) then Exit;
      if not LoadComplete or LoadInProgress then Exit;
      if JournalBytes > MAX_JOURNAL_BYTES then Exit;
      { a journal thread that runs goes first }
      T0 := GetTickCount64;
      repeat
        EnterCriticalSection(JournalCS);
        try
          Path := JournalPath;
          Failed := JournalWriteFailed;
          if (Path <> '') and not Failed and not JournalBusy then
          begin
            JournalBusy := True;
            Mine := True;
          end;
        finally LeaveCriticalSection(JournalCS); end;
        if Mine or (Path = '') or Failed then Break;
        Sleep(1);
      until GetTickCount64 - T0 >= 3000;
      if not Mine then Exit;
      Buf := PackJournal(500, Ok);
      Inc(JournalBytes, Length(Buf));
      StartBackground(TJournalThread.Create(Buf, Path, False));
      Started := True;
      if not Ok then Exit;
      if not GrabProfiles(500) then Exit;
      try
        Result := (DirtyCount = 0) and not VerdictsDirty;
      finally UnlockProfiles; end;
    finally
      { the journal is free again when no thread took it over }
      if Mine and not Started then
      begin
        EnterCriticalSection(JournalCS);
        try
          JournalBusy := False;
        finally LeaveCriticalSection(JournalCS); end;
      end;
    end;
  except
    ExportFailed('Journal_Final');
    Result := False;
  end;
end;

{ After the database is read, whatever the journal holds is put on top of it: those are the
  changes that happened after the last full write, which a server killed at the wrong moment
  would otherwise lose. A journal that belongs to another database is ignored, and a torn
  record ends the replay - everything before it still counts. A record of the decisions holds
  all of them and takes the place of those the file had (FreshVerdicts, VerdictN): they used to
  go straight into memory, where the ones of the file were put over them again. }
function JournalReplay(const DbPath: string; var Fresh: TProfileArray;
  var FreshCount: Integer; var FreshVerdicts: TVerdictArray; var VerdictN: Integer): Integer;
var
  F: TFileStream;
  H: TJournalHeader;
  Size: Int64;
  TimeUtc, TimeLocal: Double;
  Kind: Byte;
  Len, Idx, i: Integer;
  Data: AnsiString;
  P: TProfileData;
  Path: string;
  HasSum: Boolean;
  Sum: Cardinal;
begin
  Result := 0;
  Path := JournalNameFor(DbPath);
  if not FileExists(Path) then Exit;
  if not SnapshotStamp(DbPath, Size, TimeUtc, TimeLocal) then Exit;
  try
    F := TFileStream.Create(Path, fmOpenRead or fmShareDenyNone);
    try
      if F.Size < SizeOf(H) then Exit;
      F.Read(H, SizeOf(H));
      if (H.Magic <> JOURNAL_MAGIC) or
         ((H.Format <> JOURNAL_FORMAT) and (H.Format <> JOURNAL_FORMAT_SUM)) then Exit;
      HasSum := H.Format = JOURNAL_FORMAT_SUM;
      { a journal written for another copy of the database says nothing about this one. The
        time is in UTC; a journal of an older version holds the local time instead. }
      if H.SnapSize <> Size then Exit;
      if (Abs(H.SnapTime - TimeUtc) > 3.0 / 86400.0) and (Abs(H.SnapTime - TimeLocal) > 3.0 / 86400.0) then Exit;
      while F.Position + 1 + SizeOf(Len) <= F.Size do
      begin
        F.Read(Kind, 1);
        F.Read(Len, SizeOf(Len));
        if (Len < 0) or (F.Position + Len + Ord(HasSum) * SizeOf(Sum) > F.Size) then Break;
        SetLength(Data, Len);
        if Len > 0 then
          if F.Read(Data[1], Len) <> Len then Break;
        if HasSum then
        begin
          if (F.Read(Sum, SizeOf(Sum)) <> SizeOf(Sum)) then Break;
          if Sum <> JournalRecordSum(Kind, Len, Data) then
          begin
            SetJournalError('a record of the journal is damaged (its checksum is wrong); ' +
              'what came after it was not replayed');
            Break;
          end;
        end;
        if Kind = JREC_PROFILE then
        begin
          if not UnpackProfile(Data, Idx, P) then Break;
          if Idx > FreshCount + JOURNAL_MAX_JUMP then
          begin
            SetJournalError('a record of the journal names account #' + IntToStr(Idx) + ', far beyond the ' +
              IntToStr(FreshCount) + ' there are; it and what came after it were not replayed');
            Break;
          end;
          if Idx >= 0 then
          begin
            { a profile that was made after the last full write is not in the file at all,
              so the list grows to make room for it }
            if Idx >= Length(Fresh) then SetLength(Fresh, Idx + 1);
            if Idx >= FreshCount then FreshCount := Idx + 1;
            Fresh[Idx] := P;
            Inc(Result);
          end;
        end
        else if Kind = JREC_VERDICTS then
        begin
          i := Len div SizeOf(TPairVerdict);
          if (i >= 0) and (i <= MAX_VERDICTS) then
          begin
            SetLength(FreshVerdicts, i);
            VerdictN := i;
            for Idx := 0 to i - 1 do
              Move(Data[Idx * SizeOf(TPairVerdict) + 1], FreshVerdicts[Idx], SizeOf(TPairVerdict));
            Inc(Result);
          end;
        end;
      end;
    finally F.Free; end;
  except
    on E: Exception do SetJournalError(E.Message);
  end;
end;

{ what the journal has been doing }
function Get_Journal_Status(): PChar; cdecl;
var
  Err: string;
begin
  if Settings.JournalSeconds <= 0 then
    TempStringResult := 'off (JournalSeconds = 0)'
  else
  begin
    TempStringResult := Format('on, every %d s, %d writes, %d KB, %d records replayed at start',
      [Settings.JournalSeconds, JournalWrites, JournalBytes div 1024, JournalReplayed]);
    EnterCriticalSection(JournalCS);
    try
      Err := JournalLastError;
    finally LeaveCriticalSection(JournalCS); end;
    if Err <> '' then
      TempStringResult := TempStringResult + ' | last problem: ' + Err;
  end;
  Result := PChar(TempStringResult);
end;

{ the copies that are kept, newest last, one per line }
function Get_Backup_List(): PChar; cdecl;
var
  Files: TStringList;
  i: Integer;
  R: string;
begin
  R := '';
  try
    Files := ListBackupFiles(CurrentDbPath);
    try
      for i := 0 to Files.Count - 1 do
      begin
        if R <> '' then R := R + '|';
        R := R + Files[i];
      end;
    finally Files.Free; end;
  except
    ExportFailed('Get_Backup_List');
  end;
  TempStringResult := R;
  Result := PChar(TempStringResult);
end;

{ an admin picks one of the copies: it is put in place of the database and the server has to
  be restarted to read it. The file that was there is never deleted, only moved aside - and its
  journal with it (it used to be deleted, and the changes it held since the last full write of
  that file were lost). The copy is made under a name of its own first, so a copy that fails
  half way never leaves the server without its file. Only a copy of the list (ListBackupFiles):
  the folder is shared with ZitroStats, whose files are no database of AntiFake. Not while a
  full write or an append to the journal runs, and none of them starts until it is done
  (RestoreRunning, the journal taken): they would put the memory over the copy a moment later. }
function Restore_Backup(Name: PChar): Boolean; cdecl;
var
  Db, Wanted, Src, Aside, Tmp: string;
  Files: TStringList;
  k: Integer;
  Mine: Boolean;
begin
  Result := False;
  Mine := False;
  try
    Db := CurrentDbPath;
    if Db = '' then Exit;
    Wanted := ExtractFileName(string(Name));
    if Wanted = '' then Exit;
    Files := ListBackupFiles(Db);
    try
      k := Files.IndexOf(Wanted);
      if k < 0 then Exit;
      Wanted := Files[k];
    finally Files.Free; end;
    Src := BackupDirFor(Db) + PathDelim + Wanted;
    if not FileExists(Src) then Exit;
    EnterCriticalSection(JournalCS);
    try
      if not RestoreRunning and not JournalBusy and not SaveInProgress then
      begin
        RestoreRunning := True;
        JournalBusy := True;
        Mine := True;
      end;
    finally LeaveCriticalSection(JournalCS); end;
    if not Mine then Exit;
    try
      Tmp := Db + '.restoring';
      Aside := Db + '.replaced-' + FormatDateTime('yyyymmdd-hhnnss', Now);
      if not CopyFileTo(Src, Tmp) then Exit;
      if FileExists(Db) then
        if not RenameFile(Db, Aside) then Exit;
      Result := ReplaceFileAtomic(Tmp, Db);
      if not Result then
      begin
        { the one that was there goes back }
        if FileExists(Aside) and not FileExists(Db) then RenameFile(Aside, Db);
        Exit;
      end;
      if FileExists(JournalNameFor(Db)) then RenameFile(JournalNameFor(Db), Aside + '.jrn');
      { from now on nothing is written over the copy: set before a full write may start again }
      LoadError := True;
    finally
      EnterCriticalSection(JournalCS);
      try
        RestoreRunning := False;
        JournalBusy := False;
      finally LeaveCriticalSection(JournalCS); end;
    end;
  except
    Result := False;
  end;
end;

{$I af_mysql.inc}
{$I af_shared.inc}
