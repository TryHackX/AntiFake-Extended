{ ---------------------------------------------------------------------------------------
  AntiFake Extended - the thread that does the searching, and reading the database back.
  --------------------------------------------------------------------------------------- }

constructor TSearchThread.Create(const ATask: TSearchTask);
begin
  inherited Create(True);
  FreeOnTerminate := False;
  FTask := ATask;
end;

procedure TSearchThread.Execute;
var
  StartTime: TDateTime;
  i, j, k, c, d: Integer;
  TempResults: array of TSearchResultItem;
  TempCount, MaxIdx: Integer;
  Found: Boolean;
  CheckStr, MatchedStr: string;
  FoundProfiles, TargetNames, TargetSteams, TargetHWIDs, TargetIPs: TStringList;
  KeyLists, KeyIndexes: array[0..2] of TStringList;
  KeyPos, KeyEnd, KeyCap: Integer;
  TaskIsValid: Boolean;
  HasMatch, HasNameMatch: Boolean;
  TempNameCount: Integer;
  CurrentScore, BestScore: Integer;

  CityList, CountryList, CountryCodeList, RegionList, OtherList, ZipList, TimezoneList, IspList, ContinentList, ContinentCodeList, ReverseList: TStringList;
  TempList: TStringList;
  Token, Key, Val: string;
  PosSep: Integer;
  MatchCity, MatchCountry, MatchCountryCode, MatchRegion, MatchOther, MatchZip, MatchTimezone, MatchIsp, MatchMobile, MatchProxy, MatchHosting, MatchContinent, MatchContinentCode, MatchReverse: Boolean;
  ReqMobile, ReqProxy, ReqHosting: Integer;
  GeoIdx, IdxOfLoc, RiskOfMatch: Integer;
  FromT, ToT: Double;
  CurrentIP, LocStr: string;
  UniqueIPs, LocCounts, TargetGeoSigs: TStringList;
  SigLevel: Integer;
  NowT, FromDate: Double;
  PlayTime: Int64;
  TopLoc: string;
  TopKeys: array of string;
  TopOwners, TopGroup: array of Integer;
  TopN, TopG: Integer;
  MultiLinks: Integer;
  MultiName, MultiMatch: string;
  MultiNames: array of string;
  MultiNamed: array of Boolean;
  MultiRow: TSearchResultItem;
  ForeignRows: TForeignRows;
  ForeignWhy: string;

  { The list of results grows in steps. One record at a time meant a new block and a copy of
    everything found so far on every hit - the cost of a big search went up with the square of
    the number of results. }
  procedure Room;
  begin
    if TempCount >= Length(TempResults) then
      SetLength(TempResults, Length(TempResults) * 2 + 64);
  end;

  function GetRisk(GIdx: Integer): Integer;
  begin
    Result := 0;
    if GIdx < 0 then Exit;
    if GeoCache[GIdx].IsHosting = 1 then Result := 3
    else if GeoCache[GIdx].IsProxy = 1 then Result := 2
    else if GeoCache[GIdx].IsMobile = 1 then Result := 1;
  end;

  function BuildGeoSig(GIdx, Level: Integer): string;
  begin
     Result := '';
     if GIdx < 0 then Exit;
     if Level >= 1 then Result := string(GeoCache[GIdx].Continent);
     if Level >= 2 then Result := Result + '|' + string(GeoCache[GIdx].Country);
     if Level >= 3 then Result := Result + '|' + string(GeoCache[GIdx].Region);
     if Level >= 4 then Result := Result + '|' + string(GeoCache[GIdx].City);
     Result := LowerCase(Result);
  end;

  function ParseBoolFlag(V: string): Integer;
  begin
    V := LowerCase(V);
    if (V = 'true') or (V = '1') or (V = 'yes') then Result := 1
    else if (V = 'false') or (V = '0') or (V = 'no') then Result := 0
    else Result := -1;
  end;

