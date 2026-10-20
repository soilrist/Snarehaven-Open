// DESIGN 8.9·8.13 2층 검사 — 바다·수영·배·두드리기 낚시.
// 보는 것: ① `DT_WaterActivityTable` 이 DESIGN 8.9·8.13 표와 같은가 (한쪽만 고치면 운다)
// ② 수영 속도·스태미나·체력 감소가 **실제 표 값**대로 도는가, 물 밖이면 스태미나가 줄지 않는가
// ③ 배 속도 배율 ④ 두드리기 횟수 = 공식 ⑤ 서버 표(token) 없이는 CatchFish 를 안 부른다 ⑥ 서식지 거르기
// ⑦ 수영·배·낚시 몸 클립과 몸 높이(설계서 값) · 운전석 = 선미 · 남의 화면 감기 가속(복제된 누름 수) · 찌·낚싯줄.
// 서버는 `USnareBackendSubsystem::TestServerOverride` 가짜로 대신한다 — 검사 끝에 비운다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.Water; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/AnimSequence.h"
#include "Backend/SnareBackendSubsystem.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/FishingRow.h"
#include "Data/ItemRowLookup.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Equipment/EquipmentComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Gathering/TapFishingComponent.h"
#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Net/UnrealNetwork.h"
#include "Player/PlayerVitalsComponent.h"
#include "SnarehavenCharacter.h"
#include "World/BoatActor.h"
#include "World/FishingSpotActor.h"
#include "World/SeaActor.h"
#include "World/WaterRules.h"
#include "World/WaterTravelComponent.h"

namespace
{
	// 서버 FISHING_SPOTS 키 · DT_FishingTable 의 실제 아이템 ID (이름이지 숫자가 아니다).
	const FName CarpItemId(TEXT("item.carp"));                // 민물 30분
	const FName TunaItemId(TEXT("item.tuna"));                // 바다 69분
	const FName SwordfishItemId(TEXT("item.swordfish"));      // 바다 141분
	const FName EelItemId(TEXT("item.eel"));                  // 늪 120분
	const FName SalmonSharkItemId(TEXT("item.salmon_shark")); // 바다 180분
	const TCHAR* TestToken = TEXT("test-token");

	// 검사 전용 거리 (Verification.md 6): 섬 가장자리(2500)를 넘은 바다 한가운데, 섬 안쪽 한 점.
	const FVector TestSeaPoint(2800.f, 0.f, 0.f);
	const FVector TestLandPoint(0.f, 0.f, 500.f);
	constexpr float TestBiteSeconds = 0.5f;
	constexpr float TestStepSeconds = 0.6f;

	// ── ① 설계서 표 ↔ DT ──

	FString ExtractSection(const FString& Doc, const FString& HeadingPrefix)
	{
		const int32 Start = Doc.Find(HeadingPrefix, ESearchCase::CaseSensitive);
		if (Start == INDEX_NONE)
		{
			return FString();
		}
		const int32 BodyStart = Start + HeadingPrefix.Len();
		const int32 NextHeading = Doc.Find(TEXT("\n### "), ESearchCase::CaseSensitive, ESearchDir::FromStart, BodyStart);
		return NextHeading == INDEX_NONE ? Doc.Mid(BodyStart) : Doc.Mid(BodyStart, NextHeading - BodyStart);
	}

	// 값 칸이 문장이라("걷기 × **1.5** · 보트 모터 ×2.0 …") 줄마다 숫자를 집는 패턴을 따로 둔다.
	// **여기엔 이름과 패턴뿐이고 값은 문서에서 읽는다.**
	struct FWaterField
	{
		const TCHAR* Section;
		const TCHAR* RowLabel;
		const TCHAR* Pattern;
		int32 Group;
		const TCHAR* FieldName;
		double (*Get)(const FWaterActivityRow&);
	};

	const FWaterField WaterFields[] = {
		{ TEXT("### 8.9 "), TEXT("바다 너비"), TEXT("\\*\\*([0-9.]+)cm"), 1, TEXT("SeaWidthCm"), [](const FWaterActivityRow& R) { return (double)R.SeaWidthCm; } },
		{ TEXT("### 8.9 "), TEXT("수영 속도"), TEXT("×\\s*\\*\\*([0-9.]+)"), 1, TEXT("SwimSpeedMul"), [](const FWaterActivityRow& R) { return (double)R.SwimSpeedMul; } },
		{ TEXT("### 8.3 "), TEXT("탈것 속도"), TEXT("×\\s*\\*\\*([0-9.]+)"), 1, TEXT("MountSpeedMul"), [](const FWaterActivityRow& R) { return (double)R.MountSpeedMul; } },
		{ TEXT("### 8.3 "), TEXT("탄 모습 크기"), TEXT("×\\s*\\*\\*([0-9.]+)"), 1, TEXT("MountLengthPerRiderWidth"), [](const FWaterActivityRow& R) { return (double)R.MountLengthPerRiderWidth; } },
		{ TEXT("### 8.3 "), TEXT("앉는 자리"), TEXT("×\\s*\\*\\*([0-9.]+)"), 1, TEXT("MountSeatBackOfLength"), [](const FWaterActivityRow& R) { return (double)R.MountSeatBackOfLength; } },
		{ TEXT("### 8.3 "), TEXT("탈것 몸 클립"), TEXT("걷기 클립 \\*\\*([0-9.]+)\\*\\*cm/s"), 1, TEXT("MountWalkClipSpeedCmPerSecond"), [](const FWaterActivityRow& R) { return (double)R.MountWalkClipSpeedCmPerSecond; } },
		{ TEXT("### 8.3 "), TEXT("탈것 몸 클립"), TEXT("달리기 클립 \\*\\*([0-9.]+)\\*\\*cm/s"), 1, TEXT("MountRunClipSpeedCmPerSecond"), [](const FWaterActivityRow& R) { return (double)R.MountRunClipSpeedCmPerSecond; } },
		{ TEXT("### 8.9 "), TEXT("수영 스태미나"), TEXT("−([0-9.]+)/초"), 1, TEXT("SwimStaminaPerSecond"), [](const FWaterActivityRow& R) { return (double)R.SwimStaminaPerSecond; } },
		{ TEXT("### 8.9 "), TEXT("스태미나 0"), TEXT("체력 −([0-9.]+)/초"), 1, TEXT("DrownHealthPerSecond"), [](const FWaterActivityRow& R) { return (double)R.DrownHealthPerSecond; } },
		{ TEXT("### 8.9 "), TEXT("구명보트"), TEXT("([0-9]+)인승"), 1, TEXT("BoatSeats"), [](const FWaterActivityRow& R) { return (double)R.BoatSeats; } },
		{ TEXT("### 8.9 "), TEXT("배 속도"), TEXT("걷기 × \\*\\*([0-9.]+)"), 1, TEXT("BoatSpeedMul"), [](const FWaterActivityRow& R) { return (double)R.BoatSpeedMul; } },
		{ TEXT("### 8.9 "), TEXT("배 속도"), TEXT("· 배 추진기 ×([0-9.]+)"), 1, TEXT("BoatMotorMul"), [](const FWaterActivityRow& R) { return (double)R.BoatMotorMul; } },
		{ TEXT("### 8.9 "), TEXT("배 속도"), TEXT("전동 추진기 ×([0-9.]+)"), 1, TEXT("BoatElectricMotorMul"), [](const FWaterActivityRow& R) { return (double)R.BoatElectricMotorMul; } },
		{ TEXT("### 8.13 "), TEXT("포인트 소진"), TEXT("\\*\\*([0-9]+)마리"), 1, TEXT("SpotCatchesBeforeGone"), [](const FWaterActivityRow& R) { return (double)R.SpotCatchesBeforeGone; } },
		{ TEXT("### 8.13 "), TEXT("포인트 소진"), TEXT("\\*\\*([0-9]+)초"), 1, TEXT("SpotRespawnSeconds"), [](const FWaterActivityRow& R) { return (double)R.SpotRespawnSeconds; } },
		{ TEXT("### 8.13 "), TEXT("서식지"), TEXT("\\*\\*([0-9]+)분 이상"), 1, TEXT("FarSeaMinCycleMinutes"), [](const FWaterActivityRow& R) { return (double)R.FarSeaMinCycleMinutes; } },
		{ TEXT("### 8.13 "), TEXT("흐름"), TEXT("\\*\\*([0-9]+)cm"), 1, TEXT("CastRangeCm"), [](const FWaterActivityRow& R) { return (double)R.CastRangeCm; } },
		{ TEXT("### 8.13 "), TEXT("입질 대기"), TEXT("\\*\\*([0-9.]+) ~ ([0-9.]+)초"), 1, TEXT("BiteMinSeconds"), [](const FWaterActivityRow& R) { return (double)R.BiteMinSeconds; } },
		{ TEXT("### 8.13 "), TEXT("입질 대기"), TEXT("\\*\\*([0-9.]+) ~ ([0-9.]+)초"), 2, TEXT("BiteMaxSeconds"), [](const FWaterActivityRow& R) { return (double)R.BiteMaxSeconds; } },
		{ TEXT("### 8.13 "), TEXT("미끼"), TEXT("입질 대기 ×([0-9.]+)"), 1, TEXT("WormBiteMul"), [](const FWaterActivityRow& R) { return (double)R.WormBiteMul; } },
		{ TEXT("### 8.13 "), TEXT("연타"), TEXT("([0-9]+) 이면 성공"), 1, TEXT("TapGoal"), [](const FWaterActivityRow& R) { return (double)R.TapGoal; } },
		{ TEXT("### 8.13 "), TEXT("연타"), TEXT("÷ \\(([0-9.]+) \\+ 낚시 시간\\(분\\) ÷ ([0-9.]+)\\)"), 1, TEXT("TapBasePresses"), [](const FWaterActivityRow& R) { return (double)R.TapBasePresses; } },
		{ TEXT("### 8.13 "), TEXT("연타"), TEXT("÷ \\(([0-9.]+) \\+ 낚시 시간\\(분\\) ÷ ([0-9.]+)\\)"), 2, TEXT("TapMinutesDivisor"), [](const FWaterActivityRow& R) { return (double)R.TapMinutesDivisor; } },
		{ TEXT("### 8.13 "), TEXT("연타"), TEXT("\\*\\*−([0-9.]+)/초"), 1, TEXT("TapDecayPerSecond"), [](const FWaterActivityRow& R) { return (double)R.TapDecayPerSecond; } },
		{ TEXT("### 8.13 "), TEXT("연타"), TEXT("\\*\\*([0-9.]+)초\\*\\* 머물면"), 1, TEXT("TapFailSeconds"), [](const FWaterActivityRow& R) { return (double)R.TapFailSeconds; } },
	};

	/** `| 라벨 | 값 | 근거 |` 줄의 값 칸. 없으면 빈 문자열. */
	FString FindValueCell(const FString& Section, const FString& RowLabel)
	{
		TArray<FString> Lines;
		Section.ParseIntoArrayLines(Lines);
		for (const FString& Line : Lines)
		{
			TArray<FString> Cells;
			Line.ParseIntoArray(Cells, TEXT("|"), false);
			if (Cells.Num() >= 3 && Cells[1].TrimStartAndEnd() == RowLabel)
			{
				return Cells[2];
			}
		}
		return FString();
	}

