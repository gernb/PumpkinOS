function cleanup_callback()
  if pumpkin then
    pumpkin.finish()
  end
end

pit.cleanup(cleanup_callback)

pumpkin = pit.loadlib("libos")
pumpkin.init()
--pumpkin.start(320, 320, 32, false, false, false, "Launcher")
--pumpkin.start(1024, 680, 16, false, false, false, "Launcher")
