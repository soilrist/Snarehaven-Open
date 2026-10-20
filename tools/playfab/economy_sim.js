// M109-02 하루 모의 셈 — 서버 호출 없는 순수 셈 (ROADMAP M109-02, 장부 `docs/checks/M109_economy.md`).
//
// **왜 상수를 스크립트에서 읽나:** 값의 원본은 서버 상수다(CLAUDE.md 8). 여기에 숫자를 옮겨 적으면
// 서버만 고치고 셈은 옛 값으로 돈다 — 그래서 `cloudscript_main.js` 를 가짜 샌드박스에서 읽어 상수·
// `sellPriceOf`·`questReward` 를 그대로 쓴다. 이 파일이 정하는 숫자는 아래 PROFILES(플레이 형) 뿐이다.
//
// 돌리기: `node tools/playfab/economy_sim.js [날 수=7] [검사할_스크립트_경로]`
// 시험: `test_cloudscript.js` 의 「M109-02」 두 묶음(같은 입력 같은 출력 · 형마다 칸 빈 곳 없음).
//
// 셈에 넣지 않는 길(장부 번호): 뽑기 경품 #6·7(뽑기 값 #17 을 낸 뒤에만 — 남는 코인으로 몇 번 뽑나만 적는다)
// · 고구마 #10·22(사고파는 시세 놀이 — 주마다 오르내림) · 우두머리 모자 되풀이 #16(확률 0.1, 모자 뒤에만)
// · 옷장 #19(젬) · 놀이 밖 #2·24.
"use strict";

const fs = require("fs");
const path = require("path");
const vm = require("vm");

const repoRoot = path.resolve(__dirname, "..", "..");
const defaultScriptPath = path.join(repoRoot, "tools", "playfab", "cloudscript_main.js");

/** CloudScript 를 빈 `server` 샌드박스에서 읽는다 — 위 단계 코드는 상수·표만 세우고 서버를 부르지 않는다. */
function loadServerConstants(scriptPath) {
    const sandbox = { handlers: {}, server: {}, currentPlayerId: "", log: { info() {}, debug() {}, error() {} }, console };
    vm.createContext(sandbox);
    vm.runInContext(fs.readFileSync(scriptPath || defaultScriptPath, "utf8"), sandbox, { filename: scriptPath || defaultScriptPath });
    return sandbox;
}

// 🟡 플레이 형 — 하루 접속 횟수·하는 일은 M106(첫 30분: 사냥 → 포획 → 목장 → 수확 → 의뢰 → 줄기 → 상점)·
// M107(두 번째 30분: 제작 · 낚시 · 배 · 존 이동 · 칸 확장) 동선 표 순서에서 왔다. 가벼운 형은 M106 동선만,
// 보통은 M107 까지, 열심은 둘 다 날마다 되풀이. 접속은 하루에 고르게 나눈다고 본다(목장 칸이 차는 쪽으로 — 벌이 위 끝).
const PROFILES = {
    light: {
        label: "가벼운 15분", minutes: 15, visits: 2,
        petsPerDay: 1, cropSlots: 0, crop: "item.wheat", fishSlots: 0, fish: "item.carp",
        huntMinutes: 5, huntTrips: 0, quests: 1, cookBuys: 0,
        storyChapters: 1, achievementShare: 0.2,
        tools: ["item.hatchet", "item.gather_basket"], blueprints: 0
    },
    normal: {
        label: "보통 1시간", minutes: 60, visits: 4,
        petsPerDay: 2, cropSlots: 1, crop: "item.wheat", fishSlots: 2, fish: "item.carp",
        huntMinutes: 20, huntTrips: 1, quests: 3, cookBuys: 3,
        storyChapters: 3, achievementShare: 0.5,
        tools: ["item.hatchet", "item.gather_basket", "item.rod_basic", "item.tent", "item.hiking_backpack"], blueprints: 2
    },
    heavy: {
        label: "열심 3시간", minutes: 180, visits: 6,
        petsPerDay: 3, cropSlots: 2, crop: "item.wheat", fishSlots: 5, fish: "item.carp",
        huntMinutes: 90, huntTrips: 3, quests: 3, cookBuys: 6,
        storyChapters: 5, achievementShare: 1,
        tools: ["item.hatchet", "item.gather_basket", "item.rod_basic", "item.tent", "item.hiking_backpack",
            "item.lifeboat", "item.rod_telescopic"], blueprints: 4
    }
};

// 🟡 사냥 한 마리 = 고기 2 · 가죽 1(장부 #9 `SELL_PRICE` 값), 사냥하는 동안 2분에 한 마리.
const HUNT_KILLS_PER_MINUTE = 0.5;
const HUNT_DROPS = { meat: 2, hide: 1 };
// 🟡 목장 펫은 첫 포획(M106 4행 염소) — 생산물 우유. 요리 재료는 잡화점 각 1(소금).
const RANCH_ANIMAL = "animal.goat";
const COOK_ITEM = "item.salt";

const SOURCE_KEYS = ["starter", "attendance", "ranch", "ranchSell", "crop", "cropSell", "fishing", "fishSell",
    "hunt", "quests", "chain", "story", "achievements"];
