// ROADMAP M5-05a·M5-05b 2층 검사 — 화면들(로비·사냥 HUD·목장 HUD·일시정지 메뉴·뽑기 화면)이 글꼴·색
// 한 벌을 쓰는지. **화면이 늘면 여기 한 줄을 보탠다** — 새 화면만 엔진 기본 모습으로 남는 것이 ② 다.
// 이 검사가 잡는 것: ① 글꼴 에셋(`Font_UI_Jua`)이 없거나 이름이 바뀐 것 ② 한 화면만 `RebuildWidget` 에서
// 적용을 빠뜨려 그 화면만 엔진 기본 모습(Roboto — 한글 글리프가 없어 네모로 나온다 / 회색 버튼)으로 남는 것
// ③ 목장 HUD 처럼 화면이 뜬 **뒤에** 줄이 늘어나는 곳에서 새 줄만 다른 것
// ④ UI 글꼴(Jua + 대체 칸)이 못 그리는 글자를 화면 글자·표 글자에 써서 네모로 나오는 것 — M13-27 부터 Jua 에
//    없는 문장부호(`·`·`—`·`…`)는 대체 칸(엔진 Roboto Bold)이 그리고, 판정은 Slate 글꼴 캐시에 직접 묻는다
// ⑤ 버튼 색·버튼 위 글자색이 DESIGN 2.1 팔레트에서 벗어난 것(표 밖의 색을 새로 만드는 것).
// 한글이 실제로 네모가 아닌 모양으로 그려지는 것·색이 눈에 어떤지는 3층(캡처)이 본다.
// 실행: UnrealEditor-Cmd <uproject 절대경로> -ExecCmds="Automation RunTests Snarehaven.Ui; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "UI/WorldMapScreenWidget.h"
#include "UI/SnareNextGoalWidget.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Fonts/FontCache.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Framework/Application/SlateApplication.h"
#include "Player/SnareEmoteBubble.h"
#include "Rendering/SlateRenderer.h"
#include "SnarehavenCharacter.h"
#include "SnarehavenGameMode.h"
#include "UI/GachaScreenWidget.h"
#include "UI/HuntHudWidget.h"
#include "UI/LobbyWidget.h"
#include "UI/PauseMenuWidget.h"
#include "UI/PlayerNameTagWidget.h"
#include "UI/RanchHudWidget.h"
#include "UI/SnareMinimapHud.h"
#include "UI/SnareUiStyle.h"
#include "UObject/UnrealType.h"

namespace
{
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

	// 화면들을 실제로 세워(Slate 까지) 화면마다 `Check(위젯, 화면 이름)` 을 부른다.
	// 글꼴 검사와 색 검사가 같은 무대를 쓴다 — 무대를 두 벌 적으면 한쪽만 화면을 빠뜨리는 사고가 난다.
	void ForEachBuiltScreen(FAutomationTestBase& Test, TFunctionRef<void(const UUserWidget&, const TCHAR*)> Check)
	{
		UWorld* World = CreateTestWorld();
		ASnarehavenCharacter* Player = World->SpawnActor<ASnarehavenCharacter>();

		ULobbyWidget* Lobby = CreateWidget<ULobbyWidget>(World, ULobbyWidget::StaticClass());
		UPauseMenuWidget* Menu = CreateWidget<UPauseMenuWidget>(World, UPauseMenuWidget::StaticClass());
		UHuntHudWidget* HuntHud = CreateWidget<UHuntHudWidget>(World, UHuntHudWidget::StaticClass());
		URanchHudWidget* RanchHud = CreateWidget<URanchHudWidget>(World, URanchHudWidget::StaticClass());
		UGachaScreenWidget* Gacha = CreateWidget<UGachaScreenWidget>(World, UGachaScreenWidget::StaticClass());
		if (!Test.TestNotNull(TEXT("로비"), Lobby) || !Test.TestNotNull(TEXT("일시정지 메뉴"), Menu)
			|| !Test.TestNotNull(TEXT("사냥 HUD"), HuntHud) || !Test.TestNotNull(TEXT("목장 HUD"), RanchHud)
			|| !Test.TestNotNull(TEXT("뽑기 화면"), Gacha))
		{
			DestroyTestWorld(World);
			return;
		}

		HuntHud->BindToCharacter(Player);
		// 목장 칸 줄은 목장에 붙은 뒤에야 생긴다 — 화면이 뜬 뒤 늘어나는 글자까지 보려고 붙인다.
		RanchHud->BindToCharacter(Player);

		// Slate 위젯을 실제로 만들어야 `RebuildWidget` 이 돌고, 그때 자리표시 트리와 글꼴·색이 들어간다.
		// 반환값을 붙잡고 있어야 한다 — 놓으면 그 자리에서 해제되며 연결이 끊긴다(`SnarehavenRanchHudTest`).
		const TSharedRef<SWidget> LobbySlate = Lobby->TakeWidget();
		const TSharedRef<SWidget> MenuSlate = Menu->TakeWidget();
		const TSharedRef<SWidget> HuntSlate = HuntHud->TakeWidget();
		const TSharedRef<SWidget> RanchSlate = RanchHud->TakeWidget();
		const TSharedRef<SWidget> GachaSlate = Gacha->TakeWidget();

		Check(*Lobby, TEXT("로비"));
		Check(*Menu, TEXT("일시정지 메뉴"));
		Check(*HuntHud, TEXT("사냥 HUD"));
		Check(*RanchHud, TEXT("목장 HUD"));
		Check(*Gacha, TEXT("뽑기 화면"));

		DestroyTestWorld(World);
	}

	// M13-27: 화면 글자에 쓰였는데 Jua 에 없는 글자. 소스·표 문자열을 Jua 의 cmap 과 맞대어 찾은 것
	// (STATUS #0213) — `·` 미니맵 좌표(`ScreenShot00013` 에서 네모) · `—` 물 행동 안내 · `…` 옛 문구(루프#87).
	// 복합 글꼴의 대체 칸(엔진 Roboto Bold)이 이것들을 그린다(`tools/unreal/setup_ui_font_fallback.py`).
	const TCHAR JuaMissingGlyphs[] = { 0x00B7, 0x2014, 0x2026 };

