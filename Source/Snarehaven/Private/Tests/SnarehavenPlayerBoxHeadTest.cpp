// INBOX 결함 12:40 ② 2층 검사 — 플레이어 몸 머리가 공이 아니라 앞면 평평한 둥근 상자(가로 > 앞뒤)이고 머리카락이 그 머리를 덮는다.
// 원본 `make_player_box_head.py` 가 머리 정점을 초타원체로 밀어 `SK_Player_Body` 를 기하만 다시 받았다(DESIGN 3.3 「머리 모양」).
// 몸 메시 참조 자세(메시 공간 cm, 몸 앞 = +Y — `HeadLiftShowsFace` 가 발끝으로 확인)의 정점을 머리 띠(정수리 아래 일정 범위)에서 잰다.
//  ① 가로(X) 폭 / 앞뒤(Y) 깊이 ≥ 문턱 — 공이면 1.0 ② 앞면 가운데 정점들의 앞 깊이 흩어짐 / 반 깊이 ≤ 문턱 — 공이면 ≈0.3
//  ③ 머리카락 메시 가로 폭 ≥ 머리 가로 폭 — 모서리가 머리카락 밖으로 삐져나오지 않는다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.Player.Body.BoxHeadFlatFront; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "MeshDescription.h"

namespace SnarehavenBoxHeadTest
{
	const TCHAR* const BodyPath = TEXT("/Game/Game/Characters/Player/Snare/SK_Player_Body.SK_Player_Body");
	const TCHAR* const HairPath = TEXT("/Game/Game/Characters/Player/Hair/SM_Player_Hair_01.SM_Player_Hair_01");

	// 검사 전용 값 (Verification.md 6) — 게임 숫자(지수 2.5·가로 1.12)가 조금 바뀌어도 「공이 아니다」 만 가린다.
	// 머리 띠: 정수리 아래 10~30cm(메시 공간, 머리 반지름 ≈24 — 중심 위아래, 어깨·T자 팔 높이보다 위). 팔 끝은 |X| 로 자른다.
	constexpr float BandTopBelowCrown = 10.f;
	constexpr float BandBottomBelowCrown = 30.f;
	constexpr float BandMaxAbsX = 35.f;
	constexpr float MinWidthOverDepth = 1.05f;      // 공 = 1.0
	constexpr float MaxFrontSpreadOverHalfDepth = 0.20f;   // 공 ≈ 0.3, 지수 2.5 ≈ 0.13 (지수 4 ≈ 0.05)
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenPlayerBoxHeadTest, "Snarehaven.Player.Body.BoxHeadFlatFront",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenPlayerBoxHeadTest::RunTest(const FString& Parameters)
{
	using namespace SnarehavenBoxHeadTest;

	USkeletalMesh* const Body = LoadObject<USkeletalMesh>(nullptr, BodyPath);
	const FMeshDescription* const Description = Body ? Body->GetMeshDescription(0) : nullptr;
	if (!TestNotNull(TEXT("몸 메시 정점(메시 설명 LOD0)"), Description))
	{
		return false;
	}
	const TVertexAttributesConstRef<FVector3f> Positions = Description->GetVertexPositions();

	float Crown = -BIG_NUMBER;
	for (const FVertexID Vertex : Description->Vertices().GetElementIDs())
	{
		Crown = FMath::Max(Crown, Positions[Vertex].Z);
	}
	TArray<FVector3f> Band;
	for (const FVertexID Vertex : Description->Vertices().GetElementIDs())
	{
		const FVector3f P = Positions[Vertex];
		if (P.Z <= Crown - BandTopBelowCrown && P.Z >= Crown - BandBottomBelowCrown && FMath::Abs(P.X) <= BandMaxAbsX)
		{
			Band.Add(P);
		}
	}
	if (!TestTrue(TEXT("머리 띠에 정점이 있다"), Band.Num() > 20))
	{
		return false;
	}
	FBox3f Box(ForceInit);
	for (const FVector3f& P : Band)
	{
		Box += P;
	}
	const FVector3f Size = Box.GetSize();
	const FVector3f Mid = Box.GetCenter();
	const float WidthOverDepth = Size.X / Size.Y;

	// 앞면 가운데 — 앞 절반이고 가로 가운데 절반(±폭/4) 안 정점의 앞 깊이(+Y) 흩어짐
	float FrontMin = BIG_NUMBER;
	float FrontMax = -BIG_NUMBER;
	int32 FrontCount = 0;
	for (const FVector3f& P : Band)
	{
		if (P.Y > Mid.Y && FMath::Abs(P.X - Mid.X) <= Size.X * 0.25f)
		{
			FrontMin = FMath::Min(FrontMin, P.Y);
			FrontMax = FMath::Max(FrontMax, P.Y);
			++FrontCount;
		}
	}
	const float FrontSpread = FrontCount > 0 ? (FrontMax - FrontMin) / (Size.Y * 0.5f) : 1.f;
	AddInfo(FString::Printf(TEXT("[BoxHead] 정수리 %.2f · 머리 띠 정점 %d · 가로 %.2f · 앞뒤 %.2f · 가로/앞뒤 %.3f · 앞면 가운데 정점 %d 깊이 흩어짐 %.2fcm (반 깊이 비 %.3f)"),
		Crown, Band.Num(), Size.X, Size.Y, WidthOverDepth, FrontCount, FrontMax - FrontMin, FrontSpread));
	TestTrue(FString::Printf(TEXT("머리 가로가 앞뒤보다 넓다(%.3f ≥ %.2f)"), WidthOverDepth, MinWidthOverDepth),
		WidthOverDepth >= MinWidthOverDepth);
	TestTrue(FString::Printf(TEXT("머리 앞면이 평평하다(흩어짐 비 %.3f ≤ %.2f)"), FrontSpread, MaxFrontSpreadOverHalfDepth),
		FrontCount >= 4 && FrontSpread <= MaxFrontSpreadOverHalfDepth);

	// 머리카락은 소켓 공간(원점 정수리, 몸 원본 cm)이라 메시 공간 크기 그대로 견준다
	const UStaticMesh* const Hair = LoadObject<UStaticMesh>(nullptr, HairPath);
	if (TestNotNull(TEXT("머리카락 메시"), Hair))
	{
		const FVector HairSize = Hair->GetBoundingBox().GetSize();
		AddInfo(FString::Printf(TEXT("[BoxHead] 머리카락 가로 %.2f · 머리 가로 %.2f"), HairSize.X, Size.X));
		TestTrue(TEXT("머리카락이 상자 머리 가로를 덮는다"), HairSize.X >= Size.X);
	}
	return true;
}

#endif
