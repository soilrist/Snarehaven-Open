#include "Building/SnareBaseSubsystem.h"
#include "Snarehaven.h"
#include "Backend/SnareBackendSubsystem.h"
#include "Building/SnareBuildRules.h"
#include "Data/BaseRuleRow.h"
#include "Data/ItemRow.h"
#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"
#include "TimerManager.h"
#include "World/SnareSound.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "Snarehaven"

namespace
{
	const FName BaseRuleRowName(TEXT("Base"));

	/** 앞선 요청이 끝나길 기다렸다가 요리 완료를 다시 묻는 간격 (판정 숫자가 아니라 재시도 틈). */
	constexpr float CookRetryDelaySeconds = 0.5f;

	/** 서버가 오류로 답했나 — 오류도 HTTP 로는 성공(`{error, code}`)으로 온다. */
	bool IsServerError(const TSharedPtr<FJsonObject>& Response)
	{
		return !Response.IsValid() || Response->HasField(TEXT("error"));
	}

	FString ReadString(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
	{
		FString Value;
		if (Object.IsValid())
		{
			Object->TryGetStringField(Field, Value);
		}
		return Value;
	}

	int32 ReadInt(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, int32 Fallback)
	{
		double Value = 0.0;
		return Object.IsValid() && Object->TryGetNumberField(Field, Value) ? FMath::RoundToInt32(Value) : Fallback;
	}

	/** 텐트 존 — 존이 생기기 전(Rev 32 이전)에 친 텐트는 `zone` 이 없고 전부 거점존이다. */
	FName ReadZone(const TSharedPtr<FJsonObject>& Object)
	{
		const FString Zone = ReadString(Object, TEXT("zone"));
		return Zone.IsEmpty() ? SnareBaseZones::Ranch : FName(*Zone);
	}
}

USnareBaseSubsystem::USnareBaseSubsystem()
{
	// C++ 전용 프로젝트라 기본 표는 생성자 경로 참조로 잡는다 (UCodexScreenWidget 과 같은 방식).
	static ConstructorHelpers::FObjectFinder<UDataTable> ItemTableAsset(
		TEXT("/Game/Game/Data/Items/DT_ItemTable.DT_ItemTable"));
	if (ItemTableAsset.Succeeded())
	{
		ItemTable = ItemTableAsset.Object;
	}

	static ConstructorHelpers::FObjectFinder<UDataTable> RecipeTableAsset(
		TEXT("/Game/Game/Data/Crafting/DT_RecipeTable.DT_RecipeTable"));
	if (RecipeTableAsset.Succeeded())
	{
		RecipeTable = RecipeTableAsset.Object;
	}

	static ConstructorHelpers::FObjectFinder<UDataTable> BaseRuleTableAsset(
		TEXT("/Game/Game/Data/Balance/DT_BaseRuleTable.DT_BaseRuleTable"));
	if (BaseRuleTableAsset.Succeeded())
	{
		BaseRuleTable = BaseRuleTableAsset.Object;
	}
}

void USnareBaseSubsystem::Deinitialize()
{
	// 요리 타이머가 죽은 서브시스템을 부르지 않게 (engines/unreal.md 코드 규칙 7).
	if (UGameInstance* const GameInstance = GetGameInstance())
	{
		for (FSnareCookJob& Job : CookJobs)
		{
			GameInstance->GetTimerManager().ClearTimer(Job.TimerHandle);
		}
	}
	CookJobs.Reset();
	Super::Deinitialize();
}

void USnareBaseSubsystem::SetTablesForTest(UDataTable* InItemTable, UDataTable* InRecipeTable, UDataTable* InBaseRuleTable)
{
	ItemTable = InItemTable;
	RecipeTable = InRecipeTable;
	BaseRuleTable = InBaseRuleTable;
}

USnareBackendSubsystem* USnareBaseSubsystem::GetBackend() const
{
	if (IsValid(BackendOverride))
	{
		return BackendOverride;
	}
	const UGameInstance* const GameInstance = GetGameInstance();
	return GameInstance != nullptr ? GameInstance->GetSubsystem<USnareBackendSubsystem>() : nullptr;
}

const FBaseRuleRow* USnareBaseSubsystem::GetRules() const
{
	const FBaseRuleRow* const Row = IsValid(BaseRuleTable)
		? BaseRuleTable->FindRow<FBaseRuleRow>(BaseRuleRowName, TEXT("SnareBaseRules")) : nullptr;
	// 미정 칸이 하나라도 있으면 없는 것으로 친다 — 음수 규칙으로 미리보기를 칠하면 거짓 초록이 뜬다.
	return Row != nullptr && Row->IsComplete() ? Row : nullptr;
}

const FItemRow* USnareBaseSubsystem::FindItemRow(FName ItemID) const
{
	if (!IsValid(ItemTable) || ItemID.IsNone())
	{
		return nullptr;
	}
	for (const TPair<FName, uint8*>& Pair : ItemTable->GetRowMap())
	{
		const FItemRow* const Row = reinterpret_cast<const FItemRow*>(Pair.Value);
		if (Row != nullptr && Row->ItemID == ItemID)
		{
			return Row;
		}
	}
	return nullptr;
}

void USnareBaseSubsystem::PostMessage(const FText& Message, bool bError)
{
	LastMessage = Message;
	OnBaseMessage.Broadcast(Message, bError);
}

FText USnareBaseSubsystem::MessageForError(const FString& FunctionName, const TSharedPtr<FJsonObject>& Response)
{
	const FString Code = ReadString(Response, TEXT("code"));
	if (Code == TEXT("not_enough_items"))
	{
		const int32 Need = ReadInt(Response, TEXT("need"), -1);
		const int32 Have = ReadInt(Response, TEXT("have"), -1);
		return Need >= 0 && Have >= 0
			? FText::Format(LOCTEXT("BaseErrNotEnoughCount", "재료가 모자랍니다 (필요 {0}, 있음 {1})"), Need, Have)
			: LOCTEXT("BaseErrNotEnough", "재료가 모자랍니다");
	}
	if (Code == TEXT("locked") && FunctionName == TEXT("BoatStorage"))
	{
		return LOCTEXT("BaseErrBoatLocked", "배 짐칸이 있어야 배 보관함을 씁니다");
	}
	if (Code == TEXT("self")) { return LOCTEXT("BaseErrBoatSelf", "배 짐칸은 배 보관함에 넣을 수 없습니다"); }
	if (Code == TEXT("locked"))
	{
		return FunctionName.Contains(TEXT("Storage"))
			? LOCTEXT("BaseErrPin", "PIN 이 맞지 않습니다")
			: LOCTEXT("BaseErrLocked", "설계도나 작업대(텐트)가 없습니다");
	}
	if (Code == TEXT("occupied")) { return LOCTEXT("BaseErrOccupied", "다른 것과 겹칩니다"); }
	if (Code == TEXT("out_of_land")) { return LOCTEXT("BaseErrOutOfLand", "내 땅 밖에는 놓을 수 없습니다"); }
	if (Code == TEXT("limit")) { return LOCTEXT("BaseErrLimit", "더 놓을 수 없습니다 (한도)"); }
	if (Code == TEXT("inventory_full")) { return LOCTEXT("BaseErrInvFull", "가방이 가득 찼습니다"); }
	if (Code == TEXT("storage_full")) { return LOCTEXT("BaseErrStorageFull", "보관함이 가득 찼습니다"); }
	if (Code == TEXT("protected")) { return LOCTEXT("BaseErrProtected", "펫과 가챠 아이템은 넣을 수 없습니다"); }
	if (Code == TEXT("expired")) { return LOCTEXT("BaseErrExpired", "텐트가 사라졌습니다 (기한 또는 유지 끝)"); }
	if (Code == TEXT("not_found")) { return LOCTEXT("BaseErrNotFound", "대상을 찾지 못했습니다"); }
	if (Code == TEXT("not_empty")) { return LOCTEXT("BaseErrNotEmpty", "보관함을 먼저 비우세요"); }
	if (Code == TEXT("already"))
	{
		// 1층·2층 둘 다 `UpgradeHouse` 라 함수 이름으로는 못 가른다 — 둘에 다 맞는 글.
		return LOCTEXT("BaseErrAlready", "이미 올린 단계입니다");
	}
	if (Code == TEXT("same_color")) { return LOCTEXT("BaseErrSameColor", "이미 그 색입니다"); }
	if (Code == TEXT("full"))
	{
		return FunctionName == TEXT("FeedGenerator")
			? LOCTEXT("BaseErrGeneratorFull", "연료가 가득입니다")
			: LOCTEXT("BaseErrUpkeepFull", "유지가 가득입니다");
	}
	if (Code == TEXT("not_generator")) { return LOCTEXT("BaseErrNotGenerator", "발전기가 아닙니다"); }
	if (Code == TEXT("not_house"))
	{
		return FunctionName == TEXT("UpgradeHouse")
			? LOCTEXT("BaseErrNotHouseFloor2", "집이 된 뒤에 2층을 올릴 수 있습니다")
			: LOCTEXT("BaseErrNotHouse", "집이 된 뒤에 유지를 넣을 수 있습니다");
	}
	return LOCTEXT("BaseErrUnknown", "요청이 거절됐습니다");
}

FText USnareBaseSubsystem::MessageForBusy(const FString& FunctionName)
{
	// 무엇을 기다리는지 말해야 같은 버튼을 또 누르지 않는다. 오류가 아니라 "곧 된다" 는 안내다.
	if (FunctionName == TEXT("CraftItem"))
	{
		return LOCTEXT("BaseBusyCraft", "만드는 중입니다");
	}
	return LOCTEXT("BaseBusy", "앞선 요청에 답을 기다리는 중입니다");
}

bool USnareBaseSubsystem::Call(const FString& FunctionName, const TSharedPtr<FJsonObject>& Args, FSuccessHandler OnSuccess,
	bool bTellWhenBusy, const FString& MessageName)
{
	USnareBackendSubsystem* const Backend = GetBackend();
	if (bRequestInFlight)
	{
		// 말없이 돌아서면 누른 사람은 버튼이 죽은 줄 안다 (INBOX 2026-09-25 ④).
		if (bTellWhenBusy)
		{
			PostMessage(MessageForBusy(FunctionName), false);
		}
		return false;
	}
	if (!IsValid(Backend))
	{
		return false;
	}

	bRequestInFlight = true;
	TWeakObjectPtr<USnareBaseSubsystem> WeakThis(this);
	TWeakObjectPtr<USnareBackendSubsystem> WeakBackend(Backend);
	Backend->CallServerFunction(FunctionName, Args,
		[WeakThis, WeakBackend, FunctionName, MessageName, OnSuccess = MoveTemp(OnSuccess)](
			bool bOk, const TSharedPtr<FJsonObject>& Response, const FString& Error)
		{
			USnareBaseSubsystem* const Self = WeakThis.Get();
			if (Self == nullptr)
			{
				return;
			}
			Self->bRequestInFlight = false;

			if (!bOk)
			{
				UE_LOG(LogSnarehaven, Warning, TEXT("%s 왕복 실패: %s"), *FunctionName, *Error);
				Self->PostMessage(LOCTEXT("BaseErrNetwork", "서버에 닿지 못했습니다"), true);
				return;
			}
			if (IsServerError(Response))
			{
				UE_LOG(LogSnarehaven, Log, TEXT("%s 거절: code=%s"), *FunctionName, *ReadString(Response, TEXT("code")));
				Self->PostMessage(MessageForError(MessageName.IsEmpty() ? FunctionName : MessageName, Response), true);
				return;
			}

			UE_LOG(LogSnarehaven, Log, TEXT("%s 성공"), *FunctionName);
			if (OnSuccess)
			{
				OnSuccess(Response);
			}
			// 가방 숫자는 응답이 아니라 **백엔드에 다시 물어** 받는다 (CLAUDE.md 8).
			if (USnareBackendSubsystem* const BackendNow = WeakBackend.Get())
			{
				BackendNow->FetchPlayerState();
			}
			Self->OnBaseChanged.Broadcast();
		});
	return true;
}

bool USnareBaseSubsystem::RequestCraft(FName ResultItemID, int32 Times, bool bTellWhenBusy, ESnareCraftPlace Place)
{
	const TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
	Args->SetStringField(TEXT("recipeId"), ResultItemID.ToString());
	Args->SetNumberField(TEXT("times"), FMath::Max(1, Times));
	return Call(TEXT("CraftItem"), Args, [this, ResultItemID, Place](const TSharedPtr<FJsonObject>& Response)
	{
		const FName Name(*ReadString(Response, TEXT("crafted")));
		const FItemRow* const Row = FindItemRow(Name);
		PostMessage(FText::Format(LOCTEXT("BaseCrafted", "{0} x{1} 완성"),
			Row != nullptr ? Row->DisplayName : FText::FromName(Name), ReadInt(Response, TEXT("count"), 0)), false);
		// 소리·몸짓은 서버 확정 뒤에만 (M28-02) — 누름만으로는 아무것도 안 난다.
		OnCraftConfirmed.Broadcast(Name.IsNone() ? ResultItemID : Name, Place);
	}, bTellWhenBusy);
}

bool USnareBaseSubsystem::RequestPlaceTent(const FIntPoint& Cell, FName Zone)
{
	const TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
	Args->SetNumberField(TEXT("x"), Cell.X);
	Args->SetNumberField(TEXT("y"), Cell.Y);
	Args->SetStringField(TEXT("zone"), Zone.ToString());
	return Call(TEXT("PlaceTent"), Args, [this, Cell](const TSharedPtr<FJsonObject>& Response)
	{
		FSnareTentMirror Tent;
		Tent.TentId = ReadString(Response, TEXT("tentId"));
		Tent.Zone = ReadZone(Response);
		Tent.Cell = Cell;
		Tent.PlacedAtUtc = ReadString(Response, TEXT("placedAtUtc"));
		Tent.UpgradeDeadlineUtc = ReadString(Response, TEXT("upgradeDeadlineUtc"));
		Tents.Add(Tent);
		OnTentChanged.Broadcast(Tent);
		// 「쿵」은 서버가 세운 뒤에만 (M28-03) — 사본이 먼저 서야 그 자리를 찾는다.
		OnBuildConfirmed.Broadcast(Tent.TentId, FString());
		PostMessage(LOCTEXT("BaseTentPlaced", "텐트를 쳤습니다 - 기한 안에 집으로 올리세요"), false);
	});
}

bool USnareBaseSubsystem::RequestPlaceStructure(const FString& TentId, FName ItemID, const FIntPoint& Cell,
	int32 Rotation, const FString& Pin, const FIntPoint& Half, int32 WallSide, int32 WallHeightCm,
	const FString& TopHostId, int32 TopSlot, int32 Floor)
{
	const int32 Snapped = SnareBuildRules::SnapRotation(Rotation);
	// 2층 바닥은 바닥 가구·러그만 — 벽 장식·소품은 층을 안 보낸다(서버 `bad_args`, 소품은 받침 층을 따른다).
	const int32 PlaceFloor = (Floor >= 2 && WallSide < 0 && TopHostId.IsEmpty()) ? 2 : 1;
	const TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
	Args->SetStringField(TEXT("tentId"), TentId);
	Args->SetStringField(TEXT("itemId"), ItemID.ToString());
	Args->SetNumberField(TEXT("x"), Cell.X);
	Args->SetNumberField(TEXT("y"), Cell.Y);
	Args->SetNumberField(TEXT("rot"), Snapped);
	if (Half != FIntPoint::ZeroValue)
	{
		Args->SetNumberField(TEXT("hx"), Half.X);
		Args->SetNumberField(TEXT("hy"), Half.Y);
	}
	if (WallSide >= 0 && WallSide <= 3)
	{
		const TSharedRef<FJsonObject> Wall = MakeShared<FJsonObject>();
		Wall->SetNumberField(TEXT("side"), WallSide);
		Wall->SetNumberField(TEXT("h"), WallHeightCm);
		Args->SetObjectField(TEXT("wall"), Wall);
	}
	if (!TopHostId.IsEmpty())
	{
		const TSharedRef<FJsonObject> Top = MakeShared<FJsonObject>();
		Top->SetStringField(TEXT("host"), TopHostId);
		Top->SetNumberField(TEXT("slot"), TopSlot);
		Args->SetObjectField(TEXT("top"), Top);
	}
	if (PlaceFloor == 2)
	{
		Args->SetNumberField(TEXT("floor"), PlaceFloor);
	}
	if (!Pin.IsEmpty())
	{
		Args->SetStringField(TEXT("pin"), Pin);
	}
	return Call(TEXT("PlaceStructure"), Args, [this, ItemID, Cell, Half, Snapped, WallSide, WallHeightCm, TopHostId, TopSlot, PlaceFloor](const TSharedPtr<FJsonObject>& Response)
	{
		FSnareTentMirror* const Tent = FindTentMutable(ReadString(Response, TEXT("tentId")));
		if (Tent == nullptr)
		{
			return;
		}
		FSnareStructureMirror Structure;
		Structure.StructureId = ReadString(Response, TEXT("structureId"));
		Structure.ItemID = ItemID;
		Structure.Cell = Cell;
		Structure.Half = Half;
		Structure.Rotation = Snapped;
		Structure.WallSide = (WallSide >= 0 && WallSide <= 3) ? WallSide : -1;
		Structure.WallHeightCm = Structure.WallSide >= 0 ? WallHeightCm : 0;
		Structure.Floor = PlaceFloor;
		// 소품 — 서버는 자리·돌림·반 칸을 받침 것으로 저장한다(`PlaceStructure` 의 `top`). 사본도 받침 줄에서 옮긴다.
		if (!TopHostId.IsEmpty())
		{
			Structure.TopHostId = TopHostId;
			Structure.TopSlot = TopSlot;
			if (const FSnareStructureMirror* const Host = Tent->Structures.FindByPredicate(
				[&TopHostId](const FSnareStructureMirror& Other) { return Other.StructureId == TopHostId; }))
			{
				Structure.Cell = Host->Cell;
				Structure.Half = Host->Half;
				Structure.Rotation = Host->Rotation;
				Structure.Floor = Host->Floor;   // 서버도 받침 층을 적는다
			}
		}
		Tent->Structures.Add(Structure);
		OnStructurePlaced.Broadcast(*Tent, Structure);
		OnBuildConfirmed.Broadcast(Tent->TentId, Structure.StructureId);
	});
}

bool USnareBaseSubsystem::RequestRemoveStructure(const FString& TentId, const FString& StructureId)
{
	const TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
	Args->SetStringField(TEXT("tentId"), TentId);
	Args->SetStringField(TEXT("structureId"), StructureId);
	return Call(TEXT("RemoveStructure"), Args, [this, TentId, StructureId](const TSharedPtr<FJsonObject>& Response)
	{
		// 소리 자리를 잡으려면 물건이 아직 서 있어야 한다 — 지우기 전에 알린다 (M45-02).
		OnStructureEditConfirmed.Broadcast(TentId, StructureId, ESnareStructureEdit::Remove);
		if (FSnareTentMirror* const Tent = FindTentMutable(TentId))
		{
			Tent->Structures.RemoveAll([&StructureId](const FSnareStructureMirror& S) { return S.StructureId == StructureId; });
		}
		OnStructureRemoved.Broadcast(StructureId);
		PostMessage(LOCTEXT("BaseRemoved", "가방에 넣었습니다"), false);
	});
}

bool USnareBaseSubsystem::RequestUpgradeHouse(const FString& TentId, int32 Color, int32 Level)
{
	const TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
	Args->SetStringField(TEXT("tentId"), TentId);
	if (Level == 2)
	{
		Args->SetNumberField(TEXT("level"), 2);
		return Call(TEXT("UpgradeHouse"), Args, [this](const TSharedPtr<FJsonObject>& Response)
		{
			FSnareTentMirror* const Tent = FindTentMutable(ReadString(Response, TEXT("tentId")));
			if (Tent == nullptr)
			{
				return;
			}
			Tent->Floors = ReadInt(Response, TEXT("level"), 2);
			Tent->UpkeepUntilUtc = ReadString(Response, TEXT("upkeepUntilUtc"));
			OnTentChanged.Broadcast(*Tent);
			OnBuildConfirmed.Broadcast(Tent->TentId, FString());
			PostMessage(LOCTEXT("BaseFloor2", "2층을 올렸습니다"), false);
		});
	}
	Args->SetNumberField(TEXT("color"), Color);
	return Call(TEXT("UpgradeHouse"), Args, [this](const TSharedPtr<FJsonObject>& Response)
	{
		FSnareTentMirror* const Tent = FindTentMutable(ReadString(Response, TEXT("tentId")));
		if (Tent == nullptr)
		{
			return;
		}
		Tent->bUpgraded = true;
		Tent->Color = ReadInt(Response, TEXT("color"), 0);
		Tent->UpkeepUntilUtc = ReadString(Response, TEXT("upkeepUntilUtc"));
		OnTentChanged.Broadcast(*Tent);
		OnBuildConfirmed.Broadcast(Tent->TentId, FString());
		PostMessage(LOCTEXT("BaseUpgraded", "집이 됐습니다"), false);
	});
}

bool USnareBaseSubsystem::RequestPaintStructure(const FString& TentId, const FString& StructureId, FName PaintItemID)
{
	const TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
	Args->SetStringField(TEXT("tentId"), TentId);
	Args->SetStringField(TEXT("structureId"), StructureId);
	Args->SetStringField(TEXT("paintId"), PaintItemID.ToString());
	return Call(TEXT("PaintStructure"), Args, [this, TentId, StructureId](const TSharedPtr<FJsonObject>& Response)
	{
		FSnareTentMirror* const Tent = FindTentMutable(TentId);
		FSnareStructureMirror* const Structure = Tent != nullptr
			? Tent->Structures.FindByPredicate([&StructureId](const FSnareStructureMirror& S) { return S.StructureId == StructureId; })
			: nullptr;
		if (Structure == nullptr)
		{
			return;
		}
		Structure->Color = FName(*ReadString(Response, TEXT("color")));
		OnStructurePlaced.Broadcast(*Tent, *Structure); // 이미 선 액터는 그대로 두고 색만 바뀐다
		OnStructureEditConfirmed.Broadcast(TentId, StructureId, ESnareStructureEdit::Paint);
		PostMessage(LOCTEXT("BasePainted", "칠했습니다"), false);
	});
}

bool USnareBaseSubsystem::RequestFeedUpkeep(const FString& TentId, int32 Count)
{
	const TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
	Args->SetStringField(TEXT("tentId"), TentId);
	Args->SetNumberField(TEXT("count"), FMath::Max(1, Count));
	return Call(TEXT("FeedUpkeep"), Args, [this, TentId](const TSharedPtr<FJsonObject>& Response)
	{
		PlayStorageConfirmSound();   // 서버가 나무를 받아 준 뒤 — 사본에 텐트가 없어도 넣은 건 넣은 것
		FSnareTentMirror* const Tent = FindTentMutable(TentId);
		if (Tent == nullptr)
		{
			return;
		}
		Tent->UpkeepUntilUtc = ReadString(Response, TEXT("upkeepUntilUtc"));
		OnTentChanged.Broadcast(*Tent);
		PostMessage(FText::Format(LOCTEXT("BaseFed", "나무 {0}개를 유지에 넣었습니다"), ReadInt(Response, TEXT("used"), 0)), false);
	});
}

bool USnareBaseSubsystem::RequestFeedGenerator(const FString& TentId, const FString& StructureId, FName FuelItemID, int32 Count)
{
	const TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
	Args->SetStringField(TEXT("tentId"), TentId);
	Args->SetStringField(TEXT("structureId"), StructureId);
	Args->SetStringField(TEXT("fuel"), FuelItemID.ToString());
	Args->SetNumberField(TEXT("count"), FMath::Max(1, Count));
	return Call(TEXT("FeedGenerator"), Args, [this, TentId, StructureId, FuelItemID](const TSharedPtr<FJsonObject>& Response)
	{
		PlayStorageConfirmSound();   // 서버가 연료를 받아 준 뒤
		ApplyBackendNow(ReadString(Response, TEXT("nowUtc")));
		FSnareTentMirror* const Tent = FindTentMutable(TentId);
		FSnareStructureMirror* const Structure = Tent != nullptr
			? Tent->Structures.FindByPredicate([&StructureId](const FSnareStructureMirror& S) { return S.StructureId == StructureId; })
			: nullptr;
		if (Structure != nullptr)
		{
			Structure->FuelUntilUtc = ReadString(Response, TEXT("fuelUntilUtc"));
			OnStructurePlaced.Broadcast(*Tent, *Structure);   // 이미 선 발전기는 그대로 두고 연료만 바뀐다 (판은 `Call` 끝 `OnBaseChanged`)
		}
		const FItemRow* const FuelRow = FindItemRow(FuelItemID);
		PostMessage(FText::Format(LOCTEXT("BaseGeneratorFed", "{0} {1}개를 발전기에 넣었습니다"),
			FuelRow != nullptr && !FuelRow->DisplayName.IsEmpty() ? FuelRow->DisplayName : FText::FromName(FuelItemID),
			ReadInt(Response, TEXT("used"), 0)), false);
	});
}

bool USnareBaseSubsystem::RequestMoveToStorage(const FString& TentId, const FString& StructureId, FName ItemID,
	int32 Count, const FString& OwnerId, const FString& Pin)
{
	const TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
	Args->SetStringField(TEXT("tentId"), TentId);
	Args->SetStringField(TEXT("structureId"), StructureId);
	Args->SetStringField(TEXT("itemId"), ItemID.ToString());
	Args->SetNumberField(TEXT("count"), FMath::Max(1, Count));
	if (!OwnerId.IsEmpty())
	{
		Args->SetStringField(TEXT("ownerId"), OwnerId);
	}
	if (!Pin.IsEmpty())
	{
		Args->SetStringField(TEXT("pin"), Pin);
	}
	return Call(TEXT("MoveToStorage"), Args, [this, TentId, StructureId](const TSharedPtr<FJsonObject>& Response)
	{
		PlayStorageConfirmSound();
		ApplyStorageResponse(TentId, StructureId, Response);
	});
}

bool USnareBaseSubsystem::RequestMoveFromStorage(const FString& TentId, const FString& StructureId, FName ItemID,
	int32 Count, const FString& OwnerId, const FString& Pin)
{
	const TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
	Args->SetStringField(TEXT("tentId"), TentId);
	Args->SetStringField(TEXT("structureId"), StructureId);
	Args->SetStringField(TEXT("itemId"), ItemID.ToString());
	Args->SetNumberField(TEXT("count"), FMath::Max(1, Count));
	if (!OwnerId.IsEmpty())
	{
		Args->SetStringField(TEXT("ownerId"), OwnerId);
	}
	if (!Pin.IsEmpty())
	{
		Args->SetStringField(TEXT("pin"), Pin);
	}
	return Call(TEXT("MoveFromStorage"), Args, [this, TentId, StructureId](const TSharedPtr<FJsonObject>& Response)
	{
		PlayStorageConfirmSound();
		ApplyStorageResponse(TentId, StructureId, Response);
	});
}

FName USnareBaseSubsystem::GetBoatContainerItemID()
{
	return FName(TEXT("item.boat_container"));
}

int32 USnareBaseSubsystem::GetBoatStorageSlots() const
{
	const FItemRow* const Row = FindItemRow(GetBoatContainerItemID());
	return Row != nullptr ? FMath::Max(0, Row->BonusSlots) : 0;
}

bool USnareBaseSubsystem::RequestMoveToBoatStorage(FName ItemID, int32 Count)
{
	return CallBoatStorage(TEXT("MoveToStorage"), ItemID, Count);
}

bool USnareBaseSubsystem::RequestMoveFromBoatStorage(FName ItemID, int32 Count)
{
	return CallBoatStorage(TEXT("MoveFromStorage"), ItemID, Count);
}

bool USnareBaseSubsystem::CallBoatStorage(const FString& FunctionName, FName ItemID, int32 Count)
{
	// 놓인 보관함과 같은 서버 함수 — `boat: true` 면 텐트·가구 ID 없이 계정의 배 보관함을 연다 (M32-02b).
	const TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
	Args->SetBoolField(TEXT("boat"), true);
	Args->SetStringField(TEXT("itemId"), ItemID.ToString());
	Args->SetNumberField(TEXT("count"), FMath::Max(1, Count));
	return Call(FunctionName, Args, [this](const TSharedPtr<FJsonObject>& Response)
	{
		PlayStorageConfirmSound();
		const TSharedPtr<FJsonObject>* StorageObject = nullptr;
		if (Response.IsValid() && Response->TryGetObjectField(TEXT("storage"), StorageObject) && StorageObject != nullptr)
		{
			ApplyBackendBoatStorage(*StorageObject);
		}
	}, true, TEXT("BoatStorage"));
}

void USnareBaseSubsystem::ApplyBackendBoatStorage(const TSharedPtr<FJsonObject>& StorageObject)
{
	if (!StorageObject.IsValid())
	{
		return;
	}
	BoatStorage.Reset();
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : StorageObject->Values)
	{
		const int32 Count = Pair.Value.IsValid() ? FMath::RoundToInt32(Pair.Value->AsNumber()) : 0;
		if (Count > 0)
		{
			BoatStorage.Add(FName(*Pair.Key), Count);
		}
	}
	bBoatStorageKnown = true;
	OnBaseChanged.Broadcast();
}

