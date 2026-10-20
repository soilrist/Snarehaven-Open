// ROADMAP M36-03 2층 검사 — 두 HUD 가 함께 쓰는 존 이동 버튼 규칙(`SnareZoneTravelHud`)을 직접 부른다.
// `SnarehavenZoneTravelTest` 는 HUD 위젯을 거쳐 같은 길을 타지만, 이 이름공간을 바로 부르는 시험이 없었다
// (M36 머리말 탐침). 보는 것 셋:
//  ① `IsButtonVisible` — 사거리 안 **갈 곳이 확정된** 포탈만 참이다 (없음·먼 포탈·꺼진 포탈·빈 컴포넌트 = 거짓).
//  ② `FormatButtonText` — 표(`DT_ZoneTable`)의 `DisplayName` + ` 이동` 그대로. 포탈이 없거나 꺼졌으면 빈 글자.
//     섬 배는 존 표 대신 부두의 버튼 이름을 쓴다. 요금 답 전·거절 뒤에는 둘째 줄이 붙고, 성공 뒤에는 떨어진다.
//  ③ `CreateButton` — 버튼·글자 이름이 두 화면 공통 이름이고 글자가 버튼 안 최소 높이 상자에 한 개 들어 있다.
//  ④ (M38-02) 두 화면의 버튼 높이 ≥ 72(DESIGN 2 「누르는 칸 최소 크기」) · 사냥 화면 버튼이 공격·포획 버튼 칸과 겹침 0.
// 실행: UnrealEditor <uproject> -ExecCmds="Automation RunTests Snarehaven.UI.ZoneTravelHud; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Backend/SnareBackendSubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "Combat/MeleeAttackComponent.h"
#include "Components/Button.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Data/ZoneRow.h"
#include "Dom/JsonObject.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "SnarehavenCharacter.h"
#include "UI/HuntHudWidget.h"
#include "UI/RanchHudWidget.h"
#include "UI/SnareZoneTravelHud.h"
#include "World/ZonePortalActor.h"
#include "World/ZoneTravelComponent.h"

namespace
{
	const TCHAR* HudZoneTablePath = TEXT("/Game/Game/Data/World/DT_ZoneTable.DT_ZoneTable");

	// 표가 이어 둔 두 방향 (M6-01 `DT_ZoneTable`: 사냥존 ↔ 거점존). 거점 → 사냥터가 요금 구간이다(DESIGN 8.14).
	const FName HudHuntZoneID(TEXT("zone.hunt"));
	const FName HudRanchZoneID(TEXT("zone.ranch"));

	// 검사 전용 값 (Verification.md 6 — 설계 숫자가 아니다).
	constexpr float HudTestAttackDamage = 10.f;
	constexpr float HudTestAttackRangeCm = 300.f;
	constexpr float HudTestAttackIntervalSeconds = 1.f;
	constexpr float HudTestNearDistanceCm = 100.f;
	constexpr float HudTestFarDistanceCm = 1000.f;
	constexpr float HudTestAwayDistanceCm = 50000.f;
	const TCHAR* HudTestIslandName = TEXT("시험섬");

