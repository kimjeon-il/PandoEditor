param(
    [string]$AndroidRoot = (Join-Path $env:USERPROFILE 'AppData/Local/Pandoeditor/android'),
    [string]$QtRoot = (Join-Path $env:USERPROFILE 'Qt'),
    [string]$BuildTools = (Join-Path $env:USERPROFILE 'AppData/Local/Pandoeditor/installer/Scripts'),
    [switch]$AcceptLicenses,
    [switch]$IncludeSystemImage
)
. "$PSScriptRoot/android-env.ps1" -AndroidRoot $AndroidRoot -QtRoot $QtRoot -BuildTools $BuildTools
if ($PandoAndroidRoot -match '[^\x00-\x7F]') { throw 'Use an ASCII AndroidRoot path.' }
New-Item -ItemType Directory -Force -Path "$PandoAndroidRoot/downloads" | Out-Null
$pandoDisk = Get-PSDrive -Name ([IO.Path]::GetPathRoot($PandoAndroidRoot).Substring(0,1))
if ($pandoDisk.Free -lt 12GB) { throw 'At least 12 GiB free space is required before installing these packages.' }

function Get-VerifiedArchive([string]$Url, [string]$Path, [string]$Hash) {
    if (!(Test-Path -LiteralPath $Path)) { Invoke-WebRequest -Uri $Url -OutFile $Path }
    if ((Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash -ne $Hash) {
        throw "Checksum mismatch; preserve and inspect this download before retrying: $Path"
    }
}
if (!(Test-Path "$env:JAVA_HOME/bin/java.exe")) {
    Get-VerifiedArchive 'https://github.com/adoptium/temurin17-binaries/releases/download/jdk-17.0.20.1%2B1/OpenJDK17U-jdk_x64_windows_hotspot_17.0.20.1_1.zip' "$PandoAndroidRoot/downloads/jdk17.zip" 'e53a79c3c3d86865bd7e787903884331068e71321714ffd44f145785affc7cb0'
    Expand-Archive -LiteralPath "$PandoAndroidRoot/downloads/jdk17.zip" -DestinationPath "$PandoAndroidRoot/java"
}
if (!(Test-Path $PandoSdkManager)) {
    Get-VerifiedArchive 'https://dl.google.com/android/repository/commandlinetools-win-15859902_latest.zip' "$PandoAndroidRoot/downloads/commandlinetools.zip" '90ae805d20434428bffcb699c290860f19bb5f66a67e6b330067e3de801fb04a'
    $pandoStage = Join-Path $PandoAndroidRoot 'downloads/commandline-stage'
    if (Test-Path $pandoStage) { throw "Existing staging folder: $pandoStage. Inspect it before retrying." }
    Expand-Archive -LiteralPath "$PandoAndroidRoot/downloads/commandlinetools.zip" -DestinationPath $pandoStage
    New-Item -ItemType Directory -Force -Path "$env:ANDROID_HOME/cmdline-tools" | Out-Null
    $pandoFrom = (Resolve-Path "$pandoStage/cmdline-tools").Path
    $pandoTo = [IO.Path]::GetFullPath("$env:ANDROID_HOME/cmdline-tools/22.0")
    if (!$pandoFrom.StartsWith($PandoAndroidRoot + [IO.Path]::DirectorySeparatorChar) -or
        !$pandoTo.StartsWith($PandoAndroidRoot + [IO.Path]::DirectorySeparatorChar) -or (Test-Path $pandoTo)) {
        throw 'Unexpected command-line tools target; refusing to move.'
    }
    Move-Item -LiteralPath $pandoFrom -Destination $pandoTo
}
Assert-PandoTool "$PandoBuildTools/python.exe"
if (!(Test-Path "$PandoQtAndroid/lib/cmake/Qt6/qt.toolchain.cmake")) {
    Push-Location $PandoAndroidRoot
    try {
        & "$PandoBuildTools/python.exe" -m aqt install-qt all_os android 6.8.3 android_x86_64 -O $PandoQtRoot --timeout 20
        Assert-PandoExit 'Qt Android installation'
    } finally { Pop-Location }
}
$pandoPackages = @('platform-tools', 'platforms;android-35', 'build-tools;35.0.0', 'ndk;26.1.10909125', 'emulator')
if ($IncludeSystemImage) { $pandoPackages += 'system-images;android-35;default;x86_64' }
if ($AcceptLicenses) {
    1..30 | ForEach-Object { 'y' } | & $PandoSdkManager "--sdk_root=$env:ANDROID_HOME" @pandoPackages
} else {
    & $PandoSdkManager "--sdk_root=$env:ANDROID_HOME" @pandoPackages
}
Assert-PandoExit 'Android SDK installation'
& "$env:JAVA_HOME/bin/java.exe" -version
& $PandoEmulator -accel-check
if ($LASTEXITCODE -ne 0) { Write-Warning 'Emulator acceleration unavailable. APK build is still possible. No Windows features were changed.' }
