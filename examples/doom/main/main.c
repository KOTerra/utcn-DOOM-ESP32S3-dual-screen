#include "esp_log.h"

#include "mcugdx.h"
#include "driver/uart.h"
#include <stdint.h>
#include "driver/gpio.h"


#include "doomgeneric/doomgeneric.h"
#include "doomgeneric/w_file.h"
#include "doomgeneric/i_sound.h"
#include "doomgeneric/m_misc.h"
#include "doomgeneric/w_wad.h"
#include "doomgeneric/z_zone.h"
#include "doomgeneric/deh_str.h"
#include "doomgeneric/doomtype.h"
#include "doomgeneric/doomkeys.h"
#include "doomgeneric/doomtype.h"
#include "doomgeneric/d_player.h"
#include "doomgeneric/doomstat.h"
#include "doomgeneric/d_items.h"


#define TAG "DOOM"
#define DEBOUNCE_TIME 25

// Big & Little blue fucker
#if 1
mcugdx_display_config_t display_config = {
		.driver = MCUGDX_ST7789,
		.native_width = 240,
		.native_height = 320,
		.mosi = 13,// SDA in your Adafruit sketch
		.sck = 18, // SCLK
		.dc = 2,
		.cs = 15,
		.reset = 4};

#else
// ILI9341 2,8" 240x320
mcugdx_display_config_t display_config = {
		.driver = MCUGDX_ILI9341,
		.native_width = 240,
		.native_height = 320,
		.mosi = 3,
		.sck = 4,
		.dc = 2,
		.cs = 1,
		.reset = 5};
#endif


mcugdx_sound_t *mcugdx_sound_load_raw(
		int16_t *frames,
		uint32_t num_frames,
		mcugdx_audio_channels_t channels,
		uint32_t sample_rate,
		mcugdx_memory_type_t memory_type) {
	// Temporary stub: no real sound loading yet
	return NULL;
}


typedef struct {
	wad_file_t wad;
	mcugdx_file_handle_t handle;
} rofs_wad_file_t;

extern wad_file_class_t rofs_wad_file;

static wad_file_t *doom_rofs_open_file(char *path) {
	if (!mcugdx_rofs.exists(path)) return NULL;

	rofs_wad_file_t *result;
	result = mcugdx_mem_alloc(sizeof(rofs_wad_file_t), MCUGDX_MEM_EXTERNAL);
	result->handle = mcugdx_rofs.open(path);
	result->wad.file_class = &rofs_wad_file;
	result->wad.mapped = NULL;
	result->wad.length = mcugdx_rofs.length(result->handle);
	return &result->wad;
}

#include <stddef.h>


static void doom_rofs_close_file(wad_file_t *wad) {
	rofs_wad_file_t *rofs_wad = (rofs_wad_file_t *) wad;
	if (rofs_wad) {
		if (mcugdx_rofs.close) {
			mcugdx_rofs.close(rofs_wad->handle);
		}
		mcugdx_mem_free(rofs_wad);
	}
}


static size_t doom_rofs_read(wad_file_t *wad, unsigned int offset, void *buffer, size_t buffer_len) {
	rofs_wad_file_t *rofs_wad = (rofs_wad_file_t *) wad;

	if (mcugdx_rofs.seek) {
		mcugdx_rofs.seek(rofs_wad->handle, offset);
	}

	if (mcugdx_rofs.read) {
		return mcugdx_rofs.read(rofs_wad->handle, buffer, buffer_len);
	}

	return 0;
}


wad_file_class_t rofs_wad_file = {
		doom_rofs_open_file,
		doom_rofs_close_file,
		doom_rofs_read,
};

boolean doom_init_sound(boolean _use_sfx_prefix) {
	// no-op
	return true;
}

void doom_shut_down_sound(void) {
	// no-op
}

void get_sfx_lump_num(sfxinfo_t *sfx, char *buf, size_t buf_len) {

	if (sfx->link != NULL) {
		sfx = sfx->link;
	}
	M_snprintf(buf, buf_len, "ds%s", DEH_String(sfx->name));
}

int doom_get_sfx_lump_num(sfxinfo_t *sfx) {
	char namebuf[9];
	get_sfx_lump_num(sfx, namebuf, sizeof(namebuf));
	return W_GetNumForName(namebuf);
}

void doom_update_sound(void) {
	// no-op
}

