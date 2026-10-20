// ROADMAP M2-05 2층 검사 — 인벤토리·코인의 **저장 형식과 캐시 규칙**만 본다.
// 실제 PlayFab 왕복은 여기서 하지 않는다(네트워크가 있어야 하는 검사는 3층이다).
// 이 검사가 잡는 것: ① 로컬 캐시가 백엔드 응답을 이기는 사고 ② 기기를 물려받았을 때 남의 인벤토리가
// 보이는 사고 ③ 형식이 바뀐 옛 캐시를 그대로 읽어 칸이 밀리는 사고 ④ 한 스택이 여러 칸으로 흩어지는 것
// ⑤ 코인 재화 코드·조회 API 이름이 BACKEND.md 표와 어긋나는 것.
// 실행: UnrealEditor-Cmd <uproject 절대경로> -ExecCmds="Automation RunTests Snarehaven; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Backend/SnareBackendSubsystem.h"
#include "Backend/SnarePlayerCacheSave.h"
#include "Backend/SnarePlayerSnapshot.h"
#include "Engine/GameInstance.h"

namespace
{
	/**
	 * 게임 인스턴스 서브시스템은 `ClassWithin` 이 `UGameInstance` 라 아무 데나 만들 수 없다 —
	 * 빈 게임 인스턴스를 주인으로 세워 준다. 이 검사는 `Initialize` 를 부르지 않으므로 로그인은 안 붙는다.
	 */
	USnareBackendSubsystem* MakeTestBackend()
	{
		UGameInstance* const GameInstance = NewObject<UGameInstance>(GetTransientPackage());
		return NewObject<USnareBackendSubsystem>(GameInstance);
	}

