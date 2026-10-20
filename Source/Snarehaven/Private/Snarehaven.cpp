#include "Snarehaven.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/CoreDelegates.h"
#include "Modules/ModuleManager.h"
#include "UI/SnareScreenCloser.h"

DEFINE_LOG_CATEGORY(LogSnarehaven);

/**
 * ESC·뒤로 버튼 처리기를 게임 전체에 **한 번** 건다 (INBOX 2026-10-04 15:40).
 * 예전엔 캐릭터 입력 설정에서 걸어 타이틀 화면(캐릭터 없음)에선 ESC 가 새어 나가 에디터의 「플레이 중지」로 갔다.
 */
class FSnarehavenModule : public FDefaultGameModuleImpl
{
public:
	virtual void StartupModule() override
	{
		FCoreDelegates::OnPostEngineInit.AddLambda([this]()
		{
			if (FSlateApplication::IsInitialized())
			{
				ScreenCloser = MakeShared<FSnareScreenCloser>();
				FSlateApplication::Get().RegisterInputPreProcessor(ScreenCloser);
				UE_LOG(LogSnarehaven, Log, TEXT("[Esc] ESC·뒤로 버튼 처리기 등록"));
			}
		});
	}

	virtual void ShutdownModule() override
	{
		if (ScreenCloser.IsValid() && FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().UnregisterInputPreProcessor(ScreenCloser);
		}
		ScreenCloser.Reset();
	}

private:
	TSharedPtr<FSnareScreenCloser> ScreenCloser;
};

IMPLEMENT_PRIMARY_GAME_MODULE(FSnarehavenModule, Snarehaven, "Snarehaven");
