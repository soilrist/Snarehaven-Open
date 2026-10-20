// 2층 — CloudScript 핸들러 모의 실행 (docs/conventions/Verification.md 2층).
//
// **왜 여기서 도나:** 지급 판정은 PlayFab 위에서 도는 자바스크립트라 언리얼 자동화(`RunTests`)가
// 부를 수 없고, 실제 왕복(3층)은 콘솔 업로드가 사람 몫이다(BACKEND.md 배포 상태). 그래서 `server` API 를
// 가짜로 세워 핸들러를 **있는 그대로** 실행한다 — 검사용 사본을 따로 만들면 사본만 맞고 올라가는 파일은 틀린다.
//
// 돌리기: `node tools/playfab/test_cloudscript.js [검사할_스크립트_경로]`
// 두 번째 인자는 역검증용이다(일부러 깬 사본을 넣어 검사가 우는지 본다). 기본은 실제 파일.
"use strict";

const fs = require("fs");
const path = require("path");
const vm = require("vm");

const repoRoot = path.resolve(__dirname, "..", "..");
const scriptPath = process.argv[2]
    ? path.resolve(process.argv[2])
    : path.join(repoRoot, "tools", "playfab", "cloudscript_main.js");
const designPath = path.join(repoRoot, "docs", "DESIGN.md");

// ── 가짜 PlayFab (계정마다 내부 데이터·인벤토리·잔액 한 벌) ─────────────────────────
// 인벤토리는 실제처럼 인스턴스 목록이다. 지급은 1개 = 인스턴스 1개(스택 없는 카탈로그),
// 스택 아이템은 `seed(..., {stack: true})` 로 RemainingUses 를 가진 인스턴스 하나를 만든다.
function makeTitle() {
    const accounts = {};
    const calls = { grantItems: [], addCurrency: [], updateData: [], events: [], deletePlayer: [] };
    const faults = { deletePlayer: false, grantItems: false };
    let nextInstance = 1;

    function account(playFabId) {
        if (!accounts[playFabId]) {
            accounts[playFabId] = { data: {}, inventory: [], currency: {} };
        }
        return accounts[playFabId];
    }

    function findInstance(playFabId, instanceId) {
        const inv = account(playFabId).inventory;
        const index = inv.findIndex((it) => it.ItemInstanceId === instanceId);
        if (index === -1) throw new Error("없는 인스턴스: " + instanceId);
        return { inv, index };
    }

    const titleData = {};
    const server = {
        GetTitleInternalData(req) {
            const out = {};
            (req.Keys || Object.keys(titleData)).forEach((key) => {
                if (Object.prototype.hasOwnProperty.call(titleData, key)) out[key] = titleData[key];
            });
            return { Data: out };
        },
        GetUserInternalData(req) {
            const store = account(req.PlayFabId).data;
            const out = {};
            const keys = req.Keys || Object.keys(store);
            keys.forEach((key) => {
                if (Object.prototype.hasOwnProperty.call(store, key)) {
                    out[key] = { Value: store[key] };
                }
            });
            return { Data: out };
        },
        UpdateUserInternalData(req) {
            const store = account(req.PlayFabId).data;
            Object.keys(req.Data || {}).forEach((key) => {
                store[key] = req.Data[key];
            });
            const remove = req.KeysToRemove || [];
            if (remove.length > 10) throw new Error("KeysToRemove 는 10개까지(안전 한도)");
            remove.forEach((key) => delete store[key]);
            calls.updateData.push({ playFabId: req.PlayFabId, data: req.Data, keysToRemove: remove });
            return {};
        },
        GrantItemsToUser(req) {
            if (faults.grantItems) {
                // 실제 CloudScript 가 서버 API 거절 때 던지는 꼴(apiErrorInfo.apiError) — 요청 한도 초과
                const e = new Error("The script called a PlayFab API, which returned an error");
                e.apiErrorInfo = { apiError: { code: 429, error: "APIClientRequestRateLimitExceeded", errorCode: 1199 } };
                throw e;
            }
            const inv = account(req.PlayFabId).inventory;
            req.ItemIds.forEach((id) => inv.push({ ItemInstanceId: "i" + nextInstance++, ItemId: id }));
            calls.grantItems.push({ playFabId: req.PlayFabId, itemIds: req.ItemIds.slice() });
            return { ItemGrantResults: req.ItemIds.map((id) => ({ ItemId: id, Result: true })) };
        },
        RevokeInventoryItems(req) {
            if (req.Items.length > 25) throw new Error("RevokeInventoryItems 는 25개까지");
            req.Items.forEach((it) => {
                const found = findInstance(it.PlayFabId, it.ItemInstanceId);
                found.inv.splice(found.index, 1);
            });
            return {};
        },
        ConsumeItem(req) {
            const found = findInstance(req.PlayFabId, req.ItemInstanceId);
            const it = found.inv[found.index];
            if (typeof it.RemainingUses !== "number" || it.RemainingUses < req.ConsumeCount) {
                throw new Error("ConsumeItem 수량 초과");
            }
            it.RemainingUses -= req.ConsumeCount;
            if (it.RemainingUses === 0) found.inv.splice(found.index, 1);
            return { RemainingUses: it.RemainingUses };
        },
        AddUserVirtualCurrency(req) {
            const wallet = account(req.PlayFabId).currency;
            wallet[req.VirtualCurrency] = (wallet[req.VirtualCurrency] || 0) + req.Amount;
            calls.addCurrency.push({ playFabId: req.PlayFabId, currency: req.VirtualCurrency, amount: req.Amount });
            return { Balance: wallet[req.VirtualCurrency] };
        },
        SubtractUserVirtualCurrency(req) {
            const wallet = account(req.PlayFabId).currency;
            if ((wallet[req.VirtualCurrency] || 0) < req.Amount) throw new Error("잔액 부족인데 차감을 불렀다");
            wallet[req.VirtualCurrency] -= req.Amount;
            return { Balance: wallet[req.VirtualCurrency] };
        },
        DeletePlayer(req) {
            calls.deletePlayer.push(req.PlayFabId);
            if (faults.deletePlayer) throw new Error("DeletePlayer 실패(가짜)");
            delete accounts[req.PlayFabId];
            return {};
        },
        WriteTitleEvent(req) {
            calls.events.push({ name: req.EventName, body: req.Body });
            return {};
        },
        GetPlayerProfile(req) {
            if (!accounts[req.PlayFabId]) throw new Error("없는 계정: " + req.PlayFabId);
            return { PlayerProfile: { PlayerId: req.PlayFabId } };
        },
        GetUserInventory(req) {
            const acc = account(req.PlayFabId);
            return {
                Inventory: acc.inventory.map((it) => Object.assign({}, it)),
                VirtualCurrency: Object.assign({}, acc.currency)
            };
        }
    };

    const sandbox = {
        handlers: {},
        server,
        currentPlayerId: "",
        log: { info() {}, debug() {}, error() {} },
        console
    };
    vm.createContext(sandbox);
    vm.runInContext(fs.readFileSync(scriptPath, "utf8"), sandbox, { filename: scriptPath });
    // 10.8 상자 드롭은 기본으로 끈다 — 씨앗 없는 Math.random 이라 켜 두면 받기 검사가 2% 확률로 운다.
    // 상자 검사는 `crateDropChance`(스크립트 원래 값)를 다시 넣어 쓴다.
    const crateDropChance = sandbox.CRATE_DROP_CHANCE;
    sandbox.CRATE_DROP_CHANCE = 0;

    return {
        sandbox,
        crateDropChance,
        calls,
        faults,
        accounts,
        account,
        titleData,
        /** 이 계정으로 핸들러를 부른다 — `currentPlayerId` 는 PlayFab 이 호출마다 채워 주는 전역이다. */
        call(playFabId, handlerName, args) {
            sandbox.currentPlayerId = playFabId;
            const handler = sandbox.handlers[handlerName];
            if (typeof handler !== "function") {
                throw new Error("핸들러가 없다: " + handlerName);
            }
            return handler(args || {}, { playerProfile: { PlayerId: playFabId } });
        },
        seed(playFabId, itemId, count, opts) {
            const inv = account(playFabId).inventory;
            if (opts && opts.stack) {
                inv.push({ ItemInstanceId: "i" + nextInstance++, ItemId: itemId, RemainingUses: count });
            } else {
                for (let i = 0; i < count; i++) inv.push({ ItemInstanceId: "i" + nextInstance++, ItemId: itemId });
            }
        },
        count(playFabId, itemId) {
            return account(playFabId).inventory.reduce((n, it) => n + (it.ItemId === itemId
                ? (typeof it.RemainingUses === "number" ? it.RemainingUses : 1) : 0), 0);
        },
        coins(playFabId) {
            return account(playFabId).currency.SC || 0;
        },
        setCoins(playFabId, amount) {
            account(playFabId).currency.SC = amount;
        },
        gems(playFabId) {
            return account(playFabId).currency.GM || 0;
        },
        setGems(playFabId, amount) {
            account(playFabId).currency.GM = amount;
        },
        getData(playFabId, key) {
            const raw = account(playFabId).data[key];
            return raw === undefined ? undefined : JSON.parse(raw);
        },
        setData(playFabId, key, value) {
            account(playFabId).data[key] = JSON.stringify(value);
        },
        /** 저장된 `...Utc` 시각을 전부 ms 만큼 과거로 — 시간이 흐른 것과 같다. */
        age(playFabId, key, ms) {
            function walk(v) {
                if (Array.isArray(v)) return v.map(walk);
                if (v && typeof v === "object") {
                    Object.keys(v).forEach((k) => {
                        if (/Utc$/.test(k) && typeof v[k] === "string") v[k] = new Date(Date.parse(v[k]) - ms).toISOString();
                        else v[k] = walk(v[k]);
                    });
                }
                return v;
            }
            this.setData(playFabId, key, walk(this.getData(playFabId, key)));
        },
        snapshot(playFabId) {
            return JSON.stringify(account(playFabId));
        }
    };
}

// ── 설계서 읽기 ────────────────────────────────────────────────────────────────
// 기대값을 검사가 직접 들고 있지 않는 이유: 표와 스크립트가 어긋나는 순간을 잡는 것이 이 검사의 일이다.
// 표를 한 줄도 못 읽으면 그 자체로 실패다 (빈 통과 방지 — 루프#167 의 동물 표 대조와 같은 짜임).
const designLines = fs.readFileSync(designPath, "utf8").split("\n");

function designRow(label, hint) {
    const row = designLines.find((line) => line.startsWith("| " + label + " |") && (!hint || line.indexOf(hint) !== -1));
    if (!row) throw new Error("DESIGN.md 에서 '" + label + "' 줄을 못 읽었다");
    return row.split("|").slice(1, -1).map((c) => c.trim());
}

function designNumber(label, pattern, hint) {
    const value = designRow(label, hint)[1];
    const m = pattern.exec(value);
    if (!m) throw new Error("'" + label + "' 값에서 " + pattern + " 을 못 찾았다: " + value);
    return Number(m[1]);
}

function readStarterKitFromDesign() {
    const value = designRow("시작 지급 아이템")[1]; // `item.club` × 1
    const matched = /^`([a-z0-9_.]+)`\s*×\s*(\d+)$/.exec(value);
    if (!matched) {
        throw new Error("'시작 지급 아이템' 값의 꼴이 `<id>` × <수> 가 아니다: " + value);
    }
    return {
        itemId: matched[1],
        count: Number(matched[2]),
        onceOnly: designRow("지급 횟수")[1].indexOf("첫 접속 1회") !== -1
    };
}

/** 8.4 물고기 표 — 이름 → {id, heal}. heal: 숫자 · 10(`—`) · null(요리 불가). */
function readFishFromDesign(fishTable) {
    const out = {};
    designLines.forEach((line) => {
        const cells = line.split("|").slice(1, -1).map((c) => c.trim());
        const m = cells.length >= 5 && /^`(item\.[a-z_]+)`$/.exec(cells[1]);
        if (!m || !fishTable[m[1]] || !/^\d+분$/.test(cells[2])) return;
        const healCell = cells[4];
        const heal = /^\d+/.test(healCell) ? Number(/^\d+/.exec(healCell)[0])
            : healCell.startsWith("—") ? 10 : null;
        out[cells[0]] = { id: m[1], heal };
    });
    if (Object.keys(out).length === 0) throw new Error("DESIGN 8.4 물고기 표를 못 읽었다");
    return out;
}

/** 8.10 레시피 표 — 일반 줄만(씨앗·생선구이 묶음 줄은 따로 본다). */
function readRecipesFromDesign() {
    const start = designLines.findIndex((l) => l.startsWith("**레시피**"));
    if (start === -1) throw new Error("DESIGN 8.10 '레시피' 표를 못 찾았다");
    const rows = [];
    for (let i = start + 1; i < designLines.length; i++) {
        const line = designLines[i];
        if (rows.length > 0 && !line.startsWith("|")) break;
        const cells = line.split("|").slice(1, -1).map((c) => c.trim());
        const m = cells.length >= 5 && /^`(item\.[a-z_<>가-힣]+)`$/.exec(cells[1]);
        if (m) rows.push({ name: cells[0], id: m[1], inputs: cells[2], place: cells[3], why: cells[4] });
    }
    if (rows.length === 0) throw new Error("DESIGN 8.10 레시피 줄을 한 줄도 못 읽었다");
    return rows;
}

// ── 검사 틀 ────────────────────────────────────────────────────────────────────
const tests = [];
function test(name, body) {
    tests.push({ name, body });
}

function assertEqual(actual, expected, what) {
    const a = JSON.stringify(actual);
    const e = JSON.stringify(expected);
    if (a !== e) {
        throw new Error(what + " — 기대 " + e + " · 실제 " + a);
    }
}

function assertTrue(value, what) {
    if (!value) {
        throw new Error(what + " — 거짓이 나왔다");
    }
}

/** 실패 코드가 맞고, 계정이 한 글자도 안 바뀌었는지 본다(확인 → 차감 → 지급 원칙). */
function assertFailsClean(title, pid, handler, args, code, what, others) {
    const watch = [pid].concat(others || []);
    const before = watch.map((p) => title.snapshot(p));
    const result = title.call(pid, handler, args);
    assertEqual(result.code, code, what + " 실패 코드");
    assertTrue(typeof result.error === "string", what + " 사유 문구");
    watch.forEach((p, i) => assertEqual(title.snapshot(p), before[i], what + " — " + p + " 계정이 그대로다"));
    return result;
}

function withTent(title, pid) {
    title.seed(pid, "item.tent", 1);
    const placed = title.call(pid, "PlaceTent", { x: 0, y: 0 });
    assertTrue(placed.tentId, "텐트 놓기 준비");
    return placed.tentId;
}

function fillSlots(title, pid, n) {
    for (let i = 0; i < n; i++) title.seed(pid, "item.filler_" + i, 1);
}

// ══ 시작 장비 · 시작 코인 (3.5 · 8.14) ══════════════════════════════════════════
test("설계서 표와 스크립트의 시작 장비가 같다", () => {
    const design = readStarterKitFromDesign();
    assertTrue(design.onceOnly, "DESIGN 3.5 '지급 횟수' 가 첫 접속 1회");
    const title = makeTitle();
    const kit = title.sandbox.STARTER_KIT;
    assertTrue(Array.isArray(kit) && kit.length > 0, "STARTER_KIT 가 비어 있지 않다");
    assertEqual(kit.map((e) => ({ itemId: e.itemId, count: e.count })),
        [{ itemId: design.itemId, count: design.count }],
        "STARTER_KIT 가 DESIGN 3.5 표와 같다");
});

test("새 계정은 시작 장비를 한 번 받는다", () => {
    const design = readStarterKitFromDesign();
    const title = makeTitle();
    const result = title.call("player-new", "GrantStarterKit");

    assertEqual(result.granted, true, "granted");
    assertEqual(result.alreadyGranted, false, "alreadyGranted");
    assertEqual(result.items, [{ itemId: design.itemId, count: design.count }], "응답의 지급 목록");
    assertEqual(title.calls.grantItems.length, 1, "GrantItemsToUser 호출 수");
    assertEqual(title.calls.grantItems[0].itemIds, [design.itemId], "지급한 아이템 ID");
    assertEqual(title.accounts["player-new"].inventory.map((it) => it.ItemId), [design.itemId], "인벤토리에 들어간 것");
    assertTrue(typeof result.grantedAtUtc === "string" && result.grantedAtUtc.length > 0, "지급 시각이 찍힌다");
});

test("테스터 키트는 목록에 든 계정만 한 번 받고 탈것 8종이 열린다", () => {
    const title = makeTitle();
    title.titleData.testerIds = JSON.stringify(["tester"]);
    const outsider = title.call("stranger", "GrantTesterKit");
    assertEqual(outsider.code, "not_tester", "목록 밖은 거절");
    // 한 번 실행에 600개까지 — 나머지는 다음 로그인에 이어서, 다 준 뒤에만 표시
    let first = title.call("tester", "GrantTesterKit"), runs = 1;
    assertEqual(first.granted, true, "처음은 준다");
    while (first.partial && runs < 10) { first = title.call("tester", "GrantTesterKit"); runs++; }
    assertTrue(!first.partial && runs >= 2, "끊겨도 이어서 다 준다: " + runs + "번");
    const again = title.call("tester", "GrantTesterKit");
    assertEqual(again.granted, false, "다 받은 뒤엔 안 준다");
    const kit = title.sandbox.TESTER_KIT;
    const want = kit.reduce((n, e) => n + e.count, 0);
    assertEqual(title.accounts["tester"].inventory.length, want, "키트 아이템 개수");
    assertEqual(first.mounts.length, title.sandbox.MOUNT_SPECIES.length, "탈것 종 전부");
    assertEqual(title.call("tester", "GetMounts").mounts.length, title.sandbox.MOUNT_SPECIES.length, "GetMounts 도 전부");
});

test("테스터 키트에 총 11종과 탄 4종 각 200발이 들고, 총마다 맞는 탄이 함께 든다 (루프#1046)", () => {
    const sb = makeTitle().sandbox;
    const kit = {};
    sb.TESTER_KIT.forEach((e) => { kit[e.itemId] = e.count; });
    // 바주카(루프#1206)는 키트에서 뺀다 — DESIGN 「테스터 키트 v3 바주카」: 카탈로그에 `item.bazooka`·`item.rocket` 이 생긴 뒤 키트 표에 넣는다
    const guns = Object.keys(sb.GUNS || {}).filter((id) => !!sb.GUNS[id] && id !== "item.bazooka");
    assertEqual(guns.length, 11, "서버가 아는 총 수");
    guns.forEach((g) => {
        assertEqual(kit[g], 1, "키트의 총 " + g);
        assertEqual(kit[sb.GUNS[g].ammo], 200, "키트의 탄 " + sb.GUNS[g].ammo);
    });
    assertEqual(new Set(guns.map((g) => sb.GUNS[g].ammo)).size, 4, "탄 종류 수");
    assertTrue(sb.TESTER_KIT_VERSION >= 2, "키트 버전이 올랐다(이미 받은 기기도 다시 받는다)");
    // 큰 묶음은 100개씩 나눠 한 호출에 몰리지 않는다
    const t = makeTitle();
    t.titleData.testerIds = JSON.stringify(["t2"]);
    t.call("t2", "GrantTesterKit");
    assertTrue(t.calls.grantItems.every((c) => c.itemIds.length <= 100), "호출당 100개 이하");
    assertTrue(t.calls.grantItems.length <= sb.TESTER_KIT_CALLS_PER_RUN, "한 번 실행에 지급 호출 상한");
    // 꾸미기(1개짜리)가 첫 실행에 먼저 들어간다 — v3 처럼 끊겨도 꾸미기는 빠지지 않는다
    const inv1 = t.accounts["t2"].inventory.map((it) => it.ItemId);
    assertTrue(Object.keys(sb.OUTFIT_ITEMS).every((id) => inv1.indexOf(id) !== -1), "첫 실행에 옷 전부");
});

test("테스터 키트 v3 — 꾸미기 전부·도구 전부·키트 아이템 줄 중복 없음 (루프#1154)", () => {
    const sb = makeTitle().sandbox;
    const ids = sb.TESTER_KIT.map((e) => e.itemId);
    assertEqual(new Set(ids).size, ids.length, "키트에 같은 아이템 줄 중복 없음");
    assertTrue(sb.TESTER_KIT_VERSION >= 3, "키트 버전 3 이상(이미 받은 계정도 다시 받는다)");
    const want = Object.keys(sb.OUTFIT_ITEMS).concat(sb.RANCH_COSMETICS, sb.GACHA_PET_COSMETICS, Object.keys(sb.GACHA_RANCH_DECOS));
    const missing = want.filter((id) => ids.indexOf(id) === -1);
    assertEqual(missing.length, 0, "키트에 빠진 꾸미기: " + missing.join(","));
    ["item.hatchet", "item.pickaxe_basic", "item.pickaxe_advanced", "item.rod_basic", "item.rod_lucky", "item.gather_basket", "item.club"]
        .forEach((id) => assertTrue(ids.indexOf(id) !== -1, "키트의 도구 " + id));
});

test("테스터 키트 v6 — 총·탄·먹는 것·장비·설치물·꾸미기 전부가 표에서 들고, 목록 밖은 거절", () => {
    const sb = makeTitle().sandbox;
    const kit = {};
    sb.TESTER_KIT.forEach((e) => { kit[e.itemId] = e.count; });
    assertTrue(sb.TESTER_KIT_VERSION >= 6, "키트 버전 6 이상(v5 받은 계정도 다시 받는다)");
    Object.keys(sb.GUNS).forEach((g) => {
        assertEqual(kit[g], 1, "총 " + g);
        assertEqual(kit[sb.GUNS[g].ammo], sb.GUNS[g].ammo === "item.rocket" ? 30 : 200, "탄 " + sb.GUNS[g].ammo);
    });
    const edible = Object.keys(sb.RECIPES).filter((id) => sb.RECIPES[id].heal >= 0)
        .concat(Object.keys(sb.RAW_HEAL).filter((id) => sb.RAW_HEAL[id] >= 0));
    edible.forEach((id) => assertTrue(kit[id] >= 10, "먹는 것 10개 이상 " + id));
    // DT_ItemTable 의 장비·설치물·꾸미기 종류 행은 하나도 빠지지 않는다
    const kinds = ["Equipment", "Placeable", "Cosmetic", "PetCosmetic", "RanchDeco"];
    const rows = dumpItemRows().filter((r) => kinds.indexOf(r.ItemType) !== -1);
    const missing = rows.filter((r) => !(r.ItemID in kit)).map((r) => r.ItemID);
    assertEqual(missing, [], "키트에 빠진 장비·설치물·꾸미기");
    const lines = sb.TESTER_KIT.length, total = sb.TESTER_KIT.reduce((n, e) => n + e.count, 0);
    console.log("    키트 v" + sb.TESTER_KIT_VERSION + ": " + lines + "줄 · " + total + "개 · 총 " + Object.keys(sb.GUNS).length
        + " · 먹는 것 " + new Set(edible).size + " · 장비·설치물·꾸미기 행 " + rows.length);
    // 실제 지급: 목록 밖은 거절, 테스터는 이어 받아 키트 전부를 갖는다
    const t = makeTitle();
    t.titleData.testerIds = JSON.stringify(["t6"]);
    assertEqual(t.call("other", "GrantTesterKit").code, "not_tester", "목록 밖은 거절");
    let r = t.call("t6", "GrantTesterKit"), runs = 1;
    while (r.partial && runs < 10) { r = t.call("t6", "GrantTesterKit"); runs++; }
    assertEqual([r.granted, r.version], [true, sb.TESTER_KIT_VERSION], "다 준 뒤 버전 표시");
    assertEqual(t.accounts["t6"].inventory.length, total, "지급한 아이템 개수 = 키트 총합");
    const have = new Set(t.accounts["t6"].inventory.map((it) => it.ItemId));
    assertEqual(sb.TESTER_KIT.filter((e) => !have.has(e.itemId)).map((e) => e.itemId), [], "못 받은 키트 줄");
});

test("두 번째 접속에는 아무것도 안 준다", () => {
    const title = makeTitle();
    const first = title.call("player-repeat", "GrantStarterKit");
    const second = title.call("player-repeat", "GrantStarterKit");
    const third = title.call("player-repeat", "GrantStarterKit");

    assertEqual(first.granted, true, "첫 번째는 준다");
    assertEqual(second.granted, false, "두 번째는 안 준다");
    assertEqual(second.alreadyGranted, true, "이미 받았다고 답한다");
    assertEqual(third.granted, false, "세 번째도 안 준다");
    assertEqual(title.calls.grantItems.length, 1, "GrantItemsToUser 누적 호출 수");
    assertEqual(title.accounts["player-repeat"].inventory.length, 1, "인벤토리 칸 수");
    assertEqual(second.grantedAtUtc, first.grantedAtUtc, "이미 받았다는 답이 첫 지급 시각을 들고 온다");
});

test("표시는 계정마다 따로다", () => {
    const title = makeTitle();
    title.call("player-a", "GrantStarterKit");
    const other = title.call("player-b", "GrantStarterKit");

    assertEqual(other.granted, true, "다른 계정은 자기 몫을 받는다");
    assertEqual(title.calls.grantItems.length, 2, "GrantItemsToUser 누적 호출 수");
    assertEqual(title.accounts["player-a"].inventory.length, 1, "player-a 인벤토리 칸 수");
    assertEqual(title.accounts["player-b"].inventory.length, 1, "player-b 인벤토리 칸 수");
});

test("지급 표시는 내부 데이터에 남는다", () => {
    const title = makeTitle();
    title.call("player-stamp", "GrantStarterKit");
    const key = title.sandbox.STARTER_KIT_DATA_KEY;
    const raw = title.accounts["player-stamp"].data[key];

    assertTrue(typeof raw === "string" && raw.length > 0, "표시 키(" + key + ")에 값이 적혔다");
    const stamp = JSON.parse(raw);
    assertEqual(stamp.items, title.sandbox.STARTER_KIT.map((e) => ({ itemId: e.itemId, count: e.count })),
        "표시에 적힌 지급 목록");
});

test("시작 코인은 첫 접속 1회만 설계서 값만큼 준다", () => {
    const expected = designNumber("시작 코인", /\*\*(\d+)\*\*/);
    assertTrue(designRow("시작 코인")[1].indexOf("첫 접속 1회") !== -1, "DESIGN 8.14 '시작 코인' 이 첫 접속 1회");
    const title = makeTitle();
    const first = title.call("player-coin", "GrantStarterKit");
    title.call("player-coin", "GrantStarterKit");
    assertEqual(first.coins, expected, "응답의 코인");
    assertEqual(title.calls.addCurrency.map((c) => [c.currency, c.amount]), [["SC", expected]], "코인 지급 기록");
    assertEqual(title.coins("player-coin"), expected, "잔액");
});

// ══ 설계서 표 ↔ 상수 대조 ══════════════════════════════════════════════════════
test("레시피 상수가 DESIGN 8.10 표와 같다", () => {
    const title = makeTitle();
    const recipes = title.sandbox.RECIPES;
    const rows = readRecipesFromDesign();
    const names = { "나무": "item.wood", "향나무": "item.cedar", "산딸기": "item.berry", "늪꽃": "item.bog_flower", "수련": "item.water_lily",
        "배 추진기": "item.boat_motor", "소형 전동기": "item.electric_motor",
        "철 주괴": "item.iron_ingot", "쇠붙이 부속": "item.metal_parts", "무명천": "item.cotton_fabric",
        "참나무 씨앗": "item.seed_oak", "산딸기 씨앗": "item.seed_berry", "차 씨앗": "item.seed_tea", "양귀비": "item.poppy", "메리골드": "item.kingcup", "밀": "item.wheat", "철판": "item.sheet_metal", "꿀": "item.honey", "식물 기름": "item.vegetable_oil", "수지": "item.resin", "층계 짜맞춤": "item.stair_kit",
        "폴리머": "item.polymer", "회로판": "item.circuit_board", "전구": "item.light_bulb" }; // 늪꽃·수련 = 13.4 · 배 추진기 = 8.9 · 소형 전동기 = 8.11 · 철 주괴·쇠붙이 부속·철판 = 8.16 · 무명천 = 13.8 · 씨앗·꽃 = 13.3 작물(M108-05a) · 밀 = 8.2(M108-05b) · 꿀·식물 기름·수지 = 조명 재료(M108-05e) · 층계 짜맞춤 = 8.11(M108-06) · 폴리머·회로판·전구 = 8.11 전기 가구(M108-08a)
    rows.forEach((r) => { names[r.name.replace(/^★\s*/, "").replace(/\s*×\d+$/, "")] = r.id; });
    const places = { "작업대": "workbench", "고급 작업대": "workbench", "어디서나": "anywhere", "불": "fire" };
    const plain = rows.filter((r) => r.id.indexOf("<") === -1);
    assertTrue(plain.length >= 10, "일반 레시피 줄 수");

    plain.forEach((r) => {
        const got = recipes[r.id];
        assertTrue(got, "RECIPES 에 " + r.id + " 가 있다");
        const inputs = r.inputs.split(" + ").map((part) => {
            const m = /^(`([A-Za-z]+)`|(.+?)) (\d+)$/.exec(part.trim());
            if (!m) throw new Error("재료 꼴을 못 읽었다: " + part);
            if (m[2]) return { tag: m[2], count: Number(m[4]) };
            if (!names[m[3]]) throw new Error("재료 이름 → ID 를 모른다: " + m[3]);
            return { itemId: names[m[3]], count: Number(m[4]) };
        });
        assertEqual(got.inputs, inputs, r.id + " 재료");
        assertEqual(got.place, places[r.place], r.id + " 자리");
        assertEqual(!!got.advWorkbench, r.place === "고급 작업대", r.id + " 고급 작업대 필요 여부");
        const star = r.name.startsWith("★");
        assertEqual(!!got.blueprint, star, r.id + " 설계도 필요 여부");
        const yieldMatch = /×(\d+)$/.exec(r.name);
        assertEqual(got.yield || 1, yieldMatch ? Number(yieldMatch[1]) : 1, r.id + " 한 번에 나오는 수");
        if (r.place === "불" && r.id.indexOf("grilled") === -1) {
            assertEqual(got.heal, Number(/\+(\d+)/.exec(r.why)[1]), r.id + " 회복량");
        }
    });
    const from136 = readIngredients136().concat(readBuff136(), readParts138()).map((r) => r.id).concat(readCooking137().map((d) => d.id), MINING_RECIPES, ELECTRIC_RECIPES, FLOOR2_RECIPES, POWER_RECIPES); // 13.6·13.7·13.8·8.16·8.11 전력·2층은 아래 시험이 본다
    const scriptPlain = Object.keys(recipes).filter((id) => !/^item\.(seed|grilled)_/.test(id) && from136.indexOf(id) === -1);
    assertEqual(scriptPlain.sort(), plain.map((r) => r.id).sort(), "설계서에 없는 레시피가 없다");
});

test("씨앗·생선구이 레시피가 작물·8.4 회복량 열과 같다", () => {
    const title = makeTitle();
    const sb = title.sandbox;
    Object.keys(sb.CROP_HARVEST_COINS).forEach((crop) => {
        assertEqual(sb.RECIPES[sb.seedOf(crop)].inputs, [{ itemId: crop, count: 4 }], crop + " 씨앗");
    });
    const fish = readFishFromDesign(sb.FISH_TABLE);
    assertEqual(Object.keys(fish).length, Object.keys(sb.FISH_TABLE).length, "8.4 표 어종 수");
    Object.keys(fish).forEach((name) => {
        const f = fish[name];
        const recipe = sb.RECIPES["item.grilled_" + f.id.slice(5)];
        if (f.heal !== null) assertEqual(recipe && recipe.heal, f.heal, name + " 생선구이 회복량");
        // 8.4 칸이 숫자·`—` 가 아닌 어종(우파루파)은 13.7 요리 44 시험이 본다.
    });
});

// M7-07: 서버 풀(`GACHA_POOL`·`GRADE_WEIGHT`)이 공개 문서 "아이템별 확률" 표와 같은가 — FISH_TABLE ↔ 8.4 와 같은 짜임.
// 서버는 등급을 `GRADE_WEIGHT` 로 굴린 뒤 등급 안에서 균등하게 고른다 → 아이템 확률 = 등급 가중치 ÷ 그 등급 수.
test("뽑기 풀이 GACHA_DISCLOSURE 아이템별 확률과 같다", () => {
    const sb = makeTitle().sandbox;
    const gradeKey = { "커먼": "common", "레어": "rare", "에픽": "epic", "레전더리": "legendary" };
    const doc = fs.readFileSync(path.join(repoRoot, "docs", "GACHA_DISCLOSURE.md"), "utf8").split("\n")
        .map((line) => line.split("|").slice(1, -1).map((c) => c.trim()))
        .filter((cells) => cells.length === 5 && cells[0].startsWith("gacha."));
    assertTrue(doc.length > 0, "공개 문서 아이템 줄을 읽었다");
    assertEqual(sb.GACHA_POOL.map((p) => p.id + ":" + p.grade), doc.map((c) => c[0] + ":" + gradeKey[c[2]]), "풀 ID·등급·순서");
    let sum = 0;
    doc.forEach((c) => {
        const pct = Number(c[4].replace("%", ""));
        const grade = gradeKey[c[2]];
        const served = sb.GRADE_WEIGHT[grade] / sb.GACHA_POOL.filter((p) => p.grade === grade).length;
        assertTrue(Math.abs(served - pct) < 0.05, c[0] + " 서버 확률 " + served + " = 문서 " + pct);
        sum += pct;
    });
    assertTrue(Math.abs(sum - 100) < 0.05, "문서 확률 합 100% (실제 " + sum + ")");
});

// M35-01: `RollGacha` 를 실제로 부른다 — 비용·거절·천장·중복 환급을 공개 문서(GACHA_DISCLOSURE 14~16)·DESIGN 10.3 중복 줄과 맞댄다.
// 기대값은 문서에서 읽는다(검사가 숫자를 들고 있으면 문서와 서버가 같이 틀려도 통과한다).
function readGachaRules() {
    const doc = fs.readFileSync(path.join(repoRoot, "docs", "GACHA_DISCLOSURE.md"), "utf8");
    const cost = /1회 젬 (\d+) 또는 스네어 코인 (\d+) \/ 10연차 젬 (\d+)/.exec(doc);
    const pity = /천장: (\d+)회 누적/.exec(doc);
    const dup = /커먼 (\d+) \/ 레어 (\d+) \/ 에픽 (\d+) \/ 레전더리 ([\d,]+)/.exec(designRow("중복 처리")[1]);
    if (!cost || !pity || !dup) throw new Error("가챠 비용·천장·중복 환급 줄을 못 읽었다");
    return {
        singleGM: Number(cost[1]), singleSC: Number(cost[2]), multiGM: Number(cost[3]), pity: Number(pity[1]),
        refund: { common: Number(dup[1]), rare: Number(dup[2]), epic: Number(dup[3]), legendary: Number(dup[4].replace(/,/g, "")) }
    };
}

test("뽑기 RollGacha — 1회 젬·1회 코인·10연차 비용이 공개 문서대로 빠지고 결과 수 = 뽑기 수", () => {
    const rules = readGachaRules();
    const title = makeTitle();
    setRandom(title, 0); // 등급 = 커먼(0 < 70), 등급 안 첫 경품 — 결과를 고정
    title.setGems("p", 1000);
    title.setCoins("p", 600);
    const one = title.call("p", "RollGacha", { currency: "GM" });
    assertEqual([title.gems("p"), one.results.length], [1000 - rules.singleGM, 1], "젬 1회 비용·결과 1줄");
    assertEqual(one.results[0], { itemId: "gacha.pet_ribbon", grade: "common", duplicate: false }, "첫 결과");
    const sc = title.call("p", "RollGacha", { currency: "SC" });
    assertEqual([title.coins("p"), sc.results.length], [600 - rules.singleSC + rules.refund.common, 1], "코인 1회 비용(중복 환급 더함)·결과 1줄");
    const before = title.gems("p");
    const ten = title.call("p", "RollGacha", { currency: "GM", rollCount: 10 });
    assertEqual([title.gems("p"), ten.results.length], [before - rules.multiGM, 10], "10연차 비용·결과 10줄");
    assertEqual(ten.pity, 12, "천장 셈 = 뽑은 횟수(레전더리 0)");
});

test("뽑기 RollGacha — 10연차 코인·잘못된 재화·재화 부족은 거절하고 계정 그대로", () => {
    const rules = readGachaRules();
    const title = makeTitle();
    title.setGems("p", rules.multiGM - 1);
    title.setCoins("p", rules.singleSC - 1);
    const cases = [
        [{ currency: "SC", rollCount: 10 }, "10연차 코인"],
        [{ currency: "XX" }, "없는 재화"],
        [{ currency: "GM", rollCount: 10 }, "젬 부족 10연차"],
        [{ currency: "SC" }, "코인 부족 1회"]
    ];
    cases.forEach(([args, what]) => {
        const before = title.snapshot("p");
        const result = title.call("p", "RollGacha", args);
        assertTrue(typeof result.error === "string" && !result.results, what + " 거절");
        assertEqual(title.snapshot("p"), before, what + " — 계정이 그대로다");
    });
    assertEqual(title.calls.grantItems.length + title.calls.addCurrency.length, 0, "거절에 지급 0");
});

test("뽑기 RollGacha — 천장 째 레전더리 확정 후 0, 중복은 등급 환급 코인", () => {
    const rules = readGachaRules();
    const title = makeTitle();
    setRandom(title, 0); // 굴림은 늘 커먼 — 레전더리는 천장으로만 나온다
    title.setGems("p", 100000);
    title.account("p").data.gachaPity = String(rules.pity - 2);
    const r = title.call("p", "RollGacha", { currency: "GM", rollCount: 10 });
    assertEqual(r.results.map((x) => x.grade).slice(0, 3), ["common", "legendary", "common"], "천장 째(" + rules.pity + ") 레전더리");
    assertEqual(r.pity, 8, "레전더리 뒤 셈 0 에서 다시");
    assertEqual(title.account("p").data.gachaPity, "8", "저장된 천장 셈");
    // 커먼은 늘 같은 경품이라 첫 하나 뒤 8번 겹친다 → 8번은 커먼 환급, 아이템은 레전더리 1개 + 커먼 1개
    const dups = r.results.filter((x) => x.duplicate);
    assertEqual(dups.length, 8, "중복 수(커먼 9 중 첫 하나 빼고 8)");
    dups.forEach((x) => assertEqual(x.refund, rules.refund[x.grade], x.itemId + " 환급"));
    assertEqual(title.coins("p"), 8 * rules.refund.common, "환급 코인 합");
    assertEqual(title.account("p").inventory.map((it) => it.ItemId).sort(), ["gacha.pet_gold_mane", "gacha.pet_ribbon"], "아이템은 겹치지 않는 둘만");
    // 이미 가진 레전더리가 천장에서 다시 나오면 레전더리 환급
    title.account("p").data.gachaPity = String(rules.pity - 1);
    const again = title.call("p", "RollGacha", { currency: "GM" });
    assertEqual(again.results[0], { itemId: "gacha.pet_gold_mane", grade: "legendary", duplicate: true, refund: rules.refund.legendary }, "레전더리 중복 환급");
});

// M35-02: 재화 환급 경품은 공개 문서 「스네어 코인 xN」 그대로 코인으로 나가고 아이템·중복 환급이 없다.
test("뽑기 RollGacha — 재화 환급 경품은 문서 수량만큼 코인, 인벤토리 변화 0", () => {
    const sb = makeTitle().sandbox;
    const doc = fs.readFileSync(path.join(repoRoot, "docs", "GACHA_DISCLOSURE.md"), "utf8").split("\n")
        .map((line) => line.split("|").slice(1, -1).map((c) => c.trim()))
        .filter((cells) => cells.length === 5 && cells[3] === "재화 환급");
    assertEqual(doc.length, 3, "공개 문서 재화 환급 줄 3개");
    const docCoins = {};
    doc.forEach((c) => { docCoins[c[0]] = Number(/스네어 코인 x(\d+)/.exec(c[1])[1]); });
    assertEqual(Object.keys(sb.GACHA_COIN_PRIZES).sort(), Object.keys(docCoins).sort(), "코인 경품 ID = 문서");
    Object.keys(docCoins).forEach((id) => assertEqual(sb.GACHA_COIN_PRIZES[id], docCoins[id], id + " 코인 = 문서"));
    // 등급마다 그 등급의 환급 경품을 두 번 뽑는다 — 두 번째도 중복이 아니라 다시 코인이다.
    Object.keys(docCoins).forEach((id) => {
        const grade = sb.GACHA_POOL.filter((p) => p.id === id)[0].grade;
        const pool = sb.GACHA_POOL.filter((p) => p.grade === grade);
        const roll = { common: 0, rare: 0.75, epic: 0.95 }[grade]; // 70 / 22 / 6.5 누적 구간 안
        const pick = (pool.map((p) => p.id).indexOf(id) + 0.5) / pool.length;
        const t = makeTitle();
        vm.runInContext("(function(){var q=" + JSON.stringify([roll, pick, roll, pick]) + ";Math.random=function(){return q.shift();};})();", t.sandbox);
        t.setGems("q", 200);
        const a = t.call("q", "RollGacha", { currency: "GM" });
        const b = t.call("q", "RollGacha", { currency: "GM" });
        assertEqual([a.results[0], b.results[0]], [
            { itemId: id, grade: grade, duplicate: false, coins: docCoins[id] },
            { itemId: id, grade: grade, duplicate: false, coins: docCoins[id] }
        ], id + " 결과 줄 두 번 다 코인");
        assertEqual([t.coins("q"), t.count("q", id), t.account("q").inventory.length, t.calls.grantItems.length],
            [2 * docCoins[id], 0, 0, 0], id + " 코인 +" + (2 * docCoins[id]) + " · 인벤토리 변화 0");
    });
});

// M35-03: 희귀 소재 경품은 공개 문서 「이름 xN」 수량만큼 쌓이고, 제작에서 1개 = 재료 1개로 그만큼 쓰인다.
test("뽑기 RollGacha — 희귀 소재 경품은 문서 수량만큼 쌓이고 제작에서 그만큼 쓰인다", () => {
    const sb = makeTitle().sandbox;
    const doc = fs.readFileSync(path.join(repoRoot, "docs", "GACHA_DISCLOSURE.md"), "utf8").split("\n")
        .map((line) => line.split("|").slice(1, -1).map((c) => c.trim()))
        .filter((cells) => cells.length === 5 && cells[3] === "희귀 소재");
    assertEqual(doc.length, 5, "공개 문서 희귀 소재 줄 5개");
    const docCount = {};
    doc.forEach((c) => { docCount[c[0]] = Number(/ x(\d+)$/.exec(c[1])[1]); });
    assertEqual(Object.keys(sb.GACHA_MATERIAL_PRIZES).sort(), Object.keys(docCount).sort(), "소재 경품 ID = 문서");
    // 등급마다 그 소재를 두 번 뽑는다 — 두 번째는 중복 환급이 아니라 N 개 더 쌓인다.
    Object.keys(docCount).forEach((id) => {
        const grade = sb.GACHA_POOL.filter((p) => p.id === id)[0].grade;
        const pool = sb.GACHA_POOL.filter((p) => p.grade === grade);
        const roll = { common: 0, rare: 0.75, epic: 0.95, legendary: 0.995 }[grade]; // 70 / 22 / 6.5 / 1.5 누적 구간 안
        const pick = (pool.map((p) => p.id).indexOf(id) + 0.5) / pool.length;
        const t = makeTitle();
        vm.runInContext("(function(){var q=" + JSON.stringify([roll, pick, roll, pick]) + ";Math.random=function(){return q.shift();};})();", t.sandbox);
        t.setGems("q", 200);
        const a = t.call("q", "RollGacha", { currency: "GM" });
        assertEqual([a.results[0], t.count("q", id)], [{ itemId: id, grade: grade, duplicate: false, count: docCount[id] }, docCount[id]], id + " 한 번 = " + docCount[id] + "개");
        const b = t.call("q", "RollGacha", { currency: "GM" });
        assertEqual([b.results[0].duplicate, t.count("q", id), t.coins("q")], [false, 2 * docCount[id], 0], id + " 두 번 = " + (2 * docCount[id]) + "개 · 환급 0");
    });
    // 뽑은 수량이 제작에서 그대로 재료 수가 된다: 가죽 x5 → 그릴(Hide 2) 뒤 3 · 나무 x5 → 판자(나무 4) 뒤 1 · 발톱 x1 + 여우 털 1 → 그릴
    const t = makeTitle();
    withTent(t, "p");
    t.setData("p", "blueprints", ["item.blueprint_grill"]);
    t.seed("p", "item.plank", 12);
    t.seed("p", "gacha.mat_hide_5", docCount["gacha.mat_hide_5"]);
    assertEqual(t.call("p", "CraftItem", { recipeId: "item.grill" }).crafted, "item.grill", "가챠 가죽만으로 그릴");
    assertEqual(t.count("p", "gacha.mat_hide_5"), docCount["gacha.mat_hide_5"] - 2, "가죽 조각 2개 쓰임");
    t.seed("p", "gacha.mat_wood_5", docCount["gacha.mat_wood_5"]);
    assertEqual(t.call("p", "CraftItem", { recipeId: "item.plank" }).crafted, "item.plank", "나무 조각으로 판자");
    assertEqual(t.count("p", "gacha.mat_wood_5"), docCount["gacha.mat_wood_5"] - 4, "나무 조각 4개 쓰임");
    t.seed("p", "item.wood", 4);
    t.call("p", "CraftItem", { recipeId: "item.plank" });
    assertEqual([t.count("p", "item.wood"), t.count("p", "gacha.mat_wood_5")], [0, docCount["gacha.mat_wood_5"] - 4], "나무가 있으면 나무 먼저");
    const u = makeTitle();
    withTent(u, "p");
    u.setData("p", "blueprints", ["item.blueprint_grill"]);
    u.seed("p", "item.plank", 6);
    u.seed("p", "item.fox_fur", 1);
    u.seed("p", "gacha.mat_legend_claw_1", docCount["gacha.mat_legend_claw_1"]);
    assertEqual(u.call("p", "CraftItem", { recipeId: "item.grill" }).crafted, "item.grill", "발톱이 가죽 자리 하나");
    assertEqual([u.count("p", "item.fox_fur"), u.count("p", "gacha.mat_legend_claw_1")], [0, 0], "여우 털·발톱 다 쓰임");
});

// 보정 10-06: 희귀 소재 개당 판매가 = DESIGN 10.2 「희귀 소재 판매가」 줄(문서 이름 → 공개 문서 ID) — 묶음 합이 등급 코인과 같고,
// 가챠 아이템이 배낭 밖이어도 SellItem 이 받아 SC 가 는다.
test("SellItem — 가챠 희귀 소재 5종은 DESIGN 10.2 판매가대로 팔린다", () => {
    const title = makeTitle();
    const sb = title.sandbox;
    const row = fs.readFileSync(path.join(repoRoot, "docs", "DESIGN.md"), "utf8").split("\n")
        .filter((line) => line.startsWith("| 희귀 소재 판매가 |"));
    assertEqual(row.length, 1, "DESIGN 10.2 판매가 줄 하나");
    const nameToId = {};
    fs.readFileSync(path.join(repoRoot, "docs", "GACHA_DISCLOSURE.md"), "utf8").split("\n")
        .map((line) => line.split("|").slice(1, -1).map((c) => c.trim()))
        .filter((cells) => cells.length === 5 && cells[3] === "희귀 소재")
        .forEach((c) => { nameToId[c[1].replace(/ x\d+$/, "")] = c[0]; });
    const want = {};
    row[0].split("|")[2].replace(/\(.*$/, "").split("·").forEach((part) => {
        const m = /([^—*]+?)\s*\*\*([\d,]+)\*\*/.exec(part);
        if (m) want[nameToId[m[1].trim()]] = Number(m[2].replace(/,/g, ""));
    });
    assertEqual(Object.keys(want).sort(), Object.keys(sb.GACHA_MATERIAL_PRIZES).sort(), "판매가 5종 = 소재 경품 ID");
    const refund = { common: 50, rare: 150, epic: 400, legendary: 1000 };
    Object.keys(want).forEach((id) => {
        assertEqual(sb.sellPriceOf(id), want[id], id + " 판매가");
        const grade = sb.GACHA_POOL.filter((p) => p.id === id)[0].grade;
        assertEqual(want[id] * sb.GACHA_MATERIAL_PRIZES[id], refund[grade], id + " 묶음 합 = " + grade + " 코인");
        assertEqual(sb.GRADE_DUP_REFUND[grade], refund[grade], grade + " 중복 환급 그대로");
        const before = title.coins("p");
        title.seed("p", id, 2);
        const r = title.call("p", "SellItem", { itemId: id, count: 2 });
        assertEqual([r.coins, title.coins("p") - before, title.count("p", id)], [want[id] * 2, want[id] * 2, 0], id + " 2개 팔면 SC +" + want[id] * 2);
    });
});

// M35-07: 경품 19종 하나하나를 실제로 뽑아 「문서 수량 = 받은 수」 와 「받은 것을 쓰는 곳 하나 이상」 을 단정한다.
// 쓰는 곳 = 코인(잔액) · 제작(`Hide` 태그·나무 대체품으로 `CraftItem` 성공) · 펫 꾸미기(`SetPetCosmetic`) · 목장 데코(`SetRanchDeco`).
// 데코 서버 표가 없으면(명세만) 그 경품은 대기 목록으로 세고 실패로 치지 않는다(M33-05 `ItemUses` 와 같은 길).
test("뽑기 RollGacha — 경품 19종 모두 문서 수량대로 받고 쓰는 곳이 하나 이상", () => {
    const sb = makeTitle().sandbox;
    const doc = fs.readFileSync(path.join(repoRoot, "docs", "GACHA_DISCLOSURE.md"), "utf8").split("\n")
        .map((line) => line.split("|").slice(1, -1).map((c) => c.trim()))
        .filter((cells) => cells.length === 5 && /^gacha\./.test(cells[0]));
    assertEqual(doc.length, 19, "공개 문서 경품 줄 19개");
    assertEqual(sb.GACHA_POOL.map((p) => p.id).sort(), doc.map((c) => c[0]).sort(), "서버 풀 ID = 문서");
    const grantOf = (name) => { const m = / x(\d+)$/.exec(name); return m ? Number(m[1]) : 1; }; // 「xN」 없으면 1개
    const woodUse = { "item.wood": "item.plank" }; // 대체되는 재료 → 그 재료를 쓰는 레시피
    const missing = [];
    const pending = [];
    const used = {};
    doc.forEach(([id, name, , category]) => {
        const grade = sb.GACHA_POOL.filter((p) => p.id === id)[0].grade;
        const pool = sb.GACHA_POOL.filter((p) => p.grade === grade);
        const roll = { common: 0, rare: 0.75, epic: 0.95, legendary: 0.995 }[grade];
        const pick = (pool.map((p) => p.id).indexOf(id) + 0.5) / pool.length;
        const t = makeTitle();
        vm.runInContext("(function(){var q=" + JSON.stringify([roll, pick]) + ";var r=Math.random;Math.random=function(){return q.length?q.shift():r();};})();", t.sandbox); // 뒤 텐트 ID 등은 진짜 난수
        t.setGems("q", 200);
        const want = grantOf(name);
        const got = t.call("q", "RollGacha", { currency: "GM" }).results[0];
        if (!got || got.itemId !== id) { missing.push(id + "(뽑힘 " + (got && got.itemId) + ")"); return; }
        const s = t.sandbox;
        if (s.GACHA_COIN_PRIZES[id]) {
            if (t.coins("q") !== want || t.count("q", id) !== 0) missing.push(id + "(코인 " + t.coins("q") + " ≠ " + want + ")");
            else used[id] = "코인";
            return;
        }
        if (t.count("q", id) !== want) { missing.push(id + "(받음 " + t.count("q", id) + " ≠ " + want + ")"); return; }
        if (category === "목장 데코" && !s.GACHA_RANCH_DECOS) { pending.push(id); return; }
        const uses = [];
        if (s.hasCraftTag(id, "Hide")) {
            withTent(t, "q");
            t.setData("q", "blueprints", ["item.blueprint_grill"]);
            t.seed("q", "item.plank", 12);
            if (want < 2) t.seed("q", "item.fox_fur", 2 - want);
            if (t.call("q", "CraftItem", { recipeId: "item.grill" }).crafted === "item.grill" && t.count("q", id) < want) uses.push("제작 Hide");
        }
        Object.keys(s.GACHA_SUBSTITUTES).filter((k) => s.GACHA_SUBSTITUTES[k] === id).forEach((k) => {
            withTent(t, "q");
            const before = t.count("q", id);
            if (woodUse[k] && t.call("q", "CraftItem", { recipeId: woodUse[k] }).crafted === woodUse[k] && t.count("q", id) < before) uses.push("제작 " + k + " 대체");
        });
        if (s.PET_COSMETICS.indexOf(id) !== -1) {
            ranchWithPet(t, "q");
            if (t.call("q", "SetPetCosmetic", { slot: 0, itemId: id }).cosmetic === id) uses.push("펫 꾸미기");
        }
        if (s.GACHA_RANCH_DECOS && s.GACHA_RANCH_DECOS[id]) {
            const r = t.call("q", "SetRanchDeco", { itemId: id });
            if (r.itemId === id && t.count("q", id) === want) uses.push("목장 데코 " + r.kind);
        }
        if (uses.length === 0) missing.push(id + "(쓰는 곳 없음)");
        else used[id] = uses.join("+");
    });
    console.log("    [GachaPrizes] 받고 쓰임 " + Object.keys(used).length + " · 대기(데코 명세뿐) " + pending.length + " · 없음 " + missing.length
        + (missing.length ? " — " + missing.join(", ") : ""));
    assertEqual(missing, [], "문서 수량대로 못 받거나 쓰는 곳 없는 경품");
    assertEqual(Object.keys(used).length + pending.length, 19, "19종 모두 셈");
});

test("서식지·낚시 자리가 DESIGN 8.13 과 같다", () => {
    const title = makeTitle();
    const sb = title.sandbox;
    const fish = readFishFromDesign(sb.FISH_TABLE);
    const value = designRow("서식지")[1];
    [["민물", "fresh"], ["바다", "sea"], ["늪", "swamp"]].forEach(([label, key]) => {
        const m = new RegExp(label + " (\\d+)\\*\\*\\(([^)]*)\\)").exec(value);
        assertTrue(m, label + " 목록을 읽었다");
        const ids = m[2].split("·").map((n) => fish[n.trim()] && fish[n.trim()].id);
        assertEqual(ids.length, Number(m[1]), label + " 수");
        assertEqual(ids.slice().sort(), sb.FISH_HABITAT[key].slice().sort(), label + " 어종");
    });
    // 늪 자리 = 민물 + 늪 어종, 늪 어종은 다른 자리에 없다
    const ofSpot = (spot) => sb.fishWeights(spot, "worm").map((w) => w.itemId).sort();
    assertEqual(ofSpot("swamp_1"), sb.FISH_HABITAT.fresh.concat(sb.FISH_HABITAT.swamp).sort(), "늪 자리 후보");
    ["pond_1", "river_1", "coast_1", "far_1"].forEach((spot) =>
        assertTrue(ofSpot(spot).indexOf("item.eel") === -1, spot + " 에 장어 없음"));
    assertEqual(sb.FAR_SEA_MIN_MINUTES, Number(/(\d+)분 이상/.exec(value)[1]), "먼바다 최소 낚시 시간");
    const spots = designRow("자리", "연못")[1];
    const want = {
        fresh: Number(/연못 (\d+)/.exec(spots)[1]) + Number(/강 (\d+)/.exec(spots)[1]),
        sea: Number(/해안 (\d+)/.exec(spots)[1]),
        farSea: Number(/먼바다 (\d+)/.exec(spots)[1]),
        swamp: Number(/늪 물웅덩이 (\d+)/.exec(spots)[1])
    };
    const got = { fresh: 0, sea: 0, farSea: 0, swamp: 0 };
    Object.keys(sb.FISHING_SPOTS).forEach((s) => { got[sb.FISHING_SPOTS[s]] += 1; });
    assertEqual(got, want, "서식지별 포인트 수");
});

test("잡화점 바주카·로켓 묶음과 재장전 표가 DESIGN 8.6-c 와 같다 (루프#1206)", () => {
    const sb = makeTitle().sandbox;
    assertEqual([sb.SHOP_ITEMS["item.bazooka"].price, sb.SHOP_ITEMS["item.bazooka"].count], [300, 1], "바주카 값·개수");
    assertEqual([sb.SHOP_ITEMS["item.rocket"].price, sb.SHOP_ITEMS["item.rocket"].count], [20, 4], "로켓 묶음 값·개수(발당 5)");
    assertEqual([sb.GUNS["item.bazooka"].magazine, sb.GUNS["item.bazooka"].ammo], [1, "item.rocket"], "바주카 탄창·탄");
});

test("상점 목록·선착장 삯이 DESIGN 8.14 와 같다", () => {
    const title = makeTitle();
    const sb = title.sandbox;
    const want = designRow("사는 목록")[1].split(" · ").map((part) => {
        const m = /^(.+) (\d+)$/.exec(part.trim());
        return m[1] + "=" + m[2];
    }).sort();
    const seeds = readCrops133();
    const from136 = readIngredients136().concat(readBuff136()).map((r) => r.id).concat(readHats135().map((r) => r.id), readTools135().map((r) => r.id), readPaints138().ids, readOutfits13().ids, ["item.pickaxe_basic", "item.pickaxe_electric", "item.crate_key", "item.pistol_semi", "item.ammo", "item.ammo_rifle", "item.shotgun", "item.ammo_shotgun", "item.crossbow", "item.arrow", "item.pistol_auto", "item.smg", "item.rifle_burst", "item.rifle_semi", "item.shotgun_auto", "item.shotgun_short", "item.mg", "item.bazooka", "item.rocket"], ELECTRIC_SHOP); // 8.16 곡괭이·8.11 전자 재료·10.8 열쇠·8.6-c 총은 아래 시험
    const got = Object.keys(sb.SHOP_ITEMS).filter((id) => !seeds.some((c) => c.seed === id) && from136.indexOf(id) === -1)
        .map((id) => sb.SHOP_ITEMS[id].name + "=" + sb.SHOP_ITEMS[id].price).sort();
    assertEqual(got, want, "사는 목록 이름=값");
    // 13.3 「씨앗 파는 곳」 — 24종 전부, 값 = 그 작물 판매가(분별 값은 표 줄에서 읽는다)
    const seedRow = designRow("씨앗 파는 곳")[1];
    assertEqual(Number(/(\d+)종/.exec(seedRow)[1]), seeds.length, "씨앗 종 수");
    seeds.forEach((c) => {
        const want = Number(new RegExp(c.minutes + "분 (\\d+)").exec(seedRow)[1]);
        assertEqual([sb.SHOP_ITEMS[c.seed].name, sb.SHOP_ITEMS[c.seed].price, sb.SHOP_ITEMS[c.seed].count],
            [c.name + " 씨앗", want, 1], c.seed + " 상점 줄");
        const noSell = c.item === "item.berry" || c.item === "item.wood"; // 8.14 나무·산딸기 못 판다가 이긴다
        assertEqual(sb.sellPriceOf(c.item), noSell ? 0 : want, c.item + " 판매가 = 씨앗 값");
        assertEqual(sb.sellPriceOf(c.seed), 0, c.seed + " 산 씨앗은 못 판다");
    });
    assertEqual(sb.SHOP_ITEMS["item.worm_bait"].count, 3, "지렁이 팩 ×3 (8.13 미끼 줄)");
    const ferry = designRow("선착장")[1];
    assertEqual(sb.FERRY_FARES.hunt, Number(/사냥존 \*\*(\d+) 코인/.exec(ferry)[1]), "사냥존 삯");
    assertEqual(sb.FERRY_FARES.danger, Number(/위험구역 \*\*(\d+) 코인/.exec(ferry)[1]), "위험구역 삯");
    assertEqual(sb.FERRY_FARES.base, ferry.indexOf("돌아올 땐 무료") !== -1 ? 0 : -1, "돌아오기 무료");
    assertEqual(sb.FERRY_FARES.island, Number(/섬 입장 \*\*(\d+) 코인/.exec(ferry)[1]), "섬(숲 칸) 입장료");
});

test("잡화점 권총·탄약 묶음이 DESIGN 8.6-c 와 같다", () => {
    const sb = makeTitle().sandbox;
    const pistol = designNumber("권총 잡화점 값", /(\d+) 코인/);
    const ammo = /(\d+)발 = (\d+) 코인/.exec(designRow("권총탄 `item.ammo` 묶음")[1]);
    assertEqual([sb.SHOP_ITEMS["item.pistol_semi"].price, sb.SHOP_ITEMS["item.pistol_semi"].count], [pistol, 1], "권총 값·개수");
    assertEqual([sb.SHOP_ITEMS["item.ammo"].price, sb.SHOP_ITEMS["item.ammo"].count], [Number(ammo[2]), Number(ammo[1])], "탄약 묶음 값·발 수");
    assertEqual(sb.sellPriceOf("item.pistol_semi"), 0, "산 권총은 못 판다");
    assertEqual(sb.sellPriceOf("item.ammo"), 0, "산 탄약은 못 판다");
    ["ammo_rifle", "ammo_shotgun", "arrow"].forEach((id) => {
        const m = /(\d+)발 = (\d+) 코인/.exec(designRow("탄 `item." + id + "` 묶음")[1]);
        assertEqual([sb.SHOP_ITEMS["item." + id].price, sb.SHOP_ITEMS["item." + id].count], [Number(m[2]), Number(m[1])], id + " 묶음 값·발 수");
        assertEqual(sb.sellPriceOf("item." + id), 0, id + " 산 탄은 못 판다");
    });
});

// M39-08a 신식 소총 — 탄창 = 8.6-c 소총 줄, 탄약은 소총탄 item.ammo_rifle. 고급 작업대 제작이라 상점에 없고 판매가 = 재료 합 + 1.
test("LoadAmmo 소총 — 빈 탄창이면 5발, 상점에 없고 판매가는 재료 합 + 1", () => {
    const magazine = Number(/탄창 (\d+)/.exec(designRow("신식 소총 `item.rifle_modern`(M39-08)")[1])[1]);
    const title = makeTitle();
    const sb = title.sandbox;
    title.seed("p", "item.rifle_modern", 1);
    title.seed("p", "item.ammo_rifle", 25);
    assertEqual(title.call("p", "LoadAmmo", { gun: "item.rifle_modern", have: 0 }),
        { gun: "item.rifle_modern", loaded: magazine, removed: magazine, left: 25 - magazine }, "소총 빈 탄창 재장전");
    assertEqual(title.call("p", "LoadAmmo", { gun: "item.rifle_modern", have: 99 }).loaded, magazine, "have 99 → 탄창으로 자름");
    assertTrue(!sb.SHOP_ITEMS["item.rifle_modern"], "소총은 상점에 안 판다");
    const r = sb.RECIPES["item.rifle_modern"];
    const sum = r.inputs.reduce((acc, i) => acc + sb.sellPriceOf(i.itemId) * i.count, 0);
    assertEqual(sb.sellPriceOf("item.rifle_modern"), sum + 1, "소총 판매가 = 재료 합 + 1");
    assertEqual(sb.sellPriceOf("item.rifle_modern"), Number(/= (\d+) — 권총/.exec(designRow("신식 소총")[4])[1]), "판매가 = 8.10 근거 칸");
});

// 산탄총 — 탄창 = 8.6-c 산탄총 줄, 탄약은 산탄 item.ammo_shotgun. 잡화점에서 판다.
test("LoadAmmo 산탄총 — 빈 탄창이면 6발, 산탄 아닌 탄은 안 빠진다", () => {
    const magazine = Number(/탄창 (\d+)/.exec(designRow("산탄총 `item.shotgun`")[1])[1]);
    const title = makeTitle();
    const sb = title.sandbox;
    assertEqual([sb.SHOP_ITEMS["item.shotgun"].price, sb.SHOP_ITEMS["item.shotgun"].count],
        [Number(/(\d+) 코인/.exec(designRow("산탄총 잡화점 값")[1])[1]), 1], "산탄총 값·개수");
    title.seed("p", "item.shotgun", 1);
    title.seed("p", "item.ammo", 25, { stack: true });
    title.seed("p", "item.ammo_shotgun", 16, { stack: true });
    assertEqual(title.call("p", "LoadAmmo", { gun: "item.shotgun", have: 0 }),
        { gun: "item.shotgun", loaded: magazine, removed: magazine, left: 16 - magazine }, "산탄총 빈 탄창 재장전");
});

// 석궁 — 탄창 1, 탄약은 화살 item.arrow. 잡화점에서 판다.
test("LoadAmmo 석궁 — 빈 탄창이면 1발, 화살 아닌 탄은 안 빠진다", () => {
    const magazine = Number(/탄창 (\d+)/.exec(designRow("석궁 `item.crossbow`")[1])[1]);
    const title = makeTitle();
    const sb = title.sandbox;
    assertEqual([sb.SHOP_ITEMS["item.crossbow"].price, sb.SHOP_ITEMS["item.crossbow"].count],
        [Number(/(\d+) 코인/.exec(designRow("석궁 잡화점 값")[1])[1]), 1], "석궁 값·개수");
    title.seed("p", "item.crossbow", 1);
    title.seed("p", "item.ammo", 25, { stack: true });
    title.seed("p", "item.arrow", 15, { stack: true });
    assertEqual(title.call("p", "LoadAmmo", { gun: "item.crossbow", have: 0 }),
        { gun: "item.crossbow", loaded: magazine, removed: magazine, left: 15 - magazine }, "석궁 빈 탄창 재장전");
});

// 연발 권총·기관단총 — 탄약은 권총탄 item.ammo. 잡화점에서 판다.
test("LoadAmmo 연발 권총·기관단총·점사·반자동 소총·자동·짧은 산탄총·기관총 — 빈 탄창이면 탄창만큼 (ammo 인자가 맞는 탄)", () => {
    [["pistol_auto", "연발 권총", "item.ammo"], ["smg", "기관단총", "item.ammo"], ["rifle_burst", "점사 소총", "item.ammo_rifle"], ["rifle_semi", "반자동 소총", "item.ammo_rifle"], ["shotgun_auto", "연사 산탄총", "item.ammo_shotgun"], ["shotgun_short", "짧은 산탄총", "item.ammo_shotgun"], ["mg", "기관총", "item.ammo_rifle"]].forEach(([id, label, ammo]) => {
        const magazine = Number(/탄창 (\d+)/.exec(designRow(label + " `item." + id + "`")[1])[1]);
        const title = makeTitle();
        const sb = title.sandbox;
        assertEqual([sb.SHOP_ITEMS["item." + id].price, sb.SHOP_ITEMS["item." + id].count],
            [Number(/(\d+) 코인/.exec(designRow(label + " 잡화점 값")[1])[1]), 1], label + " 값·개수");
        title.seed("p", "item." + id, 1);
        title.seed("p", "item.ammo_rifle", 50, { stack: true });
        title.seed("p", "item.ammo_shotgun", 50, { stack: true });
        title.seed("p", "item.ammo", 50, { stack: true });
        assertEqual(title.call("p", "LoadAmmo", { gun: "item." + id, have: 0 }),
            { gun: "item." + id, loaded: magazine, removed: magazine, left: 50 - magazine }, label + " 빈 탄창 재장전");
    });
});

// M39-02 재장전 — 서버가 min(탄창 - have, 가진 탄약) 을 빼고 loaded 를 돌려준다(DESIGN 8.6-c · 규칙 8).
test("LoadAmmo — 빈 탄창이면 7발 빼고 loaded 7, 모자라면 있는 만큼", () => {
    const title = makeTitle();
    title.seed("p", "item.pistol_semi", 1);
    title.seed("p", "item.ammo", 25, { stack: true });
    assertEqual(title.call("p", "LoadAmmo", { gun: "item.pistol_semi", have: 0 }),
        { gun: "item.pistol_semi", loaded: 7, removed: 7, left: 18 }, "빈 탄창 재장전");
    assertEqual(title.count("p", "item.ammo"), 18, "가방 25 → 18");
    assertEqual(title.call("p", "LoadAmmo", { gun: "item.pistol_semi", have: 4 }).removed, 3, "4발 남았으면 3발만");
    const tail = makeTitle();
    tail.seed("p", "item.pistol_semi", 1);
    tail.seed("p", "item.ammo", 2, { stack: true });
    assertEqual(tail.call("p", "LoadAmmo", { gun: "item.pistol_semi", have: 1 }),
        { gun: "item.pistol_semi", loaded: 3, removed: 2, left: 0 }, "2발뿐이면 2발만");
    assertEqual(tail.count("p", "item.ammo"), 0, "탄약 다 씀");
});

test("LoadAmmo — 탄약 0 → not_enough_items, 권총 없음 → no_gun, 총 아님 → bad_args", () => {
    const title = makeTitle();
    title.seed("p", "item.pistol_semi", 1);
    assertFailsClean(title, "p", "LoadAmmo", { gun: "item.pistol_semi", have: 0 }, "not_enough_items", "탄약 없음");
    const noGun = makeTitle();
    noGun.seed("p", "item.ammo", 25, { stack: true });
    assertFailsClean(noGun, "p", "LoadAmmo", { gun: "item.pistol_semi", have: 0 }, "no_gun", "권총 없음");
    assertFailsClean(noGun, "p", "LoadAmmo", { gun: "item.wood", have: 0 }, "bad_args", "나무는 총이 아니다");
});

test("LoadAmmo — 탄창이 찼으면 0발 빼고, have 는 0..탄창으로 자른다", () => {
    const title = makeTitle();
    title.seed("p", "item.pistol_semi", 1);
    title.seed("p", "item.ammo", 25, { stack: true });
    assertEqual(title.call("p", "LoadAmmo", { gun: "item.pistol_semi", have: 7 }),
        { gun: "item.pistol_semi", loaded: 7, removed: 0, left: 25 }, "찬 탄창");
    assertEqual(title.call("p", "LoadAmmo", { gun: "item.pistol_semi", have: 99 }).loaded, 7, "have 99 → 7 로 자름");
    assertEqual(title.call("p", "LoadAmmo", { gun: "item.pistol_semi", have: -5 }).removed, 7, "have −5 → 0 으로 자름");
    assertEqual(title.count("p", "item.ammo"), 18, "자른 뒤 7발만 빠짐");
    const empty = makeTitle();
    empty.seed("p", "item.pistol_semi", 1);
    assertEqual(empty.call("p", "LoadAmmo", { gun: "item.pistol_semi", have: 7 }).loaded, 7, "찬 탄창이면 탄약 0 이어도 실패 아님");
});

test("LoadAmmo 총 표 — 서버 GUNS = DT_ItemTable MagazineSize·AmmoItemID 행", () => {
    const rows = dumpItemRows().filter((r) => (r.MagazineSize || 0) > 0);
    assertTrue(rows.length >= 1, "생성기 총 행 " + rows.length + "개(빈 통과 방지)");
    const sb = makeTitle().sandbox;
    assertEqual(rows.map((r) => r.ItemID).sort(), Object.keys(sb.GUNS).sort(), "총 행 = 서버 총 목록");
    rows.forEach((r) => assertEqual([sb.GUNS[r.ItemID].magazine, sb.GUNS[r.ItemID].ammo], [r.MagazineSize, r.AmmoItemID], r.ItemID + " 탄창·탄약"));
});

test("파는 값이 DESIGN 8.14 규칙·예시와 같다", () => {
    const title = makeTitle();
    const sb = title.sandbox;
    const value = designRow("파는 값")[1];
    const fish = readFishFromDesign(sb.FISH_TABLE);
    const examples = /\*\*\(([^)]*)\)/.exec(value)[1].split(" · ");
    assertTrue(examples.length >= 2, "물고기 예시 수");
    examples.forEach((ex) => {
        const m = /^(.+) (\d+)$/.exec(ex.trim());
        assertEqual(sb.sellPriceOf(fish[m[1]].id), Number(m[2]), m[1] + " 파는 값");
    });
    assertEqual(sb.SELL_PRICE.fishMinutesPerCoin, Number(/÷ (\d+)\*\*/.exec(value)[1]), "물고기 나눗수");
    assertEqual(sb.SELL_PRICE.meat, Number(/고기 (\d+)/.exec(value)[1]), "고기");
    assertEqual(sb.SELL_PRICE.hide, Number(/가죽·털 (\d+)/.exec(value)[1]), "가죽");
    assertEqual(sb.SELL_PRICE.trophy, Number(/ 깃털·뿔 (\d+)/.exec(value)[1]), "깃털·뿔");
    assertEqual(sb.SELL_PRICE.premium, Number(/좋은 품질 깃털·뿔 (\d+)/.exec(value)[1]), "좋은 품질");
    assertEqual(sb.SELL_PRICE.cropDivisor, Number(/수확 코인 ÷ (\d+)/.exec(value)[1]), "작물 나눗수");
    assertEqual([sb.sellPriceOf("item.wood"), sb.sellPriceOf("item.berry")], [0, 0], "나무·산딸기 0");
    assertEqual(sb.sellPriceOf("item.blueberry"), 1, "새 작물(블루베리) = 5 ÷ 5");
    assertEqual(sb.sellPriceOf("item.plank"), 0, "재료 값 합 0 인 가공품(나무판) 0");
    assertEqual(sb.sellPriceOf("item.roast_meat"), sb.SELL_PRICE.meat + 1, "구운 고기 = 고기 + 1");
    assertEqual(sb.sellPriceOf("item.grilled_perch"), 3, "농어구이 = 2 + 1");
    const pet = designNumber("펫 판매가 (확정 10-06, 루프#309, 2026-10-03 · INBOX M22 ④)", /\*\*(\d+)\*\* 코인/);
    assertEqual(sb.CAPTURE_PET_ITEMS.map(sb.sellPriceOf), sb.CAPTURE_PET_ITEMS.map(() => pet), "펫 판매가(목장 종 15)");
    assertEqual(sb.sellPriceOf("item.pet_dragon"), 0, "목장 종이 아닌 펫 ID 는 0");
});

test("건축·배낭·낚시 숫자가 설계서와 같다", () => {
    const sb = makeTitle().sandbox;
    assertEqual(sb.TENT_MAX, designNumber("텐트 수", /최대 (\d+)/), "텐트 수");
    assertEqual(sb.LAND_CELLS, designNumber("땅 크기", /\*\*(\d+) × \1 칸/), "땅 크기");
    assertEqual(sb.UPGRADE_DEADLINE_HOURS, designNumber("집 업그레이드", /\*\*(\d+)시간 안에/), "업그레이드 기한");
    assertEqual(sb.UPGRADE_COST, [
        { itemId: "item.wood", count: designNumber("집 업그레이드", /나무 (\d+) \+/) },
        { itemId: "item.cedar", count: designNumber("집 업그레이드", /향나무 (\d+)/) }
    ], "업그레이드 재료");
    assertEqual(sb.HOUSE_COLOR_COUNT, designNumber("집 색", /(\d+)가지/), "집 색 수");
    assertEqual(sb.UPKEEP_HOURS_PER_WOOD, designNumber("집 유지(부패)", /1개 = \*\*(\d+)시간/), "나무 1개 유지 시간");
    assertEqual(sb.UPKEEP_MAX_HOURS, designNumber("집 유지(부패)", /최대 \*\*(\d+)시간/), "유지 최대");
    assertEqual(sb.FURNITURE_MAX, designNumber("가구 수", /\*\*(\d+)\*\*/), "가구 수");
    assertEqual(sb.BACKPACK_SLOTS, designNumber("칸", /^(\d+)칸/), "배낭 칸");
    assertEqual(sb.STACK_MAX, designNumber("칸", /스택 (\d+)/), "스택");
    assertEqual(sb.BACKPACK_BONUS_SLOTS, designNumber("등산 짐가방", /\+(\d+)칸/), "등산 짐가방 칸");
    assertEqual(sb.BOAT_CONTAINER_SLOTS, designNumber("배 짐칸", /보관함 (\d+)칸/, "배에 붙은"), "배 짐칸 칸");
    assertEqual(sb.DROP_TTL_SECONDS, designNumber("버리기", /\*\*(\d+)초\*\*/), "가방 사라지는 시간");
    assertEqual(sb.STORAGE_SLOTS, designNumber("보관함", /^(\d+)칸/, "PIN"), "보관함 칸");
    assertEqual(sb.FISH_CALL_GAP_SECONDS, designNumber("어종 결정", /간격 \*\*(\d+)초\*\*/), "낚시 호출 간격");
    assertEqual(sb.CODEX_UNLOCK_PERCENT, designNumber("50% 보상", /낚시 (\d+)%/), "도감 50%");
    assertEqual(sb.BLUEPRINT_PRICE, designNumber("100% 보상", /\*\*(\d+) 코인\*\*/), "설계도 값");
    assertEqual(sb.BLUEPRINT_IDS.length, designNumber("100% 보상", /설계도 (\d+)장/), "설계도 장수");
});

// ══ 제작·먹기 ══════════════════════════════════════════════════════════════════
test("CraftItem — 재료를 빼고 결과를 준다", () => {
    const title = makeTitle();
    withTent(title, "p");
    title.seed("p", "item.wood", 10);
    const r = title.call("p", "CraftItem", { recipeId: "item.plank", times: 2 });
    assertEqual([r.crafted, r.count], ["item.plank", 2], "응답");
    assertEqual([title.count("p", "item.wood"), title.count("p", "item.plank")], [2, 2], "나무 10 → 2, 나무판 2");
});

test("CraftItem — 재료가 모자라면 아무것도 안 바뀐다", () => {
    const title = makeTitle();
    withTent(title, "p");
    title.seed("p", "item.wood", 3);
    assertFailsClean(title, "p", "CraftItem", { recipeId: "item.plank" }, "not_enough_items", "재료 부족");
});

test("CraftItem — 설계도·작업대 없으면 잠김", () => {
    const title = makeTitle();
    title.seed("p", "item.wood", 8);
    assertFailsClean(title, "p", "CraftItem", { recipeId: "item.plank" }, "locked", "텐트(작업대) 없음");
    withTent(title, "p");
    title.seed("p", "item.plank", 4);
    title.seed("p", "item.pheasant_feather", 2);
    assertFailsClean(title, "p", "CraftItem", { recipeId: "item.rod_bait" }, "locked", "설계도 없음");
    title.setData("p", "blueprints", ["item.blueprint_rod_bait"]);
    assertEqual(title.call("p", "CraftItem", { recipeId: "item.rod_bait" }).crafted, "item.rod_bait", "설계도 있으면 만든다");
});

test("CraftItem — 태그 재료는 일반 가죽부터, 가챠 가죽은 나중에", () => {
    const title = makeTitle();
    withTent(title, "p");
    title.setData("p", "blueprints", ["item.blueprint_grill"]);
    title.seed("p", "item.plank", 6);
    title.seed("p", "item.fox_fur", 1);
    title.seed("p", "gacha.mat_hide_5", 2);
    const r = title.call("p", "CraftItem", { recipeId: "item.grill" });
    assertEqual(r.crafted, "item.grill", "만든다");
    assertEqual([title.count("p", "item.fox_fur"), title.count("p", "gacha.mat_hide_5")], [0, 1], "여우 털 먼저, 가챠 소재 1개");
});

test("CraftItem — 가방 칸이 모자라면 거절", () => {
    const title = makeTitle();
    withTent(title, "p");
    title.seed("p", "item.wood", 8);
    fillSlots(title, "p", 19);
    assertFailsClean(title, "p", "CraftItem", { recipeId: "item.plank" }, "inventory_full", "칸 부족");
});

test("ConsumeItem — 먹으면 1개 빼고 회복량, 못 먹는 건 거절", () => {
    const title = makeTitle();
    title.seed("p", "item.grilled_salmon_shark", 3, { stack: true });
    assertEqual(title.call("p", "ConsumeItem", { itemId: "item.grilled_salmon_shark" }).heal, 90, "악상어구이 90");
    assertEqual(title.count("p", "item.grilled_salmon_shark"), 2, "스택 3 → 2 (ConsumeItem 경로)");
    title.seed("p", "item.wood", 1);
    assertFailsClean(title, "p", "ConsumeItem", { itemId: "item.wood" }, "bad_args", "나무는 못 먹는다");
    assertFailsClean(title, "p", "ConsumeItem", { itemId: "item.berry_jam" }, "not_enough_items", "없는 잼");
});

test("ConsumeItem 날로 먹기 — 감자 +5 · 서향 열매 −40 · 산딸기잼 +10 · 밀가루는 못 먹는다", () => {
    const title = makeTitle();
    title.seed("p", "item.potato", 2, { stack: true });
    title.seed("p", "item.mezereon", 1);
    title.seed("p", "item.berry_jam", 1);
    title.seed("p", "item.flour", 1);
    assertEqual(title.call("p", "ConsumeItem", { itemId: "item.potato" }).heal, 5, "감자 날로 +5");
    assertEqual(title.count("p", "item.potato"), 1, "감자 2 → 1");
    assertEqual(title.call("p", "ConsumeItem", { itemId: "item.mezereon" }).heal, -40, "서향 열매 독 −40");
    assertEqual(title.call("p", "ConsumeItem", { itemId: "item.berry_jam" }).heal, 10, "산딸기잼 +10(원본)");
    assertFailsClean(title, "p", "ConsumeItem", { itemId: "item.flour" }, "bad_args", "밀가루는 날로 못 먹는다");
});

test("FillBottle — 빈 병 1 → 샘물 병 1, 없거나 칸이 없으면 그대로", () => {
    const title = makeTitle();
    assertFailsClean(title, "p", "FillBottle", { itemId: "item.empty_bottle" }, "not_enough_items", "빈 병 없음");
    title.seed("p", "item.salt", 1);
    assertFailsClean(title, "p", "FillBottle", { itemId: "item.salt" }, "bad_args", "소금은 못 채운다");
    title.seed("p", "item.empty_bottle", 2, { stack: true });
    assertEqual(title.call("p", "FillBottle", { itemId: "item.empty_bottle" }), { itemId: "item.water_bottle", count: 1 }, "채운다");
    assertEqual([title.count("p", "item.empty_bottle"), title.count("p", "item.water_bottle")], [1, 1], "빈 병 2 → 1, 샘물 병 0 → 1");
    assertEqual(title.call("p", "ConsumeItem", { itemId: "item.water_bottle" }).heal, 15, "샘물 병 +15");
    const full = makeTitle();
    full.seed("p", "item.empty_bottle", 2, { stack: true });
    fillSlots(full, "p", 19);
    assertFailsClean(full, "p", "FillBottle", { itemId: "item.empty_bottle" }, "inventory_full", "빈 병이 남는데 칸이 없다");
});

// ══ 사냥 드롭·채집·포획 받기 ════════════════════════════════════════════════════
test("ClaimLoot — 드롭·채집·펫만 받고, 한도·간격을 넘으면 아무것도 안 바뀐다", () => {
    const title = makeTitle();
    const r = title.call("p", "ClaimLoot", { items: [
        { itemId: "item.fox_meat", count: 2 }, { itemId: "item.fox_tail", count: 1 },
        { itemId: "item.wood", count: 3 }, { itemId: "item.cedar", count: 2 }, { itemId: "item.pet_pig", count: 1 }] });
    assertEqual(r.granted.length, 5, "다섯 종류 지급");
    assertEqual(title.count("p", "item.cedar"), 2, "향나무 +2 (나무 베기 드롭)");
    assertEqual(title.count("p", "item.fox_meat"), 2, "여우고기 +2");
    assertEqual(title.count("p", "item.wood"), 3, "나무 +3");
    assertEqual(title.count("p", "item.pet_pig"), 1, "펫 +1");
    assertFailsClean(title, "p", "ClaimLoot", { items: [{ itemId: "item.berry", count: 1 }] }, "too_soon", "10초 안 재호출");
    assertFailsClean(title, "q", "ClaimLoot", { items: [{ itemId: "item.plank", count: 1 }] }, "bad_args", "제작품은 못 받는다");
    assertFailsClean(title, "q", "ClaimLoot", { items: [{ itemId: "item.roast_meat", count: 1 }] }, "bad_args", "구운 고기는 못 받는다");
    assertFailsClean(title, "q", "ClaimLoot", { items: [{ itemId: "gacha.mat_hide_5", count: 1 }] }, "bad_args", "가챠 소재는 못 받는다");
    assertFailsClean(title, "q", "ClaimLoot", { items: [{ itemId: "item.wood", count: 21 }] }, "too_many", "한 번에 20개 초과");
});

test("채집 더미 (M13-02) — 버섯·수지·꽃·베리를 받고 사냥에 안 세며, 광대버섯만 날로 먹어 −20", () => {
    const title = makeTitle();
    const gathered = ["item.chanterelle", "item.penny_bun", "item.yellowleg_bonnet", "item.fly_amanita", "item.resin",
        "item.kingcup", "item.windflower", "item.hepatica", "item.poppy", "item.fireweed",
        "item.blackberry", "item.mezereon", "item.cloudberry", "item.black_lotus", "item.blueberry", "item.lingonberry", "item.sugar_beet", "item.wheat"];
    const swamp = ["item.honey_mushroom", "item.king_stropharia", "item.oyster_mushroom", "item.vipers_mane", "item.bog_flower", "item.water_lily"];
    const r = title.call("p", "ClaimLoot", { items: gathered.map((id) => ({ itemId: id, count: 1 })) });
    assertEqual(r.granted.length, gathered.length, "13.4 채집물 18종 전부 지급");
    // 한 번에 20개 한도라 늪 6종은 다른 사람이 받는다
    assertEqual(title.call("q", "ClaimLoot", { items: swamp.map((id) => ({ itemId: id, count: 1 })) }).granted.length, swamp.length, "13.4 늪 채집물 6종 전부 지급");
    assertEqual(title.count("p", "item.resin"), 1, "수지 +1");
    const ach = title.getData("p", "achievements");
    assertEqual((ach && ach.counters && ach.counters.hunt) || 0, 0, "채집은 사냥에 안 센다");
    assertEqual(title.call("p", "ConsumeItem", { itemId: "item.fly_amanita" }).heal, -20, "광대버섯 −20");
    assertEqual(title.count("p", "item.fly_amanita"), 0, "먹은 광대버섯 1 → 0");
    assertFailsClean(title, "p", "ConsumeItem", { itemId: "item.chanterelle" }, "bad_args", "식용 버섯은 날로 못 먹는다");
});

// ══ 버리기·줍기·사망 ═══════════════════════════════════════════════════════════
test("DropItem → PickupDrop — 다른 사람이 한 번만 줍는다", () => {
    const title = makeTitle();
    title.seed("a", "item.wood", 5);
    const drop = title.call("a", "DropItem", { itemId: "item.wood", count: 3 });
    assertEqual(title.count("a", "item.wood"), 2, "떨군 사람 나무 5 → 2");
    const r = title.call("b", "PickupDrop", { ownerId: "a", dropId: drop.dropId });
    assertEqual(r.picked, true, "줍기 성공");
    assertEqual(title.count("b", "item.wood"), 3, "주운 사람 +3");
    assertEqual(title.getData("a", "drops"), [], "떨군 사람 drops 에서 지워짐");
    assertFailsClean(title, "c", "PickupDrop", { ownerId: "a", dropId: drop.dropId }, "already_taken", "두 번째 줍기", ["a", "b"]);
});

test("지급이 거절되면 기록을 안 지운다 — 가방·보관함·가구·배가 남고 다시 받으면 받는다 (결함 10-07 01:10)", () => {
    const title = makeTitle();
    // 떨군 가방 (PickupDrop)
    title.seed("a", "item.lifeboat", 1);
    title.seed("a", "item.gold_chunk", 2);
    const drop = title.call("a", "DropOnDeath");
    title.faults.grantItems = true;
    const r = assertFailsClean(title, "b", "PickupDrop", { ownerId: "a", dropId: drop.dropId }, "grant_failed", "지급 거절된 줍기", ["a"]);
    assertTrue(r.error.indexOf("APIClientRequestRateLimitExceeded") !== -1, "PlayFab 오류 이름이 error 에 실림");
    assertEqual(r.playFabErrorCode, 1199, "PlayFab 오류 번호");
    // 배 보관함 (MoveFromStorage)
    title.faults.grantItems = false;
    title.seed("c", "item.boat_container", 1);
    title.seed("c", "item.wood", 3);
    title.call("c", "MoveToStorage", { boat: true, itemId: "item.wood", count: 3 });
    // 텐트 가구 (RemoveStructure)
    const tentId = withTent(title, "d");
    title.seed("d", "item.bench", 1);
    const s = title.call("d", "PlaceStructure", { tentId, itemId: "item.bench", x: 1, y: 1, rot: 0 });
    // 띄운 배 (StowBoat)
    title.seed("e", "item.lifeboat", 1);
    title.call("e", "LaunchBoat");
    title.faults.grantItems = true;
    assertFailsClean(title, "c", "MoveFromStorage", { boat: true, itemId: "item.wood", count: 2 }, "grant_failed", "지급 거절된 보관함 빼기");
    assertFailsClean(title, "d", "RemoveStructure", { tentId, structureId: s.structureId }, "grant_failed", "지급 거절된 가구 회수");
    assertFailsClean(title, "e", "StowBoat", {}, "grant_failed", "지급 거절된 배 거두기");
    // 한도가 풀리면 같은 호출로 받는다
    title.faults.grantItems = false;
    assertEqual(title.call("b", "PickupDrop", { ownerId: "a", dropId: drop.dropId }).picked, true, "다시 주우면 받음");
    assertEqual(title.count("b", "item.lifeboat"), 1, "구명보트 받음");
    assertEqual(title.count("b", "item.gold_chunk"), 2, "금덩이 받음");
    assertEqual(title.getData("a", "drops"), [], "받은 뒤에야 기록이 지워짐");
    assertEqual(title.call("c", "MoveFromStorage", { boat: true, itemId: "item.wood", count: 2 }).storage, { "item.wood": 1 }, "다시 빼면 3 → 1");
    assertEqual(title.count("c", "item.wood"), 2, "나무 2 받음");
    assertEqual(title.call("d", "RemoveStructure", { tentId, structureId: s.structureId }).removed, "item.bench", "다시 회수");
    assertEqual(title.call("e", "StowBoat").stowed, true, "다시 거둠");
    assertEqual(title.count("e", "item.lifeboat"), 1, "구명보트 돌아옴");
});

test("DropItem — 펫·가챠는 못 떨구고, 없는 만큼은 못 떨군다", () => {
    const title = makeTitle();
    title.seed("a", "gacha.pet_crown", 1);
    title.seed("a", "item.wood", 1);
    assertFailsClean(title, "a", "DropItem", { itemId: "gacha.pet_crown" }, "protected", "가챠 떨구기");
    assertFailsClean(title, "a", "DropItem", { itemId: "item.wood", count: 2 }, "not_enough_items", "모자란 수량");
});

test("PickupDrop — 600초 지나면 사라지고, 칸이 없으면 가방이 남는다", () => {
    const title = makeTitle();
    title.seed("a", "item.wood", 2);
    const d1 = title.call("a", "DropItem", { itemId: "item.wood", count: 1 });
    fillSlots(title, "b", 20);
    assertFailsClean(title, "b", "PickupDrop", { ownerId: "a", dropId: d1.dropId }, "inventory_full", "줍는 사람 칸 부족", ["a"]);
    title.age("a", "drops", title.sandbox.DROP_TTL_SECONDS * 1000);
    const r = title.call("c", "PickupDrop", { ownerId: "a", dropId: d1.dropId });
    assertEqual(r.code, "expired", "기한 지난 가방");
    assertEqual(title.count("c", "item.wood"), 0, "아무것도 안 받는다");
    assertEqual(title.getData("a", "drops"), [], "지난 가방은 기록에서 빠진다");
});

test("DropOnDeath — 배낭만 떨구고 펫·가챠·코인은 남는다(25개 넘게도)", () => {
    const title = makeTitle();
    title.seed("a", "item.wood", 30);
    title.seed("a", "item.club", 1);
    title.seed("a", "gacha.deco_red_barn", 1);
    title.seed("a", "item.pet_fox", 1);
    title.setCoins("a", 40);
    const r = title.call("a", "DropOnDeath");
    assertEqual(r.dropped, true, "떨굼");
    assertEqual(r.items.map((e) => e.itemId + "×" + e.count).sort(), ["item.club×1", "item.wood×30"], "가방 내용");
    assertEqual(title.accounts.a.inventory.map((it) => it.ItemId).sort(), ["gacha.deco_red_barn", "item.pet_fox"], "남는 것");
    assertEqual(title.coins("a"), 40, "코인 그대로");
    assertEqual(title.call("a", "DropOnDeath").dropped, false, "빈 배낭은 가방 없음");
});

// ══ 건축·거점 ══════════════════════════════════════════════════════════════════
test("PlaceTent — 2개까지, 겹치면·텐트 없으면 거절", () => {
    const title = makeTitle();
    title.seed("p", "item.tent", 3);
    const t1 = title.call("p", "PlaceTent", { x: 0, y: 0 });
    assertTrue(t1.tentId && t1.upgradeDeadlineUtc, "첫 텐트");
    assertEqual(Date.parse(t1.upgradeDeadlineUtc) - Date.parse(t1.placedAtUtc), 24 * 3600 * 1000, "기한 24시간");
    assertFailsClean(title, "p", "PlaceTent", { x: 10, y: 0 }, "occupied", "땅 겹침");
    title.call("p", "PlaceTent", { x: 16, y: 0 });
    assertFailsClean(title, "p", "PlaceTent", { x: 40, y: 0 }, "limit", "세 번째 텐트");
    assertFailsClean(title, "q", "PlaceTent", { x: 0, y: 0 }, "not_enough_items", "텐트 아이템 없음");
});

test("PlaceTent — 섬(hunt)에도 놓고, 겹침은 같은 존끼리만", () => {
    const title = makeTitle();
    title.seed("p", "item.tent", 3);
    const t1 = title.call("p", "PlaceTent", { x: 0, y: 0 });
    assertEqual(t1.zone, "ranch", "zone 없으면 ranch");
    const t2 = title.call("p", "PlaceTent", { x: 0, y: 0, zone: "hunt" });
    assertEqual(t2.zone, "hunt", "같은 칸이라도 섬이면 놓인다");
    assertEqual(title.getData("p", "base").map((t) => t.zone), ["ranch", "hunt"], "zone 이 저장된다");
    assertFailsClean(title, "q", "PlaceTent", { x: 0, y: 0, zone: "sea" }, "bad_args", "모르는 존");
});

test("PlaceStructure / RemoveStructure — 땅 안에만, 한 칸에 하나, 100% 회수", () => {
    const title = makeTitle();
    const tentId = withTent(title, "p");
    title.seed("p", "item.bench", 2);
    const s = title.call("p", "PlaceStructure", { tentId, itemId: "item.bench", x: 7, y: -8, rot: 90 });
    assertTrue(s.structureId, "땅 끝 칸에 놓임");
    assertEqual(title.count("p", "item.bench"), 1, "벤치 1개 빠짐");
    assertFailsClean(title, "p", "PlaceStructure", { tentId, itemId: "item.bench", x: 8, y: 0 }, "out_of_land", "땅 밖");
    const f = title.call("p", "PlaceStructure", { tentId, itemId: "item.bench", x: 0, y: 0, rot: 15 });
    assertTrue(f.structureId, "15도 돌려 놓임");
    assertFailsClean(title, "p", "PlaceStructure", { tentId, itemId: "item.bench", x: 1, y: 0, rot: 20 }, "bad_args", "15 의 배수 아님");
    assertFailsClean(title, "p", "PlaceStructure", { tentId, itemId: "item.bench", x: 1, y: 0, rot: 360 }, "bad_args", "360 은 범위 밖");
    title.call("p", "RemoveStructure", { tentId, structureId: f.structureId });
    assertFailsClean(title, "p", "PlaceStructure", { tentId, itemId: "item.bench", x: 7, y: -8 }, "occupied", "같은 칸");
    assertFailsClean(title, "p", "PlaceStructure", { tentId, itemId: "item.wood", x: 1, y: 1 }, "bad_args", "놓을 수 없는 아이템");
    title.call("p", "DropItem", { itemId: "item.bench" }); // 벤치가 가방에 없어야 회수에 새 칸이 든다
    fillSlots(title, "p", 20);
    assertFailsClean(title, "p", "RemoveStructure", { tentId, structureId: s.structureId }, "inventory_full", "회수 칸 부족");
    title.call("p", "DropItem", { itemId: "item.filler_0" });
    assertEqual(title.call("p", "RemoveStructure", { tentId, structureId: s.structureId }).removed, "item.bench", "회수");
    assertEqual(title.count("p", "item.bench"), 1, "벤치가 돌아옴");
});

test("PlaceStructure 세밀 격자 — 반 칸 오프셋 hx,hy 는 0/1, 같은 칸 다른 오프셋은 따로 놓임", () => {
    const title = makeTitle();
    const tentId = withTent(title, "h");
    title.seed("h", "item.bench", 4);
    const a = title.call("h", "PlaceStructure", { tentId, itemId: "item.bench", x: 2, y: 2, rot: 0 });
    assertTrue(a.structureId, "오프셋 없이 놓임");
    const b = title.call("h", "PlaceStructure", { tentId, itemId: "item.bench", x: 2, y: 2, rot: 0, hx: 1, hy: 0 });
    assertTrue(b.structureId, "같은 칸 반 칸 옆에 놓임");
    assertFailsClean(title, "h", "PlaceStructure", { tentId, itemId: "item.bench", x: 2, y: 2, hx: 1, hy: 0 }, "occupied", "같은 반 칸");
    assertFailsClean(title, "h", "PlaceStructure", { tentId, itemId: "item.bench", x: 3, y: 2, hx: 2 }, "bad_args", "오프셋 2 는 범위 밖");
    assertFailsClean(title, "h", "PlaceStructure", { tentId, itemId: "item.bench", x: 3, y: 2, hy: -1 }, "bad_args", "오프셋 -1 은 범위 밖");
});

test("PlaceStructure 벽 장식 — 집 벽 칸 변에만, 높이 120~200, 같은 자리 높이 50 이상 벌어져야 한다 (M108-02)", () => {
    const title = makeTitle();
    const tentId = withTent(title, "w");
    title.seed("w", "item.picture_frame", 6);
    const hang = (x, y, side, h) => ({ tentId, itemId: "item.picture_frame", x, y, wall: { side, h } });
    assertFailsClean(title, "w", "PlaceStructure", hang(0, 0, 2, 150), "not_house", "텐트 상태에선 못 건다");
    title.seed("w", "item.wood", 150, { stack: true });
    title.seed("w", "item.cedar", 50, { stack: true });
    title.call("w", "UpgradeHouse", { tentId, color: 0 });
    const a = title.call("w", "PlaceStructure", hang(1, 0, 2, 150));
    assertTrue(a.structureId, "남쪽 벽 칸에 걸림");
    assertFailsClean(title, "w", "PlaceStructure", hang(1, 0, 2, 180), "occupied", "높이 30 차이는 겹침");
    assertTrue(title.call("w", "PlaceStructure", hang(1, 0, 2, 200)).structureId, "높이 50 차이는 따로 걸림");
    assertFailsClean(title, "w", "PlaceStructure", hang(1, 1, 2, 150), "not_wall", "가운데 칸은 벽이 아님");
    assertFailsClean(title, "w", "PlaceStructure", hang(5, 0, 2, 150), "not_wall", "집 밖 칸");
    assertFailsClean(title, "w", "PlaceStructure", hang(1, 0, 2, 119), "bad_args", "너무 낮음");
    assertFailsClean(title, "w", "PlaceStructure", hang(1, 0, 2, 201), "bad_args", "너무 높음");
    assertFailsClean(title, "w", "PlaceStructure", hang(1, 0, 4, 150), "bad_args", "벽 방향은 0~3");
    assertTrue(title.call("w", "PlaceStructure", hang(0, 2, 3, 150)).structureId, "모서리 칸은 두 벽 모두 걸 수 있음");
    assertTrue(title.call("w", "PlaceStructure", hang(0, 2, 0, 150)).structureId, "같은 칸 북쪽 벽");
    title.seed("w", "item.bench", 1);
    assertTrue(title.call("w", "PlaceStructure", { tentId, itemId: "item.bench", x: 1, y: 0 }).structureId, "벽 장식이 있는 칸에도 바닥 가구는 놓임");
});

test("PlaceStructure 벽 장식 넷 더함 — 아치 거울·섬 지도·닻·들꽃 화환도 같은 걸기 판정, 액자와 높이 50 규칙 공유 (M108-05c)", () => {
    const title = makeTitle();
    const tentId = withTent(title, "v");
    const ids = ["item.wall_mirror", "item.wall_map", "item.wall_anchor", "item.wall_wreath"];
    ids.forEach((id) => title.seed("v", id, 2));
    title.seed("v", "item.picture_frame", 1);
    const hang = (itemId, x, y, side, h) => ({ tentId, itemId, x, y, wall: { side, h } });
    assertFailsClean(title, "v", "PlaceStructure", hang("item.wall_map", 1, 0, 2, 150), "not_house", "텐트 상태에선 못 건다");
    title.seed("v", "item.wood", 150, { stack: true });
    title.seed("v", "item.cedar", 50, { stack: true });
    title.call("v", "UpgradeHouse", { tentId, color: 0 });
    ids.forEach((id, i) => assertTrue(title.call("v", "PlaceStructure", hang(id, i % 3, 0, 2, i < 3 ? 120 : 200)).structureId, id + " 남쪽 벽에 걸림"));
    assertFailsClean(title, "v", "PlaceStructure", hang("item.picture_frame", 0, 0, 2, 160), "occupied", "거울(120)과 높이 40 차이는 겹침");
    assertFailsClean(title, "v", "PlaceStructure", hang("item.wall_wreath", 1, 1, 2, 150), "not_wall", "가운데 칸은 벽이 아님");
    assertFailsClean(title, "v", "PlaceStructure", { tentId, itemId: "item.wall_anchor", x: 1, y: 0 }, "bad_args", "벽 없이 바닥에 못 놓음");
    const tent = title.getData("v", "base").find((tt) => tt.tentId === tentId);
    assertEqual(ids.map((id) => tent.items.filter((it) => it.itemId === id && it.wall).length), [1, 1, 1, 1], "넷 다 벽 칸·높이와 함께 저장");
});

test("PlaceStructure 위에 올리기 — 탁자 슬롯 3·보관함 2, 자리는 받침 것, 한도 0.5개, 소품 있는 받침은 못 치움 (M108-03)", () => {
    const title = makeTitle();
    const tentId = withTent(title, "t");
    title.seed("t", "item.table_wood", 1);
    title.seed("t", "item.storage_box", 1);
    title.seed("t", "item.bench", 1);
    title.seed("t", "item.mug", 6);
    const table = title.call("t", "PlaceStructure", { tentId, itemId: "item.table_wood", x: 2, y: 1, rot: 90, hx: 1 }).structureId;
    const box = title.call("t", "PlaceStructure", { tentId, itemId: "item.storage_box", x: -2, y: 0 }).structureId;
    const bench = title.call("t", "PlaceStructure", { tentId, itemId: "item.bench", x: 0, y: 3 }).structureId;
    assertTrue(table && box && bench, "받침 준비");
    const put = (host, slot) => ({ tentId, itemId: "item.mug", x: 0, y: 0, top: { host, slot } });
    assertFailsClean(title, "t", "PlaceStructure", { tentId, itemId: "item.mug", x: 0, y: 0 }, "bad_args", "소품은 받침 없이 바닥에 못 놓음");
    assertTrue(title.call("t", "PlaceStructure", put(table, 0)).structureId, "탁자 슬롯 0");
    assertFailsClean(title, "t", "PlaceStructure", put(table, 0), "occupied", "같은 슬롯 둘");
    assertTrue(title.call("t", "PlaceStructure", put(table, 2)).structureId, "탁자 슬롯 2");
    assertFailsClean(title, "t", "PlaceStructure", put(table, 3), "bad_args", "탁자 슬롯은 0~2");
    assertTrue(title.call("t", "PlaceStructure", put(box, 1)).structureId, "보관함 슬롯 1");
    assertFailsClean(title, "t", "PlaceStructure", put(box, 2), "bad_args", "보관함 슬롯은 0~1");
    assertFailsClean(title, "t", "PlaceStructure", put(bench, 0), "not_host", "벤치 위엔 못 올림");
    assertFailsClean(title, "t", "PlaceStructure", put("nope", 0), "not_found", "없는 받침");
    assertFailsClean(title, "t", "PlaceStructure", { tentId, itemId: "item.mug", x: 0, y: 0, top: { host: table } }, "bad_args", "슬롯 빠짐");
    const tentOf = () => title.getData("t", "base").find((tt) => tt.tentId === tentId);
    const tent = tentOf();
    const mug = tent.items.find((it) => it.top && it.top.host === table && it.top.slot === 0);
    assertEqual([mug.x, mug.y, mug.rot, mug.hx, mug.hy], [2, 1, 90, 1, 0], "자리·돌림·반 칸 = 받침 것");
    assertEqual(title.sandbox.furnitureLoad(tent), 3 + 3 * 0.5, "소품은 0.5개로 셈");
    assertFailsClean(title, "t", "RemoveStructure", { tentId, structureId: table }, "not_empty", "소품 있는 탁자는 못 치움");
    title.call("t", "RemoveStructure", { tentId, structureId: mug.structureId });
    const other = tentOf().items.find((it) => it.top && it.top.host === table);
    title.call("t", "RemoveStructure", { tentId, structureId: other.structureId });
    assertTrue(title.call("t", "RemoveStructure", { tentId, structureId: table }).removed, "소품 치운 뒤엔 탁자도 치움");
    // 보관함 칸 (-2,0) 의 소품은 바닥 겹침에 안 끼고, 보관함 자체가 그 칸을 막는다
    title.seed("t", "item.chair_wood", 1);
    assertFailsClean(title, "t", "PlaceStructure", { tentId, itemId: "item.chair_wood", x: -2, y: 0 }, "occupied", "받침 칸은 받침이 막음");
    assertTrue(title.call("t", "PlaceStructure", { tentId, itemId: "item.chair_wood", x: 2, y: 1, hx: 1 }).structureId, "탁자 치운 칸엔 바닥 가구");
});

test("PlaceStructure 위에 올리기 — 집 가구 받침: 둥근 탁자 2·긴 식탁 3·서랍장 2·조리대 2, 개수대·책장은 받침 아님 (M108-04c)", () => {
    const title = makeTitle();
    const tentId = withTent(title, "t");
    const hosts = [["item.table_round", 2, 2, 0], ["item.table_long", 3, 2, 3], ["item.dresser", 2, -2, 0], ["item.kitchen_counter", 2, -2, 3]];
    const notHosts = [["item.kitchen_sink", 0, -4], ["item.bookshelf", 4, 0]];
    hosts.concat(notHosts).forEach(([id]) => title.seed("t", id, 1));
    title.seed("t", "item.candle", 12);
    const put = (host, slot) => ({ tentId, itemId: "item.candle", x: 0, y: 0, top: { host, slot } });
    hosts.forEach(([id, slots, x, y]) => {
        const host = title.call("t", "PlaceStructure", { tentId, itemId: id, x, y, rot: 90 }).structureId;
        assertTrue(host, id + " 놓음");
        assertTrue(title.call("t", "PlaceStructure", put(host, slots - 1)).structureId, id + " 끝 슬롯 " + (slots - 1));
        assertFailsClean(title, "t", "PlaceStructure", put(host, slots), "bad_args", id + " 슬롯은 0~" + (slots - 1));
    });
    notHosts.forEach(([id, x, y]) => {
        const host = title.call("t", "PlaceStructure", { tentId, itemId: id, x, y }).structureId;
        assertTrue(host, id + " 놓음");
        assertFailsClean(title, "t", "PlaceStructure", put(host, 0), "not_host", id + " 위엔 못 올림");
    });
});

test("PlaceStructure 러그 — 바닥 가구와 같은 칸에 깔림(순서 상관없음), 러그끼리는 겹침, 한도 1개 (M108-05b)", () => {
    const title = makeTitle();
    const tentId = withTent(title, "r");
    ["item.rug_round", "item.rug_runner", "item.rug_square", "item.doormat"].forEach((id) => title.seed("r", id, 2));
    title.seed("r", "item.table_round", 2);
    const rug = title.call("r", "PlaceStructure", { tentId, itemId: "item.rug_round", x: 2, y: 2 });
    assertTrue(rug.structureId, "러그 먼저 깔림");
    assertTrue(title.call("r", "PlaceStructure", { tentId, itemId: "item.table_round", x: 2, y: 2 }).structureId, "러그 위에 탁자");
    assertFailsClean(title, "r", "PlaceStructure", { tentId, itemId: "item.table_round", x: 2, y: 2 }, "occupied", "탁자 위 칸에 탁자 또");
    assertFailsClean(title, "r", "PlaceStructure", { tentId, itemId: "item.rug_square", x: 2, y: 2 }, "occupied", "러그 위에 러그");
    assertTrue(title.call("r", "PlaceStructure", { tentId, itemId: "item.table_round", x: -2, y: -2 }).structureId, "탁자 먼저");
    assertTrue(title.call("r", "PlaceStructure", { tentId, itemId: "item.doormat", x: -2, y: -2 }).structureId, "탁자 밑에 깔개");
    assertTrue(title.call("r", "PlaceStructure", { tentId, itemId: "item.rug_runner", x: 4, y: 0, hx: 1 }).structureId, "반 칸 러그");
    const base = title.getData("r", "base");
    assertEqual(base[0].items.length, 5, "다섯 줄 저장");
    assertEqual(title.sandbox.furnitureLoad(base[0]), 5, "러그도 한도 1개");
    assertEqual(title.call("r", "RemoveStructure", { tentId, structureId: rug.structureId }).removed, "item.rug_round", "가구 밑 러그도 회수");
});

test("PlaceStructure 바깥 넷 — 디딤 길은 러그 층이라 아치·울타리 밑에 깔림, 우편함·아치·울타리끼리는 한 칸 하나 (M108-05d)", () => {
    const title = makeTitle();
    const tentId = withTent(title, "o");
    ["item.fence_picket", "item.path_log", "item.garden_arch", "item.mailbox"].forEach((id) => title.seed("o", id, 2));
    assertTrue(title.call("o", "PlaceStructure", { tentId, itemId: "item.path_log", x: 3, y: 0 }).structureId, "디딤 길");
    assertTrue(title.call("o", "PlaceStructure", { tentId, itemId: "item.garden_arch", x: 3, y: 0 }).structureId, "디딤 길 위에 아치");
    assertTrue(title.call("o", "PlaceStructure", { tentId, itemId: "item.fence_picket", x: 4, y: 0 }).structureId, "울타리 먼저");
    assertTrue(title.call("o", "PlaceStructure", { tentId, itemId: "item.path_log", x: 4, y: 0 }).structureId, "울타리 밑에 디딤 길");
    assertFailsClean(title, "o", "PlaceStructure", { tentId, itemId: "item.mailbox", x: 3, y: 0 }, "occupied", "아치 칸에 우편함");
    assertTrue(title.call("o", "PlaceStructure", { tentId, itemId: "item.mailbox", x: 5, y: 0 }).structureId, "옆 칸 우편함");
    assertEqual(title.sandbox.furnitureLoad(title.getData("o", "base")[0]), 5, "다섯 다 한도 1개씩");
});

test("PlaceStructure 조명 넷 — 바닥 가구처럼 한 칸 하나, 러그 위엔 놓임, 한도 1개씩 (M108-05e)", () => {
    const title = makeTitle();
    const tentId = withTent(title, "l");
    ["item.candle_stand", "item.oil_lamp", "item.lantern_post", "item.garden_torch", "item.doormat"].forEach((id) => title.seed("l", id, 2));
    assertTrue(title.call("l", "PlaceStructure", { tentId, itemId: "item.doormat", x: 3, y: 0 }).structureId, "깔개");
    assertTrue(title.call("l", "PlaceStructure", { tentId, itemId: "item.candle_stand", x: 3, y: 0 }).structureId, "깔개 위에 촛대");
    assertFailsClean(title, "l", "PlaceStructure", { tentId, itemId: "item.oil_lamp", x: 3, y: 0 }, "occupied", "촛대 칸에 등잔대");
    assertTrue(title.call("l", "PlaceStructure", { tentId, itemId: "item.oil_lamp", x: 4, y: 0 }).structureId, "옆 칸 등잔대");
    assertTrue(title.call("l", "PlaceStructure", { tentId, itemId: "item.lantern_post", x: 5, y: 0, rot: 15 }).structureId, "등불 15° 돌림");
    assertTrue(title.call("l", "PlaceStructure", { tentId, itemId: "item.garden_torch", x: 6, y: 0 }).structureId, "횃불");
    assertEqual(title.sandbox.furnitureLoad(title.getData("l", "base")[0]), 5, "깔개 + 조명 넷 — 1개씩");
});

test("capacityFor — 배 짐칸는 배낭 칸을 안 늘린다, 등산 짐가방만 +8 (M32-02 보정)", () => {
    const sb = makeTitle().sandbox;
    assertEqual(sb.capacityFor({ "item.wood": 1 }), 20, "둘 다 없으면 20칸");
    assertEqual(sb.capacityFor({ "item.boat_container": 1 }), 20, "컨테이너만 = 그대로 20칸");
    assertEqual(sb.capacityFor({ "item.boat_container": 1, "item.hiking_backpack": 1 }), 28, "배낭 + 컨테이너 = 최대 28칸");
});

test("PlaceStructure — 가구 40개 한도", () => {
    const title = makeTitle();
    const tentId = withTent(title, "p");
    const base = title.getData("p", "base");
    for (let i = 0; i < 40; i++) base[0].items.push({ structureId: "s" + i, itemId: "item.fence_wood", x: -8 + (i % 16), y: -8 + Math.floor(i / 16), rot: 0 });
    title.setData("p", "base", base);
    title.seed("p", "item.fence_wood", 1);
    assertFailsClean(title, "p", "PlaceStructure", { tentId, itemId: "item.fence_wood", x: 5, y: 5 }, "limit", "41번째 가구");
});

test("UpgradeHouse — 나무 150 + 향나무 50 이면 집, 모자라면 그대로", () => {
    const title = makeTitle();
    const tentId = withTent(title, "p");
    title.seed("p", "item.wood", 150, { stack: true });
    title.seed("p", "item.cedar", 49, { stack: true });
    assertFailsClean(title, "p", "UpgradeHouse", { tentId, color: 2 }, "not_enough_items", "향나무 49");
    title.seed("p", "item.cedar", 1);
    const r = title.call("p", "UpgradeHouse", { tentId, color: 2 });
    assertEqual([r.upgraded, r.color], [true, 2], "집이 됨");
    assertEqual([title.count("p", "item.wood"), title.count("p", "item.cedar")], [0, 0], "재료 다 빠짐");
    assertFailsClean(title, "p", "UpgradeHouse", { tentId }, "already", "두 번 업그레이드");
});

test("UpgradeHouse — 24시간 지나면 텐트와 가구가 사라진다", () => {
    const title = makeTitle();
    const tentId = withTent(title, "p");
    title.seed("p", "item.wood", 150);
    title.seed("p", "item.cedar", 50);
    title.age("p", "base", 24 * 3600 * 1000);
    const r = title.call("p", "UpgradeHouse", { tentId });
    assertEqual([r.code, r.reason], ["expired", "upgrade_deadline"], "기한 지남");
    assertEqual(title.getData("p", "base"), [], "텐트 기록이 지워짐");
    assertEqual(title.count("p", "item.wood"), 150, "재료는 그대로");
});

test("FeedUpkeep — 나무 1개 = 2시간, 200시간에서 멈추고 0 이면 집이 사라진다", () => {
    const title = makeTitle();
    const tentId = withTent(title, "p");
    title.seed("p", "item.wood", 300, { stack: true });
    title.seed("p", "item.cedar", 50, { stack: true });
    assertFailsClean(title, "p", "FeedUpkeep", { tentId, count: 1 }, "not_house", "텐트엔 유지 없음");
    const up = title.call("p", "UpgradeHouse", { tentId });
    const r = title.call("p", "FeedUpkeep", { tentId, count: 150 });
    assertEqual(r.used, (200 - 24) / 2, "24시간 남은 집은 88개만 받는다");
    assertEqual(Date.parse(r.upkeepUntilUtc) - Date.parse(up.upkeepUntilUtc), 88 * 2 * 3600 * 1000, "88 × 2시간");
    assertEqual(title.count("p", "item.wood"), 150 - 88, "88개만 빠짐");
    assertFailsClean(title, "p", "FeedUpkeep", { tentId, count: 1 }, "full", "가득");
    title.age("p", "base", 200 * 3600 * 1000);
    assertEqual(title.call("p", "FeedUpkeep", { tentId, count: 1 }).reason, "upkeep_empty", "유지 0 → 사라짐");
});

// ══ 보관함 ═════════════════════════════════════════════════════════════════════
test("MoveToStorage / MoveFromStorage — 주인과 PIN 아는 친구만, 20칸", () => {
    const title = makeTitle();
    const tentId = withTent(title, "a");
    title.seed("a", "item.storage_box", 1);
    const box = title.call("a", "PlaceStructure", { tentId, itemId: "item.storage_box", x: 1, y: 1, pin: "01234" });
    title.seed("a", "item.wood", 5);
    const put = title.call("a", "MoveToStorage", { tentId, structureId: box.structureId, itemId: "item.wood", count: 5 });
    assertEqual(put.storage, { "item.wood": 5 }, "보관함에 5");
    assertEqual(title.count("a", "item.wood"), 0, "가방에서 빠짐");
    const where = { ownerId: "a", tentId, structureId: box.structureId, itemId: "item.wood", count: 2 };
    assertFailsClean(title, "b", "MoveFromStorage", Object.assign({ pin: "99999" }, where), "locked", "틀린 PIN");
    title.call("b", "MoveFromStorage", Object.assign({ pin: "01234" }, where));
    assertEqual(title.count("b", "item.wood"), 2, "친구가 PIN 으로 꺼냄");
    assertFailsClean(title, "a", "MoveFromStorage", { tentId, structureId: box.structureId, itemId: "item.wood", count: 4 }, "not_enough_items", "보관함에 3뿐");
    assertFailsClean(title, "a", "RemoveStructure", { tentId, structureId: box.structureId }, "not_empty", "찬 보관함 치우기");
    for (let i = 0; i < 19; i++) {
        title.seed("a", "item.filler_" + i, 1);
        title.call("a", "MoveToStorage", { tentId, structureId: box.structureId, itemId: "item.filler_" + i });
    }
    title.seed("a", "item.club", 1);
    assertFailsClean(title, "a", "MoveToStorage", { tentId, structureId: box.structureId, itemId: "item.club" }, "storage_full", "21번째 칸");
});

test("배 보관함 — 배 짐칸 주인만, 8칸, 넣기·빼기·재접속 조회 (M32-02 보정)", () => {
    const title = makeTitle();
    title.seed("a", "item.wood", 5);
    assertFailsClean(title, "a", "MoveToStorage", { boat: true, itemId: "item.wood", count: 1 }, "locked", "컨테이너 없으면");
    assertFailsClean(title, "a", "MoveFromStorage", { boat: true, itemId: "item.wood", count: 1 }, "locked", "컨테이너 없이 빼기");
    title.seed("a", "item.boat_container", 1);
    const put = title.call("a", "MoveToStorage", { boat: true, itemId: "item.wood", count: 5 });
    assertEqual(put.storage, { "item.wood": 5 }, "배 보관함에 5");
    assertEqual(title.count("a", "item.wood"), 0, "가방에서 빠짐");
    // 열쇠를 자기 안에 넣으면 다시는 못 연다 — 거절하고 가방에 그대로 둔다 (M32-02c).
    assertFailsClean(title, "a", "MoveToStorage", { boat: true, itemId: "item.boat_container", count: 1 }, "self", "컨테이너 자신");
    assertEqual(title.count("a", "item.boat_container"), 1, "컨테이너는 가방에 그대로");
    assertEqual(title.call("a", "GetBaseState", {}).boatStorage, { "item.wood": 5 }, "재접속 조회에 보관함 속");
    title.seed("b", "item.boat_container", 1);
    assertFailsClean(title, "b", "MoveFromStorage", { boat: true, ownerId: "a", itemId: "item.wood", count: 1 }, "locked", "남의 배 보관함", ["a"]);
    assertEqual(title.call("b", "GetBaseState", {}).boatStorage, {}, "계정마다 따로");
    const took = title.call("a", "MoveFromStorage", { boat: true, itemId: "item.wood", count: 2 });
    assertEqual(took.storage, { "item.wood": 3 }, "빼고 3 남음");
    assertEqual(title.count("a", "item.wood"), 2, "가방에 2 돌아옴");
    assertFailsClean(title, "a", "MoveFromStorage", { boat: true, itemId: "item.wood", count: 4 }, "not_enough_items", "보관함에 3뿐");
    for (let i = 0; i < 7; i++) {
        title.seed("a", "item.filler_" + i, 1);
        title.call("a", "MoveToStorage", { boat: true, itemId: "item.filler_" + i });
    }
    title.seed("a", "item.club", 1);
    assertFailsClean(title, "a", "MoveToStorage", { boat: true, itemId: "item.club" }, "storage_full", "9번째 칸");
    assertEqual(title.call("a", "MoveToStorage", { boat: true, itemId: "item.wood", count: 2 }).storage["item.wood"], 5, "가득 차도 같은 칸 쌓기는 된다");
});

// ══ 직접 낚시 ══════════════════════════════════════════════════════════════════
test("RollFish → CatchFish — 서식지 어종 1마리, 표는 한 번만", () => {
    const title = makeTitle();
    const sb = title.sandbox;
    title.seed("p", "item.rod_basic", 1);
    title.seed("p", "item.worm_bait", 3, { stack: true });
    const roll = title.call("p", "RollFish", { spotId: "pond_1", bait: "worm" });
    assertTrue(sb.FISH_HABITAT.fresh.indexOf(roll.fishItemId) !== -1, "민물 어종");
    assertEqual(title.count("p", "item.worm_bait"), 2, "미끼 1개 소모");
    title.age("p", "fishRoll", 5000);
    const caught = title.call("p", "CatchFish", { token: roll.token });
    assertEqual(caught.caught, roll.fishItemId, "그 어종을 준다");
    assertEqual(title.count("p", roll.fishItemId), 1, "가방 +1");
    assertTrue(title.getData("p", "codex").indexOf(roll.fishItemId) !== -1, "도감에 올라감");
    title.age("p", "fishRoll", 5000);
    assertFailsClean(title, "p", "CatchFish", { token: roll.token }, "token_invalid", "표 재사용");
    assertFailsClean(title, "p", "CatchFish", { token: "fake" }, "token_invalid", "가짜 표");
});

test("RollFish — 5초 간격, 낚싯대·미끼·먼바다 잠금", () => {
    const title = makeTitle();
    assertFailsClean(title, "p", "RollFish", { spotId: "pond_1" }, "no_rod", "낚싯대 없음");
    title.seed("p", "item.rod_basic", 1);
    assertFailsClean(title, "p", "RollFish", { spotId: "pond_1", bait: "berry" }, "not_enough_items", "미끼 없음");
    assertFailsClean(title, "p", "RollFish", { spotId: "far_1" }, "locked", "도감 50% 전 먼바다");
    assertFailsClean(title, "p", "RollFish", { spotId: "moon_1" }, "bad_args", "없는 포인트");
    title.call("p", "RollFish", { spotId: "coast_1" });
    assertFailsClean(title, "p", "RollFish", { spotId: "coast_1" }, "too_fast", "5초 안에 다시");
    title.setData("p", "codex", title.sandbox.FISH_HABITAT.fresh.concat(["item.haddock"]));
    title.age("p", "fishRoll", 5000);
    const far = title.call("p", "RollFish", { spotId: "far_1" });
    assertTrue(title.sandbox.FISH_TABLE[far.fishItemId].intervalMinutes >= 120, "먼바다는 120분 이상 어종");
});

test("CatchFish — 5초 간격, 칸이 없으면 표를 남긴다", () => {
    const title = makeTitle();
    title.seed("p", "item.rod_basic", 1);
    const r1 = title.call("p", "RollFish", { spotId: "river_1" });
    title.age("p", "fishRoll", 5000);
    title.call("p", "CatchFish", { token: r1.token });
    const r3 = title.call("p", "RollFish", { spotId: "river_1" });
    assertFailsClean(title, "p", "CatchFish", { token: r3.token }, "too_fast", "잡기 3초 만에 다시");
    title.age("p", "fishRoll", 5000);
    fillSlots(title, "p", 18); // 낚싯대 + 첫 물고기 + 18 = 20칸
    const before = title.getData("p", "fishRoll");
    if (title.count("p", before.fishItemId) === 0) {
        assertFailsClean(title, "p", "CatchFish", { token: r3.token }, "inventory_full", "칸 부족");
        title.call("p", "DropItem", { itemId: "item.filler_0" });
    }
    assertEqual(title.call("p", "CatchFish", { token: r3.token }).caught, before.fishItemId, "칸을 비우면 같은 표로 받는다");
});

test("도감 보상 — 낚시 50% 먼바다, 사냥+식물 50% 위험구역, 100% 특별 상인", () => {
    const title = makeTitle();
    const sb = title.sandbox;
    const none = title.call("p", "GetCodexRewards");
    assertEqual(none.rewards, { farSea: false, dangerDock: false, specialMerchant: false }, "처음엔 다 닫힘");
    const fishing = sb.CODEX_CATEGORIES.fishing;
    title.setData("p", "codex", fishing.slice(0, Math.ceil(fishing.length / 2) - 1));
    assertEqual(title.call("p", "GetCodexRewards").rewards.farSea, false, "50% 바로 아래");
    title.setData("p", "codex", fishing.slice(0, Math.ceil(fishing.length / 2)));
    assertEqual(title.call("p", "GetCodexRewards").rewards.farSea, true, "50%");
    const hp = sb.CODEX_CATEGORIES.hunt.concat(sb.CODEX_CATEGORIES.plant);
    title.setData("p", "codex", hp.slice(0, Math.ceil(hp.length / 2)));
    assertEqual(title.call("p", "GetCodexRewards").rewards.dangerDock, true, "사냥+식물 50%");
    title.setData("p", "codex", fishing);
    assertEqual(title.call("p", "GetCodexRewards").rewards.specialMerchant, true, "낚시 100%");
});

test("GetCodexRewards entries — 찬 칸 목록을 그대로 싣고, 칸 밖 ID 는 뺀다 (M13-26)", () => {
    const title = makeTitle();
    assertEqual(title.call("p", "GetCodexRewards").entries, [], "처음엔 빈 목록");
    const fish = title.sandbox.CODEX_CATEGORIES.fishing;
    title.setData("p", "codex", [fish[0], "item.pork", fish[1]]);
    assertEqual(title.call("p", "GetCodexRewards").entries, [fish[0], fish[1]], "칸만 순서대로");
    // 도감을 늘리는 판정(CatchFish) 뒤 다시 물으면 새 칸이 실린다 — 클라는 보유 재조회마다 이 답을 받는다.
    title.setData("p", "codex", []);
    title.seed("p", "item.rod_basic", 1);
    title.seed("p", "item.worm_bait", 1, { stack: true });
    const roll = title.call("p", "RollFish", { spotId: "pond_1", bait: "worm" });
    title.age("p", "fishRoll", 5000);
    title.call("p", "CatchFish", { token: roll.token });
    assertEqual(title.call("p", "GetCodexRewards").entries, [roll.fishItemId], "잡은 어종이 실린다");
});

// ══ 상점·선착장·배 ═════════════════════════════════════════════════════════════
test("BuyItem — 코인을 빼고 준다, 모자라면·칸 없으면 그대로", () => {
    const title = makeTitle();
    title.setCoins("p", 5);
    const r = title.call("p", "BuyItem", { itemId: "item.worm_bait", count: 2 });
    assertEqual([r.count, r.cost], [6, 2], "지렁이 팩 2 = 6개, 2코인");
    assertEqual([title.coins("p"), title.count("p", "item.worm_bait")], [3, 6], "잔액·수량");
    assertFailsClean(title, "p", "BuyItem", { itemId: "item.tent" }, "not_enough_coins", "코인 부족");
    title.setCoins("p", 100);
    fillSlots(title, "p", 19);
    assertFailsClean(title, "p", "BuyItem", { itemId: "item.tent" }, "inventory_full", "칸 부족");
    assertFailsClean(title, "p", "BuyItem", { itemId: "item.wood" }, "bad_args", "상점에 없는 것");
});

test("옷장 상점 — 설계서 10.7 칸 수·젬 값, 주마다 같은 진열, 능력치 없는 것만", () => {
    const title = makeTitle();
    const sb = title.sandbox;
    assertEqual(sb.WARDROBE_SHOP_SLOTS, designNumber("옷장 상점 칸 수", /\*\*(\d+)\*\*/), "칸 수");
    // 꾸미기 42벌 = 10.7b 칸 기본 + 등급 웃돈(생성기 DT_ItemTable 의 칸·등급으로 계산), 목장 꾸미기 3 = 10.7 줄에 적힌 값.
    const prices = {};
    designRow("옷장 상점 젬 값")[1].replace(/`(item\.[a-z0-9_]+)` (\d+)/g, (m, id, gems) => { prices[id] = Number(gems); });
    const gemRule = designRow("꾸미기 젬 값").join(" | "); // 웃돈은 근거 칸에 적혀 있다(감독 확정 2026-10-06)
    const big = Number(/모자·상의·배낭 \*\*(\d+)\*\*/.exec(gemRule)[1]);
    const small = Number(/하의·신발·장갑·얼굴 장식 \*\*(\d+)\*\*/.exec(gemRule)[1]);
    const extra = { Common: 0, Rare: Number(/희귀 \*\*\+(\d+)\*\*/.exec(gemRule)[1]), Epic: Number(/에픽 \*\*\+(\d+)\*\*/.exec(gemRule)[1]) };
    const cosmetics = dumpItemRows().filter((r) => r.ItemType === "Cosmetic");
    assertEqual(cosmetics.length, 181, "생성기 꾸미기 181벌(177 + M92-03 옷 4)");
    cosmetics.forEach((r) => { prices[r.ItemID] = (["Head", "OutfitTop", "Backpack"].includes(r.EquipSlot) ? big : small) + extra[r.Grade]; });
    assertEqual(Object.keys(prices).length, 184, "181벌 + 목장 꾸미기 3");
    const sorted = (o) => JSON.stringify(Object.keys(o).sort().map((k) => [k, o[k]]));
    assertEqual(sorted(sb.WARDROBE_SHOP_POOL), sorted(prices), "젬 값 표 = 풀");
    assertEqual(sb.WARDROBE_SHOP_POOL["item.harbor_whale"], 500, "에픽 모자 = 300 + 200");
    assertEqual(sb.WARDROBE_SHOP_POOL["item.meadow_petal_skirt"], 300, "희귀 하의 = 200 + 100");
    Object.keys(sb.WARDROBE_SHOP_POOL).forEach((id) => {
        assertTrue(!sb.HAT_EFFECTS[id] || Object.keys(sb.HAT_EFFECTS[id]).length === 0, id + " 능력치 없음");
        assertTrue(id !== sb.BACKPACK_BONUS_ITEM, id + " 칸 늘리는 배낭 아님");
    });
    const a = title.call("p", "GetWardrobeShop", {});
    const b = title.call("q", "GetWardrobeShop", {});
    assertEqual(JSON.stringify(a), JSON.stringify(b), "같은 주엔 모두 같은 진열");
    assertEqual(a.items.length, Math.min(sb.WARDROBE_SHOP_SLOTS, Object.keys(prices).length), "진열 수");
    assertEqual(new Set(a.items.map((o) => o.itemId)).size, a.items.length, "겹치지 않음");
    assertEqual(JSON.stringify(sb.wardrobeShopOf(a.week)), JSON.stringify(a.items), "주 번호로 정해진다");
});

test("BuyItem 젬 — 서버 값으로 빼고 준다, 모자라면·이미 있으면·진열 밖이면 그대로", () => {
    const title = makeTitle();
    // 목장 꾸미기(10.9)는 여러 개 살 수 있어 `already` 를 안 낸다 — 사람 꾸미기 진열을 고른다.
    const offer = title.call("p", "GetWardrobeShop", {}).items.find((o) => title.sandbox.RANCH_COSMETICS.indexOf(o.itemId) === -1);
    title.setGems("p", offer.gems - 1);
    assertFailsClean(title, "p", "BuyItem", { itemId: offer.itemId, currency: "GM" }, "not_enough_gems", "젬 부족");
    title.setGems("p", offer.gems + 50);
    title.setCoins("p", 0);
    const r = title.call("p", "BuyItem", { itemId: offer.itemId, count: 1, currency: "GM", gems: 1 });
    assertEqual([r.bought, r.count, r.cost, r.currency], [offer.itemId, 1, offer.gems, "GM"], "응답 — 값은 클라 gems 무시");
    assertEqual([title.gems("p"), title.count("p", offer.itemId), title.coins("p")], [50, 1, 0], "젬만 빠지고 1개");
    assertFailsClean(title, "p", "BuyItem", { itemId: offer.itemId, currency: "GM" }, "already", "이미 가진 것");
    title.setGems("p", 9999);
    assertFailsClean(title, "p", "BuyItem", { itemId: "item.merchant_hat", currency: "GM" }, "bad_args", "진열 밖(능력치 모자)");
    assertFailsClean(title, "p", "BuyItem", { itemId: offer.itemId, count: 2, currency: "GM" }, "bad_args", "두 개");
});

test("BuyItem 젬 — 목장 꾸미기(10.9)는 가져도 또 산다", () => {
    const title = makeTitle();
    const sb = title.sandbox;
    title.setGems("p", 9999);
    sb.WARDROBE_SHOP_SLOTS = Object.keys(sb.WARDROBE_SHOP_POOL).length; // 91종 풀에서 8칸만 걸리므로 시험에선 전부 진열
    sb.RANCH_COSMETICS.forEach((id) => {
        title.call("p", "BuyItem", { itemId: id, currency: "GM" });
        title.call("p", "BuyItem", { itemId: id, currency: "GM" });
        assertEqual(title.count("p", id), 2, id + " 두 개");
    });
    const spent = sb.RANCH_COSMETICS.reduce((sum, id) => sum + 2 * sb.WARDROBE_SHOP_POOL[id], 0);
    assertEqual(title.gems("p"), 9999 - spent, "서버 값만큼 빠짐");
});

// 10.9 목장 동물 꾸미기 — 서버가 칸 기록(`ranch[i].cosmetic`)과 배낭을 한 번에 맞바꾼다.
function ranchWithPet(title, pid) {
    title.seed(pid, "item.pet_pig", 1);
    title.call(pid, "RegisterPet", { slot: 0, animalId: "animal.pig" });
}

test("SetPetCosmetic — 씌우기·바꾸기·벗기기, 아이템은 사라지지 않는다", () => {
    const title = makeTitle();
    ranchWithPet(title, "p");
    title.seed("p", "item.ranch_ribbon", 1);
    title.seed("p", "item.ranch_hat", 1);
    const r = title.call("p", "SetPetCosmetic", { slot: 0, itemId: "item.ranch_ribbon" });
    assertEqual([r.slot, r.cosmetic, r.returned], [0, "item.ranch_ribbon", ""], "씌우기 응답");
    assertEqual(title.getData("p", "ranch")[0].cosmetic, "item.ranch_ribbon", "칸 기록");
    assertEqual(title.count("p", "item.ranch_ribbon"), 0, "배낭에서 빠짐");
    const base = title.call("p", "GetBaseState", {});
    assertEqual(base.ranch[0].cosmetic, "item.ranch_ribbon", "GetBaseState 에 실린다");
    title.call("p", "SetPetCosmetic", { slot: 0, itemId: "item.ranch_hat" });
    assertEqual([title.count("p", "item.ranch_ribbon"), title.count("p", "item.ranch_hat")], [1, 0], "바꾸면 옛 것 돌아옴");
    title.call("p", "SetPetCosmetic", { slot: 0, itemId: "" });
    assertEqual(title.count("p", "item.ranch_hat"), 1, "벗기면 돌아옴");
    assertEqual(title.getData("p", "ranch")[0].cosmetic, undefined, "칸 기록 지움");
});

test("SetPetCosmetic — 거절이면 칸·배낭 그대로", () => {
    const title = makeTitle();
    title.seed("p", "item.ranch_ribbon", 1);
    assertFailsClean(title, "p", "SetPetCosmetic", { slot: 0, itemId: "item.ranch_ribbon" }, "empty_slot", "동물 없는 칸");
    ranchWithPet(title, "p");
    assertFailsClean(title, "p", "SetPetCosmetic", { slot: 99, itemId: "item.ranch_ribbon" }, "bad_slot", "칸 밖");
    assertFailsClean(title, "p", "SetPetCosmetic", { slot: 0, itemId: "item.cook_hat" }, "bad_args", "사람 모자");
    assertFailsClean(title, "p", "SetPetCosmetic", { slot: 0, itemId: "item.ranch_hat" }, "not_enough_items", "배낭에 없음");
    assertFailsClean(title, "p", "SetPetCosmetic", { slot: 0, itemId: "" }, "already", "씌운 것 없이 벗기기");
    title.call("p", "SetPetCosmetic", { slot: 0, itemId: "item.ranch_ribbon" });
    title.seed("p", "item.ranch_ribbon", 1);
    assertFailsClean(title, "p", "SetPetCosmetic", { slot: 0, itemId: "item.ranch_ribbon" }, "already", "같은 것 다시");
    // 같은 꾸미기가 배낭에 있으면 그 칸에 쌓이므로, 배낭에 하나도 없는 사람으로 본다.
    const full = makeTitle();
    ranchWithPet(full, "q");
    full.seed("q", "item.ranch_ribbon", 1);
    full.call("q", "SetPetCosmetic", { slot: 0, itemId: "item.ranch_ribbon" });
    fillSlots(full, "q", full.sandbox.BACKPACK_SLOTS);
    assertFailsClean(full, "q", "SetPetCosmetic", { slot: 0, itemId: "" }, "inventory_full", "가득 찬 가방에 벗기기");
});

// M35-04: 공개 문서 「펫 의상」 경품 6종은 목장 꾸미기 1칸에 씌운다. 가챠 아이템이라 배낭 칸 없이 오간다.
test("SetPetCosmetic — 가챠 펫 의상 6종을 받는다(공개 문서 그대로)", () => {
    const sb = makeTitle().sandbox;
    const doc = fs.readFileSync(path.join(repoRoot, "docs", "GACHA_DISCLOSURE.md"), "utf8").split("\n")
        .map((line) => line.split("|").slice(1, -1).map((c) => c.trim()))
        .filter((cells) => cells.length === 5 && cells[3] === "펫 의상")
        .map((cells) => cells[0]);
    assertEqual(doc.length, 6, "공개 문서 펫 의상 줄 6개");
    assertEqual(sb.GACHA_PET_COSMETICS.slice().sort(), doc.slice().sort(), "가챠 펫 의상 ID = 문서");
    assertEqual(sb.GACHA_PET_COSMETICS.filter((id) => sb.COSMETIC_SHOP_ITEMS.indexOf(id) !== -1), [], "옷장 상점 풀에는 없다");
    const title = makeTitle();
    ranchWithPet(title, "p");
    let prev = "";
    doc.forEach((id) => {
        title.seed("p", id, 1);
        const r = title.call("p", "SetPetCosmetic", { slot: 0, itemId: id });
        assertEqual([r.cosmetic, r.returned], [id, prev], id + " 씌우기 응답");
        assertEqual([title.getData("p", "ranch")[0].cosmetic, title.count("p", id)], [id, 0], id + " 칸 기록 · 배낭에서 빠짐");
        if (prev !== "") assertEqual(title.count("p", prev), 1, prev + " 바꾸면 돌아옴");
        prev = id;
    });
    // 배낭이 가득 차도 가챠 아이템은 칸을 안 차지하므로 벗길 수 있다.
    fillSlots(title, "p", title.sandbox.BACKPACK_SLOTS);
    const off = title.call("p", "SetPetCosmetic", { slot: 0, itemId: "" });
    assertEqual([off.returned, title.count("p", prev)], [prev, 1], "가득 찬 가방에도 벗겨진다");
});

test("RemovePet — 씌운 꾸미기도 펫과 함께 돌아온다", () => {
    const title = makeTitle();
    ranchWithPet(title, "p");
    title.seed("p", "item.ranch_necklace", 1);
    title.call("p", "SetPetCosmetic", { slot: 0, itemId: "item.ranch_necklace" });
    const r = title.call("p", "RemovePet", { slot: 0 });
    assertEqual(r.cosmetic, "item.ranch_necklace", "응답");
    assertEqual([title.count("p", "item.pet_pig"), title.count("p", "item.ranch_necklace")], [1, 1], "펫·꾸미기 배낭");
});

// M35-05a 목장 데코 — 스킨은 해금이라 소비 0. 종류마다 한 칸(`ranchMeta.deco[kind]`), 생산 기록은 손대지 않는다.
test("SetRanchDeco — 공개 문서 데코 5종 = 서버 표, 종류 4칸", () => {
    const sb = makeTitle().sandbox;
    const doc = fs.readFileSync(path.join(repoRoot, "docs", "GACHA_DISCLOSURE.md"), "utf8").split("\n")
        .map((line) => line.split("|").slice(1, -1).map((c) => c.trim()))
        .filter((cells) => cells.length === 5 && cells[3] === "목장 데코")
        .map((cells) => cells[0]);
    assertEqual(doc.length, 5, "공개 문서 목장 데코 줄 5개");
    assertEqual(Object.keys(sb.GACHA_RANCH_DECOS).sort(), doc.slice().sort(), "서버 데코 ID = 문서");
    const kinds = Object.keys(sb.GACHA_RANCH_DECOS).map((id) => sb.GACHA_RANCH_DECOS[id]);
    assertEqual(Array.from(new Set(kinds)).sort(), sb.RANCH_DECO_KINDS.slice().sort(), "종류 = 울타리·바닥·헛간·배경");
});

test("SetRanchDeco — 적용·같은 종류 바꾸기·해제, 데코도 생산도 그대로", () => {
    const title = makeTitle();
    ranchWithPet(title, "p");
    title.seed("p", "gacha.deco_wood_fence", 1);
    title.seed("p", "gacha.deco_stone_fence", 1);
    title.seed("p", "gacha.deco_starlight_bg", 1);
    const ranchBefore = JSON.stringify(title.getData("p", "ranch"));
    const metaBefore = title.getData("p", "ranchMeta");
    const r = title.call("p", "SetRanchDeco", { itemId: "gacha.deco_wood_fence" });
    assertEqual([r.kind, r.itemId, r.replaced, r.deco], ["fence", "gacha.deco_wood_fence", "", { fence: "gacha.deco_wood_fence" }], "적용 응답");
    title.call("p", "SetRanchDeco", { itemId: "gacha.deco_starlight_bg" });
    const swap = title.call("p", "SetRanchDeco", { itemId: "gacha.deco_stone_fence" });
    assertEqual([swap.replaced, swap.deco], ["gacha.deco_wood_fence", { fence: "gacha.deco_stone_fence", sky: "gacha.deco_starlight_bg" }], "같은 종류는 한 칸 — 옛 것이 밀려남");
    ["gacha.deco_wood_fence", "gacha.deco_stone_fence", "gacha.deco_starlight_bg"].forEach((id) =>
        assertEqual(title.count("p", id), 1, id + " 소비 0"));
    assertEqual(title.call("p", "GetBaseState", {}).ranchDeco, { fence: "gacha.deco_stone_fence", sky: "gacha.deco_starlight_bg" }, "GetBaseState 에 실린다");
    const off = title.call("p", "SetRanchDeco", { itemId: "", kind: "fence" });
    assertEqual([off.replaced, off.deco], ["gacha.deco_stone_fence", { sky: "gacha.deco_starlight_bg" }], "해제");
    assertEqual(JSON.stringify(title.getData("p", "ranch")), ranchBefore, "목장 칸 기록(생산) 변화 0");
    const metaAfter = Object.assign({}, title.getData("p", "ranchMeta"));
    delete metaAfter.deco;
    assertEqual(metaAfter, metaBefore, "유지·명성·칸 수 변화 0");
    // 배낭에서 없어진 데코는 기록에 남아도 보이지 않는다.
    title.account("p").inventory = title.account("p").inventory.filter((it) => it.ItemId !== "gacha.deco_starlight_bg");
    assertEqual(title.call("p", "GetBaseState", {}).ranchDeco, {}, "가진 것만 보인다");
});

test("SetRanchDeco — 거절이면 계정 그대로, 가득 찬 가방·데코 없는 계정도 된다", () => {
    const title = makeTitle();
    assertEqual(title.call("p", "GetBaseState", {}).ranchDeco, {}, "처음엔 빈 데코");
    assertFailsClean(title, "p", "SetRanchDeco", { itemId: "gacha.deco_red_barn" }, "not_enough_items", "안 가진 데코");
    assertFailsClean(title, "p", "SetRanchDeco", { itemId: "gacha.pet_crown" }, "bad_args", "데코가 아닌 가챠");
    assertFailsClean(title, "p", "SetRanchDeco", { itemId: "", kind: "roof" }, "bad_args", "모르는 종류 해제");
    assertFailsClean(title, "p", "SetRanchDeco", { itemId: "", kind: "barn" }, "already", "적용 없이 해제");
    title.seed("p", "gacha.deco_red_barn", 1);
    fillSlots(title, "p", title.sandbox.BACKPACK_SLOTS);
    const on = title.call("p", "SetRanchDeco", { itemId: "gacha.deco_red_barn" });
    assertEqual(on.deco, { barn: "gacha.deco_red_barn" }, "가득 찬 가방에도 적용");
    assertFailsClean(title, "p", "SetRanchDeco", { itemId: "gacha.deco_red_barn" }, "already", "같은 것 다시");
    assertEqual(title.call("p", "SetRanchDeco", { itemId: "", kind: "barn" }).deco, {}, "가득 찬 가방에도 해제");
    assertEqual(title.count("p", "gacha.deco_red_barn"), 1, "해제해도 그대로");
});

test("BuyItem — 설계도는 100% 뒤 100 코인, 한 번만", () => {
    const title = makeTitle();
    title.setCoins("p", 250);
    assertFailsClean(title, "p", "BuyItem", { itemId: "item.blueprint_grill" }, "locked", "특별 상인 잠김");
    title.setData("p", "codex", title.sandbox.CODEX_CATEGORIES.fishing);
    const r = title.call("p", "BuyItem", { itemId: "item.blueprint_grill" });
    assertEqual(r.cost, 100, "100 코인");
    assertEqual(title.getData("p", "blueprints"), ["item.blueprint_grill"], "설계도 기록");
    assertEqual(title.coins("p"), 150, "잔액");
    assertFailsClean(title, "p", "BuyItem", { itemId: "item.blueprint_grill" }, "already", "같은 설계도 다시");
    title.setCoins("p", 99);
    assertFailsClean(title, "p", "BuyItem", { itemId: "item.blueprint_rod_bait" }, "not_enough_coins", "99 코인");
});

test("SellItem — 물고기·펫은 팔고, 나무판·상점 물건은 거절", () => {
    const title = makeTitle();
    title.seed("p", "item.perch", 3);
    const r = title.call("p", "SellItem", { itemId: "item.perch", count: 3 });
    assertEqual(r.coins, 6, "농어 2 × 3");
    assertEqual([title.coins("p"), title.count("p", "item.perch")], [6, 0], "잔액·수량");
    title.seed("p", "item.plank", 2);
    assertFailsClean(title, "p", "SellItem", { itemId: "item.plank" }, "not_sellable", "나무판(재료 값 0)");
    title.seed("p", "item.tent", 1);
    assertFailsClean(title, "p", "SellItem", { itemId: "item.tent" }, "not_sellable", "상점 물건");
    assertFailsClean(title, "p", "SellItem", { itemId: "item.perch" }, "not_enough_items", "없는 농어");
    title.seed("p", "item.pet_pig", 2);
    const pet = title.call("p", "SellItem", { itemId: "item.pet_pig", count: 2 });
    assertEqual([pet.coins, title.coins("p"), title.count("p", "item.pet_pig")], [20, 26, 0], "돼지 펫 10 × 2");
});

test("의뢰 — 하루 3개 고정, 건네면 빼고 코인, 같은 날 두 번 안 된다", () => {
    const title = makeTitle();
    const a = title.call("p", "GetQuests");
    assertEqual(a.quests.length, 3, "3개");
    assertEqual(title.call("p", "GetQuests").quests.map((q) => q.id), a.quests.map((q) => q.id), "같은 날 같은 셋");
    assertEqual(new Set(a.quests.map((q) => q.id)).size, 3, "겹치지 않는다");
    const sb = title.sandbox;
    assertEqual(sb.questReward("item.wood", 10), 5, "나무 10 = 0.5×10");
    assertEqual(sb.questReward("item.wheat", 2), 12, "밀 2 = 4×2×1.5");
    assertEqual(sb.questReward("item.pike", 1), 5, "강꼬치 1 = 3×1.5 올림");
    const q = a.quests[0];
    assertFailsClean(title, "p", "CompleteQuest", { questId: q.id }, "not_enough_items", "없이 건네기");
    title.seed("p", q.itemId, q.count, { stack: true });
    const r = title.call("p", "CompleteQuest", { questId: q.id });
    assertEqual([r.coins, title.coins("p"), title.count("p", q.itemId)], [q.reward, q.reward, 0], "보상·차감");
    assertEqual(title.call("p", "GetQuests").quests[0].done, true, "완료 표시");
    title.seed("p", q.itemId, q.count, { stack: true });
    assertFailsClean(title, "p", "CompleteQuest", { questId: q.id }, "already", "같은 날 두 번");
    const other = sb.QUEST_POOL.map((e) => e[0]).find((id) => !a.quests.some((x) => x.id === id));
    assertFailsClean(title, "p", "CompleteQuest", { questId: other }, "bad_args", "오늘 의뢰 아님");
    title.setData("p", "quests", { day: a.day - 1, done: a.quests.map((x) => x.id) });
    assertEqual(title.call("p", "GetQuests").quests.every((x) => !x.done), true, "날이 바뀌면 새로");
});

test("의뢰 줄기 — 5단계 순서대로, 재료 모자람·건너뛰기·끝난 뒤 재수령은 실패하고 아무것도 안 바뀐다", () => {
    const title = makeTitle();
    const sb = title.sandbox;
    assertEqual(title.call("p", "GetQuestChain").step, 0, "처음 0단계");
    assertFailsClean(title, "p", "CompleteQuestChainStep", {}, "chain_short", "재료 없이");
    title.seed("p", "item.wood", 29, { stack: true });
    title.seed("p", "item.berry", 10, { stack: true });
    assertFailsClean(title, "p", "CompleteQuestChainStep", {}, "chain_short", "나무 1 모자람");
    assertEqual([title.coins("p"), title.count("p", "item.wood")], [0, 29], "실패는 불변");
    assertFailsClean(title, "p", "CompleteQuestChainStep", { step: 3 }, "chain_locked", "건너뛰기");
    title.seed("p", "item.wood", 1, { stack: true });
    const r = title.call("p", "CompleteQuestChainStep", { step: 1 });
    assertEqual([r.step, r.coins, title.coins("p"), title.count("p", "item.wood"), title.count("p", "item.berry")], [1, 10, 10, 0, 0], "1단계");
    assertEqual(title.call("p", "GetQuestChain").current.step, 2, "다음은 2단계");
    assertFailsClean(title, "p", "CompleteQuestChainStep", { step: 1 }, "chain_locked", "재수령");
    for (let i = 1; i < 5; i++) {
        sb.QUEST_CHAIN[i].needs.forEach((n) => title.seed("p", n[0], n[1], { stack: true }));
        title.call("p", "CompleteQuestChainStep", { step: i + 1 });
    }
    assertEqual(title.coins("p"), 80, "코인 합 80");
    assertEqual(title.count("p", "item.vault_pass"), 1, "열쇠패 1장");
    assertEqual(title.call("p", "GetQuestChain").done, true, "끝");
    assertFailsClean(title, "p", "CompleteQuestChainStep", {}, "chain_done", "끝난 뒤");
});

test("메인 스토리 — 보고·카운터·★ 건네기 판정, 장 끝 보상, 건너뛰기·모자람은 실패하고 불변", () => {
    const title = makeTitle();
    const sb = title.sandbox;
    assertEqual([title.call("p", "GetStory").step, sb.STORY_STEPS.length], [0, 25], "처음 0 / 25단계");
    assertFailsClean(title, "p", "AdvanceStory", {}, "story_short", "보고 없이 가기 단계");
    assertFailsClean(title, "p", "AdvanceStory", { step: 3 }, "story_locked", "건너뛰기");
    assertEqual(title.call("p", "AdvanceStory", { step: 1, report: true }).step, 1, "1단계 보고");
    assertFailsClean(title, "p", "AdvanceStory", {}, "story_short", "만들기 안 했는데");
    title.setData("p", "achievements", { counters: { craft: 1 }, done: [] });
    assertEqual(title.call("p", "AdvanceStory", {}).step, 2, "손도끼 만들기(카운터 +1)");
    assertFailsClean(title, "p", "AdvanceStory", {}, "story_short", "★ 재료 없이");
    sb.QUEST_CHAIN[0].needs.forEach((n) => title.seed("p", n[0], n[1], { stack: true }));
    const g = title.call("p", "AdvanceStory", {});
    assertEqual([g.step, g.coins, title.coins("p")], [3, 10, 10], "★ 땔감 = 10코인");
    title.setData("p", "achievements", { counters: { craft: 1, capture: 1 }, done: [] });
    assertEqual(title.call("p", "AdvanceStory", {}).step, 4, "잡기 1마리 (시작 때보다 늘어야)");
    assertFailsClean(title, "p", "AdvanceStory", {}, "story_short", "목장에 동물 없이");
    title.setData("p", "ranch", [{ slot: 0, animalId: "animal.rabbit" }]);
    const e = title.call("p", "AdvanceStory", {});
    assertEqual([e.step, e.chapterCoins, title.coins("p")], [5, 20, 30], "1장 끝 보상 20");
    assertEqual(e.chapter, 2, "2장으로");
});

test("메인 스토리 — 옛 의뢰 줄기 n단계 계정은 n 번째 ★ 까지 끝난 것으로 이어받고, 이미 받은 ★ 은 두 번 안 준다", () => {
    const title = makeTitle();
    const sb = title.sandbox;
    assertEqual([0, 1, 2, 3, 4, 5].map((n) => sb.storyStepFromChain(n)), [0, 3, 3, 12, 17, 21], "이어받기 표 — ★ 순서 0·2·1·3·4, 안 받은 ★ 은 건너뛰지 않음");
    title.setData("p", "questChain", { step: 1 });
    assertEqual(title.call("p", "GetStory").step, 3, "1단계 깬 계정 = 3단계 끝");
    const t2 = makeTitle();
    t2.setData("p", "questChain", { step: 1 });
    t2.call("p", "GetStory");
    t2.setData("p", "story", { step: 2, base: {}, paid: [0] });
    const r = t2.call("p", "AdvanceStory", {});
    assertFailsClean(t2, "p", "CompleteQuestChainStep", {}, "chain_done", "스토리로 옮겨 간 뒤 옛 줄기 재수령");
    assertEqual([r.step, r.coins, t2.coins("p")], [3, 0, 0], "이미 받은 ★ 은 재료·보상 없이 지나감");
});

test("메인 스토리 — 25단계 끝까지, 장 끝 보상 합 200 · 끝난 뒤 실패", () => {
    const title = makeTitle();
    const sb = title.sandbox;
    let counters = { craft: 0, capture: 0, petKinds: 0, harvest: 0, captureFogbuck: 0, harvestFogbuck: 0 };
    let ranchN = 0;
    for (let i = 0; i < 25; i++) {
        const st = sb.STORY_STEPS[i];
        if (st.kind === "give") sb.QUEST_CHAIN[st.chain].needs.forEach((n) => title.seed("p", n[0], n[1], { stack: true }));
        if (st.kind === "count") { counters[st.counter] += st.n; title.setData("p", "achievements", { counters: Object.assign({}, counters), done: [] }); }
        if (st.kind === "ranch") { ranchN = st.n; title.setData("p", "ranch", Array.from({ length: ranchN }, (_, k) => ({ slot: k, animalId: "animal.rabbit" }))); }
        const r = title.call("p", "AdvanceStory", { step: i + 1, report: true });
        assertEqual(r.step, i + 1, "단계 " + (i + 1));
    }
    assertEqual(title.coins("p"), 80 + 200, "★ 5개 80 + 장 끝 200");
    sb.STORY_CHAPTER_ITEMS.forEach((id) => assertEqual(title.count("p", id), 1, "장 끝 꾸미기 " + id));
    assertEqual(title.call("p", "GetStory").titles, [sb.STORY_FINAL_TITLE], "5장 끝 칭호");
    assertEqual(title.call("p", "GetStory").done, true, "끝");
    assertFailsClean(title, "p", "AdvanceStory", {}, "story_done", "끝난 뒤");
});

test("메인 스토리 — 9단계는 서로 다른 종 3종 포획만 센다(같은 종 여러 마리·변종은 한 종)", () => {
    const title = makeTitle();
    const sb = title.sandbox;
    assertEqual([sb.STORY_STEPS[8].counter, sb.STORY_STEPS[8].n], ["petKinds", 3], "9단계 카운터");
    title.setData("p", "story", { step: 8, base: {}, paid: [0, 1, 2] });
    const claim = (itemId, count) => title.call("p", "ClaimLoot", { items: [{ itemId: itemId, count: count }] });
    title.setData("p", "lootAt", { atMs: 0, n: 0 });
    claim("item.pet_fox", 3);
    assertFailsClean(title, "p", "AdvanceStory", {}, "story_short", "여우만 3마리");
    assertEqual(title.call("p", "GetStory").current.have, 1, "여우 3마리 = 1종");
    title.setData("p", "lootAt", { atMs: 0, n: 0 });
    claim("item.pet_fox_variant", 1);
    assertEqual(title.call("p", "GetStory").current.have, 1, "여우 변종도 같은 종");
    title.setData("p", "lootAt", { atMs: 0, n: 0 });
    claim("item.pet_goat", 1);
    title.setData("p", "lootAt", { atMs: 0, n: 0 });
    claim("item.pet_reindeer", 1);
    assertEqual(title.call("p", "GetStory").current.have, 3, "여우·염소·순록 = 3종");
    assertEqual(title.call("p", "AdvanceStory", {}).step, 9, "3종이면 통과");
});

test("메인 스토리 — 23단계는 안개뿔 포획만 센다(일반 포획은 안 됨)", () => {
    const sb = makeTitle().sandbox;
    assertEqual([sb.STORY_STEPS[22].kind, sb.STORY_STEPS[22].counter], ["count", "captureFogbuck"], "23단계 카운터");
    assertEqual(sb.isLootItem(sb.FOGBUCK_PET_ITEM), true, "안개뿔 펫은 받을 수 있다");
    assertEqual(sb.countListed([{ itemId: sb.FOGBUCK_PET_ITEM, count: 1 }, { itemId: "item.pet_goat", count: 2 }], sb.FOGBUCK_PET_ITEM), 1, "안개뿔만 센다");
});

test("메인 스토리 — 24단계는 안개뿔 수확만 센다(다른 동물 수확은 안 됨)", () => {
    const title = makeTitle();
    assertEqual([title.sandbox.STORY_STEPS[23].kind, title.sandbox.STORY_STEPS[23].counter], ["count", "harvestFogbuck"], "24단계 카운터");
    title.setData("p", "ranchMeta", { upkeepUntilUtc: null, fame: 0, slotCap: 5, firstBatchUsed: true });
    title.seed("p", "item.pet_pig", 1);
    title.call("p", "RegisterPet", { animalId: "animal.pig", slot: 0 });
    title.age("p", "ranch", 30 * 60 * 1000);
    title.call("p", "HarvestRanch", { slot: 0 });
    const c1 = title.getData("p", "achievements").counters;
    assertEqual([c1.harvest, c1.harvestFogbuck || 0], [1, 0], "일반 수확은 안개뿔 카운터를 안 올린다");
    title.seed("p", title.sandbox.FOGBUCK_PET_ITEM, 1);
    title.call("p", "RegisterPet", { animalId: "animal.fogbuck", slot: 1 });
    title.age("p", "ranch", 30 * 60 * 1000);
    title.call("p", "HarvestRanch", { slot: 1 });
    assertEqual(title.getData("p", "achievements").counters.harvestFogbuck, 1, "안개뿔 수확 1");
});

test("의뢰 줄기 — 서버 표가 DESIGN 8.14 줄과 같다", () => {
    const sb = makeTitle().sandbox;
    const names = ["땔감 부탁", "물가 부탁", "밭 부탁", "사냥 부탁", "큰 부탁"];
    const coins = [10, 12, 16, 20, 22];
    names.forEach((name, i) => {
        const cells = designRow(String(i + 1) + "단계 「" + name + "」");
        const text = cells[1];
        assertEqual(sb.QUEST_CHAIN[i].coins, Number(/→ \*\*(\d+)코인/.exec(text)[1]), name + " 코인");
        assertEqual(sb.QUEST_CHAIN[i].coins, coins[i], name + " 코인 표");
        const counts = (text.split("→")[0].match(/\*\*(\d+)\*\*/g) || []).map((x) => Number(x.replace(/\*/g, "")));
        assertEqual(sb.QUEST_CHAIN[i].needs.map((n) => n[1]), counts, name + " 재료 개수");
    });
});

test("7일 출석 — 보상 표가 DESIGN 11 과 같다", () => {
    const sb = makeTitle().sandbox;
    const want = designRow("출석 보상")[1].split(" · ").filter((p) => /^\d일 /.test(p)).map((part) => {
        const coin = /^\d일 코인 (\d+)$/.exec(part);
        if (coin) return { coins: Number(coin[1]) };
        const item = /^\d일 `([a-z0-9_.]+)` ×(\d+)$/.exec(part);
        return { itemId: item[1], count: Number(item[2]) };
    });
    assertEqual(sb.ATTENDANCE_REWARDS, want, "7칸");
    assertEqual(sb.ATTENDANCE_HAT_REPEAT_COINS, designNumber("출석 7일 모자 두 번째부터", /코인 (\d+)/), "두 번째 모자 칸");
});

test("7일 출석 — 같은 날 두 번 거절, 빠져도 다음 칸, 8일째 1일로", () => {
    const title = makeTitle();
    const rewards = title.sandbox.ATTENDANCE_REWARDS;
    const first = title.call("p", "GetAttendance");
    assertEqual([first.nextSlot, first.claimedToday], [1, false], "새 계정 1일");
    const r1 = title.call("p", "ClaimAttendance");
    assertEqual([r1.slot, title.coins("p")], [1, rewards[0].coins], "1일 코인");
    assertEqual(title.call("p", "GetAttendance").claimedToday, true, "받음 표시");
    assertFailsClean(title, "p", "ClaimAttendance", {}, "already", "같은 날 두 번");
    let coins = rewards[0].coins;
    for (let slot = 2; slot <= 7; slot++) {
        const st = title.getData("p", "attendance");
        title.setData("p", "attendance", Object.assign(st, { lastDay: st.lastDay - 3 })); // 사흘 빠져도
        const r = title.call("p", "ClaimAttendance");
        assertEqual(r.slot, slot, slot + "일 칸");
        const want = rewards[slot - 1];
        if (want.coins) coins += want.coins;
        else assertEqual(title.count("p", want.itemId), want.count, slot + "일 " + want.itemId);
        assertEqual(title.coins("p"), coins, slot + "일 잔액");
    }
    const st = title.getData("p", "attendance");
    title.setData("p", "attendance", Object.assign(st, { lastDay: st.lastDay - 1 }));
    assertEqual(title.call("p", "ClaimAttendance").slot, 1, "8일째 = 1일");
    const st14 = title.getData("p", "attendance");
    title.setData("p", "attendance", Object.assign(st14, { claims: 13, lastDay: st14.lastDay - 1 }));
    const again = title.call("p", "ClaimAttendance");
    assertEqual([again.slot, again.reward.coins, title.count("p", title.sandbox.ATTENDANCE_HAT)],
        [7, title.sandbox.ATTENDANCE_HAT_REPEAT_COINS, 1], "두 번째 7일 = 코인, 모자 그대로 1");
});

test("7일 출석 — 배낭에 새싹 모자가 이미 있으면 7일째 = 코인 150", () => {
    const title = makeTitle();
    const sb = title.sandbox;
    title.setData("p", "attendance", { claims: 6, lastDay: 1, hatGranted: false });
    title.seed("p", sb.ATTENDANCE_HAT, 1); // 꾸미기 상자·옷장으로 먼저 산 모자
    assertEqual(title.call("p", "GetAttendance").next, { coins: sb.ATTENDANCE_HAT_REPEAT_COINS }, "미리보기도 코인");
    const coins = title.coins("p");
    const r = title.call("p", "ClaimAttendance");
    assertEqual([r.slot, r.reward, title.coins("p") - coins, title.count("p", sb.ATTENDANCE_HAT)],
        [7, { coins: sb.ATTENDANCE_HAT_REPEAT_COINS }, sb.ATTENDANCE_HAT_REPEAT_COINS, 1], "산 모자 계정 7일째 = 코인 150, 모자 그대로 1");
});

test("7일 출석 — 가방이 차면 씨앗 칸을 안 넘긴다", () => {
    const title = makeTitle();
    title.setData("p", "attendance", { claims: 3, lastDay: 1, hatGranted: false });
    fillSlots(title, "p", title.sandbox.BACKPACK_SLOTS);
    assertFailsClean(title, "p", "ClaimAttendance", {}, "inventory_full", "가득 찬 가방 4일째");
    assertEqual(title.call("p", "GetAttendance").nextSlot, 4, "칸 그대로");
});

test("우두머리 보상 — 숫자가 DESIGN 10.6 과 같고 주는 UTC 월요일에 바뀐다", () => {
    const sb = makeTitle().sandbox;
    const hat = designRow("우두머리 모자")[1];
    assertEqual(sb.BOSS_HAT_CHANCE, Number(/\*\*(\d+)%\*\*/.exec(hat)[1]) / 100, "모자 %");
    assertEqual(sb.BOSS_HAT, /`([a-z0-9_.]+)`/.exec(hat)[1], "모자 ID");
    assertEqual(sb.BOSS_HAT_REPEAT_COINS, Number(/두 번째는 코인 (\d+)/.exec(hat)[1]), "두 번째 당첨 코인");
    assertTrue(designRow("우두머리 보상 주기")[1].indexOf("UTC 월요일 00:00") !== -1, "10.6 주 경계 글");
    const day = (iso) => Math.floor(Date.parse(iso) / 86400000);
    assertEqual(sb.bossWeekOf(day("2026-10-05T00:00:00Z")) - sb.bossWeekOf(day("2026-10-04T23:59:59Z")), 1, "일 → 월 넘어가면 다음 주");
    assertEqual(sb.bossWeekOf(day("2026-10-11T23:59:59Z")), sb.bossWeekOf(day("2026-10-05T00:00:00Z")), "월~일 같은 주");
});

test("우두머리 보상 — 씨앗 고정 1만 번 모자 10%±1%p", () => {
    const title = makeTitle();
    seedRandom(title, 330);
    let won = 0;
    for (let i = 0; i < 10000; i++) {
        title.setData("p", "boss", { lastWeek: null, hatGranted: false });
        title.account("p").inventory.length = 0; // 주 보상 열쇠패가 쌓여 칸이 차지 않게
        if (title.call("p", "ClaimBoss").won) won++;
    }
    assertTrue(Math.abs(won / 10000 - title.sandbox.BOSS_HAT_CHANCE) <= 0.01, "당첨률 " + won / 10000);
});

test("우두머리 보상 — 같은 주 두 번째 거절, 다음 주 다시, 이미 모자면 코인", () => {
    const title = makeTitle();
    const sb = title.sandbox;
    setRandom(title, 0);
    const r1 = title.call("p", "ClaimBoss");
    assertEqual([r1.won, r1.reward, title.count("p", sb.BOSS_HAT)], [true, { itemId: sb.BOSS_HAT, count: 1 }, 1], "첫 당첨 = 모자");
    assertFailsClean(title, "p", "ClaimBoss", {}, "already_claimed", "같은 주 두 번");
    const st = title.getData("p", "boss");
    title.setData("p", "boss", Object.assign(st, { lastWeek: st.lastWeek - 1 }));
    const r2 = title.call("p", "ClaimBoss");
    assertEqual([r2.reward, title.coins("p"), title.count("p", sb.BOSS_HAT)], [{ coins: sb.BOSS_HAT_REPEAT_COINS }, sb.BOSS_HAT_REPEAT_COINS, 1], "다음 주 당첨 = 코인, 모자 그대로");
    setRandom(title, 0.99);
    const st2 = title.getData("p", "boss");
    title.setData("p", "boss", Object.assign(st2, { lastWeek: st2.lastWeek - 1 }));
    const r3 = title.call("p", "ClaimBoss");
    assertEqual([r3.won, r3.reward, title.coins("p")], [false, null, sb.BOSS_HAT_REPEAT_COINS], "꽝 = 일반 드롭만");
    assertFailsClean(title, "p", "ClaimBoss", {}, "already_claimed", "꽝도 그 주 기록");
});

test("우두머리 보상 — 가방이 차면 모자 당첨이 그 주를 안 쓴다", () => {
    const title = makeTitle();
    setRandom(title, 0);
    fillSlots(title, "p", title.sandbox.BACKPACK_SLOTS);
    assertFailsClean(title, "p", "ClaimBoss", {}, "inventory_full", "가득 찬 가방 당첨");
    assertEqual(title.getData("p", "boss"), undefined, "주 기록 없음");
});

// 판매가 전수 대조 — 클라이언트 `DT_ItemTable.SellPrice` 를 만드는 생성기를 에디터 없이 돌려(dump_item_rows.py)
// 행마다 서버 `sellPriceOf` 와 맞댄다. 가게 화면 값과 실제 지급 코인이 어긋나면 운다(#0193 우유·크루아상).
// 환경변수 SNARE_ITEMS_SCRIPT = 역검증용 생성기 사본(행 하나 뺀 것). 없으면 실제 create_data_tables.py.
let itemRowsCache = null;
function dumpItemRows() {
    if (!itemRowsCache) {
        const args = [path.join(repoRoot, "tools", "playfab", "dump_item_rows.py")];
        if (process.env.SNARE_ITEMS_SCRIPT) args.push(path.resolve(process.env.SNARE_ITEMS_SCRIPT));
        itemRowsCache = JSON.parse(require("child_process").execFileSync("python3", args,
            { encoding: "utf8", maxBuffer: 16 << 20 }));
    }
    return itemRowsCache;
}

test("판매가 — DT_ItemTable 전 행 = 서버 sellPriceOf", () => {
    const rows = dumpItemRows();
    assertTrue(rows.length > 200, "생성기 행 " + rows.length + "개(빈 통과 방지)");
    const sb = makeTitle().sandbox;
    const wrong = rows.filter((r) => sb.sellPriceOf(r.ItemID) !== (r.SellPrice || 0))
        .map((r) => r.ItemID + " 표 " + (r.SellPrice || 0) + "/서버 " + sb.sellPriceOf(r.ItemID));
    assertEqual(wrong, [], "표↔서버 판매가 어긋남");
});

// M22 ① ClaimLoot 허용 목록 — 받을 수 있는 것 = 생성기 동물 행(드롭·펫) + 채집 행(지급·덤·고르기) 뿐.
// 이름 꼴(`_meat`·`pet_`)만 맞춘 지어낸 ID 는 거절한다(클라이언트가 보낸 종류를 믿지 않는다, 규칙 8).
function dumpGeneratorRows(name) {
    const args = [path.join(repoRoot, "tools", "playfab", "dump_item_rows.py"), "--rows=" + name];
    if (process.env.SNARE_ITEMS_SCRIPT) args.push(path.resolve(process.env.SNARE_ITEMS_SCRIPT));
    return JSON.parse(require("child_process").execFileSync("python3", args, { encoding: "utf8", maxBuffer: 16 << 20 }));
}

// 루프#539 잡화점 팔기 탭의 가챠 소재 줄 — 값은 클라 `DT_GachaPool.SellPrice`(아이템 표 행이 없어서). 풀 19행 전부 서버
// `sellPriceOf` 와 같아야 화면 값 = 실제 지급 코인이다. 소재 5종만 값이 있고(> 0) 의상·데코·코인 환급은 0(못 판다).
test("판매가 — DT_GachaPool 전 행 = 서버 sellPriceOf (희귀 소재 5종만 > 0)", () => {
    const rows = dumpGeneratorRows("gacha_pool_rows");
    const sb = makeTitle().sandbox;
    assertEqual(rows.length, sb.GACHA_POOL.length, "생성기 풀 행 수 = 서버 풀");
    const wrong = rows.filter((r) => sb.sellPriceOf(r.ItemID) !== r.SellPrice)
        .map((r) => r.ItemID + " 표 " + r.SellPrice + "/서버 " + sb.sellPriceOf(r.ItemID));
    assertEqual(wrong, [], "풀 표↔서버 판매가 어긋남");
    assertEqual(rows.filter((r) => r.SellPrice > 0).map((r) => r.ItemID).sort(), Object.keys(sb.GACHA_MATERIAL_PRIZES).sort(),
        "값 있는 행 = 소재 경품 5종");
});

// M32-03 상점 줄 대조 — 클라 `DT_ShopTable`(생성기 SHOP_ITEMS) 과 서버 `BuyItem` 목록이 같은 물건·값·개수인지.
// 한쪽에만 있으면 가게 화면에 떴는데 서버가 거절하거나(클라만), 살 수 있는데 화면에 안 뜬다(서버만).
test("상점 줄 — 생성기 SHOP_ITEMS = 서버 SHOP_ITEMS (M32-03 채집 바구니 포함)", () => {
    const sb = makeTitle().sandbox;
    const table = dumpGeneratorRows("SHOP_ITEMS").map((r) => "item." + r[0] + "=" + r[1] + "x" + r[2]).sort();
    const server = Object.keys(sb.SHOP_ITEMS).map((id) => id + "=" + sb.SHOP_ITEMS[id].price + "x" + sb.SHOP_ITEMS[id].count).sort();
    assertTrue(table.length > 50, "생성기 상점 줄 " + table.length + "개(빈 통과 방지)");
    assertEqual(table, server, "표↔서버 상점 줄");
    assertEqual(sb.SHOP_ITEMS["item.gather_basket"].price, sb.SHOP_ITEMS["item.hatchet"].price, "채집 바구니 = 손도끼 값(8.14 근거)");
});

test("ClaimLoot 허용 목록 (M22 ①) — 동물·채집 행 ID 는 다 받고, 행에 없는 ID 는 이름 꼴이 맞아도 거절", () => {
    const sb = makeTitle().sandbox;
    const rowIds = new Set();
    dumpGeneratorRows("ANIMAL_ROWS").forEach((r) => { r.DeathDropItems.forEach((id) => rowIds.add(id)); rowIds.add(r.PetItemID); });
    dumpGeneratorRows("GATHERABLE_ROWS").forEach((r) => {
        [r.GrantItemID, r.ExtraItemID].forEach((id) => { if (id) rowIds.add(id); });
        (r.GrantOptions || []).forEach((o) => rowIds.add(o.ItemID));
    });
    assertTrue(rowIds.size > 70, "행 ID " + rowIds.size + "개(빈 통과 방지)");
    const refused = [...rowIds].filter((id) => !sb.isLootItem(id));
    assertEqual(refused, [], "행에 있는데 서버가 거절");
    const tableIds = new Set(dumpItemRows().map((r) => r.ItemID));
    const allowed = sb.ANIMAL_DROP_ITEMS.concat(sb.CAPTURE_PET_ITEMS, sb.HUNT_TROPHIES, sb.GATHER_ITEMS);
    assertEqual(allowed.filter((id) => !tableIds.has(id)), [], "허용 목록인데 DT_ItemTable 에 없음");
    console.log("    허용 목록 " + allowed.length + "개 · 동물·채집 행 ID " + rowIds.size + "개 · 거절 0");
    ["item.dragon_meat", "item.pet_dragon", "item.pet_", "item.goat_hide", "item.cooked_bird", "item.shed_antler", "item.truffle"]
        .forEach((id) => assertFailsClean(makeTitle(), "p", "ClaimLoot", { items: [{ itemId: id, count: 1 }] }, "bad_args", id + " 은 못 받는다"));
    assertFailsClean(makeTitle(), "p", "ClaimLoot", { items: [{ itemId: "item.fox_meat", count: 1 }, { itemId: "item.pet_unicorn", count: 1 }] },
        "bad_args", "섞여 와도 묶음 통째 거절");
});

// 지시(감독 10-20 19:30) ① 서버 판정 구멍 — 사냥 드롭은 동물 ID 만 받고 서버 표로 굴린다(루프#1446).
test("ClaimLoot kills (지시 ①) — 서버 드롭 표 = 생성기 동물 행 전부(아이템·개수·최상급 확률)", () => {
    const sb = makeTitle().sandbox;
    const rows = dumpGeneratorRows("ANIMAL_ROWS");
    assertTrue(rows.length >= 16, "동물 행 " + rows.length + "개(빈 통과 방지)");
    assertEqual(rows.map((r) => r.AnimalID).sort(), Object.keys(sb.KILL_DROPS).sort(), "생성기 동물 ID = 서버 드롭 표 칸");
    const wrong = rows.filter((r) => JSON.stringify(r.DeathDropItems) !== JSON.stringify(sb.KILL_DROPS[r.AnimalID])
        || r.DeathDropCountMin !== sb.KILL_DROP_MIN || r.DeathDropCountMax !== sb.KILL_DROP_MAX
        || r.PristineDropChance !== sb.KILL_PRISTINE_CHANCE).map((r) => r.AnimalID);
    assertEqual(wrong, [], "행 ↔ 서버 드롭 표 어긋남");
    console.log("    동물 " + rows.length + "종 · 개수 " + sb.KILL_DROP_MIN + "~" + sb.KILL_DROP_MAX + " · 최상급 " + sb.KILL_PRISTINE_CHANCE);
});

/** 다음 Math.random 값을 차례로 정한다 — 다 쓰면 진짜 난수(뒤 상자 굴림 등). */
function queueRandom(title, values) {
    vm.runInContext("(function(){var q=" + JSON.stringify(values) + ";var r=Math.random;Math.random=function(){return q.length?q.shift():r();};})();", title.sandbox);
}

test("ClaimLoot kills (지시 ①) — 동물 ID 만 보내면 서버가 굴려 준다 · 사냥꾼 모자 최상급 배수 · 지어낸 종·한도 넘침은 통째 거절", () => {
    const sb = makeTitle().sandbox;
    // 여우 1마리: 개수 굴림 0.99 → 3개 · [최상급 0.01 → 최상급 꼬리] · [0.5 → 일반 첫째 고기] · [0.5 → 일반 둘째 꼬리]
    const title = makeTitle();
    queueRandom(title, [0.99, 0.01, 0.0, 0.5, 0.0, 0.5, 0.99]);
    const r = title.call("p", "ClaimLoot", { kills: [{ animalId: "animal.fox", count: 1 }] });
    assertEqual(r.granted, [{ itemId: "item.pristine_fox_tail", count: 1 }, { itemId: "item.fox_meat", count: 1 }, { itemId: "item.fox_tail", count: 1 }], "서버가 굴린 3개");
    assertEqual(title.count("p", "item.pristine_fox_tail"), 1, "가방에 최상급 +1");
    assertEqual(title.getData("p", "lootAt").n, 3, "하루 받기 개수 = 굴린 개수");
    assertEqual(title.getData("p", "achievements").counters.hunt, 3, "사냥 업적 = 드롭 개수(옛 items 길과 같음)");
    // 최상급 굴림 0.06: 맨머리는 0.05 아래가 아니라 일반, 사냥꾼 모자(×1.3 = 0.065)는 최상급.
    const plain = makeTitle();
    queueRandom(plain, [0.0, 0.06, 0.0]);
    assertEqual(plain.call("p", "ClaimLoot", { kills: [{ animalId: "animal.fox", count: 1 }] }).granted, [{ itemId: "item.fox_meat", count: 1 }], "맨머리 0.06 → 일반");
    const hat = makeTitle();
    hat.seed("p", "item.hunter_hat", 1);
    hat.call("p", "EquipHat", { itemId: "item.hunter_hat" });
    queueRandom(hat, [0.0, 0.06, 0.0]);
    assertEqual(hat.call("p", "ClaimLoot", { kills: [{ animalId: "animal.fox", count: 1 }] }).granted, [{ itemId: "item.pristine_fox_tail", count: 1 }],
        "사냥꾼 모자 0.06 → 최상급 (" + sb.KILL_PRISTINE_CHANCE + " × " + sb.HAT_EFFECTS["item.hunter_hat"].pristineMul + ")");
    // 최상급이 없는 늑대는 확률 굴림 없이 일반만: 개수 0.0 → 1개, 고르기 0.99 → 이빨.
    const wolf = makeTitle();
    queueRandom(wolf, [0.0, 0.99]);
    assertEqual(wolf.call("p", "ClaimLoot", { kills: [{ animalId: "animal.wolf", count: 1 }] }).granted, [{ itemId: "item.wolf_tooth", count: 1 }], "늑대 — 최상급 없음");
    // 채집 items 와 같이 와도 한 번에 받는다.
    const mixed = makeTitle();
    queueRandom(mixed, [0.0, 0.0]); // 돼지도 최상급 없음 — 개수 1 · 고르기 첫째
    const m = mixed.call("p", "ClaimLoot", { items: [{ itemId: "item.wood", count: 2 }], kills: [{ animalId: "animal.pig", count: 1 }] });
    assertEqual(m.granted, [{ itemId: "item.wood", count: 2 }, { itemId: "item.pig_meat", count: 1 }], "채집 + 사냥 한 묶음");
    // 거절 — 확인이 다 끝나기 전엔 계정이 한 글자도 안 바뀐다.
    const perCall = Math.floor(sb.LOOT_MAX_PER_CALL / sb.KILL_DROP_MAX);
    assertFailsClean(makeTitle(), "p", "ClaimLoot", { kills: [{ animalId: "animal.dragon", count: 1 }] }, "bad_args", "지어낸 종");
    assertFailsClean(makeTitle(), "p", "ClaimLoot", { kills: [{ animalId: "constructor", count: 1 }] }, "bad_args", "객체 속성 이름");
    assertFailsClean(makeTitle(), "p", "ClaimLoot", { kills: [{ animalId: "animal.fox", count: 0 }] }, "bad_args", "0마리");
    assertFailsClean(makeTitle(), "p", "ClaimLoot", { kills: [] }, "bad_args", "빈 요청");
    assertFailsClean(makeTitle(), "p", "ClaimLoot", { kills: [{ animalId: "animal.fox", count: perCall + 1 }] }, "too_many", (perCall + 1) + "마리 × 최대 " + sb.KILL_DROP_MAX);
    assertFailsClean(makeTitle(), "p", "ClaimLoot", { kills: [{ animalId: "animal.fox", count: 1 }, { animalId: "animal.unicorn", count: 1 }] }, "bad_args", "섞여 와도 통째 거절");
    const ok = makeTitle();
    assertTrue(Array.isArray(ok.call("p", "ClaimLoot", { kills: [{ animalId: "animal.bear", count: perCall }] }).granted), perCall + "마리는 한 번에 받는다");
});

// M21-07a 변종 펫 — 생성기 동물 행 `VariantPetItemID` 12개 = 서버 `VARIANT_PET_ITEMS`, 판매가는 펫 값 그대로.
test("변종 펫 (M21-07a) — 생성기 동물 행 변종 ID = 서버 VARIANT_PET_ITEMS · 판매가 = 펫", () => {
    const sb = makeTitle().sandbox;
    const rowIds = dumpGeneratorRows("ANIMAL_ROWS").map((r) => r.VariantPetItemID).filter((id) => id);
    assertEqual(rowIds.length, 12, "변종 펫이 있는 동물 행 수");
    assertEqual([...rowIds].sort(), [...sb.VARIANT_PET_ITEMS].sort(), "생성기 ↔ 서버 변종 펫 목록");
    sb.VARIANT_PET_ITEMS.forEach((id) => assertEqual(sb.sellPriceOf(id), sb.SELL_PRICE.pet, id + " 판매가"));
});

// M21-07b 변종 펫 서버 — 받기·목장 칸·주기 ÷1.5·도감 `variant` 12칸(보상 계산 밖). 숫자는 DESIGN 10.6 「변종 생산」·「변종 도감」.
test("변종 펫 받기 (M21-07b) — ClaimLoot 가 12종을 받고 도감 variant 칸·포획 업적을 찍는다", () => {
    const title = makeTitle();
    const r = title.call("p", "ClaimLoot", { items: [{ itemId: "item.pet_fox_variant", count: 1 }] });
    assertEqual(r.granted, [{ itemId: "item.pet_fox_variant", count: 1 }], "변종 펫 지급");
    assertEqual(title.count("p", "item.pet_fox_variant"), 1, "가방에 +1");
    assertTrue(title.getData("p", "codex").indexOf("item.pet_fox_variant") !== -1, "도감 변종 칸");
    assertEqual(title.getData("p", "achievements").counters.capture, 1, "포획으로 센다");
    ["item.pet_pig_variant", "item.pet_dragon_variant", "item.pet_fox_variant_variant"].forEach((id) =>
        assertFailsClean(makeTitle(), "p", "ClaimLoot", { items: [{ itemId: id, count: 1 }] }, "bad_args", id + " 은 못 받는다"));
});

test("변종 펫 목장 (M21-07b) — 같은 종으로 서고 칸 variant · 주기 ÷1.5 · 빼면 변종 펫이 돌아온다", () => {
    const sb = makeTitle().sandbox;
    assertEqual(sb.HARVEST_INTERVAL_MINUTES / sb.VARIANT_CYCLE_DIVISOR, 20, "30분 → 20분 (DESIGN 10.6)");
    const title = makeTitle();
    title.setData("p", "ranchMeta", { upkeepUntilUtc: null, fame: 0, slotCap: 5, firstBatchUsed: true }); // 첫 배치 1분을 비켜 주기만 잰다
    assertFailsClean(title, "p", "RegisterPet", { animalId: "animal.fox", slot: 0, variant: true }, "not_enough_items", "변종 펫 없음");
    title.seed("p", "item.pet_fox", 1);
    assertFailsClean(title, "p", "RegisterPet", { animalId: "animal.fox", slot: 0, variant: true }, "not_enough_items", "일반 펫은 변종 칸에 못 쓴다");
    title.seed("p", "item.pet_pig", 1);
    assertFailsClean(title, "p", "RegisterPet", { animalId: "animal.pig", slot: 0, variant: true }, "bad_args", "변종 없는 자리표시 종");
    title.seed("p", "item.pet_fox_variant", 1);
    const got = title.call("p", "RegisterPet", { animalId: "animal.fox", slot: 0, variant: true });
    assertEqual([got.success, got.animalId, got.variant], [true, "animal.fox", true], "변종 배치 = 같은 종");
    assertEqual([title.count("p", "item.pet_fox_variant"), title.count("p", "item.pet_fox")], [0, 1], "변종 펫만 빠진다");
    assertEqual(title.getData("p", "ranch")[0].variant, true, "칸 variant");
    assertEqual(title.call("p", "RegisterPet", { animalId: "animal.fox", slot: 1 }).variant, false, "일반 배치는 variant 없음");
    assertEqual(title.getData("p", "ranch")[1].variant, undefined, "일반 칸엔 표시 없음");
    title.age("p", "ranch", 60 * 60 * 1000);
    const base = title.call("p", "GetBaseState");
    assertEqual([base.ranch[0].stock, base.ranch[1].stock], [3, 2], "60분 = 변종 3 · 일반 2");
    const h = title.call("p", "HarvestRanch", { slot: 0 });
    assertEqual([h.harvested, h.itemId], [3, "item.fox_fur"], "생산물은 같은 종 그대로");
    title.age("p", "ranch", 19 * 60 * 1000);
    assertEqual(title.call("p", "HarvestRanch", { slot: 0 }).harvested, 0, "거둔 뒤 19분 = 아직");
    title.age("p", "ranch", 1 * 60 * 1000);
    assertEqual(title.call("p", "HarvestRanch", { slot: 0 }).harvested, 1, "20분 = 1");
    const out = title.call("p", "RemovePet", { slot: 0 });
    assertEqual([out.petItemId, out.variant], ["item.pet_fox_variant", true], "빼기 = 변종 펫");
    assertEqual(title.count("p", "item.pet_fox_variant"), 1, "변종 펫이 돌아온다");
    assertEqual(title.call("p", "RemovePet", { slot: 1 }).petItemId, "item.pet_fox", "일반 칸은 일반 펫");
});

test("변종 도감 (M21-07b) — variant 12칸, 다 채워도 특별 상인·업적 도감 % 는 그대로", () => {
    const title = makeTitle();
    const sb = title.sandbox;
    assertEqual([...sb.CODEX_CATEGORIES.variant].sort(), [...sb.VARIANT_PET_ITEMS].sort(), "칸 = 변종 펫 12");
    assertEqual(sb.CODEX_CATEGORIES.variant.length, 12, "12칸");
    title.setData("p", "codex", sb.CODEX_CATEGORIES.variant);
    const r = title.call("p", "GetCodexRewards");
    assertEqual(r.categories.variant, { have: 12, total: 12, percent: 100 }, "변종 100%");
    assertEqual(r.rewards, { farSea: false, dangerDock: false, specialMerchant: false }, "보상은 하나도 안 열린다");
    title.call("p", "ClaimLoot", { items: [{ itemId: "item.fox_meat", count: 1 }] }); // 업적 갱신
    assertEqual(title.getData("p", "achievements").counters.codexPercent, 0, "업적 도감 % 에 안 센다");
    assertEqual(title.getData("p", "codexCollars") || [], [], "목줄 없음");
});

// M13-32 합격 대조 — DESIGN 13.3~13.8 표의 넣음 행 ItemID 가 ① DT_ItemTable(생성기) ③ 서버의 「그 행이 속한 목록」에
// 다 있는지 잰다. 목록은 행이 말하는 얻는 곳으로 고른다(잡화점 → SHOP_ITEMS · 작업대/불 → RECIPES · 상자 → CHEST_LOOT …).
// ② 카탈로그는 저장소에 원본 파일이 없어(라이브 콘솔만, BACKEND.md 기록) 여기서 못 잰다 — 로그에 그렇게 적는다.
function readM13ItemRows() {
    const start = designLines.findIndex((l) => l.startsWith("### 13.3 "));
    const end = designLines.findIndex((l) => l.startsWith("### 13.9 "));
    if (start === -1 || end <= start) throw new Error("DESIGN 13.3~13.8 절을 못 찾았다");
    const rows = [];
    let section = "", header = null;
    for (let i = start; i < end; i++) {
        const line = designLines[i];
        if (line.startsWith("### ")) section = line.slice(4, 8);
        if (!line.startsWith("|")) { header = null; continue; }
        const cells = line.split("|").slice(1, -1).map((c) => c.trim());
        if (!header) { header = cells; continue; }
        if (/^-+$/.test(cells[0])) continue;
        rows.push({ section, header, cells });
    }
    return rows;
}

function idsIn(text) {
    return (text.match(/`item\.[a-z0-9_]+`/g) || []).map((s) => s.slice(1, -1));
}

/** 행 → [[ID, 목록 이름], …]. 목록이 정해지지 않는 행(판정 표)은 ① 만 본다. */
function m13Expectations(row) {
    const h = row.header, c = row.cells, out = [];
    const col = (name) => c[h.indexOf(name)] || "";
    const add = (ids, lists) => ids.forEach((id) => lists.forEach((list) => out.push([id, list])));
    const bySource = (ids, source) => {
        if (/작업대|불:/.test(source)) add(ids, ["RECIPES"]);
        if (/잡화점/.test(source)) add(ids, ["SHOP_ITEMS"]);
        if (/상자/.test(source)) add(ids, ["CHEST_LOOT"]);
        if (/채우기/.test(source)) add(ids, ["FILL_ITEMS"]);
        if (/13\.4/.test(source)) add(ids, ["GATHER_ITEMS"]);
    };
    const all = idsIn(c.join(" | "));
    if (h[0] === "작물") {
        add(idsIn(col("수확 아이템")), ["CROP_HARVEST_COINS"]);
        add(idsIn(col("씨앗")), ["SHOP_ITEMS", "RECIPES"]);
    } else if (row.section === "13.4") {
        add(all, ["GATHER_ITEMS"]);
    } else if (h[0] === "모자") {
        add(idsIn(col("ID")), ["HAT_EFFECTS"]);
        bySource(idsIn(col("ID")), col("얻는 곳"));
    } else if (h[0] === "도구") {
        add(idsIn(col("ID")), ["SHOP_ITEMS"]);
    } else if (h[0] === "재료" || h[0] === "소모품" || h[0] === "부품") {
        bySource(idsIn(col("ID")), col("만드는 법 / 얻는 곳"));
    } else if (h[0] === "요리") {
        add(idsIn(col("ID")), ["RECIPES"]);
    } else if (h[0] === "원본(영어)") {
        add(all.filter((id) => id.startsWith("item.collar_")), ["CODEX_COLLARS"]);
        add(all.filter((id) => id.startsWith("item.saddle_")), ["SADDLE_ITEMS", "RECIPES"]);
    } else if (h[0] === "항목" && h[2] === "ID") {        // 13.8 보석·보물·판매용 — 전부 상자 전리품
        add(idsIn(col("ID")), ["CHEST_LOOT"]);
        if (/판매/.test(col("값"))) add(idsIn(col("ID")), ["LOOT_SELL_PRICE"]);
    } else if (row.section === "13.8" && c[0] === "아이템") { // 13.8 페인트 7
        add(all, ["SHOP_ITEMS", "PAINT_ITEMS"]);
    }
    return { ids: all, out };
}

test("M13 합격 대조 — DESIGN 13.3~13.8 넣음 행 = DT_ItemTable · 서버 목록", () => {
    const sb = makeTitle().sandbox;
    const lists = {
        SHOP_ITEMS: Object.keys(sb.SHOP_ITEMS), RECIPES: Object.keys(sb.RECIPES),
        CROP_HARVEST_COINS: Object.keys(sb.CROP_HARVEST_COINS), GATHER_ITEMS: sb.GATHER_ITEMS,
        CHEST_LOOT: sb.CHEST_LOOT.map((l) => l.itemId), FILL_ITEMS: Object.keys(sb.FILL_ITEMS).map((k) => sb.FILL_ITEMS[k]),
        HAT_EFFECTS: Object.keys(sb.HAT_EFFECTS), CODEX_COLLARS: Object.keys(sb.CODEX_COLLARS).map((k) => sb.CODEX_COLLARS[k]),
        SADDLE_ITEMS: sb.SADDLE_ITEMS, PAINT_ITEMS: sb.PAINT_ITEMS, LOOT_SELL_PRICE: Object.keys(sb.LOOT_SELL_PRICE)
    };
    const tableIds = new Set(dumpItemRows().map((r) => r.ItemID));
    const designIds = new Set(), checks = [], missing = [];
    readM13ItemRows().forEach((row) => {
        const e = m13Expectations(row);
        e.ids.forEach((id) => designIds.add(id));
        e.out.forEach((pair) => checks.push(pair));
    });
    assertTrue(designIds.size > 150 && checks.length > 150, "13 절 ID " + designIds.size + "개 · 목록 대조 " + checks.length + "줄(빈 통과 방지)");
    designIds.forEach((id) => { if (!tableIds.has(id)) missing.push(id + " ① DT_ItemTable"); });
    checks.forEach(([id, list]) => { if (lists[list].indexOf(id) === -1) missing.push(id + " ③ " + list); });
    console.log("    M13 대조: 13 절 ID " + designIds.size + "개 · ① 표 대조 " + designIds.size + "줄 · ③ 서버 목록 대조 "
        + checks.length + "줄 · 빠진 행 " + missing.length);
    console.log("    ② 카탈로그: 저장소에 원본 파일 없음(라이브 콘솔만) — 못 잼");
    assertEqual(missing, [], "빠진 행");
});

test("PayFerry — 사냥존 5, 위험구역은 도감 뒤 10, 돌아오기 무료", () => {
    const title = makeTitle();
    title.setCoins("p", 12);
    assertEqual(title.call("p", "PayFerry", { dest: "hunt" }).paid, 5, "사냥존");
    assertEqual(title.coins("p"), 7, "잔액");
    assertFailsClean(title, "p", "PayFerry", { dest: "danger" }, "locked", "위험구역 잠김");
    const hp = title.sandbox.CODEX_CATEGORIES.hunt.concat(title.sandbox.CODEX_CATEGORIES.plant);
    title.setData("p", "codex", hp.slice(0, Math.ceil(hp.length / 2)));
    assertFailsClean(title, "p", "PayFerry", { dest: "danger" }, "not_enough_coins", "7 코인으로 위험구역");
    assertEqual(title.call("p", "PayFerry", { dest: "base" }).paid, 0, "돌아오기 무료");
    assertEqual(title.coins("p"), 7, "무료는 안 뺀다");
});

test("LaunchBoat / StowBoat — 띄우면 빠지고 거두면 돌아온다", () => {
    const title = makeTitle();
    assertFailsClean(title, "p", "LaunchBoat", {}, "not_enough_items", "보트 없음");
    assertFailsClean(title, "p", "StowBoat", {}, "not_launched", "띄운 배 없음");
    title.seed("p", "item.lifeboat", 2);
    assertEqual(title.call("p", "LaunchBoat").launched, true, "띄움");
    assertEqual(title.count("p", "item.lifeboat"), 1, "1척 빠짐");
    assertFailsClean(title, "p", "LaunchBoat", {}, "already", "두 척째");
    assertEqual(title.call("p", "StowBoat").stowed, true, "거둠");
    assertEqual(title.count("p", "item.lifeboat"), 2, "돌아옴");
});

test("GetBaseState — 빈 계정은 기본값, 등록 뒤엔 저장값 그대로 · 계정은 안 바뀐다", () => {
    const title = makeTitle();
    const empty = title.call("p", "GetBaseState");
    assertTrue(!isNaN(Date.parse(empty.nowUtc)), "서버 시계 nowUtc (M108-07)");
    delete empty.nowUtc;
    assertEqual(empty, {
        ranch: [null, null, null, null, null],
        ranchUpkeep: { upkeepUntilUtc: null, leftHours: null, locked: false, fame: 0, slotCap: 5 },
        fishing: [null, null, null, null, null],
        tents: [], boat: { launched: false }, boatStorage: {}, hat: "", outfit: {}, ranchDeco: {}
    }, "빈 계정");

    title.seed("p", "item.pet_pig", 1);
    title.call("p", "RegisterPet", { animalId: "animal.pig", slot: 2 });
    title.call("p", "RegisterFishingSpot", { fishItemId: "item.carp", slot: 0 });
    const tentId = withTent(title, "p");
    title.seed("p", "item.lifeboat", 1);
    title.call("p", "LaunchBoat");

    const before = title.snapshot("p");
    const state = title.call("p", "GetBaseState");
    assertEqual(title.snapshot("p"), before, "읽기만 한다");
    assertEqual(state.ranch, title.getData("p", "ranch"), "목장");
    assertEqual(state.ranch[2].animalId, "animal.pig", "목장 2번 칸");
    assertEqual(state.fishing, title.getData("p", "fishing"), "낚시터");
    assertEqual(state.tents, title.getData("p", "base"), "텐트");
    assertEqual(state.tents[0].tentId, tentId, "텐트 ID");
    assertEqual(state.boat, title.getData("p", "boat"), "배");
    assertEqual(state.boat.launched, true, "배 띄움");
});

test("GetBaseState — M18-08 동물 칸 stock 은 지난 시간으로 채운다(HarvestRanch 와 같은 수) · 저장은 안 바뀐다", () => {
    const title = makeTitle();
    const interval = title.sandbox.HARVEST_INTERVAL_MINUTES * 60 * 1000;
    title.seed("p", "item.pet_pig", 1);
    title.call("p", "RegisterPet", { animalId: "animal.pig", slot: 0 });
    assertEqual(title.call("p", "GetBaseState").ranch[0].stock, 0, "넣자마자 0");
    title.age("p", "ranch", 2 * interval);
    assertEqual(title.call("p", "GetBaseState").ranch[0].stock, 2, "주기 두 번 지나면 2");
    assertEqual(title.getData("p", "ranch")[0].stock, 0, "읽기만 — 저장값은 그대로");
    title.age("p", "ranch", 3 * interval);
    assertEqual(title.call("p", "GetBaseState").ranch[0].stock, title.sandbox.HARVEST_CAP_UNITS, "상한까지 찬다");
    const got = title.call("p", "HarvestRanch", { slot: 0 });
    assertEqual(got.harvested, title.sandbox.HARVEST_CAP_UNITS, "보인 수만큼 거둔다");
    assertEqual(title.call("p", "GetBaseState").ranch[0].stock, 0, "거둔 뒤 0");
});

test("HarvestRanch·HarvestFishing — 상한에서 생산이 멈춘다: 오래 둔 찬 칸은 거둔 뒤 0, 넘친 시간은 이월 안 함 (DESIGN 3.2 · 루프#1433)", () => {
    // 온라인 실측(lh1433_harvest_wait): 며칠 찬 칸을 거둔 뒤에도 GetBaseState 가 5/5 를 줬다 — 다시 누르면 또 5개·50코인.
    const title = makeTitle();
    const D = 24 * 3600 * 1000;
    const ranchInterval = title.sandbox.HARVEST_INTERVAL_MINUTES * 60 * 1000;
    title.seed("p", "item.pet_pig", 1);
    title.call("p", "RegisterPet", { animalId: "animal.pig", slot: 0 });
    title.age("p", "ranch", 3 * D);
    assertEqual(title.call("p", "HarvestRanch", { slot: 0 }).harvested, title.sandbox.HARVEST_CAP_UNITS, "찬 칸은 상한만큼");
    assertEqual(title.call("p", "GetBaseState").ranch[0].stock, 0, "거둔 뒤 0");
    assertEqual(title.call("p", "HarvestRanch", { slot: 0 }).harvested, 0, "바로 또 누르면 0");
    title.age("p", "ranch", ranchInterval);
    assertEqual(title.call("p", "GetBaseState").ranch[0].stock, 1, "한 주기 뒤 1");

    const fishInterval = title.sandbox.FISH_TABLE["item.carp"].intervalMinutes * 60 * 1000;
    title.call("p", "RegisterFishingSpot", { fishItemId: "item.carp", slot: 1 });
    title.age("p", "fishing", 3 * D);
    assertEqual(title.call("p", "HarvestFishing", { slot: 1 }).harvested, title.sandbox.FISHING_CAP_UNITS, "찬 낚시터는 상한만큼");
    assertEqual(title.call("p", "GetBaseState").fishing[1].stock, 0, "낚시터도 거둔 뒤 0");
    title.age("p", "fishing", fishInterval);
    assertEqual(title.call("p", "GetBaseState").fishing[1].stock, 1, "낚시터 한 주기 뒤 1");
});

test("M109-04a 수확 코인 하루 상한 — 동물·낚시터 합쳐 HARVEST_DAILY_COINS 까지, 넘으면 생산물만 · 빼기도 같음 · 날이 바뀌면 0 부터", () => {
    const title = makeTitle();
    const sb = title.sandbox;
    const D = 24 * 3600 * 1000;
    const day = Math.floor(Date.now() / 86400000);
    const ranchFull = sb.HARVEST_CAP_UNITS * sb.COIN_PER_UNIT;                       // 찬 동물 칸 한 번 = 50
    const fishFull = sb.FISHING_CAP_UNITS * sb.FISH_TABLE["item.carp"].coinPerUnit;   // 찬 잉어 칸 한 번 = 50
    title.seed("p", "item.pet_pig", 2);
    title.call("p", "RegisterPet", { animalId: "animal.pig", slot: 0 });
    title.call("p", "RegisterPet", { animalId: "animal.pig", slot: 1 });
    title.call("p", "RegisterFishingSpot", { fishItemId: "item.carp", slot: 0 });
    // 오늘 몫이 동물 한 칸 반만 남았다 → 동물 칸은 다 받고, 낚시터는 남은 몫만, 다음 동물 칸은 0.
    const room = ranchFull + Math.floor(fishFull / 2);
    title.setData("p", "harvestCoins", { day, coins: sb.HARVEST_DAILY_COINS - room });
    title.age("p", "ranch", 3 * D);
    title.age("p", "fishing", 3 * D);
    const a = title.call("p", "HarvestRanch", { slot: 0 });
    assertEqual([a.harvested, a.coins, a.coinsCapped, a.coinsLeftToday], [sb.HARVEST_CAP_UNITS, ranchFull, false, room - ranchFull], "상한 안 — 다 받음");
    const f = title.call("p", "HarvestFishing", { slot: 0 });
    assertEqual([f.harvested, f.coins, f.coinsCapped, f.coinsLeftToday], [sb.FISHING_CAP_UNITS, room - ranchFull, true, 0], "낚시터 — 남은 몫만");
    const b = title.call("p", "RemovePet", { slot: 1 });
    assertEqual([b.harvested, b.coins], [sb.HARVEST_CAP_UNITS, 0], "상한 뒤 빼기 — 생산물은 주고 코인 0");
    assertEqual(a.coins + f.coins + b.coins, room, "세 번 받은 수확 코인 = 남았던 몫"); // 지갑엔 수확 업적 코인(#8)도 같이 붙는다
    assertEqual(title.count("p", sb.ANIMAL_PRODUCT_ITEM["animal.pig"]), 2 * sb.HARVEST_CAP_UNITS, "생산물은 상한과 상관없이 다 받음");
    assertEqual(title.getData("p", "harvestCoins"), { day, coins: sb.HARVEST_DAILY_COINS }, "오늘 쓴 몫 = 상한");
    // 어제 몫이 꽉 찬 계정은 오늘 0 부터.
    title.setData("p", "harvestCoins", { day: day - 1, coins: sb.HARVEST_DAILY_COINS });
    title.age("p", "ranch", 3 * D);
    const c = title.call("p", "HarvestRanch", { slot: 0 });
    assertEqual([c.coins, c.coinsCapped, c.coinsLeftToday], [ranchFull, false, sb.HARVEST_DAILY_COINS - ranchFull], "다음 날 0 부터");
});

test("M21-01 첫 배치 — 계정 첫 펫은 1분에 1개, 거둔 뒤로는 30분 · 두 번째 펫·다시 넣기·옛 계정은 30분", () => {
    const title = makeTitle();
    const M = 60 * 1000;
    const interval = title.sandbox.HARVEST_INTERVAL_MINUTES * M;
    assertEqual(title.sandbox.FIRST_BATCH_MINUTES, parseFloat(designRow("첫 배치 생산 주기 (M21-01)")[1]), "DESIGN 3.2 표와 같은 값");
    const first = title.sandbox.FIRST_BATCH_MINUTES * M;
    title.seed("p", "item.pet_pig", 2);
    title.call("p", "RegisterPet", { animalId: "animal.pig", slot: 0 });
    title.call("p", "RegisterPet", { animalId: "animal.pig", slot: 1 });
    title.age("p", "ranch", first - 1000);
    assertEqual(title.call("p", "GetBaseState").ranch[0].stock, 0, "첫 펫 59초 → 0");
    title.age("p", "ranch", 1000);
    assertEqual(title.call("p", "GetBaseState").ranch[0].stock, 1, "첫 펫 60초 → 재고 1");
    assertEqual(title.call("p", "GetBaseState").ranch[1].stock, 0, "같은 계정 두 번째 펫 60초 → 0");
    const got = title.call("p", "HarvestRanch", { slot: 0 });
    assertEqual(got.harvested, 1, "60초에 1개 거둠");
    assertEqual(title.getData("p", "ranch")[0].firstBatch, undefined, "거두면 첫 배치 표시가 지워진다");
    title.age("p", "ranch", interval - 1000);
    assertEqual(title.call("p", "GetBaseState").ranch[0].stock, 0, "거둔 뒤 29분 59초 → 0");
    title.age("p", "ranch", 1000);
    assertEqual(title.call("p", "GetBaseState").ranch[0].stock, 1, "거둔 뒤 30분 → 1 (두 번째는 30분)");

    // 빼고 다시 넣어도 계정 표시가 남아 다시 못 받는다.
    const again = makeTitle();
    again.seed("p", "item.pet_pig", 1);
    again.call("p", "RegisterPet", { animalId: "animal.pig", slot: 0 });
    again.call("p", "RemovePet", { slot: 0 });
    again.call("p", "RegisterPet", { animalId: "animal.pig", slot: 0 });
    again.age("p", "ranch", 10 * M);
    assertEqual(again.call("p", "GetBaseState").ranch[0].stock, 0, "빼고 다시 넣은 펫 10분 → 0");

    // 유지가 이미 시작된 옛 계정(첫 배치 표시 이전에 펫을 넣어 본 계정)은 못 받는다.
    const old = makeTitle();
    old.setData("p", "ranchMeta", { upkeepUntilUtc: new Date(Date.now() + 20 * 3600 * 1000).toISOString(), fame: 0, slotCap: 5 });
    old.seed("p", "item.pet_pig", 1);
    old.call("p", "RegisterPet", { animalId: "animal.pig", slot: 0 });
    old.age("p", "ranch", 10 * M);
    assertEqual(old.call("p", "GetBaseState").ranch[0].stock, 0, "옛 계정 10분 → 0");
});

test("GetBaseState — M19-07 낚시터 칸 stock 도 지난 시간으로 채운다(HarvestFishing 과 같은 수) · 저장은 안 바뀐다", () => {
    const title = makeTitle();
    const interval = title.sandbox.FISH_TABLE["item.carp"].intervalMinutes * 60 * 1000;
    title.call("p", "RegisterFishingSpot", { fishItemId: "item.carp", slot: 1 });
    assertEqual(title.call("p", "GetBaseState").fishing[1].stock, 0, "넣자마자 0");
    title.age("p", "fishing", 2 * interval);
    assertEqual(title.call("p", "GetBaseState").fishing[1].stock, 2, "주기 두 번 지나면 2");
    assertEqual(title.getData("p", "fishing")[1].stock, 0, "읽기만 — 저장값은 그대로");
    title.age("p", "fishing", (title.sandbox.FISHING_CAP_UNITS - 2) * interval); // 딱 상한만큼 — 거둔 뒤 남는 몫 0
    assertEqual(title.call("p", "GetBaseState").fishing[1].stock, title.sandbox.FISHING_CAP_UNITS, "상한까지 찬다");
    const got = title.call("p", "HarvestFishing", { slot: 1 });
    assertEqual(got.harvested, title.sandbox.FISHING_CAP_UNITS, "보인 수만큼 거둔다");
    assertEqual(title.call("p", "GetBaseState").fishing[1].stock, 0, "거둔 뒤 0");
});

test("HarvestRanch — 목장 생산물은 DESIGN 3.2 표대로 (소 → item.milk, 16종)", () => {
    const title = makeTitle();
    assertEqual(Object.keys(title.sandbox.ANIMAL_PRODUCT_ITEM).length, 16, "목장 표 16종");
    title.seed("p", "item.pet_cow", 1);
    title.call("p", "RegisterPet", { animalId: "animal.cow", slot: 0 });
    title.age("p", "ranch", 30 * 60 * 1000);
    title.call("p", "HarvestRanch", { slot: 0 });
    assertEqual(title.count("p", "item.milk"), 1, "소 1사이클 = 우유 1");
    assertEqual(title.count("p", "item.cow_milk"), 0, "옛 ID 는 안 준다");
});

// ══ 목장 유지비·명성 (DESIGN 3.2 · M12-05) ═════════════════════════════════════
const H = 3600 * 1000;
function ranchMeta(title, pid) { return title.getData(pid, "ranchMeta"); }

test("목장 유지비·명성 숫자가 설계서 3.2 표와 같다", () => {
    const sb = makeTitle().sandbox;
    assertTrue(designRow("목장 유지비")[1].indexOf("`" + sb.UPKEEP_ITEM + "`") !== -1, "유지 재료 = 나무");
    assertEqual(sb.UPKEEP_HOURS_PER_WOOD, designNumber("목장 유지비", /1개 = \*\*(\d+)시간/), "나무 1개 유지 시간");
    assertEqual(sb.UPKEEP_MAX_HOURS, designNumber("목장 유지비", /최대 \*\*(\d+)시간/), "유지 최대");
    assertEqual(sb.UPKEEP_START_HOURS, designNumber("목장 유지 시작", /\*\*(\d+)시간\*\*/), "유지 시작");
    assertEqual(sb.RANCH_FAME_PER_UNIT, designNumber("명성 획득", /당 \*\*(\d+)\*\*/), "수확당 명성");
    assertEqual(sb.RANCH_EXPAND_FAME_STEP, designNumber("슬롯 확장 비용", /명성 \*\*(\d+)\*\*/), "확장 명성");
    assertEqual(sb.RANCH_EXPAND_COIN_STEP, designNumber("슬롯 확장 비용", /코인 \*\*(\d+)\*\*/), "확장 코인");
    assertEqual(sb.RANCH_SLOT_MAX, designNumber("목장 슬롯 상한", /\*\*(\d+)칸\*\*/), "슬롯 상한");
    assertEqual(sb.RANCH_SLOT_COUNT, designNumber("목장 슬롯 상한", /초기 (\d+)/), "초기 칸");
    assertEqual(sb.RANCH_SLOT_COUNT, designNumber("초기 목장 슬롯 수", /^(\d+)칸/), "3.2 초기 칸");
});

test("목장 유지 — 첫 배치에 24시간, 끊기면 배치·수확이 잠긴다(계정 그대로)", () => {
    const title = makeTitle();
    const t0 = Date.now();
    title.seed("p", "item.pet_pig", 1);
    title.call("p", "RegisterPet", { animalId: "animal.pig", slot: 0 });
    const left = Date.parse(ranchMeta(title, "p").upkeepUntilUtc) - t0;
    assertTrue(left > 23.9 * H && left <= 24.1 * H, "첫 배치에 24시간");
    assertEqual(title.call("p", "GetBaseState").ranchUpkeep.locked, false, "시작 직후 안 잠김");

    title.age("p", "ranch", 25 * H);
    title.age("p", "ranchMeta", 25 * H);
    const view = title.call("p", "GetBaseState").ranchUpkeep;
    assertEqual([view.locked, view.leftHours], [true, 0], "기한이 지나면 잠김 · 남은 0시간");
    assertFailsClean(title, "p", "HarvestRanch", { slot: 0 }, "locked", "잠긴 목장 수확");
    title.seed("p", "item.pet_cow", 1);
    assertFailsClean(title, "p", "RegisterPet", { animalId: "animal.cow", slot: 1 }, "locked", "잠긴 목장 배치");
    assertEqual(title.count("p", "item.truffle"), 0, "잠긴 동안 생산물 0");
});

test("FeedRanchUpkeep — 나무를 넣으면 풀리고, 잠긴 동안 생산은 멈춘 셈이다", () => {
    const title = makeTitle();
    const now = Date.now();
    // 잠긴 지 10시간 · 잠기기 1시간 전에 넣은 돼지 → 잠기기 전 2사이클만 쌓였다(멈추지 않았다면 상한 5).
    title.setData("p", "ranchMeta", { upkeepUntilUtc: new Date(now - 10 * H).toISOString(), fame: 0, slotCap: 5 });
    title.setData("p", "ranch", [{ slot: 0, animalId: "animal.pig", occupiedSinceUtc: new Date(now - 11 * H).toISOString(), stock: 0 }, null, null, null, null]);

    assertFailsClean(title, "p", "FeedRanchUpkeep", { count: 0 }, "bad_args", "0개");
    title.seed("p", "item.wood", 2);
    assertFailsClean(title, "p", "FeedRanchUpkeep", { count: 3 }, "not_enough_items", "나무 모자람");

    const fed = title.call("p", "FeedRanchUpkeep", { count: 2 });
    assertEqual([fed.used, fed.unlocked], [2, true], "2개 넣고 풀림");
    assertEqual(title.count("p", "item.wood"), 0, "나무 빠짐");
    const left = Date.parse(fed.upkeepUntilUtc) - Date.now();
    assertTrue(left > 3.9 * H && left <= 4 * H, "잠긴 목장은 0에서 다시 — 2개 = 4시간");
    assertEqual(title.call("p", "GetBaseState").ranchUpkeep.locked, false, "풀림");
    assertEqual(title.call("p", "HarvestRanch", { slot: 0 }).harvested, 2, "잠긴 10시간은 생산에 안 센다");

    title.setData("p", "ranchMeta", { upkeepUntilUtc: new Date(Date.now() + 200 * H).toISOString(), fame: 0, slotCap: 5 });
    title.seed("p", "item.wood", 1);
    assertFailsClean(title, "p", "FeedRanchUpkeep", { count: 1 }, "full", "유지 가득");
});

test("HarvestRanch — 수확 1개당 명성 1, 스냅샷에 실린다", () => {
    const title = makeTitle();
    title.seed("p", "item.pet_pig", 1);
    title.call("p", "RegisterPet", { animalId: "animal.pig", slot: 0 });
    title.age("p", "ranch", 90 * 60 * 1000);
    const got = title.call("p", "HarvestRanch", { slot: 0 });
    assertEqual([got.harvested, got.fame], [3, 3], "3사이클 = 명성 3");
    assertEqual(ranchMeta(title, "p").fame, 3, "서버 저장 명성");
    assertEqual(title.call("p", "GetBaseState").ranchUpkeep.fame, 3, "GetBaseState 명성");
    assertEqual(title.call("p", "HarvestRanch", { slot: 0 }).harvested, 0, "곧바로 다시 수확하면 0");
    assertEqual(ranchMeta(title, "p").fame, 3, "0개 수확엔 명성 없음");
});

test("ExpandRanchSlot — 명성·코인이 모자라면 거절, 되면 6번째 칸이 열린다 · 10칸이 끝", () => {
    const title = makeTitle();
    assertEqual(title.call("p", "RegisterPet", { animalId: "animal.pig", slot: 5 }).error, "잘못된 슬롯 번호", "확장 전 6번째 칸 없음");
    assertFailsClean(title, "p", "ExpandRanchSlot", {}, "not_enough_fame", "명성 0");

    title.setData("p", "ranchMeta", { upkeepUntilUtc: null, fame: 50, slotCap: 5 });
    title.setCoins("p", 499);
    const poor = assertFailsClean(title, "p", "ExpandRanchSlot", {}, "not_enough_coins", "코인 499");
    assertEqual([poor.need, poor.have], [500, 499], "첫 확장 코인 500");

    title.setCoins("p", 500);
    const ok = title.call("p", "ExpandRanchSlot");
    assertEqual([ok.slotCap, ok.fame, ok.cost], [6, 0, { fame: 50, coins: 500 }], "첫 확장");
    assertEqual(title.coins("p"), 0, "코인 500 빠짐");
    assertEqual(title.call("p", "GetBaseState").ranch.length, 6, "스냅샷 칸 6");
    title.seed("p", "item.pet_pig", 1);
    assertEqual(title.call("p", "RegisterPet", { animalId: "animal.pig", slot: 5 }).success, true, "6번째 칸에 배치");
    const next = assertFailsClean(title, "p", "ExpandRanchSlot", {}, "not_enough_fame", "두 번째 확장");
    assertEqual(next.need, 100, "두 번째 확장 명성 100");

    title.setData("p", "ranchMeta", { upkeepUntilUtc: null, fame: 9999, slotCap: 10 });
    title.setCoins("p", 99999);
    assertFailsClean(title, "p", "ExpandRanchSlot", {}, "full", "10칸이 끝");
});

// ══ #0190 — 펫 배치는 펫 아이템을 쓴다 · 도감은 칸 ID 만 ═══════════════════════════
test("RegisterPet — 포획한 펫 1개를 빼고 칸에 올린다 · 펫 없음·다른 종 펫·모르는 종은 계정 그대로", () => {
    const title = makeTitle();
    assertFailsClean(title, "p", "RegisterPet", { animalId: "animal.fox", slot: 0 }, "not_enough_items", "펫 없음");
    title.seed("p", "item.pet_pig", 1);
    assertFailsClean(title, "p", "RegisterPet", { animalId: "animal.fox", slot: 0 }, "not_enough_items", "다른 종 펫");
    title.seed("p", "item.pet_fox", 2);
    assertFailsClean(title, "p", "RegisterPet", { animalId: "animal.dragon", slot: 0 }, "bad_args", "모르는 종");
    assertFailsClean(title, "p", "RegisterPet", { animalId: "item.pet_fox", slot: 0 }, "bad_args", "종 ID 가 아닌 것");
    const got = title.call("p", "RegisterPet", { animalId: "animal.fox", slot: 0 });
    assertEqual(got.success, true, "배치 성공");
    assertEqual(title.count("p", "item.pet_fox"), 1, "여우 펫 1개만 빠진다");
    assertEqual(title.count("p", "item.pet_pig"), 1, "다른 펫은 그대로");
    assertEqual(title.getData("p", "ranch")[0].animalId, "animal.fox", "칸이 찬다");
    assertTrue(title.getData("p", "codex").indexOf("animal.fox") !== -1, "사냥 칸 도감");
});

test("RemovePet — 펫을 빼면 쌓인 생산물을 먼저 수확하고 펫 아이템 1개를 돌려준다(DESIGN 3.2 🟡)", () => {
    const pigAt = (pid, title, ms) => {
        title.seed(pid, "item.pet_pig", 1);
        title.call(pid, "RegisterPet", { animalId: "animal.pig", slot: 0 });
        title.age(pid, "ranch", ms);
    };
    // 90분 = 3사이클 — 같은 시간 HarvestRanch 한 것과 생산물·코인·명성이 같다.
    const twin = makeTitle();
    pigAt("p", twin, 90 * 60 * 1000);
    const viaHarvest = twin.call("p", "HarvestRanch", { slot: 0 });
    const title = makeTitle();
    pigAt("p", title, 90 * 60 * 1000);
    const got = title.call("p", "RemovePet", { slot: 0 });
    assertEqual([got.removed, got.slot, got.animalId, got.petItemId], [true, 0, "animal.pig", "item.pet_pig"], "빼기 결과");
    assertEqual([got.harvested, got.itemId, got.coins, got.fame],
        [viaHarvest.harvested, viaHarvest.itemId, viaHarvest.coins, viaHarvest.fame], "수확과 같은 몫");
    assertEqual(got.harvested, 3, "90분 = 3");
    assertEqual(title.count("p", "item.truffle"), 3, "생산물 가방에");
    assertEqual(title.count("p", "item.pet_pig"), 1, "펫이 돌아온다");
    assertEqual(title.getData("p", "ranch")[0], null, "칸이 빈다");
    assertEqual(title.getData("p", "ranchMeta").fame, got.fame, "명성 저장");
    // 빈 칸이 되었으니 다시 올릴 수 있다.
    assertEqual(title.call("p", "RegisterPet", { animalId: "animal.pig", slot: 0 }).success, true, "다시 배치");

    // 쌓인 것 0 — 펫만 돌아온다.
    const fresh = makeTitle();
    pigAt("p", fresh, 0);
    const none = fresh.call("p", "RemovePet", { slot: 0 });
    assertEqual([none.removed, none.harvested, none.coins], [true, 0, 0], "쌓인 것 0");
    assertEqual([fresh.count("p", "item.pet_pig"), fresh.count("p", "item.truffle")], [1, 0], "펫만");
});

test("RemovePet — 잘못된 칸·빈 칸·작물 칸·잠김·가방 가득·남의 계정은 아무것도 안 바뀐다", () => {
    const title = makeTitle();
    title.seed("p", "item.pet_pig", 1);
    title.call("p", "RegisterPet", { animalId: "animal.pig", slot: 0 });
    [-1, 5, 1.5, "0", undefined].forEach((slot) =>
        assertFailsClean(title, "p", "RemovePet", { slot: slot }, "bad_slot", "칸 " + slot));
    assertFailsClean(title, "p", "RemovePet", {}, "bad_slot", "칸 없음");
    assertFailsClean(title, "p", "RemovePet", { slot: 1 }, "empty_slot", "빈 칸");
    title.seed("p", "item.seed_carrot", 1);
    title.call("p", "PlantCrop", { slot: 2, seedItemId: "item.seed_carrot" });
    assertFailsClean(title, "p", "RemovePet", { slot: 2 }, "empty_slot", "작물 칸");
    // 남의 목장은 없다 — q 가 부르면 q 의 빈 칸이고, p 의 돼지는 그대로.
    assertFailsClean(title, "q", "RemovePet", { slot: 0 }, "empty_slot", "q 의 0번 칸", ["p"]);

    // 가방 가득: 생산물 자리가 없으면 거절(펫 아이템은 칸을 안 먹는다).
    const full = makeTitle();
    full.seed("p", "item.pet_pig", 1);
    full.call("p", "RegisterPet", { animalId: "animal.pig", slot: 0 });
    full.age("p", "ranch", H);
    fillSlots(full, "p", 20);
    assertFailsClean(full, "p", "RemovePet", { slot: 0 }, "inventory_full", "가방 가득 + 쌓인 생산물");

    // 잠김: 유지비가 끊기면 빼기로 잠김을 우회하지 못한다.
    title.age("p", "ranch", 25 * H);
    title.age("p", "ranchMeta", 25 * H);
    assertFailsClean(title, "p", "RemovePet", { slot: 0 }, "locked", "잠긴 목장");
});

test("addToCodex — 다섯 카테고리 칸 밖 ID(고기·펫·가축)는 저장하지 않는다", () => {
    const title = makeTitle();
    title.call("p", "ClaimLoot", { items: [{ itemId: "item.fox_meat", count: 1 }, { itemId: "item.pet_pig", count: 1 },
        { itemId: "item.fox_tail", count: 1 }] });
    assertEqual(title.getData("p", "codex"), ["item.fox_tail"], "칸인 꼬리만");
    title.call("p", "RegisterPet", { animalId: "animal.pig", slot: 0 }); // 돼지는 자리표시 종 — 사냥 칸이 아니다
    assertEqual(title.getData("p", "codex"), ["item.fox_tail"], "animal.pig 는 안 담긴다");
    const sb = title.sandbox;
    const all = [].concat(...Object.keys(sb.CODEX_CATEGORIES).map((n) => sb.CODEX_CATEGORIES[n]));
    assertTrue(new Set(all).size < sb.CODEX_MAX_ENTRIES, "칸 수(" + new Set(all).size + ") < 안전장치 " + sb.CODEX_MAX_ENTRIES);
});

test("addToCodex — 가드 전에 쌓인 칸 밖 ID 가 상한을 채워도 칸 ID 는 들어가고, 진행률은 칸 ID 로만 센다 (M13-15)", () => {
    const title = makeTitle();
    const sb = title.sandbox;
    const junk = [];
    for (let i = 0; i < sb.CODEX_MAX_ENTRIES; i++) junk.push("item.junk_" + i);
    title.setData("p", "codex", junk.concat(["item.fox_tail", "item.fox_meat", "item.resin_adhesive"]));
    const before = title.call("p", "GetCodexRewards");
    assertEqual(before.categories.hunt.have, 1, "칸 밖 ID 는 진행률에 안 센다");
    title.call("p", "ClaimLoot", { items: [{ itemId: "item.wolf_tooth", count: 1 }] });
    assertEqual(title.getData("p", "codex"), ["item.fox_tail", "item.wolf_tooth"], "칸 ID 가 들어가고 칸 밖 ID 는 정리된다");
    const after = title.call("p", "GetCodexRewards");
    assertEqual(after.categories.hunt.have, 2, "사냥 칸 하나 늘었다");
    assertEqual([after.categories.fishing, after.categories.plant], [before.categories.fishing, before.categories.plant], "다른 칸은 그대로");
});

// ══ 업적 (DESIGN 8.15) ═════════════════════════════════════════════════════════
test("ACHIEVEMENTS — 설계서 8.15 목록과 한 줄씩 같다", () => {
    const rows = [];
    designLines.forEach((line) => {
        const c = line.split("|").slice(1, -1).map((x) => x.trim());
        const m = c.length >= 6 && /^`(ach\.[a-z0-9_]+)`$/.exec(c[1]);
        if (m) rows.push({ id: m[1], counter: c[2].replace(/`/g, ""), target: Number(c[3]), coins: Number(c[4]) });
    });
    assertTrue(rows.length >= 10, "설계서 업적 줄 10개 이상");
    const script = makeTitle().sandbox.ACHIEVEMENTS.map((a) => ({ id: a.id, counter: a.counter, target: a.target, coins: a.coins }));
    assertEqual(script, rows, "서버 업적 표");
});

test("업적 — 첫 포획은 코인 10 한 번만, 두 번째엔 안 준다", () => {
    const title = makeTitle();
    const pet = [{ itemId: "item.pet_pig", count: 1 }];
    title.call("p", "ClaimLoot", { items: pet });
    assertEqual(title.coins("p"), 10, "첫 올가미 보상");
    assertEqual(title.getData("p", "achievements").done, ["ach.capture_1"], "달성 기록");
    title.setData("p", "lootAt", 0);
    title.call("p", "ClaimLoot", { items: pet });
    assertEqual(title.coins("p"), 10, "두 번째 포획엔 안 준다");
    assertEqual(title.getData("p", "achievements").counters.capture, 2, "카운터는 계속 센다");
});

test("업적 — 사냥·수확 카운터와 도감 %", () => {
    const title = makeTitle();
    title.call("p", "ClaimLoot", { items: [{ itemId: "item.fox_meat", count: 9 }, { itemId: "item.wood", count: 5 }] });
    assertEqual(title.getData("p", "achievements").counters.hunt, 9, "나무는 사냥에 안 센다");
    assertEqual(title.coins("p"), 0, "목표 전엔 안 준다");
    title.setData("p", "lootAt", 0);
    title.call("p", "ClaimLoot", { items: [{ itemId: "item.pristine_ptarmigan_feather", count: 1 }] });
    assertTrue(title.getData("p", "achievements").done.indexOf("ach.hunt_10") !== -1, "사냥 10 달성");

    title.seed("p", "item.pet_pig", 1);
    title.call("p", "RegisterPet", { animalId: "animal.pig", slot: 0 });
    title.age("p", "ranch", 5 * 30 * 60 * 1000);
    title.call("p", "HarvestRanch", { slot: 0 });
    assertEqual(title.getData("p", "achievements").counters.harvest, 5, "수확 수만큼");

    const sb = title.sandbox;
    const all = [].concat(...Object.keys(sb.CODEX_CATEGORIES).map((n) => sb.CODEX_CATEGORIES[n]));
    title.setData("p", "codex", all.slice(0, Math.ceil(all.length / 2)));
    const before = title.coins("p");
    title.seed("p", "item.pet_pig", 1);
    title.call("p", "RegisterPet", { animalId: "animal.pig", slot: 1 });
    const st = title.getData("p", "achievements");
    assertTrue(st.done.indexOf("ach.codex_50") !== -1 && st.done.indexOf("ach.codex_100") === -1, "도감 50% 만");
    assertEqual(title.coins("p") - before, 50, "도감 절반 50 코인");
});

test("GetCodexRewards achievements — 업적 저장값을 그대로 싣는다 (M13-28)", () => {
    const title = makeTitle();
    assertEqual(title.call("p", "GetCodexRewards").achievements, { counters: {}, done: [] }, "처음엔 빈 상태");
    title.call("p", "ClaimLoot", { items: [{ itemId: "item.pet_pig", count: 1 }] });
    const reply = title.call("p", "GetCodexRewards").achievements;
    assertEqual(reply, title.getData("p", "achievements"), "판정 뒤 저장값과 같다");
    assertEqual([reply.counters.capture, reply.done], [1, ["ach.capture_1"]], "첫 포획이 실린다");
});

// ══ ⑩ 작물 칸 — 씨앗 소모 (DESIGN 8.2 · 8.10) ═══════════════════════════════════
/** DESIGN 13.3 작물 표 — {name, item, seed, minutes, coins, count}. 참나무만 "**4개**". */
function readCrops133() {
    const rows = designLines.map((l) => l.split("|").slice(1, -1).map((c) => c.trim()))
        .filter((c) => c.length === 7 && /^`item\.[a-z_]+`/.test(c[2]) && /^`item\.seed_[a-z_]+`$/.test(c[3]) && /^\d+분$/.test(c[4]));
    return rows.map((c) => ({
        name: c[0].replace(/\(바꿈\)$/, ""),
        item: /`(item\.[a-z_]+)`/.exec(c[2])[1],
        seed: c[3].slice(1, -1),
        minutes: Number(/^(\d+)/.exec(c[4])[1]),
        coins: Number(c[5]),
        raw: c[6],
        count: /\*\*(\d+)개\*\*/.test(c[2]) ? Number(/\*\*(\d+)개\*\*/.exec(c[2])[1]) : 1
    }));
}

/** 13.6 재료 표 — [{name, id, source, raw}]. 한 줄에 여럿(소금 · 흑후추 · 딜)이면 " · " 로 나눈다. */
function readIngredients136() {
    const start = designLines.findIndex((l) => l.startsWith("### 13.6"));
    if (start === -1) throw new Error("DESIGN 13.6 을 못 찾았다");
    const out = [];
    for (let i = start + 1; i < designLines.length && !designLines[i].startsWith("### "); i++) {
        const c = designLines[i].split("|").slice(1, -1).map((x) => x.trim());
        if (c.length !== 6 || !/^`item\./.test(c[2])) continue;
        const names = c[0].replace(/\(있음\)$/, "").split(" · ");
        const raws = c[4].split(" · ");
        c[2].match(/item\.[a-z_]+/g).forEach((id, k) => out.push({ name: names[k], id, source: c[3], raw: raws[k] }));
    }
    if (out.length === 0) throw new Error("DESIGN 13.6 재료 줄을 한 줄도 못 읽었다");
    return out;
}

/** 날로 먹기 칸 → 숫자. "+10 (원본)" 10 · "**−40** (…)" −40 · "—" null. */
function rawHealOf(cell) {
    const m = /^(?:\*\*)?([+−-])(\d+)/.exec(cell.trim());
    return m ? (m[1] === "+" ? 1 : -1) * Number(m[2]) : null;
}

test("요리 재료·날로 먹기가 DESIGN 13.3·13.6 과 같다", () => {
    const sb = makeTitle().sandbox;
    const rows = readIngredients136();
    const crops = readCrops133();
    const names = { "밀": "item.wheat", "찻잎": "item.tea_leaves" }; // 8.2 밀 · 13.3 🟡 수확 이름
    crops.forEach((c) => { names[c.name] = c.item; });
    rows.forEach((r) => { names[r.name] = r.id; });
    const healOf = (id) => (sb.RECIPES[id] ? sb.RECIPES[id].heal : sb.RAW_HEAL[id]) || null;
    rows.forEach((r) => {
        const craft = /^(작업대|어디서나|불): (.+) (\d+)$/.exec(r.source); // 어디서나 = 배낭 제작(8.10) · 불 = 모닥불 요리
        const shop = /잡화점 (?:각 )?\*\*(\d+)\*\*/.exec(r.source);
        if (craft) {
            assertTrue(names[craft[2]], r.id + " 재료 이름 → ID: " + craft[2]);
            assertEqual(sb.RECIPES[r.id], { place: { "어디서나": "anywhere", "불": "fire", "작업대": "workbench" }[craft[1]], heal: sb.RECIPES[r.id] && sb.RECIPES[r.id].heal,
                inputs: [{ itemId: names[craft[2]], count: Number(craft[3]) }] }, r.id + " " + craft[1] + " 레시피");
        } else if (shop) {
            assertEqual(sb.SHOP_ITEMS[r.id], { name: r.name, price: Number(shop[1]), count: 1 }, r.id + " 잡화점");
        } else if (/채우기/.test(r.source)) {
            assertEqual(sb.FILL_ITEMS[names["빈 병"]], r.id, r.id + " = 빈 병 채우기");
        } else if (/상자/.test(r.source)) {
            assertEqual(sb.RECIPES[r.id], undefined, r.id + " 제작 없음(10-06 보정)");
            assertTrue(sb.CHEST_LOOT.some((l) => l.itemId === r.id), r.id + " 상자 전리품");
        } else {
            assertEqual(r.source, "—", r.id + " 얻는 곳 칸을 읽었다");
        }
        assertEqual(healOf(r.id), rawHealOf(r.raw), r.id + " 날로 먹기");
    });
    crops.forEach((c) => assertEqual(healOf(c.item), rawHealOf(c.raw), c.item + " 날로 먹기(13.3)"));
    // 13.4 광대버섯은 채집 시험이, 13.6 버프 소모품은 버프 표 시험이 본다
    const covered = rows.concat(readBuff136()).map((r) => r.id).concat(crops.map((c) => c.item), ["item.fly_amanita"]);
    assertEqual(Object.keys(sb.RAW_HEAL).filter((id) => covered.indexOf(id) === -1), [], "RAW_HEAL 에 설계서 밖 아이템이 없다");
});

/** 13.6 버프 소모품 표 — [{name, id, source, heal}] (7칸 줄). heal = 「체력」 칸, "—" 는 0(버프만). */
function readBuff136() {
    return sectionRows("### 13.6").filter((c) => c.length === 7 && /^`item\./.test(c[2]))
        .map((c) => ({ name: c[0], id: c[2].replace(/`/g, ""), source: c[3], heal: rawHealOf(c[4]) || 0 }));
}

test("버프 소모품 13.6 — 체력·만드는 법·잡화점이 서버와 같다 · 버프만(0)도 먹는다", () => {
    const sb = makeTitle().sandbox;
    const rows = readBuff136();
    assertEqual(rows.length, 12, "13.6 버프 표 줄 수");
    const names = {};
    readIngredients136().forEach((r) => { names[r.name] = r.id; });
    names["커피콩"] = "item.coffee_beans"; // 13.3 작물 이름
    readCrops133().forEach((c) => { names[c.name] = c.item; });
    rows.forEach((r) => {
        const heal = sb.RECIPES[r.id] ? sb.RECIPES[r.id].heal : sb.RAW_HEAL[r.id];
        assertEqual(heal, r.heal, r.id + " 체력");
        const craft = /^(불|작업대): (.+)$/.exec(r.source);
        const shop = /잡화점 \*\*(\d+)\*\*/.exec(r.source);
        if (craft) {
            const inputs = craft[2].split(" + ").map((part) => {
                const m = /^(`?)(.+?)\1 (\d+)$/.exec(part);
                if (m[1]) return { tag: m[2], count: Number(m[3]) };
                assertTrue(names[m[2]], r.id + " 재료 이름 → ID: " + m[2]);
                return { itemId: names[m[2]], count: Number(m[3]) };
            });
            assertEqual(sb.RECIPES[r.id], { place: craft[1] === "불" ? "fire" : "workbench", heal: r.heal, inputs }, r.id + " 레시피");
        } else if (shop) {
            assertEqual(sb.SHOP_ITEMS[r.id], { name: r.name, price: Number(shop[1]), count: 1 }, r.id + " 잡화점");
        } else {
            assertEqual(r.source, "위험구역 상자", r.id + " 얻는 곳 칸을 읽었다");
        }
    });
    const title = makeTitle();
    title.seed("p", "item.heat_pack", 1);
    assertEqual(title.call("p", "ConsumeItem", { itemId: "item.heat_pack" }), { itemId: "item.heat_pack", heal: 0 }, "손난로 = 체력 0, 먹힌다");
    assertEqual(title.count("p", "item.heat_pack"), 0, "손난로 1 → 0");
    assertFailsClean(title, "p", "ConsumeItem", { itemId: "item.energy_drink_mild" }, "not_enough_items", "없는 순한맛");
});

/** 13.8 비쇠붙이 부속 표 중 작업대 줄 — [{name, id, recipe: "..."}]. 텐트 줄 ID 칸 "`item.tent`(있음)" 도 읽는다. */
function readParts138() {
    return sectionRows("### 13.8").filter((c) => c.length === 6 && /^`item\./.test(c[2]) && /^(작업대|어디서나): /.test(c[3]))
        .map((c) => ({ name: c[0], id: /`(item\.[a-z_]+)`/.exec(c[2])[1], place: c[3].startsWith("어디서나") ? "anywhere" : "workbench",
            recipe: c[3].replace(/^(작업대|어디서나): /, "") }));
}

test("부품 13.8 — 레시피 5가 서버와 같다 · 텐트 = 기름 먹인 천 4 + 나무판 6 사슬이 돈다", () => {
    const sb = makeTitle().sandbox;
    const rows = readParts138();
    assertEqual(rows.length, 5, "13.8 제작 줄 수(접착제·원단·기름 먹인 천·조끼·텐트)");
    const names = { "수지": "item.resin", "나무판": "item.plank" };
    readCrops133().forEach((c) => { names[c.name] = c.item; });
    rows.forEach((r) => { names[r.name] = r.id; });
    rows.forEach((r) => {
        const inputs = r.recipe.split(" + ").map((part) => {
            const m = /^(.+) (\d+)$/.exec(part);
            assertTrue(m && names[m[1]], r.id + " 재료 이름 → ID: " + part);
            return { itemId: names[m[1]], count: Number(m[2]) };
        });
        assertEqual(sb.RECIPES[r.id], { place: r.place, inputs }, r.id + " 레시피");
    });

    const title = makeTitle();
    assertFailsClean(title, "p", "CraftItem", { recipeId: "item.tent" }, "locked", "텐트 없이 텐트 제작"); // 첫 텐트는 상점
    title.seed("p", "item.resin", 2);
    assertEqual(title.call("p", "CraftItem", { recipeId: "item.resin_adhesive" }).count, 1, "텐트 없이 배낭에서 접착제(8.10 어디서나)");
    title.seed("p", "item.cotton", 3);
    assertEqual(title.call("p", "CraftItem", { recipeId: "item.cotton_fabric" }).count, 1, "텐트 없이 배낭에서 원단");
    assertFailsClean(title, "p", "CraftItem", { recipeId: "item.tarp" }, "locked", "기름 먹인 천는 텐트 작업대에서만");
    withTent(title, "p");
    title.seed("p", "item.resin", 8);
    title.seed("p", "item.cotton", 24);
    title.seed("p", "item.plank", 6);
    assertEqual(title.call("p", "CraftItem", { recipeId: "item.resin_adhesive", times: 4 }).count, 4, "수지 8 → 접착제 4");
    assertEqual(title.call("p", "CraftItem", { recipeId: "item.cotton_fabric", times: 8 }).count, 8, "목화 24 → 원단 8");
    assertEqual(title.call("p", "CraftItem", { recipeId: "item.tarp", times: 4 }).count, 4, "원단 8 + 접착제 4 → 기름 먹인 천 4");
    assertFailsClean(title, "p", "CraftItem", { recipeId: "item.life_jacket" }, "not_enough_items", "원단 0 이면 구명조끼 못 만든다");
    assertEqual(title.call("p", "CraftItem", { recipeId: "item.tent" }).count, 1, "기름 먹인 천 4 + 나무판 6 → 텐트");
    assertEqual([title.count("p", "item.tarp"), title.count("p", "item.plank"), title.count("p", "item.tent")], [0, 0, 1], "재료 다 쓰고 텐트 1");
});

/** DESIGN 한 절(### 머리 ~ 다음 ###)의 표 줄을 칸 배열로. */
function sectionRows(heading) {
    const start = designLines.findIndex((l) => l.startsWith(heading));
    if (start === -1) throw new Error("DESIGN " + heading + " 을 못 찾았다");
    const rows = [];
    for (let i = start + 1; i < designLines.length && !designLines[i].startsWith("### "); i++) {
        if (designLines[i].startsWith("|")) rows.push(designLines[i].split("|").slice(1, -1).map((x) => x.trim()));
    }
    return rows;
}

/** 13.7 요리 표 — [{name, id, heal, inputs: [{name|tag, count}]}]. ID 칸이 "13.6" 인 잼 줄은 13.6 버프 표에서 ID·재료를 가져온다. */
function readCooking137() {
    const buff = sectionRows("### 13.6").filter((c) => c.length === 7 && /^`item\./.test(c[2]));
    const out = [];
    sectionRows("### 13.7").forEach((c) => {
        if (c.length !== 5 || !/^\d+/.test(c[4])) return;
        const parts = c[2] !== "13.6" ? [{ name: c[0].replace(/\(바꿈\)$/, ""), id: c[2], inputs: c[3] }]
            : c[0].split(" · ").map((name) => {
                const b = buff.find((r) => r[0] === name);
                if (!b) throw new Error("13.6 버프 표에 " + name + " 이 없다");
                return { name, id: b[2], inputs: b[3].replace(/^불: /, "") };
            });
        parts.forEach((p) => out.push({
            name: p.name, id: p.id.replace(/`/g, ""), heal: Number(/^\d+/.exec(c[4])[0]),
            inputs: p.inputs.split(" + ").map((part) => {
                const m = /^(`?)(.+?)\1 (\d+)$/.exec(part);
                if (!m) throw new Error("13.7 재료 칸을 못 읽었다: " + part);
                return m[1] ? { tag: m[2], count: Number(m[3]) } : { name: m[2], count: Number(m[3]) };
            })
        }));
    });
    if (out.length === 0) throw new Error("DESIGN 13.7 요리 줄을 한 줄도 못 읽었다");
    return out;
}

test("요리 44가 DESIGN 13.7 과 같다 (불 · 체력 · 재료 ID/태그)", () => {
    const sb = makeTitle().sandbox;
    const dishes = readCooking137();
    assertEqual(dishes.length, 44, "13.7 요리 수(잼 두 줄 포함)");
    // 재료 이름 → ID: 설계서 표(13.3 작물 · 13.6 재료·버프 · 13.7 요리 · 8.4 물고기)에서 읽고, 표 밖 이름만 여기 적는다.
    const names = { "당근": "item.carrot", "우유": "item.milk", "달걀": "item.egg", "새고기": "item.bird_meat",
        "순록 고기": "item.reindeer_meat", "늑대 고기": "item.wolf_meat", "곰고기": "item.bear_meat", "살구버섯": "item.chanterelle" };
    readCrops133().forEach((c) => { names[c.name] = c.item; });
    readIngredients136().forEach((r) => { names[r.name] = r.id; });
    sectionRows("### 13.6").filter((c) => c.length === 7 && /^`item\./.test(c[2])).forEach((c) => { names[c[0]] = c[2].replace(/`/g, ""); });
    dishes.forEach((d) => { names[d.name] = d.id; });
    const fish = readFishFromDesign(sb.FISH_TABLE);
    Object.keys(fish).forEach((name) => { names[name] = fish[name].id; });
    dishes.forEach((d) => {
        const inputs = d.inputs.map((i) => {
            assertTrue(i.tag || names[i.name], d.id + " 재료 이름 → ID: " + i.name);
            return i.tag ? { tag: i.tag, count: i.count } : { itemId: names[i.name], count: i.count };
        });
        assertEqual(sb.RECIPES[d.id], { place: "fire", heal: d.heal, inputs }, d.id + " 레시피");
    });
    const soda = sectionRows("### 13.6").find((c) => c[2] === "`item.hot_soda`");
    assertEqual(sb.RAW_HEAL["item.hot_soda"], rawHealOf(soda[4]), "따뜻한 탄산 먹기 = 13.6 체력 칸");
});

test("재료 태그가 DESIGN 13.6 과 같다 — 서버 hasCraftTag 목록", () => {
    const sb = makeTitle().sandbox;
    const tagLine = designLines.find((l) => l.startsWith("**재료 태그 추가**"));
    const names = { "산딸기": "item.berry", "당근": "item.carrot", "사과": "item.apple" }; // 8.2·8.10 작물 · 13.3 은 "사과나무"
    readCrops133().forEach((c) => { names[c.name] = c.item; });
    readIngredients136().forEach((r) => { names[r.name] = r.id; });
    const want = {};
    (tagLine.match(/`[A-Za-z]+`\([^)]+\)/g) || []).forEach((part) => {
        const m = /^`([A-Za-z]+)`\(([^)]+)\)$/.exec(part);
        if (/\d+종/.test(m[2])) return; // 이름 목록이 아닌 괄호(잼 6종·식용 버섯 3종)는 아래에서 채운다
        want[m[1]] = m[2].split("·").map((n) => {
            assertTrue(names[n.trim()], m[1] + " 이름 → ID: " + n.trim());
            return names[n.trim()];
        });
    });
    want.Berry = ["item.berry"].concat(/`Berry` 에 ([^를]+)를/.exec(tagLine)[1].split("·").map((n) => names[n.trim()]));
    want.Jam = ["item.berry_jam"].concat(readCooking137().filter((d) => /잼$/.test(d.name)).map((d) => d.id)); // 잼 크루아상은 잼이 아니다
    const excluded = /([가-힣·]+) 제외/.exec(tagLine)[1].split("·");
    const pile = designLines.filter((l) => l.includes("`gatherable.mushroom_pile`") || l.includes("`gatherable.swamp_mushroom`")).join(" ");
    want.Mushroom = (pile.match(/[가-힣]+ `item\.[a-z_]+`/g) || []).filter((s) => excluded.indexOf(s.split(" ")[0]) === -1)
        .map((s) => s.split(" ")[1].replace(/`/g, ""));
    assertEqual(want.Jam.length, 6, "잼 6종");
    assertEqual(want.Mushroom.length, 7, "식용 버섯 7종 (숲 3 + 늪 4)");
    assertEqual(Object.keys(sb.CRAFT_TAG_ITEMS).sort(), Object.keys(want).sort(), "태그 목록");
    Object.keys(want).forEach((tag) => {
        assertEqual(sb.CRAFT_TAG_ITEMS[tag].slice().sort(), want[tag].slice().sort(), tag + " 아이템");
        want[tag].forEach((id) => assertTrue(sb.hasCraftTag(id, tag), id + " 는 " + tag));
    });
    assertTrue(sb.hasCraftTag("item.axolotl", "Fish") && !sb.hasCraftTag("item.grilled_salmon", "Fish"), "Fish = 날물고기만");
});

test("CraftItem — 요리 44 는 태그 재료로 되고, 태그가 안 맞으면 계정 그대로", () => {
    const title = makeTitle(); // 불 요리는 텐트(작업대)가 없어도 된다
    title.seed("p", "item.chanterelle", 1);
    title.seed("p", "item.penny_bun", 1);
    title.seed("p", "item.carrot", 1);
    const r = title.call("p", "CraftItem", { recipeId: "item.mushroom_salad" });
    assertEqual([r.crafted, r.count], ["item.mushroom_salad", 1], "버섯 둘(종류 섞어) + 채소 → 버섯 무침");
    assertEqual([title.count("p", "item.chanterelle"), title.count("p", "item.penny_bun"), title.count("p", "item.carrot")],
        [0, 0, 0], "재료가 빠졌다");
    assertEqual(title.call("p", "ConsumeItem", { itemId: "item.mushroom_salad" }).heal, 18, "먹으면 +18");

    title.seed("p", "item.fly_amanita", 2);
    title.seed("p", "item.carrot", 1);
    assertFailsClean(title, "p", "CraftItem", { recipeId: "item.mushroom_salad" }, "not_enough_items", "광대버섯은 Mushroom 이 아니다");
    title.seed("p", "item.mezereon", 3);
    assertFailsClean(title, "p", "CraftItem", { recipeId: "item.berry_salad" }, "not_enough_items", "서향 열매은 Berry 가 아니다");
});

test("CraftItem — 늪 채집물 쓰임(M33-02): 늪꽃·수련 → 페인트, 독사갈기버섯은 Mushroom", () => {
    const title = makeTitle();
    withTent(title, "p");
    title.seed("p", "item.bog_flower", 1);
    title.seed("p", "item.water_lily", 1);
    assertEqual(title.call("p", "CraftItem", { recipeId: "item.paint_blue" }).crafted, "item.paint_blue", "늪꽃 1 → 파란 페인트");
    assertEqual(title.call("p", "CraftItem", { recipeId: "item.paint_blush" }).crafted, "item.paint_blush", "수련 1 → 블러셔 페인트");
    assertEqual([title.count("p", "item.bog_flower"), title.count("p", "item.water_lily"), title.count("p", "item.paint_blue"),
        title.count("p", "item.paint_blush")], [0, 0, 1, 1], "꽃이 빠지고 페인트가 들어왔다");
    assertEqual(title.sandbox.sellPriceOf("item.paint_blue"), 0, "만든 페인트도 상점 물건이라 판매가 0");
    title.seed("p", "item.vipers_mane", 2);
    assertEqual(title.call("p", "CraftItem", { recipeId: "item.roasted_mushrooms" }).crafted, "item.roasted_mushrooms", "독사갈기 2 → 숯불 버섯");
    assertEqual(title.count("p", "item.vipers_mane"), 0, "독사갈기가 빠졌다");
    title.seed("p", "item.fly_amanita", 2);
    assertFailsClean(title, "p", "CraftItem", { recipeId: "item.roasted_mushrooms" }, "not_enough_items", "광대버섯은 여전히 Mushroom 이 아니다");
});

test("작물 재배 시간·코인·씨앗이 설계서 8.2 + 13.3 표와 같다", () => {
    const sb = makeTitle().sandbox;
    const rows = designLines.map((l) => l.split("|").slice(1, -1).map((c) => c.trim()))
        .filter((c) => c.length === 4 && /^`item\.[a-z]+`$/.test(c[1]) && /^\d+분$/.test(c[2]) && c[3].indexOf("스네어 코인") !== -1);
    assertEqual(rows.length, 3, "8.2 작물 줄 3개");
    rows.forEach((c) => {
        const id = c[1].slice(1, -1);
        assertEqual(sb.CROP_CYCLE_MINUTES[id], Number(/^(\d+)/.exec(c[2])[1]), id + " 재배 시간");
        assertEqual(sb.CROP_HARVEST_COINS[id], Number(/코인 (\d+)/.exec(c[3])[1]), id + " 코인");
    });
    const crops = readCrops133();
    assertEqual(crops.length, 24, "13.3 작물 줄 24개");
    crops.forEach((c) => {
        assertEqual([sb.CROP_CYCLE_MINUTES[c.item], sb.CROP_HARVEST_COINS[c.item], sb.seedOf(c.item), sb.CROP_HARVEST_COUNT[c.item] || 1],
            [c.minutes, c.coins, c.seed, c.count], c.item + " 13.3 줄");
        assertEqual(c.coins, Math.floor(c.minutes * 0.33 + 0.5), c.item + " 코인 = 재배 분 × 0.33");
    });
    assertEqual(Object.keys(sb.CROP_CYCLE_MINUTES).sort(), rows.map((c) => c[1].slice(1, -1)).concat(crops.map((c) => c.item)).sort(), "서버 작물 = 8.2 + 13.3");
    assertEqual(Object.keys(sb.CROP_CYCLE_MINUTES).sort(), Object.keys(sb.CROP_HARVEST_COINS).sort(), "두 표의 작물이 같다");
});

test("PlantCrop — 씨앗이 없거나 잘못된 씨앗·찬 칸이면 거절(계정 그대로)", () => {
    const title = makeTitle();
    assertFailsClean(title, "p", "PlantCrop", { slot: 0, seedItemId: "item.seed_carrot" }, "not_enough_items", "씨앗 없음");
    title.seed("p", "item.seed_carrot", 1);
    assertFailsClean(title, "p", "PlantCrop", { slot: 0, seedItemId: "item.carrot" }, "bad_args", "씨앗이 아닌 것");
    assertFailsClean(title, "p", "PlantCrop", { slot: 0, seedItemId: "item.seed_banana" }, "bad_args", "표에 없는 작물");
    assertFailsClean(title, "p", "PlantCrop", { slot: 5, seedItemId: "item.seed_carrot" }, "bad_slot", "확장 전 6번째 칸");
    title.seed("p", "item.pet_pig", 1);
    title.call("p", "RegisterPet", { animalId: "animal.pig", slot: 0 });
    assertFailsClean(title, "p", "PlantCrop", { slot: 0, seedItemId: "item.seed_carrot" }, "slot_full", "펫이 선 칸");
});

test("PlantCrop — 씨앗 1개를 빼고 빈 칸에 작물을 적는다 · 펫과 안 섞인다", () => {
    const title = makeTitle();
    title.seed("p", "item.seed_potato", 2);
    const got = title.call("p", "PlantCrop", { slot: 1, seedItemId: "item.seed_potato" });
    assertEqual([got.success, got.cropItemId], [true, "item.potato"], "심기 성공");
    assertEqual(title.count("p", "item.seed_potato"), 1, "씨앗 1개만 빠진다");
    const entry = title.getData("p", "ranch")[1];
    assertEqual([entry.cropItemId, entry.animalId, entry.stock], ["item.potato", undefined, 0], "작물 항목 모양");
    assertEqual((title.getData("p", "codex") || []).indexOf("item.potato"), -1, "도감은 수확 때");
    assertEqual(title.call("p", "GetBaseState").ranch[1].cropItemId, "item.potato", "GetBaseState 에 실린다");
    assertEqual(title.call("p", "RegisterPet", { animalId: "animal.pig", slot: 1 }).error, "슬롯이 이미 차 있음", "작물 칸에 펫 못 넣음");
    assertEqual(title.call("p", "HarvestRanch", { slot: 1 }).error, "슬롯이 비어 있음", "작물 칸은 HarvestRanch 로 못 거둔다");
    assertFailsClean(title, "p", "PlantCrop", { slot: 1, seedItemId: "item.seed_potato" }, "slot_full", "이미 심은 칸");
});

test("HarvestCrop — 원본대로 한 번 거두면 칸이 빈다(작물 1 + 코인, 쌓임 없음) · 다시 심으면 씨앗 1개 더", () => {
    const title = makeTitle();
    title.seed("p", "item.seed_carrot", 2);
    title.call("p", "PlantCrop", { slot: 0, seedItemId: "item.seed_carrot" });
    assertEqual(title.call("p", "HarvestCrop", { slot: 0 }).harvested, 0, "심자마자 0");
    assertEqual(title.call("p", "GetBaseState", {}).ranch[0].stock, 0, "덜 자란 칸은 stock 0");
    title.age("p", "ranch", 10 * H); // 재배 시간(15분)의 40배가 지나도 쌓이지 않는다
    assertEqual(title.call("p", "GetBaseState", {}).ranch[0].stock, 1, "다 자란 칸은 stock 1(표시값)");
    const coins = title.coins("p");
    const got = title.call("p", "HarvestCrop", { slot: 0 });
    assertEqual([got.harvested, got.itemId, got.coins, got.fame], [1, "item.carrot", 5, 1], "한 번에 1개");
    assertEqual(title.count("p", "item.carrot"), 1, "당근 1");
    assertEqual(title.coins("p") - coins, 5, "코인 5 × 1");
    assertTrue(title.getData("p", "codex").indexOf("item.carrot") !== -1, "도감에 올라감");
    assertEqual(title.call("p", "GetBaseState", {}).ranch[0], null, "거둔 칸은 빈 칸");
    assertFailsClean(title, "p", "HarvestCrop", { slot: 0 }, "empty_slot", "거둔 칸은 또 못 거둔다");
    assertEqual(title.count("p", "item.seed_carrot"), 1, "첫 심기에 씨앗 1개만 빠졌다");
    assertEqual(title.call("p", "PlantCrop", { slot: 0, seedItemId: "item.seed_carrot" }).success, true, "빈 칸에 다시 심는다");
    assertEqual(title.count("p", "item.seed_carrot"), 0, "다시 심을 때 씨앗 1개 더");
    assertFailsClean(title, "p", "HarvestCrop", { slot: 3 }, "empty_slot", "빈 칸");
    title.seed("p", "item.pet_pig", 1);
    title.call("p", "RegisterPet", { animalId: "animal.pig", slot: 1 });
    assertFailsClean(title, "p", "HarvestCrop", { slot: 1 }, "empty_slot", "펫 칸은 HarvestCrop 이 아니다");
});

test("13.3 새 작물 — 블루베리 15분 · 차 씨앗 → 찻잎 · 참나무 → 나무 4개", () => {
    const title = makeTitle();
    title.seed("p", "item.seed_blueberry", 1);
    title.seed("p", "item.seed_tea", 1);
    title.seed("p", "item.seed_oak", 1);
    assertEqual(title.call("p", "PlantCrop", { slot: 0, seedItemId: "item.seed_blueberry" }).cropItemId, "item.blueberry", "블루베리 심기");
    assertEqual(title.call("p", "PlantCrop", { slot: 1, seedItemId: "item.seed_tea" }).cropItemId, "item.tea_leaves", "차 씨앗 → 찻잎");
    assertEqual(title.call("p", "PlantCrop", { slot: 2, seedItemId: "item.seed_oak" }).cropItemId, "item.wood", "참나무 씨앗 → 나무");
    assertFailsClean(title, "p", "PlantCrop", { slot: 3, seedItemId: "item.seed_tea_leaves" }, "bad_args", "표와 다른 씨앗 이름");
    title.age("p", "ranch", 16 * 60 * 1000); // 15분 단만 익었다
    const coins = title.coins("p");
    assertEqual(title.call("p", "HarvestCrop", { slot: 1 }).harvested, 0, "찻잎(60분)은 아직");
    const berry = title.call("p", "HarvestCrop", { slot: 0 });
    assertEqual([berry.harvested, berry.itemId, berry.coins], [1, "item.blueberry", 5], "블루베리 1 + 5 코인");
    title.age("p", "ranch", H);
    const oak = title.call("p", "HarvestCrop", { slot: 2 });
    assertEqual([oak.harvested, oak.itemId, oak.coins], [4, "item.wood", 20], "참나무 = 나무 4 + 20 코인");
    assertEqual(title.count("p", "item.wood"), 4, "나무 4개 들어옴");
    assertEqual(title.call("p", "HarvestCrop", { slot: 1 }).itemId, "item.tea_leaves", "찻잎 거둠");
    assertEqual(title.coins("p") - coins, 5 + 20 + 20, "코인 합");
    ["item.blueberry", "item.wood", "item.tea_leaves"].forEach((id) => {
        assertTrue(title.getData("p", "codex").indexOf(id) !== -1, id + " 도감");
    });
});

test("작물 칸도 목장 유지가 끊기면 심기·수확이 잠긴다(계정 그대로)", () => {
    const title = makeTitle();
    title.seed("p", "item.seed_wheat", 2);
    title.call("p", "PlantCrop", { slot: 0, seedItemId: "item.seed_wheat" });
    title.age("p", "ranch", 25 * H);
    title.age("p", "ranchMeta", 25 * H);
    assertFailsClean(title, "p", "HarvestCrop", { slot: 0 }, "locked", "잠긴 작물 수확");
    assertFailsClean(title, "p", "PlantCrop", { slot: 1, seedItemId: "item.seed_wheat" }, "locked", "잠긴 목장 심기");
});

// ══ 8.3 탈것 — 목줄·종·안장 (M13-03, 결정 009) ══════════════════════════════════
test("도감 사냥 100% → 사냥 목줄 1개, 그 뒤 새 도감 칸에는 다시 안 준다", () => {
    const title = makeTitle();
    const hunt = title.sandbox.CODEX_CATEGORIES.hunt;
    title.setData("p", "codex", hunt.filter((id) => id !== "item.fox_tail"));
    title.call("p", "ClaimLoot", { items: [{ itemId: "item.wood", count: 1 }] });
    assertEqual(title.count("p", "item.collar_hunting"), 0, "99% 에서는 없다");
    title.setData("p", "lootAt", 0);
    title.call("p", "ClaimLoot", { items: [{ itemId: "item.fox_tail", count: 1 }] });
    assertEqual(title.count("p", "item.collar_hunting"), 1, "사냥 100% → 목줄 1개");
    assertEqual(title.count("p", "item.collar_farming") + title.count("p", "item.collar_fishing"), 0, "다른 목줄 0");
    assertEqual(title.getData("p", "codexCollars"), ["hunt"], "지급 표시");
    title.setData("p", "lootAt", 0);
    title.call("p", "ClaimLoot", { items: [{ itemId: "item.berry", count: 1 }] }); // 새 칸 → 목줄 판정을 다시 탄다
    assertTrue(title.getData("p", "codex").indexOf("item.berry") !== -1, "두 번째 호출도 도감에 올라감");
    assertEqual(title.count("p", "item.collar_hunting"), 1, "두 번째 호출 — 목줄 그대로 1개(더 받은 것 0)");
});

test("ExchangeCollar — 목줄 1개 → 종 하나 · 이미 가진 종·목줄 없음·모르는 종은 계정 그대로", () => {
    const title = makeTitle();
    const wolf = { collarItemId: "item.collar_hunting", speciesId: "animal.wolf" };
    assertFailsClean(title, "p", "ExchangeCollar", wolf, "not_enough_items", "목줄 없음");
    title.seed("p", "item.collar_hunting", 1);
    title.seed("p", "item.collar_fishing", 1);
    assertEqual(title.call("p", "ExchangeCollar", wolf).mounts, ["animal.wolf"], "늑대를 얻었다");
    assertEqual(title.count("p", "item.collar_hunting"), 0, "목줄 1개 빠짐");
    assertEqual(title.getData("p", "mounts").species, ["animal.wolf"], "내부 데이터에 남음");
    assertFailsClean(title, "p", "ExchangeCollar", { collarItemId: "item.collar_fishing", speciesId: "animal.wolf" }, "already", "이미 가진 종");
    assertFailsClean(title, "p", "ExchangeCollar", { collarItemId: "item.collar_fishing", speciesId: "animal.pheasant" }, "bad_args", "새는 탈것이 아니다");
    assertFailsClean(title, "p", "ExchangeCollar", { collarItemId: "item.plank", speciesId: "animal.bear" }, "bad_args", "목줄이 아닌 아이템");
    const elk = title.call("p", "ExchangeCollar", { collarItemId: "item.collar_fishing", speciesId: "animal.elk" });
    assertEqual(elk.mounts, ["animal.wolf", "animal.elk"], "두 번째 종(무스 = elk)");
    assertEqual(title.call("p", "GetMounts").species.length, 8, "고를 수 있는 종 8(원본)");
});

test("AttachSaddle — 가진 종에만, 안장 1개 소모, 바꾸면 전 안장은 안 돌려준다", () => {
    const title = makeTitle();
    const hunting = { speciesId: "animal.wolf", saddleItemId: "item.saddle_hunting" };
    title.seed("p", "item.saddle_hunting", 1);
    assertFailsClean(title, "p", "AttachSaddle", hunting, "locked", "없는 종");
    title.setData("p", "mounts", { species: ["animal.wolf"], saddles: {} });
    assertFailsClean(title, "p", "AttachSaddle", { speciesId: "animal.wolf", saddleItemId: "item.collar_hunting" }, "bad_args", "안장이 아닌 아이템");
    assertFailsClean(title, "p", "AttachSaddle", { speciesId: "animal.wolf", saddleItemId: "item.saddle_farming" }, "not_enough_items", "안장 없음");
    assertEqual(title.call("p", "AttachSaddle", hunting).saddles, { "animal.wolf": "item.saddle_hunting" }, "늑대에 사냥 안장");
    assertEqual(title.count("p", "item.saddle_hunting"), 0, "안장 1개 빠짐");
    title.seed("p", "item.saddle_fishing", 1);
    title.call("p", "AttachSaddle", { speciesId: "animal.wolf", saddleItemId: "item.saddle_fishing" });
    assertEqual(title.getData("p", "mounts").saddles, { "animal.wolf": "item.saddle_fishing" }, "종마다 1개 — 바꿈");
    assertEqual(title.count("p", "item.saddle_hunting"), 0, "전 안장은 돌아오지 않는다");
});

test("안장 제작 — 고급 작업대를 땅에 놓아야 한다", () => {
    const title = makeTitle();
    const tentId = withTent(title, "p");
    title.seed("p", "item.plank", 4);
    title.seed("p", "item.wolf_hide", 4);
    assertFailsClean(title, "p", "CraftItem", { recipeId: "item.saddle_hunting" }, "locked", "고급 작업대 없음");
    title.seed("p", "item.workbench_adv", 1);
    title.call("p", "PlaceStructure", { tentId, itemId: "item.workbench_adv", x: 1, y: 1 });
    assertEqual(title.call("p", "CraftItem", { recipeId: "item.saddle_hunting" }).crafted, "item.saddle_hunting", "사냥 안장");
    assertEqual(title.count("p", "item.plank") + title.count("p", "item.wolf_hide"), 0, "나무판 4 + 가죽 4 소모");
});

// ══ 13.5 모자 (M13-04) ══════════════════════════════════════════════════════════
/** 13.5 모자 표 — [{name, id, effect, source, shopPrice}]. shopPrice: 잡화점 값 · 상자 등이면 null. */
function readHats135() {
    const out = designLines.map((l) => l.split("|").slice(1, -1).map((c) => c.trim()))
        .filter((c) => c.length === 6 && /^`item\.[a-z_]+_hat`$/.test(c[2]))
        .map((c) => ({ name: c[0].replace(/\(바꿈\)$/, ""), id: c[2].slice(1, -1), effect: c[3], source: c[4],
            shopPrice: /^잡화점 \*\*(\d+)\*\*$/.test(c[4]) ? Number(/(\d+)/.exec(c[4])[1]) : null }));
    if (out.length === 0) throw new Error("DESIGN 13.5 모자 줄을 한 줄도 못 읽었다");
    return out;
}

function setRandom(title, value) {
    vm.runInContext("Math.random = function () { return " + value + "; };", title.sandbox);
}

function wearHat(title, pid, hatId) {
    title.seed(pid, hatId, 1);
    assertEqual(title.call(pid, "EquipHat", { itemId: hatId }).hat, hatId, hatId + " 쓰기");
}

test("모자 13.5 — 12종 서버 표·잡화점 8종 값·효과 숫자가 설계서와 같다", () => {
    const sb = makeTitle().sandbox;
    const hats = readHats135();
    assertEqual(hats.length, 12, "모자 12");   // M21-05c 새싹 모자(출석 7일째) · M21-07e 우두머리 모자 +1씩
    const shop = hats.filter((h) => h.shopPrice !== null);
    assertEqual(shop.length, 8, "잡화점 모자 8");
    shop.forEach((h) => assertEqual([sb.SHOP_ITEMS[h.id].name, sb.SHOP_ITEMS[h.id].price, sb.SHOP_ITEMS[h.id].count],
        [h.name, h.shopPrice, 1], h.id + " 상점 줄"));
    hats.filter((h) => h.shopPrice === null).forEach((h) => assertTrue(!sb.SHOP_ITEMS[h.id], h.id + " 는 상점에 없다"));
    hats.forEach((h) => assertTrue(sb.HAT_EFFECTS[h.id], h.id + " 서버 표"));
    const byId = {};
    hats.forEach((h) => { byId[h.id] = h.effect; });
    const num = (id, re) => Number(re.exec(byId[id])[1]);
    assertEqual(sb.HAT_EFFECTS["item.farmer_hat"].harvestBonusChance, num("item.farmer_hat", /\*\*(\d+)%\*\*/) / 100, "농부 확률");
    assertEqual(sb.HAT_EFFECTS["item.hunter_hat"].pristineMul, num("item.hunter_hat", /\*\*×([\d.]+)\*\*/), "사냥 배수");
    assertEqual(sb.HAT_EFFECTS["item.lumberjack_hat"].woodMul, num("item.lumberjack_hat", /\*\*×([\d.]+)\*\*/), "나무꾼 배수");
    assertEqual(sb.HAT_EFFECTS["item.looter_hat"].chestRollBonus, num("item.looter_hat", /\*\*\+(\d+)번\*\*/), "약탈자 굴림");
    assertEqual(sb.HAT_EFFECTS["item.traveler_hat"].fareMul, num("item.traveler_hat", /\*\*×([\d.]+)\*\*/), "여행자 배수");
    assertEqual(sb.HAT_EFFECTS["item.merchant_hat"].sellMul, num("item.merchant_hat", /\*\*×([\d.]+)\*\*/), "상인 배수");
});

test("EquipHat — 가방에 있는 모자만 쓴다(없는 모자·모자 아닌 것은 아무것도 안 바뀐다)", () => {
    const title = makeTitle();
    assertFailsClean(title, "p", "EquipHat", { itemId: "item.merchant_hat" }, "not_enough_items", "안 가진 장사치 중절");
    title.seed("p", "item.tent", 1);
    assertFailsClean(title, "p", "EquipHat", { itemId: "item.tent" }, "bad_args", "텐트는 모자가 아니다");
    wearHat(title, "p", "item.merchant_hat");
    assertEqual(title.getData("p", "hat"), "item.merchant_hat", "서버 기록");
    wearHat(title, "p", "item.fisher_hat");
    assertEqual(title.getData("p", "hat"), "item.fisher_hat", "새 모자가 전 모자를 바꾼다(칸 하나)");
});

test("EquipHat — itemId \"\" 는 벗기: 기록이 비고 효과가 끝난다 · 모자 아닌 것은 여전히 거절", () => {
    const title = makeTitle();
    const off = title.call("p", "EquipHat", { itemId: "" });
    assertEqual(off.hat, "", "안 쓴 채 벗기도 성공(멱등)");
    wearHat(title, "p", "item.merchant_hat");
    assertEqual(title.call("p", "EquipHat", { itemId: "" }).hat, "", "벗기");
    assertEqual(title.getData("p", "hat"), null, "서버 기록 비움");
    assertEqual(title.count("p", "item.merchant_hat"), 1, "모자는 가방에 그대로");
    title.seed("p", "item.perch", 3);
    assertEqual(title.call("p", "SellItem", { itemId: "item.perch", count: 3 }).coins, 6, "벗으면 상인 ×1.2 가 끝난다");
    title.seed("p", "item.tent", 1);
    assertFailsClean(title, "p", "EquipHat", { itemId: "item.tent" }, "bad_args", "벗은 뒤에도 텐트는 모자가 아니다");
});

test("GetBaseState.hat — 재접속 복원: 쓴 모자 ID, 벗으면 \"\", 버린 모자는 \"\" · 읽기만 한다", () => {
    const title = makeTitle();
    assertEqual(title.call("p", "GetBaseState").hat, "", "안 쓴 계정");
    wearHat(title, "p", "item.merchant_hat");
    const before = title.snapshot("p");
    assertEqual(title.call("p", "GetBaseState").hat, "item.merchant_hat", "쓴 모자");
    assertEqual(title.snapshot("p"), before, "읽기만 한다");
    assertEqual(title.call("q", "GetBaseState").hat, "", "다른 계정은 빈 값");
    title.call("p", "EquipHat", { itemId: "" });
    assertEqual(title.call("p", "GetBaseState").hat, "", "벗은 뒤");
    wearHat(title, "p", "item.fisher_hat");
    title.call("p", "DropItem", { itemId: "item.fisher_hat" });
    assertEqual(title.getData("p", "hat"), "item.fisher_hat", "기록은 남아 있어도");
    assertEqual(title.call("p", "GetBaseState").hat, "", "버린 모자는 효과처럼 빈 값");
});

test("모자 효과 — 쓰고/벗고 값이 13.5 대로(농부·상인·여행자·나무꾼), 버리면 효과도 끝", () => {
    // 농부: 30% 경계 — 0.29 는 +1, 0.3 은 그대로. 코인은 그대로.
    const farm = (hat, roll) => {
        const title = makeTitle();
        setRandom(title, roll);
        if (hat) wearHat(title, "p", hat);
        title.seed("p", "item.seed_carrot", 1);
        title.call("p", "PlantCrop", { slot: 0, seedItemId: "item.seed_carrot" });
        title.age("p", "ranch", H);
        const got = title.call("p", "HarvestCrop", { slot: 0 });
        return [got.harvested, got.coins, title.count("p", "item.carrot")];
    };
    assertEqual(farm(null, 0), [1, 5, 1], "모자 없이 당근 1");
    assertEqual(farm("item.farmer_hat", 0.29), [2, 5, 2], "밭두렁 갓 +1(코인 5 그대로)");
    assertEqual(farm("item.farmer_hat", 0.3), [1, 5, 1], "밭두렁 갓라도 30% 밖이면 1");
    assertEqual(farm("item.merchant_hat", 0), [1, 5, 1], "다른 모자는 수확에 안 붙는다");

    // 상인: 농어 2 × 3 = 6 → ×1.2 = 7.2 → 7
    const sell = (hat) => {
        const title = makeTitle();
        if (hat) wearHat(title, "p", hat);
        title.seed("p", "item.perch", 3);
        return title.call("p", "SellItem", { itemId: "item.perch", count: 3 }).coins;
    };
    assertEqual([sell(null), sell("item.merchant_hat")], [6, 7], "장사치 중절 판매 ×1.2 내림");

    // 여행자: 사냥존 5 → 3, 위험구역 10 → 5, 섬 5 → 3
    const title = makeTitle();
    title.setCoins("p", 100);
    const hp = title.sandbox.CODEX_CATEGORIES.hunt.concat(title.sandbox.CODEX_CATEGORIES.plant);
    title.setData("p", "codex", hp);
    const fares = () => ["hunt", "danger", "island"].map((d) => title.call("p", "PayFerry", { dest: d }).paid);
    assertEqual(fares(), [5, 10, 5], "모자 없이 뱃삯");
    wearHat(title, "p", "item.traveler_hat");
    assertEqual(fares(), [3, 5, 3], "여행자 모자 뱃삯 ×0.5 올림");
    assertEqual(title.coins("p"), 100 - 20 - 11, "낸 만큼만 빠졌다");

    // 나무꾼: 나무·향나무만 ×2, 고기는 그대로
    const loot = (hat) => {
        const t = makeTitle();
        if (hat) wearHat(t, "p", hat);
        t.call("p", "ClaimLoot", { items: [{ itemId: "item.wood", count: 4 }, { itemId: "item.cedar", count: 1 },
            { itemId: "item.fox_meat", count: 2 }] });
        return ["item.wood", "item.cedar", "item.fox_meat"].map((id) => t.count("p", id));
    };
    assertEqual(loot(null), [4, 1, 2], "모자 없이 나무 4·향나무 1");
    assertEqual(loot("item.lumberjack_hat"), [8, 2, 2], "도끼꾼 털모자 나무·향나무 ×2");

    // 버리면 끝: 서버 기록은 장사치 중절인데 가방에 없다 → 효과 없음
    const gone = makeTitle();
    wearHat(gone, "p", "item.merchant_hat");
    gone.call("p", "DropItem", { itemId: "item.merchant_hat" });
    gone.seed("p", "item.perch", 3);
    assertEqual(gone.call("p", "SellItem", { itemId: "item.perch", count: 3 }).coins, 6, "버린 모자는 효과 없음");
});

// ══ 13.8 위험구역 상자 · 보물 지도 (M13-10) ════════════════════════════════════
/** 설계서 표 전체에서 이름 → ID 목록. 이름 칸 「가 · 나」 와 ID 칸 「`a` · `b`」 를 짝짓고, 이름이 하나면(보석 5종) ID 전부. */
function designNameIds() {
    const map = {};
    designLines.filter((l) => l.startsWith("| ")).forEach((l) => {
        const c = l.split("|").slice(1, -1).map((x) => x.trim());
        const idCell = c.slice(1).find((x) => /^`item\.[a-z_]+`( · `item\.[a-z_]+`)*$/.test(x));
        if (!idCell) return;
        const ids = idCell.split(" · ").map((x) => x.slice(1, -1));
        const names = c[0].replace(/\(바꿈\)/g, "").split(" · ");
        if (names.length === ids.length) names.forEach((n, i) => { if (!map[n]) map[n] = [ids[i]]; });
        else if (names.length === 1 && !map[names[0]]) map[names[0]] = ids;
    });
    return map;
}

/** 13.8 「위험구역 상자 전리품 줄 추가」 → [{itemId, chance}] (설계서 줄 순서). */
function readChestLoot138() {
    const ids = designNameIds();
    const out = [];
    sectionRows("### 13.8").filter((c) => c.length === 3 && /^(각 )?\d+%( · \d+%)*$/.test(c[1])).forEach((c) => {
        const names = c[0].split(" · ");
        const pcts = c[1].replace(/^각 /, "").split(" · ").map((x) => Number(x.slice(0, -1)) / 100);
        names.forEach((n, i) => {
            if (!ids[n]) throw new Error("DESIGN 에서 '" + n + "' 의 ID 를 못 찾았다");
            ids[n].forEach((id) => out.push({ itemId: id, chance: pcts.length === 1 ? pcts[0] : pcts[i] }));
        });
    });
    if (out.length === 0) throw new Error("DESIGN 13.8 상자 전리품 줄을 한 줄도 못 읽었다");
    return out;
}

function seedRandom(title, seed) {
    vm.runInContext("(function(){var a=" + seed + ";Math.random=function(){a=a+0x6D2B79F5|0;var t=Math.imul(a^a>>>15,1|a);" +
        "t=t+Math.imul(t^t>>>7,61|t)^t;return((t^t>>>14)>>>0)/4294967296;};})();", title.sandbox);
}

const countIn = (list, id) => (list.find((e) => e.itemId === id) || { count: 0 }).count;

test("상자 13.8 — 전리품 22줄·설계도·재충전·보물 숫자가 설계서(13.8·8.10·10.6)와 같다", () => {
    const sb = makeTitle().sandbox;
    const loot = readChestLoot138();
    assertEqual(loot.length, 22, "13.8 전리품 22줄(전자 재료 3 포함 M20-02b · 찻잎 주머니 10-06 · 보트 부품 3 M32-01)");
    assertEqual(JSON.parse(JSON.stringify(sb.CHEST_LOOT)), loot, "서버 CHEST_LOOT = 13.8 표");
    const disclosure = sectionRows("### 10.6").filter((c) => /^`item\.[a-z_]+`$/.test(c[1]))
        .map((c) => ({ itemId: c[1].slice(1, -1), chance: Number(c[2].slice(0, -1)) / 100 }));
    assertEqual(disclosure, loot, "10.6 확률 고지 = 13.8 표");
    assertEqual(Number(/(\d+)%/.exec(sectionRows("### 10.6").find((c) => c[1] === "`item.blueprint_*`")[2])[1]) / 100,
        sb.CHEST_BLUEPRINT_CHANCE, "10.6 설계도 줄");
    const bp = designRow("설계도", "재충전")[1];
    assertEqual(sb.CHEST_BLUEPRINT_CHANCE, Number(/(\d+)%/.exec(bp)[1]) / 100, "8.10 설계도 %");
    assertEqual(sb.CHEST_RECHARGE_SECONDS, Number(/\*\*(\d+)초\*\*/.exec(bp)[1]), "8.10 재충전 초");
    assertEqual(sb.CHEST_COUNT, Number(/옆 (\d+)개/.exec(bp)[1]), "8.10 상자 수");
    const map = designRow("보물 지도", "굴림")[3];
    assertEqual(sb.TREASURE_SECONDS, Number(/\*\*(\d+)초\*\*/.exec(map)[1]), "보물 수명");
    assertEqual(sb.TREASURE_ROLLS, Number(/굴림 \*\*(\d+)번\*\*/.exec(map)[1]), "보물 굴림");
    sb.TREASURE_ZONES.forEach((z) => assertTrue(designLines.some((l) => l.indexOf("`" + z + "`") !== -1), z + " 가 1.3 사냥존"));
    const sells = sectionRows("### 13.8").filter((c) => /^판매 \*\*\d+\*\*/.test(c[3] || ""));
    assertEqual(sells.length, 3, "13.8 판매 줄 3");
    sells.forEach((c) => c[2].split(" · ").forEach((x) => assertEqual(sb.sellPriceOf(x.slice(1, -1)),
        Number(/\*\*(\d+)\*\*/.exec(c[3])[1]), x + " 판매가")));
    assertEqual(sb.sellPriceOf("item.treasure_map"), 0, "보물 지도는 못 판다(🟡)");
});

// M33-01 13.4 「판매가」 줄의 `item.*` **값** 을 서버 sellPriceOf 와 맞댄다 — 늪 채집물이 버릴 수밖에 없는 물건이 되지 않게.
test("13.4 채집물 판매가 — DESIGN 줄 = 서버", () => {
    const sb = makeTitle().sandbox;
    const row = sectionRows("### 13.4").find((c) => c[0] === "판매가");
    const pairs = [...row[1].matchAll(/`(item\.[a-z_]+)` \*\*(\d+)\*\*/g)].map((m) => [m[1], Number(m[2])]);
    assertEqual(pairs.map((p) => p[0]).sort(), ["item.bog_flower", "item.vipers_mane", "item.water_lily"], "13.4 판매가 줄 늪 셋");
    pairs.forEach(([id, want]) => assertEqual(sb.sellPriceOf(id), want, id + " 판매가"));
    pairs.forEach(([id]) => assertTrue(sb.sellPriceOf(id) > 0, id + " 는 팔 수 있다"));
    assertEqual(sb.sellPriceOf("item.chanterelle"), 0, "식용 버섯은 그대로 0(13.4 공짜 채집)");
});

test("상자 굴림 — 고정 씨앗 10만 번, 줄마다 확률이 표 값 ±4σ 안", () => {
    const title = makeTitle();
    const sb = title.sandbox;
    seedRandom(title, 20260924);
    const n = 100000;
    const got = {};
    let bps = 0;
    for (let i = 0; i < n; i++) {
        const r = sb.rollChest(1, []);
        r.items.forEach((e) => { got[e.itemId] = (got[e.itemId] || 0) + e.count; });
        bps += r.blueprints.length;
    }
    readChestLoot138().concat([{ itemId: "blueprint", chance: sb.CHEST_BLUEPRINT_CHANCE }]).forEach((line) => {
        const k = line.itemId === "blueprint" ? bps : (got[line.itemId] || 0);
        const mean = n * line.chance;
        const sigma = Math.sqrt(n * line.chance * (1 - line.chance));
        assertTrue(Math.abs(k - mean) <= 4 * sigma, line.itemId + " " + k + "/" + n + " (기대 " + mean + " ±" + Math.round(4 * sigma) + ")");
    });
});

test("OpenChest — 서버가 굴려 준다 · 재충전 600초 · 약탈자 모자 +1 · 인자 아이템 무시 · 칸 부족은 발밑", () => {
    const all = makeTitle();
    setRandom(all, 0);
    const r = all.call("p", "OpenChest", { chest: 1 });
    assertEqual([r.rolls, r.items.length, r.blueprints.length], [1, 23, 1], "0 이면 23줄 전부 + 설계도 1");
    // 배 짐칸는 배낭 칸을 안 늘린다(M32-02 보정) → 22줄 > 20칸이라 통째 발밑.
    assertTrue(r.drop && r.drop.items.length === 23 && all.count("p", "item.boat_container") === 0,
        "22줄 > 20칸 → 컨테이너가 끼어도 전부 발밑 (M32-02 보정)");
    assertEqual(all.getData("p", "blueprints"), r.blueprints, "설계도 내부 데이터");
    assertFailsClean(all, "p", "OpenChest", { chest: 1 }, "recharging", "600초 안 다시");
    assertEqual(all.call("p", "OpenChest", { chest: 2 }).rolls, 1, "상자 2 는 따로");
    all.age("p", "chests", 600000);
    assertEqual(all.call("p", "OpenChest", { chest: 1 }).items.length, 23, "600초 뒤 다시 열림");
    assertFailsClean(all, "p", "OpenChest", { chest: 3 }, "bad_args", "없는 상자");
    assertFailsClean(all, "p", "OpenChest", {}, "bad_args", "상자 번호 없음");

    const none = makeTitle();
    setRandom(none, 0.9999);
    const empty = none.call("p", "OpenChest", { chest: 1, items: [{ itemId: "item.ruby", count: 99 }] });
    assertEqual([empty.items, empty.blueprints, none.count("p", "item.ruby")], [[], [], 0], "0.9999 면 없음 — 인자 아이템은 안 준다");

    const some = makeTitle();
    setRandom(some, 0.035);   // 3.5% 보다 큰 줄만 = 17줄 + 열쇠패(5%) 1 = 18줄 — 20칸 안
    const most = some.call("p", "OpenChest", { chest: 1 });
    assertEqual([most.items.length, !!most.drop, some.count("p", "item.water_canister"), some.count("p", "item.boat_motor")],
        [18, false, 1, 0], "0.035 면 18줄(열쇠패 포함)이 가방에 · 배 추진기(3%)는 없음");

    const looter = makeTitle();
    wearHat(looter, "p", "item.looter_hat");
    setRandom(looter, 0.035);
    const two = looter.call("p", "OpenChest", { chest: 1 });
    assertEqual([two.rolls, countIn(two.items, "item.ruby"), looter.count("p", "item.ruby")], [2, 2, 2], "약탈자 모자 굴림 2번");

    const full = makeTitle();
    fillSlots(full, "p", 20);
    setRandom(full, 0);
    const over = full.call("p", "OpenChest", { chest: 1 });
    assertTrue(over.drop && over.drop.items.length === 23, "칸 부족 → 전리품 23줄이 발밑 가방");
    assertEqual(full.count("p", "item.ruby"), 0, "가방엔 안 들어감");
    assertFailsClean(full, "p", "OpenChest", { chest: 1 }, "recharging", "발밑으로 가도 상자는 열린 것");
});

test("금고 8.1c — 표 숫자가 설계서와 같고, 열쇠패 1장·3번 굴림·24시간·실패는 열쇠패를 안 뺀다 (M104-08)", () => {
    const title = makeTitle();
    const sb = title.sandbox;
    const rows = designLines.filter((l) => /^\| .+ \| `item\.[a-z_]+`/.test(l) && /\| \d+% \|/.test(l));
    const pct = {};
    const start = designLines.findIndex((l) => l.startsWith("### 8.1c"));
    const end = designLines.findIndex((l, i) => i > start && l.startsWith("### "));
    designLines.slice(start, end).forEach((l) => {
        const c = l.split("|").slice(1, -1).map((x) => x.trim());
        const m = c.length === 4 && /^(?:각 )?(\d+)%$/.exec(c[2]);
        if (m) c[1].split("·").map((x) => x.trim().replace(/`/g, "")).filter((x) => /^item\.[a-z_]+$/.test(x)).forEach((id) => { pct[id] = Number(m[1]); });
    });
    const server = {};
    sb.VAULT_LOOT.forEach((l) => { server[l.itemId] = Math.round(l.chance * 100); });
    assertEqual(server, pct, "VAULT_LOOT = DESIGN 8.1c 표 (설계도 줄 제외)");
    assertEqual([sb.VAULT_ROLLS, sb.VAULT_RECHARGE_SECONDS, sb.VAULT_BLUEPRINT_CHANCE], [3, 86400, 0.3], "굴림 3번·24시간·설계도 30%");

    assertFailsClean(title, "p", "OpenVault", { ruin: 0 }, "bad_args", "없는 폐허");
    assertFailsClean(title, "p", "OpenVault", { ruin: 4 }, "bad_args", "없는 폐허 4");
    assertFailsClean(title, "p", "OpenVault", { ruin: 1 }, "no_key", "열쇠패 없음");
    assertEqual(title.getData("p", "vaults"), undefined, "실패는 기록을 안 남긴다");
    title.seed("p", "item.vault_pass", 2);
    setRandom(title, 0);
    const r = title.call("p", "OpenVault", { ruin: 1 });
    assertEqual([r.rolls, title.count("p", "item.vault_pass")], [3, 1], "굴림 3번 + 열쇠패 1장 소모");
    assertEqual(countIn(r.items, "item.ammo_rifle"), 60, "소총탄은 한 번에 20발 × 3번");
    assertEqual(countIn(r.items, "item.ruby"), 3, "0 이면 줄마다 3번 다 맞는다");
    const again = title.call("p", "OpenVault", { ruin: 1 });
    assertEqual([again.code, title.count("p", "item.vault_pass")], ["recharging", 1], "24시간 안 — 열쇠패 안 빠짐");
    assertTrue(again.waitMs > 0, "남은 시간");
    assertEqual(title.call("p", "OpenVault", { ruin: 2 }).rolls, 3, "다른 금고는 따로 센다");
    title.seed("p", "item.vault_pass", 1);
    title.age("p", "vaults", 86400000);
    assertEqual(title.call("p", "OpenVault", { ruin: 1 }).rolls, 3, "24시간 뒤 다시 열린다");
});

test("금고 열쇠패 얻는 길 — 우두머리 주 1장·상자/보물 굴림 5%·건네기 제외 (M104-08)", () => {
    const title = makeTitle();
    setRandom(title, 0.99);
    const boss = title.call("p", "ClaimBoss", {});
    assertEqual([boss.vaultPass, title.count("p", "item.vault_pass")], [1, 1], "우두머리 보상 때 열쇠패 1장(꽝이어도)");
    assertFailsClean(title, "p", "ClaimBoss", {}, "already_claimed", "같은 주엔 또 안 준다");
    assertEqual(title.count("p", "item.vault_pass"), 1, "거절된 날은 열쇠패 안 늘음");
    setRandom(title, 0.04);
    const chest = title.call("p", "OpenChest", { chest: 1 });
    assertEqual(countIn(chest.items, "item.vault_pass"), 1, "상자 굴림 5% (0.04 < 0.05)");
    setRandom(title, 0.06);
    const none = title.call("p", "OpenChest", { chest: 2 });
    assertEqual(countIn(none.items, "item.vault_pass"), 0, "0.06 은 안 맞는다");
    assertTrue(title.sandbox.isGiftExcluded("item.vault_pass"), "열쇠패는 건네지 못한다");
});

test("보물 지도 — 쓰면 1장 빠지고 600초 안 보물 상자 굴림 2번, 한 번만", () => {
    const title = makeTitle();
    assertFailsClean(title, "p", "UseTreasureMap", {}, "not_enough_items", "지도 없음");
    title.seed("p", "item.treasure_map", 2);
    setRandom(title, 0.5);
    const t = title.call("p", "UseTreasureMap", {});
    assertEqual([t.zoneId, t.u, t.v, title.count("p", "item.treasure_map")], [title.sandbox.TREASURE_ZONES[2], 0.5, 0.5, 1], "지도 1장 빠지고 자리");
    assertFailsClean(title, "p", "UseTreasureMap", {}, "busy", "찾는 중엔 새 지도 못 씀");
    assertFailsClean(title, "p", "OpenChest", { treasureId: "x" }, "no_treasure", "다른 보물");
    setRandom(title, 0);
    const r = title.call("p", "OpenChest", { treasureId: t.treasureId });
    assertEqual([r.rolls, countIn(r.items, "item.salt")], [2, 2], "보물 상자 굴림 2번");
    assertFailsClean(title, "p", "OpenChest", { treasureId: t.treasureId }, "no_treasure", "두 번 못 연다");

    const late = makeTitle();
    late.seed("p", "item.treasure_map", 1);
    const lt = late.call("p", "UseTreasureMap", {});
    late.age("p", "treasure", 600000);
    assertEqual(late.call("p", "OpenChest", { treasureId: lt.treasureId }).code, "expired", "600초 지나면 사라짐");
    assertEqual([late.getData("p", "treasure"), late.count("p", "item.ruby")], [null, 0], "사라진 보물은 지우고 아무것도 안 준다");
});

test("GetTreasure — 존을 옮겨도 서버 기록으로 같은 자리를 다시 받고, 열거나 지나면 없다 (M13-15)", () => {
    const title = makeTitle();
    assertEqual(title.call("p", "GetTreasure", {}), { treasure: null }, "지도 안 쓰면 없음");
    title.seed("p", "item.treasure_map", 1);
    const t = title.call("p", "UseTreasureMap", {});
    title.age("p", "treasure", 300000); // 300초 지나 다른 존을 다녀왔다
    const again = title.call("p", "GetTreasure", {}).treasure;
    assertEqual([again.treasureId, again.zoneId, again.u, again.v], [t.treasureId, t.zoneId, t.u, t.v], "같은 보물·같은 자리");
    assertEqual(title.getData("p", "treasure").treasureId, t.treasureId, "읽기만 해서는 안 지운다");
    assertEqual(title.call("p", "OpenChest", { treasureId: again.treasureId }).rolls, 2, "다시 받은 ID 로 열린다");
    assertEqual(title.call("p", "GetTreasure", {}), { treasure: null }, "열면 사라진다");

    const late = makeTitle();
    late.seed("p", "item.treasure_map", 1);
    late.call("p", "UseTreasureMap", {});
    late.age("p", "treasure", 600000);
    assertEqual(late.call("p", "GetTreasure", {}), { treasure: null }, "600초 지나면 없음");
    assertEqual(late.getData("p", "treasure"), null, "지난 기록은 지운다");
    late.seed("p", "item.treasure_map", 1);
    assertTrue(late.call("p", "UseTreasureMap", {}).treasureId !== undefined, "지운 뒤엔 새 지도를 쓸 수 있다");
});

// ══ 13.5 도구 (M13-05) ══════════════════════════════════════════════════════════
/** 13.5 도구 표 — [{name, id, price}] (잡화점 칸이 「**N**」 인 6칸 줄). */
function readTools135() {
    const out = sectionRows("### 13.5").filter((c) => c.length === 6 && /^`item\.[a-z_]+`$/.test(c[2]) && /^\*\*\d+\*\*$/.test(c[4]))
        .map((c) => ({ name: c[0], id: c[2].slice(1, -1), price: Number(c[4].slice(2, -2)) }));
    if (out.length === 0) throw new Error("DESIGN 13.5 도구 줄을 한 줄도 못 읽었다");
    return out;
}

test("도구 13.5 — 4종 잡화점 이름·값이 설계서와 같다", () => {
    const sb = makeTitle().sandbox;
    const tools = readTools135();
    assertEqual(tools.map((t) => t.id), ["item.chainsaw", "item.machete", "item.binoculars", "item.satellite_communicator"], "도구 4");
    tools.forEach((t) => assertEqual(sb.SHOP_ITEMS[t.id], { name: t.name, price: t.price, count: 1 }, t.id + " 잡화점"));
});

test("도구 효과 — 전동 톱 나무 +1 · 마체테 채집 +1 은 가방에 있을 때 그 도구로 얻은 번만, 나무꾼 ×2 는 그 뒤", () => {
    const loot = (bag, items) => {
        const t = makeTitle();
        bag.forEach((id) => (/_hat$/.test(id) ? wearHat(t, "p", id) : t.seed("p", id, 1)));
        assertTrue(t.call("p", "ClaimLoot", { items: items }).granted, "받기 성공");
        return items.map((e) => t.count("p", e.itemId));
    };
    const trees = [{ itemId: "item.wood", count: 4, toolUses: 1 }, { itemId: "item.cedar", count: 3, toolUses: 2 },
        { itemId: "item.fox_meat", count: 2, toolUses: 1 }];
    assertEqual(loot([], trees), [4, 3, 2], "도구 없으면 그대로");
    assertEqual(loot(["item.chainsaw"], trees), [5, 5, 2], "전동 톱 — 벤 그루마다 +1, 고기는 그대로");
    assertEqual(loot(["item.machete"], trees), [4, 3, 2], "마체테는 나무에 안 붙는다");
    assertEqual(loot(["item.chainsaw", "item.lumberjack_hat"], trees), [10, 10, 2], "(4+1)×2 · (3+2)×2 — +1 먼저, ×2 뒤");
    const piles = [{ itemId: "item.chanterelle", count: 2, toolUses: 2 }, { itemId: "item.resin", count: 1, toolUses: 5 },
        { itemId: "item.poppy", count: 3 }];
    assertEqual(loot(["item.machete"], piles), [4, 2, 3], "마체테 — 번마다 +1, 개수보다 많이 와도 개수까지, 안 온 줄은 그대로");
    assertEqual(loot(["item.chainsaw"], piles), [2, 1, 3], "전동 톱은 채집 더미에 안 붙는다");
    assertEqual(loot(["item.chainsaw"], [{ itemId: "item.wood", count: 2, toolUses: -1 }, { itemId: "item.cedar", count: 2, toolUses: "2" }]),
        [2, 2], "이상한 toolUses 는 무시");
});

// ══ 8.16 채광·제련 (M14-01) ═════════════════════════════════════════════════════
const MINING_RECIPES = ["item.coal_ingot", "item.copper_ingot", "item.iron_ingot", "item.silver_ingot", "item.gold_ingot",
    "item.titanium_ingot", "item.metal_parts", "item.sheet_metal", "item.pickaxe_advanced", "item.smelter"];

/** DESIGN 8.16 표의 한 줄 값 칸. */
function mining816(label) {
    // 8.16 절 안에서만 — 8.11 「판매가」 줄이 먼저 걸리지 않게
    const start = designLines.findIndex((l) => l.startsWith("### 8.16 "));
    const end = designLines.findIndex((l, i) => i > start && l.startsWith("### "));
    if (start === -1 || end <= start) throw new Error("DESIGN 8.16 절을 못 찾았다");
    const row = designLines.slice(start, end).map((l) => l.split("|").slice(1, -1).map((c) => c.trim()))
        .find((c) => c.length === 3 && c[0].indexOf(label) === 0);
    if (!row) throw new Error("DESIGN 8.16 「" + label + "」 줄을 못 찾았다");
    return row[1];
}

test("8.16 판매가 — 덩어리 표 값 · 주괴 = 재료 합 + 1 · 쇠붙이 부속 3 · 철판 = 철 주괴 2개 + 1", () => {
    const sb = makeTitle().sandbox;
    const names = { "석탄": "coal", "구리": "copper", "철": "iron", "티타늄": "titanium", "은": "silver", "금": "gold" };
    const line = /덩어리: (.+?) · 주괴/.exec(mining816("판매가"))[1];
    line.split(" · ").forEach((part) => {
        const m = /^(\S+) (\d+)$/.exec(part);
        assertEqual(sb.sellPriceOf("item." + names[m[1]] + "_chunk"), Number(m[2]), m[1] + " 덩어리");
    });
    assertEqual(sb.sellPriceOf("item.coal_ingot"), 1 * 2 + 1, "코크스 = 석탄 2 + 1");
    assertEqual(sb.sellPriceOf("item.iron_ingot"), 2 * 2 + 1 + 1, "철 주괴 = 철 2 + 석탄 1 + 1");
    assertEqual(sb.sellPriceOf("item.gold_ingot"), 8 * 2 + 1 + 1, "금 주괴");
    assertEqual(sb.sellPriceOf("item.metal_parts"), Number(/쇠붙이 부속 (\d+)/.exec(mining816("판매가"))[1]), "쇠붙이 부속 = 표 값");
    assertEqual(sb.sellPriceOf("item.sheet_metal"), 6 * 2 + 1, "철판 = 철 주괴 2개 값 + 1");
    assertEqual([sb.sellPriceOf("item.pickaxe_basic"), sb.sellPriceOf("item.pickaxe_electric")], [0, 0], "산 곡괭이는 못 판다");
});

test("8.16 곡괭이 사기 — 잡화점 기본 10 · 전기 150, 고급은 고급 작업대 제작", () => {
    const sb = makeTitle().sandbox;
    const got = /기본 = 잡화점 \*\*(\d+) 코인\*\*.*전기 = 잡화점 \*\*(\d+) 코인\*\*/.exec(mining816("곡괭이 얻기"));
    assertEqual([sb.SHOP_ITEMS["item.pickaxe_basic"].price, sb.SHOP_ITEMS["item.pickaxe_electric"].price],
        [Number(got[1]), Number(got[2])], "사는 목록 두 줄");
    assertEqual(sb.SHOP_ITEMS["item.pickaxe_advanced"], undefined, "강철 곡괭이는 안 판다");
    const title = makeTitle();
    title.setCoins("p", 10);
    assertEqual(title.call("p", "BuyItem", { itemId: "item.pickaxe_basic" }).cost, 10, "나무자루 곡괭이 10 코인");
    assertEqual([title.count("p", "item.pickaxe_basic"), title.coins("p")], [1, 0], "곡괭이 +1 · 코인 10 → 0");
});

test("8.16 제련 — 제련소를 땅에 놓아야 주괴가 되고, 덩어리 2 + 석탄 1(코크스는 석탄 2)", () => {
    const title = makeTitle();
    const tentId = withTent(title, "p");
    title.seed("p", "item.plank", 4);
    title.seed("p", "item.iron_chunk", 10, { stack: true });
    title.seed("p", "item.coal_chunk", 3, { stack: true });
    assertFailsClean(title, "p", "CraftItem", { recipeId: "item.iron_ingot" }, "locked", "제련소 없음");
    assertEqual(title.call("p", "CraftItem", { recipeId: "item.smelter" }).crafted, "item.smelter", "제련소 = 작업대 나무판 4 + 철 덩어리 6");
    assertEqual([title.count("p", "item.plank"), title.count("p", "item.iron_chunk")], [0, 4], "나무판 4 · 철 덩어리 6 소모");
    title.call("p", "PlaceStructure", { tentId, itemId: "item.smelter", x: 1, y: 1 });
    assertEqual(title.call("p", "CraftItem", { recipeId: "item.iron_ingot" }).crafted, "item.iron_ingot", "철 주괴");
    assertEqual([title.count("p", "item.iron_chunk"), title.count("p", "item.coal_chunk"), title.count("p", "item.iron_ingot")],
        [2, 2, 1], "철 2 · 석탄 1 → 주괴 1");
    assertEqual(title.call("p", "CraftItem", { recipeId: "item.coal_ingot" }).crafted, "item.coal_ingot", "코크스");
    assertEqual(title.count("p", "item.coal_chunk"), 0, "코크스 = 석탄 덩어리 2");
    assertEqual(title.call("p", "CraftItem", { recipeId: "item.metal_parts" }).crafted, "item.metal_parts", "쇠붙이 부속");
    assertEqual([title.count("p", "item.metal_parts"), title.count("p", "item.iron_ingot")], [2, 0], "철 주괴 1 → 쇠붙이 부속 ×2");
});

test("8.16 강철 곡괭이 — 고급 작업대에서 철 주괴 3 + 나무판 2", () => {
    const title = makeTitle();
    const tentId = withTent(title, "p");
    title.seed("p", "item.iron_ingot", 3, { stack: true });
    title.seed("p", "item.plank", 2, { stack: true });
    assertFailsClean(title, "p", "CraftItem", { recipeId: "item.pickaxe_advanced" }, "locked", "고급 작업대 없음");
    title.seed("p", "item.workbench_adv", 1);
    title.call("p", "PlaceStructure", { tentId, itemId: "item.workbench_adv", x: 1, y: 1 });
    assertEqual(title.call("p", "CraftItem", { recipeId: "item.pickaxe_advanced" }).crafted, "item.pickaxe_advanced", "강철 곡괭이");
    assertEqual(title.count("p", "item.iron_ingot") + title.count("p", "item.plank"), 0, "재료 소모");
});

test("8.16 광맥 캐기 — 덩어리 6종을 받고, 전동 곡괭이면 번마다 +1, 사냥에 안 센다", () => {
    const chunks = ["item.coal_chunk", "item.copper_chunk", "item.iron_chunk", "item.titanium_chunk", "item.silver_chunk", "item.gold_chunk"];
    const loot = (bag) => {
        const t = makeTitle();
        bag.forEach((id) => t.seed("p", id, 1));
        assertTrue(t.call("p", "ClaimLoot", { items: chunks.map((id) => ({ itemId: id, count: 2, toolUses: 1 })) }).granted, "받기 성공");
        const ach = t.getData("p", "achievements");
        assertEqual((ach && ach.counters && ach.counters.hunt) || 0, 0, "캐기는 사냥에 안 센다");
        return chunks.map((id) => t.count("p", id));
    };
    assertEqual(loot([]), [2, 2, 2, 2, 2, 2], "전동 곡괭이 없으면 그대로");
    assertEqual(loot(["item.pickaxe_electric"]), [3, 3, 3, 3, 3, 3], "전동 곡괭이 +1");
    assertEqual(loot(["item.machete"]), [2, 2, 2, 2, 2, 2], "마체테는 광맥에 안 붙는다");
    assertFailsClean(makeTitle(), "p", "ClaimLoot", { items: [{ itemId: "item.iron_ingot", count: 1 }] }, "bad_args", "주괴는 못 받는다");
});

// ══ 페인트 (DESIGN 13.8, M13-11) ════════════════════════════════════════════════
function readPaints138() {
    const ids = designRow("아이템", "item.paint_white")[1].match(/`item\.paint_[a-z]+`/g).map((c) => c.slice(1, -1));
    const names = designRow("이름", "하양 물감")[1].match(/`[^`]+`/g).map((c) => c.slice(1, -1));
    const price = Number(/잡화점 각 \*\*(\d+)\*\*/.exec(designRow("값", "잡화점 각")[1])[1]);
    return { ids, names, price };
}

function paintReady(title, pid, paintId, cans) {
    const tentId = withTent(title, pid);
    title.seed(pid, "item.bench", 1);
    if (cans) title.seed(pid, paintId, cans);
    const s = title.call(pid, "PlaceStructure", { tentId, itemId: "item.bench", x: 1, y: 1, rot: 0 });
    assertTrue(s.structureId, "칠할 벤치 놓기");
    return { tentId, structureId: s.structureId };
}

test("페인트 13.8 — 7종 ID·이름·잡화점 값이 설계서와 같다", () => {
    const sb = makeTitle().sandbox;
    const d = readPaints138();
    assertEqual(d.ids.length, 7, "설계서 페인트 7");
    assertEqual(d.names.length, 7, "설계서 이름 7");
    assertEqual(Array.from(sb.PAINT_ITEMS), d.ids, "서버 PAINT_ITEMS 순서까지");
    d.ids.forEach((id, i) => assertEqual(sb.SHOP_ITEMS[id], { name: d.names[i], price: d.price, count: 1 }, id + " 잡화점"));
    d.ids.forEach((id) => assertEqual(sb.sellPriceOf(id), 0, id + " 판매가 0"));
});

// DESIGN 13절 「셔츠·바지」(M9-05) — `item.flannel_shirt`(플란넬 셔츠, `OutfitTop`) · … / 잡화점 **각 5 코인** · 판매 **0**
function readOutfits13() {
    const row = designRow("🟡 아이템", "item.flannel_shirt")[1];
    const pairs = [...row.matchAll(/`(item\.[a-z_]+)`\(([^,]+), `(Outfit[A-Za-z]+)`\)/g)];
    const where = designRow("🟡 얻는 곳", "8.14 상점 물건")[1];
    return { ids: pairs.map((m) => m[1]), names: pairs.map((m) => m[2]), slots: pairs.map((m) => m[3]),
        price: Number(/각 (\d+) 코인/.exec(where)[1]), sell: Number(/판매 \*\*(\d+)\*\*/.exec(where)[1]) };
}

test("셔츠·바지 — 잡화점 이름·값이 설계서와 같고 사면 한 벌씩, 판매 0", () => {
    const title = makeTitle();
    const d = readOutfits13();
    assertEqual(d.ids, ["item.flannel_shirt", "item.work_pants"], "설계서 옷 2");
    assertEqual(d.slots, ["OutfitTop", "OutfitBottom"], "설계서 칸");
    d.ids.forEach((id, i) => assertEqual(title.sandbox.SHOP_ITEMS[id], { name: d.names[i], price: d.price, count: 1 }, id + " 잡화점"));
    title.setCoins("p", d.price * 2);
    d.ids.forEach((id) => assertEqual(title.call("p", "BuyItem", { itemId: id }).cost, d.price, id + " 사기"));
    assertEqual([title.coins("p"), title.count("p", d.ids[0]), title.count("p", d.ids[1])], [0, 1, 1], "잔액 0 · 한 벌씩");
    d.ids.forEach((id) => assertEqual(title.sandbox.sellPriceOf(id), d.sell, id + " 판매가"));
    assertFailsClean(title, "p", "BuyItem", { itemId: d.ids[0] }, "not_enough_coins", "잔액 0 이면 못 산다");
});

test("PaintStructure — 1통 빠지고 base 가구 줄 color 에 페인트 ID, 다른 색으로 다시 칠할 수 있다", () => {
    const title = makeTitle();
    title.setCoins("p", 4);
    assertEqual(title.call("p", "BuyItem", { itemId: "item.paint_red" }).cost, 2, "잡화점에서 빨강 2코인");
    const { tentId, structureId } = paintReady(title, "p", "item.paint_mint", 1);
    const houseColor = title.getData("p", "base")[0].color;
    const r = title.call("p", "PaintStructure", { tentId, structureId, paintId: "item.paint_red" });
    assertEqual(r, { painted: true, tentId, structureId, color: "item.paint_red" }, "응답");
    assertEqual(title.count("p", "item.paint_red"), 0, "빨강 1통 빠짐");
    assertEqual(title.getData("p", "base")[0].items[0].color, "item.paint_red", "서버 기록");
    title.call("p", "PaintStructure", { tentId, structureId, paintId: "item.paint_mint" });
    assertEqual([title.count("p", "item.paint_mint"), title.getData("p", "base")[0].items[0].color], [0, "item.paint_mint"], "민트로 덧칠");
    assertEqual(title.getData("p", "base")[0].color, houseColor, "텐트(집) 색은 안 건드린다");
});

test("PaintStructure — 거절 길은 아무것도 안 바뀐다(페인트 아님·통 없음·같은 색·없는 가구·없는 텐트·남의 가구)", () => {
    const title = makeTitle();
    const { tentId, structureId } = paintReady(title, "p", "item.paint_blue", 2);
    title.seed("p", "item.wood", 1);
    assertFailsClean(title, "p", "PaintStructure", { tentId, structureId, paintId: "item.wood" }, "bad_args", "나무는 페인트가 아니다");
    assertFailsClean(title, "p", "PaintStructure", { tentId, structureId, paintId: "item.paint_red" }, "not_enough_items", "빨강 통 없음");
    title.call("p", "PaintStructure", { tentId, structureId, paintId: "item.paint_blue" });
    assertFailsClean(title, "p", "PaintStructure", { tentId, structureId, paintId: "item.paint_blue" }, "same_color", "같은 색 다시");
    assertFailsClean(title, "p", "PaintStructure", { tentId, structureId: "nope", paintId: "item.paint_blue" }, "not_found", "없는 가구");
    assertFailsClean(title, "p", "PaintStructure", { tentId: "nope", structureId, paintId: "item.paint_blue" }, "not_found", "없는 텐트");
    title.seed("q", "item.paint_blue", 1);
    assertFailsClean(title, "q", "PaintStructure", { tentId, structureId, paintId: "item.paint_blue" }, "not_found", "남의 텐트·가구", ["p"]);
    assertEqual(title.count("p", "item.paint_blue"), 1, "p 는 1통만 썼다");
});

test("페인트는 집 색(8.11)과 따로 — UpgradeHouse 는 여전히 페인트 없이 색을 고른다", () => {
    const title = makeTitle();
    const { tentId, structureId } = paintReady(title, "p", "item.paint_yellow", 1);
    title.call("p", "PaintStructure", { tentId, structureId, paintId: "item.paint_yellow" });
    title.seed("p", "item.wood", 150, { stack: true });
    title.seed("p", "item.cedar", 50, { stack: true });
    assertEqual(title.call("p", "UpgradeHouse", { tentId, color: 4 }).color, 4, "집 색 4 무료");
    const tent = title.getData("p", "base")[0];
    assertEqual([tent.color, tent.items[0].color], [4, "item.paint_yellow"], "집 색과 가구 색이 따로 남는다");
});

// ══ 8.11 「집 2층·전력」 전자 재료 (M20-02) ═════════════════════════════════════════
const ELECTRIC_RECIPES = ["item.polymer", "item.circuit_board", "item.electric_motor", "item.solar_kit", "item.battery"];
const FLOOR2_RECIPES = ["item.stair_kit"]; // 8.11 층계 짜맞춤 (M20-03) — 아래 2층 시험이 본다
const POWER_RECIPES = ["item.lamp_electric"]; // 8.11 전등 (M20-04a) — 아래 전력 시험이 본다
const ELECTRIC_SHOP = ["item.solar_kit", "item.battery", "item.light_bulb"];
const POWER_PLACEABLES = ["item.solar_kit", "item.battery", "item.lamp_electric"]; // 8.11 전력 — 놓는 가구 (M20-04a)
const ELECTRIC_NAMES = {
    "수지": "item.resin", "구리 주괴": "item.copper_ingot", "철 주괴": "item.iron_ingot", "코크스": "item.coal_ingot",
    "철판": "item.sheet_metal", "폴리머": "item.polymer", "회로판": "item.circuit_board",
    "햇빛 집열판": "item.solar_kit", "키트": "item.solar_kit", "배터리": "item.battery", "전구": "item.light_bulb"
};

/** DESIGN 8.11 「집 2층·전력」 표에서 항목 칸에 label 이 든 줄의 값 칸. */
function power811(label) {
    const start = designLines.findIndex((l) => l.startsWith("**집 2층·전력"));
    if (start === -1) throw new Error("DESIGN 8.11 「집 2층·전력」 표를 못 찾았다");
    for (let i = start; i < designLines.length && !designLines[i].startsWith("**가구"); i++) {
        const c = designLines[i].split("|").slice(1, -1).map((x) => x.trim());
        if (c.length === 3 && c[0].indexOf(label) !== -1) return c[1];
    }
    throw new Error("DESIGN 8.11 「" + label + "」 줄을 못 찾았다");
}

/** "구리 주괴 2 + 폴리머 1" → RECIPES 재료 꼴. */
function electricInputs(text) {
    return text.split(" + ").map((part) => {
        const m = /^(.+?) (\d+)$/.exec(part.trim());
        if (!m || !ELECTRIC_NAMES[m[1]]) throw new Error("재료를 못 읽었다: " + part);
        return { itemId: ELECTRIC_NAMES[m[1]], count: Number(m[2]) };
    });
}

test("8.11 전자 재료 — 6종이 DT_ItemTable 에, 레시피 5·잡화점 3·판매가가 설계서 표와 같다", () => {
    const sb = makeTitle().sandbox;
    const rows = dumpItemRows();
    const want = { "item.solar_kit": "햇빛 집열판", "item.light_bulb": "전구", "item.battery": "배터리",
        "item.polymer": "굳힘 수지", "item.circuit_board": "회로판", "item.electric_motor": "소형 전동기" };
    Object.keys(want).forEach((id) => {
        const row = rows.find((r) => r.ItemID === id);
        assertTrue(row, id + " 가 DT_ItemTable 에 있다");
        // 키트·배터리는 놓는 가구(M20-04a) — 나머지 넷은 재료
        const type = POWER_PLACEABLES.indexOf(id) !== -1 ? "Placeable" : "Material";
        assertEqual([row.DisplayName, row.ItemType], [want[id], type], id + " 이름·종류");
    });
    const bold = (label) => /\*\*(.+?)\*\*/.exec(power811(label))[1];
    const expect = {
        "item.polymer": [false, electricInputs(bold("굳힘 수지"))],
        "item.circuit_board": [true, electricInputs(bold("회로판"))],
        "item.electric_motor": [true, electricInputs(bold("소형 전동기"))]
    };
    const kitLine = power811("키트·배터리 만들기");
    expect["item.solar_kit"] = [true, electricInputs(/햇빛 집열판 = \*\*(.+?)\*\*/.exec(kitLine)[1])];
    expect["item.battery"] = [true, electricInputs(/배터리 = \*\*(.+?)\*\*/.exec(kitLine)[1])];
    assertEqual(power811("굳힘 수지").indexOf("고급"), -1, "폴리머는 그냥 작업대");
    ELECTRIC_RECIPES.forEach((id) => {
        const r = sb.RECIPES[id];
        assertTrue(r, "RECIPES 에 " + id);
        assertEqual([r.place, !!r.advWorkbench, r.inputs], ["workbench"].concat(expect[id]), id + " 자리·고급 작업대·재료");
    });
    bold("햇빛 집열판·배터리·전구 얻기").split(" · ").forEach((part) => {
        const m = /^(\S+) (\d+)$/.exec(part);
        const id = ELECTRIC_NAMES[m[1]];
        assertEqual([sb.SHOP_ITEMS[id].price, sb.SHOP_ITEMS[id].count], [Number(m[2]), 1], id + " 잡화점 값");
    });
    power811("판매가").split(" · ").slice(0, 4).forEach((part) => {
        const m = /^(.+?) (\d+)$/.exec(part);
        assertEqual(sb.sellPriceOf(ELECTRIC_NAMES[m[1]]), Number(m[2]), m[1] + " 판매가");
    });
    const p = sb.sellPriceOf;
    assertEqual(p("item.circuit_board"), p("item.copper_ingot") * 2 + p("item.polymer") + 1, "회로판 = 재료 합 + 1");
    assertEqual(p("item.electric_motor"), p("item.copper_ingot") * 2 + p("item.iron_ingot") + p("item.circuit_board") + 1, "모터 = 재료 합 + 1");
    assertTrue(p("item.light_bulb") < sb.SHOP_ITEMS["item.light_bulb"].price, "산 전구를 되팔아 벌지 못한다");
});

test("8.11 전자 재료 — 폴리머는 작업대, 회로판은 고급 작업대가 놓여야 만들어진다", () => {
    const title = makeTitle();
    const tentId = withTent(title, "p");
    title.seed("p", "item.resin", 3, { stack: true });
    title.seed("p", "item.copper_ingot", 2, { stack: true });
    assertEqual(title.call("p", "CraftItem", { recipeId: "item.polymer" }).crafted, "item.polymer", "폴리머");
    assertEqual([title.count("p", "item.resin"), title.count("p", "item.polymer")], [0, 1], "수지 3 → 폴리머 1");
    assertFailsClean(title, "p", "CraftItem", { recipeId: "item.circuit_board" }, "locked", "고급 작업대 없음");
    title.seed("p", "item.workbench_adv", 1);
    title.call("p", "PlaceStructure", { tentId, itemId: "item.workbench_adv", x: 1, y: 1 });
    assertEqual(title.call("p", "CraftItem", { recipeId: "item.circuit_board" }).crafted, "item.circuit_board", "회로판");
    assertEqual([title.count("p", "item.copper_ingot"), title.count("p", "item.polymer"), title.count("p", "item.circuit_board")],
        [0, 0, 1], "구리 주괴 2 + 폴리머 1 → 회로판 1");
    title.setCoins("p", 30);
    assertEqual(title.call("p", "BuyItem", { itemId: "item.solar_kit" }).cost, 30, "햇빛 집열판 30 코인");
    assertEqual([title.count("p", "item.solar_kit"), title.coins("p")], [1, 0], "키트 +1 · 코인 30 → 0");
});

test("8.10 전기 배 추진기 — 배 추진기 + 소형 전동기를 고급 작업대에서 단다(M33-03)", () => {
    const title = makeTitle();
    const sb = title.sandbox;
    const tentId = withTent(title, "p");
    title.seed("p", "item.boat_motor", 1);
    title.seed("p", "item.electric_motor", 1);
    assertFailsClean(title, "p", "CraftItem", { recipeId: "item.boat_motor_electric" }, "locked", "고급 작업대 없음");
    title.seed("p", "item.workbench_adv", 1);
    title.call("p", "PlaceStructure", { tentId, itemId: "item.workbench_adv", x: 1, y: 1 });
    assertEqual(title.call("p", "CraftItem", { recipeId: "item.boat_motor_electric" }).crafted, "item.boat_motor_electric", "전기 배 추진기");
    assertEqual([title.count("p", "item.boat_motor"), title.count("p", "item.electric_motor"), title.count("p", "item.boat_motor_electric")],
        [0, 0, 1], "배 추진기 1 + 소형 전동기 1 → 전기 배 추진기 1");
    assertFailsClean(title, "p", "CraftItem", { recipeId: "item.boat_motor_electric" }, "not_enough_items", "재료가 없으면 거절");
    assertEqual(sb.sellPriceOf("item.boat_motor_electric"),
        sb.sellPriceOf("item.boat_motor") + sb.sellPriceOf("item.electric_motor") + 1, "판매가 = 재료 합 + 1");
    assertTrue(sb.CHEST_LOOT.some((e) => e.itemId === "item.boat_motor_electric"), "상자 2% 길은 그대로");
});

test("8.16 고급 주괴 3종 — 티타늄·은·금 주괴는 팔기만, 레시피 재료가 아니다(감독 보정 10-06, 루프#510 되돌림)", () => {
    const title = makeTitle();
    const sb = title.sandbox;
    const usedBy = (ingot) => Object.keys(sb.RECIPES).filter((id) => sb.RECIPES[id].inputs.some((i) => i.itemId === ingot));
    assertEqual([usedBy("item.titanium_ingot"), usedBy("item.silver_ingot"), usedBy("item.gold_ingot")], [[], [], []], "쓰는 레시피 0");
    ["item.titanium_ingot", "item.silver_ingot", "item.gold_ingot"].forEach((id) => assertTrue(sb.sellPriceOf(id) > 0, id + " 팔 수 있다"));
    assertEqual(sb.sellPriceOf("item.rod_lucky"), 5, "황금 찌 낚싯대 판매가 23 → 5");
    // 실제 제작: 은 주괴 없이 회로판·철판·폴리머만으로 햇빛 집열판가 나온다
    const tentId = withTent(title, "p");
    title.seed("p", "item.workbench_adv", 1);
    title.call("p", "PlaceStructure", { tentId, itemId: "item.workbench_adv", x: 1, y: 1 });
    ["item.circuit_board", "item.sheet_metal", "item.polymer"].forEach((id) => title.seed("p", id, 1));
    assertEqual(title.call("p", "CraftItem", { recipeId: "item.solar_kit" }).crafted, "item.solar_kit", "햇빛 집열판(은 주괴 없이)");
    assertEqual(sb.SHOP_ITEMS["item.solar_kit"].price, 30, "잡화점 30 코인 길은 그대로");
});

test("8.11 전력 — 전등 레시피·판매가가 설계서와 같고, 키트·배터리·전등은 1칸 가구로 놓인다", () => {
    const title = makeTitle();
    const sb = title.sandbox;
    ELECTRIC_NAMES["쇠붙이 부속"] = "item.metal_parts";
    ELECTRIC_NAMES["나무판"] = "item.plank";
    const lamp = sb.RECIPES["item.lamp_electric"];
    assertEqual(power811("전등 레시피").indexOf("고급"), -1, "전등은 그냥 작업대");
    assertEqual([lamp.place, !!lamp.advWorkbench, lamp.inputs],
        ["workbench", false, electricInputs(/\*\*(.+?)\*\*/.exec(power811("전등 레시피"))[1])], "전등 자리·재료 = 표");
    const p = sb.sellPriceOf;
    assertEqual(p("item.lamp_electric"), Number(/전등 (\d+)/.exec(power811("판매가"))[1]), "전등 판매가 = 표");
    assertEqual(p("item.lamp_electric"), p("item.light_bulb") + p("item.metal_parts") + p("item.plank"), "전등 = 재료 판매가 합(+1 없음)");
    const rows = dumpItemRows();
    POWER_PLACEABLES.forEach((id) => {
        assertTrue(sb.PLACEABLE_ITEMS.indexOf(id) !== -1, id + " 가 서버 놓기 목록에");
        const row = rows.find((r) => r.ItemID === id);
        assertTrue(row, id + " 가 DT_ItemTable 에");
        assertEqual([row.ItemType, row.FootprintX, row.FootprintY], ["Placeable", 1, 1], id + " 1칸 가구");
    });

    const tentId = withTent(title, "p");
    title.seed("p", "item.light_bulb", 1, { stack: true });
    title.seed("p", "item.metal_parts", 1, { stack: true });
    title.seed("p", "item.plank", 1, { stack: true });
    assertEqual(title.call("p", "CraftItem", { recipeId: "item.lamp_electric" }).crafted, "item.lamp_electric", "작업대(텐트)에서 전등");
    title.seed("p", "item.solar_kit", 1);
    title.seed("p", "item.battery", 1);
    [["item.solar_kit", 1], ["item.battery", 2], ["item.lamp_electric", 3]].forEach(([id, x]) => {
        title.call("p", "PlaceStructure", { tentId, itemId: id, x, y: 1 });
        assertEqual(title.count("p", id), 0, id + " 놓으면 배낭에서 빠진다");
    });
    assertEqual(title.getData("p", "base")[0].items.map((i) => i.itemId), POWER_PLACEABLES, "땅 목록에 셋");
    assertFailsClean(title, "p", "PlaceStructure", { tentId, itemId: "item.light_bulb", x: 4, y: 1 }, "bad_args", "전구는 재료 — 못 놓는다");
});

test("4.4 계정 삭제 — 확인 문자열이 있어야만 지우고, 지우기 전에 요청 기록을 남긴다", () => {
    const title = makeTitle();
    title.seed("p", "item.wood", 3);
    title.setCoins("p", 40);
    title.setData("p", "ranch", { slots: [1] });
    assertFailsClean(title, "p", "DeleteMyAccount", {}, "bad_args", "확인 문자열 없음");
    assertFailsClean(title, "p", "DeleteMyAccount", { confirm: "delete" }, "bad_args", "확인 문자열 틀림");
    assertEqual(title.calls.events.length, 0, "거절이면 기록도 안 남긴다");
    const res = title.call("p", "DeleteMyAccount", { confirm: "DELETE" });
    assertEqual([res.deleted, res.fullyDeleted], [true, true], "DeletePlayer 성공 = 완전 삭제");
    assertEqual(title.calls.events.map((e) => [e.name, e.body.playFabId]), [["account_delete_requested", "p"]], "삭제 요청 기록");
    assertEqual(title.calls.deletePlayer, ["p"], "DeletePlayer 를 이 계정으로 부른다");
    assertTrue(!title.accounts.p, "계정이 없어졌다");
});

test("4.4 계정 삭제 — DeletePlayer 가 실패하면 인벤토리·재화·데이터를 전부 비우고, 두 번 불러도 같다", () => {
    const title = makeTitle();
    title.faults.deletePlayer = true;
    title.seed("p", "item.wood", 30);
    title.seed("p", "item.worm_bait", 12, { stack: true });
    title.setCoins("p", 40);
    title.account("p").currency.GM = 7;
    for (let i = 0; i < 13; i++) title.setData("p", "key" + i, { n: i });
    title.seed("other", "item.wood", 2);
    const res = title.call("p", "DeleteMyAccount", { confirm: "DELETE" });
    assertEqual([res.deleted, res.fullyDeleted], [true, false], "비우기로 대신한 삭제");
    const acc = title.account("p");
    assertEqual([acc.inventory.length, title.coins("p"), acc.currency.GM, Object.keys(acc.data).length], [0, 0, 0, 0], "다 비었다");
    assertEqual(title.count("other", "item.wood"), 2, "남의 계정은 그대로");
    const again = title.call("p", "DeleteMyAccount", { confirm: "DELETE" });
    assertEqual([again.deleted, again.fullyDeleted], [true, false], "두 번째 호출도 같은 답");
    assertEqual(title.calls.events.length, 2, "호출마다 기록");
});

// ══ 8.11 「집 2층·전력」 집 2층 (M20-03) ═══════════════════════════════════════════
const FLOOR2_NAMES = { "층계 짜맞춤": "item.stair_kit", "나무판": "item.plank", "향나무판": "item.cedar_plank",
    "철판": "item.sheet_metal", "쇠붙이 부속": "item.metal_parts" };

/** "나무판 6 + 쇠붙이 부속 2" → RECIPES 재료 꼴. */
function floor2Inputs(text) {
    return text.split(" + ").map((part) => {
        const m = /^(.+?) (\d+)$/.exec(part.trim());
        if (!m || !FLOOR2_NAMES[m[1]]) throw new Error("재료를 못 읽었다: " + part);
        return { itemId: FLOOR2_NAMES[m[1]], count: Number(m[2]) };
    });
}

/** 집 한 채 — 텐트 + 나무 150·향나무 50 으로 1층. */
function withHouse(title, pid) {
    const tentId = withTent(title, pid);
    title.seed(pid, "item.wood", 150, { stack: true });
    title.seed(pid, "item.cedar", 50, { stack: true });
    assertTrue(title.call(pid, "UpgradeHouse", { tentId }).upgraded, "집 준비");
    return tentId;
}

function seedFloor2Cost(title, pid, cost) {
    cost.forEach((c) => title.seed(pid, c.itemId, c.count, { stack: true }));
}

test("8.11 집 2층 — 재료·계단 부품 레시피·가구 한도가 설계서 표와 같다", () => {
    const sb = makeTitle().sandbox;
    const bold = (label) => /\*\*(.+?)\*\*/.exec(power811(label))[1];
    assertEqual(sb.FLOOR2_COST, floor2Inputs(bold("2층 재료")), "2층 재료");
    const r = sb.RECIPES["item.stair_kit"];
    assertEqual([r.place, !!r.advWorkbench, r.inputs], ["workbench", true, floor2Inputs(bold("층계 짜맞춤"))], "계단 부품 레시피");
    assertTrue(power811("층계 짜맞춤").indexOf("고급 작업대") !== -1, "설계서도 고급 작업대");
    const limits = /(\d+) → (\d+)/.exec(power811("2층 효과"));
    assertEqual([sb.FURNITURE_MAX, sb.FURNITURE_MAX_FLOOR2], [Number(limits[1]), Number(limits[2])], "가구 한도 40 → 60");
    const row = dumpItemRows().find((x) => x.ItemID === "item.stair_kit");
    assertTrue(row, "item.stair_kit 이 DT_ItemTable 에 있다");
    assertEqual([row.DisplayName, row.ItemType], ["층계 짜맞춤", "Material"], "계단 부품 이름·종류");
    assertEqual(sb.sellPriceOf("item.stair_kit"), sb.sellPriceOf("item.metal_parts") * 2 + 1, "계단 부품 = 쇠붙이 부속 2개 값 + 1");
});

test("8.11 집 2층 — 텐트는 거절, 재료 하나라도 모자라면 그대로, 다 있으면 즉시 2층·재료 차감", () => {
    const title = makeTitle();
    const sb = title.sandbox;
    const cost = sb.FLOOR2_COST.map((c) => ({ itemId: c.itemId, count: c.count }));
    const tentOnly = withTent(title, "t");
    seedFloor2Cost(title, "t", cost);
    assertFailsClean(title, "t", "UpgradeHouse", { tentId: tentOnly, level: 2 }, "not_house", "텐트엔 2층 없음");
    assertFailsClean(title, "t", "UpgradeHouse", { tentId: tentOnly, level: 3 }, "bad_args", "3층 없음");

    cost.forEach((short, i) => {
        const t = makeTitle();
        const house = withHouse(t, "p");
        seedFloor2Cost(t, "p", cost.map((c, j) => ({ itemId: c.itemId, count: j === i ? c.count - 1 : c.count })).filter((c) => c.count > 0));
        const r = assertFailsClean(t, "p", "UpgradeHouse", { tentId: house, level: 2 }, "not_enough_items", short.itemId + " 하나 모자람");
        assertEqual([r.itemId, r.need], [short.itemId, short.count], short.itemId + " 모자란 줄");
    });

    const tentId = withHouse(title, "p");
    seedFloor2Cost(title, "p", cost);
    const upkeep = title.getData("p", "base")[0].upkeepUntilUtc;
    const r = title.call("p", "UpgradeHouse", { tentId, level: 2 });
    assertEqual([r.upgraded, r.level, r.furnitureMax, r.upkeepUntilUtc], [true, 2, 60, upkeep], "즉시 2층 · 한도 60 · 유지 그대로");
    cost.forEach((c) => assertEqual(title.count("p", c.itemId), 0, c.itemId + " 다 빠짐"));
    assertEqual(title.getData("p", "base")[0].floors, 2, "base 에 floors 2");
    seedFloor2Cost(title, "p", cost);
    assertFailsClean(title, "p", "UpgradeHouse", { tentId, level: 2 }, "already", "두 번 2층");
});

test("8.11 집 2층 — 가구 한도가 40 에서 60 으로", () => {
    const title = makeTitle();
    const tentId = withHouse(title, "p");
    const base = title.getData("p", "base");
    for (let i = 0; i < 40; i++) base[0].items.push({ structureId: "s" + i, itemId: "item.fence_wood", x: -8 + (i % 16), y: -8 + Math.floor(i / 16), rot: 0 });
    title.setData("p", "base", base);
    title.seed("p", "item.fence_wood", 1);
    assertFailsClean(title, "p", "PlaceStructure", { tentId, itemId: "item.fence_wood", x: 5, y: 5 }, "limit", "1층 41번째");
    seedFloor2Cost(title, "p", title.sandbox.FLOOR2_COST);
    title.call("p", "UpgradeHouse", { tentId, level: 2 });
    assertTrue(title.call("p", "PlaceStructure", { tentId, itemId: "item.fence_wood", x: 5, y: 5 }).structureId, "2층 41번째");
    const full = title.getData("p", "base");
    for (let i = 41; i < 60; i++) full[0].items.push({ structureId: "s" + i, itemId: "item.fence_wood", x: -8 + (i % 16), y: -8 + Math.floor(i / 16), rot: 0 });
    title.setData("p", "base", full);
    title.seed("p", "item.fence_wood", 1);
    assertFailsClean(title, "p", "PlaceStructure", { tentId, itemId: "item.fence_wood", x: 6, y: 6 }, "limit", "2층 61번째");
});

test("PlaceStructure 계단 — 2층 집 안 6 자리만, 집 하나 1개, 2층 가구는 계단 구멍 빼고 6칸, 2층 가구 있으면 계단 못 치움 (M108-06a)", () => {
    const title = makeTitle();
    const sb = title.sandbox;
    // 텐트·1층 집엔 not_floor2
    const tentOnly = withTent(title, "t");
    title.seed("t", "item.stairs_wood", 1);
    assertFailsClean(title, "t", "PlaceStructure", { tentId: tentOnly, itemId: "item.stairs_wood", x: 0, y: 0 }, "not_floor2", "텐트에 계단");
    const t1 = makeTitle();
    const house1 = withHouse(t1, "h");
    t1.seed("h", "item.stairs_wood", 1);
    assertFailsClean(t1, "h", "PlaceStructure", { tentId: house1, itemId: "item.stairs_wood", x: 0, y: 0 }, "not_floor2", "1층 집에 계단");

    // 집 칸 (ox, oy) 0~2 · 문 = (0, 1). 한 줄 놓기 자리 = X 로 긴 줄 셋(돌림 0·180, 앵커 (0, oy)) + Y 로 긴 줄 셋(돌림 90·270, 앵커 (ox, 0)) — 발자국 3×1 긴 변 X
    const floor2 = (pid) => {
        const t = makeTitle();
        const id = withHouse(t, pid);
        seedFloor2Cost(t, pid, t.sandbox.FLOOR2_COST);
        assertTrue(t.call(pid, "UpgradeHouse", { tentId: id, level: 2 }).upgraded, "2층 준비");
        t.seed(pid, "item.stairs_wood", 2);
        return [t, id];
    };
    let open = 0;
    [90, 270, 0, 180].forEach((rot) => {
        const alongX = rot === 0 || rot === 180;
        [0, 1, 2].forEach((line) => {
            const [t, id] = floor2("s");
            const at = alongX ? { x: 0, y: line } : { x: line, y: 0 };
            const ok = alongX ? line !== 1 : line === 2;
            const what = "돌림 " + rot + " 줄 " + line;
            if (ok) {
                assertTrue(t.call("s", "PlaceStructure", Object.assign({ tentId: id, itemId: "item.stairs_wood", rot }, at)).structureId, what + " 놓임");
                open++;
            } else {
                assertFailsClean(t, "s", "PlaceStructure", Object.assign({ tentId: id, itemId: "item.stairs_wood", rot }, at), "blocks_path", what + " 문 길·방 가름");
            }
        });
    });
    assertEqual(open, 6, "놓을 자리 6");

    const [t, id] = floor2("p");
    const put = (args) => t.call("p", "PlaceStructure", Object.assign({ tentId: id }, args));
    const fails = (args, code, what) => assertFailsClean(t, "p", "PlaceStructure", Object.assign({ tentId: id }, args), code, what);
    fails({ itemId: "item.stairs_wood", x: 1, y: 0 }, "out_of_house", "X 로 집 밖 한 칸");
    fails({ itemId: "item.stairs_wood", x: 2, y: -1, rot: 90 }, "out_of_house", "Y 로 집 밖 한 칸");
    fails({ itemId: "item.stairs_wood", x: 0, y: 0, rot: 45 }, "bad_args", "45° 돌림");
    fails({ itemId: "item.stairs_wood", x: 0, y: 0, hx: 1 }, "bad_args", "반 칸");
    fails({ itemId: "item.stairs_wood", x: 0, y: 0, floor: 2 }, "bad_args", "2층에 계단");
    t.seed("p", "item.chair_wood", 4);
    t.seed("p", "item.doormat", 1);
    t.seed("p", "item.mug", 1);
    t.seed("p", "item.picture_frame", 1);
    fails({ itemId: "item.chair_wood", x: 0, y: 0, floor: 2 }, "no_stairs", "계단 없이 2층 가구");
    assertTrue(put({ itemId: "item.chair_wood", x: 1, y: 2 }).structureId, "1층 의자 (1, 2)");
    fails({ itemId: "item.stairs_wood", x: 0, y: 2, rot: 180 }, "occupied", "의자 칸을 덮는 계단");
    assertTrue(put({ itemId: "item.doormat", x: 0, y: 0 }).structureId, "1층 깔개 (0, 0)");
    const stair = put({ itemId: "item.stairs_wood", x: 0, y: 0, rot: 180 });
    assertTrue(stair.structureId, "깔개 위로 계단 (0~2, 0)");
    fails({ itemId: "item.stairs_wood", x: 2, y: 0, rot: 90 }, "stair_exists", "둘째 계단");
    fails({ itemId: "item.chair_wood", x: 1, y: 0 }, "occupied", "1층 계단 칸에 의자");
    fails({ itemId: "item.chair_wood", x: 2, y: 0, floor: 2 }, "stair_hole", "2층 계단 구멍 칸");
    fails({ itemId: "item.chair_wood", x: 3, y: 1, floor: 2 }, "out_of_house", "2층 집 밖");
    fails({ itemId: "item.chair_wood", x: 0, y: 1, floor: 3 }, "bad_args", "3층");
    fails({ itemId: "item.picture_frame", x: 0, y: 1, wall: { side: 3, h: 150 }, floor: 2 }, "bad_args", "2층 벽 장식");
    const up = put({ itemId: "item.chair_wood", x: 1, y: 2, floor: 2 });
    assertTrue(up.structureId, "1층 의자 바로 위 2층 의자 — 층이 다르면 안 겹침");
    fails({ itemId: "item.chair_wood", x: 1, y: 2, floor: 2 }, "occupied", "2층 같은 칸 둘");
    let tent = t.getData("p", "base")[0];
    const upRow = tent.items.find((it) => it.structureId === up.structureId);
    assertEqual([upRow.floor, sb.furnitureLoad(tent)], [2, 4], "2층 줄 floor 2 · 한도는 층 합(의자 2 + 깔개 + 계단)");
    assertEqual(tent.items.find((it) => it.structureId === stair.structureId).floor, undefined, "1층 줄엔 floor 없음(옛 줄과 같은 꼴)");
    assertFailsClean(t, "p", "RemoveStructure", { tentId: id, structureId: stair.structureId }, "not_empty", "2층 가구 있는 계단");
    assertEqual(t.call("p", "RemoveStructure", { tentId: id, structureId: up.structureId }).removed, "item.chair_wood", "2층 의자 치움");
    assertEqual(t.call("p", "RemoveStructure", { tentId: id, structureId: stair.structureId }).removed, "item.stairs_wood", "그 뒤 계단 치움");
    tent = t.getData("p", "base")[0];
    assertEqual(tent.items.length, 2, "1층 의자·깔개만 남음");
});

// M108-07a 연료 발전기 — 기대값은 DESIGN 8.11 「연료 발전기 숫자」 줄에서 읽는다(서버 상수와 같이 틀리면 못 잡으니까).
test("FeedGenerator — 나무 10분·석탄 30분, 12시간에서 잘림, 가득·모자람·발전기 아님 거절, 끝 시각은 서버 시계 (M108-07a)", () => {
    const LABEL = "🟡 연료 발전기 숫자 (M108-07 명세 `docs/specs/fuel-generator.md`, 루프#1402)";
    const woodMin = designNumber(LABEL, /나무 1 = \*\*(\d+)분\*\*/);
    const coalMin = designNumber(LABEL, /석탄 덩어리 1 = \*\*(\d+)분\*\*/);
    const maxHours = designNumber(LABEL, /최대 \*\*(\d+)시간\*\*/);
    const title = makeTitle();
    const sb = title.sandbox;
    assertEqual(sb.GENERATOR_FUEL_MINUTES, { "item.wood": woodMin, "item.coal_chunk": coalMin }, "연료 분 = 설계서");
    assertEqual(sb.GENERATOR_FUEL_MAX_HOURS, maxHours, "최대 시간 = 설계서");
    const MIN = 60 * 1000;
    const tentId = withTent(title, "p");
    title.seed("p", "item.generator", 1);
    title.seed("p", "item.chair_wood", 1);
    const gen = title.call("p", "PlaceStructure", { tentId, itemId: "item.generator", x: 1, y: 1 });
    assertTrue(gen.structureId, "발전기는 텐트 땅에도 놓인다(바닥 가구 그대로)");
    const chair = title.call("p", "PlaceStructure", { tentId, itemId: "item.chair_wood", x: 2, y: 1 });
    title.seed("p", "item.wood", 200, { stack: true });
    title.seed("p", "item.coal_chunk", 40, { stack: true });
    title.seed("p", "item.iron_ingot", 5, { stack: true });

    assertFailsClean(title, "p", "FeedGenerator", { tentId, structureId: gen.structureId, fuel: "item.iron_ingot", count: 1 }, "bad_args", "연료 아님(쇠)");
    assertFailsClean(title, "p", "FeedGenerator", { tentId, structureId: gen.structureId, fuel: "item.coal_ingot", count: 1 }, "bad_args", "코크스는 연료 아님");
    assertFailsClean(title, "p", "FeedGenerator", { tentId, structureId: gen.structureId, fuel: "item.wood", count: 0 }, "bad_args", "0개");
    assertFailsClean(title, "p", "FeedGenerator", { tentId, structureId: chair.structureId, fuel: "item.wood", count: 1 }, "not_generator", "의자");
    assertFailsClean(title, "p", "FeedGenerator", { tentId, structureId: "nope", fuel: "item.wood", count: 1 }, "not_found", "없는 가구");
    assertFailsClean(title, "p", "FeedGenerator", { tentId: "nope", structureId: gen.structureId, fuel: "item.wood", count: 1 }, "not_found", "없는 텐트");
    assertFailsClean(title, "q", "FeedGenerator", { tentId, structureId: gen.structureId, fuel: "item.wood", count: 1 }, "not_found", "남의 발전기", ["p"]);

    // 나무 3 → 30분, 응답 끝 − 서버 지금 = 30분
    const a = title.call("p", "FeedGenerator", { tentId, structureId: gen.structureId, fuel: "item.wood", count: 3 });
    assertEqual(a.used, 3, "나무 3 씀");
    assertEqual(Date.parse(a.fuelUntilUtc) - Date.parse(a.nowUtc), 3 * woodMin * MIN, "나무 3 = 30분");
    assertEqual(title.count("p", "item.wood"), 197, "나무 3 빠짐");
    const saved = title.getData("p", "base")[0].items.find((s) => s.structureId === gen.structureId);
    assertEqual(saved.fuelUntilUtc, a.fuelUntilUtc, "놓인 줄에 fuelUntilUtc 저장");

    // 석탄 40 → 남은 칸만큼만(12시간 − 30분 = 690분 → 석탄 23 개, 넘치는 17 은 그대로)
    const roomCoal = Math.floor((maxHours * 60 - 3 * woodMin) / coalMin);
    const b = title.call("p", "FeedGenerator", { tentId, structureId: gen.structureId, fuel: "item.coal_chunk", count: 40 });
    assertEqual(b.used, roomCoal, "석탄은 최대까지만 " + roomCoal + "개");
    assertEqual(title.count("p", "item.coal_chunk"), 40 - roomCoal, "넘치는 석탄은 안 빠짐");
    const leftMs = Date.parse(b.fuelUntilUtc) - Date.parse(b.nowUtc);
    assertTrue(leftMs <= maxHours * 60 * MIN && leftMs > maxHours * 60 * MIN - coalMin * MIN, "12시간 넘지 않고 석탄 하나 몫 안쪽까지 참");
    // 나무 1(10분)도 남은 칸(0~29분) 안에 들어가면 들어간다, 안 들어가면 가득
    const woodRoom = Math.floor((maxHours * 60 * MIN - leftMs) / (woodMin * MIN));
    if (woodRoom > 0) title.call("p", "FeedGenerator", { tentId, structureId: gen.structureId, fuel: "item.wood", count: woodRoom });
    assertFailsClean(title, "p", "FeedGenerator", { tentId, structureId: gen.structureId, fuel: "item.wood", count: 1 }, "full", "가득");

    // 다 타면 다시 0 부터 — 옛 끝 시각이 지난 만큼 빼지 않는다
    title.age("p", "base", (maxHours + 1) * 3600 * 1000); // 텐트 기한(24시간) 안
    const c = title.call("p", "FeedGenerator", { tentId, structureId: gen.structureId, fuel: "item.wood", count: 1 });
    assertEqual(Date.parse(c.fuelUntilUtc) - Date.parse(c.nowUtc), woodMin * MIN, "다 탄 뒤 나무 1 = 10분");
    const wood = title.count("p", "item.wood");
    // 남은 석탄 17 < 석탄 칸(710분 → 23) — 넣을 만큼이 가방보다 많으면 하나도 안 뺀다
    assertFailsClean(title, "p", "FeedGenerator", { tentId, structureId: gen.structureId, fuel: "item.coal_chunk", count: 40 }, "not_enough_items", "석탄 모자람");

    // GetBaseState 에 서버 시계, 치우면 남은 연료와 함께 사라진다(가방엔 발전기만)
    assertTrue(!isNaN(Date.parse(title.call("p", "GetBaseState", {}).nowUtc)), "GetBaseState.nowUtc");
    assertEqual(title.call("p", "RemoveStructure", { tentId, structureId: gen.structureId }).removed, "item.generator", "발전기 치움");
    assertEqual(title.count("p", "item.generator"), 1, "발전기 돌아옴");
    assertEqual(title.count("p", "item.wood"), wood, "연료는 안 돌아옴");
    assertTrue(sb.RECIPES["item.generator"].advWorkbench, "고급 작업대 레시피");
    assertTrue(sb.sellPriceOf("item.generator") > 1, "판매가 = 재료 합 + 1");
});

// M108-08a 전기 가구 넷 — 놓기 길은 지금 셋 그대로(냉장고·선풍기 바닥, 라디오 받침 위, 네온 간판 벽). 레시피 기대값은 DESIGN 8.11 「🟡 전기 가구」 줄에서 읽는다.
test("PlaceStructure 전기 가구 넷 — 냉장고·선풍기 바닥, 라디오는 받침 위만, 네온 간판은 집 벽만, 레시피 = 설계서 (M108-08a)", () => {
    const LABEL = "🟡 전기 가구 (M108-08 명세 `docs/specs/electric-furniture.md`, 루프#1409)";
    const row = designLines.find((l) => l.startsWith("| " + LABEL + " |"));
    assertTrue(row, "DESIGN 8.11 「" + LABEL + "」 줄");
    const names = { "소형 전동기": "item.electric_motor", "철판": "item.sheet_metal", "굳힘 수지": "item.polymer", "쇠붙이 부속": "item.metal_parts",
        "회로판": "item.circuit_board", "나무판": "item.plank", "전구": "item.light_bulb" };
    const recipeText = row.split("레시피 고급 작업대 — ")[1].split(" · 판매가")[0];
    const expect = {};
    recipeText.split(" · ").forEach((part) => {
        const m = part.match(/^(냉장고|선풍기|라디오|네온 간판) (.+)$/);
        assertTrue(m, "레시피 조각 읽기: " + part);
        const id = { "냉장고": "item.fridge", "선풍기": "item.fan_stand", "라디오": "item.radio", "네온 간판": "item.neon_sign" }[m[1]];
        expect[id] = m[2].split(" + ").map((ing) => {
            const im = ing.match(/^(.+) (\d+)$/);
            assertTrue(im && names[im[1]], "재료 읽기: " + ing);
            return names[im[1]] + "x" + im[2];
        }).sort();
    });
    const sb = makeTitle().sandbox;
    const ids = ["item.fridge", "item.fan_stand", "item.radio", "item.neon_sign"];
    assertEqual(Object.keys(expect).sort(), ids.slice().sort(), "설계서 레시피 넷");
    ids.forEach((id) => {
        assertTrue(sb.RECIPES[id] && sb.RECIPES[id].advWorkbench, id + " 고급 작업대 레시피");
        assertEqual(sb.RECIPES[id].inputs.map((i) => i.itemId + "x" + i.count).sort(), expect[id], id + " 레시피 = 설계서");
        assertTrue(sb.sellPriceOf(id) > 1, id + " 판매가 = 재료 합 + 1");
        assertTrue(sb.PLACEABLE_ITEMS.indexOf(id) !== -1, id + " 놓기 목록");
        assertTrue(sb.TESTER_KIT.some((k) => k.itemId === id), id + " 테스터 키트");
    });

    const title = makeTitle();
    const tentId = withTent(title, "e");
    ids.forEach((id) => title.seed("e", id, 2));
    title.seed("e", "item.table_wood", 1);
    assertTrue(title.call("e", "PlaceStructure", { tentId, itemId: "item.fridge", x: 3, y: 0 }).structureId, "냉장고 바닥");
    assertFailsClean(title, "e", "PlaceStructure", { tentId, itemId: "item.fan_stand", x: 3, y: 0 }, "occupied", "냉장고 칸에 선풍기");
    assertTrue(title.call("e", "PlaceStructure", { tentId, itemId: "item.fan_stand", x: 4, y: 0, rot: 45 }).structureId, "선풍기 45° 돌림");
    const table = title.call("e", "PlaceStructure", { tentId, itemId: "item.table_wood", x: 3, y: 2 }).structureId;
    assertFailsClean(title, "e", "PlaceStructure", { tentId, itemId: "item.radio", x: 5, y: 0 }, "bad_args", "라디오는 바닥에 못 놓음");
    assertTrue(title.call("e", "PlaceStructure", { tentId, itemId: "item.radio", x: 0, y: 0, top: { host: table, slot: 1 } }).structureId, "탁자 위 라디오");
    const hang = (x, y, side, h) => ({ tentId, itemId: "item.neon_sign", x, y, wall: { side, h } });
    assertFailsClean(title, "e", "PlaceStructure", hang(1, 0, 2, 160), "not_house", "텐트 상태에선 간판 못 검");
    assertFailsClean(title, "e", "PlaceStructure", { tentId, itemId: "item.neon_sign", x: 5, y: 0 }, "bad_args", "간판은 바닥에 못 놓음");
    title.seed("e", "item.wood", 150, { stack: true });
    title.seed("e", "item.cedar", 50, { stack: true });
    title.call("e", "UpgradeHouse", { tentId, color: 0 });
    assertTrue(title.call("e", "PlaceStructure", hang(1, 0, 2, 160)).structureId, "집 남쪽 벽에 간판");
    assertFailsClean(title, "e", "PlaceStructure", hang(1, 1, 2, 160), "not_wall", "가운데 칸은 벽이 아님");
    const tent = title.getData("e", "base").find((tt) => tt.tentId === tentId);
    assertEqual(title.sandbox.furnitureLoad(tent), 3 + 0.5 + 0.5, "냉장고·선풍기·탁자 1개씩, 라디오·간판 0.5개");
});

// ── M21-06b 같이 하기 생산 −10% ─────────────────────────────────────────────────
function coopPig(title, pid) {
    title.seed(pid, "item.pet_pig", 1);
    title.call(pid, "RegisterPet", { animalId: "animal.pig", slot: 0 });
}
function pigSince(title, pid) { return Date.parse(title.getData(pid, "ranch")[0].occupiedSinceUtc); }

test("M21-06b 같이 하기 — DESIGN 3.2 값과 같다 (2인 · ×0.9 · 120초)", () => {
    const title = makeTitle();
    const prod = designRow("같이 하기 생산 (M21-06b)")[1];
    assertEqual(title.sandbox.COOP_MIN_PLAYERS, parseInt(prod.match(/\*\*(\d+)\*\*인/)[1], 10), "인원 문턱");
    assertEqual(title.sandbox.COOP_CYCLE_FACTOR, parseFloat(prod.match(/×([\d.]+)/)[1]), "주기 배율");
    const fresh = designRow("같이 하기 보고 유효 (M21-06b)")[1];
    assertEqual(title.sandbox.COOP_FRESH_SECONDS, parseInt(fresh.match(/\*\*(\d+)초\*\*/)[1], 10), "보고 유효 초");
});

test("M21-06b 같이 하기 — 둘 다 같은 호스트로 보고하면 흐른 시간의 1/9 만큼 칸 시계를 당긴다", () => {
    const title = makeTitle();
    coopPig(title, "a");
    coopPig(title, "b");
    const first = title.call("a", "ReportCoop", { hostId: "a", peers: ["b"] });
    assertEqual([first.coop, first.players, first.creditedMs], [false, 1, 0], "b 가 아직 안 보냈으면 혼자");
    const second = title.call("b", "ReportCoop", { hostId: "a", peers: ["a"] });
    assertEqual([second.coop, second.players, second.creditedMs], [true, 2, 0], "b 는 첫 보고라 쳐 줄 시간 0");
    title.age("a", "coop", 60 * 1000);
    title.age("b", "coop", 60 * 1000);
    const before = pigSince(title, "a");
    const got = title.call("a", "ReportCoop", { hostId: "a", peers: ["b"] });
    assertTrue(got.coop && got.players === 2, "60초 뒤 a 는 둘");
    assertTrue(got.creditedMs >= 60000 && got.creditedMs < 61000, "흐른 60초를 친다 — " + got.creditedMs);
    assertEqual(got.bonusMs, Math.round(got.creditedMs * (1 / 0.9 - 1)), "당김 = 흐른 시간 × (1/0.9 − 1)");
    assertEqual(before - pigSince(title, "a"), got.bonusMs, "a 의 돼지 칸 시계가 그만큼 앞선다");
    // 27분(= 30 × 0.9) 함께면 30분 몫이 찬다: 27분 + 27분/9 = 30분.
    assertEqual(Math.round(27 * 60 * 1000 * (1 / title.sandbox.COOP_CYCLE_FACTOR - 1)), 3 * 60 * 1000, "27분 함께 = 3분 당김");
});

test("M21-06b 같이 하기 — 혼자·남의 방·낡은 보고·나 자신·같은 사람 두 번은 세지 않는다", () => {
    const title = makeTitle();
    coopPig(title, "a");
    title.call("a", "ReportCoop", { hostId: "a", peers: [] });
    title.age("a", "coop", 60 * 1000);
    let before = pigSince(title, "a");
    let got = title.call("a", "ReportCoop", { hostId: "a", peers: [] });
    assertEqual([got.coop, got.bonusMs], [false, 0], "혼자");
    assertEqual(pigSince(title, "a"), before, "혼자면 시계 그대로");

    title.call("b", "ReportCoop", { hostId: "x", peers: ["a"] });
    title.age("a", "coop", 60 * 1000);
    got = title.call("a", "ReportCoop", { hostId: "a", peers: ["b"] });
    assertEqual(got.players, 1, "b 는 다른 호스트(x)라 안 센다");

    title.call("c", "ReportCoop", { hostId: "a", peers: ["a"] });
    title.age("c", "coop", (title.sandbox.COOP_FRESH_SECONDS + 1) * 1000);
    got = title.call("a", "ReportCoop", { hostId: "a", peers: ["c"] });
    assertEqual(got.players, 1, "121초 묵은 c 는 방을 나간 것으로 본다");

    got = title.call("a", "ReportCoop", { hostId: "a", peers: ["a", "a"] });
    assertEqual(got.players, 1, "peers 에 나를 넣어도 둘이 안 된다");
    title.call("d", "ReportCoop", { hostId: "a", peers: ["a"] });
    got = title.call("a", "ReportCoop", { hostId: "a", peers: ["d", "d", "d"] });
    assertEqual(got.players, 2, "같은 사람을 여러 번 넣어도 한 명");
});

test("M21-06b 같이 하기 — 연타는 흐른 시간만, 오래 끊긴 사이는 120초까지만, 방을 바꾸면 0", () => {
    const title = makeTitle();
    coopPig(title, "a");
    title.call("b", "ReportCoop", { hostId: "a", peers: ["a"] });
    title.call("a", "ReportCoop", { hostId: "a", peers: ["b"] });
    const spam = title.call("a", "ReportCoop", { hostId: "a", peers: ["b"] });
    assertTrue(spam.coop && spam.creditedMs < 1000, "바로 다시 보내면 쳐 줄 시간이 거의 없다 — " + spam.creditedMs);
    title.age("a", "coop", 10 * 60 * 1000);
    title.call("b", "ReportCoop", { hostId: "a", peers: ["a"] });
    const gap = title.call("a", "ReportCoop", { hostId: "a", peers: ["b"] });
    assertEqual(gap.creditedMs, title.sandbox.COOP_FRESH_SECONDS * 1000, "10분 끊긴 사이는 120초만");
    title.age("a", "coop", 60 * 1000);
    title.call("c", "ReportCoop", { hostId: "c", peers: ["a"] });
    const moved = title.call("a", "ReportCoop", { hostId: "c", peers: ["c"] });
    assertEqual([moved.coop, moved.creditedMs], [true, 0], "다른 방으로 옮긴 첫 보고는 0");
});

test("M21-06b 같이 하기 — 가득 찬 칸·잠긴 목장은 당기지 않는다 · 인자 오류는 아무것도 안 바꾼다", () => {
    const H = 3600 * 1000;
    const title = makeTitle();
    const now = Date.now();
    const full = new Date(now - (title.sandbox.HARVEST_CAP_UNITS + 1) * 30 * 60 * 1000).toISOString();
    const fresh = new Date(now).toISOString();
    title.setData("a", "ranchMeta", { upkeepUntilUtc: new Date(now + 10 * H).toISOString(), fame: 0, slotCap: 5 });
    title.setData("a", "ranch", [
        { slot: 0, animalId: "animal.pig", occupiedSinceUtc: full, stock: 0 },
        { slot: 1, animalId: "animal.cow", occupiedSinceUtc: fresh, stock: 0 }, null, null, null]);
    title.call("b", "ReportCoop", { hostId: "a", peers: ["a"] });
    title.call("a", "ReportCoop", { hostId: "a", peers: ["b"] });
    title.age("a", "coop", 60 * 1000);
    const got = title.call("a", "ReportCoop", { hostId: "a", peers: ["b"] });
    assertTrue(got.bonusMs > 0, "둘이라 당김이 있다");
    const ranch = title.getData("a", "ranch");
    assertEqual(ranch[0].occupiedSinceUtc, full, "가득 찬 칸은 그대로");
    assertEqual(Date.parse(fresh) - Date.parse(ranch[1].occupiedSinceUtc), got.bonusMs, "덜 찬 칸만 당긴다");

    title.setData("a", "ranchMeta", { upkeepUntilUtc: new Date(now - H).toISOString(), fame: 0, slotCap: 5 });
    const lockedBefore = JSON.stringify(title.getData("a", "ranch"));
    title.age("a", "coop", 60 * 1000);
    title.call("b", "ReportCoop", { hostId: "a", peers: ["a"] });
    assertTrue(title.call("a", "ReportCoop", { hostId: "a", peers: ["b"] }).bonusMs > 0, "잠겨도 같이 한 것은 센다");
    assertEqual(JSON.stringify(title.getData("a", "ranch")), lockedBefore, "잠긴 목장은 시계 그대로");

    assertFailsClean(title, "a", "ReportCoop", { hostId: "a", peers: ["1", "2", "3", "4", "5", "6", "7", "8"] }, "bad_args", "peers 8명");
    assertFailsClean(title, "a", "ReportCoop", { hostId: "", peers: [] }, "bad_args", "빈 hostId");
    assertFailsClean(title, "a", "ReportCoop", { hostId: "a", peers: [3] }, "bad_args", "숫자 peer");
});

// ══ 10.8 꾸미기 상자·열쇠 (상점 개편 B) ═══════════════════════════════════════
function designPct(label) {
    return Number(/\*\*(\d+(?:\.\d+)?)%\*\*/.exec(designRow(label)[1])[1]);
}

test("꾸미기 상자 10.8 — 드롭률·열쇠 값·풀이 DESIGN 과 같고 생성기 DT_CrateTable 과 같다", () => {
    const title = makeTitle();
    const sb = title.sandbox;
    assertEqual(title.crateDropChance, designPct("상자 드롭률") / 100, "드롭률");
    assertEqual(sb.SHOP_ITEMS[sb.CRATE_KEY].price, Number(/\*\*(\d+)\*\* 코인/.exec(designRow("열쇠 코인 값")[1])[1]), "열쇠 코인 값");
    assertEqual(sb.CRATE_KEY_GEM_PRICE, Number(/\*\*(\d+)\*\* 젬/.exec(designRow("열쇠 젬 값")[1])[1]), "열쇠 젬 값");
    const pool = sb.CRATE_POOLS[sb.CRATE_CURRENT].items;
    const pairs = [];
    for (const m of designRow("품목별 확률 (가을)")[1].matchAll(/`(item\.[a-z0-9_]+)` \*\*([\d.]+)%\*\*/g)) pairs.push([m[1], Number(m[2])]);
    assertEqual(pool.map((e) => [e.itemId, Math.round(e.chance * 1e6) / 1e4]), pairs, "품목별 % (설계서 = 소수 넷째 자리)");
    assertEqual(Math.round(pool.reduce((n, e) => n + e.chance, 0) * 1000), 1000, "합 100%");
    assertEqual(JSON.stringify(pool.map((e) => e.itemId).sort()), JSON.stringify(Object.keys(sb.WARDROBE_SHOP_POOL).sort()), "상자 풀 = 옷장 상점 풀 91종");
    const grades = {};
    pool.forEach((e) => { grades[e.grade] = (grades[e.grade] || 0) + e.chance * 100; });
    Object.keys(grades).forEach((g) => { grades[g] = Math.round(grades[g] * 1e6) / 1e6; });
    const want = {};
    for (const m of designRow("등급 확률")[1].matchAll(/(커먼|레어|에픽) \*\*(\d+)%\*\*/g)) want[{ 커먼: "common", 레어: "rare", 에픽: "epic" }[m[1]]] = Number(m[2]);
    assertEqual(grades, want, "등급별 %");
    const rows = dumpGeneratorRows("CRATE_ROWS");
    assertEqual(rows.map((r) => [r.CrateItemID, r.ItemID, r.Grade.toLowerCase(), r.KeyGemPrice]),
        pool.map((e) => [sb.CRATE_CURRENT, e.itemId, e.grade, sb.CRATE_KEY_GEM_PRICE]), "생성기 ↔ 서버 풀");
    rows.forEach((r, i) => assertTrue(Math.abs(r.ChancePct - pool[i].chance * 100) < 1e-9, r.ItemID + " 생성기 % = 서버 %"));
    const tableIds = new Set(dumpItemRows().map((r) => r.ItemID));
    [sb.CRATE_CURRENT, sb.CRATE_KEY].concat(pool.map((e) => e.itemId)).forEach((id) => assertTrue(tableIds.has(id), id + " 가 DT_ItemTable 에"));
    pool.forEach((e) => assertEqual(sb.HAT_EFFECTS[e.itemId] ? Object.keys(sb.HAT_EFFECTS[e.itemId]).length : 0, 0, e.itemId + " 는 효과 없음(10.3)"));
});

test("꾸미기 상자 — OpenCrate: 열쇠 없으면 그대로 거절, 있으면 상자·열쇠 1개씩 빼고 풀에서 하나", () => {
    const title = makeTitle();
    const sb = title.sandbox;
    assertFailsClean(title, "p", "OpenCrate", { crateId: "item.ruby" }, "bad_args", "상자 아닌 것");
    assertFailsClean(title, "p", "OpenCrate", { crateId: sb.CRATE_CURRENT }, "not_enough_items", "상자 없음");
    title.seed("p", sb.CRATE_CURRENT, 1);
    assertFailsClean(title, "p", "OpenCrate", { crateId: sb.CRATE_CURRENT }, "no_key", "열쇠 없음");
    title.seed("p", sb.CRATE_KEY, 2);
    setRandom(title, 0.999); // 마지막 5% 칸 = 에픽, 맨 끝 줄
    const last = sb.CRATE_POOLS[sb.CRATE_CURRENT].items.slice(-1)[0];
    const r = title.call("p", "OpenCrate", { crateId: sb.CRATE_CURRENT });
    assertEqual([r.itemId, r.grade], [last.itemId, "epic"], "0.999 = 마지막 에픽 줄");
    assertEqual([title.count("p", sb.CRATE_CURRENT), title.count("p", sb.CRATE_KEY), title.count("p", last.itemId)], [0, 1, 1], "상자·열쇠 1개씩 빠지고 꾸미기 +1");
    title.seed("p", sb.CRATE_CURRENT, 1);
    setRandom(title, 0);
    assertEqual(title.call("p", "OpenCrate", { crateId: sb.CRATE_CURRENT }).itemId, sb.CRATE_POOLS[sb.CRATE_CURRENT].items[0].itemId, "0 = 첫 줄");
    const counts = {};
    seedRandom(title, 108);
    for (let i = 0; i < 4000; i++) {
        title.seed("p", sb.CRATE_CURRENT, 1); title.seed("p", sb.CRATE_KEY, 1);
        const got = title.call("p", "OpenCrate", { crateId: sb.CRATE_CURRENT }).itemId;
        counts[got] = (counts[got] || 0) + 1;
        title.account("p").inventory = title.account("p").inventory.filter((it) => it.ItemId !== got);
    }
    sb.CRATE_POOLS[sb.CRATE_CURRENT].items.forEach((e) =>
        assertTrue(Math.abs((counts[e.itemId] || 0) / 4000 - e.chance) <= 0.025, e.itemId + " " + (counts[e.itemId] || 0) / 4000));
});

test("꾸미기 상자 — 드롭: ClaimLoot·CatchFish 가 굴려 붙이고, 칸이 차면 상자만 빠진다", () => {
    const title = makeTitle();
    const sb = title.sandbox;
    sb.CRATE_DROP_CHANCE = title.crateDropChance;
    setRandom(title, 0);
    const r = title.call("p", "ClaimLoot", { items: [{ itemId: "item.fox_meat", count: 1 }] });
    assertEqual([r.crate, title.count("p", sb.CRATE_CURRENT)], [sb.CRATE_CURRENT, 1], "맞으면 상자 +1");
    assertTrue(r.granted.some((e) => e.itemId === sb.CRATE_CURRENT), "획득 목록에도 상자");
    assertEqual(title.getData("p", "achievements").counters.hunt, 1, "상자는 사냥 수에 안 센다");
    setRandom(title, 0.5);
    title.setData("p", "lootAt", 0);
    assertEqual(title.call("p", "ClaimLoot", { items: [{ itemId: "item.fox_meat", count: 1 }] }).crate, undefined, "안 맞으면 없음");
    const full = makeTitle();
    full.sandbox.CRATE_DROP_CHANCE = full.crateDropChance;
    setRandom(full, 0);
    fillSlots(full, "q", full.sandbox.BACKPACK_SLOTS - 1);
    const f = full.call("q", "ClaimLoot", { items: [{ itemId: "item.fox_meat", count: 1 }] });
    assertEqual([f.crate, full.count("q", "item.fox_meat"), full.count("q", sb.CRATE_CURRENT)], [undefined, 1, 0], "마지막 칸 = 고기만");
    // 낚시 — 표를 직접 세워 CatchFish 한 번
    const fish = makeTitle();
    fish.sandbox.CRATE_DROP_CHANCE = fish.crateDropChance;
    setRandom(fish, 0);
    fish.setData("p", fish.sandbox.FISH_ROLL_DATA_KEY, { token: "t", fishItemId: "item.salmon", used: false });
    const c = fish.call("p", "CatchFish", { token: "t" });
    assertEqual([c.caught, c.crate, fish.count("p", sb.CRATE_CURRENT)], ["item.salmon", sb.CRATE_CURRENT, 1], "낚시 상자");
});

test("꾸미기 상자 — 열쇠는 잡화점 BuyItem 으로 코인 또는 젬, 다른 물건은 젬 불가", () => {
    const title = makeTitle();
    const sb = title.sandbox;
    title.setCoins("p", 60);
    title.account("p").currency.GM = 15;
    assertEqual(title.call("p", "BuyItem", { itemId: sb.CRATE_KEY }).cost, 50, "코인 50");
    assertEqual(title.coins("p"), 10, "코인 차감");
    assertEqual(title.call("p", "BuyItem", { itemId: sb.CRATE_KEY, currency: "GM" }).currency, "GM", "젬 구매");
    assertEqual([title.account("p").currency.GM, title.count("p", sb.CRATE_KEY)], [5, 2], "젬 10 차감 · 열쇠 2");
    assertFailsClean(title, "p", "BuyItem", { itemId: sb.CRATE_KEY, currency: "GM" }, "not_enough_gems", "젬 모자람");
    assertFailsClean(title, "p", "BuyItem", { itemId: "item.tent", currency: "GM" }, "bad_args", "텐트는 젬 불가");
});

// ══ 꾸미기 옷 `SetOutfit` (docs/specs/skin-parts.md 5장 4 · 조각 d) ══════════════════════
// 꾸미기 행은 조각 f~l 이 채운다 — 검사 전용 아이템 둘을 표에 꽂는다(Verification.md 6, 원본 파일은 그대로).
function withTestOutfits(title) {
    title.sandbox.OUTFIT_ITEMS["test.cosmetic_hat"] = "head";
    title.sandbox.OUTFIT_ITEMS["test.cosmetic_shoes"] = "shoes";
}

test("SetOutfit — 칸 이름은 클라 EEquipSlot 일곱 칸과 같다 · 표는 그 칸만 쓴다", () => {
    const sb = makeTitle().sandbox;
    assertEqual(sb.OUTFIT_SLOTS, ["head", "face", "top", "bottom", "shoes", "gloves", "back"], "칸 일곱");
    Object.keys(sb.OUTFIT_ITEMS).forEach((id) => assertTrue(sb.OUTFIT_SLOTS.indexOf(sb.OUTFIT_ITEMS[id]) !== -1, id + " 칸"));
});

test("SetOutfit — 조각 f 꾸미기 모자 57벌은 머리 칸 (클라 DT_ItemTable Cosmetic·Head 행과 같은 ID)", () => {
    const sb = makeTitle().sandbox;
    const hats = ["item.meadow_bucket", "item.meadow_cap", "item.meadow_frog",
        "item.harbor_beanie", "item.harbor_captain", "item.harbor_whale",
        "item.meadow_acorn", "item.harbor_souwester", "item.meadow_crown", "item.harbor_bandana", "item.meadow_wizard",
        "item.meadow_sunflower", "item.harbor_dixie", "item.harbor_horned", "item.meadow_mushroom", "item.meadow_tophat",
        "item.meadow_cowboy", "item.meadow_bear", "item.meadow_catears", "item.harbor_tricorn", "item.meadow_propeller",
        "item.meadow_beret", "item.harbor_fez", "item.meadow_bunny", "item.meadow_pumpkin", "item.harbor_shark",
        "item.meadow_ladybug", "item.harbor_lighthouse", "item.meadow_flowerpot", "item.meadow_carrot", "item.harbor_jellyfish",
        "item.harbor_sailor_cap", "item.meadow_teapot", "item.meadow_owl", "item.harbor_crab", "item.meadow_cake",
        "item.meadow_snail", "item.harbor_buoy", "item.meadow_pineapple", "item.harbor_starfish", "item.meadow_rocket",
        "item.meadow_duck", "item.meadow_beehive", "item.harbor_conch", "item.meadow_cactus", "item.meadow_ufo",
        "item.meadow_penguin", "item.meadow_donut", "item.meadow_mouse", "item.harbor_igloo", "item.harbor_rainbow", "item.harbor_treasure",
        "item.meadow_fox", "item.meadow_pig", "item.harbor_sailboat", "item.meadow_cheese", "item.meadow_dragon"];
    hats.forEach((id) => assertEqual(sb.OUTFIT_ITEMS[id], "head", id + " 머리 칸"));
    assertEqual(Object.keys(sb.OUTFIT_ITEMS).filter((id) => sb.OUTFIT_ITEMS[id] === "head").length, 57, "머리 칸 57벌(6 + M83-02 의 5 + M83-03 의 5 + M84-02 의 5 + M84-03 의 5 + M88-02 의 5 + M88-03 의 5 + M90-01 의 5 + M90-02 의 5 + M91-01 의 6 + M91-02 의 5)");
});

test("SetOutfit — 조각 g 꾸미기 얼굴 장식 14벌은 얼굴 칸 (클라 DT_ItemTable Cosmetic·FaceAccessory 행과 같은 ID)", () => {
    const sb = makeTitle().sandbox;
    const faces = ["item.meadow_ladybug_nose", "item.meadow_daisy_glasses", "item.meadow_petal_goggles",
        "item.harbor_eyepatch", "item.harbor_sunglasses", "item.harbor_diving_mask",
        "item.meadow_mustache", "item.harbor_bandage", "item.harbor_monocle", "item.meadow_star_shades",
        "item.harbor_kerchief", "item.meadow_cheek_stars", "item.meadow_heart_shades", "item.harbor_white_beard"];
    faces.forEach((id) => assertEqual(sb.OUTFIT_ITEMS[id], "face", id + " 얼굴 칸"));
    assertEqual(Object.keys(sb.OUTFIT_ITEMS).filter((id) => sb.OUTFIT_ITEMS[id] === "face").length, 14, "얼굴 칸 14벌(6 + M87-01 의 4 + M87-02 의 4)");
});

test("SetOutfit — 조각 h 꾸미기 상의 44벌은 상의 칸 (클라 DT_ItemTable Cosmetic·OutfitTop 행과 같은 ID)", () => {
    const sb = makeTitle().sandbox;
    const tops = ["item.meadow_tee", "item.meadow_knit", "item.meadow_petal_blouse",
        "item.harbor_stripe_tee", "item.harbor_puffer", "item.harbor_sailor",
        "item.meadow_gingham", "item.harbor_island_shirt", "item.meadow_bee_sweater",
        "item.meadow_cherry_tee", "item.harbor_anchor_sweat", "item.meadow_patchwork",
        "item.meadow_leaf_tee", "item.harbor_wave_sweat", "item.meadow_rainbow",
        "item.harbor_fish_tee", "item.meadow_strawberry", "item.harbor_sunset",
        "item.harbor_zigzag", "item.meadow_toadstool", "item.harbor_aurora",
        "item.meadow_cactus_tee", "item.harbor_snow_sweat", "item.meadow_honeycomb",
        "item.harbor_gull_tee", "item.meadow_pine_sweat", "item.harbor_flame",
        "item.meadow_daisy_tee", "item.meadow_diamond_sweat", "item.harbor_galaxy",
        "item.meadow_sun_tee", "item.harbor_kelp_sweat", "item.meadow_firefly",
        "item.harbor_shell_tee", "item.harbor_moon_sweat", "item.harbor_lantern",
        "item.meadow_acorn_tee", "item.harbor_rope_sweat", "item.harbor_coral",
        "item.meadow_clover_tee", "item.harbor_compass_sweat", "item.meadow_butterfly",
        "item.meadow_plum_tee", "item.harbor_meteor"];
    tops.forEach((id) => assertEqual(sb.OUTFIT_ITEMS[id], "top", id + " 상의 칸"));
    assertEqual(Object.keys(sb.OUTFIT_ITEMS).filter((id) => sb.OUTFIT_ITEMS[id] === "top").length, 44, "상의 칸 44벌(42 + M92-03 의 2)");
});

test("SetOutfit — 조각 i 꾸미기 하의 32벌은 하의 칸 (클라 DT_ItemTable Cosmetic·OutfitBottom 행과 같은 ID)", () => {
    const sb = makeTitle().sandbox;
    const bottoms = ["item.meadow_shorts", "item.meadow_corduroy", "item.meadow_petal_skirt",
        "item.harbor_cargo", "item.harbor_rain_pants", "item.harbor_bell_bottoms",
        "item.harbor_capri", "item.meadow_star_pants",
        "item.harbor_sail_shorts", "item.meadow_patch_pants",
        "item.harbor_check_shorts", "item.meadow_denim",
        "item.meadow_plaid_shorts", "item.harbor_polka",
        "item.meadow_cloud_shorts", "item.harbor_camo",
        "item.meadow_moon_shorts", "item.harbor_argyle",
        "item.meadow_ladybug_shorts", "item.harbor_tartan",
        "item.meadow_cherry_shorts", "item.harbor_pinstripe",
        "item.meadow_leaf_shorts", "item.harbor_wave_pants",
        "item.meadow_paw_shorts", "item.harbor_bolt_pants",
        "item.meadow_shroom_shorts", "item.harbor_chevron",
        "item.meadow_raindrop_shorts", "item.harbor_weave",
        "item.meadow_sprout_shorts", "item.harbor_dive_flag"];
    bottoms.forEach((id) => assertEqual(sb.OUTFIT_ITEMS[id], "bottom", id + " 하의 칸"));
    assertEqual(Object.keys(sb.OUTFIT_ITEMS).filter((id) => sb.OUTFIT_ITEMS[id] === "bottom").length, 32, "하의 칸 32벌(30 + M92-03 의 2)");
});

test("SetOutfit — 조각 j 꾸미기 신발 10벌은 신발 칸 (클라 DT_ItemTable Cosmetic·Shoes 행과 같은 ID)", () => {
    const sb = makeTitle().sandbox;
    const shoes = ["item.meadow_canvas", "item.meadow_clogs", "item.meadow_petal_boots",
        "item.harbor_deck_shoes", "item.harbor_rain_boots", "item.harbor_flippers",
        "item.harbor_hightops", "item.meadow_star_kicks", "item.meadow_berry_boots", "item.meadow_wing_kicks"];
    shoes.forEach((id) => assertEqual(sb.OUTFIT_ITEMS[id], "shoes", id + " 신발 칸"));
    assertEqual(Object.keys(sb.OUTFIT_ITEMS).filter((id) => sb.OUTFIT_ITEMS[id] === "shoes").length, 10, "신발 칸 10벌(6 + M87-03 의 4)");
});

test("SetOutfit — 조각 k 꾸미기 장갑 9벌은 장갑 칸 (클라 DT_ItemTable Cosmetic·Gloves 행과 같은 ID)", () => {
    const sb = makeTitle().sandbox;
    const gloves = ["item.meadow_garden_gloves", "item.meadow_mittens", "item.meadow_petal_gloves",
        "item.harbor_fingerless", "item.harbor_rubber_gloves", "item.harbor_crab_gloves",
        "item.harbor_leather_gloves", "item.meadow_wool_gloves", "item.harbor_arm_sleeves"];
    gloves.forEach((id) => assertEqual(sb.OUTFIT_ITEMS[id], "gloves", id + " 장갑 칸"));
    assertEqual(Object.keys(sb.OUTFIT_ITEMS).filter((id) => sb.OUTFIT_ITEMS[id] === "gloves").length, 9, "장갑 칸 9벌(6 + M87-04 의 3)");
});

test("SetOutfit — 조각 l 꾸미기 배낭 6벌은 등 칸 (클라 DT_ItemTable Cosmetic·Backpack 행과 같은 ID)", () => {
    const sb = makeTitle().sandbox;
    const packs = ["item.meadow_daypack", "item.meadow_basket_pack", "item.meadow_frog_pack",
        "item.harbor_duffel", "item.harbor_lifebuoy_pack", "item.harbor_octopus_pack",
        "item.meadow_mushroom_pack", "item.harbor_crate_pack", "item.meadow_beehive_pack",
        "item.meadow_watermelon_pack", "item.harbor_barrel_pack", "item.harbor_whale_pack",
        "item.meadow_strawberry_pack", "item.meadow_cat_pack", "item.harbor_submarine_pack"];
    packs.forEach((id) => assertEqual(sb.OUTFIT_ITEMS[id], "back", id + " 등 칸"));
    assertEqual(Object.keys(sb.OUTFIT_ITEMS).filter((id) => sb.OUTFIT_ITEMS[id] === "back").length, 15, "등 칸 15벌");
});

test("SetOutfit — 입기·바꾸기·벗기: 기록만 바뀌고 아이템은 배낭에 그대로", () => {
    const title = makeTitle();
    withTestOutfits(title);
    title.seed("p", "test.cosmetic_hat", 1);
    title.seed("p", "test.cosmetic_shoes", 1);
    const r = title.call("p", "SetOutfit", { slot: "head", itemId: "test.cosmetic_hat" });
    assertEqual([r.slot, r.itemId, r.outfit], ["head", "test.cosmetic_hat", { head: "test.cosmetic_hat" }], "입기 응답");
    title.call("p", "SetOutfit", { slot: "shoes", itemId: "test.cosmetic_shoes" });
    assertEqual(title.getData("p", "outfit"), { head: "test.cosmetic_hat", shoes: "test.cosmetic_shoes" }, "칸마다 따로 기록");
    assertEqual([title.count("p", "test.cosmetic_hat"), title.count("p", "test.cosmetic_shoes")], [1, 1], "배낭에 그대로");
    assertEqual(title.call("p", "GetBaseState").outfit, { head: "test.cosmetic_hat", shoes: "test.cosmetic_shoes" }, "GetBaseState 에 실린다");
    const off = title.call("p", "SetOutfit", { slot: "head", itemId: "" });
    assertEqual([off.itemId, off.outfit], ["", { shoes: "test.cosmetic_shoes" }], "벗기");
    title.call("p", "DropItem", { itemId: "test.cosmetic_shoes" });
    assertEqual(title.call("p", "GetBaseState").outfit, {}, "버린 꾸미기는 벗은 것으로 본다");
    assertEqual(title.call("q", "GetBaseState").outfit, {}, "입은 적 없는 계정");
});

test("SetOutfit — 거절이면 기록·배낭 그대로 (없는 칸·꾸미기 아님·다른 칸·안 가짐·같은 것)", () => {
    const title = makeTitle();
    withTestOutfits(title);
    title.seed("p", "test.cosmetic_hat", 1);
    title.seed("p", "item.merchant_hat", 1);
    assertFailsClean(title, "p", "SetOutfit", { slot: "neck", itemId: "test.cosmetic_hat" }, "bad_slot", "없는 칸");
    assertFailsClean(title, "p", "SetOutfit", { slot: "head", itemId: "item.merchant_hat" }, "bad_args", "효과 모자는 꾸미기가 아니다");
    assertFailsClean(title, "p", "SetOutfit", { slot: "shoes", itemId: "test.cosmetic_hat" }, "bad_args", "모자를 신발 칸에");
    assertFailsClean(title, "p", "SetOutfit", { slot: "shoes", itemId: "test.cosmetic_shoes" }, "not_enough_items", "안 산 꾸미기");
    assertFailsClean(title, "p", "SetOutfit", { slot: "head", itemId: "" }, "already", "입은 것 없이 벗기");
    title.call("p", "SetOutfit", { slot: "head", itemId: "test.cosmetic_hat" });
    assertFailsClean(title, "p", "SetOutfit", { slot: "head", itemId: "test.cosmetic_hat" }, "already", "같은 것 다시");
});

test("SetNickname — 앞뒤 공백을 자르고 받아 준 이름만 적는다 (3자·12자 끝 포함, 같은 이름 다시)", () => {
    const title = makeTitle();
    assertEqual(title.call("p", "SetNickname", { name: "  달빛여우  " }), { name: "달빛여우" }, "공백 자름");
    assertEqual(title.getData("p", "nickname"), "달빛여우", "Player Data 에 자른 이름");
    assertEqual(title.call("p", "SetNickname", { name: "곰돌이" }).name, "곰돌이", "3자 한글");
    assertEqual(title.call("p", "SetNickname", { name: "Snare12Haven" }).name, "Snare12Haven", "12자 영문·숫자");
    assertEqual(title.call("p", "SetNickname", { name: "Snare12Haven" }).name, "Snare12Haven", "같은 이름 다시 — 재시도가 막히지 않는다");
    assertEqual(title.getData("q", "nickname"), undefined, "다른 계정은 그대로");
});

test("SetNickname — 길이·글자·금칙어 거절이면 기록이 그대로", () => {
    const title = makeTitle();
    title.call("p", "SetNickname", { name: "달빛여우" });
    assertFailsClean(title, "p", "SetNickname", {}, "bad_args", "이름 없음");
    assertFailsClean(title, "p", "SetNickname", { name: 7 }, "bad_args", "글자가 아님");
    assertFailsClean(title, "p", "SetNickname", { name: " 곰 " }, "bad_length", "자르면 1자");
    assertFailsClean(title, "p", "SetNickname", { name: "곰돌" }, "bad_length", "2자 — PlayFab 표시 이름 최소 3자");
    assertFailsClean(title, "p", "SetNickname", { name: "가나다라마바사아자차카타파" }, "bad_length", "13자");
    assertFailsClean(title, "p", "SetNickname", { name: "달빛 여우" }, "bad_chars", "사이 띄어쓰기");
    assertFailsClean(title, "p", "SetNickname", { name: "ㅅㅂ여우" }, "bad_chars", "낱자모");
    assertFailsClean(title, "p", "SetNickname", { name: "여우!!" }, "bad_chars", "기호");
    assertFailsClean(title, "p", "SetNickname", { name: "여우😀" }, "bad_chars", "그림 문자");
    assertFailsClean(title, "p", "SetNickname", { name: "여우시발" }, "banned_word", "금칙어 한글");
    assertFailsClean(title, "p", "SetNickname", { name: "시1발여우" }, "banned_word", "숫자 끼운 금칙어");
    assertFailsClean(title, "p", "SetNickname", { name: "FuckFox" }, "banned_word", "대문자 금칙어");
    assertFailsClean(title, "p", "SetNickname", { name: "운영자" }, "banned_word", "운영자 사칭");
    assertEqual(title.getData("p", "nickname"), "달빛여우", "앞 이름 그대로");
});

test("SetNickname — 금칙어 흉내(숫자·반복·모음 변형)는 막고, 낱말 속 우연(Scunthorpe·Essex)은 통과 (출시 점검 10-16)", () => {
    const sb = makeTitle().sandbox;
    const bad = ["븅딱이", "쉬발놈", "개쌔끼", "느그엄마", "fuk", "f4ck", "b1tch", "n1gger", "kys", "hitler", "nazi", "whore", "retard",
        "Fuuuuck", "시이이발", "늬미럴", "KysNow", "FoxKys", "nazi99", "fuck",
        "한남충", "김치녀", "일베충", "노무현", "윤석열여우", "자살하자", "자해중", "shlt", "killyourself", "cock", "BigCock"];
    const good = ["Scunthorpe", "essex", "Dickens", "sussex", "grape", "Niger", "Fukuoka", "Badminton", "classic", "Snare12Haven", "달빛여우", "곰돌이", "세기말여우",
        "Hancock", "cockpit", "peacock", "Shelton", "김치볶음밥", "충성여우", "살자여우"];
    bad.forEach((n) => assertEqual(sb.nicknameProblem(n) && sb.nicknameProblem(n).code, "banned_word", "막아야 함: " + n));
    good.forEach((n) => assertEqual(sb.nicknameProblem(n), null, "통과해야 함: " + n));
});

test("ClaimLoot — 하루 받기 총량 상한: 넘는 호출은 daily_limit 로 거절, 날이 바뀌면 0 부터, 옛 숫자 꼴도 읽는다 (출시 점검 10-16)", () => {
    const title = makeTitle();
    const sb = title.sandbox;
    const day = Math.floor(Date.now() / 86400000);
    title.setData("p", "lootAt", { atMs: 0, day, n: sb.LOOT_DAILY_MAX - 5, coins: 0 });
    const r = assertFailsClean(title, "p", "ClaimLoot", { items: [{ itemId: "item.wood", count: 6 }] }, "daily_limit", "상한 넘김");
    assertEqual([r.limit, r.left], [sb.LOOT_DAILY_MAX, 5], "남은 수 알림");
    title.call("p", "ClaimLoot", { items: [{ itemId: "item.wood", count: 5 }] });
    assertEqual(title.getData("p", "lootAt").n, sb.LOOT_DAILY_MAX, "꽉 참");
    title.setData("q", "lootAt", { atMs: 0, day: day - 1, n: sb.LOOT_DAILY_MAX, coins: sb.LOOT_SELL_DAILY_COINS });
    title.call("q", "ClaimLoot", { items: [{ itemId: "item.wood", count: 3 }] });
    assertEqual([title.count("q", "item.wood"), title.getData("q", "lootAt").n, title.getData("q", "lootAt").coins], [3, 3, 0], "다음 날 0부터");
    title.setData("r", "lootAt", Date.now());
    assertFailsClean(title, "r", "ClaimLoot", { items: [{ itemId: "item.wood", count: 1 }] }, "too_soon", "옛 숫자 꼴 — 10초 안");
    title.setData("s", "lootAt", 1);
    title.call("s", "ClaimLoot", { items: [{ itemId: "item.wood", count: 2 }] });
    assertEqual(title.getData("s", "lootAt").n, 2, "옛 숫자 꼴 → 새 꼴로 바뀜");
});

test("SellItem — 재료·재료로 만든 것은 하루 코인 상한, 물고기는 상한 밖 (출시 점검 10-16)", () => {
    const title = makeTitle();
    const sb = title.sandbox;
    const day = Math.floor(Date.now() / 86400000);
    assertTrue(sb.isLootDerived("item.fox_meat") && sb.isLootDerived("item.roast_meat"), "고기·구운 고기는 재료 유래");
    assertTrue(!sb.isLootDerived("item.perch"), "농어는 재료 유래 아님");
    title.setData("p", "lootAt", { atMs: 0, day, n: 0, coins: sb.LOOT_SELL_DAILY_COINS });
    title.seed("p", "item.fox_meat", 1);
    title.seed("p", "item.roast_meat", 1);
    title.seed("p", "item.perch", 1);
    const r = assertFailsClean(title, "p", "SellItem", { itemId: "item.fox_meat" }, "daily_limit", "재료 상한");
    assertEqual([r.limit, r.left], [sb.LOOT_SELL_DAILY_COINS, 0], "남은 코인 알림");
    assertFailsClean(title, "p", "SellItem", { itemId: "item.roast_meat" }, "daily_limit", "가공품 상한");
    assertEqual(title.call("p", "SellItem", { itemId: "item.perch" }).sold, "item.perch", "물고기는 팔린다");
    title.setData("q", "lootAt", { atMs: 0, day, n: 0, coins: 0 });
    title.seed("q", "item.fox_meat", 1);
    const sold = title.call("q", "SellItem", { itemId: "item.fox_meat" });
    assertEqual(title.getData("q", "lootAt").coins, sold.coins, "판 코인이 쌓인다");
});

test("동시 요청 — 처리 중 표시가 살아 있으면 지급 함수 9개 모두 busy, 표시를 뺏기면 busy·지급 0, 끝나면 지운다 (출시 점검 10-16)", () => {
    const title = makeTitle();
    const live = { token: "other", untilMs: Date.now() + 10000 };
    [["GrantStarterKit", "Starter"], ["HarvestRanch", "Ranch"], ["RollGacha", "Gacha"], ["CompleteQuest", "Quest"],
        ["ClaimAttendance", "Attendance"], ["ClaimBoss", "Boss"], ["ClaimLoot", "Loot"], ["SellItem", "Loot"]].forEach(([fn, name]) => {
        const pid = "busy-" + fn;
        title.setData(pid, "lock" + name, live);
        assertFailsClean(title, pid, fn, { items: [{ itemId: "item.wood", count: 1 }] }, "busy", fn + " 표시 살아 있음");
    });
    // 떨군 가방은 주인 쪽 표시
    title.seed("a", "item.wood", 1);
    const drop = title.call("a", "DropItem", { itemId: "item.wood", count: 1 });
    title.setData("a", "lockDrops", live);
    assertFailsClean(title, "b", "PickupDrop", { ownerId: "a", dropId: drop.dropId }, "busy", "PickupDrop 주인 표시", ["a"]);
    // 기한 지난 표시는 지나가고, 끝나면 지운다
    title.setData("a", "lockDrops", { token: "old", untilMs: Date.now() - 1 });
    assertEqual(title.call("b", "PickupDrop", { ownerId: "a", dropId: drop.dropId }).picked, true, "기한 지난 표시는 무시");
    assertEqual(title.getData("a", "lockDrops"), undefined, "끝나면 표시 지움");
    // 쓰고 다시 읽는 사이 다른 호출이 표시를 덮었다 → 내 것이 아니니 busy, 아무것도 안 준다
    const real = title.sandbox.server.UpdateUserInternalData;
    title.sandbox.server.UpdateUserInternalData = function (req) {
        if (req.Data && req.Data.lockLoot) req = Object.assign({}, req, { Data: { lockLoot: JSON.stringify({ token: "racer", untilMs: Date.now() + 10000 }) } });
        return real.call(this, req);
    };
    const r = title.call("c", "ClaimLoot", { items: [{ itemId: "item.wood", count: 1 }] });
    title.sandbox.server.UpdateUserInternalData = real;
    assertEqual([r.code, title.count("c", "item.wood")], ["busy", 0], "경합에서 진 호출은 지급 0");
    // 성공한 호출도 표시를 남기지 않는다
    title.call("d", "ClaimLoot", { items: [{ itemId: "item.wood", count: 1 }] });
    assertEqual(title.getData("d", "lockLoot"), undefined, "성공 뒤 표시 지움");
});

test("보관함 PIN — 5번 틀리면 10분 잠금: 맞는 PIN·다른 사람도 pin_locked, 기한 뒤 풀린다, 주인은 안 막힌다 (출시 점검 10-16)", () => {
    const title = makeTitle();
    const sb = title.sandbox;
    const tentId = withTent(title, "a");
    title.seed("a", "item.storage_box", 1);
    const box = title.call("a", "PlaceStructure", { tentId, itemId: "item.storage_box", x: 1, y: 1, pin: "01234" });
    title.seed("a", "item.wood", 5);
    title.call("a", "MoveToStorage", { tentId, structureId: box.structureId, itemId: "item.wood", count: 5 });
    const where = { ownerId: "a", tentId, structureId: box.structureId, itemId: "item.wood", count: 1 };
    for (let i = 1; i <= sb.PIN_MAX_FAILS; i++) {
        const r = assertFailsClean(title, "b", "MoveFromStorage", Object.assign({ pin: "1111" + i }, where), "locked", i + "번째 틀림");
        assertEqual(r.failsLeft, sb.PIN_MAX_FAILS - i, "남은 횟수");
    }
    assertFailsClean(title, "b", "MoveFromStorage", Object.assign({ pin: "01234" }, where), "pin_locked", "잠긴 동안 맞는 PIN", ["a"]);
    assertFailsClean(title, "c", "MoveFromStorage", Object.assign({ pin: "01234" }, where), "pin_locked", "다른 계정도 잠김", ["a"]);
    assertEqual(title.call("a", "MoveFromStorage", { tentId, structureId: box.structureId, itemId: "item.wood", count: 1 }).storage, { "item.wood": 4 }, "주인은 그대로 연다");
    const fails = title.getData("a", "pinFails");
    assertTrue(fails[box.structureId].untilMs - Date.now() > sb.PIN_LOCK_MS - 5000, "잠금 10분");
    fails[box.structureId].untilMs = Date.now() - 1;
    title.setData("a", "pinFails", fails);
    title.call("b", "MoveFromStorage", Object.assign({ pin: "01234" }, where));
    assertEqual(title.count("b", "item.wood"), 1, "기한 뒤 맞는 PIN 으로 꺼냄");
    assertEqual(title.getData("a", "pinFails"), {}, "맞으면 기록 지움");
});

test("PickFriendFruit — 지금 같은 방이 아니면 not_in_room (내 기록 없음·내 기록만·방장 기록 오래됨·다른 방) (출시 점검 10-16)", () => {
    const title = makeTitle();
    setFruitClock(title, FRUIT_T0);
    title.call("host", "GetFruitTrees", {});
    assertFailsClean(title, "me", "PickFriendFruit", { hostId: "host" }, "not_in_room", "기록 없음", ["host"]);
    title.setData("me", "coop", { hostId: "host", atUtc: new Date().toISOString() });
    assertFailsClean(title, "me", "PickFriendFruit", { hostId: "host" }, "not_in_room", "내 기록만(방장은 안 보냄)", ["host"]);
    title.setData("host", "coop", { hostId: "host", atUtc: new Date(Date.now() - 121000).toISOString() });
    assertFailsClean(title, "me", "PickFriendFruit", { hostId: "host" }, "not_in_room", "방장 기록 121초 전", ["host"]);
    title.setData("host", "coop", { hostId: "other", atUtc: new Date().toISOString() });
    assertFailsClean(title, "me", "PickFriendFruit", { hostId: "host" }, "not_in_room", "방장이 다른 방", ["host"]);
    inRoom(title, "host", ["me"]);
    assertEqual(title.call("me", "PickFriendFruit", { hostId: "host" }).count, 3, "같은 방이면 된다");
});

test("테스터 목록 — 스크립트에 박힌 PlayFab ID 가 없고, 타이틀 키가 없으면 누구도 못 받는다 (출시 점검 10-16)", () => {
    const src = fs.readFileSync(scriptPath, "utf8");
    assertTrue(!/["'][0-9A-F]{16}["']/.test(src), "16자리 16진수 ID 문자열 없음");
    const title = makeTitle();
    assertFailsClean(title, "anyone", "GrantTesterKit", {}, "not_tester", "키 없음");
    title.titleData.testerIds = "깨진 값";
    assertFailsClean(title, "anyone", "GrantTesterKit", {}, "not_tester", "깨진 키");
});

// ── 클라가 부르는 서버 함수 이름이 서버에 다 있다 (M66-03, 언리얼 쪽 짝: Snarehaven.Backend.ServerNames) ──
function collectClientCallNames(root) {
    const names = new Set();
    (function walkDir(dir) {
        fs.readdirSync(dir, { withFileTypes: true }).forEach((entry) => {
            const full = path.join(dir, entry.name);
            if (entry.isDirectory()) {
                if (entry.name !== "Tests") walkDir(full); // 시험 글자 속 이름은 클라 호출이 아니다
            } else if (entry.name.endsWith(".cpp")) {
                const src = fs.readFileSync(full, "utf8");
                let m;
                const direct = /CallServerFunction\(\s*TEXT\("(\w+)"\)/g;
                while ((m = direct.exec(src))) names.add(m[1]);
                const getter = /::Get\w+FunctionName\(\)\s*\{\s*return TEXT\("(\w+)"\)/g;
                while ((m = getter.exec(src))) names.add(m[1]);
            }
        });
    })(root);
    return names;
}

test("클라 소스가 부르는 서버 함수 이름이 handlers 에 다 있다", () => {
    const serverSource = fs.readFileSync(scriptPath, "utf8");
    const handlers = new Set();
    const re = /^handlers\.(\w+)\s*=/gm;
    let m;
    while ((m = re.exec(serverSource))) handlers.add(m[1]);
    const client = collectClientCallNames(path.join(repoRoot, "Source", "Snarehaven", "Private"));
    if (handlers.size === 0) throw new Error("서버 핸들러를 한 개도 못 모았다 — 정규식이 깨졌다");
    if (client.size === 0) throw new Error("클라 호출 이름을 한 개도 못 모았다 — 경로·정규식이 깨졌다");
    const missing = [...client].filter((n) => !handlers.has(n));
    if (missing.length > 0) throw new Error("서버에 없는 이름: " + missing.join(", "));
    console.log("  (서버 핸들러 " + handlers.size + " · 클라가 부르는 이름 " + client.size + ")");
});

// ── 클라가 응답에서 읽는 칸 이름이 서버 글자에 다 있다 (M67-03, 언리얼 쪽 짝: Snarehaven.Backend.ResponseFields) ──
function collectClientFieldNames(root) {
    const names = new Set();
    (function walkDir(dir) {
        fs.readdirSync(dir, { withFileTypes: true }).forEach((entry) => {
            const full = path.join(dir, entry.name);
            if (entry.isDirectory()) {
                if (entry.name !== "Tests") walkDir(full); // 시험 글자 속 이름은 클라 읽기가 아니다
            } else if (entry.name.endsWith(".cpp")) {
                const src = fs.readFileSync(full, "utf8");
                const re = /(?:Get|TryGet)\w*Field\(\s*TEXT\("(\w+)"\)/g;
                let m;
                while ((m = re.exec(src))) names.add(m[1]);
            }
        });
    })(root);
    return names;
}

function missingFieldWords(clientNames, serverSource) {
    const words = new Set(serverSource.match(/\w+/g) || []);
    return [...clientNames].filter((n) => !words.has(n));
}

test("클라 소스가 응답에서 읽는 칸 이름이 서버 글자에 다 있다", () => {
    const serverSource = fs.readFileSync(scriptPath, "utf8");
    const client = collectClientFieldNames(path.join(repoRoot, "Source", "Snarehaven", "Private"));
    if (client.size === 0) throw new Error("클라 칸 이름을 한 개도 못 모았다 — 경로·정규식이 깨졌다");
    const missing = missingFieldWords(client, serverSource);
    if (missing.length > 0) throw new Error("서버 글자에 없는 칸 이름: " + missing.join(", "));
    // 역검증: 서버 사본(메모리)에서 낱말 하나를 깨면 잡아야 한다 — 파일은 안 건드린다
    const probe = [...client][0];
    const broken = serverSource.replace(new RegExp("\\b" + probe + "\\b", "g"), probe + "_x");
    if (missingFieldWords(new Set([probe]), broken).length !== 1) throw new Error("역검증 실패 — 깬 낱말을 못 잡았다: " + probe);
    console.log("  (클라가 읽는 칸 이름 " + client.size + " · 역검증 대상 " + probe + ")");
});

// M68-01 서버 글자 속 "item.…" 이름이 DT_ItemTable 행에 다 있다 — 접두사(끝이 `_` 또는 `item.` 자체)는 이름 조립용이라 허용.
const SERVER_ITEM_PREFIXES = new Set(["item.", "item.grilled_", "item.pet_", "item.seed_"]);

function serverItemNamesMissingFromTable(serverSource, tableIds) {
    const names = new Set((serverSource.match(/["']item\.[A-Za-z0-9_]*["']/g) || []).map((s) => s.slice(1, -1)));
    const missing = [...names].filter((n) => !SERVER_ITEM_PREFIXES.has(n) && !tableIds.has(n));
    return { count: names.size, missing };
}

test("서버 글자 속 item.… 이름이 아이템 표 행에 다 있다 (M68-01)", () => {
    const serverSource = fs.readFileSync(scriptPath, "utf8");
    const tableIds = new Set(dumpItemRows().map((r) => r.ItemID));
    const found = serverItemNamesMissingFromTable(serverSource, tableIds);
    if (found.count < 100) throw new Error("서버 아이템 이름을 " + found.count + "개만 모았다 — 정규식이 깨졌다");
    assertEqual(found.missing, [], "표에 없는 서버 아이템 이름");
    // 역검증: 서버 사본(메모리)에 오타 이름을 넣으면 잡아야 한다 — 파일은 안 건드린다
    const broken = serverSource + '\nvar _probe = "item.no_such_item_zz";';
    assertEqual(serverItemNamesMissingFromTable(broken, tableIds).missing, ["item.no_such_item_zz"], "역검증");
    console.log("  (서버 아이템 이름 " + found.count + " · 표 행 " + tableIds.size + ")");
});

// M69-02 서버 글자 속 "animal.…"·"ach.…"·"zone.…" 이름이 클라 생성기(create_data_tables.py)에 다 있다 — 동물은 종 "ID" 로 조립되므로 "animal.<ID>" 도 모은다.
function serverNamesMissingFromGenerator(serverSource, generatorSource) {
    const pattern = /["']((?:animal|ach|zone)\.[A-Za-z0-9_]+)["']/g;
    const collect = (text) => new Set([...text.matchAll(pattern)].map((m) => m[1]));
    const names = collect(serverSource);
    const known = collect(generatorSource);
    for (const m of generatorSource.matchAll(/"ID": "([a-z_]+)"/g)) known.add("animal." + m[1]);
    return { count: names.size, missing: [...names].filter((n) => !known.has(n)) };
}

test("서버 글자 속 animal.·ach.·zone. 이름이 클라 생성기에 다 있다 (M69-02)", () => {
    const serverSource = fs.readFileSync(scriptPath, "utf8");
    const generatorSource = fs.readFileSync(path.join(repoRoot, "tools", "unreal", "create_data_tables.py"), "utf8");
    const found = serverNamesMissingFromGenerator(serverSource, generatorSource);
    if (found.count < 25) throw new Error("서버 이름을 " + found.count + "개만 모았다 — 정규식이 깨졌다");
    assertEqual(found.missing, [], "클라 생성기에 없는 서버 이름");
    // 역검증: 서버 사본(메모리)에 오타 이름을 넣으면 잡아야 한다 — 파일은 안 건드린다
    const broken = serverSource + '\nvar _probe = ["animal.no_such_zz", "ach.no_such_zz", "zone.no_such_zz"];';
    assertEqual(serverNamesMissingFromGenerator(broken, generatorSource).missing, ["animal.no_such_zz", "ach.no_such_zz", "zone.no_such_zz"], "역검증");
    console.log("  (서버 동물·업적·존 이름 " + found.count + ")");
});

// M70-02 서버 레시피의 결과·재료(itemId) 이름이 아이템 표에 다 있다 — 씨앗·구이·주괴 키는 코드로 만들어져 M68 글자 검사에 안 잡힌다.
function recipeNamesMissingFromTable(recipes, tableIds) {
    const missing = [];
    Object.keys(recipes).forEach((id) => {
        if (!tableIds.has(id)) missing.push(id);
        (recipes[id].inputs || []).forEach((input) => {
            if (input.itemId && !tableIds.has(input.itemId)) missing.push(id + "<-" + input.itemId);
        });
    });
    return missing;
}

test("서버 레시피 결과·재료 아이템이 아이템 표에 다 있다 (M70-02)", () => {
    const recipes = makeTitle().sandbox.RECIPES;
    const tableIds = new Set(dumpItemRows().map((r) => r.ItemID));
    const count = Object.keys(recipes).length;
    if (count < 100) throw new Error("레시피를 " + count + "개만 읽었다 — 서버 상수가 바뀌었다");
    assertEqual(recipeNamesMissingFromTable(recipes, tableIds), [], "표에 없는 레시피 결과·재료");
    // 역검증: 가짜 레시피 사본(메모리)을 넣으면 잡아야 한다 — 파일은 안 건드린다
    const broken = Object.assign({}, recipes, { "item.no_such_zz": { inputs: [{ itemId: "item.wood", count: 1 }, { itemId: "item.no_such_in", count: 1 }] } });
    assertEqual(recipeNamesMissingFromTable(broken, tableIds), ["item.no_such_zz", "item.no_such_zz<-item.no_such_in"], "역검증");
    console.log("  (서버 레시피 " + count + "개)");
});

// M71-02 서버 상수 표(어종·동물 산물·옷·목장 꾸미기·모자 효과·상점)의 아이템 이름이 아이템 표에 다 있다 — 레시피(M70)·글자(M68) 밖의 길.
function constNamesMissingFromTable(groups, tableIds) {
    const missing = [];
    Object.keys(groups).forEach((g) => groups[g].forEach((id) => { if (!tableIds.has(id)) missing.push(g + ":" + id); }));
    return missing;
}

test("서버 어종·산물·옷·상점 상수의 아이템 이름이 아이템 표에 다 있다 (M71-02)", () => {
    const sb = makeTitle().sandbox;
    const tableIds = new Set(dumpItemRows().map((r) => r.ItemID));
    const groups = {
        fish: Object.keys(sb.FISH_TABLE),
        product: Object.values(sb.ANIMAL_PRODUCT_ITEM),
        outfit: Object.keys(sb.OUTFIT_ITEMS),
        ranchCosmetic: sb.RANCH_COSMETICS,
        hatEffect: Object.keys(sb.HAT_EFFECTS),
        shop: Object.keys(sb.SHOP_ITEMS),
        cosmeticShop: sb.COSMETIC_SHOP_ITEMS
    };
    const total = Object.keys(groups).reduce((n, g) => n + groups[g].length, 0);
    if (total < 150) throw new Error("상수를 " + total + "개만 읽었다 — 서버 상수가 바뀌었다");
    assertEqual(constNamesMissingFromTable(groups, tableIds), [], "표에 없는 서버 상수 아이템");
    // 역검증: 가짜 이름을 넣은 사본(메모리)은 잡아야 한다
    const broken = Object.assign({}, groups, { fish: groups.fish.concat(["item.no_such_fish"]) });
    assertEqual(constNamesMissingFromTable(broken, tableIds), ["fish:item.no_such_fish"], "역검증");
    console.log("  (서버 상수 아이템 " + total + "개)");
});

// M72-02 가챠 풀·상자 보상·퀘스트 풀·꾸미기 상수의 이름이 아이템 표(또는 코인·재료 경품 표)에 다 있다 — M71 밖의 길.
function poolNamesMissing(groups, tableIds, prizeIds) {
    const missing = [];
    Object.keys(groups).forEach((g) => groups[g].forEach((id) => { if (!tableIds.has(id) && !prizeIds.has(id)) missing.push(g + ":" + id); }));
    return missing;
}

test("서버 가챠 풀·상자·퀘스트·꾸미기 상수의 이름이 표 또는 경품 표에 다 있다 (M72-02)", () => {
    const sb = makeTitle().sandbox;
    const tableIds = new Set(dumpItemRows().map((r) => r.ItemID));
    const prizeIds = new Set(Object.keys(sb.GACHA_COIN_PRIZES).concat(Object.keys(sb.GACHA_MATERIAL_PRIZES)));
    const groups = {
        gachaPool: sb.GACHA_POOL.map((p) => p.id),
        gachaDeco: Object.keys(sb.GACHA_RANCH_DECOS),
        gachaPet: sb.GACHA_PET_COSMETICS,
        chest: sb.CHEST_LOOT.map((c) => c.itemId),
        quest: sb.QUEST_POOL.map((q) => q[0]),
        substitute: Object.values(sb.GACHA_SUBSTITUTES)
    };
    const total = Object.keys(groups).reduce((n, g) => n + groups[g].length, 0);
    if (total < 60 || prizeIds.size < 8) throw new Error("상수를 " + total + "개만 읽었다 — 서버 상수가 바뀌었다");
    assertEqual(poolNamesMissing(groups, tableIds, prizeIds), [], "표·경품 표에 없는 서버 풀 이름");
    // 역검증: 가짜 이름을 넣은 사본(메모리)은 잡아야 한다
    const broken = Object.assign({}, groups, { chest: groups.chest.concat(["item.no_such_zz"]) });
    assertEqual(poolNamesMissing(broken, tableIds, prizeIds), ["chest:item.no_such_zz"], "역검증");
    console.log("  (서버 풀 이름 " + total + "개)");
});

// M73-02 시작·테스터 키트의 아이템 이름이 아이템 표에 있고 개수가 1 이상 정수다 — M71·M72 가 못 본 지급 목록.
function kitProblems(kits, tableIds) {
    const bad = [];
    Object.keys(kits).forEach((k) => kits[k].forEach((e) => {
        if (!tableIds.has(e.itemId)) bad.push(k + ":이름 " + e.itemId);
        if (!(Number.isInteger(e.count) && e.count >= 1)) bad.push(k + ":개수 " + e.itemId + "=" + e.count);
    }));
    return bad;
}

test("서버 시작·테스터 키트의 아이템이 표에 있고 개수가 1 이상이다 (M73-02)", () => {
    const sb = makeTitle().sandbox;
    const tableIds = new Set(dumpItemRows().map((r) => r.ItemID));
    const kits = { starter: sb.STARTER_KIT, tester: sb.TESTER_KIT };
    const total = kits.starter.length + kits.tester.length;
    if (total < 3) throw new Error("키트를 " + total + "줄만 읽었다 — 서버 상수가 바뀌었다");
    assertEqual(kitProblems(kits, tableIds), [], "키트 문제");
    // 역검증: 가짜 이름·0 개를 넣은 사본(메모리)은 잡아야 한다
    const broken = { starter: kits.starter.concat([{ itemId: "item.no_such_kit", count: 0 }]), tester: kits.tester };
    assertEqual(kitProblems(broken, tableIds), ["starter:이름 item.no_such_kit", "starter:개수 item.no_such_kit=0"], "역검증");
    console.log("  (키트 " + total + "줄)");
});

// M74-02 서버 소스 어디에 쓰였든 `item.`·`gacha.` 완성형 글자는 표 또는 경품 표에 있다 — M68~M73 상수 목록 밖의 마지막 그물.
function literalsMissing(sourceText, tableIds, prizeIds) {
    const found = new Set((sourceText.match(/["'](?:item|gacha)\.[a-z0-9_]+["']/g) || []).map((x) => x.slice(1, -1)));
    const miss = [];
    found.forEach((id) => {
        if (id.endsWith("_")) return; // 접두어 조각(`item.pet_` + 종류)
        if (!tableIds.has(id) && !prizeIds.has(id)) miss.push(id);
    });
    return { found: found.size, miss: miss.sort() };
}

test("서버 소스의 item.·gacha. 글자가 표 또는 경품 표에 다 있다 (M74-02)", () => {
    const sb = makeTitle().sandbox;
    const tableIds = new Set(dumpItemRows().map((r) => r.ItemID));
    const prizeIds = new Set(Object.keys(sb.GACHA_COIN_PRIZES).concat(Object.keys(sb.GACHA_MATERIAL_PRIZES)));
    const text = fs.readFileSync(scriptPath, "utf8");
    const r = literalsMissing(text, tableIds, prizeIds);
    if (r.found < 100) throw new Error("글자를 " + r.found + "개만 찾았다 — 읽는 식이 깨졌다");
    assertEqual(r.miss, [], "표에 없는 서버 글자");
    // 역검증: 가짜 이름을 덧붙인 사본(메모리)은 잡아야 한다
    assertEqual(literalsMissing(text + "\nx('item.no_such_lit');", tableIds, prizeIds).miss, ["item.no_such_lit"], "역검증");
    console.log("  (서버 글자 " + r.found + "개)");
});

// M75-02 서버 `fail("코드", …)` 의 코드는 BACKEND.md 「실패 응답」 코드 목록(`{error, code}` 줄)에 다 적혀 있다.
function failCodesUndocumented(sourceText, docText) {
    const line = docText.split("\n").find((l) => l.indexOf("실패 응답은 `{error, code}`") !== -1) || "";
    const codes = new Set((sourceText.match(/\bfail\("[a-z_]+"/g) || []).map((x) => x.slice(6, -1)));
    const miss = [];
    codes.forEach((c) => { if (line.indexOf("`" + c + "`") === -1) miss.push(c); });
    return { found: codes.size, lineFound: line.length > 0, miss: miss.sort() };
}

test("서버 fail 코드가 BACKEND.md 코드 목록에 다 있다 (M75-02)", () => {
    const text = fs.readFileSync(scriptPath, "utf8");
    const doc = fs.readFileSync(path.join(repoRoot, "docs", "BACKEND.md"), "utf8");
    const r = failCodesUndocumented(text, doc);
    if (!r.lineFound) throw new Error("BACKEND.md 에서 코드 목록 줄을 못 찾았다");
    if (r.found < 30) throw new Error("fail 코드를 " + r.found + "개만 찾았다 — 읽는 식이 깨졌다");
    assertEqual(r.miss, [], "문서에 없는 fail 코드");
    // 역검증: 가짜 코드를 덧붙인 사본(메모리)은 잡아야 한다
    assertEqual(failCodesUndocumented(text + '\nreturn fail("no_such_code", "x");', doc).miss, ["no_such_code"], "역검증");
    console.log("  (fail 코드 " + r.found + "종)");
});

// M76-02 클라 소스(Tests 밖) 글자 속 animal.·ach.·zone. 이름이 생성기에 다 있다 — M69 의 반대쪽.
// `zone.danger` 는 위험구역 존 표가 아직 없어 가정한 ID(ZoneTravelComponent.cpp 🟡)라 허용한다.
function readClientSources(dir) {
    let text = "";
    fs.readdirSync(dir, { withFileTypes: true }).forEach((entry) => {
        const full = path.join(dir, entry.name);
        if (entry.isDirectory()) { if (entry.name !== "Tests") text += readClientSources(full); }
        else if (/\.cpp$/.test(entry.name)) text += "\n" + fs.readFileSync(full, "utf8");
    });
    return text;
}

test("클라 소스 글자 속 animal.·ach.·zone. 이름이 생성기에 다 있다 (M76-02)", () => {
    const clientSource = readClientSources(path.join(repoRoot, "Source", "Snarehaven", "Private"));
    const generatorSource = fs.readFileSync(path.join(repoRoot, "tools", "unreal", "create_data_tables.py"), "utf8");
    const found = serverNamesMissingFromGenerator(clientSource, generatorSource);
    if (found.count < 8) throw new Error("클라 이름을 " + found.count + "개만 모았다 — 읽는 식이 깨졌다");
    assertEqual(found.missing.filter((n) => n !== "zone.danger"), [], "생성기에 없는 클라 이름");
    // 역검증: 클라 사본(메모리)에 오타 이름을 넣으면 잡아야 한다
    assertEqual(serverNamesMissingFromGenerator(clientSource + '\nx("zone.no_such_zz");', generatorSource).missing.filter((n) => n !== "zone.danger"), ["zone.no_such_zz"], "역검증");
    console.log("  (클라 동물·업적·존 이름 " + found.count + ")");
});

// ══ 고구마 시세 (8.17) ═══════════════════════════════════════════════════════════
// 시험 시계: 일요일 = 날짜 번호가 (day + 4) % 7 === 0 인 날. 주 번호는 floor((day + 4) / 7).
const POTATO_SUNDAY = 7 * 3000 - 4; // (day+4)%7 === 0
function setPotatoClock(title, dayOffset, half) {
    title.sandbox.potatoClock = () => ({ day: POTATO_SUNDAY + dayOffset, half });
}

test("고구마 시세 — 같은 씨앗은 같은 값, 12칸 배율은 무늬별 범위 안, 값은 정수 ≥ 1", () => {
    const sb = makeTitle().sandbox;
    const a = sb.sweetPotatoWeek("p", 3000);
    assertEqual(JSON.stringify(sb.sweetPotatoWeek("p", 3000)), JSON.stringify(a), "같은 씨앗");
    assertTrue(JSON.stringify(sb.sweetPotatoWeek("p", 3001)) !== JSON.stringify(a)
        || JSON.stringify(sb.sweetPotatoWeek("q", 3000)) !== JSON.stringify(a), "씨앗이 다르면 다르다");
    for (let w = 0; w < 3000; w++) {
        const t = sb.sweetPotatoWeek("acct" + (w % 37), w);
        assertEqual(t.prices.length, 12, "12칸");
        assertTrue(t.base >= 9 && t.base <= 11, "사는 값 9~11");
        const top = Math.max.apply(null, t.rates);
        t.prices.forEach((v) => assertTrue(Number.isInteger(v) && v >= 1, "정수 코인 ≥ 1"));
        t.rates.forEach((r) => assertTrue(r >= 0.4 - 1e-9 && r <= 4, "배율 0.4~4: " + r));
        if (t.pattern === "wave") t.rates.forEach((r) => assertTrue(r >= 0.9 && r <= 1.4, "파동 0.9~1.4"));
        if (t.pattern === "decline") {
            for (let i = 1; i < 12; i++) assertTrue(t.rates[i] <= t.rates[i - 1] + 1e-9, "하락은 안 오른다");
            assertTrue(t.rates[0] <= 0.9, "하락 첫 칸 ≤ 0.9");
        }
        if (t.pattern === "smallSpike") assertTrue(top <= 2.0, "소폭등 최고 2.0");
        if (t.pattern === "bigSpike") assertTrue(top >= 2.0 && top <= 4.0, "대폭등 정점 2.0~4.0");
    }
});

test("고구마 시세 — 무늬 비율 35/30/20/15 (씨앗 10,000개 ±2%p)", () => {
    const sb = makeTitle().sandbox;
    const n = 10000;
    const seen = {};
    for (let i = 0; i < n; i++) {
        const t = sb.sweetPotatoWeek("player-" + i, 2900 + (i % 53));
        seen[t.pattern] = (seen[t.pattern] || 0) + 1;
    }
    const want = { wave: 35, smallSpike: 30, bigSpike: 20, decline: 15 };
    Object.keys(want).forEach((k) => {
        const pct = ((seen[k] || 0) / n) * 100;
        assertTrue(Math.abs(pct - want[k]) <= 2, k + " 비율 " + pct.toFixed(1) + "% (기대 " + want[k] + "±2)");
    });
});

test("고구마 — 일요일에만 사고, 상한 100개, 코인·가방 부족은 그대로 둔다", () => {
    const title = makeTitle();
    setPotatoClock(title, 1, 0); // 월요일
    assertFailsClean(title, "p", "BuySweetPotato", { bundles: 1 }, "closed", "평일 사기");
    setPotatoClock(title, 0, 0); // 일요일
    const g = title.call("p", "GetSweetPotato", {});
    assertEqual([g.weekday, g.slot, g.cap, g.boughtThisWeek], [0, -1, 100, 0], "일요일 상태");
    const base = title.sandbox.sweetPotatoWeek("p", title.sandbox.potatoWeekOf(POTATO_SUNDAY)).base;
    assertEqual(g.buyPrice, base, "사는 값");
    assertFailsClean(title, "p", "BuySweetPotato", { bundles: 1 }, "not_enough_coins", "코인 0");
    assertFailsClean(title, "p", "BuySweetPotato", { bundles: 0 }, "bad_args", "0묶음");
    assertFailsClean(title, "p", "BuySweetPotato", { bundles: 11 }, "bad_args", "11묶음");
    title.setCoins("p", 5000);
    const r = title.call("p", "BuySweetPotato", { bundles: 5 });
    assertEqual([r.count, r.cost, r.boughtThisWeek], [50, 50 * base, 50], "5묶음");
    assertEqual([title.count("p", "item.sweet_potato"), title.coins("p")], [50, 5000 - 50 * base], "가방·코인");
    assertEqual(title.getData("p", "sweetPotato"), { week: title.sandbox.potatoWeekOf(POTATO_SUNDAY), bought: 50, paid: 50 * base }, "산 기록");
    assertFailsClean(title, "p", "BuySweetPotato", { bundles: 6 }, "cap_reached", "상한 초과");
    title.call("p", "BuySweetPotato", { bundles: 5 });
    assertEqual(title.count("p", "item.sweet_potato"), 100, "100개까지");
    assertFailsClean(title, "p", "BuySweetPotato", { bundles: 1 }, "cap_reached", "상한 도달");
});

test("고구마 — 월~토 지금 칸 값 × 개수로 판다, 일요일·없는 수량은 거절", () => {
    const title = makeTitle();
    setPotatoClock(title, 0, 0);
    title.setCoins("p", 1000);
    title.call("p", "BuySweetPotato", { bundles: 2 });
    const week = title.sandbox.potatoWeekOf(POTATO_SUNDAY);
    const prices = title.sandbox.sweetPotatoWeek("p", week).prices;
    assertFailsClean(title, "p", "SellSweetPotato", { count: 1 }, "closed", "일요일 팔기");
    setPotatoClock(title, 3, 1); // 수요일 오후 = 칸 5
    const g = title.call("p", "GetSweetPotato", {});
    assertEqual([g.slot, g.price, g.prevPrice], [5, prices[5], prices[4]], "지금 칸·지난 칸");
    assertTrue(g.buyPrice === undefined, "평일엔 사는 값 없음");
    assertFailsClean(title, "p", "SellSweetPotato", { count: 21 }, "not_enough_items", "가진 것보다 많이");
    assertFailsClean(title, "p", "SellSweetPotato", { count: 0 }, "bad_args", "0개");
    const before = title.coins("p");
    const r = title.call("p", "SellSweetPotato", { count: 8 });
    assertEqual([r.price, r.coins, title.coins("p") - before, title.count("p", "item.sweet_potato")], [prices[5], prices[5] * 8, prices[5] * 8, 12], "8개 팔기");
    setPotatoClock(title, 1, 0); // 월요일 오전 = 칸 0 → 지난 칸 없음
    assertEqual(title.call("p", "GetSweetPotato", {}).prevPrice, 0, "첫 칸은 지난 값 없음");
});

test("고구마 — 다음 일요일이 오면 썩는다 (판매 호출·시세 조회·GetBaseState 어느 쪽이든)", () => {
    ["SellSweetPotato", "GetSweetPotato", "GetBaseState"].forEach((handler) => {
        const title = makeTitle();
        setPotatoClock(title, 0, 0);
        title.setCoins("p", 1000);
        title.call("p", "BuySweetPotato", { bundles: 3 });
        setPotatoClock(title, 6, 1); // 토요일 — 아직 안 썩음
        title.call("p", "GetSweetPotato", {});
        assertEqual(title.count("p", "item.sweet_potato"), 30, "토요일엔 그대로");
        setPotatoClock(title, 7, 0); // 다음 일요일
        title.call("p", handler, handler === "SellSweetPotato" ? { count: 1 } : {});
        assertEqual([title.count("p", "item.sweet_potato"), title.count("p", "item.sweet_potato_rotten")], [0, 30], handler + " 뒤 썩음");
        assertEqual(title.getData("p", "sweetPotato").bought, 0, "산 기록 비움");
        const g = title.call("p", "GetSweetPotato", {});
        assertEqual([g.boughtThisWeek, g.paid], [0, 0], "새 주 상한 0부터");
        title.call("p", "BuySweetPotato", { bundles: 10 }); // 새 주엔 다시 100개까지 살 수 있다
    });
    // 썩은 고구마는 팔 수 없고(판매가 0), 심을 수 없다
    const title = makeTitle();
    title.seed("p", "item.sweet_potato_rotten", 2);
    title.seed("p", "item.sweet_potato", 2);
    assertFailsClean(title, "p", "SellItem", { itemId: "item.sweet_potato_rotten", count: 1 }, "not_sellable", "썩은 고구마");
    assertFailsClean(title, "p", "PlantCrop", { slot: 0, seedItemId: "item.sweet_potato" }, "bad_args", "고구마 심기");
});

test("고구마 — 한 바퀴: 일요일 100개 사기 → 월~토 12칸마다 그 칸 값으로 팔기 → 다음 일요일 남은 것 썩음", () => {
    const title = makeTitle();
    setPotatoClock(title, 0, 0);
    title.setCoins("p", 5000);
    const week = title.sandbox.potatoWeekOf(POTATO_SUNDAY);
    const t = title.sandbox.sweetPotatoWeek("p", week);
    title.call("p", "BuySweetPotato", { bundles: 10 });
    assertEqual([title.count("p", "item.sweet_potato"), title.coins("p")], [100, 5000 - 100 * t.base], "일요일 100개");
    let coins = title.coins("p");
    for (let d = 1; d <= 6; d++) {
        for (let h = 0; h < 2; h++) {
            setPotatoClock(title, d, h);
            const slot = (d - 1) * 2 + h;
            const g = title.call("p", "GetSweetPotato", {});
            assertEqual([g.weekday, g.slot, g.price, g.prevPrice], [d, slot, t.prices[slot], slot ? t.prices[slot - 1] : 0], "칸 " + slot);
            const r = title.call("p", "SellSweetPotato", { count: 7 });
            coins += t.prices[slot] * 7;
            assertEqual([r.price, title.coins("p")], [t.prices[slot], coins], "칸 " + slot + " 팔기");
        }
    }
    assertEqual(title.count("p", "item.sweet_potato"), 100 - 84, "84개 팔고 16개 남음");
    setPotatoClock(title, 7, 0);
    title.call("p", "GetSweetPotato", {});
    assertEqual([title.count("p", "item.sweet_potato"), title.count("p", "item.sweet_potato_rotten")], [0, 16], "다음 일요일 16개 썩음");
});

// ══ 과일나무 (8.18) ══════════════════════════════════════════════════════════════
const FRUIT_T0 = 86400000 * 20000; // 시험 시작 시각(ms)
const HOUR_MS = 3600000;
function setFruitClock(title, ms) {
    title.sandbox.fruitClock = () => ({ now: ms, day: Math.floor(ms / 86400000) });
}
/** 두 사람이 지금 같은 방 — ReportCoop 이 남기는 기록(방장 자신도 보낸다). */
function inRoom(title, hostId, guests) {
    const rec = { hostId, atUtc: new Date().toISOString() };
    [hostId].concat(guests).forEach((p) => title.setData(p, "coop", rec));
}
function nativeFruitOfId(title, id) { return title.sandbox.nativeFruitOf(id); }
/** 고유 과일이 `want` 인 / 아닌 계정 ID 를 찾는다. */
function accountWithNative(title, want, notWant) {
    for (let i = 0; i < 1000; i++) {
        const id = "acct-" + i;
        const n = nativeFruitOfId(title, id);
        if (want ? n === want : n !== notWant) return id;
    }
    throw new Error("계정을 못 찾음");
}

test("과일나무 — 고유 과일은 같은 계정이면 늘 같고, 5종이 계정 10,000개에서 20%±2%p 씩", () => {
    const title = makeTitle();
    const seen = {};
    for (let i = 0; i < 10000; i++) {
        const f = title.sandbox.nativeFruitOf("player-" + i);
        assertEqual(title.sandbox.nativeFruitOf("player-" + i), f, "같은 계정");
        seen[f] = (seen[f] || 0) + 1;
    }
    assertEqual(Object.keys(seen).sort(), ["item.cherry", "item.orange", "item.peach", "item.pear", "item.plum"], "과일 5종");
    Object.keys(seen).forEach((k) => {
        const pct = (seen[k] / 10000) * 100;
        assertTrue(Math.abs(pct - 20) <= 2, k + " 비율 " + pct.toFixed(1) + "%");
    });
});

test("과일나무 — 처음 부르면 고유 과일 다 자란 나무 2그루(열매 3개씩), 다시 불러도 그대로", () => {
    const title = makeTitle();
    setFruitClock(title, FRUIT_T0);
    const g = title.call("p", "GetFruitTrees", {});
    const native = nativeFruitOfId(title, "p");
    assertEqual(g.native, native, "고유 과일");
    assertEqual(g.trees, [
        { slot: 0, fruit: native, stage: "grown", readyAt: 0, fruits: 3 },
        { slot: 1, fruit: native, stage: "grown", readyAt: 0, fruits: 3 }
    ], "처음 나무 2그루");
    assertEqual(g.pickedToday, [], "오늘 얻은 방장 없음");
    assertEqual(title.call("p", "GetFruitTrees", {}).trees, g.trees, "두 번째도 같다");
});

test("과일나무 — 심기: 잘못된 값·찬 자리·과일 없음은 아무것도 안 바꾸고 거절, 72시간 경계에 다 자란다", () => {
    const title = makeTitle();
    setFruitClock(title, FRUIT_T0);
    title.call("p", "GetFruitTrees", {});
    assertFailsClean(title, "p", "PlantFruit", { slot: 4, itemId: "item.pear" }, "bad_args", "자리 4");
    assertFailsClean(title, "p", "PlantFruit", { slot: -1, itemId: "item.pear" }, "bad_args", "자리 -1");
    assertFailsClean(title, "p", "PlantFruit", { slot: 2, itemId: "item.apple" }, "bad_args", "과일 아님");
    assertFailsClean(title, "p", "PlantFruit", { slot: 2, itemId: "item.pear" }, "not_enough_items", "과일 없음");
    title.seed("p", "item.pear", 2);
    assertFailsClean(title, "p", "PlantFruit", { slot: 0, itemId: "item.pear" }, "slot_full", "찬 자리");
    const r = title.call("p", "PlantFruit", { slot: 2, itemId: "item.pear" });
    assertEqual([r.planted, r.slot, r.readyAt], ["item.pear", 2, FRUIT_T0 + 72 * HOUR_MS], "심기");
    assertEqual(title.count("p", "item.pear"), 1, "과일 1개 소모");
    const at = (ms) => { setFruitClock(title, ms); return title.call("p", "GetFruitTrees", {}).trees.filter((t) => t.slot === 2)[0]; };
    assertEqual(at(FRUIT_T0 + 72 * HOUR_MS - 1), { slot: 2, fruit: "item.pear", stage: "sapling", readyAt: FRUIT_T0 + 72 * HOUR_MS, fruits: 0 }, "71시간 59분 59.999초 = 묘목");
    assertEqual(at(FRUIT_T0 + 72 * HOUR_MS), { slot: 2, fruit: "item.pear", stage: "grown", readyAt: 0, fruits: 3 }, "72시간 = 다 자람");
    setFruitClock(title, FRUIT_T0 + 72 * HOUR_MS - 1);
    assertFailsClean(title, "p", "HarvestTree", { slot: 2 }, "not_ready", "묘목 따기");
    assertFailsClean(title, "p", "ChopTree", { slot: 2 }, "not_ready", "묘목 베기");
});

test("과일나무 — 한 바퀴: 심고 72시간 뒤 그 나무에서 심은 과일 3개를 따고, 딴 뒤엔 열매 0·24시간 뒤 다시 3개", () => {
    const title = makeTitle();
    setFruitClock(title, FRUIT_T0);
    title.call("p", "GetFruitTrees", {});
    title.seed("p", "item.plum", 1);
    title.call("p", "PlantFruit", { slot: 3, itemId: "item.plum" });
    assertEqual(title.count("p", "item.plum"), 0, "심어서 0개");
    const slot3 = () => title.call("p", "GetFruitTrees", {}).trees.filter((t) => t.slot === 3)[0];
    setFruitClock(title, FRUIT_T0 + 72 * HOUR_MS);
    assertEqual([slot3().stage, slot3().fruits], ["grown", 3], "72시간 = 열매 3개");
    const r = title.call("p", "HarvestTree", { slot: 3 });
    assertEqual([r.harvested, r.itemId, title.count("p", "item.plum")], [3, "item.plum", 3], "심은 과일 3개 수확");
    assertEqual([slot3().stage, slot3().fruits, slot3().readyAt], ["grown", 0, FRUIT_T0 + 96 * HOUR_MS], "딴 뒤 열매 0");
    setFruitClock(title, FRUIT_T0 + 96 * HOUR_MS);
    assertEqual(slot3().fruits, 3, "24시간 뒤 다시 3개");
});

test("과일나무 — 따면 3개, 24시간 전엔 not_ready, 24시간 뒤 다시 3개, 안 따도 쌓이지 않는다", () => {
    const title = makeTitle();
    setFruitClock(title, FRUIT_T0);
    const native = nativeFruitOfId(title, "p");
    assertFailsClean(title, "p", "HarvestTree", { slot: 3 }, "empty_slot", "빈 자리");
    assertFailsClean(title, "p", "HarvestTree", { slot: 9 }, "bad_args", "자리 9");
    const r = title.call("p", "HarvestTree", { slot: 0 });
    assertEqual([r.harvested, r.itemId, r.readyAt, title.count("p", native)], [3, native, FRUIT_T0 + 24 * HOUR_MS, 3], "첫 수확");
    setFruitClock(title, FRUIT_T0 + 24 * HOUR_MS - 1);
    const early = assertFailsClean(title, "p", "HarvestTree", { slot: 0 }, "not_ready", "24시간 전");
    assertEqual(early.readyAt, FRUIT_T0 + 24 * HOUR_MS, "다시 열리는 때");
    assertEqual(title.call("p", "GetFruitTrees", {}).trees[1].fruits, 3, "손 안 댄 다른 나무는 그대로 3개");
    setFruitClock(title, FRUIT_T0 + 24 * HOUR_MS);
    assertEqual(title.call("p", "HarvestTree", { slot: 0 }).harvested, 3, "24시간 뒤 다시 3개");
    setFruitClock(title, FRUIT_T0 + 24 * HOUR_MS + 200 * HOUR_MS);
    assertEqual(title.call("p", "GetFruitTrees", {}).trees[0].fruits, 3, "오래 둬도 3개에서 멈춤");
    assertEqual(title.call("p", "HarvestTree", { slot: 0 }).harvested, 3, "쌓이지 않아 3개만");
    assertEqual(title.count("p", native), 9, "가방 합계");
});

test("과일나무 — 베기: 다 자란 나무만, 자리가 비고 나무 2개, 빈 자리는 다시 심을 수 있다", () => {
    const title = makeTitle();
    setFruitClock(title, FRUIT_T0);
    assertFailsClean(title, "p", "ChopTree", { slot: 2 }, "empty_slot", "빈 자리");
    const r = title.call("p", "ChopTree", { slot: 1 });
    assertEqual([r.wood, title.count("p", "item.wood")], [2, 2], "나무 2");
    assertEqual(title.call("p", "GetFruitTrees", {}).trees.map((t) => t.slot), [0], "자리 1 비었다");
    assertFailsClean(title, "p", "ChopTree", { slot: 1 }, "empty_slot", "두 번 베기");
    title.seed("p", "item.cherry", 1);
    assertEqual(title.call("p", "PlantFruit", { slot: 1, itemId: "item.cherry" }).slot, 1, "비운 자리에 심기");
});

test("과일나무 — 친구 나무 흔들기: 방장 고유 과일 3개, 방장 한 명당 하루 1번, 내 나무는 거절, 방장 데이터는 안 건드린다", () => {
    const title = makeTitle();
    setFruitClock(title, FRUIT_T0);
    title.call("host", "GetFruitTrees", {});
    inRoom(title, "host", ["me"]);
    const hostBefore = title.snapshot("host");
    assertFailsClean(title, "me", "PickFriendFruit", { hostId: "me" }, "same_player", "내 나무");
    assertFailsClean(title, "me", "PickFriendFruit", {}, "bad_args", "hostId 없음");
    const hostFruit = nativeFruitOfId(title, "host");
    const r = title.call("me", "PickFriendFruit", { hostId: "host" });
    assertEqual([r.picked, r.count, title.count("me", hostFruit)], [hostFruit, 3, 3], "방장 과일 3개");
    assertEqual(title.snapshot("host"), hostBefore, "방장 데이터·가방 그대로");
    assertEqual(title.call("me", "GetFruitTrees", {}).pickedToday, ["host"], "오늘 얻은 방장");
    assertFailsClean(title, "me", "PickFriendFruit", { hostId: "host" }, "already_picked", "같은 방장 하루 두 번");
    title.call("host2", "GetFruitTrees", {});
    inRoom(title, "host2", ["me"]);
    assertEqual(title.call("me", "PickFriendFruit", { hostId: "host2" }).count, 3, "다른 방장은 된다");
    setFruitClock(title, FRUIT_T0 + 86400000); // 다음 날(UTC)
    inRoom(title, "host", ["me"]);
    assertEqual(title.call("me", "PickFriendFruit", { hostId: "host" }).count, 3, "다음 날 다시");
    assertEqual(title.call("me", "GetFruitTrees", {}).pickedToday, ["host"], "날이 바뀌면 목록 비움");
});

test("과일나무 — 하루에 서로 다른 방장 3명까지, 없는 계정 hostId 는 거절 (가짜 hostId 돌려막기 막이)", () => {
    const title = makeTitle();
    setFruitClock(title, FRUIT_T0);
    for (let i = 0; i < 4; i++) title.call("h" + i, "GetFruitTrees", {});
    assertFailsClean(title, "me", "PickFriendFruit", { hostId: "fake0" }, "bad_args", "가짜 hostId");
    for (let i = 0; i < 3; i++) { inRoom(title, "h" + i, ["me"]); title.call("me", "PickFriendFruit", { hostId: "h" + i }); }
    inRoom(title, "h3", ["me"]);
    const r = assertFailsClean(title, "me", "PickFriendFruit", { hostId: "h3" }, "already_picked", "4번째 방장");
    assertEqual(r.limit, 3, "한도 알림");
});

test("과일나무 — 팔기: 고유 과일 8, 남의 과일 40 (장사치 중절 없이)", () => {
    const title = makeTitle();
    const native = nativeFruitOfId(title, "p");
    const other = ["item.pear", "item.peach", "item.cherry", "item.orange", "item.plum"].filter((f) => f !== native)[0];
    title.seed("p", native, 3);
    title.seed("p", other, 2);
    assertEqual([title.call("p", "SellItem", { itemId: native, count: 3 }).coins, title.coins("p")], [24, 24], "고유 3개");
    assertEqual([title.call("p", "SellItem", { itemId: other, count: 2 }).coins, title.coins("p")], [80, 104], "남의 것 2개");
});

// ── 건네기 (DESIGN 3.3 「건네기」, M104-05) ──────────────────────────────────────────
function giftTitle() {
    const title = makeTitle();
    title.account("a");
    title.account("b");
    return title;
}

test("건네기 — 보내면 보낸 사람 배낭에서 빠지고 수락하면 받는 사람이 갖는다", () => {
    const title = giftTitle();
    title.seed("a", "item.wood", 5);
    const sent = title.call("a", "GiftItem", { targetPlayerId: "b", itemId: "item.wood", count: 3 });
    assertEqual([sent.sent, title.count("a", "item.wood"), title.count("b", "item.wood")], [true, 2, 0], "보낸 직후");
    const list = title.call("b", "GetGifts");
    assertEqual([list.gifts.length, list.gifts[0].count], [1, 3], "받는 쪽 목록");
    const got = title.call("b", "AcceptGift", { giftId: sent.giftId });
    assertEqual([got.accepted, title.count("b", "item.wood"), title.count("a", "item.wood")], [true, 3, 2], "수락 뒤");
    assertEqual(title.call("b", "GetGifts").gifts.length, 0, "대기 비움");
    assertEqual(title.call("b", "AcceptGift", { giftId: sent.giftId }).code, "bad_args", "두 번째 수락은 없는 선물");
    assertEqual(title.count("b", "item.wood"), 3, "두 번 받아도 늘지 않음");
});

test("건네기 — 거절하면 보낸 사람에게 돌아간다", () => {
    const title = giftTitle();
    title.seed("a", "item.wood", 4);
    const sent = title.call("a", "GiftItem", { targetPlayerId: "b", itemId: "item.wood", count: 4 });
    assertEqual(title.call("b", "DeclineGift", { giftId: sent.giftId }).declined, true, "거절");
    assertEqual([title.count("a", "item.wood"), title.count("b", "item.wood")], [4, 0], "돌아옴");
});

test("건네기 — 실패 코드: 제외 아이템·없는 아이템·모자람·자기 자신·잘못된 개수", () => {
    const title = giftTitle();
    ["gacha.pet_ribbon", "item.pet_wolf", "item.collar_hunting", "item.ranch_hat"].forEach((id) => {
        title.seed("a", id, 1);
        assertEqual(title.call("a", "GiftItem", { targetPlayerId: "b", itemId: id, count: 1 }).code, "excluded_item", id);
        assertEqual(title.count("a", id), 1, id + " 그대로");
    });
    assertEqual(title.call("a", "GiftItem", { targetPlayerId: "b", itemId: "nothing", count: 1 }).code, "unknown_item", "없는 아이템");
    assertEqual(title.call("a", "GiftItem", { targetPlayerId: "b", itemId: "item.wood", count: 1 }).code, "not_enough_items", "모자람");
    title.seed("a", "item.wood", 1);
    assertEqual(title.call("a", "GiftItem", { targetPlayerId: "a", itemId: "item.wood", count: 1 }).code, "self_gift", "자기 자신");
    assertEqual(title.call("a", "GiftItem", { targetPlayerId: "b", itemId: "item.wood", count: 100 }).code, "bad_args", "100개");
    assertEqual(title.call("a", "GiftItem", { targetPlayerId: "zz", itemId: "item.wood", count: 1 }).code, "bad_args", "없는 사람");
});

test("건네기 — 하루 20건 한도(다음 날엔 풀림)·받는 쪽 대기 10건", () => {
    const title = giftTitle();
    title.account("c");
    title.seed("a", "item.wood", 30);
    for (let i = 0; i < 10; i++) assertEqual(title.call("a", "GiftItem", { targetPlayerId: "b", itemId: "item.wood", count: 1 }).sent, true, "보냄 " + i);
    assertEqual(title.call("a", "GiftItem", { targetPlayerId: "b", itemId: "item.wood", count: 1 }).code, "target_pending_full", "대기 가득");
    for (let i = 0; i < 10; i++) assertEqual(title.call("a", "GiftItem", { targetPlayerId: "c", itemId: "item.wood", count: 1 }).sent, true, "c 에게 " + i);
    assertEqual(title.call("a", "GiftItem", { targetPlayerId: "c", itemId: "item.wood", count: 1 }).code, "daily_limit", "하루 20건");
    assertEqual(title.count("a", "item.wood"), 10, "막힌 건 배낭 그대로");
    const day = title.getData("a", "giftDay");
    day.day -= 1;
    title.setData("a", "giftDay", day);
    title.call("c", "DeclineGift", { giftId: title.call("c", "GetGifts").gifts[0].id });
    assertEqual(title.call("a", "GiftItem", { targetPlayerId: "c", itemId: "item.wood", count: 1 }).sent, true, "날이 바뀌면 다시");
});

test("건네기 — 24시간 지나면 만료돼 보낸 사람에게 돌아가고 수락은 expired", () => {
    const title = giftTitle();
    title.seed("a", "item.wood", 2);
    const sent = title.call("a", "GiftItem", { targetPlayerId: "b", itemId: "item.wood", count: 2 });
    title.age("b", "gifts", 25 * 3600 * 1000);
    assertEqual(title.call("b", "AcceptGift", { giftId: sent.giftId }).code, "expired", "만료 수락");
    assertEqual([title.count("a", "item.wood"), title.count("b", "item.wood")], [2, 0], "돌아옴");
    // 받는 사람이 안 들어와도 보낸 사람이 목록을 열면 돌려받는다
    const again = title.call("a", "GiftItem", { targetPlayerId: "b", itemId: "item.wood", count: 2 });
    title.age("b", "gifts", 25 * 3600 * 1000);
    title.age("a", "giftsOut", 25 * 3600 * 1000);
    title.call("a", "GetGifts");
    assertEqual([title.count("a", "item.wood"), title.getData("a", "giftsOut").length, again.sent], [2, 0, true], "보낸 쪽이 돌려받음");
});

test("건네기 — 받는 사람 배낭이 가득이면 inventory_full, 선물은 대기에 남는다", () => {
    const title = giftTitle();
    title.seed("a", "item.wood", 1);
    const sent = title.call("a", "GiftItem", { targetPlayerId: "b", itemId: "item.wood", count: 1 });
    for (let i = 0; i < 20; i++) title.seed("b", "item.fill_" + i, 1);
    assertEqual(title.call("b", "AcceptGift", { giftId: sent.giftId }).code, "inventory_full", "가득");
    assertEqual([title.call("b", "GetGifts").gifts.length, title.count("b", "item.wood")], [1, 0], "대기에 남음");
});


// ── 신고·차단 (DESIGN 4.8 「신고 하루 상한」·「차단 목록 상한」) ──
test("신고 — 운영자 기록이 남고 같은 사람은 하루 한 번", () => {
    const title = giftTitle();
    const r = title.call("a", "ReportPlayer", { targetPlayerId: "b", reason: "name", note: "욕설 이름" });
    assertEqual([r.reported, r.reportedToday], [true, 1], "신고 성공");
    const ev = title.calls.events.filter((e) => e.name === "player_reported");
    assertEqual(ev.length, 1, "이벤트 하나");
    assertEqual([ev[0].body.reporterId, ev[0].body.targetId, ev[0].body.reason, ev[0].body.note], ["a", "b", "name", "욕설 이름"], "이벤트 몸");
    assertFailsClean(title, "a", "ReportPlayer", { targetPlayerId: "b", reason: "cheat" }, "already", "같은 날 두 번");
    assertEqual(title.calls.events.filter((e) => e.name === "player_reported").length, 1, "두 번째는 기록 없음");
});

test("신고 — 실패 코드: 자기 자신·없는 사람·잘못된 이유·긴 메모", () => {
    const title = giftTitle();
    const sb = title.sandbox;
    assertFailsClean(title, "a", "ReportPlayer", { targetPlayerId: "a", reason: "name" }, "self", "자기 자신");
    assertFailsClean(title, "a", "ReportPlayer", { targetPlayerId: "ghost", reason: "name" }, "bad_args", "없는 사람");
    assertFailsClean(title, "a", "ReportPlayer", { targetPlayerId: "b", reason: "mean" }, "bad_args", "없는 이유");
    assertFailsClean(title, "a", "ReportPlayer", { targetPlayerId: "b", reason: "other", note: "x".repeat(sb.REPORT_NOTE_MAX + 1) }, "bad_args", "긴 메모");
    assertEqual(title.calls.events.filter((e) => e.name === "player_reported").length, 0, "기록 없음");
});

test("신고 — 하루 상한은 DESIGN 값, 다음 날 다시 열린다", () => {
    const max = designNumber("신고 하루 상한 (`ReportPlayer`, `REPORT_DAILY_MAX`, 10-16 신고·차단)", /^(\d+)명/);
    const title = giftTitle();
    assertEqual(title.sandbox.REPORT_DAILY_MAX, max, "스크립트와 표");
    const day = Math.floor(Date.now() / 86400000);
    title.setData("a", "reportDay", { day, targets: Array.from({ length: max }, (_, i) => "x" + i) });
    assertFailsClean(title, "a", "ReportPlayer", { targetPlayerId: "b", reason: "harass" }, "daily_limit", "상한");
    title.setData("a", "reportDay", { day: day - 1, targets: Array.from({ length: max }, (_, i) => "x" + i) });
    assertEqual(title.call("a", "ReportPlayer", { targetPlayerId: "b", reason: "harass" }).reportedToday, 1, "다음 날");
});

test("차단 — 넣고 빼고 목록 받기, 자기 자신·상한·없는 사람", () => {
    const title = giftTitle();
    const sb = title.sandbox;
    assertEqual(sb.BLOCK_MAX, designNumber("차단 목록 상한 (`BlockPlayer`, `BLOCK_MAX`)", /^(\d+)명/), "스크립트와 표");
    assertEqual(title.call("a", "GetBlockList").blocks.length, 0, "처음 빔");
    const b = title.call("a", "BlockPlayer", { targetPlayerId: "b", name: "Bee" });
    assertEqual([b.blocked, b.blocks.length, b.blocks[0].name], [true, 1, "Bee"], "차단");
    assertEqual(title.call("a", "BlockPlayer", { targetPlayerId: "b" }).blocks.length, 1, "두 번 해도 한 줄");
    assertEqual(title.call("a", "GetBlockList").blocks[0].id, "b", "목록");
    assertEqual(title.call("b", "GetBlockList").blocks.length, 0, "상대 쪽은 그대로");
    assertFailsClean(title, "a", "BlockPlayer", { targetPlayerId: "a" }, "self", "자기 자신");
    assertFailsClean(title, "a", "BlockPlayer", { targetPlayerId: "ghost" }, "bad_args", "없는 사람");
    assertEqual(title.call("a", "UnblockPlayer", { targetPlayerId: "b" }).blocks.length, 0, "해제");
    assertFailsClean(title, "a", "UnblockPlayer", { targetPlayerId: "b" }, "not_found", "없는 줄 해제");
    title.setData("a", "blocks", Array.from({ length: sb.BLOCK_MAX }, (_, i) => ({ id: "x" + i, name: "", atUtc: "" })));
    assertFailsClean(title, "a", "BlockPlayer", { targetPlayerId: "b" }, "full", "가득");
});

test("차단 — 나를 차단한 사람에게는 선물을 못 보낸다", () => {
    const title = giftTitle();
    title.seed("a", "item.wood", 1);
    title.call("b", "BlockPlayer", { targetPlayerId: "a" });
    assertFailsClean(title, "a", "GiftItem", { targetPlayerId: "b", itemId: "item.wood", count: 1 }, "blocked", "차단당함");
    assertEqual(title.call("b", "GetGifts").gifts.length, 0, "대기 안 쌓임");
    title.call("b", "UnblockPlayer", { targetPlayerId: "a" });
    assertEqual(title.call("a", "GiftItem", { targetPlayerId: "b", itemId: "item.wood", count: 1 }).sent, true, "해제 뒤 다시");
});

test("신고·차단 — 처리 중 표시가 있으면 busy", () => {
    const title = giftTitle();
    title.setData("a", "lockReport", { token: "t", untilMs: Date.now() + 10000 });
    title.setData("a", "lockBlock", { token: "t", untilMs: Date.now() + 10000 });
    assertEqual(title.call("a", "ReportPlayer", { targetPlayerId: "b", reason: "name" }).code, "busy", "신고");
    assertEqual(title.call("a", "BlockPlayer", { targetPlayerId: "b" }).code, "busy", "차단");
    assertEqual(title.call("a", "UnblockPlayer", { targetPlayerId: "b" }).code, "busy", "해제");
});

// M109-01: 경제 장부(`docs/checks/M109_economy.md`) ↔ CloudScript 맞대기 — 한쪽만 고치면 운다.
// ① 재화 자리(더하기 = 들어옴 · 빼기·spendCoins = 나감)를 둘러싼 함수 목록이 장부 「핸들러」 칸과 같다
//    (새 소스·싱크가 장부 없이 생기면 운다) ② 칸에 적은 대문자 상수가 스크립트에 있다 ③ 숫자 칸 = 상수에서 셈한 값.
// 역검증용 사본은 `M109_LEDGER=<사본 경로>` 로 넘긴다.
const ledgerPath = process.env.M109_LEDGER
    ? path.resolve(process.env.M109_LEDGER)
    : path.join(repoRoot, "docs", "checks", "M109_economy.md");

function readLedgerRows() {
    const rows = [];
    fs.readFileSync(ledgerPath, "utf8").split("\n").forEach((line) => {
        const cells = line.split("|").slice(1, -1).map((c) => c.trim());
        if (cells.length !== 9 || !/^\d+$/.test(cells[0])) return;
        const [no, , handler, currency, constant, value, often, cap, play] = cells;
        rows.push({ no: Number(no), handler, currency, constant, value, often, cap, play, line });
    });
    if (rows.length === 0) throw new Error("장부 줄을 한 줄도 못 읽었다: " + ledgerPath);
    return rows;
}

/** 칸 속 숫자만(백틱 안 이름은 빼고) — 「5~40」 → [5, 40]. */
function ledgerNumbers(cell) {
    return (cell.replace(/`[^`]*`/g, " ").match(/\d+(?:\.\d+)?/g) || []).map(Number);
}

/** 재화 자리마다 [둘러싼 함수, +/-] — spendCoins 도우미 자신(정의·몸)은 빼고 부르는 자리를 센다. */
function currencySitesInScript() {
    let current = "";
    const sites = [];
    fs.readFileSync(scriptPath, "utf8").split("\n").forEach((line) => {
        const head = /^function (\w+)/.exec(line) || /^handlers\.(\w+)\s*=/.exec(line);
        if (head) current = head[1];
        if (current === "spendCoins") return;
        if (/AddUserVirtualCurrency\(/.test(line)) sites.push(current + " +");
        if (/SubtractUserVirtualCurrency\(|spendCoins\(/.test(line)) sites.push(current + " -");
    });
    return sites.sort();
}

test("M109-01 장부 맞대기 — 재화 자리 함수 목록이 장부 핸들러 칸과 같고, 칸의 상수가 스크립트에 있다", () => {
    const sb = makeTitle().sandbox;
    const rows = readLedgerRows();
    const sinkFrom = rows.findIndex((r) => r.line.indexOf("`RollGacha` 뽑기 값") !== -1);
    assertTrue(sinkFrom > 0, "나가는 길 첫 줄(#17 뽑기 값)을 찾음");
    const fromLedger = rows.map((r, i) => /^`(\w+)`/.exec(r.handler)[1] + (i < sinkFrom ? " +" : " -")).sort();
    assertEqual(fromLedger, currencySitesInScript(), "장부 핸들러(들어옴 +·나감 -) = 스크립트 재화 자리");
    rows.forEach((r) => {
        (r.line.match(/`[A-Z][A-Z0-9]*_[A-Z0-9_]+/g) || []).forEach((name) => {
            assertTrue(sb[name.slice(1)] !== undefined, "#" + r.no + " 칸의 상수 " + name.slice(1) + " 가 스크립트에 있음");
        });
    });
});

test("M109-01 장부 맞대기 — 값·빈도·상한 칸 숫자 = CloudScript 상수에서 셈한 값", () => {
    const sb = makeTitle().sandbox;
    const vals = (o) => Object.keys(o).map((k) => o[k]);
    const range = (a) => [Math.min(...a), Math.max(...a)];
    const sum = (a) => a.reduce((x, y) => x + y, 0);
    const chain = sb.QUEST_CHAIN.map((q) => q.coins);
    const attendance = sb.ATTENDANCE_REWARDS.filter((r) => r.coins).map((r) => r.coins);
    const expand = Array.from({ length: sb.RANCH_SLOT_MAX - sb.RANCH_SLOT_COUNT }, (_, k) => sb.RANCH_EXPAND_COIN_STEP * (k + 1));
    // 고구마 기준가·배율은 함수 속 범위라 여러 주를 굴려 양 끝을 잰다(배율 위 끝은 소수 한 자리 올림).
    const bases = [];
    const rates = [];
    for (let w = 0; w < 3000; w++) {
        const week = sb.sweetPotatoWeek("p" + (w % 7), w);
        bases.push(week.base);
        rates.push(...week.rates);
    }
    const potatoBase = range(bases);
    const potatoRate = [Math.min(...rates), Math.ceil(Math.max(...rates) * 10) / 10];
    const expected = {
        1: { value: [sb.STARTER_COINS] },
        2: { value: [sb.TESTER_KIT_COINS] },
        3: { value: [sb.COIN_PER_UNIT], cap: [sb.HARVEST_CAP_UNITS, sb.RANCH_SLOT_COUNT, sb.RANCH_SLOT_MAX, sb.HARVEST_DAILY_COINS] },
        4: { value: range(vals(sb.CROP_HARVEST_COINS)), often: range(vals(sb.CROP_CYCLE_MINUTES)) },
        5: { value: range(vals(sb.FISH_TABLE).map((f) => f.coinPerUnit)), cap: [sb.FISHING_CAP_UNITS, sb.FISHING_SLOT_COUNT, sb.HARVEST_DAILY_COINS] },
        6: { value: vals(sb.GACHA_COIN_PRIZES) },
        7: { value: vals(sb.GRADE_DUP_REFUND) },
        8: {
            value: [sb.ACHIEVEMENTS.length, sum(sb.ACHIEVEMENTS.map((a) => a.coins)), ...range(sb.ACHIEVEMENTS.map((a) => a.coins))],
            cap: [sum(sb.ACHIEVEMENTS.map((a) => a.coins))]
        },
        9: { value: [sb.FRUIT_PRICE_NATIVE, sb.FRUIT_PRICE_OTHER, sb.SELL_PRICE.pet, 1], cap: [sb.LOOT_SELL_DAILY_COINS] }, // 1 = sellPriceOf 「재료 값 합 + 1」
        10: { value: [...potatoBase, ...potatoRate, 1], often: [sb.POTATO_SLOTS / 6], cap: [sb.POTATO_WEEKLY_CAP] }, // 1 = 「1 이상」
        11: { value: [sb.QUEST_REWARD_MUL, sb.QUEST_UNSELLABLE_COIN], cap: [sb.QUEST_PER_DAY] },
        12: { value: [...chain, sum(chain)] },
        13: { value: [...chain, sum(chain)] },
        14: { value: [...sb.STORY_CHAPTER_COINS, sum(sb.STORY_CHAPTER_COINS)], cap: [sum(sb.STORY_CHAPTER_COINS)] },
        15: { value: [sb.ATTENDANCE_REWARDS.length, ...attendance, sum(attendance), sb.ATTENDANCE_REWARDS.length, sb.ATTENDANCE_HAT_REPEAT_COINS] },
        16: { value: [sb.BOSS_HAT_REPEAT_COINS, sb.BOSS_HAT_CHANCE] },
        17: { value: [sb.SINGLE_COST.GM, sb.SINGLE_COST.SC, sb.MULTI_ROLL_COUNT, sb.MULTI_COST_GM] },
        18: { value: [sb.RANCH_EXPAND_COIN_STEP, ...expand, sum(expand)], cap: [expand.length, sb.RANCH_SLOT_COUNT, sb.RANCH_SLOT_MAX] },
        19: {
            constant: [Object.keys(sb.WARDROBE_SHOP_POOL).length], value: range(vals(sb.WARDROBE_SHOP_POOL)),
            often: [sb.WARDROBE_SHOP_SLOTS]
        },
        20: { value: [sb.BLUEPRINT_PRICE], cap: [sb.BLUEPRINT_IDS.length, sb.BLUEPRINT_IDS.length * sb.BLUEPRINT_PRICE] },
        21: {
            constant: [Object.keys(sb.SHOP_ITEMS).length, sb.SELL_PRICE.cropDivisor],
            value: [...range(vals(sb.SHOP_ITEMS).map((i) => i.price)), sb.CRATE_KEY_GEM_PRICE]
        },
        22: { value: [...potatoBase, sb.POTATO_BUNDLE], cap: [sb.POTATO_WEEKLY_CAP, sb.POTATO_WEEKLY_CAP * potatoBase[1]] },
        23: { value: [sb.FERRY_FARES.hunt, sb.FERRY_FARES.danger, sb.FERRY_FARES.island, sb.FERRY_FARES.base] },
        24: { value: [] }
    };
    const rows = readLedgerRows();
    assertEqual(rows.map((r) => r.no), Object.keys(expected).map(Number), "장부 줄 번호 = 맞대는 줄");
    rows.forEach((r) => {
        Object.keys(expected[r.no]).forEach((cell) => {
            assertEqual(ledgerNumbers(r[cell]), expected[r.no][cell], "#" + r.no + " " + cell + " 칸");
        });
    });
});

// M109-02: 하루 모의 셈(`economy_sim.js`) — 상수는 검사 중인 스크립트의 것을 쓴다(역검증 사본도 그대로 탄다).
const economySim = require("./economy_sim.js");

test("M109-02 하루 모의 셈 — 같은 입력 같은 출력", () => {
    const first = economySim.summarize(makeTitle().sandbox, 7);
    const second = economySim.summarize(makeTitle().sandbox, 7);
    assertEqual(second, first, "다른 샌드박스에서 두 번 굴린 7일 결과");
    assertEqual(economySim.formatTable(second, 7), economySim.formatTable(first, 7), "출력 표 글자");
});

test("M109-02 하루 모의 셈 — 형마다 칸 빈 곳 없음 · 들어옴 − 나감 = 남음", () => {
    const sb = makeTitle().sandbox;
    const days = 7;
    const summary = economySim.summarize(sb, days);
    assertEqual(summary.map((s) => s.key), Object.keys(economySim.PROFILES), "형 줄 = PROFILES");
    const nonNegative = (v) => typeof v === "number" && Number.isFinite(v) && v >= 0;
    summary.forEach((s) => {
        ["day1In", "day1Out", "earned", "spent", "left", "ratio", "slotCap", "fame", "gachaPulls"].forEach((cell) => {
            assertTrue(nonNegative(s[cell]), s.label + " " + cell + " 칸 = 0 이상 유한 수(" + s[cell] + ")");
        });
        economySim.SOURCE_KEYS.forEach((k) => assertTrue(Number.isInteger(s.inTotal[k]) && s.inTotal[k] >= 0, s.label + " 들어옴 " + k));
        economySim.SINK_KEYS.forEach((k) => assertTrue(Number.isInteger(s.outTotal[k]) && s.outTotal[k] >= 0, s.label + " 나감 " + k));
        assertEqual(s.earned - s.spent, s.left, s.label + " 들어옴 − 나감 = 남음");
        assertTrue(s.earned > 0 && s.spent > 0, s.label + " 벌이·씀 둘 다 있음");
        assertTrue(s.slotCap >= sb.RANCH_SLOT_COUNT && s.slotCap <= sb.RANCH_SLOT_MAX, s.label + " 목장 칸 범위");
    });
    economySim.simulate(sb, economySim.PROFILES.heavy, days).forEach((r) => {
        assertTrue(r.coins >= 0, "열심 " + r.day + "일 잔액이 음수 아님");
        assertTrue(r.src.hunt <= sb.LOOT_SELL_DAILY_COINS, "열심 " + r.day + "일 사냥 판매 ≤ 하루 상한");
        assertTrue(r.src.ranch + r.src.fishing <= sb.HARVEST_DAILY_COINS, "열심 " + r.day + "일 동물·낚시터 수확 코인 ≤ 하루 상한(M109-04a)");
    });
    const table = economySim.formatTable(summary, days);
    assertTrue(!/NaN|undefined|\|[ \t]*\|/.test(table), "출력 표에 빈 칸·NaN·undefined 없음");
});

// ── 실행 ───────────────────────────────────────────────────────────────────────
let passed = 0;
const failures = [];
tests.forEach((item) => {
    try {
        item.body();
        passed += 1;
        console.log("  통과 · " + item.name);
    } catch (error) {
        failures.push({ name: item.name, message: error.message });
        console.log("  실패 · " + item.name + " — " + error.message);
    }
});

console.log("검사 대상: " + path.relative(repoRoot, scriptPath));
console.log("검사 결과: " + tests.length + "개 중 " + passed + "개 통과 · 실패 " + failures.length + "개");
process.exit(failures.length === 0 ? 0 : 1);
