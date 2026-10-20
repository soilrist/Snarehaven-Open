#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Player/SnareFaceExpression.h"
#include "PlayerFaceBlinkComponent.generated.h"

class UMaterialInstanceDynamic;
class UTexture;

// INBOX 결함 2026-10-04 12:40 — 얼굴 그림 눈 깜빡임.
// 레퍼런스 얼굴은 머리 앞면에 붙는 그림이고 **깜빡임 짝**이 따로 있다(`docs/reference/`).
// 우리 얼굴도 몸 바탕색 그림(`T_Player_Body_BaseColor`)에 그려져 있어, 같은 자리에 눈만 감은 짝
// (`T_Player_Body_BaseColor_Blink`, `tools/unreal/make_player_face_texture.py`)을 잠깐 바꿔 끼운다.
// 몸 머티리얼 0번 칸을 동적 인스턴스로 바꾸고 그 텍스처 파라미터만 갈아 끼운다 — 뼈·메시는 안 건드린다.
// 화면만이다: 복제하지 않고(기기마다 제 박자로 깜빡여도 된다) 데디 서버에서는 돌지 않는다.
// 숫자는 DESIGN 3.3 몸 표 「눈 깜빡임」 행.
UCLASS(ClassGroup = (Snarehaven), meta = (BlueprintSpawnableComponent))
class SNAREHAVEN_API UPlayerFaceBlinkComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPlayerFaceBlinkComponent();

	/** 눈 감은 짝 그림. 비었거나 못 읽으면 깜빡이지 않는다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Face")
	TSoftObjectPtr<UTexture> ClosedEyesTexture;

	/** 표정 짝 그림 (M85-03) — 싫음·기절·웃음. 비었거나 못 읽으면 그 표정은 기본 그림 그대로다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Face")
	TSoftObjectPtr<UTexture> HurtTexture;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Face")
	TSoftObjectPtr<UTexture> DownTexture;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Face")
	TSoftObjectPtr<UTexture> HappyTexture;

	/** 몸 머티리얼에서 바탕색 그림을 받는 텍스처 파라미터 이름(Interchange glTF 머티리얼). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Face")
	FName FaceTextureParameterName = TEXT("BaseColorTexture");

	/** 눈을 뜨고 있는 시간 — 이 둘 사이에서 매번 고른다(초). DESIGN 3.3 「눈 깜빡임」. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Face")
	float MinOpenSeconds = 2.5f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Face")
	float MaxOpenSeconds = 5.5f;

	/** 한 번 감고 있는 시간(초). DESIGN 3.3 「눈 깜빡임」. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Face")
	float ClosedSeconds = 0.12f;

	/** 지금 한 번 감는다 — `ClosedSeconds` 뒤 저절로 뜬다. 몸에 얼굴 그림 파라미터가 없으면 false. */
	bool BlinkNow();

	/** 먹기 성공을 알린다 — 회복 < 0 (독) 이면 아픈 얼굴, 아니면 웃는 얼굴 타이머를 새로 시작한다. */
	void NoteAte(float Heal);

	/** 매 프레임 부른다 — 경과를 흘리고 `SnareFaceExpression::For` 로 표정을 골라 바뀌었을 때만 그림을 갈아 끼운다. 값은 읽기만 한다. */
	void TickExpression(float DeltaSeconds, float Health, float MaxHealth, bool bDowned);

	/** 표정을 바로 정한다 (시험·검사용). 바뀌었으면 true. */
	bool SetExpression(ESnareFaceExpression NewExpression);

	ESnareFaceExpression GetExpression() const { return Expression; }

	/** 지금 표정이 끼워야 할 그림 (깜빡이는 중이 아닐 때의 그림). 못 불러오면 눈 뜬 기본 그림. */
	UTexture* GetExpressionTexture();

	/** 감은 짝을 끼우고 있는가. */
	bool AreEyesClosed() const { return bEyesClosed; }

	/** 깜빡임이 바꿔 끼우는 몸 머티리얼(아직 한 번도 안 감았으면 null). 검사용. */
	UMaterialInstanceDynamic* GetFaceMaterial() const { return FaceMaterial; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void ScheduleNextBlink();
	void HandleBlinkTimer();
	void OpenEyes();

	/** 지금 표정의 그림을 몸에 끼운다. 깜빡이는 중이면 건드리지 않는다. */
	void ApplyExpressionTexture();

	/** 몸 0번 칸의 동적 인스턴스. 몸이 바뀌어 칸이 다른 머티리얼이면 다시 만든다. 얼굴 파라미터가 없으면 null. */
	UMaterialInstanceDynamic* ResolveFaceMaterial();

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> FaceMaterial;

	/** 감기 전 원래 그림 — 뜰 때 이것으로 되돌린다. */
	UPROPERTY(Transient)
	TObjectPtr<UTexture> OpenEyesTexture;

	UPROPERTY(Transient)
	TObjectPtr<UTexture> LoadedClosedEyesTexture;

	UPROPERTY(Transient)
	TObjectPtr<UTexture> LoadedHurtTexture;

	UPROPERTY(Transient)
	TObjectPtr<UTexture> LoadedDownTexture;

	UPROPERTY(Transient)
	TObjectPtr<UTexture> LoadedHappyTexture;

	ESnareFaceExpression Expression = ESnareFaceExpression::Neutral;

	/** 독·먹기 뒤 지난 초. 처음엔 아주 오래 전으로 본다(음수가 아니라 큰 수 — 음수는 "없던 일"이라 같은 뜻이지만 흘림이 필요 없다). */
	float SecondsSincePoison = 1.0e9f;
	float SecondsSinceAte = 1.0e9f;

	FTimerHandle BlinkTimerHandle;
	FTimerHandle OpenTimerHandle;
	bool bEyesClosed = false;
};
