// STATUS #0172 결함 2층 검사 — 목장 칸마다 발판·난간이 있고, 잠긴 칸은 셋 다 숨는가:
// ① `L_Ranch` 에 칸 상한(`DT_BaseRuleTable` Base 행 `RanchSlotMax`)만큼의 슬롯이 전부 발판(`SM_Prop_Planks_01`)과
//    난간(`SM_Building_Fence_02`)을 붙여 갖고 있다 (`tools/unreal/setup_ranch_buildings.py` 가 붙인다),
// ② 칸 수가 기본값이면 그 너머 슬롯은 몸·발판·난간 전부 숨고 충돌이 꺼진다 (`URanchInteractComponent::ApplySlotVisibility`).
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.RanchSlotDeco; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/StaticMeshComponent.h"
#include "Data/BaseRuleRow.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/Level.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Ranch/RanchComponent.h"
#include "Ranch/RanchInteractComponent.h"
#include "Ranch/RanchSlotActor.h"

namespace SnarehavenRanchSlotDecoTest
{
	const TCHAR* RanchLevelPath = TEXT("/Game/Game/Maps/L_Ranch.L_Ranch");
	const TCHAR* BaseRuleTablePath = TEXT("/Game/Game/Data/Balance/DT_BaseRuleTable.DT_BaseRuleTable");
	const TCHAR* PlankMeshName = TEXT("SM_Prop_Planks_01");
	const TCHAR* RailMeshName = TEXT("SM_Building_Fence_02");

	FString MeshNameOf(const AStaticMeshActor* Actor)
	{
		const UStaticMeshComponent* const Component = Actor->GetStaticMeshComponent();
		const UStaticMesh* const Mesh = Component != nullptr ? Component->GetStaticMesh() : nullptr;
		return Mesh != nullptr ? Mesh->GetName() : FString();
	}

