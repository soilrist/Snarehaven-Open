#include "Player/PlayerBodyAnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNodeBase.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimationPoseData.h"
#include "AnimationRuntime.h"
#include "BoneContainer.h"

namespace
{
	/** 섞기 알파를 1 쪽으로 민다. 섞는 시간이 0 이면 바로 1 (툭 바꾸기 — 예전 동작). */
	float StepAlpha(float Alpha, float BlendSeconds, float DeltaSeconds)
	{
		return BlendSeconds > KINDA_SMALL_NUMBER ? FMath::Min(1.f, Alpha + DeltaSeconds / BlendSeconds) : 1.f;
	}

	/** 클립 하나를 그 위치의 자세로 뽑는다 — `FAnimNode_SequencePlayer` 가 하는 것과 같은 호출이다. */
	void SamplePose(const FPlayerBodyClipState& Clip, FPoseContext& Out)
	{
		FAnimationPoseData PoseData(Out);
		Clip.Anim->GetAnimationPose(PoseData, FAnimExtractContext(static_cast<double>(Clip.Time), false, {}, Clip.bLoop));
	}

	/** 클립 재생 위치·섞기 상태를 게임 스레드에서 받아 평가 스레드에서 두 층을 섞는 프록시. */
	struct FPlayerBodyAnimInstanceProxy : public FAnimInstanceProxy
	{
		using FAnimInstanceProxy::FAnimInstanceProxy;

		virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override
		{
			FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);

			// 시간은 게임 스레드에서 인스턴스가 민다 — 캐릭터 틱이 건 클립과 같은 프레임의 값을 찍어 가야
			// 평가 스레드가 반쯤 바뀐 상태를 보지 않는다.
			UPlayerBodyAnimInstance* const Body = CastChecked<UPlayerBodyAnimInstance>(InAnimInstance);
			Body->Advance(DeltaSeconds);
			Snapshot = Body->GetBlendState();
			UpperBodyRootBone = Body->GetUpperBodyRootBone();
		}

		virtual bool Evaluate(FPoseContext& Output) override
		{
			if (Snapshot.Base.Anim == nullptr)
			{
				Output.ResetToRefPose();
				return true;
			}
			EvaluateLayers(Output);
			LiftHead(Output);
			return true;
		}

