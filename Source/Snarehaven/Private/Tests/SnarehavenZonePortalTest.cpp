// ROADMAP M6-02a 2층 검사 — 포탈이 **표에 있는 존으로만** 보내는가.
// 표 자체가 성한지는 `SnarehavenZoneTableTest`(M6-01)가 보고, 여기서는 그 표를 읽는 쪽인
// `AZonePortalActor::ResolveTargetZone` 이 아무 데로나 열리지 않는지를 본다.
// 거절해야 하는 것 넷: 표에 없는 존 · 이어지지 않은 존 · 포탈이 선 존이 표에 없을 때 · 맵 이름이 빈 존.
// 액터를 띄우지 않는 이유 — 판정이 `static` 이라 월드 없이 같은 길을 걷는다(월드를 띄우면 검사가 느려지고
// 맵 상태에 기댄다).
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.Zone; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Data/ZoneRow.h"
#include "Engine/DataTable.h"
#include "Online/SnareOnlineSubsystem.h"
#include "World/ZonePortalActor.h"

namespace
{
	const TCHAR* ZoneTablePath = TEXT("/Game/Game/Data/World/DT_ZoneTable.DT_ZoneTable");
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenZonePortalTest, "Snarehaven.Zone.Portal.SendsOnlyToTableZones",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenZonePortalTest::RunTest(const FString& Parameters)
{
	UDataTable* const ZoneTable = LoadObject<UDataTable>(nullptr, ZoneTablePath);
	if (!TestNotNull(TEXT("DT_ZoneTable 로드"), ZoneTable))
	{
		return false;
	}

	const TMap<FName, FZoneRow> Rows = AZonePortalActor::ReadZoneRows(ZoneTable);
	// 행이 0개면 아래 루프가 아무것도 안 보고 조용히 통과한다 (Verification.md — 빈 검사 금지).
	if (!TestTrue(TEXT("존 행이 하나 이상 있다"), Rows.Num() > 0))
	{
		return false;
	}
	AddInfo(FString::Printf(TEXT("DT_ZoneTable 행 %d개로 포탈 방향을 본다"), Rows.Num()));

	int32 CheckedDirections = 0;
	for (const TPair<FName, FZoneRow>& RowPair : Rows)
	{
		const FName SourceZoneID = RowPair.Key;

		// ① 표가 이어 둔 방향은 전부 갈 수 있어야 한다 — 못 가면 그 포탈은 꺼진 채로 선다.
		for (const FName& ConnectedID : RowPair.Value.ConnectedZoneIDs)
		{
			FString Reason;
			const FZoneRow* const Target =
				AZonePortalActor::ResolveTargetZone(Rows, SourceZoneID, ConnectedID, Reason);
			if (Target == nullptr)
			{
				AddError(FString::Printf(TEXT("%s → %s 는 표가 이어 둔 길인데 포탈이 거절했다: %s"),
					*SourceZoneID.ToString(), *ConnectedID.ToString(), *Reason));
				continue;
			}

			TestFalse(FString::Printf(TEXT("%s → %s 의 맵 이름이 있다"),
				*SourceZoneID.ToString(), *ConnectedID.ToString()), Target->LevelName.IsNone());
			++CheckedDirections;
		}

		// ② 표에 없는 존으로는 못 간다.
		FString NowhereReason;
		TestNull(FString::Printf(TEXT("%s 에서 표에 없는 존으로는 못 간다"), *SourceZoneID.ToString()),
			AZonePortalActor::ResolveTargetZone(Rows, SourceZoneID,
				FName(TEXT("zone.no_such_zone")), NowhereReason));
	}

	// 이어진 방향이 하나도 없으면 ①이 헛돈 것이다 (지금 표는 사냥존 ↔ 거점존 왕복 2개).
	TestTrue(TEXT("표가 이어 둔 방향이 하나 이상 있다"), CheckedDirections > 0);
	AddInfo(FString::Printf(TEXT("표가 이어 둔 방향 %d개를 확인했다"), CheckedDirections));

	return true;
}


// 위 검사가 "표가 멀쩡해서" 통과할 때도 판정이 살아 있는지 본다 (역검증).
// 에셋을 건드리지 않고 행을 손으로 지어 먹인다 — 검사 전용 값이라 실제 표의 이름에 기대지 않는다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenZonePortalCheckSelfTest,
	"Snarehaven.Zone.Portal.CheckCatchesBadDestination",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenZonePortalCheckSelfTest::RunTest(const FString& Parameters)
{
	const FName ZoneA(TEXT("zone.test_a"));
	const FName ZoneB(TEXT("zone.test_b"));
	const FName ZoneFar(TEXT("zone.test_far"));   // 표에는 있지만 A 와 이어지지 않은 존
	const FName ZoneBlank(TEXT("zone.test_blank")); // 맵 이름이 빈 존

	FZoneRow RowA;
	RowA.ZoneID = ZoneA;
	RowA.LevelName = FName(TEXT("L_TestZoneA"));
	RowA.ConnectedZoneIDs = { ZoneB, ZoneBlank };

	FZoneRow RowB;
	RowB.ZoneID = ZoneB;
	RowB.LevelName = FName(TEXT("L_TestZoneB"));
	RowB.ConnectedZoneIDs = { ZoneA };

	FZoneRow RowFar;
	RowFar.ZoneID = ZoneFar;
	RowFar.LevelName = FName(TEXT("L_TestZoneFar"));

	FZoneRow RowBlank;
	RowBlank.ZoneID = ZoneBlank;

	TMap<FName, FZoneRow> Rows;
	Rows.Add(ZoneA, RowA);
	Rows.Add(ZoneB, RowB);
	Rows.Add(ZoneFar, RowFar);
	Rows.Add(ZoneBlank, RowBlank);

	// 멀쩡한 방향까지 막으면 판정이 쓸모없어진다 — 반대쪽부터 본다.
	FString Reason;
	const FZoneRow* const Good = AZonePortalActor::ResolveTargetZone(Rows, ZoneA, ZoneB, Reason);
	if (TestNotNull(TEXT("이어진 존으로는 간다"), Good))
	{
		TestEqual(TEXT("갈 맵은 표가 정한다"), Good->LevelName, RowB.LevelName);
	}
	TestTrue(TEXT("통과하면 거절 이유가 비어 있다"), Reason.IsEmpty());

	// ① 표에 없는 존
	TestNull(TEXT("표에 없는 존은 거절"),
		AZonePortalActor::ResolveTargetZone(Rows, ZoneA, FName(TEXT("zone.test_nowhere")), Reason));
	TestFalse(TEXT("거절하면 이유를 남긴다"), Reason.IsEmpty());

	// ② 표에는 있지만 이어지지 않은 존 — 포탈이 아무 데나 열리면 표를 세운 뜻이 없다
	TestNull(TEXT("이어지지 않은 존은 거절"),
		AZonePortalActor::ResolveTargetZone(Rows, ZoneA, ZoneFar, Reason));

	// ③ 포탈이 선 존이 표에 없을 때
	TestNull(TEXT("선 존이 표에 없으면 거절"),
		AZonePortalActor::ResolveTargetZone(Rows, FName(TEXT("zone.test_ghost")), ZoneB, Reason));

	// ④ 맵 이름이 빈 존 (이어져 있어도) — 떠나면 "이동 중" 에서 멈춘다
	TestNull(TEXT("맵 이름이 빈 존은 거절"),
		AZonePortalActor::ResolveTargetZone(Rows, ZoneA, ZoneBlank, Reason));

	// 떠날 URL 은 리슨 서버여야 한다. 갈 곳이 미정이면 URL 을 지어내지 않는다.
	// (`AGameModeBase::CanServerTravel` 이 `:` 가 든 URL 을 거절한다 — 주소를 넣으면 안 된다.)
	const FString ZoneUrl = USnareOnlineSubsystem::MakeZoneServerTravelUrl(RowB.LevelName);
	TestEqual(TEXT("존 URL 은 <맵>?listen"), ZoneUrl, FString(TEXT("L_TestZoneB?listen")));
	TestFalse(TEXT("존 URL 에 주소(:)가 없다"), ZoneUrl.Contains(TEXT(":")));
	TestTrue(TEXT("맵 이름이 없으면 URL 도 없다"),
		USnareOnlineSubsystem::MakeZoneServerTravelUrl(NAME_None).IsEmpty());

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
