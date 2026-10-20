// 지시(감독 10-19) HUD 겹침 공통 시험 — 계산 함수가 아니라 **실제 배치 사각형**으로 대사 판·할 일 판·사냥 HUD 가
// 서로 덮지 않고 화면 밖으로 나가지 않는지 본다. 겹치면 두 위젯 이름과 겹친 px 를 실패 문구에 적는다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.UI.HudNoOverlap; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Backend/SnareStory.h"
#include "Blueprint/UserWidget.h"
#include "Components/CanvasPanel.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"
#include "Engine/Texture2D.h"
#include "World/SeaActor.h"
#include "Engine/Engine.h"
#include "Engine/UserInterfaceSettings.h"
#include "Engine/World.h"
#include "Layout/ArrangedChildren.h"
#include "UI/HuntHudWidget.h"
#include "UI/SnareStoryDialogueWidget.h"
#include "UI/SnareStoryGoalWidget.h"

namespace
{
	struct FNamedRect
	{
		FString Name;
		FSlateRect Rect;
	};

	// 슬레이트 트리를 루트 사각형에서부터 직접 배치해 목표 위젯의 사각형을 찾는다 (접힌 위젯은 배치에서 빠져 못 찾는다).
	bool FindArranged(const FGeometry& Geo, const TSharedRef<SWidget>& Cur, const SWidget* Target, FSlateRect& Out)
	{
		if (&Cur.Get() == Target)
		{
			Out = Geo.GetLayoutBoundingRect();
			return true;
		}
		FArrangedChildren Children(EVisibility::Visible);
		Cur->ArrangeChildren(Geo, Children);
		for (int32 Index = 0; Index < Children.Num(); ++Index)
		{
			if (FindArranged(Children[Index].Geometry, Children[Index].Widget, Target, Out))
			{
				return true;
			}
		}
		return false;
	}

	bool RectOf(UUserWidget* Owner, const TSharedRef<SWidget>& Root, const FGeometry& RootGeo, const TCHAR* WidgetName, FNamedRect& Out)
	{
		const UWidget* const Found = Owner->GetWidgetFromName(WidgetName);
		if (Found == nullptr || !Found->GetCachedWidget().IsValid())
		{
			return false;
		}
		Out.Name = WidgetName;
		return FindArranged(RootGeo, Root, Found->GetCachedWidget().Get(), Out.Rect);
	}

