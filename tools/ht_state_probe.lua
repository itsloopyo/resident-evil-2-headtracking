-- Adaptive head-tracking game-state probe for RE Engine games (RE2/3/4/7/8).
-- Finds the player, enumerates its components, auto-registers control/event/
-- cutscene boolean getters, and live-samples them + common GUI/global signals
-- so reaching a cutscene reveals which flag flips. Remove after use.
--
-- Deploy to <game>/reframework/autorun/ht_state_probe.lua and read the
-- [HTPROBE] lines in the game's *_framework_log.txt.

local function P(s) log.info("[HTPROBE] " .. s) end

local NS = sdk.game_namespace("")
P("game=" .. tostring(reframework:get_game_name()) .. " namespace='" .. tostring(NS) .. "'")

local keys = {"event", "cutscene", "control", "input", "demo", "motion",
              "posture", "disable", "lock", "busy", "camera", "playing",
              "pause", "menu", "movie", "open", "active"}
local function matches(name)
    local n = name:lower()
    for _, k in ipairs(keys) do if n:find(k, 1, true) then return true end end
    return false
end

local last = {}
local function sample(label, value)
    local v = tostring(value)
    if last[label] ~= v then
        last[label] = v
        P("LIVE " .. label .. " = " .. v)
    end
end

local function singleton(short) return sdk.get_managed_singleton(sdk.game_namespace(short)) end

local function safe_call(obj, method)
    if not obj then return nil end
    local ok, ret = pcall(function() return obj:call(method) end)
    if not ok then return nil end
    return ret
end

-- Try several strategies to obtain the player GameObject.
local function find_player_gameobject()
    -- ropeway / offline: PlayerManager.get_CurrentPlayer -> GameObject
    local pm = singleton("PlayerManager")
    if pm then
        local p = safe_call(pm, "get_CurrentPlayer")
        if p then return p, "PlayerManager.get_CurrentPlayer" end
    end
    -- chainsaw / app: CharacterManager.getPlayerContextRef -> get_BodyGameObject
    local cm = singleton("CharacterManager")
    if cm then
        local ctx = safe_call(cm, "getPlayerContextRef")
        if ctx then
            local body = safe_call(ctx, "get_BodyGameObject")
            if body then return body, "CharacterManager.getPlayerContextRef.BodyGameObject" end
            return ctx, "CharacterManager.getPlayerContextRef (no body)"
        end
    end
    -- survivor manager fallback
    local sm = singleton("survivor.SurvivorManager")
    if sm then
        local s = safe_call(sm, "get_CurrentSurvivor") or safe_call(sm, "getCurrentSurvivor")
        if s then return s, "SurvivorManager.CurrentSurvivor" end
    end
    return nil, nil
end

local registered = {}
local enumerated = false

local function register_getters(obj, owner_label)
    local td = obj:get_type_definition()
    if not td then return end
    local t = td
    while t do
        local tn = t:get_full_name()
        for _, m in ipairs(t:get_methods()) do
            local mn = m:get_name()
            if matches(mn) and m:get_num_params() == 0 then
                local rt = m:get_return_type()
                local rtn = rt and rt:get_full_name() or "?"
                if rtn == "System.Boolean" then
                    registered[#registered + 1] = { obj = obj, method = mn, label = tn .. "::" .. mn }
                    P("  reg: " .. owner_label .. " -> " .. tn .. "::" .. mn)
                end
            end
        end
        t = t:get_parent_type()
    end
end

local function enumerate(player, how)
    P("PLAYER via " .. how .. " : " .. player:get_type_definition():get_full_name())
    -- register getters on the player object/context itself
    register_getters(player, "player")
    -- and on each component, if it is a GameObject
    local comps = safe_call(player, "get_Components")
    if comps then
        local ok, elems = pcall(function() return comps:get_elements() end)
        if ok and elems then
            for _, c in ipairs(elems) do
                if c then
                    local ctd = c:get_type_definition()
                    if ctd then register_getters(c, ctd:get_full_name()) end
                end
            end
        end
    end
    P("registered " .. #registered .. " bool getters")
    enumerated = true
end

-- GUI singletons vary by game; try the common ones each frame.
local gui_candidates = {
    {short = "gui.GUIMaster",  methods = {"isActiveGuiInGame", "get_IsOpenPause", "get_IsOpenPauseForEvent", "get_IsNotMoviePlayingOld"}},
    {short = "gui.GUIManager", methods = {"get_IsPlayingEvent", "get_isOpen", "get_CurrentSituationType"}},
    {short = "GUIManager",     methods = {"get_IsPlayingEvent", "get_isOpen", "get_isEnableEventFlow", "get_isEnableSystemFlow", "get_CurrentSituationType"}},
    {short = "GuiManager",     methods = {"get_IsPlayingEvent", "get_isOpen"}},
}

local frame = 0
re.on_frame(function()
    frame = frame + 1
    if frame % 15 ~= 0 then return end

    local player, how = find_player_gameobject()
    sample("player_exists", player ~= nil)
    if player and not enumerated then enumerate(player, how) end
    if not player then enumerated = false; registered = {} end

    for _, g in ipairs(gui_candidates) do
        local s = singleton(g.short)
        if s then
            for _, mth in ipairs(g.methods) do
                local r = safe_call(s, mth)
                if r ~= nil then sample(g.short .. "." .. mth, r) end
            end
        end
    end

    -- via.Application GlobalSpeed (0 => hard pause)
    local app = sdk.get_native_singleton("via.Application")
    if app then
        local appT = sdk.find_type_definition("via.Application")
        if appT then
            local ok, sp = pcall(function() return sdk.call_native_func(app, appT, "get_GlobalSpeed") end)
            if ok and sp ~= nil then sample("via.Application.GlobalSpeed", string.format("%.3f", sp)) end
        end
    end

    for _, r in ipairs(registered) do
        sample(r.label, safe_call(r.obj, r.method))
    end
end)

P("adaptive probe armed")
