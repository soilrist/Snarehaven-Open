// ROADMAP M39-01·08a 2층 검사 — 사냥 총(권총·소총) 행·탄약 행·잡화점 값이 DESIGN 8.6-c 표와 같은지, 총이 아닌 장비 행의 총 칸이 비었는지.
// 숫자는 시험에 적지 않고 설계서 표에서 읽는다 — 표 값을 바꿔도 행을 다시 만들면 그대로 통과한다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.Combat.Gun.Rows; Quit" -unattended -nullrhi
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Data/ItemRow.h"
#include "Data/ShopRow.h"
#include "Engine/DataTable.h"
#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace SnareGunRowTest
{
	const TCHAR* ItemTablePath = TEXT("/Game/Game/Data/Items/DT_ItemTable.DT_ItemTable");
	const TCHAR* ShopTablePath = TEXT("/Game/Game/Data/Shop/DT_ShopTable.DT_ShopTable");
	const FName PistolID(TEXT("item.pistol_semi"));
	const FName AmmoID(TEXT("item.ammo"));
	const FName RifleID(TEXT("item.rifle_modern"));
	const FName ShotgunID(TEXT("item.shotgun"));
	const FName CrossbowID(TEXT("item.crossbow"));

	/** 8.6-c 표에서 항목 칸이 `Label` 로 시작하는 줄의 값 칸에 정규식을 걸어 첫 숫자를 꺼낸다. 없으면 -1. */
	float ReadCell(const TArray<FString>& Lines, const FString& Label, const TCHAR* ValuePattern, int32 Group = 1)
	{
		for (const FString& Line : Lines)
		{
			TArray<FString> Cells;
			Line.ParseIntoArray(Cells, TEXT("|"), true);
			if (Cells.Num() >= 2 && Cells[0].TrimStartAndEnd().StartsWith(Label))
			{
				FRegexMatcher Matcher(FRegexPattern(ValuePattern), Cells[1]);
				return Matcher.FindNext() ? FCString::Atof(*Matcher.GetCaptureGroup(Group)) : -1.f;
			}
		}
		return -1.f;
	}

	const FItemRow* FindItem(const UDataTable* Table, FName ItemID)
	{
		for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
		{
			const FItemRow* Row = reinterpret_cast<const FItemRow*>(Pair.Value);
			if (Row->ItemID == ItemID)
			{
				return Row;
			}
		}
		return nullptr;
	}

	const FShopRow* FindShop(const UDataTable* Table, FName ItemID)
	{
		for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
		{
			const FShopRow* Row = reinterpret_cast<const FShopRow*>(Pair.Value);
			if (Row->ItemID == ItemID)
			{
				return Row;
			}
		}
		return nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenGunRowsTest, "Snarehaven.Combat.Gun.Rows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenGunRowsTest::RunTest(const FString& Parameters)
{
	using namespace SnareGunRowTest;

	FString Doc;
	const FString DocPath = FPaths::ProjectDir() / TEXT("docs/DESIGN.md");
	if (!TestTrue(TEXT("설계서를 읽는다"), FFileHelper::LoadFileToString(Doc, *DocPath)))
	{
		return false;
	}
	const int32 Start = Doc.Find(TEXT("### 8.6-c"), ESearchCase::CaseSensitive);
	if (!TestTrue(TEXT("DESIGN 8.6-c 절이 있다"), Start != INDEX_NONE))
	{
		return false;
	}
	const int32 End = Doc.Find(TEXT("\n#"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Start + 4);
	TArray<FString> Lines;
	Doc.Mid(Start, End == INDEX_NONE ? MAX_int32 : End - Start).ParseIntoArrayLines(Lines);

	const float Damage = ReadCell(Lines, TEXT("단발 권총 `item.pistol_semi` 1발 피해"), TEXT("([0-9.]+)"));
	const float Range = ReadCell(Lines, TEXT("권총 사거리"), TEXT("([0-9.]+)cm"));
	const float FalloffStart = ReadCell(Lines, TEXT("권총 `s` / `m`"), TEXT("([0-9.]+) / ([0-9.]+)"), 1);
	const float MinRatio = ReadCell(Lines, TEXT("권총 `s` / `m`"), TEXT("([0-9.]+) / ([0-9.]+)"), 2);
	const float Interval = ReadCell(Lines, TEXT("권총 쏘기 간격"), TEXT("([0-9.]+)초"));
	const float Magazine = ReadCell(Lines, TEXT("권총 탄창"), TEXT("([0-9]+)발"));
	const float Reload = ReadCell(Lines, TEXT("권총 재장전"), TEXT("([0-9.]+)초"));
	const float Speed = ReadCell(Lines, TEXT("권총 총알 속도"), TEXT("([0-9.]+)cm/s"));
	const float PistolPrice = ReadCell(Lines, TEXT("권총 잡화점 값"), TEXT("([0-9]+) 코인"));
	const float AmmoCount = ReadCell(Lines, TEXT("권총탄 `item.ammo` 묶음"), TEXT("([0-9]+)발 = ([0-9]+) 코인"), 1);
	const float AmmoPrice = ReadCell(Lines, TEXT("권총탄 `item.ammo` 묶음"), TEXT("([0-9]+)발 = ([0-9]+) 코인"), 2);
	// 표를 못 읽으면 -1 — 아래 비교가 행 0 과 엇갈려 조용히 통과하지 않게 먼저 막는다.
	for (const float Read : { Damage, Range, FalloffStart, MinRatio, Interval, Magazine, Reload, Speed, PistolPrice, AmmoCount, AmmoPrice })
	{
		if (!TestTrue(TEXT("8.6-c 표 값을 읽었다(0 보다 크다)"), Read > 0.f))
		{
			return false;
		}
	}

	const UDataTable* const ItemTable = LoadObject<UDataTable>(nullptr, ItemTablePath);
	const UDataTable* const ShopTable = LoadObject<UDataTable>(nullptr, ShopTablePath);
	if (!TestNotNull(TEXT("DT_ItemTable"), ItemTable) || !TestNotNull(TEXT("DT_ShopTable"), ShopTable))
	{
		return false;
	}

	const FItemRow* const Pistol = FindItem(ItemTable, PistolID);
	if (TestNotNull(TEXT("권총 행이 있다"), Pistol))
	{
		TestTrue(TEXT("권총 = 무기 칸"), Pistol->EquipSlot == EEquipSlot::Weapon);
		TestEqual(TEXT("1발 피해"), Pistol->AttackDamage, Damage);
		TestEqual(TEXT("사거리 R"), Pistol->AttackRangeCm, Range);
		TestEqual(TEXT("감쇠 s"), Pistol->FalloffStartRatio, FalloffStart);
		TestEqual(TEXT("감쇠 m"), Pistol->MinDamageRatio, MinRatio);
		TestEqual(TEXT("쏘기 간격"), Pistol->FireIntervalSeconds, Interval);
		TestEqual(TEXT("탄창"), static_cast<float>(Pistol->MagazineSize), Magazine);
		TestEqual(TEXT("재장전"), Pistol->ReloadSeconds, Reload);
		TestEqual(TEXT("총알 속도"), Pistol->ProjectileSpeedCmPerSecond, Speed);
		TestEqual(TEXT("탄약 칸 = item.ammo"), Pistol->AmmoItemID, AmmoID);
	}
	TestNotNull(TEXT("탄약 행이 있다"), FindItem(ItemTable, AmmoID));

	// M39-08a 현대 소총 — 8.6-c 한 줄 「피해 40 · `R` 2500 · 간격 1.2초 · 탄창 5 · 재장전 2.0초 · 속도 6000」, 감쇠는 원거리 기본값(권총 줄).
	const TCHAR* const RifleCell = TEXT("피해 ([0-9.]+) · `R` ([0-9.]+) · 간격 ([0-9.]+)초 · 탄창 ([0-9]+) · 재장전 ([0-9.]+)초 · 속도 ([0-9.]+)");
	const TCHAR* const RifleLabel = TEXT("신식 소총 `item.rifle_modern`");
	float RifleWant[6];
	for (int32 Group = 1; Group <= 6; ++Group)
	{
		RifleWant[Group - 1] = ReadCell(Lines, RifleLabel, RifleCell, Group);
		if (!TestTrue(TEXT("8.6-c 소총 줄 값을 읽었다(0 보다 크다)"), RifleWant[Group - 1] > 0.f))
		{
			return false;
		}
	}
	const FItemRow* const Rifle = FindItem(ItemTable, RifleID);
	if (TestNotNull(TEXT("소총 행이 있다"), Rifle))
	{
		TestTrue(TEXT("소총 = 무기 칸"), Rifle->EquipSlot == EEquipSlot::Weapon);
		TestEqual(TEXT("소총 1발 피해"), Rifle->AttackDamage, RifleWant[0]);
		TestEqual(TEXT("소총 사거리 R"), Rifle->AttackRangeCm, RifleWant[1]);
		TestEqual(TEXT("소총 쏘기 간격"), Rifle->FireIntervalSeconds, RifleWant[2]);
		TestEqual(TEXT("소총 탄창"), static_cast<float>(Rifle->MagazineSize), RifleWant[3]);
		TestEqual(TEXT("소총 재장전"), Rifle->ReloadSeconds, RifleWant[4]);
		TestEqual(TEXT("소총 총알 속도"), Rifle->ProjectileSpeedCmPerSecond, RifleWant[5]);
		TestEqual(TEXT("소총 감쇠 s = 권총(원거리 기본값)"), Rifle->FalloffStartRatio, FalloffStart);
		TestEqual(TEXT("소총 감쇠 m = 권총(원거리 기본값)"), Rifle->MinDamageRatio, MinRatio);
		TestEqual(TEXT("소총 탄약 칸 = item.ammo_rifle"), Rifle->AmmoItemID, FName(TEXT("item.ammo_rifle")));
	}
	TestNull(TEXT("소총은 상점에 안 판다(8.10 제작)"), FindShop(ShopTable, RifleID));

	// 산탄총 — 같은 칸 모양(피해·R·간격·탄창·재장전·속도), 탄약은 산탄, 잡화점에서 판다.
	float ShotgunWant[6];
	for (int32 Group = 1; Group <= 6; ++Group)
	{
		ShotgunWant[Group - 1] = ReadCell(Lines, TEXT("산탄총 `item.shotgun`"), RifleCell, Group);
		if (!TestTrue(TEXT("8.6-c 산탄총 줄 값을 읽었다(0 보다 크다)"), ShotgunWant[Group - 1] > 0.f))
		{
			return false;
		}
	}
	const FItemRow* const Shotgun = FindItem(ItemTable, ShotgunID);
	if (TestNotNull(TEXT("산탄총 행이 있다"), Shotgun))
	{
		TestTrue(TEXT("산탄총 = 무기 칸"), Shotgun->EquipSlot == EEquipSlot::Weapon);
		TestEqual(TEXT("산탄총 1발 피해"), Shotgun->AttackDamage, ShotgunWant[0]);
		TestEqual(TEXT("산탄총 사거리 R"), Shotgun->AttackRangeCm, ShotgunWant[1]);
		TestEqual(TEXT("산탄총 쏘기 간격"), Shotgun->FireIntervalSeconds, ShotgunWant[2]);
		TestEqual(TEXT("산탄총 탄창"), static_cast<float>(Shotgun->MagazineSize), ShotgunWant[3]);
		TestEqual(TEXT("산탄총 재장전"), Shotgun->ReloadSeconds, ShotgunWant[4]);
		TestEqual(TEXT("산탄총 총알 속도"), Shotgun->ProjectileSpeedCmPerSecond, ShotgunWant[5]);
		TestEqual(TEXT("산탄총 탄약 칸 = item.ammo_shotgun"), Shotgun->AmmoItemID, FName(TEXT("item.ammo_shotgun")));
	}
	const FShopRow* const ShotgunShop = FindShop(ShopTable, ShotgunID);
	if (TestNotNull(TEXT("산탄총이 잡화점에 있다"), ShotgunShop))
	{
		TestEqual(TEXT("산탄총 값"), static_cast<float>(ShotgunShop->Price), ReadCell(Lines, TEXT("산탄총 잡화점 값"), TEXT("([0-9]+) 코인")));
	}
	const FShopRow* const ShotAmmoShop = FindShop(ShopTable, FName(TEXT("item.ammo_shotgun")));
	if (TestNotNull(TEXT("산탄이 잡화점에 있다"), ShotAmmoShop))
	{
		TestEqual(TEXT("산탄 묶음 값"), static_cast<float>(ShotAmmoShop->Price), ReadCell(Lines, TEXT("탄 `item.ammo_shotgun` 묶음"), TEXT("([0-9]+)발 = ([0-9]+) 코인"), 2));
		TestEqual(TEXT("산탄 묶음 발 수"), static_cast<float>(ShotAmmoShop->Count), ReadCell(Lines, TEXT("탄 `item.ammo_shotgun` 묶음"), TEXT("([0-9]+)발 = ([0-9]+) 코인"), 1));
	}

	// 석궁 — 같은 칸 모양(피해·R·간격·탄창·재장전·속도), 탄약은 화살, 잡화점에서 판다.
	float CrossbowWant[6];
	for (int32 Group = 1; Group <= 6; ++Group)
	{
		CrossbowWant[Group - 1] = ReadCell(Lines, TEXT("석궁 `item.crossbow`"), RifleCell, Group);
		if (!TestTrue(TEXT("8.6-c 석궁 줄 값을 읽었다(0 보다 크다)"), CrossbowWant[Group - 1] > 0.f))
		{
			return false;
		}
	}
	const FItemRow* const Crossbow = FindItem(ItemTable, CrossbowID);
	if (TestNotNull(TEXT("석궁 행이 있다"), Crossbow))
	{
		TestTrue(TEXT("석궁 = 무기 칸"), Crossbow->EquipSlot == EEquipSlot::Weapon);
		TestEqual(TEXT("석궁 1발 피해"), Crossbow->AttackDamage, CrossbowWant[0]);
		TestEqual(TEXT("석궁 사거리 R"), Crossbow->AttackRangeCm, CrossbowWant[1]);
		TestEqual(TEXT("석궁 쏘기 간격"), Crossbow->FireIntervalSeconds, CrossbowWant[2]);
		TestEqual(TEXT("석궁 탄창"), static_cast<float>(Crossbow->MagazineSize), CrossbowWant[3]);
		TestEqual(TEXT("석궁 재장전"), Crossbow->ReloadSeconds, CrossbowWant[4]);
		TestEqual(TEXT("석궁 총알 속도"), Crossbow->ProjectileSpeedCmPerSecond, CrossbowWant[5]);
		TestEqual(TEXT("석궁 탄약 칸 = item.arrow"), Crossbow->AmmoItemID, FName(TEXT("item.arrow")));
	}
	const FShopRow* const CrossbowShop = FindShop(ShopTable, CrossbowID);
	if (TestNotNull(TEXT("석궁이 잡화점에 있다"), CrossbowShop))
	{
		TestEqual(TEXT("석궁 값"), static_cast<float>(CrossbowShop->Price), ReadCell(Lines, TEXT("석궁 잡화점 값"), TEXT("([0-9]+) 코인")));
	}
	const FShopRow* const ArrowShop = FindShop(ShopTable, FName(TEXT("item.arrow")));
	if (TestNotNull(TEXT("화살이 잡화점에 있다"), ArrowShop))
	{
		TestEqual(TEXT("화살 묶음 값"), static_cast<float>(ArrowShop->Price), ReadCell(Lines, TEXT("탄 `item.arrow` 묶음"), TEXT("([0-9]+)발 = ([0-9]+) 코인"), 2));
		TestEqual(TEXT("화살 묶음 발 수"), static_cast<float>(ArrowShop->Count), ReadCell(Lines, TEXT("탄 `item.arrow` 묶음"), TEXT("([0-9]+)발 = ([0-9]+) 코인"), 1));
	}

	// 연발 권총·기관단총 — 같은 칸 모양, 탄약은 권총탄 item.ammo, 잡화점에서 판다.
	struct FExtraGun { const TCHAR* Id; const TCHAR* Label; const TCHAR* ShopLabel; const TCHAR* Ammo; };
	const FExtraGun ExtraGuns[] = {
		{ TEXT("item.pistol_auto"), TEXT("연발 권총 `item.pistol_auto`"), TEXT("연발 권총 잡화점 값"), TEXT("item.ammo") },
		{ TEXT("item.smg"), TEXT("기관단총 `item.smg`"), TEXT("기관단총 잡화점 값"), TEXT("item.ammo") },
		{ TEXT("item.rifle_burst"), TEXT("점사 소총 `item.rifle_burst`"), TEXT("점사 소총 잡화점 값"), TEXT("item.ammo_rifle") },
		{ TEXT("item.rifle_semi"), TEXT("반자동 소총 `item.rifle_semi`"), TEXT("반자동 소총 잡화점 값"), TEXT("item.ammo_rifle") },
		{ TEXT("item.shotgun_auto"), TEXT("연사 산탄총 `item.shotgun_auto`"), TEXT("연사 산탄총 잡화점 값"), TEXT("item.ammo_shotgun") },
		{ TEXT("item.shotgun_short"), TEXT("짧은 산탄총 `item.shotgun_short`"), TEXT("짧은 산탄총 잡화점 값"), TEXT("item.ammo_shotgun") },
		{ TEXT("item.mg"), TEXT("기관총 `item.mg`"), TEXT("기관총 잡화점 값"), TEXT("item.ammo_rifle") } };
	for (const FExtraGun& Extra : ExtraGuns)
	{
		float Want[6];
		for (int32 Group = 1; Group <= 6; ++Group)
		{
			Want[Group - 1] = ReadCell(Lines, Extra.Label, RifleCell, Group);
			if (!TestTrue(FString::Printf(TEXT("8.6-c %s 줄 값을 읽었다(0 보다 크다)"), Extra.Label), Want[Group - 1] > 0.f))
			{
				return false;
			}
		}
		const FItemRow* const Gun = FindItem(ItemTable, FName(Extra.Id));
		if (TestNotNull(FString::Printf(TEXT("%s 행이 있다"), Extra.Id), Gun))
		{
			TestTrue(TEXT("무기 칸"), Gun->EquipSlot == EEquipSlot::Weapon);
			TestEqual(TEXT("1발 피해"), Gun->AttackDamage, Want[0]);
			TestEqual(TEXT("사거리 R"), Gun->AttackRangeCm, Want[1]);
			TestEqual(TEXT("쏘기 간격"), Gun->FireIntervalSeconds, Want[2]);
			TestEqual(TEXT("탄창"), static_cast<float>(Gun->MagazineSize), Want[3]);
			TestEqual(TEXT("재장전"), Gun->ReloadSeconds, Want[4]);
			TestEqual(TEXT("총알 속도"), Gun->ProjectileSpeedCmPerSecond, Want[5]);
			TestEqual(TEXT("탄약 칸 = 총에 맞는 탄"), Gun->AmmoItemID, FName(Extra.Ammo));
		}
		const FShopRow* const GunShop = FindShop(ShopTable, FName(Extra.Id));
		if (TestNotNull(FString::Printf(TEXT("%s 잡화점에 있다"), Extra.Id), GunShop))
		{
			TestEqual(TEXT("잡화점 값"), static_cast<float>(GunShop->Price), ReadCell(Lines, Extra.ShopLabel, TEXT("([0-9]+) 코인")));
		}
	}

	const FShopRow* const PistolShop = FindShop(ShopTable, PistolID);
	if (TestNotNull(TEXT("권총이 잡화점에 있다"), PistolShop))
	{
		TestEqual(TEXT("권총 값"), static_cast<float>(PistolShop->Price), PistolPrice);
		TestEqual(TEXT("권총 1개"), PistolShop->Count, 1);
	}
	const FShopRow* const AmmoShop = FindShop(ShopTable, AmmoID);
	if (TestNotNull(TEXT("탄약이 잡화점에 있다"), AmmoShop))
	{
		TestEqual(TEXT("탄약 묶음 값"), static_cast<float>(AmmoShop->Price), AmmoPrice);
		TestEqual(TEXT("탄약 묶음 발 수"), static_cast<float>(AmmoShop->Count), AmmoCount);
	}

	// 총이 아닌 행은 총 칸이 전부 비어야 한다 — 근접 무기·도구가 탄창 0 이 아니면 쏘기 길로 샌다.
	TArray<FString> Leaks;
	for (const TPair<FName, uint8*>& Pair : ItemTable->GetRowMap())
	{
		const FItemRow* Row = reinterpret_cast<const FItemRow*>(Pair.Value);
		if (Row->ItemID == PistolID || Row->ItemID == RifleID || Row->ItemID == ShotgunID || Row->ItemID == CrossbowID
			|| Row->ItemID == FName(TEXT("item.pistol_auto")) || Row->ItemID == FName(TEXT("item.smg"))
			|| Row->ItemID == FName(TEXT("item.rifle_burst")) || Row->ItemID == FName(TEXT("item.rifle_semi"))
			|| Row->ItemID == FName(TEXT("item.shotgun_auto")) || Row->ItemID == FName(TEXT("item.shotgun_short")) || Row->ItemID == FName(TEXT("item.mg")) || Row->ItemID == FName(TEXT("item.bazooka")))
		{
			continue;
		}
		if (Row->MagazineSize != 0 || Row->AttackDamage != 0.f || Row->ReloadSeconds != 0.f
			|| Row->FireIntervalSeconds != 0.f || Row->ProjectileSpeedCmPerSecond != 0.f || !Row->AmmoItemID.IsNone())
		{
			Leaks.Add(Row->ItemID.ToString());
		}
	}
	TestEqual(FString::Printf(TEXT("총 칸이 찬 다른 행 0 (%s)"), *FString::Join(Leaks, TEXT(", "))), Leaks.Num(), 0);
	return true;
}


// 쏘기 진동 종류는 총 행 데이터가 정한다 (INBOX 보정 — 아이템 이름 문자열로 가르지 않는다).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenGunShotHapticKindTest, "Snarehaven.Combat.Gun.ShotHapticKind",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenGunShotHapticKindTest::RunTest(const FString& Parameters)
{
	using namespace SnareGunRowTest;

	const UDataTable* const ItemTable = LoadObject<UDataTable>(nullptr, ItemTablePath);
	if (!TestNotNull(TEXT("아이템 표를 읽는다"), ItemTable))
	{
		return false;
	}
	// 판정 함수 — 총 행이 None 이면 잡는다. 역검증은 같은 함수에 가짜 행을 먹여 한다.
	const auto IsBad = [](const FItemRow& Row) { return Row.MagazineSize > 0 && Row.ShotHapticKind == EShotHapticKind::None; };

	FItemRow Fake;
	Fake.MagazineSize = 5;
	TestTrue(TEXT("역검증: 총인데 None 인 가짜 행을 잡는다"), IsBad(Fake));
	Fake.ShotHapticKind = EShotHapticKind::Rifle;
	TestFalse(TEXT("역검증: 종류를 적으면 통과"), IsBad(Fake));

	TArray<FString> Bad;
	int32 Guns = 0;
	for (const TPair<FName, uint8*>& Pair : ItemTable->GetRowMap())
	{
		const FItemRow* Row = reinterpret_cast<const FItemRow*>(Pair.Value);
		Guns += Row->MagazineSize > 0 ? 1 : 0;
		if (IsBad(*Row) || (Row->MagazineSize <= 0 && Row->ShotHapticKind != EShotHapticKind::None))
		{
			Bad.Add(Row->ItemID.ToString());
		}
	}
	TestTrue(TEXT("총 행이 있다"), Guns >= 2);
	TestEqual(FString::Printf(TEXT("총 행은 종류가 있고 다른 행은 None (%s)"), *FString::Join(Bad, TEXT(", "))), Bad.Num(), 0);
	const FItemRow* const Pistol = FindItem(ItemTable, PistolID);
	const FItemRow* const Rifle = FindItem(ItemTable, RifleID);
	if (TestNotNull(TEXT("권총 행"), Pistol) && TestNotNull(TEXT("소총 행"), Rifle))
	{
		TestTrue(TEXT("권총 = Pistol"), Pistol->ShotHapticKind == EShotHapticKind::Pistol);
		TestTrue(TEXT("소총 = Rifle"), Rifle->ShotHapticKind == EShotHapticKind::Rifle);
	}

	// 쏘기 코드에 소총 이름 문자열이 없다.
	FString Code;
	const FString CodePath = FPaths::ProjectDir() / TEXT("Source/Snarehaven/Private/Combat/MeleeAttackComponent.cpp");
	if (TestTrue(TEXT("쏘기 코드를 읽는다"), FFileHelper::LoadFileToString(Code, *CodePath)))
	{
		TestFalse(TEXT("쏘기 코드에 item.rifle_modern 없음"), Code.Contains(TEXT("item.rifle_modern")));
	}
	AddInfo(FString::Printf(TEXT("[ShotHapticKind] 총 행 %d · 이상 %d"), Guns, Bad.Num()));
	return true;
}

#endif
