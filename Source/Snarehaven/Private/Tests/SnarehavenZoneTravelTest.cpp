// ROADMAP M6-02b 2층 검사 — 포탈 버튼과 게스트 요청.
// 보는 것 둘:
//  ① 버튼은 **갈 수 있는 포탈**에만 뜬다 — 꺼진 포탈(`CanTravel()` false)·사거리 밖 포탈은 대상이 아니다.
//  ② 게스트는 **부탁만 한다** — 게스트 쪽에서는 방이 움직이지 않고, 호스트에서만 `ServerTravel` 이 걸린다.
// 진짜 두 프로세스 왕복은 여기서 못 한다(넷드라이버가 없는 테스트 월드다 — `SnarehavenAnimalReplicationTest`
// 와 같은 이유). 대신 액터의 네트워크 역할을 게스트 것으로 바꿔 권위 없는 코드 경로를 그대로 태운다.
//
// 떠났는지는 `World->NextURL` 로 본다: 게임모드가 없는 테스트 월드에서 `UWorld::ServerTravel` 은
// 갈 URL 을 `NextURL` 에 적어 두고 참을 준다(Engine/Private/World.cpp `UWorld::ServerTravel`).
// 검사 끝에 비워서 엔진이 실제로 맵을 열지 않게 한다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.Zone; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Backend/SnareBackendSubsystem.h"
#include "Combat/MeleeAttackComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "Data/ZoneRow.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Online/SnareOnlineSubsystem.h"
#include "SnarehavenCharacter.h"
#include "UI/HuntHudWidget.h"
#include "UI/RanchHudWidget.h"
#include "World/ZonePortalActor.h"
#include "World/ZoneTravelComponent.h"

namespace
{
	const TCHAR* ZoneTablePath = TEXT("/Game/Game/Data/World/DT_ZoneTable.DT_ZoneTable");

	// 표가 이어 둔 방향 하나 (M6-01 `DT_ZoneTable` 2행: 사냥존 ↔ 거점존).
	const FName SourceZoneID(TEXT("zone.hunt"));
	const FName TargetZoneID(TEXT("zone.ranch"));

