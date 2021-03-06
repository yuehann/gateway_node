#include <Arduino.h>
#include <stdio.h>
#include "constants.h"
#include "cipher.h"

//BLE includes
#include <SPI.h>
#if not defined (_VARIANT_ARDUINO_DUE_X_) && not defined (_VARIANT_ARDUINO_ZERO_)
  #include <SoftwareSerial.h>
#endif
#include "Adafruit_BLE.h"
#include "Adafruit_BluefruitLE_SPI.h"
#include "Adafruit_BluefruitLE_UART.h"
#include "BluefruitConfig.h"
#include "VirtualWire.h"

#define RXPIN 2
#define BUFSIZE    8 
#define BUF_CHR_SIZE    1 

#define FACTORYRESET_ENABLE         1
#define MINIMUM_FIRMWARE_VERSION    "0.6.6"
#define MODE_LED_BEHAVIOUR          "MODE"
#define TEMP_OFFSET -7


/* globals */
volatile byte ble_read_trigger = 0;
const byte interruptPin = 7;

/* ...hardware SPI, using SCK/MOSI/MISO hardware SPI pins and then user selected CS/IRQ/RST */
Adafruit_BluefruitLE_SPI ble(BLUEFRUIT_SPI_CS, BLUEFRUIT_SPI_IRQ, BLUEFRUIT_SPI_RST);

void my_ble_ISR(){
  Serial.println("i am the ble isr");
  ble_read_trigger = 1;
  }


// A small helper
void error(const __FlashStringHelper*err) {
  Serial.println(err);
  while (1);
}


void setup(void)
{
  while (!Serial);  // required for Flora & Micro
  delay(500);
  Serial.begin(4800);

  // Initialise the IO and ISR for the RF receiver
  vw_set_rx_pin(RXPIN);
  vw_set_ptt_inverted(true); // Required for DR3100
  vw_setup(100);   // Bits per sec
  vw_rx_start();       // Start the receiver PLL running
    
  setupADC();
  //configure ble interrupt
  //pinMode(interruptPin, INPUT_PULLUP);
  //digitalPinToInterrupt(interruptPin)
  //attachInterrupt(digitalPinToInterrupt(BLUEFRUIT_SPI_IRQ), my_ble_ISR, RISING);
  
  
  Serial.println(F("Adafruit Bluefruit Command <-> Data Mode Example"));
  Serial.println(F("------------------------------------------------"));

  /* Initialise the module */
  Serial.print(F("Initialising the Bluefruit LE module: "));

  if ( !ble.begin(VERBOSE_MODE) )
  {
    error(F("Couldn't find Bluefruit, make sure it's in CoMmanD mode & check wiring?"));
  }
  Serial.println( F("OK!") );

  if ( FACTORYRESET_ENABLE )
  {
    /* Perform a factory reset to make sure everything is in a known state */
    Serial.println(F("Performing a factory reset: "));
    if ( ! ble.factoryReset() ){
      error(F("Couldn't factory reset"));
    }
  }

  /* Disable command echo from Bluefruit */
  ble.echo(false);

  Serial.println("Requesting Bluefruit info:");
  /* Print Bluefruit information */
  ble.info();
/*
  Serial.println(F("Please use Adafruit Bluefruit LE app to connect in UART mode"));
  Serial.println(F("Then Enter characters to send to Bluefruit"));
  Serial.println();
*/
  ble.verbose(false);  // debug info is a little annoying after this point!

  /* Wait for connection */
  Serial.println("Waiting for connection...\n");
  while (! ble.isConnected()) {
      delay(500);
  }
Serial.println("[OK] Connected!\n");
  Serial.println(F("******************************"));

  // LED Activity command is only supported from 0.6.6
  if ( ble.isVersionAtLeast(MINIMUM_FIRMWARE_VERSION) )
  {
    // Change Mode LED Activity
    Serial.println(F("Change LED activity to " MODE_LED_BEHAVIOUR));
    ble.sendCommandCheckOK("AT+HWModeLED=" MODE_LED_BEHAVIOUR);
  }

  // Set module to DATA mode
  Serial.println( F("Switching to DATA mode!") );
  ble.setMode(BLUEFRUIT_MODE_DATA);

  Serial.println(F("******************************"));
 // Serial.println(">>Waiting for data request...");
}


//cpu temp//

