-- SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
-- SPDX-License-Identifier: Apache-2.0

-- Kite Rush radio dashboard, an EdgeTX color LCD widget for the Zephyr
-- samples/subsys/input/kite_rush game.
--
-- The game reads the sticks over CRSF and reports back in CRSF "Game" frames
-- (type 0x3C, extended header: destination 0xEA radio, origin 0xC8 flight
-- controller, sub-command, payload). EdgeTX passes frame types it does not
-- decode itself to Lua through crossfireTelemetryPop(). Sub-commands:
--   0x01 add points  int16 score delta
--   0x02 command     uint16 (event << 8) | argument
--   0x10 state       phase, level, score, best, time, combo, speed, gates,
--                    kite roll and altitude, flags, version (22 bytes)
-- Multi-byte fields are big-endian.

local floor, sin, cos, sqrt = math.floor, math.sin, math.cos, math.sqrt
local min, max = math.min, math.max
local fmt = string.format

local RGB = lcd.RGB
local fillRect = lcd.drawFilledRectangle
local drawRect = lcd.drawRectangle
local drawTri = lcd.drawFilledTriangle
local drawText = lcd.drawText
local drawLine = lcd.drawLine
local fillCircle = lcd.drawFilledCircle
local drawAnnulus = lcd.drawAnnulus
local sizeText = lcd.sizeText

local PLAY_NOW = PLAY_NOW or 0x10
local F_SML = SMLSIZE
local F_BOLD = BOLD
local F_MID = MIDSIZE
local F_DBL = DBLSIZE
local F_XL = XLSIZE or DBLSIZE
local F_XXL = XXLSIZE

-- Protocol
local FRAME_GAME = 0x3C
local ADDR_RADIO = 0xEA
local SUB_POINTS = 0x01
local SUB_COMMAND = 0x02
local SUB_STATE = 0x10
local STATE_LEN = 25 -- destination, origin, sub-command and 22 bytes (version 1)

local EV_COUNTDOWN = 0x01
local EV_GO = 0x02
local EV_GATE = 0x03
local EV_PERFECT = 0x04
local EV_MISS = 0x05
local EV_HIT = 0x06
local EV_LEVEL_UP = 0x07
local EV_GAME_OVER = 0x08
local EV_NEW_BEST = 0x09
local EV_BARREL_ROLL = 0x0A
local EV_SQUASH = 0x0B
local EV_HIGH_SCORE = 0x0C
local EV_RETRO = 0x0D

local PH_ATTRACT = 0
local PH_COUNTDOWN = 1
local PH_FLYING = 2
local PH_OVER = 3
local PH_LOST = 4
local PH_NAME = 5 -- shown as game over, with a prompt

local FLAG_ARMED = 0x01
local FLAG_ROLL_READY = 0x02

local LINK_TIMEOUT = 150 -- 10 ms ticks without a game frame

