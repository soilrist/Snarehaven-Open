#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "PlayerBodyAnimInstance.generated.h"

class UAnimSequence;

/** 몸 한 층에서 도는 클립 하나 — 클립·재생 위치·속도·되풀이. */
struct FPlayerBodyClipState
{
	UAnimSequence* Anim = nullptr;
	float Time = 0.f;
	float PlayRate = 1.f;
	bool bLoop = true;
	/** 버팀 구간 — 끝 > 시작이면 끝에 닿을 때 시작으로 감는다(누르는 동안 전기톱 내밀기). 클립이 바뀌면 사라진다. */
	float HoldStart = 0.f;
	float HoldEnd = 0.f;
	/** 버팀 구간 끝에 닿아 한 번이라도 감았나 — 감기 전에 놓으면 원래 시간 그대로 이어 간다. */
	bool bHoldWrapped = false;

	bool IsHolding() const { return HoldEnd > HoldStart; }

	/** 재생 위치를 DeltaSeconds × 속도만큼 민다. 버팀 구간이면 그 안에서 감고, 되풀이면 감고, 아니면 끝에 멈춘다. */
	void Advance(float DeltaSeconds);
};

/** 평가에 넘기는 한 프레임치 섞기 상태 (게임 스레드에서 찍어 워커로 복사한다). */
struct FPlayerBodyBlendState
{
	FPlayerBodyClipState Base;
	FPlayerBodyClipState PreviousBase;
	/** 0 = 이전 기본 클립만, 1 = 지금 기본 클립만. */
	float BaseAlpha = 1.f;

	/** 클립이 없으면(nullptr) "행동 없음" = 기본 층 자세다. */
	FPlayerBodyClipState Action;
	FPlayerBodyClipState PreviousAction;
	/** 0 = 이전 행동(또는 기본 층)만, 1 = 지금 행동(또는 기본 층)만. */
	float ActionAlpha = 1.f;

	/** 행동이 하체(상체 시작 뼈 바깥)에 얼마나 먹는가. 0 = 하체는 기본 층(걷기)을 그대로, 1 = 몸 전체가 행동. */
	float LowerBodyActionWeight = 1.f;

	/** 두 층을 섞은 뒤 머리 뼈를 턱 드는 쪽으로 젖히는 각도(도). 0 이면 클립 그대로. */
	FName HeadBone;
	float HeadLiftDegrees = 0.f;
};

/**
 * 플레이어 몸 애니메이션 (M18-10). AnimBP 에셋 없이 코드로 두 층을 섞는다.
 *  - 기본 층: 대기·걷기·달리기·물·탈것·배·낚시·쓰러짐. 클립이 바뀌면 `BaseBlendSeconds` 동안 교차 섞기.
 *  - 행동 층: 맞음·아이템 행동·휘두르기. 움직이는 중엔 상체(`UpperBodyRootBone` 아래)에만 얹고
 *    하체는 기본 층을 둔다 — 걸으며 휘둘러도 다리가 걷는다. 시작·끝은 `ActionBlendSeconds` 동안 섞는다.
 *  - 마지막에 머리 뼈를 머리 자기 좌우축으로 `HeadLiftDegrees` 만큼 젖힌다 — 클립을 갈아도 따라온다.
 * 클립 고르기는 여전히 `ASnarehavenCharacter::UpdateBodyAnimation` 몫이고, 이 인스턴스는 받은 것을 섞기만 한다.
 */
// 섞는 클립 넷(지금·이전 × 기본·행동)은 `AddReferencedObjects` 로 GC 에 알린다 — 손에 든 것의 클립은 표의
// 소프트 참조를 `LoadSynchronous` 로 받은 것이라 아무도 붙들지 않아, 휘두르는 도중 GC 가 거둬 가면 해제된 클립을
// 읽어 죽었다(루프#437, 몽둥이 휘두르기 30초쯤 SIGSEGV).
UCLASS(Transient)
class SNAREHAVEN_API UPlayerBodyAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	/** 몸 전체 기본 클립을 건다. 다른 클립이면 교차 섞기를 시작하고, 같은 클립이면 속도만 따라간다. */
	void SetBaseClip(UAnimSequence* Anim, bool bLoop, float PlayRate, bool bRestart);

	/** 행동 클립을 건다. bUpperBodyOnly 면 하체는 기본 층을 유지한다(그 전환도 섞는다). */
	void SetActionClip(UAnimSequence* Anim, bool bLoop, float PlayRate, bool bRestart, bool bUpperBodyOnly);

	/** 행동을 내린다 — `ActionBlendSeconds` 동안 기본 층으로 섞여 돌아간다. */
	void ClearActionClip();

	/** 지금 행동 클립에 버팀 구간을 건다 — 재생 위치가 끝에 닿을 때마다 시작으로 감는다. 행동이 없으면 아무것도 안 한다. */
	void SetActionHold(float StartSeconds, float EndSeconds);

	/**
	 * 버팀을 놓는다. 이미 감았으면 재생 위치를 구간 끝(돌아오기 첫 장)으로 옮겨 거기서 끝까지 잇고 true,
	 * 감기 전(내밀기·첫 버팀 중)이면 위치는 그대로 두고 구간만 지워 한 번 누름과 같게 끝까지 돌고 false.
	 */
	bool ReleaseActionHold();

	bool IsActionHolding() const { return State.Action.Anim != nullptr && State.Action.IsHolding(); }

	/** 시간을 민다. 프록시가 게임 스레드에서 애니메이션 갱신 첫머리에 부른다. */
	void Advance(float DeltaSeconds);

	void SetBlendSeconds(float InBaseBlendSeconds, float InActionBlendSeconds);
	void SetUpperBodyRootBone(FName InBoneName) { UpperBodyRootBone = InBoneName; }

	/** 모든 클립 위에 머리를 이만큼 젖힌다 — 위에서 내려다보는 게임 카메라에 얼굴이 보이게 (DESIGN 3.3). */
	void SetHeadLift(FName InHeadBone, float InDegrees)
	{
		State.HeadBone = InHeadBone;
		State.HeadLiftDegrees = InDegrees;
	}
	float GetHeadLiftDegrees() const { return State.HeadLiftDegrees; }

	const FPlayerBodyBlendState& GetBlendState() const { return State; }
	FName GetUpperBodyRootBone() const { return UpperBodyRootBone; }
	UAnimSequence* GetBaseClip() const { return State.Base.Anim; }
	UAnimSequence* GetActionClip() const { return State.Action.Anim; }
	float GetBaseBlendAlpha() const { return State.BaseAlpha; }
	float GetLowerBodyActionWeight() const { return State.LowerBodyActionWeight; }

	/** 지금 행동 클립이 몸에 먹는 몫(0~1). 행동이 없고 빠져나가는 중이면 남은 몫. */
	float GetActionWeight() const;

	/** 지금 보이는 맨 위 클립(행동이 있으면 행동, 없으면 기본)의 재생 위치·되풀이 — 추적 로그·시험용. */
	float GetShownClipTime() const;
	bool IsShownClipLooping() const;

	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;

private:
	FPlayerBodyBlendState State;
	float BaseBlendSeconds = 0.f;
	float ActionBlendSeconds = 0.f;
	FName UpperBodyRootBone;
	bool bActionUpperBodyOnly = false;
};
