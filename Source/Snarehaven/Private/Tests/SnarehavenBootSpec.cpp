// ROADMAP M0-05 2층 검사 — L_Boot 에서 시작하면 로비(L_Title)에 닿는가.
// M3-04 전까지는 여기서 사냥 맵까지 자동으로 갔다. 로비 화면이 생긴 뒤로는 **로비가 종착**이고
// 사냥 맵은 로비 버튼만 연다 — 자동으로 지나쳐 버리면 로비가 한 프레임도 보이지 않는다.
// 맵을 실제로 띄우지 않고 ASnarehavenGameMode::GetNextLevelName 의 흐름 규칙과 그 규칙이 기대는
// 전제(맵 패키지 존재·기본 맵·기본 게임모드)를 본다. 실제 화면 전이는 3층에서 확인한다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "SnarehavenGameMode.h"
#include "GameMapsSettings.h"
#include "Online/SnareOnlineSubsystem.h"
#include "Misc/PackageName.h"

namespace
{
	// 흐름에 나오는 맵 (Content/Game/Maps). 순서 = 기대하는 진행 순서.
	const TCHAR* BootFlowPackages[] =
	{
		TEXT("/Game/Game/Maps/L_Boot"),
		TEXT("/Game/Game/Maps/L_Title"),
		// 사냥 맵은 자동 흐름에 없지만 로비의 "혼자 사냥" 버튼이 여니 있어야 한다.
		TEXT("/Game/Game/Maps/L_Hunt"),
	};

	// 체인이 끝나지 않을 때 테스트가 영원히 도는 걸 막는 상한 (테스트 전용 값 — Verification.md 6).
	constexpr int32 MaxFlowSteps = 8;
}

