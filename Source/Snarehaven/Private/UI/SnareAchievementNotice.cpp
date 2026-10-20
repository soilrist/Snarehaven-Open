#include "UI/SnareAchievementNotice.h"

#include "Data/AchievementRow.h"
#include "Data/AnimalRow.h"
#include "Data/ItemRow.h"
#include "Engine/DataTable.h"

#define LOCTEXT_NAMESPACE "SnareAchievementNotice"

namespace SnareAchievementNotice
{
	namespace
	{
		void AddNew(const TArray<FName>& Was, const TArray<FName>& Now, TArray<FName>& Out)
		{
			for (const FName& ID : Now)
			{
				if (!ID.IsNone() && !Was.Contains(ID))
				{
					Out.AddUnique(ID);
				}
			}
		}

		/** 표에 이름이 있고 비지 않았으면 그 이름, 아니면 ID 글자 — 모르는 ID 를 숨기면 서버·표 어긋남을 아무도 못 본다. */
		FText NameOr(const TMap<FName, FText>& Names, FName ID)
		{
			const FText* const Found = Names.Find(ID);
			return Found != nullptr && !Found->IsEmptyOrWhitespace() ? *Found : FText::FromName(ID);
		}

		/** 카드 한 장의 「이름」 — 합칠 때 「<이름> 외 n개」 에 다시 쓴다. */
		struct FPending
		{
			bool bAchievement = false;
			FText Name;
			int32 Coins = 0;
			TSoftObjectPtr<UTexture2D> Icon;
		};

		FText BodyOf(const FPending& Card)
		{
			if (!Card.bAchievement)
			{
				return FText::Format(LOCTEXT("CodexBody", "{0} — 도감이 갱신됨"), Card.Name);
			}
			return Card.Coins > 0
				? FText::Format(LOCTEXT("AchievementBody", "{0} +{1} 코인"), Card.Name, FText::AsNumber(Card.Coins))
				: Card.Name;
		}
	}

	FDiff Diff(const TArray<FName>& WasAchievements, const TArray<FName>& NowAchievements,
		const TArray<FName>& WasCodexEntries, const TArray<FName>& NowCodexEntries, bool bFirstAnswer)
	{
		FDiff Out;
		if (bFirstAnswer)
		{
			return Out; // 접속할 때마다 모은 업적·도감이 한꺼번에 터지면 알림이 아니라 소음이다
		}
		AddNew(WasAchievements, NowAchievements, Out.NewAchievements);
		AddNew(WasCodexEntries, NowCodexEntries, Out.NewCodexEntries);
		return Out;
	}

	TArray<FCard> MakeCards(const FDiff& InDiff, const FNames& Names)
	{
		TArray<FPending> Pending;
		for (const FName& ID : InDiff.NewAchievements)
		{
			const int32* const Coins = Names.AchievementCoins.Find(ID);
			Pending.Add({ true, NameOr(Names.AchievementNames, ID), Coins != nullptr ? *Coins : 0, Names.AchievementIcon });
		}
		for (const FName& ID : InDiff.NewCodexEntries)
		{
			const TSoftObjectPtr<UTexture2D>* const Icon = Names.CodexIcons.Find(ID);
			Pending.Add({ false, NameOr(Names.CodexNames, ID), 0, Icon != nullptr ? *Icon : TSoftObjectPtr<UTexture2D>() });
		}

		TArray<FCard> Cards;
		for (int32 Index = 0; Index < Pending.Num() && Index < MaxCards; ++Index)
		{
			const FPending& Card = Pending[Index];
			FCard& Out = Cards.AddDefaulted_GetRef();
			Out.bAchievement = Card.bAchievement;
			Out.Title = Card.bAchievement ? LOCTEXT("AchievementTitle", "업적 달성!") : LOCTEXT("CodexTitle", "새 발견!");
			Out.Body = BodyOf(Card);
			Out.Icon = Card.Icon;
		}
		const int32 Overflow = Pending.Num() - MaxCards;
		if (Overflow > 0)
		{
			// 마지막 카드가 남은 것을 떠안는다 — 원본도 카드 2장만 보인다(시트 hud_ingame.md 5행).
			Cards.Last().Body = FText::Format(LOCTEXT("MergedBody", "{0} 외 {1}개"),
				Pending[MaxCards - 1].Name, FText::AsNumber(Overflow));
		}
		return Cards;
	}