		/**
		 * ④ 머리 뼈를 머리 자기 좌우축으로 젖힌다(턱 들기). 좌우축은 참조 자세에서 몸의 좌우(컴포넌트 X — 몸은 +Y 를 본다)를
		 * 머리 공간으로 옮긴 것이라, 클립이 고개를 돌리거나 숙여도 「그 자세에서 턱을 조금 더 든」 모양이 된다.
		 */
		void LiftHead(FPoseContext& Output) const
		{
			if (Snapshot.HeadBone.IsNone() || FMath::IsNearlyZero(Snapshot.HeadLiftDegrees))
			{
				return;
			}
			const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
			const int32 MeshHead = Bones.GetPoseBoneIndexForBoneName(Snapshot.HeadBone);
			if (MeshHead == INDEX_NONE)
			{
				return;
			}
			const FCompactPoseBoneIndex Head = Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshHead));
			if (!Head.IsValid())
			{
				return;
			}
			FQuat RefRotation = FQuat::Identity;
			for (FCompactPoseBoneIndex Bone = Head; Bone.IsValid(); Bone = Bones.GetParentBoneIndex(Bone))
			{
				RefRotation = Bones.GetRefPoseTransform(Bone).GetRotation() * RefRotation;
			}
			const FVector LateralInHead = RefRotation.UnrotateVector(FVector::XAxisVector);
			const FQuat Lift(LateralInHead, FMath::DegreesToRadians(Snapshot.HeadLiftDegrees));
			FTransform& Local = Output.Pose[Head];
			Local.SetRotation((Local.GetRotation() * Lift).GetNormalized());
		}

		void EvaluateLayers(FPoseContext& Output)
		{

			// ① 기본 층: 지금 클립, 섞는 중이면 이전 클립과 교차.
			SamplePose(Snapshot.Base, Output);
			if (Snapshot.PreviousBase.Anim != nullptr && Snapshot.BaseAlpha < 1.f)
			{
				FPoseContext Previous(Output);
				SamplePose(Snapshot.PreviousBase, Previous);
				FAnimationPoseData OutData(Output);
				FAnimationPoseData PreviousData(Previous);
				FAnimationRuntime::BlendTwoPosesTogetherInPlace(OutData, PreviousData, Snapshot.BaseAlpha);
			}

			// ② 행동 층. 들어오는 중이면 지금 행동을 알파만큼, 빠지는 중이면 이전 행동을 (1-알파)만큼,
			// 행동끼리 바뀌는 중이면 둘을 교차한 자세를 통째로 얹는다.
			const bool bHasAction = Snapshot.Action.Anim != nullptr;
			const bool bHasPreviousAction = Snapshot.PreviousAction.Anim != nullptr && Snapshot.ActionAlpha < 1.f;
			if (!bHasAction && !bHasPreviousAction)
			{
				return;
			}
			FPoseContext ActionPose(Output);
			float ActionWeight = 1.f;
			if (bHasAction && bHasPreviousAction)
			{
				SamplePose(Snapshot.Action, ActionPose);
				FPoseContext Previous(Output);
				SamplePose(Snapshot.PreviousAction, Previous);
				FAnimationPoseData ActionData(ActionPose);
				FAnimationPoseData PreviousData(Previous);
				FAnimationRuntime::BlendTwoPosesTogetherInPlace(ActionData, PreviousData, Snapshot.ActionAlpha);
			}
			else if (bHasAction)
			{
				SamplePose(Snapshot.Action, ActionPose);
				ActionWeight = Snapshot.ActionAlpha;
			}
			else
			{
				SamplePose(Snapshot.PreviousAction, ActionPose);
				ActionWeight = 1.f - Snapshot.ActionAlpha;
			}

			// ③ 뼈별 가중치: 상체 시작 뼈와 그 자식은 행동, 나머지(골반·다리·뿌리)는 하체 몫만.
			// 컴팩트 포즈는 부모가 늘 먼저 나오므로 한 번 훑으면 상체 여부가 부모에서 내려온다.
			// 이름이 이 몸에 없으면 몸 전체를 상체로 친다 — 예전처럼 행동이 몸을 다 덮을 뿐 망가지지 않는다.
			const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
			const int32 NumBones = Bones.GetCompactPoseNumBones();
			const int32 MeshRoot = UpperBodyRootBone.IsNone() ? INDEX_NONE : Bones.GetPoseBoneIndexForBoneName(UpperBodyRootBone);
			const FCompactPoseBoneIndex UpperRoot = MeshRoot == INDEX_NONE
				? FCompactPoseBoneIndex(INDEX_NONE)
				: Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshRoot));
			const float LowerWeight = UpperRoot.IsValid() ? Snapshot.LowerBodyActionWeight : 1.f;
			UpperMask.SetNumUninitialized(NumBones);
			BoneWeights.SetNumUninitialized(NumBones);
			for (int32 Index = 0; Index < NumBones; ++Index)
			{
				const FCompactPoseBoneIndex Bone(Index);
				const FCompactPoseBoneIndex Parent = Bones.GetParentBoneIndex(Bone);
				UpperMask[Index] = Bone == UpperRoot || (Parent.IsValid() && UpperMask[Parent.GetInt()]);
				BoneWeights[Index] = ActionWeight * (UpperMask[Index] ? 1.f : LowerWeight);
			}

			FPoseContext Blended(Output);
			FAnimationPoseData BaseData(Output);
			FAnimationPoseData ActionData(ActionPose);
			FAnimationPoseData BlendedData(Blended);
			FAnimationRuntime::BlendTwoPosesTogetherPerBone(BaseData, ActionData, BoneWeights, BlendedData);
			Output.Pose.MoveBonesFrom(Blended.Pose);
			Output.Curve.MoveFrom(Blended.Curve);
			Output.CustomAttributes.MoveFrom(Blended.CustomAttributes);
			return;
		}

		FPlayerBodyBlendState Snapshot;
		FName UpperBodyRootBone;
		TArray<bool> UpperMask;
		TArray<float> BoneWeights;
	};
}

