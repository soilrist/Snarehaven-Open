// ROADMAP M44-05 2층 검사 — 플레이어 체력이 바뀌는 세 길(맞음·쓰러짐·먹기)에 **소리 요청 줄이 빠진 길이 0** 인지.
// 소스를 글자로 훑는다 (`Snarehaven.PlayerVitals.Death.TouchesNoBalance` 와 같은 짜임) — 재생 자체는
// `Snarehaven.Player.Sound.HurtVoice`·`DownVoice`·`Snarehaven.ActionMatrix.Clip.TableRowsPlayExpectedClip` ⑪ 이 본다.
// 여기서는 누가 나중에 길을 고치다 소리 줄을 지우거나, 먹기 소리를 서버 답 앞으로 올리면 우는 것만 본다 (DESIGN 2.2).
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.Player.Sound; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	/** 한 길 = 어느 함수 몸통에서, 무엇(체력이 바뀌는 줄) 뒤에, 어떤 소리 요청이 정확히 한 번 있어야 하나. */
	struct FBodySoundPath
	{
		const TCHAR* Label;
		const TCHAR* File;
		const TCHAR* Signature;
		const TCHAR* Anchor;
		const TCHAR* SoundCall;
	};

	// DESIGN 2.2 「플레이어 몸 소리」 표의 세 때. 먹기의 기준 줄은 서버 성공 로그 — 그보다 앞이면 응답 전 재생이다 (CLAUDE.md 8).
	const FBodySoundPath BodySoundPaths[] = {
		{ TEXT("맞음"), TEXT("Source/Snarehaven/Private/Animals/AnimalCharacter.cpp"),
			TEXT("bool AAnimalCharacter::TryAttackOnAuthority("), TEXT("Client_ReceiveAnimalHit("), TEXT("Multicast_PlayHurtVoice()") },
		{ TEXT("쓰러짐"), TEXT("Source/Snarehaven/Private/SnarehavenCharacter.cpp"),
			TEXT("void ASnarehavenCharacter::BeginDownedSequence()"), TEXT("UpdateBodyAnimation();"), TEXT("Server_PlayDownVoice()") },
		{ TEXT("먹기"), TEXT("Source/Snarehaven/Private/Inventory/BackpackComponent.cpp"),
			TEXT("bool UBackpackComponent::RequestConsume("), TEXT("ConsumeItem 성공"), TEXT("PlayEatVoice(") },
	};

	/** 함수 몸통(여는 중괄호부터 짝 맞는 닫는 중괄호까지) — `SnarehavenPlayerVitalsTest.cpp` 와 같은 방식. */
	FString ExtractFunctionBody(const FString& Text, const FString& Signature)
	{
		const int32 SignatureAt = Text.Find(Signature, ESearchCase::CaseSensitive);
		if (SignatureAt == INDEX_NONE)
		{
			return FString();
		}
		const int32 OpenAt = Text.Find(TEXT("{"), ESearchCase::CaseSensitive, ESearchDir::FromStart, SignatureAt);
		if (OpenAt == INDEX_NONE)
		{
			return FString();
		}
		int32 Depth = 0;
		for (int32 At = OpenAt; At < Text.Len(); ++At)
		{
			if (Text[At] == TEXT('{'))
			{
				++Depth;
			}
			else if (Text[At] == TEXT('}') && --Depth == 0)
			{
				return Text.Mid(OpenAt, At - OpenAt + 1);
			}
		}
		return FString();
	}

	int32 CountOccurrences(const FString& Text, const TCHAR* Needle)
	{
		int32 Count = 0;
		for (int32 At = Text.Find(Needle, ESearchCase::CaseSensitive); At != INDEX_NONE;
			At = Text.Find(Needle, ESearchCase::CaseSensitive, ESearchDir::FromStart, At + 1))
		{
			++Count;
		}
		return Count;
	}

	/** 빈 글자 = 이 길은 소리가 난다. 아니면 무엇이 빠졌는지 한 줄. */
	FString SurveyPath(const FBodySoundPath& Path, const FString& Source)
	{
		const FString Body = ExtractFunctionBody(Source, Path.Signature);
		if (Body.IsEmpty())
		{
			return FString::Printf(TEXT("%s: 함수 몸통 없음 (%s)"), Path.Label, Path.Signature);
		}
		const int32 AnchorAt = Body.Find(Path.Anchor, ESearchCase::CaseSensitive);
		if (AnchorAt == INDEX_NONE)
		{
			return FString::Printf(TEXT("%s: 기준 줄 '%s' 없음"), Path.Label, Path.Anchor);
		}
		const int32 Calls = CountOccurrences(Body, Path.SoundCall);
		if (Calls != 1)
		{
			return FString::Printf(TEXT("%s: 소리 요청 '%s' %d번 (1번이어야)"), Path.Label, Path.SoundCall, Calls);
		}
		if (Body.Find(Path.SoundCall, ESearchCase::CaseSensitive) < AnchorAt)
		{
			return FString::Printf(TEXT("%s: 소리 요청이 '%s' 보다 앞"), Path.Label, Path.Anchor);
		}
		return FString();
	}

	/** 테스트 폴더 밖 소스에서 `Needle(` 을 부르는 줄 수 — 정의(`::Needle`)·선언(.h)은 뺀다. */
	int32 CountCallersOutsideTests(const TCHAR* Needle)
	{
		TArray<FString> Files;
		IFileManager::Get().FindFilesRecursive(Files, *(FPaths::ProjectDir() / TEXT("Source")), TEXT("*.cpp"), true, false);
		int32 Count = 0;
		for (const FString& File : Files)
		{
			if (File.Contains(TEXT("/Tests/")))
			{
				continue;
			}
			TArray<FString> Lines;
			FFileHelper::LoadFileToStringArray(Lines, *File);
			for (const FString& Line : Lines)
			{
				const FString Trimmed = Line.TrimStart();
				if (Trimmed.StartsWith(TEXT("//")) || Line.Contains(FString(TEXT("::")) + Needle))
				{
					continue;
				}
				Count += CountOccurrences(Line, Needle);
			}
		}
		return Count;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenPlayerNoSilentBodyPathTest,
	"Snarehaven.Player.Sound.NoSilentBodyPath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenPlayerNoSilentBodyPathTest::RunTest(const FString& Parameters)
{
	TArray<FString> Gaps;
	FString FirstSource;
	for (const FBodySoundPath& Path : BodySoundPaths)
	{
		FString Source;
		const FString FullPath = FPaths::ProjectDir() / Path.File;
		if (!TestTrue(*FString::Printf(TEXT("소스를 읽는다 (%s)"), Path.File), FFileHelper::LoadFileToString(Source, *FullPath)))
		{
			return false;
		}
		const FString Gap = SurveyPath(Path, Source);
		if (!Gap.IsEmpty())
		{
			Gaps.Add(Gap);
		}
		if (FirstSource.IsEmpty())
		{
			FirstSource = Source;
		}
	}

	// 맞음은 동물 공격 한 곳에서만 들어온다 — 새로 부르는 곳이 생기면 소리 줄 없이 생긴 것이니 같이 울린다.
	const int32 HitCallers = CountCallersOutsideTests(TEXT("Client_ReceiveAnimalHit("));
	const int32 HurtCallers = CountCallersOutsideTests(TEXT("Multicast_PlayHurtVoice("));
	if (HitCallers > HurtCallers)
	{
		Gaps.Add(FString::Printf(TEXT("맞음: 체력 깎는 곳 %d · 신음 요청 %d"), HitCallers, HurtCallers));
	}

	AddInfo(FString::Printf(TEXT("몸 소리 길: 훑음 %d · 빈 길 %d · 맞음 부르는 곳 %d / 신음 %d"),
		UE_ARRAY_COUNT(BodySoundPaths), Gaps.Num(), HitCallers, HurtCallers));
	for (const FString& Gap : Gaps)
	{
		AddError(Gap);
	}

	// 역검증 (Verification.md 13) — 파일은 그대로, 메모리 사본에서 소리 줄을 지우거나 앞으로 올리면 정확히 그 길이 잡힌다.
	const FBodySoundPath& Hurt = BodySoundPaths[0];
	const FString NoHurt = FirstSource.Replace(TEXT("Target->Multicast_PlayHurtVoice();"), TEXT(""));
	TestTrue(TEXT("역검증: 신음 줄을 지운 사본은 맞음 길이 빈다"), SurveyPath(Hurt, NoHurt).StartsWith(TEXT("맞음:")));

	const FString FakeEat =
		TEXT("bool UBackpackComponent::RequestConsume(FName ItemID)\n{\n")
		TEXT("\tBody->PlayEatVoice(1.f);\n")
		TEXT("\tSend([](){ UE_LOG(LogSnarehaven, Log, TEXT(\"[Backpack] ConsumeItem 성공\")); });\n}\n");
	TestTrue(TEXT("역검증: 서버 답 앞에서 씹는 소리는 잡힌다"), SurveyPath(BodySoundPaths[2], FakeEat).Contains(TEXT("보다 앞")));

	return true;
}

#endif