	TArray<FString> CollectWaterIssues(const FWaterActivityRow& Row, const FString& Doc)
	{
		TArray<FString> Issues;
		for (const FWaterField& Field : WaterFields)
		{
			const FString Cell = FindValueCell(ExtractSection(Doc, Field.Section), Field.RowLabel);
			FRegexMatcher Matcher(FRegexPattern(Field.Pattern), Cell);
			// 설계서 칸을 못 읽었으면 "비교할 게 없어 통과" 가 된다 — 그게 제일 위험하다.
			if (!Matcher.FindNext())
			{
				Issues.Add(FString::Printf(TEXT("DESIGN %s'%s' 에서 %s 를 못 읽었다"), Field.Section, Field.RowLabel, Field.FieldName));
				continue;
			}
			const double DesignValue = FCString::Atod(*Matcher.GetCaptureGroup(Field.Group));
			const double TableValue = Field.Get(Row);
			if (!FMath::IsNearlyEqual(TableValue, DesignValue, 0.001))
			{
				Issues.Add(FString::Printf(TEXT("%s 가 DESIGN '%s' 와 다르다 (문서 %.3f / 표 %.3f)"),
					Field.FieldName, Field.RowLabel, DesignValue, TableValue));
			}
		}
		return Issues;
	}

	bool LoadDesign(FString& OutDoc)
	{
		return FFileHelper::LoadFileToString(OutDoc, *FPaths::Combine(FPaths::ProjectDir(), TEXT("docs/DESIGN.md")));
	}

	// ── 월드 ──

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

	/** 섬 가운데에 바다 하나 — 섬 반폭·수면 높이는 SeaActor 기본값, 폭은 실제 표에서. */
	ASeaActor* SpawnSea(UWorld* World)
	{
		ASeaActor* const Sea = World->SpawnActor<ASeaActor>(FVector::ZeroVector, FRotator::ZeroRotator);
		if (Sea && !Sea->HasActorBegunPlay())
		{
			Sea->DispatchBeginPlay();
		}
		return Sea;
	}

	ASnarehavenCharacter* SpawnPlayer(UWorld* World, const FVector& At)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ASnarehavenCharacter* const Player = World->SpawnActor<ASnarehavenCharacter>(At, FRotator::ZeroRotator, Params);
		// 게임모드 없는 검사 월드는 BeginPlay 를 저절로 돌리지 않는다 — 표를 읽는 곳이 BeginPlay 라 직접 부른다.
		if (Player && !Player->HasActorBegunPlay())
		{
			Player->DispatchBeginPlay();
		}
		return Player;
	}

	AFishingSpotActor* SpawnSpot(UWorld* World, const FString& SpotId, const FVector& At)
	{
		AFishingSpotActor* const Spot = World->SpawnActorDeferred<AFishingSpotActor>(AFishingSpotActor::StaticClass(), FTransform(At));
		if (Spot)
		{
			Spot->SpotId = SpotId;
			Spot->FinishSpawning(FTransform(At));
			if (!Spot->HasActorBegunPlay())
			{
				Spot->DispatchBeginPlay();
			}
		}
		return Spot;
	}

	/** 가짜 서버: RollFish 는 주어진 표·어종을, CatchFish 는 그 어종을 돌려주고 부른 횟수를 센다. */
	struct FFakeFishServer
	{
		FString RollToken = TestToken;
		FString RollFish;
		FString RollCode;
		int32 RollCalls = 0;
		int32 CatchCalls = 0;
		FString LastCatchToken;
		FString LastRollSpot;
		/** 비어 있지 않으면 CatchFish 를 이 code 로 거절한다 (M28-01). */
		FString CatchCode;
		/** 켜면 CatchFish 답을 바로 주지 않고 `HeldCatchDone` 에 붙들어 둔다 — 「답 전」 을 보려고 (M28-01). */
		bool bHoldCatch = false;
		USnareBackendSubsystem::FServerCallDone HeldCatchDone;
		TSharedPtr<FJsonObject> HeldCatchResponse;

		void Install()
		{
			USnareBackendSubsystem::TestServerOverride =
				[this](const FString& Name, const TSharedPtr<FJsonObject>& Args, USnareBackendSubsystem::FServerCallDone Done)
				{
					TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
					if (Name == TEXT("RollFish"))
					{
						++RollCalls;
						Args->TryGetStringField(TEXT("spotId"), LastRollSpot);
						if (!RollCode.IsEmpty())
						{
							Response->SetStringField(TEXT("error"), RollCode);
							Response->SetStringField(TEXT("code"), RollCode);
						}
						else
						{
							Response->SetStringField(TEXT("token"), RollToken);
							Response->SetStringField(TEXT("fishItemId"), RollFish);
						}
					}
					else if (Name == TEXT("CatchFish"))
					{
						++CatchCalls;
						Args->TryGetStringField(TEXT("token"), LastCatchToken);
						if (!CatchCode.IsEmpty())
						{
							Response->SetStringField(TEXT("error"), CatchCode);
							Response->SetStringField(TEXT("code"), CatchCode);
						}
						else
						{
							Response->SetStringField(TEXT("caught"), RollFish);
						}
						if (bHoldCatch)
						{
							HeldCatchDone = Done;
							HeldCatchResponse = Response;
							return;
						}
					}
					Done(true, Response, FString());
				};
		}
	};

	/** 던지고 입질까지 흘린다 — 서버 응답은 가짜라 같은 틱에 온다. */
	void CastAndWaitBite(UTapFishingComponent& Fishing, AFishingSpotActor& Spot)
	{
		Fishing.BeginBiteWait(&Spot, TestBiteSeconds, false);
		Fishing.AdvanceFishing(TestStepSeconds);
	}

	float PlayerCycleMinutes(FName ItemId)
	{
		const FFishingRow* const Fish = SnareWater::FindFishByItemId(ItemId);
		return Fish ? static_cast<float>(Fish->CycleMinutes) : -1.f;
	}
}

