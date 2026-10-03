param(
    [Parameter(Mandatory=$true)][string]$ProbeExecutable,
    [Parameter(Mandatory=$true)][string]$PackageDirectory,
    [Parameter(Mandatory=$true)][string]$QtBin,
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [string]$RecoveryProject,
    [string]$ComparisonView,
    [switch]$BriefBaseline,
    [int]$TimeoutSeconds=180
)
# Build ui_tests in Release first. Do not build/run another benchmark concurrently.
# Keep the diagnostic window visible and unchanged until it closes itself.
# This runner only copies recovery inputs; production settings are never written.
$ErrorActionPreference='Stop'
$probePath=(Resolve-Path -LiteralPath $ProbeExecutable).Path
$packagePath=(Resolve-Path -LiteralPath $PackageDirectory).Path
$qtPath=(Resolve-Path -LiteralPath $QtBin).Path
if(Test-Path -LiteralPath $OutputDirectory){throw 'Use a new output directory for each measurement'}
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
$reportRoot=(Resolve-Path -LiteralPath $OutputDirectory).Path
$runtime=Join-Path $reportRoot 'runtime'
Copy-Item -LiteralPath $packagePath -Destination $runtime -Recurse
Copy-Item -LiteralPath $probePath -Destination "$runtime/ui_tests.exe"
Copy-Item -LiteralPath "$qtPath/Qt6Test.dll" -Destination "$runtime/Qt6Test.dll"
$inputHashes=@{}
if($ComparisonView){
    $comparisonViewPath=(Resolve-Path -LiteralPath $ComparisonView).Path
    Copy-Item -LiteralPath $comparisonViewPath -Destination "$reportRoot/comparison-view.json"
    $inputHashes[$comparisonViewPath]=(Get-FileHash -LiteralPath $comparisonViewPath).Hash
}
if($RecoveryProject){
    $recoveryPath=(Resolve-Path -LiteralPath $RecoveryProject).Path
    New-Item -ItemType Directory -Path "$reportRoot/input" | Out-Null
    Copy-Item -LiteralPath $recoveryPath -Destination "$reportRoot/input/autosave-project.json"
    $inputHashes[$recoveryPath]=(Get-FileHash -LiteralPath $recoveryPath).Hash
    $view=Join-Path (Split-Path $recoveryPath) 'autosave-view.json'
    if(Test-Path -LiteralPath $view){
        Copy-Item -LiteralPath $view -Destination "$reportRoot/input/autosave-view.json"
        $inputHashes[$view]=(Get-FileHash -LiteralPath $view).Hash
    }
}
$metadata=[ordered]@{
    probeSHA256=(Get-FileHash -LiteralPath $probePath).Hash
    sourceHead=(git rev-parse HEAD)
    sourceDirty=[bool](git status --porcelain --untracked-files=no)
    graphicsOverrides=@{QT_QUICK_BACKEND=$env:QT_QUICK_BACKEND;QSG_RHI_BACKEND=$env:QSG_RHI_BACKEND;PANDOEDITOR_MAP_RENDERER=$env:PANDOEDITOR_MAP_RENDERER}
    adapters=@(Get-CimInstance Win32_VideoController | Select-Object Name,DriverVersion)
    cpu=@(Get-CimInstance Win32_Processor | Select-Object Name,NumberOfLogicalProcessors)
    inputHashes=$inputHashes
}
$metadata | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath "$reportRoot/environment.json" -Encoding utf8
$env:PATH="$env:SystemRoot/System32;$env:SystemRoot;$env:SystemRoot/System32/Wbem"
$env:QT_QPA_PLATFORM='windows'
$env:QT_QUICK_BACKEND=$null;$env:QSG_RHI_BACKEND=$null;$env:PANDOEDITOR_MAP_RENDERER=$null
$env:QSG_RENDER_TIMING=$null;$env:QSG_RENDERER_DEBUG=$null
$env:QT_PLUGIN_PATH=$null;$env:QML_IMPORT_PATH=$null;$env:QML2_IMPORT_PATH=$null
$env:QSG_INFO='1';$env:QT_LOGGING_RULES='qt.scenegraph.general=true;qt.rhi.general=true'
$env:PANDOEDITOR_NATIVE_PERF_REPORT="$reportRoot/measurement.json"
$env:PANDOEDITOR_NATIVE_PERF_RECOVERY=if($RecoveryProject){"$reportRoot/input/autosave-project.json"}else{$null}
$env:PANDOEDITOR_NATIVE_PERF_BRIEF=if($BriefBaseline){'1'}else{$null}
$env:PANDOEDITOR_NATIVE_PERF_ABLATION=$null
$env:PANDOEDITOR_NATIVE_PERF_WARMUP_MS=$null
$env:PANDOEDITOR_NATIVE_PERF_VIEW=if($ComparisonView){"$reportRoot/comparison-view.json"}else{$null}
$arguments=@('nativePerformanceProbe','-o',('"'+$reportRoot+'/qt-test.txt,txt"'))
$probe=Start-Process -FilePath "$runtime/ui_tests.exe" -ArgumentList $arguments -WorkingDirectory $runtime -WindowStyle Hidden -PassThru -RedirectStandardOutput "$reportRoot/stdout.txt" -RedirectStandardError "$reportRoot/stderr.txt"
$deadline=[DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
$processSamples=[Collections.Generic.List[object]]::new()
$timedOut=$false
while(!$probe.WaitForExit(1000)){
    $probe.Refresh()
    $processSamples.Add([ordered]@{time=[DateTime]::UtcNow.ToString('o');cpuSeconds=$probe.TotalProcessorTime.TotalSeconds;workingSet=$probe.WorkingSet64;privateBytes=$probe.PrivateMemorySize64;responding=$probe.Responding})
    if([DateTime]::UtcNow -gt $deadline){Stop-Process -Id $probe.Id;$timedOut=$true;break}
}
$processSamples | ConvertTo-Json | Set-Content -LiteralPath "$reportRoot/process.json" -Encoding utf8
foreach($inputPath in $inputHashes.Keys){
    if((Get-FileHash -LiteralPath $inputPath).Hash -ne $inputHashes[$inputPath]){
        throw "Original recovery input changed during measurement: $inputPath"
    }
}
if($timedOut){throw "Probe exceeded $TimeoutSeconds seconds; only the diagnostic process was terminated"}
Get-Content -LiteralPath "$reportRoot/qt-test.txt" -Tail 8
if($probe.ExitCode -ne 0){throw "Diagnostic failed: $($probe.ExitCode)"}
Write-Output "Raw measurement: $reportRoot/measurement.json"
Write-Output 'A successful Qt test is NOT a performance pass. Run summarize-native-performance.mjs on the raw measurement.'
