#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RanchSlotActor.generated.h"

class ARanchPetActor;
class USceneComponent;
class UStaticMeshComponent;
class UWidgetComponent;
struct FAnimalRow;

// ROADMAP M2-04b: 목장(`L_Ranch`)에 깔아 두는 슬롯 한 칸의 **몸** (명세 `docs/specs/ranch-slot.md` 4장).
// 칸의 내용(어떤 펫이 있는지·얼마나 쌓였는지)은 여기에 없다 — 그건 `URanchComponent` 가 백엔드
// 스냅샷으로만 채우는 캐시고(CLAUDE.md 8 · DESIGN 4.1), 이 액터는 "몇 번 칸이 여기 서 있다" 만 안다.
//
// 배치·수확 **요청**은 `URanchInteractComponent`(M2-04c)가 보내고, 이 액터는 하이라이트만 켜고 끈다.
UCLASS()
class SNAREHAVEN_API ARanchSlotActor : public AActor
{
	GENERATED_BODY()

public:
	ARanchSlotActor();

	/** 이 칸이 `URanchComponent` 의 몇 번째 칸인지 (0~4). 맵에 깔 때 인스턴스마다 정한다. */
	UFUNCTION(BlueprintPure, Category = "Ranch")
	int32 GetSlotIndex() const { return SlotIndex; }

	/**
	 * 자동 상호작용 대상으로 뽑혔을 때 켠다 (M2-04c). 켜는 판단은 `URanchInteractComponent` 가 한다.
	 * `AGatherableActor::SetHighlighted` 와 같은 방식 — 커스텀 뎁스 스텐실만 켜고
	 * 실제 외곽선은 포스트프로세스 머티리얼(아트 단계) 몫이다.
	 * @param bHighlighted 하이라이트를 켤지
	 */
	UFUNCTION(BlueprintCallable, Category = "Ranch")
	void SetHighlighted(bool bHighlighted);

	/** 지금 하이라이트돼 있나. 2층 검사가 하이라이트가 옮겨 갔는지 확인한다. */
	UFUNCTION(BlueprintPure, Category = "Ranch")
	bool IsHighlighted() const { return bHighlighted; }

	/**
	 * 맡긴 펫의 몸을 말뚝 곁에 세운다(`Row`) 또는 치운다(nullptr). 같은 종이 이미 서 있으면 그대로 둔다 —
	 * 캐시가 바뀔 때마다 다시 세우면 걷던 펫이 말뚝으로 튄다. 부르는 쪽은 `URanchInteractComponent`.
	 */
	void ShowPet(const FAnimalRow* Row);

	/** 지금 서 있는 펫 몸 (없으면 nullptr). 2층 검사용. */
	ARanchPetActor* GetPet() const { return Pet; }

	/** 말뚝 위에 뜨는 상호작용 프롬프트 카드 (M15-03). 2층 검사가 보임·자리를 본다. */
	UWidgetComponent* GetPromptCard() const { return PromptCard; }

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

protected:
	// 칸 번호는 맵에 깔린 인스턴스마다 다르다 (engines/unreal.md 코드 규칙 1).
	// 기본값 INDEX_NONE: 깔면서 번호를 안 주면 조용히 0번이 되는 대신 "안 정했다" 로 남는다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ranch")
	int32 SlotIndex = INDEX_NONE;

private:
	// 화면에 보이는 몸 — `Post` 가 말뚝 메시 `SM_Prop_TetherPost_01`(M13-40, DESIGN 3.2 「목장 칸 말뚝 진짜 모습」).
	// `Loop` 는 예전 자리표시 고리 자리로 메시 없이 남는다(레벨 인스턴스가 들고 있는 이름이라).
	// 루트는 발밑(Z=0). 메시를 루트로 삼으면 기둥 절반이 바닥에 묻힌다 — .cpp 주석 참고.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ranch", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> SlotRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ranch", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> Post;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ranch", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> Loop;

	// 말뚝 위 상호작용 프롬프트 (M15-03 · 시트 「대상 옆 작은 크림 카드 — 대상이 없으면 안 보임」).
	// 하이라이트와 같은 스위치(`SetHighlighted`)로 켜고 끈다 — 자동 상호작용 대상은 한 칸뿐이라
	// 스위치가 갈리면 외곽선이 켜진 칸과 카드가 뜬 칸이 어긋난다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ranch", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UWidgetComponent> PromptCard;

	UPROPERTY(Transient)
	TObjectPtr<ARanchPetActor> Pet;

	/** `Pet` 의 종 (`FAnimalRow::AnimalID`). */
	FName PetAnimalID;

	// 메시 두 개가 같이 켜지고 꺼지므로 상태를 따로 들고 답한다 (컴포넌트에 되묻지 않는다).
	bool bHighlighted = false;
};
