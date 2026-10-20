// M36-02a·02b 2층 검사 — 물 버튼 줄(`UWaterActionWidget`)의 낚시 버튼 (DESIGN 8.13) · 배·탈것 버튼 (DESIGN 8.9·8.3).
// 보는 것: ① 낚시 단계(대 안 듦·대 들고 물가·던짐·입질 대기 응답·연타·잡는 중)마다 보이는 버튼·! 카드가 맞는가
// ② 「낚시」·「감기」·「그만두기」를 누르면 `UTapFishingComponent` 의 `CastAtNearest`·`Tap`·`Quit` 길로 단계가 바뀌는가
// ③ 그만두면 까닭 글자가 보이고, 다 감으면 `CatchFish` 를 한 번 부른다(지급 판정은 서버 — 위젯은 보이기·누르기만).
// ④ 뭍·물가·배 위·배 곁·탈것 곁마다 배·탈것 버튼이 맞는 것만 뜨고(같은 글자 둘 0 — 「포획」 둘 결함 02:40 꼴),
//    누르면 `LaunchBoat`·`Board`·`Leave`·`Stow`·`ToggleMount`·`CycleMountSpecies` 길로 간다.
// ⑤ 배 위 버튼 4칸이 오른쪽 아이콘 기둥·HUD 버튼과 겹치지 않고, 배 보관함 판엔 쪽 단추 줄이 없다(INBOX 결함 10-07 01:18 ①④).
// ⑥ 권총을 들면 뜨는 탄 글·재장전 버튼도 같은 아이콘 기둥·HUD 버튼과 겹치지 않는다(M39-06b 루프#578 — 게임 카메라 캡처에서 배낭·지도 아이콘에 덮여 안 보였다).
// 서버는 `USnareBackendSubsystem::TestServerOverride` 가짜로 대신하고, 답을 붙들어 「응답 전」 화면을 본다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.UI.WaterAction; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Backend/SnareBackendSubsystem.h"
#include "Backend/SnarePlayerSnapshot.h"
#include "Blueprint/UserWidget.h"
#include "Building/SnareBaseBuildComponent.h"
#include "Building/SnareBaseSubsystem.h"
#include "Components/Button.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Equipment/EquipmentComponent.h"
#include "Gathering/TapFishingComponent.h"
#include "Player/MountComponent.h"
#include "SnarehavenCharacter.h"
#include "UI/WaterActionWidget.h"
#include "World/BoatActor.h"
#include "World/FishingSpotActor.h"
#include "World/SeaActor.h"
#include "World/WaterRules.h"
#include "World/WaterTravelComponent.h"
#include "Blueprint/WidgetTree.h"
#include "Layout/ArrangedChildren.h"
#include "Layout/Geometry.h"
#include "UI/AttendanceScreenWidget.h"
#include "UI/BackpackScreenWidget.h"
#include "UI/CraftingScreenWidget.h"
#include "UI/HuntHudWidget.h"
#include "UI/RanchHudWidget.h"
#include "UI/SnareUiStyle.h"
#include "UI/WorldMapScreenWidget.h"

namespace SnarehavenWaterActionWidgetTest
{
	using FDone = USnareBackendSubsystem::FServerCallDone;

	// 검사 전용 자리 (Verification.md 6): 섬 안쪽 한 점, 포인트는 앞 3m (던질 거리 안).
	const FVector TestLandPoint(0.f, 0.f, 500.f);
	const FVector TestSpotOffset(300.f, 0.f, 0.f);
	const FName TestRodItemId(TEXT("item.rod_basic"));
	const TCHAR* TestFishItemId = TEXT("item.carp");
	const TCHAR* TestToken = TEXT("test-token");

	UWorld* CreateTestWorld()
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
		FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
		WorldContext.SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		World->BeginPlay();
		return World;
	}

	void DestroyTestWorld(UWorld* World)
	{
		USnareBackendSubsystem::TestServerOverride = nullptr;
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	}

	ASnarehavenCharacter* SpawnPlayer(UWorld* World)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ASnarehavenCharacter* const Player = World->SpawnActor<ASnarehavenCharacter>(TestLandPoint, FRotator::ZeroRotator, Params);
		if (Player && !Player->HasActorBegunPlay())
		{
			Player->DispatchBeginPlay();
		}
		return Player;
	}

	AFishingSpotActor* SpawnSpot(UWorld* World)
	{
		const FTransform At(TestLandPoint + TestSpotOffset);
		AFishingSpotActor* const Spot = World->SpawnActorDeferred<AFishingSpotActor>(AFishingSpotActor::StaticClass(), At);
		if (Spot)
		{
			Spot->SpotId = TEXT("pond_1");
			Spot->FinishSpawning(At);
			if (!Spot->HasActorBegunPlay())
			{
				Spot->DispatchBeginPlay();
			}
		}
		return Spot;
	}

	/** 가짜 서버: `RollFish`·`CatchFish` 를 세고, 켜 두면 답을 붙들어 「응답 전」 을 보게 한다. 다른 요청은 센다. */
	struct FFakeServer
	{
		int32 RollCalls = 0;
		int32 CatchCalls = 0;
		int32 OtherCalls = 0;
		bool bHoldRoll = false;
		bool bHoldCatch = false;
		FDone HeldDone;
		TSharedPtr<FJsonObject> HeldResponse;

		void Install()
		{
			USnareBackendSubsystem::TestServerOverride =
				[this](const FString& Name, const TSharedPtr<FJsonObject>& Args, FDone Done)
				{
					TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
					bool bHold = false;
					if (Name == TEXT("RollFish"))
					{
						++RollCalls;
						Response->SetStringField(TEXT("token"), TestToken);
						Response->SetStringField(TEXT("fishItemId"), TestFishItemId);
						bHold = bHoldRoll;
					}
					else if (Name == TEXT("CatchFish"))
					{
						++CatchCalls;
						Response->SetStringField(TEXT("caught"), TestFishItemId);
						bHold = bHoldCatch;
					}
					else
					{
						++OtherCalls;
					}
					if (bHold)
					{
						HeldDone = Done;
						HeldResponse = Response;
						return;
					}
					Done(true, Response, FString());
				};
		}

		void ReleaseHeld()
		{
			if (HeldDone)
			{
				FDone Done = MoveTemp(HeldDone);
				HeldDone = nullptr;
				Done(true, HeldResponse, FString());
			}
		}
	};

	bool IsShown(const UWidget* Widget)
	{
		return Widget && Widget->GetVisibility() == ESlateVisibility::Visible;
	}

	FString ButtonLabel(const UButton* Button)
	{
		const USizeBox* const Box = Button ? Cast<USizeBox>(Button->GetChildAt(0)) : nullptr;
		const UTextBlock* const Text = Box ? Cast<UTextBlock>(Box->GetChildAt(0)) : nullptr;
		return Text ? Text->GetText().ToString() : FString();
	}

	/** 화면에 붙인 위젯과 그 버튼들 — 이름은 자리표시 트리가 세운 이름 그대로. */
	struct FView
	{
		UWaterActionWidget* Widget = nullptr;
		UButton* Cast = nullptr;
		UButton* Tap = nullptr;
		UButton* Quit = nullptr;
		UTextBlock* Alert = nullptr;
		UTextBlock* Message = nullptr;
		TArray<UButton*> Others;

		bool Build(UWorld* World, ASnarehavenCharacter* Player)
		{
			Widget = CreateWidget<UWaterActionWidget>(World, UWaterActionWidget::StaticClass());
			if (Widget == nullptr)
			{
				return false;
			}
			// 자리표시 트리는 RebuildWidget 이 세운다 — 묶기는 그다음 (HuntHud 검사와 같은 순서).
			Widget->TakeWidget();
			Widget->BindToPawn(Player->FindComponentByClass<UWaterTravelComponent>(), Player->FindComponentByClass<UTapFishingComponent>());
			Cast = ::Cast<UButton>(Widget->GetWidgetFromName(TEXT("FishCastButton")));
			Tap = ::Cast<UButton>(Widget->GetWidgetFromName(TEXT("FishTapButton")));
			Quit = ::Cast<UButton>(Widget->GetWidgetFromName(TEXT("FishQuitButton")));
			Alert = ::Cast<UTextBlock>(Widget->GetWidgetFromName(TEXT("FishBiteAlert")));
			Message = ::Cast<UTextBlock>(Widget->GetWidgetFromName(TEXT("WaterMessage")));
			for (const TCHAR* Name : { TEXT("BoatLaunchButton"), TEXT("BoatBoardButton"), TEXT("BoatLeaveButton"), TEXT("BoatStowButton"),
				TEXT("BoatStorageButton"), TEXT("MountToggleButton"), TEXT("MountPickButton") })
			{
				Others.Add(::Cast<UButton>(Widget->GetWidgetFromName(Name)));
			}
			return Cast && Tap && Quit && Alert && Message && !Others.Contains(nullptr);
		}

		int32 CountOthersShown() const
		{
			int32 Count = 0;
			for (const UButton* Button : Others)
			{
				Count += IsShown(Button) ? 1 : 0;
			}
			return Count;
		}
	};
}

