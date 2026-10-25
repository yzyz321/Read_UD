param([string]$IniPath = "")
$src = @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public class WT {
    public delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder sb, int max);
    [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h, int id);
    [DllImport("user32.dll")] public static extern IntPtr SendMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern int GetWindowLongW(IntPtr h, int i);
    public static IntPtr main = IntPtr.Zero;
    public static bool Cb(IntPtr h, IntPtr l) {
        StringBuilder cls = new StringBuilder(64);
        GetClassNameW(h, cls, 64);
        if (cls.ToString() == "UDK_MAINCLASS") { main = h; return false; }
        return true;
    }
    public static IntPtr FindMain() { main = IntPtr.Zero; EnumWindows(Cb, IntPtr.Zero); return main; }
}
'@
Add-Type -TypeDefinition $src
Start-Sleep -Milliseconds 1500
$m = [WT]::FindMain()
if ($m -eq [IntPtr]::Zero) { Write-Output "FAIL: main window not found"; exit 1 }
$chk = [WT]::GetDlgItem($m, 1008)
$state = [WT]::SendMessageW($chk, 0x00F1, [IntPtr]0, [IntPtr]::Zero)   # BM_GETCHECK
$ex = [WT]::GetWindowLongW($m, -20)                                    # GWL_EXSTYLE
Write-Output ("checkbox 1008 checked: " + ($state -eq 1))
Write-Output ("WS_EX_TOPMOST set: " + (($ex -band 0x8) -ne 0))
