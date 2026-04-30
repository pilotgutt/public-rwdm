# webcli_test.py
import asyncio
import websockets

async def main():
    uri = "ws://localhost:8765"
    async with websockets.connect(uri) as ws:
        print("Connected to", uri)
        msg = await ws.recv()
        print("Received from server:", msg)

asyncio.run(main())