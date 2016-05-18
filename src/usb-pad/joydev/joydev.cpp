#include "joydev.h"
#include "../../USB.h"

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <iostream>
#include <sstream>
#include <math.h>

#if _DEBUG
#define Dbg(...) fprintf(stderr, __VA_ARGS__)
#else
#define Dbg(...)
#endif

#define APINAME "joydev"

extern bool file_exists(std::string path);
extern bool dir_exists(std::string path);
static bool sendCrap = false;

#define NORM(x, n) (((uint32_t)(32767 + x) * n)/0xFFFE)

static inline int range_max(PS2WheelTypes type)
{
	if(type == WT_DRIVING_FORCE_PRO)
		return 0x3FFF;
	return 0x3FF;
}

int JoyDevPad::TokenIn(uint8_t *buf, int buflen)
{
	ssize_t len;

	int range = range_max(mType);

	if(sendCrap)
	{
		// Setting to unpressed
		memset(&mWheelData, 0, sizeof(wheel_data_t));
		mWheelData.axis_x = range >> 1;
		mWheelData.axis_y = 0xFF;
		mWheelData.axis_z = 0xFF;
		mWheelData.axis_rz = 0xFF;
		mWheelData.hatswitch = 0x8;
		pad_copy_data(mType, buf, mWheelData);
		sendCrap = false;
		return len;
	}

	struct js_event event;

	//Non-blocking read sets len to -1 and errno to EAGAIN if no new data
	//TODO what happens when emulator is paused?
	while((len = read(mHandle, &event, sizeof(event))) > -1)
	{
		//Dbg("Read js len: %d %d\n", len, errno);
		if (len == sizeof(event))
		{ // ok
			if (event.type & ~JS_EVENT_INIT == JS_EVENT_AXIS)
			{
				//Dbg("Axis: %d, %d\n", event.number, event.value);
				switch(mAxisMap[event.number])
				{
					case ABS_X: mWheelData.axis_x = NORM(event.value, range); break;
					case ABS_Y: mWheelData.axis_y = NORM(event.value, 0xFF); break;
					case ABS_Z: mWheelData.axis_z = NORM(event.value, 0xFF); break;
					//case ABS_RX: mWheelData.axis_rx = NORM(event.value, 0xFF); break;
					//case ABS_RY: mWheelData.axis_ry = NORM(event.value, 0xFF); break;
					case ABS_RZ: mWheelData.axis_rz = NORM(event.value, 0xFF); break;

					//FIXME hatswitch mapping
					//TODO Ignoring diagonal directions
					case ABS_HAT0X:
					case ABS_HAT1X:
						if(event.value < 0 ) //left usually
							mWheelData.hatswitch = PAD_HAT_W;
						else if(event.value > 0 ) //right
							mWheelData.hatswitch = PAD_HAT_E;
						else
							mWheelData.hatswitch = PAD_HAT_COUNT;
					break;
					case ABS_HAT0Y:
					case ABS_HAT1Y:
						if(event.value < 0 ) //up usually
							mWheelData.hatswitch = PAD_HAT_N;
						else if(event.value > 0 ) //down
							mWheelData.hatswitch = PAD_HAT_S;
						else
							mWheelData.hatswitch = PAD_HAT_COUNT;
					break;
					default: break;
				}
			}
			else if (event.type & ~JS_EVENT_INIT == JS_EVENT_BUTTON)
			{
				//Dbg("Button: %d, %d\n", event.number, event.value);
				//TODO can have 12 bits for buttons?
				if(event.number < 10)
				{
					//FIXME bit juggling
					if(event.value)
						mWheelData.buttons |= 1 << event.number; //on
					else
						mWheelData.buttons &= ~(1 << event.number); //off
				}
			}
		}
		else
		{
			Dbg("usb_pad_poll: unknown read error\n");
			break;
		}
	}

	//Dbg("call pad_copy_data\n");
	pad_copy_data(mType, buf, mWheelData);
	return buflen;
}

