# WorkDesk Qt 构建脚本
# 依赖：Qt 6（Widgets）、CMake 3.21+、Ninja、Visual Studio C++ 工具链
# 用法：powershell -ExecutionPolicy Bypass -File .\build.ps1 [-QtDir C:/Qt/6.12.0/msvc2022_64] [-DeployDir D:\WorkDeskQt]

param(
    [string]$QtDir = $env:WD_QT_DIR,
    [string]$DeployDir = "",
    [switch]$SkipDeploy
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot

if (-not $QtDir) {
    $candidates = @('C:/Qt/6.12.0/msvc2022_64', 'C:/Qt/6.9.0/msvc2022_64', 'C:/Qt/6.8.0/msvc2022_64')
    foreach ($c in $candidates) { if (Test-Path (Join-Path $c 'bin/windeployqt.exe')) { $QtDir = $c; break } }
}
if (-not $QtDir -or -not (Test-Path (Join-Path $QtDir 'bin/windeployqt.exe'))) {
    Write-Host "[FAIL] 未找到 Qt6 目录，请用 -QtDir 指定，例如 -QtDir C:/Qt/6.12.0/msvc2022_64" -ForegroundColor Red
    exit 1
}
Write-Host "Qt: $QtDir" -ForegroundColor Cyan

$cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if (-not $cmake) {
    $c = Join-Path (Split-Path (Split-Path $QtDir)) 'Tools/CMake_64/bin/cmake.exe'
    if (Test-Path $c) { $cmake = $c }
}
if (-not $cmake) { Write-Host "[FAIL] 未找到 cmake，请安装 CMake 3.21+ 或加入 PATH" -ForegroundColor Red; exit 1 }

$ninja = (Get-Command ninja -ErrorAction SilentlyContinue).Source
if (-not $ninja) {
    $n = Join-Path (Split-Path (Split-Path $QtDir)) 'Tools/Ninja/ninja.exe'
    if (Test-Path $n) { $ninja = $n }
}
if (-not $ninja) { Write-Host "[FAIL] 未找到 ninja，请安装 Ninja 或加入 PATH" -ForegroundColor Red; exit 1 }

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { Write-Host "[FAIL] 未找到 vswhere，请安装 Visual Studio（含 C++ 桌面开发）" -ForegroundColor Red; exit 1 }
$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { Write-Host "[FAIL] 未找到带 C++ 工具的 Visual Studio" -ForegroundColor Red; exit 1 }
$vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path $vcvars)) { Write-Host "[FAIL] 未找到 vcvars64.bat: $vcvars" -ForegroundColor Red; exit 1 }
Write-Host "VS: $vsPath" -ForegroundColor Cyan

$buildDir = Join-Path $root 'build'
$steps = @(
    "call `"$vcvars`" >nul",
    "`"$cmake`" -S `"$root`" -B `"$buildDir`" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=`"$QtDir`" -DCMAKE_MAKE_PROGRAM=`"$ninja`"",
    "`"$cmake`" --build `"$buildDir`""
)
$cmdline = $steps -join ' && '
cmd /c $cmdline
if ($LASTEXITCODE -ne 0) { Write-Host "[FAIL] 编译失败 (exit $LASTEXITCODE)" -ForegroundColor Red; exit $LASTEXITCODE }

$exe = Join-Path $buildDir 'WorkDeskQt.exe'
if (-not (Test-Path $exe)) { Write-Host "[FAIL] 未生成 WorkDeskQt.exe" -ForegroundColor Red; exit 1 }
Write-Host "[OK] 编译完成: $exe" -ForegroundColor Green

if ($SkipDeploy -or -not $DeployDir) {
    Write-Host "跳过部署（加 -DeployDir 可自动 windeployqt 并复制运行库）" -ForegroundColor Yellow
    exit 0
}

New-Item -ItemType Directory -Path $DeployDir -Force | Out-Null
Copy-Item $exe (Join-Path $DeployDir 'WorkDeskQt.exe') -Force
$windeploy = Join-Path $QtDir 'bin/windeployqt.exe'
& $windeploy --release --no-translations --no-system-d3d-compiler --no-opengl-sw (Join-Path $DeployDir 'WorkDeskQt.exe')
Write-Host "[OK] 已部署到: $DeployDir" -ForegroundColor Green
