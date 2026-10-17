; The Windows installer (Inno Setup 6), built by package.ps1 from the release
; folder it assembles:
;
;   ISCC /DAppVersion=0.1.0 /DSourceDir=<release folder> /DOutputDir=<out> setup.iss
;
; Installs for the current user only (no admin prompt), under
; %LOCALAPPDATA%\Programs, where the launcher and the game can write their
; settings and logs next to themselves. Saves stay in
; Documents\2014 FIFA World Cup Brazil, also after an uninstall.
;
; No icon ships (it would be the game's art): the shortcuts and the Apps list
; point at icon.ico, which the launcher makes from the player's own copy once
; the game is found (launcher/src/icon.h). Until then Windows shows its plain icon.

#ifndef AppVersion
  #error Pass /DAppVersion=x.y.z (package.ps1 does)
#endif
#ifndef SourceDir
  #error Pass /DSourceDir=<release folder> (package.ps1 does)
#endif
#ifndef OutputDir
  #define OutputDir "."
#endif
; The name shown in Windows (Apps list) and the installer.
#define AppName "2014 FIFA World Cup Brazil: Recompiled"
; The shortcuts' file names: the same without the colon, which Windows doesn't
; allow in file names.
#define ShortcutName "2014 FIFA World Cup Brazil - Recompiled"
; Folder and file names.
#define AppSlug "fifa-world-cup-2014-recompiled"

[Setup]
; Identifies the app for upgrades and uninstall: never change it.
AppId={{6E0F3B52-8C1D-4F7A-9E35-2B7C1D4A9F10}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
DefaultDirName={localappdata}\Programs\{#AppSlug}
DefaultGroupName={#ShortcutName}
; One page: the notice with an Install button. No welcome, folder, shortcut
; or summary pages (the default folder suits everyone), and no last page: the
; launcher opens by itself, at its "find your game" screen.
DisableWelcomePage=yes
DisableDirPage=yes
DisableProgramGroupPage=yes
DisableReadyPage=yes
DisableFinishedPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
InfoBeforeFile={#SourceDir}\NOTICE.txt
OutputDir={#OutputDir}
OutputBaseFilename={#AppSlug}-{#AppVersion}-setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\icon.ico
UninstallDisplayName={#AppName}

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs

[Icons]
Name: "{autoprograms}\{#ShortcutName}"; Filename: "{app}\Launcher.exe"; IconFilename: "{app}\icon.ico"
Name: "{autodesktop}\{#ShortcutName}"; Filename: "{app}\Launcher.exe"; IconFilename: "{app}\icon.ico"

[Run]
Filename: "{app}\Launcher.exe"; Flags: nowait skipifsilent

[UninstallDelete]
; What the launcher and the game wrote next to themselves (not the saves).
Type: files; Name: "{app}\fifawc14.toml"
Type: files; Name: "{app}\launcher.toml"
Type: files; Name: "{app}\icon.ico"
Type: files; Name: "{app}\futurastd-condensed.ttf"
Type: files; Name: "{app}\*.log"
Type: filesandordirs; Name: "{app}\logs"
