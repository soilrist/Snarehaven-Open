#include "World/SnareFruitTree.h"

namespace
{
	// 🟡 화면 값 — 묘목은 다 자란 나무의 0.3배 키, 가지에 보일 열매는 서버 `FRUIT_PER_TREE` 와 같은 3개까지.
	constexpr float SaplingScale = 0.5f;
	constexpr int32 MaxVisibleFruits = 3;

	SnareFruitTree::EStage StageFromName(const FString& Name)
	{
		if (Name == TEXT("grown"))
		{
			return SnareFruitTree::EStage::Grown;
		}
		return Name == TEXT("sapling") ? SnareFruitTree::EStage::Sapling : SnareFruitTree::EStage::Empty;
	}
}

bool SnareFruitTree::ParseTrees(const TSharedPtr<FJsonObject>& Reply, TArray<FTreeView>& OutBySlot)
{
	const TArray<TSharedPtr<FJsonValue>>* Trees = nullptr;
	if (!Reply.IsValid() || !Reply->TryGetArrayField(TEXT("trees"), Trees))
	{
		return false;
	}
	OutBySlot.Reset();
	OutBySlot.SetNum(SlotCount);
	for (int32 Slot = 0; Slot < SlotCount; ++Slot)
	{
		OutBySlot[Slot].Slot = Slot;
	}
	for (const TSharedPtr<FJsonValue>& Value : *Trees)
	{
		const TSharedPtr<FJsonObject>* Row = nullptr;
		int32 Slot = INDEX_NONE;
		if (!Value.IsValid() || !Value->TryGetObject(Row) || !(*Row)->TryGetNumberField(TEXT("slot"), Slot)
			|| Slot < 0 || Slot >= SlotCount)
		{
			continue;
		}
		FTreeView& View = OutBySlot[Slot];
		FString Fruit;
		FString StageName;
		(*Row)->TryGetStringField(TEXT("fruit"), Fruit);
		(*Row)->TryGetStringField(TEXT("stage"), StageName);
		(*Row)->TryGetNumberField(TEXT("fruits"), View.Fruits);
		View.Fruit = FName(*Fruit);
		View.Stage = StageFromName(StageName);
	}
	return true;
}

float SnareFruitTree::StageScale(const EStage Stage)
{
	switch (Stage)
	{
	case EStage::Grown: return 1.f;
	case EStage::Sapling: return SaplingScale;
	default: return 0.f;
	}
}

int32 SnareFruitTree::VisibleFruits(const EStage Stage, const int32 ServerFruits)
{
	return Stage == EStage::Grown ? FMath::Clamp(ServerFruits, 0, MaxVisibleFruits) : 0;
}

bool SnareFruitTree::CanChop(const EStage Stage, const int32 ShownFruits, const bool bAxeInHand)
{
	return bAxeInHand && Stage == EStage::Grown && ShownFruits <= 0;
}

const TArray<FName>& SnareFruitTree::FruitItemIDs()
{
	static const TArray<FName> Ids = {TEXT("item.pear"), TEXT("item.peach"), TEXT("item.cherry"), TEXT("item.orange"), TEXT("item.plum")};
	return Ids;
}

FLinearColor SnareFruitTree::FruitColor(const FName FruitItemID)
{
	// 🟡 5종이 멀리서도 갈리게 색상환에서 떼어 잡았다 — 배 노랑 · 복숭아 분홍 · 체리 진홍 · 오렌지 주황 · 자두 보라.
	static const TMap<FName, FLinearColor> Colors = {
		{TEXT("item.pear"), FLinearColor(1.f, 0.85f, 0.05f)},
		{TEXT("item.peach"), FLinearColor(1.f, 0.45f, 0.4f)},
		{TEXT("item.cherry"), FLinearColor(0.7f, 0.02f, 0.08f)},
		{TEXT("item.orange"), FLinearColor(1.f, 0.4f, 0.02f)},
		{TEXT("item.plum"), FLinearColor(0.35f, 0.08f, 0.5f)},
	};
	const FLinearColor* Found = Colors.Find(FruitItemID);
	return Found != nullptr ? *Found : FLinearColor(0.5f, 0.5f, 0.5f);
}

FName SnareFruitTree::NativeFruitOf(const FString& PlayFabId)
{
	// 서버 `potatoRng("fruit:" + id)` 와 한 글자도 다르면 안 된다 — 이름표 과일이 실제로 받는 과일과 달라진다.
	const FString Key = TEXT("fruit:") + PlayFabId;
	uint32 Hash = 2166136261u;
	for (const TCHAR Ch : Key)
	{
		Hash ^= static_cast<uint32>(Ch);
		Hash *= 16777619u;
	}
	uint32 A = Hash + 0x6D2B79F5u;
	uint32 T = (A ^ (A >> 15)) * (1u | A);
	T = (T + (T ^ (T >> 7)) * (61u | T)) ^ T;
	const uint32 Out = T ^ (T >> 14);
	const TArray<FName>& Ids = FruitItemIDs();
	return Ids[static_cast<int32>((static_cast<double>(Out) / 4294967296.0) * Ids.Num())];
}

FText SnareFruitTree::FruitName(const FName FruitItemID)
{
	static const TMap<FName, FText> Names = {
		{TEXT("item.pear"), NSLOCTEXT("FruitTree", "Pear", "배")},
		{TEXT("item.peach"), NSLOCTEXT("FruitTree", "Peach", "복숭아")},
		{TEXT("item.cherry"), NSLOCTEXT("FruitTree", "Cherry", "체리")},
		{TEXT("item.orange"), NSLOCTEXT("FruitTree", "Orange", "오렌지")},
		{TEXT("item.plum"), NSLOCTEXT("FruitTree", "Plum", "자두")},
	};
	const FText* Found = Names.Find(FruitItemID);
	return Found != nullptr ? *Found : FText::GetEmpty();
}

FText SnareFruitTree::FriendTreeLabel(const FString& OwnerNickname, const FName FruitItemID)
{
	const FText Owner = OwnerNickname.IsEmpty() ? NSLOCTEXT("FruitTree", "Friend", "친구") : FText::FromString(OwnerNickname);
	return FText::Format(NSLOCTEXT("FruitTree", "FriendLabel", "{0}의 {1}나무"), Owner, FruitName(FruitItemID));
}

TSharedRef<FJsonObject> SnareFruitTree::MakePickArgs(const FString& HostPlayFabId)
{
	TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
	Args->SetStringField(TEXT("hostId"), HostPlayFabId);
	return Args;
}
