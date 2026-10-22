param(
    [string]$Source = (Join-Path $PSScriptRoot '..\out\Debug'),
    [string]$Destination = 'D:\Programs\BudanovGram'
)
$ErrorActionPreference = 'Stop'
$Source = [IO.Path]::GetFullPath($Source)
$Destination = [IO.Path]::GetFullPath($Destination)
$exe = Join-Path $Source 'BudanovGram.exe'
if (!(Test-Path -LiteralPath $exe -PathType Leaf)) { throw "Build is missing: $exe" }
$targetExe = Join-Path $Destination 'BudanovGram.exe'
if ((Get-Item -LiteralPath $exe).VersionInfo.ProductName -ne 'BudanovGram') {
    throw 'The executable is not a BudanovGram build.'
}
$running = Get-Process -Name BudanovGram -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $targetExe }
if ($running) { throw 'Close BudanovGram before installing.' }
if (Test-Path -LiteralPath $targetExe) { throw 'An installation already exists. Automatic replacement is not configured yet.' }
New-Item -ItemType Directory -Force -Path $Destination | Out-Null
Copy-Item -LiteralPath $exe -Destination $targetExe
Get-ChildItem -LiteralPath $Source -Filter '*.dll' -File | Copy-Item -Destination $Destination
foreach ($name in @('platforms', 'styles', 'imageformats', 'tls', 'translations')) {
    $plugin = Join-Path $Source $name
    if (Test-Path -LiteralPath $plugin -PathType Container) {
        Copy-Item -LiteralPath $plugin -Destination $Destination -Recurse
    }
}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot '..\LICENSE') -Destination (Join-Path $Destination 'LICENSE.txt')
Copy-Item -LiteralPath (Join-Path $PSScriptRoot '..\LEGAL') -Destination (Join-Path $Destination 'LEGAL.txt')
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'release.json') -Destination $Destination
New-Item -ItemType Directory -Force -Path (Join-Path $Destination 'TelegramForcePortable') | Out-Null
$desktopPath = [Environment]::GetFolderPath('Desktop')
$shellObject = New-Object -ComObject WScript.Shell
$shortcut = $shellObject.CreateShortcut((Join-Path $desktopPath 'BudanovGram.lnk'))
$shortcut.TargetPath = $targetExe
$shortcut.WorkingDirectory = $Destination
$shortcut.IconLocation = "$targetExe,0"
$shortcut.Save()
Write-Host "Installed: $targetExe"
