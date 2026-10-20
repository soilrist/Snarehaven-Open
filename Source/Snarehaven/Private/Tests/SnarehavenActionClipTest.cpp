// M15-12·M15-13 2층 검사 — STATUS #0271 「행동 11개 표」의 **② 몸 클립 칸**과 **④ 서버 칸**
// (INBOX ★★★★ 2026-09-25 ①). 표 11줄(공격·나무 베기·광맥·맨손 채집·낚시·제작·제련소·요리·먹기·설치·포획)을
// 아래 두 표(`ActionTable`·`ServerTable`)에 그대로 옮겨 두고, 행마다 **사람이 누르는 진입점을 실제로 태워**
//   ② 고른 아이템 동작(`EItemAction`)과 몸에 걸린 클립 이름,
//   ④ 가짜 서버에 실제로 나간 백엔드 함수 이름(순서대로)
// 을 읽어 견준다. 클립 이름만 보면 광맥(`Mine` → 공격 클립)과 맨주먹 공격이 구별되지 않아 **동작 종류까지**,
// 서버도 "요청했다" 는 말이 아니라 **나간 함수 이름과 인자**를 본다 (PROMPT ③-12 — 이름 패턴이 아니라 실제 결과로).
// 클립 원본·에셋은 건드리지 않는다 (DESIGN 8.8) — 표에 "클립 없음" 인 행은 동작이 **안 나는 것**이 합격이다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.ActionMatrix; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Animals/AnimalCharacter.h"
#include "Animals/AnimalStateComponent.h"
#include "Backend/SnareBackendSubsystem.h"
#include "Building/SnareBaseBuildComponent.h"
#include "Building/SnareBaseSubsystem.h"
#include "Building/SnarePlacedStructureActor.h"
#include "Combat/CaptureComponent.h"
#include "Combat/MeleeAttackComponent.h"
#include "Data/AnimalRow.h"
#include "Data/GatherableRow.h"
#include "Data/ItemRow.h"
#include "Data/RecipeRow.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Equipment/EquipmentComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "Gathering/GatherComponent.h"
#include "Gathering/GatherableActor.h"
#include "Gathering/TapFishingComponent.h"
#include "Data/BaseRuleRow.h"
#include "Inventory/BackpackComponent.h"
#include "Inventory/InventoryComponent.h"
#include "Inventory/LootClaimComponent.h"
#include "SnarehavenCharacter.h"
#include "SnarehavenGameMode.h"
#include "UI/PlayerNameTagWidget.h"
#include "World/FishingSpotActor.h"

namespace SnareActionClipTest
{
	// 표 ② 칸에 적힌 클립 이름 (STATUS #0271). 에셋 이름 그대로라 클립을 갈아 끼우면 이 표도 같이 고친다 —
	// 그러라고 한곳에 모았다. "클립 없음" 은 아이템 동작이 아예 안 나는 행이다(화면이 몸을 덮거나 타이머다).
	const TCHAR* const NoClip = TEXT("클립 없음");
	const TCHAR* const NoAction = TEXT("-");
	/** 아무 행동도 안 틀었을 때 도는 클립. "클립 없음" 행은 이게 그대로 돌아야 한다. */
	const TCHAR* const IdleClip = TEXT("A_Player_Idle");

	// 표 한 줄. `Action` 은 고른 `EItemAction` 이름("Chop"), 동작이 안 나는 행은 `NoAction`.
	struct FActionExpect
	{
		const TCHAR* Row;      // ① 행동
		const TCHAR* Button;   // ① 버튼 — 무엇을 눌렀는지 (검사가 태우는 진입점)
		const TCHAR* Action;   // ② 고른 아이템 동작
		const TCHAR* Clip;     // ② 몸 클립
	};

	// **STATUS #0271 표 11줄 = 기대값.** 낚시는 한 줄에 클립 셋이라 단계별로 폈다(표 ② 칸 `FishCast→FishIdle→FishReel`).
	const FActionExpect ActionTable[] = {
		// M18-11: 맨손 공격 = 잽, 손도끼 = 내려찍기, 기본 곡괭이 = 한 손 짧은 휘두르기 (든 행의 `UseAnim`, 루프#384 레퍼런스 관찰표).
		{ TEXT("공격"),        TEXT("HUD 공격"),                 NoAction,        TEXT("A_Player_Jab") },
		{ TEXT("나무 베기"),   TEXT("HUD 공격(나무 가까이)"),     TEXT("Smash"),   TEXT("A_Player_SwingDownward") },  // 손도끼 = 내려치기(M16-04)
		{ TEXT("광맥"),        TEXT("HUD 공격(광맥 가까이)"),     TEXT("Mine"),    TEXT("A_Player_MineOneHand") },
		{ TEXT("맨손 채집"),   TEXT("HUD 공격(덤불)"),            TEXT("Gather"),  TEXT("A_Player_Gather") },
		{ TEXT("낚시-던지기"), TEXT("HUD 낚시(포인트 앞)"),       NoAction,        TEXT("A_Player_FishCast") },
		{ TEXT("낚시-대기"),   TEXT("(던진 뒤 기다림)"),          NoAction,        TEXT("A_Player_FishIdle") },
		{ TEXT("낚시-감기"),   TEXT("연타"),                      NoAction,        TEXT("A_Player_FishReel") },
		// M28-02: 제작·제련·요리는 서버 확정 뒤 판을 닫지 않는 줍기 몸짓 (놓기와 같은 클립).
		{ TEXT("제작"),        TEXT("제작 화면 「만들기」"),       TEXT("Gather"),  TEXT("A_Player_Gather") },
		{ TEXT("제련소"),      TEXT("제련소 화면"),               TEXT("Gather"),  TEXT("A_Player_Gather") },
		{ TEXT("요리"),        TEXT("모닥불·그릴 화면"),          TEXT("Gather"),  TEXT("A_Player_Gather") },
		// INBOX 15:30 ⑤: 원본은 먹는 동작이 없다 — 선 채 머리 위 「+N」 만 (DESIGN 8.8 「먹기」).
		{ TEXT("먹기"),        TEXT("가방 카드 「먹기」"),         NoAction,        NoClip },
		{ TEXT("설치"),        TEXT("가방 카드 「놓기」"),         TEXT("Gather"),  TEXT("A_Player_Gather") },
		// DESIGN 8.8 「포획 던지기」: 누르는 순간(판정 전) 올가미를 던져 당기는 한 팔 — 낚시 던지기 클립 재사용.
		{ TEXT("포획"),        TEXT("HUD 포획"),                  TEXT("Lasso"),   TEXT("A_Player_FishCast") },
	};

	// ── M15-13: 같은 표의 **④ 서버 칸** ──
	// 한 행이 부르는 백엔드 함수를 **순서대로** 적는다. 채집·포획은 누른 자리에서 서버를 안 부르고
	// 지급 대기열에 쌓였다가 `ClaimLoot` 한 번으로 나간다 (DESIGN 8.12) — 그래서 "누를 땐 0건" 도 같이 본다.
	struct FServerExpect
	{
		const TCHAR* Row;        // ① 행동
		const TCHAR* TableCell;  // ④ 칸 원문 (STATUS #0271)
		const TCHAR* Functions[2];  // 실제로 나가야 할 함수, 순서대로 (빈 칸은 nullptr)
	};

	const FServerExpect ServerTable[] = {
		{ TEXT("공격"),        TEXT("호스트 판정"),                  { nullptr, nullptr } },
		{ TEXT("나무 베기"),   TEXT("ClaimLoot"),                    { TEXT("ClaimLoot"), nullptr } },
		{ TEXT("광맥"),        TEXT("ClaimLoot"),                    { TEXT("ClaimLoot"), nullptr } },
		{ TEXT("맨손 채집"),   TEXT("ClaimLoot"),                    { TEXT("ClaimLoot"), nullptr } },
		{ TEXT("낚시"),        TEXT("CatchFish"),                    { TEXT("RollFish"), TEXT("CatchFish") } },
		{ TEXT("제작"),        TEXT("CraftItem"),                    { TEXT("CraftItem"), nullptr } },
		{ TEXT("제련소"),      TEXT("CraftItem"),                    { TEXT("CraftItem"), nullptr } },
		{ TEXT("요리"),        TEXT("타이머 끝 CraftItem"),          { TEXT("CraftItem"), nullptr } },
		{ TEXT("먹기"),        TEXT("ConsumeItem"),                  { TEXT("ConsumeItem"), nullptr } },
		{ TEXT("설치-텐트"),   TEXT("설치 요청 → 응답 뒤 구조물"),   { TEXT("PlaceTent"), nullptr } },
		{ TEXT("설치-가구"),   TEXT("설치 요청 → 응답 뒤 구조물"),   { TEXT("PlaceStructure"), nullptr } },
		{ TEXT("포획"),        TEXT("Server_TryCaptureTarget → 펫 지급"), { TEXT("ClaimLoot"), nullptr } },
	};

	// 서버 칸 검사가 쓰는 테스트 전용 값. 채집·포획이 주는 것은 **`ClaimLoot` 허용 목록 안**이라야 한다 —
	// 목록 밖이면 대기열에 남기만 하고 요청이 안 나간다(`ULootClaimComponent::IsClaimableLoot`).
	const FName TestClaimableGatherID = TEXT("item.wood");
	const FName TestClaimablePetID = TEXT("item.pet_test_action");

	// 테스트 전용 값 (Verification.md 6 — 설계 숫자가 아니다).
	const FName TestGatherableID = TEXT("gatherable.test_action");
	const FName TestBerryID = TEXT("item.test_berry");
	const FName TestPetItemID = TEXT("item.test_pet");
	// 낚시 자리·어종은 **표에 있는 진짜 값**이라야 한다 — 서버 `FISHING_SPOTS` 에 없는 자리면 그 자리 서식지를
	// 모른다고 낚시가 그 자리에서 끝난다(`[Fishing] 끝: 서버가 표를 안 줬다`).
	const FName TestSpotId = TEXT("pond_1");
	const FName TestFishID = TEXT("item.carp");
	const TCHAR* const TestTentId = TEXT("tent-action-matrix");
	const TCHAR* const TestStructureId = TEXT("structure-action-matrix");
	const TCHAR* const TestPlacedAtUtc = TEXT("2026-09-26T00:00:00Z");
	constexpr int32 TestHeal = 15;   // 먹기 응답 회복량 — 테스트 전용 값
	constexpr int32 TestPoisonHeal = -20;   // 독 먹기 응답 회복량 — 테스트 전용 값
	constexpr float TestAttackDamage = 10.f;
	constexpr float TestAttackRangeCm = 300.f;
	constexpr float TestAttackIntervalSeconds = 1.f;
	constexpr float TestNearDistanceCm = 100.f;
	constexpr float TestAnimalMaxHP = 20.f;
	constexpr float TestCaptureThresholdPct = 50.f;
	constexpr float TestWalkSpeedCmPerSecond = 200.f;  // 걷기 문턱(30) 위 — 아이템 동작을 끊는 값
	constexpr float TestTimerStepSeconds = 0.1f;
	constexpr float TestBiteWaitCapSeconds = 10000.f;   // 던진 뒤 입질 대기(난수)를 한 번에 넘기는 시간 — M16-06 소리 순서 시험
	constexpr float TestBiteSeconds = 2.f;   // 대기 클립을 볼 만큼 길게 (연타는 아래에서 시간을 흘려 앞당긴다)
	constexpr float TestFrameSeconds = 1.f / 30.f;
	constexpr int32 TestMaxTaps = 200;         // 연타 목표를 표에서 읽으므로 상한만 둔다 (안 끝나면 검사 실패)
	constexpr int32 TestMaxPlaceSteps = 12;    // 텐트 땅 안 빈 칸을 찾아 걸어 볼 최대 걸음
	constexpr float TestFarDistanceCm = 600.f;     // 사거리(300) 밖 — 호스트가 "너무 멀다" 로 물리는 자리
	constexpr float TestRespawnSeconds = 300.f;    // 다시 자라는 채집물(나무) — 먼저 누른 사람이 가져간 자리를 만들려고

	// 표에 있는 진짜 도구 (장비 행을 찾을 수 있어야 도구 칸이 채워진다).
	const FName HatchetID = TEXT("item.hatchet");
	const FName PickaxeID = TEXT("item.pickaxe_basic");
	const FName RodID = TEXT("item.rod_basic");
	const FName CampfireID = TEXT("item.campfire");
	const FName TentID = TEXT("item.tent");
	const FName BerryID = TEXT("item.berry");

	const TCHAR* const RecipeTablePath = TEXT("/Game/Game/Data/Crafting/DT_RecipeTable.DT_RecipeTable");

	/** 나간 요청 한 건 — 함수 이름과 인자를 그대로 남긴다 (M15-13 이 이름·인자를 견준다). */
	struct FServerCall
	{
		FString Function;
		TSharedPtr<FJsonObject> Args;
	};

	/** 어떤 함수를 부르든 바로 받아 주는 가짜 서버. 나간 것은 순서대로 `Log` 에 쌓인다. */
	struct FFakeServer
	{
		/** 쥐고 있는 답 한 건 (`bHold` 일 때) — 검사가 `Flush` 를 부를 때까지 클라이언트는 응답 전이다. */
		struct FHeldReply
		{
			USnareBackendSubsystem::FServerCallDone Done;
			TSharedPtr<FJsonObject> Response;
		};

