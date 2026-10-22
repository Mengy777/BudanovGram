$ErrorActionPreference = 'Stop'
$source = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\update-windows.ps1'))
$errors = $null
$tokens = $null
$ast = [Management.Automation.Language.Parser]::ParseFile($source, [ref]$tokens, [ref]$errors)
if ($errors.Count) { throw 'Updater syntax error.' }
$functions = $ast.FindAll({ param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] }, $false)
foreach ($definition in $functions) { . ([scriptblock]::Create($definition.Extent.Text)) }
Add-Type -AssemblyName System.IO.Compression.FileSystem
$testRoot = Join-Path ([IO.Path]::GetTempPath()) ('BudanovGram-updater-tests-' + [guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($testRoot) | Out-Null
$Version = '0.1.2'
$ClientProcessId = 99999999

function New-Case([string]$Name, [string[]]$Entries) {
    $script:InstallDirectory = Join-Path $testRoot $Name
    $script:work = Join-Path $InstallDirectory '.budanovgram-update-test'
    [IO.Directory]::CreateDirectory($work) | Out-Null
    [IO.Directory]::CreateDirectory((Join-Path $InstallDirectory 'TelegramForcePortable/tdata')) | Out-Null
    [IO.File]::WriteAllText((Join-Path $InstallDirectory 'TelegramForcePortable/tdata/sentinel'), 'account-data')
    [IO.File]::WriteAllText((Join-Path $InstallDirectory 'build-secrets.json'), 'private-file')
    [IO.File]::WriteAllText((Join-Path $InstallDirectory 'BudanovGram.exe'), 'old-binary')
    $zip = [IO.Compression.ZipFile]::Open((Join-Path $work 'package.zip'), 'Create')
    foreach ($name in $Entries) {
        $entry = $zip.CreateEntry($name)
        $stream = New-Object IO.StreamWriter($entry.Open())
        if (!$name.EndsWith('/')) { $stream.Write('new-binary') }
        $stream.Dispose()
    }
    $zip.Dispose()
    @{version=$Version;hash=(Get-FileHash -LiteralPath (Join-Path $work 'package.zip')).Hash} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $work 'verified.json') -Encoding UTF8
}

function Assert-Accounts {
    if ([IO.File]::ReadAllText((Join-Path $InstallDirectory 'TelegramForcePortable/tdata/sentinel')) -cne 'account-data') { throw 'Account data changed.' }
    if ([IO.File]::ReadAllText((Join-Path $InstallDirectory 'build-secrets.json')) -cne 'private-file') { throw 'Credentials changed.' }
}

$safe = @('BudanovGram/BudanovGram.exe','BudanovGram/release.json','BudanovGram/TelegramForcePortable/')
# Mock only process launch; archive validation and file replacement run unchanged.
function Start-Process { param($FilePath,$WorkingDirectory) if ($script:FailStart) { throw 'Simulated launch failure.' }; $script:Started = $FilePath }
$script:FailStart = $false
New-Case 'success' $safe
Install-Package
if ([IO.File]::ReadAllText((Join-Path $InstallDirectory 'BudanovGram.exe')) -cne 'new-binary') { throw 'Replacement failed.' }
if (!(Get-ChildItem -LiteralPath $InstallDirectory -Directory -Filter '.budanovgram-backup-*')) { throw 'Backup missing.' }
Assert-Accounts
Write-Output 'PASS: install, backup, restart request, account and credential preservation.'

New-Case 'rollback' $safe
$script:FailStart = $true
$rejected = $false
try { Install-Package } catch { $rejected = $true }
if (!$rejected -or [IO.File]::ReadAllText((Join-Path $InstallDirectory 'BudanovGram.exe')) -cne 'old-binary') { throw 'Rollback failed.' }
Assert-Accounts
Write-Output 'PASS: failed restart rolls back app files and preserves accounts.'

New-Case 'corrupt' $safe
[IO.File]::AppendAllText((Join-Path $work 'package.zip'), 'corruption')
$rejected = $false
try { Install-Package } catch { $rejected = $true }
if (!$rejected -or [IO.File]::ReadAllText((Join-Path $InstallDirectory 'BudanovGram.exe')) -cne 'old-binary') { throw 'Corrupt archive accepted.' }
Assert-Accounts
Write-Output 'PASS: changed checksum rejects installation before replacement.'

foreach ($bad in @('BudanovGram/../escape.exe','BudanovGram/TelegramForcePortable/tdata/key','BudanovGram/build-secrets.json','BudanovGram/unexpected.dll')) {
    New-Case ([guid]::NewGuid().ToString('N')) ($safe + $bad)
    $rejected = $false
    try { Get-PackageFiles | Out-Null } catch { $rejected = $true }
    if (!$rejected) { throw ('Unsafe archive accepted: ' + $bad) }
    Assert-Accounts
}
Write-Output 'PASS: traversal, account data, credentials and unknown files rejected.'
Write-Output ('Test artifacts: ' + $testRoot)
