// 스킨 조각 j (`docs/specs/skin-parts.md` 4장 · 6장 j) 2층 검사 — 꾸미기 신발 6벌이 진짜 표·진짜 메시로 발에 신겨진다.
//  ① 표에 신발 꾸미기 행이 6개(일반 3·희귀 2·에픽 1), 메시는 옷 폴더의 자기 스켈레탈 메시 · 스켈레톤 = 몸 것 ·
//     삼각형 ≤ 2,500(명세 4장 옷 스켈레탈 칸당) · 재질 칸 = 같은 폴더 `MI_Outfit_*`(끊긴 옛 재질이면 게임에 격자로 나온다).
//  ② 효과 신발이 없는 빈 칸에 `SetCosmetic` 으로 신으면 보이는 메시 = 꾸미기 · 리더 = 몸 · 장비 줄은 빈 칸 그대로 ·
//     꾸미기를 벗기면 보이는 메시가 없어진다.
//  ③ 발을 덮는다 — 반입 바운드를 몸 glb 미터로 되돌린 높이가 밑 −0.03~0.02(바닥) · 위 0.07~0.25(발등~종아리 아래).
//     (하의 메시가 신발 행에 잘못 걸리면 위 끝이 0.30 을 넘어 ③ 이 깨진다.)
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.Item.Cosmetic; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Components/SkeletalMeshComponent.h"
#include "Data/ItemRow.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "Misc/Paths.h"
#include "Equipment/EquipmentComponent.h"
#include "Rendering/SkeletalMeshModel.h"
#include "SnarehavenCharacter.h"

namespace SnareCosmeticShoesTest
{
	const TCHAR* ItemTablePath = TEXT("/Game/Game/Data/Items/DT_ItemTable.DT_ItemTable");
	const TCHAR* OutfitFolder = TEXT("/Game/Game/Characters/Equipment/Outfits/");

	// 검사 전용 값 — 명세 4장 「옷 스켈레탈 2,500」 · 발 높이 창(몸 glb 미터, 몸 키 약 1m: 바닥 0 · 발목 0.11 · 장화 목 0.22).
	constexpr int32 ShoesTriangleLimit = 2500;
	constexpr double BottomMin = -0.03;
	constexpr double BottomMax = 0.02;
	constexpr double TopMin = 0.07;
	constexpr double TopMax = 0.25;

	int32 CountTriangles(USkeletalMesh* Mesh)
	{
		const FSkeletalMeshModel* const Model = Mesh->GetImportedModel();
		if (Model == nullptr || Model->LODModels.Num() == 0)
		{
			return 0;
		}
		int32 Triangles = 0;
		for (const FSkelMeshSection& Section : Model->LODModels[0].Sections)
		{
			Triangles += Section.NumTriangles;
		}
		return Triangles;
	}

