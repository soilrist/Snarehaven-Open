"""게임 소리 원본 wav 를 `Saved/Generated/Audio/` 에 만든다 (감독 2026-09-25 · DESIGN 2.2 소리).

  python3 tools/audio/make_sounds.py        # numpy · ffmpeg 필요

두 갈래다.
1. Kenney CC0 팩(`_Source/raw/audio/kenney/`, 라이선스 CC0 — `_Source/LICENSES.md`) 의 ogg 를 골라 wav 로 바꾼다.
   발소리·나무 찍기·광석·타격·UI·코인.
2. 녹음 원본이 없는 자연 소리는 여기서 합성한다 — 파도·바람(반복), 새 지저귐·헤엄·첨벙·휘두르기(한 번).
   레퍼런스 소리 풍경(해변 파도 + 숲 새소리 + 잔바람)을 따른다.

이름은 `SFX_<이름>_<두 자리>` — 코드(`SnareSoundSubsystem`)가 `_01` 부터 없을 때까지 읽어 무작위로 고른다.
재실행 안전(같은 씨앗이라 같은 파일).
"""
import os
import subprocess
import wave

import numpy as np

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
KENNEY = os.path.abspath(os.path.join(ROOT, "..", "..", "..", "_Source", "raw", "audio", "kenney"))
if not os.path.isdir(KENNEY):   # 워크트리가 아니면 저장소 옆
    KENNEY = os.path.join(ROOT, "_Source", "raw", "audio", "kenney")
OUT = os.path.join(ROOT, "Saved", "Generated", "Audio")
RATE = 44100
rng = np.random.default_rng(20260925)

# Kenney 원본 → 게임 이름 (팩 폴더, 파일 이름 앞부분)
KENNEY_MAP = {
    "Footstep_Grass": ("kenney_impact-sounds", "footstep_grass_"),
    "Footstep_Wood": ("kenney_impact-sounds", "footstep_wood_"),
    "Footstep_Snow": ("kenney_impact-sounds", "footstep_snow_"),
    "Chop": ("kenney_impact-sounds", "impactWood_medium_"),
    "Mine": ("kenney_impact-sounds", "impactMining_"),
    "Hit": ("kenney_impact-sounds", "impactPunch_medium_"),
    "Ui_Click": ("kenney_interface-sounds", "click_"),
    "Ui_Open": ("kenney_interface-sounds", "open_"),
    "Ui_Close": ("kenney_interface-sounds", "close_"),
    "Pickup": ("kenney_interface-sounds", "pluck_"),
    "Coins": ("kenney_rpg-audio", "handleCoins"),
}


def write_wav(name, samples):
    samples = np.clip(samples, -1.0, 1.0)
    path = os.path.join(OUT, name + ".wav")
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes((samples * 32767).astype("<i2").tobytes())
    return path


def normalize(x, peak=0.8):
    m = np.max(np.abs(x))
    return x * (peak / m) if m > 0 else x


def one_pole_lowpass(x, cutoff):
    """cutoff 가 배열이면 샘플마다 바뀌는 저역 통과 — 파도가 부서질 때 밝아지는 데 쓴다."""
    cutoff = np.broadcast_to(np.asarray(cutoff, dtype=float), x.shape)
    a = np.exp(-2.0 * np.pi * cutoff / RATE)
    y = np.empty_like(x)
    acc = 0.0
    for i in range(len(x)):   # ponytail: 파이썬 루프 — 몇 초짜리라 충분, 길어지면 scipy.signal
        acc = (1.0 - a[i]) * x[i] + a[i] * acc
        y[i] = acc
    return y


def pink_noise(n):
    spec = np.fft.rfft(rng.standard_normal(n))
    f = np.fft.rfftfreq(n, 1.0 / RATE)
    f[0] = f[1]
    return normalize(np.fft.irfft(spec / np.sqrt(f), n))


def bandpass_fft(x, lo, hi):
    spec = np.fft.rfft(x)
    f = np.fft.rfftfreq(len(x), 1.0 / RATE)
    spec[(f < lo) | (f > hi)] = 0
    return np.fft.irfft(spec, len(x))


def seamless(x, fade_s=2.0):
    """끝 fade_s 초를 앞머리에 겹쳐 이음매 없는 반복으로 만든다."""
    n = int(fade_s * RATE)
    head, body, tail = x[:n], x[n:-n], x[-n:]
    t = np.linspace(0, 1, n)
    mixed = tail * np.cos(t * np.pi / 2) + head * np.sin(t * np.pi / 2)
    return np.concatenate([mixed, body, tail[:0]]) if len(body) else mixed


def make_ocean(seconds=26.0):
    # 파도 한 번 = 밀려옴(1.5~2.5초 커짐) → 부서짐(밝은 쏴아) → 빠짐(3~5초 잦아듦). 간격 5.5~8초.
    n = int((seconds + 2.0) * RATE)
    base = pink_noise(n)
    env = np.full(n, 0.18)
    bright = np.full(n, 350.0)
    t = 0.3
    while t < seconds + 2.0:
        rise, fall = rng.uniform(1.5, 2.5), rng.uniform(3.0, 5.0)
        amp = rng.uniform(0.7, 1.0)
        s, peak = int(t * RATE), int((t + rise) * RATE)
        e = min(n, int((t + rise + fall) * RATE))
        if peak < n:
            up = np.linspace(0, 1, peak - s) ** 2
            env[s:peak] += amp * up
            bright[s:peak] += 1800 * up
            down = np.exp(-np.linspace(0, 4, e - peak))
            env[peak:e] += amp * down
            bright[peak:e] += 2600 * down
        t += rng.uniform(5.5, 8.0)
    body = one_pole_lowpass(base, np.minimum(bright, 4500)) * env
    hiss = bandpass_fft(pink_noise(n), 2500, 9000) * np.clip(env - 0.5, 0, None) * 0.35
    return normalize(seamless(body + hiss), 0.7)


def make_wind(seconds=20.0):
    n = int((seconds + 2.0) * RATE)
    slow = np.interp(np.arange(n), np.linspace(0, n, 8), rng.uniform(0.35, 1.0, 8))
    gust = one_pole_lowpass(pink_noise(n), 220 + 380 * slow) * slow
    whistle = bandpass_fft(pink_noise(n), 700, 1300) * slow ** 3 * 0.15
    return normalize(seamless(gust + whistle), 0.5)


def chirp(duration, f0, f1, curve=1.0, harm=0.15):
    t = np.linspace(0, 1, int(duration * RATE))
    freq = f0 + (f1 - f0) * t ** curve
    freq = freq * (1 + 0.04 * np.sin(2 * np.pi * rng.uniform(25, 60) * t * duration))   # 떨림 — 삑 소리가 새소리로
    phase = 2 * np.pi * np.cumsum(freq) / RATE
    tone = np.sin(phase) + harm * np.sin(2 * phase)
    env = np.sin(np.pi * t) ** 1.5
    return tone * env


def make_bird():
    # 한 소절 = 짧은 휘파람 음절 3~8개. 음절마다 2.2~6kHz 에서 오르거나 내린다(박새·되새류 느낌).
    parts = []
    kind = rng.integers(3)
    syllables = rng.integers(3, 9)
    base = rng.uniform(2600, 4200)
    for i in range(syllables):
        if kind == 0:      # 같은 음절 되풀이 "찌찌찌"
            parts.append(chirp(0.07, base * 1.3, base * 0.8, 0.7))
        elif kind == 1:    # 내려가는 트릴
            f = base * (1.25 - i * 0.05)
            parts.append(chirp(0.09, f * 1.2, f, 0.5))
        else:              # 오르내리는 노래
            f = base * rng.uniform(0.8, 1.35)
            parts.append(chirp(rng.uniform(0.06, 0.14), f, f * rng.uniform(0.7, 1.4), rng.uniform(0.5, 2.0)))
        parts.append(np.zeros(int(rng.uniform(0.03, 0.09) * RATE)))
    return normalize(np.concatenate(parts), 0.55)


def burst(duration, lo, hi, attack=0.01, decay=6.0):
    n = int(duration * RATE)
    t = np.linspace(0, duration, n)
    env = np.minimum(t / attack, 1.0) * np.exp(-decay * t)
    return bandpass_fft(rng.standard_normal(n), lo, hi) * env


def make_splash():
    x = burst(0.7, 300, 6000, 0.005, 7.0)
    for _ in range(12):   # 물방울 톡톡
        s = int(rng.uniform(0.05, 0.5) * RATE)
        d = chirp(0.04, rng.uniform(900, 1800), rng.uniform(1800, 3000), 0.5, 0)
        x[s:s + len(d)] += d * 0.15
    return normalize(x, 0.7)


def make_swim():
    x = burst(0.45, 250, 3500, 0.06, 5.0)
    return normalize(x, 0.5)


def make_swing():
    n = int(0.28 * RATE)
    t = np.linspace(0, 1, n)
    env = np.sin(np.pi * t) ** 2
    x = one_pole_lowpass(rng.standard_normal(n), 600 + 2400 * np.sin(np.pi * t)) * env
    return normalize(x, 0.5)


def loop_tone(seconds, fund, harmonics, wobble_hz=0.0, wobble_depth=0.0):
    """정수 주기만 담아 끝=처음이 이어지는 톤 — fund·wobble_hz 는 1/seconds 의 배수여야 한다."""
    t = np.arange(int(seconds * RATE)) / RATE
    x = sum(a * np.sin(2 * np.pi * fund * h * t + h) for h, a in harmonics)
    return x * (1 + wobble_depth * np.sin(2 * np.pi * wobble_hz * t)) if wobble_hz else x


def make_engine(pulse_hz, noise_hi, seconds=2.0):
    # 2행정 엔진 = 점화 펄스 열(톱니 배음 + 펄스마다 튀는 잡음). FFT 필터라 이음매가 없다.
    n = int(seconds * RATE)
    t = np.arange(n) / RATE
    saw = sum(np.sin(2 * np.pi * pulse_hz * h * t) / h for h in range(1, 16))
    pulses = np.maximum(0.0, np.sin(2 * np.pi * pulse_hz * t)) ** 6
    grit = bandpass_fft(rng.standard_normal(n), 300, noise_hi) * (0.3 + pulses)
    wobble = 1 + 0.12 * np.sin(2 * np.pi * (3.0 / seconds) * 2 * t)   # 회전이 살짝 출렁인다
    return normalize((0.6 * saw + 0.9 * grit) * wobble, 0.7)


