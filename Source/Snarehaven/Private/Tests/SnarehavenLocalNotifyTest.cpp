// ROADMAP M21-04 2층 검사 — 앱 밖 알림 예약 계산 (`SnareLocalNotify`).
// ① 기본값이 DESIGN 3.2 「앱 밖 알림」 표와 같은가 — **숫자는 설계서에서 읽는다.**
// ② 예약 시각 계산·밤 밀기·하루 상한·지난 시각 빼기·끄기가 도는가 — **검사 전용 값**(Verification.md 6)이라 표 숫자가 바뀌어도 안 깨진다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.LocalNotify; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Ranch/RanchComponent.h"
#include "Ranch/SnareLocalNotify.h"

namespace SnarehavenLocalNotifyTest
{
	int32 DesignNumber(const FString& Design, const TCHAR* Pattern)
	{
		FRegexMatcher Matcher(FRegexPattern(Pattern), Design);
		return Matcher.FindNext() ? FCString::Atoi(*Matcher.GetCaptureGroup(1)) : -1;
	}

	/** 검사 전용 값 — 표 숫자와 일부러 다르게. */
	SnareLocalNotify::FRules TestRules()
	{
		SnareLocalNotify::FRules Rules;
		Rules.RanchFullMinutes = 100;
		Rules.UpkeepWarnHours = 3;
		Rules.DailyCap = 2;
		Rules.QuietStartHour = 21;
		Rules.QuietEndHour = 7;
		return Rules;
	}

	/** 엔진 알림 서비스 대신 — 지운 횟수와 건 것만 적는다. */
	class FFakeSink : public SnareLocalNotify::ISink
	{
	public:
		int32 Clears = 0;
		TArray<SnareLocalNotify::FPlanned> Scheduled;

		virtual void ClearAll() override
		{
			++Clears;
			Scheduled.Reset();
		}

		virtual void Schedule(const SnareLocalNotify::FPlanned& Planned) override { Scheduled.Add(Planned); }
	};

