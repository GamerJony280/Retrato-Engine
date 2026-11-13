seen = 0

function mouse_move(time, x, y, dx, dy)
    assert(time == 1.5 and x == 10 and y == 20 and dx == 3 and dy == -4)
    seen = seen + 1
end

function mouse_down(time, button, x, y)
    assert(time == 2 and button == 1 and x == 8 and y == 9)
    seen = seen + 1
end

function mouse_wheel(time, x, y)
    assert(time == 2.5 and x == -1 and y == 2)
    seen = seen + 1
end

function gamepad_button_down(time, device, button)
    assert(time == 3 and device == 7 and button == 2)
    seen = seen + 1
end

function gamepad_axis(time, device, axis, value)
    assert(time == 4 and device == 7 and axis == 1 and math.abs(value + 0.5) < 0.0001)
    seen = seen + 1
    assert(seen == 5)
end
