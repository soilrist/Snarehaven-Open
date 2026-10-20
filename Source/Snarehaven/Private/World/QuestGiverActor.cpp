#include "World/QuestGiverActor.h"
#include "SnareCheckSwitch.h"
#include "SnarehavenCharacter.h"

#include "Backend/SnareBackendSubsystem.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "Misc/CommandLine.h"
#include "UI/QuestScreenWidget.h"
#include "UI/SnareWorldLabelWidget.h"

#define LOCTEXT_NAMESPACE "Snarehaven"

AQuestGiverActor::AQuestGiverActor()
{
	// 좌판 없이 몸만 선다 — 상인과 헷갈리지 않게 모자도 다르다(여행자 모자).
	GetStall()->SetVisibility(false);
	GetStall()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HatItemID = TEXT("item.traveler_hat");
	// 🟡 이름은 감독 가정 — 레퍼런스 원본 이름은 쓰지 않는다 (Reference.md).
	VendorName = LOCTEXT("QuestGiverName", "의뢰인 솔방울");
}

bool AQuestGiverActor::IsOpenSwitchSet()
{
	FString Tab;
	return SnareCheckSwitch::Param(TEXT("SnareQuestOpen")) || SnareCheckSwitch::Value(TEXT("SnareQuestOpen="), Tab);
}

void AQuestGiverActor::ApplyKindLook()
{
	// 상점 종류 표(잡화상 도토리·상인 모자)를 물려받지 않는다 — 생성자에서 정한 이름·모자 그대로.
	VendorName = LOCTEXT("QuestGiverName", "의뢰인 솔방울");
	HatItemID = TEXT("item.traveler_hat");
}

UUserWidget* AQuestGiverActor::CreateScreen(APlayerController* Controller)
{
	UQuestScreenWidget* const Quest = CreateWidget<UQuestScreenWidget>(Controller, UQuestScreenWidget::StaticClass());
	if (IsValid(Quest))
	{
		UGameInstance* const GameInstance = GetGameInstance();
		Quest->BindToBackend(GameInstance != nullptr ? GameInstance->GetSubsystem<USnareBackendSubsystem>() : nullptr);
		Quest->SetGiverName(VendorName);
		Quest->SetPromptTarget(GetPromptWorldLocation());
		// 3층 캡처용 (`-SnareQuestChainPreview=<단계>`) — 서버 없이 줄기 탭을 그 단계로 채워 연다.
		int32 PreviewStep = 0;
		if (SnareCheckSwitch::Value(TEXT("SnareQuestChainPreview="), PreviewStep) && PreviewStep > 0)
		{
			Quest->SetChainPreview(PreviewStep);
		}
		// 3층 캡처용 (`-SnareQuestOpen`) — 다가가자마자 창이 펴진 채로 뜬다. `=Chain` 이면 줄기 탭으로 (서버 실제 호출).
		else if (IsOpenSwitchSet())
		{
			FString OpenTab;
			if (SnareCheckSwitch::Value(TEXT("SnareQuestOpen="), OpenTab) && OpenTab.Equals(TEXT("Chain"), ESearchCase::IgnoreCase))
			{
				Quest->OpenOnChainTab();
			}
			else
			{
				Quest->SetOpen(true);
			}
		}
	}
	return Quest;
}

void AQuestGiverActor::CollapseScreen()
{
	if (UQuestScreenWidget* const Quest = Cast<UQuestScreenWidget>(GetScreen()))
	{
		Quest->SetOpen(false);
	}
}

void AQuestGiverActor::ExpandScreen()
{
	if (UQuestScreenWidget* const Quest = Cast<UQuestScreenWidget>(GetScreen()))
	{
		Quest->SetOpen(true);
	}
}

#undef LOCTEXT_NAMESPACE