	// 겹친 가로·세로 길이 중 작은 쪽(px) — 0 이하면 안 겹친다.
	float OverlapPx(const FSlateRect& A, const FSlateRect& B)
	{
		const float W = FMath::Min(A.Right, B.Right) - FMath::Max(A.Left, B.Left);
		const float H = FMath::Min(A.Bottom, B.Bottom) - FMath::Max(A.Top, B.Top);
		return FMath::Min(W, H);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenHudNoOverlapTest, "Snarehaven.UI.HudNoOverlap", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSnarehavenHudNoOverlapTest::RunTest(const FString& Parameters)
{
	UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());

	// 게임의 미니맵은 바다 액터의 지도 그림 틀을 쓴다 — 시험 월드에도 그림이 있는 바다를 세워 같은 크기의 미니맵이 서게 한다.
	if (ASeaActor* const Sea = World->SpawnActor<ASeaActor>())
	{
		Sea->MapTexture = UTexture2D::CreateTransient(4, 4);
	}

	UHuntHudWidget* const Hunt = CreateWidget<UHuntHudWidget>(World, UHuntHudWidget::StaticClass());
	USnareStoryGoalWidget* const Goal = CreateWidget<USnareStoryGoalWidget>(World, USnareStoryGoalWidget::StaticClass());
	USnareStoryDialogueWidget* const Dialogue = CreateWidget<USnareStoryDialogueWidget>(World, USnareStoryDialogueWidget::StaticClass());
	if (TestNotNull(TEXT("사냥 HUD"), Hunt) && TestNotNull(TEXT("할 일 판"), Goal) && TestNotNull(TEXT("대사 판"), Dialogue))
	{
		Dialogue->bPersistSeen = false;
		const TSharedRef<SWidget> HuntSlate = Hunt->TakeWidget();
		const TSharedRef<SWidget> GoalSlate = Goal->TakeWidget();
		const TSharedRef<SWidget> DialogueSlate = Dialogue->TakeWidget();

		// 할 일 판: 서버 답 없이 펴고 가장 긴 목표 글을 싣는다. 미니맵: 스토리 목표가 있을 때처럼 편다.
		FText LongestGoal;
		for (int32 Step = 1; Step <= SnareStory::TotalSteps; ++Step)
		{
			const FText Text = SnareStory::StepGoal(Step);
			if (Text.ToString().Len() > LongestGoal.ToString().Len())
			{
				LongestGoal = Text;
			}
		}
		if (UTextBlock* const GoalText = Cast<UTextBlock>(Goal->GetWidgetFromName(TEXT("StoryGoalText"))))
		{
			GoalText->SetText(LongestGoal);
		}
		if (UWidget* const Card = Goal->GetWidgetFromName(TEXT("StoryGoalCard")))
		{
			Card->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		}
		if (UWidget* const Layer = Hunt->GetWidgetFromName(TEXT("MinimapLayer")))
		{
			Layer->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		}

		const FIntPoint Sizes[] = {FIntPoint(1280, 720), FIntPoint(1334, 616), FIntPoint(2532, 1170)};
		for (const FIntPoint& Pixels : Sizes)
		{
			const float Dpi = GetDefault<UUserInterfaceSettings>()->GetDPIScaleBasedOnSize(Pixels);
			const FVector2f Slate(Pixels.X / Dpi, Pixels.Y / Dpi);
			const FGeometry RootGeo = FGeometry::MakeRoot(Slate, FSlateLayoutTransform());
			const FSlateRect Screen(0.f, 0.f, Slate.X, Slate.Y);
			const FString Tag = FString::Printf(TEXT("%dx%d(배율 %.2f)"), Pixels.X, Pixels.Y, Dpi);

			// 사냥 HUD 에서 화면에 보이는 칸들 — 루트 캔버스 바로 아래 자식(화면을 덮는 층은 뺌) + 미니맵 판.
			HuntSlate->SlatePrepass(1.f);
			GoalSlate->SlatePrepass(1.f);
			TArray<FNamedRect> HuntRects;
			if (const UCanvasPanel* const Root = Cast<UCanvasPanel>(Hunt->GetWidgetFromName(TEXT("HudRoot"))))
			{
				for (UWidget* const Child : Root->GetAllChildren())
				{
					FNamedRect Item;
					if (Child != nullptr && RectOf(Hunt, HuntSlate, RootGeo, *Child->GetName(), Item)
						&& Item.Rect.GetSize().X > 0.f && Item.Rect.GetSize().Y > 0.f
						&& !(Item.Rect.GetSize().X >= Slate.X * 0.9f && Item.Rect.GetSize().Y >= Slate.Y * 0.9f))
					{
						HuntRects.Add(Item);
					}
				}
			}
			FNamedRect MapRect;
			if (RectOf(Hunt, HuntSlate, RootGeo, TEXT("MinimapPanel"), MapRect))
			{
				HuntRects.Add(MapRect);
			}
			FNamedRect GoalRect;
			const bool bGoalFound = RectOf(Goal, GoalSlate, RootGeo, TEXT("StoryGoalCard"), GoalRect);
			TestTrue(*FString::Printf(TEXT("%s: 할 일 판이 배치됨"), *Tag), bGoalFound);
			TestTrue(*FString::Printf(TEXT("%s: 미니맵 판이 배치됨"), *Tag), MapRect.Name == TEXT("MinimapPanel"));
			TestTrue(*FString::Printf(TEXT("%s: 사냥 HUD 칸이 읽힘(%d)"), *Tag, HuntRects.Num()), HuntRects.Num() > 3);

			const auto CheckPair = [this, &Tag](const FNamedRect& A, const FNamedRect& B)
			{
				const float Px = OverlapPx(A.Rect, B.Rect);
				TestTrue(*FString::Printf(TEXT("%s: %s ↔ %s 가 %.0fpx 겹침"), *Tag, *A.Name, *B.Name, Px), Px <= 0.5f);
			};
			const auto CheckInside = [this, &Tag, &Screen](const FNamedRect& A)
			{
				const bool bInside = A.Rect.Left >= -0.5f && A.Rect.Top >= -0.5f && A.Rect.Right <= Screen.Right + 0.5f && A.Rect.Bottom <= Screen.Bottom + 0.5f;
				TestTrue(*FString::Printf(TEXT("%s: %s 가 화면 밖 (%.0f,%.0f)-(%.0f,%.0f)"), *Tag, *A.Name, A.Rect.Left, A.Rect.Top, A.Rect.Right, A.Rect.Bottom), bInside);
			};

			const auto Describe = [](const FNamedRect& A)
			{
				return FString::Printf(TEXT("%s(%.0f,%.0f-%.0f,%.0f)"), *A.Name, A.Rect.Left, A.Rect.Top, A.Rect.Right, A.Rect.Bottom);
			};
			FString Listing = Describe(GoalRect);
			for (const FNamedRect& Item : HuntRects)
			{
				Listing += TEXT(" ") + Describe(Item);
			}
			AddInfo(FString::Printf(TEXT("[HudNoOverlap] %s 화면 %.0fx%.0f: %s"), *Tag, Slate.X, Slate.Y, *Listing));

			if (bGoalFound)
			{
				CheckInside(GoalRect);
				for (const FNamedRect& Item : HuntRects)
				{
					CheckPair(GoalRect, Item);
				}
			}
			for (const FNamedRect& Item : HuntRects)
			{
				CheckInside(Item);
			}

			// 대사 판: 모든 단계·모든 줄(가장 긴 줄 포함)에서 할 일 판·사냥 HUD 를 안 덮는다.
			for (int32 Step = 1; Step <= SnareStory::TotalSteps; ++Step)
			{
				if (!Dialogue->ShowStep(Step))
				{
					continue;
				}
				for (int32 Line = 0; Line < Dialogue->GetLineCount(); ++Line)
				{
					Dialogue->FitToScreen(Slate.X);
					DialogueSlate->SlatePrepass(1.f);
					FNamedRect DialogueRect;
					if (RectOf(Dialogue, DialogueSlate, RootGeo, TEXT("DialoguePanelBox"), DialogueRect))
					{
						DialogueRect.Name = FString::Printf(TEXT("DialoguePanelBox(%d단계 %d줄)"), Step, Line + 1);
						if (Step == 15 && Line == 0)
						{
							AddInfo(FString::Printf(TEXT("[HudNoOverlap] %s 대사 판 %s"), *Tag, *Describe(DialogueRect)));
						}
						CheckInside(DialogueRect);
						if (bGoalFound)
						{
							CheckPair(DialogueRect, GoalRect);
						}
						for (const FNamedRect& Item : HuntRects)
						{
							CheckPair(DialogueRect, Item);
						}
					}
					else
					{
						AddError(FString::Printf(TEXT("%s: 대사 판이 배치 안 됨 (%d단계 %d줄)"), *Tag, Step, Line + 1));
					}
					Dialogue->Advance();
				}
			}
		}
	}
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
