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
#include <MIDI.h>

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

#define AUX_HELD keyState[0]
#define F1_HELD keyState[1]
#define F2_HELD keyState[2]
#define IS_PATTERN(x) ((x) > 2 && (x) < 11)
#define IS_STEP(x) ((x) > 10)
#define KEY_TO_PATTERN(x) ((x) - 3)
#define KEY_TO_STEP(x) ((x) - 11)
#define KEY_TO_PAGE(x) ((x) - 11 - 4)
#define PATTERN_TO_KEY(x) ((x) + 3)
#define STEP_TO_KEY(x) ((x) + 11)
#define PAGE_TO_KEY(x) ((x) + 11 + 4)

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
elapsedMillis dirtyDisplayTimer = 0;
unsigned long displayRefreshRate = 60;
elapsedMicros clksTimer = 0;		// is this still in use?

//unsigned long clksDelay;
elapsedMillis keyPressTime[27] = {0};
bool invalidateShortPress[27];

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

int pageWrapDelay = 0;
int pageChangeDelay = 0;

int miparam = 0;	// midi params item counter
int soloParam = 0;
int nsparam = 0;	// note select params
int ppparam = 0;	// pattern params
int sqparam = 0;	// seq params
int configparam = 0;
int tmpmmode = 9;

const ui_param* currentParam = NULL;

int* param = NULL;
const ui_param* params = NULL;
int numParams = 0;

// VARIABLES / FLAGS
float step_delay;
bool dirtyPixels = false;
bool dirtyDisplay = false;
bool blinkState = false;
bool slowBlinkState = false;
int selectedNote = 0;
int selectedStep = 0;
bool stepSelect = false;
bool stepRecord = false;
bool stepDirty = false;

bool sendPotsDuringPLock = false;

int scaleRoot = 0;
int scalePattern = -1;
bool scaleSelectHold;
bool showScaleInSeq = false;

uint32_t stepColor = 0x000000;
uint32_t muteColor = 0x000000;

bool mode_select = false;

bool arp = false;
bool arpLatch = false;
int arpTimer = 0;
int arpTime = 0;
int arpMdiv = 0;
int arpSwing = 0;
uint8_t arpSwingIndex = 0;
int arpOctaves = 2;
int arpOctaveIndex = 0;
int arpGate = 64;

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

const char* arpSortStr[] = {
	"PLAY",
	"UP",
	"DN",
	"UPDN",
	"DNUP",
};

int arpSeqSorted[ARP_SEQ_LEN];
int arpSeqSort = 0;
midiNote arpActiveNotes[16];
int arpLastNote = -1;
int arpLastChannel = -1;
uint8_t arpIndex = -1;
int rrIndex = 0;
int midiRRChannelCount = 1;
int currpgm = 0;
int currbank = 0;
bool midiInToCV = true;
int midiChannel = 1;
int midiLastNote = -1;

int previewLastNote = -1;
int previewLastChannel = -1;

bool seqStepHold = false;
bool seqStepEdit = false;
bool seqStepNoteSelect = false;
bool patternHold = false;
bool patternEdit = false;
bool patternEditLock = false;

int soloPattern = -1;

int clearedPageTimer = 0;
int clearedPage = -1;

const auto uiDrawTextY = 20;

char uiValueBuffer[5];

void uiDrawValueNone(int value, int x) {
	u8g2centerText("   ", x*32, hline*2+3, 32, 20);
}

void uiDrawValueInt(int value, int x) {
	if(value == -127) {
		u8g2centerText("---", x*32, hline*2+3, 32, uiDrawTextY);
	} else {
		u8g2centerNumber(value, x*32, hline*2+3, 32, uiDrawTextY);
	}
}

void uiDrawValueProb(int value, int x) {
	if(value == -127) {
		u8g2centerText("---", x*32, hline*2+3, 32, uiDrawTextY);
	} else if(value > 100) {
		u8g2centerText("ON", x*32, hline*2+3, 32, uiDrawTextY);
	} else {
        snprintf(uiValueBuffer, 5, "%d%%", value);
		u8g2centerText(uiValueBuffer, x*32, hline*2+3, 32, uiDrawTextY);
	}
}

void uiDrawValueCC(int value, int x) {
	if(value == -1) {
		u8g2centerText("VEL", x*32, hline*2+3, 32, uiDrawTextY);
	} else if(value == -2) {
		u8g2centerText("PB", x*32, hline*2+3, 32, uiDrawTextY);
	} else if(value == -3) {
		u8g2centerText("PG", x*32, hline*2+3, 32, uiDrawTextY);
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

const int chordPatterns[][3] = {
	{ -1, -1, -1 }, // 0:  N/A
	{ 4, 7, -1 },   // 1:  MAJ
	{ 3, 7, -1 },   // 2:  MIN
	{ 4, 7, 11 },   // 3:  MAJ7
	{ 3, 7, 10 },   // 4:  MIN7
	{ 4, 7, 10 },   // 5:  7
	{ 2, 7, -1 },   // 6:  SUS2
	{ 5, 7, -1 },   // 7:  SUS4
	{ 4, 8, -1 },   // 8:  AUG
	{ 3, 6, -1 },   // 9:  DIM
	{ 3, 6, 10 },   // 10: HDIM
	{ 7, -1, -1 },  // 11: 5
	{ 4, 11, 14 },  // 12: MAJ9
	{ 3, 10, 14 },  // 13: MIN9
	{ 4, 10, 14 },  // 14: 9
	{ 5, 7, 11 },    // 15: 7SUS4
};

void uiDrawValueChord(int value, int x) {
	if(value == 1) {
		u8g2centerText("MAJ", x*32, hline*2+3, 32, uiDrawTextY);
	} else if(value == 2) {
		u8g2centerText("MIN", x*32, hline*2+3, 32, uiDrawTextY);
	} else if(value == 3) {
		u8g2centerText("MAJ7", x*32, hline*2+3, 32, uiDrawTextY);
	} else if(value == 4) {
		u8g2centerText("MIN7", x*32, hline*2+3, 32, uiDrawTextY);
	} else if(value == 5) {
		u8g2centerText("7", x*32, hline*2+3, 32, uiDrawTextY);
	} else if(value == 6) {
		u8g2centerText("SUS2", x*32, hline*2+3, 32, uiDrawTextY);
	} else if(value == 7) {
		u8g2centerText("SUS4", x*32, hline*2+3, 32, uiDrawTextY);
	} else if(value == 8) {
		u8g2centerText("AUG", x*32, hline*2+3, 32, uiDrawTextY);
	} else if(value == 9) {
		u8g2centerText("DIM", x*32, hline*2+3, 32, uiDrawTextY);
	} else if(value == 10) {
		u8g2centerText("HDIM", x*32, hline*2+3, 32, uiDrawTextY);
	} else if(value == 11) {
		u8g2centerText("5", x*32, hline*2+3, 32, uiDrawTextY);
	} else if(value == 12) {
		u8g2centerText("MAJ9", x*32, hline*2+3, 32, uiDrawTextY);
	} else if(value == 13) {
		u8g2centerText("MIN9", x*32, hline*2+3, 32, uiDrawTextY);
	} else if(value == 14) {
		u8g2centerText("9", x*32, hline*2+3, 32, uiDrawTextY);
	} else if(value == 15) {
		u8g2centerText("7SUS4", x*32, hline*2+3, 32, uiDrawTextY);
	} else {
		u8g2centerText("---", x*32, hline*2+3, 32, uiDrawTextY);
	}
}


const ui_param param_DUMMY = {
	"",
	NULL,
	true,
	0,
	0,
	uiDrawValueNone,
	NULL,
	NULL,
	NULL,
};

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

const ui_param param_POTB = {
	"PBNK",
	&potbank,
	false,
	0,
	3,
	uiDrawValueInt1
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

const ui_param param_CC1 = {
	"CC1",
	NULL,
	false,
	-3,
	127,
	uiDrawValueCC,
	[](int* valPtr, int newVal, int amt)->void {
	    pots[potbank][0] = newVal;
	},
    NULL,
    []()->int {
        return (int)pots[potbank][0];
    }
};
const ui_param param_CC2 = {
	"CC2",
	NULL,
	false,
	-3,
	127,
	uiDrawValueCC,
	[](int* valPtr, int newVal, int amt)->void {
	    pots[potbank][1] = newVal;
	},
    NULL,
    []()->int {
        return (int)pots[potbank][1];
    }
};
const ui_param param_CC3 = {
	"CC3",
	NULL,
	false,
	-3,
	127,
	uiDrawValueCC,
	[](int* valPtr, int newVal, int amt)->void {
	    pots[potbank][2] = newVal;
	},
    NULL,
    []()->int {
        return (int)pots[potbank][2];
    }
};
const ui_param param_CC4 = {
	"CC4",
	NULL,
	false,
	-3,
	127,
	uiDrawValueCC,
	[](int* valPtr, int newVal, int amt)->void {
	    pots[potbank][3] = newVal;
	},
    NULL,
    []()->int {
        return (int)pots[potbank][3];
    }
};
const ui_param param_CC5 = {
	"CC5",
	NULL,
	false,
	-3,
	127,
	uiDrawValueCC,
	[](int* valPtr, int newVal, int amt)->void {
	    pots[potbank][4] = newVal;
	},
    NULL,
    []()->int {
        return (int)pots[potbank][4];
    }
};
const ui_param config_params[] = {
    param_POTB,
    param_CC1,
    param_CC2,
    param_CC3,
    param_CC4,
    param_CC5,
	{
		"PLSEND",
		(int*)&sendPotsDuringPLock,
		false,
		0,
		1,
		uiDrawValueBool,
	},
	{
		"SAT",
		NULL,
		false,
		0,
		255,
		uiDrawValueInt,
		[](int* valPtr, int newVal, int amt)->void {
			rainbowSaturation = newVal;
			setScale(scaleRoot, scalePattern);
			dirtyPixels = true;
		},
		NULL,
		[]()->int {
			return (int)rainbowSaturation;
		}
	},
	{
		"SCL",
		NULL,
		false,
		0,
		255,
		uiDrawValueInt,
		[](int* valPtr, int newVal, int amt)->void {
			scaleBrightness = newVal;
			setScale(scaleRoot, scalePattern);
			dirtyPixels = true;
		},
		NULL,
		[]()->int {
			return (int)scaleBrightness;
		}
	},
	{
		"S.PRES",
		NULL,
		false,
		10,
		100,
		uiDrawValueInt,
		[](int* valPtr, int newVal, int amt)->void {
			shortPressInterval = newVal * 10;
		},
		NULL,
		[]()->int {
			return (int)shortPressInterval / 10;
		}
	},
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
	{
		"VEL",
		&midiVelocity,
		false,
		0,
		127,
		uiDrawValueInt
	},
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
	param_POTB,
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
		"A.RATE",
		NULL,
		false,
		0,
		NUM_MULTDIVS-1,
		[](int v, int x)->void {
			u8g2centerText(mdivs[v], x*32, hline*2+3, 32, uiDrawTextY);
		},
		[](int* valPtr, int newVal, int amt)->void {
			arpMdiv = newVal;
		},
		NULL,
		[]()->int {
			return (int)arpMdiv;
		}
	},
	{
		"A.DIR",
		&arpSeqSort,
		false,
		0,
		4,
		[](int v, int x)->void {
			u8g2centerText(arpSortStr[v], x*32, hline*2+3, 32, uiDrawTextY);
		},
		[](int* valPtr, int newVal, int amt)->void {
			arpSeqSort = newVal;
			arpSort();
		},
	}

};

const ui_param solo_params[] = {
	param_OCT,
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
	{
		"VEL",
		&midiVelocity,
		false,
		0,
		127,
		uiDrawValueInt
	},
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
		"A.RATE",
		NULL,
		false,
		0,
		NUM_MULTDIVS-1,
		[](int v, int x)->void {
			u8g2centerText(mdivs[v], x*32, hline*2+3, 32, uiDrawTextY);
		},
		[](int* valPtr, int newVal, int amt)->void {
			arpMdiv = newVal;
		},
		NULL,
		[]()->int {
			return (int)arpMdiv;
		}
	},
	{
		"A.DIR",
		&arpSeqSort,
		false,
		0,
		4,
		[](int v, int x)->void {
			u8g2centerText(arpSortStr[v], x*32, hline*2+3, 32, uiDrawTextY);
		},
		[](int* valPtr, int newVal, int amt)->void {
			arpSeqSort = newVal;
			arpSort();
		},
	}
};

const ui_param seq_pattern_params[] = {
	{
		"PTN",
		&viewingPattern,
		false,
		0,
		7,
		uiDrawValueInt1
	},
	{
		"DIR",
		NULL,
		false,
		0,
		1,
		[](int value, int x)->void {
			u8g2centerText(patternSettings[viewingPattern].reverse ? "<" : ">", x*32, hline*2+3, 32, uiDrawTextY);
		},
		[](int* valPtr, int newVal, int amt)->void {
			patternSettings[viewingPattern].reverse = newVal;
		},
		NULL,
		[]()->int {
			return (int)patternSettings[viewingPattern].reverse;
		}
	},
	{
		"TRSP",
		(int*)&transpose,
		false,
		-64,
		63,
		uiDrawValueNone,
		[](int* valPtr, int newVal, int amt)->void {
			transposeSeq(viewingPattern, amt);
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
			patternSettings[viewingPattern].swing = newVal;
			//setGlobalSwing(newVal);
		},
		NULL,
		[]()->int {
			return (int)patternSettings[viewingPattern].swing;
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
			if(amt > 0) {
				soloStart(viewingPattern);
			} else {
				soloStop();
			}
		},
		NULL,
		[]()->int {
			return (int)soloPattern == viewingPattern;
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
			patternSettings[viewingPattern].len = newVal;
		},
		NULL,
		[]()->int {
			return (int)patternSettings[viewingPattern].len;
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
			patternSettings[viewingPattern].clockDivMultP = newVal;
		},
		NULL,
		[]()->int {
			return (int)patternSettings[viewingPattern].clockDivMultP;
		}

	},
	{
		"CV",
		(int*)&cvPattern[viewingPattern],
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
			rotatePattern(viewingPattern, amt);;
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
			patternSettings[viewingPattern].channel = newVal;
		},
		NULL,
		[]()->int {
			return (int)patternSettings[viewingPattern].channel;
		}
	},
	{
		"PROB",
		NULL,
		false,
		0,
		100,
		uiDrawValueProb,
		[](int* valPtr, int newVal, int amt)->void {
			patternSettings[viewingPattern].prob = newVal;
		},
		NULL,
		[]()->int {
			return (int)patternSettings[viewingPattern].prob;
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
			patternSettings[viewingPattern].vel = newVal;
		},
		NULL,
		[]()->int {
			return (int)patternSettings[viewingPattern].vel;
		}
	},
	{
		"GATE",
		NULL,
		false,
		0,
		127,
		uiDrawValueInt,
		[](int* valPtr, int newVal, int amt)->void {
			patternSettings[viewingPattern].gate = newVal;
		},
		NULL,
		[]()->int {
			return (int)patternSettings[viewingPattern].gate;
		}
	},
	{
		"CHDARP",
		NULL,
		false,
		0,
		15,
		uiDrawValueInt,
		[](int* valPtr, int newVal, int amt)->void {
			patternSettings[viewingPattern].chordArp = newVal;
		},
		NULL,
		[]()->int {
			return (int)patternSettings[viewingPattern].chordArp;
		}
	},
	param_DUMMY,

	{
		"RS.START",
		NULL,
		false,
		0,
		64,
		uiDrawValueInt1,
		[](int* valPtr, int newVal, int amt)->void {
			patternSettings[viewingPattern].startstep = CLAMP(newVal, 0, patternSettings[viewingPattern].len);
		},
		NULL,
		[]()->int {
			return (int)patternSettings[viewingPattern].startstep;
		}
	},
	{
		"RS.END",
		NULL,
		false,
		0,
		64,
		uiDrawValueInt,
		[](int* valPtr, int newVal, int amt)->void {
			patternSettings[viewingPattern].autoresetstep = CLAMP(newVal, 0, patternSettings[viewingPattern].len+1);
		},
		NULL,
		[]()->int {
			return (int)patternSettings[viewingPattern].autoresetstep;
		}
	},
	{
		"RS.FREQ",
		NULL,
		false,
		0,
		15,
		uiDrawValueInt,
		[](int* valPtr, int newVal, int amt)->void {
			patternSettings[viewingPattern].autoresetfreq = newVal;
		},
		NULL,
		[]()->int {
			return (int)patternSettings[viewingPattern].autoresetfreq;
		}
	},
	{
		"RS.PROB",
		NULL,
		false,
		0,
		100,
		uiDrawValueProb,
		[](int* valPtr, int newVal, int amt)->void {
			patternSettings[viewingPattern].autoresetprob = newVal;
		},
		NULL,
		[]()->int {
			return (int)patternSettings[viewingPattern].autoresetprob;
		}
	},
};

