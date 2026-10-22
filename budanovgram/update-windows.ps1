param(
    [Parameter(Mandatory)][ValidateSet('Check','Download','Install')][string]$Mode,
    [string]$CurrentVersion = '0.1.1',
    [string]$Version,
    [string]$InstallDirectory,
    [int]$ClientProcessId
)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
[Console]::OutputEncoding = New-Object Text.UTF8Encoding($false)
$work = [IO.Path]::GetFullPath($PSScriptRoot)
$headers = @{ 'User-Agent' = 'BudanovGram-Updater'; Accept = 'application/vnd.github+json' }
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

function Get-Release([string]$RequestedVersion) {
    $endpoint = 'latest'
    if ($RequestedVersion) {
        if ($RequestedVersion -notmatch '^\d+\.\d+\.\d+$') { throw 'Invalid version.' }
        $endpoint = "tags/v$RequestedVersion"
    }
    $release = Invoke-RestMethod -Uri "https://api.github.com/repos/Mengy777/BudanovGram/releases/$endpoint" -Headers $headers -TimeoutSec 30
    if ($release.draft -or $release.prerelease -or $release.tag_name -notmatch '^v(\d+\.\d+\.\d+)$') { throw 'Invalid release.' }
    $number = $Matches[1]
    $name = "BudanovGram-$number-Windows-x64-portable.zip"
    $archives = @($release.assets | Where-Object { $_.name -ceq $name -and $_.state -eq 'uploaded' })
    $checksums = @($release.assets | Where-Object { $_.name -ceq 'SHA256SUMS.txt' -and $_.state -eq 'uploaded' })
    if ($archives.Count -ne 1 -or $checksums.Count -ne 1) { throw 'Required release files are missing.' }
    if ($archives[0].size -le 0 -or $archives[0].size -gt 1GB -or $checksums[0].size -gt 64KB) { throw 'Invalid asset size.' }
    foreach ($asset in @($archives[0], $checksums[0])) {
        $expected = "https://github.com/Mengy777/BudanovGram/releases/download/v$number/$($asset.name)"
        if ($asset.browser_download_url -cne $expected) { throw 'Unexpected release URL.' }
    }
    return @{ version = $number; archive = $archives[0]; checksum = $checksums[0] }
}

function Get-PackageFiles {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [IO.Compression.ZipFile]::OpenRead((Join-Path $work 'package.zip'))
    try {
        $allowed = @('BudanovGram.exe','LICENSE.txt','LEGAL.txt','README.txt','release.json')
        $seen = @{}
        foreach ($entry in $zip.Entries) {
            if ($entry.FullName -ceq 'BudanovGram/TelegramForcePortable/' -and $entry.Length -eq 0) { continue }
            if (!$entry.FullName.StartsWith('BudanovGram/', [StringComparison]::Ordinal)) { throw 'Unexpected archive path.' }
            $name = $entry.FullName.Substring(12)
            if ($allowed -cnotcontains $name -or $seen.ContainsKey($name)) { throw 'Unexpected or duplicate archive entry.' }
            if ($entry.Length -gt 1GB -or (($entry.ExternalAttributes -shr 16) -band 0xF000) -eq 0xA000) { throw 'Invalid archive entry.' }
            $seen[$name] = $true
        }
        if (!$seen.ContainsKey('BudanovGram.exe') -or !$seen.ContainsKey('release.json')) { throw 'Incomplete package.' }
        return @($seen.Keys)
    } finally { $zip.Dispose() }
}

