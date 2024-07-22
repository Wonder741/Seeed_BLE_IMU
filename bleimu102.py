import asyncio
import sys
import datetime
import csv
import re
import os
from itertools import count, takewhile
from typing import Iterator
from bleak import BleakClient, BleakScanner
from bleak.backends.device import BLEDevice
from bleak.backends.scanner import AdvertisementData

# Create a subfolder 'data' in the current directory if it does not exist
subfolder = '1807test'
# Generate a filename with the current date and time when the script starts
current_time = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
if not os.path.exists(subfolder):
    os.makedirs(subfolder)
print("Press 'tt' to set the time of ble device.")
print("Press 'rr' to start logging data!")
print("Press 'ss' to stop logging data.")
print("Press 'dd' to disconnect ble devices!")
print("After stop logging data or disconnection, data will save to folder 'subfolder'")
print("Odd number IMUs will save to date_time_L.csv, Odd number IMUs will save to date_time_R.csv") 

# UUIDs for the Nordic UART Service and its characteristics
UART_SERVICE_UUID = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
UART_RX_CHAR_UUID = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
UART_TX_CHAR_UUID = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

# Dictionary to store connected clients and their indices
connected_clients = {}  

# Function to slice data into chunks of a specified size
def sliced(data: bytes, n: int) -> Iterator[bytes]:
    return takewhile(len, (data[i: i + n] for i in count(0, n)))

# Main function for the program
async def main():
    start_flag = False
    # Function to match devices based on the advertised UART service UUID
    def match_nus_uuid(advertisement_data: AdvertisementData):
        uuids = [uuid.lower() for uuid in advertisement_data.service_uuids]
        return UART_SERVICE_UUID.lower() in uuids

    # Custom callback to handle discovered devices
    def handle_discovery(device: BLEDevice, advertisement_data: AdvertisementData):
        # Use device address as a unique identifier to avoid duplicates
        if device.address not in discovered_devices:
            if match_nus_uuid(advertisement_data):
                discovered_devices.add(device.address)
                matching_devices.append(device)

    matching_devices = []
    discovered_devices = set()  # Set to track discovered device addresses

    # Setup scanner with callback
    scanner = BleakScanner(detection_callback=handle_discovery)

    # Start scanning
    await scanner.start()
    await asyncio.sleep(10)  # Scan for 10 seconds
    await scanner.stop()

    # Check if any matching devices were found
    if not matching_devices:
        print("No matching devices found. Exiting.")
        sys.exit(1)

    # List the matching devices and prompt the user to select one
    print("Matching devices found:")
    for index, device in enumerate(matching_devices):
        print(f"{index}: {device.name} ({device.address})")

    #device_indices_input = input("Enter the indices of the devices to connect to (separated by commas): ")
    #device_index = [int(index.strip()) for index in device_indices_input.split(',')]
    # Automatically connect to all found devices
    device_index = list(range(len(matching_devices)))
        
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
    def decode_byte_stream(filename, byte_stream):
        # Decode the device name
        index = 0
        pack_time = 14
        device_name = ''
        while byte_stream[index] != ord(','):
            device_name += chr(byte_stream[index])
            index += 1
        index += 1  # Skip the delimiter
        #print(device_name)

        # Initialize lists to store the decoded values
        miliBuffer = []
        sensorBuffer = []

        # Decode miliBuffer values
        for _ in range(pack_time):
            value = (byte_stream[index] << 24) | (byte_stream[index + 1] << 16) | (byte_stream[index + 2] << 8) | byte_stream[index + 3]
            miliBuffer.append(value)
            index += 4
        index += 1  # Skip the delimiter
        #print(miliBuffer)

        # Decode sensorBuffer values (16-bit integers)
        for _ in range(6 * pack_time):
            value = (byte_stream[index] << 8) | byte_stream[index + 1]
            if value & 0x8000:  # if sign bit is set (16-bit: 32768-65535)
                value = -((value ^ 0xFFFF) + 1)  # 2's complement sign conversion
            sensorBuffer.append(value)
            index += 2
        #print(sensorBuffer)

        # Check if the file exists and is non-empty
        file_exists = os.path.isfile(filename) and os.path.getsize(filename) > 0

        # Write to CSV file
        with open(filename, 'a', newline='') as csvfile:
            csvwriter = csv.writer(csvfile)
            # Write the header only if the file does not already exist
            if not file_exists:
                csvwriter.writerow(['Device Name', 'miliBuffer'] + [f'sensorBuffer_{i}' for i in range(1, 7)])

            # Write the data rows
            for i in range(pack_time):
                row = [device_name, miliBuffer[i]] + sensorBuffer[i * 6:(i + 1) * 6]
                csvwriter.writerow(row)
    
    # Function to handle data received from the device
    def handle_rx(index, _, data: bytearray):
        device_index = connected_clients.get(index, "Unknown")
        #print(f"Received from device {device_index}:", data)
        if start_flag:
            # Process the bytearray to extract the data
            decode_byte_stream(filename, data)

    def reconstruct_csv(input_file, output_file_L, output_file_R):
        with open(input_file, 'r', newline='') as infile:
            reader = csv.reader(infile)
            header = next(reader)  # Read the header

            with open(output_file_L, 'w', newline='') as outfile_L, open(output_file_R, 'w', newline='') as outfile_R:
                writer_L = csv.writer(outfile_L)
                writer_R = csv.writer(outfile_R)

                # Write the header to both output files
                writer_L.writerow(header)
                writer_R.writerow(header)

                for row in reader:
                    device_name = row[0]
                    if device_name[-1] == 'L':
                        writer_L.writerow(row)
                    elif device_name[-1] == 'R':
                        writer_R.writerow(row)

    
    # Connect to the selected devices and set up notifications and data handling
    connected_clients = {}  # Initialize as a dictionary
    # Full path including the subfolder
    current_time = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    filename = os.path.join(subfolder, f"rxdata_{current_time}.csv")
    output_file_L = os.path.join(subfolder,f"{current_time}_L.csv")
    output_file_R = os.path.join(subfolder,f"{current_time}_R.csv")
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
        if data.decode('utf-8').lower() == "tt":
            time_to_send = datetime.datetime.now().strftime("%Y/%m/%d %H:%M:%S")
            data = time_to_send.encode('utf-8')
            for index, client in connected_clients.items():
                nus = client.services.get_service(UART_SERVICE_UUID)
                rx_char = nus.get_characteristic(UART_RX_CHAR_UUID)
                for s in sliced(data, rx_char.max_write_without_response_size):
                    await client.write_gatt_char(rx_char, s, response=False)
            print("Sending current date and time:", time_to_send)

        if data.decode('utf-8').lower() == "rr":
            current_time = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
            # Full path including the subfolder
            filename = os.path.join(subfolder, f"rxdata_{current_time}.csv")
            output_file_L = os.path.join(subfolder,f"{current_time}_L.csv")
            output_file_R = os.path.join(subfolder,f"{current_time}_R.csv")
            start_flag = True
            print('Start to log data...')
        
        if data.decode('utf-8').lower() == "ss":
            start_flag = False
            reconstruct_csv(filename, output_file_L, output_file_R)
            print('Stop to logging data!')

        # Check if the input is "Disconnect" to disconnect all devices
        if data.decode('utf-8').lower() == "dd":
            print("Disconnecting all devices...")
            if start_flag:
                start_flag = False
                reconstruct_csv(filename, output_file_L, output_file_R)
            for index, client in connected_clients.items():
                await client.disconnect()
                print(f"Disconnected device {index}: {client.address}")
            connected_clients.clear()  # Clear the connected clients dictionary
            break

        if not data:
            break

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