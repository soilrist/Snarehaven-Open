// INBOX 18:10 ① 2층 검사 — 「공격」 버튼 글자가 지금 누르면 되는 일을 따르는지:
// 대상이 없으면 「공격」, 맨손 채집물이면 「줍기」, 손도끼 나무면 「베기」, 곡괭이 광맥이면 「캐기」,
// 동물과 채집물이 같이 있으면 `BeginAttack` 처럼 동물이 먼저라 「공격」.
// 도구의 동작은 실제 `DT_ItemTable` 행(`UseAction`)에서 온다 — 기대 글자만 여기 적는다.
// INBOX 18:10 ② — 맞는 도구 없이 나무·광맥 앞이면 버튼 위 안내 줄(없으면 「…가 필요 — 잡화점」, 가졌으면 「…를 들어요」).
// INBOX 18:20 ② — 맨손 채집물 「채집」(가방만 「줍기」), 포획 가능 동물 「포획」, 낚싯대+포인트 「낚시」 이고
// 그 글자일 때 `BeginAttack` 이 실제로 포획·던지기를 하는지 (글자와 되는 일이 같아야 한다).
// INBOX 22:00 — 맞는 도구를 가방에만 두면 글자는 「베기·캐기·낚시」이고 누르면 들고 바로 하며,
// 아예 없으면 「손도끼 필요·곡괭이 필요·낚싯대 필요」이고 누르면 휘두르지 않는다.
// 전수 표 실측 #0512 — 상인·의뢰인 대화 거리 안이면 「열기」 이고 누르면 휘두르지 않는다(판 열기는 3층 실측).
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.HuntHud.AttackVerb; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Animals/AnimalCharacter.h"
#include "Backend/SnareStory.h"
#include "Animals/AnimalStateComponent.h"
#include "Combat/MeleeAttackComponent.h"
#include "Data/AnimalRow.h"
#include "Data/GatherableRow.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Equipment/EquipmentComponent.h"
#include "Gathering/GatherComponent.h"
#include "Gathering/GatherableActor.h"
#include "Combat/CaptureComponent.h"
#include "Data/ItemRow.h"
#include "Engine/DataTable.h"
#include "Gathering/TapFishingComponent.h"
#include "Inventory/InventoryComponent.h"
#include "SnarehavenCharacter.h"
#include "UI/HuntHudWidget.h"
#include "Components/TextBlock.h"
#include "Widgets/Text/STextBlock.h"
#include "World/FishingSpotActor.h"
#include "World/QuestGiverActor.h"
#include "World/FruitTreeActor.h"
#include "World/ShopVendorActor.h"

namespace
{
	// 테스트 전용 값 (Verification.md 6 — 설계 숫자가 아니다).
	constexpr float TestAttackDamage = 10.f;
	constexpr float TestRangeCm = 300.f;
	constexpr float TestIntervalSeconds = 1.f;
	constexpr float TestNearDistanceCm = 100.f;
	constexpr float TestTalkRangeCm = 250.f;
	constexpr float TestFarDistanceCm = 600.f;
	constexpr float TestAnimalMaxHP = 20.f;
	constexpr float TestCaptureThresholdPct = 50.f;
	const FName TestGatherableID = TEXT("gatherable.test_verb");
	const FName TestItemID = TEXT("item.test_verb");
	// 낚싯대 아이템 ID 는 표에서 읽는다 — 행 이름만 여기 둔다 (SnarehavenHuntHudTest 와 같은 행).
	const TCHAR* const VerbItemTablePath = TEXT("/Game/Game/Data/Items/DT_ItemTable.DT_ItemTable");
	const TCHAR* const VerbRodRowName = TEXT("Item_RodBasic");

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

	AGatherableActor* SpawnGatherable(UWorld* World, FName RequiredTool)
	{
		AGatherableActor* const Target =
			World->SpawnActor<AGatherableActor>(FVector(TestNearDistanceCm, 0.f, 0.f), FRotator::ZeroRotator);
		FGatherableRow Row;
		Row.GatherableID = TestGatherableID;
		Row.GrantItemID = TestItemID;
		Row.GrantCount = 1;
		Row.RequiredToolItemID = RequiredTool;
		return Target != nullptr && Target->InitializeFromRow(Row) ? Target : nullptr;
	}