void doom_update_sound_params(int handle, int vol, int sep) {
	mcugdx_sound_set_volume(handle, vol);
}

int doom_start_sound(sfxinfo_t *sfxinfo, int channel, int vol, int sep) {
	if (sfxinfo->driver_data) {
		mcugdx_log(TAG, "sep: %li", sep);
		return mcugdx_sound_play((mcugdx_sound_t *) sfxinfo->driver_data, vol, sep, MCUGDX_SINGLE_SHOT);
	} else {
		return -1;
	}
}

void doom_stop_sound(int handle) {
	mcugdx_sound_stop(handle);
}

boolean doom_sound_is_playing(int handle) {
	return mcugdx_sound_is_playing(handle);
}

void doom_precache_sounds(sfxinfo_t *sounds, int num_sounds) {
	for (int i = 0; i < num_sounds; i++) {
		char namebuf[9];
		sfxinfo_t *sfxinfo = &sounds[i];
		get_sfx_lump_num(sfxinfo, namebuf, sizeof(namebuf));
		sfxinfo->lumpnum = W_CheckNumForName(namebuf);
		if (sounds[i].lumpnum == -1) {
			continue;
		}
		unsigned int lumpnum = sfxinfo->lumpnum;
		byte *data = W_CacheLumpNum(lumpnum, PU_STATIC);
		unsigned int lumplen = W_LumpLength(lumpnum);

		if (lumplen < 8 || data[0] != 0x03 || data[1] != 0x00) {
			W_ReleaseLumpNum(lumpnum);
			continue;
		}

		int samplerate = (data[3] << 8) | data[2];
		unsigned int length = (data[7] << 24) | (data[6] << 16) | (data[5] << 8) | data[4];

		mcugdx_log("DOOM_AUDIO", "Caching sound: %s, sample rate: %li, length: %li", namebuf, samplerate, length);

		if (length > lumplen - 8 || length <= 48) {
			W_ReleaseLumpNum(lumpnum);
			continue;
		}

		data += 16;
		length -= 32;


		int16_t *frames = mcugdx_mem_alloc(length * sizeof(int16_t), MCUGDX_MEM_EXTERNAL);
		for (int j = 0; j < length; j++) {
			float sample = data[j] / 127.5 - 1;
			frames[j] = (int16_t) (sample * 32767.0f);
		}
		mcugdx_sound_t *sound = mcugdx_sound_load_raw(frames, length, MCUGDX_MONO, samplerate, MCUGDX_MEM_EXTERNAL);

		W_ReleaseLumpNum(lumpnum);

		sfxinfo->driver_data = sound;
	}
}

static snddevice_t doom_sound_devices[] = {
		SNDDEVICE_SB,
		SNDDEVICE_PAS,
		SNDDEVICE_GUS,
		SNDDEVICE_WAVEBLASTER,
		SNDDEVICE_SOUNDCANVAS,
		SNDDEVICE_AWE32,
};

sound_module_t DG_sound_module = {
		doom_sound_devices,
		arrlen(doom_sound_devices),
		doom_init_sound,
		doom_shut_down_sound,
		doom_get_sfx_lump_num,
		doom_update_sound,
		doom_update_sound_params,
		doom_start_sound,
		doom_stop_sound,
		doom_sound_is_playing,
		doom_precache_sounds,
};

typedef struct __attribute__((packed)) {
	uint8_t health;  // 1 byte
	uint16_t bullets;// 2 bytes
	uint8_t shells;  // 1 byte
	uint8_t rockets; // 1 byte
	uint16_t cells;  // 2 bytes
} game_data_t;

#define GAME_UART_PORT UART_NUM_1

void send_player_data_raw(const player_t *plr) {
	game_data_t pkt;

	pkt.health = (uint8_t) plr->health;
	pkt.bullets = (uint16_t) plr->ammo[am_clip];
	pkt.shells = (uint8_t) plr->ammo[am_shell];
	pkt.rockets = (uint8_t) plr->ammo[am_misl];
	pkt.cells = (uint16_t) plr->ammo[am_cell];

	const char header[] = {0xAA, 0x55};
	uart_write_bytes(GAME_UART_PORT, header, 2);

	uart_write_bytes(GAME_UART_PORT, (const char *) &pkt, sizeof(pkt));
}

#define UART_TX_PIN GPIO_NUM_17

