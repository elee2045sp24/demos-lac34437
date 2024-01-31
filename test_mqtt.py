import paho.mqtt.client as mqtt
import struct

# MQTT broker details
broker_address = "mqtt.ugavel.com"
broker_port = 1883
username = "class_user"
password = "class_password"
topic = "ugaelee2045demo/user/topic_clicker_data"

# Callback when a message is received
def on_message(client, userdata, message):
    payload = message.payload
    message_size = len(payload)

    # Define the struct format based on MyData structure
    data_format = 'fffffffib20s20s20s'
    data_size = struct.calcsize(data_format)

    # Unpack the received data
    received_data = struct.unpack(data_format, payload[:data_size])

    # Process the received data
    print("Received complete message:")
    print("Accelerometer X:", received_data[0])
    print("Accelerometer Y:", received_data[1])
    print("Accelerometer Z:", received_data[2])
    print("Gyroscope X:", received_data[3])
    print("Gyroscope Y:", received_data[4])
    print("Gyroscope Z:", received_data[5])
    print("Battery Voltage:", received_data[6])
    print("Mic Value:", received_data[7])
    print("Hand State:", bool(received_data[8]))
    print("Current Time:", received_data[9].decode("latin-1", errors='replace'))
    print("First Name:", received_data[10].split(b'\x00')[0].decode("utf-8", errors='replace'))
    print("Last Name:", received_data[11].split(b'\x00')[0].decode("utf-8", errors='replace'))
    print()


# Create an MQTT client
client = mqtt.Client()

# Set the username and password for the broker
client.username_pw_set(username, password)

# Set the on_message callback
client.on_message = on_message

# Connect to the broker
client.connect(broker_address, broker_port, keepalive=60)

# Subscribe to the topic
client.subscribe(topic)

# Start the MQTT loop
client.loop_forever()