const SINK_KEYS = ["tools", "seeds", "cooking", "ferry", "blueprints", "expand"];

function zeroes(keys) {
    const out = {};
    keys.forEach((k) => { out[k] = 0; });
    return out;
}

/** 합 total 을 days 날에 고르게(내림, 끝날에 나머지) — d 는 1부터. */
function spread(total, d, days) {
    return Math.floor(total * d / days) - Math.floor(total * (d - 1) / days);
}

function shopPrice(sb, itemId) {
    const line = sb.SHOP_ITEMS[itemId];
    if (!line) throw new Error("상점에 없는 줄: " + itemId);
    return line.price;
}

/** 한 형을 days 날 굴린다. 날마다 [들어옴 칸별, 나감 칸별, 잔액, 목장 칸, 명성] — Math.random 없음(같은 입력 같은 출력). */
function simulate(sb, profile, days) {
    const gap = 1440 / profile.visits;                     // 접속 사이 분
    const animalUnitsPerVisit = Math.min(sb.HARVEST_CAP_UNITS, Math.floor(gap / sb.HARVEST_INTERVAL_MINUTES));
    const fishInfo = sb.FISH_TABLE[profile.fish];
    const fishUnitsPerVisit = Math.min(sb.FISHING_CAP_UNITS, Math.floor(gap / fishInfo.intervalMinutes));
    const cropRipe = gap >= sb.CROP_CYCLE_MINUTES[profile.crop];
    const product = sb.ANIMAL_PRODUCT_ITEM[RANCH_ANIMAL];
    const killValue = HUNT_DROPS.meat * sb.SELL_PRICE.meat + HUNT_DROPS.hide * sb.SELL_PRICE.hide;
    const pool = sb.QUEST_POOL.map((q) => sb.questReward(q[0], q[1]));
    const questAverage = pool.reduce((a, b) => a + b, 0) / pool.length;
    const chainCoins = sb.QUEST_CHAIN.map((q) => q.coins);
    const achievementTotal = Math.round(sb.ACHIEVEMENTS.reduce((a, x) => a + x.coins, 0) * profile.achievementShare);
    const chapterDay = sb.STORY_CHAPTER_COINS.slice(0, profile.storyChapters)
        .map((coins, i) => ({ coins, day: Math.max(1, Math.round((i + 1) * days / profile.storyChapters)) }));

    let coins = 0;
    let fame = 0;
    let slotCap = sb.RANCH_SLOT_COUNT;
    let pets = 0;            // 전날까지 칸에 있던 펫
    let fishPlaced = 0;
    let questCarry = 0;      // 의뢰 평균의 소수 몫 — 날마다 정수 코인만 준다
    let blueprintsOwned = 0;
    const rows = [];

    for (let d = 1; d <= days; d++) {
        const src = zeroes(SOURCE_KEYS);
        const sink = zeroes(SINK_KEYS);

        if (d === 1) src.starter = sb.STARTER_COINS;
        src.attendance = sb.ATTENDANCE_REWARDS[(d - 1) % sb.ATTENDANCE_REWARDS.length].coins || 0;

        // 목장: 오늘 첫 접속에 새 펫을 넣고, 넣은 날은 접속 한 번 덜 거둔다. 작물 칸이 먼저 칸을 잡는다.
        const animalRoom = Math.max(0, slotCap - profile.cropSlots);
        const newPets = Math.min(profile.petsPerDay, animalRoom - pets);
        const animalUnits = pets * profile.visits * animalUnitsPerVisit + newPets * (profile.visits - 1) * animalUnitsPerVisit;
        pets += newPets;
        src.ranch = animalUnits * sb.COIN_PER_UNIT;
        src.ranchSell = animalUnits * sb.sellPriceOf(product);
        fame += animalUnits * sb.RANCH_FAME_PER_UNIT;

        // 밭: 접속마다 한 번 거두고 씨앗을 다시 산다(첫날은 심는 접속을 뺀다).
        const cropHarvests = cropRipe ? profile.cropSlots * (d === 1 ? profile.visits - 1 : profile.visits) : 0;
        src.crop = cropHarvests * sb.CROP_HARVEST_COINS[profile.crop];
        src.cropSell = cropHarvests * (sb.CROP_HARVEST_COUNT[profile.crop] || 1) * sb.sellPriceOf(profile.crop);
        fame += cropHarvests * sb.RANCH_FAME_PER_UNIT;
        const seedBuys = profile.cropSlots > 0 ? cropHarvests + (d === 1 ? profile.cropSlots : 0) : 0;

        // 낚시터: 낚싯대를 산 다음 날부터 칸을 채운다(넣은 날은 접속 한 번 덜).
        const fishUnits = d === 1 ? 0 : (fishPlaced * profile.visits + (profile.fishSlots - fishPlaced) * (profile.visits - 1)) * fishUnitsPerVisit;
        if (d > 1) fishPlaced = profile.fishSlots;
        src.fishing = fishUnits * fishInfo.coinPerUnit;
        src.fishSell = fishUnits * sb.sellPriceOf(profile.fish);
        // M109-04a 동물·낚시터 수확 코인은 합쳐 하루 상한 — 동물 칸을 먼저 거둔다고 본다(생산물 판매는 그대로).
        src.ranch = Math.min(src.ranch, sb.HARVEST_DAILY_COINS);
        src.fishing = Math.min(src.fishing, sb.HARVEST_DAILY_COINS - src.ranch);

        src.hunt = Math.min(sb.LOOT_SELL_DAILY_COINS, Math.floor(profile.huntMinutes * HUNT_KILLS_PER_MINUTE) * killValue);

        const questExact = Math.min(profile.quests, sb.QUEST_PER_DAY) * questAverage + questCarry;
        src.quests = Math.floor(questExact);
        questCarry = questExact - src.quests;

        if (d <= chainCoins.length) src.chain = chainCoins[d - 1];
        chapterDay.forEach((c) => { if (c.day === d) src.story += c.coins; });
        src.achievements = spread(achievementTotal, d, days);

        coins += SOURCE_KEYS.reduce((a, k) => a + src[k], 0);

        // 나감 — 필수품부터, 살 수 있을 때만(코인이 음수로 가지 않는다).
        const pay = (key, amount) => {
            if (amount <= 0 || coins < amount) return false;
            coins -= amount;
            sink[key] += amount;
            return true;
        };
        if (d === 1) profile.tools.forEach((id) => pay("tools", shopPrice(sb, id)));
        pay("seeds", seedBuys * shopPrice(sb, sb.seedOf(profile.crop)));
        pay("cooking", profile.cookBuys * shopPrice(sb, COOK_ITEM));
        pay("ferry", profile.huntTrips * sb.FERRY_FARES.hunt);
        while (blueprintsOwned < profile.blueprints && pay("blueprints", sb.BLUEPRINT_PRICE)) blueprintsOwned += 1;
        while (slotCap < sb.RANCH_SLOT_MAX) {
            const cost = sb.ranchExpandCost(slotCap);
            if (fame < cost.fame || !pay("expand", cost.coins)) break;
            fame -= cost.fame;
            slotCap += 1;
        }

        rows.push({ day: d, src, sink, coins, slotCap, fame });
    }
    return rows;
}