void USnareBaseSubsystem::PlayStorageConfirmSound()
{
	// 판 안 일이라 내 귀에만 (DESIGN 2.2 「거점·상자 소리」 누가 듣나). 스냅샷 되살리기(`ApplyStorageResponse`)에서는 안 부른다.
	LastStorageSoundCue = SnareBuildRules::StorageConfirmCue();
	++StorageSoundCount;
	USnareSoundSubsystem::Play2D(this, LastStorageSoundCue, SnareBuildRules::StorageConfirmVolume());
}

void USnareBaseSubsystem::ApplyStorageResponse(const FString& TentId, const FString& StructureId,
	const TSharedPtr<FJsonObject>& Response)
{
	// 게스트가 연 남의 보관함은 사본에 텐트가 없다 — 그럴 땐 따로 둔다(같은 구조체, 텐트 ID 로만 묶임).
	FSnareTentMirror* Tent = FindTentMutable(TentId);
	if (Tent == nullptr)
	{
		FSnareTentMirror& Foreign = Tents.AddDefaulted_GetRef();
		Foreign.TentId = TentId;
		Tent = &Foreign;
	}
	FSnareStructureMirror* Structure = Tent->Structures.FindByPredicate(
		[&StructureId](const FSnareStructureMirror& S) { return S.StructureId == StructureId; });
	if (Structure == nullptr)
	{
		Structure = &Tent->Structures.AddDefaulted_GetRef();
		Structure->StructureId = StructureId;
	}

	Structure->Storage.Reset();
	const TSharedPtr<FJsonObject>* StorageObject = nullptr;
	if (Response->TryGetObjectField(TEXT("storage"), StorageObject) && StorageObject != nullptr)
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*StorageObject)->Values)
		{
			const int32 Count = Pair.Value.IsValid() ? FMath::RoundToInt32(Pair.Value->AsNumber()) : 0;
			if (Count > 0)
			{
				Structure->Storage.Add(FName(*Pair.Key), Count);
			}
		}
	}
}

