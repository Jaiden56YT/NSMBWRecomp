# NSMBW Online updater (Windows). Run it with Update.bat. It checks the builds repo's latest release
# and, when it is newer than build.txt, replaces the program files. The disc folder (your game) and
# UserData (saves, settings, controls) are never touched.
#
# The builds repo is private: you need a GitHub account the owner invited, and a token from it
# (github.com -> Settings -> Developer settings -> Personal access tokens -> Tokens (classic) ->
# Generate new token, tick "repo"). The first run asks for it and keeps it in UserData.
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $here
function Fail($msg) { Write-Host ""; Write-Host "Update failed: $msg"; Read-Host "Press Enter to close" | Out-Null; exit 1 }

$repo = (Get-Content update.cfg -ErrorAction SilentlyContinue | Where-Object { $_ -match '^repo\s*=' } |
    Select-Object -First 1) -replace '^repo\s*=\s*', ''
if (-not $repo) { Fail "update.cfg has no 'repo = owner/name' line." }
$prefix = 'NSMBW-Online-8P-Windows-'
$tokenFile = Join-Path $here 'UserData\github_token.txt'
$current = (Get-Content build.txt -ErrorAction SilentlyContinue | Select-Object -First 1)
New-Item -ItemType Directory -Force (Join-Path $here 'UserData') | Out-Null

$token = $env:NSMBW_UPDATE_TOKEN
if (-not $token -and (Test-Path $tokenFile)) { $token = (Get-Content $tokenFile -Raw).Trim() }
if (-not $token) {
    $secure = Read-Host "First update: paste your GitHub token and press Enter" -AsSecureString
    $token = [Runtime.InteropServices.Marshal]::PtrToStringAuto([Runtime.InteropServices.Marshal]::SecureStringToBSTR($secure))
    if (-not $token) { Fail "no token given." }
    Set-Content -Path $tokenFile -Value $token -NoNewline
}
$headers = @{ Authorization = "Bearer $token"; Accept = 'application/vnd.github+json' }

Write-Host "Checking for updates (you have: $current)..."
try { $release = Invoke-RestMethod -Headers $headers "https://api.github.com/repos/$repo/releases/latest" }
catch {
    $code = $_.Exception.Response.StatusCode.value__
    if ($code -eq 401 -or $code -eq 404) {
        Remove-Item $tokenFile -ErrorAction SilentlyContinue
        Fail "GitHub refused the token or you have no access to $repo (ask to be invited). Run Update again to enter a new token."
    }
    Fail "no answer from GitHub ($($_.Exception.Message))."
}
$tag = $release.tag_name
if ($tag -eq $current) { Write-Host "You already have the latest version ($tag)."; Read-Host "Press Enter to close" | Out-Null; exit 0 }
$asset = $release.assets | Where-Object { $_.name -like "$prefix*.zip" } | Select-Object -First 1
if (-not $asset) { Fail "release $tag has no Windows download." }
if (Get-Process NSMBWCompiled -ErrorAction SilentlyContinue | Where-Object { $_.Path -like "$here*" }) { Fail "close the game first." }

$tmp = Join-Path ([IO.Path]::GetTempPath()) ("nsmbw-update-" + [guid]::NewGuid())
New-Item -ItemType Directory $tmp | Out-Null
try {
    Write-Host "Downloading $($asset.name)..."
    Invoke-WebRequest -Headers @{ Authorization = "Bearer $token"; Accept = 'application/octet-stream' } `
        -Uri "https://api.github.com/repos/$repo/releases/assets/$($asset.id)" -OutFile "$tmp\new.zip"
    Expand-Archive "$tmp\new.zip" "$tmp\x"
    $new = Get-ChildItem "$tmp\x" -Directory | Select-Object -First 1
    if (-not $new -or -not (Test-Path (Join-Path $new.FullName 'NSMBWCompiled.exe'))) { Fail "the download has no NSMBWCompiled.exe." }
    # Copy everything except the disc folder and UserData over this folder.
    Get-ChildItem $new.FullName | Where-Object { $_.Name -ne 'disc' -and $_.Name -ne 'UserData' } | ForEach-Object {
        Copy-Item $_.FullName -Destination $here -Recurse -Force
    }
} finally { Remove-Item $tmp -Recurse -Force -ErrorAction SilentlyContinue }
Write-Host "Updated to $tag. Your game files, saves and settings were kept."
Read-Host "Press Enter to close" | Out-Null
