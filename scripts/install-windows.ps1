param(
  [string]$SourceDir = '',
  [string]$InstallRoot = '',
  [string]$VersionOverride = '',
  [string]$LogoPath = '',
  [string]$CompanionRoot = '',
  [switch]$NoPublicIntegration,
  [ValidateSet('None','AfterBaselineMove','AfterCandidateMove')]
  [string]$TestFailurePoint = 'None'
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest

$RepoRoot=$null
try{
  $candidate=(Resolve-Path (Join-Path $PSScriptRoot '..') -ErrorAction Stop).Path
  if(Test-Path -LiteralPath (Join-Path $candidate 'VERSION') -PathType Leaf){$RepoRoot=$candidate}
}catch{}

$Version=if($VersionOverride){$VersionOverride}elseif($RepoRoot){(Get-Content -LiteralPath (Join-Path $RepoRoot 'VERSION') -Raw).Trim()}else{throw 'Browser install requires -VersionOverride when repository VERSION is unavailable.'}
if($Version -notmatch '^\d+\.\d+\.\d+(?:-[A-Za-z0-9._-]+)?$'){throw "Invalid Browser version: $Version"}

if(-not $SourceDir){
  if(-not $RepoRoot){throw 'Browser install requires -SourceDir when repository payload is unavailable.'}
  $SourceDir=Join-Path $RepoRoot 'build-windows\bin'
}
$SourceDir=[IO.Path]::GetFullPath($SourceDir)
$SourceExe=Join-Path $SourceDir 'chatgpt-cef-v2.exe'
if(-not (Test-Path -LiteralPath $SourceExe -PathType Leaf)){throw "Browser payload is missing: $SourceExe"}

if(-not $InstallRoot){$InstallRoot=Join-Path $env:LOCALAPPDATA 'Programs\Remote Commander Browser'}
$InstallRoot=[IO.Path]::GetFullPath($InstallRoot)
if(-not $CompanionRoot){$CompanionRoot=Join-Path $env:LOCALAPPDATA 'ChatGPTRemoteCommander\browser-companion'}
$CompanionRoot=[IO.Path]::GetFullPath($CompanionRoot)

if(-not $LogoPath -and $RepoRoot){
  $candidate=Join-Path $RepoRoot 'assets\remote-commander-browser-logo.png'
  if(Test-Path -LiteralPath $candidate -PathType Leaf){$LogoPath=$candidate}
}
if($LogoPath){$LogoPath=[IO.Path]::GetFullPath($LogoPath)}

$Entries = Get-ChildItem -LiteralPath $SourceDir -File -Recurse | ForEach-Object {
  $rel=[IO.Path]::GetRelativePath($SourceDir,$_.FullName).Replace('\','/')
  $h=(Get-FileHash -Algorithm SHA256 -LiteralPath $_.FullName).Hash.ToLowerInvariant()
  "$rel|$h"
} | Sort-Object
$Manifest=$Entries -join "`n"
$PackageSha=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes($Manifest))).ToLowerInvariant()
$VersionDir=Join-Path $InstallRoot (("v"+$Version+"-")+$PackageSha.Substring(0,12))
$Current=Join-Path $InstallRoot 'current'
$StartMenu=Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\Remote Commander'
$Reg='HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\RemoteCommanderBrowser'
# Fault injection is only permitted in this isolated source repository's
# NoPublicIntegration test fixtures. It cannot affect a live public install.
if($TestFailurePoint -ne 'None'){
  $fixtureRoot=[IO.Path]::GetFullPath((Join-Path $RepoRoot 'test\swap-r57'))
  $fixturePrefix=$fixtureRoot.TrimEnd([char]92)+[IO.Path]::DirectorySeparatorChar
  if((-not $NoPublicIntegration) -or
     (-not $InstallRoot.StartsWith($fixturePrefix,[StringComparison]::OrdinalIgnoreCase))){
    throw 'R57_FAULT_INJECTION_RESTRICTED_TO_PRIVATE_TEST_ROOT'
  }
}

function Test-PrivateDirectoryAcl([string]$Path) {
  if(-not(Test-Path -LiteralPath $Path -PathType Container)){return $false}
  $user=[Security.Principal.WindowsIdentity]::GetCurrent().User
  $acl=Get-Acl -LiteralPath $Path
  try{$owner=(New-Object Security.Principal.NTAccount($acl.Owner)).Translate([Security.Principal.SecurityIdentifier])}
  catch{return $false}
  if($owner.Value-ne$user.Value -or -not$acl.AreAccessRulesProtected){return $false}

  $expected=@($user.Value,'S-1-5-18','S-1-5-32-544')
  $rules=@($acl.GetAccessRules($true,$false,[Security.Principal.SecurityIdentifier]))
  if($rules.Count-ne$expected.Count){return $false}
  foreach($rule in $rules){
    if([string]$rule.AccessControlType-ne'Allow'){return $false}
    if($expected -notcontains $rule.IdentityReference.Value){return $false}
    if(($rule.FileSystemRights-band[Security.AccessControl.FileSystemRights]::FullControl)-ne[Security.AccessControl.FileSystemRights]::FullControl){return $false}
    $required=[Security.AccessControl.InheritanceFlags]'ContainerInherit,ObjectInherit'
    if(($rule.InheritanceFlags-band$required)-ne$required){return $false}
  }
  return $true
}

