import paho.mqtt.client as mqtt

broker = "broker.hivemq.com"
topic = "buet/cse/2105091/led" # TODO: Put the same topic you used in the ESP code

client = mqtt.Client()
client.connect(broker)

# TODO: the following is an example of publishing a message. You have to modify it so that the python code will run infinitely and wait for input from keyboard. If user presses 'y', it will send "ON"; it will send "OFF" if 'n' is pressed. The program will terminate if user presses 'q'.
#client.publish(topic, "ON")
while True:
    user_input = input("Enter 'y' to turn ON, 'n' to turn OFF, or 'q' to quit: ")
    if user_input == 'y':
        client.publish(topic, "ON")
    elif user_input == 'n':
        client.publish(topic, "OFF")
    elif user_input == 'q':
        exit()