def make_radio(seconds=4.0):
    # M108-08d-2 라디오 가락 — 우리가 지은 5음 음계 짧은 가락(실제 노래·방송 아님). 박 0.25초 × 16 = 4초,
    # 퉁기는 소리(감쇠 사인 + 배음) 열 + 4박마다 낮은 뿌리음. 꼬리는 앞머리로 감아 붙여 이음매가 없고,
    # 작은 스피커처럼 300~3500Hz 만 남긴 뒤(FFT 라 원형) 옅은 지직임을 얹는다.
    n = int(seconds * RATE)
    out = np.zeros(n)
    beat = seconds / 16
    pent = [261.63, 293.66, 329.63, 392.00, 440.00, 523.25, 587.33]   # 다 라 마 솔 라 높은 다 레
    melody = [4, None, 3, 2, 3, None, 5, None, 4, 3, 1, None, 2, None, 0, None]
    roots = [130.81, 98.00, 110.00, 98.00]   # 낮은 다 · 솔 · 라 · 솔

    def pluck(freq, length, decay, harm):
        t = np.arange(int(length * RATE)) / RATE
        x = sum(a * np.sin(2 * np.pi * freq * h * t) for h, a in harm)
        return x * np.exp(-decay * t) * np.minimum(1.0, t / 0.004)

    def add(start_s, x):
        idx = (int(start_s * RATE) + np.arange(len(x))) % n
        np.add.at(out, idx, x)

    for i, note in enumerate(melody):
        if note is not None:
            add(i * beat, 0.8 * pluck(pent[note], 0.9, 5.5, [(1, 1.0), (2, 0.45), (3, 0.2), (4, 0.08)]))
    for i, root in enumerate(roots):
        add(i * 4 * beat, 0.6 * pluck(root, 1.2, 3.0, [(1, 1.0), (2, 0.6), (3, 0.25)]))
        add((i * 4 + 2) * beat, 0.35 * pluck(root * 1.5, 0.6, 6.0, [(1, 1.0), (2, 0.3)]))
    speaker = bandpass_fft(out, 300, 3500)
    hiss = bandpass_fft(rng.standard_normal(n), 2000, 7000)
    return normalize(normalize(speaker, 0.7) + 0.03 * normalize(hiss, 1.0), 0.7)


def make_electric_hum(seconds=1.0):
    x = loop_tone(seconds, 220, [(1, 1.0), (2, 0.5), (3, 0.35), (5, 0.15)], 7.0, 0.12)
    return normalize(x + 0.05 * bandpass_fft(rng.standard_normal(len(x)), 2000, 6000), 0.5)


def make_reel(seconds=1.0, clicks=14):
    # 릴 감기 = 톱니바퀴 딸깍 열 + 낮은 웅웅. 초당 clicks 번, 1초에 정수 번이라 반복이 이어진다.
    n = int(seconds * RATE)
    x = np.zeros(n)
    tick = burst(0.03, 1800, 7000, 0.001, 140.0)
    for i in range(clicks):
        s = int(i * n / clicks)
        x[s:s + len(tick)] += tick * (1.0 if i % 2 == 0 else 0.7)
    t = np.arange(n) / RATE
    return normalize(x + 0.08 * np.sin(2 * np.pi * 90 * t), 0.5)


def make_cast():
    # 낚싯줄 던지기 = 짧은 휙. 휘두르기보다 높고 가늘다(줄이 공기를 가른다).
    n = int(0.42 * RATE)
    t = np.linspace(0, 1, n)
    env = np.sin(np.pi * t ** 0.7) ** 2
    x = bandpass_fft(rng.standard_normal(n), 1500, 7500) * env
    whip = chirp(0.18, 2800, 1100, 0.6, 0) * 0.35
    x[:len(whip)] += whip
    return normalize(x, 0.5)


def make_bite():
    # 입질 = 톡. 짧은 물방울 음 두 번.
    x = np.zeros(int(0.22 * RATE))
    for start, f in ((0.0, 1250), (0.09, 950)):
        d = chirp(0.07, f * 1.3, f * 0.7, 0.5, 0.3)
        s = int(start * RATE)
        x[s:s + len(d)] += d
    return normalize(x, 0.6)


def make_club():
    # 몽둥이 = 퍽. 낮은 타격음 + 나무 결 잡음.
    n = int(0.3 * RATE)
    t = np.linspace(0, 0.3, n)
    body = np.sin(2 * np.pi * np.cumsum(60 + 120 * np.exp(-30 * t)) / RATE) * np.exp(-14 * t)
    crack = burst(0.3, 400, 3000, 0.001, 40.0) * 0.6
    return normalize(body + crack, 0.8)


def make_machete():
    # 마체테 = 얇고 빠른 쉭 + 풀 잎이 스치는 잔음.
    n = int(0.26 * RATE)
    t = np.linspace(0, 1, n)
    env = np.sin(np.pi * t ** 0.6) ** 2
    x = bandpass_fft(rng.standard_normal(n), 2500, 9000) * env
    leaf = burst(0.26, 1200, 5000, 0.05, 12.0) * 0.35
    return normalize(x + leaf, 0.55)


def make_cinch():
    # 올가미 조이기 = 줄이 휙 당겨지는 쉭(위로 오르는 띠 잡음) + 매듭이 꽉 무는 딱 + 짧은 반짝 두 음(M21-03b).
    n = int(0.5 * RATE)
    x = np.zeros(n)
    zn = int(0.16 * RATE)
    t = np.linspace(0, 1, zn)
    zip_ = one_pole_lowpass(rng.standard_normal(zn), 900 + 4200 * t) * np.sin(np.pi * t * 0.5) ** 2
    x[:zn] += zip_ * 0.6
    snap = burst(0.05, 1500, 6000, 0.001, 90.0)
    s = int(0.15 * RATE)
    x[s:s + len(snap)] += snap
    for start, f in ((0.19, 1568), (0.27, 2093)):   # 솔·도 — 높고 맑게
        d = chirp(0.2, f, f, 1.0, 0.25) * np.exp(-14 * np.linspace(0, 0.2, int(0.2 * RATE)))
        s = int(start * RATE)
        x[s:s + len(d)] += d * 0.35
    return normalize(x, 0.7)


def make_pistol_shot():
    # 권총 = 탕. 넓은 띠 폭발 잡음(아주 빠른 감쇠) + 낮은 쿵 + 잔향 꼬리(M39-06c).
    n = int(0.7 * RATE)
    t = np.linspace(0, 0.7, n)
    crack = burst(0.7, 800, 12000, 0.0005, 55.0)
    boom = np.sin(2 * np.pi * np.cumsum(70 + 160 * np.exp(-40 * t)) / RATE) * np.exp(-11 * t)
    tail = bandpass_fft(rng.standard_normal(n), 200, 2500) * np.exp(-6 * t) * np.minimum(t / 0.02, 1.0)
    return normalize(crack + 0.9 * boom + 0.25 * tail, 0.9)


def metal_tick(duration, lo, hi, ring_hz, decay):
    """쇠붙이 딸깍 — 짧은 잡음 + 높은 울림 한 음."""
    n = int(duration * RATE)
    t = np.linspace(0, duration, n)
    ring = np.sin(2 * np.pi * ring_hz * t) * np.exp(-decay * 0.6 * t)
    return burst(duration, lo, hi, 0.0005, decay) + 0.3 * ring


def make_empty_click():
    # 빈 탄창 = 공이가 헛치는 찰칵 두 겹(가볍고 높게).
    x = np.zeros(int(0.16 * RATE))
    for start, ring, gain in ((0.0, rng.uniform(3200, 3800), 1.0), (0.035, rng.uniform(2300, 2700), 0.55)):
        d = metal_tick(0.06, 2500, 10000, ring, 120.0)
        s = int(start * RATE)
        x[s:s + len(d)] += d * gain
    return normalize(x, 0.6)


def make_pistol_reload():
    # 재장전 1.2초 = 탄창 빠짐(딸깍) → 새 탄창 밀어 넣음(철컥, 몸 동작 f15 = 0.5초) → 슬라이드 당김·놓음(f20 = 0.67초).
    x = np.zeros(int(1.2 * RATE))
    for start, lo, hi, ring, decay, gain in (
            (0.18, 1500, 7000, 2100, 80.0, 0.5),    # 탄창 빠짐
            (0.50, 600, 6000, 1400, 45.0, 1.0),     # 탄창 철컥
            (0.66, 2000, 9000, 2900, 90.0, 0.6),    # 슬라이드 당김
            (0.76, 900, 8000, 1800, 50.0, 0.9)):    # 슬라이드 놓음
        d = metal_tick(0.12, lo, hi, ring, decay)
        s = int(start * RATE)
        x[s:s + len(d)] += d * gain
    sn = int(0.08 * RATE)   # 탄창이 손잡이 속을 스치는 쓱
    s = int(0.42 * RATE)
    x[s:s + sn] += bandpass_fft(rng.standard_normal(sn), 800, 4000) * np.sin(np.linspace(0, np.pi, sn)) * 0.15
    return normalize(x, 0.6)


def make_bullet_impact():
    # 총알이 박힘 = 둔한 퍽 + 짧은 부서짐 잡음(동물·땅·나무 공통 — 튀김 그림이 종류를 말한다).
    n = int(0.25 * RATE)
    t = np.linspace(0, 0.25, n)
    thud = np.sin(2 * np.pi * np.cumsum(90 + 140 * np.exp(-50 * t)) / RATE) * np.exp(-22 * t)
    grit = burst(0.25, 500, 5000, 0.0008, 35.0) * 0.7
    return normalize(thud + grit, 0.7)


def voice(duration, f0, formants, harmonics=24, rough=0.0, rough_hz=30.0, breath=0.0, attack=0.02, release=0.3):
    """동물 목 울음 = 배음 열(높이 곡선 f0) × 입·목 울림통(formants = (중심Hz, 폭Hz, 세기)) + 거친 떨림 + 숨 잡음.

    f0 는 0~1 시간을 받아 Hz 를 돌려주는 함수 — 올라갔다 내려가는 울음 모양을 그린다(M43-01).
    """
    n = int(duration * RATE)
    t = np.linspace(0, 1, n)
    freq = f0(t) * (1 + 0.02 * np.sin(2 * np.pi * rng.uniform(5, 8) * t * duration))
    phase = 2 * np.pi * np.cumsum(freq) / RATE
    x = np.zeros(n)
    for h in range(1, harmonics + 1):
        fh = freq * h
        amp = sum(g * np.exp(-0.5 * ((fh - c) / w) ** 2) for c, w, g in formants) / h ** 0.3
        x += np.where(fh < 16000, amp, 0.0) * np.sin(h * phase + h)
    if rough:   # 으르렁·꽥 = 진폭이 빠르게 덜컥거린다
        x *= 1 - rough + rough * np.abs(np.sin(np.pi * rough_hz * t * duration + rng.uniform(0, 3)))
    if breath:
        x += breath * bandpass_fft(rng.standard_normal(n), formants[0][0] * 0.5, formants[-1][0] * 1.5) * np.max(np.abs(x))
    sec = t * duration
    env = np.minimum(sec / attack, 1.0) * np.minimum((duration - sec) / release, 1.0) ** 1.5
    return x * env