// ── ① 단계마다 보이는 것 ──

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenWaterActionFishingStatesTest, "Snarehaven.UI.WaterAction.Fishing.ButtonsFollowState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSnarehavenWaterActionFishingStatesTest::RunTest(const FString& Parameters)
{
	using namespace SnarehavenWaterActionWidgetTest;
	UWorld* const World = CreateTestWorld();
	FFakeServer Server;
	Server.bHoldRoll = true;
	Server.bHoldCatch = true;
	Server.Install();
	ASnarehavenCharacter* const Player = SpawnPlayer(World);
	UTapFishingComponent* const Fishing = Player ? Player->FindComponentByClass<UTapFishingComponent>() : nullptr;
	UEquipmentComponent* const Equipment = Player ? Player->FindComponentByClass<UEquipmentComponent>() : nullptr;
	AFishingSpotActor* const Spot = SpawnSpot(World);
	FView View;
	if (!TestNotNull(TEXT("낚시 컴포넌트"), Fishing) || !TestNotNull(TEXT("장비"), Equipment) || !TestNotNull(TEXT("포인트"), Spot)
		|| !TestTrue(TEXT("물 버튼 줄 위젯과 버튼들"), View.Build(World, Player)))
	{
		DestroyTestWorld(World);
		return false;
	}
	const FWaterActivityRow* const Row = SnareWater::LoadRow();
	if (!TestNotNull(TEXT("실제 물 표"), Row) || !TestTrue(TEXT("포인트가 던질 거리 안"), TestSpotOffset.Size() < Row->CastRangeCm))
	{
		DestroyTestWorld(World);
		return false;
	}
	auto Expect = [this, &View](const TCHAR* When, bool bCast, bool bTap, bool bQuit, bool bAlert)
	{
		View.Widget->RefreshView();
		TestEqual(FString::Printf(TEXT("%s: 「낚시」"), When), IsShown(View.Cast), bCast);
		TestEqual(FString::Printf(TEXT("%s: 「감기」"), When), IsShown(View.Tap), bTap);
		TestEqual(FString::Printf(TEXT("%s: 「그만두기」"), When), IsShown(View.Quit), bQuit);
		TestEqual(FString::Printf(TEXT("%s: ! 카드"), When), IsShown(View.Alert), bAlert);
		TestEqual(FString::Printf(TEXT("%s: 배·탈것 버튼 0"), When), View.CountOthersShown(), 0);
	};

	TestEqual(TEXT("버튼 글자 「낚시」"), ButtonLabel(View.Cast), FString(TEXT("낚시")));
	TestEqual(TEXT("버튼 글자 「감기」"), ButtonLabel(View.Tap), FString(TEXT("감기")));
	TestEqual(TEXT("버튼 글자 「그만두기」"), ButtonLabel(View.Quit), FString(TEXT("그만두기")));

	Expect(TEXT("대 안 듦"), false, false, false, false);
	if (!TestTrue(TEXT("낚싯대를 들었다"), Equipment->EquipItemByItemID(TestRodItemId)))
	{
		DestroyTestWorld(World);
		return false;
	}
	Expect(TEXT("대 들고 물가"), true, false, false, false);

	Fishing->CastAtNearest();
	TestTrue(TEXT("던짐 → 입질 대기"), Fishing->GetState() == ETapFishingState::WaitingBite);
	Expect(TEXT("입질 대기(그만두기는 상황 버튼 몫)"), false, false, false, false);

	Fishing->AdvanceFishing(Fishing->GetBiteRemainingSeconds() + 0.1f);
	TestTrue(TEXT("입질 → 서버 답 기다림"), Fishing->GetState() == ETapFishingState::Rolling);
	TestEqual(TEXT("RollFish 한 번"), Server.RollCalls, 1);
	Expect(TEXT("입질 답 전"), false, false, false, true);

	Server.ReleaseHeld();
	TestTrue(TEXT("답 → 연타"), Fishing->GetState() == ETapFishingState::Tapping);
	Expect(TEXT("연타 첫 누름 전"), false, true, true, true);

	Fishing->Tap();
	Expect(TEXT("연타 중"), false, true, true, false);

	while (Fishing->GetState() == ETapFishingState::Tapping && Fishing->GetPressCount() < Fishing->GetPressesNeeded() + 1)
	{
		Fishing->Tap();
	}
	TestTrue(TEXT("다 감음 → 잡는 중"), Fishing->GetState() == ETapFishingState::Catching);
	Expect(TEXT("잡는 중(답 전)"), false, false, false, false);

	Server.ReleaseHeld();
	TestTrue(TEXT("잡음 → 쉬는 중"), Fishing->GetState() == ETapFishingState::Idle);
	Expect(TEXT("잡은 뒤"), true, false, false, false);
	TestEqual(TEXT("CatchFish 한 번"), Server.CatchCalls, 1);
	TestEqual(TEXT("다른 서버 요청 0"), Server.OtherCalls, 0);

	DestroyTestWorld(World);
	return true;
}

