t0_cnt = 0
ircon = 0
rfrdy_delayed = 0

function hasbit(x, b)
  local p = b + 1
  return x % (p + p) >= p
end
function sfr_p0(write, data)
  return "P0", data
end
function sfr_tcon(write, data)
  return "TCON", data
end
function sfr_tmod(write, data)
  return "TMOD", data
end
function sfr_tl0(write, data)
  if not write then
    data = t0_cnt & 0xff
    t0_cnt = t0_cnt + 1
  end
  return "TL0", data
end
function sfr_tl1(write, data)
  return "TL1", data
end
function sfr_th0(write, data)
  if not write then
    data = t0_cnt >> 8
  end
  return "TH0", data
end
function sfr_th1(write, data)
  return "TH1", data
end
function sfr_aa(write, data)
  return "FREG", data
end
function sfr_ircon(write, data)
  if write then
    ircon = data
  else
    if rfrdy_delayed > 0 then
      if rfrdy_delayed == 1 and not hasbit(ircon, 0) then
        ircon = ircon + 1
      end
      rfrdy_delayed = rfrdy_delayed - 1
    end
    data = ircon
  end
  return "IRCON", data
end
function sfr_rfdat(write, data)
  if write then
    rfrdy_delayed = 2
  end
  return "RFDAT", data
end

ramd_hooks = {
  [0x80] = sfr_p0,
  [0x88] = sfr_tcon,
  [0x89] = sfr_tmod,
  [0x8a] = sfr_tl0,
  [0x8b] = sfr_tl1,
  [0x8c] = sfr_th0,
  [0x8d] = sfr_th1,
  [0xaa] = sfr_aa,
  [0xc0] = sfr_ircon,
  [0xe5] = sfr_rfdat,
}

print("Mapping NRF24 memory...")
emu.make_ram("sram", 0x400)
emu.map_rom("rom", 0x00, 0x40)
emu.map_xram("sram", 0x00, 0x04)
