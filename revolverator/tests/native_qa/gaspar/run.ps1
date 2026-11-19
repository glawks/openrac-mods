param(
    [Parameter(Mandatory)][string]$Runtime,
    [Parameter(Mandatory)][string]$Data,
    [Parameter(Mandatory)][string]$Levels,
    [Parameter(Mandatory)][string]$Probe,
    [Parameter(Mandatory)][string]$Output,
    [string]$Candidate,
    [int]$Frames=2100
)
$ErrorActionPreference='Stop'
$runtimePath=(Resolve-Path -LiteralPath $Runtime).Path
$dataPath=(Resolve-Path -LiteralPath $Data).Path
$levelsPath=(Resolve-Path -LiteralPath $Levels).Path
$probePath=(Resolve-Path -LiteralPath $Probe).Path
$root=$ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Output)
if(Test-Path -LiteralPath $root){throw "An isolated output directory must be new: $root"}
New-Item -ItemType Directory -Path $root | Out-Null
$plugins=Join-Path $root 'plugins'
$dataRoot=Join-Path $root 'data'
$cards=Join-Path $root 'cards'
$local=Join-Path $root 'local'
New-Item -ItemType Directory -Path $plugins,$dataRoot,$cards,$local | Out-Null
New-Item -ItemType Junction -Path (Join-Path $dataRoot 'iso_data') -Target $dataPath | Out-Null

$mods=@()
$candidateSha=$null
if($Candidate){
    $modFolder=Join-Path $plugins 'revolverator'
    $source=Get-Item -LiteralPath $Candidate
    if($source.PSIsContainer){
        Copy-Item -LiteralPath $source.FullName -Destination $modFolder -Recurse
    }else{
        Expand-Archive -LiteralPath $source.FullName -DestinationPath $modFolder
    }
    $candidateSha=(Get-FileHash -LiteralPath (Join-Path $modFolder 'lib/windows-x86_64/revolverator.dll')).Hash
    $mods+=@{id='revolverator';enabled=$true;settings=@{start_unlocked=$true;trace=$true}}
}
$probeFolder=Join-Path $plugins 'gaspar-helmet-probe'
Copy-Item -LiteralPath $probePath -Destination $probeFolder -Recurse
$probeSha=(Get-FileHash -LiteralPath (Join-Path $probeFolder 'lib/windows-x86_64/gaspar_helmet_probe.dll')).Hash
$catalog=Join-Path $plugins 'plugins.json'
$settings=Join-Path $root 'settings.ini'
@{format=1;mods=@($mods+@{id='gaspar-helmet-probe';enabled=$true})} |
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
foreach($arg in @('--data',$dataRoot,'--levels',$levelsPath,'--cards',$cards,
                  '--plugins',$catalog,'--settings',$settings,'--skip-movies','--window',
                  '--frames',"$Frames",'--no-card','--level','9')){
    $info.ArgumentList.Add($arg)
}
$info.Environment['LOCALAPPDATA']=$local
$info.Environment['OPENRAC_MOD_DATA_DIR']=Join-Path $local 'mod-data'
$info.Environment['OPENRAC_DIRECT']='1'
$info.Environment['OPENRAC_GL_API']='vulkan'
$info.Environment['OPENRAC_HIDDEN']='1'
$info.Environment['OPENRAC_UNCAPPED']='1'
$info.Environment['OPENRAC_PRESS']='100:4000:5,k30:8:5'

$stderr=[IO.File]::Open((Join-Path $root 'stderr.log'),[IO.FileMode]::CreateNew)
$stdout=[IO.File]::Open((Join-Path $root 'stdout.log'),[IO.FileMode]::CreateNew)
try{
    $process=[Diagnostics.Process]::Start($info)
    $readErrors=$process.StandardError.BaseStream.CopyToAsync($stderr)
    $readOutput=$process.StandardOutput.BaseStream.CopyToAsync($stdout)
    if(-not $process.WaitForExit(120000)){
        $process.Kill()
        $process.WaitForExit()
        throw "The isolated Gaspar fixture exceeded its time limit."
    }
    $null=$readErrors.GetAwaiter().GetResult()
    $null=$readOutput.GetAwaiter().GetResult()
    $exit=$process.ExitCode
}finally{
    $stderr.Dispose()
    $stdout.Dispose()
}
$log=Get-Content -LiteralPath (Join-Path $root 'stderr.log')
$messages=@($log | Where-Object {$_ -cmatch 'GASPAR_HELMET|mod revolverator .*loaded'})
$receipt=[ordered]@{
    runtime_sha256=(Get-FileHash -LiteralPath $runtimePath).Hash.ToLowerInvariant()
    probe_sha256=$probeSha.ToLowerInvariant()
    revolverator_sha256=if($candidateSha){$candidateSha.ToLowerInvariant()}else{$null}
    exit_code=$exit
    passed=$exit -eq 0 -and [bool]($messages -match 'GASPAR_HELMET result pass=1') -and
        (-not $candidateSha -or [bool]($messages -match 'mod revolverator .*loaded'))
    messages=$messages
}
$receipt | ConvertTo-Json -Depth 4 | Tee-Object -FilePath (Join-Path $root 'result.json')
