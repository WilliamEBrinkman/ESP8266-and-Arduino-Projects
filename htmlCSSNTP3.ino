/***********************************************************************
* William Brinkman  2016 October
*
* This code is designed to use a ESP8266 and create an arduino IDE compatible
*  *.ino to make a web server to display the time,
*  an alarm, door status, car status, and 
*  a toggle to open/close the door.
* 
*  BOM  -$10.50 USD at time of initial build Oct - 2016
*  1 - Esp8266 - Wemos D1R2 https://www.wemos.cc/product/d1.html                  $7.50
*  1 - HC-SR04 Ultrasonic Ranging Sensor Ultrasonic Module For Arduino            $1.50 
*  1 - 5V 1 Channel Relay Module Board For Arduino PIC AVR DSP ARM                $1.50
*  0 - assorted wires and basic tools for arduino projects  
*  
* Change the below variables to your preference - descriptions for each in the code
* WiFi network -  ssid, password, ip, gateway, subnet
* Time Zone    -  UTC_DST, UTC_STD,TimeChangeRule usCTDST, local_alarm hours & minutes
* Door variables - door_up, door_down
*
*  Required connections on the Wemos  
*    GPIO14 D5 - +5 v signal wire to the Relay
*    GPIO4  D2 - Trigger for ultrasonic sensor
*    GPIO5  D1 - Echo for ultrasonic sensor
*    5v - to the Vcc of the ultrasonic sensor and Vcc of the Relay
*         (best practice would have a seperate power supply to these but for $10.50 - REALLY???)
*    GND - to both the ultrasonic sensor and the Relay
*
*  index.htm file needs to be inserted into Flash memory
*      https://github.com/esp8266/Arduino/blob/master/doc/filesystem.md#uploading-files-to-file-system
*
*
*   Initial Release  2016-10-01
********************************************/


#include <FS.h>            // library used to load files into the RAM memory of the ESP (DOES NOT TURN RED)  part of core
#include <ESP8266WiFi.h>   //https://github.com/esp8266/Arduino
#include <WiFiUdp.h>       //https://github.com/esp8266/Arduino
#include <TimeLib.h>       //https://github.com/PaulStoffregen/Time
#include <Timezone.h>      //https://github.com/JChristensen/Timezone
#include <TimeAlarms.h>    //https://github.com/PaulStoffregen/TimeAlarms
#include <NewPing.h>       //http://playground.arduino.cc/Code/NewPing

// router settings
const char* ssid = "XXXXXXXXX";                 // router network
const char* password = "YYYYYYYYYY";          // router password

IPAddress ip(192, 168, 0, 100);               // ip address to call up the wemos 
IPAddress gateway(192, 168, 0, 1);            // router address 
IPAddress subnet(255, 255, 255, 0);   

//  Esp8266 pin setup 
//         - relay
# define LED_PIN  14  // Esp8266 pin tied to trigger the relay.    GPIO14 D5
//         - sonar echo settings
#define TRIGGER_PIN  4  // Arduino pin tied to trigger pin on the ultrasonic sensor. GPIO4  D2
#define ECHO_PIN     5  // Arduino pin tied to echo pin on the ultrasonic sensor.    GPIO5  D1
#define MAX_DISTANCE 450 // Maximum distance (177in) we want to range set . Maximum sensor distance is rated at 400-500cm.

NewPing sonar(TRIGGER_PIN, ECHO_PIN, MAX_DISTANCE); // NewPing setup of pins and maximum distance.


// NTP Servers and time update
static const char ntpServerName[] = "us.pool.ntp.org";
//static const char ntpServerName[] = "time.nist.gov";
//static const char ntpServerName[] = "time-a.timefreq.bldrdoc.gov";
//static const char ntpServerName[] = "time-b.timefreq.bldrdoc.gov";
//static const char ntpServerName[] = "time-c.timefreq.bldrdoc.gov";

WiFiUDP Udp;
unsigned int localPort = 8888;  // local port to listen for UDP packets

