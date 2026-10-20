#!/bin/bash
# 사용: tools/director/ue_cmd.sh <파이썬파일 또는 코드> [제한초=60] — ue_watch.py 가 태워진 에디터에서 실행하고 출력을 보여 준다
D=~/UNREAL/Snarehaven/Saved/DirectorCmd; mkdir -p $D; rm -f $D/out.txt
if [ -f "$1" ]; then cp "$1" $D/cmd.py.tmp; else printf '%s\n' "$1" > $D/cmd.py.tmp; fi; mv $D/cmd.py.tmp $D/cmd.py
for i in $(seq 1 $(( ${2:-60} * 4 ))); do [ -f $D/out.txt ] && { cat $D/out.txt; exit 0; }; sleep 0.25; done
echo "응답 없음(${2:-60}초)"; exit 1
