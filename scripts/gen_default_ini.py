#!/usr/bin/env python3
# 生成 src/default_ini.h：把 profiles/UdiskTools.ini 转成 C 字符串常量（UTF-8）
# 用法：python scripts/gen_default_ini.py
# 说明：配置窗口的[恢复默认设置]会用该内嵌内容覆盖磁盘 ini，改了模板后必须重跑本脚本。
import io
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, 'profiles', 'UdiskTools.ini')
DST = os.path.join(ROOT, 'src', 'default_ini.h')

raw = open(SRC, 'rb').read()
if raw.startswith(b'\xef\xbb\xbf'):
    raw = raw[3:]
text = raw.decode('utf-8').replace('\r\n', '\n').replace('\r', '\n')
lines = text.splitlines(keepends=True)   # 保留每行行尾 \n，保证内嵌内容与文件逐字节一致

out = io.StringIO()
out.write('/* 自动生成：内嵌出厂默认配置（供配置窗口[恢复默认设置]使用） */\r\n')
out.write('/* 来源：profiles/UdiskTools.ini  生成方式：转义为 C 字符串（UTF-8） */\r\n')
out.write('#ifndef UDK_DEFAULT_INI_H\r\n')
out.write('#define UDK_DEFAULT_INI_H\r\n\r\n')
out.write('static const char* DEFAULT_INI_UTF8 =\r\n')
for ln in lines:
    esc = ln.replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\n')
    out.write('"%s"\r\n' % esc)
out.write(';\r\n\r\n#endif\r\n')

open(DST, 'wb').write(out.getvalue().encode('utf-8'))
print('generated %s (%d lines)' % (DST, len(lines)))
