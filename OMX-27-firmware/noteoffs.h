#pragma once

#include <stdint.h>

class PendingMidi {
	public:
		PendingMidi();
		bool insert(int note, int velocity, bool on, int channel, uint32_t time, bool sendCV);
		void play(uint32_t time, bool offOnly = false);
		void allOff();

	private:
		struct Entry {
			bool inUse;
			bool on;
			int note;
			int channel;
			int velocity;
			bool sendCV;
			uint32_t time;
		};
		static const int queueSize = 32;
		Entry queue[queueSize];
};

extern PendingMidi pendingMidi;
