// 스킨 조각 l (`docs/specs/skin-parts.md` 4장 · 6장 l) 2층 검사 — 꾸미기 배낭 6벌이 진짜 표·진짜 메시로 등에 메진다.
//  ① 표에 배낭 꾸미기 행이 6개(일반 3·희귀 2·에픽 1), 메시는 배낭 폴더의 자기 스태틱 메시 · 삼각형 ≤ 1,500(명세 4장 스태틱) ·
//     소켓 `Socket_Back` · 몸 배율을 따른다(`bEquipFollowsBodyScale`).
//  ② 빈 배낭 칸에 `SetCosmetic` 으로 메면 보이는 메시 = 꾸미기 · 몸의 `Socket_Back` 에 붙음 · 월드 배율 = 몸 배율.
//  ③ 등에 붙는다 — 서 있기 첫 프레임에서 배낭 가운데가 가슴 뼈보다 등 쪽, 몸 쪽 면과 등 표면(가슴 − 앞 × 10.6 × 몸 배율)
//     사이 틈 0~3cm(하이킹 배낭 `SnarehavenRodBackpackMeshTest` ④ 와 같은 잣대 — 뚫지도 뜨지도 않는다).
//  ④ 하이킹 배낭(+8칸) 위에 덮으면 보이는 것만 꾸미기, 장비 줄은 하이킹 배낭 그대로 · 꾸미기를 벗기면 하이킹 메시로 돌아온다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.Item.Cosmetic; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/ItemRow.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Equipment/EquipmentComponent.h"
#include "Snarehaven.h"
#include "SnarehavenCharacter.h"

namespace SnareCosmeticBackpackTest
{
	const TCHAR* ItemTablePath = TEXT("/Game/Game/Data/Items/DT_ItemTable.DT_ItemTable");
	const TCHAR* BackpackFolder = TEXT("/Game/Game/Characters/Equipment/Backpacks/");
	const TCHAR* IdlePose = TEXT("/Game/Game/Characters/Player/Snare/A_Player_Idle.A_Player_Idle");
	const TCHAR* HikingMeshName = TEXT("SM_Backpack_HikingBackpack_01");
	const FName BackSocket(TEXT("Socket_Back"));
	const FName HikingItemID(TEXT("item.hiking_backpack"));

	// 검사 전용 값 — 명세 4장 「모자·얼굴 장식·배낭 스태틱 1,500」, 등 표면은 `setup_equipment_socket.py` BACK_SURFACE_CM 과 같다.
	constexpr int32 BackpackTriangleLimit = 1500;
	constexpr double BackSurfaceCm = 10.2;
	constexpr double BackGapMaxCm = 3.0;
	constexpr double FloatSlackCm = 0.05;
	constexpr double ScaleTolerance = 0.01;

	/** 메시 로컬 상자 8 꼭짓점을 월드로 옮겨 Direction 방향으로 가장 먼 값 — 월드 AABB 는 돌면 부푼다. */
	double FarthestAlong(const UStaticMeshComponent* Visual, const FVector& Direction)
	{
		const FBox Local = Visual->GetStaticMesh()->GetBoundingBox();
		double Best = -UE_BIG_NUMBER;
		for (int32 Corner = 0; Corner < 8; ++Corner)
		{
			const FVector Point((Corner & 1) ? Local.Max.X : Local.Min.X, (Corner & 2) ? Local.Max.Y : Local.Min.Y,
				(Corner & 4) ? Local.Max.Z : Local.Min.Z);
			Best = FMath::Max(Best, FVector::DotProduct(Visual->GetComponentTransform().TransformPosition(Point), Direction));
		}
		return Best;
	}

