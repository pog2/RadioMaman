

/*
 1-6-2011
 Spark Fun Electronics 2011
 Nathan Seidle
  
 This code is public domain but you buy me a beer if you use this and we meet someday (Beerware license).
  
 To use this code, connect the following 5 wires:
 Arduino : Si470x board
 3.3V : VCC
 GND : GND
 A5 : SCLK
 A4 : SDIO
 D2 : RST
 A0 : Trimpot (optional)
  
 Look for serial output at 57600bps.
  
 The Si4703 ACKs the first byte, and NACKs the 2nd byte of a read.
  
 1/18 - after much hacking, I suggest NEVER write to a register without first reading the contents of a chip.
 ie, don't updateRegisters without first readRegisters.
  
 If anyone manages to get this datasheet downloaded
 http://wenku.baidu.com/view/d6f0e6ee5ef7ba0d4a733b61.html
 Please let us know. It seem to be the latest version of the programming guide. It had a change on page 12 (write 0x8100 to 0x07)
 that allowed me to get the chip working..
  
 Also, if you happen to find "AN243: Using RDS/RBDS with the Si4701/03", please share. I love it when companies refer to
 documents that don't exist.
  
 1/20 - Picking up FM stations from a plane flying over Portugal! Sweet! 93.9MHz sounds a little soft for my tastes,s but
 it's in Porteguese.
  
 ToDo:
 Display current status (from 0x0A) - done 1/20/11
 Add RDS decoding - works, sort of
 Volume Up/Down - done 1/20/11
 Mute toggle - done 1/20/11
 Tune Up/Down - done 1/20/11
 Read current channel (0xB0) - done 1/20/11
 Setup for Europe - done 1/20/11
 Seek up/down - done 1/25/11
  
 The Si4703 breakout does work with line out into a stereo or other amplifier. Be sure to test with different length 3.5mm
 cables. Too short of a cable may degrade reception.


 Ajout Pierre

 * Entrée Analog : A0 -> Volume A1 -> Selection Station

 
 */
 
#include <Wire.h>
 
int STATUS_LED = 13;
int resetPin = 2;
int SDIO = A4; //SDA/A4 on Arduino
int SCLK = A5; //SCL/A5 on Arduino
char printBuffer[50];
uint16_t si4703_registers[16]; //There are 16 registers, each 16 bits large
 
#define FAIL  0
#define SUCCESS  1
 
#define SI4703 0x10 //0b._001.0000 = I2C address of Si4703 - note that the Wire function assumes non-left-shifted I2C address, not 0b.0010.000W
#define I2C_FAIL_MAX  10 //This is the number of attempts we will try to contact the device before erroring out
 
//#define IN_EUROPE //Use this define to setup European FM reception. I wuz there for a day during testing (TEI 2011).
 
#define SEEK_DOWN  0 //Direction used for seeking. Default is down
#define SEEK_UP  1
 
//Define the register names
#define DEVICEID 0x00
#define CHIPID  0x01
#define POWERCFG  0x02
#define CHANNEL  0x03
#define SYSCONFIG1  0x04
#define SYSCONFIG2  0x05
#define STATUSRSSI  0x0A
#define READCHAN  0x0B
#define RDSA  0x0C
#define RDSB  0x0D
#define RDSC  0x0E
#define RDSD  0x0F
 
//Register 0x02 - POWERCFG
#define SMUTE  15
#define DMUTE  14
#define SKMODE  10
#define SEEKUP  9
#define SEEK  8
 
//Register 0x03 - CHANNEL
#define TUNE  15
 
//Register 0x04 - SYSCONFIG1
#define RDS  12
#define DE  11
 
//Register 0x05 - SYSCONFIG2
#define SPACE1  5
#define SPACE0  4
 
//Register 0x0A - STATUSRSSI
#define RDSR  15
#define STC  14
#define SFBL  13
#define AFCRL  12
#define RDSS  11
#define STEREO  8

#define IN_EUROPE 1 // <- europe power !
//#define FRANCE_INTER_FREQ 994
#define FRANCE_INTER_FREQ 897 // France inter Bouliac
 
