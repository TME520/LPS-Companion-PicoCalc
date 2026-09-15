#!/usr/bin/env python3
"""Check UF2 framing, RP2040 family, block numbers and flash addresses."""
import pathlib,struct,sys
p=pathlib.Path(sys.argv[1]);data=p.read_bytes()
assert data and len(data)%512==0, 'Invalid UF2 length'
count=len(data)//512
for i in range(count):
 b=data[i*512:(i+1)*512]
 m0,m1,flags,addr,size,num,total,family=struct.unpack_from('<8I',b)
 assert (m0,m1)==(0x0A324655,0x9E5D5157)
 assert struct.unpack_from('<I',b,508)[0]==0x0AB16F30
 assert flags&0x2000 and family==0xE48BFF56, 'Not RP2040 firmware'
 assert size==256 and num==i and total==count
 assert 0x10000000<=addr and addr+size<=0x10200000, 'Outside Pico 2 MiB flash'
 assert addr==0x10000000+i*256, 'Unexpected flash layout'
print(f'UF2 verified: {count} blocks, {len(data)} bytes, RP2040.')
