// 소리 2층 검사 — 효과음이 변형 여러 개로 들어왔고 파도·보폭 곡선이 DESIGN 2.2 「소리」 표대로인가.
// 실행: UnrealEditor-Cmd <uproject 절대경로> -ExecCmds="Automation RunTests Snarehaven; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Building/SnareBuildRules.h"
#include "Data/ItemRow.h"
#include "Data/ZoneRow.h"
#include "Engine/DataTable.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "Player/SnareEmoteBubble.h"
#include "Player/SnareGasp.h"
#include "Player/SnareShiver.h"
#include "Player/SnareWarmth.h"
#include "Player/SnareRevive.h"
#include "Player/SnareLowHealth.h"
#include "Player/PlayerVitalsComponent.h"
#include "Data/PlayerVitalsRow.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "SnarehavenCharacter.h"
#include "World/SnareSound.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenSoundTest, "Snarehaven.World.Sound.Base",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenSoundTest::RunTest(const FString& Parameters)
{
	USnareSoundSubsystem* const Sounds = NewObject<USnareSoundSubsystem>();
	TestEqual(TEXT("발소리 풀 5개"), Sounds->CountVariants(TEXT("Footstep_Grass")), 5);
	TestEqual(TEXT("새소리 6개"), Sounds->CountVariants(TEXT("Bird")), 6);
	TestEqual(TEXT("파도 반복 1개"), Sounds->CountVariants(TEXT("Amb_Ocean")), 1);
	TestEqual(TEXT("눈 밟는 소리 풀 5개"), Sounds->CountVariants(TEXT("Footstep_Snow")), 5);
	TestEqual(TEXT("설원은 눈 발소리"), SnareSound::FootstepCue(TEXT("zone.snow")), FName(TEXT("Footstep_Snow")));
	TestEqual(TEXT("평원은 풀 발소리"), SnareSound::FootstepCue(TEXT("zone.plains")), FName(TEXT("Footstep_Grass")));
	TestEqual(TEXT("존 밖은 풀 발소리"), SnareSound::FootstepCue(NAME_None), FName(TEXT("Footstep_Grass")));
	// M46-05 늪 = 진흙 · 숲·사냥 존·목장 = 풀(DESIGN 2.2 「존 소리 풍경」) · 나무 바닥은 늪에서도 나무.
	TestEqual(TEXT("진흙 소리 풀 3개"), Sounds->CountVariants(TEXT("Footstep_Mud")), 3);
	TestEqual(TEXT("늪은 진흙 발소리"), SnareSound::FootstepCue(TEXT("zone.swamp")), FName(TEXT("Footstep_Mud")));
	for (const TCHAR* Grass : { TEXT("zone.forest"), TEXT("zone.hunt"), TEXT("zone.ranch") })
	{
		TestEqual(FString::Printf(TEXT("%s 는 풀 발소리"), Grass), SnareSound::FootstepCue(Grass), FName(TEXT("Footstep_Grass")));
	}
	TestEqual(TEXT("늪 나무 바닥은 나무 발소리"), SnareSound::FootstepCue(TEXT("zone.swamp"), true), FName(TEXT("Footstep_Wood")));
	TestEqual(TEXT("나무 소리 풀 5개"), Sounds->CountVariants(TEXT("Footstep_Wood")), 5);
	TestEqual(TEXT("나무 바닥은 존과 상관없이 나무 발소리"), SnareSound::FootstepCue(TEXT("zone.snow"), true), FName(TEXT("Footstep_Wood")));
	TestEqual(TEXT("나무 바닥 아니면 존대로"), SnareSound::FootstepCue(TEXT("zone.snow"), false), FName(TEXT("Footstep_Snow")));
	TestEqual(TEXT("없는 이름은 0개"), Sounds->CountVariants(TEXT("NoSuchCue")), 0);

	// M16-02 도구별 소리 — 반복음은 1개, 한 번 소리는 변형 3개, 곡괭이 소리(Mine)는 이미 5개.
	for (const TCHAR* Loop : { TEXT("Chainsaw_Idle"), TEXT("Chainsaw_Rev"), TEXT("ElectricHum"), TEXT("Reel") })
	{
		TestTrue(*FString::Printf(TEXT("반복음 %s"), Loop), Sounds->CountVariants(Loop) >= 1);
	}
	for (const TCHAR* Shot : { TEXT("Machete"), TEXT("Cast"), TEXT("Bite"), TEXT("Club"), TEXT("Mine") })
	{
		TestTrue(*FString::Printf(TEXT("한 번 소리 %s"), Shot), Sounds->CountVariants(Shot) >= 1);
	}
	TestNotNull(TEXT("버튼 딸깍"), Sounds->PickSound(TEXT("Ui_Click")));

	TestEqual(TEXT("물가·바다 위는 파도 최대"), SnareSound::OceanVolume(-500.f), 1.f);
	TestTrue(TEXT("안쪽 1500cm 는 중간"), FMath::IsNearlyEqual(SnareSound::OceanVolume(1500.f), 0.56f, 0.01f));
	TestEqual(TEXT("섬 한가운데는 바닥값"), SnareSound::OceanVolume(99999.f), 0.12f);
	TestEqual(TEXT("걷기 300cm/s 보폭 150"), SnareSound::StrideCm(300.f), 150.f);
	TestTrue(TEXT("달리면 보폭이 넓다"), SnareSound::StrideCm(600.f) > SnareSound::StrideCm(300.f));

	// 설정 화면 소리 크기 — 0~1 로 잘리고, 전체 크기는 엔진에 바로 걸린다. 끝나면 원래 값으로.
	const SnareSound::FVolumes Before = SnareSound::GetVolumes();
	SnareSound::FVolumes Wild;
	Wild.Master = 0.4f;
	Wild.Effects = 1.7f;
	Wild.Ambience = -0.3f;
	SnareSound::SetVolumes(Wild);
	TestEqual(TEXT("전체 0.4 보관"), SnareSound::GetVolumes().Master, 0.4f);
	TestEqual(TEXT("효과음 1 로 잘림"), SnareSound::GetVolumes().Effects, 1.f);
	TestEqual(TEXT("주변음 0 으로 잘림"), SnareSound::GetVolumes().Ambience, 0.f);
	TestEqual(TEXT("전체 크기가 엔진에 걸림"), FApp::GetVolumeMultiplier(), 0.4f);
	SnareSound::SetVolumes(Before);
	return true;
}

// M46-02 존 소리 풍경 — `AmbienceFor` 가 존 6 × 낮/밤마다 DESIGN 2.2 「존 소리 풍경」 표대로인가.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAmbienceForTest, "Snarehaven.World.Ambience.ZoneMix",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAmbienceForTest::RunTest(const FString& Parameters)
{
	struct FRow
	{
		const TCHAR* Zone;
		float Wind;
		float BirdMin;
		float BirdMax;
		float Swamp;
		bool bNightLoop;
	};
	// 평원 = 기준(바람 0.3 · 새 2.5~7초 · 0.45). 사냥 존·목장·모르는 존은 평원과 같음.
	const FRow Rows[] = {
		{ TEXT("zone.plains"), 0.3f,  2.5f, 7.f,  0.f,  true },
		{ TEXT("zone.forest"), 0.24f, 1.5f, 4.5f, 0.f,  true },
		{ TEXT("zone.snow"),   0.6f,  0.f,  0.f,  0.f,  false },
		{ TEXT("zone.swamp"),  0.21f, 5.f,  12.f, 0.5f, true },
		{ TEXT("zone.hunt"),   0.3f,  2.5f, 7.f,  0.f,  true },
		{ TEXT("zone.ranch"),  0.3f,  2.5f, 7.f,  0.f,  true },
		{ TEXT("zone.nosuch"), 0.3f,  2.5f, 7.f,  0.f,  true },
	};
	for (const FRow& Row : Rows)
	{
		const SnareSound::FAmbienceMix Day = SnareSound::AmbienceFor(Row.Zone, 0.f);
		const SnareSound::FAmbienceMix Night = SnareSound::AmbienceFor(Row.Zone, 1.f);
		const bool bBirds = Row.BirdMax > 0.f;
		TestTrue(*FString::Printf(TEXT("%s 낮 바람 %.2f"), Row.Zone, Row.Wind), FMath::IsNearlyEqual(Day.Wind, Row.Wind, 0.001f));
		TestTrue(*FString::Printf(TEXT("%s 밤 바람도 같음"), Row.Zone), FMath::IsNearlyEqual(Night.Wind, Row.Wind, 0.001f));
		TestEqual(*FString::Printf(TEXT("%s 낮 새 크기"), Row.Zone), Day.BirdVolume, bBirds ? 0.45f : 0.f);
		if (bBirds)
		{
			TestEqual(*FString::Printf(TEXT("%s 새 최소 간격"), Row.Zone), Day.BirdMinGapSeconds, Row.BirdMin);
			TestEqual(*FString::Printf(TEXT("%s 새 최대 간격"), Row.Zone), Day.BirdMaxGapSeconds, Row.BirdMax);
		}
		TestEqual(*FString::Printf(TEXT("%s 밤엔 새 0"), Row.Zone), Night.BirdVolume, 0.f);
		TestEqual(*FString::Printf(TEXT("%s 낮 늪 고리"), Row.Zone), Day.Swamp, Row.Swamp);
		TestEqual(*FString::Printf(TEXT("%s 밤 늪 고리 그대로"), Row.Zone), Night.Swamp, Row.Swamp);
		TestEqual(*FString::Printf(TEXT("%s 낮 귀뚜라미 0"), Row.Zone), Day.Night, 0.f);
		TestEqual(*FString::Printf(TEXT("%s 밤 귀뚜라미"), Row.Zone), Night.Night, Row.bNightLoop ? 0.4f : 0.f);
	}

	// 낮·밤 경계 = 밤 정도 0.5 넘으면(새소리 줄과 같음). 0.5 딱은 아직 낮.
	TestEqual(TEXT("밤 정도 0.5 는 낮 — 새 있음"), SnareSound::AmbienceFor(TEXT("zone.plains"), 0.5f).BirdVolume, 0.45f);
	TestEqual(TEXT("밤 정도 0.5 는 낮 — 귀뚜라미 0"), SnareSound::AmbienceFor(TEXT("zone.plains"), 0.5f).Night, 0.f);
	TestEqual(TEXT("밤 정도 0.51 은 밤 — 귀뚜라미"), SnareSound::AmbienceFor(TEXT("zone.plains"), 0.51f).Night, 0.4f);
	TestEqual(TEXT("존 밖(None)은 평원 바람"), SnareSound::AmbienceFor(NAME_None, 0.f).Wind, 0.3f);
	return true;
}

