#define ProductName "LYwintabs"
#define PackageVersion "0.1.0-preview.28"
#define PluginName "LYwintabs.Explorer.dll"
#define LauncherName "LYwintabs.Launcher.exe"
#define TransferPath "Transfer\LYwintabs.Transfer.exe"
#define PayloadPath "{app}\" + BuildTag
#define PluginHash GetSHA256OfFile(PluginDll)
#define LauncherHash GetSHA256OfFile(LauncherExe)
#define TransferHash GetSHA256OfFile(TransferDir + "\LYwintabs.Transfer.exe")
#define LegacyClassId "{4A917ED1-591A-4EB4-AFF4-262CDCAB2152}"
#define PreviousLayoutClassId "{FB715715-57EA-4CE3-AC04-3E2C14B099BF}"
#define PreviousSettingsClassId "{EC31D207-DA99-4916-BED9-BEC162D4B56F}"
#define PreviousAutoAttachClassId "{8F6DA47F-2391-4BBA-BBA1-746EACC4490D}"
#define PreviousToolbarClassId "{D22BB869-3DF2-44DC-A611-CE723699C654}"
#define PreviousBookmarkClassId "{05748C5D-BD1D-48E9-BD02-E0E813DCBBBD}"
#define PreviousBookmarkDragClassId "{AD1DB7CE-F07D-4454-9BEE-942F1F4B9721}"
#define PreviousBookmarkEditClassId "{43F93CCE-6AEE-440B-BBA0-2EBAF6D6A483}"
#define PreviousBookmarkStyleClassId "{A8E78474-6548-4A5E-90B9-88F1B3EE79AE}"
#define PreviousAdaptiveTabsClassId "{39C4ABA3-848E-4C04-B820-B823C7FA3965}"
#define PreviousBrowserUiClassId "{D1FBC879-9A83-4D9F-957F-30E19FB22C4F}"
#define PreviousLucideIconsClassId "{E3EE511E-6945-4FEE-B8C5-59CCF27BBAF5}"
#define PreviousUnifiedCaptionClassId "{7FCCD37B-856B-4F7E-BFC2-4B576D7B5845}"
#define PreviousPluginThemeClassId "{E8A264B0-F03D-4E6A-80AB-065109BD6A00}"
#define PreviousWindowsThemeClassId "{E9EC36F3-BBDB-4C53-9DCA-C6272974484A}"
#define PreviousLanTransferClassId "{A063976E-0AA6-44C1-8F23-5CC6E234C22D}"
#define PreviousDirectoryClassId "{2CFDF0B9-EFBB-423D-9788-D64D8C975A18}"
#define PreviousMacStyleClassId "{7B26EEF3-CAE7-415B-82B8-7C057486EA1D}"
#define PreviousPaintFixClassId "{3986B961-77E7-4AD2-AC03-F8355ADB5037}"
#define PreviousBackgroundTransferClassId "{1C7AF264-1765-4C30-87A5-B529D0CF7A36}"
#define PreviousTransferPopupClassId "{C980E45A-0E68-42E9-A07F-4FDF0B0CD70D}"
#define PreviousForegroundClassId "{7F163C25-1177-4B55-996E-3D066B0FE911}"
#define ClassId "{B5ABC93E-9B6E-4A71-8AAA-2DE01C5A918B}"
#define ClassKey "Software\Classes\CLSID\" + ClassId
#define LiteralClassId "{" + ClassId
#define LiteralClassKey "Software\Classes\CLSID\" + LiteralClassId
#define CommandKey "Software\Classes\Directory\Background\shell\LYwintabs.FavoritesBar"
#define StartupKey "Software\Microsoft\Windows\CurrentVersion\Run"
#define StartupName "LYwintabs"
#define ShortcutName "LYwintabs 文件资源管理器"

