// 감독 보정 2026-10-06 02:15 2층 검사 — 전기톱은 누르는 동안 내민 채 버틴다 (DESIGN 3.1 「전기톱 누르는 동안 버팀」).
// 레퍼런스 원본 = 누르는 동안 톱을 수평으로 내민 채 떨며 버티고, 떼면 든 자세로 돌아온다(`docs/reference/character_motion.md` 「전기톱 — 쓸 때」).
// 시험 월드는 틱이 없어 타이머·캐릭터 틱·몸 애님 시간을 1/30초씩 손으로 민다(쓰기 클립이 30fps).
// ① 누른 채 0.5초·2.5초 — 보이는 행동 클립이 쓰기 클립이고 재생 위치가 행 버팀 구간 안(= 같은 수평 자세)
// ② 누른 채 3초여도 휘두르기(판정)는 한 번 — 한 번 누름 = 한 번(DESIGN 3.1 「공격 간격」)
// ③ 떼고 0.15초 — 쓰기 클립 돌아오기 구간(버팀 끝 뒤), 끝 구간 길이 + 0.2초 뒤 — 든 자세 클립으로 돌아옴
// ④ 짧게 누름(0.1초) — 버팀 끝 전에 떼면 감지 않고 예전처럼 한 번 끝까지(1.1초에 재생 위치 1.0초 넘음)
// ⑤ 역검증: 버팀 칸이 빈 고급 곡괭이(같은 「정해진 길이」 행)는 누른 채 2.5초에 이미 쓰기 클립이 아니다 — 버팀이 행 칸으로만 켜진다.
// 실행: UnrealEditor <uproject> -ExecCmds="Automation RunTests Snarehaven.Equipment.Chainsaw; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "SnarehavenCharacter.h"
#include "Combat/MeleeAttackComponent.h"
#include "Data/ItemRow.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Equipment/EquipmentComponent.h"
#include "Player/PlayerBodyAnimInstance.h"
#include "Snarehaven.h"
#include "TimerManager.h"

namespace SnarehavenChainsawPressHoldTest
{
	constexpr float FrameSeconds = 1.f / 30.f;
	constexpr float ClipTimeSlack = 0.02f;   // 재생 위치 비교 여유 — 한 장(0.033초)보다 작게

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

