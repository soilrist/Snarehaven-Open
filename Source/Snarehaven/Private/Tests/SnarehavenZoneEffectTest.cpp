// M36-01 2층 검사 — `UZoneEffectComponent` 가 칸 특성을 몸에 거는가 (DESIGN 1.3 존 특성).
// 보는 것 다섯: ① 위험 칸 안 = `SetZoneHealthDrain(표 값)`, 밖 = 0 ② 입장료 칸 첫 진입 = `PayFerry {dest}` 1번
// (응답 전 재요청 0) ③ 거절 = 마지막 무료 자리로 밀림 + 사유 글자, 서버에 다른 요청 0 ④ 수락 = 다시 들어와도 요청 0
// ⑤ 서버 요금표 이름 없는 입장료 칸 = 요청 0·안 막음.
// 칸 표는 **검사 전용 행**이다 — 실제 `DT_ZoneTable` 숫자가 바뀌어도 안 깨진다 (Verification 6).
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.Zone.Effect; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Backend/SnareBackendSubsystem.h"
#include "Components/SceneComponent.h"
#include "Data/ZoneRow.h"
#include "Dom/JsonObject.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Player/PlayerVitalsComponent.h"
#include "World/ZoneEffectComponent.h"
#include "World/ZoneTravelComponent.h"

namespace SnarehavenZoneEffectTest
{
	using FDone = USnareBackendSubsystem::FServerCallDone;

	// 검사 전용 칸 — X 축에 나란히, 반변 500. 숫자는 판정과 무관한 시험 값이다.
	constexpr float TestHalfExtentCm = 500.f;
	constexpr float TestDrainPerSecond = 3.5f;
	const FVector OutsideA(-5000.f, 0.f, 0.f);
	const FVector OutsideB(-3000.f, 0.f, 0.f);
	const FVector ColdCenter(2000.f, 0.f, 0.f);
	const FVector FeeCenter(6000.f, 0.f, 0.f);
	const FVector NoDestCenter(10000.f, 0.f, 0.f);
	const FName ColdID(TEXT("zone.test_cold"));
	// 요금 칸은 서버 `FERRY_FARES` 가 아는 이름이어야 요청이 간다 — 가을섬 숲(`island`).
	const FName FeeID(TEXT("zone.forest"));
	const FName NoDestID(TEXT("zone.test_nodest_island"));

	struct FFakeServer
	{
		TArray<FString> Functions;
		TArray<TSharedPtr<FJsonObject>> ArgsLog;
		TArray<FDone> Waiting;

		void Install()
		{
			USnareBackendSubsystem::TestServerOverride =
				[this](const FString& Name, const TSharedPtr<FJsonObject>& Args, FDone OnDone)
				{
					Functions.Add(Name);
					ArgsLog.Add(Args);
					Waiting.Add(MoveTemp(OnDone));
				};
		}

		~FFakeServer() { USnareBackendSubsystem::TestServerOverride = nullptr; }

		FDone Pop()
		{
			FDone Done = MoveTemp(Waiting[0]);
			Waiting.RemoveAt(0);
			return Done;
		}

		/** CloudScript `fail("not_enough_coins", …, {need, have})` 모양 — HTTP 는 성공이다. */
		void AnswerNotEnoughCoins()
		{
			const TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
			Response->SetStringField(TEXT("error"), TEXT("코인이 모자란다"));
			Response->SetStringField(TEXT("code"), TEXT("not_enough_coins"));
			Response->SetNumberField(TEXT("need"), 9);
			Response->SetNumberField(TEXT("have"), 1);
			Pop()(true, Response, FString());
		}

		void AnswerPaid()
		{
			Pop()(true, MakeShared<FJsonObject>(), FString());
		}
	};

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

	FZoneRow MakeRegion(FName ZoneID, FName LevelName, const FVector& Center)
	{
		FZoneRow Row;
		Row.ZoneID = ZoneID;
		Row.LevelName = LevelName;
		Row.RegionCenterCm = FVector2D(Center.X, Center.Y);
		Row.RegionHalfExtentCm = TestHalfExtentCm;
		return Row;
	}

