// #0204 (M13-19 ②③) 2층 검사 — 모자.
//  ② 죽어서 가방을 두고 와도 모자가 머리에 남던 결함: 서버 스냅샷의 가방에 쓴 모자가 없으면 벗겨진다.
//     (가방에 그대로 있으면 계속 쓴다 — 반대쪽도 본다.)
//  ③ `DT_ItemTable` 의 머리(`Head`) 행마다 메시가 실제로 읽히고, 소켓이 사람 몸 메시에 진짜 있다.
//     (Item_MerchantHat 이 메시·소켓 없이 들어와 써도 안 보이던 결함.)
// #0206 (M13-21) ④ 모자 10행을 폰 몸(배율 1.76)에 씌우면 월드 크기가 DESIGN 3.5 「모자 크기」 다.
//     M13-35 부터 한 배율: 월드 긴 변(X·Y) = 행의 `HatWidthCm`(0 이면 기본 폭) · 높이/폭 비 = 메시 비.
//  ⑤ 배낭 화면 「장착」이 답 전에 붙인 겉모습을 서버가 거절하면 원래 모자(없으면 빈 머리)로 되돌린다.
// M19-01 ⑥ 모자 칸이 비면 기본 머리카락 덩어리가 보이고, 모자를 쓰면 숨는다(호스트·복제 두 길).
// 루프#857 ⑦ 감독 보정 「모자가 머리 살을 뚫는다」: 모자 행마다 실제 머리 꼭짓점이 모자 안쪽 면 밖으로 나오는 점이 0개다.
// 스킨 조각 f ③④ 에 꾸미기 모자 6벌(`EItemType::Cosmetic`)도 든다 — 꾸미기는 `SetCosmetic` 으로 입혀 같은 크기 규칙을 보고,
//     머리 행마다 메시 삼각형이 명세 4장 「모자 스태틱 1,500」 안이다.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Data/ItemRow.h"
#include "Engine/DataTable.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Backend/SnareBackendSubsystem.h"
#include "Backend/SnarePlayerSnapshot.h"
#include "Components/SpotLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Equipment/EquipmentComponent.h"
#include "Inventory/BackpackComponent.h"
#include "Misc/ScopeExit.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "SnarehavenCharacter.h"
#include "UI/BackpackScreenWidget.h"
#include "World/QuestGiverActor.h"
#include "World/ShopVendorActor.h"

namespace
{
	const TCHAR* HatItemTablePath = TEXT("/Game/Game/Data/Items/DT_ItemTable.DT_ItemTable");
	const TCHAR* HatBodyMeshPath = TEXT("/Game/Game/Characters/Player/Snare/SK_Player_Body.SK_Player_Body");

	// 검사 전용 이름 (Verification.md 6 — 표 행이 아니어도 된다: 보는 것은 "가방에 있나" 뿐이다).
	const FName TestHatID(TEXT("test.hat"));
	const FName OtherItemID(TEXT("test.other"));

