// INBOX 결함 2026-10-04 12:40 (루프#403) 2층 검사 — 얼굴 그림 눈 깜빡임.
//  ① 감은 짝 `T_Player_Body_BaseColor_Blink` 은 눈 뜬 그림과 같은 크기(같은 UV)이고, 얼굴 자리만 다르다.
//  ② 사람 몸을 올린 플레이어에서 한 번 깜빡이면 몸 0번 칸 그림이 감은 짝으로 바뀌고,
//     감는 시간이 지나면 원래 그림으로 돌아온다. 몸이 자리표시면 깜빡이지 않는다(역검증).
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR // UTexture2D::Source 는 에디터 전용 — 폰 빌드에서 빠진다

#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/ScopeExit.h"
#include "Player/PlayerFaceBlinkComponent.h"
#include "SnarehavenCharacter.h"
#include "TimerManager.h"

namespace
{
	const TCHAR* OpenFacePath = TEXT("/Game/Game/Characters/Player/Snare/T_Player_Body_BaseColor.T_Player_Body_BaseColor");
	const TCHAR* ClosedFacePath =
		TEXT("/Game/Game/Characters/Player/Snare/T_Player_Body_BaseColor_Blink.T_Player_Body_BaseColor_Blink");

	// 검사 전용 값 (Verification.md 6) — DESIGN 숫자가 바뀌어도 안 깨진다.
	constexpr float TestClosedSeconds = 0.2f;
	constexpr float TestStepSeconds = 0.1f;
	constexpr float TestTimerStepSeconds = 0.01f;
	// 얼굴 자리만 달라야 한다 — 4096² 의 2% 면 머리 앞면 전체(약 12%의 일부)보다도 넉넉히 작다.
	constexpr double MaxChangedFraction = 0.02;
	constexpr int32 ChangedChannelSum = 30;