void setupADC(){
  cli();  //Disable global interrupts

  //ADC Multiplexer Selection Register
  ADMUX = 0;
  ADMUX |= (1 << REFS1);  //Internal 2.56V Voltage Reference with external capacitor on AREF pin
  ADMUX |= (1 << REFS0);  //Internal 2.56V Voltage Reference with external capacitor on AREF pin
  ADMUX |= (0 << MUX4);  //Temperature Sensor - 100111
  ADMUX |= (0 << MUX3);  //Temperature Sensor - 100111
  ADMUX |= (1 << MUX2);  //Temperature Sensor - 100111
  ADMUX |= (1 << MUX1);  //Temperature Sensor - 100111
  ADMUX |= (1 << MUX0);  //Temperature Sensor - 100111

  //ADC Control and Status Register A 
  ADCSRA = 0;
  ADCSRA |= (1 << ADEN);  //Enable the ADC
  ADCSRA |= (1 << ADPS2);  //ADC Prescaler - 16 (16MHz -> 1MHz)

  //ADC Control and Status Register B 
  ADCSRB = 0;
  ADCSRB |= (1 << MUX5);  //Temperature Sensor - 100111

  sei();  //Enable global interrupts
}

int getTemp(){
  ADCSRA |= (1 << ADSC);  //Start temperature conversion
  while (bit_is_set(ADCSRA, ADSC));  //Wait for conversion to finish
  byte low  = ADCL;
  byte high = ADCH;
  int temperature = (high << 8) | low;  //Result is in kelvin
  return temperature - 273 + TEMP_OFFSET;
}

int rcvValues(uint8_t buf[], uint8_t buflen,uint8_t * values){
  
    Serial.println(">>get ota readings...");
    if (vw_get_message(buf, &buflen)) // Non-blocking
    {
      int i;

        digitalWrite(13, true); // Flash a light to show received good message
        
      // Message with a good checksum received, dump it.
      for (i = 0; i < buflen; i++)
    {
      Serial.print(char(buf[i]));
      values[i] = (char)buf[i];
    }
        Serial.println("");
        digitalWrite(13, false);
        return 0;
    }
  Serial.println("#no readings received");
  return 1;
  }


//Main

void loop()
{ 
    uint8_t inputKey[] = {0x00, 0x01, 0x02, 0x03, 0x08, 0x09, 0x0a, 0x0b, 0x10, 0x11, 0x12, 0x13, 0x18, 0x19, 0x1a, 0x1b};
    char msg_enc_snd[BUFSIZE+1];
    uint8_t keys[SIMON_BLOCK_SIZE/16*SIMON_ROUNDS];
    uint8_t plainText[BUFSIZE+1];
    
    uint8_t txt[BUFSIZE+1];
    uint8_t buf[VW_MAX_MESSAGE_LEN];
    uint8_t buflen = VW_MAX_MESSAGE_LEN;

    uint8_t values[BUFSIZE+1];
    int ret;
    
    if( !(ret=rcvValues(buf,buflen,values)) ){
      
          encryptKeySchedule(inputKey, keys);
          decrypt(values,keys);
    }
    
        Serial.println("Recording sensor values...");
        //cpu temp
        txt[0] = (uint8_t)(getTemp());
        Serial.print("Cpu temp = ");
        Serial.println(txt[0]);
        //water pressure
        txt[1] = (uint8_t)(random(70,90));
        Serial.print("Water pressure = ");
        Serial.println(txt[1]);
        
        if(!ret){
              //temp from sensor DHT22
              txt[2] = values[0];
              Serial.print("DHT22 temp = ");
              Serial.println(txt[2]);
              //humidity from sensor DHT22
              txt[3] = values[1];
              Serial.print("DHT22 humidity = ");
              Serial.println(txt[3]);
        }
        else{
              // no readings, generate random
              txt[2]=(uint8_t)(random(20,30));
              Serial.print("DHT22_gen temp = ");
              Serial.println(txt[2]);
          
              txt[3]=(uint8_t)(random(40,50));
              Serial.print("DHT22_gen humidity  = ");
              Serial.println(txt[3]);
          }
        //room temp
        
        for(int i = 4; i < 7; i++){
        txt[i] = (uint8_t)(random(20,30));
        Serial.print("Room ");
        Serial.print(i-3);
        Serial.print(" temp = ");
        Serial.println(txt[i]);
        }
        //fan speed
        Serial.print("Fan Speed = ");
        txt[7]=(uint8_t)(random(60,70));
        Serial.println(txt[7]*100);
        
  // encrypt
    
    for(int i = 0 ; i < 8 ; i++ ){
      plainText[i] = (uint8_t)txt[i];
      }
      
    encryptKeySchedule(inputKey, keys);
    printArr(plainText,"PlainText: ");
    
    encrypt(plainText, keys);
    printArr(plainText,"After encryption: ");
  
  for(int i = 0;i < 8 ;i++){
    msg_enc_snd[i] = char(plainText[i]);
    Serial.print(char(plainText[i]));
    }
    Serial.println(""); 
  
  // send over ble
  ble.print(msg_enc_snd);
  
  delay(1000);
}