	// 도구 행은 메시 대기라 `EquipItem` 대신 칸만 채운다 (SnarehavenGatherTest 의 ToolUses 검사와 같은 길).
	bool HoldTool(ASnarehavenCharacter* Player, FName ToolItemID)
	{
		TArray<FEquippedSlot> Slots;
		if (!ToolItemID.IsNone())
		{
			FEquippedSlot& Slot = Slots.AddDefaulted_GetRef();
			Slot.Slot = EEquipSlot::Tool;
			Slot.ItemRowName = Player->GetEquipment()->FindEquipmentRowByItemID(ToolItemID);
			if (Slot.ItemRowName.IsNone())
			{
				return false;
			}
		}
		Player->GetEquipment()->ApplyEquippedSlots(Slots);
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAttackVerbTextTest, "Snarehaven.HuntHud.AttackVerb.TextPerVerb",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAttackVerbTextTest::RunTest(const FString& Parameters)
{
	// 글자는 사람이 보는 약속이라 그대로 맞댄다 (INBOX 18:10 ① 「베기·캐기·수확·줍기·열기」).
	const TPair<ESnareInteractVerb, FString> Expects[] = {
		{ ESnareInteractVerb::Attack, TEXT("공격") },
		{ ESnareInteractVerb::PickUp, TEXT("줍기") },
		{ ESnareInteractVerb::Chop, TEXT("베기") },
		{ ESnareInteractVerb::Mine, TEXT("캐기") },
		{ ESnareInteractVerb::Harvest, TEXT("수확") },
		{ ESnareInteractVerb::Place, TEXT("넣기") },
		{ ESnareInteractVerb::Open, TEXT("열기") },
		{ ESnareInteractVerb::Gather, TEXT("채집") },
		{ ESnareInteractVerb::Capture, TEXT("포획") },
		{ ESnareInteractVerb::Fish, TEXT("낚시") },
		{ ESnareInteractVerb::NeedHatchet, TEXT("손도끼 필요") },
		{ ESnareInteractVerb::NeedPickaxe, TEXT("곡괭이 필요") },
		{ ESnareInteractVerb::NeedRod, TEXT("낚싯대 필요") },
		{ ESnareInteractVerb::QuitFishing, TEXT("그만두기") },
	};
	for (const TPair<ESnareInteractVerb, FString>& Expect : Expects)
	{
		TestEqual(*FString::Printf(TEXT("동사 %d 글자"), static_cast<int32>(Expect.Key)),
			UHuntHudWidget::GetInteractVerbText(Expect.Key).ToString(), Expect.Value);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAttackVerbFitsDiscTest, "Snarehaven.HuntHud.AttackVerb.FitsDisc",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAttackVerbFitsDiscTest::RunTest(const FString& Parameters)
{
	// 10-16 HUD 3차 사람 지적 「손도끼 필요 이것도 원안에 다 안들어가네」 — 모든 동사 글자를 원 버튼과 같은 글자칸으로
	// 실제 배치해 글자 네모의 네 모서리가 원(반지름 - 테두리 여백) 안에 드는지, 그리고 읽을 만한 크기(16 이상)인지 맞댄다.
	constexpr float MinReadableFontSize = 16.f;
	const float Radius = UHuntHudWidget::GetVerbDiscTextRadius();
	const UEnum* const VerbEnum = StaticEnum<ESnareInteractVerb>();
	for (int32 Index = 0; Index < VerbEnum->NumEnums() - 1; ++Index)
	{
		const ESnareInteractVerb Verb = static_cast<ESnareInteractVerb>(VerbEnum->GetValueByIndex(Index));
		const FText Disc = UHuntHudWidget::GetVerbDiscText(UHuntHudWidget::GetInteractVerbText(Verb));
		const float FontSize = UHuntHudWidget::GetVerbDiscFontSize(Disc);
		const TSharedRef<STextBlock> Text = SNew(STextBlock).Text(Disc).Justification(ETextJustify::Center)
			.Font([FontSize] { FSlateFontInfo Font = GetDefault<UTextBlock>()->GetFont(); Font.Size = FontSize; return Font; }());
		Text->SlatePrepass(1.f);
		const FVector2D Size = Text->GetDesiredSize();
		const float Corner = static_cast<float>(Size.Size()) * 0.5f;
		AddInfo(FString::Printf(TEXT("[VerbDisc] %s `%s` 글자 %.0f → %.0f×%.0f 모서리 %.1f (원 %.0f)"),
			*VerbEnum->GetNameStringByIndex(Index), *Disc.ToString().Replace(TEXT("\n"), TEXT("/")), FontSize, Size.X, Size.Y, Corner, Radius));
		TestTrue(*FString::Printf(TEXT("%s 글자 네모가 원 안"), *VerbEnum->GetNameStringByIndex(Index)), Corner <= Radius + 1.f);
		TestTrue(*FString::Printf(TEXT("%s 글자 크기 ≥ %.0f"), *VerbEnum->GetNameStringByIndex(Index), MinReadableFontSize), FontSize >= MinReadableFontSize);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAttackVerbTargetTest, "Snarehaven.HuntHud.AttackVerb.FollowsTarget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAttackVerbTargetTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* const Player =
		World->SpawnActor<ASnarehavenCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("플레이어 스폰"), Player) || !TestNotNull(TEXT("장비"), Player->GetEquipment()))
	{
		DestroyTestWorld(World);
		return false;
	}
	Player->GetMeleeAttack()->SetAttackSettings(TestAttackDamage, TestRangeCm, TestIntervalSeconds);
	UHuntHudWidget* const Hud = CreateWidget<UHuntHudWidget>(World, UHuntHudWidget::StaticClass());
	if (!TestNotNull(TEXT("HUD 생성"), Hud))
	{
		DestroyTestWorld(World);
		return false;
	}
	Hud->BindToCharacter(Player);

	// 대상을 세우는 건 각 컴포넌트의 하이라이트 훑기다 — 게임에선 0.6초 타이머, 여기선 손으로 한 번.
	const auto Rescan = [Player]()
	{
		Player->GetMeleeAttack()->RefreshTarget();
		Player->GetGather()->RefreshTarget();
	};
	const auto Check = [this, Player, Hud](const TCHAR* What, ESnareInteractVerb Verb, const TCHAR* Text)
	{
		const ESnareInteractVerb Got = Player->GetInteractVerb();
		AddInfo(FString::Printf(TEXT("[AttackVerb] %s → %d · 버튼 「%s」"), What, static_cast<int32>(Got),
			*Hud->GetAttackButtonText().ToString()));
		TestEqual(*FString::Printf(TEXT("%s 동사"), What), static_cast<int32>(Got), static_cast<int32>(Verb));
		TestEqual(*FString::Printf(TEXT("%s 버튼 글자"), What), Hud->GetAttackButtonText().ToString(), FString(Text));
	};

	Rescan();
	Check(TEXT("대상 없음"), ESnareInteractVerb::Attack, TEXT("공격"));

	// 맨손 채집물(열매·가지)
	AGatherableActor* Target = SpawnGatherable(World, NAME_None);
	TestNotNull(TEXT("맨손 채집물"), Target);
	Rescan();
	Check(TEXT("맨손 채집물"), ESnareInteractVerb::Gather, TEXT("채집"));
	Target->Destroy();

	// 손도끼 나무 — 손도끼를 들면 베기 (손도끼 행 `UseAction` = Smash 도 베기다)
	Target = SpawnGatherable(World, TEXT("item.hatchet"));
	TestTrue(TEXT("손도끼를 든다"), HoldTool(Player, TEXT("item.hatchet")));
	Rescan();
	Check(TEXT("손도끼 나무"), ESnareInteractVerb::Chop, TEXT("베기"));

	// 동물이 같이 있으면 `BeginAttack` 처럼 동물이 먼저다.
	AAnimalCharacter* const Animal =
		World->SpawnActor<AAnimalCharacter>(FVector(-TestNearDistanceCm, 0.f, 0.f), FRotator::ZeroRotator);
	if (TestNotNull(TEXT("동물 스폰"), Animal))
	{
		FAnimalRow Row;
		Row.AnimalID = TEXT("animal.test_dummy");
		Row.DisplayName = FText::FromString(TEXT("검사용 동물"));
		Row.MaxHP = TestAnimalMaxHP;
		Row.CaptureThresholdPct = TestCaptureThresholdPct;
		Animal->GetAnimalState()->InitializeFromRow(Row);
		Rescan();
		Check(TEXT("동물+나무"), ESnareInteractVerb::Attack, TEXT("공격"));
		Animal->Destroy();
	}
	Target->Destroy();

	// 곡괭이 광맥
	Target = SpawnGatherable(World, TEXT("item.pickaxe_basic"));
	TestTrue(TEXT("곡괭이를 든다"), HoldTool(Player, TEXT("item.pickaxe_basic")));
	Rescan();
	Check(TEXT("곡괭이 광맥"), ESnareInteractVerb::Mine, TEXT("캐기"));

	// 맞는 도구가 가방에도 없으면 「곡괭이 필요」 (INBOX 22:00 ②).
	TestTrue(TEXT("맨손으로"), HoldTool(Player, NAME_None));
	Rescan();
	Check(TEXT("도구 없이 광맥"), ESnareInteractVerb::NeedPickaxe, TEXT("곡괭이 필요"));
	Target->Destroy();

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenNeedToolHintTest, "Snarehaven.HuntHud.AttackVerb.NeedToolHint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenNeedToolHintTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* const Player =
		World->SpawnActor<ASnarehavenCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("플레이어 스폰"), Player) || !TestNotNull(TEXT("장비"), Player->GetEquipment())
		|| !TestNotNull(TEXT("인벤토리"), Player->GetInventory()))
	{
		DestroyTestWorld(World);
		return false;
	}
	Player->GetMeleeAttack()->SetAttackSettings(TestAttackDamage, TestRangeCm, TestIntervalSeconds);
	UHuntHudWidget* const Hud = CreateWidget<UHuntHudWidget>(World, UHuntHudWidget::StaticClass());
	if (!TestNotNull(TEXT("HUD 생성"), Hud))
	{
		DestroyTestWorld(World);
		return false;
	}
	// 자리표시 트리(안내 알약)는 RebuildWidget 이 세운다. 붙잡는 부모가 없어 그 뒤 NativeDestruct 가 주인을 끊으므로 묶기는 그다음.
	Hud->TakeWidget();
	Hud->BindToCharacter(Player);

	const auto Rescan = [Player]()
	{
		Player->GetMeleeAttack()->RefreshTarget();
		Player->GetGather()->RefreshTarget();
	};
	// 가진 것을 바꾸는 길은 백엔드 스냅샷 하나다 (CLAUDE.md 8).
	const auto Own = [Player](FName ItemID)
	{
		TArray<FInventorySlot> Slots;
		if (!ItemID.IsNone())
		{
			FInventorySlot& Slot = Slots.AddDefaulted_GetRef();
			Slot.ItemID = ItemID;
			Slot.Count = 1;
		}
		return Player->GetInventory()->ApplyBackendSnapshot(Slots);
	};
	const auto Check = [this, Hud, &Rescan](const TCHAR* What, const TCHAR* Text)
	{
		Rescan();
		Hud->UpdateNeedToolHint();
		const FString Got = Hud->GetNeedToolHintText().ToString();
		AddInfo(FString::Printf(TEXT("[NeedToolHint] %s → 「%s」 · 펴짐 %d"), What, *Got, Hud->IsNeedToolHintVisible() ? 1 : 0));
		TestEqual(*FString::Printf(TEXT("%s 안내 글자"), What), Got, FString(Text));
		TestEqual(*FString::Printf(TEXT("%s 펴짐"), What), Hud->IsNeedToolHintVisible(), *Text != TEXT('\0'));
	};

	Check(TEXT("대상 없음"), TEXT(""));

	// 맨손 채집물은 도구가 필요 없다.
	AGatherableActor* Target = SpawnGatherable(World, NAME_None);
	TestNotNull(TEXT("맨손 채집물"), Target);
	Check(TEXT("맨손 채집물"), TEXT(""));
	Target->Destroy();

	// 손도끼 나무 — 없으면 어디서 사는지, 가지면 어떻게 드는지, 들면 안내가 사라지고 버튼이 「베기」.
	Target = SpawnGatherable(World, TEXT("item.hatchet"));
	TestNotNull(TEXT("나무"), Target);
	TestTrue(TEXT("빈손·빈 가방"), HoldTool(Player, NAME_None) && Own(NAME_None));
	Check(TEXT("손도끼 없이 나무"), TEXT("손도끼가 필요 — 잡화점"));
	TestEqual(TEXT("손도끼 없이 나무 버튼"), Hud->GetAttackButtonText().ToString(), FString(TEXT("손도끼 필요")));
	TestTrue(TEXT("손도끼를 갖는다"), Own(TEXT("item.hatchet")));
	// 가방에 있으면 버튼이 누를 때 알아서 드니 안내 없이 「베기」 (INBOX 22:00 ①).
	Check(TEXT("손도끼 가졌는데 안 듦"), TEXT(""));
	TestEqual(TEXT("손도끼 가방에만 버튼"), Hud->GetAttackButtonText().ToString(), FString(TEXT("베기")));
	TestTrue(TEXT("손도끼를 든다"), HoldTool(Player, TEXT("item.hatchet")));
	Check(TEXT("손도끼 든 나무"), TEXT(""));
	TestEqual(TEXT("손도끼 든 나무 버튼"), Hud->GetAttackButtonText().ToString(), FString(TEXT("베기")));

	// 동물을 겨누는 중이면 버튼은 진짜 공격이라 안내를 안 띄운다.
	TestTrue(TEXT("다시 맨손"), HoldTool(Player, NAME_None) && Own(NAME_None));
	AAnimalCharacter* const Animal =
		World->SpawnActor<AAnimalCharacter>(FVector(-TestNearDistanceCm, 0.f, 0.f), FRotator::ZeroRotator);
	if (TestNotNull(TEXT("동물 스폰"), Animal))
	{
		FAnimalRow Row;
		Row.AnimalID = TEXT("animal.test_dummy");
		Row.DisplayName = FText::FromString(TEXT("검사용 동물"));
		Row.MaxHP = TestAnimalMaxHP;
		Row.CaptureThresholdPct = TestCaptureThresholdPct;
		Animal->GetAnimalState()->InitializeFromRow(Row);
		Check(TEXT("동물+나무"), TEXT(""));
		Animal->Destroy();
	}
	Target->Destroy();

	// 곡괭이 광맥 — 문구가 곡괭이로 갈린다(도구 행 `UseAction` Mine).
	Target = SpawnGatherable(World, TEXT("item.pickaxe_basic"));
	TestNotNull(TEXT("광맥"), Target);
	Check(TEXT("곡괭이 없이 광맥"), TEXT("곡괭이가 필요 — 잡화점"));
	TestTrue(TEXT("곡괭이를 갖는다"), Own(TEXT("item.pickaxe_basic")));
	Check(TEXT("곡괭이 가졌는데 안 듦"), TEXT(""));
	TestEqual(TEXT("곡괭이 가방에만 버튼"), Hud->GetAttackButtonText().ToString(), FString(TEXT("캐기")));
	Target->Destroy();
	Check(TEXT("광맥 없어짐"), TEXT(""));

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAttackVerbCaptureFishTest, "Snarehaven.HuntHud.AttackVerb.CaptureAndFish",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAttackVerbCaptureFishTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* const Player =
		World->SpawnActor<ASnarehavenCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
	UDataTable* const ItemTable = LoadObject<UDataTable>(nullptr, VerbItemTablePath);
	if (!TestNotNull(TEXT("플레이어 스폰"), Player) || !TestNotNull(TEXT("장비"), Player->GetEquipment())
		|| !TestNotNull(TEXT("아이템 표"), ItemTable))
	{
		DestroyTestWorld(World);
		return false;
	}
	// 낚시 표(던지는 거리)는 BeginPlay 가 읽는다. 공격 숫자는 그 뒤에 — 검사 전용 값이 표보다 이겨야 한다.
	Player->DispatchBeginPlay();
	Player->GetMeleeAttack()->SetAttackSettings(TestAttackDamage, TestRangeCm, TestIntervalSeconds);
	UCaptureComponent* const Capture = Player->GetCapture();
	UTapFishingComponent* const Fishing = Player->FindComponentByClass<UTapFishingComponent>();
	UHuntHudWidget* const Hud = CreateWidget<UHuntHudWidget>(World, UHuntHudWidget::StaticClass());
	if (!TestNotNull(TEXT("포획 컴포넌트"), Capture) || !TestNotNull(TEXT("낚시 컴포넌트"), Fishing)
		|| !TestNotNull(TEXT("HUD 생성"), Hud))
	{
		DestroyTestWorld(World);
		return false;
	}
	Hud->BindToCharacter(Player);
	// 링 길이 0 = 첫 누름에 바로 포획 (컴포넌트 기본값과 같은 길 — 링 숫자는 CaptureRing 검사 몫).
	Capture->SetCaptureRingSettings(0.f, 0.f, 0.f, 0.f);

	const auto Check = [this, Player, Hud](const TCHAR* What, ESnareInteractVerb Verb, const TCHAR* Text)
	{
		Player->GetMeleeAttack()->RefreshTarget();
		Player->GetGather()->RefreshTarget();
		const ESnareInteractVerb Got = Player->GetInteractVerb();
		AddInfo(FString::Printf(TEXT("[AttackVerb] %s → %d · 버튼 「%s」"), What, static_cast<int32>(Got),
			*Hud->GetAttackButtonText().ToString()));
		TestEqual(*FString::Printf(TEXT("%s 동사"), What), static_cast<int32>(Got), static_cast<int32>(Verb));
		TestEqual(*FString::Printf(TEXT("%s 버튼 글자"), What), Hud->GetAttackButtonText().ToString(), FString(Text));
	};

	// ── 포획 가능 동물 ── 임계값 위에서도 깎아 내려도 「공격」, 포획은 포획 버튼으로만.
	AAnimalCharacter* const Animal =
		World->SpawnActor<AAnimalCharacter>(FVector(TestNearDistanceCm, 0.f, 0.f), FRotator::ZeroRotator);
	if (TestNotNull(TEXT("동물 스폰"), Animal))
	{
		FAnimalRow Row;
		Row.AnimalID = TEXT("animal.test_dummy");
		Row.DisplayName = FText::FromString(TEXT("검사용 동물"));
		Row.MaxHP = TestAnimalMaxHP;
		Row.CaptureThresholdPct = TestCaptureThresholdPct;
		Animal->GetAnimalState()->InitializeFromRow(Row);
		Check(TEXT("임계값 위 동물"), ESnareInteractVerb::Attack, TEXT("공격"));

		const UAnimalStateComponent* const State = Animal->GetAnimalState();
		while (State->GetState() != EAnimalState::Capturable && !State->IsFinished())
		{
			if (!Player->GetMeleeAttack()->AttackOnce())
			{
				break;
			}
		}
		TestEqual(TEXT("포획 가능 상태가 됐다"), State->GetState(), EAnimalState::Capturable);
		// 포획 가능해도 공격 버튼은 「공격」 — 「포획」은 옆 포획 버튼 하나뿐이다 (감독 PIE 10-06 02:40 「포획」 둘).
		Check(TEXT("포획 가능 동물"), ESnareInteractVerb::Attack, TEXT("공격"));
		TestTrue(TEXT("포획 버튼은 켜져 있다"), Hud->IsCaptureButtonEnabled());

		// 누르면 휘두른다(더 때리면 죽는다는 선택지) — 포획으로 새지 않아야 한다. 타격은 지연 뒤라 아직 안 들어간다.
		Player->BeginAttack();
		Player->EndAttack();
		AddInfo(FString::Printf(TEXT("[AttackVerb] 「공격」 누름 → 동물 상태 %d · 끝남 %d · HP %.0f"),
			static_cast<int32>(State->GetState()), State->IsFinished() ? 1 : 0, State->GetCurrentHP()));
		TestNotEqual(TEXT("공격 버튼은 포획하지 않는다"), State->GetState(), EAnimalState::Captured);

		// 포획은 포획 버튼(`IA_Capture` 와 같은 `TryCaptureTarget`)으로만.
		Capture->TryCaptureTarget();
		AddInfo(FString::Printf(TEXT("[AttackVerb] 포획 버튼 → 동물 상태 %d"), static_cast<int32>(State->GetState())));
		TestEqual(TEXT("포획 버튼을 누르면 포획된다"), State->GetState(), EAnimalState::Captured);
		// 위 휘두르기의 공격 간격을 흘려보낸다 — 안 그러면 아래 낚시 누름이 간격에 걸려 버려진다.
		const bool bReadyBefore = Player->GetMeleeAttack()->IsActionReady();
		// 두 번 돌린다 — 이번 프레임에 건 타이머는 첫 `Tick` 끝에야 활성 목록에 들어간다.
		// `FTimerManager::Tick` 은 한 프레임에 한 번만 일해 `GFrameCounter` 를 같이 올린다 (PlayerBody 검사와 같은 길).
		for (int32 Step = 0; Step < 2; ++Step)
		{
			++GFrameCounter;
			World->GetTimerManager().Tick(TestIntervalSeconds + 0.1f);
		}
		AddInfo(FString::Printf(TEXT("[AttackVerb] 간격 흘림 — 준비 %d → %d"), bReadyBefore ? 1 : 0,
			Player->GetMeleeAttack()->IsActionReady() ? 1 : 0));
		TestEqual(TEXT("간격이 지나도 포획된 채"), State->GetState(), EAnimalState::Captured);
		Animal->Destroy();
	}

	// ── 낚시 포인트 ── 낚싯대 없이는 「공격」, 들면 「낚시」, 누르면 던진다.
	AFishingSpotActor* const Spot = World->SpawnActor<AFishingSpotActor>(FVector::ZeroVector, FRotator::ZeroRotator);
	if (Spot != nullptr && !Spot->HasActorBegunPlay())
	{
		Spot->DispatchBeginPlay();
	}
	TestTrue(TEXT("던질 거리 안에 포인트"), Fishing->HasSpotInCastRange());
	TestTrue(TEXT("맨손"), HoldTool(Player, NAME_None));
	Check(TEXT("낚싯대 없이 포인트"), ESnareInteractVerb::NeedRod, TEXT("낚싯대 필요"));
	const FItemRow* const RodRow = ItemTable->FindRow<FItemRow>(FName(VerbRodRowName), TEXT("AttackVerb"));
	if (TestNotNull(TEXT("표에 낚싯대 행"), RodRow) && TestTrue(TEXT("낚싯대를 든다"), HoldTool(Player, RodRow->ItemID)))
	{
		Check(TEXT("낚싯대+포인트"), ESnareInteractVerb::Fish, TEXT("낚시"));
		Player->BeginAttack();
		Player->EndAttack();
		AddInfo(FString::Printf(TEXT("[AttackVerb] 「낚시」 누름 → 낚시 단계 %d"), static_cast<int32>(Fishing->GetState())));
		TestNotEqual(TEXT("누르면 던진다"), static_cast<int32>(Fishing->GetState()), static_cast<int32>(ETapFishingState::Idle));

		// 입질을 기다리는 동안은 「그만두기」 — 누르면 휘두르지 않고 거둔다 (전수 표 실측 #0507 빈 칸).
		if (TestEqual(TEXT("던진 뒤 입질 기다림"), static_cast<int32>(Fishing->GetState()),
			static_cast<int32>(ETapFishingState::WaitingBite)))
		{
			Check(TEXT("입질 기다림"), ESnareInteractVerb::QuitFishing, TEXT("그만두기"));
			const int32 SwingsBefore = Player->GetMeleeAttack()->GetSwingCount();
			Player->BeginAttack();
			Player->EndAttack();
			AddInfo(FString::Printf(TEXT("[AttackVerb] 「그만두기」 누름 → 낚시 단계 %d · 휘두르기 %d → %d"),
				static_cast<int32>(Fishing->GetState()), SwingsBefore, Player->GetMeleeAttack()->GetSwingCount()));
			TestEqual(TEXT("기다리는 동안 누름은 휘두르지 않는다"), Player->GetMeleeAttack()->GetSwingCount(), SwingsBefore);
			TestEqual(TEXT("누르면 거둔다"), static_cast<int32>(Fishing->GetState()), static_cast<int32>(ETapFishingState::Idle));
			Check(TEXT("거둔 뒤 포인트"), ESnareInteractVerb::Fish, TEXT("낚시"));
		}
	}

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAttackVerbAutoEquipTest, "Snarehaven.HuntHud.AttackVerb.AutoEquipFromBag",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAttackVerbAutoEquipTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* const Player =
		World->SpawnActor<ASnarehavenCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
	UDataTable* const ItemTable = LoadObject<UDataTable>(nullptr, VerbItemTablePath);
	if (!TestNotNull(TEXT("플레이어 스폰"), Player) || !TestNotNull(TEXT("장비"), Player->GetEquipment())
		|| !TestNotNull(TEXT("인벤토리"), Player->GetInventory()) || !TestNotNull(TEXT("아이템 표"), ItemTable))
	{
		DestroyTestWorld(World);
		return false;
	}
	Player->DispatchBeginPlay();
	Player->GetMeleeAttack()->SetAttackSettings(TestAttackDamage, TestRangeCm, TestIntervalSeconds);
	UTapFishingComponent* const Fishing = Player->FindComponentByClass<UTapFishingComponent>();
	UHuntHudWidget* const Hud = CreateWidget<UHuntHudWidget>(World, UHuntHudWidget::StaticClass());
	const FItemRow* const RodRow = ItemTable->FindRow<FItemRow>(FName(VerbRodRowName), TEXT("AutoEquip"));
	if (!TestNotNull(TEXT("낚시 컴포넌트"), Fishing) || !TestNotNull(TEXT("HUD 생성"), Hud)
		|| !TestNotNull(TEXT("표에 낚싯대 행"), RodRow))
	{
		DestroyTestWorld(World);
		return false;
	}
	Hud->BindToCharacter(Player);
	UMeleeAttackComponent* const Melee = Player->GetMeleeAttack();
	UEquipmentComponent* const Equipment = Player->GetEquipment();

	// 가진 것을 바꾸는 길은 백엔드 스냅샷 하나다 (CLAUDE.md 8).
	const auto Own = [Player](FName ItemID)
	{
		TArray<FInventorySlot> Slots;
		if (!ItemID.IsNone())
		{
			FInventorySlot& Slot = Slots.AddDefaulted_GetRef();
			Slot.ItemID = ItemID;
			Slot.Count = 1;
		}
		return Player->GetInventory()->ApplyBackendSnapshot(Slots);
	};
	const auto Check = [this, Player, Hud](const TCHAR* What, ESnareInteractVerb Verb, const TCHAR* Text)
	{
		Player->GetMeleeAttack()->RefreshTarget();
		Player->GetGather()->RefreshTarget();
		const ESnareInteractVerb Got = Player->GetInteractVerb();
		AddInfo(FString::Printf(TEXT("[AutoEquip] %s → %d · 버튼 「%s」"), What, static_cast<int32>(Got),
			*Hud->GetAttackButtonText().ToString()));
		TestEqual(*FString::Printf(TEXT("%s 동사"), What), static_cast<int32>(Got), static_cast<int32>(Verb));
		TestEqual(*FString::Printf(TEXT("%s 버튼 글자"), What), Hud->GetAttackButtonText().ToString(), FString(Text));
	};
	const auto Press = [Player]()
	{
		Player->BeginAttack();
		Player->EndAttack();
	};

	// ── 손도끼가 가방에도 없다 ── 「손도끼 필요」, 누르면 휘두르지 않고 나무도 그대로.
	AGatherableActor* Tree = SpawnGatherable(World, TEXT("item.hatchet"));
	TestTrue(TEXT("빈손·빈 가방"), HoldTool(Player, NAME_None) && Own(NAME_None));
	Check(TEXT("손도끼 없이 나무"), ESnareInteractVerb::NeedHatchet, TEXT("손도끼 필요"));
	int32 SwingsBefore = Melee->GetSwingCount();
	Press();
	AddInfo(FString::Printf(TEXT("[AutoEquip] 「손도끼 필요」 누름 → 휘두르기 %d → %d · 나무 남음 %d"),
		SwingsBefore, Melee->GetSwingCount(), IsValid(Tree) && Tree->CanGather() ? 1 : 0));
	TestEqual(TEXT("「손도끼 필요」 누름은 휘두르지 않는다"), Melee->GetSwingCount(), SwingsBefore);
	TestTrue(TEXT("나무는 그대로"), IsValid(Tree) && Tree->CanGather());
	TestTrue(TEXT("간격도 안 건다"), Melee->IsActionReady());
	Tree->Destroy();

	// ── 낚시 포인트 ── 낚싯대가 없으면 「낚싯대 필요」(휘두르지 않음), 가방에만 있으면 「낚시」 → 누르면 들고 던진다.
	AFishingSpotActor* const Spot = World->SpawnActor<AFishingSpotActor>(FVector::ZeroVector, FRotator::ZeroRotator);
	if (Spot != nullptr && !Spot->HasActorBegunPlay())
	{
		Spot->DispatchBeginPlay();
	}
	TestTrue(TEXT("던질 거리 안에 포인트"), Fishing->HasSpotInCastRange());
	Check(TEXT("낚싯대 없이 포인트"), ESnareInteractVerb::NeedRod, TEXT("낚싯대 필요"));
	SwingsBefore = Melee->GetSwingCount();
	Press();
	TestEqual(TEXT("「낚싯대 필요」 누름은 휘두르지 않는다"), Melee->GetSwingCount(), SwingsBefore);
	TestEqual(TEXT("던지지도 않는다"), static_cast<int32>(Fishing->GetState()), static_cast<int32>(ETapFishingState::Idle));

	TestTrue(TEXT("낚싯대를 가방에"), Own(RodRow->ItemID));
	Check(TEXT("낚싯대 가방에만"), ESnareInteractVerb::Fish, TEXT("낚시"));
	Press();
	AddInfo(FString::Printf(TEXT("[AutoEquip] 「낚시」 누름 → 든 도구 %s · 낚시 단계 %d"),
		*Equipment->GetEquippedItem(EEquipSlot::Tool).ToString(), static_cast<int32>(Fishing->GetState())));
	TestEqual(TEXT("누르면 낚싯대를 든다"), Equipment->GetEquippedItem(EEquipSlot::Tool), FName(VerbRodRowName));
	TestNotEqual(TEXT("누르면 바로 던진다"), static_cast<int32>(Fishing->GetState()), static_cast<int32>(ETapFishingState::Idle));
	// 던진 줄이 있으면 버튼은 낚시 몫(「그만두기」)이다 — 나무 줄을 보기 전에 거둔다.
	Fishing->Quit();
	Spot->Destroy();

	// ── 손도끼가 가방에만 ── 「베기」, 누르면 손도끼를 들고 바로 벤다(같은 한 번).
	Tree = SpawnGatherable(World, TEXT("item.hatchet"));
	TestTrue(TEXT("손도끼를 가방에"), Own(TEXT("item.hatchet")));
	Check(TEXT("손도끼 가방에만"), ESnareInteractVerb::Chop, TEXT("베기"));
	Press();
	const FName HatchetRow = Equipment->FindEquipmentRowByItemID(TEXT("item.hatchet"));
	AddInfo(FString::Printf(TEXT("[AutoEquip] 「베기」 누름 → 든 도구 %s · 나무 남음 %d"),
		*Equipment->GetEquippedItem(EEquipSlot::Tool).ToString(), IsValid(Tree) && Tree->CanGather() ? 1 : 0));
	TestEqual(TEXT("누르면 손도끼를 든다"), Equipment->GetEquippedItem(EEquipSlot::Tool), HatchetRow);
	TestFalse(TEXT("같은 누름에 벤다"), IsValid(Tree) && Tree->CanGather());

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAttackVerbVendorOpenTest, "Snarehaven.HuntHud.AttackVerb.VendorOpensNeverSwings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAttackVerbVendorOpenTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* const Player =
		World->SpawnActor<ASnarehavenCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("플레이어 스폰"), Player) || !TestNotNull(TEXT("근접 공격"), Player->GetMeleeAttack()))
	{
		DestroyTestWorld(World);
		return false;
	}
	Player->DispatchBeginPlay();
	UMeleeAttackComponent* const Melee = Player->GetMeleeAttack();
	Melee->SetAttackSettings(TestAttackDamage, TestRangeCm, TestIntervalSeconds);
	Melee->SetNpcTalkRangeCm(TestTalkRangeCm);
	UHuntHudWidget* const Hud = CreateWidget<UHuntHudWidget>(World, UHuntHudWidget::StaticClass());
	if (!TestNotNull(TEXT("HUD 생성"), Hud))
	{
		DestroyTestWorld(World);
		return false;
	}
	Hud->BindToCharacter(Player);

	// 누름 하나 = 버튼 글자 확인 + 휘두르기 셈. 휘두르면 간격이 걸리니 휘두르는 누름은 맨 끝에 둔다.
	const auto PressAndCount = [this, Player, Hud, Melee](const TCHAR* What, ESnareInteractVerb Verb, const TCHAR* Text)
	{
		Melee->RefreshTarget();
		const ESnareInteractVerb Got = Player->GetInteractVerb();
		TestEqual(*FString::Printf(TEXT("%s 동사"), What), static_cast<int32>(Got), static_cast<int32>(Verb));
		TestEqual(*FString::Printf(TEXT("%s 버튼 글자"), What), Hud->GetAttackButtonText().ToString(), FString(Text));
		const int32 SwingsBefore = Melee->GetSwingCount();
		Player->BeginAttack();
		Player->EndAttack();
		AddInfo(FString::Printf(TEXT("[VendorOpen] %s → 버튼 「%s」 · 휘두르기 %d → %d"),
			What, *Hud->GetAttackButtonText().ToString(), SwingsBefore, Melee->GetSwingCount()));
		return Melee->GetSwingCount() - SwingsBefore;
	};

	// 상인 곁 — 「열기」, 누르면 휘두르지 않는다.
	AShopVendorActor* const Vendor =
		World->SpawnActor<AShopVendorActor>(FVector(TestNearDistanceCm, 0.f, 0.f), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("상인 스폰"), Vendor))
	{
		DestroyTestWorld(World);
		return false;
	}
	TestEqual(TEXT("가까운 상인을 찾는다"), AShopVendorActor::FindInTalkRange(Player), Vendor);
	TestEqual(TEXT("상인 곁 누름은 휘두르지 않는다"), PressAndCount(TEXT("상인 곁"), ESnareInteractVerb::Open, TEXT("열기")), 0);

	// 의뢰인도 같은 길 (상인을 물려받는다). 상인은 멀리 물린다.
	Vendor->SetActorLocation(FVector(TestFarDistanceCm, 0.f, 0.f));
	AQuestGiverActor* const Giver =
		World->SpawnActor<AQuestGiverActor>(FVector(0.f, TestNearDistanceCm, 0.f), FRotator::ZeroRotator);
	if (TestNotNull(TEXT("의뢰인 스폰"), Giver))
	{
		TestEqual(TEXT("가까운 쪽은 의뢰인"), AShopVendorActor::FindInTalkRange(Player), static_cast<AShopVendorActor*>(Giver));
		TestEqual(TEXT("의뢰인 곁 누름은 휘두르지 않는다"), PressAndCount(TEXT("의뢰인 곁"), ESnareInteractVerb::Open, TEXT("열기")), 0);
		Giver->Destroy();
	}

	// 과일나무 곁 (DESIGN 8.18) — 열매가 달린 다 자란 나무는 「따기」, 누르면 휘두르지 않는다. 빈 자리인데 과일이 없으면 「공격」.
	Vendor->Destroy();
	AFruitTreeActor* const Tree = World->SpawnActor<AFruitTreeActor>(FVector(TestNearDistanceCm, 0.f, 0.f), FRotator::ZeroRotator);
	if (TestNotNull(TEXT("나무 스폰"), Tree))
	{
		SnareFruitTree::FTreeView View;
		View.Slot = 0;
		View.Fruit = TEXT("item.pear");
		View.Stage = SnareFruitTree::EStage::Grown;
		View.Fruits = 3;
		Tree->ApplyView(View);
		TestEqual(TEXT("나무 곁 누름은 휘두르지 않는다"), PressAndCount(TEXT("열매 단 나무 곁"), ESnareInteractVerb::ShakeTree, TEXT("따기")), 0);
		View.Fruits = 0;
		Tree->ApplyView(View);
		Melee->RefreshTarget();
		TestEqual(TEXT("열매 딴 나무도 도끼 없이는 공격(실수로 베지 않음)"), static_cast<int32>(Player->GetInteractVerb()), static_cast<int32>(ESnareInteractVerb::Attack));
		View.Stage = SnareFruitTree::EStage::Empty;
		Tree->ApplyView(View);
		Melee->RefreshTarget();
		TestEqual(TEXT("빈 자리에 과일이 없으면 공격"), static_cast<int32>(Player->GetInteractVerb()), static_cast<int32>(ESnareInteractVerb::Attack));
		Tree->Destroy();
	}

	// 친구 나무 곁 (DESIGN 8.18 ④) — 내 나무가 없으면 「따기」(= 흔들기) 버튼이 뜨고, 누르면 휘두르지 않는다.
	if (AFruitTreeActor* const FriendTree = AFruitTreeActor::SpawnFriendTree(World,
			FTransform(FRotator::ZeroRotator, FVector(TestNearDistanceCm, 0.f, 0.f)), TEXT("HOSTONE"), TEXT("곰돌이")))
	{
		Melee->RefreshTarget();
		TestEqual(TEXT("친구 나무 곁 누름은 휘두르지 않는다"), PressAndCount(TEXT("친구 나무 곁"), ESnareInteractVerb::ShakeTree, TEXT("따기")), 0);
		FriendTree->SetActorLocation(FVector(TestFarDistanceCm, 0.f, 0.f));
		Melee->RefreshTarget();
		TestEqual(TEXT("친구 나무가 멀면 공격"), static_cast<int32>(Player->GetInteractVerb()), static_cast<int32>(ESnareInteractVerb::Attack));
		FriendTree->Destroy();
	}

	// 대화 거리 밖 — 다시 「공격」 이고 누르면 휘두른다(이 시험이 휘두르기를 셀 수 있다는 짝).
	TestNull(TEXT("멀면 상인 없음"), AShopVendorActor::FindInTalkRange(Player));
	TestEqual(TEXT("멀면 휘두른다"), PressAndCount(TEXT("상인 멀리"), ESnareInteractVerb::Attack, TEXT("공격")), 1);

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAttackVerbLighthouseLightTest, "Snarehaven.HuntHud.AttackVerb.LighthouseLight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAttackVerbLighthouseLightTest::RunTest(const FString& Parameters)
{
	// 스토리 25단계 — 등대 탑 앞에서만 상황 버튼이 「불 켜기」, 누르면 휘두르지 않고 눌림이 남아 버튼이 「공격」 으로 돌아간다.
	const int32 KnownBefore = SnareStory::GetKnownCurrentStep();
	const float Reach = SnareStory::ArrivalFor(SnareStory::TotalSteps).RadiusCm;
	const FVector Tower(SnareStory::LighthouseSpotX, SnareStory::LighthouseSpotY, 0.0);
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* const Player = World->SpawnActor<ASnarehavenCharacter>(Tower + FVector(Reach - TestNearDistanceCm, 0.f, 0.f), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("플레이어 스폰"), Player) || !TestNotNull(TEXT("근접 공격"), Player->GetMeleeAttack()))
	{
		DestroyTestWorld(World);
		return false;
	}
	Player->DispatchBeginPlay();
	UMeleeAttackComponent* const Melee = Player->GetMeleeAttack();
	Melee->SetAttackSettings(TestAttackDamage, TestRangeCm, TestIntervalSeconds);
	UHuntHudWidget* const Hud = CreateWidget<UHuntHudWidget>(World, UHuntHudWidget::StaticClass());
	if (!TestNotNull(TEXT("HUD 생성"), Hud))
	{
		DestroyTestWorld(World);
		return false;
	}
	Hud->BindToCharacter(Player);
	const auto Verb = [Player, Melee]()
	{
		Melee->RefreshTarget();
		return static_cast<int32>(Player->GetInteractVerb());
	};

	SnareStory::SetKnownCurrentStep(SnareStory::TotalSteps - 1);
	TestEqual(TEXT("24단계엔 탑 앞이어도 공격"), Verb(), static_cast<int32>(ESnareInteractVerb::Attack));
	SnareStory::SetKnownCurrentStep(SnareStory::TotalSteps);
	TestEqual(TEXT("25단계 탑 반경 안이면 불 켜기"), Verb(), static_cast<int32>(ESnareInteractVerb::LightBeacon));
	TestEqual(TEXT("버튼 글자"), Hud->GetAttackButtonText().ToString(), FString(TEXT("불 켜기")));
	Player->SetActorLocation(Tower + FVector(Reach + TestNearDistanceCm, 0.f, 0.f));
	TestEqual(TEXT("탑 반경 밖이면 공격"), Verb(), static_cast<int32>(ESnareInteractVerb::Attack));
	TestFalse(TEXT("밖에서 누르기 전엔 안 켬"), Player->HasPressedLighthouseLight());
	Player->SetActorLocation(Tower + FVector(Reach - TestNearDistanceCm, 0.f, 0.f));

	const int32 SwingsBefore = Melee->GetSwingCount();
	Melee->RefreshTarget();
	Player->BeginAttack();
	Player->EndAttack();
	AddInfo(FString::Printf(TEXT("[LighthouseLight] 누름 → 켬 %d · 휘두르기 %d → %d · 버튼 「%s」"),
		Player->HasPressedLighthouseLight() ? 1 : 0, SwingsBefore, Melee->GetSwingCount(), *Hud->GetAttackButtonText().ToString()));
	TestTrue(TEXT("누르면 불 켜기가 남는다"), Player->HasPressedLighthouseLight());
	TestEqual(TEXT("불 켜기 누름은 휘두르지 않는다"), Melee->GetSwingCount(), SwingsBefore);
	TestEqual(TEXT("누른 뒤엔 다시 공격"), Verb(), static_cast<int32>(ESnareInteractVerb::Attack));

	SnareStory::SetKnownCurrentStep(KnownBefore);
	DestroyTestWorld(World);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