	/** 검사 전용 값 — 설계 숫자가 아니다 (Verification.md). */
	FSnarePlayerSnapshot MakeTestSnapshot(const FString& OwnerId, int32 Coin)
	{
		FSnarePlayerSnapshot Snapshot;
		Snapshot.PlayFabId = OwnerId;
		Snapshot.CoinBalance = Coin;
		Snapshot.FetchedAtUtc = FDateTime::UtcNow();
		Snapshot.AddItem(TEXT("item.pig_meat"), 2);
		return Snapshot;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenCacheBackendNamesTest, "Snarehaven.Backend.Cache.NamesMatchBackendDoc",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenCacheBackendNamesTest::RunTest(const FString& Parameters)
{
	// 이름이 어긋나면 잔액이 늘 0 으로 보이거나 조회가 조용히 빈 응답을 받는다 (BACKEND.md 데이터 모델 표).
	TestEqual(TEXT("스네어 코인은 가상 재화 SC"), USnareBackendSubsystem::GetCoinCurrencyCode(), FString(TEXT("SC")));
	TestEqual(TEXT("젬은 가상 재화 GM"), USnareBackendSubsystem::GetGemCurrencyCode(), FString(TEXT("GM")));
	TestNotEqual(TEXT("두 재화 코드는 서로 다르다"),
		USnareBackendSubsystem::GetCoinCurrencyCode(), USnareBackendSubsystem::GetGemCurrencyCode());
	TestEqual(TEXT("보유 상태는 GetUserInventory 로 묻는다"),
		USnareBackendSubsystem::GetPlayerStateApiName(), FString(TEXT("GetUserInventory")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenSnapshotFoldTest, "Snarehaven.Backend.Snapshot.FoldsSameItemIntoOneSlot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenSnapshotFoldTest::RunTest(const FString& Parameters)
{
	FSnarePlayerSnapshot Snapshot;
	Snapshot.AddItem(TEXT("item.pig_meat"), 1);
	Snapshot.AddItem(TEXT("item.pig_meat"), 2);
	Snapshot.AddItem(TEXT("item.pig_hide"), 1);

	// PlayFab 클래식 인벤토리는 한 스택을 인스턴스 여러 줄로 준다 — 그대로 옮기면 20칸이 금방 찬다.
	TestEqual(TEXT("같은 아이템은 한 칸에 모인다"), Snapshot.InventorySlots.Num(), 2);
	TestEqual(TEXT("모인 칸의 개수는 합이다"), Snapshot.InventorySlots[0].Count, 3);

	Snapshot.AddItem(TEXT("item.pig_meat"), 0);
	Snapshot.AddItem(NAME_None, 5);
	TestEqual(TEXT("0개·이름 없는 아이템은 칸을 만들지 않는다"), Snapshot.InventorySlots.Num(), 2);
	TestEqual(TEXT("0개는 개수도 안 바꾼다"), Snapshot.InventorySlots[0].Count, 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenCacheRoundTripTest, "Snarehaven.Backend.Cache.RoundTripKeepsItemsAndCoin",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenCacheRoundTripTest::RunTest(const FString& Parameters)
{
	const FSnarePlayerSnapshot Saved = MakeTestSnapshot(TEXT("PFTESTOWNER"), 7);
	const USnarePlayerCacheSave* const Cache = USnarePlayerCacheSave::MakeFrom(Saved);

	FSnarePlayerSnapshot Loaded;
	TestTrue(TEXT("같은 계정의 캐시는 읽는다"),
		USnarePlayerCacheSave::TryReadSnapshot(Cache, TEXT("PFTESTOWNER"), Loaded));
	TestEqual(TEXT("칸이 그대로 살아 있다"), Loaded.InventorySlots.Num(), Saved.InventorySlots.Num());
	TestEqual(TEXT("아이템 ID 가 그대로다"), Loaded.InventorySlots[0].ItemID, FName(TEXT("item.pig_meat")));
	TestEqual(TEXT("개수가 그대로다"), Loaded.InventorySlots[0].Count, 2);
	TestEqual(TEXT("코인 잔액이 그대로다"), Loaded.CoinBalance, 7);

	// 아직 로그인 전이라 계정을 모를 때도 지난 화면은 보여 줘야 한다 (DESIGN 4.5 오프라인 표시용).
	FSnarePlayerSnapshot BeforeLogin;
	TestTrue(TEXT("계정을 모를 때는 주인 검사를 건너뛴다"),
		USnarePlayerCacheSave::TryReadSnapshot(Cache, FString(), BeforeLogin));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenCacheForeignOwnerTest, "Snarehaven.Backend.Cache.ForeignAccountCacheIsDropped",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenCacheForeignOwnerTest::RunTest(const FString& Parameters)
{
	const USnarePlayerCacheSave* const Cache = USnarePlayerCacheSave::MakeFrom(MakeTestSnapshot(TEXT("PFOWNERA"), 7));

	FSnarePlayerSnapshot Loaded;
	// 기기를 물려주거나 계정을 갈아 끼웠을 때, 지난 사람 물건이 한 프레임도 보이면 안 된다.
	TestFalse(TEXT("다른 계정의 캐시는 버린다"),
		USnarePlayerCacheSave::TryReadSnapshot(Cache, TEXT("PFOWNERB"), Loaded));
	TestEqual(TEXT("버렸으면 아무것도 안 담는다"), Loaded.InventorySlots.Num(), 0);

	const USnarePlayerCacheSave* const Ownerless = USnarePlayerCacheSave::MakeFrom(MakeTestSnapshot(FString(), 7));
	TestFalse(TEXT("주인 없는 캐시도 버린다"),
		USnarePlayerCacheSave::TryReadSnapshot(Ownerless, FString(), Loaded));
	TestFalse(TEXT("파일 자체가 없으면 버린다"),
		USnarePlayerCacheSave::TryReadSnapshot(nullptr, FString(), Loaded));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenCacheFormatVersionTest, "Snarehaven.Backend.Cache.OldFormatVersionIsDropped",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenCacheFormatVersionTest::RunTest(const FString& Parameters)
{
	USnarePlayerCacheSave* const Cache = USnarePlayerCacheSave::MakeFrom(MakeTestSnapshot(TEXT("PFTESTOWNER"), 7));
	Cache->FormatVersion = USnarePlayerCacheSave::CurrentFormatVersion - 1;

	FSnarePlayerSnapshot Loaded;
	// 형식이 바뀐 옛 파일을 억지로 읽으면 칸이 밀려 "내 물건이 다른 물건으로 바뀌었다" 가 된다.
	TestFalse(TEXT("형식 번호가 다르면 읽지 않는다"),
		USnarePlayerCacheSave::TryReadSnapshot(Cache, TEXT("PFTESTOWNER"), Loaded));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenCacheBackendWinsTest, "Snarehaven.Backend.Cache.BackendSnapshotOverwritesLocal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenCacheBackendWinsTest::RunTest(const FString& Parameters)
{
	USnareBackendSubsystem* const Backend = MakeTestBackend();

	FSnarePlayerSnapshot Local = MakeTestSnapshot(TEXT("PFTESTOWNER"), 7);
	Local.AddItem(TEXT("item.berry"), 5);
	TestTrue(TEXT("로컬 캐시를 올린다"), Backend->ApplyPlayerSnapshot(Local, /*bFromBackend=*/false));
	TestFalse(TEXT("로컬 캐시는 백엔드가 준 것이 아니다"), Backend->IsSnapshotFromBackend());
	TestEqual(TEXT("올린 칸이 보인다"), Backend->GetPlayerSnapshot().InventorySlots.Num(), 2);

	// 백엔드 응답은 로컬 캐시를 **합치지 않고 덮어쓴다** — 합치면 클라이언트에 남은 값이 보유로 굳는다.
	FSnarePlayerSnapshot FromBackend;
	FromBackend.PlayFabId = TEXT("PFTESTOWNER");
	FromBackend.AddItem(TEXT("item.pig_hide"), 1);
	TestTrue(TEXT("백엔드 스냅샷을 반영한다"), Backend->ApplyPlayerSnapshot(FromBackend, /*bFromBackend=*/true));
	TestTrue(TEXT("이제 백엔드가 준 값이다"), Backend->IsSnapshotFromBackend());
	TestEqual(TEXT("로컬에만 있던 칸은 사라진다"), Backend->GetPlayerSnapshot().InventorySlots.Num(), 1);
	TestEqual(TEXT("남은 칸은 백엔드가 준 것이다"),
		Backend->GetPlayerSnapshot().InventorySlots[0].ItemID, FName(TEXT("item.pig_hide")));
	TestEqual(TEXT("코인도 백엔드 값으로 바뀐다"), Backend->GetPlayerSnapshot().CoinBalance, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenCacheOwnerlessRejectedTest, "Snarehaven.Backend.Cache.OwnerlessSnapshotRejected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenCacheOwnerlessRejectedTest::RunTest(const FString& Parameters)
{
	USnareBackendSubsystem* const Backend = MakeTestBackend();

	// 서브시스템이 남기는 경고(주인 없음)는 로그에 그대로 두고 `AddExpectedError` 로 잡지 않는다 —
	// 다른 검사(`SnarehavenGatherTest`·`SnarehavenCaptureTest`)와 같은 규칙이다.
	FSnarePlayerSnapshot Ownerless;
	Ownerless.AddItem(TEXT("item.berry"), 1);

	// 주인 없는 상태를 받아들이면 다음 부팅에 누구 것인지 가릴 수 없는 캐시가 파일로 남는다.
	TestFalse(TEXT("주인 없는 스냅샷은 거절한다"), Backend->ApplyPlayerSnapshot(Ownerless, /*bFromBackend=*/true));
	TestEqual(TEXT("거절했으면 캐시도 그대로다"), Backend->GetPlayerSnapshot().InventorySlots.Num(), 0);
	TestFalse(TEXT("거절했으면 백엔드 값이 아니다"), Backend->IsSnapshotFromBackend());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