void USnareBaseSubsystem::ApplyBackendTents(const TArray<TSharedPtr<FJsonValue>>& TentValues)
{
	const FBaseRuleRow* const Rules = GetRules();
	Tents.Reset();
	for (const TSharedPtr<FJsonValue>& TentValue : TentValues)
	{
		const TSharedPtr<FJsonObject> TentObject = TentValue.IsValid() ? TentValue->AsObject() : nullptr;
		if (!TentObject.IsValid())
		{
			continue;
		}
		FSnareTentMirror& Tent = Tents.AddDefaulted_GetRef();
		Tent.TentId = ReadString(TentObject, TEXT("tentId"));
		Tent.Zone = ReadZone(TentObject);
		Tent.Cell = FIntPoint(ReadInt(TentObject, TEXT("x"), 0), ReadInt(TentObject, TEXT("y"), 0));
		Tent.PlacedAtUtc = ReadString(TentObject, TEXT("placedAtUtc"));
		TentObject->TryGetBoolField(TEXT("upgraded"), Tent.bUpgraded);
		Tent.Color = ReadInt(TentObject, TEXT("color"), 0);
		Tent.UpkeepUntilUtc = ReadString(TentObject, TEXT("upkeepUntilUtc"));
		Tent.Floors = FMath::Max(1, ReadInt(TentObject, TEXT("floors"), 1));

		// 서버는 기한을 저장하지 않고 `PlaceTent` 응답 때만 placedAt + 표의 시간으로 셈해 준다 — 표시용으로 같은 셈을 한다.
		FDateTime PlacedAt;
		if (!Tent.bUpgraded && Rules != nullptr && Rules->UpgradeDeadlineHours > 0
			&& FDateTime::ParseIso8601(*Tent.PlacedAtUtc, PlacedAt))
		{
			Tent.UpgradeDeadlineUtc = (PlacedAt + FTimespan::FromHours(Rules->UpgradeDeadlineHours)).ToIso8601();
		}

		const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
		if (!TentObject->TryGetArrayField(TEXT("items"), Items) || Items == nullptr)
		{
			continue;
		}
		for (const TSharedPtr<FJsonValue>& ItemValue : *Items)
		{
			const TSharedPtr<FJsonObject> ItemObject = ItemValue.IsValid() ? ItemValue->AsObject() : nullptr;
			if (!ItemObject.IsValid())
			{
				continue;
			}
			FSnareStructureMirror& Structure = Tent.Structures.AddDefaulted_GetRef();
			Structure.StructureId = ReadString(ItemObject, TEXT("structureId"));
			Structure.ItemID = FName(*ReadString(ItemObject, TEXT("itemId")));
			Structure.Cell = FIntPoint(ReadInt(ItemObject, TEXT("x"), 0), ReadInt(ItemObject, TEXT("y"), 0));
			Structure.Half = FIntPoint(FMath::Clamp(ReadInt(ItemObject, TEXT("hx"), 0), 0, 1), FMath::Clamp(ReadInt(ItemObject, TEXT("hy"), 0), 0, 1));
			Structure.Rotation = ReadInt(ItemObject, TEXT("rot"), 0);
			const TSharedPtr<FJsonObject>* WallObject = nullptr;
			if (ItemObject->TryGetObjectField(TEXT("wall"), WallObject) && WallObject != nullptr && WallObject->IsValid())
			{
				Structure.WallSide = FMath::Clamp(ReadInt(*WallObject, TEXT("side"), 0), 0, 3);
				Structure.WallHeightCm = ReadInt(*WallObject, TEXT("h"), 0);
			}
			const TSharedPtr<FJsonObject>* TopObject = nullptr;
			if (ItemObject->TryGetObjectField(TEXT("top"), TopObject) && TopObject != nullptr && TopObject->IsValid())
			{
				Structure.TopHostId = ReadString(*TopObject, TEXT("host"));
				Structure.TopSlot = ReadInt(*TopObject, TEXT("slot"), -1);
			}
			Structure.Floor = FMath::Clamp(ReadInt(ItemObject, TEXT("floor"), 1), 1, 2);
			Structure.Color = FName(*ReadString(ItemObject, TEXT("color")));
			Structure.FuelUntilUtc = ReadString(ItemObject, TEXT("fuelUntilUtc"));
			// 보관함 속은 `MoveToStorage` 응답과 같은 `storage` 모양이라 같은 해석기를 태운다.
			if (ItemObject->HasField(TEXT("storage")))
			{
				ApplyStorageResponse(Tent.TentId, Structure.StructureId, ItemObject);
			}
		}
	}

	for (const FSnareTentMirror& Tent : Tents)
	{
		OnTentChanged.Broadcast(Tent);
		for (const FSnareStructureMirror& Structure : Tent.Structures)
		{
			OnStructurePlaced.Broadcast(Tent, Structure);
		}
	}
	OnBaseChanged.Broadcast();
	UE_LOG(LogSnarehaven, Log, TEXT("[Base] 서버 저장값으로 텐트 %d개를 되살렸다"), Tents.Num());
}

