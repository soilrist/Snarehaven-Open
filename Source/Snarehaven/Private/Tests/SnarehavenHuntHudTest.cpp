// ROADMAP M1-08 2층 검사 — 사냥 HUD 가 하이라이트된 동물의 이름·HP 를 그대로 비추는지,
// 포획 버튼이 `UCaptureComponent::CanCapture()` 와 같은 답을 내는지(판정을 두 군데 두지 않았는지),
// 그리고 자리표시 위젯 트리에 이름·HP 바·포획 버튼이 실제로 들어가는지.
// M1-10c 부터는 공격 버튼(포획 버튼 왼쪽·입력과 같은 길)과 안내줄(하이라이트가 없을 때만)도 여기서 본다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Animals/AnimalCharacter.h"
#include "Animals/AnimalStateComponent.h"
#include "Blueprint/WidgetTree.h"
#include "Combat/CaptureComponent.h"
#include "Combat/MeleeAttackComponent.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Data/AnimalRow.h"
#include "Data/ItemRow.h"
#include "Data/PlayerVitalsRow.h"
#include "Engine/DataTable.h"
#include "Equipment/EquipmentComponent.h"
#include "Gathering/GatherableActor.h"
#include "Gathering/GatherComponent.h"
#include "Gathering/TapFishingComponent.h"
#include "Internationalization/Regex.h"
#include "Inventory/InventoryComponent.h"
#include "Inventory/LootClaimComponent.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Player/PlayerVitalsComponent.h"
#include "Player/SnareLowHealth.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "SnarehavenCharacter.h"
#include "UI/HuntHudWidget.h"
#include "UI/SnareCaptureRingWidget.h"
#include "UI/SnareNoticeText.h"
#include "UI/SnareUiStyle.h"
#include "UI/WorldMapScreenWidget.h"
#include "World/FishingSpotActor.h"

namespace
{
	// 테스트 전용 값 (Verification.md 6 — 설계 숫자가 아니다).
	constexpr float TestAttackDamage = 10.f;
	constexpr float TestAttackRangeCm = 300.f;
	constexpr float TestAttackIntervalSeconds = 1.f;
	constexpr float TestNearDistanceCm = 100.f;
	constexpr float TestOutOfRangeDistanceCm = 1000.f;
	constexpr float TestMaxHP = 20.f;
	constexpr float TestCaptureThresholdPct = 50.f;
	constexpr float TestPercentTolerance = 0.001f;

	// 실제 데이터 테이블 (M1-01). 이름·체력의 기대값은 여기서만 온다.
	const TCHAR* AnimalTablePath = TEXT("/Game/Game/Data/Animals/DT_AnimalTable.DT_AnimalTable");
	const TCHAR* PigRowName = TEXT("Animal_Pig");

	// 장비 버튼 검사(M5-06c)가 쓰는 실제 장비 행. `ItemID` 는 표에서 읽는다 — 여기 적으면 표와 어긋난다.
	const TCHAR* ItemTablePath = TEXT("/Game/Game/Data/Items/DT_ItemTable.DT_ItemTable");
	const TCHAR* ClubRowName = TEXT("Item_Club");

	// M15-11 든 장비 줄 검사가 쓰는 도구 둘 — 아이템 ID 로 표에서 행을 찾는다 (DESIGN 13.5).
	const TCHAR* BinocularsItemID = TEXT("item.binoculars");
	const TCHAR* SatelliteItemID = TEXT("item.satellite_communicator");

	// M15-08 도구 자동 고르기 검사가 쓰는 도구 행 넷. `ItemID` 는 표에서 읽는다.
	const TCHAR* HatchetRowName = TEXT("Item_Hatchet");
	const TCHAR* ChainsawRowName = TEXT("Item_Chainsaw");
	const TCHAR* PickaxeRowName = TEXT("Item_PickaxeBasic");
	const TCHAR* RodRowName = TEXT("Item_RodBasic");

	// 그 행의 `ItemID` 로 "한 개 갖고 있다" 는 백엔드 스냅샷을 만든다 (인벤토리를 바꾸는 길은 이것뿐이다).
	bool GiveItemFromTable(UInventoryComponent* Inventory, const TCHAR* RowName)
	{
		UDataTable* const Table = LoadObject<UDataTable>(nullptr, ItemTablePath);
		const FItemRow* const Row =
			(Table != nullptr) ? Table->FindRow<FItemRow>(FName(RowName), TEXT("GiveItemFromTable")) : nullptr;
		if (Inventory == nullptr || Row == nullptr)
		{
			return false;
		}

		FInventorySlot Slot;
		Slot.ItemID = Row->ItemID;
		Slot.Count = 1;
		return Inventory->ApplyBackendSnapshot({ Slot });
	}

	// 플레이어 게이지 검사 전용 값 (Verification.md 6 — DESIGN 3.1 숫자를 베끼지 않는다).
	// 최대 스태미나 40 · 달리기 20/초 라서 1초 달리면 딱 절반이 준다 — 여유값 없이 눈으로 셀 수 있다.
	FPlayerVitalsRow MakeTestVitalsRow()
	{
		FPlayerVitalsRow Row;
		Row.MaxHealth = 50.f;
		Row.MaxStamina = 40.f;
		Row.RunStaminaPerSecond = 20.f;
		Row.AttackStaminaCost = 5.f;
		Row.StaminaRegenPerSecond = 10.f;
		Row.StaminaRegenDelaySeconds = 1.f;
		Row.ExhaustionHealthPerSecond = 5.f;
		Row.HealthRegenPerSecond = 1.f;
		Row.HealthRegenStaminaThreshold = 20.f;
		// M11-03 이 더한 부활 칸. 이 검사는 게이지만 보지만, 비워 두면 컴포넌트가 행을 미완성으로 보고
		// 아예 안 켜진다 — 게이지도 같이 죽는다. 값은 아무 양수나 되게 최대의 절반으로 둔다.
		Row.RespawnHealth = 25.f;
		Row.RespawnStamina = 20.f;
		// M5-01e2 가 더한 누운 시간 칸. 위와 같은 이유로 비워 두면 행이 미완성이 된다.
		Row.DeathLingerSeconds = 1.f;
		return Row;
	}

	constexpr float TestRunSeconds = 1.f;
	constexpr float TestHalfPercent = 0.5f;

	// M11-02b·M15-02 칸 검사 전용 값. 칸(번개) 수는 레퍼런스 시트가 정한 10 이고, 나머지는 위 검사용 행에서 나온다:
	// 0.3초 달리면 40 중 6 이 빠져 34/40 = 0.85 → 칸으로 올리면 0.9. 거기서 1.69초 더 달리면 0.2 만 남아
	// (= 0.005) 한 칸만 켜진다.
	constexpr int32 TestGaugeSegmentCount = 10;
	// M15-02 시트 대조 전용 — 화면 가장자리로 치는 폭(가로·세로 25%). 게임 규칙 숫자가 아니다.
	constexpr float TestScreenEdgeFraction = 0.25f;
	constexpr float TestPartialRunSeconds = 0.3f;
	constexpr float TestPartialPercent = 0.85f;
	constexpr float TestPartialSegmentPercent = 0.9f;
	constexpr float TestNearlyEmptyRunSeconds = 1.69f;
	constexpr int32 TestPartialLitBolts = 9;

