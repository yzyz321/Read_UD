# ============================================================
# run_tests.ps1 - UdiskTools 一键构建 + 测试（无交互、30s 超时守护）
#
# 用法（在任意目录，通常在 repo 根）：
#   pwsh -NoProfile -File tests/run_tests.ps1              # 默认 -CC gcc
#   pwsh -NoProfile -File tests/run_tests.ps1 -CC clang
#   pwsh -NoProfile -File tests/run_tests.ps1 -Filter pattern
#
# 参数：
#   -CC     gcc | clang（默认 gcc）
#   -Filter 只运行名称包含该子串的测试（大小写不敏感；含 cli 时跑 CLI 用例）
#
# 产物：tests/build/（gitignored）——测试 exe / 主程序 / 每个测试的 stdout/stderr
# 退出码：0=全部通过；1=有构建/测试失败或超时
# ============================================================
[CmdletBinding()]
param(
    [ValidateSet('gcc','clang')]
    [string]$CC = 'gcc',
    [string]$Filter = ''
)

$ErrorActionPreference = 'Continue'

$RepoRoot = Split-Path -Parent $PSScriptRoot
$BuildDir = Join-Path $PSScriptRoot 'build'
Set-Location $RepoRoot

$CommonFlags = @(
    '-std=c11','-Wall','-Wextra','-Werror',
    '-DUNICODE','-D_UNICODE','-D_WIN32_WINNT=0x0605',
    '-Iui','-Isrc'
)
$SrcDir = Join-Path $RepoRoot 'src'
$UiDir  = Join-Path $RepoRoot 'ui'

function Find-Tool([string]$name) {
    $c = Get-Command $name -ErrorAction SilentlyContinue
    if (-not $c) { return $null }
    return $c.Source
}

$CCExe = Find-Tool $CC
if (-not $CCExe) {
    Write-Host "[FATAL] 找不到编译器 -CC $CC（需要 gcc 或 clang 在 PATH 中）" -ForegroundColor Red
    exit 1
}
$WindRes = Find-Tool 'windres'

Write-Host "============================================================"
Write-Host "UdiskTools tests: CC=$CC ($CCExe)"
Write-Host "repo = $RepoRoot"
Write-Host "build= $BuildDir"
Write-Host "============================================================"
& $CCExe --version 2>&1 | Select-Object -First 1 | ForEach-Object { Write-Host "compiler: $_" }

if (Test-Path $BuildDir) { Remove-Item $BuildDir -Recurse -Force }
New-Item -ItemType Directory -Force $BuildDir | Out-Null

$script:Results = New-Object System.Collections.ArrayList

function Add-Result([string]$Name, [string]$Status, [double]$Ms, [string]$Detail) {
    [void]$script:Results.Add([PSCustomObject]@{
        Name = $Name; Status = $Status; Ms = [math]::Round($Ms, 0); Detail = $Detail
    })
}

function Show-Tail([string]$File, [int]$Lines = 30) {
    if (-not (Test-Path $File)) { return }
    $text = Get-Content -Path $File -Raw -ErrorAction SilentlyContinue
    if (-not $text) { return }
    $arr = $text -split "`r?`n"
    if ($arr.Count -gt $Lines) { $arr = $arr[($arr.Count - $Lines)..($arr.Count - 1)] }
    Write-Host "  --- 输出尾部 ($File) ---" -ForegroundColor DarkYellow
    $arr | ForEach-Object { Write-Host "  | $_" }
}

function Quote-Args([string[]]$Arguments) {
    $list = @()
    foreach ($a in $Arguments) {
        if ($null -eq $a) { continue }
        if ($a -match '[\s"]') {
            $list += '"' + ($a -replace '"', '\"') + '"'
        } else {
            $list += $a
        }
    }
    return ($list -join ' ')
}

