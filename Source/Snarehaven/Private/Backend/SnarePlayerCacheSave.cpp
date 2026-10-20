#include "Backend/SnarePlayerCacheSave.h"

const TCHAR* USnarePlayerCacheSave::GetSlotName()
{
	return TEXT("SnarePlayerCache");
}

USnarePlayerCacheSave* USnarePlayerCacheSave::MakeFrom(const FSnarePlayerSnapshot& InSnapshot)
{
	USnarePlayerCacheSave* const Cache = NewObject<USnarePlayerCacheSave>();
	Cache->FormatVersion = CurrentFormatVersion;
	Cache->Snapshot = InSnapshot;
	return Cache;
}

bool USnarePlayerCacheSave::TryReadSnapshot(const USnarePlayerCacheSave* Cache, const FString& ExpectedPlayFabId,
	FSnarePlayerSnapshot& OutSnapshot)
{
	if (!IsValid(Cache) || Cache->FormatVersion != CurrentFormatVersion)
	{
		// 형식이 다른 파일을 억지로 읽으면 칸이 밀려 "내 물건이 다른 물건으로 바뀌었다" 가 된다.
		return false;
	}

	if (!Cache->Snapshot.HasOwner())
	{
		return false;
	}

	// 기기를 물려주거나 계정을 갈아 끼웠을 때, 지난 사람의 인벤토리가 잠깐이라도 보이면 안 된다.
	if (!ExpectedPlayFabId.IsEmpty() && Cache->Snapshot.PlayFabId != ExpectedPlayFabId)
	{
		return false;
	}

	OutSnapshot = Cache->Snapshot;
	return true;
}