// M46-03 바탕 고리 바뀜 — 존을 넘으면 고리 크기가 표의 4초 동안 고르게 옮겨 가고(뚝 끊김 0), 중간값은 사이에 있는가.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAmbienceFadeTest, "Snarehaven.World.Ambience.Fade",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAmbienceFadeTest::RunTest(const FString& Parameters)
{
	// 테스트 전용 목표 — 표 숫자가 바뀌어도 안 깨지게 직접 만든 값.
	SnareSound::FAmbienceMix A;
	A.Wind = 0.2f;
	A.BirdVolume = 0.3f;
	SnareSound::FAmbienceMix B;
	B.Wind = 0.6f;
	B.Swamp = 0.8f;
	B.Night = 0.4f;
	B.BirdVolume = 0.f;

	SnareSound::FAmbienceFade Fade;
	TestTrue(TEXT("처음은 목표가 바뀐 것으로 친다"), SnareSound::StepAmbience(Fade, A, 0.1f));
	TestEqual(TEXT("처음은 바로 목표 바람"), Fade.Current.Wind, 0.2f);
	TestFalse(TEXT("같은 목표는 바뀜 아님"), SnareSound::StepAmbience(Fade, A, 0.5f));

	TestTrue(TEXT("존 넘음 = 바뀜"), SnareSound::StepAmbience(Fade, B, 0.f));
	TestEqual(TEXT("바뀐 순간은 아직 옛 자리(뚝 끊김 0)"), Fade.Current.Wind, 0.2f);
	TestEqual(TEXT("바뀐 순간 늪 0"), Fade.Current.Swamp, 0.f);
	TestEqual(TEXT("새 칸은 목표 그대로"), Fade.Current.BirdVolume, 0.f);
	SnareSound::StepAmbience(Fade, B, 1.f);
	TestEqual(TEXT("1초 = 4분의 1 바람"), Fade.Current.Wind, 0.3f, 1e-4f);
	SnareSound::StepAmbience(Fade, B, 1.f);
	TestEqual(TEXT("2초 = 절반 늪"), Fade.Current.Swamp, 0.4f, 1e-4f);
	TestEqual(TEXT("2초 = 절반 밤"), Fade.Current.Night, 0.2f, 1e-4f);
	TestTrue(TEXT("중간 바람은 옛 값과 목표 사이"), Fade.Current.Wind > 0.2f && Fade.Current.Wind < 0.6f);
	SnareSound::StepAmbience(Fade, B, 1.99f);
	TestTrue(TEXT("3.99초 = 아직 목표 전"), Fade.Current.Swamp < 0.8f);
	SnareSound::StepAmbience(Fade, B, 0.01f);
	TestEqual(TEXT("4초 = 목표 바람"), Fade.Current.Wind, 0.6f, 1e-4f);
	TestEqual(TEXT("4초 = 목표 늪"), Fade.Current.Swamp, 0.8f, 1e-4f);
	SnareSound::StepAmbience(Fade, B, 10.f);
	TestEqual(TEXT("넘쳐도 목표에 머묾"), Fade.Current.Swamp, 0.8f, 1e-4f);

	// 옮겨 가던 중에 되돌아가도 지금 들리는 자리에서 다시 시작한다.
	SnareSound::StepAmbience(Fade, A, 2.f);
	TestEqual(TEXT("되돌아감 2초 = 0.4 바람"), Fade.Current.Wind, 0.4f, 1e-4f);
	TestTrue(TEXT("중간 되돌림 = 바뀜"), SnareSound::StepAmbience(Fade, B, 0.f));
	TestEqual(TEXT("중간 되돌림 순간 = 지금 자리 0.4"), Fade.Current.Wind, 0.4f, 1e-4f);
	SnareSound::StepAmbience(Fade, B, 2.f);
	TestEqual(TEXT("다시 2초 = 0.4 와 0.6 사이 절반"), Fade.Current.Wind, 0.5f, 1e-4f);

	// 실제 표 값으로 한 번 — 평원 낮 → 늪 낮 4초 뒤 = 늪 표 값.
	SnareSound::FAmbienceFade Real;
	SnareSound::StepAmbience(Real, SnareSound::AmbienceFor(TEXT("zone.plains"), 0.f), 0.f);
	const SnareSound::FAmbienceMix Swamp = SnareSound::AmbienceFor(TEXT("zone.swamp"), 0.f);
	SnareSound::StepAmbience(Real, Swamp, 4.f);
	TestEqual(TEXT("평원→늪 4초 뒤 늪 고리 = 표"), Real.Current.Swamp, Swamp.Swamp, 1e-4f);
	TestEqual(TEXT("평원→늪 4초 뒤 바람 = 표"), Real.Current.Wind, Swamp.Wind, 1e-4f);
	return true;
}

// M46-04 새소리 존 규칙 — 새 켜짐·크기·간격이 존 표를 따르는가(설원 낮 = 새 0 · 숲 낮 = 숲 간격 · 밤 = 0 · 물가 = 0).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBirdCallTest, "Snarehaven.World.Ambience.Birds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBirdCallTest::RunTest(const FString& Parameters)
{
	// 테스트 전용 값 — 표 숫자가 바뀌어도 안 깨지게.
	SnareSound::FAmbienceMix Mix;
	Mix.BirdVolume = 0.3f;
	Mix.BirdMinGapSeconds = 2.f;
	Mix.BirdMaxGapSeconds = 6.f;
	const float Inland = 100000.f;
	SnareSound::FBirdCall Call = SnareSound::BirdCallFor(Mix, Inland, 0.f);
	TestTrue(TEXT("뭍 안쪽 = 운다"), Call.bSing);
	TestEqual(TEXT("크기 = 바탕 소리 새 크기"), Call.Volume, 0.3f);
	TestEqual(TEXT("간격 0 = 최소"), Call.NextGapSeconds, 2.f, 1e-4f);
	TestEqual(TEXT("간격 0.5 = 가운데"), SnareSound::BirdCallFor(Mix, Inland, 0.5f).NextGapSeconds, 4.f, 1e-4f);
	TestEqual(TEXT("간격 1 = 최대"), SnareSound::BirdCallFor(Mix, Inland, 1.f).NextGapSeconds, 6.f, 1e-4f);
	TestEqual(TEXT("간격 넘침은 최대에 머묾"), SnareSound::BirdCallFor(Mix, Inland, 3.f).NextGapSeconds, 6.f, 1e-4f);
	TestFalse(TEXT("물가(0cm) = 안 운다"), SnareSound::BirdCallFor(Mix, 0.f, 0.f).bSing);
	SnareSound::FAmbienceMix Silent = Mix;
	Silent.BirdVolume = 0.f;
	const SnareSound::FBirdCall Quiet = SnareSound::BirdCallFor(Silent, Inland, 0.f);
	TestFalse(TEXT("새 크기 0 = 안 운다"), Quiet.bSing);
	TestEqual(TEXT("안 울어도 다음 차례는 잡힌다"), Quiet.NextGapSeconds, 2.f, 1e-4f);
	TestTrue(TEXT("빈 간격 칸 = 0초 간격 아님"), SnareSound::BirdCallFor(SnareSound::FAmbienceMix(), Inland, 0.f).NextGapSeconds > 0.f);

	// 실제 표 — 존마다 AmbienceFor 를 거친 결과.
	TestFalse(TEXT("설원 낮 = 새 0"), SnareSound::BirdCallFor(SnareSound::AmbienceFor(TEXT("zone.snow"), 0.f), Inland, 0.f).bSing);
	TestFalse(TEXT("평원 밤 = 새 0"), SnareSound::BirdCallFor(SnareSound::AmbienceFor(TEXT("zone.plains"), 1.f), Inland, 0.f).bSing);
	const SnareSound::FAmbienceMix Forest = SnareSound::AmbienceFor(TEXT("zone.forest"), 0.f);
	TestTrue(TEXT("숲 낮 = 운다"), SnareSound::BirdCallFor(Forest, Inland, 0.f).bSing);
	TestEqual(TEXT("숲 낮 최소 간격 = 표"), SnareSound::BirdCallFor(Forest, Inland, 0.f).NextGapSeconds, Forest.BirdMinGapSeconds, 1e-4f);
	TestEqual(TEXT("숲 낮 최대 간격 = 표"), SnareSound::BirdCallFor(Forest, Inland, 1.f).NextGapSeconds, Forest.BirdMaxGapSeconds, 1e-4f);
	const SnareSound::FAmbienceMix Plains = SnareSound::AmbienceFor(TEXT("zone.plains"), 0.f);
	TestTrue(TEXT("숲이 평원보다 촘촘"), Forest.BirdMaxGapSeconds < Plains.BirdMaxGapSeconds);
	const SnareSound::FAmbienceMix Swamp = SnareSound::AmbienceFor(TEXT("zone.swamp"), 0.f);
	TestEqual(TEXT("늪 낮 최대 간격 = 표"), SnareSound::BirdCallFor(Swamp, Inland, 1.f).NextGapSeconds, Swamp.BirdMaxGapSeconds, 1e-4f);
	TestTrue(TEXT("늪이 평원보다 드묾"), Swamp.BirdMaxGapSeconds > Plains.BirdMaxGapSeconds);
	return true;
}

