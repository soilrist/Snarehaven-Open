#include "Crafting/SnareCraftRules.h"
#include "Backend/SnarePlayerSnapshot.h"
#include "Engine/DataTable.h"

#define LOCTEXT_NAMESPACE "Snarehaven"

namespace SnareCraftRules
{
	int32 CountItem(const TArray<FInventorySlot>& Slots, FName ItemID)
	{
		int32 Total = 0;
		for (const FInventorySlot& Slot : Slots)
		{
			if (Slot.ItemID == ItemID)
			{
				Total += Slot.Count;
			}
		}
		return Total;
	}

	void IndexItems(const UDataTable* ItemTable, TMap<FName, const FItemRow*>& OutByID)
	{
		OutByID.Reset();
		if (!IsValid(ItemTable))
		{
			return;
		}
		for (const TPair<FName, uint8*>& Pair : ItemTable->GetRowMap())
		{
			const FItemRow* const Row = reinterpret_cast<const FItemRow*>(Pair.Value);
			if (Row != nullptr && !Row->ItemID.IsNone())
			{
				OutByID.Add(Row->ItemID, Row);
			}
		}
	}

	bool OwnsBlueprint(const FSnarePlayerSnapshot& Snapshot, FName BlueprintID)
	{
		return BlueprintID.IsNone() || Snapshot.CodexEntries.Contains(BlueprintID)
			|| CountItem(Snapshot.InventorySlots, BlueprintID) > 0;
	}

	bool IsStationOpen(ERecipeStation Station, ESnareCraftPlace Place)
	{
		switch (Station)
		{
		case ERecipeStation::Anywhere:
			return Place == ESnareCraftPlace::Backpack || Place == ESnareCraftPlace::Workbench;
		case ERecipeStation::Workbench:
			return Place == ESnareCraftPlace::Workbench;
		case ERecipeStation::Fire:
			return Place == ESnareCraftPlace::Fire;
		case ERecipeStation::Smelter:
			return Place == ESnareCraftPlace::Smelter;
		}
		return false;
	}

	void MarkTentNeeded(TArray<FSnareRecipeView>& Views, bool bHasTent)
	{
		for (FSnareRecipeView& View : Views)
		{
			View.bNeedsTent = !bHasTent && View.Station == ERecipeStation::Workbench;
		}
	}

	FText TagLabel(ECraftTag Tag)
	{
		switch (Tag)
		{
		case ECraftTag::Wood: return LOCTEXT("CraftTagWood", "나무류");
		case ECraftTag::Cedar: return LOCTEXT("CraftTagCedar", "향나무류");
		case ECraftTag::Berry: return LOCTEXT("CraftTagBerry", "열매류");
		case ECraftTag::Meat: return LOCTEXT("CraftTagMeat", "고기류");
		case ECraftTag::Hide: return LOCTEXT("CraftTagHide", "가죽류");
		case ECraftTag::Feather: return LOCTEXT("CraftTagFeather", "깃털류");
		case ECraftTag::Fish: return LOCTEXT("CraftTagFish", "생선류");
		case ECraftTag::Fruit: return LOCTEXT("CraftTagFruit", "과일류");
		case ECraftTag::Jam: return LOCTEXT("CraftTagJam", "잼류");
		case ECraftTag::Oil: return LOCTEXT("CraftTagOil", "기름류");
		case ECraftTag::Spice: return LOCTEXT("CraftTagSpice", "양념류");
		case ECraftTag::Vegetable: return LOCTEXT("CraftTagVegetable", "채소류");
		case ECraftTag::Mushroom: return LOCTEXT("CraftTagMushroom", "버섯류");
		default: return FText::GetEmpty();
		}
	}

	FName ConfirmCue(ESnareCraftPlace Place)
	{
		switch (Place)
		{
		case ESnareCraftPlace::Smelter: return FName(TEXT("CraftSmelt"));
		case ESnareCraftPlace::Fire: return FName(TEXT("CraftSizzle"));
		default: return FName(TEXT("CraftHammer"));
		}
	}