// ── ② 누르면 맞는 컴포넌트 길 ──

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenWaterActionFishingClicksTest, "Snarehaven.UI.WaterAction.Fishing.ButtonsCallComponent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSnarehavenWaterActionFishingClicksTest::RunTest(const FString& Parameters)
{
	using namespace SnarehavenWaterActionWidgetTest;
	UWorld* const World = CreateTestWorld();
	FFakeServer Server;
	Server.Install();
	ASnarehavenCharacter* const Player = SpawnPlayer(World);
	UTapFishingComponent* const Fishing = Player ? Player->FindComponentByClass<UTapFishingComponent>() : nullptr;
	UEquipmentComponent* const Equipment = Player ? Player->FindComponentByClass<UEquipmentComponent>() : nullptr;
	AFishingSpotActor* const Spot = SpawnSpot(World);
	FView View;
	if (!TestNotNull(TEXT("낚시 컴포넌트"), Fishing) || !TestNotNull(TEXT("장비"), Equipment) || !TestNotNull(TEXT("포인트"), Spot)
		|| !TestTrue(TEXT("물 버튼 줄 위젯과 버튼들"), View.Build(World, Player))
		|| !TestTrue(TEXT("낚싯대를 들었다"), Equipment->EquipItemByItemID(TestRodItemId)))
	{
		DestroyTestWorld(World);
		return false;
	}

	// 「낚시」 → CastAtNearest: 입질 대기, 서버는 아직 안 부른다(입질 순간에 부른다).
	View.Cast->OnClicked.Broadcast();
	TestTrue(TEXT("「낚시」 → 입질 대기"), Fishing->GetState() == ETapFishingState::WaitingBite);
	TestEqual(TEXT("던질 때 서버 요청 0"), Server.RollCalls + Server.CatchCalls + Server.OtherCalls, 0);

	// 「그만두기」(입질 전) → Quit: 쉬는 중 + 까닭 글자.
	View.Quit->OnClicked.Broadcast();
	TestTrue(TEXT("「그만두기」 → 쉬는 중"), Fishing->GetState() == ETapFishingState::Idle);
	View.Widget->RefreshView();
	TestFalse(TEXT("그만둔 까닭 글자가 보인다"), View.Message->GetText().IsEmpty());
	AddInfo(FString::Printf(TEXT("그만둔 뒤 글자 [%s]"), *View.Message->GetText().ToString()));
	TestEqual(TEXT("입질 전 그만두면 RollFish 0"), Server.RollCalls, 0);

	// 다시 던져 입질 → 「감기」 를 누름 수만큼 → CatchFish 한 번, 표 그대로.
	View.Cast->OnClicked.Broadcast();
	Fishing->AdvanceFishing(Fishing->GetBiteRemainingSeconds() + 0.1f);
	if (!TestTrue(TEXT("입질 → 연타"), Fishing->GetState() == ETapFishingState::Tapping))
	{
		DestroyTestWorld(World);
		return false;
	}
	const int32 Needed = Fishing->GetPressesNeeded();
	for (int32 Press = 0; Press < Needed - 1; ++Press)
	{
		View.Tap->OnClicked.Broadcast();
	}
	TestEqual(TEXT("「감기」 한 번 = 누름 한 번"), Fishing->GetPressCount(), Needed - 1);
	TestTrue(TEXT("하나 모자라면 아직 연타"), Fishing->GetState() == ETapFishingState::Tapping);
	TestEqual(TEXT("하나 모자라면 CatchFish 0"), Server.CatchCalls, 0);
	View.Tap->OnClicked.Broadcast();
	TestEqual(TEXT("다 감으면 CatchFish 한 번"), Server.CatchCalls, 1);
	TestTrue(TEXT("잡은 어종은 서버 답"), Fishing->GetLastCaughtItem() == FName(TestFishItemId));
	AddInfo(FString::Printf(TEXT("「감기」 %d번 → CatchFish %d번"), Fishing->GetPressCount(), Server.CatchCalls));

	// 연타 중 「그만두기」 → 놓침, CatchFish 더 안 부름.
	View.Cast->OnClicked.Broadcast();
	Fishing->AdvanceFishing(Fishing->GetBiteRemainingSeconds() + 0.1f);
	View.Tap->OnClicked.Broadcast();
	View.Quit->OnClicked.Broadcast();
	TestTrue(TEXT("연타 중 「그만두기」 → 쉬는 중"), Fishing->GetState() == ETapFishingState::Idle);
	TestEqual(TEXT("그만두면 CatchFish 그대로 1"), Server.CatchCalls, 1);
	TestEqual(TEXT("다른 서버 요청 0"), Server.OtherCalls, 0);

	DestroyTestWorld(World);
	return true;
}

// ── ③ 배·탈것 버튼 (M36-02b) ──

namespace SnarehavenWaterActionWidgetTest
{
	// 검사 전용 자리 (Verification.md 6): 섬 가장자리(2500)를 넘은 바다 한 점. 종·아이템은 실제 표의 ID.
	const FVector TestSeaPoint(2800.f, 0.f, 0.f);
	const FName TestLifeboatId(TEXT("item.lifeboat"));
	const FName TestWolf(TEXT("animal.wolf"));
	const FName TestFox(TEXT("animal.fox"));

	const TCHAR* const BoatAndMountButtons[] = { TEXT("BoatLaunchButton"), TEXT("BoatBoardButton"), TEXT("BoatLeaveButton"),
		TEXT("BoatStowButton"), TEXT("BoatStorageButton"), TEXT("MountToggleButton"), TEXT("MountPickButton") };
	const TCHAR* const FishingButtons[] = { TEXT("FishCastButton"), TEXT("FishTapButton"), TEXT("FishQuitButton") };

