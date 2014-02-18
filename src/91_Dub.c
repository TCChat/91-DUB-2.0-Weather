#include "pebble.h"
#include <math.h>
	
static int blink = 0;
static int zeroes = 0;
static int vibrate = 0;
static int square = 0;
static int date = 0;
static int units = 0;

enum {
	KEY_ACTION = 1,	// 0-settings, 1-weather:request/OK, 2-weather failed, 3-JS ready, 4-GPS failed
	KEY_BLINK = 2,	// 0-Blinking, 1-Not blinkling
	KEY_ZEROES = 3,	// leading zeroes 0-Hide, 1-Show
	KEY_VIBRATE = 4,// BT connection vibrations 0-Vibrate, 1-No vibrations
	KEY_SQUARE = 5,	// Highligh of days 0-underscore, 1-square
	KEY_DATE = 6,	// Date format 0-Month Day, 1-Day Month
	KEY_UNITS = 7,	// Temperature units 0-Celsius, 1-Farenheit
	KEY_TEMP = 8,	// Weather - Temperature
	KEY_CITY = 9,	// Weather - City
	KEY_ICON = 10,	// Weather - Icon ID
	KEY_DESC = 11,	// Weather - Description
	KEY_LAST_UPDATE = 12
};

static Layer *weather_layer;
static GBitmap *image_place;

static bool weather_is_showing = false;
static bool JSready = false;
static bool response_received = false;

static char city[30];  			// City
static int temp = -25500;			// Temperature in Kelvins * 100
static char temp_buffer[15]; 	// Temperature and units in text form
static char icon[] = ")";		// Using icon font this means "N/A"
static char desc[40];			// Desc of the weather
static char time_buffer[50];

static time_t update_time = 0;	// Time of the last update - unix epoch time
static int seconds = -1;		// Seconds from the last update

static AppTimer *weather_timer;
static AppTimer *weather_update_timer;
static AppTimer *weather_update_timeout_timer;

static TextLayer *weather_update_layer;

static Window *window;

static GBitmap *background_image;
static BitmapLayer *background_layer;

static GBitmap *meter_bar_image;
static BitmapLayer *meter_bar_layer;
static BitmapLayer *meter_bar_mask_layer;

static GBitmap *bt_image;
static BitmapLayer *bt_layer;

static TextLayer *day_layer;
static InverterLayer *day_inv_layer;
static GFont *digital_font;
static GFont *weather_font;

static GBitmap *dots_image;
static BitmapLayer *dots_layer;

static TextLayer *time_format_layer;

#define TOTAL_TIME_DIGITS 4

static GBitmap *time_digits_images[TOTAL_TIME_DIGITS];
static BitmapLayer *time_digits_layers[TOTAL_TIME_DIGITS];

const int BIG_DIGIT_IMAGE_RESOURCE_IDS[] = {
  RESOURCE_ID_IMAGE_NUM_0,
  RESOURCE_ID_IMAGE_NUM_1,
  RESOURCE_ID_IMAGE_NUM_2,
  RESOURCE_ID_IMAGE_NUM_3,
  RESOURCE_ID_IMAGE_NUM_4,
  RESOURCE_ID_IMAGE_NUM_5,
  RESOURCE_ID_IMAGE_NUM_6,
  RESOURCE_ID_IMAGE_NUM_7,
  RESOURCE_ID_IMAGE_NUM_8,
  RESOURCE_ID_IMAGE_NUM_9
};

static TextLayer *date_layer[2];

static bool is_hidden = false;

const TimeUnits sec_unit = SECOND_UNIT|MINUTE_UNIT|HOUR_UNIT|DAY_UNIT|MONTH_UNIT;
const TimeUnits no_sec_unit = MINUTE_UNIT|HOUR_UNIT|DAY_UNIT|MONTH_UNIT;

static char text_buffer[] = "24H";
static char text_buffer0[] = "JAN";
static char text_buffer1[] = "DEC";

static int counter =0;

static void timer_update_callback(void *data);
static void timeout_callback(void *data);

unsigned short get_display_hour(unsigned short hour){
  if(clock_is_24h_style()){
    return hour;
  }
  unsigned short display_hour = hour % 12;
	
  // Converts "0" to "12"
  return display_hour ? display_hour : 12;
}