	// 켜진 번개 수 — 색이 강조 노랑인 칸만 센다(M15-02 시트 "소진분 회색").
	int32 CountLitBolts(const UHuntHudWidget& Hud)
	{
		int32 Lit = 0;
		for (int32 Index = 0; Index < TestGaugeSegmentCount; ++Index)
		{
			const UImage* const Bolt = Hud.WidgetTree->FindWidget<UImage>(
				FName(*FString::Printf(TEXT("PlayerStaminaBolt_%d"), Index)));
			if (Bolt != nullptr && Bolt->GetColorAndOpacity() == SnareUiStyle::UiBoltYellow())
			{
				++Lit;
			}
		}
		return Lit;
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

	AAnimalCharacter* SpawnTestAnimal(UWorld* World, float DistanceCm, float MaxHP = TestMaxHP)
	{
		AAnimalCharacter* Animal =
			World->SpawnActor<AAnimalCharacter>(FVector(DistanceCm, 0.f, 0.f), FRotator::ZeroRotator);
		if (Animal == nullptr)
		{
			return nullptr;
		}

		FAnimalRow Row;
		Row.AnimalID = TEXT("animal.test_dummy");
		Row.DisplayName = FText::FromString(TEXT("검사용 동물"));
		Row.MaxHP = MaxHP;
		Row.CaptureThresholdPct = TestCaptureThresholdPct;
		Animal->GetAnimalState()->InitializeFromRow(Row);
		return Animal;
	}

	ASnarehavenCharacter* SpawnTestPlayer(UWorld* World)
	{
		ASnarehavenCharacter* Player =
			World->SpawnActor<ASnarehavenCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
		if (Player == nullptr)
		{
			return nullptr;
		}

		Player->GetMeleeAttack()->SetAttackSettings(
			TestAttackDamage, TestAttackRangeCm, TestAttackIntervalSeconds);
		return Player;
	}

	// 뷰포트에 올리지 않은 HUD. 2층은 화면 없이 도는 검사라 위젯만 만들어 배선을 본다.
	UHuntHudWidget* CreateTestHud(UWorld* World, ASnarehavenCharacter* Player)
	{
		UHuntHudWidget* Hud = CreateWidget<UHuntHudWidget>(World, UHuntHudWidget::StaticClass());
		if (Hud != nullptr)
		{
			Hud->BindToCharacter(Player);
		}
		return Hud;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenHudNoTargetTest, "Snarehaven.HuntHud.Panel.NoTargetHidesAnimal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenHudNoTargetTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = SpawnTestPlayer(World);
	AAnimalCharacter* FarAnimal = SpawnTestAnimal(World, TestOutOfRangeDistanceCm);
	UHuntHudWidget* Hud = CreateTestHud(World, Player);
	if (!TestNotNull(TEXT("플레이어 스폰"), Player) || !TestNotNull(TEXT("사거리 밖 동물"), FarAnimal)
		|| !TestNotNull(TEXT("HUD 생성"), Hud))
	{
		DestroyTestWorld(World);
		return false;
	}

	TestNull(TEXT("하이라이트가 없으면 HUD 에 보여줄 동물도 없다"), Hud->GetShownAnimal());
	TestFalse(TEXT("동물 칸을 접어 둔다"), Hud->IsAnimalPanelVisible());
	TestFalse(TEXT("포획 버튼이 꺼져 있다"), Hud->IsCaptureButtonEnabled());
	TestEqual(TEXT("HP 바가 0"), Hud->GetHealthPercent(), 0.f, TestPercentTolerance);
	TestTrue(TEXT("이름 칸이 비어 있다"), Hud->GetAnimalNameText().IsEmpty());

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenHudShowsTargetTest, "Snarehaven.HuntHud.Panel.FollowsHighlight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenHudShowsTargetTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = SpawnTestPlayer(World);
	AAnimalCharacter* Animal = SpawnTestAnimal(World, TestNearDistanceCm);
	UHuntHudWidget* Hud = CreateTestHud(World, Player);
	if (!TestNotNull(TEXT("플레이어 스폰"), Player) || !TestNotNull(TEXT("동물 스폰"), Animal)
		|| !TestNotNull(TEXT("HUD 생성"), Hud))
	{
		DestroyTestWorld(World);
		return false;
	}

	// 하이라이트는 타이머가 도는 대신 검사에서 직접 한 번 돌린다 (2층은 시간 없이 돈다).
	Player->GetMeleeAttack()->RefreshTarget();

	TestEqual(TEXT("하이라이트된 동물이 HUD 에 올라온다"), Hud->GetShownAnimal(), Animal);
	TestTrue(TEXT("동물 칸이 펼쳐진다"), Hud->IsAnimalPanelVisible());
	TestEqual(TEXT("갓 초기화한 동물은 HP 바가 꽉 차 있다"), Hud->GetHealthPercent(), 1.f, TestPercentTolerance);
	TestEqual(TEXT("이름은 행에서 온 그대로다"),
		Hud->GetAnimalNameText().ToString(), Animal->GetAnimalState()->GetDisplayName().ToString());

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenHudHealthTest, "Snarehaven.HuntHud.Bar.FollowsDamage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenHudHealthTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = SpawnTestPlayer(World);
	AAnimalCharacter* Animal = SpawnTestAnimal(World, TestNearDistanceCm);
	UHuntHudWidget* Hud = CreateTestHud(World, Player);
	if (!TestNotNull(TEXT("플레이어 스폰"), Player) || !TestNotNull(TEXT("동물 스폰"), Animal)
		|| !TestNotNull(TEXT("HUD 생성"), Hud))
	{
		DestroyTestWorld(World);
		return false;
	}

	Player->GetMeleeAttack()->RefreshTarget();
	const float BeforePercent = Hud->GetHealthPercent();

	Player->GetMeleeAttack()->AttackOnce();

	const UAnimalStateComponent* State = Animal->GetAnimalState();
	const float ExpectedPercent = State->GetCurrentHP() / State->GetMaxHP();
	TestTrue(TEXT("한 대 맞으면 HP 바가 준다"), Hud->GetHealthPercent() < BeforePercent);
	TestEqual(TEXT("HP 바는 상태 컴포넌트 값 그대로다"),
		Hud->GetHealthPercent(), ExpectedPercent, TestPercentTolerance);

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenHudCaptureButtonTest, "Snarehaven.HuntHud.Button.MatchesCaptureRule",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenHudCaptureButtonTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = SpawnTestPlayer(World);
	AAnimalCharacter* Animal = SpawnTestAnimal(World, TestNearDistanceCm);
	UHuntHudWidget* Hud = CreateTestHud(World, Player);
	if (!TestNotNull(TEXT("플레이어 스폰"), Player) || !TestNotNull(TEXT("동물 스폰"), Animal)
		|| !TestNotNull(TEXT("HUD 생성"), Hud))
	{
		DestroyTestWorld(World);
		return false;
	}

	UMeleeAttackComponent* Melee = Player->GetMeleeAttack();
	const UCaptureComponent* Capture = Player->GetCapture();
	Melee->RefreshTarget();

	TestFalse(TEXT("임계값 위에서는 버튼이 꺼져 있다"), Hud->IsCaptureButtonEnabled());
	TestEqual(TEXT("버튼 상태는 포획 컴포넌트 판정과 같다"),
		Hud->IsCaptureButtonEnabled(), Capture->CanCapture());

	const UAnimalStateComponent* State = Animal->GetAnimalState();
	while (State->GetState() != EAnimalState::Capturable && !State->IsFinished())
	{
		if (!Melee->AttackOnce())
		{
			break;
		}
	}

	TestEqual(TEXT("동물이 포획 가능 상태가 됐다"), State->GetState(), EAnimalState::Capturable);
	TestTrue(TEXT("임계값에 닿으면 버튼이 켜진다"), Hud->IsCaptureButtonEnabled());
	TestEqual(TEXT("켜진 뒤에도 포획 컴포넌트와 같은 답이다"),
		Hud->IsCaptureButtonEnabled(), Capture->CanCapture());

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenHudUnsetNumbersTest, "Snarehaven.HuntHud.Bar.UnsetNumbersStayEmpty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenHudUnsetNumbersTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = SpawnTestPlayer(World);
	// 최대 체력이 미정(-1)인 동물 — 상태 머신이 켜지지 않는다 (Verification.md 7).
	AAnimalCharacter* Animal = SpawnTestAnimal(World, TestNearDistanceCm, -1.f);
	UHuntHudWidget* Hud = CreateTestHud(World, Player);
	if (!TestNotNull(TEXT("플레이어 스폰"), Player) || !TestNotNull(TEXT("동물 스폰"), Animal)
		|| !TestNotNull(TEXT("HUD 생성"), Hud))
	{
		DestroyTestWorld(World);
		return false;
	}

	Player->GetMeleeAttack()->RefreshTarget();

	TestEqual(TEXT("미정 체력이면 HP 바를 채우지 않는다"),
		Hud->GetHealthPercent(), 0.f, TestPercentTolerance);
	TestFalse(TEXT("미정 체력이면 포획 버튼도 꺼져 있다"), Hud->IsCaptureButtonEnabled());

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenHudTreeTest, "Snarehaven.HuntHud.Tree.HasPlaceholderParts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenHudTreeTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	UHuntHudWidget* Hud = CreateWidget<UHuntHudWidget>(World, UHuntHudWidget::StaticClass());
	if (!TestNotNull(TEXT("HUD 생성"), Hud))
	{
		DestroyTestWorld(World);
		return false;
	}

	// Slate 위젯을 실제로 만들어야 RebuildWidget 이 돌고, 그때 자리표시 트리가 세워진다.
	Hud->TakeWidget();

	TestNotNull(TEXT("자리표시 트리에 루트가 생긴다"), Hud->WidgetTree->RootWidget.Get());
	TestNotNull(TEXT("이름 칸이 있다"), Hud->WidgetTree->FindWidget<UTextBlock>(TEXT("AnimalNameText")));
	TestNotNull(TEXT("HP 바가 있다"), Hud->WidgetTree->FindWidget<UProgressBar>(TEXT("AnimalHealthBar")));
	TestNotNull(TEXT("포획 버튼이 있다"), Hud->WidgetTree->FindWidget<UButton>(TEXT("CaptureButton")));
	TestNotNull(TEXT("공격 버튼이 있다"), Hud->WidgetTree->FindWidget<UButton>(TEXT("AttackButton")));
	TestNotNull(TEXT("안내줄이 있다"), Hud->WidgetTree->FindWidget<UTextBlock>(TEXT("GuideText")));
	// 3층 검사 스위치 `-SnareTouch=equip[:부위]` 가 이 **이름**으로 버튼을 찾는다 — 이름이 바뀌면
	// 손가락이 허공을 짚는다. 이름은 HUD 가 한 곳에서 주므로(M9-03) 검사도 거기서 받아 온다.
	for (const EEquipSlot Slot : UHuntHudWidget::GetEquipButtonSlots())
	{
		const FName ButtonName = UHuntHudWidget::GetEquipButtonWidgetName(Slot);
		TestNotNull(*FString::Printf(TEXT("장비 버튼 %s 가 있다"), *ButtonName.ToString()),
			Hud->WidgetTree->FindWidget<UButton>(ButtonName));
	}
	TestEqual(TEXT("장비 버튼은 부위 넷이다"), UHuntHudWidget::GetEquipButtonSlots().Num(), 4);

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenHudAttackButtonPlacementTest,
	"Snarehaven.HuntHud.Tree.AttackButtonSitsLeftOfCapture",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenHudAttackButtonPlacementTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	UHuntHudWidget* Hud = CreateWidget<UHuntHudWidget>(World, UHuntHudWidget::StaticClass());
	if (!TestNotNull(TEXT("HUD 생성"), Hud))
	{
		DestroyTestWorld(World);
		return false;
	}

	Hud->TakeWidget();

	UButton* const Attack = Hud->WidgetTree->FindWidget<UButton>(TEXT("AttackButton"));
	UButton* const Capture = Hud->WidgetTree->FindWidget<UButton>(TEXT("CaptureButton"));
	if (!TestNotNull(TEXT("공격 버튼"), Attack) || !TestNotNull(TEXT("포획 버튼"), Capture))
	{
		DestroyTestWorld(World);
		return false;
	}

	const UCanvasPanelSlot* AttackSlot = Cast<UCanvasPanelSlot>(Attack->Slot);
	const UCanvasPanelSlot* CaptureSlot = Cast<UCanvasPanelSlot>(Capture->Slot);
	if (!TestNotNull(TEXT("공격 버튼 캔버스 칸"), AttackSlot) || !TestNotNull(TEXT("포획 버튼 캔버스 칸"), CaptureSlot))
	{
		DestroyTestWorld(World);
		return false;
	}

	// 둘 다 오른쪽 아래 기준(앵커 1,1)이라 X 가 더 작은 쪽이 화면에서 왼쪽이다.
	TestTrue(TEXT("공격 버튼이 포획 버튼보다 왼쪽에 있다"),
		AttackSlot->GetPosition().X < CaptureSlot->GetPosition().X);
	TestTrue(TEXT("두 버튼이 겹치지 않는다"),
		CaptureSlot->GetPosition().X - AttackSlot->GetPosition().X >= AttackSlot->GetSize().X);
	AddInfo(FString::Printf(TEXT("공격 버튼 X %.0f (폭 %.0f) · 포획 버튼 X %.0f"),
		AttackSlot->GetPosition().X, AttackSlot->GetSize().X, CaptureSlot->GetPosition().X));

	DestroyTestWorld(World);
	return true;
}

// M21-08c — 공격·포획 버튼 크기·불투명도 = DESIGN 2 「인게임 HUD 색」 표 「공격·포획 버튼」 줄 글자.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenHudButtonSizeDesignTest,
	"Snarehaven.HuntHud.Tree.ButtonSizeMatchesDesign",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenHudButtonSizeDesignTest::RunTest(const FString& Parameters)
{
	FString Design;
	if (!FFileHelper::LoadFileToString(Design, *FPaths::Combine(FPaths::ProjectDir(), TEXT("docs/DESIGN.md"))))
	{
		AddError(TEXT("docs/DESIGN.md 를 못 읽었다"));
		return false;
	}
	double DesignSize = -1.0;
	double DesignOpacity = -1.0;
	FRegexMatcher Matcher(FRegexPattern(TEXT("\\| 공격·포획 버튼 \\| 지름 ([0-9.]+), 불투명도 ([0-9.]+) \\|")), Design);
	if (Matcher.FindNext())
	{
		DesignSize = FCString::Atod(*Matcher.GetCaptureGroup(1));
		DesignOpacity = FCString::Atod(*Matcher.GetCaptureGroup(2));
	}
	if (!TestTrue(TEXT("DESIGN 「공격·포획 버튼」 지름·불투명도를 읽었다"), DesignSize > 0.0 && DesignOpacity > 0.0))
	{
		return false;
	}

	UWorld* World = CreateTestWorld();
	UHuntHudWidget* Hud = CreateWidget<UHuntHudWidget>(World, UHuntHudWidget::StaticClass());
	if (!TestNotNull(TEXT("HUD 생성"), Hud))
	{
		DestroyTestWorld(World);
		return false;
	}
	Hud->TakeWidget();

	for (const TCHAR* Name : { TEXT("AttackButton"), TEXT("CaptureButton") })
	{
		UButton* const Button = Hud->WidgetTree->FindWidget<UButton>(Name);
		const UCanvasPanelSlot* const ButtonSlot = Button ? Cast<UCanvasPanelSlot>(Button->Slot) : nullptr;
		if (!TestNotNull(*FString::Printf(TEXT("%s 캔버스 칸"), Name), ButtonSlot))
		{
			continue;
		}
		const FVector2D Size = ButtonSlot->GetSize();
		TestEqual(*FString::Printf(TEXT("%s 폭 = DESIGN 지름"), Name), static_cast<double>(Size.X), DesignSize, 0.01);
		TestEqual(*FString::Printf(TEXT("%s 높이 = DESIGN 지름"), Name), static_cast<double>(Size.Y), DesignSize, 0.01);
		TestEqual(*FString::Printf(TEXT("%s 불투명도 = DESIGN"), Name),
			static_cast<double>(Button->GetBackgroundColor().A), DesignOpacity, 0.001);
		AddInfo(FString::Printf(TEXT("[ButtonSize] %s %.0f×%.0f · 불투명도 %.2f | DESIGN 지름 %.0f · 불투명도 %.2f"),
			Name, Size.X, Size.Y, Button->GetBackgroundColor().A, DesignSize, DesignOpacity));
	}

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenHudAttackButtonPathTest, "Snarehaven.HuntHud.Button.AttackUsesInputPath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenHudAttackButtonPathTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = SpawnTestPlayer(World);
	AAnimalCharacter* Animal = SpawnTestAnimal(World, TestNearDistanceCm);
	UHuntHudWidget* Hud = CreateTestHud(World, Player);
	if (!TestNotNull(TEXT("플레이어 스폰"), Player) || !TestNotNull(TEXT("동물 스폰"), Animal)
		|| !TestNotNull(TEXT("HUD 생성"), Hud))
	{
		DestroyTestWorld(World);
		return false;
	}

	UMeleeAttackComponent* Melee = Player->GetMeleeAttack();
	TestFalse(TEXT("누르기 전에는 공격 중이 아니다"), Melee->IsAttacking());

	// 화면 버튼이 부르는 것과 **같은 함수**다 (`OnAttackStarted` 도 이걸 부른다) —
	// 검사에서 Slate 클릭을 흉내 내는 대신 길이 하나인지를 본다.
	Player->BeginAttack();
	TestTrue(TEXT("누르면 공격 누름 상태가 된다"), Melee->IsAttacking());
	TestEqual(TEXT("누르면 앞의 동물이 대상이 된다"), Melee->GetCurrentTarget(), Animal);

	Player->EndAttack();
	TestFalse(TEXT("떼면 멈춘다"), Melee->IsAttacking());

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenHudGuideTest, "Snarehaven.HuntHud.Guide.NoticeCardOnlyWithBanner",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// M15-02 — 안내 문장은 없앴다(레퍼런스 화면엔 늘 떠 있는 안내가 없다). 왼쪽 위 크림 알림 카드는
// 알림(포획 성공·거절 문구)이 떠 있을 때만 펴진다. 동물 카드는 하이라이트를 그대로 따른다.
bool FSnarehavenHudGuideTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = SpawnTestPlayer(World);
	AAnimalCharacter* FarAnimal = SpawnTestAnimal(World, TestOutOfRangeDistanceCm);
	UHuntHudWidget* Hud = CreateTestHud(World, Player);
	if (!TestNotNull(TEXT("플레이어 스폰"), Player) || !TestNotNull(TEXT("사거리 밖 동물"), FarAnimal)
		|| !TestNotNull(TEXT("HUD 생성"), Hud))
	{
		DestroyTestWorld(World);
		return false;
	}

	// 반환값을 붙잡고 있어야 트리가 산다 (`SnarehavenRanchHudTest` 와 같은 함정).
	const TSharedRef<SWidget> SlateWidget = Hud->TakeWidget();
	UTextBlock* const Guide = Hud->WidgetTree->FindWidget<UTextBlock>(TEXT("GuideText"));
	UWidget* const Card = Hud->WidgetTree->FindWidget(TEXT("NoticeCard"));
	if (!TestNotNull(TEXT("알림 글자"), Guide) || !TestNotNull(TEXT("알림 카드"), Card))
	{
		DestroyTestWorld(World);
		return false;
	}

	Hud->RefreshDisplay();
	TestEqual(TEXT("알림이 없으면 카드가 접힌다"), Card->GetVisibility(), ESlateVisibility::Collapsed);
	TestEqual(TEXT("알림이 없으면 글자도 접힌다"), Guide->GetVisibility(), ESlateVisibility::Collapsed);
	TestFalse(TEXT("하이라이트가 없으면 동물 카드도 없다"), Hud->IsAnimalPanelVisible());

	const FText Message = FText::FromString(TEXT("검사 알림"));
	Hud->ShowNoticeBanner(Message);
	TestNotEqual(TEXT("알림이 뜨면 카드가 펴진다"), Card->GetVisibility(), ESlateVisibility::Collapsed);
	TestEqual(TEXT("카드에 알림 문구가 그대로 뜬다"), Guide->GetText().ToString(), Message.ToString());

	Hud->ClearCaptureBanner();
	TestEqual(TEXT("알림이 내려가면 카드가 다시 접힌다"), Card->GetVisibility(), ESlateVisibility::Collapsed);

	// 동물을 사거리 안으로 옮기고 하이라이트를 한 번 돌리면 동물 카드가 뜬다.
	FarAnimal->SetActorLocation(FVector(TestNearDistanceCm, 0.f, 0.f));
	Player->GetMeleeAttack()->RefreshTarget();
	TestTrue(TEXT("하이라이트가 잡히면 동물 카드가 뜬다"), Hud->IsAnimalPanelVisible());

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenHudTableTest, "Snarehaven.HuntHud.Table.PigShowsTableNameAndHealth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenHudTableTest::RunTest(const FString& Parameters)
{
	UDataTable* AnimalTable = LoadObject<UDataTable>(nullptr, AnimalTablePath);
	if (!TestNotNull(TEXT("DT_AnimalTable 을 읽었다"), AnimalTable))
	{
		return false;
	}

	const FAnimalRow* PigRow = AnimalTable->FindRow<FAnimalRow>(FName(PigRowName), TEXT("HuntHudTest"));
	if (!TestNotNull(TEXT("돼지 행이 있다"), PigRow))
	{
		return false;
	}

	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = SpawnTestPlayer(World);
	AAnimalCharacter* Pig =
		World->SpawnActor<AAnimalCharacter>(FVector(TestNearDistanceCm, 0.f, 0.f), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("플레이어 스폰"), Player) || !TestNotNull(TEXT("돼지 스폰"), Pig))
	{
		DestroyTestWorld(World);
		return false;
	}

	// 기대값을 코드에 적지 않는다 — 행을 그대로 넣고, HUD 가 그 행을 비추는지만 본다.
	Pig->GetAnimalState()->InitializeFromRow(*PigRow);
	UHuntHudWidget* Hud = CreateTestHud(World, Player);
	if (!TestNotNull(TEXT("HUD 생성"), Hud))
	{
		DestroyTestWorld(World);
		return false;
	}

	Player->GetMeleeAttack()->RefreshTarget();

	TestEqual(TEXT("HUD 이름은 행의 DisplayName"),
		Hud->GetAnimalNameText().ToString(), PigRow->DisplayName.ToString());
	TestEqual(TEXT("갓 스폰한 돼지는 HP 바가 꽉 차 있다"),
		Hud->GetHealthPercent(), 1.f, TestPercentTolerance);

	// 임계값까지 때려서 포획 버튼이 켜지는 지점이 행의 비율과 맞는지 본다.
	UMeleeAttackComponent* Melee = Player->GetMeleeAttack();
	const UAnimalStateComponent* State = Pig->GetAnimalState();
	while (State->GetState() != EAnimalState::Capturable && !State->IsFinished())
	{
		if (!Melee->AttackOnce())
		{
			break;
		}
	}

	TestTrue(TEXT("임계값에서 포획 버튼이 켜진다"), Hud->IsCaptureButtonEnabled());
	AddInfo(FString::Printf(
		TEXT("[%s] HUD 이름 \"%s\" · HP 바 %.0f%% (행 최대 %.0f · 임계 %.0f%%) · 포획 버튼 %s"),
		*PigRow->AnimalID.ToString(), *Hud->GetAnimalNameText().ToString(), Hud->GetHealthPercent() * 100.f,
		PigRow->MaxHP, PigRow->CaptureThresholdPct, Hud->IsCaptureButtonEnabled() ? TEXT("켜짐") : TEXT("꺼짐")));

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenHudCaptureBannerTest, "Snarehaven.HuntHud.Guide.CaptureBannerBorrowsGuideLine",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenHudCaptureBannerTest::RunTest(const FString& Parameters)
{
	// M1-10e2 — 포획에 성공하면 안내줄 자리에 `포획 성공 — <이름>` 이 뜬다 (감독 지시 2026-09-20 13:00 (c)).
	// 2초 뒤 사라지는 것은 타이머라 헤드리스에서 못 돌린다 — 내리는 길(`ClearCaptureBanner`)을 직접 부른다.
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = SpawnTestPlayer(World);
	AAnimalCharacter* Animal = SpawnTestAnimal(World, TestNearDistanceCm);
	UHuntHudWidget* Hud = CreateTestHud(World, Player);
	if (!TestNotNull(TEXT("플레이어 스폰"), Player) || !TestNotNull(TEXT("동물 스폰"), Animal)
		|| !TestNotNull(TEXT("HUD 생성"), Hud))
	{
		DestroyTestWorld(World);
		return false;
	}

	UMeleeAttackComponent* Melee = Player->GetMeleeAttack();
	Melee->RefreshTarget();
	TestFalse(TEXT("포획 전에는 성공 줄이 없다"), Hud->IsCaptureBannerVisible());
	TestTrue(TEXT("포획 전에는 동물 칸이 떠 있다"), Hud->IsAnimalPanelVisible());

	const UAnimalStateComponent* State = Animal->GetAnimalState();
	while (State->GetState() != EAnimalState::Capturable && !State->IsFinished())
	{
		if (!Melee->AttackOnce())
		{
			break;
		}
	}
	TestEqual(TEXT("동물이 포획 가능 상태가 됐다"), State->GetState(), EAnimalState::Capturable);

	const FText AnimalName = Hud->GetAnimalNameText();
	TestTrue(TEXT("화면 버튼과 같은 길로 포획했다"), Player->GetCapture()->TryCaptureInWindowForTest());

	TestTrue(TEXT("포획하면 성공 줄이 뜬다"), Hud->IsCaptureBannerVisible());
	TestTrue(TEXT("성공 줄에 동물 이름이 들어 있다"),
		Hud->GetCaptureBannerText().ToString().Contains(AnimalName.ToString()));
	TestFalse(TEXT("성공 줄이 떠 있는 동안 동물 칸은 접힌다"), Hud->IsAnimalPanelVisible());
	AddInfo(FString::Printf(TEXT("성공 줄: %s"), *Hud->GetCaptureBannerText().ToString()));

	Hud->ClearCaptureBanner();
	TestFalse(TEXT("내리면 성공 줄이 사라진다"), Hud->IsCaptureBannerVisible());
	// M15-02 안내 문장은 없다 — 내려가면 알림 글자가 빈다(카드도 접힌다, 위 `NoticeCardOnlyWithBanner`).
	TestTrue(TEXT("성공 줄이 내려가면 알림 문구가 빈다"), Hud->GetCaptureBannerText().IsEmpty());

	DestroyTestWorld(World);
	return true;
}

// ROADMAP M5-06c — 장비 버튼이 **인벤토리와 지금 든 것**만 보고 뜨는지.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenHudEquipButtonTest, "Snarehaven.HuntHud.Equip.ButtonFollowsInventory",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenHudEquipButtonTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = SpawnTestPlayer(World);
	UHuntHudWidget* Hud = CreateTestHud(World, Player);
	if (!TestNotNull(TEXT("플레이어 스폰"), Player) || !TestNotNull(TEXT("HUD 생성"), Hud))
	{
		DestroyTestWorld(World);
		return false;
	}

	// 갓 스폰한 플레이어의 인벤토리는 비어 있다 — 백엔드 스냅샷이 오기 전 상태 그대로다 (DESIGN 4.5).
	for (const EEquipSlot Slot : UHuntHudWidget::GetEquipButtonSlots())
	{
		TestFalse(TEXT("가진 장비가 없으면 부위 버튼을 접어 둔다"), Hud->IsEquipButtonVisible(Slot));
		TestEqual(TEXT("가리키는 행도 없다"), Hud->GetEquipButtonItem(Slot), FName(NAME_None));
		TestTrue(TEXT("버튼 글자도 비어 있다"), Hud->GetEquipButtonText(Slot).IsEmpty());
	}

	if (!TestTrue(TEXT("백엔드 스냅샷으로 몽둥이를 한 개 갖는다"),
		GiveItemFromTable(Player->GetInventory(), ClubRowName)))
	{
		DestroyTestWorld(World);
		return false;
	}

	// 몽둥이는 표가 `Weapon` 이라고 적은 행이다 — **그 부위 버튼만** 뜬다 (M9-03).
	TestTrue(TEXT("가진 장비가 생기면 그 부위 버튼이 뜬다"),
		Hud->IsEquipButtonVisible(EEquipSlot::Weapon));
	TestEqual(TEXT("버튼이 표의 장비 행을 가리킨다"),
		Hud->GetEquipButtonItem(EEquipSlot::Weapon), FName(ClubRowName));
	TestFalse(TEXT("아직 들고 있지는 않다"), Hud->IsEquipButtonItemEquipped(EEquipSlot::Weapon));
	TestEqual(TEXT("글자가 아이템 이름이 아닌 `무기 장착` 이다"),
		Hud->GetEquipButtonText(EEquipSlot::Weapon).ToString(), FString(TEXT("무기 장착")));
	TestFalse(TEXT("무기를 가져도 도구 버튼은 접혀 있다"), Hud->IsEquipButtonVisible(EEquipSlot::Tool));
	TestFalse(TEXT("상의 버튼도 접혀 있다"), Hud->IsEquipButtonVisible(EEquipSlot::OutfitTop));
	TestFalse(TEXT("하의 버튼도 접혀 있다"), Hud->IsEquipButtonVisible(EEquipSlot::OutfitBottom));

	// 재료만 들고 있으면 버튼이 뜨면 안 된다 — 표가 장비라고 적은 행만 손에 들린다.
	FInventorySlot MaterialSlot;
	MaterialSlot.ItemID = TEXT("item.pig_meat");
	MaterialSlot.Count = 3;
	Player->GetInventory()->ApplyBackendSnapshot({ MaterialSlot });
	TestFalse(TEXT("재료만 가지면 장비 버튼이 접힌다"), Hud->IsEquipButtonVisible(EEquipSlot::Weapon));

	DestroyTestWorld(World);
	return true;
}

// 탭 한 번이 실제로 손에 들리고, 또 한 번이 내려놓는지. 화면 버튼과 같은 길(`ToggleEquipButtonItem`)로 본다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenHudEquipTapTest, "Snarehaven.HuntHud.Equip.TapEquipsAndUnequips",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenHudEquipTapTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = SpawnTestPlayer(World);
	UHuntHudWidget* Hud = CreateTestHud(World, Player);
	if (!TestNotNull(TEXT("플레이어 스폰"), Player) || !TestNotNull(TEXT("HUD 생성"), Hud))
	{
		DestroyTestWorld(World);
		return false;
	}

	// 몸이 없으면 붙일 소켓이 없다 (M5-06b 검사와 같은 이유).
	if (!TestTrue(TEXT("사람 메시를 올렸다"), Player->ApplyPlayerBody())
		|| !TestTrue(TEXT("몽둥이를 한 개 갖는다"), GiveItemFromTable(Player->GetInventory(), ClubRowName)))
	{
		DestroyTestWorld(World);
		return false;
	}

	UEquipmentComponent* const Equipment = Player->GetEquipment();
	if (!TestNotNull(TEXT("장비 컴포넌트가 붙어 있다"), Equipment))
	{
		DestroyTestWorld(World);
		return false;
	}

	TestTrue(TEXT("탭 한 번에 들린다"), Hud->ToggleEquipButtonItem(EEquipSlot::Weapon));
	TestEqual(TEXT("무기 부위가 몽둥이를 들고 있다"),
		Equipment->GetEquippedItem(EEquipSlot::Weapon), FName(ClubRowName));
	TestTrue(TEXT("버튼이 들고 있다고 답한다"), Hud->IsEquipButtonItemEquipped(EEquipSlot::Weapon));
	TestTrue(TEXT("글자가 `해제` 로 바뀐다"),
		Hud->GetEquipButtonText(EEquipSlot::Weapon).ToString().Contains(TEXT("해제")));

	// **부위 하나만 바뀐다** — 무기를 들어도 도구·옷 자리는 그대로 비어 있다 (M9-01 이 동시 착용을 열었다).
	TestEqual(TEXT("도구 부위는 여전히 빈다"),
		Equipment->GetEquippedItem(EEquipSlot::Tool), FName(NAME_None));
	TestFalse(TEXT("가리킬 도구가 없으면 도구 버튼 탭은 아무 일도 안 한다"),
		Hud->ToggleEquipButtonItem(EEquipSlot::Tool));
	TestEqual(TEXT("도구 버튼을 눌러도 무기는 그대로 들려 있다"),
		Equipment->GetEquippedItem(EEquipSlot::Weapon), FName(ClubRowName));

	// **든 것은 인벤토리가 비어도 버튼에 남아야 한다** — 백엔드가 서기 전(M2-03) 캐시는 언제든 빈 채로
	// 덮여 오는데, 그때 버튼이 사라지면 들고 있는 장비를 벗을 길이 없어진다.
	TestTrue(TEXT("빈 스냅샷을 받는다"), Player->GetInventory()->ApplyBackendSnapshot({}));
	TestTrue(TEXT("들고 있으면 인벤토리가 비어도 버튼이 남는다"),
		Hud->IsEquipButtonVisible(EEquipSlot::Weapon));

	TestTrue(TEXT("한 번 더 탭하면 내려놓는다"), Hud->ToggleEquipButtonItem(EEquipSlot::Weapon));
	TestEqual(TEXT("무기 부위가 빈다"),
		Equipment->GetEquippedItem(EEquipSlot::Weapon), FName(NAME_None));
	TestFalse(TEXT("가진 것도 든 것도 없으면 버튼이 접힌다"),
		Hud->IsEquipButtonVisible(EEquipSlot::Weapon));

	DestroyTestWorld(World);
	return true;
}

// ROADMAP M9-03 — 화면 버튼이 실제로 **부위 단위**인지. 위 둘은 질의 함수만 보지만 이 검사는
// 위젯 트리에 선 버튼의 접힘/펴짐을 본다 (버튼은 넷인데 갱신이 한 칸만 건드리면 여기서 걸린다).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenHudEquipButtonsSplitTest,
	"Snarehaven.HuntHud.Equip.ButtonsSplitBySlot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenHudEquipButtonsSplitTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = SpawnTestPlayer(World);
	UHuntHudWidget* Hud = CreateTestHud(World, Player);
	if (!TestNotNull(TEXT("플레이어 스폰"), Player) || !TestNotNull(TEXT("HUD 생성"), Hud))
	{
		DestroyTestWorld(World);
		return false;
	}