def flutter(duration, beats):
    """날갯짓 푸드덕 — 짧은 바람 잡음 덩어리 beats 번."""
    x = np.zeros(int(duration * RATE))
    for i in range(beats):
        d = burst(0.06, 400, 3500, 0.01, 30.0)
        s = int((i / beats) * (duration - 0.07) * RATE)
        x[s:s + len(d)] += d * rng.uniform(0.6, 1.0)
    return x


def make_animal_bird_hit():
    # 새가 맞음 = 짧고 거친 「꽥」 — 높이 솟았다 떨어지는 쉰 소리.
    top = rng.uniform(1100, 1500)
    x = voice(rng.uniform(0.24, 0.34), lambda t: top * (0.8 + 0.4 * np.sin(np.pi * t ** 0.6)) * (1 - 0.35 * t),
              [(2200, 700, 1.0), (3800, 900, 0.5)], 14, rough=0.5, rough_hz=55, breath=0.25, attack=0.008, release=0.12)
    return normalize(x, 0.8)


def make_animal_bird_death():
    # 새가 죽음 = 길게 꺾여 내려가는 울음 둘 + 마지막 푸드덕.
    d = rng.uniform(0.75, 0.95)
    top = rng.uniform(1000, 1300)
    x = voice(d, lambda t: top * (1.1 - 0.55 * t) * (1 + 0.12 * np.sin(2 * np.pi * 3 * t)),
              [(2000, 700, 1.0), (3500, 900, 0.4)], 14, rough=0.35, rough_hz=40, breath=0.2, attack=0.01, release=0.35)
    x += 0.35 * flutter(d, 5) * np.max(np.abs(x))
    return normalize(x, 0.8)


def make_animal_deer_hit():
    # 사슴류가 맞음 = 콧소리 섞인 짧은 「메엥」(염소·순록 울음 근사).
    base = rng.uniform(320, 420)
    x = voice(rng.uniform(0.32, 0.45), lambda t: base * (1 + 0.25 * np.sin(np.pi * t ** 0.5)),
              [(700, 250, 1.0), (1500, 400, 0.6), (2600, 500, 0.25)], 30, rough=0.3, rough_hz=22, breath=0.15,
              attack=0.015, release=0.15)
    return normalize(x, 0.8)


def make_animal_deer_death():
    # 사슴류가 죽음 = 길게 떨며 가라앉는 울음.
    d = rng.uniform(0.85, 1.1)
    base = rng.uniform(300, 380)
    x = voice(d, lambda t: base * (1.2 - 0.5 * t) * (1 + 0.05 * np.sin(2 * np.pi * 9 * t)),
              [(650, 250, 1.0), (1400, 400, 0.5), (2500, 500, 0.2)], 30, rough=0.35, rough_hz=18, breath=0.25,
              attack=0.02, release=0.5)
    return normalize(x, 0.8)


def make_animal_deer_aggro():
    # 사슴류가 덤빔 = 콧김 「푸흥」 + 낮은 울부짖음(엘크 수컷 근사).
    d = rng.uniform(0.55, 0.75)
    snort = burst(0.18, 300, 2500, 0.005, 14.0)
    base = rng.uniform(170, 230)
    x = voice(d, lambda t: base * (1 + 0.3 * np.sin(np.pi * t)), [(500, 200, 1.0), (1100, 350, 0.5)], 30,
              rough=0.4, rough_hz=25, breath=0.2, attack=0.04, release=0.2)
    x[:len(snort)] += snort * np.max(np.abs(x)) * 1.2
    return normalize(x, 0.85)


def make_animal_predator_hit():
    # 사냥꾼이 맞음 = 「깨갱」 — 짧게 치솟았다 떨어지는 비명.
    base = rng.uniform(550, 750)
    x = voice(rng.uniform(0.22, 0.32), lambda t: base * (1 + 0.6 * np.sin(np.pi * t ** 0.4)) * (1 - 0.3 * t),
              [(1100, 400, 1.0), (2400, 600, 0.5)], 20, rough=0.25, rough_hz=35, breath=0.15, attack=0.006, release=0.1)
    return normalize(x, 0.8)


def make_animal_predator_death():
    # 사냥꾼이 죽음 = 낑낑 꺾이다 숨이 빠지는 신음.
    d = rng.uniform(0.85, 1.1)
    base = rng.uniform(380, 480)
    x = voice(d, lambda t: base * (1.15 - 0.6 * t) * (1 + 0.08 * np.sin(2 * np.pi * 4 * t)),
              [(900, 350, 1.0), (2000, 500, 0.35)], 24, rough=0.3, rough_hz=20, breath=0.35, attack=0.01, release=0.6)
    return normalize(x, 0.8)


def make_animal_predator_aggro():
    # 사냥꾼이 덤빔 = 낮은 「그르르」 — 낮은 배음 + 빠른 덜컥거림 + 이빨 사이 숨.
    d = rng.uniform(0.7, 1.0)
    base = rng.uniform(85, 115)
    x = voice(d, lambda t: base * (1 + 0.25 * np.sin(np.pi * t) + 0.05 * np.sin(2 * np.pi * 3 * t)),
              [(350, 150, 1.0), (900, 300, 0.6), (2200, 600, 0.2)], 40, rough=0.75, rough_hz=32, breath=0.4,
              attack=0.08, release=0.25)
    return normalize(x, 0.85)


def make_animal_livestock_hit():
    # 가축이 맞음 = 「꽥」 돼지 비명 근사 — 높고 콧소리 많은 짧은 소리.
    base = rng.uniform(600, 800)
    x = voice(rng.uniform(0.28, 0.4), lambda t: base * (1 + 0.35 * np.sin(np.pi * t ** 0.5)),
              [(1300, 500, 1.0), (2800, 700, 0.6)], 20, rough=0.55, rough_hz=45, breath=0.3, attack=0.008, release=0.12)
    return normalize(x, 0.8)


def make_animal_livestock_death():
    # 가축이 죽음 = 낮게 길게 끄는 「음메에」 — 소·양 울음 근사.
    d = rng.uniform(0.9, 1.15)
    base = rng.uniform(140, 190)
    x = voice(d, lambda t: base * (1.15 - 0.35 * t) * (1 + 0.03 * np.sin(2 * np.pi * 6 * t)),
              [(450, 200, 0.6), (800, 250, 1.0), (1800, 400, 0.3)], 40, rough=0.2, rough_hz=16, breath=0.2,
              attack=0.06, release=0.5)
    return normalize(x, 0.8)


ANIMAL_SOUNDS = {   # DESIGN 8.7 「동물 소리」 — 묶음 4 × (맞음·죽음) + 덤빔 2 = 10가지
    "AnimalBirdHit": make_animal_bird_hit, "AnimalBirdDeath": make_animal_bird_death,
    "AnimalDeerHit": make_animal_deer_hit, "AnimalDeerDeath": make_animal_deer_death,
    "AnimalDeerAggro": make_animal_deer_aggro,
    "AnimalPredatorHit": make_animal_predator_hit, "AnimalPredatorDeath": make_animal_predator_death,
    "AnimalPredatorAggro": make_animal_predator_aggro,
    "AnimalLivestockHit": make_animal_livestock_hit, "AnimalLivestockDeath": make_animal_livestock_death,
}


def make_player_hurt():
    # 플레이어가 맞음 = 짧은 「윽」 — 목이 막혔다 터지는 숨 + 「으」 모음(울림통 350·1400Hz)이 떨어진다.
    d = rng.uniform(0.22, 0.36)
    base = rng.uniform(150, 200)
    x = voice(d, lambda t: base * (1.15 - 0.3 * t), [(380, 150, 1.0), (1400, 300, 0.45), (2500, 500, 0.15)], 30,
              rough=0.3, rough_hz=28, breath=0.25, attack=0.008, release=0.12)
    gasp = burst(0.06, 600, 3500, 0.003, 40.0)
    x[:len(gasp)] += gasp * np.max(np.abs(x)) * 0.6
    return normalize(x, 0.8)


def thud(duration=0.25, f0=70.0):
    """몸이 땅에 털썩 — 낮게 꺼지는 사인 + 흙 잡음."""
    n = int(duration * RATE)
    t = np.linspace(0, duration, n)
    body = np.sin(2 * np.pi * f0 * t * (1 - 0.3 * t / duration)) * np.exp(-14 * t)
    return body + 0.5 * burst(duration, 80, 1200, 0.003, 18.0) / 3


def make_player_down():
    # 플레이어가 쓰러짐 = 길게 빠지는 「으아…」 신음 + 끝에 털썩.
    d = rng.uniform(0.65, 0.9)
    base = rng.uniform(140, 180)
    x = voice(d, lambda t: base * (1.2 - 0.45 * t), [(550, 200, 1.0), (1100, 300, 0.6), (2400, 500, 0.15)], 30,
              rough=0.35, rough_hz=20, breath=0.3, attack=0.02, release=0.35)
    x = normalize(x, 0.6)
    th = normalize(thud(rng.uniform(0.22, 0.3), rng.uniform(55, 80)), 0.9)
    s = len(x) - len(th) - int(0.02 * RATE)
    x[s:s + len(th)] += th
    return normalize(x, 0.85)


def chew_bites(duration, count):
    """씹기 — 입 안 축축한 잡음 덩어리 count 번(이 부딪는 딸깍 + 짧은 바삭)."""
    x = np.zeros(int(duration * RATE))
    for i in range(count):
        s = int((0.03 + i * (duration - 0.16) / max(count - 1, 1)) * RATE)
        c = burst(rng.uniform(0.09, 0.13), rng.uniform(250, 400), rng.uniform(2500, 4500), 0.004, 28.0)
        c[:int(0.004 * RATE)] += rng.uniform(0.4, 0.7) * np.max(np.abs(c))   # 이 딸깍
        x[s:s + len(c)] += c * rng.uniform(0.7, 1.0)
    return x


def make_eat():
    # 먹기 = 씹기 2~3번(「냠냠」) — 회복 아이템.
    d = rng.uniform(0.45, 0.75)
    return normalize(chew_bites(d, int(rng.integers(2, 4))), 0.7)


def make_eat_poison():
    # 독 먹기 = 씹기 2번 + 낮게 끄는 「으…」(속이 안 좋다).
    d = rng.uniform(0.7, 0.95)
    x = normalize(chew_bites(0.32, 2), 0.6)
    x = np.concatenate([x, np.zeros(int(d * RATE) - len(x))])
    g = int(0.24 * RATE)
    base = rng.uniform(100, 125)
    groan = voice(d - 0.24, lambda t: base * (1.05 - 0.15 * t), [(350, 150, 1.0), (1300, 300, 0.4)], 30,
                  rough=0.45, rough_hz=14, breath=0.2, attack=0.06, release=0.25)
    x[g:g + len(groan)] += normalize(groan, 0.6)
    return normalize(x, 0.75)


