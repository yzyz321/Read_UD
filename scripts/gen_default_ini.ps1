<#
.SYNOPSIS
    生成 / 校验 src/default_ini.h —— 把 profiles/UdiskTools_default.ini 内嵌为 C 字符串常量。

.DESCRIPTION
    内嵌默认配置供 ini 缺失时启动与配置窗口 [恢复默认设置] 共用；必须与 profiles/UdiskTools_default.ini 保持同步。
    生成规则与 scripts/gen_default_ini.py 完全一致（本机可能无 Python，CI 以本脚本为准）：
      1. 读取 profiles/UdiskTools_default.ini 原始字节；
      2. 去掉 UTF-8 BOM（EF BB BF）；
      3. 按 UTF-8 严格解码；
      4. CRLF / CR 统一为 LF（因此源码在 LF/CRLF 工作区下结果一致）；
      5. 按“保留行尾 \n”切分（等价 Python str.splitlines(keepends=True)）；
      6. 逐行转义：\  -> \\，\" -> \\"，换行 LF -> \n；
      7. 输出：两行注释 + `static const char* DEFAULT_INI_UTF8 =` + 每行一个 C 字面量
         （每行以 CRLF 结束）+ `;` + 空行 + `#endif`，文件 UTF-8 无 BOM、行尾 CRLF。

    无参数   ：覆盖生成 src/default_ini.h
    -Check   ：生成到临时文件并与 src/default_ini.h 逐字节比较；
               不一致时打印差异摘要并以退出码 1 结束；一致退出码 0。
               （若仅行尾风格不同但内容一致，打印 WARN 并以 0 退出，以适配 CI checkout 的 CRLF 转换）

.EXAMPLE
    pwsh scripts/gen_default_ini.ps1
    pwsh scripts/gen_default_ini.ps1 -Check
