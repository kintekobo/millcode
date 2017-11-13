/*
    Program to control a steper motor driving the table of a milling machine
    Inputs:
        Five buttons and one rotary switch

    Outputs:
        Step pulse, Direction state and Enable state to control the M542T

*/

#include <Wire.h> // Only needed if using the LCD
#include <LiquidCrystal_I2C.h> // Only needed if using the LCD
#include <PinChangeInterrupt.h> // Used to detect changes on the input pins

// Arduino pin usage

// Output pins
#define STEP_PIN 3
#define DIR_PIN  4
#define ENABLE_PIN   5

// Input pins
#define FASTLEFT    13      // Input from the Fast Left button
#define SLOWLEFT    12      // Input from the Slow Left button
#define SLOWRIGHT   11      // Input from the Slow Right button
#define FASTRIGHT   10      // Input from the Fast Right button
#define STOPBUTTON  9       // Input from the Stop button
#define SWITCHBUTTON    8   // Input from the Rotary Switch button
#define ROTARY_A    7       // Input from the Rotary Switch direction pulse A
#define ROTARY_B    6       // Input from the Rotary Switch direction pulse B
#define ENDSTOP        2       // Input from the End Stop switches to indicate carriage at max travel

#define ULONG  unsigned long
#define MAX_ULONG  (0xffffffff)

// Mill table details
#define TOTAL_TURNS 186L // Number of complete rotations of the handle to take the table from one side to the other
#define MILLIMETERS_PER_TURN 2L // Number of mm travelled with one rotation of the handle
#define TOTAL_MILLIMETERS ( TOTAL_TURNS * MILLIMETERS_PER_TURN ) // Total distance the table can travel

// Stepper motor driver M542T settings
#define STEP_PULSE_WIDTH 4 // Width of the STEP pulse
#define STEPS_PER_REVOLUTION 12800L // Microstepping config. Number of steps for one revolution
#define STEPS_PER_MILLIMETER (STEPS_PER_REVOLUTION / MILLIMETERS_PER_TURN ) // Microstepping config. Number of mm for one revolution

// Some data regarding steps and travel
#define STEPS_PER_MILLIMETER ( STEPS_PER_REVOLUTION / MILLIMETERS_PER_TURN )
#define FULL_WIDTH_STEPS ( STEPS_PER_REVOLUTION * TOTAL_TURNS )
#define MAX_TRAVEL_SPEED_DELAY 10 
#define MINIMUM_TRAVEL_DISTANCE ( MILLIMETERS_PER_TURN / STEPS_PER_MILLIMETER )
#define FIDDLE_FACTOR 4 // Number of microseconds to remove from the delay to account for the loop time
#define SLOWRATEINCREMENT 10    // Used to increment the slow speed for each click of the rotary knob
#define SLOWRATEMAXIMUM 5000    // Sets the slowest traverse speed
#define FAST_RATE_DELAY ((1000000L) / (( FAST_STEP_DISTANCE * STEPS_PER_MILLIMETER)  / 1 ) - ( STEP_PULSE_WIDTH + FIDDLE_FACTOR ))
#define SLOW_RATE_DELAY ((1000000L) / (( SLOW_STEP_DISTANCE * STEPS_PER_MILLIMETER)  / 1 ) - ( STEP_PULSE_WIDTH + FIDDLE_FACTOR )) 
#define FAST_STEP_DISTANCE 7 // How many mm per second the table traverses when the fast button is pressed 
#define SLOW_STEP_DISTANCE 1 // Initial mm per second the table traverses when the slow button is pressed   

LiquidCrystal_I2C lcd(0x27, 2, 1, 0, 4, 5, 6, 7, 3, POSITIVE);  

// State machine details
enum enumState { Idle, Stopping, MovingLeftFast, MovingLeftSlow, MovingRightFast, MovingRightSlow, ButtonPressed };
volatile enumState currentState = Idle;

const int  lcd_address=027;   // I2C Address of LCD backpack

volatile ULONG travelRateDelay = 100L;
volatile ULONG slowRateAdjust = 0;
volatile bool rotaryButtonPressed = false;

enum rotationDirection {NONE, CW, CCW };
rotationDirection directionState = NONE;

bool enabledState = true; // Will be set to false on startup
volatile int pulseWidthDummy = 0; // Used as a means of wasting a very short pime for the step pulse width
bool shouldBeStepping = false; // Set to true when one of the buttons is requesting traverse
volatile bool feedRateChanged = true; // Set to true when the feed rate knob is turned


//#define SERIAL

