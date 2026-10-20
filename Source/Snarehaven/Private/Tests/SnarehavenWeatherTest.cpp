// 날씨 2층 검사 — 같은 시각 = 같은 날씨, 슬롯 1000개 분포가 DESIGN 3.3 「날씨」 표(맑음 60·비 25·눈 15%) ±5%p.
// 실행: UnrealEditor-Cmd <uproject 절대경로> -ExecCmds="Automation RunTests Snarehaven.World.Weather; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "World/SnareWeather.h"
#include "World/SnareWeatherView.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenWeatherTest, "Snarehaven.World.Weather",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenWeatherTest::RunTest(const FString& Parameters)
{
	using namespace SnareWeather;
	// 테스트 전용 값 (설계 숫자가 아니다).
	constexpr int32 SlotCount = 1000;
	constexpr double TolerancePercent = 5.0;

	TestEqual(TEXT("한 칸 10분"), SlotSeconds, 600.0);
	TestTrue(TEXT("같은 시각은 같은 날씨"), At(1234.5) == At(1234.5));
	TestTrue(TEXT("같은 칸 안(0~599초)은 같은 날씨"), At(0.0) == At(599.9));
	TestTrue(TEXT("칸 번호가 같으면 시각과 무관"), ForSlot(7) == At(7 * SlotSeconds + 300.0));
	TestTrue(TEXT("음수 시각도 안전(-1초 = 칸 -1)"), At(-1.0) == ForSlot(-1));

	int32 Counts[3] = {0, 0, 0};
	int32 Changes = 0;
	for (int32 Slot = 0; Slot < SlotCount; ++Slot)
	{
		++Counts[static_cast<int32>(ForSlot(Slot))];
		if (Slot > 0 && ForSlot(Slot) != ForSlot(Slot - 1)) ++Changes;
	}
	const double Expected[3] = {60.0, 25.0, 15.0};
	for (int32 I = 0; I < 3; ++I)
	{
		const double Percent = Counts[I] * 100.0 / SlotCount;
		AddInfo(FString::Printf(TEXT("날씨 %d: %.1f%% (표 %.0f%%)"), I, Percent, Expected[I]));
		TestTrue(*FString::Printf(TEXT("날씨 %d 분포가 표 ±5%%p"), I), FMath::Abs(Percent - Expected[I]) <= TolerancePercent);
	}
	TestTrue(TEXT("날씨가 실제로 바뀐다(칸 1000개에 100번 넘게)"), Changes > 100);

	// 추위 가산 (M104-03c): 설원 밖 눈 = 0.5 × 정도, 설원 안·지붕 아래·눈 없음 = 그대로.
	TestEqual(TEXT("설원 밖 눈 1 = 가산"), ColdDrain(1.f, false, 0.f), SnowColdPerSecond);
	TestEqual(TEXT("눈 정도 비례"), ColdDrain(0.5f, false, 0.f), SnowColdPerSecond * 0.5f);
	TestEqual(TEXT("눈 없음 0"), ColdDrain(0.f, false, 0.f), 0.f);
	TestEqual(TEXT("지붕 아래 0"), ColdDrain(1.f, true, 0.f), 0.f);
	TestEqual(TEXT("설원 안은 칸 피해 그대로(+0)"), ColdDrain(1.f, false, 7.f), 7.f);

	// 섞기 (M104-03): 칸이 바뀐 직후엔 앞 칸 날씨가 남고, BlendSeconds 뒤엔 새 날씨만. 비·눈 합은 1 을 넘지 않는다.
	int32 Boundary = -1;
	for (int32 Slot = 1; Slot < SlotCount && Boundary < 0; ++Slot)
	{
		if (ForSlot(Slot) == ESnareWeather::Rain && ForSlot(Slot - 1) == ESnareWeather::Clear) Boundary = Slot;
	}
	if (TestTrue(TEXT("맑음→비 경계를 찾음"), Boundary > 0))
	{
		float Rain = 0.f, Snow = 0.f;
		Amounts(Boundary * SlotSeconds, Rain, Snow);
		TestEqual(TEXT("경계 순간 비 0"), Rain, 0.f);
		Amounts(Boundary * SlotSeconds + BlendSeconds * 0.5, Rain, Snow);
		TestTrue(TEXT("섞는 중간 비 0~1 사이"), Rain > 0.05f && Rain < 0.95f);
		Amounts(Boundary * SlotSeconds + BlendSeconds + 1.0, Rain, Snow);
		TestEqual(TEXT("섞기 뒤 비 1"), Rain, 1.f);
		TestEqual(TEXT("눈 0"), Snow, 0.f);
	}
	for (int32 Slot = 1; Slot < 200; ++Slot)
	{
		float Rain = 0.f, Snow = 0.f;
		Amounts(Slot * SlotSeconds + 7.0, Rain, Snow);
		TestTrue(TEXT("비·눈 정도는 0~1"), Rain >= 0.f && Rain <= 1.f && Snow >= 0.f && Snow <= 1.f);
	}

	// 입자 수 상한 (표 「입자 상한」 비 600 · 눈 800)
	TestEqual(TEXT("비 상한 600"), SnareWeatherView::MaxRain, 600);
	TestEqual(TEXT("눈 상한 800"), SnareWeatherView::MaxSnow, 800);
	TestEqual(TEXT("눈송이 지름 22cm"), SnareWeatherView::SnowSizeCm, 22.f);
	TestEqual(TEXT("정도 0 = 입자 0"), SnareWeatherView::ActiveCount(0.f, 600), 0);
	TestEqual(TEXT("정도 1 = 상한"), SnareWeatherView::ActiveCount(1.f, 600), 600);
	TestEqual(TEXT("정도 2 도 상한을 안 넘음"), SnareWeatherView::ActiveCount(2.f, 800), 800);
	TestTrue(TEXT("입자 난수 0~1"), SnareWeatherView::Hash01(5, 1) >= 0.f && SnareWeatherView::Hash01(5, 1) < 1.f);
	TestTrue(TEXT("입자 난수는 같은 씨앗 = 같은 값"), SnareWeatherView::Hash01(9, 2) == SnareWeatherView::Hash01(9, 2));
	return true;
}

#endif
