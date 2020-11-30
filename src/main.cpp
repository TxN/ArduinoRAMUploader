#include <Arduino.h>
#include <SPI.h>
#include <SD.h>

#define RAM_DUMP_FILE_NAME "data.bin"
#define RAM_D0 2
#define RAM_D7 9
#define SHIFT_DATA A1  //SER
#define SHIFT_CLK A2   //SCK
#define SHIFT_LATCH A3 //RCK
#define WRITE_EN A4
#define RAM_OE 10
#define RAM_CS A5

#define SD_CS_PIN A0

#define PART_SIZE 128 //Read/Write chunk size
#define RAM_CHIP_CAPACITY 65536

#define DEBUG_SERIAL true

char inputBuf[64];

void SetDataBus(int io);
void SelectRAMChip(byte onOff);
void UploadFileToRAM(const char* filename);
void WriteFileToRAM(const char* filename);
void DumpRAM();
void SaveRAMToFile(char* filename);
void PrintFromBuf(char buf[], int size);
bool CompareRAMContents(const char* filename);
byte ReadRAM(unsigned int address);
void WriteRAM(unsigned int address, byte data);
void SetAddress(unsigned int address, bool outputEnable);
void FillWithByte(byte b);

void setup() {
  Serial.begin(57600);
  delay(10);
  if (!SD.begin(SD_CS_PIN)) {
    Serial.println("Card failed, or not present");
    while (1); // Endless loop
  }

  pinMode(RAM_OE, OUTPUT);
  pinMode(SHIFT_DATA, OUTPUT);
  pinMode(SHIFT_CLK, OUTPUT);
  pinMode(SHIFT_LATCH, OUTPUT);
  pinMode(WRITE_EN, OUTPUT);

  UploadFileToRAM(RAM_DUMP_FILE_NAME);
}

void loop() {
  while(1) {
    if ( !Serial.available() ) {
      continue;
    }
    auto str = Serial.readString();
    if (str.startsWith("cdump")) {
      DumpRAM();
    } else if ( str.startsWith("load")) {
      str.toCharArray(inputBuf, 64);
      auto pointer = strtok(inputBuf, " ");
      if (pointer) {
        UploadFileToRAM(pointer);
      }
    } else if (str.startsWith("fzero") ) {
      FillWithByte(0);
    } else if (str.startsWith("fone") ) {
      FillWithByte(0xFF);
    } else if (str.startsWith("f55") ) {
      FillWithByte(0x55);
    } else if (str.startsWith("faa") ) {
      FillWithByte(0xAA);
    } else if ( str.startsWith("fdump")) {
      str.toCharArray(inputBuf, 64);
      auto pointer = strtok(inputBuf, " ");
      if (pointer) {
        SaveRAMToFile(pointer);
      }
    }
  }
}

void SelectRAMChip(byte onOff) {
  pinMode(WRITE_EN, onOff);
  digitalWrite(WRITE_EN, onOff);
  pinMode(RAM_CS, OUTPUT);
  if ( onOff == 1) {
    digitalWrite(RAM_CS, LOW);
  } else {
    digitalWrite(RAM_CS, HIGH);
  }
 
}

void SaveRAMToFile(char* filename) {
  if (SD.exists(filename)) {
    Serial.print("File with name ");
    Serial.print(filename);
    Serial.println(" already exists.");
    return;
  }
  auto datafile = SD.open(filename, FILE_WRITE);
  if (!datafile) {
    Serial.print("Could not create file ");
    Serial.println(filename);
  }
  SelectRAMChip(HIGH);

  SetDataBus(INPUT);

  const unsigned long capacity = RAM_CHIP_CAPACITY;
  for (unsigned long i = 0; i < capacity; i++) {
    auto b = ReadRAM(i);
    datafile.write(b);
  }
  datafile.close();
  Serial.println("Dump complete");
  SelectRAMChip(LOW);
}

void UploadFileToRAM(const char* filename) {
  //Check if file exist
  auto dataFile = SD.open(filename);
  if (!dataFile) {
    Serial.print("Could not find dump file ");
    Serial.println(filename);
    return;
  }
  dataFile.close();

  SelectRAMChip(HIGH);
  Serial.println("Writing file to RAM");
  WriteFileToRAM(filename);
  Serial.println("Data upload complete.");
  delay(10);
  if (!CompareRAMContents(filename)) {
    Serial.println("Memory check failed. Abort.");
    while (1); // Endless loop
  }
  Serial.println("Integrity check passed.");
  SelectRAMChip(LOW); // RAM chip data were written, now clearing RESET output to allow all other hardware to boot up  
}

