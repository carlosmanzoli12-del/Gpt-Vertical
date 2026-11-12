#define MyAppName "Vertical Canvas Lite"
#define MyAppVersion "0.2.0"
#define MyAppPublisher "Vertical Canvas Lite"

[Setup]
AppId={{8D40DFF7-6D76-4A0C-9D45-CA0A51000002}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\obs-studio
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
PrivilegesRequired=admin
OutputDir=..\dist
OutputBaseFilename=VerticalCanvasLite-Setup-x64
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
Uninstallable=yes
CloseApplications=yes

[Languages]
Name: "brazilianportuguese"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"

[Tasks]
Name: "desktopicon"; Description: "Criar atalho para abrir o OBS Studio"; GroupDescription: "Atalhos:"; Flags: unchecked

[Files]
Source: "..\build\Release\vertical-canvas-lite.dll"; DestDir: "{app}\obs-plugins\64bit"; Flags: ignoreversion

[Icons]
Name: "{autodesktop}\OBS Studio (Vertical Canvas Lite)"; Filename: "{app}\bin\64bit\obs64.exe"; Tasks: desktopicon


[Code]
function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;

  if CurPageID = wpSelectDir then
  begin
    if not FileExists(ExpandConstant('{app}\bin\64bit\obs64.exe')) then
    begin
      MsgBox(
        'OBS Studio nao encontrado nesta pasta. Selecione a pasta correta do OBS 64 bits.',
        mbError,
        MB_OK
      );
      Result := False;
    end;
  end;
end;