static void set_container_image(GBitmap **bmp_image, BitmapLayer *bmp_layer, const int resource_id){
  GBitmap *old_image = *bmp_image;
	
  *bmp_image = gbitmap_create_with_resource(resource_id);
  bitmap_layer_set_bitmap(bmp_layer, *bmp_image);
	
  gbitmap_destroy(old_image);
}

static void update_display(struct tm *current_time, TimeUnits units_changed){
  if(blink == 0){
    if(current_time->tm_sec % 2 == 0){
      layer_set_hidden(bitmap_layer_get_layer(dots_layer), false);
    } else {
      layer_set_hidden(bitmap_layer_get_layer(dots_layer), true);
    }
  }
	
  if(battery_state_service_peek().is_charging){
    if(current_time->tm_sec % 2 == 0){
      layer_set_hidden(bitmap_layer_get_layer(meter_bar_layer), false);
      is_hidden = false;
    } else {
      layer_set_hidden(bitmap_layer_get_layer(meter_bar_layer), true);
      is_hidden = true;
      }
  } else {
    if(is_hidden){
      layer_set_hidden(bitmap_layer_get_layer(meter_bar_layer), false);
      is_hidden = false;
    }
  }

  if (units_changed & MINUTE_UNIT) {
    set_container_image(&time_digits_images[2], time_digits_layers[2], BIG_DIGIT_IMAGE_RESOURCE_IDS[current_time->tm_min/10]);
    set_container_image(&time_digits_images[3], time_digits_layers[3], BIG_DIGIT_IMAGE_RESOURCE_IDS[current_time->tm_min%10]);
	  
    if (units_changed & HOUR_UNIT) {
      // TODO implement hourly display changes
      unsigned short display_hour = get_display_hour(current_time->tm_hour);
	
      set_container_image(&time_digits_images[0], time_digits_layers[0], BIG_DIGIT_IMAGE_RESOURCE_IDS[display_hour/10]);
      set_container_image(&time_digits_images[1], time_digits_layers[1], BIG_DIGIT_IMAGE_RESOURCE_IDS[display_hour%10]);
			
      if(display_hour < 10 && zeroes == 0) {
        layer_set_hidden(bitmap_layer_get_layer(time_digits_layers[0]), true);
      } else {
	    layer_set_hidden(bitmap_layer_get_layer(time_digits_layers[0]), false);
      }
		
	  if(!clock_is_24h_style()){
		  strftime(text_buffer, sizeof(text_buffer), "%p", current_time);
		  text_layer_set_text(time_format_layer, text_buffer);
	  }	else {
		  text_layer_set_text(time_format_layer, "24H");
	  }

		if (units_changed & DAY_UNIT) {
			char *formating0 = "%b";
			char *formating1 = "%e";
			if(date == 0){
				formating0 ="%b";
				if(zeroes == 0){
					formating1 = "%e";
				} else {
					formating1 = "%d";
				}
			} else {
				if(zeroes == 0){
					formating0 = "%e";
				} else {
					formating0 = "%d";
				}
				formating1 = "%b";
			}

			strftime(text_buffer0, sizeof(text_buffer0), formating0, current_time);
			text_layer_set_text(date_layer[0], text_buffer0);
			strftime(text_buffer1, sizeof(text_buffer1), formating1, current_time);
			text_layer_set_text(date_layer[1], text_buffer1);

			GRect frame = (GRect){{0, 15}, {15, 2}};
			if(square == 1){
				frame = (GRect){{0, 3}, {15, 13}};
			}

			int offset[] = {0, 16, 33, 48, 67, 82, 94};
			int width[] = {15, 17, 14, 19, 14, 12, 15};

			frame.origin.x = offset[current_time->tm_wday];
			frame.size.w = width[current_time->tm_wday];
			if(square == 0){
				frame.origin.x = frame.origin.x + 1;
				frame.size.w = frame.size.w - 2;
			}
			layer_set_frame(inverter_layer_get_layer(day_inv_layer), frame);

			if(units_changed & MONTH_UNIT){
				//Showing of month name is already solved during the day show
			}
		}
    }
  }
}

static void handle_tick(struct tm *tick_time, TimeUnits units_changed){
  update_display(tick_time, units_changed);
}