	void SetSlotIndex(ARanchSlotActor* Slot, int32 SlotIndex)
	{
		if (FIntProperty* const IndexProperty =
			CastField<FIntProperty>(ARanchSlotActor::StaticClass()->FindPropertyByName(TEXT("SlotIndex"))))
		{
			IndexProperty->SetPropertyValue_InContainer(Slot, SlotIndex);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenRanchSlotDecoTest, "Snarehaven.RanchSlotDeco.Level.EverySlotHasPlankAndRail",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenRanchSlotDecoTest::RunTest(const FString& Parameters)
{
	using namespace SnarehavenRanchSlotDecoTest;

	const UDataTable* const Table = LoadObject<UDataTable>(nullptr, BaseRuleTablePath);
	const FBaseRuleRow* const Rules = Table != nullptr ? Table->FindRow<FBaseRuleRow>(TEXT("Base"), TEXT("RanchSlotDecoTest")) : nullptr;
	const UWorld* const Level = LoadObject<UWorld>(nullptr, RanchLevelPath);
	if (!TestNotNull(TEXT("DT_BaseRuleTable 의 Base 행"), Rules) || !TestNotNull(TEXT("L_Ranch"), Level)
		|| !TestNotNull(TEXT("L_Ranch 레벨"), Level->PersistentLevel.Get()))
	{
		return false;
	}
	const int32 SlotMax = Rules->RanchSlotMax;
	const int32 BaseSlots = GetDefault<URanchComponent>()->GetBaseSlotCount();
	TestTrue(TEXT("칸 상한이 기본 칸보다 크다"), SlotMax > BaseSlots);

	// ① 레벨: 칸 번호마다 슬롯이 있고, 그 슬롯에 붙은 액터 메시에 발판·난간이 둘 다 있다.
	TMap<int32, TArray<FString>> PiecesBySlot;
	TSet<int32> SlotsInLevel;
	for (const AActor* Actor : Level->PersistentLevel->Actors)
	{
		if (const ARanchSlotActor* const Slot = Cast<ARanchSlotActor>(Actor))
		{
			SlotsInLevel.Add(Slot->GetSlotIndex());
		}
		else if (const AStaticMeshActor* const Piece = Cast<AStaticMeshActor>(Actor))
		{
			if (const ARanchSlotActor* const Parent = Cast<ARanchSlotActor>(Piece->GetAttachParentActor()))
			{
				PiecesBySlot.FindOrAdd(Parent->GetSlotIndex()).Add(MeshNameOf(Piece));
			}
		}
	}
	int32 Dressed = 0;
	for (int32 Index = 0; Index < SlotMax; ++Index)
	{
		const TArray<FString>* const Pieces = PiecesBySlot.Find(Index);
		const bool bPlank = Pieces != nullptr && Pieces->Contains(PlankMeshName);
		const bool bRail = Pieces != nullptr && Pieces->Contains(RailMeshName);
		TestTrue(*FString::Printf(TEXT("%d번 칸 슬롯이 레벨에 있다"), Index + 1), SlotsInLevel.Contains(Index));
		TestTrue(*FString::Printf(TEXT("%d번 칸 발판"), Index + 1), bPlank);
		TestTrue(*FString::Printf(TEXT("%d번 칸 난간"), Index + 1), bRail);
		Dressed += (bPlank && bRail) ? 1 : 0;
	}

	// ② 숨김: 칸 상한만큼 슬롯을 세우고 발판·난간을 붙인 뒤 기본 칸 수만 보이게 한다.
	UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());

	TArray<TArray<AActor*>> Rigs;
	for (int32 Index = 0; Index < SlotMax; ++Index)
	{
		ARanchSlotActor* const Slot = World->SpawnActor<ARanchSlotActor>(FVector(0.f, Index * 200.f, 0.f), FRotator::ZeroRotator);
		AStaticMeshActor* const Plank = World->SpawnActor<AStaticMeshActor>();
		AStaticMeshActor* const Rail = World->SpawnActor<AStaticMeshActor>();
		if (Slot == nullptr || Plank == nullptr || Rail == nullptr)
		{
			AddError(TEXT("테스트 액터 스폰 실패"));
			break;
		}
		SetSlotIndex(Slot, Index);
		for (AStaticMeshActor* const Piece : { Plank, Rail })
		{
			// 레벨 쪽과 같게 Movable — 스태틱은 움직이는 슬롯 루트에 붙지 않는다 (setup_ranch_buildings.py `_apply`).
			Piece->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
			Piece->AttachToActor(Slot, FAttachmentTransformRules::KeepWorldTransform);
			TestTrue(*FString::Printf(TEXT("%d번 칸에 붙었다"), Index + 1), Piece->GetAttachParentActor() == Slot);
		}
		Rigs.Add({ Slot, Plank, Rail });
	}

	TestEqual(TEXT("기본 칸 수만 보인다"), URanchInteractComponent::ApplySlotVisibility(World, BaseSlots), BaseSlots);
	int32 HiddenPieces = 0;
	for (int32 Index = 0; Index < Rigs.Num(); ++Index)
	{
		const bool bOpen = Index < BaseSlots;
		for (const AActor* Piece : Rigs[Index])
		{
			TestEqual(*FString::Printf(TEXT("%d번 칸 %s 숨김"), Index + 1, *Piece->GetClass()->GetName()), Piece->IsHidden(), !bOpen);
			TestEqual(*FString::Printf(TEXT("%d번 칸 %s 충돌"), Index + 1, *Piece->GetClass()->GetName()), Piece->GetActorEnableCollision(), bOpen);
			HiddenPieces += Piece->IsHidden() ? 1 : 0;
		}
	}

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);

	AddInfo(FString::Printf(TEXT("[RanchSlotDeco] 발판·난간 갖춘 칸 %d/%d · 칸 수 %d 일 때 숨은 액터 %d (슬롯·발판·난간 × %d칸)"),
		Dressed, SlotMax, BaseSlots, HiddenPieces, SlotMax - BaseSlots));
	return true;
}

#endif
