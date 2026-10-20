// ROADMAP M17-01 2층 검사 — 첫 실행 안내가 열한 장이고(INBOX 18:10 ③ 줍기·손도끼·베기 3장 · M28-07 텐트·작업대·불 3장 더함), 넘기면 글이 바뀌며, 끝내거나 건너뛰면 끝나고,
// 저장 값이 실제 설정에 읽히고 써지는지. 판정은 위젯이 들고 있는 실제 글·저장 값이다 (PROMPT 12).
// 채집 3장은 백엔드 스냅샷으로 가방이 바뀔 때만 저절로 넘어가는지 — 실제 캐릭터 인벤토리로 본다.
// 집·제작 3장(M28-07)은 서버 저장값(`ApplyBackendTents`)·스냅샷으로만 넘어가는지 — 실제 게임 인스턴스의 거점 창구로 본다.
// 실행: UnrealEditor-Cmd <uproject 절대경로> -ExecCmds="Automation RunTests Snarehaven.UI.FirstRunGuide; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Blueprint/WidgetTree.h"
#include "Building/SnareBaseSubsystem.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/GameInstance.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Inventory/InventoryComponent.h"
#include "SnarehavenCharacter.h"
#include "UI/SnareFirstRunGuideWidget.h"

namespace
{
	FString GuideLine(USnareFirstRunGuideWidget& Guide)
	{
		const UTextBlock* const Text = Cast<UTextBlock>(Guide.WidgetTree->FindWidget(TEXT("GuideStepText")));
		return Text != nullptr ? Text->GetText().ToString() : FString();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenFirstRunGuideTest, "Snarehaven.UI.FirstRunGuide.AllStepsAndSkip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenFirstRunGuideTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());

	TestEqual(TEXT("안내는 열한 장"), USnareFirstRunGuideWidget::GetSteps().Num(), 11);

	// 끝까지 넘기기
	USnareFirstRunGuideWidget* const Guide = CreateWidget<USnareFirstRunGuideWidget>(World, USnareFirstRunGuideWidget::StaticClass());
	if (TestNotNull(TEXT("안내 위젯"), Guide))
	{
		Guide->bPersistSeen = false;   // 사용자 저장을 건드리지 않는다
		Guide->TakeWidget();
		TestEqual(TEXT("처음엔 첫 장"), Guide->GetStepIndex(), 0);
		TestTrue(TEXT("첫 장 글에 1/11"), GuideLine(*Guide).Contains(TEXT("1/11")));
		Guide->Advance();
		TestEqual(TEXT("다음 장"), Guide->GetStepIndex(), 1);
		TestTrue(TEXT("둘째 장 글에 2/11"), GuideLine(*Guide).Contains(TEXT("2/11")));
		TestTrue(TEXT("둘째 장 = 마른 가지 줍기"), GuideLine(*Guide).Contains(TEXT("마른 가지")));
		TestFalse(TEXT("아직 안 끝남"), Guide->IsFinished());
		for (int32 Index = 0; Index < 10; ++Index)
		{
			Guide->Advance();
		}
		TestTrue(TEXT("열한 번 넘기면 끝"), Guide->IsFinished());
	}

	// 건너뛰기
	USnareFirstRunGuideWidget* const Skipped = CreateWidget<USnareFirstRunGuideWidget>(World, USnareFirstRunGuideWidget::StaticClass());
	if (TestNotNull(TEXT("건너뛸 위젯"), Skipped))
	{
		Skipped->bPersistSeen = false;
		Skipped->TakeWidget();
		Skipped->Skip();
		TestTrue(TEXT("건너뛰면 바로 끝"), Skipped->IsFinished());
	}

	// 저장 값 왕복 — 원래 값을 되돌려 놓는다
	const bool bOriginal = USnareFirstRunGuideWidget::IsGuideSeen();
	USnareFirstRunGuideWidget::SetGuideSeen(false);
	TestFalse(TEXT("저장 값 false 를 읽는다"), USnareFirstRunGuideWidget::IsGuideSeen());
	USnareFirstRunGuideWidget::SetGuideSeen(true);
	TestTrue(TEXT("저장 값 true 를 읽는다"), USnareFirstRunGuideWidget::IsGuideSeen());
	TestFalse(TEXT("봤으면 다시 안 띄운다"), USnareFirstRunGuideWidget::ShouldShowGuide());
	USnareFirstRunGuideWidget::SetGuideSeen(bOriginal);

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenFirstRunGuideGatherTest, "Snarehaven.UI.FirstRunGuide.GatherSteps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenFirstRunGuideGatherTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());

	ASnarehavenCharacter* const Player = World->SpawnActor<ASnarehavenCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
	USnareFirstRunGuideWidget* const Guide = CreateWidget<USnareFirstRunGuideWidget>(World, USnareFirstRunGuideWidget::StaticClass());
	if (!TestNotNull(TEXT("플레이어"), Player) || !TestNotNull(TEXT("인벤토리"), Player->GetInventory())
		|| !TestNotNull(TEXT("안내 위젯"), Guide))
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return false;
	}
	Guide->bPersistSeen = false;   // 사용자 저장을 건드리지 않는다
	// 붙잡는 부모가 없어 TakeWidget 뒤 NativeDestruct 가 올 수 있으니 묶기는 그다음.
	Guide->TakeWidget();
	Guide->BindToCharacter(Player);

