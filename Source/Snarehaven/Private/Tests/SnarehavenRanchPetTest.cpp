// 목장 펫 모습 2층 검사 (DESIGN 3.2 「목장 펫 모습」) — 맡긴 펫이 말뚝 곁에 보이고 목줄 안에서만 거니는가:
// ① `ShowPet(돼지 행)` 이면 칸에 돼지 몸이 서고, 같은 종을 다시 부르면 그 몸을 그대로 둔다(걷던 펫이 튀지 않게),
// ② 몇 초 틱해도 몸은 말뚝에서 목줄 반경(+보폭 여유) 안에 있다,
// ③ `ShowPet(nullptr)` 이면 몸이 사라진다, ④ 앞쪽 방향을 주면 뒤 난간 쪽 부채꼴 밖으로 가지 않는다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.RanchPet; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Data/AnimalRow.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Ranch/RanchPetActor.h"
#include "Ranch/RanchSlotActor.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenRanchPetTest, "Snarehaven.RanchPet.ShowsAndTethers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenRanchPetTest::RunTest(const FString& Parameters)
{
	const UDataTable* const Table = LoadObject<UDataTable>(nullptr, TEXT("/Game/Game/Data/Animals/DT_AnimalTable.DT_AnimalTable"));
	const FAnimalRow* const Pig = Table != nullptr ? Table->FindRow<FAnimalRow>(TEXT("Animal_Pig"), TEXT("RanchPetTest")) : nullptr;
	if (!TestNotNull(TEXT("DT_AnimalTable Animal_Pig 행"), Pig))
	{
		return false;
	}

	UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();

	ARanchSlotActor* const Slot = World->SpawnActor<ARanchSlotActor>(FVector(100.f, 200.f, 0.f), FRotator::ZeroRotator);
	if (TestNotNull(TEXT("슬롯 스폰"), Slot))
	{
		Slot->ShowPet(Pig);
		ARanchPetActor* const First = Slot->GetPet();
		TestNotNull(TEXT("① 돼지 몸이 선다"), First);
		Slot->ShowPet(Pig);
		TestTrue(TEXT("① 같은 종이면 몸을 그대로 둔다"), Slot->GetPet() == First);

		float FarthestCm = 0.f;
		float NearestCm = TNumericLimits<float>::Max();
		bool bWalked = false;
		for (int32 Frame = 0; Frame < 30 * 20 && First != nullptr; ++Frame)   // 20초, 30fps
		{
			First->Tick(1.f / 30.f);
			bWalked |= First->IsWalking();
			const float Cm = FVector::Dist2D(First->GetActorLocation(), Slot->GetActorLocation());
			FarthestCm = FMath::Max(FarthestCm, Cm);
			NearestCm = FMath::Min(NearestCm, Cm);
		}
		TestTrue(TEXT("② 20초 안에 한 번은 걷는다"), bWalked);
		TestTrue(TEXT("② 목줄 반경 안에 있다"), FarthestCm <= ARanchPetActor::TetherRadiusCm + 5.f);
		TestTrue(TEXT("② 말뚝에 박히지 않는다"), NearestCm >= ARanchPetActor::TetherMinCm * 0.7f);

		// ④ 앞쪽(-X)을 주면 뒤(+X 난간 쪽)로 가지 않는다 — 부채꼴 ±75° 라 말뚝 줄보다 뒤로 넘어가는 점이 없다.
		First->SetTetherHome(Slot->GetActorLocation(), FVector(-1.f, 0.f, 0.f));
		float BackmostCm = -TNumericLimits<float>::Max();
		for (int32 Frame = 0; Frame < 30 * 20; ++Frame)
		{
			First->Tick(1.f / 30.f);
			BackmostCm = FMath::Max(BackmostCm, First->GetActorLocation().X - Slot->GetActorLocation().X);
		}
		TestTrue(TEXT("④ 뒤 난간 쪽으로 가지 않는다"), BackmostCm <= 1.f);

		Slot->ShowPet(nullptr);
		TestNull(TEXT("③ 치우면 몸이 없다"), Slot->GetPet());
		TestTrue(TEXT("③ 몸 액터가 파괴됐다"), First == nullptr || !IsValid(First));
		AddInfo(FString::Printf(TEXT("[RanchPet] 말뚝에서 %.0f~%.0fcm (고리 %.0f~%.0f) · 걸음 %s · 앞쪽만일 때 가장 뒤 %+.0fcm"),
			NearestCm, FarthestCm, ARanchPetActor::TetherMinCm, ARanchPetActor::TetherRadiusCm, bWalked ? TEXT("있음") : TEXT("없음"), BackmostCm));
	}

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

#endif
