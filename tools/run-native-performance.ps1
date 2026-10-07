param(
 [Parameter(Mandatory=$true)][string]$ProbeExecutable,
 [Parameter(Mandatory=$true)][string]$QtBin,
 [string]$AdditionalQtPluginDirectory,
 [Parameter(Mandatory=$true)][string]$OutputDirectory,
 [Parameter(Mandatory=$true)][string]$FixtureManifest,
 [ValidateSet('acceptance','diagnostic')][string]$Mode='acceptance',
 [string]$DiagnosticReason,
 [ValidateRange(1,20)][int]$Runs=3,
 [ValidateRange(60000,3600000)][int]$WarmupMs=60000,
 [ValidateRange(720000,7200000)][int]$RepeatMs=720000,
 [int]$TimeoutSeconds=2400
)
# Run the existing Release test executable against installed Qt. No runtime/package copy.
# Fixture manifest schema: pandoeditor-native-performance-fixtures/version2; four
# fixtures {id,projectPath,projectSha256,viewPath,viewSha256,assets:[{path,sha256}],
# fullStack,expectedFinalStates,inputContractPath,inputContractSha256}; paths relative to the manifest.
$ErrorActionPreference='Stop'
$probePath=(Resolve-Path -LiteralPath $ProbeExecutable).Path
$qtPath=(Resolve-Path -LiteralPath $QtBin).Path
$manifestPath=(Resolve-Path -LiteralPath $FixtureManifest).Path
if(Test-Path -LiteralPath $OutputDirectory){throw 'Use a new evidence output directory'}
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
$reportRoot=(Resolve-Path -LiteralPath $OutputDirectory).Path
$manifest=Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$fixtureRoot=Split-Path -Parent $manifestPath
$missing=[Collections.Generic.List[string]]::new();$hashes=@{}
$hashes[$manifestPath]=(Get-FileHash -LiteralPath $manifestPath).Hash
if($AdditionalQtPluginDirectory){
 $AdditionalQtPluginDirectory=(Resolve-Path -LiteralPath $AdditionalQtPluginDirectory).Path
 $supplementalImageCodec=Join-Path $AdditionalQtPluginDirectory 'imageformats/qwebp.dll'
 if(!(Test-Path -LiteralPath $supplementalImageCodec -PathType Leaf)){throw 'Supplemental Qt WebP plugin missing'}
 $hashes[$supplementalImageCodec]=(Get-FileHash -LiteralPath $supplementalImageCodec).Hash
}
function Test-ImmutableInput($relative,$expected){
 if(!$relative -or $expected -notmatch '^[a-fA-F0-9]{64}$'){$missing.Add("Missing path/hash: $relative");return}
 $path=[IO.Path]::GetFullPath((Join-Path $fixtureRoot $relative))
 if(!(Test-Path -LiteralPath $path -PathType Leaf)){$missing.Add("Missing asset: $path");return}
 $actual=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
 if($actual -ne $expected){$missing.Add("SHA256 mismatch: $path");return}
 $hashes[$path]=$actual
}
if($manifest.schema -ne 'pandoeditor-native-performance-fixtures' -or $manifest.version -ne 2){$missing.Add('Immutable fixture schema/version mismatch')}
foreach($id in @('world-standard','dense-view','editing-heavy','large-project')){
 $fixture=@($manifest.fixtures | Where-Object id -eq $id)
 if($fixture.Count -ne 1){$missing.Add("Missing/duplicate fixture: $id");continue}
 $fixture=$fixture[0]
 Test-ImmutableInput $fixture.projectPath $fixture.projectSha256
 Test-ImmutableInput $fixture.viewPath $fixture.viewSha256
 if(!$fixture.assets -or !$fixture.expectedFinalStates -or !$fixture.inputContractPath){$missing.Add("Incomplete fixture contract: $id")}
 Test-ImmutableInput $fixture.inputContractPath $fixture.inputContractSha256
 foreach($asset in $fixture.assets){Test-ImmutableInput $asset.path $asset.sha256}
 if($Mode -eq 'acceptance' -and !$fixture.fullStack){$missing.Add("Reduced fixture forbidden for acceptance: $id")}
}
$productionPlacePath=Join-Path (Split-Path $PSScriptRoot -Parent) 'assets/place/manifest.json'
$productionPlaceRevision=$null;$productionPlaceSha=$null
if(!(Test-Path -LiteralPath $productionPlacePath -PathType Leaf)){$missing.Add('Production place manifest absent')}
else{
 $productionPlaceSha=(Get-FileHash -LiteralPath $productionPlacePath).Hash
 $hashes[$productionPlacePath]=$productionPlaceSha
 $productionPlace=Get-Content -LiteralPath $productionPlacePath -Raw | ConvertFrom-Json
 $productionPlaceRevision=$productionPlace.revision
 if($productionPlaceSha -ne '148660B93529A60640F6A340117F976A7E2822AB8F34FA1814023920FC82F4B7'){$missing.Add('Fixed Web production place manifest SHA256 mismatch')}
 if($productionPlaceRevision -eq 'empty-v1'){$missing.Add('Production place runtime is empty-v1: full Dense and full acceptance blocked')}
}
$preflight=[ordered]@{status=if($missing.Count){'BLOCKED'}else{'PASS'};missing=@($missing);placeDatasetVersion=$productionPlaceRevision;productionPlaceManifestSha256=$productionPlaceSha;fixtureManifestSha256=(Get-FileHash -LiteralPath $manifestPath).Hash}
$preflight | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath "$reportRoot/preflight.json" -Encoding utf8
if($Mode -eq 'acceptance' -and $missing.Count){throw "Acceptance preflight BLOCKED; see $reportRoot/preflight.json"}
if($Mode -eq 'diagnostic' -and !$DiagnosticReason){throw 'Diagnostic mode requires an explicit reduction/synthetic reason'}
if($TimeoutSeconds*1000 -lt $WarmupMs+$RepeatMs+180000){throw 'Timeout cannot cover warm-up, long run and scenario sequence'}
if(!(Test-Path -LiteralPath "$qtPath/Qt6Test.dll")){throw 'Installed Qt6Test.dll missing'}
$metadata=[ordered]@{sourceCommit=(git rev-parse HEAD);sourceDirty=[bool](git status --porcelain --untracked-files=no);binarySha256=(Get-FileHash -LiteralPath $probePath).Hash;fixtureManifestSha256=$preflight.fixtureManifestSha256;buildType='Release';qtBin=$qtPath;os=[Environment]::OSVersion.VersionString;adapters=@(Get-CimInstance Win32_VideoController | Select-Object Name,DriverVersion);cpu=@(Get-CimInstance Win32_Processor | Select-Object Name,NumberOfLogicalProcessors);inputHashes=$hashes;measurementMode=$Mode;diagnosticReason=$DiagnosticReason;runs=$Runs;warmupMs=$WarmupMs;repeatMs=$RepeatMs;acceptedBy=$null}
$metadata | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath "$reportRoot/environment.json" -Encoding utf8
if($Mode -eq 'acceptance' -and $metadata.sourceDirty){throw 'Acceptance requires the final fixed source commit; working tree is dirty'}
$envNames=@('PATH','QT_QPA_PLATFORM','QT_PLUGIN_PATH','QML_IMPORT_PATH','QML2_IMPORT_PATH','QT_QUICK_BACKEND','QSG_RHI_BACKEND','PANDOEDITOR_MAP_RENDERER','QSG_RENDER_TIMING','QSG_RENDERER_DEBUG','PANDOEDITOR_NATIVE_PERF_REPORT','PANDOEDITOR_NATIVE_PERF_FIXTURES','PANDOEDITOR_NATIVE_PERF_MODE','PANDOEDITOR_NATIVE_PERF_REASON','PANDOEDITOR_NATIVE_PERF_PROVENANCE','PANDOEDITOR_NATIVE_PERF_PREFLIGHT','PANDOEDITOR_NATIVE_PERF_WARMUP_MS','PANDOEDITOR_NATIVE_PERF_REPEAT_MS','PANDOEDITOR_NATIVE_PERF_RUN_ID','PANDOEDITOR_NATIVE_PERF_BRIEF','PANDOEDITOR_NATIVE_PERF_ABLATION','PANDOEDITOR_NATIVE_PERF_RECOVERY','PANDOEDITOR_NATIVE_PERF_VIEW')
$saved=@{};foreach($name in $envNames){$saved[$name]=[Environment]::GetEnvironmentVariable($name,'Process')}
try{
 $env:PATH="$qtPath;$env:PATH";$env:QT_QPA_PLATFORM='windows'
 $env:QT_PLUGIN_PATH=Join-Path (Split-Path $qtPath -Parent) 'plugins'
 if($AdditionalQtPluginDirectory){$env:QT_PLUGIN_PATH=$AdditionalQtPluginDirectory+';'+$env:QT_PLUGIN_PATH}
 $env:QML_IMPORT_PATH=Join-Path (Split-Path $qtPath -Parent) 'qml';$env:QML2_IMPORT_PATH=$env:QML_IMPORT_PATH
 $env:QT_QUICK_BACKEND=$null;$env:QSG_RHI_BACKEND=$null;$env:PANDOEDITOR_MAP_RENDERER=$null
 $env:QSG_RENDER_TIMING=$null;$env:QSG_RENDERER_DEBUG=$null
 $env:PANDOEDITOR_NATIVE_PERF_BRIEF=$null;$env:PANDOEDITOR_NATIVE_PERF_ABLATION=$null;$env:PANDOEDITOR_NATIVE_PERF_RECOVERY=$null;$env:PANDOEDITOR_NATIVE_PERF_VIEW=$null
 $env:PANDOEDITOR_NATIVE_PERF_FIXTURES=$manifestPath;$env:PANDOEDITOR_NATIVE_PERF_MODE=$Mode;$env:PANDOEDITOR_NATIVE_PERF_REASON=$DiagnosticReason
 $env:PANDOEDITOR_NATIVE_PERF_PROVENANCE="$reportRoot/environment.json";$env:PANDOEDITOR_NATIVE_PERF_PREFLIGHT="$reportRoot/preflight.json"
 $env:PANDOEDITOR_NATIVE_PERF_WARMUP_MS="$WarmupMs";$env:PANDOEDITOR_NATIVE_PERF_REPEAT_MS="$RepeatMs"
 $failures=[Collections.Generic.List[object]]::new()
 for($run=1;$run -le $Runs;$run++){
  $runRoot=Join-Path $reportRoot "run-$run";New-Item -ItemType Directory -Path $runRoot | Out-Null
  $env:PANDOEDITOR_NATIVE_PERF_REPORT="$runRoot/measurement.json";$env:PANDOEDITOR_NATIVE_PERF_RUN_ID="run-$run"
  $arguments=@('nativePerformanceProbe','-o',('"'+$runRoot+'/qt-test.txt,txt"'))
  # The requested device measurement needs an exposed Windows map window.
  $probe=Start-Process -FilePath $probePath -ArgumentList $arguments -WorkingDirectory (Split-Path $probePath -Parent) -WindowStyle Normal -PassThru -RedirectStandardOutput "$runRoot/stdout.txt" -RedirectStandardError "$runRoot/stderr.txt"
  $nativeHandle=$probe.Handle
  $deadline=[DateTime]::UtcNow.AddSeconds($TimeoutSeconds);$processSamples=[Collections.Generic.List[object]]::new();$timedOut=$false
  while(!$probe.WaitForExit(1000)){
   $probe.Refresh();$processSamples.Add([ordered]@{time=[DateTime]::UtcNow.ToString('o');cpuSeconds=$probe.TotalProcessorTime.TotalSeconds;workingSetBytes=$probe.WorkingSet64;privateBytes=$probe.PrivateMemorySize64;responding=$probe.Responding})
   if([DateTime]::UtcNow -gt $deadline){Stop-Process -Id $probe.Id;$timedOut=$true;break}
  }
  $processSamples | ConvertTo-Json | Set-Content -LiteralPath "$runRoot/process.json" -Encoding utf8
  $probe.Refresh();$nativeExit=if($timedOut){$null}else{$probe.ExitCode}
  $failures.Add([ordered]@{run=$run;nativePid=$probe.Id;timedOut=$timedOut;exitCode=$nativeExit;exitCodeObserved=($null -ne $nativeExit);rawReportExists=(Test-Path -LiteralPath "$runRoot/measurement.json")})
  $failures | ConvertTo-Json | Set-Content -LiteralPath "$reportRoot/run-results.json" -Encoding utf8
  if(!$timedOut -and $null -eq $nativeExit){throw 'Native process exit code unavailable; raw failure preserved'}
  foreach($path in $hashes.Keys){if((Get-FileHash -LiteralPath $path).Hash -ne $hashes[$path]){throw "Immutable fixture changed: $path"}}
  if((Get-FileHash -LiteralPath $probePath).Hash -ne $metadata.binarySha256 -or (git rev-parse HEAD) -ne $metadata.sourceCommit){throw 'Binary/source changed during the fixed repeated run'}
  $failures | ConvertTo-Json | Set-Content -LiteralPath "$reportRoot/run-results.json" -Encoding utf8
 }
 if(@($failures | Where-Object {$_.timedOut -or $_.exitCode -ne 0}).Count){throw 'One or more native runs failed; all raw logs were preserved'}
 Write-Output "Raw evidence: $reportRoot"
 Write-Output 'Qt test completion is not acceptance. Summarize each raw v2 report; acceptedBy remains null.'
}finally{foreach($name in $envNames){[Environment]::SetEnvironmentVariable($name,$saved[$name],'Process')}}
