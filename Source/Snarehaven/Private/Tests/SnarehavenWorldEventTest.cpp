// ROADMAP M12-07 2층 검사 — 랜덤 월드 이벤트(한정시간 위험구역, DESIGN 8.1b).
// 보는 것 셋:
//  ① 표의 이벤트 행이 **DESIGN 8.1b 와 같다** (숫자는 여기 없고 문서에서 읽는다).
//  ② 등장 주기가 **표 값에서 온다** — 진짜 표 행과 검사 전용 행 둘 다 타이머 간격이 행 값과 같다.
//  ③ 수명이 지나면 **사라진다** — 타이머를 직접 돌려, 수명 직전엔 남고 지나면 구역·서 있는 동물이 없어진다.
//     수명 0(상시 8.1 구역)은 수명 타이머가 걸리지 않는다.
//  ④ 미니맵 표식이 **이벤트 위치 → 지도 좌표 변환**(`WorldToMap`)과 같은 자리에 서고, 남은 시간 줄이
//     구역의 서버 시계 값이며, 구역이 사라지면 둘 다 접힌다. 상시 구역은 표식이 안 뜬다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.WorldEvent; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Animals/AnimalCharacter.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Data/DangerZoneRow.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UI/SnareMinimapHud.h"
#include "World/DangerZoneActor.h"
#include "World/SnareZoneRules.h"

namespace
{
	const TCHAR* DangerTablePath = TEXT("/Game/Game/Data/World/DT_DangerZoneTable.DT_DangerZoneTable");

	// DESIGN 8.1b 표의 왼쪽 칸 그대로 — 이름뿐이고 값은 문서에서 읽는다.
	const TCHAR* RadiusLabel = TEXT("이벤트 구역 크기 (반지름)");
	const TCHAR* CountLabel = TEXT("이벤트 동물 수");
	const TCHAR* RespawnLabel = TEXT("이벤트 재생성 간격");
	const TCHAR* LifetimeLabel = TEXT("이벤트 수명");
	const TCHAR* IntervalLabel = TEXT("이벤트 등장 주기");

	// 검사 전용 값 (Verification.md 6 — 설계 숫자가 아니다). 표의 600·1800 과 **달라야**
	// "코드가 아니라 행을 따른다" 를 증명한다.
	constexpr int32 TestLifetimeSeconds = 7;
	constexpr int32 TestIntervalSeconds = 77;
	constexpr int32 TestAnimalCount = 3;
	constexpr float TestRadiusCm = 400.f;
	constexpr int32 TestRespawnSeconds = 60;
	constexpr float TestTimerSlackSeconds = 0.01f;
	// 이벤트를 칸 가운데서 비껴 세우는 거리(cm) — 가운데면 축이 바뀐 변환도 우연히 맞는다.
	const FVector2D TestEventOffsetCm(1234.0, -567.0);

	FString ExtractSection(const FString& Doc, const FString& HeadingPrefix)
	{
		const int32 Start = Doc.Find(HeadingPrefix, ESearchCase::CaseSensitive);
		if (Start == INDEX_NONE)
		{
			return FString();
		}

		const int32 BodyStart = Start + HeadingPrefix.Len();
		const int32 NextHeading = Doc.Find(TEXT("\n#"), ESearchCase::CaseSensitive, ESearchDir::FromStart, BodyStart);
		return NextHeading == INDEX_NONE ? Doc.Mid(BodyStart) : Doc.Mid(BodyStart, NextHeading - BodyStart);
	}

	TMap<FString, double> ParseLabelledNumbers(const FString& Section)
	{
		TArray<FString> Lines;
		Section.ParseIntoArrayLines(Lines);

		const FRegexPattern RowPattern(TEXT("^\\|\\s*([^|]+?)\\s*\\|\\s*([0-9]+(?:\\.[0-9]+)?)"));
		TMap<FString, double> Values;
		for (const FString& Line : Lines)
		{
			FRegexMatcher Matcher(RowPattern, Line);
			if (Matcher.FindNext())
			{
				Values.Add(Matcher.GetCaptureGroup(1), FCString::Atod(*Matcher.GetCaptureGroup(2)));
			}
		}
		return Values;
	}