function sumRows(rows, part, keys) {
    const out = zeroes(keys);
    rows.forEach((r) => keys.forEach((k) => { out[k] += r[part][k]; }));
    return out;
}

/** 형마다 한 줄씩 묶은 결과 — 시험과 표가 같은 값을 본다. */
function summarize(sb, days) {
    return Object.keys(PROFILES).map((key) => {
        const rows = simulate(sb, PROFILES[key], days);
        const inTotal = sumRows(rows, "src", SOURCE_KEYS);
        const outTotal = sumRows(rows, "sink", SINK_KEYS);
        const earned = SOURCE_KEYS.reduce((a, k) => a + inTotal[k], 0);
        const spent = SINK_KEYS.reduce((a, k) => a + outTotal[k], 0);
        const day1 = rows[0];
        const last = rows[rows.length - 1];
        return {
            key, label: PROFILES[key].label,
            day1In: SOURCE_KEYS.reduce((a, k) => a + day1.src[k], 0),
            day1Out: SINK_KEYS.reduce((a, k) => a + day1.sink[k], 0),
            earned, spent, left: last.coins, slotCap: last.slotCap, fame: last.fame,
            ratio: spent > 0 ? earned / spent : 0,
            gachaPulls: Math.floor(last.coins / sb.SINGLE_COST.SC),
            inTotal, outTotal
        };
    });
}

function formatTable(summary, days) {
    const lines = [];
    lines.push("| 형 | 첫날 들어옴 | 첫날 나감 | " + days + "일 들어옴 | " + days + "일 나감 | " + days + "일 뒤 남음 | 들어옴÷나감 | 목장 칸 | 남은 명성 | 남은 코인으로 뽑기 |");
    lines.push("|---|---|---|---|---|---|---|---|---|---|");
    summary.forEach((s) => {
        lines.push("| " + [s.label, s.day1In, s.day1Out, s.earned, s.spent, s.left, s.ratio.toFixed(2), s.slotCap, s.fame, s.gachaPulls].join(" | ") + " |");
    });
    lines.push("");
    lines.push("| 형 | " + SOURCE_KEYS.join(" | ") + " | " + SINK_KEYS.map((k) => "-" + k).join(" | ") + " |");
    lines.push("|---|" + SOURCE_KEYS.concat(SINK_KEYS).map(() => "---").join("|") + "|");
    summary.forEach((s) => {
        lines.push("| " + [s.label].concat(SOURCE_KEYS.map((k) => s.inTotal[k]), SINK_KEYS.map((k) => s.outTotal[k])).join(" | ") + " |");
    });
    return lines.join("\n");
}

module.exports = { loadServerConstants, PROFILES, SOURCE_KEYS, SINK_KEYS, simulate, summarize, formatTable };

if (require.main === module) {
    const days = Number(process.argv[2] || 7);
    const sb = loadServerConstants(process.argv[3] ? path.resolve(process.argv[3]) : defaultScriptPath);
    console.log(formatTable(summarize(sb, days), days));
}