PLAYER_SOUNDS = {   # DESIGN 2.2 「플레이어 몸 소리」 — 4가지 × 3
    "PlayerHurt": make_player_hurt, "PlayerDown": make_player_down,
    "Eat": make_eat, "EatPoison": make_eat_poison,
}


def wood_knock(duration, lo, hi, body_hz, decay):
    """나무 덜컥 — 짧은 잡음 + 낮은 나무통 울림 한 음."""
    n = int(duration * RATE)
    t = np.linspace(0, duration, n)
    body = np.sin(2 * np.pi * body_hz * t) * np.exp(-decay * 0.8 * t)
    return burst(duration, lo, hi, 0.001, decay) + 0.6 * body


def make_build_remove():
    # 철거 = 나무 판을 뜯는 삐걱(높은 마찰 잡음이 올라감) 두 번 + 끝에 판이 떨어지는 털썩.
    d = rng.uniform(0.6, 0.85)
    x = np.zeros(int(d * RATE))
    for start, gain in ((0.0, 0.8), (rng.uniform(0.12, 0.18), 1.0)):
        cn = int(rng.uniform(0.12, 0.16) * RATE)
        t = np.linspace(0, cn / RATE, cn)
        creak = bandpass_fft(rng.standard_normal(cn), 700, 3200) * np.sin(np.linspace(0, np.pi, cn))
        creak *= 1 + 0.6 * np.sin(2 * np.pi * rng.uniform(55, 80) * t)   # 삐걱 결
        s = int(start * RATE)
        x[s:s + cn] += creak * gain
    x = normalize(x, 0.5)
    td = rng.uniform(0.22, 0.3)
    th = normalize(thud(td, rng.uniform(70, 95)) + 0.5 * wood_knock(td, 300, 3000, 180, 30.0), 0.9)
    s = len(x) - len(th)
    x[s:s + len(th)] += th
    return normalize(x, 0.85)


def make_paint():
    # 칠하기 = 붓으로 쓱쓱 두 번(부드러운 넓은 띠 잡음, 가운데 볼록).
    d = rng.uniform(0.45, 0.65)
    x = np.zeros(int(d * RATE))
    half = d / 2
    for i in range(2):
        sn = int(rng.uniform(0.17, half - 0.02) * RATE)
        stroke = bandpass_fft(rng.standard_normal(sn), rng.uniform(1500, 2200), rng.uniform(6000, 8000))
        stroke *= np.sin(np.linspace(0, np.pi, sn)) ** 1.5
        s = int((0.01 + i * half) * RATE)
        x[s:s + sn] += stroke * (1.0 if i else 0.8)
    return normalize(x, 0.6)


def make_storage():
    # 보관함 = 나무 뚜껑 덜컥 + 물건이 바닥에 툭.
    d = rng.uniform(0.35, 0.55)
    x = np.zeros(int(d * RATE))
    lid = wood_knock(0.18, 250, 4000, rng.uniform(150, 210), 28.0)
    x[:len(lid)] += lid
    drop = wood_knock(0.12, 150, 2000, rng.uniform(100, 140), 40.0)
    s = int(rng.uniform(0.14, 0.2) * RATE)
    x[s:s + len(drop)] += drop * 0.7
    return normalize(x, 0.7)


def make_crate_open():
    # 상자 열기 = 자물쇠 딸깍 → 뚜껑 덜컥 → 위로 오르는 반짝(높은 사인 4음, 젬 쓴 결과라 화려하게).
    d = rng.uniform(0.85, 1.15)
    x = np.zeros(int(d * RATE))
    click = metal_tick(0.06, 2500, 9000, rng.uniform(2800, 3400), 110.0)
    x[:len(click)] += click * 0.6
    lid = wood_knock(0.2, 250, 4000, rng.uniform(140, 190), 25.0)
    s = int(0.1 * RATE)
    x[s:s + len(lid)] += lid
    root = rng.uniform(880, 1050)
    for i, ratio in enumerate((1.0, 1.26, 1.5, 2.0)):   # 장3화음 + 옥타브로 올라감
        tn = int(min(0.45, d - 0.3 - i * 0.07) * RATE)
        t = np.linspace(0, tn / RATE, tn)
        tone = (np.sin(2 * np.pi * root * ratio * t) + 0.3 * np.sin(4 * np.pi * root * ratio * t)) * np.exp(-7 * t)
        tone *= np.minimum(t / 0.004, 1.0) * np.minimum((tn / RATE - t) / 0.03, 1.0)   # 끝을 줄여 딸깍 없앰
        s = int((0.28 + i * 0.07) * RATE)
        x[s:s + tn] += tone * 0.35
    return normalize(x, 0.75)


def croak(duration, f0):
    """개구리 한 번 — 낮은 목소리를 초당 18~26번 끊어 「꾸르륵」 결을 낸다."""
    n = int(duration * RATE)
    t = np.linspace(0, duration, n)
    freq = f0 * (1 + 0.15 * np.sin(np.pi * t / duration))   # 가운데서 살짝 올라감
    phase = 2 * np.pi * np.cumsum(freq) / RATE
    tone = np.sin(phase) + 0.5 * np.sin(2 * phase) + 0.25 * np.sin(3 * phase)
    pulses = (0.5 + 0.5 * np.sin(2 * np.pi * rng.uniform(18, 26) * t)) ** 2
    env = np.clip(np.sin(np.pi * t / duration), 0, None) ** 0.7
    return tone * pulses * env


def make_amb_swamp(seconds=10.0):
    # 늪 = 낮은 물기 웅웅 + 벌레 지글(4~7kHz, 빠르게 떨림) + 개구리 울음이 여기저기서(멀고 가까움이 섞임).
    n = int((seconds + 2.0) * RATE)
    t = np.arange(n) / RATE
    murk = one_pole_lowpass(pink_noise(n), 300) * 0.5
    swell = np.interp(np.arange(n), np.linspace(0, n, 6), rng.uniform(0.5, 1.0, 6))
    buzz = bandpass_fft(rng.standard_normal(n), 4000, 7000) * (0.6 + 0.4 * np.sin(2 * np.pi * 47 * t)) * swell * 0.12
    x = murk + buzz
    pos = rng.uniform(0.0, 0.4)
    while pos < seconds + 1.5:
        d = rng.uniform(0.18, 0.45)
        c = croak(d, rng.uniform(180, 420)) * rng.uniform(0.25, 0.8)
        s = int(pos * RATE)
        e = min(n, s + len(c))
        x[s:e] += c[:e - s]
        pos += rng.uniform(0.35, 1.3)
    return normalize(seamless(x), 0.55)


def make_amb_night(seconds=10.0):
    # 밤 = 귀뚜라미 셋(음높이·박자 다름)이 「찌르르」 묶음을 되풀이 + 아주 낮은 밤공기.
    n = int((seconds + 2.0) * RATE)
    x = one_pole_lowpass(pink_noise(n), 250) * 0.15
    for _ in range(3):
        f = rng.uniform(4200, 5400)
        gain = rng.uniform(0.3, 1.0)
        period = rng.uniform(0.5, 0.9)
        pos = rng.uniform(0.0, period)
        pn = int(0.018 * RATE)
        pt = np.linspace(0, 1, pn)
        pulse = np.sin(2 * np.pi * f * pt * 0.018) * np.sin(np.pi * pt) ** 2
        while pos < seconds + 1.9:
            for k in range(rng.integers(3, 5)):   # 한 묶음 = 3~4 번 비빔
                s = int((pos + k * 0.03) * RATE)
                if s + pn < n:
                    x[s:s + pn] += pulse * gain
            pos += period * rng.uniform(0.9, 1.1)
    return normalize(seamless(x), 0.5)


def make_amb_rain(seconds=10.0):
    # 비 = 높은 쉬익 띠(2~9kHz 분홍 잡음) + 낮은 웅웅 + 짧은 물방울 톡톡(무작위 자리).
    n = int((seconds + 2.0) * RATE)
    x = bandpass_fft(pink_noise(n), 2000, 9000) * 0.5 + one_pole_lowpass(pink_noise(n), 400) * 0.2
    for _ in range(int(seconds * 30)):
        s0 = int(rng.uniform(0, seconds + 1.9) * RATE)
        dn = int(0.012 * RATE)
        if s0 + dn < n:
            f = rng.uniform(2500, 5000)
            dt = np.linspace(0, 1, dn)
            x[s0:s0 + dn] += np.sin(2 * np.pi * f * dt * 0.012) * (1 - dt) ** 2 * rng.uniform(0.1, 0.4)
    return normalize(seamless(x), 0.5)


def make_footstep_mud():
    # 진흙 발소리 = 발이 빠지는 낮은 철벅 + 빼낼 때 쭉 빨리는 소리(아래로 꺼지는 음).
    d = rng.uniform(0.18, 0.28)
    n = int(d * RATE)
    t = np.linspace(0, d, n)
    x = burst(d, 120, 1500, 0.004, 14.0)
    sn = int(rng.uniform(0.08, 0.12) * RATE)
    st = np.linspace(0, 1, sn)
    suck = np.sin(2 * np.pi * np.cumsum(rng.uniform(260, 340) * (1 - 0.5 * st)) / RATE) * np.sin(np.pi * st) ** 2
    s = n - sn
    x[s:] += suck * 0.35
    x *= np.minimum((d - t) / 0.02, 1.0)   # 끝을 줄여 딸깍 없앰
    return normalize(x, 0.7)


BASE_SOUNDS = {   # DESIGN 2.2 「거점·상자 소리」 — 4가지 × 3
    "BuildRemove": make_build_remove, "Paint": make_paint,
    "Storage": make_storage, "CrateOpen": make_crate_open,
}


def fade_tail(x, fade_s=0.02):
    """끝을 줄여 딸깍 없앰."""
    t = np.arange(len(x)) / RATE
    return x * np.minimum((len(x) / RATE - t) / fade_s, 1.0)


def strike_env(n, half, attack=0.002, fade=0.006):
    """쇠·나무 덩어리 모양 — 2ms 안에 서고(부드러운 곡선이라 전 대역 딸깍 없음) 반감 half 초로 줄고 끝 fade 초에 0."""
    t = np.arange(n) / RATE
    env = np.sin(np.minimum(t / attack, 1.0) * np.pi / 2) ** 2 * 0.5 ** (t / half)
    k = min(int(fade * RATE), n)
    env[n - k:] *= np.cos(np.linspace(0, np.pi / 2, k)) ** 2
    return env