// ── ① 표 ──

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenWaterTableMatchesDesignTest, "Snarehaven.Water.Table.MatchesDesign",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSnarehavenWaterTableMatchesDesignTest::RunTest(const FString& Parameters)
{
	const FWaterActivityRow* const Row = SnareWater::LoadRow();
	FString Doc;
	if (!TestNotNull(TEXT("DT_WaterActivityTable Default 행이 있고 미정 칸이 없다"), Row)
		|| !TestTrue(TEXT("docs/DESIGN.md 를 읽었다"), LoadDesign(Doc)))
	{
		return false;
	}
	const TArray<FString> Issues = CollectWaterIssues(*Row, Doc);
	for (const FString& Issue : Issues)
	{
		AddError(Issue);
	}
	AddInfo(FString::Printf(TEXT("[Water] 표↔설계 %d칸 비교, 어긋남 %d"), static_cast<int32>(UE_ARRAY_COUNT(WaterFields)), Issues.Num()));
	return Issues.IsEmpty();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenWaterTableCheckCatchesBrokenRowTest, "Snarehaven.Water.Table.CheckCatchesBrokenRow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSnarehavenWaterTableCheckCatchesBrokenRowTest::RunTest(const FString& Parameters)
{
	// 역검증: 표를 한 칸 틀리게 하면 위 검사가 운다, 미정(-1) 행은 켜지지 않는다.
	const FWaterActivityRow* const Row = SnareWater::LoadRow();
	FString Doc;
	if (!TestNotNull(TEXT("실제 행"), Row) || !TestTrue(TEXT("설계서"), LoadDesign(Doc)))
	{
		return false;
	}
	FWaterActivityRow Broken = *Row;
	Broken.SwimSpeedMul += 0.1f;
	Broken.TapDecayPerSecond += 1.f;
	TestEqual(TEXT("두 칸을 틀리면 어긋남 2"), CollectWaterIssues(Broken, Doc).Num(), 2);
	TestEqual(TEXT("설계서가 비면 전 칸을 '못 읽었다' 로 운다"),
		CollectWaterIssues(*Row, FString()).Num(), static_cast<int32>(UE_ARRAY_COUNT(WaterFields)));
	TestFalse(TEXT("기본값(-1) 행은 미완성"), SnareWater::IsRowComplete(FWaterActivityRow()));
	return true;
}

// ── ② 수영 ──

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenWaterSwimUsesTableTest, "Snarehaven.Water.Swim.SpeedStaminaHealthMatchTable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSnarehavenWaterSwimUsesTableTest::RunTest(const FString& Parameters)
{
	const FWaterActivityRow* const Row = SnareWater::LoadRow();
	if (!TestNotNull(TEXT("실제 물 표"), Row))
	{
		return false;
	}
	UWorld* const World = CreateTestWorld();
	ASeaActor* const Sea = SpawnSea(World);
	ASnarehavenCharacter* const Player = SpawnPlayer(World, TestLandPoint);
	UWaterTravelComponent* const Water = Player ? Player->FindComponentByClass<UWaterTravelComponent>() : nullptr;
	UPlayerVitalsComponent* const Vitals = Player ? Player->GetPlayerVitals() : nullptr;
	if (!TestNotNull(TEXT("바다"), Sea) || !TestNotNull(TEXT("물 컴포넌트"), Water) || !TestNotNull(TEXT("체력 컴포넌트"), Vitals))
	{
		DestroyTestWorld(World);
		return false;
	}
	TestEqual(TEXT("바다 폭 = 표"), Sea->BuiltSeaWidthCm, Row->SeaWidthCm);

	// 뭍: 걷기 속도 기준값.
	Water->UpdateWaterState();
	Player->UpdateMoveSpeed();
	const float WalkSpeed = Player->GetCharacterMovement()->MaxWalkSpeed;
	TestFalse(TEXT("섬 위는 물 밖"), Water->IsInWater());

	// 바다: 발이 수면 아래.
	Player->SetActorLocation(FVector(TestSeaPoint.X, TestSeaPoint.Y, Sea->GetSurfaceZ()));
	Water->UpdateWaterState();
	Player->UpdateMoveSpeed();
	const float SwimSpeed = Player->GetCharacterMovement()->MaxWalkSpeed;
	TestTrue(TEXT("바다 위는 물속"), Water->IsInWater());
	TestEqual(TEXT("수영 속도 = 걷기 × 표"), SwimSpeed, WalkSpeed * Row->SwimSpeedMul, 0.01f);

	const float MaxStamina = Vitals->GetMaxStamina();
	const float StartHealth = Vitals->GetHealth();
	constexpr float FirstSeconds = 5.f;
	Vitals->AdvanceTime(FirstSeconds);
	TestEqual(TEXT("5초 수영 = 스태미나 −표×5"), Vitals->GetStamina(), MaxStamina - Row->SwimStaminaPerSecond * FirstSeconds, 0.01f);
	const float StaminaAfterFirst = Vitals->GetStamina();

	const float SecondsToEmpty = StaminaAfterFirst / Row->SwimStaminaPerSecond;
	Vitals->AdvanceTime(SecondsToEmpty);
	TestEqual(TEXT("바닥까지는 스태미나만 닳는다"), Vitals->GetHealth(), StartHealth, 0.01f);
	TestEqual(TEXT("스태미나 0"), Vitals->GetStamina(), 0.f, 0.01f);

	constexpr float DrownSeconds = 2.f;
	Vitals->AdvanceTime(DrownSeconds);
	TestEqual(TEXT("스태미나 0 이면 체력 −표×초"), Vitals->GetHealth(), StartHealth - Row->DrownHealthPerSecond * DrownSeconds, 0.01f);
	AddInfo(FString::Printf(TEXT("[Water] 수영 속도 %.1f/%.1f=%.2f · 5초 뒤 스태미나 %.1f · 바닥까지 %.1f초 더 · 그 뒤 2초 체력 %.1f→%.1f"),
		SwimSpeed, WalkSpeed, SwimSpeed / WalkSpeed, StaminaAfterFirst, SecondsToEmpty, StartHealth, Vitals->GetHealth()));

	// 물 밖: 달리기·이동을 눌러도 스태미나가 줄지 않는다 (DESIGN 3.1 "스태미나 = 수영 전용").
	Player->SetActorLocation(TestLandPoint);
	Water->UpdateWaterState();
	TestFalse(TEXT("뭍으로 나옴"), Water->IsInWater());
	Vitals->SetRunInput(true);
	Vitals->SetMoveInput(true);
	const float StaminaOnShore = Vitals->GetStamina();
	const float HealthOnShore = Vitals->GetHealth();
	for (int32 Step = 0; Step < 30; ++Step)
	{
		Vitals->AdvanceTime(0.1f);
		if (Vitals->GetStamina() < StaminaOnShore - KINDA_SMALL_NUMBER)
		{
			AddError(FString::Printf(TEXT("물 밖에서 스태미나가 줄었다 (%.2f → %.2f)"), StaminaOnShore, Vitals->GetStamina()));
			break;
		}
	}
	TestFalse(TEXT("물 밖 달리기로 탈진하지 않는다"), Vitals->IsExhausted());
	TestTrue(TEXT("물 밖에서 체력이 깎이지 않는다"), Vitals->GetHealth() >= HealthOnShore);
	Player->UpdateMoveSpeed();
	TestTrue(TEXT("물 밖이면 다시 수영 배율이 안 걸린다"), Player->GetCharacterMovement()->MaxWalkSpeed >= WalkSpeed - 0.01f);
	AddInfo(FString::Printf(TEXT("[Water] 물 밖 3초 달리기: 스태미나 %.1f → %.1f"), StaminaOnShore, Vitals->GetStamina()));

	DestroyTestWorld(World);
	return true;
}

// ── ③ 배 ──

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenWaterBoatSpeedTest, "Snarehaven.Water.Boat.SpeedMulMatchesTable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSnarehavenWaterBoatSpeedTest::RunTest(const FString& Parameters)
{
	const FWaterActivityRow* const Row = SnareWater::LoadRow();
	if (!TestNotNull(TEXT("실제 물 표"), Row))
	{
		return false;
	}
	TestEqual(TEXT("모터 없음 = 배"), SnareWater::BoatSpeedMul(*Row, 0), Row->BoatSpeedMul);
	TestEqual(TEXT("보트 모터 = 배 × 모터"), SnareWater::BoatSpeedMul(*Row, 1), Row->BoatSpeedMul * Row->BoatMotorMul);
	TestEqual(TEXT("전기 모터 = 배 × 전기"), SnareWater::BoatSpeedMul(*Row, 2), Row->BoatSpeedMul * Row->BoatElectricMotorMul);

	UWorld* const World = CreateTestWorld();
	ASeaActor* const Sea = SpawnSea(World);
	ASnarehavenCharacter* const Player = SpawnPlayer(World, TestLandPoint);
	UWaterTravelComponent* const Water = Player ? Player->FindComponentByClass<UWaterTravelComponent>() : nullptr;
	if (!TestNotNull(TEXT("바다"), Sea) || !TestNotNull(TEXT("물 컴포넌트"), Water))
	{
		DestroyTestWorld(World);
		return false;
	}
	Player->UpdateMoveSpeed();
	const float WalkSpeed = Player->GetCharacterMovement()->MaxWalkSpeed;
	Player->SetActorLocation(FVector(TestSeaPoint.X, TestSeaPoint.Y, Sea->GetSurfaceZ()));

	for (int32 Tier = 0; Tier <= 2; ++Tier)
	{
		ABoatActor* const Boat = Water->SpawnBoatOnAuthority(Tier);
		if (!TestNotNull(*FString::Printf(TEXT("모터 %d 배를 띄웠다"), Tier), Boat))
		{
			break;
		}
		TestTrue(TEXT("띄운 사람이 운전한다"), Water->IsDriving());
		TestEqual(TEXT("좌석 = 표"), Boat->GetSeatCount(), Row->BoatSeats);
		Water->UpdateWaterState();
		TestFalse(TEXT("배 위는 수영이 아니다"), Water->IsInWater());
		Player->UpdateMoveSpeed();
		const float BoatSpeed = Player->GetCharacterMovement()->MaxWalkSpeed;
		TestEqual(*FString::Printf(TEXT("모터 %d 배 속도 = 걷기 × 배율"), Tier), BoatSpeed, WalkSpeed * SnareWater::BoatSpeedMul(*Row, Tier), 0.01f);
		AddInfo(FString::Printf(TEXT("[Water] 배 모터 %d: %.1f / 걷기 %.1f = ×%.2f"), Tier, BoatSpeed, WalkSpeed, BoatSpeed / WalkSpeed));
		Boat->UnseatAll();
		Boat->Destroy();
	}
	DestroyTestWorld(World);
	return true;
}

// ── ③-1 구명보트 메시·흔들림 (DESIGN 8.9 "구명보트 크기·흔들림") ──
// 메시가 붙고 길이가 280cm 이며, 흔들림은 Hull 만 움직이고 루트(복제 이동)는 그대로이고,
// 멈춰 있으면 기울기가 잔물결 폭 안이고 달리면 뱃머리(+X)가 들린다.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenWaterBoatMeshBobTest, "Snarehaven.Water.Boat.MeshSizeAndBob",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSnarehavenWaterBoatMeshBobTest::RunTest(const FString& Parameters)
{
	UWorld* const World = CreateTestWorld();
	const FVector Start(TestSeaPoint.X, TestSeaPoint.Y, 0.f);
	ABoatActor* const Boat = World->SpawnActor<ABoatActor>(Start, FRotator::ZeroRotator);
	UStaticMeshComponent* const Hull = Boat ? Boat->FindComponentByClass<UStaticMeshComponent>() : nullptr;
	const UStaticMesh* const Mesh = Hull ? Hull->GetStaticMesh() : nullptr;
	if (!TestNotNull(TEXT("배"), Boat) || !TestNotNull(TEXT("몸통 메시"), Mesh))
	{
		DestroyTestWorld(World);
		return false;
	}
	TestEqual(TEXT("몸통 메시 = SM_Vehicle_Boat_01"), Mesh->GetName(), FString(TEXT("SM_Vehicle_Boat_01")));
	const FVector Extent = Mesh->GetBoundingBox().GetSize();
	const float Length = FMath::Max(Extent.X, Extent.Y) * Hull->GetRelativeScale3D().X;
	TestEqual(TEXT("몸통 길이 = 280cm"), Length, 280.f, 0.5f);
	TestEqual(TEXT("탈 수 있는 반경 = 길이 절반"), Boat->GetBoardRadiusCm(), 140.f, 0.01f);
	AddInfo(FString::Printf(TEXT("[Boat] 메시 %.1fx%.1fx%.1f → 배율 %.3f · 길이 %.1fcm"), Extent.X, Extent.Y, Extent.Z, Hull->GetRelativeScale3D().X, Length));

	// 뱃머리 들림(도) = 기울어진 위 벡터의 -X 성분. 메시 기본 회전(요)과 무관하다.
	auto BowLiftDeg = [Hull]() { return FMath::RadiansToDegrees(FMath::Asin(-Hull->GetRelativeRotation().Quaternion().GetUpVector().X)); };
	const float Dt = 1.f / 30.f;
	float RestMinZ = TNumericLimits<float>::Max(), RestMaxZ = TNumericLimits<float>::Lowest(), RestMaxTilt = 0.f;
	for (int32 Frame = 0; Frame < 150; ++Frame)
	{
		Boat->Tick(Dt);
		RestMinZ = FMath::Min(RestMinZ, Hull->GetRelativeLocation().Z);
		RestMaxZ = FMath::Max(RestMaxZ, Hull->GetRelativeLocation().Z);
		RestMaxTilt = FMath::Max(RestMaxTilt, FMath::Abs(BowLiftDeg()));
	}
	TestTrue(TEXT("흔들려도 루트는 그대로"), Boat->GetActorLocation().Equals(Start, 0.01f));
	TestTrue(TEXT("멈춰도 출렁인다"), RestMaxZ - RestMinZ > 4.f);
	TestTrue(TEXT("멈춘 출렁임 = 잠김 15 ± 4cm 안"), RestMinZ >= -19.01f && RestMaxZ <= -10.99f);
	TestTrue(TEXT("멈춘 피치 = ±2도 안"), RestMaxTilt <= 2.01f);

	float MovingMinLift = TNumericLimits<float>::Max();
	for (int32 Frame = 0; Frame < 120; ++Frame)
	{
		Boat->SetActorLocation(Boat->GetActorLocation() + FVector(680.f * Dt, 0.f, 0.f));
		Boat->Tick(Dt);
		if (Frame >= 90)
		{
			MovingMinLift = FMath::Min(MovingMinLift, BowLiftDeg());
		}
	}
	TestTrue(TEXT("680cm/s(전기 모터 최고 속도, DESIGN 8.9) 로 달리면 뱃머리가 늘 들려 있다"), MovingMinLift > 1.f);
	AddInfo(FString::Printf(TEXT("[Boat] 멈춤: 높이 %.1f~%.1f · 최대 기울기 %.2f도 / 달림: 최소 뱃머리 %.2f도"), RestMinZ, RestMaxZ, RestMaxTilt, MovingMinLift));
	Boat->Destroy();
	DestroyTestWorld(World);
	return true;
}

// ── ④ 두드리기 횟수 ──

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenWaterTapCountTest, "Snarehaven.Water.Fishing.TapCountMatchesFormula",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSnarehavenWaterTapCountTest::RunTest(const FString& Parameters)
{
	const FWaterActivityRow* const Row = SnareWater::LoadRow();
	if (!TestNotNull(TEXT("실제 물 표"), Row))
	{
		return false;
	}
	// 공식을 검사 쪽에서 다시 쓴다 — 식을 바꾸면 여기와 WaterRules 가 함께 바뀌어야 한다.
	auto Expected = [Row](float Minutes) { return FMath::CeilToInt(Row->TapBasePresses + Minutes / Row->TapMinutesDivisor - KINDA_SMALL_NUMBER); };
	const float CarpMinutes = PlayerCycleMinutes(CarpItemId);
	const float SharkMinutes = PlayerCycleMinutes(SalmonSharkItemId);
	TestTrue(TEXT("잉어·악상어 낚시 시간을 표에서 읽었다"), CarpMinutes > 0.f && SharkMinutes > 0.f);
	TestEqual(TEXT("잉어 누름 = 공식"), SnareWater::PressesNeeded(*Row, CarpMinutes), Expected(CarpMinutes));
	TestEqual(TEXT("악상어 누름 = 공식"), SnareWater::PressesNeeded(*Row, SharkMinutes), Expected(SharkMinutes));
	TestEqual(TEXT("미끼·장비 없음 가장 짧은 입질 = 표 최소"), SnareWater::BiteWaitSeconds(*Row, 0.f, 1.f, false), Row->BiteMinSeconds);
	TestEqual(TEXT("지렁이 = 입질 대기 × 표"), SnareWater::BiteWaitSeconds(*Row, 1.f, 1.f, true), Row->BiteMaxSeconds * Row->WormBiteMul);

	UWorld* const World = CreateTestWorld();
	FFakeFishServer Server;
	Server.Install();
	ASnarehavenCharacter* const Player = SpawnPlayer(World, TestLandPoint);
	UTapFishingComponent* const Fishing = Player ? Player->FindComponentByClass<UTapFishingComponent>() : nullptr;
	AFishingSpotActor* const Pond = SpawnSpot(World, TEXT("pond_1"), TestLandPoint);
	AFishingSpotActor* const Far = SpawnSpot(World, TEXT("far_1"), TestLandPoint);
	if (!TestNotNull(TEXT("낚시 컴포넌트"), Fishing) || !TestNotNull(TEXT("연못"), Pond) || !TestNotNull(TEXT("먼바다"), Far))
	{
		DestroyTestWorld(World);
		return false;
	}

	struct FCase { AFishingSpotActor* Spot; FName Fish; };
	for (const FCase& Case : { FCase{ Pond, CarpItemId }, FCase{ Far, SalmonSharkItemId } })
	{
		Server.RollFish = Case.Fish.ToString();
		Server.CatchCalls = 0;
		CastAndWaitBite(*Fishing, *Case.Spot);
		if (!TestTrue(TEXT("입질 → 연타"), Fishing->GetState() == ETapFishingState::Tapping))
		{
			break;
		}
		const int32 Needed = Fishing->GetPressesNeeded();
		TestEqual(TEXT("컴포넌트 누름 = 공식"), Needed, Expected(PlayerCycleMinutes(Case.Fish)));
		for (int32 Press = 0; Press < Needed - 1; ++Press)
		{
			Fishing->Tap();
		}
		TestTrue(TEXT("하나 모자라면 아직 연타"), Fishing->GetState() == ETapFishingState::Tapping);
		TestEqual(TEXT("하나 모자라면 CatchFish 안 부름"), Server.CatchCalls, 0);
		Fishing->Tap();
		TestEqual(TEXT("N번째에 CatchFish 한 번"), Server.CatchCalls, 1);
		TestEqual(TEXT("받은 표 그대로 보낸다"), Server.LastCatchToken, FString(TestToken));
		TestTrue(TEXT("잡은 어종"), Fishing->GetLastCaughtItem() == Case.Fish);
		AddInfo(FString::Printf(TEXT("[Water] %s %.0f분 → %d번 (공식 %d)"),
			*Case.Fish.ToString(), PlayerCycleMinutes(Case.Fish), Fishing->GetPressCount(), Expected(PlayerCycleMinutes(Case.Fish))));
	}
	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenWaterDecayAndMissTest, "Snarehaven.Water.Fishing.DecayMissAndSpotRespawn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSnarehavenWaterDecayAndMissTest::RunTest(const FString& Parameters)
{
	const FWaterActivityRow* const Row = SnareWater::LoadRow();
	if (!TestNotNull(TEXT("실제 물 표"), Row))
	{
		return false;
	}
	UWorld* const World = CreateTestWorld();
	FFakeFishServer Server;
	Server.RollFish = CarpItemId.ToString();
	Server.Install();
	ASnarehavenCharacter* const Player = SpawnPlayer(World, TestLandPoint);
	UTapFishingComponent* const Fishing = Player ? Player->FindComponentByClass<UTapFishingComponent>() : nullptr;
	AFishingSpotActor* const Pond = SpawnSpot(World, TEXT("pond_1"), TestLandPoint);
	if (!TestNotNull(TEXT("낚시 컴포넌트"), Fishing) || !TestNotNull(TEXT("연못"), Pond))
	{
		DestroyTestWorld(World);
		return false;
	}

	// 안 누르면 −표/초.
	CastAndWaitBite(*Fishing, *Pond);
	Fishing->Tap();
	const float AfterOne = Fishing->GetProgress();
	constexpr float DecaySeconds = 0.2f;
	Fishing->AdvanceFishing(DecaySeconds);
	TestEqual(TEXT("안 누르면 진행 −표×초"), Fishing->GetProgress(), AfterOne - Row->TapDecayPerSecond * DecaySeconds, 0.01f);

	// 진행 0 에서 표 초만큼 머물면 놓친다 — 그 직전은 아직 연타.
	Fishing->Quit();
	CastAndWaitBite(*Fishing, *Pond);
	Fishing->AdvanceFishing(Row->TapFailSeconds - 0.1f);
	TestTrue(TEXT("표 초 직전은 아직 연타"), Fishing->GetState() == ETapFishingState::Tapping);
	Fishing->AdvanceFishing(0.1f);
	TestTrue(TEXT("표 초가 차면 놓침"), Fishing->GetState() == ETapFishingState::Idle);
	TestEqual(TEXT("놓치면 CatchFish 안 부름"), Server.CatchCalls, 0);

	// 입질 전에 움직이면 취소 — 서버를 부르지도 않는다.
	const int32 RollsBefore = Server.RollCalls;
	Fishing->BeginBiteWait(Pond, TestBiteSeconds, false);
	Player->SetActorLocation(TestLandPoint + FVector(50.f, 0.f, 0.f));
	Fishing->AdvanceFishing(TestStepSeconds);
	TestTrue(TEXT("움직이면 취소"), Fishing->GetState() == ETapFishingState::Idle);
	TestEqual(TEXT("취소면 RollFish 안 부름"), Server.RollCalls, RollsBefore);

	// 자리: 표 마리 수를 잡으면 사라지고 표 초 뒤 다시.
	TestEqual(TEXT("자리는 표 마리 수로 시작"), Pond->GetRemainingCatches(), Row->SpotCatchesBeforeGone);
	for (int32 Catch = 0; Catch < Row->SpotCatchesBeforeGone; ++Catch)
	{
		Pond->ConsumeCatch();
	}
	TestFalse(TEXT("다 잡으면 사라진다"), Pond->IsAvailable());
	TestEqual(TEXT("다시 생길 때까지 표 초"), Pond->GetRespawnRemainingSeconds(), Row->SpotRespawnSeconds, 0.01f);
	TestNull(TEXT("사라진 자리엔 못 던진다"), AFishingSpotActor::FindNearestAvailable(World, TestLandPoint, Row->CastRangeCm));
	AddInfo(FString::Printf(TEXT("[Water] 1번 누름 %.2f → %.1f초 뒤 %.2f · 0 에서 %.1f초 놓침 · 자리 %d마리 뒤 %.0f초"),
		AfterOne, DecaySeconds, AfterOne - Row->TapDecayPerSecond * DecaySeconds, Row->TapFailSeconds,
		Row->SpotCatchesBeforeGone, Pond->GetRespawnRemainingSeconds()));

	DestroyTestWorld(World);
	return true;
}

// ── ⑤ 표 없이는 CatchFish 없음 ──

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenWaterNoTokenNoCatchTest, "Snarehaven.Water.Fishing.NoTokenNeverCallsCatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSnarehavenWaterNoTokenNoCatchTest::RunTest(const FString& Parameters)
{
	UWorld* const World = CreateTestWorld();
	FFakeFishServer Server;
	Server.RollFish = CarpItemId.ToString();
	Server.Install();
	ASnarehavenCharacter* const Player = SpawnPlayer(World, TestLandPoint);
	UTapFishingComponent* const Fishing = Player ? Player->FindComponentByClass<UTapFishingComponent>() : nullptr;
	AFishingSpotActor* const Pond = SpawnSpot(World, TEXT("pond_1"), TestLandPoint);
	if (!TestNotNull(TEXT("낚시 컴포넌트"), Fishing) || !TestNotNull(TEXT("연못"), Pond))
	{
		DestroyTestWorld(World);
		return false;
	}

	// 빈 표.
	Server.RollToken.Reset();
	CastAndWaitBite(*Fishing, *Pond);
	TestEqual(TEXT("RollFish 는 불렀다"), Server.RollCalls, 1);
	TestEqual(TEXT("받은 spotId = 자리"), Server.LastRollSpot, FString(TEXT("pond_1")));
	TestTrue(TEXT("빈 표면 연타로 안 간다"), Fishing->GetState() == ETapFishingState::Idle);
	for (int32 Press = 0; Press < 50; ++Press)
	{
		Fishing->Tap();
	}
	TestEqual(TEXT("빈 표면 아무리 눌러도 CatchFish 0"), Server.CatchCalls, 0);

	// 서버 거절 (먼바다 도감 50% 미만 = locked).
	Server.RollToken = TestToken;
	Server.RollCode = TEXT("locked");
	CastAndWaitBite(*Fishing, *Pond);
	TestTrue(TEXT("거절이면 끝"), Fishing->GetState() == ETapFishingState::Idle);
	TestEqual(TEXT("거절 code 를 남긴다"), Fishing->GetLastMessage(), FString(TEXT("locked")));
	TestEqual(TEXT("거절이면 CatchFish 0"), Server.CatchCalls, 0);

	// 역검증: 같은 길에 표가 있으면 CatchFish 를 부른다 (검사가 늘 0 만 보는 게 아니다).
	Server.RollCode.Reset();
	CastAndWaitBite(*Fishing, *Pond);
	for (int32 Press = 0; Press < Fishing->GetPressesNeeded(); ++Press)
	{
		Fishing->Tap();
	}
	TestEqual(TEXT("표가 있으면 CatchFish 1"), Server.CatchCalls, 1);
	TestEqual(TEXT("잡으면 자리 한 마리 준다"), Pond->GetRemainingCatches(), SnareWater::LoadRow()->SpotCatchesBeforeGone - 1);
	AddInfo(FString::Printf(TEXT("[Water] RollFish %d번 · CatchFish %d번 (빈 표·거절 0, 정상 1)"), Server.RollCalls, Server.CatchCalls));

	DestroyTestWorld(World);
	return true;
}

// ── ⑤-2 잡은 것 알림 (M28-01) ──

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenWaterCatchPopupTest, "Snarehaven.Water.Fishing.CatchPopupAfterServer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSnarehavenWaterCatchPopupTest::RunTest(const FString& Parameters)
{
	UWorld* const World = CreateTestWorld();
	FFakeFishServer Server;
	Server.RollFish = CarpItemId.ToString();
	Server.Install();
	ASnarehavenCharacter* const Player = SpawnPlayer(World, TestLandPoint);
	UTapFishingComponent* const Fishing = Player ? Player->FindComponentByClass<UTapFishingComponent>() : nullptr;
	AFishingSpotActor* const Pond = SpawnSpot(World, TEXT("pond_1"), TestLandPoint);
	if (!TestNotNull(TEXT("낚시 컴포넌트"), Fishing) || !TestNotNull(TEXT("연못"), Pond))
	{
		DestroyTestWorld(World);
		return false;
	}
	// 기대 이름은 표에서 읽는다 — 이름을 바꿔도 검사가 안 깨지게.
	const UDataTable* const ItemTable = LoadObject<UDataTable>(nullptr, SnareItemRows::ItemTablePath());
	const FItemRow* const CarpRow = SnareItemRows::FindByItemID(ItemTable, CarpItemId);
	if (!TestNotNull(TEXT("잉어 아이템 행"), CarpRow))
	{
		DestroyTestWorld(World);
		return false;
	}
	const FString Expected = FString::Printf(TEXT("+1 %s"), *CarpRow->DisplayName.ToString());

	TArray<FString> Shown;
	const FDelegateHandle Handle = Fishing->OnCatchPopup.AddLambda([&Shown](const FText& Popup) { Shown.Add(Popup.ToString()); });
	auto TapToGoal = [Fishing]()
	{
		for (int32 Press = 0; Press < Fishing->GetPressesNeeded(); ++Press)
		{
			Fishing->Tap();
		}
	};

	// ① 답 전: CatchFish 는 불렀지만 알림 0.
	Server.bHoldCatch = true;
	CastAndWaitBite(*Fishing, *Pond);
	TapToGoal();
	TestEqual(TEXT("CatchFish 1번 부름"), Server.CatchCalls, 1);
	TestTrue(TEXT("답을 기다리는 중"), Fishing->GetState() == ETapFishingState::Catching);
	TestEqual(TEXT("서버 답 전 알림 0"), Shown.Num(), 0);
	TestTrue(TEXT("서버 답 전 글 비어 있음"), Fishing->GetLastCatchPopup().IsEmpty());

	// ② 답 뒤: 「+1 이름」 한 번.
	Server.bHoldCatch = false;
	Server.HeldCatchDone(true, Server.HeldCatchResponse, FString());
	TestEqual(TEXT("서버 답 뒤 알림 1번"), Shown.Num(), 1);
	TestEqual(TEXT("알림 글 = +1 표 이름"), Shown.Num() > 0 ? Shown[0] : FString(), Expected);
	TestEqual(TEXT("남긴 글도 같다"), Fishing->GetLastCatchPopup().ToString(), Expected);

	// ③ 서버 거절: 알림 0 · 지난 판 글도 남지 않는다.
	Server.CatchCode = TEXT("bad_token");
	CastAndWaitBite(*Fishing, *Pond);
	TapToGoal();
	TestEqual(TEXT("거절 판도 CatchFish 는 부름"), Server.CatchCalls, 2);
	TestEqual(TEXT("거절 답이면 알림 늘지 않음"), Shown.Num(), 1);
	TestTrue(TEXT("거절 판 글 비어 있음"), Fishing->GetLastCatchPopup().IsEmpty());
	TestEqual(TEXT("거절 code 를 남긴다"), Fishing->GetLastMessage(), FString(TEXT("bad_token")));
	AddInfo(FString::Printf(TEXT("[Water] 잡은 것 알림: 답 전 0 · 답 뒤 '%s' · 거절 0 (총 %d번)"),
		*Fishing->GetLastCatchPopup().ToString(), Shown.Num()));
	AddInfo(FString::Printf(TEXT("[Water] 정상 판 알림 글 '%s'"), Shown.Num() > 0 ? *Shown[0] : TEXT("")));

	Fishing->OnCatchPopup.Remove(Handle);
	DestroyTestWorld(World);
	return true;
}

// ── ⑥ 서식지 ──

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenWaterHabitatFilterTest, "Snarehaven.Water.Fishing.HabitatFilter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSnarehavenWaterHabitatFilterTest::RunTest(const FString& Parameters)
{
	const FWaterActivityRow* const Row = SnareWater::LoadRow();
	if (!TestNotNull(TEXT("실제 물 표"), Row))
	{
		return false;
	}
	UWorld* const World = CreateTestWorld();
	FFakeFishServer Server;
	Server.Install();
	ASnarehavenCharacter* const Player = SpawnPlayer(World, TestLandPoint);
	UTapFishingComponent* const Fishing = Player ? Player->FindComponentByClass<UTapFishingComponent>() : nullptr;
	if (!TestNotNull(TEXT("낚시 컴포넌트"), Fishing))
	{
		DestroyTestWorld(World);
		return false;
	}

	struct FCase { const TCHAR* SpotId; FName Fish; bool bAllowed; };
	const FCase Cases[] = {
		{ TEXT("pond_1"), CarpItemId, true },
		{ TEXT("river_1"), CarpItemId, true },
		{ TEXT("coast_1"), CarpItemId, false },    // 민물 어종은 바다에 없다
		{ TEXT("pond_1"), TunaItemId, false },     // 바다 어종은 연못에 없다
		{ TEXT("coast_1"), TunaItemId, true },
		{ TEXT("far_1"), TunaItemId, false },      // 69분 < 먼바다 문턱
		{ TEXT("far_1"), SwordfishItemId, true },  // 141분 ≥ 문턱
		{ TEXT("swamp_1"), EelItemId, true },      // 장어는 늪에서만
		{ TEXT("swamp_1"), CarpItemId, true },     // 늪 자리는 민물 어종도
		{ TEXT("swamp_1"), TunaItemId, false },
		{ TEXT("pond_1"), EelItemId, false },
	};
	int32 Passed = 0;
	for (const FCase& Case : Cases)
	{
		AFishingSpotActor* const Spot = SpawnSpot(World, Case.SpotId, TestLandPoint);
		Server.RollFish = Case.Fish.ToString();
		CastAndWaitBite(*Fishing, *Spot);
		const bool bTapping = Fishing->GetState() == ETapFishingState::Tapping;
		const FString What = FString::Printf(TEXT("%s + %s → %s"), Case.SpotId, *Case.Fish.ToString(), Case.bAllowed ? TEXT("연타") : TEXT("거름"));
		Passed += TestTrue(*What, bTapping == Case.bAllowed) ? 1 : 0;
		Fishing->Quit();
		Spot->Destroy();
	}
	TestEqual(TEXT("거른 어종은 CatchFish 0"), Server.CatchCalls, 0);
	AddInfo(FString::Printf(TEXT("[Water] 서식지 %d/%d 맞음 (먼바다 문턱 %d분)"), Passed, static_cast<int32>(UE_ARRAY_COUNT(Cases)), Row->FarSeaMinCycleMinutes));

	// 역검증: 먼바다 문턱을 참치 아래로 내리면 참치가 먼바다에 들어온다 — 문턱이 정말 거르고 있다.
	const FFishingRow* const Tuna = SnareWater::FindFishByItemId(TunaItemId);
	if (TestNotNull(TEXT("참치 행"), Tuna))
	{
		FWaterActivityRow Lowered = *Row;
		Lowered.FarSeaMinCycleMinutes = Tuna->CycleMinutes;
		TestFalse(TEXT("실제 문턱: 참치는 먼바다 밖"), SnareWater::FishFitsSpot(*Row, EFishingSpotKind::FarSea, *Tuna));
		TestTrue(TEXT("문턱을 내리면 참치도 먼바다"), SnareWater::FishFitsSpot(Lowered, EFishingSpotKind::FarSea, *Tuna));
	}
	DestroyTestWorld(World);
	return true;
}

// ── ⑦ 몸 클립·높이 (DESIGN 8.9 "수영 몸 높이·배 앉기 높이·낚시 몸 클립") ──
// 물·배·낚시 상태에 맞는 클립이 도는가, 수영 몸 원점이 수면 아래 설계서 깊이(× 몸 배율)에 가는가,
// 배 좌석이 흔들리는 Hull 을 따라가는가(승객은 Hull 에 붙는다), 감기가 누른 순간만 빨라지는가.

namespace
{
	/** 설계서 8.9 한 줄에서 숫자 하나. 못 읽으면 -1 — 그러면 검사가 운다. */
	float ReadDesignNumber(const FString& Doc, const TCHAR* RowLabel, const TCHAR* Pattern, int32 Group = 1)
	{
		FRegexMatcher Matcher(FRegexPattern(Pattern), FindValueCell(ExtractSection(Doc, TEXT("### 8.9 ")), RowLabel));
		return Matcher.FindNext() ? FCString::Atof(*Matcher.GetCaptureGroup(Group)) : -1.f;
	}

	FString CurrentClipName(const ASnarehavenCharacter& Player)
	{
		return IsValid(Player.GetCurrentBodyAnim()) ? Player.GetCurrentBodyAnim()->GetName() : FString(TEXT("없음"));
	}

	void TickPlayer(ASnarehavenCharacter& Player, float Seconds)
	{
		constexpr float Dt = 0.05f;
		for (float Elapsed = 0.f; Elapsed < Seconds - KINDA_SMALL_NUMBER; Elapsed += Dt)
		{
			// 캐릭터 Tick 은 protected — 액터 쪽(public·가상)으로 불러도 같은 몸 갱신 순서를 탄다.
			static_cast<AActor&>(Player).Tick(Dt);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenWaterBodySwimBoatTest, "Snarehaven.Water.Body.SwimAndBoatClipsAndHeights",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSnarehavenWaterBodySwimBoatTest::RunTest(const FString& Parameters)
{
	FString Doc;
	if (!TestTrue(TEXT("설계서"), LoadDesign(Doc)))
	{
		return false;
	}
	const float IdleSink = ReadDesignNumber(Doc, TEXT("수영 몸 높이"), TEXT("제자리 헤엄 \\*\\*([0-9.]+)\\*\\*"));
	const float MoveSink = ReadDesignNumber(Doc, TEXT("수영 몸 높이"), TEXT("자유형 \\*\\*([0-9.]+)\\*\\*"));
	const float SeatFloor = ReadDesignNumber(Doc, TEXT("배 앉기 높이"), TEXT("\\*\\*([0-9.]+)cm\\*\\*"));
	const float Draft = ReadDesignNumber(Doc, TEXT("구명보트 크기"), TEXT("잠김 \\*\\*([0-9.]+)cm"));
	// 좌석 자리 — 운전석은 선미(−), 승객은 그 앞. 부호는 문장("선미")이 정하고 숫자만 읽는다.
	const float DesignDriverX = -ReadDesignNumber(Doc, TEXT("배 좌석 자리"), TEXT("운전석 \\*\\*−([0-9.]+)cm"));
	const float DesignPassenger1X = ReadDesignNumber(Doc, TEXT("배 좌석 자리"), TEXT("승객 \\*\\*([0-9.]+)cm"));
	const float DesignPassenger2X = ReadDesignNumber(Doc, TEXT("배 좌석 자리"), TEXT("\\*\\*\\+([0-9.]+)cm"));
	if (!TestTrue(TEXT("DESIGN 8.9 몸 높이·좌석·잠김을 읽었다"), IdleSink > 0.f && MoveSink > 0.f && SeatFloor > 0.f && Draft > 0.f
		&& DesignDriverX < 0.f && DesignPassenger1X >= 0.f && DesignPassenger2X > 0.f))
	{
		return false;
	}

	UWorld* const World = CreateTestWorld();
	ASeaActor* const Sea = SpawnSea(World);
	ASnarehavenCharacter* const Player = SpawnPlayer(World, TestLandPoint);
	UWaterTravelComponent* const Water = Player ? Player->FindComponentByClass<UWaterTravelComponent>() : nullptr;
	if (!TestNotNull(TEXT("바다"), Sea) || !TestNotNull(TEXT("물 컴포넌트"), Water) || !TestTrue(TEXT("사람 몸"), Player->ApplyPlayerBody()))
	{
		DestroyTestWorld(World);
		return false;
	}
	USkeletalMeshComponent* const Body = Player->GetMesh();
	const float Scale = static_cast<float>(Body->GetRelativeScale3D().Z);
	const float GroundRelZ = static_cast<float>(Body->GetRelativeLocation().Z);
	const float SurfaceZ = Sea->GetSurfaceZ();

	// 수영 — 멈춤: 제자리 헤엄, 원점 = 수면 − 설계서 깊이 × 몸 배율.
	Player->SetActorLocation(FVector(TestSeaPoint.X, TestSeaPoint.Y, SurfaceZ));
	Water->UpdateWaterState();
	Player->GetCharacterMovement()->Velocity = FVector::ZeroVector;
	TickPlayer(*Player, 3.f);
	TestEqual(TEXT("물속 멈춤 = SwimIdle"), CurrentClipName(*Player), FString(TEXT("A_Player_SwimIdle")));
	const float IdleDepth = SurfaceZ - static_cast<float>(Body->GetComponentLocation().Z);
	TestEqual(TEXT("제자리 헤엄 깊이 = 설계서 × 몸 배율"), IdleDepth, IdleSink * Scale, 0.6f);

	// 수영 — 이동: 자유형, 등이 수면.
	Player->GetCharacterMovement()->Velocity = FVector(Player->GetMoveAnimEnterSpeed() * 10.f, 0.f, 0.f);
	TickPlayer(*Player, 3.f);
	TestEqual(TEXT("물속 이동 = SwimMove"), CurrentClipName(*Player), FString(TEXT("A_Player_SwimMove")));
	const float MoveDepth = SurfaceZ - static_cast<float>(Body->GetComponentLocation().Z);
	TestEqual(TEXT("자유형 깊이 = 설계서 × 몸 배율"), MoveDepth, MoveSink * Scale, 0.6f);
	AddInfo(FString::Printf(TEXT("[Body] 수면 %.1f · SwimIdle 수면 아래 %.1f (설계 %.0f×%.3f) · SwimMove %.1f (설계 %.0f×%.3f)"),
		SurfaceZ, IdleDepth, IdleSink, Scale, MoveDepth, MoveSink, Scale));

	// 뭍으로 나오면 몸이 캡슐 바닥으로 돌아온다 (역검증 — 수영 높이가 땅까지 남지 않는다).
	Player->GetCharacterMovement()->Velocity = FVector::ZeroVector;
	Player->SetActorLocation(TestLandPoint);
	Water->UpdateWaterState();
	TickPlayer(*Player, 3.f);
	TestEqual(TEXT("뭍 멈춤 = Idle"), CurrentClipName(*Player), FString(TEXT("A_Player_Idle")));
	TestEqual(TEXT("뭍이면 몸이 캡슐 바닥으로"), static_cast<float>(Body->GetRelativeLocation().Z), GroundRelZ, 0.6f);

	// 배 — 운전자: 앉기, 원점 = Hull + 좌석 바닥.
	Player->SetActorLocation(FVector(TestSeaPoint.X, TestSeaPoint.Y, SurfaceZ));
	ABoatActor* const Boat = Water->SpawnBoatOnAuthority(0);
	UStaticMeshComponent* const Hull = Boat ? Boat->FindComponentByClass<UStaticMeshComponent>() : nullptr;
	ASnarehavenCharacter* const Passenger = SpawnPlayer(World, FVector(TestSeaPoint.X, TestSeaPoint.Y + 300.f, SurfaceZ));
	if (!TestNotNull(TEXT("배"), Hull) || !TestNotNull(TEXT("승객"), Passenger) || !TestTrue(TEXT("승객 몸"), Passenger->ApplyPlayerBody()))
	{
		DestroyTestWorld(World);
		return false;
	}
	TestEqual(TEXT("승객은 좌석 1"), Boat->SeatPawn(Passenger), 1);
	TestTrue(TEXT("승객은 흔들리는 Hull 에 붙는다"), Passenger->GetRootComponent()->GetAttachParent() == Hull);

	float MinDriverAboveSurface = TNumericLimits<float>::Max(), MaxDriverAboveSurface = TNumericLimits<float>::Lowest();
	float MaxSeatError = 0.f, MinPassengerZ = TNumericLimits<float>::Max(), MaxPassengerZ = TNumericLimits<float>::Lowest();
	for (int32 Frame = 0; Frame < 90; ++Frame)
	{
		Boat->Tick(1.f / 30.f);
		TickPlayer(*Player, 1.f / 30.f);
		TickPlayer(*Passenger, 1.f / 30.f);
		const float DriverZ = static_cast<float>(Body->GetComponentLocation().Z);
		MinDriverAboveSurface = FMath::Min(MinDriverAboveSurface, DriverZ - SurfaceZ);
		MaxDriverAboveSurface = FMath::Max(MaxDriverAboveSurface, DriverZ - SurfaceZ);
		// 좌석 = 운전석 자리(선미)에서 기울어진 위로 좌석 바닥만큼 — 흔들림을 같이 타면 이 차이가 늘 0 근처다.
		MaxSeatError = FMath::Max(MaxSeatError, static_cast<float>(FVector::Dist(Body->GetComponentLocation(),
			Boat->GetSeatWorldTransform(0).GetLocation())));
		MinPassengerZ = FMath::Min(MinPassengerZ, static_cast<float>(Passenger->GetActorLocation().Z));
		MaxPassengerZ = FMath::Max(MaxPassengerZ, static_cast<float>(Passenger->GetActorLocation().Z));
	}
	TestEqual(TEXT("운전 = BoatSit"), CurrentClipName(*Player), FString(TEXT("A_Player_BoatSit")));
	TestEqual(TEXT("승객 = BoatSit"), CurrentClipName(*Passenger), FString(TEXT("A_Player_BoatSit")));
	TestTrue(TEXT("운전자 몸이 좌석(Hull + 좌석 바닥)을 따라간다"), MaxSeatError < 0.6f);
	const float MidAbove = (MinDriverAboveSurface + MaxDriverAboveSurface) * 0.5f;
	TestEqual(TEXT("앉은 높이 가운데 = 좌석 바닥 − 잠김"), MidAbove, SeatFloor - Draft, 1.5f);
	TestTrue(TEXT("승객이 출렁임을 같이 탄다"), MaxPassengerZ - MinPassengerZ > 2.f);
	const float PassengerSide = static_cast<float>(FVector::Dist2D(Passenger->GetActorLocation(), Boat->GetActorLocation()));
	AddInfo(FString::Printf(TEXT("[Body] 운전석 몸 수면 위 %.1f~%.1f (설계 %.0f−%.0f) · 좌석 오차 최대 %.2f · 승객 Z %.1f~%.1f · 승객 옆 %.1fcm"),
		MinDriverAboveSurface, MaxDriverAboveSurface, SeatFloor, Draft, MaxSeatError, MinPassengerZ, MaxPassengerZ, PassengerSide));

	// 운전석 = 선미 (레퍼런스 구명보트 — 모터 옆). 좌석 자리는 배 축(흔들림 기울기 포함)으로 되돌려 잰다.
	auto SeatLocalX = [Boat, Hull](int32 Seat)
	{
		const FTransform SeatTransform = Boat->GetSeatWorldTransform(Seat);
		return static_cast<float>(SeatTransform.GetRotation().UnrotateVector(SeatTransform.GetLocation() - Hull->GetComponentLocation()).X);
	};
	const float DriverX = SeatLocalX(0);
	const float BoatAheadOfDriver = static_cast<float>(FVector::DotProduct(Boat->GetActorLocation() - Player->GetActorLocation(),
		Boat->GetActorForwardVector()));
	TestEqual(TEXT("운전석 = 설계서 선미 자리"), DriverX, DesignDriverX, 0.5f);
	TestEqual(TEXT("승객 1 = 설계서 자리"), SeatLocalX(1), DesignPassenger1X, 0.5f);
	TestEqual(TEXT("승객 2 = 설계서 자리"), SeatLocalX(2), DesignPassenger2X, 0.5f);
	TestTrue(TEXT("운전석이 배 가운데보다 뒤(선미)"), DriverX < 0.f);
	TestEqual(TEXT("배 가운데는 운전자보다 운전석 거리만큼 앞"), BoatAheadOfDriver, -DesignDriverX, 1.f);
	// 좌석끼리 사람 캡슐 지름 이상 — 가까우면 두 몸이 겹쳐 앉는다.
	const float MinGap = 2.f * Player->GetCapsuleComponent()->GetUnscaledCapsuleRadius();
	float NearestPassenger = TNumericLimits<float>::Max();
	for (int32 Seat = 1; Seat < Boat->GetSeatCount(); ++Seat)
	{
		NearestPassenger = FMath::Min(NearestPassenger, static_cast<float>(FVector::Dist2D(Boat->GetSeatWorldTransform(0).GetLocation(),
			Boat->GetSeatWorldTransform(Seat).GetLocation())));
	}
	TestTrue(TEXT("운전석과 가장 가까운 승객석이 캡슐 지름 이상 떨어져 있다"), NearestPassenger >= MinGap);
	AddInfo(FString::Printf(TEXT("[Body] 운전석 배 축 X %.1f (설계 %.0f, 선미 −) · 배 가운데가 운전자보다 앞 %.1fcm · 승객석 X %.1f · %.1f · 가장 가까운 승객석 %.1fcm (최소 캡슐 지름 %.1f)"),
		DriverX, DesignDriverX, BoatAheadOfDriver, SeatLocalX(1), SeatLocalX(2), NearestPassenger, MinGap));

	Boat->UnseatAll();
	TestNull(TEXT("내리면 Hull 에서 떨어진다"), Passenger->GetRootComponent()->GetAttachParent());
	Boat->Destroy();
	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenWaterBodyFishingTest, "Snarehaven.Water.Body.FishingCastWaitReel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSnarehavenWaterBodyFishingTest::RunTest(const FString& Parameters)
{
	FString Doc;
	if (!TestTrue(TEXT("설계서"), LoadDesign(Doc)))
	{
		return false;
	}
	const float BoostSeconds = ReadDesignNumber(Doc, TEXT("낚시 몸 클립"), TEXT("\\*\\*([0-9.]+)초\\*\\* 동안 \\*\\*×([0-9.]+)"), 1);
	const float BoostRate = ReadDesignNumber(Doc, TEXT("낚시 몸 클립"), TEXT("\\*\\*([0-9.]+)초\\*\\* 동안 \\*\\*×([0-9.]+)"), 2);
	if (!TestTrue(TEXT("DESIGN 8.9 감기 빨라짐을 읽었다"), BoostSeconds > 0.f && BoostRate > 1.f))
	{
		return false;
	}

	UWorld* const World = CreateTestWorld();
	FFakeFishServer Server;
	Server.RollFish = CarpItemId.ToString();
	Server.Install();
	ASnarehavenCharacter* const Player = SpawnPlayer(World, TestLandPoint);
	UTapFishingComponent* const Fishing = Player ? Player->FindComponentByClass<UTapFishingComponent>() : nullptr;
	AFishingSpotActor* const Pond = SpawnSpot(World, TEXT("pond_1"), TestLandPoint);
	if (!TestNotNull(TEXT("낚시 컴포넌트"), Fishing) || !TestNotNull(TEXT("연못"), Pond) || !TestTrue(TEXT("사람 몸"), Player->ApplyPlayerBody()))
	{
		DestroyTestWorld(World);
		return false;
	}

	// 던지기 한 번 → 클립 길이가 지나면 대기 반복.
	Fishing->BeginBiteWait(Pond, TestBiteSeconds, false);
	TestTrue(TEXT("보이는 단계 = 판정 단계"), Fishing->GetShownState() == Fishing->GetState());
	TickPlayer(*Player, 0.1f);
	TestEqual(TEXT("던진 직후 = FishCast"), CurrentClipName(*Player), FString(TEXT("A_Player_FishCast")));
	const float CastLength = Player->GetCurrentBodyAnim() ? Player->GetCurrentBodyAnim()->GetPlayLength() : 0.f;
	TickPlayer(*Player, CastLength);
	TestEqual(TEXT("던지기가 끝나면 = FishIdle"), CurrentClipName(*Player), FString(TEXT("A_Player_FishIdle")));

	// 입질 → 감기. 누른 순간만 빨라지고, 설계서 초가 지나면 1배로.
	Fishing->AdvanceFishing(TestStepSeconds);
	TestTrue(TEXT("입질 → 연타"), Fishing->GetState() == ETapFishingState::Tapping);
	TestTrue(TEXT("보이는 단계도 연타"), Fishing->GetShownState() == ETapFishingState::Tapping);
	TickPlayer(*Player, 0.05f);
	TestEqual(TEXT("입질 뒤 = FishReel"), CurrentClipName(*Player), FString(TEXT("A_Player_FishReel")));
	const float RateBefore = Player->GetBodyAnimPlayRate();
	TestEqual(TEXT("안 누르면 1배"), RateBefore, 1.f, 0.01f);
	Fishing->Tap();
	TickPlayer(*Player, 0.05f);
	const float RateTapped = Player->GetBodyAnimPlayRate();
	TestEqual(TEXT("누르면 설계서 배율"), RateTapped, BoostRate, 0.01f);
	TickPlayer(*Player, BoostSeconds + 0.1f);
	const float RateAfter = Player->GetBodyAnimPlayRate();
	TestEqual(TEXT("설계서 초가 지나면 다시 1배"), RateAfter, 1.f, 0.01f);

	// 끝까지 감으면 쉼 → 서 있기. 보상은 가짜 서버가 한 번만 판정한다(몸 클립은 판정에 안 낀다).
	for (int32 Press = 0; Press < Fishing->GetPressesNeeded() && Fishing->GetState() == ETapFishingState::Tapping; ++Press)
	{
		Fishing->Tap();
	}
	TickPlayer(*Player, 0.1f);
	TestTrue(TEXT("잡으면 쉼"), Fishing->GetState() == ETapFishingState::Idle && Fishing->GetShownState() == ETapFishingState::Idle);
	TestEqual(TEXT("끝나면 = Idle"), CurrentClipName(*Player), FString(TEXT("A_Player_Idle")));
	TestEqual(TEXT("CatchFish 는 한 번"), Server.CatchCalls, 1);
	AddInfo(FString::Printf(TEXT("[Body] 낚시: FishCast %.2f초 → FishIdle → FishReel 속도 %.2f / 누름 %.2f / %.2f초 뒤 %.2f → Idle · CatchFish %d"),
		CastLength, RateBefore, RateTapped, BoostSeconds + 0.1f, RateAfter, Server.CatchCalls));

	DestroyTestWorld(World);
	return true;
}

// 감독 PIE 실측(10-05): 던져도 찌·줄이 없었다. 찌가 포인트 물 위에 앉고, 줄 두 끝이 대 끝·찌에 붙고,
// 감으면 물가 쪽으로 오고, 잡으면 발밑으로 튀어 들며 줄이 사라지고, 쉼이면 둘 다 숨는지 본다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenWaterFishingBobberLineTest, "Snarehaven.Water.Fishing.BobberLine",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSnarehavenWaterFishingBobberLineTest::RunTest(const FString& Parameters)
{
	UWorld* const World = CreateTestWorld();
	FFakeFishServer Server;
	Server.RollFish = CarpItemId.ToString();
	Server.Install();
	ASnarehavenCharacter* const Player = SpawnPlayer(World, TestLandPoint);
	UTapFishingComponent* const Fishing = Player ? Player->FindComponentByClass<UTapFishingComponent>() : nullptr;
	UEquipmentComponent* const Equipment = Player ? Player->FindComponentByClass<UEquipmentComponent>() : nullptr;
	// 포인트는 앞 3m, 물 표면은 발보다 조금 아래 (검사 전용 자리 — Verification.md 6).
	AFishingSpotActor* const Pond = SpawnSpot(World, TEXT("pond_1"), TestLandPoint + FVector(300.f, 0.f, -120.f));
	if (!TestNotNull(TEXT("낚시 컴포넌트"), Fishing) || !TestNotNull(TEXT("장비"), Equipment) || !TestNotNull(TEXT("연못"), Pond)
		|| !TestTrue(TEXT("사람 몸"), Player->ApplyPlayerBody())
		|| !TestTrue(TEXT("낚싯대를 들었다"), Equipment->EquipItemByItemID(FName(TEXT("item.rod_basic")))))
	{
		DestroyTestWorld(World);
		return false;
	}
	UStaticMeshComponent* const Bobber = Fishing->GetBobberMesh();
	const TArray<TObjectPtr<UStaticMeshComponent>>& Lines = Fishing->GetLineMeshes();
	UStaticMeshComponent* const Line = Lines.IsEmpty() ? nullptr : Lines[0].Get();
	const UStaticMeshComponent* const Rod = Equipment->GetEquippedMesh(EEquipSlot::Tool);
	if (!TestNotNull(TEXT("찌 메시"), Bobber) || !TestNotNull(TEXT("줄 메시"), Line) || !TestNotNull(TEXT("낚싯대 메시"), Rod)
		|| !TestTrue(TEXT("줄은 여러 토막(처진 곡선)"), Lines.Num() >= 4))
	{
		DestroyTestWorld(World);
		return false;
	}
	auto Step = [Player, Fishing](float Seconds)
	{
		constexpr float Dt = 0.05f;
		for (float Elapsed = 0.f; Elapsed < Seconds - KINDA_SMALL_NUMBER; Elapsed += Dt)
		{
			static_cast<AActor&>(*Player).Tick(Dt);
			Fishing->UpdateBobberAndLine(Dt);
		}
	};
	// 줄 원통 토막의 두 끝(메시 Z 범위 양 끝)을 세계로.
	auto End = [](const UStaticMeshComponent* Segment, bool bFar)
	{
		const FBox Box = Segment->GetStaticMesh()->GetBoundingBox();
		return Segment->GetComponentTransform().TransformPosition(FVector(Box.GetCenter().X, Box.GetCenter().Y, bFar ? Box.Max.Z : Box.Min.Z));
	};
	auto CheckLine = [this, Fishing, Bobber, &Lines, Rod, End](const TCHAR* When, bool bSag)
	{
		const FVector A = End(Lines[0], false);
		const FVector B = End(Lines.Last(), true);
		const FVector Tip = Fishing->GetRodTipLocation();
		// 토막끼리 끊김 없이 잇고, 대기 중엔 가운데가 곧은 줄보다 처진다.
		float Joint = 0.f;
		for (int32 Index = 1; Index < Lines.Num(); ++Index)
		{
			Joint = FMath::Max(Joint, static_cast<float>(FVector::Dist(End(Lines[Index - 1], true), End(Lines[Index], false))));
		}
		TestTrue(FString::Printf(TEXT("%s: 토막 이음 %.2fcm"), When, Joint), Joint < 1.f);
		const FVector Mid = End(Lines[Lines.Num() / 2 - 1], true);
		const float SagCm = FMath::Lerp(A.Z, B.Z, 0.5) - Mid.Z;
		if (bSag)
		{
			TestTrue(FString::Printf(TEXT("%s: 줄 가운데가 처짐 %.1fcm"), When, SagCm), SagCm > 5.f);
		}
		// 낚시 중엔 대 끝을 세운다 — 손보다 위.
		const float TipUp = Tip.Z - Rod->GetComponentLocation().Z;
		TestTrue(FString::Printf(TEXT("%s: 대 끝이 손보다 위 %.1fcm"), When, TipUp), TipUp > 30.f);
		// 대 끝은 실제 낚싯대 메시 상자 안이고 손에서 멀다(손잡이 쪽이 아니다).
		const FBox RodBox = Rod->Bounds.GetBox().ExpandBy(1.f);
		const float TipFromHand = FVector::Dist(Tip, Rod->GetComponentLocation());
		TestTrue(FString::Printf(TEXT("%s: 대 끝이 낚싯대 메시 안"), When), RodBox.IsInside(Tip));
		TestTrue(FString::Printf(TEXT("%s: 대 끝이 손에서 50cm 넘게 (%.1f)"), When, TipFromHand), TipFromHand > 50.f);
		const float AGap = FVector::Dist(A, Tip);
		const float BGap = FVector::Dist(B, Bobber->GetComponentLocation());
		TestTrue(FString::Printf(TEXT("%s: 줄 한 끝 = 대 끝 (%.2fcm)"), When, AGap), AGap < 1.f);
		TestTrue(FString::Printf(TEXT("%s: 줄 다른 끝 = 찌 (%.2fcm)"), When, BGap), BGap < 1.f);
		AddInfo(FString::Printf(TEXT("[Bobber] %s 줄 끝 오차 대 %.2f / 찌 %.2fcm · 대 끝-손 %.1fcm(위로 %.1f) · 줄 %d토막 처짐 %.1fcm"),
			When, AGap, BGap, TipFromHand, TipUp, Lines.Num(), SagCm));
	};

	TestFalse(TEXT("던지기 전 찌 숨김"), Bobber->IsVisible());
	TestFalse(TEXT("던지기 전 줄 숨김"), Line->IsVisible());

	// ① 던짐 — 날아가는 동안은 물보다 위, 착수 뒤엔 포인트 물 높이에서 둥실.
	const FVector Water = Pond->GetActorLocation();
	Fishing->BeginBiteWait(Pond, 30.f, false);
	TestTrue(TEXT("찌가 내려앉을 점 = 포인트"), Fishing->GetShownCastTarget().Equals(Water, 1.f));
	Step(Fishing->BobberFlightSeconds * 0.5f);
	TestTrue(TEXT("날아가는 중 찌·줄 보임"), Bobber->IsVisible() && Line->IsVisible());
	const float MidAir = Bobber->GetComponentLocation().Z - Water.Z;
	TestTrue(FString::Printf(TEXT("날아가는 중엔 물보다 위 (%.1fcm)"), MidAir), MidAir > 10.f);
	Step(Fishing->BobberFlightSeconds + 0.1f);
	const FVector Landed = Bobber->GetComponentLocation();
	const float Flat = FVector::Dist2D(Landed, Water);
	const float Height = FMath::Abs(Landed.Z - Water.Z);
	TestTrue(FString::Printf(TEXT("착수: 포인트 가로 %.2fcm"), Flat), Flat < 1.f);
	TestTrue(FString::Printf(TEXT("착수: 물 높이 ±%.2fcm (둥실 %.1f)"), Height, Fishing->BobberFloatAmplitudeCm),
		Height <= Fishing->BobberFloatAmplitudeCm + 0.1f);
	CheckLine(TEXT("대기"), true);
	AddInfo(FString::Printf(TEXT("[Bobber] 착수 %.2f초 · 날 때 물 위 %.1fcm · 착수 가로 %.2f · 높이 %.2fcm"),
		Fishing->BobberFlightSeconds, MidAir, Flat, Height));

	// ② 그만두면 쉼 — 둘 다 숨는다.
	Fishing->Quit();
	Step(0.05f);
	TestTrue(TEXT("그만둠 → 쉼"), Fishing->GetShownState() == ETapFishingState::Idle);
	TestFalse(TEXT("쉼: 찌 숨김"), Bobber->IsVisible());
	TestFalse(TEXT("쉼: 줄 숨김"), Line->IsVisible());

	// ③ 다시 던져 입질 → 감기: 물가 쪽으로 온다.
	Fishing->BeginBiteWait(Pond, TestBiteSeconds, false);
	Step(Fishing->BobberFlightSeconds + 0.1f);
	Fishing->AdvanceFishing(TestStepSeconds);
	if (!TestTrue(TEXT("입질 → 연타"), Fishing->GetState() == ETapFishingState::Tapping))
	{
		DestroyTestWorld(World);
		return false;
	}
	Step(0.05f);
	const float FarBefore = FVector::Dist2D(Bobber->GetComponentLocation(), Player->GetActorLocation());
	for (int32 Press = 0; Press < Fishing->GetPressesNeeded() / 2; ++Press)
	{
		Fishing->Tap();
	}
	Step(0.05f);
	const float FarAfter = FVector::Dist2D(Bobber->GetComponentLocation(), Player->GetActorLocation());
	TestTrue(FString::Printf(TEXT("감으면 물가 쪽으로 (%.1f → %.1fcm)"), FarBefore, FarAfter), FarAfter < FarBefore - 10.f);
	CheckLine(TEXT("감기"), false);

	// ④ 잡으면 발밑으로 튀어 들며 줄은 바로 사라지고, 다 들어오면 찌도 숨는다.
	for (int32 Press = 0; Press < Fishing->GetPressesNeeded() && Fishing->GetState() == ETapFishingState::Tapping; ++Press)
	{
		Fishing->Tap();
	}
	TestTrue(TEXT("잡으면 쉼"), Fishing->GetState() == ETapFishingState::Idle);
	Step(0.05f);
	TestTrue(TEXT("잡은 직후 찌는 튀어 드는 중"), Bobber->IsVisible());
	TestFalse(TEXT("잡은 직후 줄은 사라짐"), Line->IsVisible());
	Step(Fishing->BobberHopSeconds + 0.05f);
	TestFalse(TEXT("다 들어오면 찌 숨김"), Bobber->IsVisible());
	TestFalse(TEXT("다 들어오면 줄 숨김"), Line->IsVisible());
	AddInfo(FString::Printf(TEXT("[Bobber] 감기 물가 쪽 %.1f → %.1fcm · 잡음 → %.2f초 뒤 숨김 · CatchFish %d"),
		FarBefore, FarAfter, Fishing->BobberHopSeconds, Server.CatchCalls));
	const float RestUp = Fishing->GetRodTipLocation().Z - Rod->GetComponentLocation().Z;

	// ⑤ 검사 스위치(-SnareFishTap) 판 — 붙든 자리표시는 땅 위였다(감독 캡처 10-16). 다음 틱에 포인트를 찾아 찌가 물 높이에 앉는다.
	Fishing->HoldTestTapping(0.5f);
	Fishing->AdvanceFishing(TestStepSeconds);
	Step(0.05f);
	const float HoldHeight = Bobber->GetComponentLocation().Z - Water.Z;
	TestTrue(TEXT("붙든 판: 찌가 내려앉을 점 = 포인트"), Fishing->GetShownCastTarget().Equals(Water, 1.f));
	TestTrue(FString::Printf(TEXT("붙든 판: 찌가 물 높이 (%.2fcm)"), HoldHeight),
		FMath::Abs(HoldHeight) <= Fishing->BobberBiteDipCm + Fishing->BobberFloatAmplitudeCm);
	TestTrue(TEXT("붙든 판: 찌·줄 보임"), Bobber->IsVisible() && Line->IsVisible());
	CheckLine(TEXT("붙든 판"), false);
	AddInfo(FString::Printf(TEXT("[Bobber] 붙든 판 찌 물 높이 %.2fcm · 쉴 때 대 끝 손 위 %.1fcm"), HoldHeight, RestUp));

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenWaterBodyRemoteReelTest, "Snarehaven.Water.Body.RemoteReelBoost",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSnarehavenWaterBodyRemoteReelTest::RunTest(const FString& Parameters)
{
	FString Doc;
	if (!TestTrue(TEXT("설계서"), LoadDesign(Doc)))
	{
		return false;
	}
	const float BoostSeconds = ReadDesignNumber(Doc, TEXT("낚시 몸 클립"), TEXT("\\*\\*([0-9.]+)초\\*\\* 동안 \\*\\*×([0-9.]+)"), 1);
	const float BoostRate = ReadDesignNumber(Doc, TEXT("낚시 몸 클립"), TEXT("\\*\\*([0-9.]+)초\\*\\* 동안 \\*\\*×([0-9.]+)"), 2);
	if (!TestTrue(TEXT("DESIGN 8.9 감기 빨라짐을 읽었다"), BoostSeconds > 0.f && BoostRate > 1.f))
	{
		return false;
	}

	// 누름 수가 실제로 복제 목록에 있다 — 주인은 자기 값을 쓰므로 받지 않는다(COND_SkipOwner).
	// 넷 드라이버 없는 검사라 복제 번호(RepIndex)를 먼저 매긴다 — 안 매기면 속성이 모두 0번이라 등록이 부딪친다.
	UTapFishingComponent::StaticClass()->SetUpRuntimeReplicationData();
	FProperty* const StateProp = FindFProperty<FProperty>(UTapFishingComponent::StaticClass(), TEXT("ShownState"));
	FProperty* const PressProp = FindFProperty<FProperty>(UTapFishingComponent::StaticClass(), TEXT("ShownPressCount"));
	if (!TestNotNull(TEXT("ShownState 속성"), StateProp) || !TestNotNull(TEXT("ShownPressCount 속성"), PressProp))
	{
		return false;
	}
	TArray<FLifetimeProperty> Lifetime;
	GetDefault<UTapFishingComponent>()->GetLifetimeReplicatedProps(Lifetime);
	TestTrue(TEXT("누름 수가 COND_SkipOwner 로 복제된다"), Lifetime.ContainsByPredicate([PressProp](const FLifetimeProperty& Rep)
		{ return Rep.RepIndex == PressProp->RepIndex && Rep.Condition == COND_SkipOwner; }));

	UWorld* const World = CreateTestWorld();
	ASnarehavenCharacter* const Remote = SpawnPlayer(World, TestLandPoint);
	UTapFishingComponent* const Fishing = Remote ? Remote->FindComponentByClass<UTapFishingComponent>() : nullptr;
	if (!TestNotNull(TEXT("낚시 컴포넌트"), Fishing) || !TestTrue(TEXT("사람 몸"), Remote->ApplyPlayerBody()))
	{
		DestroyTestWorld(World);
		return false;
	}
	// 남의 기계 흉내: 판정 단계는 쉼 그대로이고, 넷 드라이버가 하듯 복제 값만 메모리에 들어온다.
	auto Receive = [Fishing, StateProp, PressProp](ETapFishingState State, uint8 Presses)
	{
		*StateProp->ContainerPtrToValuePtr<ETapFishingState>(Fishing) = State;
		*PressProp->ContainerPtrToValuePtr<uint8>(Fishing) = Presses;
	};
	Receive(ETapFishingState::Tapping, 0);
	TickPlayer(*Remote, 0.05f);
	TestEqual(TEXT("원격 연타 = FishReel"), CurrentClipName(*Remote), FString(TEXT("A_Player_FishReel")));
	const float RateBefore = Remote->GetBodyAnimPlayRate();
	TestEqual(TEXT("원격: 누름 전 1배"), RateBefore, 1.f, 0.01f);

	Receive(ETapFishingState::Tapping, 1);
	TickPlayer(*Remote, 0.05f);
	const float RateTapped = Remote->GetBodyAnimPlayRate();
	TestEqual(TEXT("원격: 복제된 누름이 오면 설계서 배율"), RateTapped, BoostRate, 0.01f);
	TickPlayer(*Remote, BoostSeconds + 0.1f);
	const float RateAfter = Remote->GetBodyAnimPlayRate();
	TestEqual(TEXT("원격: 설계서 초가 지나면 다시 1배"), RateAfter, 1.f, 0.01f);
	TestTrue(TEXT("원격은 판정 단계를 안 건드린다"), Fishing->GetState() == ETapFishingState::Idle);

	// 255 다음 0 으로 돌아도 누름으로 본다 (uint8).
	Receive(ETapFishingState::Tapping, 255);
	TickPlayer(*Remote, BoostSeconds + 0.1f);
	Receive(ETapFishingState::Tapping, 0);
	TickPlayer(*Remote, 0.05f);
	const float RateWrapped = Remote->GetBodyAnimPlayRate();
	TestEqual(TEXT("원격: 255 → 0 도 누름"), RateWrapped, BoostRate, 0.01f);
	AddInfo(FString::Printf(TEXT("[Body] 원격 감기: 누름 전 %.2f / 복제 누름 %.2f / %.2f초 뒤 %.2f / 255→0 %.2f"),
		RateBefore, RateTapped, BoostSeconds + 0.1f, RateAfter, RateWrapped));

	DestroyTestWorld(World);
	return true;
}

#endif