//#define DEBUG 
// Gestion des controles ( volume & stations)
#define DELAY_MEAS_MS 50
#define NB_MEAS_SMOOTH 30
#define FLOAT_PRECISION 0.1

typedef struct 
{
  float val;
  float lastVal;
} voltage;

float sensorToVoltage(int sensorValue)
{
  float voltage = sensorValue * (5.0 / 1023.0);
  return voltage;
}

void printVoltages(float voltage0, float voltage1)
{
  Serial.print("A0: ");
  Serial.print(voltage0);
  Serial.print(" A1: ");
  Serial.print(voltage1);
  Serial.print("\n"); 
}

int hasChanged(void *pData, float precision, float *pDiff)
{
  // Return true if voltage has changed 
  int flag;
  float diff;
  float absDiff;
  voltage *pVolt0 = (voltage *)pData;
  diff = pVolt0->val - pVolt0->lastVal;
  absDiff = (diff>0) ? diff : -diff; // abs(diff); 
  #ifdef DEBUG 
  Serial.print("[hasChanged] diff=]");
  Serial.print(diff);
  
  #endif 
  flag = ( absDiff > precision ) ? 1 : 0;
  if (pDiff != NULL)
  {
    *pDiff = diff;  
  }

  return flag; 
}

void updateVoltage(void *pData)
{
  // Store val in lastValue for next measure
  voltage *pVolt0 = (voltage *)pData;
  pVolt0->lastVal = pVolt0->val;
}

void initVoltage(void *pData)
{
  memset(pData, 0, sizeof(voltage));
}

int analogReadAverage(int potPin)
{
  // Read Analog input and smooth it
  long int val = 0;
  for (int i=0; i< NB_MEAS_SMOOTH ; i++)
  {
    val += analogRead(potPin); 
#ifdef DEBUG
    Serial.print("[analogReadAverage]val:");
    Serial.print(val);
    Serial.print("\n");
#endif
  }  
  val /= NB_MEAS_SMOOTH;
#ifdef DEBUG  
  Serial.print("[analogReadAverage]valMean:");
  Serial.print(val);
  Serial.print("\n");
  delay(1500);
#endif
  return val ;
}

void IncreaseVolume()
{
  byte current_vol;
  si4703_readRegisters(); //Read the current register set
  current_vol = si4703_registers[SYSCONFIG2] & 0x000F; //Read the current volume level
  if(current_vol < 16) current_vol++; //Limit max volume to 0x000F
  si4703_registers[SYSCONFIG2] &= 0xFFF0; //Clear volume bits
  si4703_registers[SYSCONFIG2] |= current_vol; //Set new volume
  si4703_updateRegisters(); //Update
  Serial.println("Volume: ");
  Serial.println(current_vol, DEC);
}
void DecreaseVolume()
{
    byte current_vol;
    si4703_readRegisters(); //Read the current register set
    current_vol = si4703_registers[SYSCONFIG2] & 0x000F; //Read the current volume level
    if(current_vol > 0) current_vol--; //You can't go lower than zero
    si4703_registers[SYSCONFIG2] &= 0xFFF0; //Clear volume bits
    si4703_registers[SYSCONFIG2] |= current_vol; //Set new volume
    si4703_updateRegisters(); //Update
    Serial.println("Volume: ");
    Serial.println(current_vol, DEC);
}
// set volume to given volStep
void SetVolumeStep( byte volStep)
{
    si4703_readRegisters(); //Read the current register set
    //current_vol = si4703_registers[SYSCONFIG2] & 0x000F; //Read the current volume level
    if(volStep < 0 || volStep > 16)
    {
      Serial.println("[SetVolumeStep] Error: Wrong Volume Step (");
      Serial.print(volStep, DEC); 
      Serial.print(")");
      return;
    }
    else 
    {
        Serial.println("Set Volume to VolStep: ");
        Serial.print(volStep, DEC); 
        si4703_registers[SYSCONFIG2] &= 0xFFF0; //Clear volume bits
        si4703_registers[SYSCONFIG2] |= volStep; //Set new volume
        si4703_updateRegisters(); //Update
    }
}
// return volume step € [0,16] from voltage €+/- [0V, 5V]
int VoltageToVolumeStep( float voltage, byte *pVolStep)
{
    if(pVolStep == NULL)
    {
      Serial.println("[VoltageToVolumeStep] Null Pointer error");
      return -1;  
    }
    else
    {      
      *pVolStep = (int) (voltage / 5.0 * 16.0);  
      Serial.print("[VoltageToVolumeStep] voltage: computed volStep: ");
      Serial.print(voltage);
      Serial.print(*pVolStep, DEC); 
      return 0;
    }
}

