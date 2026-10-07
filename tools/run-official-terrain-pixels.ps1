param(
    [Parameter(Mandatory=$true)][string]$Binary,
    [Parameter(Mandatory=$true)][string]$DemRoot,
    [Parameter(Mandatory=$true)][string]$RasterRoot,
    [Parameter(Mandatory=$true)][string]$Evidence,
    [string]$Function='originalData'
)
$ErrorActionPreference='Stop'
$source=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if(Test-Path -LiteralPath $Evidence){throw 'Use a fresh evidence directory; old failures must be preserved'}
New-Item -ItemType Directory -Path $Evidence | Out-Null
$verified=@()
foreach($pair in @(@{inventory='physical-inventory-terrain-dem-c3c18d1.json';root=$DemRoot;dem=$true},@{inventory='physical-inventory-c0bd31d1.json';root=$RasterRoot;dem=$false})) {
    $inventory=Get-Content -LiteralPath (Join-Path $source ('assets/world/'+$pair.inventory)) -Raw | ConvertFrom-Json
    $assets=if($pair.dem){@($inventory.assets)}else{@($inventory.assets | Where-Object {$_.path -in @('terrain/v0.12.6/0/0-0.webp','terrain/v0.12.6/0/1-0.webp')})}
    foreach($asset in $assets) {
        $path=Join-Path $pair.root $asset.path
        if(!(Test-Path -LiteralPath $path)){throw "Missing immutable source asset: $path"}
        $actual=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
        if((Get-Item -LiteralPath $path).Length -ne $asset.bytes -or $actual -ne $asset.sha256){throw "Source inventory mismatch: $path"}
        $verified+=@{path=$path;sha256=$actual;bytes=$asset.bytes}
    }
}
$verified|ConvertTo-Json -Depth 4|Set-Content -LiteralPath (Join-Path $Evidence 'verified-assets.json') -Encoding utf8
$env:PANDOEDITOR_OFFICIAL_DEM_ROOT=$DemRoot
$env:PANDOEDITOR_OFFICIAL_RASTER_ROOT=$RasterRoot
$env:PANDOEDITOR_OFFICIAL_DEM_MANIFEST=Join-Path $source 'assets/world/terrain/v0.13.3/manifest.json'
$env:PANDOEDITOR_OFFICIAL_RASTER_MANIFEST=Join-Path $source 'assets/world/terrain/v0.12.6/manifest.json'
$env:PANDOEDITOR_UI_CAPTURE_DIR=Join-Path $Evidence 'captures'
$env:QT_QPA_PLATFORM='windows';$env:QT_QUICK_BACKEND=$null;$env:QSG_RHI_BACKEND='d3d11';$env:QSG_INFO='1'
$log=Join-Path $Evidence 'qt.txt'
$native=Start-Process -FilePath $Binary -ArgumentList @($Function,'-o',('"'+$log+',txt"')) -WorkingDirectory $source -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $Evidence 'stdout.txt') -RedirectStandardError (Join-Path $Evidence 'stderr.txt')
$handle=$native.Handle
if(!$native.WaitForExit(180000)){Stop-Process -Id $native.Id;$native.WaitForExit();throw 'Official RHI process timed out; not a pass'}
$native.Refresh();if($null -eq $native.ExitCode){throw 'Native OS exit unavailable'}
$raw=Get-Content -LiteralPath $log -Raw
$processed=@([regex]::Matches($raw,'OFFICIAL_PIXEL case=')).Count
$totals=[regex]::Match($raw,'Totals: (\d+) passed, (\d+) failed, (\d+) skipped')
$expected=if($Function -eq 'originalData'){16}else{1}
$passed=$native.ExitCode -eq 0 -and $processed -eq $expected -and $totals.Success -and [int]$totals.Groups[1].Value -eq ($expected+2) -and [int]$totals.Groups[2].Value -eq 0 -and [int]$totals.Groups[3].Value -eq 0
[ordered]@{nativePID=$native.Id;nativeExit=$native.ExitCode;expected=$expected;processed=$processed;passed=$passed;binarySha256=(Get-FileHash -LiteralPath $Binary).Hash;appSHA=(git -C $source rev-parse HEAD);dirty=[bool](git -C $source status --porcelain);webSHA='ebcfae4d27b29cbbea6416a7045a4806930204be';verifiedAssetCount=$verified.Count;syntheticPixels=$false;oracle='Independent pinned-Web arithmetic over original decoded image channels; four samples per phase; not whole-screen browser parity';gpuFenceObserved=$false}|ConvertTo-Json -Depth 4|Set-Content -LiteralPath (Join-Path $Evidence 'receipt.json') -Encoding utf8
Get-Content -LiteralPath $log -Tail 22
Write-Output "NativePID=$($native.Id) NativeExit=$($native.ExitCode) expected=$expected processed=$processed"
if(!$passed){exit 1}
exit 0
