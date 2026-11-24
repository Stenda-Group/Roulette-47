# Télécharge le SDK WebView2 de Microsoft (en-têtes et WebView2LoaderStatic.lib)
# dans sdk\webview2, depuis NuGet. C'est la seule dépendance que CMake ne
# récupère pas tout seul à la compilation.
#
#   powershell -File tools\recuperer_webview2.ps1
#   powershell -File tools\recuperer_webview2.ps1 -Version 1.0.2903.40
#
# Downloads the Microsoft WebView2 SDK into sdk\webview2 (the only dependency
# CMake does not fetch by itself).

param(
    [string]$Version = "1.0.2903.40"
)

$ErrorActionPreference = "Stop"
$racine = Split-Path -Parent $PSScriptRoot
$dossier = Join-Path $racine "sdk"
New-Item -ItemType Directory -Force $dossier | Out-Null

Write-Host "WebView2 SDK v$Version..."
$paquet = Join-Path $dossier "webview2.nupkg"
Invoke-WebRequest "https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/$Version" -OutFile $paquet
$wv2 = Join-Path $dossier "webview2"
if (Test-Path $wv2) { Remove-Item -Recurse -Force $wv2 }
$temp = Join-Path $dossier "webview2-tmp"
if (Test-Path $temp) { Remove-Item -Recurse -Force $temp }
Copy-Item $paquet "$paquet.zip"
Expand-Archive "$paquet.zip" -DestinationPath $temp
Remove-Item "$paquet.zip"
New-Item -ItemType Directory -Force (Join-Path $wv2 "build") | Out-Null
Copy-Item -Recurse (Join-Path $temp "build\native") (Join-Path $wv2 "build\native")
Copy-Item (Join-Path $temp "LICENSE.txt") $wv2
Remove-Item -Recurse -Force $temp

Write-Host "SDK WebView2 installé dans $wv2. Compiler avec tools\compiler.cmd."
