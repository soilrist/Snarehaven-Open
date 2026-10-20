#include "Backend/SnareEntryReject.h"

#include "Data/EntryRejectRow.h"
#include "Engine/DataTable.h"
#include "Snarehaven.h"

namespace SnareEntryReject
{
const TCHAR* const TablePath = TEXT("/Game/Game/Data/World/DT_EntryRejectTable.DT_EntryRejectTable");
const FName UnknownCode(TEXT("unknown"));

const UDataTable* LoadTable()
{
	return LoadObject<UDataTable>(nullptr, TablePath);
}

FString ReadCode(bool bOk, const TSharedPtr<FJsonObject>& Response)
{
	// `SnareWater::CallServer` 는 서버 거절을 bOk=false 로 바꿔 넘긴다 — 그래서 `error` 칸을 bOk 보다 먼저 본다.
	if (Response.IsValid() && Response->HasField(TEXT("error")))
	{
		FString Code;
		Response->TryGetStringField(TEXT("code"), Code);
		return Code.IsEmpty() ? UnknownCode.ToString() : Code;
	}
	return (!bOk || !Response.IsValid()) ? FString(TEXT("network")) : FString();
}

FText MessageFor(const UDataTable* Table, const FString& Code, const TSharedPtr<FJsonObject>& Response)
{
	const FEntryRejectRow* Row = nullptr;
	if (Table != nullptr)
	{
		static const FString Context(TEXT("SnareEntryReject"));
		Row = Table->FindRow<FEntryRejectRow>(FName(*Code), Context, /*bWarnIfRowMissing=*/false);
		if (Row == nullptr)
		{
			Row = Table->FindRow<FEntryRejectRow>(UnknownCode, Context, /*bWarnIfRowMissing=*/false);
		}
	}
	if (Row == nullptr)
	{
		UE_LOG(LogSnarehaven, Warning, TEXT("[EntryReject] 문구 표·행이 없다 — 코드 %s 를 그대로 보인다"), *Code);
		return FText::FromString(Code);
	}

	double Need = 0.0;
	double Have = 0.0;
	if (Response.IsValid())
	{
		Response->TryGetNumberField(TEXT("need"), Need);
		Response->TryGetNumberField(TEXT("have"), Have);
	}
	FFormatNamedArguments Args;
	Args.Add(TEXT("need"), FText::AsNumber(static_cast<int32>(Need)));
	Args.Add(TEXT("have"), FText::AsNumber(static_cast<int32>(Have)));
	Args.Add(TEXT("code"), FText::FromString(Code));
	return FText::Format(Row->Message, Args);
}

FText DescribeReply(const UDataTable* Table, bool bOk, const TSharedPtr<FJsonObject>& Response)
{
	const FString Code = ReadCode(bOk, Response);
	return Code.IsEmpty() ? FText::GetEmpty() : MessageFor(Table, Code, Response);
}
}
