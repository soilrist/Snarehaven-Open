// ROADMAP M39-03 2층 검사 — 사냥 총 쏘기 판정(호스트). 탄창 0 이면 안 나가는지, 피해가 총알 도착 시각(거리 ÷ 속도)에
// 들어가는지, 사람은 맞아도 체력이 그대로이고 총알이 지나가는지, 사거리 끝 피해가 감쇠 끝값인지, 뒤쪽 동물은 안 쏘는지.
// 총 숫자(피해·사거리·속도·감쇠)는 시험에 적지 않고 **손에 든 권총 행**에서 읽는다 — 8.6-c 표를 바꿔도 그대로 통과한다.
// M39-04 재장전·탄 HUD 도 여기 — 가짜 `LoadAmmo` 답의 `loaded` 로만 탄창이 차는지, 게스트가 큰 수를 말해도 잘리는지, HUD `7/18`.
// M39-05 보이는 총알도 여기 — 총알이 거리 ÷ 속도 동안(±1장) 보이며 실제로 움직이는지, 튀김 종류가 맞은 것과 같은지.
// M39-06b 반동도 여기 — 한 발 나가면 몸 클립 신호(`SwingCount`·`IsSwinging`)가 쏘기 간격 한 바퀴만 켜지는지.
// M39-07 총알 시작점도 여기 — 보이는 총알이 손에 든 총 메시의 총구 끝에서 나오는지(몸 가운데 앞이 아니라).
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.Combat.Gun;Snarehaven.UI.GunHud; Quit" -unattended -nullrhi
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Animals/AnimalCharacter.h"
#include "Backend/SnareBackendSubsystem.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/CanvasPanelSlot.h"
#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "Inventory/InventoryComponent.h"
#include "UI/HuntHudWidget.h"
#include "Animals/AnimalStateComponent.h"
#include "Combat/MeleeAttackComponent.h"
#include "Combat/SnareGunTracerActor.h"
#include "Data/GatherableRow.h"
#include "Engine/HitResult.h"
#include "Gathering/GatherableActor.h"
#include "CoreGlobals.h"
#include "Data/AnimalRow.h"
#include "Data/ItemRow.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Equipment/EquipmentComponent.h"
#include "Player/PlayerVitalsComponent.h"
#include "Player/SnareHaptics.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "SnarehavenCharacter.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"

namespace SnareGunFireTest
{
	// 테스트 전용 값 (Verification.md 6 — 설계 숫자가 아니다).
	constexpr float TestMeleeDamage = 3.f;          // 근접 기본 — 총 행 피해와 일부러 다르다
	constexpr float TestMeleeRangeCm = 150.f;
	constexpr float TestMeleeIntervalSeconds = 1.f;
	constexpr float TestMaxHP = 1000.f;
	constexpr float TestCaptureThresholdPct = 1.f;
	constexpr float TestTimerStepSeconds = 0.005f;
	constexpr float TestTolerance = 0.01f;
	const FName PistolID(TEXT("item.pistol_semi"));
	const FName ClubID(TEXT("item.club"));
	const TCHAR* ItemTablePath = TEXT("/Game/Game/Data/Items/DT_ItemTable.DT_ItemTable");

