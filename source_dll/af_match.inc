{ ---------------------------------------------------------------------------------------
  AntiFake Extended - how alike two names are, and how alike two accounts are.
  --------------------------------------------------------------------------------------- }

function CalculateSimilarity(Source, Target: string): Integer;
var
  S1, S2: string;
  Dist, MaxLen: Integer;
begin
  S1 := SanitizeString(Source);
  S2 := SanitizeString(Target);

  if (S1 = '') and (S2 = '') then Exit(100);
  if (S1 = '') or (S2 = '') then Exit(0);

  MaxLen := Max(Length(S1), Length(S2));
  Dist := LevenshteinDistance(S1, S2);
  Result := Round((1.0 - (Dist / MaxLen)) * 100.0);
end;

function JaroWinkler(S1, S2: string): Integer;
var
  Len1, Len2, MatchWindow, Matches, Transpositions, Prefix: Integer;
  S1Matches, S2Matches: array of Boolean;
  i, j, k: Integer;
  M, T, Jaro: Double;
begin
  Len1 := Length(S1);
  Len2 := Length(S2);
  if (Len1 = 0) and (Len2 = 0) then Exit(100);
  if (Len1 = 0) or (Len2 = 0) then Exit(0);

  MatchWindow := Max(0, (Max(Len1, Len2) div 2) - 1);
  Matches := 0;
  Transpositions := 0;
  Prefix := 0;

  S1Matches := nil;
  S2Matches := nil;
  SetLength(S1Matches, Len1 + 1);
  SetLength(S2Matches, Len2 + 1);
  for i := 0 to Len1 do S1Matches[i] := False;
  for j := 0 to Len2 do S2Matches[j] := False;

  for i := 1 to Len1 do begin
    for j := Max(1, i - MatchWindow) to Min(Len2, i + MatchWindow) do begin
      if (not S2Matches[j]) and (S1[i] = S2[j]) then begin
        S1Matches[i] := True;
        S2Matches[j] := True;
        Inc(Matches);
        Break;
      end;
    end;
  end;

  if Matches = 0 then Exit(0);

  k := 1;
  for i := 1 to Len1 do begin
    if S1Matches[i] then begin
      while not S2Matches[k] do Inc(k);
      if S1[i] <> S2[k] then Inc(Transpositions);
      Inc(k);
    end;
  end;

  M := Matches;
  T := Transpositions / 2;
  Jaro := ((M / Len1) + (M / Len2) + ((M - T) / M)) / 3.0;

  for i := 1 to Min(4, Min(Len1, Len2)) do
    if S1[i] = S2[i] then Inc(Prefix) else Break;

  Result := Round((Jaro + (Prefix * 0.1 * (1.0 - Jaro))) * 100.0);
end;

{ the words of a name, sorted and put back together: "red wolf" and "wolf red" are the same
  player, "abc" and "cba" are not (sorting the letters made every anagram a perfect match) }
function SortTokens(const S: string): string;
var
  Parts: TStringList;
  k: Integer;
begin
  Result := '';
  Parts := TStringList.Create;
  try
    Parts.Delimiter := ' ';
    Parts.StrictDelimiter := False;
    Parts.DelimitedText := S;
    Parts.Sort;
    for k := 0 to Parts.Count - 1 do
      if Parts[k] <> '' then
      begin
        if Result <> '' then Result := Result + ' ';
        Result := Result + Parts[k];
      end;
  finally
    Parts.Free;
  end;
end;

function TokenSortRatio(S1, S2: string): string;
begin
  S1 := SanitizeString(S1);
  S2 := SanitizeString(S2);

  if (S1 = '') and (S2 = '') then begin Result := '100'; Exit; end;
  if (S1 = '') or (S2 = '') then begin Result := '0'; Exit; end;

  Result := IntToStr(CalculateSimilarity(SortTokens(S1), SortTokens(S2)));
end;

function CalculateSimilarityEx(Source, Target: string; AlgoType: Integer): Integer;
begin
  case AlgoType of
    ALGO_JARO_WINKLER:
      Result := JaroWinkler(SanitizeString(Source), SanitizeString(Target));
    ALGO_TOKEN_SORT:
      Result := StrToIntDef(TokenSortRatio(Source, Target), 0);
    else
      Result := CalculateSimilarity(Source, Target);
  end;
end;

{ "Poland (ISO: PL)" once the address of a joining player is located, 'Locating...' while its
  lookup runs, 'Unknown' when nobody asked (or the place has no country), '' for a local one.
  The script asks every tick until it has an answer. }
function Get_Geo_Join_String(IP: PChar): PChar; cdecl;
var
  Idx: Integer;
  SIP: string;
begin
  TempStringResult := '';
  try
    SIP := string(IP);
    { local players get no join location; the script stops asking after its timeout }
    if IsLocalAddress(SIP) then
      TempStringResult := ''
    { asked every tick while the location is on its way: a busy cache is asked again next tick }
    else if not GrabLock(GeoCritSec, 1) then
      TempStringResult := 'Locating...'
    else
      try
        Idx := FindGeoIndex(SIP);
        if Idx <> -1 then
        begin
          { a place without a country said " (ISO: )" to everybody on the server }
          if string(GeoCache[Idx].Country) = '' then
            TempStringResult := 'Unknown'
          else if string(GeoCache[Idx].CountryCode) = '' then
            TempStringResult := string(GeoCache[Idx].Country)
          else
            TempStringResult := string(GeoCache[Idx].Country) + ' (ISO: ' + string(GeoCache[Idx].CountryCode) + ')';
        end
        { drawing a table never starts a lookup: the join asks for an address once, and a page
          that is redrawn (or paged through) would otherwise ask again and again }
        else if GeoPending.IndexOf(SIP) >= 0 then
          TempStringResult := 'Locating...'
        else
          TempStringResult := 'Unknown';
      finally
        LeaveCriticalSection(GeoCritSec);
      end;
  except
    ExportFailed('Get_Geo_Join_String');
    { an answer that ends the waiting of the script: nothing is said about this join }
    TempStringResult := 'Unknown';
  end;
  Result := PChar(TempStringResult);
end;

{ S1885850061 -> 76561198146115789, and a 64-bit number is left as it is }
function SteamToId64(const S: string): string;
var
  T: string;
  V: Int64;
begin
  Result := '';
  T := Trim(S);
  if T = '' then Exit;
  if (T[1] = 'S') or (T[1] = 's') then Delete(T, 1, 1);
  if (T = '') or not TryStrToInt64(T, V) then Exit;
  if V <= 0 then Exit;
  if V < STEAM_ID_BASE then V := V + STEAM_ID_BASE;
  Result := IntToStr(V);
end;

{ and back, into the form the profiles are keyed by }
function Id64ToSteam(const S: string): string;
var
  V: Int64;
begin
  Result := '';
  if not TryStrToInt64(Trim(S), V) then Exit;
  if V > STEAM_ID_BASE then V := V - STEAM_ID_BASE;
  Result := 'S' + IntToStr(V);
end;

{ Steam answers over HTTPS and this library has no TLS of its own, so the question goes
  through curl, the way ZitroStats sends its mail. Windows 10 and every current Linux have it. }
