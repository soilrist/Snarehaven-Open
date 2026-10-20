// ROADMAP M1-10e1 2층 검사 — 화면 버튼의 클릭이 뷰포트의 공격으로 새지 않는지.
// 이 검사가 잡는 것: ① HUD 를 띄운 뒤 거는 입력 모드에서 마우스 커서가 꺼지는 것
//   (커서가 없으면 `GameOnly` 로 돌아간 것이고, 그러면 HUD `포획` 버튼 클릭이 `IA_Attack` 으로도 들어가
//    5타째가 버튼보다 먼저 나가 동물이 죽는다 — 감독 재현 2026-09-20 13:00 (2))
// ② 레벨 진입 때부터 눌려 있던 클릭을 무시하는 규칙이 사라지거나(진입하자마자 자동 공격), 반대로
//    키를 떼도 안 풀려 사람이 영영 못 때리게 되는 것
// ③ 그 규칙이 **화면 공격 버튼 길(`BeginAttack`)까지** 막는 것 — 버튼은 사람이 실제로 누른 것이다.
// `FInputModeGameAndUI` 자체는 게터가 없어 유닛에서 못 읽는다(뷰포트도 없다) — 커서 플래그까지가 여기 몫이고,
// 실제 클릭 순서는 3층·사람 확인이 본다 (STATUS #0054).
// 실행: UnrealEditor-Cmd <uproject 절대경로> -ExecCmds="Automation RunTests Snarehaven; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Combat/MeleeAttackComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/InputSettings.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/TouchInterface.h"
#include "SnarehavenCharacter.h"

namespace
{
	UWorld* CreateTestWorld()
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
		FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
		WorldContext.SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		World->BeginPlay();
		return World;
	}

	void DestroyTestWorld(UWorld* World)
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenHudInputModeCursorTest, "Snarehaven.Input.Hud.ShowsCursorForUmgButtons",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenHudInputModeCursorTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	APlayerController* PlayerController = World->SpawnActor<APlayerController>();
	if (!TestNotNull(TEXT("컨트롤러 스폰"), PlayerController))
	{
		DestroyTestWorld(World);
		return false;
	}

	// 모바일에서 넘어온 기본값은 커서가 꺼진 상태다 — HUD 를 띄우면 켜져야 버튼을 누를 수 있다.
	PlayerController->SetShowMouseCursor(false);
	ASnarehavenCharacter::ApplyHudInputMode(PlayerController);
	TestTrue(TEXT("HUD 입력 모드를 걸면 마우스 커서가 켜진다"), PlayerController->bShowMouseCursor);

	// 컨트롤러가 없어도 죽지 않아야 한다 (게스트 폰은 BeginPlay 때 컨트롤러가 아직 없을 수 있다).
	ASnarehavenCharacter::ApplyHudInputMode(nullptr);

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenLevelEntryAttackInputTest,
	"Snarehaven.Input.LevelEntry.IgnoresHeldAttackUntilRelease",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenLevelEntryAttackInputTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Character =
		World->SpawnActor<ASnarehavenCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("캐릭터 스폰"), Character))
	{
		DestroyTestWorld(World);
		return false;
	}

	UMeleeAttackComponent* const MeleeAttack = Character->FindComponentByClass<UMeleeAttackComponent>();
	if (!TestNotNull(TEXT("근접 공격 컴포넌트"), MeleeAttack))
	{
		DestroyTestWorld(World);
		return false;
	}

	// 평소엔 잠겨 있지 않다 — 레벨에 들어오자마자 누른 첫 클릭도 그대로 먹어야 한다.
	TestTrue(TEXT("기본은 공격 입력이 열려 있다"), Character->IsAttackInputArmed());

	// 로비 버튼의 클릭이 눌린 채 넘어온 상황.
	Character->DisarmAttackInputUntilRelease();
	TestFalse(TEXT("잠근 뒤엔 닫혀 있다"), Character->IsAttackInputArmed());

	Character->HandleAttackInputStarted();
	TestFalse(TEXT("잠긴 동안 들어온 IA_Attack 누름은 공격을 시작하지 않는다"), MeleeAttack->IsAttacking());

	// 화면 공격 버튼은 사람이 이 레벨에서 실제로 누른 것이라 잠금과 상관없이 먹는다 (M1-10c 의 같은 길).
	Character->BeginAttack();
	TestTrue(TEXT("화면 공격 버튼 길은 잠금에 막히지 않는다"), MeleeAttack->IsAttacking());
	Character->EndAttack();

	// 키를 떼면(첫 Completed) 풀린다.
	Character->HandleAttackInputCompleted();
	TestTrue(TEXT("키를 떼면 공격 입력이 다시 열린다"), Character->IsAttackInputArmed());
	TestFalse(TEXT("뗌은 공격을 멈춘다"), MeleeAttack->IsAttacking());

	Character->HandleAttackInputStarted();
	TestTrue(TEXT("풀린 뒤의 누름은 그대로 공격을 시작한다"), MeleeAttack->IsAttacking());
	Character->EndAttack();

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenTouchInterfaceMoveStickTest,
	"Snarehaven.Input.Touch.DefaultInterfaceHasMoveStick",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// ROADMAP M4-04 — 모바일에서 움직이는 길의 **터치 쪽 절반**을 본다.
