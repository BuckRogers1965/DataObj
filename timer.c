#include <time.h>
#include <sys/time.h>

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>


#define DAYOFSECONDS 86400
#define HOUROFSECONDS 3600
#define HOURSINADAY 24
#define MINUTEOFSECONDS 60
#define DAYSINWEEK 7
#define MONTHSINYEAR 12
#define NOON 12

#define ST_SLEEP 1

enum {FALSE=0, TRUE};

enum {FixedInterval=0, EveryWeekDay, EveryMonthDay};

/*

Keeps track of time and caches current time / tick for the program so that all the various timers and other time tracking items don't have to make thousands of system calls everytime they need to know the high accuracy time.

Making timer system calls are very expensive and it necisary to cache the time this way.

Should only ever be updated by the main thread just before the scheduled tasks are executed.

*/

/*

Tasks needed done:

O  Tie date string to timer formating

O  Allow different output formats for the the date string generation

O  cached time in timer.c

*/

static unsigned long global_seconds = 0;
static unsigned long global_milliseconds = 0;
static unsigned long global_start_seconds = 0;


void
GetCurrentTime (unsigned long * seconds, unsigned long * milliseconds){
	*seconds = global_seconds;
	*milliseconds = global_milliseconds;
}

time_t
GetCurrentSeconds (){
	return (time_t)global_seconds;
}

/* Return the elapsed time in seconds since we began running */
long
GetRunTime(){
	return global_seconds - global_start_seconds;
}


void
localCacheTime(time_t * theTime){
	*theTime=global_seconds;
}

/* 
update time to current tics, 
if time has changed more than a second since we last ran then return the offset, 
otherwise return 0

This is to allow the main loop to reschedule running tasks.
*/


long
TimeUpdate (){

	struct timeval tv;
	struct timezone tz;

        long prevSecs = global_seconds;
        long offset;

	gettimeofday(&tv, &tz);

	if (!global_seconds){
		/* first time thru manually set the offset and start time */
		prevSecs = tv.tv_sec;
		global_start_seconds  = tv.tv_sec;
	}

	global_seconds  = tv.tv_sec;
	global_milliseconds = tv.tv_usec/1000;

        offset = global_seconds - prevSecs;

	/* if offset is more than a second from last time, adjust things */
	/* time should jump forward smoothly no more than a second at a time */
        if ( offset < 0 || offset > 1){
		global_start_seconds += offset;
		return offset;
	}

//	printf("\n\n*** TimeUpdate %d %d\n\n", global_seconds, global_milliseconds);

	return 0;
}

int 
IsLeapYear (unsigned int year){
	return(((year % 4) && (year %100)) || (year % 400 == 0 ));
}

