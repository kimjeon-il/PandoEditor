param(
    [string]$AndroidRoot = (Join-Path $env:USERPROFILE 'AppData/Local/Pandoeditor/android'),
    [string]$QtRoot = (Join-Path $env:USERPROFILE 'Qt'),
    [string]$BuildTools = (Join-Path $env:USERPROFILE 'AppData/Local/Pandoeditor/installer/Scripts'),
    [string]$SourcePath = (Split-Path $PSScriptRoot -Parent)
)
. "$PSScriptRoot/android-env.ps1" -AndroidRoot $AndroidRoot -QtRoot $QtRoot -BuildTools $BuildTools
Assert-PandoTool $PandoCMake
Assert-PandoTool "$env:JAVA_HOME/bin/java.exe"
Assert-PandoTool "$env:ANDROID_NDK_ROOT/build/cmake/android.toolchain.cmake"
Assert-PandoTool "$PandoQtAndroid/lib/cmake/Qt6/qt.toolchain.cmake"
Assert-PandoTool "$PandoQtHost/bin/moc.exe"
$pandoSource = (Resolve-Path $SourcePath).Path
$pandoLink = Join-Path $PandoAndroidRoot 'source'
if ($pandoSource -match '[^\x00-\x7F]') {
    if (!(Test-Path -LiteralPath $pandoLink)) {
        New-Item -ItemType Junction -Path $pandoLink -Target $pandoSource | Out-Null
    } elseif ((Get-Item -LiteralPath $pandoLink).LinkTarget -ne $pandoSource) {
        throw "Existing source junction has another target: $pandoLink"
    }
    $pandoSource = $pandoLink
}
$pandoBuild = Join-Path $PandoAndroidRoot 'build-x86_64'
& $PandoCMake -S $pandoSource -B $pandoBuild -G Ninja `
    '-DCMAKE_BUILD_TYPE=Debug' '-DBUILD_TESTING=OFF' `
    "-DCMAKE_TOOLCHAIN_FILE=$PandoQtAndroid/lib/cmake/Qt6/qt.toolchain.cmake" `
    "-DQT_HOST_PATH=$PandoQtHost" "-DANDROID_SDK_ROOT=$env:ANDROID_HOME" `
    "-DANDROID_NDK_ROOT=$env:ANDROID_NDK_ROOT" '-DANDROID_ABI=x86_64' `
    '-DANDROID_PLATFORM=android-28' '-DQT_ANDROID_SDK_BUILD_TOOLS_REVISION=35.0.0'
Assert-PandoExit 'Android CMake configure'
& $PandoCMake --build $pandoBuild --target apk --parallel 4
Assert-PandoExit 'Android APK build'
Get-ChildItem "$pandoBuild/app/android-build" -Filter '*.apk' -Recurse | Select-Object FullName, Length