void USnareBaseSubsystem::ApplyBackendNow(const FString& NowUtc)
{
	FDateTime Parsed;
	if (NowUtc.IsEmpty() || !FDateTime::ParseIso8601(*NowUtc, Parsed))
	{
		return;   // 옛 서버 답 — 기기 UTC 로 잰다
	}
	BackendNowUtc = Parsed;
	BackendNowReceivedAt = FPlatformTime::Seconds();
}

double USnareBaseSubsystem::FuelSecondsLeft(const FString& FuelUntilUtc) const
{
	FDateTime Until;
	if (FuelUntilUtc.IsEmpty() || !FDateTime::ParseIso8601(*FuelUntilUtc, Until))
	{
		return 0.0;
	}
	// 지금 서버 시각 = 받은 서버 시각 + 그 뒤 흐른 시간. 받은 적 없으면 기기 UTC.
	const FDateTime Now = BackendNowReceivedAt >= 0.0
		? BackendNowUtc + FTimespan::FromSeconds(FPlatformTime::Seconds() - BackendNowReceivedAt)
		: FDateTime::UtcNow();
	return FMath::Max((Until - Now).GetTotalSeconds(), 0.0);
}

FSnareTentMirror* USnareBaseSubsystem::FindTentMutable(const FString& TentId)
{
	return Tents.FindByPredicate([&TentId](const FSnareTentMirror& T) { return T.TentId == TentId; });
}