// A0 -> voltage0 : unused
// A1 -> voltage1 : Volume
voltage voltage0; 
voltage voltage1; 

#define RESET_INPUT_PIN 7
void(* resetFunc) (void) = 0;

void setup() {                
  initVoltage(&voltage0);
  initVoltage(&voltage1);
  pinMode(13, OUTPUT);
  pinMode(A0, INPUT); //Optional trimpot for analog station control
  pinMode(RESET_INPUT_PIN, INPUT_PULLUP); // Entrée ON/OFF
  
  Serial.begin(57600);
  Serial.println("");
 
  si4703_init(); //Init the Si4703 - we need to toggle SDIO before Wire.begin takes over.
 
}
 
void loop() {

wait_state:
while( digitalRead(RESET_INPUT_PIN) == HIGH)
{
  //Serial.println("WAiting state");
  //delay(5); 
}
  
  char option;
  char vol = 15;
  //int currentChannel = 973; //Default the unit to a known good local radio station
  int currentChannel = FRANCE_INTER_FREQ; //FRANCE INTER  
  gotoChannel(currentChannel);
  // Analog inputs 
  int flag1= 0;
  int sensorValue1; 
  
  while(1) {
    Serial.println("");
    Serial.println("Si4703 Configuration");
 
    currentChannel = readChannel();
    sprintf(printBuffer, "Current station: %02d.%01dMHz", currentChannel / 10, currentChannel % 10);
    Serial.println(printBuffer);
 
#ifdef IN_EUROPE
    Serial.println("Configured for European Radio");
#endif
 
    Serial.println("1) Tune to France inter");
    Serial.println("2) Mute On/Off");
    Serial.println("3) Display status");
    Serial.println("4) Seek up");
    Serial.println("5) Seek down");
    Serial.println("6) Poll for RDS");
    Serial.println("r) Print registers");
    Serial.println("8) Turn GPIO1 High");
    Serial.println("9) Turn GPIO1 Low");
    Serial.println("A) <Freq>");
    Serial.println("v) Volume");
    Serial.println("w) Tune up");
    Serial.println("s) Tune down");
    Serial.println(": ");
 
    /*while (!Serial.available());
    option = Serial.read();*/

   char inData[10]; // Or whatever size you need
   byte index = 0;

  // Convert the analog reading (which goes from 0 - 1023) to a voltage (0 - 5V)(10bits)
  // 0xfffc = 0xff | 0b11111100 => filter less 2 significant bit (bad ass noise reduction)
  // 0xfff0 = 0xff | 0b11110000 => filter less 4 significant bit (bad ass noise reduction)
   int filter = 0xFFFC;
   while(!Serial.available() && !flag1)
  {
      sensorValue1 = analogReadAverage(A1);
      voltage1.val = sensorToVoltage(sensorValue1 & filter);        
      flag1 = hasChanged(&voltage1, FLOAT_PRECISION, NULL);
      
#ifdef DEBUG 
      Serial.print("flag0: ");
      Serial.print(flag0);
      printVoltages(voltage0.val, voltage1.val);
      delay(5000);
#endif
      updateVoltage(&voltage1); 
      // Check On/Off state
      if(digitalRead(RESET_INPUT_PIN) == HIGH)
      {
        Serial.println("Will close");
        delay(500);
        resetFunc();
        goto wait_state;
      }
      delay(DELAY_MEAS_MS);
      
  } // wait button changes or Serial cmd
  
   while(Serial.available()) // store cmd
   {
      char aChar = Serial.read();
      inData[index] = aChar; // Add the character to the array
      index++;   // Point to the next position
      inData[index] = '\0'; // NULL terminate the array
      Serial.println(printBuffer);
      option =  inData[0];
   }

  if (flag1)
   {
      Serial.print("[A1, VOL Changed]");      
      printVoltages(voltage0.val, voltage1.val);
      // Go in Volume config
      option = 'v';
      delay(500);
   }

//    }

    if(option == '1')  {
      Serial.print("Tune to ");
      Serial.print(FRANCE_INTER_FREQ);
      currentChannel = FRANCE_INTER_FREQ;
      gotoChannel(currentChannel);
    }
    else if(option == '2') {
      Serial.println("Mute toggle");
      si4703_readRegisters();
      si4703_registers[POWERCFG] ^= (1<<DMUTE); //Toggle Mute bit
      si4703_updateRegisters();
    }
    else if(option == '3') {
      si4703_readRegisters();
 
      Serial.println("");
      Serial.println("Radio Status:");
 
      if(si4703_registers[STATUSRSSI] & (1<<RDSR)){
        Serial.println(" (RDS Available)");
 
        byte blockerrors = (si4703_registers[STATUSRSSI] & 0x0600) >> 9; //Mask in BLERA
        if(blockerrors == 0) Serial.println (" (No RDS errors)");
        if(blockerrors == 1) Serial.println (" (1-2 RDS errors)");
        if(blockerrors == 2) Serial.println (" (3-5 RDS errors)");
        if(blockerrors == 3) Serial.println (" (6+ RDS errors)");
      }
      else
        Serial.println(" (No RDS)");
 
      if(si4703_registers[STATUSRSSI] & (1<<STC)) Serial.println(" (Tune Complete)");
      if(si4703_registers[STATUSRSSI] & (1<<SFBL)) 
        Serial.println(" (Seek Fail)");
      else
        Serial.println(" (Seek Successful!)");
      if(si4703_registers[STATUSRSSI] & (1<<AFCRL)) Serial.println(" (AFC/Invalid Channel)");
      if(si4703_registers[STATUSRSSI] & (1<<RDSS)) Serial.println(" (RDS Synch)");
 
      if(si4703_registers[STATUSRSSI] & (1<<STEREO)) 
        Serial.println(" (Stereo!)");
      else
        Serial.println(" (Mono)");
 
      byte rssi = si4703_registers[STATUSRSSI] & 0x00FF; //Mask in RSSI
      Serial.println(" (RSSI=");
      Serial.println(rssi, DEC);
      Serial.println(" of 75)");
    }
    else if(option == '4') {
      seek(SEEK_UP);
    }
    else if(option == '5') {
      seek(SEEK_DOWN);
    }
    else if(option == '6') {
      Serial.println("Poll RDS - x to exit");
      while(1) {
        if(Serial.available() > 0)
          if(Serial.read() == 'x') break;
 
        si4703_readRegisters();
        if(si4703_registers[STATUSRSSI] & (1<<RDSR)){
          Serial.println("We have RDS!");
          byte Ah, Al, Bh, Bl, Ch, Cl, Dh, Dl;
          Ah = (si4703_registers[RDSA] & 0xFF00) >> 8;
          Al = (si4703_registers[RDSA] & 0x00FF);
 
          Bh = (si4703_registers[RDSB] & 0xFF00) >> 8;
          Bl = (si4703_registers[RDSB] & 0x00FF);
 
          Ch = (si4703_registers[RDSC] & 0xFF00) >> 8;
          Cl = (si4703_registers[RDSC] & 0x00FF);
 
          Dh = (si4703_registers[RDSD] & 0xFF00) >> 8;
          Dl = (si4703_registers[RDSD] & 0x00FF);
 
          Serial.println("RDS: ");
          Serial.println(Ah);
          Serial.println(Al);
          Serial.println(Bh);
          Serial.println(Bl);
          Serial.println(Ch);
          Serial.println(Cl);
          Serial.println(Dh);
          Serial.println(Dl);
          Serial.println(" !");
 
          delay(40); //Wait for the RDS bit to clear
        }
        else {
          Serial.println("No RDS");
          delay(30); //From AN230, using the polling method 40ms should be sufficient amount of time between checks
        }
        //delay(500);
      }
    }
    else if(option == '8') {
      Serial.println("GPIO1 High");
      si4703_registers[SYSCONFIG1] |= (1<<1) | (1<<0);
      si4703_updateRegisters();
    }
    else if(option == '9') {
      Serial.println("GPIO1 Low");
      si4703_registers[SYSCONFIG1] &= ~(1<<0);
      si4703_updateRegisters();
    }
    else if(option == 'r') {
      Serial.println("Print registers");
      si4703_printRegisters();
    }
    else if(option == 't') {
      Serial.println("Trim pot tuning");
      while(1) {
        if(Serial.available())
          if(Serial.read() == 'x') break;
 
        int trimpot = 0;
        for(int x = 0 ; x < 16 ; x++)
          trimpot += analogRead(A0);
        trimpot /= 16; //Take average of trimpot reading
 
        Serial.println("Trim: ");
        Serial.println(trimpot);
        trimpot = map(trimpot, 0, 1023, 875, 1079); //Convert the trimpot value to a valid station
        Serial.println(" station: ");
        Serial.println(trimpot);
        if(trimpot != currentChannel) {
          currentChannel = trimpot;
          gotoChannel(currentChannel);
        }
 
        delay(100);
      }
    }
    else if(option == 'v') 
    {
      if (flag1)
      {
         // Volume change from A1 potentiometer
         byte volumeStep;
         int err = VoltageToVolumeStep(voltage1.val, &volumeStep);
         if (err == 0)
         {
           SetVolumeStep(volumeStep);
         }
       }
       else
       {

        // Volume change from v Serial command
        Serial.println("");
        Serial.println("Volume:");
        Serial.println("+) Up");
        Serial.println("-) Down");
        Serial.println("x) Exit");
        while(1){
          if(Serial.available()){
            option = Serial.read();
            if(option == '+') {
                IncreaseVolume();
            }
            if(option == '-') {
                DecreaseVolume();
            }
            else if(option == 'x') break;
          }
        }
      }
    }
    else if(option == 'w') {
      currentChannel = readChannel();
#ifdef IN_EUROPE
      currentChannel += 1; //Increase channel by 100kHz
#else
      currentChannel += 2; //Increase channel by 200kHz
#endif
      gotoChannel(currentChannel);
    }
    else if(option == 's') {
      currentChannel = readChannel();
#ifdef IN_EUROPE
      currentChannel -= 1; //Decreage channel by 100kHz
#else
      currentChannel -= 2; //Decrease channel by 200kHz
#endif
      gotoChannel(currentChannel);
    }
    else {
      Serial.println("Choice = ");
      Serial.println(option);
    }
    // re-init 
    option = '0';
    flag1 = 0;
  }
}
 