	/** 옷 반입 바운드(쉼 자세)를 옷 컴포넌트 월드로 옮겨 z 를 몸 glb 미터로 — `SnarehavenOutfitMeshTest` 와 같은 잣대. */
	FVector2D GlbHeightRange(const USkeletalMeshComponent* Layer, const USkeletalMeshComponent* Body)
	{
		const FBox Box = Layer->GetSkeletalMeshAsset()->GetImportedBounds().GetBox().TransformBy(Layer->GetComponentTransform());
		const double FeetZ = Body->GetComponentLocation().Z;
		const double GlbMeterCm = FMath::Max(Body->GetComponentScale().Z * 100.0, UE_KINDA_SMALL_NUMBER);
		return FVector2D((Box.Min.Z - FeetZ) / GlbMeterCm, (Box.Max.Z - FeetZ) / GlbMeterCm);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenCosmeticShoesRowsTest, "Snarehaven.Item.Cosmetic.ShoesRows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenCosmeticShoesRowsTest::RunTest(const FString& Parameters)
{
	using namespace SnareCosmeticShoesTest;

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
	const FObjectProperty* const TableProp = FindFProperty<FObjectProperty>(UEquipmentComponent::StaticClass(), TEXT("ItemTable"));
	if (!TestNotNull(TEXT("장비 컴포넌트"), Equipment) || !TestNotNull(TEXT("장비 표 칸"), TableProp)
		|| !TestNotNull(TEXT("몸 스켈레톤"), Body->GetSkeletalMeshAsset() ? Body->GetSkeletalMeshAsset()->GetSkeleton() : nullptr))
	{
		return false;
	}
	TableProp->SetObjectPropertyValue_InContainer(Equipment, ItemTable);
	const USkeleton* const BodySkeleton = Body->GetSkeletalMeshAsset()->GetSkeleton();

	// 효과 신발은 아직 없다 — 빈 칸에 꾸미기만 신는다.
	TestNull(TEXT("장비 줄 신발 칸은 비어 있다"), Equipment->GetEquippedRow(EEquipSlot::Shoes));

	TMap<EItemGrade, int32> Grades;
	int32 ShoesRows = 0;
	for (const TPair<FName, uint8*>& Pair : ItemTable->GetRowMap())
	{
		const FItemRow& Row = *reinterpret_cast<const FItemRow*>(Pair.Value);
		if (Row.ItemType != EItemType::Cosmetic || Row.EquipSlot != EEquipSlot::Shoes)
		{
			continue;
		}
		++ShoesRows;
		Grades.FindOrAdd(Row.Grade) += 1;
		const FString RowName = Pair.Key.ToString();

		// ① 행·메시
		TestTrue(FString::Printf(TEXT("[%s] 옷 폴더의 자기 메시: %s"), *RowName, *Row.EquipOutfitMeshRef.ToString()),
			Row.EquipOutfitMeshRef.ToString().StartsWith(OutfitFolder));
		USkeletalMesh* const Mesh = Row.EquipOutfitMeshRef.LoadSynchronous();
		if (!TestNotNull(FString::Printf(TEXT("[%s] 메시를 실제로 읽는다"), *RowName), Mesh))
		{
			continue;
		}
		TestEqual(FString::Printf(TEXT("[%s] 스켈레톤 = 몸 스켈레톤"), *RowName), static_cast<const USkeleton*>(Mesh->GetSkeleton()), BodySkeleton);
		// 재질 칸이 같은 폴더의 인스턴스를 가리킨다 — 반입 뒤 이름만 바뀐 옛 재질을 가리키면 디스크에서 못 읽어
		// 게임에 기본 격자 재질로 나온다(루프#460 3층에서 찾음).
		const TArray<FSkeletalMaterial>& Materials = Mesh->GetMaterials();
		const FString MeshFolder = Row.EquipOutfitMeshRef.ToSoftObjectPath().GetLongPackageName();
		const FString ExpectedMaterial = FString::Printf(TEXT("%s/MI_Outfit_"), *FPaths::GetPath(MeshFolder));
		TestTrue(FString::Printf(TEXT("[%s] 재질 칸 %d 개가 전부 %s*"), *RowName, Materials.Num(), *ExpectedMaterial),
			Materials.Num() > 0 && Materials.FindByPredicate([&ExpectedMaterial](const FSkeletalMaterial& Slot)
			{
				return Slot.MaterialInterface == nullptr || !Slot.MaterialInterface->GetPathName().StartsWith(ExpectedMaterial);
			}) == nullptr);
		const int32 Triangles = CountTriangles(Mesh);
		TestTrue(FString::Printf(TEXT("[%s] 삼각형 %d ≤ %d (명세 4장)"), *RowName, Triangles, ShoesTriangleLimit),
			Triangles > 0 && Triangles <= ShoesTriangleLimit);

		// ② 신기 — 보이는 것만 꾸미기, 장비 줄은 빈 칸 그대로
		if (!TestTrue(FString::Printf(TEXT("[%s] 신발 꾸미기를 신었다"), *RowName), Equipment->SetCosmetic(EEquipSlot::Shoes, Pair.Key)))
		{
			continue;
		}
		const USkeletalMeshComponent* const Layer = Equipment->GetEquippedOutfit(EEquipSlot::Shoes);
		if (!TestNotNull(FString::Printf(TEXT("[%s] 신발 컴포넌트"), *RowName), Layer))
		{
			continue;
		}
		TestEqual(FString::Printf(TEXT("[%s] 보이는 메시 = 꾸미기"), *RowName), Layer->GetSkeletalMeshAsset(), Mesh);
		TestEqual(FString::Printf(TEXT("[%s] 리더 포즈 = 몸"), *RowName),
			Cast<USkinnedMeshComponent>(Layer->LeaderPoseComponent.Get()), static_cast<USkinnedMeshComponent*>(Body));
		TestNull(FString::Printf(TEXT("[%s] 장비 줄 = 빈 칸 그대로"), *RowName), Equipment->GetEquippedRow(EEquipSlot::Shoes));

		// ③ 발 높이
		const FVector2D Range = GlbHeightRange(Layer, Body);
		UE_LOG(LogTemp, Display, TEXT("[CosmeticShoes] %s 삼각형 %d · 스켈레톤 몸 같음 %s · 높이 %.3f~%.3f 몸 glb 미터"),
			*RowName, Triangles, Mesh->GetSkeleton() == BodySkeleton ? TEXT("예") : TEXT("아니오"), Range.X, Range.Y);
		TestTrue(FString::Printf(TEXT("[%s] 밑 %.3f 가 %.2f~%.2f"), *RowName, Range.X, BottomMin, BottomMax),
			Range.X >= BottomMin && Range.X <= BottomMax);
		TestTrue(FString::Printf(TEXT("[%s] 위 %.3f 가 %.2f~%.2f"), *RowName, Range.Y, TopMin, TopMax),
			Range.Y >= TopMin && Range.Y <= TopMax);

		// 벗기면 보이는 신발이 없어진다 (컴포넌트는 남기고 메시만 비운다 — `RefreshEquippedVisuals`)
		TestTrue(FString::Printf(TEXT("[%s] 꾸미기를 벗겼다"), *RowName), Equipment->SetCosmetic(EEquipSlot::Shoes, NAME_None));
		const USkeletalMeshComponent* const After = Equipment->GetEquippedOutfit(EEquipSlot::Shoes);
		TestNull(FString::Printf(TEXT("[%s] 벗긴 뒤 보이는 메시 없음"), *RowName), After ? After->GetSkeletalMeshAsset() : nullptr);
	}

	UE_LOG(LogTemp, Display, TEXT("[CosmeticShoes] 신발 꾸미기 행 %d · 일반 %d · 희귀 %d · 에픽 %d"), ShoesRows,
		Grades.FindRef(EItemGrade::Common), Grades.FindRef(EItemGrade::Rare), Grades.FindRef(EItemGrade::Epic));
	TestEqual(TEXT("신발 10벌 (명세 4장 6벌 + M87-03 의 4)"), ShoesRows, 10);
	TestEqual(TEXT("일반 5"), Grades.FindRef(EItemGrade::Common), 5);
	TestEqual(TEXT("희귀 3"), Grades.FindRef(EItemGrade::Rare), 3);
	TestEqual(TEXT("에픽 2"), Grades.FindRef(EItemGrade::Epic), 2);
	return true;
}

#endif
