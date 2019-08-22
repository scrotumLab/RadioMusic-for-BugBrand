#include "Interface.h"

#include "Arduino.h"
#include "Bounce2.h"
#include "RadioMusic.h"

#ifdef DEBUG_INTERFACE
#define D(x) x
#else
#define D(x)
#endif

// SETUP VARS TO STORE CONTROLS
// A separate variable for tracking reset CV only
volatile boolean resetCVHigh = false;

// Called by interrupt on rising edge, for RESET_CV pin
void resetcv() {
	resetCVHigh = true;
}

void Interface::init(int fileSize, int channels, const Settings& settings, PlayState* state) {

  analogReadRes(ADC_BITS);
	pinMode(RESET_BUTTON, OUTPUT);
	pinMode(RESET_CV, settings.resetIsOutput ? OUTPUT : INPUT);

	// Add an interrupt on the RESET_CV pin to catch rising edges
	attachInterrupt(RESET_CV, resetcv, RISING);

	uint16_t bounceInterval = 5;
	resetButtonBounce.attach(RESET_BUTTON);
	resetButtonBounce.interval(bounceInterval);

	// make it backwards compatible with the old 10-bit cv and divider
	startCVDivider = settings.startCVDivider * (ADC_MAX_VALUE / 1024);

	pitchMode = settings.pitchMode;

    if(pitchMode) {
      
      quantiseRootCV = settings.quantiseRootCV;
      // SLab note vars now in ext. text file on SD Card
      float lowNote = settings.setLowNote + 0.5;
      startCVInput.setRange(lowNote, lowNote + settings.setNoteRange, quantiseRootCV); 
      startCVInput.borderThreshold = 64;
      
      D(Serial.print("In pitch mode"));
      D(Serial.print("Set Start Range ");Serial.println(ADC_MAX_VALUE / startCVDivider););
            
    } else {
      D(Serial.print("Not in pitch mode"));
    	D(Serial.print("Set Start Range ");Serial.println(ADC_MAX_VALUE / startCVDivider););
    	startCVInput.setRange(0.0, ADC_MAX_VALUE / startCVDivider, false); 
    	startCVInput.setAverage(true);
      startCVInput.borderThreshold = 32;
    }

	channelCVImmediate = settings.chanCVImmediate;
	startCVImmediate = settings.startCVImmediate;
 
	setChannelCount(channels);

	playState = state;
	buttonTimer = 0;
	buttonHoldTime = 0;
	buttonHeld = false;
}

void Interface::setChannelCount(uint16_t count) {
	channelCount = count;
	channelCVInput.setRange(0, channelCount - 1, true);
	D(Serial.print("Channel Count ");Serial.println(channelCount););
}

uint16_t Interface::update() {

	uint16_t channelChanged = updateChannelControls();
	uint16_t startChanged = pitchMode ? updateRootControls() : updateStartControls();

	changes = channelChanged;
	changes |= startChanged;
	changes |= updateButton();

	if(resetCVHigh || (changes & BUTTON_SHORT_PRESS)) {
		changes |= RESET_TRIGGERED;
	}
	resetCVHigh = false;

	return changes;
}

uint16_t Interface::updateChannelControls() {
	boolean channelCVChanged = channelCVInput.update();

	uint16_t channelChanged = 0;

  // SLab
  if(channelCVChanged) {
    int channel = (int) constrain(channelCVInput.currentValue, 0, channelCount - 1);  
    if (channel != playState->currentChannel) {
      D(Serial.print("Channel ");Serial.println(channel););
      playState->nextChannel = channel;
      channelChanged |= CHANNEL_CHANGED;
      if((channelCVImmediate && channelCVChanged)) {
        playState->channelChanged = true;
      }
    } else {
      D(
        Serial.print("Channel change flag but channel is the same: ");
        Serial.print(channel);
        Serial.print(" ");
        Serial.print(channelCVInput.currentValue);
        Serial.print(" ");
        Serial.println(playState->currentChannel);
      );
    }
  }
  return channelChanged;
}
// end SLab

  uint16_t Interface::updateStartControls() {
  uint16_t changes = 0;
  
  boolean cvChanged = startCVInput.update();

	if(cvChanged) {
		changes |= TIME_CV_CHANGED;
		if(startCVImmediate) {
			changes |= CHANGE_START_NOW;
		}
	}

  // SLab
  start = constrain(((startCVInput.currentValue * startCVDivider)),0,ADC_MAX_VALUE);
	
	if(changes) {
//		D(
//				Serial.print("Start ");
//				Serial.print(start);
//				Serial.print("\t");
//				Serial.print(startCVInput.currentValue);
//				Serial.print("\t");
//				// Serial.println(startPotInput.currentValue); // SLab change
//		);
//		D(startPotInput.printDebug();); //SLab change
//		D(startCVInput.printDebug(););
	}
	return changes;
}

// return bitmap of state of changes for CV, Pot and combined Note.
uint16_t Interface::updateRootControls() {

	uint16_t change = 0;

	boolean cvChanged = startCVInput.update();
	// boolean potChanged = startPotInput.update(); //SLab change

  // SLab
  if(!cvChanged) {
  	return change;
  }

  float rootCV = startCVInput.currentValue;

  if(cvChanged) {
  	D(
  		Serial.println("CV Changed");
  	);
  	if(quantiseRootCV) {
      	rootNoteCV = floor(rootCV);
      	if(rootNoteCV != rootNoteCVOld) {
      		D(
				Serial.print("CV ");Serial.println(startCVInput.inputValue);
      		);
      		change |= ROOT_CV_CHANGED;
      	}
  	} else {
  		rootNoteCV = rootCV;
  		change |= ROOT_CV_CHANGED;
  	}
  }

  // SLab
  rootNote = rootNoteCV;

  // Flag note changes when the note index itself changes
  if(floor(rootNote) != rootNoteOld) {
  	change |= ROOT_NOTE_CHANGED;
  	rootNoteOld = floor(rootNote);
  }

	return change;
}

uint16_t Interface::updateButton() {
	resetButtonBounce.update();
	uint16_t buttonState = 0;

	// Button pressed
	if(resetButtonBounce.rose()) {
		buttonTimer = 0;
		buttonHeld = true;
	}

  if(resetButtonBounce.fell()) {
  	buttonHeld = false;
  	// button has been held down for some time
      if (buttonTimer >= SHORT_PRESS_DURATION && buttonTimer < LONG_PRESS_DURATION){
      	buttonState |= BUTTON_SHORT_PRESS;
      } else if(buttonTimer > LONG_PRESS_DURATION) {
      	buttonState |= BUTTON_LONG_RELEASE;
      }
      buttonTimer = 0;
  }

  if(buttonHeld && buttonTimer >= LONG_PRESS_DURATION) {
  	buttonState |= BUTTON_LONG_PRESS;

  	uint32_t diff = buttonTimer - LONG_PRESS_DURATION;
  	if(diff >= LONG_PRESS_PULSE_DELAY) {
  		buttonState |= BUTTON_PULSE;
  		buttonTimer = LONG_PRESS_DURATION;
  	}
  }
    return buttonState;
}
