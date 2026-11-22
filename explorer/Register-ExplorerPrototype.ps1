param(
    [switch] $Unregister,
    [ValidateSet('CurrentUser', 'Machine')]
    [string] $Scope = 'CurrentUser',
    [string] $DllPath = (Join-Path $PSScriptRoot '..\out\explorer\x64\Release\LYwintabs.Explorer.dll')
)

$ErrorActionPreference = 'Stop'
$classId = '{B5ABC93E-9B6E-4A71-8AAA-2DE01C5A918B}'
$previousPluginThemeClassId = '{E8A264B0-F03D-4E6A-80AB-065109BD6A00}'
$previousUnifiedCaptionClassId = '{7FCCD37B-856B-4F7E-BFC2-4B576D7B5845}'
$previousLucideIconsClassId = '{E3EE511E-6945-4FEE-B8C5-59CCF27BBAF5}'
$previousBrowserUiClassId = '{D1FBC879-9A83-4D9F-957F-30E19FB22C4F}'
$previousAdaptiveTabsClassId = '{39C4ABA3-848E-4C04-B820-B823C7FA3965}'
$previousBookmarkStyleClassId = '{A8E78474-6548-4A5E-90B9-88F1B3EE79AE}'
$previousBookmarkEditClassId = '{43F93CCE-6AEE-440B-BBA0-2EBAF6D6A483}'
$previousBookmarkDragClassId = '{AD1DB7CE-F07D-4454-9BEE-942F1F4B9721}'
$previousBookmarkClassId = '{05748C5D-BD1D-48E9-BD02-E0E813DCBBBD}'
$previousToolbarClassId = '{D22BB869-3DF2-44DC-A611-CE723699C654}'
$previousAutoAttachClassId = '{8F6DA47F-2391-4BBA-BBA1-746EACC4490D}'
$previousSettingsClassId = '{EC31D207-DA99-4916-BED9-BEC162D4B56F}'
$previousLayoutClassId = '{FB715715-57EA-4CE3-AC04-3E2C14B099BF}'
$legacyClassId = '{4A917ED1-591A-4EB4-AFF4-262CDCAB2152}'
$ownedClassIds = @($classId, $previousPluginThemeClassId, $previousUnifiedCaptionClassId, $previousLucideIconsClassId, $previousBrowserUiClassId, $previousAdaptiveTabsClassId, $previousBookmarkStyleClassId, $previousBookmarkEditClassId, $previousBookmarkDragClassId, $previousBookmarkClassId, $previousToolbarClassId, $previousAutoAttachClassId, $previousSettingsClassId, $previousLayoutClassId, $legacyClassId)
$classes = if ($Scope -eq 'Machine') { 'Registry::HKEY_LOCAL_MACHINE\Software\Classes' } `
    else { 'Registry::HKEY_CURRENT_USER\Software\Classes' }
$classKey = "$classes\CLSID\$classId"
$serverKey = "$classKey\InprocServer32"
$verbKey = "$classes\Directory\Background\shell\LYwintabs.FavoritesBar"

if ($Unregister) {
    if (Test-Path -LiteralPath $verbKey) {
        $owner = (Get-Item -LiteralPath $verbKey).GetValue('ExplorerCommandHandler')
        if ($owner -notin $ownedClassIds) {
            throw 'The context-menu registration belongs to a different class.'
        }
        Remove-Item -LiteralPath $verbKey
    }
    foreach ($ownedClassId in $ownedClassIds) {
        $ownedClassKey = "$classes\CLSID\$ownedClassId"
        $ownedServerKey = "$ownedClassKey\InprocServer32"
        if (Test-Path -LiteralPath $ownedServerKey) { Remove-Item -LiteralPath $ownedServerKey }
        if ((Test-Path -LiteralPath $ownedClassKey) -and !(Get-ChildItem -LiteralPath $ownedClassKey)) {
            Remove-Item -LiteralPath $ownedClassKey
        }
    }
} else {
    $dll = (Resolve-Path -LiteralPath $DllPath).Path
    if (Test-Path -LiteralPath $verbKey) {
        if ((Get-Item -LiteralPath $verbKey).GetValue('ExplorerCommandHandler') -notin $ownedClassIds) {
            throw 'The context-menu registration belongs to a different class.'
        }
    }
    foreach ($key in @($classKey, $serverKey, $verbKey)) {
        if (!(Test-Path -LiteralPath $key)) { New-Item -Path $key -Force | Out-Null }
    }
    Set-Item -LiteralPath $classKey -Value 'LYwintabs Explorer Favorites Bar'
    Set-Item -LiteralPath $serverKey -Value $dll
    Set-ItemProperty -LiteralPath $serverKey -Name ThreadingModel -Value Apartment
    Set-Item -LiteralPath $verbKey -Value 'LYwintabs 收藏栏'
    Set-ItemProperty -LiteralPath $verbKey -Name ExplorerCommandHandler -Value $classId
}

if (-not ('LYwintabs.Associations' -as [type])) {
    Add-Type -Namespace LYwintabs -Name Associations -MemberDefinition @'
[System.Runtime.InteropServices.DllImport("shell32.dll")]
public static extern void SHChangeNotify(int eventId, uint flags, System.IntPtr item1, System.IntPtr item2);
'@
}
[LYwintabs.Associations]::SHChangeNotify(0x08000000, 0, [IntPtr]::Zero, [IntPtr]::Zero)
