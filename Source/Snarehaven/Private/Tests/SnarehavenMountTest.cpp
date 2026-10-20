// DESIGN 8.3 탈것 (M6-06 · M13-03) 2층 검사.
// 보는 것: ① 탄 속도 = 걷기 × `DT_WaterActivityTable` 의 `MountSpeedMul` (코드에 숫자 없음)
// ② 탈것 종이 0개면 못 타고 까닭 code 가 남는다 ③ 1개면 탄다 (결정 009) ④ 고른 종의 몸·단 안장만 보인다
// ⑤ 서버 `MOUNT_SPECIES` 가 모두 동물 표에 있다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.Mount; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/AnimSequence.h"
#include "Backend/SnarePlayerSnapshot.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/AnimalRow.h"
#include "Data/WaterActivityRow.h"
#include "Engine/DataTable.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Player/MountComponent.h"
#include "SnarehavenCharacter.h"
#include "UI/CodexScreenWidget.h"
#include "World/WaterRules.h"

namespace
{
	const TCHAR* const AnimalTablePath = TEXT("/Game/Game/Data/Animals/DT_AnimalTable.DT_AnimalTable");
	const TCHAR* const CropTablePath = TEXT("/Game/Game/Data/Ranch/DT_CropProductTable.DT_CropProductTable");
	const FName TestWolf(TEXT("animal.wolf"));
	const FName TestFox(TEXT("animal.fox"));
	const FName TestSaddle(TEXT("item.saddle_hunting"));
	// 검사 전용 자리 (Verification.md 6): 바다가 없는 빈 월드의 뭍 한 점.
	const FVector TestLandPoint(0.f, 0.f, 500.f);

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
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	}

	ASnarehavenCharacter* SpawnPlayer(UWorld* World)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ASnarehavenCharacter* const Player = World->SpawnActor<ASnarehavenCharacter>(TestLandPoint, FRotator::ZeroRotator, Params);
		// 게임모드 없는 검사 월드는 BeginPlay 를 저절로 돌리지 않는다 — 표를 읽는 곳이 BeginPlay 라 직접 부른다.
		if (Player && !Player->HasActorBegunPlay())
		{
			Player->DispatchBeginPlay();
		}
		return Player;
	}

	/** 백엔드(`GetMounts`)가 보내 준 셈 치는 상태 한 벌. 탈것 칸을 바꾸는 길은 이것 하나뿐이다 (CLAUDE.md 8). */
	FSnarePlayerSnapshot MakeMountSnapshot(const TArray<FName>& Mounts)
	{
		FSnarePlayerSnapshot Snapshot;
		Snapshot.PlayFabId = TEXT("PFTESTOWNER");
		Snapshot.FetchedAtUtc = FDateTime::UtcNow();
		Snapshot.Mounts = Mounts;
		return Snapshot;
	}

	/** 늑대 한 종 + 사냥 안장 — 모습 검사(M6-06)가 보던 탄 모습 그대로. */
	FSnarePlayerSnapshot SaddledWolf()
	{
		FSnarePlayerSnapshot Snapshot = MakeMountSnapshot({ TestWolf });
		Snapshot.MountSaddles.Add(TestWolf, TestSaddle);
		return Snapshot;
	}

	/** `var MOUNT_SPECIES = ["animal.wolf", …];` 안의 문자열만 꺼낸다(올라갈 js 를 그대로 읽는다). */
	TArray<FName> ReadServerMountSpecies(const FString& Script)
	{
		TArray<FName> IDs;
		const int32 Start = Script.Find(TEXT("var MOUNT_SPECIES = ["), ESearchCase::CaseSensitive);
		const int32 End = Start == INDEX_NONE ? INDEX_NONE : Script.Find(TEXT("];"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Start);
		TArray<FString> Parts;
		if (End != INDEX_NONE)
		{
			Script.Mid(Start, End - Start).ParseIntoArray(Parts, TEXT("\""), /*CullEmpty=*/false);
		}
		// 따옴표로 자르면 홀수 번째 조각이 문자열 안쪽이다.
		for (int32 Index = 1; Index < Parts.Num(); Index += 2)
		{
			IDs.Add(FName(*Parts[Index]));
		}
		return IDs;
	}

	float CurrentMaxSpeed(ASnarehavenCharacter& Player)
	{
		Player.UpdateMoveSpeed();
		return Player.GetCharacterMovement()->MaxWalkSpeed;
	}

	/** 탄 탈것 몸 = 사람 몸이 아닌, 메시가 붙은 스켈레탈 메시. 안 탔으면 nullptr. */
	USkeletalMeshComponent* FindMountBody(ASnarehavenCharacter& Player)
	{
		USkeletalMeshComponent* MountBody = nullptr;
		TArray<USkeletalMeshComponent*> Meshes;
		Player.GetComponents(Meshes);
		for (USkeletalMeshComponent* const Mesh : Meshes)
		{
			MountBody = Mesh != Player.GetMesh() && Mesh->GetSkeletalMeshAsset() ? Mesh : MountBody;
		}
		return MountBody;
	}

	/**
	 * 걷기 클립을 보는 속도 — 걷기↔달리기 문턱(두 클립 화면 속도의 기하평균, DESIGN 8.3 「탈것 몸 클립」)의 0.8배.
	 * 최고 속도의 몇 분의 일로 잡으면 걷기 속도·탈것 몸 배율이 바뀔 때 달리기 구간으로 넘어간다(루프#1427 전체 시험: 1/4 = 203 > 문턱 181).
	 */
	float WalkBandSpeed(const FWaterActivityRow& Row, float BodyScale)
	{
		return FMath::Sqrt(Row.MountWalkClipSpeedCmPerSecond * BodyScale * Row.MountRunClipSpeedCmPerSecond * BodyScale) * 0.8f;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenMountSpeedFromTableTest, "Snarehaven.Mount.Speed.FromTable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSnarehavenMountSpeedFromTableTest::RunTest(const FString& Parameters)
{
	const FWaterActivityRow* const Row = SnareWater::LoadRow();
	if (!TestNotNull(TEXT("실제 물 표 (MountSpeedMul 포함)"), Row))
	{
		return false;
	}
	UWorld* const World = CreateTestWorld();
	ASnarehavenCharacter* const Player = SpawnPlayer(World);
	UMountComponent* const Mount = Player ? Player->FindComponentByClass<UMountComponent>() : nullptr;
	if (!TestNotNull(TEXT("탈것 컴포넌트"), Mount))
	{
		DestroyTestWorld(World);
		return false;
	}

	const float WalkSpeed = CurrentMaxSpeed(*Player);
	Mount->ToggleMountWith(SaddledWolf());
	TestTrue(TEXT("탔다"), Mount->IsMounted());
	const float RideSpeed = CurrentMaxSpeed(*Player);
	TestEqual(TEXT("탄 속도 = 걷기 × 표 MountSpeedMul"), RideSpeed, WalkSpeed * Row->MountSpeedMul, 0.01f);
	AddInfo(FString::Printf(TEXT("[Mount] 걷기 %.1f → 탈것 %.1f cm/s (표 배율 %.2f)"), WalkSpeed, RideSpeed, Row->MountSpeedMul));
	// DESIGN 8.3 — 탄 속도는 발로 달리기의 2배 이상 (사용자 기기 시험 2026-09-25). 클립 속도는 protected 라 리플렉션으로 읽는다.
	const FFloatProperty* const WalkProp = FindFProperty<FFloatProperty>(ASnarehavenCharacter::StaticClass(), TEXT("WalkClipSpeedCmPerSecond"));
	const FFloatProperty* const RunProp = FindFProperty<FFloatProperty>(ASnarehavenCharacter::StaticClass(), TEXT("RunClipSpeedCmPerSecond"));
	if (TestNotNull(TEXT("걷기 클립 속도 칸"), WalkProp) && TestNotNull(TEXT("달리기 클립 속도 칸"), RunProp))
	{
		const float RunSpeed = WalkSpeed * RunProp->GetPropertyValue_InContainer(Player) / WalkProp->GetPropertyValue_InContainer(Player);
		TestTrue(TEXT("탄 속도 ≥ 달리기 × 2"), RideSpeed >= RunSpeed * 2.f);
		AddInfo(FString::Printf(TEXT("[Mount] 달리기 %.1f → 탈것 %.1f cm/s (달리기의 %.2f배)"), RunSpeed, RideSpeed, RideSpeed / RunSpeed));
	}

	Mount->ToggleMountWith(SaddledWolf());
	TestFalse(TEXT("같은 버튼으로 내렸다"), Mount->IsMounted());
	TestEqual(TEXT("내리면 걷기 속도로"), CurrentMaxSpeed(*Player), WalkSpeed, 0.01f);

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenMountRefusesNoMountTest, "Snarehaven.Mount.Gate.RefusesNoMount",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSnarehavenMountRefusesNoMountTest::RunTest(const FString& Parameters)
{
	UWorld* const World = CreateTestWorld();
	ASnarehavenCharacter* const Player = SpawnPlayer(World);
	UMountComponent* const Mount = Player ? Player->FindComponentByClass<UMountComponent>() : nullptr;
	if (!TestNotNull(TEXT("탈것 컴포넌트"), Mount))
	{
		DestroyTestWorld(World);
		return false;
	}
	const float WalkSpeed = CurrentMaxSpeed(*Player);

	// 탈것 종 0개 → 거절, 까닭이 남는다. 도감을 다 채운 스냅샷이어도 목줄을 안 바꿨으면 못 탄다(결정 009).
	FSnarePlayerSnapshot NoMount = MakeMountSnapshot({});
	NoMount.CodexEntries = UCodexScreenWidget::CollectMountCodexIDs(LoadObject<UDataTable>(nullptr, AnimalTablePath),
		LoadObject<UDataTable>(nullptr, CropTablePath), LoadObject<UDataTable>(nullptr, SnareWater::FishingTablePath));
	TestTrue(TEXT("도감 칸을 다 채웠다"), NoMount.CodexEntries.Num() > 0);
	Mount->ToggleMountWith(NoMount);
	TestFalse(TEXT("탈것 종 0개면 못 탄다"), Mount->IsMounted());
	TestEqual(TEXT("거절 까닭 = 탈것 없음"), Mount->GetLastMessage(), FString(TEXT("mount_locked")));
	TestEqual(TEXT("속도는 걷기 그대로"), CurrentMaxSpeed(*Player), WalkSpeed, 0.01f);

	// 백엔드 스냅샷을 아직 못 받았으면(주인 없음) 탈것 칸을 믿지 않는다.
	Mount->ToggleMountWith(FSnarePlayerSnapshot());
	TestFalse(TEXT("스냅샷 없으면 못 탄다"), Mount->IsMounted());
	TestEqual(TEXT("거절 까닭 = 도감 못 받음"), Mount->GetLastMessage(), FString(TEXT("mount_no_codex")));

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenMountAllowsOneMountTest, "Snarehaven.Mount.Gate.AllowsOneMount",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSnarehavenMountAllowsOneMountTest::RunTest(const FString& Parameters)
{
	UWorld* const World = CreateTestWorld();
	ASnarehavenCharacter* const Player = SpawnPlayer(World);
	UMountComponent* const Mount = Player ? Player->FindComponentByClass<UMountComponent>() : nullptr;
	if (!TestNotNull(TEXT("탈것 컴포넌트"), Mount))
	{
		DestroyTestWorld(World);
		return false;
	}

	// 0개로 거절된 뒤 1개가 되면 탄다 — 까닭 줄도 지워진다. 도감은 비어 있어도 된다(결정 009).
	Mount->ToggleMountWith(MakeMountSnapshot({}));
	TestFalse(TEXT("0개 → 못 탄다"), Mount->IsMounted());
	Mount->ToggleMountWith(MakeMountSnapshot({ TestWolf }));
	TestTrue(TEXT("탈것 종 1개면 탄다"), Mount->IsMounted());
	TestTrue(TEXT("거절 까닭이 지워졌다"), Mount->GetLastMessage().IsEmpty());
	TestEqual(TEXT("탄 종 = 가진 그 종"), Mount->GetMountSpecies(), TestWolf);
	AddInfo(FString::Printf(TEXT("[Mount] 탈것 0종 → 거절 · 1종(%s) → 탐"), *Mount->GetMountSpecies().ToString()));

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenMountRiderSitsOnTopTest, "Snarehaven.Mount.Body.RiderSitsOnTop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSnarehavenMountRiderSitsOnTopTest::RunTest(const FString& Parameters)
{
	// 3층 첫 캡처 결함: 늑대가 캡슐 가운데 붙어 사람 몸에 가려 꼬리만 보였다.
	// 보는 것: 타면 늑대 몸이 생기고, 길이 = 사람 너비 × 표 배율, 사람 발이 늑대 등(바닥~키 사이)으로 올라간다. 내리면 둘 다 되돌아간다.
	const FWaterActivityRow* const Row = SnareWater::LoadRow();
	UWorld* const World = CreateTestWorld();
	ASnarehavenCharacter* const Player = SpawnPlayer(World);
	UMountComponent* const Mount = Player ? Player->FindComponentByClass<UMountComponent>() : nullptr;
	USkeletalMeshComponent* const Rider = Player ? Player->GetMesh() : nullptr;
	if (!TestNotNull(TEXT("실제 물 표"), Row) || !TestNotNull(TEXT("탈것 컴포넌트"), Mount) || !TestNotNull(TEXT("사람 몸"), Rider)
		|| !TestNotNull(TEXT("사람 메시"), Rider->GetSkeletalMeshAsset()))
	{
		DestroyTestWorld(World);
		return false;
	}
	const float FootZ = static_cast<float>(Rider->GetRelativeLocation().Z);
	const FVector RiderExtent = Rider->GetSkeletalMeshAsset()->GetBounds().BoxExtent * Rider->GetRelativeScale3D().X;
	const float RiderWidth = 2.f * FMath::Min(RiderExtent.X, RiderExtent.Y);

	Mount->ToggleMountWith(SaddledWolf());
	USkeletalMeshComponent* const MountBody = FindMountBody(*Player);
	if (!TestNotNull(TEXT("탄 늑대 몸"), MountBody))
	{
		DestroyTestWorld(World);
		return false;
	}
	const FBoxSphereBounds MountBounds = MountBody->GetSkeletalMeshAsset()->GetBounds();
	const float Scale = static_cast<float>(MountBody->GetRelativeScale3D().X);
	const float MountLength = 2.f * FMath::Max(MountBounds.BoxExtent.X, MountBounds.BoxExtent.Y) * Scale;
	const float MountHeight = 2.f * MountBounds.BoxExtent.Z * Scale;
	TestEqual(TEXT("늑대 길이 = 사람 너비 × 표 배율"), MountLength, RiderWidth * Row->MountLengthPerRiderWidth, 0.5f);
	const float Lift = static_cast<float>(Rider->GetRelativeLocation().Z) - FootZ;
	TestTrue(TEXT("사람이 늑대 등에 올라앉는다 (바닥보다 높고 늑대 키보다 낮게)"), Lift > 0.f && Lift <= MountHeight + 0.5f);
	TestTrue(TEXT("늑대가 사람보다 길다 — 탑다운에서 앞뒤가 보인다"), MountLength > RiderWidth);
	// 60° 카메라에서 앉은 사람 머리가 늑대 머리·어깨를 덮던 결함(3층 `m606b_mount_ride`) — 앉는 자리를 꼬리 쪽(−X)으로 물린다.
	const float SeatBack = -static_cast<float>(Rider->GetRelativeLocation().X);
	TestEqual(TEXT("탄 사람이 늑대 몸 길이 × 표 값만큼 꼬리 쪽에 앉는다"), SeatBack, MountLength * Row->MountSeatBackOfLength, 0.5f);
	AddInfo(FString::Printf(TEXT("[Mount] 사람 너비 %.0f → 늑대 길이 %.0f · 키 %.0f · 올림 %.0f · 뒤로 %.0f cm"), RiderWidth, MountLength, MountHeight, Lift, SeatBack));

	Mount->ToggleMountWith(SaddledWolf());
	TestEqual(TEXT("내리면 사람 발이 바닥으로"), static_cast<float>(Rider->GetRelativeLocation().Z), FootZ, 0.01f);
	TestEqual(TEXT("내리면 사람이 캡슐 가운데로"), static_cast<float>(Rider->GetRelativeLocation().X), 0.f, 0.01f);
	TestFalse(TEXT("내리면 늑대 몸이 사라진다"), IsValid(MountBody) && MountBody->IsRegistered());

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenMountRideClipAndSaddleTest, "Snarehaven.Mount.Body.RideClipAndSaddle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSnarehavenMountRideClipAndSaddleTest::RunTest(const FString& Parameters)
{
	// M6-06 모습: 탄 사람 클립 = `A_Player_Ride` · 탈것 몸이 속도에 따라 대기/걷기/달리기(표 행 클립) · 안장이 등 위 · 내리면 둘 다 사라진다.
	const UDataTable* const AnimalTable = LoadObject<UDataTable>(nullptr, AnimalTablePath);
	const FAnimalRow* const WolfRow = AnimalTable ? AnimalTable->FindRow<FAnimalRow>(TEXT("Animal_Wolf"), TEXT("MountTest")) : nullptr;
	const FWaterActivityRow* const WaterRow = SnareWater::LoadRow();
	UWorld* const World = CreateTestWorld();
	ASnarehavenCharacter* const Player = SpawnPlayer(World);
	UMountComponent* const Mount = Player ? Player->FindComponentByClass<UMountComponent>() : nullptr;
	if (!TestNotNull(TEXT("늑대 행"), WolfRow) || !TestNotNull(TEXT("실제 물 표"), WaterRow) || !TestNotNull(TEXT("탈것 컴포넌트"), Mount))
	{
		DestroyTestWorld(World);
		return false;
	}
	UAnimSequence* const Idle = WolfRow->IdleAnimRef.LoadSynchronous();
	UAnimSequence* const Walk = WolfRow->WalkAnimRef.LoadSynchronous();
	UAnimSequence* const Run = WolfRow->FleeAnimRef.LoadSynchronous();
	if (!TestNotNull(TEXT("늑대 대기 클립"), Idle) || !TestNotNull(TEXT("늑대 걷기 클립"), Walk) || !TestNotNull(TEXT("늑대 도주 클립"), Run))
	{
		DestroyTestWorld(World);
		return false;
	}

	Mount->ToggleMountWith(SaddledWolf());
	USkeletalMeshComponent* const MountBody = FindMountBody(*Player);
	if (!TestNotNull(TEXT("탄 늑대 몸"), MountBody))
	{
		DestroyTestWorld(World);
		return false;
	}
	const float WalkSpeed = WalkBandSpeed(*WaterRow, static_cast<float>(MountBody->GetRelativeScale3D().X));
	UCharacterMovementComponent* const Movement = Player->GetCharacterMovement();
	const float RideMax = CurrentMaxSpeed(*Player);
	auto BodyAt = [&](float Speed)
	{
		Movement->Velocity = FVector(Speed, 0.f, 0.f);
		Mount->TickComponent(0.f, LEVELTICK_All, nullptr);
		return Mount->GetMountBodyAnim();
	};

	Movement->Velocity = FVector::ZeroVector;
	Player->UpdateBodyAnimation();
	const UAnimSequence* const RiderAnim = Player->GetCurrentBodyAnim();
	TestEqual(TEXT("탄 사람 클립 = A_Player_Ride"), GetNameSafe(RiderAnim), FString(TEXT("A_Player_Ride")));
	TestEqual(TEXT("멈춤 → 늑대 대기"), GetNameSafe(BodyAt(0.f)), Idle->GetName());
	// 탈것이 달리기의 2배 넘게 빨라(8.3, ×8.0) 최고 속도의 몇 분의 일은 달리기 구간일 수 있다 — 걷기는 문턱 밑(`WalkBandSpeed`)에서 본다.
	TestTrue(TEXT("걷기 보는 속도가 움직임 문턱 위·최고 속도 밑"), WalkSpeed >= Player->GetMoveAnimEnterSpeed() && WalkSpeed < RideMax);
	TestEqual(TEXT("문턱 밑 속도 → 늑대 걷기"), GetNameSafe(BodyAt(WalkSpeed)), Walk->GetName());
	TestEqual(TEXT("최고 속도 → 늑대 도주(달리기)"), GetNameSafe(BodyAt(RideMax)), Run->GetName());
	TestEqual(TEXT("다시 멈춤 → 늑대 대기"), GetNameSafe(BodyAt(0.f)), Idle->GetName());
	Movement->Velocity = FVector(RideMax, 0.f, 0.f);
	Player->UpdateBodyAnimation();
	TestEqual(TEXT("달려도 탄 사람 클립은 그대로"), GetNameSafe(Player->GetCurrentBodyAnim()), FString(TEXT("A_Player_Ride")));

	UStaticMeshComponent* const Saddle = Mount->GetSaddle();
	USkeletalMeshComponent* const Rider = Player->GetMesh();
	if (TestNotNull(TEXT("안장 컴포넌트"), Saddle) && TestTrue(TEXT("안장 메시"), Saddle->GetStaticMesh() != nullptr))
	{
		const float SaddleZ = static_cast<float>(Saddle->GetRelativeLocation().Z);
		const float FootZ = -Player->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
		const float RiderZ = static_cast<float>(Rider->GetRelativeLocation().Z);
		TestTrue(TEXT("안장이 늑대 등 위 (발 바닥보다 높고 탄 사람 엉덩이 밑)"), SaddleZ > FootZ && SaddleZ < RiderZ);
		AddInfo(FString::Printf(TEXT("[Mount] 탄 사람 %s · 늑대 %s/%s/%s · 안장 높이 %.0f · 엉덩이 %.0f cm (바닥 기준)"),
			*GetNameSafe(RiderAnim), *Idle->GetName(), *Walk->GetName(), *Run->GetName(), SaddleZ - FootZ, RiderZ - FootZ));
	}

	Mount->ToggleMountWith(SaddledWolf());
	TestNull(TEXT("내리면 안장이 사라진다"), Mount->GetSaddle());
	TestFalse(TEXT("내린 안장은 등록 해제"), IsValid(Saddle) && Saddle->IsRegistered());
	TestNull(TEXT("내리면 늑대 몸(클립)이 사라진다"), Mount->GetMountBodyAnim());
	Movement->Velocity = FVector::ZeroVector;
	Player->UpdateBodyAnimation();
	TestNotEqual(TEXT("내리면 탄 사람 클립이 풀린다"), GetNameSafe(Player->GetCurrentBodyAnim()), FString(TEXT("A_Player_Ride")));

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenMountPlayRateTest, "Snarehaven.Mount.Body.PlayRateMatchesSpeed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSnarehavenMountPlayRateTest::RunTest(const FString& Parameters)
{
	// 발 미끄러짐 결함: 탈것 몸 클립이 속도와 상관없이 1배로 돌았다.
	// 보는 것: 재생 속도 = 실제 속도 ÷ (표의 클립 공간 속도 × 탈것 몸 배율) — 걷기·달리기 둘 다, 멈춤은 1배.
	const FWaterActivityRow* const Row = SnareWater::LoadRow();
	UWorld* const World = CreateTestWorld();
	ASnarehavenCharacter* const Player = SpawnPlayer(World);
	UMountComponent* const Mount = Player ? Player->FindComponentByClass<UMountComponent>() : nullptr;
	if (!TestNotNull(TEXT("실제 물 표"), Row) || !TestNotNull(TEXT("탈것 컴포넌트"), Mount))
	{
		DestroyTestWorld(World);
		return false;
	}
	Mount->ToggleMountWith(SaddledWolf());
	USkeletalMeshComponent* const MountBody = FindMountBody(*Player);
	if (!TestNotNull(TEXT("탄 늑대 몸"), MountBody))
	{
		DestroyTestWorld(World);
		return false;
	}
	const float Scale = static_cast<float>(MountBody->GetRelativeScale3D().X);
	const float WalkClip = Row->MountWalkClipSpeedCmPerSecond * Scale;
	const float RunClip = Row->MountRunClipSpeedCmPerSecond * Scale;
	const float RideMax = CurrentMaxSpeed(*Player);
	UCharacterMovementComponent* const Movement = Player->GetCharacterMovement();
	auto RateAt = [&](float Speed)
	{
		Movement->Velocity = FVector(Speed, 0.f, 0.f);
		Mount->TickComponent(0.f, LEVELTICK_All, nullptr);
		return MountBody->GetPlayRate();
	};

	const float WalkSpeed = WalkBandSpeed(*Row, Scale); // `RideClipAndSaddle` 이 걷기 클립을 보는 속도
	const float WalkRate = RateAt(WalkSpeed);
	TestEqual(TEXT("걷기: 재생 속도 = 속도 ÷ 걷기 클립 속도"), WalkRate, WalkSpeed / WalkClip, 0.01f);
	const float RunRate = RateAt(RideMax);
	TestEqual(TEXT("최고 속도(달리기): 재생 속도 = 속도 ÷ 달리기 클립 속도"), RunRate, RideMax / RunClip, 0.01f);
	TestEqual(TEXT("멈춤: 대기 클립 1배"), RateAt(0.f), 1.f, 0.01f);
	AddInfo(FString::Printf(TEXT("[Mount] 몸 배율 %.2f · 걷기 클립 %.0f · 달리기 클립 %.0f cm/s → 속도 %.0f 재생 %.2f배 · 속도 %.0f 재생 %.2f배"),
		Scale, WalkClip, RunClip, WalkSpeed, WalkRate, RideMax, RunRate));

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenMountSpeciesPickTest, "Snarehaven.Mount.Species.PickAndSaddle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSnarehavenMountSpeciesPickTest::RunTest(const FString& Parameters)
{
	// 결정 009: 탈 종 = 고른 종, 없으면 첫 종 · 몸 = 그 종의 동물 표 행 · 안장 = 그 종에 단 것만(안 달았으면 맨등).
	const UDataTable* const AnimalTable = LoadObject<UDataTable>(nullptr, AnimalTablePath);
	const FAnimalRow* const FoxRow = AnimalTable ? AnimalTable->FindRow<FAnimalRow>(TEXT("Animal_Fox"), TEXT("MountTest")) : nullptr;
	UWorld* const World = CreateTestWorld();
	ASnarehavenCharacter* const Player = SpawnPlayer(World);
	UMountComponent* const Mount = Player ? Player->FindComponentByClass<UMountComponent>() : nullptr;
	if (!TestNotNull(TEXT("여우 행"), FoxRow) || !TestNotNull(TEXT("탈것 컴포넌트"), Mount))
	{
		DestroyTestWorld(World);
		return false;
	}
	FSnarePlayerSnapshot Two = MakeMountSnapshot({ TestWolf, TestFox });
	Two.MountSaddles.Add(TestWolf, TestSaddle);
	TestEqual(TEXT("고른 적 없으면 첫 종"), Mount->GetRideSpecies(Two), TestWolf);
	Mount->CycleMountSpecies(Two);
	TestEqual(TEXT("바꾸기 한 번 → 둘째 종"), Mount->GetRideSpecies(Two), TestFox);

	Mount->ToggleMountWith(Two);
	USkeletalMeshComponent* const MountBody = FindMountBody(*Player);
	TestEqual(TEXT("탄 종 = 고른 여우"), Mount->GetMountSpecies(), TestFox);
	TestTrue(TEXT("몸 = 여우 행 메시"), MountBody && MountBody->GetSkeletalMeshAsset() == FoxRow->MeshRef.LoadSynchronous());
	TestNull(TEXT("안장 안 단 여우는 맨등"), Mount->GetSaddle());
	Mount->ToggleMountWith(Two);

	Mount->CycleMountSpecies(Two);
	Mount->ToggleMountWith(Two);
	TestEqual(TEXT("한 번 더 바꾸면 첫 종으로 돈다"), Mount->GetMountSpecies(), TestWolf);
	TestNotNull(TEXT("안장 단 늑대는 안장이 보인다"), Mount->GetSaddle());
	Mount->ToggleMountWith(Two);

	// 고른 종이 스냅샷에서 사라지면(다른 계정 등) 첫 종으로 돌아간다.
	TestEqual(TEXT("고른 종이 없으면 첫 종"), Mount->GetRideSpecies(MakeMountSnapshot({ TestFox })), TestFox);
	AddInfo(FString::Printf(TEXT("[Mount] 두 종 고르기: %s → %s · 늑대 안장 %s"), *TestWolf.ToString(), *TestFox.ToString(), *TestSaddle.ToString()));
	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenMountSpeciesServerTest, "Snarehaven.Mount.Species.MatchesServer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSnarehavenMountSpeciesServerTest::RunTest(const FString& Parameters)
{
	// 서버가 목줄로 내주는 종마다 동물 표에 몸이 있어야 한다 — 없으면 탄 모습이 늑대 자리표시로 떨어진다.
	const UDataTable* const AnimalTable = LoadObject<UDataTable>(nullptr, AnimalTablePath);
	FString Script;
	if (!TestNotNull(TEXT("DT_AnimalTable"), AnimalTable)
		|| !TestTrue(TEXT("CloudScript 읽기"), FFileHelper::LoadFileToString(Script, *(FPaths::ProjectDir() / TEXT("tools/playfab/cloudscript_main.js")))))
	{
		return false;
	}
	TSet<FName> ClientIDs;
	AnimalTable->ForeachRow<FAnimalRow>(TEXT("MountSpeciesTest"), [&ClientIDs](const FName&, const FAnimalRow& Row)
	{
		ClientIDs.Add(Row.AnimalID);
	});
	auto CountMissing = [&ClientIDs](const TArray<FName>& Species)
	{
		int32 Missing = 0;
		for (const FName& ID : Species)
		{
			Missing += ClientIDs.Contains(ID) ? 0 : 1;
		}
		return Missing;
	};
	const TArray<FName> Server = ReadServerMountSpecies(Script);
	TestTrue(TEXT("서버 종 목록을 읽었다"), Server.Num() > 0);
	TestEqual(TEXT("서버 MOUNT_SPECIES 가 모두 동물 표에 있다"), CountMissing(Server), 0);
	// 반대 방향 — 표에 없는 종이 끼면 운다(항상 0 을 내는 셈이 아니다).
	TArray<FName> Tampered = Server;
	Tampered.Add(FName(TEXT("animal.test_extra")));
	TestEqual(TEXT("없는 종 하나 → 1개 빠짐"), CountMissing(Tampered), 1);
	AddInfo(FString::Printf(TEXT("[Mount] 서버 종 %d · 동물 표 %d행"), Server.Num(), ClientIDs.Num()));
	return true;
}

#endif
