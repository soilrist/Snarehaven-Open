#include "Backend/SnareQuestChain.h"

#include "Data/QuestChainRow.h"
#include "Engine/DataTable.h"
#include "Snarehaven.h"

namespace SnareQuestChain
{
const TCHAR* const TablePath = TEXT("/Game/Game/Data/Quest/DT_QuestChain.DT_QuestChain");
const TCHAR* const CodeLocked = TEXT("chain_locked");
const TCHAR* const CodeDone = TEXT("chain_done");
const TCHAR* const CodeShort = TEXT("chain_short");

const UDataTable* LoadTable()
{
	return LoadObject<UDataTable>(nullptr, TablePath);
}

FName RowNameFor(int32 Step)
{
	return FName(*FString::Printf(TEXT("chain.%d"), Step));
}

const FQuestChainRow* FindStep(const UDataTable* Table, int32 Step)
{
	if (Table == nullptr || Step < 1)
	{
		return nullptr;
	}
	static const FString Context(TEXT("SnareQuestChain"));
	return Table->FindRow<FQuestChainRow>(RowNameFor(Step), Context, /*bWarnIfRowMissing=*/false);
}

int32 StepCount(const UDataTable* Table)
{
	int32 Count = 0;
	while (FindStep(Table, Count + 1) != nullptr)
	{
		++Count;
	}
	return Count;
}

bool IsFinished(const UDataTable* Table, int32 CurrentStep)
{
	return CurrentStep > StepCount(Table);
}

int32 HaveCount(const TMap<FName, int32>& Have, FName ItemId)
{
	const int32* Found = Have.Find(ItemId);
	return Found != nullptr ? *Found : 0;
}

bool IsGoalMet(const FQuestChainRow& Row, const TMap<FName, int32>& Have)
{
	for (const FQuestChainNeed& Need : Row.Needs)
	{
		if (HaveCount(Have, Need.ItemId) < Need.Count)
		{
			return false;
		}
	}
	return true;
}

EResult CanComplete(const UDataTable* Table, int32 CurrentStep, int32 Requested, const TMap<FName, int32>& Have)
{
	if (IsFinished(Table, CurrentStep))
	{
		return EResult::Done;
	}
	const FQuestChainRow* Row = FindStep(Table, Requested);
	if (Requested != CurrentStep || Row == nullptr)
	{
		return EResult::Locked;
	}
	return IsGoalMet(*Row, Have) ? EResult::Ok : EResult::Short;
}

FString CodeFor(EResult Result)
{
	switch (Result)
	{
	case EResult::Locked: return CodeLocked;
	case EResult::Done: return CodeDone;
	case EResult::Short: return CodeShort;
	default: return FString();
	}
}

int32 StepAfter(const UDataTable* Table, int32 CurrentStep, int32 Requested, const TMap<FName, int32>& Have)
{
	return CanComplete(Table, CurrentStep, Requested, Have) == EResult::Ok ? CurrentStep + 1 : CurrentStep;
}
}
