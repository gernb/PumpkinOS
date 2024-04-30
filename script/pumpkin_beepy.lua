function cleanup_callback()
  if wp then
    wp.finish()
  end
  if pumpkin then
    pumpkin.finish()
  end
end

pit.cleanup(cleanup_callback)

wp = pit.loadlib("libbeepy")

if not wp then
  print("window provider not found")
  pit.finish(0)
  return
end

localVFS = "./vfs/"

wp.start(localVFS)
battery = wp.battery_level()

pit.mount(localVFS, "/")

pumpkin = pit.loadlib("libos")
pumpkin.init(1, battery)
pumpkin.start(400, 240, 32, false, false, false, "Launcher")