	// Slate 위젯을 실제로 만들어야 자리표시 트리가 서고, 그때 버튼 넷이 생긴다.
	// 반환값을 **끝까지 들고 있어야** 한다 — 놓으면 그 자리에서 `NativeDestruct` 가 돌아
	// `BindToCharacter(nullptr)` 로 주인이 끊기고, 이후 새로고침이 빈 인벤토리를 본다.
	TSharedRef<SWidget> Built = Hud->TakeWidget();

	if (!TestTrue(TEXT("몽둥이를 한 개 갖는다"), GiveItemFromTable(Player->GetInventory(), ClubRowName)))
	{
		DestroyTestWorld(World);
		return false;
	}

	Hud->RefreshDisplay();

	for (const EEquipSlot Slot : UHuntHudWidget::GetEquipButtonSlots())
	{
		const FName ButtonName = UHuntHudWidget::GetEquipButtonWidgetName(Slot);
		UButton* const SlotButton = Hud->WidgetTree->FindWidget<UButton>(ButtonName);
		if (!TestNotNull(*FString::Printf(TEXT("%s 가 트리에 있다"), *ButtonName.ToString()), SlotButton))
		{
			continue;
		}

		// 표에 있는 장비 행은 몽둥이(`Weapon`) 하나뿐이다 — **그 버튼만** 펴져 있어야 한다.
		const ESlateVisibility Expected =
			(Slot == EEquipSlot::Weapon) ? ESlateVisibility::Visible : ESlateVisibility::Collapsed;
		TestEqual(*FString::Printf(TEXT("%s 의 펴짐 여부"), *ButtonName.ToString()),
			static_cast<int32>(SlotButton->GetVisibility()), static_cast<int32>(Expected));
	}