	/** 타이머 → 캐릭터 틱(클립 고르기) → 몸 애님 시간 순으로 Seconds 만큼 민다 — 실제 프레임과 같은 순서. */
	void Step(UWorld* World, ASnarehavenCharacter* Player, UPlayerBodyAnimInstance* BodyAnim, float Seconds)
	{
		for (float Elapsed = 0.f; Elapsed < Seconds - KINDA_SMALL_NUMBER; Elapsed += FrameSeconds)
		{
			++GFrameCounter;   // 타이머 매니저는 한 프레임에 한 번만 돈다 — 프레임 번호를 넘겨야 다음 Tick 이 먹는다
			World->GetTimerManager().Tick(FrameSeconds);
			static_cast<AActor*>(Player)->Tick(FrameSeconds);   // 캐릭터 쪽 Tick 은 protected — 액터 공개 입구로
			BodyAnim->Advance(FrameSeconds);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenChainsawPressHoldTest, "Snarehaven.Equipment.Chainsaw.PressHoldKeepsThrust",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenChainsawPressHoldTest::RunTest(const FString& Parameters)
{
	using namespace SnarehavenChainsawPressHoldTest;

	UWorld* const World = MakeWorld();
	ASnarehavenCharacter* const Player = World->SpawnActor<ASnarehavenCharacter>();
	UEquipmentComponent* const Equipment = Player ? Player->GetEquipment() : nullptr;
	if (!TestNotNull(TEXT("장비 컴포넌트가 있다"), Equipment)
		|| !TestTrue(TEXT("사람 메시를 올렸다"), Player->ApplyPlayerBody())
		|| !TestTrue(TEXT("전기톱을 들었다"), Equipment->EquipItemByItemID(FName(TEXT("item.chainsaw")))))
	{
		DestroyWorld(World);
		return false;
	}
	UPlayerBodyAnimInstance* const BodyAnim = Player->GetBodyAnimInstance();
	const UMeleeAttackComponent* const Melee = Player->FindComponentByClass<UMeleeAttackComponent>();
	const FItemRow* const Row = Equipment->GetEquippedRow(EEquipSlot::Tool);
	UAnimSequence* const Use = Row ? Row->UseAnim.LoadSynchronous() : nullptr;
	UAnimSequence* const Hold = Row ? Row->HoldPoseAnim.LoadSynchronous() : nullptr;
	if (!TestNotNull(TEXT("코드 몸 애님 인스턴스"), BodyAnim) || !TestNotNull(TEXT("공격 컴포넌트"), Melee)
		|| !TestNotNull(TEXT("전기톱 쓰기 클립"), Use) || !TestNotNull(TEXT("전기톱 든 자세 클립"), Hold))
	{
		DestroyWorld(World);
		return false;
	}
	const float Length = Use->GetPlayLength();
	const float HoldStart = Row->UseHoldStartSeconds;
	const float HoldEnd = Row->UseHoldEndSeconds;
	if (!TestTrue(FString::Printf(TEXT("전기톱 행 버팀 구간 0 < %.2f < %.2f < 클립 %.2f초"), HoldStart, HoldEnd, Length),
			0.f < HoldStart && HoldStart < HoldEnd && HoldEnd < Length))
	{
		DestroyWorld(World);
		return false;
	}
	Step(World, Player, BodyAnim, 0.5f);   // 든 자세로 자리 잡기

	const auto InHold = [&](const TCHAR* Label)
		{
			const float Time = BodyAnim->GetShownClipTime();
			const bool bUse = BodyAnim->GetActionClip() == Use;
			AddInfo(FString::Printf(TEXT("[PressHold] %s — 클립 %s · 위치 %.3f초 · 버팀 %s"), Label,
				BodyAnim->GetActionClip() ? *BodyAnim->GetActionClip()->GetName() : TEXT("없음"), Time,
				BodyAnim->IsActionHolding() ? TEXT("켬") : TEXT("끔")));
			TestTrue(FString::Printf(TEXT("① %s 쓰기 클립 %s 이 보이고 위치 %.3f 가 버팀 %.2f~%.2f 안"), Label, *Use->GetName(), Time,
				HoldStart, HoldEnd), bUse && Time >= HoldStart - ClipTimeSlack && Time <= HoldEnd + ClipTimeSlack);
		};

	// ①② 누른 채 3초
	const int32 SwingsBefore = Melee->GetSwingCount();
	Player->BeginAttack();
	TestTrue(TEXT("누름 상태가 켜졌다"), Player->IsUseInputHeld());
	Step(World, Player, BodyAnim, 0.5f);
	InHold(TEXT("누른 채 0.5초"));
	Step(World, Player, BodyAnim, 2.0f);
	InHold(TEXT("누른 채 2.5초"));
	TestTrue(TEXT("① 2.5초에도 버팀 구간이 걸려 있다"), BodyAnim->IsActionHolding());
	Step(World, Player, BodyAnim, 0.5f);
	TestEqual(TEXT("② 누른 채 3초 동안 휘두르기(판정)는 한 번"), Melee->GetSwingCount() - SwingsBefore, 1);

	// ③ 떼면 돌아온다
	Player->EndAttack();
	Step(World, Player, BodyAnim, 0.15f);
	const float ReturnTime = BodyAnim->GetShownClipTime();
	TestTrue(FString::Printf(TEXT("③ 떼고 0.15초 — 쓰기 클립 돌아오기 구간(위치 %.3f > %.2f)"), ReturnTime, HoldEnd),
		BodyAnim->GetActionClip() == Use && ReturnTime > HoldEnd && !BodyAnim->IsActionHolding());
	Step(World, Player, BodyAnim, Length - HoldEnd + 0.2f);
	TestTrue(FString::Printf(TEXT("③ 돌아오기 뒤 든 자세 %s (지금 %s)"), *Hold->GetName(),
		BodyAnim->GetActionClip() ? *BodyAnim->GetActionClip()->GetName() : TEXT("없음")), BodyAnim->GetActionClip() == Hold);

	// ④ 짧게 누름 = 예전 그대로
	Step(World, Player, BodyAnim, Length);   // 간격(쿨다운) 넉넉히 지나기
	Player->BeginAttack();
	Step(World, Player, BodyAnim, 0.1f);
	Player->EndAttack();
	Step(World, Player, BodyAnim, 1.0f);
	const float TapTime = BodyAnim->GetShownClipTime();
	TestTrue(FString::Printf(TEXT("④ 0.1초 누름 뒤 1.1초 — 쓰기 클립이 감지 않고 이어 감(위치 %.3f > 1.0)"), TapTime),
		BodyAnim->GetActionClip() == Use && TapTime > 1.f);
	Step(World, Player, BodyAnim, Length - 1.1f + 0.2f);
	TestTrue(TEXT("④ 한 번 끝까지 돈 뒤 든 자세"), BodyAnim->GetActionClip() == Hold);

	// ⑤ 역검증 — 버팀 칸이 빈 고급 곡괭이
	if (TestTrue(TEXT("고급 곡괭이를 들었다"), Equipment->EquipItemByItemID(FName(TEXT("item.pickaxe_advanced")))))
	{
		const FItemRow* const PickRow = Equipment->GetEquippedRow(EEquipSlot::Tool);
		UAnimSequence* const PickUse = PickRow ? PickRow->UseAnim.LoadSynchronous() : nullptr;
		Step(World, Player, BodyAnim, Length);
		Player->BeginAttack();
		Step(World, Player, BodyAnim, 2.5f);
		TestTrue(FString::Printf(TEXT("⑤ 버팀 칸 없는 행은 누른 채 2.5초에 쓰기 클립 %s 이 끝났다 (지금 %s)"),
			PickUse ? *PickUse->GetName() : TEXT("없음"), BodyAnim->GetActionClip() ? *BodyAnim->GetActionClip()->GetName() : TEXT("없음")),
			PickUse != nullptr && BodyAnim->GetActionClip() != PickUse);
		Player->EndAttack();
	}

	DestroyWorld(World);
	return true;
}

#endif