	FSlateFontCache* GetSlateFontCache()
	{
		FSlateRenderer* const Renderer = FSlateApplication::IsInitialized() ? FSlateApplication::Get().GetRenderer() : nullptr;
		return Renderer != nullptr ? &Renderer->GetFontCache().Get() : nullptr;
	}

	// UI 글꼴이 이 글자를 **실제로** 그리는지 — 화면과 같은 길(기본 → 대체 타입페이스)로 얼굴을 고른 뒤
	// 그 얼굴에 글리프가 있는지 FreeType 에 묻는다. 둘 다 없으면 Slate 는 Jua 를 억지로 써 **네모**를 그린다.
	bool CanUiFontDraw(FSlateFontCache& Cache, const UFont* UiFont, const TCHAR Ch)
	{
		if (Ch < 0x20)
		{
			return true;   // 줄바꿈·탭은 글리프로 그리지 않는다
		}
		const FSlateFontInfo Info(UiFont, 24, TEXT("Default"));
		float Scale = 1.0f;
		const FFontData& Data = Cache.GetFontDataForCodepoint(Info, Ch, Scale);
		return Cache.CanLoadCodepoint(Data, Ch);
	}

	// 글자 하나라도 못 그리면 첫 글자를 오류로 적는다.
	void CheckDrawable(FAutomationTestBase& Test, FSlateFontCache& Cache, const UFont* UiFont, const FString& Where, const FString& Shown)
	{
		for (const TCHAR Ch : Shown)
		{
			if (!CanUiFontDraw(Cache, UiFont, Ch))
			{
				Test.AddError(FString::Printf(TEXT("%s 에 UI 글꼴이 못 그리는 글자 '%c'(U+%04X): \"%s\""),
					*Where, Ch, static_cast<uint32>(Ch), *Shown));
				return;
			}
		}
	}

	// 트리 안 글자를 세고, 하나라도 다른 글꼴이면 그 이름을 돌려준다.
	void CheckEveryTextUsesUiFont(FAutomationTestBase& Test, const UUserWidget& Widget, const TCHAR* ScreenName)
	{
		const UFont* const UiFont = SnareUiStyle::GetUiFont();
		FSlateFontCache* const Cache = GetSlateFontCache();
		if (!Test.TestNotNull(TEXT("Slate 글꼴 캐시"), Cache))
		{
			return;
		}
		int32 TextCount = 0;

		Widget.WidgetTree->ForEachWidget([&Test, &TextCount, UiFont, Cache, ScreenName](UWidget* Child)
		{
			const UTextBlock* const Text = Cast<UTextBlock>(Child);
			if (!IsValid(Text))
			{
				return;
			}

			++TextCount;
			Test.TestEqual(FString::Printf(TEXT("%s - %s 의 글꼴"), ScreenName, *Text->GetName()),
				Cast<const UFont>(Text->GetFont().FontObject.Get()), UiFont);

			CheckDrawable(Test, *Cache, UiFont, FString::Printf(TEXT("%s - %s"), ScreenName, *Text->GetName()),
				Text->GetText().ToString());
		});

		// 글자가 하나도 없으면 위 비교가 한 번도 안 돌아 **조용히 통과한다** — 그 구멍을 여기서 막는다.
		Test.TestTrue(FString::Printf(TEXT("%s 에 글자가 있다"), ScreenName), TextCount > 0);
	}

	void CheckColor(FAutomationTestBase& Test, const FString& What, const FLinearColor& Actual, const FLinearColor& Expected)
	{
		// 헥스 → 선형 변환이 부동소수라 정확히 같지는 않다. `Equals` 의 기본 허용오차보다 촘촘히 본다.
		Test.TestTrue(FString::Printf(TEXT("%s = %s (실제 %s)"), *What, *Expected.ToString(), *Actual.ToString()),
			Actual.Equals(Expected, 0.001f));
	}

