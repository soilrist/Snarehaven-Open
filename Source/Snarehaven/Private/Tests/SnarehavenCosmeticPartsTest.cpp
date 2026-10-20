// 스킨 조각 c (`docs/specs/skin-parts.md` 5장 3 · 6장 c) 2층 검사 — 얼굴 장식·신발·장갑 칸이 붙는 길.
//  ① 신발·장갑은 옷과 같은 스켈레탈 한 겹이다 — 몸에 붙고 몸 자세(리더)를 따른다. 얼굴 장식은 소켓에 매다는 스태틱이다.
//  ② 꾸미기를 벗기면 그 칸만 비고 다른 칸·머리카락은 그대로다.
//  ③ 몸 메시의 `Socket_Face` 는 머리 뼈에 붙어 있고, 참조 자세에서 머리 앞면 가운데(정점으로 잼)에 있다.
// 칸마다 진짜 메시는 아직 없다(조각 f~l) — 표 사본에 **검사 전용 꾸미기 행**을 더한다(Verification.md 6):
// 신발 = 플란넬 셔츠 메시, 장갑 = 작업 바지 메시, 얼굴 장식 = 요리 모자 메시를 빌린다(붙는 길만 본다).
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.Item.Cosmetic; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AnimationRuntime.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/ItemRow.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Equipment/EquipmentComponent.h"
#include "SnarehavenCharacter.h"
#if WITH_EDITOR
#include "MeshDescription.h"
#endif

namespace SnareCosmeticPartsTest
{
	const TCHAR* ItemTablePath = TEXT("/Game/Game/Data/Items/DT_ItemTable.DT_ItemTable");
	const TCHAR* BodyPath = TEXT("/Game/Game/Characters/Player/Snare/SK_Player_Body.SK_Player_Body");
	const FName FaceSocket(TEXT("Socket_Face"));
	const FName ShoesRow(TEXT("Item_TestCosmeticShoes"));
	const FName GlovesRow(TEXT("Item_TestCosmeticGloves"));
	const FName FaceRow(TEXT("Item_TestCosmeticFace"));

	// 검사 전용 값 — 머리 띠(정수리 아래 범위, 메시 공간 cm)와 팔 끝 자르기. `setup_equipment_socket.py` 의
	// `FACE_BAND_BELOW_CROWN`·`FACE_BAND_MAX_ABS_X` 와 같은 잼(`SnarehavenPlayerBoxHeadTest` 띠와 같음).
	constexpr float BandTopBelowCrown = 10.f;
	constexpr float BandBottomBelowCrown = 30.f;
	constexpr float BandMaxAbsX = 35.f;
	constexpr float FaceToleranceCm = 1.f;

	/** 표 사본 + 검사 전용 꾸미기 행 셋을 건 플레이어 (`SnarehavenCosmeticCoverTest` 와 같은 방식). */
	struct FRig
	{
		UWorld* World = nullptr;
		UDataTable* Table = nullptr;
		USkeletalMesh* ShoesMesh = nullptr;
		USkeletalMesh* GlovesMesh = nullptr;
		UStaticMesh* FaceMesh = nullptr;

		FRig()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());