	FRanchSlotState AnimalSlot(const FDateTime& Since)
	{
		FRanchSlotState Slot;
		Slot.Kind = ERanchSlotKind::AnimalProduct;
		Slot.AnimalID = TEXT("animal.test");
		Slot.OccupiedSinceUtc = Since;
		return Slot;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenLocalNotifyDesignTest, "Snarehaven.LocalNotify.Rules.MatchDesign",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenLocalNotifyDesignTest::RunTest(const FString& Parameters)
{
	using namespace SnarehavenLocalNotifyTest;
	FString Design;
	if (!TestTrue(TEXT("DESIGN.md 읽기"),
		FFileHelper::LoadFileToString(Design, *FPaths::Combine(FPaths::ProjectDir(), TEXT("docs/DESIGN.md")))))
	{
		return false;
	}
	const SnareLocalNotify::FRules Rules;
	TestEqual(TEXT("목장 가득 분"), Rules.RanchFullMinutes,
		DesignNumber(Design, TEXT("\\| 목장 가득 알림 \\| 마지막 수확 \\+ \\*\\*(\\d+)분")));
	TestEqual(TEXT("유지 부족 시간"), Rules.UpkeepWarnHours,
		DesignNumber(Design, TEXT("\\| 유지 부족 알림 \\| 유지 끝 \\*\\*(\\d+)시간")));
	TestEqual(TEXT("하루 상한"), Rules.DailyCap,
		DesignNumber(Design, TEXT("\\| 하루 상한 \\|[^|]*\\*\\*(\\d+)통")));
	TestEqual(TEXT("밤 금지 시작"), Rules.QuietStartHour,
		DesignNumber(Design, TEXT("\\| 밤 금지 \\| 현지 \\*\\*(\\d+)시")));
	TestEqual(TEXT("밤 금지 끝"), Rules.QuietEndHour,
		DesignNumber(Design, TEXT("\\| 밤 금지 \\| 현지 \\*\\*\\d+시~다음 날 (\\d+)시")));
	TestTrue(TEXT("기본 켬 (표 「설정」 줄)"), Design.Contains(TEXT("「알림 켬/끔」 기본 **켬**")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenLocalNotifyPlanTest, "Snarehaven.LocalNotify.Plan.TimesQuietAndCap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenLocalNotifyPlanTest::RunTest(const FString& Parameters)
{
	using namespace SnareLocalNotify;
	const FRules Rules = SnarehavenLocalNotifyTest::TestRules();
	const FTimespan Kst = FTimespan::FromHours(9);
	const TArray<FDateTime> None;
	TArray<FPlanned> Out;

	// ① 낮 — 수확 + 100분, 유지 끝 − 3시간 그대로 (현지 = UTC + 9). 지금 UTC 01:00 = 현지 10:00.
	const FDateTime Now(2026, 10, 3, 1, 0, 0);
	const FDateTime Harvest(2026, 10, 3, 0, 30, 0);
	const FDateTime UpkeepEnd(2026, 10, 3, 9, 0, 0);
	Plan(Now, Kst, &Harvest, &UpkeepEnd, None, Rules, true, Out);
	if (TestEqual(TEXT("① 둘 다 건다"), Out.Num(), 2))
	{
		TestTrue(TEXT("① 첫째 = 목장 가득"), Out[0].Kind == EKind::RanchFull);
		TestEqual(TEXT("① 목장 가득 현지 11:10"), Out[0].FireLocal, FDateTime(2026, 10, 3, 11, 10, 0));
		TestTrue(TEXT("① 둘째 = 유지 부족"), Out[1].Kind == EKind::UpkeepLow);
		TestEqual(TEXT("① 유지 부족 현지 15:00"), Out[1].FireLocal, FDateTime(2026, 10, 3, 15, 0, 0));
	}

	// ② 밤 밀기 — 현지 23:30 가득 → 다음 날 07:00 · 현지 03:00 → 같은 날 07:00 · 경계 21:00 은 금지, 20:59 는 허용.
	TestEqual(TEXT("② 23:30 → 다음 날 07:00"), PushOutOfQuietHours(FDateTime(2026, 10, 3, 23, 30, 0), Rules),
		FDateTime(2026, 10, 4, 7, 0, 0));
	TestEqual(TEXT("② 03:00 → 07:00"), PushOutOfQuietHours(FDateTime(2026, 10, 4, 3, 0, 0), Rules),
		FDateTime(2026, 10, 4, 7, 0, 0));
	TestEqual(TEXT("② 21:00 → 다음 날 07:00"), PushOutOfQuietHours(FDateTime(2026, 10, 3, 21, 0, 0), Rules),
		FDateTime(2026, 10, 4, 7, 0, 0));
	TestEqual(TEXT("② 20:59 그대로"), PushOutOfQuietHours(FDateTime(2026, 10, 3, 20, 59, 0), Rules),
		FDateTime(2026, 10, 3, 20, 59, 0));
	TestEqual(TEXT("② 07:00 그대로"), PushOutOfQuietHours(FDateTime(2026, 10, 4, 7, 0, 0), Rules),
		FDateTime(2026, 10, 4, 7, 0, 0));
	const FDateTime LateHarvest(2026, 10, 3, 12, 50, 0); // 현지 21:50 → 가득 23:30
	Plan(FDateTime(2026, 10, 3, 13, 0, 0), Kst, &LateHarvest, nullptr, None, Rules, true, Out);
	if (TestEqual(TEXT("② 밤 가득 하나"), Out.Num(), 1))
	{
		TestEqual(TEXT("② 밤 가득 → 다음 날 07:00"), Out[0].FireLocal, FDateTime(2026, 10, 4, 7, 0, 0));
	}

	// ③ 하루 상한 2 — 그날 이미 둘이면 다음 날 07:00 으로.
	const TArray<FDateTime> TwoToday = {FDateTime(2026, 10, 3, 9, 0, 0), FDateTime(2026, 10, 3, 10, 0, 0)};
	Plan(Now, Kst, &Harvest, nullptr, TwoToday, Rules, true, Out);
	if (TestEqual(TEXT("③ 상한 찬 날 하나"), Out.Num(), 1))
	{
		TestEqual(TEXT("③ 다음 날 07:00"), Out[0].FireLocal, FDateTime(2026, 10, 4, 7, 0, 0));
	}
	// 하나만 차 있으면 첫째는 오늘, 둘째는 다음 날.
	const TArray<FDateTime> OneToday = {FDateTime(2026, 10, 3, 9, 0, 0)};
	const FDateTime FarUpkeepEnd(2026, 10, 5, 0, 0, 0);
	Plan(Now, Kst, &Harvest, &FarUpkeepEnd, OneToday, Rules, true, Out);
	if (TestEqual(TEXT("③ 하나 차 있음 → 둘 다 건다"), Out.Num(), 2))
	{
		TestEqual(TEXT("③ 첫째 오늘 11:10"), Out[0].FireLocal, FDateTime(2026, 10, 3, 11, 10, 0));
		TestEqual(TEXT("③ 둘째 유지 현지 06:00 → 밤 밀기 07:00"), Out[1].FireLocal, FDateTime(2026, 10, 5, 7, 0, 0));
	}
	// 같은 날 둘이 겹치면 셋째 날이 아니라 그 다음 날로, 하루 2통 넘는 날이 없다.
	const TArray<FDateTime> TwoTomorrow = {FDateTime(2026, 10, 4, 8, 0, 0), FDateTime(2026, 10, 4, 9, 0, 0)};
	Plan(FDateTime(2026, 10, 3, 13, 0, 0), Kst, &LateHarvest, nullptr, TwoTomorrow, Rules, true, Out);
	if (TestEqual(TEXT("③ 이틀째도 찼음 하나"), Out.Num(), 1))
	{
		TestEqual(TEXT("③ 사흘째 07:00"), Out[0].FireLocal, FDateTime(2026, 10, 5, 7, 0, 0));
	}

	// ④ 지난 시각·끊긴 뒤는 안 건다.
	const FDateTime OldHarvest(2026, 10, 2, 20, 0, 0);
	const FDateTime SoonUpkeepEnd(2026, 10, 3, 3, 0, 0); // 경고 시각 00:00 < 지금
	Plan(Now, Kst, &OldHarvest, &SoonUpkeepEnd, None, Rules, true, Out);
	TestEqual(TEXT("④ 지난 시각 0개"), Out.Num(), 0);
	// 경고 현지 22:00(밤) → 07:00 로 밀리면 유지 끝(현지 01:00)을 넘으니 뺀다.
	const FDateTime NightUpkeepEnd(2026, 10, 3, 16, 0, 0);
	Plan(Now, Kst, nullptr, &NightUpkeepEnd, None, Rules, true, Out);
	TestEqual(TEXT("④ 유지 끝 넘는 유지 알림 0개"), Out.Num(), 0);

	// ⑤ 꺼짐 → 0개, 서버 값 모름 → 0개.
	Plan(Now, Kst, &Harvest, &UpkeepEnd, None, Rules, false, Out);
	TestEqual(TEXT("⑤ 꺼짐 0개"), Out.Num(), 0);
	Plan(Now, Kst, nullptr, nullptr, None, Rules, true, Out);
	TestEqual(TEXT("⑤ 모름 0개"), Out.Num(), 0);

	// ⑥ 켬/끔 저장 되돌리기.
	const bool bWas = IsEnabled();
	SetEnabled(!bWas);
	TestEqual(TEXT("⑥ 뒤집힘"), IsEnabled(), !bWas);
	SetEnabled(bWas);
	TestEqual(TEXT("⑥ 되돌림"), IsEnabled(), bWas);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenLocalNotifyRanchTest, "Snarehaven.LocalNotify.Reschedule.FromRanchSnapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenLocalNotifyRanchTest::RunTest(const FString& Parameters)
{
	using namespace SnareLocalNotify;
	using namespace SnarehavenLocalNotifyTest;
	const FRules Rules = TestRules();
	const FTimespan Kst = FTimespan::FromHours(9);
	const FDateTime Now(2026, 10, 3, 1, 0, 0); // 현지 10:00

	// ① 스냅샷 읽기 — 펫 있는 동물 칸 중 가장 이른 시계, 빈 칸·작물·시계 없는 칸은 안 본다. 유지 끝 = 지금 + 남은 시간.
	TArray<FRanchSlotState> Slots;
	Slots.Add(AnimalSlot(FDateTime(2026, 10, 3, 0, 40, 0)));
	Slots.Add(AnimalSlot(FDateTime(2026, 10, 3, 0, 20, 0)));
	Slots.AddDefaulted(); // 빈 칸
	FRanchSlotState Crop;
	Crop.Kind = ERanchSlotKind::Crop;
	Crop.CropItemID = TEXT("item.test_crop");
	Crop.OccupiedSinceUtc = FDateTime(2026, 10, 2, 0, 0, 0);
	Slots.Add(Crop);
	Slots.Add(AnimalSlot(FDateTime::MinValue()));
	FRanchUpkeepState Upkeep;
	Upkeep.bReceived = true;
	Upkeep.LeftHours = 10;
	TOptional<FDateTime> Harvest;
	TOptional<FDateTime> UpkeepEnd;
	ReadRanchTimes(Slots, Upkeep, Now, Harvest, UpkeepEnd);
	TestTrue(TEXT("① 수확 기준 있음"), Harvest.IsSet());
	TestEqual(TEXT("① 가장 이른 동물 칸 00:20"), Harvest.Get(FDateTime()), FDateTime(2026, 10, 3, 0, 20, 0));
	TestEqual(TEXT("① 유지 끝 = 지금 + 10시간"), UpkeepEnd.Get(FDateTime()), FDateTime(2026, 10, 3, 11, 0, 0));

	// 잠김 → 둘 다 없음 · 유지 시작 전 → 유지 끝만 없음 · 받기 전 → 둘 다 없음.
	FRanchUpkeepState Locked = Upkeep;
	Locked.bLocked = true;
	ReadRanchTimes(Slots, Locked, Now, Harvest, UpkeepEnd);
	TestTrue(TEXT("① 잠김 → 없음"), !Harvest.IsSet() && !UpkeepEnd.IsSet());
	FRanchUpkeepState NotStarted = Upkeep;
	NotStarted.LeftHours = -1;
	ReadRanchTimes(Slots, NotStarted, Now, Harvest, UpkeepEnd);
	TestTrue(TEXT("① 유지 시작 전 → 수확만"), Harvest.IsSet() && !UpkeepEnd.IsSet());
	ReadRanchTimes(Slots, FRanchUpkeepState(), Now, Harvest, UpkeepEnd);
	TestTrue(TEXT("① 받기 전 → 없음"), !Harvest.IsSet() && !UpkeepEnd.IsSet());

	// ② 다시 걸기 — 매번 지우고 건다, 건 시각을 기록한다.
	FFakeSink Sink;
	TArray<FDateTime> Sent;
	const TOptional<FDateTime> H(FDateTime(2026, 10, 3, 0, 30, 0)); // 가득 현지 11:10
	const TOptional<FDateTime> U(FDateTime(2026, 10, 3, 9, 0, 0)); // 경고 현지 15:00
	TestEqual(TEXT("② 둘 건다"), Reschedule(Sink, Now, Kst, H, U, Rules, true, Sent), 2);
	TestEqual(TEXT("② 지우기 1번"), Sink.Clears, 1);
	TestEqual(TEXT("② 기록 2개"), Sent.Num(), 2);
	// 같은 스냅샷으로 또 불러도 쌓이지 않는다 — 아직 안 울린 기록은 하루 상한에 안 센다.
	TestEqual(TEXT("② 다시 불러도 둘"), Reschedule(Sink, Now, Kst, H, U, Rules, true, Sent), 2);
	TestEqual(TEXT("② 걸린 것 둘"), Sink.Scheduled.Num(), 2);
	TestEqual(TEXT("② 기록 그대로 2개"), Sent.Num(), 2);

	// ③ 오늘 이미 둘 울렸으면(현지 16:00 에 다시 걸기) 새 알림은 다음 날 아침으로.
	const FDateTime Later(2026, 10, 3, 7, 0, 0); // 현지 16:00 — 11:10·15:00 둘 다 울림
	const TOptional<FDateTime> H2(FDateTime(2026, 10, 3, 6, 50, 0)); // 가득 현지 17:30
	TestEqual(TEXT("③ 하나 건다"), Reschedule(Sink, Later, Kst, H2, TOptional<FDateTime>(), Rules, true, Sent), 1);
	if (TestEqual(TEXT("③ 걸린 것 하나"), Sink.Scheduled.Num(), 1))
	{
		TestEqual(TEXT("③ 다음 날 07:00"), Sink.Scheduled[0].FireLocal, FDateTime(2026, 10, 4, 7, 0, 0));
	}
	TestEqual(TEXT("③ 기록 = 울린 둘 + 새 하나"), Sent.Num(), 3);

	// ④ 끄면 지우기만 하고 하나도 안 건다. 어제 울린 기록은 버린다.
	const FDateTime NextDay(2026, 10, 4, 3, 0, 0);
	const int32 ClearsBefore = Sink.Clears;
	TestEqual(TEXT("④ 꺼짐 0개"), Reschedule(Sink, NextDay, Kst, H2, U, Rules, false, Sent), 0);
	TestEqual(TEXT("④ 지우기 한 번 더"), Sink.Clears, ClearsBefore + 1);
	TestEqual(TEXT("④ 걸린 것 0"), Sink.Scheduled.Num(), 0);
	TestEqual(TEXT("④ 기록 = 오늘 07:00 울린 하나"), Sent.Num(), 1);
	return true;
}

#endif
