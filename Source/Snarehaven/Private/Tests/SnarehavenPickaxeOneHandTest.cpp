// INBOX 감독←사용자 2026-10-04 15:30 ① 2층 검사 — 기본 곡괭이 쓰기 (docs/reference/character_motion.md 「기본 곡괭이」).
// 레퍼런스 원본 = **한 손**(오른손)으로 곡괭이를 몸 옆으로 길게 뻗었다가 **몸 앞 오른쪽 아래로 비스듬히 내려치는** 짧은 휘두르기,
// 한 번 약 0.3초. 감독 보정(15:30 ①): 「휘두르기 0.3초 → 나머지 간격은 든 자세」. 기본 곡괭이 행의 `UseAnim` 을 걸고 곡괭이를 든 뒤 잰다.
// ① 타격(클립 길이 1/3 — DESIGN 3.1 「공격 타격 지연」): 머리 끝이 오른손목보다 아래 · 몸 앞 · 몸 가운데보다 오른쪽
// ② 백스윙(18장 중 f2 = 길이 2/18): 머리 끝이 몸 옆 — 오른쪽으로 손목보다 바깥, 타격 때보다 덜 앞, 손목보다 위
// ③ 휘두른 뒤(길이 15/18): 오른손목이 든 자세(행의 `HoldPoseAnim` 첫 장)와 2cm 안 — 나머지 간격은 든 자세
// ④ 한 손: 왼손목이 쥔 점에서 15cm 넘게 떨어짐(타격·백스윙 둘 다)
// ⑤ 역검증: 옛 두 손 클립 `A_Player_MineTwoHand` 타격 자세는 ④ 에 걸린다 — 기준이 두 손 자세를 가른다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.Equipment.Pickaxe; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "SnarehavenCharacter.h"
#include "Animation/AnimSequence.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/ItemRow.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Equipment/EquipmentComponent.h"
#include "MeshDescription.h"
#include "Snarehaven.h"

namespace SnarehavenPickaxeOneHandTest
{
	constexpr double OneHandMinCm = 15.0;       // 두 손으로 쥐면 왼손목이 자루 위(쥔 점 근처)에 온다
	constexpr double HoldMatchMaxCm = 2.0;      // 휘두른 뒤 = 든 자세
	const FName RightHandBone(TEXT("RightHand"));
	const FName LeftHandBone(TEXT("LeftHand"));
	const TCHAR* const OldTwoHandAnim = TEXT("/Game/Game/Characters/Player/Snare/A_Player_MineTwoHand.A_Player_MineTwoHand");

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

	/** 클립의 Seconds 자세로 뼈를 놓는다 — 시험 월드는 틱이 없어 한 번 평가해야 화면과 같은 자세가 된다. */
	void SetPose(USkeletalMeshComponent* Body, UAnimSequence* Anim, float Seconds)
	{
		Body->PlayAnimation(Anim, false);
		Body->SetPosition(Seconds, false);
		Body->TickAnimation(0.f, false);
		Body->RefreshBoneTransforms();
	}

	/** 쥔 점에서 가장 먼 메시 꼭짓점 = 곡괭이 머리 끝 (메시 공간). */
	bool HeadTip(const UStaticMesh* Mesh, const FVector& Grip, FVector& OutTip)
	{
		const FMeshDescription* const Description = Mesh->GetMeshDescription(0);
		if (Description == nullptr || Description->Vertices().Num() == 0)
		{
			return false;
		}
		OutTip = Grip;
		for (const FVertexID Vertex : Description->Vertices().GetElementIDs())
		{
			const FVector Point(Description->GetVertexPosition(Vertex));
			OutTip = FVector::DistSquared(Point, Grip) > FVector::DistSquared(OutTip, Grip) ? Point : OutTip;
		}
		return true;
	}

