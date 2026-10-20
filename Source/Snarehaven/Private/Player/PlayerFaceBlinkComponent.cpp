#include "Player/PlayerFaceBlinkComponent.h"
#include "SnareCheckSwitch.h"
#include "Snarehaven.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Texture.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "TimerManager.h"

UPlayerFaceBlinkComponent::UPlayerFaceBlinkComponent()
{
	// 타이머로만 움직인다 — 매 프레임 할 일이 없다.
	PrimaryComponentTick.bCanEverTick = false;
}

void UPlayerFaceBlinkComponent::BeginPlay()
{
	Super::BeginPlay();
	if (GetNetMode() == NM_DedicatedServer || ClosedEyesTexture.IsNull())
	{
		return;
	}
	// 3층 검사 전용 — `-SnareEyesClosed` 면 첫 깜빡임을 1초 뒤에 하고 끝까지 감은 채 둔다(0.12초를 캡처로 잡을 수 없다).
	if (SnareCheckSwitch::Param(TEXT("SnareEyesClosed")))
	{
		MinOpenSeconds = MaxOpenSeconds = 1.f;
		ClosedSeconds = 3600.f;
		UE_LOG(LogSnarehaven, Log, TEXT("검사용 눈 감기 (-SnareEyesClosed) — 1초 뒤 감고 그대로 둔다"));
	}
	ScheduleNextBlink();
}

void UPlayerFaceBlinkComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (const UWorld* const World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(BlinkTimerHandle);
		World->GetTimerManager().ClearTimer(OpenTimerHandle);
	}
	Super::EndPlay(EndPlayReason);
}

void UPlayerFaceBlinkComponent::ScheduleNextBlink()
{
	if (UWorld* const World = GetWorld())
	{
		const float OpenSeconds = FMath::FRandRange(MinOpenSeconds, FMath::Max(MinOpenSeconds, MaxOpenSeconds));
		World->GetTimerManager().SetTimer(BlinkTimerHandle, this, &UPlayerFaceBlinkComponent::HandleBlinkTimer,
			FMath::Max(OpenSeconds, 0.01f), false);
	}
}

void UPlayerFaceBlinkComponent::HandleBlinkTimer()
{
	// 몸이 아직 자리표시라 못 감아도 다음 차례는 잡는다 — 몸은 나중에 올라온다(복제본).
	BlinkNow();
	ScheduleNextBlink();
}

bool UPlayerFaceBlinkComponent::BlinkNow()
{
	// 아픈·쓰러진 얼굴은 눈이 이미 그려져 있다 — 감지 않는다 (DESIGN 2.2 「표정 3종」).
	if (Expression == ESnareFaceExpression::Hurt || Expression == ESnareFaceExpression::Down)
	{
		return false;
	}
	UWorld* const World = GetWorld();
	UMaterialInstanceDynamic* const Material = ResolveFaceMaterial();
	if (World == nullptr || Material == nullptr)
	{
		return false;
	}
	if (LoadedClosedEyesTexture == nullptr)
	{
		LoadedClosedEyesTexture = ClosedEyesTexture.LoadSynchronous();
		if (LoadedClosedEyesTexture == nullptr)
		{
			UE_LOG(LogSnarehaven, Warning, TEXT("눈 깜빡임: 감은 짝 %s 를 못 불러왔다"), *ClosedEyesTexture.ToString());
			return false;
		}
	}
	Material->SetTextureParameterValue(FaceTextureParameterName, LoadedClosedEyesTexture);
	bEyesClosed = true;
	World->GetTimerManager().SetTimer(OpenTimerHandle, this, &UPlayerFaceBlinkComponent::OpenEyes,
		FMath::Max(ClosedSeconds, 0.01f), false);
	return true;
}

void UPlayerFaceBlinkComponent::OpenEyes()
{
	bEyesClosed = false;
	ApplyExpressionTexture();
}

void UPlayerFaceBlinkComponent::NoteAte(float Heal)
{
	(Heal < 0.f ? SecondsSincePoison : SecondsSinceAte) = 0.f;
}

