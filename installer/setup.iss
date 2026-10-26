; The Windows installer (Inno Setup 6), built by package.ps1 from the release
; folder it assembles:
;
;   ISCC /DAppVersion=0.1.0 /DSourceDir=<release folder> /DOutputDir=<out> setup.iss
;
; Installs into Program Files by default (one admin prompt), like other games;
; the player may choose another folder. Nothing is written there afterwards:
; the launcher and the game keep their settings, logs, icon and font copy in
; Documents\2014 FIFA World Cup Brazil with the saves (launcher/src/main.cpp
; DataFolder, pc/src/data_folder.h), so any folder works. Uninstalling asks
; whether to delete that folder too (default: keep it).
;
; No icon ships (it would be the game's art): the shortcuts and the Apps list
; point at icon.ico in that folder, which the launcher makes from the player's
; own copy once the game is found (launcher/src/icon.h). Until then Windows
; shows its plain icon.

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
; The install folder's name (in Program Files).
#define InstallFolder "2014 FIFA World Cup Brazil Recompiled"
; File names.
#define AppSlug "fifa-world-cup-2014-recompiled"
; The player's folder: settings, logs, icon and saves.
#define UserFolder "{userdocs}\2014 FIFA World Cup Brazil"

[Setup]
; Identifies the app for upgrades and uninstall: never change it.
AppId={{6E0F3B52-8C1D-4F7A-9E35-2B7C1D4A9F10}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
; Shown in Windows' Apps list.
AppPublisher=hosse
AppPublisherURL=https://github.com/HossEz/fifa-world-cup-2014-recompiled
AppSupportURL=https://github.com/HossEz/fifa-world-cup-2014-recompiled/issues
AppUpdatesURL=https://github.com/HossEz/fifa-world-cup-2014-recompiled/releases
; The setup file's own Properties, Details (as the programs', installer/version.rc.in).
VersionInfoVersion={#AppVersion}
VersionInfoProductName={#AppName}
VersionInfoProductVersion={#AppVersion}
VersionInfoCompany=hosse
VersionInfoDescription={#AppName} - Setup
VersionInfoCopyright=Copyright (c) 2026 hosse. Unofficial; not affiliated with EA, FIFA or Microsoft.
DefaultDirName={autopf}\{#InstallFolder}
DefaultGroupName={#ShortcutName}
; Two pages: the notice, then the folder (first install only: an update or a
; reinstall keeps the folder chosen before, and a silent update shows no pages
; at all). No welcome, shortcut or summary pages, and no last page: the
; launcher opens by itself, at its "find your game" screen.
DisableWelcomePage=yes
DisableDirPage=auto
DisableProgramGroupPage=yes
DisableReadyPage=yes
DisableFinishedPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
InfoBeforeFile={#SourceDir}\NOTICE.txt
OutputDir={#OutputDir}
OutputBaseFilename={#AppSlug}-{#AppVersion}-setup
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={#UserFolder}\icon.ico
UninstallDisplayName={#AppName}

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs

[Icons]
Name: "{autoprograms}\{#ShortcutName}"; Filename: "{app}\Launcher.exe"; IconFilename: "{#UserFolder}\icon.ico"
Name: "{autodesktop}\{#ShortcutName}"; Filename: "{app}\Launcher.exe"; IconFilename: "{#UserFolder}\icon.ico"

[Run]
; As the player, not as the installer's admin: an elevated window couldn't
; take a folder or .iso dropped from Explorer, and its files would be the
; admin's.
Filename: "{app}\Launcher.exe"; Flags: nowait skipifsilent runasoriginaluser
; An update (the launcher runs this setup silently, launcher/src/updater.h):
; the launcher closed itself for it, so start it again.
Filename: "{app}\Launcher.exe"; Flags: nowait runasoriginaluser; Check: WizardSilent

[Code]
// Uninstalling asks whether the player's folder goes too (saves, settings,
// logs, shader cache, icon). Default No, so clicking through keeps the saves.
// Silent uninstalls keep it; updates don't uninstall at all.
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Folder: String;
begin
  if (CurUninstallStep = usPostUninstall) and not UninstallSilent then
  begin
    Folder := ExpandConstant('{#UserFolder}');
    if DirExists(Folder) and
       (MsgBox('Also delete your saves and settings?' + #13#10#13#10 + Folder + #13#10#13#10 +
               'Choose No to keep them for a later install.',
               mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES) then
      DelTree(Folder, True, True, True);
  end;
end;
