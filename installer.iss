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
function GetOBSDir(Param: String): String;
var
  Path: String;
begin
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
  OBSDir: String;
begin
  OBSDir := GetOBSDir('');
  if not FileExists(OBSDir + '\bin\64bit\obs64.exe') then
  begin
    MsgBox('OBS Studio was not found on this computer.' + #13#10 + #13#10 +
           'Please install OBS Studio first, then run this installer again.', mbError, MB_OK);
    Result := False;
    Exit;
  end;
  Result := True;
end;

[UninstallDelete]
Type: filesandordirs; Name: "{app}\data\obs-plugins\obs-squeezeback"