// M46-06 빈 존 0 — `DT_ZoneTable` 의 모든 존이 DESIGN 2.2 「존 소리 풍경」 표에 줄(자기 줄 또는 「평원과 같음」)을 갖는가.
// 새 존이 소리 줄 없이 생기면 여기가 운다. 임시 표(실제 행 + 표에 없는 존 한 줄)로 거름망이 살아 있는지도 본다(역검증 겸).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAmbienceEveryZoneTest, "Snarehaven.World.Ambience.EveryZone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAmbienceEveryZoneTest::RunTest(const FString& Parameters)
{
	const UDataTable* Table = LoadObject<UDataTable>(nullptr, TEXT("/Game/Game/Data/World/DT_ZoneTable.DT_ZoneTable"));
	if (!TestNotNull(TEXT("DT_ZoneTable 로드"), Table))
	{
		return false;
	}

	auto CountMissing = [this](const UDataTable* Zones, bool bReport)
	{
		int32 Missing = 0;
		Zones->ForeachRow<FZoneRow>(TEXT("AmbienceEveryZone"), [&](const FName& RowName, const FZoneRow& Row)
		{
			if (!SnareSound::HasAmbienceLine(Row.ZoneID))
			{
				++Missing;
				if (bReport)
				{
					AddError(FString::Printf(TEXT("%s(%s) 는 DESIGN 2.2 「존 소리 풍경」 표에 줄이 없다"), *RowName.ToString(), *Row.ZoneID.ToString()));
				}
			}
		});
		return Missing;
	};

	TestTrue(TEXT("존 행이 있다"), Table->GetRowMap().Num() > 0);
	AddInfo(FString::Printf(TEXT("존 %d 개 · 소리 줄 없는 존 %d 개"), Table->GetRowMap().Num(), CountMissing(Table, false)));
	TestEqual(TEXT("소리 줄 없는 존 = 0"), CountMissing(Table, true), 0);

	// 임시 표 — 실제 행을 옮기고 표에 없는 가짜 존 한 줄을 더한다. 이 존은 검사 안에서만 쓴다.
	UDataTable* Copy = NewObject<UDataTable>(GetTransientPackage());
	Copy->RowStruct = FZoneRow::StaticStruct();
	Table->ForeachRow<FZoneRow>(TEXT("AmbienceEveryZoneCopy"), [Copy](const FName& RowName, const FZoneRow& Row)
	{
		Copy->AddRow(RowName, Row);
	});
	FZoneRow Fake;
	Fake.ZoneID = FName(TEXT("zone.test_unlisted"));
	Copy->AddRow(FName(TEXT("Zone_TestUnlisted")), Fake);
	TestEqual(TEXT("표에 없는 존 한 줄 → 1 개 잡음"), CountMissing(Copy, false), 1);
	return true;
}

// M47-02 장비·탈것 소리 규칙 (DESIGN 2.2 「장비·탈것 소리」) — 시험 전용 행이라 표 값이 바뀌어도 묶음만 본다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenEquipCueTest, "Snarehaven.World.EquipSound",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenEquipCueTest::RunTest(const FString& Parameters)
{
	auto HandRow = [](const TCHAR* UseSound, const TCHAR* Ammo)
	{
		FItemRow Row;
		Row.EquipSlot = EEquipSlot::Tool;
		Row.UseSoundCue = UseSound ? FName(UseSound) : NAME_None;
		Row.AmmoItemID = Ammo ? FName(Ammo) : NAME_None;
		return Row;
	};

	const FItemRow Pistol = HandRow(nullptr, TEXT("item.test_ammo"));
	const FItemRow Hatchet = HandRow(TEXT("Chop"), nullptr);
	const FItemRow Machete = HandRow(TEXT("Machete"), nullptr);
	const FItemRow Club = HandRow(TEXT("Club"), nullptr);
	const FItemRow Pickaxe = HandRow(TEXT("Mine"), nullptr);
	const FItemRow Rod = HandRow(TEXT("Cast"), nullptr);
	const FItemRow Unknown = HandRow(TEXT("TestUnknownKind"), nullptr);
	const FItemRow Blank = HandRow(nullptr, nullptr);

	TestEqual(TEXT("총 = 철컥"), SnareSound::EquipCue(&Pistol).Cue, FName(TEXT("EquipGun")));
	TestEqual(TEXT("손도끼 = 칼날"), SnareSound::EquipCue(&Hatchet).Cue, FName(TEXT("EquipBlade")));
	TestEqual(TEXT("마체테 = 칼날"), SnareSound::EquipCue(&Machete).Cue, FName(TEXT("EquipBlade")));
	TestEqual(TEXT("몽둥이 = 둔기"), SnareSound::EquipCue(&Club).Cue, FName(TEXT("EquipBlunt")));
	TestEqual(TEXT("곡괭이 = 둔기"), SnareSound::EquipCue(&Pickaxe).Cue, FName(TEXT("EquipBlunt")));
	TestEqual(TEXT("낚싯대 = 도구(둔기 빌림)"), SnareSound::EquipCue(&Rod).Cue, FName(TEXT("EquipBlunt")));
	TestEqual(TEXT("모르는 종류 = 도구"), SnareSound::EquipCue(&Unknown).Cue, FName(TEXT("EquipBlunt")));
	TestEqual(TEXT("소리 칸 빈 도구 = 도구"), SnareSound::EquipCue(&Blank).Cue, FName(TEXT("EquipBlunt")));
	TestEqual(TEXT("빈손 = 넣기"), SnareSound::EquipCue(nullptr).Cue, FName(TEXT("Holster")));
	TestTrue(TEXT("꺼내기·넣기는 모든 기기"), SnareSound::EquipCue(&Club).bWorld && SnareSound::EquipCue(nullptr).bWorld);
	TestTrue(TEXT("꺼내기 크기·간격이 있다"), SnareSound::EquipCue(&Club).Volume > 0.f && SnareSound::EquipCue(&Club).MinGapSeconds > 0.f);

	for (const EEquipSlot Slot : { EEquipSlot::OutfitTop, EEquipSlot::OutfitBottom, EEquipSlot::Backpack, EEquipSlot::Head,
		EEquipSlot::FaceAccessory, EEquipSlot::Shoes, EEquipSlot::Gloves })
	{
		const SnareSound::FChangeCue Cue = SnareSound::OutfitCue(Slot);
		TestEqual(FString::Printf(TEXT("옷 부위 %d = 부스럭"), static_cast<int32>(Slot)), Cue.Cue, FName(TEXT("Outfit")));
		TestFalse(FString::Printf(TEXT("옷 부위 %d = 내 기기만"), static_cast<int32>(Slot)), Cue.bWorld);
	}
	for (const EEquipSlot Slot : { EEquipSlot::None, EEquipSlot::Weapon, EEquipSlot::Tool })
	{
		TestTrue(FString::Printf(TEXT("옷 아닌 칸 %d = 무음"), static_cast<int32>(Slot)), SnareSound::OutfitCue(Slot).Cue.IsNone());
	}

	TestEqual(TEXT("타기"), SnareSound::MountCue(true).Cue, FName(TEXT("MountOn")));
	TestEqual(TEXT("내리기"), SnareSound::MountCue(false).Cue, FName(TEXT("MountOff")));
	TestTrue(TEXT("탈것은 모든 기기"), SnareSound::MountCue(true).bWorld && SnareSound::MountCue(false).bWorld);
	TestTrue(TEXT("탈것 간격이 손 칸보다 길다"), SnareSound::MountCue(true).MinGapSeconds > SnareSound::EquipCue(&Club).MinGapSeconds);

	// 고른 이름마다 반입된 소리가 있어야 한다(M47-01 21개).
	USnareSoundSubsystem* const Sounds = NewObject<USnareSoundSubsystem>();
	for (const TCHAR* Cue : { TEXT("EquipGun"), TEXT("EquipBlade"), TEXT("EquipBlunt"), TEXT("Holster"), TEXT("Outfit"), TEXT("MountOn"), TEXT("MountOff") })
	{
		TestEqual(FString::Printf(TEXT("%s 변형 3개"), Cue), Sounds->CountVariants(Cue), 3);
	}
	return true;
}