	// 버튼 넷이 서로 다른 위젯이라야 부위별로 접히고 펴진다 (한 칸을 넷이 나눠 쓰면 여기서 걸린다).
	TestNotEqual(TEXT("무기 버튼과 도구 버튼은 다른 위젯이다"),
		static_cast<const UWidget*>(Hud->WidgetTree->FindWidget<UButton>(
			UHuntHudWidget::GetEquipButtonWidgetName(EEquipSlot::Weapon))),
		static_cast<const UWidget*>(Hud->WidgetTree->FindWidget<UButton>(
			UHuntHudWidget::GetEquipButtonWidgetName(EEquipSlot::Tool))));

	DestroyTestWorld(World);
	return true;
}

// ROADMAP M9-07b — **사람 손을 안 거친 장착**이 화면에 뜨는지. 시작 지급은 버튼을 누르는 사람이
// 없어서, 버튼 탭 길만 화면을 고치던 때는 몽둥이가 손에 붙고도 HUD 에 버튼이 없는 화면이 찍혔다
// (루프#169 3층 캡처에서 실제로 그렇게 나왔다). 그래서 `RefreshDisplay()` 를 일부러 안 부른다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenHudAutoEquipTest,
	"Snarehaven.HuntHud.Equip.AutoEquipShowsButton",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenHudAutoEquipTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = SpawnTestPlayer(World);
	UHuntHudWidget* Hud = CreateTestHud(World, Player);
	if (!TestNotNull(TEXT("플레이어 스폰"), Player) || !TestNotNull(TEXT("HUD 생성"), Hud))
	{
		DestroyTestWorld(World);
		return false;
	}

	// 위 버튼 검사와 같은 이유로 끝까지 들고 있는다 (놓으면 주인이 끊긴다).
	TSharedRef<SWidget> Built = Hud->TakeWidget();

	UEquipmentComponent* const Equipment = Player->GetEquipment();
	if (!TestTrue(TEXT("사람 메시를 올렸다"), Player->ApplyPlayerBody())
		|| !TestNotNull(TEXT("장비 컴포넌트가 붙어 있다"), Equipment))
	{
		DestroyTestWorld(World);
		return false;
	}

	const FName ButtonName = UHuntHudWidget::GetEquipButtonWidgetName(EEquipSlot::Weapon);
	UButton* const WeaponButton = Hud->WidgetTree->FindWidget<UButton>(ButtonName);
	if (!TestNotNull(*FString::Printf(TEXT("%s 가 트리에 있다"), *ButtonName.ToString()), WeaponButton))
	{
		DestroyTestWorld(World);
		return false;
	}

	// 시작 상태 — 가진 것도 든 것도 없다 (인벤토리 캐시는 백엔드가 서기 전이라 비어 있다).
	TestEqual(TEXT("처음엔 무기 버튼이 접혀 있다"),
		static_cast<int32>(WeaponButton->GetVisibility()), static_cast<int32>(ESlateVisibility::Collapsed));

	// 지급 응답이 준 **아이템 ID** 로 건다 — 시작 장비가 손에 걸리는 길과 같다(행 이름은 표가 찾는다).
	const UDataTable* const ItemTable = LoadObject<UDataTable>(nullptr, ItemTablePath);
	const FItemRow* const ClubRow = (ItemTable != nullptr)
		? ItemTable->FindRow<FItemRow>(FName(ClubRowName), TEXT("FSnarehavenHudAutoEquipTest"))
		: nullptr;
	if (!TestNotNull(TEXT("표에서 몽둥이 행을 읽었다"), ClubRow))
	{
		DestroyTestWorld(World);
		return false;
	}

	TestTrue(TEXT("버튼을 누르는 사람 없이 손에 건다"), Equipment->EquipItemByItemID(ClubRow->ItemID));

	// 여기서 `RefreshDisplay()` 를 부르지 않는다 — 부르면 배선이 빠져도 통과해 버린다.
	TestEqual(TEXT("자동 장착만으로 무기 버튼이 펴진다"),
		static_cast<int32>(WeaponButton->GetVisibility()), static_cast<int32>(ESlateVisibility::Visible));
	TestTrue(TEXT("글자가 `해제` 다 — 들고 있는 상태로 그려진다"),
		Hud->GetEquipButtonText(EEquipSlot::Weapon).ToString().Contains(TEXT("해제")));

	// 벗을 때도 같은 길로 화면이 따라온다 (인벤토리가 비어 있으니 가리킬 것이 없어져 접힌다).
	TestTrue(TEXT("코드로 벗긴다"), Equipment->UnequipSlot(EEquipSlot::Weapon));
	TestEqual(TEXT("벗으면 버튼이 다시 접힌다"),
		static_cast<int32>(WeaponButton->GetVisibility()), static_cast<int32>(ESlateVisibility::Collapsed));

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenHudVitalsGaugeTest, "Snarehaven.HuntHud.Vitals.GaugesFollowPlayer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// M11-02 — 체력·스태미나 게이지가 `UPlayerVitalsComponent` 의 **알림만으로** 움직이는지.
// 일부러 `RefreshDisplay()` 를 부르지 않는다: 여기서 막대가 안 움직이면 배선이 빠진 것이고,
// 반대로 HUD 가 매 프레임 되묻는 구조로 바뀌어도 이 검사는 통과해 버리지 않는다.
bool FSnarehavenHudVitalsGaugeTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = SpawnTestPlayer(World);
	UHuntHudWidget* Hud = CreateWidget<UHuntHudWidget>(World, UHuntHudWidget::StaticClass());
	if (!TestNotNull(TEXT("플레이어 스폰"), Player) || !TestNotNull(TEXT("HUD 생성"), Hud))
	{
		DestroyTestWorld(World);
		return false;
	}

	// 막대 위젯을 보려면 자리표시 트리가 먼저 서야 한다.
	Hud->TakeWidget();
	Hud->BindToCharacter(Player);

	UPlayerVitalsComponent* const Vitals = Player->GetPlayerVitals();
	UProgressBar* const HealthBar = Hud->WidgetTree->FindWidget<UProgressBar>(TEXT("PlayerHealthBar"));
	UProgressBar* const StaminaBar = Hud->WidgetTree->FindWidget<UProgressBar>(TEXT("PlayerStaminaBar"));
	if (!TestNotNull(TEXT("플레이어에 체력·스태미나 컴포넌트가 붙어 있다"), Vitals)
		|| !TestNotNull(TEXT("체력 게이지가 트리에 있다"), HealthBar)
		|| !TestNotNull(TEXT("스태미나 게이지가 트리에 있다"), StaminaBar))
	{
		DestroyTestWorld(World);
		return false;
	}

	if (!TestTrue(TEXT("검사 전용 값으로 초기화"), Vitals->InitializeFromRow(MakeTestVitalsRow())))
	{
		DestroyTestWorld(World);
		return false;
	}

	TestEqual(TEXT("갓 초기화하면 체력 게이지가 꽉 찬다"), Hud->GetPlayerHealthPercent(), 1.f, TestPercentTolerance);
	TestEqual(TEXT("갓 초기화하면 스태미나 게이지가 꽉 찬다"), Hud->GetPlayerStaminaPercent(), 1.f, TestPercentTolerance);
	TestEqual(TEXT("막대도 알림만으로 꽉 찬다"), StaminaBar->GetPercent(), 1.f, TestPercentTolerance);

	// 1초 달리면 40 중 20 이 빠진다 — 게이지는 반이 된다.
	Vitals->SetMoveInput(true);
	Vitals->SetRunInput(true);
	Vitals->AdvanceTime(TestRunSeconds);

	TestEqual(TEXT("달리면 스태미나 게이지가 반이 된다"),
		Hud->GetPlayerStaminaPercent(), TestHalfPercent, TestPercentTolerance);
	TestEqual(TEXT("게이지는 컴포넌트 값 그대로다"), Hud->GetPlayerStaminaPercent(),
		Vitals->GetStamina() / Vitals->GetMaxStamina(), TestPercentTolerance);
	TestEqual(TEXT("막대가 RefreshDisplay 없이 따라 내려온다"),
		StaminaBar->GetPercent(), TestHalfPercent, TestPercentTolerance);
	TestEqual(TEXT("달리기만으로는 체력이 안 준다"), HealthBar->GetPercent(), 1.f, TestPercentTolerance);

	// 캐릭터를 놓으면 게이지는 빈다 — 끊긴 뒤에도 옛 값을 보여 주면 죽은 사람 체력이 남는다.
	Hud->BindToCharacter(nullptr);
	TestEqual(TEXT("배선을 끊으면 체력 게이지가 빈다"), Hud->GetPlayerHealthPercent(), 0.f, TestPercentTolerance);
	TestEqual(TEXT("배선을 끊으면 스태미나 게이지가 빈다"), Hud->GetPlayerStaminaPercent(), 0.f, TestPercentTolerance);

	// 끊은 뒤 값이 더 변해도 HUD 가 따라가면 안 된다 (델리게이트를 안 뗀 것이다).
	Vitals->AdvanceTime(TestRunSeconds);
	TestEqual(TEXT("끊긴 뒤 알림은 게이지를 안 건드린다"),
		Hud->GetPlayerStaminaPercent(), 0.f, TestPercentTolerance);

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenHudVitalsPlacementTest,
	"Snarehaven.HuntHud.Tree.VitalsSitInBottomCorners",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// 레퍼런스 시트 `docs/reference/hud_ingame.md` 자리를 지키는지 (M15-02):
// 오른쪽 아래 = 기력 번개 10칸(공격·포획 버튼 **아래** 맨 끝 줄), 왼쪽 아래 = 작은 빨간 체력 알약.
// 아래 기준이라 Y 는 음수다 — **더 작을수록 위**다.
bool FSnarehavenHudVitalsPlacementTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	UHuntHudWidget* Hud = CreateWidget<UHuntHudWidget>(World, UHuntHudWidget::StaticClass());
	if (!TestNotNull(TEXT("HUD 생성"), Hud))
	{
		DestroyTestWorld(World);
		return false;
	}

	Hud->TakeWidget();

	UProgressBar* const HealthBar = Hud->WidgetTree->FindWidget<UProgressBar>(TEXT("PlayerHealthBar"));
	UProgressBar* const StaminaBar = Hud->WidgetTree->FindWidget<UProgressBar>(TEXT("PlayerStaminaBar"));
	UButton* const CaptureButton = Hud->WidgetTree->FindWidget<UButton>(TEXT("CaptureButton"));
	UBorder* const HealthPill = Hud->WidgetTree->FindWidget<UBorder>(TEXT("PlayerHealthPill"));
	if (!TestNotNull(TEXT("체력 게이지가 있다"), HealthBar) || !TestNotNull(TEXT("스태미나 게이지가 있다"), StaminaBar)
		|| !TestNotNull(TEXT("포획 버튼이 있다"), CaptureButton) || !TestNotNull(TEXT("체력 알약이 있다"), HealthPill))
	{
		DestroyTestWorld(World);
		return false;
	}

	const UCanvasPanelSlot* const PillSlot = Cast<UCanvasPanelSlot>(HealthPill->Slot);
	const UCanvasPanelSlot* const StaminaSlot = Cast<UCanvasPanelSlot>(StaminaBar->Slot);
	const UCanvasPanelSlot* const CaptureSlot = Cast<UCanvasPanelSlot>(CaptureButton->Slot);
	if (!TestNotNull(TEXT("체력 알약이 캔버스에 놓였다"), PillSlot)
		|| !TestNotNull(TEXT("스태미나 게이지가 캔버스에 놓였다"), StaminaSlot)
		|| !TestNotNull(TEXT("포획 버튼이 캔버스에 놓였다"), CaptureSlot))
	{
		DestroyTestWorld(World);
		return false;
	}

	TestEqual(TEXT("기력은 오른쪽 아래 기준이다"), StaminaSlot->GetAnchors().Minimum, FVector2D(1.f, 1.f));
	TestEqual(TEXT("체력 알약은 왼쪽 아래 기준이다"), PillSlot->GetAnchors().Minimum, FVector2D(0.f, 1.f));
	TestTrue(TEXT("포획 버튼 아랫변이 기력 줄 윗변보다 위다(안 겹친다)"),
		CaptureSlot->GetPosition().Y <= StaminaSlot->GetPosition().Y - StaminaSlot->GetSize().Y);
	TestEqual(TEXT("체력 판자는 나무색(DESIGN 7.1)"), HealthPill->GetBrushColor(), SnareUiStyle::UiWoodButton());

	// 스태미나 = 수영 전용 (DESIGN 3.1) — 수영(8.9)이 들어와 칸이 보인다(8.9 "착수 때 같이 볼 것 ②").
	TestNotEqual(TEXT("스태미나 칸이 보인다"), StaminaBar->GetVisibility(), ESlateVisibility::Collapsed);
	TestNotEqual(TEXT("체력 칸은 보인다"), HealthBar->GetVisibility(), ESlateVisibility::Collapsed);
	for (int32 Index = 0; Index < TestGaugeSegmentCount; ++Index)
	{
		const UImage* const Bolt = Hud->WidgetTree->FindWidget<UImage>(
			FName(*FString::Printf(TEXT("PlayerStaminaBolt_%d"), Index)));
		if (TestNotNull(TEXT("기력 번개 칸이 있다"), Bolt))
		{
			TestNotEqual(TEXT("번개 칸도 같이 보인다"), Bolt->GetVisibility(), ESlateVisibility::Collapsed);
		}
	}

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenHudVitalsSegmentTest,
	"Snarehaven.HuntHud.Vitals.GaugeShowsSegments",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// M11-02b — 게이지가 레퍼런스 시트(`docs/reference/hud_ingame.md` "오른쪽 아래 = 기력")대로