	FInventorySlot MakeSlot(FName ItemID, int32 Count)
	{
		FInventorySlot Slot;
		Slot.ItemID = ItemID;
		Slot.Count = Count;
		return Slot;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenHatDropOnSnapshotTest, "Snarehaven.Hat.Snapshot.UnwearsMissingHat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenHatDropOnSnapshotTest::RunTest(const FString& Parameters)
{
	UBackpackComponent* Backpack = NewObject<UBackpackComponent>();

	Backpack->ApplyBackendHat(TestHatID);
	Backpack->ApplyBackendInventory({ MakeSlot(TestHatID, 1), MakeSlot(OtherItemID, 3) });
	TestEqual(TEXT("가방에 있으면 계속 쓴다"), Backpack->GetWornHatID(), TestHatID);

	// 죽어서 가방을 두고 옴 = 새 스냅샷에 모자가 없다 (다른 물건만 있거나 개수 0).
	Backpack->ApplyBackendInventory({ MakeSlot(OtherItemID, 3), MakeSlot(TestHatID, 0) });
	TestTrue(TEXT("가방에 없는 모자는 벗겨진다"), Backpack->GetWornHatID().IsNone());

	Backpack->ApplyBackendHat(TestHatID);
	Backpack->ApplyBackendInventory({});
	TestTrue(TEXT("빈 가방이면 벗겨진다"), Backpack->GetWornHatID().IsNone());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenHatRowsWearableTest, "Snarehaven.Hat.Table.EveryHatHasMeshAndSocket",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenHatRowsWearableTest::RunTest(const FString& Parameters)
{
	const UDataTable* ItemTable = LoadObject<UDataTable>(nullptr, HatItemTablePath);
	const USkeletalMesh* Body = LoadObject<USkeletalMesh>(nullptr, HatBodyMeshPath);
	if (!TestNotNull(TEXT("아이템 표"), ItemTable) || !TestNotNull(TEXT("사람 몸 메시"), Body))
	{
		return false;
	}

	// `docs/specs/skin-parts.md` 4장 「삼각형 한도 — 모자 스태틱 1,500」.
	constexpr int32 HatTriangleLimit = 1500;
	int32 HatRows = 0;
	TSet<FSoftObjectPath> HatMeshes;
	for (const TPair<FName, uint8*>& Pair : ItemTable->GetRowMap())
	{
		const FItemRow& Row = *reinterpret_cast<const FItemRow*>(Pair.Value);
		if (Row.EquipSlot != EEquipSlot::Head)
		{
			continue;
		}
		++HatRows;
		const FString RowName = Pair.Key.ToString();
		const UStaticMesh* const Mesh = Row.EquipMeshRef.LoadSynchronous();
		TestNotNull(FString::Printf(TEXT("[%s] 모자 메시를 실제로 읽는다: %s"), *RowName, *Row.EquipMeshRef.ToString()), Mesh);
		if (Mesh != nullptr)
		{
			const int32 Triangles = Mesh->GetNumTriangles(0);
			AddInfo(FString::Printf(TEXT("[%s] 삼각형 %d"), *RowName, Triangles));
			TestTrue(FString::Printf(TEXT("[%s] 삼각형 %d ≤ %d (명세 4장)"), *RowName, Triangles, HatTriangleLimit),
				Triangles > 0 && Triangles <= HatTriangleLimit);
		}
		// M13-35 — 엔진 자리표시(`/Engine/BasicShapes`)가 아니라 모자 폴더의 자기 메시다.
		TestTrue(FString::Printf(TEXT("[%s] 자리표시가 아닌 모자 메시: %s"), *RowName, *Row.EquipMeshRef.ToString()),
			Row.EquipMeshRef.ToString().StartsWith(TEXT("/Game/Game/Characters/Equipment/Hats/")));
		HatMeshes.Add(Row.EquipMeshRef.ToSoftObjectPath());
		TestTrue(FString::Printf(TEXT("[%s] 소켓 %s 이 몸 메시에 있다"), *RowName, *Row.EquipSocketName.ToString()),
			!Row.EquipSocketName.IsNone() && Body->FindSocket(Row.EquipSocketName) != nullptr);
	}

	TestTrue(TEXT("모자 행이 있다 (검사가 빈손이 아니다)"), HatRows > 0);
	TestEqual(TEXT("모자마다 메시가 다르다 (한 메시를 돌려 쓰지 않는다)"), HatMeshes.Num(), HatRows);
	AddInfo(FString::Printf(TEXT("모자 행 %d개 · 메시 %d종 검사"), HatRows, HatMeshes.Num()));
	return true;
}

namespace SnareHatTest
{
	// 2층 허용 오차 (ROADMAP M13-21 합격 「±3cm」 · M13-35 높이/폭 비는 메시 비와 ±0.05).
	constexpr double SizeToleranceCm = 3.0;
	constexpr double RatioTolerance = 0.05;
	const FName MerchantHatID(TEXT("item.merchant_hat"));
	const FName FarmerHatID(TEXT("item.farmer_hat"));
	const FName CrownHatID(TEXT("item.meadow_crown"));
	// 보이는 머리카락이 모자 안쪽 면 밖으로 나와도 되는 깊이, 몸 메시 cm — 시험 전용 값(밸런스 숫자 아님).
	// 루프#1355 실측 남은 것은 초원 캡 0.58·두건 매듭 꼬리 0.55 이고 가까운 뒤 캡처(`lh1355_Head2_b`·`Head3_b`)에서 안 보였다.
	// 잡으려는 고장은 수 cm(#1336 농사 모자 위 머리카락 돔) — 1cm 넘게 나오면 실패.
	constexpr double HairPokeToleranceCm = 1.0;
	// 모자 아랫단에서 이만큼 위까지는 머리카락이 모자 밑으로 나와 겹쳐 보이는 띠 — 뚫음으로 안 센다(시험 전용 값,
	// DESIGN 3.5 「모자 밑 머리카락」 밀어 넣기 1.0cm + 재는 칸 10° 안 아랫단 높이 차 1.0cm).
	constexpr double HairRimBandCm = 2.0;
	// 얼굴 장식 끈·안경다리가 머리 겉(살 또는 머리카락 껍질) 밖으로 떠도 되는 거리, 몸 메시 cm — 시험 전용 값(밸런스 숫자 아님).
	// 끈 틈 최대 1.0(`face_make.py` 잠수 마스크) + 끈 굵기 지름 2.0 = 3.0. 고치기 전(옛 네모 둘레)은 모서리가 3.9~4.6 이었다.
	constexpr double FaceStrapFloatToleranceCm = 3.0;

	/** 게임 월드 + 백엔드 + 사람 몸을 올린 플레이어 하나. */
	struct FRig
	{
		UWorld* World = nullptr;
		USnareBackendSubsystem* Backend = nullptr;
		ASnarehavenCharacter* Player = nullptr;

		FRig()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			Backend = NewObject<USnareBackendSubsystem>(NewObject<UGameInstance>(GetTransientPackage()));
			Player = World->SpawnActor<ASnarehavenCharacter>();
			if (Player != nullptr && !Player->ApplyPlayerBody())
			{
				Player = nullptr;
			}
		}

		~FRig()
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenHatWorldSizeTest, "Snarehaven.Hat.Visual.WorldSizeMatchesDesign",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenHatWorldSizeTest::RunTest(const FString& Parameters)
{
	using namespace SnareHatTest;
	FRig Rig;
	const UDataTable* ItemTable = LoadObject<UDataTable>(nullptr, HatItemTablePath);
	if (!TestNotNull(TEXT("사람 몸을 올린 플레이어"), Rig.Player) || !TestNotNull(TEXT("아이템 표"), ItemTable))
	{
		return false;
	}
	UEquipmentComponent* const Equipment = Rig.Player->GetEquipment();
	const float BodyScale = Rig.Player->GetMesh()->GetComponentScale().Z;
	AddInfo(FString::Printf(TEXT("몸 배율 %.2f · 기본 폭 %.1fcm"), BodyScale, Equipment->GetDefaultHatWidthCm()));
	TestTrue(TEXT("몸 배율이 1 이 아니다 (상쇄를 실제로 본다)"), !FMath::IsNearlyEqual(BodyScale, 1.f));

	int32 HatRows = 0;
	int32 CosmeticRows = 0;
	for (const TPair<FName, uint8*>& Pair : ItemTable->GetRowMap())
	{
		const FItemRow& Row = *reinterpret_cast<const FItemRow*>(Pair.Value);
		if (Row.EquipSlot != EEquipSlot::Head)
		{
			continue;
		}
		++HatRows;
		const FString RowName = Pair.Key.ToString();
		// 꾸미기 행은 `EquipItem` 이 거절한다 — 겉모습 덮개 길(`SetCosmetic`)로 입히고, 재고 나면 벗긴다.
		const bool bCosmetic = Row.ItemType == EItemType::Cosmetic;
		CosmeticRows += bCosmetic ? 1 : 0;
		Equipment->SetCosmetic(EEquipSlot::Head, NAME_None);
		const bool bWorn = bCosmetic ? Equipment->SetCosmetic(EEquipSlot::Head, Pair.Key) : Equipment->EquipItem(Pair.Key);
		if (!TestTrue(FString::Printf(TEXT("[%s] 머리에 들었다 (%s)"), *RowName, bCosmetic ? TEXT("꾸미기") : TEXT("장비")), bWorn))
		{
			continue;
		}
		const UStaticMeshComponent* const Visual = Equipment->GetEquippedMesh(EEquipSlot::Head);
		if (!TestTrue(FString::Printf(TEXT("[%s] 메시 경로가 실제로 불러졌다"), *RowName), Visual != nullptr && Visual->GetStaticMesh() != nullptr))
		{
			continue;
		}
		const FVector Size = Visual->CalcBounds(Visual->GetComponentTransform()).BoxExtent * 2.f;
		const FVector MeshSize = Visual->GetStaticMesh()->GetBounds().BoxExtent * 2.f;
		const double WantWidth = Row.HatWidthCm > 0.f ? Row.HatWidthCm : Equipment->GetDefaultHatWidthCm();
		const double Width = FMath::Max(Size.X, Size.Y);
		const double Ratio = Size.Z / Width;
		const double WantRatio = MeshSize.Z / FMath::Max(MeshSize.X, MeshSize.Y);
		AddInfo(FString::Printf(TEXT("[%s] 월드 크기 %.1f×%.1f×%.1fcm · 표 폭 %.1f · 높이/폭 %.2f (메시 %.2f)"),
			*RowName, Size.X, Size.Y, Size.Z, WantWidth, Ratio, WantRatio));
		TestTrue(FString::Printf(TEXT("[%s] 긴 변 %.1f ≈ 표 폭 %.1f"), *RowName, Width, WantWidth),
			FMath::IsNearlyEqual(Width, WantWidth, SizeToleranceCm));
		TestTrue(FString::Printf(TEXT("[%s] 높이/폭 %.2f ≈ 메시 %.2f"), *RowName, Ratio, WantRatio),
			FMath::IsNearlyEqual(Ratio, WantRatio, RatioTolerance));
	}
	Equipment->SetCosmetic(EEquipSlot::Head, NAME_None);
	TestEqual(TEXT("모자 행 70개 (장비 13 = 10종 + 8.16 채굴 + 11 출석 새싹 + 10.6 우두머리 · 꾸미기 57 = 스킨 조각 f 6 + M83-02 5 + M83-03 5 + M84-02 5 + M84-03 5 + M88-02 5 + M88-03 5 + M90-01 5 + M90-02 5 + M91-01 6 + M91-02 5)"), HatRows, 70);
	TestEqual(TEXT("꾸미기 모자 57벌 (명세 4장 6벌 + M83-02 5 + M83-03 5 + M84-02 5 + M84-03 5 + M88-02 5 + M88-03 5 + M90-01 5 + M90-02 5 + M91-01 6 + M91-02 5)"), CosmeticRows, 57);
	return true;
}

#if WITH_EDITORONLY_DATA // GetMeshDescription 은 에디터 전용 — 게임(iOS·Win64) 빌드에선 뺀다
namespace SnareHatTest
{
	/** 몸 메시 머리 꼭짓점(참조 자세) — z ≥ 70(메시 cm, 몸 키 100) 이고 가운데 ±30cm 안(팔·손은 뺀다). 쏘는 점은 머리 속 z 83. */
	bool GatherHeadRef(const USkeletalMesh& BodyAsset, TArray<FVector>& OutHead, FVector& OutCenter)
	{
		const FMeshDescription* const BodyDesc = BodyAsset.GetMeshDescription(0);
		if (BodyDesc == nullptr)
		{
			return false;
		}
		constexpr double HeadMinZ = 70.0;
		constexpr double HeadHalfCm = 30.0;
		const TVertexAttributesConstRef<FVector3f> BodyPos = BodyDesc->GetVertexPositions();
		OutHead.Reset();
		OutCenter = FVector::ZeroVector;
		for (const FVertexID Vertex : BodyDesc->Vertices().GetElementIDs())
		{
			const FVector P(BodyPos[Vertex]);
			if (P.Z >= HeadMinZ && FMath::Abs(P.X) < HeadHalfCm && FMath::Abs(P.Y) < HeadHalfCm)
			{
				OutHead.Add(P);
				OutCenter += P;
			}
		}
		if (OutHead.Num() == 0)
		{
			return false;
		}
		OutCenter /= OutHead.Num();
		OutCenter.Z = 83.0;
		return true;
	}

	/** 머리 살·보이는 머리카락 꼭짓점 중 모자 안쪽 면 밖으로 나온 수. */
	struct FPoke
	{
		int32 Covered = 0;
		int32 Poking = 0;
		double Worst = 0.0;
		/** 그려지는 머리카락 꼭짓점 중 모자가 덮는 수(아랫단 띠 포함) — 실제로 쟀다는 증거. */
		int32 HairSeen = 0;
		int32 HairCovered = 0;
		int32 HairPoking = 0;
		double HairWorst = 0.0;
		/** 가장 많이 나온 머리카락 꼭짓점, 머리카락 메시 공간(정수리 원점 · +Y 얼굴 앞). */
		FVector HairWorstLocal = FVector::ZeroVector;
	};

	/**
	 * 루프#857 — 실제 몸 머리 꼭짓점(참조 자세, 머리 뼈 기준으로 지금 자세에 옮김)마다 머리 가운데에서 그 점으로 쏜 반직선이
	 * 모자 삼각형을 처음 만나는 거리를 잰다. 점이 그 거리보다 멀면 머리가 모자 안쪽 면을 뚫고 나온 점이다.
	 * 모자가 덮지 않는 곳(반직선이 안 닿는 점)은 세지 않는다. INBOX 보정 10-19 22:45 ③ — 보이는 머리카락 꼭짓점도 같은 선으로 잰다
	 * (머티리얼이 자른 `HairTopZ` 위는 안 그리므로 뺀다, 머리카락 메시 공간 z).
	 */
	FPoke MeasurePoking(const USkeletalMeshComponent& Body, FName Socket, const UStaticMeshComponent& Hat, const UStaticMeshComponent* Hair,
		float HairTopZ, const TArray<FVector>& HeadRef, const FVector& CenterRef)
	{
		FPoke Out;
		const USkeletalMesh* const BodyAsset = Body.GetSkeletalMeshAsset();
		const FMeshDescription* const HatDesc = Hat.GetStaticMesh() != nullptr ? Hat.GetStaticMesh()->GetMeshDescription(0) : nullptr;
		if (BodyAsset == nullptr || HatDesc == nullptr)
		{
			Out.Covered = -1;
			return Out;
		}
		const FReferenceSkeleton& RefSkel = BodyAsset->GetRefSkeleton();
		const FName HeadBone = Body.GetSocketBoneName(Socket);
		FTransform HeadRefPose = FTransform::Identity;
		for (int32 Bone = RefSkel.FindBoneIndex(HeadBone); Bone != INDEX_NONE; Bone = RefSkel.GetParentIndex(Bone))
		{
			HeadRefPose = HeadRefPose * RefSkel.GetRefBonePose()[Bone];
		}
		const FTransform HeadNow = Body.GetBoneTransform(HeadBone);
		const auto ToWorld = [&](const FVector& Ref) { return HeadNow.TransformPosition(HeadRefPose.InverseTransformPosition(Ref)); };

		// 모자 삼각형을 월드로 한 번만 옮긴다.
		const TVertexAttributesConstRef<FVector3f> HatPos = HatDesc->GetVertexPositions();
		const FTransform HatToWorld = Hat.GetComponentTransform();
		TArray<TTuple<FVector, FVector, FVector>> Tris;
		for (const FTriangleID Tri : HatDesc->Triangles().GetElementIDs())
		{
			TArrayView<const FVertexID> V = HatDesc->GetTriangleVertices(Tri);
			Tris.Emplace(HatToWorld.TransformPosition(FVector(HatPos[V[0]])), HatToWorld.TransformPosition(FVector(HatPos[V[1]])),
				HatToWorld.TransformPosition(FVector(HatPos[V[2]])));
		}

		const FVector Center = ToWorld(CenterRef);
		// 바깥으로 나온 깊이, 모자가 안 덮으면 음수.
		const auto PokeDepth = [&](const FVector& Point) -> double
		{
			const FVector Dir = (Point - Center).GetSafeNormal();
			const double Dist = FVector::Distance(Point, Center);
			const FVector End = Center + Dir * (Dist + 200.0);
			double First = UE_BIG_NUMBER;
			for (const TTuple<FVector, FVector, FVector>& T : Tris)
			{
				FVector Hit, Normal;
				if (FMath::SegmentTriangleIntersection(Center, End, T.Get<0>(), T.Get<1>(), T.Get<2>(), Hit, Normal))
				{
					First = FMath::Min(First, FVector::Distance(Hit, Center));
				}
			}
			return First > UE_BIG_NUMBER * 0.5 ? -1.0 : Dist - First;
		};
		for (const FVector& Ref : HeadRef)
		{
			const double Depth = PokeDepth(ToWorld(Ref));
			if (Depth == -1.0)
			{
				continue;   // 모자가 안 덮는 점
			}
			++Out.Covered;
			if (Depth > 0.0)
			{
				++Out.Poking;
				Out.Worst = FMath::Max(Out.Worst, Depth);
			}
		}
		const FMeshDescription* const HairDesc = Hair != nullptr && Hair->IsVisible() && Hair->GetStaticMesh() != nullptr
			? Hair->GetStaticMesh()->GetMeshDescription(0) : nullptr;
		if (HairDesc != nullptr)
		{
			const TVertexAttributesConstRef<FVector3f> HairPos = HairDesc->GetVertexPositions();
			const FTransform HairToWorld = Hair->GetComponentTransform();
			// 둘레 10° 칸마다 모자 가장 낮은 높이(머리카락 메시 공간) — 그 아랫단 띠 안의 머리카락은 「모자 밑으로 나온 머리카락」이다
			// (DESIGN 3.5 「모자 밑 머리카락」: 아랫단 위로 1cm 밀어 넣어 겹치게 한 것). 뚫음은 띠 위에서만 센다.
			constexpr int32 RimBins = 36;
			TArray<double> RimLowZ;
			RimLowZ.Init(UE_BIG_NUMBER, RimBins);
			const auto BinOf = [](const FVector& Local)
			{
				const double Deg = FMath::RadiansToDegrees(FMath::Atan2(Local.Y, Local.X)) + 180.0;
				return FMath::Clamp(FMath::FloorToInt32(Deg / (360.0 / RimBins)), 0, RimBins - 1);
			};
			const FTransform HatToHair = HatToWorld.GetRelativeTransform(HairToWorld);
			for (const FVertexID Vertex : HatDesc->Vertices().GetElementIDs())
			{
				const FVector Local = HatToHair.TransformPosition(FVector(HatPos[Vertex]));
				double& Low = RimLowZ[BinOf(Local)];
				Low = FMath::Min(Low, Local.Z);
			}
			for (const FVertexID Vertex : HairDesc->Vertices().GetElementIDs())
			{
				const FVector Local(HairPos[Vertex]);
				if (Local.Z > HairTopZ)
				{
					continue;   // 머티리얼이 안 그리는 윗부분
				}
				const double Depth = PokeDepth(HairToWorld.TransformPosition(Local));
				if (Depth == -1.0)
				{
					continue;   // 모자가 안 덮는 머리카락
				}
				++Out.HairSeen;
				if (Local.Z <= RimLowZ[BinOf(Local)] + HairRimBandCm)
				{
					continue;   // 아랫단 띠에서 모자 밑으로 나온 머리카락
				}
				++Out.HairCovered;
				if (Depth > HairPokeToleranceCm)
				{
					++Out.HairPoking;
					if (Depth > Out.HairWorst)
					{
						Out.HairWorst = Depth;
						Out.HairWorstLocal = Local;
					}
				}
			}
		}
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenHatNoPenetrationTest, "Snarehaven.Hat.Visual.NoHeadPenetration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenHatNoPenetrationTest::RunTest(const FString& Parameters)
{
	using namespace SnareHatTest;
	FRig Rig;
	const UDataTable* ItemTable = LoadObject<UDataTable>(nullptr, HatItemTablePath);
	if (!TestNotNull(TEXT("사람 몸을 올린 플레이어"), Rig.Player) || !TestNotNull(TEXT("아이템 표"), ItemTable))
	{
		return false;
	}
	UEquipmentComponent* const Equipment = Rig.Player->GetEquipment();
	Equipment->RefreshEquippedVisuals();
	USkeletalMeshComponent* const Body = Rig.Player->GetMesh();
	TArray<FVector> HeadRef;
	FVector CenterRef;
	if (!TestTrue(TEXT("머리 꼭짓점을 읽었다"), Body->GetSkeletalMeshAsset() != nullptr
		&& GatherHeadRef(*Body->GetSkeletalMeshAsset(), HeadRef, CenterRef) && HeadRef.Num() > 100))
	{
		return false;
	}

	int32 Checked = 0;
	int32 HairChecked = 0;
	for (const TPair<FName, uint8*>& Pair : ItemTable->GetRowMap())
	{
		const FItemRow& Row = *reinterpret_cast<const FItemRow*>(Pair.Value);
		if (Row.EquipSlot != EEquipSlot::Head)
		{
			continue;
		}
		const FString RowName = Pair.Key.ToString();
		const bool bCosmetic = Row.ItemType == EItemType::Cosmetic;
		Equipment->SetCosmetic(EEquipSlot::Head, NAME_None);
		const bool bWorn = bCosmetic ? Equipment->SetCosmetic(EEquipSlot::Head, Pair.Key) : Equipment->EquipItem(Pair.Key);
		const UStaticMeshComponent* const Visual = Equipment->GetEquippedMesh(EEquipSlot::Head);
		if (!TestTrue(FString::Printf(TEXT("[%s] 머리에 들었다"), *RowName), bWorn && Visual != nullptr && Visual->GetStaticMesh() != nullptr))
		{
			continue;
		}
		const FPoke Poke = MeasurePoking(*Body, Row.EquipSocketName, *Visual, Equipment->GetHairMesh(), Equipment->GetHairTopZ(), HeadRef, CenterRef);
		if (!TestTrue(FString::Printf(TEXT("[%s] 모자 메시 원본 설명"), *RowName), Poke.Covered >= 0))
		{
			continue;
		}
		++Checked;
		HairChecked += Poke.HairSeen > 0 ? 1 : 0;
		AddInfo(FString::Printf(TEXT("[%s] 모자가 덮는 머리 점 %d · 안쪽 면 밖으로 나온 점 %d · 가장 많이 %.2fcm · 덮는 머리카락 점 %d · 나온 점 %d · 가장 많이 %.2fcm %s · 자르는 높이 %.1f"),
			*RowName, Poke.Covered, Poke.Poking, Poke.Worst, Poke.HairCovered, Poke.HairPoking, Poke.HairWorst,
			Poke.HairPoking > 0 ? *Poke.HairWorstLocal.ToCompactString() : TEXT(""), Equipment->IsHairUnderHat() ? Equipment->GetHairTopZ() : 0.f));
		TestTrue(FString::Printf(TEXT("[%s] 모자가 머리 꼭짓점 일부는 덮는다 (%d)"), *RowName, Poke.Covered), Poke.Covered > 20);
		TestEqual(FString::Printf(TEXT("[%s] 머리 살이 모자를 뚫고 나온 점 0 (가장 많이 %.2fcm)"), *RowName, Poke.Worst), Poke.Poking, 0);
		TestEqual(FString::Printf(TEXT("[%s] 보이는 머리카락이 모자를 뚫고 나온 점 0 (가장 많이 %.2fcm)"), *RowName, Poke.HairWorst), Poke.HairPoking, 0);
	}
	Equipment->SetCosmetic(EEquipSlot::Head, NAME_None);
	TestEqual(TEXT("모자 행 70개를 모두 쟀다"), Checked, 70);
	AddInfo(FString::Printf(TEXT("모자 밑 머리카락 꼭짓점까지 잰 모자 %d개"), HairChecked));
	TestTrue(TEXT("머리카락 꼭짓점도 실제로 쟀다 (덮는 모자 대부분)"), HairChecked >= 60);

	// 역검증 — 시험 값: 머리카락을 통째로 그리고(자르는 높이를 아랫단 100cm 위로) 모자를 고르게 0.85배로 줄이면 껍질이 모자 윗면을 뚫는다
	// (#1336 농사 모자 위 머리카락 돔 + #1334 상인 모자가 0.55~0.80배로 줄던 고장). 지금 모자는 통째 머리카락보다 커서 줄이지 않으면 안 뚫는다(실측 0).
	// 모자 셋 중 하나라도 잡혀야 시험이 살아 있다. 메시·표는 안 건드리고 이 시험 안의 컴포넌트 배율만 바꿨다 되돌린다.
	Equipment->HatHairTuckCm = 100.f;
	int32 ReversePoking = 0;
	for (const TCHAR* const ReverseRow : {TEXT("Item_FarmerHat"), TEXT("Item_MerchantHat"), TEXT("Item_HarborFez")})
	{
		const FItemRow* const Row = ItemTable->FindRow<FItemRow>(ReverseRow, TEXT("역검증"));
		Equipment->SetCosmetic(EEquipSlot::Head, NAME_None);
		const bool bWorn = Row != nullptr && (Row->ItemType == EItemType::Cosmetic ? Equipment->SetCosmetic(EEquipSlot::Head, ReverseRow)
			: Equipment->EquipItem(ReverseRow));
		UStaticMeshComponent* const Visual = Equipment->GetEquippedMesh(EEquipSlot::Head);
		if (!TestTrue(FString::Printf(TEXT("역검증: [%s] 썼다"), ReverseRow), bWorn && Visual != nullptr))
		{
			continue;
		}
		const FVector Scale = Visual->GetComponentScale();
		Visual->SetWorldScale3D(Scale * 0.85);
		const FPoke Poke = MeasurePoking(*Body, Row->EquipSocketName, *Visual, Equipment->GetHairMesh(), Equipment->GetHairTopZ(), HeadRef, CenterRef);
		Visual->SetWorldScale3D(Scale);
		AddInfo(FString::Printf(TEXT("역검증 [%s] 머리카락 통째(자르는 높이 %.1f) · 모자 0.85배 · 나온 점 %d · 가장 많이 %.2fcm"),
			ReverseRow, Equipment->GetHairTopZ(), Poke.HairPoking, Poke.HairWorst));
		ReversePoking += Poke.HairPoking;
	}
	TestTrue(TEXT("역검증: 통째 머리카락 + 줄인 모자면 머리카락 뚫음이 잡힌다"), ReversePoking > 0);
	Equipment->HatHairTuckCm = 1.f;
	Equipment->SetCosmetic(EEquipSlot::Head, NAME_None);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenHatNoPenetrationNpcTest, "Snarehaven.Hat.Visual.NoHeadPenetrationNpc",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// INBOX 결함 10-20 00:10 ⑤ — 같은 정점 시험을 상인 5종(잡화·낚시·농사·옷·고구마) + 의뢰인이 실제로 쓴 모자·머리카락으로 돌린다.
bool FSnarehavenHatNoPenetrationNpcTest::RunTest(const FString& Parameters)
{
	using namespace SnareHatTest;
	FRig Rig;
	if (!TestNotNull(TEXT("월드"), Rig.World))
	{
		return false;
	}
	TArray<AShopVendorActor*> Npcs;
	for (const EShopKind Kind : {EShopKind::General, EShopKind::Fishing, EShopKind::Farming, EShopKind::Clothes, EShopKind::SweetPotato})
	{
		AShopVendorActor* const Vendor = Rig.World->SpawnActor<AShopVendorActor>(FVector(Npcs.Num() * 500.f, 3000.f, 0.f), FRotator::ZeroRotator);
		if (TestNotNull(TEXT("상인"), Vendor))
		{
			Vendor->SetShopKind(Kind);
			Npcs.Add(Vendor);
		}
	}
	if (AQuestGiverActor* const Giver = Rig.World->SpawnActor<AQuestGiverActor>(FVector(-500.f, 3000.f, 0.f), FRotator::ZeroRotator))
	{
		Npcs.Add(Giver);
	}
	TestEqual(TEXT("상인 5종 + 의뢰인 = NPC 6"), Npcs.Num(), 6);

	TArray<FVector> HeadRef;
	FVector CenterRef;
	for (AShopVendorActor* const Npc : Npcs)
	{
		Npc->DispatchBeginPlay();
		const FString Name = Npc->GetVendorName().ToString();
		USkeletalMeshComponent* const Body = Npc->GetBody();
		const UStaticMeshComponent* const Hat = Npc->GetEquipment()->GetEquippedMesh(EEquipSlot::Head);
		if (!TestTrue(FString::Printf(TEXT("[%s] 몸·모자가 있다"), *Name), Body != nullptr && Body->GetSkeletalMeshAsset() != nullptr
			&& Hat != nullptr && Hat->GetStaticMesh() != nullptr))
		{
			continue;
		}
		if (HeadRef.Num() == 0)
		{
			GatherHeadRef(*Body->GetSkeletalMeshAsset(), HeadRef, CenterRef);
		}
		// 상인 머리카락도 플레이어와 같은 장비 컴포넌트 길 — 덮는 모자 밑으로는 자르는 높이 아래만 잰다.
		const FPoke Poke = MeasurePoking(*Body, Hat->GetAttachSocketName(), *Hat, Npc->GetHairMesh(), Npc->GetEquipment()->GetHairTopZ(), HeadRef, CenterRef);
		AddInfo(FString::Printf(TEXT("[%s] 모자 %s · 덮는 머리 점 %d · 나온 점 %d · 가장 많이 %.2fcm · 머리카락 %s · 덮는 머리카락 점 %d · 나온 점 %d"),
			*Name, *Hat->GetStaticMesh()->GetName(), Poke.Covered, Poke.Poking, Poke.Worst,
			Npc->GetHairMesh() != nullptr && Npc->GetHairMesh()->IsVisible() ? TEXT("보임") : TEXT("숨김"), Poke.HairCovered, Poke.HairPoking));
		TestTrue(FString::Printf(TEXT("[%s] 모자가 머리 꼭짓점 일부는 덮는다 (%d)"), *Name, Poke.Covered), Poke.Covered > 20);
		TestEqual(FString::Printf(TEXT("[%s] 머리 살이 모자를 뚫고 나온 점 0 (가장 많이 %.2fcm)"), *Name, Poke.Worst), Poke.Poking, 0);
		TestEqual(FString::Printf(TEXT("[%s] 보이는 머리카락이 모자를 뚫고 나온 점 0 (가장 많이 %.2fcm)"), *Name, Poke.HairWorst), Poke.HairPoking, 0);
	}
	return true;
}
#endif // WITH_EDITORONLY_DATA

#if WITH_EDITOR
namespace SnarehavenHairFaceTest
{
	bool FindEyePoints(const FMeshDescription& Body, FVector OutEyes[2]);   // SnarehavenPlayerHairFaceTest.cpp — 눈 앞면 자리를 같이 쓴다
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenHatAboveEyesTest, "Snarehaven.Hat.Visual.BrimAboveEyes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// INBOX 결함 10-20 00:10 ② · DESIGN 3.5 「모자 아랫단」 — 모자 행마다 두 눈 윗끝에서 얼굴 앞으로 쏜 선이 모자 삼각형에 안 걸린다
// (= 얼굴 앞 아랫단이 눈보다 위). 모자 꼭짓점을 머리 뼈 기준 참조 자세(몸 메시 cm)로 옮겨 잰다 — 경계상자 아님.
bool FSnarehavenHatAboveEyesTest::RunTest(const FString& Parameters)
{
	using namespace SnareHatTest;
	FRig Rig;
	const UDataTable* ItemTable = LoadObject<UDataTable>(nullptr, HatItemTablePath);
	if (!TestNotNull(TEXT("사람 몸을 올린 플레이어"), Rig.Player) || !TestNotNull(TEXT("아이템 표"), ItemTable))
	{
		return false;
	}
	UEquipmentComponent* const Equipment = Rig.Player->GetEquipment();
	USkeletalMeshComponent* const Body = Rig.Player->GetMesh();
	const USkeletalMesh* const BodyAsset = Body->GetSkeletalMeshAsset();
	const FMeshDescription* const BodyDesc = BodyAsset != nullptr ? BodyAsset->GetMeshDescription(0) : nullptr;
	FVector Eyes[2];
	if (!TestNotNull(TEXT("몸 메시 원본 설명"), BodyDesc)
		|| !TestTrue(TEXT("몸 메시 앞면에서 두 눈 자리를 찾았다"), SnarehavenHairFaceTest::FindEyePoints(*BodyDesc, Eyes)))
	{
		return false;
	}
	// 눈 윗끝 높이 = 얼굴 그림(`make_player_face_texture.py`)의 머리 공 가운데 76.38 + (눈 가운데 v −0.04 + 반 높이 0.30) × 반지름 23.90
	// (몸 메시 cm, `head_frame()` 실측). 그림을 옮기면 같이 옮긴다.
	constexpr double EyeTopZ = 76.38 + (-0.04 + 0.30) * 23.90;
	constexpr double RayBackCm = 10.0;     // 눈 점이 피부 위라 머리 속에서부터 쏜다
	constexpr double RayLengthCm = 200.0;
	const FReferenceSkeleton& RefSkel = BodyAsset->GetRefSkeleton();
	const FName HeadBone = Body->GetSocketBoneName(TEXT("Socket_Head"));
	FTransform HeadRefPose = FTransform::Identity;
	for (int32 Bone = RefSkel.FindBoneIndex(HeadBone); Bone != INDEX_NONE; Bone = RefSkel.GetParentIndex(Bone))
	{
		HeadRefPose = HeadRefPose * RefSkel.GetRefBonePose()[Bone];
	}
	const FTransform HeadNow = Body->GetBoneTransform(HeadBone);
	const auto ToRef = [&](const FVector& World) { return HeadRefPose.TransformPosition(HeadNow.InverseTransformPosition(World)); };
	const double CrownRefZ = ToRef(Body->GetSocketLocation(TEXT("Socket_Head"))).Z;
	AddInfo(FString::Printf(TEXT("정수리 %.1f · 눈 윗끝 %.1f · 눈 앞면 %s (몸 메시 cm)"), CrownRefZ, EyeTopZ, *Eyes[0].ToCompactString()));

	int32 Checked = 0;
	for (const TPair<FName, uint8*>& Pair : ItemTable->GetRowMap())
	{
		const FItemRow& Row = *reinterpret_cast<const FItemRow*>(Pair.Value);
		if (Row.EquipSlot != EEquipSlot::Head)
		{
			continue;
		}
		const FString RowName = Pair.Key.ToString();
		const bool bCosmetic = Row.ItemType == EItemType::Cosmetic;
		Equipment->SetCosmetic(EEquipSlot::Head, NAME_None);
		const bool bWorn = bCosmetic ? Equipment->SetCosmetic(EEquipSlot::Head, Pair.Key) : Equipment->EquipItem(Pair.Key);
		const UStaticMeshComponent* const Visual = Equipment->GetEquippedMesh(EEquipSlot::Head);
		const FMeshDescription* const HatDesc = bWorn && Visual != nullptr && Visual->GetStaticMesh() != nullptr
			? Visual->GetStaticMesh()->GetMeshDescription(0) : nullptr;
		if (!TestNotNull(FString::Printf(TEXT("[%s] 머리에 든 모자 메시 원본 설명"), *RowName), HatDesc))
		{
			continue;
		}
		// 게임 빌드에서 올림 규칙이 꼭짓점을 읽으려면 CPU 접근이 켜져 있어야 한다(`tools/unreal/set_hat_cpu_access.py`).
		TestTrue(FString::Printf(TEXT("[%s] 모자 메시 CPU 접근 켜짐 (폰에서도 아랫단을 잰다)"), *RowName), Visual->GetStaticMesh()->bAllowCPUAccess);
		const TVertexAttributesConstRef<FVector3f> HatPos = HatDesc->GetVertexPositions();
		const FTransform HatToWorld = Visual->GetComponentTransform();
		TArray<FVector> RefPos;
		RefPos.SetNum(HatDesc->Vertices().GetArraySize());
		double FrontEdgeZ = UE_BIG_NUMBER;
		for (const FVertexID Vertex : HatDesc->Vertices().GetElementIDs())
		{
			const FVector P = ToRef(HatToWorld.TransformPosition(FVector(HatPos[Vertex])));
			RefPos[Vertex.GetValue()] = P;
			if (P.Y > Eyes[0].Y - RayBackCm && FMath::Abs(P.X) <= FMath::Abs(Eyes[0].X) + 5.0)
			{
				FrontEdgeZ = FMath::Min(FrontEdgeZ, P.Z);   // 얼굴 앞 두 눈 너비 안의 가장 낮은 모자 점
			}
		}
		const auto HitsHat = [&](const FVector& Start, const FVector& End)
		{
			for (const FTriangleID Tri : HatDesc->Triangles().GetElementIDs())
			{
				TArrayView<const FVertexID> V = HatDesc->GetTriangleVertices(Tri);
				FVector Hit, Normal;
				if (FMath::SegmentTriangleIntersection(Start, End, RefPos[V[0].GetValue()], RefPos[V[1].GetValue()], RefPos[V[2].GetValue()], Hit, Normal))
				{
					return true;
				}
			}
			return false;
		};
		int32 Blocked = 0;
		for (const FVector& Eye : Eyes)
		{
			const FVector Start(Eye.X, Eye.Y - RayBackCm, EyeTopZ);
			Blocked += HitsHat(Start, Start + FVector(0.0, RayLengthCm, 0.0)) ? 1 : 0;
		}
		++Checked;
		AddInfo(FString::Printf(TEXT("[%s] 얼굴 앞 아랫단 %.1f (정수리 아래 %.1f · 눈 윗끝보다 %+.1f) · 눈을 가린 쪽 %d"),
			*RowName, FrontEdgeZ, CrownRefZ - FrontEdgeZ, FrontEdgeZ - EyeTopZ, Blocked));
		TestEqual(FString::Printf(TEXT("[%s] 모자 아랫단이 두 눈보다 위 (가린 쪽 0)"), *RowName), Blocked, 0);
	}
	Equipment->SetCosmetic(EEquipSlot::Head, NAME_None);
	TestEqual(TEXT("모자 행 70개를 모두 쟀다"), Checked, 70);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenHatNoBareBackOfHeadTest, "Snarehaven.Hat.Visual.NoBareBackOfHead",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace SnareHatTest
{
	/**
	 * 머리 뒤 반을 바깥에서 머리 공으로 쏜 선 중 모자에도, 보이는 머리카락(머티리얼이 자른 `HairTopZ` 아래)에도 안 걸린 수 = 살색이 보이는 선.
	 * 모든 좌표는 머리카락 메시 공간(정수리 원점, 몸 메시 cm) — 머리 공 중심·반지름은 `make_player_hair_mesh.py` 와 같은 실측.
	 */
	int32 CountBareBackRays(const UEquipmentComponent& Equipment, FString& OutWhere)
	{
		const UStaticMeshComponent* const Hat = Equipment.GetEquippedMesh(EEquipSlot::Head);
		const UStaticMeshComponent* const Hair = Equipment.GetHairMesh();
		const FMeshDescription* const HatDesc = Hat && Hat->GetStaticMesh() ? Hat->GetStaticMesh()->GetMeshDescription(0) : nullptr;
		const FMeshDescription* const HairDesc = Hair && Hair->GetStaticMesh() ? Hair->GetStaticMesh()->GetMeshDescription(0) : nullptr;
		if (HatDesc == nullptr || HairDesc == nullptr)
		{
			return -1;
		}
		constexpr double HeadCenterZ = -22.9;   // 정수리 → 머리 공 중심
		constexpr double HeadRadius = 23.5;
		const FTransform HairToWorld = Hair->GetComponentTransform();
		const FTransform HatToHair = Hat->GetComponentTransform().GetRelativeTransform(HairToWorld);
		FVector Forward = HairToWorld.InverseTransformVectorNoScale(Hair->GetOwner()->GetActorForwardVector());
		Forward.Z = 0.0;
		Forward = Forward.GetSafeNormal();
		const FVector Side = FVector::CrossProduct(FVector::UpVector, Forward);

		const auto Gather = [](const FMeshDescription& Desc, const FTransform& ToHair, TArray<FVector>& Out)
		{
			const TVertexAttributesConstRef<FVector3f> Pos = Desc.GetVertexPositions();
			Out.SetNum(Desc.Vertices().GetArraySize());
			for (const FVertexID Vertex : Desc.Vertices().GetElementIDs())
			{
				Out[Vertex.GetValue()] = ToHair.TransformPosition(FVector(Pos[Vertex]));
			}
		};
		TArray<FVector> HatPos, HairPos;
		Gather(*HatDesc, HatToHair, HatPos);
		Gather(*HairDesc, FTransform::Identity, HairPos);
		const bool bHairVisible = Hair->IsVisible();
		const double TopZ = Equipment.GetHairTopZ();
		const auto Hits = [](const FMeshDescription& Desc, const TArray<FVector>& Pos, const FVector& A, const FVector& B, double MaxZ)
		{
			for (const FTriangleID Tri : Desc.Triangles().GetElementIDs())
			{
				TArrayView<const FVertexID> V = Desc.GetTriangleVertices(Tri);
				FVector Hit, Normal;
				if (FMath::SegmentTriangleIntersection(A, B, Pos[V[0].GetValue()], Pos[V[1].GetValue()], Pos[V[2].GetValue()], Hit, Normal)
					&& Hit.Z <= MaxZ)
				{
					return true;
				}
			}
			return false;
		};

		int32 Bare = 0;
		// 머리카락 메시 꼭짓점 줄(둘레 6° 간격, 정옆·정뒤 포함)을 정확히 지나는 선은 삼각형 모서리라 교차가 안 잡힌다 — 줄 사이로 쏜다.
		for (int32 AroundDeg = 93; AroundDeg <= 267; AroundDeg += 14)
		{
			const double Around = FMath::DegreesToRadians(static_cast<double>(AroundDeg));
			const FVector Flat = Forward * FMath::Cos(Around) + Side * FMath::Sin(Around);
			// 머리카락 끝 극각(옆 96° → 뒤 128°, `make_player_hair_mesh.edge_polar_deg`)보다 10° 위까지 — 끝 밑은 원래 맨살(목)이다
			const double EdgeDeg = 96.0 + 32.0 * FMath::Max(0.0, -FMath::Cos(Around)) - 10.0;
			for (double PolarDeg = 11.0; PolarDeg <= EdgeDeg; PolarDeg += 8.0)
			{
				const double Polar = FMath::DegreesToRadians(PolarDeg);
				const FVector Dir = Flat * FMath::Sin(Polar) + FVector::UpVector * FMath::Cos(Polar);
				const FVector Skin = FVector(0.0, 0.0, HeadCenterZ) + Dir * HeadRadius * 0.98;
				const FVector Outside = Skin + Dir * 80.0;
				if (!Hits(*HatDesc, HatPos, Outside, Skin, UE_BIG_NUMBER)
					&& !(bHairVisible && Hits(*HairDesc, HairPos, Outside, Skin, TopZ)))
				{
					if (Bare == 0)
					{
						OutWhere = FString::Printf(TEXT("둘레 %d° · 극각 %.0f° (높이 %.1f)"), AroundDeg, PolarDeg, Skin.Z);
					}
					++Bare;
				}
			}
		}
		return Bare;
	}
}

// INBOX 결함 10-20 00:10 · DESIGN 3.5 「모자 밑 머리카락」 — 덮는 모자를 써도 뒤통수·옆머리에 살색이 안 보인다:
// 모자 아랫단 위는 모자가, 아래는 남긴 머리카락이 덮는다. 머리 행 70개 전부 + 역검증(시험 값으로 머리카락을 아랫단보다 한참 아래에서 자르면 틈).
bool FSnarehavenHatNoBareBackOfHeadTest::RunTest(const FString& Parameters)
{
	using namespace SnareHatTest;
	FRig Rig;
	const UDataTable* ItemTable = LoadObject<UDataTable>(nullptr, HatItemTablePath);
	if (!TestNotNull(TEXT("사람 몸을 올린 플레이어"), Rig.Player) || !TestNotNull(TEXT("아이템 표"), ItemTable))
	{
		return false;
	}
	UEquipmentComponent* const Equipment = Rig.Player->GetEquipment();
	Equipment->RefreshEquippedVisuals();
	int32 Checked = 0;
	int32 UnderHat = 0;
	for (const TPair<FName, uint8*>& Pair : ItemTable->GetRowMap())
	{
		const FItemRow& Row = *reinterpret_cast<const FItemRow*>(Pair.Value);
		if (Row.EquipSlot != EEquipSlot::Head)
		{
			continue;
		}
		const FString RowName = Pair.Key.ToString();
		Equipment->SetCosmetic(EEquipSlot::Head, NAME_None);
		const bool bWorn = Row.ItemType == EItemType::Cosmetic ? Equipment->SetCosmetic(EEquipSlot::Head, Pair.Key) : Equipment->EquipItem(Pair.Key);
		if (!TestTrue(FString::Printf(TEXT("[%s] 머리에 썼다"), *RowName), bWorn))
		{
			continue;
		}
		FString Where;
		const int32 Bare = CountBareBackRays(*Equipment, Where);
		++Checked;
		UnderHat += Equipment->IsHairUnderHat() ? 1 : 0;
		AddInfo(FString::Printf(TEXT("[%s] 머리카락 %s · 자르는 높이 %.1f · 살색 선 %d %s"), *RowName,
			Equipment->IsHairShown() ? TEXT("통째") : Equipment->IsHairUnderHat() ? TEXT("아랫단 밑") : TEXT("숨김"),
			Equipment->IsHairUnderHat() ? Equipment->GetHairTopZ() : 0.f, Bare, *Where));
		TestEqual(FString::Printf(TEXT("[%s] 뒤통수·옆머리에 살색 선 0"), *RowName), Bare, 0);
		TestTrue(FString::Printf(TEXT("[%s] 머리카락이 통째로든 아랫단 밑이든 보인다"), *RowName),
			Equipment->IsHairShown() || Equipment->IsHairUnderHat());
	}
	TestEqual(TEXT("모자 행 70개를 모두 쟀다"), Checked, 70);
	TestEqual(TEXT("덮는 모자 69개는 머리카락을 아랫단 밑만 남긴다 (왕관 하나는 통째)"), UnderHat, 69);

	// 역검증 — 시험 값: 머리카락을 아랫단보다 30cm 아래에서 자르면 작은 모자(페즈) 뒤통수에 살색 선이 생겨야 시험이 살아 있다.
	Equipment->HatHairTuckCm = -30.f;
	Equipment->SetCosmetic(EEquipSlot::Head, NAME_None);
	if (TestTrue(TEXT("역검증: 페즈를 썼다"), Equipment->SetCosmeticByItemID(EEquipSlot::Head, FName(TEXT("item.harbor_fez")))))
	{
		FString Where;
		const int32 Bare = CountBareBackRays(*Equipment, Where);
		AddInfo(FString::Printf(TEXT("역검증 페즈 · 자르는 높이 %.1f · 살색 선 %d %s"), Equipment->GetHairTopZ(), Bare, *Where));
		TestTrue(TEXT("역검증: 머리카락을 아랫단 한참 아래서 자르면 살색 선이 생긴다"), Bare > 0);
	}
	Equipment->HatHairTuckCm = 1.f;
	Equipment->SetCosmetic(EEquipSlot::Head, NAME_None);
	return true;
}
#endif // WITH_EDITOR

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenHatRejectRestoresTest, "Snarehaven.Hat.Reject.RestoresVisual",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenHatRejectRestoresTest::RunTest(const FString& Parameters)
{
	using namespace SnareHatTest;
	using FDone = USnareBackendSubsystem::FServerCallDone;
	TArray<FDone> Waiting;
	FString LastFunction;
	USnareBackendSubsystem::TestServerOverride = [&](const FString& Name, const TSharedPtr<FJsonObject>&, FDone OnDone)
		{
			LastFunction = Name;
			Waiting.Add(MoveTemp(OnDone));
		};
	ON_SCOPE_EXIT { USnareBackendSubsystem::TestServerOverride = nullptr; };
	const auto Reject = [&Waiting]()
		{
			const TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
			Response->SetStringField(TEXT("code"), TEXT("not_owned"));
			Response->SetStringField(TEXT("error"), TEXT("가방에 없다"));
			if (!Waiting.IsEmpty())
			{
				FDone Done = MoveTemp(Waiting[0]);
				Waiting.RemoveAt(0);
				Done(true, Response, FString());
			}
		};

	FRig Rig;
	if (!TestNotNull(TEXT("사람 몸을 올린 플레이어"), Rig.Player))
	{
		return false;
	}
	UEquipmentComponent* const Equipment = Rig.Player->GetEquipment();
	UBackpackComponent* const Backpack = Rig.Player->FindComponentByClass<UBackpackComponent>();
	if (!TestNotNull(TEXT("배낭 컴포넌트"), Backpack))
	{
		return false;
	}
	Backpack->BindTargets(Rig.Backend, nullptr);
	FSnarePlayerSnapshot Snapshot;
	Snapshot.PlayFabId = TEXT("PFTESTOWNER");
	Snapshot.FetchedAtUtc = FDateTime::UtcNow();
	Snapshot.InventorySlots = { MakeSlot(MerchantHatID, 1), MakeSlot(FarmerHatID, 1) };
	Rig.Backend->ApplyPlayerSnapshot(Snapshot, false);

	UBackpackScreenWidget* const Screen = CreateWidget<UBackpackScreenWidget>(Rig.World, UBackpackScreenWidget::StaticClass());
	if (!TestNotNull(TEXT("배낭 화면"), Screen))
	{
		return false;
	}
	const TSharedRef<SWidget> Slate = Screen->TakeWidget();
	Screen->BindTargets(Rig.Backend, Backpack, Equipment);
	Screen->SetPanelOpen(true);
	const auto PressOn = [&](FName ItemID)
		{
			Screen->SelectSlot(Screen->GetShownSlots().IndexOfByPredicate(
				[ItemID](const FInventorySlot& Slot) { return Slot.ItemID == ItemID; }));
			Screen->PressEquip();
		};
	const auto HeadMeshShown = [Equipment]()
		{
			const UStaticMeshComponent* const Visual = Equipment->GetEquippedMesh(EEquipSlot::Head);
			return Visual != nullptr && Visual->GetStaticMesh() != nullptr;
		};
	const FName MerchantRow = Equipment->FindEquipmentRowByItemID(MerchantHatID);

	const auto HeadMeshName = [Equipment]()
		{
			const UStaticMeshComponent* const Visual = Equipment->GetEquippedMesh(EEquipSlot::Head);
			return (Visual != nullptr) ? GetNameSafe(Visual->GetStaticMesh()) : FString();
		};

	// ① 아무것도 안 쓴 채 거절 — 답 전엔 겉모습이 붙고, 거절 뒤 머리는 기본 겉모습(M15-10 맨머리)으로 돌아온다.
	PressOn(MerchantHatID);
	TestEqual(TEXT("부른 함수 = EquipHat"), LastFunction, FString(TEXT("EquipHat")));
	TestTrue(TEXT("답 전 겉모습이 붙었다"), HeadMeshShown());
	Reject();
	TestTrue(TEXT("거절 뒤 쓴 모자 기록 없음"), Backpack->GetWornHatID().IsNone());
	TestTrue(TEXT("거절 뒤 머리 부위 비었다"), Equipment->GetEquippedItem(EEquipSlot::Head).IsNone());
	// M15-10: 기본 겉모습에서 모자를 뺐다 (DESIGN 3.5) — 거절 뒤 머리에는 아무 메시도 안 남는다.
	TestFalse(FString::Printf(TEXT("거절 뒤 머리 = 기본 겉모습 맨머리 (DESIGN 3.5) · 지금 %s"), *HeadMeshName()), HeadMeshShown());
	AddInfo(FString::Printf(TEXT("[Hat] 거절 뒤 머리 %s · 메시 %d"),
		*Equipment->GetEquippedItem(EEquipSlot::Head).ToString(), HeadMeshShown() ? 1 : 0));

	// ② 상인 모자를 쓴 채 농부 모자 거절 — 상인 모자로 돌아온다.
	Backpack->ApplyBackendHat(MerchantHatID);
	PressOn(FarmerHatID);
	TestEqual(TEXT("답 전엔 농부 모자 겉모습"), Equipment->GetEquippedItem(EEquipSlot::Head),
		Equipment->FindEquipmentRowByItemID(FarmerHatID));
	Reject();
	TestEqual(TEXT("거절 뒤 쓴 모자 = 상인 모자"), Backpack->GetWornHatID(), MerchantHatID);
	TestEqual(TEXT("거절 뒤 머리 = 상인 모자 행"), Equipment->GetEquippedItem(EEquipSlot::Head), MerchantRow);
	TestTrue(TEXT("거절 뒤 머리 메시 있음"), HeadMeshShown());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenMiningHatHeadLightTest, "Snarehaven.Hat.MiningHat.HeadLight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// DESIGN 8.16·13.5 채굴 모자 — 쓰면 머리 앞 점광이 켜지고(표 값 그대로), 다른 모자·벗기·복제 목록 갱신이면 꺼진다.
bool FSnarehavenMiningHatHeadLightTest::RunTest(const FString& Parameters)
{
	using namespace SnareHatTest;
	FRig Rig;
	if (!TestNotNull(TEXT("사람 몸을 올린 플레이어"), Rig.Player))
	{
		return false;
	}
	UEquipmentComponent* const Equipment = Rig.Player->GetEquipment();
	const auto LightOn = [Equipment]()
		{
			const USpotLightComponent* const Light = Equipment->GetHeadLight();
			return Light != nullptr && Light->IsVisible() && Light->Intensity > 0.f;
		};

	TestFalse(TEXT("처음(기본 사냥 모자)엔 불빛 없음"), LightOn());
	if (!TestTrue(TEXT("채굴 모자를 머리에 들었다"), Equipment->EquipItemByItemID(TEXT("item.mining_hat"))))
	{
		return false;
	}
	const USpotLightComponent* const Light = Equipment->GetHeadLight();
	TestTrue(TEXT("채굴 모자 → 불빛 켜짐"), LightOn());
	if (Light != nullptr)
	{
		AddInfo(FString::Printf(TEXT("불빛 %.0fcd · 반경 %.0fcm · 몸 기준 %s · 움직임 %d"), Light->Intensity,
			Light->AttenuationRadius, *Light->GetRelativeLocation().ToString(), static_cast<int32>(Light->Mobility)));
		TestEqual(TEXT("밝기 = 표 200cd"), Light->Intensity, 200.f);
		TestEqual(TEXT("반경 = 표 900cm"), Light->AttenuationRadius, 900.f);
		TestTrue(TEXT("움직이는 빛(캐릭터를 따라간다)"), Light->Mobility == EComponentMobility::Movable);
		TestTrue(TEXT("캐릭터 루트에 붙었다"), Light->GetAttachParent() == Rig.Player->GetRootComponent());

		// 전수 착용 #1357 `lh1357_Head_05` — 쓴 사람 얼굴·몸이 하얗게 날아갔다. 쓴 사람 몸(참조 자세)·머리카락
		// 꼭짓점 중 원뿔(바깥 반각)·반경 안에 드는 것이 0 이어야 한다. 역검증: 같은 원뿔을 옛 점광 자리(앞 40 · 위 70)에 두면 걸린다.
		USkeletalMeshComponent* const Body = Rig.Player->GetMesh();
		TArray<FVector> Wearer;
		if (const FMeshDescription* const BodyDesc = Body->GetSkeletalMeshAsset()->GetMeshDescription(0))
		{
			const TVertexAttributesConstRef<FVector3f> Pos = BodyDesc->GetVertexPositions();
			for (const FVertexID Vertex : BodyDesc->Vertices().GetElementIDs())
			{
				Wearer.Add(Body->GetComponentTransform().TransformPosition(FVector(Pos[Vertex])));
			}
		}
		const UStaticMeshComponent* const Hair = Equipment->GetHairMesh();
		if (Hair != nullptr && Hair->GetStaticMesh() != nullptr)
		{
			if (const FMeshDescription* const HairDesc = Hair->GetStaticMesh()->GetMeshDescription(0))
			{
				const TVertexAttributesConstRef<FVector3f> Pos = HairDesc->GetVertexPositions();
				for (const FVertexID Vertex : HairDesc->Vertices().GetElementIDs())
				{
					Wearer.Add(Hair->GetComponentTransform().TransformPosition(FVector(Pos[Vertex])));
				}
			}
		}
		const FVector Axis = Light->GetForwardVector();
		const double CosOuter = FMath::Cos(FMath::DegreesToRadians(Light->OuterConeAngle));
		const auto CountLit = [&Wearer, &Axis, CosOuter, Light](const FVector& Lamp)
			{
				int32 Lit = 0;
				for (const FVector& P : Wearer)
				{
					const FVector ToP = P - Lamp;
					Lit += (ToP.Size() < Light->AttenuationRadius && FVector::DotProduct(ToP.GetSafeNormal(), Axis) > CosOuter) ? 1 : 0;
				}
				return Lit;
			};
		const FVector OldLamp = Rig.Player->GetRootComponent()->GetComponentTransform().TransformPosition(FVector(40.0, 0.0, 70.0));
		const int32 Lit = CountLit(Light->GetComponentLocation());
		const int32 OldLit = CountLit(OldLamp);
		AddInfo(FString::Printf(TEXT("원뿔빛 반각 %.0f°(안 %.0f°) · 내리비춤 %.0f° · 쓴 사람 꼭짓점 %d 중 원뿔 안 %d · 역검증 옛 자리 %d"),
			Light->OuterConeAngle, Light->InnerConeAngle, -Light->GetRelativeRotation().Pitch, Wearer.Num(), Lit, OldLit));
		TestTrue(TEXT("쓴 사람 꼭짓점을 모았다"), Wearer.Num() > 1000);
		TestEqual(TEXT("쓴 사람 몸·머리카락은 원뿔빛 밖 (얼굴 날아감 0)"), Lit, 0);
		TestTrue(TEXT("역검증: 옛 자리 원뿔이면 쓴 사람이 비친다"), OldLit > 0);
		TestTrue(TEXT("앞을 비춘다 (원뿔 축이 몸 앞쪽)"), FVector::DotProduct(Axis, Body->GetRightVector()) > 0.5);
		TestTrue(TEXT("아래로 비춘다"), Axis.Z < 0.0);
	}

	TestTrue(TEXT("달리기 모자로 갈아 씀"), Equipment->EquipItemByItemID(TEXT("item.runner_hat")));
	TestFalse(TEXT("다른 모자 → 불빛 꺼짐"), LightOn());
	Equipment->EquipItemByItemID(TEXT("item.mining_hat"));
	TestTrue(TEXT("다시 채굴 모자 → 같은 불빛이 다시 켜짐"), LightOn() && Equipment->GetHeadLight() == Light);
	Equipment->UnequipSlot(EEquipSlot::Head);
	TestFalse(TEXT("벗기 → 불빛 꺼짐"), LightOn());

	// 게스트 길 — 복제 목록으로만 모자를 받는다(`ApplyEquippedSlots` → `RefreshEquippedVisuals`).
	FEquippedSlot Worn;
	Worn.ItemRowName = Equipment->FindEquipmentRowByItemID(TEXT("item.mining_hat"));
	Worn.Slot = EEquipSlot::Head;
	Worn.SocketName = TEXT("Socket_Head");
	Equipment->ApplyEquippedSlots({ Worn });
	TestTrue(TEXT("복제 목록에 채굴 모자 → 불빛 켜짐"), LightOn());
	Equipment->ApplyEquippedSlots({});
	TestFalse(TEXT("복제 목록 비움 → 불빛 꺼짐"), LightOn());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenDefaultHairTest, "Snarehaven.Hat.DefaultHair.ShownOnlyWhenBareHead",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// M19-01 · DESIGN 3.5 「기본 겉모습」 — 모자 칸이 비면 정수리에 머리카락 덩어리가 보이고, 모자를 쓰면 숨고, 벗으면 돌아온다.
// 호스트 길(`EquipItem`·`UnequipSlot`)과 게스트 길(복제 목록 `ApplyEquippedSlots`) 둘 다 본다. 장비 목록엔 안 들어간다.
bool FSnarehavenDefaultHairTest::RunTest(const FString& Parameters)
{
	using namespace SnareHatTest;
	FRig Rig;
	if (!TestNotNull(TEXT("사람 몸을 올린 플레이어"), Rig.Player))
	{
		return false;
	}
	UEquipmentComponent* const Equipment = Rig.Player->GetEquipment();
	USkeletalMeshComponent* const Body = Rig.Player->GetMesh();
	Equipment->RefreshEquippedVisuals();   // `BeginPlay` 의 몸 올린 직후 길

	const UStaticMeshComponent* const Hair = Equipment->GetHairMesh();
	if (!TestNotNull(TEXT("머리카락 컴포넌트가 붙었다"), Hair))
	{
		return false;
	}
	TestTrue(TEXT("민머리 → 머리카락 보임"), Equipment->IsHairShown());
	TestTrue(TEXT("머리카락은 장비 목록에 없다 (머리 칸 None)"), Equipment->GetEquippedItem(EEquipSlot::Head).IsNone());
	TestEqual(TEXT("정수리 소켓에 붙었다"), Hair->GetAttachSocketName(), FName(TEXT("Socket_Head")));
	TestTrue(TEXT("몸에 붙었다"), Hair->GetAttachParent() == Body);

	// 머리 공을 덮는가 — 월드 폭 = 메시 폭 × 몸 배율(소켓 배율 0.3 을 안 받는다), 꼭대기는 정수리 위.
	const FVector Size = Hair->CalcBounds(Hair->GetComponentTransform()).BoxExtent * 2.f;
	const double WantWidth = Hair->GetStaticMesh()->GetBounds().BoxExtent.X * 2.0 * Body->GetComponentScale().X;
	const double CrownZ = Body->GetSocketLocation(TEXT("Socket_Head")).Z;
	const double HairTopZ = Hair->Bounds.Origin.Z + Hair->Bounds.BoxExtent.Z;
	AddInfo(FString::Printf(TEXT("머리카락 월드 %.1f×%.1f×%.1fcm · 기대 폭 %.1f · 꼭대기 %.1f · 정수리 %.1f"),
		Size.X, Size.Y, Size.Z, WantWidth, HairTopZ, CrownZ));
	TestTrue(TEXT("월드 폭 = 메시 폭 × 몸 배율 (±3cm)"),
		FMath::IsNearlyEqual(FMath::Max(Size.X, Size.Y), WantWidth, SizeToleranceCm));
	TestTrue(TEXT("꼭대기가 정수리 위 0~10cm (공을 덮는다)"), HairTopZ >= CrownZ && HairTopZ <= CrownZ + 10.0);

	if (!TestTrue(TEXT("농부 모자를 머리에 들었다"), Equipment->EquipItemByItemID(FarmerHatID)))
	{
		return false;
	}
	TestFalse(TEXT("모자 씀 → 머리카락 통째로는 안 보임"), Equipment->IsHairShown());
	TestTrue(TEXT("모자 씀 → 머리카락은 아랫단 밑만 보임"), Equipment->IsHairUnderHat());
	Equipment->UnequipSlot(EEquipSlot::Head);
	TestTrue(TEXT("모자 벗음 → 머리카락 다시 보임"), Equipment->IsHairShown());
	TestTrue(TEXT("같은 컴포넌트를 돌려쓴다"), Equipment->GetHairMesh() == Hair);

	// 게스트 길 — 복제 목록으로만 모자를 받는다.
	FEquippedSlot Worn;
	Worn.ItemRowName = Equipment->FindEquipmentRowByItemID(FarmerHatID);
	Worn.Slot = EEquipSlot::Head;
	Worn.SocketName = TEXT("Socket_Head");
	Equipment->ApplyEquippedSlots({ Worn });
	TestFalse(TEXT("복제 목록에 모자 → 머리카락 통째로는 안 보임"), Equipment->IsHairShown());
	TestTrue(TEXT("복제 목록에 모자 → 아랫단 밑만 보임"), Equipment->IsHairUnderHat());
	Equipment->ApplyEquippedSlots({});
	TestTrue(TEXT("복제 목록 비움 → 머리카락 보임"), Equipment->IsHairShown());

	// DESIGN 3.5 「모자 쓸 때 머리카락」 — 머리를 덮지 않는 장식(행 `bHatKeepsHair`, 왕관)은 써도 머리카락을 남긴다.
	// 농부 모자(덮는 모자) 다음에 써서 앞 행의 숨김이 남지 않는지도 같이 본다.
	if (!TestTrue(TEXT("농부 모자를 다시 들었다"), Equipment->EquipItemByItemID(FarmerHatID)))
	{
		return false;
	}
	if (!TestTrue(TEXT("황금 왕관을 꾸미기로 썼다"), Equipment->SetCosmeticByItemID(EEquipSlot::Head, CrownHatID)))
	{
		return false;
	}
	TestTrue(TEXT("왕관 씀 → 머리카락 그대로 보임 (고리 안 맨살 정수리 없음)"), Equipment->IsHairShown());
	TestFalse(TEXT("왕관 씀 → 자르지 않는다"), Equipment->IsHairUnderHat());
	TestFalse(TEXT("왕관은 머리카락을 숨기는 모자가 아니다"), Equipment->IsHairHiddenByHat());
	Equipment->SetCosmetic(EEquipSlot::Head, NAME_None);
	TestFalse(TEXT("왕관 벗음 → 밑의 농부 모자가 다시 머리카락을 아랫단 밑만 남김"), Equipment->IsHairShown());
	TestTrue(TEXT("왕관 벗음 → 아랫단 밑 머리카락"), Equipment->IsHairUnderHat());
	Equipment->UnequipSlot(EEquipSlot::Head);
	TestTrue(TEXT("농부 모자 벗음 → 머리카락 보임"), Equipment->IsHairShown());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenHatKeepsHairTableTest, "Snarehaven.Hat.Table.KeepsHairOnlyOnOpenTop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// DESIGN 3.5 「모자 쓸 때 머리카락」 — `bHatKeepsHair` 는 머리 행에만, 지금은 왕관 하나만 참이다
// (새 고리형 장식을 넣을 땐 표와 이 목록을 같이 고친다 — 덮는 모자에 잘못 켜면 머리카락이 모자를 뚫는다).
bool FSnarehavenHatKeepsHairTableTest::RunTest(const FString& Parameters)
{
	const UDataTable* ItemTable = LoadObject<UDataTable>(nullptr, HatItemTablePath);
	if (!TestNotNull(TEXT("아이템 표"), ItemTable))
	{
		return false;
	}
	TArray<FString> Keeps;
	int32 HatRows = 0;
	for (const TPair<FName, uint8*>& Pair : ItemTable->GetRowMap())
	{
		const FItemRow& Row = *reinterpret_cast<const FItemRow*>(Pair.Value);
		if (!Row.bHatKeepsHair)
		{
			HatRows += Row.EquipSlot == EEquipSlot::Head ? 1 : 0;
			continue;
		}
		++HatRows;
		Keeps.Add(Row.ItemID.ToString());
		TestEqual(FString::Printf(TEXT("[%s] 머리카락 유지 칸은 머리 행에만"), *Pair.Key.ToString()), Row.EquipSlot, EEquipSlot::Head);
	}
	AddInfo(FString::Printf(TEXT("머리 행 %d개 중 머리카락 유지 %d개: %s"), HatRows, Keeps.Num(), *FString::Join(Keeps, TEXT(", "))));
	TestEqual(TEXT("머리카락 유지 행 = 왕관 하나"), Keeps, TArray<FString>{ SnareHatTest::CrownHatID.ToString() });
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenFaceStrapHugsHeadTest, "Snarehaven.Hat.Visual.FaceStrapHugsHead",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// 루프#1358 전수 착용 표 「얼굴 장식 끈 뜸 9종」 — 얼굴 장식 행마다 메시 꼭짓점을 머리 뼈 참조 자세로 옮겨
// 머리 가운데에서 그 방향의 실제 머리 겉(몸 메시 머리 살과 모자 없을 때 보이는 머리카락 껍질 꼭짓점 중 바깥)보다 얼마나 바깥인지 잰다.
// 얼굴 앞면(눈·코 장식)은 빼고 귀 쪽부터 뒤(끈·안경다리)만 본다 — 끈이 머리를 감싸야지 허공에 뜨면 안 된다.
// 머리 겉이 없는 방향(목 아래로 늘어진 두건 매듭 끈)은 따로 센다. 역검증은 끈을 1.15배 키워 같은 단정이 실패하는지 본다.
bool FSnarehavenFaceStrapHugsHeadTest::RunTest(const FString& Parameters)
{
	using namespace SnareHatTest;
	FRig Rig;
	const UDataTable* ItemTable = LoadObject<UDataTable>(nullptr, HatItemTablePath);
	if (!TestNotNull(TEXT("사람 몸을 올린 플레이어"), Rig.Player) || !TestNotNull(TEXT("아이템 표"), ItemTable))
	{
		return false;
	}
	UEquipmentComponent* const Equipment = Rig.Player->GetEquipment();
	USkeletalMeshComponent* const Body = Rig.Player->GetMesh();
	TArray<FVector> HeadRef;
	FVector CenterRef;
	if (!TestTrue(TEXT("머리 꼭짓점을 읽었다"), Body->GetSkeletalMeshAsset() != nullptr
		&& GatherHeadRef(*Body->GetSkeletalMeshAsset(), HeadRef, CenterRef) && HeadRef.Num() > 100))
	{
		return false;
	}
	// 머리 공 가운데 = 정수리 아래 22.9cm(`make_player_hair_mesh.py` 실측), 가로·앞뒤 가운데는 그 높이 머리 살의 가운데.
	double CrownZ = -UE_BIG_NUMBER;
	for (const FVector& P : HeadRef)
	{
		CrownZ = FMath::Max(CrownZ, P.Z);
	}
	FBox Equator(ForceInit);
	for (const FVector& P : HeadRef)
	{
		Equator += FMath::Abs(P.Z - (CrownZ - 22.9)) < 2.0 ? FBox(P, P) : FBox(ForceInit);
	}
	const FVector Center(Equator.GetCenter().X, Equator.GetCenter().Y, CrownZ - 22.9);
	const double FaceFrontY = Equator.Max.Y;
	AddInfo(FString::Printf(TEXT("머리 가운데 %s · 앞면 Y %.1f · 가로 %.1f · 앞뒤 %.1f (몸 메시 cm)"),
		*Center.ToCompactString(), FaceFrontY, Equator.GetSize().X, Equator.GetSize().Y));
	const FName HeadBone = Body->GetSocketBoneName(TEXT("Socket_Face"));
	const FReferenceSkeleton& RefSkel = Body->GetSkeletalMeshAsset()->GetRefSkeleton();
	FTransform HeadRefPose = FTransform::Identity;
	for (int32 Bone = RefSkel.FindBoneIndex(HeadBone); Bone != INDEX_NONE; Bone = RefSkel.GetParentIndex(Bone))
	{
		HeadRefPose = HeadRefPose * RefSkel.GetRefBonePose()[Bone];
	}
	const FTransform HeadNow = Body->GetBoneTransform(HeadBone);
	const auto ToRef = [&](const FVector& World) { return HeadRefPose.TransformPosition(HeadNow.InverseTransformPosition(World)); };

	// 머리 겉 = 머리 살 + 모자 없을 때 보이는 머리카락 껍질(끈은 옆·뒤에서 그 위로 지난다).
	Equipment->RefreshEquippedVisuals();
	TArray<FVector> Surface = HeadRef;
	const UStaticMeshComponent* const Hair = Equipment->GetHairMesh();
	const FMeshDescription* const HairDesc = Hair != nullptr && Hair->GetStaticMesh() != nullptr ? Hair->GetStaticMesh()->GetMeshDescription(0) : nullptr;
	if (!TestTrue(TEXT("모자 없는 머리 — 머리카락이 보인다"), Equipment->IsHairShown() && HairDesc != nullptr))
	{
		return false;
	}
	const TVertexAttributesConstRef<FVector3f> HairPos = HairDesc->GetVertexPositions();
	for (const FVertexID Vertex : HairDesc->Vertices().GetElementIDs())
	{
		Surface.Add(ToRef(Hair->GetComponentTransform().TransformPosition(FVector(HairPos[Vertex]))));
	}
	// 그 방향(±6°) 머리 겉 가장 바깥 거리 — 머리 겉이 없는 방향(목 아래)은 음수.
	const auto SkinRadius = [&](const FVector& Dir)
	{
		double Best = -1.0;
		for (const FVector& P : Surface)
		{
			const FVector D = P - Center;
			if (FVector::DotProduct(D.GetSafeNormal(), Dir) > 0.9945)
			{
				Best = FMath::Max(Best, D.Size());
			}
		}
		return Best;
	};

	// 행 하나를 입혀 귀 뒤 꼭짓점을 잰다. `Grow` 는 역검증용 시험 값(소켓 기준 배율).
	struct FFloat { int32 Back = 0; int32 Floating = 0; int32 NoSkin = 0; double Worst = 0.0; FVector WorstAt = FVector::ZeroVector; };
	const auto Measure = [&](const UStaticMeshComponent& Visual, const FMeshDescription& Desc, double Grow)
	{
		FFloat Out;
		Out.Worst = -UE_BIG_NUMBER;
		const TVertexAttributesConstRef<FVector3f> Pos = Desc.GetVertexPositions();
		const FTransform ToWorld = Visual.GetComponentTransform();
		for (const FVertexID Vertex : Desc.Vertices().GetElementIDs())
		{
			const FVector Ref = ToRef(ToWorld.TransformPosition(FVector(Pos[Vertex]) * Grow));
			if (Ref.Y > Center.Y)
			{
				continue;   // 귀보다 앞 — 얼굴 앞 장식
			}
			++Out.Back;
			const FVector D = Ref - Center;
			const double Skin = SkinRadius(D.GetSafeNormal());
			if (Skin < 0.0)
			{
				++Out.NoSkin;   // 목 아래로 늘어진 매듭 끈 — 감쌀 머리가 없다
				continue;
			}
			const double Gap = D.Size() - Skin;
			Out.Floating += Gap > FaceStrapFloatToleranceCm ? 1 : 0;
			if (Gap > Out.Worst)
			{
				Out.Worst = Gap;
				Out.WorstAt = D;
			}
		}
		return Out;
	};

	int32 Checked = 0;
	int32 Strapped = 0;
	for (const TPair<FName, uint8*>& Pair : ItemTable->GetRowMap())
	{
		const FItemRow& Row = *reinterpret_cast<const FItemRow*>(Pair.Value);
		if (Row.EquipSlot != EEquipSlot::FaceAccessory || Row.ItemType != EItemType::Cosmetic)
		{
			continue;
		}
		const FString RowName = Pair.Key.ToString();
		Equipment->SetCosmetic(EEquipSlot::FaceAccessory, NAME_None);
		const bool bWorn = Equipment->SetCosmetic(EEquipSlot::FaceAccessory, Pair.Key);
		const UStaticMeshComponent* const Visual = Equipment->GetEquippedMesh(EEquipSlot::FaceAccessory);
		const FMeshDescription* const Desc = Visual != nullptr && Visual->GetStaticMesh() != nullptr ? Visual->GetStaticMesh()->GetMeshDescription(0) : nullptr;
		if (!TestTrue(FString::Printf(TEXT("[%s] 얼굴에 들었다"), *RowName), bWorn && Desc != nullptr))
		{
			continue;
		}
		++Checked;
		const FFloat F = Measure(*Visual, *Desc, 1.0);
		Strapped += F.Back > 0 ? 1 : 0;
		AddInfo(FString::Printf(TEXT("[%s] 귀 뒤 꼭짓점 %d · 머리 겉 밖 %.1fcm 넘게 뜬 점 %d (머리 겉 없는 방향 %d) · 가장 많이 %.2fcm %s"),
			*RowName, F.Back, FaceStrapFloatToleranceCm, F.Floating, F.NoSkin, F.Back > 0 ? F.Worst : 0.0, F.Back > 0 ? *F.WorstAt.ToCompactString() : TEXT("")));
		TestEqual(FString::Printf(TEXT("[%s] 끈이 머리 겉에서 %.1fcm 넘게 뜬 꼭짓점 0 (가장 많이 %.2fcm)"), *RowName, FaceStrapFloatToleranceCm, F.Worst), F.Floating, 0);
	}
	TestEqual(TEXT("귀 뒤로 끈·안경다리가 도는 행 9개를 쟀다"), Strapped, 9);

	// 역검증 — 시험 값: 끈 꼭짓점을 소켓 기준 1.15배로 키우면(옛 네모 둘레가 모서리에서 뜨던 만큼) 뜬 점이 잡혀야 시험이 살아 있다.
	// 메시·표는 안 건드리고 재는 꼭짓점에만 배율을 곱한다.
	{
		const FName ReverseRow(TEXT("Item_HarborEyepatch"));
		Equipment->SetCosmetic(EEquipSlot::FaceAccessory, ReverseRow);
		const UStaticMeshComponent* const Visual = Equipment->GetEquippedMesh(EEquipSlot::FaceAccessory);
		const FMeshDescription* const Desc = Visual != nullptr && Visual->GetStaticMesh() != nullptr ? Visual->GetStaticMesh()->GetMeshDescription(0) : nullptr;
		if (TestNotNull(TEXT("역검증: 안대를 썼다"), Desc))
		{
			const FFloat F = Measure(*Visual, *Desc, 1.15);
			AddInfo(FString::Printf(TEXT("역검증 [%s] 끈 1.15배 · 뜬 점 %d · 가장 많이 %.2fcm"), *ReverseRow.ToString(), F.Floating, F.Worst));
			TestTrue(TEXT("역검증: 끈을 키우면 뜬 점이 잡힌다"), F.Floating > 0);
		}
	}
	Equipment->SetCosmetic(EEquipSlot::FaceAccessory, NAME_None);
	TestEqual(TEXT("꾸미기 얼굴 장식 행 14개를 모두 쟀다"), Checked, 14);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