	FName SoundCue(const FDiff& InDiff)
	{
		if (InDiff.NewAchievements.Num() > 0)
		{
			return FName(TEXT("Achievement"));
		}
		return InDiff.NewCodexEntries.Num() > 0 ? FName(TEXT("CodexNew")) : NAME_None;
	}

	float SoundVolume(FName Cue)
	{
		if (Cue == FName(TEXT("Achievement")))
		{
			return 0.8f;
		}
		return Cue == FName(TEXT("CodexNew")) ? 0.6f : 0.f;
	}

	void AddAchievementNames(const UDataTable* AchievementTable, FNames& InOut)
	{
		if (AchievementTable == nullptr)
		{
			return;
		}
		InOut.AchievementIcon = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(AchievementIconPath));
		for (const TPair<FName, uint8*>& Pair : AchievementTable->GetRowMap())
		{
			const FAchievementRow* const Row = reinterpret_cast<const FAchievementRow*>(Pair.Value);
			if (Row != nullptr && !Row->AchievementID.IsNone())
			{
				InOut.AchievementNames.Add(Row->AchievementID, Row->DisplayName);
				// 표시 전용 코인(M48-03b) — 미정(음수)·0 이면 넣지 않아 카드가 이름만 쓴다.
				if (Row->RewardCoins > 0)
				{
					InOut.AchievementCoins.Add(Row->AchievementID, Row->RewardCoins);
				}
			}
		}
	}

	void AddCodexNames(const UDataTable* AnimalTable, const UDataTable* ItemTable, FNames& InOut)
	{
		if (ItemTable != nullptr)
		{
			for (const TPair<FName, uint8*>& Pair : ItemTable->GetRowMap())
			{
				const FItemRow* const Row = reinterpret_cast<const FItemRow*>(Pair.Value);
				if (Row != nullptr && !Row->ItemID.IsNone())
				{
					InOut.CodexNames.Add(Row->ItemID, Row->DisplayName);
					if (!Row->IconRef.IsNull())
					{
						InOut.CodexIcons.Add(Row->ItemID, Row->IconRef);
					}
				}
			}
		}
		if (AnimalTable != nullptr)
		{
			for (const TPair<FName, uint8*>& Pair : AnimalTable->GetRowMap())
			{
				const FAnimalRow* const Row = reinterpret_cast<const FAnimalRow*>(Pair.Value);
				if (Row == nullptr)
				{
					continue;
				}
				if (!Row->AnimalID.IsNone())
				{
					InOut.CodexNames.Add(Row->AnimalID, Row->DisplayName);
					// 종 행엔 그림이 없다 — 그 종 펫 아이템 행의 그림(도감 화면 M25-02 와 같음). 변종 칸은 아이템 행이라 위에서 이미 들어감.
					const TSoftObjectPtr<UTexture2D> PetIcon = InOut.CodexIcons.FindRef(Row->PetItemID); // 복사 — Add 가 맵을 다시 잡아도 안전
					if (!PetIcon.IsNull())
					{
						InOut.CodexIcons.Add(Row->AnimalID, PetIcon);
					}
				}
				if (!Row->VariantPetItemID.IsNone())
				{
					// 변종 칸 ID 는 변종 펫 아이템 ID 다 — 이름은 도감 화면과 같은 「변종 ○○」.
					InOut.CodexNames.Add(Row->VariantPetItemID,
						FText::Format(LOCTEXT("CodexVariantName", "변종 {0}"), Row->DisplayName));
				}
			}
		}
	}

	FText JoinCards(const TArray<FCard>& Cards)
	{
		TArray<FText> Lines;
		for (const FCard& Card : Cards)
		{
			Lines.Add(FText::Format(LOCTEXT("CardLine", "{0} {1}"), Card.Title, Card.Body));
		}
		return FText::Join(FText::FromString(TEXT("\n")), Lines);
	}
}

#undef LOCTEXT_NAMESPACE
