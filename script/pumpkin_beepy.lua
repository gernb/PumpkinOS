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

wp.start()

pit.mount("./vfs/", "/")

pumpkin = pit.loadlib("libos")
pumpkin.init(1)
pumpkin.start(400, 240, 32, false, false, false, "Launcher")
