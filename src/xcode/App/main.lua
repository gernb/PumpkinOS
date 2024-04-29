function cleanup_callback()
  if pumpkin then
    pumpkin.finish()
  end
end

pit.cleanup(cleanup_callback)

pumpkin = pit.loadlib("libos")
pumpkin.init({density})
pumpkin.start({width}, {height}, 32, false, false, false, "Launcher")
