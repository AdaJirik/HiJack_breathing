"""Hardware smoke test: advertise/connect, subscribe, START, classify, STOP."""
import asyncio
import struct
from bleak import BleakClient, BleakScanner

SERVICE="e40d1000-7c60-4c1a-8bbc-42a4a91131c0"
STATE="e40d1001-7c60-4c1a-8bbc-42a4a91131c0"
CONTROL="e40d1002-7c60-4c1a-8bbc-42a4a91131c0"

def decode(data):
    if len(data)!=20 or data[0]!=1:
        raise ValueError("Unexpected BLE payload")
    return dict(state=data[1],rgb=list(data[2:5]),running=bool(data[6]),
                sequence=struct.unpack_from("<I",data,8)[0],inference_us=struct.unpack_from("<I",data,16)[0])

async def main():
    device=await BleakScanner.find_device_by_filter(lambda device,adv: (adv.local_name or "").startswith("HiJack-Detect"),timeout=10)
    if not device:raise RuntimeError("HiJack-Detect not found; ensure board is on and no phone is connected")
    print("Found:",device.name,flush=True)
    packets=[]
    async with BleakClient(device,timeout=15) as client:
        assert client.services.get_service(SERVICE)
        await client.start_notify(STATE,lambda _,data:packets.append(decode(data)))
        await client.write_gatt_char(CONTROL,b"START",response=True)
        await asyncio.sleep(4)
        valid=[p for p in packets if p["state"] in (2,3)]
        assert valid,"No live classification received"
        for p in valid:
            assert p["rgb"]==([0,0,255] if p["state"]==2 else [255,0,255])
        print("Live packet:",valid[-1],"notifications:",len(packets),flush=True)
        await client.write_gatt_char(CONTROL,b"STOP",response=True)
        await asyncio.sleep(.8)
        stopped=decode(await client.read_gatt_char(STATE))
        assert stopped["state"]==0 and not stopped["running"] and stopped["rgb"]==[0,0,0]
        print("PASS: BLE START, live state/color notifications and STOP verified.",flush=True)

asyncio.run(main())
