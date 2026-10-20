// ROADMAP M2-04a 2층 검사 — 목장 슬롯 캐시가 백엔드 권위를 지키는지:
// 배치·수확은 **요청만** 쌓이고 칸은 그대로인지, 칸 내용은 스냅샷으로만 바뀌는지,
// 성하지 않은 스냅샷을 통째로 거절하는지 (명세 docs/specs/ranch-slot.md 6장).
// **M6-05b 가 같은 질문을 작물 칸에도 묻는다**(아래 `CropSlotChangesOnlyBySnapshot`·`NoClientSideTimer`) —
// 거점 슬롯 컴포넌트가 하나라(결정 005-2) 검사도 같은 파일에 둔다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Data/CropProductRow.h"
#include "Data/RanchProductRow.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Ranch/RanchComponent.h"

namespace
{
	// 테스트 전용 값 (Verification.md 6 — 설계 숫자가 아니다).
	const FName TestAnimal = TEXT("animal.test_pet");
	constexpr int32 TestSlotIndex = 0;
	constexpr int32 TestStock = 2;
	constexpr int32 TestOutOfRangeSlot = 99;

	// 실제 데이터 테이블 (M2-02). 어떤 동물이 무엇을 얼마 만에 주는지는 여기서만 온다.
	const TCHAR* RanchTablePath = TEXT("/Game/Game/Data/Ranch/DT_RanchProductTable.DT_RanchProductTable");
	// 작물 표 (M6-05a). 어떤 작물이 밭에 들어갈 수 있는지도 코드가 아니라 데이터가 정한다.
	const TCHAR* CropTablePath = TEXT("/Game/Game/Data/Ranch/DT_CropProductTable.DT_CropProductTable");