	float ConfirmVolume(ESnareCraftPlace Place)
	{
		return Place == ESnareCraftPlace::Fire ? 0.6f : 0.8f;
	}

	FSnareRecipeView BuildRecipeView(FName RowName, const FRecipeRow& Row,
		const FSnarePlayerSnapshot& Snapshot, const TMap<FName, const FItemRow*>& ItemsByID)
	{
		FSnareRecipeView View;
		View.RecipeRowName = RowName;
		View.ResultItemID = Row.ResultItemID;
		View.ResultCount = Row.ResultCount;
		View.Station = Row.Station;
		View.RequiredBlueprintID = Row.RequiredBlueprintID;
		View.bLocked = !OwnsBlueprint(Snapshot, Row.RequiredBlueprintID);

		const FItemRow* const* const ResultRow = ItemsByID.Find(Row.ResultItemID);
		View.ResultName = ResultRow != nullptr ? (*ResultRow)->DisplayName : FText::FromName(Row.ResultItemID);

		// 서버 `pickInputs` 와 같은 차례 — 앞 재료가 먼저 가져간 몫은 뒤 재료가 못 쓴다.
		TMap<FName, int32> Used;
		for (const FRecipeIngredient& Ingredient : Row.Ingredients)
		{
			FSnareIngredientView IngredientView;
			IngredientView.ItemID = Ingredient.ItemID;
			IngredientView.Tag = Ingredient.Tag;
			IngredientView.Need = Ingredient.Count;

			TArray<FName> Candidates;
			if (!Ingredient.ItemID.IsNone())
			{
				Candidates.Add(Ingredient.ItemID);
				const FItemRow* const* const IngredientRow = ItemsByID.Find(Ingredient.ItemID);
				IngredientView.Label = IngredientRow != nullptr
					? (*IngredientRow)->DisplayName : FText::FromName(Ingredient.ItemID);
			}
			else
			{
				for (const FInventorySlot& Slot : Snapshot.InventorySlots)
				{
					const FItemRow* const* const SlotRow = ItemsByID.Find(Slot.ItemID);
					if (SlotRow != nullptr && (*SlotRow)->CraftTag == Ingredient.Tag)
					{
						Candidates.AddUnique(Slot.ItemID);
					}
				}
				IngredientView.Label = TagLabel(Ingredient.Tag);
			}

			int32 StillNeed = Ingredient.Count;
			for (const FName& Candidate : Candidates)
			{
				int32& CandidateUsed = Used.FindOrAdd(Candidate);
				const int32 Available = FMath::Max(0, CountItem(Snapshot.InventorySlots, Candidate) - CandidateUsed);
				IngredientView.Have += Available;
				const int32 Take = FMath::Min(Available, StillNeed);
				CandidateUsed += Take;
				StillNeed -= Take;
			}

			View.Ingredients.Add(IngredientView);
		}

		return View;
	}

	void BuildRecipeList(const UDataTable* RecipeTable, const UDataTable* ItemTable,
		const FSnarePlayerSnapshot& Snapshot, ESnareCraftPlace Place, TArray<FSnareRecipeView>& OutViews)
	{
		OutViews.Reset();
		if (!IsValid(RecipeTable))
		{
			return;
		}

		TMap<FName, const FItemRow*> ItemsByID;
		IndexItems(ItemTable, ItemsByID);

		for (const TPair<FName, uint8*>& Pair : RecipeTable->GetRowMap())
		{
			const FRecipeRow* const Row = reinterpret_cast<const FRecipeRow*>(Pair.Value);
			if (Row != nullptr && IsStationOpen(Row->Station, Place))
			{
				OutViews.Add(BuildRecipeView(Pair.Key, *Row, Snapshot, ItemsByID));
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
