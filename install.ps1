param(
    [switch]$Uninstall,
    [string]$TargetDir = "",
    [string]$LocalDllPath = ""
)

$ErrorActionPreference = 'Stop'
$RepoUrl = 'https://github.com/shiromenya/UTF8_TeraPad'
$DownloadUrl = "$RepoUrl/releases/latest/download/version.dll"
$ScriptUrl = 'https://raw.githubusercontent.com/shiromenya/UTF8_TeraPad/main/install.ps1'

function Find-TeraPadDir {
    $candidates = @()
    if ($PSScriptRoot) {
        $candidates += (Join-Path $PSScriptRoot 'TeraPad.exe')
    }
    $candidates += @(
        (Join-Path (Get-Location) 'TeraPad.exe'),
        "${env:ProgramFiles(x86)}\TeraPad\TeraPad.exe",
        "$env:ProgramFiles\TeraPad\TeraPad.exe",
        "$env:LOCALAPPDATA\TeraPad\TeraPad.exe"
    )
    foreach ($p in $candidates) {
        if ($p -and (Test-Path -LiteralPath $p -PathType Leaf)) {
            return (Split-Path -Parent $p)
        }
    }

    $regPaths = @(
        'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*',
        'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*',
        'HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*'
    )
    foreach ($rp in $regPaths) {
        $items = Get-ItemProperty $rp -ErrorAction SilentlyContinue | Where-Object { $_.DisplayName -like '*TeraPad*' }
        foreach ($item in $items) {
            if ($item.InstallLocation -and (Test-Path (Join-Path $item.InstallLocation 'TeraPad.exe'))) {
                return $item.InstallLocation.TrimEnd('\')
            }
            if ($item.UninstallString -match '([A-Za-z]:\\[^"]+?\\)unins') {
                $dir = $Matches[1].TrimEnd('\')
                if (Test-Path (Join-Path $dir 'TeraPad.exe')) { return $dir }
            }
        }
    }

    Add-Type -AssemblyName System.Windows.Forms
    $dlg = New-Object System.Windows.Forms.OpenFileDialog
    $dlg.Title = 'TeraPad.exe を選択してください'
    $dlg.Filter = 'TeraPad (TeraPad.exe)|TeraPad.exe|実行ファイル (*.exe)|*.exe'
    if ($dlg.ShowDialog() -eq [System.Windows.Forms.DialogResult]::OK) {
        return (Split-Path -Parent $dlg.FileName)
    }
    return $null
}

function Test-DirWritable([string]$Dir) {
    try {
        $probe = Join-Path $Dir ".write_test_$([guid]::NewGuid().ToString('N')).tmp"
        [System.IO.File]::WriteAllText($probe, '')
        Remove-Item $probe -Force
        return $true
    } catch {
        return $false
    }
}

if (-not $TargetDir) {
    $TargetDir = Find-TeraPadDir
}
if (-not $TargetDir -or -not (Test-Path (Join-Path $TargetDir 'TeraPad.exe'))) {
    Write-Error 'TeraPad.exe が見つかりませんでした。処理を中止します。'
    exit 1
}

$dstDll = Join-Path $TargetDir 'version.dll'

# Elevate if target directory requires administrator privileges
if (-not (Test-DirWritable $TargetDir)) {
    $isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
    if (-not $isAdmin) {
        Write-Host "管理者権限で再実行します: $TargetDir"
        $flags = "-TargetDir `"$TargetDir`""
        if ($Uninstall) { $flags += ' -Uninstall' }
        if ($LocalDllPath) { $flags += " -LocalDllPath `"$LocalDllPath`"" }
        if ($PSCommandPath) {
            $proc = Start-Process powershell.exe -Verb RunAs -PassThru -Wait -ArgumentList "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`" $flags"
            exit $proc.ExitCode
        } else {
            $scriptPath = Join-Path $env:TEMP "tpad_utf8_install_$([guid]::NewGuid().ToString('N')).ps1"
            [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
            Invoke-WebRequest -Uri $ScriptUrl -OutFile $scriptPath -UseBasicParsing
            try {
                $proc = Start-Process powershell.exe -Verb RunAs -PassThru -Wait -ArgumentList "-NoProfile -ExecutionPolicy Bypass -File `"$scriptPath`" $flags"
                exit $proc.ExitCode
            } finally {
                Remove-Item $scriptPath -Force -ErrorAction SilentlyContinue
            }
        }
    }
}

$running = Get-Process -Name 'TeraPad' -ErrorAction SilentlyContinue
if ($running) {
    Write-Host '起動中の TeraPad を終了してから続行してください...'
    $running | Wait-Process -Timeout 30 -ErrorAction SilentlyContinue
    if (Get-Process -Name 'TeraPad' -ErrorAction SilentlyContinue) {
        Write-Error 'TeraPad が起動中のため処理を中止しました。TeraPad を閉じてから再実行してください。'
        exit 1
    }
}

if ($Uninstall) {
    if (Test-Path $dstDll) {
        Remove-Item $dstDll -Force
        Write-Host "削除しました: $dstDll"
    } else {
        Write-Host "version.dll は既に存在しません: $TargetDir"
    }
    exit 0
}

$srcDll = $LocalDllPath
$tempDll = $null
if (-not $srcDll -or -not (Test-Path $srcDll)) {
    $bundled = if ($PSScriptRoot) { Join-Path $PSScriptRoot 'version.dll' } else { '' }
    if ($bundled -and (Test-Path $bundled)) {
        $srcDll = $bundled
    } else {
        $tempDll = Join-Path $env:TEMP "tpad_utf8_version_$([guid]::NewGuid().ToString('N')).dll"
        Write-Host "最新版の version.dll をダウンロードしています..."
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        Invoke-WebRequest -Uri $DownloadUrl -OutFile $tempDll -UseBasicParsing
        $srcDll = $tempDll
    }
}

Copy-Item -LiteralPath $srcDll -Destination $dstDll -Force
if ($tempDll) { Remove-Item $tempDll -Force -ErrorAction SilentlyContinue }

Write-Host "導入が完了しました: $dstDll"
