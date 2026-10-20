// 스킨 조각 h (`docs/specs/skin-parts.md` 4장 · 6장 h) 2층 검사 — 꾸미기 상의 6벌이 진짜 표·진짜 메시로 몸통에 입혀진다.
//  ① 표에 상의 꾸미기 행이 6개(일반 3·희귀 2·에픽 1), 메시는 옷 폴더의 자기 스켈레탈 메시 · 스켈레톤 = 몸 것 ·
//     삼각형 ≤ 2,500(명세 4장 옷 스켈레탈 칸당) · 재질 칸 = 같은 폴더 `MI_Outfit_*`(끊긴 옛 재질이면 게임에 격자로 나온다).
//  ② 효과 셔츠(`item.flannel_shirt`)를 입은 채 `SetCosmetic` 으로 덮으면 보이는 메시 = 꾸미기 · 리더 = 몸 ·
//     장비 줄은 셔츠 그대로(효과 그대로) · 꾸미기를 벗기면 셔츠 메시로 돌아온다.
//  ③ 몸통을 덮는다 — 반입 바운드를 몸 glb 미터로 되돌린 높이가 밑 0.15~0.30(허리 아래까지) · 위 0.55~0.65(어깨까지).
//     (하의·신발 메시가 상의 행에 잘못 걸리면 위 끝이 0.33 아래라 ③ 이 깨진다.)
//  ④ (`TopSleevesCoverArms`, 루프#860) 긴소매 행은 몸 팔 꼭짓점(위팔~손목)을 옷이 전부 덮는다 — 팔 뼈 줄에서 꼭짓점으로 쏜
//     반직선이 옷 삼각형을 만나야 하고(안 만나면 맨살), 만난 자리가 꼭짓점보다 안쪽이면 팔이 옷을 뚫은 것이다.
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

namespace SnareCosmeticTopTest
{
	const TCHAR* ItemTablePath = TEXT("/Game/Game/Data/Items/DT_ItemTable.DT_ItemTable");
	const TCHAR* OutfitFolder = TEXT("/Game/Game/Characters/Equipment/Outfits/");
	const FName EffectShirtID(TEXT("item.flannel_shirt"));

	// 검사 전용 값 — 명세 4장 「옷 스켈레탈 2,500」 · 몸통 높이 창(몸 glb 미터, 몸 키 약 1m: 허리 0.30 · 어깨 0.55).
	constexpr int32 TopTriangleLimit = 2500;
	constexpr double BottomMin = 0.15;
	constexpr double BottomMax = 0.30;
	constexpr double TopMin = 0.55;
	constexpr double TopMax = 0.65;