void init_uart_raw() {
	const uart_config_t uart_config = {
			.baud_rate = 115200,
			.data_bits = UART_DATA_8_BITS,
			.parity = UART_PARITY_DISABLE,
			.stop_bits = UART_STOP_BITS_1,
			.flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
			.source_clk = UART_SCLK_DEFAULT,
	};

	uart_driver_install(GAME_UART_PORT, 256, 0, 0, NULL, 0);
	uart_param_config(GAME_UART_PORT, &uart_config);

	uart_set_pin(GAME_UART_PORT, UART_TX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}

void DG_Init() {
	DG_ScreenBuffer = (pixel_t *) (mcugdx_display_frame_buffer() + mcugdx_display_width() * 20);
}

void DG_SetWindowTitle(const char *title) {
}

int frames = 0;
double last_frame_time = -1;
double last_uart_send_time = -1;
void DG_DrawFrame(void) {
	mcugdx_display_show();

	frames++;
	if (frames > 60 * 5) {
		frames = 0;
		if (last_frame_time != -1) {
			double frame_time = (mcugdx_time() - last_frame_time) * 1000;
			mcugdx_log(TAG, "Frame time: %f ms, %f fps", frame_time, 1000 / frame_time);
		}
	}
	last_frame_time = mcugdx_time();
	player_t *plr = &players[consoleplayer];

	double current_time = mcugdx_time() * 1000;
	if (current_time - last_uart_send_time > 50) {
		send_player_data_raw(plr);
		last_uart_send_time = current_time;
	}
}

uint32_t DG_GetTicksMs() {
	uint32_t ticks = (uint32_t) (mcugdx_time() * 1000);
	return ticks;
}

void DG_SleepMs(uint32_t ms) {
	mcugdx_sleep(ms);
}

int DG_GetKey(int *pressed, unsigned char *doomKey) {
	mcugdx_button_event_t event;
	if (mcugdx_button_get_event(&event)) {
		*pressed = event.type == MCUGDX_BUTTON_PRESSED ? -1 : 0;
		switch (event.keycode) {
			case MCUGDX_KEY_ESCAPE:
				*doomKey = KEY_ESCAPE;
				break;
			case MCUGDX_KEY_ENTER:
				*doomKey = KEY_ENTER;
				break;
			case MCUGDX_KEY_A:
				*doomKey = KEY_LEFTARROW;
				break;
			case MCUGDX_KEY_S:
				*doomKey = KEY_DOWNARROW;
				break;
			case MCUGDX_KEY_D:
				*doomKey = KEY_RIGHTARROW;
				break;
			case MCUGDX_KEY_W:
				*doomKey = KEY_UPARROW;
				break;
			case MCUGDX_KEY_K:
				*doomKey = KEY_FIRE;
				break;
			case MCUGDX_KEY_L:
				*doomKey = KEY_USE;
				break;
			default:
				return 0;
		}
		return -1;
	} else {
		return 0;
	}
}
int mcugdx_main() {
	ESP_LOGI("MAIN", "Testing printf and logging...");
	printf("Hello from ESP32!\n");

	mcugdx_init();
	init_uart_raw();
	mcugdx_display_init(&display_config);

	mcugdx_display_set_orientation(MCUGDX_LANDSCAPE);
	mcugdx_rofs_init();

	mcugdx_button_create(5, DEBOUNCE_TIME, MCUGDX_KEY_K);
	mcugdx_button_create(6, DEBOUNCE_TIME, MCUGDX_KEY_L);
	mcugdx_button_create(7, DEBOUNCE_TIME, MCUGDX_KEY_ESCAPE);
	mcugdx_button_create(8, DEBOUNCE_TIME, MCUGDX_KEY_ENTER);
	mcugdx_button_create(9, DEBOUNCE_TIME, MCUGDX_KEY_D);
	mcugdx_button_create(10, DEBOUNCE_TIME, MCUGDX_KEY_S);
	mcugdx_button_create(11, DEBOUNCE_TIME, MCUGDX_KEY_A);
	mcugdx_button_create(12, DEBOUNCE_TIME, MCUGDX_KEY_W);


	char *args[] = {"doomgeneric", "-iwad", "Doom1.WAD", "-mmap"};
	doomgeneric_Create(4, args);
	mcugdx_log(TAG, "Game created");
	while (true) {
		doomgeneric_Tick();
	}

	return 0;
}