	/** 이 검사 월드의 맵 이름을 쓰는 칸 셋 — 컴포넌트와 같은 길(`RemovePIEPrefix(GetMapName())`)로 이름을 맞춘다. */
	UDataTable* MakeTestTable(const UWorld& World)
	{
		const FName Level(*UWorld::RemovePIEPrefix(World.GetMapName()));
		UDataTable* const Table = NewObject<UDataTable>(GetTransientPackage());
		Table->RowStruct = FZoneRow::StaticStruct();

		FZoneRow Cold = MakeRegion(ColdID, Level, ColdCenter);
		Cold.HealthDrainPerSecond = TestDrainPerSecond;
		Table->AddRow(TEXT("Test_Cold"), Cold);

		FZoneRow Fee = MakeRegion(FeeID, Level, FeeCenter);
		Fee.bChargesEntryFee = true;
		Table->AddRow(TEXT("Test_Fee"), Fee);

		FZoneRow NoDest = MakeRegion(NoDestID, Level, NoDestCenter);
		NoDest.bChargesEntryFee = true;
		Table->AddRow(TEXT("Test_NoDest"), NoDest);
		return Table;
	}

	struct FRig
	{
		UWorld* World = nullptr;
		APawn* Pawn = nullptr;
		UPlayerVitalsComponent* Vitals = nullptr;
		UZoneEffectComponent* Effect = nullptr;

		bool Build()
		{
			World = CreateTestWorld();
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Pawn = World->SpawnActor<APawn>(APawn::StaticClass(), FTransform::Identity, Params);
			if (Pawn == nullptr)
			{
				return false;
			}
			// 맨 APawn 은 뿌리가 없어 자리를 못 옮긴다 — 장면 컴포넌트 하나를 뿌리로.
			USceneComponent* const Root = NewObject<USceneComponent>(Pawn, TEXT("TestRoot"));
			Pawn->SetRootComponent(Root);
			Root->RegisterComponent();
			Vitals = NewObject<UPlayerVitalsComponent>(Pawn, TEXT("TestVitals"));
			Vitals->RegisterComponent();
			Effect = NewObject<UZoneEffectComponent>(Pawn, TEXT("TestZoneEffect"));
			Effect->RegisterComponent();
			Effect->SetZoneTableForTest(MakeTestTable(*World));

			// 요금은 그 폰을 조종하는 기계만 묻는다 — 넷 드라이버 없는 월드는 `ULocalPlayer` 붙은 조종기만 "이 기기" (CaptureTest 와 같다).
			APlayerController* const Controller = World->SpawnActor<APlayerController>();
			if (Controller == nullptr)
			{
				return false;
			}
			Controller->Player = NewObject<ULocalPlayer>(GEngine, ULocalPlayer::StaticClass());
			Controller->Possess(Pawn);
			return Pawn->IsLocallyControlled();
		}

		void StandAt(const FVector& At)
		{
			Pawn->SetActorLocation(At, false, nullptr, ETeleportType::TeleportPhysics);
			Effect->UpdateZoneEffects();
		}