//   Time sync and display on serial
int sync_int = 3600;     // sync interval (every hour) for the next call to the ntp server.  If no response, sets to a lower value
int good_sync_flag = 0;  // flag to say the machine got a correct time response from the NTP server and determine the alarm times
time_t getNtpTime();
time_t prevDisplay = 0; // when the digital clock was displayed

void digitalClockDisplay();
void alarmset();
void printDigits(int digits);
void sendNTPpacket(IPAddress &address);

//   Time Alarms
//US Central Time Zone (Dallas, Chicago)       Change this to your particular time zone

int UTC_DST = -5;
int UTC_STD = -6;
TimeChangeRule usCTDST = {"CDT", Second, Sun, Mar, 2, -300};    //Daylight time = UTC - 5 hours *60=-300 occurs 2 am 
TimeChangeRule usCTSTD = {"CST", First, Sun, Nov, 2, -360};     //Standard time = UTC - 6 hours
Timezone usCT(usCTDST, usCTSTD);
int local_alarm_hour = 21;       // becomes the UTC time of the local alarm hour1  9pm at night local
int local_alarm_minute = 30;   // in case you want the door down not at an even hour
int local_alarm_second = 0;
int hh_hold =0;                // hour holder depending on DST or STD time
int Time_flag =0;  // flag holder for the current time  0 = STD  1 = DST
TimeChangeRule *tcr;        //pointer to the time change rule, use to get TZ abbrev
time_t utc, local, alarmt1;  // utc is obvious, local is the local time, alarmt1 is the times set up for the alarm. 

// index.htm file send 
int cnt = 0;
int BUF_SIZE = 1000;
WiFiServer server(80);

String HTTP_req;
boolean LED_status = false;

File webFile;
//Method - Function Declarations
void Process_HTTP_Req(WiFiClient);
void SendXML(WiFiClient);
void SendFile(WiFiClient,String);
String ContentType(String);
void FileStatus();

// functions to write the XML string

char bigString[350];    // needs to trim down to reasonable
char tempString[20];

int car_status =0;    // 0=in   1 =out  2 = undetermined
int door_status =0;   // 0=down  1=up   2 = moving
int door_moving =0;   // 1 means moving from up to down or down to up
int door_timer = 0;
int door_up = 17;   // constant ping distance to see if the door is up
int door_down = 72; // constant ping distance to see if the door is down  - dist to floor 

void addDigits(char *s, int l);         // adds a leading zero to time hours or minutes if needed
void append(char* s, char c);          // move a single char into a string such as a newline or NULL


void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  delay(10);
  pinMode(LED_PIN,OUTPUT);
  digitalWrite(LED_PIN, HIGH);

  //Connect to wifi network
  Serial.println();
  Serial.println();
  Serial.println("Connecting to: ");
  Serial.println(ssid);
  WiFi.config(ip,gateway,subnet);   // to use a static ip address 
  WiFi.begin(ssid,password);

  
  while(WiFi.status() != WL_CONNECTED){
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.println("Wifi connected");

  //Start Web Server
  server.begin();
  Serial.println("Server Started");

  //Print Web Server IP Address
  Serial.print("Use this URL to connect: ");
  Serial.println("http://" + WiFi.localIP().toString() + "/");

  //Mount File System
  SPIFFS.begin();
  if (!SPIFFS.exists("/index.htm")){
    Serial.println("ERROR - Can't find index.htm file!");
    return;
  }
  Serial.println("SUCCESS - Found index.htm file!");
  
  delay(250);
  Serial.println("TimeNTP Section");
  Serial.println("Starting UDP");
  Udp.begin(localPort);
  Serial.println("waiting for sync");
  setSyncProvider(getNtpTime);     // set the external time provider   this one calls the Get NTP 
  
}  // end setup 

void loop() {
  // put your main code here, to run repeatedly:

if (timeStatus() != timeNotSet) // one more second has passed
  {
     if(good_sync_flag == 0)   // does this once a good NTP time sync has occurred and never again
     {
      Serial.println("Off to alarmset");
      alarmset();
     }
    Alarm.delay(10);  // check to see if it is time to alarm every 10 milliseconds
   }  // end time status

  //Check for Web Client
  WiFiClient client = server.available();
  if (!client){
    return;
  }
  Serial.println("Client Connected");
   delay(1);
  while(!client.available()){
    delay(1);
  }
  HTTP_req = client.readStringUntil(0x13);
  client.flush();

  Serial.println("HTTP_req: " + HTTP_req);
  Process_HTTP_Req(client);
  
} // end loop

