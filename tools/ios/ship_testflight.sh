#!/bin/zsh
# TestFlight 출시 빌드 — 순서를 바꾸지 않는다 (2026-10-15 감독).
# 사고: xcodebuild archive 는 Saved/StagedBuilds/IOS 의 옛 쿡 데이터를 그대로 넣는다 → 새 바이너리와 안 맞아 켜자마자 꺼짐.
#       ini 만 바꾸고 archive 하면 Info.plist·빌드 번호도 안 바뀐다.
# 사용: tools/ios/ship_testflight.sh            (쿡→아카이브→폰에 개발판 설치·45초 생존 확인까지)
#       tools/ios/ship_testflight.sh --upload   (위 + 폰 확인 통과 시에만 App Store Connect 업로드)
# 서명 신원·팀 id 가 섞이므로 로그는 Saved/ShipBuild/logs 에만, 화면엔 판정 줄만.
set -u
# worktree 에서 불러도 빌드는 본 checkout(Saved·Intermediate 가 있는 곳)에서 한다
cd "$(dirname "$(git -C "$(dirname "$0")" rev-parse --path-format=absolute --git-common-dir)")"
P=$PWD; E=~/UE_5.8/Engine/Build/BatchFiles
OUT=$P/Saved/ShipBuild; L=$OUT/logs; mkdir -p $L
STAMP=$(date +%m%d_%H%M); A=$OUT/Snarehaven_$STAMP.xcarchive
START=$(date +%s)
die() { echo "❌ $1 (로그 $L)"; exit 1; }
plist() { cat > $1 <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict><key>method</key><string>$2</string><key>signingStyle</key><string>automatic</string><key>destination</key><string>$3</string></dict></plist>
EOF
}

echo "① 에디터 빌드 (쿡이 쓰는 모듈을 소스와 맞춘다)"
$E/Mac/Build.sh SnarehavenEditor Mac Development -project="$P/Snarehaven.uproject" > $L/editor.log 2>&1 || die "에디터 빌드 실패"

echo "② 쿡·스테이지 (IOS Shipping)"
$E/RunUAT.sh BuildCookRun -project="$P/Snarehaven.uproject" -noP4 -platform=IOS -clientconfig=Shipping \
  -build -cook -stage -pak -iostore -nocompileeditor -unattended -utf8output > $L/bcr.log 2>&1 || die "BuildCookRun 실패"
PAK=Saved/StagedBuilds/IOS/cookeddata/snarehaven/content/paks/snarehaven-ios.utoc
[ -f $PAK ] && [ $(stat -f %m $PAK) -ge $START ] || die "스테이지 데이터가 이번 쿡 것이 아님"

echo "③ Info.plist·빌드 번호 재생성"
$E/Mac/Build.sh Snarehaven IOS Shipping -project="$P/Snarehaven.uproject" > $L/plist.log 2>&1 || die "plist 생성 실패"

echo "④ 아카이브"
xcodebuild -project "Intermediate/ProjectFilesIOS/Snarehaven (IOS).xcodeproj" -scheme Snarehaven -configuration Shipping \
  -destination "generic/platform=iOS" -archivePath $A -allowProvisioningUpdates archive > $L/archive.log 2>&1 || die "아카이브 실패"
APP=$(ls -d $A/Products/Applications/*.app); INFO=$APP/Info.plist
VER=$(/usr/libexec/PlistBuddy -c "Print :CFBundleVersion" $INFO)
/usr/libexec/PlistBuddy -c "Print :NSCameraUsageDescription" $INFO >/dev/null 2>&1 || die "Info.plist 카메라 문구 없음"
[ "$VER" != "$(cat $OUT/last_uploaded_version 2>/dev/null)" ] || die "빌드 번호 $VER 가 지난 업로드와 같음"
cmp -s $PAK $APP/cookeddata/snarehaven/content/paks/snarehaven-ios.utoc || die "아카이브 안 데이터가 방금 쿡과 다름"

echo "⑤ 같은 아카이브를 개발판으로 내보내 폰(15 Pro)에서 실행"
plist $L/dev.plist debugging export
xcodebuild -exportArchive -archivePath $A -exportPath $OUT/dev_$STAMP -exportOptionsPlist $L/dev.plist -allowProvisioningUpdates > $L/devexport.log 2>&1 || die "개발판 내보내기 실패"
xcrun devicectl list devices --json-output $L/dev.json > /dev/null 2>&1
U=$(python3 -c "import json;print(next(x['identifier'] for x in json.load(open('$L/dev.json'))['result']['devices'] if x['hardwareProperties'].get('productType')=='iPhone16,1'))" 2>/dev/null) || die "폰(15 Pro) 연결 안 됨"
xcrun devicectl device install app --device $U $OUT/dev_$STAMP/*.ipa > $L/install.log 2>&1 || die "폰 설치 실패"
xcrun devicectl device process launch --device $U --terminate-existing com.soilrist.snarehaven.ios > $L/launch.log 2>&1 || die "실행 실패"
sleep 45
xcrun devicectl device info processes --device $U 2>/dev/null | /usr/bin/grep -q "Snarehaven-IOS-Shipping" || die "45초 안에 꺼짐 — 업로드 안 함"
echo "✅ 폰에서 45초 살아 있음 (빌드 $VER)"

[ "${1:-}" = "--upload" ] || { echo "업로드는 --upload 로"; exit 0; }
echo "⑥ 업로드"
plist $L/up.plist app-store-connect upload
xcodebuild -exportArchive -archivePath $A -exportPath $OUT/up_$STAMP -exportOptionsPlist $L/up.plist -allowProvisioningUpdates > $L/upload.log 2>&1 || die "업로드 실패"
/usr/bin/grep -q "Upload succeeded" $L/upload.log || die "업로드 성공 줄 없음"
echo $VER > $OUT/last_uploaded_version
echo "✅ 업로드 완료 (빌드 $VER) — App Store Connect 처리 10~20분"
