'''
I used the built-in serial module instead of SoftwareSerial, which is not available in Python.
I used bytearray instead of char[] to represent the rxData buffer. In Python, bytearray is similar 
    to char[], but it is mutable and supports null-terminated strings.
I used encode() to convert strings to bytes before writing them to the serial port.
I added global statements to getResponse() to access
'''





import serial
import time

lcd = serial.Serial(2, 9600)  # SoftwareSerial on pins 2 and 3
rxData = bytearray(b'zach\x00')  # create a bytearray with 20 bytes and null-terminated
rxIndex = 0
vehicleRPM = 10000

def setup():
    # put your setup code here, to run once:
    lcd.write(b'\x0c')  # clear lcd (don't rely on this though)
    lcd.write(b'\x11')  # turn on backlight
    lcd.write(b'\x16')  # turn off cursor
    time.sleep(0.25)
    time.sleep(1)
    lcd.write(b'           ')
    lcd.write(b'STFT: ')
    lcd.write(b'\x94')
    lcd.write(b'LTFT: ')

def loop():
    # put your main code here, to run repeatedly:
    serial.write(b'0106\n')
    getResponse()  # 010C \r
    getResponse()  # SEARCHING... \r
    getResponse()  # 41 0C 12 99 \r (leaves another \r in incoming port?)
    lcd.write(b'\x87')
    lcd.write(str(int(rxData[6:], 16) / 1.28 - 100).encode())
    lcd.write(b'\n')
    
    serial.write(b'0107\n')
    getResponse()  # 010C \r
    getResponse()  # SEARCHING... \r
    getResponse()  # 41 0C 12 99 \r (leaves another \r in incoming port?)
    lcd.write(b'\x9b')
    lcd.write(str(int(rxData[6:], 16) / 1.28 - 100).encode())
    lcd.write(b'\n')
    
    time.sleep(1)

def getResponse():
    global rxData, rxIndex
    inChar = 0
    # Keep reading characters until we get a carriage return
    while inChar != b'\r':
        # If a character comes in on the serial port, we need to act on it.
        if serial.in_waiting > 0:
            # Start by checking if we've received the end of message character ('\r').
            if serial.peek() == b'\r':
                # Clear the Serial buffer
                inChar = serial.read()
                # Put the end of string character on our data string
                rxData[rxIndex] = 0
                # Reset the buffer index so that the next character goes back at the beginning of the string.
                rxIndex = 0
            # If we didn't get the end of message character, just add the new character to the string.
            else:
                # Get the new character from the Serial port.
                inChar = serial.read()
                # Add the new character to the string, and increment the index variable.
                rxData[rxIndex] = inChar
                rxIndex += 1