/*****************************************  functions ********************************************/

void Process_HTTP_Req(WiFiClient client){
  if (HTTP_req.indexOf("GET / HTTP/1.1")!= -1){
    //Send the initial webpage to the client
    SendFile(client,"/index.htm");
    BuildXML();   // get the xml built so the request for it will be near instantanous send
  }
  
  else if(HTTP_req.indexOf("LED1=1") != -1){
    //Toggle the Relay 
   if(door_timer == 0)  //set this flag once then wait for door completion to toggle to reset to zero
   {
    digitalWrite(LED_PIN,LOW);
    LED_status = true;
    door_moving = 1;
    door_timer = 6;   // calls every 2 seconds - has a 12 second door moving status
    delay(350);
    digitalWrite(LED_PIN,HIGH);
   }
    
//    LED_status = false;  // set back to false after the door timer 
//    door_moving = 0;
 }
  
  else if(HTTP_req.indexOf("ajax_input") != -1){
    //Send XML file to update webpage widgets
    SendXML(client);
  }
}

void BuildXML(){
  
  int holder;            // holds int values until converts to string
  int ping_dist = 0;    // place holder for ping distance
   
  char o = '\0';          // for use with append function
  char n = '\n';
  char r = '\r';

  // bigString[350], tempString defined above the setup function as global
  strcpy(bigString,"0");   // reset it to having nothing in it but the number zero
  bigString[0] = '\0';                         // set the null string first
  strcat(bigString,"HTTP/1.1 200 OK");                      // javascript needs the 200 as part of the XML return
  append(bigString,n);
  strcat(bigString,"Content-Type: text/xml");               // in case it can't figure out this is xml
  append(bigString,n);
  strcat(bigString,"Connection: keep-alive");               // in case it can't figure out this is xml
  append(bigString,n);
  append(bigString,n);                                      // one more newline just because  
  strcat(bigString,"<?xml version = '1.0' ?>");  // double aspos since a string
  append(bigString,n);                           // adds a newline at the end
  strcat(bigString,"<inputs>");
  append(bigString,n);
  
    utc = now();                  // get the current UTC time (now()) into time variable utc
    local = usCT.toLocal(utc);    // knowing time zone offsets, calculates the local time 
    
// local time   
  strcat(bigString,"<time_loc>");
    holder = hour(local);
    addDigits(bigString,holder);
  strcat(bigString,"</time_loc>");
    append(bigString,n); 

  strcat(bigString,"<time_loc>");
    holder = minute(local);
    addDigits(bigString,holder);
  strcat(bigString,"</time_loc>");
  append(bigString,n); 
  
  strcat(bigString,"<time_loc>");
    holder = second(local);
    addDigits(bigString,holder);
  strcat(bigString,"</time_loc>");
  append(bigString,n); 

//  alarm     local_alarm_hour,local_alarm_minute,0
strcat(bigString,"<time_ala>");
    holder = local_alarm_hour;
    addDigits(bigString,holder);
  strcat(bigString,"</time_ala>");
  append(bigString,n); 
  
strcat(bigString,"<time_ala>");
    holder = local_alarm_minute;
    addDigits(bigString,holder);
  strcat(bigString,"</time_ala>");
  append(bigString,n); 

strcat(bigString,"<time_ala>");
  strcat(bigString,"00");   // seconds forced to zero for alarms
  strcat(bigString,"</time_ala>");
  append(bigString,n); 

// ping

  ping_dist = sonar.ping_in();

 strcat(bigString,"<ping>");
   itoa(ping_dist,tempString,10);  // integer, buffer, base
   strcat(bigString,tempString);
 strcat(bigString,"</ping>");
  append(bigString,n); 

  ping_car_door_status(ping_dist, door_status, car_status, door_timer, LED_status, door_moving);  // ping dist to detrmine car and various status
     
//  door

  strcat(bigString,"<door>");
     if (door_status == 0)
        {
        strcat(bigString,"DOWN");
        }
     if (door_status == 1)
        {
        strcat(bigString,"UP");
        }
      if (door_status == 2)
        {
        strcat(bigString,"MOVING");
        }
   strcat(bigString,"</door>");
   append(bigString,n); 
 

//  car
   strcat(bigString,"<car>");
     if (car_status == 0)
        {
        strcat(bigString,"IN");
        }
     if (car_status == 1)
        {
        strcat(bigString,"OUT");
        }
      if (car_status == 2)
        {
        strcat(bigString,"UNKNOWN");
        }
   strcat(bigString,"</car>");
   append(bigString,n); 

  // LED Status  in function AddXML - above can be 4 seconds out of phase,  LED status needs 2 seconds out to keep checkbox marked
}  // end BuildXML

