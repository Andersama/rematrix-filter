#pragma once
#include <obs-module.h>
#include <stdio.h>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("rematrix-filter", "en-US")

#define MT_ obs_module_text

long long get_obs_output_channels() {
	// get channel number from output speaker layout set by obs
	struct obs_audio_info aoi;
	obs_get_audio_info(&aoi);
	long long recorded_channels = get_audio_channels(aoi.speakers);
	return recorded_channels;
}

struct rematrix_data {
	obs_source_t *context;
	size_t channels;
	//store the routing information
	long route[MAX_AUDIO_CHANNELS];
};

static const char *rematrix_name(void *unused) {
	UNUSED_PARAMETER(unused);
	return MT_("Rematrix");
}

static void rematrix_destroy(void *data) {
	struct rematrix_data *rematrix = data;
	bfree(rematrix);
}

static void rematrix_update(void *data, obs_data_t *settings) {
	struct rematrix_data *rematrix = data;

	rematrix->channels = audio_output_get_channels(obs_get_audio());

	bool route_changed = false;
	long route[MAX_AUDIO_CHANNELS];

	//make enough space for c strings
	int pad_digits = (int)floor(log10(abs(MAX_AUDIO_CHANNELS))) + 1;

	//template out the route format
	const char* route_name_format = "route %i";
	char* route_name = (char *)calloc(strlen(route_name_format) + pad_digits,
		sizeof(char));

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

static void *rematrix_create(obs_data_t *settings, obs_source_t *filter) {
	struct rematrix_data *rematrix = bzalloc(sizeof(*rematrix));
	rematrix->context = filter;
	rematrix_update(rematrix, settings);
	return rematrix;
}

static struct obs_audio_data *rematrix_filter_audio(void *data,
	struct obs_audio_data *audio) {

	//initialize once, optimize for fast use
	static volatile long long route[MAX_AUDIO_CHANNELS];

	struct rematrix_data *rematrix = data;
	const size_t channels = rematrix->channels;
	uint8_t *rematrixed_data[MAX_AUDIO_CHANNELS];
	uint8_t **adata = (uint8_t**)audio->data;
	size_t ch_buffer = (audio->frames * sizeof(float));

	//prevent race condition
	for (size_t c = 0; c < channels; c++)
		route[c] = rematrix->route[c];

	//create the new buffer
	for (size_t c = 0; c < channels; c++) {
		if (route[c] < channels && route[c] >= 0)
			rematrixed_data[c] = (uint8_t*)bmemdup(adata[route[c]],
				ch_buffer);
		//not a valid route, mute
		else {
			rematrixed_data[c] = (uint8_t*)bmalloc(ch_buffer); //(uint8_t*)calloc(1, ch_buffer);
			memset(rematrixed_data[c], 0, ch_buffer);
		}
	}
	
	//memcpy data back into place
	for (size_t c = 0; c < channels; c++) {
		memcpy(adata[c],rematrixed_data[c],ch_buffer);
		//free temporary buffer
		if (rematrixed_data[c]) {
			//don't memory leak
			bfree(rematrixed_data[c]);
		}
	}

	return audio;
}

static void rematrix_defaults(obs_data_t *settings)
{
	//make enough space for c strings
	int pad_digits = (int)floor(log10(abs(MAX_AUDIO_CHANNELS))) + 1;

	//template out the route format
	const char* route_name_format = "route %i";
	char* route_name = (char *)calloc(strlen(route_name_format) + pad_digits,
		sizeof(char));

	//default is no routing (ordered) -1 or any out of bounds is mute*
	for (long long i = 0; i < MAX_AUDIO_CHANNELS; i++) {
		sprintf(route_name, route_name_format, i);
		obs_data_set_default_int(settings, route_name, i);
	}

	//don't memory leak
	free(route_name);
}

static bool fill_out_channels(obs_properties_t *props, obs_property_t *list,
	obs_data_t *settings) {
	
	obs_property_list_clear(list);
	obs_property_list_add_int(list, MT_("mute"), -1);
	long long channels = get_obs_output_channels();

	//make enough space for c strings
	int pad_digits = (int)floor(log10(abs(MAX_AUDIO_CHANNELS))) + 1;

	//template out the format for the json
	const char* route_obs_format = "in.ch.%i";
	char* route_obs = (char *)calloc(strlen(route_obs_format) + pad_digits, 
		sizeof(char));

	for (long long c = 0; c < channels; c++) {
		sprintf(route_obs, route_obs_format, c);
		obs_property_list_add_int(list, route_obs , c);
	}

	//don't memory leak
	free(route_obs);

	return true;
}

static obs_properties_t *rematrix_properties(void *data)
{
	UNUSED_PARAMETER(data);

	obs_properties_t *props = obs_properties_create();

	//make a list long enough for the maximum # of chs
	obs_property_t *route[MAX_AUDIO_CHANNELS];
	
	size_t channels = audio_output_get_channels(obs_get_audio());

	//make enough space for c strings
	int pad_digits = (int)floor(log10(abs(MAX_AUDIO_CHANNELS))) + 1;

	//template out the route format
	const char* route_name_format = "route %i";
	char* route_name = (char *)calloc(strlen(route_name_format) + pad_digits,
		sizeof(char));

	//template out the format for the json
	const char* route_obs_format = "out.ch.%i";
	char* route_obs = (char *)calloc(strlen(route_obs_format) + pad_digits,
		sizeof(char));

	//add an appropriate # of options to mix from
	for (size_t i = 0; i < channels; i++) {
		sprintf(route_name, route_name_format, i);
		sprintf(route_obs, route_obs_format, i);
		route[i] = obs_properties_add_list(props, route_name, MT_(route_obs),
			OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
		obs_property_set_long_description(route[i], MT_("rematrix route"));
		obs_property_set_modified_callback(route[i], fill_out_channels);
	}

	//don't memory leak
	free(route_name);
	free(route_obs);

	return props;
}

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