		int32 Calls = 0;
		FString LastFunction;
		TArray<FServerCall> Log;
		/** `PlaceTent` 응답에 실어 줄 존 — 사본(`FSnareTentMirror`)이 내 땅으로 서려면 진짜 존 이름이라야 한다. */
		FString TentZone;
		/** 켜면 답을 쥐고 있는다 — "응답 전에는 화면에 아무것도 안 선다"(CLAUDE.md 8)를 보려는 검사만 켠다. */
		bool bHold = false;
		TArray<FHeldReply> Held;
		/** 이 이름의 함수는 거절 답(`error`)을 준다 — "실패면 소리·몸짓 0" 을 보려는 검사만 채운다. */
		FString RejectFunction;
		/** `ConsumeItem` 응답 회복량 — 독(음수) 소리를 보려는 검사만 바꾼다. */
		int32 Heal = TestHeal;

		void Install()
		{
			USnareBackendSubsystem::TestServerOverride =
				[this](const FString& Name, const TSharedPtr<FJsonObject>& Args, USnareBackendSubsystem::FServerCallDone Done)
				{
					++Calls;
					LastFunction = Name;
					Log.Add(FServerCall{ Name, Args });
					const TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
					// 텐트는 사본이 서야 그 땅 안에 가구를 놓을 수 있다 (`PlaceStructure` 행의 앞자리).
					if (Name == TEXT("PlaceTent"))
					{
						Response->SetStringField(TEXT("tentId"), TestTentId);
						Response->SetStringField(TEXT("placedAtUtc"), TestPlacedAtUtc);
						Response->SetStringField(TEXT("zone"), TentZone);
					}
					else if (Name == TEXT("PlaceStructure"))
					{
						Response->SetStringField(TEXT("tentId"), TestTentId);
						Response->SetStringField(TEXT("structureId"), TestStructureId);
					}
					// 집 올리기는 어느 텐트였는지만 돌려준다 — 사본이 그 텐트를 찾아야 집이 된다 (M28-03).
					else if (Name == TEXT("UpgradeHouse"))
					{
						Response->SetStringField(TEXT("tentId"), TestTentId);
					}
					// 낚시만 응답 속을 본다 — 어종이 안 오면 연타 단계로 못 가 감기 클립이 안 나온다.
					else if (Name == TEXT("RollFish"))
					{
						Response->SetStringField(TEXT("token"), TEXT("test-token"));
						Response->SetStringField(TEXT("fishItemId"), TestFishID.ToString());
					}
					// 먹기는 서버가 정한 회복량을 돌려준다 — 머리 위 「+N」 이 이 값 그대로여야 한다.
					else if (Name == TEXT("ConsumeItem"))
					{
						Response->SetNumberField(TEXT("heal"), Heal);
					}
					else if (Name == TEXT("CatchFish"))
					{
						Response->SetStringField(TEXT("caught"), TestFishID.ToString());
					}
					if (Name == RejectFunction)
					{
						Response->SetStringField(TEXT("error"), TEXT("rejected"));
						Response->SetStringField(TEXT("code"), TEXT("not_enough_items"));
					}
					if (bHold)
					{
						Held.Add(FHeldReply{ MoveTemp(Done), Response });
						return;
					}
					Done(true, Response, FString());
				};
		}

		/** 쥐고 있던 답을 온 순서대로 돌려준다 (`bHold` 검사용). */
		void Flush()
		{
			TArray<FHeldReply> Ready = MoveTemp(Held);
			Held.Reset();
			for (FHeldReply& Reply : Ready)
			{
				if (Reply.Done)
				{
					Reply.Done(true, Reply.Response, FString());
				}
			}
		}

		~FFakeServer() { USnareBackendSubsystem::TestServerOverride = nullptr; }
	};

	/** 표에서 그 자리(작업대·제련소·불)의 레시피 결과를 하나 집어 온다 — 아이템 ID 를 검사에 적지 않으려고. */
	FName FindRecipeResult(const UDataTable* RecipeTable, ERecipeStation Station)
	{
		if (RecipeTable == nullptr)
		{
			return NAME_None;
		}
		for (const TPair<FName, uint8*>& Pair : RecipeTable->GetRowMap())
		{
			const FRecipeRow* const Row = reinterpret_cast<const FRecipeRow*>(Pair.Value);
			if (Row != nullptr && Row->Station == Station && !Row->ResultItemID.IsNone())
			{
				return Row->ResultItemID;
			}
		}
		return NAME_None;
	}

	/** 타이머 한 발. `FTimerManager::Tick` 은 한 프레임에 한 번만 일해서 `GFrameCounter` 를 같이 올린다. */
	void TickTimers(UWorld* World, UGameInstance* GameInstance, float Seconds)
	{
		const int32 Steps = FMath::Max(1, FMath::RoundToInt(Seconds / TestTimerStepSeconds));
		for (int32 Step = 0; Step < Steps; ++Step)
		{
			++GFrameCounter;
			World->GetTimerManager().Tick(TestTimerStepSeconds);
			if (GameInstance != nullptr)
			{
				GameInstance->GetTimerManager().Tick(TestTimerStepSeconds);
			}
		}
	}

	/** 한 행이 끝난 자리를 치운다 — 걷기 한 프레임이면 아이템 동작이 끊긴다(표 ⑤ 칸 "걷기로 끊김"). */
	void ResetBody(UWorld* World, UGameInstance* GameInstance, ASnarehavenCharacter& Player)
	{
		Player.EndAttack();
		// 도는 휘두르기·공격 간격은 타이머가 끝낸다 — `StopAttacking` 은 누름 상태만 끈다(짧게 눌러도 한 바퀴는 돌라고 일부러 그렇다).
		TickTimers(World, GameInstance, TestAttackIntervalSeconds * 3.f);
		Player.GetCharacterMovement()->Velocity = FVector(TestWalkSpeedCmPerSecond, 0.f, 0.f);
		Player.UpdateBodyAnimation();
		Player.GetCharacterMovement()->Velocity = FVector::ZeroVector;
	}

	/** 도구 칸을 채운다. 도구 행은 메시가 아직 없어(8.11) `EquipItem` 이 못 걸어 호스트 목록 길로 넣는다. */
	bool HoldTool(ASnarehavenCharacter& Player, FName ToolItemID)
	{
		TArray<FEquippedSlot> Slots;
		if (!ToolItemID.IsNone())
		{
			FEquippedSlot& Slot = Slots.AddDefaulted_GetRef();
			Slot.Slot = EEquipSlot::Tool;
			Slot.ItemRowName = Player.GetEquipment()->FindEquipmentRowByItemID(ToolItemID);
			if (Slot.ItemRowName.IsNone())
			{
				return false;
			}
		}
		Player.GetEquipment()->ApplyEquippedSlots(Slots);
		return true;
	}

	/**
	 * 도구가 있어야 캐는 채집물 하나. 주는 것은 기본이 테스트 전용 아이템이라 실제 표를 안 읽는다.
	 * @param GrantItemID 무엇을 주는가 — 서버 칸 검사는 `ClaimLoot` 허용 목록 안 아이템이라야 대기열이 나간다
	 * @param Where 어디에 세우는가 — 플레이어가 움직인 뒤(설치 행)에도 사거리 안이라야 한다
	 */
	AGatherableActor* SpawnGatherable(UWorld* World, FName RequiredTool, FName GrantItemID = TestBerryID,
		const FVector& Where = FVector(TestNearDistanceCm, 0.f, 0.f), float RespawnSeconds = 0.f)
	{
		AGatherableActor* const Target = World->SpawnActor<AGatherableActor>(Where, FRotator::ZeroRotator);
		if (Target == nullptr)
		{
			return nullptr;
		}
		FGatherableRow Row;
		Row.GatherableID = TestGatherableID;
		Row.GrantItemID = GrantItemID;
		Row.GrantCount = 1;
		Row.RequiredToolItemID = RequiredTool;
		// 0 이면 캐는 순간 액터가 사라진다 — "먼저 누른 사람이 가져갔다" 자리를 만들려면 남아 있어야 한다.
		Row.RespawnSeconds = RespawnSeconds;
		return Target->InitializeFromRow(Row) ? Target : nullptr;
	}

	/** 지금 몸에 걸린 것을 표와 같은 말로 옮긴다. */
	FString ReadClip(const ASnarehavenCharacter& Player, bool bActionStarted)
	{
		const UAnimSequence* const Clip = Player.GetCurrentBodyAnim();
		if (!bActionStarted && Clip == nullptr)
		{
			return NoClip;
		}
		return Clip != nullptr ? Clip->GetName() : NoClip;
	}

	FString ReadAction(const ASnarehavenCharacter& Player, bool bActionStarted)
	{
		return bActionStarted
			? StaticEnum<EItemAction>()->GetNameStringByValue(static_cast<int64>(Player.GetLastItemAction()))
			: FString(NoAction);
	}

	/** `ClaimLoot` 묶음의 첫 줄 아이템 ID — 무엇을 보냈는지 이름으로 견주려고. */
	FString ClaimedItemID(const FServerCall& Call)
	{
		const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
		if (!Call.Args.IsValid() || !Call.Args->TryGetArrayField(TEXT("items"), Items) || Items->IsEmpty())
		{
			return FString();
		}
		const TSharedPtr<FJsonObject> First = (*Items)[0]->AsObject();
		FString ItemId;
		return First.IsValid() && First->TryGetStringField(TEXT("itemId"), ItemId) ? ItemId : FString();
	}

	/**
	 * 표 한 행을 태울 월드·플레이어·컴포넌트 한 벌. 두 시험(② 몸 클립 · ④ 서버 요청)이 똑같은 것을 세우므로
	 * 한곳에 모았다 (PROMPT ③-2). `Setup` 이 false 를 줘도 `Teardown` 은 부를 수 있다.
	 */
	struct FActionRig
	{
		UGameInstance* GameInstance = nullptr;
		UWorld* World = nullptr;
		USnareBackendSubsystem* Backend = nullptr;
		USnareBaseSubsystem* Base = nullptr;
		ASnarehavenCharacter* Player = nullptr;
		APlayerController* Controller = nullptr;
		UMeleeAttackComponent* Melee = nullptr;
		UGatherComponent* Gather = nullptr;
		UTapFishingComponent* Fishing = nullptr;
		UBackpackComponent* Backpack = nullptr;
		USnareBaseBuildComponent* Build = nullptr;
		ULootClaimComponent* LootClaim = nullptr;
		FActorSpawnParameters SpawnParams;