	UTexture* FaceTextureOf(const ASnarehavenCharacter* Player, const FName ParameterName)
	{
		UTexture* Value = nullptr;
		const UMaterialInterface* const Material = Player->GetMesh()->GetMaterial(0);
		if (Material != nullptr)
		{
			Material->GetTextureParameterValue(FHashedMaterialParameterInfo(ParameterName), Value);
		}
		return Value;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenFaceBlinkTextureTest, "Snarehaven.Player.Face.BlinkTextureOnlyFaceDiffers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenFaceBlinkTextureTest::RunTest(const FString& Parameters)
{
	UTexture2D* const Open = LoadObject<UTexture2D>(nullptr, OpenFacePath);
	UTexture2D* const Closed = LoadObject<UTexture2D>(nullptr, ClosedFacePath);
	if (!TestNotNull(TEXT("눈 뜬 그림"), Open) || !TestNotNull(TEXT("눈 감은 짝"), Closed))
	{
		return false;
	}
	TestEqual(TEXT("원본 가로 같음(같은 UV)"), Closed->Source.GetSizeX(), Open->Source.GetSizeX());
	TestEqual(TEXT("원본 세로 같음(같은 UV)"), Closed->Source.GetSizeY(), Open->Source.GetSizeY());
	TestEqual(TEXT("원본 형식 같음"), (int32)Closed->Source.GetFormat(), (int32)Open->Source.GetFormat());
	TestEqual(TEXT("sRGB 같음"), Closed->SRGB, Open->SRGB);

	TArray64<uint8> OpenBytes;
	TArray64<uint8> ClosedBytes;
	if (!TestTrue(TEXT("눈 뜬 원본 읽힘"), Open->Source.GetMipData(OpenBytes, 0))
		|| !TestTrue(TEXT("감은 짝 원본 읽힘"), Closed->Source.GetMipData(ClosedBytes, 0))
		|| !TestEqual(TEXT("원본 바이트 수 같음"), ClosedBytes.Num(), OpenBytes.Num())
		|| !TestEqual(TEXT("BGRA8"), (int32)Open->Source.GetFormat(), (int32)TSF_BGRA8))
	{
		return false;
	}
	int64 Changed = 0;
	for (int64 Index = 0; Index + 3 < OpenBytes.Num(); Index += 4)
	{
		const int32 Sum = FMath::Abs(OpenBytes[Index] - ClosedBytes[Index])
			+ FMath::Abs(OpenBytes[Index + 1] - ClosedBytes[Index + 1])
			+ FMath::Abs(OpenBytes[Index + 2] - ClosedBytes[Index + 2]);
		Changed += Sum > ChangedChannelSum ? 1 : 0;
	}
	const double Fraction = (double)Changed / (double)(OpenBytes.Num() / 4);
	AddInfo(FString::Printf(TEXT("[FaceBlink] 감은 짝에서 바뀐 텍셀 %lld (%.3f%%)"), Changed, Fraction * 100.0));
	TestTrue(TEXT("눈 자리가 바뀌었다"), Changed > 0);
	TestTrue(TEXT("얼굴 밖은 그대로다"), Fraction < MaxChangedFraction);
	return true;
}

// M85-01 — 표정 3종 그림은 눈 뜬 그림과 같은 크기(같은 UV)이고 얼굴 자리만 다르며, 셋이 서로 다르다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenFaceExpressionTextureTest, "Snarehaven.Player.Face.ExpressionTexturesOnlyFaceDiffers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenFaceExpressionTextureTest::RunTest(const FString& Parameters)
{
	const TCHAR* const Paths[] = {
		TEXT("/Game/Game/Characters/Player/Snare/T_Player_Body_BaseColor_Hurt.T_Player_Body_BaseColor_Hurt"),
		TEXT("/Game/Game/Characters/Player/Snare/T_Player_Body_BaseColor_Down.T_Player_Body_BaseColor_Down"),
		TEXT("/Game/Game/Characters/Player/Snare/T_Player_Body_BaseColor_Happy.T_Player_Body_BaseColor_Happy")};
	UTexture2D* const Open = LoadObject<UTexture2D>(nullptr, OpenFacePath);
	if (!TestNotNull(TEXT("눈 뜬 그림"), Open))
	{
		return false;
	}
	TArray64<uint8> OpenBytes;
	if (!TestTrue(TEXT("눈 뜬 원본 읽힘"), Open->Source.GetMipData(OpenBytes, 0)))
	{
		return false;
	}
	TArray<TArray64<uint8>> Faces;
	for (const TCHAR* const Path : Paths)
	{
		UTexture2D* const Face = LoadObject<UTexture2D>(nullptr, Path);
		if (!TestNotNull(Path, Face))
		{
			return false;
		}
		TestEqual(TEXT("원본 가로 같음(같은 UV)"), Face->Source.GetSizeX(), Open->Source.GetSizeX());
		TestEqual(TEXT("원본 세로 같음(같은 UV)"), Face->Source.GetSizeY(), Open->Source.GetSizeY());
		TestEqual(TEXT("sRGB 같음"), Face->SRGB, Open->SRGB);
		TArray64<uint8>& Bytes = Faces.AddDefaulted_GetRef();
		if (!TestTrue(TEXT("표정 원본 읽힘"), Face->Source.GetMipData(Bytes, 0))
			|| !TestEqual(TEXT("원본 바이트 수 같음"), Bytes.Num(), OpenBytes.Num()))
		{
			return false;
		}
		int64 Changed = 0;
		for (int64 Index = 0; Index + 3 < OpenBytes.Num(); Index += 4)
		{
			const int32 Sum = FMath::Abs(OpenBytes[Index] - Bytes[Index]) + FMath::Abs(OpenBytes[Index + 1] - Bytes[Index + 1])
				+ FMath::Abs(OpenBytes[Index + 2] - Bytes[Index + 2]);
			Changed += Sum > ChangedChannelSum ? 1 : 0;
		}
		const double Fraction = (double)Changed / (double)(OpenBytes.Num() / 4);
		AddInfo(FString::Printf(TEXT("[FaceExpr] %s 바뀐 텍셀 %lld (%.3f%%)"), Path, Changed, Fraction * 100.0));
		TestTrue(TEXT("얼굴 자리가 바뀌었다"), Changed > 0);
		TestTrue(TEXT("얼굴 밖은 그대로다"), Fraction < MaxChangedFraction);
	}
	for (int32 A = 0; A < Faces.Num(); ++A)
	{
		for (int32 B = A + 1; B < Faces.Num(); ++B)
		{
			TestFalse(TEXT("표정끼리 다르다"), FMemory::Memcmp(Faces[A].GetData(), Faces[B].GetData(), Faces[A].Num()) == 0);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenFaceBlinkSwapTest, "Snarehaven.Player.Face.BlinkSwapsAndRestores",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenFaceBlinkSwapTest::RunTest(const FString& Parameters)
{
	UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	ON_SCOPE_EXIT
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	};

	const UTexture* const Open = LoadObject<UTexture>(nullptr, OpenFacePath);
	const UTexture* const Closed = LoadObject<UTexture>(nullptr, ClosedFacePath);
	ASnarehavenCharacter* const Player = World->SpawnActor<ASnarehavenCharacter>();
	if (!TestNotNull(TEXT("플레이어"), Player) || !TestNotNull(TEXT("눈 뜬 그림"), Open)
		|| !TestNotNull(TEXT("눈 감은 짝"), Closed))
	{
		return false;
	}
	UPlayerFaceBlinkComponent* const Blink = Player->GetFaceBlink();
	if (!TestNotNull(TEXT("깜빡임 컴포넌트가 붙어 있다"), Blink))
	{
		return false;
	}
	Blink->ClosedSeconds = TestClosedSeconds;

	// 역검증 — 사람 몸을 올리기 전(자리표시 실린더)에는 얼굴 그림이 없어 감지 않는다.
	TestFalse(TEXT("몸 없이는 안 감는다"), Blink->BlinkNow());
	TestFalse(TEXT("몸 없이는 감은 상태 아님"), Blink->AreEyesClosed());

	if (!TestTrue(TEXT("사람 몸 올림"), Player->ApplyPlayerBody()))
	{
		return false;
	}
	const FName Parameter = Blink->FaceTextureParameterName;
	TestEqual(TEXT("감기 전 = 눈 뜬 그림"), (const UTexture*)FaceTextureOf(Player, Parameter), Open);

	TestTrue(TEXT("한 번 감는다"), Blink->BlinkNow());
	TestTrue(TEXT("감은 상태"), Blink->AreEyesClosed());
	TestTrue(TEXT("몸 0번 칸이 깜빡임 머티리얼"),
		Blink->GetFaceMaterial() != nullptr && Player->GetMesh()->GetMaterial(0) == Blink->GetFaceMaterial());
	TestEqual(TEXT("감은 동안 = 감은 짝"), (const UTexture*)FaceTextureOf(Player, Parameter), Closed);

	// 시간은 타이머가 재므로 타이머만 잘게 흘린다. `GFrameCounter` 를 같이 올려야 한다 —
	// `FTimerManager::Tick` 은 한 프레임에 한 번만 일한다.
	auto Advance = [World](float Seconds)
	{
		for (float Elapsed = 0.f; Elapsed < Seconds - KINDA_SMALL_NUMBER; Elapsed += TestTimerStepSeconds)
		{
			++GFrameCounter;
			World->GetTimerManager().Tick(TestTimerStepSeconds);
		}
	};
	Advance(TestStepSeconds);
	TestTrue(TEXT("감는 시간 안에는 아직 감고 있다"), Blink->AreEyesClosed());
	Advance(TestStepSeconds * 1.5f);
	TestFalse(TEXT("감는 시간이 지나면 뜬다"), Blink->AreEyesClosed());
	TestEqual(TEXT("뜬 뒤 = 원래 그림"), (const UTexture*)FaceTextureOf(Player, Parameter), Open);

	// 두 번째 깜빡임은 같은 동적 인스턴스를 다시 쓴다(매번 새로 만들지 않는다).
	UMaterialInstanceDynamic* const First = Blink->GetFaceMaterial();
	TestTrue(TEXT("다시 감는다"), Blink->BlinkNow());
	TestTrue(TEXT("같은 동적 인스턴스"), Blink->GetFaceMaterial() == First);
	AddInfo(FString::Printf(TEXT("[FaceBlink] 감은 짝 %s → %.2f초 뒤 %s 로 돌아옴"),
		*GetNameSafe(Closed), TestClosedSeconds, *GetNameSafe(Open)));
	return true;
}

// M85-03 — 표정이 바뀌면 몸 0번 칸 그림이 짝 그림으로 바뀌고, 풀리면 원래 그림으로 돌아온다. 아픈·쓰러진 얼굴은 깜빡이지 않는다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenFaceExpressionApplyTest, "Snarehaven.Player.Face.ExpressionSwapsAndRestores",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenFaceExpressionApplyTest::RunTest(const FString& Parameters)
{
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
	if (!TestNotNull(TEXT("플레이어"), Player) || !TestTrue(TEXT("사람 몸 올림"), Player->ApplyPlayerBody()))
	{
		return false;
	}
	UPlayerFaceBlinkComponent* const Blink = Player->GetFaceBlink();
	if (!TestNotNull(TEXT("깜빡임 컴포넌트"), Blink))
	{
		return false;
	}
	const FName Parameter = Blink->FaceTextureParameterName;
	const UTexture* const Open = LoadObject<UTexture>(nullptr, OpenFacePath);
	const UTexture* const Hurt = Blink->HurtTexture.LoadSynchronous();
	const UTexture* const Down = Blink->DownTexture.LoadSynchronous();
	const UTexture* const Happy = Blink->HappyTexture.LoadSynchronous();
	if (!TestNotNull(TEXT("눈 뜬 그림"), Open) || !TestNotNull(TEXT("싫음"), Hurt) || !TestNotNull(TEXT("기절"), Down)
		|| !TestNotNull(TEXT("웃음"), Happy))
	{
		return false;
	}
	// 검사 전용 값 — 체력 비율 문턱 위/아래와 독·먹기 경과는 규칙 함수 시험이 따로 지킨다. 여기선 "연결"만 본다.
	constexpr float TestMax = 100.f;
	constexpr float TestHealthy = 80.f;
	constexpr float TestLow = 5.f;
	constexpr float TestStep = 0.1f;

	Blink->TickExpression(TestStep, TestHealthy, TestMax, false);
	TestEqual(TEXT("멀쩡하면 기본"), Blink->GetExpression(), ESnareFaceExpression::Neutral);

	Blink->TickExpression(TestStep, TestLow, TestMax, false);
	TestEqual(TEXT("체력 낮으면 아픔"), Blink->GetExpression(), ESnareFaceExpression::Hurt);
	TestEqual(TEXT("아플 때 = 싫음 그림"), (const UTexture*)FaceTextureOf(Player, Parameter), Hurt);
	TestFalse(TEXT("아픈 얼굴은 깜빡이지 않는다"), Blink->BlinkNow());
	TestFalse(TEXT("감은 상태 아님"), Blink->AreEyesClosed());

	Blink->TickExpression(TestStep, TestLow, TestMax, true);
	TestEqual(TEXT("쓰러지면 기절"), Blink->GetExpression(), ESnareFaceExpression::Down);
	TestEqual(TEXT("쓰러졌을 때 = 기절 그림"), (const UTexture*)FaceTextureOf(Player, Parameter), Down);

	Blink->TickExpression(TestStep, TestHealthy, TestMax, false);
	TestEqual(TEXT("풀리면 기본"), Blink->GetExpression(), ESnareFaceExpression::Neutral);
	TestEqual(TEXT("풀리면 원래 그림"), (const UTexture*)FaceTextureOf(Player, Parameter), Open);

	Blink->NoteAte(1.f);
	Blink->TickExpression(TestStep, TestHealthy, TestMax, false);
	TestEqual(TEXT("먹으면 웃음"), Blink->GetExpression(), ESnareFaceExpression::Happy);
	TestEqual(TEXT("웃을 때 = 웃음 그림"), (const UTexture*)FaceTextureOf(Player, Parameter), Happy);
	TestTrue(TEXT("웃는 얼굴은 깜빡인다"), Blink->BlinkNow());

	// 웃음 시간이 지나면 기본 — 큰 한 걸음으로 흘린다.
	Blink->TickExpression(SnareFaceExpression::HappySeconds + 1.f, TestHealthy, TestMax, false);
	TestEqual(TEXT("웃음이 끝나면 기본"), Blink->GetExpression(), ESnareFaceExpression::Neutral);

	Blink->NoteAte(-3.f);
	Blink->TickExpression(TestStep, TestHealthy, TestMax, false);
	TestEqual(TEXT("독 먹으면 아픔"), Blink->GetExpression(), ESnareFaceExpression::Hurt);
	AddInfo(FString::Printf(TEXT("[Face] 표정 연결: 아픔 %s · 기절 %s · 웃음 %s · 복귀 %s"), *GetNameSafe(Hurt),
		*GetNameSafe(Down), *GetNameSafe(Happy), *GetNameSafe(Open)));
	return true;
}

// M85-04 — 빈 길 0: 표정 이름 4개 전부 (1) 규칙 함수가 돌려주는 길이 있고 (2) 서로 다른 그림이 붙어 있다.
// 역검증: 표에서 한 행을 지운 사본은 "빠진 표정"을 실제로 찾아낸다(파일은 건드리지 않는다).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenFaceExpressionNoDeadEndTest, "Snarehaven.Player.Face.EveryExpressionReachableWithPicture",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenFaceExpressionNoDeadEndTest::RunTest(const FString& Parameters)
{
	const ESnareFaceExpression All[] = { ESnareFaceExpression::Neutral, ESnareFaceExpression::Hurt, ESnareFaceExpression::Down,
		ESnareFaceExpression::Happy };

	// 검사 전용 값 — 최대 체력 100, 문턱 아래 5, 멀쩡 80. 경과 -1 = 없던 일.
	struct FProbe
	{
		float Health;
		bool bDowned;
		float SincePoison;
		float SinceAte;
	};
	const TArray<FProbe> Probes = {
		{ 80.f, false, -1.f, -1.f }, // 기본
		{ 5.f, false, -1.f, -1.f },  // 아픔
		{ 80.f, true, -1.f, -1.f },  // 기절
		{ 80.f, false, -1.f, 0.f },  // 웃음
	};
	auto Missing = [&](const TArray<FProbe>& Rows) -> int32
	{
		int32 Count = 0;
		for (const ESnareFaceExpression E : All)
		{
			bool bFound = false;
			for (const FProbe& P : Rows)
			{
				bFound |= SnareFaceExpression::For(P.Health, 100.f, P.bDowned, P.SincePoison, P.SinceAte) == E;
			}
			Count += bFound ? 0 : 1;
		}
		return Count;
	};
	TestEqual(TEXT("규칙 함수가 못 돌려주는 표정 0"), Missing(Probes), 0);
	TArray<FProbe> Cut = Probes;
	Cut.RemoveAt(2); // 기절 행 지움
	TestEqual(TEXT("역검증: 기절 행을 지우면 빠진 표정 1"), Missing(Cut), 1);

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
	if (!TestNotNull(TEXT("플레이어"), Player) || !TestTrue(TEXT("사람 몸 올림"), Player->ApplyPlayerBody()))
	{
		return false;
	}
	UPlayerFaceBlinkComponent* const Blink = Player->GetFaceBlink();
	if (!TestNotNull(TEXT("깜빡임 컴포넌트"), Blink))
	{
		return false;
	}
	// 기본 얼굴은 몸 머티리얼의 원래 그림(눈 뜬 그림)이다 — 컴포넌트가 처음 적용할 때 붙잡으므로 여기선 파일에서 직접 연다.
	TSet<const UTexture*> Pictures;
	for (const ESnareFaceExpression E : All)
	{
		Blink->SetExpression(E);
		const UTexture* const Picture =
			E == ESnareFaceExpression::Neutral ? LoadObject<UTexture>(nullptr, OpenFacePath) : Blink->GetExpressionTexture();
		TestNotNull(FString::Printf(TEXT("%s 그림"), SnareFaceExpression::NameOf(E)), Picture);
		Pictures.Add(Picture);
	}
	TestEqual(TEXT("표정 4개 그림 전부 다름"), Pictures.Num(), 4);
	AddInfo(FString::Printf(TEXT("[Face] 빈 길 0: 표정 %d · 서로 다른 그림 %d"), UE_ARRAY_COUNT(All), Pictures.Num()));
	return true;
}

#endif