//Given a channel, tune to it
//Channel is in MHz, so 973 will tune to 97.3MHz
//Note: gotoChannel will go to illegal channels (ie, greater than 110MHz)
//It's left to the user to limit these if necessary
//Actually, during testing the Si4703 seems to be internally limiting it at 87.5. Neat.
void gotoChannel(int newChannel){
  //Freq(MHz) = 0.200(in USA) * Channel + 87.5MHz
  //97.3 = 0.2 * Chan + 87.5
  //9.8 / 0.2 = 49
  newChannel *= 10; //973 * 10 = 9730
  newChannel -= 8750; //9730 - 8750 = 980
 
#ifdef IN_EUROPE
    newChannel /= 10; //980 / 10 = 98
#else
  newChannel /= 20; //980 / 20 = 49
#endif
 
  //These steps come from AN230 page 20 rev 0.5
  si4703_readRegisters();
  si4703_registers[CHANNEL] &= 0xFE00; //Clear out the channel bits
  si4703_registers[CHANNEL] |= newChannel; //Mask in the new channel
  si4703_registers[CHANNEL] |= (1<<TUNE); //Set the TUNE bit to start
  si4703_updateRegisters();
 
  //delay(60); //Wait 60ms - you can use or skip this delay
 
  //Poll to see if STC is set
  while(1) {
    si4703_readRegisters();
    if( (si4703_registers[STATUSRSSI] & (1<<STC)) != 0) break; //Tuning complete!
    Serial.println("Tuning");
  }
 
  si4703_readRegisters();
  si4703_registers[CHANNEL] &= ~(1<<TUNE); //Clear the tune after a tune has completed
  si4703_updateRegisters();
 
  //Wait for the si4703 to clear the STC as well
  while(1) {
    si4703_readRegisters();
    if( (si4703_registers[STATUSRSSI] & (1<<STC)) == 0) break; //Tuning complete!
    Serial.println("Waiting...");
  }
}
 
