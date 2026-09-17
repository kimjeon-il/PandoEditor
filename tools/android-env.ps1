param(
    [string]$AndroidRoot = (Join-Path $env:USERPROFILE 'AppData/Local/Pandoeditor/android'),
    [string]$QtRoot = (Join-Path $env:USERPROFILE 'Qt'),
    [string]$BuildTools = (Join-Path $env:USERPROFILE 'AppData/Local/Pandoeditor/installer/Scripts')
)
$ErrorActionPreference = 'Stop'
$PandoAndroidRoot = [IO.Path]::GetFullPath($AndroidRoot)
$PandoQtRoot = [IO.Path]::GetFullPath($QtRoot)
$PandoBuildTools = [IO.Path]::GetFullPath($BuildTools)
$env:JAVA_HOME = Join-Path $PandoAndroidRoot 'java/jdk-17.0.20.1+1'
$env:ANDROID_HOME = Join-Path $PandoAndroidRoot 'sdk'
$env:ANDROID_SDK_ROOT = $env:ANDROID_HOME
$env:ANDROID_NDK_ROOT = Join-Path $env:ANDROID_HOME 'ndk/26.1.10909125'
$env:ANDROID_USER_HOME = Join-Path $PandoAndroidRoot 'user'
$env:ANDROID_AVD_HOME = Join-Path $PandoAndroidRoot 'avd'
$env:GRADLE_USER_HOME = Join-Path $PandoAndroidRoot 'gradle'
$PandoSdkManager = Join-Path $env:ANDROID_HOME 'cmdline-tools/22.0/bin/sdkmanager.bat'
$PandoAvdManager = Join-Path $env:ANDROID_HOME 'cmdline-tools/22.0/bin/avdmanager.bat'
$PandoAdb = Join-Path $env:ANDROID_HOME 'platform-tools/adb.exe'
$PandoEmulator = Join-Path $env:ANDROID_HOME 'emulator/emulator.exe'
$PandoCMake = Join-Path $PandoBuildTools 'cmake.exe'
$PandoQtHost = Join-Path $PandoQtRoot '6.8.3/mingw_64'
$PandoQtAndroid = Join-Path $PandoQtRoot '6.8.3/android_x86_64'
$env:PATH = "$env:JAVA_HOME/bin;$PandoQtHost/bin;$PandoQtRoot/Tools/mingw1310_64/bin;$PandoBuildTools;$env:ANDROID_HOME/platform-tools;" + $env:PATH

function Assert-PandoTool([string]$Path) {
    if (!(Test-Path -LiteralPath $Path)) { throw "Missing tool: $Path. Run tools/install-android.ps1 first." }
}
function Assert-PandoExit([string]$Operation) {
    if ($LASTEXITCODE -ne 0) { throw "$Operation failed (exit $LASTEXITCODE)." }
}