		bool Setup(FAutomationTestBase& Test)
		{
			SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

			GameInstance = NewObject<UGameInstance>(GEngine);
			GameInstance->InitializeStandalone();
			World = GameInstance->GetWorld();
			Backend = GameInstance->GetSubsystem<USnareBackendSubsystem>();
			Base = GameInstance->GetSubsystem<USnareBaseSubsystem>();
			if (!Test.TestNotNull(TEXT("월드"), World) || !Test.TestNotNull(TEXT("백엔드 창구"), Backend)
				|| !Test.TestNotNull(TEXT("기지 창구"), Base))
			{
				return false;
			}
			World->InitializeActorsForPlay(FURL());

			Player = World->SpawnActor<ASnarehavenCharacter>(FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
			if (!Test.TestNotNull(TEXT("플레이어"), Player))
			{
				return false;
			}
			Player->DispatchBeginPlay();
			Player->ApplyPlayerBody();
			// 설치는 "방장인가"를 조종으로 본다 — 조종이 없으면 놓기 자체가 안 열린다.
			Controller = World->SpawnActor<APlayerController>(SpawnParams);
			Controller->Player = NewObject<ULocalPlayer>(GEngine, ULocalPlayer::StaticClass());
			Player->Controller = Controller;
			AGameStateBase* const GameState = World->SpawnActor<AGameStateBase>(SpawnParams);
			World->SetGameState(GameState);
			GameState->GameModeClass = ASnarehavenGameMode::StaticClass();

			Melee = Player->GetMeleeAttack();
			Gather = Player->GetGather();
			Fishing = Player->FindComponentByClass<UTapFishingComponent>();
			Backpack = Player->FindComponentByClass<UBackpackComponent>();
			Build = Player->FindComponentByClass<USnareBaseBuildComponent>();
			LootClaim = Player->FindComponentByClass<ULootClaimComponent>();
			if (!Test.TestNotNull(TEXT("근접 컴포넌트"), Melee) || !Test.TestNotNull(TEXT("채집 컴포넌트"), Gather)
				|| !Test.TestNotNull(TEXT("낚시 컴포넌트"), Fishing) || !Test.TestNotNull(TEXT("배낭 컴포넌트"), Backpack)
				|| !Test.TestNotNull(TEXT("건축 컴포넌트"), Build) || !Test.TestNotNull(TEXT("지급 컴포넌트"), LootClaim))
			{
				return false;
			}
			Melee->SetAttackSettings(TestAttackDamage, TestAttackRangeCm, TestAttackIntervalSeconds);
			Backpack->BindTargets(Backend, nullptr);
			// 지급 대기열 보내기는 `BeginPlay` 에서 "내가 조종하는 폰" 일 때만 이어진다 — 여기선 조종을 나중에
			// 붙이느라 안 이어졌으니 직접 잇는다 (안 이으면 `ClaimLoot` 이 영영 안 나간다).
			LootClaim->BindTargets(Player->GetInventory(), Backend);
			return true;
		}

		void Teardown()
		{
			if (Player != nullptr)
			{
				Player->Controller = nullptr;
			}
			if (Controller != nullptr)
			{
				Controller->Player = nullptr;
			}
			if (GameInstance != nullptr)
			{
				GameInstance->Shutdown();
			}
		}
	};
}

using namespace SnareActionClipTest;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenActionClipTableTest,
	"Snarehaven.ActionMatrix.Clip.TableRowsPlayExpectedClip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenActionClipTableTest::RunTest(const FString& Parameters)
{
	FFakeServer Server;
	Server.Install();

	FActionRig Rig;
	if (!Rig.Setup(*this))
	{
		Rig.Teardown();
		return false;
	}
	// 아래 본문이 쓰는 이름들 — 리그가 세운 것을 그대로 가리킨다.
	UGameInstance* const GameInstance = Rig.GameInstance;
	UWorld* const World = Rig.World;
	USnareBaseSubsystem* const Base = Rig.Base;
	ASnarehavenCharacter* const Player = Rig.Player;
	UMeleeAttackComponent* const Melee = Rig.Melee;
	UTapFishingComponent* const Fishing = Rig.Fishing;
	UBackpackComponent* const Backpack = Rig.Backpack;
	USnareBaseBuildComponent* const Build = Rig.Build;
	FActorSpawnParameters& Params = Rig.SpawnParams;

	// 행마다 "무엇이 났나" 를 한 줄로 모은다 — 표와 견주고 로그로도 남긴다.
	TArray<FActionExpect> Seen;
	TArray<FString> Lines;
	int32 RowIndex = 0;
	const auto Record = [&](int32 CountBefore)
	{
		const bool bStarted = Player->GetItemActionCount() > CountBefore;
		const FString Action = ReadAction(*Player, bStarted);
		const FString Clip = ReadClip(*Player, bStarted);
		const FActionExpect& Want = ActionTable[RowIndex];
		// 표가 "클립 없음" 이라고 한 행은 **아이템 동작이 안 나고 대기 클립 그대로**인 것이 합격이다
		// (화면이 몸을 덮거나 불 위 타이머라 몸이 가만히 있는 행동).
		const bool bWantsNoClip = FCString::Strcmp(Want.Clip, NoClip) == 0;
		Lines.Add(FString::Printf(TEXT("%s → %s / %s"), Want.Row, *Action, bWantsNoClip ? NoClip : *Clip));
		TestEqual(*FString::Printf(TEXT("[%s] 고른 동작"), Want.Row), Action, FString(Want.Action));
		if (bWantsNoClip)
		{
			TestFalse(*FString::Printf(TEXT("[%s] 몸 동작이 안 난다"), Want.Row), bStarted);
			TestEqual(*FString::Printf(TEXT("[%s] 대기 클립 그대로"), Want.Row), Clip, FString(IdleClip));
		}
		else
		{
			TestEqual(*FString::Printf(TEXT("[%s] 몸 클립"), Want.Row), Clip, FString(Want.Clip));
		}
		++RowIndex;
	};

	// ── ① 공격 (근처에 동물·채집물이 없다) ──
	{
		const int32 Before = Player->GetItemActionCount();
		Player->BeginAttack();
		Player->UpdateBodyAnimation();
		TestTrue(TEXT("[공격] 휘두르기가 돈다"), Melee->IsSwinging());
		Record(Before);
		ResetBody(World, GameInstance, *Player);
	}

	// ── ② 나무 베기 · ③ 광맥 · ④ 맨손 채집 (같은 공격 버튼, 앞에 무엇이 있느냐로 갈린다) ──
	const auto GatherRow = [&](FName RequiredTool, FName HeldTool, const TCHAR* RowName)
	{
		AGatherableActor* const Target = SpawnGatherable(World, RequiredTool);
		if (!TestNotNull(*FString::Printf(TEXT("[%s] 채집물"), RowName), Target)
			|| !TestTrue(*FString::Printf(TEXT("[%s] 도구를 든다"), RowName), HoldTool(*Player, HeldTool)))
		{
			return;
		}
		const int32 Before = Player->GetItemActionCount();
		Player->BeginAttack();
		Player->UpdateBodyAnimation();
		TestFalse(*FString::Printf(TEXT("[%s] 채집물이 사라졌다"), RowName), IsValid(Target) && !Target->IsGathered());
		Record(Before);
		if (IsValid(Target))
		{
			Target->Destroy();
		}
		ResetBody(World, GameInstance, *Player);
		// 채집 동작은 한 프레임에 한 번만 난다(덤 알림이 두 번 틀지 않게) — 다음 행이 같은 프레임에 걸리지 않게 넘긴다.
		++GFrameCounter;
	};
	GatherRow(HatchetID, HatchetID, TEXT("나무 베기"));
	GatherRow(PickaxeID, PickaxeID, TEXT("광맥"));
	GatherRow(NAME_None, NAME_None, TEXT("맨손 채집"));

	// ── ⑤⑥⑦ 낚시 (던지기 → 입질 대기 → 감기) ──
	{
		AFishingSpotActor* const Spot = World->SpawnActorDeferred<AFishingSpotActor>(
			AFishingSpotActor::StaticClass(), FTransform(FVector(TestNearDistanceCm, 0.f, 0.f)));
		if (TestNotNull(TEXT("[낚시] 포인트"), Spot))
		{
			Spot->SpotId = TestSpotId.ToString();
			Spot->FinishSpawning(FTransform(FVector(TestNearDistanceCm, 0.f, 0.f)));
			if (!Spot->HasActorBegunPlay())
			{
				Spot->DispatchBeginPlay();
			}
			TestTrue(TEXT("[낚시] 낚싯대를 든다"), HoldTool(*Player, RodID));

			// 던진 순간 — 던지기 클립이 먼저 한 번 돈다.
			int32 Before = Player->GetItemActionCount();
			Fishing->BeginBiteWait(Spot, TestBiteSeconds, false);
			Player->UpdateBodyAnimation();
			Record(Before);

			// 던지기 클립 길이가 지나면 대기 클립으로 — 시간은 캐릭터 틱이 센다.
			const UAnimSequence* const CastClip = Player->GetCurrentBodyAnim();
			const float CastSeconds = CastClip != nullptr ? CastClip->GetPlayLength() : 1.f;
			for (float Elapsed = 0.f; Elapsed <= CastSeconds + TestFrameSeconds; Elapsed += TestFrameSeconds)
			{
				static_cast<AActor*>(Player)->Tick(TestFrameSeconds);
			}
			Before = Player->GetItemActionCount();
			Player->UpdateBodyAnimation();
			Record(Before);

			// 입질 → 연타. 입질 대기가 끝나면 서버(가짜)가 바로 어종을 줘서 연타 단계로 간다.
			Fishing->AdvanceFishing(TestBiteSeconds + 1.f);
			Fishing->Tap();
			Before = Player->GetItemActionCount();
			Player->UpdateBodyAnimation();
			Record(Before);

			Fishing->Quit();
			HoldTool(*Player, NAME_None);
			Spot->Destroy();
		}
		else
		{
			RowIndex += 3;
		}
		ResetBody(World, GameInstance, *Player);
	}

	// ── ⑧ 제작 · ⑨ 제련소 · ⑩ 요리 (서버 확정 뒤 줍기 몸짓 — M28-02) ──
	const UDataTable* const RecipeTable = LoadObject<UDataTable>(nullptr, RecipeTablePath);
	TestNotNull(TEXT("DT_RecipeTable"), RecipeTable);
	const auto CraftRow = [&](ERecipeStation Station, const TCHAR* RowName)
	{
		const FName Result = FindRecipeResult(RecipeTable, Station);
		if (!TestFalse(*FString::Printf(TEXT("[%s] 표에 레시피가 있다"), RowName), Result.IsNone()))
		{
			++RowIndex;
			return;
		}
		const int32 CallsBefore = Server.Calls;
		const int32 Before = Player->GetItemActionCount();
		TestTrue(*FString::Printf(TEXT("[%s] 만들기 요청이 나간다"), RowName), Base->RequestCraft(Result));
		TestEqual(*FString::Printf(TEXT("[%s] 서버를 한 번 부른다"), RowName), Server.Calls, CallsBefore + 1);
		Player->UpdateBodyAnimation();
		Record(Before);
	};
	CraftRow(ERecipeStation::Workbench, TEXT("제작"));
	CraftRow(ERecipeStation::Smelter, TEXT("제련소"));
	{
		// 요리는 불 위 타이머다 — 올릴 땐 가만히, 타이머가 끝나 `CraftItem` 이 확정되면 줍기 몸짓.
		const FName Cooked = FindRecipeResult(RecipeTable, ERecipeStation::Fire);
		const int32 Before = Player->GetItemActionCount();
		const bool bStarted = Base->StartCooking(TEXT("fire_action_clip"), CampfireID, Cooked);
		TestTrue(TEXT("[요리] 불에 올린다"), bStarted);
		const int32 CallsBefore = Server.Calls;
		TickTimers(World, GameInstance, Base->GetCookSeconds(CampfireID) + 1.f);
		TestTrue(TEXT("[요리] 타이머가 끝나면 서버가 만든다"), Server.Calls > CallsBefore);
		Player->UpdateBodyAnimation();
		Record(Before);
	}

	// ── ⑪ 먹기 (몸은 가만히 — 서버가 받아 준 뒤에만 머리 위 「+N」) ──
	{
		// 앞 행(요리)의 줍기 몸짓이 아직 돌고 있다 — 먹기가 「그대로 대기」 인지 보려면 먼저 내려놓는다.
		ResetBody(World, GameInstance, *Player);
		const int32 Before = Player->GetItemActionCount();
		const int32 PopsBefore = Player->GetHealPopCount();
		const FName EatCue = ASnarehavenCharacter::GetEatCue();
		const FName PoisonCue = ASnarehavenCharacter::GetEatPoisonCue();
		const int32 EatBefore = Player->GetBodyVoiceRequestCount(EatCue);
		const int32 PoisonBefore = Player->GetBodyVoiceRequestCount(PoisonCue);
		TestTrue(TEXT("[먹기] 먹기 요청이 나간다"), Backpack->RequestConsume(BerryID));
		Player->UpdateBodyAnimation();
		Record(Before);
		TestEqual(TEXT("[먹기] 받아 준 뒤 「+N」 한 번"), Player->GetHealPopCount(), PopsBefore + 1);
		TestEqual(TEXT("[먹기] N = 서버 heal"), Player->GetHealPopAmount(), TestHeal);
		const double Now = World->GetTimeSeconds();
		TestTrue(TEXT("[먹기] 지금 떠 있다"), Player->GetHealPopProgressAt(Now) >= 0.f);
		TestTrue(TEXT("[먹기] 시간이 지나면 사라진다"),
			Player->GetHealPopProgressAt(Now + ASnarehavenCharacter::HealPopSeconds + 0.01) < 0.f);
		AddInfo(FString::Printf(TEXT("[먹기] 머리 위 「%s」 · 몸 동작 %d → %d"),
			*UPlayerNameTagWidget::MakeHealPopLabel(Player->GetHealPopAmount()), Before, Player->GetItemActionCount()));
		ResetBody(World, GameInstance, *Player);

		// 거절이면 「+N」 도 없다 (규칙 8 — 받기 전에 먹은 척하지 않는다).
		Server.RejectFunction = TEXT("ConsumeItem");
		Backpack->RequestConsume(BerryID);
		Server.RejectFunction.Reset();
		TestEqual(TEXT("[먹기] 거절이면 「+N」 0"), Player->GetHealPopCount(), PopsBefore + 1);
		ResetBody(World, GameInstance, *Player);

		// 씹는 소리 (M44-04) — 받아 준 먹기 한 번 = 1 · 거절 = 0 · 독(음수 heal) = 독 소리.
		TestEqual(TEXT("[먹기 소리] 받아 준 뒤 씹기 1"), Player->GetBodyVoiceRequestCount(EatCue), EatBefore + 1);
		TestEqual(TEXT("[먹기 소리] 거절 = 그대로(독 소리도 0)"), Player->GetBodyVoiceRequestCount(PoisonCue), PoisonBefore);
		{
			// 응답 전에는 소리가 없다 — 답을 쥐고 있는 동안 0, 풀면 1 (규칙 8).
			Server.bHold = true;
			Backpack->RequestConsume(BerryID);
			TestEqual(TEXT("[먹기 소리] 응답 전 0"), Player->GetBodyVoiceRequestCount(EatCue), EatBefore + 1);
			Server.bHold = false;
			Server.Flush();
			TestEqual(TEXT("[먹기 소리] 응답 뒤 씹기 2"), Player->GetBodyVoiceRequestCount(EatCue), EatBefore + 2);
		}
		Server.Heal = TestPoisonHeal;
		Backpack->RequestConsume(BerryID);
		Server.Heal = TestHeal;
		TestEqual(TEXT("[먹기 소리] 독 = 독 소리 1"), Player->GetBodyVoiceRequestCount(PoisonCue), PoisonBefore + 1);
		TestEqual(TEXT("[먹기 소리] 독은 씹기 그대로"), Player->GetBodyVoiceRequestCount(EatCue), EatBefore + 2);
		AddInfo(FString::Printf(TEXT("먹기 소리: 받음 2 → 씹기 %d · 거절 1 → 0 · 독 1 → 독 소리 %d"),
			Player->GetBodyVoiceRequestCount(EatCue) - EatBefore, Player->GetBodyVoiceRequestCount(PoisonCue) - PoisonBefore));
		ResetBody(World, GameInstance, *Player);

		// 글자 — 회복 「+15」 · 독 「-20」 · 앞 절반 그대로, 끝에 투명.
		TestEqual(TEXT("[먹기] 회복 글자"), UPlayerNameTagWidget::MakeHealPopLabel(15), FString(TEXT("+15")));
		TestEqual(TEXT("[먹기] 독 글자"), UPlayerNameTagWidget::MakeHealPopLabel(-20), FString(TEXT("-20")));
		TestTrue(TEXT("[먹기] 0 이면 빈 글"), UPlayerNameTagWidget::MakeHealPopLabel(0).IsEmpty());
		TestEqual(TEXT("[먹기] 앞 절반 불투명"), UPlayerNameTagWidget::GetHealPopOpacity(0.4f), 1.f);
		TestEqual(TEXT("[먹기] 끝에 투명"), UPlayerNameTagWidget::GetHealPopOpacity(1.f), 0.f);
		TestTrue(TEXT("[먹기] 위로 떠오른다"), UPlayerNameTagWidget::GetHealPopRisePx(1.f) > UPlayerNameTagWidget::GetHealPopRisePx(0.f));
	}

	// ── ⑫ 설치 (요청이 나간 순간 쪼그려 손을 댄다 — 물건은 서버 응답 뒤에 선다) ──
	{
		// 텐트로 본다 — 보관함·불은 **내 텐트 격자 칸 안**이라야 해서 자리 잡기가 이 조각 밖이다(M15-14 몫).
		const int32 Before = Player->GetItemActionCount();
		if (TestTrue(TEXT("[설치] 놓기 모드가 켜진다"), Build->BeginPlacing(TentID)))
		{
			TestTrue(TEXT("[설치] 확정이 서버로 간다"), Build->ConfirmPlacement(FString()));
		}
		Player->UpdateBodyAnimation();
		Record(Before);
		ResetBody(World, GameInstance, *Player);
	}

	// ── ⑬ 포획 (누르는 순간 올가미 던져 당기기 — 성공·실패 같은 몸짓) ──
	{
		AAnimalCharacter* const Animal =
			World->SpawnActor<AAnimalCharacter>(FVector(TestNearDistanceCm, 0.f, 0.f), FRotator::ZeroRotator, Params);
		if (TestNotNull(TEXT("[포획] 동물"), Animal))
		{
			FAnimalRow Row;
			Row.AnimalID = TEXT("animal.test_action");
			Row.MaxHP = TestAnimalMaxHP;
			Row.CaptureThresholdPct = TestCaptureThresholdPct;
			Row.PetItemID = TestPetItemID;
			Animal->GetAnimalState()->InitializeFromRow(Row);
			while (Animal->GetAnimalState()->GetState() != EAnimalState::Capturable
				&& !Animal->GetAnimalState()->IsFinished() && Melee->AttackOnce())
			{
				// 임계값까지 때려 포획 창을 연다 — 타수는 M1-03 검사 몫이다.
			}
			ResetBody(World, GameInstance, *Player);
			const int32 Before = Player->GetItemActionCount();
			TestTrue(TEXT("[포획] 포획 성공"), Player->GetCapture()->TryCaptureInWindowForTest());
			Player->UpdateBodyAnimation();
			Record(Before);
			Animal->Destroy();
		}
		else
		{
			++RowIndex;
		}
	}

	TestEqual(TEXT("표 11줄(낚시 3단계로 펴서 13줄)을 다 태웠다"), RowIndex, static_cast<int32>(UE_ARRAY_COUNT(ActionTable)));
	AddInfo(FString::Printf(TEXT("[ActionMatrix] 버튼 → 동작 / 클립 — %s"), *FString::Join(Lines, TEXT(" · "))));

	Rig.Teardown();
	return true;
}


// ── M15-13 · 표의 ④ 서버 칸 — 버튼을 누르면 **어떤 백엔드 함수가** 나가는가 ──
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenActionServerTableTest,
	"Snarehaven.ActionMatrix.Server.TableRowsCallExpectedFunction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenActionServerTableTest::RunTest(const FString& Parameters)
{
	FFakeServer Server;
	Server.Install();

	FActionRig Rig;
	if (!Rig.Setup(*this))
	{
		Rig.Teardown();
		return false;
	}
	UWorld* const World = Rig.World;
	UGameInstance* const GameInstance = Rig.GameInstance;
	ASnarehavenCharacter* const Player = Rig.Player;
	USnareBaseSubsystem* const Base = Rig.Base;
	UInventoryComponent* const Inventory = Player->GetInventory();
	Server.TentZone = USnareBaseBuildComponent::GetTentZone(World).ToString();

	TArray<FString> Lines;
	int32 RowIndex = 0;
	int32 CallsRead = 0;

	/** 지난 행 뒤로 새로 나간 함수 이름들. */
	const auto NewCalls = [&Server, &CallsRead]()
	{
		TArray<FString> Names;
		for (int32 Index = CallsRead; Index < Server.Log.Num(); ++Index)
		{
			Names.Add(Server.Log[Index].Function);
		}
		return Names;
	};

	/** 한 행을 끝내고 표와 견준다. */
	const auto Record = [&]()
	{
		const FServerExpect& Want = ServerTable[RowIndex];
		TArray<FString> Expected;
		for (const TCHAR* const Function : Want.Functions)
		{
			if (Function != nullptr)
			{
				Expected.Add(Function);
			}
		}
		const TArray<FString> Actual = NewCalls();
		const FString ActualText = Actual.IsEmpty() ? FString(TEXT("없음")) : FString::Join(Actual, TEXT("→"));
		Lines.Add(FString::Printf(TEXT("%s → %s"), Want.Row, *ActualText));
		TestEqual(*FString::Printf(TEXT("[%s] 나간 요청 수"), Want.Row), Actual.Num(), Expected.Num());
		for (int32 Index = 0; Index < FMath::Min(Actual.Num(), Expected.Num()); ++Index)
		{
			TestEqual(*FString::Printf(TEXT("[%s] %d번째 함수"), Want.Row, Index + 1), Actual[Index], Expected[Index]);
		}
		CallsRead = Server.Log.Num();
		++RowIndex;
	};

	/** 마지막으로 나간 요청의 문자열 인자 (없으면 빈 글자). */
	const auto LastArgString = [&Server](const TCHAR* Key)
	{
		FString Out;
		if (!Server.Log.IsEmpty() && Server.Log.Last().Args.IsValid())
		{
			Server.Log.Last().Args->TryGetStringField(Key, Out);
		}
		return Out;
	};

	// ── ① 공격 (호스트가 그 자리에서 판정한다 — 백엔드로는 아무것도 안 나간다) ──
	{
		Player->BeginAttack();
		TestTrue(TEXT("[공격] 호스트가 판정한다"), Player->GetMeleeAttack()->HasAttackAuthority());
		Record();
		ResetBody(World, GameInstance, *Player);
	}

	// ── ②③④ 나무 베기 · 광맥 · 맨손 채집 (누를 땐 0건, 대기열이 `ClaimLoot` 한 번으로 나간다) ──
	const auto GatherRow = [&](FName RequiredTool, FName HeldTool, const TCHAR* RowName)
	{
		AGatherableActor* const Target = SpawnGatherable(World, RequiredTool, TestClaimableGatherID,
			Player->GetActorLocation() + FVector(TestNearDistanceCm, 0.f, 0.f));
		if (!TestNotNull(*FString::Printf(TEXT("[%s] 채집물"), RowName), Target)
			|| !TestTrue(*FString::Printf(TEXT("[%s] 도구를 든다"), RowName), HoldTool(*Player, HeldTool)))
		{
			++RowIndex;
			return;
		}
		const int32 PendingBefore = Inventory->GetPendingCount(TestClaimableGatherID);
		Player->BeginAttack();
		// 캔 것은 **대기열에만** 쌓인다 — 여기서 보유를 늘리거나 서버를 부르면 CLAUDE.md 8 위반이다.
		TestEqual(*FString::Printf(TEXT("[%s] 누른 자리에선 서버를 안 부른다"), RowName), NewCalls().Num(), 0);
		TestEqual(*FString::Printf(TEXT("[%s] 대기열에 쌓인다"), RowName),
			Inventory->GetPendingCount(TestClaimableGatherID), PendingBefore + 1);
		TestTrue(*FString::Printf(TEXT("[%s] 대기열이 서버로 나간다"), RowName), Rig.LootClaim->TryClaimNow());
		TestEqual(*FString::Printf(TEXT("[%s] 보낸 아이템"), RowName),
			ClaimedItemID(Server.Log.Last()), TestClaimableGatherID.ToString());
		Record();
		if (IsValid(Target))
		{
			Target->Destroy();
		}
		ResetBody(World, GameInstance, *Player);
		++GFrameCounter;   // 채집 동작은 한 프레임에 한 번만 난다 — 다음 행이 같은 프레임에 걸리지 않게
	};
	GatherRow(HatchetID, HatchetID, TEXT("나무 베기"));
	GatherRow(PickaxeID, PickaxeID, TEXT("광맥"));
	GatherRow(NAME_None, NAME_None, TEXT("맨손 채집"));

	// ── ⑤ 낚시 (던질 때 `RollFish`, 다 감으면 `CatchFish`) ──
	{
		const FVector SpotWhere = Player->GetActorLocation() + FVector(TestNearDistanceCm, 0.f, 0.f);
		AFishingSpotActor* const Spot = World->SpawnActorDeferred<AFishingSpotActor>(
			AFishingSpotActor::StaticClass(), FTransform(SpotWhere));
		if (TestNotNull(TEXT("[낚시] 포인트"), Spot))
		{
			Spot->SpotId = TestSpotId.ToString();
			Spot->FinishSpawning(FTransform(SpotWhere));
			if (!Spot->HasActorBegunPlay())
			{
				Spot->DispatchBeginPlay();
			}
			TestTrue(TEXT("[낚시] 낚싯대를 든다"), HoldTool(*Player, RodID));
			Rig.Fishing->CastAtNearest();
			Rig.Fishing->AdvanceFishing(TestBiteWaitCapSeconds);
			// 소리 장치가 없는 실행(`-NoSound`)에선 릴 되풀이 오디오 컴포넌트가 안 생긴다 — 그때는 경고로 알리고
			// 감기 쪽 판정만 건너뛴다(전기톱 공회전 `SnarehavenHoldLoopTest` 와 같은 방식, 루프#291).
			const bool bHasAudioDevice = World->GetAudioDeviceRaw() != nullptr;
			if (bHasAudioDevice)
			{
				TestTrue(TEXT("[낚시] 입질 뒤 릴 감는 되풀이 소리가 난다"), Rig.Fishing->IsReelLoopPlaying());
			}
			else
			{
				AddWarning(TEXT("소리 장치가 없다(-NoSound?) — 낚시 릴 감는 되풀이 소리는 이번 실행에서 못 잰다"));
			}
			// 목표까지 연타한다 — 몇 번이 필요한지는 표(`DT_WaterActivityTable`)가 정한다.
			int32 Taps = 0;
			while (Rig.Fishing->GetState() == ETapFishingState::Tapping && Taps < TestMaxTaps)
			{
				Rig.Fishing->Tap();
				++Taps;
			}
			TestTrue(TEXT("[낚시] 목표까지 감았다"), Taps > 0 && Taps < TestMaxTaps);
			// M16-06: 던지기 → 입질 → 감기 순서로 낚싯대 행 칸의 소리가 난다(기본 낚싯대 = Cast · Bite · Reel…).
			const TArray<FName>& SoundLog = Rig.Fishing->GetRodSoundLog();
			if (TestEqual(TEXT("[낚시] 던지기·입질(·감기) 소리 수"), SoundLog.Num(), bHasAudioDevice ? 3 : 2))
			{
				TestEqual(TEXT("[낚시] 첫 소리는 던지기"), SoundLog[0], FName(TEXT("Cast")));
				TestEqual(TEXT("[낚시] 둘째 소리는 입질"), SoundLog[1], FName(TEXT("Bite")));
				if (bHasAudioDevice)
				{
					TestEqual(TEXT("[낚시] 셋째 소리는 감기"), SoundLog[2], FName(TEXT("Reel")));
				}
			}
			TestFalse(TEXT("[낚시] 다 감으면 릴 소리가 멈춘다"), Rig.Fishing->IsReelLoopPlaying());
			UE_LOG(LogTemp, Log, TEXT("[낚시] 소리 순서 %d개, 앞 셋: %s %s %s"), SoundLog.Num(),
				SoundLog.IsValidIndex(0) ? *SoundLog[0].ToString() : TEXT("-"),
				SoundLog.IsValidIndex(1) ? *SoundLog[1].ToString() : TEXT("-"),
				SoundLog.IsValidIndex(2) ? *SoundLog[2].ToString() : TEXT("-"));
			TestEqual(TEXT("[낚시] 잡은 어종은 서버가 말한 것"), Rig.Fishing->GetLastCaughtItem(), TestFishID);
			Record();
			Rig.Fishing->Quit();
			HoldTool(*Player, NAME_None);
			Spot->Destroy();
		}
		else
		{
			++RowIndex;
		}
		ResetBody(World, GameInstance, *Player);
	}

	// ── ⑥ 제작 · ⑦ 제련소 · ⑧ 요리 (셋 다 `CraftItem`, 요리는 불 위 타이머가 끝난 뒤) ──
	const UDataTable* const RecipeTable = LoadObject<UDataTable>(nullptr, RecipeTablePath);
	TestNotNull(TEXT("DT_RecipeTable"), RecipeTable);
	const auto CraftRow = [&](ERecipeStation Station, const TCHAR* RowName)
	{
		const FName Result = FindRecipeResult(RecipeTable, Station);
		if (!TestFalse(*FString::Printf(TEXT("[%s] 표에 레시피가 있다"), RowName), Result.IsNone()))
		{
			++RowIndex;
			return;
		}
		TestTrue(*FString::Printf(TEXT("[%s] 만들기 요청이 나간다"), RowName), Base->RequestCraft(Result));
		TestEqual(*FString::Printf(TEXT("[%s] 보낸 레시피"), RowName), LastArgString(TEXT("recipeId")), Result.ToString());
		Record();
	};
	CraftRow(ERecipeStation::Workbench, TEXT("제작"));
	CraftRow(ERecipeStation::Smelter, TEXT("제련소"));
	{
		const FName Cooked = FindRecipeResult(RecipeTable, ERecipeStation::Fire);
		TestTrue(TEXT("[요리] 불에 올린다"), Base->StartCooking(TEXT("fire_action_server"), CampfireID, Cooked));
		TestEqual(TEXT("[요리] 올린 자리에선 서버를 안 부른다"), NewCalls().Num(), 0);
		TickTimers(World, GameInstance, Base->GetCookSeconds(CampfireID) + 1.f);
		TestEqual(TEXT("[요리] 타이머 끝에 보낸 레시피"), LastArgString(TEXT("recipeId")), Cooked.ToString());
		Record();
	}

	// ── ⑨ 먹기 ──
	{
		TestTrue(TEXT("[먹기] 먹기 요청이 나간다"), Rig.Backpack->RequestConsume(BerryID));
		TestEqual(TEXT("[먹기] 보낸 아이템"), LastArgString(TEXT("itemId")), BerryID.ToString());
		Record();
		ResetBody(World, GameInstance, *Player);
	}

	// ── ⑩ 설치-텐트 · ⑪ 설치-가구 (텐트는 `PlaceTent`, 그 땅 안 가구는 `PlaceStructure`) ──
	{
		if (TestTrue(TEXT("[설치-텐트] 놓기 모드가 켜진다"), Rig.Build->BeginPlacing(TentID)))
		{
			TestTrue(TEXT("[설치-텐트] 확정이 서버로 간다"), Rig.Build->ConfirmPlacement(FString()));
		}
		Record();
		TestEqual(TEXT("[설치-텐트] 응답 뒤 텐트 사본 1채"), Base->GetTents().Num(), 1);

		// 가구는 **텐트 땅 안 빈 칸**이라야 한다 — 텐트 몸과 겹치지 않을 때까지 한 칸씩 앞으로 걸어가 본다.
		const FBaseRuleRow* const Rules = Base->GetRules();
		const float CellSizeCm = Rules != nullptr ? Rules->CellSizeCm : 0.f;
		bool bPlaced = false;
		if (TestTrue(TEXT("[설치-가구] 칸 크기가 표에 있다"), CellSizeCm > 0.f)
			&& TestTrue(TEXT("[설치-가구] 놓기 모드가 켜진다"), Rig.Build->BeginPlacing(CampfireID)))
		{
			for (int32 Step = 0; Step < TestMaxPlaceSteps && !bPlaced; ++Step)
			{
				Player->SetActorLocation(Player->GetActorLocation() + Player->GetActorForwardVector() * CellSizeCm);
				bPlaced = Rig.Build->ConfirmPlacement(FString());
			}
		}
		TestTrue(TEXT("[설치-가구] 확정이 서버로 간다"), bPlaced);
		TestEqual(TEXT("[설치-가구] 보낸 아이템"), LastArgString(TEXT("itemId")), CampfireID.ToString());
		TestEqual(TEXT("[설치-가구] 보낸 텐트"), LastArgString(TEXT("tentId")), FString(TestTentId));
		Record();
		ResetBody(World, GameInstance, *Player);
	}

	// ── ⑫ 포획 (호스트가 판정하고, 펫은 대기열을 거쳐 `ClaimLoot` 으로 나간다) ──
	{
		AAnimalCharacter* const Animal = World->SpawnActor<AAnimalCharacter>(
			Player->GetActorLocation() + FVector(TestNearDistanceCm, 0.f, 0.f), FRotator::ZeroRotator, Rig.SpawnParams);
		if (TestNotNull(TEXT("[포획] 동물"), Animal))
		{
			FAnimalRow Row;
			Row.AnimalID = TEXT("animal.test_action");
			Row.MaxHP = TestAnimalMaxHP;
			Row.CaptureThresholdPct = TestCaptureThresholdPct;
			Row.PetItemID = TestClaimablePetID;
			Animal->GetAnimalState()->InitializeFromRow(Row);
			while (Animal->GetAnimalState()->GetState() != EAnimalState::Capturable
				&& !Animal->GetAnimalState()->IsFinished() && Rig.Melee->AttackOnce())
			{
				// 임계값까지 때려 포획 창을 연다 — 때리는 동안에도 백엔드로는 아무것도 안 나간다.
			}
			ResetBody(World, GameInstance, *Player);
			TestEqual(TEXT("[포획] 때리는 동안 서버를 안 부른다"), NewCalls().Num(), 0);
			TestTrue(TEXT("[포획] 호스트가 확정한다"), Player->GetCapture()->TryCaptureInWindowForTest());
			TestEqual(TEXT("[포획] 펫은 대기열에만 1건"), Inventory->GetPendingCount(TestClaimablePetID), 1);
			TestTrue(TEXT("[포획] 대기열이 서버로 나간다"), Rig.LootClaim->TryClaimNow());
			TestEqual(TEXT("[포획] 보낸 아이템"), ClaimedItemID(Server.Log.Last()), TestClaimablePetID.ToString());
			Record();
			Animal->Destroy();
		}
		else
		{
			++RowIndex;
		}
	}

	TestEqual(TEXT("표 11줄(설치를 텐트·가구로 펴서 12줄)을 다 태웠다"), RowIndex,
		static_cast<int32>(UE_ARRAY_COUNT(ServerTable)));
	AddInfo(FString::Printf(TEXT("[ActionMatrix] 버튼 → 서버 요청 — %s"), *FString::Join(Lines, TEXT(" · "))));

	Rig.Teardown();
	return true;
}

// ── M15-14 (INBOX ★★★★ ②) — 설치를 게스트도 요청한다 ──
// 전에는 `CanBuildHere` 가 **권위(호스트)** 를 봐서 게스트는 놓기 모드조차 안 켜졌다(텐트·불·보관함을 못 썼다).
// 이제 보는 것은 "내가 조종하는 몸인가 · 지을 수 있는 땅인가" 둘뿐이고, 판정·차감은 그대로 백엔드다.
// 여기서 보는 것: ① 게스트도 요청이 **실제로 나간다**(나간 함수 이름) ② **응답 전에는 화면에 아무것도 안 선다**
// (CLAUDE.md 8) ③ 응답 뒤 선 것은 **복제하지 않는다** — 클라이언트에는 복제 권한이 없다(DESIGN 8.11 멀티)
// ④ 남의 몸(내가 조종하지 않는 복제본)은 여전히 못 놓는다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenActionGuestPlaceTest,
	"Snarehaven.ActionMatrix.Server.GuestPlaceSendsRequest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenActionGuestPlaceTest::RunTest(const FString& Parameters)
{
	FFakeServer Server;
	Server.bHold = true;   // 답을 쥐고 있는다 — "응답 전" 을 볼 수 있게
	Server.Install();

	// 리그(`FActionRig`)는 조종을 **BeginPlay 뒤에** 붙여서 건축 컴포넌트가 "내 폰이 아니다" 로 일찍 빠진다.
	// 여기서는 서버 응답이 화면에 이어지는 것까지 봐야 하므로, 조종·게임상태를 먼저 세우고 컴포넌트를 나중에 단다.
	UGameInstance* const GameInstance = NewObject<UGameInstance>(GEngine);
	GameInstance->InitializeStandalone();
	UWorld* const World = GameInstance->GetWorld();
	USnareBaseSubsystem* const Base = GameInstance->GetSubsystem<USnareBaseSubsystem>();
	if (!TestNotNull(TEXT("월드"), World) || !TestNotNull(TEXT("기지 창구"), Base))
	{
		return false;
	}
	World->InitializeActorsForPlay(FURL());

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AGameStateBase* const GameState = World->SpawnActor<AGameStateBase>(Params);
	World->SetGameState(GameState);
	GameState->GameModeClass = ASnarehavenGameMode::StaticClass();
	Server.TentZone = USnareBaseBuildComponent::GetTentZone(World).ToString();

	APawn* const Pawn = World->SpawnActor<APawn>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
	APlayerController* const Controller = World->SpawnActor<APlayerController>(Params);
	if (!TestNotNull(TEXT("폰"), Pawn) || !TestNotNull(TEXT("조종"), Controller))
	{
		return false;
	}
	Controller->Player = NewObject<ULocalPlayer>(GEngine, ULocalPlayer::StaticClass());
	Pawn->Controller = Controller;
	// 맨 `APawn` 은 뿌리 컴포넌트가 없어 `SetActorLocation` 이 먹지 않는다 — 몸이 안 움직이면
	// 미리보기 칸도 늘 같은 자리라 빈 칸을 못 찾는다. 자리를 옮길 뿌리를 하나 단다.
	USceneComponent* const Root = NewObject<USceneComponent>(Pawn, TEXT("GuestRoot"));
	Root->RegisterComponent();
	Pawn->SetRootComponent(Root);
	// 게스트 = 내가 조종하지만 권위는 없는 몸 (호스트 판정은 저쪽에서 난다).
	Pawn->SetRole(ROLE_AutonomousProxy);
	Pawn->DispatchBeginPlay();

	USnareBaseBuildComponent* const Build = NewObject<USnareBaseBuildComponent>(Pawn, TEXT("GuestBuild"));
	Build->RegisterComponent();   // 폰이 이미 깨어 있어 여기서 컴포넌트 BeginPlay 가 돈다 (서버 응답 → 화면 잇기)

	TestFalse(TEXT("게스트는 권위가 없다"), Pawn->HasAuthority());
	TestTrue(TEXT("게스트도 놓기 권한이 있다"), Build->CanBuildHere());
	TestTrue(TEXT("게스트 텐트 카드에 막는 까닭이 없다"), Build->GetPlaceBlockReason(TentID).IsEmpty());

	// 화면에 선 것 = 미리보기가 아닌 구조물 액터.
	const auto StandingCount = [World]()
	{
		int32 Count = 0;
		for (TActorIterator<ASnarePlacedStructureActor> It(World); It; ++It)
		{
			Count += It->IsPreview() ? 0 : 1;
		}
		return Count;
	};
	const auto LastFunction = [&Server]()
	{
		return Server.Log.IsEmpty() ? FString() : Server.Log.Last().Function;
	};

	// ── 텐트 (요청은 나가고, 응답 전에는 아무것도 안 선다) ──
	FString TentCall;
	if (TestTrue(TEXT("[게스트 설치-텐트] 놓기 모드가 켜진다"), Build->BeginPlacing(TentID)))
	{
		TestTrue(TEXT("[게스트 설치-텐트] 확정이 서버로 간다"), Build->ConfirmPlacement(FString()));
		TentCall = LastFunction();
	}
	TestEqual(TEXT("[게스트 설치-텐트] 나간 함수"), TentCall, FString(TEXT("PlaceTent")));
	TestEqual(TEXT("[게스트 설치-텐트] 응답 전에는 사본이 없다"), Base->GetTents().Num(), 0);
	TestEqual(TEXT("[게스트 설치-텐트] 응답 전에는 화면에 안 선다"), StandingCount(), 0);
	Server.Flush();
	TestEqual(TEXT("[게스트 설치-텐트] 응답 뒤 사본 1채"), Base->GetTents().Num(), 1);
	TestEqual(TEXT("[게스트 설치-텐트] 응답 뒤 화면에 1채"), StandingCount(), 1);
	for (TActorIterator<ASnarePlacedStructureActor> It(World); It; ++It)
	{
		if (!It->IsPreview())
		{
			TestFalse(TEXT("[게스트 설치-텐트] 게스트가 세운 것은 복제하지 않는다"), It->GetIsReplicated());
		}
	}

	// ── 가구 (내 텐트 땅 안 빈 칸을 찾을 때까지 한 칸씩 앞으로 — 서버 칸 검사와 같은 길) ──
	const FBaseRuleRow* const Rules = Base->GetRules();
	const float CellSizeCm = Rules != nullptr ? Rules->CellSizeCm : 0.f;
	bool bSent = false;
	if (TestTrue(TEXT("[게스트 설치-가구] 칸 크기가 표에 있다"), CellSizeCm > 0.f)
		&& TestTrue(TEXT("[게스트 설치-가구] 놓기 모드가 켜진다"), Build->BeginPlacing(CampfireID)))
	{
		for (int32 Step = 0; Step < TestMaxPlaceSteps && !bSent; ++Step)
		{
			Pawn->SetActorLocation(Pawn->GetActorLocation() + Pawn->GetActorForwardVector() * CellSizeCm);
			bSent = Build->ConfirmPlacement(FString());
		}
	}
	TestTrue(TEXT("[게스트 설치-가구] 확정이 서버로 간다"), bSent);
	const FString StructureCall = LastFunction();
	TestEqual(TEXT("[게스트 설치-가구] 나간 함수"), StructureCall, FString(TEXT("PlaceStructure")));
	TestEqual(TEXT("[게스트 설치-가구] 응답 전에는 텐트 1채 그대로"), StandingCount(), 1);
	Server.Flush();
	TestEqual(TEXT("[게스트 설치-가구] 응답 뒤 화면에 2채"), StandingCount(), 2);

	// ── 남의 몸(내가 조종하지 않는 복제본)은 여전히 못 놓는다 ──
	Pawn->Controller = nullptr;
	TestFalse(TEXT("남의 복제본은 놓기 권한이 없다"), Build->CanBuildHere());
	TestFalse(TEXT("남의 복제본엔 까닭이 뜬다"), Build->GetPlaceBlockReason(TentID).IsEmpty());

	AddInfo(FString::Printf(TEXT("[ActionMatrix] 게스트 설치 — 권위 %s · 텐트 %s · 가구 %s · 응답 뒤 화면 %d채 · 복제 %s"),
		Pawn->HasAuthority() ? TEXT("있음") : TEXT("없음"), *TentCall, *StructureCall, StandingCount(),
		TEXT("안 함")));

	Controller->Player = nullptr;
	GameInstance->Shutdown();
	return true;
}


// ── M15-15 · 호스트가 게스트의 채집 부탁을 물릴 때 **까닭 한 줄**이 게스트 화면으로 돌아가는가 ──
// (INBOX ★★★★ 2026-09-25 ③ · STATUS #0271 「남은 것 #9 게스트 채집 거절이 말없음」)
// 멀티에서 복제 나무를 베는 판정은 호스트 자리에서 난다. 전에는 거기서 조용히 돌아서서 게스트 화면엔
// 아무 일도 안 난 것처럼 보였다. 여기서 보는 것: 세 가지 거절(사거리 밖 · 도구 안 맞음 · 먼저 가져감)마다
// ① **까닭 글이 실제로 게스트 쪽에 도착하고** ② 채집물은 그대로 남고 ③ 되는 길은 말이 없다,
// 그리고 ④ 캐릭터가 그 까닭을 듣고 있다(HUD 한 줄로 가는 배선).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenActionGuestGatherRejectTest,
	"Snarehaven.ActionMatrix.Server.GuestGatherRejectionTellsWhy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenActionGuestGatherRejectTest::RunTest(const FString& Parameters)
{
	FFakeServer Server;
	Server.Install();

	FActionRig Rig;
	if (!Rig.Setup(*this))
	{
		Rig.Teardown();
		return false;
	}
	UWorld* const World = Rig.World;
	ASnarehavenCharacter* const Player = Rig.Player;
	UGatherComponent* const Gather = Rig.Gather;

	// 배선부터 — 까닭이 돌아와도 듣는 사람이 없으면 화면엔 여전히 아무 말이 없다.
	TestTrue(TEXT("캐릭터가 거절 까닭을 듣고 있다"),
		Gather->OnGatherRejected.GetAllObjects().Contains(Player));
	TestTrue(TEXT("처음엔 물린 까닭이 없다"), Gather->GetLastRejectMessage().IsEmpty());

	// ── 게스트 자리: 복제 채집물은 **로컬에서 직접 베지 않고 부탁만 보낸다** ──
	// 검사 월드는 standalone 이라 권위 없는 몸이 부른 Server RPC 를 엔진이 흡수한다(`AActor::GetFunctionCallspace`
	// — `LocalRole < ROLE_Authority && FUNC_NetServer` → `Absorbed`). 그래서 여기서는 "게스트가 어느 길을
	// 골랐나"(로컬로 베지 않았다)까지 보고, 호스트가 그 부탁을 받은 뒤는 아래에서 권위 자리로 태운다.
	AGatherableActor* const Remote = SpawnGatherable(World, NAME_None, TestBerryID,
		FVector(TestNearDistanceCm, 0.f, 0.f), TestRespawnSeconds);
	if (!TestNotNull(TEXT("[게스트] 복제 채집물"), Remote))
	{
		Rig.Teardown();
		return false;
	}
	Remote->SetReplicates(true);
	Player->SetRole(ROLE_AutonomousProxy);
	TestTrue(TEXT("[게스트] 부탁을 보냈다"), Gather->TryGatherNearest());
	TestTrue(TEXT("[게스트] 제 손으로 베지 않는다"), Remote->CanGather());

	// ── 호스트 자리: 게스트의 부탁을 받아 거리·도구를 다시 보는 그 함수를 그대로 태운다 ──
	Player->SetRole(ROLE_Authority);

	/** 기대 문구는 시험에 적지 않고 문구를 고르는 그 함수에서 가져온다 (문구가 바뀌면 한곳만 바뀐다). */
	const auto Expected = [](EGatherRejectReason Reason, FName Tool)
	{
		return UGatherComponent::GetRejectMessage(Reason, Tool).ToString();
	};

	// ① 사거리 밖 — 게스트 화면이 먼저 움직였거나 지연으로 어긋난 자리.
	AGatherableActor* const Far = SpawnGatherable(World, NAME_None, TestBerryID,
		FVector(TestFarDistanceCm, 0.f, 0.f));
	if (!TestNotNull(TEXT("[사거리 밖] 채집물"), Far))
	{
		Rig.Teardown();
		return false;
	}
	Gather->Server_GatherTarget(Far);
	const FString TooFarLine = Gather->GetLastRejectMessage().ToString();
	TestEqual(TEXT("[사거리 밖] 까닭 한 줄"), TooFarLine, Expected(EGatherRejectReason::TooFar, NAME_None));
	TestTrue(TEXT("[사거리 밖] 채집물은 그대로 남는다"), Far->CanGather());

	// ② 도구가 안 맞는다 — 손도끼가 있어야 하는 나무를 맨손으로 부탁한 자리.
	AGatherableActor* const NeedTool = SpawnGatherable(World, HatchetID, TestBerryID,
		FVector(TestNearDistanceCm, 0.f, 0.f), TestRespawnSeconds);
	if (!TestNotNull(TEXT("[도구] 채집물"), NeedTool) || !TestTrue(TEXT("[도구] 맨손"), HoldTool(*Player, NAME_None)))
	{
		Rig.Teardown();
		return false;
	}
	Gather->Server_GatherTarget(NeedTool);
	const FString NeedToolLine = Gather->GetLastRejectMessage().ToString();
	TestEqual(TEXT("[도구] 까닭 한 줄"), NeedToolLine, Expected(EGatherRejectReason::NeedTool, HatchetID));
	TestTrue(TEXT("[도구] 채집물은 그대로 남는다"), NeedTool->CanGather());

	// ③ 먼저 누른 사람이 가져갔다 — 호스트가 이미 벤 나무(다시 자라는 중)를 부탁한 자리.
	AGatherableActor* const Taken = SpawnGatherable(World, NAME_None, TestBerryID,
		FVector(0.f, TestNearDistanceCm, 0.f), TestRespawnSeconds);
	if (!TestNotNull(TEXT("[경합] 채집물"), Taken) || !TestTrue(TEXT("[경합] 호스트가 먼저 벴다"), Taken->Gather()))
	{
		Rig.Teardown();
		return false;
	}
	Gather->Server_GatherTarget(Taken);
	const FString TakenLine = Gather->GetLastRejectMessage().ToString();
	TestEqual(TEXT("[경합] 까닭 한 줄"), TakenLine, Expected(EGatherRejectReason::AlreadyTaken, NAME_None));

	// 세 까닭은 서로 다른 말이라야 한다 — 같은 말이면 사람은 왜 안 되는지 여전히 모른다.
	TestNotEqual(TEXT("사거리 밖과 도구는 다른 말"), TooFarLine, NeedToolLine);
	TestNotEqual(TEXT("도구와 경합은 다른 말"), NeedToolLine, TakenLine);
	TestNotEqual(TEXT("사거리 밖과 경합은 다른 말"), TooFarLine, TakenLine);

	// ④ 되는 길은 말이 없다 — 실제로 베어지고 까닭 줄은 그대로다 (알림이 성공에도 뜨면 잔소리가 된다).
	AGatherableActor* const Ok = SpawnGatherable(World, NAME_None, TestClaimableGatherID,
		FVector(0.f, -TestNearDistanceCm, 0.f), TestRespawnSeconds);
	if (!TestNotNull(TEXT("[성공] 채집물"), Ok))
	{
		Rig.Teardown();
		return false;
	}
	Gather->Server_GatherTarget(Ok);
	TestFalse(TEXT("[성공] 실제로 벴다"), Ok->CanGather());
	TestEqual(TEXT("[성공] 새 까닭이 안 뜬다"), Gather->GetLastRejectMessage().ToString(), TakenLine);

	AddInfo(FString::Printf(TEXT("[ActionMatrix] 게스트 채집 거절 — 사거리 밖 「%s」 · 도구 「%s」 · 경합 「%s」 · 성공 → 까닭 없음"),
		*TooFarLine, *NeedToolLine, *TakenLine));

	Rig.Teardown();
	return true;
}

// ── M16-08 · 남의 폰에서도 도구 소리가 난다 ──
// 채집 소리는 전엔 누른 기기에서만 났다. 이제 누른 기기가 소리 **이름**을 호스트에 알리고(`Server_PlayActionSound`),
// 호스트가 나머지 기기에 뿌린다(`Multicast_PlayActionSound`). 게스트가 누르면 부탁이 1회 나가고,
// 호스트가 그 부탁을 받으면 중계가 1회 찍히며, 호스트 본인이 누르면 부탁과 중계가 각각 1회 늘어난다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenActionSoundRelayTest,
	"Snarehaven.ActionMatrix.Server.ToolSoundReachesOtherDevices",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenActionSoundRelayTest::RunTest(const FString& Parameters)
{
	FFakeServer Server;
	Server.Install();

	FActionRig Rig;
	if (!Rig.Setup(*this))
	{
		Rig.Teardown();
		return false;
	}
	ASnarehavenCharacter* const Player = Rig.Player;
	UGatherComponent* const Gather = Rig.Gather;

	AGatherableActor* const Tree = SpawnGatherable(Rig.World, HatchetID, TestClaimableGatherID,
		FVector(TestNearDistanceCm, 0.f, 0.f), TestRespawnSeconds);
	if (!TestNotNull(TEXT("나무"), Tree) || !TestTrue(TEXT("손도끼를 든다"), HoldTool(*Player, HatchetID)))
	{
		Rig.Teardown();
		return false;
	}
	Tree->SetReplicates(true);

	// 게스트: 제 기기에서 소리를 내고 호스트에 부탁을 1회 보낸다(중계는 호스트 몫이라 아직 0).
	Player->SetRole(ROLE_AutonomousProxy);
	TestTrue(TEXT("[게스트] 채집 부탁"), Gather->TryGatherNearest());
	TestEqual(TEXT("[게스트] 소리 부탁 1회"), Player->GetSoundRequestCount(), 1);
	TestEqual(TEXT("[게스트] 중계는 호스트 몫"), Player->GetRelayedSoundCount(), 0);

	// 호스트: 게스트의 부탁을 받으면 1회 중계하고 소리 이름을 그대로 뿌린다.
	Player->SetRole(ROLE_Authority);
	Player->Server_PlayActionSound_Implementation(TEXT("Chop"), FVector::ZeroVector, 0.8f);
	TestEqual(TEXT("[호스트] 중계 1회"), Player->GetRelayedSoundCount(), 1);
	TestEqual(TEXT("[호스트] 중계한 소리 이름"), Player->GetLastRelayedSoundCue().ToString(), FString(TEXT("Chop")));

	// 호스트 본인이 눌러도 같은 길 — 부탁과 중계가 각각 1회 늘어난다.
	Tree->SetReplicates(false);
	Player->PlayActionSound(TEXT("Chop"), FVector::ZeroVector, 0.8f);
	TestEqual(TEXT("[호스트] 소리 부탁 2회째"), Player->GetSoundRequestCount(), 2);
	TestEqual(TEXT("[호스트] 중계 2회째"), Player->GetRelayedSoundCount(), 2);

	// 소리 이름이 없으면 아무것도 안 나간다.
	Player->PlayActionSound(NAME_None, FVector::ZeroVector, 0.8f);
	TestEqual(TEXT("이름 없는 소리는 안 나간다"), Player->GetSoundRequestCount(), 2);

	AddInfo(FString::Printf(TEXT("[ActionMatrix] 소리 중계 — 게스트 부탁 1 · 호스트 중계 %d · 마지막 「%s」"),
		Player->GetRelayedSoundCount(), *Player->GetLastRelayedSoundCue().ToString()));

	Rig.Teardown();
	return true;
}

// ── M15-16 (INBOX ★★★★ ④) — 제작 중복 요청에 「만드는 중」 한 줄 ──
// 제작·요리·설치는 한 번에 한 요청만 나간다(`bRequestInFlight`). 전에는 응답을 기다리는 동안 또 누르면
// **아무 일도 안 난 것처럼** 조용히 false 로 돌아서서, 누른 사람은 버튼이 죽은 줄 알고 계속 눌렀다.
// 여기서 보는 것: ① 둘째 누름은 **서버로 안 간다**(중복 차감을 만들지 않는다, CLAUDE.md 8)
// ② 그때 **안내 한 줄이 실제로 화면 쪽으로 나간다**(오류 빨강이 아니라 안내) ③ **요리 타이머의 자동
// 재시도는 말이 없다** — 사람이 누른 것이 아니다 ④ 같은 불에 또 올리면 그 까닭도 한 줄
// ⑤ 응답이 오면 같은 버튼이 다시 눌린다(안내가 버튼을 영영 막지 않는다).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenActionCraftBusyTest,
	"Snarehaven.ActionMatrix.Server.CraftBusyTellsWhy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenActionCraftBusyTest::RunTest(const FString& Parameters)
{
	FFakeServer Server;
	Server.bHold = true;   // 답을 쥐고 있는다 — "응답 전에 또 누름" 을 볼 수 있게
	Server.Install();

	UGameInstance* const GameInstance = NewObject<UGameInstance>(GEngine);
	GameInstance->InitializeStandalone();
	UWorld* const World = GameInstance->GetWorld();
	USnareBaseSubsystem* const Base = GameInstance->GetSubsystem<USnareBaseSubsystem>();
	if (!TestNotNull(TEXT("월드"), World) || !TestNotNull(TEXT("기지 창구"), Base))
	{
		return false;
	}
	World->InitializeActorsForPlay(FURL());

	// 화면(제작 창 토스트)이 듣는 자리 그대로 — "안내를 만들었다" 가 아니라 "실제로 나갔다" 를 본다.
	FString HeardLine;
	bool bHeardError = true;
	int32 HeardCount = 0;
	Base->OnBaseMessage.AddLambda([&HeardLine, &bHeardError, &HeardCount](const FText& Message, bool bError)
	{
		HeardLine = Message.ToString();
		bHeardError = bError;
		++HeardCount;
	});

	const UDataTable* const RecipeTable = LoadObject<UDataTable>(nullptr, RecipeTablePath);
	TestNotNull(TEXT("DT_RecipeTable"), RecipeTable);
	const FName Result = FindRecipeResult(RecipeTable, ERecipeStation::Workbench);
	if (!TestFalse(TEXT("표에 작업대 레시피가 있다"), Result.IsNone()))
	{
		GameInstance->Shutdown();
		return false;
	}

	// ── ① 첫 누름은 나가고, 아직 할 말은 없다 ──
	TestTrue(TEXT("[제작] 첫 누름이 서버로 간다"), Base->RequestCraft(Result));
	TestEqual(TEXT("[제작] 서버를 한 번 부른다"), Server.Calls, 1);
	TestEqual(TEXT("[제작] 첫 누름엔 안내가 없다"), HeardCount, 0);

	// ── ② 응답 전 둘째 누름 — 서버로 안 가고, 까닭 한 줄이 나간다 ──
	TestFalse(TEXT("[제작 중복] 둘째 누름은 물린다"), Base->RequestCraft(Result));
	TestEqual(TEXT("[제작 중복] 서버를 더 부르지 않는다"), Server.Calls, 1);
	TestEqual(TEXT("[제작 중복] 안내가 한 번 나간다"), HeardCount, 1);
	const FString BusyLine = HeardLine;
	TestEqual(TEXT("[제작 중복] 화면에 닿은 글"), BusyLine, FString(TEXT("만드는 중입니다")));
	TestEqual(TEXT("[제작 중복] 마지막 안내도 같은 글"), Base->GetLastMessage().ToString(), BusyLine);
	TestFalse(TEXT("[제작 중복] 오류가 아니라 안내다"), bHeardError);

	// ── ③ 요리 타이머의 자동 재시도는 말이 없다 (누르지도 않은 「만드는 중입니다」가 뜨면 안 된다) ──
	const FName Cooked = FindRecipeResult(RecipeTable, ERecipeStation::Fire);
	const TCHAR* const FireKey = TEXT("fire_craft_busy");
	TestTrue(TEXT("[요리] 불에 올린다"), Base->StartCooking(FireKey, CampfireID, Cooked));
	const FString CookLine = HeardLine;   // "… 굽는 중 (N초)"
	const int32 CountBeforeRetry = HeardCount;
	Base->CompleteCooking(FireKey);       // 앞 제작이 아직 날아가 있다 → 조용히 나중에 다시
	TestEqual(TEXT("[요리 재시도] 서버를 더 부르지 않는다"), Server.Calls, 1);
	TestEqual(TEXT("[요리 재시도] 안내가 새로 안 나간다"), HeardCount, CountBeforeRetry);
	TestEqual(TEXT("[요리 재시도] 화면 글이 그대로다"), HeardLine, CookLine);

	// ── ④ 같은 불에 또 올리면 그 까닭도 한 줄 ──
	TestFalse(TEXT("[요리 중복] 같은 불에 또 못 올린다"), Base->StartCooking(FireKey, CampfireID, Cooked));
	const FString CookBusyLine = HeardLine;
	TestEqual(TEXT("[요리 중복] 화면에 닿은 글"), CookBusyLine, FString(TEXT("이 불은 이미 굽는 중입니다")));
	TestNotEqual(TEXT("[요리 중복] 제작 중복과 다른 말"), CookBusyLine, BusyLine);

	// ── ⑤ 응답이 오면 같은 버튼이 다시 눌린다 ──
	Server.Flush();
	TestTrue(TEXT("[제작] 응답 뒤 다시 눌린다"), Base->RequestCraft(Result));
	TestEqual(TEXT("[제작] 이번엔 서버로 간다"), Server.Calls, 2);
	TestNotEqual(TEXT("[제작] 다시 눌렸을 땐 「만드는 중」이 아니다"), HeardLine, BusyLine);

	AddInfo(FString::Printf(TEXT("[ActionMatrix] 중복 요청 — 제작 「%s」 · 요리 「%s」 · 자동 재시도 말 없음 · 응답 뒤 서버 호출 %d회"),
		*BusyLine, *CookBusyLine, Server.Calls));

	GameInstance->Shutdown();
	return true;
}

// ── M28-02 — 제작·요리·제련 성공에 소리 + 몸짓 ──
// 제작 판은 성공 글만 있고 소리·몸 동작이 없었다. 서버가 `CraftItem` 을 확정한 뒤에만 그 자리 소리 한 번
// (`SnareCraftRules::ConfirmCue`) + 줍기 몸짓 한 번. ① 누름만으로는 0 ② 확정 뒤 각 1 ③ 요리·제련 타이머 끝도 같은 길,
// 자리마다 소리 이름이 다르다 ④ 거절 답이면 0.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenActionCraftConfirmFeedbackTest,
	"Snarehaven.ActionMatrix.Server.CraftConfirmSoundAndGesture",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenActionCraftConfirmFeedbackTest::RunTest(const FString& Parameters)
{
	FFakeServer Server;
	Server.bHold = true;   // 답을 쥐고 있는다 — "확정 전" 을 볼 수 있게
	Server.Install();

	FActionRig Rig;
	if (!Rig.Setup(*this))
	{
		Rig.Teardown();
		return false;
	}
	ASnarehavenCharacter* const Player = Rig.Player;
	USnareBaseSubsystem* const Base = Rig.Base;

	const UDataTable* const RecipeTable = LoadObject<UDataTable>(nullptr, RecipeTablePath);
	const FName Crafted = FindRecipeResult(RecipeTable, ERecipeStation::Workbench);
	const FName Smelted = FindRecipeResult(RecipeTable, ERecipeStation::Smelter);
	const FName Cooked = FindRecipeResult(RecipeTable, ERecipeStation::Fire);
	const UDataTable* const ItemTable = Base->GetItemTable();
	const FItemRow* const SmelterRow = ItemTable != nullptr ? ItemTable->FindRow<FItemRow>(TEXT("Item_Smelter"), TEXT("CraftConfirm")) : nullptr;
	const FName SmelterID = SmelterRow != nullptr ? SmelterRow->ItemID : NAME_None;
	if (!TestFalse(TEXT("표에 작업대·제련소·불 레시피와 제련소 행이 있다"),
		Crafted.IsNone() || Smelted.IsNone() || Cooked.IsNone() || SmelterID.IsNone()))
	{
		Rig.Teardown();
		return false;
	}

	TArray<FString> Lines;
	/** 한 경우: `Press` 로 누르고, 확정 전·뒤 소리·몸짓 늘어난 수와 소리 이름을 본다. */
	const auto Case = [&](const TCHAR* Label, TFunctionRef<bool()> Press, int32 WantAfter, FName WantCue, float WantVolume = 0.f)
	{
		const int32 SoundBefore = Player->GetSoundRequestCount();
		const int32 ActionBefore = Player->GetItemActionCount();
		TestTrue(*FString::Printf(TEXT("[%s] 서버로 간다"), Label), Press());
		const int32 SoundPending = Player->GetSoundRequestCount() - SoundBefore;
		const int32 ActionPending = Player->GetItemActionCount() - ActionBefore;
		TestEqual(*FString::Printf(TEXT("[%s] 확정 전 소리 0"), Label), SoundPending, 0);
		TestEqual(*FString::Printf(TEXT("[%s] 확정 전 몸짓 0"), Label), ActionPending, 0);
		Server.Flush();
		const int32 SoundAfter = Player->GetSoundRequestCount() - SoundBefore;
		const int32 ActionAfter = Player->GetItemActionCount() - ActionBefore;
		TestEqual(*FString::Printf(TEXT("[%s] 답 뒤 소리"), Label), SoundAfter, WantAfter);
		TestEqual(*FString::Printf(TEXT("[%s] 답 뒤 몸짓"), Label), ActionAfter, WantAfter);
		if (WantAfter > 0)
		{
			TestEqual(*FString::Printf(TEXT("[%s] 몸짓 = 줍기"), Label), Player->GetLastItemAction(), EItemAction::Gather);
			TestEqual(*FString::Printf(TEXT("[%s] 소리 이름"), Label), Player->GetLastRelayedSoundCue(), WantCue);
			TestEqual(*FString::Printf(TEXT("[%s] 소리 크기"), Label), Player->GetLastRelayedSoundVolume(), WantVolume);
		}
		Lines.Add(FString::Printf(TEXT("%s 확정 전 소리 %d·몸짓 %d → 답 뒤 소리 %d·몸짓 %d (%s)"), Label,
			SoundPending, ActionPending, SoundAfter, ActionAfter, WantAfter > 0 ? *Player->GetLastRelayedSoundCue().ToString() : TEXT("-")));
	};

	Case(TEXT("작업대"), [&]() { return Base->RequestCraft(Crafted, 1, true, ESnareCraftPlace::Workbench); },
		1, SnareCraftRules::ConfirmCue(ESnareCraftPlace::Workbench), SnareCraftRules::ConfirmVolume(ESnareCraftPlace::Workbench));
	Case(TEXT("제련소"), [&]()
	{
		TestTrue(TEXT("[제련소] 올린다"), Base->StartCooking(TEXT("smelter_confirm"), SmelterID, Smelted));
		Base->CompleteCooking(TEXT("smelter_confirm"));
		return Base->FindCookJob(TEXT("smelter_confirm")) == nullptr;
	}, 1, SnareCraftRules::ConfirmCue(ESnareCraftPlace::Smelter), SnareCraftRules::ConfirmVolume(ESnareCraftPlace::Smelter));
	Case(TEXT("불"), [&]()
	{
		TestTrue(TEXT("[불] 올린다"), Base->StartCooking(TEXT("fire_confirm"), CampfireID, Cooked));
		Base->CompleteCooking(TEXT("fire_confirm"));
		return Base->FindCookJob(TEXT("fire_confirm")) == nullptr;
	}, 1, SnareCraftRules::ConfirmCue(ESnareCraftPlace::Fire), SnareCraftRules::ConfirmVolume(ESnareCraftPlace::Fire));
	Server.RejectFunction = TEXT("CraftItem");
	Case(TEXT("거절"), [&]() { return Base->RequestCraft(Crafted, 1, true, ESnareCraftPlace::Workbench); }, 0, NAME_None);

	TestEqual(TEXT("표 이름 — 망치"), SnareCraftRules::ConfirmCue(ESnareCraftPlace::Workbench), FName(TEXT("CraftHammer")));
	TestEqual(TEXT("표 이름 — 배낭도 망치"), SnareCraftRules::ConfirmCue(ESnareCraftPlace::Backpack), FName(TEXT("CraftHammer")));
	TestEqual(TEXT("표 이름 — 쉭"), SnareCraftRules::ConfirmCue(ESnareCraftPlace::Smelter), FName(TEXT("CraftSmelt")));
	TestEqual(TEXT("표 이름 — 지글"), SnareCraftRules::ConfirmCue(ESnareCraftPlace::Fire), FName(TEXT("CraftSizzle")));
	TestEqual(TEXT("표 크기 — 망치"), SnareCraftRules::ConfirmVolume(ESnareCraftPlace::Workbench), 0.8f);
	TestEqual(TEXT("표 크기 — 제련"), SnareCraftRules::ConfirmVolume(ESnareCraftPlace::Smelter), 0.8f);
	TestEqual(TEXT("표 크기 — 지글"), SnareCraftRules::ConfirmVolume(ESnareCraftPlace::Fire), 0.6f);
	TestNotEqual(TEXT("자리마다 소리가 다르다(작업대·제련소)"),
		SnareCraftRules::ConfirmCue(ESnareCraftPlace::Workbench), SnareCraftRules::ConfirmCue(ESnareCraftPlace::Smelter));
	AddInfo(FString::Printf(TEXT("[CraftConfirm] %s"), *FString::Join(Lines, TEXT(" / "))));

	Rig.Teardown();
	return true;
}

// ── M28-03 — 집 짓기·설치물 놓기 확정 소리 ──
// 놓기는 줍기 몸짓만 있고 소리가 없었다. 서버가 물건을 세운 뒤에만 **선 자리에서** 「쿵」 한 번
// (`SnareBuildRules::PlaceConfirmCue`), 텐트 → 집 올리기도 같은 길. ① 요청만으로는 0 ② 확정 뒤 1, 자리 = 선 물건
// ③ 소리는 호스트 중계를 타서 같은 방 사람도 그 자리에서 듣는다 ④ 거절이면 0.
// M45-02 — 이미 선 물건을 바꾸는 칠하기·철거는 「쿵」 대신 제 소리(`EditConfirmCue`) 한 번, 철거는 지우기 전 그 물건 자리.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenActionBuildConfirmSoundTest,
	"Snarehaven.ActionMatrix.Server.BuildConfirmSound",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenActionBuildConfirmSoundTest::RunTest(const FString& Parameters)
{
	FFakeServer Server;
	Server.bHold = true;   // 답을 쥐고 있는다 — "확정 전" 을 볼 수 있게
	Server.Install();

	FActionRig Rig;
	if (!Rig.Setup(*this))
	{
		Rig.Teardown();
		return false;
	}
	ASnarehavenCharacter* const Player = Rig.Player;
	USnareBaseSubsystem* const Base = Rig.Base;
	Server.TentZone = USnareBaseBuildComponent::GetTentZone(Rig.World).ToString();
	const FName PlaceCue = SnareBuildRules::PlaceConfirmCue();
	// 틀의 건축 컴포넌트는 조종이 붙기 전에 깨어 「서버가 세웠다 → 화면에 세우기」 를 안 들었다 (내 몸일 때만 듣는다).
	// 소리 자리를 선 물건과 견주려면 세워야 하므로, 조종이 붙은 지금 새로 깨운 것으로 바꾼다(옛것은 확정 소리를 같이 내지 않게 치운다).
	Rig.Build->DestroyComponent();
	USnareBaseBuildComponent* const Build = NewObject<USnareBaseBuildComponent>(Player, TEXT("BuildConfirmBuild"));
	Build->RegisterComponent();

	/** 서버가 세운 물건(미리보기 아님) — 소리 자리와 견준다. */
	const auto FindPlaced = [&](const FString& StructureId) -> const ASnarePlacedStructureActor*
	{
		for (TActorIterator<ASnarePlacedStructureActor> It(Rig.World); It; ++It)
		{
			if (!It->IsPreview() && It->GetTentId() == TestTentId && It->GetStructureId() == StructureId)
			{
				return *It;
			}
		}
		return nullptr;
	};

	TArray<FString> Lines;
	/**
	 * 한 경우: `Press` 로 누르고, 확정 전·뒤 늘어난 소리 수와 이름·자리를 본다. 자리는 답 뒤 선 물건 —
	 * 철거처럼 답 뒤 사라지면 누르기 전에 서 있던 자리.
	 */
	const auto Case = [&](const TCHAR* Label, TFunctionRef<bool()> Press, int32 WantAfter, const FString* StructureId,
		FName Cue)
	{
		const ASnarePlacedStructureActor* const Before = StructureId != nullptr ? FindPlaced(*StructureId) : nullptr;
		const bool bStoodBefore = Before != nullptr;
		const FVector BeforeLocation = bStoodBefore ? Before->GetActorLocation() : FVector::ZeroVector;
		const int32 RequestBefore = Player->GetSoundRequestCount();
		const int32 RelayBefore = Player->GetRelayedSoundCount();
		TestTrue(*FString::Printf(TEXT("[%s] 서버로 간다"), Label), Press());
		const int32 Pending = Player->GetSoundRequestCount() - RequestBefore;
		TestEqual(*FString::Printf(TEXT("[%s] 확정 전 소리 0"), Label), Pending, 0);
		Server.Flush();
		const int32 After = Player->GetSoundRequestCount() - RequestBefore;
		const int32 Relayed = Player->GetRelayedSoundCount() - RelayBefore;
		TestEqual(*FString::Printf(TEXT("[%s] 답 뒤 소리"), Label), After, WantAfter);
		TestEqual(*FString::Printf(TEXT("[%s] 같은 방 중계"), Label), Relayed, WantAfter);
		float Gap = -1.f;
		if (WantAfter > 0)
		{
			TestEqual(*FString::Printf(TEXT("[%s] 소리 이름"), Label), Player->GetLastRelayedSoundCue(), Cue);
			const ASnarePlacedStructureActor* const Placed = StructureId != nullptr ? FindPlaced(*StructureId) : nullptr;
			if (TestTrue(*FString::Printf(TEXT("[%s] 선 물건(또는 누르기 전에 서 있던 것)"), Label), Placed != nullptr || bStoodBefore))
			{
				const FVector Want = Placed != nullptr ? Placed->GetActorLocation() : BeforeLocation;
				Gap = FVector::Dist(Build->GetLastBuildSoundLocation(), Want);
				TestTrue(*FString::Printf(TEXT("[%s] 소리 자리 = 선 물건 자리 (%.2fcm)"), Label, Gap), Gap < 1.f);
			}
		}
		Lines.Add(FString::Printf(TEXT("%s 확정 전 %d → 답 뒤 %d·중계 %d (%s, 자리 차 %.2fcm)"), Label, Pending, After, Relayed,
			WantAfter > 0 ? *Player->GetLastRelayedSoundCue().ToString() : TEXT("-"), Gap));
	};

	const FString TentKey;   // 텐트·집 액터는 구조물 ID 가 비어 있다
	const FString StructureKey(TestStructureId);
	Case(TEXT("텐트"), [&]() { return Build->BeginPlacing(TentID) && Build->ConfirmPlacement(FString()); }, 1, &TentKey, PlaceCue);
	TestEqual(TEXT("[텐트] 답 뒤 사본 1채"), Base->GetTents().Num(), 1);

	// 가구는 텐트 땅 안 빈 칸이라야 한다 — 텐트 몸과 겹치지 않을 때까지 한 칸씩 앞으로.
	const FBaseRuleRow* const Rules = Base->GetRules();
	const float CellSizeCm = Rules != nullptr ? Rules->CellSizeCm : 0.f;
	Case(TEXT("가구"), [&]()
	{
		if (CellSizeCm <= 0.f || !Build->BeginPlacing(CampfireID))
		{
			return false;
		}
		for (int32 Step = 0; Step < TestMaxPlaceSteps; ++Step)
		{
			Player->SetActorLocation(Player->GetActorLocation() + Player->GetActorForwardVector() * CellSizeCm);
			if (Build->ConfirmPlacement(FString()))
			{
				return true;
			}
		}
		return false;
	}, 1, &StructureKey, PlaceCue);
	Case(TEXT("집 올리기"), [&]() { return Base->RequestUpgradeHouse(TestTentId, 0, 1); }, 1, &TentKey, PlaceCue);
	const FName PaintCue = SnareBuildRules::EditConfirmCue(ESnareStructureEdit::Paint);
	const FName RemoveCue = SnareBuildRules::EditConfirmCue(ESnareStructureEdit::Remove);
	TestNotEqual(TEXT("칠하기·철거 소리는 「쿵」 과 다르다"), PaintCue, PlaceCue);
	TestNotEqual(TEXT("철거 소리는 「쿵」 과 다르다"), RemoveCue, PlaceCue);
	Case(TEXT("칠하기"), [&]() { return Base->RequestPaintStructure(TestTentId, StructureKey, TentID); }, 1, &StructureKey, PaintCue);
	Server.RejectFunction = TEXT("PaintStructure");
	Case(TEXT("칠하기 거절"), [&]() { return Base->RequestPaintStructure(TestTentId, StructureKey, TentID); }, 0, nullptr, NAME_None);
	Server.RejectFunction = TEXT("RemoveStructure");
	Case(TEXT("철거 거절"), [&]() { return Base->RequestRemoveStructure(TestTentId, StructureKey); }, 0, nullptr, NAME_None);
	TestNotNull(TEXT("[철거 거절] 물건 그대로"), FindPlaced(StructureKey));
	Server.RejectFunction = TEXT("UpgradeHouse");
	Case(TEXT("거절"), [&]() { return Base->RequestUpgradeHouse(TestTentId, 0, 2); }, 0, nullptr, NAME_None);
	Server.RejectFunction.Reset();
	Case(TEXT("철거"), [&]() { return Base->RequestRemoveStructure(TestTentId, StructureKey); }, 1, &StructureKey, RemoveCue);
	TestNull(TEXT("[철거] 답 뒤 물건 사라짐"), FindPlaced(StructureKey));

	AddInfo(FString::Printf(TEXT("[BuildConfirm] %s"), *FString::Join(Lines, TEXT(" / "))));

	Rig.Teardown();
	return true;
}

// ── M45-03 — 보관함 넣기·빼기(집·배)·유지 나무 넣기 확정 소리 ──
// 판 숫자만 바뀌어 됐는지 몰랐다. 서버가 받아 준 뒤에만 내 귀에 나무 뚜껑 「덜컥」 한 번(`StorageConfirmCue`),
// ① 요청만으로는 0 ② 답 뒤 1 ③ 거절이면 0. 판 안 일이라 같은 방 중계는 없다(DESIGN 2.2 「거점·상자 소리」).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenActionStorageConfirmSoundTest,
	"Snarehaven.ActionMatrix.Server.StorageConfirmSound",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenActionStorageConfirmSoundTest::RunTest(const FString& Parameters)
{
	FFakeServer Server;
	Server.bHold = true;   // 답을 쥐고 있는다 — "확정 전" 을 볼 수 있게
	Server.Install();

	FActionRig Rig;
	if (!Rig.Setup(*this))
	{
		Rig.Teardown();
		return false;
	}
	ASnarehavenCharacter* const Player = Rig.Player;
	USnareBaseSubsystem* const Base = Rig.Base;
	const FName Cue = SnareBuildRules::StorageConfirmCue();
	TestNotEqual(TEXT("보관함 소리는 설치 「쿵」 과 다르다"), Cue, SnareBuildRules::PlaceConfirmCue());

	TArray<FString> Lines;
	/** 한 경우: `Press` 로 누르고, 확정 전·뒤 늘어난 보관함 소리 수·이름과 같은 방 중계 수를 본다. */
	const auto Case = [&](const TCHAR* Label, TFunctionRef<bool()> Press, int32 WantAfter)
	{
		const int32 Before = Base->GetStorageSoundCount();
		const int32 RelayBefore = Player->GetRelayedSoundCount();
		TestTrue(*FString::Printf(TEXT("[%s] 서버로 간다"), Label), Press());
		const int32 Pending = Base->GetStorageSoundCount() - Before;
		TestEqual(*FString::Printf(TEXT("[%s] 확정 전 소리 0"), Label), Pending, 0);
		Server.Flush();
		const int32 After = Base->GetStorageSoundCount() - Before;
		const int32 Relayed = Player->GetRelayedSoundCount() - RelayBefore;
		TestEqual(*FString::Printf(TEXT("[%s] 답 뒤 소리"), Label), After, WantAfter);
		TestEqual(*FString::Printf(TEXT("[%s] 같은 방 중계 0 (내 귀만)"), Label), Relayed, 0);
		if (WantAfter > 0)
		{
			TestEqual(*FString::Printf(TEXT("[%s] 소리 이름"), Label), Base->GetLastStorageSoundCue(), Cue);
		}
		Lines.Add(FString::Printf(TEXT("%s 확정 전 %d → 답 뒤 %d·중계 %d (%s)"), Label, Pending, After, Relayed,
			WantAfter > 0 ? *Base->GetLastStorageSoundCue().ToString() : TEXT("-")));
	};

	const FString StructureKey(TestStructureId);
	Case(TEXT("보관함 넣기"), [&]() { return Base->RequestMoveToStorage(TestTentId, StructureKey, BerryID, 1, FString(), FString()); }, 1);
	Case(TEXT("보관함 빼기"), [&]() { return Base->RequestMoveFromStorage(TestTentId, StructureKey, BerryID, 1, FString(), FString()); }, 1);
	Case(TEXT("배 넣기"), [&]() { return Base->RequestMoveToBoatStorage(BerryID, 1); }, 1);
	Case(TEXT("배 빼기"), [&]() { return Base->RequestMoveFromBoatStorage(BerryID, 1); }, 1);
	Case(TEXT("유지 나무"), [&]() { return Base->RequestFeedUpkeep(TestTentId, 1); }, 1);
	Server.RejectFunction = TEXT("MoveToStorage");
	Case(TEXT("넣기 거절"), [&]() { return Base->RequestMoveToStorage(TestTentId, StructureKey, BerryID, 1, FString(), FString()); }, 0);
	Case(TEXT("배 넣기 거절"), [&]() { return Base->RequestMoveToBoatStorage(BerryID, 1); }, 0);
	Server.RejectFunction = TEXT("MoveFromStorage");
	Case(TEXT("빼기 거절"), [&]() { return Base->RequestMoveFromStorage(TestTentId, StructureKey, BerryID, 1, FString(), FString()); }, 0);
	Server.RejectFunction = TEXT("FeedUpkeep");
	Case(TEXT("유지 거절"), [&]() { return Base->RequestFeedUpkeep(TestTentId, 1); }, 0);
	Server.RejectFunction.Reset();

	AddInfo(FString::Printf(TEXT("[StorageConfirm] %s"), *FString::Join(Lines, TEXT(" / "))));

	Rig.Teardown();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