//Reads the current channel from READCHAN
//Returns a number like 973 for 97.3MHz
int readChannel(void) {
  si4703_readRegisters();
  int channel = si4703_registers[READCHAN] & 0x03FF; //Mask out everything but the lower 10 bits
 
#ifdef IN_EUROPE
  //Freq(MHz) = 0.100(in Europe) * Channel + 87.5MHz
  //X = 0.1 * Chan + 87.5
  channel *= 1; //98 * 1 = 98 - I know this line is silly, but it makes the code look uniform
#else
  //Freq(MHz) = 0.200(in USA) * Channel + 87.5MHz
  //X = 0.2 * Chan + 87.5
  channel *= 2; //49 * 2 = 98
#endif
 
  channel += 875; //98 + 875 = 973
  return(channel);
}
 
//Seeks out the next available station
//Returns the freq if it made it
//Returns zero if failed
byte seek(byte seekDirection){
  si4703_readRegisters();
 
  //Set seek mode wrap bit
  //si4703_registers[POWERCFG] |= (1<<SKMODE); //Allow wrap
  si4703_registers[POWERCFG] &= ~(1<<SKMODE); //Disallow wrap - if you disallow wrap, you may want to tune to 87.5 first
 
  if(seekDirection == SEEK_DOWN) si4703_registers[POWERCFG] &= ~(1<<SEEKUP); //Seek down is the default upon reset
  else si4703_registers[POWERCFG] |= 1<<SEEKUP; //Set the bit to seek up
 
  si4703_registers[POWERCFG] |= (1<<SEEK); //Start seek
 
  si4703_updateRegisters(); //Seeking will now start
 
  //Poll to see if STC is set
  while(1) {
    si4703_readRegisters();
    if((si4703_registers[STATUSRSSI] & (1<<STC)) != 0) break; //Tuning complete!
 
    Serial.println("Trying station:");
    Serial.println(readChannel());
  }
 
  si4703_readRegisters();
  int valueSFBL = si4703_registers[STATUSRSSI] & (1<<SFBL); //Store the value of SFBL
  si4703_registers[POWERCFG] &= ~(1<<SEEK); //Clear the seek bit after seek has completed
  si4703_updateRegisters();
 
  //Wait for the si4703 to clear the STC as well
  while(1) {
    si4703_readRegisters();
    if( (si4703_registers[STATUSRSSI] & (1<<STC)) == 0) break; //Tuning complete!
    Serial.println("Waiting...");
  }
 
  if(valueSFBL) { //The bit was set indicating we hit a band limit or failed to find a station
    Serial.println("Seek limit hit"); //Hit limit of band during seek
    return(FAIL);
  }
 
  Serial.println("Seek complete"); //Tuning complete!
  return(SUCCESS);
}
 