def sweep_noise(n, lo, hi, rise=True):
    """아래 띠와 위 띠 잡음을 섞는 비율이 위(또는 아래)로 쓸려 간다 — 모양은 부르는 쪽이 씌운다."""
    a = bandpass_fft(rng.standard_normal(n), lo, (lo + hi) / 2)
    b = bandpass_fft(rng.standard_normal(n), (lo + hi) / 2, hi)
    mix = np.linspace(0, 1, n) if rise else np.linspace(1, 0, n)
    return a * (1 - mix) + b * mix


def make_equip_blade():
    # 칼날 꺼내기 = 칼집을 벗어나는 순간 터지는 쉭(위로 쓸림, 반감 30~40ms) + 바로 이어 얇은 쇠 울림.
    d = rng.uniform(0.15, 0.35)
    n = int(d * RATE)
    x = sweep_noise(n, rng.uniform(2500, 3500), rng.uniform(9000, 12000)) * strike_env(n, rng.uniform(0.03, 0.04))
    rn = int(min(0.12, d - 0.05) * RATE)
    t = np.arange(rn) / RATE
    s = int(0.03 * RATE)
    x[s:s + rn] += np.sin(2 * np.pi * rng.uniform(4200, 5200) * t) * strike_env(rn, 0.03) * 0.25
    return normalize(x, 0.6)


def make_equip_blunt():
    # 둔기·도구 꺼내기 = 손바닥이 나무 자루를 쥐는 짧은 스침 + 툭(낮은 나무 울림).
    d = rng.uniform(0.15, 0.3)
    x = np.zeros(int(d * RATE))
    sn = int(0.06 * RATE)
    x[:sn] += bandpass_fft(rng.standard_normal(sn), 1200, 5000) * strike_env(sn, 0.015) * 0.3
    kn = int(min(0.14, d - 0.04) * RATE)
    t = np.arange(kn) / RATE
    k = bandpass_fft(rng.standard_normal(kn), 200, 3000) + 0.6 * np.sin(2 * np.pi * rng.uniform(170, 240) * t)
    s = int(0.04 * RATE)
    x[s:s + kn] += k * strike_env(kn, 0.02)
    return normalize(x, 0.6)


def make_equip_gun():
    # 총 꺼내기 = 노리쇠 당김(높은 딸깍, 반감 15ms) → 놓음(무거운 철컥, 반감 25ms) 두 마디. 마디마다 서고 줄고 페이드.
    d = rng.uniform(0.22, 0.35)
    x = np.zeros(int(d * RATE))
    for start, (lo, hi), ring_hz, half, length, gain in (
            (0.0, (2000, 9000), rng.uniform(2600, 3200), 0.015, 0.075, 0.55),
            (rng.uniform(0.08, d - 0.12), (800, 8000), rng.uniform(1500, 1900), 0.025, 0.11, 1.0)):
        cn = int(length * RATE)
        t = np.arange(cn) / RATE
        c = bandpass_fft(rng.standard_normal(cn), lo, hi) + 0.3 * np.sin(2 * np.pi * ring_hz * t)
        s = int(start * RATE)
        x[s:s + cn] += c * strike_env(cn, half) * gain
    return normalize(x, 0.6)


def make_holster():
    # 넣기 = 칼집·허리에 밀어 넣는 아래로 쓸리는 스륵(반감 40ms) + 끝에 가죽 툭(반감 12ms, 제 덩어리 페이드로 끝남).
    d = rng.uniform(0.15, 0.3)
    n = int(d * RATE)
    x = sweep_noise(n, rng.uniform(800, 1200), rng.uniform(4500, 6000), rise=False) * strike_env(n, 0.04) * 0.7
    tn = int(0.07 * RATE)
    tap = bandpass_fft(rng.standard_normal(tn), 150, 1500) * strike_env(tn, 0.012)
    s = n - tn
    x[s:s + tn] += tap * 0.6
    return normalize(x, 0.55)


def make_outfit():
    # 옷 부스럭 = 천이 구겨지는 넓은 띠 잡음 덩어리 3~5개가 겹침.
    d = rng.uniform(0.3, 0.5)
    x = np.zeros(int(d * RATE))
    for _ in range(rng.integers(3, 6)):
        cn = int(rng.uniform(0.06, 0.14) * RATE)
        c = bandpass_fft(rng.standard_normal(cn), rng.uniform(1500, 2500), rng.uniform(6000, 9000))
        c *= np.sin(np.linspace(0, np.pi, cn)) ** 2 * rng.uniform(0.4, 1.0)
        s = int(rng.uniform(0.0, d - cn / RATE - 0.01) * RATE)
        x[s:s + cn] += c
    return normalize(fade_tail(x), 0.5)


def leather_creak(duration, f0):
    """안장 가죽 삐걱 — 낮은 목소리를 초당 55~80번 끊어 문지르는 결."""
    n = int(duration * RATE)
    t = np.linspace(0, duration, n)
    phase = 2 * np.pi * np.cumsum(f0 * (1 + 0.25 * t / duration)) / RATE
    tone = np.sin(phase) + 0.6 * np.sin(2 * phase) + 0.4 * np.sin(3 * phase)
    grain = (0.5 + 0.5 * np.sin(2 * np.pi * rng.uniform(55, 80) * t)) ** 3
    return tone * grain * np.clip(np.sin(np.pi * t / duration), 0, None) ** 1.5


def snort(duration):
    """동물 콧김 — 낮은 띠 잡음이 빠르게 일어서 천천히 빠짐."""
    n = int(duration * RATE)
    t = np.linspace(0, duration, n)
    return bandpass_fft(rng.standard_normal(n), 300, 2200) * np.minimum(t / 0.02, 1.0) * np.exp(-6 * t)


def make_mount_on():
    # 타기 = 안장 가죽 삐걱 → 동물이 내쉬는 콧김.
    d = rng.uniform(0.5, 0.9)
    x = np.zeros(int(d * RATE))
    cr = leather_creak(rng.uniform(0.18, 0.28), rng.uniform(140, 200))
    x[:len(cr)] += cr * 0.6
    sn = snort(d - rng.uniform(0.15, 0.25))
    x[len(x) - len(sn):] += sn
    return normalize(fade_tail(x, 0.05), 0.8)


def make_mount_off():
    # 내리기 = 가죽 짧게 삐걱 → 땅을 디디는 툭(낮게).
    d = rng.uniform(0.3, 0.6)
    x = np.zeros(int(d * RATE))
    cr = leather_creak(rng.uniform(0.1, 0.15), rng.uniform(150, 210))
    x[:len(cr)] += cr * 0.5
    sn = int(0.15 * RATE)
    step = bandpass_fft(rng.standard_normal(sn), 80, 1200) * strike_env(sn, 0.03, attack=0.003)
    s = int(rng.uniform(0.12, d - 0.16) * RATE)
    x[s:s + len(step)] += step
    return normalize(fade_tail(x), 0.6)


EQUIP_SOUNDS = {   # DESIGN 2.2 「장비·탈것 소리」 — 7가지 × 3
    "EquipBlade": make_equip_blade, "EquipBlunt": make_equip_blunt, "EquipGun": make_equip_gun,
    "Holster": make_holster, "Outfit": make_outfit, "MountOn": make_mount_on, "MountOff": make_mount_off,
}


def bell(n, freq, half):
    """맑은 종 한 음 — 기음 + 2·3배음(높을수록 빨리 줄어듦), 모양은 strike_env."""
    t = np.arange(n) / RATE
    tone = (np.sin(2 * np.pi * freq * t) + 0.35 * np.sin(2 * np.pi * 2 * freq * t) * 0.5 ** (t / (half * 0.5))
            + 0.15 * np.sin(2 * np.pi * 3 * freq * t) * 0.5 ** (t / (half * 0.3)))
    return tone * strike_env(n, half)


def make_achievement():
    # 업적 달성 = 밝은 두 음 딩(아래)·동(장3도~완전4도 위) + 끝에 높은 반짝 톡톡.
    d = rng.uniform(0.6, 0.9)
    n = int(d * RATE)
    x = np.zeros(n)
    f0 = rng.uniform(880, 990)
    s2 = int(rng.uniform(0.11, 0.15) * RATE)
    x[:n] += bell(n, f0, 0.12) * 0.7
    x[s2:] += bell(n - s2, f0 * rng.choice([1.26, 1.335]), 0.18)
    for _ in range(5):   # 반짝 = 4~7kHz 짧은 종 다섯 톡
        s = int(rng.uniform(0.18, d - 0.15) * RATE)
        k = int(0.08 * RATE)
        x[s:s + k] += bell(k, rng.uniform(4000, 7000), 0.015) * 0.12
    return normalize(fade_tail(x), 0.7)


def make_codex_new():
    # 도감 새 칸 = 펜이 종이를 쓱 긋는 소리(위로 쓸리는 잡음) → 짧은 띵 한 음.
    d = rng.uniform(0.4, 0.6)
    n = int(d * RATE)
    x = np.zeros(n)
    sn = int(rng.uniform(0.12, 0.18) * RATE)
    x[:sn] += sweep_noise(sn, 1500, 7000) * np.sin(np.linspace(0, np.pi, sn)) ** 1.2 * 0.35
    s = sn + int(0.02 * RATE)
    x[s:] += bell(n - s, rng.uniform(1300, 1500), 0.08)
    return normalize(fade_tail(x), 0.6)


NOTICE_SOUNDS = {   # DESIGN 8.15 「달성 알림」 — 2가지 × 2
    "Achievement": make_achievement, "CodexNew": make_codex_new,
}


def make_bag_drop():
    # 버리기 = 천 자루가 땅에 털썩(낮은 몸 울림 + 흙) + 안의 물건이 덜그럭 한 번 + 천 스침.
    d = rng.uniform(0.3, 0.6)
    x = np.zeros(int(d * RATE))
    sn = int(0.05 * RATE)
    x[:sn] += bandpass_fft(rng.standard_normal(sn), 1500, 6000) * strike_env(sn, 0.015) * 0.25   # 손에서 놓는 천 스침
    s = int(rng.uniform(0.04, 0.07) * RATE)
    tn = int(min(0.26, d - s / RATE - 0.01) * RATE)
    th = thud(tn / RATE, rng.uniform(65, 90)) * strike_env(tn, 0.06)
    x[s:s + tn] += normalize(th, 0.9)
    kn = int(0.08 * RATE)
    k = int(s + rng.uniform(0.03, 0.06) * RATE)
    if k + kn < len(x):   # 자루 속 물건 덜그럭
        x[k:k + kn] += wood_knock(kn / RATE, 300, 3500, rng.uniform(200, 280), 45.0) * strike_env(kn, 0.015) * 0.3
    return normalize(fade_tail(x), 0.8)


