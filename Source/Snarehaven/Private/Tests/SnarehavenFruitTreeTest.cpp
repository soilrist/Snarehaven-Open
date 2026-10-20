// DESIGN 8.18 과일나무 몸 2층 검사 — `SnareFruitTree`(서버 답 → 그릴 값) + `AFruitTreeActor`.
// 이 검사가 잡는 것: ① 서버 `trees[]` 가 자리 번호대로 안 들어가는 것 ② 목록에 없는 자리가 빈 자리가 아닌 것
// ③ 묘목에 열매가 보이거나 다 자란 나무 열매가 서버 수를 안 따르는 것 ④ 과일 5종 색이 서로 같은 것
// ⑤ 액터가 단계·열매를 그대로 그리지 않는 것.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/World.h"
#include "EngineUtils.h"
#include "Ranch/RanchGameMode.h"
#include "World/FruitTreeActor.h"
#include "World/FruitTreeSync.h"
#include "World/SnareFruitTree.h"

namespace
{
	TSharedPtr<FJsonObject> MakeTree(const int32 Slot, const TCHAR* Fruit, const TCHAR* Stage, const int32 Fruits)
	{
		const TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetNumberField(TEXT("slot"), Slot);
		Row->SetStringField(TEXT("fruit"), Fruit);
		Row->SetStringField(TEXT("stage"), Stage);
		Row->SetNumberField(TEXT("fruits"), Fruits);
		return Row;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenFruitTreeParseTest, "Snarehaven.World.FruitTree.Parse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenFruitTreeParseTest::RunTest(const FString& Parameters)
{
	using namespace SnareFruitTree;
	const TSharedPtr<FJsonObject> Reply = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Trees;
	Trees.Add(MakeShared<FJsonValueObject>(MakeTree(2, TEXT("item.cherry"), TEXT("sapling"), 0)));
	Trees.Add(MakeShared<FJsonValueObject>(MakeTree(0, TEXT("item.pear"), TEXT("grown"), 3)));
	Trees.Add(MakeShared<FJsonValueObject>(MakeTree(9, TEXT("item.pear"), TEXT("grown"), 3)));   // 범위 밖은 버린다
	Reply->SetArrayField(TEXT("trees"), Trees);

	TArray<FTreeView> BySlot;
	TestTrue(TEXT("trees 필드가 있으면 true"), ParseTrees(Reply, BySlot));
	TestEqual(TEXT("자리는 늘 SlotCount 칸"), BySlot.Num(), SlotCount);
	TestTrue(TEXT("0번 다 자란 배 나무"), BySlot[0].Stage == EStage::Grown && BySlot[0].Fruit == TEXT("item.pear") && BySlot[0].Fruits == 3);
	TestTrue(TEXT("1번은 목록에 없으니 빈 자리"), BySlot[1].Stage == EStage::Empty);
	TestTrue(TEXT("2번 묘목 체리"), BySlot[2].Stage == EStage::Sapling && BySlot[2].Fruit == TEXT("item.cherry"));
	TestTrue(TEXT("3번은 빈 자리"), BySlot[3].Stage == EStage::Empty);

	TArray<FTreeView> Untouched;
	TestFalse(TEXT("trees 필드 없는 답은 false"), ParseTrees(MakeShared<FJsonObject>(), Untouched));

	TestTrue(TEXT("열매 딴 나무에 도끼면 벨 수 있다"), CanChop(EStage::Grown, 0, true));
	TestFalse(TEXT("도끼 없으면 못 벤다"), CanChop(EStage::Grown, 0, false));
	TestFalse(TEXT("열매 달린 나무는 못 벤다"), CanChop(EStage::Grown, 3, true));
	TestFalse(TEXT("묘목·빈 자리는 못 벤다"), CanChop(EStage::Sapling, 0, true) || CanChop(EStage::Empty, 0, true));
	TestEqual(TEXT("묘목은 열매 0"), VisibleFruits(EStage::Sapling, 3), 0);
	TestEqual(TEXT("다 자란 나무는 서버 수"), VisibleFruits(EStage::Grown, 2), 2);
	TestEqual(TEXT("서버 수가 3 을 넘어도 3"), VisibleFruits(EStage::Grown, 99), 3);
	TestTrue(TEXT("묘목은 다 자란 나무보다 작다"), StageScale(EStage::Sapling) > 0.f && StageScale(EStage::Sapling) < StageScale(EStage::Grown));
	TestEqual(TEXT("빈 자리는 키 0"), StageScale(EStage::Empty), 0.f);

	const FName Fruits[] = {TEXT("item.pear"), TEXT("item.peach"), TEXT("item.cherry"), TEXT("item.orange"), TEXT("item.plum")};
	for (int32 A = 0; A < 5; ++A)
	{
		for (int32 B = A + 1; B < 5; ++B)
		{
			TestFalse(*FString::Printf(TEXT("%s 와 %s 색이 다르다"), *Fruits[A].ToString(), *Fruits[B].ToString()),
				FruitColor(Fruits[A]).Equals(FruitColor(Fruits[B]), 0.05f));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenFruitTreeActorTest, "Snarehaven.World.FruitTree.Actor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenFruitTreeActorTest::RunTest(const FString& Parameters)
{
	using namespace SnareFruitTree;
	UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("시험 월드"), World))
	{
		return false;
	}
	AFruitTreeActor* const Tree = World->SpawnActor<AFruitTreeActor>();
	if (TestNotNull(TEXT("나무 액터"), Tree))
	{
		FTreeView View;
		View.Slot = 1;
		View.Fruit = TEXT("item.plum");
		View.Stage = EStage::Grown;
		View.Fruits = 2;
		Tree->ApplyView(View);
		TestEqual(TEXT("다 자란 나무 열매 2개"), Tree->GetShownFruitCount(), 2);
		TestTrue(TEXT("자두 색"), Tree->GetFruitColor().Equals(FruitColor(TEXT("item.plum")), 0.001f));

		View.Stage = EStage::Sapling;
		Tree->ApplyView(View);
		TestEqual(TEXT("묘목엔 열매가 안 보인다"), Tree->GetShownFruitCount(), 0);

		View.Stage = EStage::Empty;
		Tree->ApplyView(View);
		TestTrue(TEXT("빈 자리"), Tree->GetStage() == EStage::Empty);
	}
	{
		const FString Host = TEXT("HOSTACCOUNT01");
		AFruitTreeActor* const Friend = AFruitTreeActor::SpawnFriendTree(World, FTransform::Identity, Host, TEXT("곰돌이"));
		if (TestNotNull(TEXT("친구 나무"), Friend))
		{
			TestTrue(TEXT("친구 나무 표시"), Friend->IsFriendTree());
			TestEqual(TEXT("방장 id"), Friend->GetFriendHostId(), Host);
			TestEqual(TEXT("열매 3개"), Friend->GetShownFruitCount(), 3);
			TestTrue(TEXT("방장 고유 과일 색"), Friend->GetFruitColor().Equals(FruitColor(NativeFruitOf(Host)), 0.001f));
			TestEqual(TEXT("이름표"), Friend->GetFriendLabel().ToString(), FriendTreeLabel(TEXT("곰돌이"), NativeFruitOf(Host)).ToString());
			TestEqual(TEXT("내 자리 번호 없음"), Friend->SlotIndex, (int32)INDEX_NONE);
			TestTrue(TEXT("친구 나무 이름표가 켜진다"), Friend->IsLabelShown());
			TestEqual(TEXT("이름표 위젯 글자"), Friend->GetShownLabelText().ToString(), Friend->GetFriendLabel().ToString());
		}
		TestNull(TEXT("방장 id 가 비면 안 세운다"), AFruitTreeActor::SpawnFriendTree(World, FTransform::Identity, FString(), TEXT("곰돌이")));
		TestFalse(TEXT("내 나무는 친구 나무가 아니다"), Tree != nullptr && Tree->IsFriendTree());
		TestFalse(TEXT("내 나무엔 이름표가 안 뜬다"), Tree != nullptr && Tree->IsLabelShown());
		if (Friend != nullptr && Tree != nullptr)
		{
			const TArray<AFruitTreeActor*> Both = { Tree, Friend };
			const FVector Here = Friend->GetActorLocation();
			TestTrue(TEXT("내 나무 찾기는 친구 나무를 건너뛴다"), AFruitTreeActor::PickNearest(Both, Here, 1.0e6f, false) == Tree);
			TestTrue(TEXT("친구 나무 곁 = 친구 나무만 고른다(흔들기)"), AFruitTreeActor::PickNearest(Both, Here, 1.0e6f, true) == Friend);
			TestNull(TEXT("친구 나무만 있으면 내 나무 요청 대상 없음"), AFruitTreeActor::PickNearest({ Friend }, Here, 1.0e6f, false));
			TestNull(TEXT("범위 밖이면 없음"), AFruitTreeActor::PickNearest(Both, Here + FVector(1.0e5f, 0, 0), 10.f, true));
		}
	}
	{
		// 손님 화면 친구 나무는 한 그루만 — 같은 방장이면 그대로, 다른 방장이면 갈아 세우고, 방장 id 가 비면 치운다.
		const auto CountFriends = [World]()
		{
			int32 Count = 0;
			for (TActorIterator<AFruitTreeActor> It(World); It; ++It)
			{
				Count += It->IsFriendTree() ? 1 : 0;
			}
			return Count;
		};
		AFruitTreeActor* const First = AFruitTreeActor::EnsureFriendTree(World, TEXT("HOSTONE"), TEXT("곰돌이"));
		TestNotNull(TEXT("방장 나무 세움"), First);
		TestTrue(TEXT("같은 방장이면 그대로"), AFruitTreeActor::EnsureFriendTree(World, TEXT("HOSTONE"), TEXT("곰돌이")) == First);
		TestEqual(TEXT("친구 나무 한 그루"), CountFriends(), 1);
		AFruitTreeActor* const Second = AFruitTreeActor::EnsureFriendTree(World, TEXT("HOSTTWO"), TEXT("토끼"));
		TestTrue(TEXT("다른 방장이면 새로"), Second != nullptr && Second != First && Second->GetFriendHostId() == TEXT("HOSTTWO"));
		TestEqual(TEXT("갈아 세워도 한 그루"), CountFriends(), 1);
		TestNull(TEXT("방장 id 비면 없음"), AFruitTreeActor::EnsureFriendTree(World, FString(), FString()));
		TestEqual(TEXT("방장 없으면 친구 나무 0"), CountFriends(), 0);
	}
	for (int32 Slot = 1; Slot < SlotCount; ++Slot)
	{
		TestTrue(TEXT("자리가 한 줄로 늘어선다"), ARanchGameMode::GetFruitTreeSpot(Slot).X > ARanchGameMode::GetFruitTreeSpot(Slot - 1).X);
	}
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenFruitTreeSyncTest, "Snarehaven.World.FruitTree.Sync",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenFruitTreeSyncTest::RunTest(const FString& Parameters)
{
	using namespace SnareFruitTree;
	TestEqual(TEXT("심기 함수"), FruitTreeSync::FunctionName(FruitTreeSync::EAction::Plant), FString(TEXT("PlantFruit")));
	TestEqual(TEXT("따기 함수"), FruitTreeSync::FunctionName(FruitTreeSync::EAction::Harvest), FString(TEXT("HarvestTree")));
	TestEqual(TEXT("베기 함수"), FruitTreeSync::FunctionName(FruitTreeSync::EAction::Chop), FString(TEXT("ChopTree")));
	const TSharedRef<FJsonObject> Args = FruitTreeSync::MakeArgs(2, TEXT("item.pear"));
	FString ItemID;
	Args->TryGetStringField(TEXT("itemId"), ItemID);
	TestEqual(TEXT("심기 인자 itemId"), ItemID, FString(TEXT("item.pear")));
	TestFalse(TEXT("따기 인자엔 itemId 가 없다"), FruitTreeSync::MakeArgs(1, NAME_None)->HasField(TEXT("itemId")));

	UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("시험 월드"), World))
	{
		return false;
	}
	AFruitTreeActor* const Tree = World->SpawnActor<AFruitTreeActor>();
	Tree->SlotIndex = 1;

	const TSharedPtr<FJsonObject> Reply = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Trees;
	Trees.Add(MakeShared<FJsonValueObject>(MakeTree(1, TEXT("item.peach"), TEXT("grown"), 2)));
	Reply->SetArrayField(TEXT("trees"), Trees);
	TestTrue(TEXT("받아들인 답은 입힌다"), FruitTreeSync::ApplyReply(World, true, Reply));
	TestEqual(TEXT("서버 열매 2개가 그려진다"), Tree->GetShownFruitCount(), 2);

	const TSharedPtr<FJsonObject> Refused = MakeShared<FJsonObject>();
	Refused->SetStringField(TEXT("error"), TEXT("거절"));
	Refused->SetStringField(TEXT("code"), TEXT("not_ready"));
	TestFalse(TEXT("거절 답은 화면을 안 바꾼다"), FruitTreeSync::ApplyReply(World, true, Refused));
	TestFalse(TEXT("네트워크 실패도 안 바꾼다"), FruitTreeSync::ApplyReply(World, false, nullptr));
	TestEqual(TEXT("거절 뒤에도 열매 그대로"), Tree->GetShownFruitCount(), 2);
	TestFalse(TEXT("백엔드 없는 월드는 요청을 안 보낸다"), FruitTreeSync::Request(World, FruitTreeSync::EAction::Harvest, 1));
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenFruitTreeFriendTest, "Snarehaven.World.FruitTree.Friend",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenFruitTreeFriendTest::RunTest(const FString& Parameters)
{
	using namespace SnareFruitTree;
	// 기준값은 서버 `nativeFruitOf` 를 node 로 돌린 결과(루프#938) — 클라 계산이 서버와 달라지면 이름표 과일이 거짓말이 된다.
	TestEqual(TEXT("ABC123 → 오렌지"), NativeFruitOf(TEXT("ABC123")), FName(TEXT("item.orange")));
	TestEqual(TEXT("7F3A9C01D2E45B68 → 자두"), NativeFruitOf(TEXT("7F3A9C01D2E45B68")), FName(TEXT("item.plum")));
	TestEqual(TEXT("host-one → 오렌지"), NativeFruitOf(TEXT("host-one")), FName(TEXT("item.orange")));
	TestEqual(TEXT("A1B2C3D4E5F60718 → 배"), NativeFruitOf(TEXT("A1B2C3D4E5F60718")), FName(TEXT("item.pear")));
	TestEqual(TEXT("이름표"), FriendTreeLabel(TEXT("곰돌이"), TEXT("item.peach")).ToString(), FString(TEXT("곰돌이의 복숭아나무")));
	TestEqual(TEXT("닉네임 없으면 친구"), FriendTreeLabel(FString(), TEXT("item.pear")).ToString(), FString(TEXT("친구의 배나무")));
	TestEqual(TEXT("요청 인자 hostId"), MakePickArgs(TEXT("ABC123"))->GetStringField(TEXT("hostId")), FString(TEXT("ABC123")));
	TestEqual(TEXT("서버 함수 이름"), FruitTreeSync::PickFriendFunctionName(), FString(TEXT("PickFriendFruit")));
	TestFalse(TEXT("월드 없으면 안 보냄"), FruitTreeSync::PickFriend(nullptr, TEXT("ABC123")));
	TestTrue(TEXT("5종 이름이 다 있다"), !FruitName(TEXT("item.cherry")).IsEmpty() && FruitName(TEXT("item.sword")).IsEmpty());
	return true;
}

#endif