	// 가진 것을 바꾸는 길은 백엔드 스냅샷 하나다 (CLAUDE.md 8).
	const auto Own = [Player](int32 Wood, int32 Hatchets)
	{
		TArray<FInventorySlot> Slots;
		if (Wood > 0)
		{
			FInventorySlot& Slot = Slots.AddDefaulted_GetRef();
			Slot.ItemID = TEXT("item.wood");
			Slot.Count = Wood;
		}
		if (Hatchets > 0)
		{
			FInventorySlot& Slot = Slots.AddDefaulted_GetRef();
			Slot.ItemID = TEXT("item.hatchet");
			Slot.Count = Hatchets;
		}
		return Player->GetInventory()->ApplyBackendSnapshot(Slots);
	};
	const auto At = [this, Guide](int32 Expected, const TCHAR* What, const TCHAR* Keyword)
	{
		const FString Line = GuideLine(*Guide);
		AddInfo(FString::Printf(TEXT("[GuideGather] %s → %d/%d장 「%s」"), What, Guide->GetStepIndex() + 1,
			USnareFirstRunGuideWidget::GetSteps().Num(), *Line));
		TestEqual(*FString::Printf(TEXT("%s 장 번호"), What), Guide->GetStepIndex(), Expected);
		TestTrue(*FString::Printf(TEXT("%s 글에 '%s'"), What, Keyword), Line.Contains(Keyword));
	};