static void handle_bluetooth(bool connected){
  if(connected){
    //bitmap_layer_set_bitmap(bt_layer, bt_image);
    layer_set_hidden(bitmap_layer_get_layer(bt_layer), false);
  } else {
    //bitmap_layer_set_bitmap(bt_layer, NULL);
    layer_set_hidden(bitmap_layer_get_layer(bt_layer), true);
	  if(vibrate == 0){
		  vibes_long_pulse();
	  }
  }
}

static void handle_battery(BatteryChargeState charge_state){
  int percentage_offset = 12;

  if(charge_state.charge_percent > 10){
    percentage_offset = 11;
  }
  if(charge_state.charge_percent > 20){
    percentage_offset = 9;
  }
  if(charge_state.charge_percent > 40){
    percentage_offset = 6;
  }
  if(charge_state.charge_percent > 60){
    percentage_offset = 3;
  }
  if(charge_state.charge_percent > 80){
    percentage_offset = 0;
  }
  //GRect frame = (GRect){ .origin = { .x = 17, .y = 43}, {percentage_offset, 9}};
  GRect frame = (GRect){ .origin = { .x = 0, .y = 0}, {percentage_offset, 9}};
  layer_set_frame(bitmap_layer_get_layer(meter_bar_mask_layer), frame);

  //TimeUnits units_changed = SECOND_UNIT|MINUTE_UNIT|HOUR_UNIT|DAY_UNIT;
  TimeUnits units_changed = sec_unit;
  if (!charge_state.is_charging && blink == 1){
	layer_set_hidden(bitmap_layer_get_layer(meter_bar_layer), false);
    is_hidden = false;

	units_changed = no_sec_unit;
  }
  tick_timer_service_subscribe(units_changed, handle_tick);
}

void compose_temp(){
	int temperature = -255;
	char unit[] = "CF";
	if(units == 0){
		//modf (temp, double *integer-part)
		temperature = round((double)temp/100.00 - 273.15);
	}
	if(units == 1){
		temperature = round(((double)temp/100.00 - 273.15)* 1.8 + 32.00);
	}
	if(temp != -25500){
		snprintf(temp_buffer, sizeof(temp_buffer), "%d\u00B0%c", temperature, unit[units]);
	} else {
		snprintf(temp_buffer, sizeof(temp_buffer), "--- \u00B0%c", unit[units]);
	}
}

