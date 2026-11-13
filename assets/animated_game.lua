-- Run from ember/: ember_demo --backend cpu --lua assets/animated_game.lua --window
local hero, friend, last_clip
function start()
    engine.clear()
    hero = engine.mesh("assets/robot.glb", -1.2, 0, 0)
    friend = engine.mesh("assets/robot.glb", 1.2, 0, 0) -- shared model data, independent pose
    engine.play(hero, "Walk")
    engine.play(friend, "Wave")
    local baton = engine.box(0, -0.7, 0, 0.09, 0.5, 0.09, 1, 0.75, 0.1)
    engine.attach(baton, friend, "RightArm")
    engine.box(0, -0.07, 0, 12, 0.08, 12, 0.55, 0.60, 0.65)
    engine.camera(5, 3.5, -8, 0, 1, 0)
    last_clip = 0
end
function update(time)
    local phase = math.floor(time / 3)
    if phase ~= last_clip then
        engine.play(hero, phase % 2 == 0 and "Walk" or "Wave", 0.25, true)
        last_clip = phase
    end
end
