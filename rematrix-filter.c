/******************************************************************************
    Copyright (C) 2018 by Alex Anderson <anderson.john.alexander@gmail.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/

#pragma once
#include <obs-frontend-api.h>
#include <util/platform.h>
#include <util/dstr.h>
#include <obs-internal.h>
#include <obs-module.h>
#include <stdio.h>
#include <obs-hotkey.h>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("rematrix-filter", "en-US")

#define MT_ obs_module_text

#ifndef MAX_AUDIO_SIZE
#ifndef AUDIO_OUTPUT_FRAMES
#define	AUDIO_OUTPUT_FRAMES 1024
#endif
#define	MAX_AUDIO_SIZE (AUDIO_OUTPUT_FRAMES * sizeof(float))
#endif // !MAX_AUDIO_SIZE

#define STR_LEN(str1,str2) (strlen(str1) + strlen(str2) + 1)*sizeof(char)

/*****************************************************************************/
struct rematrix_data {
	char* profile_name;
	obs_source_t *context;
	size_t channels;
	//store the routing information
	long route[MAX_AUDIO_CHANNELS];
	//store a temporary buffer
	uint8_t *tmpbuffer[MAX_AUDIO_CHANNELS];

	obs_data_t* settings;

	DARRAY(obs_hotkey_id) profile_hotkeys;
	DARRAY(char*) profile_names;
};

struct hotkey_cb {
	const char* file_path;
	void* data;
};

/*****************************************************************************/
long long get_obs_output_channels();
static const char *rematrix_name(void *unused);
static void rematrix_destroy(void *data);
static void rematrix_update(void *data, obs_data_t *settings);
static void *rematrix_create(obs_data_t *settings, obs_source_t *filter);
static struct obs_audio_data *rematrix_filter_audio(void *data,
	struct obs_audio_data *audio);
static void rematrix_defaults(obs_data_t *settings);
static bool fill_out_channels(obs_properties_t *props, obs_property_t *list,
	obs_data_t *settings);
void rematrix_on_hotkey(struct hotkey_cb* cb_data);
static bool load_hotkey(const char* profile_name, void* data);
static bool attach_hotkey(const char* profile_name, void* data);
static bool add_hotkey(obs_properties_t *props, obs_property_t *property,
	void* data);
static obs_properties_t *rematrix_properties(void *data);

/*****************************************************************************/
long long get_obs_output_channels() {
	// get channel number from output speaker layout set by obs
	struct obs_audio_info aoi;
	obs_get_audio_info(&aoi);
	long long recorded_channels = get_audio_channels(aoi.speakers);
	return recorded_channels;
}

char* get_scene_data_path() {
	const char* scene = obs_frontend_get_current_scene_collection();
	const char* config_path = os_get_config_path_ptr("obs-studio\\basic\\profiles");
	long len = strlen(scene) + strlen("\\") + strlen(config_path) + 1;

	char *scene_data_path = malloc(len);
	sprintf(scene_data_path, "%s\\%s", config_path, scene);

	return scene_data_path;
}

/*****************************************************************************/
static const char *rematrix_name(void *unused) {
	UNUSED_PARAMETER(unused);
	return MT_("Rematrix");
}

/*****************************************************************************/
static void rematrix_destroy(void *data) {
	struct rematrix_data *rematrix = data;
	//free temp buffers
	for (size_t i = 0; i < rematrix->channels; i++) {
		if (rematrix->tmpbuffer[i])
			bfree(rematrix->tmpbuffer[i]);
	}

	//free the hotkey array
	da_free(rematrix->profile_hotkeys);

	//free the profile name array
	for (size_t i = 0; i < rematrix->profile_names.num; i++) {
		if (rematrix->profile_names.array[i])
			free(rematrix->profile_names.array[i]);
	}

	da_free(rematrix->profile_names);

	bfree(rematrix);
}

/*****************************************************************************/
static void rematrix_clear_hotkeys(void *data) {
	struct rematrix_data *rematrix = data;
	for (size_t i = 0; i < rematrix->profile_hotkeys.num; i++) {
		obs_hotkey_unregister(rematrix->profile_hotkeys.array[i]);
	}
	da_free(rematrix->profile_hotkeys);

	obs_data_array_t *empty = obs_data_array_create();
	obs_data_set_array(rematrix->settings, "profile_names", empty);
	obs_data_array_release(empty);
}