[Setup]
AppId={code:InstallAppId}
AppName={#ProductName}
AppVersion={#PackageVersion}
LicenseFile=..\EULA.txt
DefaultDirName={localappdata}\Programs\LYwintabs
UninstallDisplayName=LYwintabs Explorer Extension
UninstallDisplayIcon={#PayloadPath}\{#LauncherName}
SetupIconFile=..\app\LYwintabs.ico
OutputBaseFilename=LYwintabs-Win10-x64-{#PackageVersion}-Setup
ArchitecturesAllowed=x64compatible and not arm64
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.19045
PrivilegesRequired=lowest
UsePreviousAppDir=no
UsePreviousLanguage=no
DisableDirPage=no
DisableProgramGroupPage=yes
ChangesAssociations=yes
SetupLogging=yes
CloseApplications=no
RestartApplications=no
WizardStyle=modern

[Messages]
WizardLicense=最终用户许可协议（EULA）
LicenseLabel3=请阅读以下许可协议。选择「我同意此协议」后可继续安装。
LicenseAccepted=我同意此协议(&A)
LicenseNotAccepted=我不同意此协议(&D)

[Files]
Source: "{#PluginDll}"; DestDir: "{#PayloadPath}"; DestName: "{#PluginName}"; Flags: ignoreversion uninsrestartdelete; Check: NeedsInstalledFileCopy('{#PluginName}', '{#PluginHash}')
Source: "{#LauncherExe}"; DestDir: "{#PayloadPath}"; DestName: "{#LauncherName}"; Flags: ignoreversion; Check: NeedsInstalledFileCopy('{#LauncherName}', '{#LauncherHash}')
Source: "{#TransferDir}\*"; DestDir: "{#PayloadPath}\Transfer"; Excludes: "*.pdb"; Flags: ignoreversion recursesubdirs
Source: "..\EULA.txt"; DestDir: "{#PayloadPath}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{#PayloadPath}"; Flags: ignoreversion
Source: "..\THIRD-PARTY-NOTICES.md"; DestDir: "{#PayloadPath}"; Flags: ignoreversion
Source: "assets\Lucide-ISC.txt"; DestDir: "{#PayloadPath}\Licenses"
Source: "assets\Feather-MIT.txt"; DestDir: "{#PayloadPath}\Licenses"
Source: "assets\Fluent-UI-System-Icons-MIT.txt"; DestDir: "{#PayloadPath}\Licenses"
Source: "assets\Inno-Setup-License.txt"; DestDir: "{#PayloadPath}\Licenses"

[Registry]
Root: HKCU; Subkey: "{#LiteralClassKey}"; ValueType: string; ValueData: "LYwintabs Explorer Favorites Bar"
Root: HKCU; Subkey: "{#LiteralClassKey}\InprocServer32"; ValueType: string; ValueData: "{#PayloadPath}\{#PluginName}"
Root: HKCU; Subkey: "{#LiteralClassKey}\InprocServer32"; ValueType: string; ValueName: "ThreadingModel"; ValueData: "Apartment"
Root: HKCU; Subkey: "{#CommandKey}"; ValueType: string; ValueData: "LYwintabs 收藏栏"
Root: HKCU; Subkey: "{#CommandKey}"; ValueType: string; ValueName: "ExplorerCommandHandler"; ValueData: "{#LiteralClassId}"

[Run]
Filename: "{#PayloadPath}\{#LauncherName}"; Parameters: "--stop"; Flags: waituntilterminated runhidden runasoriginaluser
Filename: "{#PayloadPath}\{#LauncherName}"; Parameters: "--open"; Flags: nowait skipifsilent runasoriginaluser
Filename: "{#PayloadPath}\{#LauncherName}"; Flags: nowait skipifnotsilent runasoriginaluser

[Icons]
Name: "{userprograms}\{#ShortcutName}"; Filename: "{#PayloadPath}\{#LauncherName}"; Parameters: "--open"; Comment: "打开已启用 LYwintabs 的资源管理器"; Flags: uninsneveruninstall

[Code]
var
  StartupBeforeUpgrade: String;
  FreshInstall: Boolean;
  PreviousPayload: String;
  AttachBlocked: Boolean;

function InstallAppId(Param: String): String;
var
  Directory: String;
begin
  Result := '';
  if WizardForm = nil then
    Exit;
  Directory := WizardDirValue();
  if Directory <> '' then
    Result := 'LYwintabs.Explorer.x64.' + GetSHA256OfUnicodeString(UpperCase(AddBackslash(ExpandFileName(Directory))));
end;

function NeedsInstalledFileCopy(const FileName, Hash: String): Boolean;
var
  Target: String;
begin
  Target := AddBackslash(ExpandConstant('{#PayloadPath}')) + FileName;
  Result := True;
  if not FileExists(Target) then
    Exit;
  try
    Result := CompareText(GetSHA256OfFile(Target), Hash) <> 0;
    if not Result then
      Log('Reusing installed ' + FileName + ': SHA-256 matches ' + Hash + '.');
  except
    Log('Unable to read the SHA-256 of ' + FileName + ': ' + GetExceptionMessage);
  end;
end;

function StartupCommand(const Directory: String): String;
begin
  Result := '"' + AddBackslash(Directory) + '{#LauncherName}"';
end;

function StartupPointsToKnownClass(const Command, ClassId: String): Boolean;
var
  Dll: String;
  Directory: String;
begin
  Result := False;
  if not RegQueryStringValue(HKCU, 'Software\Classes\CLSID\' + ClassId + '\InprocServer32', '', Dll) then
    Exit;
  Directory := ExtractFileDir(Dll);
  Result := (CompareText(ExtractFileName(Dll), '{#PluginName}') = 0)
    and (Directory <> '') and (CompareText(ExpandFileName(Dll), Dll) = 0)
    and (CompareText(Command, StartupCommand(Directory)) = 0);
end;

function InitializeSetup(): Boolean;
var
  Handler: String;
  Command: String;
  Dll: String;
begin
  Result := True;
  FreshInstall := not RegKeyExists(HKCU, '{#CommandKey}');
  if not FreshInstall then begin
    Result := RegQueryStringValue(HKCU, '{#CommandKey}', 'ExplorerCommandHandler', Handler)
      and ((CompareText(Handler, '{#ClassId}') = 0)
        or (CompareText(Handler, '{#PreviousForegroundClassId}') = 0)
        or (CompareText(Handler, '{#PreviousTransferPopupClassId}') = 0)
        or (CompareText(Handler, '{#PreviousBackgroundTransferClassId}') = 0)
        or (CompareText(Handler, '{#PreviousPaintFixClassId}') = 0)
        or (CompareText(Handler, '{#PreviousMacStyleClassId}') = 0)
        or (CompareText(Handler, '{#PreviousDirectoryClassId}') = 0)
        or (CompareText(Handler, '{#PreviousLanTransferClassId}') = 0)
        or (CompareText(Handler, '{#PreviousPluginThemeClassId}') = 0)
        or (CompareText(Handler, '{#PreviousWindowsThemeClassId}') = 0)
        or (CompareText(Handler, '{#PreviousUnifiedCaptionClassId}') = 0)
        or (CompareText(Handler, '{#PreviousLucideIconsClassId}') = 0)
        or (CompareText(Handler, '{#PreviousBrowserUiClassId}') = 0)
        or (CompareText(Handler, '{#PreviousAdaptiveTabsClassId}') = 0)
        or (CompareText(Handler, '{#PreviousBookmarkStyleClassId}') = 0)
        or (CompareText(Handler, '{#PreviousBookmarkEditClassId}') = 0)
        or (CompareText(Handler, '{#PreviousBookmarkDragClassId}') = 0)
        or (CompareText(Handler, '{#PreviousBookmarkClassId}') = 0)
        or (CompareText(Handler, '{#PreviousToolbarClassId}') = 0)
        or (CompareText(Handler, '{#PreviousAutoAttachClassId}') = 0)
        or (CompareText(Handler, '{#PreviousSettingsClassId}') = 0)
        or (CompareText(Handler, '{#PreviousLayoutClassId}') = 0)
        or (CompareText(Handler, '{#LegacyClassId}') = 0));
  end;
  if not Result then begin
    MsgBox('The Explorer command belongs to a different extension.', mbError, MB_OK);
    Exit;
  end;
  if (Handler <> '')
    and RegQueryStringValue(HKCU, 'Software\Classes\CLSID\' + Handler + '\InprocServer32', '', Dll)
    and (CompareText(ExtractFileName(Dll), '{#PluginName}') = 0)
    and (CompareText(ExpandFileName(Dll), Dll) = 0)
    and FileExists(Dll) then
    PreviousPayload := ExtractFileDir(Dll);
  if RegQueryStringValue(HKCU, '{#StartupKey}', '{#StartupName}', Command)
    and (StartupPointsToKnownClass(Command, '{#ClassId}')
      or StartupPointsToKnownClass(Command, '{#PreviousForegroundClassId}')
      or StartupPointsToKnownClass(Command, '{#PreviousTransferPopupClassId}')
      or StartupPointsToKnownClass(Command, '{#PreviousBackgroundTransferClassId}')
      or StartupPointsToKnownClass(Command, '{#PreviousPaintFixClassId}')
      or StartupPointsToKnownClass(Command, '{#PreviousMacStyleClassId}')
      or StartupPointsToKnownClass(Command, '{#PreviousDirectoryClassId}')
      or StartupPointsToKnownClass(Command, '{#PreviousLanTransferClassId}')
      or StartupPointsToKnownClass(Command, '{#PreviousPluginThemeClassId}')
      or StartupPointsToKnownClass(Command, '{#PreviousWindowsThemeClassId}')
      or StartupPointsToKnownClass(Command, '{#PreviousUnifiedCaptionClassId}')
      or StartupPointsToKnownClass(Command, '{#PreviousLucideIconsClassId}')
      or StartupPointsToKnownClass(Command, '{#PreviousBrowserUiClassId}')
      or StartupPointsToKnownClass(Command, '{#PreviousAdaptiveTabsClassId}')
      or StartupPointsToKnownClass(Command, '{#PreviousBookmarkStyleClassId}')
      or StartupPointsToKnownClass(Command, '{#PreviousBookmarkEditClassId}')
      or StartupPointsToKnownClass(Command, '{#PreviousBookmarkDragClassId}')
      or StartupPointsToKnownClass(Command, '{#PreviousBookmarkClassId}')
      or StartupPointsToKnownClass(Command, '{#PreviousToolbarClassId}')
      or StartupPointsToKnownClass(Command, '{#PreviousAutoAttachClassId}')
      or StartupPointsToKnownClass(Command, '{#PreviousSettingsClassId}')
      or StartupPointsToKnownClass(Command, '{#PreviousLayoutClassId}')
      or StartupPointsToKnownClass(Command, '{#LegacyClassId}')) then
    StartupBeforeUpgrade := Command;
end;

function CloseTransferAt(const TransferExe, ExpectedHash: String): Boolean;
var
  ExitCode: Integer;
begin
  Result := True;
  if not FileExists(TransferExe) then
    Exit;
  try
    if (ExpectedHash <> '') and (CompareText(GetSHA256OfFile(TransferExe), ExpectedHash) <> 0) then
      Exit;
  except
    Log('Unable to verify the installed LAN transfer executable: ' + GetExceptionMessage);
    Result := False;
    Exit;
  end;
  ExitCode := 0;
  Result := Exec(TransferExe, '--close-if-idle', '', SW_HIDE, ewWaitUntilTerminated, ExitCode)
    and (ExitCode = 0);
  if not Result then
    Log('Unable to close this installation''s LAN transfer panel. Code: ' + IntToStr(ExitCode));
end;

function CloseTransferPanel(): Boolean;
begin
  Result := CloseTransferAt(ExpandConstant('{#PayloadPath}\{#TransferPath}'), '{#TransferHash}');
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := '';
  if ((PreviousPayload <> '') and (CompareText(PreviousPayload, ExpandConstant('{#PayloadPath}')) <> 0)
    and not CloseTransferAt(AddBackslash(PreviousPayload) + '{#TransferPath}', ''))
    or not CloseTransferPanel() then
    Result := '请完成或取消局域网传输并关闭互传面板，再继续安装。';
end;

procedure CurStepChanged(Step: TSetupStep);
var
  Command: String;
begin
  if Step <> ssPostInstall then
    Exit;
  RegDeleteValue(HKCU, '{#ClassKey}', 'UninstallPending');
  if RegValueExists(HKCU, '{#StartupKey}', '{#StartupName}') then begin
    if (StartupBeforeUpgrade = '')
      or not RegQueryStringValue(HKCU, '{#StartupKey}', '{#StartupName}', Command)
      or (CompareText(Command, StartupBeforeUpgrade) <> 0) then
      Exit;
  end else if not FreshInstall then
    Exit;

  if RegWriteStringValue(HKCU, '{#StartupKey}', '{#StartupName}', StartupCommand(ExpandConstant('{#PayloadPath}'))) then
    Log('Registered LYwintabs to launch at sign-in.')
  else
    Log('Unable to register LYwintabs to launch at sign-in.');
end;

procedure DeinitializeUninstall();
begin
  if AttachBlocked then
    RegDeleteValue(HKCU, '{#ClassKey}', 'UninstallPending');
end;

procedure CurUninstallStepChanged(Step: TUninstallStep);
var
  Command: String;
  Dll: String;
  Handler: String;
  ExitCode: Integer;
  OwnsClass: Boolean;
  OwnsMenu: Boolean;
begin
  if Step <> usUninstall then
    Exit;
  OwnsClass := RegQueryStringValue(HKCU, '{#ClassKey}\InprocServer32', '', Dll)
    and (CompareText(Dll, ExpandConstant('{#PayloadPath}\{#PluginName}')) = 0);
  OwnsMenu := OwnsClass and RegQueryStringValue(HKCU, '{#CommandKey}', 'ExplorerCommandHandler', Handler)
    and (CompareText(Handler, '{#ClassId}') = 0);
  if OwnsClass then begin
    AttachBlocked := RegWriteDWordValue(HKCU, '{#ClassKey}', 'UninstallPending', 1);
    if not AttachBlocked then begin
      Log('Unable to pause LYwintabs attachments.');
      if not UninstallSilent then
        MsgBox('无法暂停 LYwintabs 启用，卸载已停止。', mbError, MB_OK);
      Abort;
    end;
  end;
  if not CloseTransferPanel() then begin
    if not UninstallSilent then
      MsgBox('请完成或取消局域网传输，关闭互传面板后再卸载。', mbError, MB_OK);
    Abort;
  end;
  if OwnsClass then begin
    ExitCode := 0;
    if not Exec(ExpandConstant('{#PayloadPath}\{#LauncherName}'), '--detach', '', SW_HIDE,
      ewWaitUntilTerminated, ExitCode) or (ExitCode <> 0) then begin
      Log('Unable to detach LYwintabs or restore Windows colors. Code: ' + IntToStr(ExitCode));
      if not UninstallSilent then
        MsgBox('无法撤下 LYwintabs 或恢复 Windows 配色。若有资源管理器窗口正在操作文件，请等操作完成后正常关闭窗口，再重试卸载。错误代码 ' + IntToStr(ExitCode) + '。', mbError, MB_OK);
      Abort;
    end;
    Log('LYwintabs detached from open Explorer windows and released its Windows color changes.');
  end;
  if RegQueryStringValue(HKCU, '{#StartupKey}', '{#StartupName}', Command)
    and (CompareText(Command, StartupCommand(ExpandConstant('{#PayloadPath}'))) = 0) then begin
    if not RegDeleteValue(HKCU, '{#StartupKey}', '{#StartupName}') then
      Log('Unable to remove this installation''s launch-at-sign-in command.');
  end;
  if OwnsMenu then begin
    RegDeleteKeyIncludingSubkeys(HKCU, '{#CommandKey}');
    DeleteFile(ExpandConstant('{userprograms}\{#ShortcutName}.lnk'));
  end;
  if OwnsClass then
    RegDeleteKeyIncludingSubkeys(HKCU, '{#ClassKey}');
end;