def make_bag_pickup():
    # 줍기 = 천 자루를 움켜쥐는 부스럭(넓은 띠 잡음 덩어리 2~3) + 어깨에 메는 가벼운 툭.
    d = rng.uniform(0.3, 0.5)
    x = np.zeros(int(d * RATE))
    for _ in range(rng.integers(2, 4)):
        cn = int(rng.uniform(0.06, 0.12) * RATE)
        c = bandpass_fft(rng.standard_normal(cn), rng.uniform(1200, 2000), rng.uniform(5500, 8000))
        c *= np.sin(np.linspace(0, np.pi, cn)) ** 2 * rng.uniform(0.5, 1.0)
        s = int(rng.uniform(0.0, max(0.01, d - 0.2 - cn / RATE)) * RATE)
        x[s:s + cn] += c * 0.6
    tn = int(0.1 * RATE)
    tap = bandpass_fft(rng.standard_normal(tn), 120, 1400) * strike_env(tn, 0.02, attack=0.003)
    s = int((d - 0.14) * RATE)
    x[s:s + tn] += normalize(tap, 1.0)
    return normalize(fade_tail(x), 0.7)


def make_chest_open():
    # 전리품 상자 열기 = 걸쇠 딸깍 → 나무 뚜껑이 삐걱 올라감 → 뚜껑 덜컥 + 짧은 반짝 두 톡(가챠 4음보다 수수하게).
    d = rng.uniform(0.7, 1.1)
    x = np.zeros(int(d * RATE))
    click = metal_tick(0.05, 2000, 8000, rng.uniform(2200, 2800), 120.0)
    x[:len(click)] += click * 0.5
    cs = int(0.06 * RATE)
    cn = int(rng.uniform(0.22, 0.32) * RATE)
    t = np.arange(cn) / RATE
    creak = leather_creak(cn / RATE, rng.uniform(220, 320)) * 0.5
    creak += bandpass_fft(rng.standard_normal(cn), 800, 3000) * np.sin(np.linspace(0, np.pi, cn)) * \
        (1 + 0.6 * np.sin(2 * np.pi * rng.uniform(40, 60) * t)) * 0.3   # 나무 경첩 결
    x[cs:cs + cn] += creak
    ls = cs + cn - int(0.03 * RATE)
    lid = wood_knock(0.18, 200, 3500, rng.uniform(120, 170), 28.0) * strike_env(int(0.18 * RATE), 0.04)
    x[ls:ls + len(lid)] += lid
    root = rng.uniform(1300, 1600)
    for i, ratio in enumerate((1.0, 1.5)):   # 반짝 = 완전5도 두 톡
        s = ls + int((0.06 + i * 0.07) * RATE)
        bn = min(int(0.3 * RATE), len(x) - s)
        if bn > 0:
            x[s:s + bn] += bell(bn, root * ratio, 0.07) * 0.25
    return normalize(fade_tail(x), 0.75)


BAG_SOUNDS = {   # DESIGN 8.12 「가방·상자 소리」 — 3가지 × 3
    "BagDrop": make_bag_drop, "BagPickup": make_bag_pickup, "ChestOpen": make_chest_open,
}


def make_ranch_welcome():
    # 목장 들여보냄 = 맑은 「뿅」(아래서 위로 휘는 사인) + 목에 단 작은 방울 딸랑 서너 번(점점 작게).
    d = rng.uniform(0.5, 0.9)
    x = np.zeros(int(d * RATE))
    pn = int(rng.uniform(0.12, 0.16) * RATE)
    f0 = rng.uniform(500, 620)
    pop = chirp(pn / RATE, f0, f0 * rng.uniform(2.2, 2.6), curve=0.5, harm=0.1)
    x[:pn] += pop * np.sin(np.linspace(0, np.pi, pn)) ** 0.7 * 0.8
    ring = rng.uniform(2600, 3200)
    k = int(pn * 0.8)
    for j in range(rng.integers(3, 5)):   # 방울 = 같은 높이 짧은 종, 흔들림이 잦아들듯 점점 작게
        bn = min(int(0.25 * RATE), len(x) - k)
        if bn <= 0:
            break
        x[k:k + bn] += bell(bn, ring * rng.uniform(0.98, 1.02), 0.05) * 0.35 * 0.7 ** j
        k += int(rng.uniform(0.07, 0.1) * RATE)
    return normalize(fade_tail(x), 0.7)


def make_ranch_release():
    # 목장 풀어 줌 = 목줄 고리 딸깍 → 발굽 총총 네댓 번이 멀어짐(점점 작고 먹먹하게).
    d = rng.uniform(0.5, 0.9)
    x = np.zeros(int(d * RATE))
    click = metal_tick(0.05, 2500, 8000, rng.uniform(3000, 3600), 110.0)
    x[:len(click)] += click * 0.6
    k = int(rng.uniform(0.1, 0.14) * RATE)
    steps = rng.integers(4, 6)
    for j in range(steps):
        sn = int(0.06 * RATE)
        if k + sn >= len(x):
            break
        far = j / steps
        hoof = wood_knock(sn / RATE, 200, 4000 - 2500 * far, rng.uniform(180, 240), 60.0) * strike_env(sn, 0.012)
        x[k:k + sn] += hoof * 0.6 * (1 - 0.75 * far)
        k += int(rng.uniform(0.08, 0.11) * RATE)
    return normalize(fade_tail(x), 0.65)


def make_ranch_expand():
    # 목장 칸 늘림 = 나무 망치 땅땅 두 번(말뚝 박기) → 판자가 땅에 놓이는 낮은 털썩 + 나무 결.
    d = rng.uniform(0.8, 1.2)
    x = np.zeros(int(d * RATE))
    k = 0
    for j in range(2):
        hn = int(0.14 * RATE)
        hit = wood_knock(hn / RATE, 400, 5000, rng.uniform(260, 330), 35.0) * strike_env(hn, 0.03)
        x[k:k + hn] += hit * (0.9 if j else 0.75)
        k += int(rng.uniform(0.2, 0.26) * RATE)
    tn = min(int(0.3 * RATE), len(x) - k - int(0.01 * RATE))
    x[k:k + tn] += normalize(thud(tn / RATE, rng.uniform(70, 95)) * strike_env(tn, 0.07), 0.8)
    pn = int(0.16 * RATE)
    s = k + int(0.02 * RATE)
    if s + pn < len(x):   # 판자끼리 부딪는 나무 결
        x[s:s + pn] += wood_knock(pn / RATE, 300, 3000, rng.uniform(140, 180), 30.0) * strike_env(pn, 0.035) * 0.4
    return normalize(fade_tail(x), 0.8)


RANCH_SOUNDS = {   # DESIGN 3.2 「목장 소리」 — 3가지 × 3
    "RanchWelcome": make_ranch_welcome, "RanchRelease": make_ranch_release, "RanchExpand": make_ranch_expand,
}


def wood_creak(n, f0, f1):
    """나무 삐걱 — 마찰 잡음을 좁은 띠로 걸러 음높이가 f0→f1 로 미끄러지는 울림."""
    t = np.linspace(0, 1, n)
    x = one_pole_lowpass(rng.standard_normal(n), f0 + (f1 - f0) * t)
    x = x * np.sin(2 * np.pi * (f0 + (f1 - f0) * t) * 0.5 * t * n / RATE) ** 2 * 0.5 + x * 0.5
    return x * np.sin(np.pi * t) ** 0.8


def make_boat_launch():
    # 배 띄움 = 물 첨벙(굵은 잡음 + 물방울) → 선체가 수면에 닿는 나무 쿵.
    d = rng.uniform(0.7, 0.95)
    x = np.zeros(int(d * RATE))
    sp = burst(0.55, 300, 5000, 0.005, 7.0)
    x[:len(sp)] += normalize(sp, 0.7)
    for _ in range(8):
        s = int(rng.uniform(0.08, 0.5) * RATE)
        dr = chirp(0.04, rng.uniform(900, 1800), rng.uniform(1800, 3000), 0.5, 0)
        x[s:s + len(dr)] += dr * 0.12
    k = int(rng.uniform(0.1, 0.16) * RATE)
    hn = int(0.22 * RATE)
    x[k:k + hn] += wood_knock(hn / RATE, 150, 2500, rng.uniform(95, 130), 22.0) * strike_env(hn, 0.06) * 0.9
    return normalize(fade_tail(x), 0.8)


def make_boat_board():
    # 배 탐 = 나무 삐걱(음이 위로) + 발 디딤 덜컥, 선체가 살짝 흔들려 낮은 삐걱이 뒤따름.
    d = rng.uniform(0.45, 0.65)
    x = np.zeros(int(d * RATE))
    cn = int(rng.uniform(0.18, 0.24) * RATE)
    x[:cn] += normalize(wood_creak(cn, rng.uniform(500, 650), rng.uniform(900, 1200)), 0.55)
    k = int(rng.uniform(0.2, 0.26) * RATE)
    hn = int(0.12 * RATE)
    x[k:k + hn] += wood_knock(hn / RATE, 200, 3500, rng.uniform(150, 190), 40.0) * strike_env(hn, 0.025) * 0.8
    k2 = k + int(0.07 * RATE)
    c2 = min(int(0.16 * RATE), len(x) - k2)
    x[k2:k2 + c2] += normalize(wood_creak(c2, rng.uniform(380, 460), rng.uniform(300, 360)), 0.35)
    return normalize(fade_tail(x), 0.75)


def make_boat_leave():
    # 배 내림 = 낮은 삐걱(음이 아래로) + 뭍에 발 착지 털썩.
    d = rng.uniform(0.45, 0.65)
    x = np.zeros(int(d * RATE))
    cn = int(rng.uniform(0.16, 0.22) * RATE)
    x[:cn] += normalize(wood_creak(cn, rng.uniform(560, 680), rng.uniform(320, 400)), 0.5)
    k = int(rng.uniform(0.18, 0.24) * RATE)
    tn = min(int(0.2 * RATE), len(x) - k - int(0.01 * RATE))
    x[k:k + tn] += normalize(thud(tn / RATE, rng.uniform(75, 100)) * strike_env(tn, 0.05), 0.8)
    return normalize(fade_tail(x), 0.7)


BOAT_SOUNDS = {   # DESIGN 8.9 「배 소리」 — 3가지 × 3
    "BoatLaunch": make_boat_launch, "BoatBoard": make_boat_board, "BoatLeave": make_boat_leave,
}


def make_craft_hammer():
    # 망치질 = 쇠 망치가 두 번 쾅(두 번째가 살짝 약하고 낮게) — 쇠 울림이 뒤에 남음.
    d = rng.uniform(0.5, 0.68)
    x = np.zeros(int(d * RATE))
    for start, ring, gain in ((0.0, rng.uniform(1500, 1900), 1.0), (rng.uniform(0.22, 0.28), rng.uniform(1200, 1500), 0.75)):
        s0 = int(start * RATE)
        hn = min(int(0.2 * RATE), len(x) - s0)
        x[s0:s0 + hn] += gain * normalize(metal_tick(hn / RATE, 800, 6000, ring, 22.0), 0.7)[:hn]
        kn = min(int(0.12 * RATE), len(x) - s0)
        x[s0:s0 + kn] += gain * 0.8 * normalize(thud(kn / RATE, rng.uniform(110, 150)) * strike_env(kn, 0.04), 0.8)
    return normalize(fade_tail(x), 0.8)