const FSnareTentMirror* USnareBaseSubsystem::FindTent(const FString& TentId) const
{
	return Tents.FindByPredicate([&TentId](const FSnareTentMirror& T) { return T.TentId == TentId; });
}

const FSnareTentMirror* USnareBaseSubsystem::FindTentOwningCell(const FIntPoint& Cell, FName Zone) const
{
	const FBaseRuleRow* const Rules = GetRules();
	if (Rules == nullptr)
	{
		return nullptr;
	}
	return Tents.FindByPredicate([&Cell, Zone, Rules](const FSnareTentMirror& T)
	{
		// 게스트가 연 남의 보관함 자리(칸 없음)는 내 땅이 아니다.
		return !T.PlacedAtUtc.IsEmpty() && T.Zone == Zone && SnareBuildRules::IsInsideLand(Cell, T.Cell, Rules->LandCells);
	});
}

const FSnareStructureMirror* USnareBaseSubsystem::FindStructure(const FString& StructureId, FString* OutTentId) const
{
	for (const FSnareTentMirror& Tent : Tents)
	{
		for (const FSnareStructureMirror& Structure : Tent.Structures)
		{
			if (Structure.StructureId == StructureId)
			{
				if (OutTentId != nullptr)
				{
					*OutTentId = Tent.TentId;
				}
				return &Structure;
			}
		}
	}
	return nullptr;
}