//To get the Si4703 inito 2-wire mode, SEN needs to be high and SDIO needs to be low after a reset
//The breakout board has SEN pulled high, but also has SDIO pulled high. Therefore, after a normal power up
//The Si4703 will be in an unknown state. RST must be controlled
void si4703_init(void) {
  Serial.println("Initializing I2C and Si4703");
   
  pinMode(resetPin, OUTPUT);
  pinMode(SDIO, OUTPUT); //SDIO is connected to A4 for I2C
  digitalWrite(SDIO, LOW); //A low SDIO indicates a 2-wire interface
  digitalWrite(resetPin, LOW); //Put Si4703 into reset
  delay(1); //Some delays while we allow pins to settle
  digitalWrite(resetPin, HIGH); //Bring Si4703 out of reset with SDIO set to low and SEN pulled high with on-board resistor
  delay(1); //Allow Si4703 to come out of reset
 
  Wire.begin(); //Now that the unit is reset and I2C inteface mode, we need to begin I2C
 
  si4703_readRegisters(); //Read the current register set
  //si4703_registers[0x07] = 0xBC04; //Enable the oscillator, from AN230 page 9, rev 0.5 (DOES NOT WORK, wtf Silicon Labs datasheet?)
  si4703_registers[0x07] = 0x8100; //Enable the oscillator, from AN230 page 9, rev 0.61 (works)
  si4703_updateRegisters(); //Update
 
  delay(500); //Wait for clock to settle - from AN230 page 9
 
  si4703_readRegisters(); //Read the current register set
  si4703_registers[POWERCFG] = 0x4001; //Enable the IC
  //  si4703_registers[POWERCFG] |= (1<<SMUTE) | (1<<DMUTE); //Disable Mute, disable softmute
  si4703_registers[SYSCONFIG1] |= (1<<RDS); //Enable RDS
 
#ifdef IN_EUROPE
    si4703_registers[SYSCONFIG1] |= (1<<DE); //50kHz Europe setup
  si4703_registers[SYSCONFIG2] |= (1<<SPACE0); //100kHz channel spacing for Europe
#else
  si4703_registers[SYSCONFIG2] &= ~(1<<SPACE1 | 1<<SPACE0) ; //Force 200kHz channel spacing for USA
#endif
 
  si4703_registers[SYSCONFIG2] &= 0xFFF0; //Clear volume bits
  si4703_registers[SYSCONFIG2] |= 0x0001; //Set volume to lowest
  si4703_updateRegisters(); //Update
 
  delay(110); //Max powerup time, from datasheet page 13
}
 