	// 긴소매 상의 행(`tops_make.py` 의 sleeve="long") — 팔 덮임 단정 대상.
	const TCHAR* LongSleeveRows[] = {TEXT("item.meadow_knit"), TEXT("item.meadow_petal_blouse"), TEXT("item.harbor_stripe_tee"),
		TEXT("item.harbor_puffer"), TEXT("item.harbor_sailor"), TEXT("item.meadow_bee_sweater"),
		TEXT("item.harbor_anchor_sweat"), TEXT("item.harbor_wave_sweat"), TEXT("item.meadow_strawberry"), TEXT("item.meadow_toadstool"), TEXT("item.harbor_snow_sweat"), TEXT("item.meadow_pine_sweat"), TEXT("item.meadow_diamond_sweat"), TEXT("item.harbor_kelp_sweat"), TEXT("item.harbor_moon_sweat"), TEXT("item.harbor_rope_sweat"), TEXT("item.harbor_compass_sweat")};
	const TCHAR* BodyMeshPath = TEXT("/Game/Game/Characters/Player/Snare/SK_Player_Body.SK_Player_Body");
	// 검사 전용 값 — 위팔 뼈 시작(0)~손 뼈(1) 사이 어깨 쪽 8%·손목 쪽 14% 는 뺀다(어깨 둥근 곳 · 소매 끝 가장자리).
	constexpr double ArmFrom = 0.08;
	constexpr double ArmTo = 0.86;
	constexpr double PokeToleranceCm = 0.2;

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenCosmeticTopRowsTest, "Snarehaven.Item.Cosmetic.TopRows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenCosmeticTopRowsTest::RunTest(const FString& Parameters)
{
	using namespace SnareCosmeticTopTest;

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

	// 효과 셔츠를 먼저 입는다 — 꾸미기는 그 위에 보이는 것만 덮어야 한다.
	if (!TestTrue(TEXT("item.flannel_shirt 를 입었다"), Equipment->EquipItemByItemID(EffectShirtID)))
	{
		return false;
	}
	const USkeletalMesh* const ShirtMesh = Equipment->GetEquippedOutfit(EEquipSlot::OutfitTop)
		? Equipment->GetEquippedOutfit(EEquipSlot::OutfitTop)->GetSkeletalMeshAsset() : nullptr;
	TestNotNull(TEXT("셔츠 메시가 보인다"), ShirtMesh);

	TMap<EItemGrade, int32> Grades;
	int32 TopRows = 0;
	for (const TPair<FName, uint8*>& Pair : ItemTable->GetRowMap())
	{
		const FItemRow& Row = *reinterpret_cast<const FItemRow*>(Pair.Value);
		if (Row.ItemType != EItemType::Cosmetic || Row.EquipSlot != EEquipSlot::OutfitTop)
		{
			continue;
		}
		++TopRows;
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
		TestTrue(FString::Printf(TEXT("[%s] 삼각형 %d ≤ %d (명세 4장)"), *RowName, Triangles, TopTriangleLimit),
			Triangles > 0 && Triangles <= TopTriangleLimit);

		// ② 덮기 — 보이는 것만 꾸미기, 장비 줄은 셔츠 그대로
		if (!TestTrue(FString::Printf(TEXT("[%s] 상의 꾸미기를 입었다"), *RowName), Equipment->SetCosmetic(EEquipSlot::OutfitTop, Pair.Key)))
		{
			continue;
		}
		const USkeletalMeshComponent* const Layer = Equipment->GetEquippedOutfit(EEquipSlot::OutfitTop);
		if (!TestNotNull(FString::Printf(TEXT("[%s] 상의 컴포넌트"), *RowName), Layer))
		{
			continue;
		}
		TestEqual(FString::Printf(TEXT("[%s] 보이는 메시 = 꾸미기"), *RowName), Layer->GetSkeletalMeshAsset(), Mesh);
		TestEqual(FString::Printf(TEXT("[%s] 리더 포즈 = 몸"), *RowName),
			Cast<USkinnedMeshComponent>(Layer->LeaderPoseComponent.Get()), static_cast<USkinnedMeshComponent*>(Body));
		const FItemRow* const EffectRow = Equipment->GetEquippedRow(EEquipSlot::OutfitTop);
		TestEqual(FString::Printf(TEXT("[%s] 장비 줄 = 셔츠 그대로"), *RowName), EffectRow ? EffectRow->ItemID : NAME_None, EffectShirtID);

		// ③ 몸통 높이
		const FVector2D Range = GlbHeightRange(Layer, Body);
		UE_LOG(LogTemp, Display, TEXT("[CosmeticTop] %s 삼각형 %d · 스켈레톤 몸 같음 %s · 높이 %.3f~%.3f 몸 glb 미터"),
			*RowName, Triangles, Mesh->GetSkeleton() == BodySkeleton ? TEXT("예") : TEXT("아니오"), Range.X, Range.Y);
		TestTrue(FString::Printf(TEXT("[%s] 밑 %.3f 가 %.2f~%.2f"), *RowName, Range.X, BottomMin, BottomMax),
			Range.X >= BottomMin && Range.X <= BottomMax);
		TestTrue(FString::Printf(TEXT("[%s] 위 %.3f 가 %.2f~%.2f"), *RowName, Range.Y, TopMin, TopMax),
			Range.Y >= TopMin && Range.Y <= TopMax);

		// 벗기면 셔츠 메시로 돌아온다
		TestTrue(FString::Printf(TEXT("[%s] 꾸미기를 벗겼다"), *RowName), Equipment->SetCosmetic(EEquipSlot::OutfitTop, NAME_None));
		const USkeletalMeshComponent* const After = Equipment->GetEquippedOutfit(EEquipSlot::OutfitTop);
		TestEqual(FString::Printf(TEXT("[%s] 벗긴 뒤 보이는 메시 = 셔츠"), *RowName),
			After ? After->GetSkeletalMeshAsset() : nullptr, const_cast<USkeletalMesh*>(ShirtMesh));
	}

	UE_LOG(LogTemp, Display, TEXT("[CosmeticTop] 상의 꾸미기 행 %d · 일반 %d · 희귀 %d · 에픽 %d"), TopRows,
		Grades.FindRef(EItemGrade::Common), Grades.FindRef(EItemGrade::Rare), Grades.FindRef(EItemGrade::Epic));
	TestEqual(TEXT("상의 44벌 (명세 4장 6벌 + M83-04 의 3 + M83-05 의 3 + M84-04 의 3 + M84-05 의 3 + M88-04 의 3 + M88-05 의 3 + M90-03 의 3 + M90-04 의 3 + M91-03 의 3 + M91-04 의 3 + M92-01 의 3 + M92-02 의 3 + M92-03 의 2)"), TopRows, 44);
	TestEqual(TEXT("일반 16"), Grades.FindRef(EItemGrade::Common), 16);
	TestEqual(TEXT("희귀 14"), Grades.FindRef(EItemGrade::Rare), 14);
	TestEqual(TEXT("에픽 14"), Grades.FindRef(EItemGrade::Epic), 14);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenCosmeticTopSleevesTest, "Snarehaven.Item.Cosmetic.TopSleevesCoverArms",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenCosmeticTopSleevesTest::RunTest(const FString& Parameters)
{
	using namespace SnareCosmeticTopTest;

	UDataTable* const ItemTable = LoadObject<UDataTable>(nullptr, ItemTablePath);
	USkeletalMesh* const BodyMesh = LoadObject<USkeletalMesh>(nullptr, BodyMeshPath);
	if (!TestNotNull(TEXT("아이템 표"), ItemTable) || !TestNotNull(TEXT("몸 메시"), BodyMesh))
	{
		return false;
	}
	const FSkeletalMeshModel* const BodyModel = BodyMesh->GetImportedModel();
	if (!TestTrue(TEXT("몸 가져온 모델"), BodyModel != nullptr && BodyModel->LODModels.Num() > 0))
	{
		return false;
	}
	const FReferenceSkeleton& RefSkel = BodyMesh->GetRefSkeleton();
	const auto RefPos = [&RefSkel](const TCHAR* BoneName)
	{
		FTransform Pose = FTransform::Identity;
		for (int32 Bone = RefSkel.FindBoneIndex(FName(BoneName)); Bone != INDEX_NONE; Bone = RefSkel.GetParentIndex(Bone))
		{
			Pose = Pose * RefSkel.GetRefBonePose()[Bone];
		}
		return Pose.GetLocation();
	};
	// 몸 팔 꼭짓점(위팔·팔뚝 뼈가 가장 센 점) — 쉼 자세 메시 공간. 팔 줄 = 위팔 뼈 시작 → 손 뼈.
	struct FArmPoint { FVector Position; FVector AxisPoint; };
	TArray<FArmPoint> ArmPoints;
	for (const TCHAR* Side : {TEXT("Left"), TEXT("Right")})
	{
		const FVector Start = RefPos(*FString::Printf(TEXT("%sArm"), Side));
		const FVector End = RefPos(*FString::Printf(TEXT("%sHand"), Side));
		if (!TestTrue(FString::Printf(TEXT("%s 팔 뼈가 있다"), Side), (End - Start).Size() > 1.0))
		{
			return false;
		}
		const FName Upper(*FString::Printf(TEXT("%sArm"), Side)), Fore(*FString::Printf(TEXT("%sForeArm"), Side));
		const FVector Axis = End - Start;
		for (const FSkelMeshSection& Section : BodyModel->LODModels[0].Sections)
		{
			for (const FSoftSkinVertex& V : Section.SoftVertices)
			{
				int32 Best = 0;
				for (int32 I = 1; I < MAX_TOTAL_INFLUENCES; ++I)
				{
					Best = V.InfluenceWeights[I] > V.InfluenceWeights[Best] ? I : Best;
				}
				const FName Dominant = RefSkel.GetBoneName(Section.BoneMap[V.InfluenceBones[Best]]);
				const FVector P(V.Position);
				const double T = FVector::DotProduct(P - Start, Axis) / Axis.SizeSquared();
				if ((Dominant == Upper || Dominant == Fore) && T >= ArmFrom && T <= ArmTo)
				{
					ArmPoints.Add({P, Start + Axis * T});
				}
			}
		}
	}
	if (!TestTrue(FString::Printf(TEXT("몸 팔 꼭짓점 %d 개를 읽었다"), ArmPoints.Num()), ArmPoints.Num() > 100))
	{
		return false;
	}

	int32 Checked = 0;
	for (const TCHAR* RowID : LongSleeveRows)
	{
		const FItemRow* Row = nullptr;
		for (const TPair<FName, uint8*>& Pair : ItemTable->GetRowMap())
		{
			const FItemRow* const Candidate = reinterpret_cast<const FItemRow*>(Pair.Value);
			if (Candidate->ItemID == FName(RowID))
			{
				Row = Candidate;
				break;
			}
		}
		if (!TestNotNull(FString::Printf(TEXT("[%s] 표에 있다"), RowID), Row))
		{
			continue;
		}
		USkeletalMesh* const Mesh = Row->EquipOutfitMeshRef.LoadSynchronous();
		const FSkeletalMeshModel* const Model = Mesh != nullptr ? Mesh->GetImportedModel() : nullptr;
		if (!TestTrue(FString::Printf(TEXT("[%s] 옷 메시를 읽었다"), RowID), Model != nullptr && Model->LODModels.Num() > 0))
		{
			continue;
		}
		const FSkeletalMeshLODModel& Lod = Model->LODModels[0];
		TArray<TTuple<FVector, FVector, FVector>> Tris;
		for (const FSkelMeshSection& Section : Lod.Sections)
		{
			for (uint32 I = 0; I < Section.NumTriangles; ++I)
			{
				const uint32 Base = Section.BaseIndex + I * 3;
				const auto Pos = [&](uint32 K) { return FVector(Section.SoftVertices[Lod.IndexBuffer[Base + K] - Section.BaseVertexIndex].Position); };
				Tris.Emplace(Pos(0), Pos(1), Pos(2));
			}
		}
		int32 Bare = 0, Poking = 0;
		for (const FArmPoint& A : ArmPoints)
		{
			const FVector Dir = (A.Position - A.AxisPoint).GetSafeNormal();
			const double Dist = FVector::Distance(A.Position, A.AxisPoint);
			const FVector End = A.AxisPoint + Dir * (Dist + 200.0);
			double First = UE_BIG_NUMBER;
			for (const TTuple<FVector, FVector, FVector>& T : Tris)
			{
				FVector Hit, Normal;
				if (FMath::SegmentTriangleIntersection(A.AxisPoint, End, T.Get<0>(), T.Get<1>(), T.Get<2>(), Hit, Normal))
				{
					First = FMath::Min(First, FVector::Distance(Hit, A.AxisPoint));
				}
			}
			if (First > UE_BIG_NUMBER * 0.5)
			{
				++Bare;
			}
			else if (First < Dist - PokeToleranceCm)
			{
				++Poking;
			}
		}
		++Checked;
		UE_LOG(LogTemp, Display, TEXT("[CosmeticTop] 소매 %s — 팔 꼭짓점 %d · 맨살 %d · 옷 안으로 뚫음 %d"), RowID, ArmPoints.Num(), Bare, Poking);
		TestEqual(FString::Printf(TEXT("[%s] 옷이 덮지 않은 팔 꼭짓점 (맨살)"), RowID), Bare, 0);
		TestEqual(FString::Printf(TEXT("[%s] 옷 안쪽으로 뚫고 나온 팔 꼭짓점"), RowID), Poking, 0);
	}
	TestEqual(TEXT("긴소매 상의 17벌을 다 재었다"), Checked, 17);
	return true;
}

#endif
