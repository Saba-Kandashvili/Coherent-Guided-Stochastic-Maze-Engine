-- =================================================
-- CONFIGURATION
-- =================================================
local IMAGE_DIR = "tile_mock/"
local WINDOW_BG_COLOR = {0.05, 0.05, 0.05} 
local VOID_COLOR = {0.1, 0.0, 0.1} 
local REACHABLE_COLOR = {0.0, 0.6, 0.2, 0.5}      -- Green overlay for reachable
local UNREACHABLE_COLOR = {0.6, 0.1, 0.2, 0.6}   -- Dark red (not pure red) for unreachable

-- =================================================
-- TILE BIT VALUES (matching tiles.h)
-- =================================================
local TILE_BITS = {
    North_East_Corridor = 1,
    South_East_Corridor = 2,
    South_West_Corridor = 4,
    North_West_Corridor = 8,
    North_South_Corridor = 16,
    West_East_Corridor = 32,
    North_T_Corridor = 64,
    East_T_Corridor = 128,
    South_T_Corridor = 256,
    West_T_Corridor = 512,
    Normal_X_Corridor = 1024,
    Special_X_Corridor = 2048,
    North_DeadEnd = 4096,
    East_DeadEnd = 8192,
    South_DeadEnd = 16384,
    West_DeadEnd = 32768
}

-- Masks for checking if a tile opens in a specific direction
-- Opens North: tiles that have an exit going up
local OPENS_NORTH = TILE_BITS.North_East_Corridor + TILE_BITS.North_West_Corridor + 
                    TILE_BITS.North_South_Corridor + TILE_BITS.North_T_Corridor + 
                    TILE_BITS.East_T_Corridor + TILE_BITS.West_T_Corridor + 
                    TILE_BITS.Normal_X_Corridor + TILE_BITS.Special_X_Corridor + 
                    TILE_BITS.North_DeadEnd

-- Opens East: tiles that have an exit going right
local OPENS_EAST = TILE_BITS.North_East_Corridor + TILE_BITS.South_East_Corridor + 
                   TILE_BITS.West_East_Corridor + TILE_BITS.East_T_Corridor + 
                   TILE_BITS.South_T_Corridor + TILE_BITS.North_T_Corridor + 
                   TILE_BITS.Normal_X_Corridor + TILE_BITS.Special_X_Corridor + 
                   TILE_BITS.East_DeadEnd

-- Opens South: tiles that have an exit going down
local OPENS_SOUTH = TILE_BITS.South_East_Corridor + TILE_BITS.South_West_Corridor + 
                    TILE_BITS.North_South_Corridor + TILE_BITS.East_T_Corridor + 
                    TILE_BITS.South_T_Corridor + TILE_BITS.West_T_Corridor + 
                    TILE_BITS.Normal_X_Corridor + TILE_BITS.Special_X_Corridor + 
                    TILE_BITS.South_DeadEnd

-- Opens West: tiles that have an exit going left
local OPENS_WEST = TILE_BITS.South_West_Corridor + TILE_BITS.North_West_Corridor + 
                   TILE_BITS.West_East_Corridor + TILE_BITS.South_T_Corridor + 
                   TILE_BITS.West_T_Corridor + TILE_BITS.North_T_Corridor + 
                   TILE_BITS.Normal_X_Corridor + TILE_BITS.Special_X_Corridor + 
                   TILE_BITS.West_DeadEnd

-- =================================================
-- VARIABLES
-- =================================================
local sprites = {}
local grid = {} 
local maskGrid = {} -- 2D Table containing 0 or 1
local reachableGrid = {} -- 2D Table: true = reachable, false/nil = unreachable
local width, length, height = 0, 0, 0
local tileSize = 32

-- State
local currentLayer = 1
local showMask = false
local showTraversal = true -- Show flood fill traversal by default 

-- Camera
local camX, camY = 0, 0
local camZoom = 1.0

function love.load()
    love.window.setTitle("Maze Inspector (Exact Mask Overlay)")
    love.window.setMode(1000, 1000, {resizable=true})
    love.graphics.setBackgroundColor(WINDOW_BG_COLOR)
    
    local success = pcall(function()
        sprites["X"] = love.graphics.newImage(IMAGE_DIR .. "X.png")
        sprites["T"] = love.graphics.newImage(IMAGE_DIR .. "T.png")
        sprites["L"] = love.graphics.newImage(IMAGE_DIR .. "L.png")
        sprites["I"] = love.graphics.newImage(IMAGE_DIR .. "I.png")
        sprites["D"] = love.graphics.newImage(IMAGE_DIR .. "D.png")
    end)

    if not success then
        print("ERROR: Images missing in 'tile_mock/' folder.")
        return
    end
    
    for k, v in pairs(sprites) do v:setFilter("nearest", "nearest") end

    loadMaze3D()
    loadMask()
    runFloodFill()
    centerCamera()