const ui_param param_SeqNOTE = {
	"NOTE",
	NULL,
	false,
	0,
	127,
	uiDrawValueNote,
	[](int* valPtr, int newVal, int amt)->void {
		stepNoteP[viewingPattern][selectedStep].note = CLAMP(stepNoteP[viewingPattern][selectedStep].note + SGN(amt), 0, 127);
		if(stepNoteP[viewingPattern][selectedStep].trig == TRIGTYPE_MUTE) {
			stepNoteP[viewingPattern][selectedStep].trig = TRIGTYPE_PLAY;
		}
	},
	NULL,
	[]()->int {
		return (int)stepNoteP[viewingPattern][selectedStep].note;
	}
};

const ui_param param_SeqVEL = {
	"VEL",
	NULL,
	false,
	0,
	127,
	uiDrawValueInt,
	[](int* valPtr, int newVal, int amt)->void {
		stepNoteP[viewingPattern][selectedStep].vel = newVal;
	},
	NULL,
	[]()->int {
		return (int)stepNoteP[viewingPattern][selectedStep].vel;
	}
};

const ui_param param_SeqNLEN = {
	"LEN",
	NULL,
	false,
	0,
	127,
	uiDrawValueInt1,
	[](int* valPtr, int newVal, int amt)->void {
		stepNoteP[viewingPattern][selectedStep].len = newVal;
	},
	NULL,
	[]()->int {
		return (int)stepNoteP[viewingPattern][selectedStep].len;
	}
};

const ui_param param_SeqCHORD = {
	"CHORD",
	NULL,
	false,
	0,
	15,
	uiDrawValueChord,
	[](int* valPtr, int newVal, int amt)->void {
		stepNoteP[viewingPattern][selectedStep].chord = newVal;
	},
	NULL,
	[]()->int {
		return (int)stepNoteP[viewingPattern][selectedStep].chord;
	}
};

const ui_param param_SeqCHORDARP = {
	"CHARP",
	NULL,
	false,
	0,
	15,
	uiDrawValueInt,
	[](int* valPtr, int newVal, int amt)->void {
		stepNoteP[viewingPattern][selectedStep].chordArp = newVal;
	},
	NULL,
	[]()->int {
		return (int)stepNoteP[viewingPattern][selectedStep].chordArp;
	}
};

const ui_param param_SeqSTEP = {
	"STEP",
	NULL,
	false,
	0,
	127,
	uiDrawValueInt1,
	[](int* valPtr, int newVal, int amt)->void {
		selectedStep = CLAMP(newVal, 0, PatternLength(viewingPattern)-1);
	},
	NULL,
	[]()->int {
		return (int)selectedStep;
	}
};

