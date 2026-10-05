# Builds the MSIX package for the Microsoft Store from the folder "package" at the root of the repository (the
# same folder the Inno Setup script uses: qournal.exe, what windeployqt put next to it, and "plugins").
#
#   packaging\windows\msix.ps1 -Version 1.0.0 -Arch x64
#
# The package is not signed. The Store signs what is submitted to it; to install the package on a machine
# directly, sign it with signtool and a certificate whose subject is the Publisher below.
param(
    [Parameter(Mandatory = $true)][string]$Version,
    [ValidateSet("x64", "arm64")][string]$Arch = "x64",
    # The identity the Microsoft Store gave the app when its name was reserved (Partner Center, "Product
    # identity"; Store ID 9MZ8JS1T98WD). The Store refuses a package with another one
    [string]$IdentityName = "vereo.Qournal",
    [string]$Publisher = "CN=9CE95BBF-9D1D-4A84-A38F-96CBD9EFE631",
    [string]$PublisherName = "vereo"
)
$ErrorActionPreference = "Stop"
$root = Resolve-Path "$PSScriptRoot\..\.."
$package = Join-Path $root "package"
if (-not (Test-Path (Join-Path $package "qournal.exe"))) {
    throw "No qournal.exe in $package"
}

# The folder is copied: the manifest and the logos must not end up in the installer of Inno Setup
$stage = Join-Path $root "package-msix"
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
Copy-Item -Recurse $package $stage
New-Item -ItemType Directory (Join-Path $stage "Assets") | Out-Null
Copy-Item "$PSScriptRoot\msix\*.png" (Join-Path $stage "Assets")

# The version of a package has four parts, and the Store wants the last one to be 0
$manifest = Get-Content "$PSScriptRoot\msix\AppxManifest.xml.in" -Raw
$manifest = $manifest.Replace("@IDENTITY_NAME@", $IdentityName).Replace("@PUBLISHER@", $Publisher)
$manifest = $manifest.Replace("@PUBLISHER_NAME@", $PublisherName).Replace("@VERSION@", "$Version.0")
$manifest = $manifest.Replace("@ARCH@", $Arch)
Set-Content -Path (Join-Path $stage "AppxManifest.xml") -Value $manifest -Encoding UTF8

# makeappx.exe comes with the Windows SDK and is not in the PATH; the x86 one runs on every machine
$makeappx = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin\*\x86\makeappx.exe" |
    Sort-Object FullName | Select-Object -Last 1
if (-not $makeappx) {
    throw "makeappx.exe not found: install the Windows SDK"
}
$output = Join-Path $root "qournal-$Version-windows-$Arch.msix"
& $makeappx.FullName pack /o /d $stage /p $output
if ($LASTEXITCODE -ne 0) {
    throw "makeappx failed"
}
Write-Host "Written: $output"
