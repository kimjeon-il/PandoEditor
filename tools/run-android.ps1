param(
    [string]$AndroidRoot = (Join-Path $env:USERPROFILE 'AppData/Local/Pandoeditor/android'),
    [string]$QtRoot = (Join-Path $env:USERPROFILE 'Qt'),
    [string]$BuildTools = (Join-Path $env:USERPROFILE 'AppData/Local/Pandoeditor/installer/Scripts'),
    [string]$DeviceSerial,
    [switch]$StartEmulator
)
. "$PSScriptRoot/android-env.ps1" -AndroidRoot $AndroidRoot -QtRoot $QtRoot -BuildTools $BuildTools
Assert-PandoTool $PandoAdb
if ($StartEmulator) {
    Assert-PandoTool $PandoEmulator
    & $PandoEmulator -accel-check
    Assert-PandoExit 'Emulator acceleration check (Windows configuration is not changed automatically)'
    if (!(Test-Path "$env:ANDROID_HOME/system-images/android-35/default/x86_64/package.xml")) {
        throw 'Install the Android 35 image with install-android.ps1 -IncludeSystemImage first.'
    }
    New-Item -ItemType Directory -Force -Path $env:ANDROID_AVD_HOME | Out-Null
    $pandoAvdName = 'Pandoeditor_API35'
    $pandoAvds = & $PandoEmulator -list-avds
    if ($pandoAvdName -notin $pandoAvds) {
        'no' | & $PandoAvdManager create avd -n $pandoAvdName -k 'system-images;android-35;default;x86_64' -d pixel_5
        Assert-PandoExit 'AVD creation'
    }
    Start-Process -FilePath $PandoEmulator -WindowStyle Hidden -ArgumentList @('-avd',$pandoAvdName,'-port','5580','-no-snapshot','-no-window','-gpu','auto') | Out-Null
    $DeviceSerial = 'emulator-5580'
}
if (!$DeviceSerial) { throw 'Specify -DeviceSerial or -StartEmulator; an arbitrary connected device is never selected.' }
$pandoDeadline = [DateTime]::UtcNow.AddMinutes(3)
do {
    $pandoBooted = & $PandoAdb -s $DeviceSerial shell getprop sys.boot_completed 2>$null
    if ($LASTEXITCODE -eq 0 -and "$pandoBooted".Trim() -eq '1') { break }
    Start-Sleep -Seconds 2
} while ([DateTime]::UtcNow -lt $pandoDeadline)
if ("$pandoBooted".Trim() -ne '1') { throw "Device $DeviceSerial did not boot within 3 minutes." }
$pandoApk = Join-Path $PandoAndroidRoot 'build-x86_64/app/android-build/build/outputs/apk/debug/android-build-debug.apk'
Assert-PandoTool $pandoApk
& $PandoAdb -s $DeviceSerial install -r $pandoApk
Assert-PandoExit 'APK installation'
& $PandoAdb -s $DeviceSerial shell am start -n 'org.pandolab.pandoeditor/org.qtproject.qt.android.bindings.QtActivity'
Assert-PandoExit 'App launch'