def make_craft_smelt():
    # 제련 = 쇠 쿵 한 번 + 뜨거운 김이 새는 쉭(고음 잡음이 천천히 사그라듦).
    d = rng.uniform(0.65, 0.95)
    x = np.zeros(int(d * RATE))
    kn = int(0.18 * RATE)
    x[:kn] += normalize(metal_tick(kn / RATE, 500, 4000, rng.uniform(700, 950), 16.0), 0.7)
    x[:kn] += 0.6 * normalize(thud(kn / RATE, rng.uniform(70, 95)) * strike_env(kn, 0.05), 0.8)
    s0 = int(rng.uniform(0.05, 0.1) * RATE)
    hn = len(x) - s0
    hiss = burst(hn / RATE, 3000, 9000, 0.06, 3.2)
    x[s0:] += normalize(hiss, 0.55)[:hn]
    return normalize(fade_tail(x), 0.75)


def make_craft_sizzle():
    # 요리 = 뜨거운 기름 지글 — 고음 잡음에 작은 톡톡 튀는 소리가 계속 섞임.
    d = rng.uniform(0.65, 0.95)
    n = int(d * RATE)
    t = np.linspace(0, 1, n)
    x = bandpass_fft(rng.standard_normal(n), 2500, 10000) * (np.minimum(t / 0.05, 1.0) * np.exp(-1.6 * t))
    x = normalize(x, 0.5)
    for _ in range(26):
        s0 = int(rng.uniform(0.0, 0.8) * n)
        pop = burst(rng.uniform(0.006, 0.014), 2000, 9000, 0.0005, 300.0) * rng.uniform(0.15, 0.4)
        pn = min(len(pop), n - s0)
        x[s0:s0 + pn] += pop[:pn]
    return normalize(fade_tail(x), 0.7)


CRAFT_SOUNDS = {   # DESIGN 8.10 「제작 소리」 — 3가지 × 3
    "CraftHammer": make_craft_hammer, "CraftSmelt": make_craft_smelt, "CraftSizzle": make_craft_sizzle,
}


def make_zone_warp():
    # 포털 = 바람이 가르는 쉭(대역이 위로 올랐다 사그라듦) + 낮은 울림. 0.9초 안에 끝남(맵이 바뀌며 끊기기 전).
    d = rng.uniform(0.62, 0.84)
    n = int(d * RATE)
    t = np.linspace(0, 1, n)
    env = np.minimum(t / 0.25, 1.0) * np.exp(-3.0 * np.maximum(t - 0.25, 0))
    x = bandpass_fft(rng.standard_normal(n), rng.uniform(500, 800), rng.uniform(3500, 5000)) * env
    x = normalize(x, 0.6)
    hn = int(0.5 * RATE)
    x[:hn] += 0.5 * normalize(thud(hn / RATE, rng.uniform(55, 75)) * strike_env(hn, 0.1), 0.8)
    return normalize(fade_tail(x), 0.8)


def make_mine_door():
    # 광산 문 = 돌이 끄는 드르륵(낮은 잡음이 떨림) + 끝에 돌 쿵.
    d = rng.uniform(0.7, 0.95)
    n = int(d * RATE)
    t = np.linspace(0, 1, n)
    rumble = bandpass_fft(rng.standard_normal(n), 80, 900) * (1.0 + 0.5 * np.sin(2 * np.pi * rng.uniform(18, 28) * t * d))
    x = normalize(rumble * np.minimum(t / 0.08, 1.0) * np.where(t < 0.7, 1.0, 0.0), 0.6)
    k = int(rng.uniform(0.62, 0.7) * d * RATE)
    tn = min(int(0.25 * RATE), n - k)
    x[k:k + tn] += normalize(thud(tn / RATE, rng.uniform(55, 80)) * strike_env(tn, 0.07), 0.9)[:tn]
    return normalize(fade_tail(x), 0.8)


WARP_SOUNDS = {   # DESIGN 8.10 「존·광산 건너기 소리」 — 2가지 × 3 (나룻배는 BoatBoard 빌림)
    "ZoneWarp": make_zone_warp, "MineDoor": make_mine_door,
}


def make_emote_pop():
    # 말풍선 = 짧고 말랑한 톡(음이 위로 살짝 미끄러지는 사인 + 빠른 감쇠). 0.15~0.3초.
    d = rng.uniform(0.17, 0.26)
    n = int(d * RATE)
    t = np.linspace(0, 1, n)
    f = rng.uniform(520, 700) * (1.0 + 0.35 * (1 - np.exp(-8 * t)))
    ph = 2 * np.pi * np.cumsum(f) / RATE
    x = np.sin(ph) + 0.25 * np.sin(2 * ph)
    x = x * np.minimum(t * d / 0.004, 1.0) * np.exp(-9 * t)
    return normalize(fade_tail(x), 0.7)


EMOTE_SOUNDS = {   # DESIGN 2.2 「말풍선 소리」 — 1가지 × 3
    "EmotePop": make_emote_pop,
}


def make_deny():
    # 막힘 = 낮고 짧은 둔탁한 톡(음이 아래로 살짝 떨어지는 사인 + 빠른 감쇠). 0.12~0.25초.
    d = rng.uniform(0.13, 0.22)
    n = int(d * RATE)
    t = np.linspace(0, 1, n)
    f = rng.uniform(150, 200) * (1.0 - 0.3 * (1 - np.exp(-7 * t)))
    ph = 2 * np.pi * np.cumsum(f) / RATE
    x = np.sin(ph) + 0.35 * np.sin(2 * ph)
    x = x * np.minimum(t * d / 0.003, 1.0) * np.exp(-8 * t)
    return normalize(fade_tail(x), 0.7)


DENY_SOUNDS = {   # DESIGN 2.2 「막힘 소리」 — 1가지 × 3
    "Deny": make_deny,
}


def make_gasp():
    # 숨참 = 낮고 쉰 「하아」(대역 잡음 + 낮은 숨결 음, 부드럽게 올랐다 느리게 사라짐). 0.5~0.8초.
    d = rng.uniform(0.55, 0.78)
    n = int(d * RATE)
    t = np.linspace(0, 1, n)
    breath = bandpass_fft(rng.standard_normal(n), 300, 1800)
    f = rng.uniform(110, 150) * (1.0 - 0.2 * t)
    hum = np.sin(2 * np.pi * np.cumsum(f) / RATE)
    env = np.minimum(t * d / 0.08, 1.0) * np.exp(-3.2 * t)
    return normalize(fade_tail((breath + 0.35 * hum) * env), 0.7)


GASP_SOUNDS = {   # DESIGN 2.2 「숨참 소리」 — 1가지 × 3
    "Gasp": make_gasp,
}


def make_shiver():
    # 추위 떨림 = 높고 가는 「부르르」(높은 음을 빠르게 떨어 울림 + 얇은 잡음). 0.5~0.8초.
    d = rng.uniform(0.52, 0.78)
    n = int(d * RATE)
    t = np.linspace(0, 1, n)
    f0 = rng.uniform(1500, 1900)
    trem = 0.5 + 0.5 * np.sin(2 * np.pi * rng.uniform(16, 22) * t * d)
    tone = np.sin(2 * np.pi * np.cumsum(f0 * (1.0 - 0.08 * t)) / RATE)
    hiss = bandpass_fft(rng.standard_normal(n), 2500, 6000)
    env = np.minimum(t * d / 0.05, 1.0) * np.exp(-2.4 * t)
    return normalize(fade_tail((tone + 0.25 * hiss) * trem * env), 0.7)


SHIVER_SOUNDS = {   # DESIGN 2.2 「추위 떨림 소리」 — 1가지 × 3
    "Shiver": make_shiver,
}


def make_warmth():
    # 불 옆 온기 = 낮고 부드러운 「후우」(낮은 숨결 대역 잡음 + 낮은 음, 천천히 올랐다 길게 사라짐). 0.6~0.9초.
    d = rng.uniform(0.62, 0.88)
    n = int(d * RATE)
    t = np.linspace(0, 1, n)
    breath = bandpass_fft(rng.standard_normal(n), 150, 700)
    f = rng.uniform(95, 125) * (1.0 - 0.15 * t)
    hum = np.sin(2 * np.pi * np.cumsum(f) / RATE)
    env = np.minimum(t * d / 0.18, 1.0) * np.exp(-2.6 * t)
    return normalize(fade_tail((breath + 0.3 * hum) * env), 0.7)


WARMTH_SOUNDS = {   # DESIGN 2.2 「불 옆 온기 소리」 — 1가지 × 3
    "Warmth": make_warmth,
}


def make_revive():
    # 부활 = 숨을 들이쉬며 오르는 짧은 음(대역 잡음 숨 + 오르는 사인). 0.6~0.9초.
    d = rng.uniform(0.62, 0.88)
    n = int(d * RATE)
    t = np.linspace(0, 1, n)
    breath = bandpass_fft(rng.standard_normal(n), 400, 1800)
    f = rng.uniform(260, 320) * (1.0 + 0.9 * t)
    tone = np.sin(2 * np.pi * np.cumsum(f) / RATE)
    env = np.minimum(t * d / 0.12, 1.0) * np.exp(-2.2 * t ** 2)
    return normalize(fade_tail((0.5 * breath + tone) * env), 0.75)


REVIVE_SOUNDS = {   # DESIGN 2.2 「부활 소리」 — 1가지 × 3
    "Revive": make_revive,
}


def make_heartbeat():
    # 심장 소리 = 낮은 북소리 두 번(쿵-쿵). 둘째가 작고 약간 높다. 0.4~0.6초.
    d = rng.uniform(0.42, 0.58)
    n = int(d * RATE)
    t = np.arange(n) / RATE
    out = np.zeros(n)
    for start, f0, amp in ((0.0, rng.uniform(52, 62), 1.0), (rng.uniform(0.17, 0.21), rng.uniform(60, 72), 0.7)):
        k = np.clip(t - start, 0, None)
        thump = np.sin(2 * np.pi * (f0 + 35 * np.exp(-k * 40)) * k) * np.exp(-k * 22) * (t >= start)
        out += amp * thump
    return normalize(fade_tail(out), 0.8)


HEARTBEAT_SOUNDS = {   # DESIGN 2.2 「위험 체력 알림」 — 1가지 × 3
    "Heartbeat": make_heartbeat,
}