	// 버튼 네 상태 색과 버튼 위 글자색이 팔레트 그대로인지.
	void CheckEveryButtonUsesPalette(FAutomationTestBase& Test, const UUserWidget& Widget, const TCHAR* ScreenName)
	{
		int32 ButtonCount = 0;

		Widget.WidgetTree->ForEachWidget([&Test, &ButtonCount, ScreenName](UWidget* Child)
		{
			const UButton* const Button = Cast<UButton>(Child);
			if (!IsValid(Button))
			{
				return;
			}

			++ButtonCount;
			const FString Where = FString::Printf(TEXT("%s - %s"), ScreenName, *Button->GetName());
			const FButtonStyle& Style = Button->GetStyle();
			// DESIGN 7.1 나무 단추 — 고른 탭만 `UiButtonHighlight`(노랑) 바탕이고 그때 글은 진갈색, 나머지는 나무 바탕 + 크림 글.
			const FLinearColor Normal = Style.Normal.TintColor.GetSpecifiedColor();
			const bool bSelectedTab = Normal.Equals(SnareUiStyle::UiButtonHighlight(), 0.001f);
			CheckColor(Test, Where + TEXT(" 기본 바탕"), Normal, bSelectedTab ? SnareUiStyle::UiButtonHighlight() : SnareUiStyle::UiWoodButton());
			CheckColor(Test, Where + TEXT(" 호버"), Style.Hovered.TintColor.GetSpecifiedColor(), SnareUiStyle::UiWoodButton());
			CheckColor(Test, Where + TEXT(" 눌림"), Style.Pressed.TintColor.GetSpecifiedColor(), SnareUiStyle::UiWoodButtonPressed());
			CheckColor(Test, Where + TEXT(" 비활성"), Style.Disabled.TintColor.GetSpecifiedColor(), SnareUiStyle::UiDisabledGray());

			// 바탕과 글이 같은 계열이면 안 보인다 — 나무 위는 크림, 노랑 위는 진갈색.
			for (int32 Index = 0; Index < Button->GetChildrenCount(); ++Index)
			{
				if (const UTextBlock* const Label = Cast<UTextBlock>(Button->GetChildAt(Index)))
				{
					CheckColor(Test, Where + TEXT(" 위 글자"),
						Label->GetColorAndOpacity().GetSpecifiedColor(), bSelectedTab ? SnareUiStyle::UiTextOnButton() : SnareUiStyle::UiPanelCream());
				}
			}
		});

		// 버튼이 하나도 없으면 위 비교가 한 번도 안 돌아 조용히 통과한다 — 네 화면 모두 버튼이 있다.
		Test.TestTrue(FString::Printf(TEXT("%s 에 버튼이 있다"), ScreenName), ButtonCount > 0);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenUiFontAssetTest, "Snarehaven.Ui.Font.AssetExists",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenUiFontAssetTest::RunTest(const FString& Parameters)
{
	const UFont* const UiFont = SnareUiStyle::GetUiFont();
	if (!TestNotNull(TEXT("UI 글꼴 에셋(Font_UI_Jua) 로드"), UiFont))
	{
		return false;
	}

	// 화면 글자에 물릴 수 있는 것은 composite font 를 내주는 `UFont` 뿐이다 — 얼굴(`UFontFace`)만
	// 남아 있으면 Slate 가 조용히 대체 글꼴로 되돌아간다.
	TestEqual(TEXT("런타임 캐시 글꼴이다"), UiFont->FontCacheType, EFontCacheType::Runtime);
	const TArray<FTypefaceEntry>& Faces = UiFont->GetInternalCompositeFont().DefaultTypeface.Fonts;
	if (!TestTrue(TEXT("기본 타입페이스에 얼굴이 물려 있다"), Faces.Num() > 0))
	{
		return false;
	}

	// 얼굴 데이터가 에셋 안에 들어 있어야 한다. LazyLoad 로 남으면 `_Source/raw/...` 원본 TTF 경로를
	// 들고 있다가 포장한 빌드에서 그 파일을 못 찾아 글자가 통째로 대체 글꼴로 돌아간다 — 에디터에서만
	// 맞는 글꼴이 된다(루프#87). `tools/unreal/import_ui_font.py` 가 Inline 으로 저장한다.
	TestEqual(TEXT("얼굴 데이터가 에셋 안에 있다(Inline)"), Faces[0].Font.GetLoadingPolicy(),
		EFontLoadingPolicy::Inline);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenUiFontFallbackTest, "Snarehaven.Ui.Font.FallbackDrawsJuaMissingGlyphs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenUiFontFallbackTest::RunTest(const FString& Parameters)
{
	// M13-27 뿌리: Jua 에 없는 글자는 복합 글꼴의 **대체 칸**이 그린다. 칸이 비면(역검증) 아래가 전부 Fail.
	const UFont* const UiFont = SnareUiStyle::GetUiFont();
	FSlateFontCache* const Cache = GetSlateFontCache();
	if (!TestNotNull(TEXT("UI 글꼴"), UiFont) || !TestNotNull(TEXT("Slate 글꼴 캐시"), Cache))
	{
		return false;
	}

	const FCompositeFont& Composite = UiFont->GetInternalCompositeFont();
	TestTrue(TEXT("대체 타입페이스에 얼굴이 물려 있다"), Composite.FallbackTypeface.Typeface.Fonts.Num() > 0);
	const FFontData& JuaData = Composite.DefaultTypeface.Fonts[0].Font;

	for (const TCHAR Ch : JuaMissingGlyphs)
	{
		// 목록이 맞는지부터 — Jua 가 그리게 되면 이 줄은 목록에서 빼야 한다(없는 결함을 지키는 시험이 된다).
		TestFalse(FString::Printf(TEXT("Jua 얼굴 자체에는 U+%04X 가 없다"), static_cast<uint32>(Ch)),
			Cache->CanLoadCodepoint(JuaData, Ch));
		TestTrue(FString::Printf(TEXT("UI 글꼴(대체 포함)이 U+%04X '%c' 를 그린다"), static_cast<uint32>(Ch), Ch),
			CanUiFontDraw(*Cache, UiFont, Ch));
	}

	// 결함이 보인 그 줄 — 미니맵 좌표 `북 0 · 동 0`.
	CheckDrawable(*this, *Cache, UiFont, TEXT("미니맵 좌표"), SnareMinimapHud::FormatCoordinates(FVector::ZeroVector).ToString());

	// 다음 목표 줄(M22 ⑤) 네 꼴 — 위젯 트리 글자는 답이 오기 전 한 꼴뿐이라 규칙 함수로 전부 만든다.
	FQuestLineView Open;
	Open.ItemID = TEXT("item.carrot");
	Open.Count = 3;
	Open.Reward = 5;
	FQuestLineView Done = Open;
	Done.bDone = true;
	auto Name = [](FName) { return FText::FromString(TEXT("당근")); };
	for (const int32 Owned : { 0, 3 })
	{
		CheckDrawable(*this, *Cache, UiFont, TEXT("다음 목표"),
			USnareNextGoalWidget::FormatGoal(true, { Open }, [Owned](FName) { return Owned; }, Name).ToString());
	}
	CheckDrawable(*this, *Cache, UiFont, TEXT("다음 목표"),
		USnareNextGoalWidget::FormatGoal(true, { Done }, [](FName) { return 0; }, Name).ToString());
	CheckDrawable(*this, *Cache, UiFont, TEXT("다음 목표"),
		USnareNextGoalWidget::FormatGoal(false, {}, [](FName) { return 0; }, Name).ToString());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenUiFontDataTextTest, "Snarehaven.Ui.Font.DataTableTextDrawable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenUiFontDataTextTest::RunTest(const FString& Parameters)
{
	// 화면으로 가는 표 글자(이름·설명 같은 FText 칸)의 글자 전부가 UI 글꼴로 그려지는지.
	const UFont* const UiFont = SnareUiStyle::GetUiFont();
	FSlateFontCache* const Cache = GetSlateFontCache();
	if (!TestNotNull(TEXT("UI 글꼴"), UiFont) || !TestNotNull(TEXT("Slate 글꼴 캐시"), Cache))
	{
		return false;
	}

	const TCHAR* const DataRootPath = TEXT("/Game/Game/Data");
	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	AssetRegistry.ScanPathsSynchronous({ FString(DataRootPath) }, /*bForceRescan=*/true);
	TArray<FAssetData> Tables;
	AssetRegistry.GetAssetsByClass(UDataTable::StaticClass()->GetClassPathName(), Tables, /*bSearchSubClasses=*/true);

	int32 TextCount = 0;
	for (const FAssetData& AssetData : Tables)
	{
		const UDataTable* const Table = AssetData.PackageName.ToString().StartsWith(DataRootPath)
			? Cast<UDataTable>(AssetData.GetAsset()) : nullptr;
		if (!IsValid(Table) || Table->GetRowStruct() == nullptr)
		{
			continue;
		}
		for (const TPair<FName, uint8*>& Row : Table->GetRowMap())
		{
			for (TFieldIterator<FTextProperty> It(Table->GetRowStruct()); It; ++It)
			{
				++TextCount;
				CheckDrawable(*this, *Cache, UiFont,
					FString::Printf(TEXT("%s:%s.%s"), *AssetData.AssetName.ToString(), *Row.Key.ToString(), *It->GetName()),
					It->GetPropertyValue_InContainer(Row.Value).ToString());
			}
			// 글 목록 칸(안내서 순서 줄 `GuideSteps` 같은 TArray<FText>)도 한 줄씩 — 루프#430 전엔 빠져 「」 가 숨었다.
			for (TFieldIterator<FArrayProperty> It(Table->GetRowStruct()); It; ++It)
			{
				if (!It->Inner->IsA<FTextProperty>())
				{
					continue;
				}
				FScriptArrayHelper Lines(*It, It->ContainerPtrToValuePtr<void>(Row.Value));
				for (int32 Index = 0; Index < Lines.Num(); ++Index)
				{
					++TextCount;
					CheckDrawable(*this, *Cache, UiFont,
						FString::Printf(TEXT("%s:%s.%s[%d]"), *AssetData.AssetName.ToString(), *Row.Key.ToString(), *It->GetName(), Index),
						reinterpret_cast<const FText*>(Lines.GetRawPtr(Index))->ToString());
				}
			}
		}
	}

	// 칸을 하나도 못 보면 조용히 통과한다 — 그 구멍을 막는다.
	TestTrue(FString::Printf(TEXT("표 글자 칸을 봤다 (%d칸)"), TextCount), TextCount > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenUiPaletteValuesTest, "Snarehaven.Ui.Color.PaletteMatchesDesign",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenUiPaletteValuesTest::RunTest(const FString& Parameters)
{
	// DESIGN 2.1 표의 헥스 네 개가 코드에 그대로 들어 있는지. 코드가 딴 색으로 바뀌면 여기서 걸린다
	// (설계서 표와 코드가 갈라지는 것이 이 검사가 막는 일이다).
	CheckColor(*this, TEXT("UI_ButtonBase"), SnareUiStyle::UiButtonBase(), FLinearColor::FromSRGBColor(FColor(0xF2, 0xE4, 0xC9)));
	CheckColor(*this, TEXT("UI_ButtonPressed"), SnareUiStyle::UiButtonPressed(), FLinearColor::FromSRGBColor(FColor(0xD8, 0xC2, 0x9E)));
	CheckColor(*this, TEXT("UI_ButtonHighlight"), SnareUiStyle::UiButtonHighlight(), FLinearColor::FromSRGBColor(FColor(0xF7, 0xD9, 0x8C)));
	CheckColor(*this, TEXT("UI_TextOnButton"), SnareUiStyle::UiTextOnButton(), FLinearColor::FromSRGBColor(FColor(0x4A, 0x38, 0x26)));
	// DESIGN 7.1 목장 나무판과 밧줄
	CheckColor(*this, TEXT("UI_WoodPanel"), SnareUiStyle::UiWoodButton(), FLinearColor::FromSRGBColor(FColor(0x88, 0x60, 0x3E)));
	CheckColor(*this, TEXT("UI_WoodGrain"), SnareUiStyle::UiWoodButtonPressed(), FLinearColor::FromSRGBColor(FColor(0x74, 0x51, 0x2F)));
	CheckColor(*this, TEXT("UI_Rope"), SnareUiStyle::UiRope(), FLinearColor::FromSRGBColor(FColor(0xD9, 0xBF, 0x8C)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenUiFontEveryScreenTest, "Snarehaven.Ui.Font.EveryScreenUsesOneFont",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenUiFontEveryScreenTest::RunTest(const FString& Parameters)
{
	ForEachBuiltScreen(*this, [this](const UUserWidget& Screen, const TCHAR* Name)
	{
		CheckEveryTextUsesUiFont(*this, Screen, Name);
	});
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenUiColorEveryButtonTest, "Snarehaven.Ui.Color.EveryButtonUsesPalette",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenUiColorEveryButtonTest::RunTest(const FString& Parameters)
{
	ForEachBuiltScreen(*this, [this](const UUserWidget& Screen, const TCHAR* Name)
	{
		CheckEveryButtonUsesPalette(*this, Screen, Name);
	});
	return true;
}

// VoiceOver(10-16 접근성) — 세운 화면의 모든 단추가 비지 않은 접근성 이름을 갖는지. 그림 단추·겹 라벨 단추가 빈 이름이던 일.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenUiAccessibleNameTest, "Snarehaven.Ui.Accessibility.EveryButtonNamed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenUiAccessibleNameTest::RunTest(const FString& Parameters)
{
#if WITH_ACCESSIBILITY
	int32 Checked = 0;
	ForEachBuiltScreen(*this, [this, &Checked](const UUserWidget& Screen, const TCHAR* Name)
	{
		Screen.WidgetTree->ForEachWidget([this, &Checked, Name](UWidget* Child)
		{
			const UButton* const Button = Cast<UButton>(Child);
			const TSharedPtr<SWidget> Slate = Button != nullptr ? Button->GetCachedWidget() : nullptr;
			if (!Slate.IsValid())
			{
				return;
			}
			++Checked;
			TestFalse(FString::Printf(TEXT("%s %s 접근성 이름"), Name, *Button->GetName()),
				Slate->GetAccessibleText().IsEmptyOrWhitespace());
		});
	});
	TestTrue(FString::Printf(TEXT("단추를 봤다 (%d개)"), Checked), Checked > 0);
#endif
	return true;
}

// 이름표(DESIGN 2.2 「이름표」) — 빈 이름·긴 이름 처리, 알약 색 여덟 가지가 번호대로 돌고 서로 다른지.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenNameTagTest, "Snarehaven.Ui.NameTag.Basics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenNameTagTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("빈 이름은 플레이어"), UPlayerNameTagWidget::MakeLabel(TEXT("  ")), FString(TEXT("플레이어")));
	TestEqual(TEXT("짧은 이름 그대로"), UPlayerNameTagWidget::MakeLabel(TEXT(" Sven ")), FString(TEXT("Sven")));
	const FString Long = UPlayerNameTagWidget::MakeLabel(TEXT("abcdefghijklmnopq"));
	TestEqual(TEXT("긴 이름은 12자"), Long.Len(), 12);
	TestTrue(TEXT("긴 이름은 … 로 끝남"), Long.EndsWith(TEXT("…")));
	TestEqual(TEXT("색은 8개로 돈다"), SnareUiStyle::UiNameTagColor(9), SnareUiStyle::UiNameTagColor(1));
	TestEqual(TEXT("음수 번호도 안전"), SnareUiStyle::UiNameTagColor(-7), SnareUiStyle::UiNameTagColor(1));
	for (int32 A = 0; A < 8; ++A)
	{
		for (int32 B = A + 1; B < 8; ++B)
		{
			TestNotEqual(TEXT("여덟 색은 서로 다름"), SnareUiStyle::UiNameTagColor(A), SnareUiStyle::UiNameTagColor(B));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenEmoteTest, "Snarehaven.Ui.Emote",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenEmoteTest::RunTest(const FString& Parameters)
{
	// DESIGN 2.2 「이모트」 — 인사·만세·춤·앉기 순서, 춤·앉기만 되풀이.
	const TArray<UWorldMapScreenWidget::FEmoteEntry>& Emotes = UWorldMapScreenWidget::GetEmotes();
	if (!TestEqual(TEXT("이모트 넷"), Emotes.Num(), 4))
	{
		return false;
	}
	const EItemAction Expected[] = { EItemAction::Wave, EItemAction::Cheer, EItemAction::Dance, EItemAction::Sit };
	for (int32 Index = 0; Index < 4; ++Index)
	{
		TestTrue(FString::Printf(TEXT("%d번 이모트 순서"), Index), Emotes[Index].Action == Expected[Index]);
		TestFalse(TEXT("이모트 글자 있음"), Emotes[Index].Label.IsEmpty());
	}
	TestFalse(TEXT("인사는 한 번"), ASnarehavenCharacter::IsLoopingItemAction(EItemAction::Wave));
	TestFalse(TEXT("만세는 한 번"), ASnarehavenCharacter::IsLoopingItemAction(EItemAction::Cheer));
	TestFalse(TEXT("먹기는 한 번"), ASnarehavenCharacter::IsLoopingItemAction(EItemAction::Eat));
	TestTrue(TEXT("춤은 되풀이"), ASnarehavenCharacter::IsLoopingItemAction(EItemAction::Dance));
	TestTrue(TEXT("앉기는 되풀이"), ASnarehavenCharacter::IsLoopingItemAction(EItemAction::Sit));
	for (const TCHAR* Path : { TEXT("/Game/Game/Characters/Player/Snare/A_Player_Wave.A_Player_Wave"),
		TEXT("/Game/Game/Characters/Player/Snare/A_Player_Cheer.A_Player_Cheer"),
		TEXT("/Game/Game/Characters/Player/Snare/A_Player_Dance.A_Player_Dance"),
		TEXT("/Game/Game/UI/Icons/T_UI_Smile.T_UI_Smile") })
	{
		TestNotNull(Path, LoadObject<UObject>(nullptr, Path));
	}
	return true;
}

// 감정표현 말풍선(M20-07 · DESIGN 2.2 「감정표현 말풍선」) — 여섯 글자가 UI 글꼴로 그려지고, 누른 기기는 바로 뜨고
// 게스트는 호스트에 부탁만 1회, 호스트는 범위를 보고 1회 중계, 받은 기기도 같은 번호를 3초 띄운 뒤 사라지는지.
// 회선 없는 월드라 역할(`SetRole`)로 게스트·호스트를 가르고 Multicast 받는 쪽은 `_Implementation` 을 직접 부른다(M16-08 과 같은 길).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenEmoteBubbleTest, "Snarehaven.Ui.EmoteBubble.ReplicatesAndFades",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenEmoteBubbleTest::RunTest(const FString& Parameters)
{
	// 시험 전용 시각 — 표의 3초가 바뀌어도 "직전엔 있고 넘으면 없다" 로만 본다.
	constexpr double TestJustBefore = 0.05;

	const UFont* const UiFont = SnareUiStyle::GetUiFont();
	FSlateFontCache* const Cache = GetSlateFontCache();
	if (!TestNotNull(TEXT("UI 글꼴"), UiFont) || !TestNotNull(TEXT("Slate 글꼴 캐시"), Cache)
		|| !TestEqual(TEXT("말풍선 여섯"), SnareEmoteBubble::Num(), 6))
	{
		return false;
	}
	for (int32 Index = 0; Index < SnareEmoteBubble::Num(); ++Index)
	{
		TestFalse(FString::Printf(TEXT("%d번 버튼 글자 있음"), Index), SnareEmoteBubble::GetLabel(Index).IsEmpty());
		TestFalse(FString::Printf(TEXT("%d번 말풍선 글자 있음"), Index), SnareEmoteBubble::GetGlyph(Index).IsEmpty());
		CheckDrawable(*this, *Cache, UiFont, TEXT("말풍선 버튼"), SnareEmoteBubble::GetLabel(Index).ToString());
		CheckDrawable(*this, *Cache, UiFont, TEXT("말풍선"), SnareEmoteBubble::GetGlyph(Index).ToString());
	}
	TestTrue(TEXT("범위 밖 글자는 빈 글"), SnareEmoteBubble::GetGlyph(6).IsEmpty());

	UWorld* const World = CreateTestWorld();
	ASnarehavenCharacter* const Player = World->SpawnActor<ASnarehavenCharacter>();
	ASnarehavenCharacter* const Other = World->SpawnActor<ASnarehavenCharacter>();
	if (!TestNotNull(TEXT("누르는 사람"), Player) || !TestNotNull(TEXT("같은 방 다른 사람(받는 기기의 사본)"), Other))
	{
		DestroyTestWorld(World);
		return false;
	}
	const double Now = World->GetTimeSeconds();
	const double End = Now + SnareEmoteBubble::ShowSeconds;
	TestEqual(TEXT("처음엔 말풍선 없음"), Player->GetEmoteBubbleAt(Now), static_cast<int32>(INDEX_NONE));

	// 게스트: 제 머리 위엔 바로 뜨고 호스트에 부탁 1회 — 중계는 호스트 몫이라 0.
	Player->SetRole(ROLE_AutonomousProxy);
	Player->ShowEmoteBubble(2);
	TestEqual(TEXT("[게스트] 부탁 1회"), Player->GetEmoteBubbleRequestCount(), 1);
	TestEqual(TEXT("[게스트] 중계는 호스트 몫"), Player->GetRelayedEmoteBubbleCount(), 0);
	TestEqual(TEXT("[게스트] 내 머리 위 하트"), Player->GetEmoteBubbleAt(Now), 2);

	// 호스트: 부탁을 받으면 1회 중계, 범위 밖 번호는 버린다.
	Player->SetRole(ROLE_Authority);
	Player->Server_ShowEmoteBubble_Implementation(2);
	TestEqual(TEXT("[호스트] 중계 1회"), Player->GetRelayedEmoteBubbleCount(), 1);
	Player->Server_ShowEmoteBubble_Implementation(200);
	TestEqual(TEXT("[호스트] 범위 밖은 중계 안 함"), Player->GetRelayedEmoteBubbleCount(), 1);

	// 받는 기기: 같은 번호를 3초 띄우고 넘으면 사라진다.
	Other->Multicast_ShowEmoteBubble_Implementation(2);
	TestEqual(TEXT("[다른 기기] 같은 하트"), Other->GetEmoteBubbleAt(Now), 2);
	TestEqual(TEXT("[다른 기기] 3초 직전엔 있음"), Other->GetEmoteBubbleAt(End - TestJustBefore), 2);
	TestEqual(TEXT("[다른 기기] 3초에 사라짐"), Other->GetEmoteBubbleAt(End), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("[누른 기기] 3초에 사라짐"), Player->GetEmoteBubbleAt(End), static_cast<int32>(INDEX_NONE));

	// 소리 (M54-03): 유효 번호는 받은 기기마다 한 번, 같은 순간 연타는 건너뛰고, 범위 밖은 0.
	TestEqual(TEXT("[다른 기기] 말풍선 소리 1"), Other->GetEmoteSoundCount(), 1);
	Other->Multicast_ShowEmoteBubble_Implementation(3);
	TestEqual(TEXT("[다른 기기] 연타 간격 안이면 소리 그대로"), Other->GetEmoteSoundCount(), 1);
	Other->Multicast_ShowEmoteBubble_Implementation(200);
	TestEqual(TEXT("[다른 기기] 범위 밖은 소리 0 늘어남"), Other->GetEmoteSoundCount(), 1);

	// 호스트 본인이 눌러도 같은 길 — 부탁·중계가 각각 1회 늘고, 새 번호로 바뀐다. 범위 밖 번호는 아무것도 안 나간다.
	Player->ShowEmoteBubble(5);
	TestEqual(TEXT("[호스트] 부탁 2회째"), Player->GetEmoteBubbleRequestCount(), 2);
	TestEqual(TEXT("[호스트] 중계 2회째"), Player->GetRelayedEmoteBubbleCount(), 2);
	TestEqual(TEXT("[호스트] 새 말풍선"), Player->GetEmoteBubbleAt(Now), 5);
	Player->ShowEmoteBubble(-1);
	Player->ShowEmoteBubble(6);
	TestEqual(TEXT("범위 밖 번호는 안 나감"), Player->GetEmoteBubbleRequestCount(), 2);

	AddInfo(FString::Printf(TEXT("[Emote] 말풍선 %d개 · 부탁 %d · 중계 %d · %.1f초 뒤 사라짐"), SnareEmoteBubble::Num(),
		Player->GetEmoteBubbleRequestCount(), Player->GetRelayedEmoteBubbleCount(), SnareEmoteBubble::ShowSeconds));
	DestroyTestWorld(World);
	return true;
}

// 방 안 이름(M21-01c · DESIGN 2.2 「이름표」) — 접속 주소가 기기 호스트명을 들고 와도 머리 위엔 "플레이어 N".
// 실제 게임모드·게임 상태가 선 월드에서 플레이어 셋을 붙이고, 가운데가 나간 자리를 새 사람이 채우는지 본다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenRoomPlayerNameTest, "Snarehaven.Ui.RoomPlayerName.NotHostName",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenRoomPlayerNameTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("빈 방은 1번"), ASnarehavenGameMode::MakeRoomPlayerName({}), FString(TEXT("플레이어 1")));
	TestEqual(TEXT("빈 번호 먼저"), ASnarehavenGameMode::MakeRoomPlayerName({TEXT("플레이어 1"), TEXT("플레이어 3")}),
		FString(TEXT("플레이어 2")));

	UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	ASnarehavenGameMode* const GameMode = World->SpawnActor<ASnarehavenGameMode>();
	TestNotNull(TEXT("게임 상태가 섰다"), World->GetGameState());

	const FString HostName = TEXT("gdmui-MacBook-Pro-6A1F");
	TArray<APlayerController*> Players;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		APlayerController* const Player = World->SpawnActor<APlayerController>();
		if (GameMode != nullptr && Player != nullptr && Player->PlayerState != nullptr)
		{
			GameMode->ChangeName(Player, HostName, false);
			Players.Add(Player);
		}
	}
	if (TestEqual(TEXT("플레이어 셋이 붙음"), Players.Num(), 3))
	{
		TestEqual(TEXT("첫째"), Players[0]->PlayerState->GetPlayerName(), FString(TEXT("플레이어 1")));
		TestEqual(TEXT("둘째"), Players[1]->PlayerState->GetPlayerName(), FString(TEXT("플레이어 2")));
		TestEqual(TEXT("셋째"), Players[2]->PlayerState->GetPlayerName(), FString(TEXT("플레이어 3")));
		GameMode->ChangeName(Players[0], HostName, true);
		TestEqual(TEXT("바꾸기 요청은 무시"), Players[0]->PlayerState->GetPlayerName(), FString(TEXT("플레이어 1")));

		Players[1]->Destroy();
		APlayerController* const Late = World->SpawnActor<APlayerController>();
		GameMode->ChangeName(Late, HostName, false);
		TestEqual(TEXT("나간 자리를 채움"), Late->PlayerState->GetPlayerName(), FString(TEXT("플레이어 2")));
		UE_LOG(LogTemp, Display, TEXT("[RoomName] %s · %s · %s (접속 이름 %s)"), *Players[0]->PlayerState->GetPlayerName(),
			*Late->PlayerState->GetPlayerName(), *Players[2]->PlayerState->GetPlayerName(), *HostName);
	}

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

// 이름표 색 = 방 번호(DESIGN 2.2 「이름표」 색 줄) — 실제 게임모드가 8명에게 "플레이어 1~8" 을 주면 알약 색 8개가 전부 다르고,
// 가운데가 나간 뒤 들어온 사람은 그 자리 색을 받아 여전히 겹치지 않는다. 엔진 PlayerId 는 접속마다 늘어 이 보장이 없다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenNameTagSlotColorTest, "Snarehaven.Ui.NameTag.RoomSlotColors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenNameTagSlotColorTest::RunTest(const FString& Parameters)
{
	constexpr int32 RoomMax = 8;  // DESIGN 1 「4~8인」 최대
	TestEqual(TEXT("플레이어 1 = 0번 색"), UPlayerNameTagWidget::GetColorSlot(TEXT("플레이어 1"), 300), 0);
	TestEqual(TEXT("플레이어 8 = 7번 색"), UPlayerNameTagWidget::GetColorSlot(TEXT("플레이어 8"), 300), 7);
	TestEqual(TEXT("방 이름 꼴이 아니면 PlayerId"), UPlayerNameTagWidget::GetColorSlot(TEXT("Sven"), 3), 3);
	TestEqual(TEXT("번호가 0 이하면 PlayerId"), UPlayerNameTagWidget::GetColorSlot(TEXT("플레이어 0"), 5), 5);

	UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	ASnarehavenGameMode* const GameMode = World->SpawnActor<ASnarehavenGameMode>();

	TArray<APlayerController*> Players;
	for (int32 Index = 0; Index < RoomMax && GameMode != nullptr; ++Index)
	{
		APlayerController* const Player = World->SpawnActor<APlayerController>();
		if (Player != nullptr && Player->PlayerState != nullptr)
		{
			GameMode->ChangeName(Player, FString(), false);
			Players.Add(Player);
		}
	}
	const auto ColorsOf = [&Players]()
	{
		TArray<FString> Hex;
		for (const APlayerController* const Player : Players)
		{
			const APlayerState* const State = Player->PlayerState;
			Hex.Add(SnareUiStyle::UiNameTagColor(UPlayerNameTagWidget::GetColorSlot(State->GetPlayerName(), State->GetPlayerId()))
				.ToFColorSRGB().ToHex());
		}
		return Hex;
	};
	const auto CountDistinct = [](const TArray<FString>& Hex)
	{
		TSet<FString> Unique(Hex);
		return Unique.Num();
	};

	if (TestEqual(TEXT("8명이 붙음"), Players.Num(), RoomMax))
	{
		const TArray<FString> Before = ColorsOf();
		TestEqual(TEXT("8명 색이 전부 다름"), CountDistinct(Before), RoomMax);

		// 4번째가 나가고 새 사람이 들어온다 — 같은 "플레이어 4" 를 받아 같은 색.
		const FString LeftName = Players[3]->PlayerState->GetPlayerName();
		Players[3]->Destroy();
		APlayerController* const Late = World->SpawnActor<APlayerController>();
		GameMode->ChangeName(Late, FString(), false);
		Players[3] = Late;
		const TArray<FString> After = ColorsOf();
		TestEqual(TEXT("나간 자리 이름을 받음"), Late->PlayerState->GetPlayerName(), LeftName);
		TestEqual(TEXT("나간 자리 색을 받음"), After[3], Before[3]);
		TestEqual(TEXT("다시 들어와도 8색 다름"), CountDistinct(After), RoomMax);

		UE_LOG(LogTemp, Display, TEXT("[NameTagColor] 처음 %s · 다시 들어온 뒤 %s (서로 다른 색 %d/%d · 들어온 사람 %s PlayerId %d)"),
			*FString::Join(Before, TEXT(",")), *FString::Join(After, TEXT(",")), CountDistinct(After), RoomMax,
			*Late->PlayerState->GetPlayerName(), Late->PlayerState->GetPlayerId());
	}

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenUiTextContrastTest, "Snarehaven.Ui.Style.TextContrast",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenUiTextContrastTest::RunTest(const FString& Parameters)
{
	// 글자색 / 바탕색 / 이유 — 모두 4.5:1 이상이어야 한다 (M99-06). 제작 판 줄·탭이 진갈 위 진갈, 노랑 위 크림이던 일.
	constexpr float MinRatio = 4.5f;
	struct FPair { const TCHAR* Name; FLinearColor Text; FLinearColor Back; };
	const FPair Pairs[] = {
		{TEXT("나무 줄 위 글"), SnareUiStyle::UiTextOnWood(), SnareUiStyle::UiWoodButton()},
		{TEXT("눌린·비활성 나무 줄 위 글"), SnareUiStyle::UiTextOnWood(), SnareUiStyle::UiWoodButtonPressed()},
		{TEXT("모자란 재료 칩 위 주황 글"), SnareUiStyle::UiWarnOnWood(), SnareUiStyle::UiWarnChip()},
		{TEXT("고른 탭 노랑 위 진갈 글"), SnareUiStyle::UiTextOnButton(), SnareUiStyle::UiButtonHighlight()},
		{TEXT("제목 알약(밝은 나무) 위 흰 글"), FLinearColor::White, SnareUiStyle::UiWoodButton()},
		{TEXT("나무 판 위 크림 글"), SnareUiStyle::UiTextOnWood(), SnareUiStyle::UiWoodPanel()},
		{TEXT("나무 판 위 주황 경고 글"), SnareUiStyle::UiWarnOnWood(), SnareUiStyle::UiWoodPanel()},
		{TEXT("크림 판 위 진갈 글"), SnareUiStyle::UiTextOnButton(), SnareUiStyle::UiPanelCream()},
		// 10-16 접근성 전수 — 팔레트가 실제로 겹치는 글/바탕 쌍.
		{TEXT("나무 단추 위 크림 글(ApplyUiStyle)"), SnareUiStyle::UiPanelCream(), SnareUiStyle::UiWoodButton()},
		{TEXT("눌린 나무 단추 위 크림 글"), SnareUiStyle::UiPanelCream(), SnareUiStyle::UiWoodButtonPressed()},
		{TEXT("꺼진 단추 위 크림 글"), SnareUiStyle::UiPanelCream(), SnareUiStyle::UiDisabledGray()},
		{TEXT("크림 판 위 흐린 글(도감 빈 칸)"), SnareUiStyle::ScreenTextColor(true), SnareUiStyle::UiPanelCream()},
		{TEXT("크림 판 위 가격 초록"), SnareUiStyle::UiShopPriceGreen(), SnareUiStyle::UiPanelCream()},
		{TEXT("빨강 알약 위 흰 글(오류·이름 알약)"), FLinearColor::White, SnareUiStyle::UiAccentRed()},
		{TEXT("크림 위 빨강 글(상자 상태)"), SnareUiStyle::UiAccentRed(), SnareUiStyle::UiPanelCream()},
		{TEXT("크림 카드 위 경고 빨강"), SnareUiStyle::UiWarnRed(), SnareUiStyle::UiPanelCream()},
		{TEXT("남색 알약 위 흰 글"), FLinearColor::White, SnareUiStyle::UiNavy()},
		{TEXT("제목 탭 위 흰 글"), FLinearColor::White, SnareUiStyle::UiTitleTab()}};
	for (const FPair& Pair : Pairs)
	{
		const float Ratio = SnareUiStyle::ContrastRatio(Pair.Text, Pair.Back);
		TestTrue(FString::Printf(TEXT("%s 대비 %.2f >= %.1f"), Pair.Name, Ratio, MinRatio), Ratio >= MinRatio);
	}
	for (int32 Index = 0; Index < 8; ++Index)
	{
		const float Ratio = SnareUiStyle::ContrastRatio(FLinearColor::White, SnareUiStyle::UiNameTagColor(Index));
		TestTrue(FString::Printf(TEXT("이름표 %d번 위 흰 글 대비 %.2f >= %.1f"), Index, Ratio, MinRatio), Ratio >= MinRatio);
	}
	// 그림(포획 고리 테)은 WCAG 비문자 3:1 — 창 안 초록 테가 기본 크림 테와 갈려야 한다.
	const float RingRatio = SnareUiStyle::ContrastRatio(SnareUiStyle::UiShopPriceGreen(), SnareUiStyle::UiButtonBase());
	TestTrue(FString::Printf(TEXT("포획 고리 초록/크림 대비 %.2f >= 3"), RingRatio), RingRatio >= 3.f);
	// 역검증: 옛 가격 초록 #3DBB63 은 크림 위에서 못 넘는다.
	TestTrue(TEXT("역검증 - 옛 가격 초록은 4.5 미만"), SnareUiStyle::ContrastRatio(
		FLinearColor::FromSRGBColor(FColor(0x3D, 0xBB, 0x63)), SnareUiStyle::UiPanelCream()) < MinRatio);
	// 역검증: 고치기 전 조합(나무 위 진갈 글)은 이 잣대를 못 넘어야 한다.
	TestTrue(TEXT("역검증 - 나무 위 진갈 글은 4.5 미만"),
		SnareUiStyle::ContrastRatio(SnareUiStyle::UiTextOnButton(), SnareUiStyle::UiWoodButton()) < MinRatio);
	return true;
}

// 오른쪽 기둥 코드 그림 셋(달력·톱니·얼굴)의 원본 화소 — 화면 표시 크기 72 × 아이폰 배율 3 이하면 늘어나 계단이 진다 (INBOX 결함 10-11 23:15).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenRightColumnIconSourceTest, "Snarehaven.Ui.RightColumnIcons.SourceCoversPhoneScale",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenRightColumnIconSourceTest::RunTest(const FString& Parameters)
{
	// 시험 전용 값 — 표시 크기·배율 표가 바뀌어도 "원본 >= 표시×배율" 관계만 본다.
	constexpr int32 TestDisplayPx = 72;
	constexpr int32 TestPhoneScale = 3;
	for (const TCHAR* Path : { TEXT("/Game/Game/UI/Icons/T_UI_Calendar.T_UI_Calendar"),
		TEXT("/Game/Game/UI/Icons/T_UI_Gear.T_UI_Gear"), TEXT("/Game/Game/UI/Icons/T_UI_Smile.T_UI_Smile") })
	{
		const UTexture2D* const Texture = LoadObject<UTexture2D>(nullptr, Path);
		if (!TestNotNull(Path, Texture))
		{
			continue;
		}
		// -nullrhi 에선 GetSizeX() 가 0 — 반입 원본(Source)의 화소를 본다.
		const int32 SourceX = Texture->Source.GetSizeX();
		const int32 SourceY = Texture->Source.GetSizeY();
		TestTrue(FString::Printf(TEXT("%s 원본 %dx%d >= %d"), Path, SourceX, SourceY, TestDisplayPx * TestPhoneScale),
			SourceX >= TestDisplayPx * TestPhoneScale && SourceY >= TestDisplayPx * TestPhoneScale);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
