# R57 Windows Browser installer offline transaction tests. Isolated fixture only.
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
$installer=Join-Path $repo 'scripts\install-windows.ps1'
$fixture=Join-Path $PSScriptRoot 'swap-r57'
$report=Join-Path $PSScriptRoot 'SWAP_RESULT_CI_R57.json'
if(-not $IsWindows -or -not(Test-Path -LiteralPath (Join-Path $repo 'VERSION')) -or
   -not(Test-Path -LiteralPath $installer)){
 throw 'R57_WINDOWS_CLEAN_CHECKOUT_REQUIRED'
}
if((Test-Path -LiteralPath $fixture) -or (Test-Path -LiteralPath $report)){throw 'R57_TEST_UNSAFE_RERUN'}
$router=Join-Path $env:LOCALAPPDATA 'ChatGPTRemoteCommander\routing\default.json'
$productionCurrent=Join-Path $env:LOCALAPPDATA 'Programs\Remote Commander Browser\current'
$routerPresent=Test-Path -LiteralPath $router
$prodPresent=$null -ne (Get-Item -LiteralPath $productionCurrent -Force -ErrorAction SilentlyContinue)
$routerPre=if($routerPresent){(Get-FileHash -LiteralPath $router -Algorithm SHA256).Hash.ToLowerInvariant()}else{$null}
$prodPre=if($prodPresent){(Get-Item -LiteralPath $productionCurrent -Force).Target}else{$null}
function Assert([bool]$ok,[string]$msg){if(-not $ok){throw ('R57_TEST_ASSERT_'+$msg)}}
function LinkTarget([string]$p) {
 $entry=Get-Item -LiteralPath $p -Force -ErrorAction Stop
 Assert ($entry.LinkType -cne '' -and $entry.LinkType -eq 'Junction') ('NOT_JUNCTION_'+$p)
 return [IO.Path]::GetFullPath([string](@($entry.Target)[0]))
}
New-Item -ItemType Directory -Path $fixture -ErrorAction Stop|Out-Null
$contents=@{
 'baseline'='0.8.1 old immutable binary fixture (no executable payload)'
 'fault-baseline'='0.8.2 injected after old junction moved'
 'fault-candidate'='0.8.3 injected after new current created'
 'success'='0.8.4 new immutable candidate binary fixture'
}
foreach($name in $contents.Keys){
 $dir=Join-Path $fixture ('payload-'+$name)
 New-Item -ItemType Directory -Path $dir -ErrorAction Stop|Out-Null
 [IO.File]::WriteAllText((Join-Path $dir 'chatgpt-cef-v2.exe'),$contents[$name]+[char]10,[Text.UTF8Encoding]::new($false))
}
$app=Join-Path $fixture 'case-main\app'
$companion=Join-Path $fixture 'case-main\private-companion'
$old=Join-Path $fixture 'payload-baseline'
$one=Join-Path $fixture 'payload-fault-baseline'
$two=Join-Path $fixture 'payload-fault-candidate'
$three=Join-Path $fixture 'payload-success'
$stop=New-Object 'System.Collections.Generic.List[string]'
& $installer -SourceDir $old -InstallRoot $app -CompanionRoot $companion -VersionOverride '0.8.1' -NoPublicIntegration | Out-Null
$oldTarget=LinkTarget (Join-Path $app 'current')
Assert ($oldTarget.StartsWith([IO.Path]::GetFullPath($app)+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) 'BASELINE_OUTSIDE_TEST'
Assert ((Get-Content (Join-Path $oldTarget 'chatgpt-cef-v2.exe') -Raw).Contains('old immutable')) 'BASELINE_PAYLOAD'
$stop.Add('FIRST_INSTALL_PASS')
try{
 & $installer -SourceDir $one -InstallRoot $app -CompanionRoot $companion -VersionOverride '0.8.2' -NoPublicIntegration -TestFailurePoint 'AfterBaselineMove'|Out-Null
 throw 'R57_FAULT_INJECTION_UNEXPECTED_SUCCESS'
}catch{
 Assert ($_.Exception.Message.Contains('BROWSER_SWAP_ABORTED_AND_BASELINE_PRESERVED')) ('FAILURE_PATH_NOT_ROLLBACK_'+$_.Exception.Message)
}
Assert ((LinkTarget (Join-Path $app 'current')) -ceq $oldTarget) 'BASELINE_RESTORED_AFTER_OLD_MOVE'
$stop.Add('AFTER_OLD_LINK_MOVE_ROLLBACK_PASS')
try{
 & $installer -SourceDir $two -InstallRoot $app -CompanionRoot $companion -VersionOverride '0.8.3' -NoPublicIntegration -TestFailurePoint 'AfterCandidateMove'|Out-Null
 throw 'R57_FAULT_NEW_POINTER_UNEXPECTED_SUCCESS'
}catch{
 Assert ($_.Exception.Message.Contains('BROWSER_SWAP_ABORTED_AND_BASELINE_PRESERVED')) ('NEW_POINTER_ROLLBACK_FAILED_'+$_.Exception.Message)
}
Assert ((LinkTarget (Join-Path $app 'current')) -ceq $oldTarget) 'BASELINE_RESTORED_AFTER_CANDIDATE_MOVE'
$stop.Add('AFTER_NEW_LINK_MOVE_ROLLBACK_PASS')
& $installer -SourceDir $three -InstallRoot $app -CompanionRoot $companion -VersionOverride '0.8.4' -NoPublicIntegration | Out-Null
$newTarget=LinkTarget (Join-Path $app 'current')
Assert ($newTarget -cne $oldTarget) 'CANDIDATE_NOT_ACTIVATED'
Assert ((Get-Content (Join-Path $newTarget 'chatgpt-cef-v2.exe') -Raw).Contains('new immutable candidate')) 'CANDIDATE_SHA_CONTENT'
$rollbackAliases=@(Get-ChildItem -LiteralPath $app -Force|Where-Object {$_.Name -like 'current.rollback-*' -and $_.LinkType -eq 'Junction'})
Assert ($rollbackAliases.Count -eq 1) 'EXACTLY_ONE_PRESERVED_ROLLBACK_LINK'
Assert ([IO.Path]::GetFullPath([string](@($rollbackAliases[0].Target)[0])) -ceq $oldTarget) 'ROLLBACK_ALIAS_TARGET'
$stop.Add('SIDE_BY_SIDE_NEW_VERSION_AND_OLD_ROLLBACK_ALIAS_PASS')
try{
 & $installer -SourceDir $three -InstallRoot $app -CompanionRoot $companion -VersionOverride '0.8.4' -NoPublicIntegration|Out-Null
 throw 'R57_SAME_VERSION_OVERWRITE_UNEXPECTED_SUCCESS'
}catch{
 Assert ($_.Exception.Message.Contains('BROWSER_EXACT_VERSION_OR_SWAP_PATH_ALREADY_EXISTS')) 'IMMUTABLE_VERSION_REFUSAL'
}
Assert ((LinkTarget (Join-Path $app 'current')) -ceq $newTarget) 'CURRENT_CHANGED_DURING_RERUN'
$stop.Add('IMMUTABLE_VERSION_NO_OVERWRITE_PASS')
$unknown=Join-Path $fixture 'case-unknown-current\app'
New-Item -ItemType Directory -Path (Join-Path $unknown 'current') -Force|Out-Null
try{
 & $installer -SourceDir $old -InstallRoot $unknown -CompanionRoot (Join-Path $fixture 'case-unknown-current\companion') -VersionOverride '0.8.1' -NoPublicIntegration|Out-Null
 throw 'R57_UNSAFE_REAL_DIRECTORY_ACCEPTED'
}catch{
 Assert ($_.Exception.Message.Contains('BROWSER_CURRENT_NOT_A_JUNCTION')) 'NON_JUNCTION_NOT_REJECTED'
}
$stop.Add('UNKNOWN_CURRENT_NON_JUNCTION_REJECTED')
try{
 & $installer -SourceDir $old -InstallRoot $app -CompanionRoot $companion -VersionOverride '0.9.1' -TestFailurePoint 'AfterBaselineMove'|Out-Null
 throw 'R57_PUBLIC_INJECTION_UNEXPECTED_SUCCESS'
}catch{
 Assert ($_.Exception.Message.Contains('R57_FAULT_INJECTION_RESTRICTED_TO_PRIVATE_TEST_ROOT')) 'PUBLIC_INJECTION_NOT_BLOCKED'
}
$stop.Add('PUBLIC_FAULT_INJECTION_FORBIDDEN')
Assert ((Test-Path -LiteralPath $router) -eq $routerPresent) 'ROUTER_PRESENCE_CHANGED'
if($routerPresent){Assert ((Get-FileHash $router -Algorithm SHA256).Hash.ToLowerInvariant() -ceq $routerPre) 'ROUTER_CHANGED'}
Assert (($null -ne (Get-Item -LiteralPath $productionCurrent -Force -ErrorAction SilentlyContinue)) -eq $prodPresent) 'PRODUCTION_BROWSER_POINTER_PRESENCE_CHANGED'
if($prodPresent){Assert ((Get-Item $productionCurrent -Force).Target -ceq $prodPre) 'PRODUCTION_BROWSER_POINTER_CHANGED'}
$record=[ordered]@{
 schema=1;revision='R57';status='PASS_ISOLATED_WINDOWS_BROWSER_VERSION_SWAP_AND_ROLLBACK'
 date=(Get-Date).ToUniversalTime().ToString('o')
 testRoot=$fixture;cases=@($stop)
 oldVersion=$oldTarget;newVersion=$newTarget
 newRuntimeSha256=(Get-FileHash (Join-Path $newTarget 'chatgpt-cef-v2.exe') -Algorithm SHA256).Hash.ToLowerInvariant()
 preservedRollbackAlias=$rollbackAliases[0].FullName
 originalBrowserTarget=$prodPre;productionBrowserPointerUntouched=$true;productionRouterSha256=$routerPre;productionRouterOriginallyPresent=$routerPresent
 credentialsAccessed=$false;ownerDesktopTouched=$false;installedProductionChanged=$false
 publicReleaseSigned=$false;nativeOwnerAcceptance='OPEN'
}
[IO.File]::WriteAllText($report,($record|ConvertTo-Json -Depth 10)+[char]10,[Text.UTF8Encoding]::new($false))
'R57_ISOLATED_POINTER_SWAP_FAULT_INJECTION_PASS'
'FILE TO SEND TO CHATGPT:'
$report
'SHA256='+((Get-FileHash $report -Algorithm SHA256).Hash.ToLowerInvariant())