bool USnareBaseSubsystem::IsOwnFurniture(const FString& TentId, const FString& StructureId) const
{
	const FSnareTentMirror* const Tent = FindTent(TentId);
	return Tent != nullptr && !Tent->PlacedAtUtc.IsEmpty() && !StructureId.IsEmpty()
		&& Tent->Structures.ContainsByPredicate([&StructureId](const FSnareStructureMirror& S) { return S.StructureId == StructureId; });
}

ESnarePlacementVerdict USnareBaseSubsystem::EvaluatePlacement(FName ItemID, const FIntPoint& Cell, int32 Rotation,
	bool bInBaseZone, FName Zone, const FIntPoint& Half, int32 Floor) const
{
	const FBaseRuleRow* const Rules = GetRules();
	const FItemRow* const Row = FindItemRow(ItemID);
	if (Rules == nullptr || Row == nullptr || !bInBaseZone || Row->ItemType != EItemType::Placeable)
	{
		return ESnarePlacementVerdict::Blue;
	}

	if (Row->PlaceableFunction == EPlaceableFunction::Tent)
	{
		TArray<FIntPoint> OwnTentCells;
		for (const FSnareTentMirror& Tent : Tents)
		{
			if (!Tent.PlacedAtUtc.IsEmpty() && Tent.Zone == Zone)
			{
				OwnTentCells.Add(Tent.Cell);
			}
		}
		// 겹침은 같은 존끼리, 한도는 존을 가리지 않는다 (서버 `PlaceTent` 와 같은 줄) — 다른 존 텐트를 한도 몫으로 채운다.
		const int32 OtherZoneTents = Tents.FilterByPredicate([Zone](const FSnareTentMirror& T)
		{
			return !T.PlacedAtUtc.IsEmpty() && T.Zone != Zone;
		}).Num();
		return SnareBuildRules::EvaluateTent(Cell, true, OwnTentCells, Rules->TentMax - OtherZoneTents, Rules->LandCells);
	}

	const FSnareTentMirror* const Tent = FindTentOwningCell(Cell, Zone);
	if (Tent == nullptr || !SnareBuildRules::HasFurnitureRoom(Tent->GetFurnitureLoad(), *Rules, Tent->Floors))
	{
		return ESnarePlacementVerdict::Blue;
	}

	// 겹침은 텐트 몸과 이미 놓인 가구의 발자국 전체로 본다 — 반 칸(50cm) 단위로 센다(세밀 격자).
	TSet<FIntPoint> Occupied;
	TArray<FIntPoint> Units;
	FIntPoint TentSize(1, 1);
	for (const TPair<FName, uint8*>& Pair : ItemTable->GetRowMap())
	{
		// 텐트 발자국은 ID 를 적지 않고 기능(Tent) 행에서 읽는다.
		const FItemRow* const Candidate = reinterpret_cast<const FItemRow*>(Pair.Value);
		if (Candidate != nullptr && Candidate->PlaceableFunction == EPlaceableFunction::Tent)
		{
			TentSize = SnareBuildRules::RotatedFootprint(Candidate->FootprintX, Candidate->FootprintY, 0);
			break;
		}
	}
	const FIntPoint Size = SnareBuildRules::RotatedFootprint(Row->FootprintX, Row->FootprintY, Rotation);
	const auto FootprintOf = [this](const FSnareStructureMirror& Structure, TArray<FIntPoint>& OutUnits)
	{
		const FItemRow* const StructureRow = FindItemRow(Structure.ItemID);
		SnareBuildRules::FootprintHalfUnits(SnareBuildRules::JoinHalfUnits(Structure.Cell, Structure.Half), StructureRow != nullptr
			? SnareBuildRules::RotatedFootprint(StructureRow->FootprintX, StructureRow->FootprintY, Structure.Rotation)
			: FIntPoint(1, 1), OutUnits);
	};
	if (Floor == 2)
	{
		// 2층 — 서버 `floor2PlaceFail` 과 같은 줄. 계단·벽 장식·소품은 2층으로 묻지 않는다(서버 `bad_args`).
		const FSnareStructureMirror* const Stairs = Tent->Structures.FindByPredicate([this](const FSnareStructureMirror& S)
		{
			const FItemRow* const StructureRow = FindItemRow(S.ItemID);
			return StructureRow != nullptr && StructureRow->PlaceableFunction == EPlaceableFunction::Stairs;
		});
		if (!Tent->bUpgraded || Tent->Floors != 2 || Stairs == nullptr || Row->PlaceableFunction == EPlaceableFunction::Stairs)
		{
			return ESnarePlacementVerdict::Blue;
		}
		TArray<FIntPoint> HoleUnits;
		FootprintOf(*Stairs, HoleUnits);
		// 발자국 전부가 집 안 3×3(텐트 발자국)이어야 한다 — 밖으로 삐져나오면 판 없는 허공에 뜬다.
		const FIntPoint HouseMin = Tent->Cell * 2;
		const FIntPoint HouseMax = (Tent->Cell + TentSize) * 2;
		SnareBuildRules::FootprintHalfUnits(SnareBuildRules::JoinHalfUnits(Cell, Half), Size, Units);
		for (const FIntPoint& Unit : Units)
		{
			if (Unit.X < HouseMin.X || Unit.Y < HouseMin.Y || Unit.X >= HouseMax.X || Unit.Y >= HouseMax.Y || HoleUnits.Contains(Unit))
			{
				return ESnarePlacementVerdict::Blue;
			}
		}
	}
	else if (!Tent->bUpgraded)
	{
		// 텐트 몸만 막는다 — 집 1층 안은 걸어 들어가 꾸미는 방이다(서버 `PlaceStructure` 도 집 몸을 안 센다, M108-06f-4).
		SnareBuildRules::FootprintHalfUnits(SnareBuildRules::JoinHalfUnits(Tent->Cell, FIntPoint::ZeroValue), TentSize, Units);
		Occupied.Append(Units);
	}
	// 러그와 바닥 가구는 서로 겹쳐도 된다 — 러그는 러그끼리만 센다 (서버 `RUG_ITEMS`, DESIGN 8.11 「러그 깔기」).
	const bool bRug = Row->PlaceableFunction == EPlaceableFunction::Rug;
	for (const FSnareStructureMirror& Structure : Tent->Structures)
	{
		const FItemRow* const StructureRow = FindItemRow(Structure.ItemID);
		// 층이 다르면 같은 칸이어도 안 겹친다 (서버 `PlaceStructure` 같은 줄).
		if (Structure.Floor != Floor || bRug != (StructureRow != nullptr && StructureRow->PlaceableFunction == EPlaceableFunction::Rug))
		{
			continue;
		}
		FootprintOf(Structure, Units);
		Occupied.Append(Units);
	}

	return SnareBuildRules::EvaluateStructureHalf(SnareBuildRules::JoinHalfUnits(Cell, Half), Size, &Tent->Cell, Rules->LandCells, Occupied);
}