void ping_car_door_status(int ping_d, int &d_s, int &c_s, int &d_t, boolean &l_s, int &d_m){
                            // door_status, car_status, door_timer, LED_status, door_moving
if (d_t > 0)
  {
    d_t = d_t -1;
    l_s = true;      // want the box checked until movement stopped - redundant
  }else
  {
    d_m = 0;  // resets door_moving to zero
    l_s = false;
  }
   
 if(ping_d < door_up)
     {
       d_s = 1; // down =0   UP = 1
       c_s = 2;  // cant tell if in or out - door in the way
     } else 
     {
       d_s = 0;  // down = 0 , up = 1
       if (ping_d > door_down)  // hitting the garage floor
          {
           c_s = 1;         // assume car is in = 0 , or out = 1
          }else
          {
            c_s = 0;     // assume car is in = 0, or out = 1
          }
     }
  if(d_m == 1)
   {
     d_s = 2;    // means the door is moving
   }
}   //end ping_car_door_status

void AddXML(){
    char o = '\0';          // for use with append function
    char n = '\n';
    char r = '\r';
  
  strcat(bigString,"<LED>");
     if (LED_status){
        strcat(bigString,"on");
        }
        else{
        strcat(bigString,"off");
        }
  strcat(bigString,"</LED>");
  append(bigString,n); 
  strcat(bigString,"</inputs>");
  append(bigString,n); 
}

void SendXML(WiFiClient client){  
  // Big string built - now send after adding in last of LED section
  AddXML();    // add the LED part last - others will be 4 seconds late
  client.println(bigString);
  Serial.println("msg to client sent!");
  BuildXML();  // rebuild the majority of the xml statement
}

/////////////////////////////////////////////////////end xml spot


void SendFile(WiFiClient client,String fileName){
  Serial.print("Sending file: ");
  Serial.println(fileName);
  client.println("HTTP/1.1 200 OK");
  client.println(ContentType(fileName));
  client.println("Connection: close");
  client.println();
  webFile = SPIFFS.open(fileName,"r");
  if (webFile){
     // while(webFile.available()){
     //   client.write(webFile.read());
     //   FileStatus();
      //}
      Serial.print("File size: ");
      Serial.println(webFile.size());
      byte buf[BUF_SIZE];
      
      int i = 0;
      while(webFile.available()){
        buf[i] = webFile.read();
        if (i == (BUF_SIZE -1 )){
          client.write(buf,i +1);
          Serial.print("Sending: ");
          Serial.print(i);
          Serial.println(" bytes");
          for (int ii = 0; ii <= BUF_SIZE; ii++){
            buf[ii] = 0;
          }
          i = 0;
        }
        else{
          i++;          
        }
      }
      client.write(buf,i +1);
      Serial.print("Sending: ");
      Serial.print(i);
      Serial.println(" bytes");
      webFile.close();
      Serial.println();
    }
  Serial.print("File sent: ");
  Serial.println(fileName);   
}
String ContentType(String fileName){
  String contentType = "Content-Type: ";
  if (fileName.indexOf("htm") != -1){
   contentType += "text/html";
  }
  else if (fileName.indexOf("png") != -1){
    contentType += "image/png";
  }
  Serial.println(contentType);
  return contentType;
}
void FileStatus(){
  if (cnt % 100 == 0){
    Serial.print(".");
  }
  cnt += 1;
}