def convert_kenney():
    written = []
    for name, (pack, prefix) in KENNEY_MAP.items():
        folder = os.path.join(KENNEY, pack, "Audio")
        files = sorted(f for f in os.listdir(folder) if f.startswith(prefix) and f.endswith(".ogg"))
        if not files:
            raise SystemExit(f"Kenney 원본이 없다: {pack}/{prefix}*")
        for i, f in enumerate(files[:5], 1):
            dest = os.path.join(OUT, f"SFX_{name}_{i:02d}.wav")
            subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-i", os.path.join(folder, f),
                            "-ac", "1", "-ar", str(RATE), dest], check=True)
            written.append(dest)
    return written


def make_rocket_launch():
    # 바주카 발사 = 「퐁」 낮은 쿵 + 쉬익 뿜는 소리(중대역 잡음이 서서히 줄어듦). 0.7~0.9초.
    d = rng.uniform(0.7, 0.9)
    n = int(d * RATE)
    t = np.linspace(0, d, n)
    thump = np.sin(2 * np.pi * np.cumsum(70 + 90 * np.exp(-18 * t)) / RATE) * np.exp(-9 * t)
    hiss = bandpass_fft(rng.standard_normal(n), 400, 4500) * np.minimum(t / 0.02, 1.0) * np.exp(-4.0 * t)
    return normalize(fade_tail(thump * 1.2 + hiss * 0.7), 0.8)


def make_explosion():
    # 폭발 = 낮게 떨어지는 쿵(굵은 사인) + 넓은 대역 잡음이 길게 우르릉 사라짐. 1.0~1.3초.
    d = rng.uniform(1.0, 1.3)
    n = int(d * RATE)
    t = np.linspace(0, d, n)
    boom = np.sin(2 * np.pi * np.cumsum(120 * np.exp(-3.5 * t) + 35) / RATE) * np.exp(-3.2 * t)
    crack = bandpass_fft(rng.standard_normal(n), 200, 7000) * np.minimum(t / 0.003, 1.0) * np.exp(-9 * t)
    rumble = bandpass_fft(rng.standard_normal(n), 40, 400) * np.minimum(t / 0.02, 1.0) * np.exp(-2.6 * t)
    return normalize(fade_tail(boom * 1.2 + crack * 0.8 + rumble * 1.0), 0.85)


ROCKET_SOUNDS = {   # DESIGN 8.6-c 바주카 — 발사·폭발 2가지 × 3
    "RocketLaunch": make_rocket_launch,
    "Explosion": make_explosion,
}


def main():
    os.makedirs(OUT, exist_ok=True)
    for f in os.listdir(OUT):
        if f.endswith(".wav"):
            os.remove(os.path.join(OUT, f))
    written = convert_kenney()
    written.append(write_wav("SFX_Amb_Ocean_01", make_ocean()))
    written.append(write_wav("SFX_Amb_Wind_01", make_wind()))
    for i in range(1, 7):
        written.append(write_wav(f"SFX_Bird_{i:02d}", make_bird()))
    for i in range(1, 4):
        written.append(write_wav(f"SFX_Splash_{i:02d}", make_splash()))
        written.append(write_wav(f"SFX_Swim_{i:02d}", make_swim()))
        written.append(write_wav(f"SFX_Swing_{i:02d}", make_swing()))
    # M16-02 도구별 소리 — 위 소리의 난수 순서를 안 흔들려고 맨 끝에 둔다.
    written.append(write_wav("SFX_Chainsaw_Idle_01", make_engine(37.5, 1800)))
    written.append(write_wav("SFX_Chainsaw_Rev_01", make_engine(95.0, 4200)))
    written.append(write_wav("SFX_ElectricHum_01", make_electric_hum()))
    written.append(write_wav("SFX_Reel_01", make_reel()))
    for i in range(1, 4):
        written.append(write_wav(f"SFX_Cast_{i:02d}", make_cast()))
        written.append(write_wav(f"SFX_Bite_{i:02d}", make_bite()))
        written.append(write_wav(f"SFX_Club_{i:02d}", make_club()))
        written.append(write_wav(f"SFX_Machete_{i:02d}", make_machete()))
    for i in range(1, 4):   # M21-03b 포획 — 위 소리 난수 순서를 안 흔들려고 맨 끝
        written.append(write_wav(f"SFX_Cinch_{i:02d}", make_cinch()))
    for i in range(1, 4):   # M39-06c 사냥 총 — 위 소리 난수 순서를 안 흔들려고 맨 끝
        written.append(write_wav(f"SFX_PistolShot_{i:02d}", make_pistol_shot()))
        written.append(write_wav(f"SFX_GunEmptyClick_{i:02d}", make_empty_click()))
        written.append(write_wav(f"SFX_BulletImpact_{i:02d}", make_bullet_impact()))
    written.append(write_wav("SFX_PistolReload_01", make_pistol_reload()))
    for name, make in ANIMAL_SOUNDS.items():   # M43-01 동물 소리 — 위 소리 난수 순서를 안 흔들려고 맨 끝
        for i in range(1, 4):
            written.append(write_wav(f"SFX_{name}_{i:02d}", make()))
    for name, make in PLAYER_SOUNDS.items():   # M44-01 플레이어 몸 소리 — 위 소리 난수 순서를 안 흔들려고 맨 끝
        for i in range(1, 4):
            written.append(write_wav(f"SFX_{name}_{i:02d}", make()))
    for name, make in BASE_SOUNDS.items():   # M45-01 거점·상자 소리 — 위 소리 난수 순서를 안 흔들려고 맨 끝
        for i in range(1, 4):
            written.append(write_wav(f"SFX_{name}_{i:02d}", make()))
    # M46-01 존 소리 풍경 — 위 소리 난수 순서를 안 흔들려고 맨 끝
    written.append(write_wav("SFX_Amb_Swamp_01", make_amb_swamp()))
    written.append(write_wav("SFX_Amb_Night_01", make_amb_night()))
    for i in range(1, 4):
        written.append(write_wav(f"SFX_Footstep_Mud_{i:02d}", make_footstep_mud()))
    for name, make in EQUIP_SOUNDS.items():   # M47-01 장비·탈것 소리 — 위 소리 난수 순서를 안 흔들려고 맨 끝
        for i in range(1, 4):
            written.append(write_wav(f"SFX_{name}_{i:02d}", make()))
    for name, make in NOTICE_SOUNDS.items():   # M48-01 달성 알림 소리 — 위 소리 난수 순서를 안 흔들려고 맨 끝
        for i in range(1, 3):
            written.append(write_wav(f"SFX_{name}_{i:02d}", make()))
    for name, make in BAG_SOUNDS.items():   # M49-01 가방·상자 소리 — 위 소리 난수 순서를 안 흔들려고 맨 끝
        for i in range(1, 4):
            written.append(write_wav(f"SFX_{name}_{i:02d}", make()))
    for name, make in RANCH_SOUNDS.items():   # M50-01 목장 소리 — 위 소리 난수 순서를 안 흔들려고 맨 끝
        for i in range(1, 4):
            written.append(write_wav(f"SFX_{name}_{i:02d}", make()))
    for name, make in BOAT_SOUNDS.items():   # M51-01 배 소리 — 위 소리 난수 순서를 안 흔들려고 맨 끝
        for i in range(1, 4):
            written.append(write_wav(f"SFX_{name}_{i:02d}", make()))
    for name, make in CRAFT_SOUNDS.items():   # M52-01 제작 소리 — 위 소리 난수 순서를 안 흔들려고 맨 끝
        for i in range(1, 4):
            written.append(write_wav(f"SFX_{name}_{i:02d}", make()))
    for name, make in WARP_SOUNDS.items():   # M53-01 존·광산 건너기 소리 — 위 소리 난수 순서를 안 흔들려고 맨 끝
        for i in range(1, 4):
            written.append(write_wav(f"SFX_{name}_{i:02d}", make()))
    for name, make in EMOTE_SOUNDS.items():   # M54-01 말풍선 소리 — 위 소리 난수 순서를 안 흔들려고 맨 끝
        for i in range(1, 4):
            written.append(write_wav(f"SFX_{name}_{i:02d}", make()))
    for name, make in DENY_SOUNDS.items():   # M55-01 막힘 소리 — 위 소리 난수 순서를 안 흔들려고 맨 끝
        for i in range(1, 4):
            written.append(write_wav(f"SFX_{name}_{i:02d}", make()))
    for name, make in GASP_SOUNDS.items():   # M56-01 숨참 소리 — 위 소리 난수 순서를 안 흔들려고 맨 끝
        for i in range(1, 4):
            written.append(write_wav(f"SFX_{name}_{i:02d}", make()))
    for name, make in SHIVER_SOUNDS.items():   # M57-01 추위 떨림 소리 — 위 소리 난수 순서를 안 흔들려고 맨 끝
        for i in range(1, 4):
            written.append(write_wav(f"SFX_{name}_{i:02d}", make()))
    for name, make in WARMTH_SOUNDS.items():   # M58-01 불 옆 온기 소리 — 위 소리 난수 순서를 안 흔들려고 맨 끝
        for i in range(1, 4):
            written.append(write_wav(f"SFX_{name}_{i:02d}", make()))
    for name, make in REVIVE_SOUNDS.items():   # M59-01 부활 소리 — 위 소리 난수 순서를 안 흔들려고 맨 끝
        for i in range(1, 4):
            written.append(write_wav(f"SFX_{name}_{i:02d}", make()))
    for name, make in HEARTBEAT_SOUNDS.items():   # M60-01 심장 소리 — 위 소리 난수 순서를 안 흔들려고 맨 끝
        for i in range(1, 4):
            written.append(write_wav(f"SFX_{name}_{i:02d}", make()))
    written.append(write_wav("SFX_Amb_Rain_01", make_amb_rain()))   # M104-03b 날씨 빗소리 고리 — 위 소리 난수 순서를 안 흔들려고 맨 끝
    for name, make in ROCKET_SOUNDS.items():   # 바주카 2b 발사·폭발 소리 — 위 소리 난수 순서를 안 흔들려고 맨 끝
        for i in range(1, 4):
            written.append(write_wav(f"SFX_{name}_{i:02d}", make()))
    # M108-07d 연료 발전기 공회전 고리 — 전기톱 공회전(37.5Hz)보다 낮은 맥 25Hz·거친 잡음 위 끝도 낮게(묵직한 기계).
    # 2초에 맥 50번·출렁임 6번이 딱 떨어져 고리 이음매가 없다. 위 소리 난수 순서를 안 흔들려고 맨 끝.
    written.append(write_wav("SFX_Generator_Loop_01", make_engine(25.0, 1200)))
    # M108-08d-2 라디오 가락 고리 — 위 소리 난수 순서를 안 흔들려고 맨 끝.
    written.append(write_wav("SFX_Radio_Loop_01", make_radio()))
    print(f"{len(written)} 개 → {OUT}")


if __name__ == "__main__":
    main()
