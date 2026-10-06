# Descarga libmpv (paquete mpv-dev x86_64) de shinchiro/mpv-winbuild-cmake y lo deja en la CACHE
# (por defecto %LOCALAPPDATA%\PikPlayer-build\mpv), fuera de la carpeta del proyecto, para no volver a descargarlo nunca.
param([string]$Cache)
$ErrorActionPreference = 'Stop'
if (-not $Cache) { $Cache = Join-Path $env:LOCALAPPDATA 'PikPlayer-build\mpv' }
$dl  = Join-Path (Split-Path -Parent $Cache) 'downloads'
$tmp = Join-Path $dl 'extract'
New-Item -ItemType Directory -Force -Path $Cache, $dl | Out-Null
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$rel = Invoke-RestMethod -Uri 'https://api.github.com/repos/shinchiro/mpv-winbuild-cmake/releases/latest' -Headers @{ 'User-Agent' = 'PikPlayer' }
$asset = $rel.assets | Where-Object { $_.name -match '^mpv-dev-x86_64-\d{8}-git-.*\.7z$' } | Select-Object -First 1
if (-not $asset) { throw 'No se encontro el paquete mpv-dev en la ultima release.' }

# Si el .7z ya esta descargado completo, se reutiliza.
$arc = Join-Path $dl $asset.name
if (-not (Test-Path $arc) -or (Get-Item $arc).Length -ne [int64]$asset.size) {
    Write-Host "Descargando $($asset.name) ..."
    Invoke-WebRequest -Uri $asset.browser_download_url -OutFile $arc -UseBasicParsing
} else { Write-Host "Usando descarga existente: $($asset.name)" }

if (Test-Path $tmp) { Remove-Item $tmp -Recurse -Force }
New-Item -ItemType Directory -Force -Path $tmp | Out-Null

$sz = @("$env:ProgramFiles\7-Zip\7z.exe", "${env:ProgramFiles(x86)}\7-Zip\7z.exe") | Where-Object { Test-Path $_ } | Select-Object -First 1
$ok = $false
if ($sz) { & $sz x $arc "-o$tmp" -y | Out-Null; $ok = ($LASTEXITCODE -eq 0) }
if (-not $ok) { & tar.exe -xf $arc -C $tmp 2>$null; $ok = ($LASTEXITCODE -eq 0) }
if (-not $ok) {   # ultimo recurso: 7zr.exe (extractor autonomo oficial de 7-Zip)
    $zr = Join-Path $dl '7zr.exe'
    if (-not (Test-Path $zr)) { Invoke-WebRequest -Uri 'https://www.7-zip.org/a/7zr.exe' -OutFile $zr -UseBasicParsing }
    & $zr x $arc "-o$tmp" -y | Out-Null; $ok = ($LASTEXITCODE -eq 0)
}
if (-not $ok) { throw 'No se pudo extraer el .7z.' }

# Localiza la DLL y las cabeceras estén donde estén dentro del paquete y las normaliza en la cache.
$dll = Get-ChildItem $tmp -Recurse -File | Where-Object { $_.Extension -eq '.dll' -and $_.Name -like '*mpv*' } | Select-Object -First 1
$hdr = Get-ChildItem $tmp -Recurse -File -Filter 'client.h' | Where-Object { $_.Directory.Name -eq 'mpv' } | Select-Object -First 1
if (-not $dll -or -not $hdr) { throw 'El paquete descargado no contiene libmpv (dll + include\mpv\client.h).' }

Get-ChildItem $Cache -Force | Remove-Item -Recurse -Force
Copy-Item $dll.FullName (Join-Path $Cache $dll.Name) -Force
Copy-Item $hdr.Directory.Parent.FullName (Join-Path $Cache 'include') -Recurse -Force
Remove-Item $tmp -Recurse -Force
Write-Host "libmpv guardada en la cache: $Cache"
