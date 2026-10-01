#!/bin/bash
# baseline build, log to file
cd ~/efmos || exit 1
rm -f build_baseline.log
nohup make -k > build_baseline.log 2>&1 &
echo "make started, pid $!"
sleep 3
echo "--- log so far ---"
tail -8 build_baseline.log