// M49-02 가방·상자 소리 규칙 (DESIGN 8.12 「가방·상자 소리」) — 행동 4 × 받아들임/거절, 빈 이름 0.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBagCueTest, "Snarehaven.World.BagSound",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBagCueTest::RunTest(const FString& Parameters)
{
	using SnareSound::EBagAction;
	const EBagAction Actions[] = { EBagAction::Drop, EBagAction::DeathDrop, EBagAction::Pickup, EBagAction::OpenChest };
	const TCHAR* Expected[] = { TEXT("BagDrop"), TEXT("BagDrop"), TEXT("BagPickup"), TEXT("ChestOpen") };

	USnareSoundSubsystem* const Sounds = NewObject<USnareSoundSubsystem>();
	int32 Sounding = 0;
	int32 Silent = 0;
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Actions); ++Index)
	{
		const SnareSound::FChangeCue Ok = SnareSound::BagCue(Actions[Index], true, FString());
		TestEqual(FString::Printf(TEXT("행동 %d 받아들임 = %s"), Index, Expected[Index]), Ok.Cue, FName(Expected[Index]));
		TestTrue(FString::Printf(TEXT("행동 %d 크기 > 0"), Index), Ok.Volume > 0.f);
		TestEqual(FString::Printf(TEXT("행동 %d 반입 변형 3개"), Index), Sounds->CountVariants(Ok.Cue), 3);
		Sounding += Ok.Cue.IsNone() ? 0 : 1;

		for (const TCHAR* Code : { TEXT("already_taken"), TEXT("expired"), TEXT("inventory_full"), TEXT("not_found"), TEXT("") })
		{
			const SnareSound::FChangeCue Rejected = SnareSound::BagCue(Actions[Index], false, Code);
			TestTrue(FString::Printf(TEXT("행동 %d 거절(%s) = 무음"), Index, Code), Rejected.Cue.IsNone());
			Silent += Rejected.Cue.IsNone() ? 1 : 0;
		}
		TestTrue(FString::Printf(TEXT("행동 %d 받아들임인데 오류 코드 = 무음"), Index),
			SnareSound::BagCue(Actions[Index], true, TEXT("already_taken")).Cue.IsNone());
	}
	TestEqual(TEXT("받아들임 4 → 소리 4"), Sounding, 4);
	TestEqual(TEXT("거절 20 → 무음 20"), Silent, 20);

	TestTrue(TEXT("가방 셋은 모든 기기"), SnareSound::BagCue(EBagAction::Drop, true, FString()).bWorld
		&& SnareSound::BagCue(EBagAction::DeathDrop, true, FString()).bWorld
		&& SnareSound::BagCue(EBagAction::Pickup, true, FString()).bWorld);
	TestFalse(TEXT("상자는 내 기기만"), SnareSound::BagCue(EBagAction::OpenChest, true, FString()).bWorld);
	return true;
}


// M50-02 목장 소리 규칙 (DESIGN 3.2 「목장 소리」) — 7갈래 × 받아들임/거절, 수확 3·심기·모르는 함수는 빈 이름.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenRanchCueTest, "Snarehaven.World.RanchSound",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenRanchCueTest::RunTest(const FString& Parameters)
{
	const TCHAR* Functions[] = { TEXT("RegisterPet"), TEXT("RemovePet"), TEXT("SetPetCosmetic"), TEXT("SetRanchDeco"),
		TEXT("ExpandRanchSlot"), TEXT("FeedRanchUpkeep"), TEXT("RegisterFishingSpot") };
	const FName Expected[] = { TEXT("RanchWelcome"), TEXT("RanchRelease"), TEXT("Outfit"), SnareBuildRules::PlaceConfirmCue(),
		TEXT("RanchExpand"), TEXT("Storage"), TEXT("Splash") };

	USnareSoundSubsystem* const Sounds = NewObject<USnareSoundSubsystem>();
	int32 Sounding = 0;
	int32 Silent = 0;
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Functions); ++Index)
	{
		const SnareSound::FChangeCue Ok = SnareSound::RanchCue(Functions[Index], true, FString());
		TestEqual(FString::Printf(TEXT("%s 받아들임 = %s"), Functions[Index], *Expected[Index].ToString()), Ok.Cue, Expected[Index]);
		TestTrue(FString::Printf(TEXT("%s 크기 > 0"), Functions[Index]), Ok.Volume > 0.f);
		TestTrue(FString::Printf(TEXT("%s 반입 변형 3개 이상"), Functions[Index]), Sounds->CountVariants(Ok.Cue) >= 3);
		TestFalse(FString::Printf(TEXT("%s 내 기기만"), Functions[Index]), Ok.bWorld);
		TestEqual(FString::Printf(TEXT("%s 한 응답 한 번(간격 0)"), Functions[Index]), Ok.MinGapSeconds, 0.f);
		Sounding += Ok.Cue.IsNone() ? 0 : 1;

		for (const TCHAR* Code : { TEXT("locked"), TEXT("full"), TEXT("not_enough_items"), TEXT("bad_args"), TEXT("already"), TEXT("") })
		{
			const SnareSound::FChangeCue Rejected = SnareSound::RanchCue(Functions[Index], false, Code);
			TestTrue(FString::Printf(TEXT("%s 거절(%s) = 무음"), Functions[Index], Code), Rejected.Cue.IsNone());
			Silent += Rejected.Cue.IsNone() ? 1 : 0;
		}
		TestTrue(FString::Printf(TEXT("%s 받아들임인데 오류 코드 = 무음"), Functions[Index]),
			SnareSound::RanchCue(Functions[Index], true, TEXT("locked")).Cue.IsNone());
	}
	TestEqual(TEXT("받아들임 7 → 소리 7"), Sounding, 7);
	TestEqual(TEXT("거절 42 → 무음 42"), Silent, 42);

	// 수확 3·심기는 기존 길(`CelebrateHarvest`·`PlayActionSound`)이 낸다 — 여기서 또 내면 두 번 울린다.
	for (const TCHAR* Own : { TEXT("HarvestRanch"), TEXT("HarvestCrop"), TEXT("HarvestFishing"), TEXT("PlantCrop") })
	{
		TestTrue(FString::Printf(TEXT("%s = 빈 이름"), Own), SnareSound::RanchCue(Own, true, FString()).Cue.IsNone());
	}
	TestTrue(TEXT("모르는 함수 = 빈 이름"), SnareSound::RanchCue(TEXT("NoSuchRanchFunction"), true, FString()).Cue.IsNone());
	TestTrue(TEXT("빈 함수 이름 = 빈 이름"), SnareSound::RanchCue(FString(), true, FString()).Cue.IsNone());
	return true;
}

// M51-02 배 소리 규칙 (DESIGN 8.9 「배 소리」) — 4갈래 × 받아들임/거절, 모르는 동작은 빈 이름.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBoatCueTest, "Snarehaven.World.BoatSound",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBoatCueTest::RunTest(const FString& Parameters)
{
	using SnareSound::EBoatAction;
	const EBoatAction Actions[] = { EBoatAction::Launch, EBoatAction::Board, EBoatAction::Leave, EBoatAction::Stow };
	const FName Expected[] = { TEXT("BoatLaunch"), TEXT("BoatBoard"), TEXT("BoatLeave"), TEXT("Storage") };
	const float ExpectedVolume[] = { 0.8f, 0.7f, 0.6f, 0.7f };
	const bool bExpectedWorld[] = { true, false, false, true };

	USnareSoundSubsystem* const Sounds = NewObject<USnareSoundSubsystem>();
	int32 Sounding = 0;
	int32 Silent = 0;
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Actions); ++Index)
	{
		const SnareSound::FChangeCue Ok = SnareSound::BoatCue(Actions[Index], true, FString());
		TestEqual(FString::Printf(TEXT("동작 %d 받아들임 = %s"), Index, *Expected[Index].ToString()), Ok.Cue, Expected[Index]);
		TestEqual(FString::Printf(TEXT("동작 %d 크기"), Index), Ok.Volume, ExpectedVolume[Index]);
		TestEqual(FString::Printf(TEXT("동작 %d 월드 소리 여부"), Index), Ok.bWorld, bExpectedWorld[Index]);
		TestEqual(FString::Printf(TEXT("동작 %d 간격 0"), Index), Ok.MinGapSeconds, 0.f);
		TestTrue(FString::Printf(TEXT("동작 %d 반입 변형 3개 이상"), Index), Sounds->CountVariants(Ok.Cue) >= 3);
		Sounding += Ok.Cue.IsNone() ? 0 : 1;

		for (const TCHAR* Code : { TEXT("already"), TEXT("not_launched"), TEXT("locked"), TEXT("bad_args") })
		{
			Silent += SnareSound::BoatCue(Actions[Index], false, Code).Cue.IsNone() ? 1 : 0;
		}
		Silent += SnareSound::BoatCue(Actions[Index], false, FString()).Cue.IsNone() ? 1 : 0;
		TestTrue(FString::Printf(TEXT("동작 %d 받아들임인데 오류 코드 = 무음"), Index),
			SnareSound::BoatCue(Actions[Index], true, TEXT("already")).Cue.IsNone());
	}
	TestEqual(TEXT("받아들임 4 → 소리 4"), Sounding, 4);
	TestEqual(TEXT("거절 20 → 무음 20"), Silent, 20);
	TestTrue(TEXT("모르는 동작 = 빈 이름"), SnareSound::BoatCue(static_cast<EBoatAction>(99), true, FString()).Cue.IsNone());
	return true;
}

