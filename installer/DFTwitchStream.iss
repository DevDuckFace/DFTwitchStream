; ===========================================================================
;  DF Twitch Stream - Inno Setup script
;
;  Build with:   ISCC.exe installer\DFTwitchStream.iss
;  or simply:    build.bat installer
;
;  The wizard runs in English or Brazilian Portuguese; the language picker is
;  the first thing the user sees.
; ===========================================================================

#define AppName        "DF Twitch Stream"
#define AppShortName   "DFTwitchStream"
#define AppVersion     "1.0.0"
#define AppPublisher   "DF"
#define AppExeName     "DFTwitchStream.exe"
#define AppURL         "https://github.com/"
#define SourceDir      "..\dist"

; Fail with something readable when the application has not been staged yet,
; instead of Inno's generic "file not found" on the first [Files] entry.
#if !FileExists(AddBackslash(SourcePath) + SourceDir + "\" + AppExeName)
  #error Nothing to package. Run build.bat first, then build.bat installer.
#endif
#if !DirExists(AddBackslash(SourcePath) + SourceDir + "\plugins")
  #error The VLC runtime is missing from dist\plugins. Re-run build.bat.
#endif

; Echoed while compiling so the destination is never in doubt.
#pragma message "Installer output -> " + AddBackslash(SourcePath) + SourceDir