		~FRig()
		{
			if (World != nullptr)
			{
				DestroyTestWorld(World);
			}
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenZoneEffectDrainTest, "Snarehaven.Zone.Effect.HealthDrain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenZoneEffectDrainTest::RunTest(const FString& Parameters)
{
	using namespace SnarehavenZoneEffectTest;
	FFakeServer Server;
	Server.Install();
	FRig Rig;
	if (!TestTrue(TEXT("검사 폰·이 기기 조종"), Rig.Build()))
	{
		return false;
	}

	// ① 칸 밖 0 → 위험 칸 안 = 표 값 → 다시 밖 0
	Rig.StandAt(OutsideA);
	TestEqual(TEXT("칸 밖 = 칸 없음"), Rig.Effect->GetCurrentRegionID(), FName(NAME_None));
	TestEqual(TEXT("칸 밖 체력 감소 0"), Rig.Vitals->GetZoneHealthDrain(), 0.f);
	Rig.StandAt(ColdCenter);
	TestEqual(TEXT("위험 칸 판정"), Rig.Effect->GetCurrentRegionID(), ColdID);
	TestEqual(TEXT("위험 칸 안 체력 감소 = 표 값"), Rig.Vitals->GetZoneHealthDrain(), TestDrainPerSecond);
	Rig.StandAt(OutsideB);
	TestEqual(TEXT("칸을 나오면 체력 감소 0"), Rig.Vitals->GetZoneHealthDrain(), 0.f);
	TestEqual(TEXT("위험 칸은 서버를 부르지 않는다"), Server.Functions.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenZoneEffectFeeTest, "Snarehaven.Zone.Effect.EntryFee",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenZoneEffectFeeTest::RunTest(const FString& Parameters)
{
	using namespace SnarehavenZoneEffectTest;
	FFakeServer Server;
	Server.Install();
	FRig Rig;
	if (!TestTrue(TEXT("검사 폰·이 기기 조종"), Rig.Build()))
	{
		return false;
	}
	const FString WantDest = UZoneTravelComponent::GetFerryDestKey(FeeID);
	if (!TestFalse(TEXT("요금 칸은 서버 dest 이름이 있다"), WantDest.IsEmpty()))
	{
		return false;
	}

	// ② 첫 진입 = PayFerry 1번, 응답 전 틱이 더 돌아도 재요청 0
	Rig.StandAt(OutsideB);
	Rig.StandAt(FeeCenter);
	Rig.Effect->UpdateZoneEffects();
	Rig.Effect->UpdateZoneEffects();
	if (!TestEqual(TEXT("첫 진입 요청 1번 (응답 전 재요청 0)"), Server.Functions.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("요청 함수 = PayFerry"), Server.Functions[0], FString(TEXT("PayFerry")));
	TestEqual(TEXT("요청 dest = 요금표 이름"), Server.ArgsLog[0]->GetStringField(TEXT("dest")), WantDest);

	// ③ 거절 = 마지막 무료 자리로 밀림 + 사유 글자, 서버엔 다른 요청이 안 간다(잔액·인벤토리는 서버 몫)
	Server.AnswerNotEnoughCoins();
	TestEqual(TEXT("거절 → 마지막 무료 자리로 밀림"), Rig.Pawn->GetActorLocation(), OutsideB);
	TestFalse(TEXT("거절 사유 글자가 비지 않음"), Rig.Effect->GetLastMessage().IsEmpty());
	AddInfo(FString::Printf(TEXT("거절 문구 [%s]"), *Rig.Effect->GetLastMessage().ToString()));
	TestEqual(TEXT("거절 뒤 서버 요청 그대로 1번 (지급·차감 요청 0)"), Server.Functions.Num(), 1);

	// ④ 다시 들어오면 또 묻고, 수락하면 그 칸은 다시 들어와도 안 묻는다
	Rig.StandAt(FeeCenter);
	if (!TestEqual(TEXT("거절 뒤 재진입은 다시 묻는다"), Server.Functions.Num(), 2))
	{
		return false;
	}
	Server.AnswerPaid();
	TestEqual(TEXT("수락 → 밀리지 않음"), Rig.Pawn->GetActorLocation(), FeeCenter);
	TestTrue(TEXT("수락 → 사유 글자 비움"), Rig.Effect->GetLastMessage().IsEmpty());
	Rig.Effect->UpdateZoneEffects();
	Rig.StandAt(OutsideA);
	Rig.StandAt(FeeCenter);
	TestEqual(TEXT("수락한 칸은 다시 들어와도 요청 0"), Server.Functions.Num(), 2);
	TestEqual(TEXT("남은 응답 대기 0"), Server.Waiting.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenZoneEffectNoDestTest, "Snarehaven.Zone.Effect.UnknownFareFree",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenZoneEffectNoDestTest::RunTest(const FString& Parameters)
{
	using namespace SnarehavenZoneEffectTest;
	FFakeServer Server;
	Server.Install();
	FRig Rig;
	if (!TestTrue(TEXT("검사 폰·이 기기 조종"), Rig.Build()))
	{
		return false;
	}
	if (!TestTrue(TEXT("검사 칸은 서버 요금표에 이름이 없다"), UZoneTravelComponent::GetFerryDestKey(NoDestID).IsEmpty()))
	{
		return false;
	}

	// ⑤ 요금표에 이름 없는 입장료 칸 = 받을 길이 없으니 묻지도 막지도 않는다
	Rig.StandAt(OutsideA);
	Rig.StandAt(NoDestCenter);
	Rig.Effect->UpdateZoneEffects();
	TestEqual(TEXT("이름 없는 요금 칸 판정"), Rig.Effect->GetCurrentRegionID(), NoDestID);
	TestEqual(TEXT("이름 없는 요금 칸 요청 0"), Server.Functions.Num(), 0);
	TestEqual(TEXT("이름 없는 요금 칸은 막지 않음"), Rig.Pawn->GetActorLocation(), NoDestCenter);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