// M53-02 존·광산 건너기 소리 규칙 (DESIGN 8.10) — 3갈래 × 확정/거절, 모르는 갈래는 빈 이름.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenZoneCueTest, "Snarehaven.World.ZoneSound",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenZoneCueTest::RunTest(const FString& Parameters)
{
	using SnareSound::EZoneCrossing;
	const EZoneCrossing Crossings[] = { EZoneCrossing::Portal, EZoneCrossing::Ferry, EZoneCrossing::MineDoor };
	const FName Expected[] = { TEXT("ZoneWarp"), TEXT("BoatBoard"), TEXT("MineDoor") };
	const float ExpectedVolume[] = { 0.8f, 0.7f, 0.8f };
	const bool bExpectedWorld[] = { false, true, true };

	USnareSoundSubsystem* const Sounds = NewObject<USnareSoundSubsystem>();
	int32 Sounding = 0;
	int32 Silent = 0;
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Crossings); ++Index)
	{
		const SnareSound::FChangeCue Ok = SnareSound::ZoneCue(Crossings[Index], true);
		TestEqual(FString::Printf(TEXT("갈래 %d 확정 = %s"), Index, *Expected[Index].ToString()), Ok.Cue, Expected[Index]);
		TestEqual(FString::Printf(TEXT("갈래 %d 크기"), Index), Ok.Volume, ExpectedVolume[Index]);
		TestEqual(FString::Printf(TEXT("갈래 %d 월드 소리 여부"), Index), Ok.bWorld, bExpectedWorld[Index]);
		TestEqual(FString::Printf(TEXT("갈래 %d 간격 0"), Index), Ok.MinGapSeconds, 0.f);
		TestTrue(FString::Printf(TEXT("갈래 %d 반입 변형 3개 이상"), Index), Sounds->CountVariants(Ok.Cue) >= 3);
		Sounding += Ok.Cue.IsNone() ? 0 : 1;
		Silent += SnareSound::ZoneCue(Crossings[Index], false).Cue.IsNone() ? 1 : 0;
	}
	TestEqual(TEXT("확정 3 → 소리 3"), Sounding, 3);
	TestEqual(TEXT("거절 3 → 무음 3"), Silent, 3);
	TestTrue(TEXT("모르는 갈래 = 빈 이름"), SnareSound::ZoneCue(static_cast<EZoneCrossing>(99), true).Cue.IsNone());
	return true;
}

// M54-02 감정표현 말풍선 소리 규칙 (DESIGN 2.2) — 여섯 번호 모두 같은 소리, 범위 밖은 무음.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenEmoteCueTest, "Snarehaven.World.EmoteSound",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenEmoteCueTest::RunTest(const FString& Parameters)
{
	USnareSoundSubsystem* const Sounds = NewObject<USnareSoundSubsystem>();
	TestEqual(TEXT("말풍선 여섯"), SnareEmoteBubble::Num(), 6);
	int32 Sounding = 0;
	for (int32 Index = 0; Index < SnareEmoteBubble::Num(); ++Index)
	{
		const SnareSound::FChangeCue Cue = SnareEmoteBubble::CueFor(Index);
		TestEqual(FString::Printf(TEXT("%d번 소리 이름"), Index), Cue.Cue, FName(TEXT("EmotePop")));
		TestEqual(FString::Printf(TEXT("%d번 크기"), Index), Cue.Volume, 0.5f);
		TestEqual(FString::Printf(TEXT("%d번 연타 간격"), Index), Cue.MinGapSeconds, 0.3f);
		TestTrue(FString::Printf(TEXT("%d번 월드 소리"), Index), Cue.bWorld);
		TestTrue(FString::Printf(TEXT("%d번 반입 변형 3개 이상"), Index), Sounds->CountVariants(Cue.Cue) >= 3);
		Sounding += Cue.Cue.IsNone() ? 0 : 1;
	}
	TestEqual(TEXT("여섯 번호 → 소리 6"), Sounding, 6);
	TestTrue(TEXT("범위 밖(6) = 무음"), SnareEmoteBubble::CueFor(SnareEmoteBubble::Num()).Cue.IsNone());
	TestTrue(TEXT("음수 = 무음"), SnareEmoteBubble::CueFor(-1).Cue.IsNone());
	TestTrue(TEXT("매우 큰 번호 = 무음"), SnareEmoteBubble::CueFor(255).Cue.IsNone());
	return true;
}

// M55-02 막힘 소리 규칙 (DESIGN 2.2 「막힘 소리」) — 유효 → Deny, 빈 문구·간격 안 → 무음.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenDenyCueTest, "Snarehaven.World.DenySound",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenDenyCueTest::RunTest(const FString& Parameters)
{
	USnareSoundSubsystem* const Sounds = NewObject<USnareSoundSubsystem>();
	const SnareSound::FChangeCue Ok = SnareSound::DenyCue(false, 10.f);
	TestEqual(TEXT("유효 = Deny"), Ok.Cue, FName(TEXT("Deny")));
	TestEqual(TEXT("크기"), Ok.Volume, 0.45f);
	TestEqual(TEXT("연타 간격"), Ok.MinGapSeconds, 0.4f);
	TestFalse(TEXT("내 기기만(Play2D)"), Ok.bWorld);
	TestTrue(TEXT("반입 변형 3개 이상"), Sounds->CountVariants(Ok.Cue) >= 3);
	TestTrue(TEXT("빈 문구 = 무음"), SnareSound::DenyCue(true, 10.f).Cue.IsNone());
	TestTrue(TEXT("간격 안(0.1초) = 무음"), SnareSound::DenyCue(false, 0.1f).Cue.IsNone());
	TestTrue(TEXT("간격 바로 앞(0.39초) = 무음"), SnareSound::DenyCue(false, 0.39f).Cue.IsNone());
	TestEqual(TEXT("간격 딱 0.4초 = 소리"), SnareSound::DenyCue(false, 0.4f).Cue, FName(TEXT("Deny")));
	TestTrue(TEXT("빈 문구 + 간격 안 = 무음"), SnareSound::DenyCue(true, 0.f).Cue.IsNone());
	return true;
}

// M56-02 숨참 소리 규칙 (DESIGN 2.2 「숨참 소리」) — 물속·스태미나 0·간격 지남 → Gasp, 나머지 → 무음.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenGaspCueTest, "Snarehaven.World.GaspSound",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenGaspCueTest::RunTest(const FString& Parameters)
{
	USnareSoundSubsystem* const Sounds = NewObject<USnareSoundSubsystem>();
	const SnareSound::FChangeCue Ok = SnareGasp::CueFor(true, 0.f, 10.f);
	TestEqual(TEXT("바닥 = Gasp"), Ok.Cue, FName(TEXT("Gasp")));
	TestEqual(TEXT("크기"), Ok.Volume, 0.7f);
	TestEqual(TEXT("되풀이 간격"), Ok.MinGapSeconds, 2.f);
	TestTrue(TEXT("모든 기기 월드 소리"), Ok.bWorld);
	TestTrue(TEXT("반입 변형 3개 이상"), Sounds->CountVariants(Ok.Cue) >= 3);
	TestTrue(TEXT("물 밖 = 무음"), SnareGasp::CueFor(false, 0.f, 10.f).Cue.IsNone());
	TestTrue(TEXT("스태미나 남음 = 무음"), SnareGasp::CueFor(true, 0.1f, 10.f).Cue.IsNone());
	TestTrue(TEXT("간격 안(1.9초) = 무음"), SnareGasp::CueFor(true, 0.f, 1.9f).Cue.IsNone());
	TestEqual(TEXT("간격 딱 2.0초 = 소리"), SnareGasp::CueFor(true, 0.f, 2.f).Cue, FName(TEXT("Gasp")));
	TestTrue(TEXT("물 밖 + 스태미나 남음 + 간격 안 = 무음"), SnareGasp::CueFor(false, 50.f, 0.f).Cue.IsNone());
	return true;
}

// M57-02 추위 떨림 소리 규칙 (DESIGN 2.2 「추위 떨림 소리」) — 실제 피해·간격 지남 → Shiver, 나머지 → 무음.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenShiverCueTest, "Snarehaven.World.ShiverSound",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenShiverCueTest::RunTest(const FString& Parameters)
{
	USnareSoundSubsystem* const Sounds = NewObject<USnareSoundSubsystem>();
	const SnareSound::FChangeCue Ok = SnareShiver::CueFor(2.f, 0.f, 10.f);
	TestEqual(TEXT("깎임 = Shiver"), Ok.Cue, FName(TEXT("Shiver")));
	TestEqual(TEXT("크기"), Ok.Volume, 0.6f);
	TestEqual(TEXT("되풀이 간격"), Ok.MinGapSeconds, 3.f);
	TestTrue(TEXT("모든 기기 월드 소리"), Ok.bWorld);
	TestTrue(TEXT("반입 변형 3개 이상"), Sounds->CountVariants(Ok.Cue) >= 3);
	TestTrue(TEXT("칸 밖(피해 0) = 무음"), SnareShiver::CueFor(0.f, 0.f, 10.f).Cue.IsNone());
	TestTrue(TEXT("추위 막이 100% = 무음"), SnareShiver::CueFor(2.f, 1.f, 10.f).Cue.IsNone());
	TestEqual(TEXT("추위 막이 50% = 소리"), SnareShiver::CueFor(2.f, 0.5f, 10.f).Cue, FName(TEXT("Shiver")));
	TestTrue(TEXT("간격 안(2.9초) = 무음"), SnareShiver::CueFor(2.f, 0.f, 2.9f).Cue.IsNone());
	TestEqual(TEXT("간격 딱 3.0초 = 소리"), SnareShiver::CueFor(2.f, 0.f, 3.f).Cue, FName(TEXT("Shiver")));
	TestTrue(TEXT("칸 밖 + 막이 + 간격 안 = 무음"), SnareShiver::CueFor(0.f, 1.f, 0.f).Cue.IsNone());
	return true;
}