	// 걷기 장만 손으로 넘긴다 — 아래 채집 3장은 손 넘김 없다.
	Guide->Advance();
	At(1, TEXT("걷기 넘김"), TEXT("마른 가지"));
	// 서버가 나뭇가지를 넣어 준 스냅샷 → 손도끼 장.
	TestTrue(TEXT("가지 1"), Own(1, 0));
	At(2, TEXT("가지 들어옴"), TEXT("잡화점"));
	// 손도끼 장에서 가지를 더 주워도 그 장에 남는다.
	TestTrue(TEXT("가지 2"), Own(2, 0));
	At(2, TEXT("가지 더 주움"), TEXT("손도끼"));
	// 손도끼를 산 스냅샷 → 베기 장.
	TestTrue(TEXT("손도끼 1"), Own(2, 1));
	At(3, TEXT("손도끼 들어옴"), TEXT("베기 버튼"));
	// 가지 수가 그대로인 스냅샷은 안 넘긴다, 나무를 베어 늘면 공격 장.
	TestTrue(TEXT("변화 없음"), Own(2, 1));
	At(3, TEXT("가지 그대로"), TEXT("베기 버튼"));
	TestTrue(TEXT("나무 4"), Own(6, 1));
	At(4, TEXT("나무 벰"), TEXT("공격 버튼"));

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenFirstRunGuideBuildTest, "Snarehaven.UI.FirstRunGuide.BuildSteps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenFirstRunGuideBuildTest::RunTest(const FString& Parameters)
{
	UGameInstance* const GameInstance = NewObject<UGameInstance>(GEngine);
	GameInstance->InitializeStandalone();
	UWorld* const World = GameInstance->GetWorld();
	USnareBaseSubsystem* const Base = GameInstance->GetSubsystem<USnareBaseSubsystem>();
	if (!TestNotNull(TEXT("월드"), World) || !TestNotNull(TEXT("거점 창구"), Base))
	{
		return false;
	}
	World->InitializeActorsForPlay(FURL());
	TestNotNull(TEXT("아이템 표에 모닥불 줄"), Base->FindItemRow(TEXT("item.campfire")));

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ASnarehavenCharacter* const Player = World->SpawnActor<ASnarehavenCharacter>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
	USnareFirstRunGuideWidget* const Guide = CreateWidget<USnareFirstRunGuideWidget>(World, USnareFirstRunGuideWidget::StaticClass());
	const auto CleanUp = [GameInstance, World]()
	{
		GameInstance->Shutdown();
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	};
	if (!TestNotNull(TEXT("플레이어"), Player) || !TestNotNull(TEXT("인벤토리"), Player->GetInventory())
		|| !TestNotNull(TEXT("안내 위젯"), Guide))
	{
		CleanUp();
		return false;
	}
	Guide->bPersistSeen = false;   // 사용자 저장을 건드리지 않는다
	Guide->TakeWidget();
	Guide->BindToCharacter(Player);   // 게임 인스턴스의 거점 창구까지 건다

	// 서버 `GetBaseState` 의 `tents` 모양 — 텐트 하나, 그 땅의 가구들. 내 것은 시각이 있다.
	const auto ServerTents = [Base](bool bOwn, const TArray<FString>& Furniture)
	{
		TSharedRef<FJsonObject> Tent = MakeShared<FJsonObject>();
		Tent->SetStringField(TEXT("tentId"), TEXT("tent_guide_test"));
		Tent->SetStringField(TEXT("zone"), TEXT("ranch"));
		Tent->SetNumberField(TEXT("x"), 0);
		Tent->SetNumberField(TEXT("y"), 0);
		if (bOwn)
		{
			Tent->SetStringField(TEXT("placedAtUtc"), FDateTime::UtcNow().ToIso8601());
		}
		TArray<TSharedPtr<FJsonValue>> Items;
		for (int32 Index = 0; Index < Furniture.Num(); ++Index)
		{
			TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
			Item->SetStringField(TEXT("structureId"), FString::Printf(TEXT("s_guide_%d"), Index));
			Item->SetStringField(TEXT("itemId"), Furniture[Index]);
			Item->SetNumberField(TEXT("x"), 1 + Index);
			Item->SetNumberField(TEXT("y"), 1);
			Items.Add(MakeShared<FJsonValueObject>(Item));
		}
		Tent->SetArrayField(TEXT("items"), Items);
		TArray<TSharedPtr<FJsonValue>> Tents;
		Tents.Add(MakeShared<FJsonValueObject>(Tent));
		Base->ApplyBackendTents(Tents);
	};
	const auto OwnPlanks = [Player](int32 Planks)
	{
		TArray<FInventorySlot> Slots;
		FInventorySlot& Slot = Slots.AddDefaulted_GetRef();
		Slot.ItemID = TEXT("item.plank");
		Slot.Count = Planks;
		return Player->GetInventory()->ApplyBackendSnapshot(Slots);
	};
	const auto At = [this, Guide](int32 Expected, const TCHAR* What, const TCHAR* Keyword)
	{
		const FString Line = GuideLine(*Guide);
		AddInfo(FString::Printf(TEXT("[GuideBuild] %s → %d/%d장 「%s」"), What, Guide->GetStepIndex() + 1,
			USnareFirstRunGuideWidget::GetSteps().Num(), *Line));
		TestEqual(*FString::Printf(TEXT("%s 장 번호"), What), Guide->GetStepIndex(), Expected);
		TestTrue(*FString::Printf(TEXT("%s 글에 '%s'"), What, Keyword), Line.Contains(Keyword));
	};

	// 공격 장에서 텐트가 서도 앞 장들을 건너뛰지 않는다.
	for (int32 Index = 0; Index < 4; ++Index)
	{
		Guide->Advance();
	}
	ServerTents(true, {});
	At(4, TEXT("공격 장에서 텐트 섬"), TEXT("공격 버튼"));
	TArray<TSharedPtr<FJsonValue>> NoTents;
	Base->ApplyBackendTents(NoTents);

	// 수확 장까지 손으로 → 텐트 없음 = 텐트 장.
	for (int32 Index = 0; Index < 4; ++Index)
	{
		Guide->Advance();
	}
	At(8, TEXT("텐트 없음"), TEXT("텐트를 사고"));
	// 남의 텐트 사본(시각 없음)은 넘기지 않는다.
	ServerTents(false, {});
	At(8, TEXT("남의 텐트"), TEXT("텐트를 사고"));
	// 서버가 내 텐트를 저장했다 → 작업대 장.
	ServerTents(true, {});
	At(9, TEXT("내 텐트 섬"), TEXT("나무판"));
	// 나무판 그대로면 남고, 서버가 만들어 준 나무판이 들어오면 → 불 장.
	OwnPlanks(0);   // 빈 가방 그대로 — 바뀐 게 없어 거짓을 돌려준다
	At(9, TEXT("나무판 없음"), TEXT("나무판"));
	TestTrue(TEXT("나무판 1"), OwnPlanks(1));
	At(10, TEXT("나무판 들어옴"), TEXT("모닥불"));
	// 불이 아닌 가구(보관함)로는 안 넘기고, 모닥불이 서면 끝.
	ServerTents(true, { TEXT("item.storage_box") });
	At(10, TEXT("보관함만"), TEXT("모닥불"));
	TestFalse(TEXT("아직 안 끝남"), Guide->IsFinished());
	ServerTents(true, { TEXT("item.storage_box"), TEXT("item.campfire") });
	TestTrue(TEXT("모닥불이 서면 안내 끝"), Guide->IsFinished());

	// 텐트·모닥불을 이미 가진 사람은 텐트 장에 들어서자마자 작업대 장으로.
	USnareFirstRunGuideWidget* const Veteran = CreateWidget<USnareFirstRunGuideWidget>(World, USnareFirstRunGuideWidget::StaticClass());
	if (TestNotNull(TEXT("두 번째 안내"), Veteran))
	{
		Veteran->bPersistSeen = false;
		Veteran->TakeWidget();
		Veteran->BindToBase(Base);
		for (int32 Index = 0; Index < 8; ++Index)
		{
			Veteran->Advance();
		}
		AddInfo(FString::Printf(TEXT("[GuideBuild] 텐트 가진 채 수확 장 넘김 → %d/%d장"), Veteran->GetStepIndex() + 1,
			USnareFirstRunGuideWidget::GetSteps().Num()));
		TestEqual(TEXT("텐트 장을 바로 넘긴다"), Veteran->GetStepIndex(), 9);
	}

	CleanUp();
	return true;
}

#endif