	/** 서 있기 첫 프레임으로 뼈를 놓는다 — 시험 월드는 틱이 없어 참조 자세(T자)로 남는다. */
	bool SetIdlePose(USkeletalMeshComponent* Body)
	{
		UAnimSequence* const Anim = LoadObject<UAnimSequence>(nullptr, IdlePose);
		if (Anim == nullptr)
		{
			return false;
		}
		Body->PlayAnimation(Anim, false);
		Body->SetPosition(0.f, false);
		Body->TickAnimation(0.f, false);
		Body->RefreshBoneTransforms();
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenCosmeticBackpackRowsTest, "Snarehaven.Item.Cosmetic.BackpackRows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenCosmeticBackpackRowsTest::RunTest(const FString& Parameters)
{
	using namespace SnareCosmeticBackpackTest;

	UDataTable* const ItemTable = LoadObject<UDataTable>(nullptr, ItemTablePath);
	if (!TestNotNull(TEXT("아이템 표"), ItemTable))
	{
		return false;
	}

	UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	ON_SCOPE_EXIT
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	};

	ASnarehavenCharacter* const Player = World->SpawnActor<ASnarehavenCharacter>();
	if (!TestNotNull(TEXT("플레이어"), Player) || !TestTrue(TEXT("몸을 입혔다"), Player->ApplyPlayerBody()))
	{
		return false;
	}
	UEquipmentComponent* const Equipment = Player->GetEquipment();
	USkeletalMeshComponent* const Body = Player->GetMesh();
	if (!TestNotNull(TEXT("장비 컴포넌트"), Equipment) || !TestTrue(TEXT("서 있기 첫 프레임 자세"), SetIdlePose(Body)))
	{
		return false;
	}
	TestTrue(TEXT("몸에 Socket_Back 소켓이 있다"), Body->DoesSocketExist(BackSocket));
	TestNull(TEXT("장비 줄 배낭 칸은 비어 있다"), Equipment->GetEquippedRow(EEquipSlot::Backpack));

	const double BodyScale = Body->GetComponentScale().Z;
	const FVector Forward = Player->GetActorForwardVector();
	const FName ChestBone = Body->GetSocketBoneName(BackSocket);
	const FVector Chest = Body->GetBoneLocation(ChestBone);
	const double BackSurface = FVector::DotProduct(Chest, Forward) - BackSurfaceCm * BodyScale;

	TMap<EItemGrade, int32> Grades;
	FName EpicRow = NAME_None;
	FString EpicMesh;
	int32 BackpackRows = 0;
	for (const TPair<FName, uint8*>& Pair : ItemTable->GetRowMap())
	{
		const FItemRow& Row = *reinterpret_cast<const FItemRow*>(Pair.Value);
		if (Row.ItemType != EItemType::Cosmetic || Row.EquipSlot != EEquipSlot::Backpack)
		{
			continue;
		}
		++BackpackRows;
		Grades.FindOrAdd(Row.Grade) += 1;
		const FString RowName = Pair.Key.ToString();

		// ① 행·메시
		TestTrue(FString::Printf(TEXT("[%s] 배낭 폴더의 자기 메시: %s"), *RowName, *Row.EquipMeshRef.ToString()),
			Row.EquipMeshRef.ToString().StartsWith(BackpackFolder) && !Row.EquipMeshRef.ToString().Contains(HikingMeshName));
		TestEqual(FString::Printf(TEXT("[%s] 소켓"), *RowName), Row.EquipSocketName, BackSocket);
		TestTrue(FString::Printf(TEXT("[%s] 몸 배율을 따른다"), *RowName), Row.bEquipFollowsBodyScale);
		UStaticMesh* const Mesh = Row.EquipMeshRef.LoadSynchronous();
		if (!TestNotNull(FString::Printf(TEXT("[%s] 메시를 실제로 읽는다"), *RowName), Mesh))
		{
			continue;
		}
		const int32 Triangles = Mesh->GetNumTriangles(0);
		TestTrue(FString::Printf(TEXT("[%s] 삼각형 %d ≤ %d (명세 4장)"), *RowName, Triangles, BackpackTriangleLimit),
			Triangles > 0 && Triangles <= BackpackTriangleLimit);

		// ② 메기 — 빈 칸에 꾸미기만
		if (!TestTrue(FString::Printf(TEXT("[%s] 배낭 꾸미기를 멨다"), *RowName), Equipment->SetCosmetic(EEquipSlot::Backpack, Pair.Key)))
		{
			continue;
		}
		UStaticMeshComponent* const Pack = Equipment->GetEquippedMesh(EEquipSlot::Backpack);
		if (!TestNotNull(FString::Printf(TEXT("[%s] 배낭 칸 메시 컴포넌트"), *RowName), Pack))
		{
			continue;
		}
		Pack->UpdateComponentToWorld();
		TestEqual(FString::Printf(TEXT("[%s] 보이는 메시 = 꾸미기"), *RowName), Pack->GetStaticMesh().Get(), Mesh);
		TestTrue(FString::Printf(TEXT("[%s] 보인다"), *RowName), Pack->IsVisible());
		TestEqual(FString::Printf(TEXT("[%s] 붙은 소켓"), *RowName), Pack->GetAttachSocketName(), BackSocket);
		TestEqual(FString::Printf(TEXT("[%s] 붙은 몸"), *RowName), Pack->GetAttachParent(), static_cast<USceneComponent*>(Body));
		const double PackScale = Pack->GetComponentScale().Z;
		TestTrue(FString::Printf(TEXT("[%s] 월드 배율 %.3f ≈ 몸 배율 %.3f"), *RowName, PackScale, BodyScale),
			FMath::IsNearlyEqual(PackScale, BodyScale, ScaleTolerance));
		TestNull(FString::Printf(TEXT("[%s] 장비 줄은 빈 칸 그대로"), *RowName), Equipment->GetEquippedRow(EEquipSlot::Backpack));

		// ③ 등에 붙는다
		const FVector PackCenter = Pack->CalcBounds(Pack->GetComponentTransform()).Origin;
		const double Back = FVector::DotProduct(PackCenter - Chest, Forward);
		const double Gap = BackSurface - FarthestAlong(Pack, Forward);
		TestTrue(FString::Printf(TEXT("[%s] 가운데가 가슴 뼈 %s 보다 등 쪽 (앞 내적 %.1f < 0)"), *RowName, *ChestBone.ToString(), Back), Back < 0.0);
		TestTrue(FString::Printf(TEXT("[%s] 몸 쪽 면–등 표면 틈 %.2fcm 이 0~%.0f"), *RowName, Gap, BackGapMaxCm),
			Gap >= -FloatSlackCm && Gap <= BackGapMaxCm);
		UE_LOG(LogSnarehaven, Display, TEXT("[CosmeticBackpack] %s = %s · 등급 %d · 삼각형 %d · 소켓 %s(뼈 %s) · 배율 %.3f(몸 %.3f) · 앞 내적 %.1f · 틈 %.2fcm"),
			*RowName, *Mesh->GetName(), static_cast<int32>(Row.Grade), Triangles, *Pack->GetAttachSocketName().ToString(),
			*ChestBone.ToString(), PackScale, BodyScale, Back, Gap);

		if (Row.Grade == EItemGrade::Epic)
		{
			EpicRow = Pair.Key;
			EpicMesh = Mesh->GetName();
		}
		Equipment->SetCosmetic(EEquipSlot::Backpack, NAME_None);
	}

	TestEqual(TEXT("배낭 꾸미기 행 수 (6 + M83-01 의 3 + M84-01 의 3 + M88-01 의 3)"), BackpackRows, 15);
	TestEqual(TEXT("일반 9"), Grades.FindRef(EItemGrade::Common), 9);
	TestEqual(TEXT("희귀 5"), Grades.FindRef(EItemGrade::Rare), 5);
	TestEqual(TEXT("에픽 1"), Grades.FindRef(EItemGrade::Epic), 1);

	// ④ 하이킹 배낭(효과 +8칸) 위에 덮기 — 보이는 것만 바뀐다
	if (!EpicRow.IsNone() && TestTrue(TEXT("하이킹 배낭을 멨다"), Equipment->EquipItemByItemID(HikingItemID)))
	{
		TestTrue(TEXT("하이킹 배낭 위에 에픽 꾸미기를 덮었다"), Equipment->SetCosmetic(EEquipSlot::Backpack, EpicRow));
		const UStaticMeshComponent* const Covered = Equipment->GetEquippedMesh(EEquipSlot::Backpack);
		const FItemRow* const Effect = Equipment->GetEquippedRow(EEquipSlot::Backpack);
		const FString CoveredMesh = Covered && Covered->GetStaticMesh() ? Covered->GetStaticMesh()->GetName() : TEXT("없음");
		TestEqual(TEXT("덮인 채 보이는 메시 = 꾸미기"), CoveredMesh, EpicMesh);
		TestEqual(TEXT("장비 줄은 하이킹 배낭 그대로"), Effect ? Effect->ItemID : NAME_None, HikingItemID);
		TestTrue(TEXT("효과(+칸)는 그대로"), Effect != nullptr && Effect->BonusSlots > 0);
		TestTrue(TEXT("꾸미기를 벗겼다"), Equipment->SetCosmetic(EEquipSlot::Backpack, NAME_None));
		const UStaticMeshComponent* const Bare = Equipment->GetEquippedMesh(EEquipSlot::Backpack);
		const FString BareMesh = Bare && Bare->GetStaticMesh() ? Bare->GetStaticMesh()->GetName() : TEXT("없음");
		TestEqual(TEXT("벗기면 하이킹 메시로 돌아온다"), BareMesh, FString(HikingMeshName));
		UE_LOG(LogSnarehaven, Display, TEXT("[CosmeticBackpack] 덮기: 하이킹 위 %s → 보임 %s · 장비 줄 %s(+%d칸) · 벗김 → %s"),
			*EpicRow.ToString(), *CoveredMesh, Effect ? *Effect->ItemID.ToString() : TEXT("없음"), Effect ? Effect->BonusSlots : 0, *BareMesh);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