/*****************************************************************************/
static void rematrix_update(void *data, obs_data_t *settings) {
	struct rematrix_data *rematrix = data;

	rematrix->profile_name = obs_data_get_string(settings, "profile_name");
	rematrix->channels = audio_output_get_channels(obs_get_audio());

	bool route_changed = false;
	long route[MAX_AUDIO_CHANNELS];

	//make enough space for c strings
	int pad_digits = (int)floor(log10(abs(MAX_AUDIO_CHANNELS))) + 1;

	//template out the route format
	const char* route_name_format = "route %i";
	size_t route_len = strlen(route_name_format) + pad_digits;
	char* route_name = (char *)calloc(route_len, sizeof(char));

	//copy the routing over from the settings
	for (long long i = 0; i < MAX_AUDIO_CHANNELS; i++) {
		sprintf(route_name, route_name_format, i);
		route[i] = (int)obs_data_get_int(settings, route_name);
		if (rematrix->route[i] != route[i]) {
			rematrix->route[i] = route[i];
			route_changed = true;
		}
	}

	//don't memory leak
	free(route_name);
}

/*****************************************************************************/
static void *rematrix_create(obs_data_t *settings, obs_source_t *filter) {
	struct rematrix_data *rematrix = bzalloc(sizeof(*rematrix));
	rematrix->context = filter;
	rematrix->settings = settings;
	da_init(rematrix->profile_hotkeys);
	da_init(rematrix->profile_names);

	rematrix_update(rematrix, settings);

	for (size_t i = 0; i < rematrix->channels; i++) {
		rematrix->tmpbuffer[i] = bzalloc(MAX_AUDIO_SIZE);
	}

	obs_data_array_t *profile_names = obs_data_get_array(settings, "profile_names");
	size_t profile_count = obs_data_array_count(profile_names);
	for (size_t i = 0; i < profile_count; i++) {
		obs_data_t *profile_item = obs_data_array_item(profile_names, i);
		const char *profile_name = obs_data_get_string(profile_item, "name");
		if (!load_hotkey(profile_name, rematrix)) {
			//obs_data_array_erase(profile_names, i);
		}
		obs_data_release(profile_item);
	}
	obs_data_array_release(profile_names);
	
	return rematrix;
}

/*****************************************************************************/
static struct obs_audio_data *rematrix_filter_audio(void *data,
	struct obs_audio_data *audio) {

	//initialize once, optimize for fast use
	static volatile long long route[MAX_AUDIO_CHANNELS];

	struct rematrix_data *rematrix = data;
	const size_t channels = rematrix->channels;
	uint8_t **rematrixed_data = (uint8_t**)rematrix->tmpbuffer;
	uint8_t **adata = (uint8_t**)audio->data;
	size_t ch_buffer = (audio->frames * sizeof(float));

	//prevent race condition
	for (size_t c = 0; c < channels; c++)
		route[c] = rematrix->route[c];

	uint32_t frames = audio->frames;
	size_t copy_size = 0;
	size_t copy_index = 0;
	//consume AUDIO_OUTPUT_FRAMES or less # of frames
	for (size_t chunk = 0; chunk < frames; chunk+=AUDIO_OUTPUT_FRAMES) {
		//calculate the byte address we're copying to / from 
		//relative to the original data
		copy_index = chunk * sizeof(float);

		//calculate the size of the data we're about to try to copy
		if (frames - chunk < AUDIO_OUTPUT_FRAMES)
			copy_size = frames - chunk;
		else
			copy_size = AUDIO_OUTPUT_FRAMES;
		copy_size *= sizeof(float);

		//copy data to temporary buffer
		for (size_t c = 0; c < channels; c++) {
			//valid route copy data to temporary buffer
			if (route[c] >= 0 && route[c] < channels)
				memcpy(rematrixed_data[c], 
					&adata[route[c]][copy_index],
					copy_size);
			//not a valid route, mute
			else
				memset(rematrixed_data[c], 0, MAX_AUDIO_SIZE);
		}

		//memcpy data back into place
		for (size_t c = 0; c < channels; c++) {
			memcpy(&adata[c][copy_index], rematrixed_data[c],
				copy_size);
		}
		//move to next chunk of unprocessed data
	}
	return audio;
}

