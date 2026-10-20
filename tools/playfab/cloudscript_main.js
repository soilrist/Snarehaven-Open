// PlayFab CloudScript (Legacy) — Snarehaven MVP
// 콘솔: Automation > Cloud Script > Revisions (Legacy) > Upload new revision 에 이 파일을 그대로 올린다.
// 스펙 근거: docs/BACKEND.md CloudScript 함수 표, docs/DESIGN.md 10.1-10.3, docs/GACHA_DISCLOSURE.md

var RANCH_SLOT_COUNT = 5;
var HARVEST_INTERVAL_MINUTES = 30;
var HARVEST_CAP_UNITS = 5;
// M21-01 계정의 첫 펫 첫 생산만 1분 — 첫 접속 5분 안에 수확까지 닿게 (DESIGN 3.2 확정 수치). 그 뒤는 30분.
var FIRST_BATCH_MINUTES = 1;
// M21-07b 변종 펫 칸은 주기 ÷1.5(30 → 20분) — 생산물·개수는 같은 종 그대로 (DESIGN 10.6 「변종 생산」).
var VARIANT_CYCLE_DIVISOR = 1.5;
var COIN_PER_UNIT = 10;
// M109-04a 목장 동물·낚시터 수확 코인을 합쳐 하루(서버 UTC `questDay()`) 상한 — 넘으면 생산물·명성은 그대로 주고
// 코인만 남은 몫까지(DESIGN 11 「수확 코인 하루 상한」 🟡). 밭 수확(HarvestCrop)은 씨앗 값이 싱크라 뺀다.
var HARVEST_DAILY_COINS = 900;
var HARVEST_COIN_DATA_KEY = "harvestCoins"; // {day, coins}
// M21-06b 같은 방 2인 이상이 함께인 동안 동물 칸 생산 주기 ×0.9(30 → 27분). 함께는 각자 자기 계정으로 보낸
// `ReportCoop` 를 서버가 서로 맞대 센다 — 호스트가 보낸 인원 수는 믿지 않는다. 값은 DESIGN 3.2 확정 수치에서 옮긴다.
var COOP_MIN_PLAYERS = 2;
var COOP_CYCLE_FACTOR = 0.9;
var COOP_FRESH_SECONDS = 120;
var COOP_MAX_PEERS = 7; // 방 최대 8명(DESIGN 4.1 `NumPublicConnections`) − 나
var COOP_DATA_KEY = "coop";

// DESIGN.md 3.2 목장 생산물 — 클라이언트 `DT_RanchProductTable` 16행과 같아야 한다
// (2층 `Snarehaven.Ranch.Table.MatchesServer` 가 이 표를 읽어 맞댄다). 값을 손으로 고치지 말고 DESIGN 3.2 에서 옮긴다.
var ANIMAL_PRODUCT_ITEM = {
    "animal.pig": "item.truffle",
    "animal.cow": "item.milk",
    "animal.sheep": "item.wool",
    "animal.fox": "item.fox_fur",
    "animal.goat": "item.milk",
    "animal.reindeer": "item.shed_antler",
    "animal.elk": "item.shed_antler",
    "animal.lynx": "item.lynx_fur",
    "animal.wolf": "item.wolf_fur",
    "animal.wolverine": "item.wolverine_fur",
    "animal.bear": "item.honey",
    "animal.pheasant": "item.egg",
    "animal.peacock": "item.egg",
    "animal.ptarmigan": "item.egg",
    "animal.capercaillie": "item.egg",
    "animal.fogbuck": "item.shed_antler" // 이야기 종 안개뿔 — 도감·변종엔 안 든다(아래 STORY_ANIMALS)
};
// 아래 `CODEX_CATEGORIES.variant` 가 읽으므로 표 바로 뒤에 둔다.
// M21-07 색 변종 펫 — 바르코 12종 × 1 (DESIGN 10.6, 자리표시 돼지·소·양 제외). 받기·목장·도감 칸은 M21-07b.
var PLACEHOLDER_ANIMALS = ["animal.pig", "animal.cow", "animal.sheep"];
var STORY_ANIMALS = ["animal.fogbuck"];
var VARIANT_PET_ITEMS = Object.keys(ANIMAL_PRODUCT_ITEM).filter(function (a) { return PLACEHOLDER_ANIMALS.indexOf(a) === -1 && STORY_ANIMALS.indexOf(a) === -1; })
    .map(function (a) { return "item.pet_" + a.slice("animal.".length) + "_variant"; });
// M21-07g 변종 확률 — **고지·맞대기용 사본**이다. 굴림은 호스트가 스폰 때(`DT_AnimalTable` `VariantChance`) 하고
// 서버는 `ClaimLoot` 한도로 받는다. 서버 = 클라 표 = DESIGN 10.6 「변종 확률」 을 2층이 글자로 맞댄다.
var VARIANT_CHANCE = 0.03;

// DESIGN.md 8.4 낚시(어업) 확정 수치 — 어종 21종 (사용자 지시 2026-09-23)
// 규칙: intervalMinutes = 레퍼런스 조리 회복량 x 2 (3의 배수), coinPerUnit = intervalMinutes / 3.
// 21종 전부 분당 코인 0.33 (8.2 작물과 같은 축). 값을 손으로 고치지 말고 DESIGN 8.4 표에서 옮긴다.
var FISHING_SLOT_COUNT = 5;
var FISHING_CAP_UNITS = 5;
var FISH_TABLE = {
    "item.crucian_carp": { intervalMinutes: 15, coinPerUnit: 5 },
    "item.salmon": { intervalMinutes: 21, coinPerUnit: 7 },
    "item.cave_crab": { intervalMinutes: 27, coinPerUnit: 9 },
    "item.carp": { intervalMinutes: 30, coinPerUnit: 10 },
    "item.perch": { intervalMinutes: 30, coinPerUnit: 10 },
    "item.pike": { intervalMinutes: 51, coinPerUnit: 17 },
    "item.whitefish": { intervalMinutes: 60, coinPerUnit: 20 },
    "item.haddock": { intervalMinutes: 60, coinPerUnit: 20 },
    "item.cod": { intervalMinutes: 60, coinPerUnit: 20 },
    "item.catfish": { intervalMinutes: 60, coinPerUnit: 20 },
    "item.trout": { intervalMinutes: 69, coinPerUnit: 23 },
    "item.tuna": { intervalMinutes: 69, coinPerUnit: 23 },
    "item.stingray": { intervalMinutes: 99, coinPerUnit: 33 },
    "item.rainbow_trout": { intervalMinutes: 120, coinPerUnit: 40 },
    "item.redfish": { intervalMinutes: 120, coinPerUnit: 40 },
    "item.black_grouper": { intervalMinutes: 120, coinPerUnit: 40 },
    "item.arctic_char": { intervalMinutes: 141, coinPerUnit: 47 },
    "item.swordfish": { intervalMinutes: 141, coinPerUnit: 47 },
    "item.misty_grouper": { intervalMinutes: 159, coinPerUnit: 53 },
    "item.salmon_shark": { intervalMinutes: 180, coinPerUnit: 60 },
    "item.axolotl": { intervalMinutes: 180, coinPerUnit: 60 },
    "item.eel": { intervalMinutes: 120, coinPerUnit: 40 }
};

// 도감 저장 배열의 **크기 안전장치**(Internal Data 가 끝없이 자라지 않게) — 칸 수가 아니다.
// `addToCodex` 는 8.3 다섯 갈래(`CODEX_CATEGORIES`) 칸 ID 만 담으므로, 이 값은 칸 수(지금 154)보다 넉넉한 안전장치일 뿐이다.
var CODEX_MAX_ENTRIES = 200;

var GACHA_POOL = [
    { id: "gacha.pet_ribbon", grade: "common" },
    { id: "gacha.pet_strawhat", grade: "common" },
    { id: "gacha.deco_wood_fence", grade: "common" },
    { id: "gacha.deco_dirt_path", grade: "common" },
    { id: "gacha.mat_hide_5", grade: "common" },
    { id: "gacha.mat_wood_5", grade: "common" },
    { id: "gacha.refund_coin_50", grade: "common" },
    { id: "gacha.pet_bell", grade: "rare" },
    { id: "gacha.deco_stone_fence", grade: "rare" },
    { id: "gacha.mat_silver_hide_3", grade: "rare" },
    { id: "gacha.refund_coin_150", grade: "rare" },
    { id: "gacha.pet_crown", grade: "epic" },
    { id: "gacha.pet_festival_flags", grade: "epic" },
    { id: "gacha.deco_red_barn", grade: "epic" },
    { id: "gacha.mat_gold_hide_2", grade: "epic" },
    { id: "gacha.refund_coin_400", grade: "epic" },
    { id: "gacha.pet_gold_mane", grade: "legendary" },
    { id: "gacha.deco_starlight_bg", grade: "legendary" },
    { id: "gacha.mat_legend_claw_1", grade: "legendary" }
];

var GRADE_WEIGHT = { common: 70.0, rare: 22.0, epic: 6.5, legendary: 1.5 };
var GRADE_DUP_REFUND = { common: 50, rare: 150, epic: 400, legendary: 1000 };
// 공개 문서 GACHA_DISCLOSURE 「재화 환급 · 스네어 코인 xN」 경품 — 아이템이 아니라 코인(SC)으로 바로 준다.
// 돈은 소유가 없으므로 중복 판정에서 뺀다(M35-02). 금액은 ID 끝 숫자가 아니라 이 표 한 곳에서 읽는다.
var GACHA_COIN_PRIZES = { "gacha.refund_coin_50": 50, "gacha.refund_coin_150": 150, "gacha.refund_coin_400": 400 };
// 공개 문서 「희귀 소재 · 이름 xN」 경품 — 그 경품 아이템을 N 개 쌓아 준다(제작에서 1개 = 재료 1개, M35-03).
// 소재는 쓰면 줄어드는 물건이라 중복 환급 없이 다시 N 개 더 쌓는다. 🟡 묶음 1개를 N 개로 세는 길보다 셈이 하나뿐이라 작다.
var GACHA_MATERIAL_PRIZES = { "gacha.mat_hide_5": 5, "gacha.mat_silver_hide_3": 3, "gacha.mat_gold_hide_2": 2, "gacha.mat_wood_5": 5, "gacha.mat_legend_claw_1": 1 };
var PITY_LIMIT = 50;
var SINGLE_COST = { GM: 100, SC: 500 };
var MULTI_COST_GM = 900;
var MULTI_ROLL_COUNT = 10;

// DESIGN.md 3.5 「시작 장비 지급」 표 — 새로 시작한 계정에게 첫 접속 한 번만 주는 것.
// 값을 손으로 고치지 말고 그 표에서 옮긴다 (2층 `tools/playfab/test_cloudscript.js` 가 표와 맞대 본다).
var STARTER_KIT = [
    { itemId: "item.club", count: 1 }
];
// 받았다는 표시를 적는 Player Data(내부) 키. 이 키가 있으면 두 번째 접속부터 아무것도 안 준다.
var STARTER_KIT_DATA_KEY = "starterKit";
// DESIGN.md 8.14 「시작 코인」 — 시작 장비와 같은 첫 1회에 스네어 코인으로 더 준다.
var STARTER_COINS = 10;

/**
 * 첫 접속 1회 시작 장비 지급 (DESIGN 3.5 · BACKEND.md 함수 표).
 * 판정은 여기 하나다 — 클라이언트는 "처음인가" 를 스스로 정하지 않는다(CLAUDE.md 8).
 * 이미 받은 계정에는 `granted:false` 를 돌려준다. 오류가 아니다 — 두 번째 접속부터는 이게 정상이다.
 */
handlers.GrantStarterKit = function (args, context) {
    var stamp = loadStarterStamp();
    if (stamp) {
        return {
            granted: false,
            alreadyGranted: true,
            grantedAtUtc: stamp.grantedAtUtc,
            items: stamp.items
        };
    }

    var itemIds = [];
    var items = [];
    for (var i = 0; i < STARTER_KIT.length; i++) {
        var entry = STARTER_KIT[i];
        for (var n = 0; n < entry.count; n++) {
            itemIds.push(entry.itemId);
        }
        items.push({ itemId: entry.itemId, count: entry.count });
    }

    // **지급이 먼저, 표시가 나중이다.** 표시를 먼저 적으면 그 뒤 지급이 실패했을 때 그 계정은 시작 장비를
    // 영영 못 받는다. 반대로 지급 뒤 표시가 실패하면 다음 접속에 몽둥이 하나를 더 받을 뿐이다 —
    // 둘 다 드문 일이고, 덜 나쁜 쪽으로 기울였다.
    server.GrantItemsToUser({
        PlayFabId: currentPlayerId,
        ItemIds: itemIds
    });
    server.AddUserVirtualCurrency({
        PlayFabId: currentPlayerId,
        VirtualCurrency: "SC",
        Amount: STARTER_COINS
    });

    var grantedAtUtc = new Date().toISOString();
    saveStarterStamp({ grantedAtUtc: grantedAtUtc, items: items });

    return { granted: true, alreadyGranted: false, grantedAtUtc: grantedAtUtc, items: items, coins: STARTER_COINS };
};

// 기기 시험용 테스터 키트 — 개발 빌드가 로그인 뒤 한 번 부른다. 허용 목록 밖이면 거절(일반 플레이어는 못 받는다).
// 키트를 바꾸면 TESTER_KIT_VERSION 을 올려 다시 받게 한다.
// 허용 목록은 코드에 두지 않는다(출시 점검 10-16) — 타이틀 Internal Data `testerIds`(JSON 배열)에서 읽는다.
// 키가 없거나 비면 아무도 못 받는다 = 운영 기본값. 개발 기간에만 감독이 콘솔에서 채운다(BACKEND.md).
var TESTER_IDS_TITLE_KEY = "testerIds";
function testerIds() {
    var data = server.GetTitleInternalData({ Keys: [TESTER_IDS_TITLE_KEY] }).Data || {};
    try {
        var ids = JSON.parse(data[TESTER_IDS_TITLE_KEY] || "[]");
        return Array.isArray(ids) ? ids : [];
    } catch (e) {
        return [];
    }
}
var TESTER_KIT_VERSION = 8; // 8: 위에 올리는 소품 3(M108-03) · 7: 벽 장식 3(M108-02) · 6: 총·탄은 GUNS 표, 소모품은 회복 표(RECIPES heal·RAW_HEAL) 전부 · 5: 바주카·로켓(루프#1206) · 4: 가진 것 빼고 이어 주기(꾸미기 누락 보정) · 3: 도구·설치물·소모품 전부 + 꾸미기 전부(루프#1154) · 2: 총 11종 + 탄 4종 각 200발(루프#1046)
var TESTER_KIT_DATA_KEY = "testerKit";
var TESTER_KIT_COINS = 500;
var TESTER_KIT_CALLS_PER_RUN = 6; // 지급 호출 상한(100개씩) — 한 번 실행이 한도에 안 걸리게
var TESTER_KIT = [
    { itemId: "item.hiking_backpack", count: 1 }, { itemId: "item.hatchet", count: 1 },
    { itemId: "item.pickaxe_electric", count: 1 }, { itemId: "item.chainsaw", count: 1 },
    { itemId: "item.machete", count: 1 }, { itemId: "item.rod_telescopic", count: 1 },
    { itemId: "item.worm_bait", count: 30 }, { itemId: "item.binoculars", count: 1 },
    { itemId: "item.satellite_communicator", count: 1 }, { itemId: "item.lifeboat", count: 1 },
    { itemId: "item.tent", count: 1 }, { itemId: "item.campfire", count: 1 },
    { itemId: "item.grill", count: 1 }, { itemId: "item.storage_box", count: 1 },
    { itemId: "item.hunter_hat", count: 1 }, { itemId: "item.runner_hat", count: 1 },
    { itemId: "item.saddle_hunting", count: 1 }, { itemId: "item.saddle_farming", count: 1 },
    { itemId: "item.saddle_fishing", count: 1 }, { itemId: "item.cooked_reindeer", count: 10 },
    { itemId: "item.snackbar", count: 5 }, { itemId: "item.hot_soda", count: 5 },
    { itemId: "item.heat_pack", count: 3 }, { itemId: "item.wood", count: 99 }
    // 총·탄은 `GUNS` 표 아래에서, 소모품은 `RAW_HEAL` 아래에서 표로 모아 붙인다
];
// v3 — 카탈로그의 도구·설치물·소모품을 빠짐없이(각 1, 소모품은 넉넉히). 꾸미기·총·먹는 것은 아래 각 표 밑에서 모아 붙인다.
var TESTER_KIT_EXTRA_TOOLS = ["item.club", "item.life_jacket", "item.rod_basic", "item.rod_bait", "item.rod_lucky",
    "item.fisher_hat", "item.farmer_hat", "item.lumberjack_hat", "item.strawberry_hat", "item.cook_hat", "item.looter_hat",
    "item.traveler_hat", "item.merchant_hat", "item.mining_hat", "item.sprout_hat", "item.chief_hat",
    "item.gather_basket", "item.pickaxe_basic", "item.pickaxe_advanced", "item.flannel_shirt", "item.work_pants",
    "item.bench", "item.chair_wood", "item.table_wood", "item.fence_wood", "item.garden_brazier", "item.workbench_adv",
    "item.smelter", "item.solar_kit", "item.battery", "item.lamp_electric", "item.picture_frame", "item.wall_clock", "item.wall_shelf",
    "item.mug", "item.flower_pot", "item.candle",
    "item.armchair", "item.sofa", "item.table_round", "item.table_long", "item.bed_single",
    "item.bed_double", "item.bookshelf", "item.dresser", "item.kitchen_counter", "item.kitchen_sink",
    "item.potted_tree", "item.potted_berry", "item.flower_box", "item.potted_sprout",
    "item.rug_round", "item.rug_runner", "item.rug_square", "item.doormat",
    "item.wall_mirror", "item.wall_map", "item.wall_anchor", "item.wall_wreath",
    "item.fence_picket", "item.path_log", "item.garden_arch", "item.mailbox",
    "item.candle_stand", "item.oil_lamp", "item.lantern_post", "item.garden_torch", "item.stairs_wood", "item.generator",
    "item.fridge", "item.fan_stand", "item.radio", "item.neon_sign"];
var TESTER_KIT_EXTRA_CONSUMABLES = ["item.roast_meat", "item.berry_jam", "item.water_bottle", "item.energy_drink_mild",
    "item.energy_drink", "item.energy_drink_strong", "item.coffee_bottle", "item.tea_bottle", "item.water_canister",
    "item.canned_fish", "item.bread", "item.cake", "item.sandwich", "item.hot_pot", "item.meat_soup"];
/** 키트에 같은 아이템이 두 줄이면 합친다(개수 더하기) — 위 목록과 겹쳐도 한 줄. */
function addTesterKit(itemId, count) {
    for (var i = 0; i < TESTER_KIT.length; i++) {
        if (TESTER_KIT[i].itemId === itemId) { TESTER_KIT[i].count = Math.max(TESTER_KIT[i].count, count); return; }
    }
    TESTER_KIT.push({ itemId: itemId, count: count });
}
TESTER_KIT_EXTRA_TOOLS.forEach(function (id) { addTesterKit(id, 1); });
var TESTER_KIT_CONSUMABLE_COUNT = 10; // 소모품 넉넉히 — 한 종 10개
TESTER_KIT_EXTRA_CONSUMABLES.forEach(function (id) { addTesterKit(id, TESTER_KIT_CONSUMABLE_COUNT); });

handlers.GrantTesterKit = function (args, context) {
    if (testerIds().indexOf(currentPlayerId) === -1) return fail("not_tester", "테스터 목록에 없다");
    var stamp = loadData(currentPlayerId, TESTER_KIT_DATA_KEY, { version: 0 });
    if (stamp.version >= TESTER_KIT_VERSION) return { granted: false, version: stamp.version };
    // v3 사고(2026-10-16): 표시를 먼저 적고 1357개를 14번에 나눠 주다 한도에 걸려 꾸미기가 통째로 빠졌다.
    // 그래서 가진 만큼은 빼고, 1개짜리(꾸미기·도구)를 먼저, 한 번에 호출 TESTER_KIT_CALLS_PER_RUN 번까지만 주고,
    // 다 줬을 때만 표시한다 — 끊겨도 다음 로그인에 남은 것만 이어서 준다.
    var inv = readInventory(currentPlayerId);
    var need = [];
    TESTER_KIT.forEach(function (e) {
        var n = e.count - countOf(inv, e.itemId);
        if (n > 0) need.push({ itemId: e.itemId, count: n });
    });
    need.sort(function (a, b) { return a.count - b.count; });
    var ids = [];
    need.forEach(function (e) { ids = ids.concat(repeatItem(e.itemId, e.count)); });
    var cap = TESTER_KIT_CALLS_PER_RUN * 100;
    giveItems(currentPlayerId, ids.slice(0, cap).map(function (id) { return { itemId: id, count: 1 }; }));
    if (ids.length > cap) return { granted: true, partial: true, given: cap, remaining: ids.length - cap };
    saveData(currentPlayerId, TESTER_KIT_DATA_KEY, { version: TESTER_KIT_VERSION });
    server.AddUserVirtualCurrency({ PlayFabId: currentPlayerId, VirtualCurrency: "SC", Amount: TESTER_KIT_COINS });
    var m = loadMounts();
    MOUNT_SPECIES.forEach(function (sp) { if (m.species.indexOf(sp) === -1) m.species.push(sp); });
    saveData(currentPlayerId, MOUNTS_DATA_KEY, m);
    return { granted: true, version: TESTER_KIT_VERSION, items: TESTER_KIT, coins: TESTER_KIT_COINS, mounts: m.species };
};

handlers.RegisterPet = function (args, context) {
    if (!args || !args.animalId || typeof args.slot !== "number") {
        return { error: "animalId, slot 필요" };
    }
    // 목장 생산 표(3.2)에 있는 종만 칸에 선다 — 표 밖 종은 HarvestRanch 가 줄 것도 없다.
    if (Object.keys(ANIMAL_PRODUCT_ITEM).indexOf(args.animalId) === -1) {
        return fail("bad_args", "목장에 넣을 수 없는 종: " + args.animalId);
    }
    var meta = loadRanchMeta();
    if (args.slot < 0 || args.slot >= meta.slotCap) {
        return { error: "잘못된 슬롯 번호" };
    }

    var ranch = loadRanch();
    var entry = ranch[args.slot];
    if (entry && (entry.animalId || entry.cropItemId)) { // 작물이 선 칸(PlantCrop)도 찬 칸이다
        return { error: "슬롯이 이미 차 있음" };
    }
    var now = Date.now();
    if (ranchLocked(meta, now)) return ranchLockedFail(meta);
    // 포획한 펫 아이템(`animal.<종>` ↔ `item.pet_<종>`, create_data_tables.py 규칙) 1개를 칸에 올리며 뺀다 (CLAUDE.md 8).
    // M21-07b `variant: true` 면 같은 종의 변종 펫(`item.pet_<종>_variant`)을 쓴다 — 변종이 없는 자리표시 종은 거절.
    var variant = args.variant === true;
    var petId = petItemOf(args.animalId, variant);
    if (variant && VARIANT_PET_ITEMS.indexOf(petId) === -1) return fail("bad_args", "변종이 없는 종: " + args.animalId);
    var inv = readInventory(currentPlayerId);
    if (countOf(inv, petId) < 1) return fail("not_enough_items", "포획한 펫이 없다", { itemId: petId, need: 1, have: 0 });

    takeItems(currentPlayerId, inv, petId, 1);
    // M21-01 첫 배치는 계정에 한 번 — 유지가 이미 시작된 옛 계정(펫을 넣어 본 적 있음)은 못 받는다.
    // 펫을 빼고 다시 넣어도 표시가 남아 두 번 받지 못한다.
    var firstBatch = !meta.upkeepUntilUtc && !meta.firstBatchUsed;
    if (firstBatch) meta.firstBatchUsed = true;
    if (startRanchUpkeep(meta, now) || firstBatch) saveRanchMeta(meta);
    ranch[args.slot] = {
        slot: args.slot,
        animalId: args.animalId,
        occupiedSinceUtc: new Date().toISOString(),
        stock: 0
    };
    if (firstBatch) ranch[args.slot].firstBatch = true;
    if (variant) ranch[args.slot].variant = true;
    saveRanch(ranch);
    addToCodex(args.animalId);
    bumpAchievements({});

    return { success: true, slot: args.slot, animalId: args.animalId, variant: variant };
};

handlers.HarvestRanch = function (args, context) {
    if (!args || typeof args.slot !== "number") {
        return { error: "slot 필요" };
    }

    var ranch = loadRanch();
    var entry = ranch[args.slot];
    if (!entry || !entry.animalId) {
        return { error: "슬롯이 비어 있음" };
    }
    var meta = loadRanchMeta();
    if (ranchLocked(meta, Date.now())) return ranchLockedFail(meta);

    var now = Date.now();
    var units = animalHarvestUnits(entry, now);
    if (units <= 0) {
        return { harvested: 0, message: "아직 생산 안 됨" };
    }
    if (!ANIMAL_PRODUCT_ITEM[entry.animalId]) {
        return { error: "동물 '" + entry.animalId + "' 생산 아이템이 카탈로그에 없음 (ANIMAL_PRODUCT_ITEM 갱신 필요)" };
    }
    var got = grantAnimalHarvest(entry, meta, units, now);
    saveRanch(ranch);
    return got;
};

/** 동물 칸에 쌓인 생산 단위 수 — 경과 시간 ÷ 주기, 상한에서 자른다 (DESIGN 3.2). */
/** 낚시터 칸에 쌓인 거둘 수 — HarvestFishing·GetBaseState 가 같은 계산을 쓴다. 표에 없는 어종은 0. */
function fishingHarvestUnits(entry, nowMs) {
    var fishInfo = FISH_TABLE[entry.fishItemId];
    if (!fishInfo) return 0;
    var elapsedMinutes = (nowMs - new Date(entry.occupiedSinceUtc).getTime()) / (1000 * 60);
    return Math.min(Math.floor(elapsedMinutes / fishInfo.intervalMinutes), FISHING_CAP_UNITS);
}

function animalHarvestUnits(entry, nowMs) {
    var elapsedMinutes = (nowMs - new Date(entry.occupiedSinceUtc).getTime()) / (1000 * 60);
    var first = animalFirstUnitMinutes(entry);
    if (elapsedMinutes < first) return 0;
    return Math.min(1 + Math.floor((elapsedMinutes - first) / animalIntervalMinutes(entry)), HARVEST_CAP_UNITS);
}

/** 동물 칸 한 주기 — 변종 칸(M21-07b)은 ÷`VARIANT_CYCLE_DIVISOR`. */
function animalIntervalMinutes(entry) { return entry.variant ? HARVEST_INTERVAL_MINUTES / VARIANT_CYCLE_DIVISOR : HARVEST_INTERVAL_MINUTES; }

/** 첫 단위가 서는 분 — M21-01 첫 배치 칸은 1분, 나머지는 주기 그대로(그러면 위 식이 `floor(경과 ÷ 주기)` 와 같다). */
function animalFirstUnitMinutes(entry) { return entry.firstBatch ? FIRST_BATCH_MINUTES : animalIntervalMinutes(entry); }

/** 칸에 올리고 빼는 펫 아이템 — `animal.<종>` ↔ `item.pet_<종>`, 변종이면 꼬리 `_variant`(DESIGN 10.6). */
function petItemOf(animalId, variant) { return "item.pet_" + animalId.slice("animal.".length) + (variant ? "_variant" : ""); }

/**
 * 쌓인 `units` 만큼 생산물·코인·명성을 주고 칸 시계를 남은 몫만 남기고 당긴다. 칸 저장은 부르는 쪽이 한다 —
 * `RemovePet` 은 이어서 칸을 비우기 때문이다. HarvestRanch·RemovePet 이 같은 계산을 쓴다.
 */
function grantAnimalHarvest(entry, meta, units, nowMs) {
    var itemId = ANIMAL_PRODUCT_ITEM[entry.animalId];
    var pay = takeHarvestCoins(COIN_PER_UNIT * units);
    var coinReward = pay.coins;

    server.GrantItemsToUser({
        PlayFabId: currentPlayerId,
        ItemIds: repeatItem(itemId, units)
    });
    if (coinReward > 0) {
        server.AddUserVirtualCurrency({
            PlayFabId: currentPlayerId,
            VirtualCurrency: "SC",
            Amount: coinReward
        });
    }

    var elapsedMs = nowMs - new Date(entry.occupiedSinceUtc).getTime();
    var usedMinutes = animalFirstUnitMinutes(entry) + (units - 1) * animalIntervalMinutes(entry);
    // 상한에서 생산이 멈춘다(DESIGN 3.2 「그 이상은 생산 일시정지」) — 찬 칸의 넘친 시간은 이월하지 않는다.
    // 이월하면 며칠 둔 칸이 거두자마자 다시 상한이라 누를 때마다 또 준다(루프#1433 온라인 실측).
    var remainderMs = units >= HARVEST_CAP_UNITS ? 0 : elapsedMs - usedMinutes * 60 * 1000;
    entry.occupiedSinceUtc = new Date(nowMs - remainderMs).toISOString();
    entry.stock = 0;
    delete entry.firstBatch; // 첫 배치는 한 번 거두면 끝 — 다음부터 30분
    // M12-05: 명성은 수확 성공 자리에서만 붙는다. 유지 기록이 없던 옛 계정은 여기서 유지가 시작된다.
    var fame = units * RANCH_FAME_PER_UNIT;
    meta.fame += fame;
    startRanchUpkeep(meta, nowMs);
    saveRanchMeta(meta);
    addToCodex(entry.animalId);
    var harvestDeltas = { harvest: units };
    if (entry.animalId === "animal.fogbuck") harvestDeltas.harvestFogbuck = units; // 5장 24단계는 안개뿔 수확만 센다
    bumpAchievements(harvestDeltas);

    return { harvested: units, itemId: itemId, coins: coinReward, fame: fame, coinsCapped: pay.capped, coinsLeftToday: pay.left };
}

/**
 * M109-04a 수확 코인 하루 상한 — 오늘 더 줄 수 있는 몫(`want` 이하)을 셈해 쓴 몫을 저장하고 돌려준다.
 * 날이 바뀌면 0 부터. 코인을 더하는 자리는 부르는 쪽(장부 #3·#5 줄)에 그대로 둔다.
 */
function takeHarvestCoins(want) {
    var day = questDay();
    var state = loadData(currentPlayerId, HARVEST_COIN_DATA_KEY, null);
    if (!state || state.day !== day) state = { day: day, coins: 0 };
    var coins = Math.max(0, Math.min(want, HARVEST_DAILY_COINS - state.coins));
    if (coins > 0) {
        state.coins += coins;
        saveData(currentPlayerId, HARVEST_COIN_DATA_KEY, state);
    }
    return { coins: coins, capped: coins < want, left: HARVEST_DAILY_COINS - state.coins };
}

/**
 * #0198 목장 칸의 펫을 빼서 펫 아이템(`item.pet_<종>`)으로 돌려준다 — RegisterPet 의 반대 (DESIGN 3.2 🟡 펫 빼기).
 * 쌓인 생산물은 버리지 않고 **먼저 수확한다**(HarvestRanch 와 같은 계산). 생산물이 가방에 안 들어가면
 * 아무것도 바꾸지 않고 `inventory_full` — 펫 아이템 자체는 가방 칸을 쓰지 않는다(isBackpackItem).
 * 잠긴 목장은 수확처럼 거절한다(`locked`) — 빼기로 수확 잠금을 돌아가지 못하게.
 */
handlers.RemovePet = function (args, context) {
    args = args || {};
    var meta = loadRanchMeta();
    var slot = args.slot;
    if (typeof slot !== "number" || slot % 1 !== 0 || slot < 0 || slot >= meta.slotCap) {
        return fail("bad_slot", "잘못된 슬롯 번호: " + slot);
    }
    var ranch = loadRanch();
    var entry = ranch[slot];
    if (!entry || !entry.animalId) return fail("empty_slot", "뺄 펫이 없는 칸");
    var now = Date.now();
    if (ranchLocked(meta, now)) return ranchLockedFail(meta);

    var units = animalHarvestUnits(entry, now);
    var productId = ANIMAL_PRODUCT_ITEM[entry.animalId];
    // 10.9 씌운 꾸미기도 배낭으로 돌아온다 — 생산물과 함께 칸을 센다(꾸미기는 배낭 칸을 쓴다).
    var deltas = {};
    if (units > 0 && productId) deltas[productId] = units;
    if (entry.cosmetic) deltas[entry.cosmetic] = (deltas[entry.cosmetic] || 0) + 1;
    if (Object.keys(deltas).length > 0) {
        var full = roomCheck(readInventory(currentPlayerId), deltas);
        if (full) return full;
    }

    // 검사를 다 통과한 뒤에만 준다 — 생산물 → 펫 아이템(+꾸미기) → 칸 비우기.
    var got = (units > 0 && productId)
        ? grantAnimalHarvest(entry, meta, units, now)
        : { harvested: 0, itemId: productId || "", coins: 0, fame: 0 };
    var petItemId = petItemOf(entry.animalId, entry.variant === true);
    var back = [{ itemId: petItemId, count: 1 }];
    if (entry.cosmetic) back.push({ itemId: entry.cosmetic, count: 1 });
    giveItems(currentPlayerId, back);
    ranch[slot] = null; // HarvestCrop 과 같다 — null 이 빈 칸이다
    saveRanch(ranch);

    return {
        removed: true, slot: slot, animalId: entry.animalId, petItemId: petItemId, variant: entry.variant === true,
        harvested: got.harvested, itemId: got.itemId, coins: got.coins, fame: got.fame, cosmetic: entry.cosmetic || ""
    };
};

// ---- 10.9 목장 동물 꾸미기 (상점 개편 C) ----
// 동물 칸 하나에 꾸미기 1칸. 능력치 없음(겉모습만). 옷장 상점(10.7)·상자(10.8) 풀에 같이 들어간다.
var RANCH_COSMETICS = ["item.ranch_ribbon", "item.ranch_necklace", "item.ranch_hat"];
// 공개 문서 GACHA_DISCLOSURE 「펫 의상」 경품 6종 — 뽑기로만 얻고 같은 꾸미기 1칸에 씌운다(M35-04).
// 옷장 상점·상자 풀은 위 세 개뿐이라 따로 둔다. 가챠 아이템이라 배낭 칸을 안 차지한다(`isBackpackItem`).
var GACHA_PET_COSMETICS = ["gacha.pet_ribbon", "gacha.pet_strawhat", "gacha.pet_bell", "gacha.pet_crown",
    "gacha.pet_festival_flags", "gacha.pet_gold_mane"];
var PET_COSMETICS = RANCH_COSMETICS.concat(GACHA_PET_COSMETICS);

/**
 * 동물 칸에 꾸미기를 씌우거나(`itemId`) 벗긴다(`itemId` 빈 값). 씌우면 배낭에서 1개 빼서 칸 `cosmetic` 에 넣고,
 * 먼저 씌웠던 것은 배낭으로 돌려준다. 벗기기·바꿔 끼우기로 배낭이 넘치면 아무것도 바꾸지 않고 `inventory_full`.
 * 잠긴 목장도 받는다 — 겉모습만 바뀌고 생산·수확과 상관없다.
 */
handlers.SetPetCosmetic = function (args, context) {
    args = args || {};
    var meta = loadRanchMeta();
    var slot = args.slot;
    if (typeof slot !== "number" || slot % 1 !== 0 || slot < 0 || slot >= meta.slotCap) {
        return fail("bad_slot", "잘못된 슬롯 번호: " + slot);
    }
    var itemId = args.itemId || "";
    if (itemId !== "" && PET_COSMETICS.indexOf(itemId) === -1) return fail("bad_args", "동물 꾸미기가 아니다: " + itemId);
    var ranch = loadRanch();
    var entry = ranch[slot];
    if (!entry || !entry.animalId) return fail("empty_slot", "꾸밀 동물이 없는 칸");
    var old = entry.cosmetic || "";
    if (old === itemId) return fail("already", itemId === "" ? "씌운 꾸미기가 없다" : "이미 씌운 꾸미기");

    var inv = readInventory(currentPlayerId);
    if (itemId !== "" && countOf(inv, itemId) < 1) return fail("not_enough_items", "꾸미기가 없다", { itemId: itemId, need: 1, have: 0 });
    var deltas = {};
    if (itemId !== "") deltas[itemId] = -1;
    if (old !== "") deltas[old] = (deltas[old] || 0) + 1;
    var full = roomCheck(inv, deltas);
    if (full) return full;

    if (itemId !== "") takeItems(currentPlayerId, inv, itemId, 1);
    if (old !== "") giveItems(currentPlayerId, [{ itemId: old, count: 1 }]);
    if (itemId === "") delete entry.cosmetic; else entry.cosmetic = itemId;
    saveRanch(ranch);
    return { slot: slot, cosmetic: itemId, returned: old };
};

// ---- 10.9b 목장 데코 (M35-05a, docs/specs/gacha-deco.md) ----
// 공개 문서 「목장 데코」 경품 5종 → 목장 겉모습 종류 한 칸. 같은 종류(나무·돌담 울타리)는 한 칸이라 새 것이 옛 것을 밀어낸다.
var GACHA_RANCH_DECOS = {
    "gacha.deco_wood_fence": "fence",
    "gacha.deco_stone_fence": "fence",
    "gacha.deco_dirt_path": "path",
    "gacha.deco_red_barn": "barn",
    "gacha.deco_starlight_bg": "sky"
};
var RANCH_DECO_KINDS = ["fence", "path", "barn", "sky"];

/** 적용 중인 데코 중 배낭에 아직 있는 것만 — 겉모습은 가진 것만 보인다(wornOutfit 과 같은 규칙). */
function ranchDecoView(meta, inv) {
    var deco = meta.deco || {};
    var kinds = Object.keys(deco);
    if (kinds.length === 0) return {};
    inv = inv || readInventory(currentPlayerId);
    var view = {};
    kinds.forEach(function (kind) {
        if (countOf(inv, deco[kind]) > 0) view[kind] = deco[kind];
    });
    return view;
}

/**
 * 목장 데코를 적용하거나(`itemId`) 한 종류를 푼다(빈 `itemId` + `kind`). 스킨 = 해금이라 **소비하지 않는다** —
 * 가졌는지만 보고 `ranchMeta.deco[kind]` 에 적는다. 그래서 가득 찬 가방·잠긴 목장에도 된다(생산과 무관).
 * 답 `deco` = 적용 중인 것 전체(가진 것만). 클라는 이 응답 뒤에만 겉모습을 바꾼다(CLAUDE.md 8).
 */
handlers.SetRanchDeco = function (args, context) {
    args = args || {};
    var itemId = args.itemId || "";
    var kind;
    if (itemId === "") {
        kind = args.kind;
        if (RANCH_DECO_KINDS.indexOf(kind) === -1) return fail("bad_args", "풀 종류가 아니다: " + kind);
    } else {
        kind = GACHA_RANCH_DECOS[itemId];
        if (!kind) return fail("bad_args", "목장 데코가 아니다: " + itemId);
    }
    var meta = loadRanchMeta();
    var deco = meta.deco || {};
    var old = deco[kind] || "";
    if (old === itemId) return fail("already", itemId === "" ? "적용한 데코가 없다" : "이미 적용한 데코");
    var inv = readInventory(currentPlayerId);
    if (itemId !== "" && countOf(inv, itemId) < 1) return fail("not_enough_items", "데코가 없다", { itemId: itemId, need: 1, have: 0 });

    if (itemId === "") delete deco[kind]; else deco[kind] = itemId;
    meta.deco = deco;
    saveRanchMeta(meta);
    return { kind: kind, itemId: itemId, replaced: old, deco: ranchDecoView(meta, inv) };
};

/**
 * M21-06b 같이 하기 보고 — 클라이언트가 방에 있는 동안 주기적으로 부른다(DESIGN 3.2 「같이 하기 생산」).
 * `hostId` = 방 호스트의 PlayFabId(내가 호스트면 내 것) · `peers` = 같은 방 다른 사람들의 PlayFabId.
 * 인원은 믿지 않고 peers 각자가 **자기 계정으로** 남긴 `coop` 을 읽어 같은 hostId·`COOP_FRESH_SECONDS` 안인
 * 사람만 센다. 나 포함 `COOP_MIN_PLAYERS` 이상이면 지난 보고 뒤 흐른 시간(최대 `COOP_FRESH_SECONDS`)을
 * 주기 ×`COOP_CYCLE_FACTOR` 로 쳐서 남는 몫만큼 동물 칸 시계를 당긴다. 연타해도 흐른 시간만 친다.
 * 가득 찬 칸(더 쌓이지 않는다)·잠긴 목장(생산이 멈췄다)은 당기지 않는다.
 */
handlers.ReportCoop = function (args, context) {
    args = args || {};
    var hostId = args.hostId;
    var peers = args.peers;
    if (typeof hostId !== "string" || hostId.length < 1 || hostId.length > 64 || !Array.isArray(peers) || peers.length > COOP_MAX_PEERS) {
        return fail("bad_args", "hostId, peers(" + COOP_MAX_PEERS + "명까지) 필요");
    }
    for (var i = 0; i < peers.length; i++) {
        if (typeof peers[i] !== "string" || peers[i].length < 1 || peers[i].length > 64) return fail("bad_args", "peers 는 PlayFabId 문자열");
    }
    var now = Date.now();
    var freshMs = COOP_FRESH_SECONDS * 1000;
    var prev = loadData(currentPlayerId, COOP_DATA_KEY, null);
    var sinceMs = (prev && prev.hostId === hostId) ? Math.max(0, now - Date.parse(prev.atUtc)) : 0;
    saveData(currentPlayerId, COOP_DATA_KEY, { hostId: hostId, atUtc: new Date(now).toISOString() });

    var seen = {};
    seen[currentPlayerId] = true;
    var players = 1;
    peers.forEach(function (peerId) {
        if (seen[peerId]) return;
        seen[peerId] = true;
        var other = loadData(peerId, COOP_DATA_KEY, null);
        if (other && other.hostId === hostId && now - Date.parse(other.atUtc) <= freshMs) players += 1;
    });
    var coop = players >= COOP_MIN_PLAYERS;
    var creditedMs = coop ? Math.min(sinceMs, freshMs) : 0;
    var bonusMs = Math.round(creditedMs * (1 / COOP_CYCLE_FACTOR - 1));

    if (bonusMs > 0 && !ranchLocked(loadRanchMeta(), now)) {
        var ranch = loadRanch();
        var moved = false;
        ranch.forEach(function (entry) {
            if (!entry || !entry.animalId || animalHarvestUnits(entry, now) >= HARVEST_CAP_UNITS) return;
            entry.occupiedSinceUtc = new Date(Date.parse(entry.occupiedSinceUtc) - bonusMs).toISOString();
            moved = true;
        });
        if (moved) saveRanch(ranch);
    }
    return { coop: coop, players: players, creditedMs: creditedMs, bonusMs: bonusMs };
};

// ---- ⑩ 작물 칸 (DESIGN 8.2 · 8.10 씨앗) ----
// 작물은 목장과 **같은 `ranch` 배열의 한 칸**이다(DESIGN 8.2 "슬롯 타입만 다르게"). 항목 모양만 다르다:
// `{slot, cropItemId, occupiedSinceUtc, stock}` — `animalId` 가 없으니 RegisterPet/HarvestRanch 와 안 섞인다.

/** 씨앗 1개를 빼고 빈 칸에 심는다. 도감은 수확 때 붙는다(심은 것만으로 "얻은" 것이 아니다). */
handlers.PlantCrop = function (args, context) {
    args = args || {};
    var seedId = args.seedItemId;
    var cropId = Object.keys(CROP_CYCLE_MINUTES).filter(function (c) { return seedOf(c) === seedId; })[0] || "";
    if (!CROP_CYCLE_MINUTES[cropId] || !isInt(args.slot)) return fail("bad_args", "심을 수 없는 씨앗: " + seedId);
    var meta = loadRanchMeta();
    if (args.slot < 0 || args.slot >= meta.slotCap) return fail("bad_slot", "잘못된 슬롯 번호");
    var ranch = loadRanch();
    var entry = ranch[args.slot];
    if (entry && (entry.animalId || entry.cropItemId)) return fail("slot_full", "슬롯이 이미 차 있음");
    var now = Date.now();
    if (ranchLocked(meta, now)) return ranchLockedFail(meta);
    var inv = readInventory(currentPlayerId);
    if (countOf(inv, seedId) < 1) return fail("not_enough_items", "씨앗이 없다", { itemId: seedId, need: 1, have: 0 });

    takeItems(currentPlayerId, inv, seedId, 1);
    if (startRanchUpkeep(meta, now)) saveRanchMeta(meta);
    while (ranch.length <= args.slot) ranch.push(null); // 확장 칸은 저장 배열이 아직 짧을 수 있다
    ranch[args.slot] = { slot: args.slot, cropItemId: cropId, occupiedSinceUtc: new Date(now).toISOString(), stock: 0 };
    saveRanch(ranch);
    return { success: true, slot: args.slot, cropItemId: cropId, seedItemId: seedId };
};

/**
 * 작물 수확 — 원본(레퍼런스)대로 **한 번 거두면 칸이 빈다**. 다시 심으려면 씨앗이 또 1개 든다(PlantCrop).
 * 다 자랐으면(경과 ≥ 재배 시간) 작물 1(참나무는 나무 4 — CROP_HARVEST_COUNT) + 작물별 코인 1회분, 명성 +1, 도감. 쌓아 두는 상한은 없다(DESIGN 8.2 심기·수확 표).
 */
handlers.HarvestCrop = function (args, context) {
    args = args || {};
    if (!isInt(args.slot)) return fail("bad_args", "slot 필요");
    var ranch = loadRanch();
    var entry = ranch[args.slot];
    if (!entry || !entry.cropItemId) return fail("empty_slot", "작물 칸이 비어 있음");
    var meta = loadRanchMeta();
    var now = Date.now();
    if (ranchLocked(meta, now)) return ranchLockedFail(meta);
    if (!cropRipe(entry, now)) return { harvested: 0, message: "아직 안 자랐다" };

    var coinReward = CROP_HARVEST_COINS[entry.cropItemId];
    var count = CROP_HARVEST_COUNT[entry.cropItemId] || 1;
    var bonus = Math.random() < (wornHatEffect(readInventory(currentPlayerId)).harvestBonusChance || 0) ? 1 : 0; // 13.5 밭두렁 갓 — 코인은 그대로
    count += bonus;
    giveItems(currentPlayerId, [{ itemId: entry.cropItemId, count: count }]);
    server.AddUserVirtualCurrency({ PlayFabId: currentPlayerId, VirtualCurrency: "SC", Amount: coinReward });

    ranch[args.slot] = null; // 칸을 비운다 — GetBaseState·RegisterPet·PlantCrop 은 null 을 빈 칸으로 본다
    saveRanch(ranch);
    meta.fame += RANCH_FAME_PER_UNIT; // 같은 목장 수확이라 명성도 같다
    saveRanchMeta(meta);
    addToCodex(entry.cropItemId);
    bumpAchievements({ harvest: 1 });

    return { harvested: count, bonus: bonus, itemId: entry.cropItemId, coins: coinReward, fame: RANCH_FAME_PER_UNIT };
};

/** 다 자랐나 — 재배 시간(DESIGN 8.2)이 지났으면 참. 수확 판정과 GetBaseState 의 표시값이 같은 줄을 쓴다. */
function cropRipe(entry, now) {
    return now - Date.parse(entry.occupiedSinceUtc) >= CROP_CYCLE_MINUTES[entry.cropItemId] * 60 * 1000;
}

handlers.RegisterFishingSpot = function (args, context) {
    if (!args || !args.fishItemId || typeof args.slot !== "number") {
        return { error: "fishItemId, slot 필요" };
    }
    if (args.slot < 0 || args.slot >= FISHING_SLOT_COUNT) {
        return { error: "잘못된 슬롯 번호" };
    }
    if (!FISH_TABLE[args.fishItemId]) {
        return { error: "알 수 없는 fishItemId: " + args.fishItemId };
    }

    var fishing = loadFishing();
    var entry = fishing[args.slot];
    if (entry && entry.fishItemId) {
        return { error: "슬롯이 이미 차 있음" };
    }

    fishing[args.slot] = {
        slot: args.slot,
        fishItemId: args.fishItemId,
        occupiedSinceUtc: new Date().toISOString(),
        stock: 0
    };
    saveFishing(fishing);

    return { success: true, slot: args.slot, fishItemId: args.fishItemId };
};

handlers.HarvestFishing = function (args, context) {
    if (!args || typeof args.slot !== "number") {
        return { error: "slot 필요" };
    }

    var fishing = loadFishing();
    var entry = fishing[args.slot];
    if (!entry || !entry.fishItemId) {
        return { error: "슬롯이 비어 있음" };
    }

    var fishInfo = FISH_TABLE[entry.fishItemId];
    if (!fishInfo) {
        return { error: "어종 '" + entry.fishItemId + "' 정보가 FISH_TABLE 에 없음" };
    }

    var now = new Date();
    var elapsedMs = now.getTime() - new Date(entry.occupiedSinceUtc).getTime();
    var units = fishingHarvestUnits(entry, now.getTime());

    if (units <= 0) {
        return { harvested: 0, message: "아직 생산 안 됨" };
    }

    var pay = takeHarvestCoins(fishInfo.coinPerUnit * units); // M109-04a 동물 칸과 합친 하루 상한
    var coinReward = pay.coins;

    server.GrantItemsToUser({
        PlayFabId: currentPlayerId,
        ItemIds: repeatItem(entry.fishItemId, units)
    });
    if (coinReward > 0) {
        server.AddUserVirtualCurrency({
            PlayFabId: currentPlayerId,
            VirtualCurrency: "SC",
            Amount: coinReward
        });
    }

    var consumedMs = units * fishInfo.intervalMinutes * 60 * 1000;
    var remainderMs = units >= FISHING_CAP_UNITS ? 0 : elapsedMs - consumedMs; // 상한에서 멈춤 — 동물 칸과 같다
    entry.occupiedSinceUtc = new Date(now.getTime() - remainderMs).toISOString();
    entry.stock = 0;
    saveFishing(fishing);
    addToCodex(entry.fishItemId);
    bumpAchievements({ fish: units });

    return { harvested: units, itemId: entry.fishItemId, coins: coinReward, coinsCapped: pay.capped, coinsLeftToday: pay.left };
};

/**
 * 재시작 복원용 읽기 전용 조회 — 내 목장·낚시터·텐트·배 저장값을 그대로 돌려준다 (BACKEND.md 함수 표).
 * 판정·지급·정리(만료 텐트 지우기)를 하지 않는다 — 그건 각 쓰기 핸들러의 일이다.
 */
handlers.GetBaseState = function (args, context) {
    var meta = loadRanchMeta();
    var ranch = loadRanch();
    while (ranch.length < meta.slotCap) ranch.push(null); // 칸 수 = 서버 상한 (확장한 칸도 빈 칸으로 보인다)
    var now = Date.now();
    var potato = loadData(currentPlayerId, SWEET_POTATO_DATA_KEY, null); // 8.17 산 주가 지났으면 여기서도 썩는다
    if (potato && potato.week !== potatoWeekOf(questDay())) rotSweetPotatoes(potatoWeekOf(questDay()), potato);
    // ⑩ 작물 칸의 `stock` 은 저장하지 않고 여기서만 채운다 — 다 자랐으면 1(거둘 것 1개), 아니면 0.
    // 클라이언트는 이 값으로만 "다 자람" 을 보인다(시각 계산을 클라이언트에 두지 않는다, CLAUDE.md 8).
    // M18-08 동물 칸도 같은 자리에서 채운다 — 저장값은 RegisterPet·수확 때의 0 그대로라, 안 채우면
    // 시간이 지나도 HUD 의 "수확 준비" 가 영영 안 켜졌다. HarvestRanch 가 줄 단위 수와 같은 계산이다.
    ranch.forEach(function (entry) {
        if (entry && entry.cropItemId) entry.stock = cropRipe(entry, now) ? 1 : 0;
        else if (entry && entry.animalId) entry.stock = animalHarvestUnits(entry, now);
    });
    // M19-07 낚시터 칸도 같다 — 저장값은 RegisterFishingSpot·수확 때의 0 그대로라 HarvestFishing 과 같은 수로 채운다.
    var fishing = loadFishing();
    fishing.forEach(function (entry) { if (entry && entry.fishItemId) entry.stock = fishingHarvestUnits(entry, now); });
    return {
        ranch: ranch,
        ranchUpkeep: ranchUpkeepView(meta, now),
        fishing: fishing,
        tents: loadData(currentPlayerId, BASE_DATA_KEY, []),
        boat: loadData(currentPlayerId, BOAT_DATA_KEY, { launched: false }),
        boatStorage: loadData(currentPlayerId, BOAT_STORAGE_DATA_KEY, {}), // M32-02 배 보관함 속(배 짐칸 8칸)
        hat: wornHatId(), // #0199 재접속 때 배낭 모자 표시 복원 — 효과를 주는 모자와 같은 규칙
        outfit: wornOutfit(), // 스킨 조각 d — 입은 꾸미기(가진 것만), 기록이 없으면 인벤토리를 안 읽는다
        ranchDeco: ranchDecoView(meta), // M35-05a — 적용한 목장 데코(가진 것만), 기록이 없으면 인벤토리를 안 읽는다
        nowUtc: new Date(now).toISOString() // M108-07 — 발전기 `fuelUntilUtc` 를 서버 시계 차이로 읽게
    };
};

handlers.RollGacha = function (args, context) {
    if (!args || (args.currency !== "GM" && args.currency !== "SC")) {
        return { error: "currency 는 GM 또는 SC" };
    }

    var rollCount = args.rollCount === MULTI_ROLL_COUNT ? MULTI_ROLL_COUNT : 1;
    if (rollCount === MULTI_ROLL_COUNT && args.currency !== "GM") {
        return { error: "10연차는 젬(GM)만 가능" };
    }

    var cost = rollCount === MULTI_ROLL_COUNT ? MULTI_COST_GM : SINGLE_COST[args.currency];

    var inventory = server.GetUserInventory({ PlayFabId: currentPlayerId });
    var balance = inventory.VirtualCurrency[args.currency] || 0;
    if (balance < cost) {
        return { error: "재화 부족", need: cost, have: balance };
    }

    var ownedItemIds = {};
    for (var i = 0; i < inventory.Inventory.length; i++) {
        ownedItemIds[inventory.Inventory[i].ItemId] = true;
    }

    server.SubtractUserVirtualCurrency({
        PlayFabId: currentPlayerId,
        VirtualCurrency: args.currency,
        Amount: cost
    });

    var pityData = server.GetUserInternalData({
        PlayFabId: currentPlayerId,
        Keys: ["gachaPity"]
    });
    var pity = 0;
    if (pityData.Data && pityData.Data.gachaPity) {
        pity = parseInt(pityData.Data.gachaPity.Value, 10) || 0;
    }

    var results = [];
    for (var r = 0; r < rollCount; r++) {
        pity++;
        var grade = pity >= PITY_LIMIT ? "legendary" : rollGrade();
        if (grade === "legendary") {
            pity = 0;
        }

        var pool = poolByGrade(grade);
        var picked = pool[Math.floor(Math.random() * pool.length)];

        var prizeCoins = GACHA_COIN_PRIZES[picked.id];
        var isDuplicate = !!ownedItemIds[picked.id];
        if (prizeCoins) {
            server.AddUserVirtualCurrency({
                PlayFabId: currentPlayerId,
                VirtualCurrency: "SC",
                Amount: prizeCoins
            });
            results.push({ itemId: picked.id, grade: grade, duplicate: false, coins: prizeCoins });
        } else if (GACHA_MATERIAL_PRIZES[picked.id]) {
            var prizeCount = GACHA_MATERIAL_PRIZES[picked.id];
            server.GrantItemsToUser({
                PlayFabId: currentPlayerId,
                ItemIds: repeatItem(picked.id, prizeCount)
            });
            results.push({ itemId: picked.id, grade: grade, duplicate: false, count: prizeCount });
        } else if (isDuplicate) {
            var refund = GRADE_DUP_REFUND[grade];
            server.AddUserVirtualCurrency({
                PlayFabId: currentPlayerId,
                VirtualCurrency: "SC",
                Amount: refund
            });
            results.push({ itemId: picked.id, grade: grade, duplicate: true, refund: refund });
        } else {
            server.GrantItemsToUser({
                PlayFabId: currentPlayerId,
                ItemIds: [picked.id]
            });
            ownedItemIds[picked.id] = true;
            results.push({ itemId: picked.id, grade: grade, duplicate: false });
        }
    }

    server.UpdateUserInternalData({
        PlayFabId: currentPlayerId,
        Data: { gachaPity: pity.toString() }
    });

    return { results: results, pity: pity };
};

// ---- 내부 유틸 ----

function loadStarterStamp() {
    var data = server.GetUserInternalData({
        PlayFabId: currentPlayerId,
        Keys: [STARTER_KIT_DATA_KEY]
    });
    if (data.Data && data.Data[STARTER_KIT_DATA_KEY]) {
        return JSON.parse(data.Data[STARTER_KIT_DATA_KEY].Value);
    }
    return null;
}

function saveStarterStamp(stamp) {
    var payload = {};
    payload[STARTER_KIT_DATA_KEY] = JSON.stringify(stamp);
    server.UpdateUserInternalData({
        PlayFabId: currentPlayerId,
        Data: payload
    });
}

function loadRanch() {
    var data = server.GetUserInternalData({
        PlayFabId: currentPlayerId,
        Keys: ["ranch"]
    });
    if (data.Data && data.Data.ranch) {
        return JSON.parse(data.Data.ranch.Value);
    }
    var empty = [];
    for (var i = 0; i < RANCH_SLOT_COUNT; i++) empty.push(null);
    return empty;
}

function saveRanch(ranch) {
    server.UpdateUserInternalData({
        PlayFabId: currentPlayerId,
        Data: { ranch: JSON.stringify(ranch) }
    });
}

function loadFishing() {
    var data = server.GetUserInternalData({
        PlayFabId: currentPlayerId,
        Keys: ["fishing"]
    });
    if (data.Data && data.Data.fishing) {
        return JSON.parse(data.Data.fishing.Value);
    }
    var empty = [];
    for (var i = 0; i < FISHING_SLOT_COUNT; i++) empty.push(null);
    return empty;
}

function saveFishing(fishing) {
    server.UpdateUserInternalData({
        PlayFabId: currentPlayerId,
        Data: { fishing: JSON.stringify(fishing) }
    });
}

function addToCodex(id) {
    if (!id) return;
    var data = server.GetUserInternalData({
        PlayFabId: currentPlayerId,
        Keys: ["codex"]
    });
    // 칸 가드가 서기 전에 쌓인 칸 밖 ID(고기·가공품 등)는 여기서 버린다 — 상한을 그것들이 막지 않게,
    // 저장할 때 정리된 목록으로 덮어쓴다(M13-15). 읽는 쪽 `codexProgress` 는 칸 목록으로만 세서 원래 안 흔들린다.
    var codex = ((data.Data && data.Data.codex) ? JSON.parse(data.Data.codex.Value) : []).filter(isCodexSlot);
    if (codex.indexOf(id) !== -1) return;
    if (!isCodexSlot(id)) return; // 고기·가죽·pet_*·가축 등 칸이 아닌 ID 는 담지 않는다 — 상한을 칸 밖 ID 가 먼저 채우지 않게
    if (codex.length >= CODEX_MAX_ENTRIES) return;
    codex.push(id);
    server.UpdateUserInternalData({
        PlayFabId: currentPlayerId,
        Data: { codex: JSON.stringify(codex) }
    });
    grantCodexCollars(codex);
}

function isCodexSlot(id) {
    return Object.keys(CODEX_CATEGORIES).some(function (n) { return CODEX_CATEGORIES[n].indexOf(id) !== -1; });
}

// ---- 업적 (DESIGN 8.15) ----
// 표 사본 — 한 줄에 하나. test_cloudscript.js 가 설계서 표와, C++ 2층(SnarehavenAchievementTest)이 DT 와 맞댄다.
var ACHIEVEMENTS = [
    { id: "ach.capture_1", counter: "capture", target: 1, coins: 10 },
    { id: "ach.capture_10", counter: "capture", target: 10, coins: 30 },
    { id: "ach.capture_50", counter: "capture", target: 50, coins: 80 },
    { id: "ach.hunt_10", counter: "hunt", target: 10, coins: 10 },
    { id: "ach.hunt_100", counter: "hunt", target: 100, coins: 40 },
    { id: "ach.fish_1", counter: "fish", target: 1, coins: 5 },
    { id: "ach.fish_50", counter: "fish", target: 50, coins: 40 },
    { id: "ach.harvest_10", counter: "harvest", target: 10, coins: 10 },
    { id: "ach.harvest_100", counter: "harvest", target: 100, coins: 50 },
    { id: "ach.craft_10", counter: "craft", target: 10, coins: 15 },
    { id: "ach.house_1", counter: "house", target: 1, coins: 35 },
    { id: "ach.codex_50", counter: "codexPercent", target: 50, coins: 50 },
    { id: "ach.codex_100", counter: "codexPercent", target: 100, coins: 100 }
];
var ACHIEVEMENTS_DATA_KEY = "achievements";

/**
 * 성공한 판정 뒤에 한 번 부른다. 카운터를 더하고 도감 %를 덮어쓴 뒤, 목표에 처음 닿은 업적만
 * `done` 에 적고(먼저) 코인을 준다(나중) — 끊기면 잃을 수는 있어도 두 번 받지 않는다.
 * @return 이번에 달성한 업적 ID 목록
 */
function bumpAchievements(deltas, petKinds) {
    var state = loadData(currentPlayerId, ACHIEVEMENTS_DATA_KEY, { counters: {}, done: [] });
    Object.keys(deltas).forEach(function (k) { state.counters[k] = (state.counters[k] || 0) + deltas[k]; });
    if (petKinds && petKinds.length > 0) { // 스토리 9단계 「서로 다른 동물 3종」 — 잡아 본 종(펫 아이템) 목록, 개수는 counters.petKinds
        var kinds = state.petKinds || [];
        petKinds.forEach(function (id) { if (kinds.indexOf(id) === -1 && kinds.length < CODEX_MAX_ENTRIES) kinds.push(id); });
        state.petKinds = kinds;
        state.counters.petKinds = kinds.length;
    }
    var cats = codexProgress(loadData(currentPlayerId, "codex", [])).categories;
    var have = 0, total = 0;
    Object.keys(cats).forEach(function (n) {
        if (CODEX_REWARD_EXCLUDED.indexOf(n) === -1) { have += cats[n].have; total += cats[n].total; }
    });
    state.counters.codexPercent = Math.max(state.counters.codexPercent || 0, Math.floor(have * 100 / total));

    var reached = ACHIEVEMENTS.filter(function (a) {
        return state.done.indexOf(a.id) === -1 && (state.counters[a.counter] || 0) >= a.target;
    });
    if (reached.length === 0 && Object.keys(deltas).length === 0) return [];
    reached.forEach(function (a) { state.done.push(a.id); });
    saveData(currentPlayerId, ACHIEVEMENTS_DATA_KEY, state);
    var coins = reached.reduce(function (s, a) { return s + a.coins; }, 0);
    if (coins > 0) server.AddUserVirtualCurrency({ PlayFabId: currentPlayerId, VirtualCurrency: "SC", Amount: coins });
    return reached.map(function (a) { return a.id; });
}

function repeatItem(itemId, count) {
    var arr = [];
    for (var i = 0; i < count; i++) arr.push(itemId);
    return arr;
}

function poolByGrade(grade) {
    var out = [];
    for (var i = 0; i < GACHA_POOL.length; i++) {
        if (GACHA_POOL[i].grade === grade) out.push(GACHA_POOL[i]);
    }
    return out;
}

function rollGrade() {
    var roll = Math.random() * 100;
    var acc = 0;
    var grades = ["common", "rare", "epic", "legendary"];
    for (var i = 0; i < grades.length; i++) {
        acc += GRADE_WEIGHT[grades[i]];
        if (roll < acc) return grades[i];
    }
    return "common";
}

// =====================================================================================
// 레퍼런스 원본 맞춤 판정 (DESIGN 8.3 · 8.9 ~ 8.14, 2026-09-24)
// 값은 전부 설계서 표의 사본이다 — 손으로 고치지 말고 표를 고친 뒤 옮긴다.
// `tools/playfab/test_cloudscript.js` 가 표 줄을 직접 읽어 이 상수들과 맞대 본다.
//
// 공통 약속
// - 확인을 전부 끝낸 뒤에만 바꾼다. 확인 하나라도 실패하면 아무것도 안 바꾸고 `{error, code}` 를 돌려준다.
// - 옮길 때는 **내보내는 쪽을 먼저 지우고, 받는 쪽에 나중에 넣는다.** 중간에 호출이 끊기면 잃을 수는 있어도
//   복사는 안 생긴다(가방·보관함·상점 모두 같은 순서).
// - 시간 규칙은 다음 호출 때 지나간 시간으로 계산한다(HarvestRanch 와 같은 방식, 서버 타이머 없음).
// =====================================================================================

var HOUR_MS = 60 * 60 * 1000;
var COUNT_MAX = 999; // 한 호출 수량 상한 — 설계서 값 아님, 잘못된 입력 거르기용

// ---- 8.12 배낭·버리기·사망 ----
var BACKPACK_SLOTS = 20;
var STACK_MAX = 99;
var BACKPACK_BONUS_ITEM = "item.hiking_backpack";
var BACKPACK_BONUS_SLOTS = 8;
// 8.9 배 짐칸 — 원본(VN461)처럼 배에 붙은 보관함 8칸. 배낭 칸은 안 는다. 가지고 있으면 계정당 하나,
// 주인만 연다. 놓인 보관함처럼 서버는 거리(탑승)를 안 보고 클라 UI 가 배 위에서만 연다 (M32-02, 감독 보정 10-06).
var BOAT_CONTAINER_ITEM = "item.boat_container";
var BOAT_CONTAINER_SLOTS = 8;
var BOAT_STORAGE_DATA_KEY = "boatStorage";
var STORAGE_SLOTS = 20;
var DROP_TTL_SECONDS = 600;
var DROPS_DATA_KEY = "drops";

// ---- 8.11 건축·거점 ----
var TENT_ITEM = "item.tent";
var TENT_MAX = 2;
var TENT_ZONES = ["ranch", "hunt"];
var LAND_CELLS = 16; // 텐트 중심 16 × 16 칸
var FURNITURE_MAX = 40;
// 8.11 「집 2층·전력」 2층 — 집(upgraded)에 즉시·기한 없이, 가구 한도 40 → 60 (M20-03)
var FURNITURE_MAX_FLOOR2 = 60;
var FLOOR2_COST = [
    { itemId: "item.stair_kit", count: 1 },
    { itemId: "item.plank", count: 40 },
    { itemId: "item.cedar_plank", count: 20 },
    { itemId: "item.sheet_metal", count: 4 }
];
var UPGRADE_DEADLINE_HOURS = 24;
var UPGRADE_COST = [
    { itemId: "item.wood", count: 150 },
    { itemId: "item.cedar", count: 50 }
];
var HOUSE_COLOR_COUNT = 5;
var UPKEEP_ITEM = "item.wood";
var UPKEEP_HOURS_PER_WOOD = 2;
var UPKEEP_MAX_HOURS = 200;
var UPKEEP_START_HOURS = 24; // 🟡 설계서에 없음: 집이 된 순간 유지 0 이면 바로 사라지므로 하루치를 얹어 준다
var PIN_PATTERN = /^[0-9]{5}$/;
var STORAGE_ITEM = "item.storage_box";
// 🟡 가챠 목장 데코는 배치 판정을 아직 안 받는다(10.3 — 발자국 크기 표가 서면 더한다).
var PLACEABLE_ITEMS = [
    "item.bench", "item.chair_wood", "item.table_wood", "item.fence_wood", "item.storage_box",
    "item.campfire", "item.grill", "item.workbench_adv", "item.garden_brazier", "item.smelter",
    // 8.11 「집 2층·전력」 — 공급(키트·배터리)·소모(전등)는 놓인 목록으로 각 기기가 센다 (M20-04a)
    "item.solar_kit", "item.battery", "item.lamp_electric",
    // M108-04 집 가구 — 꾸미기만 (DESIGN 8.11 「건축가 꾸미기」 가구 종류)
    "item.armchair", "item.sofa", "item.table_round", "item.table_long", "item.bed_single",
    "item.bed_double", "item.bookshelf", "item.dresser", "item.kitchen_counter", "item.kitchen_sink",
    // M108-05a 식물 넷
    "item.potted_tree", "item.potted_berry", "item.flower_box", "item.potted_sprout",
    // M108-05b 러그 넷 — 바닥 가구 밑에 깔린다(아래 `RUG_ITEMS`)
    "item.rug_round", "item.rug_runner", "item.rug_square", "item.doormat",
    // M108-05d 바깥 넷 — 디딤 길은 러그 층(`RUG_ITEMS`)
    "item.fence_picket", "item.path_log", "item.garden_arch", "item.mailbox",
    // M108-05e 조명 넷 — 비전기 불빛. 켜짐은 놓인 목록으로 각 기기가 센다(전등과 같은 길), 서버는 놓기만
    "item.candle_stand", "item.oil_lamp", "item.lantern_post", "item.garden_torch",
    // M108-06 계단 — 2층 집 안 한 줄(1×3)만 (아래 `stairPlaceFail`)
    "item.stairs_wood",
    // M108-07 연료 발전기 — 놓기는 바닥 가구 그대로, 연료는 `FeedGenerator`
    "item.generator",
    // M108-08 전기 가구 넷 — 냉장고·선풍기는 바닥 가구, 라디오는 위에 올리기(`TOP_ITEMS`), 네온 간판은 벽 걸기(`WALL_ITEMS`).
    // 소모·켜짐은 놓인 목록으로 각 기기가 센다(전등과 같은 길), 서버는 놓기만
    "item.fridge", "item.fan_stand", "item.radio", "item.neon_sign"
];
// M108-05b 러그 — 바닥 가구와 같은 칸에 함께 놓인다(러그 위 가구 · 가구 밑 러그, 놓는 순서 상관없음). 러그끼리만 겹침을 본다.
// 가구 한도에선 1개로 센다 (DESIGN 8.11 「러그 깔기」)
// M108-05d 통나무 디딤 길도 같은 층 — 아치·울타리 밑에 깔린다
var RUG_ITEMS = ["item.rug_round", "item.rug_runner", "item.rug_square", "item.doormat", "item.path_log"];
// M108-02 벽 장식 — 집(upgraded) 3×3 발자국 바깥 변에 걸린다. 가구 한도에서는 0.5개로 센다 (DESIGN 8.11 「건축가 꾸미기」)
// M108-05c 넷 더함(아치 거울·섬 지도·닻·들꽃 화환) — 같은 걸기 판정
var WALL_ITEMS = ["item.picture_frame", "item.wall_clock", "item.wall_shelf",
    "item.wall_mirror", "item.wall_map", "item.wall_anchor", "item.wall_wreath",
    "item.neon_sign"]; // M108-08 전기 가구 — 벽에 거는 네온 간판
var WALL_HEIGHT_MIN = 120;
var WALL_HEIGHT_MAX = 200;
var WALL_ITEM_GAP = 50; // 같은 벽 칸에서 두 장식 높이가 이만큼은 벌어져야 한다 (소품 가로 40cm 이하)
var HOUSE_CELLS = 3;
// M108-03 위에 올리기 — 소품은 받침 가구 위 슬롯에만 놓인다. 값 = 받침 하나의 슬롯 수(탁자 폭 145 → 3, 보관함 80 → 2, 소품 가로 40 이하).
// M108-04c 집 가구 받침 — 둥근 탁자 90 → 2 · 긴 식탁 190 → 3(상한) · 서랍장 93 → 2 · 조리대 97 → 2. 개수대·책장은 받침 아님(통·높이).
// 가구 한도에서는 벽 장식처럼 0.5개로 센다 (DESIGN 8.11 「건축가 꾸미기」)
var TOP_HOSTS = { "item.table_wood": 3, "item.storage_box": 2, "item.table_round": 2, "item.table_long": 3, "item.dresser": 2, "item.kitchen_counter": 2 };
var TOP_ITEMS = ["item.mug", "item.flower_pot", "item.candle",
    "item.radio"]; // M108-08 전기 가구 — 탁자·수납 위 라디오(가로 36)
// M108-06 계단·위층 (DESIGN 8.11 「계단·위층」, 명세 `docs/specs/house-stairs.md`). 집 칸 좌표 ox,oy 0~2 = 집 3×3 발자국(앵커 = 텐트 칸),
// 문 = 앞(-X) 가운데 칸 (0, 1). 계단 발자국 3×1(긴 변 X — 긴 복도 러그와 같음)은 돌림 0·180 이면 X 로 길고, 90·270 이면 Y 로 길다(클라 `RotatedFootprint`).
// 놓을 자리 6 = X 로 긴 왼·오른 줄(oy 0·2) × 오르는 쪽 2 + Y 로 긴 뒷줄(ox 2) × 오르는 쪽 2.
// 문 칸을 덮는 앞줄(ox 0)·가운데 세로줄(oy 1), 방을 앞뒤로 가르는 가운데 가로줄(ox 1)은 `blocks_path`.
var STAIRS_ITEM = "item.stairs_wood";
var STAIRS_LENGTH = 3;
// M108-07 연료 발전기 (DESIGN 8.11 「연료 발전기 숫자」, 명세 `docs/specs/fuel-generator.md`). 놓인 줄의 `fuelUntilUtc` 하나만 저장하고
// 호출 때 남은 시간을 센다(집 유지와 같은 길). 연료는 늘 탄다. 출력·켜짐은 놓인 목록으로 각 기기가 센다(전등과 같은 길)
var GENERATOR_ITEM = "item.generator";
var GENERATOR_FUEL_MINUTES = { "item.wood": 10, "item.coal_chunk": 30 };
var GENERATOR_FUEL_MAX_HOURS = 12;
var BASE_DATA_KEY = "base";

// ---- 8.10 제작·요리 ----
// 생선구이 회복량 = DESIGN 8.4 「레퍼런스 조리 회복량」 열. `—` 인 붕어·잉어는 10. 우파루파 구이는 13.7 요리 44 에 있다.
var GRILLED_HEAL = {
    "item.crucian_carp": 10, "item.salmon": 10, "item.cave_crab": 13, "item.carp": 10, "item.perch": 15,
    "item.pike": 25, "item.whitefish": 30, "item.haddock": 30, "item.cod": 30, "item.catfish": 30,
    "item.trout": 35, "item.tuna": 35, "item.stingray": 50, "item.rainbow_trout": 60, "item.redfish": 60,
    "item.black_grouper": 60, "item.arctic_char": 70, "item.swordfish": 70, "item.misty_grouper": 80,
    "item.salmon_shark": 90, "item.eel": 60
};
// 키 = 거두는 아이템 ID. 앞 셋은 DESIGN 8.2, 나머지 24는 13.3 표(코인 = 재배 분 × 0.33).
var CROP_HARVEST_COINS = {
    "item.carrot": 5, "item.potato": 10, "item.wheat": 20,
    "item.berry": 5, "item.blueberry": 5, "item.lingonberry": 5, "item.blackberry": 5,
    "item.mezereon": 10, "item.sugar_beet": 10, "item.onion": 10, "item.cucumber": 10, "item.chili": 10,
    "item.cabbage": 10, "item.kingcup": 10, "item.windflower": 10, "item.hepatica": 10, "item.poppy": 10,
    "item.fireweed": 10,
    "item.pumpkin": 20, "item.cotton": 20, "item.canola": 20, "item.tea_leaves": 20, "item.coffee_beans": 20,
    "item.wood": 20,
    "item.cloudberry": 40, "item.black_lotus": 40, "item.apple": 40
};
var CROP_CYCLE_MINUTES = {
    "item.carrot": 15, "item.potato": 30, "item.wheat": 60,
    "item.berry": 15, "item.blueberry": 15, "item.lingonberry": 15, "item.blackberry": 15,
    "item.mezereon": 30, "item.sugar_beet": 30, "item.onion": 30, "item.cucumber": 30, "item.chili": 30,
    "item.cabbage": 30, "item.kingcup": 30, "item.windflower": 30, "item.hepatica": 30, "item.poppy": 30,
    "item.fireweed": 30,
    "item.pumpkin": 60, "item.cotton": 60, "item.canola": 60, "item.tea_leaves": 60, "item.coffee_beans": 60,
    "item.wood": 60,
    "item.cloudberry": 120, "item.black_lotus": 120, "item.apple": 120
};
// 씨앗 ID 는 `item.seed_<작물>` 이 기본, 13.3 표에서 이름이 다른 셋만 여기 적는다.
var CROP_SEED_EXCEPTIONS = { "item.tea_leaves": "item.seed_tea", "item.coffee_beans": "item.seed_coffee", "item.wood": "item.seed_oak" };
var CROP_HARVEST_COUNT = { "item.wood": 4 }; // 13.3 참나무 = 나무 4개. 나머지는 1개
// 13.3 에서 새로 든 씨앗 — 잡화점이 판다(가격 = 작물 판매가). 8.2 당근·감자·밀 씨앗은 제작(작물 4개)과 잡화점 둘 다 — 첫 작물이 없는 새 계정이 스토리 2장 3단계를 풀 길(DESIGN 8.14 사는 목록, 루프#1238).
// 값 = 13.3 표 「작물」 열 — 상점 줄 이름은 "<작물> 씨앗".
var CROP_SHOP_SEED_NAMES = {
    "item.carrot": "당근", "item.potato": "감자", "item.wheat": "밀",
    "item.berry": "산딸기", "item.blueberry": "블루베리", "item.lingonberry": "링곤베리", "item.blackberry": "블랙베리",
    "item.mezereon": "서향 열매", "item.sugar_beet": "사탕무", "item.onion": "양파", "item.cucumber": "오이",
    "item.chili": "고추", "item.cabbage": "양배추", "item.kingcup": "메리골드", "item.windflower": "아네모네",
    "item.hepatica": "노루귀", "item.poppy": "양귀비", "item.fireweed": "분홍바늘꽃",
    "item.pumpkin": "호박", "item.cotton": "목화", "item.canola": "카놀라", "item.tea_leaves": "차",
    "item.coffee_beans": "커피", "item.wood": "참나무",
    "item.cloudberry": "클라우드베리", "item.black_lotus": "검은 연꽃", "item.apple": "사과나무"
};
function seedOf(cropId) { return CROP_SEED_EXCEPTIONS[cropId] || "item.seed_" + cropId.slice(5); }
// 🟡 전설의 발톱은 가죽 자리(고급 작업대 안장 `Hide` 4 등)에 1개 = 1개로 들어간다 — 성능이 아니라 "더 빨리"(10.3).
var GACHA_HIDE_ITEMS = ["gacha.mat_hide_5", "gacha.mat_silver_hide_3", "gacha.mat_gold_hide_2", "gacha.mat_legend_claw_1"];
// 아이템 ID 재료의 가챠 대체품 — 레시피 표는 그대로 두고 그 재료가 모자랄 때 맨 나중에 쓴다(나무 조각 = 나무, M35-03).
var GACHA_SUBSTITUTES = { "item.wood": "gacha.mat_wood_5" };

// 결과 ID = 레시피 ID. place: workbench(내 텐트 작업대) · anywhere · fire. 재료는 아이템 ID 또는 태그.
var RECIPES = {
    "item.plank": { place: "workbench", inputs: [{ itemId: "item.wood", count: 4 }] },
    "item.cedar_plank": { place: "workbench", inputs: [{ itemId: "item.cedar", count: 4 }] },
    "item.bench": { place: "workbench", inputs: [{ itemId: "item.plank", count: 2 }, { itemId: "item.cedar_plank", count: 1 }] },
    "item.chair_wood": { place: "workbench", inputs: [{ itemId: "item.plank", count: 2 }] },
    "item.table_wood": { place: "workbench", inputs: [{ itemId: "item.plank", count: 3 }, { itemId: "item.cedar_plank", count: 1 }] },
    "item.fence_wood": { place: "workbench", inputs: [{ itemId: "item.plank", count: 2 }] },
    "item.storage_box": { place: "workbench", inputs: [{ itemId: "item.plank", count: 6 }, { itemId: "item.cedar_plank", count: 2 }] },
    // M108-04 집 가구 (DESIGN 8.10 표 · 8.11 「건축가 꾸미기」) — 무명천 = 원본 「가구 제작에 사용」
    "item.armchair": { place: "workbench", inputs: [{ itemId: "item.plank", count: 2 }, { itemId: "item.cotton_fabric", count: 2 }] },
    "item.sofa": { place: "workbench", inputs: [{ itemId: "item.plank", count: 3 }, { itemId: "item.cotton_fabric", count: 4 }] },
    "item.table_round": { place: "workbench", inputs: [{ itemId: "item.plank", count: 2 }, { itemId: "item.cedar_plank", count: 1 }] },
    "item.table_long": { place: "workbench", inputs: [{ itemId: "item.plank", count: 5 }, { itemId: "item.cedar_plank", count: 2 }] },
    "item.bed_single": { place: "workbench", inputs: [{ itemId: "item.plank", count: 4 }, { itemId: "item.cotton_fabric", count: 3 }] },
    "item.bed_double": { place: "workbench", inputs: [{ itemId: "item.plank", count: 6 }, { itemId: "item.cotton_fabric", count: 5 }] },
    "item.bookshelf": { place: "workbench", inputs: [{ itemId: "item.plank", count: 5 }, { itemId: "item.cedar_plank", count: 1 }] },
    "item.dresser": { place: "workbench", inputs: [{ itemId: "item.plank", count: 4 }, { itemId: "item.cedar_plank", count: 2 }] },
    "item.kitchen_counter": { place: "workbench", inputs: [{ itemId: "item.plank", count: 4 }, { itemId: "item.cedar_plank", count: 1 }, { itemId: "item.metal_parts", count: 1 }] },
    "item.kitchen_sink": { place: "workbench", inputs: [{ itemId: "item.plank", count: 4 }, { itemId: "item.metal_parts", count: 2 }] },
    "item.potted_tree": { place: "workbench", inputs: [{ itemId: "item.plank", count: 2 }, { itemId: "item.seed_oak", count: 1 }] },
    "item.potted_berry": { place: "workbench", inputs: [{ itemId: "item.plank", count: 1 }, { itemId: "item.seed_berry", count: 1 }] },
    "item.flower_box": { place: "workbench", inputs: [{ itemId: "item.plank", count: 2 }, { itemId: "item.poppy", count: 2 }, { itemId: "item.kingcup", count: 2 }] },
    "item.potted_sprout": { place: "workbench", inputs: [{ itemId: "item.plank", count: 1 }, { itemId: "item.seed_tea", count: 1 }] },
    "item.rug_round": { place: "workbench", inputs: [{ itemId: "item.cotton_fabric", count: 3 }] },
    "item.rug_runner": { place: "workbench", inputs: [{ itemId: "item.cotton_fabric", count: 2 }] },
    "item.rug_square": { place: "workbench", inputs: [{ itemId: "item.cotton_fabric", count: 5 }, { itemId: "item.poppy", count: 2 }] },
    "item.doormat": { place: "workbench", inputs: [{ itemId: "item.wheat", count: 4 }] },
    "item.picture_frame": { place: "workbench", inputs: [{ itemId: "item.plank", count: 1 }, { itemId: "item.cotton_fabric", count: 1 }] },
    "item.wall_clock": { place: "workbench", inputs: [{ itemId: "item.plank", count: 1 }, { itemId: "item.metal_parts", count: 1 }] },
    "item.wall_shelf": { place: "workbench", inputs: [{ itemId: "item.plank", count: 2 }] },
    "item.mug": { place: "workbench", inputs: [{ itemId: "item.plank", count: 1 }] },
    "item.flower_pot": { place: "workbench", inputs: [{ itemId: "item.plank", count: 1 }, { itemId: "item.kingcup", count: 1 }] },
    "item.candle": { place: "workbench", inputs: [{ itemId: "item.sheet_metal", count: 1 }, { itemId: "item.honey", count: 1 }] },
    "item.wall_mirror": { place: "workbench", inputs: [{ itemId: "item.plank", count: 1 }, { itemId: "item.metal_parts", count: 1 }] },
    "item.wall_map": { place: "workbench", inputs: [{ itemId: "item.cotton_fabric", count: 1 }, { itemId: "item.plank", count: 1 }] },
    "item.wall_anchor": { place: "workbench", inputs: [{ itemId: "item.iron_ingot", count: 2 }, { itemId: "item.cotton_fabric", count: 1 }] },
    "item.wall_wreath": { place: "workbench", inputs: [{ itemId: "item.wood", count: 1 }, { itemId: "item.poppy", count: 2 }, { itemId: "item.kingcup", count: 2 }] },
    "item.fence_picket": { place: "workbench", inputs: [{ itemId: "item.plank", count: 3 }] },
    "item.path_log": { place: "workbench", inputs: [{ itemId: "item.wood", count: 2 }] },
    "item.garden_arch": { place: "workbench", inputs: [{ itemId: "item.plank", count: 4 }, { itemId: "item.poppy", count: 1 }, { itemId: "item.kingcup", count: 1 }] },
    "item.mailbox": { place: "workbench", inputs: [{ itemId: "item.plank", count: 1 }, { itemId: "item.sheet_metal", count: 1 }] },
    "item.candle_stand": { place: "workbench", inputs: [{ itemId: "item.plank", count: 2 }, { itemId: "item.honey", count: 1 }] },
    "item.oil_lamp": { place: "workbench", inputs: [{ itemId: "item.plank", count: 1 }, { itemId: "item.vegetable_oil", count: 1 }] },
    "item.lantern_post": { place: "workbench", inputs: [{ itemId: "item.plank", count: 2 }, { itemId: "item.sheet_metal", count: 1 }, { itemId: "item.honey", count: 1 }] },
    "item.garden_torch": { place: "workbench", inputs: [{ itemId: "item.wood", count: 1 }, { itemId: "item.resin", count: 1 }] },
    "item.campfire": { place: "anywhere", inputs: [{ itemId: "item.wood", count: 5 }] },
    "item.berry_bait": { place: "anywhere", yield: 3, inputs: [{ itemId: "item.berry", count: 3 }] },
    "item.rod_bait": { place: "workbench", blueprint: "item.blueprint_rod_bait", inputs: [{ itemId: "item.plank", count: 4 }, { tag: "Feather", count: 2 }] },
    "item.rod_lucky": { place: "workbench", blueprint: "item.blueprint_rod_lucky", inputs: [{ itemId: "item.plank", count: 4 }, { tag: "Hide", count: 2 }] },
    "item.grill": { place: "workbench", blueprint: "item.blueprint_grill", inputs: [{ itemId: "item.plank", count: 6 }, { tag: "Hide", count: 2 }] },
    "item.workbench_adv": { place: "workbench", blueprint: "item.blueprint_workbench_adv", inputs: [{ itemId: "item.plank", count: 10 }, { itemId: "item.cedar_plank", count: 4 }] },
    "item.saddle_hunting": { place: "workbench", advWorkbench: true, inputs: [{ itemId: "item.plank", count: 4 }, { tag: "Hide", count: 4 }] },
    "item.saddle_farming": { place: "workbench", advWorkbench: true, inputs: [{ itemId: "item.plank", count: 4 }, { tag: "Hide", count: 4 }] },
    "item.saddle_fishing": { place: "workbench", advWorkbench: true, inputs: [{ itemId: "item.plank", count: 4 }, { tag: "Hide", count: 4 }] },
    // 13.8 부품 4 + 텐트 제작 (M13-09). 첫 텐트는 상점(35 코인) — 작업대 제작은 텐트가 선 뒤에만 된다.
    // 접착제·원단은 배낭에서도 (8.10 「작업대 없이 되는 것」, M22 ⑥).
    "item.resin_adhesive": { place: "anywhere", inputs: [{ itemId: "item.resin", count: 2 }] },
    "item.cotton_fabric": { place: "anywhere", inputs: [{ itemId: "item.cotton", count: 3 }] },
    "item.tarp": { place: "workbench", inputs: [{ itemId: "item.cotton_fabric", count: 2 }, { itemId: "item.resin_adhesive", count: 1 }] },
    "item.life_jacket": { place: "workbench", inputs: [{ itemId: "item.cotton_fabric", count: 2 }, { itemId: "item.tarp", count: 1 }] },
    "item.tent": { place: "workbench", inputs: [{ itemId: "item.tarp", count: 4 }, { itemId: "item.plank", count: 6 }] },
    "item.roast_meat": { place: "fire", heal: 30, inputs: [{ tag: "Meat", count: 1 }] },
    "item.berry_jam": { place: "fire", heal: 10, inputs: [{ itemId: "item.berry", count: 3 }] },
    // 13.6 요리 재료 — 모닥불 요리(원본 「요리」, 8.10 10-06 보정). 찻잎 주머니은 제작 없음(상자 전리품). heal = 13.6 「날로 먹기」 열
    "item.flour": { place: "fire", inputs: [{ itemId: "item.wheat", count: 2 }] },
    "item.sugar": { place: "fire", heal: 8, inputs: [{ itemId: "item.sugar_beet", count: 2 }] },
    "item.vegetable_oil": { place: "fire", heal: 5, inputs: [{ itemId: "item.canola", count: 3 }] },
    // 13.6 버프 소모품 — heal = 「체력」 열(0 = 버프만). 버프 값은 호스트가 DT_ItemTable 에서 읽는다.
    "item.energy_drink_mild": { place: "fire", heal: 0, inputs: [{ itemId: "item.water_bottle", count: 1 }, { itemId: "item.sugar", count: 1 }] },
    "item.energy_drink": { place: "fire", heal: 5, inputs: [{ itemId: "item.water_bottle", count: 1 }, { itemId: "item.sugar", count: 1 }, { itemId: "item.coffee_beans", count: 1 }] },
    "item.energy_drink_strong": { place: "fire", heal: 5, inputs: [{ itemId: "item.water_bottle", count: 1 }, { itemId: "item.sugar", count: 2 }, { itemId: "item.coffee_beans", count: 2 }] },
    "item.coffee_bottle": { place: "fire", heal: 15, inputs: [{ itemId: "item.water_bottle", count: 1 }, { itemId: "item.coffee_beans", count: 2 }] },
    "item.tea_bottle": { place: "fire", heal: 15, inputs: [{ itemId: "item.water_bottle", count: 1 }, { itemId: "item.tea_bag", count: 1 }] },
    "item.canned_fish": { place: "workbench", heal: 25, inputs: [{ tag: "Fish", count: 2 }] },
    // 8.10 늪 꽃 물감 (M33-02) — 상점 페인트의 둘째 길. 판매가는 상점 물건이라 0 그대로(sellPriceOf 첫 줄)
    "item.paint_blue": { place: "workbench", inputs: [{ itemId: "item.bog_flower", count: 1 }] },
    "item.paint_blush": { place: "workbench", inputs: [{ itemId: "item.water_lily", count: 1 }] },
    // 8.16 채광·제련 (M14-01). 주괴 = 제련소 옆 — 텐트 땅에 제련소가 놓여 있어야 한다.
    "item.metal_parts": { place: "workbench", yield: 2, inputs: [{ itemId: "item.iron_ingot", count: 1 }] },
    "item.sheet_metal": { place: "workbench", inputs: [{ itemId: "item.iron_ingot", count: 2 }] },
    "item.pickaxe_advanced": { place: "workbench", advWorkbench: true, inputs: [{ itemId: "item.iron_ingot", count: 3 }, { itemId: "item.plank", count: 2 }] },
    "item.smelter": { place: "workbench", inputs: [{ itemId: "item.plank", count: 4 }, { itemId: "item.iron_chunk", count: 6 }] },
    // 8.11 「집 2층·전력」 전자 재료 (M20-02). 폴리머만 작업대, 나머지는 고급 작업대.
    "item.polymer": { place: "workbench", inputs: [{ itemId: "item.resin", count: 3 }] },
    "item.circuit_board": { place: "workbench", advWorkbench: true, inputs: [{ itemId: "item.copper_ingot", count: 2 }, { itemId: "item.polymer", count: 1 }] },
    "item.electric_motor": { place: "workbench", advWorkbench: true, inputs: [{ itemId: "item.copper_ingot", count: 2 }, { itemId: "item.iron_ingot", count: 1 }, { itemId: "item.circuit_board", count: 1 }] },
    "item.solar_kit": { place: "workbench", advWorkbench: true, inputs: [{ itemId: "item.circuit_board", count: 1 }, { itemId: "item.sheet_metal", count: 1 }, { itemId: "item.polymer", count: 1 }] },
    "item.battery": { place: "workbench", advWorkbench: true, inputs: [{ itemId: "item.copper_ingot", count: 1 }, { itemId: "item.coal_ingot", count: 1 }, { itemId: "item.polymer", count: 1 }] },
    // 8.11 「층계 짜맞춤」 — 집 2층 재료 (M20-03)
    "item.stair_kit": { place: "workbench", advWorkbench: true, inputs: [{ itemId: "item.plank", count: 6 }, { itemId: "item.metal_parts", count: 2 }] },
    // 8.11 「계단·위층」 — 층계 짜맞춤이 실제 계단이 된다 (M108-06)
    "item.stairs_wood": { place: "workbench", inputs: [{ itemId: "item.plank", count: 8 }, { itemId: "item.stair_kit", count: 1 }] },
    // 8.11 「전등 레시피」 — 그냥 작업대 (M20-04a)
    "item.lamp_electric": { place: "workbench", inputs: [{ itemId: "item.light_bulb", count: 1 }, { itemId: "item.metal_parts", count: 1 }, { itemId: "item.plank", count: 1 }] },
    // 8.11 「연료 발전기 숫자」 — 소형 전동기 둘째 쓰는 곳 (M108-07)
    "item.generator": { place: "workbench", advWorkbench: true, inputs: [{ itemId: "item.electric_motor", count: 1 }, { itemId: "item.sheet_metal", count: 2 }, { itemId: "item.metal_parts", count: 2 }] },
    // 8.11 「🟡 전기 가구」 — 고급 작업대, 소모 큰 것이 재료 비쌈. 전동기 = 도는 것, 회로판 = 소리·빛 다루는 것 (M108-08)
    "item.fridge": { place: "workbench", advWorkbench: true, inputs: [{ itemId: "item.electric_motor", count: 1 }, { itemId: "item.sheet_metal", count: 3 }, { itemId: "item.polymer", count: 2 }] },
    "item.fan_stand": { place: "workbench", advWorkbench: true, inputs: [{ itemId: "item.electric_motor", count: 1 }, { itemId: "item.metal_parts", count: 2 }, { itemId: "item.polymer", count: 1 }] },
    "item.radio": { place: "workbench", advWorkbench: true, inputs: [{ itemId: "item.circuit_board", count: 1 }, { itemId: "item.plank", count: 2 }, { itemId: "item.metal_parts", count: 1 }] },
    "item.neon_sign": { place: "workbench", advWorkbench: true, inputs: [{ itemId: "item.light_bulb", count: 2 }, { itemId: "item.circuit_board", count: 1 }, { itemId: "item.polymer", count: 2 }] },
    // 8.10 전기 배 추진기 — 배 추진기에 소형 전동기를 다는 고급 작업대 줄 (M33-03, 원본 VN463). 상자 2% 의 둘째 길
    "item.boat_motor_electric": { place: "workbench", advWorkbench: true, inputs: [{ itemId: "item.boat_motor", count: 1 }, { itemId: "item.electric_motor", count: 1 }] },
    // 8.6-c 신식 소총 — 고급 작업대 (M39-08a, 원본 Modern Rifle 「제작」)
    "item.rifle_modern": { place: "workbench", advWorkbench: true, inputs: [{ itemId: "item.iron_ingot", count: 4 }, { itemId: "item.metal_parts", count: 2 }, { itemId: "item.plank", count: 2 }] }
};
var SMELTER_ITEM = "item.smelter";
var ORE_CHUNK_PRICE = { // 8.16 「판매가」 덩어리 줄
    "item.coal_chunk": 1, "item.copper_chunk": 1, "item.iron_chunk": 2,
    "item.titanium_chunk": 4, "item.silver_chunk": 5, "item.gold_chunk": 8
};
// 13.4 「판매가」 늪 채집물 — 늪꽃 3곳 × 4 = 수련 2곳 × 6 = 평원 꽃 6곳 × 2 (시간당 상한이 같게), 독사갈기 = 검은 연꽃 8
var GATHER_SELL_PRICE = { "item.vipers_mane": 8, "item.bog_flower": 4, "item.water_lily": 6 };
Object.keys(ORE_CHUNK_PRICE).forEach(function (chunkId) {
    var metal = chunkId.slice(5, -6);
    RECIPES["item." + metal + "_ingot"] = metal === "coal"
        ? { place: "smelter", inputs: [{ itemId: chunkId, count: 2 }] }
        : { place: "smelter", inputs: [{ itemId: chunkId, count: 2 }, { itemId: "item.coal_chunk", count: 1 }] };
});
// 날로 먹기 — 레시피가 아닌 것. 값 = 13.3 「날로 먹기」 열 + 13.6 표. 음수(서향 열매 독)는 체력을 깎는다.
var RAW_HEAL = {
    "item.potato": 5, "item.blueberry": 10, "item.lingonberry": 12, "item.mezereon": -40, "item.sugar_beet": 8,
    "item.onion": 8, "item.cucumber": 5, "item.cabbage": 10, "item.apple": 10,
    "item.salt": 5, "item.black_pepper": 5, "item.water_bottle": 15,
    "item.hot_soda": 25, "item.heat_pack": 0, "item.snackbar": 25, "item.water_canister": 20, // 13.6 버프 소모품(0 = 버프만)
    "item.fly_amanita": -20 // 13.4 채집 독버섯. 식용 버섯은 요리 재료만이라 여기 없다.
};
// 13.6 물에서 채우기 — 빈 것 → 찬 것.
var FILL_ITEMS = { "item.empty_bottle": "item.water_bottle" };
Object.keys(CROP_HARVEST_COINS).forEach(function (cropId) {
    RECIPES[seedOf(cropId)] = { place: "workbench", inputs: [{ itemId: cropId, count: 4 }] };
});
Object.keys(GRILLED_HEAL).forEach(function (fishId) {
    RECIPES["item.grilled_" + fishId.slice(5)] = { place: "fire", heal: GRILLED_HEAL[fishId], inputs: [{ itemId: fishId, count: 1 }] };
});
// 13.7 요리 44 — 전부 불 1개. [결과, 체력, [[재료 ID 또는 태그, 개수], …]]. 태그 판정 = hasCraftTag.
[
    ["item.bread", 30, [["item.flour", 2]]],
    ["item.ciabatta", 40, [["item.flour", 2], ["Oil", 1]]],
    ["item.ciabatta_topped", 40, [["item.ciabatta", 1], ["Vegetable", 1]]],
    ["item.carrot_bread", 30, [["item.flour", 1], ["item.carrot", 2]]],
    ["item.croissant", 15, [["item.flour", 1], ["item.milk", 1]]],
    ["item.croissant_jam", 15, [["item.croissant", 1], ["Jam", 1]]],
    ["item.cake", 75, [["item.flour", 2], ["item.sugar", 2], ["item.egg", 1], ["item.milk", 1]]],
    ["item.sandwich", 15, [["item.bread", 1], ["Vegetable", 1]]],
    ["item.fish_sandwich", 15, [["item.bread", 1], ["Fish", 1]]],
    ["item.cloudberry_jam", 40, [["item.cloudberry", 3]]],
    ["item.blackberry_jam", 15, [["item.blackberry", 3]]],
    ["item.mezereon_jam", 15, [["item.mezereon", 3]]],
    ["item.blueberry_jam", 15, [["item.blueberry", 3]]],
    ["item.lingonberry_jam", 15, [["item.lingonberry", 3]]],
    ["item.pumpkin_pie", 25, [["item.flour", 1], ["item.pumpkin", 1], ["item.sugar", 1]]],
    ["item.berry_pie", 20, [["item.flour", 1], ["Berry", 2]]],
    ["item.apple_pie", 25, [["item.flour", 1], ["Fruit", 2]]],
    ["item.meat_pie", 25, [["item.flour", 1], ["Meat", 1]]],
    ["item.chanterelle_soup", 70, [["item.chanterelle", 3], ["item.water_bottle", 1]]],
    ["item.fish_soup", 20, [["Fish", 1], ["item.water_bottle", 1]]],
    ["item.meat_soup", 25, [["Meat", 1], ["item.water_bottle", 1]]],
    ["item.hot_pot", 55, [["Meat", 2], ["Vegetable", 2], ["item.water_bottle", 1]]],
    ["item.berry_salad", 20, [["Berry", 3]]],
    ["item.mushroom_salad", 18, [["Mushroom", 2], ["Vegetable", 1]]],
    ["item.roasted_mushrooms", 20, [["Mushroom", 2]]],
    ["item.roasted_vegetables", 15, [["Vegetable", 2]]],
    ["item.stuffed_mushroom", 20, [["item.cave_crab", 1], ["Mushroom", 1]]],
    ["item.cooked_bird", 30, [["item.bird_meat", 1]]],
    ["item.cooked_reindeer", 35, [["item.reindeer_meat", 1]]],
    ["item.cooked_wolf", 30, [["item.wolf_meat", 1]]],
    ["item.seasoned_meat", 15, [["Meat", 1], ["Spice", 1]]],
    ["item.seasoned_fish", 18, [["Fish", 1], ["item.salt", 1]]],
    ["item.sauteed_meat", 20, [["Meat", 1], ["Oil", 1]]],
    ["item.fried_meat", 25, [["Meat", 1], ["item.flour", 1], ["Oil", 1]]],
    ["item.soda_battered_meat", 20, [["Meat", 1], ["item.flour", 1], ["item.hot_soda", 1]]],
    ["item.meat_and_vegetables", 20, [["Meat", 1], ["Vegetable", 1]]],
    ["item.fish_and_vegetables", 20, [["Fish", 1], ["Vegetable", 1]]],
    ["item.fish_and_chips", 15, [["Fish", 1], ["item.potato", 1], ["Oil", 1]]],
    ["item.fish_bowl", 10, [["Fish", 1], ["item.cucumber", 1]]],
    ["item.cabbage_roll", 15, [["item.cabbage", 1], ["Meat", 1]]],
    ["item.onion_rings", 10, [["item.onion", 2], ["item.flour", 1], ["Oil", 1]]],
    ["item.caramelized_carrot", 15, [["item.carrot", 2], ["item.sugar", 1]]],
    ["item.bear_sausage", 65, [["item.bear_meat", 2], ["Spice", 1]]],
    ["item.grilled_axolotl", 20, [["item.axolotl", 1]]]
].forEach(function (dish) {
    RECIPES[dish[0]] = { place: "fire", heal: dish[1], inputs: dish[2].map(function (input) {
        return input[0].indexOf("item.") === 0 ? { itemId: input[0], count: input[1] } : { tag: input[0], count: input[1] };
    }) };
});
// 13.6 「재료 태그 추가」 — 클라이언트 `DT_ItemTable.CraftTag` 와 같은 목록. Fish = FISH_TABLE 전부. Meat·Hide·Feather 는 hasCraftTag 규칙.
var CRAFT_TAG_ITEMS = {
    Berry: ["item.berry", "item.blueberry", "item.lingonberry", "item.blackberry", "item.cloudberry"],
    Fruit: ["item.apple"],
    Jam: ["item.berry_jam", "item.cloudberry_jam", "item.blackberry_jam", "item.mezereon_jam", "item.blueberry_jam", "item.lingonberry_jam"],
    Oil: ["item.vegetable_oil"],
    Spice: ["item.salt", "item.black_pepper", "item.dill", "item.chili"],
    Vegetable: ["item.carrot", "item.potato", "item.onion", "item.cucumber", "item.cabbage", "item.sugar_beet", "item.pumpkin"],
    Mushroom: ["item.chanterelle", "item.penny_bun", "item.yellowleg_bonnet",
        "item.honey_mushroom", "item.king_stropharia", "item.oyster_mushroom", "item.vipers_mane"] // 식용 7종 — 광대버섯 제외(13.6 🟡)
};
var BLUEPRINTS_DATA_KEY = "blueprints";
var BLUEPRINT_PRICE = 100;
var BLUEPRINT_IDS = Object.keys(RECIPES).filter(function (id) { return !!RECIPES[id].blueprint; })
    .map(function (id) { return RECIPES[id].blueprint; });

// ---- 8.13 직접 낚시 ----
var FISH_HABITAT = {
    fresh: ["item.crucian_carp", "item.carp", "item.salmon", "item.perch", "item.pike", "item.whitefish",
        "item.catfish", "item.trout", "item.rainbow_trout", "item.arctic_char", "item.cave_crab", "item.axolotl"],
    sea: ["item.haddock", "item.cod", "item.tuna", "item.stingray", "item.redfish", "item.black_grouper",
        "item.swordfish", "item.misty_grouper", "item.salmon_shark"],
    swamp: ["item.eel"] // 늪 물웅덩이에서만 — 늪 자리는 민물 어종 + 이것
};
var FAR_SEA_MIN_MINUTES = 120;
// 🟡 포인트 ID 는 설계서에 없다 — 자리 수(연못 2 · 강 3 · 해안 4 · 먼바다 3 · 늪 2)만 표에서 옮겼다.
var FISHING_SPOTS = {
    pond_1: "fresh", pond_2: "fresh",
    river_1: "fresh", river_2: "fresh", river_3: "fresh",
    coast_1: "sea", coast_2: "sea", coast_3: "sea", coast_4: "sea",
    far_1: "farSea", far_2: "farSea", far_3: "farSea",
    swamp_1: "swamp", swamp_2: "swamp"
};
var FISHING_RODS = ["item.rod_basic", "item.rod_telescopic", "item.rod_bait", "item.rod_lucky"];
var FISHING_BAITS = { worm: "item.worm_bait", berry: "item.berry_bait" };
var BERRY_BAIT_RARE_BONUS = 1.1;
var FISH_CALL_GAP_SECONDS = 5;
var FISH_ROLL_DATA_KEY = "fishRoll";

// ---- 8.14 상점·판매·선착장 · 8.9 배 ----
// 🟡 아이템 ID 는 설계서에 없다 — 이름·값은 「사는 목록」 줄 그대로. count = 한 번 살 때 들어오는 개수.
var SHOP_ITEMS = {
    "item.rod_basic": { name: "기본 낚싯대", price: 3, count: 1 },
    "item.rod_telescopic": { name: "접이식 낚싯대", price: 10, count: 1 },
    "item.worm_bait": { name: "지렁이 팩", price: 1, count: 3 },
    "item.fisher_hat": { name: "물가 챙갓", price: 15, count: 1 },
    "item.hatchet": { name: "손도끼", price: 5, count: 1 },
    "item.tent": { name: "텐트", price: 35, count: 1 },
    "item.hiking_backpack": { name: "등산 짐가방", price: 40, count: 1 },
    "item.garden_brazier": { name: "정원 화로", price: 15, count: 1 },
    "item.lifeboat": { name: "구명보트", price: 50, count: 1 },
    "item.club": { name: "몽둥이", price: 5, count: 1 },
    "item.gather_basket": { name: "채집 바구니", price: 5, count: 1 },   // M32-03 🟡 손도끼와 같은 첫 도구 값
    // 13.6 요리 재료 — 잡화점 각 1
    "item.salt": { name: "소금", price: 1, count: 1 },
    "item.black_pepper": { name: "흑후추", price: 1, count: 1 },
    "item.dill": { name: "딜", price: 1, count: 1 },
    "item.empty_bottle": { name: "빈 병", price: 1, count: 1 },
    // 13.6 버프 소모품 — 잡화점 3
    "item.heat_pack": { name: "손난로", price: 5, count: 1 },
    "item.hot_soda": { name: "따뜻한 탄산", price: 3, count: 1 },
    "item.snackbar": { name: "곡식 막대", price: 3, count: 1 },
    // 13절 「셔츠·바지」(M9-05) — 꾸미기 옷 각 5, 판매 0(상점 물건)
    "item.flannel_shirt": { name: "플란넬 셔츠", price: 5, count: 1 },
    "item.work_pants": { name: "작업 바지", price: 5, count: 1 },
    // 13.5 모자 — 잡화점 7종(딸기·약탈자는 위험구역 상자)
    "item.farmer_hat": { name: "밭두렁 갓", price: 15, count: 1 },
    "item.hunter_hat": { name: "길잡이 두건", price: 15, count: 1 },
    "item.lumberjack_hat": { name: "도끼꾼 털모자", price: 15, count: 1 },
    "item.cook_hat": { name: "주방 두건", price: 5, count: 1 },
    "item.traveler_hat": { name: "여행자 모자", price: 15, count: 1 },
    "item.merchant_hat": { name: "장사치 중절", price: 15, count: 1 },
    "item.runner_hat": { name: "달리기 모자", price: 15, count: 1 },
    "item.mining_hat": { name: "갱도 투구", price: 15, count: 1 },
    // 13.5 도구 4
    "item.chainsaw": { name: "전동 톱", price: 25, count: 1 },
    "item.machete": { name: "마체테", price: 10, count: 1 },
    "item.binoculars": { name: "망원경", price: 10, count: 1 },
    "item.satellite_communicator": { name: "별길 수신기", price: 10, count: 1 },
    // 8.16 곡괭이 — 고급은 고급 작업대 제작
    "item.pickaxe_basic": { name: "나무자루 곡괭이", price: 10, count: 1 },
    "item.pickaxe_electric": { name: "전동 곡괭이", price: 150, count: 1 },
    // 8.6-c 사냥 총 (M39-01) — 권총 · 탄약 25발 묶음
    "item.pistol_semi": { name: "단발 권총", price: 30, count: 1 },
    "item.ammo": { name: "권총탄", price: 5, count: 25 },
    "item.ammo_rifle": { name: "소총탄", price: 10, count: 20 },
    "item.shotgun": { name: "산탄총", price: 60, count: 1 },
    "item.ammo_shotgun": { name: "산탄", price: 8, count: 16 },
    "item.crossbow": { name: "석궁", price: 45, count: 1 },
    "item.arrow": { name: "화살", price: 6, count: 15 },
    "item.pistol_auto": { name: "연발 권총", price: 50, count: 1 },
    "item.smg": { name: "기관단총", price: 90, count: 1 },
    "item.rifle_burst": { name: "점사 소총", price: 110, count: 1 },
    "item.rifle_semi": { name: "반자동 소총", price: 75, count: 1 },
    "item.shotgun_auto": { name: "연사 산탄총", price: 100, count: 1 },
    "item.shotgun_short": { name: "짧은 산탄총", price: 55, count: 1 },
    "item.mg": { name: "기관총", price: 150, count: 1 },
    "item.bazooka": { name: "바주카", price: 300, count: 1 },
    "item.rocket": { name: "로켓", price: 20, count: 4 },
    // 8.11 「집 2층·전력」 — 원본은 일반상자, 우리는 잡화점에도
    "item.solar_kit": { name: "햇빛 집열판", price: 30, count: 1 },
    "item.battery": { name: "배터리", price: 15, count: 1 },
    "item.light_bulb": { name: "전구", price: 5, count: 1 },
    // 13.8 페인트 7 — 잡화점 각 2 (원본에 값·출처 없음 → 감독 값 🟡)
    "item.paint_white": { name: "하양 물감", price: 2, count: 1 },
    "item.paint_charcoal": { name: "숯검정 물감", price: 2, count: 1 },
    "item.paint_blue": { name: "파랑 물감", price: 2, count: 1 },
    "item.paint_red": { name: "빨강 물감", price: 2, count: 1 },
    "item.paint_yellow": { name: "노랑 물감", price: 2, count: 1 },
    "item.paint_mint": { name: "박하 물감", price: 2, count: 1 },
    "item.paint_blush": { name: "볼연지 물감", price: 2, count: 1 },
    "item.crate_key": { name: "상자 열쇠", price: 50, count: 1 }   // 10.8 — 젬 값은 CRATE_KEY_GEM_PRICE
};
// 8.11 「판매가」 줄 — 상점 물건이지만 산 값의 ⅓(전구·배터리·키트), 폴리머는 수지(0)로 만들지만 1. 상점·레시피 규칙보다 먼저.
// 전등 = 재료 판매가 합(+1 없음) — 전구 2 + 쇠붙이 부속 3 + 나무판 0 (8.11 「판매가」, M20-04a)
// 10.2 「희귀 소재 판매가」 — 가챠 소재 개당 = 등급 코인 ÷ 묶음 수(가죽·나무 50÷5 · 은빛 150÷3 · 황금 400÷2 · 발톱 1,000÷1).
// 묶음을 다 팔면 같은 등급 코인 경품·중복 환급과 같아 기대값이 그대로다(감독 보정 10-06). 가챠는 배낭 밖이라 이 표가 먼저 이긴다.
var FIXED_SELL_PRICE = { "item.light_bulb": 2, "item.battery": 5, "item.solar_kit": 10, "item.polymer": 1, "item.lamp_electric": 5,
    "gacha.mat_hide_5": 10, "gacha.mat_wood_5": 10, "gacha.mat_silver_hide_3": 50, "gacha.mat_gold_hide_2": 200, "gacha.mat_legend_claw_1": 1000 };
var SELL_PRICE = { meat: 1, hide: 2, trophy: 3, premium: 20, fishMinutesPerCoin: 15, cropDivisor: 5, pet: 10 };
// 13.3 씨앗 24 — 한 번 살 때 1개(🟡), 가격 = 작물 판매가 = max(1, floor(코인 ÷ 5)).
Object.keys(CROP_SHOP_SEED_NAMES).forEach(function (cropId) {
    SHOP_ITEMS[seedOf(cropId)] = { name: CROP_SHOP_SEED_NAMES[cropId] + " 씨앗", price: Math.max(1, Math.floor(CROP_HARVEST_COINS[cropId] / SELL_PRICE.cropDivisor)), count: 1 };
});
// island = 사냥존 안 숲(가을섬 성격) 칸 입장료 (DESIGN 1.3·8.14, M12-01 — 클라이언트 `UZoneEffectComponent`).
var FERRY_FARES = { hunt: 5, danger: 10, base: 0, island: 5 };
var BOAT_ITEM = "item.lifeboat";

// ---- 13.5 모자 — 머리 칸 하나. 서버 호출이 걸린 효과만 여기서 판정한다(달리기·딸기·어부는 호스트 몸 상태). ----
// harvestBonusChance: 수확 +1 확률 · woodMul: 나무·삼나무 배수 · fareMul: 뱃삯 배수(올림) · sellMul: 판매 총액 배수(내림)
// pristineMul: 최상급 드롭 확률 배수 — `ClaimLoot {kills}` 굴림(루프#1446). chestRollBonus 는 OpenChest(13.8).
var HAT_EFFECTS = {
    "item.fisher_hat": {},
    "item.farmer_hat": { harvestBonusChance: 0.3 },
    "item.hunter_hat": { pristineMul: 1.3 },
    "item.lumberjack_hat": { woodMul: 2 },
    "item.strawberry_hat": {},
    "item.cook_hat": {},
    "item.looter_hat": { chestRollBonus: 1 },
    "item.traveler_hat": { fareMul: 0.5 },
    "item.merchant_hat": { sellMul: 1.2 },
    "item.runner_hat": {},
    "item.mining_hat": {},
    "item.sprout_hat": {},  // 11 「7일 출석」 7일째 — 겉모습만 (M21-05c)
    "item.chief_hat": {}    // 10.6 주간 우두머리 모자 — 겉모습만 (M21-07e)
};
var HAT_DATA_KEY = "hat";
var HAT_WOOD_ITEMS = ["item.wood", "item.cedar"];

/** 쓴 모자의 효과. 서버 기록의 모자를 지금도 가지고 있을 때만 — 버리거나 팔면 효과도 끝난다. */
function wornHatEffect(inv) {
    var hat = loadData(currentPlayerId, HAT_DATA_KEY, null);
    return hat && HAT_EFFECTS[hat] && countOf(inv, hat) > 0 ? HAT_EFFECTS[hat] : {};
}

/** 지금 효과를 주는 모자 ID(없으면 ""). 기록이 없으면 인벤토리를 읽지 않는다. */
function wornHatId() {
    var hat = loadData(currentPlayerId, HAT_DATA_KEY, null);
    if (!hat || !HAT_EFFECTS[hat]) return "";
    return countOf(readInventory(currentPlayerId), hat) > 0 ? hat : "";
}

/** 모자 쓰기 — 가방에 있는 모자만. 새 모자를 쓰면 전 모자를 벗는다(칸 하나). */
handlers.EquipHat = function (args, context) {
    var itemId = args && args.itemId;
    // #0198 벗기 — 빈 칸("" 또는 null)으로 장착한다. 기록을 지우면 wornHatEffect 가 효과 0 을 돌려준다.
    if (itemId === "" || itemId === null) {
        saveData(currentPlayerId, HAT_DATA_KEY, null);
        return { hat: "" };
    }
    if (!HAT_EFFECTS[itemId]) return fail("bad_args", "모자가 아니다: " + itemId);
    var inv = readInventory(currentPlayerId);
    if (countOf(inv, itemId) < 1) return fail("not_enough_items", "가지고 있지 않은 모자", { itemId: itemId });
    saveData(currentPlayerId, HAT_DATA_KEY, itemId);
    return { hat: itemId };
};

// ---- 꾸미기 옷 — 칸마다 「겉모습 덮개」 (docs/specs/skin-parts.md 5장 4 · 조각 d) ----
// 효과 없는 꾸미기만(10.3). 효과 모자는 그대로 `EquipHat` 이 맡고, 같은 칸 꾸미기는 보이는 것만 덮는다.
// 칸 이름은 클라 `EEquipSlot` 과 한 줄씩 맞는다(head=Head · face=FaceAccessory · top=OutfitTop · bottom=OutfitBottom ·
// shoes=Shoes · gloves=Gloves · back=Backpack). 입어도 아이템은 배낭에 그대로다 — 모자처럼 기록만 한다.
var OUTFIT_SLOTS = ["head", "face", "top", "bottom", "shoes", "gloves", "back"];
// 꾸미기 아이템 → 칸. 조각 f~l 이 칸마다 첫 시즌 6벌을 채운다(클라 `DT_ItemTable` 의 `Cosmetic` 행과 같은 ID).
var OUTFIT_ITEMS = {
    // 조각 f 모자 — 들꽃 세트 · 항구 세트(일반 3 · 희귀 2 · 에픽 1, 명세 4장)
    "item.meadow_bucket": "head", "item.meadow_cap": "head", "item.meadow_frog": "head",
    "item.harbor_beanie": "head", "item.harbor_captain": "head", "item.harbor_whale": "head",
    // M83-02 모자 +5 (루프#856)
    "item.meadow_acorn": "head", "item.harbor_souwester": "head", "item.meadow_crown": "head",
    "item.harbor_bandana": "head", "item.meadow_wizard": "head",
    // M83-03 모자 +5 (루프#858)
    "item.meadow_sunflower": "head", "item.harbor_dixie": "head", "item.harbor_horned": "head",
    "item.meadow_mushroom": "head", "item.meadow_tophat": "head",
    // M84-02 모자 +5 (루프#865)
    "item.meadow_cowboy": "head", "item.meadow_bear": "head", "item.meadow_catears": "head",
    "item.harbor_tricorn": "head", "item.meadow_propeller": "head",
    // M84-03 모자 +5 (루프#866)
    "item.meadow_beret": "head", "item.harbor_fez": "head", "item.meadow_bunny": "head",
    "item.meadow_pumpkin": "head", "item.harbor_shark": "head",
    // M88-02 모자 +5 (루프#890)
    "item.meadow_ladybug": "head", "item.harbor_lighthouse": "head", "item.meadow_flowerpot": "head",
    "item.meadow_carrot": "head", "item.harbor_jellyfish": "head",
    // M88-03 모자 +5 (루프#891)
    "item.harbor_sailor_cap": "head", "item.meadow_teapot": "head", "item.meadow_owl": "head",
    "item.harbor_crab": "head", "item.meadow_cake": "head",
    // M90-01 모자 +5 (루프#898)
    "item.meadow_snail": "head", "item.harbor_buoy": "head", "item.meadow_pineapple": "head",
    "item.harbor_starfish": "head", "item.meadow_rocket": "head",
    // M90-02 모자 +5 (루프#899)
    "item.meadow_duck": "head", "item.meadow_beehive": "head", "item.harbor_conch": "head",
    "item.meadow_cactus": "head", "item.meadow_ufo": "head",
    // M91-01 모자 +6 (루프#904)
    "item.meadow_penguin": "head", "item.meadow_donut": "head", "item.meadow_mouse": "head",
    "item.harbor_igloo": "head", "item.harbor_rainbow": "head", "item.harbor_treasure": "head",
    // M91-02 모자 +5 (루프#905)
    "item.meadow_fox": "head", "item.meadow_pig": "head", "item.harbor_sailboat": "head",
    "item.meadow_cheese": "head", "item.meadow_dragon": "head",
    // 조각 g 얼굴 장식 — 들꽃 세트 · 항구 세트(일반 3 · 희귀 2 · 에픽 1, 명세 4장)
    "item.meadow_ladybug_nose": "face", "item.meadow_daisy_glasses": "face", "item.meadow_petal_goggles": "face",
    "item.harbor_eyepatch": "face", "item.harbor_sunglasses": "face", "item.harbor_diving_mask": "face",
    // M87-01 얼굴 장식 +4 (루프#883)
    "item.meadow_mustache": "face", "item.harbor_bandage": "face", "item.harbor_monocle": "face", "item.meadow_star_shades": "face",
    // M87-02 얼굴 장식 +4 (루프#884)
    "item.harbor_kerchief": "face", "item.meadow_cheek_stars": "face", "item.meadow_heart_shades": "face", "item.harbor_white_beard": "face",
    // 조각 h 상의 — 들꽃 세트 · 항구 세트(일반 3 · 희귀 2 · 에픽 1, 명세 4장)
    "item.meadow_tee": "top", "item.meadow_knit": "top", "item.meadow_petal_blouse": "top",
    "item.harbor_stripe_tee": "top", "item.harbor_puffer": "top", "item.harbor_sailor": "top",
    // M83-04 옷 +5 (루프#859)
    "item.meadow_gingham": "top", "item.harbor_island_shirt": "top", "item.meadow_bee_sweater": "top",
    "item.meadow_cherry_tee": "top", "item.harbor_anchor_sweat": "top", "item.meadow_patchwork": "top", // M83-05 (루프#861)
    "item.meadow_leaf_tee": "top", "item.harbor_wave_sweat": "top", "item.meadow_rainbow": "top", // M84-04 (루프#867)
    "item.harbor_fish_tee": "top", "item.meadow_strawberry": "top", "item.harbor_sunset": "top", // M84-05 (루프#868)
    "item.harbor_zigzag": "top", "item.meadow_toadstool": "top", "item.harbor_aurora": "top", // M88-04 (루프#892)
    "item.meadow_cactus_tee": "top", "item.harbor_snow_sweat": "top", "item.meadow_honeycomb": "top", // M88-05 (루프#893)
    "item.harbor_gull_tee": "top", "item.meadow_pine_sweat": "top", "item.harbor_flame": "top", // M90-03 (루프#900)
    "item.meadow_daisy_tee": "top", "item.meadow_diamond_sweat": "top", "item.harbor_galaxy": "top", // M90-04 (루프#901)
    "item.meadow_sun_tee": "top", "item.harbor_kelp_sweat": "top", "item.meadow_firefly": "top", // M91-03 (루프#906)
    "item.harbor_shell_tee": "top", "item.harbor_moon_sweat": "top", "item.harbor_lantern": "top", // M91-04 (루프#907)
    "item.meadow_acorn_tee": "top", "item.harbor_rope_sweat": "top", "item.harbor_coral": "top", // M92-01 (루프#913)
    "item.meadow_clover_tee": "top", "item.harbor_compass_sweat": "top", "item.meadow_butterfly": "top", // M92-02 (루프#914)
    "item.meadow_plum_tee": "top", "item.harbor_meteor": "top", // M92-03 (루프#915)
    // 조각 i 하의 — 들꽃 세트 · 항구 세트(일반 3 · 희귀 2 · 에픽 1, 명세 4장)
    "item.meadow_shorts": "bottom", "item.meadow_corduroy": "bottom", "item.meadow_petal_skirt": "bottom",
    "item.harbor_cargo": "bottom", "item.harbor_rain_pants": "bottom", "item.harbor_bell_bottoms": "bottom",
    "item.harbor_capri": "bottom", "item.meadow_star_pants": "bottom", // M83-04
    "item.harbor_sail_shorts": "bottom", "item.meadow_patch_pants": "bottom", // M83-05
    "item.harbor_check_shorts": "bottom", "item.meadow_denim": "bottom", // M84-04
    "item.meadow_plaid_shorts": "bottom", "item.harbor_polka": "bottom", // M84-05
    "item.meadow_cloud_shorts": "bottom", "item.harbor_camo": "bottom", // M88-04
    "item.meadow_moon_shorts": "bottom", "item.harbor_argyle": "bottom", // M88-05
    "item.meadow_ladybug_shorts": "bottom", "item.harbor_tartan": "bottom", // M90-03
    "item.meadow_cherry_shorts": "bottom", "item.harbor_pinstripe": "bottom", // M90-04
    "item.meadow_leaf_shorts": "bottom", "item.harbor_wave_pants": "bottom", // M91-03
    "item.meadow_paw_shorts": "bottom", "item.harbor_bolt_pants": "bottom", // M91-04
    "item.meadow_shroom_shorts": "bottom", "item.harbor_chevron": "bottom", // M92-01
    "item.meadow_raindrop_shorts": "bottom", "item.harbor_weave": "bottom", // M92-02
    "item.meadow_sprout_shorts": "bottom", "item.harbor_dive_flag": "bottom", // M92-03
    // 조각 j 신발 — 들꽃 세트 · 항구 세트(일반 3 · 희귀 2 · 에픽 1, 명세 4장)
    "item.meadow_canvas": "shoes", "item.meadow_clogs": "shoes", "item.meadow_petal_boots": "shoes",
    "item.harbor_deck_shoes": "shoes", "item.harbor_rain_boots": "shoes", "item.harbor_flippers": "shoes",
    "item.harbor_hightops": "shoes", "item.meadow_star_kicks": "shoes", "item.meadow_berry_boots": "shoes", "item.meadow_wing_kicks": "shoes", // M87-03 (루프#885)
    // 조각 k 장갑 — 들꽃 세트 · 항구 세트(일반 3 · 희귀 2 · 에픽 1, 명세 4장)
    "item.meadow_garden_gloves": "gloves", "item.meadow_mittens": "gloves", "item.meadow_petal_gloves": "gloves",
    "item.harbor_fingerless": "gloves", "item.harbor_rubber_gloves": "gloves", "item.harbor_crab_gloves": "gloves",
    "item.harbor_leather_gloves": "gloves", "item.meadow_wool_gloves": "gloves", "item.harbor_arm_sleeves": "gloves", // M87-04 (루프#886)
    // 조각 l 배낭 — 들꽃 세트 · 항구 세트(일반 3 · 희귀 2 · 에픽 1, 명세 4장). 배낭 칸 효과(+칸)는 장비가 맡고 이것은 겉모습만
    "item.meadow_daypack": "back", "item.meadow_basket_pack": "back", "item.meadow_frog_pack": "back",
    "item.harbor_duffel": "back", "item.harbor_lifebuoy_pack": "back", "item.harbor_octopus_pack": "back",
    // M83-01 배낭 +3 (루프#855)
    "item.meadow_mushroom_pack": "back", "item.harbor_crate_pack": "back", "item.meadow_beehive_pack": "back",
    // M84-01 배낭 +3 (루프#864)
    "item.meadow_watermelon_pack": "back", "item.harbor_barrel_pack": "back", "item.harbor_whale_pack": "back",
    // M88-01 배낭 +3 (루프#889)
    "item.meadow_strawberry_pack": "back", "item.meadow_cat_pack": "back", "item.harbor_submarine_pack": "back"
};
// 꾸미기 등급 — 칸마다 희귀 2 · 에픽 1, 나머지는 일반(클라 `DT_ItemTable` `Grade` 열과 같다). 목장 꾸미기 모자는 레어(10.9).
var COSMETIC_GRADES = {
    "item.meadow_frog": "rare", "item.harbor_captain": "rare", "item.harbor_whale": "epic",
    "item.meadow_petal_goggles": "rare", "item.harbor_sunglasses": "rare", "item.harbor_diving_mask": "epic",
    "item.meadow_petal_blouse": "rare", "item.harbor_puffer": "rare", "item.harbor_sailor": "epic",
    "item.meadow_petal_skirt": "rare", "item.harbor_rain_pants": "rare", "item.harbor_bell_bottoms": "epic",
    "item.meadow_petal_boots": "rare", "item.harbor_rain_boots": "rare", "item.harbor_flippers": "epic",
    "item.meadow_petal_gloves": "rare", "item.harbor_rubber_gloves": "rare", "item.harbor_crab_gloves": "epic",
    "item.meadow_frog_pack": "rare", "item.harbor_lifebuoy_pack": "rare", "item.harbor_octopus_pack": "epic", "item.meadow_beehive_pack": "rare", "item.harbor_whale_pack": "rare", "item.harbor_submarine_pack": "rare",
    "item.meadow_crown": "rare", "item.harbor_bandana": "rare", "item.meadow_wizard": "epic",
    "item.harbor_island_shirt": "rare", "item.meadow_bee_sweater": "epic", "item.meadow_star_pants": "rare",
    "item.harbor_anchor_sweat": "rare", "item.meadow_patch_pants": "rare", "item.meadow_patchwork": "epic", // M83-05
    "item.harbor_wave_sweat": "rare", "item.meadow_denim": "rare", "item.meadow_rainbow": "epic", // M84-04
    "item.meadow_strawberry": "rare", "item.harbor_polka": "rare", "item.harbor_sunset": "epic", // M84-05 (루프#868)
    "item.meadow_toadstool": "rare", "item.harbor_camo": "rare", "item.harbor_aurora": "epic", // M88-04 (루프#892)
    "item.harbor_snow_sweat": "rare", "item.harbor_argyle": "rare", "item.meadow_honeycomb": "epic", // M88-05 (루프#893)
    "item.meadow_pine_sweat": "rare", "item.harbor_tartan": "rare", "item.harbor_flame": "epic", // M90-03 (루프#900)
    "item.meadow_diamond_sweat": "rare", "item.harbor_pinstripe": "rare", "item.harbor_galaxy": "epic", // M90-04 (루프#901)
    "item.harbor_kelp_sweat": "rare", "item.meadow_firefly": "epic", "item.harbor_wave_pants": "rare", // M91-03 (루프#906)
    "item.harbor_moon_sweat": "rare", "item.harbor_lantern": "epic", "item.harbor_bolt_pants": "rare", // M91-04 (루프#907)
    "item.harbor_rope_sweat": "rare", "item.harbor_coral": "epic", "item.harbor_chevron": "rare", // M92-01 (루프#913)
    "item.harbor_compass_sweat": "rare", "item.meadow_butterfly": "epic", "item.harbor_weave": "rare", // M92-02 (루프#914)
    "item.harbor_meteor": "epic", "item.harbor_dive_flag": "rare", // M92-03 (루프#915)
    "item.harbor_horned": "rare", "item.meadow_mushroom": "rare", "item.meadow_tophat": "epic",
    "item.meadow_catears": "rare", "item.harbor_tricorn": "rare", "item.meadow_propeller": "epic",
    "item.meadow_bunny": "rare", "item.meadow_pumpkin": "rare", "item.harbor_shark": "epic",
    "item.harbor_lighthouse": "rare", "item.meadow_carrot": "rare", "item.harbor_jellyfish": "epic",
    "item.meadow_owl": "rare", "item.harbor_crab": "rare", "item.meadow_cake": "epic", // M88-03 (루프#891)
    "item.meadow_pineapple": "rare", "item.harbor_starfish": "rare", "item.meadow_rocket": "epic", // M90-01 (루프#898)
    "item.harbor_conch": "rare", "item.meadow_cactus": "rare", "item.meadow_ufo": "epic", // M90-02 (루프#899)
    "item.meadow_mouse": "rare", "item.harbor_igloo": "rare", "item.harbor_rainbow": "rare", "item.harbor_treasure": "epic", // M91-01 (루프#904)
    "item.harbor_sailboat": "rare", "item.meadow_cheese": "rare", "item.meadow_dragon": "epic", // M91-02 (루프#905)
    "item.harbor_monocle": "rare", "item.meadow_star_shades": "epic", // M87-01 (루프#883)
    "item.meadow_heart_shades": "rare", "item.harbor_white_beard": "epic", // M87-02 (루프#884)
    "item.meadow_berry_boots": "rare", "item.meadow_wing_kicks": "epic", // M87-03 (루프#885)
    "item.harbor_arm_sleeves": "rare", // M87-04 (루프#886)
    "item.ranch_hat": "rare"
};
/** 옷장 상점·꾸미기 상자에 거는 것 = 꾸미기 42벌 + 목장 꾸미기 3 (10.7·10.8). */
var COSMETIC_SHOP_ITEMS = Object.keys(OUTFIT_ITEMS).concat(RANCH_COSMETICS);

function cosmeticGrade(itemId) {
    return COSMETIC_GRADES[itemId] || "common";
}
// v3 — 꾸미기(옷·모자·배낭·목장 꾸미기·펫 의상·목장 장식) 전부 1벌씩. 표가 늘면 키트도 따라 늘도록 표에서 모은다.
Object.keys(OUTFIT_ITEMS).concat(RANCH_COSMETICS, GACHA_PET_COSMETICS, Object.keys(GACHA_RANCH_DECOS))
    .forEach(function (id) { addTesterKit(id, 1); });
var OUTFIT_DATA_KEY = "outfit";

/** 입은 꾸미기 중 지금도 가진 것만 `{칸: 아이템}` — 버리거나 팔면 그 칸은 벗은 것으로 본다(`wornHatId` 와 같은 규칙). */
function wornOutfit(inv) {
    var outfit = loadData(currentPlayerId, OUTFIT_DATA_KEY, {}) || {};
    var slots = Object.keys(outfit);
    if (slots.length === 0) return {};
    inv = inv || readInventory(currentPlayerId);
    var worn = {};
    slots.forEach(function (slot) {
        if (countOf(inv, outfit[slot]) > 0) worn[slot] = outfit[slot];
    });
    return worn;
}

/**
 * 꾸미기 한 칸을 입거나(`itemId`) 벗는다(`itemId` 빈 값). 배낭에 그 꾸미기가 있을 때만 적는다 — 클라는 이 응답 뒤에만
 * 갈아입는다(CLAUDE.md 8, 안 산 젬 꾸미기를 입어 보이기 막기). 답 `outfit` = 기록 전체(가진 것만).
 */
handlers.SetOutfit = function (args, context) {
    args = args || {};
    var slot = args.slot;
    if (OUTFIT_SLOTS.indexOf(slot) === -1) return fail("bad_slot", "잘못된 꾸미기 칸: " + slot);
    var itemId = args.itemId || "";
    if (itemId !== "" && OUTFIT_ITEMS[itemId] === undefined) return fail("bad_args", "꾸미기가 아니다: " + itemId);
    if (itemId !== "" && OUTFIT_ITEMS[itemId] !== slot) return fail("bad_args", "다른 칸 꾸미기: " + itemId);
    var inv = readInventory(currentPlayerId);
    var outfit = wornOutfit(inv);
    var old = outfit[slot] || "";
    if (old === itemId) return fail("already", itemId === "" ? "입은 꾸미기가 없다" : "이미 입은 꾸미기");
    if (itemId !== "" && countOf(inv, itemId) < 1) return fail("not_enough_items", "꾸미기가 없다", { itemId: itemId, need: 1, have: 0 });

    if (itemId === "") delete outfit[slot]; else outfit[slot] = itemId;
    saveData(currentPlayerId, OUTFIT_DATA_KEY, outfit);
    return { slot: slot, itemId: itemId, outfit: outfit };
};

// ---- 2.2 닉네임 (사람 지시 2026-10-06 13:40) ----
// 머리 위 이름표에 쓸 이름. 서버가 길이·글자·금칙어를 거른 뒤에만 Player Data `nickname` 에 적는다(CLAUDE.md 8).
// PlayFab 표시 이름(겹침 검사)은 받아 준 이름 그대로 클라가 Client API `UpdateUserTitleDisplayName` 으로 올린다 —
// Server API 에 표시 이름 바꾸기가 없어서다. 숫자는 DESIGN 2.2 「이름표」 표.
// 최소 3 = PlayFab 표시 이름 최소 길이 — 2자면 여기선 받고 표시 이름에서 거절돼 가입이 막혔다(INBOX 10-08 폰 결함).
var NICKNAME_MIN_LENGTH = 3;
var NICKNAME_MAX_LENGTH = 12;
var NICKNAME_DATA_KEY = "nickname";
// 완성 한글·영문·숫자만 — 낱자모(ㅅㅂ)는 금칙어를 피해 가는 길이라 막는다. 사이 띄어쓰기도 안 된다.
var NICKNAME_CHARS = /^[가-힣A-Za-z0-9]+$/;
// 금칙어 원본. 클라 미리 막기(`SnareNickname.cpp`)는 옛 사본이라 덜 막는다 — 최종 판정은 여기. DESIGN 2.2 「금칙어」.
// 어디서든 막는 말: 이름 어디에 들어 있어도 거절. 한글은 모음 변형을 접어서(nicknameFold) 비교한다.
var NICKNAME_BANNED_WORDS = [
    "시발", "씨발", "씨빨", "시빨", "씨팔", "시팔", "씹", "좆", "존나", "졸라", "병신", "븅신", "븅딱", "병딱", "등신",
    "개새", "새끼", "쌔끼", "썅", "개년", "개놈", "미친놈", "미친년", "애미", "애비", "느금", "느그", "니미", "엠창", "앰창",
    "창녀", "걸레", "보지", "자지", "섹스", "야동", "강간", "지랄", "후장", "닥쳐",
    "운영자", "관리자", "개발자",
    "한남충", "김치녀", "일베", "맘충", "틀딱", "된장녀",                                // 혐오 멸칭 — 대표만(과차단 주의)
    "노무현", "문재인", "박근혜", "이명박", "윤석열", "이재명", "김대중", "김영삼", "전두환", "노태우", "박정희", "이승만",
    "김정은", "김정일", "김일성", "트럼프", "바이든", "시진핑", "푸틴",                    // 주요 정치인 실명(조롱 방지)
    "자살", "자해", "killyourself", "killurself", "suicide",                              // 자살·자해
    "fuck", "fck", "fack", "phuck", "fvck", "shit", "bitch", "biatch", "pussy", "porn", "nigger", "nigga", "faggot",
    "whore", "slut", "retard", "hitler", "asshole", "rapist"
];
// 낱말일 때만 막는 말: 이름을 대소문자·숫자·한글 경계로 자른 조각과 통째로 같을 때만(Scunthorpe·Essex·Dickens·grape 통과).
var NICKNAME_BANNED_TOKENS = ["cunt", "sex", "dick", "rape", "kys", "fag", "cum", "nazi", "admin", "fuk", "fuc", "ass", "pedo", "cock"];
// 숫자로 글자 흉내(f4ck·b1tch·n1gger) — 이 표로 글자로 바꾼 꼴도 본다.
var NICKNAME_LEET = { "0": "o", "1": "i", "3": "e", "4": "a", "5": "s", "7": "t", "8": "b", "9": "g" };

/**
 * 한글 변형 접기 — 이름·금칙어 양쪽에 건다.
 * 모음: ㅐㅒㅖ→ㅔ, ㅙㅚ→ㅞ, ㅟㅢ→ㅣ (쌔끼·쉬발·늬미). 늘이기: 앞 글자와 같은 모음의 받침 없는 「ㅇ+모음」 은 뺀다 (시이이발→시발).
 */
function nicknameFold(text) {
    var VOWEL = { 1: 5, 3: 5, 7: 5, 10: 15, 11: 15, 16: 20, 19: 20 };
    var out = "", prevJung = -1;
    for (var i = 0; i < text.length; i++) {
        var code = text.charCodeAt(i) - 0xAC00;
        if (code < 0 || code > 11171) { out += text.charAt(i); prevJung = -1; continue; }
        var jong = code % 28, jung = Math.floor(code / 28) % 21, cho = Math.floor(code / 588);
        if (VOWEL[jung] !== undefined) jung = VOWEL[jung];
        if (cho === 11 && jong === 0 && jung === prevJung) continue; // ㅇ 은 초성 11번
        out += String.fromCharCode(0xAC00 + cho * 588 + jung * 28 + jong);
        prevJung = jong === 0 ? jung : -1;
    }
    return out;
}
/** 낱말을 「글자마다 1번 이상」 정규식으로 — fuuuck·시이이발 반복은 잡고, Niger 는 nigger(g 두 번)와 안 겹친다. 낱말은 한글·영문뿐이라 이스케이프 불필요. */
function nicknameWordPattern(word, whole) {
    var body = nicknameFold(word).split("").join("+") + "+";
    return new RegExp(whole ? "^" + body + "$" : body);
}
var NICKNAME_BANNED_PATTERNS = NICKNAME_BANNED_WORDS.map(function (w) { return nicknameWordPattern(w, false); });
var NICKNAME_TOKEN_PATTERNS = NICKNAME_BANNED_TOKENS.map(function (w) { return nicknameWordPattern(w, true); });

function nicknameLeet(text) { return text.replace(/[0-9]/g, function (d) { return NICKNAME_LEET[d] || d; }); }

/** 금칙어가 들었나 — 원문·숫자 뺀 꼴·숫자를 글자로 바꾼 꼴 셋 다. 낱말 목록은 두 갈래 조각(숫자에서 끊은 것·숫자를 글자로 바꾼 것)에서. */
function nicknameHasBannedWord(name) {
    var camel = name.replace(/([a-z])(?=[A-Z])/g, "$1 ");
    var lower = nicknameFold(name.toLowerCase());
    var forms = [lower, lower.replace(/[0-9]/g, ""), nicknameLeet(lower), nicknameLeet(lower).replace(/l/g, "i")]; // 마지막: i 대신 l(shlt)
    var tokens = (camel.match(/[A-Za-z]+/g) || []).concat((camel.match(/[A-Za-z0-9]+/g) || []).map(nicknameLeet))
        .map(function (t) { return t.toLowerCase(); });
    return NICKNAME_BANNED_PATTERNS.some(function (re) { return forms.some(function (f) { return re.test(f); }); })
        || NICKNAME_TOKEN_PATTERNS.some(function (re) { return tokens.some(function (t) { return re.test(t); }); });
}

/** 이름 거름 — 맞으면 null, 아니면 `fail` 답. 앞뒤 공백은 부르는 쪽이 자른다. */
function nicknameProblem(name) {
    if (name.length < NICKNAME_MIN_LENGTH || name.length > NICKNAME_MAX_LENGTH) {
        return fail("bad_length", "닉네임은 " + NICKNAME_MIN_LENGTH + "~" + NICKNAME_MAX_LENGTH + "자", { min: NICKNAME_MIN_LENGTH, max: NICKNAME_MAX_LENGTH });
    }
    if (!NICKNAME_CHARS.test(name)) return fail("bad_chars", "닉네임은 한글·영문·숫자만");
    if (nicknameHasBannedWord(name)) return fail("banned_word", "쓸 수 없는 말이 들어 있다");
    return null;
}

/**
 * 닉네임 정하기·바꾸기. `name` 앞뒤 공백을 자르고 `nicknameProblem` 으로 거른 뒤 기록한다.
 * 답 `{name}` — 클라는 이 `name` 그대로 표시 이름을 올리고, 그것까지 받아들여진 뒤에 이름표를 바꾼다.
 */
handlers.SetNickname = function (args, context) {
    args = args || {};
    if (typeof args.name !== "string") return fail("bad_args", "닉네임이 없다");
    var name = args.name.trim();
    var problem = nicknameProblem(name);
    if (problem) return problem;
    // 같은 이름도 받는다 — 표시 이름 올리기가 끊겨 다시 누를 때 막히지 않게.
    saveData(currentPlayerId, NICKNAME_DATA_KEY, name);
    return { name: name };
};

// ---- 10.8 꾸미기 상자·열쇠 (상점 개편 B) ----
// 상자는 사냥·채집(`ClaimLoot` 한 번)·낚시(`CatchFish` 한 번)마다 서버가 굴려 준다 — 클라이언트가 보낸 상자는 받지 않는다(규칙 8).
// 계절이 바뀌면 CRATE_CURRENT 와 풀을 바꾼다(클라 `DT_CrateTable` 의 Season 열과 같은 짜임). 풀 = 효과 없는 꾸미기만(10.3).
var CRATE_DROP_CHANCE = 0.02;
var CRATE_KEY = "item.crate_key";
var CRATE_KEY_GEM_PRICE = 10;
var CRATE_CURRENT = "item.crate_autumn26";
var CRATE_GRADE_CHANCE = { common: 0.75, rare: 0.20, epic: 0.05 };

/** 등급 확률을 등급 안 품목 수로 고르게 나눈다 — 일반 → 희귀 → 에픽 순(고지 줄 순서와 같다). */
function cratePoolOf(itemIds) {
    var items = [];
    ["common", "rare", "epic"].forEach(function (grade) {
        var inGrade = itemIds.filter(function (id) { return cosmeticGrade(id) === grade; });
        inGrade.forEach(function (id) { items.push({ itemId: id, grade: grade, chance: CRATE_GRADE_CHANCE[grade] / inGrade.length }); });
    });
    return items;
}

var CRATE_POOLS = {
    "item.crate_autumn26": { season: "autumn26", items: cratePoolOf(COSMETIC_SHOP_ITEMS) }
};

/** 굴려서 맞고 칸이 남으면 상자 ID, 아니면 null. 칸이 모자라면 상자만 빠진다 — 본래 지급은 막지 않는다. */
function rollCrateDrop(inv, deltas) {
    if (Math.random() >= CRATE_DROP_CHANCE) return null;
    var withCrate = copyCounts(deltas);
    withCrate[CRATE_CURRENT] = (withCrate[CRATE_CURRENT] || 0) + 1;
    return roomCheck(inv, withCrate) ? null : CRATE_CURRENT;
}

handlers.OpenCrate = function (args, context) {
    var crateId = args && args.crateId;
    var pool = CRATE_POOLS[crateId];
    if (!pool) return fail("bad_args", "상자가 아니다: " + crateId);
    var inv = readInventory(currentPlayerId);
    if (countOf(inv, crateId) < 1) return fail("not_enough_items", "상자가 없다");
    if (countOf(inv, CRATE_KEY) < 1) return fail("no_key", "열쇠가 없다");
    var picked = pool.items[0];
    var roll = Math.random();
    for (var i = 0; i < pool.items.length; i++) {
        roll -= pool.items[i].chance;
        if (roll < 0) { picked = pool.items[i]; break; }
    }
    var deltas = {};
    deltas[crateId] = -1;
    deltas[CRATE_KEY] = (deltas[CRATE_KEY] || 0) - 1;
    deltas[picked.itemId] = (deltas[picked.itemId] || 0) + 1;
    var full = roomCheck(inv, deltas);
    if (full) return full;
    takeItems(currentPlayerId, inv, crateId, 1);
    takeItems(currentPlayerId, inv, CRATE_KEY, 1);
    giveItems(currentPlayerId, [{ itemId: picked.itemId, count: 1 }]);
    addToCodex(picked.itemId);
    return { crateId: crateId, itemId: picked.itemId, grade: picked.grade };
};

// ---- 13.8 위험구역 상자 · 보물 지도 (M13-10) ----
// 굴림 한 번 = 아래 줄을 **줄마다 따로** 굴린다(13.8 「위험구역 상자 전리품 줄」) + 설계도 20%(8.10). 확률 고지 = DESIGN 10.6.
var CHEST_LOOT = [
    { itemId: "item.treasure_map", chance: 0.10 },
    { itemId: "item.ruby", chance: 0.04 },
    { itemId: "item.sapphire", chance: 0.04 },
    { itemId: "item.amethyst", chance: 0.04 },
    { itemId: "item.aquamarine", chance: 0.04 },
    { itemId: "item.emerald", chance: 0.04 },
    { itemId: "item.strawberry_hat", chance: 0.03 },
    { itemId: "item.looter_hat", chance: 0.03 },
    { itemId: "item.mystery_bag", chance: 0.10 },
    { itemId: "item.cash_bundle", chance: 0.05 },
    { itemId: "item.snackbar", chance: 0.15 },
    { itemId: "item.water_canister", chance: 0.15 },
    { itemId: "item.salt", chance: 0.15 },
    { itemId: "item.black_pepper", chance: 0.15 },
    { itemId: "item.dill", chance: 0.15 },
    { itemId: "item.tea_bag", chance: 0.15 },    // 13.6 찻잎 주머니 = 제작 없음, 원본 상자 (10-06 보정)
    { itemId: "item.solar_kit", chance: 0.05 },   // 8.11 전자 재료 — 상자 줄마다 각 5% (M20-02b)
    { itemId: "item.battery", chance: 0.05 },
    { itemId: "item.light_bulb", chance: 0.05 },
    { itemId: "item.boat_motor", chance: 0.03 },  // 8.9 보트 부품 — 원본 셋 다 "일반상자"(VN461~463) (M32-01)
    { itemId: "item.boat_motor_electric", chance: 0.02 },
    { itemId: "item.boat_container", chance: 0.03 }
];
var CHEST_BLUEPRINT_CHANCE = 0.2;   // 8.10 설계도 줄
var VAULT_PASS = "item.vault_pass";  // 8.1c 금고 열쇠패 — 위험구역·보물 상자 한 번 굴림마다 5%
var VAULT_PASS_CHANCE = 0.05;
var CHEST_COUNT = 2;                // 8.10 「8.1 옆 2개」 — args.chest = 1..2
var CHEST_RECHARGE_SECONDS = 600;   // 8.10 재충전(원본)
var CHEST_DATA_KEY = "chests";
var TREASURE_ITEM = "item.treasure_map";
var TREASURE_ROLLS = 2;             // 13.8 보물 상자 = 굴림 2번
var TREASURE_SECONDS = 600;         // 13.8 보물 지도 수명
var TREASURE_DATA_KEY = "treasure";
var TREASURE_ZONES = ["zone.plains", "zone.snow", "zone.forest", "zone.swamp"]; // 1.3 사냥존 칸 넷
var LOOT_SELL_PRICE = {             // 13.8 「값」 칸 — 보물 지도는 값이 없어 못 판다(🟡)
    "item.ruby": 15, "item.sapphire": 15, "item.amethyst": 15, "item.aquamarine": 15, "item.emerald": 15,
    "item.mystery_bag": 10, "item.cash_bundle": 20
};

/** 상자 굴림 rolls 번. 설계도는 아직 없는 것 중 하나 — 다 있으면 그 줄은 비고, 한 번 연 상자에서 같은 설계도는 두 번 안 나온다. */
function rollChest(rolls, ownedBlueprints) {
    var counts = {};
    var blueprints = [];
    for (var r = 0; r < rolls; r++) {
        CHEST_LOOT.forEach(function (line) {
            if (Math.random() < line.chance) counts[line.itemId] = (counts[line.itemId] || 0) + 1;
        });
        if (Math.random() < VAULT_PASS_CHANCE) counts[VAULT_PASS] = (counts[VAULT_PASS] || 0) + 1;
        if (Math.random() < CHEST_BLUEPRINT_CHANCE) {
            var left = BLUEPRINT_IDS.filter(function (id) { return ownedBlueprints.indexOf(id) === -1 && blueprints.indexOf(id) === -1; });
            if (left.length > 0) blueprints.push(left[Math.floor(Math.random() * left.length)]);
        }
    }
    return { items: toList(counts), blueprints: blueprints };
}

/** 보물 지도 쓰기 — 지도 1장을 빼고 사냥존 칸 하나의 한 자리(u·v = 칸 안 0~1 비율)를 600초 동안 연다. 한 번에 하나. */
handlers.UseTreasureMap = function (args, context) {
    var now = Date.now();
    var active = loadData(currentPlayerId, TREASURE_DATA_KEY, null);
    if (active && Date.parse(active.expiresUtc) > now) return fail("busy", "찾는 보물이 이미 있다", { treasure: active });
    var inv = readInventory(currentPlayerId);
    if (countOf(inv, TREASURE_ITEM) < 1) return fail("not_enough_items", "보물 지도가 없다", { itemId: TREASURE_ITEM });
    takeItems(currentPlayerId, inv, TREASURE_ITEM, 1);
    var treasure = {
        treasureId: newId(),
        zoneId: TREASURE_ZONES[Math.floor(Math.random() * TREASURE_ZONES.length)],
        u: Math.random(), v: Math.random(),
        expiresUtc: new Date(now + TREASURE_SECONDS * 1000).toISOString()
    };
    saveData(currentPlayerId, TREASURE_DATA_KEY, treasure);
    return treasure;
};

/**
 * 찾는 보물 다시 받기 (M13-15) — 존을 옮기거나 다시 들어오면 기기는 표식을 잃는다. 자리는 서버 기록이 권위라
 * 여기서 다시 받아 상자·미니맵 점을 세운다. 시간이 지난 기록은 여기서 지운다(OpenChest `expired` 와 같은 판정).
 * @return `{treasure: {treasureId,zoneId,u,v,expiresUtc}}` 또는 `{treasure: null}`
 */
handlers.GetTreasure = function (args, context) {
    var treasure = loadData(currentPlayerId, TREASURE_DATA_KEY, null);
    if (treasure && Date.parse(treasure.expiresUtc) <= Date.now()) {
        saveData(currentPlayerId, TREASURE_DATA_KEY, null);
        treasure = null;
    }
    return { treasure: treasure };
};

/**
 * 상자 열기 — 위험구역 상자(`chest` 1..2, 사람마다 600초 재충전) 또는 보물 상자(`treasureId`, 600초 안 · 굴림 2번).
 * 약탈자 모자는 굴림 +1. 전리품은 서버가 굴린다 — 인자로 온 아이템은 보지 않는다(CLAUDE.md 8).
 * 칸이 모자라면 전리품 전부가 발밑 가방(버리기와 같은 `drops`)으로 간다 — 다시 불러 새로 굴리는 길을 막는다(🟡).
 * ponytail: 서버는 위치·방을 모른다 — "상자 앞에 섰다" 는 부르는 기기 판정(ClaimLoot 같은 신뢰), 재충전은 사람마다.
 */
handlers.OpenChest = function (args, context) {
    args = args || {};
    var now = Date.now();
    var rolls = 1;
    var chests = null;
    if (args.treasureId !== undefined) {
        var treasure = loadData(currentPlayerId, TREASURE_DATA_KEY, null);
        if (!treasure || treasure.treasureId !== args.treasureId) return fail("no_treasure", "찾는 보물이 아니다");
        if (Date.parse(treasure.expiresUtc) <= now) {
            saveData(currentPlayerId, TREASURE_DATA_KEY, null);
            return fail("expired", "보물이 사라졌다");
        }
        rolls = TREASURE_ROLLS;
    } else {
        if (!isInt(args.chest) || args.chest < 1 || args.chest > CHEST_COUNT) return fail("bad_args", "chest 는 1~" + CHEST_COUNT);
        chests = loadData(currentPlayerId, CHEST_DATA_KEY, {});
        var opened = chests[args.chest] && Date.parse(chests[args.chest].openedUtc);
        if (opened && now - opened < CHEST_RECHARGE_SECONDS * 1000) {
            return fail("recharging", "상자가 다시 차는 중", { waitMs: CHEST_RECHARGE_SECONDS * 1000 - (now - opened) });
        }
    }
    var inv = readInventory(currentPlayerId);
    rolls += wornHatEffect(inv).chestRollBonus || 0;
    var owned = loadData(currentPlayerId, BLUEPRINTS_DATA_KEY, []);
    var loot = rollChest(rolls, owned);

    if (chests) {
        chests[args.chest] = { openedUtc: new Date(now).toISOString() };
        saveData(currentPlayerId, CHEST_DATA_KEY, chests);
    } else {
        saveData(currentPlayerId, TREASURE_DATA_KEY, null);
    }
    if (loot.blueprints.length > 0) saveData(currentPlayerId, BLUEPRINTS_DATA_KEY, owned.concat(loot.blueprints));
    loot.blueprints.forEach(addToCodex);
    var reply = { rolls: rolls, items: loot.items, blueprints: loot.blueprints };
    var deltas = {};
    loot.items.forEach(function (e) { deltas[e.itemId] = e.count; });
    if (loot.items.length > 0 && roomCheck(inv, deltas)) {
        reply.drop = recordDrop(currentPlayerId, loot.items);
    } else if (loot.items.length > 0) {
        giveItems(currentPlayerId, loot.items);
        loot.items.forEach(function (it) { addToCodex(it.itemId); });
    }
    if (loot.items.length > 0 || loot.blueprints.length > 0) bumpAchievements({});
    return reply;
};

// ---- 8.1c 잠긴 금고 폐허 (M104-08) — 열쇠패 1장으로 3번 굴림, 금고마다 사람마다 24시간 ----
// 줄마다 따로 굴림(CHEST_LOOT 와 같은 방식). 확률 고지 = DESIGN 8.1c 표.
var VAULT_LOOT = [
    { itemId: "item.ruby", chance: 0.12 },
    { itemId: "item.sapphire", chance: 0.12 },
    { itemId: "item.amethyst", chance: 0.12 },
    { itemId: "item.aquamarine", chance: 0.12 },
    { itemId: "item.emerald", chance: 0.12 },
    { itemId: "item.strawberry_hat", chance: 0.08 },
    { itemId: "item.looter_hat", chance: 0.08 },
    { itemId: "item.boat_motor", chance: 0.08 },
    { itemId: "item.boat_motor_electric", chance: 0.06 },
    { itemId: "item.boat_container", chance: 0.08 },
    { itemId: "item.solar_kit", chance: 0.12 },
    { itemId: "item.battery", chance: 0.12 },
    { itemId: "item.light_bulb", chance: 0.12 },
    { itemId: "item.ammo_rifle", chance: 0.15, count: 20 },
    { itemId: "item.treasure_map", chance: 0.20 }
];
var VAULT_BLUEPRINT_CHANCE = 0.3;
var VAULT_ROLLS = 3;
var VAULT_COUNT = 3;                       // 설원·숲·늪 한 곳씩 (ruin 1..3)
var VAULT_RECHARGE_SECONDS = 24 * 60 * 60;
var VAULT_DATA_KEY = "vaults";

/** 금고 굴림 rolls 번. 설계도는 아직 없는 것 중 하나, 한 번 연 금고에서 같은 설계도는 두 번 안 나온다. */
function rollVault(rolls, ownedBlueprints) {
    var counts = {};
    var blueprints = [];
    for (var r = 0; r < rolls; r++) {
        VAULT_LOOT.forEach(function (line) {
            if (Math.random() < line.chance) counts[line.itemId] = (counts[line.itemId] || 0) + (line.count || 1);
        });
        if (Math.random() < VAULT_BLUEPRINT_CHANCE) {
            var left = BLUEPRINT_IDS.filter(function (id) { return ownedBlueprints.indexOf(id) === -1 && blueprints.indexOf(id) === -1; });
            if (left.length > 0) blueprints.push(left[Math.floor(Math.random() * left.length)]);
        }
    }
    return { items: toList(counts), blueprints: blueprints };
}

/**
 * 금고 열기 — `ruin` 1..3. 열쇠패 1장을 서버가 빼고 굴려 준다. 열쇠패가 없으면 `no_key`, 24시간 안이면 `recharging`+`waitMs`.
 * 보물꾼 모자는 굴림 +1. 칸이 모자라면 전리품 전부 발밑 가방(OpenChest 와 같은 길). 실패하면 열쇠패는 빠지지 않는다.
 * ponytail: 서버는 위치를 모른다 — "금고 앞에 섰다" 는 부르는 기기 판정(OpenChest 와 같은 신뢰).
 */
handlers.OpenVault = function (args, context) {
    args = args || {};
    var now = Date.now();
    if (!isInt(args.ruin) || args.ruin < 1 || args.ruin > VAULT_COUNT) return fail("bad_args", "ruin 은 1~" + VAULT_COUNT);
    var vaults = loadData(currentPlayerId, VAULT_DATA_KEY, {});
    var opened = vaults[args.ruin] && Date.parse(vaults[args.ruin].openedUtc);
    if (opened && now - opened < VAULT_RECHARGE_SECONDS * 1000) {
        return fail("recharging", "금고가 다시 차는 중", { waitMs: VAULT_RECHARGE_SECONDS * 1000 - (now - opened) });
    }
    var inv = readInventory(currentPlayerId);
    if (countOf(inv, VAULT_PASS) < 1) return fail("no_key", "금고 열쇠패가 없다", { itemId: VAULT_PASS });
    var rolls = VAULT_ROLLS + (wornHatEffect(inv).chestRollBonus || 0);
    var owned = loadData(currentPlayerId, BLUEPRINTS_DATA_KEY, []);
    var loot = rollVault(rolls, owned);

    takeItems(currentPlayerId, inv, VAULT_PASS, 1);
    vaults[args.ruin] = { openedUtc: new Date(now).toISOString() };
    saveData(currentPlayerId, VAULT_DATA_KEY, vaults);
    if (loot.blueprints.length > 0) saveData(currentPlayerId, BLUEPRINTS_DATA_KEY, owned.concat(loot.blueprints));
    loot.blueprints.forEach(addToCodex);
    var reply = { rolls: rolls, items: loot.items, blueprints: loot.blueprints };
    var deltas = {};
    loot.items.forEach(function (e) { deltas[e.itemId] = e.count; });
    // 열쇠패가 빠진 뒤의 칸 수로 다시 본다
    if (loot.items.length > 0 && roomCheck(readInventory(currentPlayerId), deltas)) {
        reply.drop = recordDrop(currentPlayerId, loot.items);
    } else if (loot.items.length > 0) {
        giveItems(currentPlayerId, loot.items);
        loot.items.forEach(function (it) { addToCodex(it.itemId); });
    }
    if (loot.items.length > 0 || loot.blueprints.length > 0) bumpAchievements({});
    return reply;
};
var BOAT_DATA_KEY = "boat";

// ---- 8.3 도감 원본 맞춤 — 카테고리 다섯 ----
// 사냥 칸은 **실제 게임에 서는 종만**(M5-07e) — 자리표시 돼지·소·양(`bPlaceholderSpecies`)은 기본 플레이에 안 서므로 칸이 아니다.
// 클라이언트 `UCodexScreenWidget::BuildSlots` 와 같은 목록이어야 한다 — 2층 `Snarehaven.Codex.Hunt.MatchesServer` 가 맞댄다.
var HUNT_ANIMALS = ["animal.fox", "animal.goat", "animal.reindeer", "animal.elk", "animal.lynx", "animal.wolf",
    "animal.wolverine", "animal.bear", "animal.pheasant", "animal.peacock", "animal.ptarmigan",
    "animal.capercaillie"];
// 깃털·뿔(트로피) — 원본 "사냥(깃털·트로피)" 칸. 좋은 품질(pristine)도 칸이 따로고, 깃털은 새 4종마다 따로다.
// 평평한 배열 하나로 둔다(concat 금지) — 2층 `Snarehaven.Codex.Hunt.MatchesServer` 가 이 선언을 글자로 읽는다.
var HUNT_TROPHIES = ["item.fox_tail", "item.pristine_fox_tail", "item.goat_horn", "item.pristine_goat_horn",
    "item.reindeer_antler", "item.pristine_reindeer_antler", "item.elk_antler", "item.pristine_elk_antler",
    "item.lynx_tooth", "item.pristine_lynx_tooth", "item.wolf_tooth", "item.wolverine_skull",
    "item.pristine_wolverine_skull", "item.bear_paw", "item.pristine_bear_paw",
    "item.pheasant_feather", "item.pristine_pheasant_feather", "item.peacock_feather", "item.pristine_peacock_feather",
    "item.ptarmigan_feather", "item.pristine_ptarmigan_feather", "item.capercaillie_feather",
    "item.pristine_capercaillie_feather"];
// 낙각(shed_antler)은 목장 생산물이라 사냥 칸이 아니다 — 클라이언트 도감(사냥 드롭에서 셈)과 칸 수를 맞춘다.
var CODEX_CATEGORIES = {
    fishing: Object.keys(FISH_TABLE),
    hunt: HUNT_ANIMALS.concat(HUNT_TROPHIES),
    plant: Object.keys(CROP_HARVEST_COINS).concat(["item.cedar"]), // 산딸기·나무는 작물 표에 이미 있다
    blueprint: BLUEPRINT_IDS,
    cooking: Object.keys(RECIPES).filter(function (id) { return RECIPES[id].place === "fire"; }),
    // M21-07b 변종 12칸 — 칸 ID = 변종 펫 아이템, `ClaimLoot` 가 받는 순간 찍는다. 보상 계산엔 안 넣는다(CODEX_REWARD_EXCLUDED).
    variant: VARIANT_PET_ITEMS
};
// 목줄·특별 상인·업적 도감 % 에서 빼는 갈래 — 3% 운이 탈것·상인 길을 막으면 안 된다 (DESIGN 10.6 「변종 도감」).
var CODEX_REWARD_EXCLUDED = ["variant"];
var CODEX_UNLOCK_PERCENT = 50;
var CODEX_MERCHANT_PERCENT = 100;

// ---- 도감 보상 ----

/**
 * 도감 카테고리별 진행과 보상 열림 (DESIGN 8.3). 클라이언트는 이 답으로만 먼바다·선착장·특별 상인을 켠다.
 * `entries` = 찬 칸 목록 그대로(M13-26) — 도감은 Internal Data 라 클라 스냅샷 `CodexEntries` 는 이 칸으로만 채운다.
 * `achievements` = 업적 저장값 그대로 `{counters, done}`(M13-28) — 클라 스냅샷 `Achievements` 는 이 칸으로만 채운다.
 */
handlers.GetCodexRewards = function (args, context) {
    var codex = loadData(currentPlayerId, "codex", []).filter(isCodexSlot);
    var reply = codexProgress(codex);
    reply.entries = codex;
    reply.achievements = loadData(currentPlayerId, ACHIEVEMENTS_DATA_KEY, { counters: {}, done: [] });
    return reply;
};

// ---- 8.3 탈것 — 목줄·종·안장 (결정 009) ----
// 도감 카테고리 하나가 100% 가 되면 그 목줄 1개(계정당 한 번). 목줄 1개 = 탈것 종 하나. 안장은 모습뿐.
var CODEX_COLLARS = { hunt: "item.collar_hunting", plant: "item.collar_farming", fishing: "item.collar_fishing" };
var CODEX_COLLARS_DATA_KEY = "codexCollars";
// 원본 8종(무스 = elk). DT_AnimalTable 에 있는 종만 — 2층 `Snarehaven.Mount.Species.MatchesServer` 가 맞댄다.
var MOUNT_SPECIES = ["animal.wolf", "animal.bear", "animal.fox", "animal.goat", "animal.elk", "animal.reindeer",
    "animal.wolverine", "animal.lynx"];
var SADDLE_ITEMS = ["item.saddle_hunting", "item.saddle_farming", "item.saddle_fishing"];
var MOUNTS_DATA_KEY = "mounts";
var ADV_WORKBENCH_ITEM = "item.workbench_adv";

/** addToCodex 가 저장한 뒤 부른다. 새로 100% 가 된 카테고리만 표시를 먼저 적고(중복 0) 목줄을 준다(나중). */
function grantCodexCollars(codex) {
    var cats = codexProgress(codex).categories;
    var given = loadData(currentPlayerId, CODEX_COLLARS_DATA_KEY, []);
    var fresh = Object.keys(CODEX_COLLARS).filter(function (c) {
        return cats[c].have >= cats[c].total && given.indexOf(c) === -1;
    });
    if (fresh.length === 0) return;
    saveData(currentPlayerId, CODEX_COLLARS_DATA_KEY, given.concat(fresh));
    // 🟡 보상은 한 번뿐이라 가방이 차 있어도 준다(roomCheck 없음 — 넘친 가방은 빼기만 된다).
    giveItems(currentPlayerId, fresh.map(function (c) { return { itemId: CODEX_COLLARS[c], count: 1 }; }));
}

function loadMounts() {
    return loadData(currentPlayerId, MOUNTS_DATA_KEY, { species: [], saddles: {} });
}

function mountsReply(m) {
    return { mounts: m.species, saddles: m.saddles, species: MOUNT_SPECIES };
}

function hasPlaced(playFabId, itemId) {
    return loadBase(playFabId).tents.some(function (t) {
        return t.items.some(function (s) { return s.itemId === itemId; });
    });
}

/** 가진 탈것 종·안장과 고를 수 있는 종 목록. 클라이언트는 이 답으로만 탑승·종 고르기를 연다. */
handlers.GetMounts = function (args, context) {
    return mountsReply(loadMounts());
};

/** 목줄 1개 → 탈것 종 하나(잡화점). 이미 가진 종은 `already`, 목줄은 그대로. */
handlers.ExchangeCollar = function (args, context) {
    args = args || {};
    var collars = Object.keys(CODEX_COLLARS).map(function (c) { return CODEX_COLLARS[c]; });
    if (collars.indexOf(args.collarItemId) === -1) return fail("bad_args", "목줄이 아니다: " + args.collarItemId);
    if (MOUNT_SPECIES.indexOf(args.speciesId) === -1) return fail("bad_args", "탈것 종이 아니다: " + args.speciesId);
    var m = loadMounts();
    if (m.species.indexOf(args.speciesId) !== -1) return fail("already", "이미 가진 탈것이다", { speciesId: args.speciesId });
    var inv = readInventory(currentPlayerId);
    if (countOf(inv, args.collarItemId) < 1) return fail("not_enough_items", "목줄이 없다", { itemId: args.collarItemId });

    takeItems(currentPlayerId, inv, args.collarItemId, 1);
    m.species.push(args.speciesId);
    saveData(currentPlayerId, MOUNTS_DATA_KEY, m);
    return mountsReply(m);
};

/** 안장 1개를 가진 종에 단다(고급 작업대 부착 — 모습뿐). 종마다 1개, 바꾸면 전 안장은 돌려주지 않는다(🟡). */
handlers.AttachSaddle = function (args, context) {
    args = args || {};
    if (SADDLE_ITEMS.indexOf(args.saddleItemId) === -1) return fail("bad_args", "안장이 아니다: " + args.saddleItemId);
    var m = loadMounts();
    if (m.species.indexOf(args.speciesId) === -1) return fail("locked", "그 탈것이 없다", { speciesId: args.speciesId });
    var inv = readInventory(currentPlayerId);
    if (countOf(inv, args.saddleItemId) < 1) return fail("not_enough_items", "안장이 없다", { itemId: args.saddleItemId });

    takeItems(currentPlayerId, inv, args.saddleItemId, 1);
    m.saddles[args.speciesId] = args.saddleItemId;
    saveData(currentPlayerId, MOUNTS_DATA_KEY, m);
    return mountsReply(m);
};

// ---- 제작·먹기 ----

handlers.CraftItem = function (args, context) {
    args = args || {};
    var recipe = RECIPES[args.recipeId];
    if (!recipe) return fail("bad_args", "알 수 없는 recipeId: " + args.recipeId);
    var times = args.times === undefined ? 1 : args.times;
    if (!isCount(times)) return fail("bad_args", "times 는 1 이상 정수");
    if (recipe.blueprint && loadData(currentPlayerId, BLUEPRINTS_DATA_KEY, []).indexOf(recipe.blueprint) === -1) {
        return fail("locked", "설계도가 없다", { blueprint: recipe.blueprint });
    }
    if (recipe.place === "workbench" && loadBase(currentPlayerId).tents.length === 0) {
        return fail("locked", "작업대가 없다(텐트 먼저)");
    }
    if (recipe.advWorkbench && !hasPlaced(currentPlayerId, ADV_WORKBENCH_ITEM)) {
        return fail("locked", "고급 작업대가 없다", { need: ADV_WORKBENCH_ITEM });
    }
    if (recipe.place === "smelter" && !hasPlaced(currentPlayerId, SMELTER_ITEM)) {
        return fail("locked", "제련소가 없다", { need: SMELTER_ITEM });
    }

    var inv = readInventory(currentPlayerId);
    var take = pickInputs(inv, recipe, times);
    if (take.error) return take;
    var made = (recipe.yield || 1) * times;
    var deltas = negated(take);
    deltas[args.recipeId] = (deltas[args.recipeId] || 0) + made;
    var full = roomCheck(inv, deltas);
    if (full) return full;

    takeAll(currentPlayerId, inv, take);
    giveItems(currentPlayerId, [{ itemId: args.recipeId, count: made }]);
    if (recipe.place === "fire") addToCodex(args.recipeId);
    bumpAchievements({ craft: times });
    return { crafted: args.recipeId, count: made, used: toList(take) };
};

/** 먹기 — 1개를 빼고 회복량을 돌려준다(음수 = 독, 0 = 버프만 — 13.6 손난로). 체력·버프는 응답을 받은 호스트가 채운다(DESIGN 8.10·13.3). */
handlers.ConsumeItem = function (args, context) {
    args = args || {};
    var heal = RECIPES[args.itemId] ? RECIPES[args.itemId].heal : RAW_HEAL[args.itemId];
    if (typeof heal !== "number") return fail("bad_args", "먹을 수 없는 아이템: " + args.itemId);
    var inv = readInventory(currentPlayerId);
    if (countOf(inv, args.itemId) < 1) return fail("not_enough_items", "가지고 있지 않다", { itemId: args.itemId });
    takeItems(currentPlayerId, inv, args.itemId, 1);
    return { itemId: args.itemId, heal: heal };
};

/** 물 채우기 — 빈 병 1 → 샘물 병 1 (DESIGN 13.6). */
// ponytail: 서버는 위치를 모른다 — "물 400cm 안" 은 부르는 기기가 판정한다(ClaimLoot 같은 신뢰). 빈 병이 1코인이라
//           속여도 얻는 것은 샘물 병 한 개(+15)뿐이다. 커지면 호스트 서명 토큰(RollFish)으로 바꾼다.
handlers.FillBottle = function (args, context) {
    args = args || {};
    var filled = FILL_ITEMS[args.itemId];
    if (!filled) return fail("bad_args", "채울 수 없는 아이템: " + args.itemId);
    var inv = readInventory(currentPlayerId);
    if (countOf(inv, args.itemId) < 1) return fail("not_enough_items", "가지고 있지 않다", { itemId: args.itemId });
    var deltas = {};
    deltas[args.itemId] = -1;
    deltas[filled] = 1;
    var full = roomCheck(inv, deltas);
    if (full) return full;
    takeItems(currentPlayerId, inv, args.itemId, 1);
    giveItems(currentPlayerId, [{ itemId: filled, count: 1 }]);
    return { itemId: filled, count: 1 };
};

// ---- 사냥 총 재장전 (DESIGN 8.6-c · M39-02) ----

// 총 행 → 탄창·탄약. 값 = DT_ItemTable `MagazineSize`·`AmmoItemID` (node 시험이 생성기 행과 맞댄다).
var GUNS = {
    "item.pistol_semi": { magazine: 7, ammo: "item.ammo" },
    "item.rifle_modern": { magazine: 5, ammo: "item.ammo_rifle" },
    "item.shotgun": { magazine: 6, ammo: "item.ammo_shotgun" },
    "item.crossbow": { magazine: 1, ammo: "item.arrow" },
    "item.pistol_auto": { magazine: 15, ammo: "item.ammo" },
    "item.smg": { magazine: 30, ammo: "item.ammo" },
    "item.rifle_burst": { magazine: 12, ammo: "item.ammo_rifle" },
    "item.rifle_semi": { magazine: 10, ammo: "item.ammo_rifle" },
    "item.shotgun_auto": { magazine: 8, ammo: "item.ammo_shotgun" },
    "item.shotgun_short": { magazine: 2, ammo: "item.ammo_shotgun" },
    "item.mg": { magazine: 40, ammo: "item.ammo_rifle" },
    "item.bazooka": { magazine: 1, ammo: "item.rocket" }
};
// 테스터 키트 — 먹는 것 전부(요리 `RECIPES[*].heal` + 날로 먹기 `RAW_HEAL`, 독은 뺀다) = `ConsumeItem` 이 받는 표 그대로.
Object.keys(RECIPES).filter(function (id) { return RECIPES[id].heal >= 0; })
    .concat(Object.keys(RAW_HEAL).filter(function (id) { return RAW_HEAL[id] >= 0; }))
    .forEach(function (id) { addTesterKit(id, TESTER_KIT_CONSUMABLE_COUNT); });
// 테스터 키트 — 총 전부 1정씩 + 그 탄 200발(로켓은 DESIGN 「테스터 키트 v3 바주카」 30발). 표가 늘면 키트도 따라 는다.
var TESTER_KIT_AMMO = 200;
var TESTER_KIT_ROCKETS = 30;
Object.keys(GUNS).forEach(function (gunId) {
    var ammo = GUNS[gunId].ammo;
    addTesterKit(gunId, 1);
    addTesterKit(ammo, ammo === "item.rocket" ? TESTER_KIT_ROCKETS : TESTER_KIT_AMMO);
});

/** 재장전 — 탄약을 빼서 탄창을 채운다. 쏘기는 서버를 안 부르니 클라는 응답 `loaded` 로만 탄창을 채운다(규칙 8). */
handlers.LoadAmmo = function (args, context) {
    args = args || {};
    var gun = GUNS[args.gun];
    if (!gun) return fail("bad_args", "총이 아니다: " + args.gun);
    var inv = readInventory(currentPlayerId);
    if (countOf(inv, args.gun) < 1) return fail("no_gun", "총을 가지고 있지 않다", { itemId: args.gun });
    var have = Math.max(0, Math.min(gun.magazine, Math.floor(Number(args.have) || 0)));
    var need = gun.magazine - have;
    var owned = countOf(inv, gun.ammo);
    if (need > 0 && owned < 1) return fail("not_enough_items", "탄약이 없다", { itemId: gun.ammo, need: 1, have: 0 });
    var removed = Math.min(need, owned);
    if (removed > 0) takeItems(currentPlayerId, inv, gun.ammo, removed);
    return { gun: args.gun, loaded: have + removed, removed: removed, left: owned - removed };
};

// ---- 사냥 드롭·채집·포획 받기 ----

// 클라이언트 대기열(QueuePendingGrant)을 한 번에 비운다. 판정은 호스트가 했고 서버는 목록·수량만 거른다.
// ponytail: 호스트 판정을 믿는다 — 조작 클라이언트는 한도 안에서 찍어낼 수 있다. 상한 = 호출당 20개·10초 간격.
//           막아야 할 만큼 커지면 호스트가 서명한 처치 토큰(RollFish 처럼)으로 바꾼다.
var LOOT_MAX_PER_CALL = 20;   // 한 번에 짐승 ~6마리(드롭 최대 3) + 채집 몇 번
var LOOT_MIN_GAP_MS = 10000;
// 출시 점검(10-16) — 받는 개수는 호스트·클라 말을 믿으므로 계정당 하루(UTC) 총량과 그 재료를 판 코인에 상한을 둔다(DESIGN 4.8).
var LOOT_DAILY_MAX = 3000;          // 하루 받기 총량(도구 +1·모자 배수 전 개수)
var LOOT_SELL_DAILY_COINS = 2000;   // 사냥·채집 재료(와 그걸 넣어 만든 것)를 SellItem 으로 판 하루 코인
var LOOT_DATA_KEY = "lootAt";       // {atMs, day, n, coins} — 옛 꼴은 숫자(마지막 받은 시각)
// 존 칸 채집 더미 (DESIGN 13.4 · M13-02) — 맨손 채집이라 사냥 업적에 안 센다.
var GATHER_ITEMS = ["item.wood", "item.cedar", "item.berry",
    "item.chanterelle", "item.penny_bun", "item.yellowleg_bonnet", "item.fly_amanita", "item.resin",
    "item.kingcup", "item.windflower", "item.hepatica", "item.poppy", "item.fireweed",
    "item.blackberry", "item.mezereon", "item.cloudberry", "item.black_lotus", "item.blueberry", "item.lingonberry",
    "item.sugar_beet", "item.wheat",
    "item.honey_mushroom", "item.king_stropharia", "item.oyster_mushroom", "item.vipers_mane", "item.bog_flower", "item.water_lily"
].concat(Object.keys(ORE_CHUNK_PRICE)); // 13.4 평원 야생 사탕무·밀 · 늪 버섯·늪꽃·수련 · 8.16 광맥
// 동물 행(DT_AnimalTable `DeathDropItems`) 의 고기·가죽 — 트로피는 HUNT_TROPHIES. 이름 꼴(`_meat`)로 받지 않는다(M22 ①):
// 없는 종의 고기·펫을 지어내 보내면 카탈로그 밖 아이템이 생긴다. node 시험이 생성기 동물·채집 행과 맞댄다.
var ANIMAL_DROP_ITEMS = ["item.pig_meat", "item.pig_hide", "item.cow_meat", "item.cow_hide",
    "item.sheep_meat", "item.sheep_hide", "item.fox_meat", "item.goat_meat", "item.reindeer_meat", "item.elk_meat",
    "item.lynx_meat", "item.wolf_meat", "item.wolverine_meat", "item.bear_meat", "item.bird_meat"];
// 지시(감독 10-20 19:30) ① — 사냥 드롭은 서버가 굴린다. 호스트·클라는 `ClaimLoot {kills:[{animalId, count}]}` 로 잡은 종만 알린다.
// 표 = 생성기 동물 행 `DeathDropItems`·`DeathDropCountMin/Max`·`PristineDropChance`(DESIGN 3.1·M22) — node 시험이 행마다 맞댄다.
// 굴림은 클라 `UAnimalStateComponent::RollDropItems` 와 같다: 개수 min~max 균등, 한 개마다 최상급(`item.pristine_`) 확률, 아니면 일반 중 균등.
var KILL_DROP_MIN = 1;
var KILL_DROP_MAX = 3;
var KILL_PRISTINE_CHANCE = 0.05;
var KILL_DROPS = {
    "animal.pig": ["item.pig_meat", "item.pig_hide"],
    "animal.cow": ["item.cow_meat", "item.cow_hide"],
    "animal.sheep": ["item.sheep_meat", "item.sheep_hide"],
    "animal.fox": ["item.fox_meat", "item.fox_tail", "item.pristine_fox_tail"],
    "animal.goat": ["item.goat_meat", "item.goat_horn", "item.pristine_goat_horn"],
    "animal.reindeer": ["item.reindeer_meat", "item.reindeer_antler", "item.pristine_reindeer_antler"],
    "animal.elk": ["item.elk_meat", "item.elk_antler", "item.pristine_elk_antler"],
    "animal.lynx": ["item.lynx_meat", "item.lynx_tooth", "item.pristine_lynx_tooth"],
    "animal.wolf": ["item.wolf_meat", "item.wolf_tooth"],
    "animal.wolverine": ["item.wolverine_meat", "item.wolverine_skull", "item.pristine_wolverine_skull"],
    "animal.bear": ["item.bear_meat", "item.bear_paw", "item.pristine_bear_paw"],
    "animal.pheasant": ["item.bird_meat", "item.pheasant_feather", "item.pristine_pheasant_feather"],
    "animal.peacock": ["item.bird_meat", "item.peacock_feather", "item.pristine_peacock_feather"],
    "animal.ptarmigan": ["item.bird_meat", "item.ptarmigan_feather", "item.pristine_ptarmigan_feather"],
    "animal.capercaillie": ["item.bird_meat", "item.capercaillie_feather", "item.pristine_capercaillie_feather"],
    "animal.fogbuck": ["item.reindeer_meat", "item.reindeer_antler"]
};

/** 한 마리 몫 드롭을 `deltas` 에 더하고 개수를 돌려준다. 최상급 확률 × 사냥꾼 모자 배수(13.5 `pristineMul`). */
function rollKillDrops(animalId, pristineMul, deltas) {
    var pool = KILL_DROPS[animalId];
    var pristine = pool.filter(function (id) { return /^item\.pristine_/.test(id); }); // 클라 `IsPristineItem` 과 같은 머리
    var plain = pool.filter(function (id) { return !/^item\.pristine_/.test(id); });
    var n = KILL_DROP_MIN + Math.floor(Math.random() * (KILL_DROP_MAX - KILL_DROP_MIN + 1));
    for (var i = 0; i < n; i++) {
        var pick = pristine.length === 0 ? plain : (plain.length === 0 || Math.random() < KILL_PRISTINE_CHANCE * pristineMul) ? pristine : plain;
        var id = pick[Math.floor(Math.random() * pick.length)];
        deltas[id] = (deltas[id] || 0) + 1;
    }
    return n;
}
// 포획 = 동물 행 `PetItemID`(`animal.<종>` ↔ `item.pet_<종>`) — 목장에 놓을 수 있는 종만.
var CAPTURE_PET_ITEMS = Object.keys(ANIMAL_PRODUCT_ITEM).map(function (a) { return "item.pet_" + a.slice("animal.".length); });
// 5장 23단계 「안개뿔」(docs/specs/story-fogbuck.md) — 이 펫만 따로 세는 카운터. 카탈로그 추가는 감독 몫.
// 아이템 표·동물 행은 조각 2 에서 만든다 — 그때까지 표 대조 시험(M68-01)이 막지 않게 종 이름으로 조립한다(`CAPTURE_PET_ITEMS` 와 같은 방식).
var FOGBUCK_PET_ITEM = "item.pet_" + "fogbuck";
function isLootItem(id) {
    return ANIMAL_DROP_ITEMS.indexOf(id) !== -1 || CAPTURE_PET_ITEMS.indexOf(id) !== -1 || id === FOGBUCK_PET_ITEM || VARIANT_PET_ITEMS.indexOf(id) !== -1
        || HUNT_TROPHIES.indexOf(id) !== -1 || GATHER_ITEMS.indexOf(id) !== -1;
}

function countListed(list, itemId) {
    return list.reduce(function (s, it) { return s + (it.itemId === itemId ? it.count : 0); }, 0);
}

/** 오늘(UTC)의 받기 기록. 날이 바뀌면 개수·코인만 0 으로(간격 검사용 시각은 남긴다). */
function lootState() {
    var raw = loadData(currentPlayerId, LOOT_DATA_KEY, 0);
    var state = typeof raw === "number" ? { atMs: raw, day: -1, n: 0, coins: 0 } : raw;
    var day = questDay();
    if (state.day !== day) { state.day = day; state.n = 0; state.coins = 0; }
    return state;
}

/** 사냥·채집 재료이거나, 재료(고기·가죽·깃털 꼬리표 포함)를 넣어 만든 것 — 판매 하루 코인 상한에 든다. */
function isLootDerived(itemId, depth) {
    if (isLootItem(itemId)) return true;
    var recipe = RECIPES[itemId];
    if (!recipe || (depth || 0) > 4) return false;
    return recipe.inputs.some(function (input) {
        if (input.itemId) return isLootDerived(input.itemId, (depth || 0) + 1);
        if (input.tag === "Meat" || input.tag === "Hide" || input.tag === "Feather") return true;
        return (CRAFT_TAG_ITEMS[input.tag] || []).some(function (id) { return isLootDerived(id, (depth || 0) + 1); });
    });
}

handlers.ClaimLoot = function (args, context) {
    var items = (args && args.items) || [];
    var kills = (args && args.kills) || [];
    if (!Array.isArray(items) || !Array.isArray(kills) || items.length + kills.length === 0) return fail("bad_args", "items 또는 kills 필요");
    var deltas = {};
    var total = 0;
    for (var i = 0; i < items.length; i++) {
        var e = items[i] || {};
        if (!isLootItem(e.itemId) || !isCount(e.count)) return fail("bad_args", "받을 수 없는 아이템: " + e.itemId);
        deltas[e.itemId] = (deltas[e.itemId] || 0) + e.count;
        total += e.count;
    }
    // 잡은 마리 수 × 드롭 최대 개수로 한 번 한도를 미리 본다 — 굴림 운에 따라 같은 요청이 붙었다 떨어졌다 하지 않게.
    var killCount = 0;
    for (var k = 0; k < kills.length; k++) {
        var kill = kills[k] || {};
        if (!KILL_DROPS.hasOwnProperty(kill.animalId) || !isCount(kill.count)) return fail("bad_args", "드롭 표에 없는 동물: " + kill.animalId);
        killCount += kill.count;
    }
    if (total + killCount * KILL_DROP_MAX > LOOT_MAX_PER_CALL) {
        return fail("too_many", "한 번에 " + LOOT_MAX_PER_CALL + "개까지", { total: total + killCount * KILL_DROP_MAX });
    }

    var now = Date.now();
    var loot = lootState();
    if (now - loot.atMs < LOOT_MIN_GAP_MS) return fail("too_soon", "잠시 뒤 다시", { waitMs: LOOT_MIN_GAP_MS - (now - loot.atMs) });
    if (loot.n + total + killCount * KILL_DROP_MAX > LOOT_DAILY_MAX) {
        return fail("daily_limit", "오늘 받을 수 있는 만큼 다 받았다", { limit: LOOT_DAILY_MAX, left: LOOT_DAILY_MAX - loot.n });
    }

    var inv = readInventory(currentPlayerId);
    var pristineMul = wornHatEffect(inv).pristineMul || 1;
    kills.forEach(function (kill) {
        for (var c = 0; c < kill.count; c++) total += rollKillDrops(kill.animalId, pristineMul, deltas);
    });
    // 13.5 도구 — 그 도구로 얻었다고 온 번(toolUses)마다 +1, 도구가 가방에 있을 때만. 나무꾼 ×2 보다 먼저(DESIGN 13.5).
    items.forEach(function (e) {
        var tool = HAT_WOOD_ITEMS.indexOf(e.itemId) !== -1 ? "item.chainsaw" : ORE_CHUNK_PRICE[e.itemId] ? "item.pickaxe_electric" : GATHER_ITEMS.indexOf(e.itemId) !== -1 ? "item.machete" : null;
        if (tool && isCount(e.toolUses) && countOf(inv, tool) > 0) deltas[e.itemId] += Math.min(e.toolUses, e.count);
    });
    var woodMul = wornHatEffect(inv).woodMul || 1; // 13.5 도끼꾼 털모자 — 한도는 배수 전 개수로 본다
    HAT_WOOD_ITEMS.forEach(function (id) { if (deltas[id]) deltas[id] *= woodMul; });
    var full = roomCheck(inv, deltas);
    if (full) return full;

    var list = Object.keys(deltas).map(function (id) { return { itemId: id, count: deltas[id] }; });
    var crate = rollCrateDrop(inv, deltas); // 10.8 — 호출 한 번에 한 번 (10초 간격이 남용을 막는다)
    giveItems(currentPlayerId, crate ? list.concat([{ itemId: crate, count: 1 }]) : list);
    loot.atMs = now;
    loot.n += total;
    saveData(currentPlayerId, LOOT_DATA_KEY, loot);
    list.forEach(function (it) { addToCodex(it.itemId); });
    // ponytail: 호스트 판정을 믿는 받기라 업적도 위 한도 안에서 찍어낼 수 있다 — 처치 토큰이 생기면 같이 막힌다.
    var capture = 0, hunt = 0, petKinds = [];
    list.forEach(function (it) {
        if (it.itemId.indexOf("item.pet_") === 0) { capture += it.count; petKinds.push(it.itemId.replace(/_variant$/, "")); } // 변종은 같은 종
        else if (GATHER_ITEMS.indexOf(it.itemId) === -1) hunt += it.count;
    });
    bumpAchievements({ capture: capture, hunt: hunt, captureFogbuck: countListed(list, FOGBUCK_PET_ITEM) }, petKinds);
    if (!crate) return { granted: list };
    addToCodex(crate);
    return { granted: list.concat([{ itemId: crate, count: 1 }]), crate: crate }; // 획득 팝업이 상자도 띄운다
};

// ---- 버리기·줍기·사망 ----

handlers.DropItem = function (args, context) {
    args = args || {};
    var count = args.count === undefined ? 1 : args.count;
    if (!args.itemId || !isCount(count)) return fail("bad_args", "itemId, count 필요");
    if (!isBackpackItem(args.itemId)) return fail("protected", "펫·가챠 아이템은 떨굴 수 없다");
    var inv = readInventory(currentPlayerId);
    if (countOf(inv, args.itemId) < count) {
        return fail("not_enough_items", "가진 것보다 많다", { need: count, have: countOf(inv, args.itemId) });
    }
    takeItems(currentPlayerId, inv, args.itemId, count);
    return recordDrop(currentPlayerId, [{ itemId: args.itemId, count: count }]);
};

/** 죽으면 배낭 아이템 전부를 가방 하나로. 코인·젬·도감·목장·펫·가챠는 남는다(DESIGN 8.12). */
handlers.DropOnDeath = function (args, context) {
    var inv = readInventory(currentPlayerId);
    var take = {};
    Object.keys(inv.counts).forEach(function (id) {
        if (isBackpackItem(id) && inv.counts[id] > 0) take[id] = inv.counts[id];
    });
    var items = toList(take);
    if (items.length === 0) return { dropped: false };
    takeAll(currentPlayerId, inv, take);
    var drop = recordDrop(currentPlayerId, items);
    drop.dropped = true;
    return drop;
};

handlers.PickupDrop = function (args, context) {
    args = args || {};
    if (!args.ownerId || !args.dropId) return fail("bad_args", "ownerId, dropId 필요");
    var drops = loadData(args.ownerId, DROPS_DATA_KEY, []);
    var index = -1;
    for (var i = 0; i < drops.length; i++) {
        if (drops[i].dropId === args.dropId) index = i;
    }
    if (index === -1) return fail("already_taken", "이미 주웠거나 없는 가방");
    var drop = drops[index];
    if (Date.now() - Date.parse(drop.droppedAtUtc) >= DROP_TTL_SECONDS * 1000) {
        saveData(args.ownerId, DROPS_DATA_KEY, liveDrops(drops));
        return fail("expired", "가방이 사라졌다");
    }
    var inv = readInventory(currentPlayerId);
    var deltas = {};
    drop.items.forEach(function (e) { deltas[e.itemId] = (deltas[e.itemId] || 0) + e.count; });
    var full = roomCheck(inv, deltas);
    if (full) return full;

    // ponytail: 두 사람이 같은 순간 부르면 둘 다 위 확인을 통과할 수 있다 — 내부 데이터엔 비교 후 쓰기가 없다.
    // 실제로 겹치면 Entity Objects 의 ProfileVersion 비교로 옮긴다.
    // 주기 먼저, 지우기 나중 — 지급이 거절되면(요청 한도 등) 가방이 남아 다시 주울 수 있다.
    var refused = tryGiveItems(currentPlayerId, drop.items);
    if (refused) return refused;
    drops.splice(index, 1);
    saveData(args.ownerId, DROPS_DATA_KEY, liveDrops(drops));
    return { picked: true, dropId: drop.dropId, items: drop.items };
};

// ---- 건축·거점 ----

handlers.PlaceTent = function (args, context) {
    args = args || {};
    if (!isInt(args.x) || !isInt(args.y)) return fail("bad_args", "x, y(칸) 필요");
    var zone = args.zone === undefined ? "ranch" : args.zone; // 레퍼런스: 섬(hunt) 빈 땅 어디든 캠프
    if (TENT_ZONES.indexOf(zone) === -1) return fail("bad_args", "zone 은 hunt 또는 ranch");
    var base = loadBase(currentPlayerId);
    if (base.tents.length >= TENT_MAX) return fail("limit", "텐트는 " + TENT_MAX + "개까지", { removed: base.removed });
    for (var i = 0; i < base.tents.length; i++) {
        var t = base.tents[i];
        if ((t.zone || "ranch") !== zone) continue; // 다른 존의 칸 좌표는 다른 월드다
        if (Math.abs(t.x - args.x) < LAND_CELLS && Math.abs(t.y - args.y) < LAND_CELLS) {
            return fail("occupied", "내 다른 땅과 겹친다");
        }
    }
    var inv = readInventory(currentPlayerId);
    if (countOf(inv, TENT_ITEM) < 1) return fail("not_enough_items", "텐트가 없다", { itemId: TENT_ITEM });

    takeItems(currentPlayerId, inv, TENT_ITEM, 1);
    var now = Date.now();
    var tent = {
        tentId: newId(), zone: zone, x: args.x, y: args.y, placedAtUtc: new Date(now).toISOString(),
        upgraded: false, upkeepUntilUtc: null, color: 0, items: []
    };
    base.tents.push(tent);
    saveData(currentPlayerId, BASE_DATA_KEY, base.tents);
    return {
        tentId: tent.tentId, zone: zone, placedAtUtc: tent.placedAtUtc,
        upgradeDeadlineUtc: new Date(now + UPGRADE_DEADLINE_HOURS * HOUR_MS).toISOString(), removed: base.removed
    };
};

/** 벽 장식 놓기 판정 — 집 3×3 발자국(앵커 = 텐트 칸)의 바깥 변. side 0=+Y 1=+X 2=-Y 3=-X. */
function wallHangFail(tent, args) {
    var w = args.wall;
    if (!w || !isInt(w.side) || w.side < 0 || w.side > 3 || !isInt(w.h)) return fail("bad_args", "wall {side 0~3, h} 필요");
    if (w.h < WALL_HEIGHT_MIN || w.h > WALL_HEIGHT_MAX) return fail("bad_args", "걸 높이는 " + WALL_HEIGHT_MIN + "~" + WALL_HEIGHT_MAX);
    if (!tent.upgraded) return fail("not_house", "집이 된 텐트에만 건다");
    var ox = args.x - tent.x;
    var oy = args.y - tent.y;
    var onEdge = (w.side === 0 && oy === HOUSE_CELLS - 1) || (w.side === 2 && oy === 0) ||
        (w.side === 1 && ox === HOUSE_CELLS - 1) || (w.side === 3 && ox === 0);
    if (ox < 0 || oy < 0 || ox >= HOUSE_CELLS || oy >= HOUSE_CELLS || !onEdge) return fail("not_wall", "벽 칸이 아니다");
    for (var i = 0; i < tent.items.length; i++) {
        var o = tent.items[i];
        if (o.wall && o.x === args.x && o.y === args.y && o.wall.side === w.side && Math.abs(o.wall.h - w.h) < WALL_ITEM_GAP) {
            return fail("occupied", "그 자리에 이미 걸려 있다");
        }
    }
    return null;
}

/** 소품 올리기 판정 — `top {host, slot}` 의 받침이 이 텐트의 받침 가구고 그 슬롯이 비었는가. 맞으면 받침 줄을 돌려준다. */
function topPlaceFail(tent, args) {
    var t = args.top;
    if (!t || typeof t.host !== "string" || !isInt(t.slot)) return { fail: fail("bad_args", "top {host, slot} 필요") };
    var host = null;
    tent.items.forEach(function (s) { if (s.structureId === t.host) host = s; });
    if (!host) return { fail: fail("not_found", "받침 가구가 없다") };
    var slots = TOP_HOSTS[host.itemId];
    if (!slots) return { fail: fail("not_host", "그 위엔 올릴 수 없다") };
    if (t.slot < 0 || t.slot >= slots) return { fail: fail("bad_args", "슬롯은 0~" + (slots - 1)) };
    for (var i = 0; i < tent.items.length; i++) {
        var o = tent.items[i];
        if (o.top && o.top.host === t.host && o.top.slot === t.slot) return { fail: fail("occupied", "그 자리에 이미 있다") };
    }
    return { host: host };
}

/** 계단이 덮는 칸들(절대 칸 [x, y]) — 앵커가 왼쪽 아래, 돌림 0·180 이면 X 로 길다. */
function stairCells(s) {
    var alongX = s.rot === 0 || s.rot === 180;
    var cells = [];
    for (var i = 0; i < STAIRS_LENGTH; i++) cells.push(alongX ? [s.x + i, s.y] : [s.x, s.y + i]);
    return cells;
}

function stairOf(tent) {
    for (var i = 0; i < tent.items.length; i++) {
        if (tent.items[i].itemId === STAIRS_ITEM) return tent.items[i];
    }
    return null;
}

function cellIn(cells, x, y) {
    for (var i = 0; i < cells.length; i++) {
        if (cells[i][0] === x && cells[i][1] === y) return true;
    }
    return false;
}

/** 계단 놓기 판정 — 2층 집 안 3×3 의 한 줄, 문 길을 막지 않는 6 자리, 집 하나 1개, 1층 바닥 가구와 안 겹침. */
function stairPlaceFail(tent, args, rot, hx, hy) {
    if (rot % 90 !== 0) return fail("bad_args", "계단은 90° 단위로만 돌린다");
    if (hx !== 0 || hy !== 0) return fail("bad_args", "계단은 온 칸에만");
    if (!tent.upgraded || tent.floors !== 2) return fail("not_floor2", "2층을 올린 집에만 놓는다");
    if (stairOf(tent)) return fail("stair_exists", "집에 계단이 이미 있다");
    var cells = stairCells({ x: args.x, y: args.y, rot: rot });
    for (var i = 0; i < cells.length; i++) {
        var ox = cells[i][0] - tent.x;
        var oy = cells[i][1] - tent.y;
        if (ox < 0 || oy < 0 || ox >= HOUSE_CELLS || oy >= HOUSE_CELLS) return fail("out_of_house", "집 안 3×3 에만 놓는다");
    }
    var alongX = rot === 0 || rot === 180;
    var ax = args.x - tent.x;
    var ay = args.y - tent.y;
    var open = alongX ? (ay === 0 || ay === HOUSE_CELLS - 1) : ax === HOUSE_CELLS - 1;
    if (!open) return fail("blocks_path", "문 길을 막거나 방을 가른다");
    for (var j = 0; j < tent.items.length; j++) {
        var o = tent.items[j];
        if (o.wall || o.top || (o.floor || 1) !== 1 || RUG_ITEMS.indexOf(o.itemId) !== -1) continue;
        if (cellIn(cells, o.x, o.y)) return fail("occupied", "계단 자리에 가구가 있다");
    }
    return null;
}

/** 2층 바닥 놓기 판정 — 2층 집, 계단 있음, 집 안 3×3 에서 계단 구멍 칸 빼고. */
function floor2PlaceFail(tent, args) {
    if (!tent.upgraded || tent.floors !== 2) return fail("not_floor2", "2층을 올린 집에만 놓는다");
    var stair = stairOf(tent);
    if (!stair) return fail("no_stairs", "계단이 있어야 2층에 놓는다");
    var ox = args.x - tent.x;
    var oy = args.y - tent.y;
    if (ox < 0 || oy < 0 || ox >= HOUSE_CELLS || oy >= HOUSE_CELLS) return fail("out_of_house", "2층은 집 안 3×3 뿐이다");
    if (cellIn(stairCells(stair), args.x, args.y)) return fail("stair_hole", "계단 구멍 칸이다");
    return null;
}

function furnitureLoad(tent) {
    var n = 0;
    tent.items.forEach(function (s) { n += s.wall || s.top ? 0.5 : 1; });
    return n;
}

handlers.PlaceStructure = function (args, context) {
    args = args || {};
    var rot = args.rot === undefined ? 0 : args.rot;
    var isWall = WALL_ITEMS.indexOf(args.itemId) !== -1;
    var isTop = TOP_ITEMS.indexOf(args.itemId) !== -1;
    if (!isWall && !isTop && PLACEABLE_ITEMS.indexOf(args.itemId) === -1) return fail("bad_args", "놓을 수 없는 아이템: " + args.itemId);
    if (!isInt(args.x) || !isInt(args.y) || !isInt(rot) || rot < 0 || rot > 345 || rot % 15 !== 0) {
        return fail("bad_args", "x, y(칸), rot(15 의 배수, 0~345) 필요");
    }
    // 세밀 격자(반 칸 = 50cm) 오프셋 — 없으면 0. 0 또는 1 만 (DESIGN 8.11 「세밀 격자」).
    var hx = args.hx === undefined ? 0 : args.hx;
    var hy = args.hy === undefined ? 0 : args.hy;
    if ((hx !== 0 && hx !== 1) || (hy !== 0 && hy !== 1)) return fail("bad_args", "hx, hy 는 0 또는 1");
    var floor = args.floor === undefined ? 1 : args.floor;
    if (floor !== 1 && floor !== 2) return fail("bad_args", "floor 는 1·2");
    if (floor === 2 && (isWall || isTop || args.itemId === STAIRS_ITEM)) return fail("bad_args", "2층엔 바닥 가구·러그만 (소품은 받침 층을 따른다)");
    if (args.pin !== undefined && (args.itemId !== STORAGE_ITEM || !PIN_PATTERN.test(String(args.pin)))) {
        return fail("bad_args", "PIN 은 보관함에만, 숫자 5자리");
    }
    var base = loadBase(currentPlayerId);
    var tent = findTent(base, args.tentId);
    if (tent.error) return tent;
    var half = LAND_CELLS / 2;
    var dx = args.x - tent.x;
    var dy = args.y - tent.y;
    if (dx < -half || dx >= half || dy < -half || dy >= half) return fail("out_of_land", "내 땅 밖이다");
    var host = null;
    if (isWall) {
        var wallErr = wallHangFail(tent, args);
        if (wallErr) return wallErr;
    } else if (isTop) {
        var top = topPlaceFail(tent, args);
        if (top.fail) return top.fail;
        host = top.host;
    } else {
        if (args.itemId === STAIRS_ITEM) {
            var stairErr = stairPlaceFail(tent, args, rot, hx, hy);
            if (stairErr) return stairErr;
        } else if (floor === 2) {
            var upErr = floor2PlaceFail(tent, args);
            if (upErr) return upErr;
        }
        var isRug = RUG_ITEMS.indexOf(args.itemId) !== -1;
        var stair = stairOf(tent);
        if (floor === 1 && !isRug && stair && cellIn(stairCells(stair), args.x, args.y)) return fail("occupied", "계단 자리다");
        for (var i = 0; i < tent.items.length; i++) {
            if (isRug !== (RUG_ITEMS.indexOf(tent.items[i].itemId) !== -1)) continue; // 러그와 바닥 가구는 서로 겹쳐도 된다
            if ((tent.items[i].floor || 1) !== floor) continue; // 층이 다르면 같은 칸이어도 안 겹친다
            if (!tent.items[i].wall && !tent.items[i].top && tent.items[i].x === args.x && tent.items[i].y === args.y && (tent.items[i].hx || 0) === hx && (tent.items[i].hy || 0) === hy) {
                return fail("occupied", "그 자리에 이미 있다");
            }
        }
    }
    var furnitureMax = furnitureMaxOf(tent);
    if (furnitureLoad(tent) >= furnitureMax) return fail("limit", "가구는 땅 하나에 " + furnitureMax + "개까지");
    var inv = readInventory(currentPlayerId);
    if (countOf(inv, args.itemId) < 1) return fail("not_enough_items", "가지고 있지 않다", { itemId: args.itemId });

    takeItems(currentPlayerId, inv, args.itemId, 1);
    var structure = { structureId: newId(), itemId: args.itemId, x: args.x, y: args.y, rot: rot };
    if (isWall) structure.wall = { side: args.wall.side, h: args.wall.h };
    if (isTop) {
        // 자리·돌림은 받침 것을 따른다 — 슬롯이 받침 위 어디인지는 받침 돌림 기준이라 클라가 받침에서 계산한다
        structure.x = host.x;
        structure.y = host.y;
        structure.rot = host.rot;
        hx = host.hx || 0;
        hy = host.hy || 0;
        structure.top = { host: host.structureId, slot: args.top.slot };
        if (host.floor === 2) structure.floor = 2;
    }
    if (floor === 2) structure.floor = 2;
    if (!isWall && (hx !== 0 || hy !== 0)) {
        structure.hx = hx;
        structure.hy = hy;
    }
    if (args.itemId === STORAGE_ITEM) {
        structure.pin = args.pin === undefined ? null : String(args.pin);
        structure.storage = {};
    }
    tent.items.push(structure);
    saveData(currentPlayerId, BASE_DATA_KEY, base.tents);
    return { structureId: structure.structureId, tentId: tent.tentId };
};

/** 가구를 가방으로 — 100% 회수. 🟡 속이 찬 보관함은 먼저 비워야 한다. */
handlers.RemoveStructure = function (args, context) {
    args = args || {};
    var base = loadBase(currentPlayerId);
    var tent = findTent(base, args.tentId);
    if (tent.error) return tent;
    var index = -1;
    for (var i = 0; i < tent.items.length; i++) {
        if (tent.items[i].structureId === args.structureId) index = i;
    }
    if (index === -1) return fail("not_found", "그 가구가 없다");
    var structure = tent.items[index];
    if (structure.storage && Object.keys(structure.storage).length > 0) return fail("not_empty", "보관함을 먼저 비운다");
    for (var j = 0; j < tent.items.length; j++) {
        if (tent.items[j].top && tent.items[j].top.host === structure.structureId) return fail("not_empty", "위에 올린 소품을 먼저 치운다");
        // 계단이 사라지면 2층에 갈 길이 없다 — 위층 가구가 갇히지 않게 먼저 치운다 (M108-06)
        if (structure.itemId === STAIRS_ITEM && tent.items[j].floor === 2) return fail("not_empty", "2층 가구를 먼저 치운다");
    }
    var deltas = {};
    deltas[structure.itemId] = 1;
    var full = roomCheck(readInventory(currentPlayerId), deltas);
    if (full) return full;

    var refused = tryGiveItems(currentPlayerId, [{ itemId: structure.itemId, count: 1 }]);
    if (refused) return refused;
    tent.items.splice(index, 1);
    saveData(currentPlayerId, BASE_DATA_KEY, base.tents);
    return { removed: structure.itemId };
};

// ---- 페인트 (DESIGN 13.8, M13-11) ----
var PAINT_ITEMS = ["item.paint_white", "item.paint_charcoal", "item.paint_blue", "item.paint_red",
    "item.paint_yellow", "item.paint_mint", "item.paint_blush"];

/** 내 땅의 가구 하나를 칠한다 — 페인트 1통이 빠지고 가구 줄 color 에 페인트 ID 가 남는다. 집 색(8.11)은 UpgradeHouse 몫. */
handlers.PaintStructure = function (args, context) {
    args = args || {};
    if (PAINT_ITEMS.indexOf(args.paintId) === -1) return fail("bad_args", "페인트가 아니다: " + args.paintId);
    var base = loadBase(currentPlayerId); // 부른 사람 거점만 읽는다 — 남의 가구는 여기 없다
    var tent = findTent(base, args.tentId);
    if (tent.error) return tent;
    var structure = null;
    tent.items.forEach(function (s) { if (s.structureId === args.structureId) structure = s; });
    if (!structure) return fail("not_found", "그 가구가 없다");
    if (structure.color === args.paintId) return fail("same_color", "이미 그 색이다");
    var inv = readInventory(currentPlayerId);
    if (countOf(inv, args.paintId) < 1) return fail("not_enough_items", "페인트가 없다", { itemId: args.paintId, need: 1, have: 0 });

    takeItems(currentPlayerId, inv, args.paintId, 1);
    structure.color = args.paintId;
    saveData(currentPlayerId, BASE_DATA_KEY, base.tents);
    return { painted: true, tentId: tent.tentId, structureId: structure.structureId, color: args.paintId };
};

/** 땅 하나 가구 한도 — 2층 집이면 60 (8.11 「2층 효과」). */
function furnitureMaxOf(tent) {
    return tent.floors === 2 ? FURNITURE_MAX_FLOOR2 : FURNITURE_MAX;
}

/** 재료 표 하나를 확인하고 모자라면 첫 모자란 줄로 거절, 다 있으면 빼고 null. */
function takeCost(cost) {
    var inv = readInventory(currentPlayerId);
    for (var i = 0; i < cost.length; i++) {
        var c = cost[i];
        if (countOf(inv, c.itemId) < c.count) {
            return fail("not_enough_items", "재료가 모자란다", { itemId: c.itemId, need: c.count, have: countOf(inv, c.itemId) });
        }
    }
    cost.forEach(function (c) { takeItems(currentPlayerId, inv, c.itemId, c.count); });
    return null;
}

handlers.UpgradeHouse = function (args, context) {
    args = args || {};
    var level = args.level === undefined ? 1 : args.level;
    if (level !== 1 && level !== 2) return fail("bad_args", "level 은 1·2");
    if (level === 2) return upgradeHouseFloor2(args);
    var color = args.color === undefined ? 0 : args.color;
    if (!isInt(color) || color < 0 || color >= HOUSE_COLOR_COUNT) return fail("bad_args", "color 는 0~" + (HOUSE_COLOR_COUNT - 1));
    var base = loadBase(currentPlayerId);
    var tent = findTent(base, args.tentId);
    if (tent.error) return tent;
    if (tent.upgraded) return fail("already", "이미 집이다");
    var lacking = takeCost(UPGRADE_COST);
    if (lacking) return lacking;
    var now = Date.now();
    tent.upgraded = true;
    tent.color = color;
    tent.upkeepUntilUtc = new Date(now + UPKEEP_START_HOURS * HOUR_MS).toISOString();
    saveData(currentPlayerId, BASE_DATA_KEY, base.tents);
    bumpAchievements({ house: 1 });
    return { upgraded: true, tentId: tent.tentId, color: color, upkeepUntilUtc: tent.upkeepUntilUtc };
};

/** 집 2층 (8.11 「집 2층·전력」) — 유지가 남은 집에만, 즉시. 유지 0 인 집은 loadBase 가 이미 지워 `expired`. */
function upgradeHouseFloor2(args) {
    var base = loadBase(currentPlayerId);
    var tent = findTent(base, args.tentId);
    if (tent.error) return tent;
    if (!tent.upgraded) return fail("not_house", "집이 된 뒤에 2층이 있다");
    if (tent.floors === 2) return fail("already", "이미 2층이다");
    var lacking = takeCost(FLOOR2_COST);
    if (lacking) return lacking;
    tent.floors = 2;
    saveData(currentPlayerId, BASE_DATA_KEY, base.tents);
    return { upgraded: true, tentId: tent.tentId, level: 2, color: tent.color, upkeepUntilUtc: tent.upkeepUntilUtc, furnitureMax: furnitureMaxOf(tent) };
}

/** 유지 탭 — 나무 1개 = 2시간, 200시간까지. 넘치는 만큼은 빼지 않는다. */
handlers.FeedUpkeep = function (args, context) {
    args = args || {};
    if (!isCount(args.count)) return fail("bad_args", "count 필요");
    var base = loadBase(currentPlayerId);
    var tent = findTent(base, args.tentId);
    if (tent.error) return tent;
    if (!tent.upgraded) return fail("not_house", "집이 된 뒤에 유지가 있다");
    var now = Date.now();
    var leftMs = Date.parse(tent.upkeepUntilUtc) - now;
    var roomWood = Math.floor((UPKEEP_MAX_HOURS * HOUR_MS - leftMs) / (UPKEEP_HOURS_PER_WOOD * HOUR_MS));
    if (roomWood <= 0) return fail("full", "유지가 가득이다", { upkeepUntilUtc: tent.upkeepUntilUtc });
    var inv = readInventory(currentPlayerId);
    var use = Math.min(args.count, roomWood);
    if (countOf(inv, UPKEEP_ITEM) < use) {
        return fail("not_enough_items", "나무가 모자란다", { need: use, have: countOf(inv, UPKEEP_ITEM) });
    }

    takeItems(currentPlayerId, inv, UPKEEP_ITEM, use);
    tent.upkeepUntilUtc = new Date(now + leftMs + use * UPKEEP_HOURS_PER_WOOD * HOUR_MS).toISOString();
    saveData(currentPlayerId, BASE_DATA_KEY, base.tents);
    return { used: use, upkeepUntilUtc: tent.upkeepUntilUtc };
};

/**
 * 발전기 연료 넣기 (M108-07) — 나무 1 = 10분 · 석탄 덩어리 1 = 30분, 12시간까지. 넘치는 만큼은 빼지 않는다(유지와 같음).
 * 응답 `nowUtc` = 서버 시계 — 클라는 끝 시각을 기기 시계가 아니라 이 차이로 옮긴다.
 */
handlers.FeedGenerator = function (args, context) {
    args = args || {};
    var minutes = GENERATOR_FUEL_MINUTES[args.fuel];
    if (!minutes || !isCount(args.count)) return fail("bad_args", "fuel(나무·석탄 덩어리)·count 필요");
    var base = loadBase(currentPlayerId);
    var tent = findTent(base, args.tentId);
    if (tent.error) return tent;
    var gen = null;
    tent.items.forEach(function (s) { if (s.structureId === args.structureId) gen = s; });
    if (!gen) return fail("not_found", "그 가구가 없다");
    if (gen.itemId !== GENERATOR_ITEM) return fail("not_generator", "발전기가 아니다");
    var now = Date.now();
    var leftMs = gen.fuelUntilUtc ? Math.max(0, Date.parse(gen.fuelUntilUtc) - now) : 0;
    var room = Math.floor((GENERATOR_FUEL_MAX_HOURS * HOUR_MS - leftMs) / (minutes * 60 * 1000));
    if (room <= 0) return fail("full", "연료가 가득이다", { fuelUntilUtc: gen.fuelUntilUtc, nowUtc: new Date(now).toISOString() });
    var inv = readInventory(currentPlayerId);
    var use = Math.min(args.count, room);
    if (countOf(inv, args.fuel) < use) {
        return fail("not_enough_items", "연료가 모자란다", { need: use, have: countOf(inv, args.fuel) });
    }

    takeItems(currentPlayerId, inv, args.fuel, use);
    gen.fuelUntilUtc = new Date(now + leftMs + use * minutes * 60 * 1000).toISOString();
    saveData(currentPlayerId, BASE_DATA_KEY, base.tents);
    return { used: use, fuelUntilUtc: gen.fuelUntilUtc, nowUtc: new Date(now).toISOString() };
};

// ---- M12-05 목장 유지비·명성 (DESIGN 3.2 「유지비·명성」 표) ----
// 유지는 위 집 유지와 같은 나무·같은 비율(`UPKEEP_*`)이라 새 상수가 없다. 목장 전체가 유지 하나다.
var RANCH_META_DATA_KEY = "ranchMeta";
var RANCH_FAME_PER_UNIT = 1;
var RANCH_SLOT_MAX = 10;
var RANCH_EXPAND_FAME_STEP = 50;   // k번째 확장 = 명성 50·k
var RANCH_EXPAND_COIN_STEP = 500;  // k번째 확장 = 코인 500·k

/** 유지 탭(목장) — 나무 1개 = 2시간, 200시간까지. 잠겨 있었으면 잠긴 시간만큼 칸 시각을 밀어 생산을 멈춘 셈으로 한다. */
handlers.FeedRanchUpkeep = function (args, context) {
    args = args || {};
    if (!isCount(args.count)) return fail("bad_args", "count 필요");
    var meta = loadRanchMeta();
    var now = Date.now();
    var leftMs = meta.upkeepUntilUtc ? Date.parse(meta.upkeepUntilUtc) - now : 0;
    var lockedMs = Math.max(0, -leftMs);
    leftMs = Math.max(0, leftMs);
    var roomWood = Math.floor((UPKEEP_MAX_HOURS * HOUR_MS - leftMs) / (UPKEEP_HOURS_PER_WOOD * HOUR_MS));
    if (roomWood <= 0) return fail("full", "목장 유지가 가득이다", { upkeepUntilUtc: meta.upkeepUntilUtc });
    var inv = readInventory(currentPlayerId);
    var use = Math.min(args.count, roomWood);
    if (countOf(inv, UPKEEP_ITEM) < use) {
        return fail("not_enough_items", "나무가 모자란다", { need: use, have: countOf(inv, UPKEEP_ITEM) });
    }

    takeItems(currentPlayerId, inv, UPKEEP_ITEM, use);
    if (lockedMs > 0) {
        var ranch = loadRanch();
        ranch.forEach(function (e) {
            if (e && e.occupiedSinceUtc) e.occupiedSinceUtc = new Date(Date.parse(e.occupiedSinceUtc) + lockedMs).toISOString();
        });
        saveRanch(ranch);
    }
    meta.upkeepUntilUtc = new Date(now + leftMs + use * UPKEEP_HOURS_PER_WOOD * HOUR_MS).toISOString();
    saveRanchMeta(meta);
    return { used: use, upkeepUntilUtc: meta.upkeepUntilUtc, unlocked: lockedMs > 0 };
};

/** 목장 칸 +1 — 명성·코인을 둘 다 쓴다. 상한 10칸. */
handlers.ExpandRanchSlot = function (args, context) {
    var meta = loadRanchMeta();
    if (meta.slotCap >= RANCH_SLOT_MAX) return fail("full", "목장 칸이 이미 최대다", { slotCap: meta.slotCap });
    var cost = ranchExpandCost(meta.slotCap);
    if (meta.fame < cost.fame) return fail("not_enough_fame", "명성이 모자란다", { need: cost.fame, have: meta.fame });
    var inv = readInventory(currentPlayerId);
    if (coinsOf(inv) < cost.coins) return fail("not_enough_coins", "코인이 모자란다", { need: cost.coins, have: coinsOf(inv) });

    spendCoins(cost.coins);
    meta.fame -= cost.fame;
    meta.slotCap += 1;
    saveRanchMeta(meta);
    return { slotCap: meta.slotCap, fame: meta.fame, cost: cost };
};

function loadRanchMeta() {
    return loadData(currentPlayerId, RANCH_META_DATA_KEY, { upkeepUntilUtc: null, fame: 0, slotCap: RANCH_SLOT_COUNT });
}

function saveRanchMeta(meta) { saveData(currentPlayerId, RANCH_META_DATA_KEY, meta); }

/** 유지가 시작됐고 기한이 지났으면 잠김. 시작 전(펫을 넣은 적 없음)은 잠기지 않는다. */
function ranchLocked(meta, now) { return !!meta.upkeepUntilUtc && Date.parse(meta.upkeepUntilUtc) <= now; }

function ranchLockedFail(meta) {
    return fail("locked", "목장 유지가 끊겼다 — 나무를 넣어야 풀린다", { upkeepUntilUtc: meta.upkeepUntilUtc });
}

/** 처음이면 유지 하루치를 얹는다. 바꿨으면 true. */
function startRanchUpkeep(meta, now) {
    if (meta.upkeepUntilUtc) return false;
    meta.upkeepUntilUtc = new Date(now + UPKEEP_START_HOURS * HOUR_MS).toISOString();
    return true;
}

function ranchExpandCost(slotCap) {
    var k = slotCap - RANCH_SLOT_COUNT + 1;
    return { fame: RANCH_EXPAND_FAME_STEP * k, coins: RANCH_EXPAND_COIN_STEP * k };
}

/** GetBaseState 가 싣는 모양 — 클라이언트는 이 값을 그대로 적기만 한다(남은 시간도 서버가 센다). */
function ranchUpkeepView(meta, now) {
    var until = meta.upkeepUntilUtc ? Date.parse(meta.upkeepUntilUtc) : null;
    return {
        upkeepUntilUtc: meta.upkeepUntilUtc,
        leftHours: until === null ? null : Math.max(0, Math.floor((until - now) / HOUR_MS)),
        locked: ranchLocked(meta, now),
        fame: meta.fame,
        slotCap: meta.slotCap
    };
}

// ---- 보관함 ----

handlers.MoveToStorage = function (args, context) {
    var box = openStorage(args);
    if (box.error) return box;
    var count = args.count === undefined ? 1 : args.count;
    if (!args.itemId || !isCount(count)) return fail("bad_args", "itemId, count 필요");
    if (!isBackpackItem(args.itemId)) return fail("protected", "펫·가챠 아이템은 넣을 수 없다");
    // 열쇠를 자물쇠 안에 넣으면 다시는 못 연다(`openBoatStorage` 가 배낭의 컨테이너를 본다).
    if (args.boat === true && args.itemId === BOAT_CONTAINER_ITEM) return fail("self", "배 짐칸는 배 보관함에 넣을 수 없다");
    var inv = readInventory(currentPlayerId);
    if (countOf(inv, args.itemId) < count) {
        return fail("not_enough_items", "가진 것보다 많다", { need: count, have: countOf(inv, args.itemId) });
    }
    var after = copyCounts(box.structure.storage);
    after[args.itemId] = (after[args.itemId] || 0) + count;
    if (slotsFor(after) > box.slots) return fail("storage_full", "보관함이 가득 찼다");

    takeItems(currentPlayerId, inv, args.itemId, count);
    box.structure.storage = after;
    box.save();
    return { stored: args.itemId, count: count, storage: after };
};

handlers.MoveFromStorage = function (args, context) {
    var box = openStorage(args);
    if (box.error) return box;
    var count = args.count === undefined ? 1 : args.count;
    if (!args.itemId || !isCount(count)) return fail("bad_args", "itemId, count 필요");
    var storage = box.structure.storage;
    if ((storage[args.itemId] || 0) < count) {
        return fail("not_enough_items", "보관함에 그만큼 없다", { need: count, have: storage[args.itemId] || 0 });
    }
    var deltas = {};
    deltas[args.itemId] = count;
    var full = roomCheck(readInventory(currentPlayerId), deltas);
    if (full) return full;

    var refused = tryGiveItems(currentPlayerId, [{ itemId: args.itemId, count: count }]);
    if (refused) return refused;
    storage[args.itemId] -= count;
    if (storage[args.itemId] === 0) delete storage[args.itemId];
    box.save();
    return { taken: args.itemId, count: count, storage: storage };
};

// ---- 직접 낚시 ----

/** 입질 순간 어종을 정하고 일회용 표를 준다. 미끼는 여기서 1개 뺀다. */
handlers.RollFish = function (args, context) {
    args = args || {};
    var habitat = FISHING_SPOTS[args.spotId];
    if (!habitat) return fail("bad_args", "알 수 없는 spotId: " + args.spotId);
    var baitItem = null;
    if (args.bait !== undefined && args.bait !== null) {
        baitItem = FISHING_BAITS[args.bait];
        if (!baitItem) return fail("bad_args", "bait 는 worm 또는 berry");
    }
    var now = Date.now();
    var state = loadData(currentPlayerId, FISH_ROLL_DATA_KEY, {});
    if (state.lastRollUtc && now - Date.parse(state.lastRollUtc) < FISH_CALL_GAP_SECONDS * 1000) {
        return fail("too_fast", FISH_CALL_GAP_SECONDS + "초 안에 다시 부를 수 없다");
    }
    if (habitat === "farSea" && !codexProgress(loadData(currentPlayerId, "codex", [])).rewards.farSea) {
        return fail("locked", "낚시 도감 " + CODEX_UNLOCK_PERCENT + "% 가 먼저다");
    }
    var inv = readInventory(currentPlayerId);
    var hasRod = FISHING_RODS.some(function (id) { return countOf(inv, id) > 0; });
    if (!hasRod) return fail("no_rod", "낚싯대가 없다");
    if (baitItem && countOf(inv, baitItem) < 1) return fail("not_enough_items", "미끼가 없다", { itemId: baitItem });

    var fishItemId = pickWeighted(fishWeights(args.spotId, args.bait));
    if (baitItem) takeItems(currentPlayerId, inv, baitItem, 1);
    var token = newId();
    saveData(currentPlayerId, FISH_ROLL_DATA_KEY, {
        token: token, fishItemId: fishItemId, spotId: args.spotId, used: false,
        lastRollUtc: new Date(now).toISOString(), lastCatchUtc: state.lastCatchUtc || null
    });
    return { token: token, fishItemId: fishItemId };
};

/** 연타 성공 뒤 — 표 한 장에 1마리. 칸이 모자라면 표를 그대로 둔다(🟡 비우고 다시 부르면 받는다). */
handlers.CatchFish = function (args, context) {
    args = args || {};
    var now = Date.now();
    var state = loadData(currentPlayerId, FISH_ROLL_DATA_KEY, {});
    if (!args.token || state.token !== args.token || state.used) return fail("token_invalid", "쓸 수 없는 표");
    if (state.lastCatchUtc && now - Date.parse(state.lastCatchUtc) < FISH_CALL_GAP_SECONDS * 1000) {
        return fail("too_fast", FISH_CALL_GAP_SECONDS + "초 안에 다시 잡을 수 없다");
    }
    var deltas = {};
    deltas[state.fishItemId] = 1;
    var inv = readInventory(currentPlayerId);
    var full = roomCheck(inv, deltas);
    if (full) return full;
    var crate = rollCrateDrop(inv, deltas); // 10.8 — 잡을 때 한 번

    state.used = true;
    state.lastCatchUtc = new Date(now).toISOString();
    saveData(currentPlayerId, FISH_ROLL_DATA_KEY, state);
    giveItems(currentPlayerId, [{ itemId: state.fishItemId, count: 1 }].concat(crate ? [{ itemId: crate, count: 1 }] : []));
    addToCodex(state.fishItemId);
    bumpAchievements({ fish: 1 });
    if (!crate) return { caught: state.fishItemId };
    addToCodex(crate);
    return { caught: state.fishItemId, crate: crate };
};

// ---- 상점·선착장 ----

// ---- 옷장 꾸미기 상점 (DESIGN 10.7 · 10.7b) ----
// 젬으로 사는 겉모습 전용 장비. 매주 월요일(UTC) 풀에서 WARDROBE_SHOP_SLOTS 개 — 모든 플레이어가 같은 진열(주 번호 씨앗).
// 능력치 있는 것(HAT_EFFECTS 값 있는 모자·등산 짐가방 등)은 넣지 않는다.
var WARDROBE_SHOP_SLOTS = 8;
// 젬 값 = 칸 기본(모자·상의·배낭 300 · 나머지 200) + 등급 웃돈(희귀 +100 · 에픽 +200) — 10.7b 「꾸미기 젬 값」.
var OUTFIT_SLOT_GEMS = { head: 300, top: 300, back: 300, face: 200, bottom: 200, shoes: 200, gloves: 200 };
var COSMETIC_GRADE_GEMS = { common: 0, rare: 100, epic: 200 };
var RANCH_COSMETIC_GEMS = { "item.ranch_ribbon": 200, "item.ranch_necklace": 200, "item.ranch_hat": 300 };
var WARDROBE_SHOP_POOL = {};
COSMETIC_SHOP_ITEMS.forEach(function (id) {
    WARDROBE_SHOP_POOL[id] = RANCH_COSMETIC_GEMS[id] || OUTFIT_SLOT_GEMS[OUTFIT_ITEMS[id]] + COSMETIC_GRADE_GEMS[cosmeticGrade(id)];
});

/** 이번 주 진열 — 같은 주엔 누가 몇 번 물어도 같은 목록(todaysQuests 와 같은 씨앗 섞기). */
function wardrobeShopOf(week) {
    var seed = 0;
    var key = "wardrobe:" + week;
    for (var i = 0; i < key.length; i++) seed = (seed * 31 + key.charCodeAt(i)) | 0;
    var pool = Object.keys(WARDROBE_SHOP_POOL).sort();
    var out = [];
    while (out.length < WARDROBE_SHOP_SLOTS && pool.length > 0) {
        seed = (seed * 1103515245 + 12345) | 0;
        var pick = pool.splice(((seed >>> 8) % pool.length), 1)[0];
        out.push({ itemId: pick, gems: WARDROBE_SHOP_POOL[pick] });
    }
    return out;
}

handlers.GetWardrobeShop = function (args, context) {
    var week = bossWeekOf(questDay());
    return { week: week, items: wardrobeShopOf(week) };
};

/** BuyItem 의 젬(GM) 갈래 — 이번 주 진열에 있는 것만, 한 개씩, 이미 가진 것은 막는다. */
function buyWardrobeItem(args, count, inv) {
    if (count !== 1) return fail("bad_args", "꾸미기는 한 개씩");
    var offer = wardrobeShopOf(bossWeekOf(questDay())).filter(function (o) { return o.itemId === args.itemId; })[0];
    if (!offer) return fail("bad_args", "이번 주 옷장 상점에 없는 아이템: " + args.itemId);
    // 10.9 동물 꾸미기는 동물마다 하나씩 씌우므로 여러 개 가져도 된다 — 옷·모자만 하나로 막는다.
    if (RANCH_COSMETICS.indexOf(args.itemId) === -1 && countOf(inv, args.itemId) > 0) return fail("already", "이미 가진 꾸미기");
    var gems = inv.currency.GM || 0;
    if (gems < offer.gems) return fail("not_enough_gems", "젬이 모자란다", { need: offer.gems, have: gems });
    var deltas = {};
    deltas[args.itemId] = 1;
    var full = roomCheck(inv, deltas);
    if (full) return full;

    server.SubtractUserVirtualCurrency({ PlayFabId: currentPlayerId, VirtualCurrency: "GM", Amount: offer.gems });
    giveItems(currentPlayerId, [{ itemId: args.itemId, count: 1 }]);
    return { bought: args.itemId, count: 1, cost: offer.gems, currency: "GM" };
}

handlers.BuyItem = function (args, context) {
    args = args || {};
    var count = args.count === undefined ? 1 : args.count;
    if (!isCount(count)) return fail("bad_args", "count 는 1 이상 정수");
    var inv = readInventory(currentPlayerId);
    // 10.8 열쇠의 젬 구매는 아래 잡화점 갈래가 받는다 — 나머지 젬 구매는 옷장 상점(10.7).
    if (args.currency === "GM" && args.itemId !== CRATE_KEY) return buyWardrobeItem(args, count, inv);

    if (BLUEPRINT_IDS.indexOf(args.itemId) !== -1) {
        if (!codexProgress(loadData(currentPlayerId, "codex", [])).rewards.specialMerchant) {
            return fail("locked", "도감 한 카테고리 " + CODEX_MERCHANT_PERCENT + "% 가 먼저다");
        }
        var owned = loadData(currentPlayerId, BLUEPRINTS_DATA_KEY, []);
        if (owned.indexOf(args.itemId) !== -1) return fail("already", "이미 배운 설계도");
        if (coinsOf(inv) < BLUEPRINT_PRICE) return fail("not_enough_coins", "코인이 모자란다", { need: BLUEPRINT_PRICE, have: coinsOf(inv) });
        spendCoins(BLUEPRINT_PRICE);
        owned.push(args.itemId);
        saveData(currentPlayerId, BLUEPRINTS_DATA_KEY, owned);
        addToCodex(args.itemId);
        bumpAchievements({});
        return { bought: args.itemId, count: 1, cost: BLUEPRINT_PRICE };
    }

    var entry = SHOP_ITEMS[args.itemId];
    if (!entry) return fail("bad_args", "상점에 없는 아이템: " + args.itemId);
    // 10.8 열쇠만 젬(GM)으로도 산다 — 나머지 물건은 코인만.
    var currency = args.currency === "GM" ? "GM" : "SC";
    if (currency === "GM" && args.itemId !== CRATE_KEY) return fail("bad_args", "젬으로는 열쇠만 산다");
    var cost = (currency === "GM" ? CRATE_KEY_GEM_PRICE : entry.price) * count;
    var have = inv.currency[currency] || 0;
    if (have < cost) return fail(currency === "GM" ? "not_enough_gems" : "not_enough_coins", currency === "GM" ? "젬이 모자란다" : "코인이 모자란다", { need: cost, have: have });
    var deltas = {};
    deltas[args.itemId] = entry.count * count;
    var full = roomCheck(inv, deltas);
    if (full) return full;

    server.SubtractUserVirtualCurrency({ PlayFabId: currentPlayerId, VirtualCurrency: currency, Amount: cost });
    giveItems(currentPlayerId, [{ itemId: args.itemId, count: entry.count * count }]);
    return { bought: args.itemId, count: entry.count * count, cost: cost, currency: currency };
};

handlers.SellItem = function (args, context) {
    args = args || {};
    var count = args.count === undefined ? 1 : args.count;
    if (!args.itemId || !isCount(count)) return fail("bad_args", "itemId, count 필요");
    var price = FRUITS.indexOf(args.itemId) !== -1 ? fruitSellPrice(currentPlayerId, args.itemId) : sellPriceOf(args.itemId); // 8.18 과일은 고유/남의 것 값이 다르다
    if (price <= 0) return fail("not_sellable", "팔 수 없는 아이템: " + args.itemId);
    var inv = readInventory(currentPlayerId);
    if (countOf(inv, args.itemId) < count) {
        return fail("not_enough_items", "가진 것보다 많다", { need: count, have: countOf(inv, args.itemId) });
    }

    var coins = Math.floor(price * count * (wornHatEffect(inv).sellMul || 1)); // 13.5 장사치 중절 — 총액을 내림
    var loot = isLootDerived(args.itemId) ? lootState() : null; // DESIGN 4.8 재료 판매 하루 코인 상한
    if (loot && loot.coins + coins > LOOT_SELL_DAILY_COINS) {
        return fail("daily_limit", "오늘 재료를 팔 수 있는 코인을 다 채웠다", { limit: LOOT_SELL_DAILY_COINS, left: LOOT_SELL_DAILY_COINS - loot.coins });
    }
    takeItems(currentPlayerId, inv, args.itemId, count);
    server.AddUserVirtualCurrency({ PlayFabId: currentPlayerId, VirtualCurrency: "SC", Amount: coins });
    if (loot) {
        loot.coins += coins;
        saveData(currentPlayerId, LOOT_DATA_KEY, loot);
    }
    return { sold: args.itemId, count: count, coins: coins };
};

// ---- 고구마 시세 (DESIGN 8.17 — 동숲 무 주식) ----
// 일요일에 장수에게 10개 묶음으로 사서, 월~토 12칸 시세 중 높은 칸에 잡화상에 판다. 주가 바뀌면 썩는다.
// 시세는 저장하지 않는다 — `hash(PlayFabId, 주 번호)` 로 매번 같은 값을 계산한다(옷장 진열과 같은 방식).
var SWEET_POTATO = "item.sweet_potato";
var SWEET_POTATO_ROTTEN = "item.sweet_potato_rotten";
var SWEET_POTATO_DATA_KEY = "sweetPotato";
var POTATO_BUNDLE = 10;          // 묶음 = 10개
var POTATO_WEEKLY_CAP = 100;     // 한 주에 100개(10묶음)
var POTATO_SLOTS = 12;           // 월~토 하루 2칸
var POTATO_PATTERN_ODDS = [["wave", 0.35], ["smallSpike", 0.65], ["bigSpike", 0.85], ["decline", 1]]; // 누적 확률 35/30/20/15

/** 주 번호 — 일요일 시작(UTC). 1970-01-01 은 목요일이라 +4. */
function potatoWeekOf(day) { return Math.floor((day + 4) / 7); }
/** 요일 0 = 일요일 … 6 = 토요일. */
function potatoWeekdayOf(day) { return (day + 4) % 7; }
/** 지금 날짜와 오전(0)/오후(1) — 시험이 이 함수를 바꿔 시간을 정한다. */
function potatoClock() { return { day: questDay(), half: Date.now() % 86400000 >= 43200000 ? 1 : 0 }; }

function potatoRng(key) {
    var h = 2166136261;
    for (var i = 0; i < key.length; i++) { h ^= key.charCodeAt(i); h = Math.imul(h, 16777619); }
    var a = h >>> 0;
    return function () { // mulberry32
        a = (a + 0x6D2B79F5) | 0;
        var t = Math.imul(a ^ (a >>> 15), 1 | a);
        t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
        return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
    };
}

/** 이 주의 시세 한 벌 — `{base, pattern, rates[12], prices[12]}`. 무늬(pattern)는 클라이언트에 안 보낸다(8.17 무늬 공개 안 함). */
function sweetPotatoWeek(playFabId, week) {
    var rng = potatoRng("potato:" + playFabId + ":" + week);
    function between(lo, hi) { return lo + rng() * (hi - lo); }
    function whole(lo, hi) { return lo + Math.floor(rng() * (hi - lo + 1)); }
    var base = whole(9, 11);
    var roll = rng();
    var pattern = POTATO_PATTERN_ODDS.filter(function (o) { return roll < o[1]; })[0][0];
    var rates = [];
    var i;
    function fall(from, to) { // from 칸부터 to 칸 직전까지 첫 칸 0.85~0.9 에서 칸마다 0.03~0.05 씩 내림(바닥 0.4)
        var r = between(0.85, 0.9);
        for (var k = from; k < to; k++) { rates[k] = Math.max(0.4, r); r -= between(0.03, 0.05); }
    }
    if (pattern === "wave") {
        for (i = 0; i < POTATO_SLOTS; i++) rates[i] = between(0.9, 1.4);
    } else if (pattern === "decline") {
        fall(0, POTATO_SLOTS);
    } else if (pattern === "smallSpike") {
        var s = whole(2, 9);
        for (i = 0; i < POTATO_SLOTS; i++) rates[i] = i >= s && i < s + 3 ? between(1.4, 2.0) : between(0.4, 0.9);
    } else {
        var peak = whole(2, 8);
        fall(0, peak);
        var shape = [[0.9, 1.4], [1.4, 2.0], [2.0, 4.0], [1.4, 2.0], [0.9, 1.4]];
        for (i = peak; i < POTATO_SLOTS; i++) {
            var step = shape[i - peak];
            rates[i] = step ? between(step[0], step[1]) : between(0.4, 0.9);
        }
    }
    return {
        base: base,
        pattern: pattern,
        rates: rates,
        prices: rates.map(function (r) { return Math.max(1, Math.round(base * r)); })
    };
}

/** 주가 바뀌었으면 가진 고구마를 전부 썩은 고구마로 바꾸고 산 기록을 비운다. 새 주의 기록을 돌려준다. */
function rotSweetPotatoes(week, state) {
    if (state && state.week === week) return state;
    var inv = readInventory(currentPlayerId);
    var n = countOf(inv, SWEET_POTATO);
    if (n > 0) {
        giveItems(currentPlayerId, [{ itemId: SWEET_POTATO_ROTTEN, count: n }]); // 먼저 주고 뺀다 — 실패해도 고구마는 안 사라진다
        takeItems(currentPlayerId, inv, SWEET_POTATO, n);
    }
    var fresh = { week: week, bought: 0, paid: 0 };
    if (state || n > 0) saveData(currentPlayerId, SWEET_POTATO_DATA_KEY, fresh);
    return fresh;
}

handlers.GetSweetPotato = function (args, context) {
    var clock = potatoClock();
    var week = potatoWeekOf(clock.day);
    var weekday = potatoWeekdayOf(clock.day);
    var state = rotSweetPotatoes(week, loadData(currentPlayerId, SWEET_POTATO_DATA_KEY, null));
    var table = sweetPotatoWeek(currentPlayerId, week);
    var slot = weekday === 0 ? -1 : (weekday - 1) * 2 + clock.half;
    var out = { weekday: weekday, boughtThisWeek: state.bought, cap: POTATO_WEEKLY_CAP, slot: slot, price: 0, prevPrice: 0, paid: state.paid };
    if (weekday === 0) out.buyPrice = table.base;
    else {
        out.price = table.prices[slot];
        out.prevPrice = slot > 0 ? table.prices[slot - 1] : 0;
    }
    return out;
};

handlers.BuySweetPotato = function (args, context) {
    args = args || {};
    var bundles = args.bundles;
    if (!isInt(bundles) || bundles < 1 || bundles > POTATO_WEEKLY_CAP / POTATO_BUNDLE) return fail("bad_args", "bundles 는 1~10 정수");
    var clock = potatoClock();
    var week = potatoWeekOf(clock.day);
    if (potatoWeekdayOf(clock.day) !== 0) return fail("closed", "고구마 장수는 일요일에만 온다");
    var state = rotSweetPotatoes(week, loadData(currentPlayerId, SWEET_POTATO_DATA_KEY, null));
    var count = bundles * POTATO_BUNDLE;
    if (state.bought + count > POTATO_WEEKLY_CAP) return fail("cap_reached", "이번 주 상한을 넘는다", { bought: state.bought, cap: POTATO_WEEKLY_CAP });
    var cost = count * sweetPotatoWeek(currentPlayerId, week).base;
    var inv = readInventory(currentPlayerId);
    if (coinsOf(inv) < cost) return fail("not_enough_coins", "코인이 모자란다", { need: cost, have: coinsOf(inv) });
    var deltas = {};
    deltas[SWEET_POTATO] = count;
    var full = roomCheck(inv, deltas);
    if (full) return full;

    spendCoins(cost);
    giveItems(currentPlayerId, [{ itemId: SWEET_POTATO, count: count }]);
    state.bought += count;
    state.paid += cost;
    saveData(currentPlayerId, SWEET_POTATO_DATA_KEY, state);
    return { bought: SWEET_POTATO, count: count, cost: cost, boughtThisWeek: state.bought, cap: POTATO_WEEKLY_CAP };
};

handlers.SellSweetPotato = function (args, context) {
    args = args || {};
    var count = args.count;
    if (!isCount(count)) return fail("bad_args", "count 는 1 이상 정수");
    var clock = potatoClock();
    var week = potatoWeekOf(clock.day);
    var weekday = potatoWeekdayOf(clock.day);
    rotSweetPotatoes(week, loadData(currentPlayerId, SWEET_POTATO_DATA_KEY, null)); // 지난 주 것은 여기서 썩는다
    if (weekday === 0) return fail("closed", "일요일엔 잡화상이 고구마를 안 산다");
    var inv = readInventory(currentPlayerId);
    if (countOf(inv, SWEET_POTATO) < count) return fail("not_enough_items", "가진 것보다 많다", { need: count, have: countOf(inv, SWEET_POTATO) });
    var price = sweetPotatoWeek(currentPlayerId, week).prices[(weekday - 1) * 2 + clock.half];
    var coins = price * count;
    takeItems(currentPlayerId, inv, SWEET_POTATO, count);
    server.AddUserVirtualCurrency({ PlayFabId: currentPlayerId, VirtualCurrency: "SC", Amount: coins });
    return { sold: SWEET_POTATO, count: count, price: price, coins: coins };
};

// ---- 과일나무 (DESIGN 8.18 — 동숲 과일나무) ----
// 플레이어마다 고유 과일 1종(`hash(PlayFabId)`, 저장 안 함). 남의 과일은 5배 값. 친구(방장) 나무를 흔들어 얻어 와 내 목장에 심는다.
// 내부 데이터 `fruitTrees {slots:[null|{fruit, grownAt, pickedAt}], picks:{day, hosts[]}}` — 시각은 ms.
var FRUITS = ["item.pear", "item.peach", "item.cherry", "item.orange", "item.plum"];
var FRUIT_DATA_KEY = "fruitTrees";
var FRUIT_SLOTS = 4;                          // 목장 나무 자리
var FRUIT_GROW_MS = 72 * 3600000;             // 묘목 → 다 자란 나무 72시간
var FRUIT_REGROW_MS = 24 * 3600000;           // 따면 24시간 뒤 다시 열림
var FRUIT_PER_TREE = 3;                       // 나무 한 그루 열매 3개(안 따면 쌓이지 않음)
var FRUIT_PICK_COUNT = 3;                     // 친구 나무 흔들기 = 방장 고유 과일 3개
var FRUIT_PICK_HOSTS_PER_DAY = 3;             // 🟡 하루에 서로 다른 방장 3명까지(하루 9개×40=360코인, 목장 하루 수입 ≈480 보다 작게) — 가짜 hostId 돌려막기 막이
var FRUIT_CHOP_WOOD = 2;
var FRUIT_PRICE_NATIVE = 8;
var FRUIT_PRICE_OTHER = 40;                   // 고유 과일의 5배

/** 지금 시각(ms)과 날짜 번호 — 시험이 이 함수를 바꿔 시간을 정한다. */
function fruitClock() { var now = Date.now(); return { now: now, day: Math.floor(now / 86400000) }; }
function nativeFruitOf(playFabId) { return FRUITS[Math.floor(potatoRng("fruit:" + playFabId)() * FRUITS.length)]; }
/** 이 플레이어가 파는 과일 값 — 고유 8, 남의 것 40. */
function fruitSellPrice(playFabId, itemId) { return nativeFruitOf(playFabId) === itemId ? FRUIT_PRICE_NATIVE : FRUIT_PRICE_OTHER; }

/** 나무 상태 — 열매 수와 다음 열리는 때. 묘목 `readyAt` = 다 자라는 때, 열매 0 인 나무 `readyAt` = 다시 열리는 때. */
function fruitTreeView(tree, slot, now) {
    var grown = now >= tree.grownAt;
    var fruits = 0;
    var readyAt = tree.grownAt;
    if (grown) {
        if (!tree.pickedAt || now >= tree.pickedAt + FRUIT_REGROW_MS) { fruits = FRUIT_PER_TREE; readyAt = 0; }
        else readyAt = tree.pickedAt + FRUIT_REGROW_MS;
    }
    return { slot: slot, fruit: tree.fruit, stage: grown ? "grown" : "sapling", readyAt: readyAt, fruits: fruits };
}

/** 저장된 나무 기록 — 처음이면 자리 1·2번에 고유 과일 다 자란 나무 2그루(동숲의 처음부터 있는 섬 과일나무). 기본값은 늘 같아서 바뀐 것만 저장한다(조회·실패 호출은 쓰지 않는다). */
function loadFruitTrees() {
    var state = loadData(currentPlayerId, FRUIT_DATA_KEY, null);
    if (state) return state;
    var native = nativeFruitOf(currentPlayerId);
    state = { slots: [{ fruit: native, grownAt: 0, pickedAt: 0 }, { fruit: native, grownAt: 0, pickedAt: 0 }, null, null], picks: { day: 0, hosts: [] } };
    return state;
}

function fruitTreeSlotArg(args) {
    return args && isInt(args.slot) && args.slot >= 0 && args.slot < FRUIT_SLOTS ? args.slot : -1;
}

handlers.GetFruitTrees = function (args, context) {
    var clock = fruitClock();
    var state = loadFruitTrees();
    var trees = [];
    state.slots.forEach(function (tree, slot) { if (tree) trees.push(fruitTreeView(tree, slot, clock.now)); });
    return { native: nativeFruitOf(currentPlayerId), trees: trees, pickedToday: state.picks.day === clock.day ? state.picks.hosts : [], now: clock.now };
};

handlers.PlantFruit = function (args, context) {
    args = args || {};
    var slot = fruitTreeSlotArg(args);
    if (slot < 0 || FRUITS.indexOf(args.itemId) === -1) return fail("bad_args", "slot 0~3, itemId 는 과일 5종 중 하나");
    var clock = fruitClock();
    var state = loadFruitTrees();
    if (state.slots[slot]) return fail("slot_full", "나무 자리가 이미 차 있다");
    var inv = readInventory(currentPlayerId);
    if (countOf(inv, args.itemId) < 1) return fail("not_enough_items", "과일이 없다", { itemId: args.itemId, need: 1, have: 0 });
    takeItems(currentPlayerId, inv, args.itemId, 1);
    state.slots[slot] = { fruit: args.itemId, grownAt: clock.now + FRUIT_GROW_MS, pickedAt: 0 };
    saveData(currentPlayerId, FRUIT_DATA_KEY, state);
    return { planted: args.itemId, slot: slot, readyAt: state.slots[slot].grownAt };
};

handlers.HarvestTree = function (args, context) {
    var slot = fruitTreeSlotArg(args);
    if (slot < 0) return fail("bad_args", "slot 0~3 필요");
    var clock = fruitClock();
    var state = loadFruitTrees();
    var tree = state.slots[slot];
    if (!tree) return fail("empty_slot", "나무 자리가 비어 있다");
    var view = fruitTreeView(tree, slot, clock.now);
    if (view.fruits < 1) return fail("not_ready", "딸 열매가 없다", { readyAt: view.readyAt });
    var deltas = {};
    deltas[tree.fruit] = view.fruits;
    var full = roomCheck(readInventory(currentPlayerId), deltas);
    if (full) return full;
    giveItems(currentPlayerId, [{ itemId: tree.fruit, count: view.fruits }]); // 먼저 주고 기록한다 — 실패해도 열매는 안 사라진다
    tree.pickedAt = clock.now;
    saveData(currentPlayerId, FRUIT_DATA_KEY, state);
    return { harvested: view.fruits, itemId: tree.fruit, slot: slot, readyAt: clock.now + FRUIT_REGROW_MS };
};

handlers.ChopTree = function (args, context) {
    var slot = fruitTreeSlotArg(args);
    if (slot < 0) return fail("bad_args", "slot 0~3 필요");
    var clock = fruitClock();
    var state = loadFruitTrees();
    var tree = state.slots[slot];
    if (!tree) return fail("empty_slot", "나무 자리가 비어 있다");
    if (clock.now < tree.grownAt) return fail("not_ready", "묘목은 벨 수 없다", { readyAt: tree.grownAt });
    var deltas = {};
    deltas["item.wood"] = FRUIT_CHOP_WOOD;
    var full = roomCheck(readInventory(currentPlayerId), deltas);
    if (full) return full;
    giveItems(currentPlayerId, [{ itemId: "item.wood", count: FRUIT_CHOP_WOOD }]);
    state.slots[slot] = null;
    saveData(currentPlayerId, FRUIT_DATA_KEY, state);
    return { chopped: slot, wood: FRUIT_CHOP_WOOD };
};

/** 실제 PlayFab 계정인지 — 조작 클라이언트가 지어낸 hostId 로 하루 한도를 돌려막지 못하게. 읽기만 한다. */
function playerExists(playFabId) {
    try {
        server.GetPlayerProfile({ PlayFabId: playFabId });
        return true;
    } catch (e) {
        return false;
    }
}

/**
 * 지금 그 방장의 방에 있나 — `ReportCoop` 기록(60초마다)으로 본다(출시 점검 10-16). 내 기록과 방장 자신의 기록이
 * 둘 다 같은 hostId 이고 COOP_FRESH_SECONDS 안이어야 한다 — 내 기록만 보면 지어낸 hostId 로 ReportCoop 을 먼저 부르면 뚫린다.
 */
function sameRoomNow(hostId) {
    var now = Date.now();
    function fresh(rec) { return rec && rec.hostId === hostId && now - Date.parse(rec.atUtc) <= COOP_FRESH_SECONDS * 1000; }
    return fresh(loadData(currentPlayerId, COOP_DATA_KEY, null)) && fresh(loadData(hostId, COOP_DATA_KEY, null));
}

/** 방장 나무 흔들기 — 방장의 고유 과일 3개. 방장 데이터는 같은 방 기록(coop)만 읽고 쓰지 않는다(종류는 hostId 해시). 방장 한 명당 하루 1번. */
handlers.PickFriendFruit = function (args, context) {
    args = args || {};
    var hostId = args.hostId;
    if (typeof hostId !== "string" || hostId.length < 1 || hostId.length > 64) return fail("bad_args", "hostId 필요");
    if (hostId === currentPlayerId) return fail("same_player", "내 나무는 흔들어 얻을 수 없다");
    if (!playerExists(hostId)) return fail("bad_args", "없는 방장이다");
    if (!sameRoomNow(hostId)) return fail("not_in_room", "지금 그 방장의 방에 있지 않다");
    var clock = fruitClock();
    var state = loadFruitTrees();
    if (state.picks.day !== clock.day) state.picks = { day: clock.day, hosts: [] };
    if (state.picks.hosts.indexOf(hostId) !== -1) return fail("already_picked", "오늘은 이 사람 나무에서 이미 얻었다");
    if (state.picks.hosts.length >= FRUIT_PICK_HOSTS_PER_DAY) return fail("already_picked", "오늘 얻을 수 있는 사람 수를 다 채웠다", { limit: FRUIT_PICK_HOSTS_PER_DAY });
    var fruit = nativeFruitOf(hostId);
    var deltas = {};
    deltas[fruit] = FRUIT_PICK_COUNT;
    var full = roomCheck(readInventory(currentPlayerId), deltas);
    if (full) return full;
    giveItems(currentPlayerId, [{ itemId: fruit, count: FRUIT_PICK_COUNT }]);
    state.picks.hosts.push(hostId);
    saveData(currentPlayerId, FRUIT_DATA_KEY, state);
    return { picked: fruit, count: FRUIT_PICK_COUNT, hostId: hostId };
};

// ---- 의뢰인 (DESIGN 8.14 「의뢰」 🟡 감독 2026-09-25) ----
// 날마다(UTC) 플레이어마다 풀에서 QUEST_PER_DAY 개. 건네면 아이템을 빼고 코인을 준다 — 하루 한 번씩.
var QUEST_PER_DAY = 3;
var QUEST_REWARD_MUL = 1.5;        // 파는 값 × 개수 × 1.5 (올림) — 상점에 파는 것보다 이득이어야 건넨다
var QUEST_UNSELLABLE_COIN = 0.5;   // 못 파는 것(나무·산딸기) 한 개당 — 8.14 "나무·산딸기 못 판다" 를 뚫지 않게 작게
var QUEST_DATA_KEY = "quests";
var QUEST_POOL = [
    ["item.wood", 10], ["item.berry", 5], ["item.crucian_carp", 2], ["item.carp", 1], ["item.pike", 1],
    ["item.carrot", 3], ["item.potato", 2], ["item.wheat", 2], ["item.fox_tail", 1], ["item.goat_horn", 1],
    ["item.egg", 1], ["item.reindeer_meat", 3]
];

function questDay() { return Math.floor(Date.now() / 86400000); }

function questReward(itemId, count) {
    var price = sellPriceOf(itemId);
    return Math.ceil(price > 0 ? price * count * QUEST_REWARD_MUL : count * QUEST_UNSELLABLE_COIN);
}

/** 오늘 이 플레이어의 의뢰 — 같은 날엔 몇 번 물어도 같은 셋(플레이어 ID + 날짜 씨앗). */
function todaysQuests(playFabId, day) {
    var seed = 0;
    var key = playFabId + ":" + day;
    for (var i = 0; i < key.length; i++) seed = (seed * 31 + key.charCodeAt(i)) | 0;
    var pool = QUEST_POOL.slice();
    var out = [];
    while (out.length < QUEST_PER_DAY && pool.length > 0) {
        seed = (seed * 1103515245 + 12345) | 0;
        var pick = pool.splice(((seed >>> 8) % pool.length), 1)[0];
        out.push({ id: pick[0], itemId: pick[0], count: pick[1], reward: questReward(pick[0], pick[1]) });
    }
    return out;
}

function questState(playFabId, day) {
    var state = loadData(playFabId, QUEST_DATA_KEY, { day: day, done: [] });
    return state.day === day ? state : { day: day, done: [] };
}

handlers.GetQuests = function (args, context) {
    var day = questDay();
    var state = questState(currentPlayerId, day);
    var quests = todaysQuests(currentPlayerId, day);
    quests.forEach(function (q) { q.done = state.done.indexOf(q.id) !== -1; });
    return { day: day, quests: quests };
};

handlers.CompleteQuest = function (args, context) {
    args = args || {};
    var day = questDay();
    var quest = todaysQuests(currentPlayerId, day).filter(function (q) { return q.id === args.questId; })[0];
    if (!quest) return fail("bad_args", "오늘 의뢰가 아니다: " + args.questId);
    var state = questState(currentPlayerId, day);
    if (state.done.indexOf(quest.id) !== -1) return fail("already", "오늘 이미 건넸다");
    var inv = readInventory(currentPlayerId);
    if (countOf(inv, quest.itemId) < quest.count) {
        return fail("not_enough_items", "가진 것이 모자란다", { need: quest.count, have: countOf(inv, quest.itemId) });
    }
    takeItems(currentPlayerId, inv, quest.itemId, quest.count);
    server.AddUserVirtualCurrency({ PlayFabId: currentPlayerId, VirtualCurrency: "SC", Amount: quest.reward });
    state.done.push(quest.id);
    saveData(currentPlayerId, QUEST_DATA_KEY, state);
    return { questId: quest.id, coins: quest.reward };
};

// ---- 의뢰 줄기 (DESIGN 8.14 「의뢰 줄기」 🟡 M105-03) ----
// 평생 한 번, 5단계 순서대로. user data `questChain` = {step: 받은 단계 수}. 소모·코인·열쇠패·단계 +1 을 한 호출에서 처리한다.
// 단계 표 사본 — 원본은 DESIGN 줄과 DT_QuestChain, 시험이 DESIGN 줄과 맞대어 본다.
var QUEST_CHAIN_DATA_KEY = "questChain";
var QUEST_CHAIN = [
    { needs: [["item.wood", 30], ["item.berry", 10]], coins: 10 },
    { needs: [["item.crucian_carp", 3], ["item.carp", 1]], coins: 12 },
    { needs: [["item.carrot", 3], ["item.potato", 2], ["item.wheat", 2]], coins: 16 },
    { needs: [["item.fox_tail", 2], ["item.goat_horn", 2], ["item.reindeer_meat", 5]], coins: 20 },
    { needs: [["item.egg", 2], ["item.crucian_carp", 5], ["item.wood", 50]], coins: 22, itemId: "item.vault_pass", itemCount: 1 }
];

function questChainStep(playFabId) {
    var step = loadData(playFabId, QUEST_CHAIN_DATA_KEY, { step: 0 }).step;
    return isInt(step) && step > 0 ? Math.min(step, QUEST_CHAIN.length) : 0;
}

function questChainView(step) {
    var row = QUEST_CHAIN[step];
    return {
        step: step, total: QUEST_CHAIN.length, done: step >= QUEST_CHAIN.length,
        current: row ? { step: step + 1, needs: row.needs.map(function (n) { return { itemId: n[0], count: n[1] }; }), coins: row.coins, itemId: row.itemId || null, itemCount: row.itemCount || 0 } : null
    };
}

handlers.GetQuestChain = function (args, context) {
    return questChainView(questChainStep(currentPlayerId));
};

/** args.step(선택) = 클라가 보고 있는 단계(1~5). 지금 단계와 다르면 chain_locked. */
handlers.CompleteQuestChainStep = function (args, context) {
    args = args || {};
    var step = questChainStep(currentPlayerId);
    if (loadData(currentPlayerId, "story", null)) return fail("chain_done", "의뢰 줄기는 메인 스토리로 옮겨 갔다"); // 같은 ★ 을 두 번 받지 못하게
    if (step >= QUEST_CHAIN.length) return fail("chain_done", "의뢰 줄기를 이미 끝냈다");
    if (args.step !== undefined && args.step !== step + 1) return fail("chain_locked", "지금 단계가 아니다", { current: step + 1 });
    var row = QUEST_CHAIN[step];
    var inv = readInventory(currentPlayerId);
    var deltas = {};
    for (var i = 0; i < row.needs.length; i++) {
        var have = countOf(inv, row.needs[i][0]);
        if (have < row.needs[i][1]) return fail("chain_short", "재료가 모자란다", { itemId: row.needs[i][0], need: row.needs[i][1], have: have });
        deltas[row.needs[i][0]] = -row.needs[i][1];
    }
    if (row.itemId) {
        deltas[row.itemId] = (deltas[row.itemId] || 0) + row.itemCount;
        var full = roomCheck(inv, deltas);
        if (full) return full;
    }
    row.needs.forEach(function (n) { takeItems(currentPlayerId, inv, n[0], n[1]); });
    server.AddUserVirtualCurrency({ PlayFabId: currentPlayerId, VirtualCurrency: "SC", Amount: row.coins });
    if (row.itemId) giveItems(currentPlayerId, [{ itemId: row.itemId, count: row.itemCount }]);
    saveData(currentPlayerId, QUEST_CHAIN_DATA_KEY, { step: step + 1 });
    var out = questChainView(step + 1);
    out.coins = row.coins;
    out.rewardItemId = row.itemId || null;
    return out;
};

// ---- 메인 스토리 (docs/STORY.md 4·5절 · 5장 25단계) ----
// user data `story` = {step: 끝낸 단계 수, base: 지금 단계를 시작할 때의 업적 카운터 사본}. 판정은 전부 서버.
// 종류: give(★ 옛 의뢰 줄기 재료 건네기) · count(업적 카운터가 시작 때보다 n 이상 늘었나) · ranch(목장에 선 동물 수) ·
// report(가기·말 걸기·부수기 — 클라 보고, 보상 없는 단계라 신뢰 낮아도 됨 — STORY.md 4절).
// 🟡 「서로 다른 종 3종」은 종 기록이 서버에 없어 포획 횟수 3 으로 대신한다(DESIGN 의뢰 줄기 절에 적음).
var STORY_DATA_KEY = "story";
var STORY_CHAPTER_COINS = [20, 30, 40, 50, 60];
// 장 끝 꾸미기 (STORY.md 5절) — 팔지 않는 보상 전용이라 OUTFIT_ITEMS(상점 목록)에 넣지 않는다. 카탈로그 추가는 감독 몫.
var STORY_CHAPTER_ITEMS = ["item.story_traveler_hat", "item.story_ranch_signpost", "item.story_boatman_scarf", "item.story_broken_harpoon", "item.story_lighthouse_lamp"];
var STORY_FINAL_TITLE = "올가미지기";
var STORY_STEPS = [
    { kind: "report" }, { kind: "count", counter: "craft", n: 1 }, { kind: "give", chain: 0 }, { kind: "count", counter: "capture", n: 1 }, { kind: "ranch", n: 1 },
    { kind: "ranch", n: 3 }, { kind: "count", counter: "harvest", n: 3 }, { kind: "give", chain: 2 }, { kind: "count", counter: "petKinds", n: 3, absolute: true }, { kind: "report" },
    { kind: "report" }, { kind: "give", chain: 1 }, { kind: "count", counter: "craft", n: 1 }, { kind: "report" }, { kind: "report" },
    { kind: "report" }, { kind: "give", chain: 3 }, { kind: "report" }, { kind: "count", counter: "capture", n: 3 }, { kind: "report" },
    { kind: "give", chain: 4 }, { kind: "report" }, { kind: "count", counter: "captureFogbuck", n: 1 }, { kind: "count", counter: "harvestFogbuck", n: 1 }, { kind: "report" }
];
var STORY_CHAPTER_LEN = 5;

/**
 * 옛 의뢰 줄기 n단계 깬 계정이 이어받을 스토리 단계 수. 스토리 ★ 순서(줄기 0·2·1·3·4)대로 이미 받은 ★ 이 이어진 데까지만
 * 끝난 것으로 치고, 아직 안 받은 ★ 은 건너뛰지 않는다(n=2 면 줄기 2 가 아직이라 줄기 0 의 ★ 뒤에서 시작).
 */
function storyStepFromChain(chainStep) {
    var step = 0;
    for (var i = 0; i < STORY_STEPS.length; i++) {
        if (STORY_STEPS[i].kind !== "give") continue;
        if (STORY_STEPS[i].chain >= chainStep) break;
        step = i + 1;
    }
    return step;
}

function storyCounters(playFabId) {
    return loadData(playFabId, ACHIEVEMENTS_DATA_KEY, { counters: {} }).counters || {};
}

function storyLoad(playFabId) {
    var s = loadData(playFabId, STORY_DATA_KEY, null);
    if (s && isInt(s.step)) return { step: Math.max(0, Math.min(s.step, STORY_STEPS.length)), base: s.base || {}, paid: s.paid || [], titles: s.titles || [] };
    var n = questChainStep(playFabId); // 옛 줄기에서 이미 받은 ★(옛 순서 0..n-1)는 다시 안 준다
    var paid = [];
    for (var i = 0; i < n; i++) paid.push(i);
    return { step: storyStepFromChain(n), base: storyCounters(playFabId), paid: paid, titles: [] };
}

function storyProgress(playFabId, state) {
    var st = STORY_STEPS[state.step];
    if (!st) return { have: 0, need: 0 };
    if (st.kind === "count") {
        var c = storyCounters(playFabId);
        var from = st.absolute ? 0 : (state.base[st.counter] || 0); // absolute = 종 수처럼 단계 시작 전 것도 센다
        return { have: Math.min(st.n, Math.max(0, (c[st.counter] || 0) - from)), need: st.n };
    }
    if (st.kind === "ranch") {
        var n = loadRanch().filter(function (e) { return e && e.animalId; }).length;
        return { have: Math.min(st.n, n), need: st.n };
    }
    return { have: 0, need: 1 };
}

function storyView(playFabId, state) {
    var st = STORY_STEPS[state.step];
    var out = { step: state.step, total: STORY_STEPS.length, done: state.step >= STORY_STEPS.length,
        chapter: Math.min(Math.floor(state.step / STORY_CHAPTER_LEN) + 1, STORY_CHAPTER_COINS.length), titles: state.titles || [], current: null };
    if (st) {
        var pr = storyProgress(playFabId, state);
        out.current = { step: state.step + 1, kind: st.kind, have: pr.have, need: pr.need };
        if (st.kind === "give") out.current.needs = QUEST_CHAIN[st.chain].needs.map(function (x) { return { itemId: x[0], count: x[1] }; });
    }
    return out;
}

handlers.GetStory = function (args, context) {
    return storyView(currentPlayerId, storyLoad(currentPlayerId));
};

/** args.step(선택) = 클라가 보고 있는 단계(1~25), args.report = 가기·말 걸기·부수기 보고. 판정이 서야 단계 +1. */
handlers.AdvanceStory = function (args, context) {
    args = args || {};
    var state = storyLoad(currentPlayerId);
    if (state.step >= STORY_STEPS.length) return fail("story_done", "스토리를 이미 끝냈다");
    if (args.step !== undefined && args.step !== state.step + 1) return fail("story_locked", "지금 단계가 아니다", { current: state.step + 1 });
    var st = STORY_STEPS[state.step];
    var out = { coins: 0, rewardItemId: null };
    var chapterEnd = (state.step + 1) % STORY_CHAPTER_LEN === 0;
    var chapterItem = chapterEnd ? STORY_CHAPTER_ITEMS[(state.step + 1) / STORY_CHAPTER_LEN - 1] : null;
    if (chapterItem) { // 가방이 꽉 차면 아무것도 바꾸기 전에 거절
        var invC = readInventory(currentPlayerId);
        var dC = {}; dC[chapterItem] = 1;
        var fullC = roomCheck(invC, dC);
        if (fullC) return fullC;
    }
    if (st.kind === "report") {
        if (args.report !== true) return fail("story_short", "아직 아니다", { have: 0, need: 1 });
    } else if (st.kind === "give") {
        var row = QUEST_CHAIN[st.chain];
        if (state.paid.indexOf(st.chain) === -1) { // 옛 줄기에서 이미 받은 ★ 은 재료·보상 없이 지나간다
            var inv = readInventory(currentPlayerId);
            var deltas = {};
            for (var i = 0; i < row.needs.length; i++) {
                var have = countOf(inv, row.needs[i][0]);
                if (have < row.needs[i][1]) return fail("story_short", "재료가 모자란다", { itemId: row.needs[i][0], need: row.needs[i][1], have: have });
                deltas[row.needs[i][0]] = -row.needs[i][1];
            }
            if (row.itemId) {
                deltas[row.itemId] = (deltas[row.itemId] || 0) + row.itemCount;
                var full = roomCheck(inv, deltas);
                if (full) return full;
            }
            row.needs.forEach(function (n) { takeItems(currentPlayerId, inv, n[0], n[1]); });
            server.AddUserVirtualCurrency({ PlayFabId: currentPlayerId, VirtualCurrency: "SC", Amount: row.coins });
            if (row.itemId) giveItems(currentPlayerId, [{ itemId: row.itemId, count: row.itemCount }]);
            out.coins += row.coins;
            out.rewardItemId = row.itemId || null;
        }
    } else {
        var pr = storyProgress(currentPlayerId, state);
        if (pr.have < pr.need) return fail("story_short", "아직 모자란다", pr);
    }
    var next = state.step + 1;
    if (next % STORY_CHAPTER_LEN === 0) { // 장 끝 보상 (STORY.md 5절)
        var chapterCoins = STORY_CHAPTER_COINS[next / STORY_CHAPTER_LEN - 1];
        server.AddUserVirtualCurrency({ PlayFabId: currentPlayerId, VirtualCurrency: "SC", Amount: chapterCoins });
        out.coins += chapterCoins;
        out.chapterCoins = chapterCoins;
        giveItems(currentPlayerId, [{ itemId: chapterItem, count: 1 }]);
        out.chapterItemId = chapterItem;
    }
    var titles = state.titles || [];
    if (next === STORY_STEPS.length && titles.indexOf(STORY_FINAL_TITLE) === -1) titles = titles.concat([STORY_FINAL_TITLE]);
    var after = { titles: titles, step: next, base: storyCounters(currentPlayerId), paid: state.paid.concat(st.kind === "give" && state.paid.indexOf(st.chain) === -1 ? [st.chain] : []) };
    saveData(currentPlayerId, STORY_DATA_KEY, after);
    var view = storyView(currentPlayerId, after);
    view.coins = out.coins;
    view.rewardItemId = out.rewardItemId;
    if (out.chapterCoins) { view.chapterCoins = out.chapterCoins; view.chapterItemId = out.chapterItemId; }
    return view;
};

// ---- 7일 누적 출석 (DESIGN 11 「7일 출석」 · M21-05) ----
// 서버 UTC 날짜마다 한 번. 빠진 날이 있어도 처음으로 안 돌아가고 다음 칸, 7일째 다음은 다시 1일.
// 칸마다 coins 또는 itemId+count — 젬은 없다. 7일째 모자를 이미 받았거나 배낭에 있는 계정은 ATTENDANCE_HAT_REPEAT_COINS.
var ATTENDANCE_DATA_KEY = "attendance";
var ATTENDANCE_HAT = "item.sprout_hat";
var ATTENDANCE_REWARDS = [
    { coins: 50 }, { coins: 50 }, { coins: 100 }, { itemId: "item.seed_carrot", count: 3 },
    { coins: 100 }, { coins: 150 }, { itemId: ATTENDANCE_HAT, count: 1 }
];
var ATTENDANCE_HAT_REPEAT_COINS = 150;

function attendanceState(playFabId) {
    return loadData(playFabId, ATTENDANCE_DATA_KEY, { claims: 0, lastDay: null, hatGranted: false });
}

/** 다음에 받을 칸(1~7)의 보상 — 모자를 이미 받았거나 배낭에 있으면(상자·옷장으로 먼저 산 계정) 7일째는 코인. */
function attendanceRewardFor(state, inv) {
    var reward = ATTENDANCE_REWARDS[state.claims % ATTENDANCE_REWARDS.length];
    if (reward.itemId === ATTENDANCE_HAT && (state.hatGranted || countOf(inv, ATTENDANCE_HAT) > 0)) {
        return { coins: ATTENDANCE_HAT_REPEAT_COINS };
    }
    return reward;
}

handlers.GetAttendance = function (args, context) {
    var day = questDay();
    var state = attendanceState(currentPlayerId);
    return {
        day: day,
        nextSlot: state.claims % ATTENDANCE_REWARDS.length + 1,
        claimedToday: state.lastDay === day,
        next: attendanceRewardFor(state, readInventory(currentPlayerId)),
        rewards: ATTENDANCE_REWARDS
    };
};

handlers.ClaimAttendance = function (args, context) {
    var day = questDay();
    var state = attendanceState(currentPlayerId);
    if (state.lastDay === day) return fail("already", "오늘 출석은 이미 받았다");
    var slot = state.claims % ATTENDANCE_REWARDS.length + 1;
    var inv = readInventory(currentPlayerId);
    var reward = attendanceRewardFor(state, inv);
    if (reward.itemId) {
        var deltas = {};
        deltas[reward.itemId] = reward.count;
        var full = roomCheck(inv, deltas);
        if (full) return full; // 칸은 안 넘어간다 — 가방을 비우고 오늘 다시 받는다
        giveItems(currentPlayerId, [{ itemId: reward.itemId, count: reward.count }]);
        if (reward.itemId === ATTENDANCE_HAT) state.hatGranted = true;
    } else {
        server.AddUserVirtualCurrency({ PlayFabId: currentPlayerId, VirtualCurrency: "SC", Amount: reward.coins });
    }
    state.claims += 1;
    state.lastDay = day;
    saveData(currentPlayerId, ATTENDANCE_DATA_KEY, state);
    return { day: day, slot: slot, reward: reward };
};

// ---- 주간 우두머리 보상 (DESIGN 10.6 「변종·우두머리 확률 고지」 · M21-07d) ----
// 몸은 위험구역에 300초마다 다시 서고, 보상 굴림만 계정당 주 1번(UTC 월요일 00:00 경계).
// 모자 BOSS_HAT_CHANCE — 이미 받은 계정의 당첨은 BOSS_HAT_REPEAT_COINS. 꽝도 그 주 기록을 남긴다.
var BOSS_DATA_KEY = "boss";
var BOSS_HAT = "item.chief_hat";
var BOSS_HAT_CHANCE = 0.1;
var BOSS_HAT_REPEAT_COINS = 150;

/** UTC 날짜 번호(questDay) → 주 번호. 1970-01-01(목) 이 0주 — 첫 월요일(1970-01-05)부터 1주. */
function bossWeekOf(day) { return Math.floor((day + 3) / 7); }

handlers.ClaimBoss = function (args, context) {
    var week = bossWeekOf(questDay());
    var state = loadData(currentPlayerId, BOSS_DATA_KEY, { lastWeek: null, hatGranted: false });
    if (state.lastWeek === week) return fail("already_claimed", "이번 주 우두머리 보상은 이미 받았다");
    var won = Math.random() < BOSS_HAT_CHANCE;
    var reward = null;
    if (won && !state.hatGranted) {
        var deltas = {};
        deltas[BOSS_HAT] = 1;
        var full = roomCheck(readInventory(currentPlayerId), deltas);
        if (full) return full; // 주 기록을 안 남긴다 — 가방을 비우고 다시 잡으면 다시 굴린다
        giveItems(currentPlayerId, [{ itemId: BOSS_HAT, count: 1 }]);
        state.hatGranted = true;
        reward = { itemId: BOSS_HAT, count: 1 };
    } else if (won) {
        server.AddUserVirtualCurrency({ PlayFabId: currentPlayerId, VirtualCurrency: "SC", Amount: BOSS_HAT_REPEAT_COINS });
        reward = { coins: BOSS_HAT_REPEAT_COINS };
    }
    state.lastWeek = week;
    saveData(currentPlayerId, BOSS_DATA_KEY, state);
    var keyDeltas = {};
    keyDeltas[VAULT_PASS] = 1;
    var keyItems = [{ itemId: VAULT_PASS, count: 1 }];
    var keyDrop = null;
    if (roomCheck(readInventory(currentPlayerId), keyDeltas)) keyDrop = recordDrop(currentPlayerId, keyItems); // 칸이 없으면 발밑 가방
    else giveItems(currentPlayerId, keyItems);
    var bossReply = { week: week, won: won, reward: reward, vaultPass: 1 };
    if (keyDrop) bossReply.drop = keyDrop;
    return bossReply;
};

/** 선착장 배 삯. dest: hunt · danger · base(돌아오기, 무료). 위험구역은 도감 사냥+식물 50% 뒤. */
handlers.PayFerry = function (args, context) {
    args = args || {};
    var fare = FERRY_FARES[args.dest];
    if (fare === undefined) return fail("bad_args", "dest 는 hunt, danger, base, island");
    if (args.dest === "danger" && !codexProgress(loadData(currentPlayerId, "codex", [])).rewards.dangerDock) {
        return fail("locked", "도감 사냥+식물 " + CODEX_UNLOCK_PERCENT + "% 가 먼저다");
    }
    if (fare > 0) {
        var inv = readInventory(currentPlayerId);
        fare = Math.ceil(fare * (wornHatEffect(inv).fareMul || 1)); // 13.5 여행자 모자 — 올림(5→3, 10→5)
        var have = coinsOf(inv);
        if (have < fare) return fail("not_enough_coins", "코인이 모자란다", { need: fare, have: have });
        spendCoins(fare);
    }
    return { dest: args.dest, paid: fare };
};

// ---- 배 ----

handlers.LaunchBoat = function (args, context) {
    var boat = loadData(currentPlayerId, BOAT_DATA_KEY, { launched: false });
    if (boat.launched) return fail("already", "이미 띄운 배가 있다");
    var inv = readInventory(currentPlayerId);
    if (countOf(inv, BOAT_ITEM) < 1) return fail("not_enough_items", "구명보트가 없다", { itemId: BOAT_ITEM });
    takeItems(currentPlayerId, inv, BOAT_ITEM, 1);
    var launchedAtUtc = new Date().toISOString();
    saveData(currentPlayerId, BOAT_DATA_KEY, { launched: true, launchedAtUtc: launchedAtUtc });
    return { launched: true, launchedAtUtc: launchedAtUtc };
};

handlers.StowBoat = function (args, context) {
    var boat = loadData(currentPlayerId, BOAT_DATA_KEY, { launched: false });
    if (!boat.launched) return fail("not_launched", "띄운 배가 없다");
    var deltas = {};
    deltas[BOAT_ITEM] = 1;
    var full = roomCheck(readInventory(currentPlayerId), deltas);
    if (full) return full;
    var refused = tryGiveItems(currentPlayerId, [{ itemId: BOAT_ITEM, count: 1 }]);
    if (refused) return refused;
    saveData(currentPlayerId, BOAT_DATA_KEY, { launched: false });
    return { stowed: true };
};

// ---- 건네기 (DESIGN 3.3 「건네기」, M104-05) ----
// 선물만(값 없음)·받는 쪽 수락제. 보내는 순간 보낸 사람 배낭에서 빼 받는 사람 `gifts` 에 보관, 수락하면 받는 사람 배낭으로,
// 거절·만료면 보낸 사람에게 돌려준다. 거리(1000cm)·같은 방은 호스트가 보고, 아이템 이동은 여기서만 판정한다.
var GIFT_DAILY_LIMIT = 20;
var GIFT_PENDING_MAX = 10;
var GIFT_EXPIRE_MS = 24 * 60 * 60 * 1000;
var GIFTS_DATA_KEY = "gifts";      // 받는 사람: 대기 선물 [{id, from, itemId, count, createdAtUtc}]
var GIFTS_OUT_DATA_KEY = "giftsOut"; // 보낸 사람: 아직 대기 중인 보낸 선물 [{id, to, createdAtUtc}]
var GIFT_DAY_DATA_KEY = "giftDay"; // 보낸 사람: {day, n}

/** 못 보내는 것: 가챠·펫·목줄·꾸미기 상점 상품(현금화·도감 새기 막이). */
function isGiftExcluded(itemId) {
    if (!isBackpackItem(itemId)) return true;
    if (itemId === VAULT_PASS) return true; // 8.1c 열쇠패는 건네지 못한다
    if (Object.keys(CODEX_COLLARS).some(function (k) { return CODEX_COLLARS[k] === itemId; })) return true;
    return COSMETIC_SHOP_ITEMS.indexOf(itemId) !== -1;
}

function giftExpired(gift) { return Date.now() - Date.parse(gift.createdAtUtc) >= GIFT_EXPIRE_MS; }

/** 만료된 대기 선물을 보낸 사람에게 돌려주고 목록에서 뺀다. 돌려주기가 거절되면 그 선물은 남겨 다음에 다시 한다. */
function expireGifts(ownerId) {
    var gifts = loadData(ownerId, GIFTS_DATA_KEY, []);
    var keep = [];
    gifts.forEach(function (g) {
        if (!giftExpired(g)) { keep.push(g); return; }
        if (tryGiveItems(g.from, [{ itemId: g.itemId, count: g.count }])) keep.push(g);
    });
    if (keep.length !== gifts.length) saveData(ownerId, GIFTS_DATA_KEY, keep);
    return keep;
}

/** 보낸 사람 쪽: 내가 보낸 선물 중 만료된 것을 받는 사람 목록에서 돌려받고, 이미 사라진(수락·거절·만료) 기록은 지운다. */
function sweepOutgoing(senderId) {
    var out = loadData(senderId, GIFTS_OUT_DATA_KEY, []);
    var alive = out.filter(function (o) {
        var left = giftExpired(o) ? expireGifts(o.to) : loadData(o.to, GIFTS_DATA_KEY, []);
        return left.some(function (g) { return g.id === o.id; });
    });
    if (alive.length !== out.length) saveData(senderId, GIFTS_OUT_DATA_KEY, alive);
}

function removeOutgoing(senderId, giftId) {
    var out = loadData(senderId, GIFTS_OUT_DATA_KEY, []);
    var rest = out.filter(function (o) { return o.id !== giftId; });
    if (rest.length !== out.length) saveData(senderId, GIFTS_OUT_DATA_KEY, rest);
}

handlers.GiftItem = function (args, context) {
    args = args || {};
    var count = args.count === undefined ? 1 : args.count;
    if (typeof args.targetPlayerId !== "string" || !args.targetPlayerId) return fail("bad_args", "targetPlayerId 필요");
    if (typeof args.itemId !== "string" || (args.itemId.indexOf("item.") !== 0 && args.itemId.indexOf("gacha.") !== 0)) {
        return fail("unknown_item", "알 수 없는 아이템: " + args.itemId);
    }
    if (!isInt(count) || count < 1 || count > STACK_MAX) return fail("bad_args", "count 는 1~" + STACK_MAX + " 정수");
    if (args.targetPlayerId === currentPlayerId) return fail("self_gift", "자기 자신에게는 못 보낸다");
    if (isGiftExcluded(args.itemId)) return fail("excluded_item", "보낼 수 없는 아이템", { itemId: args.itemId });
    try {
        server.GetPlayerProfile({ PlayFabId: args.targetPlayerId });
    } catch (e) {
        return fail("bad_args", "받는 사람이 없다");
    }
    // 받는 사람이 나를 차단했으면 선물도 막는다(차단 = 원치 않는 접촉 끊기). 차단 사실은 문장에 드러내지 않는다.
    var theirBlocks = loadData(args.targetPlayerId, BLOCKS_DATA_KEY, []);
    if (theirBlocks.some(function (b) { return b.id === currentPlayerId; })) return fail("blocked", "이 사람에게는 보낼 수 없다");

    var today = questDay();
    var day = loadData(currentPlayerId, GIFT_DAY_DATA_KEY, { day: today, n: 0 });
    if (day.day !== today) day = { day: today, n: 0 };
    if (day.n >= GIFT_DAILY_LIMIT) return fail("daily_limit", "오늘 보낼 수 있는 선물을 다 보냈다", { limit: GIFT_DAILY_LIMIT });

    var pending = expireGifts(args.targetPlayerId);
    if (pending.length >= GIFT_PENDING_MAX) return fail("target_pending_full", "받는 사람의 대기 선물이 가득 찼다", { limit: GIFT_PENDING_MAX });

    var inv = readInventory(currentPlayerId);
    if (countOf(inv, args.itemId) < count) {
        return fail("not_enough_items", "가진 것보다 많다", { need: count, have: countOf(inv, args.itemId) });
    }

    // 배낭에서 빼는 것이 먼저 — 빼고 나서 보관 기록이 실패하면 물건이 사라질 수 있어도 복제는 안 생긴다.
    var gift = { id: newId(), from: currentPlayerId, itemId: args.itemId, count: count, createdAtUtc: new Date().toISOString() };
    takeItems(currentPlayerId, inv, args.itemId, count);
    pending.push(gift);
    saveData(args.targetPlayerId, GIFTS_DATA_KEY, pending);
    var out = loadData(currentPlayerId, GIFTS_OUT_DATA_KEY, []);
    out.push({ id: gift.id, to: args.targetPlayerId, createdAtUtc: gift.createdAtUtc });
    saveData(currentPlayerId, GIFTS_OUT_DATA_KEY, out);
    day.n += 1;
    saveData(currentPlayerId, GIFT_DAY_DATA_KEY, day);
    return { sent: true, giftId: gift.id, itemId: gift.itemId, count: count, sentToday: day.n, limit: GIFT_DAILY_LIMIT };
};

/** 내 앞 대기 선물 목록(만료는 먼저 보낸 사람에게 돌려보낸다). 내가 보낸 만료 선물도 이때 돌려받는다. */
handlers.GetGifts = function (args, context) {
    sweepOutgoing(currentPlayerId);
    return { gifts: expireGifts(currentPlayerId), limit: GIFT_PENDING_MAX };
};

handlers.AcceptGift = function (args, context) {
    args = args || {};
    var gifts = loadData(currentPlayerId, GIFTS_DATA_KEY, []);
    var gift = gifts.filter(function (g) { return g.id === args.giftId; })[0];
    if (!gift) return fail("bad_args", "그 선물이 없다", { giftId: args.giftId });
    if (giftExpired(gift)) {
        expireGifts(currentPlayerId);
        return fail("expired", "선물이 만료돼 보낸 사람에게 돌아갔다", { giftId: gift.id });
    }
    var inv = readInventory(currentPlayerId);
    var deltas = {};
    deltas[gift.itemId] = gift.count;
    var full = roomCheck(inv, deltas);
    if (full) return full;
    var bad = tryGiveItems(currentPlayerId, [{ itemId: gift.itemId, count: gift.count }]);
    if (bad) return bad;
    saveData(currentPlayerId, GIFTS_DATA_KEY, gifts.filter(function (g) { return g.id !== gift.id; }));
    removeOutgoing(gift.from, gift.id);
    return { accepted: true, giftId: gift.id, itemId: gift.itemId, count: gift.count, from: gift.from };
};

handlers.DeclineGift = function (args, context) {
    args = args || {};
    var gifts = loadData(currentPlayerId, GIFTS_DATA_KEY, []);
    var gift = gifts.filter(function (g) { return g.id === args.giftId; })[0];
    if (!gift) return fail("bad_args", "그 선물이 없다", { giftId: args.giftId });
    var bad = tryGiveItems(gift.from, [{ itemId: gift.itemId, count: gift.count }]);
    if (bad) return bad;
    saveData(currentPlayerId, GIFTS_DATA_KEY, gifts.filter(function (g) { return g.id !== gift.id; }));
    removeOutgoing(gift.from, gift.id);
    return { declined: true, giftId: gift.id };
};

// ---- 신고·차단 (DESIGN 4.8 「신고·차단」, 앱스토어 1.2 — 사람 결정 10-16) ----
// 신고는 운영자가 볼 기록만 남긴다(PlayStream 타이틀 이벤트 `player_reported`) — 자동 제재는 없다.
// 차단은 내 화면에서만 그 사람 이름·이모트·말풍선을 가리는 목록이다. 상대에게는 아무것도 안 바뀐다.
var REPORT_REASONS = ["name", "harass", "cheat", "other"];
var REPORT_DAILY_MAX = 10;
var REPORT_NOTE_MAX = 100;
var BLOCK_MAX = 100;
var REPORT_DAY_DATA_KEY = "reportDay"; // 신고한 사람: {day, targets:[PlayFabId]}
var BLOCKS_DATA_KEY = "blocks";        // [{id, name, atUtc}]

function playerExists(playFabId) {
    try {
        server.GetPlayerProfile({ PlayFabId: playFabId });
        return true;
    } catch (e) {
        return false;
    }
}

function badTarget(args) {
    if (typeof args.targetPlayerId !== "string" || !args.targetPlayerId || args.targetPlayerId.length > 64) {
        return fail("bad_args", "targetPlayerId 필요");
    }
    if (args.targetPlayerId === currentPlayerId) return fail("self", "자기 자신은 고를 수 없다");
    return null;
}

handlers.ReportPlayer = function (args, context) {
    args = args || {};
    var bad = badTarget(args);
    if (bad) return bad;
    if (REPORT_REASONS.indexOf(args.reason) === -1) return fail("bad_args", "reason 은 " + REPORT_REASONS.join("·") + " 중 하나");
    var note = args.note === undefined ? "" : args.note;
    if (typeof note !== "string" || note.length > REPORT_NOTE_MAX) return fail("bad_args", "note 는 " + REPORT_NOTE_MAX + "자 이하 글");
    if (!playerExists(args.targetPlayerId)) return fail("bad_args", "신고할 사람이 없다");

    var today = questDay();
    var day = loadData(currentPlayerId, REPORT_DAY_DATA_KEY, { day: today, targets: [] });
    if (day.day !== today) day = { day: today, targets: [] };
    if (day.targets.indexOf(args.targetPlayerId) !== -1) return fail("already", "오늘 이미 신고한 사람이다");
    if (day.targets.length >= REPORT_DAILY_MAX) return fail("daily_limit", "오늘 신고 한도를 다 썼다", { limit: REPORT_DAILY_MAX });

    var atUtc = new Date().toISOString();
    server.WriteTitleEvent({
        EventName: "player_reported",
        Body: { reporterId: currentPlayerId, targetId: args.targetPlayerId, reason: args.reason, note: note, atUtc: atUtc }
    });
    day.targets.push(args.targetPlayerId);
    saveData(currentPlayerId, REPORT_DAY_DATA_KEY, day);
    return { reported: true, reason: args.reason, reportedToday: day.targets.length, limit: REPORT_DAILY_MAX };
};

/** 로그인 때 클라가 부른다 — 내부 데이터는 클라가 직접 못 읽는다. */
handlers.GetBlockList = function (args, context) {
    return { blocks: loadData(currentPlayerId, BLOCKS_DATA_KEY, []), limit: BLOCK_MAX };
};

handlers.BlockPlayer = function (args, context) {
    args = args || {};
    var bad = badTarget(args);
    if (bad) return bad;
    var name = typeof args.name === "string" ? args.name.slice(0, 32) : "";
    var blocks = loadData(currentPlayerId, BLOCKS_DATA_KEY, []);
    if (blocks.some(function (b) { return b.id === args.targetPlayerId; })) return { blocked: true, blocks: blocks, limit: BLOCK_MAX };
    if (blocks.length >= BLOCK_MAX) return fail("full", "차단 목록이 가득 찼다", { limit: BLOCK_MAX });
    if (!playerExists(args.targetPlayerId)) return fail("bad_args", "차단할 사람이 없다");
    blocks.push({ id: args.targetPlayerId, name: name, atUtc: new Date().toISOString() });
    saveData(currentPlayerId, BLOCKS_DATA_KEY, blocks);
    return { blocked: true, blocks: blocks, limit: BLOCK_MAX };
};

handlers.UnblockPlayer = function (args, context) {
    args = args || {};
    if (typeof args.targetPlayerId !== "string" || !args.targetPlayerId) return fail("bad_args", "targetPlayerId 필요");
    var blocks = loadData(currentPlayerId, BLOCKS_DATA_KEY, []);
    var rest = blocks.filter(function (b) { return b.id !== args.targetPlayerId; });
    if (rest.length === blocks.length) return fail("not_found", "차단 목록에 없다");
    saveData(currentPlayerId, BLOCKS_DATA_KEY, rest);
    return { unblocked: true, blocks: rest, limit: BLOCK_MAX };
};

/**
 * 계정 삭제 (DESIGN 4.4 「계정 삭제」 · BACKEND.md 「계정 삭제」 · Apple 5.1.1(v)).
 * 확인 문자열 `confirm: "DELETE"` 없이는 아무것도 안 한다 — 잘못 눌린 호출 하나로 계정이 사라지면 안 된다.
 * 1) 타이틀 이벤트로 요청 기록을 먼저 남긴다(플레이어를 지워도 남는다 — 운영자가 마스터 계정 정리 대상을 찾는 근거).
 * 2) `server.DeletePlayer` 로 타이틀 계정을 통째로 지운다(데이터·인벤토리·재화·목장·통계 전부).
 * 3) 그게 실패하면 인벤토리 회수 → 재화 0 → 내부 데이터 키 전부 삭제로 비우고 `fullyDeleted:false` 를 돌려준다.
 *    클라이언트는 이때 기기 ID 연결을 끊어(Unlink) 다음 접속이 새 계정이 되게 한다.
 * 두 번 불러도 같은 답(`deleted:true`)이다 — 이미 빈 계정을 또 비울 뿐이다.
 */
handlers.DeleteMyAccount = function (args, context) {
    if (!args || args.confirm !== "DELETE") return fail("bad_args", "삭제 확인 문자열이 없다");
    var requestedAtUtc = new Date().toISOString();
    server.WriteTitleEvent({
        EventName: "account_delete_requested",
        Body: { playFabId: currentPlayerId, requestedAtUtc: requestedAtUtc }
    });
    try {
        server.DeletePlayer({ PlayFabId: currentPlayerId });
        return { deleted: true, fullyDeleted: true, requestedAtUtc: requestedAtUtc };
    } catch (e) {
        log.error("DeletePlayer 실패, 비우기로 대신한다: " + e);
    }
    wipePlayer(currentPlayerId);
    return { deleted: true, fullyDeleted: false, requestedAtUtc: requestedAtUtc };
};

function wipePlayer(playFabId) {
    var res = server.GetUserInventory({ PlayFabId: playFabId });
    var revoke = (res.Inventory || []).map(function (it) {
        return { PlayFabId: playFabId, ItemInstanceId: it.ItemInstanceId };
    });
    for (var b = 0; b < revoke.length; b += 25) {
        server.RevokeInventoryItems({ Items: revoke.slice(b, b + 25) }); // 한 번에 25개까지(PlayFab 제한)
    }
    var wallet = res.VirtualCurrency || {};
    Object.keys(wallet).forEach(function (code) {
        if (wallet[code] > 0) {
            server.SubtractUserVirtualCurrency({ PlayFabId: playFabId, VirtualCurrency: code, Amount: wallet[code] });
        }
    });
    var keys = Object.keys(server.GetUserInternalData({ PlayFabId: playFabId }).Data || {});
    for (var k = 0; k < keys.length; k += 10) {
        server.UpdateUserInternalData({ PlayFabId: playFabId, KeysToRemove: keys.slice(k, k + 10) });
    }
}

// ---- 레퍼런스 맞춤 내부 유틸 ----

function fail(code, message, extra) {
    var out = { error: message, code: code };
    if (extra) Object.keys(extra).forEach(function (k) { out[k] = extra[k]; });
    return out;
}

function isInt(n) { return typeof n === "number" && Math.floor(n) === n; }
function isCount(n) { return isInt(n) && n >= 1 && n <= COUNT_MAX; }
function newId() { return Date.now().toString(36) + Math.random().toString(36).slice(2, 10); }

function loadData(playFabId, key, fallback) {
    var data = server.GetUserInternalData({ PlayFabId: playFabId, Keys: [key] });
    if (data.Data && data.Data[key] && data.Data[key].Value) return JSON.parse(data.Data[key].Value);
    return fallback;
}

function saveData(playFabId, key, value) {
    var payload = {};
    payload[key] = JSON.stringify(value);
    server.UpdateUserInternalData({ PlayFabId: playFabId, Data: payload });
}

/** 펫·가챠는 배낭 칸도 안 차지하고 떨구지도 않는다(DESIGN 8.12 「사망해도 안 떨구는 것」). */
function isBackpackItem(itemId) {
    return itemId.indexOf("gacha.") !== 0 && itemId.indexOf("item.pet_") !== 0;
}

function readInventory(playFabId) {
    var res = server.GetUserInventory({ PlayFabId: playFabId });
    var inv = { counts: {}, instances: {}, currency: res.VirtualCurrency || {} };
    (res.Inventory || []).forEach(function (it) {
        var n = typeof it.RemainingUses === "number" ? it.RemainingUses : 1;
        inv.counts[it.ItemId] = (inv.counts[it.ItemId] || 0) + n;
        (inv.instances[it.ItemId] = inv.instances[it.ItemId] || []).push(it);
    });
    return inv;
}

function countOf(inv, itemId) { return inv.counts[itemId] || 0; }
function coinsOf(inv) { return inv.currency.SC || 0; }

function spendCoins(amount) {
    server.SubtractUserVirtualCurrency({ PlayFabId: currentPlayerId, VirtualCurrency: "SC", Amount: amount });
}

function copyCounts(counts) {
    var out = {};
    Object.keys(counts).forEach(function (k) { out[k] = counts[k]; });
    return out;
}

function slotsFor(counts) {
    var slots = 0;
    Object.keys(counts).forEach(function (id) {
        if (counts[id] > 0 && isBackpackItem(id)) slots += Math.ceil(counts[id] / STACK_MAX);
    });
    return slots;
}

// 🟡 서버는 장착 여부·배에 탄 여부를 모른다 — 등산 짐가방·배 짐칸를 가지고 있으면 각각 +8 로 본다.
function capacityFor(counts) {
    return BACKPACK_SLOTS + ((counts[BACKPACK_BONUS_ITEM] || 0) > 0 ? BACKPACK_BONUS_SLOTS : 0);
}

/** 바뀐 뒤 칸이 모자라면 사유를 돌려준다. 이미 넘친 가방이라도 칸이 늘지 않는 변화(빼기·같은 칸에 쌓기)는 막지 않는다. */
function roomCheck(inv, deltas) {
    var after = copyCounts(inv.counts);
    Object.keys(deltas).forEach(function (id) { after[id] = (after[id] || 0) + deltas[id]; });
    var before = slotsFor(inv.counts);
    var need = slotsFor(after);
    var have = capacityFor(after);
    if (need > have && need > before) return fail("inventory_full", "가방이 가득 찼다", { need: need, have: have });
    return null;
}

function takeItems(playFabId, inv, itemId, count) {
    var left = count;
    var revoke = [];
    var list = inv.instances[itemId] || [];
    for (var i = 0; i < list.length && left > 0; i++) {
        var it = list[i];
        if (it.taken) continue;
        if (typeof it.RemainingUses === "number") {
            var use = Math.min(it.RemainingUses, left);
            server.ConsumeItem({ PlayFabId: playFabId, ItemInstanceId: it.ItemInstanceId, ConsumeCount: use });
            it.RemainingUses -= use;
            if (it.RemainingUses === 0) it.taken = true;
            left -= use;
        } else {
            revoke.push({ PlayFabId: playFabId, ItemInstanceId: it.ItemInstanceId });
            it.taken = true;
            left -= 1;
        }
    }
    for (var b = 0; b < revoke.length; b += 25) {
        server.RevokeInventoryItems({ Items: revoke.slice(b, b + 25) }); // 한 번에 25개까지(PlayFab 제한)
    }
    inv.counts[itemId] = countOf(inv, itemId) - count;
}

function takeAll(playFabId, inv, take) {
    Object.keys(take).forEach(function (id) { takeItems(playFabId, inv, id, take[id]); });
}

function giveItems(playFabId, list) {
    var ids = [];
    list.forEach(function (e) { ids = ids.concat(repeatItem(e.itemId, e.count)); });
    // 한 번에 100개씩 — 시험 키트 탄 800개처럼 큰 묶음이 한 호출에 몰리지 않게(작은 묶음은 그대로 한 호출)
    for (var i = 0; i < ids.length; i += 100) server.GrantItemsToUser({ PlayFabId: playFabId, ItemIds: ids.slice(i, i + 100) });
}

/**
 * 기록(떨군 가방·보관함·가구·띄운 배)에서 꺼내 주는 곳은 이것으로 먼저 준다 — 거절되면 기록을 안 지우고
 * `grant_failed` 를 돌려 다시 시도하게 한다. PlayFab 오류 코드는 `error` 에 실어 클라 로그(`[Backpack] …`)에 남는다.
 */
function tryGiveItems(playFabId, list) {
    try {
        giveItems(playFabId, list);
        return null;
    } catch (e) {
        var api = (e && e.apiErrorInfo && e.apiErrorInfo.apiError) || {};
        var why = api.error || (e && e.message) || String(e);
        log.error("GrantItemsToUser 거절: " + why);
        return fail("grant_failed", "지급 실패(" + why + ") — 그대로 남았다, 다시 시도", { playFabError: why, playFabErrorCode: api.errorCode || 0 });
    }
}

function negated(take) {
    var out = {};
    Object.keys(take).forEach(function (k) { out[k] = -take[k]; });
    return out;
}

function toList(counts) {
    return Object.keys(counts).map(function (k) { return { itemId: k, count: counts[k] }; });
}

function hasCraftTag(itemId, tag) {
    if (CRAFT_TAG_ITEMS[tag]) return CRAFT_TAG_ITEMS[tag].indexOf(itemId) !== -1; // 잼·오일은 레시피 결과라 아래 줄보다 먼저
    if (tag === "Fish") return !!FISH_TABLE[itemId];
    if (RECIPES[itemId]) return false; // 구운 고기는 다시 Meat 가 아니다
    if (tag === "Meat") return /_meat$/.test(itemId);
    if (tag === "Hide") return /_(hide|fur)$/.test(itemId) || itemId === "item.wool" || GACHA_HIDE_ITEMS.indexOf(itemId) !== -1;
    if (tag === "Feather") return /_feather$/.test(itemId) && HUNT_TROPHIES.indexOf(itemId) !== -1;
    return false;
}

/** 재료를 고른다 — 태그 재료는 싼 것부터, 가챠 소재는 맨 나중에 쓴다. 가챠 소재는 뽑을 때 문서 수량만큼 쌓여 1개 = 재료 1개. */
function pickInputs(inv, recipe, times) {
    var take = {};
    for (var i = 0; i < recipe.inputs.length; i++) {
        var input = recipe.inputs[i];
        var need = input.count * times;
        var candidates = input.itemId ? [input.itemId].concat(GACHA_SUBSTITUTES[input.itemId] || []) : Object.keys(inv.counts).filter(function (id) {
            return hasCraftTag(id, input.tag);
        }).sort(function (a, b) {
            var ga = isBackpackItem(a) ? 0 : 1;
            var gb = isBackpackItem(b) ? 0 : 1;
            return ga - gb || sellPriceOf(a) - sellPriceOf(b) || (a < b ? -1 : 1);
        });
        for (var c = 0; c < candidates.length && need > 0; c++) {
            var use = Math.min(countOf(inv, candidates[c]) - (take[candidates[c]] || 0), need);
            if (use > 0) {
                take[candidates[c]] = (take[candidates[c]] || 0) + use;
                need -= use;
            }
        }
        if (need > 0) {
            return fail("not_enough_items", "재료가 모자란다",
                { itemId: input.itemId || input.tag, need: input.count * times, have: input.count * times - need });
        }
    }
    return take;
}

/** 파는 값 (DESIGN 8.14 「파는 값」). 0 = 못 판다. 🟡 상점 물건·가챠는 0. 클라이언트 `DT_ItemTable.SellPrice` 와 전수 대조(node 시험). */
function sellPriceOf(itemId) {
    if (FIXED_SELL_PRICE[itemId]) return FIXED_SELL_PRICE[itemId];
    if (FRUITS.indexOf(itemId) !== -1) return FRUIT_PRICE_NATIVE; // 8.18 표 값 = 고유 과일 값. 남의 과일 40 은 SellItem 이 계정마다 정한다
    // 🟡 펫 = 목장 한 주기 코인(8.14 「펫 판매가」) — 목장에 올린 펫은 배낭에 없으니 안 올린 것만 팔린다
    if (CAPTURE_PET_ITEMS.indexOf(itemId) !== -1 || VARIANT_PET_ITEMS.indexOf(itemId) !== -1) return SELL_PRICE.pet;
    if (SHOP_ITEMS[itemId] || !isBackpackItem(itemId)) return 0;
    var recipe = RECIPES[itemId];
    if (recipe) {
        // 가공품 = 재료 값 합 + 1. 합이 0 이면 못 판다 — 나무·산딸기만 쓰는 가공품이 공짜 코인 길이 되지 않게(감독 09-24).
        var sum = 0;
        recipe.inputs.forEach(function (input) {
            var unit = input.itemId ? sellPriceOf(input.itemId)
                : input.tag === "Meat" ? SELL_PRICE.meat : input.tag === "Hide" ? SELL_PRICE.hide
                : input.tag === "Feather" ? SELL_PRICE.trophy
                // 13.6 태그 = 그 태그 아이템 중 가장 싼 값 (클라이언트 표 규칙과 같다)
                : Math.min.apply(null, (CRAFT_TAG_ITEMS[input.tag] || Object.keys(FISH_TABLE)).map(sellPriceOf));
            sum += unit * input.count;
        });
        // 한 번에 여럿 나오면(쇠붙이 부속 ×2) 한 개 값 = (합 + 1) ÷ 개수 내림 — 8.16 쇠붙이 부속 3
        return sum > 0 ? Math.max(1, Math.floor((sum + 1) / (recipe.yield || 1))) : 0;
    }
    if (FISH_TABLE[itemId]) return Math.max(1, Math.round(FISH_TABLE[itemId].intervalMinutes / SELL_PRICE.fishMinutesPerCoin));
    if (itemId === "item.wood" || itemId === "item.berry" || itemId === "item.cedar") return 0; // 8.14 나무·산딸기 못 판다
    if (CROP_HARVEST_COINS[itemId]) return Math.max(1, Math.floor(CROP_HARVEST_COINS[itemId] / SELL_PRICE.cropDivisor));
    if (/^item\.pristine_/.test(itemId)) return SELL_PRICE.premium;
    if (/_meat$/.test(itemId)) return SELL_PRICE.meat;
    if (hasCraftTag(itemId, "Hide")) return SELL_PRICE.hide;
    if (HUNT_TROPHIES.indexOf(itemId) !== -1 || itemId === "item.shed_antler") return SELL_PRICE.trophy; // 낙각 = 뿔
    // 목장 생산물(송로·우유·꿀·알) = 목장 코인 10 ÷ 5 — 작물 규칙과 같은 축(DESIGN 8.14, #0193). 털·양털은 위 Hide 줄.
    if (Object.keys(ANIMAL_PRODUCT_ITEM).some(function (a) { return ANIMAL_PRODUCT_ITEM[a] === itemId; })) {
        return Math.floor(COIN_PER_UNIT / SELL_PRICE.cropDivisor);
    }
    return LOOT_SELL_PRICE[itemId] || ORE_CHUNK_PRICE[itemId] || GATHER_SELL_PRICE[itemId] || 0; // 13.8 보석·판매용 · 8.16 덩어리 · 13.4 늪
}

/** 가중치 = 1 ÷ 낚시 시간. 산딸기 미끼는 그 자리 후보 중 낚시 시간이 중앙값 이상인 쪽(희귀 절반)에 ×1.1. */
function fishWeights(spotId, bait) {
    var habitat = FISHING_SPOTS[spotId];
    var ids = habitat === "fresh" ? FISH_HABITAT.fresh
        : habitat === "swamp" ? FISH_HABITAT.fresh.concat(FISH_HABITAT.swamp) : FISH_HABITAT.sea;
    if (habitat === "farSea") {
        ids = ids.filter(function (id) { return FISH_TABLE[id].intervalMinutes >= FAR_SEA_MIN_MINUTES; });
    }
    var minutes = ids.map(function (id) { return FISH_TABLE[id].intervalMinutes; }).sort(function (a, b) { return a - b; });
    var median = minutes[Math.floor(minutes.length / 2)];
    return ids.map(function (id) {
        var m = FISH_TABLE[id].intervalMinutes;
        var w = 1 / m;
        if (bait === "berry" && m >= median) w *= BERRY_BAIT_RARE_BONUS;
        return { itemId: id, weight: w };
    });
}

function pickWeighted(list) {
    var total = 0;
    list.forEach(function (e) { total += e.weight; });
    var roll = Math.random() * total;
    for (var i = 0; i < list.length; i++) {
        roll -= list[i].weight;
        if (roll < 0) return list[i].itemId;
    }
    return list[list.length - 1].itemId;
}

function liveDrops(drops) {
    var now = Date.now();
    return drops.filter(function (d) { return now - Date.parse(d.droppedAtUtc) < DROP_TTL_SECONDS * 1000; });
}

function recordDrop(playFabId, items) {
    var drops = liveDrops(loadData(playFabId, DROPS_DATA_KEY, []));
    var now = Date.now();
    var drop = { dropId: newId(), items: items, droppedAtUtc: new Date(now).toISOString() };
    drops.push(drop);
    saveData(playFabId, DROPS_DATA_KEY, drops);
    return { dropId: drop.dropId, items: items, expiresAtUtc: new Date(now + DROP_TTL_SECONDS * 1000).toISOString() };
}

/** `base` 를 읽으며 시간이 다 된 텐트를 지운다 — 24시간 안에 집이 못 됐거나, 집 유지가 0 이 됐거나. */
function loadBase(playFabId) {
    var now = Date.now();
    var tents = loadData(playFabId, BASE_DATA_KEY, []);
    var kept = [];
    var removed = [];
    tents.forEach(function (t) {
        var reason = null;
        if (!t.upgraded && now - Date.parse(t.placedAtUtc) >= UPGRADE_DEADLINE_HOURS * HOUR_MS) reason = "upgrade_deadline";
        if (t.upgraded && now >= Date.parse(t.upkeepUntilUtc)) reason = "upkeep_empty";
        if (reason) removed.push({ tentId: t.tentId, reason: reason });
        else kept.push(t);
    });
    if (removed.length > 0) saveData(playFabId, BASE_DATA_KEY, kept);
    return { tents: kept, removed: removed };
}

function findTent(base, tentId) {
    for (var i = 0; i < base.tents.length; i++) {
        if (base.tents[i].tentId === tentId) return base.tents[i];
    }
    for (var r = 0; r < base.removed.length; r++) {
        if (base.removed[r].tentId === tentId) return fail("expired", "텐트가 사라졌다", { reason: base.removed[r].reason });
    }
    return fail("not_found", "그 텐트가 없다");
}

/**
 * 보관함 열기 — 남의 보관함은 PIN 이 맞아야 한다(🟡 PIN 없는 보관함은 주인만).
 * `boat: true` 면 배 보관함(8.9) — 배 짐칸를 가진 주인만, 텐트와 상관없이 계정당 하나.
 */
function openStorage(args) {
    args = args || {};
    if (args.boat === true) return openBoatStorage(args);
    var ownerId = args.ownerId || currentPlayerId;
    var base = loadBase(ownerId);
    var tent = findTent(base, args.tentId);
    if (tent.error) return tent;
    var structure = null;
    tent.items.forEach(function (s) { if (s.structureId === args.structureId) structure = s; });
    if (!structure || structure.itemId !== STORAGE_ITEM) return fail("not_found", "그 보관함이 없다");
    if (ownerId !== currentPlayerId) {
        var bad = pinGuard(ownerId, structure, args.pin);
        if (bad) return bad;
    }
    return {
        ownerId: ownerId, structure: structure, slots: STORAGE_SLOTS,
        save: function () { saveData(ownerId, BASE_DATA_KEY, base.tents); }
    };
}

// 출시 점검(10-16) — PIN 무차별 대입 막기. 보관함마다(주인 계정 `pinFails`) 남이 틀린 횟수를 세고, 다 차면 잠근다(DESIGN 4.8).
var PIN_MAX_FAILS = 5;
var PIN_LOCK_MS = 10 * 60 * 1000;
var PIN_FAILS_DATA_KEY = "pinFails";

/** 남의 보관함 PIN 확인 — 맞으면 null(틀린 기록 지움), 아니면 `locked`/`pin_locked`. 잠긴 동안은 맞는 PIN 도 안 받는다. */
function pinGuard(ownerId, structure, pin) {
    var fails = loadData(ownerId, PIN_FAILS_DATA_KEY, {});
    var entry = fails[structure.structureId] || { n: 0, untilMs: 0 };
    var now = Date.now();
    if (entry.untilMs > now) return fail("pin_locked", "PIN 을 너무 많이 틀렸다 — 잠시 뒤 다시", { waitMs: entry.untilMs - now });
    if (structure.pin && String(pin) === structure.pin) {
        if (fails[structure.structureId]) {
            delete fails[structure.structureId];
            saveData(ownerId, PIN_FAILS_DATA_KEY, fails);
        }
        return null;
    }
    if (!structure.pin) return fail("locked", "PIN 이 맞지 않다"); // PIN 없는 보관함은 주인만 — 셀 것도 없다
    entry.n = entry.untilMs ? 1 : entry.n + 1; // 잠금이 풀린 뒤 첫 실패는 1부터
    entry.untilMs = 0;
    if (entry.n >= PIN_MAX_FAILS) entry.untilMs = now + PIN_LOCK_MS;
    fails[structure.structureId] = entry;
    saveData(ownerId, PIN_FAILS_DATA_KEY, fails);
    return fail("locked", "PIN 이 맞지 않다", { failsLeft: Math.max(0, PIN_MAX_FAILS - entry.n) });
}

function openBoatStorage(args) {
    if (args.ownerId && args.ownerId !== currentPlayerId) return fail("locked", "배 보관함은 주인만 연다");
    if (countOf(readInventory(currentPlayerId), BOAT_CONTAINER_ITEM) < 1) return fail("locked", "배 짐칸가 없다");
    var holder = { storage: loadData(currentPlayerId, BOAT_STORAGE_DATA_KEY, {}) };
    return {
        ownerId: currentPlayerId, structure: holder, slots: BOAT_CONTAINER_SLOTS,
        save: function () { saveData(currentPlayerId, BOAT_STORAGE_DATA_KEY, holder.storage); }
    };
}

function codexProgress(codex) {
    var categories = {};
    Object.keys(CODEX_CATEGORIES).forEach(function (name) {
        var list = CODEX_CATEGORIES[name];
        var have = list.filter(function (id) { return codex.indexOf(id) !== -1; }).length;
        categories[name] = { have: have, total: list.length, percent: Math.floor(have * 100 / list.length) };
    });
    function reached(have, total, percent) { return have * 100 >= total * percent; }
    var hunt = categories.hunt;
    var plant = categories.plant;
    var merchant = Object.keys(categories).some(function (n) {
        return CODEX_REWARD_EXCLUDED.indexOf(n) === -1 && reached(categories[n].have, categories[n].total, CODEX_MERCHANT_PERCENT);
    });
    return {
        categories: categories,
        rewards: {
            farSea: reached(categories.fishing.have, categories.fishing.total, CODEX_UNLOCK_PERCENT),
            // 🟡 「사냥+식물 50%」 을 두 카테고리를 합친 칸 수의 절반으로 읽었다
            dangerDock: reached(hunt.have + plant.have, hunt.total + plant.total, CODEX_UNLOCK_PERCENT),
            specialMerchant: merchant
        }
    };
}

// ---- 동시 요청 막기 (출시 점검 10-16, DESIGN 4.8 「처리 중 표시」) ----
// 같은 계정이 같은 지급을 동시에 두 번 부르면 둘 다 「아직 안 받음」 을 읽고 둘 다 준다(읽고-확인-쓰기 경합).
// 그래서 지급 함수는 몸을 돌기 전에 처리 중 표시(`lock<이름>` = {token, untilMs})를 먼저 쓰고 다시 읽어 내 것인지 확인한다.
// 남의 표시가 살아 있으면 `busy` — 클라는 다음 주기에 다시 부른다. 몸이 끝나면(실패·예외 포함) 표시를 지운다.
// ponytail: 내부 데이터엔 비교 후 쓰기가 없어, 두 호출의 「읽기→쓰기」 가 몇 ms 안에 정확히 겹치면 아직 둘 다 지날 수 있다.
// 그 창이 실제로 쓰이면 Entity Objects `SetObjects` 의 ExpectedProfileVersion(진짜 비교 후 쓰기)로 옮긴다.
var LOCK_TTL_MS = 15000; // CloudScript 한 번 실행 상한(10초)보다 길게 — 예외로 못 지운 표시도 이 뒤엔 풀린다
var lockRandom = Math.random; // 표 굴림(Math.random)과 따로 — 표시 토큰이 뽑기 순서를 바꾸지 않게

function withLock(name, ownerOf, handler) {
    return function (args, context) {
        var owner = ownerOf ? ownerOf(args || {}) : currentPlayerId;
        if (typeof owner !== "string" || owner.length < 1 || owner.length > 64) return handler(args, context); // 인자 오류는 몸이 거른다
        var key = "lock" + name;
        var now = Date.now();
        var held = loadData(owner, key, null);
        if (held && held.untilMs > now) return fail("busy", "같은 요청을 처리 중 — 잠시 뒤 다시", { waitMs: held.untilMs - now });
        var token = now.toString(36) + lockRandom().toString(36).slice(2, 12);
        saveData(owner, key, { token: token, untilMs: now + LOCK_TTL_MS });
        var mine = loadData(owner, key, null);
        if (!mine || mine.token !== token) return fail("busy", "같은 요청을 처리 중 — 잠시 뒤 다시", { waitMs: LOCK_TTL_MS });
        try {
            return handler(args, context);
        } finally {
            server.UpdateUserInternalData({ PlayFabId: owner, KeysToRemove: [key] });
        }
    };
}

[["GrantStarterKit", "Starter"], ["HarvestRanch", "Ranch"], ["RollGacha", "Gacha"], ["CompleteQuest", "Quest"],
    ["ClaimAttendance", "Attendance"], ["ClaimBoss", "Boss"], ["ClaimLoot", "Loot"], ["SellItem", "Loot"],
    ["ReportPlayer", "Report"], ["BlockPlayer", "Block"], ["UnblockPlayer", "Block"]
].forEach(function (pair) { handlers[pair[0]] = withLock(pair[1], null, handlers[pair[0]]); });
// 떨군 가방은 주인 계정에 있다 — 두 사람이 같은 가방을 동시에 주우면 주인 쪽 표시로 막는다.
handlers.PickupDrop = withLock("Drops", function (args) { return args.ownerId; }, handlers.PickupDrop);