	/** 표에서 수명 있는 첫 행(= 월드 이벤트). 없으면 None. */
	FName FindEventRowName(const UDataTable* Table)
	{
		for (const FName& RowName : Table->GetRowNames())
		{
			const FDangerZoneRow* Row = Table->FindRow<FDangerZoneRow>(RowName, TEXT("WorldEventTest"));
			if (Row != nullptr && Row->LifetimeSeconds > 0)
			{
				return RowName;
			}
		}
		return NAME_None;
	}

	/** 보이는지 (슬레이트 없이 — 검사는 위젯을 화면에 안 올린다). */
	bool Shown(const UWidget* Widget)
	{
		return Widget->GetVisibility() != ESlateVisibility::Collapsed && Widget->GetVisibility() != ESlateVisibility::Hidden;
	}

	UWorld* CreateTestWorld()
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
		FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
		WorldContext.SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		return World;
	}

	void DestroyTestWorld(UWorld* World)
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	}

	/** `FTimerManager::Tick` 은 한 프레임에 한 번만 일한다 — 프레임 번호를 같이 올린다 (PlayerBodyTest 와 같은 방식). */
	void TickTimers(UWorld* World, float DeltaSeconds)
	{
		++GFrameCounter;
		World->GetTimerManager().Tick(DeltaSeconds);
	}
}

// ① 표의 이벤트 행 = DESIGN 8.1b
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenWorldEventTableTest,
	"Snarehaven.WorldEvent.Table.MatchesDesign",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenWorldEventTableTest::RunTest(const FString& Parameters)
{
	FString Doc;
	if (!TestTrue(TEXT("설계서를 읽는다"), FFileHelper::LoadFileToString(Doc, *(FPaths::ProjectDir() / TEXT("docs/DESIGN.md")))))
	{
		return false;
	}

	const TMap<FString, double> Design = ParseLabelledNumbers(ExtractSection(Doc, TEXT("### 8.1b")));
	const TCHAR* Labels[] = { RadiusLabel, CountLabel, RespawnLabel, LifetimeLabel, IntervalLabel };
	for (const TCHAR* Label : Labels)
	{
		if (!Design.Contains(Label))
		{
			AddError(FString::Printf(TEXT("DESIGN 8.1b 에서 '%s' 줄을 못 읽었다"), Label));
			return false;
		}
	}

	const UDataTable* Table = LoadObject<UDataTable>(nullptr, DangerTablePath);
	if (!TestNotNull(TEXT("DT_DangerZoneTable 로드"), Table))
	{
		return false;
	}

	const FName EventRowName = FindEventRowName(Table);
	if (!TestFalse(TEXT("수명 있는 이벤트 행이 표에 있다"), EventRowName.IsNone()))
	{
		return false;
	}

	const FDangerZoneRow& Row = *Table->FindRow<FDangerZoneRow>(EventRowName, TEXT("WorldEventTest"));
	AddInfo(FString::Printf(TEXT("%s: 반지름 %.0f · %d마리 · 재생성 %d초 · 수명 %d초 · 주기 %d초"),
		*EventRowName.ToString(), Row.RadiusCm, Row.AnimalCount, Row.RespawnSeconds, Row.LifetimeSeconds,
		Row.AppearIntervalSeconds));

	TestTrue(TEXT("반지름 = DESIGN 8.1b"), FMath::IsNearlyEqual(static_cast<double>(Row.RadiusCm), Design[RadiusLabel]));
	TestEqual(TEXT("동물 수 = DESIGN 8.1b"), Row.AnimalCount, static_cast<int32>(FMath::RoundToInt(Design[CountLabel])));
	TestEqual(TEXT("재생성 간격 = DESIGN 8.1b"), Row.RespawnSeconds, static_cast<int32>(FMath::RoundToInt(Design[RespawnLabel])));
	TestEqual(TEXT("수명 = DESIGN 8.1b"), Row.LifetimeSeconds, static_cast<int32>(FMath::RoundToInt(Design[LifetimeLabel])));
	TestEqual(TEXT("등장 주기 = DESIGN 8.1b"), Row.AppearIntervalSeconds, static_cast<int32>(FMath::RoundToInt(Design[IntervalLabel])));
	TestTrue(TEXT("수명 < 등장 주기 (이벤트가 겹치지 않는다)"), Row.LifetimeSeconds < Row.AppearIntervalSeconds);
	return true;
}

