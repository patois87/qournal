; Inno Setup script for the Windows installer.
;
; Expects the folder "package" at the root of the repository, with qournal.exe, the files windeployqt put
; next to it and the folder "plugins". Build with:
;   iscc /DAppVersion=1.0.0 /DArch=x64 packaging\windows\qournal.iss
; Arch is x64 (the default) or arm64, as the program in "package" was built.
; Written for Inno Setup 7 (6.3 and newer work too).
; The installer is written to the root of the repository.

#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#ifndef Arch
  #define Arch "x64"
#endif
#if Arch == "arm64"
  #define Allowed "arm64"
#else
  #define Allowed "x64compatible"
#endif
#define AppName "Qournal"
#define AppExe "qournal.exe"

[Setup]
; The id identifies the application for updates and must never change
AppId={{6B1F3A52-9C0E-4D7B-8E55-2F4A7C1D9E30}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=vereo
AppPublisherURL=https://vereo.ch/software/qournal
AppSupportURL=https://github.com/patois87/qournal/issues
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
LicenseFile=..\..\LICENSE
SetupIconFile=qournal.ico
UninstallDisplayIcon={app}\{#AppExe}
OutputDir=..\..
OutputBaseFilename=qournal-{#AppVersion}-windows-{#Arch}-setup
Compression=lzma2
SolidCompression=yes
; Dark if Windows is set to dark ("dynamic", Inno Setup 6.6 and newer, so also 7); older versions know only the
; light style and would refuse the word
#if Ver >= 0x06060000
WizardStyle=modern dynamic
#else
WizardStyle=modern
#endif
ArchitecturesAllowed={#Allowed}
ArchitecturesInstallIn64BitMode={#Allowed}
; Installs for the current user without administrator rights, or for everyone if the user chooses so
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ChangesAssociations=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "german"; MessagesFile: "compiler:Languages\German.isl"
Name: "french"; MessagesFile: "compiler:Languages\French.isl"
Name: "italian"; MessagesFile: "compiler:Languages\Italian.isl"
Name: "spanish"; MessagesFile: "compiler:Languages\Spanish.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "associate"; Description: "{cm:AssocFileExtension,{#AppName},.xopp}"

[Files]
Source: "..\..\package\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Registry]
Root: HKA; Subkey: "Software\Classes\.xopp\OpenWithProgids"; ValueType: string; ValueName: "Qournal.xopp"; ValueData: ""; Flags: uninsdeletevalue; Tasks: associate
Root: HKA; Subkey: "Software\Classes\.xoj\OpenWithProgids"; ValueType: string; ValueName: "Qournal.xopp"; ValueData: ""; Flags: uninsdeletevalue; Tasks: associate
Root: HKA; Subkey: "Software\Classes\Qournal.xopp"; ValueType: string; ValueName: ""; ValueData: "Xournal++ document"; Flags: uninsdeletekey; Tasks: associate
Root: HKA; Subkey: "Software\Classes\Qournal.xopp\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#AppExe},0"; Tasks: associate
Root: HKA; Subkey: "Software\Classes\Qournal.xopp\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExe}"" ""%1"""; Tasks: associate

[Run]
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent

[Code]
{ With a high scaling of the screen (225 % for example), Inno Setup 7 cuts off the left edge of the check boxes
  on the pages "Select Additional Tasks" and "Finished". Some more indentation avoids that. }
procedure InitializeWizard;
begin
  WizardForm.TasksList.Offset := WizardForm.TasksList.Offset + ScaleX(4);
  WizardForm.RunList.Offset := WizardForm.RunList.Offset + ScaleX(4);
end;