#>
[CmdletBinding()]
param(
    [switch]$Check
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$scriptDir = $PSScriptRoot
if (-not $scriptDir) { $scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path }
$root   = Split-Path -Parent $scriptDir
$srcIni = Join-Path (Join-Path $root 'profiles') 'UdiskTools_default.ini'
$dstHdr = Join-Path (Join-Path $root 'src') 'default_ini.h'

$Utf8NoBom  = New-Object System.Text.UTF8Encoding($false, $false)
$Utf8Strict = New-Object System.Text.UTF8Encoding($false, $true)
$CRLF = "`r`n"

function New-DefaultIniText {
    param([Parameter(Mandatory = $true)][string]$Path)

    if (-not (Test-Path -LiteralPath $Path)) { throw "源文件不存在: $Path" }
    $raw = [System.IO.File]::ReadAllBytes($Path)

    $offset = 0
    if ($raw.Length -ge 3 -and $raw[0] -eq 0xEF -and $raw[1] -eq 0xBB -and $raw[2] -eq 0xBF) { $offset = 3 }
    $text = $Utf8Strict.GetString($raw, $offset, $raw.Length - $offset)

    # CRLF / CR -> LF
    $text = $text.Replace($CRLF, "`n").Replace("`r", "`n")

    # 按“保留行尾 \n”切分（等价 Python splitlines(keepends=True)；本文件不含其它 Unicode 行分隔符）
    $lines = New-Object System.Collections.Generic.List[string]
    if ($text.Length -gt 0) {
        $start = 0
        for ($i = 0; $i -lt $text.Length; $i++) {
            if ($text[$i] -eq [char]10) {
                $lines.Add($text.Substring($start, $i - $start + 1))
                $start = $i + 1
            }
        }
        if ($start -lt $text.Length) { $lines.Add($text.Substring($start)) }
    }

    $sb = New-Object System.Text.StringBuilder
    [void]$sb.Append('/* 自动生成：内嵌默认配置（ini 缺失时启动 / 配置窗口[恢复默认设置]共用） */' + $CRLF)
    [void]$sb.Append('/* 来源：profiles/UdiskTools_default.ini  生成方式：转义为 C 字符串（UTF-8） */' + $CRLF)
    [void]$sb.Append('#ifndef UDK_DEFAULT_INI_H' + $CRLF)
    [void]$sb.Append('#define UDK_DEFAULT_INI_H' + $CRLF + $CRLF)
    [void]$sb.Append('static const char* DEFAULT_INI_UTF8 =' + $CRLF)
    foreach ($line in $lines) {
        $escaped = $line.Replace('\', '\\').Replace('"', '\"').Replace("`n", '\n')
        [void]$sb.Append('"' + $escaped + '"' + $CRLF)
    }
    [void]$sb.Append(';' + $CRLF + $CRLF + '#endif' + $CRLF)
    return $sb.ToString()
}

function Write-Utf8NoBom {
    param([Parameter(Mandatory = $true)][string]$Path, [Parameter(Mandatory = $true)][string]$Text)
    $bytes = $Utf8NoBom.GetBytes($Text)
    [System.IO.File]::WriteAllBytes($Path, $bytes)
    return $bytes
}

function Get-FirstDiffOffset {
    param([byte[]]$A, [byte[]]$B)
    $n = [Math]::Min($A.Length, $B.Length)
    for ($i = 0; $i -lt $n; $i++) {
        if ($A[$i] -ne $B[$i]) { return $i }
    }
    if ($A.Length -ne $B.Length) { return $n }
    return -1
}

function Get-NormalizedText {
    param([string]$Text)
    return $Text.Replace($CRLF, "`n").Replace("`r", "`n")
}

function Show-DiffSummary {
    param(
        [string]$ExpectedText, [string]$ActualText,
        [byte[]]$ExpectedBytes, [byte[]]$ActualBytes,
        [int]$FirstDiff, [int]$LiteralCount
    )
    Write-Host '[check] FAIL: src/default_ini.h 与 profiles/UdiskTools_default.ini 的生成结果不一致' -ForegroundColor Red
    Write-Host ("  生成结果（来自 profiles）: {0} 字节 / {1} 个 C 字面量行" -f $ExpectedBytes.Length, $LiteralCount)
    Write-Host ("  当前文件 src/default_ini.h : {0} 字节" -f $ActualBytes.Length)

    $lineNo = 1
    for ($k = 0; $k -lt $FirstDiff -and $k -lt $ExpectedBytes.Length; $k++) {
        if ($ExpectedBytes[$k] -eq 10) { $lineNo++ }
    }
    Write-Host ("  首个不同字节偏移: {0}（生成结果约第 {1} 行）" -f $FirstDiff, $lineNo)

    $expLines = @((Get-NormalizedText $ExpectedText) -split "`n")
    $actLines = @((Get-NormalizedText $ActualText) -split "`n")
    $max = [Math]::Max($expLines.Count, $actLines.Count)
    $firstLine = 0
    $diffLines = 0
    for ($i = 0; $i -lt $max; $i++) {
        $e = if ($i -lt $expLines.Count) { $expLines[$i] } else { '<缺少该行>' }
        $a = if ($i -lt $actLines.Count) { $actLines[$i] } else { '<缺少该行>' }
        if ($e -cne $a) {
            $diffLines++
            if ($firstLine -eq 0) { $firstLine = $i + 1 }
        }
    }
    Write-Host ("  首个不同行号: {0}；不同行数: {1}" -f $firstLine, $diffLines)
    if ($firstLine -gt 0 -and $firstLine -le $expLines.Count) {
        Write-Host ("  期望（生成）第 {0} 行: {1}" -f $firstLine, $expLines[$firstLine - 1])
    }
    if ($firstLine -gt 0 -and $firstLine -le $actLines.Count) {
        Write-Host ("  实际（文件）第 {0} 行: {1}" -f $firstLine, $actLines[$firstLine - 1])
    }
    Write-Host '  修复: 运行 pwsh scripts/gen_default_ini.ps1 重新生成并提交 src/default_ini.h'
}

# ---------------- main ----------------
$generated = New-DefaultIniText -Path $srcIni
$literalCount = 0
foreach ($genLine in @($generated -split "`r`n")) {
    if ($genLine.StartsWith('"')) { $literalCount++ }
}

if (-not $Check) {
    $bytes = Write-Utf8NoBom -Path $dstHdr -Text $generated
    Write-Host ("[gen] 已生成 src/default_ini.h：{0} 字节，{1} 个 C 字面量行（来源 profiles/UdiskTools_default.ini）" -f $bytes.Length, $literalCount)
    exit 0
}

if (-not (Test-Path -LiteralPath $dstHdr)) {
    Write-Host '[check] FAIL: 缺少 src/default_ini.h（请先运行 pwsh scripts/gen_default_ini.ps1）' -ForegroundColor Red
    exit 1
}

$tmp = [System.IO.Path]::GetTempFileName()
$rc = 0
try {
    $genBytes = Write-Utf8NoBom -Path $tmp -Text $generated
    $curBytes = [System.IO.File]::ReadAllBytes($dstHdr)
    $firstDiff = Get-FirstDiffOffset -A $genBytes -B $curBytes
    if ($firstDiff -lt 0) {
        Write-Host ("[check] OK: src/default_ini.h 与 profiles/UdiskTools_default.ini 一致（{0} 字节，{1} 个 C 字面量行）" -f $curBytes.Length, $literalCount)
    } else {
        $genNorm = Get-NormalizedText $generated
        $curNorm = Get-NormalizedText ([System.Text.Encoding]::UTF8.GetString($curBytes))
        if ([string]::Equals($genNorm, $curNorm, [System.StringComparison]::Ordinal)) {
            Write-Host '[check] WARN: 内容与 profiles 一致，但行尾风格不同（生成结果=CRLF，当前文件非 CRLF）。CI 视为通过。' -ForegroundColor Yellow
        } else {
            Show-DiffSummary -ExpectedText $generated -ActualText ([System.Text.Encoding]::UTF8.GetString($curBytes)) `
                -ExpectedBytes $genBytes -ActualBytes $curBytes -FirstDiff $firstDiff -LiteralCount $literalCount
            $rc = 1
        }
    }
} finally {
    Remove-Item -LiteralPath $tmp -Force -ErrorAction SilentlyContinue
}
exit $rc
