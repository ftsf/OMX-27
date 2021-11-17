// OMX-27 MIDI KEYBOARD / SEQUENCER
// v 1.4.1
//
// Steven Noreyko, November 2021
//
//
//	Big thanks to:
//	John Park and Gerald Stevens for initial testing and feature ideas
//	mzero for immense amounts of code coaching/assistance
//	drjohn for support
//  Additional code contributions: Matt Boone, Steven Zydek, Chris Atkins, Will Winder


#include <Adafruit_Keypad.h>
#include <Adafruit_NeoPixel.h>
#include <ResponsiveAnalogRead.h>
#include <U8g2_for_Adafruit_GFX.h>

#include "consts.h"
#include "config.h"
#include "colors.h"
#include "MM.h"
#include "ClearUI.h"
#include "sequencer.h"
#include "noteoffs.h"
#include "storage.h"
#include "scales.h"
#include "util.h"

typedef struct {
	const char* label;
	int* valuePtr;
	bool readOnly;
	int min, max;
	void (*drawValue)(int,int);
	void (*onChange)(int*,int,int);
	char* (*getLabel)();
	int (*getValue)();
} ui_param;

bool uiEditParam = false;

const auto ARP_SEQ_LEN = 32;

U8G2_FOR_ADAFRUIT_GFX u8g2_display;

const int potCount = 5;
ResponsiveAnalogRead *analog[potCount];

// storage of pot values; current is in the main loop; last value is for midi output
int volatile currentValue[potCount];
int lastMidiValue[potCount];
int potMin = 0;
int potMax = 8190;
int temp;

// Timers and such
elapsedMillis blink_msec = 0;
elapsedMillis slow_blink_msec = 0;
elapsedMillis pots_msec = 0;
elapsedMillis dialogTimeout = 0;
elapsedMillis dirtyDisplayTimer = 0;
unsigned long displayRefreshRate = 60;
elapsedMicros clksTimer = 0;		// is this still in use?

//unsigned long clksDelay;
elapsedMillis keyPressTime[27] = {0};

using Micros = unsigned long;
Micros lastProcessTime;
Micros nextStepTime;
Micros lastStepTime;
volatile unsigned long step_micros;
volatile unsigned long noteon_micros;
volatile unsigned long noteoff_micros;
volatile unsigned long ppqInterval;

// ANALOGS
int potbank = 0;
int analogValues[] = {0,0,0,0,0};		// default values
int potValues[] = {0,0,0,0,0};
int potCC = pots[potbank][0];
int potVal = analogValues[0];
int potNum = 0;
bool plockDirty[] = {false,false,false,false,false};
int prevPlock[] = {0,0,0,0,0};

// MODES
OMXMode omxMode = DEFAULT_MODE;
OMXMode newmode = DEFAULT_MODE;

int nspage = 0;
int pppage = 0;
int sqpage = 0;
int srpage = 0;

int pageWrapDelay = 0;
int pageChangeDelay = 0;

int miparam = 0;	// midi params item counter
int nsparam = 0;	// note select params
int ppparam = 0;	// pattern params
int sqparam = 0;	// seq params
int srparam = 0;	// step record params
int tmpmmode = 9;

// VARIABLES / FLAGS
float step_delay;
bool dirtyPixels = false;
bool dirtyDisplay = false;
bool blinkState = false;
bool slowBlinkState = false;
bool noteSelect = false;
bool noteSelection = false;
bool seqPages = false;
int noteSelectPage = 0;
int selectedNote = 0;
int selectedStep = 0;
bool stepSelect = false;
bool stepRecord = false;
bool stepDirty = false;

uint32_t stepColor = 0x000000;
uint32_t muteColor = 0x000000;

bool dialogFlags[] = {false, false, false, false, false, false};
unsigned dialogDuration = 1000;

bool copiedFlag = false;
bool pastedFlag = false;
bool clearedFlag = false;

bool mode_select = false;
bool midiAUX = false;
bool auxShift = false;

bool arp = false;
bool arpLatch = false;
int arpTimer = 0;
int arpTime = 0;
int arpSwing = 0;
int arpSwingIndex = 0;
int arpOctaves = 2;
int arpOctaveIndex = 0;
int arpGate = 127;

int defaultVelocity = 100;
int midiVelocity = 100;
const auto minOctave = -4;
const auto maxOctave = 5;
int octave = 0;			// default C4 is 0 - range is -4 to +5
int newoctave = octave;
int transpose = 0;
int rotationAmt = 0;
int hline = 8;
int pitchCV;
uint8_t RES;
uint16_t AMAX;
int V_scale;

// clock
float clockbpm = 120;
int bpm = (int)clockbpm;;
float newtempo = clockbpm;
unsigned long tempoStartTime, tempoEndTime;

unsigned long blinkInterval = clockbpm * 2;
unsigned long longPressInterval = 1000;
unsigned long shortPressInterval = 500;

uint8_t swing = 0;
const int maxswing = 100;
// int swing_values[maxswing] = {0, 1, 3, 5, 52, 66, 70, 72, 80, 99 }; // 0 = off, <50 early swing , >50 late swing, 99=drunken swing

typedef struct {
	int note;
	int channel;
} midiNote;

