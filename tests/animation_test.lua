local base = debug.getinfo(1, "S").source:sub(2):match("(.*/)") .. "../assets/"
local robot
function start()
    engine.clear()
    robot = engine.mesh(base .. "robot.glb", 0, 0, 0)
    local clips = engine.clips(robot)
    assert(clips[1] == "Walk" and clips[2] == "Wave")
    engine.play(robot, "Wave", 0, true)
    local x, y, z = engine.bone(robot, "RightArm")
    assert(math.abs(x - 0.53) < 0.001)
    engine.seek(robot, 0.5)
    engine.pause_animation(robot, true)
    local prop = engine.box(0, -0.6, 0, 0.1, 0.4, 0.1, 1, 0.8, 0)
    engine.attach(prop, robot, "RightArm")
    assert(not pcall(engine.play, robot, "MissingClip"))
    engine.camera(4, 3, -6, 0, 1, 0)
end
