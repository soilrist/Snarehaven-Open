// ROADMAP M1-05 2층 검사 — 포획 액션이 DESIGN 3.1 대로 `Capturable` 일 때만 되는지,
// 포획한 동물이 행의 펫 아이템으로 **지급 요청 대기열에만** 쌓이는지(보유는 백엔드 — CLAUDE.md 8),
// 그리고 실제 `DT_AnimalTable` 돼지 행의 `PetItemID` 를 그대로 쓰는지.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Animals/AnimalCharacter.h"
#include "Animals/AnimalStateComponent.h"
#include "Combat/CaptureComponent.h"
#include "Combat/MeleeAttackComponent.h"
#include "Data/AnimalRow.h"
#include "Data/ItemRow.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Inventory/InventoryComponent.h"
#include "EngineUtils.h"
#include "Player/SnareCaptureBurstActor.h"
#include "Player/SnareBodyHaptic.h"
#include "Player/SnareHaptics.h"
#include "Sound/SoundBase.h"
#include "SnarehavenCharacter.h"

namespace
{
	// 테스트 전용 값 (Verification.md 6 — 설계 숫자가 아니다).
	const FName TestPetItemID = TEXT("item.test_pet");
	constexpr float TestAttackDamage = 10.f;
	constexpr float TestAttackRangeCm = 300.f;
	constexpr float TestAttackIntervalSeconds = 1.f;
	constexpr float TestNearDistanceCm = 100.f;
	constexpr float TestOutOfRangeDistanceCm = 1000.f;
	constexpr float TestMaxHP = 20.f;
	constexpr float TestCaptureThresholdPct = 50.f;

	// 실제 데이터 테이블 (M1-01). 펫 아이템 ID 는 여기서만 온다 — 기대값을 코드에 적지 않는다.
	const TCHAR* AnimalTablePath = TEXT("/Game/Game/Data/Animals/DT_AnimalTable.DT_AnimalTable");
	const TCHAR* ItemTablePath = TEXT("/Game/Game/Data/Items/DT_ItemTable.DT_ItemTable");
	const TCHAR* PigRowName = TEXT("Animal_Pig");

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

