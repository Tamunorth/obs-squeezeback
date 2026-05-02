[Setup]
AppName=OBS Squeezeback
AppVersion=1.0.0
AppPublisher=Tamunorth
AppPublisherURL=https://github.com/Tamunorth/obs-squeezeback
DefaultDirName={code:GetOBSDir}
DisableDirPage=yes
DisableProgramGroupPage=yes
OutputDir=dist
OutputBaseFilename=obs-squeezeback-1.0.0-setup
Compression=lzma2
SolidCompression=yes
UninstallDisplayName=OBS Squeezeback Plugin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
SetupIconFile=compiler:SetupClassicIcon.ico
WizardStyle=modern

[Messages]
WelcomeLabel1=OBS Squeezeback Plugin
WelcomeLabel2=This will install the Squeezeback zoom filter for OBS Studio.%n%nThe plugin adds a "Squeezeback Zoom" filter that you can apply to any scene. It zooms into your video source area and smoothly zooms out to reveal your full layout (L-shape graphics).%n%nDefault hotkey: F9

[Files]
; Plugin DLL
Source: "build\Release\obs-squeezeback.dll"; DestDir: "{app}\obs-plugins\64bit"; Flags: ignoreversion

; Effect shaders
Source: "data\squeezeback.effect"; DestDir: "{app}\data\obs-plugins\obs-squeezeback"; Flags: ignoreversion
Source: "data\squeezeback_filter.effect"; DestDir: "{app}\data\obs-plugins\obs-squeezeback"; Flags: ignoreversion

; Locale
Source: "data\locale\en-US.ini"; DestDir: "{app}\data\obs-plugins\obs-squeezeback\locale"; Flags: ignoreversion

[Code]
var
  OBSPathCache: String;

function ValidateOBSDir(const Path: String): Boolean;
begin
  Result := FileExists(Path + '\bin\64bit\obs64.exe');
end;

function GetOBSDir(Param: String): String;
var
  Path: String;
begin
  { If the user picked a folder manually during InitializeSetup, use it. }
  if OBSPathCache <> '' then
  begin
    Result := OBSPathCache;
    Exit;
  end;

  { Try registry first }
  if RegQueryStringValue(HKLM, 'SOFTWARE\OBS Studio', '', Path) then
  begin
    Result := Path;
    Exit;
  end;

  { Try common install paths }
  if DirExists(ExpandConstant('{pf}\obs-studio')) then
  begin
    Result := ExpandConstant('{pf}\obs-studio');
    Exit;
  end;

  { Fallback }
  Result := ExpandConstant('{pf}\obs-studio');
end;

function InitializeSetup(): Boolean;
var
  AutoDir: String;
  ChosenDir: String;
begin
  AutoDir := GetOBSDir('');
  if ValidateOBSDir(AutoDir) then
  begin
    Result := True;
    Exit;
  end;

  { Auto-detect failed. Offer a manual browse for portable or custom installs. }
  if MsgBox('OBS Studio was not found in the usual locations.' + #13#10 + #13#10 +
            'If OBS is installed in a custom or portable folder, click Yes and pick its folder ' +
            '(the one that contains bin\64bit\obs64.exe).' + #13#10 + #13#10 +
            'Otherwise click No, install OBS Studio first, then run this installer again.',
            mbConfirmation, MB_YESNO) <> IDYES then
  begin
    Result := False;
    Exit;
  end;

  ChosenDir := AutoDir;
  while True do
  begin
    if not BrowseForFolder(
      'Select your OBS Studio installation folder. It should contain bin\64bit\obs64.exe.',
      ChosenDir, False) then
    begin
      Result := False;
      Exit;
    end;

    if ValidateOBSDir(ChosenDir) then
    begin
      OBSPathCache := ChosenDir;
      Result := True;
      Exit;
    end;

    if MsgBox('That folder does not contain bin\64bit\obs64.exe.' + #13#10 + #13#10 +
              'Pick a different folder?', mbConfirmation, MB_YESNO) <> IDYES then
    begin
      Result := False;
      Exit;
    end;
  end;
end;

[UninstallDelete]
Type: filesandordirs; Name: "{app}\data\obs-plugins\obs-squeezeback"
