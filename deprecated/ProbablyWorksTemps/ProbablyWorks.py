'''
Note that you will need to replace the COM3 and COM4 values with the appropriate port names for 
your system. Also, keep in mind that the code assumes that the LCD is connected to the second 
serial port, while the OBD-II adapter is connected to the first one.
'''




import serial
import time
import math

ser = serial.Serial('COM3', 9600) # replace 'COM3' with the appropriate port
lcd = serial.Serial('COM4', 9600) # replace 'COM4' with the appropriate port
time.sleep(2) # wait for the serial connection to initialize

rxData = bytearray([122, 97, 99, 104, 0])
rxIndex = 0
vehicleRPM = 10000

def getResponse():
    global rxData, rxIndex
    inChar = 0
    while (inChar != b'\r'):
        if ser.in_waiting:
            inChar = ser.read()
            if inChar == b'\r':
                rxData[rxIndex] = 0
                rxIndex = 0
            else:
                rxData[rxIndex] = inChar
                rxIndex += 1

def setup():
    lcd.write(b'\x12')
    lcd.write(b'\x17')
    lcd.write(b'\x22')
    time.sleep(0.25)
    lcd.write(b'\x0C')
    time.sleep(1)
    lcd.write(b'           ')
    lcd.write(b'ECT: ')
    lcd.write(b'\x94')
    lcd.write(b'IAT: ')

def loop():
    global rxData
    ser.write(b'0105\r')
    getResponse()
    getResponse()
    getResponse()
    lcd.write(b'\x86')
    ECT = int(round((1.8 * (int(rxData[6:], 16) - 40)) + 32))
    if len(str(ECT)) == 2:
        lcd.write(b'\x87')
    if len(str(ECT)) == 1:
        lcd.write(b'\x88')
    lcd.write(str(ECT).encode())
    lcd.write(b'\n')
    
    ser.write(b'010F\r')
    getResponse()
    getResponse()
    getResponse()
    lcd.write(b'\x9A')
    lcd.write(b'         ')
    lcd.write(b'\x9B')
    IAT = int(round((1.8 * (int(rxData[6:], 16) - 40)) + 32))
    if IAT < 100:
        lcd.write(b'\x9C')
    lcd.write(str(IAT).encode())
    lcd.write(b'\n')
    
    time.sleep(1)

setup()
while True:
    loop()