-- Palette
local function hex(c)
  return RGB((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF)
end

local function mixHex(a, b, t)
  local r = ((a >> 16) & 0xFF) * (1 - t) + ((b >> 16) & 0xFF) * t
  local g = ((a >> 8) & 0xFF) * (1 - t) + ((b >> 8) & 0xFF) * t
  local bl = (a & 0xFF) * (1 - t) + (b & 0xFF) * t
  return (floor(r) << 16) | (floor(g) << 8) | floor(bl)
end

local BG = 0x1A1033
local C_BG = hex(BG)
local C_SHADE = hex(0x0B0618)
local C_PANEL = hex(0x2A1C52)
local C_LINE = hex(0x3B2B6B)
local C_TEXT = hex(0xF4F0FF)
local C_MUTED = hex(0x9A8CC8)
local C_DIM = hex(0x5E5288)
local C_CYAN = hex(0x00AEFF)
local C_SKY = hex(0xB1E4FA)
local C_LILAC = hex(0xAF7FE4)
local C_GOLD = hex(0xFFD319)
local C_ORANGE = hex(0xFF901F)
local C_PINK = hex(0xFF2975)
local C_RED = hex(0xFF3B4F)
local C_GREEN = hex(0x3EE08F)
local C_WHITE = hex(0xFFFFFF)

-- Zephyr kite: the official logo (doc/_static/images/logo.svg). The body
-- facets are cut into bands along their gradients. Coordinates in SVG units
-- around the bounding box centre; vertices 1..9 are the tail, KW is their
-- position along it (0 at the body, 1 at the tip).
local KV = {3.4,40.8, -28.4,54.3, -1.3,68.0, -49.2,34.8, -65.3,58.6, -99.3,45.9,
  -91.4,72.6, -95.2,16.4, -125.0,20.3, 23.7,22.2, 23.7,36.3, 43.9,3.6, 43.9,31.8,
  64.2,-15.1, 64.2,27.2, 84.4,-33.7, 84.4,22.7, 87.3,-36.3, 104.7,-13.2, 104.7,18.2,
  125.0,13.7, 33.3,-53.7, 39.2,-72.6, 64.6,-53.5, 27.3,-34.8, 85.1,-34.3, 21.3,-15.9,
  64.7,-15.5, 15.4,3.0, 44.3,3.2, 9.4,21.9, 23.9,22.0, 114.8,-62.9, 79.3,-42.3,
  113.2,-72.6, 125.0,-72.6, 71.3,-48.4, 94.7,-72.6, 63.3,-54.5, 76.2,-72.6,
  55.3,-60.5, 57.7,-72.6, 47.2,-66.6, 125.0,-58.3, 107.4,-55.7, 125.0,-43.9,
  89.9,-38.8, 125.0,-29.5, 95.6,-25.2, 125.0,-15.1, 105.4,-12.3, 125.0,-0.7,
  115.2,0.7}
local KW = {0, 0.27, 0.21, 0.41, 0.55, 0.79, 0.77, 0.78, 1.0}
local KT = {1,2,3,1, 4,2,5,2, 5,6,7,3, 8,6,9,2, 1,10,11,1, 10,12,13,1, 10,13,11,1,
  12,14,15,4, 12,15,13,4, 14,16,17,5, 14,17,15,5, 16,18,19,6, 16,19,20,6,
  16,20,17,6, 19,21,20,7, 22,23,24,8, 25,22,24,9, 25,24,18,9, 25,18,26,9,
  27,25,26,10, 27,26,28,10, 29,27,28,11, 29,28,30,11, 31,29,30,3, 31,30,32,3,
  1,31,32,3, 33,18,34,12, 35,36,33,13, 35,33,34,13, 35,34,37,13, 38,35,37,14,
  38,37,39,14, 40,38,39,15, 40,39,41,15, 42,40,41,16, 42,41,43,16, 23,42,43,16,
  44,36,45,17, 46,44,45,18, 46,45,47,18, 48,46,47,19, 48,47,18,19, 48,18,49,19,
  50,48,49,20, 50,49,51,20, 52,50,51,11, 52,51,53,11, 21,52,53,21}
local KC = {0x7929D2, 0xAF7FE4, 0x9454DB, 0x6734D0, 0x4D43CD, 0x3452CB, 0x1A60C8,
  0x1E9CF8, 0x3C8AF0, 0x5978E9, 0x7766E2, 0x6EB0DF, 0x7FBDE6, 0x90CAED, 0xA1D7F4,
  0xB1E4FA, 0x10A5FB, 0x2995F5, 0x4385EF, 0x5D76E8, 0x9056DC}
local K_W = 250 -- logo width in SVG units
-- outermost points of the logo, to keep the rotated kite on screen
local KEXT = {125, -72.6, 39.2, -72.6, -125, 20.3, -91.4, 72.6, 125, 13.7}
local NV = #KV // 2
local NW = #KW
local NT = #KT

local KX, KY, SX, SY = {}, {}, {}, {}
for i = 1, NV do
  KX[i], KY[i] = KV[2 * i - 1], KV[2 * i]
  SX[i], SY[i] = 0, 0
end

-- Kite color variants: normal, dimmed (no game), grey (game over), red (hit)
-- and hot (sparkle flash)
local PAL_N, PAL_DIM, PAL_GREY, PAL_RED, PAL_HOT = {}, {}, {}, {}, {}
for i = 1, #KC do
  local c = KC[i]
  local l = floor(((c >> 16) & 0xFF) * 0.3 + ((c >> 8) & 0xFF) * 0.59 + (c & 0xFF) * 0.11)
  PAL_N[i] = hex(c)
  PAL_DIM[i] = hex(mixHex(c, BG, 0.6))
  PAL_GREY[i] = hex(mixHex((l << 16) | (l << 8) | l, BG, 0.4))
  PAL_RED[i] = hex(mixHex(c, 0xFF2448, 0.7))
  PAL_HOT[i] = hex(mixHex(c, 0xFFFFFF, 0.6))
end

-- Synthwave backdrop. The sky darkens in SKY_LEVELS steps as the sun sets.
local function ramp(stops, n)
  local out = {}
  for i = 0, n - 1 do
    local p = i / (n - 1) * (#stops - 1)
    local k = min(floor(p), #stops - 2)
    out[i + 1] = mixHex(stops[k + 1], stops[k + 2], p - k)
  end
  return out
end

local function hexAll(t)
  for i = 1, #t do
    t[i] = hex(t[i])
  end
  return t
end

local SKY_BANDS = 9
local SKY_LEVELS = 8
local SKYS = {}
do
  local day = ramp({0x140B30, 0x2A1352, 0x5A1C72, 0xB5307C, 0xF0587A}, SKY_BANDS)
  local night = ramp({0x0F0824, 0x150B2E, 0x1C0F38, 0x251443, 0x30184E}, SKY_BANDS)
  for l = 1, SKY_LEVELS do
    local s = {}
    for i = 1, SKY_BANDS do
      s[i] = hex(mixHex(day[i], night[i], (l - 1) / (SKY_LEVELS - 1)))
    end
    SKYS[l] = s
  end
end
local SUN = hexAll(ramp({0xFFF07A, 0xFFB23E, 0xFF6F4F, 0xFF2D78}, 12))
local GRID = hexAll(ramp({0x4A1458, 0xFF2975}, 6))
local C_GROUND = hex(0x120926)
local C_HORIZON = hex(0xFF5FA2)
local C_STREAK = hex(0x8E7BD8)

local SPARK_N = 12
local SPX, SPY = {}, {}
for i = 1, SPARK_N do
  SPX[i] = cos(i * 2 * math.pi / SPARK_N + 0.3)
  SPY[i] = sin(i * 2 * math.pi / SPARK_N + 0.3)
end
local SPARK_COL = {C_WHITE, C_SKY, C_GOLD}

-- Sounds: frequency (Hz), duration (ms), pause (ms) triplets
local SND_LEVEL = {784, 60, 10, 988, 60, 10, 1175, 60, 10, 1568, 120, 0}
local SND_OVER = {659, 160, 40, 523, 160, 40, 392, 320, 0}
local SND_BEST = {1047, 70, 10, 1319, 70, 10, 1568, 70, 10, 2093, 200, 0}
local SND_PERFECT = {1760, 40, 20, 2349, 60, 0}
local SND_SQUASH = {900, 35, 10, 500, 60, 0}
local SND_RETRO = {523, 50, 10, 659, 50, 10, 784, 50, 10, 1047, 50, 10, 1319, 50, 10, 1568, 160, 0}

local options = {
  { "Sound", BOOL, 1 },
  { "Haptic", BOOL, 1 },
  { "Demo", BOOL, 0 },
}

-- Font heights, measured on first use
local FH

local function measureFonts()
  local function h(f)
    local _, fh = sizeText("0", f)
    return fh
  end
  FH = { sml = h(F_SML), std = h(0), bold = h(F_BOLD), mid = h(F_MID), dbl = h(F_DBL),
    xl = h(F_XL), xxl = h(F_XXL) }
end

----------------------------------------------------------------------------
-- Sound and haptics

-- Every instance of the widget sees the same events: the first one to handle
-- an event plays its effects, the others stay quiet.
local fxOn, fxKey, fxT = true, -1, 0

local function fxGate(w, key)
  fxOn = key ~= fxKey or w.now - fxT > 15
  fxKey, fxT = key, w.now
end

-- Queued jingles, dropped while the radio still has a backlog of ours
local function tones(w, s, prio)
  if not (fxOn and w.sound) then
    return
  end
  local now = getTime()
  if w.audioUntil < now then
    w.audioUntil = now
  end
  if w.audioUntil - now > (prio and 100 or 30) then
    return
  end
  for i = 1, #s, 3 do
    playTone(s[i], s[i + 1], s[i + 2], 0)
    w.audioUntil = w.audioUntil + (s[i + 1] + s[i + 2]) // 10 + 1
  end
end

-- Immediate blips, dropped by the radio while another one is playing
local function blip(w, f, d, sweep)
  if fxOn and w.sound then
    playTone(f, d, 0, PLAY_NOW, sweep or 0)
  end
end

local function buzz(w, d, n)
  if fxOn and w.haptic then
    for _ = 1, n do
      playHaptic(d, 60)
    end
  end
end

----------------------------------------------------------------------------
-- Telemetry

local function u16(d, i)
  return d[i] * 256 + d[i + 1]
end

local function u32(d, i)
  if d[i] > 0x7F then
    return 0x7FFFFFFF
  end
  return ((d[i] * 256 + d[i + 1]) * 256 + d[i + 2]) * 256 + d[i + 3]
end

-- Points float up from the kite; up to three at a time, stacked
local function popup(w, text)
  local slot = 0
  for i = 1, #w.pops do
    local q = w.pops[i]
    if q.t and w.now - q.t < 45 then
      slot = slot + 1
    end
  end
  local p = w.pops[w.popIdx]
  w.popIdx = w.popIdx % #w.pops + 1
  p.text, p.t, p.slot = text, w.now, slot % 3
end

-- Short callout above the kite (perfect, miss, hit, squash)
local function callout(w, text, color)
  w.wordText, w.wordColor, w.wordT = text, color, w.now
end

local function banner(w, text, color)
  w.bannerText, w.bannerColor, w.bannerT = text, color, w.now
end

local function onEvent(w, ev, arg)
  local now = w.now
  fxGate(w, (ev << 8) | arg)
  if ev == EV_COUNTDOWN then
    w.count, w.countT = arg, now
    w.newBestRun = false
    blip(w, 600, 100)
  elseif ev == EV_GO then
    w.goT = now
    w.count = 0
    w.newBestRun = false
    blip(w, 1200, 300)
    buzz(w, 40, 1)
  elseif ev == EV_GATE then
    if arg > w.combo then
      w.comboT = now
    end
    w.combo = arg
    blip(w, 700 + 60 * min(arg, 20), 50)
  elseif ev == EV_PERFECT then
    if arg > w.combo then
      w.comboT = now
    end
    w.combo = arg
    w.sparkT = now
    callout(w, "PERFECT!", C_SKY)
    tones(w, SND_PERFECT, false)
  elseif ev == EV_MISS then
    w.combo = 0
    callout(w, "MISS", C_MUTED)
    blip(w, 300, 150)
  elseif ev == EV_HIT then
    w.combo = 0
    w.hitT = now
    callout(w, "BUG HIT!", C_RED)
    blip(w, 150, 300)
    buzz(w, 120, 1)
  elseif ev == EV_LEVEL_UP then
    w.level = arg
    w.levelT = now
    banner(w, "LEVEL " .. arg, C_GREEN)
    tones(w, SND_LEVEL, true)
  elseif ev == EV_GAME_OVER then
    w.overT = now
    tones(w, SND_OVER, true)
  elseif ev == EV_NEW_BEST then
    w.newBestRun = true
    w.bestT = now
    if w.phase == PH_FLYING then
      banner(w, "NEW BEST!", C_GOLD)
    end
    tones(w, SND_BEST, true)
    buzz(w, 40, 3)
  elseif ev == EV_BARREL_ROLL then
    w.rollT = now
    w.rollDir = w.roll < 0 and -1 or 1
    blip(w, 500, 250, 6)
  elseif ev == EV_SQUASH then
    w.sparkT = now
    callout(w, "SQUASH!", C_GREEN)
    tones(w, SND_SQUASH, false)
  elseif ev == EV_HIGH_SCORE then
    tones(w, SND_BEST, true)
    buzz(w, 40, 2)
  elseif ev == EV_RETRO then
    banner(w, arg ~= 0 and "RETRO MODE" or "RETRO MODE OFF", C_GREEN)
    tones(w, SND_RETRO, true)
    buzz(w, 40, 2)
  end
end

local function onState(w, d)
  local phase = d[4]
  w.naming = phase == PH_NAME
  if w.naming then
    phase = PH_OVER
  elseif phase > PH_LOST then
    phase = PH_ATTRACT
  end
  if phase ~= w.phase then
    if w.phase == PH_OVER or phase == PH_COUNTDOWN then
      w.newBestRun = false
    end
    w.phase = phase
  end
  w.level = d[5]
  local score = u32(d, 6)
  if score < w.score then
    w.shown = score
  end
  w.score = score
  w.best = u32(d, 10)
  w.sun = u16(d, 14)
  w.sunMax = max(1, u16(d, 16))
  local combo = d[18]
  if combo > w.combo then
    w.comboT = w.now
  end
  w.combo = combo
  w.speed = min(100, d[19])
  w.gates = u16(d, 20)
  local roll = d[22]
  w.roll = roll > 127 and roll - 256 or roll
  w.alt = min(100, d[23])
  w.flags = d[24]
end

local function onGame(w, d)
  local n = #d
  if n < 3 or d[1] ~= ADDR_RADIO then
    return
  end
  w.lastRx = w.now
  local sub = d[3]
  if sub == SUB_STATE then
    if n >= STATE_LEN and d[25] >= 1 then
      onState(w, d)
    end
  elseif sub == SUB_COMMAND then
    if n >= 5 then
      onEvent(w, d[4], d[5])
    end
  elseif sub == SUB_POINTS then
    if n >= 5 then
      local pts = u16(d, 4)
      if pts > 32767 then
        pts = pts - 65536
      end
      w.score = max(0, w.score + pts)
      if pts > 0 then
        popup(w, "+" .. pts)
      end
    end
  end
end

----------------------------------------------------------------------------
-- Demo mode: a scripted game fed through the same frame parser

local DEMO_LEN = 4000
local DEMO_EVENTS = {
  700, EV_COUNTDOWN, 3, 800, EV_COUNTDOWN, 2, 900, EV_COUNTDOWN, 1, 1000, EV_GO, 0,
  1150, EV_GATE, 1, 1300, EV_GATE, 2, 1450, EV_PERFECT, 3, 1600, EV_GATE, 4,
  1750, EV_GATE, 5, 1900, EV_MISS, 0, 2050, EV_GATE, 1, 2200, EV_LEVEL_UP, 2,
  2250, EV_PERFECT, 2, 2400, EV_GATE, 3, 2500, EV_BARREL_ROLL, 0, 2550, EV_SQUASH, 0,
  2650, EV_HIT, 0, 2800, EV_GATE, 1, 2900, EV_PERFECT, 2, 3000, EV_GAME_OVER, 0,
  3010, EV_NEW_BEST, 0,
}
local demoBuf = {ADDR_RADIO, 0xC8, 0}

local function demoPut(i, v, bytes)
  for k = bytes - 1, 0, -1 do
    demoBuf[i + k] = v & 0xFF
    v = v >> 8
  end
end

local function demoSend(w, sub, v)
  for k = 6, #demoBuf do
    demoBuf[k] = nil
  end
  demoBuf[3] = sub
  demoPut(4, v, 2)
  onGame(w, demoBuf)
end

local function demoTick(w)
  local t = w.now % DEMO_LEN
  if w.demoT == nil or t < w.demoT then
    w.demoK = 1
    w.demoScore = 0
  end
  w.demoT = t
  while w.demoK <= #DEMO_EVENTS and DEMO_EVENTS[w.demoK] <= t do
    local ev, arg = DEMO_EVENTS[w.demoK + 1], DEMO_EVENTS[w.demoK + 2]
    demoSend(w, SUB_COMMAND, (ev << 8) | arg)
    if ev == EV_GATE or ev == EV_PERFECT or ev == EV_SQUASH then
      local pts = 500
      if ev ~= EV_SQUASH then
        pts = (ev == EV_PERFECT and 250 or 100) * arg
      end
      demoSend(w, SUB_POINTS, pts)
      w.demoScore = w.demoScore + pts
    end
    w.demoK = w.demoK + 3
  end
  if w.now - (w.demoStateT or 0) < 20 then
    return
  end
  w.demoStateT = w.now
  local phase, flags, sun = PH_ATTRACT, FLAG_ROLL_READY, 200
  if t >= 3600 then
    phase = PH_LOST
  elseif t >= 3000 then
    phase, sun = PH_OVER, 0
  elseif t >= 1000 then
    phase, flags = PH_FLYING, FLAG_ARMED | FLAG_ROLL_READY
    sun = 200 - (t - 1000) // 12
  elseif t >= 700 then
    phase, flags = PH_COUNTDOWN, FLAG_ARMED
  elseif t >= 350 then
    flags = FLAG_ARMED
  end
  local s = t / 100
  for k = 26, #demoBuf do
    demoBuf[k] = nil
  end
  demoBuf[3] = SUB_STATE
  demoBuf[4] = phase
  demoBuf[5] = t >= 2200 and 2 or 1
  demoPut(6, w.demoScore, 4)
  demoPut(10, max(w.demoScore, 12450), 4)
  demoPut(14, max(0, sun), 2)
  demoPut(16, 300, 2)
  demoBuf[18] = w.combo
  demoBuf[19] = phase == PH_FLYING and floor(70 + 25 * sin(s * 0.8)) or 0
  demoPut(20, w.demoK // 3, 2)
  demoBuf[22] = floor(30 * sin(s * 1.3)) & 0xFF
  demoBuf[23] = floor(55 + 30 * sin(s * 0.6))
  demoBuf[24] = flags
  demoBuf[25] = 1
  onGame(w, demoBuf)
end

local function pump(w)
  local now = getTime()
  w.now = now
  if w.demo then
    demoTick(w)
  elseif crossfireTelemetryPop then
    for _ = 1, 32 do
      local cmd, data = crossfireTelemetryPop()
      if cmd == nil then
        break
      end
      if cmd == FRAME_GAME and data then
        onGame(w, data)
      end
    end
  end
  w.online = w.lastRx ~= nil and now - w.lastRx < LINK_TIMEOUT
  -- Low time warning, once per second
  if w.online and w.phase == PH_FLYING and w.sun < 50 then
    local sec = w.sun // 10
    if sec ~= w.warnSec then
      w.warnSec = sec
      fxGate(w, 0x10000 + sec)
      blip(w, 1000 + (5 - sec) * 150, 60)
    end
  else
    w.warnSec = nil
  end
end

----------------------------------------------------------------------------
-- Drawing

local function age(w, t)
  if t == nil then
    return 100000
  end
  return w.now - t
end

local function outlined(x, y, s, f, col)
  drawText(x - 2, y, s, f | C_SHADE)
  drawText(x + 2, y, s, f | C_SHADE)
  drawText(x, y - 2, s, f | C_SHADE)
  drawText(x, y + 2, s, f | C_SHADE)
  drawText(x, y, s, f | col)
end

local function drawKite(cx, cy, scale, angle, pal, fph, famp)
  local ca, sa = cos(angle) * scale, sin(angle) * scale
  -- tail: a wave travelling to the tip, growing along the tail
  for i = 1, NW do
    local k = KW[i]
    local d = famp * k * sin(fph - k * 5)
    local x, y = KX[i] - d * 0.16, KY[i] + d
    SX[i] = cx + x * ca - y * sa
    SY[i] = cy + x * sa + y * ca
  end
  for i = NW + 1, NV do
    local x, y = KX[i], KY[i]
    SX[i] = cx + x * ca - y * sa
    SY[i] = cy + x * sa + y * ca
  end
  for t = 1, NT, 4 do
    local a, b, c = KT[t], KT[t + 1], KT[t + 2]
    drawTri(SX[a], SY[a], SX[b], SY[b], SX[c], SY[c], pal[KT[t + 3]])
  end
end

-- Striped synthwave sun, clipped at the horizon
local function drawSun(cx, cy, r, bottom)
  local step = r > 60 and 3 or 2
  local y = cy - r
  local ylim = min(cy + r, bottom)
  while y < ylim do
    local dy = y + step * 0.5 - cy
    local f = (dy + r) / (2 * r)
    local gap = false
    if f > 0.45 then
      local b = (f - 0.45) * 9
      gap = b - floor(b) < 0.12 + (f - 0.45) * 0.9
    end
    if not gap then
      local hw = sqrt(max(0, r * r - dy * dy))
      fillRect(cx - hw, y, hw * 2 + 1, min(step, ylim - y), SUN[min(12, floor(f * 12) + 1)])
    end
    y = y + step
  end
end

-- Sky, setting sun (the game timer) and a grid rushing towards the viewer
local function drawBackdrop(w, x, y, pw, ph, horizon)
  local f = w.sunShown / w.sunMax
  local level = SKY_LEVELS
  if w.online then
    level = min(SKY_LEVELS, max(1, floor((0.3 - f) / 0.3 * (SKY_LEVELS - 1) + 1.5)))
  end
  local sky = SKYS[level]
  local hh = horizon - y
  for i = 0, SKY_BANDS - 1 do
    local y0 = y + floor(hh * i / SKY_BANDS)
    local y1 = y + floor(hh * (i + 1) / SKY_BANDS)
    fillRect(x, y0, pw, y1 - y0, sky[i + 1])
  end
  if w.online and f > 0 then
    local r = max(8, floor(min(pw, ph) * 0.17))
    drawSun(x + floor(pw * 0.66), horizon + r - f * r * 2.8, r, horizon)
  end
  local gh = y + ph - horizon
  fillRect(x, horizon, pw, gh, C_GROUND)
  local off = w.scroll % 1
  for i = 0, 5 do
    local z = (i + off) / 6
    local ly = horizon + floor(gh * z * z)
    if ly > horizon then
      fillRect(x, ly, pw, 1, GRID[i + 1])
    end
  end
  local vx = x + pw * 0.5
  local spread = pw * 0.36
  for k = -5, 5 do
    local bx = vx + k * spread
    local t = 1
    if bx < x then
      t = (x - vx) / (bx - vx)
    elseif bx > x + pw - 1 then
      t = (x + pw - 1 - vx) / (bx - vx)
    end
    drawLine(vx + (bx - vx) * 0.04, horizon + gh * 0.04, vx + (bx - vx) * t, horizon + gh * t,
      SOLID, GRID[3])
  end
  fillRect(x, horizon - 1, pw, 2, w.online and C_HORIZON or C_DIM)
end

-- Wind streaks, more and longer with airspeed
local function drawStreaks(w, x, y, pw, hh)
  local n = floor(w.speedShown / 18)
  local len = 10 + w.speedShown * 0.4
  local t = (w.now % 60000) * (0.6 + w.speedShown * 0.05)
  for i = 1, n do
    local sy = y + 8 + (i * 53) % max(1, hh - 16)
    local sx = x + pw - ((t + i * 97) % (pw + len))
    local x0 = max(x, sx)
    local x1 = min(x + pw - 1, sx + len)
    if x1 > x0 then
      fillRect(x0, sy, x1 - x0, 1, C_STREAK)
    end
  end
end

-- Star burst around the kite (perfect pass, squashed bug)
local function drawSparkles(w, cx, cy, size, x0, y0, x1, y1)
  local a = age(w, w.sparkT)
  if a > 55 then
    return
  end
  local f = a / 55
  local s = max(1, floor(size * 0.035 * (1 - f)) + 1)
  for ring = 1, 2 do
    local r = size * (ring == 1 and (0.22 + f * 0.42) or (0.12 + f * 0.25))
    for i = ring, SPARK_N, ring do
      local px = cx + SPX[i] * r
      local py = cy + SPY[i] * r * 0.8
      if px - s * 2 > x0 and px + s * 2 < x1 and py - s * 2 > y0 and py + s * 2 < y1 then
        local c = SPARK_COL[(i + ring) % 3 + 1]
        fillRect(px - s * 2, py, s * 4 + 1, 1, c)
        fillRect(px, py - s * 2, 1, s * 4 + 1, c)
        fillRect(px - s // 2, py - s // 2, s + 1, s + 1, c)
      end
    end
  end
end

-- Points rising from the kite, kept below the callout line
local function drawPopups(w, cx, base, top)
  for i = 1, #w.pops do
    local p = w.pops[i]
    local a = age(w, p.t)
    if a < 90 then
      local py = max(top, base - a * 0.35) + p.slot * FH.mid
      drawText(cx, py, p.text, F_MID | CENTER | SHADOWED | C_GOLD)
    end
  end
end

-- The kite scene: backdrop, kite and effects. Returns the horizon.
local function drawScene(w, x, y, pw, ph, kiteW)
  local online = w.online
  local horizon = y + floor(ph * 0.74)
  local skyH = horizon - y
  drawBackdrop(w, x, y, pw, ph, horizon)
  if online and w.phase == PH_FLYING then
    drawStreaks(w, x, y, pw, skyH)
  end

  local now = w.now
  local cx = x + pw * 0.5
  local cy = y + skyH * 0.5 + sin(now * 0.035) * kiteW * 0.015
  local pal = PAL_N
  local angle = w.rollShown * 0.01745
  local ha = age(w, w.hitT)
  if not online then
    pal = PAL_DIM
    cy = cy + kiteW * 0.04
  elseif w.phase == PH_OVER then
    pal = PAL_GREY
    cy = cy + skyH * 0.28
  else
    cy = cy + (50 - w.altShown) * skyH * 0.0025
    if ha < 60 then
      local k = (60 - ha) / 60
      cx = cx + sin(ha * 1.9) * kiteW * 0.06 * k
      cy = cy + cos(ha * 2.3) * kiteW * 0.035 * k
      if (ha // 5) % 2 == 0 then
        pal = PAL_RED
      end
    elseif age(w, w.sparkT) < 10 then
      pal = PAL_HOT
    end
    local ra = age(w, w.rollT)
    if ra < 70 then
      local f = ra / 70
      angle = angle + w.rollDir * 6.2832 * f * f * (3 - 2 * f)
    end
  end
  -- keep the rotated kite inside the scene, top edge first
  local scale = kiteW / K_W
  local sa, ca = sin(angle), cos(angle)
  local lo, hi = 0, 0
  for i = 1, #KEXT, 2 do
    local ky = KEXT[i] * sa + KEXT[i + 1] * ca
    lo = min(lo, ky)
    hi = max(hi, ky)
  end
  cy = max(y + 3 - lo * scale, min(y + ph - 3 - hi * scale, cy))
  local famp = online and (5 + w.speedShown * 0.1) or 3
  drawKite(cx, cy, scale, angle, pal, w.flutter, famp)
  if online then
    drawSparkles(w, cx, cy, kiteW, x, y, x + pw, horizon)
    local top = y + 4 + FH.mid + 2
    drawPopups(w, cx + kiteW * 0.2, max(top, cy - kiteW * 0.1), top)
    if age(w, w.wordT) < 80 and age(w, w.bannerT) >= 120 then
      outlined(x + pw // 2, y + 4, w.wordText, F_MID | CENTER, w.wordColor)
    end
    if ha < 60 and (ha // 8) % 2 == 0 then
      drawRect(x, y, pw, ph, C_RED, 4)
    end
  end
  return horizon
end

-- Countdown and GO: big number in a depleting ring
local function drawCountdown(cx, cy, text, col, f, big)
  local font, fh = F_XXL, FH.xxl
  if not big then
    font, fh = F_XL, FH.xl
  end
  local r = floor(fh * 0.85)
  fillCircle(cx, cy, r, C_SHADE)
  drawAnnulus(cx, cy, r - 5, r, 0, 360, C_LINE)
  if f > 0 then
    drawAnnulus(cx, cy, r - 5, r, 0, floor(360 * f), col)
  end
  outlined(cx, cy - fh // 2, text, font | CENTER, col)
end

-- Overlays on the scene: countdown, banners, game over, link problems and
-- the launch instructions
local function drawSceneOverlay(w, x, y, pw, ph, horizon)
  local cx = x + pw // 2
  local skyH = horizon - y
  local skyMid = y + skyH // 2
  local now = w.now
  local blink = (now // 40) % 2 == 0
  local groundMid = horizon + (y + ph - horizon) // 2
  local big = skyH >= 150 and pw >= 200
  local f1, h1, f2, h2 = F_DBL, FH.dbl, F_MID, FH.mid
  if not big then
    f1, h1, f2, h2 = F_MID, FH.mid, F_BOLD, FH.bold
  end
  if not w.online then
    local bh = FH.bold + FH.sml + 10
    local by = min(groundMid - bh // 2, y + ph - bh - 2)
    fillRect(x, by - 3, pw, bh + 6, C_SHADE, 5)
    local dots = string.rep(".", (now // 50) % 4)
    local wide = pw >= 240
    local hint = wide and "Start the Zephyr game (QEMU)" or "Start the game (QEMU)"
    drawText(cx, by + 2, (wide and "WAITING FOR ZEPHYR" or "WAITING") .. dots,
      F_BOLD | CENTER | C_TEXT)
    drawText(cx, by + 4 + FH.bold, hint, F_SML | CENTER | C_MUTED)
    return
  end
  local phase = w.phase
  local ga = age(w, w.goT)
  if phase == PH_COUNTDOWN and w.count > 0 then
    local a = min(100, age(w, w.countT))
    drawCountdown(cx, skyMid, tostring(w.count), a < 12 and C_WHITE or C_CYAN, 1 - a / 100,
      skyH >= 140)
  elseif ga < 100 then
    drawCountdown(cx, skyMid, "GO!", (now // 8) % 2 == 0 and C_GREEN or C_GOLD, 1, skyH >= 140)
  elseif phase == PH_OVER then
    local bh = h1 + h2 + ((w.newBestRun or w.naming) and h1 or 0) + 14
    local by = max(y, y + floor(skyH * 0.36) - bh // 2)
    fillRect(x, by, pw, bh, C_SHADE, 5)
    fillRect(x, by, pw, 1, C_PINK)
    fillRect(x, by + bh - 1, pw, 1, C_PINK)
    outlined(cx, by + 4, "GAME OVER", f1 | CENTER, C_PINK)
    drawText(cx, by + 6 + h1, fmt("SCORE %d", w.score), f2 | CENTER | C_TEXT)
    if w.naming then
      local prompt = sizeText("ENTER YOUR NAME", f1) <= pw and "ENTER YOUR NAME" or "ENTER NAME"
      outlined(cx, by + 8 + h1 + h2, prompt, f1 | CENTER, blink and C_GOLD or C_WHITE)
    elseif w.newBestRun then
      outlined(cx, by + 8 + h1 + h2, "NEW BEST!", f1 | CENTER, blink and C_GOLD or C_WHITE)
    end
  elseif phase == PH_LOST then
    local by = skyMid - h1 // 2
    fillRect(x, by - 4, pw, h1 + 8, C_SHADE, 5)
    outlined(cx, by, "SIGNAL LOST", f1 | CENTER, blink and C_RED or C_WHITE)
  elseif phase == PH_ATTRACT then
    local l1, l2, col = "THROTTLE DOWN", "TO ARM", C_ORANGE
    if w.flags & FLAG_ARMED ~= 0 then
      l1, l2, col = "THROTTLE UP", "TO LAUNCH", blink and C_GREEN or C_WHITE
    elseif not blink then
      col = C_GOLD
    end
    local font, fh = F_MID, FH.mid
    if fh * 2 + 4 > y + ph - horizon or sizeText(l1, F_MID) > pw * 0.9 then
      font, fh = F_BOLD, FH.bold
    end
    local by = min(groundMid - fh, y + ph - fh * 2)
    outlined(cx, by, l1, font | CENTER, col)
    outlined(cx, by + fh, l2, font | CENTER, col)
  end
  local ba = age(w, w.bannerT)
  if ba < 120 and phase ~= PH_OVER and ga >= 100 then
    local col = (now // 10) % 2 == 0 and w.bannerColor or C_WHITE
    outlined(cx, y + skyH // 5, w.bannerText, f1 | CENTER, col)
  end
end

local function sunColor(f)
  if f > 0.5 then
    return C_GOLD
  elseif f > 0.25 then
    return C_ORANGE
  end
  return C_RED
end

local function drawBar(x, y, bw, bh, f, col)
  fillRect(x, y, bw, bh, C_PANEL)
  local fw = floor((bw - 2) * max(0, min(1, f)))
  if fw > 0 then
    fillRect(x + 1, y + 1, fw, bh - 2, col)
  end
end

-- Dashboard sections, by display order: title, score, sun, combo and level,
-- speed, status. Sections are dropped in reverse priority when space is short.
local SEC_PRIO = {2, 3, 4, 6, 1, 5}
local SEC_H = {0, 0, 0, 0, 0, 0}
local SEC_ON = {false, false, false, false, false, false}

local function dashTitle(w, x, y, dw)
  local hs, hb = FH.sml, FH.bold
  local online = w.online
  drawText(x, y, "KITE", F_BOLD | C_CYAN)
  drawText(x + sizeText("KITE ", F_BOLD), y, "RUSH", F_BOLD | C_PINK)
  local link = online and "LIVE" or "NO GAME"
  local linkCol = online and C_GREEN or ((getRSSI() > 0) and C_ORANGE or C_RED)
  drawText(x + dw, y + (hb - hs) // 2, link, F_SML | RIGHT | linkCol)
  if not online or (w.now // 25) % 2 == 0 then
    local r = max(3, hs // 4)
    fillCircle(x + dw - sizeText(link, F_SML) - r - 4, y + hb // 2, r, linkCol)
  end
  fillRect(x, y + hb + 2, dw, 1, C_LINE)
end

local function dashScore(w, x, y, dw, font, fh)
  local online = w.online
  drawText(x, y, "SCORE", F_SML | C_MUTED)
  local bestStr = online and tostring(w.best) or "--"
  local bestCol = online and C_GOLD or C_DIM
  if online and age(w, w.bestT) < 300 and (w.now // 20) % 2 == 0 then
    bestCol = C_WHITE
  end
  drawText(x + dw, y, bestStr, F_SML | RIGHT | bestCol)
  drawText(x + dw - sizeText(bestStr .. " ", F_SML), y, "BEST", F_SML | RIGHT | C_MUTED)
  local col = C_TEXT
  if not online then
    col = C_DIM
  elseif w.score > w.shown then
    col = C_GOLD
  end
  drawText(x, y + FH.sml, online and tostring(w.shown) or "--", font | col)
end

local function dashSun(w, x, y, dw)
  local hs, hb = FH.sml, FH.bold
  local online = w.online
  local f = online and w.sunShown / w.sunMax or 0
  local col = sunColor(f)
  local low = online and w.phase == PH_FLYING and w.sun < 50
  drawText(x, y, "SUN", F_SML | C_MUTED)
  if not (low and (w.now // 25) % 2 == 1) then
    local t = online and fmt("%d.%ds", w.sun // 10, w.sun % 10) or "--"
    drawText(x + dw, y - (hb - hs) // 2, t, F_BOLD | RIGHT | (online and col or C_DIM))
  end
  local sr = max(4, hb // 3)
  y = y + hs + 2
  fillCircle(x + sr, y + sr, sr, online and col or C_DIM)
  drawBar(x + sr * 2 + 6, y + sr - hb // 4, dw - sr * 2 - 6, hb // 2, f, col)
end

local function dashCombo(w, x, y, dw)
  local online = w.online
  local colw = dw // 2
  drawText(x, y, "COMBO", F_SML | C_MUTED)
  drawText(x + colw, y, "LEVEL", F_SML | C_MUTED)
  y = y + FH.sml
  local combo = online and w.combo or 0
  local col = C_TEXT
  if combo >= 10 then
    col = C_PINK
  elseif combo >= 5 then
    col = C_GOLD
  elseif combo == 0 then
    col = C_DIM
  end
  if online and age(w, w.comboT) < 20 then
    drawText(x, y + (FH.dbl - FH.xl) // 2, "x" .. combo, F_XL | C_WHITE)
  else
    drawText(x, y, "x" .. combo, F_DBL | col)
  end
  local lvlCol = online and C_CYAN or C_DIM
  if online and age(w, w.levelT) < 200 and (w.now // 15) % 2 == 0 then
    lvlCol = C_GREEN
  end
  drawText(x + colw, y, online and tostring(w.level) or "-", F_DBL | lvlCol)
end

local function dashSpeed(w, x, y, dw)
  local online = w.online
  drawText(x, y, "SPEED", F_SML | C_MUTED)
  drawText(x + dw, y, online and fmt("%d%%", w.speed) or "--", F_SML | RIGHT | C_TEXT)
  y = y + FH.sml + 2
  local segs = 12
  local sw = (dw + 2) // segs
  local lit = floor(w.speedShown * segs / 100 + 0.5)
  for i = 0, segs - 1 do
    local col = C_PANEL
    if online and i < lit then
      col = i < 6 and C_CYAN or (i < 9 and C_LILAC or C_PINK)
    end
    fillRect(x + i * sw, y, sw - 2, FH.bold // 2, col)
  end
end

local function dashStatus(w, x, y, dw)
  local st, col = "NO TELEMETRY", C_MUTED
  if w.online then
    local phase = w.phase
    if phase == PH_FLYING or phase == PH_OVER then
      st, col = fmt("GATES %d", w.gates), C_TEXT
    elseif phase == PH_ATTRACT then
      if w.flags & FLAG_ARMED ~= 0 then
        st, col = "ARMED", C_GREEN
      else
        st, col = "NOT ARMED", C_ORANGE
      end
    elseif phase == PH_COUNTDOWN then
      st, col = "GET READY", C_CYAN
    else
      st, col = "CHECK RADIO LINK", C_RED
    end
    if phase == PH_FLYING then
      local ready = w.flags & FLAG_ROLL_READY ~= 0
      drawText(x + dw, y, "ROLL", F_BOLD | RIGHT | (ready and C_CYAN or C_DIM))
    end
  end
  drawText(x, y, st, F_BOLD | col)
end

-- Dashboard column for wide and tall zones
local function drawDash(w, x, y, dw, dh)
  local hs, hb = FH.sml, FH.bold
  local font, fh = F_XL, FH.xl
  if dh < FH.xl * 5 or sizeText(w.online and tostring(w.shown) or "--", F_XL) > dw then
    font, fh = F_DBL, FH.dbl
  end
  SEC_H[1] = hb + 3
  SEC_H[2] = hs + fh
  SEC_H[3] = hs + 2 + max(4, hb // 3) * 2
  SEC_H[4] = hs + FH.dbl
  SEC_H[5] = hs + 2 + hb // 2
  SEC_H[6] = hb
  local total, n = 0, 0
  for k = 1, #SEC_PRIO do
    local sec = SEC_PRIO[k]
    SEC_ON[sec] = total + SEC_H[sec] + n * 4 <= dh
    if SEC_ON[sec] then
      total = total + SEC_H[sec]
      n = n + 1
    end
  end
  local gap = n > 1 and max(2, min(16, (dh - total) // (n - 1))) or 0
  local cy = y
  if SEC_ON[1] then
    dashTitle(w, x, cy, dw)
    cy = cy + SEC_H[1] + gap
  end
  dashScore(w, x, cy, dw, font, fh)
  cy = cy + SEC_H[2] + gap
  dashSun(w, x, cy, dw)
  cy = cy + SEC_H[3] + gap
  if SEC_ON[4] then
    dashCombo(w, x, cy, dw)
    cy = cy + SEC_H[4] + gap
  end
  if SEC_ON[5] then
    dashSpeed(w, x, cy, dw)
  end
  if SEC_ON[6] then
    dashStatus(w, x, y + dh - hb, dw)
  end
end

-- Small zones: kite thumbnail, score and time bar
local function drawCompact(w, zw, zh)
  local online = w.online
  local ks = min(zh, zw * 0.4)
  local pal = online and PAL_N or PAL_DIM
  if online and w.phase == PH_OVER then
    pal = PAL_GREY
  end
  drawKite(ks * 0.5, zh * 0.5, ks * 0.9 / K_W, w.rollShown * 0.01745, pal, w.flutter, 4)
  local x = floor(ks) + 4
  local tw = zw - x - 4
  if tw < 20 then
    return
  end
  local hs = FH.sml
  local scoreStr = online and tostring(w.shown) or "--"
  local roomy = zh >= FH.dbl + hs * 2 + 8
  local font, fh = F_DBL, FH.dbl
  if not roomy or sizeText(scoreStr, F_DBL) > tw then
    font, fh = F_BOLD, FH.bold
  end
  local barH = max(4, hs // 2)
  local y = max(0, (zh - fh - 2 - barH - (roomy and hs or 0)) // 2)
  if roomy then
    drawText(x, y, online and "SCORE" or "KITE RUSH", F_SML | C_MUTED)
    if online and w.combo > 1 then
      drawText(x + tw, y, "x" .. w.combo, F_SML | RIGHT | C_GOLD)
    end
    y = y + hs
  end
  drawText(x, y, scoreStr, font | (online and C_TEXT or C_DIM))
  y = y + fh + 2
  if online then
    if y + barH <= zh then
      local f = w.sunShown / w.sunMax
      drawBar(x, y, tw, barH, f, sunColor(f))
    end
  elseif y + hs <= zh then
    drawText(x, y, "waiting...", F_SML | C_MUTED)
  end
end

----------------------------------------------------------------------------
-- Widget API

local function animate(w, dt)
  if dt <= 0 then
    return
  end
  local online = w.online
  local target = online and w.roll or 0
  if online and w.phase == PH_OVER then
    target = -25
  end
  w.rollShown = w.rollShown + (target - w.rollShown) * min(1, dt * 8)
  w.altShown = w.altShown + (w.alt - w.altShown) * min(1, dt * 4)
  w.speedShown = w.speedShown + ((online and w.speed or 0) - w.speedShown) * min(1, dt * 5)
  w.sunShown = w.sunShown + ((online and w.sun or 0) - w.sunShown) * min(1, dt * 6)
  if online and w.phase == PH_FLYING then
    w.scroll = w.scroll + dt * (0.4 + w.speedShown * 0.025)
  end
  w.flutter = (w.flutter + dt * (4 + (online and w.speedShown * 0.1 or 0))) % 6.2832
  -- score count-up
  if w.shown > w.score then
    w.shown = w.score
  elseif w.shown < w.score then
    w.shown = min(w.score, w.shown + max(1, floor((w.score - w.shown) * min(1, dt * 6))))
  end
end

local function create(zone, opts)
  local w = {
    zone = zone, now = getTime(),
    phase = PH_ATTRACT, level = 1, score = 0, best = 0, sun = 0, sunMax = 300,
    combo = 0, speed = 0, gates = 0, roll = 0, alt = 50, flags = 0, count = 0,
    online = false, shown = 0,
    rollShown = 0, altShown = 50, speedShown = 0, sunShown = 0,
    scroll = 0, flutter = 0, rollDir = 1, audioUntil = 0,
    pops = { {}, {}, {}, {} }, popIdx = 1,
  }
  w.sound = opts.Sound ~= 0
  w.haptic = opts.Haptic ~= 0
  w.demo = opts.Demo == 1
  return w
end

local function update(w, opts)
  w.sound = opts.Sound ~= 0
  w.haptic = opts.Haptic ~= 0
  w.demo = opts.Demo == 1
end

local function background(w)
  pump(w)
end

local function refresh(w, event, touchState)
  if FH == nil then
    measureFonts()
  end
  pump(w)
  local now = w.now
  local dt = (now - (w.lastT or now)) / 100
  w.lastT = now
  animate(w, min(dt, 0.25))

  local zw, zh = w.zone.w, w.zone.h
  fillRect(0, 0, zw, zh, C_BG)
  if zw >= 300 and zh >= 120 and zw >= zh * 1.4 then
    -- scene on the left, dashboard on the right
    local pw = floor(min(zw * 0.52, zh * 1.25))
    local horizon = drawScene(w, 0, 0, pw, zh, min(pw * 0.78, zh * 0.74))
    drawSceneOverlay(w, 0, 0, pw, zh, horizon)
    local m = max(6, zw // 60)
    drawDash(w, pw + m, m // 2, zw - pw - m * 2, zh - m)
  elseif zw >= 160 and zh >= 200 and zh >= zw * 0.9 then
    -- scene on top, dashboard below
    local ph = floor(min(zh * 0.46, zw * 0.75))
    local horizon = drawScene(w, 0, 0, zw, ph, min(zw * 0.7, ph * 0.74))
    drawSceneOverlay(w, 0, 0, zw, ph, horizon)
    local m = max(6, zw // 40)
    drawDash(w, m, ph + m // 2, zw - m * 2, zh - ph - m)
  else
    drawCompact(w, zw, zh)
  end
end

return {
  name = "KiteRush",
  options = options,
  create = create,
  update = update,
  refresh = refresh,
  background = background,
}