	// 테스트 전용 체력·펫 ID 로 초기화한 동물. 실제 테이블을 읽지 않는다.
	AAnimalCharacter* SpawnTestAnimal(UWorld* World, float DistanceCm, FName PetItemID = TestPetItemID)
	{
		AAnimalCharacter* Animal =
			World->SpawnActor<AAnimalCharacter>(FVector(DistanceCm, 0.f, 0.f), FRotator::ZeroRotator);
		if (Animal == nullptr)
		{
			return nullptr;
		}

		FAnimalRow Row;
		Row.AnimalID = TEXT("animal.test_dummy");
		Row.MaxHP = TestMaxHP;
		Row.CaptureThresholdPct = TestCaptureThresholdPct;
		Row.PetItemID = PetItemID;
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

	// 임계값 이하까지 때려서 포획 창을 연다. 타수를 세지 않는다 — 그건 M1-03 검사 몫이다.
	bool BringToCapturable(ASnarehavenCharacter* Player, const AAnimalCharacter* Animal)
	{
		UMeleeAttackComponent* Melee = Player->GetMeleeAttack();
		const UAnimalStateComponent* State = Animal->GetAnimalState();
		while (State->GetState() != EAnimalState::Capturable && !State->IsFinished())
		{
			if (!Melee->AttackOnce())
			{
				return false;
			}
		}

		return State->GetState() == EAnimalState::Capturable;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenCaptureNoTargetTest, "Snarehaven.Capture.Action.NoTargetDoesNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenCaptureNoTargetTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = SpawnTestPlayer(World);
	AAnimalCharacter* FarAnimal = SpawnTestAnimal(World, TestOutOfRangeDistanceCm);
	if (!TestNotNull(TEXT("플레이어 스폰"), Player) || !TestNotNull(TEXT("사거리 밖 동물"), FarAnimal))
	{
		DestroyTestWorld(World);
		return false;
	}

	UCaptureComponent* Capture = Player->GetCapture();
	if (!TestNotNull(TEXT("포획 컴포넌트가 붙어 있다"), Capture))
	{
		DestroyTestWorld(World);
		return false;
	}

	TestNull(TEXT("사거리 밖이면 포획 대상이 없다"), Capture->GetCaptureTarget());
	TestFalse(TEXT("대상이 없으면 포획 버튼이 안 켜진다"), Capture->CanCapture());
	TestFalse(TEXT("대상이 없으면 포획도 안 된다"), Capture->TryCaptureTarget());

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenCaptureThresholdTest, "Snarehaven.Capture.Action.RejectsAboveThreshold",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenCaptureThresholdTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = SpawnTestPlayer(World);
	AAnimalCharacter* Animal = SpawnTestAnimal(World, TestNearDistanceCm);
	if (!TestNotNull(TEXT("플레이어 스폰"), Player) || !TestNotNull(TEXT("동물 스폰"), Animal))
	{
		DestroyTestWorld(World);
		return false;
	}

	UCaptureComponent* Capture = Player->GetCapture();
	UAnimalStateComponent* State = Animal->GetAnimalState();

	// 하이라이트만 켜 둔다 — 아직 한 대도 안 때렸으니 체력은 최대치다.
	Player->GetMeleeAttack()->RefreshTarget();
	TestTrue(TEXT("사거리 안이면 대상이 잡힌다"), Capture->GetCaptureTarget() == Animal);
	TestFalse(TEXT("임계값 위에서는 포획 버튼이 안 켜진다"), Capture->CanCapture());
	TestFalse(TEXT("임계값 위에서는 포획이 안 된다"), Capture->TryCaptureTarget());
	TestTrue(TEXT("상태는 그대로 (Idle)"), State->GetState() == EAnimalState::Idle);

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenCaptureAtThresholdTest, "Snarehaven.Capture.Action.CapturesAtThreshold",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenCaptureAtThresholdTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = SpawnTestPlayer(World);
	AAnimalCharacter* Animal = SpawnTestAnimal(World, TestNearDistanceCm);
	if (!TestNotNull(TEXT("플레이어 스폰"), Player) || !TestNotNull(TEXT("동물 스폰"), Animal))
	{
		DestroyTestWorld(World);
		return false;
	}

	if (!TestTrue(TEXT("임계값까지 때려서 포획 창을 연다"), BringToCapturable(Player, Animal)))
	{
		DestroyTestWorld(World);
		return false;
	}

	UCaptureComponent* Capture = Player->GetCapture();
	TestTrue(TEXT("임계 이하면 포획 버튼이 켜진다"), Capture->CanCapture());
	TestTrue(TEXT("포획 성공 (성공률 100% — DESIGN 3.1)"), Capture->TryCaptureInWindowForTest());
	TestTrue(TEXT("상태가 Captured"), Animal->GetAnimalState()->GetState() == EAnimalState::Captured);

	// 포획된 동물은 대상 후보에서 빠진다 — 주변에 다른 동물이 없으니 대상이 비어야 한다.
	TestNull(TEXT("포획한 동물은 더 이상 하이라이트 대상이 아니다"), Capture->GetCaptureTarget());
	TestFalse(TEXT("같은 동물을 두 번 포획할 수 없다"), Capture->TryCaptureTarget());

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenCapturePendingGrantTest, "Snarehaven.Capture.Pet.QueuesPendingGrant",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenCapturePendingGrantTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = SpawnTestPlayer(World);
	AAnimalCharacter* Animal = SpawnTestAnimal(World, TestNearDistanceCm);
	if (!TestNotNull(TEXT("플레이어 스폰"), Player) || !TestNotNull(TEXT("동물 스폰"), Animal))
	{
		DestroyTestWorld(World);
		return false;
	}

	if (!TestTrue(TEXT("임계값까지 때려서 포획 창을 연다"), BringToCapturable(Player, Animal)))
	{
		DestroyTestWorld(World);
		return false;
	}

	UInventoryComponent* Inventory = Player->GetInventory();
	TestTrue(TEXT("포획 성공"), Player->GetCapture()->TryCaptureInWindowForTest());

	// CLAUDE.md 8 · DESIGN 4.1 — 펫 등록은 백엔드 몫이다. 클라이언트는 요청만 쌓는다.
	TestEqual(TEXT("펫 1마리가 지급 요청 대기열에 쌓였다"), Inventory->GetPendingCount(TestPetItemID), 1);
	TestEqual(TEXT("보유 수량은 아직 0 (백엔드 응답 전)"), Inventory->GetItemCount(TestPetItemID), 0);
	TestEqual(TEXT("보유 칸도 그대로 비어 있다"), Inventory->GetSlots().Num(), 0);

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenCaptureFeelTest, "Snarehaven.Capture.Feel.CinchSoundAndHaptic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenCaptureFeelTest::RunTest(const FString& Parameters)
{
	// M21-03b — 포획이 확정되면 조이는 소리 + 잡은 사람 기기에서 가벼운 진동 한 번. 설정에서 끄면 진동만 빠진다.
	const bool bWasEnabled = SnareHaptics::IsEnabled();
	TestNotNull(TEXT("조이는 소리 에셋이 있다"), LoadObject<USoundBase>(nullptr, TEXT("/Game/Game/Audio/SFX/SFX_Cinch_01.SFX_Cinch_01")));
	TestEqual(TEXT("포획 소리 이름"), ASnarehavenCharacter::GetCaptureCue(), FName(TEXT("Cinch")));

	for (const bool bEnabled : { true, false })
	{
		SnareHaptics::SetEnabled(bEnabled);
		UWorld* World = CreateTestWorld();
		ASnarehavenCharacter* Player = SpawnTestPlayer(World);
		AAnimalCharacter* Animal = SpawnTestAnimal(World, TestNearDistanceCm);
		APlayerController* Controller = World->SpawnActor<APlayerController>();
		if (!TestNotNull(TEXT("플레이어 스폰"), Player) || !TestNotNull(TEXT("동물 스폰"), Animal)
			|| !TestNotNull(TEXT("조종기"), Controller) || !TestTrue(TEXT("포획 창"), BringToCapturable(Player, Animal)))
		{
			DestroyTestWorld(World);
			SnareHaptics::SetEnabled(bWasEnabled);
			return false;
		}
		// 엔진은 넷 드라이버가 없는 월드에선 `ULocalPlayer` 가 붙은 조종기만 "이 기기" 로 본다 — 실제 게임과 같게 붙인다.
		Controller->Player = NewObject<ULocalPlayer>(GEngine, ULocalPlayer::StaticClass());
		Controller->Possess(Player);

		const int32 FeedbackBefore = Player->GetCaptureFeedbackCount();
		const int32 HapticsBefore = SnareHaptics::GetPlayCount();
		TestEqual(TEXT("포획 전엔 연출 없음"), FeedbackBefore, 0);
		TestTrue(TEXT("포획 성공"), Player->GetCapture()->TryCaptureInWindowForTest());
		const int32 Played = SnareHaptics::GetPlayCount() - HapticsBefore;
		AddInfo(FString::Printf(TEXT("[CaptureFeel] 진동 설정 %s → 연출 %d번 · 진동 %d번"), bEnabled ? TEXT("켬") : TEXT("끔"),
			Player->GetCaptureFeedbackCount() - FeedbackBefore, Played));
		TestEqual(TEXT("포획 한 번에 연출 한 번"), Player->GetCaptureFeedbackCount() - FeedbackBefore, 1);
		TestEqual(TEXT("진동은 설정을 따른다"), Played, bEnabled ? 1 : 0);
		TestEqual(TEXT("진동 여부 기록"), Player->DidLastCaptureVibrate(), bEnabled);

		// 실패한 포획(같은 동물 다시)은 연출이 없다.
		TestFalse(TEXT("두 번째 포획은 안 된다"), Player->GetCapture()->TryCaptureTarget());
		TestEqual(TEXT("실패엔 연출 없음"), Player->GetCaptureFeedbackCount() - FeedbackBefore, 1);
		DestroyTestWorld(World);
	}
	SnareHaptics::SetEnabled(bWasEnabled);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBodyHapticCueTest, "Snarehaven.Capture.Feel.BodyHapticCue",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBodyHapticCueTest::RunTest(const FString& Parameters)
{
	// M61-02 — 맞음 0.4/0.1 · 쓰러짐 0.7/0.4(맞음보다 세고 긴) · 심장 0.3/0.05(가장 약한) · 간격 안·모르는 일 → 무음.
	using namespace SnareBodyHaptic;
	const FCue Hurt = CueFor(EEvent::Hurt, 1.f);
	const FCue Down = CueFor(EEvent::Down, 1.f);
	const FCue Beat = CueFor(EEvent::Heartbeat, 1.f);
	TestEqual(TEXT("맞음 세기"), Hurt.Intensity, 0.4f);
	TestEqual(TEXT("맞음 길이"), Hurt.Seconds, 0.1f);
	TestEqual(TEXT("쓰러짐 세기"), Down.Intensity, 0.7f);
	TestEqual(TEXT("쓰러짐 길이"), Down.Seconds, 0.4f);
	TestEqual(TEXT("심장 세기"), Beat.Intensity, 0.3f);
	TestEqual(TEXT("심장 길이"), Beat.Seconds, 0.05f);
	TestTrue(TEXT("쓰러짐이 맞음보다 세고 길다"), Down.Intensity > Hurt.Intensity && Down.Seconds > Hurt.Seconds);
	TestTrue(TEXT("심장이 가장 약하다"), Beat.Intensity < Hurt.Intensity);
	TestTrue(TEXT("간격 안 → 무음"), CueFor(EEvent::Hurt, 0.2f).IsNone());
	TestFalse(TEXT("간격 딱 → 튼다"), CueFor(EEvent::Hurt, MinGapSeconds).IsNone());
	TestTrue(TEXT("모르는 일 → 무음"), CueFor(EEvent::None, 5.f).IsNone());
	TestTrue(TEXT("음수 시간 → 무음"), CueFor(EEvent::Down, -1.f).IsNone());
	AddInfo(TEXT("[BodyHaptic] 맞음 0.4/0.1 · 쓰러짐 0.7/0.4 · 심장 0.3/0.05 · 간격 안 0"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenGunHapticCueTest, "Snarehaven.Capture.Feel.GunHapticCue",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenGunHapticCueTest::RunTest(const FString& Parameters)
{
	// M62-01 — 권총 0.5/0.08 · 소총 0.6/0.15(권총보다 세고 긴) · 쓰러짐보다 약함 · 간격 0.2초 안·모르는 총 → 무음.
	using namespace SnareBodyHaptic;
	const FCue Pistol = CueFor(EGun::Pistol, 1.f);
	const FCue Rifle = CueFor(EGun::Rifle, 1.f);
	TestEqual(TEXT("권총 세기"), Pistol.Intensity, 0.5f);
	TestEqual(TEXT("권총 길이"), Pistol.Seconds, 0.08f);
	TestEqual(TEXT("소총 세기"), Rifle.Intensity, 0.6f);
	TestEqual(TEXT("소총 길이"), Rifle.Seconds, 0.15f);
	TestTrue(TEXT("소총이 권총보다 세고 길다"), Rifle.Intensity > Pistol.Intensity && Rifle.Seconds > Pistol.Seconds);
	TestTrue(TEXT("쏘기가 쓰러짐보다 약하고 짧다"), Rifle.Intensity < DownIntensity && Rifle.Seconds < DownSeconds);
	TestTrue(TEXT("간격 안 → 무음"), CueFor(EGun::Pistol, 0.1f).IsNone());
	TestFalse(TEXT("간격 딱 → 튼다"), CueFor(EGun::Pistol, ShotMinGapSeconds).IsNone());
	TestTrue(TEXT("모르는 총 → 무음"), CueFor(EGun::None, 5.f).IsNone());
	TestTrue(TEXT("음수 시간 → 무음"), CueFor(EGun::Rifle, -1.f).IsNone());
	AddInfo(TEXT("[GunHaptic] 권총 0.5/0.08 · 소총 0.6/0.15 · 간격 안 0"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenStrikeHapticCueTest, "Snarehaven.Capture.Feel.StrikeHapticCue",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenStrikeHapticCueTest::RunTest(const FString& Parameters)
{
	// M63-01 — 근접 0.4/0.06 · 채집 0.3/0.05(근접보다 약한) · 쏘기보다 약하고 짧음 · 간격 0.25초 안·모르는 종류 → 무음.
	using namespace SnareBodyHaptic;
	const FCue Melee = CueFor(EStrike::Melee, 1.f);
	const FCue Gather = CueFor(EStrike::Gather, 1.f);
	TestEqual(TEXT("근접 세기"), Melee.Intensity, 0.4f);
	TestEqual(TEXT("근접 길이"), Melee.Seconds, 0.06f);
	TestEqual(TEXT("채집 세기"), Gather.Intensity, 0.3f);
	TestEqual(TEXT("채집 길이"), Gather.Seconds, 0.05f);
	TestTrue(TEXT("채집이 근접보다 약하다"), Gather.Intensity < Melee.Intensity);
	TestTrue(TEXT("근접이 권총 쏘기보다 약하고 짧다"), Melee.Intensity < PistolShotIntensity && Melee.Seconds < PistolShotSeconds);
	TestTrue(TEXT("간격 안 → 무음"), CueFor(EStrike::Melee, 0.1f).IsNone());
	TestFalse(TEXT("간격 딱 → 튼다"), CueFor(EStrike::Gather, StrikeMinGapSeconds).IsNone());
	TestTrue(TEXT("모르는 종류(맨손 줍기) → 무음"), CueFor(EStrike::None, 5.f).IsNone());
	TestTrue(TEXT("음수 시간 → 무음"), CueFor(EStrike::Melee, -1.f).IsNone());
	AddInfo(TEXT("[StrikeHaptic] 근접 0.4/0.06 · 채집 0.3/0.05 · 간격 안 0"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenEatHapticCueTest, "Snarehaven.Capture.Feel.EatHapticCue",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenEatHapticCueTest::RunTest(const FString& Parameters)
{
	// M64-01 — 먹기 0.3/0.05 · 독 0.4/0.1(먹기보다 세고 길다) · 간격 0.3초 안·모르는 종류 → 무음.
	using namespace SnareBodyHaptic;
	const FCue Food = CueFor(EEat::Food, 1.f);
	const FCue Poison = CueFor(EEat::Poison, 1.f);
	TestEqual(TEXT("먹기 세기"), Food.Intensity, 0.3f);
	TestEqual(TEXT("먹기 길이"), Food.Seconds, 0.05f);
	TestEqual(TEXT("독 세기"), Poison.Intensity, 0.4f);
	TestEqual(TEXT("독 길이"), Poison.Seconds, 0.1f);
	TestTrue(TEXT("독이 먹기보다 세고 길다"), Poison.Intensity > Food.Intensity && Poison.Seconds > Food.Seconds);
	TestTrue(TEXT("간격 안 → 무음"), CueFor(EEat::Food, 0.1f).IsNone());
	TestFalse(TEXT("간격 딱 → 튼다"), CueFor(EEat::Poison, EatMinGapSeconds).IsNone());
	TestTrue(TEXT("모르는 종류 → 무음"), CueFor(EEat::None, 5.f).IsNone());
	TestTrue(TEXT("음수 시간 → 무음"), CueFor(EEat::Food, -1.f).IsNone());
	AddInfo(TEXT("[EatHaptic] 먹기 0.3/0.05 · 독 0.4/0.1 · 간격 안 0"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenHapticsPlayTest, "Snarehaven.Capture.Feel.HapticsPlayIntensity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenHapticsPlayTest::RunTest(const FString& Parameters)
{
	// M61-01 — `Play(조종기, 세기, 초)`: 꺼짐 → 0번 · 켬 → 1번 · 세기·길이 0 이하 → 0번 · 조종기 없음 → 0번. `PlayLight` 는 같은 길로 한 번.
	const bool bWasEnabled = SnareHaptics::IsEnabled();
	UWorld* World = CreateTestWorld();
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	if (!TestNotNull(TEXT("조종기"), Controller))
	{
		DestroyTestWorld(World);
		return false;
	}
	SnareHaptics::SetEnabled(false);
	int32 Before = SnareHaptics::GetPlayCount();
	TestFalse(TEXT("꺼짐 → 안 튼다"), SnareHaptics::Play(Controller, 0.7f, 0.4f));
	TestEqual(TEXT("꺼짐 → 0번"), SnareHaptics::GetPlayCount() - Before, 0);

	SnareHaptics::SetEnabled(true);
	Before = SnareHaptics::GetPlayCount();
	TestTrue(TEXT("켬 → 튼다"), SnareHaptics::Play(Controller, 0.7f, 0.4f));
	TestEqual(TEXT("켬 → 1번"), SnareHaptics::GetPlayCount() - Before, 1);
	TestFalse(TEXT("세기 0 → 안 튼다"), SnareHaptics::Play(Controller, 0.f, 0.4f));
	TestFalse(TEXT("길이 0 → 안 튼다"), SnareHaptics::Play(Controller, 0.7f, 0.f));
	TestFalse(TEXT("조종기 없음 → 안 튼다"), SnareHaptics::Play(nullptr, 0.7f, 0.4f));
	TestEqual(TEXT("안 튼 것은 세지 않는다"), SnareHaptics::GetPlayCount() - Before, 1);
	TestTrue(TEXT("PlayLight 는 그대로 한 번"), SnareHaptics::PlayLight(Controller));
	TestEqual(TEXT("PlayLight 포함 2번"), SnareHaptics::GetPlayCount() - Before, 2);
	AddInfo(FString::Printf(TEXT("[HapticsPlay] 켬 Play+PlayLight %d번 · 꺼짐 0번"), SnareHaptics::GetPlayCount() - Before));

	SnareHaptics::SetEnabled(bWasEnabled);
	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenCaptureBurstTest, "Snarehaven.Capture.Feel.SparkleAndPetFlight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenCaptureBurstTest::RunTest(const FString& Parameters)
{
	// M21-03b2b — 포획이 확정되면 동물 자리에 반짝 조각이 퍼지고, 펫 몸이 잡은 사람 배낭 쪽으로 0.4초에 날아가 사라진다.
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = SpawnTestPlayer(World);
	AAnimalCharacter* Animal = SpawnTestAnimal(World, TestNearDistanceCm);
	if (!TestNotNull(TEXT("플레이어 스폰"), Player) || !TestNotNull(TEXT("동물 스폰"), Animal)
		|| !TestTrue(TEXT("포획 창"), BringToCapturable(Player, Animal)))
	{
		DestroyTestWorld(World);
		return false;
	}
	const FVector AnimalLocation = Animal->GetActorLocation();
	auto CountBursts = [World]()
	{
		int32 Count = 0;
		for (TActorIterator<ASnareCaptureBurstActor> It(World); It; ++It)
		{
			Count += IsValid(*It) && !It->IsActorBeingDestroyed() ? 1 : 0;
		}
		return Count;
	};
	TestEqual(TEXT("포획 전엔 연출 액터 없음"), CountBursts(), 0);
	TestTrue(TEXT("포획 성공"), Player->GetCapture()->TryCaptureInWindowForTest());
	TestEqual(TEXT("포획 한 번에 연출 액터 하나"), CountBursts(), 1);

	TActorIterator<ASnareCaptureBurstActor> It(World);
	ASnareCaptureBurstActor* const Burst = It ? *It : nullptr;
	if (!TestNotNull(TEXT("연출 액터"), Burst))
	{
		DestroyTestWorld(World);
		return false;
	}
	TestEqual(TEXT("반짝 조각 수"), Burst->GetSparkleInstanceCount(), ASnareCaptureBurstActor::SparkleCount);
	AddInfo(FString::Printf(TEXT("[CaptureBurst] 동물 %s · 펫 출발 %s · 배낭 %s"), *AnimalLocation.ToCompactString(),
		*Burst->GetPetLocation().ToCompactString(), *Burst->GetFlightTargetLocation().ToCompactString()));
	TestTrue(TEXT("펫은 동물 자리에서 출발"), Burst->IsPetVisible() && Burst->GetPetLocation().Equals(AnimalLocation, 1.f));
	// 길 가운데가 위로 뜨는 포물선이라 가까워지는 건 수평 거리로 본다.
	const float StartGap = FVector::Dist2D(Burst->GetPetLocation(), Burst->GetFlightTargetLocation());

	// 시험 틱 간격 — 연출 시간을 반으로 나눠 두 번(설계 숫자가 아니라 FlightSeconds 에서 나온다).
	const float HalfFlight = ASnareCaptureBurstActor::FlightSeconds * 0.5f;
	Burst->Tick(HalfFlight);
	const float MidGap = FVector::Dist2D(Burst->GetPetLocation(), Burst->GetFlightTargetLocation());
	TestTrue(TEXT("반쯤엔 위로 떠 있다"), Burst->GetPetLocation().Z > FMath::Lerp(AnimalLocation.Z, Burst->GetFlightTargetLocation().Z, 0.25f));
	TestTrue(TEXT("반쯤엔 아직 날아가는 중"), Burst->IsPetVisible());
	TestTrue(TEXT("반쯤엔 배낭에 더 가깝다"), MidGap < StartGap);
	Burst->Tick(HalfFlight + KINDA_SMALL_NUMBER);
	AddInfo(FString::Printf(TEXT("[CaptureBurst] 반짝 %d개 · 펫 수평 거리 %.0f → %.0f → 도착 %.1fcm · 도착 뒤 보임 %s"),
		ASnareCaptureBurstActor::SparkleCount, StartGap, MidGap,
		FVector::Dist(Burst->GetPetLocation(), Burst->GetFlightTargetLocation()), Burst->IsPetVisible() ? TEXT("예") : TEXT("아니오")));
	TestFalse(TEXT("0.4초에 배낭에 닿아 사라진다"), Burst->IsPetVisible());
	TestTrue(TEXT("닿은 자리 = 배낭"), Burst->GetPetLocation().Equals(Burst->GetFlightTargetLocation(), 1.f));

	Burst->Tick(ASnareCaptureBurstActor::SparkleLifeSeconds);
	TestEqual(TEXT("반짝도 끝나면 액터가 사라진다"), CountBursts(), 0);

	// 실패한 포획(같은 동물 다시)은 연출이 없다.
	TestFalse(TEXT("두 번째 포획은 안 된다"), Player->GetCapture()->TryCaptureTarget());
	TestEqual(TEXT("실패엔 연출 액터 없음"), CountBursts(), 0);
	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenCaptureNoPetIDTest, "Snarehaven.Capture.Pet.MissingIDStaysOff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenCaptureNoPetIDTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = SpawnTestPlayer(World);
	// 행에 펫 ID 가 안 적힌 동물 — 아직 안 채운 행을 대신한다 (Verification.md 7 의 미정 취급).
	AAnimalCharacter* Animal = SpawnTestAnimal(World, TestNearDistanceCm, NAME_None);
	if (!TestNotNull(TEXT("플레이어 스폰"), Player) || !TestNotNull(TEXT("동물 스폰"), Animal))
	{
		DestroyTestWorld(World);
		return false;
	}

	if (!TestTrue(TEXT("임계값까지 때려서 포획 창을 연다"), BringToCapturable(Player, Animal)))
	{
		DestroyTestWorld(World);
		return false;
	}

	// 컴포넌트가 남기는 경고(펫 ID 없음)는 그대로 로그에 두고 AddExpectedError 로 잡지 않는다 —
	// 기존 검사들(Cache.RejectsBadSnapshot)과 같은 취급이고, 경고는 검사를 실패시키지 않는다.
	// 포획 자체는 된다(상태 머신은 ID 를 모른다) — 다만 빈 ID 로 지급 요청을 만들지 않는다.
	TestTrue(TEXT("포획 성공"), Player->GetCapture()->TryCaptureInWindowForTest());
	TestEqual(TEXT("지급 요청은 하나도 쌓이지 않았다"), Player->GetInventory()->GetPendingGrants().Num(), 0);

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenCapturePigTableTest, "Snarehaven.Capture.Table.PigBecomesTablePet",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenCapturePigTableTest::RunTest(const FString& Parameters)
{
	UDataTable* AnimalTable = LoadObject<UDataTable>(nullptr, AnimalTablePath);
	UDataTable* ItemTable = LoadObject<UDataTable>(nullptr, ItemTablePath);
	if (!TestNotNull(TEXT("DT_AnimalTable 로드"), AnimalTable) || !TestNotNull(TEXT("DT_ItemTable 로드"), ItemTable))
	{
		return false;
	}

	// 기대하는 펫 ID 를 코드에 적지 않는다 — 행에서 읽어 그대로 비교한다 (DataDriven.md 3).
	const FAnimalRow* PigRow = AnimalTable->FindRow<FAnimalRow>(FName(PigRowName), TEXT("CapturePigTableTest"));
	if (!TestNotNull(TEXT("돼지 행"), PigRow))
	{
		return false;
	}

	if (!TestFalse(TEXT("돼지 행에 펫 아이템 ID 가 적혀 있다"), PigRow->PetItemID.IsNone()))
	{
		return false;
	}

	UWorld* World = CreateTestWorld();

	// 여기서는 공격 숫자도 갈아 끼우지 않는다 — DESIGN 3.1 확정값과 테이블 값으로 실제 사냥과 같은 길을 탄다.
	ASnarehavenCharacter* Player =
		World->SpawnActor<ASnarehavenCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
	AAnimalCharacter* Pig =
		World->SpawnActor<AAnimalCharacter>(FVector(TestNearDistanceCm, 0.f, 0.f), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("플레이어 스폰"), Player) || !TestNotNull(TEXT("돼지 스폰"), Pig))
	{
		DestroyTestWorld(World);
		return false;
	}

	Pig->SetAnimalData(AnimalTable, FName(PigRowName));
	if (!TestTrue(TEXT("돼지 행으로 초기화"), Pig->InitializeFromTable())
		|| !TestTrue(TEXT("임계값까지 때려서 포획 창을 연다"), BringToCapturable(Player, Pig)))
	{
		DestroyTestWorld(World);
		return false;
	}

	TestTrue(TEXT("포획 성공"), Player->GetCapture()->TryCaptureInWindowForTest());

	const int32 PendingPetCount = Player->GetInventory()->GetPendingCount(PigRow->PetItemID);
	AddInfo(FString::Printf(TEXT("[%s] 포획 → 펫 %s 등록 요청 %d건 (보유 %d)"),
		*PigRow->AnimalID.ToString(), *PigRow->PetItemID.ToString(), PendingPetCount,
		Player->GetInventory()->GetItemCount(PigRow->PetItemID)));
	TestEqual(TEXT("행의 펫 아이템이 등록 요청 대기열에 1건"), PendingPetCount, 1);
	TestEqual(TEXT("보유 수량은 여전히 0 (백엔드 몫)"),
		Player->GetInventory()->GetItemCount(PigRow->PetItemID), 0);

	// 그 펫 ID 가 아이템 테이블에 실제로 있는 행인지도 본다 — 없으면 백엔드가 못 알아듣는다.
	const FItemRow* PetItemRow = nullptr;
	for (const TPair<FName, uint8*>& RowPair : ItemTable->GetRowMap())
	{
		const FItemRow* Row = reinterpret_cast<const FItemRow*>(RowPair.Value);
		if (Row != nullptr && Row->ItemID == PigRow->PetItemID)
		{
			PetItemRow = Row;
			break;
		}
	}

	if (TestNotNull(TEXT("펫 아이템 ID 가 DT_ItemTable 에 있다"), PetItemRow))
	{
		TestTrue(TEXT("그 행은 펫 종류다 (EItemType::Pet)"), PetItemRow->ItemType == EItemType::Pet);
	}

	DestroyTestWorld(World);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
