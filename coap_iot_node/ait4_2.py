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
        print(f"{uri} -> {response.payload.decode('utf-8')}")
    except Exception as e:
        print(f"{uri} -> ERROR: {e}")

async def main():
    protocol = await Context.create_client_context()

    request = Message(code=GET, uri="coap://[2001:67c:254:b0b2:affe:2000:0:1]/resource-lookup/")

    try:
        response = await protocol.request(request).response
    except Exception as e:
        print("Failed to fetch resource:")
        print(e)
    #else:
    #    print("Result: %s\n%r" % (response.code, response.payload))
        
    links = re.findall(r'<([^>]+)>', str(response.payload))
    
    sensor_links = [f"{l}" for l in links if "sensors" in l]


    print("Requesting sensor endpoints...\n")
    tasks = [fetch_sensor(protocol, uri) for uri in sensor_links]
    await asyncio.gather(*tasks)


if __name__ == "__main__":
    asyncio.run(main())