void UPlayerFaceBlinkComponent::TickExpression(float DeltaSeconds, float Health, float MaxHealth, bool bDowned)
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	SecondsSincePoison += DeltaSeconds;
	SecondsSinceAte += DeltaSeconds;
	const ESnareFaceExpression Wanted =
		SnareFaceExpression::For(Health, MaxHealth, bDowned, SecondsSincePoison, SecondsSinceAte);
	if (!SetExpression(Wanted) && Expression != ESnareFaceExpression::Neutral)
	{
		// 몸이 바뀌어 머티리얼이 새로 올라왔을 수 있다 — 표정 그림이 아직 끼워져 있는지 다시 맞춘다.
		ApplyExpressionTexture();
	}
}

bool UPlayerFaceBlinkComponent::SetExpression(ESnareFaceExpression NewExpression)
{
	if (NewExpression == Expression)
	{
		return false;
	}
	Expression = NewExpression;
	if (Expression == ESnareFaceExpression::Hurt || Expression == ESnareFaceExpression::Down)
	{
		// 감고 있던 눈은 바로 뜬다 — 뜨는 타이머가 나중에 와도 같은 그림이라 해가 없지만 상태를 먼저 맞춘다.
		if (const UWorld* const World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(OpenTimerHandle);
		}
		bEyesClosed = false;
	}
	ApplyExpressionTexture();
	UE_LOG(LogSnarehaven, Verbose, TEXT("[Face] 표정 %s"), SnareFaceExpression::NameOf(Expression));
	return true;
}

UTexture* UPlayerFaceBlinkComponent::GetExpressionTexture()
{
	auto Load = [](const TSoftObjectPtr<UTexture>& Soft, TObjectPtr<UTexture>& Loaded) -> UTexture*
	{
		if (Loaded == nullptr && !Soft.IsNull())
		{
			Loaded = Soft.LoadSynchronous();
			if (Loaded == nullptr)
			{
				UE_LOG(LogSnarehaven, Warning, TEXT("표정: 그림 %s 를 못 불러왔다"), *Soft.ToString());
			}
		}
		return Loaded;
	};
	UTexture* Picked = nullptr;
	switch (Expression)
	{
	case ESnareFaceExpression::Hurt:
		Picked = Load(HurtTexture, LoadedHurtTexture);
		break;
	case ESnareFaceExpression::Down:
		Picked = Load(DownTexture, LoadedDownTexture);
		break;
	case ESnareFaceExpression::Happy:
		Picked = Load(HappyTexture, LoadedHappyTexture);
		break;
	default:
		break;
	}
	return Picked != nullptr ? Picked : OpenEyesTexture.Get();
}

void UPlayerFaceBlinkComponent::ApplyExpressionTexture()
{
	if (bEyesClosed)
	{
		return; // 깜빡이는 중 — 뜰 때 이 함수가 다시 불린다.
	}
	UMaterialInstanceDynamic* const Material = ResolveFaceMaterial();
	if (Material == nullptr)
	{
		return;
	}
	if (UTexture* const Face = GetExpressionTexture())
	{
		UTexture* Current = nullptr;
		if (!Material->GetTextureParameterValue(FHashedMaterialParameterInfo(FaceTextureParameterName), Current) || Current != Face)
		{
			Material->SetTextureParameterValue(FaceTextureParameterName, Face);
		}
	}
}

UMaterialInstanceDynamic* UPlayerFaceBlinkComponent::ResolveFaceMaterial()
{
	const ACharacter* const Owner = Cast<ACharacter>(GetOwner());
	USkeletalMeshComponent* const Body = Owner != nullptr ? Owner->GetMesh() : nullptr;
	if (!IsValid(Body) || Body->GetSkeletalMeshAsset() == nullptr)
	{
		return nullptr;
	}
	UMaterialInterface* const Current = Body->GetMaterial(0);
	if (Current == nullptr)
	{
		return nullptr;
	}
	if (IsValid(FaceMaterial) && Current == FaceMaterial)
	{
		return FaceMaterial;
	}
	// 몸이 바뀌었거나 처음이다 — 지금 칸의 원래 그림을 읽어 두고(뜰 때 되돌릴 것) 동적 인스턴스로 바꾼다.
	UTexture* Open = nullptr;
	if (!Current->GetTextureParameterValue(FHashedMaterialParameterInfo(FaceTextureParameterName), Open) || Open == nullptr)
	{
		return nullptr;
	}
	OpenEyesTexture = Open;
	FaceMaterial = Body->CreateDynamicMaterialInstance(0);
	return FaceMaterial;
}