/*****************************************************************************/
static void rematrix_defaults(obs_data_t *settings)
{
	//make enough space for c strings
	int pad_digits = (int)floor(log10(abs(MAX_AUDIO_CHANNELS))) + 1;

	//template out the route format
	const char* route_name_format = "route %i";
	size_t route_len = strlen(route_name_format) + pad_digits;
	char* route_name = (char *)calloc(route_len, sizeof(char));

	//default is no routing (ordered) -1 or any out of bounds is mute*
	for (long long i = 0; i < MAX_AUDIO_CHANNELS; i++) {
		sprintf(route_name, route_name_format, i);
		obs_data_set_default_int(settings, route_name, i);
	}

	obs_data_set_default_string(settings, "profile_name", MT_("Default"));

	//don't memory leak
	free(route_name);
}

/*****************************************************************************/
static bool fill_out_channels(obs_properties_t *props, obs_property_t *list,
	obs_data_t *settings) {
	
	obs_property_list_clear(list);
	obs_property_list_add_int(list, MT_("mute"), -1);
	long long channels = get_obs_output_channels();

	//make enough space for c strings
	int pad_digits = (int)floor(log10(abs(MAX_AUDIO_CHANNELS))) + 1;

	//template out the format for the json
	const char* route_obs_format = "in.ch.%i";
	size_t route_obs_len = strlen(route_obs_format) + pad_digits;
	char* route_obs = (char *)calloc(route_obs_len, sizeof(char));

	for (long long c = 0; c < channels; c++) {
		sprintf(route_obs, route_obs_format, c);
		obs_property_list_add_int(list, route_obs , c);
	}

	//don't memory leak
	free(route_obs);

	return true;
}

/*****************************************************************************/
void rematrix_on_hotkey(struct hotkey_cb* cb_data) {
	obs_data_t *settings = obs_data_create_from_json_file_safe(
		cb_data->file_path, "bak");

	struct rematrix_data *rematrix = (struct rematrix_data *)cb_data->data;

	bool route_changed = false;
	long route[MAX_AUDIO_CHANNELS];

	//make enough space for c strings
	int pad_digits = (int)floor(log10(abs(MAX_AUDIO_CHANNELS))) + 1;

	//template out the route format
	const char* route_name_format = "route %i";
	size_t route_len = strlen(route_name_format) + pad_digits;
	char* route_name = (char *)calloc(route_len, sizeof(char));

	//default is no routing (ordered) -1 or any out of bounds is mute*
	for (long long i = 0; i < MAX_AUDIO_CHANNELS; i++) {
		sprintf(route_name, route_name_format, i);
		route[i] = obs_data_get_int(settings, route_name);
		obs_data_set_int(rematrix->settings, route_name, route[i]);
	}
	free(route_name);

	const char* profile_name = obs_data_get_string(settings,
		"profile_name");
	obs_data_set_string(rematrix->settings, "profile_name", profile_name);

	rematrix_update(cb_data->data, rematrix->settings);
	obs_data_release(settings);
	//bfree(settings);
}

/*****************************************************************************/
static bool load_hotkey(const char* profile_name, void* data) {
	struct rematrix_data *rematrix = (struct rematrix_data *)data;
	bool profile_exists = false;
	bool hotkey_created = false;
	//prevent duplicates
	for (size_t i = 0; i < rematrix->profile_names.num; i++) {
		if (strcmp(rematrix->profile_names.array[i], profile_name) == 0) {
			profile_exists = true;
			return false;
		}
	}

	size_t profile_len = strlen(profile_name);

	const char* scene_data_path = get_scene_data_path();
	size_t path_len = strlen(scene_data_path);

	size_t target_len = path_len + profile_len + strlen("\\.json") + 1;

	//template out filepath
	const char* path_format = "%s\\%s.json";
	char* file_path = malloc(target_len);
	sprintf(file_path, path_format, scene_data_path, profile_name);

	bool settings_exist = os_file_exists(file_path);
	if (!settings_exist) {
		return hotkey_created;
	} else {

		const char* desc_format = "Load Profile - %s";
		char* desc_str = malloc(STR_LEN(desc_format, profile_name));
		sprintf(desc_str, desc_format, profile_name);

		struct hotkey_cb *cb_data = malloc(sizeof(struct hotkey_cb));
		cb_data->data = data;
		cb_data->file_path = strdup(file_path);

		bool is_private = rematrix->context->context.private;
		rematrix->context->context.private = false;
		obs_hotkey_id hotkey_id = obs_hotkey_register_source(rematrix->context,
			profile_name, desc_str, rematrix_on_hotkey, cb_data);

		if (hotkey_id == OBS_INVALID_HOTKEY_ID) {
			hotkey_created = false;
		}
		else {
			//push the hotkey id into dynamic array
			char* dprofile_name = strdup(profile_name);
			da_push_back(rematrix->profile_hotkeys, &hotkey_id);
			da_push_back(rematrix->profile_names, &dprofile_name);

			hotkey_created = true;
		}
		//obs_data_array_release(profile_names);
		free(desc_str);
	}

	return hotkey_created;
}

