// Update the LCD display with the current table travel in mm/Sec
void showFeedRate( void )
{
    char buff[16] = {0};
    
    lcd.setCursor(0,0);
    lcd.print("Feed rate ");
    lcd.setCursor(0,1);
    float f = ( float ) (( 1E6 / (SLOW_RATE_DELAY + slowRateAdjust)) / STEPS_PER_MILLIMETER);
    String s = String( f, 3 ) + " mm/Sec";
    lcd.print( s ); 
}

// Set the stepper motor rotation direction on the M5542T
void setDirection( rotationDirection Direction )
{
    if ( Direction == CW )
    {
        digitalWrite( DIR_PIN, LOW );           
    }
    else
    {
        digitalWrite( DIR_PIN, HIGH );
    }
}

// Enable the M542T stepper motor driver
void setEnable( bool State )
{
    if ( State == enabledState )
        return;
    if ( State == true )
    {
        digitalWrite( ENABLE_PIN, LOW );
        enabledState = true;   
    }
    else
    {
        digitalWrite( ENABLE_PIN, HIGH );
        enabledState = false;       
    }
}

// Move the table by the number of mm passed in
void travelMillimeters( ULONG millimeters )
{
    if ( enabledState == false )
        return;
    ULONG stepsNeeded = ( long ) ( STEPS_PER_MILLIMETER * millimeters );
    doSteps( stepsNeeded );        
}

// Step the motor by the number of steps passed in
void doSteps( ULONG steps )
{
    Serial.println( travelRateDelay );
    if ( enabledState == false )
        return;
    // The maximum delay with delayMicroseconds is 16384
    if ( travelRateDelay < 16383 )
    {
        for ( ULONG c = 0; c < steps; ++c )
        {
            if ( currentState == Stopping )
                break;
            doStep();
            delayMicroseconds( travelRateDelay );
        }        
    }
    else
    {
        ULONG del = travelRateDelay / 1000L;
        for ( ULONG c = 0; c < steps; ++c )
        {
            doStep();
            delay( del );
        }
    }
}


// Produces a 4.3 uS pulseto microstep the motor 
void doStep()
{
    digitalWrite(STEP_PIN, HIGH);
    ++pulseWidthDummy; // Waste one cycle
    digitalWrite(STEP_PIN, LOW); 
}