//TODO Get rid of player_joys
bool JoyDevPad::FindPad()
{
	memset(&mWheelData, 0, sizeof(wheel_data_t));

	// Setting to unpressed
	mWheelData.axis_x = 0x3FF >> 1;
	mWheelData.axis_y = 0xFF;
	mWheelData.axis_z = 0xFF;
	mWheelData.axis_rz = 0xFF;
	memset(mAxisMap, -1, sizeof(mAxisMap));
	memset(mBtnMap, -1, sizeof(mBtnMap));

	mAxisCount = 0;
	mButtonCount = 0;
	mHandle = -1;
	mHandleFF = -1;
	mEffect.id = -1;

	std::string joypath;
	{
		CONFIGVARIANT var(N_CONFIG_JOY, CONFIG_TYPE_CHAR);
		if(LoadSetting(mPort, APINAME, var))
			joypath = var.strValue;
		else
		{
			Dbg("Cannot load joystick setting: %s\n", N_CONFIG_JOY);
			return false;
		}
	}
	if(!joypath.empty() && file_exists(joypath))
	{
		if ((mHandle = open(joypath.c_str(), O_RDONLY | O_NONBLOCK)) < 0)
		{
			Dbg("Cannot open joystick: %s\n", joypath.c_str());
		}
		else
		{
			//int flags = fcntl(mHandle, F_GETFL, 0);
			//fcntl(mHandle, F_SETFL, flags | O_NONBLOCK);

			// Axis Mapping
			if (ioctl(mHandle, JSIOCGAXMAP, mAxisMap) < 0)
			{
				Dbg("Axis mapping: %s\n", strerror(errno));
			}
			else
			{
				ioctl(mHandle, JSIOCGAXES, &mAxisCount);
				for(int i = 0; i < mAxisCount; ++i)
					Dbg("Axis: %d -> %d\n", i, mAxisMap[i] );
			}

			// Button Mapping
			if (ioctl(mHandle, JSIOCGBTNMAP, mBtnMap) < 0)
			{
				Dbg("Button mapping: %s\n", strerror(errno));
			}
			else
			{
				ioctl(mHandle, JSIOCGBUTTONS, &mButtonCount);
				for(int i = 0; i < mButtonCount; ++i)
					Dbg("Button: %d -> %d \n", i, mBtnMap[i] );
			}

			std::stringstream event;
			int index = 0;
			const char *tmp = joypath.c_str();
			while(*tmp && !isdigit(*tmp))
				tmp++;

			sscanf(tmp, "%d", &index);
			Dbg("input index: %d of '%s'\n", index, joypath.c_str());

			for (int j = 0; j <= 99; j++)
			{
				event.clear(); event.str(std::string());
				/* Try to discover the corresponding event number */
				event << "/sys/class/input/js" << index << "/device/event" << j;
				if (dir_exists(event.str())){

					event.clear(); event.str(std::string());
					event << "/dev/input/event" << j;
					break;
				}
			}
			if ((mHandleFF = open(event.str().c_str(), O_WRONLY)) < 0)
			{
				Dbg("Cannot open '%s'\n", event.str().c_str());
			}
			else
			{
				mEffect.type = FF_CONSTANT;
				mEffect.id = -1;
				mEffect.u.constant.level = 0;	/* Strength : 25 % */
				mEffect.direction = 0x4000;
				mEffect.u.constant.envelope.attack_length = 0;//0x100;
				mEffect.u.constant.envelope.attack_level = 0;
				mEffect.u.constant.envelope.fade_length = 0;//0x100;
				mEffect.u.constant.envelope.fade_level = 0;
				mEffect.trigger.button = 0;
				mEffect.trigger.interval = 0;
				mEffect.replay.length = 0x7FFFUL;  /* mseconds */
				mEffect.replay.delay = 0;

				if (ioctl(mHandleFF, EVIOCSFF, &(mEffect)) < 0) {
					Dbg("Failed to upload effect to '%s'\n", event.str().c_str());
				}

				SetGain(75);
				SetAutoCenter(0);
			}
			return true;
		}
	}

	return false;
}

void JoyDevPad::SetConstantForce(int force)
{
	struct input_event play;
	play.type = EV_FF;
	play.code = mEffect.id;
	play.value = 0;
	//if (write(mHandleFF, (const void*) &play, sizeof(play)) == -1) {
	//    Dbg("stop effect failed\n");
	//}

	Dbg("Force: %d\n", force);
	//mEffect.type = FF_CONSTANT;
	//mEffect.id = -1;
	//mEffect.u.constant.level = 0x8000;	/* Strength : 0x2000 == 25 % */
	//mEffect.direction = (0xFFFF * (force)) / 255;
	int y = 0, x = 0;
	float magnitude = -(127-force) * 78.4803149606299;
	mEffect.u.constant.level = (MAX(MIN(magnitude, 10000), -10000) / 10) * 32;
	mEffect.direction = 0x4000; //(int)((3 * M_PI / 2 - atan2(y, x)) * -0x7FFF / M_PI);
	Dbg("dir: %d lvl: %d\n", mEffect.direction, mEffect.u.constant.level);

//	mEffect.u.constant.envelope.attack_length = 0;
//	mEffect.u.constant.envelope.attack_level = 0;
//	mEffect.u.constant.envelope.fade_length = 0;
//	mEffect.u.constant.envelope.fade_level = 0;
//	mEffect.trigger.button = 0;
//	mEffect.trigger.interval = 0;
//	mEffect.replay.length = 0xFFFF;  /* INFINITE mseconds */
//	mEffect.replay.delay = 0;

	if (ioctl(mHandleFF, EVIOCSFF, &(mEffect)) < 0) {
		Dbg("Failed to upload effect\n");
	}


	//play.type = EV_FF;
	play.code = mEffect.id;
	play.value = 1;
	if (write(mHandleFF, (const void*) &play, sizeof(play)) == -1) {
		Dbg("play effect failed\n");
	}

}