ESnarePlacementVerdict USnareBaseSubsystem::EvaluateWallPlacement(const FString& TentId, const FIntPoint& Cell, int32 Side,
	int32 HeightCm, int32 HouseCells) const
{
	const FBaseRuleRow* const Rules = GetRules();
	const FSnareTentMirror* const Tent = FindTent(TentId);
	if (Rules == nullptr || Tent == nullptr || !SnareBuildRules::HasFurnitureRoom(Tent->GetFurnitureLoad(), *Rules, Tent->Floors))
	{
		return ESnarePlacementVerdict::Blue;
	}
	TArray<SnareBuildRules::FWallHung> Hung;
	for (const FSnareStructureMirror& Structure : Tent->Structures)
	{
		if (Structure.WallSide >= 0)
		{
			Hung.Add({ Structure.Cell, Structure.WallSide, Structure.WallHeightCm });
		}
	}
	return SnareBuildRules::EvaluateWallHang(Cell, Side, HeightCm, Tent->Cell, HouseCells, Tent->bUpgraded, Hung);
}

ESnarePlacementVerdict USnareBaseSubsystem::EvaluateTopPlacement(const FString& TentId, const FString& HostId, int32 Slot,
	int32 SlotCount) const
{
	const FBaseRuleRow* const Rules = GetRules();
	const FSnareTentMirror* const Tent = FindTent(TentId);
	if (Rules == nullptr || Tent == nullptr || HostId.IsEmpty()
		|| !SnareBuildRules::HasFurnitureRoom(Tent->GetFurnitureLoad(), *Rules, Tent->Floors)
		|| !Tent->Structures.ContainsByPredicate([&HostId](const FSnareStructureMirror& Other) { return Other.StructureId == HostId; }))
	{
		return ESnarePlacementVerdict::Blue;
	}
	TArray<int32> Taken;
	for (const FSnareStructureMirror& Structure : Tent->Structures)
	{
		if (Structure.TopHostId == HostId)
		{
			Taken.Add(Structure.TopSlot);
		}
	}
	return SnareBuildRules::EvaluateTopPlace(Slot, SlotCount, Taken);
}