void FPlayerBodyClipState::Advance(float DeltaSeconds)
{
	if (Anim == nullptr)
	{
		return;
	}
	const float Length = Anim->GetPlayLength();
	const float PreviousTime = Time;
	Time += DeltaSeconds * PlayRate;
	// 버팀: 구간 끝을 넘는 순간 시작 쪽으로 감는다. 구간 앞(내밀기)은 그대로 지나간다.
	if (IsHolding() && PreviousTime < HoldEnd && Time >= HoldEnd)
	{
		Time = HoldStart + FMath::Fmod(Time - HoldStart, HoldEnd - HoldStart);
		bHoldWrapped = true;
	}
	if (Length <= KINDA_SMALL_NUMBER)
	{
		Time = 0.f;
	}
	else if (bLoop)
	{
		Time = FMath::Fmod(Time, Length);
		if (Time < 0.f)
		{
			Time += Length;
		}
	}
	else
	{
		Time = FMath::Clamp(Time, 0.f, Length);
	}
}

void UPlayerBodyAnimInstance::SetBaseClip(UAnimSequence* Anim, bool bLoop, float PlayRate, bool bRestart)
{
	if (Anim == nullptr)
	{
		return;
	}
	if (Anim != State.Base.Anim)
	{
		// ponytail: 섞는 도중 또 바뀌면 가장 오래된 클립은 버린다(그 몫만큼 한 프레임 튄다). 셋 이상 겹치는 일이 잦아지면 포즈 스냅샷 섞기로.
		const bool bBlend = State.Base.Anim != nullptr && BaseBlendSeconds > KINDA_SMALL_NUMBER;
		State.PreviousBase = bBlend ? State.Base : FPlayerBodyClipState();
		State.BaseAlpha = bBlend ? 0.f : 1.f;
		State.Base = FPlayerBodyClipState();
		State.Base.Anim = Anim;
	}
	else if (bRestart)
	{
		State.Base.Time = 0.f;
	}
	State.Base.PlayRate = PlayRate;
	State.Base.bLoop = bLoop;
}

void UPlayerBodyAnimInstance::SetActionClip(UAnimSequence* Anim, bool bLoop, float PlayRate, bool bRestart, bool bUpperBodyOnly)
{
	if (Anim == nullptr)
	{
		ClearActionClip();
		return;
	}
	bActionUpperBodyOnly = bUpperBodyOnly;
	if (Anim != State.Action.Anim)
	{
		const bool bBlend = ActionBlendSeconds > KINDA_SMALL_NUMBER;
		// 행동이 없다가 들어오면 하체 몫은 바로 목표로 둔다 — 들어오는 섞기는 행동 알파가 맡는다.
		if (State.Action.Anim == nullptr)
		{
			State.LowerBodyActionWeight = bUpperBodyOnly ? 0.f : 1.f;
		}
		State.PreviousAction = bBlend ? State.Action : FPlayerBodyClipState();
		State.ActionAlpha = bBlend ? 0.f : 1.f;
		State.Action = FPlayerBodyClipState();
		State.Action.Anim = Anim;
	}
	else if (bRestart)
	{
		State.Action.Time = 0.f;
	}
	State.Action.PlayRate = PlayRate;
	State.Action.bLoop = bLoop;
}

void UPlayerBodyAnimInstance::ClearActionClip()
{
	if (State.Action.Anim == nullptr)
	{
		return;
	}
	const bool bBlend = ActionBlendSeconds > KINDA_SMALL_NUMBER;
	State.PreviousAction = bBlend ? State.Action : FPlayerBodyClipState();
	State.ActionAlpha = bBlend ? 0.f : 1.f;
	State.Action = FPlayerBodyClipState();
}