void JoyDevPad::SetSpringForce(int force)
{

}

void JoyDevPad::SetAutoCenter(int value)
{
	struct input_event ie;

	ie.type = EV_FF;
	ie.code = FF_AUTOCENTER;
	ie.value = value * 0xFFFFUL / 100;

	if (write(mHandleFF, &ie, sizeof(ie)) == -1)
		Dbg("Failed to set autocenter\n");
}

void JoyDevPad::SetGain(int gain /* between 0 and 100 */)
{
	struct input_event ie;

	ie.type = EV_FF;
	ie.code = FF_GAIN;
	ie.value = 0xFFFFUL * gain / 100;

	if (write(mHandleFF, &ie, sizeof(ie)) == -1)
		Dbg("Failed to set gain\n");
}

int JoyDevPad::TokenOut(const uint8_t *data, int len)
{
	struct ff_data* ffdata = (struct ff_data*) data;
	Dbg("FFB 0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x0%4X\n",
		ffdata->reportid, ffdata->index, ffdata->data1, ffdata->data2, *(int*)((char*)ffdata + 4) );

	switch(ffdata->reportid)
	{
		case 0xF8:
			//TODO needed?
			if(ffdata->index == 5)
				sendCrap = true;
			//TODO guess-work
			//if(ffdata.index == 3)
			//	SetAutoCenter(100);
		break;
		case 9:
			{
				//not handled
			}
			break;
		case 0x13:
			//some games issue this command on pause
			//if(ffdata.reportid == 19 && ffdata.data2 == 0)break;
			if(ffdata->index == 0x8)
				SetConstantForce(127); //data1 looks like previous force sent with reportid 0x11
			//TODO unset spring
			else if(ffdata->index == 3)
				SetSpringForce(127);

			//Dbg("FFB 0x%X, 0x%X, 0x%X\n", ffdata.reportid, ffdata.index, ffdata.data1);
			break;
		case 0x11://constant force
			{
				//handle calibration commands
				//if(!calibrating)
				{
					SetConstantForce(ffdata->data1);
				}
			}
			break;
		case 0x21:
			if(ffdata->index == 0xB)
			{
				//if(!calibrating)
				{
					//TODO guess-work
					SetAutoCenter(100);
					//SetSpringForce(ffdata.data1);
				}
				break;
			}
			//drop through
		case 0xFE://autocenter?
		case 0xFF://autocenter?
		case 0xF4://autocenter?
			//{
			//	SetAutoCenter(0);
			//}
			//break;
		case 0xF5://autocenter?
			{
				SetAutoCenter(0);
				//just release force
				SetConstantForce(127);
			}
			break;
		case 0xF1:
			//DF/GTF and GT3
			//if(!calibrating)
			{
				SetConstantForce(ffdata->pad1);
			}
			break;
		case 0xF3://initialize
			{
				//SetAutoCenter(0xFFFFUL);
			}
			break;
	}

	return len;
}

int JoyDevPad::Open()
{
	if (FindPad())
		return 0;
	return 1;
}

int JoyDevPad::Close()
{
	if(mHandle != -1)
		close(mHandle);

	if(mHandleFF != -1)
	{
		if (mEffect.id != -1 && ioctl(mHandleFF, EVIOCRMFF, mEffect.id) == -1)
		{
			Dbg("Failed to unload FF effect.\n");
		}
		close(mHandleFF);
	}

	mHandle = -1;
	mHandleFF = -1;
}

std::vector<CONFIGVARIANT> JoyDevPad::GetSettings()
{
	std::vector<CONFIGVARIANT> params;
	params.push_back(CONFIGVARIANT(S_CONFIG_JOY, N_CONFIG_JOY, CONFIG_TYPE_CHAR));
	return params;
}

REGISTER_PAD(APINAME, JoyDevPad);
#undef APINAME
#undef Dbg