end

-- =================================================
-- FLOOD FILL TRAVERSAL LOGIC
-- =================================================

-- Check if a tile value opens in a specific direction
local function opensNorth(tile) return bit.band(tile, OPENS_NORTH) ~= 0 end
local function opensEast(tile) return bit.band(tile, OPENS_EAST) ~= 0 end
local function opensSouth(tile) return bit.band(tile, OPENS_SOUTH) ~= 0 end
local function opensWest(tile) return bit.band(tile, OPENS_WEST) ~= 0 end

-- Check if tile is a valid collapsed tile (single bit set, not empty)
local function isValidTile(tile)
    if not tile or tile == 0 then return false end
    -- Check if it's a power of 2 (single bit)
    return bit.band(tile, tile - 1) == 0
end

-- Check if we can traverse from (x1,y1) to (x2,y2)
local function canTraverse(gridLayer, x1, y1, x2, y2)
    if not gridLayer[y1] or not gridLayer[y2] then return false end
    local tile1 = gridLayer[y1][x1]
    local tile2 = gridLayer[y2][x2]
    
    if not isValidTile(tile1) or not isValidTile(tile2) then return false end
    
    local dx = x2 - x1
    local dy = y2 - y1
    
    -- Moving North (y decreases)
    if dy == -1 and dx == 0 then
        return opensNorth(tile1) and opensSouth(tile2)
    -- Moving South (y increases)
    elseif dy == 1 and dx == 0 then
        return opensSouth(tile1) and opensNorth(tile2)
    -- Moving East (x increases)
    elseif dx == 1 and dy == 0 then
        return opensEast(tile1) and opensWest(tile2)
    -- Moving West (x decreases)
    elseif dx == -1 and dy == 0 then
        return opensWest(tile1) and opensEast(tile2)
    end
    
    return false
end

-- Find the first valid tile to start flood fill from
local function findStartTile(gridLayer)
    for y = 1, length do
        for x = 1, width do
            if gridLayer[y] and isValidTile(gridLayer[y][x]) then
                return x, y
            end
        end
    end
    return nil, nil
end

-- BFS flood fill to mark all reachable tiles
local function floodFillLayer(gridLayer)
    local visited = {}
    for y = 1, length do
        visited[y] = {}
    end
    
    local startX, startY = findStartTile(gridLayer)
    if not startX then return visited end
    
    -- BFS queue
    local queue = {{x = startX, y = startY}}
    visited[startY][startX] = true
    local head = 1
    
    while head <= #queue do
        local current = queue[head]
        head = head + 1
        local cx, cy = current.x, current.y
        
        -- Check all 4 neighbors
        local neighbors = {
            {x = cx, y = cy - 1},     -- North
            {x = cx + 1, y = cy},     -- East
            {x = cx, y = cy + 1},     -- South
            {x = cx - 1, y = cy}      -- West
        }
        
        for _, neighbor in ipairs(neighbors) do
            local nx, ny = neighbor.x, neighbor.y
            -- Bounds check
            if nx >= 1 and nx <= width and ny >= 1 and ny <= length then
                if not visited[ny][nx] and canTraverse(gridLayer, cx, cy, nx, ny) then
                    visited[ny][nx] = true
                    table.insert(queue, {x = nx, y = ny})
                end
            end
        end
    end
    
    return visited
end

-- Run flood fill for all layers
function runFloodFill()
    reachableGrid = {}
    for z = 1, height do
        if grid[z] then
            reachableGrid[z] = floodFillLayer(grid[z])
        end
    end
    
    -- Count stats for current layer
    local totalValid = 0
    local totalReachable = 0
    if grid[1] then
        for y = 1, length do
            for x = 1, width do
                if grid[1][y] and isValidTile(grid[1][y][x]) then
                    totalValid = totalValid + 1
                    if reachableGrid[1] and reachableGrid[1][y] and reachableGrid[1][y][x] then
                        totalReachable = totalReachable + 1
                    end
                end
            end
        end
    end
    print(string.format("Flood Fill Complete: %d/%d tiles reachable (%.1f%%)", 
                        totalReachable, totalValid, 
                        totalValid > 0 and (totalReachable/totalValid*100) or 0))
end