// ② 등장 주기 = 행 값 (진짜 표 행 · 검사 전용 행 둘 다). 상시 구역 행에는 주기가 안 걸린다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenWorldEventIntervalTest,
	"Snarehaven.WorldEvent.Schedule.IntervalFromTable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenWorldEventIntervalTest::RunTest(const FString& Parameters)
{
	UDataTable* Table = LoadObject<UDataTable>(nullptr, DangerTablePath);
	if (!TestNotNull(TEXT("DT_DangerZoneTable 로드"), Table))
	{
		return false;
	}

	const FName EventRowName = FindEventRowName(Table);
	if (!TestFalse(TEXT("이벤트 행이 표에 있다"), EventRowName.IsNone()))
	{
		return false;
	}
	const FDangerZoneRow EventRow = *Table->FindRow<FDangerZoneRow>(EventRowName, TEXT("WorldEventTest"));

	// 검사 전용 표: 이벤트 행을 복사해 주기·수명만 다른 값으로, 그리고 상시 구역 행(수명 0) 하나.
	UDataTable* TestTable = NewObject<UDataTable>();
	TestTable->RowStruct = FDangerZoneRow::StaticStruct();
	FDangerZoneRow TestEventRow = EventRow;
	TestEventRow.AppearIntervalSeconds = TestIntervalSeconds;
	TestEventRow.LifetimeSeconds = TestLifetimeSeconds;
	TestTable->AddRow(TEXT("Event_Test"), TestEventRow);
	FDangerZoneRow PermanentRow = EventRow;
	PermanentRow.LifetimeSeconds = 0;
	PermanentRow.AppearIntervalSeconds = 0;
	TestTable->AddRow(TEXT("Danger_Test"), PermanentRow);

	UWorld* World = CreateTestWorld();
	FTimerManager& Timers = World->GetTimerManager();

	const FTimerHandle RealHandle = ADangerZoneActor::ScheduleWorldEvent(World, Table, EventRowName);
	TestTrue(TEXT("진짜 이벤트 행에 등장 타이머가 걸린다"), RealHandle.IsValid());
	TestTrue(TEXT("등장 주기 = 표의 AppearIntervalSeconds"),
		FMath::IsNearlyEqual(Timers.GetTimerRate(RealHandle), static_cast<float>(EventRow.AppearIntervalSeconds)));

	const FTimerHandle TestHandle = ADangerZoneActor::ScheduleWorldEvent(World, TestTable, TEXT("Event_Test"));
	TestTrue(TEXT("검사 행의 주기(77초)를 그대로 따른다 - 코드에 박힌 주기가 없다"),
		FMath::IsNearlyEqual(Timers.GetTimerRate(TestHandle), static_cast<float>(TestIntervalSeconds)));

	TestFalse(TEXT("상시 구역 행(수명 0)에는 등장 타이머가 안 걸린다"),
		ADangerZoneActor::ScheduleWorldEvent(World, TestTable, TEXT("Danger_Test")).IsValid());

	DestroyTestWorld(World);
	return true;
}