# 运行进程：返回 @{ ExitCode; TimedOut; Stdout; Stderr }
function Invoke-Process([string]$Exe, [string[]]$Arguments, [string]$Tag, [int]$TimeoutMs = 30000) {
    $stdout = Join-Path $BuildDir ("$Tag.out.txt")
    $stderr = Join-Path $BuildDir ("$Tag.err.txt")
    if (Test-Path $stdout) { Remove-Item $stdout -Force }
    if (Test-Path $stderr) { Remove-Item $stderr -Force }

    $argLine = Quote-Args $Arguments
    if ($argLine) {
        Write-Host "  > `"$Exe`" $argLine (cwd=$RepoRoot, timeout=${TimeoutMs}ms)"
    } else {
        Write-Host "  > `"$Exe`" (cwd=$RepoRoot, timeout=${TimeoutMs}ms)"
    }

    $p = $null
    try {
        $p = Start-Process -FilePath $Exe -ArgumentList $argLine -WorkingDirectory $RepoRoot `
             -PassThru -NoNewWindow -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    } catch {
        return [PSCustomObject]@{ ExitCode = -1; TimedOut = $false; Stdout = ''; Stderr = "$_" }
    }

    if (-not $p.WaitForExit($TimeoutMs)) {
        try { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue } catch { }
        try { $p.WaitForExit(5000) | Out-Null } catch { }
        $err = if (Test-Path $stderr) { Get-Content $stderr -Raw -ErrorAction SilentlyContinue } else { '' }
        return [PSCustomObject]@{ ExitCode = -1; TimedOut = $true; Stdout = ''; Stderr = $err }
    }

    $outText = if (Test-Path $stdout) { [System.IO.File]::ReadAllText($stdout, [System.Text.Encoding]::UTF8) } else { '' }
    $errText = if (Test-Path $stderr) { [System.IO.File]::ReadAllText($stderr, [System.Text.Encoding]::UTF8) } else { '' }
    return [PSCustomObject]@{ ExitCode = $p.ExitCode; TimedOut = $false; Stdout = $outText; Stderr = $errText }
}

function Get-SummaryLine([string]$Text) {
    if (-not $Text) { return '' }
    $m = [regex]::Matches($Text, '(?im)^\[[^\]]+\]\s+PASS=\d+\s+FAIL=\d+.*$')
    if ($m.Count -gt 0) { return $m[$m.Count - 1].Value.Trim() }
    return ''
}

function Invoke-Build([string]$Tag, [string]$OutExe, [string[]]$Sources, [string[]]$ExtraArgs) {
    Write-Host ""
    Write-Host "==== 构建 $Tag ===="
    $cpu = @($CommonFlags) + @('-o', $OutExe) + $Sources + $ExtraArgs
    Write-Host "  > `"$CC`" $((@('-std=c11','-Wall','-Wextra','-Werror','...') + $Sources) -join ' ')"
    $log = Join-Path $BuildDir ("$Tag.build.txt")
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $out = & $CCExe @cpu 2>&1
    $code = $LASTEXITCODE
    $sw.Stop()
    $out | Out-File -FilePath $log -Encoding utf8 -Force
    if ($code -ne 0) {
        Write-Host "  [BUILD FAIL] $Tag (exit=$code)" -ForegroundColor Red
        Show-Tail $log 40
        return $false
    }
    Write-Host "  [BUILD OK] $Tag ($([math]::Round($sw.Elapsed.TotalMilliseconds,0)) ms)"
    return $true
}

# ------------------------------------------------------------
# 测试清单：File=测试源；Src=需要链接的 src 文件；Libs=链接库
# integration_gui_state 自身 #include "gui.c"/"gui_config.c"，因此不再单独链接两者
# ------------------------------------------------------------
$S = {
    param([string[]]$names)
    return @($names | ForEach-Object { Join-Path $SrcDir $_ })
}

$Tests = @(
    @{ Name='unit_pattern';       File='tests/unit/test_pattern.c';
       Src=(& $S @('pattern.c'));                       Libs=@() },
    @{ Name='unit_config';        File='tests/unit/test_config.c';
       Src=(& $S @('config.c','util.c','md5.c'));       Libs=@('user32') },
    @{ Name='unit_encoding';      File='tests/unit/test_encoding.c';
       Src=(& $S @('config.c','util.c','md5.c'));       Libs=@('user32') },
    @{ Name='integration_ops';    File='tests/integration/test_ops.c';
       Src=(& $S @('ops.c','pattern.c','config.c','util.c','md5.c','device.c','usbspeed.c'));
       Libs=@('user32','gdi32','shell32','comctl32','advapi32','setupapi','cfgmgr32') },
    @{ Name='integration_device_msg'; File='tests/integration/test_device_msg.c';
       Src=(& $S @('device.c','config.c','util.c','md5.c'));
       Libs=@('user32','advapi32','setupapi','cfgmgr32') },
    @{ Name='integration_gui_state'; File='tests/integration/test_gui_state.c';
       Src=(& $S @('ops.c','pattern.c','config.c','util.c','md5.c','device.c','usbspeed.c'));
       Libs=@('user32','gdi32','shell32','comctl32','advapi32','setupapi','cfgmgr32') },
    @{ Name='integration_logging'; File='tests/integration/test_logging.c';
       Src=(& $S @('util.c','device.c','config.c','md5.c'));
       Libs=@('user32','advapi32','setupapi','cfgmgr32') }
)

$Selected = @($Tests | Where-Object { -not $Filter -or $_.Name -like "*$Filter*" })

# ------------------------------------------------------------
# 构建主程序（windres 资源 + src/*.c），供 CLI 测试与资源检查使用
# ------------------------------------------------------------
$needMain = (-not $Filter) -or ($Filter -match '(?i)cli') -or
            [bool]($Selected | Where-Object { $_.Name -eq 'integration_logging' })
$MainExe = Join-Path $BuildDir 'udisktools.exe'
$mainOk = $true

if ($needMain) {
    if (-not $WindRes) {
        Write-Host "[FATAL] 找不到 windres（资源编译需要）" -ForegroundColor Red
        exit 1
    }
    Write-Host ""
    Write-Host "==== 构建主程序（windres + 全部 src） ===="
    $ResObj = Join-Path $BuildDir 'udisktools_res.o'
    $resArgs = @('--codepage=65001', '-I', 'res', '-I', 'src', '-o', $ResObj, 'res/udisktools.rc')
    Write-Host "  > windres $($resArgs -join ' ')"
    $resOut = & $WindRes @resArgs 2>&1
    if ($LASTEXITCODE -ne 0) {
        $resOut | Out-File (Join-Path $BuildDir 'main.build.txt') -Encoding utf8
        Write-Host "  [BUILD FAIL] windres 资源编译 (exit=$LASTEXITCODE)" -ForegroundColor Red
        $resOut | Select-Object -First 20 | ForEach-Object { Write-Host "  | $_" }
        $mainOk = $false
    } else {
        $AllSrc = Get-ChildItem (Join-Path $SrcDir '*.c') | ForEach-Object { "src/$($_.Name)" }
        $mainFlags = @('-municode','-mwindows') +
                     @('-luser32','-lgdi32','-lshell32','-lcomctl32','-ladvapi32',
                       '-lsetupapi','-lcfgmgr32','-ldbghelp')
        $mainOk = Invoke-Build -Tag 'main' -OutExe $MainExe -Sources @($AllSrc + @($ResObj)) -ExtraArgs $mainFlags
    }
    if (-not $mainOk) {
        Add-Result 'build:main' 'FAIL' 0 '主程序构建失败'
    }
}

# ------------------------------------------------------------
# 逐个构建 + 运行 C 测试
# ------------------------------------------------------------
foreach ($t in $Selected) {
    $tag = $t.Name
    $exe = Join-Path $BuildDir ("$tag.exe")
    $libArgs = @($t.Libs | ForEach-Object { "-l$_" })
    $extra = @() + $libArgs
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $built = Invoke-Build -Tag $tag -OutExe $exe -Sources (@($t.File) + $t.Src) -ExtraArgs $extra
    if (-not $built) {
        Add-Result $tag 'FAIL' $sw.Elapsed.TotalMilliseconds '构建失败'
        continue
    }
    $r = Invoke-Process $exe @() $tag 30000
    $sw.Stop()
    $detail = Get-SummaryLine $r.Stdout
    if ($r.TimedOut) {
        Write-Host "  [TIMEOUT] $tag (>30s)" -ForegroundColor Red
        Add-Result $tag 'TIMEOUT' $sw.Elapsed.TotalMilliseconds '30s 超时'
        Show-Tail (Join-Path $BuildDir "$tag.out.txt")
        continue
    }
    if ($r.ExitCode -ne 0) {
        Write-Host "  [TEST FAIL] $tag (exit=$($r.ExitCode))" -ForegroundColor Red
        Add-Result $tag 'FAIL' $sw.Elapsed.TotalMilliseconds $(if ($detail) { $detail } else { "exit=$($r.ExitCode)" })
        Show-Tail (Join-Path $BuildDir "$tag.out.txt")
        Show-Tail (Join-Path $BuildDir "$tag.err.txt") 10
        continue
    }
    Write-Host "  [TEST OK] $tag $(if ($detail) { $detail })"
    Add-Result $tag 'PASS' $sw.Elapsed.TotalMilliseconds $detail
}

# ------------------------------------------------------------
# CLI 测试（--version/--help/--check-ini 有效/无效）
# ------------------------------------------------------------
$CliTests = @(
    @{ Name='cli_version'; Args=@('--version'); Exit=0;
       Contains=@('1.2.0','1.2.0.0','构建时间'); NotContains=@() },
    @{ Name='cli_help';    Args=@('--help');    Exit=0;
       Contains=@('--check-ini','--version','--ini','1.2.0'); NotContains=@() },
    @{ Name='cli_check_ini_valid'; Args=@('--check-ini','--ini','profiles/UdiskTools.ini');
       Exit=0; Contains=@('UdiskTools.ini','4','配置版本','1.0.0'); NotContains=@('加载失败') },
    @{ Name='cli_check_ini_invalid'; Args=@('--check-ini','--ini','no_such_ini_file.ini');
       Exit=1; Contains=@('no_such_ini_file.ini'); NotContains=@() }
)
$SelectedCli = @($CliTests | Where-Object { -not $Filter -or ($_.Name -like "*$Filter*") })

if ($SelectedCli.Count -gt 0) {
    if (-not $mainOk) {
        foreach ($c in $SelectedCli) { Add-Result $c.Name 'FAIL' 0 '主程序构建失败，无法执行 CLI 测试' }
    } else {
        foreach ($c in $SelectedCli) {
            $sw = [System.Diagnostics.Stopwatch]::StartNew()
            $r = Invoke-Process $MainExe $c.Args $c.Name 30000
            $sw.Stop()
            if ($r.TimedOut) {
                Write-Host "  [TIMEOUT] $($c.Name)" -ForegroundColor Red
                Add-Result $c.Name 'TIMEOUT' $sw.Elapsed.TotalMilliseconds '30s 超时'
                continue
            }
            $okCase = ($r.ExitCode -eq $c.Exit)
            foreach ($needle in $c.Contains) {
                if (-not $r.Stdout.Contains($needle)) { $okCase = $false }
            }
            foreach ($bad in $c.NotContains) {
                if ($r.Stdout.Contains($bad)) { $okCase = $false }
            }
            if (-not $okCase) {
                Write-Host "  [CLI FAIL] $($c.Name): exit=$($r.ExitCode) want=$($c.Exit) args=$($c.Args -join ' ')" -ForegroundColor Red
                Show-Tail (Join-Path $BuildDir ("$($c.Name).out.txt")) 20
                Add-Result $c.Name 'FAIL' $sw.Elapsed.TotalMilliseconds "exit=$($r.ExitCode) want=$($c.Exit)"
            } else {
                Write-Host "  [CLI OK] $($c.Name) exit=$($r.ExitCode)"
                Add-Result $c.Name 'PASS' $sw.Elapsed.TotalMilliseconds "exit=$($r.ExitCode)"
            }
        }
    }
}

# ------------------------------------------------------------
# 汇总
# ------------------------------------------------------------
Write-Host ""
Write-Host "============================================================"
Write-Host "测试汇总（CC=$CC）"
Write-Host "============================================================"
$script:Results | Format-Table -AutoSize -Property Name, Status, Ms, Detail | Out-String -Width 200 | Write-Host
$nPass = @($script:Results | Where-Object { $_.Status -eq 'PASS' }).Count
$nFail = @($script:Results | Where-Object { $_.Status -ne 'PASS' }).Count
Write-Host "TOTAL: PASS=$nPass FAIL=$nFail"
if ($script:Results.Count -eq 0) {
    Write-Host "[FAIL] 没有匹配的测试（-Filter '$Filter'？）" -ForegroundColor Red
    exit 1
}
if ($nFail -gt 0) {
    Write-Host "RESULT: FAIL（构建/运行日志见 tests/build/）" -ForegroundColor Red
    exit 1
}
Write-Host "RESULT: PASS" -ForegroundColor Green
exit 0