function Ensure-PrivateDirectory([string]$Path) {
  New-Item -ItemType Directory -Force -Path $Path | Out-Null
  if(Test-PrivateDirectoryAcl $Path){return}

  $identity=[Security.Principal.WindowsIdentity]::GetCurrent()
  $user=$identity.User
  $acl=Get-Acl -LiteralPath $Path
  try{$owner=(New-Object Security.Principal.NTAccount($acl.Owner)).Translate([Security.Principal.SecurityIdentifier])}
  catch{throw 'BROWSER_COMPANION_ACL_OWNER_UNRESOLVED'}

  $icacls=Join-Path $env:SystemRoot 'System32\icacls.exe'
  if(-not(Test-Path -LiteralPath $icacls -PathType Leaf)){throw 'BROWSER_COMPANION_ICACLS_MISSING'}
  if($owner.Value-ne$user.Value){
    & $icacls $Path '/setowner' $identity.Name '/Q' | Out-Null
    if($LASTEXITCODE-ne0){throw "BROWSER_COMPANION_OWNER_REPAIR_FAILED exit=$LASTEXITCODE"}
    $acl=Get-Acl -LiteralPath $Path
    try{$owner=(New-Object Security.Principal.NTAccount($acl.Owner)).Translate([Security.Principal.SecurityIdentifier])}
    catch{throw 'BROWSER_COMPANION_ACL_OWNER_UNRESOLVED_AFTER_REPAIR'}
    if($owner.Value-ne$user.Value){throw 'BROWSER_COMPANION_ACL_OWNER_MISMATCH_AFTER_REPAIR'}
  }

  $privateAcl=[Security.AccessControl.DirectorySecurity]::new()
  $privateAcl.SetOwner($user)
  $privateAcl.SetAccessRuleProtection($true,$false)
  $inherit=[Security.AccessControl.InheritanceFlags]'ContainerInherit,ObjectInherit'
  $propagate=[Security.AccessControl.PropagationFlags]::None
  foreach($sid in @(
    $user,
    [Security.Principal.SecurityIdentifier]::new('S-1-5-18'),
    [Security.Principal.SecurityIdentifier]::new('S-1-5-32-544')
  )){
    $rule=[Security.AccessControl.FileSystemAccessRule]::new(
      $sid,
      [Security.AccessControl.FileSystemRights]::FullControl,
      $inherit,
      $propagate,
      [Security.AccessControl.AccessControlType]::Allow
    )
    [void]$privateAcl.AddAccessRule($rule)
  }
  Set-Acl -LiteralPath $Path -AclObject $privateAcl
  if(-not(Test-PrivateDirectoryAcl $Path)){
    $check=Get-Acl -LiteralPath $Path
    $checkRules=@($check.GetAccessRules($true,$false,[Security.Principal.SecurityIdentifier]) | ForEach-Object {
      [ordered]@{
        sid=$_.IdentityReference.Value
        type=[string]$_.AccessControlType
        rights=[string]$_.FileSystemRights
        inheritance=[string]$_.InheritanceFlags
        propagation=[string]$_.PropagationFlags
        inherited=[bool]$_.IsInherited
      }
    })
    Write-Output ('BROWSER_COMPANION_ACL_DIAGNOSTIC '+([ordered]@{
      owner=$check.Owner
      protected=[bool]$check.AreAccessRulesProtected
      rules=$checkRules
    }|ConvertTo-Json -Depth 6 -Compress))
    throw 'BROWSER_COMPANION_ACL_VERIFY_FAILED'
  }
}
# Fail before touching existing executable/junction on any unrecognized Current.
$priorCurrent=Get-Item -LiteralPath $Current -Force -ErrorAction SilentlyContinue
$priorTarget=''
if($null -ne $priorCurrent){
  if($priorCurrent.LinkType -cne 'Junction'){throw 'BROWSER_CURRENT_NOT_A_JUNCTION'}
  $priorTarget=[IO.Path]::GetFullPath([string](@($priorCurrent.Target)[0]))
  $childPrefix=$InstallRoot.TrimEnd([char]92)+[IO.Path]::DirectorySeparatorChar
  if((-not $priorTarget.StartsWith($childPrefix,[StringComparison]::OrdinalIgnoreCase)) -or
     (-not(Test-Path -LiteralPath $priorTarget -PathType Container))){
    throw 'BROWSER_CURRENT_TARGET_OUTSIDE_VERSION_ROOT'
  }
}
$stage=Join-Path $InstallRoot ('.stage-v'+$Version+'-'+$PackageSha.Substring(0,12))
$pending=Join-Path $InstallRoot ('current.pending-'+$PackageSha.Substring(0,12))
$rollback=Join-Path $InstallRoot ('current.rollback-'+$PackageSha.Substring(0,12))
$failed=Join-Path $InstallRoot ('current.failed-'+$PackageSha.Substring(0,12))
foreach($p in @($VersionDir,$stage,$pending,$rollback,$failed)){
  if($null -ne (Get-Item -LiteralPath $p -Force -ErrorAction SilentlyContinue)){
    throw ('BROWSER_EXACT_VERSION_OR_SWAP_PATH_ALREADY_EXISTS_'+$p)
  }
}
# The candidate is a new, immutable version directory; never copy onto
# an installed baseline. Its complete file list and SHA must match SourceDir.
New-Item -ItemType Directory -Path $InstallRoot -Force | Out-Null
New-Item -ItemType Directory -Path $stage -ErrorAction Stop | Out-Null
Copy-Item -Path (Join-Path $SourceDir '*') -Destination $stage -Recurse -ErrorAction Stop
$copyEntries=Get-ChildItem -LiteralPath $stage -File -Recurse | ForEach-Object {
  $rel=[IO.Path]::GetRelativePath($stage,$_.FullName).Replace('\','/')
  $hash=(Get-FileHash -Algorithm SHA256 -LiteralPath $_.FullName).Hash.ToLowerInvariant()
  "$rel|$hash"
} | Sort-Object
if(($copyEntries -join [char]10) -cne $Manifest){throw 'BROWSER_STAGED_PAYLOAD_SHA_OR_MEMBER_MISMATCH'}
if($LogoPath -and (Test-Path -LiteralPath $LogoPath -PathType Leaf)){
  $targetLogo=Join-Path $stage 'remote-commander-browser-logo.png'
  if(Test-Path -LiteralPath $targetLogo){
    if((Get-FileHash $targetLogo -Algorithm SHA256).Hash -cne
       (Get-FileHash $LogoPath -Algorithm SHA256).Hash){
      throw 'BROWSER_STAGE_LOGO_COLLISION'
    }
  }else{
    Copy-Item -LiteralPath $LogoPath -Destination $targetLogo -ErrorAction Stop
  }
}
$installRecord=[ordered]@{
  schema=1
  product='Remote Commander Browser'
  version=$Version
  packageSha256=$PackageSha
  installedAt=(Get-Date).ToUniversalTime().ToString('o')
  companionRoot=$CompanionRoot
  publicIntegration=(-not $NoPublicIntegration)
}
[IO.File]::WriteAllText((Join-Path $stage 'product-install.json'),
  (($installRecord|ConvertTo-Json -Depth 5)+[Environment]::NewLine),
  [Text.UTF8Encoding]::new($false))
Move-Item -LiteralPath $stage -Destination $VersionDir -ErrorAction Stop
$expectedExe=(Get-FileHash -LiteralPath $SourceExe -Algorithm SHA256).Hash.ToLowerInvariant()
if((Get-FileHash -LiteralPath (Join-Path $VersionDir 'chatgpt-cef-v2.exe') -Algorithm SHA256).Hash.ToLowerInvariant() -cne $expectedExe){
  throw 'BROWSER_COMMITTED_VERSION_EXE_SHA_DRIFT'
}
Ensure-PrivateDirectory $CompanionRoot

# Stage the replacement link first. The old Current is moved to a
# preserved rollback alias, never deleted or recursively traversed.
New-Item -ItemType Junction -Path $pending -Target $VersionDir -ErrorAction Stop | Out-Null
$baselineMoved=$false
try{
  if($null -ne $priorCurrent){
    Move-Item -LiteralPath $Current -Destination $rollback -ErrorAction Stop
    $baselineMoved=$true
  }
  if($TestFailurePoint -eq 'AfterBaselineMove'){
    throw 'R57_SYNTHETIC_FAILURE_AFTER_BASELINE_MOVE'
  }
  Move-Item -LiteralPath $pending -Destination $Current -ErrorAction Stop
  if($TestFailurePoint -eq 'AfterCandidateMove'){
    throw 'R57_SYNTHETIC_FAILURE_AFTER_CANDIDATE_MOVE'
  }
  $newCurrent=Get-Item -LiteralPath $Current -Force -ErrorAction Stop
  if($newCurrent.LinkType -cne 'Junction' -or
     [IO.Path]::GetFullPath([string](@($newCurrent.Target)[0])) -cne $VersionDir){
    throw 'BROWSER_NEW_CURRENT_POINTER_VERIFY_FAILED'
  }
}catch{
  $originalError=$_.Exception.Message
  try{
    $candidateCurrent=Get-Item -LiteralPath $Current -Force -ErrorAction SilentlyContinue
    if($null -ne $candidateCurrent){
      if($candidateCurrent.LinkType -cne 'Junction' -or
         [IO.Path]::GetFullPath([string](@($candidateCurrent.Target)[0])) -cne $VersionDir){
        throw 'UNEXPECTED_CURRENT_LINK_DURING_ROLLBACK'
      }
      Move-Item -LiteralPath $Current -Destination $failed -ErrorAction Stop
    }
    if($baselineMoved){
      Move-Item -LiteralPath $rollback -Destination $Current -ErrorAction Stop
      $restored=Get-Item -LiteralPath $Current -Force -ErrorAction Stop
      if($restored.LinkType -cne 'Junction' -or
         [IO.Path]::GetFullPath([string](@($restored.Target)[0])) -cne $priorTarget){
        throw 'RESTORED_BASELINE_POINTER_MISMATCH'
      }
    }
  }catch{
    throw ('BROWSER_ROLLBACK_UNRESOLVED_OR_DEGRADED: '+$_.Exception.Message+
      '; original='+$originalError)
  }
  throw ('BROWSER_SWAP_ABORTED_AND_BASELINE_PRESERVED: '+$originalError)
}

if(-not $NoPublicIntegration){
  New-Item -ItemType Directory -Force -Path $StartMenu | Out-Null
  $Wsh=New-Object -ComObject WScript.Shell
  $Shortcut=$Wsh.CreateShortcut((Join-Path $StartMenu 'Remote Commander Browser.lnk'))
  $Shortcut.TargetPath=Join-Path $Current 'chatgpt-cef-v2.exe'
  $Shortcut.WorkingDirectory=$Current
  $Shortcut.IconLocation=(Join-Path $Current 'chatgpt-cef-v2.exe')+',0'
  $Shortcut.Description='Remote Commander Browser'
  $Shortcut.Save()

  $Uninstall=Join-Path $InstallRoot 'uninstall.ps1'
  $UninstallText=@"
`$ErrorActionPreference='Stop'
`$Shortcut=Join-Path `$env:APPDATA 'Microsoft\Windows\Start Menu\Programs\Remote Commander\Remote Commander Browser.lnk'
Remove-Item -LiteralPath `$Shortcut -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\RemoteCommanderBrowser' -Recurse -Force -ErrorAction SilentlyContinue
`$selfRoot='$($InstallRoot.Replace("'","''"))'
Start-Process -FilePath 'cmd.exe' -ArgumentList '/d','/c',"timeout /t 1 /nobreak >nul & rmdir /s /q `"`$selfRoot`"" -WindowStyle Hidden
"@
  [IO.File]::WriteAllText($Uninstall,$UninstallText,(New-Object Text.UTF8Encoding($false)))

  New-Item -Path $Reg -Force | Out-Null
  New-ItemProperty -Path $Reg -Name DisplayName -Value 'Remote Commander Browser' -PropertyType String -Force | Out-Null
  New-ItemProperty -Path $Reg -Name DisplayVersion -Value $Version -PropertyType String -Force | Out-Null
  New-ItemProperty -Path $Reg -Name Publisher -Value 'Remote Commander' -PropertyType String -Force | Out-Null
  New-ItemProperty -Path $Reg -Name InstallLocation -Value $InstallRoot -PropertyType String -Force | Out-Null
  New-ItemProperty -Path $Reg -Name DisplayIcon -Value ((Join-Path $Current 'chatgpt-cef-v2.exe')+',0') -PropertyType String -Force | Out-Null
  New-ItemProperty -Path $Reg -Name UninstallString -Value ('"'+(Get-Command pwsh.exe).Source+'" -NoProfile -File "'+$Uninstall+'"') -PropertyType String -Force | Out-Null
  New-ItemProperty -Path $Reg -Name NoModify -Value 1 -PropertyType DWord -Force | Out-Null
  New-ItemProperty -Path $Reg -Name NoRepair -Value 1 -PropertyType DWord -Force | Out-Null

  $Check=Join-Path $StartMenu 'Remote Commander Browser.lnk'
  if(-not (Test-Path -LiteralPath $Check -PathType Leaf)){throw 'Browser Start Menu shortcut was not created'}
}

Write-Output "REMOTE_COMMANDER_BROWSER_INSTALL_PASS version=$Version package=$($PackageSha.Substring(0,12)) install=$InstallRoot companion=$CompanionRoot public=$(-not $NoPublicIntegration)"
