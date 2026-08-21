<#
.SYNOPSIS
  KM Virtual Camera Installer Script (Elevated PowerShell)
.DESCRIPTION
  Installs Visual C++ Redistributable (if missing) and registers the
  KM Virtual Camera Media Source COM DLL and Windows System Virtual Camera.
#>

[CmdletBinding()]
param(
    [string]$DllPath = ""
)

$ErrorActionPreference = "Continue"

# Configure console for clean UTF-8 text display
try {
    [Console]::OutputEncoding = [System.Text.Encoding]::UTF8
    $OutputEncoding = [System.Text.Encoding]::UTF8
} catch {}

# Self-elevation check if run directly without install.bat
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    $scriptPath = if ($PSCommandPath) { (Resolve-Path $PSCommandPath).Path } else { Join-Path $PSScriptRoot "install.ps1" }
    $argList = "-ExecutionPolicy Bypass -NoProfile -File `"$scriptPath`""
    if ($DllPath) {
        $argList += " -DllPath `"$DllPath`""
    }
    try {
        Start-Process powershell.exe -ArgumentList $argList -Verb RunAs -Wait
        exit
    } catch {
        Write-Warning "管理者権限への昇格が拒否されました。管理者として実行してください。"
    }
}

Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host "  KM Virtual Camera for Windows Installer" -ForegroundColor Cyan
Write-Host "==========================================================" -ForegroundColor Cyan

# 1. Check & Install Visual C++ 2015-2022 Redistributable (x64)
Write-Host "`n[1/3] Visual C++ ランタイムを確認中 (Checking VC++ Runtime)..." -ForegroundColor Yellow
$vcInstalled = $false
$vcKeys = @(
    "HKLM:\SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\X64",
    "HKLM:\SOFTWARE\WOW6432Node\Microsoft\VisualStudio\14.0\VC\Runtimes\X64"
)
foreach ($k in $vcKeys) {
    if (Test-Path $k) {
        $inst = (Get-ItemProperty -Path $k -ErrorAction SilentlyContinue).Installed
        if ($inst -eq 1) {
            $vcInstalled = $true
            break
        }
    }
}

if (-not $vcInstalled) {
    $redistCandidates = @(
        (Join-Path $PSScriptRoot "..\vc_redist\vc_redist.x64.exe"),
        (Join-Path $PSScriptRoot "vc_redist\vc_redist.x64.exe"),
        (Join-Path $PWD "vc_redist\vc_redist.x64.exe")
    )
    $redistExe = $null
    foreach ($c in $redistCandidates) {
        if ($c -and (Test-Path $c)) {
            $redistExe = (Resolve-Path $c).Path
            break
        }
    }

    if ($redistExe) {
        Write-Host "  Visual C++ 再頒布可能パッケージをインストール中..." -ForegroundColor Cyan
        try {
            $proc = Start-Process -FilePath $redistExe -ArgumentList "/install /quiet /norestart" -Wait -PassThru
            if ($proc.ExitCode -eq 0 -or $proc.ExitCode -eq 3010) {
                Write-Host "  [OK] Visual C++ ランタイムのインストールが完了しました。" -ForegroundColor Green
            } else {
                Write-Warning "  Visual C++ ランタイムの終了コード: $($proc.ExitCode)"
            }
        } catch {
            Write-Warning "  Visual C++ ランタイムのインストールに失敗しました: $_"
        }
    } else {
        Write-Warning "  vc_redist.x64.exe が見つかりませんでした。"
        Write-Warning "  必要に応じて https://aka.ms/vs/17/release/vc_redist.x64.exe からインストールしてください。"
    }
} else {
    Write-Host "  [OK] Visual C++ ランタイムは既にインストールされています。" -ForegroundColor Green
}

# 2. Virtual Camera DLL Deployment & Registration
Write-Host "`n[2/3] 仮想カメラを登録中 (Registering Virtual Camera)..." -ForegroundColor Yellow

$regScriptCandidates = @(
    (Join-Path $PSScriptRoot "register_vcam.ps1"),
    (Join-Path $PSScriptRoot "..\scripts\register_vcam.ps1"),
    (Join-Path $PWD "scripts\register_vcam.ps1"),
    (Join-Path $PWD "register_vcam.ps1")
)
$regScript = $null
foreach ($s in $regScriptCandidates) {
    if ($s -and (Test-Path $s)) {
        $regScript = (Resolve-Path $s).Path
        break
    }
}

if ($regScript) {
    $resolvedDll = $DllPath
    if (-not $resolvedDll -or -not (Test-Path $resolvedDll)) {
        $dllCandidates = @(
            (Join-Path $PSScriptRoot "..\VirtualCameraMediaSource.dll"),
            (Join-Path $PSScriptRoot "VirtualCameraMediaSource.dll"),
            (Join-Path $PSScriptRoot "..\windows\build\Release\VirtualCameraMediaSource.dll"),
            (Join-Path $PWD "VirtualCameraMediaSource.dll"),
            (Join-Path $PWD "windows\build\Release\VirtualCameraMediaSource.dll")
        )
        foreach ($d in $dllCandidates) {
            if ($d -and (Test-Path $d)) {
                $resolvedDll = (Resolve-Path $d).Path
                break
            }
        }
    }

    Write-Host "  Executing registration script: $regScript" -ForegroundColor Gray
    & $regScript -DllPath $resolvedDll
} else {
    Write-Error "register_vcam.ps1 が見つかりませんでした。"
}

# 3. Installation Summary
Write-Host "`n[3/3] インストール完了 (Installation Completed!)" -ForegroundColor Green
Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host "  使用手順 (Usage):" -ForegroundColor White
Write-Host "  1. Receiver.exe をダブルクリックして起動します。" -ForegroundColor White
Write-Host "  2. 画面に表示される QR コードをスマホで読み取ります。" -ForegroundColor White
Write-Host "  3. Zoom / Teams / Web会議 等で「WebRTC Bridge Virtual Camera」を選択します。" -ForegroundColor White
Write-Host "==========================================================" -ForegroundColor Cyan

if ([Environment]::UserInteractive -and -not [Console]::IsInputRedirected) {
    Write-Host "`n続行するには何かキーを押してください..." -ForegroundColor Gray
    try {
        [void][System.Console]::ReadKey($true)
    } catch {}
}