// M58-02 불 옆 온기 소리 규칙 (DESIGN 2.2 「불 옆 온기 소리」) — 회복 중·안 가득·간격 지남 → Warmth, 나머지 → 무음.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenWarmthCueTest, "Snarehaven.World.WarmthSound",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenWarmthCueTest::RunTest(const FString& Parameters)
{
	USnareSoundSubsystem* const Sounds = NewObject<USnareSoundSubsystem>();
	const SnareSound::FChangeCue Ok = SnareWarmth::CueFor(1.f, false, 10.f);
	TestEqual(TEXT("회복 = Warmth"), Ok.Cue, FName(TEXT("Warmth")));
	TestEqual(TEXT("크기"), Ok.Volume, 0.5f);
	TestEqual(TEXT("되풀이 간격"), Ok.MinGapSeconds, 4.f);
	TestTrue(TEXT("모든 기기 월드 소리"), Ok.bWorld);
	TestTrue(TEXT("반입 변형 3개 이상"), Sounds->CountVariants(Ok.Cue) >= 3);
	TestTrue(TEXT("불 밖(회복 0) = 무음"), SnareWarmth::CueFor(0.f, false, 10.f).Cue.IsNone());
	TestTrue(TEXT("체력 가득 = 무음"), SnareWarmth::CueFor(1.f, true, 10.f).Cue.IsNone());
	TestTrue(TEXT("간격 안(3.8초) = 무음"), SnareWarmth::CueFor(1.f, false, 3.8f).Cue.IsNone());
	TestEqual(TEXT("간격 딱 4.0초 = 소리"), SnareWarmth::CueFor(1.f, false, 4.f).Cue, FName(TEXT("Warmth")));
	TestTrue(TEXT("불 밖 + 가득 + 간격 안 = 무음"), SnareWarmth::CueFor(0.f, true, 0.f).Cue.IsNone());
	return true;
}

// M59-02 부활 소리 규칙 (DESIGN 2.2 「부활 소리」) — 쓰러져 있다 일어섬 → Revive, 평소 → 무음.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenReviveCueTest, "Snarehaven.World.ReviveSound",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenReviveCueTest::RunTest(const FString& Parameters)
{
	USnareSoundSubsystem* const Sounds = NewObject<USnareSoundSubsystem>();
	const SnareSound::FChangeCue Ok = SnareRevive::CueFor(true);
	TestEqual(TEXT("쓰러졌다 일어섬 = Revive"), Ok.Cue, FName(TEXT("Revive")));
	TestEqual(TEXT("크기"), Ok.Volume, 0.9f);
	TestEqual(TEXT("간격 없음"), Ok.MinGapSeconds, 0.f);
	TestTrue(TEXT("모든 기기 월드 소리"), Ok.bWorld);
	TestTrue(TEXT("반입 변형 3개 이상"), Sounds->CountVariants(Ok.Cue) >= 3);
	TestTrue(TEXT("쓰러진 적 없음 = 무음"), SnareRevive::CueFor(false).Cue.IsNone());
	return true;
}

// M60-02 위험 체력 규칙 (DESIGN 2.2 「위험 체력 알림」) — 문턱 위 0 · 아래 비례 · 체력 0/쓰러짐/최대 0 방어.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenLowHealthStateTest, "Snarehaven.World.LowHealthState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenLowHealthStateTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("문턱 위 = 0"), SnareLowHealth::StateFor(50.f, 100.f, false), 0.f);
	TestEqual(TEXT("문턱 딱 = 0"), SnareLowHealth::StateFor(30.f, 100.f, false), 0.f);
	TestEqual(TEXT("문턱 절반 = 0.5"), SnareLowHealth::StateFor(15.f, 100.f, false), 0.5f);
	TestEqual(TEXT("바닥 근처 = 1 쪽"), SnareLowHealth::StateFor(3.f, 100.f, false), 0.9f);
	TestEqual(TEXT("체력 0 = 0"), SnareLowHealth::StateFor(0.f, 100.f, false), 0.f);
	TestEqual(TEXT("쓰러짐 = 0"), SnareLowHealth::StateFor(10.f, 100.f, true), 0.f);
	TestEqual(TEXT("최대 0 방어"), SnareLowHealth::StateFor(10.f, 0.f, false), 0.f);
	TestTrue(TEXT("음수 체력 방어"), SnareLowHealth::StateFor(-5.f, 100.f, false) == 0.f);
	return true;
}


// M60-04 심장 소리 연결 — 위험 → 간격마다 1 · 평소 → 0 · 쓰러짐 → 0 · 부활 뒤 0.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenHeartbeatPlayTest, "Snarehaven.World.HeartbeatPlay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenHeartbeatPlayTest::RunTest(const FString& Parameters)
{
	UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());

	// 테스트 전용 값 — 체력 100, 회복·소모 0.
	FPlayerVitalsRow Row;
	Row.MaxHealth = 100.f;
	Row.MaxStamina = 10.f;
	Row.RespawnHealth = 100.f;
	Row.RespawnStamina = 10.f;
	Row.RunStaminaPerSecond = 0.f;
	Row.AttackStaminaCost = 0.f;
	Row.StaminaRegenPerSecond = 0.f;
	Row.StaminaRegenDelaySeconds = 0.f;
	Row.ExhaustionHealthPerSecond = 0.f;
	Row.HealthRegenPerSecond = 0.f;
	Row.HealthRegenStaminaThreshold = 0.f;
	Row.DeathLingerSeconds = 0.f;
	auto MakePlayer = [&](float Damage) -> ASnarehavenCharacter*
	{
		ASnarehavenCharacter* const Player = World->SpawnActor<ASnarehavenCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
		if (Player == nullptr || !IsValid(Player->GetPlayerVitals()) || !Player->GetPlayerVitals()->InitializeFromRow(Row))
		{
			return nullptr;
		}
		if (Damage > 0.f)
		{
			Player->GetPlayerVitals()->ApplyDamage(Damage);
		}
		return Player;
	};

	ASnarehavenCharacter* const Low = MakePlayer(90.f);   // 체력 10 → 세기 0.67, 간격 약 0.8초
	ASnarehavenCharacter* const Fine = MakePlayer(0.f);
	if (Low == nullptr || Fine == nullptr)
	{
		AddError(TEXT("플레이어를 못 세웠다"));
	}
	else
	{
		Low->TickHeartbeatSound(0.1f);
		TestEqual(TEXT("위험 = 첫 박동 1"), Low->GetHeartbeatPlayCount(), 1);
		Low->TickHeartbeatSound(0.3f);
		TestEqual(TEXT("간격 안(0.3초) = 늘지 않음"), Low->GetHeartbeatPlayCount(), 1);
		Low->TickHeartbeatSound(1.3f);
		TestEqual(TEXT("간격 지남 = 2"), Low->GetHeartbeatPlayCount(), 2);
		Fine->TickHeartbeatSound(5.f);
		TestEqual(TEXT("평소 = 0"), Fine->GetHeartbeatPlayCount(), 0);
		Low->GetPlayerVitals()->KillForCheck();
		Low->TickHeartbeatSound(5.f);
		TestEqual(TEXT("쓰러짐 = 늘지 않음"), Low->GetHeartbeatPlayCount(), 2);
		Low->GetPlayerVitals()->ReviveAtRespawnValues();
		Low->TickHeartbeatSound(5.f);
		TestEqual(TEXT("부활(체력 가득) 뒤 = 늘지 않음"), Low->GetHeartbeatPlayCount(), 2);
	}

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

