#include <SoftwareSerial.h>

SoftwareSerial lcd(2,3);

char rxData[20] = {'z', 'a', 'c', 'h', '\0'};
char rxIndex=0;
int vehicleRPM=10000;
void setup() {
  // put your setup code here, to run once:
  lcd.begin(9600);
  Serial.begin(9600);
  //lcd.write(210); // make a tone (doesn't work?)
  //lcd.write(222); // make a tone
  lcd.write(12); // clear lcd (dont rely on this though)
  lcd.write(17); // turn on backlight
  lcd.write(22); // turn off cursor
  delay(250);
  delay(1000);
  lcd.println("           ");
  lcd.print("ECT: ");
  lcd.write(148);
  lcd.print("IAT: ");
}

void loop() {
  // put your main code here, to run repeatedly:
  Serial.println("0105");
  getResponse(); // 010C \r
  getResponse(); // SEARCHING... \r
  getResponse(); // 41 0C 12 99 \r (leaves another \r in incoming port?)
  lcd.write(134);
  const int ECT = (int)round((1.8*(strtol(&rxData[6],0,16)-40))+32);
  if(sizeof(ECT) == 2){
    lcd.write(135);
  }
  if(sizeof(ECT) == 1){
    lcd.write(136);
  }
  lcd.println(ECT);
  
  Serial.println("010F");
  getResponse(); // 010C \r
  getResponse(); // SEARCHING... \r
  getResponse(); // 41 0C 12 99 \r (leaves another \r in incoming port?)
  
  lcd.write(154);
  lcd.print("         ");
  lcd.write(155);
  const int IAT = (int)round((1.8*(strtol(&rxData[6],0,16)-40))+32);
  if(IAT < 100){
    lcd.write(156);
  }
  lcd.println(IAT);
  
  delay(1000);
  //lcd.write(13);
  //lcd.write(13);
}
void getResponse(void){
  char inChar=0;
  //Keep reading characters until we get a carriage return
  while(inChar != '\r'){
    //If a character comes in on the serial port, we need to act on it.
    if(Serial.available() > 0){
      //Start by checking if we've received the end of message character ('\r').
      if(Serial.peek() == '\r'){
        //Clear the Serial buffer
        inChar=Serial.read();
        //Put the end of string character on our data string
        rxData[rxIndex]='\0';
        //Reset the buffer index so that the next character goes back at the beginning of the string.
        rxIndex=0;
      }
      //If we didn't get the end of message character, just add the new character to the string.
      else{
        //Get the new character from the Serial port.
        inChar = Serial.read();
        //Add the new character to the string, and increment the index variable.
        rxData[rxIndex++]=inChar;
      }
    }
  }
}