	struct FSwingMeasure
	{
		FVector Tip = FVector::ZeroVector;
		FVector Wrist = FVector::ZeroVector;
		double TipForward = 0.0;
		double TipRight = 0.0;
		double WristRight = 0.0;
		double TipZ = 0.0;
		double WristZ = 0.0;
		double LeftToGrip = 0.0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenPickaxeOneHandTest, "Snarehaven.Equipment.Pickaxe.BasicSwingsOneHandDownRight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenPickaxeOneHandTest::RunTest(const FString& Parameters)
{
	using namespace SnarehavenPickaxeOneHandTest;

	UWorld* const World = MakeWorld();
	ASnarehavenCharacter* const Player = World->SpawnActor<ASnarehavenCharacter>();
	UEquipmentComponent* const Equipment = Player ? Player->GetEquipment() : nullptr;
	if (!TestNotNull(TEXT("장비 컴포넌트가 있다"), Equipment)
		|| !TestTrue(TEXT("사람 메시를 올렸다"), Player->ApplyPlayerBody()))
	{
		DestroyWorld(World);
		return false;
	}
	USkeletalMeshComponent* const Body = Player->GetMesh();
	const FVector Origin = Player->GetActorLocation();
	const FVector Forward = Player->GetActorForwardVector();
	const FVector Right = Player->GetActorRightVector();

	if (!TestTrue(TEXT("기본 곡괭이를 들었다"), Equipment->EquipItemByItemID(FName(TEXT("item.pickaxe_basic")))))
	{
		DestroyWorld(World);
		return false;
	}
	const FItemRow* const Row = Equipment->GetEquippedRow(EEquipSlot::Tool);
	UAnimSequence* const Use = Row ? Row->UseAnim.LoadSynchronous() : nullptr;
	UAnimSequence* const Hold = Row ? Row->HoldPoseAnim.LoadSynchronous() : nullptr;
	UAnimSequence* const OldTwoHand = LoadObject<UAnimSequence>(nullptr, OldTwoHandAnim);
	const UStaticMeshComponent* const Pickaxe = Equipment->GetEquippedMesh(EEquipSlot::Tool);
	FVector TipLocal;
	if (!TestNotNull(TEXT("기본 곡괭이 행 쓰기 클립"), Use) || !TestNotNull(TEXT("기본 곡괭이 행 든 자세 클립"), Hold) || !TestNotNull(TEXT("옛 두 손 클립"), OldTwoHand)
		|| !TestNotNull(TEXT("Tool 칸 메시"), Pickaxe) || !TestTrue(TEXT("기본 곡괭이 행에 쥐는 자리가 있다"), Row->GripOffset.IsSet())
		|| !TestTrue(TEXT("곡괭이 메시 꼭짓점"), HeadTip(Pickaxe->GetStaticMesh(), Row->GripOffset.Location, TipLocal)))
	{
		DestroyWorld(World);
		return false;
	}

	const auto Measure = [&](UAnimSequence* Anim, float Seconds)
		{
			SetPose(Body, Anim, Seconds);
			const FTransform& ToolWorld = Pickaxe->GetComponentTransform();
			const FVector Grip = ToolWorld.TransformPosition(Row->GripOffset.Location);
			const FVector Wrist = Body->GetBoneLocation(RightHandBone);
			FSwingMeasure M;
			M.Tip = ToolWorld.TransformPosition(TipLocal);
			M.Wrist = Wrist;
			M.TipForward = FVector::DotProduct(M.Tip - Origin, Forward);
			M.TipRight = FVector::DotProduct(M.Tip - Origin, Right);
			M.WristRight = FVector::DotProduct(Wrist - Origin, Right);
			M.TipZ = M.Tip.Z;
			M.WristZ = Wrist.Z;
			M.LeftToGrip = FVector::Dist(Body->GetBoneLocation(LeftHandBone), Grip);
			UE_LOG(LogSnarehaven, Display,
				TEXT("[PickaxeOneHand] %s %.2f초 머리 끝 앞 %.1f 오른 %.1f z %.1f · 오른손목 오른 %.1f z %.1f · 왼손목-쥔 점 %.1f"),
				*Anim->GetName(), Seconds, M.TipForward, M.TipRight, M.TipZ, M.WristRight, M.WristZ, M.LeftToGrip);
			return M;
		};

	// 타격 = 길이 1/3 (재생 속도가 길이 ÷ 공격 간격이라 간격 0.6초에서 0.2초 — DESIGN 3.1) · 백스윙 f2 · 든 자세 복귀 f13~18.
	const float Length = Use->GetPlayLength();
	const FSwingMeasure Hit = Measure(Use, Length / 3.f);
	TestTrue(FString::Printf(TEXT("① 타격 머리 끝 z %.1f 이 오른손목 %.1f 보다 아래"), Hit.TipZ, Hit.WristZ), Hit.TipZ < Hit.WristZ);
	TestTrue(FString::Printf(TEXT("① 타격 머리 끝이 몸 앞(%.1f)·오른쪽(%.1f)"), Hit.TipForward, Hit.TipRight),
		Hit.TipForward > 0.0 && Hit.TipRight > 0.0);

	const FSwingMeasure Back = Measure(Use, Length * 2.f / 18.f);
	TestTrue(FString::Printf(TEXT("② 백스윙 머리 끝 오른 %.1f 이 손목 %.1f 보다 바깥 (몸 옆)"), Back.TipRight, Back.WristRight),
		Back.TipRight > Back.WristRight && Back.WristRight > 0.0);
	TestTrue(FString::Printf(TEXT("② 백스윙 머리 끝 앞 %.1f 이 타격 %.1f 보다 덜 앞"), Back.TipForward, Hit.TipForward),
		Back.TipForward < Hit.TipForward);
	TestTrue(FString::Printf(TEXT("② 백스윙 머리 끝 z %.1f 이 손목 %.1f 보다 위"), Back.TipZ, Back.WristZ), Back.TipZ > Back.WristZ);

	const FSwingMeasure After = Measure(Use, Length * 15.f / 18.f);
	const FSwingMeasure Rest = Measure(Hold, 0.f);
	const double HoldGap = FVector::Dist(After.Wrist, Rest.Wrist);
	TestTrue(FString::Printf(TEXT("③ 휘두른 뒤 오른손목이 든 자세와 %.2fcm ≤ %.1f"), HoldGap, HoldMatchMaxCm), HoldGap <= HoldMatchMaxCm);

	TestTrue(FString::Printf(TEXT("④ 한 손 — 왼손목이 쥔 점에서 타격 %.1f · 백스윙 %.1f > %.0fcm"), Hit.LeftToGrip, Back.LeftToGrip, OneHandMinCm),
		Hit.LeftToGrip > OneHandMinCm && Back.LeftToGrip > OneHandMinCm);

	const FSwingMeasure Old = Measure(OldTwoHand, OldTwoHand->GetPlayLength() / 3.f);
	TestTrue(FString::Printf(TEXT("⑤ 옛 %s(두 손)는 한 손 기준에 걸린다 (왼손목-쥔 점 %.1f)"), *OldTwoHand->GetName(), Old.LeftToGrip),
		Old.LeftToGrip <= OneHandMinCm);

	DestroyWorld(World);
	return true;
}

// 감독 2026-10-05 — 손 장비 든 자세·쓰기 동안 몸에 묻히거나 땅에 박히지 않는가 (docs/reference/character_motion.md 「기본 곡괭이」·「손도끼」·「마체테」,
// DESIGN 3.5 「도구 크기」 몸 배율). 오른손(`Socket_Weapon`)에 쥐는 자리(`GripOffset`)가 있는 행 전부를 들고 든 자세 첫 장 + 쓰기 클립 12장을 잰다.
// ① 든 자세 머리 끝(쥔 점에서 가장 먼 꼭짓점)이 캡슐 밖 — 몸에 안 묻힘
// ② 든 자세·쓰기 내내 메시 가장 낮은 점이 발바닥(캡슐 바닥)보다 위 — 땅에 안 박힘
// ③ 기본 곡괭이 쉴 때 = 원본 「한 손, 자루가 몸 앞 오른쪽 아래로」: 머리 끝이 오른손목보다 아래 · 몸 앞 · 몸 오른쪽 · 왼손목은 쥔 점에서 15cm 밖
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenHandToolClearanceTest, "Snarehaven.Equipment.Pickaxe.HandToolsClearBodyAndGround",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenHandToolClearanceTest::RunTest(const FString& Parameters)
{
	using namespace SnarehavenPickaxeOneHandTest;
	constexpr int32 UseSamples = 12;

	UWorld* const World = MakeWorld();
	ASnarehavenCharacter* const Player = World->SpawnActor<ASnarehavenCharacter>();
	UEquipmentComponent* const Equipment = Player ? Player->GetEquipment() : nullptr;
	const UDataTable* const ItemTable = LoadObject<UDataTable>(nullptr, TEXT("/Game/Game/Data/Items/DT_ItemTable.DT_ItemTable"));
	if (!TestNotNull(TEXT("장비 컴포넌트가 있다"), Equipment) || !TestNotNull(TEXT("아이템 표"), ItemTable)
		|| !TestTrue(TEXT("사람 메시를 올렸다"), Player->ApplyPlayerBody()))
	{
		DestroyWorld(World);
		return false;
	}
	USkeletalMeshComponent* const Body = Player->GetMesh();
	const UCapsuleComponent* const Capsule = Player->GetCapsuleComponent();
	const FVector Origin = Player->GetActorLocation();
	const FVector Forward = Player->GetActorForwardVector();
	const FVector Right = Player->GetActorRightVector();
	const double CapsuleRadius = Capsule->GetScaledCapsuleRadius();
	const double FootZ = Origin.Z - Capsule->GetScaledCapsuleHalfHeight();
	SetPose(Body, LoadObject<UAnimSequence>(nullptr, TEXT("/Game/Game/Characters/Player/Snare/A_Player_HoldOneHand.A_Player_HoldOneHand")), 0.f);
	UE_LOG(LogSnarehaven, Display, TEXT("[ToolClear] 몸 배율 %.3f · 발 위 목 %.1f · 머리 %.1f · 골반 %.1f · 오른손목 %.1f · 캡슐 키 %.1f"),
		Body->GetComponentScale().X, Body->GetBoneLocation(FName(TEXT("Neck"))).Z - FootZ, Body->GetBoneLocation(FName(TEXT("Head"))).Z - FootZ,
		Body->GetBoneLocation(FName(TEXT("Hips"))).Z - FootZ, Body->GetBoneLocation(RightHandBone).Z - FootZ, Capsule->GetScaledCapsuleHalfHeight() * 2.0);

	int32 Checked = 0;
	for (const TPair<FName, uint8*>& Pair : ItemTable->GetRowMap())
	{
		const FItemRow& TableRow = *reinterpret_cast<const FItemRow*>(Pair.Value);
		if (!TableRow.GripOffset.IsSet() || TableRow.HoldPoseAnim.IsNull())
		{
			continue;
		}
		const FString RowName = Pair.Key.ToString();
		Equipment->UnequipSlot(EEquipSlot::Weapon);
		Equipment->UnequipSlot(EEquipSlot::Tool);
		if (!TestTrue(FString::Printf(TEXT("[%s] 손에 들었다"), *RowName), Equipment->EquipItem(Pair.Key)))
		{
			continue;
		}
		const UStaticMeshComponent* const Tool = Equipment->GetEquippedMesh(TableRow.EquipSlot);
		UAnimSequence* const Hold = TableRow.HoldPoseAnim.LoadSynchronous();
		UAnimSequence* const Use = TableRow.UseAnim.LoadSynchronous();
		FVector TipLocal;
		if (!TestNotNull(*FString::Printf(TEXT("[%s] 메시"), *RowName), Tool) || !TestNotNull(*FString::Printf(TEXT("[%s] 든 자세"), *RowName), Hold)
			|| !TestTrue(*FString::Printf(TEXT("[%s] 메시 꼭짓점"), *RowName), HeadTip(Tool->GetStaticMesh(), TableRow.GripOffset.Location, TipLocal)))
		{
			continue;
		}
		++Checked;
		const FMeshDescription* const Description = Tool->GetStaticMesh()->GetMeshDescription(0);
		const auto LowestZ = [&]()
			{
				double Low = TNumericLimits<double>::Max();
				const FTransform& ToolWorld = Tool->GetComponentTransform();
				for (const FVertexID Vertex : Description->Vertices().GetElementIDs())
				{
					Low = FMath::Min(Low, ToolWorld.TransformPosition(FVector(Description->GetVertexPosition(Vertex))).Z);
				}
				return Low;
			};

		SetPose(Body, Hold, 0.f);
		const FTransform& ToolWorld = Tool->GetComponentTransform();
		const FVector Tip = ToolWorld.TransformPosition(TipLocal);
		const FVector Grip = ToolWorld.TransformPosition(TableRow.GripOffset.Location);
		const FVector Wrist = Body->GetBoneLocation(RightHandBone);
		const double TipForward = FVector::DotProduct(Tip - Origin, Forward);
		const double TipRight = FVector::DotProduct(Tip - Origin, Right);
		const double TipOut = FVector::Dist2D(Tip, Origin);
		const double LeftToGrip = FVector::Dist(Body->GetBoneLocation(LeftHandBone), Grip);
		double Lowest = LowestZ();
		const double HoldLowest = Lowest;
		FString LowestNote = TEXT("든 자세");
		if (Use != nullptr)
		{
			for (int32 Index = 0; Index <= UseSamples; ++Index)
			{
				SetPose(Body, Use, Use->GetPlayLength() * Index / UseSamples);
				const double FrameLowest = LowestZ();
				if (FrameLowest < Lowest)
				{
					Lowest = FrameLowest;
					const FTransform& UseWorld = Tool->GetComponentTransform();
					const FVector UseGrip = UseWorld.TransformPosition(TableRow.GripOffset.Location);
					LowestNote = FString::Printf(TEXT("%d/%d장 손목 z %.1f · 쥔 점→머리 끝 방향 %s"), Index, UseSamples,
						Body->GetBoneLocation(RightHandBone).Z, *(UseWorld.TransformPosition(TipLocal) - UseGrip).GetSafeNormal().ToCompactString());
				}
			}
		}
		UE_LOG(LogSnarehaven, Display,
			TEXT("[ToolClear] %s 배율 %.2f · 든 자세 머리 끝 앞 %.1f 오른 %.1f z %.1f(손목 z %.1f) 캡슐 중심에서 %.1f(반지름 %.1f) · 왼손목-쥔 점 %.1f · 가장 낮은 점 든 자세 %.1f 쓰기 포함 %.1f [%s] (발 %.1f)"),
			*RowName, Tool->GetComponentScale().X, TipForward, TipRight, Tip.Z, Wrist.Z, TipOut, CapsuleRadius, LeftToGrip, HoldLowest, Lowest, *LowestNote, FootZ);

		TestTrue(FString::Printf(TEXT("① [%s] 든 자세 머리 끝이 몸 밖 %.1f > 캡슐 반지름 %.1f"), *RowName, TipOut, CapsuleRadius), TipOut > CapsuleRadius);
		TestTrue(FString::Printf(TEXT("② [%s] 든 자세 가장 낮은 점 %.1f 이 발 %.1f 보다 위"), *RowName, HoldLowest, FootZ), HoldLowest > FootZ);
		// 마체테도 예외 없다 — 옛 Mixamo `Slash` 는 6/12장에서 손목이 무릎 높이로 내려가 날이 −133cm 까지 박혔고(경고로 두었음),
		// 감독 2026-10-05 에 허리 높이 수평 옆 베기 `slash_v2`(`tools/blender/slash_v2_make.py`)로 바꿨다.
		TestTrue(FString::Printf(TEXT("② [%s] 쓰기 포함 가장 낮은 점 %.1f 이 발 %.1f 보다 위"), *RowName, Lowest, FootZ), Lowest > FootZ);
		if (TableRow.ItemID == FName(TEXT("item.pickaxe_basic")))
		{
			TestTrue(FString::Printf(TEXT("③ 기본 곡괭이 쉴 때 머리 끝 z %.1f 가 오른손목 %.1f 보다 아래(늘어짐)"), Tip.Z, Wrist.Z), Tip.Z < Wrist.Z);
			TestTrue(FString::Printf(TEXT("③ 기본 곡괭이 쉴 때 머리 끝이 몸 앞(%.1f)·오른쪽(%.1f)"), TipForward, TipRight), TipForward > 0.0 && TipRight > 0.0);
			TestTrue(FString::Printf(TEXT("③ 기본 곡괭이 쉴 때 한 손 — 왼손목-쥔 점 %.1f > %.0fcm"), LeftToGrip, OneHandMinCm), LeftToGrip > OneHandMinCm);
		}
	}
	TestTrue(FString::Printf(TEXT("쥐는 자리·든 자세가 있는 손 장비 %d개를 쟀다"), Checked), Checked >= 7);

	DestroyWorld(World);
	return true;
}


// INBOX 결함 2026-10-20 00:10 ④ — 「곡괭이 3종을 들면 몸이 앞으로 크게 숙여짐」(`lh1349_Tool1`·`Tool2`). 고급·전기 곡괭이 행이
// 옛 `A_Player_HoldTwoHand`(Mixamo 2hand Idle, 무릎·허리를 굽힌 싸움 자세)를 든 자세로 썼다. 아이템별 예외 말고 표 전체로 본다:
// 든 자세(`HoldPoseAnim`)가 있는 행 전부의 든 자세를 Samples 장 걸고, 앞으로 기운 각이 맨손 대기(`A_Player_Idle`)보다 LeanMaxDeg 넘게
// 크지 않은가 — 각 = 골반→머리 뼈(몸통)와 머리 뼈의 「대기 때 위를 보던 축」(2.5등신 큰 머리, 화면에서 숙임은 고개로 읽힌다) 중 큰 것.
// 역검증: 옛 `A_Player_HoldTwoHand` 는 이 기준에 걸린다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenHoldPoseUprightTest, "Snarehaven.Equipment.Pickaxe.HoldPosesStandUpright",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenHoldPoseUprightTest::RunTest(const FString& Parameters)
{
	using namespace SnarehavenPickaxeOneHandTest;
	constexpr double LeanMaxDeg = 10.0;     // 걷기 상체 앞숙임 8°(SnarehavenPlayerArmPoseTest) + 2° — 그보다 숙이면 서 있는 자세가 아니다
	constexpr int32 Samples = 4;
	const FName HipsBone(TEXT("Hips"));
	const FName HeadBone(TEXT("Head"));

	UWorld* const World = MakeWorld();
	ASnarehavenCharacter* const Player = World->SpawnActor<ASnarehavenCharacter>();
	const UDataTable* const ItemTable = LoadObject<UDataTable>(nullptr, TEXT("/Game/Game/Data/Items/DT_ItemTable.DT_ItemTable"));
	UAnimSequence* const Idle = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/Game/Characters/Player/Snare/A_Player_Idle.A_Player_Idle"));
	UAnimSequence* const OldHold = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/Game/Characters/Player/Snare/A_Player_HoldTwoHand.A_Player_HoldTwoHand"));
	if (!TestNotNull(TEXT("플레이어"), Player) || !TestNotNull(TEXT("아이템 표"), ItemTable) || !TestNotNull(TEXT("맨손 대기 클립"), Idle)
		|| !TestNotNull(TEXT("옛 두 손 든 자세 클립"), OldHold) || !TestTrue(TEXT("사람 메시를 올렸다"), Player->ApplyPlayerBody()))
	{
		DestroyWorld(World);
		return false;
	}
	USkeletalMeshComponent* const Body = Player->GetMesh();
	const FVector Forward = Player->GetActorForwardVector();
	const auto ForwardDeg = [&](const FVector& Direction)
		{
			return FMath::RadiansToDegrees(FMath::Atan2(FVector::DotProduct(Direction, Forward), Direction.Z));
		};
	// 머리 뼈 공간에서 대기 첫 장 때 월드 위(+Z)였던 축 — 이 축이 앞으로 기울면 고개를 숙인 것
	SetPose(Body, Idle, 0.f);
	const FVector HeadUpLocal = Body->GetBoneQuaternion(HeadBone).Inverse().RotateVector(FVector::UpVector);
	// 클립 Samples 장 중 가장 앞으로 기운 각(수직 0°) — 몸통·고개 중 큰 것
	const auto MaxLean = [&](UAnimSequence* Anim)
		{
			double Worst = -90.0;
			for (int32 Index = 0; Index < Samples; ++Index)
			{
				SetPose(Body, Anim, Anim->GetPlayLength() * Index / Samples);
				const double Trunk = ForwardDeg(Body->GetBoneLocation(HeadBone) - Body->GetBoneLocation(HipsBone));
				const double Head = ForwardDeg(Body->GetBoneQuaternion(HeadBone).RotateVector(HeadUpLocal));
				Worst = FMath::Max(Worst, FMath::Max(Trunk, Head));
			}
			return Worst;
		};
	const double IdleLean = MaxLean(Idle);

	TMap<UAnimSequence*, FString> Holds;
	for (const TPair<FName, uint8*>& Pair : ItemTable->GetRowMap())
	{
		const FItemRow& TableRow = *reinterpret_cast<const FItemRow*>(Pair.Value);
		if (UAnimSequence* const Hold = TableRow.HoldPoseAnim.IsNull() ? nullptr : TableRow.HoldPoseAnim.LoadSynchronous())
		{
			FString& Rows = Holds.FindOrAdd(Hold);
			Rows += (Rows.IsEmpty() ? TEXT("") : TEXT(",")) + Pair.Key.ToString();
		}
	}
	for (const TPair<UAnimSequence*, FString>& Hold : Holds)
	{
		const double Lean = MaxLean(Hold.Key);
		UE_LOG(LogSnarehaven, Display, TEXT("[HoldUpright] %s 앞숙임 %.1f° (대기 %.1f°) · 행 %s"), *Hold.Key->GetName(), Lean, IdleLean, *Hold.Value);
		TestTrue(FString::Printf(TEXT("[%s] 든 자세 앞숙임 %.1f° ≤ 대기 %.1f° + %.0f° (행 %s)"), *Hold.Key->GetName(), Lean, IdleLean, LeanMaxDeg, *Hold.Value),
			Lean <= IdleLean + LeanMaxDeg);
	}
	TestTrue(FString::Printf(TEXT("든 자세 %d가지를 쟀다"), Holds.Num()), Holds.Num() >= 5);
	TestFalse(TEXT("옛 HoldTwoHand 를 쓰는 행이 없다"), Holds.Contains(OldHold));

	const double OldLean = MaxLean(OldHold);
	UE_LOG(LogSnarehaven, Display, TEXT("[HoldUpright] 역검증 옛 %s 앞숙임 %.1f° · 기준 %.1f°"), *OldHold->GetName(), OldLean, IdleLean + LeanMaxDeg);
	TestTrue(FString::Printf(TEXT("역검증: 옛 %s 앞숙임 %.1f° 는 기준 %.1f° 에 걸린다"), *OldHold->GetName(), OldLean, IdleLean + LeanMaxDeg),
		OldLean > IdleLean + LeanMaxDeg);

	DestroyWorld(World);
	return true;
}

#endif
