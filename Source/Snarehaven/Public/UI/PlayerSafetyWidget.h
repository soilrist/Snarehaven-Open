#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/GiftScreenWidget.h"

#include "PlayerSafetyWidget.generated.h"

class UBorder;
class UButton;
class UPanelWidget;
class UTextBlock;
class UVerticalBox;
class USnareBlockListSubsystem;
class USnareClickProxy;

// 신고·차단 판 (사람 결정 10-16, 앱스토어 1.2). 일시정지 메뉴 「플레이어」 로 연다.
//
// 진입을 머리 위 이름표가 아니라 메뉴에 둔 까닭: 폰에서 움직이는 이름표는 손가락으로 맞히기 어렵고 이동 터치와 겹친다.
// 메뉴 목록은 늘 같은 자리·큰 줄이라 처음 하는 사람도 찾는다.
// 신고·차단은 서버(`ReportPlayer`·`BlockPlayer`·`UnblockPlayer`)가 판정한다 — 답 전엔 아무것도 바꾸지 않는다.
UCLASS()
class SNAREHAVEN_API UPlayerSafetyWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	static UPlayerSafetyWidget* OpenOnViewport(APlayerController* Owner, UWorld* World);

	void BindBlockList(USnareBlockListSubsystem* InBlockList);

	/** 같은 방 사람 목록을 대는 곳. 안 정하면 빈 목록. */
	TFunction<TArray<FGiftPeer>()> PeerSource;

	/** 사람 목록을 다시 센다(고른 사람·이유·문구는 비운다). */
	void Reload();
	void SelectPeer(const FString& PlayFabId);
	void SelectReason(FName Reason);

	bool RequestReport();
	/** 고른 사람이 차단돼 있으면 해제, 아니면 차단. */
	bool RequestToggleBlock();

	void Close();

	const TArray<FGiftPeer>& GetPeers() const { return Peers; }
	const FString& GetSelectedPeer() const { return SelectedPeer; }
	FName GetSelectedReason() const { return SelectedReason; }
	const FText& GetMessage() const { return Message; }
	bool CanReport() const;
	bool IsPending() const;
	/** 줄에 보일 이름 — 차단한 사람은 「차단한 플레이어」. */
	FText GetShownName(const FGiftPeer& Peer) const;

	static FText DescribeReason(FName Reason);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

	UFUNCTION()
	void HandleReportClicked();
	UFUNCTION()
	void HandleBlockClicked();
	UFUNCTION()
	void HandleCloseClicked();

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> MessageText;
	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> BodyList;
	UPROPERTY(Transient)
	TObjectPtr<UButton> ReportButton;
	UPROPERTY(Transient)
	TObjectPtr<UButton> BlockButton;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> BlockLabel;

	UPROPERTY()
	TObjectPtr<USnareBlockListSubsystem> BlockList;

	UPROPERTY(Transient)
	TArray<TObjectPtr<USnareClickProxy>> RowProxies;

private:
	void BuildTree();
	void RefreshDisplay();
	void AddSectionTitle(const FText& Title, int32& Index);
	/** 줄 하나를 넣고 그 상자를 돌려준다 — 고른 줄을 스크롤 안으로 끌어올 때 쓴다. */
	class USizeBox* AddRow(const FText& Label, bool bSelected, FName Key, TFunction<void(FName)> OnClicked, int32& Index);
	const FGiftPeer* FindSelected() const;

	TArray<FGiftPeer> Peers;
	FString SelectedPeer;
	FName SelectedReason;
	FText Message;
};
