function start()
    first = engine.box(0, 0, 0, 1, 1, 1, 1, 0, 0)
    second = engine.box(10, 0, 0, 1, 1, 1, 0, 1, 0)
    first_collider = engine.collider(first)
end

function update()
    engine.position(first, 99, 0, 0)
end

function cue()
    engine.raycast(first_collider, 0, 0, -4, 0, 0, 1, 10)
end