function Install-Package {
    $destination = [IO.Path]::GetFullPath($InstallDirectory).TrimEnd('\')
    if ([IO.Path]::GetDirectoryName($work) -ine $destination -or [IO.Path]::GetFileName($work) -notmatch '^\.budanovgram-update-[A-Za-z0-9]+$') { throw 'Invalid update directory.' }
    $verified = Get-Content -LiteralPath (Join-Path $work 'verified.json') -Raw | ConvertFrom-Json
    if ($verified.version -cne $Version -or (Get-FileHash -LiteralPath (Join-Path $work 'package.zip') -Algorithm SHA256).Hash -ine $verified.hash) { throw 'Package verification changed.' }
    $files = @(Get-PackageFiles)
    $client = Get-Process -Id $ClientProcessId -ErrorAction SilentlyContinue
    if ($client) {
        if ($client.Path -ine (Join-Path $destination 'BudanovGram.exe')) { throw 'Unexpected client process.' }
        Wait-Process -Id $ClientProcessId -Timeout 120 -ErrorAction Stop
    }
    if (@(Get-Process -Name BudanovGram -ErrorAction SilentlyContinue | Where-Object { $_.Path -ieq (Join-Path $destination 'BudanovGram.exe') }).Count) { throw 'BudanovGram is still running.' }
    $stage = Join-Path $work 'install-stage'
    [IO.Compression.ZipFile]::ExtractToDirectory((Join-Path $work 'package.zip'), $stage)
    $backup = Join-Path $destination ('.budanovgram-backup-' + [guid]::NewGuid().ToString('N'))
    [IO.Directory]::CreateDirectory($backup) | Out-Null
    $written = @()
    try {
        foreach ($name in ($files | Sort-Object { $_ -ceq 'BudanovGram.exe' })) {
            $target = Join-Path $destination $name
            $saved = Join-Path $backup $name
            if (Test-Path -LiteralPath $target) { Copy-Item -LiteralPath $target -Destination $saved }
            $written += $name
            Copy-Item -LiteralPath (Join-Path $stage "BudanovGram/$name") -Destination $target -Force
        }
        Start-Process -FilePath (Join-Path $destination 'BudanovGram.exe') -WorkingDirectory $destination
    } catch {
        foreach ($name in $written) {
            $saved = Join-Path $backup $name
            $target = Join-Path $destination $name
            if (Test-Path -LiteralPath $saved) { Copy-Item -LiteralPath $saved -Destination $target -Force }
            elseif (Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target }
        }
        throw
    }
    Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
}

try {
    if ($Mode -eq 'Check') {
        $release = Get-Release ''
        @{ version = $release.version; available = ([version]$release.version -gt [version]$CurrentVersion); size = $release.archive.size } | ConvertTo-Json -Compress
        exit 0
    }
    if ($Mode -eq 'Download') {
        $release = Get-Release $Version
        if ($release.version -cne $Version -or [version]$Version -le [version]$CurrentVersion) { throw 'Version changed or is not newer.' }
        Invoke-WebRequest -UseBasicParsing -Uri $release.checksum.browser_download_url -OutFile (Join-Path $work 'SHA256SUMS.txt') -TimeoutSec 60
        $pattern = '^([a-fA-F0-9]{64})  ' + [regex]::Escape($release.archive.name) + '$'
        $lines = @(Get-Content -LiteralPath (Join-Path $work 'SHA256SUMS.txt') | Where-Object { $_ -cmatch $pattern })
        if ($lines.Count -ne 1 -or $lines[0] -cnotmatch $pattern) { throw 'Invalid checksum file.' }
        $expectedHash = $Matches[1]
        Invoke-WebRequest -UseBasicParsing -Uri $release.archive.browser_download_url -OutFile (Join-Path $work 'package.zip') -TimeoutSec 900
        if ((Get-Item -LiteralPath (Join-Path $work 'package.zip')).Length -ne $release.archive.size) { throw 'Incomplete download.' }
        if ((Get-FileHash -LiteralPath (Join-Path $work 'package.zip') -Algorithm SHA256).Hash -ine $expectedHash) { throw 'Checksum mismatch.' }
        $files = @(Get-PackageFiles)
        $stage = Join-Path $work 'stage'
        [IO.Directory]::CreateDirectory($stage) | Out-Null
        [IO.Compression.ZipFile]::ExtractToDirectory((Join-Path $work 'package.zip'), $stage)
        $binary = Get-Item -LiteralPath (Join-Path $stage 'BudanovGram/BudanovGram.exe')
        $metadata = Get-Content -LiteralPath (Join-Path $stage 'BudanovGram/release.json') -Raw | ConvertFrom-Json
        if ($binary.VersionInfo.ProductName -cne 'BudanovGram' -or $metadata.version -cne $Version -or $binary.VersionInfo.ProductVersion -cne "$Version.0") { throw 'Package version mismatch.' }
        @{ version = $Version; hash = $expectedHash; files = $files } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $work 'verified.json') -Encoding UTF8
        Write-Output 'READY'
        exit 0
    }
    Install-Package
} catch {
    if ($Mode -eq 'Install') {
        Add-Type -AssemblyName System.Windows.Forms
        [Windows.Forms.MessageBox]::Show(('Update failed. Previous files were retained or restored. ' + $_.Exception.Message), 'BudanovGram') | Out-Null
    }
    [Console]::Error.WriteLine($_.Exception.Message)
    exit 1
}