// **10칸으로 끊긴 한 줄**인지. 세 가지를 본다: 칸 가림막 수 · 막대 값이 칸 단위로 끊기는지 ·
// 색이 팔레트(DESIGN 2.1) 값 그대로인지. 색까지 보는 이유는 여기서 새 색을 만들지 않았다는
// 약속이 코드에 남아야 다음 바퀴가 슬쩍 다른 색을 칠하지 못하기 때문이다.
bool FSnarehavenHudVitalsSegmentTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = SpawnTestPlayer(World);
	UHuntHudWidget* Hud = CreateWidget<UHuntHudWidget>(World, UHuntHudWidget::StaticClass());
	if (!TestNotNull(TEXT("플레이어 스폰"), Player) || !TestNotNull(TEXT("HUD 생성"), Hud))
	{
		DestroyTestWorld(World);
		return false;
	}

	Hud->TakeWidget();
	Hud->BindToCharacter(Player);

	UPlayerVitalsComponent* const Vitals = Player->GetPlayerVitals();
	UProgressBar* const StaminaBar = Hud->WidgetTree->FindWidget<UProgressBar>(TEXT("PlayerStaminaBar"));
	UProgressBar* const HealthBar = Hud->WidgetTree->FindWidget<UProgressBar>(TEXT("PlayerHealthBar"));
	if (!TestNotNull(TEXT("플레이어에 체력·스태미나 컴포넌트가 붙어 있다"), Vitals)
		|| !TestNotNull(TEXT("스태미나 게이지가 트리에 있다"), StaminaBar)
		|| !TestNotNull(TEXT("체력 게이지가 트리에 있다"), HealthBar))
	{
		DestroyTestWorld(World);
		return false;
	}

	// ① 번개 칸 — 레퍼런스 시트대로 10개. 이름으로 찾는다(3층 캡처에서 세는 것과 같은 칸).
	int32 Bolts = 0;
	for (int32 Index = 0; Index < TestGaugeSegmentCount; ++Index)
	{
		if (Hud->WidgetTree->FindWidget<UImage>(
				FName(*FString::Printf(TEXT("PlayerStaminaBolt_%d"), Index))) != nullptr)
		{
			++Bolts;
		}
	}
	TestEqual(TEXT("기력이 번개 10칸이다"), Bolts, TestGaugeSegmentCount);

	if (!TestTrue(TEXT("검사 전용 값으로 초기화"), Vitals->InitializeFromRow(MakeTestVitalsRow())))
	{
		DestroyTestWorld(World);
		return false;
	}

	// ② 칸 단위로 끊긴다 — 0.3초 달리면 40 중 6 이 빠져 **0.85**(8.5칸)가 남는다. 막대는 9칸을 켠다.
	Vitals->SetMoveInput(true);
	Vitals->SetRunInput(true);
	Vitals->AdvanceTime(TestPartialRunSeconds);

	TestEqual(TEXT("실제 비율은 반올림하지 않는다"),
		Hud->GetPlayerStaminaPercent(), TestPartialPercent, TestPercentTolerance);
	TestEqual(TEXT("막대는 칸 단위로 올려 끊는다"),
		StaminaBar->GetPercent(), TestPartialSegmentPercent, TestPercentTolerance);
	TestEqual(TEXT("번개도 9칸이 켜진다"), CountLitBolts(*Hud), TestPartialLitBolts);

	// ③ 조금이라도 남으면 한 칸은 켜 둔다 — 내림이면 "아직 달릴 수 있다" 가 빈 게이지로 보인다.
	Vitals->AdvanceTime(TestNearlyEmptyRunSeconds);
	TestTrue(TEXT("바닥 직전이라도 실제 비율이 0 은 아니다"), Hud->GetPlayerStaminaPercent() > 0.f);
	TestEqual(TEXT("바닥 직전이라도 한 칸은 켜져 있다"),
		StaminaBar->GetPercent(), 1.f / TestGaugeSegmentCount, TestPercentTolerance);
	TestEqual(TEXT("바닥 직전이라도 번개 한 칸은 켜져 있다"), CountLitBolts(*Hud), 1);

	// ④ 색은 팔레트(DESIGN 2.1) 값 그대로다 — 켜진 칸은 번개 노랑, 소진 칸은 진갈색을 옅게(새 색 없음).
	const UImage* const LastBolt = Hud->WidgetTree->FindWidget<UImage>(
		FName(*FString::Printf(TEXT("PlayerStaminaBolt_%d"), TestGaugeSegmentCount - 1)));
	if (TestNotNull(TEXT("마지막 번개 칸"), LastBolt))
	{
		const FLinearColor Spent = LastBolt->GetColorAndOpacity();
		TestEqual(TEXT("소진 칸은 진갈색 계열"), Spent.CopyWithNewOpacity(1.f), SnareUiStyle::UiTextOnButton());
		TestTrue(TEXT("소진 칸은 옅다"), Spent.A < 1.f);
	}

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenHudBuffSlotsTest, "Snarehaven.HuntHud.Buff.SlotsFollowVitals",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// M13-16 — 켜진 버프마다 칸 하나가 보이고 남은 초(올림)를 적으며, 끝나면 칸이 사라지는지.
// 잡는 것: 버프가 걸려도 HUD 에 안 뜨는 것(결함 원문) · 끝난 버프 칸이 남는 것 · 다른 종류 칸에 글자가 섞이는 것.
bool FSnarehavenHudBuffSlotsTest::RunTest(const FString& Parameters)
{
	// 검사 전용 값 (Verification.md 6 — 설계의 180초가 아니다).
	constexpr float TestBuffValue = 0.5f;
	constexpr float TestShortBuffSeconds = 30.f;
	constexpr float TestLongBuffSeconds = 60.f;
	constexpr float TestStepSeconds = 10.5f;

	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = SpawnTestPlayer(World);
	UHuntHudWidget* Hud = CreateWidget<UHuntHudWidget>(World, UHuntHudWidget::StaticClass());
	if (!TestNotNull(TEXT("플레이어 스폰"), Player) || !TestNotNull(TEXT("HUD 생성"), Hud))
	{
		DestroyTestWorld(World);
		return false;
	}
	Hud->TakeWidget();
	Hud->BindToCharacter(Player);
	UPlayerVitalsComponent* const Vitals = Player->GetPlayerVitals();
	if (!TestNotNull(TEXT("바이탈"), Vitals) || !TestTrue(TEXT("검사 전용 값으로 초기화"), Vitals->InitializeFromRow(MakeTestVitalsRow())))
	{
		DestroyTestWorld(World);
		return false;
	}
	TestNotNull(TEXT("버프 줄이 트리에 있다"), Hud->WidgetTree->FindWidget(TEXT("BuffRow")));

	Hud->UpdateBuffSlots();
	TestEqual(TEXT("버프가 없으면 칸도 없다"), Hud->GetVisibleBuffSlotCount(), 0);

	Vitals->ApplyBuff(EBuffKind::Speed, TestBuffValue, TestShortBuffSeconds);
	Vitals->ApplyBuff(EBuffKind::Regen, TestBuffValue, TestLongBuffSeconds);
	Hud->UpdateBuffSlots();
	TestEqual(TEXT("켜진 버프 둘 = 칸 둘"), Hud->GetVisibleBuffSlotCount(), 2);
	TestEqual(TEXT("이동 칸 = 남은 초"), Hud->GetBuffSlotText(EBuffKind::Speed).ToString(), FString(TEXT("이동 30초")));
	TestEqual(TEXT("재생 칸 = 남은 초"), Hud->GetBuffSlotText(EBuffKind::Regen).ToString(), FString(TEXT("재생 60초")));
	TestTrue(TEXT("안 걸린 추위 칸은 비었다"), Hud->GetBuffSlotText(EBuffKind::ColdResist).IsEmpty());

	// 19.5초 남음 → 올려서 20초 (0.x 초를 0 으로 보여 주면 이미 끝난 것처럼 읽힌다).
	Vitals->AdvanceTime(TestStepSeconds);
	Hud->UpdateBuffSlots();
	TestEqual(TEXT("시간이 가면 초가 준다(올림)"), Hud->GetBuffSlotText(EBuffKind::Speed).ToString(), FString(TEXT("이동 20초")));

	// 이동 버프가 끝난다(30 − 31.5 < 0) — 칸이 사라지고 재생 칸만 남는다.
	Vitals->AdvanceTime(TestStepSeconds * 2.f);
	Hud->UpdateBuffSlots();
	TestEqual(TEXT("끝난 버프 칸은 사라진다"), Hud->GetVisibleBuffSlotCount(), 1);
	TestTrue(TEXT("끝난 이동 칸 글자도 없다"), Hud->GetBuffSlotText(EBuffKind::Speed).IsEmpty());
	TestEqual(TEXT("남은 재생 칸"), Hud->GetBuffSlotText(EBuffKind::Regen).ToString(), FString(TEXT("재생 29초")));

	Vitals->AdvanceTime(TestLongBuffSeconds);
	Hud->UpdateBuffSlots();
	TestEqual(TEXT("다 끝나면 칸이 없다"), Hud->GetVisibleBuffSlotCount(), 0);

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenHudBuffSlotsDesignedTreeTest, "Snarehaven.HuntHud.Buff.SlotsWithDesignedTree",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// M13-22 ③ 버프 칸이 C++ 자리표시 트리에만 있어서, WBP 디자인이 들어오면(자리표시를 안 세우면) 칸이 사라졌다.
// 잡는 것: 디자인 트리에 버프 칸이 없을 때 칸이 0 인 것 · 디자인에 칸이 있는데 그 칸을 안 쓰고 새로 만드는 것.
bool FSnarehavenHudBuffSlotsDesignedTreeTest::RunTest(const FString& Parameters)
{
	constexpr float TestBuffValue = 0.5f;
	constexpr float TestBuffSeconds = 30.f;
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = SpawnTestPlayer(World);
	if (!TestNotNull(TEXT("플레이어 스폰"), Player))
	{
		DestroyTestWorld(World);
		return false;
	}
	UPlayerVitalsComponent* const Vitals = Player->GetPlayerVitals();
	if (!TestNotNull(TEXT("바이탈"), Vitals) || !TestTrue(TEXT("검사 전용 값으로 초기화"), Vitals->InitializeFromRow(MakeTestVitalsRow())))
	{
		DestroyTestWorld(World);
		return false;
	}
	Vitals->ApplyBuff(EBuffKind::Speed, TestBuffValue, TestBuffSeconds);

	// 디자인 트리 = 루트 캔버스에 자식이 하나 이상(자리표시를 안 세운다). bWithSlot 이면 이동 칸을 디자이너가 둔다.
	for (const bool bWithSlot : { false, true })
	{
		const TCHAR* const Case = bWithSlot ? TEXT("WBP 칸 있음") : TEXT("WBP 칸 없음");
		UHuntHudWidget* const Hud = CreateWidget<UHuntHudWidget>(World, UHuntHudWidget::StaticClass());
		if (!TestNotNull(TEXT("HUD 생성"), Hud))
		{
			continue;
		}
		UCanvasPanel* const Root = Hud->WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("DesignedRoot"));
		Hud->WidgetTree->RootWidget = Root;
		Root->AddChild(Hud->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("DesignedLabel")));
		UBorder* DesignedSlot = nullptr;
		UTextBlock* DesignedText = nullptr;
		if (bWithSlot)
		{
			DesignedSlot = Hud->WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("BuffSlot_Speed"));
			DesignedText = Hud->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("BuffSlotText_Speed"));
			DesignedSlot->SetContent(DesignedText);
			Root->AddChild(DesignedSlot);
		}
		Hud->TakeWidget();
		Hud->BindToCharacter(Player);
		TestNull(*FString::Printf(TEXT("%s: 자리표시 트리는 안 세운다"), Case), Hud->WidgetTree->FindWidget(TEXT("AttackButton")));
		Hud->UpdateBuffSlots();
		TestEqual(*FString::Printf(TEXT("%s: 켜진 버프 하나 = 칸 하나"), Case), Hud->GetVisibleBuffSlotCount(), 1);
		TestEqual(*FString::Printf(TEXT("%s: 이동 칸 글자"), Case), Hud->GetBuffSlotText(EBuffKind::Speed).ToString(), FString(TEXT("이동 30초")));
		TestNotNull(*FString::Printf(TEXT("%s: 나머지 칸은 C++ 가 만든다"), Case), Hud->WidgetTree->FindWidget(TEXT("BuffSlot_Regen")));
		if (bWithSlot)
		{
			TestTrue(TEXT("WBP 칸을 그대로 쓴다"), Hud->WidgetTree->FindWidget(TEXT("BuffSlot_Speed")) == DesignedSlot);
			TestEqual(TEXT("WBP 글자에 남은 초"), DesignedText->GetText().ToString(), FString(TEXT("이동 30초")));
			TestTrue(TEXT("WBP 칸은 디자이너 자리 그대로(버프 줄로 안 옮긴다)"), DesignedSlot->GetParent() == Root);
		}
		AddInfo(FString::Printf(TEXT("[HuntHud] %s - 보이는 버프 칸 %d · 이동 칸 \"%s\""), Case,
			Hud->GetVisibleBuffSlotCount(), *Hud->GetBuffSlotText(EBuffKind::Speed).ToString()));
	}

	DestroyTestWorld(World);
	return true;
}