[Setup]
AppId={{7C4B1E92-3A6D-4F58-9B21-0D5E8A73C4F1}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}
AppUpdatesURL={#AppURL}

DefaultDirName={autopf}\{#AppShortName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
AllowNoIcons=yes

; Per-user installs need no elevation; the user picks in the wizard.
PrivilegesRequiredOverridesAllowed=dialog commandline
PrivilegesRequired=lowest

OutputDir=..\dist
OutputBaseFilename={#AppShortName}-{#AppVersion}-setup
SetupIconFile=..\res\app.ico
UninstallDisplayIcon={app}\{#AppExeName}
UninstallDisplayName={#AppName}

Compression=lzma2/max
SolidCompression=yes
InternalCompressLevel=max

WizardStyle=modern
WizardSizePercent=110
DisableWelcomePage=no
ShowLanguageDialog=yes

ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0

LicenseFile=..\LICENSE

; ---------------------------------------------------------------------------
[Languages]
Name: "english";    MessagesFile: "compiler:Default.isl"
Name: "portuguese"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"

; ---------------------------------------------------------------------------
[CustomMessages]
english.AppDescription=Browse Twitch channels, watch streams with chat beside the video, and recover or download old VODs.
english.CreateDesktopIcon=Create a &desktop shortcut
english.LaunchApp=Launch {#AppName}
english.OptionalTools=Optional external tools
english.ToolsInfo=Video playback works out of the box: the VLC engine is installed with the app. These extras are optional - ffmpeg converts downloads to MP4, and mpv is an alternative video engine.
english.VlcBundled=The VLC video engine is included and needs no separate install.
english.OpenMpvPage=Open the mpv download page (optional)
english.OpenFfmpegPage=Open the ffmpeg download page (recommended)
english.MpvFound=mpv was detected on this computer.
english.MpvMissing=mpv was not found. It is only needed if you switch engines.
english.FfmpegFound=ffmpeg was detected on this computer.
english.FfmpegMissing=ffmpeg was not found. Downloads will be kept as .ts files.
english.RemoveSettings=Also delete settings, saved session and logs
english.StillRunning={#AppName} is running. Please close it and try again.

portuguese.AppDescription=Explore canais da Twitch, assista com o chat ao lado do vídeo e recupere ou baixe VODs antigos.
portuguese.CreateDesktopIcon=Criar atalho na área de &trabalho
portuguese.LaunchApp=Executar o {#AppName}
portuguese.OptionalTools=Ferramentas externas opcionais
portuguese.ToolsInfo=A reprodução de vídeo já funciona: o motor do VLC é instalado junto com o aplicativo. Os itens abaixo são opcionais - o ffmpeg converte os downloads em MP4 e o mpv é um motor de vídeo alternativo.
portuguese.VlcBundled=O motor de vídeo VLC vem incluído e não precisa de instalação separada.
portuguese.OpenMpvPage=Abrir a página de download do mpv (opcional)
portuguese.OpenFfmpegPage=Abrir a página de download do ffmpeg (recomendado)
portuguese.MpvFound=O mpv foi encontrado neste computador.
portuguese.MpvMissing=O mpv não foi encontrado. Ele só é necessário se você trocar de motor.
portuguese.FfmpegFound=O ffmpeg foi encontrado neste computador.
portuguese.FfmpegMissing=O ffmpeg não foi encontrado. Os downloads ficarão como arquivos .ts.
portuguese.RemoveSettings=Excluir também as configurações, a sessão salva e os registros
portuguese.StillRunning=O {#AppName} está em execução. Feche-o e tente novamente.

; ---------------------------------------------------------------------------
[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "openffmpeg";  Description: "{cm:OpenFfmpegPage}";    GroupDescription: "{cm:OptionalTools}"
Name: "openmpv";     Description: "{cm:OpenMpvPage}";       GroupDescription: "{cm:OptionalTools}"; Flags: unchecked

; ---------------------------------------------------------------------------
[Files]
Source: "{#SourceDir}\{#AppExeName}"; DestDir: "{app}"; Flags: ignoreversion
; Bundled VLC engine (LGPL, shipped unmodified) and its codec plugins.
Source: "{#SourceDir}\libvlc.dll";     DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\libvlccore.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\plugins\*";      DestDir: "{app}\plugins"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceDir}\assets\*";      DestDir: "{app}\assets"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
Source: "..\README.md";               DestDir: "{app}"; Flags: ignoreversion isreadme skipifsourcedoesntexist
Source: "..\LICENSE";                 DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist

; ---------------------------------------------------------------------------
[Icons]
Name: "{group}\{#AppName}";           Filename: "{app}\{#AppExeName}"; Comment: "{cm:AppDescription}"
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}";     Filename: "{app}\{#AppExeName}"; Tasks: desktopicon; Comment: "{cm:AppDescription}"

; ---------------------------------------------------------------------------
[Run]
Filename: "{app}\{#AppExeName}"; Description: "{cm:LaunchApp}"; Flags: nowait postinstall skipifsilent
Filename: "https://mpv.io/installation/";       Flags: shellexec nowait postinstall skipifsilent; Tasks: openmpv;    Description: "{cm:OpenMpvPage}"
Filename: "https://ffmpeg.org/download.html";   Flags: shellexec nowait postinstall skipifsilent; Tasks: openffmpeg; Description: "{cm:OpenFfmpegPage}"

; ---------------------------------------------------------------------------
[UninstallDelete]
Type: filesandordirs; Name: "{app}\assets"
Type: filesandordirs; Name: "{app}\plugins"

; ---------------------------------------------------------------------------
[Code]
var
  ToolsPage: TOutputMsgMemoWizardPage;

// Looks for an executable on PATH so the wizard can tell the user what is
// already installed instead of making them guess.
function FindOnPath(const ExeName: string): Boolean;
var
  Dummy: string;
begin
  Result := False;
  if RegQueryStringValue(HKEY_LOCAL_MACHINE,
       'SOFTWARE\Microsoft\Windows\CurrentVersion\App Paths\' + ExeName,
       '', Dummy) then
  begin
    Result := True;
    Exit;
  end;
  // Fall back to a plain PATH search via cmd.
  Result := FileExists(ExpandConstant('{sys}\..\..\..\') + ExeName);
end;

function ToolStatusText(): string;
var
  Lines: string;
begin
  Lines := ExpandConstant('{cm:ToolsInfo}') + #13#10#13#10;
  Lines := Lines + '  [ok]  ' + ExpandConstant('{cm:VlcBundled}') + #13#10;

  if FindOnPath('mpv.exe') then
    Lines := Lines + '  [ok]  ' + ExpandConstant('{cm:MpvFound}') + #13#10
  else
    Lines := Lines + '  [ ! ] ' + ExpandConstant('{cm:MpvMissing}') + #13#10;

  if FindOnPath('ffmpeg.exe') then
    Lines := Lines + '  [ok]  ' + ExpandConstant('{cm:FfmpegFound}')
  else
    Lines := Lines + '  [ ! ] ' + ExpandConstant('{cm:FfmpegMissing}');

  Result := Lines;
end;

procedure InitializeWizard();
begin
  ToolsPage := CreateOutputMsgMemoPage(
    wpSelectTasks,
    ExpandConstant('{cm:OptionalTools}'),
    ExpandConstant('{cm:AppDescription}'),
    '',
    ToolStatusText());
end;

// Refuse to overwrite files while the app has them open.
function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := '';
  if CheckForMutexes('DFTwitchStreamRunning') then
    Result := ExpandConstant('{cm:StillRunning}');
end;

// Offer to remove the per-user data directory on uninstall.
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  DataDir: string;
begin
  if CurUninstallStep = usPostUninstall then
  begin
    DataDir := ExpandConstant('{localappdata}\DFTwitchStream');
    if DirExists(DataDir) then
      if MsgBox(ExpandConstant('{cm:RemoveSettings}'), mbConfirmation, MB_YESNO) = IDYES then
        DelTree(DataDir, True, True, True);
  end;
end;