-- =================================================
-- TILE MAPPING
-- =================================================
local TILE_MAP = {
    [1]     = {key="L", r=0},             
    [2]     = {key="L", r=math.pi/2},     
    [4]     = {key="L", r=math.pi},       
    [8]     = {key="L", r=3*math.pi/2},   
    
    [16]    = {key="I", r=0},             
    [32]    = {key="I", r=math.pi/2},     
    
    [64]    = {key="T", r=0},             
    [128]   = {key="T", r=math.pi/2},     
    [256]   = {key="T", r=math.pi},       
    [512]   = {key="T", r=3*math.pi/2},   
    
    [1024]  = {key="X", r=0, special=false}, 
    [2048]  = {key="X", r=0, special=true},  
    
    [4096]  = {key="D", r=0},             
    [8192]  = {key="D", r=math.pi/2},     
    [16384] = {key="D", r=math.pi},       
    [32768] = {key="D", r=3*math.pi/2}    
}

function loadMaze3D()
    grid = {}
    width, length, height = 0, 0, 0
    
    local filename = "maze.txt"
    if not love.filesystem.getInfo(filename) then return end
    
    local contents = love.filesystem.read(filename)
    local lines = {}
    for s in contents:gmatch("[^\r\n]+") do table.insert(lines, s) end

    if #lines > 0 then
        local h_parts = {}
        for s in lines[1]:gmatch("([^,]+)") do table.insert(h_parts, s) end
        width = tonumber(h_parts[1]) or 0
        length = tonumber(h_parts[2]) or 0
        height = tonumber(h_parts[3]) or 1 
    end

    local lineIdx = 2
    for z = 1, height do
        grid[z] = {}
        for y = 1, length do
            grid[z][y] = {}
            local row_str = lines[lineIdx]
            lineIdx = lineIdx + 1
            if row_str then
                local x = 1
                for val in row_str:gmatch("([^,]+)") do
                    grid[z][y][x] = tonumber(val)
                    x = x + 1
                end
            end
        end
    end
end

function loadMask()
    maskGrid = {}
    local filename = "debug_mask.txt" -- Reading the binary file
    if not love.filesystem.getInfo(filename) then return end

    local contents = love.filesystem.read(filename)
    local lines = {}
    for s in contents:gmatch("[^\r\n]+") do table.insert(lines, s) end

    -- Read Grid (Starting line 2)
    local startLine = 2
    for y = 1, length do
        maskGrid[y] = {}
        local row_str = lines[startLine + (y-1)]
        if row_str then
            local x = 1
            for val in row_str:gmatch("([^,]+)") do
                -- 1 = C backend said VALID
                -- 0 = C backend said VOID
                maskGrid[y][x] = (tonumber(val) == 1)
                x = x + 1
            end
        end
    end
    print("Loaded Binary Mask Overlay")
end

