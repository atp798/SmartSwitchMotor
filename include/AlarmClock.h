#ifndef __ALARM_CLOCK_H__
#define __ALARM_CLOCK_H__

#define ALARM_CHK_INTVL	1000
#define MAX_ALARM_NUM	10
#define ALARM_EXPIRE	86400

#define MONDAY		0x01
#define TUESDAY		0x02
#define WEDNESDAY	0x04
#define THURSDAY	0x08
#define FRIDAY		0x10
#define SATURDAY	0x20
#define SUNDAY		0x40

typedef enum ERepeatMode{
	ALARM_DISALBE = -127,
	ONCE_ALARMED = 0,
	RPT_ONLY_ONCE = 1,
	EVERY_HOUR = 2,
	EVERY_DAY = 3,
	EVERY_WEEK = 4,
	EVERY_MONTH = 5,
	EVERY_YEAR = 6,
	INTVL_SECS = 7,
	INTVL_MINS = 8,
	INTVL_HOURS = 9,
	INTVL_DAYS = 10,
	DAYS_OF_WEEK = 11,
	//DAYS_OF_MONTH
}EPepeatMode;

typedef struct STAlarm{
	uint8 ID;
	EPepeatMode RptMode;
	uint32 Start;
	uint32 Stop;
	uint32 RptIntvl; //<0 error, =0 for no repeat, >0 for seconds wait to realarm
}STAlarm;

bool SetAlarm(STAlarm* stAlrm);
void EnableAllAlarms();
void DisableAllAlarms();
bool EnableAlarm(uint8 ID);
bool DisableAlarm(uint8 ID);
void AlarmTimerInit();

#endif
