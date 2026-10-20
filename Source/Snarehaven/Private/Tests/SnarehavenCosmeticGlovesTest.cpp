// 스킨 조각 k (`docs/specs/skin-parts.md` 4장 · 6장 k) 2층 검사 — 꾸미기 장갑 6벌이 진짜 표·진짜 메시로 손에 끼워진다.
//  ① 표에 장갑 꾸미기 행이 6개(일반 3·희귀 2·에픽 1), 메시는 옷 폴더의 자기 스켈레탈 메시 · 스켈레톤 = 몸 것 ·
//     삼각형 ≤ 2,500(명세 4장 옷 스켈레탈 칸당) · 재질 칸 = 같은 폴더 `MI_Outfit_*`(끊긴 옛 재질이면 게임에 격자로 나온다).
//  ② 효과 장갑이 없는 빈 칸에 `SetCosmetic` 으로 끼면 보이는 메시 = 꾸미기 · 리더 = 몸 · 장비 줄은 빈 칸 그대로 ·
//     꾸미기를 빼면 보이는 메시가 없어진다.
//  ③ 손을 덮는다 — 반입 바운드(쉼 T 자세)를 몸 glb 미터로 되돌린 높이가 밑 0.40~0.47 · 위 0.56~0.65(팔 높이, 손 뼈 0.525)
//     · 가로 반폭 0.42~0.60(손끝 0.47, 꽃게 집게 0.57). 소매가 머리 상자 옆(|x| 0.275, 높이 0.9)을 물면 위 끝이 깨진다.
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

namespace SnareCosmeticGlovesTest
{
	const TCHAR* ItemTablePath = TEXT("/Game/Game/Data/Items/DT_ItemTable.DT_ItemTable");
	const TCHAR* OutfitFolder = TEXT("/Game/Game/Characters/Equipment/Outfits/");

	// 검사 전용 값 — 명세 4장 「옷 스켈레탈 2,500」 · 손 자리 창(몸 glb 미터, 쉼 T 자세: 손 높이 0.46~0.58 · 손끝 |x| 0.47).
	constexpr int32 GlovesTriangleLimit = 2500;
	constexpr double BottomMin = 0.40;
	constexpr double BottomMax = 0.47;
	constexpr double TopMin = 0.56;
	constexpr double TopMax = 0.65;
	constexpr double HalfWidthMin = 0.42;
	constexpr double HalfWidthMax = 0.60;

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