	UWorld* CreateHudTestWorld()
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
		FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
		WorldContext.SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		return World;
	}

	void DestroyHudTestWorld(UWorld* World)
	{
		// 요금 성공 뒤 적힌 갈 URL 을 지운다 — 남기면 엔진이 다음 틱에 진짜로 맵을 연다.
		USnareBackendSubsystem::TestServerOverride = nullptr;
		World->NextURL.Empty();
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	}

	ASnarehavenCharacter* SpawnHudTestPlayer(UWorld* World)
	{
		ASnarehavenCharacter* Player =
			World->SpawnActor<ASnarehavenCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
		if (Player != nullptr)
		{
			Player->GetMeleeAttack()->SetAttackSettings(
				HudTestAttackDamage, HudTestAttackRangeCm, HudTestAttackIntervalSeconds);
		}
		return Player;
	}

	// 표를 먹인 포탈 하나. 표에 없는 갈 곳을 주면 **꺼진 포탈**로 선다.
	AZonePortalActor* SpawnHudTestPortal(UWorld* World, UDataTable* Table, float DistanceCm, FName From, FName To)
	{
		AZonePortalActor* Portal =
			World->SpawnActor<AZonePortalActor>(FVector(DistanceCm, 0.f, 0.f), FRotator::ZeroRotator);
		if (Portal != nullptr)
		{
			Portal->SetZoneData(Table, From, To);
			Portal->InitializeFromTable();
		}
		return Portal;
	}

	// 표의 화면 이름을 시험이 따로 읽는다 — 버튼 글자가 표를 그대로 옮겼는지 대조하려고.
	FString ReadDisplayName(const UDataTable* Table, FName ZoneID)
	{
		for (const FName& RowName : Table->GetRowNames())
		{
			const FZoneRow* const Row = Table->FindRow<FZoneRow>(RowName, TEXT("ZoneTravelHudTest"));
			if (Row != nullptr && Row->ZoneID == ZoneID)
			{
				return Row->DisplayName.ToString();
			}
		}
		return FString();
	}

	// 게임 인스턴스 없는 검사 월드용 백엔드 (`SnarehavenZoneTravelTest` 와 같은 방식 — `Initialize` 안 부름).
	USnareBackendSubsystem* MakeHudTestBackend()
	{
		UGameInstance* const GameInstance = NewObject<UGameInstance>(GetTransientPackage());
		return NewObject<USnareBackendSubsystem>(GameInstance);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenZoneTravelHudVisibleTest,
	"Snarehaven.UI.ZoneTravelHud.VisibleOnlyForUsablePortalInRange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenZoneTravelHudVisibleTest::RunTest(const FString& Parameters)
{
	UDataTable* const ZoneTable = LoadObject<UDataTable>(nullptr, HudZoneTablePath);
	if (!TestNotNull(TEXT("DT_ZoneTable 로드"), ZoneTable))
	{
		return false;
	}
	const FString RanchName = ReadDisplayName(ZoneTable, HudRanchZoneID);
	if (!TestFalse(TEXT("표에 거점존 화면 이름이 있다"), RanchName.IsEmpty()))
	{
		return false;
	}

	// ⓪ 컴포넌트가 없으면 버튼도 글자도 없다 (HUD 가 폰을 아직 못 잡은 때).
	TestFalse(TEXT("빈 컴포넌트 = 버튼 없음"), SnareZoneTravelHud::IsButtonVisible(nullptr));
	TestTrue(TEXT("빈 컴포넌트 = 빈 글자"), SnareZoneTravelHud::FormatButtonText(nullptr).IsEmpty());

	UWorld* World = CreateHudTestWorld();
	ASnarehavenCharacter* Player = SpawnHudTestPlayer(World);
	UZoneTravelComponent* const Travel = Player != nullptr ? Player->GetZoneTravel() : nullptr;
	if (!TestNotNull(TEXT("폰에 존 이동 컴포넌트가 붙어 있다"), Travel))
	{
		DestroyHudTestWorld(World);
		return false;
	}

	// 단계마다 같은 세 가지를 본다: 버튼 = 컴포넌트 판정과 같다, 보임 여부, 글자.
	auto Expect = [this, Travel](const TCHAR* Step, bool bVisible, const FString& Text)
	{
		Travel->RefreshTarget();
		const bool bShown = SnareZoneTravelHud::IsButtonVisible(Travel);
		const FString Shown = SnareZoneTravelHud::FormatButtonText(Travel).ToString();
		AddInfo(FString::Printf(TEXT("%s: 보임 %d · 글자 [%s]"), Step, bShown ? 1 : 0, *Shown));
		TestEqual(FString::Printf(TEXT("%s: 버튼은 컴포넌트 판정 그대로"), Step), bShown, Travel->CanTravel());
		TestEqual(FString::Printf(TEXT("%s: 버튼 보임"), Step), bShown, bVisible);
		TestEqual(FString::Printf(TEXT("%s: 글자"), Step), Shown, Text);
	};

	// ① 포탈이 없다.
	Expect(TEXT("포탈 없음"), false, FString());

	// ② 갈 수 있지만 사거리 밖.
	AZonePortalActor* const FarPortal =
		SpawnHudTestPortal(World, ZoneTable, HudTestFarDistanceCm, HudHuntZoneID, HudRanchZoneID);
	TestTrue(TEXT("먼 포탈은 켜져 있다"), IsValid(FarPortal) && FarPortal->CanTravel());
	Expect(TEXT("사거리 밖"), false, FString());

	// ③ 사거리 안이지만 꺼진 포탈(표에 없는 갈 곳).
	AZonePortalActor* const DeadPortal = SpawnHudTestPortal(
		World, ZoneTable, HudTestNearDistanceCm, HudHuntZoneID, FName(TEXT("zone.no_such_zone")));
	TestTrue(TEXT("꺼진 포탈이 섰다"), IsValid(DeadPortal) && !DeadPortal->CanTravel());
	Expect(TEXT("사거리 안 꺼진 포탈"), false, FString());

	// ④ 사거리 안 켜진 포탈 — 글자는 표 이름 + ` 이동` 그대로(조사 없음).
	AZonePortalActor* const GoodPortal =
		SpawnHudTestPortal(World, ZoneTable, HudTestNearDistanceCm, HudHuntZoneID, HudRanchZoneID);
	Expect(TEXT("사거리 안 켜진 포탈"), true, RanchName + TEXT(" 이동"));
	TestEqual(TEXT("고른 포탈이 켜진 포탈이다"), Travel->GetPortalTarget(), GoodPortal);

	// ⑤ 켜진 포탈을 치우면 다시 접힌다 (꺼진 포탈은 그대로 곁에 있다).
	if (GoodPortal != nullptr)
	{
		GoodPortal->Destroy();
	}
	Expect(TEXT("켜진 포탈 치움"), false, FString());

	// ⑥ 섬 배 — 존 표가 아니라 부두에 적힌 건너편 섬 이름을 쓴다(DESIGN 8.14 섬 사이 배).
	AZonePortalActor* const FarPier =
		World->SpawnActor<AZonePortalActor>(FVector(HudTestAwayDistanceCm, 0.f, 0.f), FRotator::ZeroRotator);
	AZonePortalActor* const NearPier =
		World->SpawnActor<AZonePortalActor>(FVector(HudTestNearDistanceCm, 0.f, 0.f), FRotator::ZeroRotator);
	if (TestNotNull(TEXT("먼 부두"), FarPier) && TestNotNull(TEXT("가까운 부두"), NearPier))
	{
		NearPier->SetFerryData(FarPier, FText::FromString(HudTestIslandName));
		NearPier->InitializeFromTable();
		Expect(TEXT("섬 배"), true, FString(HudTestIslandName) + TEXT(" 이동"));
	}

	DestroyHudTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenZoneTravelHudFerryLinesTest,
	"Snarehaven.UI.ZoneTravelHud.FerryLines",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// 거점 → 사냥터 요금 구간. 요금 숫자는 화면에 없고, 둘째 줄은 컴포넌트가 들고 있는 대기·사유만 옮긴다.
bool FSnarehavenZoneTravelHudFerryLinesTest::RunTest(const FString& Parameters)
{
	UDataTable* const ZoneTable = LoadObject<UDataTable>(nullptr, HudZoneTablePath);
	if (!TestNotNull(TEXT("DT_ZoneTable 로드"), ZoneTable))
	{
		return false;
	}
	const FString Base = ReadDisplayName(ZoneTable, HudHuntZoneID) + TEXT(" 이동");

	UWorld* World = CreateHudTestWorld();
	ASnarehavenCharacter* Player = SpawnHudTestPlayer(World);
	UZoneTravelComponent* const Travel = Player != nullptr ? Player->GetZoneTravel() : nullptr;
	AZonePortalActor* const Portal =
		SpawnHudTestPortal(World, ZoneTable, HudTestNearDistanceCm, HudRanchZoneID, HudHuntZoneID);
	if (Travel == nullptr || Portal == nullptr)
	{
		DestroyHudTestWorld(World);
		AddError(TEXT("플레이어·포탈 만들기 실패"));
		return false;
	}
	TestEqual(TEXT("거점 → 사냥터 포탈을 고른다"), Travel->RefreshTarget(), Portal);
	Travel->SetFerryBackend(MakeHudTestBackend());
	TestEqual(TEXT("요청 전 = 한 줄"), SnareZoneTravelHud::FormatButtonText(Travel).ToString(), Base);

	// 답을 붙잡는 가짜 서버 — 대기 중 글자를 보려고.
	TArray<USnareBackendSubsystem::FServerCallDone> HeldDone;
	USnareBackendSubsystem::TestServerOverride =
		[&HeldDone](const FString&, const TSharedPtr<FJsonObject>&, USnareBackendSubsystem::FServerCallDone OnDone)
		{
			HeldDone.Add(MoveTemp(OnDone));
		};

	// ① 답 전 — 「요금 확인 중...」 둘째 줄. 버튼은 그대로 떠 있다.
	TestTrue(TEXT("요청이 나간다"), Travel->TryFerryToNearest());
	const FString PendingText = SnareZoneTravelHud::FormatButtonText(Travel).ToString();
	AddInfo(FString::Printf(TEXT("답 전 글자 [%s]"), *PendingText.Replace(TEXT("\n"), TEXT(" | "))));
	TestEqual(TEXT("답 전 = 대기 줄"), PendingText, Base + TEXT("\n요금 확인 중..."));
	TestTrue(TEXT("답 전에도 버튼은 떠 있다"), SnareZoneTravelHud::IsButtonVisible(Travel));

	// ② 거절 답 — 둘째 줄이 컴포넌트가 받은 사유 그대로다(화면이 사유를 짓지 않는다).
	if (TestEqual(TEXT("요청은 한 번"), HeldDone.Num(), 1))
	{
		const TSharedPtr<FJsonObject> Refusal = MakeShared<FJsonObject>();
		Refusal->SetStringField(TEXT("error"), TEXT("코인이 모자란다"));
		Refusal->SetStringField(TEXT("code"), TEXT("not_enough_coins"));
		Refusal->SetNumberField(TEXT("need"), 5);
		Refusal->SetNumberField(TEXT("have"), 2);
		HeldDone[0](true, Refusal, FString());
	}
	const FString Reason = Travel->GetFerryMessage().ToString();
	const FString RefusedText = SnareZoneTravelHud::FormatButtonText(Travel).ToString();
	AddInfo(FString::Printf(TEXT("거절 글자 [%s]"), *RefusedText.Replace(TEXT("\n"), TEXT(" | "))));
	TestFalse(TEXT("거절 사유가 비지 않았다"), Reason.IsEmpty());
	TestEqual(TEXT("거절 = 사유 줄"), RefusedText, Base + TEXT("\n") + Reason);
	TestTrue(TEXT("거절 뒤 떠나지 않았다"), World->NextURL.IsEmpty());

	// ③ 성공 답 — 둘째 줄이 떨어지고 한 줄로 돌아온다.
	HeldDone.Reset();
	TestTrue(TEXT("다시 요청이 나간다"), Travel->TryFerryToNearest());
	if (TestEqual(TEXT("다시 요청도 한 번"), HeldDone.Num(), 1))
	{
		const TSharedPtr<FJsonObject> Paid = MakeShared<FJsonObject>();
		Paid->SetStringField(TEXT("dest"), TEXT("hunt"));
		Paid->SetNumberField(TEXT("paid"), 0);
		HeldDone[0](true, Paid, FString());
	}
	TestEqual(TEXT("성공 뒤 = 한 줄"), SnareZoneTravelHud::FormatButtonText(Travel).ToString(), Base);

	DestroyHudTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenZoneTravelHudCreateButtonTest,
	"Snarehaven.UI.ZoneTravelHud.CreateButton",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenZoneTravelHudCreateButtonTest::RunTest(const FString& Parameters)
{
	UWidgetTree* const Tree = NewObject<UWidgetTree>(GetTransientPackage());
	UTextBlock* Text = nullptr;
	UButton* const Button = SnareZoneTravelHud::CreateButton(*Tree, Text);
	if (!TestNotNull(TEXT("버튼이 만들어진다"), Button) || !TestNotNull(TEXT("글자가 만들어진다"), Text))
	{
		return false;
	}

	// 두 화면이 같은 이름이라야 이름으로 버튼을 찾는 검사(`-SnareTouch=`)가 화면마다 이름을 외우지 않는다.
	TestEqual(TEXT("버튼 이름"), Button->GetFName(), FName(TEXT("ZoneTravelButton")));
	TestEqual(TEXT("글자 이름"), Text->GetFName(), FName(TEXT("ZoneTravelButtonText")));
	TestEqual(TEXT("버튼 안 자식은 하나"), Button->GetChildrenCount(), 1);
	const USizeBox* const Box = Cast<USizeBox>(Button->GetChildAt(0));
	if (TestNotNull(TEXT("그 자식은 최소 높이 상자 (M38-02)"), Box))
	{
		TestEqual(TEXT("상자 최소 높이 = MinButtonHeight"), Box->GetMinDesiredHeight(), SnareZoneTravelHud::MinButtonHeight);
		TestEqual(TEXT("상자 안이 돌려받은 글자"), Box->GetContent(), static_cast<UWidget*>(Text));
	}
	TestTrue(TEXT("글자는 비어서 시작한다(갈 곳은 RefreshDisplay 가 채운다)"), Text->GetText().IsEmpty());
	return true;
}

// M38-02 — 존 이동 버튼은 두 화면 모두 손가락 한 마디(72) 이상이고, 사냥 화면에서는 공격·포획 버튼 칸을 덮지 않는다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenZoneTravelHudTouchSizeTest,
	"Snarehaven.UI.ZoneTravelHud.TouchSize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenZoneTravelHudTouchSizeTest::RunTest(const FString& Parameters)
{
	// 숫자는 DESIGN 2 「🟡 누르는 칸 최소 크기」 줄 — 코드 상수와 같은지도 본다(상수만 바뀌면 여기서 잡힌다).
	constexpr float DesignMinTouch = 72.f;
	TestTrue(TEXT("MinButtonHeight ≥ DESIGN 바닥 72"), SnareZoneTravelHud::MinButtonHeight >= DesignMinTouch);

	UWorld* World = CreateHudTestWorld();

	// 사냥 화면 — 칸 크기가 고정이라 캔버스 칸을 그대로 읽는다. 둘 다 오른쪽 아래 기준(앵커·정렬 1,1)이라
	// 칸 = [위치 − 크기, 위치] 사각형이다.
	UHuntHudWidget* const Hunt = CreateWidget<UHuntHudWidget>(World, UHuntHudWidget::StaticClass());
	if (TestNotNull(TEXT("사냥 HUD 생성"), Hunt))
	{
		Hunt->TakeWidget();
		const UButton* const Travel = Hunt->WidgetTree->FindWidget<UButton>(TEXT("ZoneTravelButton"));
		const UCanvasPanelSlot* const TravelSlot = Travel ? Cast<UCanvasPanelSlot>(Travel->Slot) : nullptr;
		if (TestNotNull(TEXT("사냥 HUD 존 이동 버튼 캔버스 칸"), TravelSlot))
		{
			const FVector2D Size = TravelSlot->GetSize();
			TestTrue(TEXT("사냥 HUD 존 이동 버튼 짧은 변 ≥ 72"), FMath::Min(Size.X, Size.Y) >= DesignMinTouch);
			const FBox2D TravelBox(TravelSlot->GetPosition() - Size, TravelSlot->GetPosition());
			for (const TCHAR* Name : { TEXT("AttackButton"), TEXT("CaptureButton") })
			{
				const UWidget* const Other = Hunt->WidgetTree->FindWidget(Name);
				const UCanvasPanelSlot* const OtherSlot = Other ? Cast<UCanvasPanelSlot>(Other->Slot) : nullptr;
				if (!TestNotNull(*FString::Printf(TEXT("%s 캔버스 칸"), Name), OtherSlot))
				{
					continue;
				}
				const FBox2D OtherBox(OtherSlot->GetPosition() - OtherSlot->GetSize(), OtherSlot->GetPosition());
				// 맞닿기만 하는 건 겹침이 아니다 — 넓이가 0 보다 큰 교집합만 본다.
				const bool bOverlap = TravelBox.Min.X < OtherBox.Max.X && OtherBox.Min.X < TravelBox.Max.X
					&& TravelBox.Min.Y < OtherBox.Max.Y && OtherBox.Min.Y < TravelBox.Max.Y;
				TestFalse(*FString::Printf(TEXT("존 이동 버튼이 %s 칸과 겹치지 않는다"), Name), bOverlap);
				AddInfo(FString::Printf(TEXT("[ZoneTravelTouch] 사냥 존 이동 버튼 (%.0f,%.0f)~(%.0f,%.0f) · %s (%.0f,%.0f)~(%.0f,%.0f) · 겹침 %s"),
					TravelBox.Min.X, TravelBox.Min.Y, TravelBox.Max.X, TravelBox.Max.Y, Name,
					OtherBox.Min.X, OtherBox.Min.Y, OtherBox.Max.X, OtherBox.Max.Y, bOverlap ? TEXT("있음") : TEXT("없음")));
			}
			AddInfo(FString::Printf(TEXT("[ZoneTravelTouch] 사냥 존 이동 버튼 %.0f×%.0f"), Size.X, Size.Y));
		}
	}

	// 거점 화면 — 칸이 자동 크기라 Slate 원하는 크기를 잰다(글자가 비어도 상자가 받친다).
	URanchHudWidget* const Ranch = CreateWidget<URanchHudWidget>(World, URanchHudWidget::StaticClass());
	if (TestNotNull(TEXT("거점 HUD 생성"), Ranch))
	{
		Ranch->TakeWidget();
		UButton* const Travel = Ranch->WidgetTree->FindWidget<UButton>(TEXT("ZoneTravelButton"));
		if (TestNotNull(TEXT("거점 HUD 존 이동 버튼"), Travel))
		{
			const TSharedRef<SWidget> Slate = Travel->TakeWidget();
			Slate->SlatePrepass(1.f);
			const FVector2D Desired = Slate->GetDesiredSize();
			TestTrue(TEXT("거점 HUD 존 이동 버튼 높이 ≥ 72"), Desired.Y >= DesignMinTouch);
			AddInfo(FString::Printf(TEXT("[ZoneTravelTouch] 거점 존 이동 버튼 원하는 크기 %.0f×%.0f"), Desired.X, Desired.Y));
		}
	}

	DestroyHudTestWorld(World);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
