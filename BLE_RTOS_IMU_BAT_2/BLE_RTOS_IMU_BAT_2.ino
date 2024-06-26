#include <bluefruit.h>
#include "FreeRTOS.h"
#include "RTClib.h"
#include "LSM6DS3.h"
#include "Wire.h"

//Device name
String deviceName = "IMU4";
unsigned long startTime = 0;

//************************ Signal ************************
// Base frequency for D8
const int baseFrequency = 30;  // One frame frequency 1024/8=>8Hz 1024/16=>16Hz   
//Create a instance of class LSM6DS3
LSM6DS3 myIMU(I2C_MODE, 0x6A);    //I2C device address 0x6A
// IMU variables
unsigned long miliBuffer[5];
int sensorBuffer[30];
int bufferIndex = 0;
bool bufferOverflow = false;
SemaphoreHandle_t bufferSemaphore;

//************************ Battery ************************
// Define battery
#define VBAT_DIVIDER      (0.332888F)   // 1M + 0.499M voltage divider on VBAT
#define VBAT_DIVIDER_COMP (3.004008F)        // Compensation factor for the VBAT divider
const int batterySampleNum = 8;
int batteryValues[batterySampleNum] = {0}; // buffer to store the last 10 battery readings
int percentage; // average battery buffer
int currentSampleIndex = 0; // index to keep track of the current sample
float mv_per_lsb = 3600.0F/1024.0F; // 10-bit ADC with 3.6V input range


typedef struct {
    float voltage;
    int percentage;
} BatteryState;

BatteryState battery_states[] = {
    {4.16, 100}, {4.15, 99}, {4.14, 98}, {4.13, 97}, {4.12, 96}, {4.11, 95}, {4.10, 94}, {4.09, 92}, 
    {4.08, 91}, {4.07, 90}, {4.06, 89}, {4.05, 88}, {4.04, 87}, {4.03, 86}, {4.02, 85}, {4.01, 84}, 
    {4.00, 83}, {3.99, 82}, {3.98, 81}, {3.97, 80}, {3.96, 79}, {3.95, 78}, {3.94, 77}, {3.93, 76}, 
    {3.92, 75}, {3.91, 74}, {3.9, 73}, {3.89, 72}, {3.88, 71}, {3.87, 70}, {3.86, 69}, {3.85, 68}, 
    {3.84, 67}, {3.83, 66}, {3.82, 65}, {3.81, 64}, {3.8, 63}, {3.79, 62}, {3.78, 61}, {3.77, 60}, 
    {3.76, 59}, {3.75, 58}, {3.74, 57}, {3.73, 56}, {3.72, 55}, {3.71, 54}, {3.7, 53}, {3.69, 52}, 
    {3.68, 51}, {3.67, 50}, {3.66, 49}, {3.65, 48}, {3.64, 47}, {3.63, 46}, {3.62, 45}, {3.61, 44}, 
    {3.6, 43}, {3.59, 42}, {3.58, 41}, {3.57, 40}, {3.56, 39}, {3.55, 38}, {3.54, 37}, {3.53, 36}, 
    {3.52, 35}, {3.51, 34}, {3.5, 33}, {3.49, 32}, {3.48, 31}, {3.47, 30}, {3.46, 29}, {3.45, 28}, 
    {3.44, 27}, {3.43, 26}, {3.42, 25}, {3.41, 24}, {3.4, 23}, {3.39, 22}, {3.38, 21}, {3.37, 20}, 
    {3.36, 19}, {3.35, 18}, {3.34, 17}, {3.33, 16}, {3.32, 15}, {3.31, 14}, {3.3, 13}, {3.29, 12}, 
    {3.28, 11}, {3.27, 10}, {3.26, 9}, {3.25, 8}, {3.24, 7}, {3.23, 6}, {3.22, 5}, {3.21, 4}, 
    {3.19, 3}, {3.17, 2}, {3.15, 1}, {0.00, 0}
}; 

