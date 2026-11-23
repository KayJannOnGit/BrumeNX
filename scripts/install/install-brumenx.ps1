# BrumeNX installer for Windows - copies the release zip to a Switch SD card, checks every file, deletes nothing.
# Double-click install-brumenx.bat (it starts this script), or:
#   powershell -ExecutionPolicy Bypass -File install-brumenx.ps1 [-Zip brumenx-v1.0.0.zip] [-Sd E:\]
#
# 1. unzips the release in a temporary folder and checks it against brume\docs\SHA256SUMS.txt;
# 2. checks that the target looks like a Switch SD card with Atmosphere (an "atmosphere" folder);
# 3. copies every file. Identical files are left alone. A file that is already there and DIFFERENT is first saved in
#    <SD>\brumenx-sauvegarde\<date>\ then replaced;
# 4. reads every copied file back and checks its SHA-256.
# It never deletes anything on the card. Works with Windows PowerShell 5.1 (Windows 10 / 11).
#
# FR : installeur de BrumeNX pour Windows. Il copie le zip sur la carte SD, verifie chaque fichier, ne supprime rien.
param([string]$Zip = "", [string]$Sd = "")
$ErrorActionPreference = "Stop"

function Fail([string]$msg) {
    Write-Host ""
    Write-Host "ERREUR / ERROR: $msg" -ForegroundColor Red
    exit 1
}
function Get-Sha([string]$path) { (Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash.ToLowerInvariant() }

$here = Split-Path -Parent $MyInvocation.MyCommand.Path

# --- the zip ---
if ($Zip -eq "") {
    $z = Get-ChildItem -LiteralPath $here -Filter "brumenx-*.zip" | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if ($null -eq $z) { Fail "no brumenx-*.zip next to this script (put the zip in the same folder)" }
    $Zip = $z.FullName
}
if (-not (Test-Path -LiteralPath $Zip -PathType Leaf)) { Fail "zip not found: $Zip" }
Write-Host "BrumeNX installer - $(Split-Path -Leaf $Zip)"

# --- the SD card ---
if ($Sd -eq "") {
    Write-Host ""
    Write-Host "Cards that look like a Switch SD (they have an 'atmosphere' folder):"
    $cands = @()
    foreach ($d in (Get-PSDrive -PSProvider FileSystem)) {
        if ($d.Root -and (Test-Path -LiteralPath (Join-Path $d.Root "atmosphere") -PathType Container)) {
            Write-Host "   $($d.Root)"
            $cands += $d.Root
        }
    }
    if ($cands.Count -eq 0) { Write-Host "   (none found: put the card in, or start Hekate > USB Tools > SD Card, then try again)" }
    Write-Host ""
    $def = ""
    if ($cands.Count -eq 1) { $def = $cands[0] }
    $ans = Read-Host "SD card drive (e.g. E:) / Lecteur de la carte SD [$def]"
    if ($ans -eq "") { $ans = $def }
    $Sd = $ans
}
if ($Sd -match '^[A-Za-z]:?$') { $Sd = $Sd.Substring(0, 1) + ":\" }
if (-not (Test-Path -LiteralPath $Sd -PathType Container)) { Fail "folder not found: $Sd" }
$Sd = (Resolve-Path -LiteralPath $Sd).Path
if ($Sd -eq $env:SystemDrive + "\") { Fail "$Sd is your system drive, not an SD card" }
if (-not (Test-Path -LiteralPath (Join-Path $Sd "atmosphere") -PathType Container)) {
    Fail "$Sd has no 'atmosphere' folder: BrumeNX needs Atmosphere on this card (wrong drive?)"
}

$tmp = Join-Path ([System.IO.Path]::GetTempPath()) ("brumenx-" + [System.Guid]::NewGuid().ToString("N"))
try {
    Write-Host "1/4 Unzipping / decompression..."
    Expand-Archive -LiteralPath $Zip -DestinationPath $tmp
    $sums = Join-Path $tmp "brume\docs\SHA256SUMS.txt"
    if (-not (Test-Path -LiteralPath $sums)) { Fail "this zip has no brume/docs/SHA256SUMS.txt (not a BrumeNX release?)" }
    $entries = @()
    foreach ($line in (Get-Content -LiteralPath $sums -Encoding UTF8)) {
        if ($line.Trim() -eq "") { continue }
        $h = $line.Substring(0, 64); $f = $line.Substring(66)
        if ($f.StartsWith("/") -or $f.Contains("..") -or $f.Contains(":")) { Fail "bad line in SHA256SUMS.txt: $f" }
        $entries += ,@($h, $f)
    }

    Write-Host "2/4 Checking the download / verification du telechargement..."
    foreach ($e in $entries) {
        $p = Join-Path $tmp ($e[1] -replace '/', '\')
        if (-not (Test-Path -LiteralPath $p -PathType Leaf)) { Fail "missing in the zip: $($e[1])" }
        if ((Get-Sha $p) -ne $e[0]) { Fail "damaged file in the zip (download it again): $($e[1])" }
    }
    Write-Host "    $($entries.Count) files OK"

    Write-Host "3/4 Copying to $Sd / copie sur la carte..."
    $backup = Join-Path $Sd ("brumenx-sauvegarde\" + (Get-Date -Format "yyyy-MM-dd_HHmmss"))
    $new = 0; $same = 0; $saved = 0
    $all = @($entries | ForEach-Object { $_[1] }) + @("brume/docs/SHA256SUMS.txt")
    foreach ($rel in $all) {
        $relw = $rel -replace '/', '\'
        $src = Join-Path $tmp $relw
        $dst = Join-Path $Sd $relw
        if (Test-Path -LiteralPath $dst -PathType Leaf) {
            if ((Get-Sha $dst) -eq (Get-Sha $src)) { $same++; continue }
            $b = Join-Path $backup $relw
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $b) | Out-Null
            Copy-Item -LiteralPath $dst -Destination $b
            $saved++
        } else { $new++ }
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $dst) | Out-Null
        Copy-Item -LiteralPath $src -Destination $dst -Force
    }
    # an older HOME menu packed as exefs.nsp would win over BrumeNX's exefs\ (Atmosphère takes the .nsp first): moved to the backup
    $q = Join-Path $Sd "atmosphere\contents\0100000000001000\exefs.nsp"
    if (Test-Path -LiteralPath $q -PathType Leaf) {
        $b = Join-Path $backup "atmosphere\contents\0100000000001000\exefs.nsp"
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $b) | Out-Null
        Copy-Item -LiteralPath $q -Destination $b
        if ((Get-Sha $q) -eq (Get-Sha $b)) {
            Remove-Item -LiteralPath $q
            $saved++
            Write-Host "    an older HOME menu (exefs.nsp) was moved to the backup / un ancien menu (exefs.nsp) est mis de cote"
        }
    }

    Write-Host "4/4 Checking the card / verification de la carte..."
    foreach ($e in $entries) {
        $p = Join-Path $Sd ($e[1] -replace '/', '\')
        if ((Get-Sha $p) -ne $e[0]) { Fail "$($e[1]) was not written correctly: copy again, or check the card" }
    }

    Write-Host ""
    Write-Host "Done / Termine: $new new, $same already there, $saved replaced." -ForegroundColor Green
    if ($saved -gt 0) { Write-Host "Replaced files were saved first in: $backup" }
    Write-Host ""
    Write-Host "Next: eject the card safely, put it back in the Switch, boot Atmosphere on your emuMMC."
    Write-Host "Ensuite : ejecte la carte proprement, remets-la dans la Switch, demarre Atmosphere sur ton emuMMC."
    Write-Host "Rescue / secours: BRUMENX-LISEZMOI-README.txt (at the root of the card / a la racine de la carte)."
}
finally {
    if (Test-Path -LiteralPath $tmp) { Remove-Item -LiteralPath $tmp -Recurse -Force }   # (only our own temporary folder)
}
