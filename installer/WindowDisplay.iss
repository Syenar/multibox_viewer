; Inno Setup script - Stage 7 unified installer scaffold.
; Build with Inno Setup 6 after tools\build.ps1 -Configuration Release.

#define MyAppName "WindowDisplay"
#define MyAppVersion "1.0.0"
#define MyAppPublisher "WindowDisplay"
#define MyAppExeName "WindowDisplay.exe"

[Setup]
AppId={{8F3C2A91-6B4E-4D17-9C8A-1E5F0D2B7A44}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\WindowDisplay
DefaultGroupName=WindowDisplay
DisableProgramGroupPage=yes
OutputDir=..\artifacts\installer
OutputBaseFilename=WindowDisplaySetup
Compression=lzma
SolidCompression=yes
PrivilegesRequired=admin
ArchitecturesInstallIn64BitMode=x64compatible
WizardStyle=modern

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a desktop icon"; GroupDescription: "Additional icons:"

[Files]
Source: "..\artifacts\Release\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "powershell.exe"; Parameters: "-NoProfile -ExecutionPolicy Bypass -File ""{app}\install-driver.ps1"""; StatusMsg: "Installing virtual display driver..."; Flags: runhidden waituntilterminated
Filename: "{app}\{#MyAppExeName}"; Description: "Launch WindowDisplay"; Flags: nowait postinstall skipifsilent

[UninstallRun]
Filename: "powershell.exe"; Parameters: "-NoProfile -ExecutionPolicy Bypass -File ""{app}\uninstall-driver.ps1"""; RunOnceId: "UninstallDriver"; Flags: runhidden waituntilterminated
