import asyncio
import sys
import datetime
import csv
from itertools import count, takewhile
from typing import Iterator
from bleak import BleakClient, BleakScanner
from bleak.backends.device import BLEDevice

# UUIDs for the Nordic UART Service and its characteristics
UART_SERVICE_UUID = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
UART_RX_CHAR_UUID = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
UART_TX_CHAR_UUID = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

# Generate a filename with the current date and time when the script starts
current_time = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
filename = f"rxdata_{current_time}.csv"

# Dictionary to store connected clients and their indices
connected_clients = {}  

# Function to slice data into chunks of a specified size
def sliced(data: bytes, n: int) -> Iterator[bytes]:
    return takewhile(len, (data[i: i + n] for i in count(0, n)))

# Main function for the program
async def main():
    # Function to match devices that advertise the UART service UUID
    """     def match_nus_uuid(device: BLEDevice, adv: AdvertisementData):
        return UART_SERVICE_UUID.lower() in adv.service_uuids """
    def match_nus_uuid(device: BLEDevice):
        uuids = device.metadata.get('uuids', [])
        return UART_SERVICE_UUID.lower() in uuids

    # Discover all BLE devices and filter those that match the UART service UUID
    devices = await BleakScanner.discover()
    matching_devices = [device for device in devices if match_nus_uuid(device)]
    #matching_devices = [device for device in devices if match_nus_uuid(device, device.metadata["advertisement_data"])]

    # If no matching devices are found, exit the program
    if not matching_devices:
        print("No matching devices found. Exiting.")
        sys.exit(1)

    # List the matching devices and prompt the user to select one
    print("Matching devices found:")
    for index, device in enumerate(matching_devices):
        print(f"{index}: {device.name}")

    device_indices_input = input("Enter the indices of the devices to connect to (separated by commas): ")
    device_index = [int(index.strip()) for index in device_indices_input.split(',')]
        
    # Check if all device indices are in the valid range
    if all(0 <= index < len(matching_devices) for index in device_index):
        # Proceed with connecting to the devices
        # Your code to connect to devices goes here
        pass
    else:
        print(f"Invalid device indices. Please enter indices between 0 and {len(matching_devices) - 1}.")
        sys.exit(1)
    
    def handle_disconnect(client: BleakClient):
        print(f"Device {client.address} was disconnected.")
        if client.address in connected_clients:
            del connected_clients[client.address]  # Remove the disconnected client from the list
        if not connected_clients:  # Check if there are no more connected clients
            print("All devices are disconnected, goodbye.")
            for task in asyncio.all_tasks():
                task.cancel()

    # Function to save received data to a text file
    """ def save_data_to_file(device_index, data):
        with open("received_data.txt", "a") as file:
            file.write(f"{device_index}: {data}\n") """
    # Function to save received data to a CSV file
    def save_data_to_file(filename, data):
        # Write the data to the CSV file
        with open(filename, 'a', newline='') as csvfile:
            writer = csv.writer(csvfile)
            for row in data:
                writer.writerow(row)
    
    # Function to handle data received from the device
    """ def handle_rx(index, _, data: bytearray):
        device_index = connected_clients.get(index, "Unknown")
        print(f"Received from device {device_index}:", data)
        save_data_to_file(device_index, data) """
    def handle_rx(index, _, data: bytearray):
        device_index = connected_clients.get(index, "Unknown")
        #print(f"Received from device {device_index}:", data)

        # Process the bytearray to extract the data
        data_str = data.decode('utf-8')  # Convert bytearray to string
        data_rows = [row.split(',') for row in data_str.split('\n')]  # Split the data into rows and columns

        # Save the processed data to the CSV file
        save_data_to_file(filename, data_rows)
    
    # Connect to the selected devices and set up notifications and data handling
    connected_clients = {}  # Initialize as a dictionary
    for index in device_index:
        selected_device = matching_devices[index]
        client = BleakClient(selected_device, disconnected_callback=handle_disconnect)
        await client.connect()
        await client.start_notify(UART_TX_CHAR_UUID, lambda _, data, index=index: handle_rx(index, _, data))
        connected_clients[index] = client  # Use index as the key
        print(f"Connected to device {index}: {selected_device.name}")

    # Loop to read data from stdin and send it to all connected devices
    while True:
        #data = await loop.run_in_executor(None, sys.stdin.buffer.readline)
        data = await loop.run_in_executor(None, lambda: sys.stdin.buffer.readline().rstrip(b'\r\n'))

        # Check if the input is "datetime" to send the current date and time
        if data.decode('utf-8').lower() == "datetime":
            current_time = datetime.datetime.now().strftime("%Y/%m/%d %H:%M:%S")
            data = current_time.encode('utf-8')
            print("Sending current date and time:", current_time)

        # Check if the input is "Disconnect" to disconnect all devices
        if data.decode('utf-8').lower() == "disconnect":
            print("Disconnecting all devices...")
            for index, client in connected_clients.items():
                await client.disconnect()
                print(f"Disconnected device {index}: {client.address}")
            connected_clients.clear()  # Clear the connected clients dictionary
            break

        if not data:
            break

        for index, client in connected_clients.items():
            nus = client.services.get_service(UART_SERVICE_UUID)
            rx_char = nus.get_characteristic(UART_RX_CHAR_UUID)
            for s in sliced(data, rx_char.max_write_without_response_size):
                await client.write_gatt_char(rx_char, s, response=False)

        print("Sent:", data)

    # Disconnect all clients when done
    for index, client in connected_clients.items():
        await client.disconnect()


# Entry point of the program
if __name__ == "__main__":
    try:
        loop = asyncio.get_event_loop()
        loop.run_until_complete(main())
    except asyncio.CancelledError:
        pass
