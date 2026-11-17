#!/bin/bash
set +e
cd /mnt/d/src/repo/.DIODE/pca9555_demo || exit 1
B=build/native/pca9555_tool
echo "== dir ==";        $B --sim dir p0.2 out; echo "rc=$?"
echo "== write ==";      $B --sim write 0x1234; echo "rc=$?"
echo "== toggle ==";     $B --sim set p1.5 0 >/dev/null; $B --sim toggle p1.5; echo "rc=$?"
echo "== blink ==";      $B --sim blink p0.4 3 20; echo "rc=$?"
echo "== get 非法 ==";   $B --sim get 99; echo "rc=$?"
echo "== 未知命令 ==";   $B --sim foo; echo "rc=$?"
echo "== 未知选项 ==";   $B --bogus; echo "rc=$?"
echo "== 无参数 ==";     $B >/dev/null; echo "rc=$?"
