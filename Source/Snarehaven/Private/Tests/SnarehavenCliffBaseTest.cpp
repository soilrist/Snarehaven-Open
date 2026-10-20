// M19-04 2층 검사 — 절벽이 한 판 회분홍이 아니라 회색 바위로 보이고 밑동에 바위가 박혀 있는가 (ROADMAP M19-04):
// (a) 절벽 머티리얼 `MI_Ground_Cliff` 의 `DiffuseColor` 를 sRGB 로 바꾼 채도 ≤ MaxSaturation,
// (b) 저장된 `L_Hunt` 를 **실제로 불러와** 시작 절벽 단(`StartCliff`) 둘레 RingCm 안 바위 메시 ≥ MinBaseRocks.
// 칠하고 놓는 도구는 `tools/unreal/setup_start_cliff.py`. 맵은 `SnarehavenFlowerClumpTest.cpp` 처럼 스트리밍한다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.World.Cliff; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Snarehaven.h"
#include "Engine/Engine.h"
#include "Engine/Level.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceConstant.h"

namespace SnarehavenCliffBaseTest
{
	// 테스트 전용 값 (Verification.md — 설계 숫자가 아니다). 채도·바위 수는 ROADMAP M19-04 합격선.
	constexpr float MaxSaturation = 0.15f;
	constexpr int32 MinBaseRocks = 3;
	constexpr double RingCm = 250.0;           // 절벽 경계 상자 반폭 바깥으로 이만큼 — 발치에 붙은 것만 센다
	const TCHAR* CliffMaterialPath = TEXT("/Game/Game/Environment/Ground/MI_Ground_Cliff.MI_Ground_Cliff");
	const TCHAR* HuntLevel = TEXT("/Game/Game/Maps/L_Hunt");
	const TCHAR* CliffLabel = TEXT("StartCliff");
	const TCHAR* RockMeshPrefixes[] = { TEXT("SM_Nature_Boulder"), TEXT("SM_Nature_Rock") };

	UWorld* CreateWorldWithLevel(const TCHAR* LevelPackage, ULevel*& OutLevel)
	{
		UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
		FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
		WorldContext.SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		bool bSuccess = false;
		const ULevelStreamingDynamic* const Streaming = ULevelStreamingDynamic::LoadLevelInstance(
			World, LevelPackage, FVector::ZeroVector, FRotator::ZeroRotator, bSuccess);
		World->FlushLevelStreaming(EFlushLevelStreamingType::Full);
		OutLevel = bSuccess && Streaming != nullptr ? Streaming->GetLoadedLevel() : nullptr;
		return World;
	}

	void DestroyTestWorld(UWorld* World)
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	}

	bool IsRock(const UStaticMesh* Mesh)
	{
		if (Mesh == nullptr)
		{
			return false;
		}
		const FString MeshName = Mesh->GetName();
		for (const TCHAR* const Prefix : RockMeshPrefixes)
		{
			if (MeshName.StartsWith(Prefix))
			{
				return true;
			}
		}
		return false;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenCliffBaseTest, "Snarehaven.World.Cliff.GrayWithBaseRocks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenCliffBaseTest::RunTest(const FString& Parameters)
{
	using namespace SnarehavenCliffBaseTest;

	const UMaterialInstanceConstant* const CliffMaterial = LoadObject<UMaterialInstanceConstant>(nullptr, CliffMaterialPath);
	if (!TestNotNull(TEXT("MI_Ground_Cliff"), CliffMaterial))
	{
		return false;
	}
	FLinearColor Diffuse = FLinearColor::Black;
	TestTrue(TEXT("DiffuseColor 파라미터"), CliffMaterial->GetVectorParameterValue(FHashedMaterialParameterInfo(TEXT("DiffuseColor")), Diffuse));
	const FColor Srgb = Diffuse.ToFColorSRGB();
	const FLinearColor Hsv = FLinearColor(Srgb.R / 255.f, Srgb.G / 255.f, Srgb.B / 255.f).LinearRGBToHSV();
	TestTrue(*FString::Printf(TEXT("절벽 sRGB (%d,%d,%d) 채도 %.3f ≤ %.2f"), Srgb.R, Srgb.G, Srgb.B, Hsv.G, MaxSaturation),
		Hsv.G <= MaxSaturation);

	ULevel* Level = nullptr;
	UWorld* const World = CreateWorldWithLevel(HuntLevel, Level);
	if (!TestNotNull(TEXT("L_Hunt 스트리밍"), Level))
	{
		DestroyTestWorld(World);
		return false;
	}
	const AActor* Cliff = nullptr;
	for (const AActor* const Actor : Level->Actors)
	{
		if (Actor != nullptr && Actor->GetActorLabel() == CliffLabel)
		{
			Cliff = Actor;
			break;
		}
	}
	if (!TestNotNull(TEXT("L_Hunt StartCliff"), Cliff))
	{
		DestroyTestWorld(World);
		return false;
	}
	FVector CliffOrigin;
	FVector CliffExtent;
	Cliff->GetActorBounds(false, CliffOrigin, CliffExtent);
	const double Reach = FMath::Max(CliffExtent.X, CliffExtent.Y) + RingCm;
	int32 Rocks = 0;
	for (const AActor* const Actor : Level->Actors)
	{
		const AStaticMeshActor* const MeshActor = Cast<AStaticMeshActor>(Actor);
		if (MeshActor != nullptr && IsRock(MeshActor->GetStaticMeshComponent()->GetStaticMesh())
			&& FVector2D::Distance(FVector2D(MeshActor->GetActorLocation()), FVector2D(CliffOrigin)) <= Reach)
		{
			++Rocks;
		}
	}
	TestTrue(*FString::Printf(TEXT("절벽 둘레 %.0fcm 안 바위 %d ≥ %d"), Reach, Rocks, MinBaseRocks), Rocks >= MinBaseRocks);
	UE_LOG(LogSnarehaven, Display, TEXT("[CliffBase] 절벽 sRGB (%d,%d,%d) 채도 %.3f · StartCliff 둘레 %.0fcm 안 바위 %d"),
		Srgb.R, Srgb.G, Srgb.B, Hsv.G, Reach, Rocks);
	DestroyTestWorld(World);
	return true;
}

#endif