	/** 옷 반입 바운드(쉼 자세)를 옷 컴포넌트 월드로 옮겨 몸 glb 미터로 — (밑 z, 위 z, 가로 반폭). `SnarehavenOutfitMeshTest` 와 같은 잣대. */
	FVector GlbHandRange(const USkeletalMeshComponent* Layer, const USkeletalMeshComponent* Body)
	{
		const FBox Box = Layer->GetSkeletalMeshAsset()->GetImportedBounds().GetBox().TransformBy(Layer->GetComponentTransform());
		const FVector Feet = Body->GetComponentLocation();
		const double GlbMeterCm = FMath::Max(Body->GetComponentScale().Z * 100.0, UE_KINDA_SMALL_NUMBER);
		const double HalfWidth = FMath::Max(FMath::Max(FMath::Abs(Box.Min.X - Feet.X), FMath::Abs(Box.Max.X - Feet.X)),
			FMath::Max(FMath::Abs(Box.Min.Y - Feet.Y), FMath::Abs(Box.Max.Y - Feet.Y)));
		return FVector((Box.Min.Z - Feet.Z) / GlbMeterCm, (Box.Max.Z - Feet.Z) / GlbMeterCm, HalfWidth / GlbMeterCm);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenCosmeticGlovesRowsTest, "Snarehaven.Item.Cosmetic.GlovesRows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenCosmeticGlovesRowsTest::RunTest(const FString& Parameters)
{
	using namespace SnareCosmeticGlovesTest;

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

	// 효과 장갑은 아직 없다 — 빈 칸에 꾸미기만 낀다.
	TestNull(TEXT("장비 줄 장갑 칸은 비어 있다"), Equipment->GetEquippedRow(EEquipSlot::Gloves));

	TMap<EItemGrade, int32> Grades;
	int32 GlovesRows = 0;
	for (const TPair<FName, uint8*>& Pair : ItemTable->GetRowMap())
	{
		const FItemRow& Row = *reinterpret_cast<const FItemRow*>(Pair.Value);
		if (Row.ItemType != EItemType::Cosmetic || Row.EquipSlot != EEquipSlot::Gloves)
		{
			continue;
		}
		++GlovesRows;
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
		TestTrue(FString::Printf(TEXT("[%s] 삼각형 %d ≤ %d (명세 4장)"), *RowName, Triangles, GlovesTriangleLimit),
			Triangles > 0 && Triangles <= GlovesTriangleLimit);

		// ② 끼기 — 보이는 것만 꾸미기, 장비 줄은 빈 칸 그대로
		if (!TestTrue(FString::Printf(TEXT("[%s] 장갑 꾸미기를 끼었다"), *RowName), Equipment->SetCosmetic(EEquipSlot::Gloves, Pair.Key)))
		{
			continue;
		}
		const USkeletalMeshComponent* const Layer = Equipment->GetEquippedOutfit(EEquipSlot::Gloves);
		if (!TestNotNull(FString::Printf(TEXT("[%s] 장갑 컴포넌트"), *RowName), Layer))
		{
			continue;
		}
		TestEqual(FString::Printf(TEXT("[%s] 보이는 메시 = 꾸미기"), *RowName), Layer->GetSkeletalMeshAsset(), Mesh);
		TestEqual(FString::Printf(TEXT("[%s] 리더 포즈 = 몸"), *RowName),
			Cast<USkinnedMeshComponent>(Layer->LeaderPoseComponent.Get()), static_cast<USkinnedMeshComponent*>(Body));
		TestNull(FString::Printf(TEXT("[%s] 장비 줄 = 빈 칸 그대로"), *RowName), Equipment->GetEquippedRow(EEquipSlot::Gloves));

		// ③ 손 자리
		const FVector Range = GlbHandRange(Layer, Body);
		UE_LOG(LogTemp, Display, TEXT("[CosmeticGloves] %s 삼각형 %d · 스켈레톤 몸 같음 %s · 높이 %.3f~%.3f · 반폭 %.3f 몸 glb 미터"),
			*RowName, Triangles, Mesh->GetSkeleton() == BodySkeleton ? TEXT("예") : TEXT("아니오"), Range.X, Range.Y, Range.Z);
		TestTrue(FString::Printf(TEXT("[%s] 밑 %.3f 가 %.2f~%.2f"), *RowName, Range.X, BottomMin, BottomMax),
			Range.X >= BottomMin && Range.X <= BottomMax);
		TestTrue(FString::Printf(TEXT("[%s] 위 %.3f 가 %.2f~%.2f"), *RowName, Range.Y, TopMin, TopMax),
			Range.Y >= TopMin && Range.Y <= TopMax);
		TestTrue(FString::Printf(TEXT("[%s] 가로 반폭 %.3f 가 %.2f~%.2f"), *RowName, Range.Z, HalfWidthMin, HalfWidthMax),
			Range.Z >= HalfWidthMin && Range.Z <= HalfWidthMax);

		// 빼면 보이는 장갑이 없어진다 (컴포넌트는 남기고 메시만 비운다 — `RefreshEquippedVisuals`)
		TestTrue(FString::Printf(TEXT("[%s] 꾸미기를 뺐다"), *RowName), Equipment->SetCosmetic(EEquipSlot::Gloves, NAME_None));
		const USkeletalMeshComponent* const After = Equipment->GetEquippedOutfit(EEquipSlot::Gloves);
		TestNull(FString::Printf(TEXT("[%s] 뺀 뒤 보이는 메시 없음"), *RowName), After ? After->GetSkeletalMeshAsset() : nullptr);
	}

	UE_LOG(LogTemp, Display, TEXT("[CosmeticGloves] 장갑 꾸미기 행 %d · 일반 %d · 희귀 %d · 에픽 %d"), GlovesRows,
		Grades.FindRef(EItemGrade::Common), Grades.FindRef(EItemGrade::Rare), Grades.FindRef(EItemGrade::Epic));
	TestEqual(TEXT("장갑 9벌 (명세 4장 6벌 + M87-04 의 3)"), GlovesRows, 9);
	TestEqual(TEXT("일반 5"), Grades.FindRef(EItemGrade::Common), 5);
	TestEqual(TEXT("희귀 3"), Grades.FindRef(EItemGrade::Rare), 3);
	TestEqual(TEXT("에픽 1"), Grades.FindRef(EItemGrade::Epic), 1);
	return true;
}

#endif
