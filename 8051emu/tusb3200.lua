function sfr_aa(write, data)
  return "FREG", data
end

ramd_hooks = {
  [0xaa] = sfr_aa
}

print("Mapping TUSB3200 memory...")
emu.load_rom("tusb3200.bootrom", 4096)
emu.make_ram("usbram", 2048)
emu.map_rom("rom", 0x00, 0x20)
emu.map_rom("tusb3200.bootrom", 0x80, 0x10)
emu.map_xram("rom", 0x00, 0x20)
emu.map_xram("usbram", 0xf8, 0x08)