void digitalClockDisplay()
{
  // digital clock display of the time
  Serial.print(hour());
  printDigits(minute());   // sends these through the clean up for leading zeros
  printDigits(second());
  Serial.print(" ");
  Serial.print(day());
  Serial.print(".");
  Serial.print(month());
  Serial.print(".");
  Serial.print(year());
  Serial.println();
}

void printDigits(int digits)
{
  // utility for digital clock display: prints preceding colon and leading 0
  Serial.print(":");
  if (digits < 10)
    Serial.print('0');
  Serial.print(digits);
}  // end printDigits


void addDigits( char* s, int l)    // bigString adds leading zeros to time integers
{
 
  char tempString1[5];        // strings should have "05" or other two digit integer
  char tempString2[5];

  tempString1[0] = '\0';    // reset the tempString - will get a leading 0 if needed  
  tempString2[0] = '\0';    // reset the tempString  - will have the converted integer 
  
  itoa(l,tempString2,10);  // integer, buffer, base - convert the time hours or mins to string
  
  if (l < 10)
  {
  strcat(tempString1,"0");            // starts the string with a 0
  strcat(tempString1,tempString2);    // shoves the vlues behind the first 0
  strcat(s,tempString1);     // writes it all to the global bigString

    
  }else
  {
   strcat(s,tempString2);    // since over 10, can get written direct
  }
}  // end addDigits


/*-------- NTP code ----------*/

const int NTP_PACKET_SIZE = 48; // NTP time is in the first 48 bytes of message
byte packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming & outgoing packets

time_t getNtpTime()
{
  IPAddress ntpServerIP; // NTP server's ip address

  while (Udp.parsePacket() > 0) ; // discard any previously received packets
  Serial.println("Transmit NTP Request");
  // get a random server from the pool
  WiFi.hostByName(ntpServerName, ntpServerIP);
  Serial.print(ntpServerName);
  Serial.print(": ");
  Serial.println(ntpServerIP);
  sendNTPpacket(ntpServerIP);
  uint32_t beginWait = millis();
  while (millis() - beginWait < 1500) {
    int size = Udp.parsePacket();
    if (size >= NTP_PACKET_SIZE) {
      Serial.println("Receive NTP Response");
      Udp.read(packetBuffer, NTP_PACKET_SIZE);  // read packet into the buffer
      unsigned long secsSince1900;
      // convert four bytes starting at location 40 to a long integer
      secsSince1900 =  (unsigned long)packetBuffer[40] << 24;
      secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
      secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
      secsSince1900 |= (unsigned long)packetBuffer[43];
      sync_int = 14400;                              // every 4 hours re-sync since got good time result
      setSyncInterval(sync_int);           // set the number of seconds between re-sync  1 day is 86400, 5 min is 300, 30 mins is 1800, hour is 3600, 1 min is 60, 
    return secsSince1900 - 2208988800UL;  // modified to give Greenwich Time only
    }
  }
  Serial.println("No NTP Response :-(   try in 30 seconds");
  sync_int = 30;
  setSyncInterval(sync_int);           // set the number of seconds between re-sync  1 day is 86400, 5 min is 300, 30 mins is 1800, hour is 3600, 1 min is 60, 
  return 0; // return 0 if unable to get the time
}

// send an NTP request to the time server at the given address
void sendNTPpacket(IPAddress &address)
{
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12] = 49;
  packetBuffer[13] = 0x4E;
  packetBuffer[14] = 49;
  packetBuffer[15] = 52;
  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  Udp.beginPacket(address, 123); //NTP requests are to port 123
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  Udp.endPacket();
}