int
DaysInMonth(int year, int month){

	// month is from 0 to 11

	// return 0 if outside of month range

	int months[]={31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

	if ((month < 0) || (month > 11))
		return 0;

	// if feb and in a leap year, return 29
	if (month == 1  && IsLeapYear(year))
		return 29;

	return months[month];
}

char *
ShortMonthNames[]={"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

char *
LongMonthNames[]={"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};

char *
ShortDayNames[]={"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

char *
LongDayNames[]={"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};

char *
FormatDate(time_t theTime){

	struct tm * formatTime;
	static char dateStr[29];
	char * current = dateStr;
	int pm = FALSE;
	int i, year;

	if(!theTime)
		localCacheTime(&theTime);

	formatTime = localtime ( &theTime );
	if ( !formatTime )
		return "";

	// doesn't make sense to format time before year 2004
	// why?
	if (formatTime->tm_year < 104 )
		return "";


	strcpy ( current, ShortDayNames [ formatTime->tm_wday ] );
	current += 3;

	*current=' ';
	current++;


	strcpy ( current, ShortMonthNames [ formatTime->tm_mon ] );
	current += 3;

	*current=' ';
	current++;

	if ( formatTime->tm_mday < 10 ) {
		*current=' ';
		current++;
	} else {
		*current = formatTime->tm_mday/10+'0';
		current++;
	}

	*current = formatTime->tm_mday%10 + '0';
	current++;
	
	*current=' ';
	current++;

	year = formatTime->tm_year + 1900;

	i = year/1000;
	year = year - i*1000;
	*current= i + '0';
	current++;

	i = year/100;
	year = year - i*100;
	*current= i + '0';
	current++;

	i = year/10;
	year = year - i*10;
	*current= i + '0';
	current++;

	*current= year + '0';
	current++;
	
	*current=' ';
	current++;

	// format the hour
	i = formatTime->tm_hour;

	if ( i >= NOON) { 
		i -= NOON;
		pm = TRUE;
	}
	
	if (i == 0)
		i = NOON;	

	if (i < 10) {
		*current='0';
		current++;
	} else {
		*current=i/10+'0';
		current++;
	}
	*current=i%10+'0';
	current++;

	*current=':';
	current++;

	// Format minute

	if ( formatTime->tm_min < 10 ) {
		*current='0';
		current++;
	} else {
		*current = formatTime->tm_min / 10+'0';
		current++;
	}

	*current = formatTime->tm_min % 10 + '0';
	current++;
	
	*current=' ';
	current++;

	if(pm)
		*current = 'P';
	else
		*current = 'A';

	current++;

	*current='M';
	current++;

	// put the end marker on the string
	*current = 0;	

	return dateStr;

}

int
DaylightSavings(time_t theTime){
	
	struct tm *checktime;

	checktime = localtime(&theTime);
	if(checktime)
		return (checktime->tm_isdst);
	else
		return 0;
}

int
GetWeek ( int year, int month, int day ){

	//year must be greater than 2003
	// why was that again???
	int accumulateddays=4; //jan 1 2004 was a thursday

	int currentyear;
	int currentmonth;

	for(currentyear=2004; currentyear<=year; currentyear++)
		for (currentmonth=0; currentmonth < MONTHSINYEAR; currentmonth++){
			if((currentyear==year)&&(currentmonth==month)){
				accumulateddays +=day;
				break;
			}
			accumulateddays += DaysInMonth( currentyear, currentmonth);
		}

	return ((accumulateddays)/DAYSINWEEK);
}

int
NumberOfWeeksInMonth(int year, int month){

	return(GetWeek(year, month, DaysInMonth(year, month))-GetWeek(year, month, 1)+1);

}

int
CurrentWeekInMonth(int year, int month, int day){

	return(GetWeek(year, month, day)-GetWeek(year, month, 1)+1);

}

typedef struct schedtype {


} schedtype;

typedef struct mytime {

	int State;
	int StartOnNextMinute;
	int Applied;

	int MonthsOfYear[12];
	int DaysOfWeek[7];
	int StartTime[6];
	int Every[3];
	int WeeksOfMonth[7];
	int DaysOfMonth[33];
} mytime;

unsigned long
EventDelay ( mytime schedTime[], int type, time_t theTime ){

	struct tm *today;
	long seconds;
	int LastDay,
	    year,
	    yearrange,
	    month,
	    day,
	    wday = 0;
	int accumulatedDays=1;
	int yearoffset =1900; 
	int monthoffset = 0;
	int dayoffset = 0;

	int syear     = schedTime[type].StartTime[5],
	    smonth    = schedTime[type].StartTime[4],
	    sday      = schedTime[type].StartTime[3],
	    smeridian = schedTime[type].StartTime[2],
	    sminute   = schedTime[type].StartTime[1],
	    shour     = schedTime[type].StartTime[0],
	    Interval = 0,
	    Passed = 0;

	// get the time if 0 is passed into function
	if (!theTime)
		localCacheTime(&theTime);

	today = localtime ( &theTime );

	if (!today)
		return ST_SLEEP;

	// how many days in the current month
	LastDay = DaysInMonth(today->tm_year+1900, today->tm_mon);

	//adjust the start hour to be a zero if it is noon, time is messed up
	if (shour == NOON)
	    shour = 0; 	

	// number of seconds from now until event time

	seconds = ((shour + smeridian *NOON) * MINUTEOFSECONDS + sminute ) *MINUTEOFSECONDS - ((today->tm_hour * MINUTEOFSECONDS + today->tm_sec
	// add an extra second to prevent function from matching a zillion times
	+ 1));

	yearoffset = yearoffset + today->tm_year;
	yearrange = yearoffset + 6;

	monthoffset = today->tm_mon;

	dayoffset = today->tm_mday;

	switch (schedTime[type].State){

		case FixedInterval:

			Interval = ((schedTime[type].Every[0] *HOURSINADAY + schedTime[type].Every[1]) * MINUTEOFSECONDS + schedTime[type].Every[2]) * MINUTEOFSECONDS;

			if (Interval < MINUTEOFSECONDS)
				Interval = MINUTEOFSECONDS;

			if (schedTime[type].StartOnNextMinute) {
				if (schedTime[type].Applied == FALSE){
					return MINUTEOFSECONDS-today->tm_sec;
				} else {

					seconds = Interval -1;
					goto daylight_calculation;
				}

			}

			// fall thru here if not using current time

			if (syear == 0)
				syear = today->tm_year+1900;

			if (smonth == 0)
				smonth = today->tm_mon;
			else
				smonth--;

			if (yearrange < syear)
				yearrange = syear + 1;

			if (sday == 0)
				sday = today->tm_mday;

			if(syear == today->tm_year+1900 && smonth == today->tm_mon && sday == today->tm_mday){

				while (seconds < 0)
					seconds = seconds + Interval;

				goto daylight_calculation;

			}

			// has the start date already passed?

			Passed = FALSE;

			if (syear < today->tm_year+1900)
				Passed = TRUE;
			else if (syear == (today->tm_year+1900) && smonth < today->tm_mon)
				Passed = TRUE;
			else if (syear == (today->tm_year+1900) && smonth == today->tm_mon && sday < today->tm_mday)
				Passed = TRUE;

			if (Passed) {

				int x;

				// how many seconds ago was the start time?

				x = syear;
				syear = yearoffset;
				yearoffset = x;

				x=smonth;
				smonth = monthoffset;
				monthoffset = x;

				x=sday;
				sday=dayoffset;
				dayoffset = x;

			}

			break;

	case EveryWeekDay:

		// special case, today is scheduled

		// if the day of week for today is checked AND
		if (schedTime[type].DaysOfWeek[today->tm_wday] && (

			// the current day of the week is checked OR
			schedTime[type].WeeksOfMonth[(today->tm_mday-1)/DAYSINWEEK] || (
			//today is in the last week
			// AND the last week check box is selected
			!((LastDay-today->tm_mday)/DAYSINWEEK) && schedTime[type].WeeksOfMonth[5]
		)
		) ) {

			// if events are positive here then the event is scheduled to happen today
			if (seconds > 0)
				goto daylight_calculation;

			// otherwise seconds are less than now
			// we missed the event for today
			// continue checking out further for the next time we are supposed to run
		}

		// set the weekday ahead a day to check to see if we run tomorrow
		wday = today->tm_wday;
		wday++;

		// if the week is over, restart at the first day of the week, sunday
		if (wday>=DAYSINWEEK)
			wday -= DAYSINWEEK;

		break;

	case EveryMonthDay:

		//if the day of the month for today is checked AND
		if ( ( schedTime[type].MonthsOfYear[today->tm_mon] && 
			schedTime[type].DaysOfMonth[today->tm_mday-1] ) ||

			// this is the last day and last day of month is checked
			( ( LastDay == today->tm_mday ) &&
			  ( ( ( schedTime[type].DaysOfMonth[today->tm_mon] ) &&
				( schedTime[type].DaysOfMonth[LastDay-1] )  ) ||
				  schedTime[type].DaysOfMonth[31]) 
			  )
			) {

				if (seconds > 0)
					goto daylight_calculation;

				//otherwise seconds are less than now
				// missed the event for today
				// continue checking for next scheduled run event

			}
			break;

	}

	//  what if we just fell off the end of the month?

	if(LastDay == dayoffset){
		dayoffset = 0;
		monthoffset++;
	}

	//what if we just fell off the end of the year?
	if(monthoffset > 11) {
		monthoffset=0;
		yearoffset++;

	}

	// continue checking for a match

	// for each year

	for (year = yearoffset; year < yearoffset + yearoffset; year++ ){

		// for each month in the year
		// the offset is to take into account that we are
		// NOT starting our check at the beginning of the current year.
		for(month = 0+monthoffset; month < NOON; month++ ){

			//how many days does the current month have?
			LastDay = DaysInMonth(year, month);

			// for each day in the current month
			// the days in month offset is to take into account that we are 
			// NOT staring our check at the begginning of the current month
			for (day=1+dayoffset; day <= LastDay; day++){

				switch(schedTime[type].State){
					case FixedInterval:

						if(year == syear && month == smonth && day == sday){
							if(Passed){
								// we got from now til then
								seconds = -1 * accumulatedDays * DAYOFSECONDS + seconds;
								while(seconds <0)
									seconds += Interval;
							} else {
								// schedule to run on the given date
								seconds = (accumulatedDays) * DAYOFSECONDS + seconds;
							}

							goto daylight_calculation;
						}
						
						break;

					case EveryWeekDay:

						// if the day of the week for day is checked AND

						if (schedTime[type].DaysOfWeek[wday] && (

							schedTime[type].WeeksOfMonth[(day-1)/DAYSINWEEK] || (

								// we are in the last week
								// AND the last week check box is selected
								!((LastDay-day)/DAYSINWEEK) && schedTime[type].WeeksOfMonth[5] ) ) ) {

							// calculate the number of seconds to offset from
							// this is the number of days we have accumulated TIMES
							// the number of seconds in a day PLUS 
							// the offset from current time to the scheduled time
							// we calculated above
							seconds = (accumulatedDays)*DAYOFSECONDS + seconds;

							goto daylight_calculation;
						}

						// no match, add another day to the interval
						wday++;

						// if the week is over,
						if (wday>=DAYSINWEEK)
							wday -= DAYSINWEEK;

						break;

					case EveryMonthDay:

						// if month AND day are checked and match current month and day


						if (    ( ( schedTime[type].MonthsOfYear[month] ) && ( schedTime[type].DaysOfMonth[day-1] ) ) || 
							( ( day == LastDay ) && 
							  ( ( schedTime[type].MonthsOfYear[month] ) && 
							    ( ( schedTime[type].DaysOfMonth[LastDay-1] ) || 
							      ( schedTime[type].DaysOfMonth[31] ) 
							    )
							  )
							)
						    ) {

							// calculate the number of seconds to offset from
							seconds = accumulatedDays * DAYOFSECONDS + seconds;

							goto daylight_calculation;
						}
						break;
					}
					accumulatedDays++;
				}
				//turn off the offset once the first month is done
				dayoffset = 0;

			}

			// once the first year is finished, turn off the month offset
			monthoffset = 0;

		}

		return ST_SLEEP;

	daylight_calculation:
		
		// if we are in daylight savings and we end up in standard time, add an hour

		if (DaylightSavings(theTime) && !(DaylightSavings(theTime+seconds)))
			seconds += HOUROFSECONDS;

		// if we are in standar time and we end up in daylight savings, subtract an hour
		if (!DaylightSavings(theTime) && (DaylightSavings(theTime+seconds)))
			seconds -= HOUROFSECONDS;

		// if we somehow stumble onto the seconds being the same as the error return then fix it

		if (seconds == ST_SLEEP)
			seconds = seconds + MINUTEOFSECONDS;

		// add the second that we subtracted above
		return ( seconds + 1 );
}

typedef void * StrObj;
/*
StrObj ListEvents(mytime time, int count) {

	StrObj retval = NULL;

	time_t Time;
	time_t Seconds;

	int i = count;

	char * Date;

	time (&Time);

	for(;i;i--) {

		Seconds = EventDelay(&time, 1, Time);
		time[1].Applied = TRUE;

		Time += Seconds;
		Date=FormatDate(&Time);

		if (Seconds==ST_SLEEP || Date[0] == '\0') {
			StrCat ( retval, "Dates past 2038 cannot be shown.");
			StrAppendNewLine(retval);
		} else {
			StrCat ( &retval, Date ) 
			StrAppendNewLine(retval);
		}
	}

	return (retval);

}	

void Do_GetNow_change(PDEV_DATA pData) {

	if ( NumberAt(pData,NowFormat)==0 ) {		// X is Unix time_t seconds

		time_t theTime; 
		time(&theTime);

		Integer_change(pData, NowOutput, theTime);
	}
	else if ( NumberAt(pData,NowFormat)==1) {	// dd/mm/yy hh:mm:ss

		time_t		theTime;
		struct tm	*today;
		char		number[32]; 
		char		theDate[128];
		//char		timeStr[32]; 

		strcpy(theDate,"");

		time(&theTime);
		today = localtime( &theTime );
		
		itoa( today->tm_mday, number, 10 );
		if ( today->tm_mday < 10)
			strcat( theDate,"0");
		strcat( theDate, number );
		strcat( theDate, "/" );

		itoa( (today->tm_mon+1), number, 10 );	// month is 0-11
		if ( today->tm_mon < 9)
			strcat( theDate,"0");
		strcat( theDate, number );
		strcat( theDate, "/" );

		if ( today->tm_year > 99 ) 
			today->tm_year -=100;

		itoa( today->tm_year, number, 10 );
		if ( today->tm_year < 10)
			strcat( theDate,"0");
		strcat( theDate, number );
		strcat( theDate, " " );

		itoa( today->tm_hour, number, 10 );
		if ( today->tm_hour < 10)
			strcat( theDate, "0");
		strcat( theDate, number );
		strcat( theDate, ":");

		itoa( today->tm_min, number, 10 );
		if ( today->tm_min < 10)
			strcat( theDate, "0");
		strcat( theDate, number );
		strcat( theDate, ":");

		itoa( today->tm_sec, number, 10 );
		if ( today->tm_sec < 10)
			strcat( theDate, "0");
		strcat( theDate, number );

		String_change( pData, NowOutput, theDate );
	}
	else if ( NumberAt(pData,NowFormat)==2 ) {    // mm/dd/yy hh:mm:ss
		time_t		theTime;
		struct tm	*today;
		char		number[32]; 
		char		theDate[128];
		//char		timeStr[32]; 

		strcpy(theDate,"");

		time(&theTime);
		today = localtime( &theTime );
		
		itoa( (today->tm_mon+1), number, 10 );	// month is 0-11
		if ( today->tm_mon < 9)
			strcat( theDate,"0");
		strcat( theDate, number );
		strcat( theDate, "/" );

		itoa( today->tm_mday, number, 10 );
		if ( today->tm_mday < 10)
			strcat( theDate,"0");
		strcat( theDate, number );
		strcat( theDate, "/" );

		if ( today->tm_year > 99 ) 
			today->tm_year -=100;

		itoa( today->tm_year, number, 10 );
		if ( today->tm_year < 10)
			strcat( theDate,"0");
		strcat( theDate, number );
		strcat( theDate, " " );

		itoa( today->tm_hour, number, 10 );
		if ( today->tm_hour < 10)
			strcat( theDate, "0");
		strcat( theDate, number );
		strcat( theDate, ":");

		itoa( today->tm_min, number, 10 );
		if ( today->tm_min < 10)
			strcat( theDate, "0");
		strcat( theDate, number );
		strcat( theDate, ":");

		itoa( today->tm_sec, number, 10 );
		if ( today->tm_sec < 10)
			strcat( theDate, "0");
		strcat( theDate, number );

		String_change( pData, NowOutput, theDate );

	}
	else if ( NumberAt(pData,NowFormat)==3 ) {    // yy/mm/dd/ hh:mm:ss
		time_t		theTime;
		struct tm	*today;
		char		number[32]; 
		char		theDate[128];
		//char		timeStr[32]; 

		strcpy(theDate,"");

		time(&theTime);
		today = localtime( &theTime );

		if ( today->tm_year > 99 ) 
			today->tm_year -=100;

		itoa( today->tm_year, number, 10 );
		if ( today->tm_year < 10)
			strcat( theDate,"0");
		strcat( theDate, number );
		strcat( theDate, "/" );
		
		itoa( (today->tm_mon+1), number, 10 );	// month is 0-11
		if ( today->tm_mon < 9)
			strcat( theDate,"0");
		strcat( theDate, number );
		strcat( theDate, "/" );

		itoa( today->tm_mday, number, 10 );
		if ( today->tm_mday < 10)
			strcat( theDate,"0");
		strcat( theDate, number );
		strcat( theDate, " " );

		itoa( today->tm_hour, number, 10 );
		if ( today->tm_hour < 10)
			strcat( theDate, "0");
		strcat( theDate, number );
		strcat( theDate, ":");

		itoa( today->tm_min, number, 10 );
		if ( today->tm_min < 10)
			strcat( theDate, "0");
		strcat( theDate, number );
		strcat( theDate, ":");

		itoa( today->tm_sec, number, 10 );
		if ( today->tm_sec < 10)
			strcat( theDate, "0");
		strcat( theDate, number );

		String_change( pData, NowOutput, theDate );
	}
	else if ( NumberAt(pData,NowFormat)==4 ) {    // mm/dd/yy hh:mm:ss
		time_t		theTime;
		struct tm	*today;
		char		number[32]; 
		char		theDate[128];
		//char		timeStr[32]; 

		strcpy(theDate,"");

		time(&theTime);
		today = localtime( &theTime );
		
		itoa( (today->tm_mon+1), number, 10 );	// month is 0-11
		if ( today->tm_mon < 9)
			strcat( theDate,"0");
		strcat( theDate, number );
		strcat( theDate, "/" );

		itoa( today->tm_mday, number, 10 );
		if ( today->tm_mday < 10)
			strcat( theDate,"0");
		strcat( theDate, number );
		strcat( theDate, "/" );

		//if ( today->tm_year > 99 ) 
		//	today->tm_year -=100;

		today->tm_year += 1900;

		itoa( today->tm_year, number, 10 );
		//if ( today->tm_year < 10)
		//	strcat( theDate,"0");

		strcat( theDate, number );
		strcat( theDate, " " );

		itoa( today->tm_hour, number, 10 );
		if ( today->tm_hour < 10)
			strcat( theDate, "0");
		strcat( theDate, number );
		strcat( theDate, ":");

		itoa( today->tm_min, number, 10 );
		if ( today->tm_min < 10)
			strcat( theDate, "0");
		strcat( theDate, number );
		strcat( theDate, ":");

		itoa( today->tm_sec, number, 10 );
		if ( today->tm_sec < 10)
			strcat( theDate, "0");
		strcat( theDate, number );

		String_change( pData, NowOutput, theDate );

	}
}

*/