/*****************************************************************************/
static bool attach_hotkey(const char* profile_name, void* data) {

	struct rematrix_data *rematrix = (struct rematrix_data *)data;
	bool profile_exists = false;
	bool hotkey_created = false;
	//prevent duplicates
	for (size_t i = 0; i < rematrix->profile_names.num; i++) {
		if (strcmp(rematrix->profile_names.array[i], profile_name) == 0) {
			profile_exists = true;
			break;
		}
	}
	//create a settings object to make into json file
	obs_data_t *settings = obs_data_create();

	size_t profile_len = strlen(profile_name);

	const char* scene_data_path = get_scene_data_path();
	size_t path_len = strlen(scene_data_path);

	size_t target_len = path_len + profile_len + strlen("\\.json") + 1;

	//template out filepath
	const char* path_format = "%s\\%s.json";
	char* file_path = malloc(target_len);
	sprintf(file_path, path_format, scene_data_path, profile_name);

	//make enough space for c strings
	int pad_digits = (int)floor(log10(abs(MAX_AUDIO_CHANNELS))) + 1;

	const char* route_name_format = "route %i";
	size_t route_len = strlen(route_name_format) + pad_digits;
	char* route_name = (char *)calloc(route_len, sizeof(char));

	for (long long i = 0; i < MAX_AUDIO_CHANNELS; i++) {
		sprintf(route_name, route_name_format, i);
		obs_data_set_int(settings, route_name, rematrix->route[i]);
	}

	obs_data_set_string(settings, "profile_name", profile_name);

	obs_data_save_json_safe(settings, file_path, "tmp", "bak");

	if (!profile_exists) {
		obs_data_array_t *profile_names = obs_data_get_array(rematrix->settings, "profile_names");
		size_t profile_count = obs_data_array_count(profile_names);

		const char* desc_format = "Load Profile - %s";
		char* desc_str = malloc(STR_LEN(desc_format, profile_name));
		sprintf(desc_str, desc_format, profile_name);

		struct hotkey_cb *cb_data = malloc(sizeof(struct hotkey_cb));
		cb_data->data = data;
		cb_data->file_path = strdup(file_path);

		bool is_private = rematrix->context->context.private;
		rematrix->context->context.private = false;
		obs_hotkey_id hotkey_id = obs_hotkey_register_source(rematrix->context,
			profile_name, desc_str, rematrix_on_hotkey, cb_data);
		//rematrix->context->context.private = is_private;
		/*
		obs_hotkey_id hotkey_id = obs_hotkey_register_frontend(
			profile_name, desc_str, rematrix_on_hotkey, cb_data);
			*/
		if (hotkey_id == OBS_INVALID_HOTKEY_ID) {
			hotkey_created = false;
		} else {
			//push the hotkey id into dynamic array
			char* dprofile_name = strdup(profile_name);
			da_push_back(rematrix->profile_hotkeys, &hotkey_id);
			da_push_back(rematrix->profile_names, &dprofile_name);

			bool found = false;
			for (size_t i = 0; i < profile_count; i++) {
				obs_data_t *profile_item = obs_data_array_item(profile_names, i);
				const char *data_profile_name = obs_data_get_string(profile_item, "name");
				if (strcmp(profile_name, data_profile_name) == 0) {
					//obs_data_array_erase(profile_names, i);
					found = true;
					obs_data_release(profile_item);
					break;
				}
				obs_data_release(profile_item);
			}

			if (!found) {
				obs_data_t* profile_object = obs_data_create();
				obs_data_set_string(profile_object, "name", dprofile_name);
				obs_data_array_push_back(profile_names, profile_object);
				obs_data_set_array(rematrix->settings, "profile_names", profile_names);
				obs_data_release(profile_object);
			}

			hotkey_created = true;
		}
		obs_data_array_release(profile_names);
		free(desc_str);
	}

	obs_data_release(settings);//bfree(settings);
	free(route_name);
	free(file_path);

	return hotkey_created;
}

