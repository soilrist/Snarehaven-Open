// ROADMAP M45-06 2층 검사 — 거점 서버 호출(`USnareBaseSubsystem::Call`) 성공 람다·상자 열기·뽑기 성공 자리에
// **소리 요청이 빠진 길이 0** 인지 소스를 글자로 훑는다 (`Snarehaven.Player.Sound.NoSilentBodyPath` 와 같은 짜임).
// 재생 자체는 `Snarehaven.ActionMatrix.Server.BuildConfirmSound`·`StorageConfirmSound`·`Snarehaven.Crate.Sound.OnlyAfterServerReply`
// 가 본다. 여기서는 누가 새 `Call(` 을 소리 없이 더하거나, 상자·뽑기 소리를 서버 답 앞으로 올리면 우는 것만 본다 (DESIGN 2.2).
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.Base.Sound; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	const TCHAR* const BaseSubsystemFile = TEXT("Source/Snarehaven/Private/Building/SnareBaseSubsystem.cpp");
	const TCHAR* const BuildComponentFile = TEXT("Source/Snarehaven/Private/Building/SnareBaseBuildComponent.cpp");

	// 성공 람다 안에 이 중 하나가 있으면 소리가 난다 — 직접 재생이거나, 소리를 내는 확정 이벤트(아래 EventSounds 가 잇는지 본다).
	const TCHAR* const ServerPathSoundTokens[] = {
		TEXT("PlayStorageConfirmSound()"),
		TEXT("OnCraftConfirmed.Broadcast("),
		TEXT("OnBuildConfirmed.Broadcast("),
		TEXT("OnStructureEditConfirmed.Broadcast("),
	};

	// DESIGN 2.2 「거점·상자 소리」 표 + M28 제작·설치 — 철거·칠하기·유지·발전기 연료(M108-07c-2)·보관함 넣기/빼기(집·배)·제작·텐트·설치·집 고치기 2.
	constexpr int32 ExpectedServerPaths = 12;

	/** 확정 이벤트 → 소리: 받는 함수 몸통에 재생 줄이 한 번. 이벤트에 받는 쪽이 붙어 있는지도 본다. */
	struct FEventSound
	{
		const TCHAR* Label;
		const TCHAR* File;
		const TCHAR* Signature;
		const TCHAR* Binding;
		const TCHAR* SoundCall;
	};

	const FEventSound EventSounds[] = {
		{ TEXT("제작 확정"), BuildComponentFile, TEXT("void USnareBaseBuildComponent::HandleCraftConfirmed("),
			TEXT("OnCraftConfirmed.AddUObject("), TEXT("PlayActionSound(") },
		{ TEXT("설치 확정"), BuildComponentFile, TEXT("void USnareBaseBuildComponent::HandleBuildConfirmed("),
			TEXT("OnBuildConfirmed.AddUObject("), TEXT("PlayActionSound(") },
		{ TEXT("철거·칠하기 확정"), BuildComponentFile, TEXT("void USnareBaseBuildComponent::HandleStructureEditConfirmed("),
			TEXT("OnStructureEditConfirmed.AddUObject("), TEXT("PlayActionSound(") },
		{ TEXT("보관함·유지"), BaseSubsystemFile, TEXT("void USnareBaseSubsystem::PlayStorageConfirmSound()"),
			TEXT(""), TEXT("Play2D(") },
	};

	/** 젬 쓰는 화면 — 기준 줄(결과를 거는 자리) 뒤에 재생이 정확히 한 번. 앞이면 응답 전 재생이다 (CLAUDE.md 8). */
	struct FReplySoundPath
	{
		const TCHAR* Label;
		const TCHAR* File;
		const TCHAR* Signature;
		const TCHAR* Anchor;
		const TCHAR* SoundCall;
	};

	const FReplySoundPath ReplySoundPaths[] = {
		{ TEXT("상자 열기"), TEXT("Source/Snarehaven/Private/UI/CrateScreenWidget.cpp"),
			TEXT("void UCrateScreenWidget::ApplyReply("), TEXT("ResultText = "), TEXT("Play2D(") },
		{ TEXT("뽑기"), TEXT("Source/Snarehaven/Private/UI/GachaScreenWidget.cpp"),
			TEXT("bool UGachaScreenWidget::RequestPull("), TEXT("ShowPullResults(Results);"), TEXT("Play2D(") },
	};

	/** 여는 중괄호부터 짝 맞는 닫는 중괄호까지 (`SnarehavenPlayerBodySoundPathTest.cpp` 와 같은 방식). */
	FString ExtractBraceBlock(const FString& Text, int32 From)
	{
		const int32 OpenAt = Text.Find(TEXT("{"), ESearchCase::CaseSensitive, ESearchDir::FromStart, From);
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

	FString ExtractFunctionBody(const FString& Text, const FString& Signature)
	{
		const int32 SignatureAt = Text.Find(Signature, ESearchCase::CaseSensitive);
		return SignatureAt == INDEX_NONE ? FString() : ExtractBraceBlock(Text, SignatureAt);
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

	/** 거점 소스의 `return Call(` 마다 성공 람다를 꺼내 소리 줄을 찾는다. 빈 길은 Gaps 에 한 줄씩. 훑은 수를 돌려준다. */
	int32 SurveyServerCalls(const FString& Source, TArray<FString>& Gaps)
	{
		int32 Surveyed = 0;
		const TCHAR* const CallNeedle = TEXT("return Call(");
		for (int32 At = Source.Find(CallNeedle, ESearchCase::CaseSensitive); At != INDEX_NONE;
			At = Source.Find(CallNeedle, ESearchCase::CaseSensitive, ESearchDir::FromStart, At + 1))
		{
			++Surveyed;
			const int32 LineEnd = Source.Find(TEXT("\n"), ESearchCase::CaseSensitive, ESearchDir::FromStart, At);
			const FString CallLine = Source.Mid(At, (LineEnd == INDEX_NONE ? Source.Len() : LineEnd) - At).TrimEnd();
			const FString Lambda = ExtractBraceBlock(Source, At);
			bool bSounds = false;
			for (const TCHAR* const Token : ServerPathSoundTokens)
			{
				bSounds = bSounds || Lambda.Contains(Token, ESearchCase::CaseSensitive);
			}
			if (!bSounds)
			{
				Gaps.Add(FString::Printf(TEXT("거점 호출: 성공 람다에 소리 줄 없음 — %s"), *CallLine));
			}
		}
		return Surveyed;
	}

	FString SurveyEvent(const FEventSound& Event, const FString& Source)
	{
		const FString Body = ExtractFunctionBody(Source, Event.Signature);
		if (Body.IsEmpty())
		{
			return FString::Printf(TEXT("%s: 함수 몸통 없음 (%s)"), Event.Label, Event.Signature);
		}
		if (CountOccurrences(Body, Event.SoundCall) != 1)
		{
			return FString::Printf(TEXT("%s: 재생 '%s' %d번 (1번이어야)"), Event.Label, Event.SoundCall,
				CountOccurrences(Body, Event.SoundCall));
		}
		if (*Event.Binding != TEXT('\0') && !Source.Contains(Event.Binding, ESearchCase::CaseSensitive))
		{
			return FString::Printf(TEXT("%s: 받는 쪽이 안 붙음 ('%s' 없음)"), Event.Label, Event.Binding);
		}
		return FString();
	}

	FString SurveyReply(const FReplySoundPath& Path, const FString& Source)
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
			return FString::Printf(TEXT("%s: 재생 '%s' %d번 (1번이어야)"), Path.Label, Path.SoundCall, Calls);
		}
		if (Body.Find(Path.SoundCall, ESearchCase::CaseSensitive) < AnchorAt)
		{
			return FString::Printf(TEXT("%s: 재생이 '%s' 보다 앞"), Path.Label, Path.Anchor);
		}
		return FString();
	}

	bool LoadProjectFile(const TCHAR* File, FString& Out)
	{
		return FFileHelper::LoadFileToString(Out, *(FPaths::ProjectDir() / File));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBaseNoSilentServerPathTest,
	"Snarehaven.Base.Sound.NoSilentServerPath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBaseNoSilentServerPathTest::RunTest(const FString& Parameters)
{
	FString BaseSource;
	FString ComponentSource;
	if (!TestTrue(TEXT("거점 소스를 읽는다"), LoadProjectFile(BaseSubsystemFile, BaseSource))
		|| !TestTrue(TEXT("거점 짓기 소스를 읽는다"), LoadProjectFile(BuildComponentFile, ComponentSource)))
	{
		return false;
	}

	TArray<FString> Gaps;
	const int32 Surveyed = SurveyServerCalls(BaseSource, Gaps);
	// 훑은 수가 줄면 찾기 글자가 낡은 것 — 거짓 통과를 막는다. 늘면 새 길이 생긴 것이니 표·이 수를 같이 고친다.
	TestEqual(TEXT("거점 서버 호출 수 = DESIGN 2.2 표 길 수"), Surveyed, ExpectedServerPaths);

	for (const FEventSound& Event : EventSounds)
	{
		const FString Gap = SurveyEvent(Event, Event.File == BaseSubsystemFile ? BaseSource : ComponentSource);
		if (!Gap.IsEmpty())
		{
			Gaps.Add(Gap);
		}
	}

	FString CrateSource;
	for (const FReplySoundPath& Path : ReplySoundPaths)
	{
		FString Source;
		if (!TestTrue(*FString::Printf(TEXT("소스를 읽는다 (%s)"), Path.File), LoadProjectFile(Path.File, Source)))
		{
			return false;
		}
		const FString Gap = SurveyReply(Path, Source);
		if (!Gap.IsEmpty())
		{
			Gaps.Add(Gap);
		}
		if (CrateSource.IsEmpty())
		{
			CrateSource = Source;
		}
	}

	AddInfo(FString::Printf(TEXT("거점·상자 소리 길: 거점 호출 %d · 이벤트 %d · 젬 화면 %d · 빈 길 %d"),
		Surveyed, UE_ARRAY_COUNT(EventSounds), UE_ARRAY_COUNT(ReplySoundPaths), Gaps.Num()));
	for (const FString& Gap : Gaps)
	{
		AddError(Gap);
	}

	// 역검증 (Verification.md 13) — 파일은 그대로, 메모리 사본에서 소리 줄을 지우거나 앞으로 올리면 정확히 그 길이 잡힌다.
	{
		TArray<FString> CopyGaps;
		const FString NoUpkeepSound = BaseSource.Replace(
			TEXT("PlayStorageConfirmSound();   // 서버가 나무를 받아 준 뒤"), TEXT("// 서버가 나무를 받아 준 뒤"));
		SurveyServerCalls(NoUpkeepSound, CopyGaps);
		TestTrue(TEXT("역검증: 유지 나무 소리를 지운 사본은 FeedUpkeep 길이 빈다"),
			CopyGaps.Num() == 1 && CopyGaps[0].Contains(TEXT("FeedUpkeep")));
	}
	{
		TArray<FString> CopyGaps;
		const FString NewSilentCall = BaseSource + TEXT("\nbool F::X()\n{\n\treturn Call(TEXT(\"Silent\"), Args, [this](auto R)\n\t{\n\t\tOnBaseChanged.Broadcast();\n\t});\n}\n");
		TestEqual(TEXT("역검증: 소리 없는 새 호출을 붙인 사본은 하나가 더 훑이고"), SurveyServerCalls(NewSilentCall, CopyGaps),
			ExpectedServerPaths + 1);
		TestTrue(TEXT("역검증: 그 호출이 빈 길로 잡힌다"), CopyGaps.Num() == 1 && CopyGaps[0].Contains(TEXT("Silent")));
	}
	{
		const FString NoBinding = ComponentSource.Replace(TEXT("OnStructureEditConfirmed.AddUObject("), TEXT("/*unbound*/("));
		TestTrue(TEXT("역검증: 이벤트에 받는 쪽이 없으면 잡힌다"), SurveyEvent(EventSounds[2], NoBinding).Contains(TEXT("안 붙음")));
	}
	{
		const FString EarlyCrate =
			TEXT("void UCrateScreenWidget::ApplyReply(bool bAccepted)\n{\n")
			TEXT("\tUSnareSoundSubsystem::Play2D(this, Cue, 1.f);\n")
			TEXT("\tif (!bAccepted) { return; }\n\tResultText = FText();\n}\n");
		TestTrue(TEXT("역검증: 결과 전에 나는 상자 소리는 잡힌다"),
			SurveyReply(ReplySoundPaths[0], EarlyCrate).Contains(TEXT("보다 앞")));
		const FString NoCrateSound = CrateSource.Replace(TEXT("USnareSoundSubsystem::Play2D("), TEXT("NoSound("));
		TestTrue(TEXT("역검증: 상자 소리를 지운 사본은 잡힌다"), SurveyReply(ReplySoundPaths[0], NoCrateSound).Contains(TEXT("0번")));
	}

	return true;
}

#endif
