// M13-42 2층 검사 — 구명조끼 옷 메시가 몸 스켈레톤으로 들어왔고, 입히면 몸통에 겹쳐 몸을 따라가는가.
// DESIGN 13절 「구명조끼 옷 메시」: 삼각형 ≤3000 · 조끼 높이 z 0.285~0.565(몸 glb 미터, 몸 키 약 1m).
// ① `SK_Outfit_LifeJacket_01` 스켈레톤 = `SK_Player_Body` 스켈레톤(결정 006 — 달라야 할 이유가 없고, 다르면 리더 포즈가 어긋난다)
// ② LOD0 삼각형 ≤ 3000
// ③ `UEquipmentComponent::EquipItemByItemID("item.life_jacket")` → `OutfitTop` 스켈레탈 컴포넌트, 메시 = 조끼, 리더 = 몸
// ④ 월드 바운드 z 를 몸 glb 미터로 되돌린 값(몸 발 = 컴포넌트 원점, 1 glb 미터 = 몸 배율 × 100cm)이
//    DESIGN 표 0.285~0.565 ± 0.03 안. ± 0.03 = `LifeJacket_fit.py` 가 표 상자에 맞춘 뒤 Shrinkwrap 으로 몸통 겉
//    +1cm 밖으로 민 몫(원본 glb 정점 실측 0.263~0.591). 몸 스켈레탈 바운드는 발 아래·머리 위로 부풀어 있어(몸 glb
//    키 1.00m 인데 바운드 약 1.07m) 기준으로 쓰지 않는다.
// M9-05 2층 — 셔츠·바지(DESIGN 13절 「셔츠·바지」): 두 메시 스켈레톤 = 몸 · 삼각형 ≤3000 · 같이 입으면 `OutfitTop`·
// `OutfitBottom` 두 컴포넌트가 몸을 리더로 따른다 · 셔츠를 입으면 같은 칸의 조끼가 벗겨진다 · 높이 셔츠 z 0.245~0.585 ·
// 바지 z 0.11~0.33 — 조끼와 같은 ± 0.03(몸 glb 미터)을 **양 끝 각각**에 댄다(옷이 줄거나 다른 칸에 가도 잡히게).
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.Outfit; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "SnarehavenCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/ItemRow.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Equipment/EquipmentComponent.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Snarehaven.h"

namespace SnarehavenOutfitMeshTest
{
	const TCHAR* JacketPath =
		TEXT("/Game/Game/Characters/Equipment/Outfits/LifeJacket/SK_Outfit_LifeJacket_01.SK_Outfit_LifeJacket_01");
	const TCHAR* BodyPath = TEXT("/Game/Game/Characters/Player/Snare/SK_Player_Body.SK_Player_Body");
	// DESIGN 13절 「구명조끼 옷 메시」 표 값 — 설계서 숫자를 그대로 대조한다.
	constexpr int32 TriangleLimit = 3000;
	constexpr double JacketBottomRatio = 0.285;
	constexpr double JacketTopRatio = 0.565;
	constexpr double RatioTolerance = 0.03;
	const TCHAR* ShirtPath =
		TEXT("/Game/Game/Characters/Equipment/Outfits/FlannelShirt/SK_Outfit_FlannelShirt_01.SK_Outfit_FlannelShirt_01");
	const TCHAR* PantsPath =
		TEXT("/Game/Game/Characters/Equipment/Outfits/WorkPants/SK_Outfit_WorkPants_01.SK_Outfit_WorkPants_01");
	// DESIGN 13절 「셔츠·바지」 표 「셔츠 범위」·「바지 범위」 높이 z.
	constexpr double ShirtBottomRatio = 0.245;
	constexpr double ShirtTopRatio = 0.585;
	constexpr double PantsBottomRatio = 0.11;
	constexpr double PantsTopRatio = 0.33;

	UWorld* MakeWorld()
	{
		UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		return World;
	}

