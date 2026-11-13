local actor
function start()
    actor=engine.box(0,0,0,1,1,1,1,1,1)
end
function update(time)
    engine.position(actor,time*3,0,0)
end