bool keyState[27] = {false};
int midiKeyState[27] =     {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
int midiChannelState[27] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
int arpSeq[ARP_SEQ_LEN];
midiNote arpActiveNotes[16];
int arpLastNote = -1;
int arpLastChannel = -1;
uint8_t arpIndex = -1;
int rrIndex = 0;
bool midiRoundRobin = false;
int midiRRChannelCount = 1;
int midiRRChannelOffset = 0;
int currpgm = 0;
int currbank = 0;
bool midiInToCV = true;
int midiChannel = 1;
int midiLastNote = -1;

const auto uiDrawTextY = 20;

void uiDrawValueNone(int value, int x) {
	u8g2centerText("---", x*32, hline*2+3, 32, 20);
}

void uiDrawValueInt(int value, int x) {
	if(value == -127) {
		u8g2centerText("---", x*32, hline*2+3, 32, uiDrawTextY);
	} else {
		u8g2centerNumber(value, x*32, hline*2+3, 32, uiDrawTextY);
	}
}

void uiDrawValueOctave(int value, int x) {
	u8g2centerNumber(value+4, x*32, hline*2+3, 32, uiDrawTextY);
}

void uiDrawValueBool(int value, int x) {
	u8g2centerText(value == 1 ? "ON" : "OFF", x*32, hline*2+3, 32, uiDrawTextY);
}

void uiDrawValueInt1(int value, int x) {
	u8g2centerNumber(value+1, x*32, hline*2+3, 32, uiDrawTextY);
}

void uiDrawValuePercent(int value, int x) {
	// TODO
	u8g2centerNumber(value, x*32, hline*2+3, 32, uiDrawTextY);
}

char ccNameBuf[6];
char noteBuffer[4];
const auto MESSAGE_TEXT_LEN = 24;
char messageText[MESSAGE_TEXT_LEN];

void uiDrawValueNote(int value, int x) {
	if(value < 0) {
		u8g2centerText("---", x*32, hline*2+3, 32, uiDrawTextY);
	} else {
		if(value < 12) {
			snprintf(noteBuffer, 4, "%d", value);
		} else {
			snprintf(noteBuffer, 4, "%s%X", noteNames[value % 12], (value / 12) - 1);
		}
		u8g2centerText(noteBuffer, x*32, hline*2+3, 32, uiDrawTextY);
	}
}


const ui_param param_OCT = {
	"OCT",
	&octave,
	false,
	minOctave,
	maxOctave,
	uiDrawValueOctave
};

const ui_param param_CH = {
	"CH",
	&midiChannel,
	false,
	1,
	16,
	uiDrawValueInt
};

const ui_param param_BPM = {
	"BPM",
	&bpm,
	false,
	40,
	300,
	uiDrawValueInt,
	[](int* valPtr, int newVal, int amt)->void {
		*valPtr = newVal;
		clockbpm = (float)newVal;
		resetClocks();
	}
};

const ui_param midi_params[] = {
	param_OCT,
	param_CH,
	{
		"CC",
		&potVal,
		true,
		0,
		127,
		uiDrawValueInt,
		NULL,
		[]()->char* {
		  snprintf(ccNameBuf, 6, "CC%d", potCC);
		  return ccNameBuf;
		}
	},
	{
		"NOTE",
		&midiLastNote,
		true,
		0,
		255,
		uiDrawValueNote
	},
	///
	{
		"RR",
		&midiRRChannelCount,
		false,
		1,
		16,
		uiDrawValueInt
	},
	{
		"PGM",
		&currpgm,
		false,
		0,
		127,
		uiDrawValueInt1
	},
	{
		"BNK",
		&currbank,
		false,
		0,
		127,
		uiDrawValueInt
	},
	{
		"PBNK",
		&potbank,
		false,
		0,
		3,
		uiDrawValueInt1
	},
	///
	{
		"A.SWG",
		&arpSwing,
		false,
		-127,
		127,
		uiDrawValuePercent
	},
	param_BPM,
	{
		"A.OCT",
		&arpOctaves,
		false,
		1,
		4,
		uiDrawValueInt
	},
	{
		"A.GAT",
		&arpGate,
		false,
		0,
		127,
		uiDrawValuePercent
	},
	{
		"TEST",
		&arpGate,
		false,
		0,
		127,
		uiDrawValuePercent
	},
};

const ui_param seq_pattern_params[] = {
	{
		"PTN",
		&playingPattern,
		false,
		0,
		7,
		uiDrawValueInt1
	},
	{
		"TRSP",
		(int*)&transpose,
		false,
		-64,
		63,
		uiDrawValueNone,
		[](int* valPtr, int newVal, int amt)->void {
			transposeSeq(playingPattern, amt);
			transpose = newVal;
		},
	},
	{
		"SWNG",
		NULL,
		false,
		0,
		maxswing-1,
		uiDrawValuePercent,
		[](int* valPtr, int newVal, int amt)->void {
			patternSettings[playingPattern].swing = newVal;
			setGlobalSwing(newVal);
		},
		NULL,
		[]()->int {
			return (int)patternSettings[playingPattern].swing;
		}
	},
	param_BPM,
	{
		"SOLO",
		NULL,
		false,
		0,
		1,
		uiDrawValueBool,
		[](int* valPtr, int newVal, int amt)->void {
			patternSettings[playingPattern].solo = newVal;
			if(patternSettings[playingPattern].solo) {
				setAllLEDS(0,0,0);
			}
		},
		NULL,
		[]()->int {
			return (int)patternSettings[playingPattern].solo;
		}
	},
	{
		"LEN",
		NULL,
		false,
		0,
		NUM_STEPS,
		uiDrawValueInt1,
		[](int* valPtr, int newVal, int amt)->void {
			patternSettings[playingPattern].len = newVal;
		},
		NULL,
		[]()->int {
			return (int)patternSettings[playingPattern].len;
		}

	},
	{
		"RATE",
		NULL,
		false,
		0,
		NUM_MULTDIVS-1,
		[](int v, int x)->void {
			u8g2centerText(mdivs[v], x*32, hline*2+3, 32, uiDrawTextY);
		},
		[](int* valPtr, int newVal, int amt)->void {
			patternSettings[playingPattern].clockDivMultP = newVal;
		},
		NULL,
		[]()->int {
			return (int)patternSettings[playingPattern].clockDivMultP;
		}

	},
	{
		"CV",
		(int*)&cvPattern[playingPattern],
		false,
		0,
		1,
		uiDrawValueBool,
	},
	{
		"ROT",
		&rotationAmt,
		false,
		0,
		127,
		uiDrawValueNone,
		[](int* valPtr, int newVal, int amt)->void {
			rotatePattern(playingPattern, amt);;
		},
	},
	{
		"CHAN",
		NULL,
		false,
		0,
		15,
		uiDrawValueInt1,
		[](int* valPtr, int newVal, int amt)->void {
			patternSettings[playingPattern].channel = newVal;
		},
		NULL,
		[]()->int {
			return (int)patternSettings[playingPattern].channel;
		}
	},
	{
		"START",
		NULL,
		false,
		0,
		64,
		uiDrawValueInt1,
		[](int* valPtr, int newVal, int amt)->void {
			patternSettings[playingPattern].startstep = CLAMP(newVal, 0, patternSettings[playingPattern].len);
		},
		NULL,
		[]()->int {
			return (int)patternSettings[playingPattern].startstep;
		}
	},
	{
		"END",
		NULL,
		false,
		0,
		64,
		uiDrawValueInt,
		[](int* valPtr, int newVal, int amt)->void {
			patternSettings[playingPattern].autoresetstep = CLAMP(newVal, 0, patternSettings[playingPattern].len+1);
		},
		NULL,
		[]()->int {
			return (int)patternSettings[playingPattern].autoresetstep;
		}
	},
	{
		"FREQ",
		NULL,
		false,
		0,
		15,
		uiDrawValueInt,
		[](int* valPtr, int newVal, int amt)->void {
			patternSettings[playingPattern].autoresetfreq = newVal;
		},
		NULL,
		[]()->int {
			return (int)patternSettings[playingPattern].autoresetfreq;
		}
	},
	{
		"PROB",
		NULL,
		false,
		0,
		100,
		uiDrawValueInt,
		[](int* valPtr, int newVal, int amt)->void {
			patternSettings[playingPattern].autoresetprob = newVal;
		},
		NULL,
		[]()->int {
			return (int)patternSettings[playingPattern].autoresetprob;
		}
	},
};

const ui_param seq_note_select_params[] = {
	{
		"NOTE",
		NULL,
		false,
		0,
		127,
		uiDrawValueNote,
		[](int* valPtr, int newVal, int amt)->void {
			stepNoteP[playingPattern][selectedStep].note = newVal;
			if(stepNoteP[playingPattern][selectedStep].trig == TRIGTYPE_MUTE) {
				stepNoteP[playingPattern][selectedStep].trig = TRIGTYPE_PLAY;
			}
		},
		NULL,
		[]()->int {
			return (int)stepNoteP[playingPattern][selectedStep].note;
		}
	},
	{
		"VEL",
		NULL,
		false,
		0,
		127,
		uiDrawValueInt,
		[](int* valPtr, int newVal, int amt)->void {
			stepNoteP[playingPattern][selectedStep].vel = newVal;
		},
		NULL,
		[]()->int {
			return (int)stepNoteP[playingPattern][selectedStep].vel;
		}
	},
	{
		"LEN",
		NULL,
		false,
		0,
		127,
		uiDrawValueInt1,
		[](int* valPtr, int newVal, int amt)->void {
			stepNoteP[playingPattern][selectedStep].len = newVal;
		},
		NULL,
		[]()->int {
			return (int)stepNoteP[playingPattern][selectedStep].len;
		}
	},
	{
		"TYPE",
		NULL,
		false,
		0,
		STEPTYPE_COUNT-1,
		[]()->char* {
			return stepTypes[stepNoteP[playingPattern][selectedStep].stepType];
		},
		[](int* valPtr, int newVal, int amt)->void {
			stepNoteP[playingPattern][selectedStep].stepType = (StepType)newVal;
		},
		NULL,
		[]()->int {
			return (int)stepNoteP[playingPattern][selectedStep].stepType;
		}
	},
	{
		"PROB",
		NULL,
		false,
		0,
		127,
		uiDrawValueInt,
		[](int* valPtr, int newVal, int amt)->void {
			stepNoteP[playingPattern][selectedStep].prob = newVal;
		},
		NULL,
		[]()->int {
			return (int)stepNoteP[playingPattern][selectedStep].prob;
		}
	},
	{
		"COND",
		NULL,
		false,
		0,
		127,
		[]()->char* {
			return trigConditions[stepNoteP[playingPattern][selectedStep].condition];
		},
		[](int* valPtr, int newVal, int amt)->void {
			stepNoteP[playingPattern][selectedStep].condition = newVal;
		},
		NULL,
		[]()->int {
			return (int)stepNoteP[playingPattern][selectedStep].condition;
		}
	},
	{
		"L-1",
		NULL,
		false,
		0,
		127,
		uiDrawValueInt,
		[](int* valPtr, int newVal, int amt)->void {
			stepNoteP[playingPattern][selectedStep].params[0] = newVal;
		},
		NULL,
		[]()->int {
			return (int)stepNoteP[playingPattern][selectedStep].params[0];
		}
	},
	{
		"L-2",
		NULL,
		false,
		0,
		127,
		uiDrawValueInt,
		[](int* valPtr, int newVal, int amt)->void {
			stepNoteP[playingPattern][selectedStep].params[1] = newVal;
		},
		NULL,
		[]()->int {
			return (int)stepNoteP[playingPattern][selectedStep].params[1];
		}
	},
	{
		"L-3",
		NULL,
		false,
		0,
		127,
		uiDrawValueInt,
		[](int* valPtr, int newVal, int amt)->void {
			stepNoteP[playingPattern][selectedStep].params[2] = newVal;
		},
		NULL,
		[]()->int {
			return (int)stepNoteP[playingPattern][selectedStep].params[2];
		}
	},
	{
		"L-4",
		NULL,
		false,
		0,
		127,
		uiDrawValueInt,
		[](int* valPtr, int newVal, int amt)->void {
			stepNoteP[playingPattern][selectedStep].params[3] = newVal;
		},
		NULL,
		[]()->int {
			return (int)stepNoteP[playingPattern][selectedStep].params[3];
		}
	},
};

const ui_param seq_step_record_params[] = {
	param_OCT,
	{
		"STEP",
		&seqPos[playingPattern],
		false,
		0,
		15,
		uiDrawValueInt1,
	},
	{
		"NOTE",
		NULL,
		false,
		0,
		127,
		uiDrawValueNote,
		[](int* valPtr, int newVal, int amt)->void {
			stepNoteP[playingPattern][selectedStep].note = newVal;
		},
		NULL,
		[]()->int {
			return (int)stepNoteP[playingPattern][selectedStep].note;
		}
	},
	{
		"PTN",
		&playingPattern,
		false,
		0,
		7,
		uiDrawValueInt1
	},
};







// ENCODER
Encoder myEncoder(12, 11); 	// encoder pins on hardware
Button encButton(0);		// encoder button pin on hardware
//long newPosition = 0;
//long oldPosition = -999;


//initialize an instance of class Keypad
Adafruit_Keypad customKeypad = Adafruit_Keypad( makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// Declare NeoPixel strip object
Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

// setup EEPROM/FRAM storage
Storage* storage;

// ####### CLOCK/TIMING #######

void advanceClock(Micros advance) {
	static Micros timeToNextClock = 0;
	while (advance >= timeToNextClock) {
		advance -= timeToNextClock;

		MM::sendClock();
		timeToNextClock = ppqInterval * (PPQ / 24);
	}
	timeToNextClock -= advance;
}

void advanceSteps(Micros advance) {
	static Micros timeToNextStep = 0;
//	static Micros stepnow = micros();
	while (advance >= timeToNextStep) {
		advance -= timeToNextStep;
		timeToNextStep = ppqInterval;

		// turn off any expiring notes
		pendingNoteOffs.play(micros());

		// turn on any pending notes
		pendingNoteOns.play(micros());
	}
	timeToNextStep -= advance;
}

void resetClocks(){
	// BPM tempo to step_delay calculation
	ppqInterval = 60000000/(PPQ * clockbpm); 		// ppq interval is in microseconds
	step_micros = ppqInterval * (PPQ/4); 			// 16th note step in microseconds (quarter of quarter note)

	// 16th note step length in milliseconds
	step_delay = step_micros * 0.001; 	// ppqInterval * 0.006; // 60000 / clockbpm / 4;
}

void setGlobalSwing(int swng_amt){
	for(int z=0; z<NUM_PATTERNS; z++) {
		patternSettings[z].swing = swng_amt;
	}
}

// ####### POTENTIMETERS #######

void sendPots(int val, int channel){
	MM::sendControlChange(pots[potbank][val], analogValues[val], channel);
	potCC = pots[potbank][val];
	Serial.print("potCC = ");
	Serial.println(potCC);
	potVal = analogValues[val];
	potValues[val] = potVal;
}

void readPotentimeters(){
	for(int k=0; k<potCount; k++) {
		temp = analogRead(analogPins[k]);
		analog[k]->update(temp);

		// read from the smoother, constrain (to account for tolerances), and map it
		temp = analog[k]->getValue();
		temp = constrain(temp, potMin, potMax);
		temp = map(temp, potMin, potMax, 0, 16383);

		// map and update the value
		analogValues[k] = temp >> 7;

		if(analog[k]->hasChanged()) {
			 // do stuff
			switch(omxMode) {
				case MODE_OM:
						// fall through - same as MIDI
				case MODE_MIDI: // MIDI
					if(k == 4) {
						midiVelocity = analogValues[k];
					} else {
						sendPots(k, midiChannel);
						dirtyDisplay = true;
					}
					break;

				case MODE_S2: // SEQ2
						// fall through - same as SEQ1
				case MODE_S1: // SEQ1
					if (noteSelect && noteSelection){ // note selection - do P-Locks
						potNum = k;
						potCC = pots[potbank][k];
						potVal = analogValues[k];

						if (k < 4){ // only store p-lock value for first 4 knobs
							stepNoteP[playingPattern][selectedStep].params[k] = analogValues[k];
							sendPots(k, PatternChannel(playingPattern));
						}
						sendPots(k, PatternChannel(playingPattern));
						dirtyDisplay = true;

					} else if (stepRecord){
						potNum = k;
						potCC = pots[potbank][k];
						potVal = analogValues[k];

						if (k < 4){ // only store p-lock value for first 4 knobs
							stepNoteP[playingPattern][seqPos[playingPattern]].params[k] = analogValues[k];
							sendPots(k, PatternChannel(playingPattern));
						} else if (k == 4){
							stepNoteP[playingPattern][seqPos[playingPattern]].vel = analogValues[k]; // SET POT 5 to NOTE VELOCITY HERE
						}
						dirtyDisplay = true;
					} else if (!noteSelect || !stepRecord){
						sendPots(k, PatternChannel(playingPattern));
					}
					break;

				default:
					break;
				}
			}
	}
}



// ####### SETUP #######

void setup() {
	Serial.begin(115200);
	
	// incoming usbMIDI callbacks
	usbMIDI.setHandleNoteOff(OnNoteOff);
	usbMIDI.setHandleNoteOn(OnNoteOn);

	storage = Storage::initStorage();
	dialogTimeout = 0;
	clksTimer = 0;

	lastProcessTime = micros();
	resetClocks();

	nextStepTime = micros();
	lastStepTime = micros();
	for (int x=0; x<NUM_PATTERNS; x++){
		timePerPattern[x].nextStepTimeP = nextStepTime; // initialize all patterns
		timePerPattern[x].lastStepTimeP = lastStepTime; // initialize all patterns
		patternSettings[x].clockDivMultP = 2; // set all DivMult to 2 for now
	}
	randomSeed(analogRead(13));

	// SET ANALOG READ resolution to teensy's 13 usable bits
	analogReadResolution(13);

	// initialize ResponsiveAnalogRead
	for (int i = 0; i < potCount; i++){
		analog[i] = new ResponsiveAnalogRead(0, true, .001);
		analog[i]->setAnalogResolution(1 << 13);

		// ResponsiveAnalogRead is designed for 10-bit ADCs
		// meanining its threshold defaults to 4. Let's bump that for
		// our 13-bit adc by setting it to 4 << (13-10)
		analog[i]->setActivityThreshold(32);

		currentValue[i] = 0;
		lastMidiValue[i] = 0;
	}

	// HW MIDI
	MM::begin();

	//CV gate pin
	pinMode(CVGATE_PIN, OUTPUT);

	// set DAC Resolution CV/GATE
	RES = 12;
	analogWriteResolution(RES); // set resolution for DAC
		AMAX = pow(2,RES);
		V_scale = 64; // pow(2,(RES-7)); 4095 max
	analogWrite(CVPITCH_PIN, 0);

	// Load from EEPROM
	bool bLoaded = loadFromStorage();
	if ( !bLoaded )
	{
		// Failed to load due to initialized EEPROM or version mismatch
		// defaults
		omxMode = DEFAULT_MODE;
		playingPattern = 0;
		midiChannel = 1;
		pots[0][0] = CC1;
		pots[0][1] = CC2;
		pots[0][2] = CC3;
		pots[0][3] = CC4;
		pots[0][4] = CC5;
		initPatterns();
	}

	// Init Display
	initializeDisplay();
	u8g2_display.begin(display);

	// Startup screen
	display.clearDisplay();
	testdrawrect();
	delay(200);
	display.clearDisplay();
	u8g2_display.setForegroundColor(WHITE);
	u8g2_display.setBackgroundColor(BLACK);
	drawLoading();

	// Keypad
	customKeypad.begin();

	//LEDs
	strip.begin();           // INITIALIZE NeoPixel strip object (REQUIRED)
	strip.show();            // Turn OFF all pixels ASAP
	strip.setBrightness(LED_BRIGHTNESS); // Set BRIGHTNESS to about 1/5 (max = 255)
	for(int i=0; i<LED_COUNT; i++) { // For each pixel...
		strip.setPixelColor(i, HALFWHITE);
		strip.show();   // Send the updated pixel colors to the hardware.
		delay(5); // Pause before next pass through loop
	}
	rainbow(5); // rainbow startup pattern
	delay(500);

	// clear LEDs
	strip.fill(0, 0, LED_COUNT);
	strip.show();

	for(int i = 0; i < ARP_SEQ_LEN; i++) {
		arpSeq[i] = -1;
	}

	// setup UI
	snprintf(noteBuffer, 4, "---");
	snprintf(ccNameBuf, 6, "CC---");

	delay(100);

	// Clear display
	display.display();

	dirtyDisplay = true;
}

// ####### END SETUP #######



int scaleRoot = 0;
int scalePattern = -1;
bool scaleSelectHold;

int seqStepHold = -1;

int messageTextTimer = 0;

int getDefaultColor(int pixel) {
	if(scalePattern == -1) {
		return LEDOFF;
	} else {
		if(midiAUX) {
			if(pixel == 1 || pixel == 2 || pixel == 3 || pixel == 4 || pixel == 11 || pixel == 12) {
				return LEDOFF;
			}
		}
		int noteInOct = notes[pixel] % 12;
		return scaleColors[noteInOct];
	}
}

// ####### MIDI LEDS #######

void midi_leds() {
	blinkInterval = step_delay*2;

	if (blink_msec >= blinkInterval){
		blinkState = !blinkState;
		blink_msec = 0;
	}

	if (midiAUX){
		// Blink left/right keys for octave select indicators.
		auto color1 = blinkState ? LIME : getDefaultColor(1);
		auto color2 = blinkState ? MAGENTA : getDefaultColor(2);
		auto color3 = blinkState ? CYAN : getDefaultColor(3);
		auto color4 = blinkState ? INDIGO : getDefaultColor(4);
		auto color11 = blinkState ? ORANGE : getDefaultColor(11);
		auto color12 = blinkState ? RBLUE : getDefaultColor(12);

		strip.setPixelColor(0, RED);
		strip.setPixelColor(1, color1);
		strip.setPixelColor(2, color2);
		strip.setPixelColor(3, color3);
		strip.setPixelColor(4, color4);
		strip.setPixelColor(11, color11);
		strip.setPixelColor(12, color12);

	} else {
		strip.setPixelColor(0, LEDOFF);
	}

	dirtyPixels = true;
}

// ####### SEQUENCER LEDS #######

void show_current_step(int patternNum) {
	blinkInterval = step_delay*2;
	unsigned long slowBlinkInterval = blinkInterval * 2;

	if (blink_msec >= blinkInterval){
		blinkState = !blinkState;
		blink_msec = 0;
	}
	if (slow_blink_msec >= slowBlinkInterval){
		slowBlinkState = !slowBlinkState;
		slow_blink_msec = 0;
	}


	// AUX KEY

	if (playing && blinkState){
		strip.setPixelColor(0, WHITE);
	} else if (noteSelect && blinkState){
		strip.setPixelColor(0, NOTESEL);
	} else if (stepRecord && blinkState){
		strip.setPixelColor(0, seqColors[patternNum]);
	} else {
		switch(omxMode){
			case MODE_S1:
				strip.setPixelColor(0, SEQ1C);
				break;
			case MODE_S2:
				strip.setPixelColor(0, SEQ2C);
				break;
			default:
				strip.setPixelColor(0, LEDOFF);
				break;
		}
	}

	if (patternSettings[patternNum].mute) {
		stepColor = muteColors[patternNum];
	} else {
		stepColor = seqColors[patternNum];
		muteColor = muteColors[patternNum];
	}

	auto currentpage = patternPage[patternNum];
	auto pagestepstart = (currentpage * NUM_STEPKEYS);

	if (noteSelect && noteSelection) {
//		for(int j = 1; j < NUM_STEPKEYS+11; j++){
		for(int j = pagestepstart; j < (pagestepstart + NUM_STEPKEYS); j++){	// NUM_STEPKEYS or NUM_STEPS INSTEAD?>
			auto pixelpos = j - pagestepstart + 11;

			if (j < PatternLength(patternNum)){
				if (j == selectedNote){
					strip.setPixelColor(pixelpos, HALFWHITE);
				} else if (j == selectedStep){
					strip.setPixelColor(pixelpos, SEQSTEP);
				} else{
					strip.setPixelColor(pixelpos, LEDOFF);
				}
			} else {
				strip.setPixelColor(pixelpos, LEDOFF);
			}
			// Blink left/right keys for octave select indicators.
			auto color1 = blinkState ? ORANGE : WHITE;
			auto color2 = blinkState ? RBLUE : WHITE;
			strip.setPixelColor(11, color1);
			strip.setPixelColor(26, color2);
		}

	} else if (stepRecord) {
	
//		for(int j = 1; j < NUM_STEPKEYS+11; j++){
//			if (j < PatternLength(patternNum)+11){
		for(int j = pagestepstart; j < (pagestepstart + NUM_STEPKEYS); j++){	// NUM_STEPKEYS or NUM_STEPS INSTEAD?>
			auto pixelpos = j - pagestepstart + 11;

			if (j < PatternLength(patternNum)){ 
				// ONLY DO LEDS FOR THE CURRENT PAGE

				if (j == seqPos[playingPattern]){
					strip.setPixelColor(pixelpos, SEQCHASE);
				} else if (pixelpos != selectedNote){
					strip.setPixelColor(pixelpos, LEDOFF);
				}
			} else  {
				strip.setPixelColor(pixelpos, LEDOFF);
			}
		}
	} else if (patternSettings[playingPattern].solo){
//		for(int i = 0; i < NUM_STEPKEYS; i++){
//			if (i == seqPos[patternNum]){
//				if (playing){
//					strip.setPixelColor(i+11, SEQCHASE); // step chase
//				} else {
//					strip.setPixelColor(i+11, LEDOFF);  // DO WE NEED TO MARK PLAYHEAD WHEN STOPPED?
//				}
//			} else {
//				strip.setPixelColor(i+11, LEDOFF);
//			}
//		}	
	} else if (seqPages){
		// BLINK F1+F2
		auto color1 = blinkState ? FUNKONE : LEDOFF;
		auto color2 = blinkState ? FUNKTWO : LEDOFF;
		strip.setPixelColor(1, color1);
		strip.setPixelColor(2, color2);		

		// TURN OFF LEDS
		for(int j = 3; j < NUM_STEPKEYS+11; j++){  // START WITH LEDS AFTER F-KEYS
			strip.setPixelColor(j, LEDOFF);
		}
		// SHOW LEDS FOR WHAT PAGE OF SEQ PATTERN YOURE ON
		auto len = (patternSettings[patternNum].len/NUM_STEPKEYS);
		for(int h = 0; h <= len; h++){
			auto currentpage = patternPage[patternNum];
			auto color = sequencePageColors[h];
			if (h == currentpage){
				color = blinkState ? sequencePageColors[currentpage] : LEDOFF;
			}
			strip.setPixelColor(11 + h, color);
		}
		
	} else {
		for(int j = 1; j < NUM_STEPKEYS+11; j++){
			if (j < PatternLength(patternNum)+11){
				if (j == 1) {
															// NOTE SELECT / F1
					if (keyState[j] && blinkState){
						strip.setPixelColor(j, LEDOFF);
					} else {
						strip.setPixelColor(j, FUNKONE);
					}
				} else if (j == 2) {
															// PATTERN PARAMS / F2
					if (keyState[j] && blinkState){
						strip.setPixelColor(j, LEDOFF);
					} else {
						strip.setPixelColor(j, FUNKTWO);
					}

				} else if (j == patternNum+3){  			// PATTERN SELECT
					strip.setPixelColor(j, stepColor);
				} else {
					strip.setPixelColor(j, LEDOFF);
				}
			} else {
				strip.setPixelColor(j, LEDOFF);
			}
		}

		
		auto currentpage = patternPage[patternNum];
		auto pagestepstart = (currentpage * NUM_STEPKEYS);
		
		// WHAT TO DO HERE FOR MULTIPLE PAGES
		// NUM_STEPKEYS or NUM_STEPS INSTEAD?
		for(int i = pagestepstart; i < (pagestepstart + NUM_STEPKEYS); i++){
			if (i < PatternLength(patternNum)){ 
				
				// ONLY DO LEDS FOR THE CURRENT PAGE
				
				auto pixelpos = i - pagestepstart + 11;
//				Serial.println(pixelpos);
				
				int note = stepNoteP[patternNum][i].note;
				int vel = stepNoteP[patternNum][i].vel;
				int len = stepNoteP[patternNum][i].len;
				if(nsparam == 0) {
					// note
					stepColor = strip.gamma32(strip.ColorHSV((65535 / 12) * ((note - scaleRoot) % 12), 127, CLAMP((note / 12) * 24, 64, 255)));
				} else if(nsparam == 1) {
					// vel
					stepColor = strip.gamma32(strip.ColorHSV((65535 / 8) * patternNum, 127, CLAMP(vel, 64, 255)));
				} else if(nsparam == 2) {
					// note length
					stepColor = strip.gamma32(strip.ColorHSV((65535 / 8) * patternNum, 127, CLAMP(len * 48, 64, 255)));
				} else {
					stepColor = strip.gamma32(strip.ColorHSV((65535 / 8) * patternNum, 127, 255));
				}

				if(i % 4 == 0){ 					// MARK GROUPS OF 4
					if(i == seqPos[patternNum]){
						if (playing){
							strip.setPixelColor(pixelpos, SEQCHASE); // step chase
						} else if (stepNoteP[patternNum][i].trig == TRIGTYPE_PLAY){
							if (stepNoteP[patternNum][i].stepType != STEPTYPE_NONE){
								if (slowBlinkState){
									strip.setPixelColor(pixelpos, stepColor); // STEP EVENT COLOR
								}else{
									strip.setPixelColor(pixelpos, muteColor); // STEP EVENT COLOR
								}
							} else {
								strip.setPixelColor(pixelpos, stepColor); // STEP ON COLOR
							}
						} else if (stepNoteP[patternNum][i].trig == TRIGTYPE_MUTE){
							strip.setPixelColor(pixelpos, SEQMARKER);
						}

					} else if (stepNoteP[patternNum][i].trig == TRIGTYPE_PLAY){
						if (stepNoteP[patternNum][i].stepType != STEPTYPE_NONE){
							if (slowBlinkState){
								strip.setPixelColor(pixelpos, stepColor); // STEP EVENT COLOR
							}else{
								strip.setPixelColor(pixelpos, muteColor); // STEP EVENT COLOR
							}
						} else {
							strip.setPixelColor(pixelpos, stepColor); // STEP ON COLOR
						}
					} else if (stepNoteP[patternNum][i].trig == TRIGTYPE_MUTE){
						strip.setPixelColor(pixelpos, SEQMARKER);
					}

				} else if (i == seqPos[patternNum]){ 		// STEP CHASE
					if (playing){
						strip.setPixelColor(pixelpos, SEQCHASE);

					} else if (stepNoteP[patternNum][i].trig == TRIGTYPE_PLAY){
						if (stepNoteP[patternNum][i].stepType != STEPTYPE_NONE){
							if (slowBlinkState){
								strip.setPixelColor(pixelpos, stepColor); // STEP EVENT COLOR
							}else{
								strip.setPixelColor(pixelpos, muteColor); // STEP EVENT COLOR
							}
						} else {
							strip.setPixelColor(pixelpos, stepColor); // STEP ON COLOR
						}
					} else if (stepNoteP[patternNum][i].trig == TRIGTYPE_MUTE){
						strip.setPixelColor(pixelpos, LEDOFF);  // DO WE NEED TO MARK PLAYHEAD WHEN STOPPED?
					}

				} else if (stepNoteP[patternNum][i].trig == TRIGTYPE_PLAY){
					if (stepNoteP[patternNum][i].stepType != STEPTYPE_NONE){
						if (slowBlinkState){
							strip.setPixelColor(pixelpos, stepColor); // STEP EVENT COLOR
						}else{
							strip.setPixelColor(pixelpos, muteColor); // STEP EVENT COLOR
						}
					} else {
						strip.setPixelColor(pixelpos, stepColor); // STEP ON COLOR
					}

				} else if (stepNoteP[patternNum][i].trig == TRIGTYPE_MUTE){
					strip.setPixelColor(pixelpos, LEDOFF);
				}

			}
		}
	}
	dirtyPixels = true;
//	strip.show();
}
// ####### END LEDS




// ####### DISPLAY FUNCTIONS #######

void dispGridBoxes(){
	display.fillRect(0, 0, gridw, 10, WHITE);
	display.drawFastVLine(gridw/4, 0, gridh, INVERSE);
	display.drawFastVLine(gridw/2, 0, gridh, INVERSE);
	display.drawFastVLine(gridw*0.75, 0, gridh, INVERSE);
}
void invertColor(bool flip){
	if (flip) {
		u8g2_display.setForegroundColor(BLACK);
		u8g2_display.setBackgroundColor(WHITE);
	} else {
		u8g2_display.setForegroundColor(WHITE);
		u8g2_display.setBackgroundColor(BLACK);
	}
}

void dispParamLabel(const ui_param* param, int16_t n){
	u8g2centerText(param->getLabel != NULL ? param->getLabel() : param->label, (n*32) + 1, hline-2, 32, 10);
}

void dispValBox(const ui_param* param, int16_t n, bool selected){			// n is box 0-3
	if(selected) {
		u8g2_display.setForegroundColor(BLACK);
		u8g2_display.setBackgroundColor(WHITE);
	} else {
		u8g2_display.setForegroundColor(WHITE);
		u8g2_display.setBackgroundColor(BLACK);
	}
	param->drawValue(param->getValue != NULL ? param->getValue() : *param->valuePtr, n);
}

void dispSymbBox(const char* v, int16_t n, bool inv){			// n is box 0-3
	invertColor(inv);
	u8g2centerText(v, n*32, hline*2+3, 32, uiDrawTextY);
}

void dispGenericMode(int submode, int selected){
	int dispPage = selected / 4;
	int start = dispPage * 4;
	int numParams = 0;
	const ui_param* params = NULL;
	int selectedOnPage = selected % 4;

	switch(submode){
		case SUBMODE_MIDI:
			params = midi_params;
			numParams = ARRAYLEN(midi_params);
			break;
		case SUBMODE_SEQ:
		case SUBMODE_PATTPARAMS:
			params = seq_pattern_params;
			numParams = ARRAYLEN(seq_pattern_params);
			break;
		case SUBMODE_NOTESEL:
			params = seq_note_select_params;
			numParams = ARRAYLEN(seq_note_select_params);
			break;
		case SUBMODE_STEPREC:
			params = seq_step_record_params;
			numParams = ARRAYLEN(seq_step_record_params);
			break;
		default:
			params = NULL;
			numParams = 0;
			break;
	}

	int numPages = (numParams + 3) / 4; // integer ceiling
	int numParamsOnPage = MIN(numParams - start, 4);

	u8g2_display.setFontMode(1);
	u8g2_display.setFont(FONT_LABELS);
	u8g2_display.setCursor(0, 0);
	dispGridBoxes();

	// labels
	u8g2_display.setForegroundColor(BLACK);
	u8g2_display.setBackgroundColor(WHITE);

	for (int j = 0; j < 4; j++){
		if(j < numParamsOnPage) {
			dispParamLabel(&params[start+j], j);
		}
	}

	// value text formatting
	u8g2_display.setFontMode(1);
	u8g2_display.setFont(FONT_VALUES);
	u8g2_display.setForegroundColor(WHITE);
	u8g2_display.setBackgroundColor(BLACK);

	bool edit = uiEditParam || auxShift || seqStepHold != -1;


	// draw white box on selected item
	if(edit) {
		display.fillRect(selectedOnPage*32+2, 11, 29, 18, WHITE);
	} else {
		display.fillRect(selectedOnPage*32+2, 9+17, 29, 3, WHITE);
	}

	// ValueBoxes
	int highlight = false;
	for (int j = 0; j < 4; j++){
		if (edit && j == selectedOnPage) {
			highlight = true;
		}else{
			highlight = false;
		}
		if(j < numParamsOnPage) {
			dispValBox(&params[start+j], j, highlight);
		}
	}
	for (int k = 0; k < numPages; k++){
		dispPageIndicators(k, dispPage == k);
	}
}

void dispPageIndicators(int page, bool selected){
	if (selected){
		display.fillRect(43 + (page * 12), 30, 6, 2, WHITE);
	} else {
		display.fillRect(43 + (page * 12), 31, 6, 1, WHITE);
	}
}

void dispInfoDialog(){
	for (int key=0; key < NUM_DIALOGS; key++){
		if (infoDialog[key].state){
			dialogTimeout = 0;
			display.clearDisplay();
			u8g2_display.setFontMode(1);
			u8g2_display.setFont(FONT_TENFAT);
			u8g2_display.setForegroundColor(WHITE);
			u8g2_display.setBackgroundColor(BLACK);

			u8g2centerText(infoDialog[key].text, 0, 10, 128, 32);
			infoDialog[key].state = false;
		}
	}
}

void setMessage(int time) {
	Serial.println(messageText);
	messageTextTimer = 1000;
	dirtyDisplay = true;
}

void displayMessage() {
	display.fillRect(0, 0, 128, 32, BLACK);
	u8g2_display.setFontMode(1);
	u8g2_display.setFont(FONT_VALUES);
	u8g2_display.setForegroundColor(WHITE);
	u8g2_display.setBackgroundColor(BLACK);
	u8g2centerText(messageText, 0, 10, 128, 32);
}

void dispMode(){
	// labels formatting
	u8g2_display.setFontMode(1);
	u8g2_display.setFont(FONT_BIG);
	u8g2_display.setCursor(0, 0);

	u8g2_display.setForegroundColor(WHITE);
	u8g2_display.setBackgroundColor(BLACK);

	const char* displaymode = "";
	if (newmode != omxMode && mode_select) {
		displaymode = modes[newmode]; // display.print(modes[newmode]);
	} else if (mode_select) {
		displaymode = modes[omxMode]; // display.print(modes[mode]);
	}
	u8g2centerText(displaymode, 86, 20, 44, 32);
}


// ############## MAIN LOOP ##############

void loop() {
	customKeypad.tick();
	clksTimer = 0;

	Micros now = micros();
	Micros passed = now - lastProcessTime;
	lastProcessTime = now;

	if (passed > 0) {
		if (playing){
			advanceClock(passed);
			advanceSteps(passed);
		}
	}
	doStep();

	if(arp) {
		doArpStep(passed);
	}

	// DISPLAY SETUP
	display.clearDisplay();

	// ############### POTS ###############
	//
	readPotentimeters();


	// ############### ENCODER ###############
	//
	if(pageWrapDelay > 0) {
		pageWrapDelay--;
	}
	if(pageChangeDelay > 0) {
		pageChangeDelay--;
	}
	auto u = myEncoder.update();
	if (u.active()) {
		auto amt = u.accel(5); // where 5 is the acceleration factor if you want it, 0 if you don't)
//    	Serial.println(u.dir() < 0 ? "ccw " : "cw ");
//    	Serial.println(amt);

		int* param = NULL;
		const ui_param* params = NULL;
		int numParams = 0;

		// Change Mode
		if (mode_select) {
			// set mode
//			int modesize = NUM_OMX_MODES;
			newmode = (OMXMode)constrain(newmode + amt, 0, NUM_OMX_MODES - 1);
			dispMode();
			dirtyDisplayTimer = displayRefreshRate+1;
			dirtyDisplay = true;
		} else {
			switch(omxMode) {
				case MODE_OM: // Organelle Mother
					// CHANGE PAGE
					if (miparam == 0) {
						if(u.dir() < 0){									// if turn ccw
							MM::sendControlChange(CC_OM2,0,midiChannel);
						} else if (u.dir() > 0){							// if turn cw
							MM::sendControlChange(CC_OM2,127,midiChannel);
						}
					}
					dirtyDisplay = true;
					// FALL THROUGH
				case MODE_MIDI: // MIDI
					if (scaleSelectHold) {
						scalePattern = WRAP(scalePattern + amt, getNumScales());
						setScale(scaleRoot, scalePattern);
						for(int n = 1; n < 27; n++) {
							strip.setPixelColor(n, getDefaultColor(n));
						}
						strip.show();
						// show the name of the scale for a moment
						snprintf(messageText, MESSAGE_TEXT_LEN, "%s %s", noteNames[scaleRoot], scaleNames[scalePattern]);
						setMessage(1000);
						dirtyPixels = true;
						break;
					}
					param = &miparam;
					params = midi_params;
					numParams = ARRAYLEN(midi_params);
					break;
				case MODE_S1: // SEQ 1
					// FALL THROUGH
				case MODE_S2: // SEQ 2
					if(seqStepHold != -1) {
						// turning encoder while holding seq step changes note for that step
						//stepNoteP[playingPattern][seqStepHold].note = constrain(stepNoteP[playingPattern][seqStepHold].note + amt, 0, 127);
						param = &nsparam;
						params = seq_note_select_params;
						numParams = ARRAYLEN(seq_note_select_params);
						dirtyDisplay = true;
					} else if (noteSelect) {
						param = &nsparam;
						params = seq_note_select_params;
						numParams = ARRAYLEN(seq_note_select_params);
					} else if(stepRecord) {
						param = &srparam;
						params = seq_step_record_params;
						numParams = ARRAYLEN(seq_step_record_params);
					} else {
						param = &sqparam;
						params = seq_pattern_params;
						numParams = ARRAYLEN(seq_pattern_params);
					}
					break;
				default:
					break;
			}
		}

		if(scaleSelectHold == false) {
			if(uiEditParam || auxShift || seqStepHold != -1) {
			// edit current param
				if(*param < numParams) {
					if(params[*param].readOnly == false) {
						int newValue = 0;
						if(params[*param].getValue != NULL) {
							newValue = constrain(params[*param].getValue() + amt, params[*param].min, params[*param].max);
						} else {
							newValue = constrain(*params[*param].valuePtr + amt, params[*param].min, params[*param].max);
						}
						if(params[*param].onChange != NULL) {
							params[*param].onChange(params[*param].valuePtr, newValue, amt);
						} else {
							*params[*param].valuePtr = newValue;
						}
						dirtyDisplay = true;
					}
				}
			} else {
				// scroll through params
				if(pageChangeDelay == 0) {
					if(pageWrapDelay > 0) {
						*param = constrain(*param + SGN(amt), 0, numParams-1);
					} else {
						*param = WRAP(*param + SGN(amt), numParams);
					}
					if(SGN(amt) > 0 && *param % 4 == 0) {
						// landed on a new page, delay
						pageChangeDelay = 75;
					}
					if(SGN(amt) < 0 && *param % 4 == 3) {
						// landed on a new page, delay
						pageChangeDelay = 75;
					}
					if(*param == numParams-1 && SGN(amt) > 0) {
						// hit last param, add a delay
						pageWrapDelay = 100;
						pageChangeDelay = 0;
					} else if(*param == 1 && SGN(amt) < 0) {
						pageWrapDelay = 100;
						pageChangeDelay = 0;
					}
					dirtyDisplay = true;
				}
			}
		}
	}
	// END ENCODER

	// ############### ENCODER BUTTON ###############
	//
	auto s = encButton.update();
	switch (s) {
		// SHORT PRESS
		case Button::Down: //Serial.println("Button down");

			// what page are we on?
			if (newmode != omxMode && mode_select) {
				omxMode = newmode;
				seqStop();
				setAllLEDS(0,0,0);
				mode_select = false;
				dispMode();
			} else if (mode_select){
				mode_select = false;
			}

			uiEditParam = !uiEditParam;

			dirtyDisplay = true;
			break;

		// LONG PRESS
		case Button::DownLong: //Serial.println("Button downlong");
			if (stepRecord) {
				resetPatternDefaults(playingPattern);
				clearedFlag = true;
			} else {
				mode_select = true;
				newmode = omxMode;
				dispMode();
			}
			dirtyDisplay = true;

			break;
		case Button::Up: //Serial.println("Button up");
			if(omxMode == MODE_OM) {
//				MM::sendControlChange(CC_OM1,0,midiChannel);
			}
			break;
		case Button::UpLong: //Serial.println("Button uplong");
			break;
		default:
			break;
	}
	// END ENCODER BUTTON


	// ############### KEY HANDLING ###############

	while(customKeypad.available()){
		keypadEvent e = customKeypad.read();
		int thisKey = e.bit.KEY;
		int keyPos = thisKey - 11;
		int seqKey = keyPos + (patternPage[playingPattern] * NUM_STEPKEYS);

		if (e.bit.EVENT == KEY_JUST_PRESSED){
			keyState[thisKey] = true;
			keyPressTime[thisKey] = 0;
		}

		if (e.bit.EVENT == KEY_JUST_PRESSED && thisKey == 0 && mode_select) {
			// temp - save whenever the 0 key is pressed in encoder edit mode
			saveToStorage();
			//	Serial.println("EEPROM saved");
		}


		if (e.bit.EVENT == KEY_JUST_PRESSED && thisKey == 0) {
			auxShift = true;
		} else if (e.bit.EVENT == KEY_JUST_RELEASED && thisKey == 0) {
			if(auxShift) {
				auxShift = false;
				dirtyDisplay = true;
			}
		}

		switch(omxMode) {
			case MODE_OM: // Organelle
				// Fall Through

			case MODE_MIDI: // MIDI CONTROLLER

				// ### KEY PRESS EVENTS
				if (e.bit.EVENT == KEY_JUST_PRESSED && thisKey != 0) {
					//Serial.println(" pressed");
					
					if (auxShift){
						if (thisKey == 11 || thisKey == 12 || thisKey == 1 || thisKey == 2) {
							if (thisKey == 11 || thisKey == 12){
								int amt = thisKey == 11 ? -1 : 1;
								newoctave = constrain(octave + amt, minOctave, maxOctave);
								if (newoctave != octave){
									octave = newoctave;
								}
							} else if (thisKey == 1 || thisKey == 2) {
								// allow changing param selection
								int chng = thisKey == 1 ? -1 : 1;
								miparam = WRAP(miparam + chng, ARRAYLEN(midi_params));
							}
						} else if (thisKey == 3) {
							arp = !arp;
							for(int i = 0; i < ARP_SEQ_LEN; i++) {
								arpSeq[i] = -1;
								arpIndex = -1;
								arpTimer = 0;
								arpSwingIndex = 0;
							}
							if(arpLastNote != -1) {
								rawNoteOff(arpLastNote, arpLastChannel);
							}
						} else if (thisKey == 4) {
							if(arp) {
								arpLatch = !arpLatch;
								if(arpLatch == false) {
									for(int i = 0; i < ARP_SEQ_LEN; i++) {
										arpSeq[i] = -1;
										arpIndex = -1;
										arpTimer = 0;
										arpSwingIndex = 0;
									}
									if(arpLastNote != -1) {
										rawNoteOff(arpLastNote, arpLastChannel);
									}
								}
							}
						} else if(thisKey == 26) {
							scalePattern = -1; // disable scales
							setScale(0, -1);
							for(int n = 1; n < 27; n++) {
								strip.setPixelColor(n, getDefaultColor(n));
							}
							strip.show();
							dirtyPixels = true;
						} else if((thisKey >= 6 && thisKey <= 10) || thisKey >= 19) {
							int oldScaleRoot = scaleRoot;
							scaleRoot = WRAP(notes[thisKey], 12);
							scaleSelectHold = true;
							if(scaleRoot == oldScaleRoot) {
								// selecting same root again cycles through scales
								scalePattern = (scalePattern + 1) % getNumScales();
							} else {
								// new root jumps to first scale
								scalePattern = 0;
							}
							setScale(scaleRoot, scalePattern);
							for(int n = 1; n < 27; n++) {
								strip.setPixelColor(n, getDefaultColor(n));
							}
							strip.show();
							// show the name of the scale for a moment
							snprintf(messageText, MESSAGE_TEXT_LEN, "%s %s", noteNames[scaleRoot], scaleNames[scalePattern]);
							setMessage(1000);
							dirtyPixels = true;
						}
					} else {
						midiNoteOn(thisKey, midiVelocity, midiChannel);
					}
				} else if(e.bit.EVENT == KEY_JUST_RELEASED && thisKey != 0) {
					//Serial.println(" released");
					midiNoteOff(thisKey, midiChannel);
					scaleSelectHold = false;
				}

				// AUX KEY
				if (e.bit.EVENT == KEY_JUST_PRESSED && thisKey == 0) {
					// Hard coded Organelle stuff
//					MM::sendControlChange(CC_AUX, 100, midiChannel);

					midiAUX = true;
					dirtyDisplay = true;

//					if (midiAUX) {
//						// STOP CLOCK
//						Serial.println("stop clock");
//					} else {
//						// START CLOCK
//						Serial.println("start clock");
//					}
//					midiAUX = !midiAUX;


				} else if (e.bit.EVENT == KEY_JUST_RELEASED && thisKey == 0) {
					// Hard coded Organelle stuff
//					MM::sendControlChange(CC_AUX, 0, midiChannel);
					if (midiAUX) { 
						midiAUX = false; 
						dirtyDisplay = true;
					}
					// turn off leds
					strip.setPixelColor(0, LEDOFF);
					strip.setPixelColor(1, getDefaultColor(1));
					strip.setPixelColor(2, getDefaultColor(2));
					strip.setPixelColor(3, getDefaultColor(3));
					strip.setPixelColor(4, getDefaultColor(4));
					strip.setPixelColor(11, getDefaultColor(11));
					strip.setPixelColor(12, getDefaultColor(12));
				}
				break;

			case MODE_S1: // SEQUENCER 1
				// fall through

			case MODE_S2: // SEQUENCER 2
				// Sequencer row keys

				// ### KEY PRESS EVENTS

				if (e.bit.EVENT == KEY_JUST_PRESSED && thisKey != 0) {
					// set key timer to zero

					if (seqStepHold != -1) {
						if(thisKey == 1 || thisKey == 2) {
							int chng = thisKey == 1 ? -1 : 1;
							nsparam = WRAP(nsparam + chng, ARRAYLEN(seq_note_select_params));
							dirtyDisplay = true;
						}
					// NOTE SELECT
					} else if (noteSelect){
						if (noteSelection) {		// SET NOTE		
							// left and right keys change the octave
							if (thisKey == 11 || thisKey == 26) {
								int amt = thisKey == 11 ? -1 : 1;
								newoctave = constrain(octave + amt, minOctave, maxOctave);
								if (newoctave != octave){
									octave = newoctave;
								}
							// otherwise select the note
							} else {
								stepSelect = false;
								selectedNote = thisKey;
								int adjnote = notes[thisKey] + (octave * 12);
								stepNoteP[playingPattern][selectedStep].note = adjnote;
								if (!playing){
									seqNoteOn(thisKey, defaultVelocity, playingPattern);
								}
							}
							// see RELEASE events for more
							dirtyDisplay = true;

						} else if (thisKey == 1) {

						} else if (thisKey == 2) {

						} else if (thisKey > 2 && thisKey < 11) { // Pattern select keys
							playingPattern = thisKey-3;
							dirtyDisplay = true;

						} else if ( thisKey > 10 ) {
							selectedStep = seqKey; // was keyPos // set noteSelection to this step
							stepSelect = true;
							noteSelection = true;
							dirtyDisplay = true;
						}
					// STEP RECORD
					} else if (stepRecord) {
						selectedNote = thisKey;
						selectedStep = seqPos[playingPattern];

						int adjnote = notes[thisKey] + (octave * 12);
						stepNoteP[playingPattern][selectedStep].note = adjnote;

						if (!playing){
							seqNoteOn(thisKey, defaultVelocity, playingPattern);
						} // see RELEASE events for more
						stepDirty = true;
						dirtyDisplay = true;

					// MIDI SOLO
					} else if (patternSettings[playingPattern].solo) {
						midiNoteOn(thisKey, defaultVelocity, patternSettings[playingPattern].channel+1);

					// REGULAR SEQ MODE
					} else {
						if (keyState[1] && keyState[2] && thisKey > 2 && thisKey < 11) {
							clearPattern(thisKey-3);
							snprintf(messageText, MESSAGE_TEXT_LEN, "cleared P%d", (thisKey-3)+1);
							setMessage(1000);
						} else if(keyState[1] && thisKey > 2 && thisKey < 11) {
							if(auxShift) {
								// step record
								playingPattern = thisKey-3;
								seqPos[playingPattern] = 0;
								stepRecord = true;
								dirtyDisplay = true;
							} else {
								// copy
								copyPattern(thisKey-3);
								snprintf(messageText, MESSAGE_TEXT_LEN, "copied P%d", (thisKey-3)+1);
								setMessage(1000);
							}
						} else if(keyState[2] && thisKey > 2 && thisKey < 11) {
							if(auxShift) {
								// toggle mute
								patternSettings[thisKey-3].mute = !patternSettings[thisKey-3].mute;
							} else {
								pastePattern(thisKey-3);
								snprintf(messageText, MESSAGE_TEXT_LEN, "pasted to P%d", (thisKey-3)+1);
								setMessage(1000);
							}
						}

						if (thisKey == 1) {	
//							seqResetFlag = true;					// RESET ALL SEQUENCES TO FIRST/LAST STEP
																	// MOVED DOWN TO AUX KEY

						} else if (thisKey == 2) { 					// CHANGE PATTERN DIRECTION
//							patternSettings[playingPattern].reverse = !patternSettings[playingPattern].reverse;

						} else if (thisKey > 2 && thisKey < 11) {
							// BLACK KEYS
							playingPattern = thisKey-3;
							dirtyDisplay = true;
						} else if (thisKey > 10) {
							// SEQUENCE 1-16 STEP KEYS
							if (keyState[1] && keyState[2]) {		// F1+F2 HOLD
								if (!stepRecord){ // IGNORE LONG PRESSES IN STEP RECORD and Pattern Params
									if (keyPos <= getPatternPage(patternSettings[playingPattern].len) ){
										patternPage[playingPattern] = keyPos;
									}
								}
							} else if (keyState[1]) {		// F1 HOLD
									if (!stepRecord){ 		// IGNORE LONG PRESSES IN STEP RECORD and Pattern Params
										selectedStep = thisKey - 11; // set noteSelection to this step
										noteSelect = true;
										stepSelect = true;
										noteSelection = true;
										dirtyDisplay = true;
										// re-toggle the key you just held
										if ( stepNoteP[playingPattern][selectedStep].trig == TRIGTYPE_PLAY || stepNoteP[playingPattern][selectedStep].trig == TRIGTYPE_MUTE ) {
											stepNoteP[playingPattern][selectedStep].trig = ( stepNoteP[playingPattern][selectedStep].trig == TRIGTYPE_PLAY ) ? TRIGTYPE_MUTE : TRIGTYPE_PLAY;
										}
									}
							} else if (keyState[2]) {		// F2 HOLD

							} else {
								// TOGGLE STEP ON/OFF
	//							if ( stepNoteP[playingPattern][keyPos].stepType == STEPTYPE_PLAY || stepNoteP[playingPattern][keyPos].stepType == STEPTYPE_MUTE ) {
	//								stepNoteP[playingPattern][keyPos].stepType = ( stepNoteP[playingPattern][keyPos].stepType == STEPTYPE_PLAY ) ? STEPTYPE_MUTE : STEPTYPE_PLAY;
	//							}
								if ( stepNoteP[playingPattern][seqKey].trig == TRIGTYPE_PLAY) {
									stepNoteP[playingPattern][seqKey].trig = TRIGTYPE_MUTE;
								} else if(stepNoteP[playingPattern][seqKey].trig == TRIGTYPE_MUTE) {
									stepNoteP[playingPattern][seqKey].trig = TRIGTYPE_PLAY;
								}
								seqStepHold = seqKey;
								selectedStep = seqKey;
								dirtyDisplay = true;
							}
						}
					}
				}

				if(seqStepHold != -1 && e.bit.EVENT == KEY_JUST_RELEASED && thisKey != 0) {
					if(seqStepHold == seqKey) {
						seqStepHold = -1;
						dirtyDisplay = true;
					}
				}

				// ### KEY RELEASE EVENTS

				if (e.bit.EVENT == KEY_JUST_RELEASED && thisKey != 0) {
					// MIDI SOLO
					if (patternSettings[playingPattern].solo) {
						midiNoteOff(thisKey, patternSettings[playingPattern].channel+1);
					}
				}

				if (e.bit.EVENT == KEY_JUST_RELEASED && thisKey != 0 && (noteSelection || stepRecord) && selectedNote > 0) {
					if (!playing){
						seqNoteOff(thisKey, playingPattern);
					}
					if (stepRecord && stepDirty) {
						step_ahead(playingPattern);
						stepDirty = false;
					}
				}

				// AUX KEY PRESS EVENTS

				if (e.bit.EVENT == KEY_JUST_PRESSED && thisKey == 0) {
					if (keyState[1] || keyState[2]) { 				// CHECK keyState[] FOR LONG PRESS OF FUNC KEYS
						if (keyState[1]) {
							seqResetFlag = true;					// RESET ALL SEQUENCES TO FIRST/LAST STEP
							infoDialog[RESET].state = true; // reset flag

						} else if (keyState[2]) { 					// CHANGE PATTERN DIRECTION
							patternSettings[playingPattern].reverse = !patternSettings[playingPattern].reverse;
							if (patternSettings[playingPattern].reverse) {
								infoDialog[REV].state = true; // rev direction flag
							} else{
								infoDialog[FWD].state = true; // fwd direction flag
							}
						}
						dirtyDisplay = true;
					}

				// AUX KEY RELEASE EVENTS

				} else if (e.bit.EVENT == KEY_JUST_RELEASED && thisKey == 0) {
					if(keyPressTime[thisKey] <= shortPressInterval) {
						if (noteSelect){
							if (noteSelection){
								selectedStep = 0;
								selectedNote = 0;
							} else {

							}
							noteSelection = false;
							noteSelect = false;
							dirtyDisplay = true;
						} else if (stepRecord){
							stepRecord = !stepRecord;
							dirtyDisplay = true;
						} else if (seqPages){
							seqPages = false;
						} else {
							if (playing){
								// stop transport
								playing = 0;
								allNotesOff();
		//							Serial.println("stop transport");
								seqStop();
							} else {
								// start transport
		//							Serial.println("start transport");
								seqStart();
							}
						}
					}
				}

//				strip.show();
				break;

			default:
				break;
		}
		// END MODE SWITCH

		if (!keyState[1] && !keyState[2]) {	
			seqPages = false;
		}

		if (e.bit.EVENT == KEY_JUST_RELEASED){
			keyState[thisKey] = false;
		}

	} // END KEYS WHILE


	// ### LONG KEY SWITCH PRESS
	for (int j=0; j<LED_COUNT; j++){
		if (keyState[j]){
			if (keyPressTime[j] >= longPressInterval && keyPressTime[j] < 9999){

				// DO LONG PRESS THINGS
				switch (omxMode){
					case MODE_S1:
						// fall through
					case MODE_S2:
						if (!patternSettings[playingPattern].solo){
							if (keyState[1] && keyState[2]) {	
								seqPages = true;
								
							} else if (!keyState[1] && !keyState[2]) {  // SKIP LONG PRESS IF FUNC KEYS ARE ALREDY HELD
								/* else if (j > 10){
									if (!stepRecord && !patternParams){ 		// IGNORE LONG PRESSES IN STEP RECORD and Pattern Params
										selectedStep = (j - 11) + (patternPage[playingPattern] * NUM_STEPKEYS); // set noteSelection to this step
										noteSelect = true;
										stepSelect = true;
										noteSelection = true;
										dirtyDisplay = true;
										// re-toggle the key you just held
//										if ( stepNoteP[playingPattern][selectedStep].stepType == STEPTYPE_PLAY || stepNoteP[playingPattern][selectedStep].stepType == STEPTYPE_MUTE ) {
//											stepNoteP[playingPattern][selectedStep].stepType = ( stepNoteP[playingPattern][selectedStep].stepType == STEPTYPE_PLAY ) ? STEPTYPE_MUTE : STEPTYPE_PLAY;
//										}
										if ( stepNoteP[playingPattern][selectedStep].trig == TRIGTYPE_PLAY || stepNoteP[playingPattern][selectedStep].trig == TRIGTYPE_MUTE ) {
											stepNoteP[playingPattern][selectedStep].trig = ( stepNoteP[playingPattern][selectedStep].trig == TRIGTYPE_PLAY ) ? TRIGTYPE_MUTE : TRIGTYPE_PLAY;
										}
									}
								}*/
							}
						}
						break;
					default:
						break;

				}
				keyPressTime[j] = 9999;
			}
		}
	}

	// ############### MODES DISPLAY  ##############

	if(!mode_select) {
		switch(omxMode){
			case MODE_OM: 						// ############## ORGANELLE MODE
				// FALL THROUGH

			case MODE_MIDI:							// ############## MIDI KEYBOARD
				//playingPattern = 0; 		// DEFAULT MIDI MODE TO THE FIRST PATTERN SLOT
				midi_leds();				// SHOW LEDS

				if (dirtyDisplay){			// DISPLAY
					dispGenericMode(SUBMODE_MIDI, miparam);
				}
				break;

			case MODE_S1: 						// ############## SEQUENCER 1
				// FALL THROUGH
			case MODE_S2: 						// ############## SEQUENCER 2
				if (dialogTimeout > dialogDuration && dialogTimeout < dialogDuration + 20) {
					dirtyDisplay = true;
				}
				// MIDI SOLO
				if (patternSettings[playingPattern].solo) {
					midi_leds();
				}

				if (dirtyDisplay){			// DISPLAY
					if (noteSelect || seqStepHold != -1) {
						dispGenericMode(SUBMODE_NOTESEL, nsparam);
						dispInfoDialog();
					} else if (stepRecord) {
						dispGenericMode(SUBMODE_STEPREC, srparam);
						dispInfoDialog();
					} else {
						dispGenericMode(SUBMODE_SEQ, sqparam);
						dispInfoDialog();
					}
				}
				break;
			default:
				break;
		}
	}

	if(messageTextTimer > 0) {
		displayMessage();
		messageTextTimer--;
		if(messageTextTimer == 0) {
			dirtyDisplay = true;
		}
	}

	// DISPLAY at end of loop

	if (dirtyDisplay){
		if (dirtyDisplayTimer > displayRefreshRate) {
			display.display();
			dirtyDisplay = false;
			dirtyDisplayTimer = 0;
		}
	}

	// are pixels dirty
	if (dirtyPixels){
		strip.show();
		dirtyPixels = false;
	}

	while (MM::usbMidiRead()) {
		// ignore incoming messages
	}
	while (MM::midiRead()) {
		// ignore incoming messages
	}

} // ######## END MAIN LOOP ########


// ####### SEQENCER FUNCTIONS

void step_ahead(int patternNum) {
	// step each pattern ahead one place
	for (int j=0; j<8; j++){
		if (patternSettings[j].reverse) {
			seqPos[j]--;
			auto_reset(j); // determine whether to reset or not based on param settings
//			if (seqPos[j] < 0)
//				seqPos[j] = PatternLength(j)-1;
		} else {
			seqPos[j]++;
			auto_reset(j); // determine whether to reset or not based on param settings
//			if (seqPos[j] >= PatternLength(j))
//				seqPos[j] = 0;
		}
	}
}
void step_back(int patternNum) {
	// step each pattern ahead one place
	for (int j=0; j<8; j++){
		if (patternSettings[j].reverse) {
			seqPos[j]++;
			auto_reset(j); // determine whether to reset or not based on param settings
		} else {
			seqPos[j]--;
// 			auto_reset(j);
			if (seqPos[j] < 0)
				seqPos[j] = PatternLength(j)-1;
		}
	}
}

void new_step_ahead(int patternNum) {
	// step each pattern ahead one place
		if (patternSettings[patternNum].reverse) {
			seqPos[patternNum]--;
			auto_reset(patternNum); // determine whether to reset or not based on param settings
		} else {
			seqPos[patternNum]++;
			auto_reset(patternNum); // determine whether to reset or not based on param settings
		}
}

void auto_reset(int p){
	// should be conditioned on whether we're in S2!!
	if ( seqPos[p] >= PatternLength(p) ||
		 (patternSettings[p].autoreset && (patternSettings[p].autoresetstep > (patternSettings[p].startstep) ) && (seqPos[p] >= patternSettings[p].autoresetstep)) ||
		 (patternSettings[p].autoreset && (patternSettings[p].autoresetstep == 0 ) && (seqPos[p] >= patternSettings[p].rndstep)) ||
		 (patternSettings[p].reverse && (seqPos[p] < 0)) || // normal reverse reset
		 (patternSettings[p].reverse && patternSettings[p].autoreset && (seqPos[p] < patternSettings[p].startstep )) // ||
		 //(patternSettings[p].reverse && patternSettings[p].autoreset && (patternSettings[p].autoresetstep == 0 ) && (seqPos[p] < patternSettings[p].rndstep))
		 ) {

		if (patternSettings[p].reverse) {
			if (patternSettings[p].autoreset){
				if (patternSettings[p].autoresetstep == 0){
					seqPos[p] = patternSettings[p].rndstep-1;
				}else{
					seqPos[p] = patternSettings[p].autoresetstep-1; // resets pattern in REV
				}
			} else {
				seqPos[p] = (PatternLength(p)-patternSettings[p].startstep)-1;
			}

		} else {
			seqPos[p] = (patternSettings[p].startstep); // resets pattern in FWD
		}
		if (patternSettings[p].autoresetfreq == patternSettings[p].current_cycle){ // reset cycle logic
			if (probResult(patternSettings[p].autoresetprob)){
				// chance of doing autoreset
				patternSettings[p].autoreset = true;
			} else {
				patternSettings[p].autoreset = false;
			}
			patternSettings[p].current_cycle = 1; // reset cycle to start new iteration
		} else {
			patternSettings[p].autoreset = false;
			patternSettings[p].current_cycle++; // advance to next cycle
		}
		patternSettings[p].rndstep = (rand() % PatternLength(p)) + 1; // randomly choose step for next cycle
	}
	patternPage[p] = getPatternPage(seqPos[p]); // FOLLOW MODE FOR SEQ PAGE
	
// return ()
}

bool probResult(int probSetting){
//	int tempProb = (rand() % probSetting);
	if (probSetting == 0){
		return false;
	}
	if((rand() % 100) < probSetting){ // assumes probSetting is a range 0-100
		return true;
	} else {
		return false;
	}
 }

bool evaluate_AB(int condition, int patternNum) {
	bool shouldTrigger = false;;

	loopCount[patternNum][seqPos[patternNum]]++;

	int a = trigConditionsAB[condition][0];
	int b = trigConditionsAB[condition][1];

//Serial.print (patternNum);
//Serial.print ("/");
//Serial.print (seqPos[patternNum]);
//Serial.print (" ");
//Serial.print (loopCount[patternNum][seqPos[patternNum]]);
//Serial.print (" ");
//Serial.print (a);
//Serial.print (":");
//Serial.print (b);
//Serial.print (" ");

	if (loopCount[patternNum][seqPos[patternNum]] == a){
		shouldTrigger = true;
	} else {
		shouldTrigger = false;
	}
	if (loopCount[patternNum][seqPos[patternNum]] >= b){
		loopCount[patternNum][seqPos[patternNum]] = 0;
	}
	return shouldTrigger;
}

void changeStepType(int amount){
	auto tempType = stepNoteP[playingPattern][selectedStep].stepType + amount;

	// this is fucking hacky to increment the enum for stepType
	switch(tempType){
		case 0:
			stepNoteP[playingPattern][selectedStep].stepType = STEPTYPE_NONE;
			break;
		case 1:
			stepNoteP[playingPattern][selectedStep].stepType = STEPTYPE_RESTART;
			break;
		case 2:
			stepNoteP[playingPattern][selectedStep].stepType = STEPTYPE_FWD;
			break;
		case 3:
			stepNoteP[playingPattern][selectedStep].stepType = STEPTYPE_REV;
			break;
		case 4:
			stepNoteP[playingPattern][selectedStep].stepType = STEPTYPE_PONG;
			break;
		case 5:
			stepNoteP[playingPattern][selectedStep].stepType = STEPTYPE_RANDSTEP;
			break;
		case 6:
			stepNoteP[playingPattern][selectedStep].stepType = STEPTYPE_RAND;
			break;
		default:
			break;
	}
}
void step_on(int patternNum){
//	Serial.print(patternNum);
//	Serial.println(" step on");
//	playNote(playingPattern);
}

void step_off(int patternNum, int position){
	lastNote[patternNum][position] = 0;

//	Serial.print(seqPos[patternNum]);
//	Serial.println(" step off");
//	analogWrite(CVPITCH_PIN, 0);
//	digitalWrite(CVGATE_PIN, LOW);
}

void doArpStep(Micros dt) {
	arpTimer -= dt;
	if(arpTimer < (float)arpTime * (1.0f - ((float)arpGate / 127.0f))) {
		if(arpLastNote != -1) {
			rawNoteOff(arpLastNote, arpLastChannel);
			arpLastNote = -1;
			arpLastChannel = -1;
		}
	}
	if(arpTimer < 0) {
		float swing = ((float)arpSwing / 255.0f);
		Micros arpStepMicros = (Micros)((float)step_micros * (1.0f + (arpSwingIndex == 0 ? swing : -swing)));
		arpSwingIndex = (arpSwingIndex + 1) % 2;
		arpTime = arpStepMicros;
		arpTimer += arpStepMicros;
		// next note
		arpIndex = (arpIndex + 1) % ARP_SEQ_LEN;
		// find next valid note
		int note = arpSeq[arpIndex];
		if(note == -1) {
			for(int i = 0; i < ARP_SEQ_LEN; i++) {
				arpIndex = (arpIndex + 1) % ARP_SEQ_LEN;
				note = arpSeq[arpIndex];
				if(note != -1) {
					break;
				}
			}
		}
		if(note != -1) {
			if(arpIndex == 0) {
				arpOctaveIndex = (arpOctaveIndex + 1) % arpOctaves;
			}
			note += 12 * arpOctaveIndex;
			int channel = midiChannel + rrIndex;
			rawNoteOn(note, midiVelocity, channel);
			arpLastNote = note;
			arpLastChannel = channel;
			rrIndex = (rrIndex + 1) % midiRRChannelCount;
		}
	}
}

void doStep() {
// // probability test
	bool testProb = probResult(stepNoteP[playingPattern][seqPos[playingPattern]].prob);

	switch(omxMode){
		case MODE_S1:
			if(playing) {
				// ############## STEP TIMING ##############
//				if(micros() >= nextStepTime){
				if(micros() >= timePerPattern[playingPattern].nextStepTimeP){
					seqReset();
					// DO STUFF

//					int lastPos = (seqPos[playingPattern]+15) % 16;
//					if (lastNote[playingPattern][lastPos] > 0){
//						step_off(playingPattern, lastPos);
//					}
//					lastStepTime = nextStepTime;
//					nextStepTime += step_micros;

					timePerPattern[playingPattern].lastPosP = (seqPos[playingPattern]+15) % 16;
					if (lastNote[playingPattern][timePerPattern[playingPattern].lastPosP] > 0){
						step_off(playingPattern, timePerPattern[playingPattern].lastPosP);
					}
					timePerPattern[playingPattern].lastStepTimeP = timePerPattern[playingPattern].nextStepTimeP;
					timePerPattern[playingPattern].nextStepTimeP += (step_micros)*( multValues[patternSettings[playingPattern].clockDivMultP] ); // calc step based on rate

					if (testProb){ //  && evaluate_AB(stepNoteP[playingPattern][seqPos[playingPattern]].condition, playingPattern)
						playNote(playingPattern);
	//					step_on(playingPattern);
					}


					show_current_step(playingPattern); // show led for step
					step_ahead(playingPattern);
				}
			} else {
				show_current_step(playingPattern);
			}
			break;

		case MODE_S2:
			if(playing) {
				unsigned long playstepmicros = micros();

				for (int j=0; j<NUM_PATTERNS; j++){ // check all patterns for notes to play in time

					// CLOCK PER PATTERN BASED APPROACH
						if(playstepmicros >= timePerPattern[j].nextStepTimeP){

						seqReset(); // check for seqReset
						timePerPattern[j].lastStepTimeP = timePerPattern[j].nextStepTimeP;
						timePerPattern[j].nextStepTimeP += (step_micros)*( multValues[patternSettings[j].clockDivMultP] ); // calc step based on rate

						// only play if not muted
						if (!patternSettings[j].mute) {
							timePerPattern[j].lastPosP = (seqPos[j]+15) % 16;
							if (lastNote[j][timePerPattern[j].lastPosP] > 0){
								step_off(j, timePerPattern[j].lastPosP);
							}
							if (testProb){
								if (evaluate_AB(stepNoteP[j][seqPos[j]].condition, j)){
									playNote(j);
								}
							}
						}
//						show_current_step(playingPattern);
						if(j == playingPattern){ // only show selected pattern
														show_current_step(playingPattern);
												}
						new_step_ahead(j);
					}
				}


			} else {
				show_current_step(playingPattern);
			}
			break;

		default:
			break;
	}
}

void cvNoteOn(int notenum){
	if (notenum>=midiLowestNote && notenum <midiHightestNote){
		pitchCV = static_cast<int>(roundf( (notenum - midiLowestNote) * stepsPerSemitone)); // map (adjnote, 36, 91, 0, 4080);
		digitalWrite(CVGATE_PIN, HIGH);
		analogWrite(CVPITCH_PIN, pitchCV);
	}
}
void cvNoteOff(){
	digitalWrite(CVGATE_PIN, LOW);
//	analogWrite(CVPITCH_PIN, 0);
}

// #### Inbound MIDI callbacks
void OnNoteOn(byte channel, byte note, byte velocity) {
	if (midiInToCV){
		cvNoteOn(note);
	}
	if (omxMode == MODE_MIDI){				
		midiLastNote = note;
		int whatoct = (note / 12) - octave;
		int thisKey;
		uint32_t keyColor = MIDINOTEON;
		if ( (whatoct % 2) == 0) {
			thisKey = note - (12 * whatoct);
		} else {
			thisKey = note - (12 * whatoct) + 12;
		}
		strip.setPixelColor(midiKeyMap[thisKey], keyColor);         //  Set pixel's color (in RAM)
	//	dirtyPixels = true;	
		strip.show();
		dirtyDisplay = true;
	}
}
void OnNoteOff(byte channel, byte note, byte velocity) {
	if (midiInToCV){
		cvNoteOff();
	}
	if (omxMode == MODE_MIDI){
		int whatoct = (note / 12);
		int thisKey;
		if ( (whatoct % 2) == 0) {
			thisKey = note - (12 * whatoct);
		} else {
			thisKey = note - (12 * whatoct) + 12;
		}
		int pixel = midiKeyMap[thisKey];
		strip.setPixelColor(pixel, getDefaultColor(pixel));         //  Set pixel's color (in RAM)
	//	dirtyPixels = true;	
		strip.show();
		dirtyDisplay = true;
	}
}

// #### Outbound MIDI Mode note on/off
void midiNoteOn(int keynum, int velocity, int channel) {
	int adjnote = notes[keynum] + (octave * 12); // adjust key for octave range
	int adjchan = channel;

	if (adjnote>=0 && adjnote <128){
		midiLastNote = adjnote;

		// keep track of adjusted note when pressed so that when key is released we send
		// the correct note off message

		if(!arp) {
			// RoundRobin Setting?
			if (midiRoundRobin) {
				adjchan = channel + rrIndex;
			} else {
				adjchan = channel;
			}
			midiChannelState[keynum] = adjchan;
			MM::sendNoteOn(adjnote, velocity, adjchan);
			// CV
			cvNoteOn(adjnote);
		} else {
			bool latchTransition = false;
			if(arpLatch) {
				// if no keys are down, reset all arp notes and transition to it
				bool anyKeysDown = false;
				for(int i = 0; i < 27; i++) {
					if(midiKeyState[i] != -1) {
						anyKeysDown = true;
						break;
					}
				}
				if(anyKeysDown == false) {
					for(int i = 0; i < ARP_SEQ_LEN; i++) {
						if(arpSeq[i] != -1) {
							latchTransition = true;
						}
						arpSeq[i] = -1;
					}
				}
			}
			for(int i = 0; i < ARP_SEQ_LEN; i++) {
				if(arpSeq[i] == -1) {
					arpSeq[i] = adjnote;
					if(i == 0 && latchTransition == false) {
						arpIndex = -1;
						arpTimer = 0;
						arpSwingIndex = 0;
					}
					break;
				}
			}
		}
		midiKeyState[keynum] = adjnote;
	}

	strip.setPixelColor(keynum, MIDINOTEON);         //  Set pixel's color (in RAM)
	dirtyPixels = true;
	dirtyDisplay = true;

	rrIndex = (rrIndex + 1) % midiRRChannelCount;
}

void midiNoteOff(int keynum, int channel) {
	// we use the key state captured at the time we pressed the key to send the correct note off message
	int adjnote = midiKeyState[keynum];
	int adjchan = midiChannelState[keynum];
	midiKeyState[keynum] = -1;
	midiChannelState[keynum] = -1;
	if(!arp) {
		if (adjnote>=0 && adjnote <128){
			MM::sendNoteOff(adjnote, 0, adjchan);
			// CV off
			cvNoteOff();
		}
	} else {
		if(arpLatch == false) {
			for(int i = 0; i < ARP_SEQ_LEN; i++) {
				if(arpSeq[i] == adjnote) {
					arpSeq[i] = -1;
					break;
				}
			}
		}
	}

	strip.setPixelColor(keynum, getDefaultColor(keynum));
	dirtyPixels = true;
	dirtyDisplay = true;
}

void rawNoteOn(int notenum, int velocity, int channel) {
	MM::sendNoteOn(notenum, velocity, channel);
	int noteOnKB = notenum - (octave + 5) * 12;
	if(noteOnKB >= 0 && noteOnKB <= 26) {
		int key = midiKeyMap[noteOnKB];
		strip.setPixelColor(key, MIDINOTEON);
	}

}

void rawNoteOff(int notenum, int channel) {
	MM::sendNoteOff(notenum, 0, channel);
	int noteOnKB = notenum - (octave + 5) * 12;
	if(noteOnKB >= 0 && noteOnKB <= 26) {
		int key = midiKeyMap[noteOnKB];
		strip.setPixelColor(key, getDefaultColor(key));
	}
}

// #### SEQ Mode note on/off
void seqNoteOn(int notenum, int velocity, int patternNum){
	int adjnote = notes[notenum] + (octave * 12); // adjust key for octave range
	if (adjnote>=0 && adjnote <128){
		lastNote[patternNum][seqPos[patternNum]] = adjnote;
		MM::sendNoteOn(adjnote, velocity, PatternChannel(playingPattern));

		// keep track of adjusted note when pressed so that when key is released we send
		// the correct note off message
		midiKeyState[notenum] = adjnote;

		// CV
		if (cvPattern[playingPattern]){
			cvNoteOn(adjnote);
		}
	}

	strip.setPixelColor(notenum, MIDINOTEON);         //  Set pixel's color (in RAM)
	dirtyPixels = true;
	dirtyDisplay = true;
}

void seqNoteOff(int notenum, int patternNum){
	// we use the key state captured at the time we pressed the key to send the correct note off message
	int adjnote = midiKeyState[notenum];
	if (adjnote>=0 && adjnote <128){
		MM::sendNoteOff(adjnote, 0, PatternChannel(playingPattern));
		// CV off
		if (cvPattern[playingPattern]){
			cvNoteOff();
		}
	}

	strip.setPixelColor(notenum, LEDOFF);
	dirtyPixels = true;
	dirtyDisplay = true;
}


// Play a note / step (SEQUENCERS)
void playNote(int patternNum) {
//	Serial.println(stepNoteP[patternNum][seqPos[patternNum]].note); // Debug
	bool sendnoteCV = false;
	int rnd_swing;
	if (cvPattern[patternNum]){
		sendnoteCV = true;
	}
	StepType playStepType = stepNoteP[patternNum][seqPos[patternNum]].stepType;

	if (stepNoteP[patternNum][seqPos[patternNum]].stepType == STEPTYPE_RAND){
		auto tempType = random(STEPTYPE_COUNT);

		// this is fucking hacky to increment the enum for stepType
		switch(tempType){
			case 0:
				playStepType = STEPTYPE_NONE;
				break;
			case 1:
				playStepType = STEPTYPE_RESTART;
				break;
			case 2:
				playStepType = STEPTYPE_FWD;
				break;
			case 3:
				playStepType = STEPTYPE_REV;
				break;
			case 4:
				playStepType = STEPTYPE_PONG;
				break;
			case 5:
				playStepType = STEPTYPE_RANDSTEP;
				break;
		}
//		Serial.println(playStepType);
	}

	switch (playStepType) {
		case STEPTYPE_COUNT:	// fall through
		case STEPTYPE_RAND:
			break;
		case STEPTYPE_NONE:
			break;
		case STEPTYPE_FWD:
			patternSettings[patternNum].reverse = 0;
			break;
		case STEPTYPE_REV:
			patternSettings[patternNum].reverse = 1;
			break;
		case STEPTYPE_PONG:
			patternSettings[patternNum].reverse = !patternSettings[patternNum].reverse;
			break;
		case STEPTYPE_RANDSTEP:
			seqPos[patternNum] = (rand() % PatternLength(patternNum)) + 1;
			break;
		case STEPTYPE_RESTART:
			seqPos[patternNum] = 0;
			break;
		break;
	}

	// regular note on trigger

	if (stepNoteP[patternNum][seqPos[patternNum]].trig == TRIGTYPE_PLAY){

		seq_velocity = stepNoteP[patternNum][seqPos[patternNum]].vel;

		noteoff_micros = micros() + ( stepNoteP[patternNum][seqPos[patternNum]].len + 1 )* step_micros ;
		pendingNoteOffs.insert(stepNoteP[patternNum][seqPos[patternNum]].note, PatternChannel(patternNum), noteoff_micros, sendnoteCV );

		if (seqPos[patternNum] % 2 == 0){

			if (patternSettings[patternNum].swing < 99){
				noteon_micros = micros() + ((ppqInterval * multValues[patternSettings[patternNum].clockDivMultP])/(PPQ / 24) * patternSettings[patternNum].swing); // full range swing
//				Serial.println((ppqInterval * multValues[patternSettings[patternNum].clockDivMultP])/(PPQ / 24) * patternSettings[patternNum].swing);
//			} else if ((patternSettings[patternNum].swing > 50) && (patternSettings[patternNum].swing < 99)){
//			   noteon_micros = micros() + ((step_micros * multValues[patternSettings[patternNum].clockDivMultP]) * ((patternSettings[patternNum].swing - 50)* .01) ); // late swing
//			   Serial.println(((step_micros * multValues[patternSettings[patternNum].clockDivMultP]) * ((patternSettings[patternNum].swing - 50)* .01) ));
			} else if (patternSettings[patternNum].swing == 99){ // random drunken swing
				rnd_swing = rand() % 95 + 1; // rand 1 - 95 // randomly apply swing value
				noteon_micros = micros() + ((ppqInterval * multValues[patternSettings[patternNum].clockDivMultP])/(PPQ / 24) * rnd_swing);
			}

		} else {
			noteon_micros = micros();
		}

		// Queue note-on
		pendingNoteOns.insert(stepNoteP[patternNum][seqPos[patternNum]].note, seq_velocity, PatternChannel(patternNum), noteon_micros, sendnoteCV );

		// {notenum, vel, notelen, step_type, {p1,p2,p3,p4}, prob}
		// send param locks
		for (int q=0; q<4; q++){
			int tempCC = stepNoteP[patternNum][seqPos[patternNum]].params[q];
			if (tempCC > -1) {
				MM::sendControlChange(pots[potbank][q],tempCC,PatternChannel(patternNum));
				prevPlock[q] = tempCC;
			} else if (prevPlock[q] != potValues[q]) {
				//if (tempCC != prevPlock[q]) {
				MM::sendControlChange(pots[potbank][q],potValues[q],PatternChannel(patternNum));
				prevPlock[q] = potValues[q];
			}
		}
		lastNote[patternNum][seqPos[patternNum]] = stepNoteP[patternNum][seqPos[patternNum]].note;

		// CV is sent from pendingNoteOns/pendingNoteOffs

	}
}

void allNotesOff() {
	pendingNoteOffs.allOff();
}

void allNotesOffPanic() {
	analogWrite(CVPITCH_PIN, 0);
	digitalWrite(CVGATE_PIN, LOW);
	for (int j=0; j<128; j++){
		MM::sendNoteOff(j, 0, midiChannel);  // NEEDS FIXING
	}
}

void transposeSeq(int patternNum, int amt) {
	for (int k=0; k<NUM_STEPS; k++){
		stepNoteP[patternNum][k].note += amt;
	}
}

void seqReset(){
	if (seqResetFlag) {
		for (int k=0; k<NUM_PATTERNS; k++){
			for (int q=0; q<NUM_STEPS; q++){
				loopCount[k][q] = 0;
			}
			if (patternSettings[k].reverse) { // REVERSE
				seqPos[k] = PatternLength(k) - 1;
			} else {
				seqPos[k] = 0;
			}
		}
		MM::stopClock();
		MM::startClock();
		seqResetFlag = false;
	}
}

void seqStart() {
	playing = 1;


	for (int x=0; x<NUM_PATTERNS; x++){
		timePerPattern[x].nextStepTimeP = micros();
		timePerPattern[x].lastStepTimeP = micros();
	}

	if (!seqResetFlag) {
		MM::continueClock();
//	} else if (seqPos[playingPattern]==0) {
//		MM::startClock();
	}
}

void seqStop() {
	ticks = 0;
	playing = 0;
	MM::stopClock();
	allNotesOff();
}

void seqContinue() {
	playing = 1;
}

int getPatternPage(int position){
	return position / NUM_STEPKEYS;
}

void rotatePattern(int patternNum, int rot) {
	if ( patternNum < 0 || patternNum >= NUM_PATTERNS )
		return;
	int size = PatternLength(patternNum);
	StepNote arr[size];
	rot = (rot + size) % size;
	for (int d = rot, s = 0; s < size; d = (d+1) % size, ++s)
		arr[d] = stepNoteP[patternNum][s];
	for (int i = 0; i < size; ++i)
		stepNoteP[patternNum][i] = arr[i];
}

void resetPatternDefaults(int patternNum){
	for (int i = 0; i < NUM_STEPS; i++){
		// {notenum,vel,len,stepType,{p1,p2,p3,p4,p5}}
		stepNoteP[patternNum][i].note = patternDefaultNoteMap[patternNum];
		stepNoteP[patternNum][i].len = 0;
	}
}

void clearPattern(int patternNum){
	for (int i = 0; i < NUM_STEPS; i++){
		// {notenum,vel,len,stepType,{p1,p2,p3,p4,p5}}
		stepNoteP[patternNum][i].note = patternDefaultNoteMap[patternNum];
		stepNoteP[patternNum][i].vel = defaultVelocity;
		stepNoteP[patternNum][i].len = 0;
		stepNoteP[patternNum][i].stepType = STEPTYPE_NONE;
		stepNoteP[patternNum][i].params[0] = -1;
		stepNoteP[patternNum][i].params[1] = -1;
		stepNoteP[patternNum][i].params[2] = -1;
		stepNoteP[patternNum][i].params[3] = -1;
		stepNoteP[patternNum][i].params[4] = -1;
		stepNoteP[patternNum][i].prob = 100;
		stepNoteP[patternNum][i].condition = 0;
	}
}

void copyPattern(int patternNum){
	//for( int i = 0 ; i < NUM_STEPS ; ++i ){
	//	copyPatternBuffer[i] = stepNoteP[patternNum][i];
	//}

	memcpy( &copyPatternBuffer, &stepNoteP[patternNum], NUM_STEPS * sizeof(StepNote) );
}

void pastePattern(int patternNum){
	//for( int i = 0 ; i < NUM_STEPS ; ++i ){
	//	stepNoteP[patternNum][i] = copyPatternBuffer[i] ;
	//}

	memcpy( &stepNoteP[patternNum], &copyPatternBuffer, NUM_STEPS * sizeof(StepNote) );
}

void u8g2centerText(const char* s, int16_t x, int16_t y, uint16_t w, uint16_t h) {
//  int16_t bx, by;
	uint16_t bw, bh;
	bw = u8g2_display.getUTF8Width(s);
	bh = u8g2_display.getFontAscent();
	u8g2_display.setCursor(
		x + (w - bw) / 2,
		y + (h - bh) / 2
	);
	u8g2_display.print(s);
}

void u8g2centerNumber(int n, uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
	char buf[8];
	itoa(n, buf, 10);
	u8g2centerText(buf, x, y, w, h);
}


// #### LED STUFF
// Rainbow cycle along whole strip. Pass delay time (in ms) between frames.
void rainbow(int wait) {
	// Hue of first pixel runs 5 complete loops through the color wheel.
	// Color wheel has a range of 65536 but it's OK if we roll over, so
	// just count from 0 to 5*65536. Adding 256 to firstPixelHue each time
	// means we'll make 5*65536/256 = 1280 passes through this outer loop:
	for(long firstPixelHue = 0; firstPixelHue < 1*65536; firstPixelHue += 256) {
		for(int i=0; i<strip.numPixels(); i++) { // For each pixel in strip...
			// Offset pixel hue by an amount to make one full revolution of the
			// color wheel (range of 65536) along the length of the strip
			// (strip.numPixels() steps):
			int pixelHue = firstPixelHue + (i * 65536L / strip.numPixels());

			// strip.ColorHSV() can take 1 or 3 arguments: a hue (0 to 65535) or
			// optionally add saturation and value (brightness) (each 0 to 255).
			// Here we're using just the single-argument hue variant. The result
			// is passed through strip.gamma32() to provide 'truer' colors
			// before assigning to each pixel:
			strip.setPixelColor(i, strip.gamma32(strip.ColorHSV(pixelHue, 127, 255)));
		}
		strip.show(); // Update strip with new contents
		delay(wait);  // Pause for a moment
	}
}
void setAllLEDS(int R, int G, int B) {
	for(int i=0; i<LED_COUNT; i++) { // For each pixel...
		strip.setPixelColor(i, strip.Color(R, G, B));
	}
	dirtyPixels = true;
}

// #### OLED STUFF
void testdrawrect(void) {
	display.clearDisplay();

	for(int16_t i=0; i<display.height()/2; i+=2) {
		display.drawRect(i, i, display.width()-2*i, display.height()-2*i, SSD1306_WHITE);
		display.display(); // Update screen with each newly-drawn rectangle
		delay(1);
	}

	delay(500);
}

void drawLoading(void) {
	const char* loader[] = {"\u25f0", "\u25f1", "\u25f2", "\u25f3"};
	display.clearDisplay();
	u8g2_display.setFontMode(0);
	for(int16_t i=0; i<16; i+=1) {
		display.clearDisplay();
		u8g2_display.setCursor(18,18);
		u8g2_display.setFont(FONT_TENFAT);
		u8g2_display.print("OMX-27");
		u8g2_display.setFont(FONT_SYMB_BIG);
		u8g2centerText(loader[i%4], 80, 10, 32, 32); // "\u00BB\u00AB" // // dice: "\u2685"
		display.display();
		delay(100);
	}

	delay(100);
}

void initPatterns( void ) {
	// default to GM Drum Map for now -- GET THIS FROM patternDefaultNoteMap instead
//	uint8_t initNotes[NUM_PATTERNS] = {
//		36,
//		38,
//		37,
//		39,
//		42,
//		46,
//		49,
//		51 };

	StepNote stepNote = { 0, 100, 0, TRIGTYPE_MUTE, { -1, -1, -1, -1, -1 }, 100, 0, STEPTYPE_NONE };
					// {note, vel, len, TRIGTYPE, {params0, params1, params2, params3, params4}, prob, condition, STEPTYPE}

	for ( int i=0; i<NUM_PATTERNS; i++ ) {
		stepNote.note = patternDefaultNoteMap[i];		// Defined in sequencer.h
		for ( int j=0; j<NUM_STEPS; j++ ) {
			memcpy( &stepNoteP[i][j], &stepNote, sizeof(StepNote) );
		}

		patternSettings[i].len = 15;
		patternSettings[i].channel = i;		// 0 - 15 becomes 1 - 16
		patternSettings[i].mute = false;
		patternSettings[i].reverse = false;
		patternSettings[i].swing = 0;
		patternSettings[i].startstep = 0;
		patternSettings[i].autoresetstep = 0;
		patternSettings[i].autoresetfreq = 0;
		patternSettings[i].autoresetprob = 0;
		patternSettings[i].current_cycle = 1;
		patternSettings[i].rndstep = 3;
		patternSettings[i].autoreset = false;
		patternSettings[i].solo = false;
	}
}

void saveHeader( void ) {
	// 1 byte for EEPROM version
	storage->write( EEPROM_HEADER_ADDRESS + 0, EEPROM_VERSION );

	// 1 byte for mode
	storage->write( EEPROM_HEADER_ADDRESS + 1, (uint8_t)omxMode );

	// 1 byte for the active pattern
	storage->write( EEPROM_HEADER_ADDRESS + 2, (uint8_t)playingPattern );

	// 1 byte for Midi channel
	uint8_t unMidiChannel = (uint8_t)( midiChannel - 1 );
	storage->write( EEPROM_HEADER_ADDRESS + 3, unMidiChannel );

	for ( int i=0; i<NUM_CC_POTS; i++ ) {
		storage->write( EEPROM_HEADER_ADDRESS + 4 + i, pots[potbank][i] );
	}

	// 23 bytes remain for header fields
}

// returns true if the header contained initialized data
// false means we shouldn't attempt to load any further information
bool loadHeader( void ) {
	uint8_t version = storage->read(EEPROM_HEADER_ADDRESS + 0);

	//char buf[64];
	//snprintf( buf, sizeof(buf), "EEPROM Header Version is %d\n", version );
	//Serial.print( buf );

	// Uninitalized EEPROM memory is filled with 0xFF
	if ( version == 0xFF ) {
		// EEPROM was uninitialized
		//Serial.println( "version was 0xFF" );
		return false;
	}

	if ( version != EEPROM_VERSION ) {
		// write an adapter if we ever need to increment the EEPROM version and also save the existing patterns
		// for now, return false will essentially reset the state
		return false;
	}

	omxMode = (OMXMode)storage->read( EEPROM_HEADER_ADDRESS + 1 );

	playingPattern = storage->read( EEPROM_HEADER_ADDRESS + 2 );

	uint8_t unMidiChannel = storage->read( EEPROM_HEADER_ADDRESS + 3 );
	midiChannel = unMidiChannel + 1;

	for ( int i=0; i<NUM_CC_POTS; i++ ) {
		pots[potbank][i] = storage->read( EEPROM_HEADER_ADDRESS + 4 + i );
	}

	return true;
}

void savePatterns( void ) {
	int nLocalAddress = EEPROM_PATTERN_ADDRESS;
	int s = sizeof( StepNote );

	// storage->writeObject uses storage->write under the hood, so writes here of the same data are a noop

	// for each pattern
	for ( int i=0; i<NUM_PATTERNS; i++ ) {
		for ( int j=0; j<NUM_STEPS; j++ ) {
			storage->writeObject( nLocalAddress, stepNoteP[i][j] );
			nLocalAddress += s;
		}
	}

	nLocalAddress = EEPROM_PATTERN_SETTINGS_ADDRESS;
	s = sizeof( PatternSettings );

	// save pattern settings
	for ( int i=0; i<NUM_PATTERNS; i++ ) {
		storage->writeObject( nLocalAddress, patternSettings[i] );
		nLocalAddress += s;
	}
}

void loadPatterns( void ) {
	//Serial.println( "load patterns" );

	int nLocalAddress = EEPROM_PATTERN_ADDRESS;
	int s = sizeof( StepNote );

	// for each pattern
	for ( int i=0; i<NUM_PATTERNS; i++ ) {
		for ( int j=0; j<NUM_STEPS; j++ ) {
			storage->readObject( nLocalAddress, stepNoteP[i][j] );
			nLocalAddress += s;
		}
	}

	nLocalAddress = EEPROM_PATTERN_SETTINGS_ADDRESS;
	s = sizeof( PatternSettings );

	// load pattern length
	for ( int i=0; i<NUM_PATTERNS; i++ ) {
		storage->readObject( nLocalAddress, patternSettings[i] );
		nLocalAddress += s;
	}
}

// currently saves everything ( mode + patterns )
void saveToStorage( void ) {
	//Serial.println( "saving..." );
	saveHeader();
	savePatterns();
}

// currently loads everything ( mode + patterns )
bool loadFromStorage( void ) {
	// This load can happen soon after Serial.begin - enable this 'wait for Serial' if you need to Serial.print during loading
	//while( !Serial );

	bool bContainedData = loadHeader();

	//Serial.println( "read the header" );

	if ( bContainedData ) {
		//Serial.println( "loading patterns" );
		loadPatterns();
		return true;
	}

	return false;
}