// M57-03 추위 떨림 재생 연결 — 캐릭터 틱이 CueFor 를 거쳐 소리를 낸다: 깎임 → 1 · 칸 밖 → 0 · 추위 막이 → 0 · 간격 안 → 0.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenShiverPlayTest, "Snarehaven.World.ShiverPlay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenShiverPlayTest::RunTest(const FString& Parameters)
{
	UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());

	// 테스트 전용 값 — 체력 1000(쓰러지지 않게), 회복·소모 0.
	FPlayerVitalsRow Row;
	Row.MaxHealth = 1000.f;
	Row.MaxStamina = 10.f;
	Row.RespawnHealth = 1000.f;
	Row.RespawnStamina = 10.f;
	Row.RunStaminaPerSecond = 0.f;
	Row.AttackStaminaCost = 0.f;
	Row.StaminaRegenPerSecond = 0.f;
	Row.StaminaRegenDelaySeconds = 0.f;
	Row.ExhaustionHealthPerSecond = 0.f;
	Row.HealthRegenPerSecond = 0.f;
	Row.HealthRegenStaminaThreshold = 0.f;
	Row.DeathLingerSeconds = 0.f;
	auto MakePlayer = [&](float Drain, float Resist) -> ASnarehavenCharacter*
	{
		ASnarehavenCharacter* const Player = World->SpawnActor<ASnarehavenCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
		if (Player == nullptr || !IsValid(Player->GetPlayerVitals()) || !Player->GetPlayerVitals()->InitializeFromRow(Row))
		{
			return nullptr;
		}
		Player->GetPlayerVitals()->SetZoneHealthDrain(Drain);
		if (Resist > 0.f)
		{
			Player->GetPlayerVitals()->ApplyBuff(EBuffKind::ColdResist, Resist, 100.f);
		}
		return Player;
	};

	ASnarehavenCharacter* const Cold = MakePlayer(2.f, 0.f);
	ASnarehavenCharacter* const Outside = MakePlayer(0.f, 0.f);
	ASnarehavenCharacter* const Warm = MakePlayer(2.f, 1.f);
	if (Cold == nullptr || Outside == nullptr || Warm == nullptr)
	{
		AddError(TEXT("플레이어를 못 세웠다"));
	}
	else
	{
		Cold->TickShiverSound(0.1f);
		TestEqual(TEXT("깎임 = 소리 1"), Cold->GetShiverPlayCount(), 1);
		Cold->TickShiverSound(2.0f);
		TestEqual(TEXT("간격 안(2.0초) = 늘지 않음"), Cold->GetShiverPlayCount(), 1);
		Cold->TickShiverSound(1.0f);
		TestEqual(TEXT("간격 지남(3.0초) = 2"), Cold->GetShiverPlayCount(), 2);
		Outside->TickShiverSound(0.1f);
		TestEqual(TEXT("칸 밖 = 0"), Outside->GetShiverPlayCount(), 0);
		Warm->TickShiverSound(0.1f);
		TestEqual(TEXT("추위 막이 100% = 0"), Warm->GetShiverPlayCount(), 0);
		Cold->GetPlayerVitals()->KillForCheck();
		Cold->TickShiverSound(5.f);
		TestEqual(TEXT("쓰러진 뒤 = 늘지 않음"), Cold->GetShiverPlayCount(), 2);
	}

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

// M56-03 숨참 재생 연결 — 캐릭터 틱이 CueFor 를 거쳐 소리를 낸다: 바닥 → 1 · 물 밖 → 0 · 스태미나 남음 → 0 · 간격 안 → 0.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenGaspPlayTest, "Snarehaven.World.GaspPlay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenGaspPlayTest::RunTest(const FString& Parameters)
{
	UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());

	// 테스트 전용 값 — 스태미나 10, 수영 초당 100(1초면 바닥), 익사 체력 0 이라 체력은 안 깎인다.
	FPlayerVitalsRow Row;
	Row.MaxHealth = 100.f;
	Row.MaxStamina = 10.f;
	Row.RespawnHealth = 100.f;
	Row.RespawnStamina = 10.f;
	Row.RunStaminaPerSecond = 0.f;
	Row.AttackStaminaCost = 0.f;
	Row.StaminaRegenPerSecond = 0.f;
	Row.StaminaRegenDelaySeconds = 0.f;
	Row.ExhaustionHealthPerSecond = 0.f;
	Row.HealthRegenPerSecond = 0.f;
	Row.HealthRegenStaminaThreshold = 0.f;
	Row.DeathLingerSeconds = 0.f;
	auto MakePlayer = [&]() -> ASnarehavenCharacter*
	{
		ASnarehavenCharacter* const Player = World->SpawnActor<ASnarehavenCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
		return (Player != nullptr && IsValid(Player->GetPlayerVitals()) && Player->GetPlayerVitals()->InitializeFromRow(Row))
			? Player : nullptr;
	};

	ASnarehavenCharacter* const Swimmer = MakePlayer();
	ASnarehavenCharacter* const Fresh = MakePlayer();
	ASnarehavenCharacter* const Dry = MakePlayer();
	if (Swimmer == nullptr || Fresh == nullptr || Dry == nullptr)
	{
		AddError(TEXT("플레이어를 못 세웠다"));
	}
	else
	{
		Swimmer->GetPlayerVitals()->SetSwimState(true, 100.f, 0.f);
		Fresh->GetPlayerVitals()->SetSwimState(true, 100.f, 0.f);
		Dry->GetPlayerVitals()->SetSwimState(false, 100.f, 0.f);
		Swimmer->GetPlayerVitals()->AdvanceTime(1.f);   // 바닥
		Dry->GetPlayerVitals()->AdvanceTime(1.f);
		TestEqual(TEXT("수영 스태미나 바닥"), Swimmer->GetPlayerVitals()->GetStamina(), 0.f);

		Swimmer->TickGaspSound(0.1f);
		TestEqual(TEXT("바닥 = 소리 1"), Swimmer->GetGaspPlayCount(), 1);
		Swimmer->TickGaspSound(1.0f);
		TestEqual(TEXT("간격 안(1.0초) = 늘지 않음"), Swimmer->GetGaspPlayCount(), 1);
		Swimmer->TickGaspSound(1.0f);
		TestEqual(TEXT("간격 지남(2.0초) = 2"), Swimmer->GetGaspPlayCount(), 2);
		Fresh->TickGaspSound(0.1f);
		TestEqual(TEXT("스태미나 남음 = 0"), Fresh->GetGaspPlayCount(), 0);
		Dry->TickGaspSound(0.1f);
		TestEqual(TEXT("물 밖 = 0"), Dry->GetGaspPlayCount(), 0);
		Swimmer->GetPlayerVitals()->KillForCheck();
		Swimmer->TickGaspSound(5.f);
		TestEqual(TEXT("쓰러진 뒤 = 늘지 않음"), Swimmer->GetGaspPlayCount(), 2);
	}

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

// M58-03 불 옆 온기 재생 연결 — 불 옆 회복이 CueFor 를 거쳐 소리를 낸다: 회복 → 1 · 불 밖 → 0 · 가득 참 → 0 · 간격 안 → 0.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenWarmthPlayTest, "Snarehaven.World.WarmthPlay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenWarmthPlayTest::RunTest(const FString& Parameters)
{
	UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());

	// 테스트 전용 값 — 체력 1000, 회복·소모 0.
	FPlayerVitalsRow Row;
	Row.MaxHealth = 1000.f;
	Row.MaxStamina = 10.f;
	Row.RespawnHealth = 1000.f;
	Row.RespawnStamina = 10.f;
	Row.RunStaminaPerSecond = 0.f;
	Row.AttackStaminaCost = 0.f;
	Row.StaminaRegenPerSecond = 0.f;
	Row.StaminaRegenDelaySeconds = 0.f;
	Row.ExhaustionHealthPerSecond = 0.f;
	Row.HealthRegenPerSecond = 0.f;
	Row.HealthRegenStaminaThreshold = 0.f;
	Row.DeathLingerSeconds = 0.f;
	auto MakePlayer = [&]() -> ASnarehavenCharacter*
	{
		ASnarehavenCharacter* const Player = World->SpawnActor<ASnarehavenCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
		return (Player != nullptr && IsValid(Player->GetPlayerVitals()) && Player->GetPlayerVitals()->InitializeFromRow(Row))
			? Player : nullptr;
	};

	ASnarehavenCharacter* const Near = MakePlayer();
	ASnarehavenCharacter* const Away = MakePlayer();
	ASnarehavenCharacter* const Full = MakePlayer();
	if (Near == nullptr || Away == nullptr || Full == nullptr)
	{
		AddError(TEXT("플레이어를 못 세웠다"));
	}
	else
	{
		Near->PlayWarmthIfDue(1.f, false);
		TestEqual(TEXT("회복 = 소리 1"), Near->GetWarmthPlayCount(), 1);
		Near->TickWarmthClock(3.f);
		Near->PlayWarmthIfDue(1.f, false);
		TestEqual(TEXT("간격 안(3.0초) = 늘지 않음"), Near->GetWarmthPlayCount(), 1);
		Near->TickWarmthClock(1.f);
		Near->PlayWarmthIfDue(1.f, false);
		TestEqual(TEXT("간격 지남(4.0초) = 2"), Near->GetWarmthPlayCount(), 2);
		// 1초 틱 4번(프레임 흔들림으로 3.99초) → 소리 2번(0초·4초)
		ASnarehavenCharacter* const Tick = MakePlayer();
		Tick->PlayWarmthIfDue(1.f, false);
		for (int32 i = 0; i < 4; ++i)
		{
			Tick->TickWarmthClock(0.9975f);
			Tick->PlayWarmthIfDue(1.f, false);
		}
		TestEqual(TEXT("1초 틱 4번(3.99초) = 소리 2번(0초·4초)"), Tick->GetWarmthPlayCount(), 2);
		Away->PlayWarmthIfDue(0.f, false);
		TestEqual(TEXT("불 밖 = 0"), Away->GetWarmthPlayCount(), 0);
		Full->PlayWarmthIfDue(1.f, true);
		TestEqual(TEXT("체력 가득 = 0"), Full->GetWarmthPlayCount(), 0);
		Near->GetPlayerVitals()->KillForCheck();
		Near->TickWarmthClock(10.f);
		Near->PlayWarmthIfDue(1.f, false);
		TestEqual(TEXT("쓰러진 뒤 = 늘지 않음"), Near->GetWarmthPlayCount(), 2);
	}

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

