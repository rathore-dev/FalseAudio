; Inno Setup 6 script. The exe is self-contained (static CRT, gear DB embedded): only exe + README are installed.
; Normally compiled by package.ps1, which stages dist\FalseAudio first
; and passes /DAppVersion=x.y.z. Manual: ISCC /DAppVersion=0.1.0 installer\FalseAudio.iss
#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif
#define StageDir "..\dist\FalseAudio"

[Setup]
AppId={{B7A4C1D2-5E3F-4A86-9C21-FA15E0A0D10F}
AppName=FalseAudio
AppVersion={#AppVersion}
AppPublisher=FalseAudio
DefaultDirName={autopf}\FalseAudio
DefaultGroupName=FalseAudio
UninstallDisplayIcon={app}\FalseAudio.exe
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..\dist
OutputBaseFilename=FalseAudio-{#AppVersion}-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
#if FileExists("..\resources\icon\FalseAudio.ico")
SetupIconFile=..\resources\icon\FalseAudio.ico
#endif

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; Flags: unchecked

[Files]
Source: "{#StageDir}\FalseAudio.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\README.txt"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\FalseAudio"; Filename: "{app}\FalseAudio.exe"
Name: "{group}\Uninstall FalseAudio"; Filename: "{uninstallexe}"
Name: "{autodesktop}\FalseAudio"; Filename: "{app}\FalseAudio.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\FalseAudio.exe"; Description: "Launch FalseAudio"; Flags: nowait postinstall skipifsilent