static void in_received_handler(DictionaryIterator *iter, void *context) {
	APP_LOG(APP_LOG_LEVEL_INFO, "Incomming Message received from JS");

	//read ACTION
	Tuple *tuple = dict_find(iter, KEY_ACTION);
	int action = tuple->value->uint8;
	
	// ACTION = 0 means that configuration message was received
	if(action == 0){
		tuple = dict_find(iter, KEY_BLINK);
		blink = tuple->value->uint8;

		// Deal with various blinking modes dependent on charging status and settings
		TimeUnits units_changed = sec_unit;
		if (blink == 1){
			layer_set_hidden(bitmap_layer_get_layer(dots_layer), false);
			if(!battery_state_service_peek().is_charging){
				units_changed = no_sec_unit;
			}
		}
		tick_timer_service_subscribe(units_changed, handle_tick);
		// Read zeroes settings
		tuple = dict_find(iter, KEY_ZEROES);
		zeroes = tuple->value->uint8;
		// Read vibrate settings
		tuple = dict_find(iter, KEY_VIBRATE);
		vibrate = tuple->value->uint8;
		// Read square settings
		tuple = dict_find(iter, KEY_SQUARE);
		square = tuple->value->uint8;
		// Read date format settings
		tuple = dict_find(iter, KEY_DATE);
		date = tuple->value->uint8;
		// Read temperature units settings
		tuple = dict_find(iter, KEY_UNITS);
		units = tuple->value->uint8;
		compose_temp();
		
		layer_mark_dirty(weather_layer);
		
		// Update display with new settings
 		time_t now = time(NULL);
		struct tm *tick_time = localtime(&now);
		update_display(tick_time, units_changed);
	}

	if(action == 1){ // ACTION = 1 means that weather update message was received
		if(weather_update_timeout_timer != NULL){
			app_timer_cancel(weather_update_timeout_timer);
			weather_update_timeout_timer = NULL;
		}

		tuple = dict_find(iter, KEY_TEMP);
		temp = tuple->value->int32;
		compose_temp();
		APP_LOG(APP_LOG_LEVEL_INFO, "Temperature value in Kelvins: %d", temp/100);

		tuple = dict_find(iter, KEY_CITY);
		strcpy(city, tuple->value->cstring);
		APP_LOG(APP_LOG_LEVEL_INFO, "City is: %s", city);

		tuple = dict_find(iter, KEY_ICON);
		strcpy(icon, tuple->value->cstring);
		APP_LOG(APP_LOG_LEVEL_INFO, "Icon is: %s", icon);

		tuple = dict_find(iter, KEY_DESC);
		strcpy(desc, tuple->value->cstring);
		APP_LOG(APP_LOG_LEVEL_INFO, "Description: %s", desc);

		layer_mark_dirty(weather_layer);
		
		update_time = time(NULL);
		weather_update_timer = app_timer_register(60 * 60 * 1000, timer_update_callback, NULL);
	}

	if(action == 2){  // ACTION = 2 means that no ansver was received from weather service
		if(weather_update_timeout_timer != NULL){
			app_timer_cancel(weather_update_timeout_timer);
			weather_update_timeout_timer = NULL;
		}
		APP_LOG(APP_LOG_LEVEL_INFO, "Trouble with weather request. Reschedule request again in 10 minutes");
		weather_update_timer = app_timer_register(10 * 60 * 1000, timer_update_callback, NULL);
	}

	if(action == 3){  // ACTION = 3 means that JS is ready to receive messages from Pebble
		JSready = true;
	}

	if(action == 4){  // ACTION = 4 means that JS was unable to retrieve GPS coordinates from the phone
		if(weather_update_timeout_timer != NULL){
			app_timer_cancel(weather_update_timeout_timer);
			weather_update_timeout_timer = NULL;
		}
		// TODO: Implement some kind of message to user about not working GPS
		APP_LOG(APP_LOG_LEVEL_INFO, "Trouble to retrieve GPS coordinates, check your cell phone");
		response_received = true;
		weather_update_timer = app_timer_register(2 * 60 * 1000, timer_update_callback, NULL);
	}
}

char *translate_error(AppMessageResult result) {
  switch (result) {
    case APP_MSG_OK: return "APP_MSG_OK";
    case APP_MSG_SEND_TIMEOUT: return "APP_MSG_SEND_TIMEOUT";
    case APP_MSG_SEND_REJECTED: return "APP_MSG_SEND_REJECTED";
    case APP_MSG_NOT_CONNECTED: return "APP_MSG_NOT_CONNECTED";
    case APP_MSG_APP_NOT_RUNNING: return "APP_MSG_APP_NOT_RUNNING";
    case APP_MSG_INVALID_ARGS: return "APP_MSG_INVALID_ARGS";
    case APP_MSG_BUSY: return "APP_MSG_BUSY";
    case APP_MSG_BUFFER_OVERFLOW: return "APP_MSG_BUFFER_OVERFLOW";
    case APP_MSG_ALREADY_RELEASED: return "APP_MSG_ALREADY_RELEASED";
    case APP_MSG_CALLBACK_ALREADY_REGISTERED: return "APP_MSG_CALLBACK_ALREADY_REGISTERED";
    case APP_MSG_CALLBACK_NOT_REGISTERED: return "APP_MSG_CALLBACK_NOT_REGISTERED";
    case APP_MSG_OUT_OF_MEMORY: return "APP_MSG_OUT_OF_MEMORY";
    case APP_MSG_CLOSED: return "APP_MSG_CLOSED";
    case APP_MSG_INTERNAL_ERROR: return "APP_MSG_INTERNAL_ERROR";
    default: return "UNKNOWN ERROR";
  }
}

static void in_dropped_handler(AppMessageResult reason, void *context) {
	// incoming message dropped
	APP_LOG(APP_LOG_LEVEL_INFO, "Incomming Message dropped because: %s", translate_error(reason));
}

static void out_sent_handler(DictionaryIterator *iter, void *context) {
	// outgoing message was delivered
	APP_LOG(APP_LOG_LEVEL_DEBUG, "Message to JS sent successfully");
}