// M59-03 부활 소리 재생 연결 — 쓰러졌다 일어설 때 CueFor 를 거쳐 한 번: 쓰러짐 뒤 → 1 · 평소 → 0.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenRevivePlayTest, "Snarehaven.World.RevivePlay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenRevivePlayTest::RunTest(const FString& Parameters)
{
	UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());

	ASnarehavenCharacter* const Player = World->SpawnActor<ASnarehavenCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
	if (Player == nullptr)
	{
		AddError(TEXT("플레이어를 못 세웠다"));
	}
	else
	{
		Player->PlayReviveSound(false);
		TestEqual(TEXT("평소(쓰러진 적 없음) = 0"), Player->GetRevivePlayCount(), 0);
		Player->PlayReviveSound(true);
		TestEqual(TEXT("쓰러졌다 일어섬 = 1"), Player->GetRevivePlayCount(), 1);
		Player->PlayReviveSound(true);
		TestEqual(TEXT("부활마다 한 번씩 = 2"), Player->GetRevivePlayCount(), 2);
	}

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);

	// 소리 자리 = 옮긴 뒤 자리 — 자리 옮기기는 서버 RPC 라 게스트 기기의 `FinishDownedSequence` 에선 아직 쓰러진 자리다.
	// 그래서 소리는 서버 함수 안, `TeleportTo` 뒤에서 낸다 (DESIGN 2.2 「부활한 자리」).
	FString Source;
	const FString SourcePath = FPaths::ProjectDir() / TEXT("Source/Snarehaven/Private/SnarehavenCharacter.cpp");
	if (!TestTrue(TEXT("캐릭터 소스를 읽는다"), FFileHelper::LoadFileToString(Source, *SourcePath)))
	{
		return false;
	}
	const int32 FinishAt = Source.Find(TEXT("void ASnarehavenCharacter::FinishDownedSequence()"));
	const int32 RespawnAt = Source.Find(TEXT("void ASnarehavenCharacter::Server_RespawnAtPlayerStart_Implementation()"));
	const int32 NextAt = Source.Find(TEXT("void ASnarehavenCharacter::HandleBackendSnapshotChanged()"));
	if (!TestTrue(TEXT("세 함수 자리를 찾았다"), FinishAt >= 0 && RespawnAt > FinishAt && NextAt > RespawnAt))
	{
		return false;
	}
	const FString FinishBody = Source.Mid(FinishAt, RespawnAt - FinishAt);
	const FString RespawnBody = Source.Mid(RespawnAt, NextAt - RespawnAt);
	TestFalse(TEXT("FinishDownedSequence 는 부활 소리를 내지 않는다"), FinishBody.Contains(TEXT("PlayReviveSound")));
	// 중계 길 — 서버가 Multicast 하나만 쏘고, 거름 없이 모든 기기가 한 번 낸다 (게스트 무음·호스트 겹침 방지).
	const int32 PlayFnAt = Source.Find(TEXT("void ASnarehavenCharacter::PlayReviveSound(bool bWasDowned)"));
	const int32 MultiAt = Source.Find(TEXT("void ASnarehavenCharacter::Multicast_PlayReviveSound_Implementation("));
	const int32 AfterMultiAt = Source.Find(TEXT("\n}\n"), ESearchCase::CaseSensitive, ESearchDir::FromStart, MultiAt >= 0 ? MultiAt : 0);
	if (TestTrue(TEXT("부활 소리 두 함수 자리를 찾았다"), PlayFnAt >= 0 && MultiAt > PlayFnAt && AfterMultiAt > MultiAt))
	{
		const FString PlayBody = Source.Mid(PlayFnAt, MultiAt - PlayFnAt);
		const FString MultiBody = Source.Mid(MultiAt, AfterMultiAt - MultiAt);
		TestFalse(TEXT("PlayReviveSound 본문에 PlayActionSound 없음"), PlayBody.Contains(TEXT("PlayActionSound(")));
		TestTrue(TEXT("PlayReviveSound 는 Multicast 를 쏜다"), PlayBody.Contains(TEXT("Multicast_PlayReviveSound(")));
		TestFalse(TEXT("Multicast 본문에 IsLocallyControlled 거름 없음"), MultiBody.Contains(TEXT("IsLocallyControlled")));
		TestTrue(TEXT("Multicast 본문에서 PlayAt 한 번"), MultiBody.Contains(TEXT("PlayAt(")));
	}
	const int32 TeleportAt = RespawnBody.Find(TEXT("TeleportTo("));
	const int32 LastPlayAt = RespawnBody.Find(TEXT("PlayReviveSound(true)"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
	TestTrue(TEXT("옮긴 뒤(TeleportTo 다음)에 소리를 낸다"), TeleportAt >= 0 && LastPlayAt > TeleportAt);
	TestTrue(TEXT("자리 못 찾아 그 자리 부활할 때도 소리를 낸다"),
		RespawnBody.Find(TEXT("PlayReviveSound(true)")) >= 0 && RespawnBody.Find(TEXT("PlayReviveSound(true)")) < TeleportAt);
	return true;
}

// M104-03b 날씨 소리 — 비 = 바람 ×1.5 + 빗소리 고리 · 눈 = 바람 ×1.2 · 새 끔 · 비 오는 날 풀·흙 발소리 ×0.8 (DESIGN 3.3 「날씨」).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenWeatherSoundTest, "Snarehaven.World.Sound.Weather",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenWeatherSoundTest::RunTest(const FString& Parameters)
{
	USnareSoundSubsystem* const Sounds = NewObject<USnareSoundSubsystem>();
	TestEqual(TEXT("빗소리 고리 1개"), Sounds->CountVariants(TEXT("Amb_Rain")), 1);

	SnareSound::FAmbienceMix Base;   // 시험 전용 값 — 표 숫자가 바뀌어도 안 깨지게
	Base.Wind = 0.2f;
	Base.BirdVolume = 0.4f;
	Base.Swamp = 0.3f;
	const SnareSound::FAmbienceMix Clear = SnareSound::WeatherAmbience(Base, 0.f, 0.f);
	TestEqual(TEXT("맑음 = 바람 그대로"), Clear.Wind, Base.Wind, 1e-5f);
	TestEqual(TEXT("맑음 = 새 그대로"), Clear.BirdVolume, Base.BirdVolume, 1e-5f);
	TestEqual(TEXT("맑음 = 빗소리 0"), Clear.Rain, 0.f, 1e-5f);
	const SnareSound::FAmbienceMix Rain = SnareSound::WeatherAmbience(Base, 1.f, 0.f);
	TestEqual(TEXT("비 = 바람 ×1.5"), Rain.Wind, Base.Wind * 1.5f, 1e-5f);
	TestEqual(TEXT("비 = 새 끔"), Rain.BirdVolume, 0.f, 1e-5f);
	TestTrue(TEXT("비 = 빗소리 고리 켬"), Rain.Rain > 0.f);
	TestEqual(TEXT("비 = 늪 고리 그대로"), Rain.Swamp, Base.Swamp, 1e-5f);
	const SnareSound::FAmbienceMix Snow = SnareSound::WeatherAmbience(Base, 0.f, 1.f);
	TestEqual(TEXT("눈 = 바람 ×1.2"), Snow.Wind, Base.Wind * 1.2f, 1e-5f);
	TestEqual(TEXT("눈 = 새 끔"), Snow.BirdVolume, 0.f, 1e-5f);
	TestEqual(TEXT("눈 = 빗소리 0"), Snow.Rain, 0.f, 1e-5f);
	const SnareSound::FAmbienceMix Half = SnareSound::WeatherAmbience(Base, 0.5f, 0.f);
	TestEqual(TEXT("섞는 중 비 0.5 = 바람 ×1.25"), Half.Wind, Base.Wind * 1.25f, 1e-5f);
	TestEqual(TEXT("섞는 중 비 0.5 = 빗소리 절반"), Half.Rain, Rain.Rain * 0.5f, 1e-5f);

	const FName Grass(TEXT("Footstep_Grass"));
	TestEqual(TEXT("맑음 풀 발소리 그대로"), SnareSound::WeatherFootstepScale(Grass, 0.f), 1.f, 1e-5f);
	TestEqual(TEXT("비 풀 발소리 ×0.8"), SnareSound::WeatherFootstepScale(Grass, 1.f), 0.8f, 1e-5f);
	TestEqual(TEXT("비 흙 발소리 ×0.8"), SnareSound::WeatherFootstepScale(FName(TEXT("Footstep_Mud")), 1.f), 0.8f, 1e-5f);
	TestEqual(TEXT("비 눈 발소리 그대로"), SnareSound::WeatherFootstepScale(FName(TEXT("Footstep_Snow")), 1.f), 1.f, 1e-5f);
	TestEqual(TEXT("비 나무 발소리 그대로"), SnareSound::WeatherFootstepScale(FName(TEXT("Footstep_Wood")), 1.f), 1.f, 1e-5f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