// M15-08(INBOX ②) — 도구 버튼이 **눈앞의 대상**에 맞는 도구를 드는지. 전에는 표 순서대로 골라
// 나무 앞에서도 낚싯대가 들렸다. 어느 도구가 맞는지는 채집물(`CanGatherWithTool`)과 표(`FishingSpeedMul`)가 정한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenHudContextToolTest, "Snarehaven.HuntHud.Tool.PicksToolForNearbyTarget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenHudContextToolTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = SpawnTestPlayer(World);
	if (!TestNotNull(TEXT("플레이어 스폰"), Player))
	{
		DestroyTestWorld(World);
		return false;
	}
	// 낚시 표(던지는 거리)는 BeginPlay 가 읽는다. 사거리는 그 뒤에 다시 넣는다 — 검사 전용 값이라 표보다 이겨야 한다.
	Player->DispatchBeginPlay();
	Player->GetMeleeAttack()->SetAttackSettings(TestAttackDamage, TestAttackRangeCm, TestAttackIntervalSeconds);

	UHuntHudWidget* const Hud = CreateTestHud(World, Player);
	UGatherComponent* const Gather = Player->GetGather();
	UTapFishingComponent* const Fishing = Player->FindComponentByClass<UTapFishingComponent>();
	UDataTable* const ItemTable = LoadObject<UDataTable>(nullptr, ItemTablePath);
	if (!TestNotNull(TEXT("HUD 생성"), Hud) || !TestNotNull(TEXT("채집 컴포넌트"), Gather)
		|| !TestNotNull(TEXT("낚시 컴포넌트"), Fishing) || !TestNotNull(TEXT("아이템 표"), ItemTable))
	{
		DestroyTestWorld(World);
		return false;
	}

	// 기대값(아이템 ID)은 표에서만 온다 — 여기 적으면 표가 바뀔 때 한쪽만 늙는다.
	const auto ItemIdOf = [ItemTable](const TCHAR* RowName)
	{
		const FItemRow* const Row = ItemTable->FindRow<FItemRow>(FName(RowName), TEXT("ContextTool"));
		return Row != nullptr ? Row->ItemID : NAME_None;
	};
	const FName HatchetID = ItemIdOf(HatchetRowName);
	const FName ChainsawID = ItemIdOf(ChainsawRowName);
	const FName PickaxeID = ItemIdOf(PickaxeRowName);
	const FName RodID = ItemIdOf(RodRowName);
	if (!TestFalse(TEXT("표에 도구 넷이 있다"),
		HatchetID.IsNone() || ChainsawID.IsNone() || PickaxeID.IsNone() || RodID.IsNone()))
	{
		DestroyTestWorld(World);
		return false;
	}

	// 가진 것을 바꾸는 길은 백엔드 스냅샷 하나다 (CLAUDE.md 8).
	const auto Own = [Player](const TArray<FName>& ItemIDs)
	{
		TArray<FInventorySlot> Slots;
		for (const FName& ItemID : ItemIDs)
		{
			FInventorySlot Slot;
			Slot.ItemID = ItemID;
			Slot.Count = 1;
			Slots.Add(Slot);
		}
		return Player->GetInventory()->ApplyBackendSnapshot(Slots);
	};
	// 테스트 전용 채집물 — 도구 요구만 실제 아이템 ID 다(맞는지는 채집물이 판단하므로).
	const auto SpawnGatherable = [World](float DistanceCm, FName RequiredToolID, FName BonusToolID)
	{
		AGatherableActor* const Actor =
			World->SpawnActor<AGatherableActor>(FVector(DistanceCm, 0.f, 0.f), FRotator::ZeroRotator);
		FGatherableRow Row;
		Row.GatherableID = TEXT("gatherable.hud_tool_test");
		Row.GrantItemID = TEXT("item.hud_tool_test");
		Row.GrantCount = 1;
		Row.RequiredToolItemID = RequiredToolID;
		Row.BonusToolItemID = BonusToolID;
		if (Actor != nullptr)
		{
			Actor->InitializeFromRow(Row);
		}
		return Actor;
	};

	TestTrue(TEXT("낚싯대와 손도끼를 갖는다"), Own({ RodID, HatchetID }));
	TestTrue(TEXT("가진 도구가 있으면 도구 버튼이 뜬다"), Hud->IsEquipButtonVisible(EEquipSlot::Tool));
	TestTrue(TEXT("가까이 아무것도 없으면 맞는 도구가 없다"), Hud->GetContextToolItem().IsNone());

	// ── 나무 앞 ──
	AGatherableActor* const Tree = SpawnGatherable(TestNearDistanceCm, HatchetID, ChainsawID);
	// 하이라이트(`RefreshTarget`)는 맨손이면 나무를 비추지 않는다 — 그래서 도구 고르기는 도구를 안 따지는 길로 본다.
	if (!TestNotNull(TEXT("나무를 세운다"), Tree)
		|| !TestNull(TEXT("맨손이면 나무에 불이 안 들어온다"), Gather->RefreshTarget())
		|| !TestTrue(TEXT("도구를 안 따지면 나무가 가장 가깝다"), Gather->FindNearestNeedingTool() == Tree))
	{
		DestroyTestWorld(World);
		return false;
	}
	TestEqual(TEXT("나무 앞에서는 낚싯대가 아니라 손도끼다"),
		Hud->GetEquipButtonItem(EEquipSlot::Tool), FName(HatchetRowName));
	TestTrue(TEXT("탭하면 손도끼가 들린다"), Hud->ToggleEquipButtonItem(EEquipSlot::Tool));
	TestEqual(TEXT("도구 칸이 손도끼다"),
		Player->GetEquipment()->GetEquippedItem(EEquipSlot::Tool), FName(HatchetRowName));
	TestTrue(TEXT("글자가 `해제` 로 바뀐다"),
		Hud->GetEquipButtonText(EEquipSlot::Tool).ToString().Contains(TEXT("해제")));

	// 덤을 주는 도구를 가지면 그것부터 (DESIGN 13.5 전기톱 +1).
	TestTrue(TEXT("전기톱도 갖는다"), Own({ RodID, HatchetID, ChainsawID }));
	TestEqual(TEXT("전기톱이 있으면 전기톱을 든다"),
		Hud->GetEquipButtonItem(EEquipSlot::Tool), FName(ChainsawRowName));

	// ── 광맥 앞 ──
	Tree->Destroy();
	AGatherableActor* const Ore = SpawnGatherable(TestNearDistanceCm, PickaxeID, NAME_None);
	TestTrue(TEXT("광맥이 가장 가까운 채집물이 된다"), Ore != nullptr && Gather->FindNearestNeedingTool() == Ore);
	TestTrue(TEXT("곡괭이가 없으면 맞는 도구도 없다"), Hud->GetContextToolItem().IsNone());
	TestTrue(TEXT("곡괭이를 갖는다"), Own({ RodID, HatchetID, ChainsawID, PickaxeID }));
	TestEqual(TEXT("광맥 앞에서는 곡괭이다"),
		Hud->GetEquipButtonItem(EEquipSlot::Tool), FName(PickaxeRowName));

	// ── 낚시 포인트 앞 ──
	Ore->Destroy();
	TestNull(TEXT("채집물이 없어진다"), Gather->FindNearestNeedingTool());
	AFishingSpotActor* const Spot = World->SpawnActor<AFishingSpotActor>(FVector::ZeroVector, FRotator::ZeroRotator);
	if (Spot != nullptr && !Spot->HasActorBegunPlay())
	{
		Spot->DispatchBeginPlay();
	}
	TestTrue(TEXT("던질 거리 안에 포인트가 있다"), Fishing->HasSpotInCastRange());
	TestEqual(TEXT("낚시 포인트 앞에서는 낚싯대다"),
		Hud->GetEquipButtonItem(EEquipSlot::Tool), FName(RodRowName));

	AddInfo(FString::Printf(TEXT("[HuntHud] 도구 자동 - 나무 %s · 나무+전기톱 %s · 광맥 %s · 낚시터 %s (던지는 거리 %.0fcm)"),
		HatchetRowName, ChainsawRowName, PickaxeRowName, RodRowName, Fishing->GetCastRangeCm()));

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenHudHeldGearTest, "Snarehaven.HuntHud.Gear.ShowsHeldToolEffect",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// M15-11 (INBOX 2026-09-25 ⑤) — 망원경·위성 통신기는 먹는 것도 놓는 것도 아니라 들고 있는 것 말고는
// "쓰고 있다" 는 표시가 없었다. 도구 칸이 바뀌는 **알림만으로** 줄이 서고 접히는지 본다 —
// 일부러 `RefreshDisplay()` 를 부르지 않는다(부르면 배선이 빠져도 통과해 버린다).
// 기대값(이름·배율)은 코드에 적지 않고 **아이템 표**에서 읽는다 — 표가 바뀌면 줄도 같이 바뀌어야 한다.
bool FSnarehavenHudHeldGearTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = SpawnTestPlayer(World);
	UHuntHudWidget* Hud = CreateTestHud(World, Player);
	if (!TestNotNull(TEXT("플레이어 스폰"), Player) || !TestNotNull(TEXT("HUD 생성"), Hud))
	{
		DestroyTestWorld(World);
		return false;
	}
	// 자리표시 트리를 세우고 끝까지 들고 있는다 (위 버튼 검사들과 같은 이유).
	TSharedRef<SWidget> Built = Hud->TakeWidget();

	UEquipmentComponent* const Equipment = Player->GetEquipment();
	if (!TestNotNull(TEXT("장비 컴포넌트"), Equipment) || !TestTrue(TEXT("사람 메시를 올렸다"), Player->ApplyPlayerBody()))
	{
		DestroyTestWorld(World);
		return false;
	}

	// 도구 메시가 아직 없는 행도 있다 — 호스트 목록을 옮기는 길로 도구 칸만 채운다
	// (`Snarehaven.Camera.TopDown.BinocularsWidenView` 와 같은 길이다). 목록을 옮기면 장비 알림이 울린다.
	const auto Hold = [Equipment](const TCHAR* ItemID)
	{
		TArray<FEquippedSlot> Slots;
		if (ItemID != nullptr)
		{
			FEquippedSlot& Slot = Slots.AddDefaulted_GetRef();
			Slot.Slot = EEquipSlot::Tool;
			Slot.ItemRowName = Equipment->FindEquipmentRowByItemID(ItemID);
		}
		Equipment->ApplyEquippedSlots(Slots);
	};

	TestTrue(TEXT("빈손이면 줄이 없다"), Hud->GetHeldGearText().IsEmpty());

	// ── 망원경: 이름 + 표의 카메라 배율 ──
	Hold(BinocularsItemID);
	const FItemRow* const Scope = Equipment->GetEquippedRow(EEquipSlot::Tool);
	if (!TestNotNull(TEXT("망원경 행을 들었다"), Scope))
	{
		DestroyTestWorld(World);
		return false;
	}
	const FString ScopeLine = Hud->GetHeldGearText().ToString();
	TestTrue(TEXT("든 장비 줄에 이름이 있다"), ScopeLine.Contains(Scope->DisplayName.ToString()));
	TestTrue(TEXT("줄이 표의 카메라 배율을 그대로 말한다"),
		ScopeLine.Contains(FText::AsNumber(Scope->CameraDistanceMul).ToString()));

	// ── 위성 통신기: 좌표 ──
	Hold(SatelliteItemID);
	const FItemRow* const Radio = Equipment->GetEquippedRow(EEquipSlot::Tool);
	const FString RadioLine = Hud->GetHeldGearText().ToString();
	if (TestNotNull(TEXT("위성 통신기 행을 들었다"), Radio))
	{
		TestTrue(TEXT("표가 좌표를 띄우는 도구라고 적고 있다"), Radio->bShowsMapCoordinates);
		TestTrue(TEXT("줄에 이름이 있다"), RadioLine.Contains(Radio->DisplayName.ToString()));
		TestTrue(TEXT("줄이 좌표를 말한다"), RadioLine.Contains(TEXT("좌표")));
		TestFalse(TEXT("배율 없는 도구엔 시야 얘기가 없다"), RadioLine.Contains(TEXT("시야")));
	}

	// ── 효과 칸이 빈 도구(손도끼)는 줄이 없다 — 빈 칸을 띄우지 않는다 ──
	Hold(TEXT("item.hatchet"));
	const FItemRow* const Hatchet = Equipment->GetEquippedRow(EEquipSlot::Tool);
	if (TestNotNull(TEXT("손도끼 행을 들었다"), Hatchet))
	{
		TestTrue(TEXT("손도끼는 표에 든 동안 듣는 효과가 없다"),
			Hatchet->CameraDistanceMul <= 0.f && !Hatchet->bShowsMapCoordinates && Hatchet->FishingSpeedMul <= 0.f);
		TestTrue(TEXT("효과가 없으면 줄도 없다"), Hud->GetHeldGearText().IsEmpty());
	}

	// ── 내리면 접힌다 ──
	Hold(nullptr);
	TestTrue(TEXT("내리면 줄이 접힌다"), Hud->GetHeldGearText().IsEmpty());
	TestTrue(TEXT("표 밖(빈 행)이면 줄을 지어내지 않는다"), UHuntHudWidget::MakeHeldGearText(nullptr).IsEmpty());

	// ── 소지만으로 효과 (INBOX 2026-09-29 ①): 장착하지 않고 배낭에 있기만 해도 줄이 서고, 버리면 접힌다 ──
	UInventoryComponent* const Inventory = Player->FindComponentByClass<UInventoryComponent>();
	const UDataTable* const ItemTable = LoadObject<UDataTable>(nullptr, TEXT("/Game/Game/Data/Items/DT_ItemTable.DT_ItemTable"));
	if (TestNotNull(TEXT("인벤토리"), Inventory))
	{
		const FName RadioItem(SatelliteItemID);
		TestTrue(TEXT("표가 위성 통신기를 소지 효과로 적고 있다"), Radio != nullptr && Radio->bPassiveWhileCarried);
		TestTrue(TEXT("가진 게 없으면 소지 효과 행도 없다"),
			UHuntHudWidget::FindCarriedPassiveRows(ItemTable, Inventory).Num() == 0);
		FInventorySlot CarriedSlot;
		CarriedSlot.ItemID = RadioItem;
		CarriedSlot.Count = 1;
		TestTrue(TEXT("배낭에 넣었다"), Inventory->ApplyBackendSnapshot({ CarriedSlot }));
		TestTrue(TEXT("도구 칸은 빈손이다"), Equipment->GetEquippedItem(EEquipSlot::Tool).IsNone());
		TestEqual(TEXT("가지고 있으면 소지 효과 행이 하나 나온다"),
			UHuntHudWidget::FindCarriedPassiveRows(ItemTable, Inventory).Num(), 1);
		Hud->UpdateHeldGearLine();
		TestTrue(TEXT("장착 없이도 좌표 줄이 선다"), Hud->GetHeldGearText().ToString().Contains(TEXT("좌표")));
		TestTrue(TEXT("버리면"), Inventory->ApplyBackendSnapshot({}));
		Hud->UpdateHeldGearLine();
		TestTrue(TEXT("줄이 접힌다"), Hud->GetHeldGearText().IsEmpty());
	}

	AddInfo(FString::Printf(TEXT("[HuntHud] 든 장비 줄 - 망원경 '%s' · 위성 통신기 '%s' · 손도끼 %s"),
		*ScopeLine, *RadioLine, Hud->GetHeldGearText().IsEmpty() ? TEXT("없음(접힘)") : TEXT("있음")));

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenHudSheetCornersTest, "Snarehaven.HuntHud.Tree.CornersMatchReferenceSheet",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// M15-02 — 레퍼런스 시트 `docs/reference/hud_ingame.md` 의 "화면 네 귀퉁이만 쓰고 가운데는 비운다" 를
// 2층에서 본다. 3층 캡처를 눈으로 대조하던 항목이라 한 번 맞춰 놓아도 다음 조각이 화면 한가운데에
// 위젯을 얹으면 조용히 어긋났다. 판정은 이름이 아니라 **캔버스 슬롯의 앵커와 보임 상태**로 한다.
// 오른쪽 위(좌표·잔액 알약)는 영상 화면에 없어 둘 다 접혀 있어야 한다 (DESIGN 3.3 「화면 비우기」).
bool FSnarehavenHudSheetCornersTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = SpawnTestPlayer(World);
	UHuntHudWidget* Hud = CreateTestHud(World, Player);
	if (!TestNotNull(TEXT("플레이어 스폰"), Player) || !TestNotNull(TEXT("HUD 생성"), Hud))
	{
		DestroyTestWorld(World);
		return false;
	}

	// 반환값을 붙잡고 있어야 트리가 산다 (`SnarehavenRanchHudTest` 와 같은 함정).
	const TSharedRef<SWidget> SlateWidget = Hud->TakeWidget();
	Hud->RefreshDisplay();

	UCanvasPanel* const Root = Cast<UCanvasPanel>(Hud->WidgetTree->RootWidget);
	if (!TestNotNull(TEXT("HUD 루트가 캔버스다"), Root))
	{
		DestroyTestWorld(World);
		return false;
	}

	// ① 가운데는 비운다 — 보이는 칸은 전부 귀퉁이·가장자리에 붙어 있어야 한다.
	// 앵커 가운데가 화면 가운데 네모(가로·세로 25~75%) 안이면 위반이다.
	TArray<FString> Middle;
	for (int32 Index = 0; Index < Root->GetChildrenCount(); ++Index)
	{
		const UWidget* const Child = Root->GetChildAt(Index);
		const UCanvasPanelSlot* const Slot = Child != nullptr ? Cast<UCanvasPanelSlot>(Child->Slot) : nullptr;
		if (Slot == nullptr || Child->GetVisibility() == ESlateVisibility::Collapsed
			|| Child->GetVisibility() == ESlateVisibility::Hidden)
		{
			continue;
		}
		const FVector2D AnchorMid =
			(Slot->GetAnchors().Minimum + Slot->GetAnchors().Maximum) * 0.5f;
		if (AnchorMid.X > TestScreenEdgeFraction && AnchorMid.X < 1.f - TestScreenEdgeFraction
			&& AnchorMid.Y > TestScreenEdgeFraction && AnchorMid.Y < 1.f - TestScreenEdgeFraction)
		{
			Middle.Add(Child->GetName());
		}
	}
	TestEqual(TEXT("화면 가운데에 보이는 칸이 없다"), FString::Join(Middle, TEXT(", ")), FString());

	// ② 오른쪽 위는 비어 있다 — 잔액 알약(사냥 HUD)과 좌표 알약(지도 위젯) 둘 다 접힘.
	const UWidget* const BalancePill = Hud->WidgetTree->FindWidget(TEXT("BalancePill"));
	UWorldMapScreenWidget* const MapScreen = CreateWidget<UWorldMapScreenWidget>(World, UWorldMapScreenWidget::StaticClass());
	if (!TestNotNull(TEXT("잔액 알약이 트리에 있다"), BalancePill) || !TestNotNull(TEXT("지도 위젯 생성"), MapScreen))
	{
		DestroyTestWorld(World);
		return false;
	}
	const TSharedRef<SWidget> MapSlate = MapScreen->TakeWidget();
	const UWidget* const CoordPill = MapScreen->WidgetTree->FindWidget(TEXT("WorldMapCoordButton"));
	const UWidget* const MapButton = MapScreen->WidgetTree->FindWidget(TEXT("WorldMapOpenButton"));
	if (!TestNotNull(TEXT("좌표 알약이 트리에 있다"), CoordPill) || !TestNotNull(TEXT("지도 버튼이 있다"), MapButton))
	{
		DestroyTestWorld(World);
		return false;
	}
	TestEqual(TEXT("잔액 알약은 접혀 있다"), BalancePill->GetVisibility(), ESlateVisibility::Collapsed);
	TestEqual(TEXT("좌표 알약은 접혀 있다"), CoordPill->GetVisibility(), ESlateVisibility::Collapsed);
	// 좌표 알약을 접어도 지도를 여는 길은 남는다 — 오른쪽 아래 지도 버튼.
	TestNotEqual(TEXT("지도 버튼은 보인다"), MapButton->GetVisibility(), ESlateVisibility::Collapsed);

	// ③ 왼쪽 위: 미니맵은 기본 숨김, 알림 카드는 알림이 있을 때만 (문구 검사는 `Guide.NoticeCardOnlyWithBanner`).
	const UWidget* const MinimapLayer = Hud->WidgetTree->FindWidget(TEXT("MinimapLayer"));
	const UWidget* const NoticeCard = Hud->WidgetTree->FindWidget(TEXT("NoticeCard"));
	if (TestNotNull(TEXT("미니맵 층이 있다"), MinimapLayer) && TestNotNull(TEXT("알림 카드가 있다"), NoticeCard))
	{
		TestEqual(TEXT("미니맵은 기본 숨김"), MinimapLayer->GetVisibility(), ESlateVisibility::Collapsed);
		TestEqual(TEXT("알림이 없으면 카드도 접힘"), NoticeCard->GetVisibility(), ESlateVisibility::Collapsed);
	}

	// ④ 장비 버튼 기둥은 화면에 없다 — 장비는 배낭 창 왼쪽 탭에서 바꾼다(버튼은 접힌 채 남아 입력 경로를 잇는다).
	const UWidget* const EquipColumn = Hud->WidgetTree->FindWidget(TEXT("EquipColumn"));
	if (TestNotNull(TEXT("장비 기둥이 트리에 있다"), EquipColumn))
	{
		TestEqual(TEXT("장비 기둥은 접혀 있다"), EquipColumn->GetVisibility(), ESlateVisibility::Collapsed);
	}

	// ⑤ 오른쪽 아래 엄지 자리: 공격·포획 버튼은 대상이 없어도 숨기지 않고 꺼 둔다 (DESIGN 3.3).
	const UWidget* const CaptureButton = Hud->WidgetTree->FindWidget(TEXT("CaptureButton"));
	const UWidget* const AttackButton = Hud->WidgetTree->FindWidget(TEXT("AttackButton"));
	if (TestNotNull(TEXT("포획 버튼이 있다"), CaptureButton) && TestNotNull(TEXT("공격 버튼이 있다"), AttackButton))
	{
		TestNotEqual(TEXT("포획 버튼은 보인다"), CaptureButton->GetVisibility(), ESlateVisibility::Collapsed);
		TestNotEqual(TEXT("공격 버튼은 보인다"), AttackButton->GetVisibility(), ESlateVisibility::Collapsed);
		TestFalse(TEXT("대상이 없으면 꺼져 있다"), Hud->IsCaptureButtonEnabled());
	}

	int32 Visible = 0;
	for (int32 Index = 0; Index < Root->GetChildrenCount(); ++Index)
	{
		const UWidget* const Child = Root->GetChildAt(Index);
		if (Child != nullptr && Child->GetVisibility() != ESlateVisibility::Collapsed
			&& Child->GetVisibility() != ESlateVisibility::Hidden)
		{
			++Visible;
		}
	}
	AddInfo(FString::Printf(
		TEXT("[HuntHud] 시트 대조 - 가운데 칸 %d개 · 오른쪽 위 접힘(잔액·좌표) · 미니맵 접힘 · 장비 기둥 접힘 · 보이는 칸 %d개"),
		Middle.Num(), Visible));

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenHudLootPopupTest, "Snarehaven.HuntHud.Notice.LootGrantPopup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// M21-03b2 — 지급 대기열이 서버에서 받아들여졌다는 알림(`ULootClaimComponent::OnGrantPopup`)이 알림 카드 글이 되는가,
// 플레이어를 떼면 더 안 듣는가. 글 내용·서버 뒤 시점은 `Snarehaven.LootClaim.Popup` 이 본다.
bool FSnarehavenHudLootPopupTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = SpawnTestPlayer(World);
	UHuntHudWidget* Hud = CreateTestHud(World, Player);
	ULootClaimComponent* const Claim = Player != nullptr ? Player->FindComponentByClass<ULootClaimComponent>() : nullptr;
	if (!TestNotNull(TEXT("플레이어 스폰"), Player) || !TestNotNull(TEXT("HUD 생성"), Hud)
		|| !TestNotNull(TEXT("플레이어에 지급 대기열 보내기 컴포넌트"), Claim))
	{
		DestroyTestWorld(World);
		return false;
	}

	const FText Popup = FText::FromString(TEXT("+4 검사용 나무"));   // 시험 전용 글
	Claim->OnGrantPopup.Broadcast(Popup);
	TestEqual(TEXT("획득 글이 알림 카드에 뜬다"), Hud->GetCaptureBannerText().ToString(), Popup.ToString());
	TestTrue(TEXT("알림 카드가 보인다"), Hud->IsCaptureBannerVisible());
	AddInfo(FString::Printf(TEXT("[LootPopup] 알림 카드 '%s'"), *Hud->GetCaptureBannerText().ToString()));

	Hud->ClearCaptureBanner();
	Hud->BindToCharacter(nullptr);
	Claim->OnGrantPopup.Broadcast(Popup);
	TestTrue(TEXT("떼어 낸 뒤엔 안 듣는다"), Hud->GetCaptureBannerText().IsEmpty());

	DestroyTestWorld(World);
	return true;
}