void out_failed_handler(DictionaryIterator *failed, AppMessageResult reason, void *context) {
	// outgoing message failed
	counter++;
	APP_LOG(APP_LOG_LEVEL_INFO, "Count of failed messages to JS: %d", counter);
	APP_LOG(APP_LOG_LEVEL_DEBUG_VERBOSE, "Message send to JS failed because of: %s", translate_error(reason));
}

static void weather_layer_update_callback(Layer *me, GContext *ctx){
	// Draw place icon
	GRect bounds = image_place->bounds;
	graphics_context_set_fill_color(ctx, GColorWhite);
	graphics_fill_rect(ctx, layer_get_bounds(me), 0, GCornerNone);
	graphics_draw_bitmap_in_rect(ctx, image_place, (GRect) { .origin = {7, 4 }, .size = bounds.size });
	// Write city
	bounds = GRect(35, 4, 86, 24);
	graphics_context_set_text_color(ctx, GColorBlack);
	graphics_draw_text(ctx, city, digital_font, bounds, GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
	//Draw thermometer icon
	bounds = GRect(0, 25, 30, 30);
	graphics_draw_text(ctx, "'", weather_font, bounds, GTextOverflowModeFill, GTextAlignmentLeft,  NULL);
	// Write temperature
	bounds = GRect(35, 35, 86, 30);
	graphics_draw_text(ctx, temp_buffer, digital_font, bounds, GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft,  NULL);
	// Draw weather icon
	bounds = GRect(0, 57, 34, 34);
	//bounds = GRect(0, 0, 90, 90);
	graphics_draw_text(ctx, icon, weather_font, bounds, GTextOverflowModeFill, GTextAlignmentLeft,  NULL);
	APP_LOG(APP_LOG_LEVEL_INFO, "icon to show: %s", icon);
	// Write weather description
	bounds = GRect(35, 67, 86, 22);
	graphics_draw_text(ctx, desc, digital_font, bounds, GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft,  NULL);
	
	//clock_copy_time_string(time_text_buffer, sizeof(time_text_buffer));
	
	struct tm *tick_time = localtime(&update_time);
	char *time_format;
	if (clock_is_24h_style()) {
      time_format = "Updated: %R";
    } else {
      time_format = "Updated: %I:%M %p";
    }
	strftime(time_buffer, sizeof(time_buffer), time_format, tick_time);
	text_layer_set_text(weather_update_layer, time_buffer);
	APP_LOG(APP_LOG_LEVEL_INFO, "%s", time_buffer);
}

void request_weather_update(){
	if(JSready){
		DictionaryIterator *iter;
		app_message_outbox_begin(&iter);
		if(iter == NULL){
			return;
		}
		
		// Send action 1 - to request for weather update
		Tuplet tAction = TupletInteger(KEY_ACTION, 1);
		dict_write_tuplet(iter, &tAction);

		dict_write_end(iter);

		app_message_outbox_send();
		response_received = false;
		weather_update_timeout_timer = app_timer_register(30 * 1000, timeout_callback, NULL);
	} else {
		weather_update_timer = app_timer_register(3 * 1000, timer_update_callback, NULL);
	}
}

static void timer_update_callback(void *data){
	weather_update_timer = NULL;
	request_weather_update();
}

static void timeout_callback(void *data){
	weather_update_timeout_timer = NULL;
	APP_LOG(APP_LOG_LEVEL_INFO, "No update received in 30 seconds. Check your phone's internet connection");
	weather_update_timer = app_timer_register(10 * 60 * 1000, timer_update_callback, NULL);
}

void weather_hide(){
	if(weather_timer != NULL){
		app_timer_cancel(weather_timer);
		weather_timer = NULL;
	}
	weather_is_showing = false;
	layer_set_hidden(weather_layer, true);
	layer_set_hidden(text_layer_get_layer(weather_update_layer), true);
}

static void timer_callback (void *data){
	//kill_timer = false;
	weather_timer = NULL;
	weather_hide();
}

void weather_show(){
	compose_temp();
	layer_mark_dirty(weather_layer);
	layer_set_hidden(weather_layer, false);
	layer_set_hidden(text_layer_get_layer(weather_update_layer), false);
	weather_is_showing = true;
	weather_timer = app_timer_register(10000, timer_callback, NULL);
}

void tap_handler(AccelAxisType axis, int32_t direction) {
        if (weather_is_showing) {
                weather_hide();
        } else {
                weather_show();
        }
        //weather_is_showing = !weather_is_showing;
}

void save_persist_int(const uint32_t key, const int32_t value){
	int compare;
	if(persist_exists(key)){
		compare = persist_read_int(key);
		if(compare != value){
			persist_write_int(key, value);
		}
	} else {
		persist_write_int(key, value);
	}
}

void save_persist_string(const uint32_t key, const char *value){
	char storage_buffer[100];
	if(persist_exists(key)){
		persist_read_string(KEY_CITY, storage_buffer, sizeof(storage_buffer));
		if(strcmp(value, storage_buffer) != 0){
			persist_write_string(key, value);
		}
	} else {
		persist_write_string(key, value);
	}
}

void init(){	
	app_message_register_inbox_received(in_received_handler);
	app_message_register_inbox_dropped(in_dropped_handler);
	app_message_register_outbox_sent(out_sent_handler);
	app_message_register_outbox_failed(out_failed_handler);
	app_message_open(255, 128);
	
	window = window_create();
	window_stack_push(window, true /* Animated */);	
	Layer *window_layer = window_get_root_layer(window);
	
	if(persist_exists(KEY_BLINK)){
		blink = persist_read_int(KEY_BLINK);
	}
	if(persist_exists(KEY_ZEROES)){
    	zeroes = persist_read_int(KEY_ZEROES);
	}
	if(persist_exists(KEY_VIBRATE)){
    	vibrate = persist_read_int(KEY_VIBRATE);
	}
	if(persist_exists(KEY_SQUARE)){
		square = persist_read_int(KEY_SQUARE);
	}
	if(persist_exists(KEY_DATE)){
		date = persist_read_int(KEY_DATE);
	}
	
	if(persist_exists(KEY_UNITS)){
		units = persist_read_int(KEY_UNITS);
	}
	if(persist_exists(KEY_TEMP)){
		temp = persist_read_int(KEY_TEMP);
	}
	if(persist_exists(KEY_CITY)){
		persist_read_string(KEY_CITY, city, sizeof(city));
	} else {
		strcpy(city, "Neverwhere");
	}
	if(persist_exists(KEY_ICON)){
		persist_read_string(KEY_ICON, icon, sizeof(icon));
	} else {
		strcpy(icon, ")");
	}
	if(persist_exists(KEY_DESC)){
		persist_read_string(KEY_DESC, desc, sizeof(desc));
	} else {
		strcpy(desc, "N/A");
	}
	if(persist_exists(KEY_LAST_UPDATE)){
		update_time = persist_read_int(KEY_LAST_UPDATE);
	}
		
	background_image = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_BACKGROUND);
	background_layer = bitmap_layer_create(layer_get_frame(window_layer));
	bitmap_layer_set_bitmap(background_layer, background_image);
	layer_add_child(window_layer, bitmap_layer_get_layer(background_layer));
	
	GRect frame = GRect(0, 0, 144, 19);
	weather_update_layer = text_layer_create(frame);
	text_layer_set_background_color(weather_update_layer, GColorBlack);
	text_layer_set_text_color(weather_update_layer, GColorWhite);
	text_layer_set_font(weather_update_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD));
	text_layer_set_text_alignment(weather_update_layer, GTextAlignmentCenter);
	text_layer_set_overflow_mode(weather_update_layer, GTextOverflowModeTrailingEllipsis);
	text_layer_set_text(weather_update_layer, "Last update: Never");
	layer_set_hidden(text_layer_get_layer(weather_update_layer), true);
	layer_add_child(window_layer, text_layer_get_layer(weather_update_layer));
	
	meter_bar_image = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_METER_BAR);
	frame = (GRect){ .origin = { .x = 38, .y = 67}, .size = meter_bar_image->bounds.size};
	meter_bar_layer = bitmap_layer_create(frame);
	bitmap_layer_set_bitmap(meter_bar_layer, meter_bar_image);
	layer_add_child(window_layer, bitmap_layer_get_layer(meter_bar_layer));

	meter_bar_mask_layer = bitmap_layer_create(frame);
	bitmap_layer_set_background_color(meter_bar_mask_layer, GColorWhite);
	layer_add_child(bitmap_layer_get_layer(meter_bar_layer), bitmap_layer_get_layer(meter_bar_mask_layer));

	bt_image = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_BT);
	frame = (GRect){ .origin = { .x = 54, .y = 67}, .size = bt_image->bounds.size};
	bt_layer = bitmap_layer_create(frame);
	bitmap_layer_set_bitmap(bt_layer, bt_image);
	layer_add_child(window_layer, bitmap_layer_get_layer(bt_layer));

	frame = (GRect){{17, 38}, {109, 19}};
	day_layer = text_layer_create(frame);
	text_layer_set_background_color(day_layer, GColorClear);
	//text_layer_set_background_color(day_layer, GColorWhite);
  	text_layer_set_text_color(day_layer, GColorBlack);
	text_layer_set_text_alignment(day_layer, GTextAlignmentCenter);
  	text_layer_set_font(day_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  	text_layer_set_text(day_layer, "Su Mo Tu We Th Fr Sa");
  	layer_add_child(window_layer, text_layer_get_layer(day_layer));
	
	if(square == 0){
		frame = (GRect){{0, 15}, {15, 3}};
	} else {
		frame = (GRect){{0, 3}, {15, 13}};
	}
  	day_inv_layer = inverter_layer_create(frame);
  	layer_add_child(text_layer_get_layer(day_layer), inverter_layer_get_layer(day_inv_layer));
	
	frame = (GRect){ .origin = { .x = 15, .y = 62}, .size = {20, 14}};
	time_format_layer = text_layer_create(frame);
	text_layer_set_background_color(time_format_layer, GColorClear);
	text_layer_set_text_color(time_format_layer, GColorBlack);
	text_layer_set_font(time_format_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
	text_layer_set_text(time_format_layer, "24H");
	layer_add_child(window_layer, text_layer_get_layer(time_format_layer));
	
	dots_image = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_DOTS);
	frame = (GRect){ .origin = { .x = 70, .y = 91}, .size = dots_image->bounds.size};
	dots_layer = bitmap_layer_create(frame);
	bitmap_layer_set_bitmap(dots_layer, dots_image);
	layer_add_child(window_layer, bitmap_layer_get_layer(dots_layer));	

	digital_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_DIGITAL_16));
	
	frame = (GRect){ .origin = { .x = 65, .y = 60}, .size = {66, 40}};
	//frame = (GRect){ .origin = { .x = 75, .y = 54}, .size = {60, 22}};
	for(int i = 0; i < 2; i++){
		date_layer[i] = text_layer_create(frame);
		text_layer_set_background_color(date_layer[i], GColorClear);
		text_layer_set_text_color(date_layer[i], GColorBlack);
		text_layer_set_font(date_layer[i], digital_font);
		text_layer_set_text(date_layer[i], "JAN");
		layer_add_child(window_layer, text_layer_get_layer(date_layer[i]));
	}
	text_layer_set_text_alignment(date_layer[0], GTextAlignmentLeft);
	text_layer_set_text_alignment(date_layer[1], GTextAlignmentRight);

  for(int i = 0; i < TOTAL_TIME_DIGITS; i++){
    time_digits_images[i] = gbitmap_create_with_resource(BIG_DIGIT_IMAGE_RESOURCE_IDS[0]);
    GPoint point;
    switch (i){
      case 0:
        point = GPoint(11, 84);
        break;
      case 1:
        point = GPoint(41, 84);
        break;
      case 2:
        point = GPoint(78, 84);
        break;
      case 3:
        point = GPoint(106, 84);
        break;
    }
    frame = (GRect) { .origin = point, .size = time_digits_images[i]->bounds.size };
    time_digits_layers[i] = bitmap_layer_create(frame);
    bitmap_layer_set_bitmap(time_digits_layers[i], time_digits_images[i]);
    layer_add_child(window_layer, bitmap_layer_get_layer(time_digits_layers[i]));
  }  
	
  TimeUnits units_changed = sec_unit;
  if (blink == 1){
	units_changed = no_sec_unit;
  }
	
	time_t now = time(NULL);
	struct tm *tick_time = localtime(&now);
	update_display(tick_time, units_changed);
	seconds = now - update_time;
	
	if(seconds > 60*60 || seconds < 0){
		request_weather_update();
	} else {
		weather_update_timer = app_timer_register(1000*(60*60 - seconds), timer_update_callback, NULL);
	}
	
	tick_timer_service_subscribe(units_changed, handle_tick);
	bluetooth_connection_service_subscribe(handle_bluetooth);
	handle_bluetooth(bluetooth_connection_service_peek());
	battery_state_service_subscribe(handle_battery);
	handle_battery(battery_state_service_peek());
	accel_tap_service_subscribe(&tap_handler);

	image_place = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_PLACE);
	weather_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_METEOCONS_30));
	
	frame = GRect(11, 40, 121, 90);
	weather_layer = layer_create(frame);
	//text_layer_set_background_color(weather_layer, GColorWhite);
	layer_set_update_proc(weather_layer, weather_layer_update_callback);
	layer_add_child(window_layer, weather_layer);
	layer_set_hidden(weather_layer, true);
}

