// 스킨 조각 g (`docs/specs/skin-parts.md` 4장 · 6장 g) 2층 검사 — 꾸미기 얼굴 장식 6벌이 진짜 표·진짜 메시로 얼굴에 붙는다.
//  ① 표에 얼굴 장식 꾸미기 행이 6개(일반 3·희귀 2·에픽 1), 메시는 얼굴 장식 폴더의 자기 메시·삼각형 ≤ 1,500(명세 4장).
//  ② `SetCosmetic` 으로 입으면 `Socket_Face` 에 붙고 몸 배율(소켓 배율)을 그대로 받는다 — 메시가 몸 공간 치수로 지어졌다.
//  ③ 받침점이 얼굴 앞면이고 앞이 몸 앞이다 — 장식의 가장 앞 점이 소켓 앞 0~12cm, 좌우 가운데가 소켓에서 3cm 안.
//     (앞뒤가 뒤집혀 들어오면 머리를 도는 끈이 얼굴 앞 85cm 까지 뻗어 ③ 이 깨진다.)
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.Item.Cosmetic; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/ItemRow.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Equipment/EquipmentComponent.h"
#include "SnarehavenCharacter.h"

namespace SnareFaceAccessoryTest
{
	const TCHAR* ItemTablePath = TEXT("/Game/Game/Data/Items/DT_ItemTable.DT_ItemTable");
	const FName FaceSocket(TEXT("Socket_Face"));

	// 검사 전용 값 — 명세 4장 「얼굴 장식 스태틱 1,500」 · 앞 점 허용 폭(안경 렌즈·코·잠수 마스크 테가 얼굴 앞 0~7cm 몸 공간,
	// 몸 배율 1.76 을 곱해 12cm 안) · 좌우 어긋남 3cm(대롱 한쪽으로 치우친 마스크도 몸 공간 1cm).
	constexpr int32 FaceTriangleLimit = 1500;
	constexpr float FrontMinCm = 0.f;
	constexpr float FrontMaxCm = 12.f;
	constexpr float LateralToleranceCm = 3.f;
	constexpr float ScaleTolerance = 0.01f;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenFaceAccessoryRowsTest, "Snarehaven.Item.Cosmetic.FaceAccessoryRows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenFaceAccessoryRowsTest::RunTest(const FString& Parameters)
{
	using namespace SnareFaceAccessoryTest;

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
		|| !TestTrue(TEXT("몸에 Socket_Face 소켓이 있다"), Body->DoesSocketExist(FaceSocket)))
	{
		return false;
	}
	TableProp->SetObjectPropertyValue_InContainer(Equipment, ItemTable);

	const FTransform Socket = Body->GetSocketTransform(FaceSocket, RTS_World);
	const FVector Forward = Player->GetActorForwardVector();
	const FVector Right = Player->GetActorRightVector();