	void DestroyWorld(UWorld* World)
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	}

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

	/**
	 * 옷 메시의 반입 바운드(쉼 자세)를 옷 컴포넌트 월드 변환으로 옮겨 z 를 몸 glb 미터로 되돌린다 —
	 * 발 = 몸 컴포넌트 원점, 1 glb 미터 = 몸 배율 × 100cm. CalcBounds 는 쓰지 않는다: 리더를 따르는 옷
	 * 컴포넌트에서는 셔츠가 몸 전체 높이(-0.06~1.02)로 나와 옷 메시 자체의 높이를 재지 못했다(#0236).
	 */
	FVector2D GlbHeightRange(const USkeletalMeshComponent* Layer, const USkeletalMeshComponent* Body)
	{
		const FBox Box = Layer->GetSkeletalMeshAsset()->GetImportedBounds().GetBox().TransformBy(Layer->GetComponentTransform());
		const double FeetZ = Body->GetComponentLocation().Z;
		const double GlbMeterCm = FMath::Max(Body->GetComponentScale().Z * 100.0, UE_KINDA_SMALL_NUMBER);
		return FVector2D((Box.Min.Z - FeetZ) / GlbMeterCm, (Box.Max.Z - FeetZ) / GlbMeterCm);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenOutfitLifeJacketTest, "Snarehaven.Outfit.LifeJacket.WornOverTorso",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenOutfitLifeJacketTest::RunTest(const FString& Parameters)
{
	using namespace SnarehavenOutfitMeshTest;

	USkeletalMesh* const Jacket = LoadObject<USkeletalMesh>(nullptr, JacketPath);
	USkeletalMesh* const BodyMesh = LoadObject<USkeletalMesh>(nullptr, BodyPath);
	if (!TestNotNull(TEXT("조끼 메시가 있다"), Jacket) || !TestNotNull(TEXT("몸 메시가 있다"), BodyMesh))
	{
		return false;
	}

	// ① 새 스켈레톤이 아니라 몸 것이어야 리더 포즈가 뼈 이름·순서 그대로 맞는다.
	TestNotNull(TEXT("몸 스켈레톤이 있다"), BodyMesh->GetSkeleton());
	TestEqual(TEXT("조끼 스켈레톤 = 몸 스켈레톤"), Jacket->GetSkeleton(), BodyMesh->GetSkeleton());

	// ② DESIGN 표 삼각형 상한.
	const int32 Triangles = CountTriangles(Jacket);
	TestTrue(FString::Printf(TEXT("삼각형 %d 이 1 이상 %d 이하"), Triangles, TriangleLimit),
		Triangles > 0 && Triangles <= TriangleLimit);

	// ③ 행으로 입힌다 — 표의 `EquipOutfitMeshRef` 가 채워졌는지까지 같은 길로 본다.
	UWorld* const World = MakeWorld();
	ASnarehavenCharacter* const Player = World->SpawnActor<ASnarehavenCharacter>();
	UEquipmentComponent* const Equipment = Player ? Player->GetEquipment() : nullptr;
	if (!TestNotNull(TEXT("장비 컴포넌트가 있다"), Equipment)
		|| !TestTrue(TEXT("사람 메시를 올렸다"), Player->ApplyPlayerBody()))
	{
		DestroyWorld(World);
		return false;
	}
	TestTrue(TEXT("item.life_jacket 을 입었다"), Equipment->EquipItemByItemID(FName(TEXT("item.life_jacket"))));

	USkeletalMeshComponent* const Body = Player->GetMesh();
	USkeletalMeshComponent* const Top = Equipment->GetEquippedOutfit(EEquipSlot::OutfitTop);
	if (!TestNotNull(TEXT("OutfitTop 스켈레탈 컴포넌트가 생겼다"), Top))
	{
		DestroyWorld(World);
		return false;
	}
	TestEqual(TEXT("그 컴포넌트 메시 = 조끼"), Top->GetSkeletalMeshAsset(), Jacket);
	TestEqual(TEXT("리더 포즈 = 몸 메시"),
		Cast<USkinnedMeshComponent>(Top->LeaderPoseComponent.Get()), Cast<USkinnedMeshComponent>(Body));

	// ④ 몸통 범위 — 조끼 월드 바운드 z 를 몸 glb 미터로 되돌려 DESIGN 표와 그대로 견준다.
	const FBox TopBox = Top->CalcBounds(Top->GetComponentTransform()).GetBox();
	const double FeetZ = Body->GetComponentLocation().Z;
	const double GlbMeterCm = FMath::Max(Body->GetComponentScale().Z * 100.0, UE_KINDA_SMALL_NUMBER);
	const double BottomRatio = (TopBox.Min.Z - FeetZ) / GlbMeterCm;
	const double TopRatio = (TopBox.Max.Z - FeetZ) / GlbMeterCm;
	TestTrue(FString::Printf(TEXT("조끼 밑 %.3f ≥ %.3f"), BottomRatio, JacketBottomRatio - RatioTolerance),
		BottomRatio >= JacketBottomRatio - RatioTolerance);
	TestTrue(FString::Printf(TEXT("조끼 위 %.3f ≤ %.3f"), TopRatio, JacketTopRatio + RatioTolerance),
		TopRatio <= JacketTopRatio + RatioTolerance);

	UE_LOG(LogSnarehaven, Display,
		TEXT("[Outfit] 조끼 SK_Outfit_LifeJacket_01 스켈레톤 %s(몸 같음 %s) · 삼각형 %d · 리더 %s · 몸 1m = %.1f cm · 조끼 z %.1f~%.1f (몸 glb 미터 %.3f~%.3f, 표 %.3f~%.3f ± %.2f) · 조끼 폭 %.1f x %.1f cm"),
		*GetNameSafe(Jacket->GetSkeleton()), Jacket->GetSkeleton() == BodyMesh->GetSkeleton() ? TEXT("예") : TEXT("아니오"),
		Triangles, *GetNameSafe(Top->LeaderPoseComponent.Get()), GlbMeterCm, TopBox.Min.Z, TopBox.Max.Z,
		BottomRatio, TopRatio, JacketBottomRatio, JacketTopRatio, RatioTolerance, TopBox.GetSize().X, TopBox.GetSize().Y);

	DestroyWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenOutfitShirtPantsTest, "Snarehaven.Outfit.ShirtPants.WornTogether",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenOutfitShirtPantsTest::RunTest(const FString& Parameters)
{
	using namespace SnarehavenOutfitMeshTest;

	USkeletalMesh* const Shirt = LoadObject<USkeletalMesh>(nullptr, ShirtPath);
	USkeletalMesh* const Pants = LoadObject<USkeletalMesh>(nullptr, PantsPath);
	USkeletalMesh* const Jacket = LoadObject<USkeletalMesh>(nullptr, JacketPath);
	USkeletalMesh* const BodyMesh = LoadObject<USkeletalMesh>(nullptr, BodyPath);
	if (!TestNotNull(TEXT("셔츠 메시가 있다"), Shirt) || !TestNotNull(TEXT("바지 메시가 있다"), Pants)
		|| !TestNotNull(TEXT("조끼 메시가 있다"), Jacket) || !TestNotNull(TEXT("몸 메시가 있다"), BodyMesh))
	{
		return false;
	}

	// ①② 몸 스켈레톤 · 삼각형 상한 (조끼와 같은 이유).
	const int32 ShirtTriangles = CountTriangles(Shirt);
	const int32 PantsTriangles = CountTriangles(Pants);
	TestEqual(TEXT("셔츠 스켈레톤 = 몸 스켈레톤"), Shirt->GetSkeleton(), BodyMesh->GetSkeleton());
	TestEqual(TEXT("바지 스켈레톤 = 몸 스켈레톤"), Pants->GetSkeleton(), BodyMesh->GetSkeleton());
	TestTrue(FString::Printf(TEXT("셔츠 삼각형 %d 이 1 이상 %d 이하"), ShirtTriangles, TriangleLimit),
		ShirtTriangles > 0 && ShirtTriangles <= TriangleLimit);
	TestTrue(FString::Printf(TEXT("바지 삼각형 %d 이 1 이상 %d 이하"), PantsTriangles, TriangleLimit),
		PantsTriangles > 0 && PantsTriangles <= TriangleLimit);

	UWorld* const World = MakeWorld();
	ASnarehavenCharacter* const Player = World->SpawnActor<ASnarehavenCharacter>();
	UEquipmentComponent* const Equipment = Player ? Player->GetEquipment() : nullptr;
	if (!TestNotNull(TEXT("장비 컴포넌트가 있다"), Equipment)
		|| !TestTrue(TEXT("사람 메시를 올렸다"), Player->ApplyPlayerBody()))
	{
		DestroyWorld(World);
		return false;
	}

	// ③ 조끼를 먼저 입고 셔츠·바지를 입는다 — 셔츠는 같은 `OutfitTop` 이라 조끼를 밀어낸다(DESIGN 「조끼와 겹침」).
	TestTrue(TEXT("item.life_jacket 을 입었다"), Equipment->EquipItemByItemID(FName(TEXT("item.life_jacket"))));
	TestTrue(TEXT("item.flannel_shirt 를 입었다"), Equipment->EquipItemByItemID(FName(TEXT("item.flannel_shirt"))));
	TestTrue(TEXT("item.work_pants 를 입었다"), Equipment->EquipItemByItemID(FName(TEXT("item.work_pants"))));

	USkeletalMeshComponent* const Body = Player->GetMesh();
	USkeletalMeshComponent* const Top = Equipment->GetEquippedOutfit(EEquipSlot::OutfitTop);
	USkeletalMeshComponent* const Bottom = Equipment->GetEquippedOutfit(EEquipSlot::OutfitBottom);
	if (!TestNotNull(TEXT("OutfitTop 스켈레탈 컴포넌트가 있다"), Top)
		|| !TestNotNull(TEXT("OutfitBottom 스켈레탈 컴포넌트가 있다"), Bottom))
	{
		DestroyWorld(World);
		return false;
	}
	const FItemRow* const TopRow = Equipment->GetEquippedRow(EEquipSlot::OutfitTop);
	TestEqual(TEXT("상의 칸 행 = 셔츠(조끼가 벗겨졌다)"),
		TopRow ? TopRow->ItemID : NAME_None, FName(TEXT("item.flannel_shirt")));
	TestEqual(TEXT("상의 컴포넌트 메시 = 셔츠"), Top->GetSkeletalMeshAsset(), Shirt);
	TestNotEqual(TEXT("상의 컴포넌트 메시 ≠ 조끼"), Top->GetSkeletalMeshAsset(), Jacket);
	TestEqual(TEXT("하의 컴포넌트 메시 = 바지"), Bottom->GetSkeletalMeshAsset(), Pants);
	TestNotEqual(TEXT("상의·하의는 다른 컴포넌트"), Top, Bottom);
	TestEqual(TEXT("셔츠 리더 포즈 = 몸 메시"),
		Cast<USkinnedMeshComponent>(Top->LeaderPoseComponent.Get()), Cast<USkinnedMeshComponent>(Body));
	TestEqual(TEXT("바지 리더 포즈 = 몸 메시"),
		Cast<USkinnedMeshComponent>(Bottom->LeaderPoseComponent.Get()), Cast<USkinnedMeshComponent>(Body));

	// ④ 높이 — 양 끝이 각각 표 값 ± 0.03 안.
	const FVector2D ShirtRange = GlbHeightRange(Top, Body);
	const FVector2D PantsRange = GlbHeightRange(Bottom, Body);
	TestTrue(FString::Printf(TEXT("셔츠 밑 %.3f = %.3f ± %.2f"), ShirtRange.X, ShirtBottomRatio, RatioTolerance),
		FMath::Abs(ShirtRange.X - ShirtBottomRatio) <= RatioTolerance);
	TestTrue(FString::Printf(TEXT("셔츠 위 %.3f = %.3f ± %.2f"), ShirtRange.Y, ShirtTopRatio, RatioTolerance),
		FMath::Abs(ShirtRange.Y - ShirtTopRatio) <= RatioTolerance);
	TestTrue(FString::Printf(TEXT("바지 밑 %.3f = %.3f ± %.2f"), PantsRange.X, PantsBottomRatio, RatioTolerance),
		FMath::Abs(PantsRange.X - PantsBottomRatio) <= RatioTolerance);
	TestTrue(FString::Printf(TEXT("바지 위 %.3f = %.3f ± %.2f"), PantsRange.Y, PantsTopRatio, RatioTolerance),
		FMath::Abs(PantsRange.Y - PantsTopRatio) <= RatioTolerance);

	UE_LOG(LogSnarehaven, Display,
		TEXT("[Outfit] 셔츠 %s 삼각형 %d · 바지 %s 삼각형 %d · 스켈레톤 몸 같음 %s/%s · 상의 행 %s · 리더 %s/%s · 셔츠 z %.3f~%.3f (표 %.3f~%.3f) · 바지 z %.3f~%.3f (표 %.3f~%.3f) ± %.2f 몸 glb 미터"),
		*GetNameSafe(Shirt), ShirtTriangles, *GetNameSafe(Pants), PantsTriangles,
		Shirt->GetSkeleton() == BodyMesh->GetSkeleton() ? TEXT("예") : TEXT("아니오"),
		Pants->GetSkeleton() == BodyMesh->GetSkeleton() ? TEXT("예") : TEXT("아니오"),
		TopRow ? *TopRow->ItemID.ToString() : TEXT("(없음)"),
		*GetNameSafe(Top->LeaderPoseComponent.Get()), *GetNameSafe(Bottom->LeaderPoseComponent.Get()),
		ShirtRange.X, ShirtRange.Y, ShirtBottomRatio, ShirtTopRatio,
		PantsRange.X, PantsRange.Y, PantsBottomRatio, PantsTopRatio, RatioTolerance);

	DestroyWorld(World);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