// ROADMAP M21-09d — 올가미 링 화면. 링 반지름이 시간에 따라 줄고, 성공 창 안에서만 목표 원이 초록이며,
// 링이 서면 화면에 뜨고 실패해 동물이 달아나면 링이 내려가고 `놓쳤다!` 줄이 뜨는지.
// 숫자는 검사 전용 값이다(Verification.md 6 — DESIGN 1.5·0.6·0.4 와 일부러 다르다).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenHudCaptureRingTest, "Snarehaven.HuntHud.CaptureRing.ShrinksGreensAndMisses",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenHudCaptureRingTest::RunTest(const FString& Parameters)
{
	constexpr float RingSeconds = 2.f;
	constexpr float WindowSeconds = 0.5f;
	constexpr float WindowHardSeconds = 0.3f;
	constexpr float HardMinHP = 1000.f;   // 검사 동물(20)은 큰 종이 아니다
	constexpr float EscapeSeconds = 1.f;
	constexpr float RadiusTolerance = 0.01f;

	// 순수 함수 — 시작 = 바깥 반지름, 끝 = 목표 반지름, 가운데 = 그 사이, 링 밖은 양끝에 붙는다.
	TestEqual(TEXT("0초 = 시작 반지름"), USnareCaptureRingWidget::ComputeLassoRadius(0.f, RingSeconds, 10.f, 30.f), 30.f, RadiusTolerance);
	TestEqual(TEXT("반 = 가운데"), USnareCaptureRingWidget::ComputeLassoRadius(RingSeconds * 0.5f, RingSeconds, 10.f, 30.f), 20.f, RadiusTolerance);
	TestEqual(TEXT("끝 = 목표 반지름"), USnareCaptureRingWidget::ComputeLassoRadius(RingSeconds, RingSeconds, 10.f, 30.f), 10.f, RadiusTolerance);
	TestEqual(TEXT("끝을 넘어도 목표에서 멈춤"), USnareCaptureRingWidget::ComputeLassoRadius(RingSeconds * 2.f, RingSeconds, 10.f, 30.f), 10.f, RadiusTolerance);
	TestEqual(TEXT("링 길이 0 = 목표 반지름"), USnareCaptureRingWidget::ComputeLassoRadius(0.f, 0.f, 10.f, 30.f), 10.f, RadiusTolerance);

	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = SpawnTestPlayer(World);
	AAnimalCharacter* Animal = SpawnTestAnimal(World, TestNearDistanceCm);
	UHuntHudWidget* Hud = CreateTestHud(World, Player);
	if (!TestNotNull(TEXT("플레이어 스폰"), Player) || !TestNotNull(TEXT("동물 스폰"), Animal)
		|| !TestNotNull(TEXT("HUD 생성"), Hud))
	{
		DestroyTestWorld(World);
		return false;
	}
	// 반환값을 붙잡고 있어야 산다 — 버리면 Slate 가 소멸하며 `NativeDestruct` 가 링 시작 알림 구독까지 끊는다.
	const TSharedRef<SWidget> SlateWidget = Hud->TakeWidget();

	UCaptureComponent* Capture = Player->GetCapture();
	Capture->SetCaptureRingSettings(RingSeconds, WindowSeconds, WindowHardSeconds, HardMinHP);
	Capture->SetCaptureEscapeSeconds(EscapeSeconds);

	UMeleeAttackComponent* Melee = Player->GetMeleeAttack();
	Melee->RefreshTarget();
	UAnimalStateComponent* State = Animal->GetAnimalState();
	while (State->GetState() != EAnimalState::Capturable && !State->IsFinished())
	{
		if (!Melee->AttackOnce())
		{
			break;
		}
	}
	if (!TestEqual(TEXT("동물이 포획 가능 상태가 됐다"), State->GetState(), EAnimalState::Capturable))
	{
		DestroyTestWorld(World);
		return false;
	}

	USnareCaptureRingWidget* Ring = Hud->GetCaptureRingWidget();
	if (!TestNotNull(TEXT("링 화면이 트리에 섰다"), Ring))
	{
		DestroyTestWorld(World);
		return false;
	}
	Hud->UpdateCaptureRing();
	TestFalse(TEXT("링 전엔 링 화면이 없다"), Hud->IsCaptureRingVisible());

	// 첫 누름 → 링 화면이 뜨고, 막 시작이라 올가미 원은 바깥에 크림 목표 원.
	Capture->TryCaptureTarget();
	TestTrue(TEXT("링이 돈다"), Capture->IsCaptureRingActive());
	TestTrue(TEXT("링이 서면 링 화면이 뜬다"), Hud->IsCaptureRingVisible());
	TestEqual(TEXT("시작 반지름"), Ring->GetLassoRadius(), Ring->GetStartRadius(), RadiusTolerance);
	TestFalse(TEXT("시작엔 초록 아님"), Ring->IsTargetGreen());
	TestTrue(TEXT("창 밖 목표 원 = 크림"), Ring->GetTargetOutlineColor().Equals(SnareUiStyle::UiButtonBase()));

	// 시간이 흐르면 줄고, 창 직전엔 아직 크림, 창 시작·한가운데·끝은 초록, 링을 넘기면 다시 크림.
	const float EarlyElapsed = RingSeconds - WindowSeconds - 0.1f;
	Ring->UpdateRingAt(EarlyElapsed, RingSeconds, WindowSeconds);
	const float EarlyRadius = Ring->GetLassoRadius();
	TestTrue(TEXT("시간이 흐르면 올가미 원이 준다"), EarlyRadius < Ring->GetStartRadius() && EarlyRadius > Ring->GetTargetRadius());
	TestFalse(TEXT("창 직전은 초록 아님"), Ring->IsTargetGreen());
	Ring->UpdateRingAt(RingSeconds - WindowSeconds, RingSeconds, WindowSeconds);
	TestTrue(TEXT("창 시작 = 초록"), Ring->IsTargetGreen());
	Ring->UpdateRingAt(RingSeconds - WindowSeconds * 0.5f, RingSeconds, WindowSeconds);
	TestTrue(TEXT("창 한가운데 = 초록"), Ring->IsTargetGreen());
	TestTrue(TEXT("창 안 목표 원 = 초록 색"), Ring->GetTargetOutlineColor().Equals(SnareUiStyle::UiShopPriceGreen()));
	const float InWindowWidth = Ring->GetTargetOutlineWidth();
	TestTrue(TEXT("창 안에서 더 줄었다"), Ring->GetLassoRadius() < EarlyRadius);
	Ring->UpdateRingAt(RingSeconds, RingSeconds, WindowSeconds);
	TestEqual(TEXT("링 끝 = 목표 원에 닿음"), Ring->GetLassoRadius(), Ring->GetTargetRadius(), RadiusTolerance);
	TestTrue(TEXT("링 끝도 초록(판정과 같은 경계)"), Ring->IsTargetGreen());
	Ring->UpdateRingAt(RingSeconds + 0.1f, RingSeconds, WindowSeconds);
	TestFalse(TEXT("링을 넘기면 초록 아님"), Ring->IsTargetGreen());
	// 색 말고 굵기로도 갈린다 (10-16 접근성 — 색각 이상).
	TestTrue(FString::Printf(TEXT("창 안 테 굵기 %.0f > 창 밖 %.0f"), InWindowWidth, Ring->GetTargetOutlineWidth()),
		InWindowWidth >= Ring->GetTargetOutlineWidth() * 2.f);
	AddInfo(FString::Printf(TEXT("[Ring화면] 반지름 시작 %.1f → 창 직전 %.1f → 끝 %.1f · 창 [%.2f, %.2f] 초록"),
		Ring->GetStartRadius(), EarlyRadius, Ring->GetTargetRadius(), RingSeconds - WindowSeconds, RingSeconds));

	// 실패 → 동물이 달아나면 링은 내려가고 `놓쳤다!` 줄이 뜬다.
	TestFalse(TEXT("놓치기 전엔 줄 없음"), Hud->IsCaptureBannerVisible());
	Capture->ResolveCaptureRingAt(0.f);
	TestEqual(TEXT("실패 → Escaping"), State->GetState(), EAnimalState::Escaping);
	Hud->UpdateCaptureRing();
	TestFalse(TEXT("판정 뒤 링 화면이 내려간다"), Hud->IsCaptureRingVisible());
	TestTrue(TEXT("놓치면 줄이 뜬다"), Hud->IsCaptureBannerVisible());
	TestTrue(TEXT("줄 글자 = 놓쳤다!"), Hud->GetCaptureBannerText().EqualTo(UHuntHudWidget::GetCaptureMissText()));
	TestEqual(TEXT("놓쳤다 글자"), UHuntHudWidget::GetCaptureMissText().ToString(), FString(TEXT("놓쳤다!")));
	AddInfo(FString::Printf(TEXT("[Ring화면] 실패 → %s · 링 화면 %s"),
		*Hud->GetCaptureBannerText().ToString(), Hud->IsCaptureRingVisible() ? TEXT("보임") : TEXT("내려감")));

	// 남이 일으킨 도주(링을 안 건 동물)는 내 줄을 안 띄운다 — 줄을 내리고 도주가 끝난 뒤 링 없이 다시 달아나게 한다.
	Hud->ClearCaptureBanner();
	State->AdvanceEscape(EscapeSeconds);
	State->BeginEscape(EscapeSeconds);
	TestFalse(TEXT("내 링이 아닌 도주는 줄 없음"), Hud->IsCaptureBannerVisible());

	DestroyTestWorld(World);
	return true;
}

