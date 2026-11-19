param(
    [Parameter(Mandatory)][string]$Runtime,
    [Parameter(Mandatory)][string]$Data,
    [Parameter(Mandatory)][string]$Levels,
    [Parameter(Mandatory)][string]$Probe,
    [Parameter(Mandatory)][string]$FixtureCard,
    [Parameter(Mandatory)][string]$Output,
    [string]$Candidate
)
$ErrorActionPreference='Stop'
$runtimePath=(Resolve-Path -LiteralPath $Runtime).Path
$dataPath=(Resolve-Path -LiteralPath $Data).Path
$levelsPath=(Resolve-Path -LiteralPath $Levels).Path
$probePath=(Resolve-Path -LiteralPath $Probe).Path
$fixtureCardPath=(Resolve-Path -LiteralPath $FixtureCard).Path
$cardDirectory='BESCES-50916RATCHET'
$root=$ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Output)
if(Test-Path -LiteralPath $root){throw "The isolated output directory must be new: $root"}
if(!(Test-Path -LiteralPath (Join-Path $fixtureCardPath "$cardDirectory/save0.bin.arsenal"))){
    throw 'Expected the generated Golden QA PAL card with an arsenal sidecar.'
}
New-Item -ItemType Directory -Path $root | Out-Null
$plugins=Join-Path $root 'plugins'
$dataRoot=Join-Path $root 'data'
$cards=Join-Path $root 'cards'
$local=Join-Path $root 'local'
New-Item -ItemType Directory -Path $plugins,$dataRoot,$cards,$local | Out-Null
New-Item -ItemType Junction -Path (Join-Path $dataRoot 'iso_data') -Target $dataPath | Out-Null
Copy-Item -LiteralPath (Join-Path $fixtureCardPath $cardDirectory) -Destination $cards -Recurse

$mods=@()
$candidateSha=$null
if($Candidate){
    $destination=Join-Path $plugins 'revolverator'
    $source=Get-Item -LiteralPath $Candidate
    if($source.PSIsContainer){Copy-Item -LiteralPath $source.FullName -Destination $destination -Recurse}
    else{Expand-Archive -LiteralPath $source.FullName -DestinationPath $destination}
    $candidateSha=(Get-FileHash -LiteralPath (Join-Path $destination 'lib/windows-x86_64/revolverator.dll')).Hash
    $mods+=@{id='revolverator';enabled=$true;settings=@{start_unlocked=$false;trace=$true}}
}
$probeFolder=Join-Path $plugins 'challenge-mode-probe'
Copy-Item -LiteralPath $probePath -Destination $probeFolder -Recurse
$probeSha=(Get-FileHash -LiteralPath (Join-Path $probeFolder 'lib/windows-x86_64/challenge_mode_probe.dll')).Hash
$catalog=Join-Path $plugins 'plugins.json'
$settings=Join-Path $root 'settings.ini'
@{format=1;mods=@($mods+@{id='challenge-mode-probe';enabled=$true;settings=@{candidate=[bool]$Candidate}})} |
    ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $catalog -Encoding utf8
@'
display_mode = windowed
window_size = 640x480
enhanced_rendering = 0
enhanced_graphics = 0
temporal_quality = off
vsync = off
frame_rate = original
skip_cutscenes = 1
volume = 0
'@ | Set-Content -LiteralPath $settings -Encoding utf8

$cardBefore=@(Get-ChildItem -LiteralPath $cards -Recurse -File | Sort-Object FullName | Get-FileHash)
$info=[Diagnostics.ProcessStartInfo]::new()
$info.FileName=$runtimePath
$info.WorkingDirectory=$root
$info.UseShellExecute=$false
$info.CreateNoWindow=$true
$info.RedirectStandardError=$true
$info.RedirectStandardOutput=$true
foreach($key in @($info.Environment.Keys)){
    if($key.StartsWith('OPENRAC_',[StringComparison]::OrdinalIgnoreCase)){
        $info.Environment.Remove($key) | Out-Null
    }
}
foreach($arg in @('--data',$dataRoot,'--levels',$levelsPath,'--cards',$cards,'--plugins',$catalog,
    '--settings',$settings,'--skip-movies','--window','--frames','2100','--level','1')){
    $info.ArgumentList.Add($arg)
}
$info.Environment['LOCALAPPDATA']=$local
$info.Environment['XDG_CACHE_HOME']=Join-Path $local 'cache'
$info.Environment['OPENRAC_MOD_DATA_DIR']=Join-Path $local 'mod-data'
$info.Environment['OPENRAC_DIRECT']='1'
$info.Environment['OPENRAC_GL_API']='vulkan'
$info.Environment['OPENRAC_HIDDEN']='1'
$info.Environment['OPENRAC_UNCAPPED']='1'
$info.Environment['OPENRAC_PRESS']='100:4000:5,k30:8:5'

$stderr=[IO.File]::Open((Join-Path $root 'stderr.log'),[IO.FileMode]::CreateNew)
$stdout=[IO.File]::Open((Join-Path $root 'stdout.log'),[IO.FileMode]::CreateNew)
try {
    $process=[Diagnostics.Process]::Start($info)
    $readErrors=$process.StandardError.BaseStream.CopyToAsync($stderr)
    $readOutput=$process.StandardOutput.BaseStream.CopyToAsync($stdout)
    if(-not $process.WaitForExit(120000)){
        $process.Kill()
        $process.WaitForExit()
        throw 'The isolated Challenge Mode fixture exceeded its time limit.'
    }
    $null=$readErrors.GetAwaiter().GetResult()
    $null=$readOutput.GetAwaiter().GetResult()
    $exit=$process.ExitCode
} finally {
    $stderr.Dispose()
    $stdout.Dispose()
}
$cardAfter=@(Get-ChildItem -LiteralPath $cards -Recurse -File | Sort-Object FullName | Get-FileHash)
$cardUntouched=-not [bool](Compare-Object $cardBefore $cardAfter -Property Path,Hash)
$messages=@(Get-Content -LiteralPath (Join-Path $root 'stderr.log') |
    Where-Object {$_ -cmatch 'CHALLENGE_QA|mod revolverator .*loaded'})
$receipt=[ordered]@{
    runtime_sha256=(Get-FileHash -LiteralPath $runtimePath).Hash.ToLowerInvariant()
    probe_sha256=$probeSha.ToLowerInvariant()
    revolverator_sha256=if($candidateSha){$candidateSha.ToLowerInvariant()}else{$null}
    native_seed_sha256=(Get-FileHash -LiteralPath (Join-Path $fixtureCardPath "$cardDirectory/save0.bin")).Hash.ToLowerInvariant()
    arsenal_seed_sha256=(Get-FileHash -LiteralPath (Join-Path $fixtureCardPath "$cardDirectory/save0.bin.arsenal")).Hash.ToLowerInvariant()
    exit_code=$exit
    card_unchanged=$cardUntouched
    passed=$exit -eq 0 -and $cardUntouched -and [bool]($messages -match 'CHALLENGE_QA result pass=1') -and
        (-not $candidateSha -or [bool]($messages -match 'mod revolverator .*loaded'))
    messages=$messages
}
$receipt | ConvertTo-Json -Depth 4 | Tee-Object -FilePath (Join-Path $root 'result.json')
if(!$receipt.passed){throw "Challenge Mode validation failed. See the result in $root"}