void WriteFileToRAM(const char* filename) {
  auto dataFile = SD.open(filename, FILE_READ);
  unsigned long size = dataFile.size(); //Max file size is 64KB
  unsigned int partCount = size / PART_SIZE;

  Serial.print("Starting upload: ");
  Serial.print(size);
  Serial.println(" bytes to write.");

  byte buf[PART_SIZE]; 
  SetDataBus(OUTPUT);
  unsigned int addr = 0;
  for (unsigned int i = 0; i < partCount; i++) {
    dataFile.readBytes(buf, PART_SIZE);
    for (byte j = 0; j < PART_SIZE; j++) {
      WriteRAM(addr, buf[j]);
      addr++;
    }
  }
  dataFile.close();
}

void FillWithByte(byte b) {
  Serial.print("Filling memory with byte ");
  Serial.println(b);
  SelectRAMChip(HIGH);
  SetDataBus(OUTPUT);
  for (unsigned long i = 0; i < RAM_CHIP_CAPACITY; i++) {
    WriteRAM(i, b);
  }
  SelectRAMChip(LOW);
  Serial.println("Complete");
}

void DumpRAM() {
  SelectRAMChip(HIGH);
  const unsigned long ramSize = RAM_CHIP_CAPACITY;
  #define DUMP_CHUNK_SIZE 32
  const unsigned int partCount = ramSize / DUMP_CHUNK_SIZE;

  SetDataBus(INPUT);

  unsigned int addr = 0;
  for (unsigned int i = 0; i < partCount; i++) {
    byte data[DUMP_CHUNK_SIZE];
    for (byte j = 0; j < DUMP_CHUNK_SIZE; j++) {
      data[j] = ReadRAM(addr);
      addr++;
    }
    char buf[16];
    unsigned int chunkIndex = i * DUMP_CHUNK_SIZE;
    int printSize = sprintf(buf, "%04x:  ", chunkIndex);
    PrintFromBuf(buf, printSize);
    for (byte k = 0; k < DUMP_CHUNK_SIZE; k++) {
      printSize = sprintf(buf, "%02x ", data[k]);
      PrintFromBuf(buf, printSize);
    }
    Serial.println("");    
  }
  SelectRAMChip(LOW);
}

void PrintFromBuf(char buf[], int size) {
  for (byte i = 0; i < size; i++) {
    Serial.print(buf[i]);
  }
}

bool CompareRAMContents(const char* filename) {
  auto dataFile = SD.open(filename, FILE_READ);
  unsigned long size = dataFile.size(); //Max file size is 64KB
  unsigned int partCount = size / PART_SIZE;

  Serial.print("Starting integrity check: ");
  Serial.print(filename);
  Serial.print(" ");
  Serial.print(size);
  Serial.println(" bytes to check.");
  
  SetDataBus(INPUT);

  byte buf[PART_SIZE]; 
  unsigned int addr = 0;
  bool fail = false;
  for (unsigned int i = 0; i < partCount; i++) {
    dataFile.readBytes(buf, PART_SIZE);
    for (byte j = 0; j < PART_SIZE; j++) {
      auto rByte = ReadRAM(addr);
      auto fByte = buf[j];
      
      if (rByte != fByte) {
        if (DEBUG_SERIAL) {
          Serial.print("Compare failed at address ");
          Serial.print(addr);
          Serial.print(" Expected: ");
          Serial.print(fByte);
          Serial.print(" Got: ");
          Serial.println(rByte);
        }
        fail = true;
      }
      __asm__("nop\n\t");
      addr++;
    }
  }
  dataFile.close();
  return !fail;
}

byte ReadRAM(unsigned int address) {
  SetAddress(address, true);
  byte data = 0;
  for (auto pin = RAM_D7; pin >= RAM_D0; pin -= 1) {
    data = (data << 1) + digitalRead(pin);
  }
  return data;
}

void SetDataBus(int io) {
  for (int pin = RAM_D0; pin <= RAM_D7; pin += 1) {
    pinMode(pin, io);
  }
}

void WriteRAM(unsigned int address, byte data) {
  SetAddress(address, false);
  for (int pin = RAM_D0; pin <= RAM_D7; pin += 1) {
    digitalWrite(pin, data & 1);
    data = data >> 1;
  }
  digitalWrite(WRITE_EN, LOW);
   __asm__("nop\n\t");
  digitalWrite(WRITE_EN, HIGH);
}

void SetAddress(unsigned int address, bool outputEnable) {
  shiftOut(SHIFT_DATA, SHIFT_CLK, MSBFIRST, (address >> 8));
  shiftOut(SHIFT_DATA, SHIFT_CLK, MSBFIRST, address);
  digitalWrite(RAM_OE, !outputEnable);
  digitalWrite(SHIFT_LATCH, LOW);
  digitalWrite(SHIFT_LATCH, HIGH);
  digitalWrite(SHIFT_LATCH, LOW);
}
