# Cross-process verification of cfg value-editor sync fix
# Simulates CBN_SELCHANGE after CB_SETCURSEL, reads back control states.
$src = @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public class W {
    public delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder sb, int max);
    [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h, int id);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern IntPtr SendMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessageW(IntPtr h, uint m, IntPtr w, StringBuilder l);
    [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
    public static IntPtr FindByClass(string cls) {
        IntPtr result = IntPtr.Zero;
        EnumProc cb = delegate(IntPtr h, IntPtr l) {
            StringBuilder sb = new StringBuilder(256);
            GetClassNameW(h, sb, 256);
            if (sb.ToString() == cls) { result = h; return false; }
            return true;
        };
        EnumWindows(cb, IntPtr.Zero);
        return result;
    }
    public static string GetText(IntPtr h) {
        int len = (int)SendMessageW(h, 0x000E, IntPtr.Zero, IntPtr.Zero);
        StringBuilder sb = new StringBuilder(len + 2);
        SendMessageW(h, 0x000D, (IntPtr)(len + 1), sb);
        return sb.ToString();
    }
    public static int GetCurSel(IntPtr h) { return (int)SendMessageW(h, 0x0147, IntPtr.Zero, IntPtr.Zero); }
    public static void SetCurSel(IntPtr h, int i) { SendMessageW(h, 0x014E, (IntPtr)i, IntPtr.Zero); }
    public static void NotifySelChange(IntPtr parent, IntPtr combo, int id) {
        SendMessageW(parent, 0x0111, (IntPtr)(id | (1 << 16)), combo);
    }
    public static void NotifyEditChange(IntPtr parent, IntPtr combo, int id) {
        SendMessageW(parent, 0x0111, (IntPtr)(id | (5 << 16)), combo);
    }
}
'@
Add-Type -TypeDefinition $src

$main = [W]::FindByClass("UDK_MAINCLASS")
if ($main -eq [IntPtr]::Zero) { Write-Output "FAIL: main window not found"; exit 1 }
$btnCfg = [W]::GetDlgItem($main, 1007)
[W]::SendMessageW($btnCfg, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null  # BM_CLICK
Start-Sleep -Milliseconds 600
$cfg = [W]::FindByClass("UDK_CFGCLASS")
if ($cfg -eq [IntPtr]::Zero) { Write-Output "FAIL: cfg window not found"; exit 1 }

$key  = [W]::GetDlgItem($cfg, 3011)
$valC = [W]::GetDlgItem($cfg, 3012)
$valE = [W]::GetDlgItem($cfg, 3009)
$lbl  = [W]::GetDlgItem($cfg, 3014)
$hint = [W]::GetDlgItem($cfg, 3013)

function Report($tag) {
    $lc = [W]::IsWindowVisible($valC); $le = [W]::IsWindowVisible($valE)
    $vt = if ($lc) { [W]::GetText($valC) } else { [W]::GetText($valE) }
    $ki = [W]::GetCurSel($key)
    $kt = if ($ki -ge 0) { $ki.ToString() + ":" + [W]::GetText($key) } else { "(typed)" }
    Write-Output ("[{0}] key={1} label={2} comboVis={3} editVis={4} val='{5}'" -f $tag, $kt, [W]::GetText($lbl), $lc, $le, $vt)
    Write-Output ("    hint='{0}'" -f [W]::GetText($hint))
}

Report "init(idx0 ENABLE_READ)"
# select SRCPATH (index 8) via list-selection path (CBN_SELCHANGE)
[W]::SetCurSel($key, 8)
[W]::NotifySelChange($cfg, $key, 3011)
Start-Sleep -Milliseconds 300
Report "after select SRCPATH"
# back to ENABLE_READ (wrap to idx 0)
[W]::SetCurSel($key, 0)
[W]::NotifySelChange($cfg, $key, 3011)
Start-Sleep -Milliseconds 300
Report "after select ENABLE_READ"
# typed-key path: clear combo edit, type SRCPATH via WM_CHAR, notify CBN_EDITCHANGE
[W]::SendMessageW($key, 0x000C, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null  # WM_SETTEXT ""
foreach ($ch in "SRCPATH".ToCharArray()) { [W]::PostMessageW($key, 0x0102, [IntPtr]$ch, [IntPtr]::Zero) | Out-Null }
[W]::NotifyEditChange($cfg, $key, 3011)
Start-Sleep -Milliseconds 300
Report "after typed SRCPATH (EDITCHANGE path)"
Write-Output "DONE"