	TMap<EItemGrade, int32> Grades;
	int32 FaceRows = 0;
	for (const TPair<FName, uint8*>& Pair : ItemTable->GetRowMap())
	{
		const FItemRow& Row = *reinterpret_cast<const FItemRow*>(Pair.Value);
		if (Row.EquipSlot != EEquipSlot::FaceAccessory)
		{
			continue;
		}
		++FaceRows;
		Grades.FindOrAdd(Row.Grade) += 1;
		const FString RowName = Pair.Key.ToString();

		// ① 행·메시
		TestEqual(FString::Printf(TEXT("[%s] 꾸미기 행"), *RowName), Row.ItemType, EItemType::Cosmetic);
		TestEqual(FString::Printf(TEXT("[%s] 소켓 = Socket_Face"), *RowName), Row.EquipSocketName, FaceSocket);
		TestTrue(FString::Printf(TEXT("[%s] 몸 배율을 따른다"), *RowName), Row.bEquipFollowsBodyScale);
		TestTrue(FString::Printf(TEXT("[%s] 얼굴 장식 폴더의 자기 메시: %s"), *RowName, *Row.EquipMeshRef.ToString()),
			Row.EquipMeshRef.ToString().StartsWith(TEXT("/Game/Game/Characters/Equipment/Faces/")));
		const UStaticMesh* const Mesh = Row.EquipMeshRef.LoadSynchronous();
		if (!TestNotNull(FString::Printf(TEXT("[%s] 메시를 실제로 읽는다"), *RowName), Mesh))
		{
			continue;
		}
		const int32 Triangles = Mesh->GetNumTriangles(0);
		TestTrue(FString::Printf(TEXT("[%s] 삼각형 %d ≤ %d (명세 4장)"), *RowName, Triangles, FaceTriangleLimit),
			Triangles > 0 && Triangles <= FaceTriangleLimit);

		// ② 입기 — 소켓에 붙고 소켓 배율 그대로
		Equipment->SetCosmetic(EEquipSlot::FaceAccessory, NAME_None);
		if (!TestTrue(FString::Printf(TEXT("[%s] 얼굴 장식을 입었다"), *RowName), Equipment->SetCosmetic(EEquipSlot::FaceAccessory, Pair.Key)))
		{
			continue;
		}
		const UStaticMeshComponent* const Visual = Equipment->GetEquippedMesh(EEquipSlot::FaceAccessory);
		if (!TestNotNull(FString::Printf(TEXT("[%s] 얼굴 장식 컴포넌트"), *RowName), Visual))
		{
			continue;
		}
		TestEqual(FString::Printf(TEXT("[%s] 보이는 메시 = 행 메시"), *RowName), Visual->GetStaticMesh().Get(), const_cast<UStaticMesh*>(Mesh));
		TestEqual(FString::Printf(TEXT("[%s] Socket_Face 에 붙었다"), *RowName), Visual->GetAttachSocketName(), FaceSocket);
		const float SocketScale = Socket.GetScale3D().X;
		const float VisualScale = Visual->GetComponentScale().X;
		TestTrue(FString::Printf(TEXT("[%s] 월드 배율 %.3f ≈ 소켓(몸) 배율 %.3f"), *RowName, VisualScale, SocketScale),
			FMath::IsNearlyEqual(VisualScale, SocketScale, ScaleTolerance * SocketScale) && !FMath::IsNearlyEqual(SocketScale, 1.f));

		// ③ 받침점 = 얼굴 앞면, 앞 = 몸 앞 — 메시 상자 꼭짓점을 월드로 옮겨 소켓 기준 앞·옆으로 잰다
		const FBox Local = Mesh->GetBoundingBox();
		const FTransform& ToWorld = Visual->GetComponentTransform();
		float Front = -UE_BIG_NUMBER;
		float Back = UE_BIG_NUMBER;
		float Left = UE_BIG_NUMBER;
		float RightMost = -UE_BIG_NUMBER;
		for (int32 Corner = 0; Corner < 8; ++Corner)
		{
			const FVector Point((Corner & 1) ? Local.Max.X : Local.Min.X, (Corner & 2) ? Local.Max.Y : Local.Min.Y,
				(Corner & 4) ? Local.Max.Z : Local.Min.Z);
			const FVector Offset = ToWorld.TransformPosition(Point) - Socket.GetLocation();
			Front = FMath::Max(Front, static_cast<float>(FVector::DotProduct(Offset, Forward)));
			Back = FMath::Min(Back, static_cast<float>(FVector::DotProduct(Offset, Forward)));
			Left = FMath::Min(Left, static_cast<float>(FVector::DotProduct(Offset, Right)));
			RightMost = FMath::Max(RightMost, static_cast<float>(FVector::DotProduct(Offset, Right)));
		}
		const float Lateral = (Left + RightMost) * 0.5f;
		UE_LOG(LogTemp, Display, TEXT("[FaceAccessory] %s 삼각형 %d · 배율 %.3f (소켓 %.3f) · 소켓 기준 앞 %.1f / 뒤 %.1f · 옆 %.1f~%.1fcm"),
			*RowName, Triangles, VisualScale, SocketScale, Front, Back, Left, RightMost);
		TestTrue(FString::Printf(TEXT("[%s] 가장 앞 점 %.1fcm 가 얼굴 앞 %.0f~%.0fcm"), *RowName, Front, FrontMinCm, FrontMaxCm),
			Front >= FrontMinCm && Front <= FrontMaxCm);
		TestTrue(FString::Printf(TEXT("[%s] 좌우 가운데 %.1fcm 가 소켓에서 %.0fcm 안"), *RowName, Lateral, LateralToleranceCm),
			FMath::Abs(Lateral) <= LateralToleranceCm);
	}
	Equipment->SetCosmetic(EEquipSlot::FaceAccessory, NAME_None);

	UE_LOG(LogTemp, Display, TEXT("[FaceAccessory] 얼굴 장식 행 %d · 일반 %d · 희귀 %d · 에픽 %d"), FaceRows,
		Grades.FindRef(EItemGrade::Common), Grades.FindRef(EItemGrade::Rare), Grades.FindRef(EItemGrade::Epic));
	TestEqual(TEXT("얼굴 장식 14벌 (명세 4장 6벌 + M87-01 4벌 + M87-02 4벌)"), FaceRows, 14);
	TestEqual(TEXT("일반 7"), Grades.FindRef(EItemGrade::Common), 7);
	TestEqual(TEXT("희귀 4"), Grades.FindRef(EItemGrade::Rare), 4);
	TestEqual(TEXT("에픽 3"), Grades.FindRef(EItemGrade::Epic), 3);
	return true;
}

#endif
