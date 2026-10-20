#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"

#include "SnareGuideBookWidget.generated.h"

class APlayerController;
class UButton;
class UDataTable;
class UImage;
class UTextBlock;
class UVerticalBox;
class UWorld;
class USnareClickProxy;

// M28-06b 안내서 화면 — 원본 Guide Book 자리(DESIGN 13 「퀘스트·안내」, 12 「온보딩/튜토리얼」 (나)).
// 전체 화면 크림 판: 왼쪽 활동 목록(쪽 제목 버튼) · 오른쪽 그림 1(아이템 아이콘 재사용) + 쪽 제목 + 순서 줄.
// **쪽 글은 코드에 없다** — `DT_UsageHints` 의 `Guide*` 칸(M28-06a)을 `SnareUsageHints::GetGuidePageKeys` 순서로 읽는다.
// 닫기는 X 버튼·ESC(`FSnareScreenCloser`) 둘 다 `RemoveFromParent` 한 길(도감·뽑기와 같은 꼴).
// 재화·인벤토리를 보지도 바꾸지도 않는다.
UCLASS()
class SNAREHAVEN_API USnareGuideBookWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	USnareGuideBookWidget(const FObjectInitializer& ObjectInitializer);

	/** 만들어 전체 화면 판 Z 로 뷰포트에 올린다 — 배낭·일시정지 「안내서」 버튼(M28-06b2)이 부른다. 첫 쪽이 열린다. */
	static USnareGuideBookWidget* OpenOnViewport(APlayerController* Owner, UWorld* World);

	/** 그 쪽을 오른쪽에 그린다. 범위 밖이면 그대로. 왼쪽 목록 버튼과 2층 검사가 같은 길. */
	void SelectPage(int32 Index);

	int32 GetSelectedPage() const { return SelectedPage; }
	const TArray<FName>& GetPageKeys() const { return PageKeys; }
	int32 GetPageButtonCount() const { return PageButtons.Num(); }

	/** 오른쪽에 지금 그려진 쪽 제목·순서 줄(앞 번호 붙은 그대로)·아이콘 유무 — 2층이 화면 글자 그대로 본다. */
	FText GetShownTitle() const;
	TArray<FText> GetShownSteps() const;
	bool HasShownIcon() const;

	/** X 버튼·ESC 와 같다. */
	void Close();

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	UFUNCTION()
	void HandleCloseClicked();

	void BuildTree();
	void RefreshPage();

	UPROPERTY(Transient)
	TObjectPtr<UDataTable> ItemTable;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UButton>> PageButtons;

	UPROPERTY(Transient)
	TArray<TObjectPtr<USnareClickProxy>> PageProxies;

	UPROPERTY(Transient)
	TObjectPtr<UImage> PageIcon;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> PageTitleText;

	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> StepList;

	UPROPERTY(Transient)
	TObjectPtr<UButton> CloseButton;

	TArray<FName> PageKeys;
	int32 SelectedPage = 0;
};