int getBatteryPercentage(float voltage) {
  for (int i = 0; i < sizeof(battery_states)/sizeof(BatteryState) - 1; i++) {
    if (voltage >= battery_states[i].voltage) {
      return battery_states[i].percentage;
    }
  }
  return 0; // Return 0% if the voltage is below the lowest defined voltage
}

//************************ RTC ************************
RTC_Millis rtc;
// Initial date and time values
volatile int year = 2023;
volatile int month = 9;
volatile int day = 30;
volatile int hour = 15;
volatile int minute = 0;
volatile int second = 0;
// Update date and time every second
TickType_t Second_Time_Delay = 1024; 

//************************ BLE Service ************************
BLEDfu  bledfu;  // OTA DFU service
BLEDis  bledis;  // device information
BLEUart bleuart; // uart over ble
BLEBas  blebas;  // battery
char central_name_global[32] = { 0 };
String receivedString;         // Variable to store the received string
String lastProcessedString;    // Variable to store the last processed string
volatile bool bleConnected = false;

// This function updates the software-based clock every second
void updateClock() 
{
    DateTime now = rtc.now();
    year = now.year();
    month = now.month();
    day = now.day();
    hour = now.hour();
    minute = now.minute();
    second = now.second();

}

// Define a task function for the IMU reading
void SensorTask(void *pvParameters) {
  (void) pvParameters;

  for (;;) { // A Task shall never return or exit.
      if (bleConnected){
    bufferOverflow = false; // Set overflow flag
    // Read accelerometer data
    miliBuffer[bufferIndex] = millis() - startTime;
    sensorBuffer[(bufferIndex * 6)] = myIMU.readRawAccelX();
    sensorBuffer[(bufferIndex * 6) + 1] = myIMU.readRawAccelY();
    sensorBuffer[(bufferIndex * 6) + 2] = myIMU.readRawAccelZ();

    // Read gyroscope data
    sensorBuffer[(bufferIndex * 6) + 3] = myIMU.readRawGyroX();
    sensorBuffer[(bufferIndex * 6) + 4] = myIMU.readRawGyroY();
    sensorBuffer[(bufferIndex * 6) + 5] = myIMU.readRawGyroZ();

    bufferIndex++;
    if (bufferIndex >= 5) {
      bufferIndex = 0;
      bufferOverflow = true; // Set overflow flag
      }
    }
    vTaskDelay(pdMS_TO_TICKS(baseFrequency)); // Delay for a period of time
  }
}


// Task for sampling the battery
void TaskSampleBattery(void *pvParameters) {
  (void) pvParameters;

  for (;;) {
        if (bleConnected){
    // Sample the ADC value
    batteryValues[currentSampleIndex] = analogRead(PIN_VBAT);
    // Move to the next index, wrapping around if necessary
    currentSampleIndex = (currentSampleIndex + 1) % batterySampleNum;
    // Delay for next sample based on the defined sample rate
        }
    vTaskDelay(pdMS_TO_TICKS(baseFrequency * 1000));
  }
}

// Task for displaying the battery information
void TaskDisplayBattery(void *pvParameters) {
  (void) pvParameters;

  for (;;) {
    // Calculate the average ADC value
    int sum = 0;
    for(int i = 0; i < batterySampleNum; i++) {
      sum += batteryValues[i];
    }
    int avgBatValues = sum / batterySampleNum;

    // Calculate the battery value based on the average ADC value
    float batteryvalue = avgBatValues * mv_per_lsb * VBAT_DIVIDER_COMP / 1000;
    percentage = getBatteryPercentage(batteryvalue);
    
    // Delay until it's time to display again
    vTaskDelay(pdMS_TO_TICKS(baseFrequency * 8000));
  }
} 


// The RTC thread
void TaskDateTime(void *pvParameters) {
    (void) pvParameters; // Silence unused parameter warning

    while (true) 
    {
      updateClock();
      // Delay for 1 second to match our software clock update.
      vTaskDelay(Second_Time_Delay);
    }
}

