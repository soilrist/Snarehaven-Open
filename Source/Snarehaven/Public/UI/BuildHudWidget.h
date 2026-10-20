#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "BuildHudWidget.generated.h"

class UButton;
class UEditableTextBox;
class UHorizontalBox;
class USnareBaseBuildComponent;
class UTextBlock;

// DESIGN 8.10·8.11 HUD 조각: 오른쪽 `제작` 버튼(배낭 제작) + 놓기 모드일 때만 뜨는 아래쪽 막대
// (돌리기 / 놓기 / 취소 · 판정 글 · 보관함 PIN). 판정은 서버다 — 이 막대는 요청만 보낸다.
UCLASS()
class SNAREHAVEN_API UBuildHudWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void BindToBuild(USnareBaseBuildComponent* InBuild);

	/** 놓기 모드 상태를 막대에 옮긴다. */
	void RefreshPlacementBar();

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeDestruct() override;

	UFUNCTION()
	void HandleRotateClicked();

	UFUNCTION()
	void HandleFineClicked();

	UFUNCTION()
	void HandleConfirmClicked();

	UFUNCTION()
	void HandleCancelClicked();

private:
	bool NeedsPlaceholderTree() const;
	void BuildPlaceholderTree();
	UButton* MakeButton(const FName& Name, const FText& Label);

	UPROPERTY(Transient)
	TObjectPtr<UHorizontalBox> PlacementBar;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> FineText;

	/** 「돌리기」 단추 글 — 벽 장식을 걸 땐 「높이 150」 (M108-02). */
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> RotateText;

	/** 「세밀」 단추 — 벽 장식은 칸·돌림이 벽에서 정해져 숨긴다. */
	UPROPERTY(Transient)
	TObjectPtr<UButton> FineButton;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> VerdictText;

	UPROPERTY(Transient)
	TObjectPtr<UEditableTextBox> PinBox;

	TWeakObjectPtr<USnareBaseBuildComponent> Build;
	FDelegateHandle PlacementChangedHandle;
};