	// 검사 전용 값 (Verification.md 6 — 설계 숫자가 아니다).
	constexpr float TestAttackDamage = 10.f;
	constexpr float TestAttackRangeCm = 300.f;
	constexpr float TestAttackIntervalSeconds = 1.f;
	constexpr float TestNearDistanceCm = 100.f;
	constexpr float TestFarDistanceCm = 1000.f;

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
		// 검사가 적어 둔 갈 URL 을 지운다 — 남겨 두면 엔진이 다음 틱에 진짜로 맵을 연다.
		World->NextURL.Empty();
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	}

	ASnarehavenCharacter* SpawnTestPlayer(UWorld* World)
	{
		ASnarehavenCharacter* Player =
			World->SpawnActor<ASnarehavenCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
		if (Player != nullptr)
		{
			Player->GetMeleeAttack()->SetAttackSettings(
				TestAttackDamage, TestAttackRangeCm, TestAttackIntervalSeconds);
		}

		return Player;
	}

	// 표를 먹인 포탈 하나. `bUsable` 이 false 면 표에 없는 존을 적어 **꺼진 포탈**로 세운다.
	AZonePortalActor* SpawnTestPortal(UWorld* World, UDataTable* Table, float DistanceCm, bool bUsable)
	{
		AZonePortalActor* Portal =
			World->SpawnActor<AZonePortalActor>(FVector(DistanceCm, 0.f, 0.f), FRotator::ZeroRotator);
		if (Portal == nullptr)
		{
			return nullptr;
		}

		Portal->SetZoneData(Table, SourceZoneID,
			bUsable ? TargetZoneID : FName(TEXT("zone.no_such_zone")));
		Portal->InitializeFromTable();
		return Portal;
	}

	// 게임 인스턴스 없는 검사 월드용 백엔드 (`SnarehavenCodexScreenTest` 와 같은 방식 — `Initialize` 안 부름).
	USnareBackendSubsystem* MakeTestBackend()
	{
		UGameInstance* const GameInstance = NewObject<UGameInstance>(GetTransientPackage());
		return NewObject<USnareBackendSubsystem>(GameInstance);
	}

	/**
	 * 가짜 `PayFerry` 서버를 꽂는다. `FailCode` 가 비면 `{dest, paid}` 성공, 아니면 서버 `fail()` 모양
	 * (`{error, code, need, have}` — 받은 쪽은 bOk=true 다). 받은 함수 이름·dest 를 `OutCalls` 에 적는다.
	 * 검사 끝에 반드시 `TestServerOverride = nullptr` 로 비운다.
	 */
	void InstallFakeFerryServer(TArray<FString>& OutCalls, const FString& FailCode)
	{
		USnareBackendSubsystem::TestServerOverride =
			[&OutCalls, FailCode](const FString& FunctionName, const TSharedPtr<FJsonObject>& Args,
				USnareBackendSubsystem::FServerCallDone OnDone)
			{
				FString Dest;
				if (Args.IsValid())
				{
					Args->TryGetStringField(TEXT("dest"), Dest);
				}
				OutCalls.Add(FunctionName + TEXT(" ") + Dest);

				const TSharedPtr<FJsonObject> Reply = MakeShared<FJsonObject>();
				if (FailCode.IsEmpty())
				{
					Reply->SetStringField(TEXT("dest"), Dest);
					Reply->SetNumberField(TEXT("paid"), 0);
				}
				else if (FailCode == TEXT("not_enough_coins"))
				{
					Reply->SetStringField(TEXT("error"), TEXT("코인이 모자란다"));
					Reply->SetStringField(TEXT("code"), FailCode);
					Reply->SetNumberField(TEXT("need"), 5);
					Reply->SetNumberField(TEXT("have"), 2);
				}
				else
				{
					Reply->SetStringField(TEXT("error"), TEXT("도감 사냥+식물 50% 가 먼저다"));
					Reply->SetStringField(TEXT("code"), FailCode);
				}
				OnDone(true, Reply, FString());
			};
	}

	// 거점 HUD 도 같은 방식으로 만든다 (M6-03b — 이제 포탈 버튼을 부르는 화면이 둘이다).
	URanchHudWidget* CreateTestRanchHud(UWorld* World, ASnarehavenCharacter* Player)
	{
		URanchHudWidget* Hud = CreateWidget<URanchHudWidget>(World, URanchHudWidget::StaticClass());
		if (Hud != nullptr)
		{
			Hud->BindToCharacter(Player);
		}
		return Hud;
	}

	// 뷰포트에 올리지 않은 HUD (`SnarehavenHuntHudTest` 와 같은 방식 — 2층은 화면 없이 배선만 본다).
	UHuntHudWidget* CreateTestHud(UWorld* World, ASnarehavenCharacter* Player)
	{
		UHuntHudWidget* Hud = CreateWidget<UHuntHudWidget>(World, UHuntHudWidget::StaticClass());
		if (Hud != nullptr)
		{
			Hud->BindToCharacter(Player);
		}
		return Hud;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenZoneTravelButtonTest,
	"Snarehaven.Zone.Travel.ButtonOnlyOnUsablePortal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenZoneTravelButtonTest::RunTest(const FString& Parameters)
{
	UDataTable* const ZoneTable = LoadObject<UDataTable>(nullptr, ZoneTablePath);
	if (!TestNotNull(TEXT("DT_ZoneTable 로드"), ZoneTable))
	{
		return false;
	}

	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = SpawnTestPlayer(World);
	UHuntHudWidget* Hud = CreateTestHud(World, Player);
	if (Player == nullptr || Hud == nullptr)
	{
		DestroyTestWorld(World);
		AddError(TEXT("플레이어·HUD 만들기 실패"));
		return false;
	}

	UZoneTravelComponent* const Travel = Player->GetZoneTravel();
	if (!TestNotNull(TEXT("폰에 존 이동 컴포넌트가 붙어 있다"), Travel))
	{
		DestroyTestWorld(World);
		return false;
	}

	// 사거리는 근접 공격에서 빌린다 — 새 숫자를 만들지 않았는지 여기서 못 박는다 (DataDriven.md 1).
	TestEqual(TEXT("사거리를 근접 공격에서 빌린다"), Travel->GetTravelRangeCm(), TestAttackRangeCm);

	// ① 포탈이 없으면 버튼도 없다.
	TestNull(TEXT("포탈이 없으면 대상도 없다"), Travel->RefreshTarget());
	TestFalse(TEXT("포탈이 없으면 버튼이 접혀 있다"), Hud->IsZoneTravelButtonVisible());

	// ② 사거리 밖 포탈은 갈 수 있어도 고르지 않는다.
	AZonePortalActor* const FarPortal = SpawnTestPortal(World, ZoneTable, TestFarDistanceCm, true);
	if (!TestNotNull(TEXT("먼 포탈 스폰"), FarPortal))
	{
		DestroyTestWorld(World);
		return false;
	}
	TestTrue(TEXT("먼 포탈 자체는 갈 수 있는 포탈이다"), FarPortal->CanTravel());
	TestNull(TEXT("사거리 밖 포탈은 대상이 아니다"), Travel->RefreshTarget());
	TestFalse(TEXT("사거리 밖이면 버튼이 접혀 있다"), Hud->IsZoneTravelButtonVisible());

	// ③ 사거리 안이어도 **꺼진 포탈**은 고르지 않는다 (M6-02b 합격 기준).
	AZonePortalActor* const DeadPortal = SpawnTestPortal(World, ZoneTable, TestNearDistanceCm, false);
	if (!TestNotNull(TEXT("꺼진 포탈 스폰"), DeadPortal))
	{
		DestroyTestWorld(World);
		return false;
	}
	TestFalse(TEXT("표에 없는 존을 적은 포탈은 꺼져 있다"), DeadPortal->CanTravel());
	TestNull(TEXT("꺼진 포탈은 사거리 안이어도 대상이 아니다"), Travel->RefreshTarget());
	TestFalse(TEXT("꺼진 포탈 앞에서는 버튼이 접혀 있다"), Hud->IsZoneTravelButtonVisible());
	TestTrue(TEXT("꺼진 포탈 앞에서는 버튼 글자도 비어 있다"), Hud->GetZoneTravelButtonText().IsEmpty());

	// ④ 사거리 안 + 갈 수 있는 포탈 — 이때만 버튼이 뜬다.
	AZonePortalActor* const GoodPortal = SpawnTestPortal(World, ZoneTable, TestNearDistanceCm, true);
	if (!TestNotNull(TEXT("갈 수 있는 포탈 스폰"), GoodPortal))
	{
		DestroyTestWorld(World);
		return false;
	}
	TestEqual(TEXT("갈 수 있는 가까운 포탈을 고른다"), Travel->RefreshTarget(), GoodPortal);
	TestTrue(TEXT("고른 포탈이 하이라이트 대상이다"), Travel->GetPortalTarget() == GoodPortal);
	TestTrue(TEXT("이때 버튼이 뜬다"), Hud->IsZoneTravelButtonVisible());
	TestFalse(TEXT("버튼 글자가 비어 있지 않다"), Hud->GetZoneTravelButtonText().IsEmpty());

	// 버튼 글자는 표의 표시 이름을 쓴다 — 코드에 존 이름을 박지 않았는지 본다.
	TestTrue(TEXT("버튼 글자에 표의 갈 곳 이름이 들어 있다"),
		Hud->GetZoneTravelButtonText().ToString().Contains(
			GoodPortal->GetTargetDisplayName().ToString()));

	// ⑤ 그 포탈이 꺼지면 버튼도 도로 접힌다 (버튼이 대상 기억에만 기대지 않는지).
	GoodPortal->SetZoneData(ZoneTable, SourceZoneID, FName(TEXT("zone.no_such_zone")));
	GoodPortal->InitializeFromTable();
	TestNull(TEXT("꺼진 뒤에는 대상에서 빠진다"), Travel->RefreshTarget());
	TestFalse(TEXT("꺼진 뒤에는 버튼도 접힌다"), Hud->IsZoneTravelButtonVisible());

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenZoneTravelGuestRequestsTest,
	"Snarehaven.Zone.Travel.GuestOnlyRequests",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenZoneTravelGuestRequestsTest::RunTest(const FString& Parameters)
{
	UDataTable* const ZoneTable = LoadObject<UDataTable>(nullptr, ZoneTablePath);
	if (!TestNotNull(TEXT("DT_ZoneTable 로드"), ZoneTable))
	{
		return false;
	}

	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = SpawnTestPlayer(World);
	AZonePortalActor* Portal = SpawnTestPortal(World, ZoneTable, TestNearDistanceCm, true);
	if (Player == nullptr || Portal == nullptr)
	{
		DestroyTestWorld(World);
		AddError(TEXT("플레이어·포탈 스폰 실패"));
		return false;
	}

	UZoneTravelComponent* const Travel = Player->GetZoneTravel();
	if (!TestNotNull(TEXT("폰에 존 이동 컴포넌트가 붙어 있다"), Travel))
	{
		DestroyTestWorld(World);
		return false;
	}

	// 게스트 화면: 내 폰은 AutonomousProxy, 포탈은 호스트가 보내 준 복제본이다.
	Player->SetRole(ROLE_AutonomousProxy);
	Portal->SetRole(ROLE_SimulatedProxy);
	TestFalse(TEXT("게스트 폰은 이동 권위가 없다"), Travel->HasTravelAuthority());

	// 하이라이트(대상 고르기)는 게스트에서도 돈다 — 그래야 버튼이 뜬다.
	TestEqual(TEXT("게스트도 대상은 고른다"), Travel->RefreshTarget(), Portal);
	TestTrue(TEXT("게스트에서도 버튼은 켜진다"), Travel->CanTravel());

	// 버튼을 눌러도 게스트 쪽에서는 방이 움직이지 않는다 (요청만 나간다).
	TestFalse(TEXT("게스트 이동은 요청만 — 여기서 떠나지 않는다"), Travel->TryTravelToNearest());
	TestTrue(TEXT("게스트 쪽 월드는 갈 곳을 적지 않았다"), World->NextURL.IsEmpty());

	// 위 검사만으로는 **포탈이** 막은 건지 **컴포넌트가** 막은 건지 가려지지 않는다(둘 다 권위를 본다).
	// 포탈만 권위로 돌려 컴포넌트의 문턱을 홀로 세워 본다 — 게스트 폰이 스스로 떠나면 여기서 잡힌다.
	// (넷드라이버가 없는 월드라 `Server_` RPC 는 엔진이 삼킨다 — `AActor::GetFunctionCallspace` 의
	//  NM_Standalone + 권위 없음 → Absorbed. 즉 여기서 떠났다면 컴포넌트가 직접 떠난 것이다.)
	Portal->SetRole(ROLE_Authority);
	TestFalse(TEXT("포탈이 권위여도 게스트 폰은 스스로 떠나지 않는다"), Travel->TryTravelToNearest());
	TestTrue(TEXT("그래도 갈 곳을 적지 않았다"), World->NextURL.IsEmpty());

	// 호스트로 돌리면 같은 버튼이 진짜로 방을 옮긴다.
	Player->SetRole(ROLE_Authority);
	TestTrue(TEXT("호스트 폰은 이동 권위가 있다"), Travel->HasTravelAuthority());
	TestTrue(TEXT("호스트에서는 떠난다"), Travel->TryTravelToNearest());

	// 갈 URL 은 표가 정한 맵의 리슨 서버 URL 이어야 한다 (맵 이름을 검사에도 박지 않는다).
	const TMap<FName, FZoneRow> Rows = AZonePortalActor::ReadZoneRows(ZoneTable);
	FString Reason;
	const FZoneRow* const TargetRow =
		AZonePortalActor::ResolveTargetZone(Rows, SourceZoneID, TargetZoneID, Reason);
	if (TestNotNull(TEXT("표가 갈 곳을 정해 준다"), TargetRow))
	{
		TestEqual(TEXT("표가 정한 맵의 리슨 URL 로 떠난다"), World->NextURL,
			USnareOnlineSubsystem::MakeZoneServerTravelUrl(TargetRow->LevelName));
	}

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenZoneTravelRanchHudTest,
	"Snarehaven.Zone.Travel.BothScreensShowSameButton",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// ROADMAP M6-03b — **목장에서 돌아올 길**이 있는지. 루프#104 까지 포탈 버튼은 사냥 HUD 에만 있어서
// 목장에 들어가면 명령줄 말고는 나올 방법이 없었다. 두 화면이 같은 컴포넌트를 보고 같은 글자를
// 쓰는지, 그리고 거점 화면의 버튼도 **표가 정한 맵**으로만 떠나는지 본다.
bool FSnarehavenZoneTravelRanchHudTest::RunTest(const FString& Parameters)
{
	UDataTable* const ZoneTable = LoadObject<UDataTable>(nullptr, ZoneTablePath);
	if (!TestNotNull(TEXT("DT_ZoneTable 로드"), ZoneTable))
	{
		return false;
	}

	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = SpawnTestPlayer(World);
	UHuntHudWidget* HuntHud = CreateTestHud(World, Player);
	URanchHudWidget* RanchHud = CreateTestRanchHud(World, Player);
	if (Player == nullptr || HuntHud == nullptr || RanchHud == nullptr)
	{
		DestroyTestWorld(World);
		AddError(TEXT("플레이어·HUD 만들기 실패"));
		return false;
	}

	UZoneTravelComponent* const Travel = Player->GetZoneTravel();
	if (!TestNotNull(TEXT("폰에 존 이동 컴포넌트가 붙어 있다"), Travel))
	{
		DestroyTestWorld(World);
		return false;
	}

	// ① 포탈이 없으면 두 화면 다 접혀 있다.
	TestNull(TEXT("포탈이 없으면 대상도 없다"), Travel->RefreshTarget());
	TestFalse(TEXT("사냥 화면 버튼이 접혀 있다"), HuntHud->IsZoneTravelButtonVisible());
	TestFalse(TEXT("거점 화면 버튼도 접혀 있다"), RanchHud->IsZoneTravelButtonVisible());

	// ② 사거리 밖이면 두 화면 다 접혀 있다 (거점 화면이 사거리를 따로 보지 않는지).
	AZonePortalActor* const FarPortal = SpawnTestPortal(World, ZoneTable, TestFarDistanceCm, true);
	if (!TestNotNull(TEXT("먼 포탈 스폰"), FarPortal))
	{
		DestroyTestWorld(World);
		return false;
	}
	TestNull(TEXT("사거리 밖 포탈은 대상이 아니다"), Travel->RefreshTarget());
	TestFalse(TEXT("사거리 밖에서 거점 화면 버튼은 접혀 있다"), RanchHud->IsZoneTravelButtonVisible());

	// ③ 사거리 안 살아 있는 포탈 — 두 화면이 **같이** 뜨고 **같은 글자**를 쓴다.
	AZonePortalActor* const GoodPortal = SpawnTestPortal(World, ZoneTable, TestNearDistanceCm, true);
	if (!TestNotNull(TEXT("갈 수 있는 포탈 스폰"), GoodPortal))
	{
		DestroyTestWorld(World);
		return false;
	}
	TestEqual(TEXT("가까운 포탈을 고른다"), Travel->RefreshTarget(), GoodPortal);
	TestTrue(TEXT("사냥 화면 버튼이 뜬다"), HuntHud->IsZoneTravelButtonVisible());
	TestTrue(TEXT("거점 화면 버튼도 뜬다"), RanchHud->IsZoneTravelButtonVisible());
	TestEqual(TEXT("두 화면이 같은 글자를 쓴다"),
		RanchHud->GetZoneTravelButtonText().ToString(), HuntHud->GetZoneTravelButtonText().ToString());
	TestTrue(TEXT("버튼 글자에 표의 갈 곳 이름이 들어 있다"),
		RanchHud->GetZoneTravelButtonText().ToString().Contains(
			GoodPortal->GetTargetDisplayName().ToString()));

	// ④ 거점 화면의 버튼도 **표가 정한 맵**으로만 떠난다 (맵 이름을 화면에 박지 않았는지).
	//    버튼은 먼저 요금(`PayFerry`)을 묻는다 — 가짜 서버가 "냈다" 고 답해야 떠난다 (DESIGN 8.14).
	TArray<FString> FerryCalls;
	InstallFakeFerryServer(FerryCalls, FString());
	Travel->SetFerryBackend(MakeTestBackend());
	const bool bAsked = RanchHud->TryZoneTravel();
	USnareBackendSubsystem::TestServerOverride = nullptr;
	TestTrue(TEXT("호스트에서는 거점 화면 버튼으로 요금을 묻는다"), bAsked);
	TestEqual(TEXT("요금 요청은 한 번, 갈 곳은 base"), FString::Join(FerryCalls, TEXT("|")), FString(TEXT("PayFerry base")));

	const TMap<FName, FZoneRow> Rows = AZonePortalActor::ReadZoneRows(ZoneTable);
	FString Reason;
	const FZoneRow* const TargetRow =
		AZonePortalActor::ResolveTargetZone(Rows, SourceZoneID, TargetZoneID, Reason);
	if (TestNotNull(TEXT("표가 갈 곳을 정해 준다"), TargetRow))
	{
		TestEqual(TEXT("표가 정한 맵의 리슨 URL 로 떠난다"), World->NextURL,
			USnareOnlineSubsystem::MakeZoneServerTravelUrl(TargetRow->LevelName));
	}

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenFerryRefusedTest,
	"Snarehaven.Ferry.RefusedFareDoesNotTravel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// DESIGN 8.14 나룻배 — 요금이 모자라거나(`not_enough_coins`) 도감 50% 전(`locked`)이면 **떠나지 않고**
// 사유를 버튼 글자에 보인다. 요금을 낸 뒤에만 이동한다. 판정은 전부 서버 몫이고 여기는 답을 따르는지만 본다.
bool FSnarehavenFerryRefusedTest::RunTest(const FString& Parameters)
{
	UDataTable* const ZoneTable = LoadObject<UDataTable>(nullptr, ZoneTablePath);
	if (!TestNotNull(TEXT("DT_ZoneTable 로드"), ZoneTable))
	{
		return false;
	}

	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = SpawnTestPlayer(World);
	URanchHudWidget* RanchHud = CreateTestRanchHud(World, Player);
	UZoneTravelComponent* const Travel = Player != nullptr ? Player->GetZoneTravel() : nullptr;
	if (Player == nullptr || RanchHud == nullptr || Travel == nullptr)
	{
		DestroyTestWorld(World);
		AddError(TEXT("플레이어·HUD 만들기 실패"));
		return false;
	}

	// 거점 → 사냥터 포탈 (요금 5 구간). 표가 두 방향을 다 적어 뒀다 (M6-01).
	AZonePortalActor* const Portal =
		World->SpawnActor<AZonePortalActor>(FVector(TestNearDistanceCm, 0.f, 0.f), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("포탈 스폰"), Portal))
	{
		DestroyTestWorld(World);
		return false;
	}
	Portal->SetZoneData(ZoneTable, TargetZoneID, SourceZoneID);
	Portal->InitializeFromTable();
	TestEqual(TEXT("거점 → 사냥터 포탈을 고른다"), Travel->RefreshTarget(), Portal);
	Travel->SetFerryBackend(MakeTestBackend());

	// ⓪ 답 전 — 요청만 나가고 사유·이동 모두 그대로다(서버보다 먼저 거절 문구를 지어내지 않는다, CLAUDE.md 8).
	TArray<USnareBackendSubsystem::FServerCallDone> HeldDone;
	USnareBackendSubsystem::TestServerOverride =
		[&HeldDone](const FString&, const TSharedPtr<FJsonObject>&, USnareBackendSubsystem::FServerCallDone OnDone)
		{
			HeldDone.Add(MoveTemp(OnDone));
		};
	TestTrue(TEXT("답을 붙잡아도 요청은 나간다"), RanchHud->TryZoneTravel());
	TestTrue(TEXT("답 전에는 대기 중이다"), Travel->IsFerryPending());
	TestTrue(TEXT("답 전에는 거절 사유가 비어 있다"), Travel->GetFerryMessage().IsEmpty());
	TestTrue(TEXT("답 전에는 떠나지 않는다"), World->NextURL.IsEmpty());
	if (TestEqual(TEXT("요청은 한 번"), HeldDone.Num(), 1))
	{
		const TSharedPtr<FJsonObject> Refusal = MakeShared<FJsonObject>();
		Refusal->SetStringField(TEXT("error"), TEXT("그런 선착장은 없다"));
		Refusal->SetStringField(TEXT("code"), TEXT("bad_args"));
		HeldDone[0](true, Refusal, FString());
	}
	const FString BadArgsText = Travel->GetFerryMessage().ToString();
	AddInfo(FString::Printf(TEXT("bad_args 사유: [%s]"), *BadArgsText));
	TestTrue(TEXT("bad_args 는 표 문구로 뜬다"), BadArgsText.Contains(TEXT("배가 가지 않는다")));

	// ① 코인 부족 — 떠나지 않고, 버튼에 서버 사유와 모자란 수가 뜬다.
	TArray<FString> Calls;
	InstallFakeFerryServer(Calls, TEXT("not_enough_coins"));
	TestTrue(TEXT("요금을 묻기는 한다"), RanchHud->TryZoneTravel());
	const FString PoorText = RanchHud->GetZoneTravelButtonText().ToString();
	AddInfo(FString::Printf(TEXT("코인 부족 버튼: [%s] / 요청 [%s] / NextURL [%s]"),
		*PoorText.Replace(TEXT("\n"), TEXT(" | ")), *FString::Join(Calls, TEXT(",")), *World->NextURL));
	TestEqual(TEXT("요청 인자 dest 가 hunt 다"), FString::Join(Calls, TEXT(",")), FString(TEXT("PayFerry hunt")));
	TestTrue(TEXT("코인이 모자라면 떠나지 않는다"), World->NextURL.IsEmpty());
	TestTrue(TEXT("버튼에 서버 사유가 뜬다"), PoorText.Contains(TEXT("코인이 모자란다")));
	TestTrue(TEXT("버튼에 모자란 수가 뜬다"), PoorText.Contains(TEXT("5 필요")) && PoorText.Contains(TEXT("2 있음")));
	TestFalse(TEXT("답을 받았으니 대기 중이 아니다"), Travel->IsFerryPending());

	// ② 도감 50% 전 잠김 — 역시 떠나지 않는다.
	Calls.Reset();
	InstallFakeFerryServer(Calls, TEXT("locked"));
	TestTrue(TEXT("잠김도 요금을 묻기는 한다"), RanchHud->TryZoneTravel());
	const FString LockedText = RanchHud->GetZoneTravelButtonText().ToString();
	AddInfo(FString::Printf(TEXT("잠김 버튼: [%s]"), *LockedText.Replace(TEXT("\n"), TEXT(" | "))));
	TestTrue(TEXT("잠겼으면 떠나지 않는다"), World->NextURL.IsEmpty());
	TestTrue(TEXT("버튼에 잠김 사유가 뜬다"), LockedText.Contains(TEXT("50%")));

	// ③ 서버에 닿지 못함(로그인 전) — 가짜 서버를 빼면 진짜 길은 로그인 전이라 거절된다. 역시 안 떠난다.
	USnareBackendSubsystem::TestServerOverride = nullptr;
	TestTrue(TEXT("로그인 전에도 묻기는 한다"), RanchHud->TryZoneTravel());
	TestTrue(TEXT("서버에 못 닿으면 떠나지 않는다"), World->NextURL.IsEmpty());

	// ④ 요금을 낸 뒤에만 떠난다 — 같은 포탈, 성공 답.
	Calls.Reset();
	InstallFakeFerryServer(Calls, FString());
	TestTrue(TEXT("성공 답도 요청부터"), RanchHud->TryZoneTravel());
	USnareBackendSubsystem::TestServerOverride = nullptr;
	AddInfo(FString::Printf(TEXT("요금 낸 뒤 NextURL [%s]"), *World->NextURL));
	TestFalse(TEXT("요금을 냈으면 떠난다"), World->NextURL.IsEmpty());
	TestTrue(TEXT("떠난 뒤 버튼에 거절 사유가 남지 않는다"), Travel->GetFerryMessage().IsEmpty());

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenFerryDestKeyTest,
	"Snarehaven.Ferry.DestKeyMatchesServer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// 존 ID → 서버 `FERRY_FARES` 키. 이 글자가 틀리면 서버가 `bad_args` 로 모든 배를 막는다.
bool FSnarehavenFerryDestKeyTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("사냥터 → hunt"), UZoneTravelComponent::GetFerryDestKey(FName(TEXT("zone.hunt"))), FString(TEXT("hunt")));
	TestEqual(TEXT("거점 → base"), UZoneTravelComponent::GetFerryDestKey(FName(TEXT("zone.ranch"))), FString(TEXT("base")));
	TestEqual(TEXT("위험구역 → danger"), UZoneTravelComponent::GetFerryDestKey(FName(TEXT("zone.danger"))), FString(TEXT("danger")));
	TestTrue(TEXT("모르는 존은 빈 글자 (요청 안 함)"), UZoneTravelComponent::GetFerryDestKey(FName(TEXT("zone.nowhere"))).IsEmpty());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