// This task checks for buffer overflow and prints the buffer if overflow occurs forward data from HW Serial to BLEUART
void ble_uart_task(void *pvParameters)
{
    (void) pvParameters; // Just to avoid compiler warnings

  for (;;) {
        if (bleConnected){
    // Wait for the overflow flag to be set
    if (bufferOverflow) {
      // Take the semaphore to ensure no conflict on buffer access
      if (xSemaphoreTake(bufferSemaphore, (TickType_t)10) == pdTRUE) {
        uint8_t buf[1000] = {0};

        String timeString = deviceName;
        timeString += ",";
        //timeString += String(percentage);
        //timeString += "%,";
        //timeString += String(myIMU.readTempC());
        //timeString += "^,";
        // timeString += String(year);
        // timeString += "/";
        // timeString += String(month);
        // timeString += "/";
        // timeString += String(day);
        // timeString += ",";
        timeString += String(hour);
        timeString += ":";
        timeString += String(minute);
        timeString += ":";
        timeString += String(second);

        for (int i = 0; i < 5; i++) {
            timeString += ",";
            timeString += String(miliBuffer[i]);
          }

        for (int i = 0; i < 30; i++) {
          timeString += ",";
          timeString += String(sensorBuffer[i]);
        }

        int count1 = timeString.length();
        //Serial.println(count1);
        
        for(int i = 0; i < count1; i++){
          buf[i] = timeString[i];
          //Serial.print((char)buf[i]);
        }

        bleuart.write(buf, count1);
      
        // Reset the overflow flag
        bufferOverflow = false;
        
        // Give the semaphore back
        xSemaphoreGive(bufferSemaphore);
      }
    }
        }
    
    // Slight delay to prevent this task from hogging the CPU
    vTaskDelay(pdMS_TO_TICKS(baseFrequency));
  }
}

void ble_receive_task(void *pvParameters)
{
  while(true) 
  {
    // Check if there's data available
    if (bleuart.available()) 
    {
      receivedString = bleuart.readString();
      // TODO: Handle or process the receivedString if needed
    }
    vTaskDelay(pdMS_TO_TICKS(10)); // Short delay to prevent busy-waiting
  }
} 