void deinit(){
	app_message_deregister_callbacks();
	
	if(weather_is_showing){
		weather_hide();
	}
	if(weather_update_timer != NULL){
		app_timer_cancel(weather_update_timer);
		weather_update_timer = NULL;
	}
	if(weather_timer != NULL){
		app_timer_cancel(weather_timer);
		weather_timer = NULL;
	}
	if(weather_update_timeout_timer != NULL){
		app_timer_cancel(weather_update_timeout_timer);
		weather_update_timeout_timer = NULL;
	}
	layer_destroy(weather_layer);
	gbitmap_destroy(image_place);
	fonts_unload_custom_font(weather_font);
	
	battery_state_service_unsubscribe();
	bluetooth_connection_service_unsubscribe();
	tick_timer_service_unsubscribe();

	save_persist_int(KEY_BLINK, blink);
	save_persist_int(KEY_ZEROES, zeroes);
	save_persist_int(KEY_VIBRATE, vibrate);
	save_persist_int(KEY_SQUARE, square);
	save_persist_int(KEY_DATE, date);
	save_persist_int(KEY_UNITS, units);
	save_persist_int(KEY_TEMP, temp);
	save_persist_int(KEY_LAST_UPDATE, update_time);
	
	save_persist_string(KEY_ICON, icon);
	save_persist_string(KEY_CITY, city);
	save_persist_string(KEY_DESC, desc);

	for(int i = 0; i < TOTAL_TIME_DIGITS; i++){
		layer_remove_from_parent(bitmap_layer_get_layer(time_digits_layers[i]));
		bitmap_layer_destroy(time_digits_layers[i]);
		gbitmap_destroy(time_digits_images[i]);
	}

	for(int i = 0; i < 2; i++){
		layer_remove_from_parent(text_layer_get_layer(date_layer[i]));
		text_layer_destroy(date_layer[i]);
	}
	fonts_unload_custom_font(digital_font);

  layer_remove_from_parent(bitmap_layer_get_layer(dots_layer));
  bitmap_layer_destroy(dots_layer);
  gbitmap_destroy(dots_image);

	layer_remove_from_parent(text_layer_get_layer(time_format_layer));
	text_layer_destroy(time_format_layer);
	
  inverter_layer_destroy(day_inv_layer);
  text_layer_destroy(day_layer);
	
  layer_remove_from_parent(bitmap_layer_get_layer(bt_layer));
  bitmap_layer_destroy(bt_layer);
  gbitmap_destroy(bt_image);

  layer_remove_from_parent(bitmap_layer_get_layer(meter_bar_mask_layer));
  bitmap_layer_destroy(meter_bar_mask_layer);
  layer_remove_from_parent(bitmap_layer_get_layer(meter_bar_layer));
  bitmap_layer_destroy(meter_bar_layer);
  gbitmap_destroy(meter_bar_image); 

	layer_remove_from_parent(text_layer_get_layer(weather_update_layer));
	text_layer_destroy(weather_update_layer);
	
  layer_remove_from_parent(bitmap_layer_get_layer(background_layer));
  bitmap_layer_destroy(background_layer);
  gbitmap_destroy(background_image);
  window_destroy(window);
	
  app_message_deregister_callbacks();
}

int main(void){
  init();
  app_event_loop();
  deinit();
}