#!/bin/bash
cd ~/efmos || exit 1
echo "=== build log tail ==="
tail -20 build_baseline.log 2>&1
echo "=== artifacts ==="
ls -la disk.img disk.vmdk 2>&1
echo "=== make/gcc running? ==="
pgrep -a make | head -3
pgrep -a gcc | head -3
echo "=== log mtime ==="
stat -c '%y %s' build_baseline.log 2>&1
date