//Function to print time with time zone - mostly for debug
void printTime(time_t t, char *tz, char *loc)
{
    sPrintI00(hour(t));
    sPrintDigits(minute(t));
    sPrintDigits(second(t));
    Serial.print(' ');
    Serial.print(dayShortStr(weekday(t)));
    Serial.print(' ');
    sPrintI00(day(t));
    Serial.print(' ');
    Serial.print(monthShortStr(month(t)));
    Serial.print(' ');
    Serial.print(year(t));
    Serial.print(' ');
    Serial.print(tz);
    Serial.print(' ');
    Serial.print(loc);
    Serial.println();
}

//Print an integer in "00" format (with leading zero).
//Input value assumed to be between 0 and 99.
void sPrintI00(int val)
{
    if (val < 10) Serial.print('0');
    Serial.print(val, DEC);
    return;
}

//Print an integer in ":00" format (with leading zero).
//Input value assumed to be between 0 and 99.
void sPrintDigits(int val)
{
    Serial.print(':');
    if(val < 10) Serial.print('0');
    Serial.print(val, DEC);
}


// functions to be called when an alarm triggers:
void ALARMr1()
{
  Serial.println("Alarm: - Pulse the relay");   

 digitalWrite(LED_PIN,0);  //  0 sets the relay low - turns ON
 delay(250);
 digitalWrite(LED_PIN,1);
 delay(1000);
 Serial.println("Alarm: - out of function alarmr1");  
    
}  // end ALARMr1

 // functions to be called when an alarm triggers:
void ALARMtzs()   //is called once per day,  re-syncs the alarm time for the switch between DST and STD time
{
  Serial.println("beginning time zone shift check and update");
  Serial.println(Time_flag);   //has the 0=STD or 1=DST originally set when got the 1st good NTP fix
  Serial.println(hh_hold);     //has the UTC time of the local alarm hour - will shift an hour depending on the DST or STD
   utc = now();

   // Time_flag = 2;  // or 0;        debug:  force the function to pick one of the paths for debug purpose
   
   if (usCT.utcIsDST(utc) != Time_flag)  //means we flipped to the other time  1=DST, 0=STD
  {
    if (Time_flag > usCT.utcIsDST(utc))  // means going from DST (1) to STD (0) 5hrs from london in summer, 6 in winter
    {
      hh_hold = hh_hold + 1;  // hh_hold contains the timer value
      Time_flag = 0;
      Serial.println("DST to STD");
      Serial.println(Time_flag);
      Serial.println(hh_hold);
      
     }else                             // means going from STD (0) to DST (1)  6 hrs from london in winter to 5 in summer
     {
      hh_hold = hh_hold - 1;  // hh_hold contains the timer value  
      Time_flag = 1;
      Serial.println("STD to DST");
      Serial.println(Time_flag);
      Serial.println(hh_hold);
      
     }
    // reset the alarm
    Alarm.alarmRepeat(hh_hold,local_alarm_minute,0,ALARMr1);       //now have first alarm in UTC time   (HH,MM,SS,name of the alarm)
  }
  Serial.println("Out of alarm time shift update");
}  // End ALARMntp


void alarmset()    // should only run once to set the alarms up once a good ntp time fix has been accomplished
{
if(good_sync_flag == 0)           // set the alarms 1st time through with a good sync, then never again
     {
       alarmt1=now();
       Alarm.alarmRepeat(1,0,0,ALARMtzs);   //  ALARMtzs (time zone shift) - holds the time to check each day if the time swap has happened  alarm at 21 +7 hrs = 2 am london time to check
     
       if (usCT.utcIsDST(alarmt1))
        { //  means is daylight savings time when set
          Time_flag = 1;  //DST
          hh_hold = (local_alarm_hour - UTC_DST)%24;    // make sure the hour set is between 0 and 24
        } else
        {
          Time_flag = 0;  //STD
          hh_hold = (local_alarm_hour - UTC_STD)%24;   // hh_hold contains the UTC time of the alarm
        }
  
      Alarm.alarmRepeat(hh_hold,local_alarm_minute,0,ALARMr1);       //now have first alarm in UTC time   (HH,MM,SS,name of the alarm)
      good_sync_flag = 1;     // set the flag to not occur again
      Serial.println(hh_hold);
      
     }
}    

void append(char* s, char c)
{
        int len = strlen(s);
        s[len] = c;
        s[len+1] = '\0';
}