float USnareBaseSubsystem::GetCookSeconds(FName FireItemID) const
{
	const FItemRow* const Row = FindItemRow(FireItemID);
	return Row != nullptr && Row->CookSeconds > 0 ? static_cast<float>(Row->CookSeconds) : -1.f;
}

const FSnareCookJob* USnareBaseSubsystem::FindCookJob(const FString& FireKey) const
{
	return CookJobs.FindByPredicate([&FireKey](const FSnareCookJob& Job) { return Job.FireKey == FireKey; });
}

float USnareBaseSubsystem::GetCookRemainingSeconds(const FString& FireKey) const
{
	const FSnareCookJob* const Job = FindCookJob(FireKey);
	if (Job == nullptr)
	{
		return -1.f;
	}
	return FMath::Max(0.f, Job->DurationSeconds - static_cast<float>(FPlatformTime::Seconds() - Job->StartedAtSeconds));
}

bool USnareBaseSubsystem::StartCooking(const FString& FireKey, FName FireItemID, FName ResultItemID)
{
	const float Seconds = GetCookSeconds(FireItemID);
	if (FireKey.IsEmpty() || Seconds <= 0.f)
	{
		return false;
	}
	if (FindCookJob(FireKey) != nullptr)
	{
		// 같은 불에 또 올리려 한 것 — 남은 시간은 화면이 따로 센다, 여기선 왜 안 올라갔는지만 말한다.
		PostMessage(LOCTEXT("BaseBusyCook", "이 불은 이미 굽는 중입니다"), false);
		return false;
	}

	FSnareCookJob& Job = CookJobs.AddDefaulted_GetRef();
	Job.FireKey = FireKey;
	Job.FireItemID = FireItemID;
	Job.ResultItemID = ResultItemID;
	Job.DurationSeconds = Seconds;
	Job.StartedAtSeconds = FPlatformTime::Seconds();

	// 게임 인스턴스 타이머라 맵을 옮겨도 계속 돈다 (DESIGN 8.10 "불 옆을 떠나도 계속").
	if (UGameInstance* const GameInstance = GetGameInstance())
	{
		FTimerDelegate Done = FTimerDelegate::CreateWeakLambda(this, [this, FireKey]() { CompleteCooking(FireKey); });
		GameInstance->GetTimerManager().SetTimer(Job.TimerHandle, Done, Seconds, false);
	}

	const FItemRow* const Result = FindItemRow(ResultItemID);
	PostMessage(FText::Format(LOCTEXT("BaseCookStart", "{0} 굽는 중 ({1}초)"),
		Result != nullptr ? Result->DisplayName : FText::FromName(ResultItemID), FMath::RoundToInt32(Seconds)), false);
	UE_LOG(LogSnarehaven, Log, TEXT("요리 시작: 불=%s 결과=%s %.0f초"), *FireItemID.ToString(), *ResultItemID.ToString(), Seconds);
	OnBaseChanged.Broadcast();
	return true;
}

void USnareBaseSubsystem::CompleteCooking(const FString& FireKey)
{
	const int32 Index = CookJobs.IndexOfByPredicate([&FireKey](const FSnareCookJob& Job) { return Job.FireKey == FireKey; });
	if (Index == INDEX_NONE)
	{
		return;
	}

	// 다른 요청이 날아가 있으면 잠깐 뒤 다시 — 요리는 버려지면 안 된다.
	// 사람이 누른 것이 아니므로 안내는 띄우지 않는다 (누르지도 않은 「만드는 중입니다」가 뜬다).
	const FItemRow* const FireRow = FindItemRow(CookJobs[Index].FireItemID);
	const ESnareCraftPlace Place = FireRow != nullptr && FireRow->PlaceableFunction == EPlaceableFunction::Smelter
		? ESnareCraftPlace::Smelter : ESnareCraftPlace::Fire;
	if (!RequestCraft(CookJobs[Index].ResultItemID, 1, false, Place))
	{
		if (UGameInstance* const GameInstance = GetGameInstance())
		{
			FTimerDelegate Retry = FTimerDelegate::CreateWeakLambda(this, [this, FireKey]() { CompleteCooking(FireKey); });
			GameInstance->GetTimerManager().SetTimer(CookJobs[Index].TimerHandle, Retry, CookRetryDelaySeconds, false);
		}
		return;
	}

	if (UGameInstance* const GameInstance = GetGameInstance())
	{
		GameInstance->GetTimerManager().ClearTimer(CookJobs[Index].TimerHandle);
	}
	CookJobs.RemoveAt(Index);
	OnBaseChanged.Broadcast();
}

#undef LOCTEXT_NAMESPACE