//Write the current 9 control registers (0x02 to 0x07) to the Si4703
//It's a little weird, you don't write an I2C addres
//The Si4703 assumes you are writing to 0x02 first, then increments
byte si4703_updateRegisters(void) {
 
  Wire.beginTransmission(SI4703);
  //A write command automatically begins with register 0x02 so no need to send a write-to address
  //First we send the 0x02 to 0x07 control registers
  //In general, we should not write to registers 0x08 and 0x09
  for(int regSpot = 0x02 ; regSpot < 0x08 ; regSpot++) {
    byte high_byte = si4703_registers[regSpot] >> 8;
    byte low_byte = si4703_registers[regSpot] & 0x00FF;
 
    Wire.write(high_byte); //Upper 8 bits
    Wire.write(low_byte); //Lower 8 bits
  }
 
  //End this transmission
  byte ack = Wire.endTransmission();
  if(ack != 0) { //We have a problem! 
    Serial.println("Write Fail:"); //No ACK!
    Serial.println(ack, DEC); //I2C error: 0 = success, 1 = data too long, 2 = rx NACK on address, 3 = rx NACK on data, 4 = other error
    return(FAIL);
  }
 
  return(SUCCESS);
}
 
//Read the entire register control set from 0x00 to 0x0F
void si4703_readRegisters(void){
 
  //Si4703 begins reading from register upper register of 0x0A and reads to 0x0F, then loops to 0x00.
  Wire.requestFrom(SI4703, 32); //We want to read the entire register set from 0x0A to 0x09 = 32 bytes.
 
  while(Wire.available() < 32) ; //Wait for 16 words/32 bytes to come back from slave I2C device
  //We may want some time-out error here
 
  //Remember, register 0x0A comes in first so we have to shuffle the array around a bit
  for(int x = 0x0A ; ; x++) { //Read in these 32 bytes
    if(x == 0x10) x = 0; //Loop back to zero
    si4703_registers[x] = Wire.read() << 8;
    si4703_registers[x] |= Wire.read();
    if(x == 0x09) break; //We're done!
  }
}
 
void si4703_printRegisters(void) {
  //Read back the registers
  si4703_readRegisters();
 
  //Print the response array for debugging
  for(int x = 0 ; x < 16 ; x++) {
    sprintf(printBuffer, "Reg 0x%02X = 0x%04X", x, si4703_registers[x]);
    Serial.println(printBuffer);
  }
}
