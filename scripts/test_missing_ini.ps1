param(
    [string]$Mode = "yes"   # yes = 点"是（以默认配置运行）"；no = 点"否（退出）"
)
$src = @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public class W {
    public delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder sb, int max);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder sb, int max);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
    public static IntPtr dialog = IntPtr.Zero;
    public static bool Cb(IntPtr h, IntPtr l) {
        StringBuilder cls = new StringBuilder(64);
        GetClassNameW(h, cls, 64);
        if (cls.ToString() == "#32770") {
            StringBuilder title = new StringBuilder(128);
            GetWindowTextW(h, title, 128);
            if (title.ToString().StartsWith("UdiskTools")) { dialog = h; return false; }
        }
        return true;
    }
    public static IntPtr FindDialog() { dialog = IntPtr.Zero; EnumWindows(Cb, IntPtr.Zero); return dialog; }
}
'@
Add-Type -TypeDefinition $src

# 等待弹窗出现（最多 10 秒）
$d = [IntPtr]::Zero
for ($i = 0; $i -lt 20 -and $d -eq [IntPtr]::Zero; $i++) {
    $d = [W]::FindDialog()
    if ($d -eq [IntPtr]::Zero) { Start-Sleep -Milliseconds 500 }
}
if ($d -eq [IntPtr]::Zero) { Write-Output "FAIL: dialog not found"; exit 1 }
Write-Output "DIALOG FOUND"

# 读取弹窗文本（静态控件）
$sb = New-Object System.Text.StringBuilder 1024
# 发 WM_COMMAND IDYES(6)/IDNO(7) 关闭 MessageBox
$id = if ($Mode -eq "yes") { 6 } else { 7 }
[void][W]::SendMessageW($d, 0x0111, [IntPtr]$id, [IntPtr]::Zero)   # WM_COMMAND id
Write-Output "CLICKED $Mode"
