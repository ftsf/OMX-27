#include "noteoffs.h"

#include <Arduino.h>
#include "consts.h"
#include "MM.h"


PendingMidi::PendingMidi() {
	for (int i = 0; i < queueSize; ++i)
		queue[i].inUse = false;
}

bool PendingMidi::insert(int note, int velocity, bool on, int channel, uint32_t time, bool sendCV) {
	for (int i = 0; i < queueSize; ++i) {
		if (queue[i].inUse) continue;
		queue[i].inUse = true;
		queue[i].on = on;
		queue[i].velocity = velocity;
		queue[i].note = note;
		queue[i].time = time;
		queue[i].channel = channel;
		queue[i].sendCV = sendCV;
		return true;
	}
	return false; // couldn't find room!
}

void PendingMidi::play(uint32_t now, bool offOnly) {
	for (int i = 0; i < queueSize; ++i) {
		if (queue[i].inUse && queue[i].time <= now) {
			if(queue[i].on) {
				if(offOnly == false) {
					MM::sendNoteOn(queue[i].note, queue[i].velocity, queue[i].channel);
				}
			} else {
				MM::sendNoteOff(queue[i].note, 0, queue[i].channel);
			}
			if (queue[i].sendCV) {
				if(queue[i].on) {
					if(offOnly == false) {
						if(queue[i].note>=midiLowestNote && queue[i].note <midiHightestNote) {
							int pCV = static_cast<int>(roundf( (queue[i].note - midiLowestNote) * stepsPerSemitone));
							digitalWrite(CVGATE_PIN, HIGH);
							analogWrite(CVPITCH_PIN, pCV);
						}
					}
				} else {
					digitalWrite(CVGATE_PIN, LOW);
				}
			}
			queue[i].inUse = false;
		}
	}
}

void PendingMidi::allOff() {
	play(UINT32_MAX, true);
}

PendingMidi pendingMidi;