			const UDataTable* const Original = LoadObject<UDataTable>(nullptr, ItemTablePath);
			if (Original == nullptr)
			{
				return;
			}
			const FItemRow* const Shirt = Original->FindRow<FItemRow>(TEXT("Item_FlannelShirt"), TEXT("CosmeticPartsTest"));
			const FItemRow* const Pants = Original->FindRow<FItemRow>(TEXT("Item_WorkPants"), TEXT("CosmeticPartsTest"));
			const FItemRow* const CookHat = Original->FindRow<FItemRow>(TEXT("Item_CookHat"), TEXT("CosmeticPartsTest"));
			if (Shirt == nullptr || Pants == nullptr || CookHat == nullptr)
			{
				return;
			}
			Table = DuplicateObject<UDataTable>(Original, GetTransientPackage());
			FItemRow Shoes = *Shirt;
			Shoes.ItemID = TEXT("test.cosmetic_shoes");
			Shoes.ItemType = EItemType::Cosmetic;
			Shoes.EquipSlot = EEquipSlot::Shoes;
			Table->AddRow(ShoesRow, Shoes);
			FItemRow Gloves = *Pants;
			Gloves.ItemID = TEXT("test.cosmetic_gloves");
			Gloves.ItemType = EItemType::Cosmetic;
			Gloves.EquipSlot = EEquipSlot::Gloves;
			Table->AddRow(GlovesRow, Gloves);
			FItemRow Face = *CookHat;
			Face.ItemID = TEXT("test.cosmetic_face");
			Face.ItemType = EItemType::Cosmetic;
			Face.EquipSlot = EEquipSlot::FaceAccessory;
			Face.EquipSocketName = FaceSocket;
			Table->AddRow(FaceRow, Face);
			ShoesMesh = Shoes.EquipOutfitMeshRef.LoadSynchronous();
			GlovesMesh = Gloves.EquipOutfitMeshRef.LoadSynchronous();
			FaceMesh = Face.EquipMeshRef.LoadSynchronous();
		}

		ASnarehavenCharacter* SpawnPlayer() const
		{
			ASnarehavenCharacter* const Player = World->SpawnActor<ASnarehavenCharacter>();
			if (Player == nullptr || !Player->ApplyPlayerBody() || Table == nullptr)
			{
				return nullptr;
			}
			UEquipmentComponent* const Equipment = Player->GetEquipment();
			const FObjectProperty* const TableProp =
				FindFProperty<FObjectProperty>(UEquipmentComponent::StaticClass(), TEXT("ItemTable"));
			if (Equipment == nullptr || TableProp == nullptr)
			{
				return nullptr;
			}
			TableProp->SetObjectPropertyValue_InContainer(Equipment, Table);
			return Player;
		}

		~FRig()
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	};

	/** 그 칸 옷 한 겹이 이 메시로 몸에 붙어 몸 자세를 따르나. */
	bool LayerFollowsBody(const UEquipmentComponent* Equipment, EEquipSlot Slot, const USkeletalMesh* Mesh,
		const USkeletalMeshComponent* Body)
	{
		const USkeletalMeshComponent* const Layer = Equipment->GetEquippedOutfit(Slot);
		return Layer != nullptr && Layer->GetSkeletalMeshAsset() == Mesh && Layer->GetAttachParent() == Body
			&& Layer->LeaderPoseComponent.Get() == Body;
	}

	const USkeletalMesh* ShownLayerMesh(const UEquipmentComponent* Equipment, EEquipSlot Slot)
	{
		const USkeletalMeshComponent* const Layer = Equipment->GetEquippedOutfit(Slot);
		return Layer != nullptr ? Layer->GetSkeletalMeshAsset() : nullptr;
	}

	const UStaticMesh* ShownStaticMesh(const UEquipmentComponent* Equipment, EEquipSlot Slot)
	{
		const UStaticMeshComponent* const Visual = Equipment->GetEquippedMesh(Slot);
		return Visual != nullptr ? Visual->GetStaticMesh().Get() : nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenCosmeticPartsAttachTest, "Snarehaven.Item.Cosmetic.ShoesGlovesFaceAttach",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenCosmeticPartsAttachTest::RunTest(const FString& Parameters)
{
	using namespace SnareCosmeticPartsTest;

	TestTrue(TEXT("신발은 옷 부위(스켈레탈 한 겹)"), UEquipmentComponent::IsOutfitSlot(EEquipSlot::Shoes));
	TestTrue(TEXT("장갑은 옷 부위(스켈레탈 한 겹)"), UEquipmentComponent::IsOutfitSlot(EEquipSlot::Gloves));
	TestFalse(TEXT("얼굴 장식은 소켓에 매다는 부위"), UEquipmentComponent::IsOutfitSlot(EEquipSlot::FaceAccessory));

	FRig Rig;
	ASnarehavenCharacter* const Player = Rig.SpawnPlayer();
	if (!TestNotNull(TEXT("표 사본을 건 플레이어"), Player) || !TestNotNull(TEXT("신발 자리표시 메시"), Rig.ShoesMesh)
		|| !TestNotNull(TEXT("장갑 자리표시 메시"), Rig.GlovesMesh) || !TestNotNull(TEXT("얼굴 자리표시 메시"), Rig.FaceMesh))
	{
		return false;
	}
	UEquipmentComponent* const Equipment = Player->GetEquipment();
	USkeletalMeshComponent* const Body = Player->GetMesh();
	TestTrue(TEXT("몸에 Socket_Face 소켓이 있다"), Body->DoesSocketExist(FaceSocket));

	// ① 칸마다 붙음
	TestTrue(TEXT("신발 꾸미기를 입었다"), Equipment->SetCosmetic(EEquipSlot::Shoes, ShoesRow));
	TestTrue(TEXT("장갑 꾸미기를 입었다"), Equipment->SetCosmetic(EEquipSlot::Gloves, GlovesRow));
	TestTrue(TEXT("얼굴 장식 꾸미기를 입었다"), Equipment->SetCosmetic(EEquipSlot::FaceAccessory, FaceRow));
	TestTrue(TEXT("신발 = 몸에 붙은 한 겹·몸 자세를 따름"), LayerFollowsBody(Equipment, EEquipSlot::Shoes, Rig.ShoesMesh, Body));
	TestTrue(TEXT("장갑 = 몸에 붙은 한 겹·몸 자세를 따름"), LayerFollowsBody(Equipment, EEquipSlot::Gloves, Rig.GlovesMesh, Body));
	TestNotEqual(TEXT("신발과 장갑은 다른 한 겹"), static_cast<const USkeletalMeshComponent*>(Equipment->GetEquippedOutfit(EEquipSlot::Shoes)),
		static_cast<const USkeletalMeshComponent*>(Equipment->GetEquippedOutfit(EEquipSlot::Gloves)));
	const UStaticMeshComponent* const FaceVisual = Equipment->GetEquippedMesh(EEquipSlot::FaceAccessory);
	if (TestNotNull(TEXT("얼굴 장식 메시 컴포넌트"), FaceVisual))
	{
		TestEqual(TEXT("얼굴 장식 메시 = 자리표시"), FaceVisual->GetStaticMesh().Get(), static_cast<UStaticMesh*>(Rig.FaceMesh));
		TestEqual(TEXT("얼굴 장식은 Socket_Face 에 붙었다"), FaceVisual->GetAttachSocketName(), FaceSocket);
		TestEqual(TEXT("얼굴 장식의 부모 = 몸"), FaceVisual->GetAttachParent(), static_cast<USceneComponent*>(Body));
		UE_LOG(LogTemp, Display, TEXT("[CosmeticParts] 얼굴 소켓 월드 %s · 장식 %s"),
			*Body->GetSocketLocation(FaceSocket).ToCompactString(), *FaceVisual->GetComponentLocation().ToCompactString());
	}
	// 머리카락은 첫 다시 그리기 때 붙는다 — 그래서 「입기 전」 값이 아니라 빈 머리면 보인다는 것을 본다.
	TestTrue(TEXT("얼굴 장식은 머리카락을 안 가린다"), Equipment->IsHairShown());
	TestEqual(TEXT("머리 칸은 비어 있다"), ShownStaticMesh(Equipment, EEquipSlot::Head), static_cast<const UStaticMesh*>(nullptr));

	// ② 칸마다 뗌 — 그 칸만 빈다
	TestTrue(TEXT("신발만 벗겼다"), Equipment->SetCosmetic(EEquipSlot::Shoes, NAME_None));
	TestEqual(TEXT("신발 한 겹이 비었다"), ShownLayerMesh(Equipment, EEquipSlot::Shoes), static_cast<const USkeletalMesh*>(nullptr));
	TestTrue(TEXT("장갑은 그대로"), LayerFollowsBody(Equipment, EEquipSlot::Gloves, Rig.GlovesMesh, Body));
	TestEqual(TEXT("얼굴 장식도 그대로"), ShownStaticMesh(Equipment, EEquipSlot::FaceAccessory), static_cast<const UStaticMesh*>(Rig.FaceMesh));
	TestTrue(TEXT("장갑을 벗겼다"), Equipment->SetCosmetic(EEquipSlot::Gloves, NAME_None));
	TestEqual(TEXT("장갑 한 겹이 비었다"), ShownLayerMesh(Equipment, EEquipSlot::Gloves), static_cast<const USkeletalMesh*>(nullptr));
	TestTrue(TEXT("얼굴 장식을 벗겼다"), Equipment->SetCosmetic(EEquipSlot::FaceAccessory, NAME_None));
	TestEqual(TEXT("얼굴 장식 메시가 비었다"), ShownStaticMesh(Equipment, EEquipSlot::FaceAccessory), static_cast<const UStaticMesh*>(nullptr));
	TestEqual(TEXT("꾸미기 목록이 비었다"), Equipment->GetCosmeticSlots().Num(), 0);
	TestTrue(TEXT("머리카락은 그대로 보인다"), Equipment->IsHairShown());
	return true;
}

#if WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenCosmeticFaceSocketTest, "Snarehaven.Item.Cosmetic.FaceSocketOnFaceFront",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenCosmeticFaceSocketTest::RunTest(const FString& Parameters)
{
	using namespace SnareCosmeticPartsTest;

	USkeletalMesh* const Body = LoadObject<USkeletalMesh>(nullptr, BodyPath);
	const FMeshDescription* const Description = Body ? Body->GetMeshDescription(0) : nullptr;
	if (!TestNotNull(TEXT("몸 메시 정점(메시 설명 LOD0)"), Description))
	{
		return false;
	}

	// 머리 띠 정점 → 가로 가운데 절반 중 가장 앞(+Y = 몸 앞) · 높이 = 띠 가운데 (`setup_equipment_socket.py` 와 같은 잼)
	const TVertexAttributesConstRef<FVector3f> Positions = Description->GetVertexPositions();
	float Crown = -BIG_NUMBER;
	for (const FVertexID Vertex : Description->Vertices().GetElementIDs())
	{
		Crown = FMath::Max(Crown, Positions[Vertex].Z);
	}
	const float Top = Crown - BandTopBelowCrown;
	const float Bottom = Crown - BandBottomBelowCrown;
	TArray<FVector3f> Band;
	float MinX = BIG_NUMBER;
	float MaxX = -BIG_NUMBER;
	for (const FVertexID Vertex : Description->Vertices().GetElementIDs())
	{
		const FVector3f P = Positions[Vertex];
		if (P.Z >= Bottom && P.Z <= Top && FMath::Abs(P.X) <= BandMaxAbsX)
		{
			Band.Add(P);
			MinX = FMath::Min(MinX, P.X);
			MaxX = FMath::Max(MaxX, P.X);
		}
	}
	if (!TestTrue(TEXT("머리 띠에 정점이 있다"), Band.Num() > 20))
	{
		return false;
	}
	const float MidX = (MinX + MaxX) * 0.5f;
	float FrontY = -BIG_NUMBER;
	for (const FVector3f& P : Band)
	{
		if (FMath::Abs(P.X - MidX) <= (MaxX - MinX) * 0.25f)
		{
			FrontY = FMath::Max(FrontY, P.Y);
		}
	}
	const FVector Expected(MidX, FrontY, (Top + Bottom) * 0.5f);

	const USkeletalMeshSocket* const Socket = Body->FindSocket(FaceSocket);
	if (!TestNotNull(TEXT("몸 메시에 Socket_Face 가 있다"), Socket))
	{
		UE_LOG(LogTemp, Display, TEXT("[FaceSocket] 정수리 %.2f · 머리 띠 %d · 앞면 가운데(메시 공간) %s"),
			Crown, Band.Num(), *Expected.ToString());
		return false;
	}
	TestEqual(TEXT("Socket_Face 는 머리 뼈에 붙었다"), Socket->BoneName, FName(TEXT("Head")));
	const int32 HeadIndex = Body->GetRefSkeleton().FindBoneIndex(Socket->BoneName);
	if (!TestTrue(TEXT("머리 뼈가 있다"), HeadIndex != INDEX_NONE))
	{
		return false;
	}
	const FTransform HeadRef = FAnimationRuntime::GetComponentSpaceTransformRefPose(Body->GetRefSkeleton(), HeadIndex);
	const FVector Actual = (Socket->GetSocketLocalTransform() * HeadRef).GetLocation();
	UE_LOG(LogTemp, Display, TEXT("[FaceSocket] 정수리 %.2f · 머리 띠 %d · 앞면 가운데 %s · 소켓 %s · 차이 %.2fcm"),
		Crown, Band.Num(), *Expected.ToString(), *Actual.ToString(), FVector::Dist(Expected, Actual));
	TestTrue(FString::Printf(TEXT("Socket_Face 가 머리 앞면 가운데에 있다(차이 %.2f ≤ %.1fcm)"), FVector::Dist(Expected, Actual),
		FaceToleranceCm), FVector::Dist(Expected, Actual) <= FaceToleranceCm);
	return true;
}
#endif

#endif
