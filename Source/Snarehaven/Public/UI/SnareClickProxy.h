#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "SnareClickProxy.generated.h"

// 코드로 세운 버튼 여러 개가 **자기 이름표(Key)** 를 들고 눌리게 하는 심부름꾼.
// `UButton::OnClicked` 는 인자가 없어서, 줄마다 다른 아이템·탭을 가리키려면 줄마다 이것 하나를 붙인다
// (상점 거래 줄·도감 탭이 쓴다). 들고 있는 쪽이 UPROPERTY 로 쥐어야 GC 가 안 거둔다.
UCLASS()
class SNAREHAVEN_API USnareClickProxy : public UObject
{
	GENERATED_BODY()

public:
	/** 이 버튼이 가리키는 것 (`item.hatchet`·탭 번호 …). */
	FName Key;

	/** 눌렸을 때 부를 곳. */
	TFunction<void(FName)> OnClicked;

	UFUNCTION()
	void HandleClicked()
	{
		if (OnClicked)
		{
			OnClicked(Key);
		}
	}
};