	UWorld* CreateTestWorld()
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
		FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
		WorldContext.SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		return World;
	}

	void DestroyTestWorld(UWorld* World)
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	}

	URanchComponent* SpawnLooseRanch(UWorld* World)
	{
		AActor* Holder = World->SpawnActor<AActor>(FVector::ZeroVector, FRotator::ZeroRotator);
		if (Holder == nullptr)
		{
			return nullptr;
		}

		URanchComponent* Ranch = NewObject<URanchComponent>(Holder);
		Ranch->RegisterComponent();
		return Ranch;
	}

	// 백엔드가 보내 줬다고 치는 스냅샷. 칸 수는 컴포넌트가 아는 값을 그대로 쓴다 (코드에 5 를 적지 않는다).
	TArray<FRanchSlotState> MakeSnapshot(const URanchComponent* Ranch, int32 OccupiedIndex, FName AnimalID)
	{
		TArray<FRanchSlotState> Snapshot;
		Snapshot.SetNum(Ranch->GetSlotCount());
		if (Snapshot.IsValidIndex(OccupiedIndex))
		{
			Snapshot[OccupiedIndex].AnimalID = AnimalID;
			Snapshot[OccupiedIndex].OccupiedSinceUtc = FDateTime::UtcNow();
			Snapshot[OccupiedIndex].Stock = TestStock;
		}
		return Snapshot;
	}

	// M6-05b: 작물 칸이 하나 찬 스냅샷. 심는 요청이 아직 없으니 밭은 이 길로만 찬다.
	TArray<FRanchSlotState> MakeCropSnapshot(const URanchComponent* Ranch, int32 CropIndex, FName CropItemID)
	{
		TArray<FRanchSlotState> Snapshot;
		Snapshot.SetNum(Ranch->GetSlotCount());
		if (Snapshot.IsValidIndex(CropIndex))
		{
			Snapshot[CropIndex].Kind = ERanchSlotKind::Crop;
			Snapshot[CropIndex].CropItemID = CropItemID;
			Snapshot[CropIndex].OccupiedSinceUtc = FDateTime::UtcNow();
			Snapshot[CropIndex].Stock = TestStock;
		}
		return Snapshot;
	}

	/**
	 * 클라이언트가 스스로 시간을 세고 있다는 표시를 찾는다 (있으면 그 토막들을 돌려준다).
	 * 이름이 아니라 **코드 본문**을 본다 — 함수 이름만 보면 이름이 바뀔 때 조용히 거짓 통과한다 (PROMPT ③-12).
	 */
	TArray<FString> FindClientTimerTokens(const FString& SourceText)
	{
		static const TCHAR* Tokens[] = {
			TEXT("SetTimer"), TEXT("GetTimerManager"), TEXT("FDateTime::UtcNow"),
			TEXT("FTimespan"), TEXT("CycleMinutes"), TEXT("TickComponent"),
			TEXT("GetCoreTicker().AddTicker")
		};

		TArray<FString> Found;
		for (const TCHAR* Token : Tokens)
		{
			if (SourceText.Contains(Token, ESearchCase::CaseSensitive))
			{
				Found.Add(Token);
			}
		}
		return Found;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenRanchRequestTest, "Snarehaven.Ranch.Slot.RequestsDoNotChangeCache",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenRanchRequestTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	URanchComponent* Ranch = SpawnLooseRanch(World);
	if (!TestNotNull(TEXT("목장 컴포넌트가 만들어져야 한다"), Ranch))
	{
		DestroyTestWorld(World);
		return false;
	}

	TestEqual(TEXT("칸 수만큼 캐시가 있다"), Ranch->GetSlots().Num(), Ranch->GetSlotCount());
	TestTrue(TEXT("처음엔 빈 칸이다"), Ranch->IsSlotEmpty(TestSlotIndex));

	// 요청은 쌓이지만 칸은 그대로여야 한다 — 배치가 눈에 보이면 백엔드 응답 전에 믿은 것이다 (CLAUDE.md 8).
	TestTrue(TEXT("빈 칸에 배치 요청이 쌓인다"), Ranch->QueueRegisterPet(TestSlotIndex, TestAnimal));
	TestTrue(TEXT("요청을 쌓아도 칸은 여전히 비어 있다"), Ranch->IsSlotEmpty(TestSlotIndex));
	TestEqual(TEXT("대기 중인 요청 1건"), Ranch->GetPendingRequests().Num(), 1);

	// 응답이 오기 전에 두 번 누른 경우.
	TestFalse(TEXT("같은 칸에 두 번째 요청은 거절"), Ranch->QueueRegisterPet(TestSlotIndex, TestAnimal));
	TestEqual(TEXT("대기 중인 요청은 그대로 1건"), Ranch->GetPendingRequests().Num(), 1);

	TestFalse(TEXT("빈 칸은 수확할 수 없다"), Ranch->QueueHarvest(1));
	TestFalse(TEXT("없는 칸 번호는 무시"), Ranch->QueueRegisterPet(TestOutOfRangeSlot, TestAnimal));
	TestFalse(TEXT("없는 칸은 빈 칸이 아니다"), Ranch->IsSlotEmpty(TestOutOfRangeSlot));

	Ranch->ClearPendingRequests();
	TestEqual(TEXT("대기열을 비우면 0건"), Ranch->GetPendingRequests().Num(), 0);

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenRanchSnapshotTest, "Snarehaven.Ranch.Slot.SnapshotFillsSlots",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenRanchSnapshotTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	URanchComponent* Ranch = SpawnLooseRanch(World);
	UDataTable* RanchTable = LoadObject<UDataTable>(nullptr, RanchTablePath);
	if (!TestNotNull(TEXT("목장 컴포넌트가 만들어져야 한다"), Ranch)
		|| !TestNotNull(TEXT("DT_RanchProductTable 로드"), RanchTable))
	{
		DestroyTestWorld(World);
		return false;
	}

	// 배치할 동물은 표에서 고른다 — 어떤 종이 목장에 들어갈 수 있는지는 데이터가 정한다 (DataDriven.md 1).
	const TArray<FName> RowNames = RanchTable->GetRowNames();
	if (!TestTrue(TEXT("목장 생산 행이 하나 이상 있다"), RowNames.Num() > 0))
	{
		DestroyTestWorld(World);
		return false;
	}

	const FRanchProductRow* Row = RanchTable->FindRow<FRanchProductRow>(RowNames[0], TEXT("RanchSlotTest"));
	if (!TestNotNull(TEXT("첫 행"), Row))
	{
		DestroyTestWorld(World);
		return false;
	}

	TestTrue(TEXT("성한 스냅샷은 반영된다"), Ranch->ApplyBackendSnapshot(MakeSnapshot(Ranch, TestSlotIndex, Row->AnimalID)));

	const FRanchSlotState* Slot = Ranch->GetSlot(TestSlotIndex);
	if (TestNotNull(TEXT("반영된 칸"), Slot))
	{
		TestEqual(TEXT("칸의 동물이 스냅샷 값"), Slot->AnimalID, Row->AnimalID);
		TestEqual(TEXT("미수확 개수가 스냅샷 값"), Slot->Stock, TestStock);
		TestFalse(TEXT("이제 빈 칸이 아니다"), Ranch->IsSlotEmpty(TestSlotIndex));
		AddInfo(FString::Printf(TEXT("[%s] %d 번 칸 배치 — 주기 %d분 · 생산 %s · 미수확 %d개"),
			*Row->AnimalID.ToString(), TestSlotIndex, Row->CycleMinutes,
			*Row->ProducedItemID.ToString(), Slot->Stock));
	}

	// 찬 칸이라야 수확 요청이 쌓인다.
	TestTrue(TEXT("찬 칸은 수확 요청이 쌓인다"), Ranch->QueueHarvest(TestSlotIndex));
	TestEqual(TEXT("수확해도 미수확 개수는 그대로 (지급은 백엔드)"),
		Ranch->GetSlot(TestSlotIndex)->Stock, TestStock);
	TestFalse(TEXT("찬 칸에 배치 요청은 거절"), Ranch->QueueRegisterPet(TestSlotIndex, Row->AnimalID));

	// 성하지 않은 스냅샷은 통째로 거절 — 반쯤 반영된 목장은 "펫이 사라졌다" 로 보인다.
	TArray<FRanchSlotState> TooShort = MakeSnapshot(Ranch, TestSlotIndex, Row->AnimalID);
	TooShort.Pop();
	TestFalse(TEXT("칸 수가 다른 스냅샷은 거절"), Ranch->ApplyBackendSnapshot(TooShort));

	TArray<FRanchSlotState> BadStock = MakeSnapshot(Ranch, TestSlotIndex, Row->AnimalID);
	BadStock[TestSlotIndex].Stock = -1;
	TestFalse(TEXT("미수확 개수가 음수인 스냅샷은 거절"), Ranch->ApplyBackendSnapshot(BadStock));

	TArray<FRanchSlotState> GhostSlot = MakeSnapshot(Ranch, TestSlotIndex, Row->AnimalID);
	GhostSlot[TestSlotIndex].AnimalID = NAME_None;
	TestFalse(TEXT("빈 칸에 재고가 남은 스냅샷은 거절"), Ranch->ApplyBackendSnapshot(GhostSlot));

	// 거절당했으니 캐시는 아까 그대로여야 한다.
	TestEqual(TEXT("거절된 뒤에도 칸의 동물은 그대로"), Ranch->GetSlot(TestSlotIndex)->AnimalID, Row->AnimalID);

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenRanchCropSlotTest, "Snarehaven.Ranch.Slot.CropSlotChangesOnlyBySnapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// ROADMAP M6-05b 2층 검사 — 작물 칸이 **백엔드 스냅샷으로만** 바뀌는가.
// 동물 칸과 같은 짜임이라 같은 질문을 그대로 묻는다: 요청은 칸을 안 바꾸고, 어긋난 스냅샷은 통째로 거절.
// 여기에 작물 칸만의 질문 둘이 붙는다 — 밭에는 펫을 못 넣는다 · 칸 종류와 ID 가 어긋나면 거절.
bool FSnarehavenRanchCropSlotTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	URanchComponent* Ranch = SpawnLooseRanch(World);
	UDataTable* CropTable = LoadObject<UDataTable>(nullptr, CropTablePath);
	if (!TestNotNull(TEXT("목장 컴포넌트가 만들어져야 한다"), Ranch)
		|| !TestNotNull(TEXT("DT_CropProductTable 로드"), CropTable))
	{
		DestroyTestWorld(World);
		return false;
	}

	// 심을 작물은 표에서 고른다 — 코드에 `item.carrot` 을 적지 않는다 (DataDriven.md 1).
	const TArray<FName> CropRowNames = CropTable->GetRowNames();
	if (!TestTrue(TEXT("작물 행이 하나 이상 있다"), CropRowNames.Num() > 0))
	{
		DestroyTestWorld(World);
		return false;
	}

	const FCropProductRow* CropRow = CropTable->FindRow<FCropProductRow>(CropRowNames[0], TEXT("RanchSlotTest"));
	if (!TestNotNull(TEXT("첫 작물 행"), CropRow)
		|| !TestFalse(TEXT("첫 작물 행에 수확물 ID 가 있다"), CropRow->ProducedItemID.IsNone()))
	{
		DestroyTestWorld(World);
		return false;
	}

	AddInfo(FString::Printf(TEXT("작물 표 %d행 중 [%s] 로 본다 — 재배 %d분"),
		CropRowNames.Num(), *CropRow->ProducedItemID.ToString(), CropRow->CycleMinutes));

	// 기본값은 지금까지의 목장 칸이다 — 이 조각이 기존 5칸의 뜻을 바꾸지 않는다.
	if (const FRanchSlotState* FreshSlot = Ranch->GetSlot(TestSlotIndex))
	{
		TestTrue(TEXT("칸의 기본 종류는 동물 생산"), FreshSlot->Kind == ERanchSlotKind::AnimalProduct);
		TestTrue(TEXT("갓 만든 칸은 빈 칸"), FreshSlot->IsEmpty());
	}

	TestTrue(TEXT("작물 스냅샷은 반영된다"),
		Ranch->ApplyBackendSnapshot(MakeCropSnapshot(Ranch, TestSlotIndex, CropRow->ProducedItemID)));

	const FRanchSlotState* Slot = Ranch->GetSlot(TestSlotIndex);
	if (TestNotNull(TEXT("반영된 칸"), Slot))
	{
		TestTrue(TEXT("칸 종류가 작물"), Slot->Kind == ERanchSlotKind::Crop);
		TestEqual(TEXT("칸이 기르는 것이 표의 수확물 ID"), Slot->GetOccupantID(), CropRow->ProducedItemID);
		TestTrue(TEXT("작물 칸에 동물 ID 는 비어 있다"), Slot->AnimalID.IsNone());
		TestFalse(TEXT("이제 빈 칸이 아니다"), Ranch->IsSlotEmpty(TestSlotIndex));
	}

	// 밭에는 펫을 못 넣는다. 심는 요청은 아직 없어서 (BACKEND.md 에 짝이 없다) 칸은 스냅샷으로만 찬다.
	TestFalse(TEXT("작물 칸에 펫 배치 요청은 거절"), Ranch->QueueRegisterPet(TestSlotIndex, TestAnimal));
	TestEqual(TEXT("거절됐으니 대기열은 비어 있다"), Ranch->GetPendingRequests().Num(), 0);

	// 수확 요청은 동물 칸과 같은 길이다 — 쌓인 개수·지급은 백엔드가 정한다.
	TestTrue(TEXT("찬 작물 칸은 수확 요청이 쌓인다"), Ranch->QueueHarvest(TestSlotIndex));
	TestEqual(TEXT("수확 요청을 쌓아도 미수확 개수는 그대로"), Ranch->GetSlot(TestSlotIndex)->Stock, TestStock);
	TestEqual(TEXT("수확 요청을 쌓아도 칸이 기르는 것은 그대로"),
		Ranch->GetSlot(TestSlotIndex)->GetOccupantID(), CropRow->ProducedItemID);

	// 칸 종류와 ID 가 어긋난 스냅샷은 통째로 거절 — 어느 쪽을 믿을지 클라이언트가 정하지 않는다.
	TArray<FString> RejectedCases;
	TArray<FRanchSlotState> CropWithPet = MakeCropSnapshot(Ranch, TestSlotIndex, CropRow->ProducedItemID);
	CropWithPet[TestSlotIndex].AnimalID = TestAnimal;
	if (!Ranch->ApplyBackendSnapshot(CropWithPet)) { RejectedCases.Add(TEXT("작물 칸에 동물 ID")); }

	TArray<FRanchSlotState> AnimalWithCrop = MakeSnapshot(Ranch, TestSlotIndex, TestAnimal);
	AnimalWithCrop[TestSlotIndex].CropItemID = CropRow->ProducedItemID;
	if (!Ranch->ApplyBackendSnapshot(AnimalWithCrop)) { RejectedCases.Add(TEXT("동물 칸에 작물 ID")); }

	TArray<FRanchSlotState> GhostCrop = MakeCropSnapshot(Ranch, TestSlotIndex, CropRow->ProducedItemID);
	GhostCrop[TestSlotIndex].CropItemID = NAME_None;
	if (!Ranch->ApplyBackendSnapshot(GhostCrop)) { RejectedCases.Add(TEXT("빈 작물 칸에 재고")); }

	TestEqual(TEXT("일부러 깬 스냅샷 3가지가 전부 거절됐다"), RejectedCases.Num(), 3);
	AddInfo(FString::Printf(TEXT("거절된 스냅샷: %s"), *FString::Join(RejectedCases, TEXT(" / "))));

	// 거절당했으니 캐시는 아까 그대로여야 한다 (반쯤 반영되면 "내 밭이 사라졌다" 로 보인다).
	TestEqual(TEXT("거절된 뒤에도 칸이 기르는 것은 그대로"),
		Ranch->GetSlot(TestSlotIndex)->GetOccupantID(), CropRow->ProducedItemID);
	TestTrue(TEXT("거절된 뒤에도 칸 종류는 작물"),
		Ranch->GetSlot(TestSlotIndex)->Kind == ERanchSlotKind::Crop);

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenRanchNoTimerTest, "Snarehaven.Ranch.Slot.NoClientSideTimer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// ROADMAP M6-05b 합격 기준의 나머지 반쪽 — **클라이언트가 타이머를 세는 코드 0줄**.
// 동작 검사로는 "안 쓴 것" 을 못 본다(없는 코드는 안 돌아간다). 그래서 `URanchComponent` 의 소스를
// 직접 읽어 시간 세는 토막이 있는지 본다 (CLAUDE.md 8 · DESIGN 4.1 권위 2단).
bool FSnarehavenRanchNoTimerTest::RunTest(const FString& Parameters)
{
	// 파일 → 그 파일이 정말 그 파일인지 박을 못(가드). M12-05 부터 유지·명성을 옮기는 동기화기와
	// 그것을 글자로 쓰는 HUD 도 훑는다 — 유지 기한까지 남은 시간을 기기 시계로 줄이면 여기서 잡힌다.
	const TArray<TPair<FString, FString>> SourcePaths = {
		{ TEXT("Source/Snarehaven/Public/Ranch/RanchComponent.h"), TEXT("ApplyBackendSnapshot") },
		{ TEXT("Source/Snarehaven/Private/Ranch/RanchComponent.cpp"), TEXT("ApplyBackendSnapshot") },
		{ TEXT("Source/Snarehaven/Private/Ranch/RanchSyncComponent.cpp"), TEXT("ranchUpkeep") },
		{ TEXT("Source/Snarehaven/Private/UI/RanchHudWidget.cpp"), TEXT("GetUpkeepText") }
	};

	for (const TPair<FString, FString>& Entry : SourcePaths)
	{
		const FString& RelativePath = Entry.Key;
		const FString FullPath = FPaths::ProjectDir() / RelativePath;

		FString SourceText;
		if (!TestTrue(FString::Printf(TEXT("소스를 읽었다: %s"), *RelativePath),
			FFileHelper::LoadFileToString(SourceText, *FullPath)))
		{
			return false;
		}

		// 빈 파일·엉뚱한 파일을 읽고도 "토막 0개" 로 통과하지 않게 먼저 못을 박는다.
		if (!TestTrue(FString::Printf(TEXT("%s 가 정말 그 소스다 (%s 포함)"), *RelativePath, *Entry.Value),
			SourceText.Contains(Entry.Value, ESearchCase::CaseSensitive)))
		{
			return false;
		}

		const TArray<FString> Found = FindClientTimerTokens(SourceText);
		TestEqual(FString::Printf(TEXT("%s 에 시간 세는 토막 0개 (찾은 것: %s)"),
			*RelativePath, Found.Num() > 0 ? *FString::Join(Found, TEXT(", ")) : TEXT("없음")),
			Found.Num(), 0);
		AddInfo(FString::Printf(TEXT("%s — %d자를 훑었다"), *RelativePath, SourceText.Len()));
	}

	// 역검증: 훑는 자가 정말 잡는지 검사 전용 토막으로 본다 (파일은 안 건드린다 — PROMPT ③-13).
	const FString FakeTimerCode = TEXT("void Tick() { GetWorld()->GetTimerManager().SetTimer(H, 1.0f); }");
	TestEqual(TEXT("일부러 심은 타이머 토막 2개를 잡는다"), FindClientTimerTokens(FakeTimerCode).Num(), 2);
	const FString FakeTickerCode = TEXT("void Begin() { FTSTicker::GetCoreTicker().AddTicker(TEXT(\"X\"), 1.f, Fn); }");
	TestEqual(TEXT("일부러 심은 티커 토막 1개를 잡는다"), FindClientTimerTokens(FakeTickerCode).Num(), 1);
	TestEqual(TEXT("멀쩡한 토막에서는 0개"),
		FindClientTimerTokens(TEXT("bool ApplyBackendSnapshot() { return true; }")).Num(), 0);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