// This code is executed once at power up
void setup()
{
    
#ifdef SERIAL    
   Serial.begin(115200);  // We enable this when we want to print debug information
#endif

    // Set the Arduino pins as inputs or outputs as required
    pinMode( STEP_PIN, OUTPUT );
    pinMode( DIR_PIN, OUTPUT );
    pinMode( ENABLE_PIN, OUTPUT );
    pinMode( FASTLEFT, INPUT_PULLUP );
    pinMode( SLOWLEFT, INPUT_PULLUP );
    pinMode( SLOWRIGHT, INPUT_PULLUP );
    pinMode( FASTRIGHT, INPUT_PULLUP );
    pinMode( ROTARY_A, INPUT_PULLUP );
    pinMode( ROTARY_B, INPUT_PULLUP );
    pinMode( STOPBUTTON, INPUT_PULLUP );
    pinMode( SWITCHBUTTON, INPUT_PULLUP );
    pinMode( ENDSTOP, INPUT_PULLUP );  
      
    // Configure the M542T to disable the stepper motor to prevent holding and thus heating up
    setEnable( false );  
    setDirection( CW );
  
    // attach the PinChangeInterrupts and enable event functions for the buttons
    attachPinChangeInterrupt(digitalPinToPinChangeInterrupt( FASTLEFT ), buttonService, CHANGE);
    attachPinChangeInterrupt(digitalPinToPinChangeInterrupt( SLOWLEFT ), buttonService, CHANGE);
    attachPinChangeInterrupt(digitalPinToPinChangeInterrupt( SLOWRIGHT ), buttonService, CHANGE);
    attachPinChangeInterrupt(digitalPinToPinChangeInterrupt( FASTRIGHT ), buttonService, CHANGE);
    attachPinChangeInterrupt(digitalPinToPinChangeInterrupt( STOPBUTTON ), buttonService, CHANGE);
    attachPinChangeInterrupt(digitalPinToPinChangeInterrupt( SWITCHBUTTON ), buttonService, CHANGE);
    // attach the PinChangeInterrupts and enable event functions for the rotary switch
    attachPinChangeInterrupt(digitalPinToPinChangeInterrupt( ROTARY_A ), rotarySwitchService, CHANGE);
    attachPinChangeInterrupt(digitalPinToPinChangeInterrupt( ROTARY_B ), rotarySwitchService, CHANGE);
     // attach the PinChangeInterrupts and enable event functions for the End Stop switch
    // attachPinChangeInterrupt(digitalPinToPinChangeInterrupt( ENDSTOP ), endStopService, CHANGE);

    delay(500); // Give LCD time to configure itself  
    lcd.begin(16,2); // sixteen characters across - 2 lines
    lcd.backlight();
}

// This routine is called continuously ( and transparently 0 by the main() code
void loop( void )
{
    // Has the button pressed flag been set?
    if ( rotaryButtonPressed == true ) 
    {
        Serial.println("bp");
        rotaryButtonPressed = false;
        slowRateAdjust = 0;
        feedRateChanged = true;
    }
  // Global flag set when rotary dial is turned
    if ( feedRateChanged == true )
    {
        showFeedRate();
        feedRateChanged = false;
    }
    
    // Global flag set when one of the move table buttons is pressed
    if ( shouldBeStepping ) 
    {
#ifdef SERIAL 
Serial.print("Should be stepping"); 
#endif
        setEnable( true );
        while ( currentState != Idle )
        {
            doCommand();
        }
       
    }
    else
        setEnable( false );
}

// Exmine the currentState flag to see what needs to be done
void doCommand( )
{
    ULONG distance = 0L;
    
    endStopCheck();
    
    // Global flag set when rotary dial is turned
    if ( feedRateChanged == true )
    {
        showFeedRate();
        feedRateChanged = false;
    }
   
    // We need to process the stop state as quickly as possible
    if ( currentState == Stopping )
    {
        currentState = Idle;
        shouldBeStepping = false;
        return;
    }
    // Ok we now check the currentState flag to see what is needed
    switch ( currentState )
    {
            case MovingLeftFast:     
                directionState = CW;
                travelRateDelay = FAST_RATE_DELAY; 
                enabledState = true;
                distance = FAST_STEP_DISTANCE;
                break;
            case MovingRightFast:     
                directionState = CCW;
                travelRateDelay = FAST_RATE_DELAY;
                enabledState = true;
                distance = FAST_STEP_DISTANCE ;
                break;
          case MovingLeftSlow:     
                directionState = CW;
                travelRateDelay = SLOW_RATE_DELAY + slowRateAdjust; 
                enabledState = true;
                distance = SLOW_STEP_DISTANCE;
                break;
             case MovingRightSlow:     
                directionState = CCW;
                travelRateDelay = SLOW_RATE_DELAY + slowRateAdjust; 
                enabledState = true;
                distance = SLOW_STEP_DISTANCE;
                break;
             default:
                return;
     }
     setDirection( directionState );
     travelMillimeters( distance ); // Go do it
}

