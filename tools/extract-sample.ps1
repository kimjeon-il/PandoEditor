param([Parameter(Mandatory=$true)][string]$Source)
$ErrorActionPreference = 'Stop'
$sourceMap = Get-Content -LiteralPath $Source -Raw -Encoding utf8 | ConvertFrom-Json
$ids = @('DEU','POL','CZE','AUT','SVK')
$colors = @('#a8c7db','#e6b8a2','#b8d6b0','#d5c4e8','#f0d493')
$countries = @()
for ($i=0; $i -lt $ids.Count; $i++) {
    $features = @($sourceMap.features | Where-Object { $_.properties.ADM0_A3 -eq $ids[$i] })
    if ($features.Count -ne 1) { throw "Expected one feature: $($ids[$i])" }
    $feature = $features[0]
    $geometry = $feature.geometry
    if ($geometry.type -eq 'Polygon') {
        $geometry = @{ type='MultiPolygon'; coordinates=@(,$geometry.coordinates) }
    } elseif ($geometry.type -ne 'MultiPolygon') { throw 'Unsupported geometry' }
    $countries += [ordered]@{ id=$ids[$i]; name=$feature.properties.NAME_KO; color=$colors[$i]; geometry=$geometry }
}
$assetDir = Join-Path $PSScriptRoot '../assets'
New-Item -ItemType Directory -Force -Path $assetDir | Out-Null
$document = [ordered]@{ format='pandoeditor-project'; version=1; countries=$countries }
$document | ConvertTo-Json -Depth 100 -Compress | Set-Content -LiteralPath (Join-Path $assetDir 'sample.pando.json') -Encoding utf8NoBOM
$provenance = [ordered]@{
    sourceFile=[IO.Path]::GetFileName($Source)
    sourceSha256=(Get-FileHash -LiteralPath $Source -Algorithm SHA256).Hash
    dataset='Natural Earth 5.1.1, Admin 0 countries, 1:50m'
    sourceUrl='https://www.naturalearthdata.com/downloads/50m-cultural-vectors/50m-admin-0-countries-2/'
    termsUrl='https://www.naturalearthdata.com/about/terms-of-use/'
    license='Public domain'
    countryIds=$ids
    transformation='Select ADM0_A3; retain every geometry coordinate; normalize Polygon to MultiPolygon; use NAME_KO; assign initial colors.'
    outputSha256=(Get-FileHash -LiteralPath (Join-Path $assetDir 'sample.pando.json') -Algorithm SHA256).Hash
}
$provenance | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $assetDir 'provenance.json') -Encoding utf8NoBOM