	/** 가짜 서버: 배 띄우기·넣기를 세고, 켜 두면 띄우기를 거절한다. */
	struct FBoatServer
	{
		int32 LaunchCalls = 0;
		int32 StowCalls = 0;
		int32 OtherCalls = 0;
		FString RefuseLaunchCode;

		void Install()
		{
			USnareBackendSubsystem::TestServerOverride =
				[this](const FString& Name, const TSharedPtr<FJsonObject>& Args, FDone Done)
				{
					TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
					if (Name == TEXT("LaunchBoat"))
					{
						++LaunchCalls;
						if (!RefuseLaunchCode.IsEmpty())
						{
							// 서버 `fail()` 꼴 — 성공 응답 안의 `{error, code}`.
							Response->SetBoolField(TEXT("error"), true);
							Response->SetStringField(TEXT("code"), RefuseLaunchCode);
						}
					}
					else if (Name == TEXT("StowBoat"))
					{
						++StowCalls;
					}
					else
					{
						++OtherCalls;
					}
					Done(true, Response, FString());
				};
		}
	};

	/** 게임 인스턴스(백엔드 창구가 서는 곳) + 바다 + 캐릭터 + 위젯. 위젯은 백엔드 스냅샷으로 구명보트·컨테이너·탈것을 본다. */
	struct FBoatRig
	{
		UGameInstance* GameInstance = nullptr;
		UWorld* World = nullptr;
		USnareBackendSubsystem* Backend = nullptr;
		ASeaActor* Sea = nullptr;
		ASnarehavenCharacter* Player = nullptr;
		UWaterTravelComponent* Water = nullptr;
		UMountComponent* Mount = nullptr;
		UWaterActionWidget* Widget = nullptr;
		TMap<FString, UButton*> Buttons;

		bool Build()
		{
			GameInstance = NewObject<UGameInstance>(GEngine);
			GameInstance->InitializeStandalone();
			World = GameInstance->GetWorld();
			Backend = GameInstance->GetSubsystem<USnareBackendSubsystem>();
			if (World == nullptr || Backend == nullptr)
			{
				return false;
			}
			World->InitializeActorsForPlay(FURL());
			Sea = World->SpawnActor<ASeaActor>(FVector::ZeroVector, FRotator::ZeroRotator);
			if (Sea && !Sea->HasActorBegunPlay())
			{
				Sea->DispatchBeginPlay();
			}
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Player = World->SpawnActor<ASnarehavenCharacter>(TestLandPoint, FRotator::ZeroRotator, Params);
			if (Player && !Player->HasActorBegunPlay())
			{
				Player->DispatchBeginPlay();
			}
			Water = Player ? Player->FindComponentByClass<UWaterTravelComponent>() : nullptr;
			Mount = Player ? Player->FindComponentByClass<UMountComponent>() : nullptr;
			Widget = CreateWidget<UWaterActionWidget>(World, UWaterActionWidget::StaticClass());
			if (Sea == nullptr || Water == nullptr || Mount == nullptr || Widget == nullptr)
			{
				return false;
			}
			Widget->TakeWidget();
			Widget->BindToPawn(Water, Player->FindComponentByClass<UTapFishingComponent>());
			for (const TCHAR* Name : BoatAndMountButtons)
			{
				Buttons.Add(Name, ::Cast<UButton>(Widget->GetWidgetFromName(Name)));
			}
			for (const TCHAR* Name : FishingButtons)
			{
				Buttons.Add(Name, ::Cast<UButton>(Widget->GetWidgetFromName(Name)));
			}
			return !Buttons.FindKey(nullptr);
		}

		void Shutdown()
		{
			USnareBackendSubsystem::TestServerOverride = nullptr;
			if (GameInstance)
			{
				GameInstance->Shutdown();
			}
		}

		/** 백엔드가 보내 준 셈 치는 상태 — 위젯이 읽는 길은 이 스냅샷 하나다 (CLAUDE.md 8). */
		void SetOwned(const TArray<FName>& Items, const TArray<FName>& Mounts)
		{
			FSnarePlayerSnapshot Snapshot;
			Snapshot.PlayFabId = TEXT("PFTESTOWNER");
			Snapshot.FetchedAtUtc = FDateTime::UtcNow();
			for (const FName ItemID : Items)
			{
				FInventorySlot Slot;
				Slot.ItemID = ItemID;
				Slot.Count = 1;
				Snapshot.InventorySlots.Add(Slot);
			}
			Snapshot.Mounts = Mounts;
			Backend->ApplyPlayerSnapshot(Snapshot, false);
		}

		void Click(const TCHAR* Name)
		{
			Buttons.FindRef(Name)->OnClicked.Broadcast();
			Water->UpdateWaterState();
		}