void processReceivedStringTask(void *pvParameters) {
    while(1) {
        if (receivedString != lastProcessedString) {
            if (isInCorrectFormat(receivedString)) {
                int year   = receivedString.substring(0, 4).toInt();
                int month  = receivedString.substring(5, 7).toInt();
                int day    = receivedString.substring(8, 10).toInt();
                int hour   = receivedString.substring(11, 13).toInt();
                int minute = receivedString.substring(14, 16).toInt();
                int second = receivedString.substring(17, 19).toInt();

                DateTime newTime(year, month, day, hour, minute, second);
                rtc.adjust(newTime);
                startTime = millis();
            }
            
            lastProcessedString = receivedString;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void setup() {
  // Initialize digital pins as outputs
  pinMode(VBAT_ENABLE, OUTPUT);

  // Initialize pin phases
  digitalWrite(VBAT_ENABLE, LOW);
  
  //Configure IMU
  myIMU.begin();

  // initialize BLE
  setupBLE();

  delay(1000);

  // initialize RTC
  rtc.begin(DateTime(F(__DATE__), F(__TIME__)));
  // This line sets the RTC with an explicit date & time, for example to set
  rtc.adjust(DateTime(year, month, day, hour, minute, second));

  // Initialize the semaphore
  bufferSemaphore = xSemaphoreCreateMutex();

  // Create the IMU reading task
  xTaskCreate(SensorTask,    "Sensor Read", 1000,  NULL, 6, NULL);
  // Create the BLE send task
  xTaskCreate(ble_uart_task, "BLE UART Task", 1000, NULL, 7, NULL);
  // Create RTC task
  xTaskCreate(TaskDateTime, "RTC Task", 256, NULL, 4, NULL); 
  // Create battery voltage tasks
  //xTaskCreate(TaskSampleBattery, "SampleBattery", 100, NULL, 3, NULL);
  //xTaskCreate(TaskDisplayBattery, "DisplayBattery", 256, NULL, 2, NULL);
  // Create BLE receive tasks
  xTaskCreate(ble_receive_task, "BLE RE Task", 1000, NULL, 5, NULL);
  xTaskCreate(processReceivedStringTask, "Process Received String Task", 256, NULL, 1, NULL);
}


void loop() {
  // Empty. Things are managed by tasks.
}


// Start Advertising Setting
void startAdv(void)
{
  // Advertising packet
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();

  // Include bleuart 128-bit uuid
  Bluefruit.Advertising.addService(bleuart);

  // Secondary Scan Response packet (optional)
  // Since there is no room for 'Name' in Advertising packet
  Bluefruit.ScanResponse.addName();
  
  /* Start Advertising
   * - Enable auto advertising if disconnected
   * - Interval:  fast mode = 20 ms, slow mode = 152.5 ms
   * - Timeout for fast mode is 30 seconds
   * - Start(timeout) with timeout = 0 will advertise forever (until connected)
   * For recommended advertising interval
   * https://developer.apple.com/library/content/qa/qa1931/_index.html   
   */
  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(32, 244);    // in unit of 0.625 ms
  Bluefruit.Advertising.setFastTimeout(30);      // number of seconds in fast mode
  Bluefruit.Advertising.start(0);                // 0 = Don't stop advertising after n seconds  
}

// Setup the BLE LED to be enabled on CONNECT
void setupBLE(void) 
{
  // Setup the BLE LED to be enabled on CONNECT
  // Note: This is actually the default behavior, but provided
  // here in case you want to control this LED manually via PIN 19
  Bluefruit.autoConnLed(true);

  // Config the peripheral connection with maximum bandwidth 
  // more SRAM required by SoftDevice
  // Note: All config***() function must be called before begin()
  Bluefruit.configPrphBandwidth(BANDWIDTH_MAX);

  Bluefruit.begin();
  Bluefruit.setTxPower(4);    // Check bluefruit.h for supported values
  Bluefruit.setName(deviceName.c_str()); // useful testing with multiple central connections getMcuUniqueID()
  Bluefruit.Periph.setConnectCallback(connect_callback);
  Bluefruit.Periph.setDisconnectCallback(disconnect_callback);

  // To be consistent OTA DFU should be added first if it exists
  bledfu.begin();

  // Configure and Start Device Information Service
  bledis.setManufacturer("University of Queensland");
  bledis.setModel("Bluefruit Feather52");
  bledis.begin();

  // Configure and Start BLE Uart Service
  bleuart.begin();

  // Start BLE Battery Service
  blebas.begin();
  blebas.write(100);

  // Set up and start advertising
  startAdv();
}

// callback invoked when central connects
void connect_callback(uint16_t conn_handle)
{
  // Get the reference to current connection
  BLEConnection* connection = Bluefruit.Connection(conn_handle);

  char central_name[32] = { 0 };
  connection->getPeerName(central_name, sizeof(central_name));

  Serial.print("Connected to ");
  Serial.println(central_name);

  strncpy(central_name_global, central_name, 32);
  bleConnected = true;
}

/**
 * Callback invoked when a connection is dropped
 * @param conn_handle connection where this event happens
 * @param reason is a BLE_HCI_STATUS_CODE which can be found in ble_hci.h
 */
void disconnect_callback(uint16_t conn_handle, uint8_t reason)
{
  (void) conn_handle;
  (void) reason;
  bleConnected = false;

/*   Serial.println();
  Serial.print("Disconnected from ");
  Serial.print(central_name_global);
  Serial.print(", reason = 0x");
  Serial.println(reason, HEX); */
}

bool isInCorrectFormat(const String &str) {
    // Simple check for the format "YYYY/MM/DD HH:MM:SS"
    if (str.length() != 19) return false;
    if (str.charAt(4) != '/' || str.charAt(7) != '/' || 
        str.charAt(10) != ' ' || str.charAt(13) != ':' || str.charAt(16) != ':') return false;

    // Additional checks like valid month, day, hour, etc., can be added if needed.

    return true;
}