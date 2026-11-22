$ErrorActionPreference = 'Stop'

$root   = 'D:\libs\workdesk-qt'
$qt     = 'C:/Qt/6.12.0/msvc2022_64'
$cmake  = 'C:\Qt\Tools\CMake_64\bin\cmake.exe'
$ninja  = 'C:\Qt\Tools\Ninja\ninja.exe'
$deploy = 'C:/Qt/6.12.0/msvc2022_64/bin/windeployqt.exe'
$appDir = 'D:\WorkDeskQt'

if (-not (Test-Path $cmake)) { Write-Host "[FAIL] cmake not found: $cmake" -ForegroundColor Red; exit 1 }

# 定位 VS 的 vcvars64.bat
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { Write-Host "[FAIL] Visual Studio with C++ tools not found" -ForegroundColor Red; exit 1 }
$vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path $vcvars)) { Write-Host "[FAIL] vcvars64.bat not found: $vcvars" -ForegroundColor Red; exit 1 }
Write-Host "VS: $vsPath" -ForegroundColor Cyan

$steps = @(
    "call `"$vcvars`" >nul",
    "`"$cmake`" -S `"$root`" -B `"$root\build`" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=`"$qt`" -DCMAKE_MAKE_PROGRAM=`"$ninja`"",
    "`"$cmake`" --build `"$root\build`""
)
$cmdline = $steps -join ' && '

Write-Host "building..." -ForegroundColor Cyan
cmd /c $cmdline 2>&1 | ForEach-Object { Write-Host $_ }
if ($LASTEXITCODE -ne 0) { Write-Host "[FAILED] exit=$LASTEXITCODE" -ForegroundColor Red; exit 1 }

$exe = Join-Path $root 'build\WorkDeskQt.exe'
if (-not (Test-Path $exe)) { Write-Host "[FAIL] exe not produced" -ForegroundColor Red; exit 1 }

# 部署目录：D:\WorkDeskQt（不覆盖旧的 ImGui 版 D:\WorkDesk\WorkDesk.exe）
# 注意：windeployqt 不做增量覆盖，历史残留/半截 DLL 会让程序在 main 之前就 0xc0000005 崩溃。
# 因此每次先整体清空旧目录，再全量部署到一个干净目录。
# 以前是改名成 .bak_时间戳，跑几次就攒一堆 55MB 的垃圾目录，现在直接删，不再留备份。
if (Test-Path $appDir) {
    Remove-Item -Path $appDir -Recurse -Force -ErrorAction Stop
    Write-Host "[INFO] 旧部署目录已清空" -ForegroundColor Yellow
}
New-Item -ItemType Directory -Force -Path $appDir | Out-Null
$appExe = Join-Path $appDir 'WorkDeskQt.exe'
Copy-Item $exe $appExe -Force

# 卸载程序也一起部署：它得和主程序待在同一个目录，才知道该删哪儿
$uexe = Join-Path $root 'build\Uninstall.exe'
if (-not (Test-Path $uexe)) { Write-Host "[FAIL] Uninstall.exe not produced" -ForegroundColor Red; exit 1 }
Copy-Item $uexe (Join-Path $appDir 'Uninstall.exe') -Force
# 必须用 cmd 重定向调用：在 PowerShell 管道里跑 windeployqt 会中途被打断，
# 写出大小正确但内容损坏（全 0 / 半截）的 DLL，程序启动就报 0xc000012f「损坏的映像」。
$dlog = Join-Path $root 'deploy.log'
cmd /c "`"$deploy`" --release --no-translations `"$appExe`" > `"$dlog`" 2>&1"

# 部署后逐个和 Qt 源文件比对哈希，损坏的用源文件覆盖（自愈，防止再踩同一个坑）
$bad = 0
Get-ChildItem $appDir -Recurse -Filter *.dll | ForEach-Object {
    $rel = $_.FullName.Substring($appDir.Length + 1)
    $cands = @()
    if ($rel -match '\\') { $cands += (Join-Path "$qt\plugins" $rel) }
    else {
        $cands += (Join-Path "$qt\bin" $rel)
        $cands += (Join-Path "$env:SystemRoot\System32" $rel)
    }
    $src = $cands | Where-Object { Test-Path $_ } | Select-Object -First 1
    if ($src -and ((Get-FileHash $src -Algorithm MD5).Hash -ne (Get-FileHash $_.FullName -Algorithm MD5).Hash)) {
        Copy-Item $src $_.FullName -Force
        $bad++
        Write-Host "[FIX] 修复损坏 DLL: $rel" -ForegroundColor Yellow
    }
}
if ($bad -gt 0) { Write-Host "[WARN] 部署残缺已修复 $bad 个 DLL" -ForegroundColor Yellow }

Write-Host "[OK] $appDir\WorkDeskQt.exe" -ForegroundColor Green
