/* ============================================================
 * test_device_msg.c - dev_post_changes 设备事件消息协议集成测试
 * 隐藏窗口（message-only）+ PeekMessage 验证：
 *   old=0,new=F|G          → 一条 inserted 位图(lparam=1)，wParam=F|G
 *   old=F,new=G            → 先 removed(F,lparam=0)，再 inserted(G,lparam=1)
 *   old==new               → 无消息
 *   old=F|G,new=0          → 一条 removed 位图(lparam=0)
 *   old=F,new=F|G          → 一条 inserted(G,lparam=1)，wParam=G
 *   old=F|G,new=G          → 一条 removed(F,lparam=0)，wParam=F
 * 同时验证与 device.h/gui.h 相同的 WM_APP_DEVCOUNT 号码一致。
 * ============================================================ */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "device.h"
#include "gui.h"

static int g_fail = 0, g_pass = 0;

#define BIT(d) (1u << ((d) - L'A'))

typedef struct { UINT msg; WPARAM wp; LPARAM lp; } EV;

static LRESULT CALLBACK test_wnd_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)wp; (void)lp;
    return DefWindowProcW(h, msg, wp, lp);
}

static HWND make_hidden_window(void)
{
    WNDCLASSW wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = test_wnd_proc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"UDK_TEST_DEVMSG_CLASS";
    RegisterClassW(&wc);
    return CreateWindowExW(0, L"UDK_TEST_DEVMSG_CLASS", L"", 0, 0, 0, 0, 0,
                           HWND_MESSAGE, NULL, GetModuleHandleW(NULL), NULL);
}

static int drain(HWND h, EV* out, int maxn)
{
    int n = 0;
    MSG msg;
    while (PeekMessageW(&msg, h, 0, 0, PM_REMOVE)) {
        if (n < maxn) { out[n].msg = msg.message; out[n].wp = msg.wParam; out[n].lp = msg.lParam; }
        n++;
    }
    return n;
}

static void expect_ev(const char* name, const EV* ev, UINT msg, WPARAM wp, LPARAM lp)
{
    if (ev->msg == msg && ev->wp == wp && ev->lp == lp) {
        printf("PASS  %-58s (wParam=0x%02X lParam=%ld)\n", name,
               (unsigned)wp, (long)lp);
        g_pass++;
    } else {
        printf("FAIL  %-58s got msg=0x%X w=0x%02X l=%ld, want msg=0x%X w=0x%02X l=%ld\n",
               name, ev->msg, (unsigned)ev->wp, (long)ev->lp,
               msg, (unsigned)wp, (long)lp);
        g_fail++;
    }
}

int main(void)
{
    EV ev[8];
    int n;
    HWND h = make_hidden_window();
    if (!h) { printf("FAIL  create hidden window\n"); return 1; }

    printf("== test_devmsg: WM_APP_DEVCOUNT==0x%04X (device.h) gui.h==0x%04X ==\n",
           (unsigned)WM_APP_DEVCOUNT, (unsigned)WM_APP_DEVCOUNT);

    /* 0) 号码一致性由同一头文件保证；此处显式检查表达式 */
    if (WM_APP_DEVCOUNT == (WM_APP + 3)) { printf("PASS  WM_APP_DEVCOUNT == WM_APP+3\n"); g_pass++; }
    else { printf("FAIL  WM_APP_DEVCOUNT != WM_APP+3\n"); g_fail++; }

    /* 1) old=0, new=F|G → 一条 inserted 位图 */
    dev_post_changes(h, 0, BIT(L'F') | BIT(L'G'));
    n = drain(h, ev, 8);
    if (n == 1) {
        expect_ev("insert F|G: one message, inserted bitmap, lp=1",
                  &ev[0], WM_APP_DEVCOUNT, (WPARAM)(BIT(L'F') | BIT(L'G')), 1);
    } else {
        printf("FAIL  insert F|G: expected 1 message, got %d\n", n); g_fail++;
    }

    /* 2) old=F, new=G → 先 removed(F,0)，再 inserted(G,1) */
    dev_post_changes(h, BIT(L'F'), BIT(L'G'));
    n = drain(h, ev, 8);
    if (n == 2) {
        expect_ev("replace F->G: first message removed F lp=0", &ev[0], WM_APP_DEVCOUNT, (WPARAM)BIT(L'F'), 0);
        expect_ev("replace F->G: second message inserted G lp=1", &ev[1], WM_APP_DEVCOUNT, (WPARAM)BIT(L'G'), 1);
    } else {
        printf("FAIL  replace F->G: expected 2 messages, got %d\n", n); g_fail++;
    }

    /* 3) old==new → 无消息 */
    dev_post_changes(h, BIT(L'F') | BIT(L'G'), BIT(L'F') | BIT(L'G'));
    n = drain(h, ev, 8);
    if (n == 0) { printf("PASS  old==new: no message\n"); g_pass++; }
    else { printf("FAIL  old==new: got %d messages\n", n); g_fail++; }

    /* 4) old=F|G, new=0 → 一条 removed 位图 */
    dev_post_changes(h, BIT(L'F') | BIT(L'G'), 0);
    n = drain(h, ev, 8);
    if (n == 1) {
        expect_ev("remove F|G: one message, removed bitmap, lp=0",
                  &ev[0], WM_APP_DEVCOUNT, (WPARAM)(BIT(L'F') | BIT(L'G')), 0);
    } else {
        printf("FAIL  remove F|G: expected 1 message, got %d\n", n); g_fail++;
    }

    /* 5) old=0, new=0 → 无消息 */
    dev_post_changes(h, 0, 0);
    n = drain(h, ev, 8);
    if (n == 0) { printf("PASS  0->0: no message\n"); g_pass++; }
    else { printf("FAIL  0->0: got %d messages\n", n); g_fail++; }

    /* 6) old=F, new=F|G → 只报新增 G */
    dev_post_changes(h, BIT(L'F'), BIT(L'F') | BIT(L'G'));
    n = drain(h, ev, 8);
    if (n == 1) {
        expect_ev("add G while F stays: inserted G only", &ev[0], WM_APP_DEVCOUNT, (WPARAM)BIT(L'G'), 1);
    } else {
        printf("FAIL  add G while F stays: expected 1 message, got %d\n", n); g_fail++;
    }

    /* 7) old=F|G, new=G → 只报移除 F */
    dev_post_changes(h, BIT(L'F') | BIT(L'G'), BIT(L'G'));
    n = drain(h, ev, 8);
    if (n == 1) {
        expect_ev("remove F while G stays: removed F only", &ev[0], WM_APP_DEVCOUNT, (WPARAM)BIT(L'F'), 0);
    } else {
        printf("FAIL  remove F while G stays: expected 1 message, got %d\n", n); g_fail++;
    }

    /* 8) hwnd=NULL → 不发（不崩溃） */
    dev_post_changes(NULL, 0, BIT(L'F'));
    n = drain(h, ev, 8);
    if (n == 0) { printf("PASS  hwnd=NULL: no message, no crash\n"); g_pass++; }
    else { printf("FAIL  hwnd=NULL: got %d messages\n", n); g_fail++; }

    DestroyWindow(h);
    printf("\n[test_device_msg] PASS=%d FAIL=%d\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