	/**
	 * 권총 행(숫자는 실제 표 그대로) + 몽둥이 메시를 빌린 시험 전용 표. 권총 메시는 M39-06 이라 실제 행은 아직
	 * 손에 못 붙는다(`ApplyEquipVisual` 이 메시 없음으로 거절) — 판정 숫자는 그대로 두고 붙일 그림만 빌린다.
	 */
	UDataTable* MakeGunTestTable(FAutomationTestBase& Test)
	{
		const UDataTable* const Source = LoadObject<UDataTable>(nullptr, ItemTablePath);
		if (!Test.TestNotNull(TEXT("아이템 표"), Source))
		{
			return nullptr;
		}
		FName PistolRowName;
		const FItemRow* Pistol = nullptr;
		const FItemRow* Club = nullptr;
		for (const TPair<FName, uint8*>& Pair : Source->GetRowMap())
		{
			const FItemRow* const Row = reinterpret_cast<const FItemRow*>(Pair.Value);
			if (Row->ItemID == PistolID)
			{
				PistolRowName = Pair.Key;
				Pistol = Row;
			}
			else if (Row->ItemID == ClubID)
			{
				Club = Row;
			}
		}
		if (!Test.TestNotNull(TEXT("권총 행"), Pistol) || !Test.TestNotNull(TEXT("몽둥이 행"), Club))
		{
			return nullptr;
		}
		UDataTable* const Table = NewObject<UDataTable>(GetTransientPackage());
		Table->RowStruct = FItemRow::StaticStruct();
		FItemRow GunRow = *Pistol;
		GunRow.EquipMeshRef = Club->EquipMeshRef;
		Table->AddRow(PistolRowName, GunRow);
		return Table;
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

	// 타이머만 흘린다 — `GFrameCounter` 를 올려야 한 프레임 두 번째 틱이 그냥 돌아가지 않는다 (근접 시험과 같은 이유).
	void AdvanceTimers(UWorld* World, float Seconds)
	{
		const int32 Steps = FMath::RoundToInt(Seconds / TestTimerStepSeconds);
		for (int32 Step = 0; Step < Steps; ++Step)
		{
			++GFrameCounter;
			World->GetTimerManager().Tick(TestTimerStepSeconds);
		}
	}

	AAnimalCharacter* SpawnTestAnimal(UWorld* World, const FVector& Location)
	{
		AAnimalCharacter* const Animal = World->SpawnActor<AAnimalCharacter>(Location, FRotator::ZeroRotator);
		if (Animal != nullptr)
		{
			FAnimalRow Row;
			Row.AnimalID = TEXT("animal.test_dummy");
			Row.MaxHP = TestMaxHP;
			Row.CaptureThresholdPct = TestCaptureThresholdPct;
			Animal->GetAnimalState()->InitializeFromRow(Row);
		}
		return Animal;
	}

	/** 원점에서 +X 를 보고 권총을 든 사람. 실패하면 nullptr. */
	ASnarehavenCharacter* SpawnGunner(FAutomationTestBase& Test, UWorld* World, const FItemRow*& OutGun)
	{
		ASnarehavenCharacter* const Player = World->SpawnActor<ASnarehavenCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
		if (!Test.TestNotNull(TEXT("사람 스폰"), Player) || !Test.TestTrue(TEXT("사람 메시를 올렸다"), Player->ApplyPlayerBody()))
		{
			return nullptr;
		}
		UDataTable* const GunTable = MakeGunTestTable(Test);
		FObjectProperty* const TableProperty = FindFProperty<FObjectProperty>(UEquipmentComponent::StaticClass(), TEXT("ItemTable"));
		if (GunTable == nullptr || !Test.TestNotNull(TEXT("장비 표 칸"), TableProperty))
		{
			return nullptr;
		}
		TableProperty->SetObjectPropertyValue_InContainer(Player->GetEquipment(), GunTable);
		Player->GetMeleeAttack()->SetAttackSettings(TestMeleeDamage, TestMeleeRangeCm, TestMeleeIntervalSeconds);
		Player->GetMeleeAttack()->SetAttackHitDelaySeconds(0.f);
		if (!Test.TestTrue(TEXT("권총을 들었다"), Player->GetEquipment()->EquipItemByItemID(PistolID)))
		{
			return nullptr;
		}
		OutGun = Player->GetEquipment()->GetEquippedRow(EEquipSlot::Weapon);
		// 행이 비면 아래 비교가 「0 == 0」 으로 조용히 통과한다 — 먼저 막는다.
		if (!Test.TestNotNull(TEXT("든 무기 행"), OutGun) || !Test.TestTrue(TEXT("총 행이다(탄창·속도·피해·사거리)"),
			OutGun->MagazineSize > 0 && OutGun->ProjectileSpeedCmPerSecond > 0.f && OutGun->AttackDamage > 0.f && OutGun->AttackRangeCm > 0.f))
		{
			return nullptr;
		}
		return Player;
	}
}

using namespace SnareGunFireTest;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenGunEmptyMagazineTest, "Snarehaven.Combat.Gun.EmptyMagazineDoesNotFire",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenGunEmptyMagazineTest::RunTest(const FString& Parameters)
{
	UWorld* const World = CreateTestWorld();
	const FItemRow* Gun = nullptr;
	ASnarehavenCharacter* const Player = SpawnGunner(*this, World, Gun);
	AAnimalCharacter* const Animal = SpawnTestAnimal(World, FVector(300.f, 0.f, 0.f));
	if (Player == nullptr || !TestNotNull(TEXT("동물"), Animal))
	{
		DestroyTestWorld(World);
		return false;
	}
	UMeleeAttackComponent* const Melee = Player->GetMeleeAttack();
	TestTrue(TEXT("총을 들었다고 안다"), Melee->IsHoldingGun());
	TestEqual(TEXT("처음 탄창 0"), Melee->GetLoadedRounds(), 0);

	Melee->StartAttacking();
	Melee->StopAttacking();
	AdvanceTimers(World, 1.f);
	TestEqual(TEXT("나간 발 0"), Melee->GetShotCount(), 0);
	TestEqual(TEXT("빈 찰칵 1"), Melee->GetEmptyClickCount(), 1);
	TestEqual(TEXT("동물 체력 그대로"), Animal->GetAnimalState()->GetCurrentHP(), TestMaxHP, TestTolerance);
	TestEqual(TEXT("휘두르기도 안 했다(빈 총으로 근접 피해 없음)"), Melee->GetSwingCount(), 0);

	// 탄창 수는 0..탄창으로 잘린다 — 재장전(M39-04)·게스트 알림이 넘는 수를 말해도.
	TestEqual(TEXT("탄창보다 큰 수는 탄창으로"), Melee->SetLoadedRounds(Gun->MagazineSize + 92), Gun->MagazineSize);
	TestEqual(TEXT("음수는 0 으로"), Melee->SetLoadedRounds(-3), 0);

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenGunDamageOnArrivalTest, "Snarehaven.Combat.Gun.DamageLandsOnArrival",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenGunDamageOnArrivalTest::RunTest(const FString& Parameters)
{
	UWorld* const World = CreateTestWorld();
	const FItemRow* Gun = nullptr;
	ASnarehavenCharacter* const Player = SpawnGunner(*this, World, Gun);
	if (Player == nullptr)
	{
		DestroyTestWorld(World);
		return false;
	}
	// 감쇠 전 구간(사거리 × s 의 절반)에 둔다 — 피해 = 행 피해 그대로.
	const float DistanceCm = Gun->AttackRangeCm * Gun->FalloffStartRatio * 0.5f;
	AAnimalCharacter* const Animal = SpawnTestAnimal(World, FVector(DistanceCm, 0.f, 0.f));
	if (!TestNotNull(TEXT("동물"), Animal))
	{
		DestroyTestWorld(World);
		return false;
	}
	UMeleeAttackComponent* const Melee = Player->GetMeleeAttack();
	Melee->SetLoadedRounds(1);

	const float FlightSeconds = DistanceCm / Gun->ProjectileSpeedCmPerSecond;
	Melee->StartAttacking();   // 사람 입력과 같은 길
	Melee->StopAttacking();
	TestEqual(TEXT("한 발 나갔다"), Melee->GetShotCount(), 1);
	TestEqual(TEXT("탄창 1 → 0"), Melee->GetLoadedRounds(), 0);
	TestTrue(TEXT("맞을 것 = 동물"), Melee->GetLastShot().HitKind == ESnareGunHitKind::Animal);
	TestEqual(TEXT("도착 시각 = 거리 ÷ 속도"), Melee->GetLastShot().FlightSeconds, FlightSeconds, TestTolerance);
	TestEqual(TEXT("쏜 순간 체력 그대로"), Animal->GetAnimalState()->GetCurrentHP(), TestMaxHP, TestTolerance);

	AdvanceTimers(World, FlightSeconds * 0.5f);
	TestEqual(TEXT("도착 전(절반)에도 체력 그대로"), Animal->GetAnimalState()->GetCurrentHP(), TestMaxHP, TestTolerance);

	AdvanceTimers(World, FlightSeconds);
	TestEqual(TEXT("도착 뒤 행 피해만큼 깎였다"), Animal->GetAnimalState()->GetCurrentHP(), TestMaxHP - Gun->AttackDamage, TestTolerance);
	TestNotEqual(TEXT("근접 기본 피해가 아니라 총 행 피해"), Gun->AttackDamage, TestMeleeDamage);

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenGunPersonUnhurtTest, "Snarehaven.Combat.Gun.PersonTakesNoDamage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenGunPersonUnhurtTest::RunTest(const FString& Parameters)
{
	UWorld* const World = CreateTestWorld();
	const FItemRow* Gun = nullptr;
	ASnarehavenCharacter* const Player = SpawnGunner(*this, World, Gun);
	if (Player == nullptr)
	{
		DestroyTestWorld(World);
		return false;
	}
	// 사람을 총알 길 한가운데, 동물을 그 뒤에 — 사람은 맞음 판정에서 빠져 총알이 지나간다.
	const float AnimalCm = Gun->AttackRangeCm * Gun->FalloffStartRatio * 0.8f;
	ASnarehavenCharacter* const Bystander = World->SpawnActor<ASnarehavenCharacter>(FVector(AnimalCm * 0.5f, 0.f, 0.f), FRotator::ZeroRotator);
	AAnimalCharacter* const Animal = SpawnTestAnimal(World, FVector(AnimalCm, 0.f, 0.f));
	UPlayerVitalsComponent* const Vitals = Bystander != nullptr ? Bystander->FindComponentByClass<UPlayerVitalsComponent>() : nullptr;
	if (!TestNotNull(TEXT("곁 사람"), Bystander) || !TestNotNull(TEXT("곁 사람 생명"), Vitals) || !TestNotNull(TEXT("동물"), Animal))
	{
		DestroyTestWorld(World);
		return false;
	}
	const float HealthBefore = Vitals->GetHealth();
	UMeleeAttackComponent* const Melee = Player->GetMeleeAttack();
	Melee->SetLoadedRounds(Gun->MagazineSize);
	TestTrue(TEXT("쐈다"), Melee->FireOnce());
	TestTrue(TEXT("사람에 막히지 않고 동물까지"), Melee->GetLastShot().HitKind == ESnareGunHitKind::Animal);
	AdvanceTimers(World, Melee->GetLastShot().FlightSeconds + 0.1f);
	TestEqual(TEXT("사람 체력 그대로"), Vitals->GetHealth(), HealthBefore, TestTolerance);
	TestEqual(TEXT("뒤의 동물은 맞았다"), Animal->GetAnimalState()->GetCurrentHP(), TestMaxHP - Gun->AttackDamage, TestTolerance);

	// 동물 없이 사람만 앞에 있으면 — 대상 없음(빗나감), 사람 체력 그대로.
	Animal->Destroy();
	TestTrue(TEXT("또 쐈다"), Melee->FireOnce());
	TestTrue(TEXT("사람만 있으면 빗나감"), Melee->GetLastShot().HitKind == ESnareGunHitKind::Miss);
	AdvanceTimers(World, 1.f);
	TestEqual(TEXT("그래도 사람 체력 그대로"), Vitals->GetHealth(), HealthBefore, TestTolerance);

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenGunFalloffEndTest, "Snarehaven.Combat.Gun.FalloffAtRangeEnd",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenGunFalloffEndTest::RunTest(const FString& Parameters)
{
	UWorld* const World = CreateTestWorld();
	const FItemRow* Gun = nullptr;
	ASnarehavenCharacter* const Player = SpawnGunner(*this, World, Gun);
	if (Player == nullptr)
	{
		DestroyTestWorld(World);
		return false;
	}
	AAnimalCharacter* const Animal = SpawnTestAnimal(World, FVector(Gun->AttackRangeCm, 0.f, 0.f));
	if (!TestNotNull(TEXT("동물"), Animal))
	{
		DestroyTestWorld(World);
		return false;
	}
	UMeleeAttackComponent* const Melee = Player->GetMeleeAttack();
	Melee->SetLoadedRounds(1);
	TestTrue(TEXT("사거리 끝 동물을 쐈다"), Melee->FireOnce());
	AdvanceTimers(World, Melee->GetLastShot().FlightSeconds + 0.1f);
	const float Expected = Gun->AttackDamage * Gun->MinDamageRatio;
	AddInfo(FString::Printf(TEXT("사거리 끝 %.0fcm 피해 %.1f (행 %.1f × m %.2f)"),
		Gun->AttackRangeCm, TestMaxHP - Animal->GetAnimalState()->GetCurrentHP(), Gun->AttackDamage, Gun->MinDamageRatio));
	TestEqual(TEXT("끝거리 피해 = 피해 × m"), TestMaxHP - Animal->GetAnimalState()->GetCurrentHP(), Expected, TestTolerance);
	TestTrue(TEXT("감쇠가 실제로 걸렸다(m < 1)"), Gun->MinDamageRatio < 1.f);

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenGunConeTest, "Snarehaven.Combat.Gun.OnlyFrontCone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenGunConeTest::RunTest(const FString& Parameters)
{
	UWorld* const World = CreateTestWorld();
	const FItemRow* Gun = nullptr;
	ASnarehavenCharacter* const Player = SpawnGunner(*this, World, Gun);
	if (Player == nullptr)
	{
		DestroyTestWorld(World);
		return false;
	}
	// 뒤쪽 가까운 동물은 부채꼴 밖, 앞쪽 먼 동물이 대상이다.
	AAnimalCharacter* const Behind = SpawnTestAnimal(World, FVector(-200.f, 0.f, 0.f));
	AAnimalCharacter* const Front = SpawnTestAnimal(World, FVector(Gun->AttackRangeCm * 0.4f, 100.f, 0.f));
	if (!TestNotNull(TEXT("뒤 동물"), Behind) || !TestNotNull(TEXT("앞 동물"), Front))
	{
		DestroyTestWorld(World);
		return false;
	}
	UMeleeAttackComponent* const Melee = Player->GetMeleeAttack();
	Melee->SetLoadedRounds(1);
	TestTrue(TEXT("쐈다"), Melee->FireOnce());
	TestTrue(TEXT("대상 = 앞 동물"), Melee->GetLastShot().Target.Get() == Front);
	AdvanceTimers(World, Melee->GetLastShot().FlightSeconds + 0.1f);
	TestEqual(TEXT("뒤 동물 체력 그대로"), Behind->GetAnimalState()->GetCurrentHP(), TestMaxHP, TestTolerance);
	TestTrue(TEXT("앞 동물은 깎였다"), Front->GetAnimalState()->GetCurrentHP() < TestMaxHP);

	DestroyTestWorld(World);
	return true;
}

namespace SnareGunFireTest
{
	constexpr int32 TestBagAmmo = 18;   // 가방 탄약 — 상점 묶음(25)과 일부러 다르다

	/** 가짜 `LoadAmmo` — 답을 쥐고 있다가 `Reply` 로 돌려준다(응답 전 상태를 보려고). */
	struct FFakeLoadAmmo
	{
		int32 Calls = 0;
		FString LastFunction;
		TSharedPtr<FJsonObject> LastArgs;
		USnareBackendSubsystem::FServerCallDone Held;

		FFakeLoadAmmo()
		{
			USnareBackendSubsystem::TestServerOverride =
				[this](const FString& Name, const TSharedPtr<FJsonObject>& Args, USnareBackendSubsystem::FServerCallDone Done)
				{
					++Calls;
					LastFunction = Name;
					LastArgs = Args;
					Held = MoveTemp(Done);
				};
		}
		~FFakeLoadAmmo() { USnareBackendSubsystem::TestServerOverride = nullptr; }

		void Reply(int32 Loaded)
		{
			const TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
			Response->SetNumberField(TEXT("loaded"), Loaded);
			Finish(Response);
		}

		void Reject(const TCHAR* Code)
		{
			const TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
			Response->SetStringField(TEXT("error"), TEXT("rejected"));
			Response->SetStringField(TEXT("code"), Code);
			Finish(Response);
		}

		void Finish(const TSharedPtr<FJsonObject>& Response)
		{
			USnareBackendSubsystem::FServerCallDone Done = MoveTemp(Held);
			Held = nullptr;
			if (Done)
			{
				Done(true, Response, FString());
			}
		}
	};

	/** 백엔드 창구가 있는 월드(게임 인스턴스째) + 권총 든 사람 + 가방 탄약 `TestBagAmmo`. */
	struct FReloadRig
	{
		UGameInstance* GameInstance = nullptr;
		UWorld* World = nullptr;
		ASnarehavenCharacter* Player = nullptr;
		const FItemRow* Gun = nullptr;

		bool Setup(FAutomationTestBase& Test)
		{
			GameInstance = NewObject<UGameInstance>(GEngine);
			GameInstance->InitializeStandalone();
			World = GameInstance->GetWorld();
			if (!Test.TestNotNull(TEXT("월드"), World)
				|| !Test.TestNotNull(TEXT("백엔드 창구"), GameInstance->GetSubsystem<USnareBackendSubsystem>()))
			{
				return false;
			}
			World->InitializeActorsForPlay(FURL());
			Player = SpawnGunner(Test, World, Gun);
			if (Player == nullptr || !Test.TestNotNull(TEXT("인벤토리"), Player->GetInventory())
				|| !Test.TestFalse(TEXT("탄약 칸이 있다"), Gun->AmmoItemID.IsNone()))
			{
				return false;
			}
			// 가진 것을 바꾸는 길은 백엔드 스냅샷 하나다 (CLAUDE.md 8).
			TArray<FInventorySlot> Slots;
			FInventorySlot& Ammo = Slots.AddDefaulted_GetRef();
			Ammo.ItemID = Gun->AmmoItemID;
			Ammo.Count = TestBagAmmo;
			return Test.TestTrue(TEXT("가방 스냅샷"), Player->GetInventory()->ApplyBackendSnapshot(Slots));
		}

		~FReloadRig()
		{
			if (GameInstance != nullptr)
			{
				GameInstance->Shutdown();
			}
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenGunReloadWaitsTest, "Snarehaven.Combat.Gun.ReloadWaitsForServer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** M39-04 — 재장전은 서버 답의 `loaded` 로만 탄창을 채운다. 답 전엔 0 그대로, 쏘기·두 번째 재장전 무시. */
bool FSnarehavenGunReloadWaitsTest::RunTest(const FString& Parameters)
{
	FFakeLoadAmmo Server;
	FReloadRig Rig;
	if (!Rig.Setup(*this))
	{
		return false;
	}
	UMeleeAttackComponent* const Melee = Rig.Player->GetMeleeAttack();
	const int32 Magazine = Rig.Gun->MagazineSize;

	TestTrue(TEXT("재장전을 물었다"), Melee->RequestReload());
	TestEqual(TEXT("부른 함수"), Server.LastFunction, FString(TEXT("LoadAmmo")));
	FString GunArg;
	double HaveArg = -1.0;
	TestTrue(TEXT("인자 gun"), Server.LastArgs.IsValid() && Server.LastArgs->TryGetStringField(TEXT("gun"), GunArg));
	TestEqual(TEXT("gun = 든 총 ItemID"), GunArg, Rig.Gun->ItemID.ToString());
	TestTrue(TEXT("인자 have"), Server.LastArgs.IsValid() && Server.LastArgs->TryGetNumberField(TEXT("have"), HaveArg));
	TestEqual(TEXT("have = 지금 탄창 0"), HaveArg, 0.0);

	TestEqual(TEXT("응답 전 탄창 0"), Melee->GetLoadedRounds(), 0);
	TestTrue(TEXT("답을 기다린다"), Melee->IsReloadPending());
	TestFalse(TEXT("기다리는 동안 두 번째 재장전 무시"), Melee->RequestReload());
	TestEqual(TEXT("서버 호출 1번"), Server.Calls, 1);
	TestFalse(TEXT("기다리는 동안 쏘기 무시"), Melee->FireOnce());

	Server.Reply(Magazine);
	TestEqual(TEXT("응답 뒤 탄창 = loaded"), Melee->GetLoadedRounds(), Magazine);
	TestFalse(TEXT("기다림 끝"), Melee->IsReloadPending());
	TestFalse(TEXT("찬 탄창은 묻지 않는다"), Melee->RequestReload());
	TestEqual(TEXT("서버 호출 그대로 1번"), Server.Calls, 1);

	// 거절이면 탄창 그대로 (탄약 없음). 받아들인 재장전 동작(M39-06b2) 동안은 다시 못 물으니 지나간 뒤에.
	AdvanceTimers(Rig.World, Rig.Gun->ReloadSeconds * 1.5f);
	Melee->SetLoadedRounds(2);
	TestTrue(TEXT("다시 물었다"), Melee->RequestReload());
	Server.Reject(TEXT("not_enough_items"));
	TestEqual(TEXT("거절 뒤 탄창 그대로 2"), Melee->GetLoadedRounds(), 2);
	TestFalse(TEXT("거절 뒤 기다림 끝"), Melee->IsReloadPending());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenGunReloadMotionTest, "Snarehaven.Combat.Gun.ReloadMotionBlocksFire",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** M39-06b2 — 받아들여진 재장전은 행 `ReloadSeconds` 동안 동작이 돌고 그동안 쏘기를 막는다. 거절이면 동작 0. */
bool FSnarehavenGunReloadMotionTest::RunTest(const FString& Parameters)
{
	FFakeLoadAmmo Server;
	FReloadRig Rig;
	if (!Rig.Setup(*this))
	{
		return false;
	}
	UMeleeAttackComponent* const Melee = Rig.Player->GetMeleeAttack();
	const float ReloadSeconds = Rig.Gun->ReloadSeconds;
	if (!TestTrue(TEXT("행 재장전 초"), ReloadSeconds > 0.f) || !TestFalse(TEXT("행 재장전 클립"), Rig.Gun->ReloadAnim.IsNull()))
	{
		return false;
	}

	// 거절(탄약 없음)은 넣는 척하지 않는다.
	TestTrue(TEXT("물었다"), Melee->RequestReload());
	TestFalse(TEXT("답 전엔 동작 없음"), Melee->IsReloading());
	Server.Reject(TEXT("not_enough_items"));
	TestFalse(TEXT("거절 뒤 동작 없음"), Melee->IsReloading());
	TestEqual(TEXT("거절 = 동작 수 0"), Melee->GetReloadMotionCount(), 0);
	TestEqual(TEXT("거절 = 재장전 소리 0"), Melee->GetGunSoundCount(), 0);

	TestTrue(TEXT("다시 물었다"), Melee->RequestReload());
	Server.Reply(Rig.Gun->MagazineSize);
	TestTrue(TEXT("받아들여진 순간 동작 시작"), Melee->IsReloading());
	TestEqual(TEXT("동작 1번 (되감기 신호)"), Melee->GetReloadMotionCount(), 1);
	TestEqual(TEXT("받아들여진 순간 재장전 소리 (M39-06c)"), Melee->GetLastGunSound(), FName(UMeleeAttackComponent::GunReloadCue));
	TestEqual(TEXT("탄창은 응답 그대로 바로"), Melee->GetLoadedRounds(), Rig.Gun->MagazineSize);

	AdvanceTimers(Rig.World, ReloadSeconds * 0.9f);
	TestTrue(TEXT("재장전 초 90% 에도 도는 중"), Melee->IsReloading());
	TestFalse(TEXT("동작 중 쏘기 무시"), Melee->FireOnce());
	TestEqual(TEXT("동작 중 한 발도 안 나감"), Melee->GetShotCount(), 0);
	Melee->SetLoadedRounds(1);
	TestFalse(TEXT("동작 중 재장전 무시"), Melee->RequestReload());
	TestEqual(TEXT("서버 호출 2번 그대로"), Server.Calls, 2);

	AdvanceTimers(Rig.World, ReloadSeconds * 0.2f);
	TestFalse(TEXT("재장전 초 뒤 동작 끝"), Melee->IsReloading());
	TestTrue(TEXT("끝난 뒤 쏜다"), Melee->FireOnce());
	TestEqual(TEXT("한 발 나감"), Melee->GetShotCount(), 1);
	AddInfo(FString::Printf(TEXT("[GunReload] 재장전 %.2f초 · 동작 수 %d · 거절 동작 0 · 동작 중 쏘기 0 · 끝난 뒤 쏘기 %d"),
		ReloadSeconds, Melee->GetReloadMotionCount(), Melee->GetShotCount()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenGunReloadGuestClampTest, "Snarehaven.Combat.Gun.ReloadGuestClamp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** M39-04 — 게스트가 `Server_SetLoadedRounds` 로 탄창보다 큰 수(99)·음수를 말해도 호스트는 0..탄창으로 자른다. */
bool FSnarehavenGunReloadGuestClampTest::RunTest(const FString& Parameters)
{
	UWorld* const World = CreateTestWorld();
	const FItemRow* Gun = nullptr;
	ASnarehavenCharacter* const Player = SpawnGunner(*this, World, Gun);
	if (Player == nullptr)
	{
		DestroyTestWorld(World);
		return false;
	}
	UMeleeAttackComponent* const Melee = Player->GetMeleeAttack();
	Melee->Server_SetLoadedRounds(99);
	TestEqual(TEXT("게스트 99 → 탄창"), Melee->GetLoadedRounds(), Gun->MagazineSize);
	Melee->Server_SetLoadedRounds(-5);
	TestEqual(TEXT("게스트 -5 → 0"), Melee->GetLoadedRounds(), 0);
	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenGunHudAmmoTextTest, "Snarehaven.UI.GunHud.AmmoText",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** M39-04 — HUD 탄 글 `탄창/가방`(응답 전 `0/18` → 가짜 응답 → `7/18`) · 재장전 버튼 72 이상 · 탄약 없음 알약 · 총 내리면 접힘. */
bool FSnarehavenGunHudAmmoTextTest::RunTest(const FString& Parameters)
{
	FFakeLoadAmmo Server;
	FReloadRig Rig;
	if (!Rig.Setup(*this))
	{
		return false;
	}
	UHuntHudWidget* const Hud = CreateWidget<UHuntHudWidget>(Rig.World, UHuntHudWidget::StaticClass());
	if (!TestNotNull(TEXT("HUD 생성"), Hud))
	{
		return false;
	}
	Hud->TakeWidget();
	Hud->BindToCharacter(Rig.Player);
	const int32 Magazine = Rig.Gun->MagazineSize;
	const FString Full = FString::Printf(TEXT("%d/%d"), Magazine, TestBagAmmo);

	Hud->UpdateGunHud();
	TestTrue(TEXT("총을 들면 탄 HUD 가 보인다"), Hud->IsGunHudVisible());
	TestEqual(TEXT("응답 전 탄 글"), Hud->GetGunAmmoText().ToString(), FString::Printf(TEXT("0/%d"), TestBagAmmo));
	{
		// 탄 칸 왼쪽에 든 총의 탄 아이템 그림이 보인다 (표 `IconRef` — 표 안 탄 행은 전부 그림이 있다, IconMatchesItemID).
		const UImage* const AmmoIcon = Hud->WidgetTree->FindWidget<UImage>(TEXT("GunAmmoIcon"));
		if (TestNotNull(TEXT("탄 그림 위젯"), AmmoIcon))
		{
			TestTrue(TEXT("총을 들면 탄 그림이 보인다"), AmmoIcon->GetVisibility() != ESlateVisibility::Collapsed);
		}
	}

	const UButton* const Reload = Hud->WidgetTree->FindWidget<UButton>(TEXT("GunReloadButton"));
	const UCanvasPanelSlot* const ReloadSlot = Reload != nullptr ? Cast<UCanvasPanelSlot>(Reload->Slot) : nullptr;
	if (TestNotNull(TEXT("재장전 버튼 자리"), ReloadSlot))
	{
		const FVector2D Size = ReloadSlot->GetSize();
		AddInfo(FString::Printf(TEXT("[GunHud] 재장전 버튼 %.0f×%.0f"), Size.X, Size.Y));
		TestTrue(TEXT("재장전 버튼 짧은 변 72 이상 (M38)"), FMath::Min(Size.X, Size.Y) >= 72.f);
	}

	TestTrue(TEXT("버튼 길로 재장전을 물었다"), Hud->TryReloadGun());
	Hud->UpdateGunHud();
	TestEqual(TEXT("답 전 탄 글 그대로"), Hud->GetGunAmmoText().ToString(), FString::Printf(TEXT("0/%d"), TestBagAmmo));
	Server.Reply(Magazine);
	Hud->UpdateGunHud();
	TestEqual(TEXT("가짜 응답 뒤 탄 글"), Hud->GetGunAmmoText().ToString(), Full);
	AddInfo(FString::Printf(TEXT("[GunHud] 탄 글 %s"), *Hud->GetGunAmmoText().ToString()));

	AdvanceTimers(Rig.World, Rig.Gun->ReloadSeconds * 1.5f);   // 재장전 동작(M39-06b2)이 지나간 뒤
	Rig.Player->GetMeleeAttack()->SetLoadedRounds(1);
	TestTrue(TEXT("다시 물었다"), Hud->TryReloadGun());
	Server.Reject(TEXT("not_enough_items"));
	TestTrue(TEXT("탄약 없음 알약"), Hud->IsErrorBannerVisible());
	TestTrue(TEXT("알약 문구"), Hud->GetErrorBannerText().EqualTo(UHuntHudWidget::GetNoAmmoText()));

	Rig.Player->GetEquipment()->UnequipSlot(EEquipSlot::Weapon);
	Hud->UpdateGunHud();
	TestFalse(TEXT("총을 내리면 탄 HUD 접힘"), Hud->IsGunHudVisible());
	TestTrue(TEXT("탄 글 빈칸"), Hud->GetGunAmmoText().IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenGunTracerFlightTest, "Snarehaven.Combat.Gun.TracerFlightMatchesDistance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenGunTracerFlightTest::RunTest(const FString& Parameters)
{
	// 시험 전용 값 — 30fps 한 장씩 손으로 틱한다. 거리는 권총 행 사거리, 속도는 권총 행 속도(8.6-c 를 바꿔도 통과).
	constexpr float FrameSeconds = 1.f / 30.f;
	constexpr int32 MaxFrames = 300;
	UWorld* const World = CreateTestWorld();
	const FItemRow* Gun = nullptr;
	ASnarehavenCharacter* const Player = SpawnGunner(*this, World, Gun);
	if (Player == nullptr)
	{
		DestroyTestWorld(World);
		return false;
	}
	const FVector Muzzle(0.f, 0.f, 100.f);
	const FVector End = Muzzle + FVector(Gun->AttackRangeCm, 0.f, 0.f);
	const float FlightSeconds = Gun->AttackRangeCm / Gun->ProjectileSpeedCmPerSecond;
	ASnareGunTracerActor* const Tracer = World->SpawnActor<ASnareGunTracerActor>(Muzzle, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("총알 액터"), Tracer))
	{
		DestroyTestWorld(World);
		return false;
	}
	Tracer->Start(Muzzle, End, FlightSeconds, ESnareGunSplash::Animal);
	TestTrue(TEXT("첫 장: 총알 보임"), Tracer->IsBulletVisible());
	TestTrue(TEXT("첫 장: 총구 불꽃 보임"), Tracer->IsMuzzleFlashVisible());
	TestEqual(TEXT("꼬리 = 행 속도 × TailSeconds"), Tracer->GetTailLengthCm(),
		Gun->ProjectileSpeedCmPerSecond * ASnareGunTracerActor::TailSeconds, 0.5f);

	int32 VisibleFrames = 0;
	int32 FlashFrames = 0;
	bool bMovedForward = true;
	bool bSawMidway = false;
	float LastX = Muzzle.X;
	for (int32 Frame = 0; Frame < MaxFrames && IsValid(Tracer) && !Tracer->HasLanded(); ++Frame)
	{
		Tracer->Tick(FrameSeconds);
		FlashFrames += Tracer->IsMuzzleFlashVisible() ? 1 : 0;
		if (!Tracer->IsBulletVisible())
		{
			break;
		}
		++VisibleFrames;
		const float X = Tracer->GetBulletLocation().X;
		bMovedForward &= X > LastX;
		bSawMidway |= X > Muzzle.X + Gun->AttackRangeCm * 0.25f && X < End.X - Gun->AttackRangeCm * 0.25f;
		LastX = X;
	}
	const int32 ExpectedFrames = FMath::FloorToInt(FlightSeconds / FrameSeconds);
	AddInfo(FString::Printf(TEXT("[GunTracer] 거리 %.0fcm · 속도 %.0f · 수명 %.3f초 · 보인 장 %d (기대 %d ±1) · 꼬리 %.0fcm · 불꽃 장 %d"),
		Gun->AttackRangeCm, Gun->ProjectileSpeedCmPerSecond, FlightSeconds, VisibleFrames, ExpectedFrames,
		Tracer->GetTailLengthCm(), FlashFrames + 1));
	TestTrue(TEXT("총알 수명 = 거리 ÷ 속도 ±1장"), FMath::Abs(VisibleFrames - ExpectedFrames) <= 1);
	TestTrue(TEXT("순간 선이 아니다 — 2장 이상 보인다"), VisibleFrames >= 2);
	TestTrue(TEXT("매 장 앞으로 움직였다"), bMovedForward);
	TestTrue(TEXT("가운데쯤에서 보였다"), bSawMidway);
	TestTrue(TEXT("불꽃은 0.1초 = 30fps 3~4장 이내"), FlashFrames + 1 <= 4);
	TestTrue(TEXT("도착했다"), Tracer->HasLanded());
	TestFalse(TEXT("도착 뒤 총알 사라짐"), Tracer->IsBulletVisible());
	TestEqual(TEXT("도착 자리에 튀김 조각"), Tracer->GetSplashInstanceCount(), ASnareGunTracerActor::SplashCount);
	TestEqual(TEXT("동물이면 털빛 조각 몇 개"), Tracer->GetFurPieceCount(), ASnareGunTracerActor::FurPieceCount);
	TestTrue(TEXT("털빛 조각 < 전체(하얀 조각이 더 많다)"), Tracer->GetFurPieceCount() * 2 < Tracer->GetSplashInstanceCount());

	// M39-07b — 게임 카메라(1280×720, 한 화소 ≈ 1.9cm)에서 보이는 크기. 감독 기준 꼬리 굵기 ≥4px · 길이 ≥60px.
	constexpr float CmPerPixel = 1.9f;
	TestTrue(TEXT("꼬리 굵기 ≥ 4px"), ASnareGunTracerActor::GetTailThicknessCm() >= 4.f * CmPerPixel);
	TestTrue(TEXT("꼬리 길이 ≥ 60px"), Tracer->GetTailLengthCm() >= 60.f * CmPerPixel);
	TestTrue(TEXT("닿는 순간 퍽 보임"), Tracer->IsImpactPuffVisible());
	// M39-07c — 퍽은 동물 몸을 덮지 않는 크기(화면 35~45px)로, 매 장 옅어져 끝에 0. 조각은 굵게(≥ 8px).
	TestTrue(TEXT("튀김 조각 굵기 ≥ 8px"), ASnareGunTracerActor::GetSplashPieceSizeCm() >= 8.f * CmPerPixel);
	float PuffMinCm = Tracer->GetImpactPuffDiameterCm();
	float PuffMaxCm = PuffMinCm;
	float LastOpacity = Tracer->GetImpactPuffOpacity();
	bool bPuffFades = LastOpacity > 0.9f;
	int32 PuffFrames = 1;
	for (int32 Frame = 0; Frame < MaxFrames && IsValid(Tracer) && Tracer->IsImpactPuffVisible(); ++Frame)
	{
		Tracer->Tick(FrameSeconds);
		if (Tracer->IsImpactPuffVisible())
		{
			PuffMinCm = FMath::Min(PuffMinCm, Tracer->GetImpactPuffDiameterCm());
			PuffMaxCm = FMath::Max(PuffMaxCm, Tracer->GetImpactPuffDiameterCm());
			bPuffFades &= Tracer->GetImpactPuffOpacity() < LastOpacity;
			LastOpacity = Tracer->GetImpactPuffOpacity();
			++PuffFrames;
		}
	}
	AddInfo(FString::Printf(TEXT("[GunTracer] 꼬리 %.0fcm(≈%.0fpx) · 굵기 %.0fcm(≈%.1fpx) · 퍽 %.0f~%.0fcm(≈%.0f~%.0fpx) · 퍽 %d장 · 마지막 불투명도 %.2f · 조각 %d개 %.0fcm"),
		Tracer->GetTailLengthCm(), Tracer->GetTailLengthCm() / CmPerPixel, ASnareGunTracerActor::GetTailThicknessCm(),
		ASnareGunTracerActor::GetTailThicknessCm() / CmPerPixel, PuffMinCm, PuffMaxCm, PuffMinCm / CmPerPixel,
		PuffMaxCm / CmPerPixel, PuffFrames, LastOpacity, ASnareGunTracerActor::SplashCount, ASnareGunTracerActor::GetSplashPieceSizeCm()));
	TestTrue(TEXT("퍽 지름 35px 이상"), PuffMinCm >= 35.f * CmPerPixel);
	TestTrue(TEXT("퍽 지름 45px 이하 — 동물 몸을 덮지 않는다"), PuffMaxCm <= 45.f * CmPerPixel);
	TestTrue(TEXT("퍽은 매 장 옅어진다"), bPuffFades);
	TestFalse(TEXT("퍽이 꺼진 뒤 불투명도 0"), IsValid(Tracer) && Tracer->GetImpactPuffOpacity() > 0.f);
	TestTrue(TEXT("퍽은 ImpactPuffSeconds 안에 꺼진다(±1장)"),
		PuffFrames <= FMath::CeilToInt(ASnareGunTracerActor::ImpactPuffSeconds / FrameSeconds) + 1);

	// 튀김이 끝나면 스스로 사라진다.
	for (int32 Frame = 0; Frame < MaxFrames && IsValid(Tracer) && !Tracer->IsActorBeingDestroyed(); ++Frame)
	{
		Tracer->Tick(FrameSeconds);
	}
	TestTrue(TEXT("튀김 뒤 스스로 사라짐"), !IsValid(Tracer) || Tracer->IsActorBeingDestroyed());

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenGunSplashKindTest, "Snarehaven.Combat.Gun.SplashMatchesHit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenGunSplashKindTest::RunTest(const FString& Parameters)
{
	UWorld* const World = CreateTestWorld();
	const FItemRow* Gun = nullptr;
	ASnarehavenCharacter* const Player = SpawnGunner(*this, World, Gun);
	const float AnimalCm = Gun != nullptr ? Gun->AttackRangeCm * 0.5f : 0.f;
	AAnimalCharacter* const Animal = Player != nullptr ? SpawnTestAnimal(World, FVector(AnimalCm, 0.f, 0.f)) : nullptr;
	if (Player == nullptr || !TestNotNull(TEXT("동물"), Animal))
	{
		DestroyTestWorld(World);
		return false;
	}
	UMeleeAttackComponent* const Melee = Player->GetMeleeAttack();
	Melee->SetLoadedRounds(Gun->MagazineSize);

	// ① 동물 — 호스트가 쏘면 이 기기에도 총알이 뜨고, 튀김 = 하얀 별, 날아가는 시간 = 판정 도착 시각.
	TestTrue(TEXT("동물에 쐈다"), Melee->FireOnce());
	TestEqual(TEXT("보이는 총알 1"), Melee->GetShotVisualCount(), 1);
	ASnareGunTracerActor* const AnimalTracer = Melee->GetLastTracer();
	if (TestNotNull(TEXT("총알 액터"), AnimalTracer))
	{
		TestTrue(TEXT("동물 → 하얀 별"), AnimalTracer->GetSplash() == ESnareGunSplash::Animal);
		TestEqual(TEXT("총알 시간 = 피해 도착 시각"), AnimalTracer->GetFlightSeconds(), Melee->GetLastShot().FlightSeconds, TestTolerance);
		TestTrue(TEXT("하얀 별은 하양"), ASnareGunTracerActor::GetSplashColor(ESnareGunSplash::Animal).Equals(FLinearColor::White));
	}

	// ② 빗나감(앞에 아무것도 없음) — 흙먼지.
	Animal->Destroy();
	AdvanceTimers(World, 1.f);
	TestTrue(TEXT("허공에 쐈다"), Melee->FireOnce());
	TestEqual(TEXT("보이는 총알 2"), Melee->GetShotVisualCount(), 2);
	TestTrue(TEXT("빗나감 → 먼지"), Melee->GetLastTracer() != nullptr && Melee->GetLastTracer()->GetSplash() == ESnareGunSplash::Dust);

	// ③ 막힘 — 위를 보는 면 = 땅 먼지 · 서 있는 면 = 돌 · 손도끼 채집물 = 나무 · 곡괭이 채집물 = 돌.
	FHitResult Ground;
	Ground.ImpactNormal = FVector::UpVector;
	TestTrue(TEXT("땅 → 먼지"), ASnareGunTracerActor::ClassifyWorldHit(Ground) == ESnareGunSplash::Dust);
	FHitResult Wall;
	Wall.ImpactNormal = -FVector::ForwardVector;
	TestTrue(TEXT("바위 옆면 → 돌"), ASnareGunTracerActor::ClassifyWorldHit(Wall) == ESnareGunSplash::Stone);

	const FName HatchetID(TEXT("item.hatchet"));
	const FName PickaxeID(TEXT("item.pickaxe_basic"));
	AGatherableActor* const Tree = World->SpawnActor<AGatherableActor>(FVector(0.f, 500.f, 0.f), FRotator::ZeroRotator);
	AGatherableActor* const Ore = World->SpawnActor<AGatherableActor>(FVector(0.f, -500.f, 0.f), FRotator::ZeroRotator);
	if (TestNotNull(TEXT("나무"), Tree) && TestNotNull(TEXT("광맥"), Ore))
	{
		FGatherableRow TreeRow;
		TreeRow.GatherableID = TEXT("gather.test_tree");
		TreeRow.RequiredToolItemID = HatchetID;
		TreeRow.GrantItemID = TEXT("item.test_grant");   // 지급이 미정이면 행을 안 받는다 — 시험 전용 값
		TreeRow.GrantCount = 1;
		Tree->InitializeFromRow(TreeRow);
		FGatherableRow OreRow;
		OreRow.GatherableID = TEXT("gather.test_ore");
		OreRow.RequiredToolItemID = PickaxeID;
		OreRow.GrantItemID = TEXT("item.test_grant");
		OreRow.GrantCount = 1;
		Ore->InitializeFromRow(OreRow);
		// 도구가 안 들어갔으면 아래가 「막힘 = 나무」 로 조용히 통과한다 — 먼저 막는다.
		TestEqual(TEXT("나무 도구 = 손도끼"), Tree->GetRequiredToolItemID(), HatchetID);
		TestEqual(TEXT("광맥 도구 = 곡괭이"), Ore->GetRequiredToolItemID(), PickaxeID);
		FHitResult TreeHit(Tree, nullptr, FVector::ZeroVector, -FVector::ForwardVector);
		FHitResult OreHit(Ore, nullptr, FVector::ZeroVector, FVector::UpVector);
		TestTrue(TEXT("나무 → 나무 부스러기"), ASnareGunTracerActor::ClassifyWorldHit(TreeHit) == ESnareGunSplash::Wood);
		TestTrue(TEXT("광맥(위를 봐도) → 돌"), ASnareGunTracerActor::ClassifyWorldHit(OreHit) == ESnareGunSplash::Stone);
	}

	// ④ 네 색이 서로 다르다 — 튀김 종류가 화면에서 갈린다.
	TArray<FLinearColor> Colors;
	for (const ESnareGunSplash Kind : { ESnareGunSplash::Dust, ESnareGunSplash::Animal, ESnareGunSplash::Wood, ESnareGunSplash::Stone })
	{
		const FLinearColor Color = ASnareGunTracerActor::GetSplashColor(Kind);
		TestFalse(TEXT("튀김 색 겹치지 않음"), Colors.ContainsByPredicate([&Color](const FLinearColor& Other) { return Other.Equals(Color); }));
		Colors.Add(Color);
	}
	AddInfo(FString::Printf(TEXT("[GunTracer] 보이는 총알 %d발 · 동물=별 · 빗나감=먼지 · 나무=부스러기 · 광맥=돌"), Melee->GetShotVisualCount()));

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenGunShotRecoilTest, "Snarehaven.Combat.Gun.ShotPlaysRecoil",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenGunShotRecoilTest::RunTest(const FString& Parameters)
{
	// 한 발 = 반동 한 번. 캐릭터는 `IsSwinging` 동안 총 행의 쓰기 클립을 `클립 길이 ÷ 쏘기 간격` 으로 틀고
	// `SwingCount` 가 바뀌면 처음으로 되감는다 — 그 두 신호가 쏘기 간격(행 값)만큼 켜졌다 꺼지는지 본다.
	UWorld* const World = CreateTestWorld();
	const FItemRow* Gun = nullptr;
	ASnarehavenCharacter* const Player = SpawnGunner(*this, World, Gun);
	if (Player == nullptr)
	{
		DestroyTestWorld(World);
		return false;
	}
	UMeleeAttackComponent* const Melee = Player->GetMeleeAttack();
	TestFalse(TEXT("총 행에 반동 클립이 있다"), Gun->UseAnim.IsNull());
	Melee->SetLoadedRounds(2);

	Melee->StartAttacking();
	Melee->StopAttacking();
	TestEqual(TEXT("한 발 나갔다"), Melee->GetShotCount(), 1);
	TestEqual(TEXT("반동 1번 (되감기 신호)"), Melee->GetSwingCount(), 1);
	TestTrue(TEXT("쏜 순간 반동 도는 중"), Melee->IsSwinging());
	TestEqual(TEXT("한 바퀴 = 쏘기 간격"), Melee->GetActionLengthSeconds(), Gun->FireIntervalSeconds, TestTolerance);

	AdvanceTimers(World, Gun->FireIntervalSeconds * 0.5f);
	TestTrue(TEXT("간격 절반에도 도는 중"), Melee->IsSwinging());
	AdvanceTimers(World, Gun->FireIntervalSeconds);
	TestFalse(TEXT("간격 뒤 든 자세로"), Melee->IsSwinging());

	Melee->StartAttacking();
	Melee->StopAttacking();
	TestEqual(TEXT("두 번째 발 = 반동 2번"), Melee->GetSwingCount(), 2);

	// 빈 탄창 누름은 반동 없음(찰칵만).
	AdvanceTimers(World, Gun->FireIntervalSeconds * 2.f);
	Melee->StartAttacking();
	Melee->StopAttacking();
	TestEqual(TEXT("빈 탄창 = 반동 그대로 2번"), Melee->GetSwingCount(), 2);
	TestFalse(TEXT("빈 탄창은 몸 클립 안 돎"), Melee->IsSwinging());

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenGunSoundsTest, "Snarehaven.Combat.Gun.SoundsPlay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** M39-06c — 총소리 4개 에셋이 있고, 쏘기·빈 찰칵·맞음이 제자리에서 한 번씩 난다(재장전은 `ReloadMotionBlocksFire`). */
bool FSnarehavenGunSoundsTest::RunTest(const FString& Parameters)
{
	const TCHAR* const Cues[] = { UMeleeAttackComponent::GunShotCue, UMeleeAttackComponent::GunEmptyClickCue,
		UMeleeAttackComponent::GunReloadCue, ASnareGunTracerActor::ImpactCue };
	for (const TCHAR* Cue : Cues)
	{
		const FString Path = FString::Printf(TEXT("/Game/Game/Audio/SFX/SFX_%s_01.SFX_%s_01"), Cue, Cue);
		TestNotNull(*FString::Printf(TEXT("소리 에셋 %s"), Cue), LoadObject<USoundBase>(nullptr, *Path));
	}

	UWorld* const World = CreateTestWorld();
	const FItemRow* Gun = nullptr;
	ASnarehavenCharacter* const Player = SpawnGunner(*this, World, Gun);
	if (Player == nullptr)
	{
		DestroyTestWorld(World);
		return false;
	}
	UMeleeAttackComponent* const Melee = Player->GetMeleeAttack();
	Melee->SetLoadedRounds(1);

	Melee->StartAttacking();
	Melee->StopAttacking();
	TestEqual(TEXT("한 발 = 총소리 1번"), Melee->GetGunSoundCount(), 1);
	TestEqual(TEXT("총소리 이름"), Melee->GetLastGunSound(), FName(UMeleeAttackComponent::GunShotCue));
	ASnareGunTracerActor* const Tracer = Melee->GetLastTracer();
	if (TestNotNull(TEXT("보이는 총알"), Tracer))
	{
		TestEqual(TEXT("날아가는 동안 맞음 소리 0"), Tracer->GetImpactSoundCount(), 0);
		for (int32 Frame = 0; Frame < 300 && IsValid(Tracer) && !Tracer->HasLanded(); ++Frame)
		{
			Tracer->Tick(1.f / 30.f);
		}
		TestEqual(TEXT("닿는 순간 맞음 소리 1번"), Tracer->GetImpactSoundCount(), 1);
	}

	AdvanceTimers(World, Gun->FireIntervalSeconds * 2.f);
	Melee->StartAttacking();
	Melee->StopAttacking();
	TestEqual(TEXT("빈 탄창 = 찰칵 소리"), Melee->GetLastGunSound(), FName(UMeleeAttackComponent::GunEmptyClickCue));
	TestEqual(TEXT("소리 2번 (탕·찰칵)"), Melee->GetGunSoundCount(), 2);
	AddInfo(FString::Printf(TEXT("[GunSound] 에셋 4 · 쏘기 %s · 빈 %s · 맞음 %d · 이 기기 총소리 %d"),
		UMeleeAttackComponent::GunShotCue, UMeleeAttackComponent::GunEmptyClickCue,
		IsValid(Tracer) ? Tracer->GetImpactSoundCount() : -1, Melee->GetGunSoundCount()));

	DestroyTestWorld(World);
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenGunTracerMuzzleTest, "Snarehaven.Combat.Gun.TracerStartsAtMuzzle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** M39-07 — 겨눔은 오른쪽 앞으로 뻗은 한 손이라, 총알은 손에 든 메시의 총구 끝(+X 끝)에서 나와야 한다. */
bool FSnarehavenGunTracerMuzzleTest::RunTest(const FString& Parameters)
{
	constexpr float MuzzleToleranceCm = 1.f;
	UWorld* const World = CreateTestWorld();
	const FItemRow* Gun = nullptr;
	ASnarehavenCharacter* const Player = SpawnGunner(*this, World, Gun);
	const UStaticMeshComponent* const GunMesh = Player ? Player->GetEquipment()->GetEquippedMesh(EEquipSlot::Weapon) : nullptr;
	if (Player == nullptr || !TestNotNull(TEXT("손에 든 총 메시"), GunMesh) || !TestTrue(TEXT("총 메시 에셋"), GunMesh->GetStaticMesh() != nullptr))
	{
		DestroyTestWorld(World);
		return false;
	}
	UMeleeAttackComponent* const Melee = Player->GetMeleeAttack();
	Melee->SetLoadedRounds(1);
	Melee->StartAttacking();
	Melee->StopAttacking();
	ASnareGunTracerActor* const Tracer = Melee->GetLastTracer();
	if (!TestNotNull(TEXT("보이는 총알"), Tracer))
	{
		DestroyTestWorld(World);
		return false;
	}
	const FVector Tip = GunMesh->GetComponentTransform().TransformPosition(
		FVector(GunMesh->GetStaticMesh()->GetBoundingBox().Max.X, 0.0, 0.0));
	const FVector OldFront = Player->GetActorLocation() + Player->GetActorForwardVector() * UMeleeAttackComponent::MuzzleForwardCm
		+ FVector(0.f, 0.f, UMeleeAttackComponent::MuzzleUpCm);
	const float Gap = FVector::Dist(Tracer->GetStartLocation(), Tip);
	AddInfo(FString::Printf(TEXT("[GunMuzzle] 총알 시작 %s · 총구 끝 %s · 차이 %.2fcm · 예전 몸 앞 자리와 %.1fcm"),
		*Tracer->GetStartLocation().ToCompactString(), *Tip.ToCompactString(), Gap, FVector::Dist(Tracer->GetStartLocation(), OldFront)));
	TestTrue(FString::Printf(TEXT("총알이 총구 끝에서 나온다 (%.2f ≤ %.0fcm)"), Gap, MuzzleToleranceCm), Gap <= MuzzleToleranceCm);

	DestroyTestWorld(World);
	return true;
}


// M62-02 — **쏜 사람 본인 기기만 손이 떨린다** (DESIGN 2.2 「총 반동 진동」). 실제 쏘기 길(`FireOnce` → `Multicast_PlayShot`)로 친다.
// 내가 쏨 → 1 · 간격 안 또 쏨 → 그대로 1 · 남이 쏨(조종기 없음) → 0 · 끔 → 0.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenGunShotHapticTest, "Snarehaven.Combat.Gun.ShotHaptic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenGunShotHapticTest::RunTest(const FString& Parameters)
{
	const bool bWasEnabled = SnareHaptics::IsEnabled();
	SnareHaptics::SetEnabled(true);
	UWorld* const World = CreateTestWorld();
	const FItemRow* Gun = nullptr;
	ASnarehavenCharacter* const Player = SpawnGunner(*this, World, Gun);
	APlayerController* const Controller = Player != nullptr ? World->SpawnActor<APlayerController>() : nullptr;
	if (Player == nullptr || Controller == nullptr)
	{
		AddError(TEXT("쏘는 사람·조종기를 못 세웠다"));
		SnareHaptics::SetEnabled(bWasEnabled);
		DestroyTestWorld(World);
		return false;
	}
	UMeleeAttackComponent* const Melee = Player->GetMeleeAttack();
	Melee->SetLoadedRounds(Gun->MagazineSize);

	// ① 남이 쏨 — 조종기 없는 몸은 0
	int32 Before = SnareHaptics::GetPlayCount();
	TestTrue(TEXT("남이 쏨"), Melee->FireOnce());
	TestEqual(TEXT("남이 쏨 → 내 진동 0"), SnareHaptics::GetPlayCount() - Before, 0);

	// ② 내 기기 — 넷 드라이버 없는 월드는 `ULocalPlayer` 붙은 조종기만 "이 기기"
	Controller->Player = NewObject<ULocalPlayer>(GEngine, ULocalPlayer::StaticClass());
	Controller->Possess(Player);
	TestTrue(TEXT("내 몸은 내 기기 조종"), Player->IsLocallyControlled());
	Before = SnareHaptics::GetPlayCount();
	TestTrue(TEXT("내가 쏨"), Melee->FireOnce());
	TestEqual(TEXT("내가 쏨 → 진동 1"), SnareHaptics::GetPlayCount() - Before, 1);
	TestTrue(TEXT("간격 안 또 쏨"), Melee->FireOnce());
	TestEqual(TEXT("간격 안 또 쏨 → 그대로 1"), SnareHaptics::GetPlayCount() - Before, 1);

	// ③ 끔 — 간격 기록이 있어도 설정이 먼저다
	SnareHaptics::SetEnabled(false);
	Before = SnareHaptics::GetPlayCount();
	TestFalse(TEXT("끔 → 안 튼다"), Player->PlayShotHaptic(SnareBodyHaptic::EGun::Rifle));
	TestEqual(TEXT("끔 → 0"), SnareHaptics::GetPlayCount() - Before, 0);

	AddInfo(FString::Printf(TEXT("[ShotHaptic] 내가 쏨 1 · 남 0 · 끔 0 (누적 %d)"), SnareHaptics::GetPlayCount()));
	SnareHaptics::SetEnabled(bWasEnabled);
	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenGunFireAfterHitTest, "Snarehaven.Combat.Gun.SecondShotAfterHitCapturable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// 맞힌 동물이 포획 창(Capturable)에 들어가도 간격이 지나면 2발째가 나간다 (루프#1144 — 누른 채 연사가 끊긴다던 결함).
bool FSnarehavenGunFireAfterHitTest::RunTest(const FString& Parameters)
{
	UWorld* const World = CreateTestWorld();
	const FItemRow* Gun = nullptr;
	ASnarehavenCharacter* const Player = SpawnGunner(*this, World, Gun);
	if (Player == nullptr)
	{
		DestroyTestWorld(World);
		return false;
	}
	AAnimalCharacter* const Animal = SpawnTestAnimal(World, FVector(Gun->AttackRangeCm * 0.2f, 0.f, 0.f));
	if (!TestNotNull(TEXT("동물"), Animal))
	{
		DestroyTestWorld(World);
		return false;
	}
	UMeleeAttackComponent* const Melee = Player->GetMeleeAttack();
	Melee->SetLoadedRounds(3);
	Melee->StartAttacking();
	Melee->StopAttacking();
	// 포획 임계 아래로 깎아 포획 창에 세운다.
	Animal->GetAnimalState()->ApplyDamage(TestMaxHP - Animal->GetAnimalState()->GetCaptureThresholdHP() * 0.5f);
	TestTrue(TEXT("맞힌 동물이 포획 창에 섰다"), Animal->GetAnimalState()->GetState() == EAnimalState::Capturable);
	AdvanceTimers(World, Gun->FireIntervalSeconds + 0.1f);
	Melee->StartAttacking();
	Melee->StopAttacking();
	TestEqual(TEXT("포획 창 동물에게도 2발째가 나갔다"), Melee->GetShotCount(), 2);

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenGunBlastVisualTest, "Snarehaven.Combat.Gun.BlastVisual",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenGunBlastVisualTest::RunTest(const FString& Parameters)
{
	// 시험 전용 반경 — 바주카 행 숫자가 바뀌어도 통과. 폭발 불덩이는 반경의 지름까지 부풀고 소리는 폭발 소리 하나다.
	constexpr float FrameSeconds = 1.f / 30.f;
	constexpr float TestRadiusCm = 250.f;
	UWorld* const World = CreateTestWorld();
	const FVector Muzzle(0.f, 0.f, 100.f);
	ASnareGunTracerActor* const Tracer = World->SpawnActor<ASnareGunTracerActor>(Muzzle, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("총알 액터"), Tracer))
	{
		DestroyTestWorld(World);
		return false;
	}
	Tracer->Start(Muzzle, Muzzle + FVector(1000.f, 0.f, 0.f), 0.f, ESnareGunSplash::Dust, 1, 0.f, TestRadiusCm);
	TestEqual(TEXT("반경이 액터에 실림"), Tracer->GetBlastRadiusCm(), TestRadiusCm);
	TestTrue(TEXT("바로 터짐"), Tracer->HasLanded());
	TestEqual(TEXT("폭발 소리 한 번"), Tracer->GetImpactSoundCount(), 1);
	TestTrue(TEXT("불덩이 보임"), Tracer->IsImpactPuffVisible());
	const float StartCm = Tracer->GetImpactPuffDiameterCm();
	TestEqual(TEXT("시작 지름 = 지름 × 시작 비율"), StartCm, TestRadiusCm * 2.f * ASnareGunTracerActor::BlastPuffStartRatio, 0.5f);
	float MaxCm = StartCm;
	int32 Frames = 0;
	while (Frames < 60 && Tracer->IsImpactPuffVisible())
	{
		Tracer->Tick(FrameSeconds);
		MaxCm = FMath::Max(MaxCm, Tracer->GetImpactPuffDiameterCm());
		++Frames;
	}
	AddInfo(FString::Printf(TEXT("[GunBlast] 반경 %.0fcm · 불덩이 %.0f → %.0fcm · %d장"), TestRadiusCm, StartCm, MaxCm, Frames));
	TestTrue(TEXT("불덩이가 반경 지름의 90% 이상까지 부푼다"), MaxCm >= TestRadiusCm * 2.f * 0.9f);
	TestTrue(TEXT("0.35초 안팎(±2장)에 사라진다"),
		FMath::Abs(Frames - FMath::RoundToInt(ASnareGunTracerActor::BlastPuffSeconds / FrameSeconds)) <= 2);
	// 폭발은 불덩이 뒤에 검은 연기 덩어리가 남았다가 BlastSmokeSeconds 에 사라진다(#1211). 일반 총알엔 연기가 없다.
	ASnareGunTracerActor* const Boom = World->SpawnActor<ASnareGunTracerActor>(Muzzle, FRotator::ZeroRotator);
	Boom->Start(Muzzle, Muzzle + FVector(1000.f, 0.f, 0.f), 0.f, ESnareGunSplash::Dust, 1, 0.f, TestRadiusCm);
	TestTrue(TEXT("폭발 검은 연기 보임"), Boom->IsBlastSmokeVisible());
	int32 SmokeFrames = 0;
	while (SmokeFrames < 90 && IsValid(Boom) && Boom->IsBlastSmokeVisible())
	{
		Boom->Tick(FrameSeconds);
		++SmokeFrames;
	}
	AddInfo(FString::Printf(TEXT("[GunBlast] 검은 연기 %d장(%.2f초)"), SmokeFrames, SmokeFrames * FrameSeconds));
	TestTrue(TEXT("연기는 불덩이보다 오래 남는다"), SmokeFrames > Frames);
	TestTrue(TEXT("연기는 BlastSmokeSeconds 안팎(±2장)에 사라진다"),
		FMath::Abs(SmokeFrames - FMath::RoundToInt(ASnareGunTracerActor::BlastSmokeSeconds / FrameSeconds)) <= 2);
	// 날아가는 로켓은 일반 총알보다 길고 굵은 연기 꼬리를 단다.
	ASnareGunTracerActor* const Rocket = World->SpawnActor<ASnareGunTracerActor>(Muzzle, FRotator::ZeroRotator);
	Rocket->Start(Muzzle, Muzzle + FVector(2500.f, 0.f, 0.f), 1.f, ESnareGunSplash::Dust, 1, 0.f, TestRadiusCm);
	ASnareGunTracerActor* const Slug = World->SpawnActor<ASnareGunTracerActor>(Muzzle, FRotator::ZeroRotator);
	Slug->Start(Muzzle, Muzzle + FVector(2500.f, 0.f, 0.f), 1.f, ESnareGunSplash::Dust);
	AddInfo(FString::Printf(TEXT("[GunBlast] 꼬리 로켓 %.0f×%.0fcm · 총알 %.0f×%.0fcm"),
		Rocket->GetTailLengthCm(), Rocket->GetTailWidthCm(), Slug->GetTailLengthCm(), Slug->GetTailWidthCm()));
	TestTrue(TEXT("로켓 꼬리가 총알 꼬리보다 길다"), Rocket->GetTailLengthCm() > Slug->GetTailLengthCm() * 2.f);
	TestTrue(TEXT("로켓 꼬리가 총알 꼬리보다 굵다"), Rocket->GetTailWidthCm() > Slug->GetTailWidthCm() * 2.f);
	// 로켓은 약한 포물선 — 가운데가 거리 × RocketArcApexRatio 만큼 뜨고 양 끝은 곧은 선과 같다. 일반 총알은 곧다.
	const float ArcMidCm = static_cast<float>(Rocket->GetFlightPoint(1250.f).Z - Muzzle.Z);
	AddInfo(FString::Printf(TEXT("[GunBlast] 로켓 2500cm 가운데 높이 %.1fcm · 총알 %.1fcm"), ArcMidCm, Slug->GetFlightPoint(1250.f).Z - Muzzle.Z));
	TestEqual(TEXT("로켓 가운데 높이 = 거리 × 꼭대기 비율"), ArcMidCm, 2500.f * ASnareGunTracerActor::RocketArcApexRatio, 0.5f);
	TestEqual(TEXT("로켓 끝은 닿는 자리 그대로"), static_cast<float>(Rocket->GetFlightPoint(2500.f).Z - Muzzle.Z), 0.f, 0.5f);
	TestEqual(TEXT("일반 총알은 곧다"), static_cast<float>(Slug->GetFlightPoint(1250.f).Z - Muzzle.Z), 0.f, 0.01f);
	// 쏘는 장에 포구 곁 덩이가 바로 켜지고, 첫 덩이는 포구 끝에 붙는다(감독 10-16 3차 「포구와 첫 연기 사이 빈틈」).
	TestEqual(TEXT("쏘는 장에 포구 곁 덩이가 바로 켜진다"), Rocket->GetVisibleSmokePuffCount(), ASnareGunTracerActor::SmokePuffInstantCount);
	TestTrue(TEXT("첫 연기 덩이–포구 거리 < 30cm"), FVector::Dist(Rocket->GetSmokePuffLocation(0), Muzzle) < 30.f);
	// 로켓 연기 꼬리는 닿은 뒤에도 SmokeTrailFadeSeconds 동안 남는다(감독 10-16 「발사 0.1~0.2초 뒤 꼬리가 길게」) — 총알 꼬리는 닿으면 꺼진다.
	Rocket->Tick(1.05f);
	Slug->Tick(1.05f);
	Rocket->Tick(0.2f);
	TestTrue(TEXT("로켓 연기 꼬리는 닿고 0.2초 뒤에도 보인다"), Rocket->IsTailVisible());
	TestEqual(TEXT("로켓 연기는 둥근 덩이 여러 개"), Rocket->GetVisibleSmokePuffCount(), ASnareGunTracerActor::SmokePuffCount);
	TestFalse(TEXT("총알 꼬리는 닿으면 꺼진다"), Slug->IsTailVisible());
	Rocket->Tick(ASnareGunTracerActor::SmokeTrailFadeSeconds);
	TestFalse(TEXT("로켓 연기 꼬리는 SmokeTrailFadeSeconds 뒤 사라진다"), IsValid(Rocket) && Rocket->IsTailVisible());
	// 일반 총알은 폭발이 아니다.
	ASnareGunTracerActor* const Plain = World->SpawnActor<ASnareGunTracerActor>(Muzzle, FRotator::ZeroRotator);
	Plain->Start(Muzzle, Muzzle + FVector(1000.f, 0.f, 0.f), 0.f, ESnareGunSplash::Dust);
	TestEqual(TEXT("일반 총알 반경 0"), Plain->GetBlastRadiusCm(), 0.f);
	TestFalse(TEXT("일반 총알엔 검은 연기 없음"), Plain->IsBlastSmokeVisible());
	TestTrue(TEXT("일반 퍽은 폭발보다 작다"), Plain->GetImpactPuffDiameterCm() < StartCm);
	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenGunHeldSecondShotTest, "Snarehaven.Combat.Gun.HeldSecondShotAfterHit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// 맞힌 뒤 누른 채면 2발째가 나간다 — 다시 누른 BeginAttack 이 다른 가지(상인 판 등)로 빠져 공격 중 표시가 꺼져도
// 검사 손은 손가락(bUseInputHeld)을 보고 계속 누른다. 손을 떼면(EndAttack) 더 안 누른다.
bool FSnarehavenGunHeldSecondShotTest::RunTest(const FString& Parameters)
{
	UWorld* const World = CreateTestWorld();
	const FItemRow* Gun = nullptr;
	ASnarehavenCharacter* const Player = SpawnGunner(*this, World, Gun);
	if (Player == nullptr)
	{
		DestroyTestWorld(World);
		return false;
	}
	AAnimalCharacter* const Animal = SpawnTestAnimal(World, FVector(Gun->AttackRangeCm * 0.2f, 0.f, 0.f));
	if (!TestNotNull(TEXT("동물"), Animal))
	{
		DestroyTestWorld(World);
		return false;
	}
	UMeleeAttackComponent* const Melee = Player->GetMeleeAttack();
	Melee->SetLoadedRounds(5);
	Player->BeginAttack();
	TestEqual(TEXT("누르자 1발째"), Melee->GetShotCount(), 1);
	// 맞은 동물이 포획 창에 서도 검사 손은 총이면 다시 누른다 (루프#1144 — 이 결함의 실제 원인).
	Animal->GetAnimalState()->ApplyDamage(TestMaxHP - Animal->GetAnimalState()->GetCaptureThresholdHP() * 0.5f);
	TestTrue(TEXT("맞힌 동물이 포획 창에 섰다"), Animal->GetAnimalState()->GetState() == EAnimalState::Capturable);
	AdvanceTimers(World, Gun->FireIntervalSeconds + 0.1f);
	Player->RepressHeldAttackOnce();
	TestEqual(TEXT("누른 채면 2발째가 나갔다"), Melee->GetShotCount(), 2);
	// 대상이 없어 다른 가지(상인 판 등)로 빠진 누름의 끝 상태 — 공격 중은 꺼졌고 손가락은 그대로다.
	Melee->StopAttacking();
	AdvanceTimers(World, Gun->FireIntervalSeconds + 0.1f);
	Player->RepressHeldAttackOnce();
	TestEqual(TEXT("공격 중이 꺼져도 누른 채면 3발째"), Melee->GetShotCount(), 3);

	Player->EndAttack();
	AdvanceTimers(World, Gun->FireIntervalSeconds + 0.1f);
	Player->RepressHeldAttackOnce();
	TestEqual(TEXT("손을 떼면 더 안 나간다"), Melee->GetShotCount(), 3);

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenGunSplashWorldTest, "Snarehaven.Combat.Gun.SplashWorld",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// 폭발 총 한 발이 실제 세상에서: 맞은 동물은 직격, 반경 안 둘레 동물은 거리만큼 줄여, 반경 밖은 0.
// 피해는 날아간 뒤(거리 ÷ 속도) 들어간다. 사람·목장 동물(ARanchPetActor)은 AAnimalCharacter 가 아니라 애초에 대상이 아니다.
bool FSnarehavenGunSplashWorldTest::RunTest(const FString& Parameters)
{
	// 시험 전용 값 — 바주카 행 숫자가 바뀌어도 통과.
	constexpr float TestRadiusCm = 300.f;
	constexpr float TestEdgeRatio = 0.3f;
	constexpr float NearOffsetCm = 150.f;
	UWorld* const World = CreateTestWorld();
	const FItemRow* Gun = nullptr;
	ASnarehavenCharacter* const Player = SpawnGunner(*this, World, Gun);
	if (Player == nullptr)
	{
		DestroyTestWorld(World);
		return false;
	}
	// 시험 표(임시 패키지)의 권총 행을 폭발 총으로 — 실제 표는 안 건드린다.
	FItemRow* const SplashGun = const_cast<FItemRow*>(Gun);
	SplashGun->SplashRadiusCm = TestRadiusCm;
	SplashGun->SplashEdgeRatio = TestEdgeRatio;
	const float X = Gun->AttackRangeCm * 0.5f;
	AAnimalCharacter* const Hit = SpawnTestAnimal(World, FVector(X, 0.f, 0.f));
	AAnimalCharacter* const Near = SpawnTestAnimal(World, FVector(X, NearOffsetCm, 0.f));
	AAnimalCharacter* const Far = SpawnTestAnimal(World, FVector(X, TestRadiusCm + 100.f, 0.f));
	if (!TestNotNull(TEXT("동물 3"), Hit) || !TestNotNull(TEXT("둘레"), Near) || !TestNotNull(TEXT("밖"), Far))
	{
		DestroyTestWorld(World);
		return false;
	}
	UMeleeAttackComponent* const Melee = Player->GetMeleeAttack();
	Melee->SetLoadedRounds(1);
	Melee->StartAttacking();
	Melee->StopAttacking();
	TestEqual(TEXT("한 발 나감"), Melee->GetShotCount(), 1);
	TestEqual(TEXT("날아가는 동안엔 피해 없음"), Hit->GetAnimalState()->GetCurrentHP(), TestMaxHP, TestTolerance);
	AdvanceTimers(World, X / Gun->ProjectileSpeedCmPerSecond + 0.1f);
	const float HitLoss = TestMaxHP - Hit->GetAnimalState()->GetCurrentHP();
	const float NearLoss = TestMaxHP - Near->GetAnimalState()->GetCurrentHP();
	const float FarLoss = TestMaxHP - Far->GetAnimalState()->GetCurrentHP();
	AddInfo(FString::Printf(TEXT("[GunSplashWorld] 반경 %.0fcm · 맞은 동물 -%.1f · %.0fcm 둘레 -%.1f · 반경 밖 -%.1f"),
		TestRadiusCm, HitLoss, NearOffsetCm, NearLoss, FarLoss));
	TestTrue(TEXT("맞은 동물이 깎였다"), HitLoss > 0.f);
	TestEqual(TEXT("둘레 동물 = 직격 × 거리 비율"), NearLoss,
		HitLoss * FMath::Lerp(1.f, TestEdgeRatio, NearOffsetCm / TestRadiusCm), 0.1f);
	TestEqual(TEXT("반경 밖은 그대로"), FarLoss, 0.f, TestTolerance);
	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenGunHudAmmoIconTest, "Snarehaven.UI.GunHud.AmmoIconMatchesGunAmmo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 「탄약은 왜 아이콘이 없냐」(10-18) — **실제 표**의 총 행마다: 들면 탄 칸 그림이 보이고, 그 그림 = 그 총 `AmmoItemID`
 * 행의 `IconRef`(권총 탄·소총탄·산탄·화살·로켓이 서로 다르다), 크기는 무기 아이콘(시트 40px)만 하다.
 */
bool FSnarehavenGunHudAmmoIconTest::RunTest(const FString& Parameters)
{
	constexpr float MinAmmoIconPx = 40.f;   // 테스트 전용 — 시트 무기 아이콘 40px 와 같은 줄(작아서 못 알아보면 실패)
	FReloadRig Rig;
	if (!Rig.Setup(*this))
	{
		return false;
	}
	UDataTable* const Items = LoadObject<UDataTable>(nullptr, ItemTablePath);
	FObjectProperty* const TableProperty = FindFProperty<FObjectProperty>(UEquipmentComponent::StaticClass(), TEXT("ItemTable"));
	if (!TestNotNull(TEXT("DT_ItemTable"), Items) || !TestNotNull(TEXT("장비 표 칸"), TableProperty))
	{
		return false;
	}
	UEquipmentComponent* const Equipment = Rig.Player->GetEquipment();
	TableProperty->SetObjectPropertyValue_InContainer(Equipment, Items);   // 시험 전용 권총 표 → 실제 표
	UHuntHudWidget* const Hud = CreateWidget<UHuntHudWidget>(Rig.World, UHuntHudWidget::StaticClass());
	if (!TestNotNull(TEXT("HUD 생성"), Hud))
	{
		return false;
	}
	Hud->TakeWidget();
	Hud->BindToCharacter(Rig.Player);
	const UImage* const AmmoIcon = Hud->WidgetTree->FindWidget<UImage>(TEXT("GunAmmoIcon"));
	if (!TestNotNull(TEXT("탄 그림 위젯"), AmmoIcon))
	{
		return false;
	}

	TMap<FName, const FItemRow*> RowsByItemID;
	for (const TPair<FName, uint8*>& Pair : Items->GetRowMap())
	{
		const FItemRow* const Row = reinterpret_cast<const FItemRow*>(Pair.Value);
		RowsByItemID.Add(Row->ItemID, Row);
	}
	int32 GunCount = 0;
	TSet<FName> AmmoKinds;
	for (const TPair<FName, uint8*>& Pair : Items->GetRowMap())
	{
		const FItemRow* const Gun = reinterpret_cast<const FItemRow*>(Pair.Value);
		if (Gun->MagazineSize <= 0 || Gun->AmmoItemID.IsNone())
		{
			continue;
		}
		++GunCount;
		const FString Name = Pair.Key.ToString();
		const FItemRow* const* const Ammo = RowsByItemID.Find(Gun->AmmoItemID);
		if (!TestNotNull(*FString::Printf(TEXT("%s 탄 행 %s"), *Name, *Gun->AmmoItemID.ToString()), Ammo)
			|| !TestFalse(*FString::Printf(TEXT("%s 탄 IconRef 빈 칸 아님"), *Name), (*Ammo)->IconRef.IsNull())
			|| !TestTrue(*FString::Printf(TEXT("%s 들었다"), *Name), Equipment->EquipItem(Pair.Key)))
		{
			continue;
		}
		AmmoKinds.Add(Gun->AmmoItemID);
		Hud->UpdateGunHud();
		const FSlateBrush& Brush = AmmoIcon->GetBrush();
		TestTrue(*FString::Printf(TEXT("%s 탄 그림이 보인다"), *Name), AmmoIcon->GetVisibility() != ESlateVisibility::Collapsed);
		TestEqual(*FString::Printf(TEXT("%s 탄 그림 = %s 아이콘"), *Name, *Gun->AmmoItemID.ToString()),
			Brush.GetResourceObject(), static_cast<UObject*>((*Ammo)->IconRef.LoadSynchronous()));
		TestTrue(*FString::Printf(TEXT("%s 탄 그림 %.0fpx ≥ %.0f"), *Name, Brush.ImageSize.X, MinAmmoIconPx),
			FMath::Min(Brush.ImageSize.X, Brush.ImageSize.Y) >= MinAmmoIconPx);
		AddInfo(FString::Printf(TEXT("[AmmoIcon] %s → %s · %s · %.0fpx"), *Name, *Gun->AmmoItemID.ToString(),
			*GetNameSafe(Brush.GetResourceObject()), Brush.ImageSize.X));
	}
	// 표에서 총을 하나도 못 찾으면 위 훑기는 아무것도 안 본 것이다 (Verification.md — 빈 검사 금지).
	TestTrue(TEXT("총 행 둘 이상"), GunCount >= 2);
	TestTrue(TEXT("탄 종류 둘 이상(총마다 다른 탄)"), AmmoKinds.Num() >= 2);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