// M60-03 위험 체력 붉은 테두리 (DESIGN 2.2 「위험 체력 알림」) — 문턱 위 숨김 · 아래 보임(불투명도 범위) · 눌림 통과 · 회복·쓰러짐 숨김.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenHudLowHealthEdgeTest, "Snarehaven.HuntHud.LowHealthEdge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenHudLowHealthEdgeTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = SpawnTestPlayer(World);
	UHuntHudWidget* Hud = CreateWidget<UHuntHudWidget>(World, UHuntHudWidget::StaticClass());
	if (!TestNotNull(TEXT("플레이어 스폰"), Player) || !TestNotNull(TEXT("HUD 생성"), Hud))
	{
		DestroyTestWorld(World);
		return false;
	}
	const TSharedRef<SWidget> Built = Hud->TakeWidget();
	Hud->BindToCharacter(Player);
	UPlayerVitalsComponent* const Vitals = Player->GetPlayerVitals();
	if (!TestNotNull(TEXT("체력 컴포넌트"), Vitals) || !TestTrue(TEXT("검사 전용 값으로 초기화"), Vitals->InitializeFromRow(MakeTestVitalsRow())))
	{
		DestroyTestWorld(World);
		return false;
	}
	if (!TestNotNull(TEXT("테두리 레이어가 트리에 있다"), Hud->GetLowHealthEdgeLayer()))
	{
		DestroyTestWorld(World);
		return false;
	}

	Hud->UpdateLowHealthEdge(0.0);
	TestFalse(TEXT("체력 가득 = 테두리 숨김"), Hud->IsLowHealthEdgeVisible());
	TestEqual(TEXT("숨김 = 불투명도 0"), Hud->GetLowHealthEdgeOpacity(), 0.f);

	// 최대 50 중 25 → 비율 0.5 = 문턱 위
	Vitals->ApplyDamage(25.f);
	Hud->UpdateLowHealthEdge(0.0);
	TestFalse(TEXT("문턱 위 = 숨김"), Hud->IsLowHealthEdgeVisible());

	// 10 남음 → 비율 0.2 → 세기 1/3. 펄스 위상 0 = 가장 밝을 때 = 표 불투명도 그대로.
	Vitals->ApplyDamage(15.f);
	Hud->UpdateLowHealthEdge(0.0);
	TestTrue(TEXT("문턱 아래 = 보임"), Hud->IsLowHealthEdgeVisible());
	const float Strength = SnareLowHealth::StateFor(Vitals->GetHealth(), Vitals->GetMaxHealth(), false);
	TestEqual(TEXT("가장 밝을 때 불투명도 = 표 값"), Hud->GetLowHealthEdgeOpacity(), SnareLowHealth::EdgeOpacity(Strength), 0.001f);
	TestTrue(TEXT("불투명도는 하한 이상"), Hud->GetLowHealthEdgeOpacity() >= SnareLowHealth::EdgeOpacityMin);
	TestTrue(TEXT("불투명도는 상한 이하"), Hud->GetLowHealthEdgeOpacity() <= SnareLowHealth::EdgeOpacityMax);

	// 펄스: 한 주기 절반 지점은 더 어둡다.
	const float Bright = Hud->GetLowHealthEdgeOpacity();
	Hud->UpdateLowHealthEdge(SnareLowHealth::BeatInterval(Strength) * 0.5);
	TestTrue(TEXT("펄스 어두운 순간은 밝은 순간보다 낮다"), Hud->GetLowHealthEdgeOpacity() < Bright);
	TestTrue(TEXT("그래도 보인다"), Hud->GetLowHealthEdgeOpacity() > 0.f);
	TestTrue(TEXT("어두운 순간도 표 하한 이상"), Hud->GetLowHealthEdgeOpacity() >= SnareLowHealth::EdgeOpacityMin - 0.0001f);

	// 눌림 통과 — 레이어와 모든 띠가 클릭을 가로채지 않는다.
	TestEqual(TEXT("레이어 = SelfHitTestInvisible"),
		static_cast<int32>(Hud->GetLowHealthEdgeLayer()->GetVisibility()), static_cast<int32>(ESlateVisibility::SelfHitTestInvisible));
	TestEqual(TEXT("띠 24개 (6겹 × 4방향)"), Hud->GetLowHealthEdgeLayer()->GetChildrenCount(), SnareLowHealth::EdgeBandLayers * 4);
	for (int32 Index = 0; Index < Hud->GetLowHealthEdgeLayer()->GetChildrenCount(); ++Index)
	{
		const UWidget* const Band = Hud->GetLowHealthEdgeLayer()->GetChildAt(Index);
		TestEqual(*FString::Printf(TEXT("띠 %d = SelfHitTestInvisible"), Index),
			static_cast<int32>(Band->GetVisibility()), static_cast<int32>(ESlateVisibility::SelfHitTestInvisible));
	}

	// 안쪽으로 사라진다 — 바깥 겹이 안쪽 겹보다 진하고 가장 안쪽은 거의 0 (각진 안쪽 선 없음).
	Hud->UpdateLowHealthEdge(0.0);
	float PrevAlpha = 2.f;
	for (int32 Layer = 0; Layer < SnareLowHealth::EdgeBandLayers; ++Layer)
	{
		const UBorder* const LayerBand = Cast<UBorder>(Hud->GetLowHealthEdgeLayer()->GetChildAt(Layer * 4));
		if (!TestNotNull(*FString::Printf(TEXT("겹 %d 띠"), Layer), LayerBand))
		{
			continue;
		}
		const float Alpha = LayerBand->GetBrushColor().A;
		TestTrue(*FString::Printf(TEXT("겹 %d(%.3f) 는 바깥 겹보다 옅다"), Layer, Alpha), Alpha < PrevAlpha);
		PrevAlpha = Alpha;
	}
	TestTrue(TEXT("가장 바깥 겹 불투명도 > 가장 안쪽 겹"),
		Cast<UBorder>(Hud->GetLowHealthEdgeLayer()->GetChildAt(0))->GetBrushColor().A
		> Cast<UBorder>(Hud->GetLowHealthEdgeLayer()->GetChildAt((SnareLowHealth::EdgeBandLayers - 1) * 4))->GetBrushColor().A);
	TestTrue(TEXT("가장 안쪽 겹은 0.05 배 이하로 옅다"),
		Cast<UBorder>(Hud->GetLowHealthEdgeLayer()->GetChildAt((SnareLowHealth::EdgeBandLayers - 1) * 4))->GetBrushColor().A
		<= SnareLowHealth::EdgeOpacityMax * 0.05f + 0.0001f);
	const FLinearColor EdgeColor = Cast<UBorder>(Hud->GetLowHealthEdgeLayer()->GetChildAt(0))->GetBrushColor();
	TestTrue(TEXT("색 = 순빨강"), EdgeColor.R == 1.f && EdgeColor.G == 0.f && EdgeColor.B == 0.f);

	// 회복하면 즉시 숨김
	Vitals->Heal(30.f);
	Hud->UpdateLowHealthEdge(0.0);
	TestFalse(TEXT("회복 = 숨김"), Hud->IsLowHealthEdgeVisible());

	// 쓰러지면 숨김
	Vitals->ApplyDamage(35.f);
	Hud->UpdateLowHealthEdge(0.0);
	TestTrue(TEXT("5 남음 = 보임"), Hud->IsLowHealthEdgeVisible());
	Vitals->ApplyDamage(10.f);
	Hud->UpdateLowHealthEdge(0.0);
	TestTrue(TEXT("쓰러졌다"), Vitals->IsDead());
	TestFalse(TEXT("쓰러짐 = 숨김"), Hud->IsLowHealthEdgeVisible());

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenHudStoreShotPanelTest, "Snarehaven.HuntHud.StoreShotHidesAnimalPanel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenHudStoreShotPanelTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("평소엔 하이라이트 동물 막대가 뜬다"), SnareHuntHud::WantAnimalPanel(true, false, false));
	TestFalse(TEXT("스토어 촬영이면 막대를 접는다"), SnareHuntHud::WantAnimalPanel(true, false, true));
	TestFalse(TEXT("포획 성공 줄이 있으면 접는다"), SnareHuntHud::WantAnimalPanel(true, true, false));
	TestFalse(TEXT("동물이 없으면 접는다"), SnareHuntHud::WantAnimalPanel(false, false, false));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenHudSwimLowStaminaWarnTest, "Snarehaven.HuntHud.SwimLowStaminaWarn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// M107-05 ③ — 새 계정이 바다에서 경고 없이 익사했다. 기운 30% 이하면 가운데 위 경고가 잠수당 한 번 떠야 한다.
bool FSnarehavenHudSwimLowStaminaWarnTest::RunTest(const FString& Parameters)
{
	TestFalse(TEXT("뭍에선 안 띄운다"), UHuntHudWidget::ShouldWarnSwimStamina(false, 0.f, 100.f, false));
	TestFalse(TEXT("기운 31% 면 아직"), UHuntHudWidget::ShouldWarnSwimStamina(true, 31.f, 100.f, false));
	TestTrue(TEXT("기운 30% 면 띄운다"), UHuntHudWidget::ShouldWarnSwimStamina(true, 30.f, 100.f, false));
	TestFalse(TEXT("이번 잠수에 띄웠으면 다시 안 띄운다"), UHuntHudWidget::ShouldWarnSwimStamina(true, 10.f, 100.f, true));

	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = SpawnTestPlayer(World);
	UHuntHudWidget* Hud = CreateWidget<UHuntHudWidget>(World, UHuntHudWidget::StaticClass());
	if (!TestNotNull(TEXT("플레이어 스폰"), Player) || !TestNotNull(TEXT("HUD 생성"), Hud))
	{
		DestroyTestWorld(World);
		return false;
	}
	Hud->TakeWidget();
	Hud->BindToCharacter(Player);
	UPlayerVitalsComponent* const Vitals = Player->GetPlayerVitals();
	if (!TestNotNull(TEXT("체력·스태미나 컴포넌트"), Vitals) || !TestNotNull(TEXT("경고 글자 위젯"), Hud->GetSwimWarnText())
		|| !TestTrue(TEXT("검사 전용 값으로 초기화"), Vitals->InitializeFromRow(MakeTestVitalsRow())))
	{
		DestroyTestWorld(World);
		return false;
	}

	// 기운 40, 수영 −12/초. 1초 뒤 28(70%) — 아직 경고 없음. 3초 뒤 4(10%) — 경고.
	Vitals->SetSwimState(true, 12.f, 5.f);
	Vitals->AdvanceTime(1.f);
	TestFalse(TEXT("기운 70% 면 경고 없음"), Hud->UpdateSwimWarn());
	TestTrue(TEXT("경고 글자는 접혀 있다"), Hud->GetSwimWarnText()->GetVisibility() == ESlateVisibility::Collapsed);
	Vitals->AdvanceTime(2.f);
	TestTrue(TEXT("기운 10% 면 경고를 띄운다"), Hud->UpdateSwimWarn());
	TestTrue(TEXT("경고 글자가 펴진다"), Hud->GetSwimWarnText()->GetVisibility() != ESlateVisibility::Collapsed);
	TestFalse(TEXT("같은 잠수에 두 번은 안 띄운다"), Hud->UpdateSwimWarn());

	// 뭍에 올랐다 다시 들면 또 경고한다.
	Vitals->SetSwimState(false, 0.f, 0.f);
	TestFalse(TEXT("뭍에선 경고 없음"), Hud->UpdateSwimWarn());
	Vitals->SetSwimState(true, 12.f, 5.f);
	TestTrue(TEXT("다음 잠수엔 다시 띄운다"), Hud->UpdateSwimWarn());

	DestroyTestWorld(World);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