// IMC 쪽 절반(IA_Move <- Gamepad_Left2D)은 `Snarehaven.Character.Input.*` 가 이미 본다. 그 짝인
// `Gamepad_LeftX`/`LeftY` 를 내보내는 스틱이 터치 인터페이스에 없으면, 매핑이 멀쩡해도 모바일에서는
// 손가락이 아무 데도 안 닿는다. 3층 `-SnareTouch=stick` 도 이 스틱을 이름(키)으로 찾는다.
bool FSnarehavenTouchInterfaceMoveStickTest::RunTest(const FString& Parameters)
{
	const UInputSettings* const InputSettings = UInputSettings::GetInputSettings();
	if (!TestNotNull(TEXT("입력 설정"), InputSettings))
	{
		return false;
	}

	UTouchInterface* const TouchInterface = Cast<UTouchInterface>(InputSettings->DefaultTouchInterface.TryLoad());
	if (!TestNotNull(TEXT("DefaultTouchInterface 에셋을 읽는다"), TouchInterface))
	{
		AddError(FString::Printf(TEXT("DefaultInput.ini 의 DefaultTouchInterface = %s"),
			*InputSettings->DefaultTouchInterface.ToString()));
		return false;
	}

	const FTouchInputControl* const MoveStick = TouchInterface->Controls.FindByPredicate(
		[](const FTouchInputControl& Control) { return Control.MainInputKey == EKeys::Gamepad_LeftX; });
	if (!TestNotNull(TEXT("Gamepad_LeftX 를 내보내는 이동 스틱이 있다"), MoveStick))
	{
		return false;
	}

	TestEqual(TEXT("그 스틱의 세로축은 Gamepad_LeftY 다"), MoveStick->AltInputKey, EKeys::Gamepad_LeftY);
	TestFalse(TEXT("이동 스틱은 버튼이 아니라 스틱이다"), MoveStick->bTreatAsButton);
	TestTrue(TEXT("스틱에 누를 수 있는 크기가 있다"),
		MoveStick->VisualSize.X > 0.f && MoveStick->VisualSize.Y > 0.f);

	// 숫자는 우리가 정한 게 아니라 엔진 에셋 값이다 — 판정하지 않고 적어만 둔다.
	AddInfo(FString::Printf(TEXT("이동 스틱 중심 (%.3f, %.3f) · 크기 (%.3f, %.3f) · 되묻는 영역 (%.3f, %.3f)"),
		MoveStick->Center.X, MoveStick->Center.Y, MoveStick->VisualSize.X, MoveStick->VisualSize.Y,
		MoveStick->InteractionSize.X, MoveStick->InteractionSize.Y));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