begin
  GuardWorkerThread;
  StartTime := Now;
  NowT := StartTime;
  TempCount := 0;
  TempNameCount := 0;
  FromT := 0;
  ToT := 0;
  TopLoc := '';
  MultiNames := nil;
  MultiNamed := nil;
  SetLength(TempResults, 0);

  try
      { inside the try: whatever it meets, the finally below marks the search as done }
      ParseVisitRange(FTask.SearchPhrase, FromT, ToT);
      LockProfiles;
      try
        MaxIdx := ProfileCount - 1;
      finally
        UnlockProfiles;
      end;

      case FTask.TaskType of
        TASK_SEARCH:
          begin
            for i := MaxIdx downto 0 do begin
              if Terminated then Break;
              LockProfiles;
              try
                Found := ContainsText(GetProfileActiveName(i), FTask.SearchPhrase);
                if Found then begin
                  Room;
                  TempResults[TempCount].ProfileIndex := i;
                  TempResults[TempCount].DataString := GetProfileActiveName(i);
                  TempResults[TempCount].ActiveName := GetProfileActiveName(i);
                  TempResults[TempCount].NamesCount := Length(Profiles[i].OtherNames);
                  TempResults[TempCount].SteamsCount := Length(Profiles[i].OtherSteams);
                  TempResults[TempCount].HWIDsCount := Length(Profiles[i].OtherHWIDs);
                  TempResults[TempCount].IPsCount := Length(Profiles[i].OtherIPs);
                  TempResults[TempCount].LastUsedDate := Profiles[i].Profile.LastPlayedTime;
                  TempResults[TempCount].FirstUsedDate := Profiles[i].Profile.RegisterTime;
                  TempResults[TempCount].TimePlayed := Profiles[i].Profile.TimePlayed;
                  TempResults[TempCount].Similarity := NameRank(TempResults[TempCount].DataString,
                    LowerCase(Trim(FTask.SearchPhrase)));
                  Inc(TempCount);
                end;
              finally UnlockProfiles; end;
              if (FTask.MaxResults > 0) and (TempCount >= FTask.MaxResults) then Break;
              if (i mod OPTIMIZE_COUNT = 0) then Sleep(1);
            end;
            { the closest names on top: "a" first for a, "up" before "hardkorowy koksu" for u }
            if TempCount > 1 then QuickSortByRank(TempResults, 0, TempCount - 1);
          end;
        TASK_SEARCHID:
          begin
            for i := MaxIdx downto 0 do begin
              if Terminated then Break;
              LockProfiles;
              try
                Found := False;
                MatchedStr := '';
                if FTask.SearchAll then begin
                  for j := 0 to Length(Profiles[i].OtherNames) - 1 do
                  begin
                    CheckStr := GetOtherDataString(Profiles[i].OtherNames[j]);
                    if ContainsText(CheckStr, FTask.SearchPhrase) then
                    begin
                      Found := True;
                      MatchedStr := CheckStr;
                      Break;
                    end;
                  end;
                end else begin
                   MatchedStr := GetProfileActiveName(i);
                   Found := ContainsText(MatchedStr, FTask.SearchPhrase);
                end;
                if Found then begin
                  Room;
                  TempResults[TempCount].ProfileIndex := i;
                  TempResults[TempCount].DataString := MatchedStr;
                  TempResults[TempCount].ActiveName := GetProfileActiveName(i);
                  TempResults[TempCount].NamesCount := Length(Profiles[i].OtherNames);
                  TempResults[TempCount].SteamsCount := Length(Profiles[i].OtherSteams);
                  TempResults[TempCount].HWIDsCount := Length(Profiles[i].OtherHWIDs);
                  TempResults[TempCount].IPsCount := Length(Profiles[i].OtherIPs);
                  TempResults[TempCount].LastUsedDate := Profiles[i].Profile.LastPlayedTime;
                  TempResults[TempCount].TimePlayed := Profiles[i].Profile.TimePlayed;
                  TempResults[TempCount].Similarity := NameRank(MatchedStr, LowerCase(Trim(FTask.SearchPhrase)));
                  Inc(TempCount);
                end;
              finally UnlockProfiles; end;
              if (FTask.MaxResults > 0) and (TempCount >= FTask.MaxResults) then Break;
              if (i mod OPTIMIZE_COUNT = 0) then Sleep(1);
            end;
            if TempCount > 1 then QuickSortByRank(TempResults, 0, TempCount - 1);
          end;
        TASK_PRIVACY, TASK_PRIVACYID:
          begin
            LockProfiles;
            try
              if (FTask.TargetIndex >= 0) and (FTask.TargetIndex < ProfileCount) then begin
                case FTask.Kind of
                  KIND_NAME: for j := 0 to Length(Profiles[FTask.TargetIndex].OtherNames) - 1 do begin
                    if (FTask.TaskType = TASK_PRIVACYID) or EntryIsActive(Profiles[FTask.TargetIndex].OtherNames[j], NowT) then begin
                      Room;
                      TempResults[TempCount].ProfileIndex := FTask.TargetIndex;
                      TempResults[TempCount].SubIndex := j;
                      TempResults[TempCount].DataString := GetOtherDataString(Profiles[FTask.TargetIndex].OtherNames[j]);
                      TempResults[TempCount].LoginCount := Profiles[FTask.TargetIndex].OtherNames[j].LoginCount;
                      TempResults[TempCount].TimePlayed := Profiles[FTask.TargetIndex].OtherNames[j].TimePlayed;
                      TempResults[TempCount].FirstUsedDate := Profiles[FTask.TargetIndex].OtherNames[j].FirstUsedDate;
                      TempResults[TempCount].LastUsedDate := Profiles[FTask.TargetIndex].OtherNames[j].LastUsedDate;
                      Inc(TempCount);
                    end;
                  end;
                  KIND_STEAM: for j := 0 to Length(Profiles[FTask.TargetIndex].OtherSteams) - 1 do begin
                    if (FTask.TaskType = TASK_PRIVACYID) or EntryIsActive(Profiles[FTask.TargetIndex].OtherSteams[j], NowT) then begin
                      Room;
                      TempResults[TempCount].ProfileIndex := FTask.TargetIndex;
                      TempResults[TempCount].SubIndex := j;
                      TempResults[TempCount].DataString := GetOtherDataString(Profiles[FTask.TargetIndex].OtherSteams[j]);
                      TempResults[TempCount].LoginCount := Profiles[FTask.TargetIndex].OtherSteams[j].LoginCount;
                      TempResults[TempCount].TimePlayed := Profiles[FTask.TargetIndex].OtherSteams[j].TimePlayed;
                      TempResults[TempCount].FirstUsedDate := Profiles[FTask.TargetIndex].OtherSteams[j].FirstUsedDate;
                      TempResults[TempCount].LastUsedDate := Profiles[FTask.TargetIndex].OtherSteams[j].LastUsedDate;
                      Inc(TempCount);
                    end;
                  end;
                  KIND_HWID: for j := 0 to Length(Profiles[FTask.TargetIndex].OtherHWIDs) - 1 do begin
                    if (FTask.TaskType = TASK_PRIVACYID) or EntryIsActive(Profiles[FTask.TargetIndex].OtherHWIDs[j], NowT) then begin
                      Room;
                      TempResults[TempCount].ProfileIndex := FTask.TargetIndex;
                      TempResults[TempCount].SubIndex := j;
                      TempResults[TempCount].DataString := GetOtherDataString(Profiles[FTask.TargetIndex].OtherHWIDs[j]);
                      TempResults[TempCount].LoginCount := Profiles[FTask.TargetIndex].OtherHWIDs[j].LoginCount;
                      TempResults[TempCount].TimePlayed := Profiles[FTask.TargetIndex].OtherHWIDs[j].TimePlayed;
                      TempResults[TempCount].FirstUsedDate := Profiles[FTask.TargetIndex].OtherHWIDs[j].FirstUsedDate;
                      TempResults[TempCount].LastUsedDate := Profiles[FTask.TargetIndex].OtherHWIDs[j].LastUsedDate;
                      Inc(TempCount);
                    end;
                  end;
                  KIND_IP: for j := 0 to Length(Profiles[FTask.TargetIndex].OtherIPs) - 1 do begin
                    if (FTask.TaskType = TASK_PRIVACYID) or EntryIsActive(Profiles[FTask.TargetIndex].OtherIPs[j], NowT) then begin
                      Room;
                      TempResults[TempCount].ProfileIndex := FTask.TargetIndex;
                      TempResults[TempCount].SubIndex := j;

                      CheckStr := GetOtherDataString(Profiles[FTask.TargetIndex].OtherIPs[j]);
                      TempResults[TempCount].DataString := CheckStr;

                      EnterCriticalSection(GeoCritSec);
                      try
                         c := FindGeoIndex(CheckStr);
                         TempResults[TempCount].RiskLevel := GetRisk(c);
                      finally LeaveCriticalSection(GeoCritSec); end;

                      TempResults[TempCount].LoginCount := Profiles[FTask.TargetIndex].OtherIPs[j].LoginCount;
                      TempResults[TempCount].TimePlayed := Profiles[FTask.TargetIndex].OtherIPs[j].TimePlayed;
                      TempResults[TempCount].FirstUsedDate := Profiles[FTask.TargetIndex].OtherIPs[j].FirstUsedDate;
                      TempResults[TempCount].LastUsedDate := Profiles[FTask.TargetIndex].OtherIPs[j].LastUsedDate;
                      Inc(TempCount);
                    end;
                  end;
                end;
              end;
            finally
              UnlockProfiles;
            end;
            if TempCount > 1 then QuickSortResults(TempResults, 0, TempCount - 1, FTask.SortMethod);
          end;
        TASK_CHECKP:
          begin
            TaskIsValid := False;
            LockProfiles;
            try
              if (FTask.TargetIndex >= 0) and (FTask.TargetIndex < ProfileCount) then TaskIsValid := True;
            finally
              UnlockProfiles;
            end;

            if TaskIsValid then begin
                FoundProfiles := TStringList.Create; FoundProfiles.Sorted := True; FoundProfiles.Duplicates := dupIgnore;
                TargetGeoSigs := TStringList.Create; TargetGeoSigs.Sorted := True; TargetGeoSigs.Duplicates := dupIgnore;
                try
                    if (FTask.Kind = KIND_GEO_MATCH) or (FTask.Kind = KIND_ALL_LOC) then begin
                       SigLevel := FTask.SortMethod;
                       if SigLevel < 1 then SigLevel := 1;
                       if SigLevel > 4 then SigLevel := 4;

                       LockProfiles;
                       try
                          for j := 0 to Length(Profiles[FTask.TargetIndex].OtherIPs) - 1 do begin
                             CheckStr := GetOtherDataString(Profiles[FTask.TargetIndex].OtherIPs[j]);
                             EnterCriticalSection(GeoCritSec);
                             try
                                c := FindGeoIndex(CheckStr);
                                if c <> -1 then begin
                                   LocStr := BuildGeoSig(c, SigLevel);
                                   if LocStr <> '' then TargetGeoSigs.Add(LocStr);
                                end;
                             finally LeaveCriticalSection(GeoCritSec); end;
                          end;
                       finally UnlockProfiles; end;

                       if TargetGeoSigs.Count > 0 then begin
                          for i := 0 to MaxIdx do begin
                             if (i = FTask.TargetIndex) or Terminated then Continue;

                             LockProfiles;
                             try
                                Found := False;
                                MatchedStr := '';

                                for j := 0 to Length(Profiles[i].OtherIPs) - 1 do begin
                                   CheckStr := GetOtherDataString(Profiles[i].OtherIPs[j]);
                                   EnterCriticalSection(GeoCritSec);
                                   try
                                      c := FindGeoIndex(CheckStr);
                                      if c <> -1 then begin
                                         LocStr := BuildGeoSig(c, SigLevel);
                                         if (LocStr <> '') and (TargetGeoSigs.IndexOf(LocStr) >= 0) then begin
                                            Found := True;
                                            MatchedStr := string(GeoCache[c].Country);
                                            if SigLevel >= 3 then MatchedStr := MatchedStr + ', ' + string(GeoCache[c].Region);
                                            if SigLevel >= 4 then MatchedStr := MatchedStr + ', ' + string(GeoCache[c].City);
                                            Break;
                                         end;
                                      end;
                                   finally LeaveCriticalSection(GeoCritSec); end;
                                end;

                                if Found and (FoundProfiles.IndexOf(IntToStr(i)) < 0) then begin
                                   FoundProfiles.Add(IntToStr(i));
                                   Room;
                                   TempResults[TempCount].ProfileIndex := i;
                                   TempResults[TempCount].ActiveName := GetProfileActiveName(i);
                                   TempResults[TempCount].DataString := '[GEO] ' + MatchedStr;
                                   TempResults[TempCount].MatchType := KIND_GEO_MATCH;
                                   TempResults[TempCount].TimePlayed := Profiles[i].Profile.TimePlayed;
                                   Inc(TempCount);
                                end;
                             finally UnlockProfiles; end;

                             if (FTask.MaxResults > 0) and (TempCount >= FTask.MaxResults) then Break;
                             if (i mod OPTIMIZE_COUNT = 0) then Sleep(1);
                          end;
                       end;
                    end;

                    if (FTask.Kind <> KIND_GEO_MATCH) then begin
                      if (FTask.Kind = KIND_NAME) or (FTask.Kind = 4) or (FTask.Kind = KIND_ALL_LOC) then begin
                         j := 0;
                         while True do begin
                           if Terminated then Break;
                           CheckStr := '';
                           LockProfiles;
                           try
                             if j >= Length(Profiles[FTask.TargetIndex].OtherNames) then CheckStr := ''
                             else begin
                                if (not FTask.FilterFirst or (Profiles[FTask.TargetIndex].OtherNames[j].FirstUsed = 1)) and
                                   (not FTask.FilterLast or (Profiles[FTask.TargetIndex].OtherNames[j].LastUsed = 1)) then
                                   CheckStr := GetOtherDataString(Profiles[FTask.TargetIndex].OtherNames[j]);
                             end;
                           finally UnlockProfiles; end;

                           if CheckStr = '' then begin
                             LockProfiles;
                             try if j >= Length(Profiles[FTask.TargetIndex].OtherNames) then Break; finally UnlockProfiles; end;
                             Inc(j); Continue;
                           end;

                           for c := 0 to MaxIdx do begin
                              if (c = FTask.TargetIndex) or Terminated then Continue;
                              if (FoundProfiles.IndexOf(IntToStr(c)) >= 0) then Continue;

                              LockProfiles;
                              try
                                Found := False;
                                for d := 0 to Length(Profiles[c].OtherNames) - 1 do begin
                                  MatchedStr := GetOtherDataString(Profiles[c].OtherNames[d]);
                                  if FTask.ComparePartial then Found := ContainsText(MatchedStr, CheckStr)
                                  else Found := MatchedStr = CheckStr;
                                  if Found then Break;
                                end;

                                { the row says what matched and which value it was: the view
                                  used to say NAME for every row and repeat the name after it }
                                if Found then begin
                                  FoundProfiles.Add(IntToStr(c)); Room;
                                  TempResults[TempCount].ProfileIndex := c;
                                  TempResults[TempCount].DataString := MatchedStr;
                                  TempResults[TempCount].MatchType := KIND_NAME;
                                  TempResults[TempCount].ActiveName := GetProfileActiveName(c);
                                  TempResults[TempCount].TimePlayed := Profiles[c].Profile.TimePlayed;
                                  Inc(TempCount);
                                end;
                              finally UnlockProfiles; end;

                              if (FTask.MaxResults > 0) and (TempCount >= FTask.MaxResults) then Break;
                              if (c mod OPTIMIZE_COUNT = 0) then Sleep(1);
                           end;
                           Inc(j);
                           if (FTask.MaxResults > 0) and (TempCount >= FTask.MaxResults) then Break;
                         end;
                      end;

                      if ((FTask.Kind = KIND_STEAM) or (FTask.Kind = 4) or (FTask.Kind = KIND_ALL_LOC)) and ((FTask.MaxResults = 0) or (TempCount < FTask.MaxResults)) then begin
                         j := 0;
                         while True do begin
                           if Terminated then Break;
                           CheckStr := '';
                           LockProfiles;
                           try
                             if j >= Length(Profiles[FTask.TargetIndex].OtherSteams) then Break;
                             CheckStr := GetOtherDataString(Profiles[FTask.TargetIndex].OtherSteams[j]);
                           finally UnlockProfiles; end;

                           if CheckStr <> '' then begin
                              for c := 0 to MaxIdx do begin
                                if (c = FTask.TargetIndex) or Terminated then Continue;
                                if (FoundProfiles.IndexOf(IntToStr(c)) >= 0) then Continue;

                                LockProfiles;
                                try
                                  Found := False;
                                  for d := 0 to Length(Profiles[c].OtherSteams) - 1 do
                                    if (GetOtherDataString(Profiles[c].OtherSteams[d]) = CheckStr) then begin Found := True; Break; end;
                                  if Found then begin
                                    FoundProfiles.Add(IntToStr(c)); Room;
                                    TempResults[TempCount].ProfileIndex := c;
                                    TempResults[TempCount].DataString := CheckStr;
                                    TempResults[TempCount].MatchType := KIND_STEAM;
                                    TempResults[TempCount].ActiveName := GetProfileActiveName(c);
                                    TempResults[TempCount].TimePlayed := Profiles[c].Profile.TimePlayed;
                                    Inc(TempCount);
                                  end;
                                finally UnlockProfiles; end;
                                if (FTask.MaxResults > 0) and (TempCount >= FTask.MaxResults) then Break;
                                { the loop over the profiles already gives the game
                                  thread its turn; sleeping in here as well cost
                                  milliseconds for every profile looked at }
                              end;
                           end;
                           Inc(j);
                           if (FTask.MaxResults > 0) and (TempCount >= FTask.MaxResults) then Break;
                         end;
                      end;

                      if ((FTask.Kind = KIND_HWID) or (FTask.Kind = 4) or (FTask.Kind = KIND_ALL_LOC)) and ((FTask.MaxResults = 0) or (TempCount < FTask.MaxResults)) then begin
                         j := 0;
                         while True do begin
                           if Terminated then Break;
                           CheckStr := '';
                           LockProfiles;
                           try
                             if j >= Length(Profiles[FTask.TargetIndex].OtherHWIDs) then Break;
                             CheckStr := GetOtherDataString(Profiles[FTask.TargetIndex].OtherHWIDs[j]);
                           finally UnlockProfiles; end;
                           if CheckStr <> '' then begin
                              for c := 0 to MaxIdx do begin
                                if (c = FTask.TargetIndex) or Terminated then Continue;
                                if (FoundProfiles.IndexOf(IntToStr(c)) >= 0) then Continue;

                                LockProfiles;
                                try
                                  Found := False;
                                  for d := 0 to Length(Profiles[c].OtherHWIDs) - 1 do
                                    if (GetOtherDataString(Profiles[c].OtherHWIDs[d]) = CheckStr) then begin Found := True; Break; end;
                                  if Found then begin
                                    FoundProfiles.Add(IntToStr(c)); Room;
                                    TempResults[TempCount].ProfileIndex := c;
                                    TempResults[TempCount].DataString := CheckStr;
                                    TempResults[TempCount].MatchType := KIND_HWID;
                                    TempResults[TempCount].ActiveName := GetProfileActiveName(c);
                                    TempResults[TempCount].TimePlayed := Profiles[c].Profile.TimePlayed;
                                    Inc(TempCount);
                                  end;
                                finally UnlockProfiles; end;
                                if (FTask.MaxResults > 0) and (TempCount >= FTask.MaxResults) then Break;
                                { the loop over the profiles already gives the game
                                  thread its turn; sleeping in here as well cost
                                  milliseconds for every profile looked at }
                              end;
                           end;
                           Inc(j);
                           if (FTask.MaxResults > 0) and (TempCount >= FTask.MaxResults) then Break;
                         end;
                      end;

                      if ((FTask.Kind = KIND_IP) or (FTask.Kind = 4) or (FTask.Kind = KIND_ALL_LOC)) and ((FTask.MaxResults = 0) or (TempCount < FTask.MaxResults)) then begin
                         j := 0;
                         while True do begin
                           if Terminated then Break;
                           CheckStr := '';
                           LockProfiles;
                           try
                             if j >= Length(Profiles[FTask.TargetIndex].OtherIPs) then Break;
                             CheckStr := GetOtherDataString(Profiles[FTask.TargetIndex].OtherIPs[j]);
                           finally UnlockProfiles; end;
                           if CheckStr <> '' then begin
                              for c := 0 to MaxIdx do begin
                                if (c = FTask.TargetIndex) or Terminated then Continue;
                                if (FoundProfiles.IndexOf(IntToStr(c)) >= 0) then Continue;

                                LockProfiles;
                                try
                                  Found := False;
                                  for d := 0 to Length(Profiles[c].OtherIPs) - 1 do begin
                                    MatchedStr := GetOtherDataString(Profiles[c].OtherIPs[d]);
                                    if FTask.ComparePartial then Found := ContainsText(MatchedStr, CheckStr)
                                    else Found := MatchedStr = CheckStr;
                                    if Found then Break;
                                  end;
                                  if Found then begin
                                    FoundProfiles.Add(IntToStr(c)); Room;
                                    TempResults[TempCount].ProfileIndex := c;
                                    TempResults[TempCount].DataString := MatchedStr;
                                    TempResults[TempCount].MatchType := KIND_IP;
                                    TempResults[TempCount].ActiveName := GetProfileActiveName(c);
                                    TempResults[TempCount].TimePlayed := Profiles[c].Profile.TimePlayed;
                                    Inc(TempCount);
                                  end;
                                finally UnlockProfiles; end;
                                if (FTask.MaxResults > 0) and (TempCount >= FTask.MaxResults) then Break;
                                { the loop over the profiles already gives the game
                                  thread its turn; sleeping in here as well cost
                                  milliseconds for every profile looked at }
                              end;
                           end;
                           Inc(j);
                           if (FTask.MaxResults > 0) and (TempCount >= FTask.MaxResults) then Break;
                         end;
                      end;
                    end;
                finally FoundProfiles.Free; TargetGeoSigs.Free; end;
            end;
          end;
        TASK_VISITS_OF:
          begin
            { who comes from one country: the other way round from the country table.
              "PL" names the country straight away, a longer word is looked for in the names
              the location service gave us, and both end up as a short list of codes. }
            CountryCodeList := TStringList.Create;
            CountryCodeList.Sorted := True;
            CountryCodeList.Duplicates := dupIgnore;
            try
              Token := Trim(FTask.SearchPhrase);
              { the range may follow the country, so only the first word is the country }
              d := Pos(' ', Token);
              if d > 0 then Token := Copy(Token, 1, d - 1);
              if Length(Token) = 2 then
                CountryCodeList.Add(UpperCase(Token))
              else
              begin
                EnterCriticalSection(GeoCritSec);
                try
                  for i := 0 to GeoCount - 1 do
                    if ContainsText(string(GeoCache[i].Country), Token) then
                      CountryCodeList.Add(UpperCase(string(GeoCache[i].CountryCode)));
                finally LeaveCriticalSection(GeoCritSec); end;
              end;
              if CountryCodeList.Count > 0 then
                for i := 0 to MaxIdx do
                begin
                  if Terminated then Break;
                  LockProfiles;
                  try
                    if ProfileIsEmpty(i) then Continue;
                    c := 0;          { visits from there }
                    d := 0;          { seconds }
                    FromDate := 0;   { the last one }
                    for j := 0 to High(Profiles[i].Visits) do
                    begin
                      if not VisitWanted(Profiles[i].Visits[j], FromT, ToT,
                                         FTask.FilterFirst, FTask.FilterLast) then Continue;
                      LocStr := UpperCase(string(Profiles[i].Visits[j].CountryCode));
                      if LocStr = '' then LocStr := '??';
                      if CountryCodeList.IndexOf(LocStr) < 0 then Continue;
                      Inc(c);
                      Inc(d, Profiles[i].Visits[j].Seconds);
                      if Profiles[i].Visits[j].Started > FromDate then
                        FromDate := Profiles[i].Visits[j].Started;
                    end;
                    if c = 0 then Continue;
                    Room;
                    TempResults[TempCount].ProfileIndex := i;
                    TempResults[TempCount].SubIndex := i;
                    TempResults[TempCount].ActiveName := GetProfileActiveName(i);
                    TempResults[TempCount].DataString := GetProfileActiveName(i);
                    TempResults[TempCount].Location := CountryCodeList[0];
                    TempResults[TempCount].LoginCount := c;
                    TempResults[TempCount].TimePlayed := d;
                    TempResults[TempCount].LastUsedDate := FromDate;
                    TempResults[TempCount].FirstUsedDate := Profiles[i].Profile.RegisterTime;
                    Inc(TempCount);
                  finally UnlockProfiles; end;
                  if (i mod OPTIMIZE_COUNT = 0) then Sleep(1);
                end;
            finally CountryCodeList.Free; end;
            if TempCount > 1 then QuickSortResults(TempResults, 0, TempCount - 1, SORT_COUNT_DOWN);
          end;

        TASK_MULTI_TOP:
          begin
            { the computers (Kind 0) or the addresses (Kind 1) that the most accounts have
              played from. A computer with a dozen accounts behind it is the thing the whole
              multi-account check exists for, and it never shows up as one pair. The index is
              copied in one short hold and counted without the lock; only a key with two
              accounts or more takes it again, for the names. }
            TopKeys := nil;
            TopOwners := nil;
            TopGroup := nil;
            TopN := 0;
            if FTask.Kind = 1 then TempList := IpIndex else TempList := HwidIndex;
            { a few thousand entries per hold; a login in between may shift the index by one,
              which repeats or skips an entry at the edge - nothing for a top list }
            i := 0;
            while (TempList <> nil) and not Terminated do
            begin
              LockProfiles;
              try
                j := TempList.Count;
                if i >= j then Break;
                if j > i + 8192 then j := i + 8192;
                if TopN + (j - i) > Length(TopKeys) then
                begin
                  SetLength(TopKeys, TopN + (j - i) + TempList.Count div 4 + 64);
                  SetLength(TopOwners, Length(TopKeys));
                end;
                for c := i to j - 1 do
                begin
                  TopKeys[TopN] := TempList[c];
                  TopOwners[TopN] := Integer(PtrInt(TempList.Objects[c]));
                  Inc(TopN);
                end;
                i := j;
              finally UnlockProfiles; end;
            end;
            i := 0;
            while i < TopN do
            begin
              if Terminated then Break;
              Key := TopKeys[i];
              TopG := 0;
              j := i;
              while (j < TopN) and (TopKeys[j] = Key) do
              begin
                c := TopOwners[j];
                Found := False;
                for d := 0 to TopG - 1 do
                  if TopGroup[d] = c then begin Found := True; Break; end;
                if not Found then
                begin
                  if TopG >= Length(TopGroup) then SetLength(TopGroup, TopG * 2 + 8);
                  TopGroup[TopG] := c;
                  Inc(TopG);
                end;
                Inc(j);
              end;
              if TopG >= 2 then
              begin
                { the lowest account number first }
                for c := 1 to TopG - 1 do
                  for d := c downto 1 do
                    if TopGroup[d] < TopGroup[d - 1] then
                    begin
                      k := TopGroup[d];
                      TopGroup[d] := TopGroup[d - 1];
                      TopGroup[d - 1] := k;
                    end
                    else Break;
                LockProfiles;
                try
                  k := 0;
                  for c := 0 to TopG - 1 do
                    if (TopGroup[c] >= 0) and (TopGroup[c] < ProfileCount) then
                      if not ProfileIsEmpty(TopGroup[c]) then
                      begin
                        TopGroup[k] := TopGroup[c];
                        Inc(k);
                      end;
                  if k >= 2 then
                  begin
                    Room;
                    TempResults[TempCount].DataString := Key;
                    TempResults[TempCount].ProfileIndex := TopGroup[0];
                    TempResults[TempCount].SubIndex := k;
                    TempResults[TempCount].LoginCount := k;
                    TempResults[TempCount].ActiveName := GetProfileActiveName(TopGroup[0]);
                    LocStr := '';
                    for c := 0 to k - 1 do
                    begin
                      if c >= 4 then begin LocStr := LocStr + ', ...'; Break; end;
                      if LocStr <> '' then LocStr := LocStr + ', ';
                      LocStr := LocStr + GetProfileActiveName(TopGroup[c]);
                    end;
                    TempResults[TempCount].Location := LocStr;
                    TempResults[TempCount].MatchType := FTask.Kind;
                    Inc(TempCount);
                  end;
                finally UnlockProfiles; end;
              end;
              i := j;
            end;
            TopKeys := nil;
            TopOwners := nil;
            TopGroup := nil;
            if TempCount > 1 then QuickSortResults(TempResults, 0, TempCount - 1, SORT_COUNT_DOWN);
          end;

        TASK_MULTI:
          begin
            { one row per account that shares a computer or an address with another one:
              the number of accounts it is tied to, and what ties them }
            FoundProfiles := TStringList.Create;
            try
              for i := 0 to MaxIdx do
              begin
                if Terminated then Break;
                { the links, the names and the numbers of the row are taken under the lock; the
                  names are compared without it - an account on a computer shared by a hundred
                  others meant a hundred name comparisons while every join waited }
                MultiLinks := 0;
                LockProfiles;
                try
                  if not ProfileIsEmpty(i) then
                  begin
                    CollectLinks(i, FoundProfiles);
                    if FoundProfiles.Count >= Settings.MultiMinLinks then
                    begin
                      MultiLinks := FoundProfiles.Count;
                      MultiName := GetProfileActiveName(i);
                      if Length(MultiNames) < MultiLinks then
                      begin
                        SetLength(MultiNames, MultiLinks + 16);
                        SetLength(MultiNamed, MultiLinks + 16);
                      end;
                      for j := 0 to MultiLinks - 1 do
                      begin
                        k := StrToIntDef(FoundProfiles[j], -1);
                        MultiNamed[j] := (k >= 0) and (k < ProfileCount);
                        if MultiNamed[j] then MultiNames[j] := GetProfileActiveName(k) else MultiNames[j] := '';
                      end;
                      MultiRow.TimePlayed := Profiles[i].Profile.TimePlayed;
                      MultiRow.LastUsedDate := Profiles[i].Profile.LastPlayedTime;
                      MultiRow.FirstUsedDate := Profiles[i].Profile.RegisterTime;
                      MultiRow.NamesCount := Length(Profiles[i].OtherNames);
                      MultiRow.SteamsCount := Length(Profiles[i].OtherSteams);
                      MultiRow.HWIDsCount := Length(Profiles[i].OtherHWIDs);
                      MultiRow.IPsCount := Length(Profiles[i].OtherIPs);
                    end;
                  end;
                finally UnlockProfiles; end;
                if MultiLinks = 0 then Continue;
                c := 0;
                d := 0;
                SigLevel := 0;
                MultiMatch := '';
                for j := 0 to MultiLinks - 1 do
                begin
                  c := c or Integer(PtrInt(FoundProfiles.Objects[j]));
                  { the row carries the closest of the accounts it is tied to }
                  k := LinkScoreOf(MultiName, MultiNames[j], MultiNamed[j],
                                   Integer(PtrInt(FoundProfiles.Objects[j])), LocStr, TempNameCount);
                  if k > d then
                  begin
                    d := k;
                    SigLevel := TempNameCount;
                    CheckStr := LocStr;
                    MatchedStr := FoundProfiles[j];
                    MultiMatch := MultiNames[j];
                  end;
                end;
                { "strong" asks only for the pairs worth telling an admin about: enough
                  points and more than one signal that agrees }
                if FTask.Kind = 1 then
                  if (d < Settings.MultiReportScore) or (SigLevel < Settings.MultiMinSignals) then
                    Continue;
                Room;
                TempResults[TempCount].ProfileIndex := i;
                TempResults[TempCount].ActiveName := MultiName;
                TempResults[TempCount].LoginCount := MultiLinks;
                TempResults[TempCount].MatchType := c;
                TempResults[TempCount].Similarity := d;
                TempResults[TempCount].RiskLevel := SigLevel;
                TempResults[TempCount].DataString := CheckStr;
                TempResults[TempCount].SubIndex := StrToIntDef(MatchedStr, -1);
                TempResults[TempCount].Location := MultiMatch;
                TempResults[TempCount].TimePlayed := MultiRow.TimePlayed;
                TempResults[TempCount].LastUsedDate := MultiRow.LastUsedDate;
                TempResults[TempCount].FirstUsedDate := MultiRow.FirstUsedDate;
                TempResults[TempCount].NamesCount := MultiRow.NamesCount;
                TempResults[TempCount].SteamsCount := MultiRow.SteamsCount;
                TempResults[TempCount].HWIDsCount := MultiRow.HWIDsCount;
                TempResults[TempCount].IPsCount := MultiRow.IPsCount;
                Inc(TempCount);
                if (i mod OPTIMIZE_COUNT = 0) then Sleep(1);
              end;
            finally FoundProfiles.Free; end;
            { the closest pairs first, unless the admin asked for another order }
            if TempCount > 1 then
              if FTask.SortMethod = SORT_ID_UP then
                QuickSortBySimilarity(TempResults, 0, TempCount - 1)
              else
                QuickSortResults(TempResults, 0, TempCount - 1, FTask.SortMethod);
          end;
        TASK_CHECKD:
          begin
            FoundProfiles := TStringList.Create; FoundProfiles.Sorted := True; FoundProfiles.Duplicates := dupIgnore;
            try
              for i := 0 to MaxIdx do begin
                if Terminated then Break;
                LockProfiles;
                try
                  Found := False;
                  MatchedStr := '';
                  case FTask.Kind of
                    KIND_NAME: for j := 0 to Length(Profiles[i].OtherNames) - 1 do begin
                      if FTask.FilterFirst and (Profiles[i].OtherNames[j].FirstUsed <> 1) then Continue;
                      if FTask.FilterLast and (Profiles[i].OtherNames[j].LastUsed <> 1) then Continue;
                      MatchedStr := GetOtherDataString(Profiles[i].OtherNames[j]);
                      if FTask.ComparePartial then Found := ContainsText(MatchedStr, FTask.SearchPhrase)
                      else Found := LowerCase(MatchedStr) = LowerCase(FTask.SearchPhrase);
                      if Found then Break;
                    end;
                    KIND_STEAM: for j := 0 to Length(Profiles[i].OtherSteams) - 1 do begin
                      if FTask.FilterFirst and (Profiles[i].OtherSteams[j].FirstUsed <> 1) then Continue;
                      if FTask.FilterLast and (Profiles[i].OtherSteams[j].LastUsed <> 1) then Continue;
                      MatchedStr := GetOtherDataString(Profiles[i].OtherSteams[j]);
                      Found := MatchedStr = FTask.SearchPhrase;
                      if Found then Break;
                    end;
                    KIND_HWID: for j := 0 to Length(Profiles[i].OtherHWIDs) - 1 do begin
                      if FTask.FilterFirst and (Profiles[i].OtherHWIDs[j].FirstUsed <> 1) then Continue;
                      if FTask.FilterLast and (Profiles[i].OtherHWIDs[j].LastUsed <> 1) then Continue;
                      MatchedStr := GetOtherDataString(Profiles[i].OtherHWIDs[j]);
                      Found := MatchedStr = FTask.SearchPhrase;
                      if Found then Break;
                    end;
                    KIND_IP: for j := 0 to Length(Profiles[i].OtherIPs) - 1 do begin
                      if FTask.FilterFirst and (Profiles[i].OtherIPs[j].FirstUsed <> 1) then Continue;
                      if FTask.FilterLast and (Profiles[i].OtherIPs[j].LastUsed <> 1) then Continue;
                      MatchedStr := GetOtherDataString(Profiles[i].OtherIPs[j]);
                      if FTask.ComparePartial then Found := ContainsText(MatchedStr, FTask.SearchPhrase)
                      else Found := MatchedStr = FTask.SearchPhrase;
                      if Found then Break;
                    end;
                  end;

                  { the value that matched, of the kind asked for (a part of a name or an address
                    finds whole ones) }
                  if Found and (FoundProfiles.IndexOf(IntToStr(i)) < 0) then begin
                    FoundProfiles.Add(IntToStr(i)); Room;
                    TempResults[TempCount].ProfileIndex := i;
                    TempResults[TempCount].DataString := MatchedStr;
                    TempResults[TempCount].MatchType := FTask.Kind;
                    TempResults[TempCount].ActiveName := GetProfileActiveName(i);
                    TempResults[TempCount].TimePlayed := Profiles[i].Profile.TimePlayed;
                    Inc(TempCount);
                  end;
                finally UnlockProfiles; end;

                if (FTask.MaxResults > 0) and (TempCount >= FTask.MaxResults) then Break;
                if (i mod OPTIMIZE_COUNT = 0) then Sleep(1);
              end;
            finally FoundProfiles.Free; end;
          end;
        TASK_STATUSA:
          begin
            TaskIsValid := False;
            LockProfiles;
            try
              if (FTask.TargetIndex >= 0) and (FTask.TargetIndex < ProfileCount) then TaskIsValid := True;
            finally UnlockProfiles; end;

            if TaskIsValid then begin
                TargetSteams := TStringList.Create; TargetSteams.Sorted := True; TargetSteams.Duplicates := dupIgnore;
                TargetHWIDs := TStringList.Create; TargetHWIDs.Sorted := True; TargetHWIDs.Duplicates := dupIgnore;
                TargetIPs := TStringList.Create; TargetIPs.Sorted := True; TargetIPs.Duplicates := dupIgnore;
                TargetNames := TStringList.Create; TargetNames.Sorted := True; TargetNames.Duplicates := dupIgnore;
                FoundProfiles := TStringList.Create; FoundProfiles.Sorted := True; FoundProfiles.Duplicates := dupIgnore;

                try
                   LockProfiles;
                   try
                      for j := 0 to Length(Profiles[FTask.TargetIndex].OtherNames) - 1 do TargetNames.Add(GetOtherDataString(Profiles[FTask.TargetIndex].OtherNames[j]));
                      for j := 0 to Length(Profiles[FTask.TargetIndex].OtherSteams) - 1 do TargetSteams.Add(GetOtherDataString(Profiles[FTask.TargetIndex].OtherSteams[j]));
                      for j := 0 to Length(Profiles[FTask.TargetIndex].OtherHWIDs) - 1 do TargetHWIDs.Add(GetOtherDataString(Profiles[FTask.TargetIndex].OtherHWIDs[j]));
                      for j := 0 to Length(Profiles[FTask.TargetIndex].OtherIPs) - 1 do TargetIPs.Add(GetOtherDataString(Profiles[FTask.TargetIndex].OtherIPs[j]));
                   finally UnlockProfiles; end;

                   if FTask.Kind = STATUS_TIES_KEYS then
                   begin
                      { a player: the accounts behind the Steam accounts, computers and addresses of
                        this one come from the indexes, and names are not compared - the work of
                        one account instead of a walk through all of them }
                      KeyLists[0] := TargetSteams; KeyIndexes[0] := SteamIndex;
                      KeyLists[1] := TargetHWIDs; KeyIndexes[1] := HwidIndex;
                      KeyLists[2] := TargetIPs; KeyIndexes[2] := IpIndex;
                      LockProfiles;
                      try
                         for k := 0 to 2 do
                         begin
                            { what /multi leaves out, this list does too (CollectLinks): addresses
                              only with MultiAccountByAddress, and a key more accounts share than
                              the limit - a school, a cafe - says nothing about anybody; it named
                              strangers to a player with PlayerMulti = self }
                            if (k = 2) and not Settings.MultiByAddress then Continue;
                            if k = 2 then KeyCap := Settings.MultiMaxShared else KeyCap := Settings.MultiMaxSharedPC;
                            for j := 0 to KeyLists[k].Count - 1 do
                            begin
                               KeyPos := IndexFirst(KeyIndexes[k], KeyLists[k][j]);
                               if KeyPos < 0 then Continue;
                               KeyEnd := KeyPos;
                               while (KeyEnd < KeyIndexes[k].Count) and (KeyIndexes[k][KeyEnd] = KeyLists[k][j]) do
                                  Inc(KeyEnd);
                               if KeyEnd - KeyPos > KeyCap then Continue;
                               while KeyPos < KeyEnd do
                               begin
                                  c := Integer(PtrInt(KeyIndexes[k].Objects[KeyPos]));
                                  if (c >= 0) and (c < ProfileCount) and (c <> FTask.TargetIndex) then
                                     FoundProfiles.Add(Format('%.9d', [c]));
                                  Inc(KeyPos);
                               end;
                            end;
                         end;
                         { FoundProfiles is sorted: the accounts come in their order, as in the walk }
                         for j := 0 to FoundProfiles.Count - 1 do
                         begin
                            c := StrToInt(FoundProfiles[j]);
                            Room;
                            TempResults[TempCount].ProfileIndex := c;
                            TempResults[TempCount].ActiveName := GetProfileActiveName(c);
                            TempResults[TempCount].DataString := GetProfileActiveName(c);
                            TempResults[TempCount].TimePlayed := Profiles[c].Profile.TimePlayed;
                            TempResults[TempCount].MatchType := 1;
                            Inc(TempCount);
                         end;
                      finally UnlockProfiles; end;
                   end;

                   if FTask.Kind = STATUS_TIES_ALL then
                   for c := 0 to MaxIdx do begin
                      if c = FTask.TargetIndex then Continue;
                      if Terminated then Break;

                      HasMatch := False;
                      HasNameMatch := False;

                      LockProfiles;
                      try
                         if not HasMatch then for d := 0 to Length(Profiles[c].OtherSteams) - 1 do if TargetSteams.IndexOf(GetOtherDataString(Profiles[c].OtherSteams[d])) >= 0 then begin HasMatch := True; Break; end;
                         if not HasMatch then for d := 0 to Length(Profiles[c].OtherHWIDs) - 1 do if TargetHWIDs.IndexOf(GetOtherDataString(Profiles[c].OtherHWIDs[d])) >= 0 then begin HasMatch := True; Break; end;
                         if not HasMatch then for d := 0 to Length(Profiles[c].OtherIPs) - 1 do if TargetIPs.IndexOf(GetOtherDataString(Profiles[c].OtherIPs[d])) >= 0 then begin HasMatch := True; Break; end;

                         if not HasMatch then begin
                            for d := 0 to Length(Profiles[c].OtherNames) - 1 do
                               if TargetNames.IndexOf(GetOtherDataString(Profiles[c].OtherNames[d])) >= 0 then begin HasNameMatch := True; Break; end;
                         end;

                         if HasMatch or HasNameMatch then begin
                             Room;
                             TempResults[TempCount].ProfileIndex := c;
                             TempResults[TempCount].ActiveName := GetProfileActiveName(c);
                             TempResults[TempCount].DataString := GetProfileActiveName(c);
                             TempResults[TempCount].TimePlayed := Profiles[c].Profile.TimePlayed;

                             if HasMatch then
                                TempResults[TempCount].MatchType := 1
                             else
                                TempResults[TempCount].MatchType := 2;

                             if HasNameMatch then Inc(TempNameCount);
                             Inc(TempCount);
                         end;
                      finally UnlockProfiles; end;

                      if (c mod OPTIMIZE_COUNT = 0) then Sleep(1);
                   end;

                   LocCounts := TStringList.Create;
                   try
                      LockProfiles;
                      try
                         for j := 0 to Length(Profiles[FTask.TargetIndex].OtherIPs) - 1 do begin
                            CheckStr := GetOtherDataString(Profiles[FTask.TargetIndex].OtherIPs[j]);
                            EnterCriticalSection(GeoCritSec);
                            try
                               c := FindGeoIndex(CheckStr);
                               if c <> -1 then begin
                                  LocStr := string(GeoCache[c].Continent) + ', ' + string(GeoCache[c].Country) + ', ' + string(GeoCache[c].Region) + ', ' + string(GeoCache[c].City);
                               end else begin
                                  LocStr := 'Unknown';
                               end;
                            finally LeaveCriticalSection(GeoCritSec); end;

                            if LocStr <> 'Unknown' then begin
                               IdxOfLoc := LocCounts.IndexOf(LocStr);
                               if IdxOfLoc >= 0 then
                                  LocCounts.Objects[IdxOfLoc] := TObject(PtrInt(LocCounts.Objects[IdxOfLoc]) + 1)
                               else
                                  LocCounts.AddObject(LocStr, TObject(1));
                            end;
                         end;
                      finally UnlockProfiles; end;

                      TopLoc := '';
                      for k := 1 to 3 do begin
                         BestScore := 0;
                         GeoIdx := -1;
                         for d := 0 to LocCounts.Count - 1 do begin
                            if PtrInt(LocCounts.Objects[d]) > BestScore then begin
                               BestScore := PtrInt(LocCounts.Objects[d]);
                               GeoIdx := d;
                            end;
                         end;

                         if GeoIdx <> -1 then begin
                            if k > 1 then TopLoc := TopLoc + ' | ';
                            TopLoc := TopLoc + IntToStr(k) + '. ' + LocCounts[GeoIdx] + ' (' + IntToStr(BestScore) + ')';
                            LocCounts.Objects[GeoIdx] := TObject(-1);
                         end;
                      end;
                   finally LocCounts.Free; end;

                finally
                   TargetSteams.Free; TargetHWIDs.Free; TargetIPs.Free; FoundProfiles.Free; TargetNames.Free;
                end;
            end;
          end;
        TASK_VISITS:
          begin
            { The visits of one account, or the newest visits of the whole server (/lastvisits).
              For the server only the newest MaxResults stay: the list is cut back whenever it
              grows to four times that, so a big database is never held in memory whole. }
            for i := 0 to MaxIdx do
            begin
              if Terminated then Break;
              if (FTask.TargetIndex >= 0) and (i <> FTask.TargetIndex) then Continue;
              LockProfiles;
              try
                for j := 0 to High(Profiles[i].Visits) do
                begin
                  if not VisitWanted(Profiles[i].Visits[j], FromT, ToT, FTask.FilterFirst, FTask.FilterLast) then Continue;
                  Room;
                  TempResults[TempCount].ProfileIndex := i;
                  TempResults[TempCount].SubIndex := j;
                  TempResults[TempCount].ActiveName := string(Profiles[i].Visits[j].Name);
                  TempResults[TempCount].DataString := string(Profiles[i].Visits[j].IP);
                  TempResults[TempCount].FirstUsedDate := Profiles[i].Visits[j].Started;
                  TempResults[TempCount].LastUsedDate := Profiles[i].Visits[j].Started;
                  TempResults[TempCount].TimePlayed := Profiles[i].Visits[j].Seconds;
                  TempResults[TempCount].LoginCount := 1;
                  TempResults[TempCount].Similarity := Profiles[i].Visits[j].Confidence;
                  TempResults[TempCount].MatchType := Profiles[i].Visits[j].Flags;
                  TempResults[TempCount].RiskLevel := 0;
                  TempResults[TempCount].Location := '';
                  Inc(TempCount);
                end;
              finally UnlockProfiles; end;
              if (FTask.MaxResults > 0) and (TempCount >= FTask.MaxResults * 4) then
              begin
                QuickSortResults(TempResults, 0, TempCount - 1, SORT_LATEST_DOWN);
                TempCount := FTask.MaxResults;
              end;
              if (i mod OPTIMIZE_COUNT = 0) then Sleep(1);
            end;
            if TempCount > 1 then QuickSortResults(TempResults, 0, TempCount - 1, SORT_LATEST_DOWN);
            if (FTask.MaxResults > 0) and (TempCount > FTask.MaxResults) then TempCount := FTask.MaxResults;
            { the place names are read once per row, from the cache only }
            EnterCriticalSection(GeoCritSec);
            try
              for i := 0 to TempCount - 1 do
              begin
                c := FindGeoIndex(TempResults[i].DataString);
                if c >= 0 then
                begin
                  TempResults[i].Location := string(GeoCache[c].Country);
                  if string(GeoCache[c].City) <> '' then
                    TempResults[i].Location := TempResults[i].Location + ', ' + string(GeoCache[c].City);
                  TempResults[i].RiskLevel := GetRisk(c);
                end
                else if IsLocalAddress(TempResults[i].DataString) then
                  TempResults[i].Location := 'Local network'
                else
                  TempResults[i].Location := 'Unknown';
              end;
            finally LeaveCriticalSection(GeoCritSec); end;
          end;

        TASK_UNIQUE_VISITS:
          if FTask.Kind = 2 then
          begin
            { /lastvisitors: one row per account - how often and how long it came, and its last
              visit (the address and how sure the place is are those of that visit) }
            for i := 0 to MaxIdx do
            begin
              if Terminated then Break;
              LockProfiles;
              try
                d := -1;
                for j := 0 to High(Profiles[i].Visits) do
                begin
                  if not VisitWanted(Profiles[i].Visits[j], FromT, ToT, FTask.FilterFirst, FTask.FilterLast) then Continue;
                  if d < 0 then
                  begin
                    Room;
                    d := TempCount;
                    Inc(TempCount);
                    TempResults[d].ProfileIndex := i;
                    TempResults[d].SubIndex := d;
                    TempResults[d].ActiveName := GetProfileActiveName(i);
                    TempResults[d].DataString := '';
                    TempResults[d].LoginCount := 0;
                    TempResults[d].TimePlayed := 0;
                    TempResults[d].FirstUsedDate := Profiles[i].Visits[j].Started;
                    TempResults[d].LastUsedDate := -1;
                    TempResults[d].Similarity := 0;
                    TempResults[d].MatchType := 0;
                    TempResults[d].RiskLevel := 0;
                    TempResults[d].Location := '';
                  end;
                  Inc(TempResults[d].LoginCount);
                  Inc(TempResults[d].TimePlayed, Profiles[i].Visits[j].Seconds);
                  if Profiles[i].Visits[j].Started < TempResults[d].FirstUsedDate then
                    TempResults[d].FirstUsedDate := Profiles[i].Visits[j].Started;
                  if Profiles[i].Visits[j].Started >= TempResults[d].LastUsedDate then
                  begin
                    TempResults[d].LastUsedDate := Profiles[i].Visits[j].Started;
                    TempResults[d].DataString := string(Profiles[i].Visits[j].IP);
                    TempResults[d].Similarity := Profiles[i].Visits[j].Confidence;
                  end;
                  TempResults[d].MatchType := TempResults[d].MatchType or Profiles[i].Visits[j].Flags;
                end;
              finally UnlockProfiles; end;
              if (i mod OPTIMIZE_COUNT = 0) then Sleep(1);
            end;
            if TempCount > 1 then QuickSortResults(TempResults, 0, TempCount - 1, FTask.SortMethod);
            if (FTask.MaxResults > 0) and (TempCount > FTask.MaxResults) then TempCount := FTask.MaxResults;
            EnterCriticalSection(GeoCritSec);
            try
              for i := 0 to TempCount - 1 do
              begin
                c := FindGeoIndex(TempResults[i].DataString);
                if c >= 0 then
                begin
                  TempResults[i].Location := string(GeoCache[c].Country);
                  if string(GeoCache[c].City) <> '' then
                    TempResults[i].Location := TempResults[i].Location + ', ' + string(GeoCache[c].City);
                  TempResults[i].RiskLevel := GetRisk(c);
                end
                else if IsLocalAddress(TempResults[i].DataString) then
                  TempResults[i].Location := 'Local network'
                else
                  TempResults[i].Location := 'Unknown';
              end;
            finally LeaveCriticalSection(GeoCritSec); end;
          end
          else
          begin
            { one row per address (Kind 0) or per country (Kind 1) }
            UniqueIPs := TStringList.Create;
            UniqueIPs.Sorted := True;
            UniqueIPs.Duplicates := dupIgnore;
            try
              for i := 0 to MaxIdx do
              begin
                if Terminated then Break;
                if (FTask.TargetIndex >= 0) and (i <> FTask.TargetIndex) then Continue;
                LockProfiles;
                try
                  for j := 0 to High(Profiles[i].Visits) do
                  begin
                    if not VisitWanted(Profiles[i].Visits[j], FromT, ToT, FTask.FilterFirst, FTask.FilterLast) then Continue;
                    if FTask.Kind = 1 then
                      CurrentIP := string(Profiles[i].Visits[j].CountryCode)
                    else
                      CurrentIP := string(Profiles[i].Visits[j].IP);
                    if CurrentIP = '' then CurrentIP := '??';
                    d := UniqueIPs.IndexOf(CurrentIP);
                    if d < 0 then
                    begin
                      Room;
                      TempResults[TempCount].ProfileIndex := i;
                      TempResults[TempCount].SubIndex := TempCount;
                      TempResults[TempCount].DataString := CurrentIP;
                      TempResults[TempCount].ActiveName := GetProfileActiveName(i);
                      TempResults[TempCount].LoginCount := 1;
                      TempResults[TempCount].TimePlayed := Profiles[i].Visits[j].Seconds;
                      TempResults[TempCount].FirstUsedDate := Profiles[i].Visits[j].Started;
                      TempResults[TempCount].LastUsedDate := Profiles[i].Visits[j].Started;
                      TempResults[TempCount].Similarity := Profiles[i].Visits[j].Confidence;
                      TempResults[TempCount].MatchType := Profiles[i].Visits[j].Flags;
                      UniqueIPs.AddObject(CurrentIP, TObject(PtrInt(TempCount)));
                      Inc(TempCount);
                    end
                    else
                    begin
                      c := Integer(PtrInt(UniqueIPs.Objects[d]));
                      Inc(TempResults[c].LoginCount);
                      Inc(TempResults[c].TimePlayed, Profiles[i].Visits[j].Seconds);
                      if Profiles[i].Visits[j].Started < TempResults[c].FirstUsedDate then
                        TempResults[c].FirstUsedDate := Profiles[i].Visits[j].Started;
                      if Profiles[i].Visits[j].Started > TempResults[c].LastUsedDate then
                        TempResults[c].LastUsedDate := Profiles[i].Visits[j].Started;
                      TempResults[c].MatchType := TempResults[c].MatchType or Profiles[i].Visits[j].Flags;
                      if Profiles[i].Visits[j].Confidence > TempResults[c].Similarity then
                        TempResults[c].Similarity := Profiles[i].Visits[j].Confidence;
                    end;
                  end;
                finally UnlockProfiles; end;
                if (i mod OPTIMIZE_COUNT = 0) then Sleep(1);
              end;
            finally UniqueIPs.Free; end;
            if FTask.Kind <> 1 then
            begin
              EnterCriticalSection(GeoCritSec);
              try
                for i := 0 to TempCount - 1 do
                begin
                  c := FindGeoIndex(TempResults[i].DataString);
                  if c >= 0 then
                  begin
                    TempResults[i].Location := string(GeoCache[c].Country);
                    if string(GeoCache[c].City) <> '' then
                      TempResults[i].Location := TempResults[i].Location + ', ' + string(GeoCache[c].City);
                    TempResults[i].RiskLevel := GetRisk(c);
                  end
                  else if IsLocalAddress(TempResults[i].DataString) then
                    TempResults[i].Location := 'Local network'
                  else
                    TempResults[i].Location := 'Unknown';
                end;
              finally LeaveCriticalSection(GeoCritSec); end;
            end;
            if TempCount > 1 then QuickSortResults(TempResults, 0, TempCount - 1, FTask.SortMethod);
          end;

        TASK_VISIT_STATS:
          begin
            { the whole server by country }
            LocCounts := TStringList.Create;
            LocCounts.Sorted := True;
            LocCounts.Duplicates := dupIgnore;
            try
              for i := 0 to MaxIdx do
              begin
                if Terminated then Break;
                LockProfiles;
                try
                  for j := 0 to High(Profiles[i].Visits) do
                  begin
                    if not VisitWanted(Profiles[i].Visits[j], FromT, ToT, FTask.FilterFirst, FTask.FilterLast) then Continue;
                    LocStr := string(Profiles[i].Visits[j].CountryCode);
                    if LocStr = '' then LocStr := '??';
                    d := LocCounts.IndexOf(LocStr);
                    if d < 0 then
                    begin
                      Room;
                      TempResults[TempCount].DataString := LocStr;
                      { one address of this country, so the name can be looked up below }
                      TempResults[TempCount].Location := string(Profiles[i].Visits[j].IP);
                      TempResults[TempCount].ProfileIndex := -1;
                      TempResults[TempCount].SubIndex := TempCount;
                      TempResults[TempCount].LoginCount := 1;
                      TempResults[TempCount].TimePlayed := Profiles[i].Visits[j].Seconds;
                      TempResults[TempCount].FirstUsedDate := Profiles[i].Visits[j].Started;
                      TempResults[TempCount].LastUsedDate := Profiles[i].Visits[j].Started;
                      TempResults[TempCount].MatchType := Profiles[i].Visits[j].Flags;
                      TempResults[TempCount].Similarity := Profiles[i].Visits[j].Confidence;
                      TempResults[TempCount].NamesCount := 1;
                      LocCounts.AddObject(LocStr, TObject(PtrInt(TempCount)));
                      Inc(TempCount);
                    end
                    else
                    begin
                      c := Integer(PtrInt(LocCounts.Objects[d]));
                      Inc(TempResults[c].LoginCount);
                      Inc(TempResults[c].TimePlayed, Profiles[i].Visits[j].Seconds);
                      TempResults[c].MatchType := TempResults[c].MatchType or Profiles[i].Visits[j].Flags;
                      if Profiles[i].Visits[j].Started > TempResults[c].LastUsedDate then
                        TempResults[c].LastUsedDate := Profiles[i].Visits[j].Started;
                      if Profiles[i].Visits[j].Confidence > TempResults[c].Similarity then
                        TempResults[c].Similarity := Profiles[i].Visits[j].Confidence;
                    end;
                  end;
                finally UnlockProfiles; end;
                if (i mod OPTIMIZE_COUNT = 0) then Sleep(1);
              end;
            finally LocCounts.Free; end;
            { the name of the country comes from the location cache, and the locks are taken
              one after another, never one inside the other }
            for i := 0 to TempCount - 1 do
            begin
              CurrentIP := TempResults[i].Location;
              TempResults[i].Location := '';
              if CurrentIP = '' then Continue;
              EnterCriticalSection(GeoCritSec);
              try
                c := FindGeoIndex(CurrentIP);
                if c >= 0 then TempResults[i].Location := string(GeoCache[c].Country);
              finally LeaveCriticalSection(GeoCritSec); end;
            end;
            if TempCount > 1 then QuickSortResults(TempResults, 0, TempCount - 1, SORT_COUNT_DOWN);
          end;

        TASK_LIST:
          begin
            for i := 0 to MaxIdx do begin
              if Terminated then Break;
              LockProfiles;
              try
                { a profile emptied by a rollback is not a row }
                if ProfileIsEmpty(i) then Continue;
                Room;
                TempResults[TempCount].ProfileIndex := i;
                TempResults[TempCount].ActiveName := GetProfileActiveName(i);
                TempResults[TempCount].DataString := GetProfileActiveName(i);
                TempResults[TempCount].SubIndex := i;
                TempResults[TempCount].TimePlayed := Profiles[i].Profile.TimePlayed;
                TempResults[TempCount].LastUsedDate := Profiles[i].Profile.LastPlayedTime;

                { the lists themselves, as every other table counts them, not the numbers kept
                  in the head of the profile for the file }
                TempResults[TempCount].NamesCount := Length(Profiles[i].OtherNames);
                TempResults[TempCount].SteamsCount := Length(Profiles[i].OtherSteams);
                TempResults[TempCount].HWIDsCount := Length(Profiles[i].OtherHWIDs);
                TempResults[TempCount].IPsCount := Length(Profiles[i].OtherIPs);

                Inc(TempCount);
              finally UnlockProfiles; end;

              if (i mod OPTIMIZE_COUNT = 0) then Sleep(1);
            end;
            if TempCount > 1 then QuickSortResults(TempResults, 0, TempCount - 1, FTask.SortMethod);
          end;
        TASK_ADVANCED_SEARCH:
          begin
            { W6: the names are copied out under the lock and compared outside it. Counting the
              distance between two nicks is the expensive part, and it used to happen with the
              profiles held, so every join and every minute of play waited behind it. }
            TargetNames := TStringList.Create;
            try
              for i := 0 to MaxIdx do
              begin
                if Terminated then Break;
                TargetNames.Clear;
                LockProfiles;
                try
                  CheckStr := GetProfileActiveName(i);
                  PlayTime := Profiles[i].Profile.TimePlayed;
                  if FTask.SearchAll then
                    for j := 0 to Length(Profiles[i].OtherNames) - 1 do
                      TargetNames.Add(GetOtherDataString(Profiles[i].OtherNames[j]));
                finally UnlockProfiles; end;

                Found := False;
                MatchedStr := '';
                CurrentScore := 0;
                if not FTask.SearchAll then
                begin
                  CurrentScore := CalculateSimilarityEx(CheckStr, FTask.SearchPhrase, FTask.Kind);
                  if CurrentScore >= FTask.SortMethod then
                  begin
                    Found := True;
                    MatchedStr := CheckStr;
                  end;
                end
                else
                begin
                  BestScore := 0;
                  for j := 0 to TargetNames.Count - 1 do
                  begin
                    CurrentScore := CalculateSimilarityEx(TargetNames[j], FTask.SearchPhrase, FTask.Kind);
                    if CurrentScore > BestScore then
                    begin
                      BestScore := CurrentScore;
                      MatchedStr := TargetNames[j];
                    end;
                  end;
                  if BestScore >= FTask.SortMethod then
                  begin
                    Found := True;
                    CurrentScore := BestScore;
                  end;
                end;

                if Found then
                begin
                  Room;
                  TempResults[TempCount].ProfileIndex := i;
                  TempResults[TempCount].ActiveName := CheckStr;
                  TempResults[TempCount].DataString := MatchedStr;
                  TempResults[TempCount].Similarity := CurrentScore;
                  TempResults[TempCount].TimePlayed := PlayTime;
                  Inc(TempCount);
                end;
                if (FTask.MaxResults > 0) and (TempCount >= FTask.MaxResults) then Break;
                if (i mod OPTIMIZE_COUNT_ADVANCED = 0) then Sleep(1);
              end;
            finally TargetNames.Free; end;
            if TempCount > 1 then QuickSortBySimilarity(TempResults, 0, TempCount - 1);
          end;

        TASK_GEOSEARCH:
          begin
             CityList := TStringList.Create;
             CountryList := TStringList.Create;
             CountryCodeList := TStringList.Create;
             RegionList := TStringList.Create;
             ZipList := TStringList.Create;
             TimezoneList := TStringList.Create;
             IspList := TStringList.Create;

             ContinentList := TStringList.Create;
             ContinentCodeList := TStringList.Create;
             ReverseList := TStringList.Create;

             OtherList := TStringList.Create;
             TempList := TStringList.Create;

             ReqMobile := -1; ReqProxy := -1; ReqHosting := -1;

             try
               TempList.Delimiter := ' ';
               TempList.StrictDelimiter := False;
               TempList.DelimitedText := FTask.SearchPhrase;

               for j := 0 to TempList.Count - 1 do begin
                  Token := TempList[j];
                  PosSep := Pos(':', Token);
                  if PosSep > 1 then begin
                     Key := Trim(LowerCase(Copy(Token, 1, PosSep - 1)));
                     Val := Trim(LowerCase(Copy(Token, PosSep + 1, Length(Token))));
                     if Val <> '' then begin
                        if Key = 'city' then CityList.Add(Val)
                        else if Key = 'country' then CountryList.Add(Val)
                        else if (Key = 'cc') or (Key = 'countrycode') then CountryCodeList.Add(Val)
                        else if (Key = 'region') or (Key = 'regionname') then RegionList.Add(Val)
                        else if Key = 'zip' then ZipList.Add(Val)
                        else if Key = 'timezone' then TimezoneList.Add(Val)

                        else if Key = 'isp' then IspList.Add(Val)
                        else if Key = 'mobile' then ReqMobile := ParseBoolFlag(Val)
                        else if Key = 'proxy' then ReqProxy := ParseBoolFlag(Val)
                        else if Key = 'hosting' then ReqHosting := ParseBoolFlag(Val)

                        else if (Key = 'continent') then ContinentList.Add(Val)
                        else if (Key = 'ctc') or (Key = 'code') or (Key = 'continentcode') then ContinentCodeList.Add(Val)
                        else if (Key = 'dns') or (Key = 'host') or (Key = 'reverse') then ReverseList.Add(Val)

                        else OtherList.Add(Val);
                     end;
                  end else begin
                     if Trim(Token) <> '' then OtherList.Add(Trim(LowerCase(Token)));
                  end;
               end;

               for i := 0 to MaxIdx do begin
                  if Terminated then Break;
                  LockProfiles;
                  try
                    Found := False;
                    MatchedStr := '';
                    GeoIdx := -1;
                    RiskOfMatch := 0;

                    for j := 0 to Length(Profiles[i].OtherIPs) - 1 do begin
                       CheckStr := GetOtherDataString(Profiles[i].OtherIPs[j]);
                       EnterCriticalSection(GeoCritSec);
                       try
                          c := FindGeoIndex(CheckStr);
                          if c <> -1 then begin

                             MatchCity := (CityList.Count = 0);
                             if not MatchCity then for k := 0 to CityList.Count - 1 do if ContainsText(string(GeoCache[c].City), CityList[k]) then begin MatchCity := True; Break; end;

                             MatchCountry := (CountryList.Count = 0);
                             if not MatchCountry then for k := 0 to CountryList.Count - 1 do if ContainsText(string(GeoCache[c].Country), CountryList[k]) then begin MatchCountry := True; Break; end;

                             MatchCountryCode := (CountryCodeList.Count = 0);
                             if not MatchCountryCode then for k := 0 to CountryCodeList.Count - 1 do if ContainsText(string(GeoCache[c].CountryCode), CountryCodeList[k]) then begin MatchCountryCode := True; Break; end;

                             MatchRegion := (RegionList.Count = 0);
                             if not MatchRegion then for k := 0 to RegionList.Count - 1 do if ContainsText(string(GeoCache[c].Region), RegionList[k]) then begin MatchRegion := True; Break; end;

                             MatchZip := (ZipList.Count = 0);
                             if not MatchZip then for k := 0 to ZipList.Count - 1 do if ContainsText(string(GeoCache[c].Zip), ZipList[k]) then begin MatchZip := True; Break; end;

                             MatchTimezone := (TimezoneList.Count = 0);
                             if not MatchTimezone then for k := 0 to TimezoneList.Count - 1 do if ContainsText(string(GeoCache[c].Timezone), TimezoneList[k]) then begin MatchTimezone := True; Break; end;

                             MatchIsp := (IspList.Count = 0);
                             if not MatchIsp then for k := 0 to IspList.Count - 1 do if ContainsText(string(GeoCache[c].ISP), IspList[k]) then begin MatchIsp := True; Break; end;

                             MatchContinent := (ContinentList.Count = 0);
                             if not MatchContinent then for k := 0 to ContinentList.Count - 1 do if ContainsText(string(GeoCache[c].Continent), ContinentList[k]) then begin MatchContinent := True; Break; end;

                             MatchContinentCode := (ContinentCodeList.Count = 0);
                             if not MatchContinentCode then for k := 0 to ContinentCodeList.Count - 1 do if ContainsText(string(GeoCache[c].ContinentCode), ContinentCodeList[k]) then begin MatchContinentCode := True; Break; end;

                             MatchReverse := (ReverseList.Count = 0);
                             if not MatchReverse then for k := 0 to ReverseList.Count - 1 do if ContainsText(string(GeoCache[c].ReverseDNS), ReverseList[k]) then begin MatchReverse := True; Break; end;

                             MatchMobile := (ReqMobile = -1) or (GeoCache[c].IsMobile = ReqMobile);
                             MatchProxy := (ReqProxy = -1) or (GeoCache[c].IsProxy = ReqProxy);
                             MatchHosting := (ReqHosting = -1) or (GeoCache[c].IsHosting = ReqHosting);

                             { a word without a key is a place: two letters are the code of a
                               country or a continent ("pl", "eu"), anything longer a part of the
                               name of a country, a region, a city or a continent. The provider,
                               the host name, the postcode and the time zone need their key
                               (isp:, dns:, zip:, timezone:), or "pl" would find every "PLC". }
                             MatchOther := (OtherList.Count = 0);
                             if not MatchOther then begin
                                for k := 0 to OtherList.Count - 1 do
                                   if Length(OtherList[k]) = 2 then begin
                                      if SameText(string(GeoCache[c].CountryCode), OtherList[k]) or
                                         SameText(string(GeoCache[c].ContinentCode), OtherList[k]) then
                                      begin MatchOther := True; Break; end;
                                   end
                                   else if ContainsText(string(GeoCache[c].City), OtherList[k]) or
                                      ContainsText(string(GeoCache[c].Country), OtherList[k]) or
                                      ContainsText(string(GeoCache[c].Region), OtherList[k]) or
                                      ContainsText(string(GeoCache[c].Continent), OtherList[k]) then
                                   begin MatchOther := True; Break; end;
                             end;

                             if MatchCity and MatchCountry and MatchCountryCode and MatchRegion and MatchZip and MatchTimezone and MatchIsp and
                                MatchContinent and MatchContinentCode and MatchReverse and
                                MatchOther and MatchMobile and MatchProxy and MatchHosting then begin
                                Found := True;
                                MatchedStr := CheckStr;
                                GeoIdx := c;
                                { the risk has to be read here: outside the lock another thread
                                  may have reallocated GeoCache }
                                RiskOfMatch := GetRisk(c);
                             end;
                          end;
                       finally LeaveCriticalSection(GeoCritSec); end;
                       if Found then Break;
                    end;

                    if Found then begin
                      Room;
                      TempResults[TempCount].ProfileIndex := i;
                      TempResults[TempCount].ActiveName := GetProfileActiveName(i);
                      TempResults[TempCount].DataString := MatchedStr;
                      TempResults[TempCount].TimePlayed := Profiles[i].Profile.TimePlayed;
                      TempResults[TempCount].RiskLevel := RiskOfMatch;
                      Inc(TempCount);
                    end;
                  finally UnlockProfiles; end;
                  if (FTask.MaxResults > 0) and (TempCount >= FTask.MaxResults) then Break;
                  if (i mod OPTIMIZE_COUNT = 0) then Sleep(1);
               end;
             finally
               CityList.Free; CountryList.Free; CountryCodeList.Free; RegionList.Free; ZipList.Free; TimezoneList.Free; IspList.Free;
               ContinentList.Free; ContinentCodeList.Free; ReverseList.Free;
               OtherList.Free; TempList.Free;
             end;
          end;

        TASK_LOCATE_ALL:
          begin
             UniqueIPs := TStringList.Create;
             UniqueIPs.Sorted := True;
             UniqueIPs.Duplicates := dupIgnore;

             try
                for i := 0 to MaxIdx do begin
                   if Terminated then Break;
                   LockProfiles;
                   try
                      HasMatch := False;
                      if (FTask.TargetIndex = i) then HasMatch := True
                      else begin
                         case FTask.Kind of
                            KIND_HWID: begin
                               for j := 0 to Length(Profiles[i].OtherHWIDs) - 1 do
                                  if GetOtherDataString(Profiles[i].OtherHWIDs[j]) = FTask.SearchPhrase then begin HasMatch := True; Break; end;
                            end;
                            KIND_STEAM: begin
                               for j := 0 to Length(Profiles[i].OtherSteams) - 1 do
                                  if GetOtherDataString(Profiles[i].OtherSteams[j]) = FTask.SearchPhrase then begin HasMatch := True; Break; end;
                            end;
                            KIND_NAME: begin
                               { the same name, used at any time: a part of it would find every
                                 "Player" on the server }
                               for j := 0 to Length(Profiles[i].OtherNames) - 1 do
                                  if SameText(GetOtherDataString(Profiles[i].OtherNames[j]), FTask.SearchPhrase) then begin HasMatch := True; Break; end;
                            end;
                            KIND_IP: begin
                               for j := 0 to Length(Profiles[i].OtherIPs) - 1 do
                                  if GetOtherDataString(Profiles[i].OtherIPs[j]) = FTask.SearchPhrase then begin HasMatch := True; Break; end;
                            end;
                         end;
                      end;

                      if HasMatch then begin
                         for j := 0 to Length(Profiles[i].OtherIPs) - 1 do begin
                            CurrentIP := GetOtherDataString(Profiles[i].OtherIPs[j]);
                            if UniqueIPs.IndexOf(CurrentIP) < 0 then
                               UniqueIPs.AddObject(CurrentIP, TObject(PtrInt(i)));
                         end;
                      end;
                   finally UnlockProfiles; end;
                   if (i mod OPTIMIZE_COUNT = 0) then Sleep(1);
                end;

                for i := 0 to UniqueIPs.Count - 1 do begin
                   CurrentIP := UniqueIPs[i];
                   d := Integer(PtrInt(UniqueIPs.Objects[i]));
                   Room;
                   TempResults[TempCount].DataString := CurrentIP;
                   { the name first, then the location: every other task takes the locks in this
                     order, and two tasks with the opposite order block each other for good }
                   LockProfiles;
                   try
                      TempResults[TempCount].ActiveName := GetProfileActiveName(d);
                      TempResults[TempCount].ProfileIndex := d;
                   finally UnlockProfiles; end;
                   EnterCriticalSection(GeoCritSec);
                   try
                      c := FindGeoIndex(CurrentIP);
                      if c <> -1 then
                         TempResults[TempCount].RiskLevel := GetRisk(c)
                      else begin
                         TempResults[TempCount].RiskLevel := 0;
                         StartGeoLookup(CurrentIP);
                      end;
                   finally LeaveCriticalSection(GeoCritSec); end;
                   TempResults[TempCount].TimePlayed := 0;
                   Inc(TempCount);
                end;

             finally UniqueIPs.Free; end;
          end;
      end;

      { the other servers of the database, with SharedSearch or SharedLinks }
      ForeignRows := nil;
      ForeignWhy := '';
      if SharedReady and not Terminated then
        case FTask.TaskType of
          TASK_SEARCH:
            if Settings.SharedSearch then
              ForeignNames(FTask.SearchPhrase, False, ForeignRows, ForeignWhy);
          TASK_SEARCHID:
            if Settings.SharedSearch then
              ForeignNames(FTask.SearchPhrase, FTask.SearchAll, ForeignRows, ForeignWhy);
          TASK_CHECKD:
            if Settings.SharedSearch and (FTask.Kind >= KIND_NAME) and (FTask.Kind <= KIND_IP) then
              ForeignExact(FTask.Kind, FTask.SearchPhrase, ForeignRows, ForeignWhy);
          TASK_CHECKP:
            if Settings.SharedLinks and (FTask.Kind >= KIND_NAME) and (FTask.Kind <= KIND_IP) then
              ForeignLinks(FTask.TargetIndex, 1 shl FTask.Kind, ForeignRows, ForeignWhy);
          TASK_STATUSA:
            if Settings.SharedLinks and (FTask.Kind <> STATUS_TIES_NONE) then
              ForeignLinks(FTask.TargetIndex, FOREIGN_TIES, ForeignRows, ForeignWhy);
        end;

      EnterCriticalSection(Contexts[FTask.PlayerID].Lock);
      try
        { the player who asked has left, or asked again: this result is nobody's any more }
        if Contexts[FTask.PlayerID].Gen <> FTask.Gen then Exit;
        Contexts[FTask.PlayerID].Result.Foreign := ForeignRows;
        Contexts[FTask.PlayerID].Result.ForeignNote := ForeignWhy;
        Contexts[FTask.PlayerID].Result.TopLocations := TopLoc;
        Contexts[FTask.PlayerID].Result.PlayerID := FTask.PlayerID;
        { the account asked about, -1 = the whole server (the tables of visits say which) }
        Contexts[FTask.PlayerID].Result.ProfileIndex := FTask.TargetIndex;
        Contexts[FTask.PlayerID].Result.TaskType := FTask.TaskType;
        Contexts[FTask.PlayerID].Result.Kind := FTask.Kind;
        Contexts[FTask.PlayerID].Result.SortMethod := FTask.SortMethod;
        Contexts[FTask.PlayerID].Result.Success := True;
        Contexts[FTask.PlayerID].Result.ResultCount := TempCount;
        Contexts[FTask.PlayerID].Result.ExecutionTime := (Now - StartTime) * 86400;
        SetLength(Contexts[FTask.PlayerID].Result.Results, TempCount);
        for i := 0 to TempCount - 1 do Contexts[FTask.PlayerID].Result.Results[i] := TempResults[i];

        Contexts[FTask.PlayerID].Result.MultiAccountsByNameCount := 0;

        if FTask.TaskType = TASK_STATUSA then begin
          Contexts[FTask.PlayerID].Result.MultiAccountsCount := TempCount;
          Contexts[FTask.PlayerID].Result.MultiAccountsByNameCount := TempNameCount;
          LockProfiles;
          try
            if (FTask.TargetIndex >= 0) and (FTask.TargetIndex < ProfileCount) then begin
              Contexts[FTask.PlayerID].Result.ProfileIndex := FTask.TargetIndex;
              Contexts[FTask.PlayerID].Result.ActiveName := GetProfileActiveName(FTask.TargetIndex);
              Contexts[FTask.PlayerID].Result.RegisterTime := Profiles[FTask.TargetIndex].Profile.RegisterTime;
              Contexts[FTask.PlayerID].Result.LastPlayedTime := Profiles[FTask.TargetIndex].Profile.LastPlayedTime;
              Contexts[FTask.PlayerID].Result.TimePlayed := Profiles[FTask.TargetIndex].Profile.TimePlayed;
              Contexts[FTask.PlayerID].Result.TimeOnSpectator := Profiles[FTask.TargetIndex].Profile.TimeOnSpectator;
            end
            else Contexts[FTask.PlayerID].Result.ProfileIndex := -1;
          finally UnlockProfiles; end;
        end else if (FTask.TaskType = TASK_PRIVACY) or (FTask.TaskType = TASK_PRIVACYID) then begin
          { without begin/end the finally below released a lock this thread does not hold }
          LockProfiles;
          try
            if (FTask.TargetIndex >= 0) and (FTask.TargetIndex < ProfileCount) then Contexts[FTask.PlayerID].Result.ActiveName := GetProfileActiveName(FTask.TargetIndex);
          finally UnlockProfiles; end;
        end;
      finally LeaveCriticalSection(Contexts[FTask.PlayerID].Lock); end;
  finally
    EnterCriticalSection(Contexts[FTask.PlayerID].Lock);
    try
       if Contexts[FTask.PlayerID].Gen = FTask.Gen then
       begin
         Contexts[FTask.PlayerID].IsComplete := True;
         Contexts[FTask.PlayerID].IsBusy := False;
       end;
    finally LeaveCriticalSection(Contexts[FTask.PlayerID].Lock); end;
    SetLength(TempResults, 0);
  end;
end;

constructor TLoadThread.Create(const APath: string; TryMysql: Boolean);
begin inherited Create(True); FreeOnTerminate := False; FFilePath := APath; FTryMysql := TryMysql; end;

{ true only when the file really still holds the whole record }
function GrabBytes(FS: TStream; var Buf; Size: Integer): Boolean;
begin
  Result := (Size >= 0) and (FS.Size - FS.Position >= Size) and (FS.Read(Buf, Size) = Size);
end;

procedure TLoadThread.Execute;
var
  FS: TFileStream;
  Magic, Version: Cardinal;
  i, j, Count, GeoN, VCount, KeptN: Integer;
  TailOk: Boolean;
  VerdictN: Integer;
  FreshVerdicts, Kept: TVerdictArray;
  Fresh: TProfileArray;
  FreshGeo: array of TGeoEntry;
  Ok: Boolean;
  Aside: string;

  { the file could not be read: it goes aside under a name of its own, never deleted, and so
    does its journal - the next full write would start that one again from nothing }
  procedure PutAside;
  begin
    Aside := FFilePath + '.unreadable-' + FormatDateTime('yyyymmdd-hhnnss', Now);
    if RenameFile(FFilePath, Aside) then
    begin
      LoadNote := LoadNote + '; the file could not be read and was put aside as ' + ExtractFileName(Aside);
      if FileExists(JournalNameFor(FFilePath)) then RenameFile(JournalNameFor(FFilePath), Aside + '.jrn');
    end
    else
      LoadNote := LoadNote + '; the file could not be read';
  end;

  { Engine = mysql: the shared database takes the place of the file (af_shared.inc). True when
    the accounts carry the numbers of the database now: the file is written from them at once,
    and the journal starts with that file - the one on disk still belongs to the old file. }
  function SharedWanted: Boolean;
  begin
    Result := (Settings.MysqlEngine = ENGINE_MYSQL) and (Trim(Settings.MysqlHost) <> '') and not Terminated;
  end;

  function SharedAtLoad(AllowOffline: Boolean): Boolean;
  var
    Note: string;
  begin
    Result := SharedStart(FFilePath, AllowOffline, Note);
    if Note <> '' then
    begin
      if LoadNote <> '' then LoadNote := LoadNote + '; ';
      LoadNote := LoadNote + 'shared database: ' + Note;
    end;
    if Result then
    begin
      EnterCriticalSection(JournalCS);
      try
        JournalPath := '';
      finally LeaveCriticalSection(JournalCS); end;
      SaveAfterLoad := True;
    end;
  end;

  function ReadList(var Arr: TBinaryOtherDataArray; N: Integer): Boolean;
  var k: Integer;
  begin
    Result := False;
    SetLength(Arr, N);
    for k := 0 to N - 1 do
      if not GrabBytes(FS, Arr[k], SizeOf(TBinaryOtherData)) then Exit;
    Result := True;
  end;

begin
  GuardWorkerThread;
  { what an earlier load said, or asked for, is not about this one }
  LoadNote := '';
  SaveAfterLoad := False;
  JournalReplayed := 0;
  { the script prints the note after the load, and keeps it in the log }
  MoveNote := MoveOldDatabase(FFilePath);
  SetLoadedPath(FFilePath);
  try
    { Engine = mirror: the copy in the tables is read when it is newer than the file. The file
      is then written from it at once, so the two agree again; the one that was there is kept
      as .bak by the write. The write is started by the game thread (Save_After_Load), which
      owns every full write: two threads starting one could free the same thread twice. }
    if FTryMysql then
      if TryMysqlLoad(FFilePath, False) then
      begin
        SaveAfterLoad := True;
        LoadComplete := True; LoadInProgress := False;
        Exit;
      end;
    if not FileExists(FFilePath) then begin
      LockProfiles;
      try
        ProfileCount := 0;
        SetLength(Profiles, 100);
        RebuildProfileIndexes;
      finally UnlockProfiles; end;
      if SharedWanted then
        SharedAtLoad(True)
      { A new database: the file is written at once (Save_After_Load), so the journal - which
        belongs to a file on disk - keeps what changes from the first minute, and a server killed
        before the first full write loses seconds, not the quarter of an hour until it. Not with
        Engine = mirror: its copy in MySQL, unread now, would lose to a newer file at the next
        start. }
      else if not FTryMysql then
        SaveAfterLoad := True;
      LoadComplete := True; LoadInProgress := False; Exit;
    end;
    GeoN := 0;
    VerdictN := 0;
    FreshVerdicts := nil;
    Kept := nil;
    Fresh := nil;
    FreshGeo := nil;
    FS := TFileStream.Create(FFilePath, fmOpenRead or fmShareDenyWrite);
    try
      { everything is read into Fresh first: a truncated or foreign file never replaces
        the profiles the server is working with }
      Ok := GrabBytes(FS, Magic, SizeOf(Magic)) and (Magic = BINARY_MAGIC);
      if Ok then
        Ok := GrabBytes(FS, Version, SizeOf(Version)) and (Version >= 1) and (Version <= BINARY_VERSION);
      if Ok then
        Ok := GrabBytes(FS, Count, SizeOf(Count)) and (Count >= 0) and
              (Int64(Count) * SizeOf(TBinaryProfile) <= FS.Size - FS.Position);
      if Ok then begin
        SetLength(Fresh, Count);
        for i := 0 to Count - 1 do begin
          if Terminated then begin Ok := False; Break; end;
          if not GrabBytes(FS, Fresh[i].Profile, SizeOf(TBinaryProfile)) then begin Ok := False; Break; end;
          if not ReadList(Fresh[i].OtherNames, Fresh[i].Profile.OtherNamesCount) then begin Ok := False; Break; end;
          if not ReadList(Fresh[i].OtherSteams, Fresh[i].Profile.OtherSteamsCount) then begin Ok := False; Break; end;
          if not ReadList(Fresh[i].OtherHWIDs, Fresh[i].Profile.OtherHWIDsCount) then begin Ok := False; Break; end;
          if not ReadList(Fresh[i].OtherIPs, Fresh[i].Profile.OtherIPsCount) then begin Ok := False; Break; end;
          if (i mod 500 = 0) then Sleep(1);
        end;
      end;
      { the locations are extra data: a damaged tail only costs the geo cache }
      if Ok and (Version >= 3) then
        if GrabBytes(FS, Count, SizeOf(Count)) and (Count >= 0) and
           (Int64(Count) * SizeOf(TGeoEntry) <= FS.Size - FS.Position) then begin
          SetLength(FreshGeo, Count);
          GeoN := Count;
          for i := 0 to Count - 1 do
            if not GrabBytes(FS, FreshGeo[i], SizeOf(TGeoEntry)) then begin GeoN := i; Break; end;
        end;
      { after the locations the file holds a list of sections, each behind its own magic
        word: a file written before them simply has none, and an unknown word ends the
        reading. A damaged section ends it as well: the sections carry no length, so the ones
        after it cannot be found and are lost with it. }
      TailOk := Ok;
      while TailOk and (FS.Size - FS.Position >= SizeOf(Cardinal)) do
      begin
        if not GrabBytes(FS, Magic, SizeOf(Magic)) then Break;
        if Magic = VISITS_MAGIC then
        begin
          if not (GrabBytes(FS, Count, SizeOf(Count)) and (Count >= 0) and
                  (Count <= Length(Fresh))) then Break;
          for i := 0 to Count - 1 do
          begin
            if not GrabBytes(FS, VCount, SizeOf(VCount)) then begin TailOk := False; Break; end;
            { an account with more visits than it may keep (VisitKeepCount = 0 wrote them all
              in 2.0) is read whole and keeps the newest: refusing it ended the reading of the
              visits of every account after it, and of the Steam and the decisions sections }
            if (VCount < 0) or (VCount > MAX_VISITS_READ) or
               (Int64(VCount) * SizeOf(TVisitEntry) > FS.Size - FS.Position) then
            begin TailOk := False; Break; end;
            SetLength(Fresh[i].Visits, VCount);
            for j := 0 to VCount - 1 do
              if not GrabBytes(FS, Fresh[i].Visits[j], SizeOf(TVisitEntry)) then
              begin
                SetLength(Fresh[i].Visits, j);
                TailOk := False;
                Break;
              end;
            if not TailOk then Break;
            KeepNewestVisits(Fresh[i].Visits, MAX_VISITS_HARD);
          end;
        end
        else if Magic = STEAM_MAGIC then
        begin
          if not (GrabBytes(FS, Count, SizeOf(Count)) and (Count >= 0) and
                  (Count <= Length(Fresh)) and
                  (Int64(Count) * SizeOf(TSteamInfo) <= FS.Size - FS.Position)) then Break;
          for i := 0 to Count - 1 do
            if not GrabBytes(FS, Fresh[i].Steam, SizeOf(TSteamInfo)) then
            begin TailOk := False; Break; end;
        end
        else if Magic = VERDICT_MAGIC then
        begin
          if not (GrabBytes(FS, Count, SizeOf(Count)) and (Count >= 0) and
                  (Count <= MAX_VERDICTS) and
                  (Int64(Count) * SizeOf(TPairVerdict) <= FS.Size - FS.Position)) then Break;
          SetLength(FreshVerdicts, Count);
          VerdictN := Count;
          for i := 0 to Count - 1 do
            if not GrabBytes(FS, FreshVerdicts[i], SizeOf(TPairVerdict)) then
            begin VerdictN := i; TailOk := False; Break; end;
        end
        else
          Break;
      end;
    finally FS.Free; end;

    if not Ok then begin
      { the file cannot be read. With Engine = mysql the shared database holds everything the
        file did: the file is put aside under a name of its own (never deleted) and written anew
        from the database. }
      if SharedWanted then
      begin
        LockProfiles;
        try
          ProfileCount := 0;
          Profiles := nil;
          SetLength(Profiles, 100);
          VerdictCount := 0;
          RebuildVerdictIndex;
          RebuildProfileIndexes;
        finally UnlockProfiles; end;
        if SharedAtLoad(False) then
        begin
          PutAside;
          LoadComplete := True; LoadInProgress := False;
          Exit;
        end;
      end;
      { With Engine = mirror the copy in MySQL may still hold it all: then the file is put aside
        under a name of its own (never deleted) and written anew. }
      if FTryMysql and not Terminated then
        if TryMysqlLoad(FFilePath, True) then
        begin
          PutAside;
          SaveAfterLoad := True;
          LoadComplete := True; LoadInProgress := False;
          Exit;
        end;
      { the file stays untouched and nothing is saved over it until an admin looks at it }
      LoadError := True; LoadComplete := True; LoadInProgress := False; Exit;
    end;

    { and on top of it whatever happened after the last full write - the decisions of the
      admins as well: the journal holds all of them, and they used to be read from it and then
      replaced by the older ones of the file }
    if Settings.JournalSeconds > 0 then
    begin
      i := Length(Fresh);
      JournalReplayed := JournalReplay(FFilePath, Fresh, i, FreshVerdicts, VerdictN);
      if i > Length(Fresh) then SetLength(Fresh, i);
    end;

    { Everything is put right before a lock is taken: a date that is no date (a damaged file)
      is taken as "never" - comparing one raised an exception on the game thread, at a join -
      and every entry ever written used to be marked as the first one of its kind; the oldest
      date says which one really was, so a file from an older version is put right here. }
    Count := Length(Fresh);
    for i := 0 to Count - 1 do
    begin
      SaneProfile(Fresh[i]);
      MarkProfileFirstUsed(Fresh[i]);
    end;
    { a decision about an account that is gone means nothing any more }
    SetLength(Kept, VerdictN + 100);
    KeptN := 0;
    for i := 0 to VerdictN - 1 do
      if (FreshVerdicts[i].A >= 0) and (FreshVerdicts[i].A < Count) and
         (FreshVerdicts[i].B >= 0) and (FreshVerdicts[i].B < Count) and
         (FreshVerdicts[i].A <> FreshVerdicts[i].B) then
      begin
        Kept[KeptN] := FreshVerdicts[i];
        SaneDate(Kept[KeptN].Decided);
        Inc(KeptN);
      end;
    FreshVerdicts := nil;
    { the room for new players is made here too: the lists change hands in one step under the
      lock. They used to be copied there profile by profile while every join waited, into a list
      made after the old one was thrown away - a list that could not be had any more (the memory
      of a 32-bit server) left the server without any. }
    SetLength(Fresh, Count + 100);
    for i := 0 to GeoN - 1 do
    begin
      SaneDate(FreshGeo[i].LastUpdated);
      { entries written before 20.09.2026 hold raw UTF-8; the same conversion repairs them,
        and text that is already converted is left alone }
      SetGeoString(FreshGeo[i].Continent, string(FreshGeo[i].Continent));
      SetGeoString(FreshGeo[i].Country, string(FreshGeo[i].Country));
      SetGeoString(FreshGeo[i].Region, string(FreshGeo[i].Region));
      SetGeoString(FreshGeo[i].City, string(FreshGeo[i].City));
      SetGeoString(FreshGeo[i].ISP, string(FreshGeo[i].ISP));
      SetGeoString(FreshGeo[i].ReverseDNS, string(FreshGeo[i].ReverseDNS));
      SetGeoString(FreshGeo[i].Timezone, string(FreshGeo[i].Timezone));
    end;
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

    if SharedWanted then
      if SharedAtLoad(True) then
      begin
        LoadComplete := True; LoadInProgress := False;
        Exit;
      end;

    { From here on every change is written to the journal as it happens. A journal that was put
      on top of the file is not started again here: until the file holds what it gave, it is the
      only copy of that on disk - started again empty, a second stop before the next full write
      (a restart, a /recompile, an empty server that never writes) lost those changes. The file
      is written at once instead (Save_After_Load), and that write starts the journal again. }
    if JournalReplayed > 0 then
      SaveAfterLoad := True
    else if Settings.JournalSeconds > 0 then
      JournalStart(FFilePath);
    LoadComplete := True; LoadInProgress := False;
  except LoadError := True; LoadComplete := True; LoadInProgress := False; end;
end;

{ how a full write ended: the admin who asked hears it, and the console hears a failure }
procedure SaveDone(const Path: string; Asker, Session: Integer; Started: QWord; Failed: Boolean);
var
  SR: TSearchRec;
  Size: Int64;
begin
  if Failed then
  begin
    if Asker > 0 then
      NoticeFor(Asker, Session, Settings.ColError, 'The database could not be written - the file that was there is kept.');
    if Asker <> MAX_SLOTS then
      Notice(MAX_SLOTS, Settings.ColError, 'The database could not be written - the file that was there is kept.');
    Exit;
  end;
  if Asker <= 0 then Exit;
  Size := 0;
  if FindFirst(Path, faAnyFile, SR) = 0 then
  begin
    Size := SR.Size;
    FindClose(SR);
  end;
  NoticeFor(Asker, Session, Settings.ColGood, Format('The database is written: %d accounts, %d KB, in %.1f s.',
    [ProfileCount, Size div 1024, (GetTickCount64 - Started) / 1000]));
end;

constructor TSaveThread.Create(const APath: string; AAsker, ASession: Integer);
begin
  inherited Create(True);
  FreeOnTerminate := False;
  FFilePath := APath;
  FAsker := AAsker;
  FSession := ASession;
  FStarted := GetTickCount64;
end;

{ Profiles that went into the journal while this write ran may be missing from the file, and
  the journal that held them was just started again: they go into the new one, and so do the
  decisions of the admins. The flag goes down under the same lock, so the journal never misses
  the moment. }
procedure SaveFinished(Failed: Boolean);
var
  i: Integer;
begin
  LockProfiles;
  try
    for i := 0 to SaveFlushedCount - 1 do JournalMark(SaveFlushed[i]);
    SaveFlushedCount := 0;
    if SaveVerdictsFlushed then VerdictsDirty := True;
    SaveVerdictsFlushed := False;
    SaveError := Failed;
    SaveComplete := True;
    SaveInProgress := False;
  finally UnlockProfiles; end;
end;

procedure TSaveThread.Execute;
const
  CHUNK_BYTES = 1024 * 1024;
var FS: TFileStream; Magic, Version: Cardinal; i, j, Count, VCount, GeoN: Integer; TempPath, BakPath: string; Ok: Boolean;
  Failed: Boolean;
  GeoCopy: array of TGeoEntry;
  Chunk: TMemoryStream;

  { what was packed goes to the file in large blocks, outside every lock: a join waits for the
    copy of one profile into memory, never for the disk }
  procedure Spill(All: Boolean);
  begin
    if (Chunk.Position > 0) and (All or (Chunk.Position >= CHUNK_BYTES)) then
    begin
      FS.WriteBuffer(Chunk.Memory^, Chunk.Position);
      Chunk.Position := 0;
    end;
  end;

begin
  GuardWorkerThread;
  Failed := False;
  GeoCopy := nil;
  Chunk := TMemoryStream.Create;
  try
    TempPath := FFilePath + '.tmp';
    if ExtractFilePath(FFilePath) <> '' then ForceDirectories(ExtractFilePath(FFilePath));
    FS := TFileStream.Create(TempPath, fmCreate);
    try
      Magic := BINARY_MAGIC; Version := BINARY_VERSION;
      FS.WriteBuffer(Magic, SizeOf(Magic));
      FS.WriteBuffer(Version, SizeOf(Version));

      LockProfiles;
      Count := ProfileCount;
      UnlockProfiles;

      FS.WriteBuffer(Count, SizeOf(Count));

      for i := 0 to Count - 1 do begin
        if Terminated then Break;
        LockProfiles;
        try
          { the header of a profile says how many entries follow, so it is taken from the lists }
          Profiles[i].Profile.OtherNamesCount := Length(Profiles[i].OtherNames);
          Profiles[i].Profile.OtherSteamsCount := Length(Profiles[i].OtherSteams);
          Profiles[i].Profile.OtherHWIDsCount := Length(Profiles[i].OtherHWIDs);
          Profiles[i].Profile.OtherIPsCount := Length(Profiles[i].OtherIPs);
          Chunk.WriteBuffer(Profiles[i].Profile, SizeOf(TBinaryProfile));
          for j := 0 to Length(Profiles[i].OtherNames) - 1 do Chunk.WriteBuffer(Profiles[i].OtherNames[j], SizeOf(TBinaryOtherData));
          for j := 0 to Length(Profiles[i].OtherSteams) - 1 do Chunk.WriteBuffer(Profiles[i].OtherSteams[j], SizeOf(TBinaryOtherData));
          for j := 0 to Length(Profiles[i].OtherHWIDs) - 1 do Chunk.WriteBuffer(Profiles[i].OtherHWIDs[j], SizeOf(TBinaryOtherData));
          for j := 0 to Length(Profiles[i].OtherIPs) - 1 do Chunk.WriteBuffer(Profiles[i].OtherIPs[j], SizeOf(TBinaryOtherData));
        finally UnlockProfiles; end;
        Spill(False);
      end;
      Spill(True);

      { the number and the entries are taken under the same lock: a lookup that ended in between
        used to leave one entry more in the file than its number said, and the reading stopped
        there. They are copied, and written after the lock: a join waits for no disk. }
      EnterCriticalSection(GeoCritSec);
      try
        GeoN := GeoCount;
        SetLength(GeoCopy, GeoN);
        for i := 0 to GeoN - 1 do GeoCopy[i] := GeoCache[i];
      finally
        LeaveCriticalSection(GeoCritSec);
      end;
      FS.WriteBuffer(GeoN, SizeOf(GeoN));
      for i := 0 to GeoN - 1 do
      begin
        Chunk.WriteBuffer(GeoCopy[i], SizeOf(TGeoEntry));
        Spill(False);
      end;
      Spill(True);
      GeoCopy := nil;

      { the visits, in their own section }
      Magic := VISITS_MAGIC;
      FS.WriteBuffer(Magic, SizeOf(Magic));
      FS.WriteBuffer(Count, SizeOf(Count));
      for i := 0 to Count - 1 do
      begin
        LockProfiles;
        try
          VCount := Length(Profiles[i].Visits);
          Chunk.WriteBuffer(VCount, SizeOf(VCount));
          for j := 0 to VCount - 1 do
            Chunk.WriteBuffer(Profiles[i].Visits[j], SizeOf(TVisitEntry));
        finally UnlockProfiles; end;
        Spill(False);
      end;
      Spill(True);

      { and what Steam says about the accounts, in a section of its own }
      Magic := STEAM_MAGIC;
      FS.WriteBuffer(Magic, SizeOf(Magic));
      FS.WriteBuffer(Count, SizeOf(Count));
      for i := 0 to Count - 1 do
      begin
        LockProfiles;
        try
          Chunk.WriteBuffer(Profiles[i].Steam, SizeOf(TSteamInfo));
        finally UnlockProfiles; end;
        Spill(False);
      end;
      Spill(True);

      { and what the admins decided about pairs of accounts }
      Magic := VERDICT_MAGIC;
      FS.WriteBuffer(Magic, SizeOf(Magic));
      LockProfiles;
      try
        VCount := VerdictCount;
        Chunk.WriteBuffer(VCount, SizeOf(VCount));
        for i := 0 to VCount - 1 do Chunk.WriteBuffer(Verdicts[i], SizeOf(TPairVerdict));
      finally UnlockProfiles; end;
      Spill(True);
      { on the disk before it takes the place of the file: a power cut right after the rename
        left the new name on blocks that were never written }
      if not Terminated then SyncFileHandle(FS.Handle);
    finally FS.Free; end;
    { a write that was stopped half way is no database: it stays in the .tmp and says so }
    if Terminated then Failed := True
    else begin
      { The new file takes the place of the old one in one step, and the old one stays as .bak.
        It used to be renamed to .bak first and the new one into its place after that: a server
        killed in between started with no database at all, and its next write threw the .bak
        away. }
      BakPath := FFilePath + '.bak';
      if FileExists(FFilePath) then
        if not KeepAsBak(FFilePath, BakPath) then
          LogEvent('the .bak of the database could not be made: ' + BakPath);
      Ok := ReplaceFileAtomic(TempPath, FFilePath);
      if not Ok then
        { nothing was replaced: the old file is where it was, and the failure is reported }
        Failed := True
      else begin
        { everything up to here is in the file now, so the journal starts again empty and
          a copy of the finished file is put aside }
        if Settings.JournalSeconds > 0 then JournalStart(FFilePath);
        { a database without accounts (the first write of a new one) is worth no copy, and its
          copy would hold off the first real one for BackupHours }
        if Count > 0 then KeepBackup(FFilePath);
        SharedAfterSave(FFilePath);
      end;
    end;
  except
    Failed := True;
  end;
  Chunk.Free;
  SaveFinished(Failed);
  SaveDone(FFilePath, FAsker, FSession, FStarted, Failed);
end;

{ False when the slot's last search is still running (or the server is shutting down) }
function StartSearch(PlayerID, TaskType, Kind, SortMethod, TargetIndex, MaxResults: Integer; const SearchPhrase: string;
  SearchAll, ComparePartial, FilterFirst, FilterLast, FilterAll: Boolean): Boolean;
var Task: TSearchTask; Old: TThread;
begin
  Result := False;
  if (PlayerID < 1) or (PlayerID > MAX_SLOTS) then Exit;
  { Cleanup collects the searches of every slot: one started after that would outlive the library }
  if ShuttingDown then Exit;
  EnterCriticalSection(Contexts[PlayerID].Lock);
  if Contexts[PlayerID].IsBusy or ShuttingDown then begin
     LeaveCriticalSection(Contexts[PlayerID].Lock);
     Exit;
  end;

  { the old thread is picked up here and waited for below: waiting while its own lock is held
    stops it from ever finishing }
  Old := Contexts[PlayerID].Thread;
  Contexts[PlayerID].Thread := nil;

  Task.PlayerID := PlayerID; Task.TaskType := TaskType; Task.Kind := Kind; Task.SortMethod := SortMethod;
  Task.TargetIndex := TargetIndex; Task.SearchPhrase := SearchPhrase; Task.SearchAll := SearchAll;
  Task.ComparePartial := ComparePartial; Task.FilterFirst := FilterFirst; Task.FilterLast := FilterLast;
  Task.FilterAll := FilterAll; Task.MaxResults := MaxResults;
  Inc(Contexts[PlayerID].Gen);
  Task.Gen := Contexts[PlayerID].Gen;

  Contexts[PlayerID].IsBusy := True;
  Contexts[PlayerID].IsComplete := False;
  Contexts[PlayerID].Result.ResultCount := 0;
  SetLength(Contexts[PlayerID].Result.Results, 0);
  LeaveCriticalSection(Contexts[PlayerID].Lock);

  if Old <> nil then begin
    Old.Terminate;
    Old.WaitFor;
    Old.Free;
  end;

  Contexts[PlayerID].Thread := TSearchThread.Create(Task);
  Contexts[PlayerID].Thread.Start;
  Result := True;
end;

procedure Start_Search_Task(PlayerID, TaskType, Kind, SortMethod, TargetIndex, MaxResults: Integer; SearchPhrase: PChar; SearchAll, ComparePartial, FilterFirst, FilterLast, FilterAll: Boolean); cdecl;
begin
  StartSearch(PlayerID, TaskType, Kind, SortMethod, TargetIndex, MaxResults, string(SearchPhrase), SearchAll,
    ComparePartial, FilterFirst, FilterLast, FilterAll);
end;

function Is_Task_Complete(PlayerID: Integer): Boolean; cdecl;
begin
  Result := False;
  try
    if (PlayerID < 1) or (PlayerID > MAX_SLOTS) then Exit;
    EnterCriticalSection(Contexts[PlayerID].Lock);
    try
      Result := Contexts[PlayerID].IsComplete;
    finally
      LeaveCriticalSection(Contexts[PlayerID].Lock);
    end;
  except
    ExportFailed('Is_Task_Complete');
    Result := False;
  end;
end;

function Is_Task_In_Progress(PlayerID: Integer): Boolean; cdecl;
begin
  if (PlayerID < 1) or (PlayerID > MAX_SLOTS) then Exit(False);
  EnterCriticalSection(Contexts[PlayerID].Lock);
  try
    Result := Contexts[PlayerID].IsBusy;
  finally
    LeaveCriticalSection(Contexts[PlayerID].Lock);
  end;
end;

var
  { what Select_Task_Result took: a copy of the slot's result, made under the slot's lock }
  SelectedResult: TSearchResult;

{ Select_Task_Result and the Get_Result_* below are not used by the script (it draws the tables
  in the library, af_views.inc); they are kept for the test programs and for tools. The result
  is copied under the slot's lock: a search that ends (or a player who leaves) replaces it while
  the Get_Result_* read it, and the count could say more rows than the list had yet. }
procedure Select_Task_Result(PlayerID: Integer); cdecl;
begin
  try
    if (PlayerID < 1) or (PlayerID > MAX_SLOTS) then
    begin
      ActiveResult := @EmptyResult;
      Exit;
    end;
    EnterCriticalSection(Contexts[PlayerID].Lock);
    try
      SelectedResult := Contexts[PlayerID].Result;
    finally
      LeaveCriticalSection(Contexts[PlayerID].Lock);
    end;
    if SelectedResult.ResultCount > Length(SelectedResult.Results) then
      SelectedResult.ResultCount := Length(SelectedResult.Results);
    ActiveResult := @SelectedResult;
  except
    ExportFailed('Select_Task_Result');
    ActiveResult := @EmptyResult;
  end;
end;

procedure Clear_Task_Result(PlayerID: Integer); cdecl;
begin
  try
    if (PlayerID < 1) or (PlayerID > MAX_SLOTS) then Exit;
    EnterCriticalSection(Contexts[PlayerID].Lock);
    try
      { a search still running keeps its answer to itself (see TPlayerContext.Gen) }
      Inc(Contexts[PlayerID].Gen);
      Contexts[PlayerID].IsComplete := False;
      Contexts[PlayerID].IsBusy := False;
      Contexts[PlayerID].Result.ResultCount := 0;
      SetLength(Contexts[PlayerID].Result.Results, 0);
    finally
      LeaveCriticalSection(Contexts[PlayerID].Lock);
    end;
  except
    ExportFailed('Clear_Task_Result');
  end;
end;

function Get_Result_Player_ID(): Integer; cdecl;
begin if ActiveResult = nil then Exit(0); Result := ActiveResult^.PlayerID; end;

function Get_Result_Task_Type(): Integer; cdecl;
begin if ActiveResult = nil then Exit(0); Result := ActiveResult^.TaskType; end;

function Get_Result_Kind(): Integer; cdecl;
begin if ActiveResult = nil then Exit(0); Result := ActiveResult^.Kind; end;

function Get_Result_Sort_Method(): Integer; cdecl;
begin if ActiveResult = nil then Exit(0); Result := ActiveResult^.SortMethod; end;

function Get_Result_Count(): Integer; cdecl;
begin if ActiveResult = nil then Exit(0); Result := ActiveResult^.ResultCount; end;

function Get_Result_Execution_Time(): Integer; cdecl;
begin if ActiveResult = nil then Exit(0); Result := Round(ActiveResult^.ExecutionTime * 1000); end;

function Get_Result_Item_Index(ItemIdx: Integer): Integer; cdecl;
begin if ActiveResult = nil then Exit(-1); if (ItemIdx >= 0) and (ItemIdx < ActiveResult^.ResultCount) then Result := ActiveResult^.Results[ItemIdx].ProfileIndex else Result := -1; end;

function Get_Result_Item_SubIndex(ItemIdx: Integer): Integer; cdecl;
begin if ActiveResult = nil then Exit(-1); if (ItemIdx >= 0) and (ItemIdx < ActiveResult^.ResultCount) then Result := ActiveResult^.Results[ItemIdx].SubIndex else Result := -1; end;

function Get_Result_Item_String(ItemIdx: Integer): PChar; cdecl;
begin if ActiveResult = nil then Exit(''); if (ItemIdx >= 0) and (ItemIdx < ActiveResult^.ResultCount) then begin TempStringResult := ActiveResult^.Results[ItemIdx].DataString; Result := PChar(TempStringResult); end else Result := ''; end;

function Get_Result_Item_LoginCount(ItemIdx: Integer): PChar; cdecl;
begin if ActiveResult = nil then Exit('0'); if (ItemIdx >= 0) and (ItemIdx < ActiveResult^.ResultCount) then TempStringResult := IntToStr(ActiveResult^.Results[ItemIdx].LoginCount) else TempStringResult := '0'; Result := PChar(TempStringResult); end;

function Get_Result_Item_TimePlayed(ItemIdx: Integer): PChar; cdecl;
begin if ActiveResult = nil then Exit('0'); if (ItemIdx >= 0) and (ItemIdx < ActiveResult^.ResultCount) then TempStringResult := IntToStr(ActiveResult^.Results[ItemIdx].TimePlayed) else TempStringResult := '0'; Result := PChar(TempStringResult); end;

function Get_Result_Item_FirstUsedDate(ItemIdx: Integer): PChar; cdecl;
begin if ActiveResult = nil then Exit('0'); if (ItemIdx >= 0) and (ItemIdx < ActiveResult^.ResultCount) then TempStringResult := FloatToStr(ActiveResult^.Results[ItemIdx].FirstUsedDate) else TempStringResult := '0'; Result := PChar(TempStringResult); end;

function Get_Result_Item_LastUsedDate(ItemIdx: Integer): PChar; cdecl;
begin if ActiveResult = nil then Exit('0'); if (ItemIdx >= 0) and (ItemIdx < ActiveResult^.ResultCount) then TempStringResult := FloatToStr(ActiveResult^.Results[ItemIdx].LastUsedDate) else TempStringResult := '0'; Result := PChar(TempStringResult); end;

function Get_Result_Item_NamesCount(ItemIdx: Integer): Integer; cdecl;
begin if ActiveResult = nil then Exit(0); if (ItemIdx >= 0) and (ItemIdx < ActiveResult^.ResultCount) then Result := ActiveResult^.Results[ItemIdx].NamesCount else Result := 0; end;

function Get_Result_Item_MatchType(ItemIdx: Integer): Integer; cdecl;
begin if ActiveResult = nil then Exit(0); if (ItemIdx >= 0) and (ItemIdx < ActiveResult^.ResultCount) then Result := ActiveResult^.Results[ItemIdx].MatchType else Result := 0; end;

function Get_Result_Multi_Accounts_ByName_Count(): Integer; cdecl;
begin if ActiveResult = nil then Exit(0); Result := ActiveResult^.MultiAccountsByNameCount; end;

function Get_Result_Item_SteamsCount(ItemIdx: Integer): Integer; cdecl;
begin if ActiveResult = nil then Exit(0); if (ItemIdx >= 0) and (ItemIdx < ActiveResult^.ResultCount) then Result := ActiveResult^.Results[ItemIdx].SteamsCount else Result := 0; end;

function Get_Result_Item_HWIDsCount(ItemIdx: Integer): Integer; cdecl;
begin if ActiveResult = nil then Exit(0); if (ItemIdx >= 0) and (ItemIdx < ActiveResult^.ResultCount) then Result := ActiveResult^.Results[ItemIdx].HWIDsCount else Result := 0; end;

function Get_Result_Item_IPsCount(ItemIdx: Integer): Integer; cdecl;
begin if ActiveResult = nil then Exit(0); if (ItemIdx >= 0) and (ItemIdx < ActiveResult^.ResultCount) then Result := ActiveResult^.Results[ItemIdx].IPsCount else Result := 0; end;

function Get_Result_Active_Name(): PChar; cdecl;
begin if ActiveResult = nil then Exit(''); TempStringResult := ActiveResult^.ActiveName; Result := PChar(TempStringResult); end;

function Get_Result_Item_ActiveName(ItemIdx: Integer): PChar; cdecl;
begin if ActiveResult = nil then Exit(''); if (ItemIdx >= 0) and (ItemIdx < ActiveResult^.ResultCount) then begin TempStringResult := ActiveResult^.Results[ItemIdx].ActiveName; Result := PChar(TempStringResult); end else Result := ''; end;

function Get_Result_Profile_Index(): Integer; cdecl;
begin if ActiveResult = nil then Exit(-1); Result := ActiveResult^.ProfileIndex; end;

function Get_Result_Register_Time(): PChar; cdecl;
begin if ActiveResult = nil then Exit('0'); TempStringResult := FloatToStr(ActiveResult^.RegisterTime); Result := PChar(TempStringResult); end;

function Get_Result_Last_Played_Time(): PChar; cdecl;
begin if ActiveResult = nil then Exit('0'); TempStringResult := FloatToStr(ActiveResult^.LastPlayedTime); Result := PChar(TempStringResult); end;

function Get_Result_Time_Played(): PChar; cdecl;
begin if ActiveResult = nil then Exit('0'); TempStringResult := IntToStr(ActiveResult^.TimePlayed); Result := PChar(TempStringResult); end;

function Get_Result_Time_On_Spectator(): PChar; cdecl;
begin if ActiveResult = nil then Exit('0'); TempStringResult := IntToStr(ActiveResult^.TimeOnSpectator); Result := PChar(TempStringResult); end;

function Get_Result_Multi_Accounts_Count(): Integer; cdecl;
begin if ActiveResult = nil then Exit(0); Result := ActiveResult^.MultiAccountsCount; end;

function Get_Result_Item_Similarity(ItemIdx: Integer): Integer; cdecl;
begin if ActiveResult = nil then Exit(0); if (ItemIdx >= 0) and (ItemIdx < ActiveResult^.ResultCount) then Result := ActiveResult^.Results[ItemIdx].Similarity else Result := 0; end;

{ reads settings.ini; missing file = the values above, and the script is told about it }