const ui_param seq_note_select_params[] = {
	param_SeqSTEP,
	param_SeqNOTE,
	param_SeqVEL,
	param_SeqNLEN,
	///
	{
		"TYPE",
		NULL,
		false,
		0,
		STEPTYPE_COUNT-1,
		[](int value, int x)->void {
			u8g2centerText(stepTypes[stepNoteP[viewingPattern][selectedStep].stepType], x*32, hline*2+3, 32, uiDrawTextY);
		},
		[](int* valPtr, int newVal, int amt)->void {
			stepNoteP[viewingPattern][selectedStep].stepType = (StepType)newVal;
		},
		NULL,
		[]()->int {
			return (int)stepNoteP[viewingPattern][selectedStep].stepType;
		}
	},
	{
		"PROB",
		NULL,
		false,
		0,
		101,
		uiDrawValueProb,
		[](int* valPtr, int newVal, int amt)->void {
			stepNoteP[viewingPattern][selectedStep].prob = newVal;
		},
		NULL,
		[]()->int {
			return (int)stepNoteP[viewingPattern][selectedStep].prob;
		}
	},
	{
		"COND",
		NULL,
		false,
		0,
		127,
		[](int value, int x)->void {
			u8g2centerText(trigConditions[stepNoteP[viewingPattern][selectedStep].condition], x*32, hline*2+3, 32, uiDrawTextY);
		},
		[](int* valPtr, int newVal, int amt)->void {
			stepNoteP[viewingPattern][selectedStep].condition = newVal;
		},
		NULL,
		[]()->int {
			return (int)stepNoteP[viewingPattern][selectedStep].condition;
		}
	},
	param_SeqCHORD,
	param_SeqCHORDARP,
	///
	{
		"L-1",
		NULL,
		false,
		0,
		127,
		uiDrawValueInt,
		[](int* valPtr, int newVal, int amt)->void {
			stepNoteP[viewingPattern][selectedStep].params[0] = newVal;
		},
		NULL,
		[]()->int {
			return (int)stepNoteP[viewingPattern][selectedStep].params[0];
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
			stepNoteP[viewingPattern][selectedStep].params[1] = newVal;
		},
		NULL,
		[]()->int {
			return (int)stepNoteP[viewingPattern][selectedStep].params[1];
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
			stepNoteP[viewingPattern][selectedStep].params[2] = newVal;
		},
		NULL,
		[]()->int {
			return (int)stepNoteP[viewingPattern][selectedStep].params[2];
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
			stepNoteP[viewingPattern][selectedStep].params[3] = newVal;
		},
		NULL,
		[]()->int {
			return (int)stepNoteP[viewingPattern][selectedStep].params[3];
		}
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

		// turn on/off any pending notes
		pendingMidi.play(micros());
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

void setGlobalSwing(int swing_amt){
	for(int z=0; z<NUM_PATTERNS; z++) {
		patternSettings[z].swing = swing_amt;
	}
}

// ####### POTENTIMETERS #######

void sendPots(int val, int channel){
	MM::sendControlChange(pots[potbank][val], analogValues[val], channel);
	potCC = pots[potbank][val];
	potVal = analogValues[val];
	potValues[val] = potVal;
}

void sendCC(int pot, int value, int channel) {
	if(pots[potbank][pot] == -1) {
		// velocity
		midiVelocity = value;
	} else if(pots[potbank][pot] == -2) {
		// pitch bend
		//MM::sendPitchBend(map(raw, potMin, potMax, MIDI_PITCHBEND_MIN, MIDI_PITCHBEND_MAX), channel);
	} else if(pots[potbank][pot] == -3) {
		// program change
		MM::sendProgramChange(value, channel);
	} else {
		MM::sendControlChange(pots[potbank][pot], value, channel);
	}
}

void readPotentimeters(){
	for(int k=0; k<potCount; k++) {
		temp = analogRead(analogPins[k]);
		analog[k]->update(temp);

		// read from the smoother, constrain (to account for tolerances), and map it
		temp = analog[k]->getValue();
		int raw = constrain(temp, potMin, potMax);
		temp = map(raw, potMin, potMax, 0, 16383);

		// map and update the value
		analogValues[k] = temp >> 7;

		if(analog[k]->hasChanged()) {
			 // do stuff
			switch(omxMode) {
				case MODE_OM:
						// fall through - same as MIDI
				case MODE_MIDI: // MIDI
					if(pots[potbank][k] == -1) {
						midiVelocity = analogValues[k];
						dirtyDisplay = true;
					} else if(pots[potbank][k] == -2) {
						MM::sendPitchBend(map(raw, potMin, potMax, MIDI_PITCHBEND_MIN, MIDI_PITCHBEND_MAX), midiChannel);
					} else if(pots[potbank][k] == -3) {
						// program change
						MM::sendProgramChange(analogValues[k], midiChannel);
					} else {
						sendPots(k, midiChannel);
						dirtyDisplay = true;
					}
					dirtyDisplay = true;
					break;

				case MODE_S2: // SEQ2
						// fall through - same as SEQ1
				case MODE_S1: // SEQ1
					if (seqStepHold){ // note selection - do P-Locks
						potNum = k;
						potCC = pots[potbank][k];
						potVal = analogValues[k];

						if (k < 4){ // only store p-lock value for first 4 knobs
							stepNoteP[viewingPattern][selectedStep].params[k] = analogValues[k];
						} else if (k == 4){
							stepNoteP[viewingPattern][selectedStep].vel = analogValues[k]; // SET POT 5 to NOTE VELOCITY HERE
						}

						if(sendPotsDuringPLock) {
							sendPots(k, PatternChannel(viewingPattern));
						}
						setScale(scaleRoot, scalePattern);
						dirtyPixels = true;
						dirtyDisplay = true;

					} else if (stepRecord){
						potNum = k;
						potCC = pots[potbank][k];
						potVal = analogValues[k];

						if (k < 4){ // only store p-lock value for first 4 knobs
							stepNoteP[viewingPattern][seqPos[viewingPattern]].params[k] = analogValues[k];
							if(sendPotsDuringPLock) {
								sendPots(k, PatternChannel(viewingPattern));
							}
						} else if (k == 4){
							stepNoteP[viewingPattern][seqPos[viewingPattern]].vel = analogValues[k]; // SET POT 5 to NOTE VELOCITY HERE
						}
						dirtyDisplay = true;
					} else {
						if(F1_HELD) { // record p-lock
							stepNoteP[viewingPattern][seqPos[viewingPattern]].params[k] = analogValues[k];
						} else if(F2_HELD) { // clear p-lock
							stepNoteP[viewingPattern][seqPos[viewingPattern]].params[k] = -127;
						}
						if(k == 4 && soloPattern == viewingPattern) {
							midiVelocity = analogValues[k]; // SET POT 5 to NOTE VELOCITY HERE
						} else {
							sendPots(k, PatternChannel(viewingPattern));
						}
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
		nextPlayingPattern = -1;
		viewingPattern = 0;
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

	// setup UI
	snprintf(noteBuffer, 4, "---");
	snprintf(ccNameBuf, 6, "CC---");

	// ensure copy buffer is empty
	for(int i = 0; i < NUM_STEPS; i++) {
		copyPatternBuffer[i] = {0, 0, 0, TRIGTYPE_MUTE, { -1, -1, -1, -1, -1}, 100, 0, STEPTYPE_NONE };
	}

	arpReset();

	delay(100);

	// Clear display
	display.display();

	dirtyDisplay = true;
}

// ####### END SETUP #######



int getDefaultColor(int pixel) {
	if(scalePattern == -1) {
		return LEDOFF;
	} else {
		if(omxMode == MODE_MIDI && AUX_HELD) {
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

	if (omxMode == MODE_MIDI && AUX_HELD){
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

void drawPatternSteps(int pattern) {
	auto currentpage = getPatternPage(selectedStep);
	auto pagestepstart = (currentpage * NUM_STEPKEYS);

	auto trigColor = patternSettings[pattern].mute ? muteColors[pattern] : seqColors[pattern];
	
	// WHAT TO DO HERE FOR MULTIPLE PAGES
	// NUM_STEPKEYS or NUM_STEPS INSTEAD?
	for(int i = pagestepstart; i < pagestepstart + NUM_STEPKEYS; i++){
		auto pixelpos = i - pagestepstart + 11;
		if(i < PatternLength(pattern)) {
			// default to blank or marker on every 4th
			stepColor = i % 4 == 0 ? SEQMARKER : LEDOFF;

			bool trig = stepNoteP[pattern][i].trig == TRIGTYPE_PLAY;
			if(trig) {
				stepColor = trigColor;
			}

			if(params == seq_note_select_params) {
				if(trig) {
					if(*param == 1) { // would prefer to use pointer comparison for this, but it doesn't seem to work, not sure why
						// note
						int note = stepNoteP[pattern][i].note;
						stepColor = strip.gamma32(strip.ColorHSV((65535 / 12) * ((note - scaleRoot) % 12), rainbowSaturation, 255));
					} else if(*param == 2) {
						// vel
						int vel = stepNoteP[pattern][i].vel;
						stepColor = strip.gamma32(strip.ColorHSV((65535 / 8) * 0, rainbowSaturation, CLAMP(vel, 64, 255)));
					} else if(*param == 3) {
						// note length
						int len = stepNoteP[pattern][i].len;
						stepColor = strip.gamma32(strip.ColorHSV((65535 / 8) * 1, rainbowSaturation, CLAMP(len * 48, 64, 255)));
					} else if(*param == 5) {
						// prob
						int prob = stepNoteP[pattern][i].prob;
						if(prob > 100) {
							stepColor = strip.gamma32(strip.ColorHSV(0, 0, 255));
						} else {
							stepColor = strip.gamma32(strip.ColorHSV((65535 / 8) * 2, rainbowSaturation, CLAMP(prob * 2, 64, 255)));
						}
					}
				}
				if(*param == 0) {
					// highlight current step when step param selected
					stepColor = (i == selectedStep && slowBlinkState) ? HALFWHITE : stepColor;
				}
			}

			if(playing && i == seqPos[pattern]) {
				stepColor = SEQCHASE; // step chase
			}
			strip.setPixelColor(pixelpos, stepColor);
		} else {
			strip.setPixelColor(pixelpos, LEDOFF);
		}
	}
}

void drawPatternTweaks(int pattern) {
	// TURN OFF STEP LEDS
	for(int j = 11; j < 27; j++){
		strip.setPixelColor(j, LEDOFF);
	}
	// transpose keys
	strip.setPixelColor(11, ORANGE);
	strip.setPixelColor(12, RBLUE);
	strip.setPixelColor(25, RBLUE);
	strip.setPixelColor(26, ORANGE);

	// solo
	strip.setPixelColor(11 + 2, soloPattern == pattern ? MAGENTA : DKMAGENTA);

	// page lock
	strip.setPixelColor(11 + 3, pageLock[pattern] ? GREEN : DKDKGREEN);

	// draw the leds for the pages, highlight viewing and playing
	auto len = PatternPages(pattern);
	auto currentpage = patternPage[pattern];
	auto playingPage = playing ? (seqPos[pattern] / NUM_STEPKEYS) : -1;
	for(int h = 0; h < 4; h++){
		auto color = h < len ? seqColors[pattern] : DKDKPURPLE;
		if (h == currentpage && h == playingPage) {
			color = blinkState ? HALFWHITE : SEQCHASE;
		} else if (h == currentpage) {
			color = HALFWHITE;
		} else if (h == playingPage) {
			color = blinkState ? color : SEQCHASE;
		}
		strip.setPixelColor(11 + 4 + h, color);
	}

	// mute / cue
	if(omxMode == MODE_S1) {
		strip.setPixelColor(11 + 8, (playingPattern == pattern || (nextPlayingPattern == pattern && blinkState)) ? GREEN : DKDKGREEN);
	} else {
		strip.setPixelColor(11 + 8, patternSettings[pattern].mute ? DKDKRED : RED);
	}
	// restart
	strip.setPixelColor(11 + 9, CYAN);
	// reverse
	strip.setPixelColor(11 + 10, patternSettings[pattern].reverse ? MAGENTA : DKDKMAGENTA);
	// toggle FX
	strip.setPixelColor(11 + 11, disableFX[pattern] ? DKDKYELLOW : YELLOW);
	// toggle plock
	strip.setPixelColor(11 + 12, disablePLock[pattern] ? DKDKBLUE : BLUE);
}

void drawPatternIndicators() {
	// function keys
	strip.setPixelColor(1, FUNKONE);
	strip.setPixelColor(2, FUNKTWO);

	for(int p = 0; p < NUM_PATTERNS; p++){
		const int pixel = PATTERN_TO_KEY(p);
		if(omxMode == MODE_S1) {
			// highlight currently viewed and playing
			if(playing && p == playingPattern) {
				strip.setPixelColor(pixel, blinkState ? SEQCHASE : seqColors[p]);
			} else if(p == viewingPattern) {
				strip.setPixelColor(pixel, seqColors[p]);
			}
		} else {
			// highlight current viewing and show mutes
			if(viewingPattern == p) {
				if(params == seq_pattern_params) {
					strip.setPixelColor(pixel, blinkState ? (patternSettings[p].mute ? DKDKRED : HALFWHITE) : seqColors[p]);
				} else {
					strip.setPixelColor(pixel, seqColors[p]);
				}
			} else {
				if(patternSettings[p].mute) {
					strip.setPixelColor(pixel, DKDKRED);
				} else {
					if(patternActive[p]) {
						strip.setPixelColor(pixel, muteColors[p]);
						patternActive[p] = 0;
					} else {
						strip.setPixelColor(pixel, LEDOFF);
					}
				}
			}
		}
	}
}

void drawSeqLEDs(int patternNum) {
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

	if ((seqStepHold && *param == 1) || seqStepNoteSelect) {
		if(showScaleInSeq || seqStepNoteSelect) {
			// show the scale for reference
			for(int j = 1; j < 27; j++){
				if(KEY_TO_STEP(j) == selectedStep) {
					//int note = stepNoteP[viewingPattern][KEY_TO_STEP(j)].note;
					//strip.setPixelColor(j, blinkState ? MIDINOTEON : strip.gamma32(strip.ColorHSV((65535 / 12) * ((note - scaleRoot) % 12), rainbowSaturation, 255)));
					strip.setPixelColor(j, blinkState ? MIDINOTEON : getDefaultColor(j));
				} else {
					strip.setPixelColor(j, getDefaultColor(j));
				}
			}
		} else {
			// show notes for trig steps
			for(int j = 11; j < 27; j++){
				int step = pagestepstart + KEY_TO_STEP(j);
				if(stepNoteP[viewingPattern][step].trig == TRIGTYPE_PLAY) {
					int note = stepNoteP[viewingPattern][step].note;
					strip.setPixelColor(j, strip.gamma32(strip.ColorHSV((65535 / 12) * ((note - scaleRoot) % 12), rainbowSaturation, 255)));
				} else {
					strip.setPixelColor(j, LEDOFF);
				}
			}

		}
	} else if (stepRecord) {
		for(int j = pagestepstart; j < (pagestepstart + NUM_STEPKEYS); j++){	// NUM_STEPKEYS or NUM_STEPS INSTEAD?>
			auto pixelpos = j - pagestepstart + 11;

			if (j < PatternLength(patternNum)){ 
				// ONLY DO LEDS FOR THE CURRENT PAGE

				if (j == selectedStep){
					strip.setPixelColor(pixelpos, SEQCHASE);
				} else if (pixelpos != selectedNote){
					strip.setPixelColor(pixelpos, LEDOFF);
				}
			} else  {
				strip.setPixelColor(pixelpos, LEDOFF);
			}
		}
	} else if (soloPattern == viewingPattern) {
		// show scale, same as midi mode
		for(int j = 1; j < 27; j++){
			strip.setPixelColor(j, getDefaultColor(j));
		}
	} else if (patternEdit) {
		drawPatternTweaks(viewingPattern);
		drawPatternIndicators();
	} else {
		drawPatternSteps(viewingPattern);
		drawPatternIndicators();
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
	if(param->label == NULL && param->getLabel == NULL){
		u8g2centerText("", (n*32) + 1, hline-2, 32, 10);
	} else {
		u8g2centerText(param->getLabel != NULL ? param->getLabel() : param->label, (n*32) + 1, hline-2, 32, 10);
	}
}

void dispValBox(const ui_param* param, int16_t n, bool selected){			// n is box 0-3
	if(selected) {
		u8g2_display.setForegroundColor(BLACK);
		u8g2_display.setBackgroundColor(WHITE);
	} else {
		u8g2_display.setForegroundColor(WHITE);
		u8g2_display.setBackgroundColor(BLACK);
	}
    if(param->getValue == NULL && param->valuePtr == NULL) {
        uiDrawValueNone(0, n);
    } else {
        param->drawValue(param->getValue != NULL ? param->getValue() : *param->valuePtr, n);
    }
}

void dispSymbBox(const char* v, int16_t n, bool inv){			// n is box 0-3
	invertColor(inv);
	u8g2centerText(v, n*32, hline*2+3, 32, uiDrawTextY);
}

void dispGenericMode(const ui_param *params, int numParams, int selected){
	int dispPage = selected / 4;
	int start = dispPage * 4;
	int selectedOnPage = selected % 4;

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

	bool edit = uiEditParam || AUX_HELD || seqStepHold || patternHold;


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

// MESSAGE DISPLAY
const int MESSAGE_TIMEOUT_US = 700000;
int messageDisplayTimer = 0;

void displayMessageEx(const uint8_t* font, const int time, const char* msg) {
	Serial.print("displayMessageEx ");
	Serial.println(msg);
	display.fillRect(0, 0, 128, 32, BLACK);
	u8g2_display.setFontMode(1);
	u8g2_display.setFont(font);
	u8g2_display.setForegroundColor(WHITE);
	u8g2_display.setBackgroundColor(BLACK);
	u8g2centerText(msg, 0, 10, 128, 32);

	// nothing else will be drawn until this timer expires
	messageDisplayTimer = time;
	dirtyDisplay = true;
}

void displayMessagefEx(const uint8_t* font, const int time, const char* fmt, ...) {
	va_list args;
	va_start(args, fmt);
	char buf[24];
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
	displayMessageEx(font, time, buf);
}

void clearMessage() {
	if(messageDisplayTimer > 0) {
		Serial.println("clearMessage");
		messageDisplayTimer = 1;
		dirtyDisplay = true;
	}
}

#define displayMessage(msg) displayMessageEx(FONT_VALUES, MESSAGE_TIMEOUT_US, msg)
#define displayMessagef(...) displayMessagefEx(FONT_VALUES, MESSAGE_TIMEOUT_US, __VA_ARGS__)

int dangerConfirm = -1;
int dangerConfirmTimer = 0;
#define DANGER_CONFIRM_TIME 700000

// macro that allows us to wrap a block of code in a confirm dialog requiring double press of key
#define DANGER_CONFIRM(key, msg, ...) { \
	if(dangerConfirm != key) { \
		displayMessagefEx(FONT_VALUES, DANGER_CONFIRM_TIME, msg); \
		dangerConfirm = (key); \
		dangerConfirmTimer = DANGER_CONFIRM_TIME; \
	} else { \
		__VA_ARGS__ \
	} \
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

void changeParamValue(const ui_param* params, int param, int amt) {
	if(params[param].readOnly == false) {
		int newValue = params[param].getValue != NULL ? params[param].getValue() : *params[param].valuePtr;
		newValue = constrain(newValue + amt, params[param].min, params[param].max);
		if(params[param].onChange != NULL) {
			params[param].onChange(params[param].valuePtr, newValue, amt);
		} else {
			*params[param].valuePtr = newValue;
		}
		dirtyDisplay = true;
	}
}

void setParamModeMidi() {
	param = &miparam;
	params = midi_params;
	numParams = ARRAYLEN(midi_params);
	if(currentParam != &params[*param]) {
		dirtyDisplay = true;
	}
	currentParam = &params[*param];
}

void setParamModeSolo() {
	param = &soloParam;
	params = solo_params;
	numParams = ARRAYLEN(solo_params);
	if(currentParam != &params[*param]) {
		dirtyDisplay = true;
	}
	currentParam = &params[*param];
}

void setParamModePattern() {
	param = &sqparam;
	params = seq_pattern_params;
	numParams = ARRAYLEN(seq_pattern_params);
	if(currentParam != &params[*param]) {
		dirtyDisplay = true;
	}
	currentParam = &params[*param];
}

void setParamModeNoteSelect() {
	param = &nsparam;
	params = seq_note_select_params;
	numParams = ARRAYLEN(seq_note_select_params);
	if(currentParam != &params[*param]) {
		dirtyDisplay = true;
	}
	currentParam = &params[*param];
}

void setParamModeConfig() {
	param = &configparam;
	params = config_params;
	numParams = ARRAYLEN(config_params);
	if(currentParam != &params[*param]) {
		dirtyDisplay = true;
	}
	currentParam = &params[*param];
}

bool handleKeyEventCommon(keypadEvent e) {
	int thisKey = e.bit.KEY;

	if (e.bit.EVENT == KEY_JUST_PRESSED && thisKey == 0) {
		if(F1_HELD && F2_HELD) {
			seqStop();
			allNotesOffPanic();
			playing = false;
			invalidateShortPress[0] = true;
			DANGER_CONFIRM(0, "CLEAR ALL DATA?", {
				initPatterns();
				displayMessagef("DATA CLEARED");
				dangerConfirm = false;
			});
			return true;
		}
		// new press on AUX
		invalidateShortPress[0] = false;
		dirtyDisplay = true;
		return false;
	} else if (e.bit.EVENT == KEY_JUST_RELEASED && thisKey == 0) {
		dirtyDisplay = true;

		strip.setPixelColor(0, LEDOFF);
		strip.setPixelColor(1, getDefaultColor(1));
		strip.setPixelColor(2, getDefaultColor(2));
		strip.setPixelColor(3, getDefaultColor(3));
		strip.setPixelColor(4, getDefaultColor(4));
		strip.setPixelColor(11, getDefaultColor(11));
		strip.setPixelColor(12, getDefaultColor(12));

		return false;
	}
	if (e.bit.EVENT == KEY_JUST_PRESSED && thisKey != 0) {
		// pressing any other key invalidates short press on AUX
		invalidateShortPress[0] = true;
	}
	// common controls for editing
	if (AUX_HELD && e.bit.EVENT == KEY_JUST_PRESSED) {
		if(!seqStepNoteSelect) {
			if (thisKey == 1 || thisKey == 2) {
				// allow changing param selection
				int chng = thisKey == 1 ? -1 : 1;
				*param = WRAP(*param + chng, numParams);
				dirtyDisplay = true;
				return true;
			} else if (thisKey == 11 || thisKey == 12){
				int amt = thisKey == 11 ? -1 : 1;
				newoctave = constrain(octave + amt, minOctave, maxOctave);
				if (newoctave != octave){
					octave = newoctave;
				}
				return true;
			} else if (thisKey == 13 || thisKey == 14) {
				// allow changing param value
				int amt = thisKey == 13 ? -1 : 1;
				changeParamValue(params, *param, amt);
				return true;
			}
		}
	}

	if (e.bit.EVENT == KEY_JUST_PRESSED && thisKey == 0 && mode_select) {
		// temp - save whenever the 0 key is pressed in encoder edit mode
		saveToStorage();
		//	Serial.println("EEPROM saved");
		return true;
	}
	return false;
}

bool handleKeyEventMidi(keypadEvent e, int pattern) {
	int thisKey = e.bit.KEY;
	//int keyPos = thisKey - 11;
	bool down = e.bit.EVENT == KEY_JUST_PRESSED;

	if(down == false && thisKey != 0) {
		scaleSelectHold = false;
	}

	if(AUX_HELD) {
		if(down && thisKey == 3) {
			arp = !arp;
			if(arp) {
				displayMessage("ARP ON");
			} else {
				displayMessage("ARP OFF");
			}
			if(arp == false && omxMode == MODE_MIDI) {
				allNotesOff();
			}
			arpReset();
			return true;
		} else if (down && thisKey == 4) {
			arpLatch = !arpLatch;
			if(arpLatch) {
				displayMessage("LATCH ON");
			} else {
				displayMessage("LATCH OFF");
			}
			if(arpLatch == false) {
				arpReset();
			}
			return true;
		} else if(down && thisKey == 26) {
			// disable scales
			if(scalePattern != -1) {
				displayMessage("SCALES OFF");
			}
			scalePattern = -1; // disable scales
			setScale(0, -1);
			for(int n = 1; n < 27; n++) {
				strip.setPixelColor(n, getDefaultColor(n));
			}
			strip.show();
			dirtyPixels = true;
			return true;
		} else if((thisKey >= 6 && thisKey <= 10) || thisKey >= 19) {
			if(down) {
				// set scale
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
				displayMessagef("%s %s", noteNames[scaleRoot], scaleNames[scalePattern]);
				dirtyPixels = true;
				return true;
			}
		}
	} else if(down) {
		if(pattern > -1) {
			midiNoteOn(thisKey, midiVelocity, PatternChannel(pattern));
		} else {
			midiNoteOn(thisKey, midiVelocity, midiChannel);
		}
	} else {
		if(pattern > -1) {
			midiNoteOff(thisKey, PatternChannel(pattern));
		} else {
			midiNoteOff(thisKey, midiChannel);
		}
	}
	return true;
}

bool handleKeyEventSeq(keypadEvent e) {
	int thisKey = e.bit.KEY;
	int keyPos = thisKey - 11;
	int seqKey = keyPos + (patternPage[viewingPattern] * NUM_STEPKEYS);
	bool down = e.bit.EVENT == KEY_JUST_PRESSED;

	if (thisKey > 0 && soloPattern != -1) {
		// MIDI SOLO
		return handleKeyEventMidi(e, soloPattern);
	} else if(seqStepNoteSelect) {
		if(down && thisKey == 0) {
			// cancel note select
			seqStepNoteSelect = false;
			invalidateShortPress[0] = true;
			invalidateShortPress[thisKey] = true;
			displayMessage("CANCELED");
			dirtyPixels = true;
			return true;
		} else if(down) {
			// enter note value and exit mode
			int note = notes[thisKey] + (octave * 12);
			stepNoteP[viewingPattern][selectedStep].note = note;
			stepNoteP[viewingPattern][selectedStep].trig = TRIGTYPE_PLAY;
			dirtyDisplay = true;
			dirtyPixels = true;
			invalidateShortPress[0] = true;
			invalidateShortPress[thisKey] = true;
			if(!AUX_HELD) {
				seqStepNoteSelect = false;
				clearMessage();
			}
			return true;
		}
		return false;
	}
	if(patternEdit == false && seqStepHold == false) {
		if(down == false && (thisKey == 1 || thisKey == 2)) {
			clearMessage();
			if(F1_HELD) {
				displayMessage("COPY");
			} else if(F2_HELD) {
				if(AUX_HELD) {
					displayMessage("MUTE");
				} else {
					displayMessage("PASTE");
				}
			}
			dangerConfirm = false;
		}
		if(down && ((thisKey == 1 && F2_HELD) || (thisKey == 2 && F1_HELD))) {
			displayMessage("CUT");
			dangerConfirm = false;
		} else if(down && thisKey == 1) {
			displayMessage("COPY");
			dangerConfirm = false;
		} else if(down && thisKey == 2) {
			if(AUX_HELD) {
				displayMessage("MUTE");
			} else {
				displayMessage("PASTE");
			}
			dangerConfirm = false;
		}
	}
	if (seqStepHold) {
		if(down && thisKey == 0) {
			invalidateShortPress[0] = true;
			displayMessagef("NOTE SEL S%d", selectedStep);
			seqStepNoteSelect = true;
			seqStepHold = false;
			seqStepEdit = false;
			dirtyPixels = true;
			return true;
		}
		if(down && thisKey > 0) {
			// while holding down sequence step, you can press a keyboard key to enter that note for that step
			// you can enter the note under the current step by holding any other note, releasing the step and then pressing the step again
			int adjnote = notes[thisKey] + (octave * 12);
			stepNoteP[viewingPattern][selectedStep].note = adjnote;
			stepNoteP[viewingPattern][selectedStep].trig = TRIGTYPE_PLAY;
			invalidateShortPress[11+seqStepHold] = true;
			// play note if stopped
			if(!playing) {
				if(previewLastNote != -1) {
					rawNoteOff(previewLastNote, previewLastChannel);
				}
				rawNoteOn(adjnote, stepNoteP[viewingPattern][selectedStep].vel, PatternChannel(viewingPattern));
				previewLastNote = adjnote;
				previewLastChannel = PatternChannel(viewingPattern);
			}
			dirtyDisplay = true;
		}
	} else if(down && stepRecord) {
		if(thisKey == 0) {
			// exit step record
			stepRecord = false;
			stepDirty = true;
			dirtyDisplay = true;
		} else {
			// STEP RECORD
			selectedNote = thisKey;
			int adjnote = notes[thisKey] + (octave * 12);
			stepNoteP[viewingPattern][selectedStep].note = adjnote;
			stepNoteP[viewingPattern][selectedStep].trig = TRIGTYPE_PLAY;

			if (!playing){
				midiNoteOn(thisKey, defaultVelocity, PatternChannel(viewingPattern));
			} // see RELEASE events for more
			stepDirty = true;
			dirtyDisplay = true;

			// increment step
			selectedStep++;
			if(selectedStep >= PatternLength(viewingPattern)) {
				stepRecord = false;
				displayMessage("DONE");
			} else {
				displayMessagef("STEP REC %d/%d", selectedStep+1, PatternLength(viewingPattern));
			}
		}
	} else {
		// REGULAR SEQ MODE
		// BLACK KEYS - SELECT PATTERN
		if (patternHold && down && IS_PATTERN(thisKey)) { // WHILE HOLDING PATTERN
			if(patternEditLock) {
				patternEditLock = false;
				patternHold = false;
				patternEdit = false;
				dirtyDisplay = true;
				dirtyPixels = true;
			} else {
				patternEditLock = true;
			}
			return true;
		} else if(down && IS_PATTERN(thisKey)) {
			if(AUX_HELD && F1_HELD == false && F2_HELD == false && IS_PATTERN(thisKey)) {
				invalidateShortPress[0] = true;
				if(playing) {
					if(omxMode == MODE_S1) {
						// cue next pattern
						nextPlayingPattern = thisKey-3;
						seqPos[nextPlayingPattern] = 0;
						displayMessagef("CUE P%d", nextPlayingPattern+1);
					} else {
						// toggle mute for pattern
						patternSettings[KEY_TO_PATTERN(thisKey)].mute = !patternSettings[KEY_TO_PATTERN(thisKey)].mute;
						if(patternSettings[KEY_TO_PATTERN(thisKey)].mute) {
							displayMessagef("MUTED P%d", thisKey-3);
						} else {
							displayMessagef("UNMUTED P%d", thisKey-3);
						}
					}
				} else {
					// step record
					viewingPattern = thisKey-3;
					selectedStep = 0;
					stepRecord = true;
					dirtyDisplay = true;
					displayMessagef("STEP REC %d/%d", selectedStep+1, PatternLength(viewingPattern));
					invalidateShortPress[0] = true;
				}
				return true;
			} else if (F1_HELD && F2_HELD) {
				DANGER_CONFIRM(thisKey, "CONFIRM CUT?", {
					copyPattern(KEY_TO_PATTERN(thisKey));
					clearPattern(KEY_TO_PATTERN(thisKey));
					displayMessagef("CUT P%d", KEY_TO_PATTERN(thisKey)+1);
					dangerConfirm = false;
				});
			} else if(F1_HELD) {
				// copy pattern
				copyPattern(KEY_TO_PATTERN(thisKey));
				displayMessagef("COPIED P%d", KEY_TO_PATTERN(thisKey)+1);
			} else if(F2_HELD) {
				// paste pattern
				DANGER_CONFIRM(thisKey, "CONFIRM PASTE?", {
					pastePattern(KEY_TO_PATTERN(thisKey));
					displayMessagef("PASTED P%d", KEY_TO_PATTERN(thisKey)+1);
					dangerConfirm = false;
				});
			} else {
				// switch pattern
				int prevPattern = viewingPattern;
				viewingPattern = KEY_TO_PATTERN(thisKey);
				if(viewingPattern != prevPattern) {
					selectedStep = patternPage[viewingPattern] * NUM_STEPKEYS;
					if(!playing) {
						seqPos[viewingPattern] = patternPage[viewingPattern] * NUM_STEPKEYS;
					}
				} else {
					patternEditLock = false;
				}
				seqStepEdit = false;
				seqStepHold = false;
				patternHold = true;
				patternEdit = true;
				dirtyDisplay = true;
				dirtyPixels = true;

				//snprintf(messageText, MESSAGE_TEXT_LEN, "P%d PAGE SELECT", viewingPattern+1);

				setParamModePattern();

				if(!playing) {
					playingPattern = viewingPattern;
				}
			}
		} else if(down && IS_STEP(thisKey)) {
			// SEQUENCE 1-16 STEP KEYS
			if (patternEdit) { // WHILE HOLDING PATTERN
				if(F1_HELD && F2_HELD) { // cut page
					if(KEY_TO_PAGE(thisKey) >= 0 && KEY_TO_PAGE(thisKey) < 4) {
						if(clearedPage == KEY_TO_PAGE(thisKey) && clearedPageTimer > 0) {
							// using three times will remove page
							int newPage = KEY_TO_PAGE(thisKey)-1;
							SetPatternLength(viewingPattern, (newPage+1) * NUM_STEPKEYS);
							clearedPage = -1;
							clearedPageTimer = 0;
							displayMessagef("REM PAGE %d", KEY_TO_PAGE(thisKey)+1);
							if(selectedStep > PatternLength(viewingPattern)) {
								selectedStep = CLAMP(selectedStep, 0, PatternLength(viewingPattern)-1);
								patternPage[viewingPattern] = getPatternPage(selectedStep);
							}
						} else {
							DANGER_CONFIRM(thisKey, "CONFIRM CUT?", {
								copyPage(viewingPattern, KEY_TO_PAGE(thisKey));
								clearPage(viewingPattern, KEY_TO_PAGE(thisKey));
								displayMessagef("CUT PAGE %d", KEY_TO_PAGE(thisKey)+1);

								clearedPage = KEY_TO_PAGE(thisKey);
								clearedPageTimer = shortPressInterval * 2;
								dangerConfirm = false;
							});
						}
					}
				} else if(F1_HELD) { // copy page
					if(KEY_TO_PAGE(thisKey) >= 0 && KEY_TO_PAGE(thisKey) < 4) {
						copyPage(viewingPattern, KEY_TO_PAGE(thisKey));
						displayMessagef("COPIED PAGE %d", KEY_TO_PAGE(thisKey)+1);
					}
				} else if(F2_HELD) { // paste page
					if(KEY_TO_PAGE(thisKey) >= 0 && KEY_TO_PAGE(thisKey) < 4) {
						DANGER_CONFIRM(thisKey, "CONFIRM PASTE?", {
							pastePage(viewingPattern, KEY_TO_PAGE(thisKey));
							displayMessagef("PASTED PAGE %d", KEY_TO_PAGE(thisKey)+1);
							dangerConfirm = false;
							// TODO: extend pattern to fit
						});
					}
				} else {
					if(KEY_TO_PAGE(thisKey) >= 0 && KEY_TO_PAGE(thisKey) < 4) {
						// select pattern page
						int lastPage = patternPage[viewingPattern];
						int newPage = KEY_TO_PAGE(thisKey);
						if(newPage >= PatternPages(viewingPattern)) {
							DANGER_CONFIRM(thisKey, "CONFIRM EXTEND?", {
								// extend pattern
								SetPatternLength(viewingPattern, (newPage+1) * NUM_STEPKEYS);
								dirtyDisplay = true;
								dirtyPixels = true;
								dangerConfirm = 0;
								displayMessagef("P%d len: %d", viewingPattern, PatternLength(viewingPattern));
							});
						}
						patternPage[viewingPattern] = CLAMP(newPage, 0, PatternPages(viewingPattern)-1);
						if(patternPage[viewingPattern] != lastPage) {
							selectedStep = patternPage[viewingPattern] * NUM_STEPKEYS;
							if(AUX_HELD) {
								// jump play position to start of page
								playingPattern = viewingPattern;
								seqPos[playingPattern] = selectedStep;
							}
							if(!playing) {
								seqPos[viewingPattern] = selectedStep;
							}
							patternPage[viewingPattern] = getPatternPage(selectedStep);
							dirtyPixels = true;
							dirtyDisplay = true;
						}
					} else if(KEY_TO_STEP(thisKey) == 2) {
						// solo
						soloStart(viewingPattern);
					} else if(KEY_TO_STEP(thisKey) == 3) {
						// page lock
						pageLock[viewingPattern] = !pageLock[viewingPattern];
						dirtyPixels = true;
					} else if(KEY_TO_STEP(thisKey) == 0 || KEY_TO_STEP(thisKey) == 15) {
						// transpose pattern by octaves
						int transpose = KEY_TO_STEP(thisKey) == 0 ? -12 : 12;
						transposeSeq(viewingPattern, transpose);
						dirtyDisplay = true;
					} else if(KEY_TO_STEP(thisKey) == 1 || KEY_TO_STEP(thisKey) == 14) {
						// transpose pattern by half-step
						int transpose = KEY_TO_STEP(thisKey) == 1 ? -1 : 1;
						transposeSeq(viewingPattern, transpose);
						dirtyDisplay = true;
					} else if(KEY_TO_STEP(thisKey) == 8) {
						// toggle mute for pattern S2, cue pattern S1
						if(omxMode == MODE_S1) {
							// cue next pattern
							nextPlayingPattern = viewingPattern;
							seqPos[nextPlayingPattern] = 0;
							displayMessagef("CUE P%d", nextPlayingPattern+1);
						} else {
							// toggle mute for pattern
							patternSettings[viewingPattern].mute = !patternSettings[viewingPattern].mute;
						}
					} else if(KEY_TO_STEP(thisKey) == 9) {
						// reset to start
						seqPos[viewingPattern] = 0;
						dirtyDisplay = true;
					} else if(KEY_TO_STEP(thisKey) == 10) {
						// reverse
						patternSettings[viewingPattern].reverse = !patternSettings[viewingPattern].reverse;
						dirtyDisplay = true;
					} else if(KEY_TO_STEP(thisKey) == 11) {
						disableFX[viewingPattern] = !disableFX[viewingPattern];
					} else if(KEY_TO_STEP(thisKey) == 12) {
						disablePLock[viewingPattern] = !disablePLock[viewingPattern];
					}
				}
			} else if (F1_HELD && F2_HELD) {
				// clear step
				copyStep(viewingPattern, seqKey);
				clearStep(viewingPattern, seqKey);
				displayMessagef("CUT S%d", seqKey);
			} else if (F1_HELD) {
				// copy step
				copyStep(viewingPattern, seqKey);
				displayMessagef("COPIED S%d", seqKey);
			} else if (F2_HELD) {
				// paste step
				pasteStep(viewingPattern, seqKey);
				displayMessagef("PASTED S%d", seqKey);
				dirtyDisplay = true;
			} else if (AUX_HELD) {
				displayMessagef("NOTE SEL S%d", seqKey);
				seqStepNoteSelect = true;
				selectedStep = seqKey;
				dirtyPixels = true;
				dirtyDisplay = true;
				invalidateShortPress[0] = true;
			} else {
				seqStepHold = true;
				seqStepEdit = true;
				selectedStep = seqKey;
				setParamModeNoteSelect();
				dirtyPixels = true;
				dirtyDisplay = true;
				invalidateShortPress[thisKey] = false;
                setScale(scaleRoot, scalePattern);
			}
		}
	}
	if(F1_HELD && down && thisKey == 0) {
		invalidateShortPress[0] = true;
		invalidateShortPress[1] = true;
		// resync all patterns to 0
		for(int i = 0; i < NUM_PATTERNS; i++) {
			seqPos[i] = 0;
		}
		seqReset();
	}

	if(seqStepHold && e.bit.EVENT == KEY_JUST_RELEASED && thisKey != 0) {
		// end seqStepHold if all keys are released
		bool anyKeysDown = false;
		for(int i = 0; i < 27; i++) {
			if(keyState[i] == true && thisKey != i) {
				anyKeysDown = true;
				break;
			}
		}
		if(anyKeysDown == false) {
			seqStepHold = false;

			dirtyDisplay = true;
			// if short press do toggle
			if(keyPressTime[thisKey] <= shortPressInterval && invalidateShortPress[thisKey] == false) {
				if ( stepNoteP[viewingPattern][seqKey].trig == TRIGTYPE_PLAY) {
					stepNoteP[viewingPattern][seqKey].trig = TRIGTYPE_MUTE;
				} else if(stepNoteP[viewingPattern][seqKey].trig == TRIGTYPE_MUTE) {
					stepNoteP[viewingPattern][seqKey].trig = TRIGTYPE_PLAY;
				}
			}
		} else {
			if(previewLastNote != -1) {
				rawNoteOff(previewLastNote, previewLastChannel);
				previewLastNote = -1;
				previewLastChannel = -1;
			}
		}
	} else if(patternHold && e.bit.EVENT == KEY_JUST_RELEASED && thisKey != 0) {
		// end patternHold if all keys are released
		bool anyKeysDown = false;
		for(int i = 0; i < 27; i++) {
			if(keyState[i] == true && thisKey != i) {
				anyKeysDown = true;
				break;
			}
		}
		if(anyKeysDown == false) {
			patternHold = false;
			if(patternEditLock == false) {
				patternEdit = false;
			}
			clearMessage();
		} else {
			if(previewLastNote != -1) {
				rawNoteOff(previewLastNote, previewLastChannel);
				previewLastNote = -1;
				previewLastChannel = -1;
			}
		}
	}

	if (e.bit.EVENT == KEY_JUST_RELEASED && thisKey != 0 && stepRecord && selectedNote > 0) {
		if (!playing){
			midiNoteOff(thisKey, PatternChannel(viewingPattern));
		}
		if (stepRecord && stepDirty) {
			stepDirty = false;
		}
	}

	if(e.bit.EVENT == KEY_JUST_RELEASED && thisKey == 0 && keyPressTime[thisKey] <= shortPressInterval && invalidateShortPress[0] == false) {
		// quick tap on AUX to toggle playback
		if(soloPattern != -1) {
			soloStop();
		} else if(stepRecord) {
			stepRecord = false;
		} else if(playing) {
			seqPos[viewingPattern] = 0;
			playingPattern = viewingPattern;
			seqStop();
			playing = false;
		} else {
			seqStart();
			playing = true;
		}
	}
	return true;
}

void handleKeyEvent(keypadEvent e) {
	int thisKey = e.bit.KEY;

	if (e.bit.EVENT == KEY_JUST_PRESSED){
		keyState[thisKey] = true;
		keyPressTime[thisKey] = 0;
	} else if (e.bit.EVENT == KEY_JUST_RELEASED){
		keyState[thisKey] = false;
	}

	if(handleKeyEventCommon(e)) {
		return;
	}

	// mode specific key handling
	switch(omxMode) {
		case MODE_OM:
		case MODE_MIDI: handleKeyEventMidi(e, -1); break;
		case MODE_S1:
		case MODE_S2: handleKeyEventSeq(e); break;
		default:
		case MODE_CONFIG: break;
	}
}

// ############## MAIN LOOP ##############

void loop() {
	customKeypad.tick();
	clksTimer = 0;

	Micros now = micros();
	Micros passed = now - lastProcessTime;
	lastProcessTime = now;

	if (passed > 0) {
		if (playing || (omxMode == MODE_MIDI && arp)){
			advanceClock(passed);
			advanceSteps(passed);
		}
	}
	doStep();

	if(omxMode == MODE_MIDI && arp) {
		doArp(passed);
	}

	// DISPLAY SETUP
	if(messageDisplayTimer == 0) {
		display.clearDisplay();
	}

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
	if(clearedPageTimer > 0) {
		clearedPageTimer--;
	}

	switch(omxMode) {
		case MODE_OM: // Organelle Mother
		case MODE_MIDI: // MIDI
			setParamModeMidi();
			break;
		case MODE_S1: // SEQ 1
			// FALL THROUGH
		case MODE_S2: // SEQ 2
			if(soloPattern != -1) {
				setParamModeSolo();
			} else if(seqStepHold || seqStepEdit || stepRecord || seqStepNoteSelect) {
				setParamModeNoteSelect();
			} else {
				setParamModePattern();
			}
			break;
		case MODE_CONFIG:
			setParamModeConfig();
		default:
			break;
	}

	auto u = myEncoder.update();
	if (u.active()) {
		auto amt = u.accel(5); // where 5 is the acceleration factor if you want it, 0 if you don't)
//    	Serial.println(u.dir() < 0 ? "ccw " : "cw ");
//    	Serial.println(amt);

		if(amt != 0) {
			// turning the encoder invalidates a short press
			for(int i = 0; i < 27; i++) {
				invalidateShortPress[i] = true;
			}
		}

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
					/*if (miparam == 0) {
						if(u.dir() < 0){									// if turn ccw
							MM::sendControlChange(CC_OM2,0,midiChannel);
						} else if (u.dir() > 0){							// if turn cw
							MM::sendControlChange(CC_OM2,127,midiChannel);
						}
					}*/
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
						displayMessagef("%s %s", noteNames[scaleRoot], scaleNames[scalePattern]);
					}
					break;
				case MODE_S1: // SEQ 1
					// FALL THROUGH
				case MODE_S2: // SEQ 2
					if(soloPattern != -1) {
						if (scaleSelectHold) {
							scalePattern = WRAP(scalePattern + amt, getNumScales());
							setScale(scaleRoot, scalePattern);
							for(int n = 1; n < 27; n++) {
								strip.setPixelColor(n, getDefaultColor(n));
							}
							strip.show();
							// show the name of the scale for a moment
							displayMessagef("%s %s", noteNames[scaleRoot], scaleNames[scalePattern]);
						}
					}
					break;
				case MODE_CONFIG:
				default:
					break;
			}
		}

		if(scaleSelectHold == false) {
			if(uiEditParam || AUX_HELD || seqStepHold || patternHold) {
			// edit current param
				if(*param < numParams) {
					changeParamValue(params, *param, amt);
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

			if(AUX_HELD) {
				saveToStorage();
			}

			if(mode_select == false) {
				uiEditParam = !uiEditParam;
			} else {
				// what page are we on?
				if (newmode != omxMode) {
					omxMode = newmode;
					uiEditParam = false;
					seqStepHold = false;
					seqStepEdit = false;
					patternHold = false;
					patternEditLock = false;
					patternEdit = false;
					scaleSelectHold = false;
					seqStop();
					setAllLEDS(0,0,0);
					mode_select = false;
					dispMode();
				} else if (mode_select){
					mode_select = false;
				}
			}
			dirtyDisplay = true;
			break;

		// LONG PRESS
		case Button::DownLong: //Serial.println("Button downlong");
			if (stepRecord) {
				resetPatternDefaults(viewingPattern);
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
		handleKeyEvent(e);
	}

	// ############### MODES DISPLAY  ##############
	if(dangerConfirmTimer > 0) {
		dangerConfirmTimer -= passed;
		if(dangerConfirmTimer <= 0) {
			dangerConfirmTimer = 0;
			dangerConfirm = -1;
		}
	}
	if(messageDisplayTimer > 0) {
		if(scaleSelectHold == false) {
			messageDisplayTimer -= passed;
			if(messageDisplayTimer <= 0) {
				Serial.print("message display timer expired");
				dirtyDisplay = true;
				display.clearDisplay();
				messageDisplayTimer = 0;
			}
		}
	}

	if(!mode_select) {
		switch(omxMode){
			case MODE_OM: 						// ############## ORGANELLE MODE
				// FALL THROUGH
			case MODE_MIDI:							// ############## MIDI KEYBOARD
				//playingPattern = 0; 		// DEFAULT MIDI MODE TO THE FIRST PATTERN SLOT
				midi_leds();				// SHOW LEDS
				break;

			case MODE_S1: 						// ############## SEQUENCER 1
				// FALL THROUGH
			case MODE_S2: 						// ############## SEQUENCER 2
				break;
			case MODE_CONFIG:
                break;
			default:
				break;
		}
		if (dirtyDisplay && messageDisplayTimer == 0){			// DISPLAY
			dispGenericMode(params, numParams, *param);
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

	if (omxMode == MODE_S1 && nextPlayingPattern != -1 && seqPos[p] >= PatternLength(p)) {
		// jump to cued pattern in S1
		playingPattern = nextPlayingPattern;
		nextPlayingPattern = -1;
		seqPos[playingPattern] = 0;
        step_ahead(playingPattern);
		return;
	}

	if ( seqPos[p] >= PatternLength(p) ||
		 (patternSettings[p].autoreset && (patternSettings[p].autoresetstep > (patternSettings[p].startstep) ) && (seqPos[p] >= patternSettings[p].autoresetstep)) ||
		 (patternSettings[p].autoreset && (patternSettings[p].autoresetstep == 0 ) && (seqPos[p] >= patternSettings[p].rndstep)) ||
		 (patternSettings[p].reverse && (seqPos[p] < 0)) || // normal reverse reset
		 (patternSettings[p].reverse && patternSettings[p].autoreset && (seqPos[p] < patternSettings[p].startstep )) ||
		 (pageLock[p] && (seqPos[p] < patternPage[p] * NUM_STEPKEYS)) ||
		 (pageLock[p] && (seqPos[p] > patternPage[p] * NUM_STEPKEYS + (NUM_STEPKEYS - 1)))
		 // ||
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
				if(pageLock[p]) {
					seqPos[p] = patternPage[p] * NUM_STEPKEYS + (NUM_STEPKEYS - 1);
				} else {
					seqPos[p] = (PatternLength(p)-patternSettings[p].startstep)-1;
				}
			}

		} else {
			if(pageLock[p]) {
				seqPos[p] = patternPage[p] * NUM_STEPKEYS;
			} else {
				seqPos[p] = (patternSettings[p].startstep); // resets pattern in FWD
			}
		}
		if (patternSettings[p].autoresetfreq == patternSettings[p].current_cycle){ // reset cycle logic
			if (probResult(patternSettings[p].autoresetprob, 100)){
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
	
// return ()
}

bool probResult(int stepProbSetting, int patternProbSetting){
	if (stepProbSetting > 100){
		return true;
	}
	if (stepProbSetting == 0 || patternProbSetting == 0){
		return false;
	}
	if(rand() % 100 < stepProbSetting && rand() % 100 < patternProbSetting) { // assumes probSetting is a range 0-100
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

void arpReset() {
	for(int i = 0; i < ARP_SEQ_LEN; i++) {
		arpSeq[i] = -1;
		arpSeqSorted[i] = -1;
		arpIndex = -1;
		arpTimer = 0;
		arpSwingIndex = 0;
	}
	if(arpLastNote != -1) {
		rawNoteOff(arpLastNote, arpLastChannel);
	}
	arpLastNote = -1;
	arpLastChannel = -1;
}

int cmpUp(int a, int b) {
	// return > 0 if a should be after b
	if(a == b) {
		return 0;
	}
	if(a == -1 && b != -1) {
		// -1 should always go last
		return 1;
	}
	return a - b;
}

int cmpDown(int a, int b) {
	// return > 0 if b should be after a
	if(a == b) {
		return 0;
	}
	if(a == -1 && b != -1) {
		// -1 should always go last
		return 1;
	}
	return b - a;
}

void bSort(int array [], int from, int upTo) {
	int swaps;
	do {
		swaps=0;
		for(int i = from; i < upTo; i++) {
			if(cmpUp(array[i], array[i+1]) > 0) {
				int x = array[i+1];
				array[i+1] = array[i];
				array[i] = x;
				swaps++;
			}
		}
	} while(swaps != 0);
}

void bSortRev(int array [], int from, int upTo) {
	int swaps;
	do {
		swaps=0;
		for(int i = from; i < upTo; i++) {
			if(cmpDown(array[i], array[i+1]) > 0) {
				int x = array[i+1];
				array[i+1] = array[i];
				array[i] = x;
				swaps++;
			}
		}
	} while(swaps != 0);
}


void arpSort() {
	if(arpSeqSort == 0) {
		return;
	} else if(arpSeqSort == 1 || arpSeqSort == 3) {
		// up
		Serial.println("before sorting up");
		for(int i = 0; i < ARP_SEQ_LEN; i++) {
			Serial.print(i);
			Serial.print(": ");
			Serial.println(arpSeq[i]);
		}
		int count = 0;
		for(int i = 0; i < ARP_SEQ_LEN; i++) {
			if(arpSeq[i] != -1) {
				arpSeqSorted[count++] = arpSeq[i];
			}
		}
		for(int i = count; i < ARP_SEQ_LEN; i++) {
			arpSeqSorted[i] = -1;
		}
		bSort(arpSeqSorted, 0, count-1);
		Serial.println("after sorting up");
		for(int i = 0; i < ARP_SEQ_LEN; i++) {
			Serial.print(i);
			Serial.print(": ");
			Serial.println(arpSeqSorted[i]);
		}
	} else if(arpSeqSort == 2 || arpSeqSort == 4) {
		// dn
		Serial.println("before sorting down");
		for(int i = 0; i < ARP_SEQ_LEN; i++) {
			Serial.print(i);
			Serial.print(": ");
			Serial.println(arpSeq[i]);
		}
		int count = 0;
		for(int i = 0; i < ARP_SEQ_LEN; i++) {
			if(arpSeq[i] != -1) {
				arpSeqSorted[count++] = arpSeq[i];
			}
		}
		for(int i = count; i < ARP_SEQ_LEN; i++) {
			arpSeqSorted[i] = -1;
		}
		bSortRev(arpSeqSorted, 0, count-1);
		Serial.println("after sorting down");
		for(int i = 0; i < ARP_SEQ_LEN; i++) {
			Serial.print(i);
			Serial.print(": ");
			Serial.println(arpSeqSorted[i]);
		}
	}
}

void soloStart(int pattern) {
	soloPattern = pattern;
	seqStepHold = false;
	seqStepEdit = false;
	seqStepNoteSelect = false;
	patternEditLock = false;
	patternEdit = false;
	patternHold = false;
	arpReset();
	displayMessagef("SOLO P%d", soloPattern);
	dirtyPixels = true;
	dirtyDisplay = true;
}

void soloStop() {
	soloPattern = -1;
	arpReset();
	dirtyPixels = true;
}

void doArpStep() {
	arpIndex = (arpIndex + 1) % ARP_SEQ_LEN;
	arpSwingIndex = (arpSwingIndex + 1) % 2;
	// next note
	// find next valid note
	int note = arpSeqSort != 0 ? arpSeqSorted[arpIndex] : arpSeq[arpIndex];
	if(note == -1) {
		for(int i = 0; i < ARP_SEQ_LEN; i++) {
			arpIndex = (arpIndex + 1) % ARP_SEQ_LEN;
			note = arpSeqSort != 0 ? arpSeqSorted[arpIndex] : arpSeq[arpIndex];
			if(note != -1) {
				break;
			}
		}
	}
	if(note != -1) {
		if(arpIndex == 0) {
			arpOctaveIndex = (arpOctaveIndex + 1) % arpOctaves;
		}
		if(arpSeqSort == 2) {
			note += 12 * ((arpOctaves - 1) - arpOctaveIndex);
		} else {
			note += 12 * arpOctaveIndex;
		}
		int channel = soloPattern != -1 ? PatternChannel(soloPattern) : (midiChannel + rrIndex);

		Micros offset = (Micros)((float)step_micros * multValues[arpMdiv] * (1.0f + (arpSwingIndex == 0 ? swing : -swing)));

		seqPlayScheduledNote(
			note,
			midiVelocity,
			channel,
			micros() + offset,
			(float)step_micros * multValues[arpMdiv] * ((float)arpGate / 32.0f),
			false
		);
		arpLastNote = note;
		arpLastChannel = channel;
		if(soloPattern == -1) {
			rrIndex = (rrIndex + 1) % midiRRChannelCount;
		}
	}
}

void doArp(Micros dt) {
	arpTimer -= dt;
	if(arpTimer < 0) {
		float swing = ((float)arpSwing / 255.0f);
		Micros arpStepMicros = (Micros)((float)step_micros * multValues[arpMdiv] * (1.0f + (arpSwingIndex == 0 ? swing : -swing)));
		arpTime = arpStepMicros;
		arpTimer += arpStepMicros;
		doArpStep();
	}
}

void doStep() {
// // probability test
	switch(omxMode){
		case MODE_S1:
			if(playing) {
				bool testProb = probResult(stepNoteP[playingPattern][seqPos[playingPattern]].prob, patternSettings[playingPattern].prob);
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
						seqPlayPatternStep(playingPattern);
	//					step_on(playingPattern);
					}


					drawSeqLEDs(viewingPattern);
					step_ahead(playingPattern);
				}
			} else {
				drawSeqLEDs(viewingPattern);
			}
			break;

		case MODE_S2:
			if(playing) {
				unsigned long playstepmicros = micros();

				for (int j=0; j<NUM_PATTERNS; j++){ // check all patterns for notes to play in time

					// CLOCK PER PATTERN BASED APPROACH
					if(playstepmicros >= timePerPattern[j].nextStepTimeP){

						seqReset(); // check for seqReset
						if(soloPattern == j && arp) {
							timePerPattern[j].nextStepTimeP += (step_micros)*( multValues[arpMdiv] ); // calc step based on arp rate
							timePerPattern[j].lastStepTimeP = timePerPattern[j].nextStepTimeP;
						} else {
							timePerPattern[j].nextStepTimeP += (step_micros)*( multValues[patternSettings[j].clockDivMultP] ); // calc step based on rate
							timePerPattern[j].lastStepTimeP = timePerPattern[j].nextStepTimeP;
						}

						timePerPattern[j].lastPosP = (seqPos[j]+15) % 16;
						if (lastNote[j][timePerPattern[j].lastPosP] > 0){
							step_off(j, timePerPattern[j].lastPosP);
						}

						// only play if not muted and not soloing
						if(soloPattern == j) {
							if(arp) {
								doArpStep();
							}
						} else {
							if (!patternSettings[j].mute) {
								bool testProb = probResult(stepNoteP[j][seqPos[j]].prob, patternSettings[j].prob);
								if(testProb) {
									if (evaluate_AB(stepNoteP[j][seqPos[j]].condition, j)){
										seqPlayPatternStep(j);
									}
								}
							}
						}
						if(j == viewingPattern){ // only show selected pattern
							drawSeqLEDs(viewingPattern);
						}
						new_step_ahead(j);
					}
				}

			} else {
				drawSeqLEDs(viewingPattern);
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
			// RoundRobin Setting
			adjchan = channel + rrIndex;

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
						arpSeqSorted[i] = -1;
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
					arpSort();
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
			arpSort();
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

void seqPlayScheduledNote(int note, int vel, int channel, int noteon_micros, int length_micros, bool sendnoteCV) {
	pendingMidi.insert(note, vel, true,  channel, noteon_micros, sendnoteCV);
	pendingMidi.insert(note, 0,   false, channel, noteon_micros + MAX(length_micros, 1), sendnoteCV);
}

// Play a note / step (SEQUENCERS)
void seqPlayPatternStep(int patternNum) {
//	Serial.println(stepNoteP[patternNum][seqPos[patternNum]].note); // Debug
	if (stepNoteP[patternNum][seqPos[patternNum]].trig != TRIGTYPE_PLAY){
		return;
	}

	bool sendnoteCV = false;
	int rnd_swing;
	if (cvPattern[patternNum]){
		sendnoteCV = true;
	}
	StepType playStepType = stepNoteP[patternNum][seqPos[patternNum]].stepType;

	if(disableFX[patternNum] == false) {
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
	}

	// regular note on trigger

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

    Micros fullStep_micros = step_micros * multValues[patternSettings[patternNum].clockDivMultP];

	Micros noteLength_micros = (stepNoteP[patternNum][seqPos[patternNum]].len + 1) * ((fullStep_micros * patternSettings[patternNum].gate) / 127);
	int baseNote = stepNoteP[patternNum][seqPos[patternNum]].note;
	int noteVel = (stepNoteP[patternNum][seqPos[patternNum]].vel * patternSettings[patternNum].vel) / 127;

	seqPlayScheduledNote(
		baseNote,
		noteVel,
		PatternChannel(patternNum),
		noteon_micros,
		noteLength_micros,
		sendnoteCV
	);

	if(stepNoteP[patternNum][seqPos[patternNum]].chord != 0) {
		for(int j = 0; j < 3; j++) {
			int chordNote = chordPatterns[stepNoteP[patternNum][seqPos[patternNum]].chord][j];
			if(chordNote == -1) {
				break;
			}
			seqPlayScheduledNote(
				baseNote + chordNote,
				noteVel,
				PatternChannel(patternNum),
				noteon_micros + ((((patternSettings[patternNum].chordArp * stepNoteP[patternNum][seqPos[patternNum]].chordArp) / 15 * fullStep_micros) / 15) * (j+1)),
				noteLength_micros,
				sendnoteCV
			);
		}
	} 

	// {notenum, vel, notelen, step_type, {p1,p2,p3,p4}, prob}
	// send param locks
	if(disablePLock[patternNum] == false) {
		for (int q=0; q<4; q++){
			int tempCC = stepNoteP[patternNum][seqPos[patternNum]].params[q];
			if (tempCC > -1) {
				MM::sendControlChange(pots[potbank][q],tempCC,PatternChannel(patternNum));
				prevPlock[q] = tempCC;
			} else if (prevPlock[q] != potValues[q]) {
				//if (tempCC != prevPlock[q]) {
				MM::sendControlChange(pots[potbank][q],potValues[q],PatternChannel(patternNum));
				prevPlock[q] = potValues[q];
				//}
			}
		}
	}
	lastNote[patternNum][seqPos[patternNum]] = stepNoteP[patternNum][seqPos[patternNum]].note;
	patternActive[patternNum] = 1;
	dirtyPixels = true;

	// CV is sent from pendingMidi
}

void allNotesOff() {
	pendingMidi.allOff();
}

void allNotesOffPanic() {
	analogWrite(CVPITCH_PIN, 0);
	digitalWrite(CVGATE_PIN, LOW);
	for(int ch=1; ch < 17; ch++) {
		for (int j=0; j<128; j++){
			MM::sendNoteOff(j, 0, ch);  // NEEDS FIXING
		}
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
	playingPattern = viewingPattern;


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

void clearStep(int patternNum, int step){
	// {notenum,vel,len,stepType,{p1,p2,p3,p4,p5}}
	stepNoteP[patternNum][step].note = patternDefaultNoteMap[patternNum];
	stepNoteP[patternNum][step].vel = defaultVelocity;
	stepNoteP[patternNum][step].len = 0;
	stepNoteP[patternNum][step].stepType = STEPTYPE_NONE;
	stepNoteP[patternNum][step].trig = TRIGTYPE_MUTE;
	stepNoteP[patternNum][step].params[0] = -1;
	stepNoteP[patternNum][step].params[1] = -1;
	stepNoteP[patternNum][step].params[2] = -1;
	stepNoteP[patternNum][step].params[3] = -1;
	stepNoteP[patternNum][step].params[4] = -1;
	stepNoteP[patternNum][step].prob = 100;
	stepNoteP[patternNum][step].condition = 0;
	stepNoteP[patternNum][step].chord = 0;
	stepNoteP[patternNum][step].chordArp = 0;
}

void clearPage(int patternNum, int page){
	// {notenum,vel,len,stepType,{p1,p2,p3,p4,p5}}
	for (int step = page * NUM_STEPKEYS; step < page * NUM_STEPKEYS + NUM_STEPKEYS; step++){
		clearStep(patternNum, step);
	}
}

void clearPattern(int patternNum){
	for (int i = 0; i < NUM_STEPS; i++){
		// {notenum,vel,len,stepType,{p1,p2,p3,p4,p5}}
		clearStep(patternNum, i);
	}
}

void copyStep(int patternNum, int step){
	memcpy( &copyStepBuffer, &stepNoteP[patternNum][step], sizeof(StepNote) );
}

void pasteStep(int patternNum, int step){
	memcpy( &stepNoteP[patternNum][step], &copyStepBuffer, sizeof(StepNote) );
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

void copyPage(int patternNum, int page){
	memcpy( &copyPageBuffer, &stepNoteP[patternNum][page * NUM_STEPKEYS], NUM_STEPKEYS * sizeof(StepNote) );
}

void pastePage(int patternNum, int page){
	memcpy( &stepNoteP[patternNum][page * NUM_STEPKEYS], &copyPageBuffer, NUM_STEPKEYS * sizeof(StepNote) );
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
			strip.setPixelColor(i, strip.gamma32(strip.ColorHSV(pixelHue, rainbowSaturation, 255)));
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

		clearPattern(i);

		patternSettings[i].len = 15;
		patternSettings[i].channel = i;
		patternSettings[i].startstep = 0;
		patternSettings[i].autoresetstep = 0;
		patternSettings[i].autoresetfreq = 0;
		patternSettings[i].autoresetprob = 0;
		patternSettings[i].current_cycle = 1;
		patternSettings[i].rndstep = 3;
		patternSettings[i].clockDivMultP = 2;
		patternSettings[i].swing = 0;
		patternSettings[i].reverse = false;
		patternSettings[i].mute = false;
		patternSettings[i].autoreset = false;
		patternSettings[i].solo = false;
		patternSettings[i].prob = 100;
		patternSettings[i].vel = 127;
		patternSettings[i].gate = 127;
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

	storage->write( EEPROM_HEADER_ADDRESS + 4 + NUM_CC_POTS, rainbowSaturation);
	storage->write( EEPROM_HEADER_ADDRESS + 4 + NUM_CC_POTS + 1, scaleBrightness);
	uint8_t spresstmp = shortPressInterval / 10;
	storage->write( EEPROM_HEADER_ADDRESS + 4 + NUM_CC_POTS + 2, spresstmp);

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
		if(pots[potbank][i] > 127) {
			pots[potbank][i] = pots[potbank][i] - 256;
		}
	}

	rainbowSaturation = storage->read( EEPROM_HEADER_ADDRESS + 4 + NUM_CC_POTS );
	scaleBrightness = storage->read( EEPROM_HEADER_ADDRESS + 4 + NUM_CC_POTS + 1 );
	uint8_t spresstmp = storage->read( EEPROM_HEADER_ADDRESS + 4 + NUM_CC_POTS + 2 );
	if(spresstmp == 0) {
		spresstmp = 50;
	}
	shortPressInterval = spresstmp * 10;
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
