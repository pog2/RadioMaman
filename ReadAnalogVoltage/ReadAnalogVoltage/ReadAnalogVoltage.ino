/*
  ReadAnalogVoltage

  Reads an analog input on pin 0, converts it to voltage, and prints the result to the Serial Monitor.
  Graphical representation is available using Serial Plotter (Tools > Serial Plotter menu).
  Attach the center pin of a potentiometer to pin A0, and the outside pins to +5V and ground.

  This example code is in the public domain.

  https://www.arduino.cc/en/Tutorial/BuiltInExamples/ReadAnalogVoltage
*/

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

int hasChanged(void *pData, float precision)
{
  // Return true if voltage has changed 
  int flag;
  float diff;
  voltage *pVolt0 = (voltage *)pData;
  diff = pVolt0->val - pVolt0->lastVal;
  diff = (diff>0) ? diff : -diff; // abs(diff); 
  #ifdef DEBUG 
  Serial.print("[hasChanged] diff="])
  Serial.print(diff);
  
  #endif 
  flag = ( diff > precision ) ? 1 : 0;
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

voltage voltage0; 
voltage voltage1; 
  
// the setup routine runs once when you press reset:
void setup() {
  // initialize serial communication at 9600 bits per second:
  Serial.begin(9600);
  initVoltage(&voltage0);
  initVoltage(&voltage1);
}

// the loop routine runs over and over again forever:
void loop() {
  // read the input on analog pin 0:
  int flag0, flag1;
  int sensorValue0 = analogReadAverage(A0);
  int sensorValue1 = analogReadAverage(A1);

  // Convert the analog reading (which goes from 0 - 1023) to a voltage (0 - 5V)(10bits)
  // 0xfffc = 0xff | 0b11111100 => filter less 2 significant bit (bad ass noise reduction)
  // 0xfff0 = 0xff | 0b11110000 => filter less 4 significant bit (bad ass noise reduction)
  int filter = 0xFFFC;
  voltage0.val = sensorToVoltage(sensorValue0 & filter); 
  voltage1.val = sensorToVoltage(sensorValue1 & filter);
 
  flag0 = hasChanged(&voltage0, FLOAT_PRECISION);
  flag1 = hasChanged(&voltage1, FLOAT_PRECISION);
  
  // Print out the value you read:
  if(flag0)
  {
    Serial.print("[A0 CHANGED]");
    printVoltages(voltage0.val, voltage1.val);
  }
  if(flag1)
  {
    Serial.print("[A1 CHANGED]");
    printVoltages(voltage0.val, voltage1.val);
  }

  // Update voltage
  updateVoltage(&voltage0);
  updateVoltage(&voltage1);
  delay(DELAY_MEAS_MS);
}
