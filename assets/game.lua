-- Native C++ core; Lua only changes game state. No rendering/pixel loops in Lua.
local player
local x = 0
function start()
    engine.clear()
    engine.box(0, -0.2, 0, 24, 0.4, 24, 0.28, 0.4, 0.46)
    player = engine.box(0, 1.5, 0, 2, 2, 2, 0.95, 0.35, 0.15)
    for i = -4, 4 do
        engine.box(i * 2, 1, 5, 0.6, 2, 0.6, 0.3, 0.7, 0.7)
    end
    engine.light(-2, 3, -2, 0.2, 0.8, 1, 8)
    engine.camera(10, 7, -13, 0, 1, 0)
    engine.cue(2, 100)
end
function update(t)
    engine.rotation(player, t * 0.5)
    engine.position(player, x, 1.5 + math.sin(t) * 0.3, 0)
end
function key_down(t, key)
    if key == 97 then x = x - 0.3 end -- A
    if key == 100 then x = x + 0.3 end -- D
end
function cue(t, id)
    print("Timeline event", id, t)
end