/*****************************************************************************/
static bool add_hotkey(obs_properties_t *props, obs_property_t *property,
	void* data) {
	struct rematrix_data *rematrix = data;
	int rc = attach_hotkey(rematrix->profile_name, data);

	return true;
}

/*****************************************************************************/
static bool profile_changed(obs_properties_t *props, obs_property_t *property,
	obs_data_t *settings) {

	const char* profile_name = obs_data_get_string(settings,
		"profile_name");

	obs_data_set_default_string(settings, "profile_name", MT_("Default"));

	return true;
}

/*****************************************************************************/
static obs_properties_t *rematrix_properties(void *data)
{
	UNUSED_PARAMETER(data);

	obs_properties_t *props = obs_properties_create();

	//make a list long enough for the maximum # of chs
	obs_property_t *route[MAX_AUDIO_CHANNELS];
	obs_property_t *add_hotkey_button;
	obs_property_t *clear_hotkeys_button;
	obs_property_t *profile_name_text;
	
	size_t channels = audio_output_get_channels(obs_get_audio());

	//make enough space for c strings
	int pad_digits = (int)floor(log10(abs(MAX_AUDIO_CHANNELS))) + 1;

	//template out the route format
	const char* route_name_format = "route %i";
	size_t route_len = strlen(route_name_format) + pad_digits;
	char* route_name = (char *)calloc(route_len, sizeof(char));

	//template out the format for the json
	const char* route_obs_format = "out.ch.%i";
	size_t route_obs_len = strlen(route_obs_format) + pad_digits;
	char* route_obs = (char *)calloc(route_obs_len, sizeof(char));

	profile_name_text = obs_properties_add_text(props, "profile_name", MT_("Profile Name"), OBS_TEXT_DEFAULT);
	obs_property_set_modified_callback(profile_name_text, profile_changed);
	
	add_hotkey_button = obs_properties_add_button(props, "add_hotkey", MT_("Add Hotkey"), add_hotkey);
	//obs_property_t *profile_names = obs_properties_add_editable_list(props, "profile_names", "", OBS_EDITABLE_LIST_TYPE_STRINGS, "", "");

	//add an appropriate # of options to mix from
	for (size_t i = 0; i < channels; i++) {
		sprintf(route_name, route_name_format, i);
		sprintf(route_obs, route_obs_format, i);
		route[i] = obs_properties_add_list(props, route_name,
		    MT_(route_obs), OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_INT);

		obs_property_set_long_description(route[i],
		    MT_("tooltip"));

		obs_property_set_modified_callback(route[i],
		    fill_out_channels);
	}
	
	clear_hotkeys_button = obs_properties_add_button(props, "clear_hotkeys", MT_("Clear Hotkeys"), rematrix_clear_hotkeys);

	//don't memory leak
	free(route_name);
	free(route_obs);

	return props;
}

/*****************************************************************************/
bool obs_module_load(void)
{
	struct obs_source_info rematrixer_filter = {
		.id = "rematrix_filter",
		.type = OBS_SOURCE_TYPE_FILTER,
		.output_flags = OBS_SOURCE_AUDIO,
		.get_name = rematrix_name,
		.create = rematrix_create,
		.destroy = rematrix_destroy,
		.update = rematrix_update,
		.filter_audio = rematrix_filter_audio,
		.get_defaults = rematrix_defaults,
		.get_properties = rematrix_properties,
	};

	obs_register_source(&rematrixer_filter);
	return true;
}