function love.draw()
    if width == 0 then return end

    love.graphics.push()
    love.graphics.translate(camX, camY)
    love.graphics.scale(camZoom)

    local currentGrid = grid[currentLayer]
    local currentReachable = reachableGrid[currentLayer]
    local belowGrid = nil
    if currentLayer > 1 then belowGrid = grid[currentLayer - 1] end
    
    if currentGrid then
        for y = 1, length do
            for x = 1, width do
                local val = currentGrid[y] and currentGrid[y][x] or 0
                local drawX = (x-1) * tileSize
                local drawY = (y-1) * tileSize
                
                local def = TILE_MAP[val]
                
                -- 1. Draw Actual Maze
                if def then
                    love.graphics.setColor(1, 1, 1)
                    local img = sprites[def.key]
                    local scale = tileSize / img:getWidth()
                    love.graphics.draw(img, drawX + tileSize/2, drawY + tileSize/2, def.r, scale, scale, img:getWidth()/2, img:getHeight()/2)
                    
                    if def.special then
                        love.graphics.setColor(1, 0, 0, 0.8) 
                        love.graphics.circle("fill", drawX + tileSize/2, drawY + tileSize/2, tileSize/4)
                    end
                    if val == 1024 and belowGrid then
                        local valBelow = belowGrid[y] and belowGrid[y][x] or 0
                        if valBelow == 2048 then
                             love.graphics.setColor(0, 0.5, 1, 0.8)
                             love.graphics.circle("fill", drawX + tileSize/2, drawY + tileSize/2, tileSize/4)
                        end
                    end
                    
                    -- 2. Draw Traversal Overlay (Green = reachable, Dark Red = unreachable)
                    if showTraversal and isValidTile(val) then
                        local isReachable = currentReachable and currentReachable[y] and currentReachable[y][x]
                        if isReachable then
                            love.graphics.setColor(REACHABLE_COLOR)
                        else
                            love.graphics.setColor(UNREACHABLE_COLOR)
                        end
                        love.graphics.rectangle("fill", drawX + 2, drawY + 2, tileSize - 4, tileSize - 4)
                    end
                else
                    love.graphics.setColor(VOID_COLOR)
                    love.graphics.rectangle("fill", drawX, drawY, tileSize, tileSize)
                end

                -- 3. Draw EXACT Mask Overlay
                if showMask and maskGrid[y] and maskGrid[y][x] then
                    -- Draw a red border or tint
                    love.graphics.setColor(1, 0, 0, 0.5) 
                    love.graphics.rectangle("fill", drawX + 4, drawY + 4, tileSize - 8, tileSize - 8)
                end
            end
        end
    end

    love.graphics.pop()
    
    -- UI
    love.graphics.setColor(1,1,1)
    love.graphics.print("Layer: " .. currentLayer .. " / " .. height, 10, 10)
    love.graphics.print(string.format("Maze Size: %d x %d", width, length), 10, 30)
    
    -- Calculate fill percentage for current layer
    local totalCells = width * length
    local filledCells = 0
    if currentGrid then
        for y = 1, length do
            for x = 1, width do
                if currentGrid[y] and isValidTile(currentGrid[y][x]) then
                    filledCells = filledCells + 1
                end
            end
        end
    end
    local fillPct = totalCells > 0 and (filledCells / totalCells * 100) or 0
    love.graphics.print(string.format("Fill: %d/%d (%.2f%%)", filledCells, totalCells, fillPct), 10, 50)
    
    if showMask then
        love.graphics.setColor(1, 0.2, 0.2)
        love.graphics.print("MASK OVERLAY: ON (Red = C Code marked this VALID)", 10, 70)
    else
        love.graphics.setColor(0.5, 0.5, 0.5)
        love.graphics.print("MASK OVERLAY: OFF (Press 'M')", 10, 70)
    end
    
    if showTraversal then
        love.graphics.setColor(0.2, 1, 0.4)
        love.graphics.print("TRAVERSAL: ON (Green=Reachable, DarkRed=Unreachable) (Press 'T')", 10, 90)
        -- Show stats
        local totalValid, totalReachable = 0, 0
        if currentGrid and currentReachable then
            for y = 1, length do
                for x = 1, width do
                    if currentGrid[y] and isValidTile(currentGrid[y][x]) then
                        totalValid = totalValid + 1
                        if currentReachable[y] and currentReachable[y][x] then
                            totalReachable = totalReachable + 1
                        end
                    end
                end
            end
        end
        local pct = totalValid > 0 and (totalReachable/totalValid*100) or 0
        if pct < 100 then
            love.graphics.setColor(1, 0.3, 0.3)
        else
            love.graphics.setColor(0.3, 1, 0.3)
        end
        love.graphics.print(string.format("Reachable: %d/%d (%.2f%%)", totalReachable, totalValid, pct), 10, 110)
    else
        love.graphics.setColor(0.5, 0.5, 0.5)
        love.graphics.print("TRAVERSAL: OFF (Press 'T')", 10, 90)
    end
    
    love.graphics.setColor(1, 1, 1)
    love.graphics.print("Controls: (R) Reload, (C) Center, (M) Mask, (T) Traversal", 10, height*tileSize > 700 and 700 or 140)
end

function centerCamera()
    if width == 0 then return end
    local scaleX = (love.graphics.getWidth() - 50) / (width * tileSize)
    local scaleY = (love.graphics.getHeight() - 50) / (length * tileSize)
    camZoom = math.min(scaleX, scaleY)
    local mazePixelW = width * tileSize * camZoom
    local mazePixelH = length * tileSize * camZoom
    camX = (love.graphics.getWidth() - mazePixelW) / 2
    camY = (love.graphics.getHeight() - mazePixelH) / 2
end

function love.wheelmoved(x, y)
    local mouseX, mouseY = love.mouse.getPosition()
    local oldZoom = camZoom
    if y > 0 then camZoom = camZoom * 1.1 elseif y < 0 then camZoom = camZoom / 1.1 end
    camX = mouseX - (mouseX - camX) * (camZoom / oldZoom)
    camY = mouseY - (mouseY - camY) * (camZoom / oldZoom)
end

function love.mousemoved(x, y, dx, dy)
    if love.mouse.isDown(2) or love.mouse.isDown(3) then
        camX = camX + dx
        camY = camY + dy
    end
end

function love.keypressed(key)
    if key == "r" then 
        loadMaze3D() 
        loadMask()
        runFloodFill()
    elseif key == "escape" then 
        love.event.quit() 
    elseif key == "c" then 
        centerCamera()
    elseif key == "m" then 
        showMask = not showMask 
    elseif key == "t" then
        showTraversal = not showTraversal
    elseif key == "=" or key == "kp+" then 
        if currentLayer < height then currentLayer = currentLayer + 1 end
    elseif key == "-" or key == "kp-" then 
        if currentLayer > 1 then currentLayer = currentLayer - 1 end
    end
end