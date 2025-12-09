import logging
import asyncio
import re

from aiocoap import *

logging.basicConfig(level=logging.INFO)

async def fetch_sensor(session, uri):
    """Request a single CoAP resource and print response."""
    request = Message(code=GET, uri=uri)
    try:
        response = await session.request(request).response
        data = response.payload.decode('utf-8')
        y = int(re.search(r'y:([-\d]+)', data).group(1))
        return y
    except Exception as e:
        print(f"{uri} -> ERROR: {e}")
        return 0
    
async def led(session, color, uri):
    request = Message(code=PUT, uri=uri, payload=color.encode('utf-8'))
    response = await session.request(request).response
    return response

async def main():
    protocol = await Context.create_client_context()

    while True:
        print("\n--- Running sensor scan ---")

        request = Message(
            code=GET,
            uri="coap://[2001:67c:254:b0b2:affe:2000:0:1]/resource-lookup/"
        )

        try:
            response = await protocol.request(request).response
        except Exception as e:
            print("Failed to fetch resource:", e)
            await asyncio.sleep(1)
            continue

        # Parse link-format
        links = re.findall(r'<([^>]+)>', response.payload.decode())

        sensor_accel_links = [l for l in links if "sensors/accel" in l]
        led_links = [l for l in links if "led/color" in l]

        print("Requesting sensor endpoints...")
        tasks = [fetch_sensor(protocol, uri) for uri in sensor_accel_links]
        results = await asyncio.gather(*tasks)

        # Check values
        in_range = any(value >= 500 for value in results)

        if in_range:
            print("✔ Switching LEDs to RED")
            led_tasks = [led(protocol, "255,0,0", uri) for uri in led_links]
        else:
            print("✘ Switching LEDs to WHITE")
            led_tasks = [led(protocol, "255,255,255", uri) for uri in led_links]

        await asyncio.gather(*led_tasks)

        # Wait 1 second before next loop
        await asyncio.sleep(1)


if __name__ == "__main__":
    asyncio.run(main())