// ③ 수명이 지나면 구역과 서 있는 동물이 사라진다. 수명 0 구역은 수명 타이머가 없다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenWorldEventLifetimeTest,
	"Snarehaven.WorldEvent.Lifetime.ExpiresAfterRowSeconds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenWorldEventLifetimeTest::RunTest(const FString& Parameters)
{
	const UDataTable* Table = LoadObject<UDataTable>(nullptr, DangerTablePath);
	const FName EventRowName = IsValid(Table) ? FindEventRowName(Table) : NAME_None;
	if (EventRowName.IsNone())
	{
		AddError(TEXT("DT_DangerZoneTable 에서 이벤트 행을 못 읽었다"));
		return false;
	}

	// 종은 표 행의 것(세울 수 있는 종), 숫자는 검사 전용 값.
	FDangerZoneRow Row;
	Row.ZoneID = FName(TEXT("zone.hunt"));
	Row.AnimalRowName = Table->FindRow<FDangerZoneRow>(EventRowName, TEXT("WorldEventTest"))->AnimalRowName;
	Row.RadiusCm = TestRadiusCm;
	Row.AnimalCount = TestAnimalCount;
	Row.RespawnSeconds = TestRespawnSeconds;
	Row.LifetimeSeconds = TestLifetimeSeconds;

	UWorld* World = CreateTestWorld();
	ADangerZoneActor* Zone = World->SpawnActor<ADangerZoneActor>(FVector::ZeroVector, FRotator::ZeroRotator);
	ADangerZoneActor* Permanent = World->SpawnActor<ADangerZoneActor>(FVector::ZeroVector, FRotator::ZeroRotator);
	if (Zone == nullptr || Permanent == nullptr || !Zone->SetDangerZoneRow(Row))
	{
		AddError(TEXT("이벤트 구역을 못 세웠다"));
		DestroyTestWorld(World);
		return false;
	}

	FDangerZoneRow PermanentRow = Row;
	PermanentRow.LifetimeSeconds = 0;
	Permanent->SetDangerZoneRow(PermanentRow);
	TestFalse(TEXT("수명 0(상시 8.1 구역)은 수명 타이머가 안 걸린다"), Permanent->StartLifetime());

	TestEqual(TEXT("정원만큼 선다"), Zone->RefillZone(), TestAnimalCount);
	TArray<TWeakObjectPtr<AAnimalCharacter>> Animals;
	for (TActorIterator<AAnimalCharacter> It(World); It; ++It)
	{
		Animals.Add(*It);
	}

	TestTrue(TEXT("수명 타이머가 걸린다"), Zone->StartLifetime());
	TestTrue(TEXT("남은 수명 = 행의 LifetimeSeconds"),
		FMath::IsNearlyEqual(Zone->GetLifetimeRemaining(), static_cast<float>(TestLifetimeSeconds)));

	// 이번 프레임에 건 타이머는 "대기" 로 들어가 **다음 틱 끝에** 시계가 붙는다 (`FTimerManager::InternalSetTimer`).
	// 0초 틱으로 먼저 붙여야 아래 두 틱이 수명을 그대로 잰다.
	TickTimers(World, 0.f);

	// 수명 직전 — 아직 있다.
	TickTimers(World, static_cast<float>(TestLifetimeSeconds) - TestTimerSlackSeconds);
	TestFalse(TEXT("수명 직전엔 구역이 남아 있다"), Zone->IsActorBeingDestroyed());

	// 수명 지남 — 구역과 서 있던 동물이 없어진다.
	TickTimers(World, TestTimerSlackSeconds * 2.f);
	TestTrue(TEXT("수명이 지나면 구역이 사라진다"), !IsValid(Zone) || Zone->IsActorBeingDestroyed());
	int32 Remaining = 0;
	for (const TWeakObjectPtr<AAnimalCharacter>& Animal : Animals)
	{
		if (Animal.IsValid() && !Animal->IsActorBeingDestroyed())
		{
			++Remaining;
		}
	}
	TestEqual(TEXT("서 있던 동물도 함께 사라진다"), Remaining, 0);
	TestFalse(TEXT("상시 구역은 그대로다"), Permanent->IsActorBeingDestroyed());

	DestroyTestWorld(World);
	return true;
}