void UPlayerBodyAnimInstance::SetActionHold(float StartSeconds, float EndSeconds)
{
	if (State.Action.Anim == nullptr)
	{
		return;
	}
	State.Action.HoldStart = StartSeconds;
	State.Action.HoldEnd = EndSeconds;
	State.Action.bHoldWrapped = false;
}

bool UPlayerBodyAnimInstance::ReleaseActionHold()
{
	if (!IsActionHolding())
	{
		return false;
	}
	const bool bWrapped = State.Action.bHoldWrapped;
	// 감은 뒤의 위치는 늘 구간 안이다 — 끝으로 옮기면 떨림 한 장(≤3cm) 차이로 바로 돌아오기가 시작된다.
	if (bWrapped)
	{
		State.Action.Time = State.Action.HoldEnd;
	}
	State.Action.HoldStart = State.Action.HoldEnd = 0.f;
	State.Action.bHoldWrapped = false;
	return bWrapped;
}

void UPlayerBodyAnimInstance::Advance(float DeltaSeconds)
{
	State.Base.Advance(DeltaSeconds);
	State.PreviousBase.Advance(DeltaSeconds);
	State.Action.Advance(DeltaSeconds);
	State.PreviousAction.Advance(DeltaSeconds);

	State.BaseAlpha = StepAlpha(State.BaseAlpha, BaseBlendSeconds, DeltaSeconds);
	State.ActionAlpha = StepAlpha(State.ActionAlpha, ActionBlendSeconds, DeltaSeconds);
	if (State.BaseAlpha >= 1.f)
	{
		State.PreviousBase = FPlayerBodyClipState();
	}
	if (State.ActionAlpha >= 1.f)
	{
		State.PreviousAction = FPlayerBodyClipState();
	}

	// 서 있다 걷기 시작하면(몸 전체 → 상체만) 다리가 행동에서 걷기로 같은 시간에 걸쳐 넘어간다.
	const float LowerTarget = bActionUpperBodyOnly ? 0.f : 1.f;
	State.LowerBodyActionWeight = ActionBlendSeconds > KINDA_SMALL_NUMBER
		? FMath::FInterpConstantTo(State.LowerBodyActionWeight, LowerTarget, DeltaSeconds, 1.f / ActionBlendSeconds)
		: LowerTarget;
}

void UPlayerBodyAnimInstance::SetBlendSeconds(float InBaseBlendSeconds, float InActionBlendSeconds)
{
	BaseBlendSeconds = FMath::Max(0.f, InBaseBlendSeconds);
	ActionBlendSeconds = FMath::Max(0.f, InActionBlendSeconds);
}

float UPlayerBodyAnimInstance::GetActionWeight() const
{
	if (State.Action.Anim != nullptr)
	{
		return State.ActionAlpha;
	}
	return State.PreviousAction.Anim != nullptr ? 1.f - State.ActionAlpha : 0.f;
}

float UPlayerBodyAnimInstance::GetShownClipTime() const
{
	return State.Action.Anim != nullptr ? State.Action.Time : State.Base.Time;
}

bool UPlayerBodyAnimInstance::IsShownClipLooping() const
{
	return State.Action.Anim != nullptr ? State.Action.bLoop : State.Base.bLoop;
}

void UPlayerBodyAnimInstance::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	Super::AddReferencedObjects(InThis, Collector);
	FPlayerBodyBlendState& Blend = CastChecked<UPlayerBodyAnimInstance>(InThis)->State;
	for (FPlayerBodyClipState* const Clip : { &Blend.Base, &Blend.PreviousBase, &Blend.Action, &Blend.PreviousAction })
	{
		Collector.AddReferencedObject(Clip->Anim, InThis);
	}
}

FAnimInstanceProxy* UPlayerBodyAnimInstance::CreateAnimInstanceProxy()
{
	return new FPlayerBodyAnimInstanceProxy(this);
}