		FString Label(const TCHAR* Name) const
		{
			return ButtonLabel(Buttons.FindRef(Name));
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenWaterActionBoatStatesTest, "Snarehaven.UI.WaterAction.Boat.ButtonsFollowState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSnarehavenWaterActionBoatStatesTest::RunTest(const FString& Parameters)
{
	using namespace SnarehavenWaterActionWidgetTest;
	FBoatServer Server;
	Server.Install();
	FBoatRig Rig;
	if (!TestTrue(TEXT("게임 인스턴스·바다·캐릭터·물 버튼 줄"), Rig.Build()))
	{
		Rig.Shutdown();
		return false;
	}
	// 보여야 할 것만 보이고, 보이는 버튼끼리 글자가 겹치지 않는다(같은 일 버튼 둘 0).
	auto ExpectOnly = [this, &Rig](const TCHAR* When, const TArray<FString>& Wanted)
	{
		Rig.Widget->RefreshView();
		TSet<FString> ShownLabels;
		for (const TPair<FString, UButton*>& Pair : Rig.Buttons)
		{
			const bool bShown = IsShown(Pair.Value);
			TestEqual(FString::Printf(TEXT("%s: %s"), When, *Pair.Key), bShown, Wanted.Contains(Pair.Key));
			if (bShown)
			{
				const FString Label = ButtonLabel(Pair.Value);
				TestFalse(FString::Printf(TEXT("%s: 「%s」 버튼 둘"), When, *Label), ShownLabels.Contains(Label));
				ShownLabels.Add(Label);
			}
		}
	};

	// 뭍 · 아무것도 없음 → 0.
	Rig.SetOwned({}, {});
	Rig.Water->UpdateWaterState();
	TestFalse(TEXT("섬 안쪽은 물 밖"), Rig.Water->IsInWater());
	ExpectOnly(TEXT("뭍·빈손"), {});
	// 뭍 · 구명보트만 → 물가가 아니니 띄우기 없음.
	Rig.SetOwned({ TestLifeboatId }, {});
	ExpectOnly(TEXT("뭍·구명보트"), {});

	// 물가(바다 위) · 구명보트 + 탈것 둘 → 띄우기 하나 (물 위 탈것 버튼 0).
	Rig.Player->SetActorLocation(FVector(TestSeaPoint.X, TestSeaPoint.Y, Rig.Sea->GetSurfaceZ()));
	Rig.Water->UpdateWaterState();
	Rig.SetOwned({ TestLifeboatId }, { TestWolf, TestFox });
	ExpectOnly(TEXT("물가·구명보트"), { TEXT("BoatLaunchButton") });
	TestEqual(TEXT("버튼 글자 「배 띄우기」"), Rig.Label(TEXT("BoatLaunchButton")), FString(TEXT("배 띄우기")));

	// 서버가 거절 → 배 없음, 까닭 글자, 버튼은 그대로.
	Server.RefuseLaunchCode = TEXT("not_enough_items");
	Rig.Click(TEXT("BoatLaunchButton"));
	TestEqual(TEXT("거절도 LaunchBoat 한 번"), Server.LaunchCalls, 1);
	TestNull(TEXT("거절이면 배가 안 뜬다"), Rig.Water->GetRiddenBoat());
	TestEqual(TEXT("M51-03 띄우기 거절 → 소리 0"), Rig.Water->GetBoatCueCount(), 0);
	ExpectOnly(TEXT("띄우기 거절 뒤"), { TEXT("BoatLaunchButton") });
	const FText Refused = Cast<UTextBlock>(Rig.Widget->GetWidgetFromName(TEXT("WaterMessage")))->GetText();
	TestFalse(TEXT("거절 까닭 글자가 보인다"), Refused.IsEmpty());
	AddInfo(FString::Printf(TEXT("띄우기 거절 글자 [%s]"), *Refused.ToString()));

	// 「배 띄우기」 → 서버 답 뒤에만 배가 뜨고 운전석.
	Server.RefuseLaunchCode.Reset();
	Rig.Click(TEXT("BoatLaunchButton"));
	TestEqual(TEXT("LaunchBoat 두 번째"), Server.LaunchCalls, 2);
	ABoatActor* const Boat = Rig.Water->GetRiddenBoat();
	if (!TestNotNull(TEXT("「배 띄우기」 → 배 위"), Boat))
	{
		Rig.Shutdown();
		return false;
	}
	TestTrue(TEXT("띄운 사람이 운전"), Rig.Water->IsDriving());
	TestEqual(TEXT("M51-03 띄우기 받아들임 → 소리 1"), Rig.Water->GetBoatCueCount(), 1);
	TestEqual(TEXT("M51-03 띄우기 소리 = BoatLaunch"), Rig.Water->GetLastBoatCue(), FName(TEXT("BoatLaunch")));
	ExpectOnly(TEXT("배 위"), { TEXT("BoatLeaveButton"), TEXT("BoatStowButton") });
	// 보트 컨테이너가 가방에 있으면 배 위에서만 「배 보관함」.
	Rig.SetOwned({ TestLifeboatId, USnareBaseSubsystem::GetBoatContainerItemID() }, { TestWolf, TestFox });
	ExpectOnly(TEXT("배 위·컨테이너"), { TEXT("BoatLeaveButton"), TEXT("BoatStowButton"), TEXT("BoatStorageButton") });

	// 배 보관함 판이 열린 동안 버튼 줄이 통째로 접힌다 — 줄이 판 오른쪽 끝을 가렸다 (INBOX 결함 10-07 02:00 ②).
	USnareBaseBuildComponent* const BuildComp = Rig.Player->FindComponentByClass<USnareBaseBuildComponent>();
	UCraftingScreenWidget* const BoatPanel = CreateWidget<UCraftingScreenWidget>(Rig.World, UCraftingScreenWidget::StaticClass());
	const UWidget* const ButtonColumn = Rig.Widget->GetWidgetFromName(TEXT("WaterButtons"));
	if (TestNotNull(TEXT("만들기 컴포넌트"), BuildComp) && TestNotNull(TEXT("제작 판"), BoatPanel)
		&& TestNotNull(TEXT("물 버튼 줄"), ButtonColumn))
	{
		BoatPanel->TakeWidget();
		BuildComp->SetCraftingScreenForTest(BoatPanel);
		BoatPanel->OpenBoatStorage();
		Rig.Widget->RefreshView();
		TestEqual(TEXT("판 열림: 물 버튼 줄 접힘"), ButtonColumn->GetVisibility(), ESlateVisibility::Collapsed);
		BoatPanel->Close();
		Rig.Widget->RefreshView();
		TestNotEqual(TEXT("판 닫힘: 물 버튼 줄 다시"), ButtonColumn->GetVisibility(), ESlateVisibility::Collapsed);
		BuildComp->SetCraftingScreenForTest(nullptr);
	}

	// 「내리기」 → 내 배 곁: 타기·배 넣기 (띄우기·보관함 0).
	Rig.Click(TEXT("BoatLeaveButton"));
	TestNull(TEXT("「내리기」 → 배에서 내림"), Rig.Water->GetRiddenBoat());
	TestEqual(TEXT("M51-04 내리기 → 소리 +1"), Rig.Water->GetBoatCueCount(), 2);
	TestEqual(TEXT("M51-04 내리기 소리 = BoatLeave"), Rig.Water->GetLastBoatCue(), FName(TEXT("BoatLeave")));
	ExpectOnly(TEXT("내 배 곁"), { TEXT("BoatBoardButton"), TEXT("BoatStowButton") });

	// 「타기」 → 같은 배에 다시.
	Rig.Click(TEXT("BoatBoardButton"));
	TestTrue(TEXT("「타기」 → 같은 배"), Rig.Water->GetRiddenBoat() == Boat);
	TestEqual(TEXT("M51-04 타기 → 소리 +1"), Rig.Water->GetBoatCueCount(), 3);
	TestEqual(TEXT("M51-04 타기 소리 = BoatBoard"), Rig.Water->GetLastBoatCue(), FName(TEXT("BoatBoard")));
	ExpectOnly(TEXT("다시 배 위"), { TEXT("BoatLeaveButton"), TEXT("BoatStowButton"), TEXT("BoatStorageButton") });

	// 「배 넣기」 → 서버 답 뒤 배가 사라지고, 다시 띄우기만.
	Rig.Click(TEXT("BoatStowButton"));
	TestEqual(TEXT("StowBoat 한 번"), Server.StowCalls, 1);
	TestEqual(TEXT("M51-03 넣기 받아들임 → 소리 +1"), Rig.Water->GetBoatCueCount(), 4);
	TestEqual(TEXT("M51-03 넣기 소리 = Storage"), Rig.Water->GetLastBoatCue(), FName(TEXT("Storage")));
	TestFalse(TEXT("넣은 배는 월드에서 사라진다"), IsValid(Boat) && !Boat->IsActorBeingDestroyed());
	TestNull(TEXT("넣은 뒤 배 위 아님"), Rig.Water->GetRiddenBoat());
	ExpectOnly(TEXT("배 넣은 뒤 물가"), { TEXT("BoatLaunchButton") });
	TestEqual(TEXT("다른 서버 요청 0"), Server.OtherCalls, 0);

	Rig.Shutdown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenWaterActionMountTest, "Snarehaven.UI.WaterAction.Boat.MountButtons",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSnarehavenWaterActionMountTest::RunTest(const FString& Parameters)
{
	using namespace SnarehavenWaterActionWidgetTest;
	FBoatServer Server;
	Server.Install();
	FBoatRig Rig;
	if (!TestTrue(TEXT("게임 인스턴스·바다·캐릭터·물 버튼 줄"), Rig.Build()))
	{
		Rig.Shutdown();
		return false;
	}
	auto Shown = [&Rig](const TCHAR* Name)
	{
		Rig.Widget->RefreshView();
		return IsShown(Rig.Buttons.FindRef(Name));
	};
	Rig.Water->UpdateWaterState();

	// 탈것 없음 → 타기 버튼 0 (못 타는 동안 늘 떠 있으면 화면 가운데를 막는다 — M15-02).
	Rig.SetOwned({}, {});
	TestFalse(TEXT("탈것 없음: 타기 0"), Shown(TEXT("MountToggleButton")));
	TestFalse(TEXT("탈것 없음: 바꾸기 0"), Shown(TEXT("MountPickButton")));

	// 한 종 → 타기만.
	Rig.SetOwned({}, { TestWolf });
	TestTrue(TEXT("한 종: 타기"), Shown(TEXT("MountToggleButton")));
	TestFalse(TEXT("한 종: 바꾸기 0"), Shown(TEXT("MountPickButton")));
	TestEqual(TEXT("버튼 글자 「탈것 타기」"), Rig.Label(TEXT("MountToggleButton")), FString(TEXT("탈것 타기")));

	// 두 종 → 타기 + 바꾸기(지금 탈 종 이름). 누를 때마다 다음 종.
	Rig.SetOwned({}, { TestWolf, TestFox });
	TestTrue(TEXT("두 종: 타기"), Shown(TEXT("MountToggleButton")));
	TestTrue(TEXT("두 종: 바꾸기"), Shown(TEXT("MountPickButton")));
	const FString FirstPick = Rig.Label(TEXT("MountPickButton"));
	TestTrue(TEXT("바꾸기 글자에 첫 종 이름"), FirstPick.Contains(Rig.Mount->GetSpeciesName(TestWolf).ToString()));
	Rig.Click(TEXT("MountPickButton"));
	Shown(TEXT("MountPickButton"));
	const FString SecondPick = Rig.Label(TEXT("MountPickButton"));
	TestTrue(TEXT("「탈것 바꾸기」 → 둘째 종"), Rig.Mount->GetRideSpecies(Rig.Backend->GetPlayerSnapshot()) == TestFox);
	TestTrue(TEXT("바꾸기 글자에 둘째 종 이름"), SecondPick.Contains(Rig.Mount->GetSpeciesName(TestFox).ToString()));
	AddInfo(FString::Printf(TEXT("바꾸기 글자 [%s] → [%s]"), *FirstPick, *SecondPick));

	// 「탈것 타기」 → 고른 종에 탐, 버튼은 「탈것 내리기」 하나 (바꾸기 0).
	Rig.Click(TEXT("MountToggleButton"));
	TestTrue(TEXT("「탈것 타기」 → 탐"), Rig.Mount->IsMounted());
	TestEqual(TEXT("탄 종 = 고른 종"), Rig.Mount->GetMountSpecies(), TestFox);
	TestTrue(TEXT("탄 채: 내리기"), Shown(TEXT("MountToggleButton")));
	TestFalse(TEXT("탄 채: 바꾸기 0"), Shown(TEXT("MountPickButton")));
	TestEqual(TEXT("버튼 글자 「탈것 내리기」"), Rig.Label(TEXT("MountToggleButton")), FString(TEXT("탈것 내리기")));

	// 「탈것 내리기」 → 내림, 다시 타기·바꾸기.
	Rig.Click(TEXT("MountToggleButton"));
	TestFalse(TEXT("「탈것 내리기」 → 내림"), Rig.Mount->IsMounted());
	TestTrue(TEXT("내린 뒤: 바꾸기"), Shown(TEXT("MountPickButton")));
	TestEqual(TEXT("탈것은 서버 요청 0"), Server.LaunchCalls + Server.StowCalls + Server.OtherCalls, 0);

	Rig.Shutdown();
	return true;
}

// ── ⑤ 버튼 자리 ──

namespace SnarehavenWaterActionLayoutTest
{
	// 1920×1080 뿌리에서 끝까지 배치한 칸의 화면 사각형 (TouchSize 시험과 같은 배치 길).
	void ArrangeAll(const TSharedRef<SWidget>& Widget, const FGeometry& Geometry, TMap<const SWidget*, FSlateRect>& OutRects)
	{
		OutRects.Add(&Widget.Get(), Geometry.GetLayoutBoundingRect());
		FArrangedChildren Arranged(EVisibility::All);
		Widget->ArrangeChildren(Geometry, Arranged);
		for (int32 Index = 0; Index < Arranged.Num(); ++Index)
		{
			ArrangeAll(Arranged[Index].Widget, Arranged[Index].Geometry, OutRects);
		}
	}

	bool IsShownInTree(const UWidget& Widget)
	{
		for (const UWidget* Walk = &Widget; Walk != nullptr; Walk = Walk->GetParent())
		{
			const ESlateVisibility Vis = Walk->GetVisibility();
			if (Vis == ESlateVisibility::Collapsed || Vis == ESlateVisibility::Hidden)
			{
				return false;
			}
		}
		return true;
	}

	struct FPlacedButton
	{
		FString Name;
		FSlateRect Rect;
	};

	// 판의 보이는 버튼마다 "판/버튼" 이름과 1920×1080 사각형.
	TArray<FPlacedButton> PlaceButtons(UUserWidget& Panel, const TCHAR* PanelName)
	{
		TArray<FPlacedButton> Out;
		const TSharedRef<SWidget> Root = Panel.TakeWidget();
		Root->SlatePrepass(1.f);
		TMap<const SWidget*, FSlateRect> Rects;
		ArrangeAll(Root, FGeometry::MakeRoot(FVector2D(1920.f, 1080.f), FSlateLayoutTransform()), Rects);
		TArray<UWidget*> All;
		Panel.WidgetTree->GetAllWidgets(All);
		for (UWidget* const Widget : All)
		{
			if (Cast<UButton>(Widget) == nullptr || !IsShownInTree(*Widget))
			{
				continue;
			}
			const TSharedPtr<SWidget> Slate = Widget->GetCachedWidget();
			if (const FSlateRect* const Rect = Slate.IsValid() ? Rects.Find(Slate.Get()) : nullptr)
			{
				Out.Add({ FString::Printf(TEXT("%s/%s"), PanelName, *Widget->GetName()), *Rect });
			}
		}
		return Out;
	}

	template <typename TWidget>
	TWidget* Build(UWorld* World)
	{
		TWidget* const Panel = CreateWidget<TWidget>(World, TWidget::StaticClass());
		if (Panel != nullptr)
		{
			Panel->TakeWidget();
		}
		return Panel;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenWaterActionLayoutTest, "Snarehaven.UI.WaterAction.Layout.ClearOfHudButtons",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 물 버튼 줄이 한 번에 띄우는 칸 묶음마다, 버튼 전 영역이 오른쪽 아이콘 기둥·사냥/목장 HUD 버튼과 겹치지 않는다. */
bool FSnarehavenWaterActionLayoutTest::RunTest(const FString& Parameters)
{
	using namespace SnarehavenWaterActionLayoutTest;
	UWorld* const World = SnarehavenWaterActionWidgetTest::CreateTestWorld();

	// 닫힌 상태의 HUD 쪽 버튼 — 배낭·지도·메뉴·이모트·출석 기둥과 사냥·목장 HUD.
	TArray<FPlacedButton> Hud;
	if (UWorldMapScreenWidget* const Map = Build<UWorldMapScreenWidget>(World)) { Hud.Append(PlaceButtons(*Map, TEXT("지도"))); }
	if (UBackpackScreenWidget* const Backpack = Build<UBackpackScreenWidget>(World)) { Hud.Append(PlaceButtons(*Backpack, TEXT("배낭"))); }
	if (UAttendanceScreenWidget* const Attendance = Build<UAttendanceScreenWidget>(World)) { Hud.Append(PlaceButtons(*Attendance, TEXT("출석"))); }
	if (UHuntHudWidget* const HuntHud = Build<UHuntHudWidget>(World)) { Hud.Append(PlaceButtons(*HuntHud, TEXT("사냥 HUD"))); }
	if (URanchHudWidget* const RanchHud = Build<URanchHudWidget>(World)) { Hud.Append(PlaceButtons(*RanchHud, TEXT("목장 HUD"))); }
	const int32 ColumnIcons = Hud.FilterByPredicate([](const FPlacedButton& B)
	{
		return B.Name.Contains(TEXT("EmoteOpenButton")) || B.Name.Contains(TEXT("WorldMapOpenButton")) || B.Name.Contains(TEXT("PauseMenuOpenButton"));
	}).Num();
	TestEqual(TEXT("아이콘 기둥 지도·메뉴·이모트 버튼이 배치됐다"), ColumnIcons, 3);

	// 실제로 함께 뜨는 묶음 (최대 4칸 — 배 위: 낚시·내리기·배 넣기·배 보관함).
	const TArray<TArray<const TCHAR*>> Groups = {
		{ TEXT("FishCastButton"), TEXT("BoatLeaveButton"), TEXT("BoatStowButton"), TEXT("BoatStorageButton") },
		{ TEXT("FishCastButton"), TEXT("BoatLaunchButton"), TEXT("MountToggleButton"), TEXT("MountPickButton") },
		{ TEXT("BoatBoardButton"), TEXT("FishTapButton"), TEXT("FishQuitButton") },
	};
	int32 Checked = 0;
	int32 Overlaps = 0;
	float NearestGap = TNumericLimits<float>::Max();
	float MinHeight = TNumericLimits<float>::Max();
	for (const TArray<const TCHAR*>& Group : Groups)
	{
		UWaterActionWidget* const Water = Build<UWaterActionWidget>(World);
		if (!TestNotNull(TEXT("물 버튼 줄을 만든다"), Water))
		{
			break;
		}
		for (const TCHAR* const Name : Group)
		{
			if (UWidget* const Button = Water->GetWidgetFromName(Name))
			{
				Button->SetVisibility(ESlateVisibility::Visible);
			}
		}
		const TArray<FPlacedButton> Placed = PlaceButtons(*Water, TEXT("물"));
		TestEqual(*FString::Printf(TEXT("묶음 %d칸이 다 배치됐다"), Group.Num()), Placed.Num(), Group.Num());
		for (const FPlacedButton& Mine : Placed)
		{
			++Checked;
			MinHeight = FMath::Min(MinHeight, Mine.Rect.GetSize().Y);
			TestTrue(*FString::Printf(TEXT("%s 높이 %.0f ≥ 72"), *Mine.Name, Mine.Rect.GetSize().Y),
				Mine.Rect.GetSize().Y >= SnareUiStyle::HudTouchMin);
			TestTrue(*FString::Printf(TEXT("%s 가 화면 안"), *Mine.Name),
				Mine.Rect.Left >= 0.f && Mine.Rect.Top >= 0.f && Mine.Rect.Right <= 1920.f && Mine.Rect.Bottom <= 1080.f);
			for (const FPlacedButton& Other : Hud)
			{
				if (FSlateRect::DoRectanglesIntersect(Mine.Rect, Other.Rect))
				{
					++Overlaps;
					AddError(FString::Printf(TEXT("[WaterLayout] 겹침: %s (%.0f,%.0f~%.0f,%.0f) ↔ %s (%.0f,%.0f~%.0f,%.0f)"),
						*Mine.Name, Mine.Rect.Left, Mine.Rect.Top, Mine.Rect.Right, Mine.Rect.Bottom,
						*Other.Name, Other.Rect.Left, Other.Rect.Top, Other.Rect.Right, Other.Rect.Bottom));
				}
				else if (Other.Name.Contains(TEXT("OpenButton")) && Mine.Rect.Bottom > Other.Rect.Top && Mine.Rect.Top < Other.Rect.Bottom)
				{
					NearestGap = FMath::Min(NearestGap, Other.Rect.Left - Mine.Rect.Right);
				}
			}
		}
	}
	AddInfo(FString::Printf(TEXT("[WaterLayout] 물 버튼 %d · HUD 버튼 %d · 겹침 %d · 같은 높이 아이콘까지 가로 틈 %.0f · 가장 낮은 버튼 %.0f"),
		Checked, Hud.Num(), Overlaps, NearestGap == TNumericLimits<float>::Max() ? -1.f : NearestGap,
		MinHeight == TNumericLimits<float>::Max() ? -1.f : MinHeight));
	TestTrue(TEXT("잰 물 버튼이 있다"), Checked > 0);

	// ④ 배 보관함 판엔 쪽 단추 줄이 없다(제목 탭과 같은 말 둘) — 배낭 제작 판은 그대로 있다.
	UCraftingScreenWidget* const Crafting = Build<UCraftingScreenWidget>(World);
	if (TestNotNull(TEXT("제작 판을 만든다"), Crafting))
	{
		const UWidget* const TabsBox = Crafting->GetWidgetFromName(TEXT("CraftTabsBox"));
		if (TestNotNull(TEXT("쪽 단추 줄 상자가 있다"), TabsBox))
		{
			Crafting->OpenBoatStorage();
			TestEqual(TEXT("배 보관함: 쪽 단추 줄 접힘"), TabsBox->GetVisibility(), ESlateVisibility::Collapsed);
			Crafting->OpenBackpack();
			TestEqual(TEXT("배낭 제작: 쪽 단추 줄 보임"), TabsBox->GetVisibility(), ESlateVisibility::Visible);
		}
	}

	SnarehavenWaterActionWidgetTest::DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenGunHudLayoutTest, "Snarehaven.UI.GunHud.ClearOfHudButtons",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 권총 탄 글(`7/25` 카드)·재장전 버튼이 오른쪽 아이콘 기둥·사냥 HUD 버튼(존 이동 포함)과 겹치지 않는다 (M39-06b). */
bool FSnarehavenGunHudLayoutTest::RunTest(const FString& Parameters)
{
	using namespace SnarehavenWaterActionLayoutTest;
	UWorld* const World = SnarehavenWaterActionWidgetTest::CreateTestWorld();

	TArray<FPlacedButton> Others;
	if (UWorldMapScreenWidget* const Map = Build<UWorldMapScreenWidget>(World)) { Others.Append(PlaceButtons(*Map, TEXT("지도"))); }
	if (UBackpackScreenWidget* const Backpack = Build<UBackpackScreenWidget>(World)) { Others.Append(PlaceButtons(*Backpack, TEXT("배낭"))); }
	if (UAttendanceScreenWidget* const Attendance = Build<UAttendanceScreenWidget>(World)) { Others.Append(PlaceButtons(*Attendance, TEXT("출석"))); }

	UHuntHudWidget* const Hunt = Build<UHuntHudWidget>(World);
	if (!TestNotNull(TEXT("사냥 HUD 를 만든다"), Hunt))
	{
		SnarehavenWaterActionWidgetTest::DestroyTestWorld(World);
		return false;
	}
	// 총을 든 화면 — 탄 HUD 둘을 켜고 글은 사람 예시 `7/25`. 포탈 곁이면 존 이동 버튼도 같이 뜬다.
	for (const TCHAR* const Name : { TEXT("GunReloadButton"), TEXT("GunAmmoCard"), TEXT("ZoneTravelButton") })
	{
		if (UWidget* const Widget = Hunt->GetWidgetFromName(Name))
		{
			Widget->SetVisibility(ESlateVisibility::Visible);
		}
	}
	if (UTextBlock* const AmmoText = Cast<UTextBlock>(Hunt->GetWidgetFromName(TEXT("GunAmmoText"))))
	{
		AmmoText->SetText(FText::FromString(TEXT("7/25")));
	}
	for (const FPlacedButton& Placed : PlaceButtons(*Hunt, TEXT("사냥 HUD")))
	{
		if (!Placed.Name.EndsWith(TEXT("GunReloadButton")))
		{
			Others.Add(Placed);
		}
	}

	// 탄 HUD 두 칸의 1920×1080 사각형 (카드는 버튼이 아니라 PlaceButtons 가 안 줍는다).
	const TSharedRef<SWidget> Root = Hunt->TakeWidget();
	Root->SlatePrepass(1.f);
	TMap<const SWidget*, FSlateRect> Rects;
	ArrangeAll(Root, FGeometry::MakeRoot(FVector2D(1920.f, 1080.f), FSlateLayoutTransform()), Rects);
	int32 Overlaps = 0;
	for (const TCHAR* const Name : { TEXT("GunReloadButton"), TEXT("GunAmmoCard") })
	{
		const UWidget* const Widget = Hunt->GetWidgetFromName(Name);
		const TSharedPtr<SWidget> Slate = Widget != nullptr ? Widget->GetCachedWidget() : nullptr;
		const FSlateRect* const Mine = Slate.IsValid() ? Rects.Find(Slate.Get()) : nullptr;
		if (!TestNotNull(*FString::Printf(TEXT("%s 가 배치됐다"), Name), Mine))
		{
			continue;
		}
		TestTrue(*FString::Printf(TEXT("%s 가 화면 안"), Name),
			Mine->Left >= 0.f && Mine->Top >= 0.f && Mine->Right <= 1920.f && Mine->Bottom <= 1080.f);
		for (const FPlacedButton& Other : Others)
		{
			if (FSlateRect::DoRectanglesIntersect(*Mine, Other.Rect))
			{
				++Overlaps;
				AddError(FString::Printf(TEXT("[GunHudLayout] 겹침: %s (%.0f,%.0f~%.0f,%.0f) ↔ %s (%.0f,%.0f~%.0f,%.0f)"),
					Name, Mine->Left, Mine->Top, Mine->Right, Mine->Bottom,
					*Other.Name, Other.Rect.Left, Other.Rect.Top, Other.Rect.Right, Other.Rect.Bottom));
			}
		}
		AddInfo(FString::Printf(TEXT("[GunHudLayout] %s (%.0f,%.0f~%.0f,%.0f)"), Name, Mine->Left, Mine->Top, Mine->Right, Mine->Bottom));
	}
	AddInfo(FString::Printf(TEXT("[GunHudLayout] 견준 버튼 %d · 겹침 %d"), Others.Num(), Overlaps));
	TestTrue(TEXT("견줄 아이콘 기둥·HUD 버튼이 있다"), Others.Num() >= 6);

	SnarehavenWaterActionWidgetTest::DestroyTestWorld(World);
	return true;
}

#endif