// ④ 미니맵 표식 = 이벤트 위치의 지도 좌표. 남은 시간 = 서버 시계 값. 사라지면 표식도 접힌다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenWorldEventMinimapTest,
	"Snarehaven.WorldEvent.Minimap.MarkerAtEventLocation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenWorldEventMinimapTest::RunTest(const FString& Parameters)
{
	const UDataTable* Table = LoadObject<UDataTable>(nullptr, DangerTablePath);
	const UDataTable* ZoneTable = LoadObject<UDataTable>(nullptr, SnareZones::TablePath);
	const FName EventRowName = IsValid(Table) ? FindEventRowName(Table) : NAME_None;
	const TArray<const FZoneRow*> Regions = SnareMinimapHud::GetRegionRows(ZoneTable, TEXT("L_Hunt"));
	if (EventRowName.IsNone() || Regions.Num() == 0)
	{
		AddError(TEXT("이벤트 행이나 L_Hunt 칸을 못 읽었다"));
		return false;
	}

	// HUD 와 같은 함수로 지도를 세운다.
	UWidgetTree* Tree = NewObject<UWidgetTree>(GetTransientPackage());
	UCanvasPanel* Root = Tree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("TestRoot"));
	Tree->RootWidget = Root;
	const SnareMinimapHud::FPlaceholder Map = SnareMinimapHud::AddPlaceholder(*Tree, *Root, Regions, FVector2D::ZeroVector);
	if (!TestNotNull(TEXT("이벤트 표식이 있다"), Map.EventDot) || !TestNotNull(TEXT("남은 시간 줄이 있다"), Map.EventText))
	{
		return false;
	}
	TestFalse(TEXT("이벤트가 없으면 표식이 접혀 있다"), Shown(Map.EventDot));

	FDangerZoneRow Row = *Table->FindRow<FDangerZoneRow>(EventRowName, TEXT("WorldEventTest"));
	Row.LifetimeSeconds = TestLifetimeSeconds;
	FDangerZoneRow PermanentRow = Row;
	PermanentRow.LifetimeSeconds = 0;

	UWorld* World = CreateTestWorld();
	const FVector EventLocation(Regions[0]->RegionCenterCm + TestEventOffsetCm, 0.0);
	ADangerZoneActor* Permanent = World->SpawnActor<ADangerZoneActor>(FVector::ZeroVector, FRotator::ZeroRotator);
	Permanent->SetDangerZoneRow(PermanentRow);
	TestNull(TEXT("상시 구역만 있으면 이벤트가 없다"), ADangerZoneActor::FindWorldEvent(World));

	ADangerZoneActor* Zone = World->SpawnActor<ADangerZoneActor>(EventLocation, FRotator::ZeroRotator);
	Zone->SetDangerZoneRow(Row);
	Zone->StartLifetime();
	const ADangerZoneActor* Found = ADangerZoneActor::FindWorldEvent(World);
	TestTrue(TEXT("떠 있는 이벤트 구역을 찾는다"), Found == Zone);

	SnareMinimapHud::UpdateEventMarker(Map.EventDot, Map.EventText, Map.Bounds, Found);
	TestTrue(TEXT("이벤트가 뜨면 표식이 보인다"), Shown(Map.EventDot));
	TestTrue(TEXT("이벤트가 뜨면 남은 시간 줄이 보인다"), Shown(Map.EventText));

	const UCanvasPanelSlot* DotSlot = Cast<UCanvasPanelSlot>(Map.EventDot->Slot);
	const FVector2D DotCenter = DotSlot->GetPosition() + DotSlot->GetSize() * 0.5f - FVector2D(SnareMinimapHud::FrameBorder);
	const FVector2D Want = SnareMinimapHud::WorldToMap(Map.Bounds, EventLocation);
	AddInfo(FString::Printf(TEXT("이벤트 %s → 표식 가운데 %s / 변환 %s"), *EventLocation.ToString(), *DotCenter.ToString(), *Want.ToString()));
	TestTrue(TEXT("표식 가운데 = WorldToMap(이벤트 위치)"), DotCenter.Equals(Want, 0.01));
	TestTrue(TEXT("표식 크기 = EventDotSize"), DotSlot->GetSize().Equals(FVector2D(SnareMinimapHud::EventDotSize), 0.01));
	// 역검증: 축을 바꾼 자리와는 달라야 한다 (가운데가 아닌 곳에 세웠으니 우연히 같을 수 없다).
	TestFalse(TEXT("축을 바꾼 자리와는 다르다"), DotCenter.Equals(FVector2D(Want.Y, Want.X), 0.01));

	TestEqual(TEXT("남은 시간 줄 = 서버 시계 남은 수명"), Map.EventText->GetText().ToString(),
		SnareMinimapHud::FormatEventRemaining(Zone->GetLifetimeRemaining()).ToString());
	TestTrue(TEXT("남은 수명 = 행의 수명 (시계가 안 흘렀다)"),
		FMath::IsNearlyEqual(Zone->GetLifetimeRemaining(), static_cast<float>(TestLifetimeSeconds)));
	TestEqual(TEXT("남은 시간 글자 꼴"), SnareMinimapHud::FormatEventRemaining(601.5f).ToString(), FString(TEXT("추락 지점 10:02")));

	// 수명 끝 → 구역이 사라지면 표식·줄이 접힌다.
	Zone->ExpireZone();
	TestNull(TEXT("사라진 구역은 이벤트로 안 잡힌다"), ADangerZoneActor::FindWorldEvent(World));
	SnareMinimapHud::UpdateEventMarker(Map.EventDot, Map.EventText, Map.Bounds, ADangerZoneActor::FindWorldEvent(World));
	TestFalse(TEXT("사라지면 표식이 접힌다"), Shown(Map.EventDot));
	TestFalse(TEXT("사라지면 남은 시간 줄이 접힌다"), Shown(Map.EventText));

	DestroyTestWorld(World);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