BEGIN_DEFINE_SPEC(FSnarehavenBootSpec, "Snarehaven.Boot.ReachesLobby",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
END_DEFINE_SPEC(FSnarehavenBootSpec)

void FSnarehavenBootSpec::Define()
{
	Describe(TEXT("맵 흐름 규칙"), [this]()
	{
		It(TEXT("L_Boot 다음은 L_Title"), [this]()
		{
			TestEqual(TEXT("L_Boot 의 다음 맵"),
				ASnarehavenGameMode::GetNextLevelName(TEXT("L_Boot")), FName(TEXT("L_Title")));
		});

		It(TEXT("로비(L_Title)가 종착이라 더 넘어가지 않는다"), [this]()
		{
			// 여기가 다시 L_Hunt 를 돌려주면 로비 화면(M3-04)이 한 프레임 만에 사라진다.
			TestTrue(TEXT("L_Title 의 다음 맵은 없다"),
				ASnarehavenGameMode::GetNextLevelName(TEXT("L_Title")).IsNone());
			TestTrue(TEXT("L_Title 은 로비 맵이다"), ASnarehavenGameMode::IsLobbyMap(TEXT("L_Title")));
		});

		It(TEXT("L_Hunt 는 흐름 밖이라 더 넘어가지 않는다"), [this]()
		{
			TestTrue(TEXT("L_Hunt 의 다음 맵은 없다"),
				ASnarehavenGameMode::GetNextLevelName(TEXT("L_Hunt")).IsNone());
			// 사냥 맵에서 로비 UI 가 뜨면 조작 위에 버튼 네 개가 덮인다.
			TestFalse(TEXT("L_Hunt 는 로비 맵이 아니다"), ASnarehavenGameMode::IsLobbyMap(TEXT("L_Hunt")));
		});

		It(TEXT("PIE 접두어(UEDPIE_0_)가 붙어도 같은 흐름"), [this]()
		{
			// PIE 에서만 조용히 흐름이 끊기는 사고를 막는 단언 — GetMapName() 이 접두어를 붙여 준다.
			TestEqual(TEXT("UEDPIE_0_L_Boot 의 다음 맵"),
				ASnarehavenGameMode::GetNextLevelName(TEXT("UEDPIE_0_L_Boot")), FName(TEXT("L_Title")));
			TestTrue(TEXT("UEDPIE_0_L_Title 은 로비라 멈춘다"),
				ASnarehavenGameMode::GetNextLevelName(TEXT("UEDPIE_0_L_Title")).IsNone());
			TestTrue(TEXT("UEDPIE_0_L_Title 도 로비 맵으로 알아본다"),
				ASnarehavenGameMode::IsLobbyMap(TEXT("UEDPIE_0_L_Title")));
		});

		It(TEXT("흐름 밖의 맵은 아무 데로도 보내지 않는다"), [this]()
		{
			// 2층 검사가 손으로 만드는 테스트 월드나 에디터 임시 맵에서 OpenLevel 이 튀어나오면 안 된다.
			TestTrue(TEXT("모르는 맵의 다음 맵은 없다"),
				ASnarehavenGameMode::GetNextLevelName(TEXT("L_Ranch")).IsNone());
			TestTrue(TEXT("빈 맵 이름의 다음 맵은 없다"),
				ASnarehavenGameMode::GetNextLevelName(FString()).IsNone());
		});

		It(TEXT("L_Boot 에서 따라가면 로비에서 멈춘다"), [this]()
		{
			// 조각별 단언이 다 맞아도 체인이 돌거나(L_Boot -> L_Title -> L_Boot) 끝나지 않을 수 있어
			// 실제로 끝까지 따라가 본다. 이게 M0-05 합격선("Boot 가 사람이 조작할 첫 화면에 닿는다")의 본체다.
			TArray<FName> VisitedMaps;
			FString CurrentMap = TEXT("L_Boot");
			int32 StepCount = 0;
			for (; StepCount < MaxFlowSteps; ++StepCount)
			{
				const FName NextMap = ASnarehavenGameMode::GetNextLevelName(CurrentMap);
				if (NextMap.IsNone())
				{
					break;
				}
				if (VisitedMaps.Contains(NextMap))
				{
					AddError(FString::Printf(TEXT("맵 흐름이 %s 로 되돌아가 돈다"), *NextMap.ToString()));
					return;
				}
				VisitedMaps.Add(NextMap);
				CurrentMap = NextMap.ToString();
			}

			TestTrue(TEXT("상한 안에 흐름이 끝났다"), StepCount < MaxFlowSteps);
			TestEqual(TEXT("종착 맵"), CurrentMap, USnareOnlineSubsystem::LobbyLevelName.ToString());
			TestEqual(TEXT("거쳐 간 맵 수"), VisitedMaps.Num(), 1);
		});
	});

	Describe(TEXT("흐름이 기대는 것"), [this]()
	{
		It(TEXT("흐름에 나오는 맵 세 개가 실제로 있다"), [this]()
		{
			// 규칙이 맞아도 맵 파일이 없으면 OpenLevel 이 런타임에 조용히 실패한다.
			for (const TCHAR* PackageName : BootFlowPackages)
			{
				TestTrue(FString::Printf(TEXT("맵 패키지 존재: %s"), PackageName),
					FPackageName::DoesPackageExist(PackageName));
			}
		});

		It(TEXT("프로젝트 기본 맵이 L_Boot 다"), [this]()
		{
			// 흐름의 출발점. 여기가 바뀌면 부팅이 L_Title 부터 시작해 버린다.
			const FString GameDefaultMap = UGameMapsSettings::GetGameDefaultMap();
			TestTrue(FString::Printf(TEXT("기본 맵(%s)이 L_Boot"), *GameDefaultMap),
				GameDefaultMap.Contains(TEXT("L_Boot")));
		});

		It(TEXT("기본 게임모드가 ASnarehavenGameMode 다"), [this]()
		{
			// 흐름을 돌리는 BeginPlay 가 이 게임모드에 있다 — 다른 게임모드가 걸리면 전이가 통째로 사라진다.
			const FString GlobalDefaultGameMode = UGameMapsSettings::GetGlobalDefaultGameMode();
			TestTrue(FString::Printf(TEXT("기본 게임모드(%s)가 SnarehavenGameMode"), *GlobalDefaultGameMode),
				GlobalDefaultGameMode.Contains(TEXT("SnarehavenGameMode")));
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
